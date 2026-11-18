#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPostProcess.h"
#include "SdlSceneCapture.h"
#include "SdlSceneContext.h"
#include "SdlRenderMatrix.h"
#include "SdlConstants.h"
#include "Platform/RenderWaterFoam.h"
#include <cstring>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

struct PosixDevice9::PostSession {
    explicit PostSession(SdlGpuFrame &frame) : Captures(frame) {}
    SdlSceneCaptureCache Captures;
    SdlMainViewSnapshot CurrentView, LastView, ViewStack[8];
    bool WorldAO = false, Classic = false;
    SdlMainViewSnapshot FoamView;
    uint64_t FoamRefusedKey = 0;
    SdlPostProcess Processor;
    SdlPostSettings Settings;
    SdlTarget Scene = {}, Presentation = {};
    bool Routed = false, InView = false, Entered = false;
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
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    if (Post->Routed) Finish_Post(true);
    auto &s = *Post;
    s.InView = s.Entered = false; s.ViewDepth = 0;
    s.CurrentView = {}; s.LastView = {};
    s.WorldAO = settings.WorldAO && !settings.Classic;
    s.Classic = settings.Classic; s.FoamView = {}; s.FoamRefusedKey = 0;
    s.StandInTexture = nullptr; s.StandInSurface = nullptr;
    s.Settings = {};
    if (!settings.Classic && !DX11Post_Parse_Chain(settings.Chain, s.Settings.Chain, s.Settings.Count))
        s.Settings.Count = 0;
    if (!s.Settings.Count && s.WorldAO) { s.Settings.Count = 1; s.Settings.Chain[0] = DX11_POST_COPY; }
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
    if (s.ViewDepth == 8) { s.CurrentView = {}; s.InView = false; return 0; }
    s.ViewStack[s.ViewDepth++] = s.CurrentView;
    const unsigned token = s.ViewDepth;
    const bool nested = s.ViewDepth > 1;
    s.CurrentView = {}; s.InView = false;
    SdlTarget target = {}; std::string error;
    if (nested || !eligible || reflection || SunActive || !Resolve_Target(target, error) || !target.Colour || !target.Depth ||
        !Viewport.Width || !Viewport.Height || Viewport.X > target.Width || Viewport.Y > target.Height ||
        Viewport.Width > target.Width - Viewport.X || Viewport.Height > target.Height - Viewport.Y ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane) return token;
    // Nested custom/mirror targets never replace an explicitly prepared main source.
    const SdlTarget expected = s.Routed ? s.Scene : Gpu->Back_Buffer_Target();
    if (target.Colour != expected.Colour || target.Depth != expected.Depth) return token;
    SdlMainViewSnapshot view;
    view.Frame = Gpu; view.Target = target; view.Pass = SdlSceneView::Main;
    view.DeviceGeneration = Gpu->Device_Generation(); view.FrameSerial = Gpu->Scene_Frame();
    const float viewport[] = {float(Viewport.X),float(Viewport.Y),float(Viewport.Width),float(Viewport.Height),Viewport.MinZ,Viewport.MaxZ};
    std::memcpy(view.Viewport,viewport,sizeof(viewport));
    view.Scissor[0]=Viewport.X; view.Scissor[1]=Viewport.Y; view.Scissor[2]=Viewport.Width; view.Scissor[3]=Viewport.Height;
    multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,view.ViewProjection);
    if (!render_inverse(view.ViewProjection,view.WorldFromClip) ||
        !render_full_texture_to_world(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,
            target.Width,target.Height,viewport,view.FullTextureWorldFromClip,view.ProjectionScale)) return token;
    if (s.Entered && !s.InView) {
        // A second explicit view has its own ownership key; no old phase capture can satisfy it.
        s.Settings.MainViewKey = ++s.Serial;
        if (s.Routed && !s.Processor.Prepare(*Gpu,s.Presentation,s.Settings,s.Scene)) s.Routed=false;
    }
    view.ViewKey=s.Settings.MainViewKey;
    view.Capture.Source=target;
    view.Capture.Source.ColourFormat=Gpu->Target_Colour_Format(target);
    view.Capture.Source.DepthFormat=Gpu->Target_Depth_Format(target);
    view.Capture.Frame=view.FrameSerial; view.Capture.Generation=Gpu->Scene_Generation();
    view.Capture.ViewKey=view.ViewKey; view.Near=nearPlane; view.Far=farPlane;
    s.CurrentView=s.LastView=view; s.InView=s.Entered=true;
    return token;
}
void PosixDevice9::End_Post_View(unsigned token)
{
    if (!Post || !token || token!=Post->ViewDepth) return;
    Post->CurrentView=Post->ViewStack[--Post->ViewDepth];
    Post->InView=Post->CurrentView.Pass==SdlSceneView::Main;
}
bool PosixDevice9::Read_Main_View(SdlMainViewSnapshot &snapshot, bool capture, SdlSceneCapturePhase phase)
{
    snapshot={};
    if (!Gpu || !Post || !Post->InView || SunActive || Post->CurrentView.Pass!=SdlSceneView::Main) return false;
    auto view=Post->CurrentView;
    SdlTarget actual={}; std::string error;
    if (!Resolve_Target(actual,error)) return false;
    auto current=view.Capture; current.Source=actual;
    current.Source.ColourFormat=Gpu->Target_Colour_Format(actual);
    current.Source.DepthFormat=Gpu->Target_Depth_Format(actual);
    current.Frame=Gpu->Scene_Frame(); current.Generation=Gpu->Scene_Generation();
    if (!current.Same_Identity(view.Capture) || view.DeviceGeneration!=Gpu->Device_Generation()) return false;
    view.Capture.Phase=phase; view.Capture.Depth=capture;
    if (capture) {
        SdlSceneDepthCapture depth;
        if (!Post->Captures.Capture(view.Capture,depth)) return false;
        view.Capture=depth.Identity; view.SampledDepth=depth.Texture;
        view.DepthWidth=depth.Width; view.DepthHeight=depth.Height; view.DepthFormat=depth.Format;
    }
    snapshot=view; return true;
}
bool PosixDevice9::Read_Post_Inputs(SdlSceneCapturePhase phase, SdlPostSceneInputs &inputs)
{
    inputs={};
    if (!Gpu || !Post || !Post_Active()) return false;
    SdlMainViewSnapshot view;
    if (phase==SdlSceneCapturePhase::BeforePost) {
        view=Post->LastView;
        if (view.Pass!=SdlSceneView::Main || view.Capture.Frame!=Gpu->Scene_Frame() ||
            view.Capture.Generation!=Gpu->Scene_Generation() || view.ViewKey!=Post->Settings.MainViewKey ||
            view.Target.Colour!=Post->Scene.Colour || view.Target.Depth!=Post->Scene.Depth) return false;
        view.Capture.Phase=phase; view.Capture.Depth=true;
        SdlSceneDepthCapture depth;
        if (!Post->Captures.Capture(view.Capture,depth)) return false;
        view.Capture=depth.Identity; view.SampledDepth=depth.Texture;
        view.DepthWidth=depth.Width; view.DepthHeight=depth.Height; view.DepthFormat=depth.Format;
    } else if (!Read_Main_View(view,true,phase)) return false;
    inputs.Capture=view.Capture; inputs.SampledDepth=view.SampledDepth;
    inputs.DepthWidth=view.DepthWidth; inputs.DepthHeight=view.DepthHeight; inputs.DepthFormat=view.DepthFormat;
    inputs.Near=view.Near; inputs.Far=view.Far;
    std::memcpy(inputs.WorldFromClip,view.FullTextureWorldFromClip,sizeof(inputs.WorldFromClip));
    std::memcpy(inputs.ProjectionScale,view.ProjectionScale,sizeof(inputs.ProjectionScale));
    return true;
}
void PosixDevice9::Post_Opaque_Scene(bool allowWorldAO)
{
    if (!allowWorldAO || !Post_Active() || !Post->InView || !Post->WorldAO) return;
    SdlPostSceneInputs inputs;
    if (Read_Post_Inputs(SdlSceneCapturePhase::OpaqueScene,inputs))
        Post->Processor.Apply_World_AO(*Gpu,Post->Scene,inputs);
}
void PosixDevice9::Post_Before_Transparent()
{
    if (!Post_Active() || !Post->InView || !Post->Processor.Needs_BeforePost_Depth()) return;
    SdlPostSceneInputs inputs;
    if (Read_Post_Inputs(SdlSceneCapturePhase::BeforeTransparent,inputs))
        Post->Processor.Apply_Chain_AO(*Gpu,Post->Scene,inputs);
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
    SdlPostSceneInputs inputs;
    const SdlPostSceneInputs *depth = normal && s.Processor.Needs_BeforePost_Depth() &&
        Read_Post_Inputs(SdlSceneCapturePhase::BeforePost,inputs) ? &inputs : nullptr;
    bool finished = normal ? s.Processor.Finish(*Gpu, s.Scene, s.Presentation, depth) :
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

namespace RenderPost {
void OpaqueScene(IDirect3DDevice9 *d,bool allowWorldAO) { if(d) static_cast<PosixDevice9 *>(d)->Post_Opaque_Scene(allowWorldAO); }
void BeforeTransparent(IDirect3DDevice9 *d) { if(d) static_cast<PosixDevice9 *>(d)->Post_Before_Transparent(); }
}
bool PosixRenderer_Main_View(IDirect3DDevice9 *d,SdlMainViewSnapshot &s) {
    s={}; return d && static_cast<PosixDevice9 *>(d)->Read_Main_View(s,false,SdlSceneCapturePhase::OpaqueScene);
}
bool PosixRenderer_Capture_Main_View(IDirect3DDevice9 *d,SdlSceneCapturePhase p,SdlMainViewSnapshot &s) {
    s={}; return d && static_cast<PosixDevice9 *>(d)->Read_Main_View(s,true,p);
}

bool PosixDevice9::Build_Main_View_Constants(SdlEnginePixelConstants &constants, bool receiving)
{
    SdlMainViewSnapshot main;
    if (!Read_Main_View(main, false, SdlSceneCapturePhase::OpaqueScene)) return false;
    std::memcpy(constants.WorldFromClip, main.FullTextureWorldFromClip, sizeof(constants.WorldFromClip));
    if (receiving) {
        // Preserve the existing shadow's bounded-viewport/half-pixel convention
        // while cosmetic reconstruction uses full-texture pixel coordinates.
        float toShadowClip[16] = {}; toShadowClip[0] = float(main.Target.Width) / main.Viewport[2];
        toShadowClip[5] = float(main.Target.Height) / main.Viewport[3]; toShadowClip[10] = toShadowClip[15] = 1;
        toShadowClip[12] = (main.Target.Width - 2 * main.Viewport[0] - main.Viewport[2] - 1) / main.Viewport[2];
        toShadowClip[13] = (2 * main.Viewport[1] + main.Viewport[3] - main.Target.Height + 1) / main.Viewport[3];
        float folded[16]; multiply(toShadowClip, constants.ShadowFromClip, folded);
        std::memcpy(constants.ShadowFromClip, folded, sizeof(folded));
    }
    constants.ShadowViewport[0] = 1.0f/main.Target.Width; constants.ShadowViewport[1] = 1.0f/main.Target.Height;
    constants.ShadowViewport[2] = constants.ShadowViewport[3] = 0;
    return true;
}

bool PosixDevice9::Prepare_Classic_Water_Foam()
{
    SdlMainViewSnapshot main;
    if (!Post || Post->Classic || !Read_Main_View(main, false, SdlSceneCapturePhase::OpaqueScene)) return false;
    if (Post->FoamRefusedKey == main.ViewKey) return false;
    if (FoamRefusalForTest & 1) { FoamRefusalForTest &= ~1u; Post->FoamRefusedKey = main.ViewKey; return false; }
    if (!Read_Main_View(Post->FoamView, true, SdlSceneCapturePhase::OpaqueScene)) {
        Post->FoamRefusedKey = main.ViewKey; return false;
    }
    return true;
}
SDL_GPUTexture *PosixDevice9::Classic_Water_Depth()
{
    SdlMainViewSnapshot main;
    if (!Post || Post->Classic || !Post->FoamView.SampledDepth ||
        !Read_Main_View(main, false, SdlSceneCapturePhase::OpaqueScene)) return nullptr;
    return main.Capture.Same_Identity(Post->FoamView.Capture) ? Post->FoamView.SampledDepth : nullptr;
}
bool PosixDevice9::Classic_Water_Foam_Available() const { return Gpu && Gpu->Supports_Scene_Depth(); }
namespace RenderWaterFoam {
bool Available(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Classic_Water_Foam_Available(); }
bool Ready(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Prepare_Classic_Water_Foam(); }
}
