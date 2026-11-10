-- Authored mock fixtures. No proprietary script source is included.
-- Optional arguments load only caller-supplied, hash-verified native prototypes.
local mode, nativeGoal, nativeGroups, nativeInstance = arg[1], arg[2], arg[3], arg[4]
local root=assert(os.getenv('DSF_MOD_ROOT'))
local makeWorld=assert(loadfile(os.getenv('DSF_WORLD_CHUNK') or root..'/src/world_script.lua'))
local online,split,freeze,preset,pending=false,false,false,0,false
local moodCalls,skyWrites,callbacks={}, {}, {}
local smoothWeather=false
local reports={}
local function pad(lines,line,text)
    while #lines<line-1 do lines[#lines+1]='' end
    lines[#lines+1]=text
end
local function mockFactory()
    local s={'local startTime,feedback,limit,uid = ...; local goalReportedSuccessful=false'}
    pad(s,1805,'return function(...)')
    s[#s+1]='local elapsed=g_NetworkTime-startTime; if feedback then feedback(elapsed) end'
    s[#s+1]='if elapsed>=limit and not goalReportedSuccessful then goalSystem.callbackHandler(uid);goalReportedSuccessful=true end'
    s[#s+1]='return elapsed,select(1,...)'
    pad(s,1826,'end')
    return assert(loadstring(table.concat(s,'\n'),'@fixture\\goals\\goals.lua'))
end
local function mockInstance()
    local s={'local operandA,unused,UID,params=...;local goalReportedSuccessful=false'}
    pad(s,4598,'return function()')
    s[#s+1]='local elapsed=g_NetworkTime-operandA.instance.networkVars.startTime'
    s[#s+1]='if elapsed>=params.value and not goalReportedSuccessful then goalSystem.callbackHandler(UID);goalReportedSuccessful=true end'
    pad(s,4614,'end')
    return assert(loadstring(table.concat(s,'\n'),'@fixture\\goals\\goals.lua'))
end
local function mockHud()
    local s={'local startTime,setStartTime,timeRemaining=0,false,0; local timerUpdate'}
    pad(s,921,'timerUpdate=function(params)')
    s[#s+1]='if not setStartTime then startTime=g_NetworkTime;setStartTime=true end'
    s[#s+1]='timeRemaining=params.startTime and params.startTime-(g_NetworkTime-startTime) or g_NetworkTime-startTime'
    s[#s+1]='return timeRemaining'
    pad(s,1043,'end')
    pad(s,1046,'return function(params)')
    s[#s+1]='return timerUpdate(params)'
    pad(s,1131,'end')
    return assert(loadstring(table.concat(s,'\n'),'@fixture\\feedbacksystemfunctions.lua'))()
end
local function reset()
    online,split,freeze,preset,pending=false,false,false,0,false
    moodCalls,skyWrites,callbacks={}, {}, {};smoothWeather=false;reports={}
    Network={isOnlineGame=function()return online end,isSplitScreenMode=function()return split end}
    gameStatus={onlineSession=false,splitscreenSession=false}
    g_NetworkTime=10
    taskSystem={getTaskBaseParameters=function(task)return task.base end}
    goalSystem={activeGoals={},taskSupport={registeredObjects={}},
        callbackHandler=function(uid)callbacks[uid]=(callbacks[uid] or 0)+1 end}
    feedbackSystem={stepTimer=mockHud()}
    local mission={}
    localPlayer={getTaskObject=function()return {coreData={instance=mission}} end}
    progressionSystem={currentChapter=2}
    localPlayerManager={players={[0]={}}}
    configSelector={launchConfig={InitialSky='Default'}}
    moodSystem={moodStack={n=1,[1]={sky='MissionSky'}}}
    local groups={ClearDay='ClearDay2',Fog='ClearDay2',Challenge_TheDriver='Sunset',JericohFull='Dreamy',Frozen='Chapter62'}
    moodSystem.moodGroups={}
    for name,skyName in pairs(groups) do
        moodSystem[name]={name=name};moodSystem[name..'Car']={name=name..'Car'}
        moodSystem.moodGroups[name]={main=moodSystem[name],incar=moodSystem[name..'Car'],sky=skyName}
    end
    if nativeGroups then
        setmetatable(moodSystem,{__index=_G})
        local fn=assert(loadfile(nativeGroups));setfenv(fn,moodSystem);fn()
    end
    -- The real requestSkyDome property is write-only: a normal table mock would
    -- conceal broken cleanup that tries to read the last requested sky name.
    sky=setmetatable({}, {__newindex=function(t,k,v)
        if k=='requestSkyDome' then assert(type(v)=='string');skyWrites[#skyWrites+1]=v
        else rawset(t,k,v) end
    end})
    Mood={}
    Mood.addMoodUserDefined=function(obj,key,weight,blend,cb,id,replay)
        assert(obj and key=='DriverSF.mod.weather' and weight==1 and (blend==0.5 or blend==0) and cb==nil and id==0 and replay==false)
        moodCalls[#moodCalls+1]={'main',key,blend}
    end
    Mood.addMoodInCar=function(obj,key,weight,blend,duration,id,replay)
        assert(obj and key=='DriverSF.mod.weather.IC' and weight==1 and (blend==0.5 or blend==0) and duration==-1 and id==0 and replay==false)
        moodCalls[#moodCalls+1]={'car',key,blend}
    end
    Mood.removeMood=function(key,blend,cb,id)
        assert((key=='DriverSF.mod.weather' or key=='DriverSF.mod.weather.IC') and (blend==0.5 or blend==0) and cb==nil and id==0)
        moodCalls[#moodCalls+1]={'remove',key}
    end
    return makeWorld(function()return freeze end,function()
        return preset>0,pending,preset==1,preset==2,preset==3,preset==4,preset==5,smoothWeather
    end,nil,function(message)reports[#reports+1]=message end)
end
local function run(sync)
    local found,handled,canceled=sync();if handled then pending=false end
    return found,handled,canceled
end
local function goal(uid,fail,first,limit,kind)
    -- Native taskSupport puts failCondition on the indexed BASE condition,
    -- not on the task base. Goal metadata is inherited through its condition.
    kind=kind or 'noTarget'
    local task={base={},instance={timerStartTime=first}}
    local g={name='Time trigger',goalKey=2,goalValue={value=limit,startTimeValue=true}}
    local definition={failCondition=fail,[2]={goal='Time trigger',params=g.goalValue}}
    local target=kind=='task' and -1 or kind=='dynamic' and 3 or 0
    local condition={__index={task=true,object=task,operandA=task,conditionKey=2,dynamicTargetID=target},[2]=g}
    setmetatable(g,condition)
    local registration={}
    if kind=='task' then
        task.base.taskConditions={[2]=definition};registration.taskCompletionConditions={[2]=condition}
    else
        task.base.goalConditions={[2]=definition}
        if kind=='dynamic' then registration.dynamicConditions={[3]={[2]=condition}}
        else registration.noTargetConditions={[2]=condition} end
    end
    goalSystem.taskSupport.registeredObjects[task]=registration
    local fb=function(n)g.elapsed=n end
    if nativeGoal then
        g.update=assert(loadfile(nativeGoal))(task,nil,uid,g.goalValue,fb)
    else g.update=mockFactory()(first,fb,limit,uid) end
    g.original=g.update;goalSystem.activeGoals[uid]=g;return g
end
local function instanceGoal(uid,fail,first,limit)
    local g=goal(uid,fail,first,limit)
    g.name='Instance time above'
    local task=g.operandA;task.instance.networkVars={startTime=first}
    task.base.goalConditions[2][2].goal=g.name
    g.update=(nativeInstance and assert(loadfile(nativeInstance)) or mockInstance())(task,nil,uid,g.goalValue)
    g.original=g.update
    return g
end
local function instanceTests()
    -- Mirror the actual logged 120-second retail deadline. Verify native FAIL
    -- callbacks, not just a frozen HUD. A second goal on the SAME instance is
    -- deliberately not a failure deadline and must still run on native time.
    local sync=reset();local g=instanceGoal(1,true,10,120)
    local other=instanceGoal(2,false,10,120)
    other.operandA.instance=g.operandA.instance
    local instance=g.operandA.instance;local vars=instance.networkVars
    local originalOperand=g.operandA
    feedbackSystem.stepTimer({startTime=120})
    g_NetworkTime=12;freeze=true;assert(run(sync))
    assert(feedbackSystem.stepTimer({startTime=120})==118)
    for n=1,125 do
        g_NetworkTime=12+n;run(sync);g.update();other.update()
        assert(g_NetworkTime==12+n and vars.startTime==10 and instance.networkVars==vars)
        assert(g.operandA==originalOperand and originalOperand.instance==instance)
        assert(callbacks[1]==nil and math.abs(feedbackSystem.stepTimer({startTime=120})-118)<1e-5)
    end
    assert(callbacks[2]==1 and other.update==other.original)
    -- OFF before sync must resume without instant failure/catch-up. Keep only
    -- a private, non-frozen compensator, then release it when the goal ends.
    freeze=false;g_NetworkTime=140;g.update();assert(callbacks[1]==nil)
    assert(run(sync) and g.update~=g.original)
    g_NetworkTime=257;g.update();assert(callbacks[1]==nil)
    g_NetworkTime=258;g.update();assert(callbacks[1]==1)
    goalSystem.activeGoals={};assert(not run(sync));assert(g.update==g.original)
    local restored=false
    for i=1,64 do local name,v=debug.getupvalue(g.original,i);if not name then break end
        if name=='operandA' then restored=v==originalOperand end
    end
    assert(restored and vars.startTime==10)
    -- New phase/reset, re-freeze, foreign replacement, bad/missing ownership,
    -- and offline guards. No writes to the original instance clock in any case.
    sync=reset();g=instanceGoal(1,true,10,120);freeze=true;assert(run(sync))
    g_NetworkTime=30;run(sync);g.update();freeze=false;run(sync)
    g.operandA.instance.networkVars.startTime=30
    assert(not run(sync) and g.update==g.original)
    freeze=true;assert(run(sync));g_NetworkTime=60;g.update();assert(callbacks[1]==nil)
    online=true;assert(not run(sync));assert(g.update==g.original)
    sync=reset();g=instanceGoal(1,true,10,120);freeze=true;run(sync)
    local foreign=function()return 19 end;g.update=foreign;freeze=false;run(sync);assert(g.update==foreign)
    sync=reset();g=instanceGoal(1,true,10,120);g.operandA.instance.networkVars.startTime=nil
    freeze=true;assert(not run(sync) and g.update==g.original)
    print('World Instance time above: native failure deadline + HUD freeze, shared-instance isolation, no catch-up on OFF, reset/retry/foreign/online cleanup passed. MOCK engine, optional native SCRIPT.')
end
local function timerTests()
    local probe=reset();freeze=true;assert(not run(probe))
    assert(#reports==1 and reports[1]:find('active=0 wrapped=0',1,true))
    run(probe);assert(#reports==1) -- no per-frame diagnostic spam
    -- Exercise all native registration paths, non-first condition/goal keys,
    -- and rejection of a misleading root failCondition or stale registration.
    for _,kind in ipairs({'task','noTarget','dynamic'}) do
        local sync=reset();local g=goal(1,true,10,30,kind)
        freeze=true;assert(run(sync),'native nested failure condition not detected: '..kind)
        g_NetworkTime=14;g.update();assert(g.elapsed==0)
        freeze=false;run(sync);assert(g.update==g.original)
        g_NetworkTime=15;g.update();assert(g.elapsed==1)
        sync=reset();g=goal(1,false,10,30,kind);g.operandA.base.failCondition=true
        freeze=true;assert(not run(sync),'root flag must not freeze a success condition')
        assert(g.update==g.original)
        sync=reset();g=goal(1,true,10,30,kind)
        goalSystem.taskSupport.registeredObjects[g.operandA]=nil
        freeze=true;assert(not run(sync),'stale/unregistered goal must stay native')
        sync=reset();g=goal(1,true,10,30,kind)
        local base=g.operandA.base
        if kind=='task' then
            base.goalConditions=base.taskConditions;base.taskConditions=nil
        else base.taskConditions=base.goalConditions;base.goalConditions=nil end
        freeze=true;assert(not run(sync),'must not fall back to an unrelated condition list')
        sync=reset();g=goal(1,true,10,30,kind)
        getmetatable(g).__index.conditionKey=1
        freeze=true;assert(not run(sync),'wrong condition index must stay native')
    end
    local sync=reset();local g=goal(1,true,10,30);local wait=goal(2,false,10,2)
    local wrong=goal(3,true,10,20);wrong.name='X time has past'
    local unknown={name='Time trigger',operandA=g.operandA,goalValue={value=3},update=function()return 'foreign' end}
    goalSystem.activeGoals[4]=unknown;local foreign=unknown.update
    g_NetworkTime=12;g.update();assert(g.elapsed==2)
    local originalHud=feedbackSystem.stepTimer
    local first=feedbackSystem.stepTimer({startTime=30});assert(first==30)
    freeze=true;assert(run(sync));assert(g.update~=g.original and wait.update==wait.original and unknown.update==foreign)
    assert(feedbackSystem.stepTimer({startTime=30})==30) -- first matching display establishes HUD ownership
    for n=1,100 do
        g_NetworkTime=12+n/10
        local a,b=g.update('native-argument');wait.update();wrong.update()
        if not nativeGoal then assert(a and b=='native-argument') end
        assert(math.abs(g.elapsed-2)<0.00001)
        assert(math.abs(feedbackSystem.stepTimer({startTime=30})-30)<0.00001)
        assert(feedbackSystem.stepTimer==originalHud and g_NetworkTime==12+n/10)
    end
    assert(callbacks[1]==nil and callbacks[2]==1)
    -- OFF is observed even before the next installer sync; no catch-up jump.
    g_NetworkTime=30;freeze=false;g.update();assert(math.abs(g.elapsed-2)<0.00001)
    run(sync);assert(g.update==g.original)
    g_NetworkTime=31;g.update();assert(math.abs(g.elapsed-3)<0.00001)
    g_NetworkTime=60;g.update();assert(callbacks[1]==1)
    -- Count-up, unmatched HUD countdowns and unsupported goals stay native.
    for _,params in ipairs({{}, {startTime=100}}) do
        sync=reset();g=goal(1,true,10,30);feedbackSystem.stepTimer(params)
        g_NetworkTime=12;freeze=true;run(sync);g_NetworkTime=14
        local result=feedbackSystem.stepTimer(params);assert(result==(params.startTime and 96 or 4))
        for n=1,5 do
            g_NetworkTime=14+n
            result=feedbackSystem.stepTimer(params);assert(result==(params.startTime and 96-n or 4+n))
        end
    end
    sync=reset();feedbackSystem.stepTimer({startTime=30});freeze=true;assert(not run(sync))
    g_NetworkTime=20;assert(feedbackSystem.stepTimer({startTime=30})==20)
    -- Captured validated debug accessors keep owned delegates safe if a module
    -- hides debug after installation; cleanup must not throw into native Lua.
    sync=reset();g=goal(1,true,10,30);freeze=true;run(sync)
    local savedDebug=debug;debug=nil;g_NetworkTime=12;g.update();assert(g.elapsed==0)
    freeze=false;run(sync);assert(g.update==g.original);debug=savedDebug
    -- Repeated clock values cannot accumulate fabricated frozen time; rewind
    -- delegates native elapsed-time behavior without writing the global clock.
    sync=reset();g=goal(1,true,10,30);freeze=true;run(sync)
    for n=1,20 do g.update();assert(g.elapsed==0 and g_NetworkTime==10) end
    g_NetworkTime=5;g.update();assert(g_NetworkTime==5 and g.elapsed==-5)
    -- Other-mod replacement wins; removed/retried mission objects are unwrapped.
    sync=reset();g=goal(1,true,10,10);freeze=true;run(sync)
    local replaced=function()return 77 end;g.update=replaced;freeze=false;run(sync);assert(g.update==replaced)
    sync=reset();g=goal(1,true,10,10);freeze=true;run(sync);goalSystem.activeGoals={};run(sync);assert(g.update==g.original)
    -- Online/split-screen and missing debug support must fail closed.
    for _,which in ipairs({'online','split','debug','session','splitSession','invalidNetwork'}) do
        sync=reset();g=goal(1,true,10,10);freeze=true
        local saved=debug
        if which=='online' then online=true elseif which=='split' then split=true elseif which=='debug' then debug=nil
        elseif which=='session' then gameStatus.onlineSession=true elseif which=='splitSession' then gameStatus.splitscreenSession=true
        else Network.isOnlineGame=function()return nil end end
        assert(not run(sync));assert(g.update==g.original);debug=saved
    end
    -- Joining an online/split mode restores only our own wrappers.
    sync=reset();g=goal(1,true,10,10);freeze=true;run(sync);online=true;run(sync);assert(g.update==g.original)
    instanceTests()
    print('World Lua timer: supported fail countdown freezes/resumes; generic waits, unsupported/foreign goals, native returns, HUD shared upvalue, OFF/retry/online guards passed. MOCK native engine.')
end
local function weatherTests()
    local sync=reset();local nativeSetter=getmetatable(sky).__newindex
    for n=1,5 do
        preset=n;pending=true
        local _,handled,canceled=run(sync);assert(handled and not canceled)
        assert(#moodCalls==2+(n-1)*4 and sky.requestSkyDome==nil)
        assert(moodSystem.moodStack.n==1)
        local oldCalls=#moodCalls;for j=1,30 do run(sync) end;assert(#moodCalls==oldCalls) -- no automatic repaint
    end
    preset=0;pending=true;run(sync);assert(skyWrites[#skyWrites]=='MissionSky')
    assert(getmetatable(sky).__newindex==nativeSetter)
    -- Native sky writes are not readable; watcher must detect them and preserve.
    sync=reset();preset=4;pending=true;run(sync);sky.requestSkyDome='NativeCutsceneSky'
    local _,_,canceled=run(sync);assert(canceled and skyWrites[#skyWrites]=='NativeCutsceneSky')
    preset=0;pending=true;run(sync);assert(skyWrites[#skyWrites]=='NativeCutsceneSky')
    -- A new mission even with the SAME mood table invalidates the layer.
    sync=reset();preset=4;pending=true;run(sync)
    localPlayer.getTaskObject=function()return {coreData={instance={}}} end
    _,_,canceled=run(sync);assert(canceled and skyWrites[#skyWrites]=='MissionSky')
    -- Native mood/chapter changes restore the NEW active mission sky, not old.
    sync=reset();preset=3;pending=true;run(sync);moodSystem.moodStack[1]={sky='NewMissionSky'}
    _,_,canceled=run(sync);assert(canceled and skyWrites[#skyWrites]=='NewMissionSky')
    -- Do not overwrite another mod's replacement of our setter watcher.
    sync=reset();preset=2;pending=true;run(sync)
    local other=function()end;getmetatable(sky).__newindex=other
    _,_,canceled=run(sync);assert(canceled and getmetatable(sky).__newindex==other)
    -- A fresh/replaced VM cannot replay an acknowledged request or falsely
    -- report an existing layer; this cancels the C++ Live switch instead.
    sync=reset();preset=3;pending=false
    _,_,canceled=run(sync);assert(canceled and #moodCalls==0 and #skyWrites==0)
    -- Unavailable/native-uninitialized preset is rejected, not falsely applied.
    sync=reset();moodSystem.moodGroups.Fog=nil;preset=2;pending=true
    local _,handled,bad=run(sync);assert(not handled and bad and #moodCalls==0)
    sync=reset();online=true;preset=3;pending=true;run(sync);assert(#moodCalls==0)
    -- Protected partial failure can remove both unique layers on the next sync.
    sync=reset();preset=3;pending=true
    local old=Mood.addMoodInCar;Mood.addMoodInCar=function()error('mock engine error') end
    assert(not pcall(sync));Mood.addMoodInCar=old;preset=0;pending=true;run(sync)
    assert(#skyWrites==0) -- failure happened before any dome write; no restore/reload needed
    -- Smooth mode must never request OR restore a sky asset, including OFF.
    sync=reset();smoothWeather=true
    for n=1,5 do
        preset=n;pending=true
        local _,handled,bad=run(sync);assert(handled and not bad and #skyWrites==0)
        assert(moodCalls[#moodCalls][3]==0 and moodCalls[#moodCalls-1][3]==0)
    end
    preset=0;pending=true;run(sync);assert(#skyWrites==0)
    -- Full mode skips redundant dome reloads (clear and fog share an asset).
    sync=reset();preset=1;pending=true;run(sync);assert(#skyWrites==1)
    preset=2;pending=true;run(sync);assert(#skyWrites==1)
    preset=3;pending=true;run(sync);assert(#skyWrites==2)
    print('World Lua weather: five native preset requests, write-only sky watcher/restore, unique keys, no repaint loop, mission/foreign-write cancellation, OFF and partial-failure cleanup passed. NOT visual acceptance.')
end
if mode=='timer' then timerTests() elseif mode=='weather' then weatherTests()
elseif mode=='street_error' then
    reset();freeze=true;preset=1;pending=true
    goal('street-isolation',true,10,20)
    local f=makeWorld(function()return freeze end,function()return true,true,true,false,false,false,false end,
        function()error('isolated street failure') end,nil,function(read)return read end)
    local found,handled,canceled,sh,sc,se=f()
    assert(found and handled and not canceled and not sh and sc and se:find('isolated street failure',1,true))
    assert(#moodCalls==2 and skyWrites[#skyWrites]=='ClearDay2')
    print('World Lua: population protected failure preserves mission-timer/weather work and reports separate error text; MOCK APIs.')
else error('unknown fixture mode') end
