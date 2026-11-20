// Actual named Classic programs through native depth/layout/target/fallback routing.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/RenderWaterFoam.h"
#include "Platform/EngineShaderName.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
struct Vertex {float x,y,z;unsigned colour;float u,v;};
static int gpu()
{
    if(!SDL_Init(SDL_INIT_VIDEO))return 77;
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9 *raw=nullptr;
    D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=p.BackBufferHeight=16;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.Windowed=1;
    CHECK(api && api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&raw)==D3D_OK);
    if(!raw){if(api)api->Release();SDL_Quit();return 1;}
    auto *device=static_cast<PosixDevice9 *>(raw);
    if(device->Create_Gpu_Frame(true)!=D3D_OK || !device->Get_Gpu()->Supports_Scene_Depth()){device->Release();api->Release();SDL_Quit();return 77;}
    auto *frame=device->Get_Gpu();CHECK(RenderWaterFoam::Available(device));IDirect3DTexture9 *textures[4]={};IDirect3DPixelShader9 *shader=nullptr;
    const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};CHECK(device->CreatePixelShader(tokens,&shader)==D3D_OK);
    for(unsigned i=0;i<4;++i){CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[i],nullptr)==D3D_OK);CHECK(device->SetTexture(i,textures[i])==D3D_OK);}
    auto texel=[&](unsigned index,unsigned value){D3DLOCKED_RECT lock={};CHECK(textures[index]->LockRect(0,&lock,nullptr,0)==D3D_OK);std::memcpy(lock.pBits,&value,4);CHECK(textures[index]->UnlockRect(0)==D3D_OK);};
    texel(0,0x80402010);texel(1,0xFF000000);texel(2,0xFF000000);
    auto state=[&](D3DRENDERSTATETYPE key,unsigned value){CHECK(device->SetRenderState(key,value)==D3D_OK);};
    state(D3DRS_LIGHTING,0);state(D3DRS_COLORVERTEX,1);state(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);
    state(D3DRS_FOGENABLE,0);state(D3DRS_SPECULARENABLE,0);state(D3DRS_ALPHATESTENABLE,0);
    state(D3DRS_CULLMODE,D3DCULL_NONE);state(D3DRS_ZENABLE,D3DZB_TRUE);state(D3DRS_ZWRITEENABLE,0);
    state(D3DRS_ZFUNC,D3DCMP_ALWAYS);state(D3DRS_ALPHABLENDENABLE,1);state(D3DRS_COLORWRITEENABLE,15);state(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    CHECK(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);
    for(unsigned i=0;i<4;++i){CHECK(device->SetTextureStageState(i,D3DTSS_TEXCOORDINDEX,0)==D3D_OK);CHECK(device->SetTextureStageState(i,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE)==D3D_OK);}
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    D3DMATRIX view=identity;view._33=-1;view._43=10;
    D3DMATRIX projection={};projection._11=projection._22=projection._34=1;projection._33=100.0f/99;projection._43=-100.0f/99;
    CHECK(device->SetTransform(D3DTS_WORLD,&identity)==D3D_OK);CHECK(device->SetTransform(D3DTS_VIEW,&view)==D3D_OK);CHECK(device->SetTransform(D3DTS_PROJECTION,&projection)==D3D_OK);
    IDirect3DSurface9 *back=nullptr,*standSurface=nullptr;IDirect3DTexture9 *standTexture=nullptr;
    CHECK(device->GetRenderTarget(0,&back)==D3D_OK);
    CHECK(device->CreateTexture(16,16,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&standTexture,nullptr)==D3D_OK);
    CHECK(standTexture && standTexture->GetSurfaceLevel(0,&standSurface)==D3D_OK);
    auto draw=[&](bool river,bool enhanced,float bed,unsigned fault=0,bool classic=false,bool reflection=false,bool hdr=false,bool partial=false,bool legacyFeather=false,unsigned rtt=0) {
        RenderPost::Settings settings;settings.Chain=hdr?"bloom":"off";settings.Classic=classic;
        if(rtt==3)device->Post_Stand_In(standTexture,standSurface); // Begin_Post must discard stale registration
        device->Begin_Post(settings);CHECK(device->SetRenderTarget(0,rtt?standSurface:back)==D3D_OK);
        if(rtt==1 || rtt==4)device->Post_Stand_In(standTexture,standSurface);
        D3DVIEWPORT9 viewport={partial?4u:0u,partial?3u:0u,partial?8u:16u,partial?8u:16u,0,1};CHECK(device->SetViewport(&viewport)==D3D_OK);
        std::string error;SdlTarget target;CHECK(device->Current_Gpu_Target(target,error));const auto scope=device->Begin_Post_View(true,1,100,reflection);
        frame->Set_Target(target);frame->Clear_Back_Buffer(true,true,false,0x40000000,projection._33+projection._43/bed,0);
        state(D3DRS_SRCBLEND,legacyFeather?D3DBLEND_DESTALPHA:D3DBLEND_SRCALPHA);
        state(D3DRS_DESTBLEND,legacyFeather?D3DBLEND_INVDESTALPHA:D3DBLEND_INVSRCALPHA);
        state(D3DRS_TEXTUREFACTOR,0x80FF4000);texel(3,river?0xFF000000:0xFFFFFFFF);
        const char *name=river?(enhanced?"river water foam ps.1.1":"classic river water ps.1.1"):
            (enhanced?"trapezoid water foam ps.1.1":"classic trapezoid water ps.1.1");
        PosixDevice_Name_Shader(shader,name);CHECK(device->SetPixelShader(shader)==D3D_OK);device->Refuse_Next_Foam_For_Test(fault);
        if(enhanced && !classic && !reflection && !fault){
            if(rtt){CHECK(!RenderWaterFoam::Ready(device));CHECK(!device->Classic_Water_Depth());}
            else {CHECK(RenderWaterFoam::Ready(device));CHECK(device->Classic_Water_Depth());CHECK(device->Classic_Water_Depth()!=target.Depth);}
            if(rtt==4){device->Post_Stand_In(nullptr,nullptr);CHECK(!RenderWaterFoam::Ready(device));CHECK(!device->Classic_Water_Depth());}
        }
        const Vertex vertices[]={{-2,-2,8,0xFFFFFFFF,0,1},{6,-2,8,0xFFFFFFFF,2,1},{-2,6,8,0xFFFFFFFF,0,-1}};
        const auto recorded=device->Draws_Recorded();const auto refused=device->Draw_Refusals();
        CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex))==D3D_OK);CHECK(device->Draws_Recorded()==recorded+1);CHECK(device->Draw_Refusals()==refused);
        CHECK(device->SetPixelShader(nullptr)==D3D_OK);device->End_Post_View(scope);
        std::vector<uint8_t> pixels;
        if(hdr){device->Finish_Post(false);CHECK(frame->Read_Back(frame->Back_Buffer(),16,16,pixels));}
        else CHECK(frame->Read_Back(target.Colour,16,16,pixels));
        CHECK(device->SetRenderTarget(0,back)==D3D_OK);
        return pixels;
    };
    for(bool river:{false,true}) {
        const auto plain=draw(river,false,2,0,false,false,false,false,true);CHECK(plain.size()==16*16*4);
        bool visible=false;for(size_t i=0;i<plain.size();i+=4)visible|=plain[i]||plain[i+1]||plain[i+2];CHECK(visible);
        const auto edge=draw(river,true,2);CHECK(edge.size()==plain.size());
        for(size_t i=0;i<edge.size();i+=4)CHECK(edge[i]<=1 && edge[i+1]<=1 && edge[i+2]<=1);
        const auto deep=draw(river,true,20);CHECK(deep!=edge);
        CHECK(draw(river,true,2,0,false,false,false,false,false,1)==plain); // chain-off RTT is outside slot03 main ownership
        for(unsigned rtt:{2u,3u,4u})CHECK(draw(river,true,2,0,false,false,false,false,false,rtt)==plain); // special/arbitrary, stale, invalidated owner
        const auto offset=draw(river,true,2,0,false,false,false,true);for(size_t i=0;i<offset.size();i+=4)CHECK(offset[i]<=1 && offset[i+1]<=1 && offset[i+2]<=1);
        for(unsigned fault:{1u,2u,4u,8u})CHECK(draw(river,true,2,fault)==plain); // whole plain program + legacy feather
        CHECK(draw(river,true,2,0,true)==plain);CHECK(draw(river,true,2,0,false,true)==plain);
        if(SDL_GPUTextureSupportsFormat(frame->Device(),SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
            const auto hdr=draw(river,true,2,0,false,false,true);for(size_t i=0;i<hdr.size();i+=4)CHECK(hdr[i]<=1 && hdr[i+1]<=1 && hdr[i+2]<=1);
        }
    }
    if(standSurface)standSurface->Release();if(standTexture)standTexture->Release();if(back)back->Release();
    for(unsigned i=0;i<4;++i){device->SetTexture(i,nullptr);if(textures[i])textures[i]->Release();}if(shader)shader->Release();
    device->Release();api->Release();SDL_Quit();return failures?1:0;
}
int main(int argc,char **argv)
{
    CHECK(!RenderWaterFoam::Ready(nullptr));
    int result=0;if(argc>1 && !std::strcmp(argv[1],"--gpu"))result=gpu();
    std::printf("native Classic water foam: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;
}
