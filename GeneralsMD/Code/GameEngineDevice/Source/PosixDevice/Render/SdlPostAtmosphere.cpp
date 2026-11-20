#include "SdlPostAtmosphere.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
unsigned format_slot(unsigned f)
{
    if(f==SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)return 0;
    if(f==SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)return 1;
    return f==SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT?2:3;
}
SdlSceneCaptureRequest normalized(SdlGpuFrame &frame,SdlSceneCaptureRequest identity)
{
    identity.Source.ColourFormat=SdlGpuFrame::Target_Colour_Format(identity.Source);
    identity.Source.DepthFormat=frame.Target_Depth_Format(identity.Source);
    return identity;
}
bool same_scene(const SdlSceneCaptureRequest &a,const SdlSceneCaptureRequest &b)
{
    auto left=a,right=b;left.Phase=right.Phase=SdlSceneCapturePhase::OpaqueScene;return left.Same_Identity(right);
}
bool same_view(const SdlPostBorrowedView &a,const SdlPostBorrowedView &b)
{return a.Texture==b.Texture && a.Identity==b.Identity && a.Generation==b.Generation;}
bool valid_view(const SdlPostBorrowedView &view)
{return view.Texture ? view.Identity && view.Generation : !view.Identity && !view.Generation;}
bool finite(const float *values,unsigned count)
{for(unsigned i=0;i<count;++i)if(!std::isfinite(values[i]))return false;return true;}
}

struct SdlPostAtmosphere::Impl
{
    enum Pass {Sky,Haze,Fade,Count};
    struct Program {SDL_GPUShader *Shader=nullptr;SDL_GPUGraphicsPipeline *Pipelines[3]={};unsigned Samplers=0,Uniforms=0;} Programs[Count];
    SdlGpuFrame *Owner=nullptr;
    const SdlPostProcess *Post=nullptr;
    SDL_GPUDevice *Device=nullptr;
    SDL_GPUShader *Vertex=nullptr;
    SDL_GPUSampler *Clamp=nullptr,*Wrap=nullptr;
    SDL_GPUTexture *White=nullptr,*Black=nullptr;
    SdlPostGroundHazeSettings Ground;
    SdlPostAtmosphereSettings Atmosphere;
    SdlPostAtmosphereViews Views;
    SdlSceneCaptureRequest Main;
    float WorldFromClip[16]={};
    bool Ready=false,Done[Count]={};
    std::string LastError;
    unsigned long long Creations=0;
    TestFailure Failure=TestFailure::None;

