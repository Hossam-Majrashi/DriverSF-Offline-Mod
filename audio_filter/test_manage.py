#!/usr/bin/env python3
"""Pure tests: never write to game assets."""
import struct
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import contextlib
import io
import manage as m


def bink(tracks=2):
    count = 3
    frames = []
    for index in range(count):
        audio = b''.join(struct.pack('<I', 4) + bytes([i + index]) * 4 for i in range(tracks))
        frames.append(audio + bytes([index + 30]) * (10 + index * 2))
    header = bytearray(44 + tracks * 12 + (count + 1) * 4)
    pos, indices = len(header), []
    for index, frame in enumerate(frames):
        indices.append(pos | (1 if index == 0 else 0))
        pos += len(frame)
    indices.append(pos)
    struct.pack_into('<4s10I', header, 0, b'BIKi', pos - 8, count, max(map(len, frames)),
                     count, 1280, 720, 30000, 1001, 0, tracks)
    struct.pack_into('<4I', header, 44 + tracks * 12, *indices)
    return bytes(header) + b''.join(frames)


def sound(stream=1, fx=False, synth=False):
    # A v48 object, deliberately independent of production bank sources.
    plugin = 0x640002 if synth else 0x40001
    source = struct.pack('<4I', plugin, stream, 12345, 12345)
    if stream != 1:
        source += struct.pack('<II', 0, 300)
    source += b'\x01'
    if synth:
        source += struct.pack('<I', 4) + bytes(4)
    initial_fx = bytes([int(fx), int(fx)])
    if fx:
        initial_fx += b'\0' + struct.pack('<BIBI', 0, 0x650003, 0, 2) + b'\0\0' + struct.pack('<I', 0)
    base = initial_fx + struct.pack('<II4B', 20, 30, 100, 0, 0, 10)
    volume = 12 + len(source) + len(base)
    params = struct.pack('<12fI', *([0.] * 12), 0)
    tail = bytes([0]) + bytes(7) + bytes([0]) + struct.pack('<HHI3h', 0, 0, 0, 1, 0, 0)
    body = struct.pack('<I', 777) + source + base + params + tail
    raw = struct.pack('<II', 2, len(body)) + body
    return raw, volume


class SpeakerTests(unittest.TestCase):
    def classify(self, label, **extra):
        return m.voice_class({'cue_labels': [label], **extra})

    def test_final_speaker_not_context(self):
        self.assertEqual(self.classify('Vai_GLOBAL_VpsFCh1552_FelFree_DriTanner_03_02_85904'),
                         'retained_male_speaker_tag')

    def test_female_codes_and_case(self):
        for speaker in ('VpsFWt2555', 'CpsFCh2053', 'VdrFWt2501', 'NomFBk5501', 'FWT4552'):
            self.assertEqual(self.classify('Vai_FR01_X_' + speaker + '_01_02_12345'), 'female_speaker_tag')

    def test_male_codes_and_names(self):
        for speaker in ('PasJones', 'DriTanner', 'VpsMWt3551', 'NomMWt2054', 'NomCharlesJerich'):
            self.assertEqual(self.classify('Voi_UID12345_GP_' + speaker + '_01_02_12345'), 'retained_male_speaker_tag')

    def test_named_female(self):
        for speaker in ('LailaSharan', 'SarahAllen', 'NomJessicaAdams', 'MINDYCATHERSON'):
            self.assertEqual(self.classify('Voi_UID12345_GP_' + speaker + '_01_02_12345'), 'female_speaker_tag')

    def test_unknown_conservative(self):
        self.assertEqual(self.classify('Voi_UID12345_GP_POLICEDISPATCHER_01_02_12345'), 'uncertain_speaker')
        self.assertEqual(m.voice_class({}), 'uncertain_speaker')

    def test_placeholder_requires_zero_samples(self):
        label = 'Vai_GLOBAL_X_Y_mrblanky_01_02_12345'
        self.assertEqual(self.classify(label, declared_samples=0), 'empty_placeholder')
        self.assertEqual(self.classify(label, declared_samples=3), 'uncertain_speaker')


class PolicyTests(unittest.TestCase):
    def test_interface_and_collision_not_music_by_package(self):
        for group in ('HUD', 'Collision', 'Pedestrian'):
            row = {'category': 'Effects_and_Misc', 'group': group,
                   'origins': [{'uid': 42, 'language': 'Shared'}]}
            self.assertEqual(m.selection_decision(row, {99}), 'retained_shared_effect')

    def test_music_reference_even_in_misc_category(self):
        row = {'category': 'Effects_and_Misc', 'origins': [{'uid': 42, 'language': 'Shared'}]}
        self.assertEqual(m.selection_decision(row, {42}), 'music_reference')

    def test_unknown_dialogue_retained(self):
        row = {'category': 'Dialogue', 'cue_labels': ['Voi_X_POLICEDISPATCHER_01_01_12345']}
        self.assertEqual(m.selection_decision(row, set()), 'retained_uncertain_speaker')

    def test_context_does_not_silence_male_reply(self):
        row = {'category': 'Dialogue', 'cue_labels': ['Vai_GLOBAL_VpsFCh1552_FelFree_DriTanner_03_02_85904']}
        self.assertEqual(m.selection_decision(row, set()), 'retained_male_speaker_tag')


