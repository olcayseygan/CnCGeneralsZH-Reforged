// Standalone exact sky/haze/fade fixtures. Producer policy and world/particle ordering are integrated elsewhere.
#include "SdlPostAtmosphere.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}} while(0)
static void cpu()
{
    for(auto id:{POST_SHADER_SKY,POST_SHADER_HAZE,POST_SHADER_ATMOSPHERE}) {
        std::string source,error,msl;std::vector<unsigned char> spirv;
        CHECK(Sdl_Post_Pixel_Source(id,source));
        CHECK(source.find("register(b1, space3)")!=std::string::npos);
        CHECK(source.find("WorldFromClip : packoffset(c15)")!=std::string::npos);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error));
        unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);
        CHECK(samplers==(id==POST_SHADER_SKY?4u:3u));CHECK(uniforms==2);
        CHECK(source.find(id==POST_SHADER_SKY?"register(s3, space2)":"register(s2, space2)")!=std::string::npos);
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error));CHECK(!msl.empty());
    }
    CHECK(sizeof(PostProcessConstants)==352);
}
static SdlPostSettings post_settings(bool hdr=false)
{
    SdlPostSettings s;s.Chain[0]=hdr?DX11_POST_BLOOM:DX11_POST_COPY;s.Count=1;s.BloomIntensity=0;return s;
}
static uint16_t half(float value)
{
    if(value==0)return 0;
    uint32_t bits;std::memcpy(&bits,&value,4);return uint16_t(((int(bits>>23&255)-127+15)<<10)|(((bits&0x7fffff)+0x1000)>>13));
}
static SDL_GPUTexture *texture(SdlGpuFrame &frame,unsigned w,unsigned h,SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo i={};i.type=SDL_GPU_TEXTURETYPE_2D;i.format=format;i.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    i.width=w;i.height=h;i.layer_count_or_depth=i.num_levels=1;
    auto *result=SDL_CreateGPUTexture(frame.Device(),&i);CHECK(result);return result;
}
static void upload(SdlGpuFrame &frame,SDL_GPUTexture *texture,unsigned w,unsigned h,const void *data,unsigned size)
{
    uint32_t offset=0;auto *bytes=frame.Upload_Space(size,offset);CHECK(bytes);
    if(bytes){std::memcpy(bytes,data,size);frame.Queue_Texture_Upload(texture,0,w,h,offset);}
}
static void fill(SdlGpuFrame &frame,const SdlTarget &target)
{
    if(SdlGpuFrame::Target_Colour_Format(target)==SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
        std::vector<uint16_t> pixels(size_t(target.Width)*target.Height*4,0);
        for(size_t i=3;i<pixels.size();i+=4)pixels[i]=half(.375f);
        upload(frame,target.Colour,target.Width,target.Height,pixels.data(),unsigned(pixels.size()*2));
    } else {
        std::vector<uint8_t> pixels(size_t(target.Width)*target.Height*4,0);
        for(size_t i=3;i<pixels.size();i+=4)pixels[i]=96;
        upload(frame,target.Colour,target.Width,target.Height,pixels.data(),unsigned(pixels.size()));
    }
}
static std::vector<uint8_t> read(SdlGpuFrame &frame,const SdlTarget &target)
{
    std::vector<uint8_t> result;
    if(SdlGpuFrame::Target_Colour_Format(target)!=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
        CHECK(frame.Read_Back(target.Colour,target.Width,target.Height,result));return result;
    }
    CHECK(frame.Flush());const unsigned size=target.Width*target.Height*8;
    SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=size;
    auto *transfer=SDL_CreateGPUTransferBuffer(frame.Device(),&info);CHECK(transfer);
    auto *commands=SDL_AcquireGPUCommandBuffer(frame.Device());CHECK(commands);if(!transfer||!commands)return result;
    auto *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
    SDL_GPUTextureRegion source={};source.texture=target.Colour;source.w=target.Width;source.h=target.Height;source.d=1;
    SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;destination.pixels_per_row=target.Width;destination.rows_per_layer=target.Height;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
    if(fence){CHECK(SDL_WaitForGPUFences(frame.Device(),true,&fence,1));auto *mapped=SDL_MapGPUTransferBuffer(frame.Device(),transfer,false);CHECK(mapped);
        if(mapped){result.resize(size);std::memcpy(result.data(),mapped,size);SDL_UnmapGPUTransferBuffer(frame.Device(),transfer);}SDL_ReleaseGPUFence(frame.Device(),fence);}
    SDL_ReleaseGPUTransferBuffer(frame.Device(),transfer);return result;
}
static unsigned component(const std::vector<uint8_t> &pixels,unsigned w,unsigned x,unsigned y,unsigned rgb=0)
{
    const size_t at=(size_t(y)*w+x)*4+(rgb==0?2:rgb==1?1:0);return at<pixels.size()?pixels[at]:999;
}
static void alpha(const std::vector<uint8_t> &pixels,bool hdr=false)
{
    CHECK(!pixels.empty());for(size_t i=hdr?6:3;i<pixels.size();i+=hdr?8:4) {
        if(hdr){uint16_t bits;std::memcpy(&bits,pixels.data()+i,2);CHECK(bits==half(.375f));}
        else CHECK(pixels[i]==96);
    }
}
static SdlSceneCaptureRequest main_identity(SdlGpuFrame &frame,const SdlTarget &scene)
{
    SdlSceneCaptureRequest m;m.Source=scene;m.Frame=frame.Scene_Frame();m.Generation=frame.Scene_Generation();return m;
}
static SdlPostSceneInputs capture(SdlGpuFrame &frame,const SdlTarget &scene,SDL_GPUTexture *depth,const float matrix[16],const SdlPostAtmosphereViews &views)
{
    SdlPostSceneInputs in;in.Capture=main_identity(frame,scene);in.Capture.Phase=SdlSceneCapturePhase::BeforeTransparent;in.Capture.Depth=true;
    in.SampledDepth=depth;in.DepthWidth=scene.Width;in.DepthHeight=scene.Height;in.DepthFormat=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    in.Shroud=views.Shroud.Texture;std::memcpy(in.WorldFromClip,matrix,64);std::memcpy(in.ShroudRect,views.ShroudRect,16);return in;
}
static void depth(SdlGpuFrame &frame,SDL_GPUTexture *texture,unsigned w,unsigned h,float left,float right)
{
    std::vector<float> pixels(size_t(w)*h);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)pixels[size_t(y)*w+x]=x<w/2?left:right;
    upload(frame,texture,w,h,pixels.data(),unsigned(pixels.size()*4));
}
static int gpu()
{
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP SDL: %s\n",SDL_GetError());return 77;}
    std::string error;auto *frame=SdlGpuFrame::Create(nullptr,33,25,error);
    if(!frame){std::printf("SKIP GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    const auto props=SDL_GetGPUDeviceProperties(frame->Device());std::printf("GPU: %s; driver %s\n",SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),SDL_GetGPUDeviceDriver(frame->Device()));
    {
        SdlPostProcess post;SdlPostAtmosphere module;unsigned w=33,h=25;
        auto *sampled=texture(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
        auto *sky=texture(*frame,4,4,SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
        auto *fog=texture(*frame,1,1,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        auto *black=texture(*frame,1,1,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        const uint32_t grey=0xff808080,zero=0;upload(*frame,fog,1,1,&grey,4);upload(*frame,black,1,1,&zero,4);
        uint32_t skyPixels[16];const uint32_t colours[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00};
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)skyPixels[y*4+x]=colours[(x>=2)+(y>=2)*2];
        upload(*frame,sky,4,4,skyPixels,64);
        SdlPostAtmosphereViews views;views.MapIdentity=1;views.MapGeneration=1;views.Sky={sky,100,1};
        SdlPostGroundHazeSettings haze;SdlPostAtmosphereSettings atmosphere;atmosphere.Strength=1;
        atmosphere.MapSize[0]=atmosphere.MapSize[1]=1000000;atmosphere.Horizon[0]=atmosphere.Horizon[1]=atmosphere.Horizon[2]=1;
        const float up[16]={1,0,0,0,0,1,0,0,-250,-250,1,-.5f,500,500,0,1};
        const float affineUp[16]={1,0,0,0,0,1,0,0,0,0,1,0,500,500,0,1};
        const float down[16]={1,0,0,0,0,1,0,0,0,0,-1,0,500,500,0,1};
        SdlTarget scene,presentation;
        auto begin=[&](const float *matrix,bool hdr=false) {
            presentation=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,presentation,post_settings(hdr),scene));fill(*frame,scene);
            CHECK(module.Prepare(*frame,post,scene,main_identity(*frame,scene),matrix,haze,atmosphere,views));
        };
        auto end=[&](){CHECK(post.Finish(*frame,scene,presentation));CHECK(frame->Present(nullptr));};
        begin(up);frame->Set_Target(scene);frame->Clear_Back_Buffer(false,true,false,0,.375f,0);
        std::vector<float> beforeDepth;CHECK(frame->Read_Depth(scene.Depth,w,h,beforeDepth));
        CHECK(module.Draw_Sky(*frame,scene));CHECK(!module.Draw_Sky(*frame,scene));auto pixels=read(*frame,scene);alpha(pixels);
        CHECK(component(pixels,w,1,1,1)==255 && component(pixels,w,1,1,0)==0);
        CHECK(component(pixels,w,w-2,1,0)==255 && component(pixels,w,w-2,1,1)==0);
        CHECK(component(pixels,w,1,h-2,0)==255 && component(pixels,w,1,h-2,1)==255);
        CHECK(component(pixels,w,w-2,h-2,2)==255 && component(pixels,w,w-2,h-2,0)==0);
        std::vector<float> afterDepth;CHECK(frame->Read_Depth(scene.Depth,w,h,afterDepth));CHECK(beforeDepth==afterDepth);end();
        CHECK(!module.Draw_Sky(*frame,scene));

        // Horizon follows eye distance, shroud and far-plane/map-edge closure, not the clock.
        views.Sky={};begin(down);depth(*frame,sampled,w,h,.1f,.7f);auto in=capture(*frame,scene,sampled,down,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));CHECK(!module.Apply_Atmosphere(*frame,scene,in));pixels=read(*frame,scene);alpha(pixels);
        const unsigned near=component(pixels,w,3,h/2),far=component(pixels,w,w-4,h/2);
        std::printf("horizon distance near/far: %u/%u\n",near,far);CHECK(near>=12 && near<=14);CHECK(far>=70 && far<=72);end();
        views.Shroud={fog,200,1};begin(down);depth(*frame,sampled,w,h,.7f,.7f);in=capture(*frame,scene,sampled,down,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));const unsigned fogged=component(read(*frame,scene),w,w/2,h/2);CHECK(fogged>30 && fogged<40);end();
        views.Shroud={black,201,1};begin(down);depth(*frame,sampled,w,h,.7f,.7f);in=capture(*frame,scene,sampled,down,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);end();
        views.Shroud={};begin(down);depth(*frame,sampled,w,h,1,1);in=capture(*frame,scene,sampled,down,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==140);end();
        begin(affineUp);depth(*frame,sampled,w,h,1,1);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);end();
        atmosphere.MapSize[0]=atmosphere.MapSize[1]=10;begin(down);depth(*frame,sampled,w,h,.1f,.1f);in=capture(*frame,scene,sampled,down,views);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==140);end();
        atmosphere.MapSize[0]=atmosphere.MapSize[1]=1000000;atmosphere.Strength=0;

        haze.Strength=1;haze.Top=1;haze.Thickness=1;haze.Reach=1000;haze.Colour[0]=haze.Colour[1]=haze.Colour[2]=1;
        begin(affineUp);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(!module.Apply_Ground_Haze(*frame,scene,in));pixels=read(*frame,scene);alpha(pixels);
        const unsigned low=component(pixels,w,w/2,h/2);CHECK(low>16);end();
        begin(affineUp);depth(*frame,sampled,w,h,.75f,.75f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));const unsigned high=component(read(*frame,scene),w,w/2,h/2);CHECK(high>0 && low>high+16);end();
        haze.Top=.2f;begin(affineUp);depth(*frame,sampled,w,h,.5f,.5f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);end();
        haze.Top=1;haze.WaterHeight=10;begin(affineUp);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);end();
        haze.WaterHeight=-1e6f;views.Shroud={fog,200,1};begin(affineUp);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));const unsigned hazeFog=component(read(*frame,scene),w,w/2,h/2);CHECK(hazeFog>0 && hazeFog<low/8);end();
        views.Shroud={black,201,1};begin(affineUp);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,affineUp,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);end();
        views.Shroud={};haze.Warmth=1;haze.TowardSun[0]=1;haze.TowardSun[2]=0;
        begin(up);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,views);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));pixels=read(*frame,scene);
        CHECK(component(pixels,w,w-3,h/2,0)>component(pixels,w,w-3,h/2,2));
        CHECK(component(pixels,w,2,h/2,2)>component(pixels,w,2,h/2,0));end();
        std::printf("ground haze low/high/fog: %u/%u/%u\n",low,high,hazeFog);

        // Settings are copied; explicit stale/mismatched inputs cannot consume completion flags.
        haze.Warmth=0;begin(affineUp);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,affineUp,views);
        auto bad=in;bad.Capture.Phase=SdlSceneCapturePhase::BeforePost;CHECK(!module.Apply_Ground_Haze(*frame,scene,bad));
        bad=in;bad.Capture.Generation++;CHECK(!module.Apply_Ground_Haze(*frame,scene,bad));
        bad=in;bad.DepthWidth++;CHECK(!module.Apply_Ground_Haze(*frame,scene,bad));
        bad=in;bad.WorldFromClip[0]+=1;CHECK(!module.Apply_Ground_Haze(*frame,scene,bad));
        bad=in;bad.ShroudRect[0]+=1;CHECK(!module.Apply_Ground_Haze(*frame,scene,bad));
        haze.Strength=0;CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)==low);end();haze.Strength=1;
        begin(affineUp);haze.Strength=0;atmosphere.Strength=0;
        CHECK(module.Prepare(*frame,post,scene,main_identity(*frame,scene),affineUp,haze,atmosphere,views));
        CHECK(!module.Apply_Ground_Haze(*frame,scene,in));CHECK(!module.Draw_Sky(*frame,scene));end();

        // Explicit main-view ownership cannot reuse capture identity from another view.
        haze.Strength=1;atmosphere.Strength=1;
        auto keyed=post_settings();keyed.MainViewKey=23;
        presentation=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,presentation,keyed,scene));fill(*frame,scene);
        auto mainKey=main_identity(*frame,scene);
        CHECK(!module.Prepare(*frame,post,scene,mainKey,affineUp,haze,atmosphere,views));
        mainKey.ViewKey=23;CHECK(module.Prepare(*frame,post,scene,mainKey,affineUp,haze,atmosphere,views));
        in=capture(*frame,scene,sampled,affineUp,views);CHECK(!module.Apply_Ground_Haze(*frame,scene,in));
        in.Capture.ViewKey=23;CHECK(module.Apply_Ground_Haze(*frame,scene,in));end();

        // Prepare failures never draw or mark a stage complete; partial fallbacks remain safely owned.
        haze.Strength=1;atmosphere.Strength=1;
        using Failure=SdlPostAtmosphere::TestFailure;
        for(Failure point:{Failure::Program,Failure::Pipeline,Failure::Target,Failure::TargetAfterOne}) {
            presentation=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,presentation,post_settings(),scene));fill(*frame,scene);
            SdlPostAtmosphere recovery;recovery.Set_Failure_For_Test(point);
            CHECK(!recovery.Prepare(*frame,post,scene,main_identity(*frame,scene),down,haze,atmosphere,views));
            CHECK(!recovery.Draw_Sky(*frame,scene));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);
            recovery.Set_Failure_For_Test(Failure::None);
            CHECK(recovery.Prepare(*frame,post,scene,main_identity(*frame,scene),down,haze,atmosphere,views));
            CHECK(recovery.Draw_Sky(*frame,scene));end();CHECK(recovery.Release(*frame));
        }
        // HDR output retains values above white, while all three operations preserve half alpha.
        views.Sky={sky,100,1};atmosphere.Tint[0]=atmosphere.Tint[1]=atmosphere.Tint[2]=2;
        begin(up,true);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,views);
        CHECK(module.Draw_Sky(*frame,scene));auto hdr=read(*frame,scene);alpha(hdr,true);
        uint16_t greenBits=0;if(hdr.size()>size_t(w+1)*8+4)std::memcpy(&greenBits,hdr.data()+size_t(w+1)*8+2,2);CHECK(greenBits==half(2));
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));alpha(read(*frame,scene),true);
        CHECK(module.Apply_Atmosphere(*frame,scene,in));alpha(read(*frame,scene),true);end();
        atmosphere.Tint[0]=atmosphere.Tint[1]=atmosphere.Tint[2]=1;
        // Warm both output formats, then repeat without growing module resources.
        begin(up);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,views);
        CHECK(module.Draw_Sky(*frame,scene));CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(module.Apply_Atmosphere(*frame,scene,in));end();
        const auto warm=module.Resources();
        for(unsigned n=0;n<4;++n) {
            begin(up,n&1);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,views);
            CHECK(module.Draw_Sky(*frame,scene));CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(module.Apply_Atmosphere(*frame,scene,in));end();
        }
        CHECK(module.Resources().Creations==warm.Creations);CHECK(module.Resources().Textures==2);
        // A same-frame/target map reset with absent views must not reuse old sky/shroud borrows.
        const auto savedViews=views;views.Shroud={black,201,1};begin(up);
        CHECK(module.Draw_Sky(*frame,scene));
        SdlPostAtmosphereViews nextMap;nextMap.MapIdentity=2;nextMap.MapGeneration=1;
        CHECK(module.Prepare(*frame,post,scene,main_identity(*frame,scene),up,haze,atmosphere,nextMap));
        CHECK(module.Draw_Sky(*frame,scene));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);
        depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,nextMap);
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(component(read(*frame,scene),w,w/2,h/2)>16);end();
        views=savedViews;
        // Direct user-off routing remains direct. Its caller explicitly supplies neutral COPY
        // ownership for independent effects, with gain one and no implicit HDR/bloom.
        presentation=frame->Back_Buffer_Target();SdlPostSettings userOff;
        CHECK(post.Prepare(*frame,presentation,userOff,scene));CHECK(!post.Active());
        CHECK(!module.Prepare(*frame,post,scene,main_identity(*frame,scene),up,haze,atmosphere,views));
        CHECK(post.Prepare(*frame,presentation,post_settings(),scene));fill(*frame,scene);
        CHECK(!post.Scene_Is_Float() && post.Additive_Gain()==1 && !post.Replaces_Legacy_Bloom());
        CHECK(module.Prepare(*frame,post,scene,main_identity(*frame,scene),up,haze,atmosphere,views));
        CHECK(module.Draw_Sky(*frame,scene));end();
        // Untagged/stale borrowed views refuse before recording and remain retryable.
        begin(up);auto invalidViews=views;invalidViews.Sky.Generation=0;
        CHECK(!module.Prepare(*frame,post,scene,main_identity(*frame,scene),up,haze,atmosphere,invalidViews));
        CHECK(!module.Draw_Sky(*frame,scene));CHECK(component(read(*frame,scene),w,w/2,h/2)==0);
        CHECK(module.Prepare(*frame,post,scene,main_identity(*frame,scene),up,haze,atmosphere,views));
        CHECK(module.Draw_Sky(*frame,scene));end();
        // A changed map epoch and resized main identity cannot inherit prior completion flags.
        const auto old=in;views.MapGeneration++;CHECK(frame->Resize(41,29));w=41;h=29;
        frame->Release_After_Batch(sampled,nullptr);sampled=texture(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
        begin(up);depth(*frame,sampled,w,h,.25f,.25f);in=capture(*frame,scene,sampled,up,views);
        CHECK(!module.Apply_Ground_Haze(*frame,scene,old));CHECK(module.Draw_Sky(*frame,scene));
        CHECK(module.Apply_Ground_Haze(*frame,scene,in));CHECK(module.Apply_Atmosphere(*frame,scene,in));end();
        CHECK(module.Resources().Creations==warm.Creations);
        // Borrowed sky replacement is copied and may retire only after its recorded borrower.
        auto *replacement=texture(*frame,1,1,SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);const uint32_t cyan=0xff00ffff;upload(*frame,replacement,1,1,&cyan,4);
        begin(up);views.Sky={replacement,101,2};CHECK(module.Draw_Sky(*frame,scene));
        frame->Release_After_Batch(sky,nullptr);sky=nullptr;pixels=read(*frame,scene);CHECK(component(pixels,w,1,1,0)==0 && component(pixels,w,1,1,1)==255);end();
        begin(up);CHECK(module.Draw_Sky(*frame,scene));pixels=read(*frame,scene);CHECK(component(pixels,w,1,1,1)==255 && component(pixels,w,1,1,2)==255);end();
        CHECK(module.Release(*frame));CHECK(post.Release(*frame));const auto released=module.Resources();
        CHECK(!released.Textures && !released.Shaders && !released.Pipelines && !released.Samplers);
        frame->Release_After_Batch(sampled,nullptr);frame->Release_After_Batch(replacement,nullptr);frame->Release_After_Batch(fog,nullptr);frame->Release_After_Batch(black,nullptr);CHECK(frame->Flush());
    }
    delete frame;SDL_Quit();return failures?1:0;
}
int main(int argc,char **argv)
{
    cpu();if(failures)return 1;
    const int result=argc>1&&!std::strcmp(argv[1],"--gpu")?gpu():0;
    std::printf("SDL atmosphere/haze: %u checks, %u failures\n",checks,failures);return result;
}
