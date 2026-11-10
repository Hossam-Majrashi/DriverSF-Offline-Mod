// Cap-only replacement for the removed v7.5.x torque/gearing modification.
// Native model/input ceilings + ability overspeed drag; selected HUD MPH target.
#include "speed_policy.h"
namespace SpeedMod {
static std::atomic<bool> enabled{false},ready{false};
static std::atomic<unsigned long> calls{0},matched{0},views{0};
static std::atomic<unsigned long> limitCalls{0},limitViews{0};
static std::atomic<bool> reported{false},reportedLimit{false},reportedBoost{false};
static std::atomic<unsigned long> boostViews{0};
static std::atomic<bool> modelPowerBand{false}; // separately opted-in engine mode
static std::atomic<int> targetMph{300}; // session-only; chosen before enabling
static SpeedPolicy::Target selectedTarget(){return SpeedPolicy::target(targetMph.load());}
static bool chooseTarget(int mph){
    if(enabled || modelPowerBand || !SpeedPolicy::validTarget(mph))return false;
    targetMph=mph;return true;
}
extern "C" {
void *originalBoostCapTarget=nullptr;
void boostCeilingHook();
}
static char speedStatus[160]="Ceiling only. Stock acceleration.";
using Update=void (__thiscall *)(void*,void*);
using Getter=BYTE* (__thiscall *)(void*);
using Limiter=void (__thiscall *)(void*,void*,void*);
static Update originalUpdate=nullptr;
static Getter originalTweaks=nullptr;
static Limiter originalLimiter=nullptr;
static BYTE *tweakCaller=nullptr;
struct Context {
    Context *previous;
    BYTE *vehicle,*adapter;
    alignas(4) BYTE copy[SpeedPolicy::viewSize];
};
// A native call consumes its stack-scoped snapshot synchronously. Thread-local
// nesting never overwrites another invocation's view. No persistent game pointer
// or changed coefficient requires restoration on OFF, Shift or car changes.
static thread_local Context *context=nullptr;
static bool currentAdapter(BYTE *v,void *self) {
    if(!v || v!=VehicleMod::playerVehicle() || !readable(self,0x2c4) ||
       *(void**)(v+0x53c)!=self || *(BYTE**)((BYTE*)self+0xc)!=v)return false;
    BYTE *physics=VehicleMod::vehiclePhysics(v);
    return physics && *(BYTE**)((BYTE*)self+8)==physics;
}
static bool currentLimiterEngine(void *self,void *model) {
    BYTE *v=VehicleMod::playerVehicle(),*engine=(BYTE*)self;
    BYTE *physics=VehicleMod::vehiclePhysics(v);
    if(!v || !physics || *(BYTE**)(physics+0x1150)!=engine || !readable(engine,0x1274))return false;
    BYTE *resource=*(BYTE**)(v+0x5b4);
    return readable(resource,0x310+SpeedPolicy::drivingViewSize) &&
        *(BYTE**)(physics+0xaa4)==resource && *(BYTE**)(engine+0x1270)==resource &&
        model==resource+0x310 && readable(model,SpeedPolicy::drivingViewSize);
}
static void __attribute__((noinline)) applyModelCeiling(void *self,void *model,void *output) {
    // Keep the 11 KiB snapshot frame off the OFF/BOT/invalid-state fast path.
    // The complete hash-checked consumer reads model+0x2be0 once, never retains
    // the model pointer, and writes only its normal native output. All other
    // readers of p160 (including steering/gear setup) still see original data.
    alignas(16) BYTE copy[SpeedPolicy::drivingViewSize];
    const auto target=selectedTarget();
    float requested=modelPowerBand?target.performance:target.model;
    if(!SpeedPolicy::modelCeilingView(model,copy,requested,true) || !enabled || !currentLimiterEngine(self,model)) {
        originalLimiter(self,model,output);return;
    }
    ++limitViews;
    if(!reportedLimit.exchange(true)) {
        float stock;memcpy(&stock,(BYTE*)model+SpeedPolicy::modelLimitOffset,sizeof(stock));
        char b[220];snprintf(b,sizeof(b),"%d MPH model limiter snapshot consumed: p160 %.3f -> %.3f native MPH; snapshot changes p160 only; not a measured speed",target.mph,stock,requested);logLine(b);
    }
    originalLimiter(self,copy,output);
}
static void __fastcall limiterHook(void *self,void*,void *model,void *output) {
    ++limitCalls;
    if(!ready || !enabled || !VehicleMod::ready || !currentLimiterEngine(self,model)) {
        originalLimiter(self,model,output);return;
    }
    applyModelCeiling(self,model,output);
}
static BYTE *selectTweaks(void *self,BYTE *normal,void *caller) {
    Context *frame=context;
    if(!ready || !enabled || !VehicleMod::ready || !frame || caller!=tweakCaller ||
       self!=frame->vehicle || normal!=frame->vehicle+0xb0 ||
       !currentAdapter(frame->vehicle,frame->adapter) || !readable(normal,SpeedPolicy::viewSize))return normal;
    if(!SpeedPolicy::ceilingView(normal,frame->copy,selectedTarget().ms,true))return normal;
    ++views;
    if(!reported.exchange(true)){char b[180];snprintf(b,sizeof(b),"%d MPH input-ceiling snapshot consumed; adds no power; not a measured speed",targetMph.load());logLine(b);}
    return frame->copy;
}
static BYTE* __attribute__((noinline)) __fastcall tweaksHook(void *self,void*) {
    BYTE *normal=originalTweaks(self);
    // Only the proven consumer at native update+0x0f receives our snapshot.
    // Other users of the same getter always receive the original object.
    return selectTweaks(self,normal,__builtin_return_address(0));
}
static void __fastcall updateHook(void *self,void*,void *input) {
    ++calls;
    BYTE *v=VehicleMod::playerVehicle();
    if(!ready || !enabled || !VehicleMod::ready || !currentAdapter(v,self)) {
        originalUpdate(self,input);return;
    }
    ++matched;
    Context frame{context,v,(BYTE*)self,{}};
    context=&frame;
    // Naturally scheduled native thiscall, ret 4. No forced engine step, no
    // artificial throttle/boost, and no writes to any native gameplay object.
    originalUpdate(self,input);
    context=frame.previous;
}
// The native ability update applies only OPPOSING force above this cap.
// XMM1 is already min(stock topSpeed, 71.5264 m/s) at the intercepted point.
// A private FXSAVE snapshot lets the original comparison use a higher threshold
// without touching native force gains, ability state, input or vehicle memory.
extern "C" void boostCeilingSnapshot(void *input,void *fxState) {
    if(!ready || !enabled || !VehicleMod::ready || !readable(input,0xa0))return;
    BYTE *v=*(BYTE**)((BYTE*)input+0x98);
    if(!v || v!=VehicleMod::playerVehicle() || !currentAdapter(v,*(BYTE**)(v+0x53c)))return;
    float gain,velocity;memcpy(&gain,(BYTE*)input+0x84,sizeof(gain));memcpy(&velocity,(BYTE*)input+0x80,sizeof(velocity));
    if(!std::isfinite(gain) || gain<0.0f || !std::isfinite(velocity))return;
    float native,cap;memcpy(&native,(BYTE*)fxState+176,sizeof(native));
    if(!SpeedPolicy::boostCeiling(native,cap,selectedTarget().ms,true))return;
    memcpy((BYTE*)fxState+176,&cap,sizeof(cap));++boostViews;
    if(!reportedBoost.exchange(true)){char b[190];snprintf(b,sizeof(b),"%d MPH ability overspeed-brake ceiling consumed; native thrust unchanged; not a measured speed",targetMph.load());logLine(b);}
}
static void bind() {
    if(!bound || !VehicleMod::ready){strcpy(speedStatus,"Speed ceiling is not ready.");return;}
    const unsigned char updatePattern[]={0x83,0xec,0x54,0x56,0x57,0x8b,0xf9,0x8b,0x4f,0x0c,0xe8,0x51,0x08,0xff,0xff,0xf3,0x0f,0x10,0x47,0x58};
    const unsigned char tweaksPattern[]={0x8d,0x81,0xb0,0,0,0,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x8a,0x81,0xe5,0x05,0,0};
    const unsigned char limiterPattern[]={0x83,0xec,0x10,0x8b,0x44,0x24,0x14,0x8b,0x90,0xe0,0x2b,0,0,0x0f,0x57,0xff,0x89,0x54,0x24,0x14};
    const unsigned char boostPattern[]={0x83,0xec,0x34,0x0f,0x57,0xc0,0x56,0x57,0x8b,0x7c,0x24,0x40,0x8d,0x44,0x24,0x0c};
    BYTE *boostUpdate=VehicleMod::uniqueCode(boostPattern,sizeof(boostPattern));
    BYTE *update=VehicleMod::uniqueCode(updatePattern,sizeof(updatePattern));
    BYTE *getter=VehicleMod::uniqueCode(tweaksPattern,sizeof(tweaksPattern));
    BYTE *limiter=VehicleMod::uniqueCode(limiterPattern,sizeof(limiterPattern));
    int32_t getterOffset=0;
    if(update && inside(update,0x0f))memcpy(&getterOffset,update+0x0b,sizeof(getterOffset));
    uint32_t boostFingerprint=2166136261u;
    if(boostUpdate && inside(boostUpdate,0x210))for(size_t i=0;i<0x210;i++)boostFingerprint=(boostFingerprint^boostUpdate[i])*16777619u;
    uint32_t limiterFingerprint=2166136261u;
    if(limiter && inside(limiter,0x20e))for(size_t i=0;i<0x20e;i++)limiterFingerprint=(limiterFingerprint^limiter[i])*16777619u;
    if(!update || !getter || !limiter || !boostUpdate || !inside(boostUpdate,0x210) ||
       boostFingerprint!=0x021e28a4u ||
       memcmp(boostUpdate+0xad,"\xf3\x0f\x10\x87\x80\x00\x00\x00\x0f\x2f\xc1",11) ||
       memcmp(boostUpdate+0x208,"\x5f\x5e\x83\xc4\x34\xc2\x04\x00",8) ||
       !inside(limiter,0x20e) || limiterFingerprint!=0x5c5ee4d1u ||
       memcmp(limiter+0x208,"\x83\xc4\x10\xc2\x08\x00",6) ||
       !inside(update,0x175) || !inside(getter,0x16) ||
       memcmp(update+0x16d,"\x5f\x5e\x83\xc4\x54\xc2\x04\x00",8) ||
       update+0x0f+getterOffset!=getter ||
       memcmp(update+0x107,"\x0f\x2f\x46\x1c",4)) {
        strcpy(speedStatus,"Speed ceiling signature check failed.");logLine(speedStatus);return;
    }
    void *targets[]={getter,update,limiter,boostUpdate+0xad};
    void *detours[]={(void*)tweaksHook,(void*)updateHook,(void*)limiterHook,(void*)boostCeilingHook};
    void **originals[]={(void**)&originalTweaks,(void**)&originalUpdate,(void**)&originalLimiter,&originalBoostCapTarget};
    constexpr int hookCount=4;
    int created=0;bool ok=true;
    for(;created<hookCount;created++)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK){ok=false;break;}
    if(ok)for(auto target:targets)if(MH_EnableHook(target)!=MH_OK){ok=false;break;}
    if(!ok) {
        for(int i=0;i<created;i++){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}
        originalTweaks=nullptr;originalUpdate=nullptr;originalLimiter=nullptr;originalBoostCapTarget=nullptr;
        strcpy(speedStatus,"Speed ceiling hooks unavailable.");logLine(speedStatus);return;
    }
    tweakCaller=update+0x0f;ready=true;
    strcpy(speedStatus,"Ceiling only. Stock acceleration.");
    char b[270];snprintf(b,sizeof(b),"Selected-speed cap-only bound: input RVA=%lx tweak-getter RVA=%lx model-limiter RVA=%lx ability-brake RVA=%lx; cap module does not hook engine-step; defaults OFF; acceptance pending",
        (unsigned long)(update-image),(unsigned long)(getter-image),(unsigned long)(limiter-image),(unsigned long)(boostUpdate+0xad-image));logLine(b);
}
static void setEnabled(bool on) {
    if(on && (!ready || !VehicleMod::ready))return;
    if(on==enabled.load())return;
    enabled=on;reported=false;reportedLimit=false;reportedBoost=false;
    char b[190];snprintf(b,sizeof(b),"%d MPH ceiling only %s; native thresholds delegated when OFF; no power/gearing/thrust changes or hard velocity clamp",targetMph.load(),on?"ON":"OFF");logLine(b);
}
static void disable(){setEnabled(false);}
static void draw() {
    if(!Ui::CollapsingHeader("Speed ceiling",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::BeginDisabled(!ready || modelPowerBand);
    bool on=enabled;
    if(Ui::Checkbox("Selected speed ceiling only - Player",&on))setEnabled(on);
    ImGui::EndDisabled();
    if(!modelPowerBand)Ui::TextDisabled("Stock acceleration / gearing. No extra power.");
    else Ui::TextDisabled("Ceiling only here; separate engine / gears option is ON.");
    Ui::TextWrapped("Sets selected model / input / ability-brake thresholds, not power. Stock gears or drag may limit speed.");
    if(!ready)Ui::TextWrapped("%s",speedStatus);
    else if(on && !VehicleMod::playerVehicle())Ui::TextDisabled("Enter a car and leave Shift.");
    if(showDiagnostics) {
        Ui::Text("Native calls / player / ceiling views: %lu / %lu / %lu",calls.load(),matched.load(),views.load());
        Ui::Text("Model limiter calls / ceiling views: %lu / %lu",limitCalls.load(),limitViews.load());
        Ui::Text("Ability overspeed-brake ceiling views: %lu",boostViews.load());
        Ui::Text("Native HUD ceiling: %.0f MPH | simulation threshold: %.3f m/s",float(targetMph.load()),selectedTarget().ms);
        Ui::TextDisabled("Ceiling module: no torque / gearing writes. See separate engine option.");
        Ui::TextWrapped("%s",speedStatus);
    }
}
} // namespace SpeedMod
