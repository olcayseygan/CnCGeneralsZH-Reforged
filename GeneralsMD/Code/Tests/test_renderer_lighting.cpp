// Full/static environment ownership, per-draw fallback, and actual native light receivers.
#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlConstants.h"
#include "Platform/SunShadow.h"
#include "SdlSunShadowDebug.h"
#include "Platform/EngineShaderName.h"
#include "Platform/RenderLighting.h"
#include "lightenvironment.h"
#include "light.h"
#include "dx8wrapper.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "sortingrenderer.h"
#include "vertmaterial.h"
#include "ww3d.h"
#include "wwmath.h"
#include "ffvertex.h"
#include "ffshader.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>
#include <cstring>

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char prefix[] = "";
char *gAppPrefix = prefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)

static bool same_environment(const LightEnvironmentClass &a,const LightEnvironmentClass &b)
{
    if (a.Get_Light_Count()!=b.Get_Light_Count() || a.Get_Equivalent_Ambient()!=b.Get_Equivalent_Ambient()) return false;
    for (int i=0;i<a.Get_Light_Count();++i) {
        if (a.Get_Light_Diffuse(i)!=b.Get_Light_Diffuse(i) || a.Get_Light_Direction(i)!=b.Get_Light_Direction(i)
            || a.isPointLight(i)!=b.isPointLight(i)) return false;
        if (a.isPointLight(i) && (a.getPointCenter(i)!=b.getPointCenter(i) || a.getPointDiffuse(i)!=b.getPointDiffuse(i)
            || a.getPointAmbient(i)!=b.getPointAmbient(i) || a.getPointIrad(i)!=b.getPointIrad(i)
            || a.getPointOrad(i)!=b.getPointOrad(i))) return false;
    }
    return true;
}
static void check_snapshots()
{
    LightEnvironmentClass full, legacy, independent;
    const Vector3 center(0,0,0), ambient(.12f,.17f,.22f);
    for (auto *e : {&full,&legacy,&independent}) { e->Reset(center,ambient); e->Set_Fill_Intensity(.35f); }
    // More lights than the environment holds exercises ranking/overflow and fill, not a trivial copy.
    for (unsigned i=0;i<7;++i) {
        LightClass light(LightClass::DIRECTIONAL);
        light.Set_Diffuse(Vector3(.1f+i*.07f,.2f+i*.04f,.3f+i*.03f));
        light.Set_Ambient(Vector3(.01f,.02f,.03f));
        Matrix3D transform(1); transform.Rotate_X(.12f*i); light.Set_Transform(transform);
        for (auto *e : {&full,&legacy,&independent}) e->Add_Light(light);
    }
    full.Capture_Static_Lighting();
    for (unsigned i=0;i<9;++i) {
        LightClass light(LightClass::POINT);
        light.Set_Position(Vector3(2+i,3,-1));
        light.Set_Diffuse(Vector3(.8f+i*.1f,.4f,.2f)); light.Set_Ambient(Vector3(.2f,.1f,.03f));
        light.Set_Far_Attenuation_Range(2,30); light.Set_Flag(LightClass::FAR_ATTENUATION,true);
        full.Add_Light(light); legacy.Add_Light(light);
    }
    Matrix3D camera(1);
    full.Pre_Render_Update(camera); legacy.Pre_Render_Update(camera); independent.Pre_Render_Update(camera);
    CHECK(same_environment(full,legacy));
    LightEnvironmentClass alternate;
    CHECK(full.Get_Static_Lighting(alternate)); CHECK(same_environment(alternate,independent));
    LightEnvironmentClass queued=full, assigned; assigned=full;
    CHECK(queued==full && assigned==full);
    full.Reset(center,Vector3(1,0,0));
    CHECK(!full.Get_Static_Lighting(alternate));
    CHECK(queued.Get_Static_Lighting(alternate)); CHECK(same_environment(alternate,independent));
    auto changed=queued; changed.Capture_Static_Lighting(); CHECK(!(changed==queued));
    // A second camera resolves the saved raw static inputs, never a previous fill light again.
    camera.Rotate_Y(.4f); queued.Pre_Render_Update(camera);
    CHECK(queued.Get_Static_Lighting(alternate)); CHECK(same_environment(alternate,independent));
    Vector3 replacement(.2f,.2f,.2f); assigned.Set_Output_Ambient(replacement);
    CHECK(!assigned.Get_Static_Lighting(alternate));
}

