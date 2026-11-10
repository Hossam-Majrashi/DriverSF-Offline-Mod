#!/usr/bin/env python3
"""Reversible, offline DSF v48 Wwise/BIKi audio filter. Standard library only.

Never delete banks, change media sizes, replace compressed Vorbis packets, or
remove events. Sound-node gain retains timing and callbacks. Music ADPCM is
zeroed without changing headers. Bink video packets are copied verbatim.
"""
from pathlib import Path
import argparse
import collections
import csv
import fcntl
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
GAME = HERE.parents[1]
LIBRARY = GAME.parent / 'DSF_Audio'
BACKUP = GAME / 'mod/backups/audio_filter_v1/original'
STAGED = HERE / 'prepared'
GAIN = struct.pack('<6f', -200.0, 0.0, 0.0, -200.0, 0.0, 0.0)
MUTE_ACTION = struct.pack('<I', 0x06011)  # v48 Mute_O, same fixed-size value-action layout
MALE_NAMES = {'dritanner', 'pasjones', 'nomcyrusgelt', 'nomfoghorn',
              'nomkarlkrug', 'nomcharlesjerich', 'charlesjericho',
              'nomjonathandoe', 'nomramonfernande', 'nomkrazyp', 'nompolarbear'}
FEMALE_NAMES = {'lailasharan', 'nomlailasharan', 'sarahallen', 'nomsarahallen',
                'jessicaadams', 'nomjessicaadams', 'mindycatherson',
                'nomkathysullivan', 'nombeckieparamed'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(4 << 20), b''):
            h.update(b)
    return h.hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    os.replace(temp, path)


def game_path(relative):
    path = (GAME / relative).resolve()
    require(path.is_relative_to(GAME) and path.is_file(), 'unsafe/missing source: ' + relative)
    return path


def stopped():
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        try:
            args = (proc / 'cmdline').read_bytes().split(b'\0')
            names = [a.decode(errors='replace').replace('\\', '/').rsplit('/', 1)[-1].lower()
                     for a in args]
            comm = (proc / 'comm').read_text().strip().lower()
            require(comm != 'driver.exe' and 'driver.exe' not in names,
                    'Close Driver.exe before modifying game assets')
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass


def speaker(label):
    match = re.search(r'_([^_]+)_\d+_\d+_\d+$', label)
    return match[1] if match else ''


def voice_class(row):
    """Last speaker field only. Earlier fields identify conversational context.
    Classification only: the current selection policy retains unknown speakers.
    """
    labels = row.get('cue_labels', [])
    if not labels:
        return 'uncertain_speaker'
    decisions = []
    for label in labels:
        name = speaker(label).lower()
        if re.fullmatch(r'(?:vps|cps|vdr|nom)?f(?:wt|bk|ch|as|la|fi|ot|mi)\d+', name) or name in FEMALE_NAMES:
            decisions.append('female_speaker_tag')
        elif re.fullmatch(r'(?:vps|cps|vdr|nom)?m(?:wt|bk|ch|as|la|fi|ot|mi)\d+', name) or name in MALE_NAMES:
            decisions.append('retained_male_speaker_tag')
        elif name == 'mrblanky' and row.get('declared_samples') == 0:
            decisions.append('empty_placeholder')
        else:
            decisions.append('uncertain_speaker')
    for decision in ('female_speaker_tag', 'uncertain_speaker', 'retained_male_speaker_tag'):
        if decision in decisions:
            return decision
    return 'empty_placeholder'


def sections(raw):
    require(raw[:4] == b'BKHD' and struct.unpack_from('<I', raw, 8)[0] == 48,
            'only Wwise v48 is supported')
    pos = 0
    while pos + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, pos)
        start, end = pos + 8, pos + 8 + size
        require(end <= len(raw), 'bank section out of bounds')
        yield tag, start, end
        pos = end
    require(pos == len(raw), 'bank has unparsed trailing bytes')


def objects(raw):
    for tag, start, end in sections(raw):
        if tag != b'HIRC':
            continue
        count = struct.unpack_from('<I', raw, start)[0]
        pos = start + 4
        for _ in range(count):
            require(pos + 12 <= end, 'HIRC header truncated')
            typ, size, ident = struct.unpack_from('<III', raw, pos)
            limit = pos + 8 + size
            require(size >= 4 and limit <= end, 'HIRC object out of bounds')
            yield typ, ident, pos, limit
            pos = limit
        require(pos == end, 'HIRC object count mismatch')


