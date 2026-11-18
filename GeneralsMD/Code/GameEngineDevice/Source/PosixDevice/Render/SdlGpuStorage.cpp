#include "SdlGpuStorage.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace {
bool fail(std::string &error, const char *message) { error=message; return false; }
bool valid_range(uint32_t size, uint32_t offset, size_t count) {
    return offset <= size && count <= size-offset && !(offset%4) && !(count%4);
}
}

bool Sdl_Prepare_Storage_Updates(uint32_t size, const SdlStorageUpdate *updates,
    size_t count, std::vector<SdlOwnedStorageUpdate> &owned, std::string &error)
{
    error.clear();
    if (count && !updates) return fail(error,"storage update list is null");
    for (size_t i=0; i<count; ++i)
        if (!valid_range(size,updates[i].DestinationOffset,updates[i].ByteCount)
            || (updates[i].ByteCount && !updates[i].Data))
            return fail(error,"storage update is out of bounds, unaligned or has null data");
    try {
        std::vector<SdlOwnedStorageUpdate> result;
        for (size_t i=0; i<count; ++i) {
            const SdlStorageUpdate &update=updates[i];
            if (!update.ByteCount) continue;
            const uint32_t begin=update.DestinationOffset, end=begin+update.ByteCount;
            std::vector<SdlOwnedStorageUpdate> next;
            for (const SdlOwnedStorageUpdate &old : result) {
                const uint32_t oldEnd=old.DestinationOffset+uint32_t(old.Data.size());
                if (oldEnd<=begin || old.DestinationOffset>=end) { next.push_back(old); continue; }
                if (old.DestinationOffset<begin) next.push_back({old.DestinationOffset,
                    std::vector<uint8_t>(old.Data.begin(),old.Data.begin()+begin-old.DestinationOffset)});
                if (oldEnd>end) next.push_back({end,
                    std::vector<uint8_t>(old.Data.begin()+end-old.DestinationOffset,old.Data.end())});
            }
            const uint8_t *data=static_cast<const uint8_t *>(update.Data);
            next.push_back({begin,std::vector<uint8_t>(data,data+update.ByteCount)});
            result.swap(next);
        }
        std::sort(result.begin(),result.end(),[](const SdlOwnedStorageUpdate &a,const SdlOwnedStorageUpdate &b) {
            return a.DestinationOffset<b.DestinationOffset;
        });
        owned.swap(result);
    } catch (const std::bad_alloc &) { return fail(error,"storage update preparation allocation failed"); }
    return true;
}

SdlGpuStorage::SdlGpuStorage(SDL_GPUDevice *device, FlushReaders flushReaders, RetireBuffer retireBuffer, SdlStorageAllocator allocator)
    : Device(device), Flush(std::move(flushReaders)), Retire(std::move(retireBuffer)), Allocate(allocator) {}
