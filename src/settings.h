// Safe preferences/bindings only; active lamps/gameplay switches are not saved.
#include "complete_hook.h"
static const WCHAR*signalKeyNames[]={L"LeftKey",L"RightKey",L"HazardKey"};
static const WCHAR*signalPadNames[]={L"LeftPad",L"RightPad",L"HazardPad"};
static WCHAR settingsPath[MAX_PATH];
static int menuScalePercent=125;
static char settingsStatus[160]="Preferences loaded.";
static int clampSetting(int value,int low,int high,int fallback) {
    return value>=low && value<=high?value:fallback;
}
static void loadSettings() {
    if(!GetModuleFileNameW(nullptr,settingsPath,MAX_PATH))return;
    WCHAR *slash=wcsrchr(settingsPath,L'\\');if(!slash)return;
    *slash=0;
    if(wcslen(settingsPath)+wcslen(L"\\mod\\settings.ini")>=MAX_PATH){settingsPath[0]=0;return;}
    wcscat(settingsPath,L"\\mod\\settings.ini");
    UiText::language=clampSetting(GetPrivateProfileIntW(L"Menu",L"Language",0,settingsPath),0,1,0);
    menuScalePercent=clampSetting(GetPrivateProfileIntW(L"Menu",L"ScalePercent",125,settingsPath),90,160,125);
    VehicleMod::boostCapKmh=clampSetting(GetPrivateProfileIntW(L"Speed",L"BoostCeilingKmh",300,settingsPath),150,350,300);
    int n=clampSetting(GetPrivateProfileIntW(L"Money",L"AmountInput",100000,settingsPath),0,POLICY_MAX,100000);
    snprintf(amountText,sizeof(amountText),"%d",n);
    int keys[3],pads[3];const int defaultsKeys[]={0x5a,0x43,0x58},defaultsPad[]={3,4,1};
    for(unsigned i=0;i<3;i++){
        keys[i]=GetPrivateProfileIntW(L"Indicators",signalKeyNames[i],defaultsKeys[i],settingsPath);
        pads[i]=GetPrivateProfileIntW(L"Indicators",signalPadNames[i],defaultsPad[i],settingsPath);
    }
    SignalControls::loadBindings(keys,pads);
    logLine("v7.14 safe preferences/indicator bindings loaded; no live lamp/gameplay state restored");
}
static bool writeSetting(const WCHAR *section,const WCHAR *key,int value) {
    WCHAR text[32];swprintf(text,32,L"%d",value);
    return settingsPath[0] && WritePrivateProfileStringW(section,key,text,settingsPath);
}
static void saveSettings() {
    int n=100000;parseAmount(amountText,n);
    bool ok=writeSetting(L"Menu",L"Language",UiText::language);
    ok=writeSetting(L"Menu",L"ScalePercent",menuScalePercent) && ok;
    ok=writeSetting(L"Speed",L"BoostCeilingKmh",VehicleMod::boostCapKmh.load()) && ok;
    ok=writeSetting(L"Money",L"AmountInput",n) && ok;
    for(unsigned i=0;i<3;i++){
        ok=writeSetting(L"Indicators",signalKeyNames[i],SignalControls::keyboard[i]) && ok;
        ok=writeSetting(L"Indicators",signalPadNames[i],SignalControls::controller[i]) && ok;
    }
    strcpy(settingsStatus,ok?"Preferences saved.":"Could not save mod/settings.ini.");
    logLine(settingsStatus);
}
static void drawSettings() {
    Ui::TextUnformatted("Developer: Hossam Majrashi");
    Ui::TextWrapped("GitHub: https://github.com/Hossam-Majrashi");
    if(Ui::Button("Copy GitHub URL"))ImGui::SetClipboardText("https://github.com/Hossam-Majrashi");
    ImGui::Separator();
    Ui::TextUnformatted("Game Completion [100%]");
    if(Ui::Button("Complete Game [100%]: All Missions, Dares & Tokens")) {
        if(onCompleteGame100) onCompleteGame100();
        strcpy(settingsStatus,"[100%] All missions, dares, tokens & cars unlocked and saved.");
    }
    Ui::TextDisabled("Unlocks all main story missions, challenges, city movie tokens, garages and cars.");
    ImGui::Separator();
    Ui::TextUnformatted("Language / English");
    ImGui::PushID("ui-language");
    if(Ui::Button("Arabic")) {
        UiText::language=0;
        strcpy(settingsStatus,writeSetting(L"Menu",L"Language",0)?"Language saved.":"Could not save mod/settings.ini.");
    }
    ImGui::SameLine();
    if(Ui::Button("English")) {
        UiText::language=1;
        strcpy(settingsStatus,writeSetting(L"Menu",L"Language",1)?"Language saved.":"Could not save mod/settings.ini.");
    }
    ImGui::PopID();
    Ui::Checkbox("Show technical details",&showDiagnostics);
    if(showDiagnostics)Ui::TextDisabled("Technical diagnostics below remain in English.");
    if(Ui::SliderInt("Menu scale (%)",&menuScalePercent,90,160))
        ImGui::GetIO().FontGlobalScale=menuScalePercent/100.0f;
    if(Ui::Button("Save safe preferences"))saveSettings();
    if(Ui::Button("Restore menu defaults")) {
        UiText::language=0;menuScalePercent=125;showDiagnostics=false;ImGui::GetIO().FontGlobalScale=1.25f;
        strcpy(settingsStatus,"Defaults restored. Save to keep them.");
    }
    Ui::TextWrapped("%s",settingsStatus);
    Ui::TextDisabled("Indicators start OFF; passenger removal defaults ON. Saved bindings do not enable features.");
    if(showDiagnostics)Ui::TextWrapped("Preferences: mod/settings.ini. Updating the DLL requires a game restart. Technical counters do not prove gameplay effects.");
}
