// Scene light packets and immutable alternatives to legacy vertex lighting.
#include "PosixDevice9.h"
#include "SdlConstants.h"
#include "ffshader.h"
#include <algorithm>
#include <cstring>

void PosixDevice9::Set_Headlights(const float *lights, unsigned count, const float gain[3])
{
    HeadlightCount = lights && gain ? std::min(count, unsigned(HEADLIGHT_SLOTS)) : 0;
    if (HeadlightCount) {
        std::memcpy(Headlights, lights, HeadlightCount * sizeof(Headlights[0]));
        std::memcpy(HeadlightGain, gain, sizeof(HeadlightGain));
    } else std::memset(HeadlightGain, 0, sizeof(HeadlightGain));
}
void PosixDevice9::Set_Blast_Lights(const float *lights, unsigned count)
{
    BlastLightCount = lights ? std::min(count, unsigned(BLAST_LIGHT_SLOTS)) : 0;
    if (BlastLightCount) std::memcpy(BlastLights, lights, BlastLightCount * sizeof(BlastLights[0]));
}
bool PosixDevice9::Lights_Per_Pixel()
{
    return Gpu && Sun_Owns_Scene_Shadows();
}
static D3DLIGHT9 native_light(const RenderLighting::VertexLight &source)
{
    D3DLIGHT9 result = {};
    result.Type=source.Type==RenderLighting::LightType::Point ? D3DLIGHT_POINT
        : source.Type==RenderLighting::LightType::Spot ? D3DLIGHT_SPOT : D3DLIGHT_DIRECTIONAL;
    std::memcpy(&result.Diffuse,source.Diffuse,sizeof(source.Diffuse));
    std::memcpy(&result.Specular,source.Specular,sizeof(source.Specular));
    std::memcpy(&result.Ambient,source.Ambient,sizeof(source.Ambient));
    std::memcpy(&result.Position,source.Position,sizeof(source.Position));
    std::memcpy(&result.Direction,source.Direction,sizeof(source.Direction));
    result.Range=source.Range;result.Falloff=source.Falloff;
    result.Attenuation0=source.Attenuation[0];result.Attenuation1=source.Attenuation[1];result.Attenuation2=source.Attenuation[2];
    result.Theta=source.Theta;result.Phi=source.Phi;
    return result;
}
void PosixDevice9::Set_Light_Environments(const RenderLighting::VertexEnvironment &full,
    const RenderLighting::VertexEnvironment *staticOnly)
{
    StaticLightingAvailable=staticOnly && full.Count<=4 && staticOnly->Count<=4;
    if (!StaticLightingAvailable) return;
    FullEnvironmentCount=full.Count;
    FullEnvironmentAmbient=full.Ambient;
    StaticAmbient=staticOnly->Ambient;
    std::memset(StaticLightsEnabled,0,sizeof(StaticLightsEnabled));
    for (unsigned i=0;i<full.Count;++i) FullEnvironmentLights[i]=native_light(full.Lights[i]);
    for (unsigned i=0;i<staticOnly->Count;++i) {
        StaticLights[i]=native_light(staticOnly->Lights[i]);
        StaticLightsEnabled[i]=true;
    }
}
bool PosixDevice9::Static_Lighting_Matches() const
{
    if (!StaticLightingAvailable || RenderStates[D3DRS_AMBIENT]!=FullEnvironmentAmbient) return false;
    for (unsigned i=0;i<LIGHT_COUNT;++i) {
        if (LightsEnabled[i] != (i<FullEnvironmentCount)) return false;
        if (i<FullEnvironmentCount && std::memcmp(&Lights[i],&FullEnvironmentLights[i],sizeof(Lights[i]))) return false;
    }
    return true;
}
void PosixDevice9::Build_Light_Constants(SdlEnginePixelConstants &constants, bool pointLights) const
{
    constants.HeadlightParameters[0] = float(HeadlightCount);
    std::memcpy(constants.HeadlightParameters + 1, HeadlightGain, sizeof(HeadlightGain));
    for (unsigned i=0; i<HeadlightCount; ++i) {
        std::memcpy(constants.HeadlightPosition[i], Headlights[i], 4*sizeof(float));
        std::memcpy(constants.HeadlightDirection[i], Headlights[i]+4, 4*sizeof(float));
    }
    // Camera-space smoke already receives CPU Firelight. Multiplicative overlays inherit
    // the base pass's illumination. Neither may gain the point lights a second time.
    const bool multiply = RenderStates[D3DRS_ALPHABLENDENABLE]
        && RenderStates[D3DRS_SRCBLEND] == D3DBLEND_ZERO
        && RenderStates[D3DRS_DESTBLEND] == D3DBLEND_SRCCOLOR;
    const unsigned count = !pointLights || Sun_Camera_Space() || multiply ? 0 : BlastLightCount;
    constants.BlastLightParameters[0] = float(count);
    for (unsigned i=0; i<count; ++i) {
        std::memcpy(constants.BlastLightPosition[i], BlastLights[i], 4*sizeof(float));
        std::memcpy(constants.BlastLightDiffuse[i], BlastLights[i]+4, 4*sizeof(float));
        std::memcpy(constants.BlastLightAmbient[i], BlastLights[i]+8, 4*sizeof(float));
    }
}

namespace RenderLighting {
static PosixDevice9 *native(IDirect3DDevice9 *device) { return static_cast<PosixDevice9 *>(device); }
void SetHeadlights(IDirect3DDevice9 *d,const float *l,unsigned n,const float g[3]) { if(d) native(d)->Set_Headlights(l,n,g); }
void SetBlastLights(IDirect3DDevice9 *d,const float *l,unsigned n) { if(d) native(d)->Set_Blast_Lights(l,n); }
bool LightsPerPixel(IDirect3DDevice9 *d) { return d && native(d)->Lights_Per_Pixel(); }
bool PrepareTerrainLighting(IDirect3DDevice9 *d) { return d && native(d)->Commit_Terrain_Lighting(); }
void UseTerrainCpuLighting(IDirect3DDevice9 *d) { if(d) native(d)->Use_Terrain_Cpu_Lighting(); }
void SetEnvironments(IDirect3DDevice9 *d,const VertexEnvironment &full,const VertexEnvironment *alternate)
{ if(d) native(d)->Set_Light_Environments(full,alternate); }
}
bool PosixPerPixelLightingAvailable(IDirect3DDevice9 *device)
{ return device && static_cast<PosixDevice9 *>(device)->Get_Gpu(); }
