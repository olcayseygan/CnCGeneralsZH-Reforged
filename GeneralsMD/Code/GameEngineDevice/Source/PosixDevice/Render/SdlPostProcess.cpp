#include "SdlPostProcess.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned FLOAT_FORMAT = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
constexpr unsigned CLARITY_BLUR_PASSES = 2;
bool same_target(const SdlTarget &a, const SdlTarget &b)
{
    return a.Colour == b.Colour && a.Depth == b.Depth && a.Width == b.Width && a.Height == b.Height
        && a.ColourFormat == b.ColourFormat && a.DepthFormat == b.DepthFormat;
}
unsigned format_slot(unsigned format)
{
    if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) return 0;
    if (format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) return 1;
    return format == FLOAT_FORMAT ? 2 : 3;
}
}

struct SdlPostProcess::Impl
{
    struct Program {
        SDL_GPUShader *Shader = nullptr;
        unsigned Samplers = 0, Uniforms = 0;
        SDL_GPUGraphicsPipeline *Pipelines[3] = {};
    };
    struct Images {
        unsigned Width = 0, Height = 0, Format = 0;
        SDL_GPUTexture *Scene8 = nullptr, *SceneHDR = nullptr;
        SDL_GPUTexture *Chain[2] = {}, *Bloom[2] = {};
    };
    SdlGpuFrame *Owner = nullptr;
    SDL_GPUDevice *Device = nullptr;
    SDL_GPUShader *Vertex = nullptr;
    SDL_GPUSampler *Sampler = nullptr;
    Program Programs[POST_SHADER_COUNT];
    Images Targets;
    SdlPostSettings Settings;
    SdlPostOcclusion Occlusion;
    SdlTarget Scene = {}, Presentation = {};
    uint64_t Frame = 0, Generation = 0;
    bool Enabled = false, Armed = false, Finished = false, FailedFinish = false;
    std::string LastError;
    unsigned long long Creations = 0;
    TestFailure FailureForTest = TestFailure::None;
    bool fault(TestFailure point) const { return FailureForTest == point; }

