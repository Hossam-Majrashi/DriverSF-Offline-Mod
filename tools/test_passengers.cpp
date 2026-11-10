// Production per-seat policy/detour with MOCK characters, real x86 ABI.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#define __cdecl __attribute__((cdecl))
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;
static std::vector<unsigned char>a(0x600),b(0x600),service(0x734),audio(0x1c600);
static BYTE*root(){return service.data();}
static void logLine(const char*){}
static unsigned refreshes=0,cleanups=0;
static void __thiscall refresh(void*v){assert(v==audio.data());++refreshes;}
static void __thiscall clean(void*v){assert(v==audio.data());++cleanups;}
static void*current=a.data();
static bool validA=true;
static void*playerVehicle(){return current;}
static bool readable(const void*p,size_t n){return (p==a.data()&&validA&&n<=a.size())||(p==b.data()&&n<=b.size())||(p==service.data()&&n<=service.size())||(p==audio.data()&&n<=audio.size());}
#include "passengers_hook_under_test.h"
namespace N=NativePassengers;
static unsigned writes[4]={};
static uint16_t&profile(void*v,int seat){return *(uint16_t*)((unsigned char*)v+0x5cc+2*seat);}
static uint16_t&flags(void*v){return *(uint16_t*)((unsigned char*)v+0x5d4);}
static uint16_t __thiscall get(void*v,int seat){assert(seat>=1&&seat<=3);return profile(v,seat);}
static void __thiscall set(void*v,int seat,uint16_t p,bool backup){assert(seat>=0&&seat<=3&&!backup);++writes[seat];profile(v,seat)=p;flags(v)&=~(1u<<(seat+4));flags(v)|=1u<<seat;}
static void reset(){N::discard();current=a.data();validA=true;flags(a.data())=flags(b.data())=0;for(int seat=0;seat<4;seat++){writes[seat]=0;profile(a.data(),seat)=100+seat;profile(b.data(),seat)=200+seat;}}
int main(){
 N::originalSetter=set;N::getter=get;N::ready=true;reset();
 profile(a.data(),2)=N::empty;profile(a.data(),3)=52345;
 assert(N::apply(a.data(),true)&&N::active&&writes[0]==0);
 for(int i=1;i<=3;i++)assert(profile(a.data(),i)==N::empty);
 assert(writes[1]==1&&writes[2]==0&&writes[3]==1);
 assert(N::apply(a.data(),true)&&writes[1]==1); // no periodic repaint
 assert(N::apply(a.data(),false)&&!N::active);
 assert(profile(a.data(),0)==100&&profile(a.data(),1)==101&&profile(a.data(),2)==N::empty&&profile(a.data(),3)==52345);
 assert(writes[0]==0&&writes[1]==2&&writes[3]==2); // exact RAW IDs restored
 reset();assert(!N::apply(b.data(),true)&&writes[1]==0&&!N::active); // not player's car
 assert(N::apply(a.data(),true));
 N::setterHook(a.data(),nullptr,1,N::empty,false); // same-value native intent wins
 N::setterHook(a.data(),nullptr,2,65534,false);    // actual native replacement wins
 unsigned n=writes[1];assert(N::apply(a.data(),true)&&writes[1]==n);
 assert(N::apply(a.data(),false)&&profile(a.data(),1)==N::empty&&profile(a.data(),2)==65534&&profile(a.data(),3)==103);
 reset();assert(N::apply(a.data(),true));current=b.data();
 assert(N::apply(a.data(),false)); // live prior-car cleanup after a shift
 assert(profile(a.data(),1)==101&&N::apply(b.data(),true)&&profile(b.data(),0)==200);
 N::discard();assert(!N::active); // invalidated Lua owner: metadata only, no deref
 reset();assert(N::apply(a.data(),true));validA=false;current=b.data();
 assert(!N::apply(a.data(),false));N::discard();assert(N::apply(b.data(),true));
 assert(profile(a.data(),1)==N::empty&&profile(b.data(),1)==N::empty&&writes[0]==0);
 reset();flags(a.data())=1u<<(2+4);assert(N::apply(a.data(),true));
 assert(profile(a.data(),2)==102&&writes[2]==0&&(flags(a.data())&(1u<<(2+4)))); // native temporary override untouched
 flags(a.data())|=1u<<(1+4);assert(N::apply(a.data(),false));
 assert(profile(a.data(),1)==N::empty&&(flags(a.data())&(1u<<(1+4)))&&profile(a.data(),2)==102); // do not clear a new native backup during release
 reset();*(BYTE**)(service.data()+0x730)=audio.data();*(void**)(audio.data()+0x1c29c)=a.data();
 N::refreshRoster=refresh;N::cleanAmbientQueue=clean;
 N::audioReady=true;assert(N::apply(a.data(),true)&&refreshes==1&&cleanups==1&&!N::record.audioPending);
 assert(N::apply(a.data(),true)&&refreshes==1&&cleanups==1); // no audio repaint/queue loop
 assert(N::apply(a.data(),false,true)&&refreshes==2&&cleanups==1); // free-drive OFF refresh, no stop
 reset();audio[0x1c3dd]=1;assert(N::apply(a.data(),true)&&N::record.audioPending&&refreshes==2);
 audio[0x1c3dd]=0;assert(N::apply(a.data(),true)&&refreshes==3&&cleanups==2&&!N::record.audioPending);
 assert(N::apply(a.data(),false,false)&&refreshes==3); // unsafe/mission cleanup never touches dialogue
 reset();*(void**)(audio.data()+0x1c29c)=b.data();assert(N::apply(a.data(),true)&&N::record.audioPending&&refreshes==3);
 N::setterHook(a.data(),nullptr,1,N::empty,false);assert(!N::record.audioPending); // newer native intent cancels deferred audio cleanup
 N::ready=false;assert(!N::apply(b.data(),true));
 puts("Passenger production policy: empty=0xffff, driver untouched, raw-ID restore, live prior car, stale-owner discard, no NPC edits, same-value/native overwrite wins, no repaint; x86 ABI MOCK only, NOT visual/audio acceptance.");
}
