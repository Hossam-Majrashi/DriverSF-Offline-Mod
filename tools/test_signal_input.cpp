// Production preference validation and edge policy. No generated game input.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "signal_bindings.h"
namespace P=SignalInputPolicy;namespace B=SignalControls;
int main(){
 int pad[]={3,4,1};bool keys[3]{};P::Edges e;
 assert(!e.poll(false,false,keys,0,pad));assert(!e.poll(true,true,keys,1,pad)); // held at connect ignored
 assert(!e.poll(true,true,keys,0,pad));assert(e.poll(true,true,keys,1,pad)==3);
 for(int i=0;i<1000;i++)assert(!e.poll(true,true,keys,1,pad)); // no repeat
 assert(!e.poll(true,true,keys,0,pad));assert(e.poll(true,true,keys,4,pad)==1);
 assert(!e.poll(false,true,keys,8,pad));assert(!e.poll(true,true,keys,8,pad)); // menu/focus/resume held ignored
 assert(!e.poll(true,true,keys,0,pad));assert(e.poll(true,true,keys,8,pad)==2);
 assert(!e.poll(true,false,keys,0,pad));assert(!e.poll(true,true,keys,1,pad)); // reconnect never manufactures an action
 assert(!e.poll(true,true,keys,0,pad));assert(e.poll(true,true,keys,1,pad)==3);
 assert(!e.poll(true,true,keys,0,pad));keys[0]=true;assert(e.poll(true,true,keys,0,pad)==1);
 assert(!e.poll(true,true,keys,0,pad));keys[0]=false;e.poll(true,true,keys,0,pad);
 keys[0]=keys[1]=true;assert(!e.poll(true,true,keys,0,pad)); // ambiguous left/right ignored
 keys[0]=keys[1]=false;e.poll(true,true,keys,0,pad);keys[2]=true;assert(e.poll(true,true,keys,0,pad)==3);
 int defaultsKeys[]={0x5a,0x43,0x58};assert(P::valid(defaultsKeys,true)&&P::valid(pad,false));
 int invalid[]={0x70,0x43,0x58};assert(!P::valid(invalid,true)); // F1 is reserved, not bindable
 int duplicates[]={1,1,3};assert(!P::valid(duplicates,false));int badPad[]={999,4,1};
 B::loadBindings(invalid,badPad);for(int i=0;i<3;i++)assert(B::keyboard[i]==defaultsKeys[i]&&B::controller[i]==pad[i]);
 int customKeys[]={0x26,0x27,0},customPad[]={15,16,0};B::loadBindings(customKeys,customPad);
 assert(B::keyboard[0]==0x26&&B::controller[0]==15&&B::controller[1]==16);
 assert(!B::choose(1,0x26,true)&&B::keyboard[1]==0x27);assert(B::choose(0,0,true));assert(B::choose(1,0x26,true));
 assert(!B::choose(9,1,false)&&!B::choose(1,99,false));
 P::Edges trigger;int triggers[]={15,16,0};bool released[3]{};
 trigger.poll(false,true,released,0,triggers);trigger.poll(true,true,released,0,triggers);
 assert(trigger.poll(true,true,released,1u<<16,triggers)==1);
 assert(trigger.poll(true,true,released,1u<<17,triggers)==2);
 puts("Signal bindings: validated unique/safe keyboard+controller prefs, default/custom/unbound/trigger choices, press-only/no-repeat/menu/focus/initial-connect/reconnect release gates and simultaneous-action handling; production policy MOCK, NOT controller gameplay.");
}
