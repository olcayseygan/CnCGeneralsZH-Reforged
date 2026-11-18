#include "SdlSceneCapture.h"
#include <SDL3/SDL.h>

SdlSceneCaptureCache::SdlSceneCaptureCache(SdlGpuFrame &frame) : Frame(frame) {}
SdlSceneCaptureCache::~SdlSceneCaptureCache() { Release(); }

bool SdlSceneCaptureCache::Fail(const char *message)
{
    LastError = message;
    return false;
}

void SdlSceneCaptureCache::Release()
{
    for (Entry &entry : Entries) {
        if (entry.View.Texture) Frame.Release_After_Batch(entry.View.Texture, nullptr);
        entry = {};
    }
}

bool SdlSceneCaptureCache::Capture(const SdlSceneCaptureRequest &requested, SdlSceneDepthCapture &result)
{
    result = {};
    unsigned slot;
    switch (requested.Phase) {
        case SdlSceneCapturePhase::OpaqueScene: slot = 0; break;
        case SdlSceneCapturePhase::BeforeTransparent: slot = 1; break;
        case SdlSceneCapturePhase::BeforePost: slot = 2; break;
        case SdlSceneCapturePhase::TerrainOverlay: slot = 3; break;
        default: return Fail("unknown main-scene capture phase");
    }
    if (!requested.Depth || requested.Colour || !requested.ViewKey || !requested.Source.Colour ||
        !requested.Source.Depth || !requested.Source.Width || !requested.Source.Height ||
        requested.Frame != Frame.Scene_Frame() || requested.Generation != Frame.Scene_Generation() ||
        !Frame.Supports_Scene_Depth())
        return Fail("depth capture requires a current explicit main view and sampleable scene depth");
    SdlSceneCaptureRequest identity = requested;
    identity.Source.ColourFormat = Frame.Target_Colour_Format(identity.Source);
    identity.Source.DepthFormat = Frame.Target_Depth_Format(identity.Source);
    Entry &entry = Entries[slot];
    if (entry.Ready && entry.View.Identity.Same_Identity(identity)) {
        result = entry.View;
        return true;
    }
    entry.Ready = false;
    const unsigned width = identity.Source.Width, height = identity.Source.Height;
    if (!entry.View.Texture || entry.View.Width != width || entry.View.Height != height) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width; info.height = height; info.layer_count_or_depth = info.num_levels = 1;
        SDL_GPUTexture *replacement = SDL_CreateGPUTexture(Frame.Device(), &info);
        if (!replacement) return Fail("cannot allocate independent normalized depth capture");
        if (entry.View.Texture) Frame.Release_After_Batch(entry.View.Texture, nullptr);
        entry.View.Texture = replacement;
        entry.View.Width = width; entry.View.Height = height;
        entry.View.Format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    }
    if (!Frame.Capture_Scene_Depth(identity.Source, entry.View.Texture))
        return Fail("cannot submit independent normalized depth capture");
    entry.View.Identity = identity;
    entry.Ready = true;
    result = entry.View;
    LastError.clear();
    return true;
}
