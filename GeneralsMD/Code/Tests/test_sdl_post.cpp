// The native post module through its public route, actual SDL shaders and readback.
// Default mode checks SDL retargeting/reflection without a GPU; --gpu checks pixels/lifetimes.
#include "SdlPostProcess.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void check_bindings()
{
    std::string hlsl, error, msl;
    std::vector<unsigned char> spirv;
    unsigned samplers = 99, uniforms = 99;
    CHECK(Sdl_Post_Vertex_Source(hlsl));
    CHECK(hlsl.find("[[vk::location(2)]] float2 Texture") != std::string::npos);
    CHECK(SDL3_Compile_HLSL_To_SPIRV(hlsl, true, spirv, error));
    SDL3_Shader_Slots(spirv, true, samplers, uniforms);
    CHECK(samplers == 0 && uniforms == 0);
    CHECK(SDL3_Translate_SPIRV_To_MSL(spirv, true, msl, error));
    CHECK(msl.find("main0") != std::string::npos);
    const PostShaderProgram programs[] = {POST_SHADER_COPY, POST_SHADER_FXAA, POST_SHADER_SHARPEN,
        POST_SHADER_SHARPEN_MULTISAMPLED, POST_SHADER_TONE_MAP, POST_SHADER_TONE_MAP_WIDE,
        POST_SHADER_BLOOM_EXTRACT, POST_SHADER_BLOOM_BLUR, POST_SHADER_BLOOM_COMPOSITE,
        POST_SHADER_BLOOM_COMPOSITE_WIDE};
    for (PostShaderProgram program : programs) {
        CHECK(Sdl_Post_Pixel_Source(program, hlsl));
        CHECK(hlsl.find("register(b0, space3)") != std::string::npos);
        CHECK(hlsl.find("register(t0, space2)") != std::string::npos);
        CHECK(hlsl.find("[[vk::location(2)]] float2 Texture") != std::string::npos);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(hlsl, false, spirv, error));
        SDL3_Shader_Slots(spirv, false, samplers, uniforms);
        const bool composite = program == POST_SHADER_BLOOM_COMPOSITE || program == POST_SHADER_BLOOM_COMPOSITE_WIDE;
        // The bundled glslang retains Source/Extra and the common cbuffer even when main
        // does not read them. Pin actual slot reflection; the renderer fills every slot.
        CHECK(samplers == 2);
        CHECK(uniforms == 1);
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv, false, msl, error));
        CHECK(msl.find("main0") != std::string::npos);
        if (composite) {
            CHECK(hlsl.find("register(s1, space2)") != std::string::npos);
            CHECK(hlsl.find("Extra.Sample(Extra_Sampler,") != std::string::npos);
        }
    }
    CHECK(!Sdl_Post_Pixel_Source(POST_SHADER_HEAT_HAZE, hlsl)); CHECK(hlsl.empty());
    CHECK(!Sdl_Post_Pixel_Source(POST_SHADER_COUNT, hlsl)); CHECK(hlsl.empty());
}

