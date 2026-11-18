// Explicit main-scene post lifecycle. Other render targets and HUD retain their existing paths.
#pragma once
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
struct IDirect3DSurface9;
namespace RenderPost {
struct Settings {
    const char *Chain = "off";
    bool Classic = false, InternalHDR = false;
    float BloomThreshold = 1, BloomIntensity = 0.75f, BloomSaturation = 1;
    float Clarity = 0, AdditiveGain = 1, Grade = 0;
    float ClearAlpha = 1; // same main-scene clear alpha used by Begin_Render
    unsigned BlurPasses = 2;
};
void BeginMain(const Settings &);
unsigned BeginView(bool eligible, float nearPlane, float farPlane, bool reflection = false);
void EndView(unsigned token);
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
unsigned BeginView(IDirect3DDevice9 *, bool, float, float, bool);
void EndView(IDirect3DDevice9 *, unsigned);
void FinishScene(IDirect3DDevice9 *, bool);
bool ReplacesLegacyBloom(IDirect3DDevice9 *);
void SetSceneStandIn(IDirect3DDevice9 *, IDirect3DBaseTexture9 *, IDirect3DSurface9 *);
void SetGlowDraw(IDirect3DDevice9 *, bool);
#endif
}
