#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPostProcess.h"
#include "Platform/RenderTeamGlow.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

struct PosixDevice9::PostSession {
    SdlPostProcess Processor;
    SdlPostSettings Settings;
    SdlTarget Scene = {}, Presentation = {};
    bool Routed = false, InView = false, Entered = false;
    bool ViewStack[8] = {};
    unsigned ViewDepth = 0;
    uint64_t Serial = 0;
    IDirect3DBaseTexture9 *StandInTexture = nullptr;
    IDirect3DSurface9 *StandInSurface = nullptr;
    SDL_GPUTexture *StandInCopy = nullptr;
    unsigned CopyWidth = 0, CopyHeight = 0, CopyFormat = 0;
};
void PosixDevice9::Begin_Post(const RenderPost::Settings &settings)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>();
    if (Post->Routed) Finish_Post(true);
    auto &s = *Post;
    s.InView = s.Entered = false; s.ViewDepth = 0;
    TeamGlowDraw = false;
    TeamGlowAllowed = !settings.Classic; // do not reread TeamGlow: meshes keep their load-time markings
    s.StandInTexture = nullptr; s.StandInSurface = nullptr;
    s.Settings = {};
    if (!settings.Classic && !DX11Post_Parse_Chain(settings.Chain, s.Settings.Chain, s.Settings.Count))
        s.Settings.Count = 0;
    // The saved default chain includes AO. This slot has no depth consumer;
    // omit only that stage locally, preserving all other order/options and DX11.
    unsigned supported = 0;
    for (unsigned i = 0; i < s.Settings.Count; ++i)
        if (s.Settings.Chain[i] != DX11_POST_AO) s.Settings.Chain[supported++] = s.Settings.Chain[i];
    s.Settings.Count = supported;
    s.Settings.MainViewKey = ++s.Serial;
    // Every actual SDL scene attachment is created with SDL_GPU_SAMPLECOUNT_1.
    // The D3D9 requested multisample option is not an attachment capability.
    s.Settings.ActualSamples = 1;
    s.Settings.BloomThreshold = settings.BloomThreshold; s.Settings.BloomIntensity = settings.BloomIntensity;
    s.Settings.BloomSaturation = settings.BloomSaturation; s.Settings.Clarity = settings.Clarity;
    s.Settings.AdditiveGain = settings.AdditiveGain; s.Settings.BlurPasses = settings.BlurPasses;
    s.Settings.Grade = settings.Grade; s.Settings.InternalHDR = settings.InternalHDR;
    s.Presentation = Gpu->Back_Buffer_Target();
    s.Routed = s.Processor.Prepare(*Gpu, s.Presentation, s.Settings, s.Scene) && s.Processor.Active();
    if (s.Routed) {
        Gpu->Set_Target(s.Scene);
        // Begin_Render already cleared presentation. Keep the redirected colour/depth equally initialized.
        const unsigned depthFormat = Gpu->Target_Depth_Format(s.Scene);
        const bool stencil = depthFormat == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
            depthFormat == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        const float alpha = std::isfinite(settings.ClearAlpha) ? std::fmax(0.0f, std::fmin(settings.ClearAlpha, 1.0f)) : 1.0f;
        Gpu->Clear_Back_Buffer(true, true, stencil, unsigned(alpha * 255) << 24, 1, 0);
    }
}
unsigned PosixDevice9::Begin_Post_View(bool eligible, float nearPlane, float farPlane, bool reflection)
{
    if (!Post || !Gpu) return 0;
    auto &s = *Post;
    if (s.ViewDepth == 8) { s.InView = false; return 0; }
    s.ViewStack[s.ViewDepth++] = s.InView;
    s.InView = false;
    SdlTarget actual = {}; std::string error;
    if (eligible && !reflection && !SunActive && s.Routed && !s.Entered &&
        std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0 && farPlane > nearPlane &&
        Resolve_Target(actual, error) && actual.Colour == s.Scene.Colour && actual.Depth == s.Scene.Depth &&
        Viewport.Width && Viewport.Height && Viewport.X <= actual.Width && Viewport.Y <= actual.Height &&
        Viewport.Width <= actual.Width - Viewport.X && Viewport.Height <= actual.Height - Viewport.Y) {
        s.InView = s.Entered = true;
    }
    return s.ViewDepth;
}
void PosixDevice9::End_Post_View(unsigned token)
{
    if (!Post || !token || token != Post->ViewDepth) return;
    Post->InView = Post->ViewStack[--Post->ViewDepth];
}
bool PosixDevice9::Post_Active() const { return Post && Post->Routed; }
bool PosixDevice9::Post_Replaces_Bloom() const { return Post_Active() && Post->Processor.Replaces_Legacy_Bloom(); }
void PosixDevice9::Post_Stand_In(IDirect3DBaseTexture9 *texture, IDirect3DSurface9 *surface)
{
    if (Post) {
        // endRenderToTexture still samples the stand-in during its default-filter copy/bloom.
        if (texture) Post->StandInTexture = texture;
        Post->StandInSurface = surface;
    }
}
bool PosixDevice9::Post_Target(IDirect3DSurface9 *surface, SdlTarget &target) const
{
    if (!Post_Active() || (surface != BackBuffer && surface != Post->StandInSurface)) return false;
    target = Post->Scene; return true;
}
bool PosixDevice9::Post_Texture(IDirect3DBaseTexture9 *texture, SDL_GPUTexture *&result)
{
    if (!Post_Active() || !texture || texture != Post->StandInTexture) return false;
    PostSession &s = *Post; result = nullptr;
    const unsigned format = Gpu->Target_Colour_Format(s.Scene);
    if (!s.StandInCopy || s.CopyWidth != s.Scene.Width || s.CopyHeight != s.Scene.Height || s.CopyFormat != format) {
        SDL_GPUTextureCreateInfo info = {}; info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPUTextureFormat(format); info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = s.Scene.Width; info.height = s.Scene.Height; info.layer_count_or_depth = info.num_levels = 1;
        SDL_GPUTexture *replacement = SDL_CreateGPUTexture(Gpu->Device(), &info);
        if (!replacement) return true;
        if (s.StandInCopy) Gpu->Release_After_Batch(s.StandInCopy, nullptr);
        s.StandInCopy = replacement; s.CopyWidth = info.width; s.CopyHeight = info.height; s.CopyFormat = format;
    }
    const int32_t rect[4] = {0, 0, int32_t(s.Scene.Width), int32_t(s.Scene.Height)};
    Gpu->Record_Blit(s.Scene.Colour, rect, s.StandInCopy, rect, false);
    result = s.StandInCopy; return true;
}
float PosixDevice9::Post_Additive_Gain(const SdlTarget &target) const
{
    return !SunActive && Post_Active() && Post->InView && target.Colour == Post->Scene.Colour &&
        target.Depth == Post->Scene.Depth ? Post->Processor.Additive_Gain() : 1.0f;
}

