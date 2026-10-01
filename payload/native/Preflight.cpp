// Independently authored, isolated hardware/API preflight. No game hooks.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include "sl.h"
#include "sl_helpers.h"
#include "sl_security.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
using Microsoft::WRL::ComPtr;
template<class T> T* Import(HMODULE module, const char* name) {
    auto function = reinterpret_cast<T*>(GetProcAddress(module,name));
    if(!function) { std::fprintf(stderr,"Missing export: %s\n",name); std::exit(2); }
    return function;
}
static void Log(sl::LogType type,const char* message) {
    if(type==sl::LogType::eError || type==sl::LogType::eWarn) std::printf("SL[%u] %s\n",unsigned(type),message);
}
static bool Report(const char* operation,sl::Result result) {
    std::printf("%s: %s (%u)\n",operation,sl::getResultAsStr(result),unsigned(result));
    std::fflush(stdout); return result==sl::Result::eOk;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { std::fprintf(stderr,"Usage: Preflight.exe absolute-sdk-binaries-folder\n"); return 2; }
    auto folder=std::filesystem::absolute(argv[1]);
    auto path=folder/L"sl.interposer.dll";
    if(!sl::security::verifyEmbeddedSignature(path.c_str())) return 3;
    auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) { std::printf("Load failed: %lu\n",GetLastError()); return 4; }
    auto init=Import<PFun_slInit>(module,"slInit");
    auto shutdown=Import<PFun_slShutdown>(module,"slShutdown");
    auto support=Import<PFun_slIsFeatureSupported>(module,"slIsFeatureSupported");
    auto requirements=Import<PFun_slGetFeatureRequirements>(module,"slGetFeatureRequirements");
    auto setDevice=Import<PFun_slSetD3DDevice>(module,"slSetD3DDevice");
    auto getFunction=Import<PFun_slGetFeatureFunction>(module,"slGetFeatureFunction");
    auto pluginFolder=folder.wstring();
    auto logFolder=std::filesystem::absolute(L"logs").wstring();
    std::filesystem::create_directories(logFolder);
    const wchar_t* paths[]={pluginFolder.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL,sl::kFeatureDLSS};
    sl::Preferences preferences;
    preferences.pathsToPlugins=paths; preferences.numPathsToPlugins=1;
    preferences.pathToLogsAndData=logFolder.c_str(); preferences.logMessageCallback=&Log;
    preferences.featuresToLoad=features; preferences.numFeaturesToLoad=4;
    preferences.engine=sl::EngineType::eUnity; preferences.engineVersion="2020.3.44f1";
    preferences.projectId="19f82428-6481-48c5-b47c-e2df3df2dc92";
    preferences.renderAPI=sl::RenderAPI::eD3D12;
    preferences.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    if(!Report("slInit before DirectX",init(preferences,sl::kSDKVersion))) return 5;
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr)) { shutdown(); return 6; }
    ComPtr<IDXGIAdapter1> selected;
    for(UINT index=0;;++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
        if(desc.VendorId!=0x10DE || desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        std::printf("Adapter: %S; VRAM=%llu; LUID=%ld:%lu\n",desc.Description,static_cast<unsigned long long>(desc.DedicatedVideoMemory),desc.AdapterLuid.HighPart,desc.AdapterLuid.LowPart);
        sl::AdapterInfo info;
        info.deviceLUID=reinterpret_cast<uint8_t*>(&desc.AdapterLuid); info.deviceLUIDSizeInBytes=sizeof(LUID);
        bool fg=Report("DLSS-G adapter support",support(sl::kFeatureDLSS_G,info));
        Report("Reflex adapter support",support(sl::kFeatureReflex,info));
        Report("DLSS-SR adapter support",support(sl::kFeatureDLSS,info));
        sl::FeatureRequirements req;
        if(Report("DLSS-G requirements",requirements(sl::kFeatureDLSS_G,req))) {
            std::printf("Requirements: flags=%llu maxViewports=%u\n",static_cast<unsigned long long>(req.flags),req.maxNumViewports);
        }
        if(fg) { selected=adapter; break; }
    }
    if(!selected) { shutdown(); return 7; }
    ComPtr<ID3D12Device> device;
    hr=D3D12CreateDevice(selected.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device));
    std::printf("D3D12CreateDevice: 0x%08X\n",unsigned(hr));
    if(FAILED(hr) || !Report("slSetD3DDevice",setDevice(device.Get()))) { shutdown(); return 8; }
    void* function=nullptr;
    if(!Report("DLSS-G state function",getFunction(sl::kFeatureDLSS_G,"slDLSSGGetState",function))) { shutdown(); return 9; }
    sl::DLSSGState state;
    sl::DLSSGOptions options; options.colorWidth=2560; options.colorHeight=1440;
    Report("DLSS-G state",reinterpret_cast<PFun_slDLSSGGetState*>(function)(sl::ViewportHandle(0),state,&options));
    std::printf("DLSS-G: status=%u numFramesToGenerateMax=%u minDimension=%u vsync=%u dynamicMFG=%u\n",unsigned(state.status),state.numFramesToGenerateMax,state.minWidthOrHeight,unsigned(state.bIsVsyncSupportAvailable),unsigned(state.bIsDynamicMFGSupported));
    function=nullptr;
    if(Report("Reflex state function",getFunction(sl::kFeatureReflex,"slReflexGetState",function))) {
        sl::ReflexState reflex;
        Report("Reflex state",reinterpret_cast<PFun_slReflexGetState*>(function)(reflex));
        std::printf("Reflex: lowLatencyAvailable=%u latencyReportAvailable=%u\n",reflex.lowLatencyAvailable,reflex.latencyReportAvailable);
    }
    Report("slShutdown",shutdown());
    std::puts("Preflight complete. This does not establish game frame generation.");
    return 0;
}
