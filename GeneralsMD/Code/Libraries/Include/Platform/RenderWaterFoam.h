// Classic-water foam capability is independent of current view readiness.
#pragma once
namespace RenderWaterFoam { bool Available(); bool Ready(); }
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderWaterFoam { bool Available(IDirect3DDevice9 *); bool Ready(IDirect3DDevice9 *); }
#endif
