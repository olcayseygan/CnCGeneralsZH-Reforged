#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPostProcess.h"
#include "SdlSceneCapture.h"
#include "SdlSceneContext.h"
#include "SdlRenderMatrix.h"
#include "SdlPostAtmosphere.h"
#include "SdlResourceMirror.h"
#include <cstring>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

struct PosixDevice9::PostSession {
    explicit PostSession(SdlGpuFrame &frame) : Captures(frame) {}
    SdlSceneCaptureCache Captures;
    SdlPostAtmosphere Atmosphere;
    RenderPost::Environment Environment;
    bool EnvironmentFresh = false, EnvironmentCurrent = false, EnvironmentReady = false;
    SdlMainViewSnapshot CurrentView, LastView, ViewStack[8];
    bool WorldAO = false;
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
void PosixDevice9::Set_Post_Environment(const RenderPost::Environment &environment)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    // Invalidate prepared borrowed views before releasing the old packet's COM
    // references. Already queued draws retain their native readers through batch retirement.
    Post->Atmosphere.Abandon(); Post->EnvironmentReady = false;
    IDirect3DBaseTexture9 *sky = Post->Environment.Sky, *shroud = Post->Environment.Shroud;
    Posix_Bind(sky, environment.Sky); Posix_Bind(shroud, environment.Shroud);
    Post->Environment = environment; Post->Environment.Sky = sky; Post->Environment.Shroud = shroud;
    Post->EnvironmentFresh = true;
    Post->EnvironmentCurrent = false;
}

