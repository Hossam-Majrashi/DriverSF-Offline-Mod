#!/usr/bin/env python3
"""Current standalone suite. No backups, research, screenshots or game required.
C++ fixtures run production code with MOCK native APIs, not game physics.
Optional DSF_GAME_EXE permits a read-only fingerprint/ABI check on YOUR game.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from find_proton import find
from embed_ui import outputs
ROOT=Path(__file__).resolve().parents[1]
CXX=shutil.which('g++')
def run(args,**kwargs):return subprocess.run([str(a) for a in args],check=True,timeout=90,**kwargs)
def header(tmp,stem,marker='static void draw()',name=None):
    source=(ROOT/f'src/{stem}.h').read_text().split(marker,1)[0]+'}\n'
    (tmp/(name or stem+'_hook_under_test.h')).write_text(source)
def cc32(tmp,source,extra=()):
    b=tmp/'fixture'
    run([CXX,'-m32','-std=c++17','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all',
        '-fno-pie','-no-pie','-mstackrealign','-pthread','-Wno-attributes','-I'+str(ROOT/'src'),'-I'+str(tmp),
        ROOT/'tools'/source,*extra,'-o',b])
    return b
class CurrentChecks(unittest.TestCase):
    def test_current_build_and_licenses(self):
        r=json.loads((ROOT/'release.json').read_text());b=(ROOT/r['dll']).read_bytes()
        self.assertEqual(hashlib.sha256(b).hexdigest(),r['dll_sha256'])
        self.assertEqual(b[:2],b'MZ');pe=struct.unpack_from('<I',b,0x3c)[0]
        self.assertEqual(b[pe:pe+4],b'PE\0\0');self.assertEqual(struct.unpack_from('<H',b,pe+4)[0],0x14c)
        for s in [f"Driver SF | Offline mod v{r['version']} | F1",'DirectInput8Create','Live color','Automatic speed - Player','Freeze mission countdown','Game weather (default)','Choose maximum speed (MPH)']:
            self.assertTrue(s.encode()+b'\0' in b,'Missing current build label: '+s)
        for removed in ('Control police chaser count','Police chaser target / limit','Match current police count (experimental)','Live weather','Smooth weather (keep current sky dome)'):
            self.assertTrue(removed.encode()+b'\0' not in b,'Removed control still present: '+removed)
        self.assertEqual((ROOT/'build/SHA256SUMS').read_text().split()[0],r['dll_sha256'])
        for p in ['vendor/imgui/LICENSE.txt','vendor/minhook/LICENSE.txt','assets/DejaVu-LICENSE.txt']:
            self.assertTrue((ROOT/p).is_file())
    @unittest.skipUnless(shutil.which('fribidi'),'FriBidi needed only to verify/regenerate translated headers')
    def test_generated_font_and_translations(self):
        for p,content in outputs().items():self.assertEqual((ROOT/p).read_text(),content)
    def test_no_cursor_teleport_or_forced_gameplay_state(self):
        ui=(ROOT/'src/ui.h').read_text()
        self.assertNotIn('ImGui::SetCursorPosX(',ui)
        self.assertIn('if(offset>0)ImGui::Indent(offset)',ui);self.assertIn('if(offset>0)ImGui::Unindent(offset)',ui)
        power=(ROOT/'src/powertrain.h').read_text()
        for s in ['WriteProcessMemory','SetVelocity','SetSpeed','memcpy(g+0x44,','awaitingPark']:
            self.assertNotIn(s,power)
        self.assertIn('memcpy(g+0x48,&plan.overall,4)',power)
        self.assertIn('enabled{false},ready{false}',power)
        self.assertIn('L"Language",0,settingsPath),0,1,0',(ROOT/'src/settings.h').read_text())
    def test_proton_discovery_with_nonstandard_library_and_spaces(self):
        with tempfile.TemporaryDirectory() as d:
            base=Path(d);home=base/'home';data=home/'.local/share';steam=data/'Steam/steamapps';steam.mkdir(parents=True)
            game=base/'games/Ubisoft/Driver San Francisco';game.mkdir(parents=True)
            library=base/'another Steam library';runner=library/'steamapps/common/Proton - Experimental';(runner/'files/bin').mkdir(parents=True)
            (runner/'proton').touch();w=runner/'files/bin/wine';w.touch();w.chmod(0o755)
            (steam/'libraryfolders.vdf').write_text('"path" "'+str(library)+'"\n')
            self.assertEqual(find(game,home,data),runner.resolve());w.unlink();self.assertIsNone(find(game,home,data))
    @unittest.skipUnless(CXX,'g++ required')
    def test_actual_cap_model_and_assembly_bridges_with_mock_native(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'speed')
            b=cc32(tmp,'test_speed.cpp',[ROOT/'src/speed_bridge.S',ROOT/'tools/test_speed_bridge.S']);run([b])
    @unittest.skipUnless(CXX,'g++ required')
    def test_actual_powertrain_automatic_following_with_mock_native(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'speed');header(tmp,'powertrain')
            b=cc32(tmp,'test_powertrain.cpp',[ROOT/'src/speed_bridge.S']);run([b])
    @unittest.skipUnless(CXX,'g++ required')
    def test_native_traffic_candidate_filter_and_owned_cleanup(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'traffic',marker='static void bind(')
            run([cc32(tmp,'test_traffic.cpp')])
    @unittest.skipUnless(CXX,'g++ required')
    def test_native_passenger_seat_identity_and_owned_restore(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'passengers',marker='static void bind(')
            run([cc32(tmp,'test_passengers.cpp')])
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_player_passenger_lifecycle_and_scope_guards(self):
        run(['lua5.1',ROOT/'tools/test_passengers.lua'],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    @unittest.skipUnless(CXX,'g++ required')
    def test_player_native_indicators_and_owned_restore(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'signals',marker='static void bind(')
            run([cc32(tmp,'test_signals.cpp')])
    @unittest.skipUnless(CXX,'g++ required')
    def test_indicator_binding_input_edges_and_preferences(self):
        with tempfile.TemporaryDirectory() as d:run([cc32(Path(d),'test_signal_input.cpp')])
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_indicator_player_scope_and_dispatch(self):
        run(['lua5.1',ROOT/'tools/test_signals.lua'],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    @unittest.skipUnless(CXX,'g++ required')
    def test_indicator_actual_os_input_reservation_and_directinput(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);source=(ROOT/'src/mod.cpp').read_text()
            controls=(ROOT/'src/signal_controls.h').read_text().split('static void poll()',1)[0]+'}\n'
            (tmp/'signal_os_filters_under_test.h').write_text(controls)
            quiet='static SHORT WINAPI quietAsync'+source.split('static SHORT WINAPI quietAsync',1)[1].split('static BOOL WINAPI quietClip',1)[0]
            direct='static HRESULT WINAPI getState'+source.split('static HRESULT WINAPI getState',1)[1].split('static void hookInputDevice',1)[0]
            (tmp/'signal_os_callbacks_under_test.h').write_text(quiet+direct)
            run([cc32(tmp,'test_signal_os.cpp')])
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_zero_implies_police_cancellation_and_owned_restoration(self):
        run(['lua5.1',ROOT/'tools/test_zero_police.lua'],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    def test_simplified_world_controls_have_no_chaser_tuning_or_deletion(self):
        world=(ROOT/'src/world_lua.h').read_text();street=(ROOT/'src/street_script.lua').read_text()
        for removed in ('chaserLimit','exactPolice','chasers','Live weather','Smooth weather (','Weather preset','Previous weather','Next weather'):
            self.assertNotIn(removed,world)
        for removed in ('matchChasers','limitChasers','chaseRestore','deleteVehicle','Getaway.SetSettings','maximumNumberOfChasers','requiredNumberOfChasers'):
            self.assertNotIn(removed,street)
        self.assertEqual(world.count('ImGui::BeginCombo('),1)
        self.assertIn('Game weather (default)',world)
        self.assertIn('api.pushBool(L,true);return 8;',world)
        self.assertIn('return 16;',world)
        self.assertIn('passengers{true}',world)
        self.assertIn('Traffic level (0-100)',world)
        self.assertNotIn('DisableSpeech(',world+street+(ROOT/'src/passengers.h').read_text())
    def powertrain_regression(self,name):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'speed');header(tmp,'powertrain')
            run([cc32(tmp,'test_powertrain.cpp',[ROOT/'src/speed_bridge.S']),'--regression-'+name])
    @unittest.skipUnless(CXX,'g++ required')
    def test_powertrain_mild_body_motion_does_not_starve_power(self):
        self.powertrain_regression('motion')
    @unittest.skipUnless(CXX,'g++ required')
    def test_powertrain_native_baseline_updates_do_not_silently_disable(self):
        self.powertrain_regression('reconfigure')
    @unittest.skipUnless(CXX,'g++ required')
    def test_powertrain_native_neutral_does_not_reset_forward_gearing(self):
        self.powertrain_regression('neutral')
    @unittest.skipUnless(CXX,'g++ required')
    def test_selected_speed_targets_and_gearing_bounds(self):
        self.powertrain_regression('targets')
    @unittest.skipUnless(CXX,'g++ required')
    def test_controller_hotplug_native_delegation_and_ordinal_iat(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d)
            (tmp/'controller_under_test.h').write_text((ROOT/'src/controller.h').read_text().split('static void bind()',1)[0]+'}\n')
            source=(ROOT/'src/mod.cpp').read_text()
            iat='static void *replaceImport'+source.split('static void *replaceImport',1)[1].split('// Vtable copies',1)[0]
            quiet='static DWORD WINAPI quietXInput'+source.split('static DWORD WINAPI quietXInput',1)[1].split('\n',1)[0]
            (tmp/'controller_iat_under_test.h').write_text(iat+quiet+'\n')
            run([cc32(tmp,'test_controller.cpp')])
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_street_police_block_and_traffic_ownership(self):
        run(['lua5.1',ROOT/'tools/test_streets.lua'],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    @unittest.skipUnless(CXX,'g++ required')
    def test_production_log_length_is_bounded_after_snprintf_truncation(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d)
            body='static void logLine'+(ROOT/'src/mod.cpp').read_text().split('static void logLine',1)[1].split('static bool readable',1)[0]
            (tmp/'logging_under_test.h').write_text(body)
            run([cc32(tmp,'test_logging.cpp')])
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_paranoia_buffered_hud_and_actual_suspicion_payload(self):
        run(['lua5.1',ROOT/'tools/test_paranoia.lua'],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    @unittest.skipUnless(os.environ.get('DSF_GAME_EXE') and shutil.which('lua5.1'),'Optional: private game and Lua 5.1')
    def test_optional_private_paranoia_native_timer_hud_and_payload(self):
        from private_lua import selected_paranoia,dump
        exe=Path(os.environ['DSF_GAME_EXE'])
        self.assertEqual(hashlib.sha256(exe.read_bytes()).hexdigest(),json.loads((ROOT/'release.json').read_text())['driver_exe_sha256'])
        proto=selected_paranoia(exe.parent/'media/ScriptsLuaScripts.fchunk',exe.parent/'media/ScriptsChapter3.fchunk')
        host=subprocess.run(['lua5.1','-e','io.write(string.dump(function()end):sub(1,12))'],check=True,capture_output=True).stdout
        with tempfile.TemporaryDirectory() as d:
            paths=[]
            for key in ('goal','hud','callback','payload','owner'):
                out=Path(d)/(key+'.luac');out.write_bytes(dump(proto[key],host));paths.append(out)
            run(['lua5.1',ROOT/'tools/test_paranoia.lua',*paths],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
    def lua_world(self,mode,*private):
        env={**os.environ,'DSF_MOD_ROOT':str(ROOT)}
        run([shutil.which('lua5.1'),ROOT/'tools/test_world.lua',mode,*private],env=env)
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required for offline script fixtures')
    def test_world_supported_timer_freeze_resume_and_native_isolation(self):
        self.lua_world('timer')
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required for offline script fixtures')
    def test_world_weather_native_moods_write_only_sky_and_cleanup(self):
        self.lua_world('weather')
    @unittest.skipUnless(shutil.which('lua5.1'),'Lua 5.1 required')
    def test_street_lua_failure_does_not_disable_time_or_weather(self):
        self.lua_world('street_error')
    @unittest.skipUnless(CXX,'g++ required')
    def test_world_production_callbacks_stack_errors_and_independent_dispatch(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d)
            world=(ROOT/'src/world_lua.h').read_text().split('static void draw()',1)[0]+'}\n'
            nitro=(ROOT/'src/nitro_lua.h').read_text().split('static bool scriptsSupported()',1)[0]+'#endif\n}\n'
            from lua51_chunk import compile_script,include
            from embed_nitro_script import script_source
            for stem in ('world','nitro'):
                lua=script_source(stem);name='worldSource' if stem=='world' else 'source'
                (tmp/f'{stem}_script.inc').write_text(include(name,compile_script(lua,f'{stem}_script.lua')))
            (tmp/'world_lua.h').write_text(world.replace('../build/world_script.inc','world_script.inc'))
            (tmp/'world_api_under_test.h').write_text(nitro.replace('../build/nitro_script.inc','nitro_script.inc'))
            run([cc32(tmp,'test_world_api.cpp')])
    @unittest.skipUnless(os.environ.get('DSF_GAME_EXE') and shutil.which('lua5.1'),'Optional: private supported game and Lua 5.1')
    def test_optional_private_native_goal_and_mood_prototypes(self):
        from private_lua import selected,dump
        exe=Path(os.environ['DSF_GAME_EXE'])
        self.assertEqual(hashlib.sha256(exe.read_bytes()).hexdigest(),json.loads((ROOT/'release.json').read_text())['driver_exe_sha256'])
        proto=selected(exe.parent/'media/ScriptsLuaScripts.fchunk')
        host=subprocess.run([shutil.which('lua5.1'),'-e','io.write(string.dump(function()end):sub(1,12))'],check=True,capture_output=True).stdout
        with tempfile.TemporaryDirectory() as d:
            paths={name:Path(d)/(name+'.luac') for name in proto}
            for name,p in proto.items():paths[name].write_bytes(dump(p,host))
            self.lua_world('timer',paths['goal'],paths['moods'],paths['instance_goal'])
            self.lua_world('weather',paths['goal'],paths['moods'])
    @unittest.skipUnless(os.environ.get('DSF_GAME_EXE') and shutil.which('lua5.1'),'Optional: private supported game and Lua 5.1')
    def test_optional_private_street_prototypes_and_controller_binding(self):
        from private_lua import selected,dump
        exe=Path(os.environ['DSF_GAME_EXE']);data=exe.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),json.loads((ROOT/'release.json').read_text())['driver_exe_sha256'])
        # Static native policy/lifecycle proof, never execute code from the game.
        pe=struct.unpack_from('<I',data,0x3c)[0];optional=struct.unpack_from('<H',data,pe+20)[0]
        base=struct.unpack_from('<I',data,pe+24+28)[0]
        def at(address,size):
            for i in range(struct.unpack_from('<H',data,pe+6)[0]):
                off=pe+24+optional+i*40;_,rva,n,raw=struct.unpack_from('<IIII',data,off+8)
                if base+rva<=address and address+size<=base+rva+n:return data[raw+address-base-rva:raw+address-base-rva+size]
            self.fail('Unmapped native address '+hex(address))
        self.assertEqual(at(0x8066e0,11),bytes.fromhex('8a44240488417d84c07505')) # false invokes fleet cleanup
        for source,target in ((0x6872a6,0x8066e0),(0x8066eb,0x806640),
                              (0x806672,0x8078f0),(0x80669a,0x76fbd0),(0x8066a2,0x7704f0),
                              (0x686989,0x806150),(0x806175,0x8037b0),
                              (0x803818,0x8034b0),(0x803879,0x803590)):
            code=at(source,5);self.assertEqual(code[0],0xe8)
            self.assertEqual(source+5+struct.unpack_from('<i',code,1)[0],target)
        p=selected(exe.parent/'media/ScriptsLuaScripts.fchunk',include_streets=True)
        host=subprocess.run(['lua5.1','-e','io.write(string.dump(function()end):sub(1,12))'],check=True,capture_output=True).stdout
        with tempfile.TemporaryDirectory() as d:
            paths=[]
            for name in ('patrol','patrol_spawn','road','player_task'):
                out=Path(d)/(name+'.luac');out.write_bytes(dump(p[name],host));paths.append(out)
            run(['lua5.1',ROOT/'tools/test_streets.lua',*paths],env={**os.environ,'DSF_MOD_ROOT':str(ROOT)})
        values=re.search(r'const unsigned char sig\[\]=\{([^}]+)\}',(ROOT/'src/controller.h').read_text()).group(1)
        signature=bytes(int(v,0) for v in values.split(','));self.assertEqual(data.count(signature),1)
        body=data[data.index(signature):data.index(signature)+0x2de];h=2166136261
        for x in body:h=((h^x)*16777619)&0xffffffff
        self.assertEqual(h,0x483ed10a)
    @unittest.skipUnless(CXX,'g++ required')
    def test_actual_native_paint_dispatch_with_mock_native(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'paint',name='paint_dispatch_under_test.h')
            run([cc32(tmp,'test_paint.cpp')])
    @unittest.skipUnless(CXX,'g++ required')
    def test_actual_vehicle_hooks_and_register_contract_with_mock_native(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);header(tmp,'vehicle','// Exact instruction signatures','vehicle_hooks_under_test.h')
            s=(ROOT/'src/vehicle.h').read_text();block=s.split('static void disableAll() {',1)[1].split('\n}',1)[0]
            (tmp/'vehicle_disable_under_test.h').write_text('static void disableAll() {'+block+'\n}\n')
            run([cc32(tmp,'test_v7_hooks.cpp',[ROOT/'src/nitro_bridge.S',ROOT/'tools/test_nitro_abi.S'])])
    @unittest.skipUnless(CXX,'g++ required')
    def test_actual_imgui_arabic_english_layout_font_and_mock_settings(self):
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);b=tmp/'ui'
            for stem in ('powertrain','world_lua','signal_controls'):
                body='static void draw()'+(ROOT/f'src/{stem}.h').read_text().split('static void draw()',1)[1]
                name='world' if stem=='world_lua' else 'signals' if stem=='signal_controls' else stem
                (tmp/f'{name}_draw_under_test.h').write_text(body)
            run([CXX,'-std=c++17','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all',
                 '-I'+str(ROOT/'vendor/imgui'),'-I'+str(ROOT/'src'),'-I'+str(tmp),ROOT/'tools/test_ui.cpp',
                 *[ROOT/'vendor/imgui'/p for p in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']],'-o',b])
            run([b])
    @unittest.skipUnless(os.environ.get('DSF_GAME_EXE'),'Optional: set DSF_GAME_EXE to your supported private Driver.exe')
    def test_optional_private_executable_and_powertrain_binding(self):
        b=Path(os.environ['DSF_GAME_EXE']).read_bytes()
        self.assertEqual(hashlib.sha256(b).hexdigest(),json.loads((ROOT/'release.json').read_text())['driver_exe_sha256'])
        signatures=re.findall(r'const unsigned char (\w+Sig)\[\]=\{([^}]+)\}',(ROOT/'src/nitro_lua.h').read_text())
        self.assertEqual(len(signatures),14)
        for name,values in signatures:
            signature=bytes(int(v,0) for v in values.split(','))
            self.assertEqual(b.count(signature),1,name+' must match exactly once in supported native executable')
        traffic_signatures=re.findall(r'const unsigned char (\w+Sig)\[\]=\{([^}]+)\}',(ROOT/'src/traffic.h').read_text())
        self.assertEqual(len(traffic_signatures),5);traffic_offsets={}
        for name,values in traffic_signatures:
            sig=bytes(int(v,0) for v in values.split(','));self.assertEqual(b.count(sig),1,name)
            traffic_offsets[name]=b.index(sig)
        passenger_signatures=re.findall(r'const unsigned char (\w+Sig)\[\]=\{([^}]+)\}',(ROOT/'src/passengers.h').read_text())
        self.assertEqual(len(passenger_signatures),5)
        for name,values in passenger_signatures:
            sig=bytes(int(v,0) for v in values.split(','));self.assertEqual(b.count(sig),1,name)
        signal_signatures=re.findall(r'const unsigned char (\w+Sig)\[\]=\{([^}]+)\}',(ROOT/'src/signals.h').read_text())
        self.assertEqual(len(signal_signatures),1)
        for name,values in signal_signatures:
            sig=bytes(int(v,0) for v in values.split(','));self.assertEqual(b.count(sig),1,name)
        self.assertEqual(b.count(bytes.fromhex('0fb7d8b8ffff0000663bd80f84b3000000')),1) # empty seat skips replacement
        call=traffic_offsets['callerSig'];target=call+25+struct.unpack_from('<i',b,call+21)[0]
        self.assertEqual(target,traffic_offsets['nearbySig'])
        q=traffic_offsets['querySig']
        self.assertEqual(b[q+0x47:q+0x4e],bytes.fromhex('c686081e000000')) # native dirty flag
        self.assertEqual(b[q+0x4e:q+0x54],bytes.fromhex('b0015ec20800')) # AL result / ret 8
        pe=struct.unpack_from('<I',b,0x3c)[0];n=struct.unpack_from('<H',b,pe+6)[0];optional=struct.unpack_from('<H',b,pe+20)[0]
        base=struct.unpack_from('<I',b,pe+24+28)[0];address=0x8c0df0;body=None
        for i in range(n):
            off=pe+24+optional+i*40;_,rva,size,raw=struct.unpack_from('<IIII',b,off+8)
            if base+rva<=address and address+0x881<=base+rva+size:body=b[raw+address-base-rva:raw+address-base-rva+0x881]
        self.assertIsNotNone(body)
        self.assertEqual(hashlib.sha256(body).hexdigest(),'4966ac49ea386d25b097df06cbb0857d78ffca9b7b962b2ad88cd0194925ace6')
        with tempfile.TemporaryDirectory() as d:
            tmp=Path(d);(tmp/'engine.bin').write_bytes(body);header(tmp,'speed');header(tmp,'powertrain')
            run([cc32(tmp,'test_powertrain.cpp',[ROOT/'src/speed_bridge.S']),tmp/'engine.bin'])
if __name__=='__main__':unittest.main(verbosity=2)
