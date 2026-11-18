#include "SdlGpuParticles.h"
#include "SdlGpuStorage.h"
#include "W3DDevice/GameClient/GpuParticles.h"
#include "W3DDevice/GameClient/ParticleGpuShader.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace SdlGpuParticles {
namespace {
constexpr unsigned SpawnStride=80,SystemStride=240,OutputStride=32,SliceSize=128,SliceMips=8,GroupSize=64;
struct FrameBlock { unsigned First,Count,Capacity,Now;float Smooth,Unused[3],GroundMap[4]; };
struct PassBlock {float View[16],Projection[16],SoftDepth[4];unsigned Count,Shader,NewestFirst,Soft;float Gain,Unused[3];};
static_assert(sizeof(FrameBlock)==48 && offsetof(FrameBlock,GroundMap)==32,"compute uniform ABI");
static_assert(sizeof(PassBlock)==176 && offsetof(PassBlock,SoftDepth)==128 && offsetof(PassBlock,Count)==144 && offsetof(PassBlock,Gain)==160,"draw uniform ABI");
struct Pipeline {unsigned Colour,Depth,Shader;SDL_GPUGraphicsPipeline *Native;};
struct State {
    SdlGpuFrame *Frame=nullptr;SDL_GPUDevice *Device=nullptr;uint64_t Generation=0;unsigned Capacity=0,Evaluated=0;
    FlushHook Flush=nullptr;bool Refused=false,Changed=true;unsigned First=0,Count=0,Now=0;float Smooth=-1,GroundMap[4]={};
    std::unique_ptr<SdlGpuStorage> Storage;SdlGpuStorage::Buffer Spawns,Systems,Fades,Output;
    SDL_GPUComputePipeline *Compute=nullptr;SDL3_StorageLayout ComputeLayout;
    SDL_GPUShader *Vertex=nullptr,*Pixel=nullptr,*BlitVertex=nullptr,*BlitPixel=nullptr;
    SDL_GPUGraphicsPipeline *Blit=nullptr;std::vector<Pipeline> Pipelines;
    SDL_GPUSampler *Linear=nullptr,*Point=nullptr;SDL_GPUTexture *Sprites=nullptr,*Ground=nullptr;
    std::array<IDirect3DBaseTexture9 *,GpuParticles::TEXTURE_SLICES> Requested={};
    std::array<SdlGpuParticleTextureView,GpuParticles::TEXTURE_SLICES> Sources={};
    std::array<bool,GpuParticles::TEXTURE_SLICES> Asked={};Statistics Metrics;
};
State state;
bool fail(const std::string &error) {
    if(!state.Refused) std::fprintf(stderr,"GPUPARTICLES SDL: device operation refused: %s; next Prepare returns CPU fallback\n",error.c_str());
    state.Refused=true;state.Evaluated=0;return false;
}
bool same_device(const SdlGpuParticlesContext &context) {
    return context.Frame==state.Frame && context.Frame && context.Frame->Device()==state.Device
        && context.DeviceGeneration==state.Generation;
}
bool submit_readers() {
    return state.Frame && (!state.Frame->Has_Pending_Work() || (state.Flush ? state.Flush(state.Frame) : state.Frame->Flush()));
}
bool submit(SDL_GPUCommandBuffer *commands,SDL_GPUTransferBuffer *staging=nullptr) {
    const bool ok=SDL_SubmitGPUCommandBuffer(commands);
    // SDL retires released transfer resources after their submitted references;
    // each upload uses fresh staging and never cycles a persistent destination.
    if(staging) SDL_ReleaseGPUTransferBuffer(state.Device,staging);
    return ok || fail(SDL_GetError());
}
bool texture_upload(SDL_GPUTexture *texture,const void *data,unsigned width,unsigned height,unsigned bytesPerPixel) {
    const uint64_t size=uint64_t(width)*height*bytesPerPixel;
    if(!data || !width || !height || size>UINT32_MAX) return fail("invalid texture upload extent");
    SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=uint32_t(size);
    SDL_GPUTransferBuffer *staging=SDL_CreateGPUTransferBuffer(state.Device,&ti);
    if(!staging) return fail(SDL_GetError());
    void *mapped=SDL_MapGPUTransferBuffer(state.Device,staging,false);
    if(!mapped) {SDL_ReleaseGPUTransferBuffer(state.Device,staging);return fail(SDL_GetError());}
    memcpy(mapped,data,size);SDL_UnmapGPUTransferBuffer(state.Device,staging);
    if(!submit_readers()) {SDL_ReleaseGPUTransferBuffer(state.Device,staging);return fail("frame readers failed to submit before texture write");}
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(state.Device);
    if(!commands) {SDL_ReleaseGPUTransferBuffer(state.Device,staging);return fail(SDL_GetError());}
    SDL_GPUCopyPass *pass=SDL_BeginGPUCopyPass(commands);
    if(!pass) {SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(state.Device,staging);return fail(SDL_GetError());}
    SDL_GPUTextureTransferInfo source={};source.transfer_buffer=staging;
    SDL_GPUTextureRegion destination={};destination.texture=texture;destination.w=width;destination.h=height;destination.d=1;
    SDL_UploadToGPUTexture(pass,&source,&destination,false);SDL_EndGPUCopyPass(pass);
    return submit(commands,staging);
}
bool shader(ParticleGpuShader::Program program,SDL3_ShaderStage stage,SDL_GPUShader *&output) {
    std::vector<unsigned char> spirv;std::string error;SDL3_StorageLayout layout;
    if(!SDL3_Compile_HLSL_To_SPIRV(ParticleGpuShader::Sdl(program),stage,spirv,error)) return fail(error);
    output=SDL3_Create_Storage_Graphics_Shader(state.Device,spirv,stage,layout,error);
    return output || fail(error);
}
bool soft_allowed(const SdlGpuParticlesContext &context) {
    if(context.Pass!=SdlGpuParticlesPass::MainScene || !context.SoftAllowed || !context.SoftDepth || !context.Target.Depth
        || !context.SoftCapture.Depth || context.SoftCapture.Phase!=SdlSceneCapturePhase::BeforeTransparent
        || context.FrameSerial!=state.Frame->Scene_Frame() || context.SoftCapture.Frame!=context.FrameSerial
        || context.SoftCapture.Generation!=state.Frame->Scene_Generation()
        || !context.MainViewKey || context.SoftCapture.ViewKey!=context.MainViewKey
        || context.SoftDepth==context.Target.Colour || context.SoftDepth==context.Target.Depth) return false;
    const auto &source=context.SoftCapture.Source;
    return source.Colour==context.Target.Colour && source.Depth==context.Target.Depth
        && source.Width==context.Target.Width && source.Height==context.Target.Height
        && SdlGpuFrame::Target_Colour_Format(source)==SdlGpuFrame::Target_Colour_Format(context.Target)
        && state.Frame->Target_Depth_Format(source)==state.Frame->Target_Depth_Format(context.Target);
}
SDL_GPUGraphicsPipeline *pipeline(unsigned colour,unsigned depth,unsigned shaderType) {
    for(const auto &pipeline:state.Pipelines) if(pipeline.Colour==colour && pipeline.Depth==depth && pipeline.Shader==shaderType) return pipeline.Native;
    SDL_GPUColorTargetDescription target={};target.format=SDL_GPUTextureFormat(colour);target.blend_state.enable_blend=true;
    target.blend_state.src_color_blendfactor=target.blend_state.src_alpha_blendfactor=shaderType==1 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor=target.blend_state.dst_alpha_blendfactor=shaderType==1 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op=target.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=state.Vertex;info.fragment_shader=state.Pixel;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;
    info.depth_stencil_state.enable_depth_test=depth!=0;info.depth_stencil_state.enable_depth_write=false;info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;info.target_info.has_depth_stencil_target=depth!=0;info.target_info.depth_stencil_format=SDL_GPUTextureFormat(depth);
    SDL_GPUGraphicsPipeline *native=SDL_CreateGPUGraphicsPipeline(state.Device,&info);
    if(!native) {fail(SDL_GetError());return nullptr;}
    state.Pipelines.push_back({colour,depth,shaderType,native});state.Metrics.PipelineVariants=state.Pipelines.size();return native;
}
bool clear_slices() {
    if(!submit_readers()) return fail("initial array clear reader submission failed");
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(state.Device);if(!commands) return fail(SDL_GetError());
    for(unsigned layer=0;layer<GpuParticles::TEXTURE_SLICES;++layer) {
        SDL_GPUColorTargetInfo target={};target.texture=state.Sprites;target.layer_or_depth_plane=layer;target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;target.cycle=false;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);
        if(!pass) {SDL_CancelGPUCommandBuffer(commands);return fail(SDL_GetError());}SDL_EndGPURenderPass(pass);
    }
    SDL_GenerateMipmapsForGPUTexture(commands,state.Sprites);return submit(commands);
}
bool make_resources() {
    std::string error;std::vector<unsigned char> spirv;
    auto *owner=state.Frame;const auto flush=state.Flush;
    state.Storage.reset(new SdlGpuStorage(state.Device,[owner,flush] {
        return !owner->Has_Pending_Work() || (flush ? flush(owner) : owner->Flush());
    },[owner](SDL_GPUBuffer *buffer) {owner->Release_After_Batch(nullptr,buffer);}));
    const auto read=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    if(!state.Storage->Create(state.Capacity*SpawnStride,read,state.Spawns,error)
        || !state.Storage->Create(GpuParticles::SYSTEM_SLOTS*SystemStride,read,state.Systems,error)
        || !state.Storage->Create(GpuParticles::SYSTEM_SLOTS*4,read,state.Fades,error)
        || !state.Storage->Create(state.Capacity*OutputStride,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,state.Output,error)) return fail(error);
    if(!SDL3_Compile_HLSL_To_SPIRV(ParticleGpuShader::Sdl(ParticleGpuShader::Program::Evaluate),SDL3_ShaderStage::Compute,spirv,error)
        || !(state.Compute=SDL3_Create_Storage_Compute_Pipeline(state.Device,spirv,state.ComputeLayout,error))) return fail(error);
    if(state.ComputeLayout.ReadOnlyBuffers!=3 || state.ComputeLayout.ReadWriteBuffers!=1 || state.ComputeLayout.SampledTextures!=1 || state.ComputeLayout.UniformBuffers!=1
        || state.ComputeLayout.Threads[0]!=64 || state.ComputeLayout.Threads[1]!=1 || state.ComputeLayout.Threads[2]!=1) return fail("particle compute resource layout mismatch");
    bool spawnLayout=false,systemLayout=false,outputLayout=false,fadeLayout=false;
    for(const auto &type:state.ComputeLayout.Types) {
        if(type.Name=="Spawn") spawnLayout=type.ArrayStride==80 && type.MemberOffsets==std::vector<unsigned>({0,12,16,28,32,36,40,44,48,52,56,60,64,68,72,76});
        if(type.Name=="SystemEntry") systemLayout=type.ArrayStride==240 && type.MemberOffsets==std::vector<unsigned>({0,12,16,28,32,36,40,44,48,112});
        if(type.Name=="Particle") outputLayout=type.ArrayStride==32 && type.MemberOffsets==std::vector<unsigned>({0,12,16,24,28});
    }
    for(const auto &binding:state.ComputeLayout.Bindings) if(binding.Kind==SDL3_StorageKind::ReadOnlyBuffer && binding.Binding==3) fadeLayout=binding.ElementStride==4;
    if(!spawnLayout || !systemLayout || !outputLayout || !fadeLayout) return fail("particle SPIR-V record ABI mismatch");
    if(!shader(ParticleGpuShader::Program::DrawVertex,SDL3_ShaderStage::Vertex,state.Vertex)
        || !shader(ParticleGpuShader::Program::DrawPixel,SDL3_ShaderStage::Fragment,state.Pixel)
        || !shader(ParticleGpuShader::Program::BlitVertex,SDL3_ShaderStage::Vertex,state.BlitVertex)
        || !shader(ParticleGpuShader::Program::BlitPixel,SDL3_ShaderStage::Fragment,state.BlitPixel)) return false;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo blit={};blit.vertex_shader=state.BlitVertex;blit.fragment_shader=state.BlitPixel;blit.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    blit.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;blit.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;blit.rasterizer_state.enable_depth_clip=true;blit.target_info.color_target_descriptions=&colour;blit.target_info.num_color_targets=1;
    state.Blit=SDL_CreateGPUGraphicsPipeline(state.Device,&blit);if(!state.Blit) return fail(SDL_GetError());
    SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;sampler.max_lod=1000;
    state.Linear=SDL_CreateGPUSampler(state.Device,&sampler);sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;state.Point=SDL_CreateGPUSampler(state.Device,&sampler);
    if(!state.Linear || !state.Point) return fail(SDL_GetError());
    SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D_ARRAY;texture.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    texture.width=texture.height=SliceSize;texture.layer_count_or_depth=GpuParticles::TEXTURE_SLICES;texture.num_levels=SliceMips;state.Sprites=SDL_CreateGPUTexture(state.Device,&texture);
    if(!state.Sprites || !clear_slices()) return fail(SDL_GetError());
    texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;texture.width=texture.height=texture.layer_count_or_depth=texture.num_levels=1;
    state.Ground=SDL_CreateGPUTexture(state.Device,&texture);const float zero=0;
    if(!state.Ground || !texture_upload(state.Ground,&zero,1,1,4)) return false;
    // Admission happens outside an active scene. Preflight eventual main HDR
    // and legacy/reflection formats before the manager gives up CPU particles.
    // An HDR-incapable device still supports the legacy route; the core cannot
    // create/route an HDR target there. Supported HDR formats must compile now.
    state.Pipelines.reserve(8);
    const unsigned formats[]={SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT};
    for(unsigned colourFormat:formats) {
        const bool available=SDL_GPUTextureSupportsFormat(state.Device,SDL_GPUTextureFormat(colourFormat),SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        if(!available) {if(colourFormat==formats[0]) return fail("required BGRA particle target unsupported");continue;}
        for(unsigned depthFormat:{0u,state.Frame->Depth_Format()})
            for(unsigned shaderType:{1u,2u}) if(!pipeline(colourFormat,depthFormat,shaderType)) return false;
    }
    state.Metrics.BufferBytes=uint64_t(state.Capacity)*(SpawnStride+OutputStride)+uint64_t(GpuParticles::SYSTEM_SLOTS)*(SystemStride+4);
    for(unsigned size=SliceSize;size;size>>=1) state.Metrics.ArrayBytes+=uint64_t(size)*size*4*GpuParticles::TEXTURE_SLICES;
    return true;
}
bool same_source(const SdlGpuParticleTextureView &a,const SdlGpuParticleTextureView &b) {
    return a.Texture==b.Texture && a.Identity==b.Identity && a.Generation==b.Generation && a.Width==b.Width && a.Height==b.Height && a.MipLevels==b.MipLevels;
}
bool draw_slices(const SdlGpuParticlesContext &context) {
    bool work=false;for(bool asked:state.Asked) work|=asked;if(!work) return true;
    // Texture mirroring can enqueue source uploads. Resolve all requested views
    // before Flush, so its copy pass precedes our independent resampling pass.
    auto sources=state.Sources;auto changed=state.Asked;std::string error;bool any=false;
    for(unsigned slice=0;slice<GpuParticles::TEXTURE_SLICES;++slice) if(state.Asked[slice]) {
        SdlGpuParticleTextureView view;
        if(state.Requested[slice] && !context.MirrorTexture(state.Requested[slice],view,error)) return fail(error);
        if(view.Texture && (!view.Width || !view.Height || !view.MipLevels || !view.Identity || !view.Generation)) return fail("invalid mirrored particle texture identity");
        sources[slice]=view;changed[slice]=!same_source(view,state.Sources[slice]);any|=changed[slice];
    }
    if(!any) {state.Asked.fill(false);return true;}
    if(!submit_readers()) return fail("sprite source uploads/readers failed to submit");
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(state.Device);if(!commands) return fail(SDL_GetError());
    for(unsigned slice=0;slice<GpuParticles::TEXTURE_SLICES;++slice) if(changed[slice]) {
        SDL_GPUColorTargetInfo target={};target.texture=state.Sprites;target.layer_or_depth_plane=slice;target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;target.cycle=false;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);if(!pass) {SDL_CancelGPUCommandBuffer(commands);return fail(SDL_GetError());}
        const auto &source=sources[slice];
        if(source.Texture) {
            float level=std::log2(float(source.Width)/SliceSize);float block[4]={std::clamp(level,0.0f,float(source.MipLevels-1)),0,0,0};
            SDL_PushGPUFragmentUniformData(commands,0,block,sizeof(block));SDL_BindGPUGraphicsPipeline(pass,state.Blit);
            SDL_GPUTextureSamplerBinding binding={source.Texture,state.Linear};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);SDL_DrawGPUPrimitives(pass,3,1,0,0);
        }
        SDL_EndGPURenderPass(pass);++state.Metrics.SliceCopies;
    }
    SDL_GenerateMipmapsForGPUTexture(commands,state.Sprites);
    if(!submit(commands)) return false;state.Sources=sources;state.Asked.fill(false);return true;
}
}

