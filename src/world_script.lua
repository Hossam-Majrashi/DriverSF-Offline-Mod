-- Installed only after a naturally scheduled, successful native Lua protected call.
-- No disk script changes, global time writes, simulation pause or native offsets.
local frozen, weatherCommand, readStreets, report, streetFactory = ...
local streetSync=readStreets and (createStreets or streetFactory)(readStreets,report)
local lastTimerStatus
local function timerStatus(message)
    if message~=lastTimerStatus then
        lastTimerStatus=message
        if report then report('World timer status: '..message) end
    end
end
local timerRecords = setmetatable({}, {__mode = 'k'})
local hudRecord, weatherState
local supportedDurations={}
local supportedHudParams=setmetatable({}, {__mode='k'})
local meterRecords=setmetatable({}, {__mode='k'})
local meterBlocked=setmetatable({}, {__mode='k'})
local hudBlocked,lastHudStatus,lastMeterStatus
local moodNames = {'ClearDay', 'Fog', 'Challenge_TheDriver', 'JericohFull', 'Frozen'}
local moodKey, carKey = 'DriverSF.mod.weather', 'DriverSF.mod.weather.IC'
local type, pairs, pcall = type, pairs, pcall
local function finite(n) return type(n)=='number' and n==n and n>-1e12 and n<1e12 end
local function clock() return finite(g_NetworkTime) and g_NetworkTime or nil end
local function offline()
    return type(gameStatus)=='table' and not gameStatus.onlineSession and not gameStatus.splitscreenSession
        and type(Network)=='table' and type(Network.isOnlineGame)=='function'
        and Network.isOnlineGame()==false
        and (type(Network.isSplitScreenMode)~='function' or Network.isSplitScreenMode()==false)
