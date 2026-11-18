// Dedicated sampled/storage programs. These do not pass through sdl3target.h or
// the ordinary texture/sampler graphics reflection path.
#pragma once
#include "sdl3shadercompile.h"
#include <cstdint>
struct SDL_GPUComputePipeline;

enum class SDL3_StorageKind { ReadOnlyBuffer, ReadWriteBuffer, UniformBuffer, SampledTexture, Sampler };
struct SDL3_StorageBinding
{
    SDL3_StorageKind Kind;
    unsigned Set, Binding, ElementStride;
};
struct SDL3_StorageTypeLayout
{
    std::string Name;
    std::vector<unsigned> MemberOffsets;
    unsigned ArrayStride = 0;
};
struct SDL3_StorageLayout
{
    unsigned ReadOnlyBuffers = 0, ReadWriteBuffers = 0, UniformBuffers = 0, SampledTextures = 0;
    unsigned Threads[3] = {0, 0, 0};
    std::vector<SDL3_StorageBinding> Bindings;
    std::vector<SDL3_StorageTypeLayout> Types;
};
// Explicit contract: sampled textures first, followed by readonly buffers;
// each sampled image uses its same-slot sampler (Load-only images need none).
// Compute: tN/sN space0, writable uN space1, uniform bN space2.
// Graphics: tN/sN space0/2, uniform bN space1/3. No storage images,
// writable graphics buffers, guessed resource kinds or register rewriting.
bool SDL3_Reflect_Storage_SPIRV(const std::vector<unsigned char> &spirv,
    SDL3_ShaderStage stage, SDL3_StorageLayout &layout, std::string &error);
SDL_GPUComputePipeline *SDL3_Create_Storage_Compute_Pipeline(SDL_GPUDevice *device,
    const std::vector<unsigned char> &spirv, SDL3_StorageLayout &layout, std::string &error);
SDL_GPUShader *SDL3_Create_Storage_Graphics_Shader(SDL_GPUDevice *device,
    const std::vector<unsigned char> &spirv, SDL3_ShaderStage stage,
    SDL3_StorageLayout &layout, std::string &error);
