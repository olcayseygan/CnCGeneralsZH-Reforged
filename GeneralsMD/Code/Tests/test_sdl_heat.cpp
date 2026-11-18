// The shared heat formulas run through instance records, half-float splats and scene
// composite. No engine capture, water snapshot or particle producer is mocked in.
#include "SdlPostDistortion.h"
#include "sdl3shadercompile.h"
#include "dx11runtime.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
using Splat=std::array<float,DIRECT3D11_HEAT_SPLAT_FLOATS>;
static void cpu()
{
    std::string source,error,msl;std::vector<unsigned char> spirv;
    CHECK(Sdl_Post_Heat_Vertex_Source(source));CHECK(source.find("HeatRecord record")!=std::string::npos);
    CHECK(source.find("Splats.Load")==std::string::npos);
    CHECK(SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,error));
    unsigned vertexSamplers=0,vertexUniforms=0;SDL3_Shader_Slots(spirv,true,vertexSamplers,vertexUniforms);
    CHECK(vertexSamplers==0 && vertexUniforms==0);
    CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,true,msl,error));CHECK(!msl.empty());
    for(auto id:{POST_SHADER_HEAT_SPLAT,POST_SHADER_HEAT_HAZE}) {
        CHECK(Sdl_Post_Distortion_Pixel_Source(id,source));
        std::string body=PostShader_Pixel_Body(id);
        const std::string from="Extra.Sample(Sampler,";size_t at=0;
        while((at=body.find(from,at))!=std::string::npos){body.replace(at,from.size(),"Extra.Sample(Extra_Sampler,");at+=27;}
        const auto start=body.find(id==POST_SHADER_HEAT_SPLAT ? "static const float HEAT_FREQUENCY" : "{");
        CHECK(source.find(body.substr(start))!=std::string::npos);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error));
        unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);
        CHECK(samplers==2 && uniforms==1);CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error));
    }
    CHECK(DIRECT3D11_HEAT_SPLAT_LIMIT==2048 && sizeof(Splat)==64);
}
static float half_value(uint16_t value)
{
    const unsigned exponent=value>>10&31,mantissa=value&1023;
    const float magnitude=exponent ? std::ldexp(1.0f+mantissa/1024.0f,int(exponent)-15) : std::ldexp(float(mantissa),-24);
    return value&32768 ? -magnitude : magnitude;
}
static uint16_t half_bits(float value)
{
    if(value==0)return 0;
    uint32_t bits;std::memcpy(&bits,&value,4);const unsigned sign=bits>>16&0x8000;
    const int exponent=int(bits>>23&255)-127+15;
    if(exponent<=0)return uint16_t(sign|unsigned(std::lround(std::ldexp(std::fabs(value),24))));
    return uint16_t(sign|((unsigned(exponent)<<10)+(((bits&0x7fffff)+0x1000)>>13)));
}
static std::vector<float> pattern(unsigned w,unsigned h,bool hdr)
{
    std::vector<float> p(size_t(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const auto i=(size_t(y)*w+x)*4;
        p[i]=((x*9+y*3)%256)/255.0f;p[i+1]=std::floor(y*255.0f/(h-1))/255;
        p[i+2]=std::floor(x*255.0f/(w-1))/255;p[i+3]=((x+3*y)%192+32)/255.0f;
        if(hdr)for(unsigned c=0;c<3;++c)p[i+c]*=2;
    }
    return p;
}
static void upload(SdlGpuFrame &frame,SDL_GPUTexture *texture,unsigned w,unsigned h,const void *data,unsigned bytes)
{
    uint32_t offset=0;auto *mapped=frame.Upload_Space(bytes,offset);CHECK(mapped);
    if(mapped){std::memcpy(mapped,data,bytes);frame.Queue_Texture_Upload(texture,0,w,h,offset);}
}
static void fill(SdlGpuFrame &frame,const SdlTarget &scene,const std::vector<float> &pixels)
{
    const bool hdr=SdlGpuFrame::Target_Colour_Format(scene)==SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if(hdr){std::vector<uint16_t> p;for(float f:pixels)p.push_back(half_bits(f));upload(frame,scene.Colour,scene.Width,scene.Height,p.data(),unsigned(p.size()*2));}
    else {std::vector<unsigned char> p;for(float f:pixels)p.push_back(unsigned(std::lround(f*255)));upload(frame,scene.Colour,scene.Width,scene.Height,p.data(),unsigned(p.size()));}
}
static std::vector<float> read(SdlGpuFrame &frame,const SdlTarget &scene)
{
    std::vector<float> result;
    if(SdlGpuFrame::Target_Colour_Format(scene)!=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
        std::vector<unsigned char> bytes;CHECK(frame.Read_Back(scene.Colour,scene.Width,scene.Height,bytes));
        for(auto b:bytes)result.push_back(b/255.0f);
        return result;
    }
    CHECK(frame.Flush());SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=scene.Width*scene.Height*8;
    auto *transfer=SDL_CreateGPUTransferBuffer(frame.Device(),&info);CHECK(transfer);if(!transfer)return result;
    auto *commands=SDL_AcquireGPUCommandBuffer(frame.Device());CHECK(commands);if(!commands){SDL_ReleaseGPUTransferBuffer(frame.Device(),transfer);return result;}
    auto *copy=SDL_BeginGPUCopyPass(commands);SDL_GPUTextureRegion source={};source.texture=scene.Colour;source.w=scene.Width;source.h=scene.Height;source.d=1;
    SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
    auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
    if(fence){CHECK(SDL_WaitForGPUFences(frame.Device(),true,&fence,1));auto *p=static_cast<uint16_t *>(SDL_MapGPUTransferBuffer(frame.Device(),transfer,false));CHECK(p);
        if(p){for(unsigned i=0;i<scene.Width*scene.Height*4;++i)result.push_back(half_value(p[i]));SDL_UnmapGPUTransferBuffer(frame.Device(),transfer);}SDL_ReleaseGPUFence(frame.Device(),fence);}
    SDL_ReleaseGPUTransferBuffer(frame.Device(),transfer);return result;
}
static std::array<double,4> sample(const std::vector<float> &image,unsigned w,unsigned h,double u,double v)
{
    const double x=std::clamp(u*w-.5,0.0,double(w-1)),y=std::clamp(v*h-.5,0.0,double(h-1));
    const unsigned ix=unsigned(x),iy=unsigned(y),jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);std::array<double,4> result;
    for(unsigned c=0;c<4;++c) {
        auto at=[&](unsigned px,unsigned py){return image[(size_t(py)*w+px)*4+c];};
        const double a=at(ix,iy)*(1-(x-ix))+at(jx,iy)*(x-ix),b=at(ix,jy)*(1-(x-ix))+at(jx,jy)*(x-ix);
        result[c]=a*(1-(y-iy))+b*(y-iy);
    }
    return result;
}
static std::vector<float> reference(const std::vector<float> &source,unsigned w,unsigned h,
    const std::vector<float> &depth,const std::vector<Splat> &splats)
{
    const unsigned hw=(w+1)/2,hh=(h+1)/2;const double aspect=double(w)/h;
    std::vector<float> field(size_t(hw)*hh*4,0);
    auto saturate=[](double v){return std::clamp(v,0.0,1.0);};
    for(unsigned y=0;y<hh;++y)for(unsigned x=0;x<hw;++x)for(const auto &a:splats) {
        if(a[2]==0 || a[3]==0)continue;
        const double u=(x+.5)/hw,v=(y+.5)/hh,cx=(u*2-1-a[0])/a[2],cy=(1-v*2-a[1])/a[3],reach=cx*cx+cy*cy;
        if(reach>=1)continue;
        double weight=std::pow(1-reach,3);
        auto linear=[&](double z){return a[8]*a[9]/std::max(a[9]-z*(a[9]-a[8]),.0001);};
        weight*=saturate((linear(depth[size_t(std::min(y*2+1,h-1))*w+std::min(x*2+1,w-1)])-linear(a[4])-a[10])/std::max(double(a[11]),.0001));
        if(a[12]*a[12]+a[13]*a[13]>0) {
            const double ax=a[12]*aspect,ay=a[13],length=std::hypot(ax,ay);
            const double off=std::fabs(cx*a[2]*aspect*.5*ay/length-cy*a[3]*.5*ax/length);
            weight*=saturate((off-std::max(double(a[14]),2.0/h))/std::max(double(a[15]),.0001));
        }
        if(weight<=0)continue;
        const double px=u*aspect*50,py=v*50,t=a[6]*2,rise=py+t*3+.8*std::sin(px*.35+t*.7);
        const double sx=std::sin(rise)+.3*std::sin(rise*1.9+px*.5-t*1.3),sy=.25*std::sin(rise*.6+px*.4-t*.9);
        const double values[]={sx*a[5]*weight,sy*a[5]*weight,weight,0};
        for(unsigned c=0;c<4;++c){auto &value=field[(size_t(y)*hw+x)*4+c];value=half_value(half_bits(float(value+values[c])));}
    }
    auto result=source;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const double u=(x+.5)/w,v=(y+.5)/h;const auto heat=sample(field,hw,hh,u,v);
        double sx=heat[0]/std::max(heat[2],1.0),sy=heat[1]/std::max(heat[2],1.0);
        const double touched=saturate(sample(field,hw,hh,u+sx,v+sy)[2]*4);
        const double settle=saturate(std::min(u,1-u)/.02)*saturate(std::min(v,1-v)/.02);
        sx*=touched*settle;sy*=touched*settle;const auto colour=sample(source,w,h,u+sx,v+sy);
        for(unsigned c=0;c<3;++c)result[(size_t(y)*w+x)*4+c]=float(colour[c]);
    }
    return result;
}
static void compare(const std::vector<float> &actual,const std::vector<float> &expected,bool hdr)
{
    CHECK(actual.size()==expected.size());if(actual.size()!=expected.size())return;
    unsigned bad=0;double maximum=0;
    for(size_t i=0;i<actual.size();++i) {
        ++checks;const double difference=std::fabs(actual[i]-expected[i]);maximum=std::max(maximum,difference);
        if(difference>(i%4==3 ? 0 : hdr ? .012 : 2.0/255)) {++failures;++bad;if(bad<=4)std::printf("heat pixel%zu c%zu actual%.6f expected%.6f\n",i/4,i%4,actual[i],expected[i]);}
    }
    std::printf("heat compare bad%u max%.6f\n",bad,maximum);
}
// Independent blast oracle over the heat result, exercising one shared module and
// the required heat -> completed normal post -> blast ordering in a single frame.
static std::vector<float> warped(const std::vector<float> &image,unsigned w,unsigned h,const DX11PostWarp &a)
{
    auto result=image;const double aspect=double(w)/h;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const double u=(x+.5)/w,v=(y+.5)/h,ax=(u-a.CentreX)*aspect,ay=v-a.CentreY,span=std::hypot(ax,ay);
        const double reach=std::clamp(span/std::max(double(a.Radius),.0001),0.0,1.0);
        const double inward=a.Pull*a.Radius*6.75*reach*(1-reach)*(1-reach);
        const double squash=ay>0?std::max(double(a.RingSquash),.05):1,dy=ay/squash;
        const double normal=std::max(std::hypot(ax,dy/squash),.0001);
        const double across=(std::hypot(ax,dy)-a.RingRadius)/std::max(double(a.RingWidth),.0001);
        const double push=a.RingStrength*a.Radius*std::exp(-across*across);
        const double sx=ax/std::max(span,.0001)*inward-ax/normal*push;
        const double sy=ay/std::max(span,.0001)*inward-dy/squash/normal*push;
        const double settle=std::clamp(std::min(u,1-u)/.08,0.0,1.0)*std::clamp(std::min(v,1-v)/.08,0.0,1.0);
        const auto colour=sample(image,w,h,u+sx/aspect*settle,v+sy*settle);
        for(unsigned c=0;c<3;++c)result[(size_t(y)*w+x)*4+c]=std::round(colour[c]*255)/255;
    }
    return result;
}

