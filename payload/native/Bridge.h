#pragma once
#include "FrameGen.h"
#include <memory>
#include <array>
struct FGSharedTexture {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    bool Create(ID3D12Device*,ID3D11Device1*,UINT,UINT,DXGI_FORMAT,bool=false);
    void Clear() { uav.Reset(); srv.Reset(); texture.Reset(); resource.Reset(); }
};
class FGSwapChain final : public IDXGISwapChain4 {
    std::atomic<ULONG> refs{1};
    ComPtr<IDXGISwapChain4> dummy;
    ComPtr<IDXGISwapChain3> display;
    ComPtr<ID3D11Device1> d11; ComPtr<ID3D11Device5> d11v5;
    ComPtr<ID3D11DeviceContext4> context;
    ComPtr<ID3DDeviceContextState> computeState;
    ComPtr<ID3D11ComputeShader> uiMaskShader;
    ComPtr<ID3D12Device> d12; ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D11Fence> from11Native,to11Native;
    ComPtr<ID3D12Fence> from11,to11;
    std::array<ComPtr<ID3D12CommandAllocator>,3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,3> lists;
    uint64_t slotFences[3]={},fenceValue=0;
    FGSharedTexture finalColor,hudless,depth,motion,alpha;
    HWND gameWindow=nullptr,dummyWindow=nullptr;
    HANDLE finished=nullptr;
    UINT width=0,height=0,inputWidth=0,inputHeight=0,slot=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool enabled=false; uint32_t activeMultiplier=0,lastFrame=0;
    int previousReason=-1;
    uint64_t rendered=0,presented=0,generated=0,sampleRendered=0,samplePresented=0;
    LARGE_INTEGER sampleStart={},frequency={};
    bool AllocateInputs(const FGFrame& frame);
    void Drain();
    void Disable();
    HRESULT CopyAndPresent(UINT interval,UINT flags);
    ~FGSwapChain();
public:
    static HRESULT Create(IDXGIFactory*,IUnknown*,const DXGI_SWAP_CHAIN_DESC&,IDXGISwapChain**);
    HRESULT Initialize(IDXGIFactory*,ID3D11Device*,const DXGI_SWAP_CHAIN_DESC&);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--refs; if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID key,UINT size,const void* data) override { return dummy->SetPrivateData(key,size,data); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID key,const IUnknown* data) override { return dummy->SetPrivateDataInterface(key,data); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID key,UINT* size,void* data) override { return dummy->GetPrivateData(key,size,data); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** parent) override { return dummy->GetParent(id,parent); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** device) override { return dummy->GetDevice(id,device); }
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags) override { return CopyAndPresent(interval,flags); }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void** buffer) override { return dummy->GetBuffer(index,id,buffer); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL state,IDXGIOutput* target) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* state,IDXGIOutput** target) override { return display->GetFullscreenState(state,target); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT,UINT,UINT,DXGI_FORMAT,UINT) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* target) override { Disable(); return display->ResizeTarget(target); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override { return display->GetContainingOutput(output); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* stats) override { return display->GetFrameStatistics(stats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override { return display->GetLastPresentCount(count); }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override { return dummy->GetDesc1(desc); }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override { return display->GetFullscreenDesc(desc); }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* output) override { if(!output) return E_POINTER; *output=gameWindow; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id,void** window) override { return display->GetCoreWindow(id,window); }
    HRESULT STDMETHODCALLTYPE Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS*) override { return CopyAndPresent(interval,flags); }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return display->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override { return display->GetRestrictToOutput(output); }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return display->SetBackgroundColor(color); }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return display->GetBackgroundColor(color); }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return display->SetRotation(rotation); }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return display->GetRotation(rotation); }
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w,UINT h) override { return display->SetSourceSize(w,h); }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w,UINT* h) override { return display->GetSourceSize(w,h); }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override { return display->SetMaximumFrameLatency(latency); }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override { return display->GetMaximumFrameLatency(latency); }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return display->GetFrameLatencyWaitableObject(); }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* matrix) override { return display->SetMatrixTransform(matrix); }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* matrix) override { return display->GetMatrixTransform(matrix); }
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return dummy->GetCurrentBackBufferIndex(); }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE type,UINT* support) override { return display->CheckColorSpaceSupport(type,support); }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE type) override { return display->SetColorSpace1(type); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown* const*) override;
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE,UINT,void*) override { return E_NOTIMPL; }
};
using FGCreateSwapChain=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
extern FGCreateSwapChain fgOriginalCreateSwapChain;
