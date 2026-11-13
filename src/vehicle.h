// Internal, single-player vehicle hooks for the hash-gated executable only.
// Player-only native hooks; native register/stack contracts must be retained.
#include "vehicle_policy.h"
#include "complete_hook.h"
namespace VehicleMod {
static std::atomic<bool> health{false}, nitro{false}, body{false}, speed{false};
static std::atomic<bool> ram{false}, oneHit{false};
static std::atomic<bool> ready{false}, repairRequested{false};
static std::atomic<unsigned long> ramCalls{0}, ramChanges{0}, collisionCalls{0}, wreckCalls{0};
static std::atomic<uintptr_t> repairFor{0};
static std::atomic<DWORD> repairDeadline{0};
static std::atomic<unsigned long> repairs{0}, cancelledRepairs{0};
static std::atomic<int> boostCapKmh{300};
static std::atomic<unsigned long> frames{0}, nitroCalls{0}, damageChecks{0}, bodyChecks{0}, speedChecks{0};
static char bindingStatus[160]="Vehicle hooks not bound.";
using FloatGetter = float (__thiscall *)(void*);
using BoolGetter = unsigned char (__thiscall *)(void*);
using FloatSetter = void (__thiscall *)(void*,float);
using Repair = void (__thiscall *)(void*);
using AbilityUpdate = void (__thiscall *)(void*,void*);
using Collision = void (__thiscall *)(void*,void*,void*,uintptr_t,uintptr_t);
static FloatGetter originalMaximum, originalTopSpeed;
static BoolGetter originalDisableDamage;
static FloatSetter originalDamage;
static Repair repair;
static AbilityUpdate originalRamUpdate;
// C symbols are consumed by nitro_bridge.S; this is NOT a normal C++ detour.
extern "C" {
AbilityUpdate originalNitroHeat=nullptr;
void nitroHeatHook();
}
struct NitroContext { BYTE *vehicle; int state; float fuel; bool ours; };
static_assert(sizeof(NitroContext)==16,"Nitro bridge requires 32-bit layout");
static Collision originalCollision;
static bool writable(void *p,size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    return readable(p,n) && VirtualQuery(p,&m,sizeof(m)) &&
        (m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}

static BYTE *root() {
    if(!bound || !singletonSlot || !readable(singletonSlot,4))return nullptr;
    BYTE *p=(BYTE*)*singletonSlot;
    return readable(p,0x68c)?p:nullptr;
}
// Same attachment query as VEdit.ResetVehicleDamage(), player index 0.
// The flag at manager+0x688 makes the shipped getter return null while shifting.
static BYTE *playerVehicle() {
    BYTE *r=root();if(!r)return nullptr;
    BYTE *manager=*(BYTE**)(r+0x684);
    if(!readable(manager,0x68c) || *(uint32_t*)(manager+0x688))return nullptr;
    BYTE *v=*(BYTE**)(manager+0x678);
    return readable(v,0x5f4)?v:nullptr;
}
static bool playerPhysics(void *p) {
    BYTE *v=playerVehicle();return v && readable(p,0xa54) && *(BYTE**)((BYTE*)p+0xa50)==v;
}
// 0x7a9530 constructs boost at +0x10 and RAM at +0x68 within
// the player ability block (manager+0x48). These are different objects.
static BYTE *abilityManager() {
    BYTE *r=root();if(!r)return nullptr;
    BYTE *manager=*(BYTE**)(r+0x688);
    return readable(manager,0x2b4)?manager:nullptr;
}
static bool playerRam(void *p) {
    BYTE *manager=abilityManager();
    return manager && p==manager+0xb0 && writable(p,0x148);
}
static BYTE *vehiclePhysics(BYTE *v) {
    if(!readable(v,0x5f4) || *(int*)(v+0x544)!=3)return nullptr;
    BYTE *adapter=*(BYTE**)(v+0x53c);
    if(!readable(adapter,0xc))return nullptr;
    BYTE *physics=*(BYTE**)(adapter+8);
    return readable(physics,0x1154) && *(BYTE**)(physics+0xa50)==v?physics:nullptr;
}
static bool playerNitroEngine(void *p) {
    BYTE *physics=vehiclePhysics(playerVehicle());
    return physics && *(BYTE**)(physics+0x1150)==p && writable(p,0xdb8);
}
static float __fastcall maximumHook(void *self,void*) {
    if(health && self==playerVehicle()) {++damageChecks;return 0.0f;}
    return originalMaximum(self);
}
static void __fastcall damageHook(void *self,void*,float damage) {
    if((health || body) && self==playerVehicle()) {++damageChecks;damage=0.0f;}
    originalDamage(self,damage);
}
static unsigned char __fastcall disableDamageHook(void *self,void*) {
    if(body && playerPhysics(self)){++bodyChecks;return 1;}
    return originalDisableDamage(self);
}
static float __fastcall topSpeedHook(void *self,void*) {
    float normal=originalTopSpeed(self);
    // This getter limits boost/RAM speed. It is NOT a proven powertrain tune.
    // Never lower a vehicle's already higher stock limit.
    if(speed && self==playerVehicle()) {
        ++speedChecks;float cap=boostCapKmh.load()/3.6f;
        return normal<cap?cap:normal;
    }
    return normal;
}
// Entry and native return registers/FP state are protected by nitro_bridge.S.
// Context is per-call stack storage, not shared/TLS; reentrant updates are safe.
extern "C" void nitroHeatBefore(void *self,NitroContext *context) {
    ++nitroCalls;*context={};
    if(!nitro)return;
    BYTE *v=playerVehicle(),*p=(BYTE*)self;
    if(!v || !playerNitroEngine(self))return;
    context->ours=true;context->vehicle=v;
    context->state=*(int*)(p+0xa8);context->fuel=*(float*)(p+0xb0);++frames;
    if(context->state==1 && VehiclePolicy::positive(context->fuel))
        *(float*)(p+0xb4)=VehiclePolicy::activeHeat(*(float*)(p+0xb4));
}
extern "C" void nitroHeatAfter(void *self,const NitroContext *context) {
    if(!context->ours || !nitro || context->vehicle!=playerVehicle() || !playerNitroEngine(self))return;
    BYTE *p=(BYTE*)self;int after=*(int*)(p+0xa8);
    *(float*)(p+0xb0)=VehiclePolicy::remaining(context->state,after,context->fuel,*(float*)(p+0xb0),*(float*)(p+0xd98));
    if(context->state==1 && after==1 && VehiclePolicy::positive(*(float*)(p+0xb0)))
        *(float*)(p+0xb4)=VehiclePolicy::activeHeat(*(float*)(p+0xb4));
}
static void fullRamCharge(BYTE *p) {
    float duration=*(float*)(p+0x3c),elapsed=*(float*)(p+0x38);
    if(VehiclePolicy::charge(*(int*)(p+4),duration,elapsed)) {
        // Native charge is elapsed/duration. Bound full charge indefinitely;
        // start/release, unlocks and post-impact transitions remain native.
        *(float*)(p+0x38)=duration;++ramChanges;
    }
}
static void __fastcall ramUpdateHook(void *self,void*,void *input) {
    ++ramCalls;BYTE *v=playerVehicle(),*p=(BYTE*)self,*in=(BYTE*)input;
    bool ours=ram && v && playerRam(self) && writable(input,0xa0) && *(BYTE**)(in+0x98)==v;
    int state=0;float before[3]{};
    if(ours) {
        fullRamCharge(p);state=*(int*)(p+4);memcpy(before,in+0x40,sizeof(before));
    }
    originalRamUpdate(self,input);
    if(ours && ram && v==playerVehicle() && playerRam(self) && *(BYTE**)(in+0x98)==v) {
        fullRamCharge(p);
        if(state==2 && *(int*)(p+4)==2) {
            float after[3],amplified[3];memcpy(after,in+0x40,sizeof(after));
            if(VehiclePolicy::amplify(before,after,amplified))memcpy(in+0x40,amplified,sizeof(amplified));
        }
    }
}
static BYTE *collisionVehicle(void *p) {
    if(!readable(p,0x1154))return nullptr;
    BYTE *v=*(BYTE**)((BYTE*)p+0xa50);
    return vehiclePhysics(v)==p?v:nullptr;
}
static void __fastcall collisionHook(void *self,void*,void *a,void *b,uintptr_t c,uintptr_t d) {
    ++collisionCalls;
    // 0x7a97b0 post-contact dispatch, ret 0x10: first two arguments
    // are physics objects, not arbitrary Lua IDs or body handles.
    originalCollision(self,a,b,c,d);
    if(!oneHit)return;
    BYTE *v=playerVehicle(),*manager=abilityManager();
    if(!v || !manager || self!=manager+0x48 || *(BYTE**)((BYTE*)self+0x250)!=v)return;
    BYTE *va=collisionVehicle(a),*vb=collisionVehicle(b);
    BYTE *target=(BYTE*)VehiclePolicy::collisionTarget(uintptr_t(v),uintptr_t(va),uintptr_t(vb));
    if(!target || v!=playerVehicle())return; // scenery, Shift, AI/AI excluded
    float maximum=originalMaximum(target);
    if(!VehiclePolicy::positive(maximum))return; // scripted invulnerability
    if(*(float*)(target+0x508)>=maximum)return;
    // Engine setter also forwards damage to active physics. No global damage
    // scalar, forced explosion, queued raw pointer or fabricated callback.
    originalDamage(target,maximum);++wreckCalls;
}
// Exact instruction signatures, unique in executable sections; no fixed VAs.
static BYTE *uniqueCode(const unsigned char *signature,size_t length) {
    auto nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
    auto sections=IMAGE_FIRST_SECTION(nt);BYTE *result=nullptr;int matches=0;
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++) {
        if(!(sections[i].Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
        BYTE *start=image+sections[i].VirtualAddress;size_t size=sections[i].Misc.VirtualSize;
        if(!inside(start,size))return nullptr;
        for(size_t k=0;k+length<=size;k++)if(!memcmp(start+k,signature,length)){result=start+k;++matches;}
    }
    return matches==1?result:nullptr;
}
#include "passengers.h"
#include "signals.h"
#include "traffic.h"
#include "nitro_lua.h"
static void bind() {
    if(!bound){strcpy(bindingStatus,"Vehicle hooks disabled: executable/offline validation failed.");return;}
    const unsigned char maxSig[]={0x51,0x8a,0x81,0xec,0,0,0,0xc0,0xe8,2,0xa8,1};
    const unsigned char damageSig[]={0x8a,0x81,0xec,0,0,0,0xc0,0xe8,2,0xa8,1,0x74,0x0a,0xf3,0x0f,0x10};
    const unsigned char disableSig[]={0x8b,0x81,0x50,0x0a,0,0,0x8a,0x80,0xe2,5,0,0,0xc3};
    const unsigned char updateSig[]={0x81,0xec,0xac,0,0,0,0x53,0x8b,0x9c,0x24,0xb4,0,0,0,0x55,0x56,0x8b,0xe9,0x57,0x8d,0xbd,0x8c,0,0,0,0xb9,0x28,0,0,0,0x8b,0xf3};
    const unsigned char repairSig[]={0x51,0x53,0x56,0x8b,0xf1,0x8b,0x86,0x74,5,0,0,0x57,0x33,0xff,0x85,0xc0,0x0f};
    const unsigned char speedSig[]={0x51,0x8b,0x81,0xb4,5,0,0,0x8b,0x88,0xf0,0x2e,0,0};
    const unsigned char heatSig[]={0x8b,0x54,0x24,0x04,0xf3,0x0f,0x10,0x15,0x14,0x64,0xdd,0x00,0x0f,0x57,0xed};
    const unsigned char contactSig[]={0x51,0x56,0x8b,0xf1,0x8b,0x8e,0x50,0x02,0,0,0x85,0xc9,0x74,0x65};
    void *targets[]={uniqueCode(maxSig,sizeof(maxSig)),uniqueCode(damageSig,sizeof(damageSig)),uniqueCode(disableSig,sizeof(disableSig)),uniqueCode(updateSig,sizeof(updateSig)),uniqueCode(speedSig,sizeof(speedSig)),uniqueCode(heatSig,sizeof(heatSig)),uniqueCode(contactSig,sizeof(contactSig))};
    constexpr int hookCount=sizeof(targets)/sizeof(targets[0]);
    repair=(Repair)uniqueCode(repairSig,sizeof(repairSig));
    if(!repair){strcpy(bindingStatus,"Repair signature mismatch; vehicle features disabled.");return;}
    for(auto target:targets)if(!target){strcpy(bindingStatus,"Non-unique/missing vehicle signature; features disabled.");return;}
    void *detours[]={(void*)maximumHook,(void*)damageHook,(void*)disableDamageHook,(void*)ramUpdateHook,(void*)topSpeedHook,(void*)nitroHeatHook,(void*)collisionHook};
    void **originals[]={(void**)&originalMaximum,(void**)&originalDamage,(void**)&originalDisableDamage,(void**)&originalRamUpdate,(void**)&originalTopSpeed,(void**)&originalNitroHeat,(void**)&originalCollision};
    int created=0;bool ok=true;
    for(;created<hookCount;created++)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK){ok=false;break;}
    if(ok)for(auto target:targets)if(MH_EnableHook(target)!=MH_OK){ok=false;break;}
    if(!ok){for(int i=0;i<created;i++){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}strcpy(bindingStatus,"Vehicle hook installation failed; all vehicle hooks removed.");logLine(bindingStatus);return;}
    ready=true;strcpy(bindingStatus,"Experimental vehicle hooks bound; driving tests pending.");logLine(bindingStatus);
    NitroLua::bind();
    for(int i=0;i<hookCount;i++){char b[100];snprintf(b,sizeof(b),"vehicle hook %d resolved RVA=%lx",i,(unsigned long)((BYTE*)targets[i]-image));logLine(b);}
}
static void cancelRepair() {repairRequested=false;repairFor=0;}
static uintptr_t lastObservedCar=0;
static void executeRepair() {
    if(!repairRequested.exchange(false))return;
    uintptr_t expected=repairFor.exchange(0);BYTE *v=playerVehicle();
    if(!ready || !v || uintptr_t(v)!=expected || LONG(repairDeadline.load()-GetTickCount())<0 ||
       GetCurrentThreadId()!=GetWindowThreadProcessId(windowHandle,nullptr)) {
        ++cancelledRepairs;logLine("vehicle: stale/changed-car/wrong-thread repair cancelled");return;
    }
    // Same window/game-thread dispatch boundary as WP Apply, independent of
    // Nitro unlock/update. The exact repair entry is hash/signature validated.
    float before=*(float*)(v+0x508);repair(v);float after=*(float*)(v+0x508);
    ++repairs;char b[200];snprintf(b,sizeof(b),"vehicle: one-shot engine repair executed, damage %.6f -> %.6f",before,after);logLine(b);
}
static void requestRepair() {
    BYTE *v=playerVehicle();
    if(!ready || !v){logLine("vehicle: repair refused, no attached car");return;}
    repairFor=uintptr_t(v);repairDeadline=GetTickCount()+2000;repairRequested=true;
    if(!PostMessage(windowHandle,repairMessage,0,0)){cancelRepair();logLine("vehicle: repair dispatch failed");return;}
    logLine("vehicle: repair queued for current car, expires in 2 seconds");
}
static void refillNitro() {
    BYTE *v=playerVehicle();
    if(!ready || !v)return;
    BYTE *physics=vehiclePhysics(v);
    if(!physics)return;
    BYTE *engine=*(BYTE**)(physics+0x1150);
    if(engine && writable(engine,0xdb8)) {
        float maxFuel=*(float*)(engine+0xd98);
        if(maxFuel>0.0f) {
            *(float*)(engine+0xb0)=maxFuel;
            *(float*)(engine+0xb4)=0.0f;
            logLine("vehicle: Nitro refilled to full");
        }
    }
}
static void observe() {
    BYTE *v=playerVehicle();
    if(v) {
        if(uintptr_t(v)!=lastObservedCar) {
            lastObservedCar=uintptr_t(v);
            if(health.load() || body.load())requestRepair();
            if(nitro.load())refillNitro();
            if(NitroLua::World::density.load()==0) {
                NitroLua::World::traffic=true;
                NitroLua::World::policeOff=true;
                NitroLua::World::trafficUnavailable=false;
                NitroLua::World::streetRequest();
            } else if(NitroLua::World::traffic.load()) {
                NitroLua::World::streetRequest();
            }
        }
        if(NitroLua::World::density.load()==0 && !NitroLua::World::traffic.load()) {
            NitroLua::World::traffic=true;
            NitroLua::World::policeOff=true;
            NitroLua::World::trafficUnavailable=false;
            NitroLua::World::streetRequest();
        }
    } else {
        lastObservedCar=0;
    }
}
static void disableAll() {
    ram=false;oneHit=false;
    health=false;nitro=false;body=false;speed=false;cancelRepair();
    logLine("vehicle: all overrides OFF; original getters delegated immediately");
}
static void button(const char *name,std::atomic<bool>& flag,bool initialRepair=false) {
    bool on=flag.load();char label[140];snprintf(label,sizeof(label),"%s: %s",name,on?"ON":"OFF");
    if(Ui::Button(label,ImVec2(-1,0))) {
        flag=!on;if(initialRepair && !on)requestRepair();
        if(&flag==&nitro && !on)refillNitro();
        char b[190];snprintf(b,sizeof(b),"vehicle requested %s=%s (effect not established by toggle alone)",name,!on?"ON":"OFF");logLine(b);
    }
}
static bool garageUnlockNotified = false;

static void draw() {
    if(!playerVehicle())Ui::TextDisabled("Load Story and enter a car.");
    ImGui::BeginDisabled(!ready);
    if(Ui::CollapsingHeader("Vehicle damage",ImGuiTreeNodeFlags_DefaultOpen)) {
        button("Health protection",health,true);
        button("Body / collision protection",body,true);
        button("One-hit wreck (player collisions)",oneHit);
        Ui::TextDisabled("One-hit affects the car you hit.");
        ImGui::BeginDisabled(!playerVehicle());
        if(Ui::Button("Repair current vehicle",ImVec2(-1,0)))requestRepair();
        ImGui::EndDisabled();
        if(showDiagnostics)Ui::Text("Repair calls: %lu | cancelled: %lu | pending: %s",repairs.load(),cancelledRepairs.load(),repairRequested?"yes":"no");
        if(Ui::Button("Restore damage defaults")){health=false;body=false;oneHit=false;cancelRepair();logLine("damage defaults restored");}
        Ui::TextWrapped("Turn both protections OFF for normal damage. Repairs are not undone.");
    }
    if(Ui::CollapsingHeader("Boost",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(!NitroLua::ready);
        button("Infinite Nitrous",nitro);
        ImGui::EndDisabled();
        if(!NitroLua::ready)Ui::TextDisabled("Nitro is not ready.");
        if(showDiagnostics)Ui::Text("Nitro Lua: %s | callbacks: %lu | errors: %lu",!NitroLua::ready?"not ready":NitroLua::scriptReady?"installed":"pending input",NitroLua::calls.load(),NitroLua::errors.load());
        button("Unlimited RAM charge / 2x launch (Alt)",ram);
        Ui::TextWrapped("RAM: hold Alt, then release.");
        if(Ui::Button("Restore boost defaults")){nitro=false;ram=false;logLine("boost/RAM defaults restored");}
    }
    if(Ui::CollapsingHeader("Game Completion [100%]",ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool complete100Notified=false;
        if(Ui::Button("Complete Game [100%]: All Missions, Dares & Tokens",ImVec2(-1,0))) {
            if(onCompleteGame100) onCompleteGame100();
            else NitroLua::World::queueCompleteGame100();
            complete100Notified=true;
        }
        if(complete100Notified) {
            Ui::Text("[100%] All missions, dares, tokens & cars unlocked and saved.");
        }
        Ui::TextDisabled("Unlocks all main story missions, challenges, city movie tokens, garages and cars.");
    }
    if(Ui::CollapsingHeader("Mission & special vehicles",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(Ui::Button("Unlock all vehicles & mission cars in garage",ImVec2(-1,0))) {
            NitroLua::World::queueUnlockGarage();
            garageUnlockNotified=true;
        }
        if(garageUnlockNotified) {
            Ui::Text("All vehicles & mission cars unlocked in garage!");
        }
    }
    if(Ui::CollapsingHeader("Main story missions",ImGuiTreeNodeFlags_DefaultOpen)) {
        struct MissionItem { const char* id; const char* display; };
        struct ChapterItem { const char* name; int count; const MissionItem* missions; };

        static const MissionItem expositionMissions[] = {
            {"Exposition pre crash chase", "The Chase (Prologue)"},
            {"Exposition 01 Forty Adam Thirty", "40 Adam 30"},
            {"Exposition 01 he's getting away", "He's Getting Away"},
            {"Exposition 02 I wish we could help", "I Wish We Could Help"},
            {"Exposition 03 zap at will", "Zap At Will"},
            {"Exposition 04 return to dealer", "Return to Dealer"},
            {"Exposition 06 Law Breaker (cop)", "Law Breaker"},
            {"The debrief", "The Debrief"}
        };
        static const MissionItem chapter1Missions[] = {
            {"Tanner & Jones Mission 1", "Tanner & Jones 1 (Takedown)"},
            {"1 Downtown race", "Downtown Race"},
            {"Breaking news", "Breaking News"},
            {"Escape the law", "Escape The Law"},
            {"Learn to scream", "Learn To Scream"},
            {"Trunked", "Trunked"}
        };
        static const MissionItem chapter2Missions[] = {
            {"Tanner & Jones Mission 2", "Tanner & Jones 2"},
            {"All clubbed out", "All Clubbed Out"},
            {"Easy Street", "Easy Street"},
            {"Heat From Above", "Heat From Above"},
            {"Streetrace takedown", "Street Race Takedown"}
        };
        static const MissionItem chapter3Missions[] = {
            {"Tanner And Jones 3", "Tanner & Jones 3"},
            {"In the nick of time", "In The Nick Of Time"},
            {"Peroxide convoy", "Peroxide Convoy"},
            {"Team colours 01", "Team Colours"},
            {"TheSneakout", "The Sneakout"},
            {"Wrecked evidence", "Wrecked Evidence"}
        };
        static const MissionItem chapter4Missions[] = {
            {"Tanner & Jones Mission 4", "Tanner & Jones 4"},
            {"Collateral Damage", "Collateral Damage"},
            {"Escape the law 3", "Escape The Law 3"},
            {"Felony lure", "Felony Lure"},
            {"Final destination", "Final Destination"},
            {"Speed Race", "Speed Race"}
        };
        static const MissionItem chapter5Missions[] = {
            {"Bad medicine", "Bad Medicine"},
            {"Deactivating bombs under trucks", "Bomb Under Trucks"},
            {"DriveToSurvive", "Drive To Survive"},
            {"Something weird", "Something Weird"}
        };
        static const MissionItem chapter6Missions[] = {
            {"Tanner and Jones 6", "Tanner & Jones 6"},
            {"Gone in 59 seconds", "Gone in 59 Seconds"},
            {"High plains drifter", "High Plains Drifter"},
            {"Kill Tanner", "Kill Tanner"},
            {"Protect the base", "Protect The Base"},
            {"Smash tv", "Smash TV"}
        };
        static const MissionItem chapter7Missions[] = {
            {"Tanner and Jones 7", "Tanner & Jones 7"},
            {"Alone", "Alone"},
            {"Bad medicine 2", "Bad Medicine 2"},
            {"Breaking news 2", "Breaking News 2"},
            {"Marin County race", "Marin County Race"},
            {"Mass Chase", "Mass Chase"}
        };
        static const MissionItem finaleMissions[] = {
            {"Avoid The Cars", "Avoid The Cars"},
            {"Anything you can do", "Anything You Can Do"},
            {"Final fight", "Final Fight"}
        };
        static const MissionItem epilogueMissions[] = {
            {"Epilogue", "Epilogue Part 1"},
            {"Epilogue pt 2", "Epilogue Part 2"}
        };
        static const ChapterItem chaptersList[] = {
            {"Exposition (Prologue)", sizeof(expositionMissions)/sizeof(expositionMissions[0]), expositionMissions},
            {"Chapter 1", sizeof(chapter1Missions)/sizeof(chapter1Missions[0]), chapter1Missions},
            {"Chapter 2", sizeof(chapter2Missions)/sizeof(chapter2Missions[0]), chapter2Missions},
            {"Chapter 3", sizeof(chapter3Missions)/sizeof(chapter3Missions[0]), chapter3Missions},
            {"Chapter 4", sizeof(chapter4Missions)/sizeof(chapter4Missions[0]), chapter4Missions},
            {"Chapter 5", sizeof(chapter5Missions)/sizeof(chapter5Missions[0]), chapter5Missions},
            {"Chapter 6", sizeof(chapter6Missions)/sizeof(chapter6Missions[0]), chapter6Missions},
            {"Chapter 7", sizeof(chapter7Missions)/sizeof(chapter7Missions[0]), chapter7Missions},
            {"Finale", sizeof(finaleMissions)/sizeof(finaleMissions[0]), finaleMissions},
            {"Epilogue", sizeof(epilogueMissions)/sizeof(epilogueMissions[0]), epilogueMissions}
        };

        static int selectedChapter = 1;
        static int selectedMission = 0;

        if(ImGui::BeginCombo(UiText::label("Chapter").c_str(), UiText::lookup(chaptersList[selectedChapter].name))) {
            for(int i = 0; i < 10; i++) {
                if(ImGui::Selectable(UiText::label(chaptersList[i].name).c_str(), i == selectedChapter)) {
                    selectedChapter = i;
                    selectedMission = 0;
                }
                if(i == selectedChapter) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const auto& curChap = chaptersList[selectedChapter];
        if(selectedMission >= curChap.count) selectedMission = 0;

        if(ImGui::BeginCombo(UiText::label("Mission").c_str(), UiText::lookup(curChap.missions[selectedMission].display))) {
            for(int i = 0; i < curChap.count; i++) {
                if(ImGui::Selectable(UiText::label(curChap.missions[i].display).c_str(), i == selectedMission)) {
                    selectedMission = i;
                }
                if(i == selectedMission) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if(Ui::Button("Launch selected mission", ImVec2(-1, 0))) {
            NitroLua::World::queueLaunchMission(curChap.missions[selectedMission].id, false);
        }
        if(Ui::Button("Teleport to mission location", ImVec2(-1, 0))) {
            NitroLua::World::queueLaunchMission(curChap.missions[selectedMission].id, true);
        }

        if(NitroLua::World::lastLaunchedMission[0]) {
            if(NitroLua::World::lastLaunchWasTeleportOnly.load()) {
                Ui::Text("Teleported to mission: %s", NitroLua::World::lastLaunchedMission);
            } else {
                Ui::Text("Mission launched: %s", NitroLua::World::lastLaunchedMission);
            }
        }
        Ui::TextWrapped("Select any chapter and main mission to play it directly.");
    }
    ImGui::EndDisabled();
    if(!ready)Ui::TextWrapped("Vehicle controls are not ready. Check the mod log.");
    if(showDiagnostics) {
        Ui::TextWrapped("%s",bindingStatus);
        Ui::Text("Nitro/player: %lu/%lu | health/body/ceiling: %lu/%lu/%lu",nitroCalls.load(),frames.load(),damageChecks.load(),bodyChecks.load(),speedChecks.load());
        Ui::Text("RAM/full-charge: %lu/%lu | contacts/wreck: %lu/%lu",ramCalls.load(),ramChanges.load(),collisionCalls.load(),wreckCalls.load());
    }
}
} // namespace VehicleMod