def source_prefix(raw, pos, end):
    plugin, stream, source, fileid = struct.unpack_from('<4I', raw, pos)
    require(stream in (0, 1, 2), 'unknown stream type')
    pos += 17 + (8 if stream != 1 else 0)
    if plugin & 0xF in (2, 5):
        size = struct.unpack_from('<I', raw, pos)[0]
        pos += 4 + size
    require(pos <= end, 'source prefix exceeds object')
    return plugin, source, fileid, pos


def node_volume(raw, pos, end):
    """v48 FX + parent/priority + ranged Volume/LFE. Cross-checked with wwiser."""
    require(pos + 2 <= end, 'truncated FX prefix')
    override, count = raw[pos:pos + 2]
    require(override in (0, 1) and count <= 8, 'unknown FX prefix')
    pos += 2
    if count:
        pos += 1  # bitsFXBypass
        for _ in range(count):
            # uFXIndex + fxID + bIsRendered + ulPresetSize + preset + ulNumBankData
            require(pos + 10 <= end, 'truncated FX data')
            size = struct.unpack_from('<I', raw, pos + 6)[0]
            pos += 10 + size
            entries = struct.unpack_from('<I', raw, pos)[0]
            pos += 4 + entries * 8
    pos += 8 + 4  # OverrideBusId, DirectParentID, four priority bytes
    require(pos + 52 <= end, 'truncated initial parameters')
    values = struct.unpack_from('<12f', raw, pos)
    require(all(math.isfinite(x) and abs(x) < 1e7 for x in values), 'invalid node parameters')
    return pos


def sound_prefix(raw, start, end):
    plugin, source, fileid, pos = source_prefix(raw, start + 12, end)
    return plugin, source, fileid, node_volume(raw, pos, end)


def music_track_prefix(raw, start, end):
    pos = start + 12
    count = struct.unpack_from('<I', raw, pos)[0]
    pos += 4
    refs = set()
    for _ in range(count):
        plugin, source, fileid, pos = source_prefix(raw, pos, end)
        refs.update((source, fileid))
    count = struct.unpack_from('<I', raw, pos)[0]
    pos += 4 + count * 40 + (4 if count else 0)
    return refs, node_volume(raw, pos, end)


def collect_music_refs(manifest):
    refs = {o['uid'] for row in manifest['entries'] if row['category'] == 'Music'
            for o in row['origins']}
    for bank in manifest['banks']:
        raw = (LIBRARY / bank['file']).read_bytes()
        for typ, ident, start, end in objects(raw):
            if typ == 11:
                track_refs, _ = music_track_prefix(raw, start, end)
                refs.update(track_refs)
    return refs


def selection_decision(row, music_refs):
    if row['category'] == 'Dialogue':
        decision = voice_class(row)
        return ('retained_uncertain_speaker' if decision == 'uncertain_speaker' else decision)
    if row['category'] == 'Music' or any(o.get('language', 'Shared') == 'Shared'
                                       and o.get('uid') in music_refs for o in row['origins']):
        return 'music_reference'
    return 'retained_vehicle_audio' if row['category'] == 'Vehicles' else 'retained_shared_effect'


def pcm_data(raw):
    require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'not PCM RIFF')
    pos, valid = 12, False
    while pos + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, pos)
        start, end = pos + 8, pos + 8 + size
        require(end <= len(raw), 'truncated PCM chunk')
        if tag == b'fmt ':
            fmt, channels, rate, byte_rate, align, bits = struct.unpack_from('<HHIIHH', raw, start)
            valid = fmt in (1, 65534) and bits == 16 and align == channels * 2
        if tag == b'data':
            require(valid and size % align == 0, 'unsupported PCM layout')
            return start, size
        pos = end + (size & 1)
    raise ValueError('PCM data not found')


def check_local_voice_tail(raw, volume, end):
    """Fail closed if a localized sound has states/RTPCs or an unknown layout.
    Positioning may inherit, or explicitly use the standard 2D layout.
    """
    pos = volume + 48
    require(struct.unpack_from('<I', raw, pos)[0] == 0, 'voice has a state group')
    pos += 4
    flags = raw[pos]
    pos += 1
    if flags & 1:
        pos += 12
        require(raw[pos] == 0, 'unhandled localized 3D positioning')
        pos += 2
    pos += 7  # AdvSettingsParams
    pos += 1  # eStateSyncType
    require(struct.unpack_from('<H', raw, pos)[0] == 0, 'voice has custom states')
    pos += 2
    require(struct.unpack_from('<H', raw, pos)[0] == 0, 'voice has RTPC overrides')
    pos += 2
    require(struct.unpack_from('<I', raw, pos)[0] == 0, 'voice has feedback bus')
    pos += 4 + 6  # feedback bus, Loop and loop modifiers
    require(pos == end, 'unknown localized sound tail')


