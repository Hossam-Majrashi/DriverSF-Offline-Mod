-- Authored factory. Called only by World on its verified native script thread.
-- Population changes are explicitly WORLD-wide, offline free drive only.
local read, report = ...
local lastStatus
local function status(message)
    if message~=lastStatus then
        lastStatus=message
        if report then report('Street status: '..message) end
    end
end
local roads, police
local passenger,passengerApi
local signal,signalApi
local passengerStatus
local function passengerMessage(s)
    if s~=passengerStatus then passengerStatus=s;if report then report('Passenger status: '..s) end end
end
local nativeTraffic,nativeActive
local nativeChanged=false
local ownCancel=false
local lastCheck,lastFree
local function finite(x) return type(x)=='number' and x==x and x>=0 and x<=1 end
local function freeDrive()
    if type(localPlayer)~='table' or type(localPlayer.getTaskObject)~='function'
        or (localPlayer.inCutsceneOrIcam and not localPlayer.inZap)
        or (progressionSystem and progressionSystem.applyingChapterSettings)
        or (dareSystem and type(dareSystem.isDareActive)=='function' and dareSystem.isDareActive()) then return false end
    local ok,task=pcall(localPlayer.getTaskObject)
    return ok and (task==nil or task==false) -- native getTaskObject returns false in free drive
end
local function releasePassenger(audioSafe)
    local r=passenger;passenger=nil
    if not passengerApi then return end
    local all=vehicleManager and vehicleManager.vehiclesByGameVehicle
    if r and all==r.registry and vehicleManager==r.manager and all[r.handle]==r.agent
        and r.agent.gameVehicle==r.handle then
        passengerApi(r.handle,false,audioSafe==true) -- live agent/handle AND native per-seat ownership
    end
    passengerApi() -- discard metadata only; never dereference an old raw car
end
local function passengerStep(wanted)
    local agent=localPlayer and not localPlayer.inZap and localPlayer.currentVehicle
    local all=vehicleManager and vehicleManager.vehiclesByGameVehicle
    local handle=type(agent)=='table' and agent.gameVehicle
    local valid=false
    if wanted and type(all)=='table' and handle and all[handle]==agent
        and type(agent.getTaskObject)=='function' then
        local ok,task=pcall(agent.getTaskObject,agent)
        valid=ok and (task==nil or task==false)
    end
    if passenger and (not valid or passenger.agent~=agent or passenger.registry~=all
        or passenger.manager~=vehicleManager or passenger.handle~=handle) then releasePassenger(true) end
    if not valid then return end
    if not passengerApi then passengerMessage('unavailable: verified native per-seat backend missing');return end
    if not passenger then
        passenger={manager=vehicleManager,registry=all,agent=agent,handle=handle}
        if passengerApi(handle,true) then passengerMessage('player passenger seats emptied; driver retained; audio/visual acceptance pending')
        else releasePassenger(true);passengerMessage('unavailable or native ownership/player guard rejected change') end
    else
        passengerApi(handle,true) -- finish deferred LIAB refresh, never repaint characters
    end
end
local function releaseSignal()
    local r=signal;signal=nil
    if not signalApi then return end
    local all=vehicleManager and vehicleManager.vehiclesByGameVehicle
    if r and all==r.registry and vehicleManager==r.manager and all[r.handle]==r.agent
        and r.agent.gameVehicle==r.handle then signalApi(r.handle,false) end
    signalApi() -- discard metadata; never dereference an invalid prior vehicle
end
local function signalStep(wanted)
    local agent=localPlayer and not localPlayer.inZap and localPlayer.currentVehicle
    local all=vehicleManager and vehicleManager.vehiclesByGameVehicle
    local handle=type(agent)=='table' and agent.gameVehicle
    local valid=false
    if wanted and type(all)=='table' and handle and all[handle]==agent and type(agent.getTaskObject)=='function' then
        local ok,task=pcall(agent.getTaskObject,agent);valid=ok and (task==nil or task==false)
    end
    if signal and (not valid or signal.agent~=agent or signal.registry~=all
        or signal.manager~=vehicleManager or signal.handle~=handle) then releaseSignal() end
    if not valid then if signalApi then signalApi() end;return end
    if not signalApi then return end
    local fresh=not signal
    if fresh then signal={manager=vehicleManager,registry=all,agent=agent,handle=handle} end
    if not signalApi(handle,true,fresh) then releaseSignal() end
end
local function roadTable() return atlas and atlas.road end
local fields={'pingInProbabilityWith','pingInProbabilityAgainst'}
local function restoreRoads()
    local old=roads;if not old then return end
    -- Do not dereference an old userdata after its owning road/container changed.
    if roadTable()==old.owner then
        for _,r in ipairs(old.records) do
            if old.owner[r.index]==r.road then
                local params=r.road.trafficParameters
                if params then
                    local changed=false
                    for i,key in ipairs(fields) do
                        if params[key]==r.applied[i] and params[key]~=r.original[i] then params[key]=r.original[i];changed=true end
                    end
                    if changed then r.road.trafficParameters=params end -- verified native road setter
                end
            end
        end
    end
    roads=nil
