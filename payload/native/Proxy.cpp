#include "Bridge.h"
#include "MinHook.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstring>
#include <bcrypt.h>

static std::once_flag systemOnce;
static HMODULE systemDxgi=nullptr;
static std::atomic<bool> probeMode{false};
static std::mutex bootstrapLock,hooksLock;
static bool bootstrapAttempted=false;
static thread_local bool insideBootstrap=false,insideBridge=false;
static std::vector<void*> hookedAddresses;
using CreateForHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
static CreateForHwnd originalForHwnd=nullptr;

void* FG_SystemExport(const char* name) {
    std::call_once(systemOnce,[] {
        wchar_t path[32768]={}; GetSystemDirectoryW(path,32768);
        auto absolute=std::filesystem::path(path)/L"dxgi.dll";
        systemDxgi=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    });
    return systemDxgi?reinterpret_cast<void*>(GetProcAddress(systemDxgi,name)):nullptr;
}
FG_EXPORT void* __cdecl NSFG_GetSystemExport(uint32_t index) {
    static const char* names[]={"ApplyCompatResolutionQuirking","CompatString","CompatValue","DXGIDumpJournal",
        "PIXBeginCapture","PIXEndCapture","PIXGetCaptureState","SetAppCompatStringPointer","UpdateHMDEmulationStatus",
        "CreateDXGIFactory","CreateDXGIFactory1","CreateDXGIFactory2","DXGID3D10CreateDevice","DXGID3D10CreateLayeredDevice",
        "DXGID3D10GetLayeredDeviceSize","DXGID3D10RegisterLayers","DXGIDeclareAdapterRemovalSupport","DXGIDisableVBlankVirtualization",
        "DXGIGetDebugInterface1","DXGIReportAdapterConfiguration"};
    return index<20?FG_SystemExport(names[index]):nullptr;
}
FG_EXPORT void __cdecl NSFG_SetProbeMode() { probeMode=true; }
static bool IsTarget() {
    if(probeMode) return true;
    wchar_t path[32768]={}; GetModuleFileNameW(nullptr,path,32768);
    return _wcsicmp(std::filesystem::path(path).filename().c_str(),L"Nivalis Nights.exe")==0;
}
static bool SupportedBinary() {
    if(probeMode) return true;
    auto path=std::filesystem::path(FG_Root())/L"GameAssembly.dll";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::vector<unsigned char> buffer(1024*1024); DWORD bytes=0;
    while(ok) {
        if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&bytes,nullptr)) { ok=false; break; }
        if(!bytes) break;
        ok=BCryptHashData(hash,buffer.data(),bytes,0)>=0;
    }
    unsigned char digest[32]={}; if(ok) ok=BCryptFinishHash(hash,digest,32,0)>=0;
    if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); CloseHandle(file);
    char value[65]={}; for(int i=0;i<32;i++) std::sprintf(value+i*2,"%02X",digest[i]);
    return ok && (std::strcmp(value,"9A0E32C2D09A5025F867D29BF39B9BEDD0715B513456617FBFD82C581E1A376D")==0 || std::strcmp(value,"D7D7FEF8B76699AE6A9F70B111C239B013BA00261A02A85BDB394BDE38C46504")==0 || std::strcmp(value,"0DA6AAC5209F504DA743ABD7926F6F528010E2CA7B884B4A20F5198F42F1A26D")==0);
}
static bool Bootstrap() {
    if(insideBootstrap || !IsTarget()) return false;
    if(FG_SL().initialized) return true;
    std::lock_guard<std::mutex> lock(bootstrapLock);
    if(bootstrapAttempted) return FG_SL().initialized;
    bootstrapAttempted=true; insideBootstrap=true;
    bool ok=false;
    try {
        if(SupportedBinary()) ok=FG_SL().Initialize();
        else FG_Message("Frame-generation bootstrap skipped: game binary differs from the supported build; original DXGI retained");
    }
    catch(const std::exception& error) { FG_Message(std::string("Frame-generation bootstrap failed: ")+error.what()); }
    insideBootstrap=false; return ok;
}
static bool IsGameSwapchain(IUnknown* owner,HWND window,UINT width,UINT height) {
    if(insideBridge || insideBootstrap || !FG_SL().initialized || !owner || !window) return false;
    RECT client={}; GetClientRect(window,&client);
    if(!width) width=UINT(std::max(0L,client.right-client.left));
    if(!height) height=UINT(std::max(0L,client.bottom-client.top));
    if(width<320 || height<200) return false;
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId()) return false;
    ComPtr<ID3D11Device> device;
    return SUCCEEDED(owner->QueryInterface(IID_PPV_ARGS(&device)));
}
static HRESULT STDMETHODCALLTYPE HookCreate(IDXGIFactory* factory,IUnknown* owner,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** output) {
    if(desc && output && IsGameSwapchain(owner,desc->OutputWindow,desc->BufferDesc.Width,desc->BufferDesc.Height)) {
        insideBridge=true;
        HRESULT result=FGSwapChain::Create(factory,owner,*desc,output);
        insideBridge=false;
        if(SUCCEEDED(result)) return result;
    }
    return fgOriginalCreateSwapChain(factory,owner,desc,output);
}
static HRESULT STDMETHODCALLTYPE HookCreateForHwnd(IDXGIFactory2* factory,IUnknown* owner,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* output,IDXGISwapChain1** swapchain) {
    if(desc && swapchain && fgOriginalCreateSwapChain && IsGameSwapchain(owner,window,desc->Width,desc->Height)) {
        DXGI_SWAP_CHAIN_DESC legacy={}; legacy.OutputWindow=window; legacy.Windowed=fullscreen?fullscreen->Windowed:TRUE;
        legacy.BufferDesc.Width=desc->Width; legacy.BufferDesc.Height=desc->Height; legacy.BufferDesc.Format=desc->Format;
        if(fullscreen) { legacy.BufferDesc.RefreshRate=fullscreen->RefreshRate; legacy.BufferDesc.ScanlineOrdering=fullscreen->ScanlineOrdering; legacy.BufferDesc.Scaling=fullscreen->Scaling; }
        legacy.BufferUsage=desc->BufferUsage; legacy.BufferCount=desc->BufferCount; legacy.SampleDesc=desc->SampleDesc;
        legacy.SwapEffect=desc->SwapEffect; legacy.Flags=desc->Flags;
        ComPtr<IDXGISwapChain> candidate;
        insideBridge=true; HRESULT result=FGSwapChain::Create(factory,owner,legacy,&candidate); insideBridge=false;
        if(SUCCEEDED(result)) return candidate->QueryInterface(IID_PPV_ARGS(swapchain));
    }
    return originalForHwnd(factory,owner,window,desc,fullscreen,output,swapchain);
}
void FG_InstallFactoryHooks(IDXGIFactory* factory) {
    if(!factory || insideBootstrap || !FG_SL().initialized) return;
    std::lock_guard<std::mutex> lock(hooksLock);
    static bool minHookReady=false;
    if(!minHookReady) {
        auto status=MH_Initialize();
        if(status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) { FG_Failure("DXGI hook initialization",status); return; }
        minHookReady=true;
    }
    auto Install=[](void* address,void* hook,void** original) {
        if(std::find(hookedAddresses.begin(),hookedAddresses.end(),address)!=hookedAddresses.end()) return;
        // DXGI factories in this process share the same implementation. Avoid
        // replacing a trampoline with one from an unrelated implementation.
        if(*original) return;
        auto status=MH_CreateHook(address,hook,original);
        if(status==MH_OK) status=MH_EnableHook(address);
        if(status==MH_OK) hookedAddresses.push_back(address);
        else FG_Failure("DXGI factory hook",status);
    };
    void** vtable=*reinterpret_cast<void***>(factory);
    Install(vtable[10],reinterpret_cast<void*>(&HookCreate),reinterpret_cast<void**>(&fgOriginalCreateSwapChain));
    ComPtr<IDXGIFactory2> factory2;
    if(SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2)))) {
        void** newer=*reinterpret_cast<void***>(factory2.Get());
        Install(newer[15],reinterpret_cast<void*>(&HookCreateForHwnd),reinterpret_cast<void**>(&originalForHwnd));
    }
}
static void FinishFactory(HRESULT result,void** output,bool enable) {
    if(!enable || FAILED(result) || !output || !*output) return;
    ComPtr<IDXGIFactory> factory;
    if(SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&factory)))) FG_InstallFactoryHooks(factory.Get());
}
extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID id,void** output) {
    bool enable=Bootstrap();
    auto original=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(FG_SystemExport("CreateDXGIFactory"));
    if(!original) return E_NOTIMPL;
    auto result=original(id,output); FinishFactory(result,output,enable); return result;
}
extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID id,void** output) {
    bool enable=Bootstrap();
    auto original=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(FG_SystemExport("CreateDXGIFactory1"));
    if(!original) return E_NOTIMPL;
    auto result=original(id,output); FinishFactory(result,output,enable); return result;
}
extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT flags,REFIID id,void** output) {
    bool enable=Bootstrap();
    auto original=reinterpret_cast<HRESULT(WINAPI*)(UINT,REFIID,void**)>(FG_SystemExport("CreateDXGIFactory2"));
    if(!original) return E_NOTIMPL;
    auto result=original(flags,id,output); FinishFactory(result,output,enable); return result;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    // All graphics/SDK work happens after leaving the Windows loader lock.
    return TRUE;
}
