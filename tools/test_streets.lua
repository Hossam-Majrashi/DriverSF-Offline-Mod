-- MOCK native APIs; authored production Lua, not in-game population acceptance.
local root=assert(os.getenv('DSF_MOD_ROOT'))
local nativePolicy,nativeSpawn,nativeRoad,nativeTask=arg[1],arg[2],arg[3],arg[4]
local factory=assert(loadfile(root..'/src/street_script.lua'))
if os.getenv('DSF_WORLD_CHUNK') then
 local makeWorld=assert(loadfile(os.getenv('DSF_WORLD_CHUNK')))
 factory=function(read)
  goalSystem={activeGoals={}};moodSystem={moodStack={n=0}};localPlayerManager={players={}}
  Network={isOnlineGame=function()return false end};gameStatus={}
  local world=makeWorld(function()return false end,function()return false,false end,read)
  return function(safe)
   gameStatus.onlineSession=not safe
   local _,_,_,handled,canceled,err=world();assert(err==nil,err);return handled,canceled
  end
 end
end
local t,p,pending,allowed,percent
local e,s,active,calls,task,backend
local function bits(n,i)return math.floor(n/2^i)%2==1 end
local function read()
 return t,bits(percent,0),bits(percent,1),bits(percent,2),bits(percent,3),bits(percent,4),bits(percent,5),bits(percent,6),p,pending,allowed,backend
end
local function reset()
 t,p,pending,allowed,percent=false,false,false,true,100
 vehicleManager={deleteVehicle=function()error("removed chaser deletion must never run")end};backend=nil
 e,s,active,calls,task=true,true,false,{},nil
 g_NetworkTime=10
 localPlayer={getTaskObject=function()return task end}
 if nativeTask then
  local getter=assert(loadfile(nativeTask))
  setfenv(getter,setmetatable({missionSupport={getMainTaskObject=function()return task end}},{__index=_G}))
  localPlayer.getTaskObject=getter
 end
 local params={pingInProbabilityWith=.8,pingInProbabilityAgainst=.6,trafficSpeedWith=40,halfLaneWidth=2}
 -- Real road parameters are userdata: do not hide __index/__newindex behavior.
 local commits=0
 local road=setmetatable({}, {__index=function(_,key)
  if key~='trafficParameters' then return nil end
  local copy={};for k,v in pairs(params) do copy[k]=v end
  local u=newproxy(true);local m=getmetatable(u);m.__index=copy;m.__newindex=function(_,k,v)copy[k]=v end
  return u -- value-copy getter: mutations MUST use the native road setter
 end,__newindex=function(_,key,u)
  assert(key=='trafficParameters' and type(u)=='userdata');commits=commits+1
  for k in pairs(params) do params[k]=u[k] end
 end})
 atlas={road={[1]=road,[2]={trafficParameters={pingInProbabilityWith=.2,pingInProbabilityAgainst=.4}}}}
 PatrollingVehicleManager={IsEnabled=function()return e end,IsSpawningEnabled=function()return s end,
 Enable=function(v)assert(type(v)=='boolean');e=v;return 'enabled',nil,false end,
 EnableSpawning=function(v)assert(type(v)=='boolean');s=v end}
 felony_patrollingVehicleManager={enablePatrollingVehicles=function(v)
  assert(v==true);calls.enable=(calls.enable or 0)+1;PatrollingVehicleManager.Enable(true);PatrollingVehicleManager.EnableSpawning(true)
 end}
 if nativePolicy then
  challengeProgressionTable={[1]={settings={}}};progressionSystem={currentProgression=1}
  local native=assert(loadfile(nativePolicy))
  setfenv(native,setmetatable({enablePatrollingVehiclesSpawning=assert(loadfile(nativeSpawn))},{__index=_G}))
  felony_patrollingVehicleManager.enablePatrollingVehicles=function(v)calls.enable=(calls.enable or 0)+1;return native(v) end
 end
 if nativeRoad then
  local original=atlas.road[1].trafficParameters
  local setter=assert(loadfile(nativeRoad));local before=commits
  setter(nil,1,original);assert(commits==before+1)
 end
 Getaway={IsAGetawayActive=function()return active end,
 StopAll=function()calls.stop=(calls.stop or 0)+1;active=false;felony_patrollingVehicleManager.enablePatrollingVehicles(true) end,
 SetSettings=function()error("removed chaser tuning must never run")end}
 return factory(read),params
end
local function sync(f,safe)
 local h,c=f(safe~=false);if h then pending=false end;return h,c
