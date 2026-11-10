// Real 32-bit calling convention/production-hook tests; mocked engine, not gameplay.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;
struct Region{BYTE*p;size_t n;};static std::vector<Region>regions;
static bool bound=true,writes=true;static BYTE*attached;
static bool readable(const void*p,size_t n){for(auto r:regions)if(uintptr_t(p)>=uintptr_t(r.p)&&uintptr_t(p)+n>=uintptr_t(p)&&uintptr_t(p)+n<=uintptr_t(r.p)+r.n)return true;return false;}
static std::vector<BYTE>nativeCode;static BYTE*image=nullptr;
static bool inside(const void*p,size_t n){return image&&uintptr_t(p)>=uintptr_t(image)&&uintptr_t(p)+n<=uintptr_t(image)+nativeCode.size();}
static std::vector<std::string>logs;
static void logLine(const char*s){logs.emplace_back(s);}
static constexpr int MH_OK=0;
static int createResult=0,enableResult=0,creates=0,removes=0;
static void*nativeFunction=nullptr;
static int MH_CreateHook(void*,void*,void**out){++creates;*out=nativeFunction;return createResult;}
static int MH_EnableHook(void*){return enableResult;}
static int MH_DisableHook(void*){return 0;}
static int MH_RemoveHook(void*){++removes;return 0;}
namespace VehicleMod{
static std::atomic<bool>ready{true};
static BYTE*root();
static bool writable(const void*p,size_t n){return writes&&readable(p,n);}
static BYTE*playerVehicle(){return attached;}
static BYTE*vehiclePhysics(BYTE*v){
    if(!readable(v,0x5f4)||*(int*)(v+0x544)!=3)return nullptr;
    BYTE*a=*(BYTE**)(v+0x53c);if(!readable(a,0x10))return nullptr;
    BYTE*p=*(BYTE**)(a+8);return readable(p,0x1154)&&*(BYTE**)(p+0xa50)==v?p:nullptr;
}
static BYTE*uniqueCode(const BYTE*b,size_t n){return image&&nativeCode.size()>=n&&!memcmp(image,b,n)?image:nullptr;}
}
// Compile the real speed ownership/snapshot guards too, not a shared false stub.
#include "speed_hook_under_test.h"
#include "powertrain_hook_under_test.h"
alignas(16)static BYTE car[0x600],bot[0x600],adapter[0x300],botAdapter[0x300],physics[0x1200],botPhysics[0x1200],
    engine[0x1400],botEngine[0x1400],resource[0x4000],rootBytes[0x700],clockBytes[0x20];
namespace VehicleMod{static BYTE*root(){return rootBytes;}}
extern "C"{void*powertrain_test_entry=nullptr;}
static void __attribute__((naked,thiscall)) nativeCall(void*,void*,void*){__asm__ volatile("jmp *powertrain_test_entry");}
static float F(const BYTE*p){float f;memcpy(&f,p,4);return f;}
static void put(BYTE*p,float f){memcpy(p,&f,4);}
static bool same(const BYTE*p,float f){return memcmp(p,&f,4)==0;} // round expected float to its native 32-bit bits
static int scenario=0;
static bool expectRamp=false;
static unsigned nativeCalls=0;static bool expectTune=false,turnOff=false,switchCar=false,nestBot=false,nestedSame=false,
    conflictTorque=false,conflictRatio=false,changeIdentity=false,nativeShift=false;
