#include "SdlPostDistortion.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include "sdl3target.h"
#include "dx11runtime.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <memory>
#include <new>

namespace {
unsigned format_slot(unsigned format)
{
    if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) return 0;
    if (format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) return 1;
    return format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT ? 2 : 3;
}
bool same_target(SdlGpuFrame &frame, const SdlTarget &a, const SdlTarget &b)
{
    return a.Colour == b.Colour && a.Depth == b.Depth && a.Width == b.Width && a.Height == b.Height
        && SdlGpuFrame::Target_Colour_Format(a) == SdlGpuFrame::Target_Colour_Format(b)
        && frame.Target_Depth_Format(a) == frame.Target_Depth_Format(b);
}
SdlSceneCaptureRequest normalized(SdlGpuFrame &frame, SdlSceneCaptureRequest request)
{
    request.Source.ColourFormat = SdlGpuFrame::Target_Colour_Format(request.Source);
    request.Source.DepthFormat = frame.Target_Depth_Format(request.Source);
    return request;
}
}

namespace {
void replace(std::string &text, const std::string &from, const std::string &to)
{
    size_t at=0;while((at=text.find(from,at))!=std::string::npos){text.replace(at,from.size(),to);at+=to.size();}
}
void heat_varyings(std::string &source)
{
    const char *fields[]={"float2 Corner : TEXCOORD0;","float2 Screen : TEXCOORD1;",
        "float4 Heat : TEXCOORD2;","float4 Planes : TEXCOORD3;","float4 Across : TEXCOORD4;","float2 Offset : TEXCOORD5;"};
    for(unsigned i=0;i<6;++i)replace(source,fields[i],"[[vk::location("+std::to_string(i+2)+")]] "+fields[i]);
}
}
bool Sdl_Post_Heat_Vertex_Source(std::string &source)
{
    source=PostShader_Heat_Splat_Vertex_Source();
    replace(source,"Buffer<float4> Splats : register(t0);",R"HLSL(
struct HeatRecord {
 [[vk::location(0)]] float4 Shape : TEXCOORD0;
 [[vk::location(1)]] float4 Heat : TEXCOORD1;
 [[vk::location(2)]] float4 Planes : TEXCOORD2;
 [[vk::location(3)]] float4 Across : TEXCOORD3;
};
)HLSL");
    replace(source,"main(uint index : SV_VertexID)","main(uint index : SV_VertexID, HeatRecord record)");
    replace(source,"Splats.Load(splat * 4)","record.Shape");
    replace(source,"Splats.Load(splat * 4 + 1)","record.Heat");
    replace(source,"Splats.Load(splat * 4 + 2)","record.Planes");
    replace(source,"Splats.Load(splat * 4 + 3)","record.Across");
    heat_varyings(source);return true;
}
bool Sdl_Post_Distortion_Pixel_Source(PostShaderProgram program, std::string &source)
{
    if (program != POST_SHADER_WARP && program != POST_SHADER_HEAT_SPLAT && program != POST_SHADER_HEAT_HAZE) { source.clear(); return false; }
    source = PostShader_Pixel_Source(program);
    const std::string from = "VertexOutput";
    size_t at = 0;
    while ((at = source.find(from, at)) != std::string::npos) {
        source.replace(at, from.size(), "Input"); at += 5;
    }
    if(program==POST_SHADER_HEAT_SPLAT)heat_varyings(source);
    return SDL3_Shader_Retarget(source, false);
}

