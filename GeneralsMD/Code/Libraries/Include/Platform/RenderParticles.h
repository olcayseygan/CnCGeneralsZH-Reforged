// Native sorted-billboard policy. Backend identity and user options remain separate.
#pragma once
namespace RenderParticles {
void AllowEnhancements(bool allowed); // Classic keeps the original hard/plain picture
void SetSoftParticles(bool enabled);
void SetFireLook(bool enabled);
void TakeSceneDepth(); // only a real billboard batch authorizes the phase-owned depth
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderParticles {
void AllowEnhancements(IDirect3DDevice9 *, bool);
void SetSoftParticles(IDirect3DDevice9 *, bool);
void SetFireLook(IDirect3DDevice9 *, bool);
void TakeSceneDepth(IDirect3DDevice9 *);
}
#endif
