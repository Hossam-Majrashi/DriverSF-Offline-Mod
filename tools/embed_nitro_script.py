#!/usr/bin/env python3
"""Compile authored scripts into the game's Lua 5.1 float32 / size_t32 ABI.
Never compile in the live VM, use private game code or load runtime disk scripts.
"""
from pathlib import Path
from lua51_chunk import compile_script,include
root=Path(__file__).resolve().parents[1]
def script_source(stem):
    s=(root/f'src/{stem}_script.lua').read_text()
    if stem=='world':s='local createStreets=function(...)\n'+(root/'src/street_script.lua').read_text()+'\nend\n'+s
    return s

def outputs():
    return {f'build/{stem}_script.inc':include(name,compile_script(script_source(stem),f'{stem}_script.lua'))
            for stem,name in [('nitro','source'),('world','worldSource')]}
if __name__=='__main__':
    for path,content in outputs().items():(root/path).write_text(content)
