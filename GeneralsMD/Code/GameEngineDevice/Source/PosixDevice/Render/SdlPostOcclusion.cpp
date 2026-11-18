#include "SdlPostOcclusion.h"
#include "SdlPostShader.h"
#include "postprocess.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
unsigned format_slot(unsigned format)
{
    if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) return 0;
    if (format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) return 1;
    if (format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) return 2;
    return format == SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT ? 3 : 4;
}
bool same_scene(const SdlSceneCaptureRequest &a, const SdlSceneCaptureRequest &b)
{
    // Phase is validated per operation; world and chain AO share one occlusion result.
    SdlSceneCaptureRequest left = a, right = b;
    left.Phase = right.Phase = SdlSceneCapturePhase::OpaqueScene;
    return left.Same_Identity(right);
}
SdlSceneCaptureRequest identity(SdlGpuFrame &frame, const SdlTarget &scene, uint64_t viewKey)
{
    SdlSceneCaptureRequest result;
    result.Source = scene; result.ViewKey = viewKey;
    result.Source.ColourFormat = SdlGpuFrame::Target_Colour_Format(scene);
    result.Source.DepthFormat = frame.Target_Depth_Format(scene);
    result.Frame = frame.Scene_Frame(); result.Generation = frame.Scene_Generation();
    return result;
}
SdlTarget colour_target(SDL_GPUTexture *texture, unsigned w, unsigned h, unsigned format)
{
    return {texture, nullptr, w, h, 0, format};
}
}

struct SdlPostOcclusion::Impl
{
    enum Pass { World, Blur, Upsample, Chain, Final, Count };
    static PostShaderProgram shader(Pass pass)
    {
        static const PostShaderProgram ids[] = {POST_SHADER_WORLD_AO, POST_SHADER_WORLD_AO_BLUR,
            POST_SHADER_WORLD_AO_UPSAMPLE, POST_SHADER_AO_SCENE, POST_SHADER_AO};
        return ids[pass];
    }
    struct Program {
        SDL_GPUShader *Shader = nullptr;
        SDL_GPUGraphicsPipeline *Pipelines[4] = {};
        unsigned Samplers = 0, Uniforms = 0;
    } Programs[Count];
    struct Images {
        unsigned Width = 0, Height = 0;
        SDL_GPUTexture *World[2] = {}, *Copy[3] = {};
    } Targets;
    SdlGpuFrame *Owner = nullptr;
    SDL_GPUDevice *Device = nullptr;
    SDL_GPUShader *Vertex = nullptr;
    SDL_GPUSampler *Sampler = nullptr;
    SDL_GPUTexture *White = nullptr;
    SdlSceneCaptureRequest Applied;
    bool Occluded = false;
    uint64_t ExpectedViewKey = 0;
    std::string LastError;
    unsigned long long Creations = 0;
    TestFailure FailureForTest = TestFailure::None;
    bool fault(TestFailure point) const { return FailureForTest == point; }