    bool fault(TestFailure point) const { return Failure==point; }
    bool fail(const std::string &why)
    {
        const std::string text="native SDL atmosphere: "+why;
        if(text!=LastError)std::fprintf(stderr,"%s\n",text.c_str());
        LastError=text;return false;
    }
    bool current(SdlGpuFrame &frame,const SdlTarget &scene)
    {
        return Ready && Owner==&frame && Post && Post->Scene_Matches_Identity(frame,scene,Main);
    }
    bool captured(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &in)
    {
        if(!scene.Depth || !current(frame,scene) || in.Capture.Phase!=SdlSceneCapturePhase::BeforeTransparent || !in.Capture.Depth
            || !same_scene(Main,normalized(frame,in.Capture)))return fail("BeforeTransparent capture does not match its unresolved main scene");
        if(!in.SampledDepth || in.SampledDepth==scene.Colour || in.SampledDepth==scene.Depth
            || in.DepthWidth!=scene.Width || in.DepthHeight!=scene.Height || in.DepthFormat!=SDL_GPU_TEXTUREFORMAT_R32_FLOAT
            || in.ActualSamples!=1 || in.Shroud!=Views.Shroud.Texture
            || std::memcmp(in.WorldFromClip,WorldFromClip,sizeof(WorldFromClip))
            || std::memcmp(in.ShroudRect,Views.ShroudRect,sizeof(Views.ShroudRect)))
            return fail("requires independent matching R32 depth and the copied camera/shroud mapping");
        return true;
    }
    bool program(Pass pass,unsigned format)
    {
        if(fault(TestFailure::Program))return fail("injected program preparation failure");
        std::string source,error;std::vector<unsigned char> spirv;
        if(!Vertex) {
            if(!Sdl_Post_Vertex_Source(source) || !SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,error)
                || !(Vertex=SDL3_Create_Shader(Device,spirv,true,error)))return fail("vertex program: "+error);
            ++Creations;
        }
        if(!Clamp || !Wrap) {
            SDL_GPUSamplerCreateInfo info={};info.min_filter=info.mag_filter=SDL_GPU_FILTER_LINEAR;
            info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;info.max_lod=1000;
            info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            if(!Clamp){Clamp=SDL_CreateGPUSampler(Device,&info);if(!Clamp)return fail(SDL_GetError());++Creations;}
            info.address_mode_u=info.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            if(!Wrap){Wrap=SDL_CreateGPUSampler(Device,&info);if(!Wrap)return fail(SDL_GetError());++Creations;}
        }
        Program &p=Programs[pass];
        if(!p.Shader) {
            const PostShaderProgram ids[]={POST_SHADER_SKY,POST_SHADER_HAZE,POST_SHADER_ATMOSPHERE};
            if(!Sdl_Post_Pixel_Source(ids[pass],source) || !SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error)
                || !(p.Shader=SDL3_Create_Shader(Device,spirv,false,error)))return fail("pixel program: "+error);
            SDL3_Shader_Slots(spirv,false,p.Samplers,p.Uniforms);++Creations;
            if(p.Samplers>4 || p.Uniforms>2)return fail("program exceeds explicit sampler/constant bindings");
        }
        const unsigned slot=format_slot(format);
        if(slot>2)return fail("unsupported scene colour format");
        if(fault(TestFailure::Pipeline))return fail("injected pipeline preparation failure");
        if(!p.Pipelines[slot]) {
            SDL_GPUColorTargetDescription target={};target.format=SDL_GPUTextureFormat(format);
            target.blend_state.enable_color_write_mask=true;
            target.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
            if(pass!=Sky) {
                target.blend_state.enable_blend=true;target.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                target.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                target.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;
                target.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
                target.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
                target.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=Vertex;info.fragment_shader=p.Shader;
            info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;
            info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
            info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
            p.Pipelines[slot]=SDL_CreateGPUGraphicsPipeline(Device,&info);
            if(!p.Pipelines[slot])return fail(std::string("pipeline: ")+SDL_GetError());
            ++Creations;
        }
        return true;
    }
    bool fallback(SDL_GPUTexture *&texture,bool white)
    {
        if(texture)return true;
        if(fault(TestFailure::Target) || (fault(TestFailure::TargetAfterOne) && White))return fail("injected fallback texture allocation failure");
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=info.height=info.layer_count_or_depth=info.num_levels=1;
        texture=SDL_CreateGPUTexture(Device,&info);if(!texture)return fail(std::string("fallback texture: ")+SDL_GetError());++Creations;
        uint32_t offset=0;uint8_t *bytes=Owner->Upload_Space(4,offset);
        if(!bytes)return fail("fallback upload allocation failed");
        std::memset(bytes,white?255:0,4);Owner->Queue_Texture_Upload(texture,0,1,1,offset);return true;
    }
    bool draw(Pass id,const SdlTarget &scene,SDL_GPUTexture *depth)
    {
        const Program &p=Programs[id];SdlRecordedDraw draw={};draw.Pipeline=p.Pipelines[format_slot(SdlGpuFrame::Target_Colour_Format(scene))];
        draw.Count=3;draw.Viewport[2]=float(scene.Width);draw.Viewport[3]=float(scene.Height);draw.Viewport[5]=1;
        draw.Scissor[2]=int(scene.Width);draw.Scissor[3]=int(scene.Height);draw.SamplerCount=p.Samplers;
        draw.Textures[0]=Black;draw.Textures[1]=depth?depth:Black;
        draw.Textures[2]=Views.Shroud.Texture?Views.Shroud.Texture:White;
        draw.Textures[3]=Views.Sky.Texture?Views.Sky.Texture:Black;
        for(unsigned i=0;i<p.Samplers;++i) {
            if(!draw.Textures[i] || draw.Textures[i]==scene.Colour || draw.Textures[i]==scene.Depth)return fail("pass texture aliases its scene attachment");
            draw.Samplers[i]=id==Sky && i==3?Wrap:Clamp;
        }
        PostProcessConstants constants={};constants.TexelSize[0]=1.0f/scene.Width;constants.TexelSize[1]=1.0f/scene.Height;
        constants.TexelSize[2]=float(scene.Width);constants.TexelSize[3]=float(scene.Height);
        std::memcpy(constants.WorldFromClip,WorldFromClip,sizeof(WorldFromClip));
        std::memcpy(constants.HazeShroudRect,Views.ShroudRect,sizeof(Views.ShroudRect));
        constants.HazeSun[2]=Views.Shroud.Texture?1:0;
        if(id==Haze) {
            constants.Haze[0]=Ground.Top;constants.Haze[1]=Ground.Thickness;constants.Haze[2]=Ground.Strength;constants.Haze[3]=Ground.Reach;
            std::memcpy(constants.HazeColour,Ground.Colour,sizeof(Ground.Colour));constants.HazeColour[3]=Ground.WaterHeight;
            constants.HazeSun[0]=Ground.TowardSun[0];constants.HazeSun[1]=Ground.TowardSun[1];constants.HazeSun[3]=Ground.Warmth;
        } else {
            constants.Haze[0]=Atmosphere.Start;constants.Haze[1]=Atmosphere.Scale;constants.Haze[2]=Atmosphere.Strength;constants.Haze[3]=Atmosphere.Ground;
            std::memcpy(constants.HazeColour,Atmosphere.Horizon,sizeof(Atmosphere.Horizon));
            std::memcpy(constants.BlurDirection,Atmosphere.Tint,sizeof(Atmosphere.Tint));
            std::memcpy(constants.WorldAoProjection,Atmosphere.MapSize,sizeof(Atmosphere.MapSize));
        }
        const SdlNativeUniformBlock blocks[]={{1,0,&constants,sizeof(constants)},{1,1,&constants,sizeof(constants)}};
        const SdlTarget destination={scene.Colour,nullptr,scene.Width,scene.Height,0,SdlGpuFrame::Target_Colour_Format(scene)};
        return Owner->Record_Native_Draw(destination,draw,blocks,p.Uniforms) || fail("native draw refused");
    }
};

