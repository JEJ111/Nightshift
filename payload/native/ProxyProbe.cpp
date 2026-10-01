// Exercises the exact DXGI proxy used by the mod, including its native D3D11
// backbuffer, shared resources, UI mask, D3D12 swapchain and resize path.
#include "FrameGen.h"
#include <filesystem>
#include <cstdio>
#include <cmath>
#include <stdexcept>
template<class T> static T Function(HMODULE module,const char* name) {
    auto value=reinterpret_cast<T>(GetProcAddress(module,name));
    if(!value) throw std::runtime_error(std::string("Missing export: ")+name); return value;
}
static void Check(HRESULT result,const char* name) { if(FAILED(result)) throw std::runtime_error(std::string(name)+": "+std::to_string(uint32_t(result))); }
struct Texture {
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> rtv;
    void Create(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC desc={}; desc.Width=w; desc.Height=h; desc.Format=format; desc.MipLevels=desc.ArraySize=1;
        desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(device->CreateTexture2D(&desc,nullptr,&texture),"texture"); Check(device->CreateRenderTargetView(texture.Get(),nullptr,&rtv),"RTV");
    }
};
static FGMatrix Identity() { FGMatrix m={}; for(int i=0;i<4;++i) m.values[i*4+i]=1; return m; }
int wmain(int argc,wchar_t** argv) {
    HWND window=nullptr;
    try {
        auto path=std::filesystem::absolute(L"dxgi.dll");
        auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module) throw std::runtime_error("DXGI proxy could not be loaded");
        Function<void(__cdecl*)()>(module,"NSFG_SetProbeMode")();
        auto factoryFunction=Function<HRESULT(WINAPI*)(REFIID,void**)>(module,"CreateDXGIFactory1");
        auto begin=Function<void(__cdecl*)(uint32_t)>(module,"NSFG_BeginFrame");
        auto end=Function<void(__cdecl*)(uint32_t)>(module,"NSFG_EndSimulation");
        auto configure=Function<void(__cdecl*)(uint32_t)>(module,"NSFG_Configure");
        auto status=Function<void(__cdecl*)(FGStatus*,char*,uint32_t)>(module,"NSFG_GetStatus");
        auto packet=Function<void*(__cdecl*)(const FGFrame*)>(module,"NSFG_CreatePacket");
        auto render=reinterpret_cast<void(__stdcall*)(int,void*)>(Function<void*(__cdecl*)()>(module,"NSFG_GetRenderEvent")());
        ComPtr<IDXGIFactory1> factory; Check(factoryFunction(IID_PPV_ARGS(&factory)),"factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i) { Check(factory->EnumAdapters1(i,&adapter),"adapter"); DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc); if(desc.VendorId==0x10DE) break; adapter.Reset(); }
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level;
        Check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"D3D11 renderer");
        WNDCLASSW wc={}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"NightShiftProxyProbe"; RegisterClassW(&wc);
        window=CreateWindowW(wc.lpszClassName,L"NightShift frame-generation presentation test",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window) throw std::runtime_error("window");
        bool visible=argc>1;
        if(visible) {
            ShowWindow(window,SW_SHOWNOACTIVATE); std::puts("Focus the presentation test window manually; this test closes only its own window."); std::fflush(stdout);
            DWORD start=GetTickCount();
            while(GetForegroundWindow()!=window && GetTickCount()-start<600000) { MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(50); }
            if(GetForegroundWindow()!=window) throw std::runtime_error("Test window not focused");
        }
        DXGI_SWAP_CHAIN_DESC desc={}; desc.OutputWindow=window; desc.Windowed=TRUE; desc.BufferDesc.Width=1280; desc.BufferDesc.Height=720;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferCount=2; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.SampleDesc.Count=1; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> swap; Check(factory->CreateSwapChain(device.Get(),&desc,&swap),"bridged swapchain");
        FGStatus before={sizeof(FGStatus)}; char text[1024]; status(&before,text,sizeof(text));
        if(!before.ready) throw std::runtime_error(std::string("Bridge not ready: ")+text);
        DXGI_SWAP_CHAIN_DESC reported; Check(swap->GetDesc(&reported),"reported descriptor");
        if(reported.OutputWindow!=window) throw std::runtime_error("Proxy changed public HWND");
        Texture hud,depth,motion; hud.Create(device.Get(),1280,720,DXGI_FORMAT_R8G8B8A8_UNORM); depth.Create(device.Get(),640,360,DXGI_FORMAT_R32_FLOAT); motion.Create(device.Get(),640,360,DXGI_FORMAT_R16G16_FLOAT);
        uint32_t frameId=0;
        for(uint32_t mode: {0u,visible?2u:0u,visible?3u:0u,visible?4u:0u,0u}) {
            configure(mode); FGStatus start={sizeof(FGStatus)}; status(&start,text,sizeof(text));
            UINT frames=mode?180:16;
            for(UINT i=0;i<frames;++i) {
                ++frameId; begin(frameId); end(frameId);
                ComPtr<ID3D11Texture2D> back; Check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"native D3D11 backbuffer");
                ComPtr<ID3D11RenderTargetView> target; Check(device->CreateRenderTargetView(back.Get(),nullptr,&target),"native backbuffer RTV");
                const float clear[]={.1f+.15f*std::sin(frameId*.1f),.3f,.4f,1},d[]={.5f,.5f,.5f,.5f},mv[]={0,0,0,0};
                context->ClearRenderTargetView(target.Get(),clear); context->ClearRenderTargetView(hud.rtv.Get(),clear); context->ClearRenderTargetView(depth.rtv.Get(),d); context->ClearRenderTargetView(motion.rtv.Get(),mv);
                FGFrame f={}; f.size=sizeof(f); f.version=1; f.frameId=frameId; f.flags=4|(i==0?2:0);
                f.hudless=hud.texture.Get(); f.depth=depth.texture.Get(); f.motion=motion.texture.Get(); f.inputWidth=640; f.inputHeight=360; f.outputWidth=1280; f.outputHeight=720;
                f.depthNear=.1f; f.depthFar=100; f.fov=1; f.aspect=1280.f/720; f.motionScaleX=f.motionScaleY=1; f.up[1]=f.right[0]=f.forward[2]=1;
                f.viewToClip=Identity(); f.clipToView=Identity(); f.clipToPrevious=f.previousToClip=Identity();
                float s=1/std::tan(.5f),a=100/99.9f,b=-.1f*a;
                f.viewToClip.values[0]=s/f.aspect; f.viewToClip.values[5]=s; f.viewToClip.values[10]=a; f.viewToClip.values[11]=1; f.viewToClip.values[14]=b; f.viewToClip.values[15]=0;
                f.clipToView.values[0]=f.aspect/s; f.clipToView.values[5]=1/s; f.clipToView.values[10]=0; f.clipToView.values[11]=1/b; f.clipToView.values[14]=1; f.clipToView.values[15]=-a/b;
                render(3,reinterpret_cast<void*>(uintptr_t(frameId))); if(mode) render(1,packet(&f));
                Check(swap->Present(0,0),"bridged present");
                MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                Sleep(33);
            }
            FGStatus current={sizeof(FGStatus)}; status(&current,text,sizeof(text));
            std::printf("Mode %u: rendered=%llu presented=%llu generated=%llu API=%u FG=%u status=%s\n",mode,current.rendered-start.rendered,current.presented-start.presented,current.generated-start.generated,current.apiResult,current.fgStatus,text); std::fflush(stdout);
            if(current.apiResult || current.fgStatus || (mode && current.presented-start.presented<uint64_t(frames*mode*0.9))) throw std::runtime_error("Proxy generation/status validation failed");
        }
        configure(0); Check(swap->ResizeBuffers(2,1280,720,DXGI_FORMAT_R8G8B8A8_UNORM,0),"resize/reallocation");
        swap.Reset(); DestroyWindow(window); window=nullptr;
        std::puts("PASS: exact proxy ordinary presentation, buffers, resize and release. Visible mode additionally validates actual generated frames."); return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); if(window) DestroyWindow(window); return 1; }
}