def adpcm_data(raw):
    require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'not a RIFF WEM')
    pos = 12
    fmt = None
    while pos + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, pos)
        start, end = pos + 8, pos + 8 + size
        require(end <= len(raw), 'truncated ADPCM WEM')
        if tag == b'fmt ':
            fmt, channels, rate, byte_rate, align = struct.unpack_from('<HHIIH', raw, start)
            require(fmt == 2 and align == 36 * channels, 'not supported Wwise IMA ADPCM')
        if tag == b'data':
            require(fmt == 2 and size % align == 0, 'invalid ADPCM block layout')
            return start, size
        pos = end + (size & 1)
    raise ValueError('no ADPCM data chunk')


def bink_parts(raw):
    require(len(raw) >= 48, 'truncated Bink')
    magic, size, count, largest, count2, w, h, num, den, flags, tracks = struct.unpack_from('<4s10I', raw)
    require(magic == b'BIKi' and size + 8 == len(raw), 'unsupported Bink/header size')
    require(count == count2 and 0 < count <= 100000 and 0 <= tracks <= 32,
            'invalid Bink counts')
    table = 44 + tracks * 12
    header_end = table + (count + 1) * 4
    require(header_end <= len(raw), 'truncated Bink index')
    indices = struct.unpack_from('<' + str(count + 1) + 'I', raw, table)
    offsets = [i & ~1 for i in indices]
    require(offsets[0] == header_end and offsets[-1] == len(raw) and indices[0] & 1,
            'invalid Bink index bounds/keyframe')
    frames = []
    for index in range(count):
        start, end = offsets[index:index + 2]
        require(header_end <= start < end <= len(raw) and end - start <= largest,
                'invalid Bink frame bounds')
        pos = start
        for _ in range(tracks):
            require(pos + 4 <= end, 'truncated Bink audio packet')
            length = struct.unpack_from('<I', raw, pos)[0]
            pos += 4 + length
            require(pos <= end, 'Bink audio exceeds frame')
        require(pos < end, 'empty Bink video packet')
        frames.append((pos, end, indices[index] & 1))
    return tracks, frames


def strip_bink_audio(raw):
    tracks, frames = bink_parts(raw)
    if not tracks:
        return raw
    header = bytearray(raw[:44] + bytes((len(frames) + 1) * 4))
    struct.pack_into('<I', header, 40, 0)
    pos = len(header)
    indices, chunks = [], []
    for start, end, key in frames:
        chunk = raw[start:end]
        require(pos % 2 == 0 and len(chunk) % 2 == 0, 'unaligned Bink video packet')
        indices.append(pos | key)
        chunks.append(chunk)
        pos += len(chunk)
    indices.append(pos)
    struct.pack_into('<I', header, 4, pos - 8)
    struct.pack_into('<I', header, 12, max(map(len, chunks)))
    struct.pack_into('<' + str(len(indices)) + 'I', header, 44, *indices)
    after = bytes(header) + b''.join(chunks)
    verify_bink(raw, after)
    return after


def verify_bink(before, after):
    _, old = bink_parts(before)
    tracks, new = bink_parts(after)
    require(tracks == 0 and len(old) == len(new), 'audio remains/video frame count changed')
    require(before[8:12] == after[8:12] and before[16:40] == after[16:40],
            'video timing/format changed')
    for (a, b, key), (c, d, new_key) in zip(old, new):
        require(key == new_key and before[a:b] == after[c:d], 'video packet changed')