struct SdlPostDistortion::Impl
{
    struct Program {
        SDL_GPUShader *Shader = nullptr;
        SDL_GPUGraphicsPipeline *Pipelines[3] = {};
        unsigned Samplers = 0, Uniforms = 0;
    } Copy, Warp, HeatComposite;
    SDL_GPUShader *HeatVertex = nullptr, *HeatPixel = nullptr, *ClearPixel = nullptr;
    SDL_GPUGraphicsPipeline *HeatPipeline = nullptr, *ClearPipeline = nullptr;
    unsigned HeatSamplers = 0, HeatUniforms = 0;
    SDL_GPUBuffer *HeatBuffer = nullptr;
    TestFailure Failure = TestFailure::None;
    bool fault(TestFailure point) const { return Failure == point; }
    SDL_GPUTexture *HeatField = nullptr, *HeatCopy = nullptr;
    unsigned HeatWidth = 0, HeatHeight = 0, HeatFormat = 0, HeatCount = 0;
    std::vector<float> HeatRecords;
    SdlSceneCaptureRequest HeatMain, HeatApplied;
    const SdlPostProcess *HeatPost = nullptr;
    bool HeatReady = false, HeatDone = false;
    SdlGpuFrame *Owner = nullptr;
    const SdlPostProcess *Post = nullptr;
    SDL_GPUDevice *Device = nullptr;
    SDL_GPUShader *Vertex = nullptr;
    SDL_GPUSampler *Sampler = nullptr;
    SDL_GPUTexture *Snapshot = nullptr;
    unsigned Width = 0, Height = 0, Format = 0, Count = 0;
    DX11PostWarp Warps[DX11_POST_WARP_LIMIT] = {};
    SdlSceneCaptureRequest Main, Applied;
    SdlTarget Output = {};
    bool Ready = false, Done = false;
    std::string LastError;
    unsigned long long Creations = 0;

