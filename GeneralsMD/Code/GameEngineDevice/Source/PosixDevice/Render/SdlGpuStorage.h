// Persistent storage buffers and ordered, owned partial updates. No particle
// provider or capability is enabled here. Call from the renderer's owner thread.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "sdl3storage.h"

struct SdlStorageUpdate {
    uint32_t DestinationOffset, ByteCount;
    const void *Data;
};
struct SdlOwnedStorageUpdate {
    uint32_t DestinationOffset;
    std::vector<uint8_t> Data;
};
// Validates the entire list before copying; canonical nonoverlapping ranges own
// every byte. Ordered overlaps are last-wins. Storage copies are 4-byte aligned.
bool Sdl_Prepare_Storage_Updates(uint32_t destinationSize, const SdlStorageUpdate *updates,
    size_t count, std::vector<SdlOwnedStorageUpdate> &owned, std::string &error);

// Allocation policy can supply an engine resource budget; failures occur before
// any persistent destination is mutated. Defaults use the real SDL allocator.
struct SdlStorageAllocator {
    decltype(&SDL_CreateGPUBuffer) Buffer = SDL_CreateGPUBuffer;
    decltype(&SDL_CreateGPUTransferBuffer) Transfer = SDL_CreateGPUTransferBuffer;
    decltype(&SDL_MapGPUTransferBuffer) Map = SDL_MapGPUTransferBuffer;
};

class SdlGpuStorage {
public:
    // flushReaders must submit all deferred frame readers before returning true.
    // Production adapter: [&frame] { return frame.Flush(); }, with frame.Device().
    // No upload ever cycles the persistent destination. SDL submission order
    // separates old readers, this copy/compute work, and later frame readers.
    using FlushReaders = std::function<bool()>;
    // Mandatory owning policy: retain a buffer until queued CPU readers are
    // submitted/discarded (for frames, Release_After_Batch(nullptr, buffer)).
    // Called only if the owner destroys this object after a failed Close.
    using RetireBuffer = std::function<void(SDL_GPUBuffer *)>;
    struct Buffer { unsigned Index = ~0u; uint64_t Generation = 0; uint32_t Size = 0; const SdlGpuStorage *Owner = nullptr; };
    struct Uniform { unsigned Slot; const void *Data; uint32_t Size; };
    SdlGpuStorage(SDL_GPUDevice *device, FlushReaders flushReaders, RetireBuffer retireBuffer, SdlStorageAllocator allocator = {});
    ~SdlGpuStorage();
    SdlGpuStorage(const SdlGpuStorage &) = delete;
    SdlGpuStorage &operator=(const SdlGpuStorage &) = delete;
    bool Create(uint32_t size, SDL_GPUBufferUsageFlags usage, Buffer &buffer, std::string &error);
    // Allocates a fresh generation first. Failure preserves old handle and bytes.
    bool Replace(Buffer &buffer, uint32_t size, SDL_GPUBufferUsageFlags usage, std::string &error);
    bool Write(const Buffer &buffer, const SdlStorageUpdate *updates, size_t count, std::string &error);
    bool Dispatch(SDL_GPUComputePipeline *pipeline, const SDL3_StorageLayout &layout,
        const std::vector<Buffer> &readOnly, const std::vector<Buffer> &readWrite,
        const std::vector<Uniform> &uniforms, unsigned groupsX, unsigned groupsY, unsigned groupsZ,
        std::string &error, const std::vector<SDL_GPUTextureSamplerBinding> &samplers = {});
    // Explicit diagnostic readback waits for completion; ordinary writes/dispatch
    // only poll retired staging fences, never wait for the GPU on every update.
    bool Read(const Buffer &buffer, std::vector<uint8_t> &bytes, std::string &error);
    SDL_GPUBuffer *Native(const Buffer &buffer) const;
    // False preserves all resources: the caller may retry or retain the owner.
    bool Close(std::string &error);
    void Collect();
    unsigned PendingSubmissions() const { return Pending.size(); }
private:
    struct Slot { SDL_GPUBuffer *Native; uint32_t Size; SDL_GPUBufferUsageFlags Usage; uint64_t Generation; };
    struct Submission { SDL_GPUFence *Fence; SDL_GPUTransferBuffer *Transfer; };
    SDL_GPUDevice *Device;
    FlushReaders Flush;
    RetireBuffer Retire;
    SdlStorageAllocator Allocate;
    std::vector<Slot> Buffers;
    std::vector<Submission> Pending;
    void FinishSubmitted();
    bool BeforeWrite(std::string &error);
    bool Submit(SDL_GPUCommandBuffer *commands, SDL_GPUTransferBuffer *transfer, std::string &error);
};
