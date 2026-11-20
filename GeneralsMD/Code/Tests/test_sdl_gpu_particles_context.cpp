// Real native context, phase ownership and mirrored texture-array inputs.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlGpuParticles.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
int main()
{
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP SDL: %s\n",SDL_GetError());return 77;}
    auto *api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9 *raw=nullptr;
    D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=48;
    p.BackBufferFormat=D3DFMT_X8R8G8B8;p.Windowed=1;
    CHECK(api && api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&raw)==D3D_OK);
    if(!raw){if(api)api->Release();SDL_Quit();return 1;}
    auto *device=static_cast<PosixDevice9 *>(raw);SdlGpuParticlesContext c;
    CHECK(!PosixGpuParticles_Context(raw,c));CHECK(!c.Frame);
    if(device->Create_Gpu_Frame(true)!=D3D_OK){std::printf("SKIP GPU: %s\n",SDL_GetError());device->Release();api->Release();SDL_Quit();return 77;}
    auto *frame=device->Get_Gpu();
    CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::Other);
    CHECK(c.Frame==frame && c.DeviceGeneration==frame->Device_Generation());CHECK(SdlGpuParticles::Supported(c));
    bool fresh=false;CHECK(SdlGpuParticles::Prepare(c,4,&fresh));CHECK(fresh);
    IDirect3DTexture9 *texture=nullptr;
    CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)==D3D_OK);
    if(texture){
        auto paint=[&](unsigned value){D3DLOCKED_RECT lock={};CHECK(texture->LockRect(0,&lock,nullptr,0)==D3D_OK);std::memcpy(lock.pBits,&value,4);CHECK(texture->UnlockRect(0)==D3D_OK);};
        paint(0xff103040u);std::string error;SdlGpuParticleTextureView before,after;
        CHECK(c.MirrorTexture(texture,before,error));CHECK(before.Identity && before.Generation && before.Width==1 && before.MipLevels==1);
        SdlGpuParticles::SetSliceTexture(0,texture);SdlGpuParticles::Evaluate(c,0,0,0,0);
        std::vector<uint8_t> pixels;CHECK(SdlGpuParticles::ReadSlice(0,0,pixels,error));
        CHECK(pixels.size()==128*128*4);
        if(pixels.size()==128*128*4)CHECK(pixels[0]==0x10 && pixels[1]==0x30 && pixels[2]==0x40 && pixels[3]==255);
        auto copies=SdlGpuParticles::Stats().SliceCopies;
        SdlGpuParticles::SetSliceTexture(0,texture);SdlGpuParticles::Evaluate(c,0,0,0,0);CHECK(SdlGpuParticles::Stats().SliceCopies==copies);
        paint(0xff604020u);CHECK(c.MirrorTexture(texture,after,error));
        CHECK(before.Identity==after.Identity && before.Generation!=after.Generation);
        SdlGpuParticles::SetSliceTexture(0,texture);SdlGpuParticles::Evaluate(c,0,0,0,0);
        CHECK(SdlGpuParticles::Stats().SliceCopies==copies+1);CHECK(SdlGpuParticles::ReadSlice(0,0,pixels,error));
        if(pixels.size()==128*128*4)CHECK(pixels[0]==0x60 && pixels[1]==0x40 && pixels[2]==0x20);
    }
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    CHECK(device->SetTransform(D3DTS_VIEW,&identity)==D3D_OK);CHECK(device->SetTransform(D3DTS_PROJECTION,&identity)==D3D_OK);
    RenderPost::Settings settings;settings.Chain="off";
    device->Begin_Post(settings);const auto main=device->Begin_Post_View(true,1,100,false);
    CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::MainScene && c.MainViewKey && !c.SoftAllowed);
    device->Post_Before_Transparent();CHECK(PosixGpuParticles_Context(raw,c));
    CHECK(c.SoftAllowed && c.SoftDepth && c.SoftCapture.Phase==SdlSceneCapturePhase::BeforeTransparent);
    CHECK(c.SoftCapture.ViewKey==c.MainViewKey && c.SoftCapture.Frame==frame->Scene_Frame());
    const auto mirror=device->Begin_Post_View(true,1,100,true);
    CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::Reflection && !c.SoftAllowed);
    device->End_Post_View(mirror);CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::MainScene && c.SoftAllowed);
    CHECK(frame->Present(nullptr));CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::Other && !c.SoftAllowed);
    device->End_Post_View(main);
    settings.Classic=true;device->Begin_Post(settings);const auto classic=device->Begin_Post_View(true,1,100,false);
    CHECK(PosixGpuParticles_Context(raw,c));CHECK(c.Pass==SdlGpuParticlesPass::Other && !c.SoftAllowed);
    device->End_Post_View(classic);
    CHECK(SdlGpuParticles::TryReleaseDevice());
    if(texture)texture->Release();device->Release();api->Release();SDL_Quit();
    std::printf("native GPU particle context: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
