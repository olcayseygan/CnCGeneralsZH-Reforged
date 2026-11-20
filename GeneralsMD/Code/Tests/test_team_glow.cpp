// Existing team-color shader arithmetic and the native HDR marker bridge.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/RenderTeamGlow.h"
#include "ffshader.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static void cpu()
{
    for(bool marked:{false,true})for(bool shadow:{false,true})for(bool fog:{false,true}) {
        CombinerDescription p={};p.StageCount=1;p.Stages[0].TextureBound=true;
        p.Stages[0].ColourOperation=p.Stages[0].AlphaOperation=FF_TOP_MODULATE;
        p.Stages[0].ColourArgument1=p.Stages[0].AlphaArgument1=FF_TA_TEXTURE;
        p.Stages[0].ColourArgument2=p.Stages[0].AlphaArgument2=FF_TA_DIFFUSE;
        p.TeamGlow=marked;p.ShadowReceiving=shadow;p.PixelPipeline.FogEnabled=fog;
        std::string native,windows,error,msl;std::vector<unsigned char> spirv;
        CHECK(CombinerShader_Generate(p,COMBINER_SHADER_TARGET_SDL3_GPU,native));
        CHECK(CombinerShader_Generate(p,COMBINER_SHADER_TARGET_D3D11,windows));
        CHECK(SDL3_Shader_Retarget(windows,false));CHECK(native==windows);
        CHECK(SDL3_Compile_HLSL_To_SPIRV(native,false,spirv,error));
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error));
    }
}
struct Vertex {float x,y,z,nx,ny,nz;unsigned colour;float u,v;};
struct Fixture {
    IDirect3D9 *Api=nullptr;PosixDevice9 *Device=nullptr;SdlGpuFrame *Frame=nullptr;IDirect3DTexture9 *Texture=nullptr;
    SdlTarget Target={};unsigned Scope=0;
    ~Fixture(){release();}
    void release(){if(Device){Device->SetTexture(0,nullptr);if(Texture)Texture->Release();Device->Release();}if(Api)Api->Release();Device=nullptr;Texture=nullptr;Api=nullptr;Frame=nullptr;}
    bool create() {
        Api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9 *raw=nullptr;D3DPRESENT_PARAMETERS p={};
        p.BackBufferWidth=p.BackBufferHeight=16;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.Windowed=1;
        if(!Api || Api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&raw)!=D3D_OK)return false;
        Device=static_cast<PosixDevice9 *>(raw);if(Device->Create_Gpu_Frame(true)!=D3D_OK)return false;Frame=Device->Get_Gpu();
        CHECK(Device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&Texture,nullptr)==D3D_OK);
        if(!Texture)return false;D3DLOCKED_RECT lock={};CHECK(Texture->LockRect(0,&lock,nullptr,0)==D3D_OK);
        std::memset(lock.pBits,255,4);CHECK(Texture->UnlockRect(0)==D3D_OK);CHECK(Device->SetTexture(0,Texture)==D3D_OK);
        CHECK(Device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);
        set(D3DRS_LIGHTING,0);set(D3DRS_COLORVERTEX,1);set(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);
        set(D3DRS_SPECULARENABLE,0);set(D3DRS_FOGENABLE,0);set(D3DRS_CULLMODE,D3DCULL_NONE);
        set(D3DRS_ALPHATESTENABLE,0);set(D3DRS_ZENABLE,D3DZB_TRUE);set(D3DRS_ZWRITEENABLE,0);
        set(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);set(D3DRS_COLORWRITEENABLE,15);set(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        for(auto key:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})CHECK(Device->SetTextureStageState(0,key,D3DTOP_MODULATE)==D3D_OK);
        for(auto key:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})CHECK(Device->SetTextureStageState(0,key,D3DTA_TEXTURE)==D3D_OK);
        for(auto key:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})CHECK(Device->SetTextureStageState(0,key,D3DTA_DIFFUSE)==D3D_OK);
        CHECK(Device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE)==D3D_OK);
        D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
        D3DMATRIX projection={};projection._11=projection._22=projection._34=1;projection._33=100.0f/99;projection._43=-100.0f/99;
        CHECK(Device->SetTransform(D3DTS_WORLD,&identity)==D3D_OK);CHECK(Device->SetTransform(D3DTS_VIEW,&identity)==D3D_OK);
        CHECK(Device->SetTransform(D3DTS_PROJECTION,&projection)==D3D_OK);return true;
    }
    void set(D3DRENDERSTATETYPE key,unsigned value){CHECK(Device->SetRenderState(key,value)==D3D_OK);}
    void begin(bool hdr=false,bool classic=false,bool reflection=false) {
        RenderPost::Settings settings;settings.Chain=hdr?"bloom":"off";settings.Classic=classic;settings.AdditiveGain=1.6f;
        Device->Begin_Post(settings);std::string error;IDirect3DSurface9 *surface=nullptr;CHECK(Device->GetRenderTarget(0,&surface)==D3D_OK);
        Target=Frame->Back_Buffer_Target();Device->Post_Target(surface,Target);if(surface)surface->Release();
        Scope=Device->Begin_Post_View(true,1,100,reflection);Device->Set_Team_Glow(false);
        clear(0xFF2060A0);
    }
    void end(){Device->End_Post_View(Scope);Device->Finish_Post(true);CHECK(Frame->Flush());Scope=0;}
    void clear(unsigned colour){Frame->Set_Target(Target);Frame->Clear_Back_Buffer(true,true,false,colour,1,0);}
    void mesh(unsigned effect,const float values[4],bool blend=false,unsigned colour=0xFFCC3300) {
        set(D3DRS_ALPHABLENDENABLE,blend);set(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);set(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

        Vertex v[]={{-2,-2,2,0,0,1,colour,0,1},{6,-2,2,0,0,1,colour,2,1},{-2,6,2,0,0,1,colour,0,-1}};
        const auto recorded=Device->Draws_Recorded();const auto refused=Device->Draw_Refusals();
        CHECK(Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex))==D3D_OK);
        CHECK(Device->Draws_Recorded()==recorded+1);CHECK(Device->Draw_Refusals()==refused);
        for(auto &vertex:v)vertex.colour=0;Device->Set_Team_Glow(false); // prior packet remains immutable
    }
    std::vector<uint8_t> read(){std::vector<uint8_t> pixels;CHECK(Frame->Read_Back(Target.Colour,16,16,pixels));return pixels;}
    std::vector<uint16_t> half() {
        CHECK(Frame->Flush());std::vector<uint16_t> result(16*16*4);
        SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=uint32_t(result.size()*2);
        auto *transfer=SDL_CreateGPUTransferBuffer(Frame->Device(),&info);auto *commands=SDL_AcquireGPUCommandBuffer(Frame->Device());
        CHECK(transfer && commands);if(!transfer || !commands){if(transfer)SDL_ReleaseGPUTransferBuffer(Frame->Device(),transfer);if(commands)SDL_CancelGPUCommandBuffer(commands);return {};}
        auto *pass=SDL_BeginGPUCopyPass(commands);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(Frame->Device(),transfer);return {};}
        SDL_GPUTextureRegion source={};source.texture=Target.Colour;source.w=source.h=16;source.d=1;
        SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;
        SDL_DownloadFromGPUTexture(pass,&source,&destination);SDL_EndGPUCopyPass(pass);
        auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
        if(fence){CHECK(SDL_WaitForGPUFences(Frame->Device(),true,&fence,1));void *bytes=SDL_MapGPUTransferBuffer(Frame->Device(),transfer,false);CHECK(bytes);if(bytes){std::memcpy(result.data(),bytes,result.size()*2);SDL_UnmapGPUTransferBuffer(Frame->Device(),transfer);}SDL_ReleaseGPUFence(Frame->Device(),fence);}
        SDL_ReleaseGPUTransferBuffer(Frame->Device(),transfer);return result;
    }
};
static float half_float(uint16_t h){const unsigned e=(h>>10)&31;return std::ldexp(float(h&1023)/1024+(e?1:0),e?int(e)-15:-14);}