bool Supported(const SdlGpuParticlesContext &context) {
    SDL_GPUDevice *device=context.Frame ? context.Frame->Device() : nullptr;
    return device && context.DeviceGeneration && context.MirrorTexture
        && SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTURETYPE_2D_ARRAY,SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
        && SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
bool Prepare(const SdlGpuParticlesContext &context,unsigned capacity,bool *fresh,FlushHook flush) {
    if(!fresh) return false;*fresh=false;
    if(!Supported(context) || !capacity || capacity>UINT32_MAX/SpawnStride || capacity>65535*GroupSize || capacity>UINT32_MAX/6) {ReleaseDevice();return false;}
    if(!context.Frame->Register_Owner_Release_Hook(TryReleaseForFrame)) return false;
    if(same_device(context) && state.Capacity==capacity) return !state.Refused;
    if(!TryReleaseDevice()) return false;
    state.Frame=context.Frame;state.Device=context.Frame->Device();state.Generation=context.DeviceGeneration;state.Capacity=capacity;state.Flush=flush;
    state.Metrics.DeviceGeneration=state.Generation;state.Metrics.Capacity=capacity;*fresh=true;
    try {if(make_resources()) {std::fprintf(stderr,"GPUPARTICLES SDL: native compute ring=%u buffer-bytes=%llu array-bytes=%llu\n",capacity,(unsigned long long)state.Metrics.BufferBytes,(unsigned long long)state.Metrics.ArrayBytes);return true;}}
    catch(const std::bad_alloc &) {fail("native particle allocation failed");}
    SdlGpuFrame *frame=state.Frame;SDL_GPUDevice *device=state.Device;uint64_t generation=state.Generation;
    ReleaseDevice();state.Frame=frame;state.Device=device;state.Generation=generation;state.Capacity=capacity;state.Refused=true;return false;
}
static void write(const SdlGpuStorage::Buffer &buffer,unsigned first,const void *records,unsigned count,unsigned stride) {
    if(state.Refused || !state.Storage || !count) return;
    if(!records || uint64_t(first+uint64_t(count))*stride>buffer.Size) {fail("particle partial write out of range");return;}
    SdlStorageUpdate update={first*stride,count*stride,records};std::string error;
    if(!state.Storage->Write(buffer,&update,1,error)) {fail(error);return;}
    state.Changed=true;++state.Metrics.Writes;
}
void WriteSpawns(unsigned first,const void *records,unsigned count) {write(state.Spawns,first,records,count,SpawnStride);}
void WriteSystems(unsigned first,const void *records,unsigned count) {write(state.Systems,first,records,count,SystemStride);}
void WriteFades(const float *fades,unsigned count) {write(state.Fades,0,fades,count,4);}
void SetGround(const float *heights,unsigned width,unsigned height,const float mapping[4]) {
    if(state.Refused || !state.Device) return;
    if(!heights || !mapping || !width || !height || uint64_t(width)*height*4>UINT32_MAX) {fail("invalid particle ground");return;}
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *texture=SDL_CreateGPUTexture(state.Device,&info);if(!texture) {fail(SDL_GetError());return;}
    if(!texture_upload(texture,heights,width,height,4)) {SDL_ReleaseGPUTexture(state.Device,texture);return;}
    SDL_ReleaseGPUTexture(state.Device,state.Ground);state.Ground=texture;memcpy(state.GroundMap,mapping,sizeof(state.GroundMap));state.Changed=true;
}
void SetSliceTexture(unsigned slice,IDirect3DBaseTexture9 *texture) {if(slice>=GpuParticles::TEXTURE_SLICES || state.Refused) return;state.Requested[slice]=texture;state.Asked[slice]=true;}
void Evaluate(const SdlGpuParticlesContext &context,unsigned first,unsigned count,unsigned now,float smooth) {
    if(state.Refused || !same_device(context) || context.Pass==SdlGpuParticlesPass::Sun) return;
    if(first>=state.Capacity || count>state.Capacity || !std::isfinite(smooth)) {fail("invalid particle evaluation range/time");return;}
    if(!draw_slices(context)) return;state.Evaluated=count;if(!count) return;
    state.Metrics.MaximumEvaluated=std::max(state.Metrics.MaximumEvaluated,count);
    if(!state.Changed && first==state.First && count==state.Count && now==state.Now && smooth==state.Smooth) {++state.Metrics.ReusedEvaluations;return;}
    FrameBlock block={first,count,state.Capacity,now,smooth,{0,0,0},{}};memcpy(block.GroundMap,state.GroundMap,sizeof(block.GroundMap));std::string error;
    if(!state.Storage->Dispatch(state.Compute,state.ComputeLayout,{state.Spawns,state.Systems,state.Fades},{state.Output},{{0,&block,sizeof(block)}},(count+GroupSize-1)/GroupSize,1,1,error,{{state.Ground,state.Linear}})) {fail(error);return;}
    state.First=first;state.Count=count;state.Now=now;state.Smooth=smooth;state.Changed=false;++state.Metrics.Dispatches;
}
void Draw(const SdlGpuParticlesContext &context,unsigned shaderType,const float view[16],const float projection[16]) {
    if(state.Refused || !same_device(context) || !state.Evaluated || !view || !projection || (shaderType!=1 && shaderType!=2)
        || (context.Pass!=SdlGpuParticlesPass::MainScene && context.Pass!=SdlGpuParticlesPass::Reflection) || !context.Target.Colour || !context.Target.Width || !context.Target.Height) return;
    const bool main=context.Pass==SdlGpuParticlesPass::MainScene;
    const bool soft=soft_allowed(context);
    PassBlock block={};memcpy(block.View,view,sizeof(block.View));memcpy(block.Projection,projection,sizeof(block.Projection));
    block.SoftDepth[0]=projection[11]*projection[14];block.SoftDepth[1]=projection[11];block.SoftDepth[2]=projection[10];block.SoftDepth[3]=1.0f/12.0f;
    block.Count=state.Evaluated;block.Shader=shaderType;block.NewestFirst=shaderType==2;block.Soft=soft;block.Gain=main && shaderType==1 ? context.AdditiveGain : 1;
    const unsigned colour=SdlGpuFrame::Target_Colour_Format(context.Target),depth=context.Target.Depth ? state.Frame->Target_Depth_Format(context.Target) : 0;
    SDL_GPUGraphicsPipeline *native=pipeline(colour,depth,shaderType);if(!native) return;
    SdlRecordedDraw draw={};draw.Pipeline=native;draw.Count=state.Evaluated*6;
    memset(state.Frame->Stage(16,draw.VertexOffset),0,16);
    draw.VertexStorageCount=1;draw.VertexStorageBuffers[0]=state.Storage->Native(state.Output);
    draw.SamplerCount=2;draw.Textures[0]=state.Sprites;draw.Samplers[0]=state.Linear;draw.Textures[1]=soft ? context.SoftDepth : state.Ground;draw.Samplers[1]=state.Point;
    memcpy(draw.Viewport,context.Viewport,sizeof(draw.Viewport));memcpy(draw.Scissor,context.Scissor,sizeof(draw.Scissor));
    SdlNativeUniformBlock uniforms[]={{0,0,&block,sizeof(block)},{1,0,&block,sizeof(block)}};
    if(!state.Frame->Record_Native_Draw(context.Target,draw,uniforms,2)) {fail("native particle draw packet refused");return;}
    ++state.Metrics.Draws;
}
bool TryReleaseDevice() {
    if(state.Device) {
        if(!submit_readers()) return false;
        std::string error;if(state.Storage && !state.Storage->Close(error)) return false;
        if(state.Metrics.Dispatches || state.Metrics.Draws) std::fprintf(stderr,"GPUPARTICLES SDL: dispatches=%llu reused=%llu draws=%llu max-range=%u slice-copies=%llu writes=%llu buffer-bytes=%llu array-bytes=%llu\n",(unsigned long long)state.Metrics.Dispatches,(unsigned long long)state.Metrics.ReusedEvaluations,(unsigned long long)state.Metrics.Draws,state.Metrics.MaximumEvaluated,(unsigned long long)state.Metrics.SliceCopies,(unsigned long long)state.Metrics.Writes,(unsigned long long)state.Metrics.BufferBytes,(unsigned long long)state.Metrics.ArrayBytes);
        for(const auto &pipeline:state.Pipelines) SDL_ReleaseGPUGraphicsPipeline(state.Device,pipeline.Native);
        if(state.Compute) SDL_ReleaseGPUComputePipeline(state.Device,state.Compute);if(state.Blit) SDL_ReleaseGPUGraphicsPipeline(state.Device,state.Blit);
        for(SDL_GPUShader *shader:{state.Vertex,state.Pixel,state.BlitVertex,state.BlitPixel}) if(shader) SDL_ReleaseGPUShader(state.Device,shader);
        if(state.Linear) SDL_ReleaseGPUSampler(state.Device,state.Linear);if(state.Point) SDL_ReleaseGPUSampler(state.Device,state.Point);
        if(state.Ground) SDL_ReleaseGPUTexture(state.Device,state.Ground);if(state.Sprites) SDL_ReleaseGPUTexture(state.Device,state.Sprites);
        state.Storage.reset();
    }
    state=State();return true;
}
bool TryReleaseForFrame(SdlGpuFrame *owner) {
    return state.Frame!=owner || TryReleaseDevice();
}
void ReleaseDevice() {
    if(!TryReleaseDevice()) std::fprintf(stderr,"GPUPARTICLES SDL: release retained owner until queued readers submit or teardown discards them\n");
}
Statistics Stats() {return state.Metrics;}
bool ReadEvaluated(std::vector<uint8_t> &bytes,std::string &error) {return state.Storage && state.Storage->Read(state.Output,bytes,error);}
bool ReadSlice(unsigned slice,unsigned mip,std::vector<uint8_t> &bytes,std::string &error) {
    if(state.Refused || !state.Sprites || slice>=GpuParticles::TEXTURE_SLICES || mip>=SliceMips) {error="invalid particle slice readback";return false;}
    const unsigned size=SliceSize>>mip;std::vector<uint8_t> result(size*size*4);
    if(!submit_readers()) {error="slice readback flush failed";return false;}
    SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=result.size();SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(state.Device,&info);
    if(!transfer) {error=SDL_GetError();return false;}SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(state.Device);
    if(!commands) {SDL_ReleaseGPUTransferBuffer(state.Device,transfer);error=SDL_GetError();return false;}
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureRegion source={};source.texture=state.Sprites;source.layer=slice;source.mip_level=mip;source.w=source.h=size;source.d=1;
    SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);bool ok=fence && SDL_WaitForGPUFences(state.Device,true,&fence,1);
    void *mapped=ok ? SDL_MapGPUTransferBuffer(state.Device,transfer,false) : nullptr;
    if(mapped) {memcpy(result.data(),mapped,result.size());SDL_UnmapGPUTransferBuffer(state.Device,transfer);}else {ok=false;error=SDL_GetError();}
    if(fence) SDL_ReleaseGPUFence(state.Device,fence);SDL_ReleaseGPUTransferBuffer(state.Device,transfer);if(ok) bytes.swap(result);return ok;
}
}