static D3DLIGHT9 point_light()
{
    D3DLIGHT9 light={}; light.Type=D3DLIGHT_POINT;
    light.Position.z=-.5f; light.Diffuse.r=light.Diffuse.g=light.Diffuse.b=.7f;
    light.Range=4;light.Attenuation0=1;light.Attenuation2=.5f;
    return light;
}
static RenderLighting::VertexEnvironment full_packet()
{
    RenderLighting::VertexEnvironment packet={}; packet.Count=1;packet.Ambient=0x00404040;
    auto &light=packet.Lights[0];light.Type=RenderLighting::LightType::Point;
    light.Position[2]=-.5f;light.Diffuse[0]=light.Diffuse[1]=light.Diffuse[2]=.7f;
    light.Range=4;light.Attenuation[0]=1;light.Attenuation[2]=.5f;
    return packet;
}
static void apply_packet(PosixDevice9 *device,bool eligible=true)
{
    const auto full=full_packet();RenderLighting::VertexEnvironment base={};base.Ambient=full.Ambient;
    device->Set_Light_Environments(full,eligible?&base:nullptr);
    D3DLIGHT9 light=point_light();device->SetLight(0,&light);device->LightEnable(0,1);
    for(unsigned i=1;i<PosixDevice9::LIGHT_COUNT;++i)device->LightEnable(i,0);
    device->SetRenderState(D3DRS_AMBIENT,full.Ambient);
}
static PosixDevice9 *create_device(IDirect3D9 *&api)
{
    api=Direct3DCreate9(D3D_SDK_VERSION);if(!api)return nullptr;
    D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=p.BackBufferHeight=64;
    p.BackBufferFormat=D3DFMT_X8R8G8B8;p.Windowed=1;p.EnableAutoDepthStencil=1;p.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9 *device=nullptr;
    if(api->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&device)!=D3D_OK)return nullptr;
    return static_cast<PosixDevice9*>(device);
}
static void check_packets(PosixDevice9 *device)
{
    device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL);device->SetRenderState(D3DRS_LIGHTING,1);
    apply_packet(device); CHECK(device->Static_Lighting_Matches());
    VertexPipelineDescription full,base;
    CHECK(device->Build_Vertex_Description(full));CHECK(full.LightCount==1);
    CHECK(device->Build_Vertex_Description(base,nullptr,true));CHECK(base.LightCount==0);
    CHECK(device->Build_Vertex_Description(full));CHECK(full.LightCount==1); // canonical never changed
    SdlVertexConstants a,b;SdlPixelConstants p;
    device->Build_Constants(a,p);device->Build_Constants(b,p,true);
    CHECK(a.LightFields[0][2][0]==.7f && b.LightFields[0][2][0]==0);
    device->SetRenderState(D3DRS_AMBIENT,0);CHECK(!device->Static_Lighting_Matches());
    CHECK(device->Build_Vertex_Description(base,nullptr,true));CHECK(base.LightCount==1);
    apply_packet(device,false);CHECK(!device->Static_Lighting_Matches());
    apply_packet(device);CHECK(device->Static_Lighting_Matches());
    D3DLIGHT9 changed=point_light();changed.Diffuse.r=.1f;device->SetLight(0,&changed);
    CHECK(!device->Static_Lighting_Matches());
    apply_packet(device);RenderLighting::VertexEnvironment empty={};
    device->Set_Light_Environments(empty,nullptr);CHECK(!device->Static_Lighting_Matches());
    device->Set_Hemi_Ambient(true);CHECK(device->Build_Vertex_Description(full));CHECK(full.HemiAmbient);
    D3DMATRIX view={};view._11=1;view._23=1;view._32=-1;view._44=1;
    device->SetTransform(D3DTS_VIEW,&view);device->Build_Constants(a,p);
    CHECK(a.MaterialPower[1]==0 && a.MaterialPower[2]==-1 && a.MaterialPower[3]==0);
    device->SetFVF(D3DFVF_XYZRHW);CHECK(device->Build_Vertex_Description(full));CHECK(!full.HemiAmbient);
    device->Set_Hemi_Ambient(false);
    const float headlights[8]={1,2,3,40,0,0,-1,.85f},gain[3]={2,3,4};
    const float blast[12]={4,5,6,50,.2f,.3f,.4f,10,.01f,.02f,.03f,0};
    device->Set_Headlights(headlights,1,gain);device->Set_Blast_Lights(blast,1);
    SdlEnginePixelConstants lights={};device->Build_Light_Constants(lights);
    CHECK(lights.HeadlightParameters[0]==1 && lights.HeadlightParameters[2]==3);
    CHECK(lights.HeadlightPosition[0][3]==40 && lights.HeadlightDirection[0][3]==.85f);
    CHECK(lights.BlastLightParameters[0]==1 && lights.BlastLightDiffuse[0][3]==10);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);
    lights={};device->Build_Light_Constants(lights);CHECK(lights.BlastLightParameters[0]==0);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    lights={};device->Build_Light_Constants(lights,false);CHECK(lights.BlastLightParameters[0]==0);
    D3DMATRIX camera={},projection={};camera._11=camera._22=camera._33=camera._44=1;
    projection=camera;projection._34=1;
    device->SetTransform(D3DTS_VIEW,&camera);device->SetTransform(D3DTS_PROJECTION,&projection);
    lights={};device->Build_Light_Constants(lights);CHECK(lights.BlastLightParameters[0]==0);
    device->Set_Headlights(nullptr,0,nullptr);device->Set_Blast_Lights(nullptr,0);
    lights={};device->Build_Light_Constants(lights);CHECK(lights.HeadlightParameters[0]==0 && lights.BlastLightParameters[0]==0);
}

