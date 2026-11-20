// Shared per-model cosmetic dispatch. Support is independent of options and current drawing.
#pragma once
namespace RenderEffects {
void AllowDrawEffects(bool allowed);
bool DrawEffectsAvailable();
bool TakesDrawEffect(unsigned effect);
void SetDrawEffect(unsigned effect, const float parameters[4]);
unsigned RequestedDrawEffect(float parameters[4]);
// Battle marks, Classic water splashes and the building collapse: plain draws, so the Direct3D 11
// picture on Windows and any live SDL GPU device elsewhere. Not under -d3d9 or headless.
bool WorldEffectsAvailable();
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderEffects {
void AllowDrawEffects(IDirect3DDevice9 *, bool);
bool DrawEffectsAvailable(IDirect3DDevice9 *);
bool TakesDrawEffect(IDirect3DDevice9 *, unsigned);
void SetDrawEffect(IDirect3DDevice9 *, unsigned, const float[4]);
unsigned RequestedDrawEffect(IDirect3DDevice9 *, float[4]);
bool WorldEffectsAvailable(IDirect3DDevice9 *);
}
#endif
