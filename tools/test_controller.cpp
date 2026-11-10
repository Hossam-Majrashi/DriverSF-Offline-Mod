// Actual native-update forwarding and IAT replacement bodies; MOCK OS/native.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <strings.h>
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
#define WINAPI __attribute__((stdcall))
using BYTE=unsigned char;using WORD=uint16_t;using DWORD=uint32_t;
static constexpr DWORD ERROR_SUCCESS=0,XINPUT_FLAG_GAMEPAD=1,PAGE_READWRITE=4;
struct XINPUT_CAPABILITIES{BYTE Type,SubType;WORD Flags;BYTE rest[16];};
struct XINPUT_STATE{DWORD dwPacketNumber;BYTE Gamepad[12];};
static BYTE manager[0x660];static DWORD tick=100;static bool present=false,calledForce=false;static unsigned probes=0,nativeCalls=0;
static DWORD GetTickCount(){return tick;}
static bool readable(const void*p,size_t n){return p==manager&&n<=sizeof(manager);}
static void logLine(const char*){}
static DWORD WINAPI XInputGetCapabilities(DWORD index,DWORD flags,XINPUT_CAPABILITIES*out){assert(index==0&&flags==1&&out);++probes;out->Type=1;out->SubType=1;return present?0:1167;}
static DWORD WINAPI XInputGetState(DWORD index,XINPUT_STATE*out){assert(index==0);if(!present)return 1167;if(!out)return 87;out->dwPacketNumber=123;memset(out->Gamepad,0x45,sizeof(out->Gamepad));return 0;}
static bool __thiscall native(void*self,bool force){assert(self==manager);++nativeCalls;calledForce=force;return force;}
#include "controller_under_test.h"
// A tiny PE-layout fixture for the actual production IAT walker. All structs
// keep the Windows PE32 field offsets used by this function.
struct IMAGE_DOS_HEADER{BYTE pad[60];int32_t e_lfanew;};
struct Dir{DWORD VirtualAddress,Size;};
struct Opt{BYTE pad[96];Dir DataDirectory[16];};
struct IMAGE_NT_HEADERS{BYTE pad[24];Opt OptionalHeader;};
struct IMAGE_IMPORT_DESCRIPTOR{DWORD OriginalFirstThunk,TimeDateStamp,ForwarderChain,Name,FirstThunk;};
struct IMAGE_THUNK_DATA{union{DWORD AddressOfData,Ordinal,Function;}u1;};
struct IMAGE_IMPORT_BY_NAME{WORD Hint;BYTE Name[1];};
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_SNAP_BY_ORDINAL(x) (((x)&0x80000000)!=0)
#define IMAGE_ORDINAL(x) ((x)&0xffff)
#define _stricmp strcasecmp
static BYTE*image;static bool protect=true;static unsigned writes=0;
static bool VirtualProtect(void*,size_t n,DWORD,DWORD*old){assert(n==4);*old=4;++writes;return protect;}
static std::atomic<bool>visible{false};
namespace SignalControls {template<class T>static void filterPad(T&){} }
#include "controller_iat_under_test.h"
static void initImage(std::vector<BYTE>&b,bool ordinal=true){
 b.assign(2048,0);image=b.data();((IMAGE_DOS_HEADER*)image)->e_lfanew=64;
 auto nt=(IMAGE_NT_HEADERS*)(image+64);nt->OptionalHeader.DataDirectory[1].VirtualAddress=400;
 auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(image+400);imp->OriginalFirstThunk=600;imp->FirstThunk=640;imp->Name=500;
 strcpy((char*)image+500,"XINPUT1_3.dll");auto names=(IMAGE_THUNK_DATA*)(image+600),slots=(IMAGE_THUNK_DATA*)(image+640);
 names[0].u1.Ordinal=0x80000004;slots[0].u1.Function=0x11111111;
 names[1].u1.Ordinal=ordinal?0x80000002:700;slots[1].u1.Function=0x22222222;
 strcpy((char*)((IMAGE_IMPORT_BY_NAME*)(image+700))->Name,"XInputGetState");
}
int main(){
 ControllerMod::originalUpdate=native;manager[4]=1;
 assert(!ControllerMod::update(manager,nullptr,false)&&probes==1&&nativeCalls==1);
 present=true;tick+=499;assert(!ControllerMod::update(manager,nullptr,false)&&probes==1);
 tick++;assert(ControllerMod::update(manager,nullptr,false)&&calledForce&&probes==2);
 uint16_t one=1;memcpy(manager+0x656,&one,2);tick+=500;
 assert(!ControllerMod::update(manager,nullptr,false));
 present=false;tick+=500;assert(!ControllerMod::update(manager,nullptr,false));
 present=true;tick+=500;assert(ControllerMod::update(manager,nullptr,false));
 assert(ControllerMod::update(manager,nullptr,true)); // native force never lost
 unsigned before=probes;manager[4]=0;tick+=500;assert(!ControllerMod::update(manager,nullptr,false)&&probes==before);
 manager[4]=1;tick=0xfffffff0;ControllerMod::lastProbe=tick;ControllerMod::sampled=true;tick=0x200;
 ControllerMod::update(manager,nullptr,false);assert(probes==before+1); // DWORD wrap
 for(bool ordinal:{false,true}){
  std::vector<BYTE>b;initImage(b,ordinal);writes=0;
  auto slots=(IMAGE_THUNK_DATA*)(image+640);
  assert(replaceImport("xinput1_3.DLL","XInputGetState",(void*)0x12345678,2)==(void*)0x22222222);
  assert(slots[0].u1.Function==0x11111111&&slots[1].u1.Function==0x12345678&&writes==2);
  initImage(b,ordinal);protect=false;assert(!replaceImport("XINPUT1_3.dll","XInputGetState",(void*)0x12345678,2));
  assert(((IMAGE_THUNK_DATA*)(image+640))[1].u1.Function==0x22222222);protect=true;
 }
 std::vector<BYTE>b;initImage(b);assert(!replaceImport("other.dll","XInputGetState",(void*)1,2));
 assert(!replaceImport("XINPUT1_3.dll","XInputGetState",(void*)1));
 XINPUT_STATE state;present=true;visible=false;assert(quietXInput(0,&state)==0&&state.dwPacketNumber==123&&state.Gamepad[0]==0x45);
 visible=true;assert(quietXInput(0,&state)==0&&state.dwPacketNumber==123);
 for(auto c:state.Gamepad)assert(c==0);present=false;assert(quietXInput(0,&state)==1167);
 assert(ControllerMod::rescans>=2);
 puts("Controller actual 32-bit forwarding/IAT bodies: real-device edges via MOCK XInput, first late connect/reconnect, no generated input, native force/results, poll interval/wrap, named/ordinal imports, protection failure, F1 suppression passed. NOT hardware gameplay.");
}