    bool fail(const std::string &reason)
    {
        const std::string text = "native SDL post: " + reason;
        if (text != LastError) std::fprintf(stderr, "%s\n", text.c_str());
        LastError = text;
        return false;
    }
    SDL_GPUTexture *texture(unsigned width, unsigned height, unsigned format)
    {
        const auto usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (fault(TestFailure::Target) || !SDL_GPUTextureSupportsFormat(Device, SDL_GPUTextureFormat(format),
            SDL_GPU_TEXTURETYPE_2D, usage)) {
            fail("unsupported target format or injected target failure"); return nullptr;
        }
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPUTextureFormat(format);
        info.usage = usage; info.width = width; info.height = height;
        info.layer_count_or_depth = info.num_levels = 1; info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *result = SDL_CreateGPUTexture(Device, &info);
        if (result) ++Creations;
        else fail(std::string("target allocation: ") + SDL_GetError());
        return result;
    }
    void dispose_images(Images &images, bool recorded)
    {
        SDL_GPUTexture *all[] = {images.Scene8, images.SceneHDR, images.Chain[0], images.Chain[1],
            images.Bloom[0], images.Bloom[1]};
        for (SDL_GPUTexture *image : all) if (image) {
            if (recorded) Owner->Release_After_Batch(image, nullptr);
            else SDL_ReleaseGPUTexture(Device, image);
        }
        images = {};
    }
    bool programs(const std::vector<PostShaderProgram> &needed, unsigned presentationFormat)
    {
        if (fault(TestFailure::Shader)) return fail("injected shader preparation failure");
        std::string source, log;
        std::vector<unsigned char> spirv;
        if (!Vertex) {
            if (!Sdl_Post_Vertex_Source(source)
                || !SDL3_Compile_HLSL_To_SPIRV(source, true, spirv, log)
                || !(Vertex = SDL3_Create_Shader(Device, spirv, true, log)))
                return fail("fullscreen vertex program: " + log);
            ++Creations;
        }
        if (!Sampler) {
            SDL_GPUSamplerCreateInfo info = {};
            info.min_filter = info.mag_filter = SDL_GPU_FILTER_LINEAR;
            info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            info.max_lod = 1000.0f;
            Sampler = SDL_CreateGPUSampler(Device, &info);
            if (!Sampler) return fail(std::string("bilinear clamp sampler: ") + SDL_GetError());
            ++Creations;
        }
        for (PostShaderProgram id : needed) {
            Program &program = Programs[id];
            if (!program.Shader) {
                if (!Sdl_Post_Pixel_Source(id, source)
                    || !SDL3_Compile_HLSL_To_SPIRV(source, false, spirv, log)
                    || !(program.Shader = SDL3_Create_Shader(Device, spirv, false, log)))
                    return fail("pixel program " + std::to_string(unsigned(id)) + ": " + log);
                SDL3_Shader_Slots(spirv, false, program.Samplers, program.Uniforms);
                ++Creations;
                if (program.Samplers > 2 || program.Uniforms > 1)
                    return fail("base post program exceeds its declared native binding contract");
            }
            const unsigned format = id == POST_SHADER_BLOOM_EXTRACT || id == POST_SHADER_BLOOM_BLUR
                ? FLOAT_FORMAT : presentationFormat;
            SDL_GPUGraphicsPipeline *&pipeline = program.Pipelines[format_slot(format)];
            if (!pipeline) {
                if (fault(TestFailure::Pipeline)) return fail("injected pipeline failure");
                SDL_GPUColorTargetDescription colour = {};
                colour.format = SDL_GPUTextureFormat(format);
                SDL_GPUGraphicsPipelineCreateInfo info = {};
                info.vertex_shader = Vertex; info.fragment_shader = program.Shader;
                info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
                info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
                info.rasterizer_state.enable_depth_clip = true;
                info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
                info.target_info.num_color_targets = 1;
                info.target_info.color_target_descriptions = &colour;
                pipeline = SDL_CreateGPUGraphicsPipeline(Device, &info);
                if (!pipeline) return fail(std::string("post pipeline: ") + SDL_GetError());
                ++Creations;
            }
        }
        return true;
    }
    bool images(unsigned width, unsigned height, unsigned format, bool hdr, bool bloom)
    {
        const bool replace = Targets.Width != width || Targets.Height != height || Targets.Format != format;
        Images next = replace ? Images{} : Targets;
        next.Width = width; next.Height = height; next.Format = format;
        // New images are kept separately until the whole transaction succeeds. A refusal retains
        // the previous usable set and returns the presentation route, never a half-prepared scene.
        std::vector<SDL_GPUTexture *> created;
        auto ensure = [&](SDL_GPUTexture *&image, unsigned w, unsigned h, unsigned f) {
            if (image) return true;
            if (fault(TestFailure::TargetAfterOne) && created.size() == 1)
                return fail("injected failure after the first transactional target allocation");
            image = texture(w, h, f);
            if (image) created.push_back(image);
            return image != nullptr;
        };
        bool ok = ensure(hdr ? next.SceneHDR : next.Scene8, width, height, hdr ? FLOAT_FORMAT : format);
        for (unsigned i = 0; ok && i < 2; ++i) ok = ensure(next.Chain[i], width, height, format);
        for (unsigned i = 0; ok && bloom && i < 2; ++i)
            ok = ensure(next.Bloom[i], std::max(width / PostProcess::BLOOM_DIVISOR, 1u),
                std::max(height / PostProcess::BLOOM_DIVISOR, 1u), FLOAT_FORMAT);
        if (!ok) {
            for (SDL_GPUTexture *image : created) SDL_ReleaseGPUTexture(Device, image);
            return false;
        }
        if (replace) dispose_images(Targets, true);
        Targets = next;
        return true;
    }
    SdlTarget target(SDL_GPUTexture *texture, unsigned width, unsigned height, unsigned format)
    {
        return {texture, nullptr, width, height, 0, format};
    }
    bool pass(PostShaderProgram id, SDL_GPUTexture *source, unsigned width, unsigned height,
        const SdlTarget &destination, SDL_GPUTexture *extra = nullptr, float blurX = 0, float blurY = 0,
        bool ungraded = false, bool blurClarity = false)
    {
        if (!source || source == destination.Colour || (extra && extra == destination.Colour))
            return fail("post pass refused a source/destination alias");
        const Program &program = Programs[id];
        SdlRecordedDraw draw = {};
        draw.Pipeline = program.Pipelines[format_slot(SdlGpuFrame::Target_Colour_Format(destination))];
        draw.Count = 3;
        draw.Viewport[2] = float(destination.Width); draw.Viewport[3] = float(destination.Height);
        draw.Viewport[5] = 1;
        draw.Scissor[2] = int(destination.Width); draw.Scissor[3] = int(destination.Height);
        draw.SamplerCount = program.Samplers;
        // glslang can retain an unused Extra declaration. Every reflected SDL slot must
        // still receive a valid pair; use the source as the unused-slot fallback.
        draw.Textures[0] = source; draw.Textures[1] = extra ? extra : source;
        draw.Samplers[0] = draw.Samplers[1] = Sampler;
        PostProcessConstants constants = {};
        constants.TexelSize[0] = 1.0f / width; constants.TexelSize[1] = 1.0f / height;
        constants.TexelSize[2] = float(width); constants.TexelSize[3] = float(height);
        constants.BlurDirection[0] = blurX; constants.BlurDirection[1] = blurY;
        if (id == POST_SHADER_BLOOM_EXTRACT) constants.BlurDirection[2] = Settings.BloomSaturation;
        if (id == POST_SHADER_BLOOM_BLUR) constants.BlurDirection[3] = blurClarity ? 1.0f : 0.0f;
        if (id == POST_SHADER_BLOOM_COMPOSITE || id == POST_SHADER_BLOOM_COMPOSITE_WIDE)
            constants.BlurDirection[3] = Settings.Clarity;
        constants.Tuning[0] = Settings.BloomThreshold; constants.Tuning[1] = Settings.BloomIntensity;
        constants.Tuning[2] = PostProcess::TONE_CURVE_KNEE; constants.Tuning[3] = PostProcess::BLOOM_KNEE;
        constants.Grade[0] = ungraded ? 0.0f : Settings.Grade; constants.Grade[1] = PostProcess::GRADE_CONTRAST;
        constants.Grade[2] = PostProcess::GRADE_SATURATION; constants.Grade[3] = PostProcess::GRADE_SPLIT;
        const SdlNativeUniformBlock block = {1, 0, &constants, sizeof(constants)};
        return Owner->Record_Native_Draw(destination, draw, program.Uniforms ? &block : nullptr,
            program.Uniforms ? 1 : 0) || fail("native post draw was refused");
    }
};

