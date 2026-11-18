// Copied producer packets through the actual native main-view lifecycle.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/RenderPost.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static int gpu() {
    if(!SDL_Init(SDL_INIT_VIDEO))return 77;
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9 *raw=nullptr;
    D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=16;p.BackBufferHeight=8;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.Windowed=1;
    CHECK(api && api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&raw)==D3D_OK);
    if(!raw){if(api)api->Release();SDL_Quit();return 1;}
    auto *device=static_cast<PosixDevice9 *>(raw);
    if(device->Create_Gpu_Frame(true)!=D3D_OK || !device->Get_Gpu()->Supports_Scene_Depth()){device->Release();api->Release();SDL_Quit();return 77;}
    auto *frame=device->Get_Gpu();CHECK(RenderPost::EnvironmentAvailable(device));
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    CHECK(device->SetTransform(D3DTS_VIEW,&identity)==D3D_OK);CHECK(device->SetTransform(D3DTS_PROJECTION,&identity)==D3D_OK);
    D3DVIEWPORT9 viewport={0,0,16,8,0,1};CHECK(device->SetViewport(&viewport)==D3D_OK);
    IDirect3DSurface9 *back=nullptr;CHECK(device->GetRenderTarget(0,&back)==D3D_OK);
    RenderPost::Settings settings;std::vector<uint8_t> presented;
        // A copied, same-frame environment owns an ungraded eight-bit scene even
        // with the user chain off. Haze runs before particles and preserves alpha.
        RenderPost::Environment environment;
        environment.MapIdentity = 1; environment.MapGeneration = 1;
        environment.Haze.Strength = 1; environment.Haze.Top = environment.Haze.Thickness = 1;
        environment.Haze.Reach = 1000;
        environment.Haze.Colour[0] = environment.Haze.Colour[1] = environment.Haze.Colour[2] = 1;
        IDirect3DTexture9 *shroud = nullptr;
        CHECK(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &shroud, nullptr) == D3D_OK);
        if (shroud) {
            D3DLOCKED_RECT lock = {}; CHECK(shroud->LockRect(0, &lock, nullptr, 0) == D3D_OK);
            std::memset(lock.pBits, 255, 4); CHECK(shroud->UnlockRect(0) == D3D_OK);
            environment.Shroud = shroud;
        }
        device->Set_Post_Environment(environment);
        if (shroud) shroud->Release(); // the copied packet is now the only COM owner
        settings.WorldAO = false; settings.InternalHDR = true; settings.Grade = 1; settings.AdditiveGain = 3; settings.Chain = "off";
        device->Begin_Post(settings); CHECK(device->Post_Active());
        SdlTarget hazed; CHECK(device->Post_Target(back, hazed));
        CHECK(frame->Target_Colour_Format(hazed) == frame->Target_Format());
        CHECK(device->Post_Additive_Gain(hazed) == 1);
        const unsigned hazeScope = device->Begin_Post_View(true, 1, 100, false);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Sky(); // haze-only preparation emits no sky
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[0] > 16 && presented[3] == 0x40);
        const auto onceHaze = presented;
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented)); CHECK(presented == onceHaze);
        device->End_Post_View(hazeScope);
        const unsigned mirrorScope=device->Begin_Post_View(true,1,100,true);
        frame->Set_Target(hazed);frame->Clear_Back_Buffer(true,true,false,0x40402010,.25f,0);
        device->Post_Sky();device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour,16,8,presented));
        CHECK(presented.size()==16*8*4 && presented[0]==0x10 && presented[1]==0x20 && presented[2]==0x40 && presented[3]==0x40);
        device->End_Post_View(mirrorScope);
        const unsigned hazeReentry = device->Begin_Post_View(true, 1, 100, false);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Before_Transparent(); // original sky hook is absent for haze-only settings
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented == onceHaze); // new main key gets the same single application
        device->Finish_Post(false); device->End_Post_View(hazeReentry);
        ++environment.MapGeneration;
        device->Set_Post_Environment(environment);
        device->Begin_Post(settings); CHECK(device->Post_Target(back, hazed));
        const unsigned resetHazeScope = device->Begin_Post_View(true, 1, 100, false);
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40000000, 0.25f, 0);
        device->Post_Sky(); device->Post_Before_Transparent(); // real queued shroud reader, intentionally not flushed
        // An authoritative same-frame map reset invalidates prepared borrowed
        // haze before its old references retire; no old fade may run afterward.
        frame->Set_Target(hazed); frame->Clear_Back_Buffer(true, true, false, 0x40402010, 0.25f, 0);
        device->Set_Post_Environment(RenderPost::Environment{});
        device->Post_Before_Transparent();
        CHECK(frame->Read_Back(hazed.Colour, 16, 8, presented));
        CHECK(presented.size() == 16 * 8 * 4 && presented[0] == 0x10 && presented[1] == 0x20 && presented[2] == 0x40 && presented[3] == 0x40);
        device->Finish_Post(false); device->End_Post_View(resetHazeScope);
        // Freshness is consumed by one display pass; stale settings cannot carry
        // a map's haze into the next scene, including an identical pointer epoch.
        device->Begin_Post(settings); CHECK(!device->Post_Active());
    // Classic never activates the independent owner; the option packet stays copied.
    environment.Shroud=nullptr;environment.Haze.Strength=1;device->Set_Post_Environment(environment);settings.Classic=true;
    device->Begin_Post(settings);CHECK(!device->Post_Active());
    if(back)back->Release();device->Release();api->Release();SDL_Quit();return failures?1:0;
}
int main(int argc,char **argv){CHECK(!RenderPost::EnvironmentAvailable(nullptr));int result=argc>1 && !std::strcmp(argv[1],"--gpu")?gpu():0;std::printf("Native atmosphere producer: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;}