struct Vertex {float x,y,z,nx,ny,nz;};
static const Vertex plane[]={{-1,-1,.5f,0,0,-1},{1,-1,.5f,0,0,-1},{-1,1,.5f,0,0,-1},
    {-1,1,.5f,0,0,-1},{1,-1,.5f,0,0,-1},{1,1,.5f,0,0,-1}};
static int render(PosixDevice9 *device)
{
    device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,plane,sizeof(Vertex))==D3D_OK);
    std::vector<uint8_t> pixels;CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
    return pixels.size()==64*64*4?pixels[(32*64+32)*4+2]:-1;
}
static void configure(PosixDevice9 *device)
{
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    for(auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION})device->SetTransform(transform,&identity);
    D3DVIEWPORT9 viewport={0,0,64,64,0,1};device->SetViewport(&viewport);
    device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL);
    device->SetRenderState(D3DRS_LIGHTING,1);device->SetRenderState(D3DRS_COLORVERTEX,0);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ZENABLE,1);
    device->SetRenderState(D3DRS_ZWRITEENABLE,1);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_FOGENABLE,0);device->SetRenderState(D3DRS_SPECULARENABLE,0);
    D3DMATERIAL9 material={};material.Ambient.r=material.Ambient.g=material.Ambient.b=1;
    material.Diffuse.r=material.Diffuse.g=material.Diffuse.b=material.Diffuse.a=1;
    device->SetMaterial(&material);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
    apply_packet(device);
}
static void check_gpu(PosixDevice9 *device)
{
    configure(device);
    CHECK(PosixPerPixelLightingAvailable(device));
    CHECK(PosixHemisphericAmbientAvailable(device));
    SunShadow::SceneScope scene(device,true);
    const float blast[]={0,0,-.5f,4,.2f,.2f,.2f,1,0,0,0,0};device->Set_Blast_Lights(blast,1);

    device->Set_Hemi_Ambient(true);
    CHECK(SunShadow::Begin(device,32));
    VertexPipelineDescription captureDescription;CHECK(device->Build_Vertex_Description(captureDescription));CHECK(!captureDescription.HemiAmbient);
    device->Set_Hemi_Ambient(false);
    RenderLighting::VertexEnvironment caster={};device->Set_Light_Environments(caster,nullptr);
    device->LightEnable(0,0);device->SetRenderState(D3DRS_AMBIENT,0);
    device->Set_Blast_Lights(nullptr,0);
    CHECK(SunShadow::End(device));CHECK(device->Static_Lighting_Matches());
    CHECK(device->Build_Vertex_Description(captureDescription));CHECK(captureDescription.HemiAmbient);
    device->Set_Hemi_Ambient(false);
    SdlEnginePixelConstants restored={};device->Build_Light_Constants(restored);CHECK(restored.BlastLightParameters[0]==1);
    CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0));CHECK(device->Lights_Per_Pixel());
    const int lit=render(device);CHECK(lit>=75 && lit<=78); // .25098 * (1+.2), once
    apply_packet(device,false);const int excluded=render(device);CHECK(excluded>lit+15);
    apply_packet(device);CHECK(render(device)==lit);
    {SunShadow::ReceiverExclusion reflection(device);CHECK(render(device)==excluded);}
    CHECK(render(device)==lit);
    // Zeroing both the frame packet and object environment removes all light, without stale slots.
    device->Set_Blast_Lights(nullptr,0);
    RenderLighting::VertexEnvironment empty={};empty.Ambient=0x00404040;
    device->Set_Light_Environments(empty,&empty);for(unsigned i=0;i<PosixDevice9::LIGHT_COUNT;++i)device->LightEnable(i,0);
    const int unlit=render(device);CHECK(unlit>=63 && unlit<=65);
    const float headlight[]={0,0,-.5f,4,0,0,1,.85f},gain[]={.3f,.3f,.3f};
    device->Set_Headlights(headlight,1,gain);const int head=render(device);CHECK(head>=75 && head<=78);
    device->Set_Headlights(nullptr,0,nullptr);CHECK(render(device)==unlit);
    // Existing shared formula varies ambient by world normal and returns exactly to the old result when disabled.
    device->Set_Hemi_Ambient(true);const int down=render(device);
    Vertex up[6];std::memcpy(up,plane,sizeof(up));for(auto &v:up)v.nz=1;
    device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,up,sizeof(Vertex))==D3D_OK);
    std::vector<uint8_t> pixels;CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
    CHECK(pixels.size()==64*64*4);
    if(pixels.size()==64*64*4) { const int brighter=pixels[(32*64+32)*4+2];CHECK(brighter>unlit && down<unlit); }
    device->Set_Hemi_Ambient(false);CHECK(render(device)==unlit);
    CHECK(device->Commit_Terrain_Lighting());
    device->SetRenderState(D3DRS_LIGHTING,0);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    device->Set_Light_Environments(empty,nullptr); // excluded-model policy must not disable terrain
    const float groundBlast[]={0,0,-.5f,4,.2f,.2f,.2f,1,0,0,0,0};device->Set_Blast_Lights(groundBlast,1);
    IDirect3DTexture9 *white=nullptr;CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr)==D3D_OK);
    D3DLOCKED_RECT lock={};CHECK(white->LockRect(0,&lock,nullptr,0)==D3D_OK);
    *static_cast<unsigned *>(lock.pBits)=0xffffffff;CHECK(white->UnlockRect(0)==D3D_OK);
    for(unsigned i=0;i<5;++i)device->SetTexture(i,white);
    const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};IDirect3DPixelShader9 *ground=nullptr;
    CHECK(device->CreatePixelShader(tokens,&ground)==D3D_OK);device->SetPixelShader(ground);
    struct G {float x,y,z;unsigned colour;};G vertices[6];
    for(unsigned i=0;i<6;++i)vertices[i]={plane[i].x,plane[i].y,plane[i].z,0xff404040};
    for(const char *name:{"terrain.pso","terrainnoise.pso","terrainnoise2.pso","fterrain.pso",
        "fterrain0.pso","fterrainnoise.pso","fterrainnoise2.pso","roadnoise2.pso"}) {
        PosixDevice_Name_Shader(ground,name);device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
        CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(G))==D3D_OK);
        CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
        const int value=pixels[(32*64+32)*4+2];CHECK(value>=75 && value<=78);
        std::printf("native ground %s: pointlight=%d\n",name,value);
    }
    device->SetPixelShader(nullptr);ground->Release();for(unsigned i=0;i<5;++i)device->SetTexture(i,nullptr);white->Release();
    std::printf("native lighting: pixel=%d legacy/excluded=%d zero=%d headlight=%d\n",lit,excluded,unlit,head);
}
static int ground_sample(PosixDevice9 *device,unsigned colour)
{
    device->SetRenderState(D3DRS_LIGHTING,0);device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
    IDirect3DTexture9 *white=nullptr;CHECK(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr)==D3D_OK);
    D3DLOCKED_RECT lock={};CHECK(white->LockRect(0,&lock,nullptr,0)==D3D_OK);*static_cast<unsigned*>(lock.pBits)=0xffffffff;
    CHECK(white->UnlockRect(0)==D3D_OK);for(unsigned i=0;i<5;++i)device->SetTexture(i,white);
    const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};IDirect3DPixelShader9 *shader=nullptr;
    CHECK(device->CreatePixelShader(tokens,&shader)==D3D_OK);PosixDevice_Name_Shader(shader,"fterrain0.pso");device->SetPixelShader(shader);
    struct G{float x,y,z;unsigned colour;};G vertices[6];
    for(unsigned i=0;i<6;++i)vertices[i]={plane[i].x,plane[i].y,plane[i].z,colour};
    device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(G))==D3D_OK);
    std::vector<uint8_t> pixels;CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
    const int value=pixels[(32*64+32)*4+2];
    device->SetPixelShader(nullptr);shader->Release();for(unsigned i=0;i<5;++i)device->SetTexture(i,nullptr);white->Release();
    return value;
}
static void check_terrain_policy()
{
    const float blast[]={0,0,-.5f,4,.2f,.2f,.2f,1,0,0,0,0};
    for(bool before:{true,false}) {
        IDirect3D9 *api=nullptr;auto *device=create_device(api);CHECK(device && device->Create_Gpu_Frame(true)==D3D_OK);
        configure(device);device->Set_Blast_Lights(blast,1);
        {
            SunShadow::SceneScope scene(device,true);
            CHECK(SunShadow::Begin(device,32));
            Vertex blocker[6];std::memcpy(blocker,plane,sizeof(blocker));for(auto &v:blocker)v.z=.25f;
            CHECK(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,blocker,sizeof(Vertex))==D3D_OK);
            CHECK(SunShadow::End(device));CHECK(SunShadow::Parameters(device,0,.5f,0,0,0,1,0));
            if(!before) {
                CHECK(device->Commit_Terrain_Lighting());
                const int shadowed=ground_sample(device,0xff404040);CHECK(shadowed>=37 && shadowed<=40);
                configure(device); // leave the committed ground policy intact; prepare an optional model receiver
            }
            Sdl_Sun_Shadow_Fault_Point()="receiver-program";CHECK(render(device)>=103);
            Sdl_Sun_Shadow_Fault_Point()=nullptr;
            CHECK(device->Commit_Terrain_Lighting()==!before);
            const int value=ground_sample(device,before?0xff696969:0xff404040);
            CHECK(value==(before?105:77));CHECK(device->Draw_Refusals().empty());
            std::printf("terrain optional failure %s commit: %s packet=%d\n",before?"before":"after",before?"CPU":"pixel",value);
        }
        {
            SunShadow::SceneScope next(device,true);CHECK(!SunShadow::Begin(device,32));
            CHECK(!device->Commit_Terrain_Lighting());CHECK(ground_sample(device,0xff696969)==105);
        }
        device->Release();api->Release();
    }
    IDirect3D9 *api=nullptr;auto *device=create_device(api);CHECK(device && device->Create_Gpu_Frame(true)==D3D_OK);
    configure(device);device->Set_Blast_Lights(blast,1);
    {SunShadow::SceneScope scene(device,true);CHECK(!device->Commit_Terrain_Lighting());CHECK(ground_sample(device,0xff696969)==105);}
    for(unsigned i=0;i<2;++i) {
        SunShadow::SceneScope scene(device,true);CHECK(SunShadow::Begin(device,32));CHECK(SunShadow::End(device));
        CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0));CHECK(device->Commit_Terrain_Lighting());
        CHECK(ground_sample(device,0xff404040)==77);
        {SunShadow::ReceiverExclusion overlay(device);CHECK(ground_sample(device,0xff404040)==64);}
        device->Use_Terrain_Cpu_Lighting();CHECK(!device->Commit_Terrain_Lighting());CHECK(ground_sample(device,0xff696969)==105);
    }
    std::puts("terrain CPU/pixel/CPU/new-scene transitions: exactly-once packets; overlays excluded");
    device->Release();api->Release();
}

