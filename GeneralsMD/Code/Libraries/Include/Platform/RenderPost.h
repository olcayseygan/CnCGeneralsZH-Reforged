// Explicit main-scene post lifecycle. Other render targets and HUD retain their existing paths.
#pragma once
#include <cstdint>
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
struct IDirect3DSurface9;
namespace RenderPost {
struct Settings {
    const char *Chain = "off";
    bool Classic = false, InternalHDR = false, WorldAO = false;
    float BloomThreshold = 1, BloomIntensity = 0.75f, BloomSaturation = 1;
    float Clarity = 0, AdditiveGain = 1, Grade = 0;
    float ClearAlpha = 1; // same main-scene clear alpha used by Begin_Render
    unsigned BlurPasses = 2;
};
struct GroundHazeSettings {
    float Colour[3] = {}, Top = 0, Thickness = 1, Strength = 0, Reach = 1, WaterHeight = -1.0e6f;
    float TowardSun[3] = {0,0,1}, Warmth = 0;
};
struct AtmosphereSettings {
    float Horizon[3] = {}, Tint[3] = {1,1,1}, Start = 0, Scale = 1, Strength = 0, Ground = 0, MapSize[2] = {};
};
struct Environment {
    std::uint64_t MapIdentity = 0, MapGeneration = 0;
    GroundHazeSettings Haze;
    AtmosphereSettings Atmosphere;
    IDirect3DBaseTexture9 *Sky = nullptr, *Shroud = nullptr;
    float ShroudRect[4] = {0,0,1,1};
};
// Copies values and retains texture references; consume once in the following display pass.
void SetEnvironment(const Environment &);
bool EnvironmentAvailable(); // stable hardware capability, independent of current scene
void Sky();
void BeginMain(const Settings &);
unsigned BeginView(bool eligible, float nearPlane, float farPlane, bool reflection = false);
void EndView(unsigned token);
void OpaqueScene(bool allowWorldAO); // explicit caller gate, before water
void BeforeTransparent();
void FinishScene();
void FinishFrame();
bool ReplacesLegacyBloom();
void SetSceneStandIn(IDirect3DBaseTexture9 *, IDirect3DSurface9 *);
void SetGlowDraw(bool);
class ViewScope {
    unsigned Token;
public:
    ViewScope(bool e, float n, float f, bool reflection = false) : Token(BeginView(e,n,f,reflection)) {}
    ~ViewScope() { EndView(Token); }
    ViewScope(const ViewScope &) = delete;
    ViewScope &operator=(const ViewScope &) = delete;
};
#if !defined(_WIN32)
void BeginMain(IDirect3DDevice9 *, const Settings &);
void SetEnvironment(IDirect3DDevice9 *, const Environment &);
bool EnvironmentAvailable(IDirect3DDevice9 *);
void Sky(IDirect3DDevice9 *);
unsigned BeginView(IDirect3DDevice9 *, bool, float, float, bool);
void EndView(IDirect3DDevice9 *, unsigned);
void OpaqueScene(IDirect3DDevice9 *, bool);
void BeforeTransparent(IDirect3DDevice9 *);
void FinishScene(IDirect3DDevice9 *, bool);
bool ReplacesLegacyBloom(IDirect3DDevice9 *);
void SetSceneStandIn(IDirect3DDevice9 *, IDirect3DBaseTexture9 *, IDirect3DSurface9 *);
void SetGlowDraw(IDirect3DDevice9 *, bool);
#endif
}
