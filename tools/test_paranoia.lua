-- Authored fixtures; optional locally extracted native prototypes are never distributed.
-- Host Lua 5.1, mocked engine APIs. This is NOT gameplay acceptance.
local root=assert(os.getenv('DSF_MOD_ROOT'))
local makeWorld=assert(loadfile(os.getenv('DSF_WORLD_CHUNK') or root..'/src/world_script.lua'))
local function pad(s,n,line) while #s<n-1 do s[#s+1]='' end;s[#s+1]=line end
local function fixture(source,first,last,body,prefix)
    local s={prefix or ''};pad(s,first,'return function(...)');s[#s+1]=body;pad(s,last,'end')
    return assert(loadstring(table.concat(s,'\n'),'@fixture\\'..source))
end
local function uv(fn,wanted,...)
    for i=1,64 do local name,value=debug.getupvalue(fn,i);if not name then break end
        if name==wanted then if select('#',...)>0 then debug.setupvalue(fn,i,...) end;return value,i end
    end
    error('missing upvalue '..wanted)
end
local mockGoal=fixture('goals\\goals.lua',1805,1826,
    'local elapsed=g_NetworkTime-startTime;if elapsed>=params.value then goalSystem.callbackHandler(UID) end;return elapsed',
    'local startTime,UID,params=...')
local mockMeter=fixture('tasks\\payloadtracking.lua',23,39,
    'local success,condition,data,points=...;points=points or 1;task.networkVars.payload=math.max(0,math.min(100,task.networkVars.payload+(success and points or -points)))',
    'local task=...')
local mockOwner=fixture('hud\\thesneakouthud.lua',139,167,
    'if task.specialName=="Mission feedback" then if showTimer then feedbackSystem.stepTimer(timer) end else feedbackSystem.updateDangerBar({value=task.networkVars.payload}) end',
    'local task,timer,showTimer=...')
local function mockHud()
    local s={'local startTime,setStartTime,timeRemaining=0,false,0;local timerUpdate'}
    pad(s,921,'timerUpdate=function(params)')
    s[#s+1]='if not setStartTime then startTime=g_NetworkTime;setStartTime=true end;timeRemaining=params.startTime and params.startTime-(g_NetworkTime-startTime) or g_NetworkTime-startTime;return timeRemaining'
    pad(s,1043,'end');pad(s,1046,'local function stepTimer(params)')
    pad(s,1101,'local function callback()');s[#s+1]='timerUpdate(params)';pad(s,1103,'end')
    s[#s+1]='userUpdateFunctions.timerUpdate={func=callback,stepRate=1,steps=0}'
    pad(s,1131,'end');s[#s+1]='return stepTimer,timerUpdate'
    return assert(loadstring(table.concat(s,'\n'),'@fixture\\feedbacksystemfunctions.lua'))()
end
local freeze,online,split,obj,instance,sync,goal,first,second,owner,params,update,step,callback,originalMeter,reports,callbacks,shown
local function noop()end
local function reset(options)
    options=options or {};freeze=false;online=false;split=false;g_NetworkTime=10;reports={};callbacks={};shown=nil
    Network={isOnlineGame=function()return online end,isSplitScreenMode=function()return split end}
    gameStatus={onlineSession=false,splitscreenSession=false}
    instance={challenge={name='TheSneakout'},networkVars={startTime=10}}
    obj={coreData={instance=instance},namedTasks={}}
    localPlayer={challenge={},getTaskObject=function()return obj end}
    taskSystem={getTaskBaseParameters=function(t)return t.base end}
    goalSystem={activeGoals={},taskSupport={registeredObjects={}},callbackHandler=function(uid)callbacks[uid]=(callbacks[uid] or 0)+1 end}
    userUpdateFunctions={};step,update=mockHud();feedbackSystem={stepTimer=step}
    params={startTime=120,slot=2};step(params);callback=userUpdateFunctions.timerUpdate.func
    if arg[1] then
        update=assert(loadfile(arg[2]))
        local values={pauseTimer=false,blockHUD=false,setStartTime=false,startTime=0,timeRemaining=0,countDownTime=120,
            showTimerPrompt=false,countdownPanelSet=true,format=string.format,fullTimeFormat='%s:%s:%s',timerPromptSetup={},
            timerIcon=0,timerSlots={mins={'m1','m2'},secs={'s1','s2'},milli={'x1','x2'},flash={'f1','f2'}},
            activePanels={{panelType='startTimerCountDown'},{panelType='startTimerCountDown'}},timerFlashTime=10,timerFlash=false,
            ceil=math.ceil,timerAudio=noop,prevMins=false,prevSecs=false,prevMilli=false}
        for name,value in pairs(values) do uv(update,name,value) end
        uv(step,'timerUpdate',update)
        -- Native callback copied for host execution has an independent upvalue
        -- cell. The authored fixture above also tests retail's SHARED-cell path.
        callback=assert(loadfile(arg[3]));uv(callback,'timerUpdate',update);uv(callback,'params',params)
        userUpdateFunctions.timerUpdate.func=callback
        feedbackSystem.menusMaster=setmetatable({}, {__index=function()return noop end})
        formatTime=function(t)return string.format('%02d',math.floor(t/60)),string.format('%02d',math.floor(t%60)),'00' end
        OneShotSound={PlayCountdown=noop,Play=noop};dareSystem={};promptTimes=nil
    end
    feedbackSystem.updateDangerBar=function(bar)shown=bar.value end
    Sound={SetRTPC=noop,SetState=noop}
    local function task(name,kind)
        local t={taskName=kind,specialName=name,instance=instance,taskObject=obj,networkVars={payload=40},
            agent={getTaskObject=function()return obj end}}
        obj.namedTasks[name]=t;goalSystem.taskSupport.registeredObjects[t]={};return t
    end
    local t=task('Second safehouse','No AI');t.instance.timerStartTime=10
    goal={name='Time trigger',goalKey=1,goalValue={value=123,startTimeValue=true}}
    local condition={__index={task=true,object=t,operandA=t,conditionKey=1,dynamicTargetID=-1},[1]=goal};setmetatable(goal,condition)
    t.base={task='No AI',taskConditions={{failCondition=true,{goal='Time trigger',params=goal.goalValue}}}}
    goalSystem.taskSupport.registeredObjects[t].taskCompletionConditions={condition}
    goal.update=arg[1] and assert(loadfile(arg[1]))(t,nil,1,goal.goalValue) or mockGoal(10,1,goal.goalValue)
    goal.original=goal.update;goalSystem.activeGoals[1]=goal
    owner=task('Mission feedback','No AI')
    local function ownerUpdate(t)
        if arg[5] then
            local fn=assert(loadfile(arg[5]));uv(fn,'task',t);uv(fn,'timer',params);uv(fn,'showTimer',false)
            uv(fn,'currentSuspicion',0);uv(fn,'prevSuspicion',0);uv(fn,'suspicionBar',{})
            return fn
        end
        return mockOwner(t,params,false)
    end
    owner.feedbackSystem={HUD={{update=ownerUpdate(owner)}}}
    for _,name in ipairs({'First suspicion','Second suspicion'}) do
        local t=task(name,'Payload Tracking');t.coreData={lower=0,upper=100}
        t.base={task='Payload Tracking',coreData=t.coreData,taskConditions={{failCondition=true,{goal='Payload over',params={value=100}}}}}
        t.goalCheckCallback=arg[4] and assert(loadfile(arg[4]))(t) or mockMeter(t)
        t.feedbackSystem={HUD={{update=ownerUpdate(t)}}}
    end
    first=obj.namedTasks['First suspicion'];second=obj.namedTasks['Second suspicion'];originalMeter=first.goalCheckCallback
    if options.noDeadline then goalSystem.activeGoals={} end
    sync=makeWorld(function()return freeze end,function()return false,false,false,false,false,false,false,true end,nil,
        function(s)reports[#reports+1]=s end)
    callback();assert(uv(update,'timeRemaining')==120)
end
local function frame(n)
    g_NetworkTime=n;sync();goal.update();callback()
    first.goalCheckCallback(true,1,false,9);first.goalCheckCallback(false,2,false,2)
    second.goalCheckCallback(true,1,false,3)
    first.feedbackSystem.HUD[1].update()
end
-- Enable AFTER both the timer and meter are running; freeze past native expiry.
reset();g_NetworkTime=12;callback();assert(uv(update,'timeRemaining')==118)
freeze=true;assert(sync());local vars=first.networkVars;local base=first.base;local registry=userUpdateFunctions.timerUpdate
for n=13,300 do
    frame(n);assert(math.abs(uv(update,'timeRemaining')-118)<1e-5 and callbacks[1]==nil)
    assert(first.networkVars==vars and vars.payload==40 and second.networkVars.payload==40 and shown==40)
    assert(first.base==base and userUpdateFunctions.timerUpdate==registry and registry.func==callback and registry.stepRate==1)
    assert(instance.networkVars.startTime==10 and g_NetworkTime==n)
end
assert(uv(owner.feedbackSystem.HUD[1].update,'timer')==params)
local log=table.concat(reports,'\n');assert(log:find('buffered HUD owners=1',1,true) and log:find('suspicion callbacks=2',1,true))
-- Native direct writes/reset are not repainted; OFF has no queued meter catch-up.
vars.payload=21;frame(301);assert(vars.payload==21 and shown==21)
freeze=false;g_NetworkTime=302;sync();callback();assert(uv(update,'timeRemaining')==118)
assert(first.goalCheckCallback==originalMeter);first.goalCheckCallback(true,1,false,3);assert(vars.payload==24)
g_NetworkTime=304;callback();assert(uv(update,'timeRemaining')==116)
assert(uv(step,'timerUpdate')==update and uv(callback,'timerUpdate')==update)
g_NetworkTime=422;goal.update();assert(callbacks[1]==nil);g_NetworkTime=423;goal.update();assert(callbacks[1]==1)
-- A bar-only phase still owns installation and releases cleanly without a deadline.
reset({noDeadline=true});freeze=true;assert(sync());first.goalCheckCallback(true,1,false,50);assert(first.networkVars.payload==40)
freeze=false;assert(not sync());assert(first.goalCheckCallback==originalMeter)
-- Never infer a 120-second alias from a buffered failure in another mission/HUD.
for _,change in ipairs({
    function()instance.challenge.name='Other mission' end,
    function()owner.feedbackSystem.HUD={} end,
    function()obj.namedTasks['Second safehouse']=nil end,
    function()owner.instance={} end,
    function()params.slot=1 end,
    function()goalSystem.taskSupport.registeredObjects[owner]=nil end,
    function()local other={startTime=120,slot=2};uv(callback,'params',other) end,
    function()local other={};uv(owner.feedbackSystem.HUD[1].update,'task',other) end,
}) do
    reset();change();freeze=true;sync();callback();g_NetworkTime=15;sync();callback()
    assert(uv(update,'timeRemaining')==115,'unowned 120-second HUD incorrectly frozen')
end
-- Unknown meters, missing live identity/registration/bounds/fail condition stay native.
for _,change in ipairs({
    function()instance.challenge.name='Other mission' end,
    function()obj.namedTasks['First suspicion']=nil end,
    function()first.taskObject={} end,
    function()first.instance={} end,
    function()first.base.taskConditions[1].failCondition=false end,
    function()first.base.coreData.upper=120 end,
    function()goalSystem.taskSupport.registeredObjects[first]=nil end,
    function()first.taskName='Payload Tracking With Multiplyer' end,
}) do
    reset();change();freeze=true;sync();first.goalCheckCallback(true,1,false,2);assert(first.networkVars.payload==42)
end
-- Context switches take effect even before the next housekeeping poll.
for _,change in ipairs({function()online=true end,function()split=true end,
    function()gameStatus.onlineSession=true end,function()gameStatus.splitscreenSession=true end,
    function()instance.challenge.name='Another mission' end,
    function()obj={coreData={instance={}},namedTasks={}} end}) do
    reset();freeze=true;sync();change();first.goalCheckCallback(true,1,false,2);assert(first.networkVars.payload==42)
    sync();assert(first.goalCheckCallback==originalMeter)
end
-- Stale goals/callbacks and newer native/foreign writes are never overwritten.
reset();freeze=true;sync();local foreign=function()return 'new' end
first.goalCheckCallback=foreign;sync();assert(first.goalCheckCallback==foreign)
uv(step,'timerUpdate',foreign);sync();assert(uv(step,'timerUpdate')==foreign)
freeze=false;sync();assert(uv(step,'timerUpdate')==foreign and first.goalCheckCallback==foreign)
reset();freeze=true;sync();local staleMeter=first.goalCheckCallback
first.goalCheckCallback=originalMeter;sync();sync();assert(first.goalCheckCallback==originalMeter)
staleMeter(true,1,false,2);assert(first.networkVars.payload==42)
freeze=false;sync();freeze=true;sync();first.goalCheckCallback(true,1,false,2);assert(first.networkVars.payload==42)
reset();freeze=true;sync();local old=first;local stale=old.goalCheckCallback;local replacement={}
for k,v in pairs(old) do replacement[k]=v end
replacement.networkVars={payload=17}
replacement.goalCheckCallback=arg[4] and assert(loadfile(arg[4]))(replacement) or mockMeter(replacement)
obj.namedTasks['First suspicion']=replacement;goalSystem.taskSupport.registeredObjects[replacement]={};sync()
stale(true,1,false,2);assert(old.networkVars.payload==42 and replacement.networkVars.payload==17 and old.goalCheckCallback==originalMeter)
replacement.goalCheckCallback(true,1,false,2);assert(replacement.networkVars.payload==17)
reset();freeze=true;sync();frame(20)
uv(update,'startTime',19);frame(21);assert(uv(update,'timeRemaining')==118,'native HUD clock reset overridden')
uv(update,'setStartTime',false);frame(22);assert(uv(update,'timeRemaining')==120)
-- Goal retirement removes the BUFFERED mapping; 120 is not a global whitelist.
goalSystem.activeGoals={};sync();g_NetworkTime=24;callback();assert(uv(update,'timeRemaining')==118)
print('Paranoia: 123s failure + live 120s slot-2 HUD; active/shared/captured callbacks; actual suspicion payload + bar; OFF/no catch-up, resets, lifecycle/foreign/online guards passed. '..(arg[1] and 'PRIVATE native Lua prototypes + MOCK engine.' or 'Authored MOCK fixtures.'))
