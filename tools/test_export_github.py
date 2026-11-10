#!/usr/bin/env python3
"""Publication policy tests: temporary directories only; never launch the game."""
from pathlib import Path
import tempfile
import unittest
import zipfile
import export_github as package


class ExportChecks(unittest.TestCase):
    def test_whitelist_excludes_machine_state(self):
        selected = package.selected_files(package.ROOT)
        self.assertNotIn('settings.ini', selected)
        self.assertNotIn('installed-dll.sha256', selected)
        self.assertTrue(all(not any(p in package.PRIVATE_PARTS for p in Path(s).parts) for s in selected))
        package.verify_build(package.ROOT)

    def test_export_roundtrip_zip_and_tamper_detection(self):
        with tempfile.TemporaryDirectory(prefix='dsf-export-test-') as folder:
            output = Path(folder) / 'mod'
            result = package.export(output, True)
            self.assertTrue(result['no_private_game_assets'])
            self.assertEqual(result['files'], package.check(output)['files'])
            with zipfile.ZipFile(result['zip']) as z:
                self.assertIsNone(z.testzip())
                self.assertTrue(all(n.startswith('mod/') and '..' not in Path(n).parts for n in z.namelist()))
            (output / 'README.md').write_text('modified')
            with self.assertRaisesRegex(ValueError, 'Changed package file'):
                package.check(output)

    def test_existing_output_never_overwritten(self):
        with tempfile.TemporaryDirectory(prefix='dsf-export-test-') as folder:
            output = Path(folder) / 'mod'
            output.mkdir()
            sentinel = output / 'private.txt'
            sentinel.write_text('keep')
            with self.assertRaisesRegex(ValueError, 'Output already exists'):
                package.export(output)
            self.assertEqual(sentinel.read_text(), 'keep')

    def test_symlinked_code_refused(self):
        with tempfile.TemporaryDirectory(prefix='dsf-export-test-') as folder:
            root = Path(folder) / 'source'
            root.mkdir()
            for rel in package.ROOT_FILES + package.FILTER_FILES:
                p = root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            for rel in package.SOURCE_DIRS:
                (root / rel).mkdir(exist_ok=True)
            outside = Path(folder) / 'private.h'
            outside.write_text('not public')
            (root / 'src/leak.h').symlink_to(outside)
            with self.assertRaisesRegex(ValueError, 'Symlink refused'):
                package.selected_files(root)


if __name__ == '__main__':
    unittest.main(verbosity=2)
