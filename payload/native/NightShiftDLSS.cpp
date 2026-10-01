// Independently authored bridge. SDK headers/libraries are obtained separately.
// This software contains source code provided by NVIDIA Corporation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers_d3d.h"
#include "NightShiftDLSS.h"

using Microsoft::WRL::ComPtr;
static std::mutex stateLock;
static std::mutex statusLock;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext1> context;
static ComPtr<ID3DDeviceContextState> isolatedState;
static NVSDK_NGX_Parameter* parameters = nullptr;
static NVSDK_NGX_Handle* feature = nullptr;
static bool initialized = false;
static bool ready = false;
static uint32_t width=0,height=0,outWidth=0,outHeight=0;
static int quality=-1,featureFlags=0;
static std::atomic<uint64_t> successfulFrames{0};
static std::atomic<uint32_t> lastResult{0};
static std::string status="OFF; no DLSS frames evaluated";
static void SetStatus(const char* message)
{
    std::lock_guard<std::mutex> guard(statusLock);
    status=message;
}

// Own the context and prior state through shutdown as well as evaluation.
// NGX initialization and release are allowed to touch device state too.
struct ContextIsolation
{
    ComPtr<ID3D11DeviceContext1> savedContext;
    ComPtr<ID3DDeviceContextState> previous;
    bool swapped=false;
    ContextIsolation(ID3D11DeviceContext1* current,ID3DDeviceContextState* isolated)
        : savedContext(current)
    {
        if(current && isolated)
        { current->SwapDeviceContextState(isolated,&previous); swapped=true; }
    }
    ~ContextIsolation()
    { if(swapped) savedContext->SwapDeviceContextState(previous.Get(),nullptr); }
};

