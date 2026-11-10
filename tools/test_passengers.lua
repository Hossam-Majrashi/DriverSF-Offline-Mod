-- Production authored lifecycle with MOCK agents/native per-seat backend.
local root=assert(os.getenv('DSF_MOD_ROOT'))
local alone,allowed,safe,task,vehicleTask,clears,releases,discards,record
local function agent(n)
 local a={gameVehicle={id=n},getTaskObject=function()return vehicleTask end}
 vehicleManager.vehiclesByGameVehicle[a.gameVehicle]=a;return a
end
local function backend(handle,clear)
 if not handle then discards=discards+1;record=nil;return true end
 local a=vehicleManager.vehiclesByGameVehicle[handle];assert(a and a.gameVehicle==handle)
 if clear then assert(localPlayer.currentVehicle==a and not localPlayer.inZap);if record~=handle then clears=clears+1 end;record=handle
 else assert(record==handle);releases=releases+1;record=nil end
 return true
end
local function reset()
 alone=true;allowed=true;safe=true;task=nil;vehicleTask=nil;clears=0;releases=0;discards=0;record=nil
 localPlayer={getTaskObject=function()return task end}
 vehicleManager={vehiclesByGameVehicle={},deleteVehicle=function()error('no car deletion')end}
 Sound={DisableSpeech=function()error('no global dialogue mute')end}
 localPlayer.currentVehicle=agent(1)
 local function read()return false,false,false,false,false,false,false,false,false,false,allowed,nil,alone,backend end
 return assert(loadfile(root..'/src/street_script.lua'))(read),localPlayer.currentVehicle
end
local f,a=reset();assert(not select(2,f(true)));assert(clears==1 and releases==0)
f(true);assert(clears==1) -- no periodic occupant repaint
localPlayer.currentVehicle=agent(2);f(true);assert(clears==2 and releases==1)
alone=false;f(true);assert(releases==2 and not record)
alone=true;f(true);assert(clears==3)
localPlayer.inZap=true;f(true);assert(releases==3 and not record)
localPlayer.inZap=false;f(true);assert(clears==4)
-- A removed/replaced registry must never be used to dereference stale userdata.
f,a=reset();f(true);vehicleManager.vehiclesByGameVehicle[a.gameVehicle]=nil
localPlayer.currentVehicle=agent(3);f(true);assert(releases==0 and clears==2)
f,a=reset();f(true);vehicleManager={vehiclesByGameVehicle={}}
localPlayer.currentVehicle=agent(4);f(true);assert(releases==0 and clears==2)
-- Mission actors are protected even if the main free-drive task is empty.
f,a=reset();vehicleTask={};f(true);assert(clears==0);vehicleTask=false;f(true);assert(clears==1)
for _,guard in ipairs({'mission','online','cutscene','dare','chapter','failed'}) do
 f,a=reset();f(true)
 if guard=='mission' then task={} elseif guard=='online' then safe=false
 elseif guard=='cutscene' then localPlayer.inCutsceneOrIcam=true
 elseif guard=='dare' then dareSystem={isDareActive=function()return true end}
 elseif guard=='chapter' then progressionSystem={applyingChapterSettings=true}
 elseif guard=='failed' then allowed=false end
 f(safe);assert(releases==1 and alone and not record)
 task=false;safe=true;localPlayer.inCutsceneOrIcam=false;dareSystem=nil;progressionSystem=nil;allowed=true
 f(true);assert(clears==2) -- armed default resumes ONLY in safe free drive
end
-- Default intentionally applies in a fresh VM without replaying acknowledged
-- traffic/police commands. Missing agent/handle mapping fails closed.
f,a=reset();a.getTaskObject=nil;f(true);assert(clears==0)
f,a=reset();vehicleManager.vehiclesByGameVehicle={};f(true);assert(clears==0)
print('Passenger authored Lua: default armed, player-only live agent/handle registry, shift/OFF restoration, stale-owner discard, mission/actor/offline/cutscene/dare/chapter/error guards and resume, no mute/deletion; MOCK only, NOT gameplay/audio acceptance.')