void SdlGpuStorage::FinishSubmitted()
{
    for (const Submission &submission : Pending) {
        SDL_WaitForGPUFences(Device,true,&submission.Fence,1);
        if (submission.Transfer) SDL_ReleaseGPUTransferBuffer(Device,submission.Transfer);
        SDL_ReleaseGPUFence(Device,submission.Fence);
    }
    Pending.clear();
}
bool SdlGpuStorage::Close(std::string &error)
{
    error.clear();
    if (!Device) return true;
    // Failed acquisition can leave CPU packets queued. Do not release anything
    // or overwrite this owner until the caller submits or discards those readers.
    if (!Flush || !Flush()) return fail(error,"storage close retained queued readers");
    FinishSubmitted();
    for (const Slot &slot : Buffers) if (slot.Native) SDL_ReleaseGPUBuffer(Device,slot.Native);
    Buffers.clear(); Device=nullptr;
    return true;
}
SdlGpuStorage::~SdlGpuStorage()
{
    if (!Device) return;
    std::string error;
    if (Close(error)) return;
    // Submitted staging has a different lifetime from still-queued CPU readers.
    // The mandatory owning policy takes the buffers; no borrowed handle is freed.
    FinishSubmitted();
    for (const Slot &slot : Buffers) if (slot.Native) Retire(slot.Native);
    Buffers.clear(); Device=nullptr;
}
SDL_GPUBuffer *SdlGpuStorage::Native(const Buffer &buffer) const
{
    if (buffer.Owner!=this || buffer.Index>=Buffers.size()) return nullptr;
    const Slot &slot=Buffers[buffer.Index];
    return slot.Generation==buffer.Generation && slot.Size==buffer.Size ? slot.Native : nullptr;
}
bool SdlGpuStorage::BeforeWrite(std::string &error)
{
    if (!Device || !Flush) return fail(error,"storage needs a device and deferred-reader flush callback");
    Collect();
    return Flush() || fail(error,"storage deferred-reader submission failed");
}
void SdlGpuStorage::Collect()
{
    if (!Device) return;
    size_t kept=0;
    for (const Submission &submission : Pending) {
        if (SDL_QueryGPUFence(Device,submission.Fence)) {
            if (submission.Transfer) SDL_ReleaseGPUTransferBuffer(Device,submission.Transfer);
            SDL_ReleaseGPUFence(Device,submission.Fence);
        } else Pending[kept++]=submission;
    }
    Pending.resize(kept);
}
bool SdlGpuStorage::Create(uint32_t size, SDL_GPUBufferUsageFlags usage, Buffer &buffer, std::string &error)
{
    error.clear();
    if (!Device || !Flush || !Retire || !Allocate.Buffer || !Allocate.Transfer || !Allocate.Map || !size || size%4 || !usage) return fail(error,"invalid storage buffer size/device/usage/owner policy");
    SDL_GPUBufferCreateInfo info={}; info.size=size; info.usage=usage;
    SDL_GPUBuffer *native=Allocate.Buffer(Device,&info);
    if (!native) return fail(error,SDL_GetError());
    try { Buffers.push_back({native,size,usage,1}); }
    catch (const std::bad_alloc &) { SDL_ReleaseGPUBuffer(Device,native); return fail(error,"storage slot allocation failed"); }
    buffer={unsigned(Buffers.size()-1),1,size,this}; return true;
}
bool SdlGpuStorage::Replace(Buffer &buffer, uint32_t size, SDL_GPUBufferUsageFlags usage, std::string &error)
{
    error.clear();
    if (!Native(buffer) || !size || size%4 || !usage || buffer.Generation==UINT64_MAX)
        return fail(error,"invalid storage replacement");
    SDL_GPUBufferCreateInfo info={}; info.size=size; info.usage=usage;
    SDL_GPUBuffer *native=Allocate.Buffer(Device,&info);
    if (!native) return fail(error,SDL_GetError());
    if (!BeforeWrite(error)) { SDL_ReleaseGPUBuffer(Device,native); return false; }
    Slot &slot=Buffers[buffer.Index];
    SDL_ReleaseGPUBuffer(Device,slot.Native);
    slot={native,size,usage,slot.Generation+1};
    buffer.Generation=slot.Generation; buffer.Size=size; return true;
}
bool SdlGpuStorage::Submit(SDL_GPUCommandBuffer *commands, SDL_GPUTransferBuffer *transfer, std::string &error)
{
    // Reserve before submission: after a successful submit the retirement record
    // must not allocate and fail while the GPU still refers to staging.
    try { Pending.reserve(Pending.size()+1); }
    catch (const std::bad_alloc &) {
        SDL_CancelGPUCommandBuffer(commands);
        if (transfer) SDL_ReleaseGPUTransferBuffer(Device,transfer);
        return fail(error,"storage submission tracking allocation failed");
    }
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (!fence) {
        if (transfer) SDL_ReleaseGPUTransferBuffer(Device,transfer);
        return fail(error,SDL_GetError());
    }
    Pending.push_back({fence,transfer}); return true;
}
bool SdlGpuStorage::Write(const Buffer &buffer, const SdlStorageUpdate *updates, size_t count, std::string &error)
{
    error.clear();
    SDL_GPUBuffer *destination=Native(buffer);
    if (!destination) return fail(error,"stale storage buffer generation");
    std::vector<SdlOwnedStorageUpdate> owned;
    if (!Sdl_Prepare_Storage_Updates(buffer.Size,updates,count,owned,error)) return false;
    if (owned.empty()) return true;
    uint64_t total=0; for (const auto &update : owned) total+=update.Data.size();
    if (total>UINT32_MAX) return fail(error,"storage staging size overflow");
    SDL_GPUTransferBufferCreateInfo info={}; info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; info.size=uint32_t(total);
    SDL_GPUTransferBuffer *staging=Allocate.Transfer(Device,&info);
    if (!staging) return fail(error,SDL_GetError());
    uint8_t *mapped=static_cast<uint8_t *>(Allocate.Map(Device,staging,false));
    if (!mapped) { SDL_ReleaseGPUTransferBuffer(Device,staging); return fail(error,SDL_GetError()); }
    size_t at=0;
    for (const auto &update : owned) { memcpy(mapped+at,update.Data.data(),update.Data.size()); at+=update.Data.size(); }
    SDL_UnmapGPUTransferBuffer(Device,staging);
    if (!BeforeWrite(error)) { SDL_ReleaseGPUTransferBuffer(Device,staging); return false; }
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(Device);
    if (!commands) { SDL_ReleaseGPUTransferBuffer(Device,staging); return fail(error,SDL_GetError()); }
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
    if (!copy) { SDL_CancelGPUCommandBuffer(commands); SDL_ReleaseGPUTransferBuffer(Device,staging); return fail(error,SDL_GetError()); }
    uint32_t offset=0;
    for (const auto &update : owned) {
        SDL_GPUTransferBufferLocation source={staging,offset};
        SDL_GPUBufferRegion target={destination,update.DestinationOffset,uint32_t(update.Data.size())};
        SDL_UploadToGPUBuffer(copy,&source,&target,false);
        offset+=uint32_t(update.Data.size());
    }
    SDL_EndGPUCopyPass(copy);
    return Submit(commands,staging,error);
}
bool SdlGpuStorage::Dispatch(SDL_GPUComputePipeline *pipeline, const SDL3_StorageLayout &layout,
    const std::vector<Buffer> &readOnly, const std::vector<Buffer> &readWrite,
    const std::vector<Uniform> &uniforms, unsigned x, unsigned y, unsigned z, std::string &error,
    const std::vector<SDL_GPUTextureSamplerBinding> &samplers)
{
    error.clear();
    if (!pipeline || !x || !y || !z || x>65535 || y>65535 || z>65535 || readOnly.size()!=layout.ReadOnlyBuffers
        || readWrite.size()!=layout.ReadWriteBuffers || uniforms.size()!=layout.UniformBuffers || uniforms.size()>4
        || samplers.size()!=layout.SampledTextures || samplers.size()>16)
        return fail(error,"compute storage dispatch shape mismatch");
    for (const auto &sampler : samplers) if (!sampler.texture || !sampler.sampler)
        return fail(error,"invalid compute sampled texture binding");
    std::vector<SDL_GPUBuffer *> readers;
    std::vector<SDL_GPUStorageBufferReadWriteBinding> writers;
    std::vector<SDL_GPUBuffer *> unique;
    try {
        for (const Buffer &buffer : readOnly) {
            SDL_GPUBuffer *native=Native(buffer);
            if (!native || !(Buffers[buffer.Index].Usage & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ))
                return fail(error,"invalid compute readonly storage binding");
            readers.push_back(native); unique.push_back(native);
        }
        for (const Buffer &buffer : readWrite) {
            SDL_GPUBuffer *native=Native(buffer);
            if (!native || !(Buffers[buffer.Index].Usage & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE)
                || std::find(unique.begin(),unique.end(),native)!=unique.end())
                return fail(error,"invalid or aliased compute writable storage binding");
            writers.push_back({native,false,0,0,0}); unique.push_back(native);
        }
    } catch (const std::bad_alloc &) { return fail(error,"compute binding preparation allocation failed"); }
    unsigned uniformMask=0;
    for (const Uniform &uniform : uniforms) {
        if (uniform.Slot>=layout.UniformBuffers || !uniform.Data || !uniform.Size || uniform.Size>65536
            || (uniformMask & (1u<<uniform.Slot))) return fail(error,"invalid compute uniform binding");
        uniformMask|=1u<<uniform.Slot;
    }
    if (!BeforeWrite(error)) return false;
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(Device);
    if (!commands) return fail(error,SDL_GetError());
    for (const Uniform &uniform : uniforms) SDL_PushGPUComputeUniformData(commands,uniform.Slot,uniform.Data,uniform.Size);
    SDL_GPUComputePass *pass=SDL_BeginGPUComputePass(commands,nullptr,0,writers.data(),writers.size());
    if (!pass) { SDL_CancelGPUCommandBuffer(commands); return fail(error,SDL_GetError()); }
    SDL_BindGPUComputePipeline(pass,pipeline);
    if (!samplers.empty()) SDL_BindGPUComputeSamplers(pass,0,samplers.data(),samplers.size());
    if (!readers.empty()) SDL_BindGPUComputeStorageBuffers(pass,0,readers.data(),readers.size());
    SDL_DispatchGPUCompute(pass,x,y,z); SDL_EndGPUComputePass(pass);
    return Submit(commands,nullptr,error);
}
bool SdlGpuStorage::Read(const Buffer &buffer, std::vector<uint8_t> &bytes, std::string &error)
{
    error.clear();
    SDL_GPUBuffer *native=Native(buffer);
    if (!native) return fail(error,"stale storage readback generation");
    std::vector<uint8_t> result;
    try { result.resize(buffer.Size); } catch (const std::bad_alloc &) { return fail(error,"storage readback allocation failed"); }
    SDL_GPUTransferBufferCreateInfo info={}; info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size=buffer.Size;
    SDL_GPUTransferBuffer *transfer=Allocate.Transfer(Device,&info);
    if (!transfer) return fail(error,SDL_GetError());
    if (!BeforeWrite(error)) { SDL_ReleaseGPUTransferBuffer(Device,transfer); return false; }
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(Device);
    if (!commands) { SDL_ReleaseGPUTransferBuffer(Device,transfer); return fail(error,SDL_GetError()); }
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
    if (!copy) { SDL_CancelGPUCommandBuffer(commands); SDL_ReleaseGPUTransferBuffer(Device,transfer); return fail(error,SDL_GetError()); }
    SDL_GPUBufferRegion source={native,0,buffer.Size}; SDL_GPUTransferBufferLocation target={transfer,0};
    SDL_DownloadFromGPUBuffer(copy,&source,&target); SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (!fence) { SDL_ReleaseGPUTransferBuffer(Device,transfer); return fail(error,SDL_GetError()); }
    const bool complete=SDL_WaitForGPUFences(Device,true,&fence,1);
    void *mapped=complete ? Allocate.Map(Device,transfer,false) : nullptr;
    if (mapped) { memcpy(result.data(),mapped,result.size()); SDL_UnmapGPUTransferBuffer(Device,transfer); }
    if (!mapped) error=SDL_GetError();
    SDL_ReleaseGPUFence(Device,fence); SDL_ReleaseGPUTransferBuffer(Device,transfer);
    if (!mapped) return false;
    bytes.swap(result); Collect(); return true;
}
