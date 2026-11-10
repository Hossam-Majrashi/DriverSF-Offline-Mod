#!/usr/bin/env python3
"""Independent reversible BIK portrait supplement. Does not modify the v1 plan.
Prepare is offline staging only and may run while the game is open. Apply and
restore refuse a running game. Original audio is retained, not muted/replaced.
Only BI Ki files with matching video layout are supported; no transcoding.
"""
from pathlib import Path
from dataclasses import dataclass
import argparse
import fcntl
import json
import os
import re
import shutil
import struct
import sys

import manage as base

HERE = Path(__file__).resolve().parent
GAME = HERE.parent.parent
PRIVATE = HERE / 'video_private'
BACKUP = GAME / 'mod/backups/asset_filter_videos_v1/original'
SELECTION = HERE / 'mission_video_selection.json'


@dataclass
class Frame:
    audio: bytes  # Original length-prefixed packets for every language track.
    video: bytes
    key: int


@dataclass
class Bink:
    header: bytes
    frames: list
    table_offset: int
    layout: tuple
    audio_tracks: int


def parse_bink(raw):
    base.require(len(raw) >= 48, 'truncated BIK header')
    h = struct.unpack_from('<4s10I', raw)
    magic, size, count, largest, count2, width, height, num, den, flags, tracks = h
    base.require(magic == b'BIKi', 'only BI Ki is supported')
    base.require(size + 8 == len(raw), 'BIK declared size mismatch')
    base.require(0 < count <= 100000 and count2 == count, 'invalid BIK frame count')
    base.require(width > 0 and height > 0 and num > 0 and den > 0 and tracks <= 32,
                 'invalid BIK layout')
    table = 44 + tracks * 12
    header_end = table + (count + 1) * 4
    base.require(header_end <= len(raw), 'truncated BIK index')
    entries = struct.unpack_from('<' + str(count + 1) + 'I', raw, table)
    offsets = [e & ~1 for e in entries]
    base.require(offsets[0] == header_end and offsets[-1] == len(raw),
                 'unexpected BIK index boundary')
    base.require(entries[0] & 1, 'BIK must start with a keyframe')
    frames = []
    for i in range(count):
        start, end = offsets[i:i + 2]
        base.require(header_end <= start < end <= len(raw), 'invalid BIK frame range')
        base.require(end - start <= largest, 'BIK largest-frame header too small')
        pos = start
        for _ in range(tracks):
            base.require(pos + 4 <= end, 'truncated audio packet length')
            length = struct.unpack_from('<I', raw, pos)[0]
            pos += 4 + length
            base.require(pos <= end, 'audio packet exceeds frame')
        base.require(pos < end, 'empty video packet')
        frames.append(Frame(raw[start:pos], raw[pos:end], entries[i] & 1))
    return Bink(raw[:header_end], frames, table,
                (magic, width, height, num, den, flags), tracks)


def replace_video(original, donor):
    """Reuse complete donor sequences, restarting only at its initial keyframe.

    Retain the destination's exact frame count, rate, language metadata and
    length-prefixed audio packets. No mixing of delta frames from unrelated
    videos, guessed index patches, cropping or unsupported Bink encoding.
    """
    target, source = parse_bink(original), parse_bink(donor)
    base.require(target.layout == source.layout, 'incompatible donor video layout')
    header = bytearray(target.header)
    position = len(header)
    chunks, indices, biggest = [], [], 0
    for i, frame in enumerate(target.frames):
        replacement = source.frames[i % len(source.frames)]
        chunk = frame.audio + replacement.video
        base.require(position % 2 == 0 and len(chunk) % 2 == 0,
                     'unaligned BIK frame cannot be indexed safely')
        indices.append(position | replacement.key)
        chunks.append(chunk)
        position += len(chunk)
        biggest = max(biggest, len(chunk))
    indices.append(position)
    base.require(position < 0x100000000, 'BIK exceeds 32-bit container')
    struct.pack_into('<I', header, 4, position - 8)
    struct.pack_into('<I', header, 12, biggest)
    struct.pack_into('<' + str(len(indices)) + 'I', header, target.table_offset, *indices)
    result = bytes(header) + b''.join(chunks)
    check_replacement(original, donor, result)
    return result


