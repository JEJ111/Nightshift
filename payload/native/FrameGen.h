#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <string>
#include "sl.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
#include "sl_pcl.h"
#define FG_EXPORT extern "C" __declspec(dllexport)
using Microsoft::WRL::ComPtr;
struct FGMatrix { float values[16]; };
// Naturally aligned x64 ABI. The camera matrices use SL's row-vector layout.
struct FGFrame {
    uint32_t size,version,frameId,flags;
    void* hudless; void* depth; void* motion;
    uint32_t inputWidth,inputHeight,outputWidth,outputHeight;
    float jitterX,jitterY,motionScaleX,motionScaleY,depthNear,depthFar,fov,aspect;
    float position[3],up[3],right[3],forward[3];
    FGMatrix viewToClip,clipToView,clipToPrevious,previousToClip;
};
static_assert(sizeof(FGFrame)==392,"Managed/native frame layout must agree");
struct FGPacket {
    FGFrame frame;
    ComPtr<ID3D11Texture2D> hudless,depth,motion;
};
struct FGStatus {
    uint32_t size,version,ready,requested,active,apiResult,fgStatus,reserved;
    uint64_t rendered,presented,generated,submitted;
    double renderedFps,presentedFps;
};
struct FGSession {
    HMODULE module=nullptr;
    bool initialized=false;
    std::atomic<bool> ready{false};
    PFun_slInit* init=nullptr; PFun_slShutdown* shutdown=nullptr;
    PFun_slSetD3DDevice* setDevice=nullptr; PFun_slUpgradeInterface* upgrade=nullptr;
    PFun_slGetNativeInterface* getNative=nullptr;
    PFun_slGetFeatureFunction* getFunction=nullptr;
    PFun_slGetNewFrameToken* newFrame=nullptr; PFun_slSetConstants* constants=nullptr;
    PFun_slSetTagForFrame* tags=nullptr; PFun_slIsFeatureSupported* support=nullptr;
    PFun_slDLSSGSetOptions* setOptions=nullptr; PFun_slDLSSGGetState* getState=nullptr;
    PFun_slReflexSetOptions* reflexOptions=nullptr; PFun_slReflexSleep* sleep=nullptr;
    PFun_slPCLSetMarker* marker=nullptr;
    std::mutex tokenLock;
    struct Token { uint32_t id=0; sl::FrameToken* value=nullptr; } tokens[16];
    bool Initialize();
    bool BindDevice(ID3D12Device* device);
    sl::FrameToken* FindFrame(uint32_t id);
    bool Begin(uint32_t id);
    void End(uint32_t id);
};
FGSession& FG_SL();
void FG_Log(const std::string& message);
void FG_Message(const std::string& message);
void FG_Failure(const std::string& operation,uint32_t result);
void* FG_SystemExport(const char* name);
void FG_InstallFactoryHooks(IDXGIFactory* factory);
std::wstring FG_Root();
extern std::atomic<uint32_t> fgRequested;
extern std::atomic<uint32_t> fgMaximumMultiplier;
extern std::atomic<uint64_t> fgSubmitted;
extern std::atomic<uint32_t> fgRenderFrame;
extern std::mutex fgStatusLock;
extern FGStatus fgStatus;
extern std::mutex fgPendingLock;
extern FGPacket* fgPending;
void FG_ResetPending();
FG_EXPORT void* __cdecl NSFG_GetSystemExport(uint32_t index);
FG_EXPORT void __cdecl NSFG_BeginFrame(uint32_t id);
FG_EXPORT void __cdecl NSFG_EndSimulation(uint32_t id);
FG_EXPORT void __cdecl NSFG_Configure(uint32_t multiplier);
FG_EXPORT uint32_t __cdecl NSFG_GetMaximumMultiplier();
FG_EXPORT void* __cdecl NSFG_GetRenderEvent();
FG_EXPORT void* __cdecl NSFG_CreatePacket(const FGFrame* frame);
FG_EXPORT void __cdecl NSFG_DiscardPacket(void* handle);
FG_EXPORT void __cdecl NSFG_GetStatus(FGStatus* status,char* message,uint32_t capacity);
FG_EXPORT void __cdecl NSFG_SetProbeMode();
