// Classic foam uses the shared Windows formula, the extended engine constants,
// and an explicit opaque-scene depth copy. Engine routing/capability is separate.
#include "engineshader.h"
#include "sdl3shadercompile.h"
#include "sdl3target.h"
#include "SdlConstants.h"
#include <spirv_cross_c.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL line %u: %s\n",__LINE__,#x);}}while(0)
static bool require(bool ok,const std::string &error)
{
    if(!ok)std::printf("ERROR: %s\n",error.c_str());
    CHECK(ok);return ok;
}
struct Member {const char *Name;size_t Offset,Size;};
#define MEMBER(name) {#name,offsetof(SdlEnginePixelConstants,name),sizeof(((SdlEnginePixelConstants *)nullptr)->name)}
static const Member members[]={
    {"TextureFactor",0,16},{"FogColour",16,16},{"AlphaReference",32,16},
    MEMBER(NormalLightDirection),MEMBER(NormalLightDiffuse),MEMBER(NormalMapParameters),MEMBER(TerrainSunDirection),
    MEMBER(ShadowFromClip),MEMBER(ShadowParameters),MEMBER(ShadowViewport),MEMBER(ShadowSoftness),MEMBER(Sky),MEMBER(SkyUp),
    MEMBER(VolumeParameters),MEMBER(WorldFromClip),MEMBER(HeadlightParameters),MEMBER(HeadlightPosition),MEMBER(HeadlightDirection),
    MEMBER(BlastLightParameters),MEMBER(BlastLightPosition),MEMBER(BlastLightDiffuse),MEMBER(BlastLightAmbient),MEMBER(SoftParticleDepth)
};
#undef MEMBER
static void reflect(const std::vector<unsigned char> &spirv)
{
    struct Owner {spvc_context Context=nullptr;~Owner(){if(Context)spvc_context_destroy(Context);}} owner;
    CHECK(spvc_context_create(&owner.Context)==SPVC_SUCCESS);if(!owner.Context)return;
    std::vector<SpvId> words(spirv.size()/sizeof(SpvId));std::memcpy(words.data(),spirv.data(),spirv.size());
    spvc_parsed_ir ir=nullptr;spvc_compiler compiler=nullptr;spvc_resources resources=nullptr;
    bool ok=spvc_context_parse_spirv(owner.Context,words.data(),words.size(),&ir)==SPVC_SUCCESS
        && spvc_context_create_compiler(owner.Context,SPVC_BACKEND_NONE,ir,SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,&compiler)==SPVC_SUCCESS
        && spvc_compiler_create_shader_resources(compiler,&resources)==SPVC_SUCCESS;
    CHECK(ok);if(!ok)return;
    const spvc_reflected_resource *blocks=nullptr;size_t count=0;
    CHECK(spvc_resources_get_resource_list_for_type(resources,SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,&blocks,&count)==SPVC_SUCCESS);
    CHECK(count==1);if(count!=1)return;
    const auto &block=blocks[0];CHECK(std::strcmp(block.name,"CombinerConstants")==0);
    CHECK(spvc_compiler_get_decoration(compiler,block.id,SpvDecorationDescriptorSet)==3);
    CHECK(spvc_compiler_get_decoration(compiler,block.id,SpvDecorationBinding)==0);
    const auto type=spvc_compiler_get_type_handle(compiler,block.base_type_id);size_t size=0;
    CHECK(spvc_compiler_get_declared_struct_size(compiler,type,&size)==SPVC_SUCCESS);
    CHECK(size==2528 && size==offsetof(SdlEnginePixelConstants,DrawEffectParameters));
    CHECK(spvc_type_get_num_member_types(type)==sizeof(members)/sizeof(members[0]));
    if(spvc_type_get_num_member_types(type)!=sizeof(members)/sizeof(members[0]))return;
    for(unsigned i=0;i<sizeof(members)/sizeof(members[0]);++i) {
        unsigned offset=0;size_t bytes=0;
        CHECK(std::strcmp(spvc_compiler_get_member_name(compiler,block.base_type_id,i),members[i].Name)==0);
        CHECK(spvc_compiler_type_struct_member_offset(compiler,type,i,&offset)==SPVC_SUCCESS);
        CHECK(spvc_compiler_get_declared_struct_member_size(compiler,type,i,&bytes)==SPVC_SUCCESS);
        CHECK(offset==members[i].Offset && bytes==members[i].Size);
        if(bytes==64 && std::strstr(members[i].Name,"FromClip")) {
            CHECK(spvc_compiler_has_member_decoration(compiler,block.base_type_id,i,SpvDecorationColMajor));
            CHECK(spvc_compiler_get_member_decoration(compiler,block.base_type_id,i,SpvDecorationMatrixStride)==16);
        }
    }
    // The HLSL row-vector matrix is transposed in SPIR-V: ColMajor here is expected,
    // with identical CPU bytes (the same convention used by the other SDL shaders).
    const spvc_reflected_resource *images=nullptr;size_t imageCount=0;
    CHECK(spvc_resources_get_resource_list_for_type(resources,SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,&images,&imageCount)==SPVC_SUCCESS);
    unsigned imageMask=0;
    for(size_t i=0;i<imageCount;++i) {
        CHECK(spvc_compiler_get_decoration(compiler,images[i].id,SpvDecorationDescriptorSet)==2);
        const unsigned slot=spvc_compiler_get_decoration(compiler,images[i].id,SpvDecorationBinding);
        CHECK(slot<8);if(slot<8)imageMask|=1u<<slot;
    }
    CHECK(imageMask==0xef); // t0..3, t5 (sun), t6 (smoke), t7 (opaque depth); no t4.
    const spvc_reflected_resource *samplers=nullptr;size_t samplerCount=0;
    CHECK(spvc_resources_get_resource_list_for_type(resources,SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS,&samplers,&samplerCount)==SPVC_SUCCESS);
    unsigned samplerMask=0;
    for(size_t i=0;i<samplerCount;++i) {
        CHECK(spvc_compiler_get_decoration(compiler,samplers[i].id,SpvDecorationDescriptorSet)==2);
        const unsigned slot=spvc_compiler_get_decoration(compiler,samplers[i].id,SpvDecorationBinding);
        CHECK(slot<8);if(slot<8)samplerMask|=1u<<slot;
    }
    CHECK(samplerMask==0x6f); // Load-only t7 has no sampler in SPIR-V.
}
static void cpu()
{
    for(int p=ENGINE_SHADER_NONE;p<=ENGINE_SHADER_WATER_RIVER_FOAM;++p) {
        const auto program=EngineShaderProgram(p);
        const bool foam=p==ENGINE_SHADER_WATER_TRAPEZOID_FOAM || p==ENGINE_SHADER_WATER_RIVER_FOAM;
        const bool ground=p>=ENGINE_SHADER_TERRAIN && p<=ENGINE_SHADER_ROAD_NOISE_2;
        CHECK(EngineShader_Reads_Scene_Depth(program)==foam);
        CHECK(EngineShader_Uses_Extended_Pixel_Constants(program)==(foam || ground));
    }
    CHECK(!EngineShader_Uses_Extended_Pixel_Constants(EngineShaderProgram(999)));
    CHECK(offsetof(SdlEnginePixelConstants,ShadowViewport)==288);
    CHECK(offsetof(SdlEnginePixelConstants,WorldFromClip)==368);
    CHECK(sizeof(SdlEnginePixelConstants)==2544);
    for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID_FOAM,ENGINE_SHADER_WATER_RIVER_FOAM})for(unsigned state=0;state<5;++state) {
        PixelPipelineDescription pipeline{};pipeline.AlphaTestEnabled=state>=1;pipeline.AlphaFunction=state==4?FF_CMP_LESS:FF_CMP_GREATER;
        pipeline.FogEnabled=state>=2;pipeline.AlphaToCoverage=state==3;
        std::string windows,native,log,msl;std::vector<unsigned char> spirv;
        CHECK(EngineShader_Pixel_Program(program,pipeline,windows,COMBINER_SHADER_TARGET_D3D11));
        CHECK(EngineShader_Pixel_Program(program,pipeline,native,COMBINER_SHADER_TARGET_SDL3_GPU));
        CHECK(SDL3_Shader_Retarget(windows,false));CHECK(windows==native);
        CHECK(native.find("SceneDepth.Load(int3(position.xy, 0))")!=std::string::npos);
        CHECK(native.find("foam_amount(input.TexCoord2 * 16.0, water_depth, TextureFactor.g)")!=std::string::npos);
        if(!require(SDL3_Compile_HLSL_To_SPIRV(native,false,spirv,log),log))continue;
        unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);CHECK(samplers==8 && uniforms==1);
        require(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log),log);CHECK(msl.find("main0")!=std::string::npos);
        if(state==0)reflect(spirv);
    }
    PixelPipelineDescription plain{};std::string source;
    for(auto program:{ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID,ENGINE_SHADER_WATER_CLASSIC_RIVER}) {
        CHECK(EngineShader_Pixel_Program(program,plain,source,COMBINER_SHADER_TARGET_SDL3_GPU));
        CHECK(source.find("foam_amount")==std::string::npos && source.find("WorldFromClip")==std::string::npos);
    }
}
// The reference fragment fixes the physical depth only. All foam/noise/derivative
// math remains the upstream body, independently of depth texture/matrix bindings.
// Separate probes below assert edge alpha, vertex brightness and shroud behavior.
struct FoamGpu
{
    SDL_GPUDevice *Device=nullptr;
    SDL_GPUShader *Vertex=nullptr;
    SDL_GPUSampler *Sampler=nullptr;
    std::vector<SDL_GPUTexture *> Textures;
    std::vector<SDL_GPUShader *> Shaders;
    std::vector<SDL_GPUGraphicsPipeline *> Pipelines;
    unsigned W=83,H=67;
    ~FoamGpu()
    {
        if(!Device)return;
        SDL_WaitForGPUIdle(Device);
        for(auto *p:Pipelines)SDL_ReleaseGPUGraphicsPipeline(Device,p);
        for(auto *p:Shaders)SDL_ReleaseGPUShader(Device,p);
        for(auto *p:Textures)SDL_ReleaseGPUTexture(Device,p);
        if(Vertex)SDL_ReleaseGPUShader(Device,Vertex);
        if(Sampler)SDL_ReleaseGPUSampler(Device,Sampler);
        SDL_DestroyGPUDevice(Device);
    }
    bool submit(SDL_GPUCommandBuffer *commands)
    {
        auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
        if(!fence)return false;
        bool ok=SDL_WaitForGPUFences(Device,true,&fence,1);CHECK(ok);SDL_ReleaseGPUFence(Device,fence);return ok;
    }
    SDL_GPUTexture *texture(SDL_GPUTextureFormat format,unsigned w,unsigned h,const void *pixels,unsigned bytes,bool target=false)
    {
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;
        info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|(target?SDL_GPU_TEXTUREUSAGE_COLOR_TARGET:0);
        info.width=w;info.height=h;info.layer_count_or_depth=1;info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
        auto *t=SDL_CreateGPUTexture(Device,&info);CHECK(t);if(!t)return nullptr;Textures.push_back(t);
        if(!pixels)return t;
        SDL_GPUTransferBufferCreateInfo transferInfo={};transferInfo.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;transferInfo.size=bytes;
        auto *transfer=SDL_CreateGPUTransferBuffer(Device,&transferInfo);CHECK(transfer);if(!transfer)return nullptr;
        auto *mapped=SDL_MapGPUTransferBuffer(Device,transfer,false);CHECK(mapped);
        if(!mapped){SDL_ReleaseGPUTransferBuffer(Device,transfer);return nullptr;}
        std::memcpy(mapped,pixels,bytes);SDL_UnmapGPUTransferBuffer(Device,transfer);
        auto *commands=SDL_AcquireGPUCommandBuffer(Device);CHECK(commands);
        if(!commands){SDL_ReleaseGPUTransferBuffer(Device,transfer);return nullptr;}
        auto *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;
        SDL_GPUTextureRegion destination={};destination.texture=t;destination.w=w;destination.h=h;destination.d=1;
        SDL_UploadToGPUTexture(copy,&source,&destination,false);SDL_EndGPUCopyPass(copy);submit(commands);
        SDL_ReleaseGPUTransferBuffer(Device,transfer);return t;
    }
    SDL_GPUGraphicsPipeline *pipeline(EngineShaderProgram program,const char *constantDepth=nullptr,bool diagnostic=false)
    {
        std::string source,log;std::vector<unsigned char> spirv;PixelPipelineDescription plain{};
        if(!require(EngineShader_Pixel_Program(program,plain,source,COMBINER_SHADER_TARGET_SDL3_GPU),log))return nullptr;
        if(constantDepth) {
            const std::string from="float water_depth = foam_water_depth(input.Position);";
            auto at=source.find(from);CHECK(at!=std::string::npos);if(at==std::string::npos)return nullptr;
            source.replace(at,from.size(),std::string("float water_depth = ")+constantDepth+";");
        }
        if(diagnostic) {
            const std::string from="    return current;";auto at=source.rfind(from);CHECK(at!=std::string::npos);
            if(at==std::string::npos)return nullptr;
            source.replace(at,from.size(),"    return float4(saturate(water_depth / 4.0), 0.0, 0.0, 1.0);");
        }
        if(!require(SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,log),log))return nullptr;
        auto *pixel=SDL3_Create_Shader(Device,spirv,false,log);if(!require(pixel!=nullptr,log))return nullptr;Shaders.push_back(pixel);
        SDL_GPUColorTargetDescription target={};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=Vertex;info.fragment_shader=pixel;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;
        info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&target;
        auto *p=SDL_CreateGPUGraphicsPipeline(Device,&info);CHECK(p);if(p)Pipelines.push_back(p);return p;
    }
    bool init()
    {
        Device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,true,"vulkan");
        if(!Device)std::printf("GPU init: %s\n",SDL_GetError());
        CHECK(Device);if(!Device)return false;
        std::printf("GPU backend: %s\n",SDL_GetGPUDeviceDriver(Device));
        std::string source=R"HLSL(
