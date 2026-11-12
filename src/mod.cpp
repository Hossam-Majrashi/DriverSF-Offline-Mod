// Driver SF 1.04.1114 x86. Supported executable is hash-checked; offline only.
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <wincrypt.h>
#include <d3d9.h>
#include <dinput.h>
#include <xinput.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <atomic>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include "MinHook.h"
#include "ui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static std::atomic<bool> visible{false}, infinite{false};
static bool uiReady=false, bound=false, focusMenu=false;
static bool showDiagnostics=false;
static CRITICAL_SECTION uiLock;
static UINT applyMessage,repairMessage,paintMessage;
static unsigned long menuKeyEvents=0,menuMouseEvents=0;
static int currentTab=0,requestedTab=-1;
static constexpr int menuTabCount=4;
static std::atomic<uint32_t> applyGeneration{1};
static HWND windowHandle; static WNDPROC oldWndProc;
static RECT oldClip; static bool hadClip=false; static int cursorAdds=0;
static char status[256]="Load an offline Story save before changing Willpower.";
static char amountText[32]="100000";
static const int POLICY_MAX=9999999; // Conservative mod policy, NOT an engine limit.
static BYTE *image; static size_t imageSize;
static uintptr_t *singletonSlot;
static void *setterTarget;
typedef void (__thiscall *SetBalance)(int*,int,int);
static SetBalance originalSet;
static std::atomic<DWORD> setterThread{0};
static LONG blockedSpends=0, normalChanges=0;
static HANDLE logHandle=INVALID_HANDLE_VALUE;
static void logLine(const char *s) {
    if(logHandle==INVALID_HANDLE_VALUE) return;
    char b[600]; int n=snprintf(b,sizeof(b),"[%lu tid=%lu] %s\r\n",GetTickCount(),GetCurrentThreadId(),s);
    if(n<0)return;
    if(size_t(n)>=sizeof(b)){n=int(sizeof(b))-1;b[n-2]='\r';b[n-1]='\n';}
    DWORD written; WriteFile(logHandle,b,DWORD(n),&written,nullptr);
}
static bool readable(const void *p,size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    return uintptr_t(p)+n>=uintptr_t(p) && uintptr_t(p)+n<=uintptr_t(m.BaseAddress)+m.RegionSize;
}
static bool inside(const void *p,size_t n) { return uintptr_t(p)>=uintptr_t(image) && uintptr_t(p)+n<=uintptr_t(image)+imageSize; }
static int *profile() {
    if(!singletonSlot || !readable(singletonSlot,4)) return nullptr;
    uintptr_t root=*singletonSlot;
    if(!readable((void*)root,0x370)) return nullptr;
    uintptr_t p=*(uintptr_t*)(root+0x36c);
    return readable((void*)p,0x4d64)?(int*)p:nullptr;
}
static int *wallet() { int *p=profile(); return p?(int*)((BYTE*)p+0x14c):nullptr; }
static bool validWallet(int *w) { return w && *w>=0 && *w<=POLICY_MAX; }
static bool addFits(int value,int64_t delta) { int64_t n=int64_t(value)+delta; return n>=0 && n<=INT_MAX; }
static bool safeApply(int *p,int value) {
    int *w=(int*)((BYTE*)p+0x14c); int64_t delta=int64_t(value)-*w;
    if(delta>0) {
        const size_t offsets[]={0x154,0x2f48,0x4d58,0x2f44};
        for(size_t off:offsets) if(!addFits(*(int*)((BYTE*)p+off),delta)) return false;
    } else {
        if(!addFits(*(int*)((BYTE*)p+0x2f40),-delta) || !addFits(*(int*)((BYTE*)p+0x4d60),-delta)) return false;
    }
    return true;
}
static void __fastcall hookedSet(int *self,void*,int value,int source) {
    int *w=wallet(); int before=(w==self)?*w:0;
    setterThread=GetCurrentThreadId();
    // Source 3 is the purchase path observed in 0x70ebec. Do not suppress
    // resets, save loading, or a user's explicit Apply (source 1).
    if(infinite && self==w && validWallet(w) && source==3 && value>=0 && value<before) {
        InterlockedIncrement(&blockedSpends);
        char b[180];snprintf(b,sizeof(b),"spend blocked: %d -> %d source=%d",before,value,source);logLine(b);
        value=before;
    } else if(self==w) {
        InterlockedIncrement(&normalChanges);
        char b[180];snprintf(b,sizeof(b),"balance setter: %d -> %d source=%d",before,value,source);logLine(b);
    }
    originalSet(self,value,source);
}
static bool expectedFileHash(const WCHAR *path,const char *expected) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider=0; HCRYPTHASH hash=0; bool ok=false;
    if(CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) && CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)) {
        BYTE buf[16384], digest[32]; DWORD n=0; bool readOK=true;
        while(true) { if(!ReadFile(f,buf,sizeof(buf),&n,nullptr)){readOK=false;break;} if(!n)break; if(!CryptHashData(hash,buf,n,0)){readOK=false;break;} }
        DWORD len=32;
        if(readOK && CryptGetHashParam(hash,HP_HASHVAL,digest,&len,0)) {
            char text[65]; for(int i=0;i<32;i++)snprintf(text+i*2,3,"%02x",digest[i]);
            ok=!strcmp(text,expected);
        }
    }
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);CloseHandle(f);return ok;
}
static bool expectedHash() {
    WCHAR path[MAX_PATH];DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    return n && n<MAX_PATH && expectedFileHash(path,"c79987b0548f7130337c3f295c8fe269e120ceddad55c1159e6f5cbc6aa26616");
}
static void bindMoney() {
    if(!expectedHash()){strcpy(status,"Unsupported Driver.exe SHA-256: money features disabled.");logLine(status);return;}
    if(!strstr(GetCommandLineA(),"-offline")){strcpy(status,"Money features require the official -offline launch option.");logLine(status);return;}
    // Resolve through the shipped Lua registration table, not a numeric game address.
    auto nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
    auto sec=IMAGE_FIRST_SECTION(nt); BYTE *name=nullptr,*callback=nullptr;
    int names=0,rows=0;
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++) {
        if(!(sec[i].Characteristics&IMAGE_SCN_MEM_READ) || (sec[i].Characteristics&IMAGE_SCN_MEM_WRITE))continue;
        BYTE *b=image+sec[i].VirtualAddress; size_t n=sec[i].Misc.VirtualSize;
        for(size_t k=0;k+13<=n;k++)if(!memcmp(b+k,"SetWillpower",12) && b[k+12]==0){name=b+k;names++;}
    }
    if(names!=1){strcpy(status,"SetWillpower string is not unique.");return;}
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++) {
        if(!(sec[i].Characteristics&IMAGE_SCN_MEM_READ) || (sec[i].Characteristics&IMAGE_SCN_MEM_WRITE))continue;
        BYTE *b=image+sec[i].VirtualAddress;size_t n=sec[i].Misc.VirtualSize;
        for(size_t k=0;k+16<=n;k+=4) if(*(uintptr_t*)(b+k)==uintptr_t(name)) {callback=*(BYTE**)(b+k+4);rows++;}
    }
    const BYTE begin[]={0xa1};
    if(rows!=1 || !inside(callback,0x3b) || memcmp(callback,begin,1) || memcmp(callback+0x14,"\x8b\xb0\x6c\x03\x00\x00",6) || memcmp(callback+0x2c,"\x8d\x8e\x4c\x01\x00\x00\xe8",7)) {
        strcpy(status,"SetWillpower callback validation failed.");return;
    }
    singletonSlot=*(uintptr_t**)(callback+1);
    BYTE *target=callback+0x37+*(int32_t*)(callback+0x33);
    const BYTE signature[]={0x8b,0x44,0x24,0x04,0x56,0x57,0x8b,0xf9,0x8b,0xf0,0x2b,0x37,0x89,0x07,0xa1};
    if(!inside(target,0x9e) || memcmp(target,signature,sizeof(signature)) || *(uintptr_t**)(target+15)!=singletonSlot || !inside(singletonSlot,4)) {
        strcpy(status,"Balance setter signature/pointer mismatch.");return;
    }
    setterTarget=target;
    if(MH_Initialize()!=MH_OK || MH_CreateHook(target,(void*)hookedSet,(void**)&originalSet)!=MH_OK || MH_EnableHook(target)!=MH_OK) {
        strcpy(status,"Balance hook installation failed.");logLine(status);return;
    }
    bound=true;
    char b[200];snprintf(b,sizeof(b),"bound by registration: callback RVA=%lx setter RVA=%lx singleton RVA=%lx",(unsigned long)(callback-image),(unsigned long)(target-image),(unsigned long)((BYTE*)singletonSlot-image));logLine(b);
}
#include "complete_game.h"
#include "vehicle.h"
#include "paint.h"
#include "speed.h"
#include "powertrain.h"
#include "controller.h"
#include "signal_controls.h"
static void execute100PercentCompletion() {
    logLine("execute100PercentCompletion: writing 100% save data...");
    bool wrote = CompleteGame::executeSavegameWrite();
    char b[180];
    snprintf(b, sizeof(b), "Complete Game 100%%: save file write %s", wrote ? "SUCCEEDED" : "FAILED");
    logLine(b);

    int *p = profile();
    if (p) {
        safeApply(p, 10000000);
        logLine("Complete Game 100%: wallet willpower set to 10,000,000");
    }

    VehicleMod::NitroLua::World::queueCompleteGame100();
}
static bool parseAmount(const char *s,int &v) {
    if(!*s)return false;int64_t n=0;
    for(;*s;s++){if(*s<'0'||*s>'9')return false;n=n*10+(*s-'0');if(n>POLICY_MAX)return false;}v=(int)n;return true;
}
#include "settings.h"
static void disableAllModifications() {
    ++applyGeneration;infinite=false;VehicleMod::disableAll();PaintMod::stopLive();PowertrainMod::disable();SpeedMod::disable();VehicleMod::NitroLua::World::disable();
    strcpy(status,"All switches OFF. Completed changes are not undone.");
    logLine(status);
}
// Runs only in the game's window/message thread, never in its render worker.
static void applyOnGameThread(int n) {
    int *p=profile(),*w=wallet();
    if(!bound){strcpy(status,"No validated balance hook.");return;}
    if(n<0 || n>POLICY_MAX){strcpy(status,"Amount outside the mod policy range.");return;}
    if(!validWallet(w) || !p || (*w==0 && !setterThread)){strcpy(status,"Load a Story save and earn Willpower first.");return;}
    DWORD tid=GetCurrentThreadId();
    DWORD observedThread=setterThread.load();
    if(tid!=GetWindowThreadProcessId(windowHandle,nullptr) || (observedThread && tid!=observedThread)) {strcpy(status,"Wrong game thread: Apply refused safely.");return;}
    if(!safeApply(p,n)){strcpy(status,"Apply refused: a signed 32-bit statistic would overflow.");return;}
    int before=*w;
    // Goes THROUGH the hooked engine function; never writes balance directly.
    ((SetBalance)setterTarget)(w,n,1);
    snprintf(status,sizeof(status),"Apply %s: %d -> %d WP (read back from engine).",*w==n?"OK":"FAILED",before,*w);logLine(status);
}
static void menuInput(bool on);
static void setVisible(bool on) {
    if(on==visible)return;
    visible=on;
    menuInput(on);
    if(on) {
        focusMenu=true;
        if(uiReady)ImGui::GetIO().AddFocusEvent(true);
        hadClip=GetClipCursor(&oldClip)!=FALSE;ClipCursor(nullptr);
        cursorAdds=0;do{cursorAdds++;}while(ShowCursor(TRUE)<0 && cursorAdds<32);
    } else {
        if(hadClip)ClipCursor(&oldClip);
        while(cursorAdds>0){ShowCursor(FALSE);cursorAdds--;}
        if(uiReady){ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();}
    }
    logLine(on?"menu opened":"menu closed");
}
static LRESULT CALLBACK wndProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    EnterCriticalSection(&uiLock);
    bool eaten=false; LRESULT result=0;
    if(m==applyMessage) {
        if(uint32_t(l)==applyGeneration.load())applyOnGameThread((int)w);
        else logLine("queued WP Apply cancelled by defaults/disable-all");
        eaten=true;
    }
    else if(m==repairMessage){VehicleMod::executeRepair();eaten=true;}
    else if(m==paintMessage){PaintMod::execute(uint32_t(w));eaten=true;}
    else if((m==WM_KEYDOWN || m==WM_SYSKEYDOWN) && w==VK_F1) {
        if(!(l&(1L<<30)))setVisible(!visible.load());eaten=true;
    } else if((m==WM_KEYUP || m==WM_SYSKEYUP) && w==VK_F1)eaten=true;
    else if((m==WM_KEYDOWN || m==WM_SYSKEYDOWN || m==WM_KEYUP || m==WM_SYSKEYUP)
        && SignalControls::consumesKey(int(w)))eaten=true;
    else {
        if(m==WM_KILLFOCUS)setVisible(false);
        if(visible && uiReady) {
            if(m==WM_KEYDOWN || m==WM_SYSKEYDOWN) {
                ++menuKeyEvents;
                if(w==VK_NEXT)requestedTab=(currentTab+1)%menuTabCount;
                if(w==VK_PRIOR)requestedTab=(currentTab+menuTabCount-1)%menuTabCount;
            }
            if(m==WM_LBUTTONDOWN || m==WM_RBUTTONDOWN)++menuMouseEvents;
            ImGui_ImplWin32_WndProcHandler(h,m,w,l);
            if(m==WM_SETCURSOR){SetCursor(LoadCursor(nullptr,IDC_ARROW));eaten=true;result=TRUE;}
            if((m>=WM_KEYFIRST && m<=WM_KEYLAST) || (m>=WM_MOUSEFIRST && m<=WM_MOUSELAST) || m==WM_INPUT || m==WM_INPUT_DEVICE_CHANGE)eaten=true;
        }
    }
    LeaveCriticalSection(&uiLock);
    return eaten?result:CallWindowProc(oldWndProc,h,m,w,l);
}
// Win32 polling and XInput suppression only in the game's IAT; ImGui keeps
// using the genuine APIs imported by this DLL.
static SHORT WINAPI quietAsync(int v){return visible || SignalControls::consumesKey(v)?0:GetAsyncKeyState(v);}
static SHORT WINAPI quietKey(int v){return visible || SignalControls::consumesKey(v)?0:GetKeyState(v);}
static BOOL WINAPI quietKeyboard(PBYTE p){if(visible){if(p)memset(p,0,256);return TRUE;}BOOL r=GetKeyboardState(p);if(r)SignalControls::filterKeyboard(p);return r;}
static DWORD WINAPI quietXInput(DWORD i,XINPUT_STATE *s){DWORD r=XInputGetState(i,s);if(r==ERROR_SUCCESS && s){if(visible)memset(&s->Gamepad,0,sizeof(s->Gamepad));else if(i==0)SignalControls::filterPad(s->Gamepad);}return r;}
static BOOL WINAPI quietClip(const RECT *r){return visible?TRUE:ClipCursor(r);}
static void *replaceImport(const char *dll,const char *name,void *replacement,WORD ordinal=0) {
    auto nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress)return nullptr;
    for(auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(image+dir.VirtualAddress);imp->Name;imp++) {
        if(_stricmp((char*)(image+imp->Name),dll) || !imp->OriginalFirstThunk)continue;
        auto names=(IMAGE_THUNK_DATA*)(image+imp->OriginalFirstThunk), slots=(IMAGE_THUNK_DATA*)(image+imp->FirstThunk);
        for(;names->u1.AddressOfData;names++,slots++) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                if(!ordinal || IMAGE_ORDINAL(names->u1.Ordinal)!=ordinal)continue;
            } else if(!name || strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(image+names->u1.AddressOfData))->Name,name))continue;
            DWORD old;void *previous=(void*)slots->u1.Function;
            if(!VirtualProtect(&slots->u1.Function,4,PAGE_READWRITE,&old))return nullptr;
            slots->u1.Function=(uintptr_t)replacement;VirtualProtect(&slots->u1.Function,4,old,&old);return previous;
        }
    }
    return nullptr;
}
// Vtable copies avoid patching executable graphics/input code.
struct InputRecord {void *object;void **original;void *table[32];bool reacquire,keyboard;};
static InputRecord inputs[64];static int inputCount=0;
static InputRecord *inputRecord(void *p){for(int i=0;i<inputCount;i++)if(inputs[i].object==p)return &inputs[i];return nullptr;}
static HRESULT WINAPI pollInput(void *self) {
    auto r=inputRecord(self);if(!r)return E_FAIL;
    if(visible)return DI_OK;
    return ((HRESULT(WINAPI*)(void*))r->original[25])(self);
}
static HRESULT WINAPI acquireInput(void *self) {
    auto r=inputRecord(self);if(!r)return E_FAIL;
    if(visible)return DI_OK;
    return ((HRESULT(WINAPI*)(void*))r->original[7])(self);
}
static void menuInput(bool on) {
    for(int i=0;i<inputCount;i++) {
        auto &r=inputs[i];
        if(on)r.reacquire=((HRESULT(WINAPI*)(void*))r.original[8])(r.object)==DI_OK;
        else if(r.reacquire) {
            ((HRESULT(WINAPI*)(void*))r.original[7])(r.object);
            DWORD count=INFINITE;
            ((HRESULT(WINAPI*)(void*,DWORD,DIDEVICEOBJECTDATA*,DWORD*,DWORD))r.original[10])(r.object,sizeof(DIDEVICEOBJECTDATA),nullptr,&count,0);
            r.reacquire=false;
        }
    }
}
static HRESULT WINAPI getState(void *self,DWORD n,void *data) {
    auto r=inputRecord(self);if(!r)return E_FAIL;
    if(visible && data){memset(data,0,n);return DI_OK;}
    HRESULT hr=((HRESULT(WINAPI*)(void*,DWORD,void*))r->original[9])(self,n,data);
    if(SUCCEEDED(hr) && r->keyboard && n==256 && data)SignalControls::filterDirectKeyboard((PBYTE)data);
    return hr;
}
static HRESULT WINAPI getData(void *self,DWORD size,DIDEVICEOBJECTDATA *data,DWORD *n,DWORD flags) {
    auto r=inputRecord(self);if(!r)return E_FAIL;
    auto fn=(HRESULT(WINAPI*)(void*,DWORD,DIDEVICEOBJECTDATA*,DWORD*,DWORD))r->original[10];
    if(visible && n){*n=0;return DI_OK;}
    HRESULT hr=fn(self,size,data,n,flags);
    if(SUCCEEDED(hr) && r->keyboard && data && n && size>=sizeof(DWORD) && size<=128 && *n<=4096){
        DWORD out=0;auto*b=(BYTE*)data;
        for(DWORD i=0;i<*n;i++)if(!SignalControls::consumesScan(*(DWORD*)(b+i*size))){
            if(out!=i)memmove(b+out*size,b+i*size,size);++out;
        }
        *n=out;
    }
    return hr;
}
static void hookInputDevice(void *p,bool keyboard) {
    if(inputCount>=64 || inputRecord(p))return;
    auto r=&inputs[inputCount++];r->object=p;r->original=*(void***)p;r->keyboard=keyboard;
    memcpy(r->table,r->original,sizeof(r->table));r->table[7]=(void*)acquireInput;r->table[9]=(void*)getState;r->table[10]=(void*)getData;r->table[25]=(void*)pollInput;
    *(void***)p=r->table;logLine("DirectInput device state/data intercepted");
}
struct FactoryRecord {void *object;void **original;void *table[11];};
static FactoryRecord factories[8];static int factoryCount=0;
static HRESULT WINAPI createInputDevice(void *self,REFGUID guid,void **out,IUnknown *outer) {
    FactoryRecord *r=nullptr;for(int i=0;i<factoryCount;i++)if(factories[i].object==self)r=&factories[i];
    if(!r)return E_FAIL;
    auto fn=(HRESULT(WINAPI*)(void*,REFGUID,void**,IUnknown*))r->original[3];HRESULT hr=fn(self,guid,out,outer);
    const GUID keyboardGuid={0x6f1d2b61,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0x00,0x00}};
    if(SUCCEEDED(hr) && out && *out)hookInputDevice(*out,memcmp(&guid,&keyboardGuid,sizeof(GUID))==0);return hr;
}
extern "C" HRESULT WINAPI proxyDirectInput8Create(HINSTANCE h,DWORD version,REFIID riid,LPVOID *out,LPUNKNOWN outer) {
    static HMODULE real=nullptr;static void *entry=nullptr;
    if(!real){WCHAR path[MAX_PATH];GetSystemDirectoryW(path,MAX_PATH);wcscat(path,L"\\dinput8.dll");real=LoadLibraryW(path);if(real)entry=(void*)GetProcAddress(real,"DirectInput8Create");}
    if(!entry)return E_FAIL;
    auto fn=(HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN))entry;HRESULT hr=fn(h,version,riid,out,outer);
    if(SUCCEEDED(hr) && out && *out && factoryCount<8) {
        auto r=&factories[factoryCount++];r->object=*out;r->original=*(void***)*out;memcpy(r->table,r->original,sizeof(r->table));r->table[3]=(void*)createInputDevice;*(void***)*out=r->table;
    }
    return hr;
}
static void **deviceOriginal=nullptr;static void *deviceTable[119];
static void drawMenu(IDirect3DDevice9 *device) {
    EnterCriticalSection(&uiLock);
    if(!uiReady) {
        D3DDEVICE_CREATION_PARAMETERS cp{};device->GetCreationParameters(&cp);windowHandle=cp.hFocusWindow;
        ImGui::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        onCompleteGame100 = execute100PercentCompletion;
        loadSettings();Ui::initFont();
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.FontGlobalScale=menuScalePercent/100.0f;
        ImGui::StyleColorsDark();ImGui_ImplWin32_Init(windowHandle);ImGui_ImplDX9_Init(device);
        oldWndProc=(WNDPROC)SetWindowLongPtr(windowHandle,GWLP_WNDPROC,(LONG_PTR)wndProc);
        uiReady=true;logLine("D3D9 overlay initialized; F1 ready");
    }
    ImGui_ImplDX9_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor=visible;
    VehicleMod::observe();
    PowertrainMod::observe();
    PaintMod::observe();
    SignalControls::poll();
    if(visible) {
        const ImVec2 area=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(24,24),ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(area.x<810?area.x-48:760,area.y<780?area.y-48:720),ImGuiCond_Always);
        if(focusMenu){ImGui::SetNextWindowFocus();focusMenu=false;}
        ImGui::Begin("Driver SF | Offline mod v7.14 | F1",nullptr,ImGuiWindowFlags_NoCollapse);
        if(Ui::Button("DISABLE ALL ACTIVE MODIFICATIONS",ImVec2(-1,0)))disableAllModifications();
        Ui::TextDisabled("Offline only. Park first; the menu does not pause the game.");
        if(ImGui::BeginTabBar("Features")) {
        if(Ui::BeginTabItem("Vehicle",nullptr,requestedTab==0?ImGuiTabItemFlags_SetSelected:0)) {currentTab=0;PowertrainMod::draw();if(showDiagnostics)SpeedMod::draw();VehicleMod::draw();PaintMod::draw();SignalControls::draw();ImGui::EndTabItem();}
        if(Ui::BeginTabItem("Money",nullptr,requestedTab==1?ImGuiTabItemFlags_SetSelected:0)) {currentTab=1;
        int *w=wallet();if(w)Ui::Text("Engine balance: %d WP",*w);else Ui::TextUnformatted("Engine balance: unavailable");
        ImGui::BeginDisabled(!bound);
        const char *moneyLabel=infinite?"Infinite Money: ON":"Infinite Money: OFF";
        if(Ui::Button(moneyLabel,ImVec2(-1,0))) {
            bool enabled=!infinite.load();infinite=enabled;
            strcpy(status,enabled?"Purchase deductions blocked.":"Normal spending restored.");logLine(status);
        }
        Ui::TextWrapped("Infinite Money prevents purchase deductions; you still need enough WP.");
        Ui::InputText("Amount (WP)",amountText,sizeof(amountText),ImGuiInputTextFlags_CharsDecimal);
        if(Ui::Button("Apply",ImVec2(100,0))) {
            int n;
            if(!parseAmount(amountText,n))snprintf(status,sizeof(status),"Enter whole digits only: 0..%d.",POLICY_MAX);
            else if(!PostMessage(windowHandle,applyMessage,(WPARAM)n,(LPARAM)applyGeneration.load()))strcpy(status,"Failed to queue Apply to the game thread.");
            else strcpy(status,"Apply queued to the game thread...");
        }
        ImGui::SameLine();
        if(Ui::Button("+100K")) {
            int current=0;parseAmount(amountText,current);
            int next=(current+100000>POLICY_MAX)?POLICY_MAX:current+100000;
            snprintf(amountText,sizeof(amountText),"%d",next);
            PostMessage(windowHandle,applyMessage,(WPARAM)next,(LPARAM)applyGeneration.load());
        }
        ImGui::SameLine();
        if(Ui::Button("+500K")) {
            int current=0;parseAmount(amountText,current);
            int next=(current+500000>POLICY_MAX)?POLICY_MAX:current+500000;
            snprintf(amountText,sizeof(amountText),"%d",next);
            PostMessage(windowHandle,applyMessage,(WPARAM)next,(LPARAM)applyGeneration.load());
        }
        ImGui::SameLine();
        if(Ui::Button("Max WP")) {
            snprintf(amountText,sizeof(amountText),"%d",POLICY_MAX);
            PostMessage(windowHandle,applyMessage,(WPARAM)POLICY_MAX,(LPARAM)applyGeneration.load());
        }
        ImGui::EndDisabled();
        if(Ui::Button("Restore money defaults")) {
            ++applyGeneration;infinite=false;strcpy(amountText,"100000");
            strcpy(status,"Money controls reset. Balance unchanged.");logLine(status);
        }
        Ui::TextDisabled("Set WP, then Apply. Changes may be saved by the game.");
        ImGui::Separator();Ui::TextWrapped("%s",status);
        if(showDiagnostics)Ui::Text("Changes: %ld | blocked purchases: %ld",normalChanges,blockedSpends);
        ImGui::EndTabItem();}
        if(Ui::BeginTabItem("World",nullptr,requestedTab==2?ImGuiTabItemFlags_SetSelected:0)){currentTab=2;VehicleMod::NitroLua::World::draw();ImGui::EndTabItem();}
        if(Ui::BeginTabItem("Settings",nullptr,requestedTab==3?ImGuiTabItemFlags_SetSelected:0)){currentTab=3;drawSettings();ImGui::EndTabItem();}
        ImGui::EndTabBar();requestedTab=-1;}
        ImGui::Separator();
        Ui::TextDisabled("F1: close | PgUp/PgDn: tabs | Tab: navigate");
        if(showDiagnostics)Ui::TextDisabled("UI keys/clicks: %lu/%lu",menuKeyEvents,menuMouseEvents);
        ImGui::End();
    }
    ImGui::EndFrame();ImGui::Render();ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    static DWORD lastDrawLog=0;
    if(visible && GetTickCount()-lastDrawLog>5000) {
        lastDrawLog=GetTickCount();char b[160];
        snprintf(b,sizeof(b),"visible UI render: vertices=%d nav=%d focuslost=%d mouse=%.0f,%.0f",ImGui::GetDrawData()->TotalVtxCount,ImGui::GetIO().NavActive,ImGui::GetIO().AppFocusLost,ImGui::GetIO().MousePos.x,ImGui::GetIO().MousePos.y);logLine(b);
    }
    LeaveCriticalSection(&uiLock);
}
// Draw once, immediately before presenting the final backbuffer. EndScene can
// be called for mirrors, shadow maps and other offscreen passes in this game.
static HRESULT WINAPI present(IDirect3DDevice9 *d,const RECT *src,const RECT *dst,HWND overrideWindow,const RGNDATA *dirty) {
    EnterCriticalSection(&uiLock);
    IDirect3DSurface9 *back=nullptr,*targets[4]={},*depth=nullptr;
    IDirect3DStateBlock9 *states=nullptr;D3DVIEWPORT9 viewport{};D3DCAPS9 caps{};
    UINT count=0;bool captured=false,rendered=false;
    if(SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)) &&
       SUCCEEDED(d->GetDeviceCaps(&caps)) && SUCCEEDED(d->GetViewport(&viewport)) &&
       SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&states)) && SUCCEEDED(states->Capture())) {
        count=caps.NumSimultaneousRTs<4?caps.NumSimultaneousRTs:4;
        captured=count>0;
        for(UINT i=0;i<count;i++) {HRESULT hr=d->GetRenderTarget(i,&targets[i]);if(i==0 && FAILED(hr))captured=false;}
        d->GetDepthStencilSurface(&depth);
        if(captured) {
            for(UINT i=1;i<count;i++)d->SetRenderTarget(i,nullptr);
            d->SetDepthStencilSurface(nullptr);
            if(SUCCEEDED(d->SetRenderTarget(0,back))) {
                D3DSURFACE_DESC desc{};back->GetDesc(&desc);
                D3DVIEWPORT9 finalViewport{0,0,desc.Width,desc.Height,0.0f,1.0f};
                d->SetViewport(&finalViewport);
                if(SUCCEEDED(d->BeginScene())) {
                    // ImGui's DX9 backend does not normalize these inherited
                    // post-processing states. A zero/alpha-only color mask can
                    // produce valid UI vertices with no visible RGB output.
                    if(visible) {
                        static DWORD lastStateLog=0;
                        if(GetTickCount()-lastStateLog>5000) {
                            lastStateLog=GetTickCount();DWORD mask=0,planes=0,sample=0;
                            d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask);
                            d->GetRenderState(D3DRS_CLIPPLANEENABLE,&planes);
                            d->GetRenderState(D3DRS_MULTISAMPLEMASK,&sample);
                            char b[160];snprintf(b,sizeof(b),"overlay inherited state: color=%lx planes=%lx sample=%lx",mask,planes,sample);logLine(b);
                        }
                    }
                    d->SetRenderState(D3DRS_COLORWRITEENABLE,0x0f);
                    d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
                    d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);
                    d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
                    d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
                    drawMenu(d);d->EndScene();rendered=true;
                }
            }
            // Render targets/depth are not part of D3DSBT_ALL.
            d->SetRenderTarget(0,targets[0]);
            for(UINT i=1;i<count;i++)d->SetRenderTarget(i,targets[i]);
            d->SetDepthStencilSurface(depth);states->Apply();d->SetViewport(&viewport);
        }
    }
    for(auto target:targets)if(target)target->Release();
    if(depth)depth->Release();if(states)states->Release();if(back)back->Release();
    static bool reported=false;static DWORD lastFailure=0;
    if(!reported && rendered){reported=true;logLine("v7.14: final-backbuffer Present overlay rendered");}
    if(visible && !rendered && GetTickCount()-lastFailure>5000) {
        lastFailure=GetTickCount();logLine("WARNING: Present overlay draw could not acquire state/target/scene");
    }
    LeaveCriticalSection(&uiLock);
    return ((HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*))deviceOriginal[17])(d,src,dst,overrideWindow,dirty);
}
static HRESULT WINAPI reset(IDirect3DDevice9 *d,D3DPRESENT_PARAMETERS *p) {
    EnterCriticalSection(&uiLock);
    if(uiReady)ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr=((HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*))deviceOriginal[16])(d,p);
    if(uiReady && SUCCEEDED(hr))ImGui_ImplDX9_CreateDeviceObjects();
    LeaveCriticalSection(&uiLock);return hr;
}
static void **d3dOriginal;static void *d3dTable[17];
static HRESULT WINAPI createDevice(IDirect3D9 *self,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,D3DPRESENT_PARAMETERS *pp,IDirect3DDevice9 **out) {
    auto fn=(HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**))d3dOriginal[16];
    HRESULT hr=fn(self,adapter,type,window,flags,pp,out);
    if(SUCCEEDED(hr) && out && *out) {
        deviceOriginal=*(void***)*out;memcpy(deviceTable,deviceOriginal,sizeof(deviceTable));deviceTable[17]=(void*)present;deviceTable[16]=(void*)reset;*(void***)*out=deviceTable;logLine("D3D9 device intercepted");
    }
    return hr;
}
static decltype(&Direct3DCreate9) realCreate9;
static IDirect3D9 *WINAPI create9(UINT sdk) {
    static bool attempted=false;if(!attempted){attempted=true;bindMoney();VehicleMod::bind();PaintMod::bind();SpeedMod::bind();PowertrainMod::bind();ControllerMod::bind();logLine(status);}
    IDirect3D9 *d=realCreate9(sdk);
    if(d){d3dOriginal=*(void***)d;memcpy(d3dTable,d3dOriginal,sizeof(d3dTable));d3dTable[16]=(void*)createDevice;*(void***)d=d3dTable;logLine("Direct3DCreate9 intercepted");}return d;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&uiLock);
        applyMessage=RegisterWindowMessageA("DSF_WillpowerMod_Apply_1_04_1114");
        repairMessage=RegisterWindowMessageA("DSF_VehicleMod_Repair_1_04_1114");
        paintMessage=RegisterWindowMessageA("DSF_VehicleMod_Paint_1_04_1114");
        image=(BYTE*)GetModuleHandle(nullptr);
        auto nt=(IMAGE_NT_HEADERS*)(image+((IMAGE_DOS_HEADER*)image)->e_lfanew);imageSize=nt->OptionalHeader.SizeOfImage;
        // The proxy is loaded as a normal game import. No remote injection.
        logHandle=CreateFileA("mod\\logs\\mod-runtime.log",FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
        logLine("Offline mod v7.14 attached; selected 100/200/300/400/500 MPH targets; supported mission countdown / native weather controls; Arabic-default/English UI; player passenger removal default ON, other gameplay switches OFF; gameplay acceptance pending");
        if(inside(image+0x46383b,2) && image[0x46383b]==0x75 && image[0x46383c]==0x16) {
            DWORD oldProtect=0;
            if(VirtualProtect(image+0x46383b,2,PAGE_EXECUTE_READWRITE,&oldProtect)) {
                image[0x46383b]=0xeb;
                VirtualProtect(image+0x46383b,2,oldProtect,&oldProtect);
                logLine("Offline login check bypassed (RVA 0x46383b forced jmp)");
            }
        }
        replaceImport("USER32.dll","GetAsyncKeyState",(void*)quietAsync);
        replaceImport("USER32.dll","GetKeyState",(void*)quietKey);
        replaceImport("USER32.dll","GetKeyboardState",(void*)quietKeyboard);
        replaceImport("USER32.dll","ClipCursor",(void*)quietClip);
        if(!replaceImport("XINPUT1_3.dll","XInputGetState",(void*)quietXInput,2))logLine("WARNING: game XInput state import not found; overlay controller suppression unavailable");
        realCreate9=(decltype(realCreate9))replaceImport("d3d9.dll","Direct3DCreate9",(void*)create9);
        if(!realCreate9)logLine("ERROR: Direct3DCreate9 import interception failed");
    }
    return TRUE;
}
