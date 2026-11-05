/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// The program cache (PosixDevice/Render/SdlProgramCache, A3c) on this machine's GPU, with descriptions
// built by the device's own resolve from state set through D3D9 calls:
//   - the device's defaults make a vertex and a pixel program the device accepts, with one uniform
//     buffer each and at least one texture slot for the modulated stage;
//   - every colour operation the generator implements (ffshader.h's list, as D3D9 values) makes a pixel
//     program, and the three light types a vertex program: each compiles to something the device takes;
//   - asking again is a cache hit, the same shader, nothing built;
//   - a description the generator refuses (BUMPENVMAP) is a refusal with its reason, kept, and not tried
//     again;
//   - the slot lines: slot n is n and n, and a line moves one slot's texture and sampler.
//
// WHAT THIS DOES NOT PROVE: that the programs compute the right thing.  That is the reference's (A3b).

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlProgramCache.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "engineshader.h"
#include "Platform/EngineShaderName.h"
#include "Platform/WaterPerformanceSettings.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static PosixDevice9 *make_device(IDirect3D9 *&d3d)
{
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 64;
	parameters.BackBufferHeight = 64;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *device = NULL;
	if (d3d == NULL || Render_Failed(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &device))) return NULL;
	return static_cast<PosixDevice9 *>(device);
}

static void check_slot_lines()
{
	std::vector<int> textures, samplers;
	Sdl_Read_Slot_Lines("float4 main() {}\n", 3, textures, samplers);
	CHECK(textures.size() == 3 && textures[2] == 2 && samplers[2] == 2);
	Sdl_Read_Slot_Lines("// SDL3 slot 6: texture t4, sampler state s1\n// SDL3 slot 4: texture t4, sampler state s0\n", 7, textures, samplers);
	CHECK(textures[6] == 4 && samplers[6] == 1 && textures[4] == 4 && samplers[4] == 0 && textures[5] == 5 && samplers[5] == 5);
}

struct WaterShaderNameProbe : PosixDevice9 { using PosixDevice9::Engine_Name_Of; };

static void check_live_water_style_programs(PosixDevice9 *device,SdlProgramCache &cache)
{
    const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};
    IDirect3DPixelShader9 *shader=NULL;
    CHECK(device->CreatePixelShader(tokens,&shader)==D3D_OK);
    if (!shader) return;
    CHECK(device->SetPixelShader(shader)==D3D_OK);
    PixelPipelineDescription pipeline={};
    const char *names[]={"river water ps.1.1","classic river water ps.1.1",
        "trapezoid water ps.1.1","classic trapezoid water ps.1.1"};
    const SdlProgram *programs[4]={};
    const unsigned built=cache.Programs_Built();
    for (unsigned pass=0;pass<32;++pass) for (unsigned style=0;style<4;++style) {
        PosixDevice_Name_Shader(shader,names[style]);
        IDirect3DPixelShader9 *bound=NULL;
        CHECK(device->GetPixelShader(&bound)==D3D_OK && bound==shader);
        const auto engine=EngineShader_From_File(WaterShaderNameProbe::Engine_Name_Of(bound).c_str());
        if (bound) bound->Release();
        const SdlProgram &program=cache.Engine_Pixel_Program(engine,pipeline);
        CHECK(program.Shader!=NULL);
        if (!pass) programs[style]=&program;
        CHECK(&program==programs[style]);
#if defined(__linux__)
        if (style&1) CHECK(program.SamplerSlots==6 && program.UniformBuffers==1);
        else CHECK(program.SamplerSlots>6);
#else
        CHECK(program.SamplerSlots==6 && program.UniformBuffers==1);
#endif
    }
    CHECK(cache.Programs_Built()==built+4);
    CHECK(programs[0]->Shader!=programs[1]->Shader && programs[2]->Shader!=programs[3]->Shader);
    device->SetPixelShader(NULL);shader->Release();
    printf("sdl_program_cache_selfcheck: 128 water style changes on the same bound shader, four distinct cached GPU programs, no recompiles on return\n");
}

