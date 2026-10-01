#include "FrameGen.h"
#include "sl_helpers.h"
#include "sl_security.h"
#include <filesystem>
#include <cstdio>
#include <memory>
#include <unordered_map>
std::atomic<uint32_t> fgRequested{0};
std::atomic<uint32_t> fgMaximumMultiplier{1};
std::atomic<uint64_t> fgSubmitted{0};
std::atomic<uint32_t> fgRenderFrame{0};
std::mutex fgPendingLock,fgStatusLock;
FGPacket* fgPending=nullptr;
FGStatus fgStatus={sizeof(FGStatus),1};
static std::string fgMessage="Frame generation OFF; waiting for graphics bootstrap";
static std::mutex logLock;
static HMODULE self=nullptr;
static std::mutex packetsLock;
static std::unordered_map<uintptr_t,std::unique_ptr<FGPacket>> packets;
static uintptr_t nextPacket=1;
std::wstring FG_Root() {
    if(!self) GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&FG_Root),&self);
    wchar_t path[32768]; GetModuleFileNameW(self,path,32768);
    return std::filesystem::path(path).parent_path().wstring();
}
void FG_Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logLock);
    auto folder=std::filesystem::path(FG_Root())/L"BepInEx/NightShift/framegen";
    std::error_code error; std::filesystem::create_directories(folder,error);
    FILE* file=nullptr; _wfopen_s(&file,(folder/L"bridge.log").c_str(),L"ab");
    if(file) { std::fprintf(file,"[pid=%lu tid=%lu] %s\n",GetCurrentProcessId(),GetCurrentThreadId(),message.c_str()); std::fclose(file); }
}
void FG_Message(const std::string& message) {
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgMessage=message; }
    FG_Log(message);
}
void FG_Failure(const std::string& operation,uint32_t result) {
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgStatus.apiResult=result; fgStatus.active=0; }
    FG_Message(operation+": error "+std::to_string(result)+"; frame generation disabled");
    fgRequested=0;
}
FGSession& FG_SL() { static FGSession* instance=new FGSession; return *instance; }
template<class T> static bool Import(HMODULE module,const char* name,T*& function) {
    function=reinterpret_cast<T*>(GetProcAddress(module,name));
    if(!function) FG_Failure(std::string("Missing Streamline export ")+name,GetLastError());
    return function!=nullptr;
}
static void SLLog(sl::LogType type,const char* message) {
    if(type==sl::LogType::eWarn || type==sl::LogType::eError) FG_Log(std::string("Streamline ")+message);
}
bool FGSession::Initialize() {
    if(initialized) return true;
    auto runtime=std::filesystem::path(FG_Root())/L"BepInEx/NightShift/framegen/runtime";
    auto dll=runtime/L"sl.interposer.dll";
    if(!std::filesystem::exists(dll) || !sl::security::verifyEmbeddedSignature(dll.c_str())) {
        FG_Message("Frame generation unavailable: signed Streamline runtime missing or invalid"); return false;
    }
    module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) { FG_Failure("Loading Streamline",GetLastError()); return false; }
    if(!Import(module,"slInit",init) || !Import(module,"slShutdown",shutdown) || !Import(module,"slSetD3DDevice",setDevice) ||
       !Import(module,"slUpgradeInterface",upgrade) || !Import(module,"slGetNativeInterface",getNative) ||
       !Import(module,"slGetFeatureFunction",getFunction) || !Import(module,"slGetNewFrameToken",newFrame) ||
       !Import(module,"slSetConstants",constants) || !Import(module,"slSetTagForFrame",tags) || !Import(module,"slIsFeatureSupported",support)) return false;
    auto runtimePath=runtime.wstring(),logs=(runtime.parent_path()/L"logs").wstring();
    std::filesystem::create_directories(logs);
    const wchar_t* paths[]={runtimePath.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
    sl::Preferences preferences;
    preferences.pathsToPlugins=paths; preferences.numPathsToPlugins=1; preferences.pathToLogsAndData=logs.c_str();
    preferences.featuresToLoad=features; preferences.numFeaturesToLoad=3; preferences.logMessageCallback=&SLLog;
    preferences.engine=sl::EngineType::eUnity; preferences.engineVersion="2020.3.44f1";
    preferences.projectId="19f82428-6481-48c5-b47c-e2df3df2dc92";
    preferences.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    auto result=init(preferences,sl::kSDKVersion);
    if(result!=sl::Result::eOk) { FG_Failure("Streamline early initialization",uint32_t(result)); return false; }
    initialized=true; FG_Message("Streamline initialized before the frame-generation D3D12 graphics path"); return true;
}
template<class T> static bool Feature(FGSession& s,sl::Feature feature,const char* name,T*& target) {
    void* function=nullptr; auto result=s.getFunction(feature,name,function);
    if(result!=sl::Result::eOk || !function) { FG_Failure(name,uint32_t(result)); return false; }
    target=reinterpret_cast<T*>(function); return true;
}
bool FGSession::BindDevice(ID3D12Device* device) {
    if(ready) return true;
    if(!initialized) return false;
    auto result=setDevice(device);
    if(result!=sl::Result::eOk) { FG_Failure("Streamline D3D12 device binding",uint32_t(result)); return false; }
    if(!Feature(*this,sl::kFeatureDLSS_G,"slDLSSGSetOptions",setOptions) || !Feature(*this,sl::kFeatureDLSS_G,"slDLSSGGetState",getState) ||
       !Feature(*this,sl::kFeatureReflex,"slReflexSetOptions",reflexOptions) || !Feature(*this,sl::kFeatureReflex,"slReflexSleep",sleep) ||
       !Feature(*this,sl::kFeaturePCL,"slPCLSetMarker",marker)) return false;
    sl::ReflexOptions options; options.mode=sl::ReflexMode::eLowLatency;
    result=reflexOptions(options);
    if(result!=sl::Result::eOk) { FG_Failure("Reflex options",uint32_t(result)); return false; }
    ready=true;
    FG_Message("Frame-generation bridge ready; native D3D11 rendering retained; Reflex low latency enabled; FG starts OFF"); return true;
}
sl::FrameToken* FGSession::FindFrame(uint32_t id) {
    std::lock_guard<std::mutex> lock(tokenLock);
    auto& token=tokens[id%16]; return token.id==id?token.value:nullptr;
}
bool FGSession::Begin(uint32_t id) {
    if(!ready || !id) return false;
    sl::FrameToken* token=nullptr;
    auto result=newFrame(token,&id);
    if(result!=sl::Result::eOk || !token) { FG_Failure("Frame token",uint32_t(result)); return false; }
    { std::lock_guard<std::mutex> lock(tokenLock); tokens[id%16]={id,token}; }
    result=sleep(*token);
    if(result==sl::Result::eOk) result=marker(sl::PCLMarker::eSimulationStart,*token);
    if(result!=sl::Result::eOk) { FG_Failure("Reflex frame start",uint32_t(result)); return false; }
    return true;
}
void FGSession::End(uint32_t id) {
    if(!ready) return;
    auto token=FindFrame(id); if(!token) return;
    auto result=marker(sl::PCLMarker::eSimulationEnd,*token);
    if(result==sl::Result::eOk) result=marker(sl::PCLMarker::eRenderSubmitStart,*token);
    if(result!=sl::Result::eOk) FG_Failure("Reflex simulation end",uint32_t(result));
}
void FG_ResetPending() {
    std::lock_guard<std::mutex> lock(fgPendingLock); delete fgPending; fgPending=nullptr;
}
FG_EXPORT void __cdecl NSFG_BeginFrame(uint32_t id) { FG_SL().Begin(id); }
FG_EXPORT void __cdecl NSFG_EndSimulation(uint32_t id) { FG_SL().End(id); }
FG_EXPORT void __cdecl NSFG_Configure(uint32_t multiplier) {
    fgRequested=(multiplier>=2 && multiplier<=4 && multiplier<=fgMaximumMultiplier.load())?multiplier:0;
    { std::lock_guard<std::mutex> lock(fgStatusLock); fgStatus.requested=fgRequested.load(); }
    FG_Message(fgRequested?std::to_string(fgRequested.load())+"x frame generation requested; awaiting fresh gameplay buffers":"Frame generation OFF requested");
}
FG_EXPORT uint32_t __cdecl NSFG_GetMaximumMultiplier() { return fgMaximumMultiplier.load(); }
static void __stdcall RenderEvent(int eventId,void* data) {
    if(eventId==2) { FG_ResetPending(); return; }
    if(eventId==3) { fgRenderFrame=uint32_t(reinterpret_cast<uintptr_t>(data)); return; }
    std::unique_ptr<FGPacket> owned;
    { std::lock_guard<std::mutex> lock(packetsLock);
      auto found=packets.find(reinterpret_cast<uintptr_t>(data));
      if(found==packets.end()) return;
      owned=std::move(found->second); packets.erase(found); }
    auto packet=owned.release();
    fgRenderFrame=packet->frame.frameId;
    if(!fgRequested) { delete packet; return; }
    std::lock_guard<std::mutex> lock(fgPendingLock);
    delete fgPending; fgPending=packet; ++fgSubmitted;
}
FG_EXPORT void* __cdecl NSFG_GetRenderEvent() { return reinterpret_cast<void*>(&RenderEvent); }
FG_EXPORT void* __cdecl NSFG_CreatePacket(const FGFrame* frame) {
    if(!frame || frame->size!=sizeof(FGFrame) || frame->version!=1 || !frame->hudless || !frame->depth || !frame->motion) return nullptr;
    auto packet=std::make_unique<FGPacket>(); packet->frame=*frame;
    packet->hudless=static_cast<ID3D11Texture2D*>(frame->hudless); packet->depth=static_cast<ID3D11Texture2D*>(frame->depth); packet->motion=static_cast<ID3D11Texture2D*>(frame->motion);
    std::lock_guard<std::mutex> lock(packetsLock);
    auto id=nextPacket++; packets.emplace(id,std::move(packet));
    return reinterpret_cast<void*>(id);
}
FG_EXPORT void __cdecl NSFG_DiscardPacket(void* handle) {
    std::lock_guard<std::mutex> lock(packetsLock); packets.erase(reinterpret_cast<uintptr_t>(handle));
}
FG_EXPORT void __cdecl NSFG_GetStatus(FGStatus* output,char* message,uint32_t capacity) {
    std::lock_guard<std::mutex> lock(fgStatusLock);
    if(output && output->size==sizeof(FGStatus)) { *output=fgStatus; output->submitted=fgSubmitted.load(); }
    if(message && capacity) strncpy_s(message,capacity,fgMessage.c_str(),_TRUNCATE);
}
