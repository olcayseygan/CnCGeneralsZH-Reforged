// Render-only sun capture. The snapshot owns COM references across caster queue flushes.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlConstants.h"
#include "SdlPipelineCache.h"
#include "SdlRenderMatrix.h"
#include "engineshader.h"
#include "Platform/SunShadow.h"
#include "Platform/SunSmoke.h"
#include "ffshader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

// One list for declaration, capture and restoration, including shader register banks.
#define SUN_VALUES(X) \
    X(RenderStates) X(TextureStageStates) X(SamplerStates) X(Transforms) X(Viewport) \
    X(Material) X(Lights) X(LightsEnabled) X(ClipPlanes) X(StreamOffsets) X(StreamStrides) \
    X(DeclarationIsCurrent) X(FVF) X(VertexShaderConstants) X(PixelShaderConstants) X(SmokeGlow)
#define SUN_REFS(X) X(DepthStencil) X(Indices) X(Declaration) X(VertexShader) X(PixelShader)
#define SUN_REF_ARRAYS(X) X(RenderTargets) X(Textures) X(Streams)

struct PosixDevice9::SunCapture {
#define FIELD(name) decltype(PosixDevice9::name) name = {};
    SUN_VALUES(FIELD) SUN_REFS(FIELD) SUN_REF_ARRAYS(FIELD)
#undef FIELD
    SdlTarget Target = {};
    float ViewProjection[16] = {};
    unsigned FirstDraw = 0, Draws = 0;
    bool Receiving = false;
    bool SmokeFilled = false;
    float SmokeStrength = 0;
    uint64_t SceneGeneration = 0;
    SdlTarget Scene = {};
    D3DMATRIX SceneView = {}, SceneProjection = {};
    D3DVIEWPORT9 SceneViewport = {};
    float FromClip[16] = {}, WorldFromClip[16] = {}, Parameters[4] = {}, Softness[4] = {};
    SDL_GPUSampler *Sampler = nullptr; // owned by the device's sampler cache
    explicit SunCapture(PosixDevice9 &device) {
#define COPY(name) std::memcpy(&name, &device.name, sizeof(name));
        SUN_VALUES(COPY)
#undef COPY
#define HOLD(name) Posix_Bind(name, device.name);
#define HOLDS(name) for (unsigned i = 0; i < sizeof(name)/sizeof(name[0]); ++i) Posix_Bind(name[i], device.name[i]);
        SUN_REFS(HOLD) SUN_REF_ARRAYS(HOLDS)
#undef HOLD
#undef HOLDS
    }
    void Restore(PosixDevice9 &device) {
#define COPY(name) std::memcpy(&device.name, &name, sizeof(name));
        SUN_VALUES(COPY)
#undef COPY
#define BIND(name) Posix_Bind(device.name, name);
#define BINDS(name) for (unsigned i = 0; i < sizeof(name)/sizeof(name[0]); ++i) Posix_Bind(device.name[i], name[i]);
        SUN_REFS(BIND) SUN_REF_ARRAYS(BINDS)
#undef BIND
#undef BINDS
        Release();
    }
    void Release() {
#define DROP(name) if (name) { name->Release(); name = nullptr; }
#define DROPS(name) for (auto &object : name) if (object) { object->Release(); object = nullptr; }
        SUN_REFS(DROP) SUN_REF_ARRAYS(DROPS)
#undef DROP
#undef DROPS
    }
    ~SunCapture() { Release(); }
};
#undef SUN_VALUES
#undef SUN_REFS
#undef SUN_REF_ARRAYS

bool PosixDevice9::Begin_Sun_Shadow(unsigned size)
{
    // Refusing re-entry must leave the outer capture intact.
    if (SunActive || SunSceneExclusions) return false;
    SunReady = false;
    if (!Gpu || !SunReceiverFailure.empty()) return false;
    SdlTarget target;
    std::string error;
    if (!Gpu->Sun_Shadow_Target(size, target, error)) {
        if (SunCaptureFailure != error)
            fprintf(stderr,"SunShadow: capture unavailable (%s); legacy shadows\n",error.c_str());
        SunCaptureFailure = error;
        return false;
    }
    SunCaptureFailure.clear();
    if (!SunSceneDepth) SunReceiversAtSceneStart = SunSolidReceivers + SunGroundReceivers;
    Sun = std::make_shared<SunCapture>(*this);
    Sun->Target = target;
    Sun->FirstDraw = DrawsRecorded;
    SunActive = true;
    SunCasterEffect = DRAW_EFFECT_NONE;
    SunFailed = false;
    Viewport = {0, 0, size, size, 0.0f, 1.0f};
    Gpu->Set_Target(target);
    Gpu->Clear_Back_Buffer(false, true, false, 0, 1.0f, 0);
    return true;
}