SdlPostProcess::SdlPostProcess() : State(new Impl) {}
void SdlPostProcess::Set_Failure_For_Test(TestFailure failure) { State->FailureForTest = failure; }
void SdlPostProcess::Set_AO_Failure_For_Test(SdlPostOcclusion::TestFailure failure)
{
    State->Occlusion.Set_Failure_For_Test(failure);
}
SdlPostProcess::~SdlPostProcess()
{
    if (State->Owner && !Release(*State->Owner)) {
        // An unsubmitted batch still borrows these objects. Keep them alive until device teardown
        // rather than freeing handles behind queued draws after an exceptional submission failure.
        std::fprintf(stderr, "native SDL post: owner must resolve failed Release before teardown\n");
    }
    delete State;
}

bool SdlPostProcess::Prepare(SdlGpuFrame &frame, const SdlTarget &presentation,
    const SdlPostSettings &requested, SdlTarget &scene)
{
    Impl &s = *State;
    scene = presentation; s.Enabled = s.Armed = s.Finished = s.FailedFinish = false;
    s.Presentation = presentation;
    struct ClearAbandonedAO {
        Impl &State;
        ~ClearAbandonedAO() { if (!State.Enabled) State.Occlusion.Abandon(); }
    } clearAbandoned{s};
    if (s.Owner && s.Owner != &frame) return s.fail("Release the previous frame owner before rebinding");
    if (!presentation.Colour || !presentation.Width || !presentation.Height)
        return s.fail("missing presentation colour target or dimensions");
    const unsigned format = SdlGpuFrame::Target_Colour_Format(presentation);
    if (format_slot(format) > 1) return s.fail("presentation must be RGBA8/BGRA8 UNORM");
    if (requested.Count > DX11_POST_CHAIN_LIMIT) return s.fail("post chain exceeds its limit");
    if (!requested.Count) { s.Settings = requested; s.LastError.clear(); return true; }
    if (requested.ActualSamples != 1) return s.fail("native post requires actual single-sample attachments");
    if (s.fault(TestFailure::Prepare)) return s.fail("injected preparation failure");
    if (!std::isfinite(requested.BloomThreshold) || !std::isfinite(requested.BloomIntensity)
        || !std::isfinite(requested.AdditiveGain) || !std::isfinite(requested.Grade)
        || !std::isfinite(requested.BloomSaturation) || !std::isfinite(requested.Clarity))
        return s.fail("non-finite post settings");
    for (unsigned i = 0; i < requested.Count; ++i) {
        if (requested.Chain[i] < DX11_POST_COPY || requested.Chain[i] > DX11_POST_BLOOM
            || (requested.Chain[i] == DX11_POST_BLOOM && i != 0))
            return s.fail("unsupported post effect or bloom ordering");
    }
    s.Settings = requested;
    s.Settings.BloomThreshold = std::max(requested.BloomThreshold, 0.0f);
    s.Settings.BloomIntensity = std::max(requested.BloomIntensity, 0.0f);
    s.Settings.BloomSaturation = std::max(0.0f, std::min(requested.BloomSaturation, 3.0f));
    s.Settings.Clarity = std::max(0.0f, std::min(requested.Clarity, 2.0f));
    s.Settings.AdditiveGain = std::max(requested.AdditiveGain, 1.0f);
    s.Settings.BlurPasses = std::max(1u, std::min(requested.BlurPasses, PostProcess::BLOOM_BLUR_PASS_LIMIT));
    s.Settings.Grade = std::max(0.0f, std::min(requested.Grade, 1.0f));
    s.Owner = &frame; s.Device = frame.Device();
    const bool hdr = requested.Chain[0] == DX11_POST_BLOOM;
    const bool bloom = hdr && (s.Settings.BloomIntensity > 0 || s.Settings.Clarity > 0);
    std::vector<PostShaderProgram> needed = {hdr ? POST_SHADER_TONE_MAP : POST_SHADER_COPY};
    bool chainAO = false;
    for (unsigned i = 0; i < requested.Count; ++i) {
        if (requested.Chain[i] == DX11_POST_BLOOM) {
            if (bloom) {
                needed.push_back(POST_SHADER_BLOOM_EXTRACT); needed.push_back(POST_SHADER_BLOOM_BLUR);
                needed.push_back(requested.InternalHDR ? POST_SHADER_BLOOM_COMPOSITE_WIDE : POST_SHADER_BLOOM_COMPOSITE);
            } else needed.push_back(requested.InternalHDR ? POST_SHADER_TONE_MAP_WIDE : POST_SHADER_TONE_MAP);
        } else if (requested.Chain[i] == DX11_POST_AO) {
            chainAO = true; needed.push_back(POST_SHADER_COPY); // early/missing-depth AO bypass still presents
        } else needed.push_back(PostShader_Simple_Effect(requested.Chain[i]));
    }
    if (!s.programs(needed, format)) return false;
    if (chainAO && !s.Occlusion.Prepare_Final(frame, format)) return s.fail(s.Occlusion.Error());
    if (!s.images(presentation.Width, presentation.Height, format, hdr, bloom)) return false;
    scene = presentation;
    scene.Colour = hdr ? s.Targets.SceneHDR : s.Targets.Scene8;
    scene.ColourFormat = hdr ? FLOAT_FORMAT : format;
    s.Scene = scene; s.Frame = frame.Scene_Frame(); s.Generation = frame.Scene_Generation();
    s.Occlusion.Prepare(requested.MainViewKey);
    s.Enabled = s.Armed = true; s.LastError.clear();
    return true;
}

