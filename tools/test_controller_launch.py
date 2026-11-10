#!/usr/bin/env python3
"""Controller-launch environment tests; no game, Wine, input/haptic injection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
MOD=Path(__file__).resolve().parents[1]
class ControllerLaunchChecks(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='dsf-controller-env-');self.root=Path(self.tmp.name);r=self.root
        mod=r/'mod';mod.mkdir();(mod/'tools').mkdir();(mod/'launch.sh').write_bytes((MOD/'launch.sh').read_bytes());self.mod=mod
        runner=r/'Proton - Fake Experimental';(runner/'files/bin').mkdir(parents=True);(runner/'proton').touch()
        w=runner/'files/bin/wine';w.write_text('#!/bin/sh\necho fixture-version\n');w.chmod(0o755)
        (runner/'version').write_text('mock Experimental\n');prefix=r/'existing prefix';prefix.mkdir();(prefix/'system.reg').touch();(prefix/'drive_c').mkdir()
        (r/'Driver.exe').write_text('NOT an executable: no actual game can be launched')
        (mod/'install.sh').write_text('#!/bin/sh\nexit 0\n');(mod/'install.sh').chmod(0o755)
        (mod/'tools/backup_saves.py').write_text('import os\nfrom pathlib import Path\nPath(os.environ["TEST_OUT"]+".backup").write_text("fixture only; no actual saves")\n')
        self.out=r/'receipt.json';umu=r/'fake_umu.py'
        umu.write_text('import os,json,sys\nfrom pathlib import Path\nPath(os.environ["TEST_OUT"]).write_text(json.dumps({"args":sys.argv[1:],"env":{k:os.environ.get(k) for k in ["PROTONPATH","WINEPREFIX","WINEDLLOVERRIDES","PROTON_DISABLE_HIDRAW","PROTON_ENABLE_HIDRAW","SDL_GAMECONTROLLERCONFIG","SDL_GAMECONTROLLER_IGNORE_DEVICES"]}}))\n')
        bins=r/'bin';bins.mkdir();p=bins/'pgrep';p.write_text('#!/bin/sh\nexit 1\n');p.chmod(0o755)
        self.env={k:v for k,v in os.environ.items() if not k.startswith(('DSF_','SDL_','PROTON_'))}
        self.env.update(DSF_PROTONPATH=str(runner),DSF_WINEPREFIX=str(prefix),DSF_UMU=str(umu),TEST_OUT=str(self.out),PATH=str(bins)+':'+os.environ['PATH'])
    def tearDown(self):self.tmp.cleanup()
    def run_launcher(self,**env):
        p=subprocess.run(['bash',str(self.mod/'launch.sh')],env={**self.env,**env},capture_output=True,text=True,timeout=10)
        self.assertEqual(p.returncode,0,p.stderr);return json.loads(self.out.read_text())
    def test_default_requests_only_ds4_sdl_route_keeps_experimental_and_game_args(self):
        d=self.run_launcher();e=d['env']
        self.assertEqual(e['PROTON_DISABLE_HIDRAW'],'0x054c/0x05c4')
        self.assertEqual(e['PROTONPATH'],self.env['DSF_PROTONPATH'])
        self.assertIn('dinput8=n,b',e['WINEDLLOVERRIDES']);self.assertEqual(d['args'],[str(self.root/'Driver.exe'),'-offline'])
        for k in ['SDL_GAMECONTROLLERCONFIG','SDL_GAMECONTROLLER_IGNORE_DEVICES']:self.assertIsNone(e[k])
        log=next((self.mod/'logs').glob('launch-*')).read_text();self.assertIn('compatibility requested: 1',log)
    def test_no_backups_by_default(self):
        self.run_launcher()
        self.assertFalse(Path(str(self.out)+'.backup').exists())
        self.assertFalse((self.mod/'backups').exists())
    def test_backups_require_explicit_opt_in(self):
        self.run_launcher(DSF_BACKUPS='1')
        self.assertTrue(Path(str(self.out)+'.backup').exists())
    def test_bad_backup_mode_rejected(self):
        p=subprocess.run(['bash',str(self.mod/'launch.sh')],env={**self.env,'DSF_BACKUPS':'bad'},capture_output=True,text=True,timeout=10)
        self.assertNotEqual(p.returncode,0);self.assertIn('DSF_BACKUPS must be 0 or 1',p.stderr)
        self.assertFalse(self.out.exists())
    def test_existing_list_preserved_and_ds4_not_duplicated_case_insensitively(self):
        for original,expected in [('0x1234/0x5678','0x1234/0x5678,0x054c/0x05c4'),('0x054C/0x05C4','0x054C/0x05C4'),('1','1')]:
            with self.subTest(original=original):self.assertEqual(self.run_launcher(PROTON_DISABLE_HIDRAW=original)['env']['PROTON_DISABLE_HIDRAW'],expected)
    def test_opt_out_leaves_input_env_untouched_and_mapping_not_overwritten(self):
        self.assertIsNone(self.run_launcher(DSF_DS4_XINPUT='0')['env']['PROTON_DISABLE_HIDRAW'])
        d=self.run_launcher(DSF_DS4_XINPUT='0',PROTON_DISABLE_HIDRAW='custom',SDL_GAMECONTROLLERCONFIG='user-mapping',SDL_GAMECONTROLLER_IGNORE_DEVICES='user-list')
        self.assertEqual(d['env']['PROTON_DISABLE_HIDRAW'],'custom');self.assertEqual(d['env']['SDL_GAMECONTROLLERCONFIG'],'user-mapping');self.assertEqual(d['env']['SDL_GAMECONTROLLER_IGNORE_DEVICES'],'user-list')
    def test_bad_opt_in_rejected_before_install_or_umu(self):
        p=subprocess.run(['bash',str(self.mod/'launch.sh'),'--check'],env={**self.env,'DSF_DS4_XINPUT':'bad'},capture_output=True,text=True,timeout=10)
        self.assertNotEqual(p.returncode,0);self.assertIn('must be 0 or 1',p.stderr);self.assertFalse(self.out.exists())
    def test_published_build_and_input_configuration_are_scoped(self):
        release=json.loads((MOD/'release.json').read_text())
        self.assertEqual(hashlib.sha256((MOD/'build/dinput8.dll').read_bytes()).hexdigest(),release['dll_sha256'])
        source=(MOD/'launch.sh').read_text()
        for banned in ['reg add','chmod 777','PROTON_SONY_AUTO_XINPUT=','xinput1_3=','SDL_GAMECONTROLLERCONFIG=','SDL_GAMECONTROLLER_IGNORE_DEVICES=']:
            self.assertNotIn(banned,source)
        self.assertIn('0x054c/0x05c4',source)
        self.assertIn('tools/find_proton.py',source)
        self.assertNotIn('/mnt/',source)
        self.assertNotIn(str(Path.home()),source)
if __name__=='__main__':unittest.main(verbosity=2)
