// Production native hooks with a MOCK native builder/reconciler. Not gameplay.
#include <atomic>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define __cdecl __attribute__((cdecl))
#define __thiscall __attribute__((thiscall))
#define __fastcall __attribute__((fastcall))
using BYTE=unsigned char;using DWORD=uint32_t;
static DWORD tick=100,threadId=7;
static DWORD GetTickCount(){return tick;}
static DWORD GetCurrentThreadId(){return threadId;}
static std::vector<BYTE>service(0x75c),manager(0x5ca900);
static bool writeAllowed=true;
static BYTE*root(){return service.data();}
static bool readable(const void*p,size_t n){
 auto within=[&](const std::vector<BYTE>&b){auto x=uintptr_t(p),s=uintptr_t(b.data());return x>=s&&n<=b.size()&&x-s<=b.size()-n;};
 return within(service)||within(manager);
}
static bool writable(void*p,size_t n){return writeAllowed&&readable(p,n);}
static void logLine(const char*){}
#include "traffic_hook_under_test.h"
namespace N=NativeTraffic;namespace P=TrafficPolicy;
static BYTE*list(){return manager.data()+0x50182c;}
static unsigned&count(){return *reinterpret_cast<unsigned*>(list()+0x1000);}
static P::Lane*records(){return reinterpret_cast<P::Lane*>(list());}
static bool expired=true,append=true,bad=false;
static unsigned onOffCalls=0,tickCalls=0;
static void __thiscall onOff(void*self,bool enabled){assert(self==manager.data());++onOffCalls;manager[0x5ca84c]=enabled;}
static void __thiscall nativeTick(void*self,float time){assert(self==manager.data()&&time==12.5f);++tickCalls;}
static int originalCalls=0,nearCalls=0;
static void*lastSelf=nullptr;static void*const token=reinterpret_cast<void*>(0x1234);
static std::vector<P::Lane>all,keep;
static void* __cdecl nearby(void*self,const void*position,float radius){
 assert(self==list()&&position==token&&radius==100.f);++nearCalls;
 for(auto r:all)records()[count()++]=r;
 if(bad)records()[count()++]={0x5550,2};
 return self;
}
static unsigned char __thiscall query(void*self,bool refresh,void*interlopers){
 ++originalCalls;lastSelf=self;assert(interlopers==token);
 if(self!=list())return 77;
 if(!expired&&!refresh)return 0;
 if(!*(list()+0x1e09)){
  count()=0;for(auto r:keep)records()[count()++]=r;
  if(append){assert(N::nearbyHook(self,token,100.f)==self);assert(N::nearbyHook(self,token,100.f)==self);}
  auto less=[](const P::Lane&a,const P::Lane&b){return a.road<b.road||(a.road==b.road&&a.direction<b.direction);};
  std::sort(records(),records()+count(),less);
  count()=std::unique(records(),records()+count(),P::same)-records();
 }
 expired=false;return 1;
}
static std::vector<P::Lane>snapshot(){return {records(),records()+count()};}
static void run(){assert(N::queryHook(list(),nullptr,false,token)<=1);}
static bool contains(P::Lane x){for(unsigned i=0;i<count();i++)if(P::same(records()[i],x))return true;return false;}
int main(){
 *reinterpret_cast<BYTE**>(service.data()+0x758)=manager.data();
 for(unsigned i=0;i<150;i++)all.push_back({0x100000+i*0x34,i%2});
 keep={all[0],{0x777700,1}};
 std::atomic<bool>control{true};N::control=&control;N::ready=true;N::originalQuery=query;N::originalNearby=nearby;
 // Default OFF must not force a refresh or alter any native result.
 control=false;expired=false;run();assert(nearCalls==0&&count()==0);control=true;expired=true;
 N::set(true,50);run();unsigned half=count();assert(half==2+P::quota(149,50)&&contains(keep[0])&&contains(keep[1]));
 assert(N::beforeCount==151&&N::protectedCount==2&&N::afterCount==half);
 auto saved=snapshot();int calls=nearCalls;run();assert(nearCalls==calls&&snapshot().size()==saved.size());
 for(unsigned i=0;i<saved.size();i++)assert(P::same(saved[i],records()[i]));
 // Lowering the level must NOT evict cars from still-native candidates.
 auto incumbents=snapshot();
 for(int level:{1,5,20}){N::set(true,level);run();assert(count()==half);for(auto x:incumbents)assert(contains(x));}
 N::set(true,100);run();assert(count()==151);
 N::set(true,25);run();assert(count()==151); // grandfather cached native traffic
 // Moving to genuinely new native groups allows the reduced quota to converge.
 all.clear();for(unsigned i=0;i<150;i++)all.push_back({0x900000+i*0x34,i%2});
 expired=true;run();assert(count()==2+P::quota(150,25)&&count()<half);
 auto moving=snapshot();all.push_back({0xa00000,0});expired=true;run();
 for(auto x:moving)assert(contains(x)); // a new priority cannot replace incumbents
 N::set(true,1);run();for(auto x:moving)assert(contains(x));
 P::Lane leaving{};for(auto x:moving)if(!P::same(x,keep[0])&&!P::same(x,keep[1])){leaving=x;break;}
 all.erase(std::remove_if(all.begin(),all.end(),[&](P::Lane x){return P::same(x,leaving);}),all.end());
 expired=true;run();assert(!contains(leaving)); // native retirement, not pin forever
 unsigned baseline=all.size()+2;
 control=false;N::set(false);run();assert(count()==baseline);
 control=true;N::set(true,1);run();assert(count()==baseline); // no activation pop
 *(list()+0x1e09)=1;N::set(false);run();assert(count()==baseline&&N::lastPercent==1);
 *(list()+0x1e09)=0;run();assert(count()==baseline&&N::lastPercent==100);
 N::set(true,1);run();tick+=501;run();assert(count()==baseline); // expired Lua lease
 N::set(true,1);run();threadId++;run();assert(count()==baseline);threadId--; // wrong thread
 // Every failed validation leaves the complete native output untouched.
 N::set(true,1);writeAllowed=false;run();assert(count()==baseline);writeAllowed=true;
 bad=true;expired=true;run();assert(count()==baseline+1&&!N::permit);bad=false;
 N::set(true,1);expired=true;append=false;run();assert(count()==2);append=true;
 assert(N::queryHook(manager.data(),nullptr,true,token)==77&&lastSelf==manager.data());
 assert(originalCalls>10&&N::faults>=2);
 assert(!N::canZero());N::set(true,0);assert(!N::permit); // unavailable ZERO never fakes a probability fallback
 N::set(true,101);assert(!N::permit);
 // ZERO uses stock on/off, not an empty required-candidate list. Native tick
 // remains callable while disabled, and expires its lease BEFORE enabled check.
 std::atomic<uint32_t>generation{1};N::generation=&generation;
 N::originalOnOff=onOff;N::originalTick=nativeTick;N::zeroReady=true;manager[0x5ca84c]=1;
 auto oldHeartbeat=N::heartbeat.load();assert(N::canZero()&&onOffCalls==0&&!N::zero.owned&&N::heartbeat==oldHeartbeat);
 assert(N::set(true,0)&&manager[0x5ca84c]==0&&onOffCalls==1&&N::zero.owned);
 assert(N::canZero()&&onOffCalls==1);
 assert(N::set(true,0)&&onOffCalls==1);N::tickHook(manager.data(),nullptr,12.5f);assert(tickCalls==1&&onOffCalls==1);
 tick+=501;N::tickHook(manager.data(),nullptr,12.5f);assert(manager[0x5ca84c]==1&&onOffCalls==2&&!N::zero.owned);
 assert(N::set(true,0));N::set(false);assert(manager[0x5ca84c]==1); // OFF owned restoration
 assert(N::set(true,0));N::set(true,50);assert(manager[0x5ca84c]==1); // returning to nonzero
 // No grandfathering a stale pre-ZERO cached list after stock removed traffic.
 assert(N::set(true,0));assert(N::set(true,1));expired=true;run();assert(count()==3);
 // Native same-value OFF invalidates ownership and rejects old intent; never
 // re-enable a native mission policy. A NEW explicit UI generation may retry.
 assert(N::set(true,0));N::onOffHook(manager.data(),nullptr,false);
 unsigned foreignCalls=onOffCalls;assert(!N::canZero()&&onOffCalls==foreignCalls);assert(!N::set(true,0));N::set(false);
 assert(manager[0x5ca84c]==0&&onOffCalls==foreignCalls);++generation;
 assert(N::set(true,0));N::set(false);assert(manager[0x5ca84c]==0); // initial native OFF preserved
 manager[0x5ca84c]=1;++generation;assert(N::set(true,0));
 N::onOffHook(manager.data(),nullptr,true);assert(!N::set(true,0)&&manager[0x5ca84c]==1);
 ++generation;assert(N::set(true,0));control=false;N::tickHook(manager.data(),nullptr,12.5f);assert(manager[0x5ca84c]==1);control=true;
 manager[0x5ca84c]=2;foreignCalls=onOffCalls;assert(!N::canZero()&&!N::set(true,0)&&onOffCalls==foreignCalls); // unknown native flag fails closed
 manager[0x5ca84c]=1;++generation;assert(N::set(true,0));manager[0x5ca84c]=1;foreignCalls=onOffCalls;
 tick+=501;N::tickHook(manager.data(),nullptr,12.5f);assert(onOffCalls==foreignCalls&&!N::set(true,0)); // direct foreign value change also wins
 // Policy validation is transactional; zero is a separate native policy.
 P::Lane invalid[]={{0x1000,0},{0x2000,2}};unsigned n=2;
 assert(!P::filter(invalid,n,nullptr,0,1)&&n==2&&invalid[0].road==0x1000);
 assert(!P::filter(invalid,n,nullptr,0,0)&&n==2);
 // Regression: a small native pool whose old hash buckets all missed 5%.
 std::vector<P::Lane>sparse;
 for(unsigned i=1;sparse.size()<5;i++){
  P::Lane x{0x300000+i*0x34,i%2};if(P::priority(x)%100>=20)sparse.push_back(x);
 }
 for(int level:{1,5,20}){auto v=sparse;unsigned size=v.size();assert(P::filter(v.data(),size,nullptr,0,level)&&size>=1);}
 // All 100 levels: exact quota, nonempty eligible floor, protected lanes,
 // stable original order, deterministic output and nested subsets. At 100 the
 // ENTIRE current native list is byte-for-byte unchanged (not above stock).
 for(unsigned size:{0u,1u,2u,3u,5u,20u,150u,512u}){
  std::vector<P::Lane>pool;for(unsigned i=0;i<size;i++)pool.push_back({0x500000+i*0x34,i%2});
  std::vector<P::Lane>protectedLanes;if(size>=3)protectedLanes={pool[1],pool.back()};
  unsigned eligible=size-protectedLanes.size(),previousQuota=0;
  std::vector<bool>previous(size,false);
  for(int level=1;level<=100;level++){
   auto v=pool;unsigned output=size,quota=P::quota(eligible,level);
   assert(quota>=previousQuota&&quota<=eligible&&(!eligible||quota>=1));previousQuota=quota;
   assert(P::filter(v.data(),output,protectedLanes.data(),protectedLanes.size(),level));
   assert(output==protectedLanes.size()+quota);
   std::vector<bool>selected(size,false);unsigned next=0;
   for(unsigned i=0;i<size;i++)if(next<output&&P::same(pool[i],v[next])){selected[i]=true;++next;}
   assert(next==output);for(unsigned i=0;i<size;i++)assert(!previous[i]||selected[i]);previous=selected;
   auto again=pool;unsigned repeated=size;assert(P::filter(again.data(),repeated,protectedLanes.data(),protectedLanes.size(),level));
   assert(repeated==output);for(unsigned i=0;i<output;i++)assert(P::same(again[i],v[i]));
   if(level==1)assert(quota==(eligible?1u:0u));
   if(level==100)assert(output==size&&(!size||!memcmp(pool.data(),v.data(),size*sizeof(P::Lane))));
  }
 }
 // Stateless admission policy with incumbent retention, including a smaller
 // quota and incoming candidates that rank ahead of an already-admitted group.
 P::Lane old[]={{0x710000,0},{0x720000,1}};
 P::Lane changed[]={{0x710000,0},{0x720000,1},{0x730000,0},{0x740000,1}};unsigned size=4;
 assert(P::filter(changed,size,nullptr,0,1,old,2)&&size==2);
 assert(P::same(changed[0],old[0])&&P::same(changed[1],old[1]));
 P::Lane afterExit[]={{0x720000,1},{0x730000,0},{0x740000,1}};size=3;
 assert(P::filter(afterExit,size,nullptr,0,1,old,2)&&size==1&&P::same(afterExit[0],old[1]));
 // Pool growth must not admit previously rejected lanes just to fill headroom.
 P::Lane previous[]={{0x810000,0},{0x820000,1},{0x830000,0},{0x840000,1}};
 P::Lane retained[]={previous[0]};P::Lane grown[]={previous[0],previous[1],previous[2],previous[3],{0x850000,0},{0x860000,1}};
 size=6;assert(P::filter(grown,size,nullptr,0,50,retained,1,previous,4));
 assert(size==3&&P::same(grown[0],retained[0])&&grown[1].road==0x850000&&grown[2].road==0x860000);
 puts("native traffic: separate ZERO stock policy / expiry tick / foreign intent / exact owned restore, sticky admission/rejection, no incumbent eviction, 1..100 quota / 100=native, production x86 ABI MOCK only; NOT rear-camera gameplay acceptance");
}
