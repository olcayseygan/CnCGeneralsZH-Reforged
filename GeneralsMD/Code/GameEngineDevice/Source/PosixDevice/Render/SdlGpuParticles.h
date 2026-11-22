// Native storage/compute particle device half. Manager eligibility and birth-ring
// bookkeeping remain in W3DParticleSys. Distributed under GPL-3.0; see LICENSE.md.
#pragma once
#include "SdlGpuParticlesContext.h"
#include <vector>
namespace SdlGpuParticles {
struct Statistics {
    uint64_t Dispatches=0, ReusedEvaluations=0, Draws=0, SliceCopies=0, Writes=0;
    uint64_t BufferBytes=0, ArrayBytes=0, DeviceGeneration=0;
    unsigned Capacity=0, MaximumEvaluated=0, PipelineVariants=0;
};
bool Supported(const SdlGpuParticlesContext &context);
using FlushHook = bool (*)(SdlGpuFrame *);
// Optional owning flush policy; nullptr uses Frame.Flush. Empty/discarded
// batches never acquire another command buffer just to release resources.
bool Prepare(const SdlGpuParticlesContext &context,unsigned capacity,bool *fresh,FlushHook flush=nullptr);
void WriteSpawns(unsigned first,const void *records,unsigned count);
void WriteSystems(unsigned first,const void *records,unsigned count);
void WriteFades(const float *fades,unsigned count);
void SetGround(const float *heights,unsigned width,unsigned height,const float mapping[4]);
void SetSliceTexture(unsigned slice,IDirect3DBaseTexture9 *texture);
void SetSmokeTint(const float tint[3]);
void Evaluate(const SdlGpuParticlesContext &context,unsigned first,unsigned count,unsigned now,float smooth);
void Draw(const SdlGpuParticlesContext &context,unsigned shaderType,const float view[16],const float projection[16]);
// A false release retains the complete owner. Teardown may explicitly discard
// the frame's queued work, then retry; ordinary replacement must never discard.
bool TryReleaseDevice();
// Frame teardown callback: a stale/foreign owner cannot release the current one.
bool TryReleaseForFrame(SdlGpuFrame *owner);
void ReleaseDevice();
Statistics Stats();
// Explicit diagnostic readback. These synchronize; the rendering path never
// uses them. They retain ordinary provider validation and actual GPU resources.
bool ReadEvaluated(std::vector<uint8_t> &bytes,std::string &error);
bool ReadSlice(unsigned slice,unsigned mip,std::vector<uint8_t> &bytes,std::string &error);
}
