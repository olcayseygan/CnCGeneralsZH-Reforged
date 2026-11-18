#include "sdl3storage.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <set>

namespace {
const unsigned Missing = ~0u;
struct Id {
    unsigned Opcode = 0, Storage = Missing, Pointee = 0, Element = 0, Source = 0;
    unsigned Set = Missing, Binding = Missing, Stride = 0;
    bool Block = false, BufferBlock = false, NonWritable = false;
    std::string Name;
    std::vector<unsigned> Members, Offsets;
};
bool fail(std::string &error, const char *message) { error = message; return false; }
unsigned word(const unsigned char *p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
}
bool shader_code(SDL_GPUDevice *device, const std::vector<unsigned char> &spirv,
    SDL3_ShaderStage stage, std::string &msl, SDL_GPUShaderFormat &format,
    const void *&code, size_t &size, const char *&entry, std::string &error)
{
    if (!device) return fail(error, "storage shader needs a GPU device");
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) {
        format = SDL_GPU_SHADERFORMAT_SPIRV; code = spirv.data(); size = spirv.size(); entry = "main";
        return true;
    }
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL))
        return fail(error, "storage shader device supports neither SPIR-V nor MSL");
    if (!SDL3_Translate_SPIRV_To_MSL(spirv, stage, msl, error)) return false;
    format = SDL_GPU_SHADERFORMAT_MSL; code = msl.data(); size = msl.size(); entry = "main0";
    return true;
}
}