def prepare():
    require(not (HERE / 'plan.json').exists() and not BACKUP.exists() and not STAGED.exists(),
            'plan/backup/staging exists; inspect it instead of preparing twice')
    manifest = json.loads((LIBRARY / 'manifest.json').read_text())
    expected = {s['source']: s for s in manifest['sources']}
    ids = collections.defaultdict(set)
    known = collections.defaultdict(set)
    reasons = collections.Counter()
    language_counts = collections.Counter()
    clip_rows, zeroes = [], collections.defaultdict(list)
    zeroed_ids = collections.defaultdict(set)
    music_refs = collect_music_refs(manifest)
    for row in manifest['entries']:
        category = row['category']
        decision = selection_decision(row, music_refs)
        selected = decision in ('female_speaker_tag', 'music_reference')
        reasons[decision] += 1
        if selected and category == 'Dialogue':
            language_counts[row['language']] += 1
        for origin in row['origins']:
            language = origin.get('language', 'Shared')
            known[language].add(origin.get('uid', 0))
            if selected:
                ids[language].add(origin.get('uid', 0))
        clip_rows.append([row['id'], category, row['language'], selected, decision,
                          '|'.join(speaker(label) for label in row.get('cue_labels', [])),
                          '|'.join(row.get('cue_labels', []))])
        if selected and (row.get('format_tag') == 2 or row.get('decoded', {}).get('codec') == '16-bit Little Endian PCM'):
            raw = (LIBRARY / row['native']).read_bytes()
            require(sha(raw) == row['sha256'], 'library WEM changed: ' + row['id'])
            start, size = adpcm_data(raw) if row.get('format_tag') == 2 else pcm_data(raw)
            for origin in row['origins']:
                require(origin['bytes'] == len(raw), 'media origin length mismatch')
                zeroes[origin['source']].append([origin['offset'] + start, size, row['id']])
                zeroed_ids[origin.get('language', 'Shared')].add(origin.get('uid', 0))
    gains = collections.defaultdict(list)
    coverage = collections.Counter()
    muted_nodes = collections.defaultdict(set)
    banks_checked = []
    for bank in manifest['banks']:
        raw = (LIBRARY / bank['file']).read_bytes()
        language = bank['source'].split('/')[1] if bank['source'].startswith('Locale/') else 'Shared'
        count, selected_count = 0, 0
        for typ, ident, start, end in objects(raw):
            if typ == 11:  # MusicTrack, including boot-up music missed by source-only categorization.
                refs, volume = music_track_prefix(raw, start, end)
                gains[bank['source']].append([bank['offset'] + volume, ident, 0, 0])
                muted_nodes[language].update(refs)
                coverage['muted_music_track_nodes'] += 1
                continue
            if typ != 2:
                continue
            plugin, source, fileid, volume = sound_prefix(raw, start, end)
            if plugin & 0xF != 1:  # generated tone/silence, not recorded speech
                continue
            count += 1
            selected = source in ids[language] or fileid in ids[language]
            if not selected:
                continue
            if language != 'Shared':
                check_local_voice_tail(raw, volume, end)
            gains[bank['source']].append([bank['offset'] + volume, ident, source, fileid])
            muted_nodes[language].update((source, fileid))
            coverage['muted_sound_nodes'] += 1
            selected_count += 1
        banks_checked.append({'bank': bank['file'], 'codec_sound_nodes': count, 'muted_nodes': selected_count})
    # Detect direct actions that could replace a selected sound node's base gain.
    # Only Play/Stop/Pause/Resume should target localized speech sounds.
    action_types = collections.Counter()
    action_gains = collections.defaultdict(list)
    target_ids = collections.defaultdict(set)
    for source, changes in gains.items():
        language = source.split('/')[1] if source.startswith('Locale/') else 'Shared'
        target_ids[language].update(change[1] for change in changes)
    for bank in manifest['banks']:
        raw = (LIBRARY / bank['file']).read_bytes()
        language = bank['source'].split('/')[1] if bank['source'].startswith('Locale/') else 'Shared'
        for typ, ident, start, end in objects(raw):
            if typ == 3:
                action, target = struct.unpack_from('<II', raw, start + 12)
                if target in target_ids[language]:
                    action_types[hex(action)] += 1
                    # v48 category begins at bit 12; 4=Play, 5=PlayAndContinue.
                    category = action >> 12
                    require(category in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
                            'unhandled gain-affecting action: ' + hex(action))
                    if category in (10, 12):  # SetVolume/SetLFE -> Mute_O; retain target/timing/parameter block.
                        require(end - start == 69 and struct.unpack_from('<I', raw, start + 65)[0] == 0,
                                'unknown property-action layout')
                        action_gains[bank['source']].append(bank['offset'] + start + 12)
    missing = {lang: sorted(selected - muted_nodes[lang]) for lang, selected in ids.items()}
    # Music tracks are a separate HIRC type, but their ADPCM payload is independently zeroed.
    dialogue_missing = {lang: values for lang, values in missing.items() if lang != 'Shared' and values}
    require(not dialogue_missing, 'selected dialogue is not covered by sound nodes: ' + repr(dialogue_missing)[:400])
    shared_uncovered = set(missing.get('Shared', [])) - zeroed_ids['Shared']
    require(not shared_uncovered, 'shared media without mute coverage: ' + repr(shared_uncovered))
    sources = sorted(set(gains) | set(action_gains) | set(zeroes) | set(manifest['videos']))
    # Full preflight before creating backup/staging. Historical inventory must still match.
    for relative in sources:
        path = game_path(relative)
        require(digest(path) == expected[relative]['sha256_before'], 'source changed since extraction: ' + relative)
    audit = HERE / 'selection.csv'
    with audit.open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'category', 'language', 'muted', 'reason', 'speaker_tags', 'cue_labels'])
        writer.writerows(clip_rows)
    rows = []
    for relative in sources:
        path = game_path(relative)
        before = path.read_bytes()
        require(sha(before) == expected[relative]['sha256_before'], 'source changed during preparation')
        after = bytearray(before)
        for offset, ident, source, fileid in gains[relative]:
            after[offset:offset + len(GAIN)] = GAIN
        for offset in action_gains[relative]:
            after[offset:offset + len(MUTE_ACTION)] = MUTE_ACTION
        ranges = sorted({(offset, size) for offset, size, _ in zeroes[relative]})
        for index, (offset, size) in enumerate(ranges):
            require(offset >= 0 and offset + size <= len(after), 'zero range exceeds source')
            require(index == 0 or ranges[index - 1][0] + ranges[index - 1][1] <= offset,
                    'overlapping ADPCM ranges')
            after[offset:offset + size] = bytes(size)
        video = relative in manifest['videos']
        if video:
            after = strip_bink_audio(bytes(after))
        else:
            require(len(after) == len(before), 'Wwise container size changed')
        after = bytes(after)
        if after == before:
            continue
        backup, staged = BACKUP / relative, STAGED / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        staged.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(before)
        staged.write_bytes(after)
        shutil.copystat(path, backup)
        shutil.copystat(path, staged)
        row = {'source': relative, 'before_sha256': sha(before), 'after_sha256': sha(after),
               'before_bytes': len(before), 'after_bytes': len(after),
               'gain_offsets': [change[0] for change in gains[relative]],
               'mute_action_offsets': action_gains[relative],
               'zero_ranges': ranges, 'video_audio_removed': video}
        if video:
            tracks, frames = bink_parts(before)
            row.update(removed_audio_tracks=tracks, video_frames=len(frames))
        rows.append(row)
        print('Prepared', relative, 'gain nodes', len(row['gain_offsets']), 'ADPCM ranges', len(ranges), flush=True)
    plan = {'version': 2, 'files': rows, 'selection_counts': dict(reasons),
            'selected_dialogue_by_primary_language': dict(language_counts),
            'coverage': dict(coverage), 'direct_action_types': dict(action_types),
            'banks_checked': banks_checked, 'shared_media_without_sound_node': missing.get('Shared', []),
            'shared_media_without_any_mute_coverage': sorted(shared_uncovered),
            'runtime_tested': False,
            'limitations': ['Speaker decisions use original cue metadata, not listening to every recording.',
                            'Only tagged female dialogue and referenced music are selected; uncertain speakers and non-music shared effects are retained.',
                            'All Bink tracks are removed, including men, music and effects mixed into video audio.',
                            'Localized sound nodes were checked to have no states/RTPC overrides; parent/runtime code remains outside offline proof.',
                            'Existing image/video mods are preserved as the baseline; restore audio before using their separate managers.',
                            'The extracted library stays an unmodified historical inventory for recovery/audit.']}
    save(HERE / 'plan.json', plan)
    save(HERE / 'state.json', {'mode': 'prepared_not_applied', 'runtime_tested': False})
    print('Prepared', len(rows), 'files. Game assets have NOT been modified.')
    print(json.dumps(plan['selection_counts'], indent=2))