    bool fail(const std::string &reason)
    {
        const std::string message = "native SDL distortion: " + reason;
        if (message != LastError) std::fprintf(stderr, "%s\n", message.c_str());
        LastError = message; return false;
    }
    bool program(Program &program, PostShaderProgram id, unsigned format)
    {
        if (fault(TestFailure::Program)) return fail("injected program preparation failure");
        std::string source, error; std::vector<unsigned char> spirv;
        if (!Vertex) {
            if (!Sdl_Post_Vertex_Source(source) || !SDL3_Compile_HLSL_To_SPIRV(source, true, spirv, error)
                || !(Vertex = SDL3_Create_Shader(Device, spirv, true, error))) return fail("vertex: " + error);
            ++Creations;
        }
        if (!Sampler) {
            SDL_GPUSamplerCreateInfo info = {};
            info.min_filter = info.mag_filter = SDL_GPU_FILTER_LINEAR;
            info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            info.max_lod = 1000;
            Sampler = SDL_CreateGPUSampler(Device, &info);
            if (!Sampler) return fail(SDL_GetError());
            ++Creations;
        }
        if (!program.Shader) {
            const bool adapted = id == POST_SHADER_COPY ? Sdl_Post_Pixel_Source(id, source)
                : Sdl_Post_Distortion_Pixel_Source(id, source);
            if (!adapted || !SDL3_Compile_HLSL_To_SPIRV(source, false, spirv, error)
                || !(program.Shader = SDL3_Create_Shader(Device, spirv, false, error))) return fail("pixel: " + error);
            SDL3_Shader_Slots(spirv, false, program.Samplers, program.Uniforms); ++Creations;
            if (program.Samplers > 2 || program.Uniforms > 1)
                return fail("warp program exceeds native sampler/constant bindings");
        }
        auto &pipeline = program.Pipelines[format_slot(format)];
        if (!pipeline) {
            if (fault(TestFailure::Pipeline)) return fail("injected pipeline preparation failure");
            SDL_GPUColorTargetDescription colour = {}; colour.format = SDL_GPUTextureFormat(format);
            if (id == POST_SHADER_WARP || id == POST_SHADER_HEAT_HAZE) {
                colour.blend_state.enable_color_write_mask = true;
                colour.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
            }
            SDL_GPUGraphicsPipelineCreateInfo info = {};
            info.vertex_shader = Vertex; info.fragment_shader = program.Shader;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.rasterizer_state.enable_depth_clip = true;
            info.target_info.num_color_targets = 1; info.target_info.color_target_descriptions = &colour;
            pipeline = SDL_CreateGPUGraphicsPipeline(Device, &info);
            if (!pipeline) return fail(SDL_GetError());
            ++Creations;
        }
        return true;
    }
    bool image(const SdlTarget &output)
    {
        const unsigned format = SdlGpuFrame::Target_Colour_Format(output);
        if (Snapshot && Width == output.Width && Height == output.Height && Format == format) return true;
        if (fault(TestFailure::Target)) return fail("injected snapshot allocation failure");
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = SDL_GPUTextureFormat(format);
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = output.Width; info.height = output.Height;
        info.layer_count_or_depth = info.num_levels = 1;
        SDL_GPUTexture *next = SDL_CreateGPUTexture(Device, &info);
        if (!next) return fail(SDL_GetError());
        if (Snapshot) Owner->Release_After_Batch(Snapshot, nullptr);
        Snapshot = next; Width = output.Width; Height = output.Height; Format = format; ++Creations;
        return true;
    }
    bool heat_resources(const SdlTarget &scene)
    {
        const unsigned format=SdlGpuFrame::Target_Colour_Format(scene);
        if(!program(Copy,POST_SHADER_COPY,format) || !program(HeatComposite,POST_SHADER_HEAT_HAZE,format))return false;
        std::string source,error;std::vector<unsigned char> spirv;
        if(!HeatVertex) {
            if(!Sdl_Post_Heat_Vertex_Source(source)
                || !SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,error)
                || !(HeatVertex=SDL3_Create_Shader(Device,spirv,true,error)))return fail("heat vertex: "+error);
            ++Creations;
        }
        if(!HeatPixel) {
            if(!Sdl_Post_Distortion_Pixel_Source(POST_SHADER_HEAT_SPLAT,source)
                || !SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error)
                || !(HeatPixel=SDL3_Create_Shader(Device,spirv,false,error)))return fail("heat pixel: "+error);
            SDL3_Shader_Slots(spirv,false,HeatSamplers,HeatUniforms);++Creations;
            if(HeatSamplers>2 || HeatUniforms>1)return fail("heat splat exceeds declared native bindings");
        }
        if(!ClearPixel) {
            source="float4 main(float4 p:SV_Position):SV_Target{return float4(0,0,0,0);}";
            if(!SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error)
                || !(ClearPixel=SDL3_Create_Shader(Device,spirv,false,error)))return fail("heat clear: "+error);
            ++Creations;
        }
        SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        SDL_GPUGraphicsPipelineCreateInfo info={};info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.enable_depth_clip=true;info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&colour;
        if(!ClearPipeline) {
            info.vertex_shader=Vertex;info.fragment_shader=ClearPixel;
            ClearPipeline=SDL_CreateGPUGraphicsPipeline(Device,&info);if(!ClearPipeline)return fail(SDL_GetError());++Creations;
        }
        if(!HeatPipeline) {
            info.vertex_shader=HeatVertex;info.fragment_shader=HeatPixel;
            SDL_GPUVertexBufferDescription buffer={};buffer.slot=0;buffer.pitch=DIRECT3D11_HEAT_SPLAT_FLOATS*sizeof(float);
            buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_INSTANCE;
            SDL_GPUVertexAttribute attributes[4]={};
            for(unsigned i=0;i<4;++i){attributes[i].location=i;attributes[i].buffer_slot=0;
                attributes[i].format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;attributes[i].offset=i*16;}
            info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;
            info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=4;
            colour.blend_state.enable_blend=true;
            colour.blend_state.src_color_blendfactor=colour.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            colour.blend_state.src_alpha_blendfactor=colour.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
            colour.blend_state.color_blend_op=colour.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            HeatPipeline=SDL_CreateGPUGraphicsPipeline(Device,&info);if(!HeatPipeline)return fail(SDL_GetError());++Creations;
        }
        if(!HeatBuffer) {
            if(fault(TestFailure::Buffer))return fail("injected heat instance allocation failure");
            SDL_GPUBufferCreateInfo buffer={};buffer.usage=SDL_GPU_BUFFERUSAGE_VERTEX;
            buffer.size=DIRECT3D11_HEAT_SPLAT_LIMIT*DIRECT3D11_HEAT_SPLAT_FLOATS*sizeof(float);
            HeatBuffer=SDL_CreateGPUBuffer(Device,&buffer);if(!HeatBuffer)return fail(SDL_GetError());++Creations;
        }
        if(HeatField && HeatCopy && HeatWidth==scene.Width && HeatHeight==scene.Height && HeatFormat==format)return true;
        if(fault(TestFailure::Target))return fail("injected heat image allocation failure");
        SDL_GPUTextureCreateInfo image={};image.type=SDL_GPU_TEXTURETYPE_2D;
        image.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        image.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        image.width=(scene.Width+1)/2;image.height=(scene.Height+1)/2;image.layer_count_or_depth=image.num_levels=1;
        SDL_GPUTexture *field=SDL_CreateGPUTexture(Device,&image);
        if(!field)return fail(SDL_GetError());
        image.width=scene.Width;image.height=scene.Height;image.format=SDL_GPUTextureFormat(format);
        SDL_GPUTexture *copy=fault(TestFailure::TargetAfterOne) ? nullptr : SDL_CreateGPUTexture(Device,&image);
        if(!copy){SDL_ReleaseGPUTexture(Device,field);return fail("heat colour snapshot allocation failed");}
        if(HeatField)Owner->Release_After_Batch(HeatField,nullptr);
        if(HeatCopy)Owner->Release_After_Batch(HeatCopy,nullptr);
        HeatField=field;HeatCopy=copy;HeatWidth=scene.Width;HeatHeight=scene.Height;HeatFormat=format;Creations+=2;
        return true;
    }
    bool pass(const Program &program, SDL_GPUTexture *source, const SdlTarget &target, bool warp)
    {
        if (!source || source == target.Colour) return fail("source aliases distortion destination");
        SdlTarget colour = target; colour.Depth = nullptr; colour.DepthFormat = 0;
        SdlRecordedDraw draw = {};
        draw.Pipeline = program.Pipelines[format_slot(SdlGpuFrame::Target_Colour_Format(colour))];
        draw.Count = 3; draw.Viewport[2] = float(colour.Width); draw.Viewport[3] = float(colour.Height);
        draw.Viewport[5] = 1; draw.Scissor[2] = int(colour.Width); draw.Scissor[3] = int(colour.Height);
        draw.SamplerCount = program.Samplers;
        draw.Textures[0] = draw.Textures[1] = source; draw.Samplers[0] = draw.Samplers[1] = Sampler;
        PostProcessConstants constants = {};
        constants.TexelSize[0] = 1.0f / colour.Width; constants.TexelSize[1] = 1.0f / colour.Height;
        constants.TexelSize[2] = float(colour.Width); constants.TexelSize[3] = float(colour.Height);
        if (warp) {
            constants.WarpInfo[0] = float(Count); constants.WarpInfo[1] = float(colour.Width) / colour.Height;
            for (unsigned i = 0; i < Count; ++i) {
                const auto &w = Warps[i];
                const float centre[] = {w.CentreX, w.CentreY, w.Radius, w.Pull};
                const float ring[] = {w.RingRadius, w.RingWidth, w.RingStrength, w.RingSquash};
                std::memcpy(constants.WarpCentre[i], centre, sizeof(centre));
                std::memcpy(constants.WarpRing[i], ring, sizeof(ring));
            }
        }
        const SdlNativeUniformBlock block = {1, 0, &constants, sizeof(constants)};
        return Owner->Record_Native_Draw(colour, draw, program.Uniforms ? &block : nullptr,
            program.Uniforms ? 1 : 0) || fail("native distortion packet refused");
    }
};

