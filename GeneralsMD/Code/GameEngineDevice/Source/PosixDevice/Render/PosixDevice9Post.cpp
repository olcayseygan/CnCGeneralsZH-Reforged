#include "SdlGpuParticlesContext.h"
#include "SdlResourceMirror.h"
#include "Platform/RenderParticles.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPostProcess.h"
#include "SdlPostDistortion.h"
#include "SdlSceneCapture.h"
#include "SdlSceneContext.h"
#include "SdlConstants.h"
#include "SdlRenderMatrix.h"
#include "Platform/RenderEffects.h"
#include "Platform/RenderTeamGlow.h"
#include "Platform/RenderWaterFoam.h"
#include "ffshader.h"
#include "SdlPostAtmosphere.h"
#include <cstring>
#include <algorithm>
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
    explicit PostSession(SdlGpuFrame &frame) : Captures(frame) {}
    SdlSceneCaptureCache Captures;
    SdlPostAtmosphere Atmosphere;
    RenderPost::Environment Environment;
    bool EnvironmentFresh = false, EnvironmentCurrent = false, EnvironmentReady = false;
    SdlMainViewSnapshot CurrentView, LastView, ViewStack[8], SoftView;
    bool SoftRequested = false;
    bool WorldAO = false, RequestedChain = false, Classic = false;
    bool HeatPending = false, BlastPending = false;
    SdlPostDistortion Distortion;
    SdlMainViewSnapshot FoamView;
    uint64_t FoamRefusedKey = 0;
    SdlPostProcess Processor;
    SdlPostSettings Settings;
    SdlTarget Scene = {}, Presentation = {};
    bool Routed = false, InView = false, Entered = false;
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

