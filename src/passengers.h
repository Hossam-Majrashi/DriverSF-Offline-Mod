// Included inside VehicleMod. Native per-seat occupant identity changes only.
// 0xffff is the empty profile: 8ddf50 destroys the old seat character and skips
// creating a replacement. Seat zero (driver) is NEVER changed by this feature.
namespace NativePassengers {
using Setter=void (__thiscall *)(void*,int,uint16_t,bool);
using Getter=uint16_t (__thiscall *)(void*,int);
using CheckUserdata=void* (__cdecl *)(void*,int,const char*);
static Setter originalSetter=nullptr;
static Getter getter=nullptr;
static CheckUserdata checkUserdata=nullptr;
using AudioRefresh=void (__thiscall *)(void*);
static AudioRefresh refreshRoster=nullptr,cleanAmbientQueue=nullptr;
static std::atomic<bool> ready{false},audioReady{false},active{false};
struct Record {void*vehicle=nullptr;uint16_t before[3]{};bool owned[3]{};bool audioPending=false;};
static Record record;
static constexpr uint16_t empty=0xffff;
static bool nativeBackup(void*v,int seat){
    // Setter's backup bit means a native temporary character override is live.
    // Our false-backup call would clear it: never take over that seat.
    return (*(const uint16_t*)((const unsigned char*)v+0x5d4)&(1u<<(seat+4)))!=0;
}
static void discard(){record={};active=false;}
static void __fastcall setterHook(void*self,void*,int seat,uint16_t profile,bool backup) {
    // Foreign native writes win even if they write the same empty value. Calls
    // we own use the trampoline directly, not this detour.
    if(self==record.vehicle && seat>=1 && seat<=3){
        if(record.audioPending)logLine("Passenger audio: deferred refresh canceled by a newer native seat write");
        record.owned[seat-1]=false;record.audioPending=false;
    }
    originalSetter(self,seat,profile,backup);
}
static bool refreshAudio(void*vehicle,bool clearQueue){
    // LIAB caches the talking passenger independently of the visible character.
    // Touch only its current PLAYER car, never debug/override/disabled contexts.
    if(!audioReady || vehicle!=playerVehicle())return false;
    BYTE*r=root();if(!r || !readable(r,0x734))return false;
    BYTE*m=*(BYTE**)(r+0x730);
    if(!m || !readable(m,0x1c600) || *(void**)(m+0x1c29c)!=vehicle
        || m[0x1c3e0] || m[0x1c3f9] || m[0x1c3dd])return false;
    // Stock selective cleanup retains native protected/important lines. Do not
    // call StopCommentary/DisableSpeech: those have much broader effects.
    if(clearQueue)cleanAmbientQueue(m);
    refreshRoster(m);
    logLine(clearQueue?"Passenger audio: matching player LIAB selective queue cleanup + speaker roster refreshed; protected lines retained; audible acceptance pending"
        :"Passenger audio: owned free-drive occupant restoration; speaker roster refreshed without stopping dialogue");
    return true;
}
static bool apply(void*vehicle,bool clear,bool audioSafe=false) {
    if(!ready || !vehicle || !readable(vehicle,0x5f4))return false;
    if(!clear) {
        // Caller has validated the live Lua vehicle/agent identity before
        // releasing a previous car. Never dereference a saved raw pointer.
        if(vehicle!=record.vehicle)return false;
        bool restored=false;
        for(int seat=1;seat<=3;seat++)if(record.owned[seat-1]
            && !nativeBackup(vehicle,seat) && getter(vehicle,seat)==empty && record.before[seat-1]!=empty) {
            record.owned[seat-1]=false;
            originalSetter(vehicle,seat,record.before[seat-1],false);restored=true;
        }
        if(audioSafe && restored)refreshAudio(vehicle,false);
        discard();return true;
    }
    if(vehicle!=playerVehicle())return false; // only the attached player's car
    if(record.vehicle==vehicle){
        if(record.audioPending && refreshAudio(vehicle,true))record.audioPending=false;
        return true; // no character repaint after a foreign write
    }
    discard();record.vehicle=vehicle;active=true;
    for(int seat=1;seat<=3;seat++) {
        record.before[seat-1]=getter(vehicle,seat);
        record.owned[seat-1]=record.before[seat-1]!=empty && !nativeBackup(vehicle,seat); // publish before calls
        if(record.owned[seat-1]) {
            record.audioPending=true;
            originalSetter(vehicle,seat,empty,false);
            if(getter(vehicle,seat)!=empty){record.owned[seat-1]=false;return false;}
        }
    }
    if(record.audioPending && refreshAudio(vehicle,true))record.audioPending=false;
    if(record.audioPending)logLine("Passenger audio: removed passenger; waiting for a verified matching player LIAB context (or audio backend unavailable)");
    return true;
}
static void bind() {
    const unsigned char passengerSetSig[]={0x8a,0x54,0x24,0x0c,0x53,0x8b,0x5c,0x24,0x08,0x56,0x57,0x8b,0xf1,0x84,0xd2,0x74,0x10,0x66,0x8b,0x84,0x5e};
    const unsigned char passengerGetSig[]={0x8b,0x44,0x24,0x04,0x66,0x8b,0x84,0x41,0xcc,0x05,0x00,0x00,0xc2,0x04,0x00};
    const unsigned char passengerUserdataSig[]={0x53,0x55,0x8b,0x6c,0x24,0x10,0x56,0x8b,0x74,0x24,0x10,0x57,0x8b,0xcd,0x8b,0xd6,0xe8,0x3b,0x29,0xff,0xff};
    void*s=uniqueCode(passengerSetSig,sizeof(passengerSetSig));
    getter=(Getter)uniqueCode(passengerGetSig,sizeof(passengerGetSig));
    checkUserdata=(CheckUserdata)uniqueCode(passengerUserdataSig,sizeof(passengerUserdataSig));
    if(!s || !getter || !checkUserdata){logLine("Passenger control unavailable: native signatures missing/nonunique");return;}
    if(MH_CreateHook(s,(void*)setterHook,(void**)&originalSetter)!=MH_OK)return;
    if(MH_EnableHook(s)!=MH_OK){MH_RemoveHook(s);originalSetter=nullptr;return;}
    ready=true;
    const unsigned char passengerRosterSig[]={0x81,0xec,0x84,0x00,0x00,0x00,0x53,0x8b,0xd9,0x80,0xbb,0xf9,0xc3,0x01,0x00,0x01,0x89,0x5c,0x24,0x10};
    const unsigned char passengerQueueCleanupSig[]={0x51,0x53,0x55,0x56,0x8b,0xd9,0x57,0x33,0xff,0x33,0xf6,0x89,0xbb,0xc0,0xc3,0x01,0x00,0x89,0xbb,0xc4,0xc3,0x01,0x00};
    refreshRoster=(AudioRefresh)uniqueCode(passengerRosterSig,sizeof(passengerRosterSig));
    cleanAmbientQueue=(AudioRefresh)uniqueCode(passengerQueueCleanupSig,sizeof(passengerQueueCleanupSig));
    audioReady=refreshRoster && cleanAmbientQueue;
    if(!audioReady)logLine("Passenger audio unavailable: selective LIAB refresh/cleanup signatures failed; visible removal independent");
    logLine("Passengers review8: native per-seat removal plus player LIAB roster refresh/selective ambient queue cleanup; no global speech mute; audio acceptance pending");
}
}
