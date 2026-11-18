// Native scene post processing. The integration owner selects the main scene explicitly;
// this module never guesses whether an engine render target is the main scene or a mirror.
#pragma once
#include "postprocess.h"
#include "SdlGpuFrame.h"
#include "SdlPostOcclusion.h"
#include <string>

struct SdlPostSettings
{
    DX11PostEffect Chain[DX11_POST_CHAIN_LIMIT] = {};
    unsigned Count = 0; // zero is off; invalid/unsupported chains refuse before rerouting
    float BloomThreshold = PostProcess::BLOOM_THRESHOLD;
    float BloomIntensity = PostProcess::BLOOM_INTENSITY;
    float BloomSaturation = 1.0f;
    float Clarity = 0.0f;
    float AdditiveGain = 1.0f;
    unsigned BlurPasses = PostProcess::BLOOM_BLUR_PASSES;
    float Grade = 0.0f;
    bool InternalHDR = false;
    uint64_t MainViewKey = 0; // caller-owned main view epoch; zero preserves standalone API compatibility
    unsigned ActualSamples = 1; // actual attachment samples, never the requested option
};

struct SdlPostResources
{
    unsigned Textures = 0, Shaders = 0, Pipelines = 0, Samplers = 0;
    unsigned long long Creations = 0;
};

class SdlPostProcess
{
public:
    SdlPostProcess();
    ~SdlPostProcess();
    SdlPostProcess(const SdlPostProcess &) = delete;
    SdlPostProcess &operator=(const SdlPostProcess &) = delete;

    // All required allocations/programs/pipelines are ready before scene differs from presentation.
    // False leaves scene==presentation, Active()==false and Additive_Gain()==1, with a diagnostic.
    // Off succeeds with the presentation route. Only active routes require actual single-sample attachments.
    // The caller clears/draws the returned scene and calls Finish before any health bars or HUD.
    // Scene depth is borrowed from presentation at identical dimensions/samples and is never cleared
    // or written by a post pass. Neither method changes the engine's render state or frame target.
    bool Prepare(SdlGpuFrame &frame, const SdlTarget &presentation, const SdlPostSettings &settings,
        SdlTarget &scene);
    // Unperformed chain AO requires fresh BeforePost inputs at its requested chain positions.
    // Missing/stale input bypasses those stages with a diagnostic while preserving presentation.
    // Final AO preserves incoming chain-source alpha; in-place AO preserves scene destination alpha.
    bool Finish(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &presentation,
        const SdlPostSceneInputs *beforePost = nullptr);
    bool Needs_BeforePost_Depth() const;
    // OpaqueScene world AO precedes water; BeforeTransparent chain AO precedes particles.
    // Both require an active, unresolved prepared main scene; chain AO additionally requires "ao".
    // Successful early occlusion is shared across phases and suppresses every final-chain AO entry.
    bool Apply_World_AO(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    bool Apply_Chain_AO(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    // Frame abandonment/menu fallback: copy an eight-bit scene, or legacy tone-map HDR without
    // grade/bloom/wide mapping, matching DX11 Copy_Through. Prepared before scene rerouting.
    // One exact armed normal Finish that fails also authorizes one recovery copy.
    // Copy authorization is consumed on a validated attempt; success/failure never grants
    // positive Scene_Has_Finished proof, and stale/repeated copies are refused.
    bool Copy_Through(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &presentation);

    // Flushes queued borrowers before releasing programs/pipelines/samplers; textures retire safely.
    // Must be called before destroying the owning frame. On flush failure retains all resources.
    bool Release(SdlGpuFrame &frame);
    bool Active() const;
    bool Scene_Is_Unresolved(SdlGpuFrame &frame, const SdlTarget &scene) const;
    bool Scene_Matches_Identity(SdlGpuFrame &frame, const SdlTarget &scene,
        const SdlSceneCaptureRequest &identity) const;
    // Positive proof of completed normal Finish, cleared by preparation/failed attempts/copy-through.
    bool Scene_Has_Finished(SdlGpuFrame &frame, const SdlTarget &scene,
        const SdlTarget &presentation, const SdlSceneCaptureRequest &identity) const;
    bool Scene_Is_Float() const;
    unsigned Scene_Colour_Format() const;
    float Additive_Gain() const;
    bool Replaces_Legacy_Bloom() const;
    const std::string &Error() const;
    SdlPostResources Resources() const;
    enum class TestFailure { None, Prepare, Shader, Pipeline, Target, TargetAfterOne };
    void Set_Failure_For_Test(TestFailure failure);
    void Set_AO_Failure_For_Test(SdlPostOcclusion::TestFailure failure);

private:
    struct Impl;
    Impl *State;
};
