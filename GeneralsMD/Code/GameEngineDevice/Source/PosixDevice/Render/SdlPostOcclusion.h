// Caller-owned scene evidence for native post occlusion. No capture acquisition lives here.
// SampledDepth and Shroud must remain alive and unchanged through the recorded batch submission;
// replacements retire through frame.Release_After_Batch. Separate phase captures must not overwrite
// depth already borrowed by earlier queued passes. No earlier-phase depth is retained for Finish.
#pragma once
#include "SdlGpuFrame.h"
#include <string>

struct SdlPostSceneInputs
{
    SdlSceneCaptureRequest Capture;
    SDL_GPUTexture *SampledDepth = nullptr; // independent normalized D3D [0,1] R32_FLOAT image
    unsigned DepthWidth = 0, DepthHeight = 0;
    unsigned DepthFormat = 0; // explicitly SDL_GPU_TEXTUREFORMAT_R32_FLOAT
    unsigned ActualSamples = 1;
    SDL_GPUTexture *Shroud = nullptr; // null means fully visible; never sample a null descriptor
    float WorldFromClip[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // row major
    float ProjectionScale[2] = {1,1};
    float ShroudRect[4] = {0,0,1,1}; // world origin.xy, inverse extent.xy
    float Near = 1, Far = 1200;
};

struct SdlPostOcclusionResources
{
    unsigned Textures = 0, Shaders = 0, Pipelines = 0, Samplers = 0;
    unsigned long long Creations = 0;
};

// Internal post module helper. SdlPostProcess supplies its active/armed main-scene policy;
// this class validates the explicit evidence and owns only its shaders/intermediate images.
class SdlPostOcclusion
{
public:
    SdlPostOcclusion();
    ~SdlPostOcclusion();
    SdlPostOcclusion(const SdlPostOcclusion &) = delete;
    SdlPostOcclusion &operator=(const SdlPostOcclusion &) = delete;
    // Capture owner policy; unchanged keys preserve an existing same-scene early result.
    void Prepare(uint64_t expectedViewKey = 0);
    bool Prepare_Final(SdlGpuFrame &frame, unsigned outputFormat);
    bool Apply_World(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    bool Apply_Chain(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    bool Filter_Final(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &source,
        const SdlTarget &destination, const SdlPostSceneInputs &inputs);
    bool Was_Occluded(SdlGpuFrame &frame, const SdlTarget &scene) const;
    void Abandon();
    bool Release(SdlGpuFrame &frame);
    const std::string &Error() const;
    SdlPostOcclusionResources Resources() const;
    // Per-instance injection: mutating process environment after the GPU starts races driver getenv.
    enum class TestFailure { None, Program, Pipeline, Target, TargetAfterOne };
    void Set_Failure_For_Test(TestFailure failure);
private:
    struct Impl;
    Impl *State;
};
