#pragma once
#include <cstdint>
#ifdef _WIN32
#define NS_EXPORT extern "C" __declspec(dllexport)
#else
#define NS_EXPORT extern "C"
#endif

// Versioned, naturally aligned x64 ABI shared with DlssRenderer.cs.
struct NSFrame
{
    uint32_t size, version;
    void* color;
    void* depth;
    void* motion;
    void* output;
    uint32_t inputWidth, inputHeight, outputWidth, outputHeight;
    int32_t quality, reset, reversedDepth, hdr;
    float jitterX, jitterY, motionScaleX, motionScaleY, deltaMs;
    uint32_t reserved;
};
NS_EXPORT void* __cdecl NS_CreatePacket(const NSFrame* frame);
NS_EXPORT void* __cdecl NS_GetRenderEvent();
NS_EXPORT void __cdecl NS_GetStatus(char* buffer, uint32_t capacity, uint64_t* successfulFrames, uint32_t* lastResult);
NS_EXPORT void __cdecl NS_ReleasePacket(void* packet);
NS_EXPORT void __cdecl NS_EvaluateNow(const NSFrame* frame);
NS_EXPORT void __cdecl NS_ShutdownNow();