bool PosixDevice9::Post_Distortion_Available() const { return Gpu!=nullptr; }
void PosixDevice9::Set_Post_Heat(const float *records,unsigned count)
{
    if(!Post)return;
    auto &s=*Post;
    if(!Post_Active() || !s.RequestedChain || s.Classic || !s.InView || SunActive)return;
    s.HeatPending=false;
    SdlMainViewSnapshot main;
    if(!Read_Main_View(main,false,SdlSceneCapturePhase::OpaqueScene))return;
    s.HeatPending=s.Distortion.Prepare_Heat(*Gpu,s.Processor,s.Scene,main.Capture,records,count) && count;
}
void PosixDevice9::Set_Post_Warps(const RenderPost::Warp *records,unsigned count)
{
    if(!Post)return;
    auto &s=*Post;s.BlastPending=false;
    if(!Post_Active() || !s.RequestedChain || s.Classic || SunActive || !s.Entered || (!records && count))return;
    DX11PostWarp warps[DX11_POST_WARP_LIMIT]={};
    const unsigned copied=std::min(count,DX11_POST_WARP_LIMIT);
    for(unsigned i=0;i<copied;++i){const auto &from=records[i];auto &to=warps[i];
        to.CentreX=from.CentreX;to.CentreY=from.CentreY;to.Radius=from.Radius;to.Pull=from.Pull;
        to.RingRadius=from.RingRadius;to.RingWidth=from.RingWidth;to.RingStrength=from.RingStrength;to.RingSquash=from.RingSquash;}
    s.BlastPending=s.Distortion.Prepare_Blast_Warps(*Gpu,s.Processor,s.Scene,s.Presentation,s.LastView.Capture,warps,copied) && copied;
}
void PosixDevice9::Begin_Post(const RenderPost::Settings &settings)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    if (Post->Routed) Finish_Post(true);
    auto &s = *Post;
    s.Distortion.Abandon();s.HeatPending=s.BlastPending=false;
    s.InView = s.Entered = false; s.ViewDepth = 0;
    s.CurrentView = {}; s.LastView = {}; s.SoftView = {}; s.SoftRequested = false;
    s.Atmosphere.Abandon(); s.EnvironmentReady = false;
    s.EnvironmentCurrent = s.EnvironmentFresh && !settings.Classic && s.Environment.MapIdentity && s.Environment.MapGeneration;
    s.EnvironmentFresh = false;
    ParticlesAllowed = !settings.Classic;
    s.WorldAO = settings.WorldAO && !settings.Classic;
    s.ModelCurrent = {}; s.ModelReadyKey = s.ModelRefusedKey = 0;
    ModelEffectsAllowed = !settings.Classic;
    ModelEffect = 0; std::memset(ModelEffectParameters, 0, sizeof(ModelEffectParameters));
    TeamGlowDraw = false;
    TeamGlowAllowed = !settings.Classic; // do not reread TeamGlow: meshes keep their load-time markings
    s.Classic = settings.Classic; s.FoamView = {}; s.FoamRefusedKey = 0;
    s.StandInTexture = nullptr; s.StandInSurface = nullptr;
    s.Settings = {};
    if (!settings.Classic && !DX11Post_Parse_Chain(settings.Chain, s.Settings.Chain, s.Settings.Count))
        s.Settings.Count = 0;
    s.RequestedChain=s.Settings.Count!=0;
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
    if (s.ViewDepth == 8) { s.CurrentView = {}; s.InView = false; s.ModelCurrent = {}; return 0; }
    s.ModelStack[s.ViewDepth] = s.ModelCurrent;
    s.ViewStack[s.ViewDepth++] = s.CurrentView;
    const unsigned token = s.ViewDepth;
    const bool nested = s.ViewDepth > 1;
    s.CurrentView = {}; s.InView = false; s.ModelCurrent = {};
    s.CurrentView.Pass = SunActive ? SdlSceneView::Sun : reflection ? SdlSceneView::Reflection : SdlSceneView::Other;
    SdlTarget target = {}; std::string error;
    if (!eligible || reflection || SunActive || !Resolve_Target(target, error) || !target.Colour || !target.Depth ||
        !Viewport.Width || !Viewport.Height || Viewport.X > target.Width || Viewport.Y > target.Height ||
        Viewport.Width > target.Width - Viewport.X || Viewport.Height > target.Height - Viewport.Y ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane) return token;
    // The model effects own a colour-only view of their own, nested views included.
    const auto direct = Gpu->Back_Buffer_Target();
    if (ModelEffectsAllowed && ((target.Colour == direct.Colour && target.Depth == direct.Depth)
        || (s.Routed && target.Colour == s.Scene.Colour && target.Depth == s.Scene.Depth)
        || (s.StandInSurface && RenderTargets[0] == s.StandInSurface))) {
        ModelView model;
        const float modelViewport[] = {float(Viewport.X), float(Viewport.Y), float(Viewport.Width),
            float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ};
        float scale[2];
        if (render_full_texture_to_world(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11,
            target.Width, target.Height, modelViewport, model.WorldFromClip, scale)) {
            model.Target = target; model.Target.ColourFormat = Gpu->Target_Colour_Format(target);
            model.Target.DepthFormat = Gpu->Target_Depth_Format(target);
            model.Viewport = Viewport; model.View = Transforms[D3DTS_VIEW]; model.Projection = Transforms[D3DTS_PROJECTION];
            model.Frame = Gpu->Scene_Frame(); model.Generation = Gpu->Scene_Generation();
            if (++s.ModelSerial == 0) ++s.ModelSerial;
            model.Key = s.ModelSerial; model.Valid = true;
            s.ModelCurrent = model;
        }
    }
    if (nested) return token;
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
    Post->ModelCurrent=Post->ModelStack[Post->ViewDepth];
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
    // Capture at the authoritative phase, before any sorted run can write depth.
    // A later real billboard batch authorizes borrowing this immutable copy.
    if (Post) { Post->SoftView = {}; Post->SoftRequested = false; }
    if (ParticlesAllowed && Post && Post->InView) {
        SdlMainViewSnapshot depth;
        if (Read_Main_View(depth,true,SdlSceneCapturePhase::BeforeTransparent)) Post->SoftView=depth;
    }
    if (!Post_Active() || !Post->InView) return;
    // A haze-only scene also needs preparation when the Atmosphere option did not
    // request the earlier sky hook. This performs no visible sky draw at strength0.
    if (Post->EnvironmentCurrent && !Post->EnvironmentReady) Post_Sky(false);
    const bool chainAO = Post->Processor.Needs_BeforePost_Depth();
    if (!chainAO && !Post->EnvironmentReady) return;
    SdlPostSceneInputs inputs;
    if (Read_Post_Inputs(SdlSceneCapturePhase::BeforeTransparent, inputs)) {
        if (chainAO) Post->Processor.Apply_Chain_AO(*Gpu, Post->Scene, inputs);
        if (Post->EnvironmentReady) {
            Post->Atmosphere.Apply_Ground_Haze(*Gpu, Post->Scene, inputs);
            Post->Atmosphere.Apply_Atmosphere(*Gpu, Post->Scene, inputs);
        }
    }
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
    SdlPostSceneInputs inputs;
    const SdlPostSceneInputs *depth = normal && (s.Processor.Needs_BeforePost_Depth() || s.HeatPending) &&
        Read_Post_Inputs(SdlSceneCapturePhase::BeforePost,inputs) ? &inputs : nullptr;
    if(normal && s.HeatPending)s.Distortion.Apply_Heat(*Gpu,s.Scene,inputs);
    bool finished = normal ? s.Processor.Finish(*Gpu, s.Scene, s.Presentation, depth) :
        s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation);
    const bool normalFinished=normal && finished;
    if (!finished && normal) finished = s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation);
    if (!finished) std::fprintf(stderr, "PosixDevice9: main post finish failed: %s\n", s.Processor.Error().c_str());
    if(normalFinished && s.BlastPending)s.Distortion.Apply_Blast_Warp(*Gpu,s.Scene,s.Presentation);
    s.Distortion.Abandon();s.HeatPending=s.BlastPending=false;
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
    if (!Post->Distortion.Release(*Gpu)) {
        if(!teardown)return;
        Gpu->Discard_Pending_Work_For_Teardown();
        if(!Post->Distortion.Release(*Gpu))return;
    }
    if (!Post->Processor.Release(*Gpu)) {
        if (!teardown) return; // ordinary reset keeps queued borrowers alive
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Processor.Release(*Gpu)) return;
    }
    if (Post->StandInCopy) Gpu->Release_After_Batch(Post->StandInCopy, nullptr);
    if (Post->ModelColour) Gpu->Release_After_Batch(Post->ModelColour, nullptr);
    Posix_Bind(Post->Environment.Sky, static_cast<IDirect3DBaseTexture9 *>(nullptr));
    Posix_Bind(Post->Environment.Shroud, static_cast<IDirect3DBaseTexture9 *>(nullptr));
    Post.reset();
}
namespace RenderPost {
bool DistortionAvailable(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Distortion_Available(); }
void SetHeatSplats(IDirect3DDevice9 *d,const float *r,unsigned n) { if(d)static_cast<PosixDevice9 *>(d)->Set_Post_Heat(r,n); }
void SetWarps(IDirect3DDevice9 *d,const Warp *r,unsigned n) { if(d)static_cast<PosixDevice9 *>(d)->Set_Post_Warps(r,n); }
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

void PosixDevice9::Allow_Particle_Enhancements(bool allowed)
{
    ParticlesAllowed=allowed;
    if (!allowed && Post) { Post->SoftRequested=false; Post->SoftView={}; }
}
void PosixDevice9::Take_Particle_Depth()
{
    if (!Post || !ParticlesAllowed) return;
    Post->SoftRequested=true;
    SdlMainViewSnapshot view;
    if (!Read_Soft_Depth(view)) Post->SoftRequested=false;
}
bool PosixDevice9::Read_Soft_Depth(SdlMainViewSnapshot &view)
{
    view={};
    if (!Post || !ParticlesAllowed || !Post->SoftRequested || !Post->SoftView.SampledDepth) return false;
    SdlMainViewSnapshot current;
    if (!Read_Main_View(current,false,SdlSceneCapturePhase::BeforeTransparent)) return false;
    current.Capture.Depth=true;
    if (!current.Capture.Same_Identity(Post->SoftView.Capture)) return false;
    view=Post->SoftView; return true;
}
namespace RenderParticles {
void AllowEnhancements(IDirect3DDevice9 *d,bool e) { if(d) static_cast<PosixDevice9 *>(d)->Allow_Particle_Enhancements(e); }
void SetSoftParticles(IDirect3DDevice9 *d,bool e) { if(d) static_cast<PosixDevice9 *>(d)->Set_Soft_Particles(e); }
void SetFireLook(IDirect3DDevice9 *d,bool e) { if(d) static_cast<PosixDevice9 *>(d)->Set_Fire_Look(e); }
void TakeSceneDepth(IDirect3DDevice9 *d) { if(d) static_cast<PosixDevice9 *>(d)->Take_Particle_Depth(); }
}

bool PosixDevice9::Read_Particle_Context(SdlGpuParticlesContext &context)
{
    context={};
    if(!Gpu || !Mirrors) return false;
    context.Frame=Gpu; context.DeviceGeneration=Gpu->Device_Generation();
    context.FrameSerial=Gpu->Scene_Frame(); context.MirrorTexture=SdlResourceMirrors::Native_Texture_View;
    std::string error;
    if(!Resolve_Target(context.Target,error)) return true;
    const float viewport[]={float(Viewport.X),float(Viewport.Y),float(Viewport.Width),float(Viewport.Height),Viewport.MinZ,Viewport.MaxZ};
    std::memcpy(context.Viewport,viewport,sizeof(viewport));
    context.Scissor[0]=Viewport.X;context.Scissor[1]=Viewport.Y;context.Scissor[2]=Viewport.Width;context.Scissor[3]=Viewport.Height;
    if(SunActive) {context.Pass=SdlGpuParticlesPass::Sun;return true;}
    if(!ParticlesAllowed) return true; // Classic retains the original CPU picture.
    if(Post && Post->CurrentView.Pass==SdlSceneView::Reflection) {context.Pass=SdlGpuParticlesPass::Reflection;return true;}
    SdlMainViewSnapshot view;
    if(!Read_Main_View(view,false,SdlSceneCapturePhase::BeforeTransparent)) return true;
    context.Pass=SdlGpuParticlesPass::MainScene;context.MainViewKey=view.ViewKey;
    context.AdditiveGain=Post_Additive_Gain(context.Target);
    // GPU draw admission itself proves retained particles exist; it need not wait
    // for the CPU sorted-quads callback to authorize this exact phase snapshot.
    if(Post && Post->SoftView.SampledDepth && Post->SoftView.Capture.Same_Identity(view.Capture)) {
        context.SoftAllowed=true;context.SoftDepth=Post->SoftView.SampledDepth;context.SoftCapture=Post->SoftView.Capture;
    }
    return true;
}
bool PosixGpuParticles_Context(IDirect3DDevice9 *device,SdlGpuParticlesContext &context) {
    context={};return device && static_cast<PosixDevice9 *>(device)->Read_Particle_Context(context);
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
bool WorldEffectsAvailable(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Get_Gpu(); }
}

namespace RenderTeamGlow {
void SetDraw(IDirect3DDevice9 *d, bool marked) { if(d) static_cast<PosixDevice9 *>(d)->Set_Team_Glow(marked); }
bool Available(IDirect3DDevice9 *d) {
    auto *frame = d ? static_cast<PosixDevice9 *>(d)->Get_Gpu() : nullptr;
    return frame && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
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

bool PosixDevice9::Post_Environment_Available() const { return Gpu && Gpu->Supports_Scene_Depth(); }
namespace RenderPost {
bool EnvironmentAvailable(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Environment_Available(); }
void SetEnvironment(IDirect3DDevice9 *d,const Environment &e) { if(d) static_cast<PosixDevice9 *>(d)->Set_Post_Environment(e); }
void Sky(IDirect3DDevice9 *d) { if(d) static_cast<PosixDevice9 *>(d)->Post_Sky(); }
}
