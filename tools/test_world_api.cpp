// Real production C callbacks with a MOCK Lua stack/API, not game execution.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <thread>
#define __cdecl __attribute__((cdecl))
using DWORD=uint32_t;
static DWORD tick=100;
static DWORD GetTickCount(){return tick;}
static std::atomic<bool> nitro{false};
static void*playerVehicle(){return reinterpret_cast<void*>(0x1000);}
static std::vector<std::string>logs;
static void logLine(const char*s){logs.emplace_back(s);}
// Native traffic is separately tested against its production hook model.
namespace NativeTraffic {
static std::atomic<bool> ready{false},permit{false};static int percent=100;
static bool accept=true;
static bool canZero(){return ready&&accept;}
static bool set(bool on,int p=100){permit=on&&ready&&accept;percent=p;return ready&&(!on||accept);}
}
namespace NativePassengers {
static std::atomic<bool>ready{false},active{false};
static void*handle=playerVehicle();
static void*check(void*,int,const char*s){assert(!strcmp(s,"gameVehicleResourcesMetatable"));return &handle;}
static void*(*checkUserdata)(void*,int,const char*)=check;
static void discard(){active=false;}
static bool apply(void*v,bool clear,bool=false){assert(v==playerVehicle());active=clear;return true;}
}
namespace NativeSignals {
static std::atomic<bool>ready{false},enabled{false},active{false};
static void cancel(){}
static void discard(){active=false;}
static bool release(void*v){assert(v==playerVehicle());active=false;return true;}
static bool pump(void*v,bool=false){assert(v==playerVehicle());active=true;return true;}
}
#include "world_api_under_test.h"
using namespace NitroLua;
namespace W=NitroLua::World;
struct V{int type=0,kind=0;bool b=false;Callback fn=nullptr;std::string s;};
static V boolean(bool b){V v;v.type=1;v.b=b;return v;}
static V function(int kind){V v;v.type=6;v.kind=kind;return v;}
static V string(const char*s){V v;v.type=4;v.s=s;return v;}
struct VM{std::vector<V>stack;std::map<std::string,V>registry,globals;};
static bool loadError=false,callError=false,badFactory=false,cancelWeather=false,timerFound=true,newerDuringCall=false,hasStack=true,streetCancel=false,streetThrow=false;
static int hostError=0;
static bool nestedHost=false,compensatedTimer=false;
static unsigned nativeCalls=0,worldCalls=0,nitroCalls=0,loads=0;
static unsigned observedPreset=99;
static VM&vm(State*p){return *static_cast<VM*>(p);}
static V&at(State*p,int i){auto&s=vm(p).stack;int n=i>0?i-1:int(s.size())+i;assert(n>=0&&n<int(s.size()));return s[n];}
static int __cdecl getTop(State*p){return int(vm(p).stack.size());}
static void __cdecl setTop(State*p,int n){assert(n>=0);vm(p).stack.resize(n);}
static int __cdecl typeOf(State*p,int i){return at(p,i).type;}
static int __cdecl toBool(State*p,int i){auto&v=at(p,i);return v.type!=0&&(v.type!=1||v.b);}
static const char* __cdecl toString(State*p,int i,size_t*n){auto&v=at(p,i);assert(v.type==4);*n=v.s.size();return v.s.c_str();}
static void __cdecl pushBool(State*p,int b){vm(p).stack.push_back(boolean(b));}
static void __cdecl pushValue(State*p,int i){V v=at(p,i);vm(p).stack.push_back(v);}
static void __cdecl pushClosure(State*p,Callback fn,int n){assert(n==0);V v=function(5);v.fn=fn;vm(p).stack.push_back(v);}
static void __cdecl getField(State*p,int i,const char*k){assert(i==registry||i==-10002);vm(p).stack.push_back((i==registry?vm(p).registry:vm(p).globals)[k]);}
static int __cdecl checkStack(State*,int n){assert(n==24);return hasStack;}
static void __cdecl setField(State*p,int i,const char*k){assert(i==registry);auto&s=vm(p).stack;assert(!s.empty());vm(p).registry[k]=s.back();s.pop_back();}
static int __cdecl loadBuffer(State*p,const char*b,size_t n,const char*name){
    assert(b&&n>100&&memcmp(b,"\033Lua\121\0\1\4\4\4\4\0",12)==0);++loads;bool world=strstr(name,"world_script")!=nullptr;
    if(world&&loadError){vm(p).stack.push_back(string("mock load error"));return 3;}
    vm(p).stack.push_back(function(world?1:3));return 0;
}
static std::vector<V>invoke(State*p,Callback cb){
    auto&s=vm(p).stack;size_t before=s.size();int n=cb(p);assert(n>=0&&s.size()==before+size_t(n));
    std::vector<V>out(s.end()-n,s.end());s.resize(before);return out;
}
static int __cdecl protectedCall(State*p,int nargs,int results,int errorFunction){
    assert(errorFunction==0);auto&s=vm(p).stack;assert(s.size()>size_t(nargs));size_t base=s.size()-nargs-1;
    V f=s[base];assert(f.type==6);std::vector<V>args(s.begin()+base+1,s.end());s.resize(base);std::vector<V>out;
    if(f.kind==1){assert(nargs==4&&results==1);assert(args[0].fn==W::frozen&&args[1].fn==W::weather&&args[2].fn==W::readStreets&&args[3].fn==W::diagnostic);out.push_back(badFactory?boolean(false):function(2));}
    else if(f.kind==2){
        assert(nargs==0&&results==6);++worldCalls;
        if(callError){s.push_back(string("mock world error"));return 2;}
        auto a=invoke(p,W::frozen),b=invoke(p,W::weather);assert(a.size()==1&&b.size()==8&&b[7].b);
        assert(a[0].b==bool(W::freeze&&!W::failed));unsigned choices=0;observedPreset=0;
        for(unsigned i=2;i<7;i++)if(b[i].b){++choices;observedPreset=i-1;}
        assert(choices==(b[0].b?1u:0u));
        bool pending=b[1].b;
        if(newerDuringCall){newerDuringCall=false;W::request(5);}
        auto street=invoke(p,W::readStreets);assert(street.size()==16 && street[11].fn==W::nativeTraffic && street[13].fn==W::passengerAccess && street[12].b==bool(W::passengers));
        assert(street[14].b==bool(NativeSignals::enabled)&&street[15].fn==W::signalAccess);
        unsigned density=0;for(unsigned i=0;i<7;i++)density|=unsigned(street[i+1].b)<<i;
        assert(density==unsigned(W::density.load()));
        assert(street[0].b==bool(W::traffic)&&street[8].b==bool(W::policeOff));
        assert(street[10].b==!W::streetFailed);
        out={boolean((timerFound&&a[0].b)||compensatedTimer),boolean(pending&&!cancelWeather),boolean(cancelWeather),boolean(street[9].b&&!streetThrow),boolean(streetCancel||streetThrow),streetThrow?string("mock street failure"):V{}};
    }else if(f.kind==7){
        assert(nargs==1&&results==2&&args[0].type==1&&args[0].b);
        if(hostError){s.push_back(string("original host error"));return hostError;}
        if(nestedHost){
            nestedHost=false;size_t before=s.size();unsigned count=worldCalls;
            s.push_back(function(7));s.push_back(boolean(true));
            assert(scriptStepHook(p,1,2,0)==0&&worldCalls==count);s.resize(before);
        }
        out={string("native result"),boolean(false)};
    }else if(f.kind==3){assert(nargs==1&&results==1);out.push_back(function(4));}
    else if(f.kind==4){assert(nargs==0&&results==1);++nitroCalls;out.push_back(boolean(true));}
    else assert(false);
    assert(out.size()==size_t(results));s.insert(s.end(),out.begin(),out.end());return 0;
}
static std::vector<V>nativeArguments;
static int __cdecl nativeInput(State*p){
    ++nativeCalls;auto&s=vm(p).stack;assert(s.size()==nativeArguments.size());
    for(size_t i=0;i<s.size();i++)assert(s[i].type==nativeArguments[i].type&&s[i].b==nativeArguments[i].b&&s[i].s==nativeArguments[i].s);
    return 7;
}
static VM reset(){
    api={getTop,setTop,typeOf,toBool,pushBool,pushValue,pushClosure,getField,setField,loadBuffer,protectedCall,checkStack,toString};
    originalPcall=protectedCall;W::bound=true;
    ready=true;scriptReady=false;nitro=false;originalInput=nativeInput;calls=installs=errors=0;
    W::freeze=W::live=W::installed=W::supportedTimer=W::failed=false;
    W::trafficUnavailable=false;
    W::command=W::completed=0;W::deadline=0;W::syncs=W::errors=0;W::inFlight.clear();
    W::traffic=W::policeOff=W::passengers=W::streetFailed=false;W::density=100;
    NativeSignals::enabled=false;NativeSignals::active=false;NativeSignals::ready=false;
    NativePassengers::active=false;NativePassengers::ready=false;NativeTraffic::ready=false;NativeTraffic::permit=false;NativeTraffic::accept=true;
    W::streetCommand=W::streetCompleted=0;W::streetDeadline=W::streetErrors=0;streetCancel=streetThrow=false;
    tick=100;loadError=callError=badFactory=cancelWeather=newerDuringCall=nestedHost=false;timerFound=hasStack=true;hostError=0;
    assert(scriptDepth==0);
    nativeCalls=worldCalls=nitroCalls=loads=0;observedPreset=99;compensatedTimer=false;logs.clear();
    VM v;v.stack={string("native argument"),boolean(true)};nativeArguments=v.stack;
    V table;table.type=5;for(const char*k:{"goalSystem","moodSystem","localPlayerManager"})v.globals[k]=table;
    return v;
}
static void input(VM&v){
    // Both are NATURAL host calls in this fixture. World must not rely on Nitro
    // input or alter native pcall results, including error return/stack values.
    assert(inputHook(&v)==7);assert(v.stack.size()==nativeArguments.size());
    v.stack.push_back(function(7));v.stack.push_back(boolean(true));
    assert(scriptStepHook(&v,1,2,0)==hostError);
    assert(v.stack[0].s==nativeArguments[0].s&&v.stack[1].b==nativeArguments[1].b);
    if(hostError){assert(v.stack.size()==3&&v.stack[2].s=="original host error");}
    else assert(v.stack.size()==4&&v.stack[2].s=="native result"&&v.stack[3].type==1&&!v.stack[3].b);
    v.stack.resize(nativeArguments.size());
}
int main(){
    assert(W::passengers); // actual startup default, unlike explicit OFF reset fixture
    auto armed=reset();W::passengers=true;input(armed);assert(worldCalls==1&&W::needed());
    W::stopStreets();input(armed);assert(W::passengers&&W::needed()); // missions do not disarm default passenger policy
    W::disable();input(armed);assert(!W::passengers&&!W::needed());
    reset();VM callback;callback.stack={boolean(true)};W::traffic=true;W::density=0;NativeTraffic::ready=true;
    callback.stack={boolean(true),boolean(true)};auto result=invoke(&callback,W::nativeTraffic);
    assert(result[0].b&&!NativeTraffic::permit&&NativeTraffic::percent==100&&W::traffic); // ZERO preflight is read-only
    NativeTraffic::accept=false;result=invoke(&callback,W::nativeTraffic);assert(!result[0].b&&W::traffic&&!NativeTraffic::permit);
    NativeTraffic::accept=true;W::density=50;result=invoke(&callback,W::nativeTraffic);assert(!result[0].b&&NativeTraffic::percent==100);
    W::density=0;callback.stack={boolean(true)};result=invoke(&callback,W::nativeTraffic);assert(result[0].b&&NativeTraffic::permit&&NativeTraffic::percent==0);
    NativeTraffic::accept=false;result=invoke(&callback,W::nativeTraffic);assert(!result[0].b&&!W::traffic&&!NativeTraffic::permit);
    callback.stack.clear();result=invoke(&callback,W::passengerAccess);assert(result[0].b&&!NativePassengers::active);
    V userdata;userdata.type=7;callback.stack={userdata,boolean(true)};NativePassengers::ready=true;W::passengers=true;
    result=invoke(&callback,W::passengerAccess);assert(result[0].b&&NativePassengers::active);
    userdata.type=2;callback.stack={userdata,boolean(true)};result=invoke(&callback,W::passengerAccess);assert(result[0].b&&NativePassengers::active); // native light/full userdata compatibility
    W::passengers=false;callback.stack={userdata,boolean(false)};result=invoke(&callback,W::passengerAccess);assert(result[0].b&&!NativePassengers::active);
    callback.stack={userdata,boolean(true)};result=invoke(&callback,W::passengerAccess);assert(!result[0].b&&!NativePassengers::active);
    NativeSignals::ready=true;NativeSignals::enabled=true;callback.stack={userdata,boolean(true)};
    result=invoke(&callback,W::signalAccess);assert(result[0].b&&NativeSignals::active);
    callback.stack={userdata,boolean(false)};result=invoke(&callback,W::signalAccess);assert(result[0].b&&!NativeSignals::active);
    NativeSignals::enabled=false;callback.stack={userdata,boolean(true)};result=invoke(&callback,W::signalAccess);assert(!result[0].b);
    reset();VM diag;
    assert(W::diagnostic(&diag)==0&&logs.empty());
    diag.stack={boolean(true)};assert(W::diagnostic(&diag)==0&&logs.empty()&&diag.stack.size()==1);
    diag.stack={string("Street status: traffic unavailable: atlas.road is absent")};
    assert(W::diagnostic(&diag)==0&&W::trafficUnavailable&&logs.size()==1&&diag.stack.size()==1);
    auto v=reset();input(v);assert(worldCalls==0&&nativeCalls==1&&loads==0);
    W::freeze=true;ready=false;input(v);assert(worldCalls==0);ready=true;
    hasStack=false;input(v);assert(worldCalls==0);hasStack=true;
    auto saved=v.globals;v.globals.clear();input(v);assert(worldCalls==0);v.globals=saved;
    hostError=2;input(v);assert(worldCalls==0);hostError=0;
    input(v);
    assert(worldCalls==1&&W::supportedTimer&&W::installed&&loads==1);
    input(v);assert(worldCalls==2&&loads==1);W::disable();input(v);
    assert(!W::installed&&!W::supportedTimer&&!W::needed());
    // An unfrozen instance-clock compensator needs native cleanup polling
    // until its goal ends; otherwise OFF could leak wrappers or catch up.
    v=reset();W::freeze=true;input(v);W::disable();compensatedTimer=true;input(v);
    assert(!W::freeze&&W::installed&&W::needed());compensatedTimer=false;input(v);assert(!W::needed());
    for(int preset=1;preset<=5;preset++){
        v=reset();W::chooseWeather(preset);auto token=W::command.load();input(v);
        assert(W::completed==token&&observedPreset==unsigned(preset)&&W::installed);
        W::disable();input(v);assert(observedPreset==0&&!W::needed());
    }
    v=reset();W::freeze=true;nestedHost=true;input(v);assert(worldCalls==1&&scriptDepth==0);
    v=reset();W::live=true;W::request(2);newerDuringCall=true;auto old=W::command.load();input(v);
    assert(W::completed==old&&W::completed!=W::command&&observedPreset==2);
    input(v);assert(observedPreset==5&&W::completed==W::command);
    v=reset();W::live=true;W::request(3);cancelWeather=true;input(v);
    assert(!W::live&&(W::command.load()&7)==0);cancelWeather=false;input(v);assert(!W::needed());
    v=reset();W::live=true;W::request(3);tick=2501;input(v);
    assert(!W::live&&observedPreset==0&&!W::needed());
    // Source loads/errors never corrupt the native argument stack or Nitro.
    for(int failure=0;failure<3;failure++){
        v=reset();W::freeze=true;nitro=true;
        loadError=failure==0;callError=failure==1;badFactory=failure==2;input(v);
        assert(W::errors==1&&!W::freeze&&nitro&&nitroCalls==1&&nativeCalls==1);
        if(failure<2){bool found=false;for(auto&s:logs)if(s.find(failure==0?"bytecode load detail: mock load error":"world update detail: mock world error")!=std::string::npos)found=true;assert(found);}
        loadError=callError=badFactory=false;input(v);assert(!W::needed()&&nitroCalls==2);
    }
    v=reset();W::freeze=true;callError=true;
    for(int n=0;n<3;n++)input(v);
    assert(W::failed&&W::errors==3&&!W::needed());input(v);assert(worldCalls==3&&nativeCalls==4);
    v=reset();W::freeze=true;W::inFlight.test_and_set();input(v);assert(worldCalls==0&&nativeCalls==1);W::inFlight.clear();
    v=reset();auto requestMany=[](int p){for(int n=0;n<1000;n++)W::request(p);};
    std::thread a(requestMany,2),b(requestMany,4);a.join();b.join();
    assert((W::command.load()&~7u)==2000*8u);assert((W::command.load()&7u)==2||(W::command.load()&7u)==4);
    auto token=W::command.load();W::request(6);W::request(-1);assert(W::command==token);
    v=reset();W::chooseWeather(3);input(v);assert(W::live&&observedPreset==3);
    W::chooseWeather(0);input(v);assert(!W::live&&observedPreset==0&&W::command==W::completed);
    auto weatherToken=W::command.load();W::chooseWeather(-1);W::chooseWeather(6);assert(W::command==weatherToken&&!W::live);
    v=reset();W::traffic=true;W::density=25;W::streetRequest();input(v);
    assert(W::streetCommand==W::streetCompleted&&W::needed());W::disable();input(v);assert(!W::needed());
    v=reset();W::traffic=W::policeOff=true;W::trafficUnavailable=true;W::streetRequest();input(v);
    assert(!W::traffic&&W::policeOff);W::disable();input(v);assert(!W::needed());
    v=reset();W::policeOff=true;W::streetRequest();streetCancel=true;input(v);assert(!W::policeOff);input(v);assert(!W::needed());
    v=reset();W::traffic=true;W::streetRequest();tick=2501;input(v);assert(!W::traffic&&!W::needed());
    v=reset();W::freeze=true;W::traffic=true;W::streetRequest();streetThrow=true;
    for(int n=0;n<3;n++)input(v);
    assert(W::streetFailed&&!W::failed&&W::freeze&&W::supportedTimer&&!W::traffic);
    puts("World production callbacks: real 32-bit C ABI / MOCK Lua stack; native args/results, timer/weather/traffic default OFF and passenger default ON, 16-value street ABI, raw userdata passenger bridge/cleanup, ZERO policy rejection, verified protected-script dispatch/headroom/game-VM/error guards, per-VM caching, latest request, expiry, OFF, re-entry, errors and Nitro independence passed. NOT gameplay.");
}