static void check_receiver_failures()
{
    for(const char *fault:{"receiver-program","receiver-pipeline"})for(bool late:{false,true}) {
        IDirect3D9 *api=nullptr;auto *device=create_device(api);CHECK(device);
        if(!device || device->Create_Gpu_Frame(true)!=D3D_OK) { if(device)device->Release();if(api)api->Release();CHECK(false);continue; }
        configure(device);
        {
            SunShadow::SceneScope scene(device,true);
            CHECK(SunShadow::Begin(device,32));CHECK(SunShadow::End(device));
            CHECK(SunShadow::Parameters(device,0,0,0,0,0,1,0));
            const float blast[]={0,0,-.5f,4,.2f,.2f,.2f,1,0,0,0,0};device->Set_Blast_Lights(blast,1);
            if(late) {
                CHECK(render(device)>=75 && render(device)<=78);
                device->SetRenderState(D3DRS_ALPHATESTENABLE,1);device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);
                device->SetRenderState(D3DRS_ALPHAREF,8);
            }
            Sdl_Sun_Shadow_Fault_Point()=fault;
            const unsigned before=device->Draws_Recorded();
            const int fallback=render(device);CHECK(fallback>=103 && fallback<=107);
            CHECK(device->Draws_Recorded()==before+1);CHECK(device->Draw_Refusals().empty());
            Sdl_Sun_Shadow_Fault_Point()=nullptr;CHECK(render(device)==fallback);
            std::printf("lighting %s %s: full legacy fallback=%d\n",fault,late?"late":"early",fallback);
        }
        device->Release();api->Release();
    }
}