def inspect(plan):
    result = []
    for row in plan['files']:
        value = digest(game_path(row['source']))
        state = ('original' if value == row['before_sha256'] else
                 'filtered' if value == row['after_sha256'] else 'UNKNOWN_MODIFICATION')
        result.append({'source': row['source'], 'state': state, 'sha256': value})
    return result


def verify(plan):
    statuses = inspect(plan)
    for row, status in zip(plan['files'], statuses):
        require(status['state'] != 'UNKNOWN_MODIFICATION', 'unknown game file: ' + row['source'])
        backup, staged = BACKUP / row['source'], STAGED / row['source']
        require(digest(backup) == row['before_sha256'], 'backup changed: ' + row['source'])
        require(digest(staged) == row['after_sha256'], 'staged file changed: ' + row['source'])
        if row['video_audio_removed']:
            verify_bink(backup.read_bytes(), staged.read_bytes())
        else:
            before, after = backup.read_bytes(), staged.read_bytes()
            expected = bytearray(before)
            for offset in row['gain_offsets']:
                expected[offset:offset + len(GAIN)] = GAIN
            for offset in row.get('mute_action_offsets', []):
                expected[offset:offset + len(MUTE_ACTION)] = MUTE_ACTION
            for offset, size in row['zero_ranges']:
                expected[offset:offset + size] = bytes(size)
            require(after == expected, 'unexpected changes outside approved byte ranges')
    counts = dict(collections.Counter(row['state'] for row in statuses))
    report = {'state_counts': counts, 'files': statuses,
              'approved_ranges_and_video_packets_verified': True, 'runtime_tested': False}
    save(HERE / 'verification.json', report)
    print(json.dumps({'files': len(statuses), **counts, 'runtime_tested': False}))
    return statuses


