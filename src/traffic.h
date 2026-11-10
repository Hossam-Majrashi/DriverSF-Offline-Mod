// Included inside VehicleMod. 1..100 filter native candidates; zero alone owns
// the stock traffic on/off policy. No Lua execution/pool writes/vehicle deletion.
#include "traffic_policy.h"
namespace NativeTraffic {
using Query=unsigned char (__thiscall *)(void*,bool,void*);
// Preserve native EAX even for callers that do not consume its result.
using Nearby=void* (__cdecl *)(void*,const void*,float);
using OnOff=void (__thiscall *)(void*,bool);
using Tick=void (__thiscall *)(void*,float);
static OnOff originalOnOff=nullptr;
static Tick originalTick=nullptr;
static std::atomic<bool> zeroReady{false};
static std::atomic<uint32_t>*generation=nullptr;
struct Zero {BYTE*manager=nullptr;bool before=false,owned=false,blocked=false;uint32_t canceled=0;};
static Zero zero;
static Query originalQuery=nullptr;
static Nearby originalNearby=nullptr;
static std::atomic<bool> ready{false},permit{false};
static std::atomic<int> wanted{100};
static std::atomic<DWORD> heartbeat{0},ownerThread{0};
static std::atomic<bool>*control=nullptr;
static std::atomic<unsigned long> rebuilds{0},beforeCount{0},afterCount{0},protectedCount{0},faults{0};
struct Context {
    void*list;TrafficPolicy::Lane protectedLanes[TrafficPolicy::capacity];
    unsigned count=0;bool captured=false,valid=true;
};
static thread_local Context*context=nullptr;
static thread_local void*lastList=nullptr;
static thread_local int lastPercent=100;
static thread_local bool clearedByZero=false;
struct Admission {
    TrafficPolicy::Lane lanes[TrafficPolicy::capacity];unsigned count=0;bool valid=false;
    void clear(){count=0;valid=false;}
    bool remember(const void*list,unsigned n){
        clear();if(n>TrafficPolicy::capacity)return false;
        const auto*p=(const TrafficPolicy::Lane*)list;
        for(unsigned i=0;i<n;i++)if(!p[i].road || p[i].direction>1)return false;
        memcpy(lanes,list,n*sizeof(*p));count=n;valid=true;return true;
    }
};
static thread_local Admission admitted,seen;
static BYTE*expectedManager(){
    BYTE*r=root();if(!r || !readable(r,0x75c))return nullptr;
    BYTE*m=*(BYTE**)(r+0x758);return m && readable(m,0x5ca84d)?m:nullptr;
}
static uint32_t epoch(){return generation?generation->load():0;}
static void restoreZero(){
    Zero old=zero;zero.manager=nullptr;zero.owned=false;
    // The manager identity AND its last-applied value must still be ours.
    if(old.owned && old.manager==expectedManager()) {
        if(old.manager[0x5ca84c]==0)originalOnOff(old.manager,old.before);
        else {zero.blocked=true;zero.canceled=epoch();} // direct native value change also wins
    }
}
// Read-only preflight: ZERO must be supportable before Lua cancels a pursuit.
// No policy write/lease renewal here. set() validates again when committing.
static bool canZero(){
    if(!ready || !zeroReady || (zero.blocked && zero.canceled==epoch()))return false;
    BYTE*m=expectedManager();
    return m && m[0x5ca84c]<=1 && !(zero.manager==m && zero.owned && m[0x5ca84c]!=0);
}
static bool set(bool enabled,int percent=100) {
    if(!enabled){permit=false;restoreZero();return ready;}
    if(percent<0 || percent>100 || !ready || (percent==0 && !zeroReady)){
        permit=false;restoreZero();return false;
    }
    if(percent==0 && zero.blocked && zero.canceled==epoch()){permit=false;return false;}
    if(percent!=0 || zero.canceled!=epoch())zero.blocked=false;
    wanted=percent;ownerThread=GetCurrentThreadId();heartbeat=GetTickCount();permit=true;
    if(percent!=0){restoreZero();return true;}
    BYTE*m=expectedManager();if(!m || m[0x5ca84c]>1){permit=false;restoreZero();return false;}
    if(zero.manager==m && zero.owned && m[0x5ca84c]!=0){
        zero.owned=false;zero.blocked=true;zero.canceled=epoch();permit=false;return false;
    }
    if(zero.manager!=m || !zero.owned){restoreZero();zero.manager=m;zero.before=m[0x5ca84c]!=0;zero.owned=true;
        originalOnOff(m,false);
        if(m[0x5ca84c]!=0 || !zero.owned){permit=false;return false;}
        admitted.clear();seen.clear();lastList=nullptr;lastPercent=100;clearedByZero=true;
        logLine("Native traffic ZERO: stock on/off disabled; native lifecycle owns retirement; player/mission preservation requires gameplay acceptance");
    }
    return true;
}
static int desired() {
    if(!ready || !permit || !control || !control->load() || ownerThread!=GetCurrentThreadId()
        || DWORD(GetTickCount()-heartbeat.load())>500)return 100;
    return wanted.load();
}
static BYTE*expectedList() {
    // Candidate-only builds remain usable even if ZERO signatures fail.
    BYTE*r=root();if(!r || !readable(r,0x75c))return nullptr;
    BYTE*m=*(BYTE**)(r+0x758);
    return m && readable(m,0x503630)?m+0x50182c:nullptr;
}
static void __fastcall onOffHook(void*self,void*,bool enabled){
    if(self==zero.manager && zero.owned){
        zero.owned=false;zero.blocked=true;zero.canceled=epoch();permit=false;
        logLine("Native traffic ZERO: foreign stock policy wins; no restoration/reassertion of old intent");
    }
    originalOnOff(self,enabled);
}
static void __fastcall tickHook(void*self,void*,float time){
    // Stock tick is called even with traffic disabled. Restore an expired ZERO
    // lease BEFORE its enabled check. Never rely on disabled candidate queries.
    if(self==zero.manager && desired()!=0)restoreZero();
    originalTick(self,time);
}
static void* __cdecl nearbyHook(void*list,const void*position,float radius) {
    Context*c=context;
    // Before the first spatial append, the native builder has inserted ALL
    // script interlopers, their adjacent required tracks, and permanent tracks.
    if(c && c->list==list && !c->captured) {
        c->captured=true;
        if(!readable(list,0x1004))c->valid=false;
        else {
            c->count=*(unsigned*)((BYTE*)list+0x1000);
            if(c->count>TrafficPolicy::capacity)c->valid=false;
            else memcpy(c->protectedLanes,list,c->count*sizeof(TrafficPolicy::Lane));
        }
    }
    return originalNearby(list,position,radius);
}
static unsigned char __fastcall queryHook(void*self,void*,bool refresh,void*interlopers) {
    // Other callers/instances are delegated unchanged. Rebuilds are requested
    // through the native refresh argument, never by patching its cache fields.
    int percent=desired();
    // Zero uses the stock manager switch, not a destructive empty candidate
    // list (which could remove required tracks). Delegate any special query.
    if(percent==0)return originalQuery(self,refresh,interlopers);
    if(percent==100 && lastPercent==100){if(self==expectedList())clearedByZero=false;return originalQuery(self,refresh,interlopers);}
    if(self!=expectedList() || !readable(self,0x1e0a))return originalQuery(self,refresh,interlopers);
    bool changed=self!=lastList || percent!=lastPercent;
    bool unlocked=*((BYTE*)self+0x1e09)==0;
    // On activation / return from normal traffic, grandfather the cached native
    // groups BEFORE rebuilding. These may already contain visible cars. Do not
    // retroactively cull them just because the user chose a lower level.
    if(percent<100 && (lastPercent==100 || lastList!=self || !admitted.valid))
        admitted.remember(self,clearedByZero?0:*(unsigned*)((BYTE*)self+0x1000));
    Context local{self};Context*previous=context;context=&local;
    unsigned char result=originalQuery(self,refresh || changed,interlopers);
    context=previous;
    if(!result || !unlocked)return result; // native cache lock always wins
    clearedByZero=false;
    bool restoring=lastPercent<100;
    bool hold=lastList==self && percent<=lastPercent && seen.valid;
    lastList=self;lastPercent=100;
    if(percent==100){
        admitted.clear();seen.clear();
        if(restoring)logLine("Native traffic: original candidate list rebuilt; engine refill pending");
        return result;
    }
    // An absent spatial append means ONLY keep-alive entries: protect them all.
    if(!local.captured){
        admitted.remember(self,*(unsigned*)((BYTE*)self+0x1000));seen=admitted;lastPercent=percent;return result;
    }
    if(!local.valid || !writable(self,0x1004) || desired()!=percent){++faults;return result;}
    unsigned count=*(unsigned*)((BYTE*)self+0x1000),before=count;
    if(!admitted.valid){admitted.remember(self,count);lastPercent=percent;return result;}
    Admission current;current.remember(self,count);
    // Previously rejected groups do not become accepted merely because pool
    // growth offers quota headroom. Raising the level explicitly may admit them.
    if(!TrafficPolicy::filter((TrafficPolicy::Lane*)self,count,local.protectedLanes,local.count,percent,admitted.lanes,admitted.count,
        hold?seen.lanes:nullptr,hold?seen.count:0)) {
        ++faults;permit=false;return result;
    }
    *(unsigned*)((BYTE*)self+0x1000)=count;
    admitted.remember(self,count);seen=current;
    lastPercent=percent;++rebuilds;beforeCount=before;afterCount=count;protectedCount=local.count;
    if(changed){char b[220];snprintf(b,sizeof(b),"Native traffic: level=%d/100 candidates=%u -> %u protected-prefix=%u; sticky admission only; existing native candidates retained; gradual density",percent,before,count,local.count);logLine(b);}
    // The UNMODIFIED caller now diffs this sorted list and invokes its own
    // normal lane retirement/creation. Never call those lifecycle methods here.
    return result;
}
static void bind(std::atomic<bool>*enabled,std::atomic<uint32_t>*commands=nullptr) {
    control=enabled;generation=commands;
    const unsigned char querySig[]={0x56,0x8b,0xf1,0x8b,0x86,0x00,0x1e,0x00,0x00,0x85,0xc0,0x74,0x1d,0x80,0xbe,0x08,0x1e,0x00,0x00,0x00,0x75,0x14,0x80,0x7c,0x24,0x08,0x00};
    const unsigned char nearbySig[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x81,0xec,0x5c,0x01,0x00,0x00,0x8b,0x45,0x0c,0xf3,0x0f,0x10,0x40,0x08,0xf3,0x0f,0x10,0x08,0xf3,0x0f,0x10,0x55,0x10};
    const unsigned char callerSig[]={0xf3,0x0f,0x11,0x44,0x24,0x1c,0xd9,0x44,0x24,0x1c,0x51,0x8d,0x44,0x24,0x0c,0xd9,0x1c,0x24,0x50,0x56,0xe8};
    BYTE*q=(BYTE*)uniqueCode(querySig,sizeof(querySig));
    BYTE*n=(BYTE*)uniqueCode(nearbySig,sizeof(nearbySig));
    BYTE*c=(BYTE*)uniqueCode(callerSig,sizeof(callerSig));
    if(!q || !n || !c || !readable(c,25) || c+25+*(int32_t*)(c+21)!=n) {
        logLine("Native traffic unavailable: candidate-builder signatures/call relationship mismatch");return;
    }
    void*targets[]={q,n};void*hooks[]={(void*)queryHook,(void*)nearbyHook};
    void**originals[]={(void**)&originalQuery,(void**)&originalNearby};
    unsigned created=0;bool ok=true;
    for(;created<2;created++)if(MH_CreateHook(targets[created],hooks[created],originals[created])!=MH_OK){ok=false;break;}
    if(ok)for(unsigned i=0;i<2;i++)if(MH_EnableHook(targets[i])!=MH_OK){ok=false;break;}
    if(!ok){for(unsigned i=0;i<created;i++){MH_DisableHook(targets[i]);MH_RemoveHook(targets[i]);}originalQuery=nullptr;originalNearby=nullptr;
        logLine("Native traffic unavailable: isolated hook setup failed; timers/police unchanged");return;}
    ready=true;
    const unsigned char zeroOnOffSig[]={0x8a,0x44,0x24,0x04,0x56,0x8b,0xf1,0x38,0x86,0x4c,0xa8,0x5c,0x00};
    const unsigned char zeroTickSig[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x4c,0x53,0x56,0x8b,0xf1,0x66,0x8b,0x86,0x24,0x18,0x50,0x00};
    void*z=uniqueCode(zeroOnOffSig,sizeof(zeroOnOffSig));void*t=uniqueCode(zeroTickSig,sizeof(zeroTickSig));
    void*zt[]={z,t};void*zh[]={(void*)onOffHook,(void*)tickHook};
    void**zo[]={(void**)&originalOnOff,(void**)&originalTick};
    created=0;ok=z && t;
    if(ok)for(;created<2;created++)if(MH_CreateHook(zt[created],zh[created],zo[created])!=MH_OK){ok=false;break;}
    if(ok)for(unsigned i=0;i<2;i++)if(MH_EnableHook(zt[i])!=MH_OK){ok=false;break;}
    if(!ok){for(unsigned i=0;i<created;i++){MH_DisableHook(zt[i]);MH_RemoveHook(zt[i]);}originalOnOff=nullptr;originalTick=nullptr;
        logLine("Native traffic ZERO unavailable: isolated policy hooks failed; 1..100 filter remains usable");}
    zeroReady=ok;
    logLine("Native traffic review9: 0=stock traffic OFF plus Lua-owned ambient police cancellation/retirement; owned restoration/lease; 1..100=sticky admission, stable rejection, 100=native; rear-camera gameplay acceptance pending");
}
} // namespace NativeTraffic
