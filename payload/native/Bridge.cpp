#include "Bridge.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <cstdio>
FGCreateSwapChain fgOriginalCreateSwapChain=nullptr;
static ComPtr<ID3D12Device> graphicsDevice;
static ComPtr<ID3D12CommandQueue> graphicsQueue;
static std::mutex graphicsLock;
static void Check(HRESULT result,const char* operation) {
    if(FAILED(result)) throw std::runtime_error(std::string(operation)+": HRESULT "+std::to_string(uint32_t(result)));
}
static void Check(sl::Result result,const char* operation) {
    if(result!=sl::Result::eOk) throw std::runtime_error(std::string(operation)+": SL result "+std::to_string(uint32_t(result)));
}
bool FGSharedTexture::Create(ID3D12Device* device12,ID3D11Device1* device11,UINT width,UINT height,DXGI_FORMAT format,bool writable) {
    Clear();
    D3D12_HEAP_PROPERTIES heap={}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc={}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=width; desc.Height=height; desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.Format=format; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if(writable) desc.Flags|=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(FAILED(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)))) return false;
    HANDLE handle=nullptr;
    if(FAILED(device12->CreateSharedHandle(resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle))) return false;
    HRESULT result=device11->OpenSharedResource1(handle,IID_PPV_ARGS(&texture)); CloseHandle(handle);
    if(FAILED(result) || FAILED(device11->CreateShaderResourceView(texture.Get(),nullptr,&srv))) return false;
    return !writable || SUCCEEDED(device11->CreateUnorderedAccessView(texture.Get(),nullptr,&uav));
}
static const char* maskSource=R"(
Texture2D<float4> FinalColor : register(t0);
Texture2D<float4> HudlessColor : register(t1);
RWTexture2D<unorm float> UiAlpha : register(u0);
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) {
    uint w,h; UiAlpha.GetDimensions(w,h); if(p.x>=w || p.y>=h) return;
    float3 difference=abs(FinalColor.Load(int3(p.xy,0)).rgb-HudlessColor.Load(int3(p.xy,0)).rgb);
    // Preserve the current composed UI pixel wherever it differs from the same
    // frame's scene. This conservative alpha mask avoids interpolating text.
    UiAlpha[p.xy]=any(difference>1.5/255.0)?1.0:0.0;
})";
HRESULT FGSwapChain::Create(IDXGIFactory* factory,IUnknown* owner,const DXGI_SWAP_CHAIN_DESC& desc,IDXGISwapChain** output) {
    ComPtr<ID3D11Device> gameDevice;
    if(!owner || FAILED(owner->QueryInterface(IID_PPV_ARGS(&gameDevice)))) return E_NOINTERFACE;
    auto bridge=new FGSwapChain;
    try {
        HRESULT result=bridge->Initialize(factory,gameDevice.Get(),desc);
        if(FAILED(result)) { delete bridge; return result; }
        *output=bridge; return S_OK;
    } catch(const std::exception& error) { FG_Message(std::string("Frame-generation bridge initialization failed; preserving original D3D11 swapchain: ")+error.what()); delete bridge; return E_FAIL; }
}
HRESULT FGSwapChain::Initialize(IDXGIFactory* factory,ID3D11Device* owner,const DXGI_SWAP_CHAIN_DESC& desc) {
    gameWindow=desc.OutputWindow;
    Check(owner->QueryInterface(IID_PPV_ARGS(&d11)),"D3D11.1 device");
    Check(owner->QueryInterface(IID_PPV_ARGS(&d11v5)),"D3D11 fence device");
    ComPtr<ID3D11DeviceContext> baseContext; owner->GetImmediateContext(&baseContext);
    Check(baseContext.As(&context),"D3D11.4 immediate context");
    ComPtr<IDXGIDevice> dxgiDevice; Check(owner->QueryInterface(IID_PPV_ARGS(&dxgiDevice)),"renderer DXGI device");
    ComPtr<IDXGIAdapter> adapter; Check(dxgiDevice->GetAdapter(&adapter),"renderer adapter");
    DXGI_ADAPTER_DESC adapterDesc; adapter->GetDesc(&adapterDesc);
    sl::AdapterInfo adapterInfo; adapterInfo.deviceLUID=reinterpret_cast<uint8_t*>(&adapterDesc.AdapterLuid); adapterInfo.deviceLUIDSizeInBytes=sizeof(LUID);
    auto& sl=FG_SL(); Check(sl.support(sl::kFeatureDLSS_G,adapterInfo),"DLSS-G support on renderer adapter");
    {
        std::lock_guard<std::mutex> lock(graphicsLock);
        if(!graphicsDevice) {
            Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&graphicsDevice)),"D3D12 presentation device");
            if(!sl.BindDevice(graphicsDevice.Get())) throw std::runtime_error("Streamline binding failed");
            D3D12_COMMAND_QUEUE_DESC q={}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            Check(graphicsDevice->CreateCommandQueue(&q,IID_PPV_ARGS(&graphicsQueue)),"D3D12 presentation queue");
        }
        d12=graphicsDevice; queue=graphicsQueue;
    }
    WNDCLASSW wc={}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"NightShiftPrivateSwapchain";
    RegisterClassW(&wc);
    dummyWindow=CreateWindowW(wc.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
    if(!dummyWindow) throw std::runtime_error("Private offscreen swapchain window could not be created");
    auto privateDesc=desc; privateDesc.OutputWindow=dummyWindow; privateDesc.Windowed=TRUE;
    privateDesc.Flags&=~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    ComPtr<IDXGISwapChain> rawDummy;
    Check(fgOriginalCreateSwapChain(factory,owner,&privateDesc,&rawDummy),"original D3D11 offscreen swapchain");
    Check(rawDummy.As(&dummy),"D3D11 swapchain interface");
    ComPtr<ID3D11Texture2D> buffer; Check(dummy->GetBuffer(0,IID_PPV_ARGS(&buffer)),"original scene backbuffer");
    D3D11_TEXTURE2D_DESC bufferDesc; buffer->GetDesc(&bufferDesc);
    width=bufferDesc.Width; height=bufferDesc.Height; format=bufferDesc.Format;
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgStatus.reserved=uint32_t(format); }
    if(bufferDesc.SampleDesc.Count!=1 || (format!=DXGI_FORMAT_R8G8B8A8_UNORM && format!=DXGI_FORMAT_B8G8R8A8_UNORM))
        throw std::runtime_error("Unsupported multisampled or HDR backbuffer");
    if(!finalColor.Create(d12.Get(),d11.Get(),width,height,format)) throw std::runtime_error("Shared final-color allocation failed");
    void* upgraded=factory; factory->AddRef(); Check(sl.upgrade(&upgraded),"Streamline factory upgrade");
    ComPtr<IDXGIFactory2> proxyFactory; proxyFactory.Attach(static_cast<IDXGIFactory2*>(upgraded));
    DXGI_SWAP_CHAIN_DESC1 target={}; target.Width=width; target.Height=height; target.Format=format;
    target.SampleDesc.Count=1; target.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; target.BufferCount=3; target.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    target.Flags=desc.Flags & (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING|DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH|DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen={}; fullscreen.Windowed=desc.Windowed; fullscreen.RefreshRate=desc.BufferDesc.RefreshRate;
    ComPtr<IDXGISwapChain1> rawDisplay;
    Check(proxyFactory->CreateSwapChainForHwnd(queue.Get(),gameWindow,&target,&fullscreen,nullptr,&rawDisplay),"D3D12 display swapchain");
    Check(rawDisplay.As(&display),"D3D12 swapchain3");
    Check(d11v5->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&from11Native)),"D3D11 signal fence");
    HANDLE handle; Check(from11Native->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&handle),"D3D11 fence handle");
    HRESULT result=d12->OpenSharedHandle(handle,IID_PPV_ARGS(&from11)); CloseHandle(handle); Check(result,"D3D12 input fence");
    Check(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&to11)),"D3D12 release fence");
    Check(d12->CreateSharedHandle(to11.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"D3D12 release handle");
    result=d11v5->OpenSharedFence(handle,IID_PPV_ARGS(&to11Native)); CloseHandle(handle); Check(result,"D3D11 reuse fence");
    for(UINT i=0;i<3;++i) {
        Check(d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[i])),"presentation allocator");
        Check(d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[i].Get(),nullptr,IID_PPV_ARGS(&lists[i])),"presentation list"); lists[i]->Close();
    }
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0}; D3D_FEATURE_LEVEL chosen;
    Check(d11->CreateDeviceContextState(0,levels,2,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&computeState),"isolated UI-mask graphics state");
    ComPtr<ID3DBlob> code,errors;
    Check(D3DCompile(maskSource,std::strlen(maskSource),"NightShift UI alpha",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors),"UI-mask shader compilation");
    Check(d11->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&uiMaskShader),"UI-mask shader creation");
    finished=CreateEventW(nullptr,FALSE,FALSE,nullptr); QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&sampleStart);
    sl::DLSSGOptions off; off.colorWidth=width; off.colorHeight=height;
    sl::DLSSGState capabilities;
    Check(sl.getState(sl::ViewportHandle(0),capabilities,&off),"frame-generation capabilities");
    if(capabilities.numFramesToGenerateMax<1) throw std::runtime_error("No supported frame-generation multiplier");
    fgMaximumMultiplier=std::min(4u,capabilities.numFramesToGenerateMax+1u);
    Check(sl.setOptions(sl::ViewportHandle(0),off),"initial frame generation OFF");
    FG_Log("Supported total frame multiplier: up to "+std::to_string(fgMaximumMultiplier.load())+"x");
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgStatus.ready=1; }
    FG_Message("D3D11-to-D3D12 presentation installed: "+std::to_string(width)+"x"+std::to_string(height)+"; format="+std::to_string(unsigned(format))+"; game shaders retained");
    return S_OK;
}
void FGSwapChain::Drain() {
    if(queue && to11 && finished) {
        ++fenceValue; queue->Signal(to11.Get(),fenceValue);
        if(to11->GetCompletedValue()<fenceValue) { to11->SetEventOnCompletion(fenceValue,finished); WaitForSingleObject(finished,5000); }
    }
}
void FGSwapChain::Disable() {
    if(enabled && display && FG_SL().ready) { sl::DLSSGOptions off; FG_SL().setOptions(sl::ViewportHandle(0),off); }
    enabled=false; activeMultiplier=0;
}
FGSwapChain::~FGSwapChain() {
    Disable(); if(context) context->Flush(); Drain(); FG_ResetPending();
    if(display) display->SetFullscreenState(FALSE,nullptr);
    display.Reset(); dummy.Reset();
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgStatus.ready=0; fgStatus.active=0; }
    if(finished) CloseHandle(finished);
    if(dummyWindow) DestroyWindow(dummyWindow);
    FG_Log("Frame-generation presentation released; no game exit was requested");
}
HRESULT FGSwapChain::QueryInterface(REFIID id,void** output) {
    if(!output) return E_POINTER;
    if(id==__uuidof(IUnknown) || id==__uuidof(IDXGIObject) || id==__uuidof(IDXGIDeviceSubObject) ||
       id==__uuidof(IDXGISwapChain) || id==__uuidof(IDXGISwapChain1) || id==__uuidof(IDXGISwapChain2) || id==__uuidof(IDXGISwapChain3) || id==__uuidof(IDXGISwapChain4)) {
        *output=static_cast<IDXGISwapChain4*>(this); AddRef(); return S_OK;
    }
    *output=nullptr; return E_NOINTERFACE;
}
HRESULT FGSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* output) {
    auto result=dummy->GetDesc(output); if(SUCCEEDED(result)) { output->OutputWindow=gameWindow; BOOL full=FALSE; display->GetFullscreenState(&full,nullptr); output->Windowed=!full; } return result;
}
HRESULT FGSwapChain::SetFullscreenState(BOOL full,IDXGIOutput* output) { Disable(); Drain(); return display->SetFullscreenState(full,output); }
HRESULT FGSwapChain::ResizeBuffers(UINT count,UINT w,UINT h,DXGI_FORMAT newFormat,UINT flags) {
    Disable(); FG_ResetPending(); Drain();
    auto result=dummy->ResizeBuffers(count,w,h,newFormat,flags & ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH); if(FAILED(result)) return result;
    ComPtr<ID3D11Texture2D> buffer; result=dummy->GetBuffer(0,IID_PPV_ARGS(&buffer)); if(FAILED(result)) return result;
    D3D11_TEXTURE2D_DESC desc; buffer->GetDesc(&desc); width=desc.Width; height=desc.Height; format=desc.Format;
    result=display->ResizeBuffers(0,width,height,format,flags); if(FAILED(result)) return result;
    finalColor.Clear(); hudless.Clear(); depth.Clear(); motion.Clear(); alpha.Clear(); inputWidth=inputHeight=0;
    return finalColor.Create(d12.Get(),d11.Get(),width,height,format)?S_OK:E_FAIL;
}
HRESULT FGSwapChain::ResizeBuffers1(UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags,const UINT*,IUnknown* const*) { return ResizeBuffers(count,w,h,format,flags); }