bool SDL3_Reflect_Storage_SPIRV(const std::vector<unsigned char> &spirv,
    SDL3_ShaderStage stage, SDL3_StorageLayout &layout, std::string &error)
{
    layout = SDL3_StorageLayout(); error.clear();
    if (spirv.size() < 20 || spirv.size() % 4 || word(spirv.data()) != 0x07230203)
        return fail(error, "invalid SPIR-V header");
    const unsigned bound = word(spirv.data() + 12);
    if (!bound || bound > 1048576) return fail(error, "invalid SPIR-V ID bound");
    std::vector<Id> ids(bound);
    const unsigned model = stage == SDL3_ShaderStage::Compute ? 5 : stage == SDL3_ShaderStage::Vertex ? 0 : 4;
    unsigned entry = 0, localEntry = 0, threads[3] = {};
    std::vector<std::pair<unsigned,unsigned>> sampledPairs;
    const size_t words = spirv.size() / 4;
    for (size_t at = 5; at < words;) {
        const unsigned first = word(spirv.data() + at * 4), op = first & 65535, n = first >> 16;
        if (!n || n > words - at) return fail(error, "truncated SPIR-V instruction");
        auto w = [&](unsigned index) { return index < n ? word(spirv.data() + (at + index) * 4) : Missing; };
        auto valid = [&](unsigned id) { return id && id < bound; };
        if (op == 15 && n >= 4) { // OpEntryPoint
            if (w(1) != model || entry) return fail(error, "storage shader entry stage mismatch or multiple entries");
            entry = w(2);
        } else if (op == 16 && n >= 6 && w(2) == 17) { // OpExecutionMode LocalSize
            localEntry = w(1); for (unsigned i=0; i<3; ++i) threads[i] = w(3+i);
        } else if (op == 5 && n >= 3 && valid(w(1))) {
            const char *text = reinterpret_cast<const char *>(spirv.data() + (at+2)*4);
            size_t length = (n-2)*4; const void *end = memchr(text, 0, length);
            if (!end) return fail(error, "unterminated SPIR-V name");
            ids[w(1)].Name.assign(text, static_cast<const char *>(end)-text);
        } else if (op == 71 && n >= 3 && valid(w(1))) { // OpDecorate
            Id &id = ids[w(1)];
            if (w(2) == 2) id.Block = true;
            else if (w(2) == 3) id.BufferBlock = true;
            else if (w(2) == 24) id.NonWritable = true;
            else if (n >= 4 && w(2) == 6) id.Stride = w(3);
            else if (n >= 4 && w(2) == 33) id.Binding = w(3);
            else if (n >= 4 && w(2) == 34) id.Set = w(3);
        } else if (op == 72 && n >= 4 && valid(w(1))) { // OpMemberDecorate
            Id &id = ids[w(1)];
            if (w(2) > 4096) return fail(error, "invalid SPIR-V member index");
            if (w(3) == 24) id.NonWritable = true;
            if (w(3) == 35 && n >= 5) {
                if (id.Offsets.size() <= w(2)) id.Offsets.resize(w(2)+1, Missing);
                id.Offsets[w(2)] = w(4);
            }
        } else if ((op == 25 || op == 26 || op == 27) && n >= 2 && valid(w(1))) {
            Id &id=ids[w(1)]; id.Opcode=op;
            if (op == 25) id.Element=w(7); // OpTypeImage Sampled=1, not a storage image
        } else if (op == 61 && n >= 4 && valid(w(2)) && valid(w(3))) {
            ids[w(2)].Source=w(3); // OpLoad used by OpSampledImage
        } else if (op == 86 && n >= 5 && valid(w(3)) && valid(w(4))) {
            sampledPairs.emplace_back(w(3),w(4));
        } else if ((op == 28 || op == 29) && n >= 3 && valid(w(1)) && valid(w(2))) {
            ids[w(1)].Opcode = op; ids[w(1)].Element = w(2);
        } else if (op == 30 && n >= 2 && valid(w(1))) {
            Id &id=ids[w(1)]; id.Opcode=op;
            for (unsigned i=2; i<n; ++i) {
                if (!valid(w(i))) return fail(error, "invalid SPIR-V member type");
                id.Members.push_back(w(i));
            }
        } else if (op == 32 && n == 4 && valid(w(1)) && valid(w(3))) {
            Id &id=ids[w(1)]; id.Opcode=op; id.Storage=w(2); id.Pointee=w(3);
        } else if (op == 59 && n >= 4 && valid(w(1)) && valid(w(2))) {
            Id &id=ids[w(2)]; id.Opcode=op; id.Pointee=w(1); id.Storage=w(3);
        }
        at += n;
    }
    if (!entry) return fail(error, "missing storage shader entry point");
    if (stage == SDL3_ShaderStage::Compute) {
        if (localEntry != entry || !threads[0] || !threads[1] || !threads[2])
            return fail(error, "compute shader must declare a nonzero LocalSize");
        for (unsigned i=0; i<3; ++i) layout.Threads[i]=threads[i];
    }
    std::set<unsigned> ro, rw, uniforms, images, samplers;
    for (unsigned index=1; index<bound; ++index) {
        const Id &var=ids[index];
        if (var.Opcode != 59 || var.Binding == Missing) continue;
        if (!var.Pointee || var.Pointee >= bound) return fail(error, "invalid storage variable type");
        const Id &pointer=ids[var.Pointee];
        if (pointer.Opcode != 32 || !pointer.Pointee || pointer.Pointee >= bound)
            return fail(error, "invalid storage pointer type");
        const Id &type=ids[pointer.Pointee];
        SDL3_StorageBinding binding; binding.Set=var.Set; binding.Binding=var.Binding; binding.ElementStride=0;
        unsigned expectedSet; std::set<unsigned> *slots;
        if (var.Storage == 0 && (type.Opcode == 25 || type.Opcode == 26 || type.Opcode == 27)) {
            if (type.Opcode == 25 && type.Element != 1)
                return fail(error,"storage images are outside this helper's explicit contract");
            binding.Kind=type.Opcode == 26 ? SDL3_StorageKind::Sampler : SDL3_StorageKind::SampledTexture;
            slots=type.Opcode == 26 ? &samplers : &images;
            expectedSet=stage == SDL3_ShaderStage::Fragment ? 2 : 0;
        } else if (var.Storage == 2 && type.Block && !type.BufferBlock) {
            binding.Kind=SDL3_StorageKind::UniformBuffer; slots=&uniforms;
            expectedSet=stage == SDL3_ShaderStage::Compute ? 2 : stage == SDL3_ShaderStage::Vertex ? 1 : 3;
        } else if ((var.Storage == 2 && type.BufferBlock) || var.Storage == 12) {
            if (type.Members.size() != 1) return fail(error, "storage buffer must contain one runtime array");
            const Id &array=ids[type.Members[0]];
            if (array.Opcode != 29 || !array.Stride) return fail(error, "storage buffer has no runtime ArrayStride");
            binding.ElementStride=array.Stride;
            const bool readonly=var.NonWritable || type.NonWritable || array.NonWritable;
            if (!readonly && stage != SDL3_ShaderStage::Compute)
                return fail(error, "writable graphics storage is outside this helper's contract");
            binding.Kind=readonly ? SDL3_StorageKind::ReadOnlyBuffer : SDL3_StorageKind::ReadWriteBuffer;
            slots=readonly ? &ro : &rw;
            expectedSet=stage == SDL3_ShaderStage::Compute ? (readonly ? 0 : 1) : stage == SDL3_ShaderStage::Vertex ? 0 : 2;
        } else return fail(error, "storage helper refuses unknown resource types");
        if (var.Set != expectedSet || var.Binding > 31 || !slots->insert(var.Binding).second)
            return fail(error, "storage resource set/binding does not match the explicit SDL contract");
        layout.Bindings.push_back(binding);
    }
    auto contiguous=[](const std::set<unsigned> &slots) { return slots.empty() || *slots.rbegin()+1 == slots.size(); };
    if (!contiguous(images) || !contiguous(rw) || !contiguous(uniforms))
        return fail(error, "sampled/writable/uniform bindings must be contiguous from zero");
    if (!ro.empty() && (*ro.begin()!=images.size() || *ro.rbegin()+1!=images.size()+ro.size()))
        return fail(error,"readonly buffers must follow sampled textures without gaps");
    for (unsigned slot : samplers) if (!images.count(slot))
        return fail(error,"sampler has no same-slot sampled texture");
    auto origin=[&](unsigned id) { for (unsigned i=0; i<bound && ids[id].Source; ++i) id=ids[id].Source; return id; };
    for (const auto &pair : sampledPairs) {
        const Id &image=ids[origin(pair.first)], &sampler=ids[origin(pair.second)];
        if (image.Set!=sampler.Set || image.Binding!=sampler.Binding || !images.count(image.Binding))
            return fail(error,"sampled texture must use its same-slot sampler");
    }
    layout.SampledTextures=images.size();
    layout.ReadOnlyBuffers=ro.size(); layout.ReadWriteBuffers=rw.size(); layout.UniformBuffers=uniforms.size();
    for (unsigned i=1; i<bound; ++i) if (ids[i].Opcode == 30 && !ids[i].Offsets.empty()) {
        SDL3_StorageTypeLayout type; type.Name=ids[i].Name; type.MemberOffsets=ids[i].Offsets;
        for (const Id &array : ids) if ((array.Opcode == 28 || array.Opcode == 29) && array.Element == i)
            type.ArrayStride=std::max(type.ArrayStride, array.Stride);
        layout.Types.push_back(type);
    }
    return true;
}