bool SdlPostProcess::Finish(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &presentation,
    const SdlPostSceneInputs *beforePost)
{
    Impl &s = *State; s.Finished = s.FailedFinish = false;
    if (!s.Enabled) return true;
    if (!s.Armed || s.Owner != &frame || frame.Scene_Frame() != s.Frame || frame.Scene_Generation() != s.Generation
        || !same_target(scene, s.Scene) || !same_target(presentation, s.Presentation))
        return s.fail("finish does not match its prepared main scene/frame");
    const bool earlyOccluded = s.Occlusion.Was_Occluded(frame, scene);
    s.Armed = false;
    // Only an exact, armed normal attempt can authorize one ungraded recovery.
    // Intermediate passes do not replace the original scene, so recovery never
    // re-applies grade/bloom to the partial chain's output.
    s.FailedFinish = true;
    SDL_GPUTexture *source = scene.Colour;
    unsigned next = 0;
    for (unsigned i = 0; i < s.Settings.Count; ++i) {
        SdlTarget destination = i + 1 == s.Settings.Count
            ? s.target(presentation.Colour, presentation.Width, presentation.Height,
                SdlGpuFrame::Target_Colour_Format(presentation))
            : s.target(s.Targets.Chain[next], scene.Width, scene.Height, s.Targets.Format);
        next = 1 - next;
        if (s.Settings.Chain[i] == DX11_POST_BLOOM) {
            const bool wide = s.Settings.InternalHDR;
            if (s.Settings.BloomIntensity <= 0 && s.Settings.Clarity <= 0) {
                if (!s.pass(wide ? POST_SHADER_TONE_MAP_WIDE : POST_SHADER_TONE_MAP,
                    source, scene.Width, scene.Height, destination)) return false;
            } else {
                const unsigned w = std::max(scene.Width / PostProcess::BLOOM_DIVISOR, 1u);
                const unsigned h = std::max(scene.Height / PostProcess::BLOOM_DIVISOR, 1u);
                if (!s.pass(POST_SHADER_BLOOM_EXTRACT, source, scene.Width, scene.Height,
                    s.target(s.Targets.Bloom[0], w, h, FLOAT_FORMAT))) return false;
                const bool clarity = s.Settings.Clarity > 0;
                const unsigned pairs = std::max(s.Settings.BloomIntensity > 0 ? s.Settings.BlurPasses : 0u,
                    clarity ? CLARITY_BLUR_PASSES : 0u);
                for (unsigned blur = 0; blur < pairs; ++blur) {
                    const float spread = float(blur + 1);
                    const bool blurClarity = clarity && blur < CLARITY_BLUR_PASSES;
                    if (!s.pass(POST_SHADER_BLOOM_BLUR, s.Targets.Bloom[0], w, h,
                        s.target(s.Targets.Bloom[1], w, h, FLOAT_FORMAT), nullptr, spread / w, 0, false, blurClarity)
                        || !s.pass(POST_SHADER_BLOOM_BLUR, s.Targets.Bloom[1], w, h,
                            s.target(s.Targets.Bloom[0], w, h, FLOAT_FORMAT), nullptr, 0, spread / h, false, blurClarity)) return false;
                }
                if (!s.pass(wide ? POST_SHADER_BLOOM_COMPOSITE_WIDE : POST_SHADER_BLOOM_COMPOSITE,
                    source, scene.Width, scene.Height, destination, s.Targets.Bloom[0])) return false;
            }
        } else if (s.Settings.Chain[i] == DX11_POST_AO) {
            bool applied = false;
            if (!earlyOccluded && beforePost) {
                const SdlTarget input = s.target(source, scene.Width, scene.Height,
                    i == 0 ? SdlGpuFrame::Target_Colour_Format(scene) : s.Targets.Format);
                applied = s.Occlusion.Filter_Final(frame, scene, input, destination, *beforePost);
            }
            if (!earlyOccluded && !applied)
                s.fail(beforePost ? s.Occlusion.Error() : "final AO bypassed: fresh BeforePost depth was not supplied");
            if (!applied && !s.pass(POST_SHADER_COPY, source, scene.Width, scene.Height, destination)) return false;
        } else if (!s.pass(PostShader_Simple_Effect(s.Settings.Chain[i]), source,
            scene.Width, scene.Height, destination)) return false;
        source = destination.Colour;
    }
    s.FailedFinish = false; s.Finished = true;
    return true;
}