end
local function nativeFunction(fn, suffix, first, last)
    if type(fn)~='function' or type(debug)~='table' or type(debug.getinfo)~='function'
        or type(debug.getupvalue)~='function' or type(debug.setupvalue)~='function' then return false end
    local d=debug.getinfo(fn,'S')
    return d and d.what=='Lua' and d.linedefined==first and d.lastlinedefined==last
        and type(d.source)=='string' and d.source:lower():sub(-#suffix)==suffix
end
local function upvalue(fn, wanted)
    for i=1,64 do local name,value=debug.getupvalue(fn,i)
        if not name then break end
        if name==wanted then return i,value end
    end
end
local function supported(goal)
    if type(goal)~='table' or (goal.name~='Time trigger' and goal.name~='Instance time above') or type(goal.operandA)~='table'
        or type(goal.goalValue)~='table' or not finite(goal.goalValue.value)
        or goal.goalValue.value<=0 or type(taskSystem)~='table'
        or type(taskSystem.getTaskBaseParameters)~='function' then return false,'not a supported deadline with a positive duration/task API' end
    -- Native goalSystem.taskSupport stores ownership in the goal's condition
    -- metatable, and failCondition on an indexed base condition (NOT base).
    -- Match the live registered condition before selecting task/goal conditions;
    -- never infer failure from another condition on the same task.
    local condition=getmetatable(goal)
    local meta=type(condition)=='table' and rawget(condition,'__index')
    local support=type(goalSystem)=='table' and goalSystem.taskSupport
    local registry=type(support)=='table' and support.registeredObjects
    local registration=type(registry)=='table' and registry[goal.operandA]
    if type(meta)~='table' or meta.task~=true or meta.object~=goal.operandA
        or meta.operandA~=goal.operandA or type(registration)~='table' then return false,'missing native task condition ownership' end
    local key,target,goalKey=meta.conditionKey,meta.dynamicTargetID,goal.goalKey
    if not finite(key) or key<1 or key%1~=0 or not finite(target) or target%1~=0
        or not finite(goalKey) or goalKey<1 or goalKey%1~=0 or condition[goalKey]~=goal then return false,'invalid native condition/goal index' end
    local owned
    if target==-1 then owned=registration.taskCompletionConditions
    elseif target==0 then owned=registration.noTargetConditions
    elseif target>0 and type(registration.dynamicConditions)=='table' then owned=registration.dynamicConditions[target]
    end
    if type(owned)~='table' or owned[key]~=condition then return false,'registered condition mismatch' end
    local ok,base=pcall(taskSystem.getTaskBaseParameters,goal.operandA)
    local definitions
    if ok and type(base)=='table' then
        if target==-1 then definitions=base.taskConditions else definitions=base.goalConditions end
    end
    local definition=type(definitions)=='table' and definitions[key]
    local entry=type(definition)=='table' and definition[goalKey]
    -- Never freeze generic waits, successful timed objectives or MP timers.
    if type(definition)~='table' or definition.failCondition~=true then
        return false,'base condition is not failure (flag='..tostring(type(definition)=='table' and definition.failCondition)..')'
    end
    return type(entry)=='table' and entry.goal==goal.name,'failure condition; validating native closure' 
end
local function settle(record, now, enabled)
    if record.instanceClock then
        local name,operand=record.getUpvalue(record.original,record.operandIndex)
        local instance=record.operand.instance
        local vars=type(instance)=='table' and instance.networkVars
        local stamp=type(vars)=='table' and vars.startTime
        if name~='operandA' or operand~=record.proxy or not finite(stamp) then return false end
        if instance~=record.instance or vars~=record.vars or stamp~=record.stamp then
            -- A restart/new phase gets its own clock. Never carry a previous
            -- phase's compensation into a newly initialized native deadline.
            record.offset=0;record.last=now
        end
        record.instance,record.vars,record.stamp=instance,vars,stamp
        if now and record.last and now>=record.last and record.wasFrozen then
            record.offset=record.offset+(now-record.last)
        end
        record.last,record.wasFrozen=now,enabled
        return true
    end
    if now and record.last and now>=record.last and record.wasFrozen then
        local name,value=record.getUpvalue(record.original,record.startIndex)
        if name=='startTime' and finite(value) then
            local canShift=true
            if record.readyIndex then
                local readyName,ready=record.getUpvalue(record.original,record.readyIndex)
                canShift=readyName=='setStartTime' and ready==true
            end
            if canShift then record.setUpvalue(record.original,record.startIndex,value+(now-record.last)) end
        end
    end
    record.last,record.wasFrozen=now,enabled
end
local function detachTimer(goal,r)
    if goal.update==r.wrapper then
        settle(r,clock(),false);goal.update=r.original
    end
    if r.instanceClock then
        local name,value=r.getUpvalue(r.original,r.operandIndex)
        if name=='operandA' and value==r.proxy then r.setUpvalue(r.original,r.operandIndex,r.operand) end
    end
    timerRecords[goal]=nil
end
local function instanceTimer(goal)
    if not nativeFunction(goal.update,'\\goals\\goals.lua',4598,4614) then return end
    local index,operand=upvalue(goal.update,'operandA')
    local _,params=upvalue(goal.update,'params')
    local instance=type(operand)=='table' and operand.instance
    local vars=type(instance)=='table' and instance.networkVars
    if not index or operand~=goal.operandA or params~=goal.goalValue
        or type(vars)~='table' or not finite(vars.startTime) then return end
    local r={instanceClock=true,original=goal.update,operandIndex=index,operand=operand,
        instance=instance,vars=vars,stamp=vars.startTime,offset=0,last=clock(),wasFrozen=true,
        getUpvalue=debug.getupvalue,setUpvalue=debug.setupvalue}
    -- This verified closure reads operandA only for instance.networkVars.startTime.
    -- Give ONLY that closure a private read-only clock view. The real task,
    -- networkVars, global clock and every other mission goal remain untouched.
    local clockVars=setmetatable({}, {__index=function(_,key)
        if key=='startTime' then return r.stamp+r.offset end
        return r.vars[key]
    end})
    local clockInstance=setmetatable({networkVars=clockVars},{__index=function(_,key) return r.instance[key] end})
    r.proxy=setmetatable({instance=clockInstance},{__index=operand})
    r.setUpvalue(r.original,index,r.proxy)
    return r
end
-- Paranoia's displayed timer (120) differs from its failure deadline (123).
-- Bind the ACTUAL live HUD params object, never a global 120 -> 123 heuristic.
local function liveSneakout()
    if type(localPlayer)~='table' or type(localPlayer.getTaskObject)~='function' then return end
    local ok,obj=pcall(localPlayer.getTaskObject)
    local core=ok and type(obj)=='table' and obj.coreData
    local instance=type(core)=='table' and core.instance
    local challenge=type(instance)=='table' and instance.challenge
    if type(challenge)=='table' and challenge.name=='TheSneakout' and type(obj.namedTasks)=='table' then return obj,instance end
end
local function registered(task)
    local support=type(goalSystem)=='table' and goalSystem.taskSupport
    local registry=type(support)=='table' and support.registeredObjects
    return type(registry)=='table' and type(registry[task])=='table'
end
local function bufferedHud(goal)
    if goal.name~='Time trigger' or goal.goalValue.value~=123 then return end
    local obj,instance=liveSneakout();local task=goal.operandA
    if not obj or task.instance~=instance or task.taskObject~=obj or obj.namedTasks['Second safehouse']~=task then return end
    local owner=obj.namedTasks['Mission feedback']
    if type(owner)~='table' or owner.instance~=instance or owner.taskObject~=obj or not registered(owner) then return end
    local feedback=owner.feedbackSystem
    local hud=type(feedback)=='table' and feedback.HUD
    if type(hud)~='table' then return end
    for i=1,8 do
        local entry=hud[i]
        if type(entry)=='table' and nativeFunction(entry.update,'\\hud\\thesneakouthud.lua',139,167) then
            local _,actualTask=upvalue(entry.update,'task');local _,params=upvalue(entry.update,'timer')
            if actualTask==owner and type(params)=='table' and params.startTime==120 and params.slot==2 then return params end
        end
    end
end
local function suspicionTask(task)
    local obj,instance=liveSneakout()
    if not obj or type(task)~='table' or task.instance~=instance or task.taskObject~=obj or not registered(task)
        or (task.specialName~='First suspicion' and task.specialName~='Second suspicion')
        or obj.namedTasks[task.specialName]~=task or task.taskName~='Payload Tracking'
        or type(task.networkVars)~='table' or not finite(task.networkVars.payload)
        or task.networkVars.payload<0 or task.networkVars.payload>100
        or type(taskSystem)~='table' or type(taskSystem.getTaskBaseParameters)~='function' then return false end
    local ok,base=pcall(taskSystem.getTaskBaseParameters,task)
    if not ok or type(base)~='table' or base.task~='Payload Tracking'
        or type(base.coreData)~='table' or base.coreData.lower~=0 or base.coreData.upper~=100
        or type(base.taskConditions)~='table' then return false end
    for _,condition in pairs(base.taskConditions) do
        if type(condition)=='table' and condition.failCondition==true then
            for _,entry in ipairs(condition) do
                if type(entry)=='table' and entry.goal=='Payload over' and type(entry.params)=='table' and entry.params.value==100 then return true end
            end
        end
    end
    return false
end
local function meterSync(enable)
    if not enable then meterBlocked=setmetatable({}, {__mode='k'}) end
    local wanted={};local count=0
    local obj=enable and liveSneakout()
    if obj then
        for _,name in ipairs({'First suspicion','Second suspicion'}) do
            local task=obj.namedTasks[name]
            if suspicionTask(task) then
                local r=meterRecords[task]
                if r and task.goalCheckCallback~=r.wrapper then meterBlocked[task]=true;meterRecords[task]=nil;r=nil end
                if not r and not meterBlocked[task] and nativeFunction(task.goalCheckCallback,'\\tasks\\payloadtracking.lua',23,39) then
                    local _,owner=upvalue(task.goalCheckCallback,'task')
                    if owner==task then
                        r={original=task.goalCheckCallback}
                        r.wrapper=function(success,condition,data,points,...)
                            if task.goalCheckCallback==r.wrapper and offline() and frozen() and suspicionTask(task) then
                                -- Keep native goal progression/autoRefresh/feedback. Only this
                                -- verified callback's payload delta is zero; no replay or repaint.
                                return r.original(success,condition,data,0,...)
                            end
                            return r.original(success,condition,data,points,...)
                        end
                        meterRecords[task]=r;task.goalCheckCallback=r.wrapper
                    end
                end
                if r then wanted[task]=true;count=count+1 end
            end
        end
    end
    for task,r in pairs(meterRecords) do
        if not wanted[task] then
            if task.goalCheckCallback==r.wrapper then task.goalCheckCallback=r.original end
            meterRecords[task]=nil
        end
    end
    local message='Paranoia suspicion callbacks='..count..'; actual payload deltas held; no health/global-clock freeze'
    if message~=lastMeterStatus and (count>0 or lastMeterStatus) then
        lastMeterStatus=message;if report then report('World meter status: '..message) end
    end
    return count
end
local function timerScan(enable)
    local goals=type(goalSystem)=='table' and goalSystem.activeGoals or nil
    local present={};local count=0;supportedDurations={};supportedHudParams=setmetatable({}, {__mode='k'})
    local details={};local scanned=0
    if type(goals)=='table' then
        for _,goal in pairs(goals) do
            scanned=scanned+1
            local accepted,reason=supported(goal)
            if enable and report and #details<8 and type(goal)=='table' then
                local info=type(debug)=='table' and type(debug.getinfo)=='function' and type(goal.update)=='function'
                    and debug.getinfo(goal.update,'S')
                details[#details+1]=tostring(goal.name)..' value='..tostring(type(goal.goalValue)=='table' and goal.goalValue.value)
                    ..' task='..tostring(type(goal.operandA)=='table' and goal.operandA.taskName)
                    ..' accepted='..tostring(accepted)..' reason='..tostring(reason)
                    ..' lines='..tostring(info and info.linedefined)..':'..tostring(info and info.lastlinedefined)
            end
            if accepted then
                local r=timerRecords[goal]
                if r and goal.update~=r.wrapper then detachTimer(goal,r);r=nil end
                if enable and not r and nativeFunction(goal.update,'\\goals\\goals.lua',1805,1826) then
                    local index,value=upvalue(goal.update,'startTime')
                    if index and finite(value) then
                        r={original=goal.update,startIndex=index,last=clock(),wasFrozen=true,
                            getUpvalue=debug.getupvalue,setUpvalue=debug.setupvalue}
                    end
                end
                if enable and not r and goal.name=='Instance time above' then r=instanceTimer(goal) end
                if r and not r.wrapper then
                    r.wrapper=function(...)
                        settle(r,clock(),offline() and frozen() and supported(goal))
                        return r.original(...)
                    end
                    timerRecords[goal]=r;goal.update=r.wrapper
                end
                local keep=r~=nil
                if r and r.instanceClock then
                    keep=settle(r,clock(),enable) and (enable or (offline() and r.offset>0))
                elseif not enable then keep=false end
                if keep then
                    present[goal]=true;supportedDurations[goal.goalValue.value]=true
                    local params=bufferedHud(goal);if params then supportedHudParams[params]=goal end
                end
            end
        end
    end
    for goal,r in pairs(timerRecords) do
        if not present[goal] then detachTimer(goal,r) else count=count+1 end
    end
    if enable then
        table.sort(details)
        timerStatus('active='..scanned..' wrapped='..count..' ['..table.concat(details,'; ')..']')
    elseif lastTimerStatus then timerStatus('OFF') end
    -- Instance deadlines retain a non-frozen compensator after OFF until the
    -- goal ends/restarts; otherwise disabling freeze would instantly catch up.
    return count
end
local function hudMatches(params)
    if type(params)~='table' or params.countUp==true then return false end
    if supportedDurations[params.startTime]==true then return true end
    local goal=supportedHudParams[params]
    return goal~=nil and supported(goal) and bufferedHud(goal)==params
end
local function hudSync(enable)
    local step=type(feedbackSystem)=='table' and feedbackSystem.stepTimer or nil
    if not enable then hudBlocked=nil end
    local lost=hudRecord and select(2,hudRecord.getUpvalue(hudRecord.step,hudRecord.ownerIndex))~=hudRecord.wrapper
    if hudRecord and (hudRecord.step~=step or not enable or lost) then
        local r=hudRecord
        local _,value=r.getUpvalue(r.step,r.ownerIndex)
        if value==r.wrapper then r.advance(clock(),false) end
        -- Lua 5.1 native callbacks usually SHARE this upvalue cell with stepTimer.
        -- Restore only cells still ours; never replace func/stepRate/params or a
        -- newer callback. Independent captured cells are handled as well.
        for callback,index in pairs(r.callbacks) do
            local _,current=r.getUpvalue(callback,index)
            if current==r.wrapper then r.setUpvalue(callback,index,r.original) end
        end
        local _,current=r.getUpvalue(r.step,r.ownerIndex)
        if current==r.wrapper then r.setUpvalue(r.step,r.ownerIndex,r.original) end
        if lost and enable then hudBlocked=step end
        r.retired=true;hudRecord=nil
    end
    if enable and not hudRecord and hudBlocked~=step and nativeFunction(step,'\\feedbacksystemfunctions.lua',1046,1131) then
        local ownerIndex,update=upvalue(step,'timerUpdate')
        if ownerIndex and nativeFunction(update,'\\feedbacksystemfunctions.lua',921,1043) then
            local startIndex,start=upvalue(update,'startTime')
            local readyIndex=upvalue(update,'setStartTime')
            if startIndex and readyIndex and finite(start) then
                local r={step=step,ownerIndex=ownerIndex,original=update,startIndex=startIndex,
                    readyIndex=readyIndex,last=clock(),wasFrozen=false,expectedStart=start,
                    callbacks=setmetatable({}, {__mode='k'}),
                    getUpvalue=debug.getupvalue,setUpvalue=debug.setupvalue}
                r.advance=function(now,active)
                    local _,current=r.getUpvalue(r.original,r.startIndex)
                    -- A native reset/retry or external clock write takes precedence.
                    if current~=r.expectedStart then r.wasFrozen=false;r.last=now end
                    settle(r,now,active)
                    _,r.expectedStart=r.getUpvalue(r.original,r.startIndex)
                end
                r.finish=function(...)
                    local _,current=r.getUpvalue(r.original,r.startIndex);r.expectedStart=current
                    return ...
                end
                r.wrapper=function(params,...)
                    local _,current=r.getUpvalue(r.step,r.ownerIndex)
                    if r.retired or current~=r.wrapper then
                        r.wasFrozen=false;r.last=clock();return r.original(params,...)
                    end
                    local same=r.params==params or (type(params)=='table' and type(r.params)=='table'
                        and supportedDurations[params.startTime]==true and params.startTime==r.params.startTime
                        and params.slot==r.params.slot and params.countUp==r.params.countUp)
                    if not same then r.wasFrozen=false;r.last=clock() end
                    r.params=params
                    r.advance(clock(),hudMatches(params) and offline() and frozen())
                    return r.finish(r.original(params,...))
                end
                hudRecord=r;debug.setupvalue(step,ownerIndex,r.wrapper)
            end
        end
    end
    local r=hudRecord
    if r then
        local entry=type(userUpdateFunctions)=='table' and userUpdateFunctions.timerUpdate
        local callback=type(entry)=='table' and entry.func
        if nativeFunction(callback,'\\feedbacksystemfunctions.lua',1101,1103) then
            local index,current=upvalue(callback,'timerUpdate');local _,params=upvalue(callback,'params')
            if index and (current==r.original or current==r.wrapper) then
                r.callbacks[callback]=index
                if current==r.original then r.setUpvalue(callback,index,r.wrapper) end
                if not r.params then r.params=params;r.wasFrozen=hudMatches(params);r.last=clock() end
            end
        end
        local buffered=0;for _ in pairs(supportedHudParams) do buffered=buffered+1 end
        local message='bound; buffered HUD owners='..buffered..'; running native callback='..tostring(r.callbacks[callback]~=nil)
        if message~=lastHudStatus then lastHudStatus=message;if report then report('World HUD timer status: '..message) end end
    elseif lastHudStatus and lastHudStatus~='OFF' then
        lastHudStatus='OFF';if report then report('World HUD timer status: OFF (or native ownership changed)') end
    end
end
local function context()
    local m=type(moodSystem)=='table' and moodSystem or nil
    local stack=m and m.moodStack
    local mission
    if type(localPlayer)=='table' and type(localPlayer.getTaskObject)=='function' then
        local ok,task=pcall(localPlayer.getTaskObject)
        if ok and type(task)=='table' and type(task.coreData)=='table' then mission=task.coreData.instance end
    end
    return m, type(stack)=='table' and stack[stack.n] or nil,
        type(progressionSystem)=='table' and progressionSystem.currentChapter or nil,mission
end
local function nativeSky()
    local m=type(moodSystem)=='table' and moodSystem
    local stack=m and m.moodStack
    if type(stack)=='table' and finite(stack.n) and stack.n>=0 and stack.n<=1024 then
        for i=stack.n,1,-1 do
            local group=stack[i]
            if type(group)=='table' and type(group.sky)=='string' then return group.sky end
        end
    end
    local config=type(configSelector)=='table' and configSelector.launchConfig
    return type(config)=='table' and type(config.InitialSky)=='string' and config.InitialSky or nil
end
local function removeWeather(restoreSky,blend)
    local old=weatherState;if not old then return end
    -- Our unique keys only: never remove a mission's own mood or moodStack entry.
    if type(Mood)=='table' and type(Mood.removeMood)=='function' then
        Mood.removeMood(moodKey,blend or 0.5,nil,0);Mood.removeMood(carKey,blend or 0.5,nil,0)
    end
    -- requestSkyDome is a WRITE-ONLY native property, not a readable state.
    -- Observe its native __newindex while our layer is active; external writes
    -- win, and cleanup must never restore over them or another mod's watcher.
    local own=old.meta and old.meta.__newindex==old.watcher
    if own then old.meta.__newindex=old.originalNewIndex end
    if restoreSky and old.changedSky and own and not old.foreignSky and sky==old.sky then
        local m,top,chapter,mission=context()
        local original=(m==old.system and top==old.top and chapter==old.chapter and mission==old.mission)
            and old.previousSky or nativeSky()
        if original then sky.requestSkyDome=original end
    end
    weatherState=nil
end
local function applyWeather(index,smooth)
    local m,top,chapter,mission=context()
    local groups=m and m.moodGroups;local group=type(groups)=='table' and groups[moodNames[index]]
    if type(group)~='table' or not group.main or not group.incar
        or type(Mood)~='table' or type(Mood.addMoodUserDefined)~='function'
        or type(Mood.addMoodInCar)~='function' or type(Mood.removeMood)~='function'
        or type(sky)~='table' or type(localPlayerManager)~='table'
        or type(localPlayerManager.players)~='table' or not localPlayerManager.players[0] then return false end
    local meta=getmetatable(sky)
    local old=weatherState
    local previous=old and old.previousSky or nativeSky()
    if type(meta)~='table' or type(meta.__newindex)~='function' or not previous then return false end
    local currentSky=old and old.currentSky or previous
    local changedSky=old and old.changedSky or false
    -- Smooth mode keeps the resident sky asset: no synchronous dome reload.
    -- It changes native lighting/fog only; full dome swaps remain opt-in.
    removeWeather(false,smooth and 0 or 0.5)
    -- Record before calls so a protected failure can clean up a partial request.
    local r={system=m,top=top,chapter=chapter,mission=mission,previousSky=previous,
        sky=sky,meta=meta,originalNewIndex=meta.__newindex,currentSky=currentSky,changedSky=changedSky}
    r.watcher=function(t,key,value)
        if key=='requestSkyDome' and not r.ownSky then r.foreignSky=true end
        return r.originalNewIndex(t,key,value)
    end
    weatherState=r;meta.__newindex=r.watcher
    local blend=smooth and 0 or 0.5
    Mood.addMoodUserDefined(group.main,moodKey,1,blend,nil,0,false)
    Mood.addMoodInCar(group.incar,carKey,1,blend,-1,0,false)
    if not smooth and group.sky and group.sky~=currentSky then
        r.ownSky=true;sky.requestSkyDome=group.sky;r.ownSky=false
        r.currentSky=group.sky;r.changedSky=true
    end
    return true
end
return function()
    local safe=offline()
    local enable=safe and frozen()
    local count=timerScan(enable);local meters=meterSync(enable);hudSync(enable and count>0)
    local on,pending,a,b,c,d,e,smooth=weatherCommand()
    local handled,canceled,failed=false,false,false
    local m,top,chapter,mission=context()
    if weatherState and (not safe or m~=weatherState.system or top~=weatherState.top or chapter~=weatherState.chapter
        or mission~=weatherState.mission or weatherState.foreignSky or sky~=weatherState.sky
        or weatherState.meta.__newindex~=weatherState.watcher) then
        -- Native mission/chapter mood changes win; never repeatedly repaint them.
        removeWeather(safe);canceled=true
    end
    -- A fresh/replaced gameplay VM has no owned layer: do not leave the UI
    -- claiming Live weather is applied, and do not replay a completed request.
    if on and (not safe or (not pending and not weatherState)) then canceled=true end
    if pending then
        if not on then removeWeather(safe);handled=true
        elseif safe and not canceled then
            local index=a and 1 or b and 2 or c and 3 or d and 4 or e and 5
            if index then handled=applyWeather(index,smooth);failed=not handled else failed=true end
        end
    end
    local streetHandled,streetCanceled,streetError=false,false,nil
    if streetSync then
        local ok,a,b=pcall(streetSync,safe)
        if ok then streetHandled,streetCanceled=a,b
        else streetCanceled=true;streetError=tostring(a) end
    end
    return count+meters>0,handled,canceled or failed,streetHandled,streetCanceled,streetError
end