cbuffer Fixture : register(b0, space1) {float4 Diffuse; float4 Parameters;};
struct Output {
    float4 Position : SV_Position;
    [[vk::location(0)]] float4 Diffuse : COLOR0;
    [[vk::location(1)]] float4 Specular : COLOR1;
    [[vk::location(2)]] float2 TexCoord0 : TEXCOORD0;
    [[vk::location(3)]] float2 TexCoord1 : TEXCOORD1;
    [[vk::location(4)]] float2 TexCoord2 : TEXCOORD2;
    [[vk::location(5)]] float2 TexCoord3 : TEXCOORD3;
    [[vk::location(10)]] float Fog : FOG;
};
Output main(uint id : SV_VertexID) {
    float2 uv=float2((id << 1) & 2, id & 2);
    Output o;o.Position=float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, Parameters.x, 1.0);
    o.Diffuse=Diffuse;o.Specular=0;o.TexCoord0=uv;o.TexCoord1=uv;
    o.TexCoord2=uv*8.0+float2(0.37,0.19);o.TexCoord3=uv;o.Fog=1;return o;
})HLSL";
        std::string log;std::vector<unsigned char> spirv;
        if(!require(SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,log),log))return false;
        Vertex=SDL3_Create_Shader(Device,spirv,true,log);if(!require(Vertex!=nullptr,log))return false;
        SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        Sampler=SDL_CreateGPUSampler(Device,&sampler);CHECK(Sampler);return Sampler!=nullptr;
    }
    std::vector<unsigned char> draw(SDL_GPUGraphicsPipeline *p,SDL_GPUTexture *target,const SDL_GPUViewport &viewport,
        const SdlEnginePixelConstants &constants,const std::array<SDL_GPUTexture *,8> &textures,float brightness=1,float vertexAlpha=1)
    {
        if(!p)return {};
        auto *commands=SDL_AcquireGPUCommandBuffer(Device);CHECK(commands);if(!commands)return {};
        SDL_GPUColorTargetInfo colour={};colour.texture=target;colour.load_op=SDL_GPU_LOADOP_CLEAR;colour.store_op=SDL_GPU_STOREOP_STORE;
        colour.clear_color={4/255.0f,6/255.0f,9/255.0f,11/255.0f};
        auto *pass=SDL_BeginGPURenderPass(commands,&colour,1,nullptr);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);return {};}
        SDL_BindGPUGraphicsPipeline(pass,p);SDL_SetGPUViewport(pass,&viewport);
        std::array<SDL_GPUTextureSamplerBinding,8> bindings={};
        for(unsigned i=0;i<8;++i)bindings[i]={textures[i],Sampler};
        SDL_BindGPUFragmentSamplers(pass,0,bindings.data(),8);
        const float vertex[8]={brightness,brightness,brightness,vertexAlpha,.6f,0,0,0};
        SDL_PushGPUVertexUniformData(commands,0,vertex,sizeof(vertex));SDL_PushGPUFragmentUniformData(commands,0,&constants,sizeof(constants));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);if(!submit(commands))return {};
        SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=W*H*4;
        auto *transfer=SDL_CreateGPUTransferBuffer(Device,&info);CHECK(transfer);if(!transfer)return {};
        commands=SDL_AcquireGPUCommandBuffer(Device);CHECK(commands);if(!commands){SDL_ReleaseGPUTransferBuffer(Device,transfer);return {};}
        auto *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureRegion from={};from.texture=target;from.w=W;from.h=H;from.d=1;
        SDL_GPUTextureTransferInfo to={};to.transfer_buffer=transfer;SDL_DownloadFromGPUTexture(copy,&from,&to);SDL_EndGPUCopyPass(copy);
        std::vector<unsigned char> result;
        if(submit(commands)) {auto *mapped=static_cast<unsigned char *>(SDL_MapGPUTransferBuffer(Device,transfer,false));CHECK(mapped);
            if(mapped){result.assign(mapped,mapped+W*H*4);SDL_UnmapGPUTransferBuffer(Device,transfer);}}
        SDL_ReleaseGPUTransferBuffer(Device,transfer);return result;
    }
};
static bool inside(unsigned x,unsigned y,const SDL_GPUViewport &view)
{
    return x>=view.x && x<view.x+view.w && y>=view.y && y<view.y+view.h;
}
static void same_image(const std::vector<unsigned char> &a,const std::vector<unsigned char> &b,unsigned tolerance=1)
{
    CHECK(a.size()==b.size());if(a.size()!=b.size())return;
    unsigned maximum=0;for(size_t i=0;i<a.size();++i)maximum=std::max(maximum,unsigned(std::abs(int(a[i])-int(b[i]))));
    if(maximum>tolerance)std::printf("Image max difference: %u (allowed %u)\n",maximum,tolerance);
    CHECK(maximum<=tolerance);
}
static SdlEnginePixelConstants camera_constants(const FoamGpu &gpu,const SDL_GPUViewport &v)
{
    SdlEnginePixelConstants c={};c.ShadowViewport[0]=1.0f/gpu.W;c.ShadowViewport[1]=1.0f/gpu.H;
    // Fold full-texture NDC into the bounded viewport and undo the actual depth
    // range. Deliberately asymmetric projective inverse tests translation/packing.
    const float ax=gpu.W/v.w,bx=(gpu.W-2*v.x)/v.w-1;
    const float ay=gpu.H/v.h,by=1-(gpu.H-2*v.y)/v.h,az=1/(v.max_depth-v.min_depth),bz=-v.min_depth*az;
    c.WorldFromClip[0]=ax*70;c.WorldFromClip[12]=bx*70;
    c.WorldFromClip[5]=ay*70;c.WorldFromClip[13]=by*70;
    c.WorldFromClip[2]=ax*4;c.WorldFromClip[6]=ay*3;c.WorldFromClip[10]=az*80;c.WorldFromClip[14]=bx*4+by*3+bz*80+1;
    c.WorldFromClip[3]=ax*.12f;c.WorldFromClip[7]=ay*.08f;c.WorldFromClip[11]=az*.2f;c.WorldFromClip[15]=bx*.12f+by*.08f+bz*.2f+1;
    return c;
}
static std::vector<float> sampled_depth(const FoamGpu &gpu,const SDL_GPUViewport &v,float physicalDepth,bool asymmetric=false)
{
    std::vector<float> pixels(size_t(gpu.W)*gpu.H,.5f);
    for(unsigned y=0;y<gpu.H;++y)for(unsigned x=0;x<gpu.W;++x) {
        if(!inside(x,y,v))continue;
        // Independently invert the intended raw camera, using viewport-local NDC.
        const double nx=2*(x+.5-v.x)/v.w-1,ny=1-2*(y+.5-v.y)/v.h;
        const double q=4*nx+3*ny+1,d=.12*nx+.08*ny+1;
        const double surface=(80*.6+q)/(.2*.6+d);
        const double gap=asymmetric ? .1+2*x/double(gpu.W)+.7*y/double(gpu.H) : physicalDepth;
        const double bottom=surface-gap,z=(bottom*d-q)/(80-.2*bottom);
        pixels[size_t(y)*gpu.W+x]=float(v.min_depth+z*(v.max_depth-v.min_depth));
        CHECK(pixels[size_t(y)*gpu.W+x]>0 && pixels[size_t(y)*gpu.W+x]<1);
    }
    return pixels;
}
static int gpu()
{
    const bool initialized=SDL_Init(SDL_INIT_VIDEO);if(!initialized)return 77;
    struct Video {~Video(){SDL_Quit();}} video;
    {
        FoamGpu g;if(!g.init())return 77;
        auto *a=g.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,g.W,g.H,nullptr,0,true);
        auto *b=g.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,g.W,g.H,nullptr,0,true);
        const unsigned char base[]={25,51,76,128},black[]={0,0,0,255},white[]={255,255,255,255},edge[]={0,0,0,128},zeroAlpha[]={0,0,0,0};
        auto colour=[&](const unsigned char *p){return g.texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,1,1,p,4);};
        auto *baseTexture=colour(base),*blackTexture=colour(black),*whiteTexture=colour(white),*edgeTexture=colour(edge),*zeroEdge=colour(zeroAlpha);
        CHECK(a && b && baseTexture && blackTexture && whiteTexture && edgeTexture && zeroEdge);
        if(!(a && b && baseTexture && blackTexture && whiteTexture && edgeTexture && zeroEdge))return 1;
        const SDL_GPUViewport views[]={{0,0,float(g.W),float(g.H),0,1},{7,9,61,43,.2f,.8f}};
        for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID_FOAM,ENGINE_SHADER_WATER_RIVER_FOAM}) {
            auto *actual=g.pipeline(program);auto *diagnostic=g.pipeline(program,nullptr,true);
            for(const auto &view:views) {
                auto constants=camera_constants(g,view);
                std::array<SDL_GPUTexture *,8> textures={baseTexture,blackTexture,blackTexture,program==ENGINE_SHADER_WATER_RIVER_FOAM?edgeTexture:whiteTexture,whiteTexture,whiteTexture,blackTexture,whiteTexture};
                for(const char *depth:{"0.0","0.3","1.0","8.0"}) {
                    const float gap=std::strtof(depth,nullptr);auto source=sampled_depth(g,view,gap);
                    textures[7]=g.texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT,g.W,g.H,source.data(),unsigned(source.size()*4));
                    auto *reference=g.pipeline(program,depth);
                    auto drawn=g.draw(actual,a,view,constants,textures),expected=g.draw(reference,b,view,constants,textures);same_image(drawn,expected);
                    CHECK(drawn.size()==size_t(g.W)*g.H*4);if(drawn.empty())continue;
                    for(unsigned y=0;y<g.H;++y)for(unsigned x=0;x<g.W;++x) {
                        const auto i=(size_t(y)*g.W+x)*4;
                        if(!inside(x,y,view)){CHECK(drawn[i]==4 && drawn[i+1]==6 && drawn[i+2]==9 && drawn[i+3]==11);continue;}
                        if(gap==0)CHECK(drawn[i+3]==0);
                        if(gap==.3f) {
                            const unsigned low=program==ENGINE_SHADER_WATER_RIVER_FOAM?31:63,high=program==ENGINE_SHADER_WATER_RIVER_FOAM?65:129;
                            CHECK(drawn[i+3]>=low && drawn[i+3]<=high);
                        }
                    }
                }
                // Asymmetric sampled depth independently checks top-left coordinates,
                // attachment-sized Load, bounded viewport, and actual depth range.
                auto field=sampled_depth(g,view,0,true);textures[7]=g.texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT,g.W,g.H,field.data(),unsigned(field.size()*4));
                auto measured=g.draw(diagnostic,a,view,constants,textures);CHECK(measured.size()==size_t(g.W)*g.H*4);
                if(!measured.empty())for(unsigned y=0;y<g.H;++y)for(unsigned x=0;x<g.W;++x)if(inside(x,y,view)) {
                    const int expected=int(std::lround((.1+2*x/double(g.W)+.7*y/double(g.H))/4*255));
                    CHECK(std::abs(int(measured[(size_t(y)*g.W+x)*4])-expected)<=1);
                }
                auto shallow=sampled_depth(g,view,.3f);textures[7]=g.texture(SDL_GPU_TEXTUREFORMAT_R32_FLOAT,g.W,g.H,shallow.data(),unsigned(shallow.size()*4));
                auto phase0=g.draw(actual,a,view,constants,textures);constants.Combiner.TextureFactor[1]=.37f;
                auto phaseShift=g.draw(actual,b,view,constants,textures);unsigned changed=0;
                CHECK(phase0.size()==phaseShift.size());if(phase0.empty() || phase0.size()!=phaseShift.size())continue;
                for(size_t i=0;i<phase0.size();++i)changed+=phase0[i]!=phaseShift[i];
                CHECK(changed>100);
                constants.Combiner.TextureFactor[1]=1;auto period=g.draw(actual,b,view,constants,textures);same_image(phase0,period);
                constants.Combiner.TextureFactor[1]=0;
                constants.Combiner.TextureFactor[0]=.0625f;constants.Combiner.TextureFactor[3]=.7f;
                auto feathered=g.draw(actual,b,view,constants,textures);
                auto featherReference=g.draw(g.pipeline(program,"0.3"),a,view,constants,textures);same_image(feathered,featherReference);
                constants.Combiner.TextureFactor[0]=constants.Combiner.TextureFactor[3]=0;
                auto dim=g.draw(actual,b,view,constants,textures,.4f);
                if(!dim.empty())for(unsigned y=0;y<g.H;++y)for(unsigned x=0;x<g.W;++x)if(inside(x,y,view)) {
                    const auto i=(size_t(y)*g.W+x)*4;for(unsigned c=0;c<3;++c)CHECK(std::abs(int(dim[i+c])-int(std::lround(phase0[i+c]*.4)))<=1);
                    CHECK(dim[i+3]==phase0[i+3]);
                }
                textures[3]=program==ENGINE_SHADER_WATER_RIVER_FOAM?zeroEdge:blackTexture;
                auto hidden=g.draw(actual,b,view,constants,textures);
                if(!hidden.empty())for(unsigned y=0;y<g.H;++y)for(unsigned x=0;x<g.W;++x)if(inside(x,y,view)) {
                    const auto i=(size_t(y)*g.W+x)*4;
                    if(program==ENGINE_SHADER_WATER_RIVER_FOAM)CHECK(hidden[i+3]==0);
                    else CHECK(hidden[i]==0 && hidden[i+1]==0 && hidden[i+2]==0 && hidden[i+3]==phase0[i+3]);
                }
            }
        }
    }
    return failures?1:0;
}
int main(int argc,char **argv)
{
    cpu();if(argc>1 && std::strcmp(argv[1],"--gpu")==0){int result=gpu();if(result==77)return 77;}
    std::printf("Classic foam: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
