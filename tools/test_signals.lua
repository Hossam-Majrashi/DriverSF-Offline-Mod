-- Production street dispatcher / MOCK signal bridge, independent of police/traffic.
local root=assert(os.getenv('DSF_MOD_ROOT'))
local enabled,allowed,task,vehicleTask,pumps,releases,record
local function agent(n)
 local a={gameVehicle={id=n},getTaskObject=function()return vehicleTask end}
 vehicleManager.vehiclesByGameVehicle[a.gameVehicle]=a;return a
end
local function backend(handle,run)
 if not handle then record=nil;return true end
 assert(vehicleManager.vehiclesByGameVehicle[handle])
 if run then assert(localPlayer.currentVehicle.gameVehicle==handle and not localPlayer.inZap);pumps=pumps+1;record=handle
 else assert(record==handle);releases=releases+1;record=nil end
 return true
end
local function reset()
 enabled=false;allowed=true;task=nil;vehicleTask=nil;pumps=0;releases=0;record=nil
 dareSystem=nil;progressionSystem=nil;gameStatus={}
 vehicleManager={vehiclesByGameVehicle={}}
 localPlayer={getTaskObject=function()return task end};localPlayer.currentVehicle=agent(1)
 local function read()return false,false,false,false,false,false,false,false,false,false,allowed,nil,false,nil,enabled,backend end
 return assert(loadfile(root..'/src/street_script.lua'))(read),localPlayer.currentVehicle
end
local f,a=reset();f(true);assert(pumps==0 and not record)
enabled=true;f(true);f(true);assert(pumps==2) -- lease renewed before traffic throttle / no pending UI command needed
localPlayer.currentVehicle=agent(2);f(true);assert(releases==1 and pumps==3)
enabled=false;f(true);assert(releases==2 and not record)
f,a=reset();enabled=true;f(true);vehicleManager.vehiclesByGameVehicle[a.gameVehicle]=nil
localPlayer.currentVehicle=agent(3);f(true);assert(releases==0 and pumps==2) -- no stale deref
for _,guard in ipairs({'mission','actor','online','cutscene','dare','chapter','shift','pause','failed'}) do
 f,a=reset();enabled=true;f(true);local safe=true
 if guard=='mission' then task={} elseif guard=='actor' then vehicleTask={} elseif guard=='online' then safe=false
 elseif guard=='cutscene' then localPlayer.inCutsceneOrIcam=true
 elseif guard=='dare' then dareSystem={isDareActive=function()return true end}
 elseif guard=='chapter' then progressionSystem={applyingChapterSettings=true}
 elseif guard=='shift' then localPlayer.inZap=true elseif guard=='pause' then gameStatus.simulationPaused=true elseif guard=='failed' then allowed=false end
 f(safe);assert(releases==1 and not record and pumps==1)
end
print('Signals Lua: independent enable/lease dispatch, live player-only agent/handle, prior-car/OFF restore, stale-owner discard, mission/actor/online/cutscene/dare/chapter/Shift/error suspension; MOCK, NOT gameplay.')