static PowertrainPolicy::Stock stockConfig;static PowertrainPolicy::Setup setupConfig;
static float seenTorque=0,seenRatio=0;
static void __thiscall nativeEngine(void*self,void*model,void*output){
    ++nativeCalls;BYTE*e=(BYTE*)self,*g=e+0xe0;
    if(self==botEngine && attached!=bot){assert(same(g,700)&&same(g+0x48,0.28169f));return;}
    assert(self==engine || self==botEngine);seenTorque=F(g);seenRatio=F(g+0x48);
    assert(same(g+0x44,stockConfig.radius));assert(same(g+8,stockConfig.redline));
    assert(*(int*)(g+0xc)==stockConfig.gears);assert(!memcmp(g+0x10,stockConfig.ratios,sizeof(stockConfig.ratios)));
    if(expectTune){
        if(expectRamp){assert(seenRatio>=stockConfig.overall-0.000001f&&seenRatio<=setupConfig.overall+0.000001f);}
        else assert(seenRatio==setupConfig.overall);
        assert(seenTorque>=stockConfig.torque);
    }
    else{assert(seenRatio==stockConfig.overall);assert(seenTorque==stockConfig.torque);}
    if(output==e+0x1190)*(uint32_t*)output=0x1234abcd; // native result must survive restoration
    if(turnOff){turnOff=false;PowertrainMod::disable();}
    if(nestBot){nestBot=false;nativeCall(botEngine,resource+0x310,botEngine+0x1190);}
    if(nestedSame){nestedSame=false;nativeCall(engine,model,output);}
    if(switchCar){switchCar=false;attached=bot;}
    if(conflictTorque)put(g,777);
    if(conflictRatio)put(g+0x48,0.99f);
    if(changeIdentity)*(BYTE**)(engine+0x1270)=resource+4;
    if(nativeShift){*(int*)(g+0x98)=4;put(g+0x94,2345);g[0x8d]=1;}
}
static void setup(){
    ++scenario;
    for(auto r:regions)if(r.p!=image)memset(r.p,0,r.n);
    *(int*)(car+0x544)=*(int*)(bot+0x544)=3;
    *(BYTE**)(car+0x53c)=adapter;*(BYTE**)(bot+0x53c)=botAdapter;
    *(BYTE**)(adapter+8)=physics;*(BYTE**)(botAdapter+8)=botPhysics;
    *(BYTE**)(adapter+0xc)=car;*(BYTE**)(botAdapter+0xc)=bot;
    *(BYTE**)(physics+0xa50)=car;*(BYTE**)(botPhysics+0xa50)=bot;
    *(BYTE**)(physics+0x1150)=engine;*(BYTE**)(botPhysics+0x1150)=botEngine;
    *(BYTE**)(car+0x5b4)=*(BYTE**)(bot+0x5b4)=resource;
    *(BYTE**)(physics+0xaa4)=*(BYTE**)(botPhysics+0xaa4)=resource;
    *(BYTE**)(engine+0x1270)=*(BYTE**)(botEngine+0x1270)=resource;
    *(BYTE**)(rootBytes+0x584)=clockBytes;put(clockBytes+0xc,10);
    for(BYTE*e:{engine,botEngine}){
        BYTE*g=e+0xe0;put(g,700);put(g+4,550);put(g+8,6500);*(int*)(g+0xc)=4;
        const float ratios[]={-0.440529f,0.295858f,0.551078f,0.766564f,0.946724f,1,1};
        memcpy(g+0x10,ratios,sizeof ratios);put(g+0x44,0.33f);put(g+0x48,0.28169f);*(int*)(g+0x98)=1;
    }
    attached=car;writes=true;VehicleMod::ready=true;SpeedMod::ready=true;SpeedMod::enabled=false;SpeedMod::modelPowerBand=false;
    SpeedMod::targetMph=300;PowertrainMod::unsupportedTarget=false;
    PowertrainMod::ready=true;PowertrainMod::enabled=false;PowertrainMod::reported=false;
    PowertrainMod::progress={};PowertrainMod::engagement={};PowertrainMod::generation=1;PowertrainMod::needsRearm=false;PowertrainMod::autoFollowing=false;PowertrainMod::inFlight.clear();
    PowertrainMod::calls=0;PowertrainMod::matched=0;PowertrainMod::applied=0;PowertrainMod::conflicts=0;PowertrainMod::busyCalls=0;PowertrainMod::rejected=0;PowertrainMod::baselineChanges=0;PowertrainMod::lastRejectedEngine=nullptr;
    PowertrainMod::originalEngineUpdate=nativeEngine;
    PowertrainMod::stock(engine,stockConfig);assert(PowertrainPolicy::configure(stockConfig,setupConfig));
    logs.clear();nativeCalls=0;expectTune=false;expectRamp=false;turnOff=false;switchCar=false;nestBot=false;nestedSame=false;
    conflictTorque=false;conflictRatio=false;changeIdentity=false;nativeShift=false;
}
static void enable(){
    PowertrainMod::setEnabled(true);assert(PowertrainMod::enabled&&SpeedMod::enabled&&SpeedMod::modelPowerBand);expectTune=true;
    nativeCall(engine,resource+0x310,engine+0x1190); // engage parked BEFORE driving
    assert(PowertrainMod::autoFollowing);nativeCalls=0;PowertrainMod::calls=0;PowertrainMod::matched=0;PowertrainMod::applied=0;
}
static void checkRestored(){assert(same(engine+0xe0,stockConfig.torque)&&same(engine+0xe0+0x48,stockConfig.overall));assert(same(engine+0xe0+0x44,stockConfig.radius));}
static void call(void*model=resource+0x310,void*out=engine+0x1190){nativeCall(engine,model,out);}
static void purePolicy(){
    using namespace PowertrainPolicy;
    // Sweep broad synthetic stock configurations, including buses/trucks. This
    // checks maths/numerics, not actual installed-model or gameplay coverage.
    for(int gears=1;gears<=6;gears++)for(float radius:{0.25f,0.35f,0.6f})for(float rpm:{2200.0f,4500.0f,7500.0f}){
        Stock s{700,0.35f,radius,500,rpm,{-0.3f,0.25f,0.4f,0.6f,0.8f,1,1.2f},gears};Setup p{};
        bool valid=configure(s,p);float theoretical=rpm*s.ratios[gears]*radius*s.overall*rpmToRadians;
        if(theoretical<SpeedPolicy::ceilingMs/(redlineFractionAt300*maxStretch)){assert(!valid);continue;}
        assert(valid);float tuned=rpm*s.ratios[gears]*radius*p.overall*rpmToRadians;
        assert(tuned*redlineFractionAt300>=SpeedPolicy::ceilingMs-0.0001f);
        assert(p.stretch>=1&&p.stretch<=maxStretch&&p.maxGain>=2&&p.maxGain<=maxWheelGain);
        // Exact adjacent-gear RPM drop ratios remain unchanged by fixed stretch.
        for(int i=1;i<gears;i++)assert(std::fabs((s.ratios[i]*p.overall)/(s.ratios[i+1]*p.overall)-s.ratios[i]/s.ratios[i+1])<0.00001f);
        // At low speed/throttle the baseline torque compensation gives the same
        // ideal steady-state wheel-force coefficient; not a suspension simulation.
        float t;assert(coefficients(s,p,1,1,0,false,t));
        assert(std::fabs(t/p.overall-s.torque/s.overall)<0.02f);
        float before=1;
        for(int n=0;n<1000;n++){float after=rateLimit(before,p.maxGain,0.04f);assert(after-before<=0.01001f);assert(after>=before);before=after;}
        assert(requestedGain(p,0,1,0,false,0,false,1)==1);
        assert(requestedGain(p,p.stockTopMs*2,1,0,false,powerFadeEnd,false,gears)==1);
        assert(requestedGain(p,p.stockTopMs*2,1,0,false,0.4f,false,gears)>1);
        assert(requestedGain(p,p.stockTopMs*2,1,0,false,0,true,gears)==1);
        assert(requestedGain(p,p.stockTopMs*2,1,0.1f,false,0,false,gears)==1);
        assert(requestedGain(p,p.stockTopMs*2,1,0,true,0,false,gears)==1);
        assert(requestedGain(p,p.stockTopMs*2,1,0,false,0,false,0)==1);
        assert(requestedGain(p,p.stockTopMs*2,0.5f,0,false,0,false,gears)==1);
        assert(coefficients(s,p,8,0,0,false,t)&&t==s.torque);
    }
    assert(rateLimit(8,16,3)==1);assert(rateLimit(8,16,-1)==1);assert(rateLimit(8,1,0.04f)==1);
    Stock bad=stockConfig;Setup p{};
    bad.radius=__builtin_nanf("");assert(!configure(bad,p));bad=stockConfig;bad.gears=7;assert(!configure(bad,p));
    bad=stockConfig;bad.ratios[2]=bad.ratios[1];assert(!configure(bad,p));bad=stockConfig;bad.torque=-1;assert(!configure(bad,p));
    BYTE model[SpeedPolicy::drivingViewSize],copy[SpeedPolicy::drivingViewSize];memset(model,0x37,sizeof model);
    put(model+SpeedPolicy::modelLimitOffset,145.7f);
    assert(SpeedPolicy::modelCeilingView(model,copy,SpeedPolicy::performanceLimitMph));
    assert(same(copy+SpeedPolicy::modelLimitOffset,SpeedPolicy::performanceLimitMph));
    for(size_t i=0;i<sizeof model;i++)if(i<SpeedPolicy::modelLimitOffset||i>=SpeedPolicy::modelLimitOffset+4)assert(copy[i]==model[i]);
    assert(!SpeedPolicy::modelCeilingView(model,copy,__builtin_nanf("")));
    assert(!SpeedPolicy::modelCeilingView(model,copy,1001));
    put(model+SpeedPolicy::modelLimitOffset,400);assert(!SpeedPolicy::modelCeilingView(model,copy,SpeedPolicy::performanceLimitMph));
    assert(smooth(0)==0&&smooth(1)==1);float previous=0;
    for(int i=0;i<=1000;i++){float v=smooth(float(i)/1000);assert(v>=previous-1e-6f);previous=v;}
}
static void integration(){
    setup();call();assert(PowertrainMod::applied==0);checkRestored();
    setup();put(car+0x588,2);PowertrainMod::setEnabled(true);assert(PowertrainMod::enabled);
    expectTune=true;expectRamp=true;call();checkRestored();assert(seenRatio==stockConfig.overall && PowertrainMod::applied==1);
    setup();put(engine+0xc0,0.5f);PowertrainMod::setEnabled(true);assert(PowertrainMod::enabled);
    expectTune=true;expectRamp=true;call();checkRestored();assert(PowertrainMod::applied==1);
    setup();enable();
    std::vector<BYTE>savedModel(resource,resource+sizeof resource),savedCar(car,car+sizeof car),savedBot(botEngine,botEngine+sizeof botEngine);
    put(car+0x588,setupConfig.stockTopMs*2);put(engine+0xc0,1);put(engine+0xc8,1);
    float last=1;
    for(int n=0;n<10000;n++){
        put(clockBytes+0xc,10+n*0.04f);call();checkRestored();
        assert(PowertrainMod::progress.gain-last<=0.0101f);last=PowertrainMod::progress.gain;
        assert(last>=1&&last<=setupConfig.maxGain);
        assert(*(uint32_t*)(engine+0x1190)==0x1234abcd);
    }
    assert(nativeCalls==10000&&PowertrainMod::applied==10000);
    assert(last==setupConfig.maxGain);assert(!memcmp(resource,savedModel.data(),sizeof resource));
    assert(!memcmp(botEngine,savedBot.data(),sizeof botEngine));
    for(size_t i=0;i<sizeof car;i++)if(i<0x588||i>=0x58c)assert(car[i]==savedCar[i]);
    // No engine writes beyond the two scoped coefficients plus deliberate mock
    // native output. Whole engine compares in a one-call baseline below.
    setup();enable();std::vector<BYTE>before(engine,engine+sizeof engine);call();checkRestored();
    for(size_t i=0;i<sizeof engine;i++)if(i<0x1190||i>=0x1194)assert(engine[i]==before[i]);
    setup();enable();nativeShift=true;call();checkRestored();assert(*(int*)(engine+0xe0+0x98)==4&&F(engine+0xe0+0x94)==2345&&engine[0xe0+0x8d]==1);
    setup();enable();turnOff=true;call();checkRestored();assert(!PowertrainMod::enabled&&!SpeedMod::modelPowerBand);
    setup();enable();switchCar=true;call();checkRestored();expectTune=false;call();assert(PowertrainMod::applied==1);
    setup();enable();nestBot=true;call();checkRestored();assert(nativeCalls==2&&PowertrainMod::applied==1);
    setup();enable();nestedSame=true;call();checkRestored();assert(nativeCalls==2&&PowertrainMod::applied==1&&PowertrainMod::busyCalls==1);
    for(int guard=0;guard<12;guard++){
        setup();enable();expectTune=false;
        switch(guard){case 0:PowertrainMod::ready=false;break;case 1:VehicleMod::ready=false;break;
        case 2:attached=nullptr;break;case 3:attached=bot;break;case 4:*(BYTE**)(engine+0x1270)=resource+4;break;
        case 5:*(BYTE**)(physics+0xaa4)=resource+4;break;case 6:writes=false;break;
        case 7:*(int*)(engine+0xe0+0x98)=0;break;case 8:put(engine+0xc0,-1);break;
        case 9:*(BYTE**)(adapter+0xc)=bot;break;case 10:*(BYTE**)(physics+0xa50)=bot;break;case 11:SpeedMod::ready=false;break;}
        call();checkRestored();assert(PowertrainMod::applied==0);
    }
    setup();enable();expectTune=false;call(resource);assert(PowertrainMod::applied==0);
    setup();enable();expectTune=false;call(resource+0x310,engine+0x1194);assert(PowertrainMod::applied==0);
    setup();enable();attached=nullptr;PowertrainMod::observe();attached=car;
    put(car+0x588,30);put(engine+0xc0,1);expectTune=true;expectRamp=true;call();checkRestored();assert(PowertrainMod::autoFollowing&&PowertrainMod::applied==1&&seenRatio==stockConfig.overall);
    put(car+0x588,0);put(engine+0xc0,0);expectRamp=false;call();checkRestored();assert(PowertrainMod::autoFollowing&&PowertrainMod::applied==2);
    // Reused engine/model address with a different current vehicle must NOT
    // inherit previous gearing; it gets a fresh STOCK starting ratio in motion.
    setup();enable();attached=bot;*(BYTE**)(botPhysics+0x1150)=engine;put(bot+0x588,30);expectRamp=true;
    call();checkRestored();assert(PowertrainMod::autoFollowing&&PowertrainMod::applied==1&&seenRatio==stockConfig.overall);
    setup();enable();put(engine+0xe0+8,6400);stockConfig.redline=6400;
    assert(PowertrainPolicy::configure(stockConfig,setupConfig));expectRamp=true;call();
    assert(PowertrainMod::enabled&&PowertrainMod::applied==1&&PowertrainMod::baselineChanges==1);checkRestored();
    setup();enable();conflictTorque=true;call();assert(F(engine+0xe0)==777&&F(engine+0xe0+0x48)==stockConfig.overall);
    assert(!PowertrainMod::enabled&&PowertrainMod::conflicts==1);
    setup();enable();conflictRatio=true;call();assert(F(engine+0xe0)==700&&same(engine+0xe0+0x48,0.99f));
    assert(!PowertrainMod::enabled&&PowertrainMod::conflicts==1);
    setup();enable();changeIdentity=true;call();assert(!PowertrainMod::enabled&&PowertrainMod::conflicts==1);
    setup();enable();put(car+0x588,setupConfig.stockTopMs*2);put(engine+0xc8,1);put(engine+0xc0,1);
    for(int n=0;n<100;n++){put(clockBytes+0xc,10+n*0.04f);call();}assert(PowertrainMod::progress.gain>1);
    put(clockBytes+0xc,99);call();assert(PowertrainMod::progress.gain==1);checkRestored();
    PowertrainMod::progress.gain=5;put(engine+0x54,2.5f);put(clockBytes+0xc,99.04f);call();assert(PowertrainMod::progress.gain==1);checkRestored();
    PowertrainMod::progress.gain=5;put(engine+0x54,0);put(engine+0xcc,0.1f);call();assert(PowertrainMod::progress.gain==1&&seenTorque==700);checkRestored();
    PowertrainMod::progress.gain=5;put(engine+0xcc,0);engine[0xe0+0xc4]=1;call();assert(PowertrainMod::progress.gain==1);checkRestored();
}
static unsigned traceCount(){unsigned n=0;for(auto&s:logs)if(s.find("Speed auto loaded sample:")==0)++n;return n;}
static void automaticFollowing(){
    using namespace PowertrainPolicy;
    // No parked warm-up: engage the production detour at an already moving
    // full-throttle vehicle. First coefficients MUST remain native/stock.
    setup();put(car+0x588,40);put(engine+0xc0,1);put(engine+0xc8,1);
    PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;call();checkRestored();
    assert(PowertrainMod::engagement.stretch==1&&seenRatio==stockConfig.overall&&seenTorque==stockConfig.torque);
    assert(traceCount()==0);float before=1;
    for(int n=1;n<=1500;n++){
        put(clockBytes+0xc,10+n*0.04f);call();checkRestored();
        float after=PowertrainMod::engagement.stretch;
        assert(after>=before&&after<=before*1.00321f+0.00001f&&after<=setupConfig.stretch);
        before=after;
    }
    assert(before==setupConfig.stretch&&traceCount()>10&&traceCount()<35);
    assert(logs.back().find("rendered HUD")!=std::string::npos);
    // Every moving car switch auto-engages immediately, at a fresh factor 1,
    // without a second button/parking requirement. Former player/BOT untouched.
    for(int n=0;n<100;n++){
        bool second=n%2==0;attached=second?bot:car;BYTE*e=second?botEngine:engine;
        BYTE*v=second?bot:car;put(v+0x588,40);put(e+0xc0,1);put(e+0xc8,1);
        std::vector<BYTE>other((second?engine:botEngine),(second?engine:botEngine)+sizeof engine);
        put(clockBytes+0xc,80+n*0.04f);nativeCall(e,resource+0x310,e+0x1190);
        assert(PowertrainMod::engagement.vehicle==v&&PowertrainMod::engagement.engine==e&&PowertrainMod::autoFollowing);
        assert(PowertrainMod::engagement.stretch==1&&PowertrainMod::progress.gain==1);
        assert(same(e+0xe0,700)&&same(e+0xe0+0x48,0.28169f));
        assert(!memcmp(second?engine:botEngine,other.data(),sizeof engine));
    }
    // Pauses never rewind an already-engaged gearbox to factor 1 (RPM shock).
    setup();put(car+0x588,40);put(engine+0xc0,1);put(engine+0xc8,1);PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;
    call();put(clockBytes+0xc,10.04f);call();before=PowertrainMod::engagement.stretch;assert(before>1);
    unsigned traces=traceCount();put(clockBytes+0xc,99);call();assert(PowertrainMod::engagement.stretch==before&&PowertrainMod::progress.gain==1);
    assert(traceCount()==traces);call();assert(PowertrainMod::engagement.stretch==before&&traceCount()==traces);
    for(int gate=0;gate<4;gate++){
        if(gate==0)engine[0xe0+0xc4]=1;
        if(gate==1){engine[0xe0+0xc4]=0;put(engine+0x54,2.5f);}
        if(gate==2){put(engine+0x54,0);put(engine+0xcc,0.2f);}
        if(gate==3){put(engine+0xcc,0);engine[0xd0]=1;}
        put(clockBytes+0xc,99+(gate+1)*0.04f);call();assert(PowertrainMod::engagement.stretch==before);checkRestored();
    }
    // Synthetic heavy truck (NOT a measured T-575 configuration). It is
    // deliberately already moving; no parked setup or second enable is allowed.
    setup();BYTE*g=engine+0xe0;put(g,3050);put(g+4,650);put(g+8,2200);put(g+0x44,0.48f);put(g+0x48,0.375f);
    *(int*)(g+0xc)=6;const float truckRatios[]={-0.35f,0.2f,0.35f,0.52f,0.7f,0.85f,1.0f};memcpy(g+0x10,truckRatios,sizeof truckRatios);
    PowertrainMod::stock(engine,stockConfig);assert(configure(stockConfig,setupConfig));
    put(car+0x588,40);put(engine+0xc0,1);put(engine+0xc8,1);PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;
    call();assert(seenRatio==stockConfig.overall&&seenTorque==3050);checkRestored();
    for(int n=1;n<=1500;n++){put(clockBytes+0xc,10+n*0.04f);call();checkRestored();}
    assert(PowertrainMod::engagement.stretch==setupConfig.stretch&&PowertrainMod::progress.gain>1&&PowertrainMod::applied==1501);
    assert(PowertrainMod::rejected==0&&PowertrainMod::conflicts==0&&traceCount()>10);
    // Freeze/finite bounds for the pure ratio scheduler, including long gaps.
    assert(rampStretch(2,4,3,false,false)==2);
    assert(rampStretch(4,2,0.04f,false,false)==4); // never snap to shorter gearing while rolling
    assert(rampStretch(2,4,0.04f,false,true)==2);
    assert(rampStretch(2,4,0.04f,true,false)==4);
    assert(rampStretch(__builtin_nanf(""),4,0.04f,false,false)==1);
    for(float target:{1.1f,2.0f,4.0f,8.0f})for(float dt:{0.01f,0.04f,0.1f}){
        float ratio=1;
        for(int n=0;n<40000;n++){
            float next=rampStretch(ratio,target,dt,false,false);
            assert(next>=ratio&&next<=target&&next<=ratio*(1+0.08f*dt)+0.00001f);
            // Independent native kinematic RPM relationship; no forced RPM.
            float oldRpm=3000/ratio,newRpm=3000/next;
            assert(newRpm<=oldRpm+0.001f&&newRpm>=oldRpm/(1+0.08f*dt)-0.001f);
            ratio=next;
        }
        assert(ratio==target);
    }
}
static void regressionMotion(){
    using namespace PowertrainPolicy;
    // Constant 0.4 rad/s is the exact old zero-power/held-gearing boundary.
    // Test coefficients under MOCK physics, not a simulated 300-MPH speed claim.
    for(int gears:{3,4,6})for(bool noisy:{false,true}){
        setup();BYTE*g=engine+0xe0;*(int*)(g+0xc)=gears;
        if(gears==6){put(g,3050);put(g+4,650);put(g+8,2200);put(g+0x44,0.48f);put(g+0x48,0.375f);put(g+0x10+6*4,1.2f);}
        PowertrainMod::stock(engine,stockConfig);assert(configure(stockConfig,setupConfig));
        put(car+0x588,setupConfig.stockTopMs*1.5f);put(engine+0xc0,1);put(engine+0xc8,1);
        PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;call();
        float oldRatio=1,oldGain=1;
        const float motion[]={0.10f,0.42f,0.28f,0.55f,0.36f,0.48f};
        for(int n=1;n<=2000;n++){
            put(engine+0x54,noisy?motion[(n/12)%6]:0.4f);put(clockBytes+0xc,10+n*0.04f);call();checkRestored();
            float ratio=PowertrainMod::engagement.stretch,gain=PowertrainMod::progress.gain;
            assert(ratio>=oldRatio&&ratio<=oldRatio*1.00321f+0.00001f);
            assert(gain-oldGain<=0.0101f);oldRatio=ratio;oldGain=gain;
            if(n>=1800)assert(gain>2);
        }
        assert(oldRatio==setupConfig.stretch&&oldGain>2&&PowertrainMod::enabled);
        // Severe motion cuts extra power immediately, without shortening the
        // engaged gearbox; braking and handbrake still use native torque.
        put(engine+0x54,2.5f);put(clockBytes+0xc,90.04f);call();
        assert(PowertrainMod::progress.gain==1&&PowertrainMod::engagement.stretch==oldRatio);checkRestored();
        put(engine+0xcc,0.2f);put(clockBytes+0xc,90.08f);call();assert(seenTorque==stockConfig.torque);checkRestored();
    }
    assert(filteredMotion(0,0.4f,0.04f)>0&&filteredMotion(0,0.4f,0.04f)<0.4f);
    assert(filteredMotion(0,2,3)==2&&filteredMotion(0,2,0)==2);
    assert(filteredMotion(0,__builtin_nanf(""),0.04f)==1000);
    puts("regression motion: mild body-motion power starvation fixed; rise bounds/severe-motion/brake guards passed (MOCK physics)");
}
static void regressionReconfigure(){
    using namespace PowertrainPolicy;
    setup();enable();expectRamp=true;put(car+0x588,setupConfig.stockTopMs*1.5f);put(engine+0xc0,1);put(engine+0xc8,1);
    float clock=10;for(int n=0;n<120;n++){clock+=0.04f;put(clockBytes+0xc,clock);call();}
    BYTE*g=engine+0xe0;float absolute=seenRatio;
    // Unused native slots must not disable/re-engage a four-speed vehicle.
    put(g+0x10+5*4,-317);put(g+0x10+6*4,__builtin_nanf(""));
    stockConfig.ratios[5]=-317;stockConfig.ratios[6]=__builtin_nanf("");
    assert(changes(PowertrainMod::engagement.stock,stockConfig)==0);
    clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
    assert(PowertrainMod::enabled&&PowertrainMod::baselineChanges==0&&seenRatio>=absolute);
    // Valid between-step native initialization/health/tuning updates are a new
    // baseline, not an in-call restoration conflict. Keep absolute gearing.
    absolute=seenRatio;put(g,600);put(g+4,600);put(g+8,6400);put(g+0x48,stockConfig.overall*0.95f);
    PowertrainMod::stock(engine,stockConfig);assert(configure(stockConfig,setupConfig));
    clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
    assert(PowertrainMod::enabled&&PowertrainMod::autoFollowing&&PowertrainMod::baselineChanges==1);
    assert(seenRatio>=absolute-0.000001f&&seenRatio<=absolute*1.00321f+0.000001f);
    float gain=PowertrainMod::progress.gain;
    for(int n=0;n<500;n++){
        float before=seenRatio;put(g,600+n*0.005f);PowertrainMod::stock(engine,stockConfig);assert(configure(stockConfig,setupConfig));
        clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
        assert(PowertrainMod::enabled&&PowertrainMod::conflicts==0&&PowertrainMod::generation==2);
        assert(seenRatio>=before-0.000001f&&seenRatio<=before*1.00321f+0.000001f);
        assert(PowertrainMod::progress.gain-gain<=0.0101f);gain=PowertrainMod::progress.gain;
    }
    unsigned refreshLogs=0;for(auto&s:logs)if(s.find("Speed auto native baseline refreshed:")==0)++refreshLogs;
    assert(refreshLogs>1&&refreshLogs<=12);
    // Invalid native configuration is NEVER accepted just to keep the toggle on.
    put(g+8,__builtin_nanf(""));stockConfig.redline=__builtin_nanf("");expectTune=false;
    clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
    assert(PowertrainMod::enabled&&!PowertrainMod::autoFollowing&&PowertrainMod::rejected>0&&same(g+8,stockConfig.redline));
    Stock a=stockConfig,b=stockConfig;a.overall=1;b.overall=0.01f;float rebased;
    assert(!rebaseStretch(a,8,b,rebased));
    puts("regression baseline: unused slots ignored; valid native updates retain bounded gearing/ON; invalid configs delegate; real in-call conflicts remain separately tested");
}
static void regressionNeutral(){
    setup();put(car+0x588,40);put(engine+0xc0,1);put(engine+0xc8,1);
    PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;call();
    float clock=10;for(int n=0;n<200;n++){clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();}
    float before=PowertrainMod::engagement.stretch;assert(before>1.5f);
    unsigned applied=PowertrainMod::applied;expectTune=false;*(int*)(engine+0xe0+0x98)=0;
    for(int n=0;n<4;n++){
        clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
        assert(PowertrainMod::enabled&&PowertrainMod::autoFollowing&&PowertrainMod::applied==applied);
        assert(PowertrainMod::engagement.stretch==before&&PowertrainMod::progress.gain==1);
    }
    expectTune=true;*(int*)(engine+0xe0+0x98)=4;clock+=0.04f;put(clockBytes+0xc,clock);call();checkRestored();
    assert(PowertrainMod::engagement.stretch>=before&&PowertrainMod::engagement.stretch<=before*1.00321f);
    assert(PowertrainMod::progress.gain<=1.0101f&&*(int*)(engine+0xe0+0x98)==4);
    std::vector<BYTE>former(engine,engine+sizeof engine);
    // A different player vehicle in neutral must NOT inherit that gearing.
    attached=bot;*(int*)(botEngine+0xe0+0x98)=0;expectTune=false;
    clock+=0.04f;put(clockBytes+0xc,clock);nativeCall(botEngine,resource+0x310,botEngine+0x1190);
    assert(PowertrainMod::engagement.vehicle==bot&&PowertrainMod::engagement.stretch==1);
    assert(!memcmp(engine,former.data(),sizeof engine));
    puts("regression neutral: no coefficient writes in native neutral; same-car gearing retained; other-car isolation passed");
}
static void regressionTargets(){
    using namespace PowertrainPolicy;
    for(int mph:SpeedPolicy::choices)for(int gears:{3,4,6}){
        setup();assert(SpeedMod::chooseTarget(mph));auto target=SpeedMod::selectedTarget();
        BYTE*g=engine+0xe0;*(int*)(g+0xc)=gears;
        if(gears==6){put(g,3050);put(g+4,650);put(g+8,2200);put(g+0x44,0.48f);put(g+0x48,0.375f);put(g+0x10+6*4,1.2f);}
        PowertrainMod::stock(engine,stockConfig);assert(configure(stockConfig,setupConfig,target.ms));
        put(car+0x588,setupConfig.stockTopMs*1.5f);put(engine+0xc0,1);put(engine+0xc8,1);
        PowertrainMod::setEnabled(true);expectTune=true;expectRamp=true;call();
        assert(PowertrainMod::engagement.stretch==1);assert(!SpeedMod::chooseTarget(100));
        float oldStretch=1,oldGain=1;
        for(int n=1;n<=1500;n++){
            put(clockBytes+0xc,10+n*0.04f);call();checkRestored();
            float stretch=PowertrainMod::engagement.stretch,gain=PowertrainMod::progress.gain;
            assert(stretch>=oldStretch&&stretch<=oldStretch*1.00321f+0.00001f&&stretch<=maxStretch);
            assert(gain<=maxWheelGain&&gain-oldGain<=0.0101f);oldStretch=stretch;oldGain=gain;
        }
        assert(oldStretch==setupConfig.stretch&&PowertrainMod::engagement.plan.stretch==setupConfig.stretch);
        assert(!PowertrainMod::unsupportedTarget&&PowertrainMod::conflicts==0);
        PowertrainMod::disable();assert(!PowertrainMod::enabled&&!SpeedMod::enabled&&!SpeedMod::modelPowerBand);
        assert(SpeedMod::chooseTarget(200));
    }
    setup();assert(SpeedMod::chooseTarget(500));put(engine+0xe0+0x48,0.03f);
    PowertrainMod::stock(engine,stockConfig);PowertrainMod::setEnabled(true);call();checkRestored();
    assert(PowertrainMod::unsupportedTarget&&!PowertrainMod::autoFollowing&&PowertrainMod::rejected==1);
    Setup p{};assert(!configure(stockConfig,p,0)&&!configure(stockConfig,p,201)&&!configure(stockConfig,p,__builtin_nanf("")));
    puts("Selected-target drivetrain: 100/200/300/400/500, three/four/six native gears and synthetic truck; moving engagement, unchanged rise/torque bounds/radius/spacing, target locking, OFF/cap ownership and unrepresentable-target delegation passed (MOCK physics; NOT speed reachability)");
}
static void binding(){
    setup();PowertrainMod::ready=false;nativeFunction=(void*)nativeEngine;creates=removes=0;createResult=enableResult=0;
    PowertrainMod::bind();assert(PowertrainMod::ready&&creates==1);
    setup();PowertrainMod::ready=false;creates=0;nativeCode[100]^=1;PowertrainMod::bind();assert(!PowertrainMod::ready&&creates==0);nativeCode[100]^=1;
    setup();PowertrainMod::ready=false;creates=removes=0;enableResult=1;PowertrainMod::bind();assert(!PowertrainMod::ready&&creates==1&&removes==1);enableResult=0;
    setup();PowertrainMod::ready=false;creates=0;createResult=1;PowertrainMod::bind();assert(!PowertrainMod::ready&&creates==1);createResult=0;
}
int main(int argc,char**argv){
    // Pure mock mode requires no proprietary game bytes. An optional private
    // engine-body file enables the native fingerprint binding fixture too.
    bool regression=argc==2&&!strncmp(argv[1],"--regression-",13);
    bool privateEngine=argc==2&&!regression;
    nativeCode.resize(0x881);
    if(privateEngine){FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(nativeCode.data(),1,nativeCode.size(),f)==nativeCode.size());fclose(f);}
    image=nativeCode.data();
    regions={{car,sizeof car},{bot,sizeof bot},{adapter,sizeof adapter},{botAdapter,sizeof botAdapter},{physics,sizeof physics},{botPhysics,sizeof botPhysics},
             {engine,sizeof engine},{botEngine,sizeof botEngine},{resource,sizeof resource},{rootBytes,sizeof rootBytes},{clockBytes,sizeof clockBytes},{image,nativeCode.size()}};
    powertrain_test_entry=(void*)PowertrainMod::engineUpdateHook;
    assert(!PowertrainMod::enabled);
    if(regression){
        if(!strcmp(argv[1],"--regression-motion"))regressionMotion();
        else if(!strcmp(argv[1],"--regression-reconfigure"))regressionReconfigure();
        else if(!strcmp(argv[1],"--regression-neutral"))regressionNeutral();
        else if(!strcmp(argv[1],"--regression-targets"))regressionTargets();
        else return 2;
        return 0;
    }
    setup();purePolicy();integration();automaticFollowing();regressionMotion();regressionReconfigure();regressionNeutral();regressionTargets();
    if(privateEngine)binding();
    else {setup();PowertrainMod::ready=false;creates=0;PowertrainMod::bind();assert(!PowertrainMod::ready&&creates==0);} // synthetic bytes must fail closed
    puts("powertrain: 10000 real 32-bit ABI calls to production detour / MOCK native engine; automatic moving-car/100-switch following, bounded gradual gearing/torque, loaded-trace gates, fixed gear spacing, native-state preservation, BOT/Shift/OFF/ownership/restoration/clock/turn/shift guards passed. NOT gameplay or universal 300-MPH certification.");
}