SdlPostDistortion::SdlPostDistortion() : State(new Impl) {}
SdlPostDistortion::~SdlPostDistortion()
{
    if (State->Owner && !Release(*State->Owner))
        std::fprintf(stderr, "native SDL distortion: owner must resolve failed Release before teardown\n");
    delete State;
}
bool SdlPostDistortion::Prepare_Blast_Warps(SdlGpuFrame &frame, const SdlPostProcess &post,
    const SdlTarget &scene, const SdlTarget &output, const SdlSceneCaptureRequest &main,
    const DX11PostWarp *warps, unsigned count)
{
    Impl &s = *State; s.Ready = false; s.Count = 0; s.Post = nullptr;
    std::memset(s.Warps, 0, sizeof(s.Warps));
    if (!count) { s.Main = {}; s.Applied = {}; s.Output = {}; s.Done = false; s.LastError.clear(); return true; }
    if (s.Owner && s.Owner != &frame) return s.fail("Release the previous owner before rebinding");
    if (main.Phase != SdlSceneCapturePhase::OpaqueScene || !post.Scene_Matches_Identity(frame, scene, main))
        return s.fail("requires explicit unresolved main post identity");
    if (!output.Colour || output.Colour == scene.Colour || output.Colour == scene.Depth
        || output.Width != scene.Width || output.Height != scene.Height
        || format_slot(SdlGpuFrame::Target_Colour_Format(output)) > 2)
        return s.fail("requires distinct matching final output and supported colour format");
    if (!warps) return s.fail("null warp records");
    count = count > DX11_POST_WARP_LIMIT ? DX11_POST_WARP_LIMIT : count;
    for (unsigned i = 0; i < count; ++i) {
        const auto &w = warps[i];
        const float values[] = {w.CentreX,w.CentreY,w.Radius,w.Pull,w.RingRadius,w.RingWidth,w.RingStrength,w.RingSquash};
        for (float value : values) if (!std::isfinite(value)) return s.fail("non-finite warp record");
    }
    s.Owner = &frame; s.Device = frame.Device();
    const unsigned format = SdlGpuFrame::Target_Colour_Format(output);
    if (!s.program(s.Copy, POST_SHADER_COPY, format) || !s.program(s.Warp, POST_SHADER_WARP, format)
        || !s.image(output)) return false;
    s.Main = normalized(frame, main); s.Output = output; s.Post = &post;
    s.Count = count; std::memcpy(s.Warps, warps, count * sizeof(*warps));
    s.Ready = true; s.LastError.clear(); return true;
}
bool SdlPostDistortion::Apply_Blast_Warp(SdlGpuFrame &frame, const SdlTarget &scene, const SdlTarget &output)
{
    Impl &s = *State;
    struct Consume {
        Impl &State;
        ~Consume() {
            State.Ready = false; State.Count = 0; State.Post = nullptr;
            std::memset(State.Warps, 0, sizeof(State.Warps));
        }
    } consume{s};
    auto current = s.Main; current.Source = scene; current.Frame = frame.Scene_Frame();
    current.Generation = frame.Scene_Generation(); current = normalized(frame, current);
    if (!s.Ready || s.Owner != &frame || !s.Post || !s.Main.Same_Identity(current)
        || !same_target(frame, output, s.Output) || !s.Post->Scene_Has_Finished(frame, scene, output, s.Main))
        return s.fail("blast warp requires its completed main scene and exact final output");
    if (s.Done && s.Applied.Same_Identity(s.Main)) return s.fail("blast warp already recorded for this scene");
    SdlTarget snapshot = {s.Snapshot, nullptr, s.Width, s.Height, 0, s.Format};
    if (!s.pass(s.Copy, output.Colour, snapshot, false) || !s.pass(s.Warp, s.Snapshot, output, true)) return false;
    s.Applied = s.Main; s.Done = true; s.Ready = false; s.Count = 0; s.Post = nullptr;
    std::memset(s.Warps, 0, sizeof(s.Warps)); return true;
}
bool SdlPostDistortion::Prepare_Heat(SdlGpuFrame &frame, const SdlPostProcess &post, const SdlTarget &scene,
    const SdlSceneCaptureRequest &main, const float *records, unsigned count)
{
    Impl &s=*State;s.HeatReady=false;s.HeatPost=nullptr;s.HeatCount=0;s.HeatRecords.clear();
    if(!count){s.HeatMain={};s.LastError.clear();return true;}
    if(s.Owner && s.Owner!=&frame)return s.fail("Release the previous owner before rebinding heat");
    if(main.Phase!=SdlSceneCapturePhase::OpaqueScene || !post.Scene_Matches_Identity(frame,scene,main)
        || !scene.Depth || format_slot(SdlGpuFrame::Target_Colour_Format(scene))>2)
        return s.fail("heat requires explicit unresolved main identity and scene depth");
    if(!records)return s.fail("null heat records");
    count=std::min(count,DIRECT3D11_HEAT_SPLAT_LIMIT);
    for(unsigned i=0;i<count*DIRECT3D11_HEAT_SPLAT_FLOATS;++i)
        if(!std::isfinite(records[i]))return s.fail("non-finite heat record");
    s.Owner=&frame;s.Device=frame.Device();
    try {
        if(!s.heat_resources(scene))return false;
        s.HeatRecords.assign(records,records+count*DIRECT3D11_HEAT_SPLAT_FLOATS);
    }catch(const std::bad_alloc &){return s.fail("heat preparation allocation failed");}
    s.HeatMain=normalized(frame,main);s.HeatPost=&post;s.HeatCount=count;s.HeatReady=true;s.LastError.clear();return true;
}
bool SdlPostDistortion::Apply_Heat(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &beforePost)
{
    Impl &s=*State;
    struct Consume {
        Impl &State;
        ~Consume(){State.HeatReady=false;State.HeatPost=nullptr;State.HeatCount=0;State.HeatRecords.clear();}
    }consume{s};
    auto capture=normalized(frame,beforePost.Capture);capture.Phase=SdlSceneCapturePhase::OpaqueScene;
    if(!s.HeatReady || s.Owner!=&frame || !s.HeatPost || !s.HeatPost->Scene_Matches_Identity(frame,scene,s.HeatMain)
        || beforePost.Capture.Phase!=SdlSceneCapturePhase::BeforePost || !beforePost.Capture.Depth
        || !capture.Same_Identity(s.HeatMain))return s.fail("heat requires fresh matching BeforePost main depth");
    if(!beforePost.SampledDepth || beforePost.SampledDepth==scene.Colour || beforePost.SampledDepth==scene.Depth
        || beforePost.DepthWidth!=scene.Width || beforePost.DepthHeight!=scene.Height
        || beforePost.DepthFormat!=SDL_GPU_TEXTUREFORMAT_R32_FLOAT || beforePost.ActualSamples!=1)
        return s.fail("heat requires independent single-sample full-size R32 depth");
    if(s.HeatDone && s.HeatApplied.Same_Identity(s.HeatMain))return s.fail("heat already recorded for this scene");
    // All queued readers of this persistent instance buffer must submit before its
    // next upload. The ordinary frame staging path copies before subsequent draws.
    if(frame.Has_Pending_Work() && !frame.Flush())return s.fail("heat instance readers could not submit");
    uint32_t offset=0;const unsigned bytes=unsigned(s.HeatRecords.size()*sizeof(float));
    auto *upload=frame.Upload_Space(bytes,offset);if(!upload)return s.fail("heat instance staging failed");
    std::memcpy(upload,s.HeatRecords.data(),bytes);frame.Queue_Buffer_Upload(s.HeatBuffer,offset,bytes);
    const unsigned halfWidth=(scene.Width+1)/2,halfHeight=(scene.Height+1)/2;
    const SdlTarget field={s.HeatField,nullptr,halfWidth,halfHeight,0,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT};
    const SdlTarget copy={s.HeatCopy,nullptr,scene.Width,scene.Height,0,s.HeatFormat};
    SdlTarget destination=scene;destination.Depth=nullptr;destination.DepthFormat=0;
    auto draw=[](SDL_GPUGraphicsPipeline *pipeline,const SdlTarget &target,unsigned count) {
        SdlRecordedDraw result={};result.Pipeline=pipeline;result.Count=count;
        result.Viewport[2]=float(target.Width);result.Viewport[3]=float(target.Height);result.Viewport[5]=1;
        result.Scissor[2]=int(target.Width);result.Scissor[3]=int(target.Height);return result;
    };
    SdlRecordedDraw clear=draw(s.ClearPipeline,field,3),splats=draw(s.HeatPipeline,field,6);
    splats.VertexBuffer=s.HeatBuffer;splats.InstanceCount=s.HeatCount;
    splats.SamplerCount=s.HeatSamplers;
    splats.Textures[0]=splats.Textures[1]=beforePost.SampledDepth;splats.Samplers[0]=splats.Samplers[1]=s.Sampler;
    SdlRecordedDraw snapshot=draw(s.Copy.Pipelines[format_slot(s.HeatFormat)],copy,3);
    snapshot.SamplerCount=s.Copy.Samplers;snapshot.Textures[0]=snapshot.Textures[1]=scene.Colour;
    snapshot.Samplers[0]=snapshot.Samplers[1]=s.Sampler;
    SdlRecordedDraw composite=draw(s.HeatComposite.Pipelines[format_slot(s.HeatFormat)],destination,3);
    composite.SamplerCount=s.HeatComposite.Samplers;composite.Textures[0]=s.HeatCopy;composite.Textures[1]=s.HeatField;
    composite.Samplers[0]=composite.Samplers[1]=s.Sampler;
    PostProcessConstants constants={};constants.TexelSize[0]=1.0f/scene.Width;constants.TexelSize[1]=1.0f/scene.Height;
    constants.TexelSize[2]=float(scene.Width);constants.TexelSize[3]=float(scene.Height);
    SdlNativeUniformBlock uniform={1,0,&constants,sizeof(constants)};
    SdlNativeDrawPacket packets[]={{field,clear,nullptr,0},{field,splats,s.HeatUniforms?&uniform:nullptr,s.HeatUniforms?1u:0u},
        {copy,snapshot,s.Copy.Uniforms?&uniform:nullptr,s.Copy.Uniforms?1u:0u},
        {destination,composite,s.HeatComposite.Uniforms?&uniform:nullptr,s.HeatComposite.Uniforms?1u:0u}};
    if(s.fault(TestFailure::Record) || !frame.Record_Native_Draw_Batch(packets,4))return s.fail("atomic native heat packets refused");
    s.HeatApplied=s.HeatMain;s.HeatDone=true;s.LastError.clear();return true;
}
void SdlPostDistortion::Abandon()
{
    State->Ready = State->Done = false; State->Count = 0; State->Post = nullptr;
    State->Main = {}; State->Applied = {}; State->Output = {};
    std::memset(State->Warps, 0, sizeof(State->Warps));
    State->HeatReady=State->HeatDone=false;State->HeatCount=0;State->HeatPost=nullptr;
    State->HeatMain={};State->HeatApplied={};State->HeatRecords.clear();
}
bool SdlPostDistortion::Release(SdlGpuFrame &frame)
{
    Impl &s = *State;
    if (!s.Owner) return true;
    if (s.Owner != &frame || (frame.Has_Pending_Work() && !frame.Flush()))
        return s.fail("release requires owning frame and submitted/discarded readers");
    if(s.HeatBuffer)SDL_ReleaseGPUBuffer(s.Device,s.HeatBuffer);s.HeatBuffer=nullptr;
    for (auto *program : {&s.Copy, &s.Warp, &s.HeatComposite}) {
        for (auto *pipeline : program->Pipelines) if (pipeline) SDL_ReleaseGPUGraphicsPipeline(s.Device, pipeline);
        if (program->Shader) SDL_ReleaseGPUShader(s.Device, program->Shader);
        *program = {};
    }
    if(s.HeatPipeline)SDL_ReleaseGPUGraphicsPipeline(s.Device,s.HeatPipeline);
    if(s.ClearPipeline)SDL_ReleaseGPUGraphicsPipeline(s.Device,s.ClearPipeline);
    if (s.Vertex) SDL_ReleaseGPUShader(s.Device, s.Vertex);
    if (s.Sampler) SDL_ReleaseGPUSampler(s.Device, s.Sampler);
    if (s.Snapshot) SDL_ReleaseGPUTexture(s.Device, s.Snapshot);
    for(auto *shader:{s.HeatVertex,s.HeatPixel,s.ClearPixel})if(shader)SDL_ReleaseGPUShader(s.Device,shader);
    if(s.HeatField)SDL_ReleaseGPUTexture(s.Device,s.HeatField);
    if(s.HeatCopy)SDL_ReleaseGPUTexture(s.Device,s.HeatCopy);
    s.HeatPipeline=s.ClearPipeline=nullptr;s.HeatVertex=s.HeatPixel=s.ClearPixel=nullptr;
    s.HeatField=s.HeatCopy=nullptr;s.HeatWidth=s.HeatHeight=s.HeatFormat=0;
    s.Vertex = nullptr; s.Sampler = nullptr; s.Snapshot = nullptr;
    s.Owner = nullptr; s.Device = nullptr; s.Width = s.Height = s.Format = 0;
    Abandon(); return true;
}
const std::string &SdlPostDistortion::Error() const { return State->LastError; }
SdlPostDistortionResources SdlPostDistortion::Resources() const
{
    const Impl &s = *State; SdlPostDistortionResources result;
    result.Textures = s.Snapshot != nullptr; result.Shaders = s.Vertex != nullptr;
    result.Samplers = s.Sampler != nullptr; result.Creations = s.Creations;
    for (const auto *program : {&s.Copy, &s.Warp, &s.HeatComposite}) {
        result.Shaders += program->Shader != nullptr;
        for (auto *pipeline : program->Pipelines) result.Pipelines += pipeline != nullptr;
    }
    result.Textures+=(s.HeatField!=nullptr)+(s.HeatCopy!=nullptr);
    result.Shaders+=(s.HeatVertex!=nullptr)+(s.HeatPixel!=nullptr)+(s.ClearPixel!=nullptr);
    result.Pipelines+=(s.HeatPipeline!=nullptr)+(s.ClearPipeline!=nullptr);
    result.Buffers=s.HeatBuffer ? 1u : 0u;
    result.BufferBytes=result.Buffers ? DIRECT3D11_HEAT_SPLAT_LIMIT*DIRECT3D11_HEAT_SPLAT_FLOATS*sizeof(float) : 0;
    return result;
}

void SdlPostDistortion::Set_Failure_For_Test(TestFailure failure) { State->Failure=failure; }
