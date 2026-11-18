// Buffer-only shader/storage foundation. No particle provider is selected.
// CPU mode verifies real compiled SPIR-V layouts; --gpu uses actual SDL copies,
// compute, storage vertex bindings and readback. No headless/no-op GPU substitute.
#include "SdlGpuStorage.h"
#include "sdl3storage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

static unsigned checks=0, failures=0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %u: %s\n",__LINE__,#x); } } while (0)
static bool require(bool ok, const std::string &error) { if (!ok) std::printf("ERROR: %s\n",error.c_str()); CHECK(ok); return ok; }

// Field orders and expected offsets are those of ParticleClosedForm.h. Alpha
// words are scalar members: DX11's uint2 at byte68 needs relaxed vector layout;
// scalar declarations keep the exact 80-byte CPU ABI on Vulkan and Metal.
static const char Records[]=R"(
struct Spawn {
 float3 Position; uint Birth; float3 Velocity; float Lifetime;
 float Size; float SizeRate; float SizeDamping; float VelocityDamping;
 float Angle; float AngularRate; float AngularDamping; float ColorScale;
 uint SlotSeed; uint Alpha0; uint Alpha1; uint Pad;
};
struct AlphaSegment { float Time; uint End; };
struct ColorSegment { float3 Rate; uint End; };
struct SystemEntry {
 float3 Drift; float Gravity; float3 Color; uint Slice;
 uint ShaderType; uint Flags; uint AlphaCount; uint ColorCount;
 AlphaSegment Alpha[8]; ColorSegment Colour[8];
};
struct Particle { float3 Position; float Size; uint2 Colour; float Angle; uint Kind; };
)";
static std::string compute_source()
{
    // A layout/dispatch probe, not an implementation of particle closed-form
    // motion: touches ring wrap, the final nested keys and scalar fade indexing.
    return std::string(Records)+R"(
StructuredBuffer<Spawn> Spawns : register(t0, space0);
StructuredBuffer<SystemEntry> Systems : register(t1, space0);
StructuredBuffer<float> Fades : register(t2, space0);
RWStructuredBuffer<Particle> Evaluated : register(u0, space1);
cbuffer Frame : register(b0, space2) { uint First; uint Count; uint Capacity; uint Now; };
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
 uint i=id.x; if(i>=Count) return;
 Spawn spawn=Spawns[(First+i)%Capacity]; uint slot=spawn.SlotSeed & 65535;
 float k=float(Now-spawn.Birth+1); float fade=Fades[slot];
 Particle result;
 result.Position=(spawn.Position+(spawn.Velocity+Systems[slot].Drift)*k)*fade;
 result.Size=spawn.Size+Systems[slot].Alpha[7].Time;
 result.Colour=uint2(spawn.Alpha0,spawn.Alpha1);
 result.Angle=spawn.Angle+Systems[slot].Colour[7].Rate.z;
 result.Kind=(Systems[slot].ShaderType<<16)|Systems[slot].Slice;
 Evaluated[i]=result;
})";
}
static const char StorageVertex[]=R"(
StructuredBuffer<float4> Values : register(t0, space0);
struct Out { float4 Position:SV_Position; [[vk::location(0)]] float4 Colour:COLOR0; };
Out main(uint id:SV_VertexID) {
 Out o; o.Position=float4(id==1?3:-1,id==2?3:-1,0,1); o.Colour=Values[0]; return o;
})";
static const char Pixel[]=R"(
float4 main([[vk::location(0)]] float4 colour:COLOR0):SV_Target0 { return colour; }
)";
static void layouts(const SDL3_StorageLayout &layout)
{
    CHECK(layout.ReadOnlyBuffers==3 && layout.ReadWriteBuffers==1 && layout.UniformBuffers==1);
    CHECK(layout.Threads[0]==64 && layout.Threads[1]==1 && layout.Threads[2]==1);
    const std::vector<unsigned> spawn={0,12,16,28,32,36,40,44,48,52,56,60,64,68,72,76};
    const std::vector<unsigned> system={0,12,16,28,32,36,40,44,48,112};
    const std::vector<unsigned> output={0,12,16,24,28};
    bool foundSpawn=false,foundSystem=false,foundOutput=false;
    for (const auto &type:layout.Types) {
        if(type.Name=="Spawn") { foundSpawn=true; CHECK(type.MemberOffsets==spawn); CHECK(type.ArrayStride==80); }
        if(type.Name=="SystemEntry") { foundSystem=true; CHECK(type.MemberOffsets==system); CHECK(type.ArrayStride==240); }
        if(type.Name=="Particle") { foundOutput=true; CHECK(type.MemberOffsets==output); CHECK(type.ArrayStride==32); }
    }
    CHECK(foundSpawn && foundSystem && foundOutput);
    unsigned scalar=0;
    for(const auto &binding:layout.Bindings) if(binding.Kind==SDL3_StorageKind::ReadOnlyBuffer && binding.Binding==2) scalar=binding.ElementStride;
    CHECK(scalar==4);
}
static void cpu()
{
    std::string error,msl; std::vector<unsigned char> spirv,legacy,explicitStage; SDL3_StorageLayout layout;
    if(require(SDL3_Compile_HLSL_To_SPIRV(compute_source(),SDL3_ShaderStage::Compute,spirv,error),error)) {
        require(SDL3_Reflect_Storage_SPIRV(spirv,SDL3_ShaderStage::Compute,layout,error),error); layouts(layout);
        require(SDL3_Translate_SPIRV_To_MSL(spirv,SDL3_ShaderStage::Compute,msl,error),error);
        CHECK(msl.find("kernel ")!=std::string::npos && msl.find("[[buffer(4)]]")!=std::string::npos);
        CHECK(!SDL3_Reflect_Storage_SPIRV(spirv,SDL3_ShaderStage::Vertex,layout,error));
        spirv.pop_back(); CHECK(!SDL3_Reflect_Storage_SPIRV(spirv,SDL3_ShaderStage::Compute,layout,error));
    }
    std::string wrong=compute_source(); wrong.replace(wrong.find("t0, space0"),10,"t0, space3");
    require(SDL3_Compile_HLSL_To_SPIRV(wrong,SDL3_ShaderStage::Compute,spirv,error),error);
    CHECK(!SDL3_Reflect_Storage_SPIRV(spirv,SDL3_ShaderStage::Compute,layout,error));
    require(SDL3_Compile_HLSL_To_SPIRV(StorageVertex,true,legacy,error),error);
    require(SDL3_Compile_HLSL_To_SPIRV(StorageVertex,SDL3_ShaderStage::Vertex,explicitStage,error),error);
    CHECK(legacy==explicitStage);
    require(SDL3_Reflect_Storage_SPIRV(legacy,SDL3_ShaderStage::Vertex,layout,error),error);
    CHECK(layout.ReadOnlyBuffers==1 && layout.UniformBuffers==0 && layout.ReadWriteBuffers==0);
    require(SDL3_Compile_HLSL_To_SPIRV(Pixel,false,legacy,error),error);
    require(SDL3_Compile_HLSL_To_SPIRV(Pixel,SDL3_ShaderStage::Fragment,explicitStage,error),error);
    CHECK(legacy==explicitStage);
    unsigned samplers=99,uniforms=99; SDL3_Shader_Slots(legacy,false,samplers,uniforms); CHECK(samplers==0 && uniforms==0);
    unsigned a[4]={1,2,3,4}, b[2]={8,9};
    SdlStorageUpdate updates[]={{0,16,a},{4,8,b}};
    std::vector<SdlOwnedStorageUpdate> owned;
    require(Sdl_Prepare_Storage_Updates(32,updates,2,owned,error),error);
    a[0]=99; b[0]=77; unsigned result[8]={};
    for(const auto &update:owned) memcpy(reinterpret_cast<uint8_t *>(result)+update.DestinationOffset,update.Data.data(),update.Data.size());
    CHECK(result[0]==1 && result[1]==8 && result[2]==9 && result[3]==4 && result[4]==0);
    const size_t oldSize=owned.size();
    SdlStorageUpdate bad[]={{0,4,a},{28,8,b}};
    CHECK(!Sdl_Prepare_Storage_Updates(32,bad,2,owned,error)); CHECK(owned.size()==oldSize);
    bad[1]={UINT32_MAX-3,8,b}; CHECK(!Sdl_Prepare_Storage_Updates(UINT32_MAX,bad,2,owned,error));
    bad[1]={4,4,nullptr}; CHECK(!Sdl_Prepare_Storage_Updates(32,bad,2,owned,error));
    bad[1]={2,4,b}; CHECK(!Sdl_Prepare_Storage_Updates(32,bad,2,owned,error));
    CHECK(!Sdl_Prepare_Storage_Updates(32,nullptr,1,owned,error));
    CHECK(Sdl_Prepare_Storage_Updates(32,nullptr,0,owned,error) && owned.empty());
}