SdlPostAtmosphere::SdlPostAtmosphere():State(new Impl){}
SdlPostAtmosphere::~SdlPostAtmosphere()
{
    if(State->Owner && !Release(*State->Owner))std::fprintf(stderr,"native SDL atmosphere: queued resources retained until device teardown\n");
    delete State;
}
bool SdlPostAtmosphere::Prepare(SdlGpuFrame &frame,const SdlPostProcess &post,const SdlTarget &scene,
    const SdlSceneCaptureRequest &main,const float worldFromClip[16],const SdlPostGroundHazeSettings &ground,
    const SdlPostAtmosphereSettings &atmosphere,const SdlPostAtmosphereViews &views)
{
    Impl &s=*State;
    const auto key=normalized(frame,main);
    const bool same=s.Owner==&frame && s.Post==&post && same_scene(s.Main,key)
        && s.Views.MapIdentity==views.MapIdentity && s.Views.MapGeneration==views.MapGeneration
        && same_view(s.Views.Sky,views.Sky) && same_view(s.Views.Shroud,views.Shroud)
        && worldFromClip && !std::memcmp(s.WorldFromClip,worldFromClip,sizeof(s.WorldFromClip));
    s.Ready=false;
    if(s.Owner && s.Owner!=&frame)return s.fail("Release old frame owner before rebinding");
    if(!std::isfinite(ground.Strength) || !std::isfinite(atmosphere.Strength))return s.fail("non-finite strength");
    if(ground.Strength<=0 && atmosphere.Strength<=0) {Abandon();s.LastError.clear();return true;}
    if(!post.Scene_Matches_Identity(frame,scene,key) || main.Phase!=SdlSceneCapturePhase::OpaqueScene)return s.fail("requires explicit unresolved main post scene identity");
    if(!worldFromClip || !finite(worldFromClip,16) || !finite(ground.Colour,3) || !finite(ground.TowardSun,3)
        || !std::isfinite(ground.Top) || !std::isfinite(ground.Thickness) || !std::isfinite(ground.Reach)
        || !std::isfinite(ground.WaterHeight) || !std::isfinite(ground.Warmth)
        || !finite(atmosphere.Horizon,3) || !finite(atmosphere.Tint,3) || !finite(atmosphere.MapSize,2)
        || !std::isfinite(atmosphere.Start) || !std::isfinite(atmosphere.Scale) || !std::isfinite(atmosphere.Ground)
        || (atmosphere.Strength>0 && (atmosphere.Scale<=0 || atmosphere.MapSize[0]<=0 || atmosphere.MapSize[1]<=0)))
        return s.fail("invalid copied atmosphere/haze/camera settings");
    if(!views.MapIdentity || !views.MapGeneration || !valid_view(views.Sky) || !valid_view(views.Shroud) || !finite(views.ShroudRect,4)
        || (views.Sky.Texture && (views.Sky.Texture==scene.Colour || views.Sky.Texture==scene.Depth))
        || (views.Shroud.Texture && (views.Shroud.Texture==scene.Colour || views.Shroud.Texture==scene.Depth)))
        return s.fail("requires explicit nonaliasing borrowed view identities/generations and map epoch");
    s.Owner=&frame;s.Device=frame.Device();
    const unsigned format=SdlGpuFrame::Target_Colour_Format(scene);
    if(format_slot(format)>2)return s.fail("unsupported atmosphere scene format");
    if((ground.Strength>0 && !s.program(Impl::Haze,format)) || (atmosphere.Strength>0
        && (!s.program(Impl::Sky,format) || !s.program(Impl::Fade,format))) || !s.fallback(s.White,true) || !s.fallback(s.Black,false))return false;
    if(!same)std::memset(s.Done,0,sizeof(s.Done));
    if(ground.Strength<=0)s.Done[Impl::Haze]=false;
    if(atmosphere.Strength<=0)s.Done[Impl::Sky]=s.Done[Impl::Fade]=false;
    s.Main=key;s.Post=&post;s.Ground=ground;s.Atmosphere=atmosphere;s.Views=views;
    std::memcpy(s.WorldFromClip,worldFromClip,sizeof(s.WorldFromClip));s.Ready=true;s.LastError.clear();return true;
}
bool SdlPostAtmosphere::Draw_Sky(SdlGpuFrame &frame,const SdlTarget &scene)
{
    Impl &s=*State;if(!s.Ready || s.Atmosphere.Strength<=0)return false;
    if(!s.current(frame,scene))return s.fail("sky requested outside its unresolved main scene");
    if(s.Done[Impl::Sky])return false;
    if(!s.draw(Impl::Sky,scene,nullptr))return false;
    s.Done[Impl::Sky]=true;return true;
}
bool SdlPostAtmosphere::Apply_Ground_Haze(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &inputs)
{
    Impl &s=*State;if(!s.Ready || s.Ground.Strength<=0)return false;
    if(!s.captured(frame,scene,inputs) || s.Done[Impl::Haze])return false;
    if(!s.draw(Impl::Haze,scene,inputs.SampledDepth))return false;
    s.Done[Impl::Haze]=true;return true;
}
bool SdlPostAtmosphere::Apply_Atmosphere(SdlGpuFrame &frame,const SdlTarget &scene,const SdlPostSceneInputs &inputs)
{
    Impl &s=*State;if(!s.Ready || s.Atmosphere.Strength<=0)return false;
    if(!s.captured(frame,scene,inputs) || s.Done[Impl::Fade])return false;
    if(!s.draw(Impl::Fade,scene,inputs.SampledDepth))return false;
    s.Done[Impl::Fade]=true;return true;
}
void SdlPostAtmosphere::Abandon()
{
    State->Ready=false;State->Post=nullptr;State->Main={};State->Views={};std::memset(State->Done,0,sizeof(State->Done));
}
bool SdlPostAtmosphere::Release(SdlGpuFrame &frame)
{
    Impl &s=*State;if(!s.Owner)return true;
    if(s.Owner!=&frame || (frame.Has_Pending_Work() && !frame.Flush()))return s.fail("release requires owner and submitted batch");
    for(auto &p:s.Programs){for(auto *pipeline:p.Pipelines)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(s.Device,pipeline);if(p.Shader)SDL_ReleaseGPUShader(s.Device,p.Shader);p={};}
    if(s.Vertex)SDL_ReleaseGPUShader(s.Device,s.Vertex);
    if(s.Clamp)SDL_ReleaseGPUSampler(s.Device,s.Clamp);
    if(s.Wrap)SDL_ReleaseGPUSampler(s.Device,s.Wrap);
    if(s.White)SDL_ReleaseGPUTexture(s.Device,s.White);
    if(s.Black)SDL_ReleaseGPUTexture(s.Device,s.Black);
    s.Vertex=nullptr;s.Clamp=s.Wrap=nullptr;s.White=s.Black=nullptr;s.Owner=nullptr;s.Device=nullptr;Abandon();return true;
}
const std::string &SdlPostAtmosphere::Error()const{return State->LastError;}
SdlPostAtmosphereResources SdlPostAtmosphere::Resources()const
{
    const Impl &s=*State;SdlPostAtmosphereResources result;
    result.Textures=(s.White!=nullptr)+(s.Black!=nullptr);result.Shaders=s.Vertex!=nullptr;result.Samplers=(s.Clamp!=nullptr)+(s.Wrap!=nullptr);
    for(const auto &p:s.Programs){result.Shaders+=p.Shader!=nullptr;for(auto *pipeline:p.Pipelines)result.Pipelines+=pipeline!=nullptr;}
    result.Creations=s.Creations;return result;
}
void SdlPostAtmosphere::Set_Failure_For_Test(TestFailure failure) { State->Failure=failure; }
