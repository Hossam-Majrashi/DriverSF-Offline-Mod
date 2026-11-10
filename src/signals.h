// Included inside VehicleMod. Original GameVehicle::setLights(3,mask,state).
// LeftIndicator=8, RightIndicator=16. Native lamp animation owns blink timing.
namespace NativeSignals {
using Setter=void (__thiscall *)(void*,int,uint32_t,bool);
static Setter originalSetter=nullptr;
static std::atomic<bool>ready{false},enabled{false},active{false},permit{false};
static std::atomic<DWORD>heartbeat{0};
static std::atomic<uintptr_t>authorized{0};
static std::atomic<uint32_t>epoch{0},mode{0};
static constexpr uint32_t masks[2]={8,16};
struct Record{void*vehicle=nullptr;bool before[2]{},applied[2]{},owned[2]{};};
static Record record;
struct Request{void*vehicle=nullptr;DWORD deadline=0;uint32_t epoch=0,action=0;bool pending=false;};
static Request inbox;
static std::atomic_flag inboxLock=ATOMIC_FLAG_INIT;
static void cancel(){++epoch;permit=false;authorized=0;}
static void discard(){cancel();record={};active=false;mode=0;}
static bool usable(){return ready && enabled && permit && DWORD(GetTickCount()-heartbeat.load())<=500
    && authorized.load()!=0 && authorized.load()==uintptr_t(playerVehicle());}
static bool request(unsigned action){
    if(action>3 || !usable() || inboxLock.test_and_set(std::memory_order_acquire))return false;
    inbox={playerVehicle(),GetTickCount()+500,epoch.load(),action,true};
    inboxLock.clear(std::memory_order_release);return true;
}
static void __fastcall setterHook(void*self,void*,int operation,uint32_t mask,bool state){
    if(self==record.vehicle && (mask&24)){
        for(unsigned i=0;i<2;i++)if(mask&masks[i])record.owned[i]=false;
        ++epoch; // a queued older UI/input command must not overwrite this write
    }
    originalSetter(self,operation,mask,state);
}
static uint32_t lights(void*v){return *(const uint32_t*)((const BYTE*)v+0x530)&24;}
static bool release(void*v){
    cancel();if(!ready || v!=record.vehicle || !v || !readable(v,0x5f4))return false;
    for(unsigned i=0;i<2;i++)if(record.owned[i] && record.before[i]!=record.applied[i] && bool(lights(v)&masks[i])==record.applied[i]){
        record.owned[i]=false;originalSetter(v,3,masks[i],record.before[i]);
    }
    record={};active=false;mode=0;return true;
}
static bool pump(void*v,bool newOwner=false){
    if(newOwner)cancel(); // fresh/replaced Lua owner cannot replay an older queued action
    if(!ready || !enabled || !v || v!=playerVehicle() || !readable(v,0x5f4))return false;
    if(record.vehicle!=v){discard();record.vehicle=v;}
    heartbeat=GetTickCount();authorized=uintptr_t(v);permit=true;active=true;
    Request r;
    if(!inboxLock.test_and_set(std::memory_order_acquire)){
        r=inbox;inbox.pending=false;inboxLock.clear(std::memory_order_release);
    }
    if(r.pending && r.vehicle==v && r.epoch==epoch && int32_t(r.deadline-GetTickCount())>=0){
        uint32_t old=lights(v),target=r.action==1?8:r.action==2?16:r.action==3?24:0;
        if(target==old)target=0; // press same signal/hazards again to turn it OFF
        for(unsigned i=0;i<2;i++){
            if(epoch!=r.epoch)break; // native re-entry wins over the remaining command
            bool before=(old&masks[i])!=0,value=(target&masks[i])!=0;
            if(record.owned[i] && before!=record.applied[i])record.owned[i]=false;
            if(!record.owned[i])record.before[i]=before;
            record.owned[i]=true;record.applied[i]=value;
            if(before!=value)originalSetter(v,3,masks[i],value);
        }
    }
    uint32_t current=lights(v);mode=current==8?1:current==16?2:current==24?3:0;
    return true;
}
static void bind(){
    const unsigned char indicatorSetSig[]={0x8b,0x44,0x24,0x04,0x83,0xe8,0x02,0x56,0x8b,0xf1,0x74,0x6a,0x83,0xe8,0x01,0x53,0x8b,0x5c,0x24,0x14,0x57};
    void*s=uniqueCode(indicatorSetSig,sizeof(indicatorSetSig));
    if(!s || !NativePassengers::checkUserdata){logLine("Indicators unavailable: native setter/userdata signatures failed");return;}
    if(MH_CreateHook(s,(void*)setterHook,(void**)&originalSetter)!=MH_OK)return;
    if(MH_EnableHook(s)!=MH_OK){MH_RemoveHook(s);originalSetter=nullptr;return;}
    ready=true;logLine("Indicators review8: player native left/right/hazards, original blink timing, command/vehicle/lease guards; gameplay acceptance pending");
}
}