bool PosixDevice9::Sun_Target(SdlTarget &target) const
{
    if (!SunActive || !Sun) return false;
    target = Sun->Target;
    return true;
}

bool PosixDevice9::End_Sun_Shadow(bool *submission_ok)
{
    if (submission_ok) *submission_ok = false;
    if (!SunActive || !Sun) return false;
    const float *view = &Transforms[D3DTS_VIEW]._11;
    const float *projection = &Transforms[D3DTS_PROJECTION]._11;
    multiply(view, projection, Sun->ViewProjection);
    // Capture uses the D3D9 viewport displaced by +0.5 pixels. Convert nominal sun
    // clip coordinates to the texel centers actually written by that viewport.
    for (unsigned row=0; row<4; ++row) {
        Sun->ViewProjection[row*4] += Sun->ViewProjection[row*4+3] / Sun->Target.Width;
        Sun->ViewProjection[row*4+1] -= Sun->ViewProjection[row*4+3] / Sun->Target.Height;
    }
    Sun->Draws = DrawsRecorded - Sun->FirstDraw;
    // Submission failure must reach the stencil fallback before the engine decides who owns shadows.
    const bool submitted = Gpu->Flush();
    SunActive = false;
    SunCasterEffect = DRAW_EFFECT_NONE;
    Sun->Restore(*this);
    SdlTarget scene;
    std::string error;
    const bool restored = Resolve_Target(scene, error);
    if (restored) Gpu->Set_Target(scene);
    if (submission_ok) *submission_ok = submitted;
    SunReady = submitted && restored && !SunFailed;
    return SunReady;
}

void PosixDevice9::Clear_Sun_Shadow()
{
    if (SunActive) End_Sun_Shadow();
    SunReady = false;
}

bool PosixDevice9::Fill_Sun_Smoke(const float *casters, unsigned count, float strength)
{
    if (!Gpu || SunActive || SunSceneExclusions) return false;
    if (Sun) Sun->SmokeFilled = false;
    if (!casters || !count) return Gpu->Fill_Sun_Smoke(nullptr,nullptr,0);
    if (!Sun || !SunReady || !std::isfinite(strength) || strength<0 || strength>1) return false;
    if (!Gpu->Fill_Sun_Smoke(Sun->ViewProjection,casters,count)) return false;
    Sun->SmokeFilled = Gpu->Sun_Smoke_Texture()!=nullptr;
    Sun->SmokeStrength = strength;
    return true;
}

void PosixDevice9::Set_Sun_Caster_Effect(unsigned effect, const float parameters[4])
{
    // Cosmetic scene effects need their own capture path. This pass implements only the
    // Windows cloak's shadow coverage, independently of Direct3D11_Draw_Effects().
    SunCasterEffect = SunActive && effect==DRAW_EFFECT_CLOAK_SHADOW ? effect : DRAW_EFFECT_NONE;
    SunCasterOpacity = parameters && std::isfinite(parameters[3]) ? std::max(0.0f,std::min(1.0f,parameters[3])) : 1.0f;
}

void PosixDevice9::Get_Sun_Caster_Effect(unsigned &effect, float parameters[4]) const
{
    effect = SunActive ? SunCasterEffect : DRAW_EFFECT_NONE;
    std::fill(parameters, parameters + 4, 0.0f);
    if (effect != DRAW_EFFECT_NONE) parameters[3] = SunCasterOpacity;
}

bool PosixDevice9::Read_Sun_Shadow(std::vector<float> &depth)
{
    depth.clear();
    return SunReady && Sun && Gpu->Read_Depth(Sun->Target.Depth, Sun->Target.Width, Sun->Target.Height, depth);
}

