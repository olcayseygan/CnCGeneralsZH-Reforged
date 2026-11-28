#include "test_harness.h"
#include "ffvertex.h"
#include "engineshader.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterPerformanceSettings.h"
#include <cstdio>

TEST(clean_shader_stages_compile_with_one_shared_atlas_binding) {
    const auto saved=WaterPerformanceSettings::Applied();
    for(unsigned resolution:{128u,256u}) {
      WaterPerformanceSettings::Applied().activeFftResolution=resolution;
      for(bool heading:{false,true})for(bool clean:{false,true}) {
        VertexPipelineDescription description={};description.FVF=0x252;description.StageCount=4;
        for(unsigned i=0;i<4;++i)description.Stages[i].TextureCoordinateIndex=i<2?i:0;
        std::string vertex,pixel;PixelPipelineDescription pipeline={};
        CHECK(VertexShader_Generate(description,VERTEX_SHADER_TARGET_D3D11,vertex));
        CHECK(WaterFftSampling::WrapVertex(vertex,true,heading,clean));
        CHECK(SDL3_Shader_Retarget(vertex,true));
        CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,pixel,COMBINER_SHADER_TARGET_SDL3_GPU,heading,clean));
        CHECK((vertex.find("FftCleanAtlas :")!=std::string::npos)==clean);
        CHECK((pixel.find("FftCleanAtlas :")!=std::string::npos)==clean);
        CHECK((vertex.find("FftHeadingTexture")!=std::string::npos)==(heading&&!clean));
        CHECK((pixel.find("FftHeadingTexture")!=std::string::npos)==(heading&&!clean));
        if(clean) {
            // The provider is assembled explicitly: the legacy raw decode and
            // analytic heading evaluator are absent from this compiled variant.
            CHECK(pixel.find("FftSurface fftDecodeSamples(")==std::string::npos);
            CHECK(pixel.find("float3 waterWave(")==std::string::npos);
            CHECK(pixel.find("float4 waterOceanSurface(")==std::string::npos);
            CHECK(pixel.find("float3 causticWave=float3(0,spectral.derivativeX.z,spectral.derivativeY.z)")!=std::string::npos);
            CHECK(pixel.find("float4 cleanDepth=spectral.mediumDepth")!=std::string::npos);
            CHECK(vertex.find("float originalHeight=input.Position.z-input.TexCoord1.y")!=std::string::npos);
            CHECK(pixel.find("float continuousHeight=baseLevel+wave.x;")!=std::string::npos);
            CHECK(pixel.find("float2 slope=baseSlope+wave.yz;")!=std::string::npos);
            CHECK(pixel.find("float3 wave=float3(input.Specular.z,fftParameterGradient(input.Specular.z,parameterDx,parameterDy))")!=std::string::npos);
            CHECK(pixel.find("fftSwellField")==std::string::npos);
            CHECK(pixel.find("#define WATER_CLEAN_MAIN 1")!=std::string::npos);
            CHECK(pixel.find("fftCleanWalk(physical,shading,p,depth,lod,tiles,true)")!=std::string::npos);
            CHECK(pixel.find("fftRawAndShadingSurface(reflected.samplePosition")!=std::string::npos);
            CHECK(pixel.find("fftResistWind(value,windGain);fftResistWind(shading,windGain)")!=std::string::npos);
            CHECK(pixel.find("fftResistWind(incident,windGain);fftResistWind(shadingIncident,windGain)")!=std::string::npos);
            CHECK(pixel.find("*windGain*windGain")!=std::string::npos);
            CHECK(vertex.find("fftRawDisplacement(reflected.samplePosition)*modernWaterWindGain(MaterialSurface.w)")!=std::string::npos);
            // The collar already contains the CPU wind gain; applying it again
            // would double resistance at the canonical boundary.
            CHECK(pixel.find("legacyWave*=materialWindGain")==std::string::npos);
            CHECK(pixel.find("normal=modernWaterDampedNormal(normal,MaterialSurface.z)")!=std::string::npos);
            CHECK(pixel.find("roughness=modernWaterSurfaceRoughness(roughness,MaterialSurface.y)")!=std::string::npos);
            CHECK(pixel.find("transmission=modernWaterTurbidTransmission(transmission,MaterialSurface.x,length(bottom-opticalWorld))")!=std::string::npos);
        }
        for(bool isVertex:{true,false}) {
            std::vector<unsigned char> spirv;std::string log,msl;
            const bool compiled=SDL3_Compile_HLSL_To_SPIRV(isVertex?vertex:pixel,isVertex,spirv,log);
            if(!compiled)std::printf("heading=%d clean=%d vertex=%d: %s\n",heading,clean,isVertex,log.c_str());
            CHECK(compiled);if(!compiled)continue;
            unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,isVertex,samplers,buffers);
            CHECK_EQ(samplers,isVertex?((heading||clean)?5u:4u):((heading||clean)?16u:15u));
            CHECK_EQ(buffers,isVertex?2u:4u);
            CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,isVertex,msl,log));
        }
    }
    }
    WaterPerformanceSettings::Applied()=saved;
}
TEST(clean_provider_keeps_river_shader_identical) {
    PixelPipelineDescription pipeline={};std::string standard,requested;
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_RIVER,pipeline,standard,COMBINER_SHADER_TARGET_SDL3_GPU,false,false));
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_RIVER,pipeline,requested,COMBINER_SHADER_TARGET_SDL3_GPU,true,true));
    CHECK(standard==requested);
}
TEST(clean_provider_leaves_direct3d_programs_identical) {
    PixelPipelineDescription pipeline={};
    for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID,ENGINE_SHADER_WATER_RIVER}) {
        std::string standard,requested;
        CHECK(EngineShader_Pixel_Program(program,pipeline,standard,COMBINER_SHADER_TARGET_D3D11,false,false));
        CHECK(EngineShader_Pixel_Program(program,pipeline,requested,COMBINER_SHADER_TARGET_D3D11,true,true));
        CHECK(standard==requested);
        CHECK(requested.find("FftCleanAtlas")==std::string::npos);
    }
}