struct Spawn {
    float Position[3]; uint32_t Birth; float Velocity[3],Lifetime;
    float Size,SizeRate,SizeDamping,VelocityDamping,Angle,AngularRate,AngularDamping,ColorScale;
    uint32_t SlotSeed,Alpha[2],Pad;
};
struct System {
    float Drift[3],Gravity,Color[3]; uint32_t Slice,Shader,Flags,AlphaCount,ColorCount;
    struct { float Time; uint32_t End; } Alpha[8];
    struct { float Rate[3]; uint32_t End; } Colour[8];
};
struct Output { float Position[3],Size; uint32_t Colour[2]; float Angle; uint32_t Kind; };
static_assert(sizeof(Spawn)==80 && sizeof(System)==240 && sizeof(Output)==32,"CPU records");
static bool allocationFailure=false, stagingFailure=false, mapFailure=false;
static SDL_GPUBuffer *allocate_buffer(SDL_GPUDevice *d,const SDL_GPUBufferCreateInfo *i) {
    if(allocationFailure) { SDL_SetError("injected engine buffer allocation refusal"); return nullptr; } return SDL_CreateGPUBuffer(d,i);
}
static SDL_GPUTransferBuffer *allocate_transfer(SDL_GPUDevice *d,const SDL_GPUTransferBufferCreateInfo *i) {
    if(stagingFailure) { SDL_SetError("injected engine staging allocation refusal"); return nullptr; } return SDL_CreateGPUTransferBuffer(d,i);
}
static void *map_transfer(SDL_GPUDevice *d,SDL_GPUTransferBuffer *t,bool cycle) {
    if(mapFailure) { SDL_SetError("injected engine staging map refusal"); return nullptr; } return SDL_MapGPUTransferBuffer(d,t,cycle);
}
static bool equal_words(SdlGpuStorage &storage,const SdlGpuStorage::Buffer &buffer,const std::vector<uint32_t> &expected,std::string &error) {
    std::vector<uint8_t> bytes; if(!require(storage.Read(buffer,bytes,error),error)) return false;
    return bytes.size()==expected.size()*4 && memcmp(bytes.data(),expected.data(),bytes.size())==0;
}
static bool write_all(SdlGpuStorage &storage,const SdlGpuStorage::Buffer &buffer,const void *data,std::string &error) {
    SdlStorageUpdate update={0,buffer.Size,data}; return require(storage.Write(buffer,&update,1,error),error);
}