end
local function policeRestore()
    local r=police;if not r then return end
    for _,f in ipairs(r.fields) do
        if r.api[f.setter]==f.watch then
            r.api[f.setter]=f.original
            if r.api==PatrollingVehicleManager and f.owned and r.api[f.getter]()==false then f.original(f.before) end
        end
    end
    police=nil
    if felony_suspiciousVehicleManager and type(felony_suspiciousVehicleManager.enableSuspiciousVehicles)=='function' then
        pcall(felony_suspiciousVehicleManager.enableSuspiciousVehicles, true)
    end
    if TrafficEventsManager and type(TrafficEventsManager.SetEnable)=='function' then
        pcall(TrafficEventsManager.SetEnable, true)
    end
    if progressionSystem and type(progressionSystem.setTrafficEvents)=='function' then
        pcall(progressionSystem.setTrafficEvents, true)
    end
end
local function restoreTraffic()
    if nativeActive and nativeTraffic then nativeTraffic(false) end
    nativeActive=false;restoreRoads()
end
local function cleanup() restoreTraffic();policeRestore() end
local function traffic(percent)
    if nativeActive then
        status('native traffic level requested='..percent..'/100; 0=stock civilian traffic OFF + ambient police block/retirement, 1=minimum gradual admission, 100=native; native lifecycle/policy protected; gameplay verification pending')
        return true
    end
    if percent==0 then return true,false end
    local all=roadTable()
    if type(all)~='table' then status('traffic unavailable: atlas.road is absent');return false,true end
    if not roads then
        local r={owner=all,records={}};local scanned=0
        for index,road in pairs(all) do
            scanned=scanned+1;if scanned>20000 then return false end
            if type(road)=='table' or type(road)=='userdata' then
                local params=road.trafficParameters
                if params and finite(params[fields[1]]) and finite(params[fields[2]]) then
                    r.records[#r.records+1]={index=index,road=road,
                        original={params[fields[1]],params[fields[2]]},applied={params[fields[1]],params[fields[2]]}}
                end
            end
        end
        if #r.records==0 then return false end
        roads=r -- publish BEFORE writes so protected partial failures can clean up
    end
    if roads.owner~=all then return false end
    for _,r in ipairs(roads.records) do
        if all[r.index]~=r.road then return false end
        local params=r.road.trafficParameters
        if not params then return false end
        for i,key in ipairs(fields) do
            if params[key]~=r.applied[i] then return false end -- native/other mod wins
        end
        local values={r.original[1]*percent/100,r.original[2]*percent/100}
        if values[1]~=r.applied[1] or values[2]~=r.applied[2] then
            for i,key in ipairs(fields) do r.applied[i]=values[i];params[key]=values[i] end
            r.road.trafficParameters=params -- verified native setter; no periodic repaint
        end
    end
    return true
