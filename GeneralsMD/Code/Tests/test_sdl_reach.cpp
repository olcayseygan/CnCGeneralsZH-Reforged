// Exact distance-field shader semantics, explicit terrain-depth identity and
// atomic multi-owner native packets. --gpu uses actual Vulkan/Metal resources.
#include "SdlReachOverlay.h"
#include "SdlReachOverlayShader.h"
#include "sdl3shadercompile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line%u: %s\n",__LINE__,#x);}}while(0)
static bool require(bool ok,const std::string &error){if(!ok)std::printf("ERROR: %s\n",error.c_str());CHECK(ok);return ok;}
static SdlReachOverlay::Inputs camera() {
    SdlReachOverlay::Inputs input;input.Target.Width=128;input.Target.Height=96;input.ViewProjection[0]=input.ViewProjection[5]=input.ViewProjection[10]=input.ViewProjection[15]=1;memcpy(input.WorldFromClip,input.ViewProjection,sizeof(input.WorldFromClip));
    input.Viewport[0]=12;input.Viewport[1]=8;input.Viewport[2]=80;input.Viewport[3]=60;input.Viewport[4]=.2f;input.Viewport[5]=.8f;input.Scissor[2]=128;input.Scissor[3]=96;return input;
}
static void look_arrays(const SdlReachOverlay::Look &look,float a[4],float b[4],float c[4],float d[4],float seconds) {
    const float av[]={look.BorderWidth,look.BandWidth,look.FillAlpha,look.BandAlpha},bv[]={look.LineAlpha,look.LineWhite,0,seconds},cv[]={look.SoftWidth,look.SoftLine,look.RippleAlpha,look.RipplePeriod},dv[]={look.RippleSpan,look.RippleWidth,look.ShadeWidth,look.ShadeAlpha};memcpy(a,av,16);memcpy(b,bv,16);memcpy(c,cv,16);memcpy(d,dv,16);
}
static void cpu() {
    std::string error,msl;std::vector<unsigned char> spirv;unsigned samplers=0,uniforms=0;
    require(SDL3_Compile_HLSL_To_SPIRV(SdlReachShader::Vertex(),true,spirv,error),error);require(SDL3_Translate_SPIRV_To_MSL(spirv,true,msl,error),error);
    require(SDL3_Compile_HLSL_To_SPIRV(SdlReachShader::Pixel(),false,spirv,error),error);require(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error),error);SDL3_Shader_Slots(spirv,false,samplers,uniforms);
    CHECK(samplers==2 && uniforms==1);
    CHECK(sizeof(ReachOverlayShaderSources::Constants)==1184);CHECK(offsetof(ReachOverlayShaderSources::Constants,Grid)==160 && offsetof(ReachOverlayShaderSources::Constants,Size)==672);
    auto input=camera();ReachOverlayShaderSources::Constants constants={};CHECK(SdlReachOverlay::CameraConstants(input,constants));CHECK(std::fabs(constants.WorldFromClip[10]-1/.6f)<1e-6f);CHECK(std::fabs(constants.WorldFromClip[14]+.2f/.6f)<1e-6f);CHECK(constants.Viewport[0]==12.5f && constants.Viewport[1]==8.5f);
    CHECK(std::fabs(.5f*constants.WorldFromClip[10]+constants.WorldFromClip[14]-.5f)<1e-6f);
    SdlReachOverlay::Field field;field.Width=field.Height=33;field.Step=2.0f/32;field.OriginX=field.OriginY=-1;const SdlReachOverlay::Field *fields[]={&field};
    CHECK((SdlReachOverlay::Coverage(input,fields,1)==std::array<int32_t,4>({12,8,80,60})));input.Scissor[0]=20;input.Scissor[1]=16;input.Scissor[2]=40;input.Scissor[3]=20;CHECK((SdlReachOverlay::Coverage(input,fields,1)==std::array<int32_t,4>({20,16,40,20})));
    input.ViewProjection[11]=-1;input.ViewProjection[15]=0;CHECK((SdlReachOverlay::Coverage(input,fields,1)==std::array<int32_t,4>({20,16,40,20})));
    input=camera();input.Viewport[5]=input.Viewport[4];CHECK(!SdlReachOverlay::CameraConstants(input,constants));input=camera();memset(input.WorldFromClip,0,sizeof(input.WorldFromClip));CHECK(!SdlReachOverlay::CameraConstants(input,constants));
    float a[4],b[4],c[4],d[4],colour[]={.2f,.4f,.8f};auto look=SdlReachOverlay::DefaultLook();look_arrays(look,a,b,c,d,0);
    auto far=SdlReachShader::Shade(16,1,.6f,.1f,colour,a,b,c,d);CHECK(std::fabs(far[3]-.048f)<1e-6f);CHECK(std::fabs(far[2]-.0384f)<1e-6f);
    auto blocked=SdlReachShader::Shade(6,0,1,.1f,colour,a,b,c,d);CHECK(std::fabs(blocked[3]-.0512f)<1e-6f);auto outside=SdlReachShader::Shade(-.1f,1,1,1,colour,a,b,c,d);CHECK((outside==std::array<float,4>({0,0,0,0})));
}
#ifndef REACH_CPU_ONLY
struct Pixel {float R,G,B,A;};
struct FixtureField {
    SdlReachOverlay::Field Meta;std::vector<float> Distance,Hardness;
    SdlReachOverlay::Field view() const {auto copy=Meta;copy.Distance=Distance.data();copy.DistanceCount=Distance.size();copy.Hardness=Hardness.empty()?nullptr:Hardness.data();copy.HardnessCount=Hardness.size();return copy;}
};
static FixtureField field(uint64_t identity,unsigned w,unsigned h,float step,float bias,float dx=0,float dy=0,float hardness=1,uint32_t colour=0xff0000,float opacity=1) {
    FixtureField f;f.Meta.Identity=identity;f.Meta.Version=identity;f.Meta.Width=w;f.Meta.Height=h;f.Meta.Step=step;f.Meta.OriginX=f.Meta.OriginY=-1.2f;f.Meta.DistanceLimit=16;f.Meta.Colour=colour;f.Meta.Opacity=opacity;f.Distance.resize(w*h);f.Hardness.resize(w*h,hardness);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)f.Distance[y*w+x]=bias+dx*(f.Meta.OriginX+x*step)+dy*(f.Meta.OriginY+y*step);return f;
}
static bool failedFlush=false;static int textureBudget=-1;static unsigned flushCalls=0;
static bool flush(SdlGpuFrame *frame){++flushCalls;return !failedFlush && frame->Flush();}
static SDL_GPUTexture *allocate(SDL_GPUDevice *device,const SDL_GPUTextureCreateInfo *info){if(textureBudget==0){SDL_SetError("bounded reach texture allocator refusal");return nullptr;}if(textureBudget>0)--textureBudget;return SDL_CreateGPUTexture(device,info);}
static SdlReachOverlay::Policies policy(){return {allocate,flush};}
struct GroundBlock {float Colour[4],Viewport[4],Parameters[4];};
static const char GroundPixel[]=R"(
cbuffer Ground : register(b0,space3) {float4 Colour;float4 Viewport;float4 Parameters;};
struct Result {float4 Colour:SV_Target0;float Depth:SV_Depth;};
Result main(float4 position:SV_Position) {
 float2 uv=(position.xy-Viewport.xy-.5)/Viewport.zw;float depth=.5;
 if(Parameters.x==1)depth=.2+.6*uv.x;
 if(Parameters.x==2)depth=(uv.x>.35 && uv.x<.65 && uv.y>.25 && uv.y<.75)?.25:.7;
 Result r;r.Colour=Colour;r.Depth=Parameters.y+depth*Parameters.z;return r;
})";
struct Scene {
    std::unique_ptr<SdlGpuFrame> Frame;SDL_GPUShader *Vertex=nullptr,*PixelShader=nullptr;SDL_GPUGraphicsPipeline *Ground=nullptr;SDL_GPUTexture *Depth=nullptr;SdlReachOverlay::Inputs Input;GroundBlock Block={};unsigned Mode=0;std::string Error;
    bool initialize(unsigned w=128,unsigned h=96) {
        Frame.reset(SdlGpuFrame::Create(nullptr,w,h,Error));if(!require(bool(Frame),Error))return false;
        std::printf("GPU: driver=%s device=%s\n",SDL_GetGPUDeviceDriver(Frame->Device()),SDL_GetStringProperty(SDL_GetGPUDeviceProperties(Frame->Device()),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"));
        std::vector<unsigned char> spirv;require(SDL3_Compile_HLSL_To_SPIRV(SdlReachShader::Vertex(),true,spirv,Error),Error);Vertex=SDL3_Create_Shader(Frame->Device(),spirv,true,Error);require(Vertex!=nullptr,Error);
        require(SDL3_Compile_HLSL_To_SPIRV(GroundPixel,false,spirv,Error),Error);PixelShader=SDL3_Create_Shader(Frame->Device(),spirv,false,Error);require(PixelShader!=nullptr,Error);
        SDL_GPUColorTargetDescription target={};target.format=SDL_GPUTextureFormat(SdlGpuFrame::Target_Format());SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=Vertex;info.fragment_shader=PixelShader;info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.enable_depth_clip=true;info.depth_stencil_state.enable_depth_test=info.depth_stencil_state.enable_depth_write=true;info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_ALWAYS;info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&target;info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=SDL_GPUTextureFormat(Frame->Depth_Format());Ground=SDL_CreateGPUGraphicsPipeline(Frame->Device(),&info);require(Ground!=nullptr,Error);
        Input=camera();Input.Target=Frame->Back_Buffer_Target();Input.Eligible=true;Input.DeviceGeneration=Frame->Device_Generation();Input.ViewKey=77;Input.Viewport[0]=Input.Viewport[1]=0;Input.Viewport[2]=w;Input.Viewport[3]=h;Input.Viewport[4]=0;Input.Viewport[5]=1;Input.Scissor[2]=w;Input.Scissor[3]=h;
        return Ground && resize_depth();
    }
    bool resize_depth(){if(Depth)SDL_ReleaseGPUTexture(Frame->Device(),Depth);SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=Input.Target.Width;info.height=Input.Target.Height;info.layer_count_or_depth=info.num_levels=1;Depth=SDL_CreateGPUTexture(Frame->Device(),&info);require(Depth!=nullptr,Error);return Depth;}
    SdlRecordedDraw ground_draw() {SdlRecordedDraw draw={};draw.Pipeline=Ground;draw.Count=3;memset(Frame->Stage(16,draw.VertexOffset),0,16);memcpy(draw.Viewport,Input.Viewport,sizeof(draw.Viewport));draw.Scissor[2]=Input.Target.Width;draw.Scissor[3]=Input.Target.Height;return draw;}
    bool prepare() {
        Frame->Set_Target(Input.Target);Frame->Clear_Back_Buffer(true,true,false,0x5f203040,1,0);
        const float colour[]={32/255.0f,48/255.0f,64/255.0f,95/255.0f};memcpy(Block.Colour,colour,16);memcpy(Block.Viewport,Input.Viewport,16);Block.Parameters[0]=Mode;Block.Parameters[1]=Input.Viewport[4];Block.Parameters[2]=Input.Viewport[5]-Input.Viewport[4];
        if(Mode!=3){auto draw=ground_draw();SdlNativeUniformBlock block={1,0,&Block,sizeof(Block)};if(!require(Frame->Record_Native_Draw(Input.Target,draw,&block,1),"real ground/deck depth draw"))return false;}
        if(!require(Frame->Capture_Scene_Depth(Input.Target,Depth),"independent terrain R32 depth copy"))return false;
        Input.TerrainDepth=Depth;Input.DepthFormat=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;Input.DepthWidth=Input.Target.Width;Input.DepthHeight=Input.Target.Height;Input.FrameSerial=Frame->Scene_Frame();Input.DepthCapture.Source=Input.Target;Input.DepthCapture.Frame=Input.FrameSerial;Input.DepthCapture.Generation=Frame->Scene_Generation();Input.DepthCapture.ViewKey=Input.ViewKey;Input.DepthCapture.Depth=true;Input.DepthCapture.Phase=SdlSceneCapturePhase::TerrainOverlay;return true;
    }
    bool read(std::vector<uint8_t> &pixels){return require(Frame->Read_Back(Input.Target.Colour,Input.Target.Width,Input.Target.Height,pixels),Error);}
    ~Scene(){if(!Frame)return;failedFlush=false;if(!SdlReachOverlay::TryRelease()){Frame->Discard_Pending_Work_For_Teardown();SdlReachOverlay::TryRelease();}Frame->Flush();if(Depth)SDL_ReleaseGPUTexture(Frame->Device(),Depth);if(Ground)SDL_ReleaseGPUGraphicsPipeline(Frame->Device(),Ground);if(Vertex)SDL_ReleaseGPUShader(Frame->Device(),Vertex);if(PixelShader)SDL_ReleaseGPUShader(Frame->Device(),PixelShader);}
};
static SdlReachShader::Sample sample(const FixtureField &field,float x,float y) {
    const auto &f=field.Meta;float gx=(x-f.OriginX)/f.Step,gy=(y-f.OriginY)/f.Step;if(gx<0 || gy<0 || gx>f.Width-1 || gy>f.Height-1)return {-std::max(f.DistanceLimit,1.0f),1};
    unsigned ix=unsigned(std::min(std::floor(gx),float(f.Width-2))),iy=unsigned(std::min(std::floor(gy),float(f.Height-2)));float a=gx-ix,b=gy-iy;
    auto at=[&](unsigned xx,unsigned yy){size_t n=size_t(yy)*f.Width+xx;return SdlReachShader::Sample{field.Distance[n],n<field.Hardness.size()?field.Hardness[n]:1.0f};};auto v00=at(ix,iy),v10=at(ix+1,iy),v01=at(ix,iy+1),v11=at(ix+1,iy+1);
    return {(v00.Distance*(1-a)+v10.Distance*a)*(1-b)+(v01.Distance*(1-a)+v11.Distance*a)*b,(v00.Hardness*(1-a)+v10.Hardness*a)*(1-b)+(v01.Hardness*(1-a)+v11.Hardness*a)*b};
}
static std::array<float,2> world(const Scene &scene,int x,int y) {
    const auto &in=scene.Input;float u=(x-in.Viewport[0])/in.Viewport[2],v=(y-in.Viewport[1])/in.Viewport[3],z=.5;
    if(scene.Mode==1)z=.2f+.6f*u;if(scene.Mode==2)z=(u>.35f && u<.65f && v>.25f && v<.75f)?.25f:.7f;
    // Readback depth is minZ + projectionDepth*range; applying the original
    // inverse after range correction gives these normalized projection inputs.
    float clip[]={u*2-1,1-v*2,z,1},result[4]={};for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)result[c]+=clip[r]*in.WorldFromClip[r*4+c];float w=std::fabs(result[3])<1e-6f?1e-6f:result[3];return {result[0]/w,result[1]/w};
}
static Pixel reference(const Scene &scene,const std::vector<FixtureField> &fields,int x,int y) {
    Pixel result={32/255.0f,48/255.0f,64/255.0f,95/255.0f};const auto &in=scene.Input;
    if(x<in.Viewport[0] || y<in.Viewport[1] || x>=in.Viewport[0]+in.Viewport[2] || y>=in.Viewport[1]+in.Viewport[3] || x<in.Scissor[0] || y<in.Scissor[1] || x>=in.Scissor[0]+in.Scissor[2] || y>=in.Scissor[1]+in.Scissor[3])return result;
    std::vector<size_t> order(fields.size());for(size_t i=0;i<order.size();++i)order[i]=i;std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return (fields[a].Meta.Colour&0xffffff)<(fields[b].Meta.Colour&0xffffff);});
    auto look=SdlReachOverlay::DefaultLook();float l0[4],l1[4],l2[4],l3[4];look_arrays(look,l0,l1,l2,l3,in.VisualSeconds);
    for(size_t begin=0;begin<order.size();) {uint32_t colour=fields[order[begin]].Meta.Colour&0xffffff;size_t end=begin;while(end<order.size() && end-begin<32 && (fields[order[end]].Meta.Colour&0xffffff)==colour)++end;
        auto at=[&](int px,int py){auto p=world(scene,px,py);SdlReachShader::Sample best={-1e6f,1};for(size_t i=begin;i<end;++i){auto v=sample(fields[order[i]],p[0],p[1]);if(v.Distance>best.Distance)best=v;}return best;};
        auto p=world(scene,x,y);auto value=at(x,y);int qx=x&~1,qy=y&~1;float footprint=std::clamp(std::fabs(at(qx+1,qy).Distance-at(qx,qy).Distance)+std::fabs(at(qx,qy+1).Distance-at(qx,qy).Distance),.001f,16.0f),opacity=0;
        for(size_t i=begin;i<end;++i)opacity=std::max(opacity,fields[order[i]].Meta.Opacity*std::clamp(sample(fields[order[i]],p[0],p[1]).Distance/footprint,0.0f,1.0f));
        float owner[]={float((colour>>16)&255)/255,float((colour>>8)&255)/255,float(colour&255)/255};auto shade=SdlReachShader::Shade(value.Distance,value.Hardness,opacity,footprint,owner,l0,l1,l2,l3);
        result.R=shade[0]+result.R*(1-shade[3]);result.G=shade[1]+result.G*(1-shade[3]);result.B=shade[2]+result.B*(1-shade[3]);begin=end;
    }return result;
}
static std::vector<SdlReachOverlay::Field> views(const std::vector<FixtureField> &fields){std::vector<SdlReachOverlay::Field> result;for(const auto &field:fields)result.push_back(field.view());return result;}
static void compare(const Scene &scene,const std::vector<FixtureField> &fields,const std::vector<uint8_t> &pixels) {
    const unsigned w=scene.Input.Target.Width,h=scene.Input.Target.Height;CHECK(pixels.size()==size_t(w)*h*4);unsigned mismatches=0,tested=0;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){size_t n=(size_t(y)*w+x)*4;CHECK(pixels[n+3]==95);if(x%3 || y%3)continue;
        // Derivatives at raster/scissor boundaries are implementation-dependent;
        // sample analytic interior plus all definitely-outside pixels.
        const auto &v=scene.Input.Viewport;bool near=x>=v[0]-2 && x<v[0]+2 || x>=v[0]+v[2]-2 && x<v[0]+v[2]+2 || y>=v[1]-2 && y<v[1]+2 || y>=v[1]+v[3]-2 && y<v[1]+v[3]+2;if(near)continue;
        auto expected=reference(scene,fields,x,y);float rgb[]={expected.B,expected.G,expected.R};++tested;for(unsigned c=0;c<3;++c)if(std::abs(int(pixels[n+c])-int(std::lround(rgb[c]*255)))>3)++mismatches;
    }if(mismatches)std::printf("RGB mismatches=%u sampled=%u\n",mismatches,tested);CHECK(mismatches==0);
}
static bool run(Scene &scene,const std::vector<FixtureField> &fields,std::vector<uint8_t> &pixels,bool check=true) {
    if(!scene.prepare())return false;auto input=views(fields);if(!require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"native reach draw"))return false;
    if(!scene.read(pixels))return false;if(check)compare(scene,fields,pixels);return true;
}
static bool baseline(const std::vector<uint8_t> &pixels){for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]!=64 || pixels[i+1]!=48 || pixels[i+2]!=32 || pixels[i+3]!=95)return false;return true;}
static void atomic_fixture(Scene &scene) {
    require(scene.prepare(),"atomic scene");auto draw=scene.ground_draw();GroundBlock red=scene.Block,blue=scene.Block;red.Colour[0]=1;blue.Colour[2]=1;SdlNativeUniformBlock blocks[]={{1,0,&red,sizeof(red)},{1,0,&blue,sizeof(blue)}};SdlNativeDrawPacket packets[]={{scene.Input.Target,draw,&blocks[0],1},{scene.Input.Target,draw,&blocks[1],1}};
    packets[1].Draw.SamplerCount=SdlRecordedDraw::MAXIMUM_SAMPLERS+1;const bool pending=scene.Frame->Has_Pending_Work();CHECK(!scene.Frame->Record_Native_Draw_Batch(packets,2));CHECK(scene.Frame->Has_Pending_Work()==pending);std::vector<uint8_t> pixels;scene.read(pixels);CHECK(baseline(pixels));
    packets[1].Draw.SamplerCount=0;require(scene.Frame->Record_Native_Draw_Batch(packets,2),"valid immutable two-packet commit");red.Colour[0]=0;blue.Colour[2]=0;scene.read(pixels);unsigned at=(scene.Input.Target.Height/2*scene.Input.Target.Width+scene.Input.Target.Width/2)*4;CHECK(pixels[at]==255 && pixels[at+3]==95);
}
static int gpu() {
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("GPU UNAVAILABLE: %s\n",SDL_GetError());return 77;}int result=0;
    {
        Scene scene;if(!scene.initialize())return 77;CHECK(SdlReachOverlay::Supported(*scene.Frame));atomic_fixture(scene);
        std::vector<uint8_t> pixels;std::vector<FixtureField> fields={field(1,33,33,.075f,5,5,.5f)};run(scene,fields,pixels);
        auto stats=SdlReachOverlay::Stats();for(unsigned i=0;i<12;++i){scene.Input.VisualSeconds=i*.25f;run(scene,fields,pixels,false);}CHECK(SdlReachOverlay::Stats().TextureAllocations==stats.TextureAllocations && SdlReachOverlay::Stats().Uploads==stats.Uploads && SdlReachOverlay::Stats().OwnedTextureBytes==stats.OwnedTextureBytes);
        fields={field(2,9,13,.3f,8,1,0,0,0x0010a020,.2f),field(3,7,7,.4f,6,0,1,1,0x0010a020,.9f)};fields[1].Hardness.clear();run(scene,fields,pixels);
        fields={field(4,9,9,.3f,6,0,0,0,0xaaef0000),field(5,5,7,.6f,6,0,0,1,0x01ef0000)};run(scene,fields,pixels);
        fields={field(6,9,9,.3f,5,2,0,1,0xff0000),field(7,13,9,.3f,7,-1,1,1,0x0000ff)};run(scene,fields,pixels);
        std::vector<FixtureField> many;for(unsigned i=0;i<33;++i)many.push_back(field(100+i,3+i%3,3+i%4,1.2f,6,0,0,1));std::vector<FixtureField> first32(many.begin(),many.begin()+32);stats=SdlReachOverlay::Stats();run(scene,first32,pixels);CHECK(SdlReachOverlay::Stats().RecordedBatches==stats.RecordedBatches+1);stats=SdlReachOverlay::Stats();run(scene,many,pixels);CHECK(SdlReachOverlay::Stats().RecordedBatches==stats.RecordedBatches+2);stats=SdlReachOverlay::Stats();run(scene,many,pixels,false);CHECK(SdlReachOverlay::Stats().TextureAllocations==stats.TextureAllocations && SdlReachOverlay::Stats().Uploads==stats.Uploads);
        scene.Input.Viewport[0]=8;scene.Input.Viewport[1]=6;scene.Input.Viewport[2]=112;scene.Input.Viewport[3]=64;scene.Input.Viewport[4]=.2f;scene.Input.Viewport[5]=.8f;scene.Input.Scissor[0]=16;scene.Input.Scissor[1]=10;scene.Input.Scissor[2]=96;scene.Input.Scissor[3]=56;
        fields={field(501,17,17,.15f,5,4,2,.35f,0x60c0ff)};scene.Mode=1;scene.Input.WorldFromClip[8]=.5f;scene.Input.ViewProjection[8]=-.5f;run(scene,fields,pixels);scene.Mode=2;run(scene,fields,pixels);scene.Mode=3;run(scene,fields,pixels,false);CHECK(baseline(pixels));scene.Mode=0;
        std::vector<float> depthBefore,depthAfter;require(scene.prepare(),"depth preservation scene");require(scene.Frame->Read_Depth(scene.Input.Target.Depth,scene.Input.Target.Width,scene.Input.Target.Height,depthBefore),"depth before reach");auto input=views(fields);require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"depth preserving reach");scene.read(pixels);require(scene.Frame->Read_Depth(scene.Input.Target.Depth,scene.Input.Target.Width,scene.Input.Target.Height,depthAfter),"depth after reach");CHECK(depthBefore==depthAfter);
        // The caller can mutate/retire its grid after Draw; staged bytes and
        // immutable native constants retain this call's versioned snapshot.
        fields[0].Meta.Version++;auto saved=fields;require(scene.prepare(),"copied input scene");input=views(fields);require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"copied field upload");std::fill(fields[0].Distance.begin(),fields[0].Distance.end(),-16);scene.read(pixels);compare(scene,saved,pixels);fields=saved;
        // Refuse the second new array: first array upload must remain invisible.
        for(auto &f:many)f.Meta.Version+=1000;require(scene.prepare(),"all batches preflight");input=views(many);stats=SdlReachOverlay::Stats();textureBudget=1;CHECK(!SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()));textureBudget=-1;scene.read(pixels);CHECK(baseline(pixels));CHECK(SdlReachOverlay::Stats().RecordedBatches==stats.RecordedBatches && SdlReachOverlay::Stats().OwnedTextureBytes==stats.OwnedTextureBytes);
        require(scene.prepare(),"atomic recorder refusal scene");input=views(fields);scene.Frame->Fail_Next_Native_Record_For_Test();stats=SdlReachOverlay::Stats();CHECK(!SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()));scene.read(pixels);CHECK(baseline(pixels));CHECK(SdlReachOverlay::Stats().RecordedBatches==stats.RecordedBatches);
        // Every stale/wrong-owner/phase/format/alias request refuses before a
        // native packet. These are not interchangeable with OpaqueScene AO.
        auto valid=scene.Input;for(unsigned which=0;which<9;++which){require(scene.prepare(),"invalid identity base");auto bad=scene.Input;if(which==0)bad.DepthCapture.Phase=SdlSceneCapturePhase::OpaqueScene;if(which==1)++bad.DepthCapture.Generation;if(which==2)++bad.DepthCapture.ViewKey;if(which==3)++bad.FrameSerial;if(which==4)bad.TerrainDepth=bad.Target.Colour;if(which==5)bad.TerrainDepth=bad.Target.Depth;if(which==6)bad.DepthFormat=SDL_GPU_TEXTUREFORMAT_R16_FLOAT;if(which==7)bad.Eligible=false;if(which==8)++bad.DeviceGeneration;CHECK(!SdlReachOverlay::Draw(*scene.Frame,bad,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()));scene.read(pixels);CHECK(baseline(pixels));}
        // Pending native readers keep arrays/programs alive on a failed flush;
        // a later successful submission still draws the old version exactly.
        require(scene.prepare(),"retained reader scene");input=views(fields);require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"old reader packet");auto old=fields;fields[0].Meta.Version++;std::fill(fields[0].Distance.begin(),fields[0].Distance.end(),16);input=views(fields);stats=SdlReachOverlay::Stats();failedFlush=true;CHECK(!SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()));CHECK(!SdlReachOverlay::TryRelease());CHECK(SdlReachOverlay::Stats().OwnedTextureBytes==stats.OwnedTextureBytes);failedFlush=false;scene.read(pixels);compare(scene,old,pixels);run(scene,fields,pixels);
        require(scene.prepare(),"teardown owner scene");input=views(fields);require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"teardown pending packet");failedFlush=true;CHECK(!SdlReachOverlay::TryRelease());scene.Frame->Discard_Pending_Work_For_Teardown();unsigned calls=flushCalls;CHECK(SdlReachOverlay::TryRelease());CHECK(SdlReachOverlay::TryRelease());CHECK(flushCalls==calls);failedFlush=false;
        // A real resize keeps versioned arrays on the same device and requires
        // new depth identity. Repeated empty selection retires cached fields.
        scene.Input.WorldFromClip[8]=scene.Input.ViewProjection[8]=0;scene.Input.Viewport[0]=scene.Input.Viewport[1]=0;scene.Input.Viewport[2]=128;scene.Input.Viewport[3]=96;scene.Input.Viewport[4]=0;scene.Input.Viewport[5]=1;scene.Input.Scissor[0]=scene.Input.Scissor[1]=0;scene.Input.Scissor[2]=128;scene.Input.Scissor[3]=96;run(scene,fields,pixels);stats=SdlReachOverlay::Stats();auto stale=scene.Input;require(scene.Frame->Resize(160,100),"actual viewport resize");scene.Input.Target=scene.Frame->Back_Buffer_Target();scene.Input.Viewport[2]=160;scene.Input.Viewport[3]=72;scene.Input.Scissor[2]=160;scene.Input.Scissor[3]=100;require(scene.resize_depth(),"resized R32 depth");input=views(fields);CHECK(!SdlReachOverlay::Draw(*scene.Frame,stale,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()));run(scene,fields,pixels);CHECK(SdlReachOverlay::Stats().TextureAllocations==stats.TextureAllocations);require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,nullptr,0,SdlReachOverlay::DefaultLook(),policy()),"empty selection");CHECK(SdlReachOverlay::Stats().CachedBatches==0 && SdlReachOverlay::Stats().OwnedTextureBytes==0);
        // Final frame teardown releases its registered native owner while the
        // device is alive, even when the ordinary owner flush policy refuses.
        require(scene.prepare(),"registered teardown scene");input=views(fields);
        require(SdlReachOverlay::Draw(*scene.Frame,scene.Input,input.data(),input.size(),SdlReachOverlay::DefaultLook(),policy()),"registered teardown packet");
        failedFlush=true;scene.Frame->Release_Owner_Hooks_For_Teardown();
        CHECK(SdlReachOverlay::Stats().OwnedTextureBytes==0);CHECK(SdlReachOverlay::TryRelease());failedFlush=false;
        result=failures?1:0;
    }
    return result;
}
#endif
int main(int argc,char **argv) {
    cpu();
    if(argc>1 && !strcmp(argv[1],"--gpu")) {
#ifdef REACH_CPU_ONLY
        std::printf("GPU UNAVAILABLE: CPU-only compile\n");return 77;
#else
        const int result=gpu();SDL_Quit();std::printf("SDL reach: %u checks, %u failures\n",checks,failures);return result;
#endif
    }
    std::printf("SDL reach: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
