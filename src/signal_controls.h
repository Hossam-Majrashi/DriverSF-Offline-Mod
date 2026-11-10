#include "signal_bindings.h"
namespace SignalControls {
static SignalInputPolicy::Edges edges;
static bool receiving(){return !visible && uiReady && GetForegroundWindow()==windowHandle
    && !(GetAsyncKeyState(VK_MENU)&0x8000) && !(GetAsyncKeyState(VK_LWIN)&0x8000) && !(GetAsyncKeyState(VK_RWIN)&0x8000)
    && VehicleMod::NativeSignals::usable();}
static bool consumesKey(int key){
    if(!key || !receiving())return false;
    for(unsigned i=0;i<3;i++)if(keyboard[i]==key)return true;
    return false;
}
static uint32_t consumedPad(){
    if(!receiving())return 0;
    uint32_t bits=0;for(unsigned i=0;i<3;i++){int p=controller[i];if(p>=0 && unsigned(p)<SignalInputPolicy::padCount)bits|=SignalInputPolicy::pads[p];}
    return bits;
}
static UINT keyScan(int key){
    UINT s=key?MapVirtualKeyW(UINT(key),MAPVK_VK_TO_VSC_EX):0;
    return s?((s&0xff)|((s&0xff00)?0x80:0)):0;
}
static void filterKeyboard(PBYTE p){
    if(!p || !receiving())return;
    for(unsigned i=0;i<3;i++){int k=keyboard[i];if(k>0 && k<256)p[k]=0;}
}
static void filterDirectKeyboard(PBYTE p){
    if(!p || !receiving())return;
    for(unsigned i=0;i<3;i++){UINT s=keyScan(keyboard[i]);if(s && s<256)p[s]=0;}
}
static bool consumesScan(DWORD scan){
    if(!receiving())return false;
    for(unsigned i=0;i<3;i++){
        int key=keyboard[i];if(!key)continue;
        UINT s=keyScan(key);
        if(s && scan==s)return true;
    }return false;
}
static void filterPad(XINPUT_GAMEPAD&p){
    uint32_t mask=consumedPad();p.wButtons&=WORD(~mask);
    if(mask&(1u<<16))p.bLeftTrigger=0;if(mask&(1u<<17))p.bRightTrigger=0;
}
static void poll(){
    if(!VehicleMod::NativeSignals::enabled || !VehicleMod::NativeSignals::ready){edges={};return;}
    bool down[3];int padBindings[3];
    for(unsigned i=0;i<3;i++){int k=keyboard[i];down[i]=k && (GetAsyncKeyState(k)&0x8000)!=0;padBindings[i]=controller[i];}
    XINPUT_STATE s{};bool connected=XInputGetState(0,&s)==ERROR_SUCCESS;
    uint32_t pad=connected?s.Gamepad.wButtons:0;
    if(connected && s.Gamepad.bLeftTrigger>=128)pad|=1u<<16;
    if(connected && s.Gamepad.bRightTrigger>=128)pad|=1u<<17;
    unsigned action=edges.poll(receiving(),connected,down,pad,padBindings);
    if(action)VehicleMod::NativeSignals::request(action); // queue only, no engine calls
}
static void draw(){
    namespace N=VehicleMod::NativeSignals;
    ImGui::Separator();Ui::TextWrapped("Player turn signals / hazards");
    ImGui::BeginDisabled(!N::ready);
    bool on=N::enabled;
    if(Ui::Checkbox("Enable player indicators",&on)){N::enabled=on;N::cancel();}
    ImGui::BeginDisabled(!on);
    if(Ui::Button("Turn left"))N::request(1);
    ImGui::SameLine();if(Ui::Button("Turn right"))N::request(2);
    if(Ui::Button("Hazard flashers"))N::request(3);
    ImGui::SameLine();if(Ui::Button("Signals OFF"))N::request(0);
    ImGui::EndDisabled();
    const char*actions[]={"Left indicator binding","Right indicator binding","Hazard binding"};
    for(unsigned a=0;a<3;a++){
        ImGui::PushID(int(a));Ui::TextWrapped(actions[a]);
        int key=SignalInputPolicy::keyIndex(keyboard[a]);if(key<0)key=0;
        if(ImGui::BeginCombo(UiText::label("Keyboard key").c_str(),UiText::lookup(SignalInputPolicy::keyNames[key]))){
            for(unsigned i=0;i<SignalInputPolicy::keyCount;i++)if(ImGui::Selectable(UiText::label(SignalInputPolicy::keyNames[i]).c_str(),int(i)==key))choose(a,SignalInputPolicy::keys[i],true);
            ImGui::EndCombo();
        }
        int p=controller[a];if(p<0 || unsigned(p)>=SignalInputPolicy::padCount)p=0;
        if(ImGui::BeginCombo(UiText::label("Controller button").c_str(),UiText::lookup(SignalInputPolicy::padNames[p]))){
            for(unsigned i=0;i<SignalInputPolicy::padCount;i++)if(ImGui::Selectable(UiText::label(SignalInputPolicy::padNames[i]).c_str(),int(i)==p))choose(a,int(i),false);
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    Ui::TextWrapped("Press once to toggle; the same signal again turns it off. Default controller: Left / Right / Up for hazards. Default keyboard: Z / C / X. Active bindings are reserved only in safe free drive with the menu closed; other controls stay native. Save bindings in Settings. No automatic steering cancel.");
    Ui::TextWrapped("%s",bindingStatus);
    if(!N::ready)Ui::TextDisabled("Native player indicators unavailable.");
}
}
