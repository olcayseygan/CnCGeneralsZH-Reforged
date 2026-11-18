// Shared enhancement dispatch. Queries inspect live backend readiness;
// these calls carry scene/draw policy and never change backend identity.
#pragma once
#ifndef RENDERLIGHTING_H
#define RENDERLIGHTING_H
namespace RenderLighting {
void SetHeadlights(const float *lights, unsigned count, const float gain[3]);
void SetBlastLights(const float *lights, unsigned count);
// Scene readiness, not an instruction to suppress every object's legacy vertex lights.
bool LightsPerPixel();
// Commits this main terrain view's CPU/pixel choice before tile relighting.
// A late optional-shadow failure cannot change the choice for already prepared ground.
bool PrepareTerrainLighting();
void UseTerrainCpuLighting();
void SetHemiAmbient(bool enabled);
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderLighting {
void SetHeadlights(IDirect3DDevice9 *, const float *, unsigned, const float[3]);
void SetBlastLights(IDirect3DDevice9 *, const float *, unsigned);
bool LightsPerPixel(IDirect3DDevice9 *);
bool PrepareTerrainLighting(IDirect3DDevice9 *);
void UseTerrainCpuLighting(IDirect3DDevice9 *);
void SetHemiAmbient(IDirect3DDevice9 *, bool);
// Pure, bounded environment packets. Types are backend-independent.
enum class LightType { Point, Spot, Directional };
struct VertexLight {
    LightType Type;
    float Diffuse[4], Specular[4], Ambient[4];
    float Position[3], Direction[3];
    float Range, Falloff, Attenuation[3], Theta, Phi;
};
struct VertexEnvironment {
    unsigned Count, Ambient;
    VertexLight Lights[4];
};
void SetEnvironments(IDirect3DDevice9 *, const VertexEnvironment &full, const VertexEnvironment *staticOnly);

}
#endif
#endif