static void check_real_sorted_queue(PosixDevice9 *device)
{
    WW3D::Enable_Sorting(true);
    DX8Wrapper::Set_Transform(D3DTS_WORLD,Matrix3D(1));DX8Wrapper::Set_Transform(D3DTS_VIEW,Matrix3D(1));
    DX8Wrapper::Set_Projection_Transform_With_Z_Bias(Matrix4x4(true),0,1);
    ShaderClass shader;shader.Set_Texturing(ShaderClass::TEXTURING_DISABLE);shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
    shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ZERO);
    DX8Wrapper::Set_Shader(shader);
    VertexMaterialClass *material=new VertexMaterialClass;
    material->Set_Lighting(true);material->Set_Ambient(1,1,1);material->Set_Diffuse(0,0,0);
    material->Set_Opacity(1);DX8Wrapper::Set_Material(material);material->Release_Ref();
    DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING,1);DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORVERTEX,0);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_FOGENABLE,0);DX8Wrapper::Set_DX8_Render_State(D3DRS_SPECULARENABLE,0);
    auto *vb=new SortingVertexBufferClass(12);auto *ib=new SortingIndexBufferClass(12);
    {
        VertexBufferClass::WriteLockClass lock(vb);auto *vertices=static_cast<VertexFormatXYZNDUV2*>(lock.Get_Vertex_Array());
        for(unsigned side=0;side<2;++side)for(unsigned i=0;i<6;++i) {
            auto &v=vertices[side*6+i];v={};v.x=(plane[i].x+1)*.5f-1+side;v.y=plane[i].y;
            v.z=.5f;v.nz=-1;v.diffuse=0xffffffff;
        }
    }
    {IndexBufferClass::WriteLockClass lock(ib);for(unsigned i=0;i<12;++i)lock.Get_Index_Array()[i]=i;}
    DX8Wrapper::Set_Vertex_Buffer(vb);DX8Wrapper::Set_Index_Buffer(ib,0);
    LightEnvironmentClass environment;environment.Reset(Vector3(0,0,0),Vector3(.2f,.3f,.4f));
    environment.Capture_Static_Lighting();environment.Pre_Render_Update(Matrix3D(1));
    const auto apply=[] {DX8Wrapper::Apply_Render_State_Changes();DX8Wrapper::Flush_Deferred_D3D9_State();};
    std::vector<uint8_t> pixels;
    const auto read=[&](unsigned x) {
        CHECK(device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),64,64,pixels));
        return int(pixels[(32*64+x)*4+2]);
    };
    DX8Wrapper::Set_Light_Environment(&environment);apply();device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    DX8Wrapper::Draw_Triangles(0,2,0,6);const int referenceFull=read(16);
    DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0);apply();
    DX8Wrapper::Draw_Triangles(6,2,6,6);const int referenceOverride=read(48);CHECK(referenceFull>referenceOverride+20);
    for(bool reverse:{false,true}) {
        DX8Wrapper::Set_Vertex_Buffer(vb);DX8Wrapper::Set_Index_Buffer(ib,0);DX8Wrapper::Set_Shader(shader);
        device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
        for(unsigned side=0;side<2;++side) {
            environment.Reset(Vector3(0,0,0),Vector3(.2f,.3f,.4f));
            environment.Capture_Static_Lighting();environment.Pre_Render_Update(Matrix3D(1));
            DX8Wrapper::Set_Light_Environment(&environment);
            if(bool(side)==reverse)DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0);
            SortingRendererClass::Insert_Triangles(SphereClass(Vector3(0,0,.5f),1),side*6,2,side*6,6);
        }
        // The actual pool must replay the copies after the original stack object and current state change.
        environment.Reset(Vector3(0,0,0),Vector3(1,0,0));DX8Wrapper::Set_Light_Environment(nullptr);
        DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0x00ff0000);
        SortingRendererClass::Flush();
        const int left=read(16),right=read(48);
        CHECK(left==(reverse?referenceFull:referenceOverride));CHECK(right==(reverse?referenceOverride:referenceFull));
        std::printf("real sorted environment %s: left=%d right=%d references=%d/%d\n",reverse?"reversed":"forward",left,right,referenceOverride,referenceFull);
    }
    vb->Release_Ref();ib->Release_Ref();SortingRendererClass::Deinit();
}