def library_status(plan, mode):
    selection = HERE / 'selection.csv'
    if not selection.exists():  # also keeps isolated transaction tests away from the real library
        return
    with selection.open(newline='') as f:
        muted = sorted(row['id'] for row in csv.DictReader(f) if row['muted'] == 'True')
    status = {'mode': mode, 'muted_ids': muted, 'all_video_tracks_muted': True,
              'original_library_retained': True, 'runtime_tested': False,
              'counts': plan['selection_counts']}
    path = LIBRARY / 'filter_status.js'
    temp = path.with_suffix('.js.tmp')
    temp.write_text('window.DSF_AUDIO_FILTER=' + json.dumps(status, ensure_ascii=False, separators=(',', ':')) + ';\n')
    os.replace(temp, path)


def install(plan, restore=False):
    stopped()
    statuses = verify(plan)  # check every source, backup and prepared file before first write
    changed = []
    try:
        for row, status in zip(plan['files'], statuses):
            wanted = 'original' if restore else 'filtered'
            if status['state'] == wanted:
                continue
            stopped()
            path = game_path(row['source'])
            require(digest(path) == status['sha256'], 'source changed after preflight')
            replacement = (BACKUP if restore else STAGED) / row['source']
            temp = path.with_name(path.name + '.audio-filter.tmp')
            require(not temp.exists(), 'temporary file already exists: ' + str(temp))
            try:
                shutil.copy2(replacement, temp)
                require(digest(temp) == row['before_sha256' if restore else 'after_sha256'],
                        'replacement copy failed verification')
                with temp.open('rb') as f:
                    os.fsync(f.fileno())
                os.replace(temp, path)
                changed.append((row, status))
            finally:
                temp.unlink(missing_ok=True)
            print('Restored' if restore else 'Applied', row['source'], flush=True)
    except BaseException:
        # Roll back only writes made in THIS invocation, never unknown external edits.
        for row, old in reversed(changed):
            path = game_path(row['source'])
            require(digest(path) == row['before_sha256' if restore else 'after_sha256'],
                    'external change blocks rollback; inspect journal/backups')
            old_file = (BACKUP if old['state'] == 'original' else STAGED) / row['source']
            temp = path.with_name(path.name + '.audio-filter.rollback.tmp')
            shutil.copy2(old_file, temp)
            os.replace(temp, path)
        raise
    mode = 'restored' if restore else 'applied'
    save(HERE / 'state.json', {'mode': mode, 'runtime_tested': False})
    verify(plan)
    library_status(plan, mode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'apply', 'restore', 'verify', 'status'))
    args = parser.parse_args()
    with (HERE / '.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.command == 'prepare':
            prepare()
            return
        require((HERE / 'plan.json').is_file(),
                'No local audio plan is available. Apply/restore/verify need the original private plan, prepared files and backups; a source-only package cannot restore installed game edits.')
        plan = json.loads((HERE / 'plan.json').read_text())
        if args.command in ('apply', 'restore'):
            install(plan, args.command == 'restore')
        elif args.command == 'verify':
            verify(plan)
        else:
            print(json.dumps(dict(collections.Counter(r['state'] for r in inspect(plan))), indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        print('ERROR:', error, file=sys.stderr)
        sys.exit(1)