end
local function advance()g_NetworkTime=g_NetworkTime+2 end
local f,params=reset();sync(f);assert(e and s and params.pingInProbabilityWith==.8 and not next(calls))
t=true;percent=25;pending=true;assert(select(1,sync(f)));assert(params.pingInProbabilityWith==.2 and params.pingInProbabilityAgainst==.15)
assert(params.trafficSpeedWith==40 and params.halfLaneWidth==2)
percent=50;pending=true;sync(f);assert(params.pingInProbabilityWith==.4)
t=false;pending=true;sync(f);assert(params.pingInProbabilityWith==.8 and params.pingInProbabilityAgainst==.6)
-- Foreign writes win. Restore only our other owned field, not that native edit.
f,params=reset();t=true;percent=1;pending=true;sync(f);params.pingInProbabilityWith=.3;advance()
assert(select(2,sync(f)));assert(params.pingInProbabilityWith==.3 and params.pingInProbabilityAgainst==.6)
-- Do not touch stale userdata/containers after world replacement.
f,params=reset();t=true;percent=1;pending=true;sync(f);local old=atlas.road;atlas.road={};advance();assert(select(2,sync(f)))
assert(params.pingInProbabilityWith==.008 and old~=atlas.road)
f=reset();p=true;active=true;pending=true;sync(f);assert(not e and not s and not active and calls.stop==1)
-- Retail native free drive uses false, not just nil, for no task.
f,params=reset();task=false;p=true;active=true;pending=true
local _,rejected=sync(f);assert(not rejected and not e and not s and calls.stop==1)
p=false;pending=true;sync(f);assert(e and s)
t=true;percent=25;pending=true;assert(not select(2,sync(f)));assert(params.pingInProbabilityWith==.2)
t=false;p=true;active=true;pending=true;sync(f);calls.stop=1
-- Native cleanup of a newly appeared ambient pursuit cannot re-enable our block.
active=true;advance();sync(f);assert(not e and not s and not active and calls.stop==2)
p=false;pending=true;sync(f);assert(e and s)
f=reset();e=false;s=false;p=true;pending=true;sync(f);p=false;pending=true;sync(f);assert(not e and not s)
f=reset();p=true;pending=true;sync(f);local a,b,c=PatrollingVehicleManager.Enable(true)
assert(a=='enabled' and b==nil and c==false);advance();assert(select(2,sync(f)));assert(e and s)
-- Exact replacement by another mod wins, including cleanup.
f=reset();p=true;pending=true;sync(f);local other=function(v)e=v end;PatrollingVehicleManager.Enable=other
p=false;pending=true;sync(f);assert(PatrollingVehicleManager.Enable==other)
-- Count/reinforcement/deletion controls were removed; the remaining police
-- switch cancels an ambient pursuit and restores only its owned patrol flags.
-- Missions / online / cutscenes refuse commands, restore owned state, never StopAll.
for _,which in ipairs({'mission','online','cutscene'}) do
 f,params=reset();t=true;p=true;percent=50;pending=true;sync(f)
 if which=='mission' then task={} elseif which=='cutscene' then localPlayer.inCutsceneOrIcam=true end
 active=true;pending=true;advance();assert(select(2,sync(f,which~='online')))
 assert(e and s and params.pingInProbabilityWith==.8 and not calls.stop)
end
-- Stale/fresh VM cannot replay acknowledged intent; unsupported APIs fail closed.
f=reset();t=true;pending=false;assert(select(2,sync(f)));assert(e and s)
f=reset();p=true;pending=true;PatrollingVehicleManager.IsEnabled=nil;assert(select(2,sync(f)));assert(e and s)
f=reset();t=true;pending=true;atlas.road={};assert(select(2,sync(f)))
-- Known retail absence is reported separately, never undoing working police.
f=reset();t=true;p=true;pending=true;atlas=nil;assert(not select(2,sync(f)));assert(not e and not s)
f=reset();p=true;pending=true;allowed=false;assert(select(2,sync(f)));assert(e and s)
-- Retail backend renews its lease before the one-second legacy throttle.
f=reset();atlas=nil;local nativeOn=false;local renewals=0
backend=function(on) nativeOn=on;if on then renewals=renewals+1 end;return true end
t=true;p=true;pending=true;percent=1;assert(not select(2,sync(f)));assert(nativeOn and not e and not s)
g_NetworkTime=g_NetworkTime+.01;sync(f);assert(renewals==2 and nativeOn)
t=false;g_NetworkTime=g_NetworkTime+.01;sync(f);assert(not nativeOn and not e)
t=true;pending=true;sync(f);assert(nativeOn);task={};sync(f);assert(not nativeOn)
f=reset();atlas=nil;backend=function(on)nativeOn=on;return true end
t=true;pending=false;assert(select(2,sync(f)));assert(not nativeOn) -- no VM replay
-- Zero may never masquerade as zero spawn probability while old cars remain.
f,params=reset();t=true;p=true;pending=true;percent=0;assert(not select(2,sync(f)))
assert(params.pingInProbabilityWith==.8 and not e and not s)
f=reset();atlas=nil;backend=function(on)nativeOn=on;return true end
percent=0;t=true;pending=true;assert(not select(2,sync(f)) and nativeOn)
t=false;pending=true;sync(f);assert(not nativeOn)
print('Street production Lua / MOCK APIs: spawn probabilities, userdata ownership, native precedence, single police OFF/restore/StopAll, no chaser tuning/deletion, independent unavailable density, native lease renewal and free-drive/online/VM guards passed. NOT gameplay.')