int main()
{
	check_slot_lines();
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_program_cache_selfcheck: SKIP - SDL_Init: %s\n", SDL_GetError());
		return 77;
	}
	std::string error;
	SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, 16, 16, error);
	if (frame == NULL) {
		printf("sdl_program_cache_selfcheck: SKIP - no GPU device here: %s\n", error.c_str());
		return 77;
	}
	IDirect3D9 *d3d = NULL;
	PosixDevice9 *device = make_device(d3d);
	CHECK(device != NULL);
	if (device == NULL) return 1;
	device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	// A texture at stage 0: with none bound, a stage whose COLORARG1 is the texture ends the cascade and
	// every operation below would resolve to the one diffuse program.
	IDirect3DTexture9 *texture = NULL;
	CHECK(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL) == D3D_OK);
	device->SetTexture(0, texture);

	SdlProgramCache *cache_owner = new SdlProgramCache(frame->Device());
	SdlProgramCache &cache = *cache_owner;
	CombinerDescription combiner;
	VertexPipelineDescription vertex;

	device->Build_Combiner_Description(combiner);
	CHECK(device->Build_Vertex_Description(vertex));
	const SdlProgram &pixel = cache.Pixel_Program(combiner);
	const SdlProgram &vertex_program = cache.Vertex_Program(vertex);
	CHECK(pixel.Shader != NULL && vertex_program.Shader != NULL);
	CHECK(pixel.SamplerSlots >= 1 && pixel.UniformBuffers == 1 && vertex_program.UniformBuffers == 1);
	CHECK(pixel.SlotTexture.size() == pixel.SamplerSlots && pixel.SlotTexture[0] == 0 && pixel.SlotSampler[0] == 0);
	const unsigned int built = cache.Programs_Built();
	CHECK(built == 2);

	// A hit: the same shader, nothing built.
	const SdlProgram &again = cache.Pixel_Program(combiner);
	CHECK(again.Shader == pixel.Shader && cache.Programs_Built() == built && again.Uses == 2);

	// Every colour operation the generator implements, stage 0 over the texture and the diffuse colour.
	static const RenderUInt32 OPERATIONS[] = {
		D3DTOP_SELECTARG1, D3DTOP_SELECTARG2, D3DTOP_MODULATE, D3DTOP_MODULATE2X, D3DTOP_MODULATE4X,
		D3DTOP_ADD, D3DTOP_ADDSIGNED, D3DTOP_ADDSIGNED2X, D3DTOP_SUBTRACT, D3DTOP_ADDSMOOTH,
		D3DTOP_BLENDDIFFUSEALPHA, D3DTOP_BLENDTEXTUREALPHA, D3DTOP_BLENDFACTORALPHA, D3DTOP_BLENDCURRENTALPHA,
		D3DTOP_BLENDTEXTUREALPHAPM, D3DTOP_MODULATEALPHA_ADDCOLOR, D3DTOP_MODULATECOLOR_ADDALPHA,
		D3DTOP_MODULATEINVALPHA_ADDCOLOR, D3DTOP_MODULATEINVCOLOR_ADDALPHA, D3DTOP_DOTPRODUCT3,
		D3DTOP_MULTIPLYADD, D3DTOP_LERP };
	const unsigned int count = sizeof(OPERATIONS) / sizeof(OPERATIONS[0]);
	unsigned int compiled = 0;
	for (unsigned int i = 0; i < count; ++i) {
		device->SetTextureStageState(0, D3DTSS_COLOROP, OPERATIONS[i]);
		device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		device->SetTextureStageState(0, D3DTSS_COLORARG0, D3DTA_TFACTOR);
		device->Build_Combiner_Description(combiner);
		const SdlProgram &program = cache.Pixel_Program(combiner);
		if (program.Shader != NULL) {
			++compiled;
		}
		else {
			printf("  operation %u refused: %s\n", (unsigned)OPERATIONS[i], program.Refusal.c_str());
		}
	}
	CHECK(compiled == count);
	CHECK(cache.Programs_Built() >= built + count - 1);	// one program per operation (MODULATE was built above)

	// The three light types.
	D3DLIGHT9 light;
	memset(&light, 0, sizeof(light));
	static const D3DLIGHTTYPE TYPES[] = { D3DLIGHT_DIRECTIONAL, D3DLIGHT_POINT, D3DLIGHT_SPOT };
	for (int i = 0; i < 3; ++i) {
		light.Type = TYPES[i];
		device->SetLight(0, &light);
		device->LightEnable(0, 1);
		CHECK(device->Build_Vertex_Description(vertex));
		const SdlProgram &program = cache.Vertex_Program(vertex);
		CHECK(program.Shader != NULL);
	}

	// A refusal, kept: asked twice, tried once.
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
	device->Build_Combiner_Description(combiner);
	const unsigned int refused_before = cache.Programs_Refused();
	const SdlProgram &refused = cache.Pixel_Program(combiner);
	CHECK(refused.Shader == NULL && !refused.Refusal.empty());
	const SdlProgram &refused_again = cache.Pixel_Program(combiner);
	CHECK(refused_again.Shader == NULL && cache.Programs_Refused() == refused_before + 1);

	check_live_water_style_programs(device,cache);
    const auto savedWater=WaterPerformanceSettings::Applied();
    const SdlProgram *tiers[12]={};unsigned tier=0;PixelPipelineDescription waterPipeline={};
    for(unsigned resolution:{128u,256u}) for(unsigned refraction=0;refraction<3;++refraction) for(bool reflections:{false,true}) {
        auto &settings=WaterPerformanceSettings::Applied();settings.activeFftResolution=resolution;
        settings.refraction=WaterPerformanceSettings::Refraction(refraction);settings.sceneReflections=reflections;
        tiers[tier]=&cache.Engine_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,waterPipeline);
        CHECK(tiers[tier]->Shader!=NULL);
        for(unsigned prior=0;prior<tier;++prior) CHECK(tiers[prior]!=tiers[tier]);
        CHECK(&cache.Engine_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,waterPipeline)==tiers[tier]);++tier;
    }
    const auto builtTiers=cache.Programs_Built();tier=0;
    for(unsigned resolution:{128u,256u}) for(unsigned refraction=0;refraction<3;++refraction) for(bool reflections:{false,true}) {
        auto &settings=WaterPerformanceSettings::Applied();settings.activeFftResolution=resolution;
        settings.refraction=WaterPerformanceSettings::Refraction(refraction);settings.sceneReflections=reflections;
        CHECK(&cache.Engine_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,waterPipeline)==tiers[tier++]);
    }
    CHECK(cache.Programs_Built()==builtTiers);WaterPerformanceSettings::Applied()=savedWater;
    printf("sdl_program_cache_selfcheck: all 12 water quality variants cached and reused\n");


	printf("sdl_program_cache_selfcheck: %s, %u programs built in %.0f ms, %u refused\n",
		SDL_GetGPUDeviceDriver(frame->Device()), cache.Programs_Built(), cache.Milliseconds_Compiling(),
		cache.Programs_Refused());
	device->SetTexture(0, NULL);
	texture->Release();
	device->Release();
	d3d->Release();
	delete cache_owner;		// its shaders go before the GPU device that made them
	delete frame;
	SDL_Quit();
	if (failures != 0) {
		printf("sdl_program_cache_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	return 0;
}