static DXGI_FORMAT Family(DXGI_FORMAT format) {
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format==DXGI_FORMAT_R8G8B8A8_TYPELESS) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format==DXGI_FORMAT_B8G8R8A8_TYPELESS) return DXGI_FORMAT_B8G8R8A8_UNORM;
    if(format==DXGI_FORMAT_R32_TYPELESS) return DXGI_FORMAT_R32_FLOAT;
    if(format==DXGI_FORMAT_R16G16_TYPELESS) return DXGI_FORMAT_R16G16_FLOAT;
    return format;
}
static std::string Description(ID3D11Texture2D* texture,ID3D11Device* expected) {
    if(!texture) return "missing";
    D3D11_TEXTURE2D_DESC desc={}; texture->GetDesc(&desc);
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
    ComPtr<IUnknown> actualIdentity,wantedIdentity;
    owner.As(&actualIdentity); expected->QueryInterface(IID_PPV_ARGS(&wantedIdentity));
    return std::to_string(desc.Width)+"x"+std::to_string(desc.Height)+" fmt="+std::to_string(uint32_t(desc.Format))+" family="+std::to_string(uint32_t(Family(desc.Format)))+" device="+(actualIdentity.Get()==wantedIdentity.Get()?"match":"DIFFERENT");
}
bool FGSwapChain::AllocateInputs(const FGFrame& frame) {
    if(frame.outputWidth!=width || frame.outputHeight!=height) return false;
    if(hudless.resource && inputWidth==frame.inputWidth && inputHeight==frame.inputHeight) return true;
    Disable(); Drain();
    inputWidth=frame.inputWidth; inputHeight=frame.inputHeight;
    return hudless.Create(d12.Get(),d11.Get(),width,height,format) &&
        depth.Create(d12.Get(),d11.Get(),inputWidth,inputHeight,DXGI_FORMAT_R32_FLOAT) &&
        motion.Create(d12.Get(),d11.Get(),inputWidth,inputHeight,DXGI_FORMAT_R16G16_FLOAT) &&
        alpha.Create(d12.Get(),d11.Get(),width,height,DXGI_FORMAT_R8_UNORM,true);
}
static bool Matches(ID3D11Texture2D* texture,ID3D11Device* expected,UINT width,UINT height,DXGI_FORMAT format) {
    if(!texture) return false;
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
    D3D11_TEXTURE2D_DESC desc; texture->GetDesc(&desc);
    ComPtr<IUnknown> ownerIdentity,expectedIdentity;
    owner.As(&ownerIdentity); expected->QueryInterface(IID_PPV_ARGS(&expectedIdentity));
    return ownerIdentity.Get()==expectedIdentity.Get() && desc.Width==width && desc.Height==height && desc.SampleDesc.Count==1 && Family(desc.Format)==format;
}
static sl::Constants FrameConstants(const FGFrame& frame) {
    sl::Constants constants;
    std::memcpy(&constants.cameraViewToClip,frame.viewToClip.values,64);
    std::memcpy(&constants.clipToCameraView,frame.clipToView.values,64);
    std::memcpy(&constants.clipToPrevClip,frame.clipToPrevious.values,64);
    std::memcpy(&constants.prevClipToClip,frame.previousToClip.values,64);
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) reinterpret_cast<float*>(&constants.clipToLensClip[r])[c]=r==c?1.f:0.f;
    constants.jitterOffset={frame.jitterX,frame.jitterY}; constants.mvecScale={frame.motionScaleX,frame.motionScaleY}; constants.cameraPinholeOffset={0,0};
    constants.cameraPos={frame.position[0],frame.position[1],frame.position[2]};
    constants.cameraUp={frame.up[0],frame.up[1],frame.up[2]}; constants.cameraRight={frame.right[0],frame.right[1],frame.right[2]};
    constants.cameraFwd={frame.forward[0],frame.forward[1],frame.forward[2]};
    constants.cameraNear=frame.depthNear; constants.cameraFar=frame.depthFar; constants.cameraFOV=frame.fov; constants.cameraAspectRatio=frame.aspect;
    constants.depthInverted=(frame.flags&1)?sl::eTrue:sl::eFalse; constants.reset=(frame.flags&2)?sl::eTrue:sl::eFalse;
    constants.cameraMotionIncluded=sl::eTrue; constants.motionVectors3D=sl::eFalse; constants.motionVectorsInvalidValue=0;
    return constants;
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=resource;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore=before; b.Transition.StateAfter=after;
    list->ResourceBarrier(1,&b);
}
HRESULT FGSwapChain::CopyAndPresent(UINT interval,UINT flags) {
    if(flags&DXGI_PRESENT_TEST) return display->Present(interval,flags);
    try {
        auto& sl=FG_SL();
        std::unique_ptr<FGPacket> packet;
        { std::lock_guard<std::mutex> lock(fgPendingLock); packet.reset(fgPending); fgPending=nullptr; }
        const uint32_t multiplier=fgRequested.load();
        bool canGenerate=multiplier>=2 && multiplier<=fgMaximumMultiplier.load() && packet && (packet->frame.flags&4) && packet->frame.frameId>lastFrame;
        auto frame=sl.FindFrame(packet?packet->frame.frameId:fgRenderFrame.load());
        canGenerate=canGenerate && frame && GetForegroundWindow()==gameWindow;
        if(multiplier>=2 && !canGenerate) {
            int reason=!packet?1:!(packet->frame.flags&4)?2:packet->frame.frameId<=lastFrame?3:!frame?4:5;
            if(reason!=previousReason) {
                const char* reasons[]={"","waiting for this frame's camera packet","gameplay suspended","camera frame already presented","matching Reflex frame token missing","game window not focused"};
                FG_Message(std::string("Frame generation awaiting inputs: ")+reasons[reason]); previousReason=reason;
            }
        } else previousReason=-1;
        if(canGenerate) {
            auto& f=packet->frame;
            canGenerate=Matches(packet->hudless.Get(),d11.Get(),width,height,format) &&
                Matches(packet->depth.Get(),d11.Get(),f.inputWidth,f.inputHeight,DXGI_FORMAT_R32_FLOAT) &&
                Matches(packet->motion.Get(),d11.Get(),f.inputWidth,f.inputHeight,DXGI_FORMAT_R16G16_FLOAT) && AllocateInputs(f);
            if(!canGenerate && previousReason!=6) {
                FG_Message("Frame-generation buffers rejected: HUD="+Description(packet->hudless.Get(),d11.Get())+"; depth="+Description(packet->depth.Get(),d11.Get())+"; motion="+Description(packet->motion.Get(),d11.Get())+"; expected input="+std::to_string(f.inputWidth)+"x"+std::to_string(f.inputHeight)+" output="+std::to_string(width)+"x"+std::to_string(height)+" format="+std::to_string(uint32_t(format)));
                previousReason=6;
            }
        }
        if(canGenerate!=enabled || (canGenerate && activeMultiplier!=multiplier)) {
            sl::DLSSGOptions options;
            options.mode=canGenerate?sl::DLSSGMode::eOn:sl::DLSSGMode::eOff;
            options.numFramesToGenerate=canGenerate?multiplier-1u:1u;
            options.enableUserInterfaceRecomposition=sl::eTrue;
            Check(sl.setOptions(sl::ViewportHandle(0),options),"frame-generation options");
            enabled=canGenerate; activeMultiplier=canGenerate?multiplier:0;
            FG_Message(enabled?std::to_string(activeMultiplier)+"x NVIDIA frame generation enabled with fresh camera buffers and UI alpha":"Frame generation OFF or suspended; ordinary presentation retained");
        }
        ComPtr<ID3D11Texture2D> back11;
        Check(dummy->GetBuffer(dummy->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back11)),"D3D11 final backbuffer");
        context->CopyResource(finalColor.texture.Get(),back11.Get());
        if(canGenerate) {
            context->CopyResource(hudless.texture.Get(),packet->hudless.Get());
            context->CopyResource(depth.texture.Get(),packet->depth.Get());
            context->CopyResource(motion.texture.Get(),packet->motion.Get());
            ComPtr<ID3DDeviceContextState> oldState; context->SwapDeviceContextState(computeState.Get(),&oldState);
            ID3D11ShaderResourceView* inputs[]={finalColor.srv.Get(),hudless.srv.Get()};
            ID3D11UnorderedAccessView* output[]={alpha.uav.Get()};
            context->CSSetShader(uiMaskShader.Get(),nullptr,0); context->CSSetShaderResources(0,2,inputs); context->CSSetUnorderedAccessViews(0,1,output,nullptr);
            context->Dispatch((width+7)/8,(height+7)/8,1);
            context->SwapDeviceContextState(oldState.Get(),nullptr);
        }
        ++fenceValue;
        Check(context->Signal(from11Native.Get(),fenceValue),"D3D11 frame completion"); context->Flush();
        Check(queue->Wait(from11.Get(),fenceValue),"D3D12 frame wait");
        if(slotFences[slot] && to11->GetCompletedValue()<slotFences[slot]) {
            Check(to11->SetEventOnCompletion(slotFences[slot],finished),"allocator fence");
            if(WaitForSingleObject(finished,5000)!=WAIT_OBJECT_0) throw std::runtime_error("Presentation allocator timeout");
        }
        Check(allocators[slot]->Reset(),"presentation allocator reset");
        auto list=lists[slot].Get(); Check(list->Reset(allocators[slot].Get(),nullptr),"presentation list reset");
        ComPtr<ID3D12Resource> back12; UINT backIndex=display->GetCurrentBackBufferIndex();
        Check(display->GetBuffer(backIndex,IID_PPV_ARGS(&back12)),"D3D12 presentation backbuffer");
        Transition(list,finalColor.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list,back12.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(back12.Get(),finalColor.resource.Get());
        Transition(list,back12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
        Transition(list,finalColor.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        if(canGenerate) {
            Check(sl.constants(FrameConstants(packet->frame),*frame,sl::ViewportHandle(0)),"camera constants");
            sl::Resource dep(sl::ResourceType::eTex2d,depth.resource.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
            sl::Resource mv(sl::ResourceType::eTex2d,motion.resource.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
            sl::Resource hud(sl::ResourceType::eTex2d,hudless.resource.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
            sl::Resource ui(sl::ResourceType::eTex2d,alpha.resource.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_COMMON);
            sl::Extent inputExtent={0,0,inputWidth,inputHeight},full={0,0,width,height};
            sl::ResourceTag tags[]={ {&dep,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&inputExtent},
                {&mv,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&inputExtent},
                {&hud,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&full},
                {&ui,sl::kBufferTypeUIAlpha,sl::ResourceLifecycle::eValidUntilPresent,&full},
                {nullptr,sl::kBufferTypeBackbuffer,sl::ResourceLifecycle::eValidUntilPresent,&full} };
            Check(sl.tags(*frame,sl::ViewportHandle(0),tags,5,list),"DLSS-G frame resource tags");
            lastFrame=packet->frame.frameId;
        } else if(frame) {
            sl::Extent full={0,0,width,height};
            sl::ResourceTag clear[]={ {nullptr,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent},
                {nullptr,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent},
                {nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent},
                {nullptr,sl::kBufferTypeUIAlpha,sl::ResourceLifecycle::eValidUntilPresent},
                {nullptr,sl::kBufferTypeBackbuffer,sl::ResourceLifecycle::eValidUntilPresent,&full} };
            sl.tags(*frame,sl::ViewportHandle(0),clear,5,list);
        }
        Check(list->Close(),"presentation list close");
        ID3D12CommandList* submit[]={list}; queue->ExecuteCommandLists(1,submit);
        if(frame) { sl.marker(sl::PCLMarker::eRenderSubmitEnd,*frame); sl.marker(sl::PCLMarker::ePresentStart,*frame); }
        HRESULT result=display->Present(interval,flags);
        if(frame) sl.marker(sl::PCLMarker::ePresentEnd,*frame);
        sl::DLSSGState state; auto statusResult=sl.getState(sl::ViewportHandle(0),state,nullptr);
        ++rendered;
        if(statusResult==sl::Result::eOk) {
            presented+=state.numFramesActuallyPresented;
            if(state.numFramesActuallyPresented>1) generated+=state.numFramesActuallyPresented-1;
            if(enabled && state.status!=sl::DLSSGStatus::eOk) {
                FG_Failure("DLSS-G runtime status",uint32_t(state.status)); Disable();
            }
        }
        Check(queue->Signal(to11.Get(),fenceValue),"D3D12 frame release");
        Check(context->Wait(to11Native.Get(),fenceValue),"D3D11 shared-buffer reuse");
        slotFences[slot]=fenceValue; slot=(slot+1)%3;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double seconds=double(now.QuadPart-sampleStart.QuadPart)/frequency.QuadPart;
        {
            std::lock_guard<std::mutex> lock(fgStatusLock);
            fgStatus.rendered=rendered; fgStatus.presented=presented; fgStatus.generated=generated;
            fgStatus.active=enabled && statusResult==sl::Result::eOk && state.status==sl::DLSSGStatus::eOk;
            fgStatus.fgStatus=uint32_t(state.status); fgStatus.apiResult=uint32_t(statusResult);
            if(seconds>=1) { fgStatus.renderedFps=(rendered-sampleRendered)/seconds; fgStatus.presentedFps=(presented-samplePresented)/seconds; }
        }
        if(seconds>=1) { sampleStart=now; sampleRendered=rendered; samplePresented=presented; }
        return result;
    } catch(const std::exception& error) {
        FG_Failure(std::string("Presentation error: ")+error.what(),0xBADF0001); Disable();
        // Never suppress the game or request a quit. The retained ordinary
        // D3D12 swapchain continues presenting the last completed scene.
        return display?display->Present(interval,flags):DXGI_ERROR_DEVICE_REMOVED;
    }
}
