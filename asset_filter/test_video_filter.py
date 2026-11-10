#!/usr/bin/env python3
"""Synthetic BIK/container and temporary-install tests. No game files modified."""
import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import video_filter as v


def fake_bink(videos, audio=None, width=832, keys=None):
    audio = [b'ABCD' + bytes([i, 0, 0, 0]) for i in range(len(videos))] if audio is None else audio
    keys = [1] + [0] * (len(videos) - 1) if keys is None else keys
    count, tracks = len(videos), 1
    table = 44 + tracks * 12
    pos = table + (count + 1) * 4
    chunks, entries = [], []
    for sound, video, key in zip(audio, videos, keys):
        entries.append(pos | key)
        chunk = struct.pack('<I', len(sound)) + sound + video
        chunks.append(chunk)
        pos += len(chunk)
    entries.append(pos)
    header = struct.pack('<4s10I', b'BIKi', pos - 8, count, max(map(len, chunks)), count,
                         width, 288, 2997, 100, 1048576, tracks)
    metadata = struct.pack('<IHHI', 72960, 48000, 0x5000, 0)
    return header + metadata + struct.pack('<' + str(count + 1) + 'I', *entries) + b''.join(chunks)


class ContainerTests(unittest.TestCase):
    def setUp(self):
        self.before = fake_bink([b'female00', b'female11', b'female22', b'female33', b'female44'])
        self.donor = fake_bink([b'MALE0000', b'MALE1111'])

    def test_audio_frames_timing_alpha_preserved_and_donor_loops(self):
        result = v.replace_video(self.before, self.donor)
        a, b = v.parse_bink(self.before), v.parse_bink(result)
        self.assertEqual(a.layout, b.layout)
        self.assertEqual([f.audio for f in a.frames], [f.audio for f in b.frames])
        self.assertEqual([f.video for f in b.frames], [b'MALE0000', b'MALE1111', b'MALE0000', b'MALE1111', b'MALE0000'])
        self.assertEqual([f.key for f in b.frames], [1, 0, 1, 0, 1])
        self.assertNotIn(b'female', result)

    def test_reject_incompatible_dimensions(self):
        with self.assertRaisesRegex(ValueError, 'incompatible'):
            v.replace_video(self.before, fake_bink([b'MALE0000'], width=640))

    def test_reject_wrong_file_size(self):
        with self.assertRaises(ValueError):
            v.parse_bink(self.before + b'extra')

    def test_reject_truncated_index(self):
        with self.assertRaises(ValueError):
            v.parse_bink(self.before[:45])

    def test_reject_nonkey_first_frame(self):
        with self.assertRaisesRegex(ValueError, 'keyframe'):
            v.parse_bink(fake_bink([b'MALE0000'], keys=[0]))

    def test_reject_audio_overrun(self):
        data = bytearray(self.before)
        table = 56
        first = struct.unpack_from('<I', data, table)[0] & ~1
        struct.pack_into('<I', data, first, 0x7fffffff)
        with self.assertRaisesRegex(ValueError, 'audio packet exceeds'):
            v.parse_bink(bytes(data))

    def test_verify_rejects_changed_audio(self):
        result = bytearray(v.replace_video(self.before, self.donor))
        first = struct.unpack_from('<I', result, 56)[0] & ~1
        result[first + 4] ^= 1
        with self.assertRaisesRegex(ValueError, 'audio bytes changed'):
            v.check_replacement(self.before, self.donor, bytes(result))

    def test_reject_bad_magic(self):
        with self.assertRaisesRegex(ValueError, 'BI Ki'):
            v.parse_bink(b'BIKf' + self.before[4:])


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.private = self.root / 'mod/asset_filter/video_private'
        self.private.mkdir(parents=True)
        self.before = fake_bink([b'female00', b'female11'])
        self.donor = fake_bink([b'MALE0000', b'MALE1111'])
        self.after = v.replace_video(self.before, self.donor)
        self.plan = {'files': []}
        for i in range(2):
            relative = f'Common/fmv/previews/uid0000{i}_mpr01.bik'
            backup = f'mod/backups/video-test/original/{relative}'
            prepared = f'prepared/{relative}'
            for p, content in [(self.root / relative, self.before),
                               (self.root / backup, self.before),
                               (self.private / prepared, self.after)]:
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(content)
            self.plan['files'].append({'source': relative, 'backup': backup, 'prepared': prepared,
                                       'before_sha256': v.base.sha(self.before),
                                       'after_sha256': v.base.sha(self.after)})
        for target, value in [('GAME', self.root), ('PRIVATE', self.private)]:
            p = patch.object(v, target, value)
            p.start(); self.addCleanup(p.stop)
        p = patch.object(v.base, 'GAME', self.root)
        p.start(); self.addCleanup(p.stop)
        p = patch.object(v.base, 'ensure_closed')
        self.closed = p.start(); self.addCleanup(p.stop)
        output = contextlib.redirect_stdout(io.StringIO())
        output.__enter__(); self.addCleanup(output.__exit__, None, None, None)

    def assert_state(self, content):
        for row in self.plan['files']:
            self.assertEqual((self.root / row['source']).read_bytes(), content)

    def test_apply_restore_and_idempotence(self):
        v.apply_or_restore(self.plan)
        self.assert_state(self.after)
        v.apply_or_restore(self.plan)
        v.apply_or_restore(self.plan, True)
        self.assert_state(self.before)
        v.apply_or_restore(self.plan, True)

    def test_refuses_running_game(self):
        self.closed.side_effect = ValueError('Close the game first')
        with self.assertRaisesRegex(ValueError, 'Close the game'):
            v.apply_or_restore(self.plan)
        self.assert_state(self.before)

    def test_refuses_unknown_external_changes(self):
        p = self.root / self.plan['files'][0]['source']
        p.write_bytes(b'foreign')
        with self.assertRaisesRegex(ValueError, 'mixed/unknown'):
            v.apply_or_restore(self.plan)
        self.assertEqual(p.read_bytes(), b'foreign')

    def test_preflight_rejects_tampered_prepared_copy(self):
        (self.private / self.plan['files'][1]['prepared']).write_bytes(b'foreign')
        with self.assertRaisesRegex(ValueError, 'prepared video changed'):
            v.apply_or_restore(self.plan)
        self.assert_state(self.before)

    def test_second_commit_failure_rolls_back_first(self):
        original_replace = v.os.replace
        commits = 0
        def fail_second(source, destination):
            nonlocal commits
            if str(source).endswith('.video-filter-staging'):
                commits += 1
                if commits == 2:
                    raise OSError('simulated second commit failure')
            return original_replace(source, destination)
        with patch.object(v.os, 'replace', side_effect=fail_second):
            with self.assertRaisesRegex(OSError, 'simulated'):
                v.apply_or_restore(self.plan)
        self.assert_state(self.before)
        self.assertFalse(list(self.root.rglob('*.video-filter-staging')))

    def test_running_game_before_commit_cleans_staging(self):
        self.closed.side_effect = [None, ValueError('Close the game first')]
        with self.assertRaisesRegex(ValueError, 'Close the game'):
            v.apply_or_restore(self.plan)
        self.assert_state(self.before)
        self.assertFalse(list(self.root.rglob('*.video-filter-staging')))


if __name__ == '__main__':
    unittest.main()
