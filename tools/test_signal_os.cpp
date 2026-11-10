// Actual production OS filters + DI bodies, MOCK OS only. No input injection.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define WINAPI __attribute__((stdcall))
using BYTE=unsigned char;using PBYTE=BYTE*;using WORD=uint16_t;using DWORD=uint32_t;using UINT=unsigned;using SHORT=int16_t;using BOOL=int;using HRESULT=int32_t;
static constexpr BOOL TRUE=1;static constexpr HRESULT DI_OK=0,E_FAIL=-1;
#define SUCCEEDED(x) ((x)>=0)
static constexpr DWORD ERROR_SUCCESS=0;static constexpr UINT MAPVK_VK_TO_VSC_EX=4;
static constexpr int VK_MENU=0x12,VK_LWIN=0x5b,VK_RWIN=0x5c;static bool systemChord=false;
struct XINPUT_GAMEPAD{WORD wButtons;BYTE bLeftTrigger,bRightTrigger;int16_t axes[4];};
struct XINPUT_STATE{DWORD dwPacketNumber;XINPUT_GAMEPAD Gamepad;};
struct DIDEVICEOBJECTDATA{DWORD dwOfs,dwData,dwTimeStamp,dwSequence,uAppData;};
static bool visible=false,uiReady=true,authorized=true;static void*windowHandle=(void*)0x1234;static void*foreground=windowHandle;
static void*GetForegroundWindow(){return foreground;}
namespace VehicleMod {namespace NativeSignals {static bool usable(){return authorized;}}}
static UINT MapVirtualKeyW(UINT k,UINT mode){assert(mode==4);return k==0x5a?0x2c:k==0x43?0x2e:k==0x58?0x2d:k==0x26?0xe048:k==0x57?0x11:0;}
static SHORT WINAPI GetAsyncKeyState(int k){return (k==VK_MENU || k==VK_LWIN || k==VK_RWIN)?(systemChord?SHORT(0x8001):0):SHORT(0x8001);}
static SHORT WINAPI GetKeyState(int){return SHORT(0x8001);}
static BOOL WINAPI GetKeyboardState(PBYTE p){if(!p)return 0;memset(p,0x80,256);return 1;}
static DWORD WINAPI XInputGetState(DWORD i,XINPUT_STATE*p){assert(i<2);if(!p)return 87;p->dwPacketNumber=123;p->Gamepad={0x100f,200,199,{1,2,3,4}};return 0;}
#include "signal_os_filters_under_test.h"
struct InputRecord{void**original;bool keyboard=true;};
static void*methods[32]{};static InputRecord input{methods,true};static void*const device=(void*)0x5678;
static InputRecord*inputRecord(void*p){return p==device?&input:nullptr;}
static DWORD lastFlags=0;static HRESULT result=0;
static HRESULT WINAPI state(void*,DWORD n,void*p){if(p)memset(p,0x80,n);return result;}
static HRESULT WINAPI data(void*,DWORD size,DIDEVICEOBJECTDATA*p,DWORD*n,DWORD flags){
 lastFlags=flags;if(!n)return -1;if(!p){*n=2;return result;}assert(size==sizeof(*p)&&*n>=2);
 p[0]={0x2c,0x80,10,11,12};p[1]={0x11,0x55,20,21,22};*n=2;return result;
}
#include "signal_os_callbacks_under_test.h"
int main(){
 BYTE keyboard[256];assert(!quietAsync(0x5a)&&quietAsync(0x57)==SHORT(0x8001));
 assert(!quietKey(0x43));assert(quietKeyboard(keyboard)&&keyboard[0x5a]==0&&keyboard[0x43]==0&&keyboard[0x58]==0&&keyboard[0x57]==0x80);
 XINPUT_STATE x{};assert(!quietXInput(0,&x)&&x.dwPacketNumber==123&&x.Gamepad.wButtons==0x1002&&x.Gamepad.bLeftTrigger==200&&x.Gamepad.axes[3]==4);
 assert(!quietXInput(1,&x)&&x.Gamepad.wButtons==0x100f); // never reserve another player's buttons
 assert(SignalControls::choose(2,15,false));assert(!quietXInput(0,&x)&&x.Gamepad.bLeftTrigger==0&&x.Gamepad.bRightTrigger==199);
 assert(SignalControls::choose(2,0x26,true));SignalControls::filterDirectKeyboard(keyboard);
 assert(keyboard[0xc8]==0&&keyboard[0x11]==0x80); // extended arrow -> native DI scan
 methods[9]=(void*)state;methods[10]=(void*)data;
 assert(getState(device,256,keyboard)==0&&keyboard[0x2c]==0&&keyboard[0xc8]==0&&keyboard[0x11]==0x80);
 DIDEVICEOBJECTDATA events[2];DWORD count=2;
 assert(getData(device,sizeof(*events),events,&count,1)==0&&count==1&&events[0].dwOfs==0x11&&events[0].dwData==0x55&&events[0].dwSequence==21&&lastFlags==1);
 count=2;assert(getData(device,sizeof(*events),nullptr,&count,0)==0&&count==2); // count-only query unchanged
 input.keyboard=false;assert(getState(device,256,keyboard)==0&&keyboard[0x2c]==0x80);input.keyboard=true;
 authorized=false;assert(quietAsync(0x5a)==SHORT(0x8001));assert(!quietXInput(0,&x)&&x.Gamepad.wButtons==0x100f);
 assert(getState(device,256,keyboard)==0&&keyboard[0x2c]==0x80);
 authorized=true;systemChord=true;assert(quietAsync(0x5a)==SHORT(0x8001));systemChord=false;foreground=nullptr;assert(quietAsync(0x5a)==SHORT(0x8001));foreground=windowHandle;
 visible=true;assert(!quietXInput(0,&x)&&x.dwPacketNumber==123);for(BYTE b:reinterpret_cast<BYTE(&)[12]>(x.Gamepad))assert(b==0);
 count=2;assert(getData(device,sizeof(*events),events,&count,0)==0&&count==0);
 visible=false;result=-42;count=2;assert(getData(device,sizeof(*events),events,&count,0)==-42&&count==2);
 assert(getState(nullptr,256,keyboard)==E_FAIL);
 puts("Indicator actual OS/DI bodies: only leased foreground bindings reserved, raw return/packet/axes/other buttons preserved, slot-1 unchanged, trigger and extended-key scan mapping, buffered events/count-only/error/device/menu/focus/disabled paths; MOCK OS x86, NOT hardware acceptance.");
}