static void check_wrapper_cache()
{
    SDL_SetHint("ZH_OFFSCREEN_FRAMES","1");SDL_SetHint("ZH_OFFSCREEN_HZ","0");
    CHECK(DX8Wrapper::Init(nullptr));
    CHECK(WW3D::Set_Render_Device(0,64,64,32,1,false,false,false)==WW3D_ERROR_OK);
    auto *device=static_cast<PosixDevice9*>(DX8Wrapper::_Get_D3D_Device());
    if(!device || !device->Get_Gpu()) return;
    LightEnvironmentClass environment;
    environment.Reset(Vector3(0,0,0),Vector3(.1f,.2f,.3f));
    LightClass sun(LightClass::DIRECTIONAL);sun.Set_Diffuse(Vector3(.3f,.3f,.3f));
    environment.Add_Light(sun);environment.Capture_Static_Lighting();
    LightClass dynamic(LightClass::POINT);dynamic.Set_Position(Vector3(1,2,-1));
    dynamic.Set_Diffuse(Vector3(.8f,.4f,.2f));dynamic.Set_Ambient(Vector3(.2f,.1f,.05f));
    dynamic.Set_Far_Attenuation_Range(2,30);environment.Add_Light(dynamic);
    environment.Pre_Render_Update(Matrix3D(1));
    const LightEnvironmentClass saved=environment;
    const auto apply=[&] { DX8Wrapper::Apply_Render_State_Changes();DX8Wrapper::Flush_Deferred_D3D9_State(); };
    DX8Wrapper::Set_Light_Environment(&environment);apply();
    CHECK(device->Static_Lighting_Matches());
    RenderStateStruct queued,assigned;
    DX8Wrapper::Get_Render_State(queued);assigned=queued;
    CHECK(assigned.HasStaticLighting && assigned.FullLighting.Count==2 && assigned.StaticLighting.Count==1);
    VertexPipelineDescription description;
    device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL);device->SetRenderState(D3DRS_LIGHTING,1);
    CHECK(device->Build_Vertex_Description(description));CHECK(description.LightCount==2);
    CHECK(device->Build_Vertex_Description(description,nullptr,true));CHECK(description.LightCount==1);
    // The full caller's stack can be reset/reused while both queued packets retain their own values.
    environment.Reset(Vector3(0,0,0),Vector3(.7f,.7f,.7f));
    DX8Wrapper::Set_Light_Environment(&environment);apply();CHECK(!device->Static_Lighting_Matches());
    DX8Wrapper::Apply_Queued_Light_Environment(assigned);
    for(unsigned i=0;i<4;++i)DX8Wrapper::Set_Light(i,assigned.LightEnable[i]?&assigned.Lights[i]:nullptr);
    apply();CHECK(device->Static_Lighting_Matches());
    // Equal canonical setters may be deduplicated; explicit packet handoff remains coherent.
    environment=saved;DX8Wrapper::Set_Light_Environment(&environment);apply();CHECK(device->Static_Lighting_Matches());
    DX8Wrapper::Set_DX8_Render_State(D3DRS_AMBIENT,0);apply();CHECK(!device->Static_Lighting_Matches());
    RenderStateStruct overridden;DX8Wrapper::Get_Render_State(overridden);
    DX8Wrapper::Set_Light_Environment(&environment);apply();CHECK(device->Static_Lighting_Matches());
    // Set_Render_State is also the non-pool native-buffer sorted replay boundary.
    DX8Wrapper::Set_Render_State(overridden);apply();CHECK(!device->Static_Lighting_Matches());
    SdlVertexConstants constants;SdlPixelConstants pixel;
    device->Build_Constants(constants,pixel,true);CHECK(constants.GlobalAmbient[0]==0);
    DX8Wrapper::Set_Render_State(assigned);apply();CHECK(device->Static_Lighting_Matches());
    DX8Wrapper::Set_Light_Environment(&environment);apply();CHECK(device->Static_Lighting_Matches());
    DX8Wrapper::Set_Light_Environment(nullptr);apply();CHECK(!device->Static_Lighting_Matches());
    CHECK(device->Build_Vertex_Description(description));CHECK(description.LightCount==2);
    check_real_sorted_queue(device);
    DX8Wrapper::Shutdown();
}

int main(int argc,char **argv)
{
    initMemoryManager();WWMath::Init();
    check_snapshots();
    IDirect3D9 *api=nullptr;auto *device=create_device(api);CHECK(device);
    CHECK(!PosixPerPixelLightingAvailable(nullptr));
    CHECK(!PosixHemisphericAmbientAvailable(nullptr));
    if(device) { CHECK(!PosixPerPixelLightingAvailable(device));CHECK(!PosixHemisphericAmbientAvailable(device));check_packets(device); }
    int result=failures?1:0;
    if(!failures && argc>1 && !std::strcmp(argv[1],"--gpu")) {
        setenv("ZH_SUN_SHADOW_MODE","mapped",1);
        if(!SDL_Init(SDL_INIT_VIDEO) || device->Create_Gpu_Frame(true)!=D3D_OK)result=77;
        else {check_gpu(device);result=failures?1:0;}
    }
    if(device)device->Release();if(api)api->Release();
    if(result==0 && argc>1 && !std::strcmp(argv[1],"--gpu")) {check_receiver_failures();check_terrain_policy();check_wrapper_cache();result=failures?1:0;}
    WWMath::Shutdown();SDL_Quit();
    return result;
}
