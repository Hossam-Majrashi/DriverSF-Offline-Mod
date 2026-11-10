// Existing model paint variants, selected per current car via the native setter.
// No writes to shared model shader definitions and no arbitrary RGB shader patch.
// Model-specific native presets; queued changes are ownership/thread guarded.
#include "paint_policy.h"
namespace PaintMod {
using Selection=PaintPolicy::Selection;
using NativeSetter=void (__thiscall *)(void*,const Selection*,bool);
static NativeSetter setPaint=nullptr;
static std::atomic<bool> ready{false};
static char paintStatus[192]="Paint binding pending.";
struct Snapshot {
    BYTE *vehicle,*resource,*physics;
    Selection selection;
    uint32_t groups,counts[3];
};
struct Pending {Snapshot before;int variant;uint32_t token;DWORD deadline;bool active;};
static Pending pending{};
static uint32_t generation=0;
static int selectedVariant=0;
static bool liveEnabled=false;
static uintptr_t displayedCar=0,displayedResource=0,displayedPhysics=0;
static unsigned long applied=0,cancelled=0;

static bool capture(Snapshot &s) {
    s={};
    if(!ready)return false;
    s.vehicle=VehicleMod::playerVehicle();
    if(!VehicleMod::writable(s.vehicle,0x5f4) || *(int*)(s.vehicle+0x544)!=3)return false;
    s.physics=VehicleMod::vehiclePhysics(s.vehicle);
    if(!VehicleMod::writable(s.physics,0x11c0))return false;
    s.resource=*(BYTE**)(s.vehicle+0x5b4);
    if(!readable(s.resource,0x2c64))return false;
    // Model+0x310 holds <=3 paint groups, each <=16 compiled presets.
    s.groups=*(uint32_t*)(s.resource+0x310+0x294c);
    if(s.groups<1 || s.groups>3)return false;
    for(uint32_t i=0;i<s.groups;i++)
        s.counts[i]=*(uint32_t*)(s.resource+0x310+i*0xdc4+0xdc0);
    memcpy(&s.selection,s.vehicle+0x5a4,sizeof(s.selection));
    return PaintPolicy::valid(s.selection,s.groups,s.counts);
}
static bool sameCarState(const Snapshot &a,const Snapshot &b) {
    if(a.vehicle!=b.vehicle || a.resource!=b.resource || a.physics!=b.physics ||
       a.groups!=b.groups || !PaintPolicy::same(a.selection,b.selection))return false;
    for(uint32_t i=0;i<a.groups;i++)if(a.counts[i]!=b.counts[i])return false;
    return true;
}
static void cancelPending() {
    ++generation;
    if(pending.active){++cancelled;strcpy(paintStatus,"Queued paint change cancelled.");}
    pending.active=false;
}
static void bind() {
    if(!bound){strcpy(paintStatus,"Paint requires the supported executable and offline mode.");return;}
    // Native selection setter: thiscall; 2 stack arguments; ret 8.
    const BYTE setterPattern[]={0x53,0x8b,0x5c,0x24,0x08,0x8b,0x03,0x56,0x8b,0xf1,0x57,0x8d,0xbe,0xa4,0x05,0,0,0x89,0x07,0x8b,0x4b,0x04,0x89,0x4f,0x04};
    BYTE *entry=VehicleMod::uniqueCode(setterPattern,sizeof(setterPattern));
    const BYTE tail[]={0x5f,0x5e,0x5b,0xc2,0x08,0x00};
    if(!entry || !inside(entry,0x7c) || memcmp(entry+0x76,tail,sizeof(tail))) {
        strcpy(paintStatus,"Paint setter validation failed; no color writes enabled.");logLine(paintStatus);return;
    }
    setPaint=(NativeSetter)entry;ready=true;
    strcpy(paintStatus,"Enable Live color to browse colors.");
    char line[160];snprintf(line,sizeof(line),"paint setter bound RVA=%lx; no shared shader edits",(unsigned long)(entry-image));logLine(line);
}
static void request(int variant) {
    Snapshot before{};Selection input{},normalized{};
    if(!capture(before) || !PaintPolicy::choose(before.selection,before.groups,before.counts,variant,input,normalized)) {
        strcpy(paintStatus,"Paint refused: no attached supported car or invalid variant.");return;
    }
    pending={before,variant,++generation,GetTickCount()+2000,true};
    if(!paintMessage || !PostMessage(windowHandle,paintMessage,(WPARAM)pending.token,0)) {
        cancelPending();strcpy(paintStatus,"Paint request could not reach the game thread.");return;
    }
    strcpy(paintStatus,"Changing color...");
}
static void execute(uint32_t token) {
    if(!pending.active || token!=pending.token || token!=generation)return;
    Pending work=pending;pending.active=false;
    Snapshot now{};Selection input{},expected{};
    if(GetCurrentThreadId()!=GetWindowThreadProcessId(windowHandle,nullptr) ||
       !PaintPolicy::fresh(GetTickCount(),work.deadline) || !capture(now) ||
       !sameCarState(now,work.before) ||
       !PaintPolicy::choose(now.selection,now.groups,now.counts,work.variant,input,expected)) {
        ++cancelled;liveEnabled=false;
        strcpy(paintStatus,"Change cancelled. Enable Live color again.");logLine(paintStatus);return;
    }
    // Calls the game's own setter and its visual/physics refresh paths. No
    // script-VM calls from the render worker and no raw selection/resource writes.
    setPaint(now.vehicle,&input,false);
    Snapshot after{};
    if(capture(after) && after.vehicle==now.vehicle && after.resource==now.resource &&
       after.physics==now.physics && PaintPolicy::same(after.selection,expected)) {
        ++applied;
        snprintf(paintStatus,sizeof(paintStatus),"Color %d selected.",work.variant+1);
    } else {
        liveEnabled=false;
        strcpy(paintStatus,"Color check failed. Live color stopped.");
    }
    logLine(paintStatus);
}
static void stopLive() {
    liveEnabled=false;cancelPending();
}
// Observe only: never apply automatically on a frame, car switch, or checkbox enable.
// Called even with the menu closed/another tab selected to invalidate old ownership.
static void observe() {
    Snapshot car{};
    if(!capture(car)) {
        if(liveEnabled || pending.active)stopLive();
        displayedCar=displayedResource=displayedPhysics=0;
        return;
    }
    if(displayedCar!=uintptr_t(car.vehicle) || displayedResource!=uintptr_t(car.resource) ||
       displayedPhysics!=uintptr_t(car.physics)) {
        bool wasActive=liveEnabled || pending.active;
        stopLive();
        displayedCar=uintptr_t(car.vehicle);displayedResource=uintptr_t(car.resource);
        displayedPhysics=uintptr_t(car.physics);selectedVariant=car.selection.indices[0];
        strcpy(paintStatus,wasActive?"Car changed. Enable Live color again.":"Enable Live color to browse colors.");
    } else if(!liveEnabled && !pending.active)selectedVariant=car.selection.indices[0];
}
static void enableLive(bool enabled) {
    if(!enabled){stopLive();strcpy(paintStatus,"Live color off.");return;}
    observe();Snapshot car{};
    liveEnabled=capture(car) && car.counts[0]>1;
    if(liveEnabled){selectedVariant=car.selection.indices[0];strcpy(paintStatus,"Choose a color. Changes apply immediately.");}
}
static void selectVariant(int variant) {
    if(!liveEnabled)return;
    observe();if(!liveEnabled)return;
    Snapshot car{};
    if(!capture(car) || variant<0 || uint32_t(variant)>=car.counts[0])return;
    if(pending.active && selectedVariant==variant)return;
    selectedVariant=variant;
    if(variant==car.selection.indices[0]) {
        cancelPending();strcpy(paintStatus,"Current color selected.");return;
    }
    // The latest user selection supersedes earlier queued messages. The slider
    // stays usable while pending, but only the matching token can apply.
    request(variant);
    if(!pending.active)liveEnabled=false;
}

static void draw() {
    if(!Ui::CollapsingHeader("Car color",ImGuiTreeNodeFlags_DefaultOpen))return;
    Snapshot car{};bool attached=capture(car);
    if(!attached)Ui::TextDisabled("Enter a car and leave Shift.");
    if(attached && car.counts[0]<2)Ui::TextDisabled("This car has one paint option.");
    ImGui::BeginDisabled(!attached || (attached && car.counts[0]<2));
    bool enabled=liveEnabled;
    if(Ui::Checkbox("Live stock color",&enabled))enableLive(enabled);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!attached || !liveEnabled || (attached && car.counts[0]<2));
    int variant=selectedVariant+1;
    if(Ui::SliderInt("Stock Color",&variant,1,attached?int(car.counts[0]):1))selectVariant(variant-1);
    if(Ui::Button("< Previous"))selectVariant(selectedVariant>0?selectedVariant-1:int(car.counts[0])-1);
    ImGui::SameLine();
    if(Ui::Button("Next >"))selectVariant(selectedVariant+1<int(car.counts[0])?selectedVariant+1:0);
    ImGui::EndDisabled();
    Ui::TextWrapped("%s",paintStatus);
    if(showDiagnostics)Ui::Text("Paint readbacks: %lu | cancelled: %lu",applied,cancelled);
}
} // namespace PaintMod