static void gpu_storage(SDL_GPUDevice *device)
{
    std::string error; unsigned flushes=0; bool flushSuccess=true; std::function<bool()> deferredReader;
    SdlGpuStorage storage(device,[&]() {
        ++flushes; if(!flushSuccess) return false;
        if(deferredReader) { auto reader=std::move(deferredReader); deferredReader=nullptr; return reader(); } return true;
    },[device](SDL_GPUBuffer *buffer) {SDL_ReleaseGPUBuffer(device,buffer);},{allocate_buffer,allocate_transfer,map_transfer});
    const auto usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ|SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    SdlGpuStorage::Buffer buffer;
    if(!require(storage.Create(320,usage,buffer,error),error)) return;
    std::vector<uint32_t> expected(80,0x5a1234ef); write_all(storage,buffer,expected.data(),error);
    uint32_t one[]={11,12},two[]={71,72};
    SdlStorageUpdate separated[]={{16,8,one},{280,8,two}};
    require(storage.Write(buffer,separated,2,error),error); expected[4]=11;expected[5]=12;expected[70]=71;expected[71]=72;
    CHECK(equal_words(storage,buffer,expected,error));
    // Two wrapped 80-byte spawn writes leave the other ring slots untouched.
    uint32_t slot[20]; std::fill(slot,slot+20,0x10101010); uint32_t next[20];std::fill(next,next+20,0x20202020);
    SdlStorageUpdate wrapped[]={{240,80,slot},{0,80,next}};
    require(storage.Write(buffer,wrapped,2,error),error);
    std::copy(slot,slot+20,expected.begin()+60);std::copy(next,next+20,expected.begin()); CHECK(equal_words(storage,buffer,expected,error));
    uint32_t overlapA[]={1,2,3,4},overlapB[]={8,9};SdlStorageUpdate overlaps[]={{80,16,overlapA},{84,8,overlapB}};
    require(storage.Write(buffer,overlaps,2,error),error);expected[20]=1;expected[21]=8;expected[22]=9;expected[23]=4;CHECK(equal_words(storage,buffer,expected,error));
    const unsigned beforeInvalid=flushes; SdlStorageUpdate invalid[]={{0,4,one},{316,8,two}};
    CHECK(!storage.Write(buffer,invalid,2,error));CHECK(flushes==beforeInvalid);CHECK(equal_words(storage,buffer,expected,error));
    stagingFailure=true;CHECK(!storage.Write(buffer,separated,2,error));stagingFailure=false;CHECK(equal_words(storage,buffer,expected,error));
    mapFailure=true;CHECK(!storage.Write(buffer,separated,2,error));mapFailure=false;CHECK(equal_words(storage,buffer,expected,error));
    const auto generation=buffer.Generation;SDL_GPUBuffer *old=storage.Native(buffer);
    allocationFailure=true;CHECK(!storage.Replace(buffer,640,usage,error));allocationFailure=false;
    CHECK(buffer.Generation==generation && storage.Native(buffer)==old);CHECK(equal_words(storage,buffer,expected,error));
    flushSuccess=false;CHECK(!storage.Write(buffer,separated,2,error));CHECK(!storage.Replace(buffer,640,usage,error));flushSuccess=true;
    CHECK(buffer.Generation==generation && storage.Native(buffer)==old);CHECK(equal_words(storage,buffer,expected,error));
    const auto stale=buffer;require(storage.Replace(buffer,640,usage,error),error);
    CHECK(buffer.Generation==generation+1 && storage.Native(stale)==nullptr);CHECK(!storage.Write(stale,separated,2,error));
    std::vector<uint32_t> replacement(160,0x87654321);write_all(storage,buffer,replacement.data(),error);
    for(unsigned frame=0;frame<48;++frame) {
        uint32_t word=frame;SdlStorageUpdate update={uint32_t((frame%160)*4),4,&word};require(storage.Write(buffer,&update,1,error),error);replacement[frame%160]=word;
    }
    CHECK(equal_words(storage,buffer,replacement,error));
    SdlGpuStorage::Buffer systems,fades;
    require(storage.Create(3*240,usage,systems,error),error);require(storage.Create(3*4,usage,fades,error),error);
    std::vector<uint32_t> systemWords(180,0x12345678),fadeWords={0x3f800000,0x3f000000,0x3e800000};
    write_all(storage,systems,systemWords.data(),error);write_all(storage,fades,fadeWords.data(),error);
    uint32_t entry[60];std::fill(entry,entry+60,0x33333333);SdlStorageUpdate systemUpdate={240,240,entry};require(storage.Write(systems,&systemUpdate,1,error),error);
    std::copy(entry,entry+60,systemWords.begin()+60);CHECK(equal_words(storage,systems,systemWords,error));
    uint32_t fade=0;SdlStorageUpdate fadeUpdate={4,4,&fade};require(storage.Write(fades,&fadeUpdate,1,error),error);fadeWords[1]=0;CHECK(equal_words(storage,fades,fadeWords,error));

    std::vector<unsigned char> spirv;SDL3_StorageLayout layout;
    if(!require(SDL3_Compile_HLSL_To_SPIRV(compute_source(),SDL3_ShaderStage::Compute,spirv,error),error)) return;
    SDL_GPUComputePipeline *pipeline=SDL3_Create_Storage_Compute_Pipeline(device,spirv,layout,error);
    if(!require(pipeline!=nullptr,error)) return;
    SdlGpuStorage::Buffer spawns,output,earlier;
    require(storage.Create(4*80,usage,spawns,error),error);require(storage.Create(4*32,usage,output,error),error);require(storage.Create(4*32,usage,earlier,error),error);
    Spawn births[4]={};System entries[3]={};float fadeValues[]={1,.5f,.25f};
    for(unsigned i=0;i<4;++i) { for(unsigned j=0;j<3;++j) {births[i].Position[j]=float(i*10+j);births[i].Velocity[j]=float(j+1);} births[i].Birth=8+i;births[i].Size=2+i;births[i].Angle=.125f*i;births[i].SlotSeed=i%3;births[i].Alpha[0]=100+i;births[i].Alpha[1]=200+i; }
    for(unsigned i=0;i<3;++i) {for(unsigned j=0;j<3;++j) entries[i].Drift[j]=float(i+j)*.125f;entries[i].Slice=7+i;entries[i].Shader=i+1;entries[i].Alpha[7].Time=.25f*(i+1);entries[i].Colour[7].Rate[2]=.5f*(i+1);}
    write_all(storage,spawns,births,error);write_all(storage,systems,entries,error);write_all(storage,fades,fadeValues,error);
    uint32_t frame[]={3,4,4,20};std::vector<SdlGpuStorage::Uniform> uniforms={{0,frame,sizeof(frame)}};
    auto dispatch=[&](const SdlGpuStorage::Buffer &target) {return require(storage.Dispatch(pipeline,layout,{spawns,systems,fades},{target},uniforms,1,1,1,error),error);};
    dispatch(output);
    auto verify=[&](const SdlGpuStorage::Buffer &target,const float *values) {
        std::vector<uint8_t> bytes;if(!require(storage.Read(target,bytes,error),error)) return;
        for(unsigned i=0;i<4;++i) { Output value;memcpy(&value,bytes.data()+i*32,32);const Spawn &birth=births[(3+i)%4];const System &system=entries[birth.SlotSeed];float k=float(20-birth.Birth+1);
            for(unsigned j=0;j<3;++j) CHECK(std::fabs(value.Position[j]-(birth.Position[j]+(birth.Velocity[j]+system.Drift[j])*k)*values[birth.SlotSeed])<.0001f);
            CHECK(value.Size==birth.Size+system.Alpha[7].Time);CHECK(value.Colour[0]==birth.Alpha[0] && value.Colour[1]==birth.Alpha[1]);CHECK(value.Angle==birth.Angle+system.Colour[7].Rate[2]);CHECK(value.Kind==((system.Shader<<16)|system.Slice));
        }
    };verify(output,fadeValues);
    // An actual reader command is recorded before the patch, but submitted only
    // by Write's flush callback. It must see old fades; later compute sees new.
    SDL_GPUCommandBuffer *readerCommands=SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUStorageBufferReadWriteBinding writer={storage.Native(earlier),false,0,0,0};
    SDL_PushGPUComputeUniformData(readerCommands,0,frame,sizeof(frame));
    SDL_GPUComputePass *readerPass=SDL_BeginGPUComputePass(readerCommands,nullptr,0,&writer,1);
    SDL_BindGPUComputePipeline(readerPass,pipeline);SDL_GPUBuffer *reads[]={storage.Native(spawns),storage.Native(systems),storage.Native(fades)};
    SDL_BindGPUComputeStorageBuffers(readerPass,0,reads,3);SDL_DispatchGPUCompute(readerPass,1,1,1);SDL_EndGPUComputePass(readerPass);
    deferredReader=[&] {return SDL_SubmitGPUCommandBuffer(readerCommands);};
    float changed=.75f;SdlStorageUpdate patch={4,4,&changed};require(storage.Write(fades,&patch,1,error),error);
    dispatch(output);verify(earlier,fadeValues);fadeValues[1]=changed;verify(output,fadeValues);
    CHECK(!storage.Dispatch(pipeline,layout,{spawns,systems,fades},{output},uniforms,0,1,1,error));
    CHECK(!storage.Dispatch(pipeline,layout,{spawns,systems,fades},{spawns},uniforms,1,1,1,error));
    SDL_ReleaseGPUComputePipeline(device,pipeline);
}

