// Shared per-model cosmetic dispatch. Support is independent of options and current drawing.
#pragma once
namespace RenderEffects {
void AllowDrawEffects(bool allowed);
bool DrawEffectsAvailable();
bool TakesDrawEffect(unsigned effect);
void SetDrawEffect(unsigned effect, const float parameters[4]);
unsigned RequestedDrawEffect(float parameters[4]);
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderEffects {
void AllowDrawEffects(IDirect3DDevice9 *, bool);
bool DrawEffectsAvailable(IDirect3DDevice9 *);
bool TakesDrawEffect(IDirect3DDevice9 *, unsigned);
void SetDrawEffect(IDirect3DDevice9 *, unsigned, const float[4]);
unsigned RequestedDrawEffect(IDirect3DDevice9 *, float[4]);
}
#endif