def check_replacement(original, donor, result):
    target, source, after = map(parse_bink, (original, donor, result))
    base.require(target.layout == source.layout == after.layout, 'video layout changed')
    base.require(len(target.frames) == len(after.frames), 'video duration/frame count changed')
    base.require(target.header[44:target.table_offset] == after.header[44:after.table_offset],
                 'audio metadata changed')
    for i, (before, now) in enumerate(zip(target.frames, after.frames)):
        expected = source.frames[i % len(source.frames)]
        base.require(before.audio == now.audio, 'original audio bytes changed')
        base.require(now.video == expected.video and now.key == expected.key,
                     'unexpected video bytes/keyframe flag')


def target_paths(selection):
    uids = selection['reviewed_mission_uids']
    base.require(uids and all(re.fullmatch(r'\d{5}', uid) for uid in uids), 'invalid mission UID')
    requested = set(selection['extra_pip_files'])
    result = []
    for directory in ('previews', 'pips'):
        for p in sorted((GAME / 'Common/fmv' / directory).glob('*.bik')):
            relative = p.relative_to(GAME).as_posix()
            if any(p.name.lower().startswith('uid' + uid + '_') for uid in uids) or relative in requested:
                result.append(relative)
    base.require(requested <= set(result), 'explicit PIP filename is missing')
    for uid in uids:
        base.require(any(Path(p).name.lower().startswith('uid' + uid + '_') for p in result),
                     'selected mission UID is missing: ' + uid)
    base.require(selection['donor'] not in result, 'donor must not also be a target')
    return result


def prepare():
    base.require(not (PRIVATE / 'plan.json').exists(), 'video plan exists; use verify/apply/restore')
    base.require(not BACKUP.exists() and not (PRIVATE / 'prepared').exists(),
                 'video backup/prepared directory exists; inspect before preparing')
    selection = json.loads(SELECTION.read_text())
    donor_path = base.safe_game_path(selection['donor'])
    donor = donor_path.read_bytes()
    parse_bink(donor)
    rows = []
    for relative in target_paths(selection):
        p = base.safe_game_path(relative)
        before = p.read_bytes()
        after = replace_video(before, donor)
        base.require(base.sha(before) != base.sha(after), 'no-op video replacement')
        base.require(base.digest(p) == base.sha(before), 'source changed during preparation')
        backup = BACKUP / relative
        staged = PRIVATE / 'prepared' / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        staged.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(before)
        staged.write_bytes(after)
        base.require(base.digest(backup) == base.sha(before) and base.digest(staged) == base.sha(after),
                     'private output verification failed')
        video = parse_bink(before)
        rows.append({'source': relative, 'before_sha256': base.sha(before),
                     'after_sha256': base.sha(after), 'before_bytes': len(before),
                     'after_bytes': len(after), 'backup': str(backup.relative_to(GAME)),
                     'prepared': str(staged.relative_to(PRIVATE)),
                     'frames': len(video.frames), 'audio_tracks': video.audio_tracks})
    base.require(base.digest(donor_path) == base.sha(donor), 'donor changed during preparation')
    plan = {'version': 1, 'scope': 'selected mission briefing/PIP video frames only',
            'donor': selection['donor'], 'donor_sha256': base.sha(donor), 'files': rows,
            'audio_preserved': True, 'runtime_tested': False, 'limitations': selection['limitations']}
    base.save_json(PRIVATE / 'plan.json', plan)
    base.save_json(PRIVATE / 'state.json', {'mode': 'prepared_not_applied', 'runtime_tested': False})
    print(f'Prepared {len(rows)} video replacements privately; NO game files modified.')