static void gpu_graphics(SDL_GPUDevice *device)
{
    std::string error;SdlGpuStorage storage(device,[] {return true;},[device](SDL_GPUBuffer *buffer) {SDL_ReleaseGPUBuffer(device,buffer);});SdlGpuStorage::Buffer buffer;
    if(!require(storage.Create(16,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,buffer,error),error)) return;
    const float colour[]={.25f,.5f,.75f,1};write_all(storage,buffer,colour,error);
    std::vector<unsigned char> spirv;SDL3_StorageLayout layout;
    require(SDL3_Compile_HLSL_To_SPIRV(StorageVertex,SDL3_ShaderStage::Vertex,spirv,error),error);
    SDL_GPUShader *vertex=SDL3_Create_Storage_Graphics_Shader(device,spirv,SDL3_ShaderStage::Vertex,layout,error);
    require(vertex!=nullptr,error);CHECK(layout.ReadOnlyBuffers==1);
    require(SDL3_Compile_HLSL_To_SPIRV(Pixel,false,spirv,error),error);
    SDL_GPUShader *pixel=SDL3_Create_Shader(device,spirv,false,error);require(pixel!=nullptr,error);
    if(!vertex || !pixel) return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=4;ti.height=4;ti.layer_count_or_depth=1;ti.num_levels=1;
    SDL_GPUTexture *texture=SDL_CreateGPUTexture(device,&ti);CHECK(texture);
    SDL_GPUColorTargetDescription description={};description.format=ti.format;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=vertex;pi.fragment_shader=pixel;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;pi.target_info.num_color_targets=1;pi.target_info.color_target_descriptions=&description;
    SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(device,&pi);CHECK(pipeline);
    SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;bi.size=64;SDL_GPUTransferBuffer *download=SDL_CreateGPUTransferBuffer(device,&bi);CHECK(download);
    if(texture && pipeline && download) {
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(device);SDL_GPUColorTargetInfo target={};target.texture=texture;target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_GPUBuffer *native=storage.Native(buffer);SDL_BindGPUVertexStorageBuffers(pass,0,&native,1);SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureRegion source={};source.texture=texture;source.w=4;source.h=4;source.d=1;SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=download;SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
        SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
        if(fence) {CHECK(SDL_WaitForGPUFences(device,true,&fence,1));const uint8_t *bytes=static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(device,download,false));CHECK(bytes);if(bytes) {for(unsigned i=0;i<16;++i) {CHECK(std::abs(int(bytes[i*4])-64)<=1);CHECK(std::abs(int(bytes[i*4+1])-128)<=1);CHECK(std::abs(int(bytes[i*4+2])-191)<=1);CHECK(bytes[i*4+3]==255);}SDL_UnmapGPUTransferBuffer(device,download);}SDL_ReleaseGPUFence(device,fence);}
    }
    if(download) SDL_ReleaseGPUTransferBuffer(device,download);if(pipeline) SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(texture) SDL_ReleaseGPUTexture(device,texture);SDL_ReleaseGPUShader(device,vertex);SDL_ReleaseGPUShader(device,pixel);
}
int main(int argc,char **argv)
{
    cpu();
    if(argc>1 && !strcmp(argv[1],"--gpu")) {
        if(!SDL_Init(SDL_INIT_VIDEO)) {std::printf("GPU UNAVAILABLE: %s\n",SDL_GetError());return 77;}
        SDL_GPUDevice *device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL,true,nullptr);
        if(!device) {std::printf("GPU UNAVAILABLE: %s\n",SDL_GetError());SDL_Quit();return 77;}
        const char *name=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown");
        std::printf("GPU: driver=%s device=%s\n",SDL_GetGPUDeviceDriver(device),name);
        gpu_storage(device);gpu_graphics(device);SDL_DestroyGPUDevice(device);SDL_Quit();
    }
    std::printf("SDL storage: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
