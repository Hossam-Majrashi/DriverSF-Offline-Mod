// v7.13 selected-target automatic player engine + true transmission-ratio tuning.
// All native wheel/suspension/steering/gear-state fields stay untouched by us.
#include "powertrain_policy.h"
namespace PowertrainMod {
static std::atomic<bool> enabled{false},ready{false},reported{false};
static std::atomic<uint32_t> generation{1};
static std::atomic<bool> needsRearm{false},autoFollowing{false},unsupportedTarget{false};
static std::atomic<unsigned long> calls{0},matched{0},applied{0},conflicts{0},busyCalls{0};
static std::atomic_flag inFlight=ATOMIC_FLAG_INIT;
static std::atomic<unsigned long> rejected{0},baselineChanges{0};
static void*lastRejectedEngine=nullptr; // comparison/log key only; accessed under inFlight
static char powerStatus[192]="Automatic per-car tuning. Park before switching OFF.";
using EngineUpdate=void (__thiscall *)(void*,void*,void*);
static EngineUpdate originalEngineUpdate=nullptr;
struct Progress {void *engine=nullptr,*model=nullptr;uint32_t generation=0;float clock=0,gain=1,motion=0;};
// Accessed only while holding inFlight. No saved gameplay fields/pointers to
// restore later: the two changed coefficients are restored before native return.
static Progress progress;
struct Engagement {void*engine=nullptr,*model=nullptr;uint32_t generation=0;PowertrainPolicy::Stock stock{};PowertrainPolicy::Setup plan{};BYTE*vehicle=nullptr;float stretch=1,lastTrace=0,peak=0,lastBaselineLog=0;unsigned loadedSamples=0,baselineUpdates=0;};
static Engagement engagement;
static void observe(){if(enabled && !VehicleMod::playerVehicle()){needsRearm=true;autoFollowing=false;unsupportedTarget=false;}}
static float getFloat(const BYTE*p){float f;memcpy(&f,p,4);return f;}
static bool current(void*self,void*model) {
    BYTE*v=VehicleMod::playerVehicle();
    return VehicleMod::ready && SpeedMod::ready && v &&
        SpeedMod::currentAdapter(v,*(BYTE**)(v+0x53c)) &&
        SpeedMod::currentLimiterEngine(self,model) && readable(self,0x1348) && readable(model,0x3250) &&
        VehicleMod::writable((BYTE*)self+0xe0,0xcc);
}
static bool stock(BYTE*engine,PowertrainPolicy::Stock&s) {
    const BYTE*g=engine+0xe0;
    s.torque=getFloat(g);s.overall=getFloat(g+0x48);s.radius=getFloat(g+0x44);
    s.idle=getFloat(g+4);s.redline=getFloat(g+8);memcpy(&s.gears,g+0xc,4);
    memcpy(s.ratios,g+0x10,sizeof(s.ratios));
    return true;
}
static float simulationClock() {
    BYTE*r=VehicleMod::root();
    if(!r)return __builtin_nanf("");
    BYTE*clock=*(BYTE**)(r+0x584);
    return readable(clock,0x10)?getFloat(clock+0xc):__builtin_nanf("");
}
static bool parked() {
    BYTE*v=VehicleMod::playerVehicle(),*p=VehicleMod::vehiclePhysics(v);
    if(!v || !p)return false;
    BYTE*e=*(BYTE**)(p+0x1150),*resource=*(BYTE**)(v+0x5b4);
    if(!current(e,resource+0x310))return false;
    float speed=getFloat(v+0x588),pedal=getFloat(e+0xc0);
    return PowertrainPolicy::range(speed,0,0.5f) && PowertrainPolicy::range(pedal,-0.01f,0.01f);
}
static void disable() {
    SpeedMod::modelPowerBand=false;autoFollowing=false;unsupportedTarget=false;SpeedMod::disable();
    if(enabled.exchange(false)){++generation;reported=false;logLine("Selected-speed engine/gears OFF; next native step uses stock coefficients; park before toggling at overspeed");}
}
static void setEnabled(bool on) {
    if(!on){disable();return;}
    if(enabled || !ready)return;
    if(!VehicleMod::ready || !SpeedMod::ready)return;
    SpeedMod::setEnabled(true);SpeedMod::modelPowerBand=true;++generation;reported=false;needsRearm=true;autoFollowing=false;enabled=true;
    snprintf(powerStatus,sizeof(powerStatus),"Automatic %d-MPH engine/gears ON; follows current car/truck.",SpeedMod::targetMph.load());
    char b[260];snprintf(b,sizeof(b),"%d MPH automatic ON: follows player without a parking/re-enable gate; gradual native engine/gearing; NOT a hard speed clamp or measured HUD speed",SpeedMod::targetMph.load());logLine(b);
}
static bool restore(BYTE*field,float written,float before) {
    if(!VehicleMod::writable(field,4) || memcmp(field,&written,4))return false;
    memcpy(field,&before,4);return true;
}
static void __fastcall engineUpdateHook(void*self,void*,void*model,void*output) {
    ++calls;observe();
    if(!ready || !enabled || !current(self,model) || output!=(BYTE*)self+0x1190 ||
       !VehicleMod::writable(output,0xe0)) {originalEngineUpdate(self,model,output);return;}
    ++matched;
    if(inFlight.test_and_set(std::memory_order_acquire)) {
        ++busyCalls;originalEngineUpdate(self,model,output);return;
    }
    struct Release {~Release(){inFlight.clear(std::memory_order_release);}}release;
    BYTE*e=(BYTE*)self,*v=VehicleMod::playerVehicle(),*g=e+0xe0;
    if(!v || !readable(v,0x5f4)){progress={};originalEngineUpdate(self,model,output);return;}
    PowertrainPolicy::Stock s{};PowertrainPolicy::Setup plan{};stock(e,s);
    int gear;memcpy(&gear,g+0x98,4);
    float speed=getFloat(v+0x588),pedal=getFloat(e+0xc0),throttle=getFloat(e+0xc8),brake=getFloat(e+0xcc);
    bool handbrake=e[0xd0]!=0,shifting=g[0x8d] || g[0xc4];
    float wx=getFloat(e+0x50),wy=getFloat(e+0x54),wz=getFloat(e+0x58);
    const auto target=SpeedMod::selectedTarget();
    bool configured=PowertrainPolicy::configure(s,plan,target.ms);unsupportedTarget=!configured;
    if(!configured || gear<0 || gear>s.gears ||
       !PowertrainPolicy::range(speed,0,1000) || !PowertrainPolicy::range(pedal,0,1) ||
       !PowertrainPolicy::range(throttle,0,1) || !PowertrainPolicy::range(brake,0,1) ||
       !PowertrainPolicy::range(wx,-100,100) || !PowertrainPolicy::range(wy,-100,100) || !PowertrainPolicy::range(wz,-100,100)) {
        if(!configured || gear>s.gears || !std::isfinite(speed) || !std::isfinite(pedal) || !std::isfinite(throttle) || !std::isfinite(brake) || !std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz)) {
            ++rejected;
            if(lastRejectedEngine!=self) {
                lastRejectedEngine=self;char b[420];snprintf(b,sizeof(b),"Speed auto rejected: target_mph=%d car=%p engine=%p config=%u gear=%d/%d torque=%.1f radius=%.3f overall=%.5f idle=%.1f redline=%.1f; native drivetrain retained, no reachability claim",target.mph,v,e,unsigned(configured),gear,s.gears,s.torque,s.radius,s.overall,s.idle,s.redline);logLine(b);
            }
        }
        autoFollowing=false;engagement={};progress={};originalEngineUpdate(self,model,output);return;
    }
    lastRejectedEngine=nullptr;
    uint32_t gen=generation.load();
    if(needsRearm.exchange(false))engagement={};
    if(engagement.engine!=self || engagement.model!=model || engagement.vehicle!=v || engagement.generation!=gen) {
        progress={};engagement={self,model,gen,s,plan,v};autoFollowing=true;reported=false;
    } else if(uint32_t changed=PowertrainPolicy::changes(engagement.stock,s)) {
        float rebased;
        if(!PowertrainPolicy::rebaseStretch(engagement.stock,engagement.stretch,s,rebased)) {
            // New native baseline cannot safely represent our previous gearing.
            // Delegate, do not force an out-of-bounds ratio or silently turn OFF.
            ++rejected;autoFollowing=false;engagement={};progress={};
            logLine("Speed auto rejected: changed native baseline cannot retain bounded gearing; native driving retained; automatic reacquisition pending");
            originalEngineUpdate(self,model,output);return;
        }
        float now=simulationClock();++baselineChanges;++engagement.baselineUpdates;
        if(engagement.baselineUpdates==1 || (std::isfinite(now) && now-engagement.lastBaselineLog>=2)) {
            engagement.lastBaselineLog=now;char b[400];
            snprintf(b,sizeof(b),"Speed auto native baseline refreshed: fields=0x%02x torque=%.1f->%.1f overall=%.6f->%.6f redline=%.1f->%.1f updates=%u; bounded gearing retained; NOT a restoration conflict",unsigned(changed),engagement.stock.torque,s.torque,engagement.stock.overall,s.overall,engagement.stock.redline,s.redline,engagement.baselineUpdates);logLine(b);
        }
        engagement.stock=s;engagement.plan=plan;engagement.stretch=rebased;
    } else plan=engagement.plan;
    float now=simulationClock(),dt=0;
    float angular=std::sqrt(wx*wx+wy*wy+wz*wz);
    if(gear==0) {
        // Native neutral/clutch transition: no coefficient edits, but do not
        // throw away this same vehicle's already-engaged forward gearing.
        progress={self,model,gen,now,1,angular};
        originalEngineUpdate(self,model,output);return;
    }
    if(progress.engine==self && progress.model==model && progress.generation==gen &&
       std::isfinite(now) && std::isfinite(progress.clock) && now>=progress.clock && now-progress.clock<=0.10001f)dt=now-progress.clock;
    else progress={self,model,gen,now,1,angular};
    progress.motion=PowertrainPolicy::filteredMotion(progress.motion,angular,dt);
    float motion=angular>=PowertrainPolicy::immediateMotionCut?angular:progress.motion;
    bool stopped=speed<=0.5f && pedal<=0.01f;
    bool hold=shifting || handbrake || brake>0.01f || throttle<0.8f || motion>PowertrainPolicy::gearingHoldMotion;
    engagement.stretch=PowertrainPolicy::rampStretch(engagement.stretch,engagement.plan.stretch,dt,stopped,hold);
    plan.stretch=engagement.stretch;plan.overall=s.overall*plan.stretch;
    float wanted=PowertrainPolicy::requestedGain(engagement.plan,speed,throttle,brake,handbrake,motion,shifting,gear);
    progress.gain=PowertrainPolicy::rateLimit(progress.gain,wanted,dt);progress.clock=now;
    float torque;
    if(!PowertrainPolicy::coefficients(s,plan,progress.gain,throttle,brake,handbrake,torque) ||
       !enabled || gen!=generation.load() || !current(self,model)) {
        originalEngineUpdate(self,model,output);return;
    }
    // This is the real p361-derived overall ratio (+0x48), NOT radius +0x44.
    // Uniform across every gear; frozen in native shifts/braking/large motion.
    // Mild course corrections no longer endlessly starve the gearing/power ramp.
    // All adjacent native ratio spacing is retained, even while auto-engaging.
    // Only naturally scheduled native engine updates run. No forced gear/RPM,
    // throttle, velocity, position or boost input, and no shared model writes.
    memcpy(g,&torque,4);memcpy(g+0x48,&plan.overall,4);++applied;
    if(!reported.exchange(true)) {
        char b[280];snprintf(b,sizeof(b),"%d MPH auto attached: car=%p engine=%p p160=%.2f; transmission x%.3f/%.3f; torque %.1f -> %.1f; radius %.3f unchanged; not a measured speed",target.mph,v,e,getFloat((BYTE*)model+SpeedPolicy::modelLimitOffset),plan.stretch,engagement.plan.stretch,s.torque,torque,s.radius);logLine(b);
    }
    originalEngineUpdate(self,model,output);
    // Restore this original engine even if ownership changes during its native
    // call. Never overwrite a conflicting coefficient written by another system.
    bool identity=readable(e,0x1274) && *(BYTE**)(e+0x1270)==(BYTE*)model-0x310;
    bool torqueOK=identity && restore(g,torque,s.torque);
    bool ratioOK=identity && restore(g+0x48,plan.overall,s.overall);
    if(!torqueOK || !ratioOK) {
        ++conflicts;disable();progress={};
        logLine("Engine/gears restoration conflict; tuning disabled, conflicting fields preserved. Stop testing; inspect diagnostics.");
    } else if(dt>0 && throttle>=0.95f && pedal>=0.95f && brake<=0.01f && !handbrake) {
        // Read-only trace of naturally advancing, loaded native updates. No
        // extra game calls/polling/input. Speed is simulation m/s, NOT HUD MPH.
        ++engagement.loadedSamples;engagement.peak=std::fmax(engagement.peak,speed);
        if(engagement.loadedSamples==1 || now-engagement.lastTrace>=2.0f) {
            engagement.lastTrace=now;int nativeGear;memcpy(&nativeGear,g+0x98,4);
            char b[720];snprintf(b,sizeof(b),"Speed auto loaded sample: target_mph=%d car=%p engine=%p clock=%.3f dt=%.4f speed_ms=%.3f peak_ms=%.3f throttle=%.3f brake=%.3f gear=%d/%d rpm=%.1f/%.1f ratio_x=%.4f/%.4f gain=%.3f torque=%.1f/%.1f angular=%.3f motion=%.3f wanted=%.3f hold=%u shift=%u/%u limiter=%.3f loaded=%u; simulation telemetry NOT rendered HUD",target.mph,v,e,now,dt,speed,engagement.peak,throttle,brake,nativeGear,s.gears,getFloat(g+0x94),s.redline,plan.stretch,engagement.plan.stretch,progress.gain,torque,s.torque,angular,motion,wanted,unsigned(hold),unsigned(g[0x8d]),unsigned(g[0xc4]),getFloat(e+0xd88),engagement.loadedSamples);logLine(b);
        }
    }
}
static void bind() {
    if(!bound || !SpeedMod::ready || !VehicleMod::ready){strcpy(powerStatus,"Engine/gears unavailable.");return;}
    const BYTE enginePattern[]={0x83,0xec,0x38,0x53,0x55,0x56,0x8b,0xf1,0x80,0xbe,0xfc,0x0c,0,0,0,0x57,0xc6,0x86,0x84,0x11,0,0,0};
    BYTE*target=VehicleMod::uniqueCode(enginePattern,sizeof(enginePattern));uint32_t h=2166136261u;
    if(target && inside(target,0x881))for(size_t i=0;i<0x881;i++)h=(h^target[i])*16777619u;
    if(!target || !inside(target,0x881) || h!=0x1ee6b61bu ||
       memcmp(target+0x877,"\x5f\x5e\x5d\x5b\x83\xc4\x38\xc2\x08\x00",10)) {
        strcpy(powerStatus,"Engine/gears native signature check failed.");logLine(powerStatus);return;
    }
    if(MH_CreateHook(target,(void*)engineUpdateHook,(void**)&originalEngineUpdate)!=MH_OK){strcpy(powerStatus,"Engine/gears hook unavailable.");return;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalEngineUpdate=nullptr;strcpy(powerStatus,"Engine/gears hook enable failed.");return;}
    ready=true;logLine("Selected-speed optional engine/gears bound; defaults OFF; only torque and true p361 ratio may change during native player engine call; radius NOT modified");
}
static void draw() {
    if(!Ui::CollapsingHeader("Speed",ImGuiTreeNodeFlags_DefaultOpen))return;
    bool on=enabled;bool stopped=parked();
    ImGui::BeginDisabled(!ready || (on && !stopped));
    if(Ui::Checkbox("Automatic speed - Player",&on)) {
        if(on)ImGui::OpenPopup("SpeedTargetPicker");else setEnabled(false);
    }
    ImGui::EndDisabled();
    if(ImGui::BeginPopup("SpeedTargetPicker")) {
        Ui::TextWrapped("Choose maximum speed (MPH)");
        Ui::TextDisabled("Target only. 400 / 500 MPH are experimental.");
        ImGui::BeginDisabled(!ready || enabled || SpeedMod::enabled);
        for(int mph:SpeedPolicy::choices) {
            char label[24];snprintf(label,sizeof(label),"%d MPH",mph);
            if(ImGui::Selectable(label)) {
                if(SpeedMod::chooseTarget(mph))setEnabled(true);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        if(SpeedMod::enabled&&!enabled)Ui::TextWrapped("Turn the technical ceiling OFF before choosing a target.");
        ImGui::EndPopup();
    }
    on=enabled.load();
    if(on)Ui::Text("Selected maximum: %d MPH",SpeedMod::targetMph.load());
    Ui::TextDisabled("Auto-follows cars / trucks. Gradual power. Park to switch OFF.");
    if(on && !autoFollowing) {
        if(unsupportedTarget)Ui::TextWrapped("This vehicle cannot use this target within tuning bounds. Native drivetrain retained.");
        else Ui::TextDisabled("Waiting for a valid player vehicle, not for parking.");
    }
    if(on && !SpeedMod::enabled)Ui::TextWrapped("Speed ceiling is OFF; original caps can still restrict speed.");
    if(showDiagnostics) {
        Ui::Text("Engine calls / player / tuned: %lu / %lu / %lu",calls.load(),matched.load(),applied.load());
        Ui::Text("Restoration conflicts / busy / rejected: %lu / %lu / %lu",conflicts.load(),busyCalls.load(),rejected.load());
        Ui::TextDisabled("Loaded native samples are logged every 2 simulation seconds.");
        Ui::TextDisabled("True overall ratio, not wheel radius. Extra power reduces while turning/braking.");
        if(on)Ui::TextWrapped("%s",powerStatus);
        else Ui::TextDisabled("Automatic tuning OFF.");
    }
}
} // namespace PowertrainMod