struct Colour { float R, G, B, A = 1; };
static uint16_t half(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    const unsigned sign = bits >> 16 & 0x8000;
    const int exponent = int(bits >> 23 & 255) - 127 + 15;
    if (exponent <= 0) return uint16_t(sign); // fixtures use zero or normal positive halves
    if (exponent >= 31) return uint16_t(sign | 0x7c00);
    const unsigned rounded = (bits & 0x7fffff) + 0x1000;
    return uint16_t(sign | (unsigned(exponent) << 10) | (rounded >> 13));
}
static unsigned byte(float value) { return unsigned(std::lround(std::max(0.0f, std::min(value, 1.0f)) * 255)); }
static bool upload(SdlGpuFrame &frame, const SdlTarget &target, const std::vector<Colour> &pixels)
{
    const bool hdr = SdlGpuFrame::Target_Colour_Format(target) == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    const unsigned size = unsigned(pixels.size()) * (hdr ? 8 : 4);
    uint32_t offset = 0;
    uint8_t *bytes = frame.Upload_Space(size, offset);
    if (!bytes) return false;
    for (size_t i = 0; i < pixels.size(); ++i) {
        const Colour &c = pixels[i];
        if (hdr) {
            const uint16_t rgba[] = {half(c.R), half(c.G), half(c.B), half(c.A)};
            std::memcpy(bytes + i * 8, rgba, 8);
        } else {
            bytes[i * 4] = uint8_t(byte(c.B)); bytes[i * 4 + 1] = uint8_t(byte(c.G));
            bytes[i * 4 + 2] = uint8_t(byte(c.R)); bytes[i * 4 + 3] = uint8_t(byte(c.A));
        }
    }
    frame.Queue_Texture_Upload(target.Colour, 0, target.Width, target.Height, offset);
    return true;
}
static unsigned channel(const std::vector<uint8_t> &image, unsigned w, unsigned x, unsigned y, unsigned rgb)
{
    const size_t index = (size_t(y) * w + x) * 4 + (rgb == 0 ? 2 : rgb == 1 ? 1 : 0);
    return index < image.size() ? image[index] : 999;
}
static SdlPostSettings setting(const char *chain)
{
    SdlPostSettings value;
    CHECK(DX11Post_Parse_Chain(chain, value.Chain, value.Count));
    return value;
}
static std::vector<Colour> corners(unsigned w, unsigned h)
{
    std::vector<Colour> pixels(size_t(w) * h);
    const Colour colours[] = {{0.8f, 0.1f, 0.2f}, {0.2f, 0.7f, 0.3f},
        {0.1f, 0.3f, 0.9f}, {0.9f, 0.7f, 0.1f}};
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
        pixels[size_t(y) * w + x] = colours[(x > w / 3 ? 1 : 0) + (y > h * 2 / 3 ? 2 : 0)];
    pixels[0] = {1, 0, 1}; pixels.back() = {0, 1, 1};
    return pixels;
}
static bool run(SdlGpuFrame &frame, SdlPostProcess &post, const SdlPostSettings &settings,
    const std::vector<Colour> &pixels, std::vector<uint8_t> &result)
{
    const SdlTarget presentation = frame.Back_Buffer_Target();
    SdlTarget scene;
    if (!post.Prepare(frame, presentation, settings, scene)) return false;
    if (!upload(frame, scene, pixels) || !post.Finish(frame, scene, presentation)
        || !frame.Read_Back(presentation.Colour, presentation.Width, presentation.Height, result)) return false;
    return frame.Present(nullptr);
}
static unsigned differences(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    if (a.size() != b.size()) return ~0u;
    unsigned changed = 0;
    for (size_t i = 0; i < a.size(); i += 4) changed += std::memcmp(a.data() + i, b.data() + i, 4) != 0;
    return changed;
}
static float curve(float x)
{
    return std::min(x, 0.8f) + 0.2f * (1.0f - std::exp(-std::max(x - 0.8f, 0.0f) / 0.2f));
}

