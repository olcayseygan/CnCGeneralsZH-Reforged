// A by-value native particle draw context. Core code validates view/resource identity;
// providers may record through Frame's public immutable-packet API, never mutable frame internals.
#pragma once
#include "SdlGpuFrame.h"
#include <string>
struct IDirect3DBaseTexture9;
struct IDirect3DDevice9;
enum class SdlGpuParticlesPass { MainScene, Reflection, Sun, Other };
struct SdlGpuParticleTextureView {
    SDL_GPUTexture *Texture = nullptr;
    uint64_t Identity = 0, Generation = 0;
    unsigned Width = 0, Height = 0, MipLevels = 0;
};
struct SdlGpuParticlesContext {
    SdlGpuFrame *Frame = nullptr;
    uint64_t DeviceGeneration = 0, FrameSerial = 0, MainViewKey = 0;
    SdlTarget Target = {};
    float Viewport[6] = {};
    int32_t Scissor[4] = {};
    SdlGpuParticlesPass Pass = SdlGpuParticlesPass::Other;
    float AdditiveGain = 1;
    SDL_GPUTexture *SoftDepth = nullptr;
    bool SoftAllowed = false;
    SdlSceneCaptureRequest SoftCapture;
    bool (*MirrorTexture)(IDirect3DBaseTexture9 *, SdlGpuParticleTextureView &, std::string &) = nullptr;
};
// False without a live GPU. Admission/Prepare queries are valid outside a main-scene draw;
// the provider classifies/restricts Draw using Pass and actual Target. No capability recursion.
bool PosixGpuParticles_Context(SdlGpuParticlesContext &context);

// Lower-layer overload; does not consult the engine singleton or capability query.
bool PosixGpuParticles_Context(IDirect3DDevice9 *, SdlGpuParticlesContext &context);
