// Production native indicator policy, MOCK lamps with real 32-bit thiscall ABI.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#define __cdecl __attribute__((cdecl))
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;using DWORD=uint32_t;
static DWORD tick=100;static DWORD GetTickCount(){return tick;}
static std::vector<BYTE>a(0x600),b(0x600);static void*current=a.data();
static void*playerVehicle(){return current;}
static bool readable(const void*p,size_t n){return (p==a.data()||p==b.data())&&n<=a.size();}
#include "signals_hook_under_test.h"
namespace N=NativeSignals;
static unsigned calls=0;
static uint32_t&bits(void*v){return *(uint32_t*)((BYTE*)v+0x530);}
static void __thiscall set(void*v,int op,uint32_t mask,bool on){assert(op==3&&(mask==8||mask==16));++calls;bits(v)=(bits(v)&~mask)|(on?mask:0);}
int main(){
 N::ready=true;N::enabled=true;N::originalSetter=set;bits(a.data())=0x201;bits(b.data())=0x402;
 assert(!N::request(1));assert(N::pump(a.data())&&calls==0&&N::usable());
 assert(N::request(1)&&N::pump(a.data())&&N::mode==1&&bits(a.data())==0x209);
 unsigned before=calls;N::pump(a.data());assert(calls==before); // no blink repaint; engine animates
 assert(N::request(1)&&N::pump(a.data())&&N::mode==0&&bits(a.data())==0x201);
 assert(N::request(3)&&N::pump(a.data())&&N::mode==3&&bits(a.data())==0x219);
 N::setterHook(a.data(),nullptr,3,8,true); // same-value foreign write wins LEFT only
 assert(N::release(a.data())&&bits(a.data())==0x209); // RIGHT restores, foreign LEFT stays
 assert(N::pump(a.data())&&N::request(2)&&N::pump(a.data())&&N::mode==2);
 assert(N::release(a.data())&&bits(a.data())==0x209); // exact independent baseline
 N::pump(a.data());assert(N::request(3));N::setterHook(a.data(),nullptr,3,8,false);
 before=calls;N::pump(a.data());assert(calls==before&&N::mode==0); // old command cannot stomp native write
 assert(N::request(1));tick+=501;assert(!N::usable());N::pump(a.data());assert(N::mode==0); // stale command rejected
 assert(N::request(1));N::pump(a.data(),true);assert(N::mode==0); // fresh VM/owner never replays acknowledged/queued actions
 assert(N::request(3));N::enabled=false;N::cancel();assert(!N::pump(a.data())&&!N::request(2));
 N::release(a.data());N::enabled=true;N::pump(a.data());assert(N::mode==0);
 assert(N::request(1));current=b.data();assert(!N::usable()&&!N::pump(a.data()));
 N::release(a.data());assert(N::pump(b.data())&&N::mode==0&&bits(b.data())==0x402);
 assert(N::request(2)&&N::pump(b.data())&&bits(b.data())==0x412&&bits(a.data())==0x201);
 current=nullptr;assert(!N::usable()&&!N::request(3));current=b.data();
 N::inboxLock.test_and_set();assert(!N::request(1));N::inboxLock.clear();
 assert(N::release(b.data())&&bits(b.data())==0x402&&!N::active);
 puts("Native indicators: left/right/hazards/OFF toggles, native animation/no repaint, other light bits/BOT unchanged, per-bit exact restore/foreign same-value precedence, epoch/deadline/car/Shift/lease/inbox guards; real x86 ABI MOCK, NOT gameplay.");
}
