// Real ImGui + embedded-font offline fixture. No game, graphics driver, input
// injection, Wine runtime or native game hook. Settings APIs explicitly mocked.
#include "imgui.h"
#include "imgui_internal.h"
#include <atomic>
#include <cassert>
#include <map>
#include <cwchar>
#include <iostream>
#include "ui.h"
static unsigned capturedErrors=0;
static std::string capturedMessage;
static void errorCallback(ImGuiContext*,void*,const char*message){++capturedErrors;capturedMessage=message;}
using WCHAR=wchar_t;
static const int MAX_PATH=260,POLICY_MAX=9999999;
static bool showDiagnostics=false;
static char amountText[32]="100000";
static void logLine(const char*){}
namespace VehicleMod {static std::atomic<int>boostCapKmh{300};
namespace NativeSignals {
static std::atomic<bool>ready{true},enabled{false};
static void cancel(){}static bool request(unsigned a){assert(a<=3);return true;}
}}
#include "signal_bindings.h"
using Key=std::pair<std::wstring,std::wstring>;
static std::map<Key,int>prefs;
static bool canWrite=true;
static unsigned GetModuleFileNameW(void*,WCHAR*p,unsigned){wcscpy(p,L"Z:\\test\\Driver.exe");return wcslen(p);}
static unsigned GetPrivateProfileIntW(const WCHAR*s,const WCHAR*k,int fallback,const WCHAR*) {
    auto it=prefs.find({s,k});return it==prefs.end()?fallback:it->second;
}
static bool WritePrivateProfileStringW(const WCHAR*s,const WCHAR*k,const WCHAR*v,const WCHAR*) {
    if(!canWrite)return false;prefs[{s,k}]=wcstol(v,nullptr,10);return true;
}
static bool parseAmount(const char*s,int&v){v=atoi(s);return true;}
#include "settings.h"
namespace SignalControls {
#include "signals_draw_under_test.h"
#include "speed_policy.h"
// Only native readiness/actions are mocked. Both new draw() bodies below are
// extracted verbatim from production headers by test_project.py.
namespace SpeedMod {
static std::atomic<bool>enabled{false};static std::atomic<int>targetMph{300};
static bool chooseTarget(int mph){if(enabled||!SpeedPolicy::validTarget(mph))return false;targetMph=mph;return true;}
}
namespace PowertrainMod {
static std::atomic<bool>enabled{false},ready{true},autoFollowing{false},unsupportedTarget{false};
static std::atomic<unsigned long>calls{0},matched{0},applied{0},conflicts{0},busyCalls{0},rejected{0};
static char powerStatus[]="Offline fixture";static bool isParked=true;
static bool parked(){return isParked;}
static void setEnabled(bool on){enabled=on;SpeedMod::enabled=on;}
#include "powertrain_draw_under_test.h"
namespace World {
static std::atomic<bool>bound{true},ready{true},failed{false},freeze{false},live{false},supportedTimer{false};
static std::atomic<unsigned long>syncs{0},errors{0};static std::atomic<unsigned>command{0},completed{0};
static std::atomic<bool>traffic{false},policeOff{false},passengers{true},streetFailed{false};
static std::atomic<bool>trafficUnavailable{false};
static std::atomic<int>density{100};
static void streetRequest(){}
static void chooseWeather(int p){assert(p>=0&&p<=5);live=p!=0;command=(command.load()&~7u)|p;}
#include "world_draw_under_test.h"
static std::string formatted(const char*f,...) {
    va_list ap;va_start(ap,f);auto t=Ui::format(f,ap,false);va_end(ap);return t;
}
static void prepareContext(bool strict=true) {
    ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigErrorRecoveryEnableAssert=strict;GImGui->ErrorCallback=errorCallback;
    Ui::initFont();unsigned char*pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);assert(pixels&&w>0&&h>0);
}
static void finalText(int kind) {
    if(kind==0)Ui::TextUnformatted("Settings");
    if(kind==1)Ui::TextDisabled("F1: close | PgUp/PgDn: tabs | Tab: navigate");
    if(kind==2)Ui::TextWrapped("Offline only. Park first; the menu does not pause the game.");
    if(kind==3)Ui::Text("Engine balance: %d WP",123456);
    if(kind==4)Ui::TextWrapped("%s","Apply OK: 100 -> 123456 WP (read back from engine).");
    if(kind==5)Ui::TextUnformatted("");
    if(kind==6)Ui::TextUnformatted("Settings\nSettings\nSettings");
    assert(!ImGui::GetCurrentWindow()->DC.IsSetPos);
}
static void layoutRegression() {
    auto&io=ImGui::GetIO();io.DisplaySize=ImVec2(1920,1080);io.DeltaTime=1.0f/60;
    unsigned frames=0;
    for(int lang:{0,1})for(float scale:{0.9f,1.23f,1.6f})for(int kind=0;kind<7;kind++)for(bool nested:{false,true}) {
        UiText::language=lang;io.FontGlobalScale=scale;
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(nested?340:760,nested?260:720));ImGui::Begin("Strict terminal text regression");
        if(nested)ImGui::BeginChild("Scrolled child",ImVec2(0,150));
        // Exercise clipping/scrollbars and calls that end a group without any
        // later checkbox/dummy masking the text's pending state.
        ImGui::Indent(12);ImGui::BeginGroup();
        if(nested)for(int i=0;i<20;i++)Ui::TextUnformatted("Settings");
        float startX=ImGui::GetCursorPosX(),indent=ImGui::GetCurrentWindow()->DC.Indent.x;
        finalText(kind);
        assert(fabsf(ImGui::GetCursorPosX()-startX)<0.1f);
        assert(fabsf(ImGui::GetCurrentWindow()->DC.Indent.x-indent)<0.1f);
        ImGui::EndGroup();ImGui::Unindent(12);
        if(nested)ImGui::EndChild();
        // Same last-item sequence as the real screenshot, including diagnostics
        // ON/OFF. End() follows the text immediately.
        Ui::TextDisabled("F1: close | PgUp/PgDn: tabs | Tab: navigate");
        if(kind%2)Ui::TextDisabled("UI keys/clicks: %lu/%lu",1ul,2ul);
        assert(!ImGui::GetCurrentWindow()->DC.IsSetPos);
        ImGui::End();ImGui::Render();
        assert(GImGui->ErrorCountCurrentFrame==0&&capturedErrors==0);
        ++frames;
    }
    io.FontGlobalScale=1;UiText::language=0;
    std::cout<<"Layout: "<<frames<<" actual strict-assert ImGui frames: terminal Arabic/English text, empty/multiline,3 scales,groups/children/clipping/scrollbars,footer/diagnostics; ZERO captured errors, no suppressed production diagnostics.\n";
}
static void frame() {
    auto&io=ImGui::GetIO();io.DisplaySize=ImVec2(1280,960);io.DeltaTime=1.0f/60;
    ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(760,720));ImGui::Begin("Offline localization fixture");
    drawSettings();
    Ui::TextWrapped("Offline only. Park first; the menu does not pause the game.");
    Ui::Text("Engine balance: %d WP",123456);
    Ui::TextWrapped("%s","Apply OK: 100 -> 123456 WP (read back from engine).");
    Ui::TextWrapped("%s","Color 12 selected.");
    bool on=false;Ui::Checkbox("Automatic speed - Player",&on);
    // End on text, as the actual production window does (NOT a masking item).
    Ui::TextDisabled("F1: close | PgUp/PgDn: tabs | Tab: navigate");
    ImGui::End();ImGui::Render();assert(ImGui::GetDrawData()->TotalVtxCount>100);
    assert(GImGui->ErrorCountCurrentFrame==0&&capturedErrors==0);
}
static void newControlsRegression(){
    auto&io=ImGui::GetIO();io.DisplaySize=ImVec2(1920,1080);io.DeltaTime=1.0f/60;
    unsigned frames=0;
    for(int lang:{0,1})for(float scale:{0.9f,1.23f,1.6f})for(int preset=0;preset<=5;preset++)for(int state=0;state<4;state++){
        UiText::language=lang;io.FontGlobalScale=scale;showDiagnostics=state&1;
        PowertrainMod::enabled=state==1;PowertrainMod::autoFollowing=false;PowertrainMod::unsupportedTarget=state==1;
        PowertrainMod::isParked=state!=1;SpeedMod::enabled=state==1||state==2;SpeedMod::targetMph=500;
        World::bound=state!=3;World::command=preset;World::live=state!=3;World::freeze=state!=3;World::supportedTimer=state==1;
        World::traffic=state==1;World::policeOff=state==1;World::streetFailed=state==3;World::trafficUnavailable=state==3;
        VehicleMod::NativeSignals::ready=state!=3;VehicleMod::NativeSignals::enabled=state==1;
        World::passengers=state!=2;World::density=state==0?0:state==1?50:state==2?100:1;
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(760,840));ImGui::Begin("Production speed/world controls");
        ImGui::OpenPopup("SpeedTargetPicker");PowertrainMod::draw();
        assert(!GImGui->OpenPopupStack.empty()&&GImGui->OpenPopupStack.back().Window);
        ImGui::Separator();World::draw();SignalControls::draw();
        assert(!ImGui::GetCurrentWindow()->DC.IsSetPos);
        ImGui::End();ImGui::Render();assert(GImGui->ErrorCountCurrentFrame==0&&capturedErrors==0);
        assert(ImGui::GetDrawData()->TotalVtxCount>100);++frames;
    }
    // Open the actual production weather combo, including all translated
    // choices, not just the collapsed selector tested in the grid above.
    for(int lang:{0,1}){
        UiText::language=lang;World::bound=true;World::failed=false;World::streetFailed=false;World::command=3;
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(760,840));ImGui::Begin("Weather combo popup");
        ImGuiID id=ImGui::GetCurrentWindow()->GetID(UiText::label("Weather").c_str());
        ImGui::OpenPopupEx(ImHashStr("##ComboPopup",0,id));World::draw();
        assert(!GImGui->OpenPopupStack.empty()&&GImGui->OpenPopupStack.back().Window);
        ImGui::End();ImGui::Render();assert(GImGui->ErrorCountCurrentFrame==0&&capturedErrors==0);++frames;
    }
    io.FontGlobalScale=1;VehicleMod::NativeSignals::ready=true;
    for(int lang:{0,1})for(const char*label:{"Keyboard key","Controller button"}){
        UiText::language=lang;ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(760,840));ImGui::Begin("Signal binding popup");
        ImGui::PushID(0);ImGuiID id=ImGui::GetCurrentWindow()->GetID(UiText::label(label).c_str());ImGui::PopID();
        ImGui::OpenPopupEx(ImHashStr("##ComboPopup",0,id));SignalControls::draw();
        assert(!GImGui->OpenPopupStack.empty()&&GImGui->OpenPopupStack.back().Window);
        ImGui::End();ImGui::Render();assert(GImGui->ErrorCountCurrentFrame==0&&capturedErrors==0);++frames;
    }
    io.FontGlobalScale=1;UiText::language=0;showDiagnostics=false;
    std::cout<<"New UI: "<<frames<<" real strict ImGui frames, production target popup / World / signals draw bodies and actual keyboard/controller binding popups, single six-choice weather selector, Arabic/English, three scales, police/traffic ON/OFF/failed states, traffic level 0/1/50/100, default passenger switch ON/OFF, unavailable/locked/unsupported/diagnostic states; zero layout errors. NOT in-game acceptance.\n";
}
int main(){
    assert(UiText::language==0);
    loadSettings();assert(UiText::language==0&&menuScalePercent==125);
    prefs[{L"Menu",L"Language"}]=1;loadSettings();assert(UiText::language==1);
    prefs[{L"Menu",L"Language"}]=123;loadSettings();assert(UiText::language==0);
    UiText::language=1;saveSettings();assert(prefs.at({L"Menu",L"Language"})==1);
    UiText::language=0;loadSettings();assert(UiText::language==1);
    UiText::language=0;saveSettings();loadSettings();assert(UiText::language==0);
    assert(prefs.size()==10); // safe prefs + six bindings; no live lamp/feature state
    assert(SignalControls::keyboard[0]==0x5a&&SignalControls::controller[2]==1);
    assert(SignalControls::choose(2,0x26,true)&&SignalControls::choose(2,15,false));saveSettings();
    const int keys[]={0x5a,0x43,0x58},pads[]={3,4,1};SignalControls::loadBindings(keys,pads);loadSettings();
    assert(SignalControls::keyboard[2]==0x26&&SignalControls::controller[2]==15&&!VehicleMod::NativeSignals::enabled);
    canWrite=false;saveSettings();assert(!strcmp(settingsStatus,"Could not save mod/settings.ini."));canWrite=true;
    prepareContext();layoutRegression();newControlsRegression();auto&io=ImGui::GetIO();
    unsigned char*pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);assert(pixels&&width>0&&height>0);
    auto baked=io.FontDefault->GetFontBaked(16);
    unsigned glyphs=0;
    for(const auto&t:UiText::translations){
        for(const char*text:{t.visual,t.wrapped}){
            const char*p=text;
            while(*p){unsigned cp;int n=ImTextCharFromUtf8(&cp,p,nullptr);assert(n>0);p+=n;
                if(cp>=32){assert(baked->FindGlyphNoFallback(cp)!=nullptr);++glyphs;}
            }
        }
    }
    UiText::language=0;
    auto ar=UiText::label("Settings");assert(ar!="Settings###Settings");
    auto id=ImHashStr(ar.c_str());
    assert(formatted("Engine balance: %d WP",123456).find("123456")!=std::string::npos);
    assert(UiText::display("Color 12 selected.").find("12")!=std::string::npos);
    assert(UiText::display("Apply OK: 123 -> 456 WP (read back from engine).").find("456")!=std::string::npos);
    assert(UiText::display("Apply OK: 123 -> 456 WP (read back from engine).").find("123")!=std::string::npos);
    for(int i=0;i<30;i++){
        UiText::language=i%2;assert(ImHashStr(UiText::label("Settings").c_str())==id);frame();
    }
    UiText::language=1;assert(UiText::display("Settings")=="Settings");
    assert(formatted("Engine balance: %d WP",123456)=="Engine balance: 123456 WP");
    assert(UiText::display("Color 12 selected.")=="Color 12 selected.");
    ImGui::DestroyContext();
    std::cout<<"UI: "<<glyphs<<" actual embedded-font glyph lookups, connected Arabic visual strings, unchanged English/digits, stable bilingual widget IDs,30 actual ImGui frames, mocked INI default/persistence/failure tests passed. NOT in-game visual acceptance.\n";
}
