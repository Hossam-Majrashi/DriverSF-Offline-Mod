"""Authored bytecode ABI/roundtrip regressions, not native runtime acceptance."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from lua51_chunk import Reader,GAME_HEADER,compile_script,transcode
from embed_nitro_script import script_source
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 build/test dependency')
class Chunks(unittest.TestCase):
    def host(self):
        return subprocess.run(['lua5.1','-e','io.write(string.dump(function()end):sub(1,12))'],capture_output=True,check=True).stdout
    def test_native_bytecode_header_and_float_constants(self):
        chunk=compile_script('local a=0.1; return function(x,...)return x+a,... end','fixture.lua')
        self.assertEqual(chunk[:12],GAME_HEADER);r=Reader(chunk);p=r.proto();self.assertEqual(r.pos,len(chunk))
        self.assertEqual(p['source'],b'@mod/fixture.lua')
        values=[v for tag,v in p['constants'] if tag==3];self.assertIn(struct.unpack('<f',struct.pack('<f',0.1))[0],values)
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'roundtrip.luac';f.write_bytes(transcode(chunk,self.host()))
            subprocess.run(['lua5.1','-e','local f=assert(loadfile('+json.dumps(str(f))+'))(); local x,y,z=f(2,"ok",false); assert(math.abs(x-2.1)<1e-6 and y=="ok" and z==false)'],check=True)
    def test_reject_incompatible_truncated_and_trailing_chunks(self):
        chunk=compile_script('return 1','fixture.lua')
        for b in (b'',chunk[:9],chunk[:-1],chunk+b'junk',chunk[:4]+b'\x52'+chunk[5:],chunk[:10]+b'\x10'+chunk[11:]):
            with self.assertRaises(ValueError):transcode(b,GAME_HEADER)
    def test_authored_scripts_compile_deterministically(self):
        for stem in ('world','nitro'):
            s=script_source(stem)
            a=compile_script(s,stem+'_script.lua');self.assertEqual(a,compile_script(s,stem+'_script.lua'))
            self.assertEqual(transcode(a,GAME_HEADER),a)
    def test_native_abi_roundtrip_runs_full_world_fixtures(self):
        # A host VM cannot load a game-ABI chunk directly: transcode BACK only
        # in this isolated fixture, keeping all float32 rounding/instructions.
        chunk=compile_script(script_source('world'),'world_script.lua')
        with tempfile.TemporaryDirectory() as d:
            f=Path(d)/'world.luac';f.write_bytes(transcode(chunk,self.host()))
            for mode in ('timer','weather','street_error'):
                subprocess.run(['lua5.1',str(ROOT/'tools/test_world.lua'),mode],check=True,
                    env={**os.environ,'DSF_MOD_ROOT':str(ROOT),'DSF_WORLD_CHUNK':str(f)})
            subprocess.run(['lua5.1',str(ROOT/'tools/test_streets.lua')],check=True,
                env={**os.environ,'DSF_MOD_ROOT':str(ROOT),'DSF_WORLD_CHUNK':str(f)})
if __name__=='__main__':unittest.main()
