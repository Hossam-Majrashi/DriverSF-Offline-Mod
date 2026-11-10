// Actual production paint dispatch/capture with explicitly mocked engine/Win32 APIs.
// Not a native game render test and not a substitute for visual acceptance.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define __thiscall __attribute__((thiscall))
using BYTE=unsigned char;using DWORD=uint32_t;using WPARAM=uintptr_t;using LONG=int32_t;
static BYTE imageBytes[128];static BYTE *image=imageBytes;
static bool bound=true;static void *windowHandle=(void*)1;static unsigned paintMessage=100;
static DWORD tick=100,thread=7;static bool postOk=true;static WPARAM lastToken;
struct Region{BYTE *p;size_t n;};static std::vector<Region> regions;
static bool readable(const void *p,size_t n){for(auto r:regions)if(uintptr_t(p)>=uintptr_t(r.p) && uintptr_t(p)+n>=uintptr_t(p) && uintptr_t(p)+n<=uintptr_t(r.p)+r.n)return true;return false;}
static bool inside(const void*,size_t){return false;}
static void logLine(const char*){}
static DWORD GetTickCount(){return tick;}
static DWORD GetCurrentThreadId(){return thread;}
static DWORD GetWindowThreadProcessId(void*,void*){return 7;}
static bool PostMessage(void*,unsigned,WPARAM token,int){lastToken=token;return postOk;}
static BYTE *currentCar,*currentPhysics;
namespace VehicleMod {
static BYTE *playerVehicle(){return currentCar;}
static BYTE *vehiclePhysics(BYTE *v){return v==currentCar?currentPhysics:nullptr;}
static bool writable(void *p,size_t n){return readable(p,n);}
static BYTE *uniqueCode(const BYTE*,size_t){return nullptr;}
}
#include "paint_dispatch_under_test.h"
alignas(16) static BYTE car[0x600],other[0x600],physics[0x1200],resource[0x2d00];
static int nativeCalls=0;
static void __thiscall mockNative(void *self,const PaintPolicy::Selection *input,bool refreshFlag) {
    assert(self==car && !refreshFlag);++nativeCalls;
    auto result=*input;
    if(result.count>2)result.indices[2]=result.indices[0];
    for(uint32_t i=0;i<result.count;i++)result.indices[i]%=*(uint32_t*)(resource+0x310+i*0xdc4+0xdc0);
    memcpy((BYTE*)self+0x5a4,&result,sizeof(result));
}
static void setup(uint32_t groups=2) {
    memset(car,0,sizeof car);memset(other,0,sizeof other);memset(resource,0,sizeof resource);
    regions={{car,sizeof car},{other,sizeof other},{physics,sizeof physics},{resource,sizeof resource}};
    currentCar=car;currentPhysics=physics;*(int*)(car+0x544)=*(int*)(other+0x544)=3;
    *(BYTE**)(car+0x5b4)=*(BYTE**)(other+0x5b4)=resource;
    *(uint32_t*)(resource+0x310+0x294c)=groups;
    for(uint32_t i=0;i<3;i++)*(uint32_t*)(resource+0x310+i*0xdc4+0xdc0)=i==2?2:5;
    PaintPolicy::Selection s{{1,2,1,0},groups};memcpy(car+0x5a4,&s,sizeof s);memcpy(other+0x5a4,&s,sizeof s);
    PaintMod::pending={};PaintMod::generation=0;PaintMod::ready=true;PaintMod::setPaint=mockNative;
    PaintMod::liveEnabled=false;PaintMod::selectedVariant=0;
    PaintMod::displayedCar=PaintMod::displayedResource=PaintMod::displayedPhysics=0;
    nativeCalls=0;tick=100;thread=7;postOk=true;paintMessage=100;
}
int main(){
    using namespace PaintMod;
    // Successful production dispatch, current-car scope, resource bytes unchanged.
    setup();BYTE before[sizeof resource],trafficBefore[sizeof other];memcpy(before,resource,sizeof before);memcpy(trafficBefore,other,sizeof other);
    request(4);assert(pending.active);execute(uint32_t(lastToken));assert(nativeCalls==1);
    assert(car[0x5a4]==4 && car[0x5a5]==2);
    assert(!memcmp(before,resource,sizeof before) && !memcmp(trafficBefore,other,sizeof other));
    execute(uint32_t(lastToken));assert(nativeCalls==1); // no duplicate application
    // Native third-group coupling is predicted rather than mistaken for an error.
    setup(3);request(3);execute(uint32_t(lastToken));assert(nativeCalls==1 && car[0x5a6]==1);
    setup();request(5);assert(!pending.active && nativeCalls==0);
    setup();request(-1);assert(!pending.active && nativeCalls==0);
    setup();request(3);tick=2101;execute(uint32_t(lastToken));assert(nativeCalls==0);
    setup();request(3);currentCar=other;execute(uint32_t(lastToken));assert(nativeCalls==0);
    setup();request(3);thread=8;execute(uint32_t(lastToken));assert(nativeCalls==0);
    setup();request(3);car[0x5a4]=2;execute(uint32_t(lastToken));assert(nativeCalls==0);
    setup();request(3);cancelPending();execute(uint32_t(lastToken));assert(nativeCalls==0);
    setup();request(3);uint32_t old=uint32_t(lastToken);request(4);execute(old);assert(nativeCalls==0 && pending.active);execute(uint32_t(lastToken));assert(nativeCalls==1);
    setup();postOk=false;request(3);assert(!pending.active && nativeCalls==0);
    setup();*(uint32_t*)(resource+0x310+0xdc0)=0;Snapshot s{};assert(!capture(s));
    setup();*(uint32_t*)(resource+0x310+0xdc0)=17;assert(!capture(s));
    setup();*(uint32_t*)(resource+0x310+0x294c)=4;assert(!capture(s));
    setup();*(int*)(car+0x544)=0;assert(!capture(s));
    setup();currentPhysics=nullptr;assert(!capture(s));
    setup();ready=false;assert(!capture(s));
    // v7.4: checkbox opt-in, no automatic repaint, rapid selection supersession.
    setup();enableLive(true);assert(liveEnabled && !pending.active && nativeCalls==0 && selectedVariant==1);
    for(int frame=0;frame<1000;frame++)observe();assert(nativeCalls==0);
    selectVariant(2);uint32_t first=uint32_t(lastToken);
    selectVariant(3);uint32_t second=uint32_t(lastToken);
    selectVariant(4);uint32_t newest=uint32_t(lastToken);
    execute(first);execute(second);assert(nativeCalls==0 && pending.active);
    execute(newest);assert(nativeCalls==1 && car[0x5a4]==4 && liveEnabled);
    selectVariant(4);assert(!pending.active && nativeCalls==1); // no duplicate call
    enableLive(false);assert(!liveEnabled && car[0x5a4]==4);selectVariant(2);assert(!pending.active);
    setup();enableLive(true);selectVariant(3);uint32_t disabled=uint32_t(lastToken);
    enableLive(false);execute(disabled);assert(nativeCalls==0 && !liveEnabled);
    setup();enableLive(true);selectVariant(3);uint32_t undone=uint32_t(lastToken);
    selectVariant(1);execute(undone);assert(nativeCalls==0 && liveEnabled && !pending.active);
    setup();enableLive(true);selectVariant(3);uint32_t stale=uint32_t(lastToken);
    currentCar=other;observe();execute(stale);assert(!liveEnabled && !pending.active && nativeCalls==0);
    setup();enableLive(true);selectVariant(3);currentCar=nullptr;observe();assert(!liveEnabled && !pending.active);
    setup();enableLive(true);selectVariant(3);tick=2101;execute(uint32_t(lastToken));assert(!liveEnabled && nativeCalls==0);
    setup();postOk=false;enableLive(true);selectVariant(3);assert(!liveEnabled && !pending.active);
    setup();*(uint32_t*)(resource+0x310+0xdc0)=1;car[0x5a4]=0;enableLive(true);assert(!liveEnabled);
    setup();selectVariant(3);assert(!pending.active && nativeCalls==0); // starts unchecked
    // Wrap-safe deadlines and pure policy boundaries.
    assert(PaintPolicy::fresh(0xfffffff0u,0x20u));assert(!PaintPolicy::fresh(0x21u,0x20u));
    uint32_t counts[3]={16,1,2};PaintPolicy::Selection cur{{0,0,0,0},3},input{},normalized{};
    assert(PaintPolicy::choose(cur,3,counts,15,input,normalized));assert(normalized.indices[2]==1 && input.indices[1]==0);
    counts[0]=0;assert(!PaintPolicy::choose(cur,3,counts,0,input,normalized));
    puts("Production paint/live-color fixtures passed: opt-in, rapid/latest selection, no frame loop, stop/car-switch cancellation, bounds and thread guards (mocked engine).");
}
