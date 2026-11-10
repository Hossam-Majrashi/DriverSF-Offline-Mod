// Offline 32-bit fixtures; vehicle hooks themselves are included by the runner.
// This exercises real hook bodies with synthetic objects, NOT Driver.exe.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <vector>
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;
using DWORD=uint32_t;
constexpr DWORD PAGE_READWRITE=4,PAGE_WRITECOPY=8,PAGE_EXECUTE_READWRITE=64,PAGE_EXECUTE_WRITECOPY=128;
struct MEMORY_BASIC_INFORMATION { DWORD Protect; };
struct Region { BYTE *p;size_t n;bool write; };
static std::vector<Region> regions;
static bool readable(const void *p,size_t n) {
    for(auto r:regions)if(uintptr_t(p)>=uintptr_t(r.p) && uintptr_t(p)+n>=uintptr_t(p) && uintptr_t(p)+n<=uintptr_t(r.p)+r.n)return true;
    return false;
}
static int VirtualQuery(const void *p,MEMORY_BASIC_INFORMATION *m,size_t) {
    for(auto r:regions)if(uintptr_t(p)>=uintptr_t(r.p) && uintptr_t(p)<uintptr_t(r.p)+r.n){m->Protect=r.write?4:2;return sizeof(*m);}
    return 0;
}
static bool bound=true;
static uintptr_t *singletonSlot;
static void logLine(const char*) {}
#include "vehicle_hooks_under_test.h"
namespace VehicleMod {
static void cancelRepair(){repairRequested=false;repairFor=0;}
#include "vehicle_disable_under_test.h"
}
static void nitroHeatFixture(void *self,void*,void *out) {
    // Invoke the actual assembly detour with the native thiscall stack layout.
    ((VehicleMod::AbilityUpdate)(void*)VehicleMod::nitroHeatHook)(self,out);
}
#define nitroHeatHook nitroHeatFixture
struct alignas(16) AbiResult {
    uintptr_t ecx,edx,eax,flags,ebp,esp,expectedEsp,esi,edi,ebx,pad0,pad1;
    BYTE fx[512];
};
static_assert(sizeof(AbiResult)==560,"probe result layout");
extern "C" void probe_nitro_abi(void*,void*,void*,AbiResult*);
extern "C" void nitro_abi_native();
static unsigned nativeHeatCalls=0,nativeRamCalls=0,nativeContacts=0,setters=0;
static BYTE *damaged=nullptr;
static float ramChargeSeen=0;
static int endHeatState=1;
static bool switchCarOnContact=false;
alignas(16) static BYTE rootObject[0x700],attached[0x700],abilities[0x600];
alignas(16) static BYTE car[0x600],other[0x600],third[0x600],adapter[0x20],otherAdapter[0x20];
alignas(16) static BYTE physics[0x1200],otherPhysics[0x1200],engine[0xe00],input[0xa0],output[0xd0];
static uintptr_t rootValue;
static void ptr(BYTE *p,size_t off,void *value){*(void**)(p+off)=value;}
static float& f(BYTE*p,size_t off){return *(float*)(p+off);}
static int& i(BYTE*p,size_t off){return *(int*)(p+off);}
static void __thiscall heatNative(void *self,void *out) {
    ++nativeHeatCalls;auto p=(BYTE*)self;
    f(p,0xb0)-=1.0f;f(p,0xb4)+=0.01f;i(p,0xa8)=endHeatState;
    f((BYTE*)out,0xc4)=1.25f; // observable native output must survive
}
static void __thiscall ramNative(void *self,void *in) {
    ++nativeRamCalls;auto p=(BYTE*)self;ramChargeSeen=f(p,0x38);
    if(i(p,4)==1)f(p,0x38)+=1;
    if(i(p,4)==2){f((BYTE*)in,0x40)+=3;f((BYTE*)in,0x44)+=2;f((BYTE*)in,0x48)+=1;}
}
static void __thiscall contactNative(void*,void *a,void *b,uintptr_t c,uintptr_t d) {
    ++nativeContacts;assert(a && b && c==0x1234 && d==0x5678);
    if(switchCarOnContact)ptr(attached,0x678,third);
}
static float __thiscall maximumNative(void *self){return f((BYTE*)self,0xc0);}
static void __thiscall damageNative(void *self,float damage){++setters;damaged=(BYTE*)self;f(damaged,0x508)=damage;}
static void add(BYTE *p,size_t n){regions.push_back({p,n,true});}
static void reset() {
    using namespace VehicleMod;
    regions.clear();
    for(auto r:{Region{rootObject,sizeof rootObject,true},{attached,sizeof attached,true},{abilities,sizeof abilities,true},
        {car,sizeof car,true},{other,sizeof other,true},{third,sizeof third,true},{adapter,sizeof adapter,true},
        {otherAdapter,sizeof otherAdapter,true},{physics,sizeof physics,true},{otherPhysics,sizeof otherPhysics,true},
        {engine,sizeof engine,true},{input,sizeof input,true},{output,sizeof output,true}}){memset(r.p,0,r.n);regions.push_back(r);}
    rootValue=uintptr_t(rootObject);singletonSlot=&rootValue;add((BYTE*)&rootValue,sizeof rootValue);
    ptr(rootObject,0x684,attached);ptr(rootObject,0x688,abilities);ptr(attached,0x678,car);
    ptr(abilities,0x298,car);ptr(car,0x53c,adapter);i(car,0x544)=3;
    ptr(other,0x53c,otherAdapter);i(other,0x544)=3;
    ptr(adapter,8,physics);ptr(otherAdapter,8,otherPhysics);
    ptr(physics,0xa50,car);ptr(otherPhysics,0xa50,other);ptr(physics,0x1150,engine);
    ptr(input,0x98,car);f(car,0xc0)=f(other,0xc0)=1;
    i(engine,0xa8)=1;f(engine,0xb0)=100;f(engine,0xb4)=0.99f;f(engine,0xd98)=100;
    i(abilities+0xb0,4)=1;f(abilities+0xb0,0x3c)=60;f(abilities+0xb0,0x38)=2;
    nativeHeatCalls=nativeRamCalls=nativeContacts=setters=0;damaged=nullptr;endHeatState=1;switchCarOnContact=false;
    nitro=ram=oneHit=health=body=speed=false;
    originalNitroHeat=heatNative;originalRamUpdate=ramNative;originalCollision=contactNative;
    originalMaximum=maximumNative;originalDamage=damageNative;
}
// Return the unwrapped production entry, not the C++ fixture adapter.
#undef nitroHeatHook
static void *nitroEntry(){return (void*)VehicleMod::nitroHeatHook;}
#define nitroHeatHook nitroHeatFixture
int main() {
    using namespace VehicleMod;
    for(bool enabled:{false,true}) for(bool active:{false,true}) {
        reset();nitro=enabled;originalNitroHeat=(AbilityUpdate)(void*)nitro_abi_native;
        i(engine,0xa8)=active?1:0;f(engine,0xb0)=active?100.0f:0.0f;
        AbiResult result{};probe_nitro_abi(nitroEntry(),engine,output,&result);
#ifdef LEGACY_ABI_REPRO
        if(!enabled)continue;
        printf("Legacy ON ECX after return: %08x; required: %08x\n",(unsigned)result.ecx,(unsigned)(uintptr_t)engine);
        assert(result.ecx!=uintptr_t(engine)); // reproduce the old register-contract defect
        puts("PASS: original v7 hook reproduces ECX corruption; no game launched");
        return 0;
#else
        assert(result.ecx==uintptr_t(engine) && result.edx==uintptr_t(output));
        assert(result.eax==0x12345678 && (result.flags&1));
        assert(result.esp==result.expectedEsp);
        assert(result.esi==0x13579bdf && result.edi==0x2468ace0);
        assert(*(uint32_t*)(result.fx+24)==0x3f80); // native MXCSR rounding mode
        const uint32_t pattern[]={0x3f800000,0x40000000,0x40400000,0x40800000};
        assert(!memcmp(result.fx+160,pattern,sizeof pattern)); // native XMM0
        assert(result.fx[4]!=0 && *(uint64_t*)(result.fx+32)==0x8000000000000000ULL);
        assert(*(uint16_t*)(result.fx+40)==0x3fff); // native x87 ST0 = 1.0
#endif
    }
#ifndef LEGACY_ABI_REPRO
    puts("PASS: native ECX/EDX/EAX, stack, callee-saved registers, flags, XMM0, x87 and MXCSR preserved with Nitro OFF/ON and idle/active engine");
    reset();nitroHeatHook(engine,nullptr,output);
    assert(f(engine,0xb0)==99 && nativeHeatCalls==1 && f(output,0xc4)==1.25f);
    reset();nitro=true;
    for(int k=0;k<100000;k++)nitroHeatHook(engine,nullptr,output);
    assert(f(engine,0xb0)==100 && f(engine,0xb4)==0.5f && nativeHeatCalls==100000);
    endHeatState=0;nitroHeatHook(engine,nullptr,output);assert(f(engine,0xb0)==99); // release is not revived
    reset();nitro=true;f(engine,0xb0)=120;nitroHeatHook(engine,nullptr,output);
    assert(f(engine,0xb0)==119); // preserve warm-up progression before sustain
    reset();nitro=true;i(engine,0xa8)=2;endHeatState=2;nitroHeatHook(engine,nullptr,output);assert(f(engine,0xb0)==99);
    reset();nitro=true;i(attached,0x688)=1;nitroHeatHook(engine,nullptr,output);assert(f(engine,0xb0)==99); // Shift
    reset();nitro=true;ptr(physics,0x1150,nullptr);nitroHeatHook(engine,nullptr,output);assert(f(engine,0xb0)==99);
    reset();nitro=true;for(auto &r:regions)if(r.p==engine)r.write=false;
    nitroHeatHook(engine,nullptr,output);assert(f(engine,0xb0)==99); // no mod write on read-only region
    reset();ramUpdateHook(abilities+0xb0,nullptr,input);assert(ramChargeSeen==2 && f(abilities+0xb0,0x38)==3);
    reset();ram=true;for(int k=0;k<10000;k++)ramUpdateHook(abilities+0xb0,nullptr,input);
    assert(ramChargeSeen==60 && f(abilities+0xb0,0x38)==60);
    i(abilities+0xb0,4)=2;f(input,0x40)=10;ramUpdateHook(abilities+0xb0,nullptr,input);
    assert(f(input,0x40)==16 && f(input,0x44)==4 && f(input,0x48)==2); // existing 10 not doubled
    ram=false;ramUpdateHook(abilities+0xb0,nullptr,input);assert(f(input,0x40)==19);
    reset();ram=true;ptr(input,0x98,other);ramUpdateHook(abilities+0xb0,nullptr,input);assert(ramChargeSeen==2);
    reset();ram=true;ramUpdateHook(abilities+0x58,nullptr,input);assert(ramChargeSeen==0); // old wrong boost object excluded
    reset();collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==0 && nativeContacts==1);
    oneHit=true;collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);
    assert(setters==1 && damaged==other && f(other,0x508)==1 && f(car,0x508)==0);
    collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==1); // already wrecked
    reset();oneHit=true;collisionHook(abilities+0x48,nullptr,otherPhysics,physics,0x1234,0x5678);assert(damaged==other);
    reset();oneHit=true;collisionHook(abilities+0x48,nullptr,otherPhysics,otherPhysics,0x1234,0x5678);assert(setters==0);
    reset();oneHit=true;ptr(otherPhysics,0xa50,third);collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==0);
    reset();oneHit=true;collisionHook(abilities+0x48,nullptr,physics,output,0x1234,0x5678);assert(setters==0); // scenery
    reset();oneHit=true;i(attached,0x688)=1;collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==0);
    reset();oneHit=true;f(other,0xc0)=0;collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==0);
    reset();oneHit=true;switchCarOnContact=true;collisionHook(abilities+0x48,nullptr,physics,otherPhysics,0x1234,0x5678);assert(setters==0);
    reset();nitro=ram=oneHit=health=body=speed=true;repairRequested=true;repairFor=1;disableAll();
    assert(!nitro && !ram && !oneHit && !health && !body && !speed && !repairRequested && repairFor==0);
    assert(VehiclePolicy::collisionTarget(1,2,3)==0);
    assert(VehiclePolicy::remaining(1,1,0,10,100)==10);
    assert(!VehiclePolicy::charge(1,0,1));
    float nan=__builtin_nanf("");assert(!VehiclePolicy::charge(1,60,nan));
    float before[]={1,2,3},after[]={2,nan,4},out[3];assert(!VehiclePolicy::amplify(before,after,out));
    puts("PASS: actual 32-bit hook fixtures (100000 nitro ticks, 10000 RAM ticks, scoping/release/OFF/collision/reset cases); NOT gameplay acceptance");
#endif
}
