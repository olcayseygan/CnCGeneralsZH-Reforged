#include "test_harness.h"
#include "sdl3shadercompile.h"
#include "sdl3target.h"
#include "Platform/WaterFftSampling.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>

// Execute the production clean incident-field helpers. The expected contact
// gradient is a finite difference of the wind-scaled bounded height; expected
// whitecaps use the determinant of the bounded horizontal mapping.
TEST(clean_material_wind_incident_field_matches_bounded_geometry) {
    if(!SDL_Init(SDL_INIT_VIDEO)) {std::printf("SKIPPED clean GPU: %s\n",SDL_GetError());std::exit(77);}
    struct Resources {
        SDL_GPUDevice *device=nullptr;SDL_GPUShader *vs=nullptr,*ps=nullptr;
        SDL_GPUGraphicsPipeline *pipeline=nullptr;SDL_GPUTexture *target=nullptr,*dummy=nullptr;SDL_GPUSampler *sampler=nullptr;SDL_GPUTransferBuffer *transfer=nullptr;
        ~Resources(){if(device){SDL_WaitForGPUIdle(device);if(transfer)SDL_ReleaseGPUTransferBuffer(device,transfer);
            if(target)SDL_ReleaseGPUTexture(device,target);if(dummy)SDL_ReleaseGPUTexture(device,dummy);if(sampler)SDL_ReleaseGPUSampler(device,sampler);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);SDL_DestroyGPUDevice(device);}SDL_Quit();}
    } r;
    r.device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,false,nullptr);
    if(!r.device){std::printf("SKIPPED clean GPU: %s\n",SDL_GetError());std::exit(77);}
    const std::string vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3:-1,id==2?3:-1,0,1);}";
    std::string ps=WaterFftSampling::UniformSource("Water",0,false,true);
    for(unsigned i=8;i<14;++i)ps+="Texture2D Texture"+std::to_string(i)+":register(t"+std::to_string(i)+");SamplerState Sampler"+std::to_string(i)+":register(s"+std::to_string(i)+");\n";
    ps+=WaterFftSampling::PixelSource(WaterFftSpectrum::ForResolution(128),false,true,true);
    ps+=R"HLSL(
cbuffer Fixture : register(b1) {float4 InputDisplacement;float4 InputX;float4 InputY;};
float4 main(float4 position:SV_Position):SV_Target0 {
    FftSurface q=(FftSurface)0;q.displacement=InputDisplacement.xyz;
    q.derivativeX=InputX.xyz;q.derivativeY=InputY.xyz;
    fftCleanFinish(q,float4(40,0,0,1),float3(0,0,0));
    float gain=position.x<1?0:position.x<2?.5:position.x<3?.8:1;
    fftResistWind(q,gain);return float4(q.contactIncident,q.incidentCompression);
}
)HLSL";
    CHECK(SDL3_Shader_Retarget(ps,false));
    std::vector<unsigned char> spirv;std::string log;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,log);CHECK(ok);if(!ok)return;
    r.vs=SDL3_Create_Shader(r.device,spirv,true,log);CHECK(r.vs);if(!r.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,log);if(!ok)std::printf("%s\n",log.c_str());CHECK(ok);if(!ok)return;
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);
    CHECK(samplers<=16u);CHECK_EQ(buffers,2u);
    r.ps=SDL3_Create_Shader(r.device,spirv,false,log);CHECK(r.ps);if(!r.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;
    ti.width=4;ti.height=ti.layer_count_or_depth=ti.num_levels=1;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    // The shared source retains declarations through glslang. Supply valid
    // descriptors for every reflected slot, including inactive FFT/atlas slots.
    ti.width=1;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    r.dummy=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.dummy);if(!r.dummy)return;
    SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r.sampler=SDL_CreateGPUSampler(r.device,&si);CHECK(r.sampler);if(!r.sampler)return;
    SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;bi.size=4*4*sizeof(float);
    r.transfer=SDL_CreateGPUTransferBuffer(r.device,&bi);CHECK(r.transfer);if(!r.transfer)return;
    const float fixture[3][4]={{80,-40,25,0},{2,.8f,1.5f,0},{-.4f,-1.5f,-.7f,0}};
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
    SDL_GPUColorTargetInfo out={};out.texture=r.target;out.load_op=SDL_GPU_LOADOP_DONT_CARE;out.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&out,1,nullptr);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
    SDL_BindGPUGraphicsPipeline(pass,r.pipeline);
    SDL_GPUTextureSamplerBinding binding[16];for(auto &b:binding)b={r.dummy,r.sampler};
    if(samplers)SDL_BindGPUFragmentSamplers(pass,0,binding,samplers);
    // SDL's uniform count is highest slot+1. Bind the unused b0 gap as well;
    // leaving that descriptor unbound is invalid even though this probe reads b1 only.
    const float unused[4]={};SDL_PushGPUFragmentUniformData(commands,0,unused,sizeof(unused));
    SDL_PushGPUFragmentUniformData(commands,1,fixture,sizeof(fixture));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);if(!copy){SDL_CancelGPUCommandBuffer(commands);return;}
    SDL_GPUTextureRegion region={};region.texture=r.target;region.w=4;region.h=region.d=1;
    SDL_GPUTextureTransferInfo to={};to.transfer_buffer=r.transfer;to.pixels_per_row=4;to.rows_per_layer=1;
    SDL_DownloadFromGPUTexture(copy,&region,&to);SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);if(!fence)return;
    CHECK(SDL_WaitForGPUFences(r.device,true,&fence,1));SDL_ReleaseGPUFence(r.device,fence);
    const float *mapped=static_cast<const float *>(SDL_MapGPUTransferBuffer(r.device,r.transfer,false));CHECK(mapped);if(!mapped)return;
    float result[16];std::memcpy(result,mapped,sizeof(result));SDL_UnmapGPUTransferBuffer(r.device,r.transfer);
    const double bound[3]={WaterFftGeometry::MaxHorizontalDisplacement/std::sqrt(2.0),WaterFftGeometry::MaxHorizontalDisplacement/std::sqrt(2.0),WaterFftGeometry::MaxVerticalDisplacement};
    const double gains[4]={0,.5,.8,1},epsilon=.001;
    for(unsigned i=0;i<4;++i) {
        const auto bounded=[&](int component,double x,double y) {
            const double raw=fixture[0][component]+x*fixture[1][component]+y*fixture[2][component];
            return bound[component]*std::tanh(raw*gains[i]/bound[component]);
        };
        double dx[3],dy[3];for(unsigned c=0;c<3;++c){dx[c]=(bounded(c,epsilon,0)-bounded(c,-epsilon,0))/(2*epsilon);dy[c]=(bounded(c,0,epsilon)-bounded(c,0,-epsilon))/(2*epsilon);}
        CHECK_NEAR(result[i*4],bounded(2,0,0),.0001);
        CHECK_NEAR(result[i*4+1],dx[2],.0001);CHECK_NEAR(result[i*4+2],dy[2],.0001);
        CHECK_NEAR(result[i*4+3],(1+dx[0])*(1+dy[1])-dx[1]*dy[0],.0001);
    }
}
