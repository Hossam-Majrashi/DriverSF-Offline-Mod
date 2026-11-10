// Cap-only production hook exercised with a 32-bit native-call ABI fixture.
// No real game engine/handling simulation; real-game acceptance is separate.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;
static bool bound=true;static BYTE bytes[16],*image=bytes;
struct Region {BYTE*p;size_t n;};static std::vector<Region> regions;
static bool readable(const void*p,size_t n){for(auto r:regions)if(uintptr_t(p)>=uintptr_t(r.p)&&uintptr_t(p)+n>=uintptr_t(p)&&uintptr_t(p)+n<=uintptr_t(r.p)+r.n)return true;return false;}
static bool inside(const void*,size_t){return false;}
static bool clobberSpeedLog=false;
static void logLine(const char*){if(clobberSpeedLog)__asm__ volatile("pxor %%xmm0,%%xmm0; pxor %%xmm7,%%xmm7; fldz; clc" ::: "xmm0","xmm7","st","cc");}
static constexpr int MH_OK=0;
static int MH_CreateHook(void*,void*,void**){return 0;}
static int MH_EnableHook(void*){return 0;}
static int MH_DisableHook(void*){return 0;}
static int MH_RemoveHook(void*){return 0;}
extern "C" void speed_probe(void*,float,void*);
extern "C" void speed_probe_continuation();
struct alignas(16) SpeedAbiState {uint32_t regs[9];BYTE padding[12];BYTE fx[512];};
static_assert(sizeof(SpeedAbiState)==560,"probe ABI layout");
static BYTE *attached;
namespace VehicleMod {
static std::atomic<bool> ready{true},speed{false};static std::atomic<int> boostCapKmh{300};
static BYTE *playerVehicle(){return attached;}
static BYTE *vehiclePhysics(BYTE*v){
    if(!v || v!=attached || *(int*)(v+0x544)!=3)return nullptr;
    BYTE *a=*(BYTE**)(v+0x53c),*p=*(BYTE**)(a+8);
    return *(BYTE**)(p+0xa50)==v?p:nullptr;
}
static BYTE *uniqueCode(const BYTE*,size_t){return nullptr;}
}
#include "speed_hook_under_test.h"
alignas(16) static BYTE car[0x600],otherCar[0x600],adapter[0x300],otherAdapter[0x300],
    physics[0x1200],otherPhysics[0x1200],engine[0x1400],otherEngine[0x1400],resource[0x4000],controller[0x20],boostInput[0xa0];