static int gpu()
{
    if(!SDL_Init(SDL_INIT_VIDEO))return 77;
    int result=0;{
        Fixture f;if(!f.create()){std::printf("SKIP GPU: %s\n",SDL_GetError());f.release();SDL_Quit();return 77;}
        CHECK(RenderTeamGlow::Available(f.Device));
        const float numbers[4]={};
        auto team=[&](bool marked,bool blend=false,bool reflection=false,unsigned fault=0) {
            f.begin(true,false,reflection);f.Device->Set_Team_Glow(marked);f.Device->Refuse_Next_Team_Glow_For_Test(fault);
            f.mesh(0,numbers,blend);auto pixels=f.half();f.end();return pixels;
        };
        const auto plain=team(false),glow=team(true);CHECK(plain!=glow);
        const auto at=(8*16+8)*4;
        CHECK(glow.size()==16*16*4);
        if(glow.size()==16*16*4)CHECK(glow[at]==0x3E00 && glow[at+1]==0x3600 && glow[at+2]==0);
        CHECK(team(true,true)==plain);CHECK(team(true,false,true)==plain);
        CHECK(team(true,false,false,4)==plain);CHECK(team(true,false,false,8)==plain);
        // Scene ownership ends before HUD drawing, even if a stale marker survives.
        f.begin(true);f.Device->End_Post_View(f.Scope);f.Scope=0;f.Device->Set_Team_Glow(true);
        f.mesh(0,numbers);CHECK(f.half()==plain);f.end();
        // UI FVF is excluded independently of scope ownership.
        f.begin(true);f.Device->Set_Team_Glow(true);
        struct UiVertex {float x,y,z,rhw;unsigned colour;float u,v;};
        const UiVertex ui[]={{0,0,.5f,1,0xFFCC3300,0,0},{32,0,.5f,1,0xFFCC3300,1,0},{0,32,.5f,1,0xFFCC3300,0,1}};
        CHECK(f.Device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);
        CHECK(f.Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,ui,sizeof(UiVertex))==D3D_OK);
        const auto hud=f.half();CHECK(hud.size()==plain.size());
        if(hud.size()==plain.size())CHECK(hud[at]==plain[at] && hud[at+1]==plain[at+1]);
        CHECK(f.Device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);f.end();
        f.begin();f.Device->Set_Team_Glow(false);f.mesh(0,numbers);const auto ordinary=f.read();f.end();
        f.begin();f.Device->Set_Team_Glow(true);f.mesh(0,numbers);CHECK(f.read()==ordinary);f.end();
        f.begin(false,true);f.Device->Set_Team_Glow(true);f.mesh(0,numbers);CHECK(f.read()==ordinary);f.end();
        result=failures?1:0;
    }SDL_Quit();return result;
}
int main(int argc,char **argv)
{
    CHECK(!RenderTeamGlow::Available(static_cast<IDirect3DDevice9 *>(nullptr)));
    cpu();if(failures)return 1;int result=0;if(argc>1 && !std::strcmp(argv[1],"--gpu"))result=gpu();
    std::printf("native team glow: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;
}
