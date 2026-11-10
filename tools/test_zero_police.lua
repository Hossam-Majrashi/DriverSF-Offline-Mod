-- Production authored street dispatcher / MOCK stock police + traffic lifecycle.
-- These are policy/ordering assertions, NOT proof that game actors disappeared.
local root=assert(os.getenv('DSF_MOD_ROOT'))
local factory=assert(loadfile(root..'/src/street_script.lua'))
local t,p,percent,pending,allowed,active,e,s,fleet,chasers,civil,commits,stops,drains,trace,task,supported,rejectCommit,zeroOwned
local function reset()
 t,p,percent,pending,allowed=true,false,0,true,true
 active,e,s,fleet,chasers,civil=true,true,true,3,2,true
 commits,stops,drains,trace,task,supported,rejectCommit,zeroOwned=0,0,0,{},nil,true,false,false
 atlas=nil;dareSystem=nil;progressionSystem=nil;g_NetworkTime=10
 vehicleManager={deleteVehicle=function()error('no global/mod vehicle deletion')end}
 localPlayer={getTaskObject=function()return task end,currentVehicle={gameVehicle={player=true}}}
 PatrollingVehicleManager={IsEnabled=function()return e end,IsSpawningEnabled=function()return s end,
  Enable=function(on)
   e=on;if not on then drains=drains+1;fleet=0;trace[#trace+1]='patrol-retired' end
  end,EnableSpawning=function(on)s=on end}
 Getaway={IsAGetawayActive=function()return active end,StopAll=function()
  stops=stops+1;trace[#trace+1]='stop';active=false
  -- Simulate stock cancellation callback requesting patrol re-enablement.
  PatrollingVehicleManager.Enable(true);PatrollingVehicleManager.EnableSpawning(true)
  -- Worst-case ordering: returned chasers appear AFTER that callback's cleanup.
  fleet=fleet+chasers;chasers=0;trace[#trace+1]='returned-chasers'
 end}
 local function backend(on,probe)
  if probe then assert(on and percent==0);return supported end -- READ ONLY
  if not on then zeroOwned=false;civil=true;return true end
  if not supported or rejectCommit then t=false;return false end -- actual C bridge rejects density only
  commits=commits+1
  if percent==0 then
   assert(not active and not e and not s and fleet==0)
   if not zeroOwned then trace[#trace+1]='civil-off';zeroOwned=true;civil=false end
  else zeroOwned=false;civil=true end
  return true
 end
 local function bit(i)return math.floor(percent/2^i)%2==1 end
 local function read()return t,bit(0),bit(1),bit(2),bit(3),bit(4),bit(5),bit(6),p,pending,allowed,backend end
 return factory(read)
end
local function step(f,safe)local h,c=f(safe~=false);if h then pending=false end;return h,c end
local f=reset();assert(not select(2,step(f)));assert(not p and not e and not s and not active and not civil and fleet==0 and stops==1)
assert(trace[#trace]=='civil-off' and trace[#trace-1]=='patrol-retired' and trace[#trace-2]=='returned-chasers')
local before=drains;g_NetworkTime=10.01;step(f);assert(commits==2 and stops==1 and drains==before) -- renew only, no repeating cleanup
active=true;chasers=1;g_NetworkTime=10.02;step(f);assert(stops==2 and not active and fleet==0 and not civil) -- cancel before legacy throttle
percent=50;g_NetworkTime=10.03;step(f);assert(e and s and civil and not p) -- immediate ZERO-only restoration
f=reset();step(f);t=false;g_NetworkTime=10.01;step(f);assert(e and s and civil)
f=reset();p=true;step(f);percent=50;g_NetworkTime=10.01;step(f);assert(not e and not s and civil and p)
p=false;pending=true;step(f);assert(e and s) -- independent switch still owns its own lifetime
f=reset();p=true;step(f);p=false;pending=true;step(f);assert(not e and not s) -- ZERO still requires police block
percent=100;pending=true;step(f);assert(e and s and civil)
f=reset();active=false;e=false;s=false;step(f);percent=1;pending=true;step(f);assert(not e and not s) -- native initial OFF preserved
for _,guard in ipairs({'mission','online','cutscene','dare','chapter','errors'}) do
 f=reset();step(f);active=true;chasers=1
 if guard=='mission' then task={} elseif guard=='cutscene' then localPlayer.inCutsceneOrIcam=true
 elseif guard=='dare' then dareSystem={isDareActive=function()return true end}
 elseif guard=='chapter' then progressionSystem={applyingChapterSettings=true} elseif guard=='errors' then allowed=false end
 assert(select(2,step(f,guard~='online')));assert(active and stops==1 and chasers==1 and e and s and civil)
end
f=reset();pending=false;assert(select(2,step(f)));assert(stops==0 and drains==0 and commits==0) -- fresh VM no replay
f=reset();supported=false;step(f);assert(stops==0 and drains==0 and e and s and active) -- failed preflight has no irreversible effects
f=reset();supported=false;p=true;step(f);assert(stops==1 and not e and not s) -- independent police request works without ZERO
for _,missing in ipairs({'Getaway','EnableSpawning'}) do
 f=reset();if missing=='Getaway' then Getaway.StopAll=nil else PatrollingVehicleManager.EnableSpawning=nil end
 assert(select(2,step(f)));assert(stops==0 and drains==0 and commits==0 and civil and active)
end
f=reset();rejectCommit=true;step(f);assert(e and s and civil and not t) -- no leftover automatic block after failed commit
f=reset();rejectCommit=true;p=true;assert(not select(2,step(f)));assert(not e and not s and civil and p and not t)
f=reset();step(f);PatrollingVehicleManager.Enable(false);assert(select(2,step(f)));assert(not e and s and civil and stops==1) -- foreign same-value wins
f=reset();step(f);e=true;active=true;assert(select(2,step(f)));assert(e and s and civil and active and stops==1) -- direct native flag wins
f=reset();local stop=Getaway.StopAll;Getaway.StopAll=function()stop();task={} end
assert(select(2,step(f)));assert(commits==0 and civil and e and s) -- callback switched context; do not commit traffic OFF
print('ZERO police production Lua: automatic block with independent switch OFF, StopAll -> returned-patrol native retirement -> civilian OFF ordering, no periodic deletion/repaint, fast reappearing-pursuit/0-exit cleanup, explicit police intent, native initial OFF, failed preflight/commit/API, callback/native precedence and mission/online/VM guards; MOCK only, NOT visual/gameplay acceptance.')