static float simulatedSpeed=20,baseAcceleration=4.5f,nativeAcceleration=0;
static BYTE modelSeen[SpeedPolicy::drivingViewSize],limiterOutput[0xe0];
static void *limiterData=nullptr;static unsigned limiterNativeCalls=0;
static bool limiterReenter=false;
static void __thiscall fakeLimiter(void*self,void*data,void*out){
    assert((self==engine || self==otherEngine) && out==limiterOutput);++limiterNativeCalls;limiterData=data;
    memcpy(modelSeen,data,sizeof modelSeen);
    float cap;memcpy(&cap,(BYTE*)data+SpeedPolicy::modelLimitOffset,4);
    float ratio=simulatedSpeed/(cap*SpeedPolicy::nativeMphToMs);
    float value=ratio<=0.9f?1.0f:ratio>=1.0f?0.0f:1.0f-std::pow((ratio-0.9f)/0.1f,4.0f);
    memcpy(out,&value,4); // mock native limiter output, not actual physics
    if(limiterReenter){limiterReenter=false;BYTE before[sizeof modelSeen];memcpy(before,data,sizeof before);
        SpeedMod::limiterHook(self,nullptr,data,out);assert(!memcmp(before,data,sizeof before));}
}
static BYTE seen[0x40];static bool returnedCopy=false,turnOff=false,changeCar=false,reenter=false;
static unsigned nativeCalls=0;
extern "C" {
void *speed_test_getter_entry=nullptr;
extern BYTE speed_test_getter_return[];
}
static BYTE* __thiscall fakeTweaks(void*self){
    if(turnOff)SpeedMod::setEnabled(false);
    if(changeCar)attached=otherCar;
    return (BYTE*)self+0xb0;
}
extern "C" void speed_test_consume(void*self,BYTE*view,void*input){
    assert(input==controller);++nativeCalls;
    BYTE *v=*(BYTE**)((BYTE*)self+0xc);
    returnedCopy=view!=v+0xb0;memcpy(seen,view,sizeof seen);
    float limit;uint16_t flags;memcpy(&limit,view+0x1c,4);memcpy(&flags,view+0x3c,2);
    nativeAcceleration=(flags&0x10)&&simulatedSpeed>limit&&baseAcceleration>0?0:baseAcceleration;
    if(reenter){
        reenter=false;BYTE outer[0x40];memcpy(outer,view,sizeof outer);
        SpeedMod::updateHook(self,nullptr,input);
        assert(!memcmp(outer,view,sizeof outer)); // nested frame cannot clobber outer snapshot
    }
}
// A real call to the production fastcall detour. Its builtin return address
// must match this exported label; wrong-callsite delegation is tested below.
static void __attribute__((naked,thiscall)) fakeUpdate(void*,void*){
    __asm__ volatile(
        "push %ebx\n\t"
        "mov %ecx,%ebx\n\t"
        "mov 0xc(%ebx),%ecx\n\t"
        "call *speed_test_getter_entry\n\t"
        ".globl speed_test_getter_return\n"
        "speed_test_getter_return:\n\t"
        "push 8(%esp)\n\t"
        "push %eax\n\t"
        "push %ebx\n\t"
        "call speed_test_consume\n\t"
        "add $12,%esp\n\t"
        "pop %ebx\n\t"
        "ret $4\n\t");
}
static void setup(){
    for(auto p:{car,otherCar})memset(p,0,0x600);
    for(auto p:{adapter,otherAdapter})memset(p,0,0x300);
    for(auto p:{physics,otherPhysics})memset(p,0,0x1200);
    memset(engine,0x5a,sizeof engine);memset(otherEngine,0x4a,sizeof otherEngine);
    memset(resource,0x6b,sizeof resource);memset(controller,0x7c,sizeof controller);
    *(float*)(resource+0x2ef0)=145.7f;
    *(BYTE**)(engine+0x1270)=*(BYTE**)(otherEngine+0x1270)=resource;
    regions={{car,sizeof car},{otherCar,sizeof otherCar},{adapter,sizeof adapter},{otherAdapter,sizeof otherAdapter},
             {physics,sizeof physics},{otherPhysics,sizeof otherPhysics},
             {engine,sizeof engine},{otherEngine,sizeof otherEngine},{resource,sizeof resource},{boostInput,sizeof boostInput}};
    memset(boostInput,0x39,sizeof boostInput);*(BYTE**)(boostInput+0x98)=car;
    *(float*)(boostInput+0x80)=66;*(float*)(boostInput+0x84)=2;
    BYTE *cars[]={car,otherCar},*adapters[]={adapter,otherAdapter},*ps[]={physics,otherPhysics};
    for(int i=0;i<2;i++){
        *(BYTE**)(cars[i]+0x53c)=adapters[i];*(int*)(cars[i]+0x544)=3;
        *(BYTE**)(cars[i]+0x5b4)=resource;
        *(BYTE**)(adapters[i]+8)=ps[i];*(BYTE**)(adapters[i]+0xc)=cars[i];
        *(BYTE**)(ps[i]+0xa50)=cars[i];*(BYTE**)(ps[i]+0x1150)=i==0?engine:otherEngine;
        *(BYTE**)(ps[i]+0xaa4)=resource;
        *(float*)(cars[i]+0xcc)=50;*(uint16_t*)(cars[i]+0xec)=0x8152;
    }
    attached=car;VehicleMod::ready=true;VehicleMod::speed=false;VehicleMod::boostCapKmh=300;
    SpeedMod::ready=true;SpeedMod::enabled=false;SpeedMod::targetMph=300;SpeedMod::modelPowerBand=false;SpeedMod::reported=false;SpeedMod::reportedLimit=false;SpeedMod::reportedBoost=false;
    SpeedMod::boostViews=0;SpeedMod::originalBoostCapTarget=(void*)speed_probe_continuation;
    clobberSpeedLog=false;
    SpeedMod::limitCalls=SpeedMod::limitViews=0;SpeedMod::originalLimiter=fakeLimiter;
    limiterData=nullptr;limiterNativeCalls=0;limiterReenter=false;memset(limiterOutput,0,sizeof limiterOutput);
    SpeedMod::calls=SpeedMod::matched=SpeedMod::views=0;SpeedMod::context=nullptr;
    SpeedMod::originalTweaks=fakeTweaks;SpeedMod::originalUpdate=fakeUpdate;
    SpeedMod::tweakCaller=speed_test_getter_return;speed_test_getter_entry=(void*)SpeedMod::tweaksHook;
    returnedCopy=turnOff=changeCar=reenter=false;nativeCalls=0;simulatedSpeed=20;baseAcceleration=4.5f;
}
static float selectedCap(){float cap;memcpy(&cap,seen+0x1c,4);return cap;}
static void run(){SpeedMod::updateHook(adapter,nullptr,controller);assert(!SpeedMod::context);}
int main(){using namespace SpeedMod;
    setup();run();assert(!returnedCopy&&selectedCap()==50&&nativeCalls==1);
    setup();setEnabled(true);
    BYTE carBefore[sizeof car],botBefore[sizeof otherCar],eBefore[sizeof engine],rBefore[sizeof resource],cBefore[sizeof controller];
    memcpy(carBefore,car,sizeof car);memcpy(botBefore,otherCar,sizeof otherCar);memcpy(eBefore,engine,sizeof engine);
    memcpy(rBefore,resource,sizeof resource);memcpy(cBefore,controller,sizeof controller);
    for(int n=0;n<10000;n++)run();
    assert(calls==10000&&matched==10000&&views==10000&&returnedCopy&&reported);
    assert(selectedCap()==SpeedPolicy::ceilingMs);
    for(size_t i=0;i<sizeof seen;i++)if(i<0x1c||i>=0x20)assert(seen[i]==car[0xb0+i]);
    assert(!memcmp(carBefore,car,sizeof car)&&!memcmp(botBefore,otherCar,sizeof otherCar));
    assert(!memcmp(eBefore,engine,sizeof engine)&&!memcmp(rBefore,resource,sizeof resource)&&!memcmp(cBefore,controller,sizeof controller));
    assert(!VehicleMod::speed&&VehicleMod::boostCapKmh==300); // never changes boost behavior
    setEnabled(false);run();assert(!returnedCopy&&selectedCap()==50);
    // The limiter can release an existing throttle cut, never synthesize acceleration.
    setup();for(int speed=0;speed<50;speed++){
        simulatedSpeed=speed;setEnabled(false);run();float normal=nativeAcceleration;
        setEnabled(true);run();assert(nativeAcceleration==normal&&normal==baseAcceleration);
    }
    simulatedSpeed=60;setEnabled(false);run();assert(nativeAcceleration==0);
    setEnabled(true);run();assert(nativeAcceleration==baseAcceleration);
    simulatedSpeed=SpeedPolicy::ceilingMs+1;run();assert(nativeAcceleration==0);
    baseAcceleration=-3;run();assert(nativeAcceleration==-3); // braking unchanged
    baseAcceleration=0;run();assert(nativeAcceleration==0); // coasting unchanged
    setup();setEnabled(true);updateHook(otherAdapter,nullptr,controller);assert(!returnedCopy&&selectedCap()==50&&views==0); // same-model BOT
    setup();setEnabled(true);attached=nullptr;run();assert(!returnedCopy&&views==0); // Shift
    setup();setEnabled(true);*(BYTE**)(adapter+0xc)=otherCar;run();assert(!returnedCopy&&views==0);
    setup();setEnabled(true);*(BYTE**)(adapter+8)=otherPhysics;run();assert(!returnedCopy&&views==0);
    setup();setEnabled(true);VehicleMod::ready=false;run();assert(!returnedCopy&&views==0);
    setup();setEnabled(true);for(auto&r:regions)if(r.p==adapter)r.n=0xc;run();assert(!returnedCopy&&views==0);
    setup();setEnabled(true);turnOff=true;run();assert(!enabled&&!returnedCopy&&views==0);
    setup();setEnabled(true);changeCar=true;run();assert(!returnedCopy&&views==0);
    changeCar=false;updateHook(otherAdapter,nullptr,controller);assert(returnedCopy&&views==1); // follows new player
    setup();setEnabled(true);reenter=true;run();assert(nativeCalls==2&&views==2&&!context);
    setup();setEnabled(true);{
        Context frame{nullptr,car,adapter,{}};context=&frame;
        assert(tweaksHook(car,nullptr)==car+0xb0); // other caller cannot receive copy
        assert(selectTweaks(car,car+0xb4,tweakCaller)==car+0xb4);
        context=nullptr;
    }
    setup();setEnabled(true);*(float*)(car+0xcc)=200;run();assert(returnedCopy&&selectedCap()==SpeedPolicy::ceilingMs); // chosen maximum may LOWER a native override
    setup();setEnabled(true);*(float*)(car+0xcc)=__builtin_nanf("");run();assert(!returnedCopy);
    setup();setEnabled(true);*(float*)(car+0xcc)=-1;run();assert(!returnedCopy);
    setup();setEnabled(true);*(uint16_t*)(car+0xec)&=~0x10;run();assert(returnedCopy&&selectedCap()==SpeedPolicy::ceilingMs);
    uint16_t flags;memcpy(&flags,seen+0x3c,2);assert(flags==0x8152);
    setup();setEnabled(true);*(float*)(car+0xcc)=1001;run();assert(!returnedCopy);
    setup();setEnabled(true);*(float*)(car+0xcc)=__builtin_inff();run();assert(!returnedCopy);
    setup();setEnabled(true);*(float*)(car+0xcc)=0;run();assert(returnedCopy);
    setup();std::atomic<int> arrived{0};
    auto isolated=[&](){Context local{nullptr,nullptr,nullptr,{}};assert(context==nullptr);context=&local;
        ++arrived;while(arrived.load()!=2)std::this_thread::yield();assert(context==&local);context=nullptr;};
    std::thread a(isolated),b(isolated);a.join();b.join();assert(context==nullptr);
    // Direct model-p160 limiter, the separate path discovered from public research.
    auto limit=[&](){limiterHook(engine,nullptr,resource+0x310,limiterOutput);};
    setup();limit();assert(limiterData==resource+0x310&&limitViews==0);
    setup();setEnabled(true);memcpy(eBefore,engine,sizeof engine);memcpy(rBefore,resource,sizeof resource);
    memcpy(carBefore,car,sizeof car);memcpy(botBefore,otherCar,sizeof otherCar);
    for(int n=0;n<10000;n++)limit();
    assert(limitCalls==10000&&limitViews==10000&&limiterNativeCalls==10000&&reportedLimit);
    assert(limiterData!=resource+0x310);
    float selected;memcpy(&selected,modelSeen+SpeedPolicy::modelLimitOffset,4);
    assert(selected==SpeedPolicy::nativeLimitMph);
    for(size_t n=0;n<sizeof modelSeen;n++)if(n<SpeedPolicy::modelLimitOffset||n>=SpeedPolicy::modelLimitOffset+4)
        assert(modelSeen[n]==resource[0x310+n]);
    assert(!memcmp(eBefore,engine,sizeof engine)&&!memcmp(rBefore,resource,sizeof resource));
    assert(!memcmp(carBefore,car,sizeof car)&&!memcmp(botBefore,otherCar,sizeof otherCar));
    for(float speed:{0.0f,5.0f,20.0f,40.0f,55.0f}){
        simulatedSpeed=speed;setEnabled(false);limit();float off;memcpy(&off,limiterOutput,4);
        setEnabled(true);limit();float on;memcpy(&on,limiterOutput,4);assert(off==1&&on==off);
    }
    simulatedSpeed=145.7f*SpeedPolicy::nativeMphToMs*0.97f;
    setEnabled(false);limit();float off;memcpy(&off,limiterOutput,4);assert(off<1&&off>0);
    setEnabled(true);limit();float on;memcpy(&on,limiterOutput,4);assert(on==1);
    simulatedSpeed=SpeedPolicy::ceilingMs+1;limit();memcpy(&on,limiterOutput,4);assert(on==0);
    setup();setEnabled(true);limiterHook(otherEngine,nullptr,resource+0x310,limiterOutput);assert(limitViews==0&&limiterData==resource+0x310);
    setup();setEnabled(true);attached=nullptr;limit();assert(limitViews==0);
    setup();setEnabled(true);*(BYTE**)(physics+0xaa4)=nullptr;limit();assert(limitViews==0);
    setup();setEnabled(true);*(BYTE**)(engine+0x1270)=nullptr;limit();assert(limitViews==0);
    setup();setEnabled(true);limiterHook(engine,nullptr,resource,limiterOutput);assert(limitViews==0&&limiterData==resource);
    setup();setEnabled(true);limiterHook(engine,nullptr,resource+0x314,limiterOutput);assert(limitViews==0);
    setup();setEnabled(true);for(auto&r:regions)if(r.p==resource)r.n=0x2ef3;limit();assert(limitViews==0);
    setup();setEnabled(true);for(auto&r:regions)if(r.p==engine)r.n=0x1273;limit();assert(limitViews==0);
    for(float cap:{0.0f,-1.0f,__builtin_nanf(""),__builtin_inff()}){
        setup();setEnabled(true);*(float*)(resource+0x2ef0)=cap;limit();assert(limitViews==0);
    }
    setup();setEnabled(true);limiterReenter=true;limit();assert(limitCalls==2&&limitViews==1&&limiterNativeCalls==2); // private copy is not a shared resource view
    setup();ready=false;setEnabled(true);assert(!enabled);
    assert(std::fabs(SpeedPolicy::ceilingMs*1.3f*3600/1609.344f-300)<0.001f);
    // Real interior assembly bridge, not a C++ approximation of its ABI.
    auto probe=[&](float cap,bool changed){
        alignas(16) SpeedAbiState states[2]{};
        speed_probe(boostInput,cap,states);
        for(int i=0;i<9;i++)if(i!=3)assert(states[0].regs[i]==states[1].regs[i]);
        assert(states[0].regs[3]==states[1].regs[3]+4); // CALL return address only
        float result;memcpy(&result,states[1].fx+176,4);
        if(changed)assert(result==selectedTarget().ms);
        else assert(!memcmp(states[0].fx+176,states[1].fx+176,4));
        // Defined FXSAVE fields through XMM7; padding/reserved bytes excluded.
        for(int i=0;i<288;i++)if(!changed || i<176 || i>=180)assert(states[0].fx[i]==states[1].fx[i]);
        assert(states[0].regs[8]&(1<<10)); // DF set at entry, survives bridge
        assert(states[0].regs[8]&1); // carry flag survives helper
        return result;
    };
    setup();clobberSpeedLog=true;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);clobberSpeedLog=true;
    BYTE boostBefore[sizeof boostInput];memcpy(boostBefore,boostInput,sizeof boostInput);
    memcpy(eBefore,engine,sizeof engine);memcpy(rBefore,resource,sizeof resource);
    for(int n=0;n<10000;n++)probe(65.13f,true);
    assert(boostViews==10000&&reportedBoost);
    assert(!memcmp(boostBefore,boostInput,sizeof boostInput));
    assert(!memcmp(eBefore,engine,sizeof engine)&&!memcmp(rBefore,resource,sizeof resource));
    setEnabled(false);probe(65.13f,false);
    setup();setEnabled(true);*(BYTE**)(boostInput+0x98)=otherCar;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);attached=nullptr;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);ready=false;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);VehicleMod::ready=false;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);*(BYTE**)(adapter+8)=otherPhysics;probe(65.13f,false);assert(boostViews==0);
    setup();setEnabled(true);for(auto&r:regions)if(r.p==boostInput)r.n=0x9f;probe(65.13f,false);assert(boostViews==0);
    for(float cap:{0.0f,-1.0f,__builtin_nanf(""),__builtin_inff()}){
        setup();setEnabled(true);probe(cap,false);assert(boostViews==0);
    }
    for(float gain:{-1.0f,__builtin_nanf(""),__builtin_inff()}){
        setup();setEnabled(true);*(float*)(boostInput+0x84)=gain;probe(65.13f,false);assert(boostViews==0);
    }
    setup();setEnabled(true);*(float*)(boostInput+0x80)=__builtin_nanf("");probe(65.13f,false);assert(boostViews==0);
    // Model only the verified native opposing-force branch: no positive push.
    auto drag=[](float speed,float cap,float gain){return speed>cap?(cap-speed)*gain:0.0f;};
    for(float speed:{0.0f,20.0f,60.0f,65.0f})assert(drag(speed,65.13f,2)==drag(speed,SpeedPolicy::ceilingMs,2));
    assert(drag(66,65.13f,2)<0&&drag(66,SpeedPolicy::ceilingMs,2)==0);
    assert(drag(105,SpeedPolicy::ceilingMs,2)<0);
    // Every selected HUD target reaches all THREE scoped native cap paths,
    // including LOWERING a stock cap at 100 MPH. No hard velocity clamp.
    for(int mph:SpeedPolicy::choices){
        setup();assert(chooseTarget(mph));auto t=selectedTarget();
        assert(t.mph==mph&&std::fabs(SpeedPolicy::displayedMph(t.ms)-mph)<0.001f);
        assert(t.ms>0&&t.ms<200&&t.performance>=t.model);
        assert(!chooseTarget(0)&&!chooseTarget(600)&&!chooseTarget(101));
        setEnabled(true);assert(!chooseTarget(mph==100?200:100));assert(targetMph==mph);
        memcpy(carBefore,car,sizeof car);memcpy(botBefore,otherCar,sizeof otherCar);
        memcpy(eBefore,engine,sizeof engine);memcpy(rBefore,resource,sizeof resource);
        run();assert(returnedCopy&&selectedCap()==t.ms);
        limit();memcpy(&selected,modelSeen+SpeedPolicy::modelLimitOffset,4);assert(selected==t.model);
        modelPowerBand=true;limit();memcpy(&selected,modelSeen+SpeedPolicy::modelLimitOffset,4);assert(selected==t.performance);
        probe(65.13f,true);probe(200.0f,true);
        assert(!memcmp(carBefore,car,sizeof car)&&!memcmp(botBefore,otherCar,sizeof otherCar));
        assert(!memcmp(eBefore,engine,sizeof engine)&&!memcmp(rBefore,resource,sizeof resource));
        *(BYTE**)(boostInput+0x98)=otherCar;probe(65.13f,false);
        updateHook(otherAdapter,nullptr,controller);assert(!returnedCopy);
        setEnabled(false);assert(!chooseTarget(300)); // engine mode still owns the target
        modelPowerBand=false;assert(chooseTarget(300));
    }
    assert(SpeedPolicy::target(0).mph==300);
    puts("Selected 100/200/300/400/500 MPH: original HUD inversion, all three native cap paths, lower-cap selection, option lock, BOT isolation passed (MOCK native physics)");
    puts("ability brake: 10000 real assembly bridge calls; GPRs/flags/DF/x87/MXCSR/XMM lanes preserved except cap, player/BOT/Shift/OFF guards; no native-object writes (mock continuation, not gameplay)");
    puts("cap-only: 10000 input + 10000 model-limiter calls; native ABI, resource/view identity, nesting, BOT/Shift/OFF and zero engine/model writes passed (mock engine, not gameplay acceptance)");
}