std::string PosixDevice9::Sun_Shadow_Report()
{
    std::vector<float> depth;
    if (!Read_Sun_Shadow(depth)) return "SDL sun capture unavailable" +
        (SunReceiverFailure.empty() ? std::string() : "; legacy fallback: " + SunReceiverFailure);
    unsigned covered=0;
    float nearest=1.0f, farthest=0.0f;
    for (float d : depth) if (d < 1.0f) {
        ++covered; nearest=std::min(nearest,d); farthest=std::max(farthest,d);
    }
    char line[360];
    std::snprintf(line,sizeof(line),"SDL sun %ux%u, %u draws, %u/%zu texels occupied, depth %.6f..%.6f, receiving=%u, cumulative receivers solid=%llu ground=%llu",
        Sun->Target.Width,Sun->Target.Height,Sun->Draws,covered,depth.size(),nearest,farthest,
        unsigned(Sun->Receiving && SunReceiverFailure.empty()),(unsigned long long)SunSolidReceivers,(unsigned long long)SunGroundReceivers);
    return std::string(line)+", smoke casters="+std::to_string(Sun->SmokeFilled ? Gpu->Sun_Smoke_Count() : 0)
        +", cumulative camera-space="+std::to_string(SunCameraReceivers)
        +" alpha="+std::to_string(SunAlphaReceivers)+" cloak casters="+std::to_string(SunCloakCasters);
}

