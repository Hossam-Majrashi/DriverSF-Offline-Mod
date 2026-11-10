#!/usr/bin/env python3
"""Copy existing DSF saves, never follow a missing prefix into creating one."""
import datetime, hashlib, json, pathlib, shutil, sys
prefix=pathlib.Path(sys.argv[1]).expanduser().resolve()
if not (prefix/'system.reg').is_file() or not (prefix/'drive_c').is_dir():
    raise SystemExit('Refusing backup/launch: existing Wine prefix required')
base=prefix/'drive_c'
dest=pathlib.Path(__file__).resolve().parents[1]/'backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
sources=[]
users=base/'users'
if users.exists():
    for user in users.iterdir():
        for documents in ('Documents','My Documents'):
            p=user/documents/'Ubisoft/Driver San Francisco'
            if p.is_dir():sources.append(p)
p=base/'Program Files (x86)/Ubisoft/Ubisoft Game Launcher/savegames'
if p.is_dir():sources.append(p)
seen=set(); manifest=[]
for source in sources:
    for p in source.rglob('*'):
        if not p.is_file() or p.resolve() in seen:continue
        seen.add(p.resolve());target=dest/p.relative_to(prefix)
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        manifest.append(dict(source=str(p),backup=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
dest.mkdir(parents=True,exist_ok=True)
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(f'Save backup: {dest} ({len(manifest)} files)')
