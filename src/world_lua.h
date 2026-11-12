// Included inside VehicleMod::NitroLua, sharing ONLY its verified Lua API.
// All Lua/game calls happen after a naturally scheduled outermost protected call.
namespace World {
static std::atomic<bool> bound{false},freeze{false},live{false},installed{false},supportedTimer{false},failed{false};
static std::atomic<uint32_t> command{0},completed{0};
static std::atomic<DWORD> deadline{0};
static std::atomic<unsigned long> syncs{0},errors{0};
static std::atomic<bool> traffic{false},policeOff{false},passengers{true},streetFailed{false};
static std::atomic<bool> trafficUnavailable{false};
static std::atomic<int> density{100};
static std::atomic<uint32_t> streetCommand{0},streetCompleted{0};
static std::atomic<DWORD> streetDeadline{0};
static std::atomic<unsigned> streetErrors{0};
static thread_local uint32_t streetToken=0;
static char pendingSpawnModel[64]="";
static std::atomic<int> pendingSpawnModelID{0};
static std::atomic<bool> spawnRequested{false};
static std::atomic<bool> unlockGarageRequested{false};
static std::atomic<bool> completeGame100Requested{false};
static char pendingMissionName[64]="";
static char lastLaunchedMission[64]="";
static std::atomic<bool> launchMissionRequested{false};
static std::atomic<bool> teleportOnlyRequested{false};
static std::atomic<bool> lastLaunchWasTeleportOnly{false};
static void streetRequest() {
    streetDeadline=GetTickCount()+2000;++streetCommand;
}
static void queueLaunchMission(const char* mission, bool teleportOnly = false) {
    if(!mission || !mission[0]) return;
    strncpy(pendingMissionName, mission, sizeof(pendingMissionName)-1);
    pendingMissionName[sizeof(pendingMissionName)-1]=0;
    strncpy(lastLaunchedMission, mission, sizeof(lastLaunchedMission)-1);
    lastLaunchedMission[sizeof(lastLaunchedMission)-1]=0;
    teleportOnlyRequested.store(teleportOnly);
    lastLaunchWasTeleportOnly.store(teleportOnly);
    launchMissionRequested.store(true);
    streetRequest();
    logLine("Mission launch requested");
}
static void queueSpawnVehicle(int modelID, const char *model=nullptr) {
    if(modelID<=0)return;
    pendingSpawnModelID.store(modelID);
    if(model) {
        strncpy(pendingSpawnModel,model,sizeof(pendingSpawnModel)-1);
        pendingSpawnModel[sizeof(pendingSpawnModel)-1]=0;
    } else pendingSpawnModel[0]=0;
    spawnRequested.store(true);
    streetRequest();
    logLine("Vehicle spawn requested");
}
static void queueUnlockGarage() {
    unlockGarageRequested.store(true);
    streetRequest();
    logLine("Garage unlock requested");
}
static void queueCompleteGame100() {
    completeGame100Requested.store(true);
    streetRequest();
    logLine("Complete game 100% requested");
}
static void stopStreets(){traffic=policeOff=false;NativeTraffic::set(false);streetRequest();}
static bool streetsNeeded(){return !streetFailed && (traffic || policeOff || passengers || NativePassengers::active || NativeSignals::enabled || NativeSignals::active || streetCommand.load()!=streetCompleted.load());}
static int __cdecl diagnostic(State*L) {
    if(!api.toString || api.getTop(L)<1 || api.type(L,1)!=4)return 0;
    size_t n=0;const char*s=api.toString(L,1,&n);
    if(s){
        if(strncmp(s,"Street status: traffic unavailable:",34)==0)trafficUnavailable=true;
        char b[600];snprintf(b,sizeof(b),"%.*s",int(n<580?n:580),s);logLine(b);
    }
    return 0;
}
static int __cdecl nativeTraffic(State*L) {
    bool allow=api.getTop(L)>=1 && api.type(L,1)==1 && api.toBool(L,1) && traffic && !streetFailed && !failed;
    bool probe=api.getTop(L)>=2 && api.type(L,2)==1 && api.toBool(L,2);
    if(probe){
        if(allow && density==0 && !NativeTraffic::canZero() && traffic)streetRequest();
        api.pushBool(L,allow && density==0 && NativeTraffic::canZero());return 1;
    }
    bool accepted=NativeTraffic::set(allow,density.load());
    if(allow && !accepted && NativeTraffic::ready && density==0){
        streetRequest();
        accepted=NativeTraffic::set(allow,density.load());
        if(!accepted)traffic=false;
    }
    api.pushBool(L,accepted);return 1;
}
static int __cdecl passengerAccess(State*L) {
    if(api.getTop(L)==0){NativePassengers::discard();api.pushBool(L,true);return 1;}
    bool ok=false;
    if(NativePassengers::ready && NativePassengers::checkUserdata && api.getTop(L)>=2
        && (api.type(L,1)==7 || api.type(L,1)==2) && api.type(L,2)==1) {
        bool clear=api.toBool(L,2)!=0;
        if(!clear || (passengers && !failed && !streetFailed)) {
            auto**handle=(void**)NativePassengers::checkUserdata(L,1,"gameVehicleResourcesMetatable");
            bool audioSafe=api.getTop(L)>=3 && api.type(L,3)==1 && api.toBool(L,3);
            if(handle)ok=NativePassengers::apply(*handle,clear,audioSafe);
        }
    }
    api.pushBool(L,ok);return 1;
}
static int __cdecl signalAccess(State*L){
    if(api.getTop(L)==0){NativeSignals::discard();api.pushBool(L,true);return 1;}
    bool ok=false;
    if(NativeSignals::ready && NativePassengers::checkUserdata && api.getTop(L)>=2
        && (api.type(L,1)==7 || api.type(L,1)==2) && api.type(L,2)==1){
        bool run=api.toBool(L,2)!=0;
        auto**handle=(void**)NativePassengers::checkUserdata(L,1,"gameVehicleResourcesMetatable");
        if(handle){if(!run)ok=NativeSignals::release(*handle);
            else if(!failed && !streetFailed && NativeSignals::enabled){
                bool fresh=api.getTop(L)>=3 && api.type(L,3)==1 && api.toBool(L,3);
                ok=NativeSignals::pump(*handle,fresh);
            }}
    }
    api.pushBool(L,ok);return 1;
}
static int __cdecl readStreets(State*L) {
    api.pushBool(L,traffic);int d=density.load();for(unsigned i=0;i<7;i++)api.pushBool(L,(d>>i)&1);
    api.pushBool(L,policeOff);
    api.pushBool(L,streetToken!=streetCompleted.load());api.pushBool(L,!streetFailed);
    api.pushClosure(L,nativeTraffic,0);api.pushBool(L,passengers);api.pushClosure(L,passengerAccess,0);api.pushBool(L,NativeSignals::enabled);api.pushClosure(L,signalAccess,0);return 16;
}
static std::atomic_flag inFlight=ATOMIC_FLAG_INIT;
static thread_local uint32_t syncToken=0;
static constexpr char worldKey[]="DriverSF.mod.world.v7.14.review10";
#include "../build/world_script.inc"
static void request(int preset) {
    if(preset<0 || preset>5)return;
    deadline=GetTickCount()+2000;
    uint32_t old=command.load(),next;
    do {next=((old+8u)&~7u)|uint32_t(preset);}while(!command.compare_exchange_weak(old,next));
}
static void stopLive(){live=false;if((command.load()&7u)!=0)request(0);}
static void chooseWeather(int preset){if(preset>=0 && preset<=5){live=preset!=0;request(preset);}}
static void disable(){
    freeze=false;passengers=false;NativeSignals::enabled=false;NativeSignals::cancel();stopLive();stopStreets();
    unlockGarageRequested=false;completeGame100Requested=false;spawnRequested=false;pendingSpawnModelID=0;pendingSpawnModel[0]=0;
    launchMissionRequested=false;teleportOnlyRequested=false;pendingMissionName[0]=0;
}
static bool needed(){return !failed && (freeze || live || installed || streetsNeeded() || command.load()!=completed.load() || unlockGarageRequested.load() || completeGame100Requested.load() || spawnRequested.load() || launchMissionRequested.load());}
static int __cdecl frozen(State*L){api.pushBool(L,freeze && !failed);return 1;}
static int __cdecl weather(State*L){
    api.pushBool(L,(syncToken&7u)!=0);
    api.pushBool(L,syncToken!=completed.load());
    for(unsigned n=1;n<=5;n++)api.pushBool(L,(syncToken&7u)==n);
    api.pushBool(L,true);return 8; // smooth sky-preserving mode is automatic
}
static bool gameVm(State*L) {
    const int top=api.getTop(L);bool valid=true;
    for(const char*name:{"goalSystem","moodSystem","localPlayerManager"}) {
        api.getField(L,-10002,name);valid=valid && api.type(L,-1)==5;api.setTop(L,top);
    }
    return valid;
}
static void sync(State*L) {
    if(inFlight.test_and_set(std::memory_order_acquire))return;
    struct Release{~Release(){inFlight.clear(std::memory_order_release);}}release;
    ++syncs;syncToken=command.load();streetToken=streetCommand.load();
    if(streetToken!=streetCompleted.load() && int32_t(GetTickCount()-streetDeadline.load())>0) {
        stopStreets();streetToken=streetCommand.load();logLine("Street request expired; controls OFF, owned cleanup only");
    }
    if((syncToken&7u)!=0 && syncToken!=completed.load() && int32_t(GetTickCount()-deadline.load())>0) {
        stopLive();syncToken=command.load();logLine("World weather request expired before native script update; cleanup only");
    }
    int top=api.getTop(L),error=0;const char*phase="world update";
    api.getField(L,registry,worldKey);
    if(api.type(L,-1)!=6) {
        api.setTop(L,top);
        phase="bytecode load";
        error=api.loadBuffer(L,worldSource,sizeof(worldSource)-1,"@mod/world_script.lua");
        if(!error){phase="installer factory";api.pushClosure(L,frozen,0);api.pushClosure(L,weather,0);api.pushClosure(L,readStreets,0);api.pushClosure(L,diagnostic,0);error=api.pcall(L,4,1,0);}
        if(!error && api.type(L,-1)!=6)error=-1;
        if(!error){api.pushValue(L,-1);api.setField(L,registry,worldKey);}
    }
    if(!error) {
        phase="world update";
        error=api.pcall(L,0,6,0);
        if(!error) {
            supportedTimer=api.toBool(L,-6)!=0;
            bool handled=api.toBool(L,-5)!=0,canceled=api.toBool(L,-4)!=0;
            bool streetHandled=api.toBool(L,-3)!=0,streetCanceled=api.toBool(L,-2)!=0;
            if(streetHandled){
                streetCompleted=streetToken;
                // Missing retail road API rejects ONLY density, never a working
                // police request. A newer UI command must not be canceled here.
                if(trafficUnavailable && streetCommand.load()==streetToken)traffic=false;
            }
            bool streetError=api.type(L,-1)==4;
            if(streetError && ++streetErrors>=3)streetFailed=true;
            if(streetError && api.toString && streetErrors<=3) {
                size_t n=0;const char*s=api.toString(L,-1,&n);char b[600];
                snprintf(b,sizeof(b),"Street protected error: %.*s; street controls OFF, World timers/weather independent",int(n<480?n:480),s?s:"");logLine(b);
            }
            if(streetCanceled && streetCommand.load()==streetToken && !streetFailed) {
                // Do not endlessly manufacture cleanup commands when already OFF.
                if(traffic || policeOff || streetError)stopStreets();
                else streetCompleted=streetToken;
                logLine("Street controls canceled: mission/native context, unavailable API or ownership change");
            }
            if(handled){completed=syncToken;char b[180];snprintf(b,sizeof(b),"World weather request acknowledged: token=%u preset=%u; native mood/sky request, NOT visual acceptance",unsigned(syncToken),unsigned(syncToken&7));logLine(b);}
            if(canceled && command.load()==syncToken){stopLive();logLine("World weather canceled: mission/native mood changed or preset unavailable; native mission wins");}
            installed=supportedTimer || freeze || live || streetsNeeded() || command.load()!=completed.load();
        }
    }
    if(unlockGarageRequested.exchange(false)) {
        const char *code=
            "for id=1,151 do "
            "  if ProfileSettings and type(ProfileSettings.SetVehicleUnlocked)=='function' then "
            "    pcall(ProfileSettings.SetVehicleUnlocked,id,true) "
            "  end "
            "  if type(vehicleStats)=='table' and type(vehicleStats[id])=='table' then "
            "    vehicleStats[id].UnlockedInGarage=true "
            "    vehicleStats[id].garageUnlocked=true "
            "  end "
            "end "
            "if type(vehicleManager)=='table' and type(vehicleManager.updateVehicleUnlocks)=='function' then "
            "  pcall(vehicleManager.updateVehicleUnlocks) "
            "end";
        if(api.loadBuffer(L,code,strlen(code),"@mod/unlock_garage.lua")==0)api.pcall(L,0,0,0);
    }
    if(completeGame100Requested.exchange(false)) {
        const char *code =
            "if ProfileSettings then "
            "  if type(ProfileSettings.SetStoryModeComplete)=='function' then pcall(ProfileSettings.SetStoryModeComplete,true) end "
            "  if type(ProfileSettings.SetChapter)=='function' then pcall(ProfileSettings.SetChapter,8) end "
            "  if type(ProfileSettings.SetProgression)=='function' and type(challengeProgressionTable)=='table' then pcall(ProfileSettings.SetProgression,#challengeProgressionTable) end "
            "  if type(ProfileSettings.SetWillpower)=='function' then pcall(ProfileSettings.SetWillpower,10000000) end "
            "  if type(ProfileSettings.SetUserControlledShiftUnlocked)=='function' then pcall(ProfileSettings.SetUserControlledShiftUnlocked,true) end "
            "  if type(ProfileSettings.SetTakedownUnlocked)=='function' then pcall(ProfileSettings.SetTakedownUnlocked,true) end "
            "end "
            "if type(progressionSystem)=='table' then "
            "  progressionSystem.isEverythingUnlocked=true "
            "  progressionSystem.currentChapter=8 "
            "  if type(progressionSystem.updateUnlockProgression)=='function' then pcall(progressionSystem.updateUnlockProgression) end "
            "end "
            "if type(challengeProgressionTable)=='table' and ProfileSettings then "
            "  for _,pot in ipairs(challengeProgressionTable) do "
            "    if type(pot)=='table' then "
            "      for _,mList in pairs(pot) do "
            "        if type(mList)=='table' then "
            "          for _,ch in ipairs(mList) do "
            "            if type(ch)=='table' and ch.ID then "
            "              local netID=(cards and type(cards.ReverseMissionNetworkLookup)=='table' and cards.ReverseMissionNetworkLookup[ch.ID]) or ch.ID "
            "              if netID then "
            "                if type(ProfileSettings.SetMissionAttempted)=='function' then pcall(ProfileSettings.SetMissionAttempted,netID,true) end "
            "                if type(ProfileSettings.SetMissionUnlocked)=='function' then pcall(ProfileSettings.SetMissionUnlocked,netID,true) end "
            "                if type(ProfileSettings.SetMissionCompleted)=='function' then pcall(ProfileSettings.SetMissionCompleted,netID,true) end "
            "                if type(ProfileSettings.SetMissionOwned)=='function' then pcall(ProfileSettings.SetMissionOwned,netID,true) end "
            "              end "
            "              ch.complete=true "
            "            end "
            "          end "
            "        end "
            "      end "
            "    end "
            "  end "
            "end "
            "if type(cards)=='table' and ProfileSettings then "
            "  if type(cards.MissionNetworkLookup)=='table' then "
            "    for netID,_ in pairs(cards.MissionNetworkLookup) do "
            "      if type(ProfileSettings.SetMissionAttempted)=='function' then pcall(ProfileSettings.SetMissionAttempted,netID,true) end "
            "      if type(ProfileSettings.SetMissionUnlocked)=='function' then pcall(ProfileSettings.SetMissionUnlocked,netID,true) end "
            "      if type(ProfileSettings.SetMissionCompleted)=='function' then pcall(ProfileSettings.SetMissionCompleted,netID,true) end "
            "      if type(ProfileSettings.SetMissionOwned)=='function' then pcall(ProfileSettings.SetMissionOwned,netID,true) end "
            "    end "
            "  end "
            "  if type(cards.Missions)=='table' then "
            "    for _,m in pairs(cards.Missions) do "
            "      if type(m)=='table' and m.MissionID then "
            "        if type(ProfileSettings.SetMissionAttempted)=='function' then pcall(ProfileSettings.SetMissionAttempted,m.MissionID,true) end "
            "        if type(ProfileSettings.SetMissionUnlocked)=='function' then pcall(ProfileSettings.SetMissionUnlocked,m.MissionID,true) end "
            "        if type(ProfileSettings.SetMissionCompleted)=='function' then pcall(ProfileSettings.SetMissionCompleted,m.MissionID,true) end "
            "        if type(ProfileSettings.SetMissionOwned)=='function' then pcall(ProfileSettings.SetMissionOwned,m.MissionID,true) end "
            "      end "
            "    end "
            "  end "
            "end "
            "if type(getChallengeList)=='function' and ProfileSettings then "
            "  local ok,list=pcall(getChallengeList) "
            "  if ok and type(list)=='table' then "
            "    for _,group in pairs(list) do "
            "      if type(group)=='table' then "
            "        for _,ch in ipairs(group) do "
            "          if type(ch)=='table' and ch.networkID then "
            "            local isAct=(ch.type=='activity') "
            "            local isMov=(ch.objectiveType=='Movie') "
            "            if type(ProfileSettings.SetChallengeUnlocked)=='function' then pcall(ProfileSettings.SetChallengeUnlocked,ch.networkID,isAct,isMov) end "
            "            if type(ProfileSettings.SetChallengeCompleted)=='function' then pcall(ProfileSettings.SetChallengeCompleted,ch.networkID,true) end "
            "            if type(ProfileSettings.SetChallengeOwned)=='function' then pcall(ProfileSettings.SetChallengeOwned,ch.networkID,isAct,isMov) end "
            "          end "
            "        end "
            "      end "
            "    end "
            "  end "
            "end "
            "if type(challengeLookupTable)=='table' and ProfileSettings then "
            "  for groupName,group in pairs(challengeLookupTable) do "
            "    if type(group)=='table' then "
            "      for _,ch in ipairs(group) do "
            "        local id=ch.networkID or (cards and type(cards.ReverseMissionNetworkLookup)=='table' and cards.ReverseMissionNetworkLookup[ch.ID or ch.name]) "
            "        if id then "
            "          if type(ProfileSettings.SetChallengeUnlocked)=='function' then pcall(ProfileSettings.SetChallengeUnlocked,id,groupName=='activity',groupName=='movie') end "
            "          if type(ProfileSettings.SetChallengeCompleted)=='function' then pcall(ProfileSettings.SetChallengeCompleted,id,true) end "
            "          if type(ProfileSettings.SetChallengeOwned)=='function' then pcall(ProfileSettings.SetChallengeOwned,id,true) end "
            "        end "
            "      end "
            "    end "
            "  end "
            "end "
            "if ProfileSettings then "
            "  if type(daresByChapter)=='table' then "
            "    for _,dList in pairs(daresByChapter) do "
            "      if type(dList)=='table' then "
            "        for _,dare in pairs(dList) do "
            "          local uid=dare.uid or dare.ID "
            "          if uid then "
            "            if type(ProfileSettings.SetDareUnlocked)=='function' then pcall(ProfileSettings.SetDareUnlocked,uid,true) end "
            "            if type(ProfileSettings.SetDareCompleted)=='function' then pcall(ProfileSettings.SetDareCompleted,uid,true) end "
            "          end "
            "        end "
            "      end "
            "    end "
            "  end "
            "  if type(daresByUID)=='table' then "
            "    for uid,_ in pairs(daresByUID) do "
            "      if type(ProfileSettings.SetDareUnlocked)=='function' then pcall(ProfileSettings.SetDareUnlocked,uid,true) end "
            "      if type(ProfileSettings.SetDareCompleted)=='function' then pcall(ProfileSettings.SetDareCompleted,uid,true) end "
            "    end "
            "  end "
            "  for id=1,120 do "
            "    if type(ProfileSettings.SetDareUnlocked)=='function' then pcall(ProfileSettings.SetDareUnlocked,id,true) end "
            "    if type(ProfileSettings.SetDareCompleted)=='function' then pcall(ProfileSettings.SetDareCompleted,id,true) end "
            "  end "
            "end "
            "if ProfileSettings then "
            "  if type(collectablesByUID)=='table' then "
            "    for uid,token in pairs(collectablesByUID) do "
            "      if type(ProfileSettings.SetCollectableUnlocked)=='function' then pcall(ProfileSettings.SetCollectableUnlocked,uid,true) end "
            "      if type(ProfileSettings.SetCollectableOwned)=='function' then pcall(ProfileSettings.SetCollectableOwned,uid,true) end "
            "      if type(token)=='table' then token.collected=true; token.initialiseCollected=true end "
            "    end "
            "  end "
            "  if type(collectables)=='table' and type(collectables.collectableLookupTable)=='table' then "
            "    for id,_ in pairs(collectables.collectableLookupTable) do "
            "      if type(ProfileSettings.SetCollectableUnlocked)=='function' then pcall(ProfileSettings.SetCollectableUnlocked,id,true) end "
            "      if type(ProfileSettings.SetCollectableOwned)=='function' then pcall(ProfileSettings.SetCollectableOwned,id,true) end "
            "    end "
            "  end "
            "  for id=1,250 do "
            "    if type(ProfileSettings.SetCollectableUnlocked)=='function' then pcall(ProfileSettings.SetCollectableUnlocked,id,true) end "
            "    if type(ProfileSettings.SetCollectableOwned)=='function' then pcall(ProfileSettings.SetCollectableOwned,id,true) end "
            "  end "
            "  if type(incrementTokensCollected)=='function' then "
            "    local curTokens=(type(getTokensCollected)=='function' and getTokensCollected()) or 0 "
            "    for i=curTokens+1,80 do pcall(incrementTokensCollected) end "
            "  end "
            "  if type(collectables)=='table' and type(collectables.updateCollectableUnlocks)=='function' then pcall(collectables.updateCollectableUnlocks) end "
            "end "
            "if ProfileSettings then "
            "  if type(garage)=='table' and type(garage.locations)=='table' then "
            "    for _,g in pairs(garage.locations) do "
            "      if type(g)=='table' and g.ID then "
            "        if type(ProfileSettings.SetGarageUnlocked)=='function' then pcall(ProfileSettings.SetGarageUnlocked,g.ID,true) end "
            "        if type(ProfileSettings.SetGarageOwned)=='function' then pcall(ProfileSettings.SetGarageOwned,g.ID,true) end "
            "        if type(ProfileSettings.SetGarageBought)=='function' then pcall(ProfileSettings.SetGarageBought,g.ID,true) end "
            "      end "
            "    end "
            "  end "
            "  for id=1,30 do "
            "    if type(ProfileSettings.SetGarageUnlocked)=='function' then pcall(ProfileSettings.SetGarageUnlocked,id,true) end "
            "    if type(ProfileSettings.SetGarageOwned)=='function' then pcall(ProfileSettings.SetGarageOwned,id,true) end "
            "    if type(ProfileSettings.SetGarageBought)=='function' then pcall(ProfileSettings.SetGarageBought,id,true) end "
            "  end "
            "  if type(ProfileSettings.SetGarageTutorialPlayed)=='function' then pcall(ProfileSettings.SetGarageTutorialPlayed,true) end "
            "  if type(ProfileSettings.SetGarageWillpowerTutorialPlayed)=='function' then pcall(ProfileSettings.SetGarageWillpowerTutorialPlayed,true) end "
            "  if type(garage)=='table' and type(garage.updateGarageUnlocks)=='function' then pcall(garage.updateGarageUnlocks) end "
            "end "
            "for id=1,160 do "
            "  if ProfileSettings and type(ProfileSettings.SetVehicleUnlocked)=='function' then pcall(ProfileSettings.SetVehicleUnlocked,id,true) end "
            "  if ProfileSettings and type(ProfileSettings.SetVehicleOwned)=='function' then pcall(ProfileSettings.SetVehicleOwned,id,true) end "
            "  if type(vehicleStats)=='table' and type(vehicleStats[id])=='table' then "
            "    vehicleStats[id].UnlockedInGarage=true "
            "    vehicleStats[id].garageUnlocked=true "
            "  end "
            "end "
            "if type(vehicleManager)=='table' and type(vehicleManager.updateVehicleUnlocks)=='function' then pcall(vehicleManager.updateVehicleUnlocks) end "
            "if ProfileSettings then "
            "  for ability=0,9 do "
            "    pcall(ProfileSettings.SetAbilityUnlocked,ability,0,true) "
            "    pcall(ProfileSettings.SetAbilityOwned,ability,0,true) "
            "    for level=1,2 do "
            "      pcall(ProfileSettings.SetAbilityUnlocked,ability,level,true) "
            "      pcall(ProfileSettings.SetAbilityOwned,ability,level,true) "
            "    end "
            "  end "
            "end "
            "if type(unlockAllAbilities)=='function' then pcall(unlockAllAbilities) end "
            "if type(enableCityLocking)=='function' then pcall(enableCityLocking,false) end "
            "if ProfileSettings and type(ProfileSettings.TriggerAutoSave)=='function' then pcall(ProfileSettings.TriggerAutoSave) end "
            "if type(progressionSystem)=='table' and type(progressionSystem.saveGame)=='function' then pcall(progressionSystem.saveGame,'When you complete the game') end";
        int status=api.loadBuffer(L,code,strlen(code),"@mod/complete_game_100.lua");
        if(status!=0) {
            const char* msg=api.toString?api.toString(L,-1,nullptr):"unknown syntax error";
            char b[256];snprintf(b,sizeof(b),"completeGame100 loadBuffer error %d: %s",status,msg);
            logLine(b);
        } else {
            status=api.pcall(L,0,0,0);
            if(status!=0) {
                const char* msg=api.toString?api.toString(L,-1,nullptr):"unknown runtime error";
                char b[256];snprintf(b,sizeof(b),"completeGame100 pcall error %d: %s",status,msg);
                logLine(b);
            } else {
                logLine("Game completed 100%: missions, dares, tokens, garages and cars applied and autosaved");
            }
        }
    }
    if(spawnRequested.exchange(false)) {
        int id=pendingSpawnModelID.exchange(0);
        if(id>0) {
            char buf[4096];
            snprintf(buf,sizeof(buf),
                "local id=%d "
                "local cur=localPlayer and localPlayer.currentVehicle "
                "local pos=cur and cur.position or (localPlayer and localPlayer.position) "
                "local heading=cur and cur.heading or 0 "
                "if pos then "
                "  if ProfileSettings and type(ProfileSettings.SetVehicleUnlocked)=='function' then "
                "    pcall(ProfileSettings.SetVehicleUnlocked,id,true) "
                "  end "
                "  if type(TrafficSpooler)=='table' and type(TrafficSpooler.SetAsPlayerVehicle)=='function' then "
                "    pcall(TrafficSpooler.SetAsPlayerVehicle,id) "
                "  end "
                "  local attempts=0 "
                "  local function trySpawn() "
                "    attempts=attempts+1 "
                "    local loaded=(type(TrafficSpooler)=='table' and type(TrafficSpooler.IsPlayerVehicleLoaded)=='function' and TrafficSpooler.IsPlayerVehicleLoaded()) or attempts>40 "
                "    if loaded then "
                "      if type(removeUserUpdateFunction)=='function' then pcall(removeUserUpdateFunction,'modVehicleSpawn') end "
                "      local v=nil "
                "      if type(vehicleManager)=='table' and type(vehicleManager.spawnVehicle)=='function' then "
                "        local ok,res=pcall(vehicleManager.spawnVehicle,{position=pos,heading=heading,initialVelocity=0,modelID=id}) "
                "        if ok and res then v=res end "
                "      end "
                "      if v and localPlayer then "
                "        if GameVehicleResource and type(GameVehicleResource.setCharacterSpoolingEntityIndex)=='function' and v.gameVehicle then "
                "          for i=1,3 do pcall(GameVehicleResource.setCharacterSpoolingEntityIndex,v.gameVehicle,i,-1) end "
                "        end "
                "        if type(localPlayer.clearPreviousVehicle)=='function' then pcall(localPlayer.clearPreviousVehicle,localPlayer) end "
                "        if cur and type(cur.delete)=='function' then pcall(cur.delete,cur) end "
                "        if type(localPlayer.SetZapLevel)=='function' then "
                "          local ok=pcall(localPlayer.SetZapLevel,localPlayer,0,v,true,{disableZapFlash=true}) "
                "          if not ok and v.gameVehicle then pcall(localPlayer.SetZapLevel,localPlayer,0,v.gameVehicle,true,{disableZapFlash=true}) end "
                "        end "
                "      end "
                "    end "
                "  end "
                "  if type(removeUserUpdateFunction)=='function' then pcall(removeUserUpdateFunction,'modVehicleSpawn') end "
                "  if type(addUserUpdateFunction)=='function' then "
                "    pcall(addUserUpdateFunction,'modVehicleSpawn',trySpawn,1) "
                "  else "
                "    trySpawn() "
                "  end "
                "end",id);
            pendingSpawnModel[0]=0;
            if(api.loadBuffer(L,buf,strlen(buf),"@mod/spawn_car.lua")==0)api.pcall(L,0,0,0);
        }
    }
    if(launchMissionRequested.exchange(false)) {
        if(pendingMissionName[0]) {
            bool teleportOnly = teleportOnlyRequested.exchange(false);
            char buf[8192];
            snprintf(buf, sizeof(buf),
                "local name = \"%s\" "
                "local teleportOnly = %s "
                "local mission, potID, subType = nil, nil, nil "
                "if type(progressionSystem) == 'table' and type(progressionSystem.findMissionInProgression) == 'function' then "
                "  pcall(function() mission, potID, subType = progressionSystem.findMissionInProgression(name) end) "
                "end "
                "local netID = nil "
                "if type(cards) == 'table' and type(cards.MissionNetworkLookup) == 'table' then "
                "  for id, mName in next, cards.MissionNetworkLookup do "
                "    if mName == name then netID = id break end "
                "  end "
                "end "
                "if not netID and type(cards) == 'table' and type(cards.ReverseMissionNetworkLookup) == 'table' then "
                "  netID = cards.ReverseMissionNetworkLookup[name] "
                "end "
                "if not netID and type(cards) == 'table' and type(cards.Missions) == 'table' and type(cards.Missions[name]) == 'table' then "
                "  netID = cards.Missions[name].MissionID "
                "end "
                "local targetPos, targetHeading = nil, 0 "
                "if type(cards) == 'table' and type(cards.Missions) == 'table' and type(cards.Missions[name]) == 'table' then "
                "  local m = cards.Missions[name] "
                "  if type(m.startPositions) == 'table' and m.startPositions[1] and m.startPositions[1].position then "
                "    targetPos = m.startPositions[1].position "
                "    targetHeading = m.startPositions[1].heading or 0 "
                "  elseif type(m.spawnPositions) == 'table' and m.spawnPositions[1] and m.spawnPositions[1].position then "
                "    targetPos = m.spawnPositions[1].position "
                "    targetHeading = m.spawnPositions[1].heading or 0 "
                "  elseif m.position then "
                "    targetPos = m.position "
                "    targetHeading = m.heading or 0 "
                "  end "
                "end "
                "if not targetPos and type(cardSystem) == 'table' and type(cardSystem.formattedMissionData) == 'table' and type(cardSystem.formattedMissionData[name]) == 'table' then "
                "  local ch = cardSystem.formattedMissionData[name].challenge "
                "  if type(ch) == 'table' then "
                "    if type(ch.actorPool) == 'table' then "
                "      for _, actor in ipairs(ch.actorPool) do "
                "        if type(actor) == 'table' and type(actor.spawn) == 'table' and actor.spawn.position then "
                "          targetPos = actor.spawn.position "
                "          targetHeading = actor.spawn.heading or 0 "
                "          break "
                "        end "
                "      end "
                "    end "
                "    if not targetPos and type(ch.settings) == 'table' and ch.settings.position then "
                "      targetPos = ch.settings.position "
                "      targetHeading = ch.settings.heading or 0 "
                "    end "
                "  end "
                "end "
                "if not targetPos and type(mission) == 'table' then "
                "  if mission.position then "
                "    targetPos = mission.position "
                "    targetHeading = mission.heading or 0 "
                "  elseif type(mission.settings) == 'table' and mission.settings.position then "
                "    targetPos = mission.settings.position "
                "    targetHeading = mission.settings.heading or 0 "
                "  end "
                "end "
                "if targetPos and type(localPlayer) == 'table' then "
                "  if type(localPlayer.inZap) == 'function' and localPlayer.inZap() and type(localPlayer.SetZapLevel) == 'function' then "
                "    pcall(localPlayer.SetZapLevel, localPlayer, 0) "
                "  end "
                "  local cur = localPlayer.currentVehicle "
                "  if cur then "
                "    if type(alignMatrix) == 'function' and type(cur.teleport) == 'function' then "
                "      local ok, mat = pcall(alignMatrix, targetPos, targetHeading or 0) "
                "      if ok and mat then pcall(cur.teleport, cur, mat) end "
                "    end "
                "    if type(cur.teleportToPositionAndHeading) == 'function' then "
                "      pcall(cur.teleportToPositionAndHeading, cur, targetPos, targetHeading or 0, nil, false) "
                "    end "
                "  elseif type(localPlayer.teleportToPositionAndHeading) == 'function' then "
                "    pcall(localPlayer.teleportToPositionAndHeading, localPlayer, targetPos, targetHeading or 0) "
                "  end "
                "end "
                "if not teleportOnly then "
                "  if type(challengeSystem) == 'table' and type(challengeSystem.instances) == 'table' then "
                "    for _, inst in next, challengeSystem.instances do "
                "      if type(inst) == 'table' and type(inst.delete) == 'function' then "
                "        pcall(inst.delete, inst) "
                "      end "
                "    end "
                "  end "
                "  if potID and type(challengeProgressionTable) == 'table' and challengeProgressionTable[potID] then "
                "    if type(progressionSystem) == 'table' then progressionSystem.currentProgression = potID end "
                "    local current = challengeProgressionTable[potID] "
                "    if type(current) == 'table' and type(current.missions) == 'table' then "
                "      for _, ch in next, current.missions do ch.complete = true end "
                "    end "
                "    for i = potID + 1, #challengeProgressionTable do "
                "      local pot = challengeProgressionTable[i] "
                "      if type(pot) == 'table' and type(pot.storyMission) == 'table' then "
                "        for j = 1, #pot.storyMission do pot.storyMission[j].complete = false end "
                "      end "
                "    end "
                "  end "
                "  local launched = false "
                "  if type(progressionSystem) == 'table' and type(progressionSystem.initiateMissionsMenu) == 'function' then "
                "    local ok, fn = pcall(progressionSystem.initiateMissionsMenu, name) "
                "    if ok and type(fn) == 'function' then "
                "      pcall(fn) "
                "      launched = true "
                "    end "
                "  end "
                "  if not launched and netID and type(progressionSystem) == 'table' and type(progressionSystem.launchMission) == 'function' then "
                "    launched = pcall(progressionSystem.launchMission, netID) "
                "  end "
                "  if not launched and potID and type(progressionSystem) == 'table' and type(progressionSystem.applyChapterSettings) == 'function' then "
                "    launched = pcall(progressionSystem.applyChapterSettings, potID, netID or name, true, false, false, false, nil, false, false) "
                "  end "
                "end "
                "if type(PauseMenu) == 'table' and type(PauseMenu.resume) == 'function' then "
                "  pcall(PauseMenu.resume) "
                "end",
                pendingMissionName,
                teleportOnly ? "true" : "false"
            );
            pendingMissionName[0] = 0;
            if(api.loadBuffer(L, buf, strlen(buf), "@mod/launch_mission.lua") == 0) api.pcall(L, 0, 0, 0);
            logLine("Mission launch script executed");
        }
    }
    if(error && api.toString && api.getTop(L)>top && api.type(L,-1)==4) {
        // Known string only: do not invoke a metamethod or convert/allocate on
        // an unprotected error path. Read before restoring the native stack.
        size_t length=0;const char*message=api.toString(L,-1,&length);
        if(message){char b[600];snprintf(b,sizeof(b),"World Lua %s detail: %.*s",phase,int(length<480?length:480),message);logLine(b);}
    }
    api.setTop(L,top); // every path restores exact native Lua arguments/results
    if(error) {
        unsigned n=++errors;disable();supportedTimer=false;installed=true;
        char b[180];snprintf(b,sizeof(b),"World protected Lua error=%d count=%u; controls OFF; cleanup requested; Nitro independent",error,n);logLine(b);
        if(n>=3){failed=true;logLine("World Lua disabled after repeated failures; restart game before further world testing");}
    }
}
static void draw() {
    bool usable=bound && ready && !failed;
    Ui::TextWrapped("Mission time");
    ImGui::BeginDisabled(!usable);
    bool on=freeze;if(Ui::Checkbox("Freeze mission countdown",&on))freeze=on;
    ImGui::EndDisabled();
    Ui::TextWrapped("Supported failure countdowns and Paranoia suspicion only, including its buffered 120-second HUD. Not health/damage bars. Physics and mission event waits stay native.");
    if(freeze)Ui::TextDisabled(supportedTimer?"Supported countdown detected.":"Waiting for a supported countdown / native script update.");
    ImGui::Separator();
    Ui::TextWrapped("Weather / lighting");
    ImGui::BeginDisabled(!usable);
    const char*names[]={"Game weather (default)","Clear day","Fog / overcast style","Sunset","Red mission style","Frozen mission style"};
    int value=int(command.load()&7u);if(value>5)value=0;
    if(ImGui::BeginCombo(UiText::label("Weather").c_str(),UiText::lookup(names[value]))){
        for(int i=0;i<=5;i++){
            if(ImGui::Selectable(UiText::label(names[i]).c_str(),i==value))chooseWeather(i);
            if(i==value)ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Ui::TextWrapped("Smooth lighting/fog changes are automatic and keep the current sky dome. Select Game weather to restore native weather. Mission weather takes priority.");
    ImGui::Separator();
    Ui::TextWrapped("Police / traffic - free drive only");
    ImGui::BeginDisabled(!usable || streetFailed);
    bool alone=passengers;
    if(Ui::Checkbox("No passengers in player car",&alone)){passengers=alone;streetRequest();}
    bool block=policeOff;
    if(Ui::Checkbox("Disable free-drive police",&block)){policeOff=block;streetRequest();}
    bool use=traffic;
    if(Ui::Checkbox("Control traffic level",&use)){
        traffic=use;
        if(use)trafficUnavailable=false;
        else if(density==0)density=100;
        streetRequest();
    }
    int percent=density;
    if(Ui::SliderInt("Traffic level (0-100)",&percent,0,100)){density=percent<0?0:percent>100?100:percent;if(traffic)streetRequest();}
    if(Ui::Button("No Traffic (0)")) {density=0;traffic=true;policeOff=true;trafficUnavailable=false;streetRequest();}
    ImGui::SameLine();
    if(Ui::Button("Traffic: 50%")) {density=50;traffic=true;trafficUnavailable=false;streetRequest();}
    ImGui::SameLine();
    if(Ui::Button("Normal Traffic (100)")) {density=100;traffic=true;trafficUnavailable=false;streetRequest();}
    if(traffic && density==0) Ui::Text("Status: Zero traffic and ambient police blocked.");
    ImGui::EndDisabled();
    Ui::TextWrapped("Offline free drive only. Traffic: 0 = civilian traffic and free-drive police OFF (active pursuit canceled), 1 = lightest gradual civilian spawning, 100 = normal game traffic. ZERO temporarily includes the police block even if its separate switch is OFF; leaving ZERO restores only owned policy unless the separate police block remains ON. Levels 1-99 retain already admitted native tracks; engine streaming can still spawn cars. No passengers is ON by default for the player car only, preserves the driver and suspends in missions. Mission/script vehicles and native policy take priority; no global speech mute.");
    if(trafficUnavailable)Ui::TextWrapped("Traffic request unavailable or native policy took ownership. Density is OFF; police and passenger controls remain independent.");
    if(streetFailed)Ui::TextDisabled("Street controls failed repeatedly; restart required. Mission timers and weather remain independent.");
    if(!usable)Ui::TextDisabled("World script controls are not ready.");
    else if(command.load()!=completed.load())Ui::TextDisabled("Waiting for the next native script update.");
    if(showDiagnostics)Ui::Text("World sync / errors: %lu / %lu",syncs.load(),errors.load());
}
} // namespace World