bool PosixDevice9::Set_Sun_Shadow_Parameters(float bias, float strength, float widest, float narrowest,
    float texelsPerGap, float unitsPerDepth, float skyFill)
{
    if (!Sun || SunSceneExclusions || !SunReceiverFailure.empty()) return false;
    Sun->Receiving = false;
    if (!SunReady || SunActive) return false;
    const float numbers[] = {bias,strength,widest,narrowest,texelsPerGap,unitsPerDepth,skyFill};
    for (float value : numbers) if (!std::isfinite(value)) return false;
    if (bias<0 || strength<0 || strength>1 || narrowest<0 || widest<narrowest || texelsPerGap<0
        || unitsPerDepth<=0 || skyFill<0 || skyFill>1 || !Viewport.Width || !Viewport.Height) return false;
    std::string error;
    if (!Resolve_Target(Sun->Scene,error) || !Sun->Scene.Colour || !Sun->Scene.Depth) return false;
    float sceneClip[16], inverse[16];
    multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,sceneClip);
    if (!render_inverse(sceneClip,inverse)) return false;
    std::memcpy(Sun->WorldFromClip,inverse,sizeof(inverse));
    multiply(inverse,Sun->ViewProjection,Sun->FromClip);
    const float depthRange = Viewport.MaxZ - Viewport.MinZ;
    if (depthRange <= 1e-8f) return false;
    for (unsigned col=0; col<4; ++col) {
        const float z = Sun->FromClip[8+col] / depthRange;
        Sun->FromClip[12+col] -= Viewport.MinZ*z;
        Sun->FromClip[8+col] = z;
    }
    RenderUInt32 sampler[SAMPLER_STATE_COUNT] = {};
    sampler[D3DSAMP_ADDRESSU] = sampler[D3DSAMP_ADDRESSV] = sampler[D3DSAMP_ADDRESSW] = D3DTADDRESS_CLAMP;
    sampler[D3DSAMP_MINFILTER] = sampler[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
    sampler[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
    sampler[D3DSAMP_MAXANISOTROPY] = 1;
    Sun->Sampler = Samplers->Sampler(sampler);
    if (!Sun->Sampler) return false;
    Sun->SceneGeneration = SunSceneGeneration;
    Sun->SceneView = Transforms[D3DTS_VIEW];
    Sun->SceneProjection = Transforms[D3DTS_PROJECTION];
    Sun->SceneViewport = Viewport;
    const float parameters[] = {1.0f/Sun->Target.Width,bias,strength,widest};
    const float softness[] = {narrowest,texelsPerGap,unitsPerDepth,skyFill};
    std::memcpy(Sun->Parameters,parameters,sizeof(parameters));
    std::memcpy(Sun->Softness,softness,sizeof(softness));
    Sun->Receiving = true;
    return true;
}

void PosixDevice9::Fail_Sun_Receiver(const char *reason)
{
    if (!SunReceiverFailure.empty()) return;
    SunReceiverFailure = reason;
    const bool drawn = SunSolidReceivers + SunGroundReceivers != SunReceiversAtSceneStart;
    // Already-submitted mapped draws cannot be undone here. Do not add stencil
    // darkening over them; retain this scene's ownership and use legacy next scene.
    if (Sun && !drawn) Sun->Receiving = false;
    fprintf(stderr,"SunShadow: receiver failure (%s); ordinary draw retry, legacy shadows %s\n",
        reason,drawn ? "from next scene" : "in this scene");
    // Failed program/pipeline keys remain cached, so the latch lasts for the device.
}

bool PosixDevice9::Sun_Camera_Space() const
{
    static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    return Transforms[D3DTS_PROJECTION]._34!=0
        && std::memcmp(&Transforms[D3DTS_VIEW],identity,sizeof(identity))==0;
}

bool PosixDevice9::Sun_Receives_Target(const SdlTarget &target) const
{
    return Sun && SunReady && Sun->Receiving && !SunActive && !SunSceneExclusions
        && target.Colour == Sun->Scene.Colour && target.Depth == Sun->Scene.Depth
        && target.Width == Sun->Scene.Width && target.Height == Sun->Scene.Height
        && std::memcmp(&Viewport,&Sun->SceneViewport,sizeof(Viewport)) == 0
        && (Sun_Camera_Space() || std::memcmp(&Transforms[D3DTS_VIEW],&Sun->SceneView,sizeof(D3DMATRIX)) == 0)
        && std::memcmp(&Transforms[D3DTS_PROJECTION],&Sun->SceneProjection,sizeof(D3DMATRIX)) == 0;
}

bool PosixDevice9::Sun_Receives_View()
{
    if (!Gpu || !Sun || !SunReady || !SunReceiverFailure.empty()) return false;
    SdlTarget target;
    std::string error;
    return Resolve_Target(target,error) && Sun_Receives_Target(target);
}

bool PosixDevice9::Enter_Sun_Scene(bool eligible)
{
    const bool owns = eligible && SunSceneDepth == 0;
    ++SunSceneDepth;
    if (owns) {
        Clear_Sun_Shadow();
        SunReceiversAtSceneStart = SunSolidReceivers + SunGroundReceivers;
        ++SunSceneGeneration;
    } else ++SunSceneExclusions;
    return owns;
}

void PosixDevice9::Leave_Sun_Scene(bool eligible)
{
    if (!SunSceneDepth) return;
    if (!eligible && SunSceneExclusions) --SunSceneExclusions;
    --SunSceneDepth;
    if (eligible) Clear_Sun_Shadow(); // recorded draws already own their constants/bindings
}

bool PosixDevice9::Sun_Owns_Scene_Shadows()
{
    SdlTarget target;
    std::string error;
    if (!SunSceneDepth) // direct-device callers retain the same late-failure rule
        return Gpu && Sun && SunReady && Resolve_Target(target,error) && Sun_Receives_Target(target);
    return Sun && SunReady && Sun->Receiving && !SunActive && !SunSceneExclusions
        && Sun->SceneGeneration == SunSceneGeneration
        && Resolve_Target(target,error)
        && target.Colour == Sun->Scene.Colour && target.Depth == Sun->Scene.Depth
        && target.Width == Sun->Scene.Width && target.Height == Sun->Scene.Height
        && std::memcmp(&Viewport,&Sun->SceneViewport,sizeof(Viewport)) == 0;
}

void PosixDevice9::Exclude_Sun_Receivers(bool exclude)
{
    if (exclude) ++SunExclusions;
    else if (SunExclusions) --SunExclusions;
}

bool PosixDevice9::Sun_Receiving(int vertexProgram, int pixelProgram, const SdlTarget &target) const
{
    if (SunExclusions || !SunReceiverFailure.empty() || !Sun_Receives_Target(target) || (FVF & D3DFVF_XYZRHW)
        || !RenderStates[D3DRS_ZENABLE] || RenderStates[D3DRS_FILLMODE] != D3DFILL_SOLID
        || (RenderStates[D3DRS_COLORWRITEENABLE] & 7u) != 7u) return false;
    const bool ground = EngineShader_Paints_Ground(static_cast<EngineShaderProgram>(pixelProgram));
    if (pixelProgram != ENGINE_SHADER_NONE && !ground) return false;
    if (vertexProgram != ENGINE_SHADER_NONE && vertexProgram != ENGINE_SHADER_TREES) return false;
    // Match Windows: ordinary alpha surfaces/particles receive even without depth writes.
    // Additive light and multiplicative overlays retain their authored contribution.
    if (RenderStates[D3DRS_ALPHABLENDENABLE]) {
        const unsigned source=RenderStates[D3DRS_SRCBLEND], destination=RenderStates[D3DRS_DESTBLEND];
        if (source==D3DBLEND_ONE || destination==D3DBLEND_ONE || source==D3DBLEND_DESTCOLOR
            || destination==D3DBLEND_DESTCOLOR || destination==D3DBLEND_SRCCOLOR
            || destination==D3DBLEND_INVSRCCOLOR) return false;
    }
    return true;
}

void PosixDevice9::Build_Sun_Constants(SdlEnginePixelConstants &constants) const
{
    std::memcpy(constants.ShadowFromClip,Sun->FromClip,sizeof(Sun->FromClip));
    std::memcpy(constants.ShadowParameters,Sun->Parameters,sizeof(Sun->Parameters));
    std::memcpy(constants.ShadowSoftness,Sun->Softness,sizeof(Sun->Softness));
    constants.ShadowViewport[0] = 1.0f/Viewport.Width;
    constants.ShadowViewport[1] = 1.0f/Viewport.Height;
    constants.ShadowViewport[2] = Viewport.X+0.5f;
    constants.ShadowViewport[3] = Viewport.Y+0.5f;
    constants.VolumeParameters[0] = Sun->SmokeFilled ? Sun->SmokeStrength : 0;
    constants.VolumeParameters[1] = Sun->SmokeFilled ? SunSmoke::SelfGain : 0;
    constants.VolumeParameters[2] = Sun_Camera_Space() ? 1 : 0;
    constants.VolumeParameters[3] = SunSmoke::SelfCurve;
    std::memcpy(constants.WorldFromClip,Sun->WorldFromClip,sizeof(Sun->WorldFromClip));
    // Headlight/Blast counts stay zero. Linux still supplies its existing vertex lights.
}
SDL_GPUTexture *PosixDevice9::Sun_Texture() const { return Sun ? Sun->Target.Depth : nullptr; }
SDL_GPUSampler *PosixDevice9::Sun_Sampler() const { return Sun ? Sun->Sampler : nullptr; }

namespace SunShadow {
bool FillSmoke(IDirect3DDevice9 *d,const float *c,unsigned n,float s) { return d && static_cast<PosixDevice9*>(d)->Fill_Sun_Smoke(c,n,s); }
bool CasterEffects(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9*>(d)->Get_Gpu(); }
void SetCasterEffect(IDirect3DDevice9 *d,unsigned e,const float p[4]) { if(d) static_cast<PosixDevice9*>(d)->Set_Sun_Caster_Effect(e,p); }
CasterEffect GetCasterEffect(IDirect3DDevice9 *d) {
    CasterEffect effect;
    if (d) static_cast<PosixDevice9*>(d)->Get_Sun_Caster_Effect(effect.Effect, effect.Parameters);
    return effect;
}
void SetSmokeGlow(IDirect3DDevice9 *d,bool g) { if(d) static_cast<PosixDevice9*>(d)->Set_Smoke_Glow(g); }
bool Begin(IDirect3DDevice9 *device, unsigned size)
{
#if defined(__linux__)
    return device && static_cast<PosixDevice9 *>(device)->Begin_Sun_Shadow(size);
#else
    return false; // Preserve the existing macOS scene shadows.
#endif
}
bool End(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9*>(d)->End_Sun_Shadow(); }
void Clear(IDirect3DDevice9 *d) {
    // A nested view's renderShadowMap starts with Clear, even when Begin will be
    // refused. Leave the owning scene's map intact; reset/destruction clear directly.
    if (d && !static_cast<PosixDevice9*>(d)->Sun_Scene_Excluded())
        static_cast<PosixDevice9*>(d)->Clear_Sun_Shadow();
}
bool Parameters(IDirect3DDevice9 *d,float b,float s,float w,float n,float t,float u,float f) {
    return d && static_cast<PosixDevice9*>(d)->Set_Sun_Shadow_Parameters(b,s,w,n,t,u,f);
}
bool ReceivesView(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9*>(d)->Sun_Receives_View(); }
bool OwnsSceneShadows(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9*>(d)->Sun_Owns_Scene_Shadows(); }
bool EnterScene(IDirect3DDevice9 *d,bool eligible) { return d && static_cast<PosixDevice9*>(d)->Enter_Sun_Scene(eligible); }
void LeaveScene(IDirect3DDevice9 *d,bool eligible) { if (d) static_cast<PosixDevice9*>(d)->Leave_Sun_Scene(eligible); }
void ExcludeReceivers(IDirect3DDevice9 *d,bool exclude) { if (d) static_cast<PosixDevice9*>(d)->Exclude_Sun_Receivers(exclude); }
bool ReceiversExcluded(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9*>(d)->Sun_Receivers_Excluded(); }
std::string Report(IDirect3DDevice9 *d) { return d ? static_cast<PosixDevice9*>(d)->Sun_Shadow_Report() : "no device"; }
}