static SdlPostSettings settings(bool hdr=false)
{SdlPostSettings s;s.Count=1;s.Chain[0]=hdr?DX11_POST_BLOOM:DX11_POST_COPY;s.BloomIntensity=0;s.MainViewKey=31;return s;}
static SdlSceneCaptureRequest identity(SdlGpuFrame &frame,const SdlTarget &scene)
{SdlSceneCaptureRequest r;r.Source=scene;r.Frame=frame.Scene_Frame();r.Generation=frame.Scene_Generation();r.ViewKey=31;return r;}
static void gpu()
{
    std::string error;std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,65,49,error));CHECK(frame);if(!frame)return;
    std::printf("GPU: %s; driver %s\n",SDL_GetStringProperty(SDL_GetGPUDeviceProperties(frame->Device()),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),SDL_GetGPUDeviceDriver(frame->Device()));
    SdlPostProcess post;SdlPostDistortion module;SdlTarget scene,output;SDL_GPUTexture *sampled=nullptr;unsigned width=0,height=0;
    const Splat flame={.1f,-.05f,.7f,.65f,.3f,.035f,.37f,0,1,100,0,1,0,0,0,0};
    auto begin=[&](bool hdr=false,float left=1,float right=1) {
        output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,settings(hdr),scene));
        if(width!=scene.Width || height!=scene.Height) {
            if(sampled)frame->Release_After_Batch(sampled,nullptr);
            width=scene.Width;height=scene.Height;SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
            info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;sampled=SDL_CreateGPUTexture(frame->Device(),&info);CHECK(sampled);
        }
        fill(*frame,scene,pattern(width,height,hdr));std::vector<float> depth(size_t(width)*height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)depth[size_t(y)*width+x]=x<width/2?left:right;
        upload(*frame,sampled,width,height,depth.data(),unsigned(depth.size()*4));return depth;
    };
    auto inputs=[&] {SdlPostSceneInputs in;in.Capture=identity(*frame,scene);in.Capture.Phase=SdlSceneCapturePhase::BeforePost;in.Capture.Depth=true;
        in.SampledDepth=sampled;in.DepthWidth=width;in.DepthHeight=height;in.DepthFormat=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;return in;};
    auto end=[&] {CHECK(post.Finish(*frame,scene,output));CHECK(frame->Present(nullptr));};
    for(unsigned fixture=0;fixture<7;++fixture) {
        const bool hdr=fixture==6;auto depth=begin(hdr,fixture==2?.1f:1,1);auto before=read(*frame,scene);
        std::vector<Splat> records={flame};
        if(fixture==0)records[0][5]=0;
        if(fixture==3)records.push_back(flame);
        if(fixture==4){records[0][12]=1;records[0][13]=.4f;records[0][14]=.04f;records[0][15]=.03f;}
        if(fixture==5){records[0][0]=-.9f;records[0][2]=.8f;}
        const auto copied=records;
        CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),records[0].data(),unsigned(records.size())));
        records[0]={};CHECK(module.Apply_Heat(*frame,scene,inputs()));CHECK(!module.Apply_Heat(*frame,scene,inputs()));
        auto actual=read(*frame,scene);compare(actual,reference(before,width,height,depth,copied),hdr);
        if(fixture==0)CHECK(actual==before);else if(fixture==1)CHECK(actual!=before);
        end();
    }
    // Warm the final eight-bit configuration after the preceding HDR fixture.
    begin();CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));end();
    const auto warm=module.Resources();CHECK(warm.Buffers==1 && warm.BufferBytes==131072);
    for(unsigned i=0;i<4;++i){begin();CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));end();}
    CHECK(module.Resources().Creations==warm.Creations);
    {
        SdlPostDistortion combined;const auto depth=begin();const auto original=read(*frame,scene);
        const DX11PostWarp blast={.51f,.48f,.32f,.08f,.24f,.055f,.12f,.6f};
        CHECK(combined.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));
        CHECK(combined.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),&blast,1));
        CHECK(combined.Apply_Heat(*frame,scene,inputs()));const auto heated=read(*frame,scene);
        compare(heated,reference(original,width,height,depth,{flame}),false);
        CHECK(post.Finish(*frame,scene,output));CHECK(combined.Apply_Blast_Warp(*frame,scene,output));
        compare(read(*frame,output),warped(heated,width,height,blast),false);
        CHECK(combined.Resources().Textures==3 && combined.Resources().Buffers==1);
        CHECK(frame->Present(nullptr));CHECK(combined.Release(*frame));
    }
    // Refused attempts consume. A new explicit Prepare can recover, while a
    // replay without new records cannot change the scene.
    for(unsigned invalid=0;invalid<7;++invalid) {
        begin();auto before=read(*frame,scene);CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));auto bad=inputs();
        if(invalid==0)bad.Capture.Phase=SdlSceneCapturePhase::BeforeTransparent;
        if(invalid==1)++bad.Capture.ViewKey;
        if(invalid==2)++bad.Capture.Generation;
        if(invalid==3)++bad.Capture.Frame;
        if(invalid==4)bad.SampledDepth=scene.Colour;
        if(invalid==5)++bad.DepthWidth;
        if(invalid==6)bad.SampledDepth=nullptr;
        CHECK(!module.Apply_Heat(*frame,scene,bad));CHECK(!module.Apply_Heat(*frame,scene,inputs()));CHECK(read(*frame,scene)==before);
        CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));end();
    }
    begin();auto before=read(*frame,scene);CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));
    CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),nullptr,0));CHECK(!module.Apply_Heat(*frame,scene,inputs()));CHECK(read(*frame,scene)==before);end();
    for(auto point:{SdlPostDistortion::TestFailure::Program,SdlPostDistortion::TestFailure::Pipeline,SdlPostDistortion::TestFailure::Buffer,SdlPostDistortion::TestFailure::Target,SdlPostDistortion::TestFailure::TargetAfterOne,SdlPostDistortion::TestFailure::Record}) {
        SdlPostDistortion recovery;begin();before=read(*frame,scene);recovery.Set_Failure_For_Test(point);
        const bool prepared=recovery.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1);
        if(point==SdlPostDistortion::TestFailure::Record){CHECK(prepared);CHECK(!recovery.Apply_Heat(*frame,scene,inputs()));}else CHECK(!prepared);
        recovery.Set_Failure_For_Test(SdlPostDistortion::TestFailure::None);CHECK(!recovery.Apply_Heat(*frame,scene,inputs()));CHECK(read(*frame,scene)==before);
        CHECK(recovery.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(recovery.Apply_Heat(*frame,scene,inputs()));end();CHECK(recovery.Release(*frame));
    }
    begin();frame->Set_Target(scene);frame->Clear_Back_Buffer(false,true,false,0,.375f,0);std::vector<float> depthBefore,depthAfter;
    CHECK(frame->Read_Depth(scene.Depth,width,height,depthBefore));CHECK(!depthBefore.empty());if(!depthBefore.empty())CHECK(std::fabs(depthBefore[0]-.375f)<1e-5f);
    CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));
    CHECK(frame->Read_Depth(scene.Depth,width,height,depthAfter));CHECK(depthBefore==depthAfter);end();
    // All 2048 records fit one fixed instance buffer; producer overflow is dropped exactly
    // as upstream, and a dropped final nonzero record cannot create heat.
    begin();before=read(*frame,scene);std::vector<Splat> overflow(DIRECT3D11_HEAT_SPLAT_LIMIT+1);
    overflow.back()=flame;
    CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),overflow[0].data(),unsigned(overflow.size())));
    CHECK(module.Apply_Heat(*frame,scene,inputs()));CHECK(read(*frame,scene)==before);end();
    SdlPostSettings off;output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,off,scene));
    CHECK(!module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(!module.Apply_Heat(*frame,scene,inputs()));CHECK(frame->Present(nullptr));

    // A format replacement while old packets are still queued retires old
    // field/copy images only after those exact readers submit. Buffer contents
    // are similarly ordered by the instance staging upload before the new splat draw.
    const auto oldDepth=begin();const auto oldSource=read(*frame,scene);const auto oldScene=scene;
    CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));
    CHECK(frame->Has_Pending_Work());output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,settings(true),scene));
    const auto newDepth=oldDepth;const auto newSource=pattern(width,height,true);fill(*frame,scene,newSource);
    // Keep the old readonly depth content unchanged until its queued borrowers submit.
    CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(frame->Has_Pending_Work());
    CHECK(module.Apply_Heat(*frame,scene,inputs()));
    compare(read(*frame,oldScene),reference(oldSource,width,height,oldDepth,{flame}),false);
    // The stored HDR source is quantized to half before filtering.
    auto quantized=newSource;for(auto &v:quantized)v=half_value(half_bits(v));
    compare(read(*frame,scene),reference(quantized,width,height,newDepth,{flame}),true);end();

    CHECK(frame->Resize(43,31));begin();CHECK(module.Prepare_Heat(*frame,post,scene,identity(*frame,scene),flame.data(),1));CHECK(module.Apply_Heat(*frame,scene,inputs()));end();
    CHECK(module.Resources().Textures==2 && module.Resources().Buffers==1);
    CHECK(module.Release(*frame));CHECK(post.Release(*frame));frame->Release_After_Batch(sampled,nullptr);CHECK(frame->Flush());
}
int main(int argc,char **argv)
{
    cpu();if(argc>1 && !std::strcmp(argv[1],"--gpu")){if(!SDL_Init(SDL_INIT_VIDEO))return 77;gpu();SDL_Quit();}
    std::printf("SDL heat distortion: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
