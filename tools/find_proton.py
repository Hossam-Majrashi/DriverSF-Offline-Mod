#!/usr/bin/env python3
"""Find installed Steam Proton Experimental; no download or GE/Wine fallback."""
import os
from pathlib import Path
import re
import sys

def find(game,home=None,data_home=None):
    home=Path(home or Path.home())
    data=Path(data_home or os.environ.get('XDG_DATA_HOME',str(home/'.local/share')))
    steam=[data/'Steam',home/'.steam/steam',home/'.steam/root']
    libraries=[game.parent.parent/'Steam']+steam
    for base in steam:
        vdf=base/'steamapps/libraryfolders.vdf'
        if vdf.is_file():
            for entry in re.findall(r'"path"\s+"([^"\n]+)"',vdf.read_text(errors='replace')):
                libraries.append(Path(entry.replace('\\\\','\\')))
    seen=set()
    for library in libraries:
        p=library/'steamapps/common/Proton - Experimental'
        if p in seen:continue
        seen.add(p)
        if (p/'proton').is_file() and os.access(p/'files/bin/wine',os.X_OK):return p.resolve()
    return None
if __name__=='__main__':
    runner=find(Path(sys.argv[1]).resolve())
    if not runner:raise SystemExit('Install Proton Experimental in Steam, or set DSF_PROTONPATH=/path/to/Proton - Experimental.')
    print(runner)
