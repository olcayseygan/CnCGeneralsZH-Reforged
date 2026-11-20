// Exact native blast-warp source, ownership/refusal and real GPU pixel fixtures.
#include "SdlPostDistortion.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %u: %s\n", __LINE__, #x); } } while (0)
static void cpu()
{
    std::string source, error, msl; std::vector<unsigned char> spirv;
    CHECK(Sdl_Post_Distortion_Pixel_Source(POST_SHADER_WARP, source));
    CHECK(source.find("register(t0, space2)") != std::string::npos);
    CHECK(source.find("register(b0, space3)") != std::string::npos);
    const std::string body=PostShader_Pixel_Body(POST_SHADER_WARP);
    CHECK(source.find(body.substr(body.find('{'))) != std::string::npos);
    CHECK(SDL3_Compile_HLSL_To_SPIRV(source, false, spirv, error));
    unsigned samplers = 0, uniforms = 0; SDL3_Shader_Slots(spirv, false, samplers, uniforms);
    CHECK(samplers == 2 && uniforms == 1);
    CHECK(SDL3_Translate_SPIRV_To_MSL(spirv, false, msl, error)); CHECK(!msl.empty());
    CHECK(!Sdl_Post_Distortion_Pixel_Source(POST_SHADER_COUNT, source));
    CHECK(sizeof(PostProcessConstants) == 352);
}
static SdlPostSettings settings(bool hdr = false)
{
    SdlPostSettings s; s.Count = 1; s.Chain[0] = hdr ? DX11_POST_BLOOM : DX11_POST_COPY;
    s.BloomIntensity = 0; s.MainViewKey = 7; return s;
}
static SdlSceneCaptureRequest identity(SdlGpuFrame &frame, const SdlTarget &scene)
{
    SdlSceneCaptureRequest result; result.Source = scene; result.Frame = frame.Scene_Frame();
    result.Generation = frame.Scene_Generation(); result.ViewKey = 7; return result;
}
static std::vector<unsigned char> pattern(unsigned w, unsigned h)
{
    std::vector<unsigned char> p(size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const auto i = (size_t(y) * w + x) * 4;
        p[i] = (x * 9 + y * 3) % 256; p[i+1] = y * 255 / (h-1);
        p[i+2] = x * 255 / (w-1); p[i+3] = (x + 3*y) % 192 + 32;
    }
    return p;
}
static void upload(SdlGpuFrame &frame, const SdlTarget &target, const std::vector<unsigned char> &p)
{
    uint32_t offset = 0; auto *bytes = frame.Upload_Space(unsigned(p.size()), offset); CHECK(bytes);
    if (bytes) { std::memcpy(bytes, p.data(), p.size()); frame.Queue_Texture_Upload(target.Colour, 0, target.Width, target.Height, offset); }
}
static std::vector<unsigned char> read(SdlGpuFrame &frame, const SdlTarget &target)
{
    std::vector<unsigned char> p; CHECK(frame.Read_Back(target.Colour, target.Width, target.Height, p)); return p;
}
// Independent numerical oracle at texel centres, including hardware bilinear
// sample addressing. Tests compare RGB and separately require alpha bit identity.
static std::vector<unsigned char> reference(const std::vector<unsigned char> &image, unsigned w, unsigned h,
    const std::vector<DX11PostWarp> &warps)
{
    auto result = image; const double aspect = double(w) / h;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const double u = (x+.5)/w, v = (y+.5)/h; double sx = 0, sy = 0;
        for (const auto &a : warps) {
            const double ax=(u-a.CentreX)*aspect, ay=v-a.CentreY, span=std::hypot(ax,ay);
            const double reach=std::clamp(span/std::max(double(a.Radius),.0001),0.0,1.0);
            const double inward=a.Pull*a.Radius*6.75*reach*(1-reach)*(1-reach);
            const double squash=ay>0 ? std::max(double(a.RingSquash),.05) : 1;
            const double dy=ay/squash, domeSpan=std::hypot(ax,dy), nx=ax, ny=dy/squash;
            const double normal=std::max(std::hypot(nx,ny),.0001);
            const double across=(domeSpan-a.RingRadius)/std::max(double(a.RingWidth),.0001);
            const double push=a.RingStrength*a.Radius*std::exp(-across*across);
            sx+=ax/std::max(span,.0001)*inward-nx/normal*push;
            sy+=ay/std::max(span,.0001)*inward-ny/normal*push;
        }
        const double settle=std::clamp(std::min(u,1-u)/.08,0.0,1.0)*std::clamp(std::min(v,1-v)/.08,0.0,1.0);
        const double fx=std::clamp((u+sx/aspect*settle)*w-.5,0.0,double(w-1));
        const double fy=std::clamp((v+sy*settle)*h-.5,0.0,double(h-1));
        const unsigned ix=unsigned(fx),iy=unsigned(fy),jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);
        for (unsigned c=0;c<3;++c) {
            auto at=[&](unsigned px,unsigned py){return image[(size_t(py)*w+px)*4+c];};
            const double top=at(ix,iy)*(1-(fx-ix))+at(jx,iy)*(fx-ix);
            const double bottom=at(ix,jy)*(1-(fx-ix))+at(jx,jy)*(fx-ix);
            result[(size_t(y)*w+x)*4+c]=unsigned(std::lround(top*(1-(fy-iy))+bottom*(fy-iy)));
        }
    }
    return result;
}
static void compare(const std::vector<unsigned char> &actual, const std::vector<unsigned char> &expected)
{
    CHECK(actual.size() == expected.size());
    if (actual.size() != expected.size()) return;
    static unsigned group=0; ++group;
    unsigned bad[4]={},maximum[4]={};
    for (size_t i=0;i<actual.size();++i) {
        ++checks;const unsigned difference=unsigned(std::abs(int(actual[i])-expected[i]));
        maximum[i%4]=std::max(maximum[i%4],difference);
        if(difference > (i%4==3 ? 0u : 2u)) {
            ++failures;++bad[i%4];
            if(bad[0]+bad[1]+bad[2]+bad[3]<=4)
                std::printf("pixel group%u index%zu channel%zu actual%u expected%u\n",group,i/4,i%4,actual[i],expected[i]);
        }
    }
    std::printf("compare group%u bad=%u,%u,%u,%u max=%u,%u,%u,%u\n",group,bad[0],bad[1],bad[2],bad[3],maximum[0],maximum[1],maximum[2],maximum[3]);
}
static void gpu()
{
    std::string error; std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,63,47,error));
    CHECK(frame); if (!frame) { std::printf("GPU: %s\n",error.c_str()); return; }
    std::printf("GPU: %s; driver %s\n", SDL_GetStringProperty(SDL_GetGPUDeviceProperties(frame->Device()),
        SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),SDL_GetGPUDeviceDriver(frame->Device()));
    SdlPostProcess post; SdlPostDistortion module; SdlTarget scene, output;
    DX11PostWarp a={.5f,.43f,.4f,.23f,.23f,.055f,.13f,.35f};
    auto begin=[&] {
        output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,settings(),scene));
        auto p=pattern(scene.Width,scene.Height);upload(*frame,scene,p);return p;
    };
    auto end=[&] {CHECK(frame->Present(nullptr));};
    auto p=begin(); auto key=identity(*frame,scene);
    CHECK(!post.Scene_Has_Finished(*frame,scene,output,key));
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,&a,1));
    CHECK(!module.Apply_Blast_Warp(*frame,scene,output)); // consumes an early/failed attempt
    CHECK(post.Finish(*frame,scene,output));CHECK(post.Scene_Has_Finished(*frame,scene,output,key));
    CHECK(!module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),p);end();

    for (auto warps : {std::vector<DX11PostWarp>{},std::vector<DX11PostWarp>{{.5f,.5f,.4f,0,0,.1f,0,1}},
        std::vector<DX11PostWarp>{a},std::vector<DX11PostWarp>{a,{-.1f,.65f,.9f,.1f,.3f,.08f,.05f,1}},
        std::vector<DX11PostWarp>{{.5f,.43f,.4f,.23f,.23f,.055f,.13f,1}}}) {
        p=begin();key=identity(*frame,scene);
        CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,warps.data(),unsigned(warps.size())));
        // Camera/producer arrays can change as soon as Prepare returns.
        CHECK(post.Finish(*frame,scene,output));
        if (!warps.empty()) {CHECK(module.Apply_Blast_Warp(*frame,scene,output));CHECK(!module.Apply_Blast_Warp(*frame,scene,output));}
        else CHECK(!module.Apply_Blast_Warp(*frame,scene,output));
        compare(read(*frame,output),reference(p,scene.Width,scene.Height,warps));end();
    }
    // Source data is copied, depth is never attached, and the colour snapshot
    // is taken after normal post rather than from an earlier opaque capture.
    p=begin();key=identity(*frame,scene);DX11PostWarp copied=a;
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,&copied,1));copied={};
    frame->Set_Target(scene);frame->Clear_Back_Buffer(false,true,false,0,.375f,0);
    std::vector<float> before,after;CHECK(frame->Read_Depth(scene.Depth,scene.Width,scene.Height,before));
    CHECK(!before.empty());if(!before.empty())CHECK(std::fabs(before[0]-.375f)<1e-5f);
    CHECK(post.Finish(*frame,scene,output));
    // Frame uploads precede recorded draws within a batch. Submit Finish before
    // replacing its result, so this fixture really changes the current output.
    CHECK(frame->Flush());p=pattern(scene.Width,scene.Height);std::reverse(p.begin(),p.end());upload(*frame,output,p);
    CHECK(module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),reference(p,scene.Width,scene.Height,{a}));
    CHECK(frame->Read_Depth(scene.Depth,scene.Width,scene.Height,after));CHECK(before==after);end();
    const auto warm=module.Resources();
    for(unsigned i=0;i<4;++i) {p=begin();CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),&a,1));CHECK(post.Finish(*frame,scene,output));CHECK(module.Apply_Blast_Warp(*frame,scene,output));end();}
    CHECK(module.Resources().Creations==warm.Creations);CHECK(module.Resources().Textures==1);

    p=begin();key=identity(*frame,scene);auto stale=key;++stale.ViewKey;
    CHECK(!module.Prepare_Blast_Warps(*frame,post,scene,output,stale,&a,1));
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,&a,1));CHECK(post.Copy_Through(*frame,scene,output));
    CHECK(!post.Scene_Has_Finished(*frame,scene,output,key));CHECK(!module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),p);end();

    p=begin();key=identity(*frame,scene);
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,&a,1));
    auto wrongOutput=output;wrongOutput.Width++;
    CHECK(!post.Finish(*frame,scene,wrongOutput));CHECK(!post.Scene_Has_Finished(*frame,scene,output,key));
    CHECK(!module.Apply_Blast_Warp(*frame,scene,output));CHECK(post.Finish(*frame,scene,output));
    CHECK(!module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),p);end();

    // Explicit user-off ownership does not manufacture a source scene for warps.
    SdlPostSettings off;output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,off,scene));
    CHECK(!module.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),&a,1));
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),nullptr,0));end();

    // Upstream count clamping: a fifth record must never affect the output.
    p=begin();DX11PostWarp five[5]={};five[4]=a;
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),five,5));
    CHECK(post.Finish(*frame,scene,output));CHECK(module.Apply_Blast_Warp(*frame,scene,output));
    compare(read(*frame,output),p);end();

    // The final warp follows tonemapping of a real float scene. Its alpha
    // contract applies to the completed output, including the tone pass's1.
    output=frame->Back_Buffer_Target();CHECK(post.Prepare(*frame,output,settings(true),scene));CHECK(post.Scene_Is_Float());
    auto raw=pattern(scene.Width,scene.Height);std::vector<unsigned char> hdr(raw.size()*2);
    for(size_t i=0;i<raw.size();++i) {
        const float value=i%4==3 ? .375f : raw[i]/255.0f*2;
        uint32_t bits;std::memcpy(&bits,&value,4);
        uint16_t packed=value==0 ? 0 : uint16_t(((int(bits>>23&255)-127+15)<<10)|(((bits&0x7fffff)+0x1000)>>13));
        std::memcpy(hdr.data()+i*2,&packed,2);
    }
    upload(*frame,scene,hdr);
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),&a,1));
    CHECK(post.Finish(*frame,scene,output));const auto toned=read(*frame,output);
    CHECK(module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),reference(toned,scene.Width,scene.Height,{a}));end();

    CHECK(frame->Resize(41,29));p=begin();key=identity(*frame,scene);
    CHECK(module.Prepare_Blast_Warps(*frame,post,scene,output,key,&a,1));CHECK(post.Finish(*frame,scene,output));
    CHECK(module.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),reference(p,scene.Width,scene.Height,{a}));end();
    CHECK(module.Resources().Textures==1);

    for(auto point:{SdlPostDistortion::TestFailure::Program,SdlPostDistortion::TestFailure::Pipeline,SdlPostDistortion::TestFailure::Target}) {
        SdlPostDistortion recovery;p=begin();key=identity(*frame,scene);recovery.Set_Failure_For_Test(point);
        CHECK(!recovery.Prepare_Blast_Warps(*frame,post,scene,output,key,&a,1));recovery.Set_Failure_For_Test(SdlPostDistortion::TestFailure::None);
        CHECK(post.Finish(*frame,scene,output));CHECK(!recovery.Apply_Blast_Warp(*frame,scene,output));compare(read(*frame,output),p);end();
        p=begin();CHECK(recovery.Prepare_Blast_Warps(*frame,post,scene,output,identity(*frame,scene),&a,1));CHECK(post.Finish(*frame,scene,output));CHECK(recovery.Apply_Blast_Warp(*frame,scene,output));end();CHECK(recovery.Release(*frame));
    }
    CHECK(module.Release(*frame));CHECK(post.Release(*frame));
}
int main(int argc,char **argv)
{
    cpu();
    if(argc>1 && !std::strcmp(argv[1],"--gpu")) {
        if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("GPU unavailable: %s\n",SDL_GetError());return 77;}
        gpu();SDL_Quit();
    }
    std::printf("SDL blast distortion: %u checks, %u failures\n",checks,failures);return failures ? 1 : 0;
}