void PosixDevice9::Begin_Post(const RenderPost::Settings &settings)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    if (Post->Routed) Finish_Post(true);
    auto &s = *Post;
    s.InView = s.Entered = false; s.ViewDepth = 0;
    s.CurrentView = {}; s.LastView = {};
    s.Atmosphere.Abandon(); s.EnvironmentReady = false;
    s.EnvironmentCurrent = s.EnvironmentFresh && !settings.Classic && s.Environment.MapIdentity && s.Environment.MapGeneration;
    s.EnvironmentFresh = false;
    s.WorldAO = settings.WorldAO && !settings.Classic;
    s.StandInTexture = nullptr; s.StandInSurface = nullptr;
    s.Settings = {};
    if (!settings.Classic && !DX11Post_Parse_Chain(settings.Chain, s.Settings.Chain, s.Settings.Count))
        s.Settings.Count = 0;
    if (!s.Settings.Count && (s.WorldAO || (s.EnvironmentCurrent &&
        (s.Environment.Haze.Strength > 0 || s.Environment.Atmosphere.Strength > 0)))) { s.Settings.Count = 1; s.Settings.Chain[0] = DX11_POST_COPY; }
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
        s.Atmosphere.Abandon(); s.EnvironmentReady = false;
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
    if (Post->EnvironmentCurrent && Post->Environment.Shroud) {
        std::string error;
        inputs.Shroud = Get_Mirrors()->Texture(Post->Environment.Shroud,error);
        if (!inputs.Shroud) return false;
        std::memcpy(inputs.ShroudRect,Post->Environment.ShroudRect,sizeof(inputs.ShroudRect));
    }
    return true;
}
void PosixDevice9::Post_Sky(bool drawSky)
{
    if (!Post_Active() || !Post->InView || !Post->EnvironmentCurrent) return;
    PostSession &s = *Post;
    SdlMainViewSnapshot view;
    if (!Read_Main_View(view, false, SdlSceneCapturePhase::OpaqueScene) ||
        view.Target.Colour != s.Scene.Colour || view.Target.Depth != s.Scene.Depth) return;
    SdlPostGroundHazeSettings haze;
    std::memcpy(haze.Colour, s.Environment.Haze.Colour, sizeof(haze.Colour));
    haze.Top = s.Environment.Haze.Top; haze.Thickness = s.Environment.Haze.Thickness;
    haze.Strength = s.Environment.Haze.Strength; haze.Reach = s.Environment.Haze.Reach;
    haze.WaterHeight = s.Environment.Haze.WaterHeight; haze.Warmth = s.Environment.Haze.Warmth;
    std::memcpy(haze.TowardSun, s.Environment.Haze.TowardSun, sizeof(haze.TowardSun));
    SdlPostAtmosphereSettings atmosphere;
    std::memcpy(atmosphere.Horizon, s.Environment.Atmosphere.Horizon, sizeof(atmosphere.Horizon));
    std::memcpy(atmosphere.Tint, s.Environment.Atmosphere.Tint, sizeof(atmosphere.Tint));
    atmosphere.Start = s.Environment.Atmosphere.Start; atmosphere.Scale = s.Environment.Atmosphere.Scale;
    atmosphere.Strength = s.Environment.Atmosphere.Strength; atmosphere.Ground = s.Environment.Atmosphere.Ground;
    std::memcpy(atmosphere.MapSize, s.Environment.Atmosphere.MapSize, sizeof(atmosphere.MapSize));
    SdlPostAtmosphereViews views;
    views.MapIdentity = s.Environment.MapIdentity; views.MapGeneration = s.Environment.MapGeneration;
    std::memcpy(views.ShroudRect, s.Environment.ShroudRect, sizeof(views.ShroudRect));
    std::string error;
    auto mirror = [&](IDirect3DBaseTexture9 *source, SdlPostBorrowedView &destination) {
        if (!source) return true;
        destination.Texture = Get_Mirrors()->Texture(source,error);
        if (!destination.Texture) return false;
        // The retained COM owner identifies the view; native replacement changes
        // Texture, and the stable device lifetime supplies its generation.
        destination.Identity = reinterpret_cast<uintptr_t>(source);
        destination.Generation = Gpu->Device_Generation();
        return true;
    };
    if (!mirror(s.Environment.Sky, views.Sky) || !mirror(s.Environment.Shroud, views.Shroud)) return;
    s.EnvironmentReady = s.Atmosphere.Prepare(*Gpu, s.Processor, s.Scene, view.Capture,
        view.FullTextureWorldFromClip, haze, atmosphere, views);
    if (drawSky && s.EnvironmentReady && atmosphere.Strength > 0) s.Atmosphere.Draw_Sky(*Gpu, s.Scene);
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
    if (!Post_Active() || !Post->InView) return;
    // A haze-only scene also needs preparation when the Atmosphere option did not
    // request the earlier sky hook. This performs no visible sky draw at strength0.
    if (Post->EnvironmentCurrent && !Post->EnvironmentReady) Post_Sky(false);
    SdlPostSceneInputs inputs;
    if (Read_Post_Inputs(SdlSceneCapturePhase::BeforeTransparent, inputs)) {
        Post->Processor.Apply_Chain_AO(*Gpu, Post->Scene, inputs);
        if (Post->EnvironmentReady) {
            Post->Atmosphere.Apply_Ground_Haze(*Gpu, Post->Scene, inputs);
            Post->Atmosphere.Apply_Atmosphere(*Gpu, Post->Scene, inputs);
        }
    }
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
    if (!Post->Atmosphere.Release(*Gpu)) {
        if (!teardown) return;
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Atmosphere.Release(*Gpu)) return;
    }
    if (!Post->Processor.Release(*Gpu)) {
        if (!teardown) return; // ordinary reset keeps queued borrowers alive
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Processor.Release(*Gpu)) return;
    }
    if (Post->StandInCopy) Gpu->Release_After_Batch(Post->StandInCopy, nullptr);
    Posix_Bind(Post->Environment.Sky, static_cast<IDirect3DBaseTexture9 *>(nullptr));
    Posix_Bind(Post->Environment.Shroud, static_cast<IDirect3DBaseTexture9 *>(nullptr));
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

bool PosixDevice9::Post_Environment_Available() const { return Gpu && Gpu->Supports_Scene_Depth(); }
namespace RenderPost {
bool EnvironmentAvailable(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Environment_Available(); }
void SetEnvironment(IDirect3DDevice9 *d,const Environment &e) { if(d) static_cast<PosixDevice9 *>(d)->Set_Post_Environment(e); }
void Sky(IDirect3DDevice9 *d) { if(d) static_cast<PosixDevice9 *>(d)->Post_Sky(); }
}