    bool fail(const std::string &why)
    {
        const std::string text = "native SDL AO: " + why;
        if (LastError != text) std::fprintf(stderr, "%s\n", text.c_str());
        LastError = text; return false;
    }
    bool owner(SdlGpuFrame &frame)
    {
        if (Owner && Owner != &frame) return fail("Release the old frame owner before rebinding");
        Owner = &frame; Device = frame.Device(); return true;
    }
    bool valid(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs,
        SdlSceneCapturePhase phase)
    {
        if (!owner(frame)) return false;
        SdlSceneCaptureRequest source = inputs.Capture;
        source.Source.ColourFormat = SdlGpuFrame::Target_Colour_Format(source.Source);
        source.Source.DepthFormat = frame.Target_Depth_Format(source.Source);
        if (!scene.Colour || !scene.Depth || !scene.Width || !scene.Height || format_slot(SdlGpuFrame::Target_Colour_Format(scene)) > 2
            || inputs.Capture.Phase != phase || !inputs.Capture.Depth || !same_scene(source, identity(frame, scene, ExpectedViewKey)))
            return fail("capture phase/target/dimensions/frame/generation does not match this scene");
        if (!inputs.SampledDepth || inputs.SampledDepth == scene.Colour || inputs.SampledDepth == scene.Depth
            || inputs.DepthWidth != scene.Width || inputs.DepthHeight != scene.Height
            || inputs.DepthFormat != SDL_GPU_TEXTUREFORMAT_R32_FLOAT || inputs.ActualSamples != 1
            || inputs.Shroud == scene.Colour || inputs.Shroud == scene.Depth)
            return fail("requires independent matching single-sample R32 normalized depth and nonaliasing shroud");
        if (!std::isfinite(inputs.Near) || !std::isfinite(inputs.Far) || inputs.Near <= 0 || inputs.Far <= inputs.Near)
            return fail("invalid near/far planes");
        for (float value : inputs.WorldFromClip) if (!std::isfinite(value)) return fail("non-finite clip-to-world matrix");
        for (float value : inputs.ShroudRect) if (!std::isfinite(value)) return fail("non-finite shroud rectangle");
        for (float value : inputs.ProjectionScale) if (!std::isfinite(value) || value <= 0) return fail("invalid projection scale");
        return true;
    }
    bool program(Pass pass, unsigned format)
    {
        if (fault(TestFailure::Program)) return fail("injected AO program failure");
        const unsigned slot = format_slot(format);
        if (slot > 3) return fail("unsupported AO output format");
        std::string source, error;
        std::vector<unsigned char> spirv;
        if (!Vertex) {
            if (!Sdl_Post_Vertex_Source(source) || !SDL3_Compile_HLSL_To_SPIRV(source, true, spirv, error)
                || !(Vertex = SDL3_Create_Shader(Device, spirv, true, error))) return fail("AO vertex program: " + error);
            ++Creations;
        }
        if (!Sampler) {
            SDL_GPUSamplerCreateInfo info = {};
            info.min_filter = info.mag_filter = SDL_GPU_FILTER_LINEAR;
            info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            info.max_lod = 1000;
            Sampler = SDL_CreateGPUSampler(Device, &info);
            if (!Sampler) return fail(std::string("AO sampler: ") + SDL_GetError());
            ++Creations;
        }
        Program &p = Programs[pass];
        if (!p.Shader) {
            if (!Sdl_Post_Pixel_Source(shader(pass), source) || !SDL3_Compile_HLSL_To_SPIRV(source, false, spirv, error)
                || !(p.Shader = SDL3_Create_Shader(Device, spirv, false, error))) return fail("AO pixel program: " + error);
            SDL3_Shader_Slots(spirv, false, p.Samplers, p.Uniforms);
            ++Creations;
            if (p.Samplers > 3 || p.Uniforms > 2) return fail("AO shader exceeds explicit descriptor/uniform bindings");
        }
        if (fault(TestFailure::Pipeline)) return fail("injected AO pipeline failure");
        if (!p.Pipelines[slot]) {
            SDL_GPUColorTargetDescription colour = {};
            colour.format = SDL_GPUTextureFormat(format);
            if (pass == Upsample || pass == Chain) {
                colour.blend_state.enable_blend = true;
                colour.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
                colour.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
                colour.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
                colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            if (pass == Final || pass == Upsample || pass == Chain) {
                // In-place passes preserve destination alpha bits. For final AO the source is
                // copied first; keep that defined alpha while the exact original
                // final-AO program writes RGB. No shader formula or depth write is changed.
                colour.blend_state.enable_color_write_mask = true;
                colour.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            }
            SDL_GPUGraphicsPipelineCreateInfo info = {};
            info.vertex_shader = Vertex; info.fragment_shader = p.Shader;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.rasterizer_state.enable_depth_clip = true;
            info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            info.target_info.num_color_targets = 1; info.target_info.color_target_descriptions = &colour;
            p.Pipelines[slot] = SDL_CreateGPUGraphicsPipeline(Device, &info);
            if (!p.Pipelines[slot]) return fail(std::string("AO pipeline: ") + SDL_GetError());
            ++Creations;
        }
        return true;
    }
    SDL_GPUTexture *texture(unsigned w, unsigned h, unsigned format)
    {
        if (fault(TestFailure::Target)) { fail("injected AO target failure"); return nullptr; }
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPUTextureFormat(format);
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = w; info.height = h; info.layer_count_or_depth = info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *result = SDL_CreateGPUTexture(Device, &info);
        if (result) ++Creations; else fail(std::string("AO allocation: ") + SDL_GetError());
        return result;
    }
    void dispose(Images &images, bool recorded)
    {
        SDL_GPUTexture *all[] = {images.World[0], images.World[1], images.Copy[0], images.Copy[1], images.Copy[2]};
        for (SDL_GPUTexture *image : all) if (image) {
            if (recorded) Owner->Release_After_Batch(image, nullptr);
            else SDL_ReleaseGPUTexture(Device, image);
        }
        images = {};
    }
    bool images(const SdlTarget &scene, bool world)
    {
        const bool replace = Targets.Width != scene.Width || Targets.Height != scene.Height;
        Images next = replace ? Images{} : Targets;
        next.Width = scene.Width; next.Height = scene.Height;
        std::vector<SDL_GPUTexture *> created;
        auto ensure = [&](SDL_GPUTexture *&image, unsigned w, unsigned h, unsigned format) {
            if (image) return true;
            if (fault(TestFailure::TargetAfterOne) && created.size() == 1) return fail("injected AO partial-allocation failure");
            image = texture(w, h, format);
            if (image) created.push_back(image);
            return image != nullptr;
        };
        bool ok = true;
        if (world) for (unsigned i = 0; ok && i < 2; ++i)
            ok = ensure(next.World[i], (scene.Width + 1) / 2, (scene.Height + 1) / 2, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT);
        else {
            const unsigned format = SdlGpuFrame::Target_Colour_Format(scene);
            ok = ensure(next.Copy[format_slot(format)], scene.Width, scene.Height, format);
        }
        if (!ok) {
            for (SDL_GPUTexture *image : created) SDL_ReleaseGPUTexture(Device, image);
            return false;
        }
        if (replace) dispose(Targets, true);
        Targets = next;
        return true;
    }
    bool white()
    {
        if (White) return true;
        White = texture(1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        if (!White) return false;
        uint32_t offset = 0;
        uint8_t *bytes = Owner->Upload_Space(4, offset);
        if (!bytes) return fail("AO white shroud upload allocation failed");
        std::memset(bytes, 255, 4);
        Owner->Queue_Texture_Upload(White, 0, 1, 1, offset);
        return true;
    }
    bool pass(Pass id, const SdlTarget &destination, SDL_GPUTexture *source, unsigned width, unsigned height,
        const SdlPostSceneInputs &inputs, float blurX = 0, float blurY = 0)
    {
        const Program &p = Programs[id];
        SdlRecordedDraw draw = {};
        draw.Pipeline = p.Pipelines[format_slot(SdlGpuFrame::Target_Colour_Format(destination))];
        draw.Count = 3;
        draw.Viewport[2] = float(destination.Width); draw.Viewport[3] = float(destination.Height); draw.Viewport[5] = 1;
        draw.Scissor[2] = int(destination.Width); draw.Scissor[3] = int(destination.Height);
        draw.SamplerCount = p.Samplers;
        draw.Textures[0] = source; draw.Textures[1] = inputs.SampledDepth;
        draw.Textures[2] = inputs.Shroud ? inputs.Shroud : White;
        for (unsigned i = 0; i < p.Samplers; ++i) {
            if (!draw.Textures[i] || draw.Textures[i] == destination.Colour) return fail("AO pass resource aliases its output");
            draw.Samplers[i] = Sampler;
        }
        PostProcessConstants constants = {};
        constants.TexelSize[0] = 1.0f / width; constants.TexelSize[1] = 1.0f / height;
        constants.TexelSize[2] = float(width); constants.TexelSize[3] = float(height);
        constants.BlurDirection[0] = blurX; constants.BlurDirection[1] = blurY;
        constants.Tuning[0] = inputs.Near; constants.Tuning[1] = inputs.Far;
        constants.Tuning[2] = PostProcess::AO_RADIUS; constants.Tuning[3] = PostProcess::AO_SPREAD;
        std::memcpy(constants.WorldFromClip, inputs.WorldFromClip, sizeof(constants.WorldFromClip));
        constants.HazeSun[2] = inputs.Shroud ? 1 : 0;
        std::memcpy(constants.HazeShroudRect, inputs.ShroudRect, sizeof(constants.HazeShroudRect));
        std::memcpy(constants.WorldAoProjection, inputs.ProjectionScale, sizeof(inputs.ProjectionScale));
        // Both buffers name the complete 352-byte object. b1 retains c15..c21 packoffsets.
        const SdlNativeUniformBlock blocks[] = {{1,0,&constants,sizeof(constants)}, {1,1,&constants,sizeof(constants)}};
        return Owner->Record_Native_Draw(destination, draw, blocks, p.Uniforms) || fail("AO native draw refused");
    }
};

SdlPostOcclusion::SdlPostOcclusion() : State(new Impl) {}
void SdlPostOcclusion::Set_Failure_For_Test(TestFailure failure) { State->FailureForTest = failure; }
SdlPostOcclusion::~SdlPostOcclusion()
{
    if (State->Owner && !Release(*State->Owner)) std::fprintf(stderr, "native SDL AO: queued resources retained until device teardown\n");
    delete State;
}
bool SdlPostOcclusion::Prepare_Final(SdlGpuFrame &frame, unsigned outputFormat)
{
    return State->owner(frame) && State->program(Impl::Final, outputFormat);
}
bool SdlPostOcclusion::Was_Occluded(SdlGpuFrame &frame, const SdlTarget &scene) const
{
    return State->Occluded && State->Owner == &frame && same_scene(State->Applied, identity(frame, scene, State->ExpectedViewKey));
}
bool SdlPostOcclusion::Apply_World(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs)
{
    Impl &s = *State;
    if (!s.valid(frame, scene, inputs, SdlSceneCapturePhase::OpaqueScene) || Was_Occluded(frame, scene)) return false;
    const unsigned format = SdlGpuFrame::Target_Colour_Format(scene);
    if (!s.program(Impl::World, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT)
        || !s.program(Impl::Blur, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT) || !s.program(Impl::Upsample, format)
        || !s.images(scene, true) || !s.white()) return false;
    const unsigned w = (scene.Width + 1) / 2, h = (scene.Height + 1) / 2;
    if (!s.pass(Impl::World, colour_target(s.Targets.World[0], w, h, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT),
            inputs.SampledDepth, scene.Width, scene.Height, inputs)
        || !s.pass(Impl::Blur, colour_target(s.Targets.World[1], w, h, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT),
            s.Targets.World[0], w, h, inputs, 1, 0)
        || !s.pass(Impl::Blur, colour_target(s.Targets.World[0], w, h, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT),
            s.Targets.World[1], w, h, inputs, 0, 1)
        || !s.pass(Impl::Upsample, colour_target(scene.Colour, scene.Width, scene.Height, format),
            s.Targets.World[0], w, h, inputs)) return false;
    s.Applied = identity(frame, scene, s.ExpectedViewKey); s.Occluded = true; s.LastError.clear(); return true;
}
bool SdlPostOcclusion::Apply_Chain(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs)
{
    Impl &s = *State;
    if (!s.valid(frame, scene, inputs, SdlSceneCapturePhase::BeforeTransparent) || Was_Occluded(frame, scene)) return false;
    const unsigned format = SdlGpuFrame::Target_Colour_Format(scene);
    if (!s.program(Impl::Chain, format) || !s.images(scene, false) || !s.white()) return false;
    SDL_GPUTexture *copy = s.Targets.Copy[format_slot(format)];
    const int32_t rectangle[] = {0,0,int32_t(scene.Width),int32_t(scene.Height)};
    frame.Record_Blit(scene.Colour, rectangle, copy, rectangle, false);
    if (!s.pass(Impl::Chain, colour_target(scene.Colour, scene.Width, scene.Height, format),
        copy, scene.Width, scene.Height, inputs)) return false;
    s.Applied = identity(frame, scene, s.ExpectedViewKey); s.Occluded = true; s.LastError.clear(); return true;
}
bool SdlPostOcclusion::Filter_Final(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &source,
    const SdlTarget &destination, const SdlPostSceneInputs &inputs)
{
    Impl &s = *State;
    if (!s.valid(frame, scene, inputs, SdlSceneCapturePhase::BeforePost)) return false;
    if (source.Width != scene.Width || source.Height != scene.Height || destination.Width != scene.Width
        || destination.Height != scene.Height || source.Colour == destination.Colour
        || destination.Colour == inputs.SampledDepth || destination.Colour == inputs.Shroud)
        return s.fail("final AO targets do not match the explicit scene or alias inputs");
    if (!s.program(Impl::Final, SdlGpuFrame::Target_Colour_Format(destination))) return false;
    // A deterministic source alpha precedes RGB-only AO, including intermediate chain destinations.
    const int32_t rectangle[] = {0,0,int32_t(scene.Width),int32_t(scene.Height)};
    frame.Record_Blit(source.Colour, rectangle, destination.Colour, rectangle, false);
    return s.pass(Impl::Final, colour_target(destination.Colour, destination.Width, destination.Height,
        SdlGpuFrame::Target_Colour_Format(destination)), source.Colour, source.Width, source.Height, inputs);
}
void SdlPostOcclusion::Prepare(uint64_t expectedViewKey)
{
    if (State->ExpectedViewKey != expectedViewKey) Abandon();
    State->ExpectedViewKey = expectedViewKey;
}
void SdlPostOcclusion::Abandon() { State->Occluded = false; State->Applied = {}; }
bool SdlPostOcclusion::Release(SdlGpuFrame &frame)
{
    Impl &s = *State;
    if (!s.Owner) return true;
    if (s.Owner != &frame || (frame.Has_Pending_Work() && !frame.Flush())) return s.fail("AO release requires its owner and a submitted batch");
    s.dispose(s.Targets, false);
    if (s.White) SDL_ReleaseGPUTexture(s.Device, s.White);
    if (s.Sampler) SDL_ReleaseGPUSampler(s.Device, s.Sampler);
    for (auto &p : s.Programs) {
        for (auto *pipeline : p.Pipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(s.Device, pipeline);
        if (p.Shader) SDL_ReleaseGPUShader(s.Device, p.Shader);
        p = {};
    }
    if (s.Vertex) SDL_ReleaseGPUShader(s.Device, s.Vertex);
    s.White = nullptr; s.Sampler = nullptr; s.Vertex = nullptr; s.Owner = nullptr; s.Device = nullptr;
    Abandon(); return true;
}
const std::string &SdlPostOcclusion::Error() const { return State->LastError; }
SdlPostOcclusionResources SdlPostOcclusion::Resources() const
{
    const Impl &s = *State;
    SdlPostOcclusionResources result;
    result.Textures = s.White != nullptr; result.Shaders = s.Vertex != nullptr; result.Samplers = s.Sampler != nullptr;
    for (auto *texture : s.Targets.World) result.Textures += texture != nullptr;
    for (auto *texture : s.Targets.Copy) result.Textures += texture != nullptr;
    for (const auto &p : s.Programs) {
        result.Shaders += p.Shader != nullptr;
        for (auto *pipeline : p.Pipelines) result.Pipelines += pipeline != nullptr;
    }
    result.Creations = s.Creations; return result;
}
