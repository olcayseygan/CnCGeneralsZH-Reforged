// Native distortion consumes explicit main-scene ownership. It never captures
// opaque/water images or changes engine render state. All coordinates cover the
// full texture: warp centres are top-left UV, radii use texture height and aspect
// is texture width/height; heat shape is full-texture clip space (Y up). A caller
// drawing into an offset/sub-sized world viewport must map its projected records
// into this texture space before Prepare. The module consumes no viewport state.
#pragma once
#include "SdlPostProcess.h"
#include "postshader.h"

// Mechanical SDL descriptor/varying adaptation of the shared DX11 source.
bool Sdl_Post_Distortion_Pixel_Source(PostShaderProgram program, std::string &source);
bool Sdl_Post_Heat_Vertex_Source(std::string &source);

struct SdlPostDistortionResources : SdlPostResources
{
    unsigned Buffers = 0;
    uint64_t BufferBytes = 0;
};

class SdlPostDistortion
{
public:
    SdlPostDistortion();
    ~SdlPostDistortion();
    SdlPostDistortion(const SdlPostDistortion &) = delete;
    SdlPostDistortion &operator=(const SdlPostDistortion &) = delete;

    // Call while the explicit main scene is unresolved. All programs/images are
    // ready before Apply can record visible work. Records are copied, limit4.
    // Count0 clears the borrow and records; it does not enable a scene route.
    bool Prepare_Blast_Warps(SdlGpuFrame &frame, const SdlPostProcess &post,
        const SdlTarget &scene, const SdlTarget &output, const SdlSceneCaptureRequest &main,
        const DX11PostWarp *warps, unsigned count);
    // Caller invokes only after successful post.Finish, before health bars/HUD.
    // Takes an independent same-format colour snapshot at this operation, then
    // bends it into output. Never samples the active destination or touches depth.
    // Output alpha is preserved; original RGB formulas and border settling are unchanged.
    bool Apply_Blast_Warp(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &output);
    // Heat records retain the DX11 four-float4 layout (limit2048), supplied as
    // native instance vertex attributes rather than a later storage subsystem. Copied
    // at Prepare. Caller applies user-chain eligibility even for neutral COPY
    // scenes owned by another consumer. Count0 clears heat without affecting blast.
    bool Prepare_Heat(SdlGpuFrame &frame, const SdlPostProcess &post, const SdlTarget &scene,
        const SdlSceneCaptureRequest &main, const float *records, unsigned count);
    // Before normal Finish: fresh independent BeforePost R32 depth can also be
    // supplied readonly to final-chain AO. Every attempt consumes heat records.
    // Composite preserves current scene alpha/depth and snapshots its own colour.
    bool Apply_Heat(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &beforePost);
    enum class TestFailure { None, Program, Pipeline, Buffer, Target, TargetAfterOne, Record };
    void Set_Failure_For_Test(TestFailure failure);
    void Abandon();
    // Retains all resources on failed submission with queued CPU readers.
    // Release before destroying the owning frame; it must outlive this module.
    bool Release(SdlGpuFrame &frame);
    const std::string &Error() const;
    SdlPostDistortionResources Resources() const;
private:
    struct Impl;
    Impl *State;
};