def inspect(plan):
    rows = []
    for row in plan['files']:
        p = base.safe_game_path(row['source'])
        value = base.digest(p)
        state = ('original' if value == row['before_sha256'] else
                 'filtered' if value == row['after_sha256'] else 'UNKNOWN_MODIFICATION')
        rows.append({'source': row['source'], 'state': state, 'sha256': value})
    return rows


def verify(plan):
    donor = base.safe_game_path(plan['donor']).read_bytes()
    base.require(base.sha(donor) == plan['donor_sha256'], 'donor changed')
    rows = inspect(plan)
    for row, status in zip(plan['files'], rows):
        base.require(status['state'] != 'UNKNOWN_MODIFICATION', 'unknown video: ' + row['source'])
        before = (GAME / row['backup']).read_bytes()
        after = (PRIVATE / row['prepared']).read_bytes()
        base.require(base.sha(before) == row['before_sha256'], 'video backup changed')
        base.require(base.sha(after) == row['after_sha256'], 'prepared video changed')
        check_replacement(before, donor, after)
    report = {'files': rows, 'audio_bytes_and_video_frames_verified': True, 'runtime_tested': False}
    base.save_json(PRIVATE / 'verification.json', report)
    print(json.dumps(report, indent=2))


def apply_or_restore(plan, restoring=False):
    base.ensure_closed()
    rows = inspect(plan)
    target, initial = ('original', 'filtered') if restoring else ('filtered', 'original')
    states = {r['state'] for r in rows}
    if states == {target}:
        print(f'Already {target}; no video changes needed.')
        return
    base.require(states == {initial}, 'mixed/unknown video hashes; refusing to overwrite')
    stages, committed = [], []
    # Full preflight before replacing any installed video.
    for row in plan['files']:
        base.require(base.digest(GAME / row['backup']) == row['before_sha256'], 'video backup changed')
        base.require(base.digest(PRIVATE / row['prepared']) == row['after_sha256'], 'prepared video changed')
    try:
        for row in plan['files']:
            p = base.safe_game_path(row['source'])
            stage = p.with_name(p.name + '.video-filter-staging')
            base.require(not stage.exists(), 'leftover video staging file')
            stages.append(stage)
            origin = GAME / row['backup'] if restoring else PRIVATE / row['prepared']
            shutil.copy2(origin, stage)
            with stage.open('r+b') as f:
                os.fsync(f.fileno())
            base.require(base.digest(stage) == row['before_sha256' if restoring else 'after_sha256'],
                         'video staging hash mismatch')
        base.ensure_closed()
        for row, stage in zip(plan['files'], stages):
            p = base.safe_game_path(row['source'])
            base.require(base.digest(p) == row['after_sha256' if restoring else 'before_sha256'],
                         'video changed during staging')
            os.replace(stage, p)
            committed.append(row)
            fd = os.open(str(p.parent), os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        base.require(all(r['state'] == target for r in inspect(plan)), 'installed video hash mismatch')
        base.save_json(PRIVATE / 'state.json', {'mode': target, 'runtime_tested': False})
        print(f'{target}: {len(committed)} video files; original audio preserved. Runtime NOT tested.')
    except BaseException:
        for row in reversed(committed):
            p = base.safe_game_path(row['source'])
            origin = PRIVATE / row['prepared'] if restoring else GAME / row['backup']
            rollback = p.with_name(p.name + '.video-filter-rollback')
            shutil.copy2(origin, rollback)
            os.replace(rollback, p)
        raise
    finally:
        for stage in stages:
            if stage.exists():
                stage.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'apply', 'restore', 'verify'))
    args = parser.parse_args()
    PRIVATE.mkdir(exist_ok=True)
    with (PRIVATE / '.lock').open('w') as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.command == 'prepare':
            prepare()
            return
        base.require((PRIVATE / 'plan.json').exists(), 'prepare the private video plan first')
        plan = json.loads((PRIVATE / 'plan.json').read_text())
        if args.command == 'verify':
            verify(plan)
        else:
            apply_or_restore(plan, args.command == 'restore')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('ERROR:', error, file=sys.stderr)
        sys.exit(1)
