// Independent D3D11 -> D3D12 sharing and actual Streamline presentation probe.
// This process has no game hooks and terminates only its own test window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include "sl.h"
#include "sl_helpers.h"
#include "sl_security.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
#include "sl_pcl.h"
using Microsoft::WRL::ComPtr;
static void Check(HRESULT result,const char* operation) {
    if(FAILED(result)) { char text[160]; std::snprintf(text,sizeof(text),"%s: HRESULT 0x%08X",operation,unsigned(result)); throw std::runtime_error(text); }
}
static void Check(sl::Result result,const char* operation) {
    if(result!=sl::Result::eOk) throw std::runtime_error(std::string(operation)+": "+sl::getResultAsStr(result));
}
template<class T> T* Import(HMODULE module,const char* name) {
    auto function=reinterpret_cast<T*>(GetProcAddress(module,name));
    if(!function) throw std::runtime_error(std::string("Missing export: ")+name);
    return function;
}
template<class T> T* Feature(PFun_slGetFeatureFunction* get,sl::Feature feature,const char* name) {
    void* function=nullptr; Check(get(feature,name,function),name); return reinterpret_cast<T*>(function);
}
static void Log(sl::LogType type,const char* message) {
    if(type==sl::LogType::eWarn || type==sl::LogType::eError) { std::printf("SL[%u] %s\n",unsigned(type),message); std::fflush(stdout); }
}
static sl::float4x4 Identity() {
    sl::float4x4 matrix; for(int r=0;r<4;++r) for(int c=0;c<4;++c) reinterpret_cast<float*>(&matrix.row[r])[c]=(r==c)?1.0f:0.0f; return matrix;
}
static sl::Constants Constants(bool reset) {
    sl::Constants c;
    auto matrix=Identity(), inverse=Identity();
    float depthNear=.1f,depthFar=100.f,aspect=1280.f/720.f,s=1.f/std::tan(.5f);
    float a=depthFar/(depthFar-depthNear),b=-depthNear*a;
    matrix[0].x=s/aspect; matrix[1].y=s; matrix[2].z=a; matrix[2].w=1; matrix[3].z=b; matrix[3].w=0;
    inverse[0].x=aspect/s; inverse[1].y=1/s; inverse[2].z=0; inverse[2].w=1/b; inverse[3].z=1; inverse[3].w=-a/b;
    c.cameraViewToClip=matrix; c.clipToCameraView=inverse;
    c.clipToPrevClip=c.prevClipToClip=c.clipToLensClip=Identity();
    c.jitterOffset={0,0}; c.mvecScale={1,1}; c.cameraPinholeOffset={0,0};
    c.cameraPos={0,0,0}; c.cameraUp={0,1,0}; c.cameraRight={1,0,0}; c.cameraFwd={0,0,1};
    c.cameraNear=depthNear; c.cameraFar=depthFar; c.cameraFOV=1; c.cameraAspectRatio=aspect;
    c.depthInverted=sl::eFalse; c.cameraMotionIncluded=sl::eTrue; c.motionVectors3D=sl::eFalse;
    c.reset=reset?sl::eTrue:sl::eFalse; c.motionVectorsInvalidValue=0;
    return c;
}
struct SharedTexture {
    ComPtr<ID3D12Resource> native12;
    ComPtr<ID3D11Texture2D> native11;
    ComPtr<ID3D11RenderTargetView> rtv;
    void Create(ID3D12Device* d12,ID3D11Device1* d11,DXGI_FORMAT format,UINT width,UINT height) {
        D3D12_HEAP_PROPERTIES heap={}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc={}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width; desc.Height=height; desc.DepthOrArraySize=1; desc.MipLevels=1;
        desc.Format=format; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        Check(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&native12)),"shared texture creation");
        HANDLE handle=nullptr; Check(d12->CreateSharedHandle(native12.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"texture NT handle");
        HRESULT result=d11->OpenSharedResource1(handle,IID_PPV_ARGS(&native11)); CloseHandle(handle); Check(result,"D3D11 shared texture open");
        Check(d11->CreateRenderTargetView(native11.Get(),nullptr,&rtv),"shared RTV");
    }
};
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier={}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=resource; barrier.Transition.StateBefore=before; barrier.Transition.StateAfter=after;
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; list->ResourceBarrier(1,&barrier);
}
int wmain(int argc,wchar_t** argv) {
    PFun_slShutdown* shutdown=nullptr;
    HWND window=nullptr;
    try {
        if(argc<2) throw std::runtime_error("Usage: InteropProbe.exe sdk-folder [visible]");
        auto folder=std::filesystem::absolute(argv[1]);
        auto interposer=folder/L"sl.interposer.dll";
        if(!sl::security::verifyEmbeddedSignature(interposer.c_str())) return 3;
        auto module=LoadLibraryExW(interposer.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module) throw std::runtime_error("Could not load verified Streamline library");
        auto init=Import<PFun_slInit>(module,"slInit"); shutdown=Import<PFun_slShutdown>(module,"slShutdown");
        auto setDevice=Import<PFun_slSetD3DDevice>(module,"slSetD3DDevice");
        auto upgrade=Import<PFun_slUpgradeInterface>(module,"slUpgradeInterface");
        auto functions=Import<PFun_slGetFeatureFunction>(module,"slGetFeatureFunction");
        auto newFrame=Import<PFun_slGetNewFrameToken>(module,"slGetNewFrameToken");
        auto setConstants=Import<PFun_slSetConstants>(module,"slSetConstants");
        auto tag=Import<PFun_slSetTagForFrame>(module,"slSetTagForFrame");
        const auto pluginFolder=folder.wstring(), logs=std::filesystem::absolute(L"interop-logs").wstring();
        std::filesystem::create_directories(logs);
        const wchar_t* paths[]={pluginFolder.c_str()};
        const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
        sl::Preferences pref; pref.pathsToPlugins=paths; pref.numPathsToPlugins=1; pref.pathToLogsAndData=logs.c_str();
        pref.logMessageCallback=&Log; pref.featuresToLoad=features; pref.numFeaturesToLoad=3;
        pref.engine=sl::EngineType::eUnity; pref.engineVersion="2020.3.44f1"; pref.projectId="19f82428-6481-48c5-b47c-e2df3df2dc92";
        pref.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        Check(init(pref,sl::kSDKVersion),"slInit before graphics APIs");
        ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i) {
            Check(factory->EnumAdapters1(i,&adapter),"NVIDIA adapter enumeration");
            DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc); if(desc.VendorId==0x10DE) break; adapter.Reset();
        }
        ComPtr<ID3D12Device> d12; Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d12)),"native D3D12 device");
        Check(setDevice(d12.Get()),"slSetD3DDevice");
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC qdesc={}; qdesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(d12->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&queue)),"D3D12 queue");
        ComPtr<ID3D11Device> base11; ComPtr<ID3D11DeviceContext> baseContext; D3D_FEATURE_LEVEL level;
        Check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&base11,&level,&baseContext),"original D3D11 device");
        ComPtr<ID3D11Device1> d11; ComPtr<ID3D11Device5> d11v5; ComPtr<ID3D11DeviceContext4> context;
        Check(base11.As(&d11),"D3D11.1 sharing"); Check(base11.As(&d11v5),"D3D11 fences"); Check(baseContext.As(&context),"D3D11.4 context");
        std::puts("Native D3D11 renderer and independent D3D12 presentation device created on the same GPU.");
        ComPtr<ID3D12Fence> from11,done12;
        ComPtr<ID3D11Fence> from11Native,done12Native;
        Check(d11v5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&from11Native)),"D3D11 signal fence");
        HANDLE handle; Check(from11Native->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&handle),"D3D11 fence handle");
        HRESULT hr=d12->OpenSharedHandle(handle,IID_PPV_ARGS(&from11)); CloseHandle(handle); Check(hr,"D3D12 fence open");
        Check(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&done12)),"D3D12 return fence");
        Check(d12->CreateSharedHandle(done12.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"D3D12 fence handle");
        hr=d11v5->OpenSharedFence(handle,IID_PPV_ARGS(&done12Native)); CloseHandle(handle); Check(hr,"D3D11 return fence open");
        SharedTexture color,depth,motion,hudless;
        color.Create(d12.Get(),d11.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,1280,720);
        hudless.Create(d12.Get(),d11.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,1280,720);
        depth.Create(d12.Get(),d11.Get(),DXGI_FORMAT_R32_FLOAT,1280,720);
        motion.Create(d12.Get(),d11.Get(),DXGI_FORMAT_R16G16_FLOAT,1280,720);
        std::puts("Shared color, HUD-less color, depth, motion and bidirectional GPU fences passed."); std::fflush(stdout);
        WNDCLASSW wc={}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"NightShiftFrameGenProbe";
        RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"NightShift frame-generation backend test",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) throw std::runtime_error("Test window creation failed");
        if(argc>2) {
            ShowWindow(window,SW_SHOWNOACTIVATE);
            std::puts("Please focus the NightShift frame-generation backend test window. Waiting up to 60 seconds."); std::fflush(stdout);
            DWORD start=GetTickCount();
            while(GetForegroundWindow()!=window && GetTickCount()-start<60000) {
                MSG message; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                Sleep(50);
            }
            if(GetForegroundWindow()!=window) throw std::runtime_error("Test requires its own window to be focused; no focus was taken automatically");
        }
        void* proxy=factory.Get(); Check(upgrade(&proxy),"factory upgrade");
        ComPtr<IDXGIFactory4> proxyFactory; proxyFactory.Attach(static_cast<IDXGIFactory4*>(proxy)); factory.Detach();
        DXGI_SWAP_CHAIN_DESC1 swapDesc={}; swapDesc.Width=1280; swapDesc.Height=720; swapDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count=1; swapDesc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; swapDesc.BufferCount=3; swapDesc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swapBase; Check(proxyFactory->CreateSwapChainForHwnd(queue.Get(),window,&swapDesc,nullptr,nullptr,&swapBase),"Streamline swapchain");
        ComPtr<IDXGISwapChain3> swap; Check(swapBase.As(&swap),"swapchain3");
        auto options=Feature<PFun_slDLSSGSetOptions>(functions,sl::kFeatureDLSS_G,"slDLSSGSetOptions");
        auto state=Feature<PFun_slDLSSGGetState>(functions,sl::kFeatureDLSS_G,"slDLSSGGetState");
        auto reflex=Feature<PFun_slReflexSetOptions>(functions,sl::kFeatureReflex,"slReflexSetOptions");
        auto sleep=Feature<PFun_slReflexSleep>(functions,sl::kFeatureReflex,"slReflexSleep");
        auto marker=Feature<PFun_slPCLSetMarker>(functions,sl::kFeaturePCL,"slPCLSetMarker");
        sl::ReflexOptions reflexOptions; reflexOptions.mode=sl::ReflexMode::eLowLatency;
        reflexOptions.frameLimitUs=33333;
        Check(reflex(reflexOptions),"Reflex Low Latency");
        ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
        Check(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"command allocator");
        Check(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"copy list"); list->Close();
        HANDLE finished=CreateEventW(nullptr,FALSE,FALSE,nullptr); uint64_t fenceValue=0,totalRendered=0,totalPresented=0;
        sl::ViewportHandle viewport(0); uint32_t frameIndex=0;
        const UINT multipliers[]={1,2,4,6,1};
        for(UINT multiplier:multipliers) {
            sl::DLSSGOptions fg; fg.mode=multiplier==1?sl::DLSSGMode::eOff:sl::DLSSGMode::eOn; fg.numFramesToGenerate=multiplier-1;
            if(multiplier==1) fg.numFramesToGenerate=1;
            Check(options(viewport,fg),"DLSS-G mode change");
            uint64_t presented=0; const UINT frames=multiplier==1?16:60;
            for(UINT i=0;i<frames;++i) {
                ++frameIndex; ++fenceValue;
                sl::FrameToken* frame=nullptr; Check(newFrame(frame,&frameIndex),"frame token");
                Check(sleep(*frame),"Reflex sleep"); Check(marker(sl::PCLMarker::eSimulationStart,*frame),"simulation start");
                auto constants=Constants(i==0); Check(setConstants(constants,*frame,viewport),"common constants");
                Check(marker(sl::PCLMarker::eSimulationEnd,*frame),"simulation end"); Check(marker(sl::PCLMarker::eRenderSubmitStart,*frame),"render start");
                const float clear[]={.1f+.2f*std::sin(frameIndex*.1f),.3f,.4f,1},depthValue[]={.5f,.5f,.5f,.5f},motionValue[]={0,0,0,0};
                context->ClearRenderTargetView(color.rtv.Get(),clear); context->ClearRenderTargetView(hudless.rtv.Get(),clear);
                context->ClearRenderTargetView(depth.rtv.Get(),depthValue); context->ClearRenderTargetView(motion.rtv.Get(),motionValue);
                Check(context->Signal(from11Native.Get(),fenceValue),"D3D11 render signal"); context->Flush();
                Check(queue->Wait(from11.Get(),fenceValue),"D3D12 input wait");
                Check(allocator->Reset(),"allocator reset"); Check(list->Reset(allocator.Get(),nullptr),"list reset");
                UINT backIndex=swap->GetCurrentBackBufferIndex(); ComPtr<ID3D12Resource> back;
                Check(swap->GetBuffer(backIndex,IID_PPV_ARGS(&back)),"D3D12 backbuffer");
                Barrier(list.Get(),color.native12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
                Barrier(list.Get(),back.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyResource(back.Get(),color.native12.Get());
                Barrier(list.Get(),back.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
                Barrier(list.Get(),color.native12.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
                sl::Resource dep(sl::ResourceType::eTex2d,depth.native12.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
                sl::Resource mv(sl::ResourceType::eTex2d,motion.native12.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
                sl::Resource hud(sl::ResourceType::eTex2d,hudless.native12.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
                sl::Extent extent={0,0,1280,720};
                sl::ResourceTag tags[]={ {&dep,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&extent},
                    {&mv,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&extent},
                    {&hud,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&extent},
                    {nullptr,sl::kBufferTypeBackbuffer,sl::ResourceLifecycle::eValidUntilPresent,&extent} };
                Check(tag(*frame,viewport,tags,4,list.Get()),"frame resource tags");
                Check(list->Close(),"copy list close"); ID3D12CommandList* submit[]={list.Get()}; queue->ExecuteCommandLists(1,submit);
                Check(marker(sl::PCLMarker::eRenderSubmitEnd,*frame),"render end");
                Check(marker(sl::PCLMarker::ePresentStart,*frame),"present start");
                Check(swap->Present(0,0),"DLSS-G present"); Check(marker(sl::PCLMarker::ePresentEnd,*frame),"present end");
                sl::DLSSGState current; Check(state(viewport,current,nullptr),"present-thread DLSS-G state");
                if(current.status!=sl::DLSSGStatus::eOk) throw std::runtime_error("FG status failed: "+std::to_string(unsigned(current.status)));
                presented+=current.numFramesActuallyPresented;
                Check(queue->Signal(done12.Get(),fenceValue),"D3D12 release signal"); Check(context->Wait(done12Native.Get(),fenceValue),"D3D11 reuse wait");
                Check(done12->SetEventOnCompletion(fenceValue,finished),"probe allocator wait"); WaitForSingleObject(finished,10000);
                if(FAILED(d12->GetDeviceRemovedReason()) || FAILED(base11->GetDeviceRemovedReason())) throw std::runtime_error("Graphics device removed");
                MSG message; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            std::printf("Mode %ux: rendered=%u SDK-presented=%llu\n",multiplier,frames,static_cast<unsigned long long>(presented)); std::fflush(stdout);
            if(multiplier>1 && presented<frames+frames/2) throw std::runtime_error("No adequate evidence of additional FG presents");
            totalRendered+=frames; totalPresented+=presented;
        }
        sl::DLSSGOptions off; Check(options(viewport,off),"final FG off");
        CloseHandle(finished); swap.Reset(); swapBase.Reset(); proxyFactory.Reset();
        Check(shutdown(),"slShutdown"); shutdown=nullptr;
        DestroyWindow(window); window=nullptr;
        std::printf("PASS: native D3D11 sharing into genuine DLSS-G D3D12 presentation; rendered=%llu SDK-presented=%llu. This is not a game benchmark.\n",static_cast<unsigned long long>(totalRendered),static_cast<unsigned long long>(totalPresented));
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); if(shutdown) shutdown(); if(window) DestroyWindow(window); return 1;
    }
}