SDL_GPUComputePipeline *SDL3_Create_Storage_Compute_Pipeline(SDL_GPUDevice *device,
    const std::vector<unsigned char> &spirv, SDL3_StorageLayout &layout, std::string &error)
{
    if (!SDL3_Reflect_Storage_SPIRV(spirv, SDL3_ShaderStage::Compute, layout, error)) return nullptr;
    std::string msl; SDL_GPUComputePipelineCreateInfo info={}; const void *code; const char *entry;
    if (!shader_code(device,spirv,SDL3_ShaderStage::Compute,msl,info.format,code,info.code_size,entry,error)) return nullptr;
    info.code=static_cast<const Uint8 *>(code); info.entrypoint=entry;
    info.num_samplers=layout.SampledTextures;
    info.num_readonly_storage_buffers=layout.ReadOnlyBuffers;
    info.num_readwrite_storage_buffers=layout.ReadWriteBuffers;
    info.num_uniform_buffers=layout.UniformBuffers;
    info.threadcount_x=layout.Threads[0]; info.threadcount_y=layout.Threads[1]; info.threadcount_z=layout.Threads[2];
    SDL_GPUComputePipeline *pipeline=SDL_CreateGPUComputePipeline(device,&info);
    if (!pipeline) error=SDL_GetError();
    return pipeline;
}

SDL_GPUShader *SDL3_Create_Storage_Graphics_Shader(SDL_GPUDevice *device,
    const std::vector<unsigned char> &spirv, SDL3_ShaderStage stage,
    SDL3_StorageLayout &layout, std::string &error)
{
    if (stage == SDL3_ShaderStage::Compute) { fail(error,"graphics storage shader requires a graphics stage"); return nullptr; }
    if (!SDL3_Reflect_Storage_SPIRV(spirv,stage,layout,error)) return nullptr;
    std::string msl; SDL_GPUShaderCreateInfo info={}; const void *code; const char *entry;
    if (!shader_code(device,spirv,stage,msl,info.format,code,info.code_size,entry,error)) return nullptr;
    info.code=static_cast<const Uint8 *>(code); info.entrypoint=entry;
    info.stage=stage == SDL3_ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers=layout.SampledTextures;
    info.num_storage_buffers=layout.ReadOnlyBuffers; info.num_uniform_buffers=layout.UniformBuffers;
    SDL_GPUShader *shader=SDL_CreateGPUShader(device,&info);
    if (!shader) error=SDL_GetError();
    return shader;
}
