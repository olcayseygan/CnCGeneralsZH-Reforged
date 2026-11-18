// Shared Windows arithmetic plus actual native policy, payload and refusal checks.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/RenderParticles.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static void cpu()
{
    for(unsigned soft=0;soft<3;++soft)for(unsigned fire=0;fire<3;++fire)for(unsigned fog=0;fog<2;++fog) {
        VertexPipelineDescription vertex={};vertex.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;
        vertex.ColourVertexEnabled=true;vertex.StageCount=1;vertex.Stages[0].TextureCoordinateIndex=FF_TSS_TCI_PASSTHRU;
        vertex.SoftParticle=soft!=0;vertex.FireLook=fire!=0;vertex.FogEnabled=fog;vertex.FogVertexMode=FF_FOG_LINEAR;
        CombinerDescription pixel={};pixel.StageCount=1;pixel.Stages[0].TextureBound=true;
        pixel.Stages[0].ColourOperation=pixel.Stages[0].AlphaOperation=FF_TOP_MODULATE;
        pixel.Stages[0].ColourArgument1=pixel.Stages[0].AlphaArgument1=FF_TA_TEXTURE;
        pixel.Stages[0].ColourArgument2=pixel.Stages[0].AlphaArgument2=FF_TA_DIFFUSE;
        pixel.SoftParticle=soft;pixel.FireLook=fire;pixel.PixelPipeline.FogEnabled=fog;
        std::string native,reference,error,msl;std::vector<unsigned char> spirv;
        CHECK(VertexShader_Generate(vertex,VERTEX_SHADER_TARGET_SDL3_GPU,native));
        CHECK(VertexShader_Generate(vertex,VERTEX_SHADER_TARGET_D3D11,reference));
        CHECK(SDL3_Shader_Retarget(reference,true));CHECK(native==reference);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(native,true,spirv,error));
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,true,msl,error));
        CHECK(CombinerShader_Generate(pixel,COMBINER_SHADER_TARGET_SDL3_GPU,native));
        CHECK(CombinerShader_Generate(pixel,COMBINER_SHADER_TARGET_D3D11,reference));
        CHECK(SDL3_Shader_Retarget(reference,false));CHECK(native==reference);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(native,false,spirv,error));
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error));
    }
}
struct Vertex {float x,y,z,nx,ny,nz;unsigned colour;float u,v,width,age;};
static int gpu()
{
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP SDL: %s\n",SDL_GetError());return 77;}
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9 *raw=nullptr;
    D3DPRESENT_PARAMETERS parameters={};parameters.BackBufferWidth=parameters.BackBufferHeight=16;
    parameters.BackBufferFormat=D3DFMT_X8R8G8B8;parameters.Windowed=1;
    CHECK(api && api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&parameters,&raw)==D3D_OK);
    if(!raw){if(api)api->Release();SDL_Quit();return 1;}
    auto *device=static_cast<PosixDevice9 *>(raw);
    if(device->Create_Gpu_Frame(true)!=D3D_OK){std::printf("SKIP GPU: %s\n",SDL_GetError());device->Release();api->Release();SDL_Quit();return 77;}
    auto *frame=device->Get_Gpu();IDirect3DTexture9 *texture=nullptr;
    CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)==D3D_OK);
    if(!texture){device->Release();api->Release();SDL_Quit();return 1;}
    D3DLOCKED_RECT locked={};CHECK(texture->LockRect(0,&locked,nullptr,0)==D3D_OK);
    std::memset(locked.pBits,255,4);CHECK(texture->UnlockRect(0)==D3D_OK);
    CHECK(device->SetTexture(0,texture)==D3D_OK);
    CHECK(device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX2)==D3D_OK);
    const auto state=[&](D3DRENDERSTATETYPE key,unsigned value){CHECK(device->SetRenderState(key,value)==D3D_OK);};
    state(D3DRS_LIGHTING,0);state(D3DRS_COLORVERTEX,1);state(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);
    state(D3DRS_SPECULARENABLE,0);state(D3DRS_FOGENABLE,0);state(D3DRS_CULLMODE,D3DCULL_NONE);
    state(D3DRS_ALPHATESTENABLE,0);state(D3DRS_ZENABLE,D3DZB_TRUE);state(D3DRS_ZWRITEENABLE,0);
    state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);state(D3DRS_ALPHABLENDENABLE,1);state(D3DRS_COLORWRITEENABLE,15);
    state(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    for(auto stage:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})CHECK(device->SetTextureStageState(0,stage,D3DTOP_MODULATE)==D3D_OK);
    for(auto stage:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})CHECK(device->SetTextureStageState(0,stage,D3DTA_TEXTURE)==D3D_OK);
    for(auto stage:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})CHECK(device->SetTextureStageState(0,stage,D3DTA_DIFFUSE)==D3D_OK);
    CHECK(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE)==D3D_OK);
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    D3DMATRIX projection={};projection._11=projection._22=1;projection._33=100.0f/99;
    projection._34=1;projection._43=-100.0f/99;
    CHECK(device->SetTransform(D3DTS_WORLD,&identity)==D3D_OK);CHECK(device->SetTransform(D3DTS_VIEW,&identity)==D3D_OK);
    CHECK(device->SetTransform(D3DTS_PROJECTION,&projection)==D3D_OK);
    auto draw=[&](bool soft,bool fire,float width,float age,bool classic=false,bool reflection=false,unsigned fault=0,bool phase=true,bool stale=false) {
        RenderPost::Settings settings;settings.Classic=classic;device->Begin_Post(settings);
        const auto scope=device->Begin_Post_View(true,1,100,reflection);
        frame->Set_Target(frame->Back_Buffer_Target());
        const float behind=projection._33+projection._43/2.25f;
        frame->Clear_Back_Buffer(true,true,false,0,behind,0);
        if(phase) device->Post_Before_Transparent();
        RenderParticles::TakeSceneDepth(device);
        if(stale) CHECK(frame->Present(nullptr));
        state(D3DRS_SRCBLEND,fire?D3DBLEND_ONE:D3DBLEND_SRCALPHA);
        state(D3DRS_DESTBLEND,fire?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA);
        RenderParticles::SetSoftParticles(device,soft);RenderParticles::SetFireLook(device,fire);
        device->Refuse_Next_Sprite_For_Test(fault);
        const float packed=17+0.01f+0.98f*age;
        Vertex vertices[]={{-2,-2,2,0,0,1,0xFFFFFFFF,0,1,width,packed},
            {6,-2,2,0,0,1,0xFFFFFFFF,2,1,width,packed},{-2,6,2,0,0,1,0xFFFFFFFF,0,-1,width,packed}};
        const auto recorded=device->Draws_Recorded(); const auto refused=device->Draw_Refusals();
        CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex))==D3D_OK);
        CHECK(device->Draws_Recorded()==recorded+1);CHECK(device->Draw_Refusals()==refused);
        // The delayed draw owns its selected programs/constants and copied payload.
        for(auto &v:vertices){v.width=0.001f;v.age=0;v.colour=0;}
        RenderParticles::SetSoftParticles(device,false);RenderParticles::SetFireLook(device,false);
        std::vector<uint8_t> pixels;CHECK(frame->Read_Back(frame->Back_Buffer(),16,16,pixels));
        device->End_Post_View(scope);return pixels;
    };
    const auto hard=draw(false,false,0.5f,0.8f);CHECK(hard.size()==16*16*4);
    if(hard.size()==16*16*4)CHECK(hard[(8*16+8)*4]==255);
    const auto soft=draw(true,false,0.5f,0.8f);
    if(soft.size()==hard.size())for(unsigned channel=0;channel<3;++channel)CHECK(std::abs(int(soft[(8*16+8)*4+channel])-128)<=1);
    CHECK(draw(true,false,0.5f,0.8f,false,false,0,false)==hard); // no pretransparent phase proof
    CHECK(draw(true,false,0.5f,0.8f,false,false,0,true,true)==hard); // stale frame capture
    CHECK(draw(true,false,0.001f,0.8f)==hard); // mine-sized hard edge
    CHECK(draw(true,false,0.5f,0.8f,true)==hard);CHECK(draw(true,false,0.5f,0.8f,false,true)==hard);
    for(unsigned fault:{1u,2u,4u,8u})CHECK(draw(true,false,0.5f,0.8f,false,false,fault)==hard);
    const auto fire=draw(false,true,0.5f,0.8f);CHECK(fire!=hard);CHECK(draw(false,true,0.5f,0.2f)!=fire);
    if(fire.size()==hard.size())CHECK(fire[(8*16+8)*4+2]>fire[(8*16+8)*4]);
    CHECK(draw(false,true,0.5f,0.8f,true)==hard);
    CHECK(draw(false,true,0.5f,0.8f,false,true)==fire); // original fire math is allowed in mirrors
    CHECK(draw(true,true,0.5f,0.8f)!=fire);
    for(unsigned fault:{1u,2u,4u,8u})CHECK(draw(false,true,0.5f,0.8f,false,false,fault)==hard);

    CHECK(device->SetTexture(0,nullptr)==D3D_OK);texture->Release();device->Release();api->Release();SDL_Quit();
    return failures?1:0;
}
int main(int argc,char **argv)
{
    cpu();if(failures)return 1;
    int result=0;if(argc>1 && !std::strcmp(argv[1],"--gpu"))result=gpu();
    std::printf("native sorted particles: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;
}