struct Packet
{
    NSFrame frame;
    ComPtr<ID3D11Texture2D> color,depth,motion,output;
};
static void SetResult(const char* operation,NVSDK_NGX_Result result)
{
    auto value=static_cast<uint32_t>(result); lastResult=value;
    char s[180]; std::snprintf(s,sizeof(s),"%s: 0x%08X",operation,value);
    SetStatus(s);
}
static void ReleaseFeature()
{
    if(feature) { NVSDK_NGX_D3D11_ReleaseFeature(feature); feature=nullptr; }
    width=height=outWidth=outHeight=0; quality=-1;
}
static void Shutdown()
{
    ContextIsolation isolation(context.Get(),isolatedState.Get());
    ReleaseFeature();
    if(parameters) { NVSDK_NGX_D3D11_DestroyParameters(parameters); parameters=nullptr; }
    if(initialized) { NVSDK_NGX_D3D11_Shutdown1(device.Get()); initialized=false; }
    isolatedState.Reset(); context.Reset(); device.Reset();
    ready=false;
    lastResult=0;
}
static bool Initialize(ID3D11Device* inputDevice)
{
    if(device.Get()==inputDevice && ready) return true;
    Shutdown(); device=inputDevice;
    ComPtr<ID3D11Device1> device1;
    ComPtr<ID3D11DeviceContext> baseContext;
    device->GetImmediateContext(&baseContext);
    if(FAILED(device.As(&device1)) || FAILED(baseContext.As(&context)))
    { lastResult=0xBAD00006; SetStatus("Unavailable: D3D11.1 context state isolation required"); return false; }
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL chosen;
    if(FAILED(device1->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&isolatedState)))
    { lastResult=0xBAD00007; SetStatus("Unavailable: could not isolate graphics state"); return false; }

    ContextIsolation isolation(context.Get(),isolatedState.Get());

    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&NS_GetRenderEvent),&module);
    wchar_t file[MAX_PATH]; GetModuleFileNameW(module,file,MAX_PATH);
    std::wstring folder=file; folder=folder.substr(0,folder.find_last_of(L"\\/"));
    std::wstring cache=folder+L"\\ngx-cache";
    CreateDirectoryW(cache.c_str(),nullptr);
    const wchar_t* searchPaths[]={folder.c_str()};
    NVSDK_NGX_FeatureCommonInfo info={};
    info.PathListInfo.Path=searchPaths; info.PathListInfo.Length=1;
    // Independent project GUID; never impersonates the game's NVIDIA application ID.
    auto result=NVSDK_NGX_D3D11_Init_with_ProjectID("19f82428-6481-48c5-b47c-e2df3df2dc92",
        NVSDK_NGX_ENGINE_TYPE_UNITY,"2020.3.44f1",cache.c_str(),device.Get(),&info);
    SetResult("NGX init",result);
    if(NVSDK_NGX_FAILED(result)) return false;
    initialized=true;
    result=NVSDK_NGX_D3D11_GetCapabilityParameters(&parameters);
    SetResult("NGX capabilities",result);
    if(NVSDK_NGX_FAILED(result)) return false;
    int available=0;
    result=parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available);
    if(NVSDK_NGX_FAILED(result) || !available) { lastResult=0xBAD00003; SetStatus("Unavailable: DLSS Super Resolution not supported by this device/driver"); return false; }
    ready=true;
    return true;
}
static bool TextureMatches(ID3D11Texture2D* texture,ID3D11Device* expected,uint32_t w,uint32_t h,bool uav)
{
    if(!texture) return false;
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
    D3D11_TEXTURE2D_DESC d; texture->GetDesc(&d);
    return owner.Get()==expected && d.Width==w && d.Height==h && d.SampleDesc.Count==1 &&
        (!uav || (d.BindFlags&D3D11_BIND_UNORDERED_ACCESS));
}
static void Evaluate(Packet& packet)
{
    const auto& f=packet.frame;
    ComPtr<ID3D11Device> owner; packet.color->GetDevice(&owner);
    if(!TextureMatches(packet.color.Get(),owner.Get(),f.inputWidth,f.inputHeight,false) ||
       !TextureMatches(packet.depth.Get(),owner.Get(),f.inputWidth,f.inputHeight,false) ||
       !TextureMatches(packet.motion.Get(),owner.Get(),f.inputWidth,f.inputHeight,false) ||
       !TextureMatches(packet.output.Get(),owner.Get(),f.outputWidth,f.outputHeight,true))
    { lastResult=0xBAD00005; SetStatus("Rejected: mismatched dimensions/device, multisampling or missing output UAV"); return; }
    // NGX calls execute solely on the engine render thread (or standalone probe).
    if(!Initialize(owner.Get())) return;
    ContextIsolation isolation(context.Get(),isolatedState.Get());

    int flags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if(f.reversedDepth) flags|=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if(f.hdr) flags|=NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if(!feature || width!=f.inputWidth || height!=f.inputHeight || outWidth!=f.outputWidth ||
       outHeight!=f.outputHeight || quality!=f.quality || featureFlags!=flags)
    {
        ReleaseFeature();
        NVSDK_NGX_DLSS_Create_Params create={};
        create.Feature.InWidth=f.inputWidth; create.Feature.InHeight=f.inputHeight;
        create.Feature.InTargetWidth=f.outputWidth; create.Feature.InTargetHeight=f.outputHeight;
        create.Feature.InPerfQualityValue=static_cast<NVSDK_NGX_PerfQuality_Value>(f.quality);
        create.InFeatureCreateFlags=flags;
        auto result=NGX_D3D11_CREATE_DLSS_EXT(context.Get(),&feature,parameters,&create);
        SetResult("DLSS create",result);
        if(NVSDK_NGX_FAILED(result)) return;
        width=f.inputWidth; height=f.inputHeight; outWidth=f.outputWidth; outHeight=f.outputHeight;
        quality=f.quality; featureFlags=flags;
    }
    NVSDK_NGX_D3D11_DLSS_Eval_Params eval={};
    eval.Feature.pInColor=packet.color.Get(); eval.Feature.pInOutput=packet.output.Get();
    eval.pInDepth=packet.depth.Get(); eval.pInMotionVectors=packet.motion.Get();
    eval.InJitterOffsetX=f.jitterX; eval.InJitterOffsetY=f.jitterY;
    eval.InMVScaleX=f.motionScaleX; eval.InMVScaleY=f.motionScaleY;
    eval.InReset=f.reset; eval.InRenderSubrectDimensions={f.inputWidth,f.inputHeight};
    eval.InPreExposure=1.0f; eval.InExposureScale=1.0f;
    eval.InFrameTimeDeltaInMsec=f.deltaMs;
    auto result=NGX_D3D11_EVALUATE_DLSS_EXT(context.Get(),feature,parameters,&eval);
    SetResult("DLSS evaluate",result);
    if(NVSDK_NGX_SUCCEED(result)) { successfulFrames++; SetStatus("ACTIVE: genuine DLSS Super Resolution / DLAA evaluation succeeded"); }
}
static void __stdcall RenderEvent(int eventId,void* data)
{
    std::lock_guard<std::mutex> guard(stateLock);
    if(eventId==2) { Shutdown(); SetStatus("OFF; graphics resources released"); return; }
    Packet* packet=static_cast<Packet*>(data);
    if(!packet) return;
    // Native failure propagates as status. No game quit or update suppression.
    try { Evaluate(*packet); }
    catch(...) { lastResult=0xBAD00001; SetStatus("Native bridge exception; renderer must fall back"); }
    delete packet;
}
NS_EXPORT void* __cdecl NS_CreatePacket(const NSFrame* frame)
{
    if(!frame || frame->size!=sizeof(NSFrame) || frame->version!=1 || !frame->color || !frame->depth || !frame->motion || !frame->output) return nullptr;
    auto p=new Packet;
    p->frame=*frame;
    // Own COM references until the queued event executes, independent of Unity wrapper lifetimes.
    p->color=static_cast<ID3D11Texture2D*>(frame->color); p->depth=static_cast<ID3D11Texture2D*>(frame->depth);
    p->motion=static_cast<ID3D11Texture2D*>(frame->motion); p->output=static_cast<ID3D11Texture2D*>(frame->output);
    return p;
}
NS_EXPORT void __cdecl NS_ReleasePacket(void* packet) { delete static_cast<Packet*>(packet); }
NS_EXPORT void* __cdecl NS_GetRenderEvent() { return reinterpret_cast<void*>(&RenderEvent); }
NS_EXPORT void __cdecl NS_GetStatus(char* buffer,uint32_t capacity,uint64_t* frames,uint32_t* result)
{
    // A HUD/status read must not wait for NGX to initialize or evaluate on the
    // render thread, which would serialize Unity's main/render frame pipeline.
    std::lock_guard<std::mutex> guard(statusLock);
    if(buffer && capacity) { strncpy_s(buffer,capacity,status.c_str(),_TRUNCATE); }
    if(frames) *frames=successfulFrames.load();
    if(result) *result=lastResult.load();
}
NS_EXPORT void __cdecl NS_EvaluateNow(const NSFrame* frame)
{
    void* packet=NS_CreatePacket(frame); RenderEvent(1,packet);
}
NS_EXPORT void __cdecl NS_ShutdownNow()
{
    RenderEvent(2,nullptr);
}
static_assert(sizeof(NSFrame)==96,"x64 managed/native layout must match");
