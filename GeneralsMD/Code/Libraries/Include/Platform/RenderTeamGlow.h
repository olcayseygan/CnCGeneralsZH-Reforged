// Backend-neutral team-color draw marker. Saved mesh markings retain their next-map policy.
#pragma once
namespace RenderTeamGlow {
void SetDraw(bool marked);
bool Available();
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderTeamGlow {
void SetDraw(IDirect3DDevice9 *, bool marked);
bool Available(IDirect3DDevice9 *);
}
#endif