bool SdlPostProcess::Needs_BeforePost_Depth() const
{
    const Impl &s = *State;
    if (!s.Enabled || !s.Armed || !s.Owner || s.Owner->Scene_Frame() != s.Frame
        || s.Owner->Scene_Generation() != s.Generation || s.Occlusion.Was_Occluded(*s.Owner, s.Scene)) return false;
    for (unsigned i = 0; i < s.Settings.Count; ++i) if (s.Settings.Chain[i] == DX11_POST_AO) return true;
    return false;
}

bool SdlPostProcess::Apply_World_AO(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs)
{
    Impl &s = *State;
    if (!s.Enabled) return false;
    if (!s.Armed || s.Owner != &frame || s.Frame != frame.Scene_Frame() || s.Generation != frame.Scene_Generation()
        || !same_target(scene, s.Scene)) return s.fail("world AO requested outside its unresolved prepared main scene");
    const bool recorded = s.Occlusion.Apply_World(frame, scene, inputs);
    if (!recorded && !s.Occlusion.Error().empty()) s.LastError = s.Occlusion.Error();
    return recorded;
}

bool SdlPostProcess::Apply_Chain_AO(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs)
{
    Impl &s = *State;
    bool wanted = false;
    for (unsigned i = 0; i < s.Settings.Count; ++i) wanted |= s.Settings.Chain[i] == DX11_POST_AO;
    if (!s.Enabled || !wanted) return false;
    if (!s.Armed || s.Owner != &frame || s.Frame != frame.Scene_Frame() || s.Generation != frame.Scene_Generation()
        || !same_target(scene, s.Scene)) return s.fail("chain AO requested outside its unresolved prepared main scene");
    const bool recorded = s.Occlusion.Apply_Chain(frame, scene, inputs);
    if (!recorded && !s.Occlusion.Error().empty()) s.LastError = s.Occlusion.Error();
    return recorded;
}

