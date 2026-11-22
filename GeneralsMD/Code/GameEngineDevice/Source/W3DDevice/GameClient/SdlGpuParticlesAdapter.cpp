// Native SDL implementation of the existing manager/device API. Core context
// resolution does not call renderer capabilities, preventing Supported recursion.
// Distributed under GPL-3.0; see LICENSE.md.
#include "W3DDevice/GameClient/GpuParticles.h"
#include "SdlGpuParticles.h"
namespace GpuParticles {
bool Supported() {
    SdlGpuParticlesContext context;
    return PosixGpuParticles_Context(context) && SdlGpuParticles::Supported(context);
}
bool Prepare(unsigned capacity,bool *fresh) {
    if(fresh) *fresh=false;
    SdlGpuParticlesContext context;
    return PosixGpuParticles_Context(context) && SdlGpuParticles::Prepare(context,capacity,fresh);
}
void Write_Spawns(unsigned first,const ParticleGpuSpawn *records,unsigned count) {SdlGpuParticles::WriteSpawns(first,records,count);}
void Write_Systems(unsigned first,const ParticleGpuSystemEntry *entries,unsigned count) {SdlGpuParticles::WriteSystems(first,entries,count);}
void Write_Fades(const float *fades,unsigned count) {SdlGpuParticles::WriteFades(fades,count);}
void Set_Ground(const float *heights,unsigned width,unsigned height,const float mapping[4]) {SdlGpuParticles::SetGround(heights,width,height,mapping);}
void Set_Slice_Texture(unsigned slice,IDirect3DBaseTexture9 *texture) {SdlGpuParticles::SetSliceTexture(slice,texture);}
void Set_Smoke_Tint(const float tint[3]) {SdlGpuParticles::SetSmokeTint(tint);}
void Evaluate(unsigned first,unsigned count,unsigned now,float smooth) {
    SdlGpuParticlesContext context;if(PosixGpuParticles_Context(context)) SdlGpuParticles::Evaluate(context,first,count,now,smooth);
}
void Draw(unsigned shaderType,const Camera &camera) {
    SdlGpuParticlesContext context;if(PosixGpuParticles_Context(context)) SdlGpuParticles::Draw(context,shaderType,camera.view,camera.projection);
}
void ReleaseDevice() {SdlGpuParticles::ReleaseDevice();}
}