class FormatTests(unittest.TestCase):
    def test_sound_offsets(self):
        for stream in (0, 1, 2):
            for fx in (False, True):
                for synth in (False, True):
                    raw, expected = sound(stream, fx, synth)
                    plugin, src, fileid, volume = m.sound_prefix(raw, 0, len(raw))
                    self.assertEqual((src, fileid, volume), (12345, 12345, expected))
                    m.check_local_voice_tail(raw, volume, len(raw))

    def test_music_track_layout(self):
        raw, _ = sound()
        _, _, _, end_source = m.source_prefix(raw, 12, len(raw))
        body = struct.pack('<II', 888, 1) + raw[12:end_source]
        body += struct.pack('<III4dI', 1, 0, 12345, 0., 0., 0., 2000., 1)
        body += raw[end_source:] + struct.pack('<Ii', 0, 100)
        track = struct.pack('<II', 11, len(body)) + body
        refs, volume = m.music_track_prefix(track, 0, len(track))
        self.assertEqual(refs, {12345})
        self.assertEqual(struct.unpack_from('<12f', track, volume), (0.,) * 12)

    def test_pcm_range(self):
        fmt = struct.pack('<HHIIHH', 65534, 2, 44100, 176400, 4, 16) + bytes(8)
        body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', 40) + b'\xff' * 40
        raw = b'RIFF' + struct.pack('<I', len(body)) + body
        pos, size = m.pcm_data(raw)
        self.assertEqual(raw[pos:pos + size], b'\xff' * 40)

    def test_state_rejected(self):
        raw, volume = sound()
        raw = bytearray(raw)
        struct.pack_into('<I', raw, volume + 48, 3)
        with self.assertRaisesRegex(ValueError, 'state group'):
            m.check_local_voice_tail(raw, volume, len(raw))

    def test_bink_all_tracks_removed_without_video_change(self):
        raw = bink()
        result = m.strip_bink_audio(raw)
        self.assertLess(len(result), len(raw))
        self.assertEqual(m.bink_parts(result)[0], 0)
        m.verify_bink(raw, result)

    def test_bink_zero_tracks_idempotent(self):
        raw = bink(0)
        self.assertEqual(m.strip_bink_audio(raw), raw)
        stripped = m.strip_bink_audio(bink())
        self.assertEqual(m.strip_bink_audio(stripped), stripped)

    def test_bink_bad_size_rejected(self):
        with self.assertRaises(ValueError):
            m.strip_bink_audio(bink()[:-1])

    def test_bink_video_tamper_detected(self):
        before = bink()
        after = bytearray(m.strip_bink_audio(before))
        after[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'video packet'):
            m.verify_bink(before, bytes(after))

    def test_adpcm_range(self):
        fmt = struct.pack('<HHIIHH', 2, 1, 32000, 18000, 36, 4)
        body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', 36) + b'\xff' * 36
        raw = b'RIFF' + struct.pack('<I', len(body)) + body
        pos, size = m.adpcm_data(raw)
        self.assertEqual(raw[pos:pos + size], b'\xff' * 36)


class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dsf-audio-test-')
        root = Path(self.temp.name)
        self.patches = [patch.object(m, 'GAME', root), patch.object(m, 'HERE', root / 'manager'),
                        patch.object(m, 'BACKUP', root / 'backup'), patch.object(m, 'STAGED', root / 'staged'),
                        patch.object(m, 'stopped', lambda: None)]
        for p in self.patches:
            p.start()
        for folder in (m.HERE, m.BACKUP, m.STAGED):
            folder.mkdir()
        self.plan = {'files': []}
        for name in ('one.pck', 'two.pck'):
            before = bytes(64)
            after = before[:8] + m.GAIN + before[8 + len(m.GAIN):]
            (m.GAME / name).write_bytes(before)
            (m.BACKUP / name).write_bytes(before)
            (m.STAGED / name).write_bytes(after)
            self.plan['files'].append({'source': name, 'before_sha256': m.sha(before),
                'after_sha256': m.sha(after), 'gain_offsets': [8], 'zero_ranges': [],
                'video_audio_removed': False})

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.temp.cleanup()

    def test_round_trip_and_idempotence(self):
        with contextlib.redirect_stdout(io.StringIO()):
            m.install(self.plan)
            m.install(self.plan)
            self.assertTrue(all(r['state'] == 'filtered' for r in m.inspect(self.plan)))
            m.install(self.plan, restore=True)
            self.assertTrue(all(r['state'] == 'original' for r in m.inspect(self.plan)))

    def test_unknown_modification_blocks_all_writes(self):
        (m.GAME / 'two.pck').write_bytes(b'external edit')
        original_one = (m.GAME / 'one.pck').read_bytes()
        with self.assertRaisesRegex(ValueError, 'unknown game file'):
            m.install(self.plan)
        self.assertEqual((m.GAME / 'one.pck').read_bytes(), original_one)

    def test_partial_failure_rolls_back(self):
        real_replace = m.os.replace
        def fail_second(src, dest):
            if str(src).endswith('audio-filter.tmp') and Path(dest).name == 'two.pck':
                raise OSError('simulated write failure')
            return real_replace(src, dest)
        with patch.object(m.os, 'replace', fail_second), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(OSError, 'simulated'):
                m.install(self.plan)
        self.assertTrue(all(r['state'] == 'original' for r in m.inspect(self.plan)))

    def test_path_escape_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unsafe/missing'):
            m.game_path('../outside.pck')


if __name__ == '__main__':
    unittest.main(verbosity=2)