bool SdlPostProcess::Copy_Through(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &presentation)
{
    Impl &s = *State; s.Finished = false;
    if (!s.Enabled) return true;
    if ((!s.Armed && !s.FailedFinish) || s.Owner != &frame || frame.Scene_Frame() != s.Frame || frame.Scene_Generation() != s.Generation
        || !same_target(scene, s.Scene) || !same_target(presentation, s.Presentation))
        return s.fail("copy-through does not match its prepared main scene/frame");
    s.Armed = s.FailedFinish = false;
    s.Occlusion.Abandon();
    return s.pass(Scene_Is_Float() ? POST_SHADER_TONE_MAP : POST_SHADER_COPY, scene.Colour,
        scene.Width, scene.Height, s.target(presentation.Colour, presentation.Width, presentation.Height,
            SdlGpuFrame::Target_Colour_Format(presentation)), nullptr, 0, 0, true);
}

bool SdlPostProcess::Release(SdlGpuFrame &frame)
{
    Impl &s = *State;
    if (!s.Owner) return true;
    if (s.Owner != &frame) return s.fail("Release called with the wrong frame owner");
    if (frame.Has_Pending_Work() && !frame.Flush()) return s.fail("cannot release resources while queued post draws remain unsubmitted");
    if (!s.Occlusion.Release(frame)) return s.fail(s.Occlusion.Error());
    s.dispose_images(s.Targets, false);
    for (Impl::Program &program : s.Programs) {
        for (SDL_GPUGraphicsPipeline *pipeline : program.Pipelines)
            if (pipeline) SDL_ReleaseGPUGraphicsPipeline(s.Device, pipeline);
        if (program.Shader) SDL_ReleaseGPUShader(s.Device, program.Shader);
        program = {};
    }
    if (s.Sampler) SDL_ReleaseGPUSampler(s.Device, s.Sampler);
    if (s.Vertex) SDL_ReleaseGPUShader(s.Device, s.Vertex);
    s.Sampler = nullptr; s.Vertex = nullptr; s.Owner = nullptr; s.Device = nullptr;
    s.Enabled = s.Armed = s.Finished = s.FailedFinish = false;
    return true;
}