void PosixDevice9::Finish_Post(bool fallback)
{
    if (!Gpu || !Post_Active()) return;
    auto &s = *Post;
    const bool normal = !fallback && s.Entered;
    bool finished = normal ? s.Processor.Finish(*Gpu, s.Scene, s.Presentation) :
        s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation);
    if (!finished && normal) finished = s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation);
    if (!finished) std::fprintf(stderr, "PosixDevice9: main post finish failed: %s\n", s.Processor.Error().c_str());
    s.Routed = s.InView = false;
    Gpu->Set_Target(s.Presentation);
}
void PosixDevice9::Release_Post(bool teardown)
{
    if (!Post || !Gpu) return;
    Finish_Post(true);
    if (!Post->Processor.Release(*Gpu)) {
        if (!teardown) return; // ordinary reset keeps queued borrowers alive
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Processor.Release(*Gpu)) return;
    }
    if (Post->StandInCopy) Gpu->Release_After_Batch(Post->StandInCopy, nullptr);
    Post.reset();
}
namespace RenderPost {
void BeginMain(IDirect3DDevice9 *d, const Settings &s) { if (d) static_cast<PosixDevice9 *>(d)->Begin_Post(s); }
unsigned BeginView(IDirect3DDevice9 *d, bool e, float n, float f, bool r) { return d ? static_cast<PosixDevice9 *>(d)->Begin_Post_View(e,n,f,r) : 0; }
void EndView(IDirect3DDevice9 *d, unsigned t) { if(d) static_cast<PosixDevice9 *>(d)->End_Post_View(t); }
void FinishScene(IDirect3DDevice9 *d, bool f) { if(d) static_cast<PosixDevice9 *>(d)->Finish_Post(f); }
bool ReplacesLegacyBloom(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Replaces_Bloom(); }
void SetSceneStandIn(IDirect3DDevice9 *d, IDirect3DBaseTexture9 *t, IDirect3DSurface9 *s) { if(d) static_cast<PosixDevice9 *>(d)->Post_Stand_In(t,s); }
void SetGlowDraw(IDirect3DDevice9 *d, bool e) { if(d) static_cast<PosixDevice9 *>(d)->Set_Glow_Draw(e); }
}

namespace RenderTeamGlow {
void SetDraw(IDirect3DDevice9 *d, bool marked) { if(d) static_cast<PosixDevice9 *>(d)->Set_Team_Glow(marked); }
bool Available(IDirect3DDevice9 *d) {
    auto *frame = d ? static_cast<PosixDevice9 *>(d)->Get_Gpu() : nullptr;
    return frame && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
}