static int check_gpu()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("SKIP SDL: %s\n", SDL_GetError()); return 77; }
    std::string error;
    SdlGpuFrame *frame = SdlGpuFrame::Create(nullptr, 129, 97, error);
    if (!frame) { std::printf("SKIP GPU: %s\n", error.c_str()); SDL_Quit(); return 77; }
    const SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(frame->Device());
    std::printf("GPU: %s; driver %s\n",
        SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
        SDL_GetGPUDeviceDriver(frame->Device()));
    {
        SdlPostProcess post;
        unsigned w = 129, h = 97;
        std::vector<uint8_t> copy, off, filtered, sharpened, legacy, wide, glow, noGlow, graded;
        std::vector<Colour> pixels = corners(w, h);
        CHECK(run(*frame, post, setting("copy"), pixels, copy));
        // Every texel, asymmetric divisions and unique corner pixels: orientation, placement and BGRA.
        CHECK(copy.size() == size_t(w) * h * 4);
        if (copy.size() == size_t(w) * h * 4) for (size_t i = 0; i < pixels.size(); ++i) {
            CHECK(copy[i * 4] == byte(pixels[i].B)); CHECK(copy[i * 4 + 1] == byte(pixels[i].G));
            CHECK(copy[i * 4 + 2] == byte(pixels[i].R)); CHECK(copy[i * 4 + 3] == 255);
        }
        CHECK(run(*frame, post, setting("off"), pixels, off));
        CHECK(!post.Active() && post.Additive_Gain() == 1 && !post.Replaces_Legacy_Bloom());
        CHECK(differences(copy, off) == 0);

        SdlPostSettings tone = setting("bloom"); tone.BloomIntensity = 0; tone.AdditiveGain = 4;
        pixels.assign(size_t(w) * h, {0.2f, 0.5f, 0.75f});
        for (unsigned y = 0; y < h; ++y) for (unsigned x = w / 2; x < w; ++x)
            pixels[size_t(y) * w + x] = {8, 4, 1.6f};
        CHECK(run(*frame, post, tone, pixels, legacy));
        CHECK(post.Scene_Is_Float() && post.Replaces_Legacy_Bloom() && post.Additive_Gain() == 1);
        CHECK(post.Scene_Colour_Format() == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
        for (unsigned rgb = 0; rgb < 3; ++rgb) {
            const float ordinary[] = {0.2f, 0.5f, 0.75f};
            CHECK(std::abs(int(channel(legacy, w, 3, 3, rgb)) - int(byte(curve(ordinary[rgb])))) <= 1);
        }
        tone.InternalHDR = true;
        CHECK(run(*frame, post, tone, pixels, wide));
        CHECK(channel(wide, w, w - 3, 3, 0) > channel(wide, w, w - 3, 3, 1));
        CHECK(channel(wide, w, w - 3, 3, 1) > channel(wide, w, w - 3, 3, 2));
        CHECK(channel(legacy, w, w - 3, 3, 2) > channel(wide, w, w - 3, 3, 2) + 70);
        for (unsigned rgb = 0; rgb < 3; ++rgb)
            CHECK(channel(legacy, w, 3, 3, rgb) == channel(wide, w, 3, 3, rgb));

        // An abandoned/no-view frame keeps the Windows fallback's legacy curve, with no grade.
        SdlPostSettings fallback = tone; fallback.Grade = 1;
        SdlTarget fallbackScene;
        const SdlTarget fallbackPresentation = frame->Back_Buffer_Target();
        CHECK(post.Prepare(*frame, fallbackPresentation, fallback, fallbackScene));
        CHECK(upload(*frame, fallbackScene, pixels));
        CHECK(post.Copy_Through(*frame, fallbackScene, fallbackPresentation));
        std::vector<uint8_t> through;
        CHECK(frame->Read_Back(fallbackPresentation.Colour, w, h, through));
        CHECK(differences(legacy, through) == 0);
        CHECK(frame->Present(nullptr));

        // The actual atomic recorder can refuse while the scene remains intact.
        // Exactly one ungraded original-scene recovery follows a failed normal
        // Finish; it never qualifies for the blast module's normal-Finish proof.
        SdlPostSettings recovery = setting("bloom,fxaa,sharpen");
        recovery.InternalHDR = true; recovery.Grade = 1; recovery.MainViewKey = 29;
        SdlTarget recoveryScene; const auto recoveryOutput = frame->Back_Buffer_Target();
        CHECK(post.Prepare(*frame, recoveryOutput, recovery, recoveryScene));
        CHECK(upload(*frame, recoveryScene, pixels));
        SdlSceneCaptureRequest recoveryKey; recoveryKey.Source = recoveryScene;
        recoveryKey.Frame = frame->Scene_Frame(); recoveryKey.Generation = frame->Scene_Generation(); recoveryKey.ViewKey = 29;
        frame->Fail_Next_Native_Record_For_Test(); CHECK(!post.Finish(*frame, recoveryScene, recoveryOutput));
        CHECK(!post.Scene_Has_Finished(*frame, recoveryScene, recoveryOutput, recoveryKey));
        SdlTarget wrongRecovery = recoveryScene; --wrongRecovery.Width;
        CHECK(!post.Copy_Through(*frame, wrongRecovery, recoveryOutput));
        CHECK(post.Copy_Through(*frame, recoveryScene, recoveryOutput));
        CHECK(!post.Scene_Has_Finished(*frame, recoveryScene, recoveryOutput, recoveryKey));
        CHECK(!post.Copy_Through(*frame, recoveryScene, recoveryOutput));
        CHECK(frame->Read_Back(recoveryOutput.Colour, w, h, through));
        CHECK(differences(legacy, through) == 0); // no wide map, grade, blur or duplicate bloom
        CHECK(frame->Present(nullptr));

        // A second refusal consumes the only recovery authorization. The frame
        // owner may retain its prior presentation and route HUD there, but may
        // not spin/replay a normal chain or call it a successfully finished view.
        CHECK(post.Prepare(*frame, recoveryOutput, recovery, recoveryScene));
        CHECK(upload(*frame, recoveryScene, pixels));
        recoveryKey.Source = recoveryScene; recoveryKey.Frame = frame->Scene_Frame(); recoveryKey.Generation = frame->Scene_Generation();
        frame->Fail_Next_Native_Record_For_Test(); CHECK(!post.Finish(*frame, recoveryScene, recoveryOutput));
        frame->Fail_Next_Native_Record_For_Test(); CHECK(!post.Copy_Through(*frame, recoveryScene, recoveryOutput));
        CHECK(!post.Copy_Through(*frame, recoveryScene, recoveryOutput));
        CHECK(!post.Finish(*frame, recoveryScene, recoveryOutput));
        CHECK(!post.Scene_Has_Finished(*frame, recoveryScene, recoveryOutput, recoveryKey));
        CHECK(frame->Read_Back(recoveryOutput.Colour, w, h, through));CHECK(differences(legacy, through) == 0);
        CHECK(frame->Present(nullptr));

        CHECK(post.Prepare(*frame, recoveryOutput, recovery, recoveryScene));
        CHECK(upload(*frame, recoveryScene, pixels));
        recoveryKey.Source = recoveryScene; recoveryKey.Frame = frame->Scene_Frame(); recoveryKey.Generation = frame->Scene_Generation();
        frame->Fail_Next_Native_Record_For_Test(); CHECK(!post.Finish(*frame, recoveryScene, recoveryOutput));
        CHECK(frame->Present(nullptr)); // an old-frame failure cannot authorize a new-frame copy
        CHECK(!post.Copy_Through(*frame, recoveryScene, recoveryOutput));

        pixels.assign(size_t(w) * h, {0, 0, 0});
        for (unsigned y = h / 2 - 3; y <= h / 2 + 3; ++y)
            for (unsigned x = w / 2 - 3; x <= w / 2 + 3; ++x) pixels[size_t(y) * w + x] = {8, 4, 2};
        SdlPostSettings bloom = setting("bloom"); bloom.AdditiveGain = 3;
        CHECK(run(*frame, post, bloom, pixels, glow)); CHECK(post.Additive_Gain() == 3);
        CHECK(run(*frame, post, tone, pixels, noGlow));
        CHECK(channel(glow, w, w / 2 + 14, h / 2, 0) > 0);
        CHECK(channel(noGlow, w, w / 2 + 14, h / 2, 0) == 0);
        CHECK(channel(glow, w, 1, 1, 0) == 0);

        // Upstream's orange bloom reaches the native path through the shared shader.
        SdlPostSettings saturated = bloom; saturated.BloomSaturation = 1.5f;
        std::vector<uint8_t> orange;
        CHECK(run(*frame, post, saturated, pixels, orange));
        CHECK(channel(orange, w, w / 2 + 14, h / 2, 0) >= channel(glow, w, w / 2 + 14, h / 2, 0));
        CHECK(channel(orange, w, w / 2 + 14, h / 2, 2) < channel(glow, w, w / 2 + 14, h / 2, 2));

        // Clarity still runs with Glow off. It separates an edge, leaves a flat
        // field alone, and turning it back off restores the original bytes.
        pixels.assign(size_t(w) * h, {0.25f, 0.25f, 0.25f});
        const unsigned left = w / 2 - 8, top = h / 2 - 8;
        for (unsigned y = top; y < top + 16; ++y) for (unsigned x = left; x < left + 16; ++x)
            pixels[size_t(y) * w + x] = {0.625f, 0.625f, 0.625f};
        SdlPostSettings clarity = setting("bloom"); clarity.BloomIntensity = 0;
        std::vector<uint8_t> plain, detailed, restored, widerGlow;
        CHECK(run(*frame, post, clarity, pixels, plain));
        clarity.Clarity = 0.75f;
        CHECK(run(*frame, post, clarity, pixels, detailed));
        CHECK(post.Additive_Gain() == 1);
        CHECK(channel(detailed, w, left + 1, top + 8, 0) > channel(plain, w, left + 1, top + 8, 0) + 4);
        CHECK(channel(detailed, w, left - 2, top + 8, 0) + 2 < channel(plain, w, left - 2, top + 8, 0));
        CHECK(std::abs(int(channel(detailed, w, 1, 1, 0)) - int(channel(plain, w, 1, 1, 0))) <= 1);
        // Later glow blur pairs carry the same luminance rather than widening
        // clarity. Sub-threshold art must have the same contrast at every level.
        clarity.BloomIntensity = 1.5f; clarity.BlurPasses = 4;
        CHECK(run(*frame, post, clarity, pixels, widerGlow));
        CHECK(differences(detailed, widerGlow) == 0);
        clarity.BloomIntensity = 0; clarity.Clarity = 0;
        CHECK(run(*frame, post, clarity, pixels, restored));
        CHECK(differences(plain, restored) == 0);
        pixels.assign(size_t(w) * h, {0.25f, 0.25f, 0.25f});
        clarity.Clarity = 0.75f;
        CHECK(run(*frame, post, clarity, pixels, detailed));
        CHECK(std::abs(int(channel(detailed, w, w / 2, h / 2, 0)) - int(byte(0.25f))) <= 1);

        pixels.assign(size_t(w) * h, {0.15f, 0.15f, 0.15f});
        for (unsigned y = 0; y < h; ++y) for (unsigned x = w / 2; x < w; ++x)
            pixels[size_t(y) * w + x] = {0.75f, 0.75f, 0.75f};
        tone.InternalHDR = false; tone.Grade = 0;
        CHECK(run(*frame, post, tone, pixels, legacy));
        tone.Grade = 1;
        CHECK(run(*frame, post, tone, pixels, graded));
        CHECK(channel(graded, w, 3, 3, 0) < channel(legacy, w, 3, 3, 0));
        CHECK(channel(graded, w, w - 3, 3, 0) > channel(legacy, w, w - 3, 3, 0));
        CHECK(channel(graded, w, 3, 3, 2) > channel(graded, w, 3, 3, 0));
        // a light over the pivot keeps its hue: a warm lean there turned lit snow cream
        CHECK(std::abs(int(channel(graded, w, w - 3, 3, 0)) - int(channel(graded, w, w - 3, 3, 2))) <= 1);

        pixels.assign(size_t(w) * h, {0.2f, 0.2f, 0.2f});
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
            if (x * h > y * w) pixels[size_t(y) * w + x] = {0.8f, 0.8f, 0.8f};
        CHECK(run(*frame, post, setting("copy"), pixels, copy));
        CHECK(run(*frame, post, setting("fxaa"), pixels, filtered));
        CHECK(differences(copy, filtered) > 32);
        CHECK(channel(filtered, w, 2, h - 3, 0) == 51);
        CHECK(channel(filtered, w, w - 3, 2, 0) == 204);
        pixels.assign(size_t(w) * h, {0.2f, 0.2f, 0.2f});
        pixels[size_t(h / 2) * w + w / 2] = {0.5f, 0.5f, 0.5f};
        pixels[size_t(h / 2) * w + w / 2 + 1] = {0.8f, 0.8f, 0.8f};
        CHECK(run(*frame, post, setting("copy"), pixels, copy));
        CHECK(run(*frame, post, setting("sharpen"), pixels, sharpened));
        CHECK(channel(sharpened, w, w / 2, h / 2, 0) >= channel(copy, w, w / 2, h / 2, 0) + 10);
        CHECK(channel(sharpened, w, 2, 2, 0) == 51);

        // Warm every supported path, then vary chains/parameters/repeated frames without new resources.
        std::vector<SdlPostSettings> choices = {setting("copy"), setting("fxaa"), setting("sharpen"), bloom,
            tone, setting("bloom,fxaa,sharpen,copy")};
        choices.push_back(bloom); choices.back().InternalHDR = true;
        choices.push_back(tone); choices.back().InternalHDR = true;
        choices.push_back(clarity);
        choices.push_back(saturated); choices.back().Clarity = 0.75f; choices.back().InternalHDR = true;
        for (const auto &choice : choices) CHECK(run(*frame, post, choice, pixels, filtered));
        const SdlPostResources warm = post.Resources();
        CHECK(warm.Textures == 6 && warm.Samplers == 1);
        for (unsigned cycle = 0; cycle < 4; ++cycle)
            for (auto choice : choices) {
                choice.BlurPasses = cycle + 1;
                CHECK(run(*frame, post, choice, pixels, filtered));
            }
        const SdlPostResources repeated = post.Resources();
        CHECK(repeated.Textures == warm.Textures && repeated.Shaders == warm.Shaders);
        CHECK(repeated.Pipelines == warm.Pipelines && repeated.Samplers == warm.Samplers);
        CHECK(repeated.Creations == warm.Creations);

        std::vector<uint8_t> expectedRecovery;
        CHECK(run(*frame, post, bloom, pixels, expectedRecovery));
        using Failure = SdlPostProcess::TestFailure;
        for (auto point : {Failure::Prepare, Failure::Shader, Failure::Pipeline, Failure::Target, Failure::TargetAfterOne}) {
            SdlPostProcess refused;
            refused.Set_Failure_For_Test(point);
            SdlTarget scene;
            const SdlTarget presentation = frame->Back_Buffer_Target();
            CHECK(!refused.Prepare(*frame, presentation, bloom, scene));
            CHECK(scene.Colour == presentation.Colour && scene.Depth == presentation.Depth);
            CHECK(!refused.Active() && refused.Additive_Gain() == 1 && !refused.Replaces_Legacy_Bloom());
            CHECK(!refused.Error().empty());
            CHECK(refused.Resources().Textures == 0); // no partially committed image transaction
            // An active fault in refused must not affect another module on the same device.
            std::vector<uint8_t> independent;
            CHECK(run(*frame, post, bloom, pixels, independent));
            CHECK(differences(expectedRecovery, independent) == 0);
            refused.Set_Failure_For_Test(Failure::None);
            std::vector<uint8_t> recovered;
            CHECK(run(*frame, refused, bloom, pixels, recovered));
            CHECK(differences(expectedRecovery, recovered) == 0);
            CHECK(refused.Release(*frame));
        }
        const SdlTarget presentation = frame->Back_Buffer_Target();
        SdlTarget scene;
        SdlPostSettings bypass = setting("off"); bypass.ActualSamples = 4;
        CHECK(post.Prepare(*frame, presentation, bypass, scene));
        CHECK(scene.Colour == presentation.Colour && !post.Active());
        SdlPostSettings invalid = bloom; invalid.ActualSamples = 4;
        CHECK(!post.Prepare(*frame, presentation, invalid, scene)); CHECK(scene.Colour == presentation.Colour);
        invalid = bloom; invalid.Chain[0] = static_cast<DX11PostEffect>(99); CHECK(!post.Prepare(*frame, presentation, invalid, scene));
        CHECK(scene.Colour == presentation.Colour && !post.Active());
        CHECK(post.Prepare(*frame, presentation, setting("copy"), scene));
        CHECK(scene.Depth == presentation.Depth && scene.Width == presentation.Width && scene.Height == presentation.Height);
        frame->Set_Target(scene); frame->Clear_Back_Buffer(false, true, false, 0, 0.375f, 0);
        CHECK(upload(*frame, scene, pixels));
        std::vector<float> depthBefore;
        CHECK(frame->Read_Depth(presentation.Depth, w, h, depthBefore));
        CHECK(depthBefore.size() == size_t(w) * h);
        if (depthBefore.size() == size_t(w) * h) {
            const float measured = depthBefore[size_t(h / 2) * w + w / 2];
            const unsigned format = frame->Target_Depth_Format(presentation);
            const float quantum = format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? 1.0f / 16777215.0f
                : format == SDL_GPU_TEXTUREFORMAT_D16_UNORM ? 1.0f / 65535.0f
                : std::nextafter(0.375f, 1.0f) - 0.375f;
            std::printf("borrowed scene depth: format=%u requested=0.375 actual=%.9g quantum=%.9g\n",
                format, measured, quantum);
            CHECK(std::abs(measured - 0.375f) <= quantum);
        }
        SdlTarget wrong = scene; wrong.Width--;
        CHECK(!post.Finish(*frame, wrong, presentation));
        CHECK(post.Finish(*frame, scene, presentation));
        CHECK(!post.Finish(*frame, scene, presentation));
        std::vector<float> depth;
        CHECK(frame->Read_Depth(presentation.Depth, w, h, depth));
        CHECK(depth.size() == size_t(w) * h);
        CHECK(depth == depthBefore); // bit-for-bit preservation of the actual depth attachment
        CHECK(frame->Present(nullptr));

        // Odd sizes, replacement while the preceding packet still names old resources, then reuse.
        CHECK(post.Prepare(*frame, presentation, bloom, scene));
        CHECK(upload(*frame, scene, pixels)); CHECK(post.Finish(*frame, scene, presentation));
        SDL_GPUTextureCreateInfo replacementInfo = {};
        replacementInfo.type = SDL_GPU_TEXTURETYPE_2D;
        replacementInfo.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        replacementInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        replacementInfo.width = 67; replacementInfo.height = 51;
        replacementInfo.layer_count_or_depth = replacementInfo.num_levels = 1;
        SDL_GPUTexture *replacement = SDL_CreateGPUTexture(frame->Device(), &replacementInfo);
        CHECK(replacement);
        SdlTarget replacementPresentation = {replacement, nullptr, 67, 51, 0, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM};
        SdlTarget replacementScene;
        const std::vector<Colour> replacementPixels = corners(67, 51);
        CHECK(post.Prepare(*frame, replacementPresentation, setting("copy"), replacementScene));
        CHECK(upload(*frame, replacementScene, replacementPixels));
        CHECK(post.Finish(*frame, replacementScene, replacementPresentation));
        std::vector<uint8_t> replaced;
        CHECK(frame->Read_Back(replacement, 67, 51, replaced));
        CHECK(channel(replaced, 67, 0, 0, 0) == 255 && channel(replaced, 67, 66, 50, 1) == 255);
        SDL_ReleaseGPUTexture(frame->Device(), replacement);
        CHECK(frame->Resize(67, 51)); w = 67; h = 51; pixels = corners(w, h);
        CHECK(run(*frame, post, setting("copy"), pixels, copy));
        CHECK(channel(copy, w, 0, 0, 0) == 255 && channel(copy, w, 0, 0, 2) == 255);
        CHECK(channel(copy, w, w - 1, h - 1, 1) == 255 && channel(copy, w, w - 1, h - 1, 2) == 255);
        CHECK(run(*frame, post, bloom, pixels, filtered));
        const auto resized = post.Resources();
        for (unsigned i = 0; i < 5; ++i) CHECK(run(*frame, post, bloom, pixels, filtered));
        CHECK(post.Resources().Creations == resized.Creations);
        CHECK(post.Release(*frame));
        const auto released = post.Resources();
        CHECK(released.Textures == 0 && released.Shaders == 0 && released.Pipelines == 0 && released.Samplers == 0);
    }
    delete frame;
    SDL_Quit();
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    check_bindings();
    if (failures) return 1;
    const int result = argc > 1 && !std::strcmp(argv[1], "--gpu") ? check_gpu() : 0;
    std::printf("SDL native post: %d checks, %d failures\n", checks, failures);
    return result;
}
