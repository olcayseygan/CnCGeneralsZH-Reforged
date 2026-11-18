#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPostProcess.h"
#include "SdlConstants.h"
#include "SdlRenderMatrix.h"
#include "Platform/RenderEffects.h"
#include "ffshader.h"
#include <cstring>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

// Color-only owner: no borrowed camera pointer, water snapshot or depth-provider lifetime.
struct PosixDevice9::ModelView {
    SdlTarget Target = {};
    D3DVIEWPORT9 Viewport = {};
    D3DMATRIX View = {}, Projection = {};
    float WorldFromClip[16] = {};
    uint64_t Frame = 0, Generation = 0, Key = 0;
    bool Valid = false;
};
struct PosixDevice9::PostSession {
    SdlPostProcess Processor;
    SdlPostSettings Settings;
    SdlTarget Scene = {}, Presentation = {};
    bool Routed = false, InView = false, Entered = false;
    bool ViewStack[8] = {};
    ModelView ModelCurrent, ModelStack[8];
    uint64_t ModelSerial = 0, ModelReadyKey = 0, ModelRefusedKey = 0;
    SDL_GPUTexture *ModelColour = nullptr;
    unsigned ModelWidth = 0, ModelHeight = 0, ModelFormat = 0;
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
    s.ModelCurrent = {}; s.ModelReadyKey = s.ModelRefusedKey = 0;
    ModelEffectsAllowed = !settings.Classic;
    ModelEffect = 0; std::memset(ModelEffectParameters, 0, sizeof(ModelEffectParameters));
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
    if (s.ViewDepth == 8) { s.InView = false; s.ModelCurrent = {}; return 0; }
    s.ViewStack[s.ViewDepth] = s.InView;
    s.ModelStack[s.ViewDepth] = s.ModelCurrent;
    ++s.ViewDepth;
    s.InView = false; s.ModelCurrent = {};
    SdlTarget actual = {}; std::string error;
    if (!eligible || reflection || SunActive || !std::isfinite(nearPlane) || !std::isfinite(farPlane)
        || nearPlane <= 0 || farPlane <= nearPlane || !Resolve_Target(actual, error)
        || !actual.Colour || !actual.Depth || !Viewport.Width || !Viewport.Height
        || Viewport.X > actual.Width || Viewport.Y > actual.Height
        || Viewport.Width > actual.Width - Viewport.X || Viewport.Height > actual.Height - Viewport.Y)
        return s.ViewDepth;
    if (s.Routed && !s.Entered && actual.Colour == s.Scene.Colour && actual.Depth == s.Scene.Depth)
        s.InView = s.Entered = true;

