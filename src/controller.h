// Native input update delegation, NOT synthetic input or device emulation.
namespace ControllerMod {
using Update=bool (__thiscall *)(void*,bool);
static Update originalUpdate=nullptr;
static DWORD lastProbe=0;static bool sampled=false,wasPresent=false;
static std::atomic<unsigned long>rescans{0};
static bool __fastcall update(void*self,void*,bool force) {
    // The original update already has a force-enumeration argument. Only
    // forward that argument on a REAL slot-0 connection edge / missed device.
    // Do not invoke the native enumerator ourselves or edit its device list.
    DWORD now=GetTickCount();
    if(::readable(self,0x65a) && *((BYTE*)self+4)
        && (!sampled || DWORD(now-lastProbe)>=500)) {
        lastProbe=now;XINPUT_CAPABILITIES caps{};
        bool present=XInputGetCapabilities(0,XINPUT_FLAG_GAMEPAD,&caps)==ERROR_SUCCESS;
        uint16_t count;memcpy(&count,(BYTE*)self+0x656,2);
        if(present && (!sampled || !wasPresent || count==0)) {
            force=true;++rescans;
            if(!sampled || !wasPresent)logLine("Controller: genuine XInput slot 0 connected; forwarding native force-enumeration argument; no input generated");
        }
        sampled=true;wasPresent=present;
    }
    return originalUpdate(self,force); // exactly one naturally scheduled call
}
static void bind() {
    if(!::bound){logLine("Controller hotplug unavailable: supported offline executable not bound");return;}
    // Supported exe: 0x852f10..0x8531ed, thiscall, bool arg, ret 4. The full
    // fingerprint includes the native enumeration branch and its normal call.
    const unsigned char sig[]={0x83,0xec,0x14,0x56,0x8b,0xf1,0x80,0x7e,0x04,0x00,0x75,0x09,0x32,0xc0,0x5e,0x83,0xc4,0x14,0xc2,0x04,0x00,0xff,0x86,0x40,0x06,0x00,0x00};
    void*target=VehicleMod::uniqueCode(sig,sizeof(sig));
    if(!target || !::readable(target,0x2de))return;
    uint32_t hash=2166136261u;for(size_t i=0;i<0x2de;i++)hash=(hash^((BYTE*)target)[i])*16777619u;
    if(hash!=0x483ed10au){logLine("Controller hotplug unavailable: native input-update fingerprint mismatch");return;}
    if(MH_CreateHook(target,(void*)update,(void**)&originalUpdate)==MH_OK) {
        if(MH_EnableHook(target)==MH_OK)logLine("Controller hotplug: verified native input-update delegate bound; first XInput slot only; in-game acceptance pending");
        else {MH_RemoveHook(target);originalUpdate=nullptr;}
    }
}
} // namespace ControllerMod
