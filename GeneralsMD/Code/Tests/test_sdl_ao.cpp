// Explicit independent depth fixtures; this does not claim actual water/particle hook integration.
#include "SdlPostProcess.h"
#include "SdlPostShader.h"
#include "SdlSceneCapture.h"
#include "SdlRenderMatrix.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %u: %s\n",__LINE__,#x); } } while(0)

static void cpu()
{
    // The main viewport excludes HUD pixels; reconstruction must use the full
    // attachment's NDC without shifting AO to that viewport's local coordinates.
    float identity[16]={},fromTexture[16],scale[2];
    for(unsigned i=0;i<4;++i) identity[i*5]=1;
    const float viewport[]={4,2,8,4,.2f,.8f};
    CHECK(render_full_texture_to_world(identity,identity,16,8,viewport,fromTexture,scale));
    const float full[2][4]={{-.5f,.5f,.2f,1},{.5f,-.5f,.8f,1}};
    const float camera[2][4]={{-1,1,0,1},{1,-1,1,1}};
    for(unsigned i=0;i<2;++i)for(unsigned c=0;c<4;++c) {
        float value=0;for(unsigned j=0;j<4;++j)value+=full[i][j]*fromTexture[j*4+c];
        CHECK(std::abs(value-camera[i][c])<.00001f);
    }
    CHECK(scale[0]==.5f && scale[1]==.5f);
    CHECK(!render_full_texture_to_world(identity,identity,0,8,viewport,fromTexture,scale));
    float singular[16]={}; CHECK(!render_full_texture_to_world(identity,singular,16,8,viewport,fromTexture,scale));
    const PostShaderProgram programs[] = {POST_SHADER_AO,POST_SHADER_AO_SCENE,POST_SHADER_WORLD_AO,
        POST_SHADER_WORLD_AO_BLUR,POST_SHADER_WORLD_AO_UPSAMPLE};
    for (auto id : programs) {
        std::string source, error, msl; std::vector<unsigned char> spirv;
        CHECK(Sdl_Post_Pixel_Source(id,source));
        CHECK(source.find("register(b0, space3)") != std::string::npos);
        CHECK(source.find("register(t1, space2)") != std::string::npos);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error));
        unsigned samplers = 0, uniforms = 0; SDL3_Shader_Slots(spirv,false,samplers,uniforms);
        const bool world = id == POST_SHADER_WORLD_AO || id == POST_SHADER_WORLD_AO_BLUR || id == POST_SHADER_WORLD_AO_UPSAMPLE;
        CHECK(samplers == (world ? 3u : 2u)); CHECK(uniforms == (world ? 2u : 1u));
        if (world) {
            CHECK(source.find("register(b1, space3)") != std::string::npos);
            CHECK(source.find("WorldFromClip : packoffset(c15)") != std::string::npos);
            CHECK(source.find("WorldAoProjection : packoffset(c21)") != std::string::npos);
        }
        if (id == POST_SHADER_WORLD_AO) {
            CHECK(source.find("register(s2, space2)") != std::string::npos);
            CHECK(source.find("WorldAoShroud.SampleLevel(WorldAoShroud_Sampler,") != std::string::npos);
        }
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error)); CHECK(!msl.empty());
    }
    CHECK(sizeof(PostProcessConstants) == 352);
}
static SdlPostSettings settings(const char *chain)
{
    SdlPostSettings value; CHECK(DX11Post_Parse_Chain(chain,value.Chain,value.Count));
    value.BloomIntensity = 0; return value;
}
static uint16_t half(float value)
{
    uint32_t bits; std::memcpy(&bits,&value,4);
    if (value == 0) return 0;
    return uint16_t(((int(bits>>23&255)-127+15)<<10) | (((bits&0x7fffff)+0x1000)>>13));
}
static SDL_GPUTexture *image(SdlGpuFrame &frame,unsigned w,unsigned h,SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo info = {}; info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;
    info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width=w;info.height=h;info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *result=SDL_CreateGPUTexture(frame.Device(),&info);CHECK(result);return result;
}
static void upload(SdlGpuFrame &frame,SDL_GPUTexture *texture,unsigned w,unsigned h,const void *data,unsigned size)
{
    uint32_t offset=0;uint8_t *bytes=frame.Upload_Space(size,offset);CHECK(bytes);
    if(bytes) {std::memcpy(bytes,data,size);frame.Queue_Texture_Upload(texture,0,w,h,offset);}
}
static void fill_scene(SdlGpuFrame &frame,const SdlTarget &scene,float level=.6f)
{
    if(SdlGpuFrame::Target_Colour_Format(scene)==SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
        std::vector<uint16_t> pixels(size_t(scene.Width)*scene.Height*4);
        for(size_t i=0;i<pixels.size();i+=4) {pixels[i]=pixels[i+1]=pixels[i+2]=half(level);pixels[i+3]=half(.375f);}
        upload(frame,scene.Colour,scene.Width,scene.Height,pixels.data(),unsigned(pixels.size()*2));
    } else {
        std::vector<uint8_t> pixels(size_t(scene.Width)*scene.Height*4,uint8_t(std::lround(level*255)));
        for(size_t i=3;i<pixels.size();i+=4)pixels[i]=96;
        upload(frame,scene.Colour,scene.Width,scene.Height,pixels.data(),unsigned(pixels.size()));
    }
}
static void depth(SdlGpuFrame &frame,SDL_GPUTexture *texture,unsigned w,unsigned h,float left,float right)
{
    std::vector<float> pixels(size_t(w)*h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)pixels[size_t(y)*w+x]=x<w/2?left:right;
    upload(frame,texture,w,h,pixels.data(),unsigned(pixels.size()*4));
}
static SdlPostSceneInputs inputs(SdlGpuFrame &frame,const SdlTarget &scene,SDL_GPUTexture *sampled,SdlSceneCapturePhase phase)
{
    SdlPostSceneInputs in;in.Capture.Source=scene;in.Capture.Frame=frame.Scene_Frame();
    in.Capture.Generation=frame.Scene_Generation();in.Capture.Phase=phase;in.Capture.Depth=true;
    in.SampledDepth=sampled;in.DepthWidth=scene.Width;in.DepthHeight=scene.Height;in.DepthFormat=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    in.WorldFromClip[0]=in.WorldFromClip[5]=40;in.WorldFromClip[10]=1000;
    in.ProjectionScale[0]=in.ProjectionScale[1]=.025f;in.Near=1;in.Far=100;
    return in;
}
static std::vector<uint8_t> read(SdlGpuFrame &frame,const SdlTarget &target)
{
    std::vector<uint8_t> result;
    if(SdlGpuFrame::Target_Colour_Format(target)!=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
        CHECK(frame.Read_Back(target.Colour,target.Width,target.Height,result));return result;
    }
    // Read_Back's public contract is BGRA8. This fixture downloads HDR explicitly at eight
    // bytes per texel, with its own transfer/fence, rather than treating it as an eight-bit image.
    CHECK(frame.Flush());
    const unsigned size=target.Width*target.Height*8;
    SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=size;
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(frame.Device(),&info);CHECK(transfer);
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame.Device());CHECK(commands);
    if(!transfer || !commands)return result;
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
    SDL_GPUTextureRegion source={};source.texture=target.Colour;source.w=target.Width;source.h=target.Height;source.d=1;
    SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;
    destination.pixels_per_row=target.Width;destination.rows_per_layer=target.Height;
    SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
    if(fence) {
        CHECK(SDL_WaitForGPUFences(frame.Device(),true,&fence,1));
        const void *mapped=SDL_MapGPUTransferBuffer(frame.Device(),transfer,false);CHECK(mapped);
        if(mapped) {result.resize(size);std::memcpy(result.data(),mapped,size);SDL_UnmapGPUTransferBuffer(frame.Device(),transfer);}
        SDL_ReleaseGPUFence(frame.Device(),fence);
    }
    SDL_ReleaseGPUTransferBuffer(frame.Device(),transfer);return result;
}
static unsigned red(const std::vector<uint8_t> &pixels,unsigned w,unsigned x,unsigned y)
{
    size_t at=(size_t(y)*w+x)*4+2;return at<pixels.size()?pixels[at]:999;
}
static void alpha_preserved(const std::vector<uint8_t> &before,const std::vector<uint8_t> &after,bool hdr)
{
    CHECK(before.size()==after.size());if(before.size()!=after.size())return;
    for(size_t i=hdr?6:3;i<before.size();i+=hdr?8:4) {
        CHECK(before[i]==after[i]);if(hdr)CHECK(before[i+1]==after[i+1]);
    }
}
static int gpu()
{
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP SDL: %s\n",SDL_GetError());return 77;}
    std::string error;SdlGpuFrame *frame=SdlGpuFrame::Create(nullptr,65,49,error);
    if(!frame){std::printf("SKIP GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    const auto props=SDL_GetGPUDeviceProperties(frame->Device());
    std::printf("GPU: %s; driver %s\n",SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),SDL_GetGPUDeviceDriver(frame->Device()));
    {
        SdlPostProcess post;unsigned w=65,h=49;
        SDL_GPUTexture *early=image(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
        SDL_GPUTexture *late=image(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
        SDL_GPUTexture *black=image(*frame,1,1,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        const uint32_t zero=0;upload(*frame,black,1,1,&zero,4);
        SdlTarget scene;auto presentation=frame->Back_Buffer_Target();
        auto prepare=[&](const SdlPostSettings &s){presentation=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,presentation,s,scene));fill_scene(*frame,scene);};
        auto finish=[&](const SdlPostSceneInputs *finalDepth=nullptr){CHECK(post.Finish(*frame,scene,presentation,finalDepth));auto pixels=read(*frame,presentation);CHECK(frame->Present(nullptr));return pixels;};
        prepare(settings("copy"));depth(*frame,early,w,h,.5f,.5f);
        auto opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        const auto plain=read(*frame,scene);
        CHECK(post.Apply_World_AO(*frame,scene,opaque));CHECK(!post.Apply_World_AO(*frame,scene,opaque));
        auto flat=read(*frame,scene);CHECK(plain==flat);finish();

        // Same attachment/frame may serve distinct authoritative main views.
        // A new view must reject old captures and must not inherit prior AO completion.
        auto keyed=settings("copy");keyed.MainViewKey=17;prepare(keyed);
        opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        CHECK(!post.Apply_World_AO(*frame,scene,opaque));opaque.Capture.ViewKey=17;
        CHECK(post.Apply_World_AO(*frame,scene,opaque));
        keyed.MainViewKey=18;prepare(keyed);CHECK(!post.Apply_World_AO(*frame,scene,opaque));
        opaque.Capture.ViewKey=18;CHECK(post.Apply_World_AO(*frame,scene,opaque));finish();

        prepare(settings("ao"));depth(*frame,early,w,h,.49f,.5f);opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        frame->Set_Target(scene);frame->Clear_Back_Buffer(false,true,false,0,.375f,0);
        std::vector<float> depthBefore;CHECK(frame->Read_Depth(scene.Depth,w,h,depthBefore));
        fill_scene(*frame,scene,1.0f); // the original DX11 contact fixture measures darkening from white
        const auto before=read(*frame,scene);CHECK(post.Needs_BeforePost_Depth());
        CHECK(post.Apply_World_AO(*frame,scene,opaque));CHECK(!post.Needs_BeforePost_Depth());
        auto contact=read(*frame,scene);alpha_preserved(before,contact,false);
        std::printf("world AO contact: before=%u contact=%u top=%u far=%u\n",red(before,w,w/2+2,h/2),
            red(contact,w,w/2+2,h/2),red(contact,w,2,h/2),red(contact,w,w-3,h/2));
        CHECK(red(contact,w,w/2+2,h/2)<red(before,w,w/2+2,h/2)-10);
        CHECK(red(contact,w,2,h/2)==255 && red(contact,w,w-3,h/2)==255);
        auto transparent=inputs(*frame,scene,early,SdlSceneCapturePhase::BeforeTransparent);
        CHECK(!post.Apply_Chain_AO(*frame,scene,transparent));
        std::vector<float> depthAfter;CHECK(frame->Read_Depth(scene.Depth,w,h,depthAfter));CHECK(depthBefore==depthAfter);
        depth(*frame,late,w,h,1,1);auto beforePost=inputs(*frame,scene,late,SdlSceneCapturePhase::BeforePost);
        CHECK(finish(&beforePost)==contact); // successful early AO skips every final AO entry
        CHECK(!post.Apply_World_AO(*frame,scene,opaque));

        prepare(settings("copy"));depth(*frame,early,w,h,.49f,.5f);opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);opaque.Shroud=black;
        CHECK(post.Apply_World_AO(*frame,scene,opaque));CHECK(read(*frame,scene)==plain);finish();

        prepare(settings("ao,ao"));depth(*frame,early,w,h,.4f,.5f);
        transparent=inputs(*frame,scene,early,SdlSceneCapturePhase::BeforeTransparent);
        CHECK(post.Apply_Chain_AO(*frame,scene,transparent));CHECK(!post.Apply_Chain_AO(*frame,scene,transparent));
        const auto earlyChain=read(*frame,scene);CHECK(red(earlyChain,w,w/2+2,h/2)<150);
        for(size_t i=3;i<earlyChain.size();i+=4)CHECK(earlyChain[i]==96);
        beforePost=inputs(*frame,scene,late,SdlSceneCapturePhase::BeforePost);
        CHECK(finish(&beforePost)==earlyChain);

        // No early success: the final filter must use NEW BeforePost depth, not the earlier flat image.
        prepare(settings("ao"));depth(*frame,early,w,h,1,1);depth(*frame,late,w,h,.4f,.5f);
        beforePost=inputs(*frame,scene,late,SdlSceneCapturePhase::BeforePost);CHECK(post.Needs_BeforePost_Depth());
        const auto finalOne=finish(&beforePost);CHECK(red(finalOne,w,w/2+2,h/2)<150);
        for(size_t i=3;i<finalOne.size();i+=4)CHECK(finalOne[i]==96); // incoming source alpha, not stale presentation alpha
        prepare(settings("ao,ao"));depth(*frame,late,w,h,.4f,.5f);beforePost=inputs(*frame,scene,late,SdlSceneCapturePhase::BeforePost);
        const auto finalTwice=finish(&beforePost);CHECK(red(finalTwice,w,w/2+2,h/2)<red(finalOne,w,w/2+2,h/2));

        // AO-only routes still present with missing or stale final capture, without inventing success.
        prepare(settings("ao"));const auto missing=finish();CHECK(missing==plain);CHECK(!post.Error().empty());
        prepare(settings("ao"));beforePost=inputs(*frame,scene,late,SdlSceneCapturePhase::BeforePost);beforePost.Capture.Frame--;
        CHECK(finish(&beforePost)==plain);
        prepare(settings("copy"));opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        std::vector<SdlPostSceneInputs> invalid;
        auto bad=opaque;bad.Capture.Frame++;invalid.push_back(bad);
        bad=opaque;bad.Capture.Generation++;invalid.push_back(bad);
        bad=opaque;bad.Capture.Source.Colour=presentation.Colour;invalid.push_back(bad);
        bad=opaque;bad.Capture.Source.Depth=nullptr;invalid.push_back(bad);
        bad=opaque;bad.Capture.Phase=SdlSceneCapturePhase::BeforeTransparent;invalid.push_back(bad);
        bad=opaque;bad.Capture.Depth=false;invalid.push_back(bad);
        bad=opaque;bad.DepthWidth++;invalid.push_back(bad);
        bad=opaque;bad.DepthFormat=SDL_GPU_TEXTUREFORMAT_R16_FLOAT;invalid.push_back(bad);
        bad=opaque;bad.ActualSamples=4;invalid.push_back(bad);
        bad=opaque;bad.SampledDepth=scene.Depth;invalid.push_back(bad);
        bad=opaque;bad.Shroud=scene.Colour;invalid.push_back(bad);
        bad=opaque;bad.Near=bad.Far;invalid.push_back(bad);
        for(const auto &i:invalid)CHECK(!post.Apply_World_AO(*frame,scene,i));
        CHECK(!post.Apply_Chain_AO(*frame,scene,transparent)); // no AO membership in copy chain
        CHECK(read(*frame,scene)==plain);finish();

        using Failure=SdlPostOcclusion::TestFailure;
        for(auto point:{Failure::Program,Failure::Pipeline,Failure::Target,Failure::TargetAfterOne}) {
            SdlPostProcess recovery;SdlTarget recoveredScene;
            CHECK(recovery.Prepare(*frame,presentation,settings("copy"),recoveredScene));fill_scene(*frame,recoveredScene);
            depth(*frame,early,w,h,.49f,.5f);auto recoveredInputs=inputs(*frame,recoveredScene,early,SdlSceneCapturePhase::OpaqueScene);
            recovery.Set_AO_Failure_For_Test(point);CHECK(!recovery.Apply_World_AO(*frame,recoveredScene,recoveredInputs));
            CHECK(read(*frame,recoveredScene)==plain);
            // The injected fault belongs to recovery alone. Another module must still allocate
            // and draw while it is active, without advancing the original capture's frame.
            {
                SdlPostProcess independent;SdlTarget otherScene;
                CHECK(independent.Prepare(*frame,presentation,settings("copy"),otherScene));fill_scene(*frame,otherScene);
                auto otherInputs=inputs(*frame,otherScene,early,SdlSceneCapturePhase::OpaqueScene);
                CHECK(independent.Apply_World_AO(*frame,otherScene,otherInputs));
                CHECK(independent.Finish(*frame,otherScene,presentation));CHECK(independent.Release(*frame));
            }
            recovery.Set_AO_Failure_For_Test(Failure::None);
            CHECK(recovery.Apply_World_AO(*frame,recoveredScene,recoveredInputs));CHECK(!recovery.Apply_World_AO(*frame,recoveredScene,recoveredInputs));
            CHECK(recovery.Finish(*frame,recoveredScene,presentation));CHECK(frame->Present(nullptr));CHECK(recovery.Release(*frame));
        }
        // HDR alpha is preserved as the exact half bits, and post effects retain their base contracts.
        prepare(settings("bloom,ao"));depth(*frame,early,w,h,.49f,.5f);opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        const auto hdrBefore=read(*frame,scene);CHECK(post.Apply_World_AO(*frame,scene,opaque));
        alpha_preserved(hdrBefore,read(*frame,scene),true);finish();
        prepare(settings("bloom,ao"));depth(*frame,early,w,h,.4f,.5f);transparent=inputs(*frame,scene,early,SdlSceneCapturePhase::BeforeTransparent);
        CHECK(post.Apply_Chain_AO(*frame,scene,transparent));finish();
        const auto warm=post.Resources();
        for(unsigned n=0;n<5;++n) {
            prepare(settings("ao"));depth(*frame,early,w,h,.49f,.5f);opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
            CHECK(post.Apply_World_AO(*frame,scene,opaque));finish();
            prepare(settings("ao"));depth(*frame,early,w,h,.4f,.5f);transparent=inputs(*frame,scene,early,SdlSceneCapturePhase::BeforeTransparent);
            CHECK(post.Apply_Chain_AO(*frame,scene,transparent));finish();
        }
        CHECK(post.Resources().Creations==warm.Creations);CHECK(post.Resources().Textures==warm.Textures);
        // Replacement dimensions cannot reuse an old capture identity or old AO intermediate sizes.
        const auto old=opaque;CHECK(frame->Resize(81,63));w=81;h=63;
        frame->Release_After_Batch(early,nullptr);frame->Release_After_Batch(late,nullptr);
        early=image(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);late=image(*frame,w,h,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
        prepare(settings("ao"));CHECK(!post.Apply_World_AO(*frame,scene,old));
        depth(*frame,early,w,h,.49f,.5f);opaque=inputs(*frame,scene,early,SdlSceneCapturePhase::OpaqueScene);
        CHECK(post.Apply_World_AO(*frame,scene,opaque));finish();
        prepare(settings("off"));CHECK(!post.Apply_World_AO(*frame,scene,opaque));CHECK(!post.Apply_Chain_AO(*frame,scene,opaque));
        // Active first consumer: capture depth written to the real scene attachment,
        // rather than uploading a precomputed sampled-depth fixture.
        {
            auto owned=settings("copy"); owned.MainViewKey=701;
            prepare(owned);
            frame->Set_Target(scene);
            frame->Clear_Back_Buffer(false,true,false,0,.5f,0);
            frame->Clear_Rect(0,0,int(w/2),int(h),false,true,false,0,.49f,0);
            fill_scene(*frame,scene,1.0f);
            const auto unccluded=read(*frame,scene);
            SdlSceneCaptureCache captures(*frame);
            SdlSceneCaptureRequest request;
            request.Source=scene; request.Frame=frame->Scene_Frame(); request.Generation=frame->Scene_Generation();
            request.ViewKey=701; request.Depth=true;
            SdlSceneDepthCapture opaqueCapture,reused,transparentCapture,empty;
            CHECK(captures.Capture(request,opaqueCapture));
            CHECK(opaqueCapture.Texture!=scene.Depth && opaqueCapture.Texture!=scene.Colour);
            CHECK(captures.Capture(request,reused)); CHECK(reused.Texture==opaqueCapture.Texture);
            auto sampled=inputs(*frame,scene,opaqueCapture.Texture,SdlSceneCapturePhase::OpaqueScene);
            sampled.Capture=opaqueCapture.Identity;
            auto stale=sampled; stale.Capture.ViewKey++;
            CHECK(!post.Apply_World_AO(*frame,scene,stale)); CHECK(read(*frame,scene)==unccluded);
            stale=sampled; stale.Capture.Phase=SdlSceneCapturePhase::BeforeTransparent;
            CHECK(!post.Apply_World_AO(*frame,scene,stale)); CHECK(read(*frame,scene)==unccluded);
            CHECK(post.Apply_World_AO(*frame,scene,sampled));
            auto active=read(*frame,scene);
            CHECK(red(active,w,w/2+2,h/2)<red(unccluded,w,w/2+2,h/2)-10);
            alpha_preserved(unccluded,active,false);
            // Later geometry/depth cannot overwrite the immutable opaque-phase evidence.
            frame->Clear_Back_Buffer(false,true,false,0,.75f,0);
            request.Phase=SdlSceneCapturePhase::BeforeTransparent;
            CHECK(captures.Capture(request,transparentCapture)); CHECK(transparentCapture.Texture!=opaqueCapture.Texture);
            const auto sampleDepth=[&](const SdlSceneDepthCapture &capture,unsigned x) {
                std::vector<uint8_t> bytes;
                CHECK(frame->Read_Back(capture.Texture,w,h,bytes)); float value=-1;
                if(bytes.size()==size_t(w)*h*4) std::memcpy(&value,bytes.data()+(size_t(h/2)*w+x)*4,4);
                return value;
            };
            CHECK(std::abs(sampleDepth(opaqueCapture,w/4)-.49f)<.000001f);
            CHECK(std::abs(sampleDepth(transparentCapture,w/4)-.75f)<.000001f);
            CHECK(finish()==active);
            request.Frame--;
            CHECK(!captures.Capture(request,empty)); CHECK(!empty.Texture && !empty.Width);
            request.Frame=frame->Scene_Frame(); request.Generation++;
            CHECK(!captures.Capture(request,empty)); CHECK(!empty.Texture);
        }
        CHECK(post.Release(*frame));CHECK(post.Resources().Textures==0 && post.Resources().Shaders==0);
        frame->Release_After_Batch(early,nullptr);frame->Release_After_Batch(late,nullptr);frame->Release_After_Batch(black,nullptr);
        CHECK(frame->Flush());
    }
    delete frame;SDL_Quit();return failures?1:0;
}
int main(int argc,char **argv)
{
    cpu();if(failures)return 1;
    const int result=argc>1&&!std::strcmp(argv[1],"--gpu")?gpu():0;
    std::printf("SDL native AO: %u checks, %u failures\n",checks,failures);return result;
}
