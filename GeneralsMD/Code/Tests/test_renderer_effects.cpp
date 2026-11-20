// Exact shared model arithmetic, native scene-copy admission and immutable draw policy.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/RenderEffects.h"
#include "Platform/SunShadow.h"
#include "ffshader.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <limits>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static void cpu()
{
    for(unsigned effect=0;effect<DRAW_EFFECT_CLOAK_SHADOW;++effect)for(unsigned shadow=0;shadow<2;++shadow)
    for(unsigned fog=0;fog<2;++fog) {
        CombinerDescription p={};p.StageCount=1;p.Stages[0].TextureBound=true;
        p.Stages[0].ColourOperation=p.Stages[0].AlphaOperation=FF_TOP_MODULATE;
        p.Stages[0].ColourArgument1=p.Stages[0].AlphaArgument1=FF_TA_TEXTURE;
        p.Stages[0].ColourArgument2=p.Stages[0].AlphaArgument2=FF_TA_DIFFUSE;
        p.DrawEffect=effect;p.ShadowReceiving=shadow;p.TeamGlow=false;p.PixelPipeline.FogEnabled=fog;
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
        Device->Begin_Post(settings);std::string error;CHECK(Device->Current_Gpu_Target(Target,error));
        Scope=Device->Begin_Post_View(true,1,100,reflection);
        clear(0xFF2060A0);
    }
    void end(){Device->End_Post_View(Scope);Device->Finish_Post(true);CHECK(Frame->Flush());Scope=0;}
    void clear(unsigned colour){Frame->Set_Target(Target);Frame->Clear_Back_Buffer(true,true,false,colour,1,0);}
    void mesh(unsigned effect,const float values[4],bool blend=false,unsigned colour=0xFFCC3300) {
        set(D3DRS_ALPHABLENDENABLE,blend);set(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);set(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        Device->Set_Model_Effect(effect,values);
        Vertex v[]={{-2,-2,2,0,0,1,colour,0,1},{6,-2,2,0,0,1,colour,2,1},{-2,6,2,0,0,1,colour,0,-1}};
        const auto recorded=Device->Draws_Recorded();const auto refused=Device->Draw_Refusals();
        CHECK(Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex))==D3D_OK);
        CHECK(Device->Draws_Recorded()==recorded+1);CHECK(Device->Draw_Refusals()==refused);
        for(auto &vertex:v)vertex.colour=0;const float none[4]={};Device->Set_Model_Effect(0,none); // prior packet remains immutable
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
        const float numbers[4]={1,1,0.123f,0.25f};
        f.begin();CHECK(RenderEffects::DrawEffectsAvailable(f.Device));CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_OUTAGE));
        f.mesh(DRAW_EFFECT_NONE,numbers);const auto plain=f.read();CHECK(plain.size()==16*16*4 && plain[(8*16+8)*4+2]==204);f.end();
        for(unsigned effect:{DRAW_EFFECT_OUTAGE,DRAW_EFFECT_EMP,DRAW_EFFECT_REVEALED}) {
            f.begin();f.mesh(effect,numbers);CHECK(f.read()!=plain);f.end();
            for(unsigned fault:{4u,8u}){f.begin();f.Device->Refuse_Next_Model_For_Test(fault);f.mesh(effect,numbers);CHECK(f.read()==plain);f.end();}
        }
        f.begin();CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK)); // allocation alone must not copy old colour
        f.clear(0xFF2060A0);f.mesh(DRAW_EFFECT_CLOAK,numbers,true);const auto blue=f.read();CHECK(blue!=plain);
        CHECK(blue.size()==plain.size() && blue[(8*16+8)*4]>16);
        f.clear(0xFF10C020);f.mesh(DRAW_EFFECT_CLOAK,numbers,true);CHECK(f.read()==blue); // once for this exact main key
        f.Device->End_Post_View(f.Scope);f.Scope=f.Device->Begin_Post_View(true,1,100,false);
        f.clear(0xFF10C020);f.mesh(DRAW_EFFECT_CLOAK,numbers,true);CHECK(f.read()!=blue);f.end();
        f.begin();f.Device->Refuse_Next_Model_For_Test(1);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));f.mesh(0,numbers);CHECK(f.read()==plain);f.end();
        f.begin();CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));f.Device->Refuse_Next_Model_For_Test(2);
        f.mesh(DRAW_EFFECT_CLOAK,numbers,true);CHECK(f.read()==plain);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));f.end();
        f.begin(false,true);CHECK(!RenderEffects::DrawEffectsAvailable(f.Device));f.mesh(DRAW_EFFECT_EMP,numbers);CHECK(f.read()==plain);f.end();
        f.begin(false,false,true);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_EMP));f.mesh(DRAW_EFFECT_EMP,numbers);CHECK(f.read()==plain);f.end();
        // Invalid payload and screen-space UI cannot inherit a world's optional look.
        f.begin();const float invalid[4]={1,std::numeric_limits<float>::quiet_NaN(),0,0};
        f.Device->Set_Model_Effect(DRAW_EFFECT_EMP,invalid);float requested[4]={9,9,9,9};
        CHECK(f.Device->Requested_Model_Effect(requested)==DRAW_EFFECT_NONE);
        CHECK(requested[0]==0 && requested[1]==0 && requested[2]==0 && requested[3]==0);
        f.Device->Set_Model_Effect(DRAW_EFFECT_EMP,numbers);
        CHECK(f.Device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);
        CHECK(f.Device->Draw_Model_Effect()==DRAW_EFFECT_NONE);
        CHECK(f.Device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1)==D3D_OK);
        // A viewport or camera replacement cannot borrow an earlier main-view snapshot.
        D3DVIEWPORT9 viewport={};CHECK(f.Device->GetViewport(&viewport)==D3D_OK);
        D3DVIEWPORT9 changed=viewport;changed.Width=8;
        CHECK(f.Device->SetViewport(&changed)==D3D_OK);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_EMP));
        CHECK(f.Device->SetViewport(&viewport)==D3D_OK);CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_EMP));
        D3DMATRIX view={};CHECK(f.Device->GetTransform(D3DTS_VIEW,&view)==D3D_OK);
        D3DMATRIX moved=view;moved._41+=1;
        CHECK(f.Device->SetTransform(D3DTS_VIEW,&moved)==D3D_OK);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_EMP));
        CHECK(f.Device->SetTransform(D3DTS_VIEW,&view)==D3D_OK);f.end();
        // The default scene filter is an explicitly registered main stand-in,
        // even with the user post chain off. Arbitrary and special RTTs are not.
        IDirect3DSurface9 *back=nullptr,*surface=nullptr;IDirect3DTexture9 *standIn=nullptr;
        CHECK(f.Device->GetRenderTarget(0,&back)==D3D_OK);
        CHECK(f.Device->CreateTexture(16,16,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&standIn,nullptr)==D3D_OK);
        CHECK(standIn && standIn->GetSurfaceLevel(0,&surface)==D3D_OK);
        if(surface) {
            RenderPost::Settings direct;f.Device->Begin_Post(direct);f.Device->Post_Stand_In(standIn,surface);
            CHECK(f.Device->SetRenderTarget(0,surface)==D3D_OK);std::string error;
            CHECK(f.Device->Current_Gpu_Target(f.Target,error));f.Scope=f.Device->Begin_Post_View(true,1,100,false);
            CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));f.clear(0xFF2060A0);
            f.mesh(DRAW_EFFECT_CLOAK,numbers,true);CHECK(f.read()==blue);
            const auto mirror=f.Device->Begin_Post_View(true,1,100,true);
            CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));f.Device->End_Post_View(mirror);
            CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));
            CHECK(SunShadow::Begin(f.Device,16));CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));CHECK(SunShadow::End(f.Device));
            CHECK(f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK));
            f.Device->Post_Stand_In(nullptr,nullptr);CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK)); // arbitrary/special RTT
            f.Device->Post_Stand_In(standIn,surface);f.Device->End_Post_View(f.Scope);
            f.Device->Begin_Post(direct);f.Scope=f.Device->Begin_Post_View(true,1,100,false);
            CHECK(!f.Device->Takes_Model_Effect(DRAW_EFFECT_CLOAK)); // stale prior-frame registration
            f.Device->End_Post_View(f.Scope);CHECK(f.Device->SetRenderTarget(0,back)==D3D_OK);CHECK(f.Frame->Flush());
            surface->Release();
        }
        if(standIn)standIn->Release();if(back)back->Release();
        // Same-format HDR copy retains values above one before the first cloak.
        f.begin(true);CHECK(SdlGpuFrame::Target_Colour_Format(f.Target)==SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
        CHECK(f.Frame->Flush()); // this frame clear must precede the independent fixture upload
        std::vector<uint16_t> hdr(16*16*4);for(size_t at=0;at<hdr.size();at+=4){hdr[at]=0x4400;hdr[at+1]=0x3400;hdr[at+2]=0x3800;hdr[at+3]=0x3C00;}
        uint32_t offset=0;auto *space=f.Frame->Upload_Space(uint32_t(hdr.size()*2),offset);CHECK(space);if(space)std::memcpy(space,hdr.data(),hdr.size()*2);
        f.Frame->Queue_Texture_Upload(f.Target.Colour,0,16,16,offset);
        auto sourceSanity=f.half();CHECK(sourceSanity.size()==16*16*4);if(sourceSanity.size()==16*16*4){const auto at=(8*16+8)*4;CHECK(sourceSanity[at]==0x4400 && sourceSanity[at+1]==0x3400 && sourceSanity[at+2]==0x3800 && sourceSanity[at+3]==0x3C00);std::printf("HDR source actual red %.8g expected 4\n",half_float(sourceSanity[at]));}
        const float glass[4]={1,0,0.123f,0};f.mesh(DRAW_EFFECT_CLOAK,glass,true);auto values=f.half();
        CHECK(values.size()==16*16*4);if(values.size()==16*16*4){const float red=half_float(values[(8*16+8)*4]);std::printf("HDR cloak actual red %.8g\n",red);CHECK(red>3.5f && red<3.6f);}f.end();
        result=failures?1:0;
    }SDL_Quit();return result;
}
int main(int argc,char **argv)
{
    CHECK(!RenderEffects::DrawEffectsAvailable(static_cast<IDirect3DDevice9 *>(nullptr)));
    cpu();if(failures)return 1;int result=0;if(argc>1 && !std::strcmp(argv[1],"--gpu"))result=gpu();
    std::printf("native model effects: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;
}
