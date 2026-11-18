#include "test_harness.h"
#include "ffvertex.h"
#include "engineshader.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include "Platform/WaterFftSampling.h"
#include <cstdio>
TEST(heading_shader_stages_compile_and_disabled_path_has_no_field_binding) {
    for(bool heading:{false,true}) {
        VertexPipelineDescription description={};description.FVF=0x252;description.StageCount=4;
        for(unsigned i=0;i<4;++i)description.Stages[i].TextureCoordinateIndex=i<2?i:0;
        std::string vertex,pixel;PixelPipelineDescription pipeline={};
        CHECK(VertexShader_Generate(description,VERTEX_SHADER_TARGET_D3D11,vertex));
        CHECK(WaterFftSampling::WrapVertex(vertex,true,heading));CHECK(SDL3_Shader_Retarget(vertex,true));
        CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,pixel,COMBINER_SHADER_TARGET_SDL3_GPU,heading));
        CHECK((vertex.find("FftHeadingTexture")!=std::string::npos)==heading);
        CHECK((pixel.find("FftHeadingTexture")!=std::string::npos)==heading);
        for(bool isVertex:{true,false}) {
            std::vector<unsigned char> spirv;std::string log,msl;
            const bool compiled=SDL3_Compile_HLSL_To_SPIRV(isVertex?vertex:pixel,isVertex,spirv,log);
            if(!compiled)std::printf("heading=%d vertex=%d: %s\n",heading,isVertex,log.c_str());
            CHECK(compiled);if(!compiled)continue;
            unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,isVertex,samplers,buffers);
            CHECK_EQ(samplers,isVertex?(heading?5u:4u):(heading?16u:15u));CHECK_EQ(buffers,isVertex?2u:4u);
            CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,isVertex,msl,log));
        }
    }
}
