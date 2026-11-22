// The GPU particle path's entry points for a build with no Direct3D 11 backend: nothing is
// supported, so the particle manager keeps every particle on the CPU and never asks for the rest.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "W3DDevice/GameClient/GpuParticles.h"

namespace GpuParticles {

bool Supported() { return false; }
bool Prepare(unsigned, bool *fresh) { *fresh = false; return false; }
void Write_Spawns(unsigned, const ParticleGpuSpawn *, unsigned) {}
void Write_Systems(unsigned, const ParticleGpuSystemEntry *, unsigned) {}
void Write_Fades(const float *, unsigned) {}
void Set_Ground(const float *, unsigned, unsigned, const float[4]) {}
void Set_Slice_Texture(unsigned, IDirect3DBaseTexture9 *) {}
void Set_Smoke_Tint(const float[3]) {}
void Evaluate(unsigned, unsigned, unsigned, float) {}
void Draw(unsigned, const Camera &) {}
void ReleaseDevice() {}

} // namespace GpuParticles
