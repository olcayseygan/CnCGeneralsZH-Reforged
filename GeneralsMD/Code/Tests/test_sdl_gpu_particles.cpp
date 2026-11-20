// Actual closed-form particle shaders and native provider. CPU mode compiles
// SPIR-V/MSL; --gpu uses real ring/array/compute/ordered frame draw/readback.
#include "PreRTS.h"
#include "GameClient/ParticleClosedForm.h"
#include "W3DDevice/GameClient/ParticleGpuShader.h"
#include "SdlGpuParticles.h"
#include "SdlGpuStorage.h"
#include "sdl3storage.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
static unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL line%u: %s\n",__LINE__,#x);}}while(0)
static bool require(bool ok,const std::string &error) {if(!ok) std::printf("ERROR: %s\n",error.c_str());CHECK(ok);return ok;}
static void cpu() {
    using Program=ParticleGpuShader::Program;
    const Program programs[]={Program::Evaluate,Program::DrawVertex,Program::DrawPixel,Program::BlitVertex,Program::BlitPixel};
    const SDL3_ShaderStage stages[]={SDL3_ShaderStage::Compute,SDL3_ShaderStage::Vertex,SDL3_ShaderStage::Fragment,SDL3_ShaderStage::Vertex,SDL3_ShaderStage::Fragment};
    std::string error,msl;std::vector<unsigned char> spirv;
    for(unsigned i=0;i<5;++i) {
        const std::string hlsl=ParticleGpuShader::Sdl(programs[i]);CHECK(!hlsl.empty());
        if(!require(SDL3_Compile_HLSL_To_SPIRV(hlsl,stages[i],spirv,error),error)) continue;
        SDL3_StorageLayout layout;if(!require(SDL3_Reflect_Storage_SPIRV(spirv,stages[i],layout,error),error)) continue;
        require(SDL3_Translate_SPIRV_To_MSL(spirv,stages[i],msl,error),error);CHECK(msl.find("main0")!=std::string::npos);
        if(i!=3) {
            const char *name=i==0 ? "Frame" : i==4 ? "Blit" : "DrawPass";
            const std::vector<unsigned> offsets=i==0 ? std::vector<unsigned>({0,4,8,12,16,20,24,28,32})
                : i==4 ? std::vector<unsigned>({0,4,8,12}) : std::vector<unsigned>({0,64,128,144,148,152,156,160,164,168,172});
            bool found=false;for(const auto &type:layout.Types) if(type.Name==name) {found=true;CHECK(type.MemberOffsets==offsets);}CHECK(found);
        }
        if(i==0) {
            CHECK(layout.SampledTextures==1 && layout.ReadOnlyBuffers==3 && layout.ReadWriteBuffers==1 && layout.UniformBuffers==1);
            CHECK(layout.Threads[0]==64 && layout.Threads[1]==1 && layout.Threads[2]==1);
            bool spawn=false,system=false,output=false,fade=false;
            for(const auto &type:layout.Types) {
                if(type.Name=="Spawn") {spawn=true;CHECK(type.ArrayStride==sizeof(ParticleGpuSpawn));CHECK(type.MemberOffsets==std::vector<unsigned>({0,12,16,28,32,36,40,44,48,52,56,60,64,68,72,76}));}
                if(type.Name=="SystemEntry") {system=true;CHECK(type.ArrayStride==sizeof(ParticleGpuSystemEntry));CHECK(type.MemberOffsets==std::vector<unsigned>({0,12,16,28,32,36,40,44,48,112}));}
                if(type.Name=="Particle") {output=true;CHECK(type.ArrayStride==32);CHECK(type.MemberOffsets==std::vector<unsigned>({0,12,16,24,28}));}
            }
            for(const auto &binding:layout.Bindings) if(binding.Kind==SDL3_StorageKind::ReadOnlyBuffer && binding.Binding==3) {fade=true;CHECK(binding.ElementStride==4);}
            CHECK(spawn && system && output && fade);
        } else if(i==1) {
            CHECK(layout.ReadOnlyBuffers==1 && layout.SampledTextures==0 && layout.UniformBuffers==1);
        } else if(i==2) CHECK(layout.SampledTextures==2 && layout.ReadOnlyBuffers==0 && layout.UniformBuffers==1);
    }
    std::string bad=ParticleGpuShader::Sdl(Program::Evaluate);
    auto at=bad.find("register(s0, space0)");bad.replace(at,20,"register(s1, space0)");
    if(require(SDL3_Compile_HLSL_To_SPIRV(bad,SDL3_ShaderStage::Compute,spirv,error),error)) {SDL3_StorageLayout layout;CHECK(!SDL3_Reflect_Storage_SPIRV(spirv,SDL3_ShaderStage::Compute,layout,error));}
}
#ifndef PARTICLE_CPU_ONLY
struct Output {float Position[3],Size;uint32_t Colour[2];float Angle;uint32_t Kind;};
static_assert(sizeof(Output)==32,"evaluated ABI");
static bool refuseReaderFlush=false;
static unsigned flushHookCalls=0;
static bool controlled_flush(SdlGpuFrame *frame) {++flushHookCalls;return !refuseReaderFlush && frame->Flush();}
static SDL_GPUTexture *sourceTexture=nullptr;static uint64_t sourceGeneration=1;static unsigned sourceWidth=256,sourceLevels=9;
static bool mirror(IDirect3DBaseTexture9 *,SdlGpuParticleTextureView &view,std::string &) {
    view={sourceTexture,77,sourceGeneration,sourceWidth,sourceWidth,sourceLevels};return true;
}
static SdlGpuParticlesContext context(SdlGpuFrame *frame,uint64_t generation=1) {
    SdlGpuParticlesContext c;c.Frame=frame;c.DeviceGeneration=generation;c.FrameSerial=frame->Scene_Frame();c.Target=frame->Back_Buffer_Target();c.Pass=SdlGpuParticlesPass::MainScene;c.MirrorTexture=mirror;
    c.MainViewKey=19;
    c.Viewport[2]=c.Target.Width;c.Viewport[3]=c.Target.Height;c.Viewport[5]=1;c.Scissor[2]=c.Target.Width;c.Scissor[3]=c.Target.Height;return c;
}
static float half(uint16_t value) {
    unsigned sign=(value>>15)&1,exponent=(value>>10)&31,mantissa=value&1023;
    const float magnitude=exponent ? std::ldexp(1.0f+mantissa/1024.0f,int(exponent)-15) : std::ldexp(float(mantissa),-24);
    return sign ? -magnitude : magnitude;
}
static float ground_height(const float *heights,unsigned width,unsigned height,float x,float y,const float mapping[4]) {
    float fx=std::clamp((x*mapping[0]+mapping[2])*width-.5f,0.0f,float(width-1));
    float fy=std::clamp((y*mapping[1]+mapping[3])*height-.5f,0.0f,float(height-1));
    unsigned ix=unsigned(fx),iy=unsigned(fy),nx=std::min(ix+1,width-1),ny=std::min(iy+1,height-1);fx-=ix;fy-=iy;
    float top=heights[iy*width+ix]*(1-fx)+heights[iy*width+nx]*fx,bottom=heights[ny*width+ix]*(1-fx)+heights[ny*width+nx]*fx;return top*(1-fy)+bottom*fy;
}
static void verify(const std::vector<uint8_t> &bytes,const std::vector<ParticleGpuSpawn> &births,const std::vector<ParticleGpuSystemEntry> &systems,const std::vector<float> &fades,unsigned first,unsigned count,unsigned now,float smooth,const float *ground,const float mapping[4]) {
    CHECK(bytes.size()>=count*32);
    for(unsigned i=0;i<count;++i) {
        Output gpu;memcpy(&gpu,bytes.data()+i*32,32);const auto &spawn=births[(first+i)%births.size()];unsigned slot=spawn.m_systemSlotSeed&65535;const auto &entry=systems[slot];
        float k=std::max(float(now-spawn.m_birthFrame)+smooth,1.0f);ParticleClosedFormState reference;particleClosedFormEvaluate(spawn,entry,k,&reference);
        const bool dead=reference.m_dead || !(entry.m_flags&1) || fades[slot]<=0;
        CHECK(gpu.Kind==(dead ? 0u : (entry.m_shaderType<<16)|(entry.m_textureSlice&65535)));
        if(dead) {CHECK(gpu.Size==0 && gpu.Colour[0]==0 && gpu.Colour[1]==0);continue;}
        if(entry.m_flags&2) reference.m_pos.z=std::max(reference.m_pos.z,ground_height(ground,4,4,reference.m_pos.x,reference.m_pos.y,mapping));
        const float expected[]={reference.m_pos.x,reference.m_pos.y,reference.m_pos.z};
        for(unsigned c=0;c<3;++c) CHECK(std::fabs(gpu.Position[c]-expected[c])<.002f*std::max(1.0f,std::fabs(expected[c])));
        CHECK(std::fabs(gpu.Size-reference.m_size)<.002f*std::max(1.0f,std::fabs(reference.m_size)));
        CHECK(std::fabs(gpu.Angle-reference.m_angleZ)<.002f*std::max(1.0f,std::fabs(reference.m_angleZ)));
        float colour[]={reference.m_color.red,reference.m_color.green,reference.m_color.blue,reference.m_alpha};
        if(entry.m_shaderType==1) for(unsigned c=0;c<3;++c) colour[c]*=fades[slot];else colour[3]*=fades[slot];
        for(unsigned c=0;c<4;++c) {colour[c]=std::clamp(colour[c],0.0f,1.0f);const uint16_t packed=uint16_t(gpu.Colour[c/2]>>((c%2)*16));CHECK(std::fabs(half(packed)-colour[c])<.0015f);}
    }
}
static bool upload_source(SDL_GPUDevice *device,bool white=false) {
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=info.height=sourceWidth;info.layer_count_or_depth=1;info.num_levels=sourceLevels;
    sourceTexture=SDL_CreateGPUTexture(device,&info);CHECK(sourceTexture);if(!sourceTexture) return false;
    // Distinct solid colour per source mip proves closest-mip sampling. Level1
    // is128x128, so the array's level0 must be green rather than source red0.
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(device);SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
    std::vector<SDL_GPUTransferBuffer *> transfers;
    for(unsigned mip=0,size=sourceWidth;mip<sourceLevels;++mip,size=std::max(1u,size/2)) {
        SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=size*size*4;auto *transfer=SDL_CreateGPUTransferBuffer(device,&ti);CHECK(transfer);transfers.push_back(transfer);
        auto *bytes=static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device,transfer,false));CHECK(bytes);
        for(unsigned p=0;p<size*size;++p) {bytes[p*4]=white || mip==0?255:0;bytes[p*4+1]=white || mip==1?255:0;bytes[p*4+2]=white || mip>1?255:0;bytes[p*4+3]=255;}SDL_UnmapGPUTransferBuffer(device,transfer);
        SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;SDL_GPUTextureRegion target={};target.texture=sourceTexture;target.mip_level=mip;target.w=target.h=size;target.d=1;SDL_UploadToGPUTexture(copy,&source,&target,false);
    }
    SDL_EndGPUCopyPass(copy);bool ok=SDL_SubmitGPUCommandBuffer(commands);for(auto *transfer:transfers) SDL_ReleaseGPUTransferBuffer(device,transfer);CHECK(ok);return ok;
}
static void draw_probes(SdlGpuFrame &frame,SdlGpuParticlesContext c) {
    std::string error;ParticleGpuSpawn births[2]={};ParticleGpuSystemEntry entries[2]={};float fades[2]={1,1};
    for(unsigned i=0;i<2;++i) {births[i].m_birthFrame=1;births[i].m_pos[2]=.5f;births[i].m_size=1;births[i].m_sizeRateDamping=births[i].m_velDamping=births[i].m_angularDamping=1;births[i].m_systemSlotSeed=i;births[i].m_alphaKeys[0]=births[i].m_alphaKeys[1]=0x80808080;entries[i].m_flags=1;entries[i].m_shaderType=2;}
    entries[0].m_color[0]=1;entries[1].m_color[2]=1;
    SdlGpuParticles::WriteSpawns(0,births,2);SdlGpuParticles::WriteSystems(0,entries,2);SdlGpuParticles::WriteFades(fades,2);SdlGpuParticles::Evaluate(c,0,2,1,0);
    float matrix[16]={};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    auto render=[&](SdlGpuParticlesContext ctx,unsigned shader,std::vector<uint8_t> &bytes) {
        frame.Set_Target(ctx.Target);frame.Clear_Back_Buffer(true,true,false,0,1,0);
        SdlGpuParticles::Draw(ctx,shader,matrix,matrix);
        return require(frame.Read_Back(ctx.Target.Colour,ctx.Target.Width,ctx.Target.Height,bytes),error);
    };
    SDL_GPUTextureCreateInfo depthInfo={};depthInfo.type=SDL_GPU_TEXTURETYPE_2D;depthInfo.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;depthInfo.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;depthInfo.width=c.Target.Width;depthInfo.height=c.Target.Height;depthInfo.layer_count_or_depth=depthInfo.num_levels=1;
    SDL_GPUTexture *softTexture=SDL_CreateGPUTexture(frame.Device(),&depthInfo);CHECK(softTexture);
    if(softTexture) {uint32_t offset=0;memset(frame.Upload_Space(c.Target.Width*c.Target.Height*4,offset),0,c.Target.Width*c.Target.Height*4);frame.Queue_Texture_Upload(softTexture,0,c.Target.Width,c.Target.Height,offset);require(frame.Flush(),"soft reference depth upload");}
    auto centre=[](const std::vector<uint8_t> &bytes,const SdlGpuParticlesContext &ctx,unsigned channel) {return bytes[(size_t(ctx.Target.Height/2)*ctx.Target.Width+ctx.Target.Width/2)*4+channel];};
    std::vector<uint8_t> pixels;if(render(c,2,pixels)) {CHECK(std::abs(int(centre(pixels,c,2))-128)<=2);CHECK(std::abs(int(centre(pixels,c,0))-64)<=2);}
    // The newest blue birth was drawn first, then the older red birth. A depth
    // sort or oldest-first alpha would produce the opposite128/64 channels.
    entries[0].m_shaderType=1;entries[0].m_color[0]=.2f;SdlGpuParticles::WriteSystems(0,entries,1);SdlGpuParticles::Evaluate(c,0,1,1,0);
    c.AdditiveGain=2;if(render(c,1,pixels)) CHECK(std::abs(int(centre(pixels,c,2))-102)<=2);
    for(float z:{-.5f,1.5f}) {births[0].m_pos[2]=z;SdlGpuParticles::WriteSpawns(0,births,1);SdlGpuParticles::Evaluate(c,0,1,1,0);if(render(c,1,pixels)) CHECK(centre(pixels,c,2)==0);}
    births[0].m_pos[2]=.5f;SdlGpuParticles::WriteSpawns(0,births,1);SdlGpuParticles::Evaluate(c,0,1,1,0);

    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPUTextureFormat(SdlGpuFrame::Target_Format());info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=64;info.height=96;info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *reflection=SDL_CreateGPUTexture(frame.Device(),&info);CHECK(reflection);
    if(reflection) {
        auto mirrorContext=c;mirrorContext.Pass=SdlGpuParticlesPass::Reflection;mirrorContext.Target={reflection,frame.Depth_For(64,96),64,96,frame.Depth_Format(),SdlGpuFrame::Target_Format()};mirrorContext.Viewport[2]=64;mirrorContext.Viewport[3]=96;mirrorContext.Scissor[2]=64;mirrorContext.Scissor[3]=96;mirrorContext.AdditiveGain=99;
        mirrorContext.SoftAllowed=true;mirrorContext.SoftDepth=softTexture;mirrorContext.SoftCapture.Source=mirrorContext.Target;mirrorContext.SoftCapture.Frame=mirrorContext.FrameSerial;
        if(render(mirrorContext,1,pixels)) CHECK(std::abs(int(centre(pixels,mirrorContext,2))-51)<=2);
        frame.Set_Target(c.Target);require(frame.Flush(),"reflection submitted before retirement");SDL_ReleaseGPUTexture(frame.Device(),reflection);
    }
    if(SDL_GPUTextureSupportsFormat(frame.Device(),SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
        entries[0].m_color[0]=.8f;SdlGpuParticles::WriteSystems(0,entries,1);SdlGpuParticles::Evaluate(c,0,1,1,0);
        info.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;info.width=info.height=64;SDL_GPUTexture *hdr=SDL_CreateGPUTexture(frame.Device(),&info);CHECK(hdr);
        if(hdr) {
            auto wide=c;wide.Target={hdr,frame.Depth_For(64,64),64,64,frame.Depth_Format(),SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT};wide.Viewport[2]=wide.Viewport[3]=64;wide.Scissor[2]=wide.Scissor[3]=64;wide.AdditiveGain=2;
            auto prepared=SdlGpuParticles::Stats();frame.Set_Target(wide.Target);frame.Clear_Back_Buffer(true,true,false,0,1,0);SdlGpuParticles::Draw(wide,1,matrix,matrix);require(frame.Flush(),"actual HDR storage draw");CHECK(SdlGpuParticles::Stats().PipelineVariants==prepared.PipelineVariants);
            SDL_GPUTransferBufferCreateInfo transferInfo={};transferInfo.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;transferInfo.size=64*64*8;auto *download=SDL_CreateGPUTransferBuffer(frame.Device(),&transferInfo);CHECK(download);
            if(download) {auto *commands=SDL_AcquireGPUCommandBuffer(frame.Device());auto *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureRegion region={};region.texture=hdr;region.w=region.h=64;region.d=1;SDL_GPUTextureTransferInfo target={};target.transfer_buffer=download;SDL_DownloadFromGPUTexture(copy,&region,&target);SDL_EndGPUCopyPass(copy);auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);if(fence) {CHECK(SDL_WaitForGPUFences(frame.Device(),true,&fence,1));auto *values=static_cast<const uint16_t *>(SDL_MapGPUTransferBuffer(frame.Device(),download,false));CHECK(values);if(values) {CHECK(std::fabs(half(values[(32*64+32)*4])-1.6f)<.004f);SDL_UnmapGPUTransferBuffer(frame.Device(),download);}SDL_ReleaseGPUFence(frame.Device(),fence);}SDL_ReleaseGPUTransferBuffer(frame.Device(),download);}
            frame.Set_Target(c.Target);SDL_ReleaseGPUTexture(frame.Device(),hdr);
        }
        entries[0].m_color[0]=.2f;SdlGpuParticles::WriteSystems(0,entries,1);SdlGpuParticles::Evaluate(c,0,1,1,0);
    } else std::printf("SKIP HDR particle readback: device format unsupported; legacy route retained\n");
    auto sun=c;sun.Pass=SdlGpuParticlesPass::Sun;auto before=SdlGpuParticles::Stats();if(render(sun,1,pixels)) CHECK(centre(pixels,sun,2)==0);CHECK(SdlGpuParticles::Stats().Draws==before.Draws);
    // Supplied valid scene depth enables the exact soft formula; with this
    // orthographic identity probe both reconstructed distances are zero.
    auto soft=c;soft.SoftAllowed=true;soft.SoftDepth=softTexture;soft.SoftCapture.Source=soft.Target;soft.SoftCapture.Frame=soft.FrameSerial;soft.SoftCapture.Depth=true;soft.SoftCapture.Generation=frame.Scene_Generation();soft.SoftCapture.Phase=SdlSceneCapturePhase::BeforeTransparent;
    soft.SoftCapture.ViewKey=soft.MainViewKey;
    if(render(soft,1,pixels)) CHECK(centre(pixels,soft,2)==0);
    // A real queued native draw still owns every provider handle when a
    // transient command acquisition/refusal stops replacement and release.
    frame.Set_Target(c.Target);frame.Clear_Back_Buffer(true,true,false,0,1,0);SdlGpuParticles::Draw(c,1,matrix,matrix);CHECK(frame.Has_Pending_Work());
    const auto retained=SdlGpuParticles::Stats();bool replacementFresh=true;refuseReaderFlush=true;
    CHECK(!SdlGpuParticles::Prepare(c,96,&replacementFresh,controlled_flush));CHECK(!replacementFresh);
    CHECK(!SdlGpuParticles::TryReleaseDevice());CHECK(SdlGpuParticles::Stats().Capacity==retained.Capacity && SdlGpuParticles::Stats().DeviceGeneration==retained.DeviceGeneration);
    refuseReaderFlush=false;require(frame.Read_Back(c.Target.Colour,c.Target.Width,c.Target.Height,pixels),"retained old reader replay");CHECK(std::abs(int(centre(pixels,c,2))-102)<=2);
    const auto validSoft=soft;
    auto hard=[&](SdlGpuParticlesContext invalid) {if(render(invalid,1,pixels)) CHECK(std::abs(int(centre(pixels,invalid,2))-102)<=2);};
    ++soft.SoftCapture.Frame;hard(soft);
    soft=validSoft;++soft.SoftCapture.Generation;hard(soft);
    soft=validSoft;++soft.SoftCapture.ViewKey;hard(soft);
    soft=validSoft;soft.SoftCapture.ViewKey=0;hard(soft);
    soft=validSoft;soft.MainViewKey=0;soft.SoftCapture.ViewKey=0;hard(soft);
    soft=validSoft;soft.SoftCapture.Phase=SdlSceneCapturePhase::OpaqueScene;hard(soft);
    soft=validSoft;++soft.FrameSerial;soft.SoftCapture.Frame=soft.FrameSerial;hard(soft);
    soft=validSoft;soft.SoftDepth=c.Target.Colour;hard(soft);
    soft=validSoft;soft.SoftDepth=c.Target.Depth;hard(soft);
    soft=validSoft;soft.SoftCapture.Source.ColourFormat=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;hard(soft);
    soft=validSoft;soft.SoftCapture.Source.DepthFormat=SDL_GPU_TEXTUREFORMAT_D16_UNORM;hard(soft);
    fades[0]=0;SdlGpuParticles::WriteFades(fades,2);SdlGpuParticles::Evaluate(c,0,1,1,0);if(render(c,1,pixels)) CHECK(centre(pixels,c,2)==0);
    SdlGpuParticles::Evaluate(c,0,0,1,0);before=SdlGpuParticles::Stats();if(render(c,1,pixels)) CHECK(centre(pixels,c,2)==0);CHECK(SdlGpuParticles::Stats().Draws==before.Draws);
    require(frame.Flush(),"soft reference depth retirement");if(softTexture) SDL_ReleaseGPUTexture(frame.Device(),softTexture);
}
static void storage_destructor_probe(SdlGpuFrame &frame) {
    std::string error;const char *vertex=R"(
StructuredBuffer<float4> Values : register(t0,space0);
struct Out {float4 Position:SV_Position; [[vk::location(0)]] float4 Colour:COLOR0;};
Out main(uint id:SV_VertexID) {Out o;o.Position=float4(id==1?3:-1,id==2?3:-1,0,1);o.Colour=Values[0];return o;})";
    const char *pixel="float4 main([[vk::location(0)]] float4 c:COLOR0):SV_Target0{return c;}";
    std::vector<unsigned char> spirv;SDL3_StorageLayout layout;
    require(SDL3_Compile_HLSL_To_SPIRV(vertex,SDL3_ShaderStage::Vertex,spirv,error),error);auto *vs=SDL3_Create_Storage_Graphics_Shader(frame.Device(),spirv,SDL3_ShaderStage::Vertex,layout,error);require(vs!=nullptr,error);
    require(SDL3_Compile_HLSL_To_SPIRV(pixel,false,spirv,error),error);auto *ps=SDL3_Create_Shader(frame.Device(),spirv,false,error);require(ps!=nullptr,error);
    SdlTarget target=frame.Back_Buffer_Target();target.Depth=nullptr;target.DepthFormat=0;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPUTextureFormat(SdlGpuFrame::Target_Colour_Format(target));SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=vs;info.fragment_shader=ps;info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&colour;
    auto *pipeline=SDL_CreateGPUGraphicsPipeline(frame.Device(),&info);CHECK(pipeline);if(!pipeline || !vs || !ps) return;
    unsigned retired=0;SdlGpuStorage::Buffer buffer;
    auto storage=std::make_unique<SdlGpuStorage>(frame.Device(),[&] {return !frame.Has_Pending_Work() || controlled_flush(&frame);},[&](SDL_GPUBuffer *native) {++retired;frame.Release_After_Batch(nullptr,native);});
    require(storage->Create(16,SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,buffer,error),error);const float rgba[]={.25f,.5f,.75f,1};SdlStorageUpdate update={0,16,rgba};require(storage->Write(buffer,&update,1,error),error);
    frame.Set_Target(target);frame.Clear_Back_Buffer(true,false,false,0,1,0);SdlRecordedDraw draw={};draw.Pipeline=pipeline;draw.Count=3;memset(frame.Stage(16,draw.VertexOffset),0,16);draw.VertexStorageCount=1;draw.VertexStorageBuffers[0]=storage->Native(buffer);draw.Viewport[2]=target.Width;draw.Viewport[3]=target.Height;draw.Viewport[5]=1;draw.Scissor[2]=target.Width;draw.Scissor[3]=target.Height;require(frame.Record_Native_Draw(target,draw,nullptr,0),"owned buffer reader packet");
    refuseReaderFlush=true;SDL_GPUBuffer *native=storage->Native(buffer);CHECK(!storage->Close(error));CHECK(storage->Native(buffer)==native);storage.reset();CHECK(retired==1 && frame.Has_Pending_Work());
    refuseReaderFlush=false;std::vector<uint8_t> bytes;require(frame.Read_Back(target.Colour,target.Width,target.Height,bytes),"destructor retirement reader replay");unsigned at=(target.Height/2*target.Width+target.Width/2)*4;CHECK(std::abs(int(bytes[at])-191)<=1 && std::abs(int(bytes[at+1])-128)<=1 && std::abs(int(bytes[at+2])-64)<=1);
    SDL_ReleaseGPUGraphicsPipeline(frame.Device(),pipeline);SDL_ReleaseGPUShader(frame.Device(),vs);SDL_ReleaseGPUShader(frame.Device(),ps);frame.Set_Target(frame.Back_Buffer_Target());
}
static void gpu() {
    std::string error;std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,128,128,error));if(!require(bool(frame),error)) return;
    struct ReleaseBeforeFrame {~ReleaseBeforeFrame(){SdlGpuParticles::ReleaseDevice();}} releaseBeforeFrame;
    std::printf("GPU: driver=%s device=%s\n",SDL_GetGPUDeviceDriver(frame->Device()),SDL_GetStringProperty(SDL_GetGPUDeviceProperties(frame->Device()),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"));
    auto c=context(frame.get());bool fresh=false;CHECK(SdlGpuParticles::Supported(c));
    if(!require(SdlGpuParticles::Prepare(c,64,&fresh,controlled_flush),"native Prepare")) return;CHECK(fresh);
    require(SdlGpuParticles::Prepare(c,64,&fresh),"reuse Prepare");CHECK(!fresh);
    std::vector<ParticleGpuSpawn> births(64);std::vector<ParticleGpuSystemEntry> systems(8);std::vector<float> fades(8,1);
    const float damping[]={0,.7f,.9999f,1,1.02f};
    for(unsigned slot=0;slot<systems.size();++slot) {
        auto &entry=systems[slot];entry.m_shaderType=(slot%2)+1;entry.m_textureSlice=slot;entry.m_flags=slot==7?0:(1|(slot%3==0?2:0));entry.m_gravity=-.007f;entry.m_drift[0]=.01f*slot;entry.m_drift[1]=-.02f;entry.m_color[0]=.8f;entry.m_color[1]=.6f;entry.m_color[2]=.4f;entry.m_alphaSegmentCount=entry.m_colorSegmentCount=7;
        for(unsigned key=0;key<8;++key) {entry.m_alphaSegment[key]={8,8*(key+1)};entry.m_colorSegment[key].m_end=8*(key+1);for(unsigned rgb=0;rgb<3;++rgb) entry.m_colorSegment[key].m_rate[rgb]=-.001f*rgb;}
    }
    fades[5]=0;fades[6]=.35f;
    for(unsigned i=0;i<births.size();++i) {
        auto &spawn=births[i];spawn.m_birthFrame=10+i%15;spawn.m_pos[0]=float(int(i%9)-4);spawn.m_pos[1]=float(int(i%7)-3);spawn.m_pos[2]=5;spawn.m_vel[0]=.12f;spawn.m_vel[1]=-.08f;spawn.m_vel[2]=.3f;spawn.m_lifetime=i%4==0?0:float(35+i);spawn.m_size=1.5f;spawn.m_sizeRate=.02f;spawn.m_sizeRateDamping=damping[i%5];spawn.m_velDamping=damping[(i+1)%5];spawn.m_angleZ=.12f*i;spawn.m_angularRateZ=.015f;spawn.m_angularDamping=damping[(i+2)%5];spawn.m_systemSlotSeed=(i%8)|(123u<<16);spawn.m_alphaKeys[0]=0xcceef0ff;spawn.m_alphaKeys[1]=0x808898aa;
    }
    float heights[16];for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) heights[y*4+x]=10+2*x+3*y;float mapping[]={.025f,.025f,.5f,.5f};
    SdlGpuParticles::WriteSpawns(0,births.data(),births.size());SdlGpuParticles::WriteSystems(0,systems.data(),systems.size());SdlGpuParticles::WriteFades(fades.data(),fades.size());SdlGpuParticles::SetGround(heights,4,4,mapping);
    SdlGpuParticles::Evaluate(c,0,64,60,.25f);std::vector<uint8_t> bytes;require(SdlGpuParticles::ReadEvaluated(bytes,error),error);verify(bytes,births,systems,fades,0,64,60,.25f,heights,mapping);
    auto stats=SdlGpuParticles::Stats();SdlGpuParticles::Evaluate(c,0,64,60,.25f);CHECK(SdlGpuParticles::Stats().Dispatches==stats.Dispatches && SdlGpuParticles::Stats().ReusedEvaluations==stats.ReusedEvaluations+1);
    births[63].m_pos[0]=88;births[0].m_pos[0]=99;SdlGpuParticles::WriteSpawns(63,&births[63],1);SdlGpuParticles::WriteSpawns(0,&births[0],1);
    SdlGpuParticles::Evaluate(c,60,12,61,.5f);require(SdlGpuParticles::ReadEvaluated(bytes,error),error);verify(bytes,births,systems,fades,60,12,61,.5f,heights,mapping);
    stats=SdlGpuParticles::Stats();systems[0].m_drift[2]=.25f;SdlGpuParticles::WriteSystems(0,&systems[0],1);SdlGpuParticles::Evaluate(c,60,12,61,.5f);CHECK(SdlGpuParticles::Stats().Dispatches==stats.Dispatches+1);
    fades[0]=0;SdlGpuParticles::WriteFades(fades.data(),fades.size());SdlGpuParticles::Evaluate(c,60,12,61,.5f);require(SdlGpuParticles::ReadEvaluated(bytes,error),error);verify(bytes,births,systems,fades,60,12,61,.5f,heights,mapping);
    require(upload_source(frame->Device()),"upload mipped source");SdlGpuParticles::SetSliceTexture(0,reinterpret_cast<IDirect3DBaseTexture9 *>(uintptr_t(1)));SdlGpuParticles::SetSliceTexture(2,reinterpret_cast<IDirect3DBaseTexture9 *>(uintptr_t(1)));SdlGpuParticles::Evaluate(c,0,0,61,0);
    for(unsigned mip=0;mip<8;++mip) {require(SdlGpuParticles::ReadSlice(0,mip,bytes,error),error);CHECK(bytes[0]==0 && bytes[1]==255 && bytes[2]==0 && bytes[3]==255);require(SdlGpuParticles::ReadSlice(255,mip,bytes,error),error);CHECK(std::all_of(bytes.begin(),bytes.end(),[](uint8_t b){return b==0;}));}
    auto previous=sourceTexture;++sourceGeneration;sourceWidth=sourceLevels=1;require(upload_source(frame->Device(),true),"replacement white source");SdlGpuParticles::SetSliceTexture(0,reinterpret_cast<IDirect3DBaseTexture9 *>(uintptr_t(1)));SdlGpuParticles::Evaluate(c,0,0,61,0);
    require(SdlGpuParticles::ReadSlice(0,0,bytes,error),error);CHECK(bytes[0]==255 && bytes[1]==255 && bytes[2]==255 && bytes[3]==255);
    require(SdlGpuParticles::ReadSlice(2,7,bytes,error),error);CHECK(bytes[0]==0 && bytes[1]==255 && bytes[2]==0 && bytes[3]==255);SDL_ReleaseGPUTexture(frame->Device(),previous);
    draw_probes(*frame,c);
    storage_destructor_probe(*frame);
    // Final teardown is the only operation permitted to discard. No new GPU
    // command acquisition is needed once pending CPU readers are discarded.
    float identity[16]={};identity[0]=identity[5]=identity[10]=identity[15]=1;
    const float visibleFade=1;SdlGpuParticles::WriteFades(&visibleFade,1);SdlGpuParticles::Evaluate(c,0,1,1,0);
    frame->Set_Target(c.Target);frame->Clear_Back_Buffer(true,true,false,0,1,0);SdlGpuParticles::Draw(c,1,identity,identity);CHECK(frame->Has_Pending_Work());
    refuseReaderFlush=true;CHECK(!SdlGpuParticles::TryReleaseDevice());frame->Discard_Pending_Work_For_Teardown();CHECK(!frame->Has_Pending_Work());const unsigned calls=flushHookCalls;CHECK(SdlGpuParticles::TryReleaseDevice());CHECK(SdlGpuParticles::TryReleaseDevice());CHECK(flushHookCalls==calls);refuseReaderFlush=false;
    require(SdlGpuParticles::Prepare(c,64,&fresh,controlled_flush),"fresh owner after explicit teardown discard");CHECK(fresh);

    previous=sourceTexture;sourceTexture=nullptr;++sourceGeneration;SdlGpuParticles::SetSliceTexture(0,nullptr);SdlGpuParticles::Evaluate(c,0,0,61,0);require(SdlGpuParticles::ReadSlice(0,0,bytes,error),error);CHECK(std::all_of(bytes.begin(),bytes.end(),[](uint8_t b){return b==0;}));SDL_ReleaseGPUTexture(frame->Device(),previous);
    CHECK(SdlGpuParticles::Stats().ArrayBytes==22369280);CHECK(SdlGpuParticles::Stats().BufferBytes==64*112+16384*244);
    stats=SdlGpuParticles::Stats();c.Pass=SdlGpuParticlesPass::Sun;SdlGpuParticles::Evaluate(c,0,64,90,0);CHECK(SdlGpuParticles::Stats().Dispatches==stats.Dispatches);c.Pass=SdlGpuParticlesPass::Other;CHECK(SdlGpuParticles::Supported(c));require(SdlGpuParticles::Prepare(c,64,&fresh),"non-scene admission");CHECK(!fresh);
    c.Pass=SdlGpuParticlesPass::MainScene;require(SdlGpuParticles::Prepare(c,96,&fresh),"capacity replacement");CHECK(fresh && SdlGpuParticles::Stats().Capacity==96);
    ++c.DeviceGeneration;require(SdlGpuParticles::Prepare(c,96,&fresh),"device epoch replacement");CHECK(fresh && SdlGpuParticles::Stats().DeviceGeneration==c.DeviceGeneration);
    // Final frame destruction must release the native provider even with a
    // queued draw and a deliberately refused ordinary reader flush.
    CHECK(SdlGpuParticles::TryReleaseDevice());
    require(SdlGpuParticles::Prepare(c,32,&fresh,controlled_flush),"owner teardown fixture");
    frame->Set_Target(c.Target);frame->Clear_Back_Buffer(true,true,false,0,1,0);
    refuseReaderFlush=true;frame.reset();refuseReaderFlush=false;
    CHECK(SdlGpuParticles::Stats().Capacity==0);
    frame.reset(SdlGpuFrame::Create(nullptr,64,64,error));require(bool(frame),"frame after owner teardown");
    if(!frame) return;
    c=context(frame.get(),c.DeviceGeneration+1);
    CHECK(!SdlGpuParticles::Prepare(c,0,&fresh));CHECK(SdlGpuParticles::Stats().Capacity==0);
    CHECK(!SdlGpuParticles::Prepare(c,UINT32_MAX,&fresh));SdlGpuParticles::ReleaseDevice();
    frame.reset();frame.reset(SdlGpuFrame::Create(nullptr,64,64,error));if(require(bool(frame),"actual GPU device recreation")) {auto recreated=context(frame.get(),c.DeviceGeneration+1);require(SdlGpuParticles::Prepare(recreated,32,&fresh,controlled_flush),"new device owner without stale frame pointer");CHECK(fresh);CHECK(SdlGpuParticles::TryReleaseDevice());}

}
#endif
int main(int argc,char **argv) {
    cpu();
    if(argc>1 && !strcmp(argv[1],"--gpu")) {
#ifdef PARTICLE_CPU_ONLY
        std::printf("GPU UNAVAILABLE: CPU-only compile\n");return 77;
#else
        if(!SDL_Init(SDL_INIT_VIDEO)) {std::printf("GPU UNAVAILABLE: %s\n",SDL_GetError());return 77;}
        gpu();SDL_Quit();
#endif
    }
    std::printf("SDL GPU particles: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
