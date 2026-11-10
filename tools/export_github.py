#!/usr/bin/env python3
"""Export only authored mod files/licensed dependencies, never local game data.
No Git operations, upload, overwrite, symlink following or game modifications.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = (
    '.gitignore', '.gitattributes', 'README.md', 'PUBLISHING_AR.md',
    'THIRD_PARTY_NOTICES.md', 'release.json', 'settings.example.ini',
    'build.sh', 'install.sh', 'launch.sh', 'launch.cmd', 'launch.command',
    'prepare-windows.ps1', 'build/dinput8.dll', 'build/SHA256SUMS',
)
FILTER_FILES = (
    'asset_filter/manage.py', 'asset_filter/video_filter.py',
    'asset_filter/test_manage.py', 'asset_filter/test_video_filter.py',
    'asset_filter/enable.sh', 'asset_filter/restore.sh', 'asset_filter/verify.sh',
    'asset_filter/selection.json', 'asset_filter/mission_video_selection.json',
    'asset_filter/README_AR.md', 'asset_filter/README_VIDEOS_AR.md',
    'audio_filter/manage.py', 'audio_filter/test_manage.py',
    'audio_filter/enable.sh', 'audio_filter/restore.sh', 'audio_filter/verify.sh',
    'audio_filter/README_AR.md',
)
SOURCE_DIRS = ('src', 'vendor', 'assets', 'ui', 'tools')
SOURCE_SUFFIXES = {'.py', '.c', '.cpp', '.h', '.s', '.def', '.lua', '.md', '.txt', '.ttf', '.json', '.sh', '.jpg', '.png'}
PRIVATE_PARTS = {'backups', 'logs', 'prepared', 'payloads', 'video_private',
                 'refinement', 'library_ui_before', '__pycache__'}
GAME_SUFFIXES = {'.exe', '.pck', '.sp', '.fchunk', '.dngc', '.astd', '.bik',
                 '.wem', '.wav', '.ogg', '.mp3', '.flac', '.grbk', '.dds', '.bin', '.sav'}
MANIFEST = 'PACKAGE_SHA256SUMS'


def need(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_files(root):
    selected = set(ROOT_FILES + FILTER_FILES)
    for directory in SOURCE_DIRS:
        base = root / directory
        need(base.is_dir() and not base.is_symlink(), 'Missing/linked source directory: ' + directory)
        for path in base.rglob('*'):
            relative = path.relative_to(root)
            if '__pycache__' in relative.parts:
                continue
            need(not path.is_symlink(), 'Symlink refused: ' + str(relative))
            if path.is_file():
                need(path.suffix.lower() in SOURCE_SUFFIXES, 'Unexpected source file: ' + str(relative))
                selected.add(relative.as_posix())
    for relative in sorted(selected):
        path = root / relative
        parts = Path(relative).parts
        need(all(not root.joinpath(*parts[:i]).is_symlink() for i in range(1, len(parts))),
             'Linked release ancestor: ' + relative)
        need(path.is_file() and not path.is_symlink(), 'Missing/linked release file: ' + relative)
        need(not any(part in PRIVATE_PARTS for part in Path(relative).parts), 'Private release path: ' + relative)
        need(path.suffix.lower() not in GAME_SUFFIXES, 'Game asset refused: ' + relative)
        need(path.stat().st_size < 50 * 1024 * 1024, 'Unexpected oversized release file: ' + relative)
    return sorted(selected)


def verify_build(root):
    release = json.loads((root / 'release.json').read_text())
    need(release['dll'] == 'build/dinput8.dll', 'Unexpected DLL path')
    expected = release['dll_sha256']
    need(digest(root / release['dll']) == expected, 'Mod DLL does not match release.json')
    need((root / 'build/SHA256SUMS').read_text().split()[0] == expected, 'DLL checksum file mismatch')
    for relative in ('THIRD_PARTY_NOTICES.md', 'vendor/imgui/LICENSE.txt',
                     'vendor/minhook/LICENSE.txt', 'assets/DejaVu-LICENSE.txt'):
        need((root / relative).is_file(), 'Missing license: ' + relative)


def check(root):
    need(root.is_dir() and not root.is_symlink(), 'Missing/linked package directory')
    selected = selected_files(root)
    actual = set()
    for path in root.rglob('*'):
        relative = path.relative_to(root)
        if '.git' in relative.parts:  # Git's own database is not package content.
            continue
        need(not path.is_symlink(), 'Symlink in package: ' + str(relative))
        need(not any(p in PRIVATE_PARTS for p in relative.parts), 'Private directory in package: ' + str(relative))
        if path.is_file():
            actual.add(relative.as_posix())
    need(actual == set(selected) | {MANIFEST}, 'Package contains missing or unapproved files: ' +
         repr(sorted(actual ^ (set(selected) | {MANIFEST}))))
    listed = {}
    for line in (root / MANIFEST).read_text().splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        need(match is not None, 'Invalid package checksum line')
        value, relative = match.groups()
        need(relative in selected and relative not in listed, 'Invalid/duplicate checksum path')
        listed[relative] = value
    need(set(listed) == set(selected), 'Incomplete package checksum list')
    for relative, expected in listed.items():
        need(digest(root / relative) == expected, 'Changed package file: ' + relative)
    verify_build(root)
    return {'files': len(actual), 'bytes': sum((root / p).stat().st_size for p in actual),
            'no_private_game_assets': True, 'dll_and_licenses_verified': True}


def export(destination, make_zip=False):
    destination = destination.expanduser().absolute()
    need(not destination.exists() and not destination.is_symlink(), 'Output already exists; refusing overwrite')
    need(not destination.is_relative_to(ROOT), 'Output must be outside the source mod directory')
    archive = destination.parent / (destination.name + '.zip')
    need(not make_zip or not archive.exists(), 'ZIP already exists; refusing overwrite')
    selected = selected_files(ROOT)
    verify_build(ROOT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix='.dsf-export-', dir=destination.parent))
    try:
        for relative in selected:
            target = temp / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        (temp / MANIFEST).write_text(''.join(digest(temp / p) + '  ' + p + '\n' for p in selected))
        report = check(temp)
        os.rename(temp, destination)
    finally:
        if temp.exists():
            shutil.rmtree(temp)
    if make_zip:
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in sorted(destination.rglob('*')):
                if path.is_file():
                    z.write(path, destination.name + '/' + path.relative_to(destination).as_posix())
        with zipfile.ZipFile(archive) as z:
            need(z.testzip() is None, 'ZIP verification failed')
    return {'output': str(destination), 'zip': str(archive) if make_zip else None, **report}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    parser.add_argument('--zip', action='store_true')
    args = parser.parse_args()
    need(bool(args.output) != bool(args.check), 'Choose --output or --check')
    need(not args.check or not args.zip, '--zip requires --output')
    result = check(args.check) if args.check else export(args.output, args.zip)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError) as error:
        raise SystemExit('ERROR: ' + str(error))