bool SdlPostProcess::Active() const { return State->Enabled; }
bool SdlPostProcess::Scene_Is_Unresolved(SdlGpuFrame &frame, const SdlTarget &scene) const
{
    const Impl &s = *State;
    return s.Enabled && s.Armed && s.Owner == &frame && s.Frame == frame.Scene_Frame()
        && s.Generation == frame.Scene_Generation() && same_target(scene, s.Scene);
}
namespace {
bool matches_identity(SdlGpuFrame &frame, const SdlTarget &scene,
    const SdlSceneCaptureRequest &identity, uint64_t viewKey)
{
    SdlSceneCaptureRequest expected, supplied = identity;
    expected.Source = scene; expected.Frame = frame.Scene_Frame();
    expected.Generation = frame.Scene_Generation(); expected.ViewKey = viewKey;
    expected.Source.ColourFormat = SdlGpuFrame::Target_Colour_Format(scene);
    expected.Source.DepthFormat = frame.Target_Depth_Format(scene);
    supplied.Source.ColourFormat = SdlGpuFrame::Target_Colour_Format(supplied.Source);
    supplied.Source.DepthFormat = frame.Target_Depth_Format(supplied.Source);
    supplied.Phase = expected.Phase;
    return supplied.Same_Identity(expected);
}
}
bool SdlPostProcess::Scene_Matches_Identity(SdlGpuFrame &frame, const SdlTarget &scene,
    const SdlSceneCaptureRequest &identity) const
{
    return Scene_Is_Unresolved(frame, scene) && matches_identity(frame, scene, identity, State->Settings.MainViewKey);
}
bool SdlPostProcess::Scene_Has_Finished(SdlGpuFrame &frame, const SdlTarget &scene,
    const SdlTarget &presentation, const SdlSceneCaptureRequest &identity) const
{
    const Impl &s = *State;
    return s.Enabled && s.Finished && s.Owner == &frame && s.Frame == frame.Scene_Frame()
        && s.Generation == frame.Scene_Generation() && same_target(scene, s.Scene)
        && same_target(presentation, s.Presentation) && matches_identity(frame, scene, identity, s.Settings.MainViewKey);
}
bool SdlPostProcess::Scene_Is_Float() const { return Active() && State->Settings.Chain[0] == DX11_POST_BLOOM; }
unsigned SdlPostProcess::Scene_Colour_Format() const
{
    return SdlGpuFrame::Target_Colour_Format(Active() ? State->Scene : State->Presentation);
}
float SdlPostProcess::Additive_Gain() const
{
    return Scene_Is_Float() && State->Settings.BloomIntensity > 0 ? State->Settings.AdditiveGain : 1.0f;
}
bool SdlPostProcess::Replaces_Legacy_Bloom() const { return Scene_Is_Float(); }
const std::string &SdlPostProcess::Error() const { return State->LastError; }
SdlPostResources SdlPostProcess::Resources() const
{
    const Impl &s = *State;
    SdlPostResources result;
    const SDL_GPUTexture *textures[] = {s.Targets.Scene8, s.Targets.SceneHDR, s.Targets.Chain[0],
        s.Targets.Chain[1], s.Targets.Bloom[0], s.Targets.Bloom[1]};
    for (const SDL_GPUTexture *texture : textures) result.Textures += texture != nullptr;
    result.Shaders = s.Vertex != nullptr; result.Samplers = s.Sampler != nullptr;
    for (const Impl::Program &program : s.Programs) {
        result.Shaders += program.Shader != nullptr;
        for (const SDL_GPUGraphicsPipeline *pipeline : program.Pipelines) result.Pipelines += pipeline != nullptr;
    }
    const auto ao = s.Occlusion.Resources();
    result.Textures += ao.Textures; result.Shaders += ao.Shaders;
    result.Pipelines += ao.Pipelines; result.Samplers += ao.Samplers;
    result.Creations = s.Creations + ao.Creations;
    return result;
}