end
local function disablePolice()
    local api=PatrollingVehicleManager
    if type(api)~='table' then return false end
    if not police then
        local r={api=api,fields={}}
        for _,names in ipairs({{'Enable','IsEnabled'},{'EnableSpawning','IsSpawningEnabled'}}) do
            if type(api[names[1]])~='function' or type(api[names[2]])~='function' then return false end
            local before=api[names[2]]();if type(before)~='boolean' then return false end
            r.fields[#r.fields+1]={setter=names[1],getter=names[2],original=api[names[1]],before=before,owned=true}
        end
        police=r
        for _,f in ipairs(r.fields) do
            f.watch=function(value,...)
                -- Our own StopAll cleanup can request patrol re-enablement.
                -- Keep this owned cancellation OFF, but retain that native
                -- intention for OFF restoration. All foreign writes win.
                if ownCancel and freeDrive() then
                    if type(value)=='boolean' then f.before=value end
                    return f.original(false,...)
                end
                f.owned=false;nativeChanged=true
                return f.original(value,...)
            end
            api[f.setter]=f.watch;f.original(false)
        end
    end
    if police.api~=api then return false end
    for _,f in ipairs(police.fields) do
        if api[f.setter]~=f.watch or not f.owned or api[f.getter]()~=false then return false end
    end
    if felony_suspiciousVehicleManager and type(felony_suspiciousVehicleManager.enableSuspiciousVehicles)=='function' then
        pcall(felony_suspiciousVehicleManager.enableSuspiciousVehicles, false)
    end
    if TrafficEventsManager and type(TrafficEventsManager.SetEnable)=='function' then
        pcall(TrafficEventsManager.SetEnable, false)
    end
    if progressionSystem and type(progressionSystem.setTrafficEvents)=='function' then
        pcall(progressionSystem.setTrafficEvents, false)
    end
    if CityLockManager and type(CityLockManager)=='table' then
        pcall(function() CityLockManager.CityLockActive=false end)
    end
    return true
end
local function pursuitReady()
    return type(Getaway)=='table' and type(Getaway.IsAGetawayActive)=='function' and type(Getaway.StopAll)=='function'
end
local function cancelPursuit()
    if not pursuitReady() then return false end
    if Getaway.IsAGetawayActive() then
        ownCancel=true;local ok,err=pcall(Getaway.StopAll);ownCancel=false
        if not ok then error(err) end
        return true,true
    end
    return true,false
end
local function zeroPolice()
    if not pursuitReady() then return false end
    if not disablePolice() then return false end
    local ok,stopped=cancelPursuit()
    -- StopAll can return chasers to ambient ownership. Drain the stock patrol
    -- fleet AFTER cancellation, not just the old patrol fleet before it.
    -- No periodic vehicle-deletion loop and no required/max/count tuning.
    if not ok or nativeChanged or not freeDrive() or not disablePolice() then return false end
    if stopped then
        for _,f in ipairs(police.fields) do
            if f.setter=='Enable' then f.original(false) end -- stock native fleet retirement
        end
    end
    local valid=not nativeChanged and freeDrive() and disablePolice()
    if stopped and valid and report then report('Street ZERO: ambient pursuit canceled; stock patrol-fleet retirement requested; visual acceptance pending') end
    return valid
end
return function(safe)
    local t,d0,d1,d2,d3,d4,d5,d6,p,pending,allowed,backend,alone,occupants,indicators,lamps=read()
    if type(backend)=='function' then nativeTraffic=backend end
    if type(occupants)=='function' then passengerApi=occupants end
    if type(lamps)=='function' then signalApi=lamps end
    local free=safe and freeDrive()
    if lastFree==false and free then lastCheck=0 end
    lastFree=free
    -- Default passenger policy is armed across missions/VM changes, but can
    -- touch ONLY the live player car in free drive. Independent of traffic/police.
    if allowed and free then passengerStep(alone==true);signalStep(indicators==true and not (gameStatus and gameStatus.simulationPaused))
    else releasePassenger(false);releaseSignal() end
    if not allowed then cleanup();status('disabled after errors');return pending,true end
    local active=t or p
    if not free or nativeChanged then
        if active then status(not safe and 'offline guard rejected request'
            or nativeChanged and 'native policy or ownership changed'
            or 'mission, cutscene or dare active') end
        cleanup();nativeChanged=false
        return pending,active -- mission/native policy takes precedence
    end
    if not active then cleanup();return pending,false end
    -- A fresh/replaced VM must not replay acknowledged settings/actions.
    if not pending and not roads and not nativeActive and not police and lastCheck==nil then return false,true end
    local percent=(d0 and 1 or 0)+(d1 and 2 or 0)+(d2 and 4 or 0)+(d3 and 8 or 0)+(d4 and 16 or 0)+(d5 and 32 or 0)+(d6 and 64 or 0)
    if percent<0 or percent>100 then cleanup();return pending,true end
    -- ZERO implies its own temporary police block, NOT a persistent edit of
    -- the separate UI switch. Probe without writing traffic first: unsupported
    -- or canceled ZERO must not irreversibly cancel a pursuit.
    local zero=t and percent==0 and nativeTraffic and nativeTraffic(true,true)==true
    if zero and not zeroPolice() then cleanup();status('ZERO rejected: police API/ownership or free-drive context changed');return pending,true end
    -- Drop ZERO-only patrol ownership immediately on 0 -> 1..100 / OFF,
    -- even inside the one-second legacy throttle. An explicit police block stays.
    if not zero and not p then policeRestore() end
    -- Renew the native lease on EVERY safe script step, not the one-second
    -- Lua road/police throttle. Leaving free drive cleans up before this point.
    if t and nativeTraffic then nativeActive=nativeTraffic(true)==true
    elseif nativeActive then restoreTraffic() end
    if zero and not nativeActive then
        if not p then policeRestore() end -- commit rejected; no leftover automatic police block
        restoreTraffic();status('traffic unavailable: ZERO commit rejected; independent police intent retained');return pending,false
    end
    local now=g_NetworkTime
    if not pending and type(now)=='number' and lastCheck and now>=lastCheck and now-lastCheck<1 then return false,false end
    lastCheck=type(now)=='number' and now or 0
    local ok=true
    if pending then lastStatus=nil end
    local unavailableTraffic=false
    if t and percent~=0 then
        local applied,unavailable=traffic(percent);unavailableTraffic=unavailable==true
        if unavailableTraffic then restoreRoads() else ok=applied and ok end
    elseif t and percent==0 then
        restoreRoads()
    else restoreTraffic() end
    if p and not zero then ok=disablePolice() and cancelPursuit() and ok
    elseif not zero then policeRestore() end
    if not ok then
        if not lastStatus then status('native API unavailable or ownership lost') end
        cleanup()
    elseif pending and not unavailableTraffic then status(percent==0 and t and 'request applied; ZERO uses stock civilian traffic OFF + ambient pursuit cancellation and patrol-fleet retirement; gameplay verification pending'
        or 'request applied; traffic admission changes gradually; existing admitted tracks retained until native retirement') end
    return pending,not ok
end