    const auto direct = Gpu->Back_Buffer_Target();
    const bool main = (actual.Colour == direct.Colour && actual.Depth == direct.Depth)
        || (s.Routed && actual.Colour == s.Scene.Colour && actual.Depth == s.Scene.Depth)
        || (s.StandInSurface && RenderTargets[0] == s.StandInSurface);
    if (ModelEffectsAllowed && main) {
        ModelView view;
        const float viewport[] = {float(Viewport.X), float(Viewport.Y), float(Viewport.Width),
            float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ};
        float scale[2];
        if (!render_full_texture_to_world(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11,
            actual.Width, actual.Height, viewport, view.WorldFromClip, scale)) return s.ViewDepth;
        view.Target = actual; view.Target.ColourFormat = Gpu->Target_Colour_Format(actual);
        view.Target.DepthFormat = Gpu->Target_Depth_Format(actual);
        view.Viewport = Viewport; view.View = Transforms[D3DTS_VIEW]; view.Projection = Transforms[D3DTS_PROJECTION];
        view.Frame = Gpu->Scene_Frame(); view.Generation = Gpu->Scene_Generation();
        if (++s.ModelSerial == 0) ++s.ModelSerial;
        view.Key = s.ModelSerial; view.Valid = true;
        s.ModelCurrent = view;
    }
    return s.ViewDepth;
}
void PosixDevice9::End_Post_View(unsigned token)
{
    if (!Post || !token || token != Post->ViewDepth) return;
    --Post->ViewDepth;
    Post->InView = Post->ViewStack[Post->ViewDepth];
    Post->ModelCurrent = Post->ModelStack[Post->ViewDepth];
}
bool PosixDevice9::Read_Model_View(ModelView &view)
{
    view = {};
    if (!Gpu || !Post || SunActive || !ModelEffectsAllowed || !Post->ModelCurrent.Valid) return false;
    const ModelView &owner = Post->ModelCurrent;
    SdlTarget actual = {}; std::string error;
    if (!Resolve_Target(actual, error) || actual.Colour != owner.Target.Colour || actual.Depth != owner.Target.Depth
        || actual.Width != owner.Target.Width || actual.Height != owner.Target.Height
        || Gpu->Target_Colour_Format(actual) != owner.Target.ColourFormat
        || Gpu->Target_Depth_Format(actual) != owner.Target.DepthFormat
        || Gpu->Scene_Frame() != owner.Frame || Gpu->Scene_Generation() != owner.Generation
        || std::memcmp(&Viewport, &owner.Viewport, sizeof(Viewport))
        || std::memcmp(&Transforms[D3DTS_VIEW], &owner.View, sizeof(D3DMATRIX))
        || std::memcmp(&Transforms[D3DTS_PROJECTION], &owner.Projection, sizeof(D3DMATRIX))) return false;
    const auto direct = Gpu->Back_Buffer_Target();
    if (!((actual.Colour == direct.Colour && actual.Depth == direct.Depth)
        || (Post->Routed && actual.Colour == Post->Scene.Colour && actual.Depth == Post->Scene.Depth)
        || (Post->StandInSurface && RenderTargets[0] == Post->StandInSurface))) return false;
    view = owner; return true;
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
    if (!Gpu || !Post) return;
    auto &s = *Post;
    s.ModelCurrent = {}; s.ModelReadyKey = 0;
    if (!Post_Active()) return;
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
    if (Post->ModelColour) Gpu->Release_After_Batch(Post->ModelColour, nullptr);
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

// Admission allocates before the scene suppresses the old stealth treatment. The copy itself
// happens only for a real cloaked mesh, after all preceding opaque draws have been submitted.
bool PosixDevice9::Takes_Model_Effect(unsigned effect)
{
    ModelView main;
    if (!Model_Effects_Available() || effect == DRAW_EFFECT_NONE || effect >= DRAW_EFFECT_CLOAK_SHADOW
        || !Read_Model_View(main)) return false;
    if (effect != DRAW_EFFECT_CLOAK) return true;
    auto &s = *Post;
    if (s.ModelRefusedKey == main.Key) return false;
    if (ModelRefusalForTest & 1) {
        ModelRefusalForTest &= ~1u; s.ModelRefusedKey = main.Key; return false;
    }
    const unsigned format = main.Target.ColourFormat;
    if (!s.ModelColour || s.ModelWidth != main.Target.Width || s.ModelHeight != main.Target.Height
        || s.ModelFormat != format) {
        SDL_GPUTextureCreateInfo info = {}; info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPUTextureFormat(format); info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = main.Target.Width; info.height = main.Target.Height; info.layer_count_or_depth = info.num_levels = 1;
        SDL_GPUTexture *replacement = SDL_CreateGPUTexture(Gpu->Device(), &info);
        if (!replacement) { s.ModelRefusedKey = main.Key; return false; }
        if (s.ModelColour) Gpu->Release_After_Batch(s.ModelColour, nullptr);
        s.ModelColour = replacement; s.ModelWidth = info.width; s.ModelHeight = info.height; s.ModelFormat = format;
        s.ModelReadyKey = 0;
    }
    return true;
}
void PosixDevice9::Set_Model_Effect(unsigned effect, const float parameters[4])
{
    if (SunActive) return; // caster coverage already has its own independently saved metadata
    if (effect >= DRAW_EFFECT_CLOAK_SHADOW || (effect && !parameters)) effect = DRAW_EFFECT_NONE;
    for (unsigned i = 0; effect && i < 4; ++i) if (!std::isfinite(parameters[i])) effect = DRAW_EFFECT_NONE;
    ModelEffect = effect;
    if (effect) std::memcpy(ModelEffectParameters, parameters, sizeof(ModelEffectParameters));
    else std::memset(ModelEffectParameters, 0, sizeof(ModelEffectParameters));
    if (effect != DRAW_EFFECT_CLOAK || !Takes_Model_Effect(effect)) return;
    ModelView main;
    if (!Read_Model_View(main) || Post->ModelReadyKey == main.Key) return;
    const bool injected = (ModelRefusalForTest & 2) != 0; ModelRefusalForTest &= ~2u;
    if (injected || !Gpu->Capture_Scene_Colour(main.Target, Post->ModelColour)) {
        Post->ModelReadyKey = 0; Post->ModelRefusedKey = main.Key;
    } else Post->ModelReadyKey = main.Key;
}
unsigned PosixDevice9::Requested_Model_Effect(float parameters[4]) const
{
    const unsigned effect = ModelEffectsAllowed && !SunActive ? ModelEffect : unsigned(DRAW_EFFECT_NONE);
    if (effect) std::memcpy(parameters, ModelEffectParameters, sizeof(ModelEffectParameters));
    else std::memset(parameters, 0, sizeof(ModelEffectParameters));
    return effect;
}
SDL_GPUTexture *PosixDevice9::Model_Colour()
{
    ModelView main;
    return Read_Model_View(main) && Post->ModelReadyKey == main.Key ? Post->ModelColour : nullptr;
}
unsigned PosixDevice9::Draw_Model_Effect()
{
    ModelView main;
    if (!ModelEffect || (FVF & D3DFVF_XYZRHW) || !Read_Model_View(main)) return DRAW_EFFECT_NONE;
    if (RenderStates[D3DRS_ALPHABLENDENABLE]) {
        const auto source = RenderStates[D3DRS_SRCBLEND], destination = RenderStates[D3DRS_DESTBLEND];
        if (source == D3DBLEND_ZERO || source == D3DBLEND_DESTCOLOR || destination == D3DBLEND_SRCCOLOR) return DRAW_EFFECT_NONE;
        if (source == D3DBLEND_ONE || destination == D3DBLEND_ONE)
            return ModelEffect == DRAW_EFFECT_OUTAGE ? DRAW_EFFECT_OUTAGE : DRAW_EFFECT_NONE;
    }
    return ModelEffect == DRAW_EFFECT_CLOAK && !Model_Colour() ? DRAW_EFFECT_NONE : ModelEffect;
}
bool PosixDevice9::Build_Model_Constants(SdlEnginePixelConstants &constants, bool receiving)
{
    ModelView main;
    if (!Read_Model_View(main)) return false;
    std::memcpy(constants.WorldFromClip, main.WorldFromClip, sizeof(constants.WorldFromClip));
    if (receiving) {
        // Fold the sun receiver's bounded-viewport/half-pixel convention before switching
        // cosmetic reconstruction to full-texture NDC. Both then see the same world point.
        float toShadowClip[16] = {}; toShadowClip[0] = float(main.Target.Width) / main.Viewport.Width;
        toShadowClip[5] = float(main.Target.Height) / main.Viewport.Height; toShadowClip[10] = toShadowClip[15] = 1;
        toShadowClip[12] = (float(main.Target.Width) - 2 * main.Viewport.X - main.Viewport.Width - 1) / main.Viewport.Width;
        toShadowClip[13] = (2.0f * main.Viewport.Y + main.Viewport.Height + 1 - main.Target.Height) / main.Viewport.Height;
        float folded[16]; multiply(toShadowClip, constants.ShadowFromClip, folded);
        std::memcpy(constants.ShadowFromClip, folded, sizeof(folded));
    }
    constants.ShadowViewport[0] = 1.0f / main.Target.Width; constants.ShadowViewport[1] = 1.0f / main.Target.Height;
    constants.ShadowViewport[2] = constants.ShadowViewport[3] = 0;
    std::memcpy(constants.DrawEffectParameters, ModelEffectParameters, sizeof(ModelEffectParameters));
    return true;
}
namespace RenderEffects {
void AllowDrawEffects(IDirect3DDevice9 *d, bool a) { if(d) static_cast<PosixDevice9 *>(d)->Allow_Model_Effects(a); }
bool DrawEffectsAvailable(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Model_Effects_Available(); }
bool TakesDrawEffect(IDirect3DDevice9 *d, unsigned e) { return d && static_cast<PosixDevice9 *>(d)->Takes_Model_Effect(e); }
void SetDrawEffect(IDirect3DDevice9 *d, unsigned e, const float p[4]) { if(d) static_cast<PosixDevice9 *>(d)->Set_Model_Effect(e,p); }
unsigned RequestedDrawEffect(IDirect3DDevice9 *d, float p[4]) {
    if(d) return static_cast<PosixDevice9 *>(d)->Requested_Model_Effect(p);
    std::memset(p,0,4*sizeof(float)); return DRAW_EFFECT_NONE;
}
}
