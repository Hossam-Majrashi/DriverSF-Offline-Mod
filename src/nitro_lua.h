// Internal to VehicleMod. Nitro uses the native boost-input C callback; World
// uses successful native protected-script returns, never a UI/render/physics call.
// No shared-pool refill or synthetic ability/input invocation.
namespace NitroLua {
using State=void;
using Callback=int (__cdecl *)(State*);
struct Api {
    int (__cdecl *getTop)(State*);
    void (__cdecl *setTop)(State*,int);
    int (__cdecl *type)(State*,int);
    int (__cdecl *toBool)(State*,int);
    void (__cdecl *pushBool)(State*,int);
    void (__cdecl *pushValue)(State*,int);
    void (__cdecl *pushClosure)(State*,Callback,int);
    void (__cdecl *getField)(State*,int,const char*);
    void (__cdecl *setField)(State*,int,const char*);
    int (__cdecl *loadBuffer)(State*,const char*,size_t,const char*);
    int (__cdecl *pcall)(State*,int,int,int);
    int (__cdecl *checkStack)(State*,int);
    const char* (__cdecl *toString)(State*,int,size_t*);
};
static Api api{};
#ifndef NITRO_LUA_TEST
static decltype(api.pcall) originalPcall=nullptr;
#endif
static Callback originalInput=nullptr;
static std::atomic<bool> ready{false}, scriptReady{false};
static std::atomic<unsigned long> calls{0}, installs{0}, errors{0};
static constexpr int registry=-10000;
static constexpr char key[]="DriverSF.mod.nitro.v7.2.installer";
#include "../build/nitro_script.inc"
#ifndef NITRO_LUA_TEST
#include "world_lua.h"
#endif
static int __cdecl enabled(State *L) {
    api.pushBool(L,ready && nitro && playerVehicle()!=nullptr);
    return 1;
}
static void sync(State *L) {
    int top=api.getTop(L),error=0;
    api.getField(L,registry,key);
    if(api.type(L,-1)!=6) {
        api.setTop(L,top);
        error=api.loadBuffer(L,source,sizeof(source)-1,"@mod/nitro_script.lua");
        if(!error) {
            api.pushClosure(L,enabled,0);
            error=api.pcall(L,1,1,0); // returns installer, not an unprotected call
        }
        if(!error && api.type(L,-1)!=6)error=-1;
        if(!error) {
            api.pushValue(L,-1);
            api.setField(L,registry,key); // per-VM cache; GC roots both closures
        }
    }
    bool installed=false;
    if(!error) {
        error=api.pcall(L,0,1,0); // idempotent; repairs bindings after module reload
        if(!error)installed=api.toBool(L,-1)!=0;
    }
    api.setTop(L,top); // keep all native arguments/results on every path
    bool was=scriptReady.exchange(installed);
    if(installed && !was){++installs;logLine("nitro v7.2: Lua consumption/duration delegates installed; gameplay verification pending");}
    if(error) {
        nitro=false;
        if(++errors<=5){char b[120];snprintf(b,sizeof(b),"nitro v7.2: protected Lua setup error=%d; Nitro switched OFF",error);logLine(b);}
    }
}
static int __cdecl inputHook(State *L) {
    ++calls;
    if(ready && nitro)sync(L);
    return originalInput(L); // native Lua C callback: standard cdecl ABI
}
#ifndef NITRO_LUA_TEST
static thread_local unsigned scriptDepth=0;
static int __cdecl scriptStepHook(State*L,int args,int results,int errorFunction) {
    // Work only after the OUTERMOST protected call, not a script's nested pcall
    // during goal iteration/module cleanup. Retain depth through World work so
    // its own nested native callbacks cannot schedule more work either.
    struct Depth {Depth(){++scriptDepth;}~Depth(){--scriptDepth;}}depth;
    const int error=originalPcall(L,args,results,errorFunction);
    if(scriptDepth==1 && !error && ready && World::bound && World::needed() && api.checkStack && api.checkStack(L,24)
        && World::gameVm(L))World::sync(L);
    return error;
}
static bool scriptsSupported() {
    WCHAR path[MAX_PATH];DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    WCHAR *slash=wcsrchr(path,L'\\');if(!slash)return false;
    constexpr WCHAR relative[]=L"\\media\\ScriptsLuaScripts.fchunk";
    if(size_t(slash-path)+sizeof(relative)/sizeof(WCHAR)>MAX_PATH)return false;
    memcpy(slash,relative,sizeof(relative));
    return expectedFileHash(path,"17fb9c7208f0b936d3977b04077f7cc49243b14fd3a0112d58f465c7984aa8c0");
}
static void bind() {
    if(!scriptsSupported()){logLine("nitro v7.2 disabled: script-container hash/path mismatch");return;}
    const unsigned char getTopSig[]={0x8b,0x4c,0x24,0x04,0x8b,0x41,0x08,0x2b,0x41,0x0c,0xc1,0xf8,0x03,0xc3};
    const unsigned char setTopSig[]={0x8b,0x4c,0x24,0x08,0x8b,0x44,0x24,0x04,0x85,0xc9,0x7c,0x37,0x8b,0x50,0x0c,0x03,0xc9,0x03,0xc9,0x03,0xc9,0x03,0xd1,0x39};
    const unsigned char typeSig[]={0x8b,0x4c,0x24,0x08,0x8b,0x54,0x24,0x04,0xe8,0xa3,0xfd,0xff,0xff,0x3d,0x18,0x6f,0xde,0x00,0x75,0x04,0x83,0xc8,0xff,0xc3};
    const unsigned char toBoolSig[]={0x8b,0x4c,0x24,0x08,0x8b,0x54,0x24,0x04,0xe8,0xd3,0xfc,0xff,0xff,0x8b,0x48,0x04,0x85,0xc9,0x74,0x10,0x83,0xf9,0x01,0x75};
    const unsigned char pushBoolSig[]={0x8b,0x44,0x24,0x04,0x8b,0x48,0x08,0x33,0xd2,0x39,0x54,0x24,0x08,0xc7,0x41,0x04,0x01,0x00,0x00,0x00,0x0f,0x95,0xc2,0x89};
    const unsigned char pushValueSig[]={0x8b,0x4c,0x24,0x08,0x56,0x8b,0x74,0x24,0x08,0x8b,0xd6,0xe8,0xd0,0xfd,0xff,0xff,0x8b,0x4e,0x08,0x8b,0x10,0x89,0x11,0x8b};
    const unsigned char pushClosureSig[]={0x53,0x55,0x56,0x8b,0x74,0x24,0x10,0x8b,0x46,0x10,0x8b,0x48,0x44,0x57,0x3b,0x48,0x40,0x72,0x09,0x56,0xe8,0xd7,0x16,0xff};
    const unsigned char getFieldSig[]={0x8b,0x4c,0x24,0x08,0x83,0xec,0x08,0x53,0x56,0x8b,0x74,0x24,0x14,0x57,0x8b,0xd6,0xe8,0x6b,0x9b,0xff,0xff,0x8b,0x54,0x24};
    const unsigned char setFieldSig[]={0x8b,0x4c,0x24,0x08,0x83,0xec,0x08,0x53,0x56,0x8b,0x74,0x24,0x14,0x57,0x8b,0xd6,0xe8,0xdb,0x9a,0xff,0xff,0x8b,0x54,0x24};
    const unsigned char loadBufferSig[]={0x83,0xec,0x1c,0x8b,0x44,0x24,0x24,0x8b,0x4c,0x24,0x28,0x89,0x04,0x24,0x8b,0x44,0x24,0x2c,0x33,0xd2,0x89,0x4c,0x24,0x04};
    const unsigned char pcallSig[]={0x8b,0x4c,0x24,0x10,0x83,0xec,0x08,0x56,0x8b,0x74,0x24,0x10,0x85,0xc9,0x75,0x04,0x33,0xc9,0xeb,0x0c,0x8b,0xd6,0xe8,0x05};
    const unsigned char boostInputSig[]={0x56,0x8b,0x74,0x24,0x08,0x57,0x56,0x33,0xff,0xe8,0xf2,0x0f,0xfa,0xff,0x83,0xc4,0x04,0x83,0xf8,0x01,0x7c,0x1c,0x6a,0x01};
    void *entries[]={uniqueCode(getTopSig,sizeof(getTopSig)),uniqueCode(setTopSig,sizeof(setTopSig)),uniqueCode(typeSig,sizeof(typeSig)),uniqueCode(toBoolSig,sizeof(toBoolSig)),uniqueCode(pushBoolSig,sizeof(pushBoolSig)),uniqueCode(pushValueSig,sizeof(pushValueSig)),uniqueCode(pushClosureSig,sizeof(pushClosureSig)),uniqueCode(getFieldSig,sizeof(getFieldSig)),uniqueCode(setFieldSig,sizeof(setFieldSig)),uniqueCode(loadBufferSig,sizeof(loadBufferSig)),uniqueCode(pcallSig,sizeof(pcallSig)),uniqueCode(boostInputSig,sizeof(boostInputSig))};
    for(auto entry:entries)if(!entry){logLine("nitro v7.2 disabled: Lua signature missing/non-unique");return;}
    api.getTop=reinterpret_cast<decltype(api.getTop)>(entries[0]);
    api.setTop=reinterpret_cast<decltype(api.setTop)>(entries[1]);
    api.type=reinterpret_cast<decltype(api.type)>(entries[2]);
    api.toBool=reinterpret_cast<decltype(api.toBool)>(entries[3]);
    api.pushBool=reinterpret_cast<decltype(api.pushBool)>(entries[4]);
    api.pushValue=reinterpret_cast<decltype(api.pushValue)>(entries[5]);
    api.pushClosure=reinterpret_cast<decltype(api.pushClosure)>(entries[6]);
    api.getField=reinterpret_cast<decltype(api.getField)>(entries[7]);
    api.setField=reinterpret_cast<decltype(api.setField)>(entries[8]);
    api.loadBuffer=reinterpret_cast<decltype(api.loadBuffer)>(entries[9]);
    api.pcall=reinterpret_cast<decltype(api.pcall)>(entries[10]);
    void *target=entries[11];
    if(MH_CreateHook(target,(void*)inputHook,(void**)&originalInput)!=MH_OK) {
        logLine("nitro v7.2 disabled: Lua input hook creation failed");return;
    }
    if(MH_EnableHook(target)!=MH_OK) {
        MH_RemoveHook(target);logLine("nitro v7.2 disabled: Lua input hook enable failed");return;
    }
    // World must update without a Nitro key event. Failure of this independent
    // hook leaves World unavailable, while retaining the existing Nitro path.
    // Unlike a Lua C callback, a host pcall return does not promise MINSTACK
    // spare slots. Verify/use native lua_checkstack before any extra API pushes.
    const unsigned char checkStackSig[]={0x8b,0x44,0x24,0x08,0x56,0x8b,0x74,0x24,0x08,0x8b,0x4e,0x08,0x8b,0xd1,0x2b,0x56,0x0c,0xc1,0xfa,0x03,0x03,0xd0,0x81,0xfa,0x00,0x08,0x00,0x00};
    void*stackCheck=uniqueCode(checkStackSig,sizeof(checkStackSig));
    api.checkStack=reinterpret_cast<decltype(api.checkStack)>(stackCheck);
    const unsigned char toStringSig[]={0x56,0x8b,0x74,0x24,0x08,0x57,0x8b,0x7c,0x24,0x10,0x8b,0xcf,0x8b,0xd6,0xe8,0x8d,0x64,0xff,0xff,0x83,0x78,0x04,0x04};
    api.toString=reinterpret_cast<decltype(api.toString)>(uniqueCode(toStringSig,sizeof(toStringSig)));
    void*scriptTarget=entries[10];
    if(stackCheck && api.toString && MH_CreateHook(scriptTarget,(void*)scriptStepHook,(void**)&originalPcall)==MH_OK) {
        api.pcall=originalPcall;
        if(MH_EnableHook(scriptTarget)==MH_OK)World::bound=true;
        else {MH_RemoveHook(scriptTarget);api.pcall=reinterpret_cast<decltype(api.pcall)>(scriptTarget);originalPcall=nullptr;}
    }
    if(World::bound){NativeTraffic::bind(&World::traffic,&World::streetCommand);NativePassengers::bind();NativeSignals::bind();logLine("World v7.14 review10: Paranoia live 120s HUD linked to 123s failure deadline; actual suspicion payload deltas held; legacy timers/traffic ZERO/weather/passengers/indicators retained; gameplay acceptance pending");}
    else logLine("World v7.14 unavailable: protected-script hook failed; Nitro remains independent");
    ready=true;logLine("nitro v7.2: Lua input callback bound; script setup deferred until Nitro ON");
}
#endif
} // namespace NitroLua