TEST(clean_shading_threshold_preserves_full_filtering_above_quarter_lod) {
    // This exact operation leaves every selected LOD intact at/above .25 and
    // substitutes mip zero below it. It must precede both the filtered flag and
    // paired sampling, so physical samples are reused throughout the small-LOD band.
    CHECK_EQ(WaterCleanCollapse::ShadingLod,.25f);
    for(unsigned resolution:{128u,256u})for(bool heading:{false,true}) {
        const auto config=WaterFftSpectrum::ForResolution(resolution);
        const auto clean=WaterFftSampling::PixelSource(config,heading,true,true);
        const auto ordinary=WaterFftSampling::PixelSource(config,heading,true,false);
        const std::string ordinarySelection="float3 shadingLod=fftShadingLod(footprint);\n    bool filtered=any(shadingLod>0.0);";
        const std::string cleanSelection="float3 shadingLod=fftShadingLod(footprint);shadingLod*=step(0.25,shadingLod);\n    bool filtered=any(shadingLod>0.0);";
        const size_t selection=clean.find(cleanSelection);
        CHECK(selection!=std::string::npos);
        CHECK(ordinary.find(ordinarySelection)!=std::string::npos);
        CHECK(ordinary.find("shadingLod*=step(")==std::string::npos);
        CHECK(clean.find("shadingLod*=step(",selection+cleanSelection.size())==std::string::npos);
        const size_t sampling=clean.find("fftRawAndShadingSurface(p,shadingLod,value,shading);");
        CHECK(selection<sampling);
        CHECK(clean.find("fftRawAndShadingSurface(reflected.samplePosition,shadingLod,incident,shadingIncident);",sampling)!=std::string::npos);
    }
}
