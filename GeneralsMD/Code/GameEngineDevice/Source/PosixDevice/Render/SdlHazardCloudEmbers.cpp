// Sparse nuclear embers. No assets, gameplay particles or damage are involved.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "SdlHazardCloudEmbers.h"
#include "Platform/HazardCloudCore.h"
#include "WW3D2/sdl3shadercompile.h"
#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SdlHazardCloudEmbers {
namespace {

SDL_GPUDevice *gpu = nullptr;
SDL_GPUShader *vertexShader = nullptr;
SDL_GPUShader *pixelShader = nullptr;
SDL_GPUGraphicsPipeline *pipeline = nullptr;
SDL_GPUSampler *depthSampler = nullptr;
bool failed = false;
unsigned renderedFrames = 0;

bool fail(const char *reason)
{
    if (!failed) std::fprintf(stderr, "hazard-cloud: nuclear embers FAIL: %s\n", reason);
    failed = true;
    return false;
}

bool makeResources(SdlGpuFrame *frame)
{
    gpu = frame->Device();
    std::vector<unsigned char> spirv;
    std::string log;
    if (!SDL3_Compile_HLSL_To_SPIRV(HazardCloud::emberVertexShader(true), true, spirv, log)
        || !(vertexShader = SDL3_Create_Shader(gpu, spirv, true, log)))
        return fail(("vertex shader: " + log).c_str());
    if (!SDL3_Compile_HLSL_To_SPIRV(HazardCloud::emberPixelShader(true), false, spirv, log)
        || !(pixelShader = SDL3_Create_Shader(gpu, spirv, false, log)))
        return fail(("pixel shader: " + log).c_str());
    SDL_GPUColorTargetDescription colour = {};
    colour.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
    colour.blend_state.enable_blend = true;
    colour.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo description = {};
    description.vertex_shader = vertexShader;
    description.fragment_shader = pixelShader;
    description.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    description.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    description.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    description.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    description.depth_stencil_state.enable_depth_test = false;
    description.depth_stencil_state.enable_depth_write = false;
    description.target_info.color_target_descriptions = &colour;
    description.target_info.num_color_targets = 1;
    description.target_info.has_depth_stencil_target = true;
    // Clouds render only into the main scene. Its implicit and mirrored depth
    // surfaces use the frame's scene format, independently of shadow targets.
    description.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(frame->Depth_Format());
    pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!pipeline) return fail(SDL_GetError());
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w
        = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    depthSampler = SDL_CreateGPUSampler(gpu, &sampler);
    if (!depthSampler) return fail(SDL_GetError());
    return true;
}

} // namespace

bool initialize(SdlGpuFrame *frame)
{
    if (failed || !frame || !frame->Device()) return false;
    if (!pipeline && !makeResources(frame)) return false;
    return gpu == frame->Device() || fail("the graphics device was replaced");
}

bool prepare(SdlGpuFrame *frame, const SdlTarget &target,
             const HazardCloud::FieldState *fields, unsigned fieldCount)
{
    if (failed || !frame || !frame->Device() || !target.Colour || !target.Depth
        || fieldCount > 2 || (fieldCount && !fields)) return false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        if (!HazardCloud::emberField(fields[i])) continue;
        if (!initialize(frame)) return false;
        if (gpu != frame->Device()) return fail("the graphics device was replaced");
    }
    return true;
}

bool render(SdlGpuFrame *frame, const SdlTarget &target, SDL_GPUTexture *sceneDepth,
            const float viewProjection[16], const float viewport[6],
            const HazardCloud::FieldState *fields, unsigned fieldCount,
            float timeSeconds, SDL_GPUTexture *terrainTexture, const bool *terrainEnabled,
            const float *terrainDomain)
{
    if (failed) return false;
    if (!frame || !frame->Device() || !target.Colour || !target.Depth || !sceneDepth
        || !target.Width || !target.Height || !viewProjection || !viewport
        || fieldCount > 2 || (fieldCount && !fields) || !std::isfinite(timeSeconds))
        return fail("invalid rendering inputs");
    if (terrainDomain) {
        HazardCloud::EmberConstants check = {};
        if (!HazardCloud::setEmberTerrainDomain(terrainDomain, check))
            return fail("invalid shared terrain domain");
    }
    bool active = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        if (!HazardCloud::emberField(fields[i])) continue;
        if (!initialize(frame)) return false;
        if (gpu != frame->Device()) return fail("the preview graphics device was replaced");
        HazardCloud::EmberConstants constants;
        const unsigned emberCount = HazardCloud::emberConstants(fields[i], i,
            terrainTexture && terrainEnabled && terrainEnabled[i], viewProjection, viewport,
            timeSeconds, constants);
        if (terrainDomain) HazardCloud::setEmberTerrainDomain(terrainDomain, constants);
        const float pixelConstants[4] = { 1.0f / target.Width, 1.0f / target.Height, 0, 0 };
        SdlRecordedDraw draw = {};
        draw.Pipeline = pipeline;
        // SV_VertexID creates each billboard; the common batch still binds a
        // valid staging buffer even though this pipeline has no vertex inputs.
        std::memset(frame->Stage(16, draw.VertexOffset), 0, 16);
        draw.Count = emberCount * 6;
        draw.VertexConstants = frame->Constants(0, &constants, sizeof(constants));
        draw.VertexConstantsSize = sizeof(constants);
        draw.PixelConstants = frame->Constants(1, pixelConstants, sizeof(pixelConstants));
        draw.PixelConstantsSize = sizeof(pixelConstants);
        draw.VertexSamplerCount = 1;
        draw.VertexTextures[0] = terrainTexture ? terrainTexture : sceneDepth;
        draw.VertexSamplers[0] = depthSampler;
        draw.SamplerCount = 1;
        draw.Textures[0] = sceneDepth;
        draw.Samplers[0] = depthSampler;
        std::copy(viewport, viewport + 6, draw.Viewport);
        draw.Scissor[0] = static_cast<int32_t>(std::clamp(viewport[0], 0.0f, static_cast<float>(target.Width)));
        draw.Scissor[1] = static_cast<int32_t>(std::clamp(viewport[1], 0.0f, static_cast<float>(target.Height)));
        draw.Scissor[2] = static_cast<int32_t>(std::min(viewport[2], static_cast<float>(target.Width - draw.Scissor[0])));
        draw.Scissor[3] = static_cast<int32_t>(std::min(viewport[3], static_cast<float>(target.Height - draw.Scissor[1])));
        frame->Set_Target(target);
        frame->Record_Draw(draw);
        active = true;
    }
    if (active) ++renderedFrames;
    return true;
}

bool validationPassed() { return !failed; }

void shutdown()
{
    if (gpu) {
        if (pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        if (depthSampler) SDL_ReleaseGPUSampler(gpu, depthSampler);
        if (vertexShader) SDL_ReleaseGPUShader(gpu, vertexShader);
        if (pixelShader) SDL_ReleaseGPUShader(gpu, pixelShader);
    }
    abandon();
}

void abandon()
{
    gpu = nullptr;
    vertexShader = pixelShader = nullptr;
    pipeline = nullptr;
    depthSampler = nullptr;
    renderedFrames = 0;
    failed = false;
}

} // namespace SdlHazardCloudEmbers
