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

// A3a's frame (PosixDevice/Render/SdlGpuFrame), on this machine's GPU, offscreen, read back:
//   - a colour clear lands as D3D9's ARGB in the back buffer's B8G8R8A8 bytes, every pixel;
//   - a depth-only clear leaves the colour as it was (the colour is loaded, not cleared);
//   - the gamma pass maps each channel through its own ramp, on a picture where every pixel differs, so
//     a flipped, shifted or channel-swapped pass cannot match; the identity ramp's blit gives the picture
//     back unchanged;
//   - an armed control: the gamma output compared against the wrong ramp must not match.
//
// WHAT THIS DOES NOT PROVE:
//   - Presenting to a window: the swap chain needs a display, and ctest has none.  The window's path is
//     the same pass into the swap chain's texture instead of this test's.
//   - Any GPU but this one.  Exits 77, and ctest reports it skipped, where SDL has no GPU device.

#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlConstants.h"
#include "SdlResourceMirror.h"
#include "PosixResources9.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3target.h"
#include "../Libraries/Source/WWVegas/WW3D2/engineshader.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterHullVisual.h"
#include <limits>
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterFoamShading.h"
#include "Platform/WaterFoamEnvironment.h"
#include "Platform/WaterShoreFoam.h"
#include "Platform/WaterShoreOverlay.h"
#include "../Libraries/Source/WWVegas/WW3D2/ffshader.h"
#include "Platform/WaterBowRefinement.h"
#include <complex>
#include <sstream>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <cstring>
#include <math.h>
#include <string.h>
#include <stddef.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

// Bind the production fragment layout even when a normal-only fixture has no mesh hulls.
static void push_empty_visual_water(SDL_GPUCommandBuffer *commands,const SdlWaterPixelConstants &water)
{
    SdlWaterVisualPixelConstants common={};common.Water=water;
    SDL_PushGPUFragmentUniformData(commands,0,&common,sizeof(common));
    const float packets[WaterHullVisualLayout::PacketRegisters][4]={};
    for(unsigned block=0;block<WaterHullVisualLayout::BlockCount;++block)
        SDL_PushGPUFragmentUniformData(commands,block+1,packets[WaterHullVisualLayout::BlockOffset(block)],
            WaterHullVisualLayout::BlockRegisters(block)*16);
}

// Observe the production normal without a runtime shader variant or environment switch.
static bool observe_water_normal(std::string &source)
{
    const std::string output="    return current;\n}";
    const size_t at=source.rfind(output);
    CHECK(at!=std::string::npos);
    if(at==std::string::npos)return false;
    source.insert(at,"    current.rgb = normal*0.5+0.5;\n");
    return true;
}

// Compile the exact production refraction helpers with fixture inputs. Unrelated mesh-hull
// buffers occupy b1..b3 in the complete material shader; these helpers need only its b0.
static bool water_refraction_fixture_source(const std::string &production,std::string &source)
{
    const size_t input=production.find("struct Input");
    const size_t terrain=production.find("float terrainHeight("),terrainEnd=production.find("float terrainSun(",terrain);
    const size_t first=production.find("float2 pixelOffset("),last=production.find("WaterRefractionSample waterRefractedScene(",first);
    const size_t opening=production.find('{',last);
    CHECK(input!=std::string::npos && terrain<terrainEnd && terrainEnd!=std::string::npos
        && first<last && last<opening && opening!=std::string::npos);
    if(input==std::string::npos || terrain>=terrainEnd || terrainEnd==std::string::npos
        || first>=last || last>=opening || opening==std::string::npos)return false;
    size_t end=opening+1;int depth=1;
    for(;end<production.size() && depth;++end) {
        if(production[end]=='{')++depth;
        else if(production[end]=='}')--depth;
    }
    CHECK(depth==0);if(depth)return false;
    std::istringstream prefix(production.substr(0,input));std::string line;
    source.clear();
    while(std::getline(prefix,line)) {
        if(line.find("cbuffer WaterVisualBlock")!=std::string::npos
            || line.find("float4 VisualHullMeta[")!=std::string::npos)continue;
        source+=line+'\n';
    }
    source+=production.substr(terrain,terrainEnd-terrain);
    source+=production.substr(first,end-first);
    source+=WaterOpticalModel::GetHlslSource();
    return true;
}

static const unsigned int WIDTH = 64;
static const unsigned int HEIGHT = 48;

// Cloud caches and their half-resolution ray pass have colour attachments only.
// Exercise their recorded clear path and switching back to the scene attachment:
// passing a depth descriptor whose texture is null crashes SDL's validation path.
static void check_colour_only_target(SdlGpuFrame *frame)
{
	SDL_GPUTextureCreateInfo info = {};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = WIDTH;
	info.height = HEIGHT;
	info.layer_count_or_depth = info.num_levels = 1;
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(texture != NULL);
	if (!texture) return;

	const SdlTarget colour_only = { texture, NULL, WIDTH, HEIGHT };
	frame->Set_Target(colour_only);
	frame->Clear_Back_Buffer(true, false, false, 0xFF123456u, 1.0f, 0);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(texture, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4);
	for (size_t i = 0; i + 3 < pixels.size(); i += 4)
		CHECK(pixels[i] == 0x56 && pixels[i + 1] == 0x34
			&& pixels[i + 2] == 0x12 && pixels[i + 3] == 0xFF);

	// Both clears are pending in one batch: changing targets must close the
	// colour-only pass and retain the scene's normal depth/stencil attachment.
	frame->Clear_Back_Buffer(true, false, false, 0xFFABCDEFu, 1.0f, 0);
	frame->Set_Target(frame->Back_Buffer_Target());
	frame->Clear_Back_Buffer(true, true, true, 0xFF102030u, 0.5f, 7);
	CHECK(frame->Read_Back(texture, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0xEF
		&& pixels[1] == 0xCD && pixels[2] == 0xAB && pixels[3] == 0xFF);
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x30
		&& pixels[1] == 0x20 && pixels[2] == 0x10 && pixels[3] == 0xFF);
	SDL_ReleaseGPUTexture(frame->Device(), texture);
	printf("sdl_gpu_frame_selfcheck: colour-only attachment clear/readback and scene target restoration checked\n");
}

// Rasterize a submerged object independently of the water capture: a triangle writes its own
// depth, so a terrain-only estimate or a copy of the depth clear cannot satisfy the assertions.
// With sun, a depth-only caster through the production pipeline cache. The optional vertex
// texture path also exercises the height sampling used by cloud embers.
class DepthObject
{
public:
	SDL_GPUShader *Vertex = NULL, *Pixel = NULL;
	SDL_GPUGraphicsPipeline *Pipeline = NULL;
	SdlPipelineCache *Cache = NULL;
	SDL_GPUSampler *Sampler = NULL;
	SdlGpuFrame *Frame;
	explicit DepthObject(SdlGpuFrame *frame, bool sun = false, bool vertexTexture = false) : Frame(frame)
	{
		const char *vertex =
			"cbuffer Object : register(b0, space1) { float4 Depth; };\n"
			"float4 main(uint id : SV_VertexID) : SV_Position {\n"
			"float2 p = id == 0 ? float2(-0.75,-0.75) : (id == 1 ? float2(0.75,-0.75) : float2(0,0.75));\n"
			"return float4(p,Depth.x,1); }\n";
		if (vertexTexture) {
			vertex =
				"Texture2D<float> Height : register(t0, space0);\n"
				"SamplerState HeightSampler : register(s0, space0);\n"
				"float4 main(uint id : SV_VertexID) : SV_Position {\n"
				"float2 p = id == 0 ? float2(-0.75,-0.75) : (id == 1 ? float2(0.75,-0.75) : float2(0,0.75));\n"
				"return float4(p, Height.SampleLevel(HeightSampler,float2(0.5,0.5),0),1); }\n";
		}
		const char *pixel = sun ? "void main() {}\n" : "float4 main() : SV_Target0 { return float4(0.8,0.2,0.1,1); }\n";
		std::vector<unsigned char> spirv;
		std::string error;
		CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error));
		Vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
		CHECK(Vertex != NULL);
		CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error));
		Pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
		CHECK(Pixel != NULL);
		SDL_GPUColorTargetDescription colour = {};
		colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = Vertex;
		info.fragment_shader = Pixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.depth_stencil_state.enable_depth_test = true;
		info.depth_stencil_state.enable_depth_write = true;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
		info.target_info.color_target_descriptions = &colour;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
		if (sun) {
			// Exercise the production cache with no colour attachment and a distinct
			// depth format, not just a hand-built SDL pipeline.
			RenderUInt32 states[256] = {};
			states[D3DRS_FILLMODE] = D3DFILL_SOLID;
			states[D3DRS_CULLMODE] = D3DCULL_NONE;
			states[D3DRS_ZENABLE] = states[D3DRS_ZWRITEENABLE] = 1;
			states[D3DRS_ZFUNC] = D3DCMP_LESS;
			SdlPipelineKey key;
			CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, Vertex, Pixel, D3DFVF_XYZ,
				0, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, key, error));
			Cache = new SdlPipelineCache(frame->Device());
			Pipeline = Cache->Pipeline(key);
		} else Pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
		CHECK(Pipeline != NULL);
		if (vertexTexture) {
			SDL_GPUSamplerCreateInfo sampler = {};
			sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
			sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
			sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w
				= SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
			Sampler = SDL_CreateGPUSampler(frame->Device(), &sampler);
			CHECK(Sampler != NULL);
		}
	}
	~DepthObject()
	{
		Frame->Flush();
		if (Cache) delete Cache;
		else if (Pipeline) SDL_ReleaseGPUGraphicsPipeline(Frame->Device(), Pipeline);
		if (Sampler) SDL_ReleaseGPUSampler(Frame->Device(), Sampler);
		if (Vertex) SDL_ReleaseGPUShader(Frame->Device(), Vertex);
		if (Pixel) SDL_ReleaseGPUShader(Frame->Device(), Pixel);
	}
	void Draw(const SdlTarget &target, float depth, SDL_GPUTexture *height = NULL)
	{
		if (!Pipeline || (height && !Sampler)) return;
		Frame->Set_Target(target);
		SdlRecordedDraw draw = {};
		draw.Pipeline = Pipeline;
		draw.Count = 3;
		memset(Frame->Stage(36, draw.VertexOffset), 0, 36);
		const float values[4] = { depth, 0, 0, 0 };
		draw.VertexConstants = Frame->Constants(0, values, sizeof(values));
		if (height) {
			draw.VertexSamplerCount = 1;
			draw.VertexTextures[0] = height;
			draw.VertexSamplers[0] = Sampler;
		} else {
			draw.VertexConstantsSize = sizeof(values);
		}
		draw.Viewport[2] = (float)target.Width;
		draw.Viewport[3] = (float)target.Height;
		draw.Viewport[5] = 1;
		draw.Scissor[2] = target.Width;
		draw.Scissor[3] = target.Height;
		Frame->Record_Draw(draw);
	}
};

static float depth_at(const std::vector<uint8_t> &bytes, unsigned int width, unsigned int x, unsigned int y)
{
	float value = -1;
	const size_t offset = ((size_t)y * width + x) * sizeof(float);
	if (offset + sizeof(float) <= bytes.size()) memcpy(&value, &bytes[offset], sizeof(value));
	return value;
}

// The hazard clouds' view of the shared scene snapshot: object depth, sampling it in a vertex
// stage, and invalidation by target, frame, clear and resize.
static void check_scene_depth_snapshot(SdlGpuFrame *frame)
{
	const SdlTarget scene = frame->Back_Buffer_Target();
	CHECK(frame->Supports_Scene_Depth() == bool(frame->Depth_Usage() & SDL_GPU_TEXTUREUSAGE_SAMPLER));
	if (!frame->Supports_Scene_Depth()) {
		CHECK(frame->Scene_Depth(scene) == NULL);
		printf("sdl_gpu_frame_selfcheck: sampled depth unavailable; fallback checked\n");
		return;
	}
	frame->Set_Target(scene);
	frame->Clear_Back_Buffer(true, true, true, 0xFF000000u, 0.8f, 0);
	DepthObject object(frame);
	object.Draw(scene, 0.25f);
	SDL_GPUTexture *snapshot = frame->Scene_Depth(scene);
	CHECK(snapshot != NULL);
	if (!snapshot) return;
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(snapshot, WIDTH, HEIGHT, bytes));
	CHECK(std::fabs(depth_at(bytes, WIDTH, WIDTH / 2, HEIGHT / 2) - 0.25f) < 0.00001f);
	CHECK(std::fabs(depth_at(bytes, WIDTH, 0, 0) - 0.8f) < 0.00001f);
	CHECK(frame->Scene_Depth(scene) == snapshot);

	SDL_GPUTextureCreateInfo info = {};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = WIDTH; info.height = HEIGHT;
	info.layer_count_or_depth = info.num_levels = 1;
	SDL_GPUTexture *colour = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(colour != NULL);
	if (colour) {
		const SdlTarget intermediate = { colour, frame->Depth_For(WIDTH, HEIGHT), WIDTH, HEIGHT };
		frame->Set_Target(intermediate);
		frame->Clear_Back_Buffer(true, true, true, 0xFF000000u, 1.0f, 0);
		DepthObject sampled(frame, false, true);
		sampled.Draw(intermediate, 0, snapshot);
		SDL_GPUTexture *sampled_depth = frame->Scene_Depth(intermediate);
		CHECK(sampled_depth != NULL);
		if (sampled_depth) CHECK(frame->Read_Back(sampled_depth, WIDTH, HEIGHT, bytes));
		CHECK(std::fabs(depth_at(bytes, WIDTH, WIDTH / 2, HEIGHT / 2) - 0.25f) < 0.00001f);
		CHECK(std::fabs(depth_at(bytes, WIDTH, 0, 0) - 1.0f) < 0.00001f);
		frame->Set_Target(scene);
		frame->Release_After_Batch(colour, NULL);
	}
	frame->Clear_Back_Buffer(false, true, true, 0, 0.6f, 0);
	CHECK(frame->Read_Back(frame->Scene_Depth(scene), WIDTH, HEIGHT, bytes));
	CHECK(std::fabs(depth_at(bytes, WIDTH, WIDTH / 2, HEIGHT / 2) - 0.6f) < 0.00001f);
	CHECK(frame->Present(NULL));
	object.Draw(scene, 0.1f);
	CHECK(frame->Read_Back(frame->Scene_Depth(scene), WIDTH, HEIGHT, bytes));
	CHECK(std::fabs(depth_at(bytes, WIDTH, WIDTH / 2, HEIGHT / 2) - 0.1f) < 0.00001f);
	CHECK(frame->Resize(WIDTH, HEIGHT));
	frame->Clear_Back_Buffer(true, true, true, 0, 0.4f, 0);
	CHECK(frame->Read_Back(frame->Scene_Depth(frame->Back_Buffer_Target()), WIDTH, HEIGHT, bytes));
	CHECK(std::fabs(depth_at(bytes, WIDTH, WIDTH / 2, HEIGHT / 2) - 0.4f) < 0.00001f);
	printf("sdl_gpu_frame_selfcheck: scene/object depth, vertex texture, target/frame/clear invalidation and resize checked\n");
}

static void check_scene_depth(SdlGpuFrame *frame)
{
	if (!(frame->Depth_Usage() & SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
		printf("sdl_gpu_frame_selfcheck: scene depth unavailable on this device (fallback colour still tested)\n");
		CHECK(frame->Scene_Depth(frame->Back_Buffer_Target()) == NULL);
		return;
	}
	DepthObject object(frame);
	const SdlTarget scene = frame->Back_Buffer_Target();
	frame->Set_Target(scene);
	frame->Clear_Back_Buffer(true, true, true, 0xFF183048, 0.9f, 0);
	frame->Clear_Rect(0, 0, 16, 12, false, true, false, 0, 0.65f, 0);
	uint64_t readySerial=123;
	CHECK(frame->Ready_Scene_Depth(scene,readySerial)==NULL && readySerial==0);
	object.Draw(scene, 0.3f);
	SDL_GPUTexture *depth = frame->Scene_Depth(scene);
	CHECK(depth != NULL && depth != scene.Depth && depth != scene.Colour);
	CHECK(frame->Ready_Scene_Depth(scene,readySerial)==depth && readySerial!=0);
	const uint64_t originalSerial=readySerial;
	std::vector<uint8_t> sampled;
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, sampled));
	CHECK(fabs(depth_at(sampled, scene.Width, 32, 24) - 0.3f) < 0.00001f);
	CHECK(fabs(depth_at(sampled, scene.Width, 2, 2) - 0.65f) < 0.00001f);
	CHECK(fabs(depth_at(sampled, scene.Width, 62, 46) - 0.9f) < 0.00001f);
	std::vector<uint8_t> colour;
	CHECK(frame->Read_Back(frame->Water_Background(scene), scene.Width, scene.Height, colour));
	CHECK(colour[(24 * scene.Width + 32) * 4 + 2] == 204); // the same object's colour

	// A later draw must not contaminate the pre-water depth, even over multiple flushes.
	object.Draw(scene, 0.1f);
	CHECK(frame->Ready_Scene_Depth(scene,readySerial)==depth && readySerial==originalSerial);
	CHECK(frame->Scene_Depth(scene) == depth);
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, colour));
	CHECK(colour == sampled);
	// No clear is needed to refresh the next frame's depth.
	CHECK(frame->Present(NULL));
	CHECK(frame->Ready_Scene_Depth(scene,readySerial)==NULL && readySerial==0);
	CHECK(frame->Scene_Depth(scene) == depth);
	CHECK(frame->Ready_Scene_Depth(scene,readySerial)==depth && readySerial!=originalSerial);
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, sampled));
	CHECK(fabs(depth_at(sampled, scene.Width, 32, 24) - 0.1f) < 0.00001f);

	SDL_GPUTextureCreateInfo info = {};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)frame->Depth_Format();
	info.usage = frame->Depth_Usage();
	info.width = 23; info.height = 17; info.layer_count_or_depth = 1; info.num_levels = 1;
	SDL_GPUTexture *other_depth = SDL_CreateGPUTexture(frame->Device(), &info);
	info.format = (SDL_GPUTextureFormat)frame->Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	SDL_GPUTexture *other_colour = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(other_depth && other_colour);
	const SdlTarget intermediate = { other_colour, other_depth, 23, 17 };
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(true, true, true, 0xFFABCDEF, 0.8f, 0);
	CHECK(frame->Ready_Scene_Depth(intermediate,readySerial)==NULL && readySerial==0);
	object.Draw(intermediate, 0.45f);
	SDL_GPUTexture *intermediate_depth = frame->Scene_Depth(intermediate);
	CHECK(intermediate_depth && intermediate_depth != depth);
	CHECK(frame->Read_Back(intermediate_depth, 23, 17, colour));
	CHECK(fabs(depth_at(colour, 23, 11, 8) - 0.45f) < 0.00001f);
	CHECK(fabs(depth_at(colour, 23, 0, 0) - 0.8f) < 0.00001f);
	// Returning to a source preserves its own snapshot instead of copying previously rendered water.
	object.Draw(scene, 0.05f);
	CHECK(frame->Read_Back(frame->Scene_Depth(scene), scene.Width, scene.Height, colour));
	CHECK(colour == sampled);
    // A declared depth-only water boundary must skip colour allocation/copy, retain
    // the frozen original depth after later draws, and reject a late colour request.
    frame->Set_Target(intermediate);frame->Clear_Back_Buffer(true,true,true,0xFF204060,.75f,0);
    const auto coloursBefore=frame->Water_Colour_Captures(),depthsBefore=frame->Water_Depth_Captures();
    const auto savedSettings=WaterPerformanceSettings::Applied();
    WaterPerformanceSettings::Applied().sceneColourRequired=false;
    CHECK(frame->Scene_Depth(intermediate)!=NULL);
    WaterPerformanceSettings::Applied()=savedSettings;
    CHECK(frame->Water_Colour_Captures()==coloursBefore && frame->Water_Depth_Captures()==depthsBefore+1);
    CHECK(frame->Water_Background(intermediate)==NULL);
    object.Draw(intermediate,.12f);CHECK(frame->Flush());
    CHECK(frame->Water_Depth(intermediate)==intermediate_depth);
    CHECK(frame->Read_Back(intermediate_depth,23,17,colour));
    CHECK(fabs(depth_at(colour,23,11,8)-.75f)<.00001f);
    CHECK(frame->Water_Colour_Captures()==coloursBefore && frame->Water_Background(intermediate)==NULL);
    frame->Clear_Back_Buffer(true,true,true,0xFF102030,.6f,0);
    frame->Prepare_Water_Snapshot(intermediate,true,false);
    CHECK(frame->Water_Colour_Captures()==coloursBefore+1 && frame->Water_Depth_Captures()==depthsBefore+1);
    CHECK(frame->Water_Depth(intermediate)==NULL && frame->Ready_Scene_Depth(intermediate,readySerial)==NULL && readySerial==0);
    CHECK(frame->Water_Background(intermediate)!=NULL);
	// Depth-only clear invalidates the pair, then release source handles with a capture still queued.
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(false, true, false, 0, 0.7f, 0);
	CHECK(frame->Ready_Scene_Depth(intermediate,readySerial)==NULL && readySerial==0);
	CHECK(frame->Read_Back(frame->Scene_Depth(intermediate), 23, 17, colour));
	CHECK(fabs(depth_at(colour, 23, 11, 8) - 0.7f) < 0.00001f);
	frame->Clear_Back_Buffer(true, true, true, 0xFF102030, 0.6f, 0);
	CHECK(frame->Scene_Depth(intermediate) != NULL);
	frame->Release_After_Batch(other_colour, NULL);
	frame->Release_After_Batch(other_depth, NULL);
	CHECK(frame->Flush());
	frame->Set_Target(scene);
	CHECK(frame->Present(NULL));
}

static void check_gpu_wave_model(SdlGpuFrame *frame)
{
	// Execute the exact shared HLSL model on the GPU. Columns are height, dH/dx and dH/dy; rows use
	// different positions/times, including the free-camera view and large/negative map coordinates.
	const float fixtures[][4] = {
		{0, 0, 0, 0}, {0, 0, 1.7f, 0}, {12, -21, 3.2f, 0}, {-80, 43, 19.4f, 0},
		{1938, 736, 0, 0}, {1938, 736, 4.2f, 0}, {1938, 736, 67.1f, 0},
		{4100, 3700, 13.8f, 0}, {-1900, 720, 3.6f, 0}, {736, 1938, 431.2f, 0},
		{17, 89, 864.3f, 0}, {4096, 4096, 3600, 0} };
	const unsigned int count = sizeof(fixtures) / sizeof(fixtures[0]);
	const char *vertex_source =
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source = "cbuffer Samples : register(b0, space3) { float4 Fixtures[12]; };\n";
	pixel_source += WaterSurfaceModel::ShaderSource();
	pixel_source += "float main(float4 position : SV_Position) : SV_Target0 {\n"
		"float4 f=Fixtures[(uint)position.y]; return waterWave(f.xy,f.z)[(uint)position.x]; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description = {};
	description.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.vertex_shader = vertex;
	pipeline_info.fragment_shader = pixel;
	pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions = &description;
	pipeline_info.target_info.num_color_targets = 1;
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &pipeline_info);
	SDL_GPUTextureCreateInfo texture_info = {};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width = 3; texture_info.height = count;
	texture_info.layer_count_or_depth = 1; texture_info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment = {};
	attachment.texture = target;
	attachment.load_op = SDL_GPU_LOADOP_DONT_CARE;
	attachment.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &attachment, 1, NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_PushGPUFragmentUniformData(commands, 0, fixtures, sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target, 3, count, bytes));
	float max_model_error = 0, max_difference_error = 0;
	for (unsigned int i = 0; i < count; ++i) {
		const float x = fixtures[i][0], y = fixtures[i][1], seconds = fixtures[i][2];
		const WaterSurfaceModel::Sample cpu = WaterSurfaceModel::Evaluate(x, y, seconds);
		const float values[3] = { cpu.height, cpu.dx, cpu.dy };
		for (unsigned int c = 0; c < 3; ++c) {
			const float difference = fabsf(depth_at(bytes, 3, c, i) - values[c]);
			if (difference > max_model_error) max_model_error = difference;
			CHECK(difference < 0.0025f); // float phase precision at large map coordinates/times
		}
		const float epsilon = 0.03125f;
		const float dx = (WaterSurfaceModel::Evaluate(x + epsilon, y, seconds).height
			- WaterSurfaceModel::Evaluate(x - epsilon, y, seconds).height) / (2 * epsilon);
		const float dy = (WaterSurfaceModel::Evaluate(x, y + epsilon, seconds).height
			- WaterSurfaceModel::Evaluate(x, y - epsilon, seconds).height) / (2 * epsilon);
		const float error_x = fabsf(depth_at(bytes, 3, 1, i) - dx), error_y = fabsf(depth_at(bytes, 3, 2, i) - dy);
		if (error_x > max_difference_error) max_difference_error = error_x;
		if (error_y > max_difference_error) max_difference_error = error_y;
		CHECK(error_x < 0.01f && error_y < 0.01f);
	}
	printf("sdl_gpu_frame_selfcheck: GPU wave height/normal consistency: max CPU model error %.7f, finite-difference slope error %.7f\n",
		max_model_error, max_difference_error);
	SDL_ReleaseGPUTexture(frame->Device(), target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(), pipeline);
	SDL_ReleaseGPUShader(frame->Device(), vertex);
	SDL_ReleaseGPUShader(frame->Device(), pixel);
}

static void check_gpu_ocean_wave_model(SdlGpuFrame *frame)
{
	// A spatially varying offshore weight exercises the derivative of the blend, not just the
	// interpolation of two normals. Every fixture is {x,y,time,weight, dWeight/dx,dWeight/dy,0,0}.
	const float fixtures[][8] = {
		{0,0,0,0, 0,0,0,0}, {12,-21,3.2f,0, 0,0,0,0}, {1938,736,67.1f,0, 0,0,0,0},
		{0,0,0,1, 0,0,0,0}, {0,0,1.7f,1, 0,0,0,0}, {3028,588,12,1, 0,0,0,0},
		{4096,4096,3600,1, 0,0,0,0}, {-1900,720,3.6f,1, 0,0,0,0}, {17,89,864.3f,1, 0,0,0,0},
		{0,0,0,.2f, .014f,-.011f,0,0}, {12,-21,3.2f,.5f, -.006f,.018f,0,0},
		{-80,43,19.4f,.8f, .011f,.007f,0,0}, {1938,736,4.2f,.5f, .008f,-.012f,0,0},
		{4100,3700,13.8f,.5f, -.009f,.004f,0,0}, {736,1938,431.2f,.9f, .002f,-.003f,0,0}
	};
	const unsigned int count=sizeof(fixtures)/sizeof(fixtures[0]), columns=30;
	const float epsilon=.03125f;
	const char *vertex_source=
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source="cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source+=std::to_string(count*2)+"]; };\n";
	WaterSurfaceModel::AppendHLSL(pixel_source);
	pixel_source+=
		"float3 blendAt(float2 offset,float4 f,float2 gradient) {\n"
		"float3 legacy=waterWave(f.xy+offset,f.z), ocean=waterOceanWave(f.xy+offset,f.z);\n"
		"float weight=saturate(f.w+dot(offset,gradient));\n"
		"float3 result=legacy+(ocean-legacy)*weight;\n"
		"result.yz+=(ocean.x-legacy.x)*gradient; return result; }\n"
		"float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y,column=(uint)position.x;\n"
		"float4 f=Fixtures[row*2]; float2 gradient=Fixtures[row*2+1].xy;\n"
		"if (column<3) return blendAt(float2(0,0),f,gradient)[column];\n"
		"if (column<7) { float2 offset=column==3?float2(0.03125,0):\n"
		"column==4?float2(-0.03125,0):column==5?float2(0,0.03125):float2(0,-0.03125);\n"
		"return blendAt(offset,f,gradient).x; }\n"
		"if (column<10) return waterOceanWave(f.xy,f.z)[column-7];\n"
		"if (column<13) return waterWave(f.xy,f.z)[column-10];\n"
		"if (column==13) return waterOceanSurface(f.xy,f.z).w;\n"
		"if (column>=26) { float2 offset=column==26?float2(0.25,0):column==27?float2(-0.25,0):\n"
		"column==28?float2(0,0.25):float2(0,-0.25); return waterOceanWave(f.xy+offset,f.z).x; }\n"
		"if (column>=18) { float4 swell; waterOceanSurface(f.xy,f.z,swell);\n"
		"if (column<22) return swell[column-18];\n"
		"float2 step=column==22?float2(0.125,0):column==23?float2(-0.125,0):column==24?float2(0,0.125):float2(0,-0.125);\n"
		"waterOceanSurface(f.xy+step,f.z,swell); return swell.x; }\n"
		"float2 offset=column==14?float2(0.125,0):column==15?float2(-0.125,0):\n"
		"column==16?float2(0,0.125):float2(0,-0.125);\n"
		"return waterOceanWave(f.xy+offset,f.z).x; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};
	description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info={};
	pipeline_info.vertex_shader=vertex; pipeline_info.fragment_shader=pixel;
	pipeline_info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions=&description;
	pipeline_info.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipeline_info);
	SDL_GPUTextureCreateInfo texture_info={};
	texture_info.type=SDL_GPU_TEXTURETYPE_2D; texture_info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width=columns; texture_info.height=count;
	texture_info.layer_count_or_depth=1; texture_info.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment={};
	attachment.texture=target; attachment.load_op=SDL_GPU_LOADOP_DONT_CARE; attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);
	SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target,columns,count,bytes));
	float max_model_error=0,max_gpu_derivative_error=0,max_cpu_derivative_error=0;
	float max_curvature_model_error=0,max_cpu_curvature_error=0,max_gpu_curvature_error=0;
	float max_cpu_coarse_curvature_error=0,max_gpu_coarse_curvature_error=0;
	float max_swell_model_error=0,max_swell_gradient_error=0,max_swell_laplacian_error=0;
	unsigned int gradient_controls=0,curvature_checks=0,curvature_controls=0,curvature_refinements=0;
	for (unsigned int i=0;i<count;++i) {
		using namespace WaterSurfaceModel;
		const float *f=fixtures[i];
		const Sample legacy=Evaluate(f[0],f[1],f[2]),ocean=EvaluateOcean(f[0],f[1],f[2]);
		const Sample offshore={f[3],f[4],f[5]};
		const Sample blended=BlendSamples(legacy,ocean,offshore);
		const float expected[3]={blended.height,blended.dx,blended.dy};
		const float expected_ocean[3]={ocean.height,ocean.dx,ocean.dy};
		for (unsigned int c=0;c<3;++c) {
			const float a=fabsf(depth_at(bytes,columns,c,i)-expected[c]);
			const float b=fabsf(depth_at(bytes,columns,c+7,i)-expected_ocean[c]);
			max_model_error=fmaxf(max_model_error,fmaxf(a,b));
			CHECK(a<.0025f && b<.0025f); // float phase precision at long elapsed times/large map positions
			if (f[3]==0) CHECK(depth_at(bytes,columns,c,i)==depth_at(bytes,columns,c+10,i));
			if (f[3]==1) CHECK(fabsf(depth_at(bytes,columns,c,i)-depth_at(bytes,columns,c+7,i))<.000001f);
		}
		auto height_at=[&](float ox,float oy) {
			// Independent scalar interpolation deliberately does not use the analytic BlendSamples
			// derivatives. Its central difference must recover both domain warp and blend product terms.
			const float a=Evaluate(f[0]+ox,f[1]+oy,f[2]).height;
			const float b=EvaluateOcean(f[0]+ox,f[1]+oy,f[2]).height;
			const float weight=f[3]+ox*f[4]+oy*f[5];
			return a+(b-a)*weight;
		};
		const float numeric_cpu[2]={(height_at(epsilon,0)-height_at(-epsilon,0))/(2*epsilon),
			(height_at(0,epsilon)-height_at(0,-epsilon))/(2*epsilon)};
		const float numeric_gpu[2]={(depth_at(bytes,columns,3,i)-depth_at(bytes,columns,4,i))/(2*epsilon),
			(depth_at(bytes,columns,5,i)-depth_at(bytes,columns,6,i))/(2*epsilon)};
		for (unsigned int c=0;c<2;++c) {
			const float cpu_error=fabsf(numeric_cpu[c]-expected[c+1]);
			const float gpu_error=fabsf(numeric_gpu[c]-depth_at(bytes,columns,c+1,i));
			max_cpu_derivative_error=fmaxf(max_cpu_derivative_error,cpu_error);
			max_gpu_derivative_error=fmaxf(max_gpu_derivative_error,gpu_error);
			CHECK(cpu_error<.01f && gpu_error<.0125f);
			// Armed control: omitting dWeight/dx,y must measurably break several fixtures.
			const float wrong=(c==0?legacy.dx:legacy.dy)*(1-f[3])+(c==0?ocean.dx:ocean.dy)*f[3];
			if (fabsf(expected[c+1]-wrong)>.008f) ++gradient_controls;
		}
		if (f[3]==0) {
			const Sample endpoint=EvaluateBlended(f[0],f[1],f[2],offshore);
			CHECK(memcmp(&legacy,&endpoint,sizeof(Sample))==0);
		}
		const OceanSurface surface=EvaluateOceanSurface(f[0],f[1],f[2]);
		const float gpu_curvature=depth_at(bytes,columns,13,i);
		const float model_error=fabsf(gpu_curvature-surface.laplacian);
		max_curvature_model_error=fmaxf(max_curvature_model_error,model_error);
		CHECK(model_error<.0025f);
		const OceanSurface swell=EvaluateOceanSwell(f[0],f[1],f[2]);
		const float expected_swell[4]={swell.height,swell.dx,swell.dy,swell.laplacian};
		for(unsigned c=0;c<4;++c) {
			const float difference=fabsf(depth_at(bytes,columns,18+c,i)-expected_swell[c]);
			max_swell_model_error=fmaxf(max_swell_model_error,difference);CHECK(difference<.0025f);
		}
		// Height-only second differences independently exercise the warp Hessian and all packet
		// product terms. Near-origin fixtures avoid subtractive float cancellation from long phases;
		// large-coordinate/elapsed-time fixtures still check CPU/GPU analytic agreement above.
		if (fabsf(f[0])<128 && fabsf(f[1])<128 && f[2]<100) {
			const float swell_x=(depth_at(bytes,columns,22,i)-depth_at(bytes,columns,23,i))/.25f;
			const float swell_y=(depth_at(bytes,columns,24,i)-depth_at(bytes,columns,25,i))/.25f;
			const float swell_laplacian=(depth_at(bytes,columns,22,i)+depth_at(bytes,columns,23,i)
				+depth_at(bytes,columns,24,i)+depth_at(bytes,columns,25,i)-4*depth_at(bytes,columns,18,i))/.015625f;
			const float slope_error=fmaxf(fabsf(swell_x-depth_at(bytes,columns,19,i)),fabsf(swell_y-depth_at(bytes,columns,20,i)));
			const float curvature_error=fabsf(swell_laplacian-depth_at(bytes,columns,21,i));
			max_swell_gradient_error=fmaxf(max_swell_gradient_error,slope_error);
			max_swell_laplacian_error=fmaxf(max_swell_laplacian_error,curvature_error);
			CHECK(slope_error<.0015f && curvature_error<.0015f);
			const float step=.125f;
			// The shortest ripples make the three-point h=.125 stencil's O(h^2) truncation
			// exceed .005 at (0,0,1.7). Use the five-point O(h^4) stencil at the same spacing,
			// rather than weakening the bound or halving h and amplifying float cancellation.
			// Accumulate downloaded/CPU float heights in double; both evaluators stay production float.
			auto cpu_height_sum=[&](float h) {
				return double(EvaluateOcean(f[0]+h,f[1],f[2]).height)
					+EvaluateOcean(f[0]-h,f[1],f[2]).height
					+EvaluateOcean(f[0],f[1]+h,f[2]).height
					+EvaluateOcean(f[0],f[1]-h,f[2]).height;
			};
			const double cpu_near=cpu_height_sum(step),cpu_far=cpu_height_sum(2*step);
			const double gpu_near=double(depth_at(bytes,columns,14,i))+depth_at(bytes,columns,15,i)
				+depth_at(bytes,columns,16,i)+depth_at(bytes,columns,17,i);
			const double gpu_far=double(depth_at(bytes,columns,26,i))+depth_at(bytes,columns,27,i)
				+depth_at(bytes,columns,28,i)+depth_at(bytes,columns,29,i);
			const double gpu_center=depth_at(bytes,columns,7,i);
			const float coarse_cpu=float((cpu_near-4.0*ocean.height)/(step*step));
			const float coarse_gpu=float((gpu_near-4.0*gpu_center)/(step*step));
			const float numeric_cpu=float((16.0*cpu_near-cpu_far-60.0*ocean.height)/(12.0*step*step));
			const float numeric_gpu=float((16.0*gpu_near-gpu_far-60.0*gpu_center)/(12.0*step*step));
			const float cpu_error=fabsf(numeric_cpu-surface.laplacian);
			const float gpu_error=fabsf(numeric_gpu-gpu_curvature);
			const float cpu_coarse_error=fabsf(coarse_cpu-surface.laplacian);
			const float gpu_coarse_error=fabsf(coarse_gpu-gpu_curvature);
			max_cpu_curvature_error=fmaxf(max_cpu_curvature_error,cpu_error);
			max_gpu_curvature_error=fmaxf(max_gpu_curvature_error,gpu_error);
			max_cpu_coarse_curvature_error=fmaxf(max_cpu_coarse_curvature_error,cpu_coarse_error);
			max_gpu_coarse_curvature_error=fmaxf(max_gpu_coarse_curvature_error,gpu_coarse_error);
			CHECK(cpu_error<.005f && gpu_error<.005f);
			if (cpu_coarse_error>.002f && gpu_coarse_error>.002f
				&& cpu_error<cpu_coarse_error*.25f && gpu_error<gpu_coarse_error*.25f) ++curvature_refinements;
			++curvature_checks;
			// Armed control keeps the same warped phases and envelope, but deliberately omits their
			// spatial derivatives. Merely adding k-squared per-mode curvature must fail this fixture.
			const float q0=f[0]*.0241f+f[1]*.0157f-f[2]*.23f+.4f;
			const float q1=f[0]*-.0173f+f[1]*.0299f-f[2]*.19f+2.1f;
			const float s0=sinf(q0),s1=sinf(q1),px=f[0]+5*s0,py=f[1]+4*s1;
			float incomplete_laplacian=0;
#define TEST_OCEAN_UNWARPED_LAPLACIAN(kx,ky,amplitude,omega,phase) { \
			const float p=px*(kx)+py*(ky)-f[2]*(omega)+(phase); \
			const float s=sinf(p),c=cosf(p); \
			incomplete_laplacian+=(amplitude)*(-s-.64f*s*c)*((kx)*(kx)+(ky)*(ky)); }
			WATER_OCEAN_MODES(TEST_OCEAN_UNWARPED_LAPLACIAN)
#undef TEST_OCEAN_UNWARPED_LAPLACIAN
			incomplete_laplacian*=.86f+.14f*s0*s1;
			if (fabsf(numeric_cpu-incomplete_laplacian)>.008f) ++curvature_controls;
		}
	}
	CHECK(gradient_controls>=4);
	CHECK(curvature_checks>=4 && curvature_controls>=4);
	CHECK(curvature_refinements>=1); // a truncation-dominated case must improve on both CPU and GPU
	printf("sdl_gpu_frame_selfcheck: GPU ocean/blended waves: %u fixtures, CPU model error %.7f, numerical slopes CPU %.7f GPU %.7f, gradient controls %u\n",
		count,max_model_error,max_cpu_derivative_error,max_gpu_derivative_error,gradient_controls);
	printf("sdl_gpu_frame_selfcheck: GPU ocean curvature: model error %.7f, fourth-order height Laplacian CPU %.7f GPU %.7f (second-order %.7f/%.7f), %u checks, %u armed controls, %u stencil improvements\n",
		max_curvature_model_error,max_cpu_curvature_error,max_gpu_curvature_error,
		max_cpu_coarse_curvature_error,max_gpu_coarse_curvature_error,curvature_checks,curvature_controls,curvature_refinements);
	printf("sdl_gpu_frame_selfcheck: GPU macro swell: model error %.7f, numerical gradient %.7f Laplacian %.7f\n",
		max_swell_model_error,max_swell_gradient_error,max_swell_laplacian_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

#if defined(__linux__)
static void check_gpu_water_mesh_correction(SdlGpuFrame *frame)
{
	// Exercise the production water normal, including its UV0 metadata gate and fixed-function
	// vertex transport. A zero-damped wave leaves an independently known linear correction plane.
	// The wrapper only supplies vertices; both shader bodies below are the actual generators.
	const unsigned width=32,height=32;
	struct Resources {
		SDL_GPUDevice *device;
		SDL_GPUShader *vertices[2]={},*pixel=NULL;
		SDL_GPUGraphicsPipeline *pipelines[2]={};
		SDL_GPUTexture *target=NULL,*white=NULL;
		SDL_GPUSampler *sampler=NULL;
		~Resources() {
			for(unsigned i=0;i<2;++i) {
				if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vertices[i]) SDL_ReleaseGPUShader(device,vertices[i]);
			}
			if(pixel) SDL_ReleaseGPUShader(device,pixel);
			if(target) SDL_ReleaseGPUTexture(device,target);
			if(white) SDL_ReleaseGPUTexture(device,white);
			if(sampler) SDL_ReleaseGPUSampler(device,sampler);
		}
	} resources;
	resources.device=frame->Device();
	PixelPipelineDescription pixelDescription={};std::string pixelSource;
	const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pixelDescription,
		pixelSource,COMBINER_SHADER_TARGET_SDL3_GPU);
	CHECK(generated);if(!generated) return;
	if(!observe_water_normal(pixelSource)) return;
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);CHECK(compiled);
	if(!compiled) {printf("water correction pixel compile: %s\n",error.c_str());return;}
	unsigned samplers=0,pixelUniforms=0;SDL3_Shader_Slots(spirv,false,samplers,pixelUniforms);
	const unsigned expectedUniforms=4u;
	CHECK(samplers<=16 && pixelUniforms==expectedUniforms);if(samplers>16 || pixelUniforms!=expectedUniforms) return;
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	if(!resources.pixel) return;
	for(unsigned transformed=0;transformed<2;++transformed) {
		VertexPipelineDescription vertexDescription={};
		vertexDescription.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;
		vertexDescription.StageCount=4;
		vertexDescription.Stages[0].TextureCoordinateIndex=0;
		vertexDescription.Stages[0].TextureTransformFlags=transformed?FF_TTFF_COUNT2:FF_TTFF_DISABLE;
		vertexDescription.Stages[1].TextureCoordinateIndex=1;
		std::string vertexSource;
		CHECK(VertexShader_Generate(vertexDescription,VERTEX_SHADER_TARGET_SDL3_GPU,vertexSource));
		const std::string entry="Output main(Input input)";
		const size_t entryAt=vertexSource.find(entry);CHECK(entryAt!=std::string::npos);
		if(entryAt==std::string::npos) return;
		vertexSource.replace(entryAt,entry.size(),"Output waterFixtureVertex(Input input)");
		vertexSource+="\ncbuffer Fixture : register(b1, space1) { float4 Correction; };\n"
			"Output main(uint id : SV_VertexID) { Input input;\n"
			"input.Position=float3(id==1?3.0:-1.0,id==2?3.0:-1.0,0.5);\n"
			"input.Normal=float3(0,0,1);input.Diffuse=float4(1,1,1,1);\n"
			"input.TexCoord0=float2(dot(input.Position.xy,Correction.xy)+Correction.z,0);\n"
			"input.TexCoord1=float2(0,0.5);return waterFixtureVertex(input); }\n";
		compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water correction vertex compile: %s\n",error.c_str());return;}
		resources.vertices[transformed]=SDL3_Create_Shader(resources.device,spirv,true,error);
		CHECK(resources.vertices[transformed]);if(!resources.vertices[transformed]) return;
		SDL_GPUColorTargetDescription colour={};colour.format=(SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info={};
		info.vertex_shader=resources.vertices[transformed];info.fragment_shader=resources.pixel;
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
		info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		resources.pipelines[transformed]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);
		CHECK(resources.pipelines[transformed]);if(!resources.pipelines[transformed]) return;
	}
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format=(SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	textureInfo.width=width;textureInfo.height=height;textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&textureInfo);
	textureInfo.width=textureInfo.height=1;textureInfo.usage|=SDL_GPU_TEXTUREUSAGE_SAMPLER;
	resources.white=SDL_CreateGPUTexture(resources.device,&textureInfo);
	SDL_GPUSamplerCreateInfo samplerInfo={};samplerInfo.min_filter=samplerInfo.mag_filter=SDL_GPU_FILTER_NEAREST;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&samplerInfo);
	CHECK(resources.target && resources.white && resources.sampler);
	if(!resources.target || !resources.white || !resources.sampler) return;
	SdlVertexConstants vertexConstants={};SdlWaterPixelConstants water={};
	for(unsigned i=0;i<4;++i) {
		vertexConstants.WorldViewProjection[i*5]=vertexConstants.WorldView[i*5]=vertexConstants.NormalTransform[i*5]=1;
		water.WorldFromClip[i*5]=1;
		for(unsigned stage=0;stage<MAXIMUM_VERTEX_STAGES;++stage) vertexConstants.TextureMatrix[stage][i*5]=1;
	}
	// A stale matrix must be harmless with metadata passthrough; the armed alternate pipeline
	// enables it and must visibly double the correction gradient.
	vertexConstants.TextureMatrix[0][0]=2;vertexConstants.TextureMatrix[0][8]=13;
	water.Viewport[2]=width;water.Viewport[3]=height;water.CameraPosition[2]=10;water.SunDirection[2]=1;
	water.SunColour[0]=water.SunColour[1]=water.SunColour[2]=1;
	water.TerrainParameters[1]=100;water.SceneParameters[1]=water.SceneParameters[2]=1;
	water.OpticalParameters[0]=3;water.OpticalParameters[1]=1;
	float correction[4]={.35f,-.20f,1.25f,0};
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&](bool enabled,unsigned transformed) {
		water.OpticalParameters[3]=enabled?1.0f:0.0f;
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.white;
		attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
		attachment.clear_color.r=attachment.clear_color.g=attachment.clear_color.b=attachment.clear_color.a=1;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_EndGPURenderPass(pass);
		attachment.texture=resources.target;attachment.clear_color.r=attachment.clear_color.g=attachment.clear_color.b=0;
		pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
		SDL_BindGPUGraphicsPipeline(pass,resources.pipelines[transformed]);
		SDL_GPUTextureSamplerBinding bindings[16]={};
		for(unsigned i=0;i<samplers;++i) {bindings[i].texture=resources.white;bindings[i].sampler=resources.sampler;}
		if(samplers) SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
		SDL_PushGPUVertexUniformData(commands,0,&vertexConstants,sizeof(vertexConstants));
		SDL_PushGPUVertexUniformData(commands,1,correction,sizeof(correction));
		push_empty_visual_water(commands,water);
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
		std::vector<uint8_t> bytes;CHECK(frame->Read_Back(resources.target,width,height,bytes));++renders;return bytes;
	};
	auto checkNormal=[&](const std::vector<uint8_t>& bytes,float gx,float gy) {
		const float inverse=1.0f/sqrtf(1+gx*gx+gy*gy);
		const float expected[3]={.5f+.5f*inverse,.5f-.5f*gy*inverse,.5f-.5f*gx*inverse}; // BGR
		bool correct=bytes.size()==width*height*4;
		if(correct) for(unsigned y=4;y<height-4;++y) for(unsigned x=4;x<width-4;++x) for(unsigned c=0;c<3;++c)
			correct &= fabsf(bytes[(y*width+x)*4+c]/255.0f-expected[c])<2.0f/255.0f;
		CHECK(correct);
	};
	const auto legacy=render(false,0);checkNormal(legacy,0,0);
	const auto corrected=render(true,0);checkNormal(corrected,correction[0],correction[1]);CHECK(corrected!=legacy);
	correction[2]+=17;const auto offset=render(true,0);checkNormal(offset,correction[0],correction[1]);CHECK(offset==corrected);
	const auto transformed=render(true,1);checkNormal(transformed,2*correction[0],2*correction[1]);CHECK(transformed!=corrected);
	// Rotate projection by 90 degrees and independently invert it. A screen-space gradient used
	// directly as a world slope now has the wrong signs/axes even though the height field is linear.
	vertexConstants.WorldViewProjection[0]=vertexConstants.WorldViewProjection[5]=0;
	vertexConstants.WorldViewProjection[1]=1;vertexConstants.WorldViewProjection[4]=-1;
	water.WorldFromClip[0]=water.WorldFromClip[5]=0;water.WorldFromClip[1]=-1;water.WorldFromClip[4]=1;
	const auto rotated=render(true,0);checkNormal(rotated,correction[0],correction[1]);
	correction[0]=-.42f;correction[1]=.28f;
	checkNormal(render(true,0),correction[0],correction[1]);
	checkNormal(render(false,0),0,0);
	printf("sdl_gpu_frame_selfcheck: GPU production water correction: %u renders; known normals, legacy gate, constant offsets, projection rotation and armed texture-transform control passed\n",renders);
}
#endif // __linux__

static void check_gpu_water_optical_model(SdlGpuFrame *frame)
{
	// Execute the same light-transport function used by the generated water shader. Each row has
	// path, river blend, offshore blend, optical reference depth, authored opacity and padding.
	// Columns capture all three channels independently in an R32 target without display conversion.
	const float fixtures[][8] = {
		{0, 0, 1, 2.7f, .75f, 0, 0, 0},
		{.25f, 0, 1, 3, 1, 0, 0, 0}, // submerged object's short ray stays much clearer than the bed
		{10, 0, 0, 2.7f, .75f, 0, 0, 0},
		{10, 0, .5f, 2.7f, .75f, 0, 0, 0},
		{10, 0, 1, 2.7f, .75f, 0, 0, 0}, // mission sea optical interpretation
		{3.2f, 0, 0, 3, 1, 0, 0, 0},
		{3.2f, 0, .5f, 3, 1, 0, 0, 0},
		{3.2f, 0, 1, 3, 1, 0, 0, 0}, // shell sea optical interpretation
		{2, 1, 0, 2.7f, .75f, 0, 0, 0},
		{2, 1, .5f, 2.7f, .75f, 0, 0, 0},
		{2, 1, 1, 2.7f, .75f, 0, 0, 0},
		{180, 1, 0, 3, 0, 0, 0, 0},
		{360, 1, 0, 3, 0, 0, 0, 0}, // no old 180-world-unit plateau
		{100000, 1, 1, 3, 1, 0, 0, 0},
		{-4, -1, -1, -1, -1, 0, 0, 0},
		{.5f, 2, 2, 0, 2, 0, 0, 0},
		{10, .5f, .5f, 2.7f, .75f, 0, 0, 0},
		{1.0e20f, 0, 0, 1.0e20f, 0, 0, 0, 0}
	};
	const unsigned int count = sizeof(fixtures) / sizeof(fixtures[0]);
	const char *vertex_source =
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source = "cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source += std::to_string(count * 2) + "]; };\n";
	pixel_source += WaterOpticalModel::GetHlslSource();
	pixel_source += "float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y; float4 f=Fixtures[row*2];\n"
		"return WaterOpticalTransmission(f.x,f.y,f.z,f.w,Fixtures[row*2+1].x)[(uint)position.x]; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description = {};
	description.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.vertex_shader = vertex;
	pipeline_info.fragment_shader = pixel;
	pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions = &description;
	pipeline_info.target_info.num_color_targets = 1;
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &pipeline_info);
	SDL_GPUTextureCreateInfo texture_info = {};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width = 3; texture_info.height = count;
	texture_info.layer_count_or_depth = 1; texture_info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment = {};
	attachment.texture = target;
	attachment.load_op = SDL_GPU_LOADOP_DONT_CARE;
	attachment.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &attachment, 1, NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_PushGPUFragmentUniformData(commands, 0, fixtures, sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target, 3, count, bytes));
	float max_model_error = 0;
	for (unsigned int i=0; i<count; ++i) {
		const WaterOpticalModel::Rgb cpu=WaterOpticalModel::Transmission(
			fixtures[i][0],fixtures[i][1],fixtures[i][2],fixtures[i][3],fixtures[i][4]);
		const float expected[3]={cpu.r,cpu.g,cpu.b};
		for (unsigned int c=0; c<3; ++c) {
			const float gpu=depth_at(bytes,3,c,i),difference=fabsf(gpu-expected[c]);
			if (difference>max_model_error) max_model_error=difference;
			CHECK(gpu>=0 && gpu<=1 && difference<0.00001f);
			CHECK(difference<=expected[c]*0.00003f+1e-36f);
		}
	}
	for (unsigned int c=0;c<3;++c) {
		CHECK(depth_at(bytes,3,c,0)==1.0f);
		CHECK(depth_at(bytes,3,c,1)>depth_at(bytes,3,c,7));
		CHECK(depth_at(bytes,3,c,2)>depth_at(bytes,3,c,3) && depth_at(bytes,3,c,3)>depth_at(bytes,3,c,4));
		CHECK(depth_at(bytes,3,c,5)>depth_at(bytes,3,c,6) && depth_at(bytes,3,c,6)>depth_at(bytes,3,c,7));
		CHECK(depth_at(bytes,3,c,12)<depth_at(bytes,3,c,11));
	}
	printf("sdl_gpu_frame_selfcheck: GPU water optical transmission: %u fixtures, max CPU model error %.9f\n",count,max_model_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_optical_ray_shift(SdlGpuFrame *frame)
{
	// delta-height, view.z, offshore, displacement bound, actual scene ray distance, camera distance.
	const float fixtures[][8]={
		{2,.8f,0,7,20,100,0,0}, {2,.8f,1,7,20,100,0,0}, {-2,.8f,1,7,20,100,0,0},
		{2,.8f,.5f,7,20,100,0,0}, {1000,.8f,1,7,20,100,0,0}, {-1000,.8f,1,7,20,100,0,0},
		{1000,.001f,1,7,100,100,0,0}, {-1000,.001f,1,7,100,100,0,0},
		{1,0,1,7,20,100,0,0}, {1,-.8f,1,7,20,100,0,0},
		{-2,.8f,1,7,.25f,100,0,0}, {-2,.8f,1,7,4,100,0,0}, // near object and farther bed
		{2,.8f,1,7,20,.3f,0,0}, {-2,.8f,1,7,.03f,100,0,0}, {2,.8f,1,7,20,.04f,0,0},
		{2,.8f,-1,7,20,100,0,0}, {2,.8f,2,7,20,100,0,0}, {-1,.2f,.5f,7,2.25f,100,0,0},
		{-1000,.8f,1,0,20,100,0,0}
	};
	const unsigned int count=sizeof(fixtures)/sizeof(fixtures[0]),columns=4;
	const char *vertex_source=
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source="cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source+=std::to_string(count*2)+"]; };\n";
	pixel_source+=WaterOpticalModel::GetHlslSource();
	pixel_source+="float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y,column=(uint)position.x; float4 f=Fixtures[row*2],g=Fixtures[row*2+1];\n"
		"float shift=WaterOpticalRayShift(f.x,f.y,f.z,f.w,g.x,g.y);\n"
		"if (column==0) return shift;\n"
		"if (column==1) return 5+sqrt(max(1-f.y*f.y,0))*shift;\n"
		"if (column==2) return 12+f.y*shift;\n"
		"return WaterOpticalTransmission(abs(g.x+shift),0,1,3,.75).r; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};
	description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info={};
	pipeline_info.vertex_shader=vertex;pipeline_info.fragment_shader=pixel;
	pipeline_info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions=&description;
	pipeline_info.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipeline_info);
	SDL_GPUTextureCreateInfo texture_info={};
	texture_info.type=SDL_GPU_TEXTURETYPE_2D;texture_info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width=columns;texture_info.height=count;
	texture_info.layer_count_or_depth=1;texture_info.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands!=NULL);
	SDL_GPUColorTargetInfo attachment={};
	attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);
	CHECK(pass!=NULL);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);
	SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target,columns,count,bytes));
	float max_error=0;
	for (unsigned int i=0;i<count;++i) {
		using namespace WaterOpticalModel;
		const float *f=fixtures[i],cpu=RayShift(f[0],f[1],f[2],f[3],f[4],f[5]);
		const float vx=sqrtf(fmaxf(1-f[1]*f[1],0));
		const float expected[4]={cpu,5+vx*cpu,12+f[1]*cpu,Transmission(fabsf(f[4]+cpu),0,1,3,.75f).r};
		for (unsigned int c=0;c<columns;++c) {
			const float gpu=depth_at(bytes,columns,c,i),difference=fabsf(gpu-expected[c]);
			max_error=fmaxf(max_error,difference);
			CHECK(std::isfinite(gpu) && difference<.00003f);
		}
		const float shift=depth_at(bytes,columns,0,i),x=depth_at(bytes,columns,1,i),z=depth_at(bytes,columns,2,i);
		CHECK(fabsf(shift)<=2*f[3]/.2f+.00001f);
		CHECK(f[4]+shift>=fminf(f[4],RaySurfaceMargin)-.00001f);
		CHECK(f[5]-shift>=fminf(f[5],RaySurfaceMargin)-.00001f);
		CHECK(fabsf((x-5)*f[1]-(z-12)*vx)<.00001f);
		if (f[2]<=0 || f[1]<=0) CHECK(shift==0 && x==5 && z==12);
	}
	CHECK(depth_at(bytes,columns,0,1)==2.5f && depth_at(bytes,columns,0,2)==-2.5f);
	CHECK(depth_at(bytes,columns,0,3)==1.25f);
	CHECK(depth_at(bytes,columns,3,10)>depth_at(bytes,columns,3,11)); // object stays clearer than the bed
	const float wrong_object=WaterOpticalModel::Transmission(fabsf(.25f-2.5f),0,1,3,.75f).r;
	CHECK(wrong_object<depth_at(bytes,columns,3,11)); // control catches a shift that crossed the object
	CHECK(fabsf(depth_at(bytes,columns,0,12)-.22f)<.000001f);
	printf("sdl_gpu_frame_selfcheck: GPU optical ray correction: %u fixtures, max CPU/coordinate error %.9f; ray, bounds, near-object control passed\n",count,max_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_hull_distance(SdlGpuFrame *frame)
{
	using namespace WaterHullModel;
	const unsigned count=16,columns=4;
	struct Uniforms { Hull hulls[MaxHulls]; float fixtures[count][4]; } uniforms={};
	const Point rectangle[]={{-100,-20},{100,-20},{100,20},{-100,20}};
	for(unsigned vessel=0;vessel<MaxHulls;++vessel) {
		Hull &h=uniforms.hulls[vessel];
		h.data[0][0]=3028;h.data[0][1]=588;h.data[0][2]=7;h.data[0][3]=vessel==7?12.0f:4.0f;
		h.data[1][0]=vessel==0?1.0f:0.0f;h.data[1][1]=vessel==0?0.0f:-1.0f;
		h.data[1][2]=100;h.data[1][3]=vessel==7?100.0f:20.0f;
		for(unsigned i=0;i<MaxVertices;++i) {
			const float angle=float(i)*6.28318530718f/MaxVertices;
			const Point p=vessel==7?Point{100*cosf(angle),100*sinf(angle)}:rectangle[i%4];
			h.data[4+i/2][(i%2)*2]=p.x;h.data[4+i/2][(i%2)*2+1]=p.y;
		}
	}
	const Point local[]={{0,0},{0,20},{0,24.5f},{0,29},{100,0},{105,25},{-101,-21},{0,-100},
		{0,0},{0,24.5f},{105,25},{-101,-21},{0,0},{0,110},{98,-26},{86,-50}};
	for(unsigned i=0;i<count;++i) {
		const unsigned index=i<8?0:i<12?3:7;
		const Hull &h=uniforms.hulls[index];const Point p=local[i];
		uniforms.fixtures[i][0]=h.data[0][0]+p.x*h.data[1][0]-p.y*h.data[1][1];
		uniforms.fixtures[i][1]=h.data[0][1]+p.x*h.data[1][1]+p.y*h.data[1][0];
		uniforms.fixtures[i][2]=float(index);
	}
	const char *vertexSource="float4 main(uint id : SV_VertexID) : SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string pixelSource="cbuffer Samples : register(b0, space3) { float4 HullData[80]; float4 Fixtures[16]; };\n";
	pixelSource+=GetHlslSource();
	pixelSource+="float main(float4 position : SV_Position) : SV_Target { uint row=(uint)position.y,column=(uint)position.x; "
		"float4 f=Fixtures[row]; int baseIndex=(int)f.z*10; float2 delta=f.xy-HullData[baseIndex].xy,axis=HullData[baseIndex+1].xy; "
		"float2 local=float2(dot(delta,axis),dot(delta,float2(-axis.y,axis.x))); "
		"float distance=WaterHullSignedDistance(local,baseIndex,(int)HullData[baseIndex].w); "
		"if(column==0) return distance; if(column==1) return smoothstep(0,9,distance); return local[column-2]; }";
	std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};
	pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pipelineInfo.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipelineInfo.target_info.color_target_descriptions=&description;pipelineInfo.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;textureInfo.width=columns;textureInfo.height=count;
	textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&textureInfo);CHECK(pipeline && target);
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
	SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUFragmentUniformData(commands,0,&uniforms,sizeof(uniforms));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;CHECK(frame->Read_Back(target,columns,count,bytes));float maxError=0;
	for(unsigned i=0;i<count;++i) {
		const Hull &h=uniforms.hulls[(unsigned)uniforms.fixtures[i][2]];
		const Point p=LocalPoint(h,uniforms.fixtures[i][0],uniforms.fixtures[i][1]);
		const float cpu=SignedDistance(p,Unpack(h)),damping=Damping(h,uniforms.fixtures[i][0],uniforms.fixtures[i][1],7);
		const float expected[]={cpu,damping,p.x,p.y};
		for(unsigned c=0;c<4;++c) {const float e=fabsf(depth_at(bytes,columns,c,i)-expected[c]);maxError=fmaxf(maxError,e);CHECK(e<.0003f);}
		if(i<12) { // Independent exact rectangle SDF, including outside corners and inside sign.
			const float qx=fabsf(local[i].x)-100,qy=fabsf(local[i].y)-20;
			const float exact=sqrtf(fmaxf(qx,0)*fmaxf(qx,0)+fmaxf(qy,0)*fmaxf(qy,0))+fminf(fmaxf(qx,qy),0);
			CHECK(fabsf(depth_at(bytes,columns,0,i)-exact)<.0003f);
		}
	}
	CHECK(depth_at(bytes,columns,0,0)<0 && depth_at(bytes,columns,0,2)>0);
	CHECK(fabsf(depth_at(bytes,columns,1,2)-.5f)<.00001f);
	// Last vertex matters: collapsing it measurably moves the lower-right edge of the last hull.
	Polygon missing=Unpack(uniforms.hulls[7]);--missing.count;
	CHECK(fabsf(SignedDistance(local[14],missing)-depth_at(bytes,columns,0,14))>2);
	printf("sdl_gpu_frame_selfcheck: GPU hull contours: %u fixtures, max CPU/rotation error %.7f; exact rectangle, final vertex, sign and damping controls passed\n",count,maxError);
	SDL_ReleaseGPUTexture(frame->Device(),target);SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);SDL_ReleaseGPUShader(frame->Device(),pixel);
}

// These are renderer fixtures, not injected gameplay velocities. The stable USA02 save has
// stationary ships and no PT-boat patrol script, so it cannot validate moving-vessel integration.
// Render the production wake function over an independently specified rectangular waterline.
#if defined(__linux__)
static void check_gpu_water_hull_effects(SdlGpuFrame *frame)
{
	using namespace WaterHullModel;
	const unsigned samplesX=301,samplesY=81,channels=3,width=samplesX*channels;
	struct Uniforms {
		float hullParameters[4]; Hull hulls[MaxHulls];
		float query[4],testBasis[4],waveInput[4]; // level,time,offshore,footprint; origin XY,forward XY; height/impact
		Hull contactHulls[MaxHulls];
	} uniforms={};
	uniforms.hullParameters[0]=1;
	uniforms.query[0]=7;uniforms.query[1]=1.25f;uniforms.query[2]=1;uniforms.query[3]=.5f;
	uniforms.testBasis[2]=1;
	uniforms.waveInput[0]=.75f;uniforms.waveInput[1]=.6f;
	Hull &h=uniforms.hulls[0];
	h.data[0][2]=7;h.data[0][3]=4;
	h.data[1][0]=1;h.data[1][2]=20;h.data[1][3]=4;
	h.data[2][2]=.37f;h.data[2][3]=sqrtf(20*20+4*4);
	h.data[3][0]=-20;h.data[3][2]=20;
	const Point rectangle[]={{-20,-4},{20,-4},{20,4},{-20,4}};
	for(unsigned i=0;i<MaxVertices;++i) {
		h.data[4+i/2][(i%2)*2]=rectangle[i%4].x;
		h.data[4+i/2][(i%2)*2+1]=rectangle[i%4].y;
	}
	const Uniforms stationaryUniforms=uniforms;
	PixelPipelineDescription waterPipeline={};
	std::string waterSource;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,waterPipeline,waterSource,COMBINER_SHADER_TARGET_SDL3_GPU));
	// Import the exact production noise dependency, not a test-only constant or copy that could
	// hide broken animated wash. The tested effects body is also the production header emission.
	const size_t noiseStart=waterSource.find("float oceanHash("),noiseEnd=waterSource.find("float3 oceanSky(");
	CHECK(noiseStart!=std::string::npos && noiseEnd!=std::string::npos && noiseEnd>noiseStart);
	if(noiseStart==std::string::npos || noiseEnd==std::string::npos || noiseEnd<=noiseStart) return;
	const char *vertexSource="float4 main(uint id : SV_VertexID) : SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string pixelSource="cbuffer Samples : register(b0, space3) { float4 HullParameters; float4 HullData[80]; float4 Query; float4 TestBasis; float4 WaveInput; float4 ContactHullData[80]; };\n";
	pixelSource+=waterSource.substr(noiseStart,noiseEnd-noiseStart);
	pixelSource+="static const float4 VisualHullMeta[8]={0,0,0,0,0,0,0,0};\n";
	pixelSource+=GetHlslSource();pixelSource+=GetEffectsHlslSource();
	pixelSource+="float main(float4 pixel : SV_Position) : SV_Target { "
		"uint column=(uint)pixel.x, channel=column%3; float2 local=float2((int)(column/3)-150,(int)pixel.y-40); "
		"float2 world=TestBasis.xy+local.x*TestBasis.zw+local.y*float2(-TestBasis.w,TestBasis.z); "
		"return oceanHullEffects(world,Query.x,Query.y,Query.z,WaveInput.x,Query.w,WaveInput.y)[channel]; }";
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);CHECK(compiled);
	if(!compiled) {printf("wake vertex compile: %s\n",error.c_str());return;}
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);CHECK(compiled);
	if(!compiled) {printf("wake pixel compile: %s\n",error.c_str());SDL_ReleaseGPUShader(frame->Device(),vertex);return;}
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(vertex && pixel);
	if(!vertex || !pixel) return;
	SDL_GPUColorTargetDescription description={};description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pipelineInfo.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipelineInfo.target_info.color_target_descriptions=&description;pipelineInfo.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;textureInfo.width=width;textureInfo.height=samplesY;
	textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&textureInfo);CHECK(pipeline && target);
	if(!pipeline || !target) return;
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&]() {
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
		SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUFragmentUniformData(commands,0,&uniforms,sizeof(uniforms));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
		std::vector<uint8_t> bytes;CHECK(frame->Read_Back(target,width,samplesY,bytes));++renders;
		bool bounded=true;
		for(unsigned y=0;y<samplesY;++y) for(unsigned x=0;x<width;++x) {
			const float v=depth_at(bytes,width,x,y);bounded &= std::isfinite(v) && v>=0 && v<=1;
		}
		CHECK(bounded);return bytes;
	};
	auto value=[&](const std::vector<uint8_t>& bytes,int x,int y,unsigned channel) {
		return depth_at(bytes,width,unsigned(x+150)*channels+channel,unsigned(y+40));
	};
	auto empty=[&](const std::vector<uint8_t>& bytes) {
		bool ok=true;
		for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
			ok &= value(bytes,x,y,0)==0 && value(bytes,x,y,1)==0 && value(bytes,x,y,2)==1;
		CHECK(ok);
	};
	const auto stationary=render();
	// An idle hull needs incoming-wave impact for foam. Its thin, broken side wash cannot turn
	// into a propulsion trail or fill the hull interior; count clean and foamy sections separately.
	unsigned sideSamples=0,sideFoamy=0,sideClean=0;bool washBounded=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) {
		const float dx=fmaxf(fabsf(float(x))-20,0),dy=fmaxf(fabsf(float(y))-4,0);
		const float distance=sqrtf(dx*dx+dy*dy),foam=value(stationary,x,y,0);
		if(abs(x)<=15 && abs(y)>=5 && abs(y)<=8) {
			++sideSamples;if(foam>.005f)++sideFoamy;if(foam<.005f)++sideClean;
		}
		if(distance>=7 || (abs(x)<20 && abs(y)<4))
			washBounded &= foam==0 && value(stationary,x,y,1)==0;
	}
	CHECK(washBounded && sideFoamy>8 && sideClean>sideSamples/4);
	// A flat crest still has contact energy; calm requires both height and slope zero.
    uniforms.waveInput[0]=WaterHullContactFoam::HeightScale;uniforms.waveInput[1]=0;
    const auto crestContact=render();CHECK(value(crestContact,0,5,0)>.24f);
    uniforms.waveInput[0]=-WaterHullContactFoam::HeightScale;
    const auto troughContact=render();CHECK(value(troughContact,0,5,0)==0);
	uniforms.waveInput[0]=0;uniforms.waveInput[1]=0;const auto calm=render();bool impactGated=true;
	for(int y=-40;y<=40;++y)for(int x=-150;x<=150;++x)
		impactGated &= value(calm,x,y,0)==0 && value(calm,x,y,1)==0 && value(calm,x,y,2)==value(stationary,x,y,2);
	CHECK(impactGated);uniforms=stationaryUniforms;
	uniforms.hulls[0].data[2][0]=.3f;const auto threshold=render();CHECK(threshold==stationary);
	uniforms.hulls[0].data[2][0]=6.3f;const auto forward=render();
	// Fresh turbulence stays attached to the actual stern, with a broad central region. The
	// persistent world-space atlas now owns the long wake, so this field must remain local.
	bool connected=true;
	for(int behind=1;behind<=10;++behind) {
		connected &= value(forward,-20-behind,0,1)>.05f;
	}
	CHECK(connected);
	CHECK(value(forward,-35,0,1)>.1f && value(stationary,-35,0,1)==0);
	CHECK(value(forward,-30,0,1)>.15f && value(forward,-30,12,1)<.01f && value(forward,-30,-12,1)<.01f);
	float bowIncrease=0,sideIncrease=0;
	for(int x=20;x<=23;++x)for(int y=-2;y<=2;++y)
		bowIncrease=fmaxf(bowIncrease,value(forward,x,y,0)-value(stationary,x,y,0));
	for(int x=-10;x<=10;++x)for(int y:{-5,5})
		sideIncrease=fmaxf(sideIncrease,value(forward,x,y,0)-value(stationary,x,y,0));
	CHECK(bowIncrease>.05f && bowIncrease>sideIncrease); // bow response is concentrated, not a uniform halo
	bool forwardBounds=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
		if(x<=-44 || x>=33) forwardBounds &= value(forward,x,y,0)==0 && value(forward,x,y,1)==0;
	CHECK(forwardBounds);
	// Heading and world translation must not move the wake relative to its actual hull. A 90deg
	// rotation is exact in floating point, so this is sensitive to world/local velocity mistakes.
	uniforms.hulls[0].data[0][0]=3028;uniforms.hulls[0].data[0][1]=588;
	uniforms.hulls[0].data[1][0]=0;uniforms.hulls[0].data[1][1]=1;
	uniforms.hulls[0].data[2][0]=0;uniforms.hulls[0].data[2][1]=6.3f;
	uniforms.testBasis[0]=3028;uniforms.testBasis[1]=588;uniforms.testBasis[2]=0;uniforms.testBasis[3]=1;
	const auto rotated=render();CHECK(rotated==forward);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=-6.3f;
	const auto reverse=render();
	bool reversed=true;
	for(int ahead=1;ahead<=10;++ahead) reversed &= value(reverse,20+ahead,0,1)>.05f;
	CHECK(reversed && value(reverse,35,0,1)>.1f && value(reverse,-35,0,1)==0);
	CHECK(value(forward,35,0,1)==0); // Armed wrong-end control: forward's bow must not emit a trail.
	// Near sideways travel the physical wave origin blends across the hull. Foam must use the
	// same continuous endpoint selection, instead of jumping forty units on a velocity sign flip.
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=.00001f;uniforms.hulls[0].data[2][1]=6.3f;
	const auto broadsidePositive=render();uniforms.hulls[0].data[2][0]=-.00001f;
	const auto broadsideNegative=render();float broadsideChange=0;
	CHECK(value(broadsidePositive,0,-15,1)>.1f && value(broadsideNegative,0,-15,1)>.1f);
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) for(unsigned c=0;c<3;++c)
		broadsideChange=fmaxf(broadsideChange,fabsf(value(broadsidePositive,x,y,c)-value(broadsideNegative,x,y,c)));
	CHECK(broadsideChange<.002f);
	// Force both endpoint choices to each old hard-selected end. This armed control must expose
	// a large jump in exactly the atlas and travel direction used by the continuity assertion.
	uniforms.hulls[0].data[3][0]=uniforms.hulls[0].data[3][2]=-20;const auto forcedLeft=render();
	uniforms.hulls[0].data[3][0]=uniforms.hulls[0].data[3][2]=20;const auto forcedRight=render();
	CHECK(value(forcedLeft,-20,-15,1)-value(forcedRight,-20,-15,1)>.1f);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;
	uniforms.query[2]=0;empty(render());
	uniforms.query[2]=1;uniforms.query[0]=7.11f;empty(render());
	uniforms.query[0]=7;uniforms.hullParameters[0]=0;empty(render());
	uniforms.hullParameters[0]=1;uniforms.query[2]=.25f;const auto transition=render();
	bool blended=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) for(unsigned c=0;c<3;++c)
		blended &= fabsf(value(transition,x,y,c)-value(forward,x,y,c)*(c==2?1.0f:.25f))<.000001f;
	CHECK(blended);
	uniforms.query[2]=1;const auto paused=render();CHECK(paused==forward);
	uniforms.query[1]+=1.0f;const auto animated=render();unsigned changed=0;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
		if(fabsf(value(animated,x,y,0)-value(forward,x,y,0))>.005f) ++changed;
	CHECK(changed>50);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=60;
	const auto capped=render();bool lengthBounded=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=-130;++x)
		lengthBounded &= value(capped,x,y,0)==0 && value(capped,x,y,1)==0;
	for(int y=-40;y<=40;++y)for(int x=-150;x<=-65;++x)
		lengthBounded &= value(capped,x,y,0)==0 && value(capped,x,y,1)==0;
	CHECK(lengthBounded && value(capped,-100,0,1)==0 && value(capped,-35,0,1)>2*value(forward,-35,0,1));
	// All eight array entries are legal. Only the final hull matches this level: a truncated count
	// or bad ten-register stride must lose the entire result, rather than accidentally pass.
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;
	uniforms.hulls[7]=uniforms.hulls[0];uniforms.hulls[0]=Hull();uniforms.hullParameters[0]=8;
	const auto lastHull=render();CHECK(lastHull==forward);
	// Contact geometry may be wider and offset from the exact mean-water slice. Construct an
	// independent reference by moving the original mean-only fixture to that shape, then require
	// only foam/churn to match it: the new contact path must retain the original damping field.
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;
	Hull contact=uniforms.hulls[0];contact.data[0][0]=3;contact.data[0][1]=7;
	contact.data[1][3]=10;contact.data[2][3]=sqrtf(20*20+10*10);
	for(unsigned i=0;i<MaxVertices;++i) contact.data[4+i/2][(i%2)*2+1]*=2.5f;
	uniforms.hulls[0]=contact;const auto contactReference=render();
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;uniforms.contactHulls[0]=contact;
	const auto contactResult=render();bool contactMatches=true,meanDampingPreserved=true;
	unsigned newContourFoam=0;float armedContactDifference=0;
	for(int y=-40;y<=40;++y)for(int x=-150;x<=150;++x) {
		for(unsigned c=0;c<2;++c) contactMatches &= value(contactResult,x,y,c)==value(contactReference,x,y,c);
		meanDampingPreserved &= value(contactResult,x,y,2)==value(forward,x,y,2);
		armedContactDifference=fmaxf(armedContactDifference,fabsf(value(contactResult,x,y,0)-value(forward,x,y,0)));
		if(y>=18 && y<=20 && x>=-12 && x<=18 && value(contactResult,x,y,0)>.005f && value(forward,x,y,0)==0)
			++newContourFoam;
	}
	CHECK(contactMatches && meanDampingPreserved && newContourFoam>8 && armedContactDifference>.1f);
	// The optional count alone controls fallback, regardless of stale remaining registers.
	uniforms.contactHulls[0].data[0][3]=2;CHECK(render()==forward);
	uniforms.contactHulls[0]=Hull();CHECK(render()==forward);
	// A valid contact is paired with its mean hull index, including the last array entry. It
	// must obey the same local-to-world rotation as an ordinary contour, and its water level.
	uniforms.contactHulls[0]=contact;uniforms.contactHulls[7]=contact;
	uniforms.contactHulls[0]=Hull();uniforms.hulls[7]=uniforms.hulls[0];uniforms.hulls[0]=Hull();uniforms.hullParameters[0]=8;
	CHECK(render()==contactResult);
	uniforms.contactHulls[7].data[0][2]=7.11f;const auto wrongContactLevel=render();bool contactLevelGated=true;
	for(int y=-40;y<=40;++y)for(int x=-150;x<=150;++x)
		contactLevelGated &= value(wrongContactLevel,x,y,0)==0 && value(wrongContactLevel,x,y,1)==0
			&& value(wrongContactLevel,x,y,2)==value(forward,x,y,2);
	CHECK(contactLevelGated);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;uniforms.contactHulls[0]=contact;
	for(Hull *v:{&uniforms.hulls[0],&uniforms.contactHulls[0]}) {
		const float oldX=v->data[0][0],oldY=v->data[0][1];
		v->data[0][0]=3028-oldY;v->data[0][1]=588+oldX;
		v->data[1][0]=0;v->data[1][1]=1;v->data[2][0]=0;v->data[2][1]=6.3f;
	}
	uniforms.testBasis[0]=3028;uniforms.testBasis[1]=588;uniforms.testBasis[2]=0;uniforms.testBasis[3]=1;
	CHECK(render()==contactResult);
	printf("sdl_gpu_frame_selfcheck: GPU separate contact hull: shifted/wider foam matches independent mean-only reference, %u new contour foam samples, armed mean-only difference %.6f, mean damping exact, invalid/zero fallback, last hull, level and rigid transform checked\n",newContourFoam,armedContactDifference);
	printf("sdl_gpu_frame_selfcheck: GPU production hull wash/wake: %u spatial renders, %u pixels each; thin side wash %u/%u with %u clean sections, impact gate, concentrated bow %.5f versus side %.5f, local central stern churn, speed/reverse/broadside continuity %.8f with armed snap control, rigid transform, level/offshore, pause, animation (%u foam pixels), 44-unit stern bound and final hull checked\n",renders,samplesX*samplesY,sideFoamy,sideSamples,sideClean,bowIncrease,sideIncrease,broadsideChange,changed);
	SDL_ReleaseGPUTexture(frame->Device(),target);SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);SDL_ReleaseGPUShader(frame->Device(),pixel);
}
#endif // __linux__

// A float readback used only by verification. Runtime FFT never reads a result back to the CPU.
static std::vector<float> read_float_targets(SdlGpuFrame *frame,SDL_GPUTexture *const *textures,
	unsigned count,unsigned width,unsigned height,unsigned mip=0)
{
	const unsigned bytes=width*height*4*sizeof(float),total=count*bytes;
	std::vector<float> result(total/sizeof(float));
	CHECK(frame->Flush());
	SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=total;
	SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(frame->Device(),&info);CHECK(transfer);
	if(!transfer) return result;
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
	if(!commands) {SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);return result;}
	SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
	for(unsigned i=0;i<count;++i) {
		SDL_GPUTextureRegion region={};region.texture=textures[i];region.w=width;region.h=height;region.d=1;region.mip_level=mip;
		SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;destination.offset=i*bytes;
		destination.pixels_per_row=width;destination.rows_per_layer=height;
		SDL_DownloadFromGPUTexture(copy,&region,&destination);
	}
	SDL_EndGPUCopyPass(copy);SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
	if(fence) {
		CHECK(SDL_WaitForGPUFences(frame->Device(),true,&fence,1));SDL_ReleaseGPUFence(frame->Device(),fence);
		const void *mapped=SDL_MapGPUTransferBuffer(frame->Device(),transfer,false);CHECK(mapped);
		if(mapped) {memcpy(result.data(),mapped,total);SDL_UnmapGPUTransferBuffer(frame->Device(),transfer);}
	}
	SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);return result;
}

// Verify depth-aware colour filtering with an explicit independent valid-tap case table.
#if defined(__linux__)
static void check_gpu_water_refraction_footprint(SdlGpuFrame *frame)
{
    const unsigned rows=27,columns=3;
    struct Resources {
        SDL_GPUDevice *device;
        SDL_GPUShader *vertex=NULL,*pixel=NULL;
        SDL_GPUGraphicsPipeline *pipeline=NULL;
        SDL_GPUTexture *target=NULL,*colour=NULL,*depth=NULL,*white=NULL;
        SDL_GPUSampler *linear=NULL,*point=NULL;
        ~Resources() {
            if(pipeline) SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vertex) SDL_ReleaseGPUShader(device,vertex);
            if(pixel) SDL_ReleaseGPUShader(device,pixel);
            if(target) SDL_ReleaseGPUTexture(device,target);
            if(colour) SDL_ReleaseGPUTexture(device,colour);
            if(depth) SDL_ReleaseGPUTexture(device,depth);
            if(white) SDL_ReleaseGPUTexture(device,white);
            if(linear) SDL_ReleaseGPUSampler(device,linear);
            if(point) SDL_ReleaseGPUSampler(device,point);
        }
    } r;
    r.device=frame->Device();
    PixelPipelineDescription pd={};std::string ps;
    const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,ps,
        COMBINER_SHADER_TARGET_SDL3_GPU);
    CHECK(generated);if(!generated)return;
    std::string production;production.swap(ps);
    if(!water_refraction_fixture_source(production,ps))return;
    ps+=R"M4C(
cbuffer M4cCases : register(b1, space3) { float4 M4cFixtures[27 * 3]; };
// Armed M4A nearest-only control, retaining its corrected sign/shared viewport behaviour.
WaterRefractionSample m4cNearestOnly(float2 originalPixel,float2 offset,float3 world,
    float3 opticalWorld,float3 view,float3 originalBottom)
{
    WaterRefractionSample r;
    float2 origin=waterScenePixel(originalPixel),candidate=waterScenePixel(originalPixel+offset);
    float3 hit=bottomPosition(candidate,world,0);
    float valid=step(hit.z,min(world.z,opticalWorld.z)-.08)
        *step(0.0,min(dot(hit-world,-view),dot(hit-opticalWorld,-view)));
    r.pixel=lerp(origin,candidate,valid);r.bottom=lerp(originalBottom,hit,valid);r.accepted=valid;
    r.colour=Texture5.SampleLevel(Sampler5,r.pixel*SceneParameters.yz,0).rgb;
    return r;
}
float4 main(float4 position:SV_Position):SV_Target0
{
    uint row=(uint)position.y,column=(uint)position.x;
    float4 a=M4cFixtures[row*3],b=M4cFixtures[row*3+1],c=M4cFixtures[row*3+2];
    float3 world=scenePosition(a.xy,0),opticalWorld=world+b.xyz*b.w;
    float3 originalBottom=bottomPosition(a.xy,world,0);
    WaterRefractionSample s;
    if(c.x>.5) s=m4cNearestOnly(a.xy,a.zw,world,opticalWorld,b.xyz,originalBottom);
    else s=waterRefractedScene(a.xy,a.zw,world,opticalWorld,b.xyz,0,originalBottom);
    if(column==0)return float4(s.pixel,s.accepted,length(s.bottom-opticalWorld));
    if(column==1)return float4(s.colour,1);
    return float4(s.bottom,1);
}

)M4C";
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position { return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); }";
    std::string error;std::vector<unsigned char> spirv;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
    r.vertex=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vertex);if(!r.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
    if(!compiled){printf("M4D production helper compile: %s\n",error.c_str());return;}
    unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);
    CHECK(samplers<=16 && uniforms==2);if(samplers>16 || uniforms!=2)return;
    r.pixel=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.pixel);if(!r.pixel)return;
    SDL_GPUColorTargetDescription colourDescription={};colourDescription.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vertex;pi.fragment_shader=r.pixel;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions=&colourDescription;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colourDescription.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    SDL_GPUSamplerCreateInfo si={};si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    r.linear=SDL_CreateGPUSampler(r.device,&si);si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;
    r.point=SDL_CreateGPUSampler(r.device,&si);CHECK(r.linear && r.point);if(!r.linear || !r.point)return;
    const float offsets[rows][2]={
        {.25f,.375f},{.25f,0},{0,.25f},{.25f,.25f},{0,0},{.75f,0},{.25f,0},
        {.25f,0},{.25f,0},{.25f,0},{8,0},{8,8},{-.25f,-.25f},{.75f,.875f},
        {.25f,0},{-8,0},{.25f,0},{.25f,0},{.5f,.5f},{.75f,.25f},{.25f,.75f},
        {.75f,.375f},{.75f,.75f},{.25f,.375f},{.25f,.375f},{.75f,.875f},{.5f,.5f}
    };
    // Independent case truth: bit0=lower-left in texture coordinates (x,y), bit1=(x+1,y),
    // bit2=(x,y+1), bit3=(x+1,y+1). These masks are authored, not recomputed from shader rules.
    const unsigned validTaps[rows]={15,13,11,7,13,13,13,13,13,13,15,15,14,15,13,15,13,15,15,15,15,
        10,8,15,7,7,8};
    const bool accepted[rows]={true,true,true,true,true,false,true,true,true,true,
        true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,false,true};
    unsigned renders=0,armedLeaks=0,checked=0,missingColourChecks=0;
    CHECK(frame->Flush());
    for(unsigned resize=0;resize<2;++resize) {
        const unsigned width=40u<<resize,height=(rows*4+12)<<resize;
        const float viewportWidth=24,viewportHeight=float(rows*4+4),lastX=27.5f,lastY=3.5f+viewportHeight-1;
        float cases[rows][12]={};
        for(unsigned row=0;row<rows;++row){
            cases[row][0]=9.5f;cases[row][1]=5.5f+row*4;
            cases[row][2]=offsets[row][0];cases[row][3]=offsets[row][1];cases[row][6]=1;
        }
        cases[7][7]=-.4f;cases[8][7]=.4f;
        cases[9][4]=.8f;cases[9][6]=.6f; // below z, but in front along the raster view ray
        cases[10][0]=lastX;cases[11][0]=lastX;cases[11][1]=lastY;
        cases[14][4]=.6f;cases[14][6]=.8f;cases[14][7]=-.5f; // forward of raster, behind optical surface
        cases[15][0]=4.5f;
        ti.format=colourDescription.format;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;ti.width=width;ti.height=height;
        r.colour=SDL_CreateGPUTexture(r.device,&ti);ti.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        r.depth=SDL_CreateGPUTexture(r.device,&ti);ti.format=colourDescription.format;ti.width=ti.height=1;
        r.white=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.colour && r.depth && r.white);
        if(!r.colour || !r.depth || !r.white)return;
        std::vector<float> pixels(width*height*4),depth(width*height,.5f);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const unsigned p=(y*width+x)*4;
            const bool outside=x<4 || x>27 || y<3 || y>unsigned(lastY);
            pixels[p]=outside?1.0f:float(x)/width;pixels[p+1]=outside?0.0f:float(y)/height;
            pixels[p+2]=outside?1.0f:0.0f;pixels[p+3]=1;
            if(outside)depth[y*width+x]=0; // even invalid off-viewport neighbours must be ignored
        }
        auto mark=[&](unsigned row,int dx,int dy,float d){
            const unsigned x=unsigned(int(cases[row][0])+dx),y=unsigned(int(cases[row][1])+dy),p=(y*width+x)*4;
            depth[y*width+x]=d;pixels[p]=pixels[p+1]=0;pixels[p+2]=1;
        };
        mark(1,1,0,0);mark(2,0,1,0);mark(3,1,1,0);mark(4,1,0,0);mark(5,1,0,0);
        mark(6,1,0,1);mark(7,1,0,.02f);mark(8,1,0,.004f);mark(9,1,0,.02f);
        mark(12,-1,-1,0);mark(14,1,0,.1f);mark(16,1,0,.0079f);mark(17,1,0,.0081f);
        // Row21: original is clear (terrain fallback would be20 deep), nearest displaced hit
        // is valid5 deep. Losing a colour neighbour must not switch this selected ray.
        mark(21,0,0,1);mark(21,0,1,0);
        // Only the nearest survives; row26 exercises the minimum possible surviving weight1/4.
        for(unsigned row:{22u,26u}) {mark(row,0,0,0);mark(row,1,0,1);mark(row,0,1,-.1f);}
        mark(25,1,1,0); // invalid nearest must still reject even with three valid neighbours
        auto paint=[&](unsigned row,int dx,int dy,float d,float red,float green,float blue){
            const unsigned x=unsigned(int(cases[row][0])+dx),y=unsigned(int(cases[row][1])+dy),p=(y*width+x)*4;
            depth[y*width+x]=d;pixels[p]=red;pixels[p+1]=green;pixels[p+2]=blue;
        };
        for(unsigned row:{23u,24u}) {
            paint(row,0,0,.2f,1,0,0);paint(row,1,0,.3f,0,1,0);
            paint(row,0,1,.6f,0,0,1);paint(row,1,1,row==24?0.0f:.9f,1,1,1);
        }
        const unsigned colourBytes=unsigned(pixels.size()*sizeof(float)),depthBytes=unsigned(depth.size()*sizeof(float));
        SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;bi.size=colourBytes+depthBytes+16;
        SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(r.device,&bi);CHECK(transfer);if(!transfer)return;
        unsigned char *mapped=(unsigned char*)SDL_MapGPUTransferBuffer(r.device,transfer,false);CHECK(mapped);
        if(!mapped){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        memcpy(mapped,pixels.data(),colourBytes);memcpy(mapped+colourBytes,depth.data(),depthBytes);
        const float white[4]={1,1,1,1};memcpy(mapped+colourBytes+depthBytes,white,sizeof(white));SDL_UnmapGPUTransferBuffer(r.device,transfer);
        SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(r.device);CHECK(upload);
        if(!upload){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);
        if(!copy){SDL_CancelGPUCommandBuffer(upload);SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        SDL_GPUTexture *textures[3]={r.colour,r.depth,r.white};const unsigned uploadOffsets[3]={0,colourBytes,colourBytes+depthBytes};
        for(unsigned i=0;i<3;++i){
            SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.offset=uploadOffsets[i];
            from.pixels_per_row=i==2?1:width;from.rows_per_layer=i==2?1:height;
            SDL_GPUTextureRegion to={};to.texture=textures[i];to.w=i==2?1:width;to.h=i==2?1:height;to.d=1;
            SDL_UploadToGPUTexture(copy,&from,&to,false);
        }
        SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(r.device,transfer);
        SdlWaterPixelConstants water={};water.WorldFromClip[0]=viewportWidth/2;water.WorldFromClip[5]=viewportHeight/2;
        water.WorldFromClip[10]=-10;water.WorldFromClip[15]=1;
        water.Viewport[0]=4.5f;water.Viewport[1]=3.5f;water.Viewport[2]=viewportWidth;water.Viewport[3]=viewportHeight;
        water.SceneParameters[0]=1;water.SceneParameters[1]=1.0f/width;water.SceneParameters[2]=1.0f/height;
        water.TerrainParameters[1]=1;
        auto render=[&](bool old,bool depthAvailable,bool colourAvailable){
            for(unsigned row=0;row<rows;++row)cases[row][8]=old?1.0f:0.0f;
            water.SceneParameters[0]=colourAvailable?1.0f:0.0f;
            water.SceneParameters[3]=depthAvailable?1.0f:0.0f;
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return std::vector<float>();
            SDL_GPUColorTargetInfo attachment={};attachment.texture=r.target;attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
            if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>();}
            SDL_BindGPUGraphicsPipeline(pass,r.pipeline);SDL_GPUTextureSamplerBinding bindings[16]={};
            for(unsigned i=0;i<samplers;++i){bindings[i].texture=i==5?(colourAvailable?r.colour:r.white):i==6?r.depth:r.white;bindings[i].sampler=i==6?r.point:r.linear;}
            SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
            SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));SDL_PushGPUFragmentUniformData(commands,1,cases,sizeof(cases));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
            return read_float_targets(frame,&r.target,1,columns,rows);
        };
        const auto actual=render(false,true,true),old=render(true,true,true),fallback=render(false,false,true);
        // Match production's missing-background binding: a real 1x1 white texture at slot5,
        // while SceneParameters.yz and Viewport still describe the larger render target.
        const auto missingColour=render(false,true,false),missingBoth=render(false,false,false);
        CHECK(actual.size()==columns*rows*4 && old.size()==actual.size() && fallback.size()==actual.size()
            && missingColour.size()==actual.size() && missingBoth.size()==actual.size());
        if(actual.size()!=columns*rows*4 || old.size()!=actual.size() || fallback.size()!=actual.size()
            || missingColour.size()!=actual.size() || missingBoth.size()!=actual.size())return;
        // Independent acceptance/tap masks are the explicit case table. The colour oracle
        // interpolates known pixels using those masks, never calling/copying the depth guard.
        auto sampleColour=[&](float px,float py,unsigned channel){
            const int x=int(std::floor(px-.5f)),y=int(std::floor(py-.5f));
            const float fx=px-.5f-x,fy=py-.5f-y;
            auto at=[&](int xx,int yy){xx=std::max(0,std::min(int(width)-1,xx));yy=std::max(0,std::min(int(height)-1,yy));return pixels[(yy*width+xx)*4+channel];};
            return (at(x,y)*(1-fx)+at(x+1,y)*fx)*(1-fy)+(at(x,y+1)*(1-fx)+at(x+1,y+1)*fx)*fy;
        };
        auto filteredColour=[&](unsigned row,float px,float py,unsigned channel){
            if(!accepted[row])return sampleColour(px,py,channel);
            const int x=int(std::floor(px-.5f)),y=int(std::floor(py-.5f));
            const double fx=double(px)-.5-x,fy=double(py)-.5-y;
            const double weights[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
            double value=0,weight=0;
            for(unsigned tap=0;tap<4;++tap)if((validTaps[row]&(1u<<tap)) && weights[tap]>0) {
                const int xx=std::max(4,std::min(27,x+int(tap&1))),
                    yy=std::max(3,std::min(int(lastY),y+int(tap>>1)));
                value+=weights[tap]*pixels[(yy*width+xx)*4+channel];weight+=weights[tap];
            }
            CHECK(weight>=.25);return float(value/weight);
        };
        for(unsigned row=0;row<rows;++row){
            const float *f=cases[row],*got=&actual[row*columns*4],*missing=&fallback[row*columns*4];
            const float px=accepted[row]?std::max(4.5f,std::min(lastX,f[0]+f[2])):f[0];
            const float py=accepted[row]?std::max(3.5f,std::min(lastY,f[1]+f[3])):f[1];
            CHECK(std::abs(got[0]-px)<2e-6f && std::abs(got[1]-py)<2e-6f && got[2]==float(accepted[row]));
            CHECK(missing[0]==f[0] && missing[1]==f[1] && missing[2]==0);
            for(unsigned c=0;c<3;++c){
                CHECK(std::abs(got[4+c]-filteredColour(row,px,py,c))<3e-6f);
                CHECK(std::abs(missing[4+c]-sampleColour(f[0],f[1],c))<3e-6f);
            }
            const unsigned nx=unsigned(std::floor(px)),ny=unsigned(std::floor(py));
            const float bottom[3]={nx+.5f-16.5f,3.5f+viewportHeight/2-(ny+.5f),-10*depth[ny*width+nx]};
            const float world[3]={f[0]-16.5f,3.5f+viewportHeight/2-f[1],0};
            float distance2=0,fallbackDistance2=0;
            for(unsigned c=0;c<3;++c){
                CHECK(std::abs(got[8+c]-bottom[c])<3e-5f);
                const float optical=world[c]+f[4+c]*f[7],difference=bottom[c]-optical;
                distance2+=difference*difference;
                const float originalFallback=c==2?-20.0f:world[c];
                CHECK(std::abs(missing[8+c]-originalFallback)<3e-5f);
                const float fallbackDifference=originalFallback-optical;fallbackDistance2+=fallbackDifference*fallbackDifference;
            }
            CHECK(std::abs(got[3]-std::sqrt(distance2))<3e-5f);
            CHECK(std::abs(missing[3]-std::sqrt(fallbackDistance2))<3e-5f);++checked;
            for(unsigned noDepth=0;noDepth<2;++noDepth){
                const float *noColour=&(noDepth?missingBoth:missingColour)[row*columns*4];
                CHECK(noColour[0]==f[0] && noColour[1]==f[1] && noColour[2]==0);
                float expectedDistance2=0;
                for(unsigned c=0;c<3;++c){
                    CHECK(noColour[4+c]==0); // unused: caller uses its material scattering fallback
                    const float originalDepth=depth[unsigned(f[1])*width+unsigned(f[0])];
                    const float originalHit=c==2?((noDepth || originalDepth>=.999999f)?-20.0f:-10*originalDepth):world[c];
                    CHECK(std::abs(noColour[8+c]-originalHit)<3e-5f);
                    const float difference=originalHit-(world[c]+f[4+c]*f[7]);
                    expectedDistance2+=difference*difference;
                }
                CHECK(std::abs(noColour[3]-std::sqrt(expectedDistance2))<3e-5f);++missingColourChecks;
            }
        }
        // Old nearest-only sampling leaks unsafe blue. M4d keeps the actual selected ray
        // while dropping that blue. The previous whole-footprint M4c also fails these accepts.
        for(unsigned row:{1u,2u,3u,6u,7u,8u,9u,12u,14u,16u,21u,22u,26u}){
            CHECK(old[row*columns*4+2]==1 && old[row*columns*4+6]>.05f);
            CHECK(actual[row*columns*4+2]==1 && actual[row*columns*4+6]==0);++armedLeaks;
        }
        // Explicit physical-path control: row21's original clear pixel falls back20 units,
        // but the displaced actual nearest is(1,0,-5), distance sqrt26, despite bad neighbours.
        CHECK(std::abs(actual[21*columns*4+3]-std::sqrt(26.0f))<3e-5f);
        CHECK(std::abs(actual[21*columns*4+3]-20.0f)>14.0f);
        // All-valid Rgb mixture retains hardware bilinear values. Differently deep neighbours
        // do not turn transport into averaged depth: nearest red has depth2, not weighted3.9375.
        CHECK(std::abs(actual[23*columns*4+4]-.5625f)<3e-6f);
        CHECK(std::abs(actual[23*columns*4+5]-.25f)<3e-6f);
        CHECK(std::abs(actual[23*columns*4+6]-.375f)<3e-6f);
        CHECK(std::abs(actual[23*columns*4+3]-2.0f)<3e-5f);
        CHECK(std::abs(actual[24*columns*4+4]-15.0f/29)<3e-6f);
        CHECK(std::abs(actual[24*columns*4+5]-5.0f/29)<3e-6f);
        CHECK(std::abs(actual[24*columns*4+6]-9.0f/29)<3e-6f);
        CHECK(old[24*columns*4+5]-actual[24*columns*4+5]>.05f); // armed invalid-white mixture
        CHECK(actual[25*columns*4+2]==0); // no substitute ray when nearest itself is invalid
        CHECK(actual[26*columns*4+2]==1); // exactly1/4 surviving weight is sufficient
        CHECK(actual[4*columns*4+2]==1); // zero-weight bad neighbour must not suppress an exact-centre sample
        CHECK(actual[17*columns*4+2]==1 && actual[17*columns*4+6]>.2f); // valid .081-deep blue object stays visible
        SDL_ReleaseGPUTexture(r.device,r.colour);r.colour=NULL;SDL_ReleaseGPUTexture(r.device,r.depth);r.depth=NULL;
        SDL_ReleaseGPUTexture(r.device,r.white);r.white=NULL;
    }
    CHECK(renders==10 && checked==54 && armedLeaks==26 && missingColourChecks==108);
    printf("sdl_gpu_frame_selfcheck: M4D production depth-aware colour filter: %u renders, %u cases at two sizes, %u armed nearest-only leaks, %u missing-colour checks; independent normalized colours, nearest transport, both surfaces/rays, clear depth, margins, viewport and fallback passed\n",renders,checked,armedLeaks,missingColourChecks);
}
#endif // __linux__

// Verify production refraction helpers against independent ray/plane intersections.
#if defined(__linux__)
static void check_gpu_water_refraction_coordinates(SdlGpuFrame *frame)
{
    const unsigned rows=10,columns=3;
    struct Resources {
        SDL_GPUDevice *device;
        SDL_GPUShader *vertex=NULL,*pixel=NULL;
        SDL_GPUGraphicsPipeline *pipeline=NULL;
        SDL_GPUTexture *target=NULL,*colour=NULL,*depth=NULL,*white=NULL;
        SDL_GPUSampler *linear=NULL,*point=NULL;
        ~Resources() {
            if(pipeline) SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vertex) SDL_ReleaseGPUShader(device,vertex);
            if(pixel) SDL_ReleaseGPUShader(device,pixel);
            if(target) SDL_ReleaseGPUTexture(device,target);
            if(colour) SDL_ReleaseGPUTexture(device,colour);
            if(depth) SDL_ReleaseGPUTexture(device,depth);
            if(white) SDL_ReleaseGPUTexture(device,white);
            if(linear) SDL_ReleaseGPUSampler(device,linear);
            if(point) SDL_ReleaseGPUSampler(device,point);
        }
    } r;
    r.device=frame->Device();
    PixelPipelineDescription pd={};std::string ps;
    const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,ps,
        COMBINER_SHADER_TARGET_SDL3_GPU);
    CHECK(generated);if(!generated)return;
    std::string production;production.swap(ps);
    if(!water_refraction_fixture_source(production,ps))return;
    ps+="\n#define M4aCaseCount 10\n";
    ps+=R"M4A(
// Test-only entry point invokes the exact helpers extracted from the production shader.
// Set M4aCaseCount when constructing this shader; each case occupies four float4s.
cbuffer M4aCases : register(b1, space3) { float4 M4aFixtures[M4aCaseCount * 4]; };

// Deliberately retain the old equations as armed controls, not as expected results.
float2 m4aOldOffset(float depth, float3 view, float3 normal)
{
    float3 incident = -view, underwater = refract(incident, normal, 0.75);
    return clamp(depth * (underwater.xy/min(underwater.z,-0.1)
        - incident.xy/min(incident.z,-0.1)), float2(-8,-8), float2(8,8))
        * smoothstep(0.15,2.5,depth);
}
WaterRefractionSample m4aOldSample(float2 origin, float2 offset,
    float3 world, float3 view, float3 originalBottom)
{
    WaterRefractionSample result;
    float2 candidate = origin + offset;
    float3 hit = bottomPosition(candidate,world,0);
    float valid = step(hit.z,world.z-0.08) * step(0.0,dot(hit-world,-view));
    result.pixel = lerp(origin,candidate,valid);
    result.bottom = lerp(originalBottom,hit,valid);
    result.accepted = valid;
    result.colour = Texture5.SampleLevel(Sampler5,saturate(result.pixel*SceneParameters.yz),0).rgb;
    return result;
}
float4 main(float4 position : SV_Position) : SV_Target0
{
    uint row = (uint)position.y, column = (uint)position.x;
    float4 a = M4aFixtures[row*4], b = M4aFixtures[row*4+1];
    float4 c = M4aFixtures[row*4+2], flags = M4aFixtures[row*4+3];
    if (flags.x < 0.5) {
        // Supplied constant ray, avoiding an accidental claim to fix production ortho cameras.
        // b.xyz=view, b.w=original vertical depth, c.xyz=normal, c.w=analytic height delta.
        float shift = WaterOpticalRayShift(c.w,b.z,1,7,b.w/b.z,100);
        float opticalDepth = max(b.w+b.z*shift,0.0);
        float2 offset = flags.y > 0.5 ? m4aOldOffset(opticalDepth,b.xyz,c.xyz)
            : waterRefractionPixels(opticalDepth,b.xyz,c.xyz,float2(1,0),float2(0,1));
        return float4(offset,shift,opticalDepth);
    }
    // Sampling cases use WorldFromClip below: the raster surface is z=0 and actual depth
    // maps to negative world z. a.xy=original pixel, a.zw=prescribed fractional displacement.
    float3 world = scenePosition(a.xy,0), view = float3(0,0,1);
    float3 originalBottom = bottomPosition(a.xy,world,0);
    WaterRefractionSample sampledScene;
    if (flags.y > 0.5) sampledScene = m4aOldSample(a.xy,a.zw,world,view,originalBottom);
    else sampledScene = waterRefractedScene(a.xy,a.zw,world,world,view,0,originalBottom);
    if (column == 0) return float4(sampledScene.pixel,sampledScene.accepted,length(sampledScene.bottom-world));
    if (column == 1) return float4(sampledScene.colour,1);
    return float4(sampledScene.bottom,1);
}

)M4A";
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position { return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); }";
    std::string error;std::vector<unsigned char> spirv;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
    r.vertex=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vertex);if(!r.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
    if(!compiled){printf("M4A production helper compile: %s\n",error.c_str());return;}
    unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);
    CHECK(samplers<=16 && uniforms==2);if(samplers>16 || uniforms!=2)return;
    r.pixel=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.pixel);if(!r.pixel)return;
    SDL_GPUColorTargetDescription colourDescription={};colourDescription.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vertex;pi.fragment_shader=r.pixel;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions=&colourDescription;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colourDescription.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    SDL_GPUSamplerCreateInfo si={};si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    r.linear=SDL_CreateGPUSampler(r.device,&si);si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;
    r.point=SDL_CreateGPUSampler(r.device,&si);CHECK(r.linear && r.point);if(!r.linear || !r.point)return;
    const float templateCases[rows][16]={
        {0,0,0,0, .6f,0,.8f,4, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, -.6f,0,.8f,4, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, 0,0,1,4, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, .6f,0,.8f,4, 0,0,1,.8f, 0,0,0,0},
        {0,0,0,0, .6f,0,.8f,4, 0,0,1,-.8f, 0,0,0,0},
        {0,0,0,0, 0,0,1,4, .6f,0,.8f,0, 0,0,0,0},
        {0,0,0,0, .6f,0,.8f,.1f, 0,0,1,0, 0,0,0,0},
        {0,0,0,0, .6f,0,.8f,100, 0,0,1,0, 0,0,0,0},
        {7.5f,7.5f,.75f,.875f, 0,0,1,0, 0,0,1,0, 1,0,0,0},
        {15.5f,7.5f,8,0, 0,0,1,0, 0,0,1,0, 1,0,0,0}
    };
    unsigned renders=0,armedSign=0,armedViewport=0;
    CHECK(frame->Flush());
    for(unsigned resize=0;resize<2;++resize) {
        const unsigned width=24u<<resize,height=20u<<resize;
        ti.format=colourDescription.format;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;ti.width=width;ti.height=height;
        r.colour=SDL_CreateGPUTexture(r.device,&ti);ti.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        r.depth=SDL_CreateGPUTexture(r.device,&ti);ti.format=colourDescription.format;ti.width=ti.height=1;
        r.white=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.colour && r.depth && r.white);
        if(!r.colour || !r.depth || !r.white)return;
        std::vector<float> pixels(width*height*4),depth(width*height,.5f);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const unsigned p=(y*width+x)*4;const bool outside=x<4 || x>15 || y<3 || y>12;
            pixels[p]=outside?1.0f:float(x)/width;pixels[p+1]=outside?0.0f:float(y)/height;
            pixels[p+2]=outside?1.0f:0.0f;pixels[p+3]=1;
        }
        const unsigned colourBytes=unsigned(pixels.size()*sizeof(float)),depthBytes=unsigned(depth.size()*sizeof(float));
        SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;bi.size=colourBytes+depthBytes+16;
        SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(r.device,&bi);CHECK(transfer);if(!transfer)return;
        unsigned char *mapped=(unsigned char*)SDL_MapGPUTransferBuffer(r.device,transfer,false);CHECK(mapped);
        if(!mapped){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        memcpy(mapped,pixels.data(),colourBytes);memcpy(mapped+colourBytes,depth.data(),depthBytes);
        const float white[4]={1,1,1,1};memcpy(mapped+colourBytes+depthBytes,white,sizeof(white));
        SDL_UnmapGPUTransferBuffer(r.device,transfer);
        SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(r.device);CHECK(upload);
        if(!upload){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);
        if(!copy){SDL_CancelGPUCommandBuffer(upload);SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
        SDL_GPUTexture *textures[3]={r.colour,r.depth,r.white};const unsigned offsets[3]={0,colourBytes,colourBytes+depthBytes};
        for(unsigned i=0;i<3;++i) {
            SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.offset=offsets[i];
            from.pixels_per_row=i==2?1:width;from.rows_per_layer=i==2?1:height;
            SDL_GPUTextureRegion to={};to.texture=textures[i];to.w=i==2?1:width;to.h=i==2?1:height;to.d=1;
            SDL_UploadToGPUTexture(copy,&from,&to,false);
        }
        SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(r.device,transfer);
        SdlWaterPixelConstants water={};water.WorldFromClip[0]=6;water.WorldFromClip[5]=5;
        water.WorldFromClip[10]=-10;water.WorldFromClip[15]=1;
        water.Viewport[0]=4.5f;water.Viewport[1]=3.5f;water.Viewport[2]=12;water.Viewport[3]=10;
        water.SceneParameters[0]=water.SceneParameters[3]=1;
        water.SceneParameters[1]=1.0f/width;water.SceneParameters[2]=1.0f/height;water.TerrainParameters[1]=1;
        auto render=[&](bool old) {
            float cases[rows][16];memcpy(cases,templateCases,sizeof(cases));
            for(unsigned row=0;row<rows;++row)cases[row][13]=old?1.0f:0.0f;
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);
            if(!commands)return std::vector<float>();
            SDL_GPUColorTargetInfo attachment={};attachment.texture=r.target;
            attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
            if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>();}
            SDL_BindGPUGraphicsPipeline(pass,r.pipeline);
            SDL_GPUTextureSamplerBinding bindings[16]={};
            for(unsigned i=0;i<samplers;++i){bindings[i].texture=i==5?r.colour:i==6?r.depth:r.white;bindings[i].sampler=i==6?r.point:r.linear;}
            SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
            SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
            SDL_PushGPUFragmentUniformData(commands,1,cases,sizeof(cases));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
            return read_float_targets(frame,&r.target,1,columns,rows);
        };
        const auto actual=render(false),old=render(true);
        CHECK(actual.size()==columns*rows*4 && old.size()==actual.size());
        if(actual.size()!=columns*rows*4 || old.size()!=actual.size())return;
        for(unsigned row=0;row<6;++row) {
            const float *f=templateCases[row],*got=&actual[row*columns*4];
            const double vx=f[4],vy=f[5],vz=f[6],D=f[7],nx=f[8],ny=f[9],nz=f[10];
            const double shift=f[11]/vz,ox=vx*shift,oy=vy*shift,oz=vz*shift;
            const double ix=-vx,iy=-vy,iz=-vz,dot=ix*nx+iy*ny+iz*nz;
            const double eta=.75,k=eta*dot+std::sqrt(1-eta*eta*(1-dot*dot));
            const double ux=eta*ix-k*nx,uy=eta*iy-k*ny,uz=eta*iz-k*nz;
            // Independent ray/plane intersections, not the implementation's ratio subtraction.
            const double straightT=(-D-oz)/iz,bentT=(-D-oz)/uz;
            const double straightX=ox+ix*straightT,straightY=oy+iy*straightT;
            const double bentX=ox+ux*bentT,bentY=oy+uy*bentT;
            CHECK(std::abs(straightX+vx*D/vz)<2e-6 && std::abs(straightY+vy*D/vz)<2e-6);
            CHECK(std::abs(got[0]-(bentX-straightX))<3e-5 && std::abs(got[1]-(bentY-straightY))<3e-5);
            CHECK(std::abs(got[2]-shift)<3e-5 && std::abs(got[3]-(D+oz))<3e-5);
            if(row==0 || row==3 || row==4){CHECK(std::abs(old[row*columns*4]-(bentX-straightX))>.1);++armedSign;}
        }
        CHECK(actual[6*columns*4]==0 && actual[6*columns*4+1]==0);
        CHECK(std::abs(actual[7*columns*4]-8)<1e-6 && actual[7*columns*4+1]==0);
        const float *fraction=&actual[8*columns*4],*fractionColour=fraction+4,*fractionBottom=fraction+8;
        CHECK(std::abs(fraction[0]-8.25f)<1e-6 && std::abs(fraction[1]-8.375f)<1e-6 && fraction[2]==1);
        CHECK(std::abs(fractionColour[0]-7.75f/width)<2e-6 && std::abs(fractionColour[1]-7.875f/height)<2e-6 && fractionColour[2]==0);
        // Nearest point depth unprojects (8.5,8.5), whereas fractional colour stays bilinear.
        CHECK(std::abs(fractionBottom[0]+2)<2e-6 && std::abs(fractionBottom[1])<2e-6 && std::abs(fractionBottom[2]+5)<2e-6);
        CHECK(std::abs(fraction[3]-std::sqrt(27.0))<3e-5);
        const float *edge=&actual[9*columns*4],*edgeColour=edge+4,*edgeBottom=edge+8;
        CHECK(edge[0]==15.5f && edge[1]==7.5f && edge[2]==1);
        CHECK(std::abs(edgeColour[0]-15.0f/width)<2e-6 && std::abs(edgeColour[1]-7.0f/height)<2e-6 && edgeColour[2]==0);
        CHECK(std::abs(edgeBottom[0]-5)<2e-6 && std::abs(edgeBottom[1]-1)<2e-6 && std::abs(edgeBottom[2]+5)<2e-6);
        CHECK(old[9*columns*4+4+2]>.99f);++armedViewport;
        SDL_ReleaseGPUTexture(r.device,r.colour);r.colour=NULL;SDL_ReleaseGPUTexture(r.device,r.depth);r.depth=NULL;
        SDL_ReleaseGPUTexture(r.device,r.white);r.white=NULL;
    }
    CHECK(armedSign==6 && armedViewport==2);
    printf("sdl_gpu_frame_selfcheck: M4A production refraction helpers: %u renders; sign, zero, tilt, optical shift, clamp, fractional centres, subviewport and resize passed; %u sign/%u viewport old controls\n",renders,armedSign,armedViewport);
}
#endif // __linux__

// Authored material invariants are checked on the GPU with the production helpers. This
// deliberately avoids a second C++ implementation of their colour conversion or extinction.
static void check_gpu_modern_water_material(SdlGpuFrame *frame)
{
    const unsigned rows=12,columns=3;
    // display colour/brightness, tint/pollution/bubbles/reserved, path/depth/illumination/face.
    const float fixtures[rows][12]={
        {0,0,0,0, 0,0,0,0, 20,10,1,.6f},
        {1,1,1,1, 1,0,0,0, 20,10,1,.6f},
        {.5f,.5f,.5f,1, 1,0,0,0, 20,10,1,.6f},
        {.7f,0,0,1, 1,0,0,0, 20,10,1,.6f},
        {0,0,.7f,1, 1,0,0,0, 20,10,1,.6f},
        {.7f,0,0,1, 1,.8f,1,0, 20,10,1,.6f},
        {0,0,.7f,1, 1,.8f,1,0, 20,10,1,.6f},
        {45/255.f,58/255.f,11/255.f,.67f, 1,.8f,0,0, 20,10,1,.6f},
        {45/255.f,58/255.f,11/255.f,.67f, 1,0,1,0, 20,10,1,.6f},
        {45/255.f,58/255.f,11/255.f,.67f, 1,.8f,1,0, 20,0,1,.6f},
        {45/255.f,58/255.f,11/255.f,.67f, 1,.8f,1,0, 20,10,1,.6f},
        {45/255.f,58/255.f,11/255.f,1, 1,.8f,1,0, 20,10,1,.6f}
    };
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;
        SDL_GPUGraphicsPipeline *pipeline=NULL;SDL_GPUTexture *target=NULL;
        ~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vertex)SDL_ReleaseGPUShader(device,vertex);if(pixel)SDL_ReleaseGPUShader(device,pixel);
            if(target)SDL_ReleaseGPUTexture(device,target);}
    } r;r.device=frame->Device();
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::string ps="cbuffer Fixtures:register(b0,space3){float4 F[36];};\n";
    ps+=ModernWaterMaterial::OpticalSource();
    ps+=R"HLSL(
float4 main(float4 position:SV_Position):SV_Target0 {
    uint row=(uint)position.y,column=(uint)position.x;
    float4 colour=F[row*3],params=F[row*3+1],context=F[row*3+2];
    if(column==0)return float4(modernWaterMaterialSource(float3(.002,.035,.075),colour,params.x,context.z,context.w),1);
    if(column==1)return float4(modernWaterMaterialTransmission(float3(.8,.8,.8),colour.rgb,params.y,context.x,.5),1);
    return float4(modernWaterBubbleFilm(colour.rgb),modernWaterBubbleWeight(params.y,params.z,context.y));
})HLSL";
    std::string error;std::vector<unsigned char> spirv;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
    r.vertex=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vertex);if(!r.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
    if(!compiled){printf("Modern water material compile: %s\n",error.c_str());return;}
    r.pixel=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.pixel);if(!r.pixel)return;
    SDL_GPUColorTargetDescription cd={};cd.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vertex;pi.fragment_shader=r.pixel;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions=&cd;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=cd.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    CHECK(frame->Flush());
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
    SDL_GPUColorTargetInfo attachment={};attachment.texture=r.target;attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
    if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
    SDL_BindGPUGraphicsPipeline(pass,r.pipeline);SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
    const auto actual=read_float_targets(frame,&r.target,1,columns,rows);
    CHECK(actual.size()==rows*columns*4);if(actual.size()!=rows*columns*4)return;
    auto value=[&](unsigned row,unsigned column,unsigned channel){return actual[(row*columns+column)*4+channel];};
    for(float value:actual)CHECK(std::isfinite(value) && value>=0 && value<=1);
    const float baseline[]={.002f,.035f,.075f};
    for(unsigned c=0;c<3;++c) {
        CHECK(value(0,0,c)==baseline[c] && value(1,0,c)==baseline[c]);
        CHECK(std::abs(value(2,0,c)/baseline[c]-std::pow(.5f,2.2f))<.000001);
        CHECK(value(0,1,c)==.8f && value(3,1,c)==.8f && value(4,1,c)==.8f);
        CHECK(value(3,0,c)==value(5,0,c) && value(4,0,c)==value(6,0,c)); // tint independent of pollution
        CHECK(value(5,1,c)<value(3,1,c) && value(6,1,c)<value(4,1,c));
        CHECK(value(7,0,c)==value(8,0,c) && value(8,0,c)==value(10,0,c));
        CHECK(std::abs(value(10,0,c)-.67f*value(11,0,c))<.000001);
    }
    CHECK(value(3,0,0)>0 && value(3,0,1)==0 && value(3,0,2)==0); // red does not inherit a blue cast
    CHECK(value(4,0,2)>0 && value(4,0,0)==0 && value(4,0,1)==0);
    CHECK(value(5,1,0)>value(5,1,1) && value(5,1,1)==value(5,1,2));
    CHECK(value(6,1,2)>value(6,1,0) && value(6,1,0)==value(6,1,1));
    CHECK(value(5,2,0)>value(5,2,1) && value(6,2,2)>value(6,2,1));
    CHECK(value(7,2,3)==0 && value(8,2,3)==0 && value(9,2,3)==0 && value(10,2,3)==.8f);
    printf("sdl_gpu_frame_selfcheck: Modern material: exact clean/white identity, gray attenuation, red/blue tint and absorption, independent pollution, brightness and bubble gates passed\n");
}

// Executes the exact shared functions called by generated production water; not a whole-frame
// FFT toggle test. Fixed context/path/face inputs intentionally isolate material independence.
static void check_gpu_water_material_transport(SdlGpuFrame *frame)
{
    const unsigned rows=15,columns=4;
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;
        SDL_GPUGraphicsPipeline *pipeline=NULL;SDL_GPUTexture *target=NULL;
        ~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vertex)SDL_ReleaseGPUShader(device,vertex);if(pixel)SDL_ReleaseGPUShader(device,pixel);
            if(target)SDL_ReleaseGPUTexture(device,target);}
    } r;r.device=frame->Device();
    // a=(path,river,effective-offshore,face), b=(scene.rgb,daylight),
    // c=(caustic,sceneAvailable,FFT weight,old control), d=(reference-depth,opacity,0,0).
    const float base[rows][16]={
        {0,0,1,0, .2f,.4f,.6f,1, 1,1,0,0, 3,.75f,0,0},
        {0,0,1,0, .2f,.4f,.6f,1, 1,1,1,0, 3,.75f,0,0},
        {0,0,1,1, .2f,.4f,.6f,1, 1,1,1,0, 3,.75f,0,0},
        {100000,0,1,.3f, .2f,.4f,.6f,1, 1,1,0,0, 3,.75f,0,0},
        {100000,0,1,.3f, .8f,.1f,.2f,1, 1,1,1,0, 3,.75f,0,0},
        {4,0,.5f,.4f, .2f,.4f,.6f,.7f, 1,1,.5f,0, 3,.75f,0,0},
        {4,1,0,.4f, .2f,.4f,.6f,.7f, 1,1,0,0, 3,.75f,0,0},
        {4,0,0,.4f, .2f,.4f,.6f,.7f, 1,1,0,0, 3,.75f,0,0},
        {2,0,1,0, .7f,.5f,.3f,1, 1.2f,1,1,0, 3,.75f,0,0},
        {2,0,1,0, .1f,.2f,.1f,1, 1.2f,1,1,0, 3,.75f,0,0},
        {2,0,1,1, .7f,.5f,.3f,1, 1.2f,1,1,0, 3,.75f,0,0},
        {2,0,1,1, .1f,.2f,.1f,1, 1.2f,1,1,0, 3,.75f,0,0},
        {2,0,1,.4f, .7f,.5f,.3f,.7f, 1.2f,0,1,0, 3,.75f,0,0},
        {4,0,.6f,.4f, .2f,.4f,.6f,1, 1,1,0,0, 3,.75f,0,0},
        {4,.4f,.6f,.4f, .2f,.4f,.6f,1, 1,1,1,0, 3,.75f,0,0}
    };
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::string ps="cbuffer Fixtures:register(b0,space3){float4 F[60];};\n";
    ps+=WaterOpticalModel::GetHlslSource();
    ps+=R"HLSL(
float4 main(float4 position:SV_Position):SV_Target0 {
    uint row=(uint)position.y,column=(uint)position.x;
    float4 a=F[row*4],b=F[row*4+1],c=F[row*4+2],d=F[row*4+3];
    float3 T=WaterOpticalTransmission(a.x,a.y,a.z,d.x,d.y);
    float3 S=WaterOpticalScatteringSource(a.y,a.z,b.w,a.w);
    float3 body=WaterOpticalBody(b.xyz,T,S,c.x,c.y);
    if(c.w>0.5) {
        // Armed pre-fix colour/whole-body-lighting control, not the expected-value oracle.
        float3 oldSource=lerp(float3(.002,.028,.041),float3(.0012,.015,.029),a.z)*(.08+.92*b.w);
        oldSource=lerp(oldSource,float3(.002,.035,.075)*(.08+.92*b.w),c.z);
        float face=lerp(1.0,.4+1.8*a.w,a.z);
        S=oldSource*face;
        body=lerp(oldSource,b.xyz*T*c.x+oldSource*(1-T),c.y)*face;
    }
    if(column==0)return float4(T,1);
    if(column==1)return float4(S,1);
    if(column==2)return float4(body,1);
    return float4(WaterOpticalMaterialSource(a.y,a.z),1);
})HLSL";
    std::string error;std::vector<unsigned char> spirv;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
    r.vertex=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vertex);if(!r.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
    if(!compiled){printf("M4B optical source compile: %s\n",error.c_str());return;}
    r.pixel=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.pixel);if(!r.pixel)return;
    SDL_GPUColorTargetDescription cd={};cd.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vertex;pi.fragment_shader=r.pixel;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions=&cd;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=cd.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    CHECK(frame->Flush());
    auto render=[&](bool old){
        float fixtures[rows][16];memcpy(fixtures,base,sizeof(fixtures));
        for(unsigned row=0;row<rows;++row)fixtures[row][11]=old?1.0f:0.0f;
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return std::vector<float>();
        SDL_GPUColorTargetInfo attachment={};attachment.texture=r.target;attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
        if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>();}
        SDL_BindGPUGraphicsPipeline(pass,r.pipeline);SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
        return read_float_targets(frame,&r.target,1,columns,rows);
    };
    const auto actual=render(false),old=render(true);CHECK(actual.size()==rows*columns*4 && old.size()==actual.size());
    if(actual.size()!=rows*columns*4 || old.size()!=actual.size())return;
    auto value=[&](const std::vector<float>& data,unsigned row,unsigned column,unsigned channel){return data[(row*columns+column)*4+channel];};
    float maximumError=0;
    for(unsigned row=0;row<rows;++row){
        using namespace WaterOpticalModel;const float *f=base[row];
        const Rgb T=Transmission(f[0],f[1],f[2],f[12],f[13]),S=ScatteringSource(f[1],f[2],f[7],f[3]);
        const Rgb scene={f[4],f[5],f[6]},B=Body(scene,T,S,f[8],f[9]),M=MaterialSource(f[1],f[2]);
        const Rgb expected[4]={T,S,B,M};
        for(unsigned col=0;col<columns;++col)for(unsigned c=0;c<3;++c){
            const float e=c==0?expected[col].r:c==1?expected[col].g:expected[col].b;
            const float got=value(actual,row,col,c),difference=std::abs(got-e);
            maximumError=std::max(maximumError,difference);CHECK(std::isfinite(got) && difference<2e-5f);
        }
    }
    for(unsigned c=0;c<3;++c){
        // Independent identities, not just CPU/HLSL agreement.
        for(unsigned row=0;row<3;++row)CHECK(std::abs(value(actual,row,2,c)-base[row][4+c])<1e-6f);
        CHECK(std::abs(value(actual,0,1,c)-value(actual,1,1,c))<1e-7f); // FFT metadata cannot retint fixed material
        CHECK(std::abs(value(actual,3,2,c)-value(actual,3,1,c))<1e-7f);
        CHECK(std::abs(value(actual,3,2,c)-value(actual,4,2,c))<1e-7f); // deep source independent of scene and FFT
        CHECK(std::abs(value(actual,12,2,c)-value(actual,12,1,c))<1e-7f); // no-scene source fallback
        const float contrast0=value(actual,8,2,c)-value(actual,9,2,c),contrast1=value(actual,10,2,c)-value(actual,11,2,c);
        CHECK(std::abs(contrast0-contrast1)<2e-6f);
        CHECK(std::abs(contrast0-(base[8][4+c]-base[9][4+c])*value(actual,8,0,c)*base[8][8])<2e-6f);
        CHECK(std::abs(value(actual,13,3,c)-value(actual,14,3,c))<1e-7f); // no second river gate
    }
    CHECK(std::abs(value(old,0,2,0)-base[0][4])>.1f); // old whole-body factor violates zero-path identity
    CHECK(std::abs(value(old,0,1,2)-value(old,1,1,2))>.01f); // old FFT colour selection is armed
    const float oldContrast0=value(old,8,2,0)-value(old,9,2,0),oldContrast1=value(old,10,2,0)-value(old,11,2,0);
    CHECK(std::abs(oldContrast0-oldContrast1)>.1f);
    printf("sdl_gpu_frame_selfcheck: M4B shared material transport: %u fixtures, max CPU/GPU error %.8f; zero/deep/fallback/contrast/context/FFT-independence and old controls passed\n",rows,maximumError);
}

static std::vector<float> read_ocean(SdlGpuFrame *frame,unsigned mip=0)
{
	SDL_GPUTexture *textures[6];for(unsigned i=0;i<6;++i) textures[i]=frame->Ocean_Map(i);
	return read_float_targets(frame,textures,6,frame->Ocean_Configuration().resolution>>mip,frame->Ocean_Configuration().resolution>>mip,mip);
}

using OceanMipReference=std::array<std::vector<double>,WaterFftSpectrum::Phases+1>;
static OceanMipReference ocean_mip_reference(const std::vector<float> &base)
{
	// Independent direct box integrals of actual GPU base texels, not repeated averages of the
	// runtime's previous mip. Every channel remains signed float, including all derivatives.
	using namespace WaterFftSpectrum;OceanMipReference result;
	for(unsigned level=0;level<=Phases;++level) {
		const unsigned side=Resolution>>level,block=1u<<level;
		result[level].resize(6*side*side*4);
		for(unsigned map=0;map<6;++map) for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) for(unsigned c=0;c<4;++c) {
			double sum=0;
			for(unsigned by=0;by<block;++by) for(unsigned bx=0;bx<block;++bx)
				sum+=base[((map*Resolution+y*block+by)*Resolution+x*block+bx)*4+c];
			result[level][((map*side+y)*side+x)*4+c]=sum/double(block*block);
		}
	}
	return result;
}
static double ocean_mip_sample(const OceanMipReference &mips,unsigned map,unsigned channel,double u,double v,double lod)
{
	using namespace WaterFftSpectrum;lod=(std::max)(0.0,(std::min)(double(Phases),lod));
	const unsigned low=unsigned(lod),high=(std::min)(Phases,low+1);const double blend=lod-low;
	auto bilinear=[&](unsigned level) {
		const int side=int(Resolution>>level);const double x=u*side-.5,y=v*side-.5;
		const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
		auto at=[&](int px,int py) {px=(px%side+side)%side;py=(py%side+side)%side;
			return mips[level][((map*side+unsigned(py))*side+unsigned(px))*4+channel];};
		return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
	};
	return bilinear(low)*(1-blend)+bilinear(high)*blend;
}
static std::array<std::vector<float>,WaterFftSpectrum::Phases+1> check_ocean_mip_contents(
	SdlGpuFrame *frame,const OceanMipReference &expected,double &maximumError)
{
	std::array<std::vector<float>,WaterFftSpectrum::Phases+1> result;
	for(unsigned level=0;level<result.size();++level) {
		result[level]=read_ocean(frame,level);bool valid=result[level].size()==expected[level].size();
		for(size_t i=0;i<result[level].size();++i) {
			const double error=std::abs(double(result[level][i])-expected[level][i]);maximumError=(std::max)(maximumError,error);
			valid &= std::isfinite(result[level][i]) && error<1e-6+std::abs(expected[level][i])*3e-6;
		}
		CHECK(valid);
	}
	return result;
}
static void check_ocean_fractional_mips(SdlGpuFrame *frame,const OceanMipReference &expected)
{
	const unsigned width=36,height=6;
	std::string source;
	for(unsigned i=0;i<6;++i) source+="Texture2D<float4> T"+std::to_string(i)+":register(t"+std::to_string(i)+",space2);SamplerState S"+std::to_string(i)+":register(s"+std::to_string(i)+",space2);\n";
	source+="float4 main(float4 position:SV_Position):SV_Target0 { int2 pixel=int2(position.xy);float lod=pixel.x*0.25;float2 uv=float2((pixel.x%3)-1,pixel.y%3)*2.0;\n";
	for(unsigned i=0;i<6;++i) source+="if(pixel.y=="+std::to_string(i)+") return T"+std::to_string(i)+".SampleLevel(S"+std::to_string(i)+",uv,lod);\n";
	source+="return 0;}\n";
	std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(WaterFftSpectrum::VertexHlsl(),true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);CHECK(vertex);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=vertex;info.fragment_shader=pixel;
	info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
	info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	SDL_GPUGraphicsPipeline *pipeline=vertex&&pixel?SDL_CreateGPUGraphicsPipeline(frame->Device(),&info):NULL;CHECK(pipeline);
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=colour.format;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;texture.width=width;texture.height=height;
	texture.layer_count_or_depth=texture.num_levels=1;SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture);CHECK(target);
	if(pipeline && target) {
		frame->Set_Target({target,frame->Depth_For(width,height),width,height});SdlRecordedDraw draw={};draw.Pipeline=pipeline;draw.Count=3;
		memset(frame->Stage(16,draw.VertexOffset),0,16);draw.SamplerCount=6;
		for(unsigned i=0;i<6;++i) {draw.Textures[i]=frame->Ocean_Map(i);draw.Samplers[i]=frame->Ocean_Sampler();}
		draw.Viewport[2]=draw.Scissor[2]=width;draw.Viewport[3]=draw.Scissor[3]=height;draw.Viewport[5]=1;frame->Record_Draw(draw);
		const auto sampled=read_float_targets(frame,&target,1,width,height);double maximumError=0,armedNearest=0,armedBase=0;
		for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) for(unsigned c=0;c<4;++c) {
			const double lod=x*.25,u=2*(int(x%3)-1),v=2*(y%3);
			const double value=ocean_mip_sample(expected,y,c,u,v,lod),actual=sampled[(y*width+x)*4+c];
			maximumError=(std::max)(maximumError,std::abs(actual-value));
            CHECK(std::abs(actual-value)<1e-5);
			armedNearest=(std::max)(armedNearest,std::abs(value-ocean_mip_sample(expected,y,c,u,v,std::floor(lod+.5))));
			armedBase=(std::max)(armedBase,std::abs(value-ocean_mip_sample(expected,y,c,u,v,0)));
		}
		CHECK(armedNearest>1e-4 && armedBase>1e-3);
		printf("sdl_gpu_frame_selfcheck: all6 FFT maps fractional/negative-wrapped mip sampling: max error %.9f, armed nearest %.6f and base-only %.6f\n",maximumError,armedNearest,armedBase);
	}
	CHECK(frame->Flush());frame->Set_Target(frame->Back_Buffer_Target());
	if(target) SDL_ReleaseGPUTexture(frame->Device(),target);if(pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	if(vertex) SDL_ReleaseGPUShader(frame->Device(),vertex);if(pixel) SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_vertex_uniform_order(SdlGpuFrame *frame)
{
	// Use the real recorded draw path, with the complete water block shared between VS b1 and
	// PS b0. Reading its last hull and FFT tail catches a truncated copy that a count-only test
	// misses. All draws remain queued while the source variables are changed again.
	const unsigned width=4,rows=6;
	const unsigned timeRegister=offsetof(SdlWaterPixelConstants,WaterParameters)/16;
	const unsigned lastHullRegister=offsetof(SdlWaterPixelConstants,HullData)/16+79;
	const unsigned fftRegister=offsetof(SdlWaterPixelConstants,FftParameters)/16;
	const unsigned historyAreaRegister=offsetof(SdlWaterPixelConstants,FoamHistoryArea)/16;
	const unsigned historyRegister=offsetof(SdlWaterPixelConstants,FoamHistoryParameters)/16;
	const unsigned contactRegister=offsetof(SdlWaterPixelConstants,ContactHullData)/16;
	const unsigned lastContactRegister=contactRegister+79;
	const unsigned fillRegister=offsetof(SdlWaterPixelConstants,FoamFillLights)/16;
	CHECK(contactRegister==100 && fillRegister==180 && sizeof(SdlWaterPixelConstants)==190*16);
	std::string block="cbuffer WaterBlock : register(b1,space1) { float4 Water[";
	block+=std::to_string(sizeof(SdlWaterPixelConstants)/16)+"]; };\n";
	const std::string output="struct Output { float4 Position:SV_Position; [[vk::location(0)]] float4 Value:TEXCOORD0; };\n";
	const std::string position="Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);";
	std::string waterVertex="cbuffer FixedBlock : register(b0,space1) { float4 Fixed; };\n"+block+output;
	waterVertex+="Output main(uint id:SV_VertexID) {"+position+
		"o.Value=float4(Fixed.x,Water["+std::to_string(timeRegister)+"].x,Water["+
		std::to_string(lastHullRegister)+"].w,Water["+std::to_string(fftRegister)+"].x);"
		"o.Value.x+=Water["+std::to_string(historyAreaRegister)+"].x;"
		"o.Value.z+=Water["+std::to_string(historyRegister)+"].w;"
		"o.Value.x+=Water["+std::to_string(contactRegister)+"].w;"
		"o.Value.y+=Water["+std::to_string(lastContactRegister)+"].w;"
		"o.Value.x+=Water["+std::to_string(fillRegister)+"].w+Water["+std::to_string(fillRegister+1)+"].x;"
		"return o;}\n";
	std::string waterPixel="cbuffer WaterBlock : register(b0,space3) { float4 Water["+
		std::to_string(sizeof(SdlWaterPixelConstants)/16)+"]; };\n";
	waterPixel+="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {\n"
		"value.w+=abs(value.y-Water["+std::to_string(timeRegister)+"].x-Water["+
		std::to_string(lastContactRegister)+"].w)*100.0;"
		"value.x-=Water["+std::to_string(historyAreaRegister)+"].x;"
		"value.z+=Water["+std::to_string(historyAreaRegister)+"].w;"
		"value.w+=Water["+std::to_string(historyRegister)+"].w;"
		"value.z+=Water["+std::to_string(lastContactRegister)+"].z;"
		"value.y+=Water["+std::to_string(fillRegister+3)+"].z;"
		"value.z+=Water["+std::to_string(fillRegister+2)+"].z;"
		"return value;}\n";
	const std::string plainVertex="cbuffer FixedBlock : register(b0,space1) { float4 Fixed; };\n"+output+
		"Output main(uint id:SV_VertexID) {"+position+"o.Value=float4(9,8,7,6)*Fixed.x;return o;}\n";
	const char *plainPixel="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {return value;}\n";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs[2]={},*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};SDL_GPUTexture *target=NULL;
		~Resources() {
			if(target) SDL_ReleaseGPUTexture(device,target);
			for(unsigned i=0;i<2;++i) {
				if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vs[i]) SDL_ReleaseGPUShader(device,vs[i]);if(ps[i]) SDL_ReleaseGPUShader(device,ps[i]);
			}
		}
	} resources;
	resources.device=frame->Device();
	for(unsigned water=0;water<2;++water) {
		std::vector<unsigned char> spirv;std::string error;
		bool compiled=SDL3_Compile_HLSL_To_SPIRV(water?waterVertex:plainVertex,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water uniform VS: %s\n",error.c_str());return;}
		unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);
		CHECK(samplers==0 && buffers==(water?2u:1u));
		resources.vs[water]=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs[water]);
		compiled=SDL3_Compile_HLSL_To_SPIRV(water?waterPixel:std::string(plainPixel),false,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water uniform PS: %s\n",error.c_str());return;}
		resources.ps[water]=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps[water]);
		if(!resources.vs[water] || !resources.ps[water]) return;
		SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
		SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs[water];info.fragment_shader=resources.ps[water];
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
		resources.pipelines[water]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipelines[water]);
		if(!resources.pipelines[water]) return;
	}
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=width;texture.height=rows;texture.layer_count_or_depth=texture.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(resources.target);if(!resources.target) return;
	CHECK(frame->Flush());frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
	const bool waterRows[rows]={true,false,true,true,false,true};
	const unsigned state[rows]={0,0,1,0,0,1};
	const float expectedWater[2][4]={{.25f,2.5f,79.25f,1.0f},{.75f,9.125f,-3.75f,0.0f}};
	const float historyTail[2][3]={{-128,.25f,24.25f},{192,.125f,26.0f}};
	const float contactTail[2][3]={{12,-175.25f,91.5f},{0,0,0}};
	const float fillTail[2][4]={{1,2.25f,-.875f,5.125f},{0,0,0,0}};
	for(unsigned row=0;row<rows;++row) {
		if(row==3) {
			// Engine rectangle-clear pipelines target its native UNORM scene format.
			frame->Set_Target(frame->Back_Buffer_Target());
			frame->Clear_Rect(0,0,1,1,true,false,false,0xFF123456u,1,0);
			frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
		}
		SdlRecordedDraw draw={};draw.Pipeline=resources.pipelines[waterRows[row]?1:0];draw.Count=3;
		memset(frame->Stage(16,draw.VertexOffset),0,16);
		float fixed[4]={waterRows[row]?expectedWater[state[row]][0]:2.0f,0,0,0};
		draw.VertexConstants=frame->Constants(0,fixed,sizeof(fixed));draw.VertexConstantsSize=sizeof(fixed);
		if(waterRows[row]) {
			SdlWaterPixelConstants water={};water.WaterParameters[0]=expectedWater[state[row]][1];
			water.HullData[79][3]=expectedWater[state[row]][2];water.FftParameters[0]=expectedWater[state[row]][3];
			water.FoamHistoryArea[0]=historyTail[state[row]][0];water.FoamHistoryArea[3]=historyTail[state[row]][1];
			water.FoamHistoryParameters[3]=historyTail[state[row]][2];
			water.ContactHullData[0][3]=contactTail[state[row]][0];
			water.ContactHullData[79][3]=contactTail[state[row]][1];
			water.ContactHullData[79][2]=contactTail[state[row]][2];
			water.FoamFillLights[0][3]=fillTail[state[row]][0];water.FoamFillLights[1][0]=fillTail[state[row]][1];
			water.FoamFillLights[2][2]=fillTail[state[row]][2];water.FoamFillLights[3][2]=fillTail[state[row]][3];
			draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
			draw.VertexWaterConstants=draw.PixelConstants;draw.VertexWaterConstantsSize=draw.PixelConstantsSize;
			// Poison the source after recording: deferred execution must read its copied snapshot.
			memset(&water,0xDA,sizeof(water));
		} else CHECK(draw.VertexWaterConstantsSize==0 && draw.PixelConstantsSize==0);
		draw.Viewport[1]=float(row);draw.Viewport[2]=width;draw.Viewport[3]=1;draw.Viewport[5]=1;
		draw.Scissor[1]=row;draw.Scissor[2]=width;draw.Scissor[3]=1;frame->Record_Draw(draw);
	}
	const auto values=read_float_targets(frame,&resources.target,1,width,rows);
	frame->Set_Target(frame->Back_Buffer_Target());
	bool matched=values.size()==width*rows*4;
	if(matched) for(unsigned row=0;row<rows;++row) for(unsigned x=0;x<width;++x) for(unsigned c=0;c<4;++c) {
		float expected=waterRows[row]?expectedWater[state[row]][c]:float(9-c)*2.0f;
		if(waterRows[row] && c>=2)expected+=historyTail[state[row]][2]+(c==2?historyTail[state[row]][1]:0);
		if(waterRows[row] && c<3)expected+=contactTail[state[row]][c];
		if(waterRows[row] && c<3)expected+=c==0?fillTail[state[row]][0]+fillTail[state[row]][1]:fillTail[state[row]][c==1?3:2];
		matched &= std::isfinite(values[(row*width+x)*4+c]) && std::abs(values[(row*width+x)*4+c]-expected)<0.00001f;
	}
	CHECK(matched);
	printf("sdl_gpu_frame_selfcheck: GPU water VS slot1: %u queued draws, shared 186-float4 pixel snapshot, mean/FFT/history/contact/fill-light tails in both stages, explicit zero contact/light fallback, intervening non-water and rectangle clear checked\n",rows);
}

static void check_gpu_water_visual_uniform_order(SdlGpuFrame *frame)
{
	// Every packet register and component is rendered, including both sides of b1/b2/b3
	// boundaries and hull eight's tail. Queued draws outlive poisoned stack sources.
	using namespace WaterHullVisualLayout;
	const unsigned width=PacketRegisters+MaxHulls,rows=8;
	const char *output="struct Output { float4 Position:SV_Position; [[vk::location(0)]] float4 Value:TEXCOORD0; };\n";
	const char *position="Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);";
	std::string vs=std::string(output)+WaterFftSampling::UniformSource("WaterVertex",1,false,true)+
		"cbuffer FixedBlock:register(b0){float4 Fixed;};\n"+
		"Output main(uint id:SV_VertexID){"+position+
		"o.Value=float4(WaterParameters.x,ContactHullData[79].w,FoamFillLights[3].z,1)+Fixed;return o;}\n";
	std::string ps=WaterFftSampling::UniformSource("WaterFragment",0,true,true)+R"HLSL(
float4 main(float4 position:SV_Position,[[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {
    int x=(int)position.x;
    if(x<704) {
        float4 packet;
        if(x<256) packet=VisualHullBlock0[x];
        else if(x<512) packet=VisualHullBlock1[x-256];
        else packet=VisualHullBlock2[x-512];
        return packet+VisualHullMeta[x/88]+value;
    }
    return VisualHullMeta[x-704]+value;
}
)HLSL";
	const std::string plainVS=std::string(output)+"Output main(uint id:SV_VertexID){"+position+
		"o.Value=float4(9,8,7,6);return o;}\n";
	const char *plainPS="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0{return value;}\n";
	// UniformSource emits engine register syntax; the production generator retargets it
	// to SDL descriptor sets. Apply that same register placement before direct compilation.
	const bool placed=sdl3target_detail::place_registers(vs,true) && sdl3target_detail::place_registers(ps,false);
	CHECK(placed);if(!placed)return;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs[2]={},*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};SDL_GPUTexture *target=NULL;
		~Resources() {
			if(target) SDL_ReleaseGPUTexture(device,target);
			for(unsigned i=0;i<2;++i) {
				if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vs[i]) SDL_ReleaseGPUShader(device,vs[i]);if(ps[i]) SDL_ReleaseGPUShader(device,ps[i]);
			}
		}
	} resources;
	resources.device=frame->Device();
	for(unsigned water=0;water<2;++water) {
		std::vector<unsigned char> spirv;std::string error;
		bool compiled=SDL3_Compile_HLSL_To_SPIRV(water?vs:plainVS,true,spirv,error);CHECK(compiled);
		if(!compiled){printf("visual hull transport VS: %s\n",error.c_str());return;}
		unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);
		CHECK(samplers==0 && buffers==(water?2u:0u));
		if(samplers!=0 || buffers!=(water?2u:0u))return;
		resources.vs[water]=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs[water]);
		compiled=SDL3_Compile_HLSL_To_SPIRV(water?ps:std::string(plainPS),false,spirv,error);CHECK(compiled);
		if(!compiled){printf("visual hull transport PS: %s\n",error.c_str());return;}
		SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0 && buffers==(water?4u:0u));
		if(samplers!=0 || buffers!=(water?4u:0u))return;
		resources.ps[water]=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps[water]);
		if(!resources.vs[water] || !resources.ps[water]) return;
		SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
		SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs[water];info.fragment_shader=resources.ps[water];
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
		resources.pipelines[water]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipelines[water]);
		if(!resources.pipelines[water]) return;
	}
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=width;texture.height=rows;texture.layer_count_or_depth=texture.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
	const bool waterRows[rows]={true,false,true,true,false,true,true,true};
	const unsigned states[rows]={1,0,2,1,0,0,1,2};
	for(unsigned row=0;row<rows;++row) {
		if(row==3) {
			frame->Set_Target(frame->Back_Buffer_Target());frame->Clear_Rect(0,0,1,1,true,false,false,0xFF123456u,1,0);
			frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
		}
		if(row==6) {
			// A new batch and replacement scene target cannot reuse old constant-arena offsets.
			CHECK(frame->Flush());CHECK(frame->Resize(WIDTH+3,HEIGHT+2));
			frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
		}
		SdlRecordedDraw draw={};draw.Pipeline=resources.pipelines[waterRows[row]?1:0];draw.Count=3;
		memset(frame->Stage(16,draw.VertexOffset),0,16);
		if(waterRows[row]) {
			const unsigned state=states[row];SdlWaterVisualPixelConstants water={};float packet[PacketRegisters][4]={};
			float fixed[4]={.0625f,.125f,.1875f,.25f};
			draw.VertexConstants=frame->Constants(0,fixed,sizeof(fixed));draw.VertexConstantsSize=sizeof(fixed);
			water.Water.WaterParameters[0]=state+.125f;water.Water.ContactHullData[79][3]=state*2+.25f;
			water.Water.FoamFillLights[3][2]=state*3+.5f;
			for(unsigned h=0;h<MaxHulls;++h)for(unsigned c=0;c<4;++c)
				water.VisualHullMeta[h][c]=state?float(state*100+h*4+c)+.5f:0;
			for(unsigned r=0;r<PacketRegisters;++r)for(unsigned c=0;c<4;++c)
				packet[r][c]=state?float(state*10000+r*4+c)+.25f:0;
			draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
			draw.VertexWaterConstants=draw.PixelConstants;draw.VertexWaterConstantsSize=sizeof(SdlWaterPixelConstants);
			for(unsigned b=0;b<BlockCount;++b) {
				draw.PixelWaterConstantsSize[b]=BlockRegisters(b)*16;
				draw.PixelWaterConstants[b]=frame->Constants(1,packet[BlockOffset(b)],draw.PixelWaterConstantsSize[b],b+1);
			}
			// Cache keys include the slot, so same-sized b1/b2 and later b0 cannot evict each other.
			CHECK(frame->Constants(1,packet[0],Block0Registers*16,1)==draw.PixelWaterConstants[0]);
			CHECK(frame->Constants(1,&water,sizeof(water))==draw.PixelConstants);
			memset(&water,0xDA,sizeof(water));memset(packet,0xDB,sizeof(packet));
		} else {
			CHECK(draw.PixelConstantsSize==0 && draw.VertexWaterConstantsSize==0);
			for(unsigned b=0;b<BlockCount;++b)CHECK(draw.PixelWaterConstantsSize[b]==0);
		}
		draw.Viewport[1]=float(row);draw.Viewport[2]=width;draw.Viewport[3]=1;draw.Viewport[5]=1;
		draw.Scissor[1]=row;draw.Scissor[2]=width;draw.Scissor[3]=1;frame->Record_Draw(draw);
	}
	const auto values=read_float_targets(frame,&resources.target,1,width,rows);
	frame->Set_Target(frame->Back_Buffer_Target());CHECK(frame->Resize(WIDTH,HEIGHT));
	bool matched=values.size()==width*rows*4;unsigned armedLatest=0,armedBoundary=0;double maxError=0;
	if(matched)for(unsigned row=0;row<rows;++row)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<4;++c) {
		const unsigned state=states[row],h=x<PacketRegisters?x/RegistersPerHull:x-PacketRegisters;
		double expected=9-c;
		if(waterRows[row]) {
			const double prefix=c==0?state+.125:c==1?state*2+.25:c==2?state*3+.5:1;
			expected=prefix+(c+1)*.0625+(state?state*100+h*4+c+.5:0);
			if(x<PacketRegisters)expected+=state?state*10000+x*4+c+.25:0;
		}
		const double actual=values[(row*width+x)*4+c],error=std::abs(actual-expected);maxError=std::max(maxError,error);
		matched&=std::isfinite(actual) && error<.0001;
		if(waterRows[row] && state==1 && x<PacketRegisters)armedLatest+=std::abs(actual-(expected+10000))>100;
		if(waterRows[row] && state && (x==256 || x==512))armedBoundary+=std::abs(actual-(expected-1024))>100;
	}
	CHECK(matched && armedLatest>0 && armedBoundary>0);
	printf("sdl_gpu_frame_selfcheck: visual hull transport: all704 packet float4s +8 metadata, PS slots0..3, 190-register VS alias and 198-register PS common, queued A/B/zero, non-water, clear, new batch, resize; max error %.9g, armed %u/%u\n",maxError,armedLatest,armedBoundary);
}

static void check_gpu_water_visual_query(SdlGpuFrame *frame)
{
	// Closed-form geometry is the oracle, never WaterHullVisual::Query. Every
	// hull uses all 96 index scalars, exercising 255/256,511/512 and703.w.
	using namespace WaterHullVisualLayout;
	const unsigned width=MaxHulls*3,rows=27;
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string ps=WaterFftSampling::UniformSource("WaterQuery",0,true,true)+WaterHullVisual::Source()+R"HLSL(
float4 main(float4 position:SV_Position):SV_Target0 {
    int column=(int)position.x,hull=column/3;
    float scale=1.0+0.25*hull;
    float3 q=float3(float2(17*hull,-3*hull)+WaterParameters.xy*scale,WaterParameters.z);
    int mode=(int)WaterParameters.w;
    if(mode==19) hull=-1;
    if(mode==20) hull=8;
    WaterVisualResult r=WaterVisualQuery(hull,q);
    if(column%3==0) return float4(r.distance,r.hasSegment,r.coplanar,r.segmentCount);
    if(column%3==1) return float4(r.closest,r.positive);
    return float4(r.negative,r.minY,r.maxY);
}
)HLSL";
	const bool placed=sdl3target_detail::place_registers(ps,false);
	CHECK(placed);if(!placed)return;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;SDL_GPUTexture *target=nullptr;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} resources;
	resources.device=frame->Device();
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
	if(!compiled){printf("visual query VS: %s\n",error.c_str());return;}
	resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("visual query PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0&&buffers==4);
	if(samplers!=0 || buffers!=4)return;
	resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);
	if(!resources.vs||!resources.ps)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs;info.fragment_shader=resources.ps;
	info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
	info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=colour.format;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=width;texture.height=rows;texture.layer_count_or_depth=texture.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
	std::vector<float> expected(width*rows*4,0);
	const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
	for(unsigned row=0;row<rows;++row) {
		SdlWaterVisualPixelConstants water={};float packet[PacketRegisters][4]={};
		float qx=2,qy=3,qz=0;
		if(row==1){qx=qy=0;qz=-2;} // exact tangent vertex
		if(row==2){qx=4;qy=2;qz=2;} // exact top edge
		if(row==3)qz=3; // no triangle at this height
		if(row==4){qx=2.5f;qy=2.25f;qz=.5f;} // raked triangle at intermediate height
		if(row==5||row==6||row==26){qx=row==26?-5.75f:row==5?5.75f:6.75f;qy=.25f;qz=row==6?1:.5f;}
		if(row==7){qx=qy=5;qz=0;} // coplanar plane must not emit its diagonal
		if(row==17)qx=nan;
		if(row==18)qz=inf;
		water.Water.WaterParameters[0]=qx;water.Water.WaterParameters[1]=qy;
		water.Water.WaterParameters[2]=qz;water.Water.WaterParameters[3]=float(row);
		for(unsigned h=0;h<MaxHulls;++h) {
			const float ox=float(h*17),oy=-float(h*3),s=1+h*.25f;
			WaterHullVisual::Packet p={};p.metadata[0]=64;p.metadata[1]=96;p.metadata[2]=512;p.metadata[3]=1;
			const float triangle[3][3]={{0,0,-2},{8,0,2},{0,4,2}};
			for(unsigned v=0;v<3;++v){p.data[61+v][0]=ox+s*triangle[v][0];p.data[61+v][1]=oy+s*triangle[v][1];p.data[61+v][2]=triangle[v][2];}
			for(unsigned t=0;t<96;++t)p.data[64+t/4][t%4]=float(61+64*62+4096*63);
			if(row==5||row==6||row==26) {
				// Closed raked box: at height z, L=4+2z and B=1+z/2.
				const float v[8][3]={{-2,-.5f,-1},{2,-.5f,-1},{2,.5f,-1},{-2,.5f,-1},
					{-6,-1.5f,1},{6,-1.5f,1},{6,1.5f,1},{-6,1.5f,1}};
				const unsigned f[12][3]={{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7},
					{0,2,1},{0,3,2},{4,5,6},{4,6,7}};
				for(unsigned i=0;i<8;++i){p.data[i][0]=ox+s*v[i][0];p.data[i][1]=oy+s*v[i][1];p.data[i][2]=v[i][2];}
				p.metadata[0]=8;
				for(unsigned t=0;t<96;++t){const auto& indices=f[t%12];p.data[64+t/4][t%4]=float(indices[0]+64*indices[1]+4096*indices[2]);}
			}
			if(row==7) {
				const float v[4][2]={{0,0},{10,0},{10,10},{0,10}};
				for(unsigned i=0;i<4;++i){p.data[i][0]=ox+s*v[i][0];p.data[i][1]=oy+s*v[i][1];p.data[i][2]=0;}
				p.metadata[0]=4;p.metadata[1]=2;p.data[64][0]=float(0+64*1+4096*2);p.data[64][1]=float(0+64*2+4096*3);
			}
			if(row==8) {WaterHullVisual::Builder degenerate(-2,2);CHECK(degenerate.AddTriangle({0,0,-1},{1,1,0},{2,2,1}));CHECK(degenerate.Finish(p));}
			if(row==9){p=WaterHullVisual::Packet{};p.metadata[3]=1;}
			if(row==10)p.metadata[3]=0;
			if(row==11)p.metadata[1]=97;
			if(row==12)p.metadata[0]=65;
			if(row==13)p.data[64][0]=nan;
			if(row==14)p.data[64][0]+=.5f;
			if(row==15)p.metadata[0]=63; // index63 outside declared vertex count
			if(row==16)p.data[61][0]=nan;
			if(row==21)p.metadata[2]=-1;
			if(row==22)p.metadata[0]=nan;
			if(row==23)p.metadata[0]=0; // incomplete nonempty pair
			if(row==24)p.data[64][0]=262144;
			if(row==25)p.metadata[1]=1.5f;
			memcpy(water.VisualHullMeta[h],p.metadata,sizeof(p.metadata));memcpy(packet[h*RegistersPerHull],p.data,sizeof(p.data));
			float e[12]={1e6f,0,0,0,0,0,0,0,0,0,0,0};
			if((row<=6&&row!=3)||row==26) {
				e[1]=1;e[3]=row==5||row==6||row==26?64:96;
				float ax=0,ay=0,px=0,py=0,nx=0,ny=0,minY=0,maxY=0;
				if(row==0){e[0]=4*s/std::sqrt(5.0f);ax=1.2f;ay=1.4f;px=4;ny=2;maxY=2;}
				if(row==1)e[0]=0;
				if(row==2){e[0]=0;ax=4;ay=2;px=8;ny=4;maxY=4;}
				if(row==4){e[0]=2*s/std::sqrt(5.0f);ax=2.1f;ay=1.45f;px=5;ny=2.5f;maxY=2.5f;}
				if(row==5||row==6||row==26){const float L=row==6?6:5,B=row==6?1.5f:1.25f;e[0]=.75f*s;e[2]=row==6?1:0;ax=row==26?-L:L;ay=.25f;px=L;py=0;nx=-L;ny=0;minY=-B;maxY=B;}
				e[4]=ox+s*ax;e[5]=oy+s*ay;e[6]=ox+s*px;e[7]=oy+s*py;
				e[8]=ox+s*nx;e[9]=oy+s*ny;e[10]=oy+s*minY;e[11]=oy+s*maxY;
			}
			if(row==7)e[2]=1;
			memcpy(&expected[(row*width+h*3)*4],e,sizeof(e));
		}
		SdlRecordedDraw draw={};draw.Pipeline=resources.pipeline;draw.Count=3;memset(frame->Stage(16,draw.VertexOffset),0,16);
		draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
		for(unsigned b=0;b<BlockCount;++b){draw.PixelWaterConstantsSize[b]=BlockRegisters(b)*16;
			draw.PixelWaterConstants[b]=frame->Constants(1,packet[BlockOffset(b)],draw.PixelWaterConstantsSize[b],b+1);}
		draw.Viewport[1]=float(row);draw.Viewport[2]=width;draw.Viewport[3]=1;draw.Viewport[5]=1;
		draw.Scissor[1]=row;draw.Scissor[2]=width;draw.Scissor[3]=1;frame->Record_Draw(draw);
		memset(packet,0xDB,sizeof(packet));memset(&water,0xDA,sizeof(water));
	}
	const auto actual=read_float_targets(frame,&resources.target,1,width,rows);frame->Set_Target(frame->Back_Buffer_Target());
	bool matched=actual.size()==expected.size();double maxError=0;unsigned checked=0,armed=0;
	if(matched)for(unsigned i=0;i<actual.size();++i){const double difference=std::abs(double(actual[i])-expected[i]);maxError=std::max(maxError,difference);
		if(!std::isfinite(actual[i])||difference>.0005){if(checked<8)printf("visual query mismatch row%u hull%u component%u: %.9g expected %.9g\n",i/(width*4),(i%(width*4))/12,i%12,actual[i],expected[i]);++checked;matched=false;}
		if(i<width*4&&i%12==0)armed+=std::abs(actual[i])>.5;}
	CHECK(matched&&armed==8);
	printf("sdl_gpu_frame_selfcheck: exact visual query: 27 closed-form/guard cases x8 hulls x12 outputs; triangle/raked hull/actual-height/tangent/coplanar/empty/corrupt, all packet boundaries; max error %.9g, mismatches %u, armed %u\n",maxError,checked,armed);
}

#if defined(__linux__)
static void check_gpu_water_visual_zero_effect_skip(SdlGpuFrame *frame)
{
	// Compare actual emitted effects against an explicit full-query control using the current
	// contact response in the same fragment. Only the optimized body may skip a zero-effect query.
	// The only test instrumentation is a counter immediately before each geometry query.
	using namespace WaterHullVisualLayout;
	PixelPipelineDescription description={};std::string production;
	const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU);
	CHECK(generated);if(!generated)return;
	auto extract=[&](const char *signature) {
		const size_t start=production.find(signature),opening=start==std::string::npos?start:production.find('{',start);
		if(opening==std::string::npos)return std::string();
		size_t at=opening+1;int depth=1;
		for(;at<production.size()&&depth;++at){if(production[at]=='{')++depth;else if(production[at]=='}')--depth;}
		return depth?std::string():production.substr(start,at-start);
	};
	std::string current=extract("float2 oceanMeshHullEffects("),contact=extract("float4 WaterContactRegister(");
	const std::string sharedContact=extract("float3 oceanMeshContactFoam(");
	CHECK(!sharedContact.empty());if(sharedContact.empty())return;
	const size_t noiseBegin=production.find("float oceanHash("),noiseEnd=production.find("float3 oceanSky(");
	CHECK(!current.empty()&&!contact.empty()&&noiseBegin!=std::string::npos&&noiseEnd!=std::string::npos&&noiseEnd>noiseBegin);
	if(current.empty()||contact.empty()||noiseBegin==std::string::npos||noiseEnd==std::string::npos||noiseEnd<=noiseBegin)return;
	// Pre-skip control with the current contact-response curve. The query ordering and
	// accumulation remain the original implementation; both paths must agree per fragment.
	std::string previous=R"BASELINE(
float2 oceanMeshHullEffects(float3 world,float level,float time,float offshore,
    float incomingHeight,float footprint,float waveImpact)
{
    float foam=0.0,turbulence=0.0;
    if(offshore<=0.001 || abs(world.z-level)>fftVerticalBound) return float2(0,0);
    for(int h=0;h<clamp((int)HullParameters.x,0,8);++h) {
        float4 meta=VisualHullMeta[h];
        if(meta.w<0.5 || meta.y<1.0) continue;
        int index=h*10;
        float4 position=WaterContactRegister(index),axis=WaterContactRegister(index+1);
        if(position.w<3.0 || abs(level-position.z)>0.1) continue;
        float2 delta=world.xy-position.xy;
        if(dot(delta,delta)>(meta.z+12.0)*(meta.z+12.0)) continue;
        float2 local=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
        WaterVisualResult contact=WaterVisualQuery(h,float3(local,world.z-level));
        if(contact.hasSegment<0.5 || contact.distance>12.0) continue;
        float4 motion=WaterContactRegister(index+2);
        float speed=length(motion.xy),moving=smoothstep(0.3,18.0,speed);
        float2 velocity=float2(dot(motion.xy,axis.xy),dot(motion.xy,float2(-axis.y,axis.x)));
        float blend=smoothstep(-0.25,0.25,velocity.x/max(speed,0.001));
        float2 bow=lerp(contact.negative,contact.positive,blend);
        float width=max((contact.maxY-contact.minY)*0.5,1.0);
        float aa=clamp(footprint*0.6,0.25,1.5);
        float wash=oceanNoise(local*float2(0.065,0.32)+float2(-time*0.12,motion.z*9.0));
        float flecks=oceanNoise(local*0.48+float2(time*0.08,-time*0.13));
        float collision=waterHullContactResponse(incomingHeight,waveImpact);
        float sideWidth=1.0+moving*min(2.0,sqrt(width)*0.35)+collision*1.3;
        float rim=1.0-smoothstep(sideWidth,sideWidth+aa,contact.distance);
        float streak=smoothstep(0.43,0.68,wash)*(0.2+0.8*smoothstep(0.25,0.65,flecks));
        float sideFoam=max(rim*streak*moving*0.38,
            waterHullContactCoverage(contact.distance,footprint,incomingHeight,waveImpact,width));
        float outer=1.0-smoothstep(sideWidth,sideWidth+3.5,contact.distance);
        float bowDistance=length((local-bow)/float2(max(width*0.8,3.0),max(width*0.9,3.0)));
        float bowRegion=exp(-bowDistance*bowDistance*1.4);
        float bowWidth=2.0+moving*min(7.0,3.0+sqrt(width)*0.55);
        float bowFoam=(1.0-smoothstep(bowWidth,bowWidth+aa,contact.distance))*bowRegion
            *moving*(0.45+0.55*smoothstep(0.25,0.7,flecks));
        foam=max(foam,max(sideFoam,bowFoam*0.82));
        turbulence=max(turbulence,outer*(collision*0.18+moving*0.25)+bowFoam*0.4);
    }
    return saturate(float2(foam,turbulence))*offshore;
}
)BASELINE";
	auto instrument=[&](std::string text,const char *name) {
		const std::string signature="oceanMeshHullEffects(",tail="float waveImpact)\n{",query="WaterVisualResult contact=WaterVisualQuery(";
		const size_t entry=text.find(signature),parameters=text.find(tail),call=text.find(query);
		CHECK(entry!=std::string::npos&&parameters!=std::string::npos&&call!=std::string::npos);
		if(entry==std::string::npos||parameters==std::string::npos||call==std::string::npos)return std::string();
		// Work backwards so the source offsets remain valid.
		text.insert(call,"queryCalls++; ");
		text.replace(parameters,tail.size(),"float waveImpact,out int queryCalls)\n{\n    queryCalls=0;");
		text.replace(entry,signature.size(),std::string(name)+"(");return text;
	};
	current=instrument(current,"CurrentMeshEffects");previous=instrument(previous,"PreviousMeshEffects");
	if(current.empty()||previous.empty())return;
	const unsigned samplesX=25,samplesY=7,width=samplesX*2;
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string ps=WaterFftSampling::UniformSource("WaterEffects",0,true,true)+WaterFftSampling::CommonSource()+
		production.substr(noiseBegin,noiseEnd-noiseBegin)+WaterHullContactFoam::Source()+
		contact+WaterHullVisual::Source()+sharedContact+previous+current+R"HLSL(
float4 main(float4 pixel:SV_Position):SV_Target0 {
    int x=(int)pixel.x,y=(int)pixel.y;
    float2 local=float2((x/2-12)*2,(y%7-3)*2);
    float2 world=CameraPosition.xy+local.x*CameraPosition.zw+local.y*float2(-CameraPosition.w,CameraPosition.z);
    float3 q=float3(world,WaterParameters.x+TerrainParameters.z);
    int oldCalls=0,newCalls=0;
    float2 oldValue=PreviousMeshEffects(q,WaterParameters.x,WaterParameters.y,WaterParameters.z,
        TerrainParameters.x,WaterParameters.w,TerrainParameters.y,oldCalls);
    float2 newValue=CurrentMeshEffects(q,WaterParameters.x,WaterParameters.y,WaterParameters.z,
        TerrainParameters.x,WaterParameters.w,TerrainParameters.y,newCalls);
    if(x%2==0)return float4(oldValue,newValue);
    return float4(oldCalls,newCalls,0,1);
}
)HLSL";
	const bool placed=sdl3target_detail::place_registers(ps,false);CHECK(placed);if(!placed)return;
	struct Case {
		float speed=0,impact=.6f,height=.75f,time=1.25f,offshore=1,footprint=.5f,z=0,axisX=1,axisY=0;
		unsigned slot=0,mix=0,packet=0,oldCalls=1,newCalls=1;
		bool expectZero=false,expectPositive=true;
	};
	std::vector<Case> cases;
	auto add=[&](Case c){cases.push_back(c);return unsigned(cases.size()-1);};
	Case c;
	const unsigned stationaryImpact=add(c);
	// Zero slope alone is not calm: a broad crest must survive. The moving-threshold
	// controls below use height=0 too, isolating the unchanged propulsion predicate.
	c.impact=0;const unsigned stationaryCrest=add(c);
	c.height=0;c.newCalls=0;c.expectZero=true;c.expectPositive=false;add(c);
	c.speed=.2999f;add(c);c.speed=.3f;add(c);
	c.speed=std::nextafter(.3f,1.0f);c.newCalls=1;c.expectZero=false;c.expectPositive=true;add(c);
	c.speed=.3001f;add(c);c.speed=2;add(c);c.speed=18;const unsigned movingNoImpact=add(c);
	c.speed=-18;add(c);
	// There is no old .04 slope cutoff in the incident-energy response. Keep those
	// samples as armed controls, plus a small positive slope at mean water height.
	c=Case{};c.height=0;c.impact=.0001f;add(c);
	c.impact=.03999f;add(c);c.impact=.04f;add(c);
	c.impact=std::nextafter(.04f,1.0f);add(c);c.impact=.0401f;add(c);c.impact=.2f;add(c);c.impact=.4f;add(c);
	const float trough=-WaterHullContactFoam::HeightScale;
	c=Case{};c.height=std::nextafter(trough,-std::numeric_limits<float>::infinity());
	c.newCalls=0;c.expectZero=true;c.expectPositive=false;add(c);
	c.height=trough;add(c);
	c.height=std::nextafter(trough,0.0f);c.newCalls=1;c.expectZero=false;c.expectPositive=true;add(c);
	c.height=-.8f;add(c);c.height=1.6f;add(c);
	c=Case{};c.speed=18;c.impact=0;c.height=-2;add(c);
	for(unsigned h=0;h<MaxHulls;++h){c=Case{};c.speed=18;c.slot=h;c.time+=h*.57f;add(c);}
	c=Case{};c.impact=0;c.height=0;c.mix=1;c.oldCalls=2;c.newCalls=1;const unsigned inactiveFirst=add(c);
	c.mix=2;const unsigned inactiveLast=add(c);c.mix=3;c.newCalls=2;add(c);
	c=Case{};c.speed=18;c.axisX=.6f;c.axisY=.8f;c.footprint=4;add(c);
	c=Case{};c.z=1;add(c);c.z=2;add(c);c.z=3;c.expectZero=true;c.expectPositive=false;add(c);
	c=Case{};c.offshore=.001f;c.oldCalls=c.newCalls=0;c.expectZero=true;c.expectPositive=false;add(c);
	c.offshore=0;add(c);c=Case{};c.offshore=.0011f;add(c);
	c=Case{};c.z=WaterFftGeometry::MaxVerticalDisplacement+.25f;c.oldCalls=c.newCalls=0;c.expectZero=true;c.expectPositive=false;add(c);
	for(unsigned mode=1;mode<=6;++mode){c=Case{};c.packet=mode;c.expectZero=true;c.expectPositive=false;
		c.oldCalls=c.newCalls=mode==3?1:0;add(c);}
	const unsigned rows=unsigned(cases.size())*samplesY;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;SDL_GPUTexture *target=nullptr;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} resources;
	resources.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);CHECK(samplers==0&&buffers==0);if(samplers||buffers)return;
	resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("exact zero-effect PS: %s\n",error.c_str());return;}
	SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0&&buffers==4);if(samplers||buffers!=4)return;
	resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs;info.fragment_shader=resources.ps;
	info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
	info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=width;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
	for(unsigned row=0;row<cases.size();++row) {
		const Case &f=cases[row];SdlWaterVisualPixelConstants water={};float packet[PacketRegisters][4]={};
		water.Water.WaterParameters[0]=7;water.Water.WaterParameters[1]=f.time;water.Water.WaterParameters[2]=f.offshore;water.Water.WaterParameters[3]=f.footprint;
		water.Water.TerrainParameters[0]=f.height;water.Water.TerrainParameters[1]=f.impact;water.Water.TerrainParameters[2]=f.z;
		water.Water.CameraPosition[0]=47;water.Water.CameraPosition[1]=-29;water.Water.CameraPosition[2]=f.axisX;water.Water.CameraPosition[3]=f.axisY;
		water.Water.HullParameters[0]=f.mix?8:float(f.slot+1);
		for(unsigned h=0;h<MaxHulls;++h) {
			if(f.mix?(h!=0&&h!=7):(h!=f.slot))continue;
			const float vertices[8][3]={{-20,-4,-2},{20,-4,-2},{20,4,-2},{-20,4,-2},
				{-20,-4,2},{20,-4,2},{20,4,2},{-20,4,2}};
			const unsigned faces[12][3]={{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7},
				{0,2,1},{0,3,2},{4,5,6},{4,6,7}};
			WaterHullVisual::Packet p={};p.metadata[0]=8;p.metadata[1]=96;p.metadata[2]=21;p.metadata[3]=1;
			for(unsigned v=0;v<8;++v)for(unsigned d=0;d<3;++d)p.data[v][d]=vertices[v][d];
			for(unsigned t=0;t<96;++t){const auto &v=faces[t%12];p.data[64+t/4][t%4]=float(v[0]+64*v[1]+4096*v[2]);}
			float speed=f.speed;
			if(f.mix==1)speed=h==7?18:0;
			if(f.mix==2)speed=h==0?18:0;
			if(f.mix==3)speed=h==0?2:18;
			float *position=water.Water.ContactHullData[h*10],*axis=water.Water.ContactHullData[h*10+1],*motion=water.Water.ContactHullData[h*10+2];
			position[0]=47;position[1]=-29;position[2]=7;position[3]=4;
			axis[0]=f.axisX;axis[1]=f.axisY;motion[0]=speed*f.axisX;motion[1]=speed*f.axisY;motion[2]=.37f+h*.09f;
			if(f.packet==1)p.metadata[3]=0;
			if(f.packet==2){p.metadata[0]=p.metadata[1]=0;}
			if(f.packet==3)p.data[64][0]=std::numeric_limits<float>::quiet_NaN();
			if(f.packet==4)position[2]=8;
			if(f.packet==5)position[3]=2; // fallback mean packet is also explicitly empty
			if(f.packet==6)position[0]+=1000;
			memcpy(water.VisualHullMeta[h],p.metadata,sizeof(p.metadata));memcpy(packet[h*RegistersPerHull],p.data,sizeof(p.data));
		}
		SdlRecordedDraw draw={};draw.Pipeline=resources.pipeline;draw.Count=3;memset(frame->Stage(16,draw.VertexOffset),0,16);
		draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
		for(unsigned b=0;b<BlockCount;++b){draw.PixelWaterConstantsSize[b]=BlockRegisters(b)*16;
			draw.PixelWaterConstants[b]=frame->Constants(1,packet[BlockOffset(b)],draw.PixelWaterConstantsSize[b],b+1);}
		draw.Viewport[1]=float(row*samplesY);draw.Viewport[2]=width;draw.Viewport[3]=samplesY;draw.Viewport[5]=1;
		draw.Scissor[1]=row*samplesY;draw.Scissor[2]=width;draw.Scissor[3]=samplesY;frame->Record_Draw(draw);
		memset(packet,0xDB,sizeof(packet));memset(&water,0xDA,sizeof(water));
	}
	const auto actual=read_float_targets(frame,&resources.target,1,width,rows);frame->Set_Target(frame->Back_Buffer_Target());
	bool matched=actual.size()==size_t(width)*rows*4;double maxError=0;unsigned skipped=0,active=0,mismatches=0;
	std::vector<unsigned> positive(cases.size(),0);
	if(matched)for(unsigned row=0;row<cases.size();++row)for(unsigned y=0;y<samplesY;++y)for(unsigned x=0;x<samplesX;++x) {
		const Case &f=cases[row];const size_t offset=((row*samplesY+y)*width+x*2)*4;
		for(unsigned channel=0;channel<2;++channel) {
			const float before=actual[offset+channel],after=actual[offset+2+channel];const double difference=std::abs(double(before)-after);
			maxError=std::max(maxError,difference);
			if(!std::isfinite(before)||!std::isfinite(after)||before<0||before>1||after<0||after>1||difference>1e-6||(f.expectZero&&(before!=0||after!=0))) {
				if(mismatches++<8)printf("zero-effect mismatch case%u xy%u/%u channel%u old %.9g new %.9g\n",row,x,y,channel,before,after);matched=false;
			}
			if(after>0)++positive[row];
		}
		const float oldCalls=actual[offset+4],newCalls=actual[offset+5];
		if(oldCalls!=f.oldCalls||newCalls!=f.newCalls){if(mismatches++<8)printf("zero-effect query count case%u: %.0f/%.0f expected %u/%u\n",row,oldCalls,newCalls,f.oldCalls,f.newCalls);matched=false;}
		if(f.newCalls<f.oldCalls)++skipped;else if(f.newCalls)++active;
	}
	if(matched)for(unsigned row=0;row<cases.size();++row)if(cases[row].expectPositive)CHECK(positive[row]>0);
	CHECK(matched&&skipped>0&&active>0);
	CHECK(positive[stationaryImpact]>0&&positive[stationaryCrest]>0&&positive[movingNoImpact]>0&&positive[inactiveFirst]>0&&positive[inactiveLast]>0);
	printf("sdl_gpu_frame_selfcheck: exact zero-effect skip: %zu cases x%u samples, emitted/control same-fragment max %.9g, mismatches %u; query-skipped %u active %u; calm/trough and moving thresholds, all8 slots/96 triangles, stationary slope/zero-slope crest, moving/reverse, mixed accumulation, actual-height and guards checked\n",
		cases.size(),samplesX*samplesY,maxError,mismatches,skipped,active);
}
#endif // __linux__

static void check_gpu_foam_history_upload(SdlGpuFrame *frame)
{
	// Exercise the same versioned CPU texture mirror as the runtime atlas. Reading via slot14
	// catches binding truncation; changing a used image before submitting catches upload hazards.
	const unsigned width=8,tileHeight=4,rows=tileHeight*4;
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string ps=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+R"HLSL(
Texture2D History:register(t14);SamplerState HistorySampler:register(s14);
float4 main(float4 pixel:SV_Position):SV_Target0 {
    float2 uv=float2(pixel.x/8.0,frac(pixel.y/4.0));
    float2 world=FoamHistoryArea.xy+uv/FoamHistoryArea.zw;
    return History.SampleLevel(HistorySampler,(world-FoamHistoryArea.xy)*FoamHistoryArea.zw,0);
}
)HLSL";
	CHECK(SDL3_Shader_Retarget(ps,false));
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL;SDL_GPUSampler *sampler=NULL;PosixTexture9 *atlas=NULL;
		~Resources(){if(atlas)atlas->Release();if(target)SDL_ReleaseGPUTexture(device,target);
			if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vertex)SDL_ReleaseGPUShader(device,vertex);
			if(pixel)SDL_ReleaseGPUShader(device,pixel);if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} resources;
	resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("history upload PS: %s\n",error.c_str());return;}
	unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);CHECK(samplers==15 && uniforms==1);
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	pi.target_info.has_depth_stencil_target=true;pi.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=width;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.sampler);if(!resources.sampler)return;
	resources.atlas=PosixTexture9::create(width,tileHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);CHECK(resources.atlas);if(!resources.atlas)return;
	auto packed=[](unsigned revision,unsigned x,unsigned y) {
		return ((32+x*20+revision*40)<<16)|(((y+1)*16+revision*8)<<8)|(12+x*3+y*7+revision*5);
	};
	auto update=[&](unsigned revision) {
		D3DLOCKED_RECT locked={};CHECK(resources.atlas->LockRect(0,&locked,NULL,0)==D3D_OK);
		if(!locked.pBits)return;
		for(unsigned y=0;y<tileHeight;++y)for(unsigned x=0;x<width;++x)
			((uint32_t *)((uint8_t *)locked.pBits+y*locked.Pitch))[x]=packed(revision,x,y);
		CHECK(resources.atlas->UnlockRect(0)==D3D_OK);
	};
	CHECK(frame->Flush());const unsigned oldWidth=frame->Width(),oldHeight=frame->Height();
	{
		SdlResourceMirrors mirrors(frame);
		auto record=[&](unsigned band) {
			std::string refusal;SDL_GPUTexture *atlas=mirrors.Texture(resources.atlas,refusal);CHECK(atlas);if(!atlas)return;
			SDL_GPUTexture *white=mirrors.White();CHECK(white);if(!white)return;
			frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
			SdlRecordedDraw draw={};draw.Pipeline=resources.pipeline;draw.Count=3;draw.SamplerCount=15;
			memset(frame->Stage(16,draw.VertexOffset),0,16);
			for(unsigned i=0;i<15;++i){draw.Textures[i]=i==14?atlas:white;draw.Samplers[i]=resources.sampler;}
			SdlWaterPixelConstants water={};water.FoamHistoryArea[0]=band==3?96:-32;water.FoamHistoryArea[1]=band==3?-128:48;
			water.FoamHistoryArea[2]=1.0f/64;water.FoamHistoryArea[3]=1.0f/32;
			water.FoamHistoryParameters[0]=1;water.FoamHistoryParameters[1]=7;water.FoamHistoryParameters[2]=8;water.FoamHistoryParameters[3]=float(band);
			draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
			draw.Viewport[1]=float(band*tileHeight);draw.Viewport[2]=width;draw.Viewport[3]=tileHeight;draw.Viewport[5]=1;
			draw.Scissor[1]=band*tileHeight;draw.Scissor[2]=width;draw.Scissor[3]=tileHeight;frame->Record_Draw(draw);
		};
		update(0);record(0);CHECK(mirrors.Textures_Uploaded()==1);
		const uint64_t oldBatch=frame->Batch();update(1);record(1);
		CHECK(frame->Batch()>oldBatch && mirrors.Stale_Flushes()==1 && mirrors.Textures_Uploaded()==2);
		record(2);CHECK(mirrors.Textures_Uploaded()==2); // duplicate/reflection draw does not upload again
		// Destroy the CPU owner while recorded draws still reference its GPU copy. The normal
		// deferred-release path must retain that copy through submission and a frame resize.
		resources.atlas->Release();resources.atlas=NULL;CHECK(mirrors.Live_Copies()==0);mirrors.Collect_Dead();
		CHECK(frame->Resize(oldWidth+8,oldHeight+4));
		resources.atlas=PosixTexture9::create(width,tileHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);CHECK(resources.atlas);
		if(!resources.atlas)return;
		update(2);record(3);CHECK(mirrors.Textures_Uploaded()==3);
		const auto values=read_float_targets(frame,&resources.target,1,width,rows);
		CHECK(values.size()==width*rows*4);bool pixels=values.size()==width*rows*4;
		if(pixels)for(unsigned band=0;band<4;++band)for(unsigned y=0;y<tileHeight;++y)for(unsigned x=0;x<width;++x) {
			const uint32_t expected=packed(band==0?0:band==3?2:1,x,y);
			for(unsigned c=0;c<4;++c) {
				const unsigned shift=c<3?16-c*8:24;const float value=values[((band*tileHeight+y)*width+x)*4+c];
				pixels &= std::isfinite(value) && std::abs(value-float((expected>>shift)&255)/255)<.00001f;
			}
		}
		CHECK(pixels);resources.atlas->Release();resources.atlas=NULL;mirrors.Collect_Dead();CHECK(frame->Flush());
	}
	CHECK(frame->Resize(oldWidth,oldHeight));frame->Set_Target(frame->Back_Buffer_Target());
	printf("sdl_gpu_frame_selfcheck: GPU foam history upload: slot14 channels, queued A/B snapshots, unchanged reuse, deferred release, resize and recreation checked\n");
}

static void check_gpu_foam_history_tiles(SdlGpuFrame *frame)
{
    // Actual slot14 UNORM mirror, production tile/shore helpers, and an independent
    // single-tile reference. No material/noise helper is involved in this test.
    struct Query {float uvLevelTime[4];};std::vector<Query> queries;
    auto add=[&](float x,float y,float level=7,float time=10){queries.push_back({{x,y,level,time}});};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)add((x+.5f)/8,(y+.5f)/4);
    const float edges[]={0,1.f/4096,1.f/16,3.f/32,.5f,29.f/32,15.f/16,1-1.f/4096,1};
    for(float x:edges){add(x,0);add(x,1);add(0,x);add(1,x);}
    add(5.f/32,7.f/16);add(13.f/32,11.f/16);add(-1.f/32,.5f);add(33.f/32,.5f);
    add(.5f,-1.f/32);add(.5f,33.f/32);add(.5f,.5f,8);add(.5f,.5f,7,12);
    const unsigned count=unsigned(queries.size()),bands=9,rows=count*bands;
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::string ps=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+
        "Texture2D<float4> Legacy:register(t13);Texture2D<float4> Texture14:register(t14);SamplerState Sampler14:register(s14);\n"
        "cbuffer TileQueries:register(b1){float4 Queries["+std::to_string(count)+"];};\n"+
        WaterFoamShading::HistorySamplingSource()+WaterShoreFoam::HistorySource()+
        "float4 main(float4 pixel:SV_Position):SV_Target0{float4 q=Queries[(uint)pixel.y%"+std::to_string(count)+"];\n"
        "uint column=(uint)pixel.x;if(column==0)return waterFoamHistoryTile(q.xy,0u);\n"
        "if(column==1)return Legacy.SampleLevel(Sampler14,q.xy,0);\n"
        "if(column==2)return waterFoamHistoryTile(q.xy,1u);\n"
        "float2 p=FoamHistoryArea.xy+q.xy/FoamHistoryArea.zw;return float4(waterShoreHistory(p,q.z,q.w),0,0,1);}\n";
    const bool retarget=SDL3_Shader_Retarget(ps,false);CHECK(retarget);if(!retarget)return;
    struct Resources {
        SdlGpuFrame *f;unsigned oldWidth,oldHeight;SDL_GPUDevice *device;
        SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;
        SDL_GPUTexture *target=nullptr;SDL_GPUSampler *sampler=nullptr;PosixTexture9 *atlas=nullptr,*legacy=nullptr;
        ~Resources(){CHECK(f->Flush());f->Set_Target(f->Back_Buffer_Target());
            if(atlas)atlas->Release();if(legacy)legacy->Release();if(target)SDL_ReleaseGPUTexture(device,target);
            if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vs)SDL_ReleaseGPUShader(device,vs);
            if(ps)SDL_ReleaseGPUShader(device,ps);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
            CHECK(f->Resize(oldWidth,oldHeight));f->Set_Target(f->Back_Buffer_Target());}
    } r{frame,frame->Width(),frame->Height(),frame->Device()};
    std::string error;std::vector<unsigned char> spirv;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);if(!r.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(ok);
    if(!ok){printf("history tiles PS: %s\n",error.c_str());return;}
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);
    CHECK(samplers==15&&buffers==2);if(samplers!=15||buffers!=2)return;
    r.ps=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps);if(!r.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;
    pi.target_info.num_color_targets=1;pi.target_info.has_depth_stencil_target=true;
    pi.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=4;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
    si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r.sampler=SDL_CreateGPUSampler(r.device,&si);CHECK(r.sampler);if(!r.sampler)return;
    auto texel=[](unsigned revision,unsigned tile,unsigned x,unsigned y){
        const unsigned red=tile?239-7*x-3*y-11*revision:17+11*x+7*y+23*revision;
        const unsigned green=tile?211-9*x-5*y-13*revision:43+5*x+13*y+17*revision;
        const unsigned blue=tile?13+19*x+11*y+5*revision:91+3*x+2*y+13*revision;
        const unsigned alpha=tile?227-3*x-7*y-17*revision:32+7*x+9*y+19*revision;
        return (alpha<<24)|(red<<16)|(green<<8)|blue;
    };
    unsigned width=8,height=4;
    auto create=[&](){r.atlas=PosixTexture9::create(width,height*2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);
        r.legacy=PosixTexture9::create(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);
        CHECK(r.atlas&&r.legacy);return r.atlas&&r.legacy;};
    auto update=[&](unsigned revision){
        PosixTexture9 *textures[]={r.legacy,r.atlas};
        for(unsigned n=0;n<2;++n){D3DLOCKED_RECT lock={};const bool locked=textures[n]->LockRect(0,&lock,nullptr,0)==D3D_OK;
            CHECK(locked&&lock.pBits&&lock.Pitch>=int(width*4));if(!locked||!lock.pBits)return false;
            if(lock.Pitch<int(width*4)){textures[n]->UnlockRect(0);return false;}
            for(unsigned y=0;y<height*(n+1);++y)for(unsigned x=0;x<width;++x)
                reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(lock.pBits)+y*lock.Pitch)[x]=texel(revision,y/height,x,y%height);
            const bool unlocked=textures[n]->UnlockRect(0)==D3D_OK;CHECK(unlocked);if(!unlocked)return false;
        }return true;
    };
    if(!create())return;CHECK(frame->Flush());
    {
        SdlResourceMirrors mirrors(frame);
        auto record=[&](unsigned band,unsigned flags){
            std::string refusal;SDL_GPUTexture *legacy=mirrors.Texture(r.legacy,refusal);
            SDL_GPUTexture *atlas=(flags&4)?mirrors.Texture(r.atlas,refusal):legacy;
            SDL_GPUTexture *white=mirrors.White();CHECK(legacy&&atlas&&white);if(!legacy||!atlas||!white)return false;
            SDL_GPUTexture *depth=frame->Depth_For(4,rows);CHECK(depth);if(!depth)return false;
            frame->Set_Target({r.target,depth,4,rows});SdlRecordedDraw draw={};draw.Pipeline=r.pipeline;draw.Count=3;draw.SamplerCount=15;
            void *bytes=frame->Stage(16,draw.VertexOffset);CHECK(bytes);if(!bytes)return false;memset(bytes,0,16);
            for(unsigned i=0;i<15;++i){draw.Textures[i]=i==14?atlas:i==13?legacy:white;draw.Samplers[i]=r.sampler;}
            SdlWaterPixelConstants water={};water.FoamHistoryArea[2]=1.f/64;water.FoamHistoryArea[3]=1.f/32;
            water.FoamHistoryParameters[0]=float(flags);water.FoamHistoryParameters[1]=7;
            water.FoamHistoryParameters[2]=1;water.FoamHistoryParameters[3]=10;
            draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
            draw.PixelWaterConstants[0]=frame->Constants(1,queries.data(),unsigned(queries.size()*sizeof(Query)),1);
            draw.PixelWaterConstantsSize[0]=unsigned(queries.size()*sizeof(Query));
            draw.Viewport[1]=float(band*count);draw.Viewport[2]=4;draw.Viewport[3]=float(count);draw.Viewport[5]=1;
            draw.Scissor[1]=band*count;draw.Scissor[2]=4;draw.Scissor[3]=count;frame->Record_Draw(draw);return true;
        };
        if(!update(0)||!record(0,7))return;CHECK(mirrors.Textures_Uploaded()==2);
        const uint64_t firstBatch=frame->Batch();if(!update(1)||!record(1,7))return;
        CHECK(frame->Batch()>firstBatch&&mirrors.Stale_Flushes()==1&&mirrors.Textures_Uploaded()==4);
        if(!record(2,7)||!record(3,5)||!record(4,6))return;CHECK(mirrors.Textures_Uploaded()==4);
        // The queued B readers retain both old textures and their dimensions through
        // destruction, backbuffer resize and a differently shaped recreated atlas.
        r.atlas->Release();r.atlas=nullptr;r.legacy->Release();r.legacy=nullptr;
        CHECK(mirrors.Live_Copies()==0);mirrors.Collect_Dead();CHECK(frame->Resize(r.oldWidth+5,r.oldHeight+3));
        width=4;height=8;if(!create()||!update(2)||!record(5,7))return;
        if(!record(6,3)||!record(7,1)||!record(8,2))return;CHECK(mirrors.Textures_Uploaded()==6);
        const auto values=read_float_targets(frame,&r.target,1,4,rows);
        CHECK(values.size()==size_t(rows)*4*4);if(values.size()!=size_t(rows)*4*4)return;
        auto reference=[&](unsigned revision,unsigned tile,unsigned w,unsigned h,const Query&q,unsigned channel){
            const double x=double(q.uvLevelTime[0])*w-.5,y=double(q.uvLevelTime[1])*h-.5;
            const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;double value=0;
            for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
                const unsigned sx=unsigned((std::max)(0,(std::min)(int(w)-1,ix+int(dx))));
                const unsigned sy=unsigned((std::max)(0,(std::min)(int(h)-1,iy+int(dy))));
                const unsigned shift=channel<3?16-channel*8:24;
                value+=double((texel(revision,tile,sx,sy)>>shift)&255)/255*(dx?fx:1-fx)*(dy?fy:1-fy);
            }return value;
        };
        const unsigned flags[bands]={7,7,7,5,6,7,3,1,2};
        // The unchanged UNORM legacy sampler also rounds filtered values; it is not a
        // double-precision bilinear evaluator. Bound that independent scalar comparison
        // by one16-bit normalized unit; packed-vs-legacy remains BIT-EXACT below.
        const double scalarTolerance=1.0/65535.0+1e-7;
        double maximum=0,topDifference=0,legacyMaximum=0,centreMaximum=0;
        unsigned exactTop=0,checks=0,roundingExamples=0;
        for(unsigned band=0;band<bands;++band)for(unsigned i=0;i<count;++i){
            const unsigned rev=band==0?0:band<5?1:2,w=band<5?8:4,h=band<5?4:8;
            const Query&q=queries[i];const size_t row=size_t(band*count+i)*16;
            for(unsigned c=0;c<4;++c){
                const double top=reference(rev,0,w,h,q,c),bottom=(flags[band]&4)?reference(rev,1,w,h,q,c):0;
                for(unsigned column=0;column<3;++column){const double expected=column==2?bottom:top;
                    const double error=std::abs(values[row+column*4+c]-expected);maximum=(std::max)(maximum,error);
                    if(column==1) {
                        legacyMaximum=(std::max)(legacyMaximum,error);
                        const double tx=double(q.uvLevelTime[0])*w-.5,ty=double(q.uvLevelTime[1])*h-.5;
                        if(tx==std::floor(tx)&&ty==std::floor(ty))centreMaximum=(std::max)(centreMaximum,error);
                        if(error>.000002 && roundingExamples<4) {++roundingExamples;
                            printf("history legacy rounding: band%u uv(%.8g,%.8g) c%u expected %.10g actual %.10g error %.10g\n",
                                band,double(q.uvLevelTime[0]),double(q.uvLevelTime[1]),c,expected,double(values[row+column*4+c]),error);}
                    }
                    CHECK(std::isfinite(values[row+column*4+c])&&error<scalarTolerance);++checks;}
                const double difference=std::abs(values[row+c]-values[row+4+c]);topDifference=(std::max)(topDifference,difference);
                CHECK(values[row+c]==values[row+4+c]);exactTop+=values[row+c]==values[row+4+c];
            }
            double shore=0;const double u=q.uvLevelTime[0],v=q.uvLevelTime[1];
            if((flags[band]&2)&&u>=0&&u<=1&&v>=0&&v<=1&&q.uvLevelTime[2]==7){
                // Use the independently sampled legacy alpha for this composition oracle.
                // Its scalar bilinear error is already checked above; a rounded UNORM
                // input must not be mistaken for an error in the shore decay/guards.
                const double coverage=values[row+7],elapsed=(std::max)(0.0,double(q.uvLevelTime[3])-10);
                const double distance=(std::min)((std::min)(u,1-u)*64,(std::min)(v,1-v)*32);
                const double t=(std::max)(0.0,(std::min)(1.0,distance/8));
                shore=(1-std::pow((std::max)(1-coverage,1.0/65536),std::exp(-elapsed/5)))*t*t*(3-2*t);
            }
            CHECK(std::isfinite(values[row+12])&&std::abs(values[row+12]-shore)<.000002);
            CHECK(values[row+13]==0&&values[row+14]==0&&values[row+15]==1);
        }
        CHECK(exactTop==bands*count*4&&maximum<scalarTolerance&&topDifference==0);
        CHECK(centreMaximum<.0000001);
        printf("sdl_gpu_frame_selfcheck: history legacy scalar rounding max %.10g, texel centres %.10g; tile-vs-legacy difference %.10g\n",legacyMaximum,centreMaximum,topDifference);
        r.atlas->Release();r.atlas=nullptr;r.legacy->Release();r.legacy=nullptr;mirrors.Collect_Dead();CHECK(frame->Flush());
        printf("sdl_gpu_frame_selfcheck: history tiles: %u scalar samples max %.9g, %u exact legacy/top components; bottom bytes, seam/edges/fractional/outside, shore flags1/2/3/5/6/7, queued A/B, reuse, deferred release, resize and shape recreation checked\n",checks,maximum,exactTop);
    }
}

#if defined(__linux__)
static void check_gpu_foam_history_sampling(SdlGpuFrame *frame)
{
	// Unequal density/age moments distinguish filtered weighted age from an incorrectly
	// filtered unweighted age. All sampling, guards and breakup below are production HLSL.
	const uint32_t texels[4]={0x00800080u,0x00FFFF00u,0,0x00402010u};
	struct Query {float point[4];};std::vector<Query> queries;
	auto add=[&](float x,float y,float level=7,float time=10){queries.push_back({{x,y,level,time}});return unsigned(queries.size()-1);};
	const unsigned fresh=add(64,128),old=add(192,128),weighted=add(256,128),empty=add(320,128);
	add(384,128);add(448,128);const unsigned outside=add(-1,128);add(513,128);add(64,-1);add(64,257);
	const unsigned wrongLevel=add(64,128,8),border=add(0,128),repeat=add(64,128),aged=add(64,128,7,11.5f),expired=add(64,128,7,250);
	const unsigned evolution=unsigned(queries.size());
	for(unsigned i=0;i<16;++i){add(64,16+i*14);add(64,16+i*14,7,22);}
	const unsigned rows=unsigned(queries.size());
	PixelPipelineDescription pd={};std::string production;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,production,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t noiseStart=production.find("float oceanHash("),noiseEnd=production.find("float3 oceanSky(");
	CHECK(noiseStart!=std::string::npos && noiseEnd>noiseStart);if(noiseStart==std::string::npos || noiseEnd==std::string::npos || noiseEnd<=noiseStart)return;
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string oldHistory=WaterFoamShading::Source();
	const size_t oldStart=oldHistory.find("float3 waterFoamHistory(");CHECK(oldStart!=std::string::npos);if(oldStart==std::string::npos)return;
	oldHistory.erase(0,oldStart);
	const std::string replacement="    coverage=waterFoamHistoryRemnants(p,coverage,footprint,age*24.0,time);\n";
	const size_t changed=oldHistory.find(replacement);CHECK(changed!=std::string::npos);if(changed==std::string::npos)return;
	oldHistory.erase(changed,replacement.size());
	oldHistory.replace(oldHistory.find("waterFoamHistory("),strlen("waterFoamHistory("),"frozenHistory(");
	std::string ps=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+
		"Texture2D Texture14:register(t14);SamplerState Sampler14:register(s14);\n"+
		"cbuffer QueryBlock:register(b1){float4 Queries["+std::to_string(rows)+"];};\n"+
		production.substr(noiseStart,noiseEnd-noiseStart)+WaterFoamShading::HistorySamplingSource()+WaterFoamShading::Source()+oldHistory+
		"float4 main(float4 pixel:SV_Position):SV_Target0{float4 q=Queries[(uint)pixel.y];return float4(waterFoamHistory(q.xy,q.z,q.w,0.5),frozenHistory(q.xy,q.z,q.w,0.5).x);}\n";
	CHECK(SDL3_Shader_Retarget(ps,false));
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL;SDL_GPUSampler *sampler=NULL;PosixTexture9 *atlas=NULL;
		~Resources(){if(atlas)atlas->Release();if(target)SDL_ReleaseGPUTexture(device,target);
			if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vertex)SDL_ReleaseGPUShader(device,vertex);
			if(pixel)SDL_ReleaseGPUShader(device,pixel);if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} resources;
	resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("history sampling PS: %s\n",error.c_str());return;}
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=1;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.sampler);if(!resources.sampler)return;
	resources.atlas=PosixTexture9::create(4,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);CHECK(resources.atlas);if(!resources.atlas)return;
	D3DLOCKED_RECT lock={};CHECK(resources.atlas->LockRect(0,&lock,NULL,0)==D3D_OK);if(!lock.pBits)return;
	memcpy(lock.pBits,texels,sizeof(texels));CHECK(resources.atlas->UnlockRect(0)==D3D_OK);
	CHECK(frame->Flush());SdlResourceMirrors mirrors(frame);std::string refusal;
	SDL_GPUTexture *atlas=mirrors.Texture(resources.atlas,refusal),*white=mirrors.White();CHECK(atlas && white);if(!atlas || !white)return;
	CHECK(frame->Flush());SdlWaterPixelConstants water={};water.FoamHistoryArea[2]=1.0f/512;water.FoamHistoryArea[3]=1.0f/256;
	water.FoamHistoryParameters[0]=1;water.FoamHistoryParameters[1]=7;water.FoamHistoryParameters[2]=1;water.FoamHistoryParameters[3]=10;
	auto render=[&](bool enabled) {
		water.FoamHistoryParameters[0]=enabled?1:0;
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
		SDL_GPUTextureSamplerBinding bindings[15]={};for(unsigned i=0;i<15;++i){bindings[i].texture=i==14?atlas:white;bindings[i].sampler=resources.sampler;}
		SDL_BindGPUFragmentSamplers(pass,0,bindings,15);SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
		SDL_PushGPUFragmentUniformData(commands,1,queries.data(),queries.size()*sizeof(Query));SDL_DrawGPUPrimitives(pass,3,1,0,0);
		SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));return read_float_targets(frame,&resources.target,1,1,rows);
	};
	const auto values=render(true),paused=render(true),disabled=render(false);CHECK(values==paused);
	CHECK(values.size()==rows*4 && disabled.size()==rows*4);if(values.size()!=rows*4 || disabled.size()!=rows*4)return;
	double ageError=0,churnError=0;
	for(unsigned row=0;row<rows;++row) {
		const Query &q=queries[row];const bool inside=q.point[0]>=0 && q.point[0]<=512 && q.point[1]>=0 && q.point[1]<=256 && q.point[2]==7;
		double channels[3]={};const double coordinate=q.point[0]/512.0*4-.5;const int lower=int(std::floor(coordinate));const double fraction=coordinate-lower;
		for(unsigned c=0;c<3;++c)for(unsigned tap=0;tap<2;++tap) {
			const int index=(std::max)(0,(std::min)(3,lower+int(tap)));
			channels[c]+=double((texels[index]>>(16-c*8))&255)/255*(tap?fraction:1-fraction);
		}
		const double elapsed=(std::max)(0.0,double(q.point[3])-10),expectedAge=inside && channels[0]>0?(std::min)(1.0,channels[1]/channels[0]+elapsed/24):0;
		const double expectedChurn=inside?channels[2]*std::exp(-elapsed/1.5):0;
		ageError=(std::max)(ageError,std::abs(values[row*4+1]-expectedAge));churnError=(std::max)(churnError,std::abs(values[row*4+2]-expectedChurn));
		CHECK(std::abs(values[row*4+1]-expectedAge)<.00001 && std::abs(values[row*4+2]-expectedChurn)<.00001);
		// Porous redistribution can exceed its mean at individual pixels; the unchanged
		// envelope remains bounded by source coverage, and fresh history is exactly equal.
		CHECK(values[row*4]>=0 && values[row*4]<=1);
		CHECK(values[row*4+3]>=0 && values[row*4+3]<=channels[0]*std::exp(-elapsed/9)+.00001);
		CHECK(values[row*4]<=std::sqrt(double(values[row*4+3]))+.000002);
		if(expectedAge*24<=1.5)CHECK(values[row*4]==values[row*4+3]);
		for(unsigned c=0;c<3;++c){CHECK(std::isfinite(values[row*4+c]));CHECK(disabled[row*4+c]==0);if(!inside)CHECK(values[row*4+c]==0);}
	}
	CHECK(values[fresh*4]>.18f && values[old*4+1]==1 && values[weighted*4+1]==1); // unweighted interpolation would report .5
	CHECK(values[empty*4]==0 && values[empty*4+1]==0 && values[empty*4+2]==0);
	CHECK(values[outside*4]==0 && values[wrongLevel*4]==0 && values[border*4]==0);
	for(unsigned c=0;c<3;++c)CHECK(values[fresh*4+c]==values[repeat*4+c]);
	CHECK(values[aged*4+1]>.06f && values[expired*4]<.0000000001f);
	unsigned breakupChanged=0,remnants=0;
	for(unsigned i=0;i<16;++i) {
		const double freshCoverage=values[(evolution+i*2)*4],oldCoverage=values[(evolution+i*2+1)*4];
		if(std::abs(oldCoverage/std::exp(-12.0/9)-freshCoverage)>.01)++breakupChanged;
		if(oldCoverage<freshCoverage*.15)++remnants;
	}
	CHECK(breakupChanged>=4 && remnants>=3);
	resources.atlas->Release();resources.atlas=NULL;mirrors.Collect_Dead();CHECK(frame->Flush());
	printf("sdl_gpu_frame_selfcheck: GPU production foam history: %u samples, age %.8f/churn %.8f; density-weighted filtering, level/outside/border/disabled guards, exact pause and elapsed-time breakup (%u changed, %u fragmented) checked\n",rows,ageError,churnError,breakupChanged,remnants);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_foam_microcoverage(SdlGpuFrame *frame)
{
	// Evaluate the real production helper over large, distinct world windows. The independent
	// invariants distinguish conserved optical density from nonlinear average coverage,
	// continuous time evolution from static masks, and the exact unchanged older material.
	const unsigned width=256,height=256;
	PixelPipelineDescription description={};std::string source;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,source,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t noiseStart=source.find("float oceanHash("),noiseEnd=source.find("float3 oceanSky(");
	std::string helper=WaterFoamShading::Source();const size_t historyStart=helper.find("// Returns coverage");
	CHECK(noiseStart!=std::string::npos && noiseEnd>noiseStart && historyStart!=std::string::npos);
	if(noiseStart==std::string::npos || noiseEnd<=noiseStart || historyStart==std::string::npos)return;
	helper.resize(historyStart); // exact emitted functions; the unused history resource is unnecessary here
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string ps="cbuffer Samples : register(b0,space3) {float4 Query;float4 Basis;float4 Clock;};\n"+
		source.substr(noiseStart,noiseEnd-noiseStart)+helper+
		// Frozen M1i/M1j morphology, including the old fresh allocation and band filter. This
		// supplies independent unchanged-old and armed smooth/large/hard-patch controls.
		"float frozenBand(float2 q,float c,float fp,float size,float minimumWidth) {"
		"c=saturate(c);if(c<=0 || c>=1)return c;float ratio=max(fp,0)/size;"
		"float unresolved=smoothstep(.5,1.5,ratio);if(unresolved>=1)return c;"
		"float rank=waterFoamNoiseRank(oceanNoise(q/size));float w=clamp(ratio*.5,minimumWidth,.5);"
		"return lerp(waterFoamRankCoverage(rank,c,w),c,unresolved);}"
		"float frozenFoam(float2 p,float c,float fp,float age,float coarseSize,float minimumWidth) {"
		"c=saturate(c);if(c<=0 || c>=1)return c;float a=lerp(1,sqrt(c),smoothstep(2,8,max(age,0)));"
		"float2 coarse=float2(dot(p,float2(.8,.6)),dot(p,float2(-.6,.8)))+float2(17.31,-8.67);"
		"float2 fine=float2(dot(p,float2(.28,.96)),dot(p,float2(-.96,.28)))+float2(-31.7,23.2);"
		"return frozenBand(coarse,a,fp,coarseSize,minimumWidth)*frozenBand(fine,c/max(a,.00001),fp,.35,.01);}"
		"float sampleFoam(float2 p,float c,float fp,float age) {return waterFoamMicroCoverage(p,c,fp,age,Clock.x);}"
		"float4 main(float4 pixel:SV_Position):SV_Target0 {"
		// Mode 12 exercises the actual fresh-contact redistribution separately from
		// history and downstream microcoverage. Query.w optionally supplies compact support.
		"if(Query.z>11.5) {float2 q=(pixel.xy-.5)*2.0;"
		"float2 p=Basis.xy+float2(q.x*Basis.z-q.y*Basis.w,q.x*Basis.w+q.y*Basis.z);"
		"float c=Query.x;if(Query.w>.5 && (abs(q.x-256)>96 || abs(q.y-256)>96))c=0;"
		"return float4(waterFoamContactPatches(p,c,Query.y,Clock.x),c,"
		"waterFoamContactPatches(p,0,Query.y,Clock.x),waterFoamContactPatches(p,1,Query.y,Clock.x));}"
		// Fixture-only controls: mode 9 supplies angle/half-length/confidence directly;
		// mode 10 checks the decoder; mode 11 connects real decoded moments to fallback.
		"if(Query.z>9.5 && Query.z<10.5) {float a=Query.x;"
		"float2 axial=float2(cos(2*Clock.y),sin(2*Clock.y))*Query.y;"
		"return waterFoamRecordedDirection(float4((1+axial)*(.5*a),a*Query.w/32,a));}"
		"if(Query.z>8.5) {float2 q=(pixel.xy-.5)*2.0;"
		"float2 p=Basis.xy+float2(q.x*Basis.z-q.y*Basis.w,q.x*Basis.w+q.y*Basis.z);"
		"float iso=waterFoamHistoryRemnants(p,Query.x,Query.y,Query.w,Clock.x);"
		"float4 d=float4(cos(Clock.y),sin(Clock.y),Clock.z,Clock.w);"
		"if(Query.z>10.5) {float2 axial=float2(cos(2*Clock.y),sin(2*Clock.y))*Clock.w;"
		"d=waterFoamRecordedDirection(float4((1+axial)*(.5*Query.x),Query.x*Clock.z/32,Query.x));"
		"return float4(waterFoamHistoryStreaks(p,Query.x,Query.y,Query.w,Clock.x,d,iso),iso,d.z,d.w);}"
		"float streak=waterFoamHistoryStreaks(p,Query.x,Query.y,Query.w,Clock.x,d,iso);"
		"float reverse=waterFoamHistoryStreaks(p,Query.x,Query.y,Query.w,Clock.x,float4(-d.xy,d.zw),iso);"
		"float control=iso;if(Query.x>0 && Query.x<1 && Query.w>1.5) {float area=sqrt(Query.x);"
		"control=lerp(Query.x,waterFoamStreakTap(p,area,max(Query.y,0)*1.7,Query.w,Clock.x)"
		"*(Query.x/area),smoothstep(1.5,6,Query.w));}return float4(streak,reverse,iso,control);}"
		"if(Query.z>7.5) {float2 q=(pixel.xy-.5)*2.0;"
		"float2 p=Basis.xy+float2(q.x*Basis.z-q.y*Basis.w,q.x*Basis.w+q.y*Basis.z);"
		"float c=waterFoamHistoryRemnants(p,Query.x,Query.y,Query.w,Clock.x);"
		"return float4(c,waterFoamMicroCoverage(p,c,Query.y,Query.w,Clock.x),"
		"waterFoamMicroCoverage(p,Query.x,Query.y,Query.w,Clock.x),Query.x);}"
		"if(Query.z>5.5) {float c=Query.z>6.5?Query.x:(pixel.y-.5)/255.0,r=pixel.x/256.0;"
		"if(c<=0 || c>=1)return float4(c,c,c,c);float d=waterFoamOpticalDepth(c),m=lerp(.15,1.85,r);"
		"return float4(d*m,waterFoamOpticalCoverage(d*m),waterFoamFreshMean(d),c);}"
		"if(Query.z>0.5 && Query.z<1.5) {float c=(pixel.y-.5)/255.0,r= pixel.x/256.0;"
		"return float4(waterFoamRankCoverage(r,c,Query.y),0,0,1);}"
		"float2 q=(pixel.xy-.5)*2.0;"
		"float2 p=Basis.xy+float2(q.x*Basis.z-q.y*Basis.w,q.x*Basis.w+q.y*Basis.z);"
		"if(Query.z>4.5) return float4(sampleFoam(p,Query.x,Query.y,0),"
		"frozenFoam(p,Query.x,Query.y,0,1,.08),"
		"sampleFoam(p,Query.x,Query.y,Query.w),frozenFoam(p,Query.x,Query.y,Query.w,1,.08));"
		"if(Query.z>3.5) return float4(sampleFoam(p,Query.x,Query.y,8),"
		"frozenFoam(p,Query.x,Query.y,8,1,.01),"
		"waterFoamCoverageBand(p,Query.x,Query.y,1,.08),frozenBand(p,Query.x,Query.y,1,.01));"
		"if(Query.z>2.5) return float4(sampleFoam(p,Query.x,Query.y,8),"
		"frozenFoam(p,Query.x,Query.y,8,2.8,.01),frozenFoam(p,Query.x,Query.y,0,1,.08),1);"
		"if(Query.z>1.5) return float4(waterFoamMicroCoverage(p,Query.x,Query.y),"
		"sampleFoam(p,Query.x,Query.y,2),"
		"sampleFoam(p,Query.x,Query.y,Query.w),frozenFoam(p,Query.x,Query.y,Query.w,1,.08));"
		"return float4(sampleFoam(p,Query.x,Query.y,Query.w),"
		"sampleFoam(p,Query.x*.25,Query.y,Query.w),"
		"sampleFoam(p,0,Query.y,Query.w),sampleFoam(p,1,Query.y,Query.w));}";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;SDL_GPUTexture *target=NULL;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} resources;resources.device=frame->Device();
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("foam microcoverage PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0 && buffers==1);
	if(samplers!=0 || buffers!=1)return;
	resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vs;pi.fragment_shader=resources.ps;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=width;ti.height=height;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&](float coverage,float footprint,float age=0,float x=0,float y=0,float angle=0,unsigned mode=0,float step=2,float time=0,float directionAngle=0,float halfLength=0,float confidence=0) {
		const float constants[12]={coverage,footprint,float(mode),age,x,y,cosf(angle)*step*.5f,sinf(angle)*step*.5f,time,directionAngle,halfLength,confidence};
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
		SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);SDL_PushGPUFragmentUniformData(commands,0,constants,sizeof(constants));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
		return read_float_targets(frame,&resources.target,1,width,height);
	};
	auto meanFresh=[](double coverage) {
		if(coverage<=0 || coverage>=1)return coverage;
		const double d=-std::log1p(-coverage);
		return 1-std::exp(-.15*d)*(-std::expm1(-1.7*d))/(1.7*d);
	};
	auto expectedMean=[&](double coverage,double age) {
		const double t=(std::max)(0.0,(std::min)(1.0,(age-2)/6));
		const double aged=t*t*(3-2*t);
		return meanFresh(coverage)*(1-aged)+coverage*aged;
	};
	double maximumMeanError=0;unsigned gradedFresh=0;std::vector<float> reference;
	for(float age:{0.f,2.f,5.f,8.f,16.f})for(float footprint:{.04f,.4f,4.f})for(float coverage:{.02f,.2f,.5f,.8f}) {
		const auto values=render(coverage,footprint,age);CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		double sum[2]={};bool bounded=true,nested=true,endpoints=true;
		for(unsigned i=0;i<width*height;++i) {
			for(unsigned c=0;c<2;++c){const float v=values[i*4+c];bounded &= std::isfinite(v) && v>=0 && v<=1;sum[c]+=v;}
			nested &= values[i*4+1]<=values[i*4]+.000001f;
			endpoints &= values[i*4+2]==0 && values[i*4+3]==1;
			if(footprint==4.f)nested &= std::abs(values[i*4]-expectedMean(coverage,age))<.000002 && std::abs(values[i*4+1]-expectedMean(coverage*.25f,age))<.000002;
		}
		CHECK(bounded && nested && endpoints);
		for(unsigned c=0;c<2;++c) {
			const double mean=sum[c]/(width*height),expected=expectedMean(coverage*(c?.25:1),age);
			maximumMeanError=(std::max)(maximumMeanError,std::abs(mean-expected));
			if(std::abs(mean-expected)>=.003+expected*.025)
				printf("foam mean: age %.3f footprint %.3f coverage %.4f measured %.7f expected %.7f\n",age,footprint,coverage,mean,expected);
			CHECK(std::abs(mean-expected)<.003+expected*.025);
		}
		if(age==0 && coverage==.5f && footprint==.04f) {
			reference=values;
			for(unsigned i=0;i<width*height;++i)gradedFresh+=values[i*4]>.05f && values[i*4]<.95f;
		}
	}
	CHECK(gradedFresh==width*height); // no nearly opaque/empty ranked islands at c=.5
	CHECK(render(.5f,.04f)==reference); // world-fixed, stationary/paused evaluation is exact
	CHECK(render(.5f,.04f,2)==reference); // fresh allocation stays stable before the age transition
	const auto panned=render(.5f,.04f,0,2,0),rotated=render(.5f,.04f,8,2113,-192,1.13f);
	bool fixedWorld=true;double rotatedMean=0;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		rotatedMean+=rotated[(y*width+x)*4];
		if(x+1<width)fixedWorld &= panned[(y*width+x)*4]==reference[(y*width+x+1)*4];
	}
	CHECK(fixedWorld && std::abs(rotatedMean/(width*height)-.5)<.025);
	const auto aged=render(.5f,.04f,8),agedPaused=render(.5f,.04f,8),agedLater=render(.5f,.04f,16);
	CHECK(aged==agedPaused && aged==agedLater);
	const auto agedPan=render(.5f,.04f,8,2,0);
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		if(x+1<width)CHECK(agedPan[at]==aged[at+4]);
	}
	for(float coverage:{.02f,.2f,.5f,.8f})for(float footprint:{.04f,.4f,1.5f})for(float age:{8.f,16.f}) {
		const auto oldControl=render(coverage,footprint,age,0,0,0,2);bool exactAgeEndpoints=true;
		float freshDifference=0,oldDifference=0;
		for(unsigned i=0;i<width*height;++i) {
			exactAgeEndpoints &= oldControl[i*4]==oldControl[i*4+1] && oldControl[i*4+2]==oldControl[i*4+3];
			freshDifference=fmaxf(freshDifference,fabsf(oldControl[i*4]-oldControl[i*4+1]));
			oldDifference=fmaxf(oldDifference,fabsf(oldControl[i*4+2]-oldControl[i*4+3]));
		}
		if(!exactAgeEndpoints)printf("foam endpoint: c %.9g footprint %.9g age %.9g fresh %.9g old %.9g\n",coverage,footprint,age,freshDifference,oldDifference);
		CHECK(exactAgeEndpoints); // fresh overload agrees at time zero; old ages exactly retain frozen M1j
	}
	const auto close=render(.5f,.0401f),almostFar=render(.5f,1.499f,8),far=render(.5f,1.5f,8);
	float nearChange=0,farChange=0;
	for(unsigned i=0;i<width*height;++i) {
		nearChange=fmaxf(nearChange,fabsf(close[i*4]-reference[i*4]));
		farChange=fmaxf(farChange,fabsf(almostFar[i*4]-far[i*4]));
	}
	CHECK(nearChange<.002f && farChange<.000002f);
	float maximumAgeChange=0,ageGateChange=0;
	for(float age:{2.f,5.f,8.f}) {
		const auto before=render(.5f,.04f,age-.001f),after=render(.5f,.04f,age+.001f);
		for(unsigned i=0;i<width*height;++i) {
			const float change=fabsf(before[i*4]-after[i*4]);maximumAgeChange=fmaxf(maximumAgeChange,change);
			if(age!=5)ageGateChange=fmaxf(ageGateChange,change);
		}
	}
	CHECK(maximumAgeChange<.01f && ageGateChange<.00001f);
	for(float coverage:{.000001f,.99f,.999f,.9999f,std::nextafter(1.f,0.f)}) {
		const auto values=render(coverage,0),filtered=render(coverage,4);bool endpointLimit=true;
		const double d=-std::log1p(-double(coverage));
		const double lower=-std::expm1(-.15*d),upper=-std::expm1(-1.85*d);
		for(unsigned i=0;i<width*height;++i) {
			endpointLimit &= values[i*4]>=lower-.000002 && values[i*4]<=upper+.000002;
			endpointLimit &= std::abs(filtered[i*4]-meanFresh(coverage))<.000002;
		}
		CHECK(endpointLimit); // explicitly retain the nonlinear, slow high-density limit
	}

	// Frozen M1j fresh pores are unresolved here, but one-world-unit old remnants remain resolved. The
	// frozen M1i field has identical coverage and a 2.8-world-unit coarse scale. A normalized
	// variogram measures actual connected-patch size independently of brightness/mean: at a
	// half-world separation the smaller new remnants must decorrelate faster than that control.
	const auto shape=render(.5f,.55f,0,0,0,0,3,.0625f);
	double shapeMean[3]={},shapeVariance[3]={},shapeDifference[2]={};unsigned oldHoles=0,oldBodies=0,shapePairs=0;
	for(unsigned i=0;i<width*height;++i)for(unsigned channel=0;channel<3;++channel)shapeMean[channel]+=shape[i*4+channel]/double(width*height);
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		for(unsigned channel=0;channel<3;++channel) {
			const double centered=shape[at+channel]-shapeMean[channel];shapeVariance[channel]+=centered*centered/(width*height);
		}
		oldHoles+=shape[at]<.25f;oldBodies+=shape[at]>.65f;
		if(x+8<width && y+8<height) {
			for(unsigned channel=0;channel<2;++channel) {
				const double dx=shape[at+8*4+channel]-shape[at+channel],dy=shape[at+8*width*4+channel]-shape[at+channel];
				shapeDifference[channel]+=dx*dx+dy*dy;
			}
			++shapePairs;
		}
	}
	double shapeVariogram[2]={};for(unsigned c=0;c<2;++c)shapeVariogram[c]=shapeDifference[c]/(4*shapePairs*(std::max)(shapeVariance[c],1e-12));
	CHECK(shapeVariance[2]<.000000001 && shapeVariance[0]>.01 && oldHoles>100 && oldBodies>100);
	CHECK(shapeVariogram[0]>shapeVariogram[1]*2); // armed old large-patch control
	// Compare the same world positions, scale, coverage and footprint against a hard-edged
	// control. More transition pixels and less normalized edge energy prove softness without
	// relying on mean opacity, a larger remnant size, or averaging over distant pixels. The
	// full two-band composition is checked as well as the isolated old-remnant boundary.
	const auto soft=render(.5f,.01f,0,0,0,0,4,.03125f);
	double softMean[4]={},softVariance[4]={},softDifference[4]={};unsigned transition[4]={},softPairs=0;
	for(unsigned i=0;i<width*height;++i)for(unsigned c=0;c<4;++c)softMean[c]+=soft[i*4+c]/double(width*height);
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		for(unsigned c=0;c<4;++c) {
			const double value=soft[at+c],centered=value-softMean[c];softVariance[c]+=centered*centered/(width*height);
			transition[c]+=value>.05 && value<.95;
			if(x+1<width && y+1<height) {
				const double dx=soft[at+4+c]-value,dy=soft[at+width*4+c]-value;softDifference[c]+=dx*dx+dy*dy;
			}
		}
		if(x+1<width && y+1<height)++softPairs;
	}
	double softEnergy[4]={};for(unsigned c=0;c<4;++c)softEnergy[c]=softDifference[c]/(4*softPairs*(std::max)(softVariance[c],1e-12));
	CHECK(softVariance[0]>.05 && softVariance[2]>.05);
	CHECK(transition[2]>transition[3]*4 && softEnergy[2]<softEnergy[3]*.6);
	CHECK(transition[0]>transition[1] && softEnergy[0]<softEnergy[1]); // production call must select the softer coarse filter
	// Independent uniform-rank quadrature proves density conservation and the different
	// coverage mean, including the stable near-zero branch. This control is not calibrated
	// against the actual procedural noise or the implementation's mean helper.
	const auto quadrature=render(0,0,0,0,0,0,6);
	double densityError=0,coverageQuadratureError=0,meanFormulaError=0;
	for(unsigned y=1;y<height-1;++y) {
		// The shader may lower division to multiplication by a float reciprocal. Near one,
		// that input rounding is amplified by -log(1-c); use its explicit input channel,
		// not the output density, for an independent double-precision reference.
		const double c=quadrature[y*width*4+3],d=-std::log1p(-c);double densitySum=0,coverageSum=0;
		CHECK(std::abs(c-double(y)/(height-1))<.00000012);
		for(unsigned x=0;x<width;++x) {
			const size_t at=(y*width+x)*4;const double multiplier=.15+1.7*(x+.5)/width;
			densityError=(std::max)(densityError,std::abs(quadrature[at]-d*multiplier));
			coverageQuadratureError=(std::max)(coverageQuadratureError,std::abs(quadrature[at+1]+std::expm1(-d*multiplier)));
			meanFormulaError=(std::max)(meanFormulaError,std::abs(quadrature[at+2]-meanFresh(c)));
			densitySum+=quadrature[at];coverageSum+=quadrature[at+1];
		}
		if(std::abs(densitySum/width-d)>=.000003)printf("foam density row: y %u c %.12g mean %.12g expected %.12g\n",y,c,densitySum/width,d);
		CHECK(std::abs(densitySum/width-d)<.000003);
		CHECK(std::abs(coverageSum/width-meanFresh(c))<.000005);
		CHECK(coverageSum/width<c); // Jensen: averaging density first would fail this control
	}
	CHECK(densityError<.000006 && coverageQuadratureError<.000002 && meanFormulaError<.000002);
	for(float coverage:{.00000001f,.000001f,.0001f,.001f,.00999f,.01001f,.04876f,.04878f}) {
		const auto values=render(coverage,0,0,0,0,0,7);
		const double d=-std::log1p(-double(coverage)),mean=meanFresh(coverage);
		bool accurateSmallDensity=true;double relativeErrors[3]={};
		for(unsigned x=0;x<width;++x) {
			const double multiplier=.15+1.7*(x+.5)/width;
			const double expected=-std::expm1(-d*multiplier);
			accurateSmallDensity &= std::abs(values[x*4]-d*multiplier)<(std::max)(1e-11,d*multiplier*2e-5);
			accurateSmallDensity &= std::abs(values[x*4+1]-expected)<(std::max)(1e-11,expected*2e-5);
			accurateSmallDensity &= std::abs(values[x*4+2]-mean)<(std::max)(1e-11,mean*2e-5);
			relativeErrors[0]=(std::max)(relativeErrors[0],std::abs(values[x*4]-d*multiplier)/(d*multiplier));
			relativeErrors[1]=(std::max)(relativeErrors[1],std::abs(values[x*4+1]-expected)/expected);
			relativeErrors[2]=(std::max)(relativeErrors[2],std::abs(values[x*4+2]-mean)/mean);
		}
		if(!accurateSmallDensity)printf("foam small density: c %.12g first %.12g/%.12g/%.12g relative %.9g/%.9g/%.9g\n",coverage,values[0],values[1],values[2],relativeErrors[0],relativeErrors[1],relativeErrors[2]);
		CHECK(accurateSmallDensity); // zero/cancellation cannot pass on faint trail edges
	}
	// Continuous material interiors and time evolution are both required. Use a close,
	// contiguous world window, then compare a tiny time step and a substantial elapsed time.
	// Pause and camera translation reproduce exactly the same world samples at that time.
	const auto freshShape=render(.8f,.4f,8,0,0,0,5,.0625f,3);
	const auto freshSoon=render(.8f,.4f,8,0,0,0,5,.0625f,3.001f);
	const auto freshLater=render(.8f,.4f,8,0,0,0,5,.0625f,4);
	CHECK(freshShape==render(.8f,.4f,8,0,0,0,5,.0625f,3));
	const auto freshPan=render(.8f,.4f,8,.0625f,0,0,5,.0625f,3);
	double freshMean=0,temporalRms=0;float tinyTimeChange=0;unsigned gradedInterior=0;bool exactOldShape=true,freshWorld=true;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;const double value=freshShape[at];freshMean+=value/(width*height);
		gradedInterior+=value>.25 && value<.93;
		tinyTimeChange=fmaxf(tinyTimeChange,fabsf(freshSoon[at]-value));
		const double dt=freshLater[at]-value;temporalRms+=dt*dt/(width*height);
		exactOldShape &= freshShape[at+2]==freshShape[at+3] && freshLater[at+2]==freshShape[at+2];
		if(x+1<width)freshWorld &= freshPan[at]==freshShape[at+4];
	}
	temporalRms=std::sqrt(temporalRms);
	CHECK(exactOldShape && freshWorld && gradedInterior>width*height/2);
	CHECK(tinyTimeChange<.002 && temporalRms>.005 && temporalRms<.2);
	const auto freshFar=render(.8f,4,0,0,0,0,0,.0625f,3);
	const auto freshFarLater=render(.8f,4,0,0,0,0,0,.0625f,4);
	CHECK(freshFar==freshFarLater); // filtered coverage mean, independent of unresolved phase
	const auto filterBefore=render(.8f,6.f/1.7f-.001f),filterAfter=render(.8f,6.f/1.7f+.001f);
	float freshFilterChange=0;
	for(unsigned i=0;i<width*height;++i)freshFilterChange=fmaxf(freshFilterChange,fabsf(filterBefore[i*4]-filterAfter[i*4]));
	CHECK(freshFilterChange<.000002);

	// Independent quadrature over uniform ranks: each row must have its requested mean at all
	// filter widths and be monotone, so a folded/periodic outlining interval fails this control.
	double rankAreaError=0;
	for(float filterWidth:{.02f,.1125f,.3f,.5f}) {
		const auto values=render(0,filterWidth,0,0,0,0,1);bool monotone=true;
		for(unsigned y=0;y<height;++y) {
			double sum=0;for(unsigned x=0;x<width;++x) {
				const float v=values[(y*width+x)*4];sum+=v;
				if(x>0)monotone &= v<=values[(y*width+x-1)*4]+.000001f;
				if(y==0 || y==height-1)monotone &= v==float(y/(height-1));
			}
			rankAreaError=(std::max)(rankAreaError,std::abs(sum/width-double(y)/(height-1)));
		}
		CHECK(monotone);
	}
	CHECK(rankAreaError<.0002);
	// The history-only redistribution targets a local mean, not a pointwise opacity cap.
	// The preceding uniform-rank quadrature is an independent area oracle for each band.
	// Here the actual production noise and full microcoverage composition must be armed at
	// RTS footprints, remain bounded, and converge to the previous material when unresolved.
	double historyMeanError=0,historyFinalMeanError=0,historyVariance=0,historyTemporal=0;
	unsigned historyHoles=0,historyBright=0;
	for(float age:{1.f,6.f,12.f})for(float footprint:{1.f,3.f})for(float coverage:{.2f,.5f,.8f}) {
		const auto values=render(coverage,footprint,age,0,0,0,8,2,3);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		double mean=0,composed=0;bool finite=true,fresh=true;
		for(unsigned i=0;i<width*height;++i) {
			const double v=values[i*4];mean+=v/(width*height);composed+=values[i*4+1]/double(width*height);
			CHECK(v<=std::sqrt(double(coverage))+.000001);
			for(unsigned c=0;c<4;++c)finite &= std::isfinite(values[i*4+c]) && values[i*4+c]>=0 && values[i*4+c]<=1;
			if(age<=1.5f)fresh &= values[i*4]==coverage && values[i*4+1]==values[i*4+2];
			if(age==12 && footprint==3 && coverage==.5f) {
				historyVariance+=(v-.5)*(v-.5)/(width*height);
				historyHoles+=v<.2;historyBright+=v>.6;
			}
		}
		CHECK(finite && fresh);historyMeanError=(std::max)(historyMeanError,std::abs(mean-coverage));
		// A spatial CDF is calibrated, not an exact per-window integral. This bound applies
		// to the 512x512-world test window; it does not assert exact conservation in a trail.
		CHECK(std::abs(mean-coverage)<.025);
		if(age==12 && footprint==3) {
			historyFinalMeanError=(std::max)(historyFinalMeanError,std::abs(composed-coverage));
			CHECK(std::abs(composed-coverage)<.025);
		}
	}
	CHECK(historyVariance>.012 && historyHoles>width*height/20 && historyBright>width*height/20);
	const auto history=render(.5f,3,12,0,0,0,8,2,3),historyPause=render(.5f,3,12,0,0,0,8,2,3);
	const auto historyPan=render(.5f,3,12,2,0,0,8,2,3),historyLater=render(.5f,3,12,0,0,0,8,2,5);
	const auto historySoon=render(.5f,3,12,0,0,0,8,2,3.0001f);
	const auto noHistory=render(0,3,12,0,0,0,8,2,3),fullHistory=render(1,3,12,0,0,0,8,2,3);
	const auto historyFar=render(.5f,12,12,0,0,0,8,2,3),historyFarLater=render(.5f,12,12,0,0,0,8,2,5);
	const auto historyAgeBefore=render(.5f,3,1.499f,0,0,0,8,2,3),historyAgeAfter=render(.5f,3,1.501f,0,0,0,8,2,3);
	const auto historyFilterBefore=render(.5f,18.f/1.7f-.001f,12,0,0,0,8,2,3);
	const auto historyFilterAfter=render(.5f,18.f/1.7f+.001f,12,0,0,0,8,2,3);
	for(const auto *v:{&history,&historyPause,&historyPan,&historyLater,&historySoon,&noHistory,&fullHistory,&historyFar,&historyFarLater,&historyAgeBefore,&historyAgeAfter,&historyFilterBefore,&historyFilterAfter}) {
		CHECK(v->size()==width*height*4);if(v->size()!=width*height*4)return;
	}
	CHECK(history==historyPause && historyFar==historyFarLater);
	bool historyWorld=true,historyEndpoints=true;float historyStep=0,historyAgeStep=0,historyFilterStep=0;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t i=(y*width+x)*4;
		if(x+1<width)historyWorld &= historyPan[i]==history[i+4];
		historyEndpoints &= noHistory[i]==0 && noHistory[i+1]==0 && fullHistory[i]==1 && fullHistory[i+1]==1;
		historyEndpoints &= historyFar[i]==.5f && historyFar[i+1]==historyFar[i+2];
		historyTemporal+=(historyLater[i]-history[i])*(historyLater[i]-history[i])/double(width*height);
		historyStep=fmaxf(historyStep,fabsf(historySoon[i]-history[i]));
		historyAgeStep=fmaxf(historyAgeStep,fabsf(historyAgeBefore[i]-historyAgeAfter[i]));
		historyFilterStep=fmaxf(historyFilterStep,fabsf(historyFilterBefore[i]-historyFilterAfter[i]));
	}
	historyTemporal=std::sqrt(historyTemporal);
	CHECK(historyWorld && historyEndpoints && historyStep<.002f && historyAgeStep<.00001f && historyFilterStep<.00001f);
	CHECK(historyTemporal>.005 && historyTemporal<.4);
	printf("sdl_gpu_frame_selfcheck: history remnant material mean error %.7f/final %.7f, RTS variance %.7f holes %u bright %u, temporal RMS %.7f, small-time/age/filter %.7f/%.7f/%.7f; exact fresh/empty/full/pause/world-pan/unresolved controls checked\n",historyMeanError,historyFinalMeanError,historyVariance,historyHoles,historyBright,historyTemporal,historyStep,historyAgeStep,historyFilterStep);

	// Retained axial moments orient coverage convolution, not the procedural coordinates.
	// Decoder cases use an independent double-precision axial tensor/confidence oracle;
	// compare the tensor rather than its arbitrary eigenvector sign near the pi seam.
	auto smoothUnit=[](double lo,double hi,double x) {
		const double t=(std::max)(0.0,(std::min)(1.0,(x-lo)/(hi-lo)));return t*t*(3-2*t);
	};
	struct DirectionCase {float opacity,coherence,angle,initialWidth;};
	const DirectionCase directionCases[]={
		{0,1,0,6},{.00005f,1,0,6},{.01f,1,.7f,6},{.02f,1,.7f,6},
		{.05f,1,.7f,6},{.08f,1,.7f,6},{.5f,0,.7f,6},{.5f,.19f,.7f,6},
		{.5f,.4f,.7f,6},{.5f,.65f,.7f,6},{.5f,1,0,0},{.5f,1,0,3},
		{.5f,1,1.5707963f,6},{.5f,1,-1.5707963f,12},
		{.5f,1,.7f,24},{.5f,1,3.8415927f,32},{.05f,.4f,-.7f,9}};
	double directionError=0;
	for(const auto &q:directionCases) {
		const auto values=render(q.opacity,q.coherence,q.initialWidth,0,0,0,10,2,3,q.angle);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		const double confidence=q.opacity<=.0001f?0:
			smoothUnit(.20,.65,q.coherence)*smoothUnit(.02,.08,q.opacity);
		const double expectedLength=(std::max)(3.0,(std::min)(12.0,double(q.initialWidth)));
		bool constant=true;
		for(unsigned i=0;i<width*height;++i)for(unsigned c=0;c<4;++c)
			constant &= std::isfinite(values[i*4+c]) && values[i*4+c]==values[c];
		CHECK(constant);
		if(confidence==0) {
			CHECK(values[0]==0 && values[1]==0 && values[2]==0 && values[3]==0);
		} else {
			const double x=values[0],y=values[1];
			directionError=(std::max)(directionError,std::abs(x*x+y*y-1));
			directionError=(std::max)(directionError,std::abs(x*x-y*y-std::cos(2*double(q.angle))));
			directionError=(std::max)(directionError,std::abs(2*x*y-std::sin(2*double(q.angle))));
			directionError=(std::max)(directionError,std::abs(values[2]-expectedLength));
			directionError=(std::max)(directionError,std::abs(values[3]-confidence));
		}
	}
	CHECK(directionError<.00002);
	// Each positive normalized convolution preserves the band's expected coverage. These
	// finite windows are statistical checks, not exact mass conservation for a real trail
	// with spatially varying density, age or direction. Existing rank quadrature is the
	// independent area oracle; the zero-offset fourth channel is an armed spatial control.
	double streakMeanError=0,streakSignError=0;
	for(float coverage:{.2f,.5f,.8f})for(float footprint:{1.f,3.f})for(float length:{3.f,12.f}) {
		const auto values=render(coverage,footprint,12,0,0,0,9,2,3,.7f,length,1);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		double sums[4]={};bool bounded=true;
		for(unsigned i=0;i<width*height;++i)for(unsigned c=0;c<4;++c) {
			const double v=values[i*4+c];sums[c]+=v/(width*height);
			bounded &= std::isfinite(v) && v>=0 && v<=std::sqrt(double(coverage))+.000001;
			streakSignError=(std::max)(streakSignError,std::abs(double(values[i*4])-values[i*4+1]));
		}
		CHECK(bounded);
		for(unsigned c=0;c<4;++c)streakMeanError=(std::max)(streakMeanError,std::abs(sums[c]-coverage));
	}
	CHECK(streakMeanError<.025 && streakSignError==0);
	// Low opacity and incoherent crossing moments must exercise the actual decoder and
	// return the unmodified isotropic material, not invent a direction from a near-zero axis.
	for(float opacity:{.01f,.5f})for(float coherence:{0.f,.19f}) {
		const auto values=render(opacity,3,12,0,0,0,11,2,3,.7f,12,coherence);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		bool exact=true;for(unsigned i=0;i<width*height;++i)
			exact &= values[i*4]==values[i*4+1] && values[i*4+2]==0 && values[i*4+3]==0;
		CHECK(exact);
	}
	const auto lowOpacity=render(.01f,3,12,0,0,0,11,2,3,.7f,12,1);
	CHECK(lowOpacity.size()==width*height*4);if(lowOpacity.size()!=width*height*4)return;
	for(unsigned i=0;i<width*height;++i)CHECK(lowOpacity[i*4]==lowOpacity[i*4+1] && lowOpacity[i*4+3]==0);
	const auto activeDecoded=render(.5f,3,12,0,0,0,11,2,3,.7f,12,1);
	const auto partlyDecoded=render(.5f,3,12,0,0,0,11,2,3,.7f,12,.4f);
	const auto directAxis=render(.5f,3,12,0,0,0,9,2,3,.7f,12,1);
	for(const auto *v:{&activeDecoded,&partlyDecoded,&directAxis}) {
		CHECK(v->size()==width*height*4);if(v->size()!=width*height*4)return;
	}
	double decodedCompositionError=0;
	const double partialConfidence=smoothUnit(.20,.65,double(.4f));
	for(unsigned i=0;i<width*height;++i) {
		const size_t at=i*4;const double expected=directAxis[at+2]+partialConfidence*(directAxis[at]-directAxis[at+2]);
		decodedCompositionError=(std::max)(decodedCompositionError,std::abs(double(activeDecoded[at])-directAxis[at]));
		decodedCompositionError=(std::max)(decodedCompositionError,std::abs(double(partlyDecoded[at])-expected));
		CHECK(activeDecoded[at+2]==12 && activeDecoded[at+3]==1);
	}
	CHECK(decodedCompositionError<.0001); // only decoder/trig roundoff may separate the paths
	// Correlation is measured at a four-world-unit separation with the sampling grid
	// aligned to each chosen axis. Comparing against the SAME 4/8-world unconvolved field
	// prevents the older isotropic helper's different 6/12 scales from faking this result.
	double minimumCorrelationGain=1e30,minimumAlongAdvantage=1e30,minimumStreakVariance=1e30;
	for(float angle:{0.f,.7f,1.5707963f}) {
		const auto values=render(.5f,3,12,0,0,angle,9,2,3,angle,12,1);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		double means[2]={},variances[2]={},along[2]={},across[2]={};unsigned pairs=0;
		for(unsigned i=0;i<width*height;++i)for(unsigned c=0;c<2;++c)means[c]+=values[i*4+c*3]/double(width*height);
		for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
			const size_t at=(y*width+x)*4;
			for(unsigned c=0;c<2;++c) {
				const double v=values[at+c*3],centered=v-means[c];variances[c]+=centered*centered/(width*height);
				if(x+2<width && y+2<height) {
					const double dx=values[at+8+c*3]-v,dy=values[at+width*8+c*3]-v;
					along[c]+=dx*dx;across[c]+=dy*dy;
				}
			}
			if(x+2<width && y+2<height)++pairs;
		}
		CHECK(pairs>0 && variances[0]>.005 && variances[1]>.02 && along[0]>0 && along[1]>0 && across[1]>0);
		const double gain=(across[0]/along[0])/(across[1]/along[1]);
		const double advantage=(across[0]-along[0])/(2*pairs*variances[0]);
		minimumCorrelationGain=(std::min)(minimumCorrelationGain,gain);
		minimumAlongAdvantage=(std::min)(minimumAlongAdvantage,advantage);
		minimumStreakVariance=(std::min)(minimumStreakVariance,variances[0]);
		CHECK(gain>2 && advantage>.15); // scalar blur or the unconvolved control cannot pass
	}
	const auto streak=render(.5f,3,12,0,0,0,9,2,3,.7f,12,1);
	const auto streakPause=render(.5f,3,12,0,0,0,9,2,3,.7f,12,1);
	const auto streakPan=render(.5f,3,12,2,0,0,9,2,3,.7f,12,1);
	const auto streakTurn=render(.5f,3,12,0,0,0,9,2,3,.7001f,12,1);
	const auto streakCross=render(.5f,3,12,0,0,0,9,2,3,2.2707963f,12,1);
	const auto streakFallback=render(.5f,3,12,0,0,0,9,2,3,.7f,12,0);
	const auto streakHalf=render(.5f,3,12,0,0,0,9,2,3,.7f,12,.5f);
	for(const auto *v:{&streak,&streakPause,&streakPan,&streakTurn,&streakCross,&streakFallback,&streakHalf}) {
		CHECK(v->size()==width*height*4);if(v->size()!=width*height*4)return;
	}
	CHECK(streak==streakPause);
	bool streakWorld=true,exactFallback=true;float smallTurn=0,confidenceError=0;double crossTurnRms=0;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		if(x+1<width)streakWorld &= streakPan[at]==streak[at+4];
		exactFallback &= streakFallback[at]==streakFallback[at+2];
		smallTurn=fmaxf(smallTurn,fabsf(streakTurn[at]-streak[at]));
		confidenceError=fmaxf(confidenceError,fabsf(streakHalf[at]-(streak[at]+streak[at+2])*.5f));
		const double d=streakCross[at]-streak[at];crossTurnRms+=d*d/(width*height);
	}
	crossTurnRms=std::sqrt(crossTurnRms);
	CHECK(streakWorld && exactFallback && smallTurn<.002f && confidenceError<.000001f && crossTurnRms>.02);
	// The early endpoints must call the actual production function and remain exact.
	for(float coverage:{0.f,.2f,.5f,1.f})for(float age:{0.f,1.5f,12.f}) {
		if(age>1.5f && coverage>0 && coverage<1)continue;
		const auto values=render(coverage,3,age,0,0,0,9,2,3,.7f,12,1);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		bool exact=true;for(unsigned i=0;i<width*height;++i)for(unsigned c=0;c<4;++c)exact &= values[i*4+c]==coverage;
		CHECK(exact);
	}
	const auto streakFar=render(.5f,12,12,0,0,0,9,2,3,.7f,12,1);
	const auto streakFarMoved=render(.5f,12,12,137,-219,0,9,2,5,1.1f,3,1);
	CHECK(streakFar.size()==width*height*4 && streakFarMoved.size()==width*height*4);
	if(streakFar.size()!=width*height*4 || streakFarMoved.size()!=width*height*4)return;
	CHECK(streakFar==streakFarMoved);
	float unresolvedError=0;for(float v:streakFar)unresolvedError=fmaxf(unresolvedError,fabsf(v-.5f));
	CHECK(unresolvedError<.000001f);
	printf("sdl_gpu_frame_selfcheck: recorded streak material decoder %.8f/composition %.8f, mean %.7f, exact sign error %.8f; minimum axial correlation gain %.5f / along advantage %.5f / variance %.7f; small-angle %.7f, quarter-turn RMS %.7f, confidence %.8f, unresolved %.8f; exact low-coherence/fresh/pause/world-pan controls checked\n",directionError,decodedCompositionError,streakMeanError,streakSignError,minimumCorrelationGain,minimumAlongAdvantage,minimumStreakVariance,smallTurn,crossTurnRms,confidenceError,unresolvedError);

	// Fresh contact: verify statistical redistribution, not a particular noise formula.
	// The earlier uniform-rank quadrature independently checks the rank-area integral.
	// These constant-source windows cannot establish conservation for a correlated,
	// spatially varying native contact source, nor replace its visual acceptance.
	const float contactWindows[][3]={{0,0,0},{2113,-192,1.13f},{-811,1201,.37f}};
	double contactMeanError=0,contactMinimumVariance=1e30,contactMinimumScaleRatio=1e30;
	unsigned contactMinimumGaps=width*height,contactMinimumClusters=width*height;
	std::vector<float> contactReference;
	for(float footprint:{1.f,3.f})for(float coverage:{.2f,.5f,.8f}) {
		if(footprint==3 && coverage!=.5f)continue;
		for(const auto &window:contactWindows) {
			const auto values=render(coverage,footprint,0,window[0],window[1],window[2],12,2,3);
			CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
			const double ceiling=1;double sum=0;bool bounded=true,endpoints=true;
			for(unsigned i=0;i<width*height;++i) {
				const double v=values[i*4];sum+=v;
				bounded &= std::isfinite(v) && v>=0 && v<=ceiling+.000001;
				endpoints &= values[i*4+1]==coverage && values[i*4+2]==0 && values[i*4+3]==1;
			}
			CHECK(bounded && endpoints);
			const double mean=sum/(width*height),error=std::abs(mean-coverage);
			contactMeanError=(std::max)(contactMeanError,error);
			if(error>=.025)printf("contact patch mean: c %.3f fp %.3f window %.1f %.1f angle %.3f measured %.7f\n",coverage,footprint,window[0],window[1],window[2],mean);
			CHECK(error<.025);
			if(footprint==1 && coverage==.5f) {
				double variance=0,nearEnergy=0,farEnergy=0;unsigned gaps=0,clusters=0;
				for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
					const size_t at=(y*width+x)*4;const double v=values[at],d=v-mean;
					// A dilute film remains between clusters; test contrast around the
					// supplied mean rather than requiring opaque/empty binary islands.
					variance+=d*d/(width*height);gaps+=v<.30;clusters+=v>.65;
					if(x+8<width) {const double near=values[at+4]-v,far=values[at+32]-v;
						nearEnergy+=near*near;farEnergy+=far*far;}
				}
				CHECK(variance>.015 && gaps>width*height/100 && clusters>width*height/20);
				CHECK(nearEnergy>0 && farEnergy>1.5*nearEnergy);
				contactMinimumVariance=(std::min)(contactMinimumVariance,variance);
				if(nearEnergy>0)contactMinimumScaleRatio=(std::min)(contactMinimumScaleRatio,farEnergy/nearEnergy);
				contactMinimumGaps=(std::min)(contactMinimumGaps,gaps);
				contactMinimumClusters=(std::min)(contactMinimumClusters,clusters);
				if(window[0]==0 && window[1]==0)contactReference=values;
			}
		}
	}
	const auto contactFrozen=render(.5f,1,0,0,0,0,12,2,3);
	const auto contactTinyTime=render(.5f,1,0,0,0,0,12,2,3.0001f);
	const auto contactLater=render(.5f,1,0,0,0,0,12,2,4);
	const auto contactPan=render(.5f,1,0,2,0,0,12,2,3);
	const std::vector<float> *contactTimeSamples[]={&contactReference,&contactFrozen,&contactTinyTime,&contactLater,&contactPan};
	for(const auto *v:contactTimeSamples) {
		CHECK(v->size()==width*height*4);if(v->size()!=width*height*4)return;
	}
	CHECK(contactReference==contactFrozen);
	float contactTinyChange=0;double contactTimeRms=0;bool contactWorldFixed=true;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		contactTinyChange=fmaxf(contactTinyChange,fabsf(contactTinyTime[at]-contactReference[at]));
		const double d=contactLater[at]-contactReference[at];contactTimeRms+=d*d/(width*height);
		if(x+1<width)contactWorldFixed &= contactPan[at]==contactReference[at+4];
	}
	contactTimeRms=std::sqrt(contactTimeRms);
	CHECK(contactTinyChange<.002f && contactTimeRms>.005 && contactTimeRms<.35 && contactWorldFixed);
	// An armed finite source region distinguishes redistribution from additive foam:
	// thousands of samples outside the support must remain exactly zero at nonzero time.
	const auto contactSupported=render(.5f,1,1,-73,119,.37f,12,2,4);
	CHECK(contactSupported.size()==width*height*4);if(contactSupported.size()!=width*height*4)return;
	unsigned contactOutside=0,contactInsideActive=0;bool contactSupportExact=true;
	for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
		const size_t at=(y*width+x)*4;
		const bool inside=std::abs(int(x)*2-256)<=96 && std::abs(int(y)*2-256)<=96;
		const float c=inside?.5f:0.f,v=contactSupported[at];
		contactSupportExact &= contactSupported[at+1]==c && contactSupported[at+2]==0 && contactSupported[at+3]==1;
		contactSupportExact &= std::isfinite(v) && v>=0 && v<=1;
		if(!inside){++contactOutside;contactSupportExact &= v==0;}
		else contactInsideActive+=v>.1f;
	}
	CHECK(contactSupportExact && contactOutside>width*height/2 && contactInsideActive>100);
	// Above the physical filter cutoff, phase/position must not leave shimmering patches.
	// The independent target is the input mean; sqrt/divide/multiply may round by an ULP.
	float contactFarError=0;std::vector<float> contactFarHalf;
	for(float coverage:{.2f,.5f,.8f}) {
		const auto values=render(coverage,12,0,0,0,0,12,2,3);
		CHECK(values.size()==width*height*4);if(values.size()!=width*height*4)return;
		bool uniform=true;for(unsigned i=0;i<width*height;++i) {
			contactFarError=fmaxf(contactFarError,fabsf(values[i*4]-coverage));
			uniform &= values[i*4]==values[0] && values[i*4+1]==coverage && values[i*4+2]==0 && values[i*4+3]==1;
		}
		CHECK(uniform);if(coverage==.5f)contactFarHalf=values;
	}
	const auto contactFarMoved=render(.5f,12,0,137,-219,.71f,12,2,19);
	CHECK(contactFarMoved.size()==width*height*4);if(contactFarMoved.size()!=width*height*4)return;
	CHECK(contactFarHalf==contactFarMoved && contactFarError<.000001f);
	printf("sdl_gpu_frame_selfcheck: fresh contact patches: 21 added atlases, maximum mean error %.7f; bounded two-scale coverage, minimum variance %.7f / 16:2-world energy ratio %.5f, minimum gaps %u / clusters %u; 0.0001-second max %.7f / 1-second RMS %.7f; exact pause/world-pan/support (%u exterior samples), unresolved mean error %.8f\n",contactMeanError,contactMinimumVariance,contactMinimumScaleRatio,contactMinimumGaps,contactMinimumClusters,contactTinyChange,contactTimeRms,contactOutside,contactFarError);

	printf("sdl_gpu_frame_selfcheck: GPU foam microcoverage: %u world atlases, maximum expected-coverage error %.7f; uniform-rank density/value/mean errors %.7f/%.7f/%.7f; graded fresh %u, close mean %.7f, time-step max %.7f and 1-second RMS %.7f; old/frozen patch variogram %.7f/%.7f; soft/hard edge energy %.7f/%.7f, band %.7f/%.7f; rank quadrature %.7f; exact pause/world pan/old ages, smooth age %.7f (gates %.9f), footprint %.7f/%.7f/%.7f checked\n",renders,maximumMeanError,densityError,coverageQuadratureError,meanFormulaError,gradedInterior,freshMean,tinyTimeChange,temporalRms,shapeVariogram[0],shapeVariogram[1],softEnergy[0],softEnergy[1],softEnergy[2],softEnergy[3],rankAreaError,maximumAgeChange,ageGateChange,nearChange,farChange,freshFilterChange);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_foam_irradiance(SdlGpuFrame *frame)
{
	// These are authored legacy light colours, not pre-linearized radiance. Expected values
	// are independently integrated in double precision below, including coloured and HDR sums.
	struct Query {float normal[4],light[4],ambient[4],sun[4],fillDirection1[4],fillColour1[4],fillDirection2[4],fillColour2[4];};
	const Query queries[]={
		{{0,0,1,0},{0,0,1,0},{.21f,.34f,.56f,0},{0,0,0,0}},
		{{1,0,0,0},{0,0,-1,1},{.21f,.34f,.56f,0},{0,0,0,0}},
		{{0,0,-1,0},{.6f,0,.8f,.5f},{.21f,.34f,.56f,0},{0,0,0,0}},
		{{0,0,1,0},{0,0,1,1},{0,0,0,0},{.25f,.5f,.75f,0}},
		{{0,0,1,0},{0,0,1,0},{0,0,0,0},{.25f,.5f,.75f,0}},
		{{0,0,-1,0},{0,0,1,1},{0,0,0,0},{.25f,.5f,.75f,0}},
		{{1,0,0,0},{0,0,1,1},{0,0,0,0},{.25f,.5f,.75f,0}},
		{{.6f,0,.8f,0},{0,0,1,1},{0,0,0,0},{.25f,.5f,.75f,0}},
		{{.6f,0,.8f,0},{0,.6f,.8f,1},{.18f,.31f,.52f,0},{.67f,.23f,.11f,0}},
		{{.6f,0,.8f,0},{0,.6f,.8f,.375f},{.18f,.31f,.52f,0},{.67f,.23f,.11f,0}},
		{{0,0,1,0},{0,0,1,1},{.75f,.65f,.55f,0},{.85f,.95f,1.15f,0}},
		{{0,0,1,0},{0,0,1,1},{-.1f,.1f,-.5f,0},{.3f,.2f,.1f,0}},
		{{0,0,1,0},{0,0,1,1},{0,0,0,0},{0,0,0,0}},
		{{.6f,0,.8f,0},{0,.6f,.8f,0},{.18f,.31f,.52f,0},{.67f,.23f,.11f,0}},
		// Rotate both directions ninety degrees around Z: diffuse illumination is unchanged.
		{{0,.6f,.8f,0},{-.6f,0,.8f,1},{.18f,.31f,.52f,0},{.67f,.23f,.11f,0}},
		// Three coloured lights, then shadow only the main sun. The authored fill stays present.
		{{0,0,1,0},{0,0,1,1},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0},{.6f,0,.8f,0},{.2f,.4f,.6f,0},{0,.8f,.6f,0},{.3f,.2f,.1f,0}},
		{{0,0,1,0},{0,0,1,0},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0},{.6f,0,.8f,0},{.2f,.4f,.6f,0},{0,.8f,.6f,0},{.3f,.2f,.1f,0}},
		{{1,0,0,0},{0,0,1,1},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0},{.6f,0,.8f,0},{.2f,.4f,.6f,0},{0,.8f,.6f,0},{.3f,.2f,.1f,0}},
		{{0,1,0,0},{0,0,1,1},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0},{0,.6f,.8f,0},{.2f,.4f,.6f,0},{-.8f,0,.6f,0},{.3f,.2f,.1f,0}},
		{{0,0,-1,0},{0,0,1,1},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0},{.6f,0,.8f,0},{.2f,.4f,.6f,0},{0,.8f,.6f,0},{.3f,.2f,.1f,0}},
		{{0,0,1,0},{0,0,1,0},{.1f,.2f,.3f,0},{.4f,.2f,.1f,0}},
		// Tournament Island's actual morning map lights, with an armed one-sun-only control.
		{{0,0,1,0},{.88978535f,-.04663176f,.45399061f,1},{.50196081f,.4f,.3019608f,0},{.38823530f,.27450982f,.08627451f,0},
		 {.38457698f,-.86377418f,.32556823f,0},{.23529412f,.19607843f,.19607843f,0},{.80184519f,.27609733f,.52991933f,0},{.23529412f,.19607843f,.19607843f,0}},
		{{0,0,1,0},{.88978535f,-.04663176f,.45399061f,1},{.50196081f,.4f,.3019608f,0},{.38823530f,.27450982f,.08627451f,0}},
		// USA02 has valid fill directions but zero diffuse colours: preserve its former result.
		{{0,0,1,0},{.79692197f,-.53753084f,.27563736f,1},{.52156866f,.41568628f,.3019608f,0},{.90196079f,.70196080f,.56470591f,0},
		 {-.01745246f,0,.99984771f,0},{0,0,0,0},{-.01745246f,0,.99984771f,0},{0,0,0,0}},
	};
	const unsigned rows=sizeof(queries)/sizeof(queries[0]);
	PixelPipelineDescription pd={};std::string production;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,production,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t noiseStart=production.find("float oceanHash("),noiseEnd=production.find("float3 oceanSky(");
	CHECK(noiseStart!=std::string::npos && noiseEnd>noiseStart);
	if(noiseStart==std::string::npos || noiseEnd==std::string::npos || noiseEnd<=noiseStart)return;
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string ps="Texture2D Texture14:register(t14);SamplerState Sampler14:register(s14);\n"
		"struct Query{float4 normal;float4 light;float4 ambient;float4 sun;float4 fillDirection1;float4 fillColour1;float4 fillDirection2;float4 fillColour2;};\n"
		"cbuffer QueryBlock:register(b0){Query Queries["+std::to_string(rows)+"];"
		"float4 FoamHistoryArea;float4 FoamHistoryParameters;};\n"+
		production.substr(noiseStart,noiseEnd-noiseStart)+WaterFoamShading::HistorySamplingSource()+WaterFoamShading::Source()+
		"float4 main(float4 pixel:SV_Position):SV_Target0{Query q=Queries[(uint)pixel.y];"
		"float3 value=waterFoamIrradiance(q.normal.xyz,q.light.xyz,q.ambient.xyz,q.sun.xyz,q.light.w,q.fillDirection1.xyz,q.fillColour1.xyz,q.fillDirection2.xyz,q.fillColour2.xyz);"
		"float3 old=waterFoamIrradiance(q.normal.xyz,q.light.xyz,q.ambient.xyz,q.sun.xyz,q.light.w);float3 error=abs(value-old);"
		"return float4(value,max(error.x,max(error.y,error.z)));}\n";
	CHECK(SDL3_Shader_Retarget(ps,false));
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;SDL_GPUTexture *target=NULL;
		SDL_GPUSampler *sampler=NULL;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vertex)SDL_ReleaseGPUShader(device,vertex);if(pixel)SDL_ReleaseGPUShader(device,pixel);
			if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} resources;
	resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);if(!resources.vertex)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("foam irradiance PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==15 && buffers==1);
	if(samplers!=15 || buffers!=1)return;
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);if(!resources.pixel)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=1;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.sampler);if(!resources.sampler)return;
	CHECK(frame->Flush());SdlResourceMirrors mirrors(frame);SDL_GPUTexture *white=mirrors.White();CHECK(white);if(!white)return;
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return;
	SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
	if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
	SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
	// The unused history declarations deliberately remain available to the full production
	// source. The compiler retains its Texture14 descriptor, so all reflected slots must be
	// valid even for the irradiance entry. Its noise function is the real one, not a stub.
	SDL_GPUTextureSamplerBinding bindings[15]={};
	for(unsigned i=0;i<15;++i){bindings[i].texture=white;bindings[i].sampler=resources.sampler;}
	SDL_BindGPUFragmentSamplers(pass,0,bindings,15);
	std::vector<float> constants(rows*sizeof(Query)/sizeof(float)+8,0);
	memcpy(constants.data(),queries,sizeof(queries));
	SDL_PushGPUFragmentUniformData(commands,0,constants.data(),unsigned(constants.size()*sizeof(float)));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	const auto values=read_float_targets(frame,&resources.target,1,1,rows);CHECK(values.size()==rows*4);if(values.size()!=rows*4)return;
	double errorMax=0,wrongLinearOrder=0;
	for(unsigned row=0;row<rows;++row) {
		const Query &q=queries[row];double cosine=0,fillCosine1=0,fillCosine2=0;
		for(unsigned c=0;c<3;++c){cosine+=double(q.normal[c])*q.light[c];
			fillCosine1+=double(q.normal[c])*q.fillDirection1[c];fillCosine2+=double(q.normal[c])*q.fillDirection2[c];}
		const double incoming=(std::max)(0.0,cosine)*q.light[3];
		for(unsigned c=0;c<3;++c) {
			const double expected=std::pow((std::max)(0.0,double(q.ambient[c])+q.sun[c]*incoming
				+q.fillColour1[c]*(std::max)(0.0,fillCosine1)+q.fillColour2[c]*(std::max)(0.0,fillCosine2)),2.2);
			const double actual=values[row*4+c];errorMax=(std::max)(errorMax,std::abs(actual-expected));
			CHECK(std::isfinite(actual) && std::abs(actual-expected)<.00001);
			if(row==8) {
				// Armed control: decoding the two light terms separately is observably different.
				const double wrong=std::pow(double(q.ambient[c]),2.2)+std::pow(double(q.sun[c]),2.2)*incoming;
				wrongLinearOrder=(std::max)(wrongLinearOrder,std::abs(actual-wrong));
			}
		}
		if(row<15 || row==20 || row==22 || row==23)CHECK(values[row*4+3]==0); // one-light/zero-fill results remain bit-exact
	}
	for(unsigned c=0;c<3;++c) {
		CHECK(values[c]==values[4+c] && values[c]==values[8+c]); // ambient ignores normal, sun direction and shadow
		CHECK(values[3*4+c]>0 && values[4*4+c]==0 && values[5*4+c]==0 && values[6*4+c]==0);
		CHECK(values[7*4+c]>0 && values[7*4+c]<values[3*4+c]);
		CHECK(values[9*4+c]<values[8*4+c] && values[13*4+c]<values[9*4+c]);
		CHECK(values[10*4+c]>1.0f); // no global radiance saturation before surface composition
		CHECK(values[12*4+c]==0);
		CHECK(std::abs(values[14*4+c]-values[8*4+c])<.000001f);
	}
	CHECK(values[11*4]>0 && values[11*4+1]>0 && values[11*4+2]==0);
	CHECK(wrongLinearOrder>.03);
	for(unsigned c=0;c<3;++c) {
		CHECK(values[15*4+c]>values[16*4+c] && values[16*4+c]>values[20*4+c]);
		CHECK(values[17*4+c]==values[18*4+c] && values[19*4+c]==values[20*4+c]);
		CHECK(values[21*4+c]>values[22*4+c]);
	}
	CHECK(values[21*4+3]>.3f && values[23*4+3]==0);
	printf("sdl_gpu_frame_selfcheck: GPU production foam irradiance: %u coloured fixtures, independent error %.8f; zero/one/three lights, unshadowed authored fills, ambient invariance, rotated normals, Island missing-fill control %.8f, USA02 unchanged, HDR/no clamp and decode-after-light-sum control %.8f checked\n",rows,errorMax,values[21*4+3],wrongLinearOrder);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_foam_environment(SdlGpuFrame *frame)
{
	using namespace WaterFoamEnvironment;
	// The source below independently reproduces the selected oceanSky radiance, including its
	// hash and directional cloud bank, with only the explicit solar halo omitted. It never
	// calls the CPU sky generator or its emitted coefficients. GPU radiance is integrated
	// with independent sphere and cosine-hemisphere measures on the CPU after test readback.
	const char *reference=R"HLSL(
float originalHash(float2 p) {
 float3 q=frac(float3(p.x,p.y,p.x)*0.1031);q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);
}
float originalNoise(float2 p) {
 float2 cell=floor(p),f=frac(p);f=f*f*(3.0-2.0*f);
 return lerp(lerp(originalHash(cell),originalHash(cell+float2(1,0)),f.x),
  lerp(originalHash(cell+float2(0,1)),originalHash(cell+float2(1,1)),f.x),f.y);
}
float3 originalSky(float3 ray) {
 float horizon=pow(1.0-saturate(ray.z),4.0);
 float2 plane=ray.xy/(0.45+abs(ray.z));
 float cloud=originalNoise(plane*2.7+float2(7.8,2.3))*0.7+originalNoise(plane*6.1+float2(0.4,13.7))*0.3;
 float bank=pow(saturate(dot(ray,normalize(float3(0,1,0.85)))),48.0);
 cloud=smoothstep(0.46,0.73,cloud)*(0.04+1.5*bank);
 float3 radiance=lerp(float3(0.016,0.055,0.110),float3(0.023,0.070,0.130),horizon)+cloud*float3(1.85,1.96,2.04);
 return lerp(float3(0.001,0.005,0.010),radiance,smoothstep(-0.12,0.18,ray.z));
}
)HLSL";
	struct Query {float normal[4],light[4],ambient[4],sun[4],fillDirection1[4],fillColour1[4],fillDirection2[4],fillColour2[4];};
	std::vector<Query> queries;
	std::vector<unsigned> canonical,rotationSource;
	const double pi=3.14159265358979323846;
	auto append=[&](Query q){q.fillDirection1[3]=1;queries.push_back(q);return unsigned(queries.size()-1);};
	for(unsigned tilt:{0u,20u,45u,60u,85u,90u,120u,160u,180u}) for(unsigned azimuth=0;azimuth<360;azimuth+=60) {
		if((tilt==0 || tilt==180) && azimuth)continue;
		const double a=tilt*pi/180,b=azimuth*pi/180;
		Query q={};q.normal[0]=float(std::sin(a)*std::cos(b));q.normal[1]=float(std::sin(a)*std::sin(b));q.normal[2]=float(std::cos(a));
		q.normal[3]=1;q.light[0]=1;q.light[3]=1;q.sun[3]=1;canonical.push_back(append(q));
	}
	const unsigned canonicalCount=unsigned(canonical.size());
	for(unsigned index:{0u,7u,14u,21u,28u,35u,43u})for(float angle:{.67f,2.13f}) {
		Query q=queries[index];const float c=cosf(angle),s=sinf(angle),x=q.normal[0],y=q.normal[1];
		q.normal[0]=c*x-s*y;q.normal[1]=s*x+c*y;q.light[0]=c;q.light[1]=s;append(q);rotationSource.push_back(index);
	}
	const unsigned lightingBegin=unsigned(queries.size());
	for(float offshore:{0.f,.35f,1.f})for(float visibility:{0.f,.4f,1.f}) {
		Query q={{.6f,0,.8f,offshore},{0,.6f,.8f,visibility},{.5019608f,.4f,.3019608f,0},
			{.3882353f,.2745098f,.0862745f,.3882353f},{.384577f,-.863774f,.325568f,0},{.235294f,.196078f,.196078f,0},
			{.801845f,.276097f,.529919f,0},{.235294f,.196078f,.196078f,0}};
		append(q);
	}
	Query q=queries[lightingBegin];q.normal[3]=1;q.ambient[0]=q.ambient[1]=q.ambient[2]=0;
	q.sun[0]=q.sun[1]=q.sun[2]=q.fillColour1[0]=q.fillColour1[1]=q.fillColour1[2]=q.fillColour2[0]=q.fillColour2[1]=q.fillColour2[2]=0;
	q.sun[3]=0;const unsigned night=append(q);q.sun[3]=1;const unsigned day=append(q);q.sun[3]=5;const unsigned hdr=append(q);
	q=queries[lightingBegin];q.normal[3]=0;q.sun[0]=2;q.sun[1]=3;q.sun[2]=4;q.light[3]=1;
	const unsigned hdrAuthored=append(q);
	q=queries[lightingBegin+6];const unsigned noVessel=append(q);queries[noVessel].fillDirection1[3]=0;
	const unsigned count=unsigned(queries.size()),batchQueries=31;CHECK(count<128);
	static_assert(sizeof(Query)==128,"Lighting query must follow eight HLSL float4 registers");
	// SDL Vulkan exposes4096-byte UBO sections. The production water block is smaller,
	// but this independent atlas has more queries and must upload bounded batches.
	CHECK(16+batchQueries*sizeof(Query)<=4096);
	PixelPipelineDescription description={};std::string production;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t callStart=production.find("float3 vesselIrradiance=waterFoamIrradiance("),callEnd=production.find("float3 vesselDiffuse=",callStart);
	CHECK(callStart!=std::string::npos && callEnd>callStart);if(callStart==std::string::npos || callEnd<=callStart)return;
	const std::string foam=WaterFoamShading::Source();const size_t end=foam.find("// Cubic bilinear value noise");
	CHECK(end!=std::string::npos && foam.find("waterFoamReplaceAmbient")<end);if(end==std::string::npos)return;
	std::string ps="struct Query{float4 normal;float4 light;float4 ambient;float4 sun;float4 fillDirection1;float4 fillColour1;float4 fillDirection2;float4 fillColour2;};\n"
		"cbuffer QueriesBlock:register(b0){float4 Meta;Query Queries["+std::to_string(batchQueries)+"];};\n"+
		reference+HlslSource()+foam.substr(0,end)+
		"float3 productionIrradiance(Query q){float3 normal=q.normal.xyz,light=q.light.xyz;"
		"float4 HullParameters=float4(0,q.ambient.xyz),SunColour=q.sun;float sunVisible=q.light.w;"
		"float4 FoamFillLights[4]={float4(q.fillDirection1.xyz,1),q.fillColour1,q.fillDirection2,q.fillColour2};"
		"float vesselFoam=q.fillDirection1.w,offshore=q.normal.w,daylight=q.sun.w;"+
		production.substr(callStart,callEnd-callStart)+"return vesselIrradiance;}\n"+R"HLSL(
struct Result{float4 radiance:SV_Target0;float4 ray:SV_Target1;};
Result main(float4 pixel:SV_Position) {
 Result result;
 if(Meta.z<1.5) {
  uint id=(uint)pixel.x+(uint)pixel.y*(uint)Meta.x;float u=(float(id)+0.5)/Meta.w;
  float phase=float(id)*2.399963229728653;float3 ray;
  if(Meta.z<0.5) {float z=1.0-2.0*u,r=sqrt(max(0.0,1.0-z*z));ray=float3(r*cos(phase),r*sin(phase),z);}
  else {
   float3 n=normalize(Queries[0].normal.xyz);
   float3 tangent=normalize(cross(n,abs(n.z)<0.9?float3(0,0,1):float3(1,0,0))),bitangent=cross(n,tangent);
   float radius=sqrt(u);ray=tangent*(radius*cos(phase))+bitangent*(radius*sin(phase))+n*sqrt(1.0-u);
  }
  result.radiance=float4(originalSky(ray),1);result.ray=float4(ray,1);return result;
 }
 Query q=Queries[min((uint)pixel.x,(uint)Meta.w-1)];
 float3 sky=waterFoamSkyIrradiance(q.normal.xyz,q.light.xyz,q.sun.w);
 float3 authored=waterFoamIrradiance(q.normal.xyz,q.light.xyz,q.ambient.xyz,q.sun.xyz,q.light.w,
  q.fillDirection1.xyz,q.fillColour1.xyz,q.fillDirection2.xyz,q.fillColour2.xyz);
 float3 replaced=waterFoamReplaceAmbient(authored,q.ambient.xyz,sky,q.normal.w);
 result.radiance=float4(sky,all(replaced==authored)?1.0:0.0);
 result.ray=float4(productionIrradiance(q),1);return result;
}
)HLSL";
	CHECK(SDL3_Shader_Retarget(ps,false));
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;SDL_GPUTexture *targets[2]={};
		~Resources(){for(auto *t:targets)if(t)SDL_ReleaseGPUTexture(device,t);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} resources;resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("foam environment PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0 && buffers==1);
	if(samplers!=0 || buffers!=1)return;
	resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
	SDL_GPUColorTargetDescription colour[2]={};colour[0].format=colour[1].format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo piInfo={};piInfo.vertex_shader=resources.vs;piInfo.fragment_shader=resources.ps;
	piInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;piInfo.target_info.color_target_descriptions=colour;piInfo.target_info.num_color_targets=2;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&piInfo);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour[0].format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=ti.height=256;ti.layer_count_or_depth=ti.num_levels=1;
	for(auto &target:resources.targets){target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(target);if(!target)return;}
	auto render=[&](unsigned width,unsigned height,float mode,unsigned query=0) {
		const bool queryMode=mode>=1.5f;
		const unsigned batches=queryMode?(count+batchQueries-1)/batchQueries:1;
		std::vector<float> combined(queryMode?count*8:0);
		for(unsigned batch=0;batch<batches;++batch) {
		const unsigned first=queryMode?batch*batchQueries:query;
		const unsigned localCount=queryMode?(std::min)(batchQueries,count-first):1;
		const unsigned localWidth=queryMode?localCount:width,localHeight=queryMode?1:height;
		std::vector<unsigned char> constants(16+batchQueries*sizeof(Query));
		const float meta[4]={float(localWidth),float(localHeight),mode,float(queryMode?localCount:width*height)};
		memcpy(constants.data(),meta,16);memcpy(constants.data()+16,queries.data()+first,localCount*sizeof(Query));
		CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return std::vector<float>();
		SDL_GPUColorTargetInfo targets[2]={};for(unsigned i=0;i<2;++i){targets[i].texture=resources.targets[i];targets[i].load_op=SDL_GPU_LOADOP_DONT_CARE;targets[i].store_op=SDL_GPU_STOREOP_STORE;}
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,targets,2,NULL);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>();}
		SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);SDL_GPUViewport viewport={0,0,float(localWidth),float(localHeight),0,1};SDL_SetGPUViewport(pass,&viewport);
		SDL_PushGPUFragmentUniformData(commands,0,constants.data(),unsigned(constants.size()));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
		CHECK(SDL_SubmitGPUCommandBuffer(commands));const auto result=read_float_targets(frame,resources.targets,2,localWidth,localHeight);
		if(!queryMode)return result;
		CHECK(result.size()==localCount*8);if(result.size()!=localCount*8)return std::vector<float>();
		for(unsigned target=0;target<2;++target)std::copy(result.begin()+target*localCount*4,result.begin()+(target+1)*localCount*4,
			combined.begin()+target*count*4+first*4);
		}
		return combined;
	};
	const auto fine=render(256,256,0),coarse=render(128,128,0),values=render(count,1,2);
	CHECK(fine.size()==65536*8 && coarse.size()==16384*8 && values.size()==count*8);
	if(fine.size()!=65536*8 || coarse.size()!=16384*8 || values.size()!=count*8)return;
	auto component=[](Rgb rgb,unsigned c){return c==0?rgb.r:c==1?rgb.g:rgb.b;};
	auto integrate=[&](const std::vector<float>&samples,const Query &query) {
		const unsigned n=unsigned(samples.size()/8);Rgb out={};double sum[3]={};
		for(unsigned i=0;i<n;++i) {
			const float *ray=&samples[n*4+i*4];const double cosine=(std::max)(0.0,double(query.normal[0])*ray[0]+double(query.normal[1])*ray[1]+double(query.normal[2])*ray[2]);
			for(unsigned c=0;c<3;++c)sum[c]+=samples[i*4+c]*cosine;
		}
		out.r=sum[0]*4/n;out.g=sum[1]*4/n;out.b=sum[2]*4/n;return out;
	};
	double maxReferenceError=0,maxConvergence=0,maxCpuGpu=0,maxRotation=0,maxAmbient=0,maxCosineConvergence=0;
	for(unsigned index=0;index<canonicalCount;++index) {
		const Rgb a=integrate(fine,queries[index]),b=integrate(coarse,queries[index]);
		for(unsigned c=0;c<3;++c) {
			const double v=values[index*4+c],expected=component(a,c),convergence=std::abs(expected-component(b,c));
			maxReferenceError=(std::max)(maxReferenceError,std::abs(v-expected));maxConvergence=(std::max)(maxConvergence,convergence);
			if(std::abs(v-expected)>=.006 || convergence>=.003)printf("sky integral: normal %u channel %u GPU %.8g reference %.8g coarse %.8g\n",index,c,v,expected,component(b,c));
			CHECK(std::abs(v-expected)<.006 && convergence<.003);
		}
	}
	for(unsigned index:{0u,13u,16u,25u,43u}) {
		const auto cosine=render(256,128,1,index);CHECK(cosine.size()==32768*8);if(cosine.size()!=32768*8)return;
		const Rgb reference=integrate(fine,queries[index]);
		for(unsigned c=0;c<3;++c) {
			double sum=0;for(unsigned i=0;i<32768;++i)sum+=cosine[i*4+c];const double difference=std::abs(sum/32768-component(reference,c));
			maxCosineConvergence=(std::max)(maxCosineConvergence,difference);CHECK(difference<.003);
		}
	}
	for(unsigned row=0;row<count;++row) {
		const Query &f=queries[row];const Rgb cpu=Evaluate({f.normal[0],f.normal[1],f.normal[2]},{f.light[0],f.light[1],f.light[2]},f.sun[3]);
		double sunDot=0,fill1=0,fill2=0;for(unsigned c=0;c<3;++c){sunDot+=double(f.normal[c])*f.light[c];fill1+=double(f.normal[c])*f.fillDirection1[c];fill2+=double(f.normal[c])*f.fillDirection2[c];}
		for(unsigned c=0;c<3;++c) {
			const double sky=values[row*4+c],value=values[count*4+row*4+c];
			maxCpuGpu=(std::max)(maxCpuGpu,std::abs(sky-component(cpu,c)));
			CHECK(std::isfinite(sky) && sky>=0 && std::abs(sky-component(cpu,c))<.00001);
			const double authored=std::pow((std::max)(0.0,double(f.ambient[c])+f.sun[c]*(std::max)(0.0,sunDot)*f.light[3]+
				f.fillColour1[c]*(std::max)(0.0,fill1)+f.fillColour2[c]*(std::max)(0.0,fill2)),2.2);
			const double expected=authored+(sky-std::pow((std::max)(0.0,double(f.ambient[c])),2.2))*(f.fillDirection1[3]>0?f.normal[3]:0);
			maxAmbient=(std::max)(maxAmbient,std::abs(value-expected));CHECK(std::isfinite(value) && value>=0 && std::abs(value-expected)<.00003);
			if(f.normal[3]==0)CHECK(values[row*4+3]==1); // exact production early return, including HDR authored lighting
		}
	}
	for(unsigned i=0;i<rotationSource.size();++i)for(unsigned c=0;c<3;++c) {
		const double difference=std::abs(values[(canonicalCount+i)*4+c]-values[rotationSource[i]*4+c]);
		maxRotation=(std::max)(maxRotation,difference);CHECK(difference<.00002);
	}
	for(unsigned c=0;c<3;++c) {
		CHECK(std::abs(values[night*4+c]-.06*values[day*4+c])<.000002);
		CHECK(values[hdr*4+c]==values[day*4+c]); // daylight is the existing bounded weather factor; HDR radiance is tested below
		CHECK(values[count*4+hdrAuthored*4+c]>1); // HDR light energy remains unclamped
		CHECK(values[count*4+noVessel*4+c]==values[count*4+lightingBegin*4+c]); // same authored lights, new material guard bypassed
		CHECK(std::abs(values[count*4+noVessel*4+c]-values[count*4+(lightingBegin+6)*4+c])>.001); // armed ambient replacement control
		for(unsigned shore=0;shore<3;++shore) {
			const unsigned first=lightingBegin+shore*3,last=first+2;
			const double a=values[count*4+first*4+c],b=values[count*4+last*4+c];
			const double oldA=values[count*4+lightingBegin*4+c],oldB=values[count*4+(lightingBegin+2)*4+c];
			CHECK(std::abs((b-a)-(oldB-oldA))<.000003); // shadow delta is preserved, never applied to ambient/fill sky
		}
	}
	CHECK(&CachedTable()==&CachedTable());
	const Table dark=Build([](Vector){return Rgb{0,0,0};});
	const Table unit=Build([](Vector){return Rgb{1,1,1};});
	const Table coloured=Build([](Vector){return Rgb{2,4,8};});
	double unitMaximum=0,unitMinimum=1;
	for(const Query &f:queries) {
		const Vector normal={f.normal[0],f.normal[1],f.normal[2]};const Rgb d=Evaluate(dark,normal),u=Evaluate(unit,normal),h=Evaluate(coloured,normal);
		CHECK(d.r==0 && d.g==0 && d.b==0);
		unitMaximum=(std::max)(unitMaximum,u.r);unitMinimum=(std::min)(unitMinimum,u.r);
		CHECK(u.r>=0 && u.r<=1.00001 && std::abs(u.r-u.g)<1e-12 && std::abs(u.r-u.b)<1e-12);
		CHECK(h.r>=0 && h.r<=2.00002 && h.g<=4.00004 && h.b<=8.00008);
		CHECK(std::abs(h.g-2*h.r)<1e-10 && std::abs(h.b-4*h.r)<1e-10); // no tint/clamp or per-channel gain
	}
	CHECK(std::abs(Evaluate(unit,{0,0,1}).r-1)<.00001 && std::abs(Evaluate(unit,{0,0,-1}).r-1)<.00001);
	printf("sdl_gpu_frame_selfcheck: GPU foam sky: %u full-sphere/rotated/light queries; independent original-sky integral max %.7f, sphere/cosine convergence %.7f/%.7f, CPU/GPU %.7f, rigid rotation %.7f; ambient replacement %.7f, executed production callsite with exact zero-foam/offshore0/HDR and shadow increment, constant positive energy %.7f..%.7f checked\n",count,maxReferenceError,maxConvergence,maxCosineConvergence,maxCpuGpu,maxRotation,maxAmbient,unitMinimum,unitMaximum);
}
#endif // __linux__

#if defined(__linux__)
static std::string water_scene_reflection_define(const std::string &production)
{
	const size_t begin=production.find("#define WATER_SCENE_REFLECTIONS ");
	const size_t end=production.find('\n',begin);
	CHECK(begin!=std::string::npos && end!=std::string::npos);
	return begin!=std::string::npos && end!=std::string::npos?production.substr(begin,end-begin+1):std::string();
}

static void check_gpu_foam_material(SdlGpuFrame *frame)
{
	// Execute the actual material helpers and the emitted production integration block. A
	// deliberately coloured mirror separates incident reflection, diffuse foam and transmitted
	// clear water; double precision bilinear integration is independent of the shader filter.
	struct Query {float uv[4],mapping[4],sky[4],diffuse[4],incident[4],clear[4],crest[4],material[4];};
	const Query base={{8.5f/32,8.5f/32,0,0},{.1408f,.1408f,.55f,1},{.03f,.08f,.17f,0},
		{.3f,.12f,.6f,0},{.02f,1.4f,.7f,0},{.07f,.5f,.21f,0},{.6f,.35f,.15f,0},{.025f,.6f,.5f,.5f}};
	std::vector<Query> queries;
	auto add=[&](Query q){queries.push_back(q);return unsigned(queries.size()-1);};
	const unsigned constant=add(base);
	Query q=base;q.uv[0]=20.5f/32;q.uv[1]=5.5f/32;const unsigned empty=add(q);
	q=base;q.mapping[3]=0;const unsigned disabled=add(q);
	q=base;q.mapping[3]=.5f;add(q);
	q=base;q.uv[0]=-.5f;const unsigned outside=add(q);
	q=base;q.uv[0]=0;add(q);
	q=base;q.uv[0]=.5f/32;add(q); // partial border and coloured edge texels
	q=base;q.uv[0]=1.5f;add(q);
	q=base;q.mapping[1]*=.5f;add(q); // same footprint in both dimensions of a non-square reflection
	q=base;q.uv[0]=23.25f/32;q.uv[1]=23.75f/32;const unsigned mixed=add(q);
	q.uv[2]=.25f/32;q.uv[3]=-.5f/32;add(q); // distortion stays in the reflection's coordinate system
	q=base;q.uv[0]=q.uv[1]=16.5f/32;const unsigned feature=add(q);
	q.uv[0]-=1.0f/32;const unsigned featureLeft=add(q);
	q.uv[0]+=2.0f/32;const unsigned featureRight=add(q);
	q.uv[0]-=1.0f/32;q.uv[1]-=1.0f/32;const unsigned featureDown=add(q);
	q.uv[1]+=2.0f/32;const unsigned featureUp=add(q);
	q.uv[0]=18.5f/32;q.uv[1]=16.5f/32;const unsigned featureFar=add(q);
	q=base;q.material[0]=0;const unsigned noFresnel=add(q);
	q=base;q.material[1]=0;const unsigned tangent=add(q);
	q=base;q.material[1]=-.2f;add(q);
	q=base;q.material[0]=q.material[1]=1;const unsigned fullReflection=add(q);
	q=base;for(unsigned c=0;c<3;++c)q.diffuse[c]=q.incident[c]=0;const unsigned dark=add(q);
	q=base;for(unsigned c=0;c<3;++c){q.diffuse[c]=2.0f+c*1.7f;q.incident[c]=4.0f+c*2;}const unsigned hdr=add(q);
	q=base;q.mapping[2]=1;q.mapping[0]=q.mapping[1]=.256f;add(q);
	q=base;q.mapping[2]=0;add(q);
	q=base;q.diffuse[0]=.3f;q.diffuse[1]=q.diffuse[2]=0;
	q.incident[0]=q.incident[1]=0;q.incident[2]=.9f;
	q.clear[0]=q.clear[2]=0;q.clear[1]=.5f;
	q.material[0]=.2f;const float k=q.mapping[2]*q.mapping[2]*.5f;q.material[1]=k/(1+k);
	q.material[2]=q.material[3]=1;const unsigned singleFresnel=add(q);
	q.material[2]=q.material[3]=.4f;const unsigned partial=add(q);
	q=base;q.material[2]=q.material[3]=0;const unsigned clear=add(q);
	q.material[2]=.7f;const unsigned crestOnly=add(q);
	q.incident[0]=7;q.incident[1]=11;q.incident[2]=19;q.mapping[3]=0;q.material[0]=1;
	const unsigned changedUnusedMaterial=add(q);
	const unsigned rows=unsigned(queries.size()),columns=5,side=32;
	std::vector<uint32_t> texels(side*side,0);
	for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
		if(x<2)texels[y*side+x]=0xFF20C060;
		if(x>=4 && x<=11 && y>=4 && y<=11)texels[y*side+x]=0xFF4080C0;
		if(x>=21 && x<=26 && y>=21 && y<=26)texels[y*side+x]=(x+y)%2?0xFFC02060:0xFF1090E0;
	}
	texels[16*side+16]=0xFFE06020;
	PixelPipelineDescription pd={};std::string production;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,production,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t lightingStart=production.find("float3 waterLinear("),lightingEnd=production.find("float terrainHeight("),
		integrationStart=production.find("float3 vesselRadiance=vesselDiffuse;"),integrationEnd=production.find("float4 current;",integrationStart);
	CHECK(lightingStart!=std::string::npos && lightingEnd>lightingStart && integrationStart!=std::string::npos && integrationEnd>integrationStart);
	if(lightingStart==std::string::npos || lightingEnd<=lightingStart || integrationStart==std::string::npos || integrationEnd<=integrationStart)return;
	std::string ps=water_scene_reflection_define(production)+"Texture2D Texture1:register(t1);SamplerState Sampler1:register(s1);\n"
		"struct Query{float4 uv;float4 mapping;float4 sky;float4 diffuse;float4 incident;float4 clear;float4 crest;float4 material;};\n"
		"cbuffer QueriesBlock:register(b0){Query Queries["+std::to_string(rows)+"];};\n"+
		production.substr(lightingStart,lightingEnd-lightingStart)+WaterFoamShading::MaterialSource()+
		"float3 productionLayer(Query q){float3 vesselDiffuse=q.diffuse.rgb,surface=q.clear.rgb;"
		"float vesselFoam=q.material.w,foam=q.material.z,environmentRoughness=q.mapping.z;"
		"float3 sky=pow(q.sky.rgb,1.0/2.2),reflectedRay=float3(0,0,1),light=float3(0,0,1);"
		"float offshore=0,daylight=1,baseLevel=0;float4 WaterParameters=float4(0,0,q.mapping.w,0);"
		"float2 reflectionUV=q.uv.xy,distortion=q.uv.zw,reflectionScale=q.mapping.xy;"
		"float environmentFresnel=q.material.x,oceanViewCosine=q.material.y,foamDiffuse=1;"
		"float3 foamAlbedo=pow(q.crest.rgb,1.0/2.2),vesselIrradiance=float3(0,0,0);float shoreMaterialWeight=0;"+
		production.substr(integrationStart,integrationEnd-integrationStart)+"return surface;}\n"
		"float4 main(float4 pixel:SV_Position):SV_Target0{Query q=Queries[(uint)pixel.y];uint col=(uint)pixel.x;"
		"if(col==0)return float4(waterFoamPlanarReflection(q.sky.rgb,q.uv.xy,q.uv.zw,q.mapping.xy,q.mapping.z,q.mapping.w),1);"
		"float3 material=waterFoamRoughMaterial(q.diffuse.rgb,q.incident.rgb,q.material.x,q.material.y,q.mapping.z);"
		"if(col==1)return float4(material,1);"
		"if(col==2)return float4(waterFoamLayer(q.clear.rgb,q.crest.rgb,material,q.material.z,q.material.w),1);"
		"if(col==3){float share=saturate(q.material.w/max(q.material.z,.0001));"
		"float3 crest=waterLinear(pow(q.crest.rgb,1.0/2.2));"
		"return float4(waterDisplay(lerp(q.clear.rgb,lerp(crest,q.diffuse.rgb,share),"
		"saturate(max(q.material.z*.88,q.material.w)))),1);}"
		"return float4(productionLayer(q),1);}";
	CHECK(SDL3_Shader_Retarget(ps,false));
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL;SDL_GPUSampler *sampler=NULL;PosixTexture9 *mirror=NULL;
		~Resources(){if(mirror)mirror->Release();if(target)SDL_ReleaseGPUTexture(device,target);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
			if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} resources;resources.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("foam material PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==2 && buffers==1);
	if(samplers!=2 || buffers!=1)return;
	resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vs;pi.fragment_shader=resources.ps;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.sampler);if(!resources.sampler)return;
	resources.mirror=PosixTexture9::create(side,side,2,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);CHECK(resources.mirror);if(!resources.mirror)return;
	for(unsigned level=0;level<2;++level) {
		D3DLOCKED_RECT lock={};CHECK(resources.mirror->LockRect(level,&lock,NULL,0)==D3D_OK);if(!lock.pBits)return;
		for(unsigned y=0;y<(side>>level);++y)for(unsigned x=0;x<(side>>level);++x)
			((uint32_t *)((uint8_t *)lock.pBits+y*lock.Pitch))[x]=level?0xFFFF00FF:texels[y*side+x];
		CHECK(resources.mirror->UnlockRect(level)==D3D_OK);
	}
	CHECK(frame->Flush());SdlResourceMirrors mirrors(frame);std::string refusal;
	SDL_GPUTexture *mirror=mirrors.Texture(resources.mirror,refusal),*white=mirrors.White();CHECK(mirror && white);if(!mirror || !white)return;
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return;
	SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
	if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
	SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);SDL_GPUTextureSamplerBinding bindings[2]={{white,resources.sampler},{mirror,resources.sampler}};
	SDL_BindGPUFragmentSamplers(pass,0,bindings,2);SDL_PushGPUFragmentUniformData(commands,0,queries.data(),unsigned(queries.size()*sizeof(Query)));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	const auto values=read_float_targets(frame,&resources.target,1,columns,rows);CHECK(values.size()==rows*columns*4);if(values.size()!=rows*columns*4)return;
	auto sat=[](double v){return (std::max)(0.0,(std::min)(1.0,v));};
	auto sample=[&](double u,double v,unsigned channel) {
		const double px=sat(u)*side-.5,py=sat(v)*side-.5;const int ix=int(std::floor(px)),iy=int(std::floor(py));
		const double fx=px-ix,fy=py-iy;double value=0;
		for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
			const unsigned x=unsigned((std::max)(0,(std::min)(int(side)-1,ix+dx))),y=unsigned((std::max)(0,(std::min)(int(side)-1,iy+dy)));
			value+=double((texels[y*side+x]>>(16-channel*8))&255)/255*(dx?fx:1-fx)*(dy?fy:1-fy);
		}
		return value;
	};
	auto planar=[&](const Query &f,double roughness,double *out,double *wrongDecode) {
		const double offset[5][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1}},weight[5]={.4,.15,.15,.15,.15};
		double averagedDisplay[3]={};bool allCovered=true;for(unsigned c=0;c<3;++c)out[c]=0;
		for(unsigned tap=0;tap<5;++tap) {
			const double u=f.uv[0]+f.uv[2]+offset[tap][0]*sat(roughness)*.008/(std::max)(double(f.mapping[0]),.001),
				v=f.uv[1]+f.uv[3]+offset[tap][1]*sat(roughness)*.008/(std::max)(double(f.mapping[1]),.001);
			double rgb[3]={sample(u,v,0),sample(u,v,1),sample(u,v,2)};
			const double border=(std::min)((std::min)(u,1-u),(std::min)(v,1-v));
			const double coverage=sat(f.mapping[3])*sat(border*30)*sat((std::max)(rgb[0],(std::max)(rgb[1],rgb[2]))*64);
			allCovered &= coverage>.999999;
			for(unsigned c=0;c<3;++c){out[c]+=weight[tap]*(f.sky[c]*(1-coverage)+std::pow(rgb[c],2.2)*coverage);averagedDisplay[c]+=weight[tap]*rgb[c];}
		}
		if(wrongDecode)for(unsigned c=0;c<3;++c)wrongDecode[c]=allCovered?std::pow(averagedDisplay[c],2.2):out[c];
	};
	auto reflectance=[&](const Query &f,double roughness){const double mu=sat(f.material[1]),r=sat(roughness),k=r*r*.5;
		return sat(f.material[0])*mu/(std::max)(mu*(1-k)+k,.0001);};
	auto at=[&](unsigned row,unsigned column,unsigned channel){return double(values[(row*columns+column)*4+channel]);};
	double filterError=0,materialError=0,layerError=0,productionError=0,decodeControl=0;
	for(unsigned row=0;row<rows;++row) {
		const Query &f=queries[row];double reflected[3],wideReflected[3],wrongDecode[3];planar(f,f.mapping[2],reflected,wrongDecode);
		const double wideRoughness=(std::max)(double(f.mapping[2]),.55);planar(f,wideRoughness,wideReflected,NULL);
		const double factor=reflectance(f,f.mapping[2]),wideFactor=reflectance(f,wideRoughness),
			share=sat(f.material[3]/(std::max)(double(f.material[2]),.0001)),coverage=sat((std::max)(f.material[2]*.88,double(f.material[3])));
		for(unsigned c=0;c<3;++c) {
			const double material=f.diffuse[c]*(1-factor)+f.incident[c]*factor;
			const double layer=f.clear[c]*(1-coverage)+(f.crest[c]*(1-share)+material*share)*coverage;
			const double integratedMaterial=f.material[3]>0?f.diffuse[c]*(1-wideFactor)+wideReflected[c]*wideFactor:f.diffuse[c];
			const double integrated=f.clear[c]*(1-coverage)+(f.crest[c]*(1-share)+integratedMaterial*share)*coverage;
			filterError=(std::max)(filterError,std::abs(at(row,0,c)-reflected[c]));
			materialError=(std::max)(materialError,std::abs(at(row,1,c)-material));
			layerError=(std::max)(layerError,std::abs(at(row,2,c)-layer));
			productionError=(std::max)(productionError,std::abs(at(row,4,c)-std::pow(sat(integrated),1/2.2)));
			CHECK(std::abs(at(row,0,c)-reflected[c])<.00002 && std::abs(at(row,1,c)-material)<.00001);
			CHECK(std::abs(at(row,2,c)-layer)<.00001 && std::abs(at(row,4,c)-std::pow(sat(integrated),1/2.2))<.00002);
			CHECK(at(row,1,c)>=(std::min)(double(f.diffuse[c]),double(f.incident[c]))-.00001
				&& at(row,1,c)<=(std::max)(double(f.diffuse[c]),double(f.incident[c]))+.00001);
			for(unsigned col=0;col<columns;++col)CHECK(std::isfinite(at(row,col,c)) && at(row,col,c)>=0);
			if(row==mixed)decodeControl=(std::max)(decodeControl,std::abs(at(row,0,c)-wrongDecode[c]));
			if(f.material[3]==0)CHECK(at(row,4,c)==at(row,3,c)); // actual emitted engine block, not a fixture-only composition
		}
	}
	for(unsigned c=0;c<3;++c) {
		const double constantRadiance=std::pow(double((texels[8*side+8]>>(16-c*8))&255)/255,2.2);
		CHECK(std::abs(at(constant,0,c)-constantRadiance)<.00001);
		CHECK(std::abs(at(empty,0,c)-base.sky[c])<.000001 && std::abs(at(disabled,0,c)-base.sky[c])<.000001 && std::abs(at(outside,0,c)-base.sky[c])<.000001);
		CHECK(at(noFresnel,1,c)==base.diffuse[c] && at(tangent,1,c)==base.diffuse[c]);
		CHECK(std::abs(at(fullReflection,1,c)-base.incident[c])<.000001 && at(dark,1,c)==0 && at(hdr,1,c)>1);
		CHECK(at(clear,4,c)==at(clear,3,c) && at(crestOnly,4,c)==at(changedUnusedMaterial,4,c));
	}
	const double sharpRed=std::pow(224.0/255,2.2);
	CHECK(at(feature,0,0)<sharpRed-.1 && at(feature,0,0)>base.sky[0]+.1);
	for(unsigned row:{featureLeft,featureRight,featureDown,featureUp})CHECK(at(row,0,0)>base.sky[0]+.05);
	CHECK(std::abs(at(featureFar,0,0)-base.sky[0])<.000001 && decodeControl>.001);
	CHECK(std::abs(at(singleFresnel,1,0)-.27)<.000001 && at(singleFresnel,1,1)==0 && std::abs(at(singleFresnel,1,2)-.09)<.000001);
	CHECK(std::abs(at(singleFresnel,1,2)-.018)>.07); // armed double-Fresnel control
	CHECK(std::abs(at(partial,2,0)-.108)<.000001 && std::abs(at(partial,2,1)-.3)<.000001 && std::abs(at(partial,2,2)-.036)<.000001);
	resources.mirror->Release();resources.mirror=NULL;mirrors.Collect_Dead();CHECK(frame->Flush());
	printf("sdl_gpu_frame_selfcheck: GPU rough foam material: %u fixtures, independent filter/material/layer/production errors %.8f/%.8f/%.8f/%.8f; mip-zero coloured planar filtering, isotropic broadening, sky/border/level guards, single-Fresnel/HDR/convex energy, exact clear/crest invariance and decode-before-filter control %.8f checked\n",rows,filterError,materialError,layerError,productionError,decodeControl);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_coastal_material(SdlGpuFrame *frame)
{
	// Execute the actual emitted material block. Independent double-precision weights and
	// authored-light arithmetic check the layer, including its display/visibility composition.
	struct Query {float normal[4],light[4],ambient[4],sun[4],fillDirection1[4],fillColour1[4],
		fillDirection2[4],fillColour2[4],clear[4],foam[4],material[4],sky[4],edge[4],fog[4];};
	static_assert(sizeof(Query)==224,"Coastal queries follow fourteen HLSL float4 registers");
	const Query base={{0,0,1,0},{0,.8910065f,.4539905f,1},{.5019608f,.4f,.3019608f,0},
		{.3882353f,.2745098f,.0862745f,.3882353f},{.384577f,-.863774f,.325568f,0},{.235294f,.196078f,.196078f,0},
		{.801845f,.276097f,.529919f,0},{.235294f,.196078f,.196078f,0},
		{.12f,.10f,.06f,.65f},{.5f,0,0,0},{.025f,.8f,.35f,0},{.08f,.12f,.17f,0},
		{.9f,.8f,.7f,.7f},{.11f,.15f,.19f,0}};
	std::vector<Query> queries;
	auto add=[&](Query q){queries.push_back(q);return unsigned(queries.size()-1);};
	Query q=base;q.foam[0]=q.foam[1]=0;const unsigned clear=add(q);
	q=base;q.foam[0]=q.foam[1]=.7f;const unsigned crest=add(q);
	q=base;q.foam[0]=q.foam[1]=q.foam[2]=.6f;const unsigned vessel=add(q);
	const unsigned partial=add(base);
	q=base;q.foam[0]=1.0f/.88f;const unsigned full=add(q);
	q=base;q.foam[0]=q.foam[1]=.8f;const unsigned hiddenCrest=add(q);
	q=base;q.foam[0]=q.foam[1]=q.foam[2]=.8f;const unsigned hiddenVessel=add(q);
	q=base;q.foam[0]=.9f;q.foam[1]=.4f;q.foam[2]=.3f;const unsigned mixed=add(q);
	q=base;q.light[3]=0;const unsigned shadow=add(q);
	q=base;for(unsigned c=0;c<3;++c)q.fillColour1[c]=q.fillColour2[c]=0;const unsigned noFill=add(q);
	q=base;for(unsigned c=0;c<3;++c)q.ambient[c]=q.sun[c]=q.fillColour1[c]=q.fillColour2[c]=q.sky[c]=0;
	const unsigned dark=add(q);
	q=base;q.material[1]=0;const unsigned tangent=add(q);
	q=base;q.edge[3]=0;const unsigned fogged=add(q);
	q=base;for(unsigned c=0;c<3;++c)q.edge[c]=0;const unsigned shrouded=add(q);
	q=base;q.clear[3]=0;const unsigned transparent=add(q);
	q=base;for(unsigned c=0;c<3;++c){q.ambient[c]=1+c;q.sun[c]=4-c;}const unsigned hdr=add(q);
	const unsigned rows=unsigned(queries.size()),columns=4;
	CHECK(rows==16 && rows*sizeof(Query)<=4096);
	PixelPipelineDescription pd={};std::string production;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,production,COMBINER_SHADER_TARGET_SDL3_GPU));
	const size_t lightStart=production.find("float3 waterLinear("),lightEnd=production.find("float terrainHeight("),
		start=production.find("float3 vesselRadiance=vesselDiffuse;"),end=production.find("float4 current;",start);
	CHECK(lightStart!=std::string::npos && lightEnd>lightStart && start!=std::string::npos && end>start);
	if(lightStart==std::string::npos || lightEnd<=lightStart || start==std::string::npos || end<=start)return;
	const std::string lighting=WaterFoamShading::Source();const size_t lightingEnd=lighting.find("// Cubic bilinear value noise");
	CHECK(lightingEnd!=std::string::npos);if(lightingEnd==std::string::npos)return;
	std::string ps=water_scene_reflection_define(production)+"Texture2D Texture1:register(t1);SamplerState Sampler1:register(s1);\n"
		"struct Query{float4 normal;float4 light;float4 ambient;float4 sun;float4 fillDirection1;float4 fillColour1;"
		"float4 fillDirection2;float4 fillColour2;float4 clear;float4 foam;float4 material;float4 sky;float4 edge;float4 fog;};\n"
		"cbuffer QueriesBlock:register(b0){Query Queries["+std::to_string(rows)+"];};\n"+
		production.substr(lightStart,lightEnd-lightStart)+lighting.substr(0,lightingEnd)+WaterFoamShading::MaterialSource()+
		"float3 productionCoast(Query q,bool applyCoast){float3 surface=q.clear.rgb,normal=q.normal.xyz,light=q.light.xyz;"
		"float vesselFoam=q.foam.z,foam=q.foam.x,environmentRoughness=q.material.z,offshore=0,daylight=q.sun.w,baseLevel=0;"
		"float shoreMaterialWeight=applyCoast?waterFoamCoastWeight(foam,q.foam.y,vesselFoam):0;"
		"float3 foamAlbedo=float3(.67,.73,.71);float foamDiffuse=(.22+.78*saturate(dot(normal,light)))*(.12+.88*daylight);"
		"float3 vesselIrradiance=waterFoamIrradiance(normal,light,q.ambient.rgb,q.sun.rgb,q.light.w,"
		"q.fillDirection1.xyz,q.fillColour1.rgb,q.fillDirection2.xyz,q.fillColour2.rgb);"
		"float3 vesselDiffuse=waterLinear(float3(.75,.80,.77))*vesselIrradiance;"
		"float3 sky=pow(q.sky.rgb,1.0/2.2),reflectedRay=float3(0,0,1);float4 WaterParameters=float4(0,0,q.material.w,0);"
		"float2 reflectionUV=float2(.5,.5),distortion=float2(0,0),reflectionScale=float2(1,1);"
		"float environmentFresnel=q.material.x,oceanViewCosine=q.material.y;"+
		production.substr(start,end-start)+"return surface;}\n"
		"float4 main(float4 pixel:SV_Position):SV_Target0{Query q=Queries[(uint)pixel.y];uint col=(uint)pixel.x;"
		"float3 result=productionCoast(q,true);if(col==0)return float4(result,1);"
		"if(col==1)return float4(productionCoast(q,false),1);"
		"if(col==2){float3 visible=lerp(q.fog.rgb,saturate(result)*q.edge.rgb,saturate(q.edge.w));"
		"return float4(lerp(float3(.07,.11,.19),visible,q.clear.w),q.clear.w);}"
		"float coverage=saturate(max(q.foam.x*.88,q.foam.z)),share=saturate(q.foam.z/max(q.foam.x,.0001));"
		"return float4(waterFoamCoastWeight(q.foam.x,q.foam.y,q.foam.z),coverage,coverage*(1-share),1);}";
	CHECK(SDL3_Shader_Retarget(ps,false));
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL;SDL_GPUSampler *sampler=NULL;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
			if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
	} r;r.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled)return;
	r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);if(!r.vs)return;
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("coastal material PS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==2 && buffers==1);
	if(samplers!=2 || buffers!=1)return;
	r.ps=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps);if(!r.ps)return;
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	r.sampler=SDL_CreateGPUSampler(r.device,&si);CHECK(r.sampler);if(!r.sampler)return;
	CHECK(frame->Flush());SdlResourceMirrors mirrors(frame);SDL_GPUTexture *white=mirrors.White();CHECK(white);if(!white)return;
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
	SDL_GPUColorTargetInfo target={};target.texture=r.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,NULL);CHECK(pass);
	if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
	SDL_BindGPUGraphicsPipeline(pass,r.pipeline);SDL_GPUTextureSamplerBinding bindings[2]={{white,r.sampler},{white,r.sampler}};
	SDL_BindGPUFragmentSamplers(pass,0,bindings,2);SDL_PushGPUFragmentUniformData(commands,0,queries.data(),unsigned(queries.size()*sizeof(Query)));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	const auto values=read_float_targets(frame,&r.target,1,columns,rows);CHECK(values.size()==rows*columns*4);if(values.size()!=rows*columns*4)return;
	auto sat=[](double x){return (std::max)(0.0,(std::min)(1.0,x));};
	auto at=[&](unsigned row,unsigned col,unsigned channel){return double(values[(row*columns+col)*4+channel]);};
	auto dot=[](const float *a,const float *b){return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];};
	double maximumError=0,oldControl=0,wrongAlbedoControl=0;
	for(unsigned row=0;row<rows;++row) {
		const Query &f=queries[row];const double total=f.foam[0],other=f.foam[1],vesselCoverage=f.foam[2];
		const double cover=sat((std::max)(total*.88,vesselCoverage));
		const double vesselWeight=cover*sat(vesselCoverage/(std::max)(total,.0001)),genericWeight=cover-vesselWeight;
		const double otherCover=sat((std::max)(other*.88,vesselCoverage));
		const double otherGeneric=otherCover*(1-sat(vesselCoverage/(std::max)(other,.0001)));
		const double coastWeight=(std::max)(0.0,genericWeight-otherGeneric);
		CHECK(coastWeight>=0 && coastWeight<=genericWeight+1e-12);
		CHECK(std::abs(at(row,3,0)-coastWeight)<.000001 && std::abs(at(row,3,1)-cover)<.000001 && std::abs(at(row,3,2)-genericWeight)<.000001);
		const double roughness=(std::max)(double(f.material[2]),.55),mu=sat(f.material[1]),k=roughness*roughness*.5;
		const double reflected=sat(f.material[0])*mu/(std::max)(mu*(1-k)+k,.0001);
		const double genericLight=(.22+.78*sat(dot(f.normal,f.light)))*(.12+.88*f.sun[3]);
		const double albedo[3]={.67,.73,.71},vesselAlbedo[3]={.75,.80,.77},destination[3]={.07,.11,.19};
		for(unsigned c=0;c<3;++c) {
			// Sum authored display-space illumination once, then decode. This does not
			// call any production CPU shading helper and gives each material its own albedo.
			const double authored=f.ambient[c]+f.sun[c]*(std::max)(dot(f.normal,f.light),0.0)*f.light[3]
				+f.fillColour1[c]*(std::max)(dot(f.normal,f.fillDirection1),0.0)
				+f.fillColour2[c]*(std::max)(dot(f.normal,f.fillDirection2),0.0);
			const double irradiance=std::pow((std::max)(authored,0.0),2.2),generic=std::pow(albedo[c],2.2)*genericLight;
			const double coast=std::pow(albedo[c],2.2)*irradiance*(1-reflected)+f.sky[c]*reflected;
			const double vesselMaterial=std::pow(vesselAlbedo[c],2.2)*irradiance*(1-reflected)+f.sky[c]*reflected;
			const double ordinary=f.clear[c]*(1-cover)+generic*genericWeight+vesselMaterial*vesselWeight;
			const double linear=f.clear[c]*(1-cover)+generic*(genericWeight-coastWeight)+coast*coastWeight+vesselMaterial*vesselWeight;
			const double display=std::pow(sat(linear),1/2.2),oldDisplay=std::pow(sat(ordinary),1/2.2);
			const double visible=f.fog[c]*(1-sat(f.edge[3]))+display*f.edge[c]*sat(f.edge[3]);
			const double final=destination[c]*(1-f.clear[3])+visible*f.clear[3];
			maximumError=(std::max)(maximumError,std::abs(at(row,0,c)-display));
			maximumError=(std::max)(maximumError,std::abs(at(row,1,c)-oldDisplay));
			maximumError=(std::max)(maximumError,std::abs(at(row,2,c)-final));
			CHECK(std::abs(at(row,0,c)-display)<.00002 && std::abs(at(row,1,c)-oldDisplay)<.00002 && std::abs(at(row,2,c)-final)<.00002);
			for(unsigned col=0;col<columns;++col)CHECK(std::isfinite(at(row,col,c)) && at(row,col,c)>=0 && at(row,col,c)<=1.000001);
			if(coastWeight==0)CHECK(at(row,0,c)==at(row,1,c));
			if(row==partial){oldControl=(std::max)(oldControl,std::abs(display-oldDisplay));
				const double wrong=linear+coastWeight*(vesselMaterial-coast);
				wrongAlbedoControl=(std::max)(wrongAlbedoControl,std::abs(display-std::pow(sat(wrong),1/2.2)));}
		}
		CHECK(std::abs(at(row,2,3)-f.clear[3])<.000001);
	}
	for(unsigned row:{clear,crest,vessel,hiddenCrest,hiddenVessel})for(unsigned c=0;c<3;++c)CHECK(at(row,0,c)==at(row,1,c));
	CHECK(at(full,3,0)>.999999 && at(mixed,3,0)>0 && at(partial,3,0)>0);
	for(unsigned c=0;c<3;++c) {
		CHECK(at(partial,0,c)>at(shadow,0,c) && at(partial,0,c)>at(noFill,0,c));
		CHECK(at(dark,0,c)<at(partial,0,c) && at(hdr,0,c)>at(partial,0,c));
		CHECK(at(transparent,2,c)==float(c==0?.07:(c==1?.11:.19)));
	}
	CHECK(oldControl>.02 && wrongAlbedoControl>.005);
	CHECK(frame->Flush());
	printf("sdl_gpu_frame_selfcheck: GPU coastal material: %u actual emitted-block fixtures; independent lighting/weight/display/fog/shroud/alpha error %.8f, armed old-material %.6f and vessel-albedo %.6f controls; exact zero/crest/vessel/max-hidden identity, full and mixed coverage, same albedo, shadow/fill/HDR, tangent %u, fog %u and shroud %u checked\n",
		rows,maximumError,oldControl,wrongAlbedoControl,tangent,fogged,shrouded);
}
#endif // __linux__

// Independent scalar stern reference. The contour bow has its own closed-form oracle.
static double vessel_test_packet(const WaterHullModel::Hull &h,double x,double y,double level,double time,
	double *sternOut=NULL)
{
	if(sternOut)*sternOut=0;
	const double speed=std::hypot(h.data[2][0],h.data[2][1]);
	if(h.data[0][3]<3||std::abs(level-h.data[0][2])>.1||speed<=double(.3f))return 0;
	auto smooth=[](double a,double b,double v){const double t=(std::max)(0.0,(std::min)(1.0,(v-a)/(b-a)));return t*t*(3-2*t);};
	auto gaussian=[](double v,double width){return std::exp(-v*v/(width*width));};
	const double fx=h.data[2][0]/speed,fy=h.data[2][1]/speed,ax=h.data[1][0],ay=h.data[1][1],beam=h.data[1][3];
	const double blend=smooth(-.25,.25,fx*ax+fy*ay);
	const double sx=h.data[3][2]+(h.data[3][0]-h.data[3][2])*blend,sy=h.data[3][3]+(h.data[3][1]-h.data[3][3])*blend;
	const double dx=x-h.data[0][0]-(ax*sx-ay*sy),dy=y-h.data[0][1]-(ay*sx+ax*sy);
	const double along=-dx*fx-dy*fy,across=-dx*fy+dy*fx;
	const double length=(std::max)(30.0,(std::min)(110.0,speed*3.2+beam*2));
	const double width=(std::max)(beam*.35,1.0)+along*.353553,strip=(std::max)(2.5,(std::min)(7.0,beam*.18+2));
	const double arms=gaussian(across-width,strip)*std::cos((across-width)*.40)+gaussian(across+width,strip)*std::cos((across+width)*.40);
	const double churn=gaussian(across,(std::max)(beam*.55,2.0))*std::sin(along*.30-time*1.15+h.data[2][2]*6.2831853)*.24;
	const double radial=std::hypot(x-h.data[0][0],y-h.data[0][1]);
	const double support=1-smooth(h.data[2][3]+80,h.data[2][3]+110,radial);
	const double stern=(arms*1.35+churn)*smooth(0,10,along)*(1-smooth(length*.5,length,along))*smooth(.3,14,speed)*support;
	if(sternOut)*sternOut=stern;return stern;
}

static void check_gpu_vessel_hull_damping(SdlGpuFrame *frame)
{
	// Independent rectangle distances and finite differences check the shared VS/PS helper.
	// The reference never calls the production closest-edge or damping-gradient expressions.
	SdlWaterPixelConstants water={};water.HullParameters[0]=8;
	struct Rectangle {double x,y,angle,level;unsigned slot;};
	const Rectangle rectangles[]={{0,0,0,7,0},{80,23,.63,7,4},{91,27,.63,7,5},{-70,-40,-.41,7,7}};
	for(const auto &r:rectangles) {
		WaterHullModel::Hull hull;hull.data[0][0]=float(r.x);hull.data[0][1]=float(r.y);hull.data[0][2]=float(r.level);hull.data[0][3]=4;
		hull.data[1][0]=float(cos(r.angle));hull.data[1][1]=float(sin(r.angle));hull.data[1][2]=20;hull.data[1][3]=6;
		const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(hull.data[4],outline,sizeof(outline));
		memcpy(water.HullData+r.slot*10,&hull,sizeof(hull));
	}
	memcpy(water.HullData+10,water.HullData,sizeof(WaterHullModel::Hull));water.HullData[10][3]=2; // invalid contour
	memcpy(water.HullData+20,water.HullData,sizeof(WaterHullModel::Hull));water.HullData[20][2]=11; // another level
	struct Fixture {float point[4],freeDamping[4],offshore[4];};
	std::vector<Fixture> fixtures;
	auto add=[&](float x,float y,float level,float freeValue,float offshoreValue,float fx=0,float fy=0,float ox=0,float oy=0) {
		Fixture f={{x,y,level,0},{freeValue,fx,fy,0},{offshoreValue,ox,oy,0}};fixtures.push_back(f);
	};
	for(float y:{0.0f,5.0f,6.0f,6.01f,8.0f,10.5f,14.99f,15.0f,15.01f,40.0f}) add(0,y,7,1,1);
	for(float x:{19.99f,20.0f,20.01f,22.0f,25.0f}) add(x,8,7,1,1);
	const double localPoints[][2]={{0,8},{23,10},{-23,-10}};
	for(const auto &r:rectangles) for(const auto &p:localPoints)
		add(float(r.x+cos(r.angle)*p[0]-sin(r.angle)*p[1]),float(r.y+sin(r.angle)*p[0]+cos(r.angle)*p[1]),7,1,1);
	add(0,8,8,1,1);add(0,8,7,1,0);add(0,8,7,0,1);add(0,8,7,.9f,.6f,.003f,-.002f,.004f,.006f);
	add(0,12,7,.1f,.6f,.003f,-.002f,.004f,.006f); // floor/terrain wins the minimum
	add(0,8,7,WaterFftGeometry::SafeDamping(1,2.25f),1);
	add(0,14,7,WaterFftGeometry::SafeDamping(1,2.25f),1);
	add(23,10,7,.95f,.8f,-.004f,.001f,-.003f,.004f);
	add(80,23,7,.7f,.4f,.003f,-.002f,.004f,.006f); // inside the rotated hull
	const unsigned rows=unsigned(fixtures.size()),columns=3;
	std::string common=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+WaterVesselWaves::Source()+
		"cbuffer FixtureBlock:register(b1) {float4 Point;float4 FreeDamping;float4 Offshore;};\n";
	std::string vs=common+R"HLSL(
struct Output {float4 Position:SV_Position;[[vk::location(0)]] float3 Value:TEXCOORD0;};
Output main(uint id:SV_VertexID) {Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
o.Value=vesselApplyHullDamping(Point.xy,Point.z,FreeDamping.xyz,Offshore.xyz);return o;}
)HLSL";
	std::string ps=common+R"HLSL(
float4 main(float4 pixel:SV_Position,[[vk::location(0)]] float3 value:TEXCOORD0):SV_Target0 {
uint col=(uint)pixel.x;
if(col==0) return float4(vesselHullDamping(Point.xy,Point.z),1);
if(col==1) return float4(vesselApplyHullDamping(Point.xy,Point.z,FreeDamping.xyz,Offshore.xyz),1);
return float4(value,1);}
)HLSL";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUTexture *target=NULL;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vertex)SDL_ReleaseGPUShader(device,vertex);if(pixel)SDL_ReleaseGPUShader(device,pixel);}
	} resources;
	resources.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Shader_Retarget(vs,true));CHECK(SDL3_Shader_Retarget(ps,false));
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull damping VS: %s\n",error.c_str());return;}
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull damping PS: %s\n",error.c_str());return;}
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
	SDL_GPUColorTargetInfo target={};target.texture=resources.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
	SDL_PushGPUVertexUniformData(commands,0,&water,sizeof(water));SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
	for(unsigned row=0;row<rows;++row) {
		SDL_GPUViewport viewport={0,float(row),float(columns),1,0,1};SDL_SetGPUViewport(pass,&viewport);
		SDL_PushGPUVertexUniformData(commands,1,&fixtures[row],sizeof(Fixture));SDL_PushGPUFragmentUniformData(commands,1,&fixtures[row],sizeof(Fixture));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);
	}
	SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	const auto values=read_float_targets(frame,&resources.target,1,columns,rows);CHECK(values.size()==rows*columns*4);if(values.size()!=rows*columns*4)return;
	auto raw=[&](double x,double y,double level) {
		double result=1;
		for(const auto &r:rectangles) {
			if(std::abs(level-r.level)>.1)continue;
			const double ax=water.HullData[r.slot*10+1][0],ay=water.HullData[r.slot*10+1][1];
			const double localX=(x-r.x)*ax+(y-r.y)*ay,localY=-(x-r.x)*ay+(y-r.y)*ax;
			const double distance=std::hypot((std::max)(0.0,std::abs(localX)-20),(std::max)(0.0,std::abs(localY)-6));
			const double t=(std::min)(1.0,distance/9);result=(std::min)(result,t*t*(3-2*t));
		}
		return result;
	};
	double maxValue=0,maxGradient=0,maxStage=0;unsigned hullWins=0,freeWins=0,gradientControls=0;
	for(unsigned row=0;row<rows;++row) {
		const Fixture &f=fixtures[row];const double x=f.point[0],y=f.point[1],level=f.point[2];
		auto composed=[&](double px,double py) {
			const double free=f.freeDamping[0]+f.freeDamping[1]*(px-x)+f.freeDamping[2]*(py-y);
			const double offshore=f.offshore[0]+f.offshore[1]*(px-x)+f.offshore[2]*(py-y);
			if(free<=0 || offshore<=0)return free;
			return (std::min)(free,1+(raw(px,py,level)-1)*offshore);
		};
		for(unsigned col=0;col<2;++col) {
			const double expected=col?composed(x,y):raw(x,y,level);
			const double difference=std::abs(values[(row*columns+col)*4]-expected);
			maxValue=(std::max)(maxValue,difference);CHECK(difference<.000004);
			for(unsigned axis=0;axis<2;++axis) {
				const double step=.0001,px=x+(axis==0?step:0),py=y+(axis==1?step:0),mx=x-(axis==0?step:0),my=y-(axis==1?step:0);
				const double numeric=(col?composed(px,py)-composed(mx,my):raw(px,py,level)-raw(mx,my,level))/(2*step);
				const double error=std::abs(values[(row*columns+col)*4+1+axis]-numeric);
				maxGradient=(std::max)(maxGradient,error);CHECK(error<.00004);
			}
		}
		for(unsigned c=0;c<3;++c) {
			const double error=std::abs(values[(row*columns+1)*4+c]-values[(row*columns+2)*4+c]);maxStage=(std::max)(maxStage,error);CHECK(error<.000001);
		}
		CHECK(values[(row*columns+1)*4]>=0 && values[(row*columns+1)*4]<=f.freeDamping[0]+.000001f);
		if(f.offshore[0]==0) for(unsigned c=0;c<3;++c) CHECK(values[(row*columns+1)*4+c]==f.freeDamping[c]);
		if(composed(x,y)<f.freeDamping[0]-.01)++hullWins;else if(f.freeDamping[0]>0 && f.freeDamping[0]<1)++freeWins;
		const double missingOffshoreX=values[row*columns*4+1]*f.offshore[0];
		if(std::abs(values[(row*columns+1)*4+1]-missingOffshoreX)>.001)++gradientControls;
		const double shallow=(std::min)(double(WaterFftGeometry::SafeDamping(1,2.25f)),composed(x,y));
		CHECK(-WaterFftGeometry::MaxVerticalDisplacement*shallow>=-2.100001);
	}
	for(float value:values)CHECK(std::isfinite(value));CHECK(hullWins>8 && freeWins>=2 && gradientControls>=2);
	printf("sdl_gpu_frame_selfcheck: GPU analytic hull damping: %u fixtures, independent value %.8f/gradient %.8f/VS-PS %.8f; rotated/corner/final hull, level, min winner, offshore product, zero/floor bounds checked\n",rows,maxValue,maxGradient,maxStage);
}

static void check_gpu_water_wave_cage(SdlGpuFrame *frame)
{
	// The real water vertex wrapper reads VS3; the fragment mapping reads PS2. A
	// curved bilinear terrain field exercises changing cage axes/radii and the final
	// floor correction. The oracle samples the uploaded field and finite-differences
	// the complete ellipsoid map, independently of the generated shader derivatives.
	const unsigned side=16,width=8,height=8,plane=side*side*4;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *terrain[3]={},*spectrum=NULL,*zero=NULL,*targets[4]={};SDL_GPUSampler *sampler=NULL;
		~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vs)SDL_ReleaseGPUShader(device,vs);
			if(ps)SDL_ReleaseGPUShader(device,ps);for(auto *t:terrain)if(t)SDL_ReleaseGPUTexture(device,t);
			for(auto *t:targets)if(t)SDL_ReleaseGPUTexture(device,t);if(spectrum)SDL_ReleaseGPUTexture(device,spectrum);
			if(zero)SDL_ReleaseGPUTexture(device,zero);if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} r;r.device=frame->Device();
	VertexPipelineDescription vd={};vd.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;vd.StageCount=4;
	vd.Stages[0].TextureCoordinateIndex=0;vd.Stages[1].TextureCoordinateIndex=1;
	std::string vs;CHECK(VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,vs));CHECK(WaterFftSampling::WrapVertex(vs,true));
	CHECK(SDL3_Shader_Retarget(vs,true));
	const std::string entry="Output main(Input input)";const size_t at=vs.find(entry);CHECK(at!=std::string::npos);if(at==std::string::npos)return;
	vs.replace(at,entry.size(),"Output cageWaterVertex(Input input)");
	const std::string fixture=R"HLSL(
cbuffer Fixture:register(b2) {float4 ProbePoint;float4 ProbeControl;float4 ProbeEta;};
)HLSL";
	vs+=fixture+R"HLSL(
struct CageProbe {float4 Position:SV_Position;float4 Displacement:TEXCOORD0;};
CageProbe main(uint id:SV_VertexID) {
    Input input=(Input)0;input.Position=float3(ProbePoint.xy,ProbePoint.z+ProbePoint.w);
    input.Normal=ProbeControl.xyz;input.TexCoord1=float2(0,ProbePoint.z);
    // The actual SDL vertex entry converts its packed BGRA attribute to RGBA.
    input.Diffuse=float4(0,0,ProbeControl.w,1);
    Output actual=cageWaterVertex(input);CageProbe o;
    o.Displacement=float4(actual.Position.xyz-float3(ProbePoint.xy,ProbePoint.z),1);
    o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);return o;
}
)HLSL";
	// Retarget the newly added fixture block; the actual fixed input was already retargeted.
	const size_t fixtureAt=vs.find("cbuffer Fixture:register(b2)");CHECK(fixtureAt!=std::string::npos);
	if(fixtureAt==std::string::npos)return;vs.replace(fixtureAt,strlen("cbuffer Fixture:register(b2)"),"cbuffer Fixture:register(b2,space1)");
	std::string pixelFixture=fixture;pixelFixture.replace(pixelFixture.find("register(b2)"),strlen("register(b2)"),"register(b1)");
	std::string ps=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+pixelFixture+R"HLSL(
Texture2D Texture2:register(t2);SamplerState Sampler2:register(s2);
)HLSL"+WaterFftSampling::CommonSource()+WaterWaveCageShader::Source(false)+R"HLSL(
struct CageProbe {float4 Position:SV_Position;float4 Displacement:TEXCOORD0;};
struct Result {float4 Vertex:SV_Target0;float4 Pixel:SV_Target1;float4 DX:SV_Target2;float4 DY:SV_Target3;};
Result main(CageProbe input) {
    float2 p=ProbePoint.xy+(input.Position.xy-float2(4.5,4.5))*.625;
    float3 etaX=float3(.07,-.03,.05),etaY=float3(-.02,.04,-.06);
    float3 eta=ProbeEta.xyz+etaX*(p.x-ProbePoint.x)+etaY*(p.y-ProbePoint.y);
    WaveCage c=waveCage(p,ProbePoint.z);
    float3 obstacle=ProbeControl.w>0.0?float3(ProbeControl.w+.0003*(p.x-ProbePoint.x)-.0002*(p.y-ProbePoint.y),.0003,-.0002):float3(0,0,0);
    WaveCageSurface mapped=waveCageSurface(c,p,ProbePoint.z,eta,etaX,etaY,obstacle,ddx(p),ddy(p));
    Result o;o.Vertex=input.Displacement;o.Pixel=float4(mapped.displacement,c.support);
    o.DX=float4(mapped.derivativeX,waveCageMap(c,eta).z*obstacle.x);
    o.DY=float4(mapped.derivativeY,waveCageHeight(p+mapped.displacement.xy)-ProbePoint.z+.035);return o;
}
)HLSL";
	CHECK(SDL3_Shader_Retarget(ps,false));
	std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled){printf("wave cage VS: %s\n",error.c_str());return;}
	unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);CHECK(samplers==4&&buffers==3);
	r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);if(!compiled){printf("wave cage PS: %s\n",error.c_str());return;}
	r.ps=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps);if(!r.vs||!r.ps)return;
	SDL_GPUColorTargetDescription colours[4]={};for(auto &c:colours)c.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.target_info.color_target_descriptions=colours;pi.target_info.num_color_targets=4;r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colours[0].format;ti.layer_count_or_depth=ti.num_levels=1;
	ti.width=ti.height=side;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;for(auto &t:r.terrain)t=SDL_CreateGPUTexture(r.device,&ti);
	ti.width=ti.height=1;r.spectrum=SDL_CreateGPUTexture(r.device,&ti);r.zero=SDL_CreateGPUTexture(r.device,&ti);
	ti.width=width;ti.height=height;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;for(auto &t:r.targets)t=SDL_CreateGPUTexture(r.device,&ti);
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;
	si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;r.sampler=SDL_CreateGPUSampler(r.device,&si);
	for(auto *t:r.terrain){CHECK(t);if(!t)return;}for(auto *t:r.targets){CHECK(t);if(!t)return;}
	CHECK(r.spectrum&&r.zero&&r.sampler);if(!r.spectrum||!r.zero||!r.sampler)return;
	std::vector<float> texels(3*plane+8,0);
	for(unsigned a=0;a<3;++a)for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
		const double px=x*10,py=y*10;float *t=&texels[a*plane+(y*side+x)*4];
		t[0]=float((20+.03*px+.02*py+.0001*px*py)/100);t[1]=float(.6+.001*px-.0005*py);t[2]=float(.45+.0001*px);t[3]=a==0?1:a==1?0:.4f;
	}
	texels[3*plane]=.025f;texels[3*plane+1]=.12f;texels[3*plane+2]=-.04f;
	SDL_GPUTransferBufferCreateInfo tb={};tb.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;tb.size=texels.size()*sizeof(float);
	SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(r.device,&tb);CHECK(transfer);if(!transfer)return;
	void *data=SDL_MapGPUTransferBuffer(r.device,transfer,false);CHECK(data);if(!data){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
	memcpy(data,texels.data(),tb.size);SDL_UnmapGPUTransferBuffer(r.device,transfer);CHECK(frame->Flush());
	SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(r.device);CHECK(upload);if(!upload){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
	SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);
	for(unsigned i=0;i<5;++i){SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.offset=(i<3?i*plane:3*plane+(i-3)*4)*sizeof(float);from.pixels_per_row=from.rows_per_layer=i<3?side:1;
		SDL_GPUTextureRegion to={};to.texture=i<3?r.terrain[i]:i==3?r.spectrum:r.zero;to.w=to.h=i<3?side:1;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);}
	SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(r.device,transfer);
	auto sample=[&](unsigned channel,double x,double y){
		const double gx=(std::max)(0.0,(std::min)(double(side-1),x/10)),gy=(std::max)(0.0,(std::min)(double(side-1),y/10));
		const unsigned ix=unsigned(gx),iy=unsigned(gy),jx=(std::min)(ix+1,side-1),jy=(std::min)(iy+1,side-1);
		return (texels[(iy*side+ix)*4+channel]*(1-(gx-ix))+texels[(iy*side+jx)*4+channel]*(gx-ix))*(1-(gy-iy))+
			(texels[(jy*side+ix)*4+channel]*(1-(gx-ix))+texels[(jy*side+jx)*4+channel]*(gx-ix))*(gy-iy);
	};
	using V=std::array<double,3>;
	const double bound=WaterFftGeometry::MaxVerticalDisplacement,horizontal=WaterFftGeometry::MaxHorizontalDisplacement/std::sqrt(2.0);
	const V incident={horizontal*std::tanh(-1.2*WaterFftSampling::WorldScale*texels[3*plane]/horizontal),
		horizontal*std::tanh(-1.2*WaterFftSampling::WorldScale*texels[3*plane+2]/horizontal),bound*std::tanh(WaterFftSampling::WorldScale*texels[3*plane+1]/bound)};
	SdlVertexConstants fixed={};SdlWaterPixelConstants water={};for(unsigned i=0;i<4;++i){fixed.WorldViewProjection[i*5]=fixed.WorldView[i*5]=fixed.NormalTransform[i*5]=1;for(unsigned j=0;j<MAXIMUM_VERTEX_STAGES;++j)fixed.TextureMatrix[j][i*5]=1;}
	water.TerrainParameters[1]=side*10;water.TerrainParameters[2]=100;water.FftParameters[0]=1;
	struct Fixture {float point[4],control[4],eta[4];};
	// Only the final floor probe uses non-dyadic texture coordinates, because its
	// XY includes the nonlinear displacement. Bound that filtered height separately
	// using Vulkan's minimum four subtexel bits and the uploaded adjacent differences.
	// Ellipsoid values, zero-obstacle guards, stage agreement and the projection law
	// still use strict tolerances; this bound cannot hide an omitted floor correction.
	double floorFilterBound=0;
	for(unsigned y=0;y+1<side;++y)for(unsigned x=0;x+1<side;++x){const double h=texels[(y*side+x)*4];
		floorFilterBound=(std::max)(floorFilterBound,100*(std::abs(texels[(y*side+x+1)*4]-h)+std::abs(texels[((y+1)*side+x)*4]-h))/16);}
	floorFilterBound+=.00001;
	double maxVertex=0,maxValue=0,maxDerivative=0,maxFloor=0,maxProjection=0,maxStage=0,armed=0;unsigned checks=0;
	for(unsigned mode=0;mode<11;++mode){
		Fixture f={{35,45,0,.4f},{1,.04f,0,.7f},{}};f.point[2]=float(sample(0,35,45)*100+(mode==2||mode==10?0:mode==0?2:12));
		if(mode==1){f.control[0]=.45f;f.control[1]=.6f;f.control[2]=.65f;}if(mode>=9)f.control[3]=0;
		for(unsigned c=0;c<3;++c)f.eta[c]=mode==3?0:float(incident[c]);
		water.TerrainParameters[3]=mode==5?0:1;water.OpticalParameters[3]=mode==6?0:1;water.WaterParameters[1]=mode==7?1:0;
		const unsigned field=mode==4?1:mode==8?2:0;const bool active=mode!=4&&mode!=5&&mode!=6&&mode!=7;
		const double support=active?(mode==8?double(.4f):1):0;
		auto mapped=[&](double x,double y,bool varying,bool project=true){
			const double dx=x-f.point[0],dy=y-f.point[1];V eta={f.eta[0],f.eta[1],f.eta[2]};
			if(varying){eta[0]+=.07*dx-.02*dy;eta[1]+=-.03*dx+.04*dy;eta[2]+=.05*dx-.06*dy;}
			const double gx=5*(sample(0,x+10,y)-sample(0,x-10,y)),gy=5*(sample(0,x,y+10)-sample(0,x,y-10)),nl=std::sqrt(1+gx*gx+gy*gy);
			const V n={-gx/nl,-gy/nl,1/nl};const double rt=active?sample(1,x,y)*bound:0,rn=active?(std::min)(sample(2,x,y)*bound,std::abs(f.point[2]-sample(0,x,y)*100)/nl):0;
			const double length=std::sqrt(eta[0]*eta[0]+eta[1]*eta[1]+eta[2]*eta[2]),scale=length?std::tanh(length/bound)/length:1/bound;
			const double along=(eta[0]*n[0]+eta[1]*n[1]+eta[2]*n[2])*scale,obstacle=f.control[3]>0?f.control[3]+(varying?.0003*dx-.0002*dy:0):0;
			V out;for(unsigned c=0;c<3;++c)out[c]=(rt*(eta[c]*scale-along*n[c])+rn*along*n[c])*obstacle;
			if(project&&active&&obstacle>0)out[2]=(std::min)(bound,(std::max)(out[2],sample(0,x+out[0],y+out[1])*100-f.point[2]+.035));return out;
		};
		V expected=mapped(f.point[0],f.point[1],false);for(unsigned c=0;c<3;++c){const double legacy=c==2?f.point[3]*(1-f.control[0])+f.eta[c]*f.control[1]*f.control[0]:f.eta[c]*f.control[0]*f.control[2]*f.control[1];
			expected[c]=legacy*(1-support)+(c==2?f.point[3]*(1-f.control[0])+expected[c]*f.control[0]:expected[c]*f.control[0])*support;
			if(mode==0)armed=(std::max)(armed,std::abs(expected[c]-legacy));}
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
		SDL_GPUColorTargetInfo attachments[4]={};for(unsigned i=0;i<4;++i){attachments[i].texture=r.targets[i];attachments[i].load_op=SDL_GPU_LOADOP_CLEAR;attachments[i].store_op=SDL_GPU_STOREOP_STORE;}
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,attachments,4,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,r.pipeline);
		SDL_GPUTextureSamplerBinding vb[4]={{mode==3?r.zero:r.spectrum,r.sampler},{r.zero,r.sampler},{r.zero,r.sampler},{r.terrain[field],r.sampler}};
		SDL_GPUTextureSamplerBinding pb[3]={{r.zero,r.sampler},{r.zero,r.sampler},{r.terrain[field],r.sampler}};
		SDL_BindGPUVertexSamplers(pass,0,vb,4);SDL_BindGPUFragmentSamplers(pass,0,pb,3);
		SDL_PushGPUVertexUniformData(commands,0,&fixed,sizeof(fixed));SDL_PushGPUVertexUniformData(commands,1,&water,sizeof(water));SDL_PushGPUVertexUniformData(commands,2,&f,sizeof(f));
		SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));SDL_PushGPUFragmentUniformData(commands,1,&f,sizeof(f));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
		const auto values=read_float_targets(frame,r.targets,4,width,height);CHECK(values.size()==width*height*16);if(values.size()!=width*height*16)return;
		auto value=[&](unsigned target,unsigned x,unsigned y,unsigned c){return double(values[((target*height+y)*width+x)*4+c]);};
		double caseVertex=0,caseValue=0,caseDerivative=0;
		for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const double px=f.point[0]+(x-4.0)*.625,py=f.point[1]+(y-4.0)*.625;const V pixel=mapped(px,py,true),unprojected=mapped(px,py,true,false);
			for(unsigned c=0;c<3;++c){const double ve=std::abs(value(0,x,y,c)-expected[c]),pe=std::abs(value(1,x,y,c)-pixel[c]);
				if(mode==2&&c==2)maxFloor=(std::max)(maxFloor,(std::max)(ve,pe));
				else {caseVertex=(std::max)(caseVertex,ve);caseValue=(std::max)(caseValue,pe);}}
			caseValue=(std::max)(caseValue,std::abs(value(2,x,y,3)-unprojected[2]));
			if(mode==2)maxProjection=(std::max)(maxProjection,std::abs(value(1,x,y,2)-(std::min)(bound,(std::max)(value(2,x,y,3),value(3,x,y,3)))));
			CHECK(std::abs(value(1,x,y,3)-support)<.00001);
			// The exact waterline has an abs-depth cusp. Check its positions and floor
			// clearance; differentiability is exercised on either smooth side instead.
			if(active&&mode!=2)for(unsigned axis=0;axis<2;++axis){const double e=.001;const V a=mapped(px+(axis==0?e:0),py+(axis==1?e:0),true),b=mapped(px-(axis==0?e:0),py-(axis==1?e:0),true);
				for(unsigned c=0;c<3;++c)caseDerivative=(std::max)(caseDerivative,std::abs(value(2+axis,x,y,c)-(a[c]-b[c])/(2*e)));++checks;}
		}
		if(f.control[0]==1)for(unsigned c=0;c<3;++c)maxStage=(std::max)(maxStage,std::abs(value(0,4,4,c)-value(1,4,4,c)*support-f.eta[c]*f.control[1]*(1-support)*(c==2?1:f.control[2])));
		if(mode==2)CHECK(value(1,4,4,2)-value(2,4,4,3)>.02); // Armed: removing floor projection fails despite its sampling bound.
		if(caseVertex>=.0001||caseValue>=.0001||caseDerivative>=.003)printf("wave cage case%u: vertex %.9f, value %.9f, derivative %.9f, center vertex/pixel z %.9f/%.9f, before-floor %.9f\n",mode,caseVertex,caseValue,caseDerivative,value(0,4,4,2),value(1,4,4,2),value(2,4,4,3));
		maxVertex=(std::max)(maxVertex,caseVertex);maxValue=(std::max)(maxValue,caseValue);maxDerivative=(std::max)(maxDerivative,caseDerivative);
		for(float v:values)CHECK(std::isfinite(v));
	}
	CHECK(maxVertex<.0001&&maxValue<.0001&&maxDerivative<.0002&&maxStage<.00001&&maxProjection<.00001&&maxFloor<floorFilterBound);CHECK(armed>.1&&checks>500);
	printf("sdl_gpu_frame_selfcheck: GPU wave cages: VS3/PS2 real wrapper %.8f, independent map %.8f, full spatial derivative %.8f (%u checks), stage %.8f/projection %.8f/floor filter %.8f bound %.8f, armed legacy coast delta %.5f; waterline, zero, partial support, obstacle and four fallback guards checked\n",maxVertex,maxValue,maxDerivative,checks,maxStage,maxProjection,maxFloor,floorFilterBound,armed);
}

#if defined(__linux__)
static void check_gpu_water_hull_normal(SdlGpuFrame *frame)
{
	// A constant spectral height has zero spectral slope. Its production normal is therefore
	// independently the derivative of its reflected height and clearance envelopes. A coarse quad's
	// diagonal must not change that normal; the old interpolated-envelope control must change it.
	const unsigned width=96,height=32;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL,*white=NULL,*zero=NULL,*displacement=NULL;SDL_GPUSampler *sampler=NULL;
		~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vertex)SDL_ReleaseGPUShader(device,vertex);
			if(pixel)SDL_ReleaseGPUShader(device,pixel);for(auto *t:{target,white,zero,displacement})if(t)SDL_ReleaseGPUTexture(device,t);
			if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} resources;
	resources.device=frame->Device();
	PixelPipelineDescription pd={};std::string ps;const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,ps,COMBINER_SHADER_TARGET_SDL3_GPU);
	CHECK(generated);if(!generated)return;
	if(!observe_water_normal(ps)) return;
	VertexPipelineDescription vd={};vd.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;vd.StageCount=4;
	vd.Stages[0].TextureCoordinateIndex=0;vd.Stages[1].TextureCoordinateIndex=1;
	std::string vs;CHECK(VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,vs));CHECK(WaterFftSampling::WrapVertex(vs,true));
	CHECK(SDL3_Shader_Retarget(vs,true));
	const std::string entry="Output main(Input input)";const size_t at=vs.find(entry);CHECK(at!=std::string::npos);if(at==std::string::npos)return;
	vs.replace(at,entry.size(),"Output hullFixtureWaterVertex(Input input)");
	vs+=R"HLSL(
cbuffer Fixture:register(b2,space1) {float4 FixtureControls;};
Output main(uint id:SV_VertexID) {
    // Two diagonals through the same asymmetric quad. A corner and its nearby straight hull
    // edge both cross the domain, so interpolating the old envelope is measurably non-planar.
    uint c;
    if(FixtureControls.x<0.5) c=id==0?0:id==1?1:id==2?2:id==3?0:id==4?2:3;
    else c=id==0?0:id==1?1:id==2?3:id==3?1:id==4?2:3;
    float2 p=float2(c==0||c==3?-24.0:28.0,c<2?4.0:17.0);
    Input input=(Input)0;input.Position=float3(p,0.5);input.Diffuse=float4(0,0,0,1);
    input.Normal=float3(FixtureControls.z,FixtureControls.w,1);
    if(FixtureControls.y>0.5) {
        input.Normal.y=min(input.Normal.y,vesselHullDamping(p,0.5).x);
        input.Normal.z=0; // armed pre-fix path: transport the hull-combined vertex damping
    }
    input.TexCoord1=float2(0,0.5);
    Output o=hullFixtureWaterVertex(input);
    // Keep an exact parameter-space atlas after executing the production vertex wrapper.
    // Its displacement was checked separately; its actual Specular/TC2 transport stays intact.
    o.Position=float4((p.x-2.0)/26.0,(p.y-10.5)/6.5,0.5,1);
    return o;
}
)HLSL";
	std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull normal VS: %s\n",error.c_str());return;}
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull normal PS: %s\n",error.c_str());return;}
	unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);CHECK(samplers<=16 && uniforms==(4u));
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=width;ti.height=height;ti.layer_count_or_depth=ti.num_levels=1;resources.target=SDL_CreateGPUTexture(resources.device,&ti);
	ti.width=ti.height=1;ti.usage|=SDL_GPU_TEXTUREUSAGE_SAMPLER;
	resources.white=SDL_CreateGPUTexture(resources.device,&ti);resources.zero=SDL_CreateGPUTexture(resources.device,&ti);resources.displacement=SDL_CreateGPUTexture(resources.device,&ti);
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;resources.sampler=SDL_CreateGPUSampler(resources.device,&si);
	CHECK(resources.target && resources.white && resources.zero && resources.displacement && resources.sampler);
	if(!resources.target || !resources.white || !resources.zero || !resources.displacement || !resources.sampler)return;
	SdlVertexConstants vertex={};SdlWaterPixelConstants water={};
	for(unsigned i=0;i<4;++i) {
		vertex.WorldViewProjection[i*5]=vertex.WorldView[i*5]=vertex.NormalTransform[i*5]=1;water.WorldFromClip[i*5]=1;
		for(unsigned stage=0;stage<MAXIMUM_VERTEX_STAGES;++stage)vertex.TextureMatrix[stage][i*5]=1;
	}
	water.WorldFromClip[0]=26;water.WorldFromClip[5]=6.5f;water.WorldFromClip[12]=2;water.WorldFromClip[13]=10.5f;
	water.Viewport[2]=width;water.Viewport[3]=height;water.CameraPosition[2]=100;
	water.SunDirection[2]=1;water.SunColour[0]=water.SunColour[1]=water.SunColour[2]=1;
	water.TerrainParameters[1]=100;water.SceneParameters[1]=water.SceneParameters[2]=1;
	water.OpticalParameters[0]=3;water.OpticalParameters[1]=water.OpticalParameters[2]=1;
	water.FftParameters[0]=1;
	water.HullParameters[0]=1;water.HullData[0][2]=.5f;water.HullData[0][3]=4;
	water.HullData[1][0]=1;water.HullData[1][2]=20;water.HullData[1][3]=6;
	const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(water.HullData[4],outline,sizeof(outline));
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&](unsigned diagonal,bool coarse,float weight,float freeDamping) {
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
		for(unsigned i=0;i<3;++i) {
			attachment.texture=i==0?resources.white:i==1?resources.zero:resources.displacement;
			attachment.clear_color={i==0?1.0f:0.0f,i==0?1.0f:i==2?.05f:0.0f,i==0?1.0f:0.0f,i==0?1.0f:0.0f};
			SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_EndGPURenderPass(pass);
		}
		attachment.texture=resources.target;attachment.clear_color={0,0,0,0};
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
		SDL_GPUTextureSamplerBinding bindings[16]={};for(unsigned i=0;i<samplers;++i) {
			bindings[i].texture=i<8?resources.white:i==8?resources.displacement:resources.zero;bindings[i].sampler=resources.sampler;
		}
		SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
		SDL_GPUTextureSamplerBinding vertexBindings[4]={{resources.displacement,resources.sampler},{resources.zero,resources.sampler},{resources.zero,resources.sampler},{resources.zero,resources.sampler}};
		SDL_BindGPUVertexSamplers(pass,0,vertexBindings,4); // TerrainParameters.w=0 preserves this hull-only fixture.
		const float controls[4]={float(diagonal),coarse?1.0f:0.0f,weight,freeDamping};
		SDL_PushGPUVertexUniformData(commands,0,&vertex,sizeof(vertex));SDL_PushGPUVertexUniformData(commands,1,&water,sizeof(water));
		SDL_PushGPUVertexUniformData(commands,2,controls,sizeof(controls));push_empty_visual_water(commands,water);
		SDL_DrawGPUPrimitives(pass,6,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
		return read_float_targets(frame,&resources.target,1,width,height);
	};
	const auto first=render(0,false,1,1),second=render(1,false,1,1);
	const auto coarseFirst=render(0,true,1,1),coarseSecond=render(1,true,1,1);
	const auto seam=render(0,false,0,1),floor=render(1,false,1,WaterFftGeometry::SafeDamping(1,2.25f));
	CHECK(first.size()==width*height*4 && second.size()==first.size() && coarseFirst.size()==first.size() && coarseSecond.size()==first.size() && seam.size()==first.size() && floor.size()==first.size());
	if(first.size()!=width*height*4 || second.size()!=first.size() || coarseFirst.size()!=first.size() || coarseSecond.size()!=first.size() || seam.size()!=first.size() || floor.size()!=first.size())return;
    auto surfaceHeight=[](double x,double y,double freeDamping) {
        const double dx=x<-20?x+20:x>20?x-20:0,dy=y>6?y-6:y<-6?y+6:0;
        const double distance=std::hypot(dx,dy),t=std::min(1.0,distance/9);
        const double damping=std::min(freeDamping,t*t*(3-2*t));
        double reflected=0;
        if(distance>.0001 && distance<28) {
            const double r=distance/28,fade=1-r*r*(3-2*r);
            reflected=.68*fade*fade;
        }
        const double bound=WaterFftGeometry::MaxVerticalDisplacement;
        return bound*std::tanh((1+reflected)/bound)*damping;
    };
    auto scalarNormal=[&](double x,double y,double freeDamping) {
        const double step=.0001;
        const double gx=(surfaceHeight(x+step,y,freeDamping)-surfaceHeight(x-step,y,freeDamping))/(2*step);
        const double gy=(surfaceHeight(x,y+step,freeDamping)-surfaceHeight(x,y-step,freeDamping))/(2*step);
        const double length=std::sqrt(1+gx*gx+gy*gy);
        return std::array<double,3>{-gx/length,-gy/length,1/length};
    };
	double maximumError=0,maximumDiagonal=0,maximumControl=0,maximumSeam=0,maximumFloor=0;unsigned controls=0,active=0;
	for(unsigned y=1;y<height-1;++y)for(unsigned x=1;x<width-1;++x) {
		const double px=-24+52*(x+.5)/width,py=17-13*(y+.5)/height;
        const auto normal=scalarNormal(px,py,1);
        const auto floorNormal=scalarNormal(px,py,WaterFftGeometry::SafeDamping(1,2.25f));
		for(unsigned c=0;c<3;++c) {
			const unsigned offset=(y*width+x)*4+c;
			const double error=std::abs(first[offset]-(.5+.5*normal[c]));maximumError=(std::max)(maximumError,error);
			maximumDiagonal=(std::max)(maximumDiagonal,double(std::abs(first[offset]-second[offset])));
			const double control=std::abs(coarseFirst[offset]-coarseSecond[offset]);maximumControl=(std::max)(maximumControl,control);controls+=control>.005;
			maximumSeam=(std::max)(maximumSeam,std::abs(seam[offset]-(c==2?1.0:.5)));
			const double expected=.5+.5*floorNormal[c];maximumFloor=(std::max)(maximumFloor,std::abs(floor[offset]-expected));
		}
		active+=std::abs(normal[0])+std::abs(normal[1])>.08;
	}
	for(const auto *image:{&first,&second,&coarseFirst,&coarseSecond,&seam,&floor})for(float v:*image)CHECK(std::isfinite(v));
	CHECK(maximumError<.0001 && maximumDiagonal<.0001 && maximumFloor<.0001 && maximumSeam<.0001);
	CHECK(active>200 && maximumControl>.01 && controls>100);
	printf("sdl_gpu_frame_selfcheck: GPU production hull normal: %u renders, independent normal %.8f/diagonal %.8f/floor %.8f/zero-weight %.8f; armed interpolated damping diagonal error %.8f (%u components), composed reflected-wave clearance checked\n",renders,maximumError,maximumDiagonal,maximumFloor,maximumSeam,maximumControl,controls);
}
#endif // __linux__

#include "WaterBowShoulderTest.h"
static void check_gpu_water_bow_shoulder(SdlGpuFrame *frame)
{
    namespace T=WaterBowShoulderTest;using WaterHullModel::Hull;
    Hull hulls[8];hulls[0]=T::hull();hulls[1]=T::hull(-40);hulls[2]=T::hull(40,.63);
    hulls[3]=T::hull();hulls[3].data[2][0]=0;hulls[3].data[2][1]=40;
    hulls[4]=T::hull(.3);hulls[5]=T::hull();hulls[5].data[0][3]=0;
    hulls[6]=T::hull();hulls[6].data[0][3]=13;hulls[7]=T::scaledHull(280,54);
    struct Fixture {float x,y,level,h;};std::vector<Fixture> fixtures;
    auto add=[&](unsigned h,float x,float y,float level=4) {
        const auto &v=hulls[h];fixtures.push_back({v.data[0][0]+x*v.data[1][0]-y*v.data[1][1],
            v.data[0][1]+x*v.data[1][1]+y*v.data[1][0],level,float(h)});
    };
    for(unsigned h=0;h<8;++h){add(h,42.025f,0);add(h,33,-7.5f);add(h,-42.025f,0);add(h,0,11.025f);}
    add(0,42.025f,0,5);add(0,55,0);add(0,40,0);add(0,42.025f,0); // wrong level/compact/exact vertex/repeat
    add(7,286,0);add(7,292,0);add(7,298,0);add(7,322,0); // carrier cap/wing/derivative/compact
    const unsigned rows=unsigned(fixtures.size()),columns=12;
    std::string common="cbuffer BowShoulderFixtures : register(b0) {float4 HullData[80];float4 Fixture["+std::to_string(rows)+"];};\nstatic const float4 HullParameters=float4(8,0,0,0);\n";
    common+="static const float4 ContactHullData[80]={0};\n";
    common+=WaterFftSampling::CommonSource()+WaterVesselWaves::Source();
    common+=R"HLSL(
float3 shoulderEval(uint row,uint col) {
    float4 f=Fixture[row];int h=(int)f.w;float2 p=f.xy;
    if(col>=6&&col<=9)p+=col==6?float2(.015625,0):col==7?float2(-.015625,0):col==8?float2(0,.015625):float2(0,-.015625);
    float3 direct=vesselGeneratedBow(p,f.z,h),oldBow;
    float3 old=vesselGeneratedWave(p,f.z,4.0,h,oldBow);
    if(col==0)return direct;
    if(col==4)return old;
    float phase=.013*p.x-.017*p.y;
    float3 raw=float3(2.0*sin(.07*p.x+.025*p.y),1.5*cos(.035*p.x-.045*p.y),0);
    float3 rawX=float3(.14*cos(.07*p.x+.025*p.y),-.0525*sin(.035*p.x-.045*p.y),0);
    float3 rawY=float3(.05*cos(.07*p.x+.025*p.y),.0675*sin(.035*p.x-.045*p.y),0);
    float3 scale=float3(.5+.1*sin(phase),.0013*cos(phase),-.0017*cos(phase));
    if(col==3){raw=0;rawX=0;rawY=0;}
    FftHorizontalRegistration map=fftHorizontalRegistration(p,raw,rawX,rawY,scale);
    float3 mapped=vesselGeneratedBow(map.position,f.z,h);
    mapped.yz=float2(dot(mapped.yz,map.derivativeX),dot(mapped.yz,map.derivativeY));
    float3 result=old+mapped-oldBow;
    if(col==2)return (result-mapped)-(old-oldBow);
    if(col==5)return result-mapped; // unchanged stern, including time/phase
    if(col==10)return mapped;
    return result;
}
)HLSL";
    std::string vs=common+
        "struct Output {float4 position:SV_Position;nointerpolation float3 value:TEXCOORD0;};\n"
        "Output main(uint id:SV_VertexID){uint cell=id/6,row=cell/"+std::to_string(columns)+",col=cell%"+std::to_string(columns)+";"
        "uint v=id%6;float2 corner=v==0?float2(0,0):v==1?float2(1,0):v==2?float2(0,1):v==3?float2(0,1):v==4?float2(1,0):float2(1,1);"
        "Output o;o.position=float4((float2(col,row)+corner)/float2("+std::to_string(columns)+","+std::to_string(rows)+")*float2(2,-2)+float2(-1,1),0,1);o.value=shoulderEval(row,col);return o;}";
    std::string ps=common+R"HLSL(
float4 main(float4 p:SV_Position,nointerpolation float3 value:TEXCOORD0):SV_Target0 {
    uint row=(uint)p.y,col=(uint)p.x;float3 result=shoulderEval(row,col);
    return float4(col==11?abs(result-value):result,0);
})HLSL";
    const bool placed=sdl3target_detail::place_registers(vs,true)&&sdl3target_detail::place_registers(ps,false);CHECK(placed);if(!placed)return;
    std::vector<unsigned char> spirv;std::string error;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled){printf("shoulder VS: %s\n",error.c_str());return;}
    SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);CHECK(vertex);if(!vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
    if(!compiled){printf("shoulder PS: %s\n",error.c_str());SDL_ReleaseGPUShader(frame->Device(),vertex);return;}
    SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(pixel);
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=vertex;info.fragment_shader=pixel;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
    SDL_GPUGraphicsPipeline *pipeline=pixel?SDL_CreateGPUGraphicsPipeline(frame->Device(),&info):NULL;CHECK(pipeline);
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&ti);CHECK(target);
    if(pipeline&&target) {
        std::vector<float> uniforms(80*4+rows*4);memcpy(uniforms.data(),hulls,sizeof(hulls));memcpy(uniforms.data()+80*4,fixtures.data(),fixtures.size()*sizeof(Fixture));
        CHECK(uniforms.size()*sizeof(float)<=4096);CHECK(frame->Flush());
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
        if(commands){SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
            if(pass){SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUVertexUniformData(commands,0,uniforms.data(),uniforms.size()*sizeof(float));SDL_PushGPUFragmentUniformData(commands,0,uniforms.data(),uniforms.size()*sizeof(float));
                SDL_DrawGPUPrimitives(pass,rows*columns*6,1,0,0);SDL_EndGPURenderPass(pass);}
            CHECK(SDL_SubmitGPUCommandBuffer(commands));
            const auto pixels=read_float_targets(frame,&target,1,columns,rows);
            auto at=[&](unsigned r,unsigned c,unsigned k){return double(pixels[(r*columns+c)*4+k]);};
            double maxValue=0,maxDerivative=0,maxParity=0,maxStern=0;unsigned armed=0;
            auto scalar=[&](unsigned row,double x,double y,bool mapped,bool stern) {
                const auto &f=fixtures[row];const auto &h=hulls[unsigned(f.h)];
                if(h.data[0][3]<3||std::abs(f.level-h.data[0][2])>.1)return 0.;
                double wake=0;vessel_test_packet(h,x,y,f.level,4,&wake);
                if(h.data[0][3]>12)return stern?wake:0.;
                if(mapped){double phase=.013*x-.017*y,scale=.5+.1*std::sin(phase),bound=8/std::sqrt(2.);
                    double px=x+scale*bound*std::tanh(2*std::sin(.07*x+.025*y)/bound),py=y+scale*bound*std::tanh(1.5*std::cos(.035*x-.045*y)/bound);x=px;y=py;}
                return T::oracle(h,x,y)+(stern?wake:0);
            };
            for(unsigned r=0;r<rows;++r){const auto &f=fixtures[r];const auto &h=hulls[unsigned(f.h)];
                for(unsigned col=0;col<columns;++col)for(unsigned c=0;c<4;++c)CHECK(std::isfinite(at(r,col,c)));
                for(unsigned col:{0u,1u,3u}) {
                    const double expected=scalar(r,f.x,f.y,col==1,col!=0),error=std::abs(at(r,col,0)-expected);maxValue=(std::max)(maxValue,error);CHECK(error<.00003);
                    // Exact vertex has a legitimate derivative crease, tested for finite value only.
                    if(r!=34)for(unsigned k=0;k<2;++k){const double e=.0001;double plus=scalar(r,f.x+(k==0?e:0),f.y+(k==1?e:0),col==1,col!=0),minus=scalar(r,f.x-(k==0?e:0),f.y-(k==1?e:0),col==1,col!=0);
                        const double diff=std::abs(at(r,col,k+1)-(plus-minus)/(2*e));maxDerivative=(std::max)(maxDerivative,diff);CHECK(diff<.00006);}
                }
                double stern=0;vessel_test_packet(h,f.x,f.y,f.level,4,&stern);
                // Invalid-count row6 is explicitly unsupported for shoulder; legacy packet
                // itself had no upper-count check, so exclude it from its unrelated oracle.
                if(h.data[0][3]<=12){maxStern=(std::max)(maxStern,std::abs(at(r,5,0)-stern));CHECK(std::abs(at(r,5,0)-stern)<.00003);}
                for(unsigned c=0;c<3;++c){CHECK(std::abs(at(r,2,c))<.000004);maxParity=(std::max)(maxParity,at(r,11,c));CHECK(at(r,11,c)<.000003);}
                if(at(r,0,0)>.1)++armed;
            }
            CHECK(armed>=6);CHECK(at(0,0,0)>.1&&at(1,0,0)>.1&&at(2,0,0)==0);
            for(unsigned r=16;r<28;++r)CHECK(at(r,0,0)==0);
            CHECK(at(32,0,0)==0&&at(33,0,0)==0);
            CHECK(at(36,0,0)>5 && at(37,0,0)>4 && at(38,0,0)>1 && at(39,0,0)==0);
            const auto &wide=fixtures[37];
            CHECK(at(37,0,0)-T::oracle(hulls[7],wide.x,wide.y,6)>3); // armed old-cap control
            for(unsigned c=0;c<3;++c)CHECK(at(0,0,c)==at(35,0,c));
            printf("sdl_gpu_frame_selfcheck: GPU contour shoulder %u rows: scalar %.8g, independent derivative %.8g, VS/PS %.8g, stern %.8g; rest/reverse/broadside/rotated/unsupported/compact/zerochop/pause checked\n",rows,maxValue,maxDerivative,maxParity,maxStern);
        }
    }
    if(target)SDL_ReleaseGPUTexture(frame->Device(),target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
    if(pixel)SDL_ReleaseGPUShader(frame->Device(),pixel);SDL_ReleaseGPUShader(frame->Device(),vertex);
}

static void check_gpu_water_bow_refinement(SdlGpuFrame *frame)
{
	// Exercise the actual nonlinear water vertex wrapper over production subdivision output.
	// The independent raster oracle uses uploaded texels, fresh world-space metadata and affine
	// barycentrics. It does not copy the refinement constructor or the vertex shader source.
	using namespace WaterBowRefinement;
	const unsigned width=80,height=64,side=32,maxNodes=32,maxIndices=128;
	struct Fixture {float vertex[maxNodes][3][4];uint32_t indices[maxIndices];};
	static_assert(sizeof(Fixture)==2048,"SDL Vulkan uniform section bound");
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs=NULL,*ps=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *field=NULL,*zero=NULL,*targets[2]={};SDL_GPUSampler *sampler=NULL;
		~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vs)SDL_ReleaseGPUShader(device,vs);
			if(ps)SDL_ReleaseGPUShader(device,ps);for(auto *t:{field,zero,targets[0],targets[1]})if(t)SDL_ReleaseGPUTexture(device,t);
			if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} r;r.device=frame->Device();
	VertexPipelineDescription vd={};vd.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;vd.StageCount=4;
	vd.Stages[0].TextureCoordinateIndex=0;vd.Stages[1].TextureCoordinateIndex=1;
	std::string vs;CHECK(VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,vs));CHECK(WaterFftSampling::WrapVertex(vs,true));
	// Retarget the actual Input entry before replacing it with the procedural fixture.
	CHECK(SDL3_Shader_Retarget(vs,true));
	const std::string entry="Output main(Input input)";const size_t at=vs.find(entry);CHECK(at!=std::string::npos);if(at==std::string::npos)return;
	vs.replace(at,entry.size(),"Output refinementWaterVertex(Input input)");
	vs+=R"HLSL(
cbuffer Fixture : register(b2,space1) {float4 FixtureVertex[96];uint4 FixtureIndex[32];};
struct ProbeOutput {float4 Position:SV_Position;float4 Parameter:TEXCOORD0;float4 Metadata:TEXCOORD1;};
ProbeOutput main(uint id:SV_VertexID) {
    uint v=FixtureIndex[id/4][id%4]*3;
    Input input=(Input)0;input.Position=FixtureVertex[v].xyz;input.Normal=FixtureVertex[v+1].xyz;
    input.TexCoord0=FixtureVertex[v+2].xy;input.TexCoord1=FixtureVertex[v+2].zw;input.Diffuse=float4(0,0,0,1);
    Output displaced=refinementWaterVertex(input);ProbeOutput o;o.Position=displaced.Position;
    o.Parameter=float4(displaced.TexCoord2,displaced.Specular.z,1);
    o.Metadata=float4(displaced.Specular.x,displaced.Specular.y,displaced.Specular.w,0);return o;
}
)HLSL";
	std::string ps=R"HLSL(
struct ProbeOutput {float4 Position:SV_Position;float4 Parameter:TEXCOORD0;float4 Metadata:TEXCOORD1;};
struct Result {float4 Parameter:SV_Target0;float4 Metadata:SV_Target1;};
Result main(ProbeOutput input) {Result o;o.Parameter=input.Parameter;o.Metadata=float4(input.Metadata.xyz,input.Position.z);return o;}
)HLSL";
	CHECK(SDL3_Shader_Retarget(ps,false));
	std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled){printf("bow refinement VS: %s\n",error.c_str());return;}
	r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);if(!compiled){printf("bow refinement PS: %s\n",error.c_str());return;}
	r.ps=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps);if(!r.vs||!r.ps)return;
	SDL_GPUColorTargetDescription colours[2]={};for(auto &c:colours)c.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.target_info.color_target_descriptions=colours;pi.target_info.num_color_targets=2;r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colours[0].format;ti.layer_count_or_depth=ti.num_levels=1;
	ti.width=ti.height=side;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;r.field=SDL_CreateGPUTexture(r.device,&ti);r.zero=SDL_CreateGPUTexture(r.device,&ti);
	ti.width=width;ti.height=height;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;for(auto &t:r.targets)t=SDL_CreateGPUTexture(r.device,&ti);
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	r.sampler=SDL_CreateGPUSampler(r.device,&si);CHECK(r.field&&r.zero&&r.targets[0]&&r.targets[1]&&r.sampler);if(!r.field||!r.zero||!r.targets[0]||!r.targets[1]||!r.sampler)return;
	std::vector<float> texels(side*side*4*2,0);
	for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
		const double u=2*WaterFftSpectrum::Pi*(x+.5)/side,v=2*WaterFftSpectrum::Pi*(y+.5)/side;
		float *p=&texels[(y*side+x)*4];p[0]=float(.055*std::sin(u+.4)*std::cos(v));p[1]=float(.075*std::cos(2*u-.3)*std::cos(v+.5));p[2]=float(.04*std::cos(u)*std::sin(v-.2));
	}
	SDL_GPUTransferBufferCreateInfo tb={};tb.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;tb.size=texels.size()*sizeof(float);
	SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(r.device,&tb);CHECK(transfer);if(!transfer)return;
	void *mapped=SDL_MapGPUTransferBuffer(r.device,transfer,false);CHECK(mapped);if(!mapped){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
	memcpy(mapped,texels.data(),tb.size);SDL_UnmapGPUTransferBuffer(r.device,transfer);CHECK(frame->Flush());
	SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(r.device);CHECK(upload);if(!upload){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
	SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);
	for(unsigned i=0;i<2;++i){SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.offset=i*side*side*4*sizeof(float);from.pixels_per_row=from.rows_per_layer=side;
		SDL_GPUTextureRegion to={};to.texture=i?r.zero:r.field;to.w=to.h=side;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);}
	SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(r.device,transfer);
	SdlVertexConstants fixed={};SdlWaterPixelConstants water={};
	for(unsigned i=0;i<4;++i){fixed.WorldView[i*5]=fixed.NormalTransform[i*5]=1;for(unsigned j=0;j<MAXIMUM_VERTEX_STAGES;++j)fixed.TextureMatrix[j][i*5]=1;}
	fixed.WorldViewProjection[0]=fixed.WorldViewProjection[5]=1.0f/45;fixed.WorldViewProjection[9]=.018f;fixed.WorldViewProjection[10]=.02f;fixed.WorldViewProjection[14]=.5f;fixed.WorldViewProjection[15]=1;
	water.FftParameters[0]=1;
	WaterProjectedMesh::Projection projection={{1.0/45,0,0,0,1.0/45,0,0,0,1},{0},{0,0,1},.1,10,0};CHECK(projection.Prepare());
	Topology base;
	const double p[4][2]={{-28,-23},{27,-23},{27,25},{-28,25}};
	const int logical[4][2]={{0,0},{4,0},{4,4},{0,4}};
	for(unsigned i=0;i<4;++i){Node n;n.logical={logical[i][0],logical[i][1]};n.world={p[i][0],p[i][1]};CHECK(projection.Transform(n.world,n.screen));n.source=int(i);base.nodes.push_back(n);}
	base.triangles={{0,1,2,1},{0,2,3,1}};
	auto fresh=[](double x,double y,float out[3][4]) {
		memset(out,0,12*sizeof(float));out[0][0]=float(x);out[0][1]=float(y);out[0][2]=.2f+float(.24*std::sin(x*.09)*std::cos(y*.07));
		out[1][0]=float(.46+.15*std::cos(x*.055)*std::cos(y*.04));out[1][1]=float(.38+.00018*(x*x+y*y));out[1][2]=float(.6+.12*std::sin(x*.065-y*.035));
		out[2][1]=float(y/150);out[2][2]=float(.4+.2*std::cos(x*.06+y*.04));out[2][3]=.2f;
	};
	struct V {double sx,sy,value[8];};
	auto displaced=[&](const float input[3][4]) {
		const double x=input[0][0],y=input[0][1],u=x/(13*WaterFftSampling::WorldScale)*side-.5,v=y/(13*WaterFftSampling::WorldScale)*side-.5;
		const int ix=int(std::floor(u)),iy=int(std::floor(v));const double tx=u-ix,ty=v-iy;
		double d[3]={};for(unsigned c=0;c<3;++c)for(int j=0;j<2;++j)for(int i=0;i<2;++i){int xx=(ix+i)%int(side),yy=(iy+j)%int(side);if(xx<0)xx+=side;if(yy<0)yy+=side;d[c]+=texels[(yy*side+xx)*4+c]*(i?tx:1-tx)*(j?ty:1-ty);}
		const double horizontal=WaterFftGeometry::MaxHorizontalDisplacement/std::sqrt(2.0),vertical=WaterFftGeometry::MaxVerticalDisplacement;
		const double dx=horizontal*std::tanh(-1.5*WaterFftSampling::WorldScale*d[0]/horizontal),dy=horizontal*std::tanh(-1.5*WaterFftSampling::WorldScale*d[2]/horizontal),dz=vertical*std::tanh(WaterFftSampling::WorldScale*d[1]/vertical);
		const double weight=input[1][0],damping=input[1][1],old=input[0][2]-input[2][3];
		const double chopWeight=weight*input[1][2];
		const double wx=x+dx*chopWeight*damping,wy=y+dy*chopWeight*damping,wz=input[2][3]+old*(1-weight)+dz*damping*weight;
		V out;out.sx=(wx*fixed.WorldViewProjection[0]+1)*width*.5;out.sy=(1-wy*fixed.WorldViewProjection[5]-wz*fixed.WorldViewProjection[9])*height*.5;
		const double values[8]={x,y,old,1,weight,damping,input[1][2],wz*fixed.WorldViewProjection[10]+fixed.WorldViewProjection[14]};memcpy(out.value,values,sizeof(values));return out;
	};
	// Vulkan Core guarantees at least four subpixel/subtexel precision bits:
	// https://docs.vulkan.org/refpages/latest/refpages/source/Required_Limits.html
	// Use a whole least-significant bit, not an assumed device-specific nearest rounding.
	// The bilinear error is bounded independently by the largest adjacent texel differences;
	// tanh is 1-Lipschitz. These bounds apply only to coverage/XY, never to metadata or depth.
	double filterError[3]={};
	for(unsigned c=0;c<3;++c) {double du=0,dv=0;
		for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
			const double value=texels[(y*side+x)*4+c];
			du=(std::max)(du,std::abs(value-texels[(y*side+(x+1)%side)*4+c]));
			dv=(std::max)(dv,std::abs(value-texels[(((y+1)%side)*side+x)*4+c]));
		}
		filterError[c]=(du+dv)/16;
	}
	const double coverageBoundX=1.0/16+filterError[0]*1.5*WaterFftSampling::WorldScale*width/90+.00001;
	const double coverageBoundY=1.0/16+(filterError[2]*1.5/45+filterError[1]*.018)*WaterFftSampling::WorldScale*height/2+.00001;
	unsigned renders=0,totalStable=0;double maxCoordinateError=0,maxMetadataError=0,maxDepthError=0,maxPixelErrorX=0,maxPixelErrorY=0,maxBound=0;std::vector<float> coarse,zero,nearZero,intermediate,full,stale,locked;
	for(unsigned mode=0;mode<9;++mode) {
		const double amount=mode==0||mode==1?0:mode==2?1e-6:mode==3?.25:mode==4||mode==6?.65:1;
		Topology mesh;std::vector<std::pair<Logical,WaterProjectedMesh::Point>> finals;
		auto weight=[&](unsigned level,Logical midpoint,WaterProjectedMesh::Point finalWorld){finals.push_back({midpoint,finalWorld});return amount;};
		CHECK(Subdivide(base,projection,weight,mesh,mode==0?0:2,mode==7));CHECK(mesh.nodes.size()<=maxNodes&&mesh.triangles.size()*3<=maxIndices);if(mesh.nodes.size()>maxNodes||mesh.triangles.size()*3>maxIndices)return;
		Fixture fixture={};std::vector<V> vertices;
		for(size_t i=0;i<mesh.nodes.size();++i){const Node &n=mesh.nodes[i];fresh(n.world.x,n.world.y,fixture.vertex[i]);
			if(mode==8)fixture.vertex[i][1][2]=0; // Connected R0 shore: FFT height stays active, horizontal chop is zero.
			if(mode==6&&n.source<0){for(const auto &q:finals)if(q.first.x==n.logical.x&&q.first.y==n.logical.y){float bad[3][4];fresh(q.second.x,q.second.y,bad);memcpy(fixture.vertex[i][1],bad[1],4*sizeof(float));break;}}
			if(n.parent>=0) {CHECK(size_t(n.parent)<i);if(mode!=6)CHECK(memcmp(fixture.vertex[i],fixture.vertex[n.parent],sizeof(fixture.vertex[i]))==0);}
			vertices.push_back(displaced(fixture.vertex[i]));}
		unsigned count=0;for(const Triangle &t:mesh.triangles){fixture.indices[count++]=t.a;fixture.indices[count++]=t.b;fixture.indices[count++]=t.c;}
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
		SDL_GPUColorTargetInfo attachments[2]={};for(unsigned i=0;i<2;++i){attachments[i].texture=r.targets[i];attachments[i].load_op=SDL_GPU_LOADOP_CLEAR;attachments[i].store_op=SDL_GPU_STOREOP_STORE;}
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,attachments,2,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,r.pipeline);
		SDL_GPUTextureSamplerBinding bindings[4]={{r.zero,r.sampler},{r.zero,r.sampler},{r.field,r.sampler},{r.zero,r.sampler}};SDL_BindGPUVertexSamplers(pass,0,bindings,4);
		SDL_PushGPUVertexUniformData(commands,0,&fixed,sizeof(fixed));SDL_PushGPUVertexUniformData(commands,1,&water,sizeof(water));SDL_PushGPUVertexUniformData(commands,2,&fixture,sizeof(fixture));
		SDL_DrawGPUPrimitives(pass,count,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
		const auto values=read_float_targets(frame,r.targets,2,width,height);CHECK(values.size()==width*height*8);if(values.size()!=width*height*8)return;
		for(float v:values)CHECK(std::isfinite(v));
		unsigned stable=0,coverage=0;
		for(unsigned py=0;py<height;++py)for(unsigned px=0;px<width;++px){const double x=px+.5,y=py+.5;bool found=false,edge=false;double expected[8]={},parameterExpected[8]={},surfaceX=0,surfaceY=0;
			const unsigned offset=(py*width+px)*4;
			for(const Triangle &t:mesh.triangles){const V &a=vertices[t.a],&b=vertices[t.b],&c=vertices[t.c];const double det=(b.sx-a.sx)*(c.sy-a.sy)-(b.sy-a.sy)*(c.sx-a.sx);if(std::abs(det)<1e-10)continue;
				// Rasterizers quantize edge equations to subpixels. Exclude a fixed 0.15-pixel
				// band on BOTH sides of every segment, rather than forgiving arbitrary mismatches.
				const V *ends[3]={&a,&b,&c};for(unsigned e=0;e<3;++e){const V &lo=*ends[e],&hi=*ends[(e+1)%3];const double dx=hi.sx-lo.sx,dy=hi.sy-lo.sy,len2=dx*dx+dy*dy;
					if(len2>1e-20){const double t=(std::max)(0.0,(std::min)(1.0,((x-lo.sx)*dx+(y-lo.sy)*dy)/len2));edge|=std::hypot(x-lo.sx-t*dx,y-lo.sy-t*dy)<.15;}}
				const double wb=((x-a.sx)*(c.sy-a.sy)-(y-a.sy)*(c.sx-a.sx))/det,wc=((b.sx-a.sx)*(y-a.sy)-(b.sy-a.sy)*(x-a.sx))/det,wa=1-wb-wc;
				if(wa>=-1e-5&&wb>=-1e-5&&wc>=-1e-5){const double distance=(std::min)(wa*std::abs(det)/std::hypot(b.sx-c.sx,b.sy-c.sy),(std::min)(wb*std::abs(det)/std::hypot(a.sx-c.sx,a.sy-c.sy),wc*std::abs(det)/std::hypot(a.sx-b.sx,a.sy-b.sy)));edge|=distance<.15;found=true;for(unsigned k=0;k<8;++k)expected[k]=a.value[k]*wa+b.value[k]*wb+c.value[k]*wc;
					// At an observed interpolated parameter XY, metadata and depth have an
					// independent exact affine answer. This removes subpixel raster rounding
					// without copying the GPU's nonlinear displacement or its metadata values.
					const double ax=a.value[0],ay=a.value[1],bx=b.value[0],by=b.value[1],cx=c.value[0],cy=c.value[1];
					const double qx=values[offset],qy=values[offset+1],area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
					CHECK(std::abs(area)>1e-12);if(std::abs(area)<=1e-12)continue;
					const double qb=((qx-ax)*(cy-ay)-(qy-ay)*(cx-ax))/area,qc=((bx-ax)*(qy-ay)-(by-ay)*(qx-ax))/area,qa=1-qb-qc;
					for(unsigned k=0;k<8;++k)parameterExpected[k]=a.value[k]*qa+b.value[k]*qb+c.value[k]*qc;
					surfaceX=a.sx*qa+b.sx*qb+c.sx*qc;surfaceY=a.sy*qa+b.sy*qb+c.sy*qc;
				}}
			coverage+=values[offset+3]>.5f;if(edge)continue;++stable;
			if(found) {
				for(unsigned k=0;k<8;++k){const double actual=values[(k/4)*width*height*4+offset+k%4];
					if(k<2)maxCoordinateError=(std::max)(maxCoordinateError,std::abs(actual-expected[k]));
					else if(k<7)maxMetadataError=(std::max)(maxMetadataError,std::abs(actual-parameterExpected[k]));
					else maxDepthError=(std::max)(maxDepthError,std::abs(actual-parameterExpected[k]));
				}
				maxPixelErrorX=(std::max)(maxPixelErrorX,std::abs(surfaceX-x));maxPixelErrorY=(std::max)(maxPixelErrorY,std::abs(surfaceY-y));
				CHECK(std::abs(surfaceX-x)<=coverageBoundX&&std::abs(surfaceY-y)<=coverageBoundY);
			} else CHECK(values[offset+3]==0);
		}
		CHECK(stable>width*height/2&&coverage>width*height/4);totalStable+=stable;
		for(const V &v:vertices){maxBound=(std::max)(maxBound,std::abs((v.value[7]-.5)/.02-.2));CHECK(v.sx>=0&&v.sx<width&&v.sy>=0&&v.sy<height);}
		if(mode==0)coarse=values;if(mode==1)zero=values;if(mode==2)nearZero=values;if(mode==4)intermediate=values;if(mode==5)full=values;if(mode==6)stale=values;if(mode==7)locked=values;
	}
	CHECK(maxMetadataError<.00003&&maxDepthError<.00003);CHECK(maxBound<=WaterFftGeometry::MaxVerticalDisplacement);
	unsigned changed=0;double staleDifference=0,zeroError=0,nearError=0;
	for(unsigned i=0;i<width*height;++i) {
		// Full interior demand with locked boundary must cover exactly the original
		// displaced outer quad, including sparse collapsed-parent closure.
		CHECK(coarse[i*4+3]==zero[i*4+3]);CHECK(coarse[i*4+3]==locked[i*4+3]);
		if(coarse[i*4+3]>0.5&&nearZero[i*4+3]>0.5)
			for(unsigned c=0;c<8;++c){size_t at=(c/4)*width*height*4+i*4+c%4;nearError=(std::max)(nearError,double(std::abs(coarse[at]-nearZero[at])));}
	}
	for(size_t i=0;i<coarse.size();++i){changed+=std::abs(full[i]-coarse[i])>.01;zeroError=(std::max)(zeroError,double(std::abs(coarse[i]-zero[i])));staleDifference=(std::max)(staleDifference,double(std::abs(stale[i]-intermediate[i])));}
	CHECK(changed>100);CHECK(staleDifference>.01);CHECK(zeroError<.00003&&nearError<.0003);
	printf("sdl_gpu_frame_selfcheck: GPU production bow refinement: %u meshes/%u stable pixels, raster world-coordinate error %.8f; fresh metadata/depth errors %.8f/%.8f (bounds .00003/.00003), pixel XY errors %.8f/%.8f (independent bounds %.8f/%.8f), coarse collapse %.8f/near-limit %.8f, full-change %u, stale metadata control %.8f, exact locked-boundary coverage, bounded height %.5f\n",renders,totalStable,maxCoordinateError,maxMetadataError,maxDepthError,maxPixelErrorX,maxPixelErrorY,coverageBoundX,coverageBoundY,zeroError,nearError,changed,staleDifference,maxBound);
}

static void check_gpu_fft_vessel_composition(SdlGpuFrame *frame)
{
	// Known Fourier modes, independently packed into the six real field maps, make a numerical
	// derivative meaningful: the random full-band spectrum is too close to texture Nyquist for a
	// small finite difference of bilinear samples to approximate its stored spectral derivative.
	const unsigned side=256,rows=10,columns=41,plane=side*side*4;
	const double amplitude[3]={.13,.17,.32},phase[3]={.31,-.42,.71},chop[3]={-1.2,-1.5,-1.5};
	const int modes[3][2]={{1,1},{1,-1},{1,0}};
	const float points[rows][4]={{0,8,7,0},{23,10,7,.5f},{-24,-10,7,1.5f},{12,18,7,3},{28,0,7,8},
		{-40,9,7,16},{-60,-15,7,64},{80,65,7,128},{0,0,7,512},{8,-17,7,8192}};
	std::vector<float> fields(6*plane);
	for(unsigned c=0;c<3;++c) for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
		const double kx=2*WaterFftSpectrum::Pi*modes[c][0]/WaterFftSpectrum::Sizes[c];
		const double ky=2*WaterFftSpectrum::Pi*modes[c][1]/WaterFftSpectrum::Sizes[c],k=std::hypot(kx,ky);
		const double angle=2*WaterFftSpectrum::Pi*(modes[c][0]*(x+.5)+modes[c][1]*(y+.5))/side+phase[c];
		const double a=amplitude[c]*std::sin(angle),b=amplitude[c]*std::cos(angle);
		float *d=&fields[c*2*plane+(y*side+x)*4],*s=d+plane;
		d[0]=float(a*kx/k);d[1]=float(b);d[2]=float(a*ky/k);d[3]=float(b*kx*ky/k);
		s[0]=float(-a*kx);s[1]=float(-a*ky);s[2]=float(b*kx*kx/k);s[3]=float(b*ky*ky/k);
	}
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUTexture *maps[6]={},*targets[2]={};SDL_GPUSampler *sampler=NULL;
		SDL_GPUShader *vs[2]={},*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};unsigned vertexSamplers[2]={};
		~Resources() {
			for(auto *p:maps) if(p) SDL_ReleaseGPUTexture(device,p);
			for(auto *p:targets) if(p) SDL_ReleaseGPUTexture(device,p);
			if(sampler) SDL_ReleaseGPUSampler(device,sampler);
			for(unsigned i=0;i<2;++i) {if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vs[i]) SDL_ReleaseGPUShader(device,vs[i]);if(ps[i]) SDL_ReleaseGPUShader(device,ps[i]);}
		}
	} resources;
	resources.device=frame->Device();
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;texture.width=texture.height=side;texture.layer_count_or_depth=1;texture.num_levels=WaterFftSpectrum::Phases+1;
	for(auto &map:resources.maps) {map=SDL_CreateGPUTexture(resources.device,&texture);CHECK(map);if(!map) return;}
	texture.num_levels=1;texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=columns;texture.height=rows;
	for(auto &target:resources.targets) {target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(target);if(!target) return;}
	SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
	sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;sampler.max_lod=WaterFftSpectrum::Phases;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&sampler);CHECK(resources.sampler);if(!resources.sampler) return;
	OceanMipReference knownMips;
	auto upload=[&]() {
		knownMips=ocean_mip_reference(fields);
		std::vector<float> packed;std::array<unsigned,WaterFftSpectrum::Phases+1> offsets={};
		for(unsigned level=0;level<knownMips.size();++level) {
			offsets[level]=unsigned(packed.size());
			for(double value:knownMips[level])packed.push_back(float(value));
		}
		SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=packed.size()*sizeof(float);
		SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(resources.device,&ti);CHECK(transfer);if(!transfer) return false;
		void *mapped=SDL_MapGPUTransferBuffer(resources.device,transfer,false);CHECK(mapped);
		if(!mapped) {SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
		memcpy(mapped,packed.data(),ti.size);SDL_UnmapGPUTransferBuffer(resources.device,transfer);
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		if(!commands) {SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
		SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
		for(unsigned level=0;level<knownMips.size();++level) for(unsigned i=0;i<6;++i) {
			const unsigned size=side>>level;
			SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;source.offset=(offsets[level]+i*size*size*4)*sizeof(float);source.pixels_per_row=source.rows_per_layer=size;
			SDL_GPUTextureRegion dest={};dest.texture=resources.maps[i];dest.w=dest.h=size;dest.d=1;dest.mip_level=level;
			SDL_UploadToGPUTexture(copy,&source,&dest,false);
		}
		SDL_EndGPUCopyPass(copy);const bool submitted=SDL_SubmitGPUCommandBuffer(commands);CHECK(submitted);
		SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return submitted;
	};
	CHECK(frame->Flush());if(!upload()) return;
	const std::string basicVS="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string sampleVS=WaterFftSampling::UniformSource("WaterBlock",0,false,true)+WaterFftSampling::VertexSource()+R"HLSL(
cbuffer PointBlock:register(b1) {float4 PointData;};
struct Output {float4 Position:SV_Position;[[vk::location(0)]] float4 Value:TEXCOORD0;[[vk::location(1)]] nointerpolation float4 Registered:TEXCOORD1;};
Output main(uint id:SV_VertexID) {Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
o.Value=float4(fftVertexDisplacement(PointData.xy,PointData.z),1);
o.Registered=float4(fftVertexDisplacement(PointData.xy,PointData.z,.559713426),1);return o;}
)HLSL";
	std::string samplePS="float4 main(float4 pixel:SV_Position,[[vk::location(0)]] float4 value:TEXCOORD0,[[vk::location(1)]] nointerpolation float4 registered:TEXCOORD1):SV_Target0 {return pixel.x>=30.0?registered:value;}";
	std::string surfacePS=WaterFftSampling::UniformSource("WaterBlock",0,false,true);
	for(unsigned i=0;i<6;++i) surfacePS+="Texture2D Texture"+std::to_string(8+i)+":register(t"+std::to_string(i)+");SamplerState Sampler"+std::to_string(8+i)+":register(s"+std::to_string(i)+");\n";
	surfacePS+="cbuffer PointBlock:register(b1) {float4 Points["+std::to_string(rows)+"];};\n";
	surfacePS+=WaterFftSampling::PixelSource();
	surfacePS+=R"HLSL(
float4 main(float4 pixel:SV_Position):SV_Target0 {uint col=(uint)pixel.x;if(col>=6)col+=6;if(col>=45)col+=4;float4 p=Points[(uint)pixel.y];
FftSurface s=fftSurface(p.xy,p.z);
if(col>=36) {
    FftSurface registered=fftSurface(p.xy,p.z,p.w,float3(.559713426,0,0));
    if(col==36)return float4(registered.displacement,registered.vesselCrest);
    if(col==37)return float4(registered.derivativeX,0);
    if(col==38)return float4(registered.derivativeY,0);
    FftSurface unfiltered=fftSurface(p.xy,p.z,0.0,float3(.559713426,0,0));
    return float4(max(abs(registered.displacement-unfiltered.displacement),
        max(abs(registered.derivativeX-unfiltered.derivativeX),abs(registered.derivativeY-unfiltered.derivativeY))),
        0);
}
if(col==0) return float4(s.displacement,0);
if(col==1) return float4(s.derivativeX,0);
if(col==2) return float4(s.derivativeY,s.vesselCrest);
if(col==3) return float4(normalize(cross(float3(1,0,0)+s.derivativeX,float3(0,1,0)+s.derivativeY)),s.incidentCompression);
if(col==4) return float4(vesselGeneratedFoam(s.vesselCrest,1,saturate((4.0-0.15)/fftVerticalBound)),
    vesselGeneratedFoam(s.vesselCrest,1,saturate((2.25-0.15)/fftVerticalBound)),
    vesselGeneratedFoam(s.vesselCrest,0,1),vesselGeneratedFoam(s.vesselCrest,1,0));
if(col==5) return float4(vesselGeneratedFoam(-1,1,1),vesselGeneratedFoam(2,1,2),
    vesselGeneratedFoam(1,-2,1),vesselGeneratedFoam(1,2,1));
float3 lod=fftShadingLod(p.w);
if(col==19)return Texture8.SampleLevel(Sampler8,p.xy/(450.0*fftWorldScale),lod.x);
if(col==20)return Texture9.SampleLevel(Sampler9,p.xy/(450.0*fftWorldScale),lod.x);
if(col==21)return Texture10.SampleLevel(Sampler10,p.xy/(103.0*fftWorldScale),lod.y);
if(col==22)return Texture11.SampleLevel(Sampler11,p.xy/(103.0*fftWorldScale),lod.y);
if(col==23)return Texture12.SampleLevel(Sampler12,p.xy/(13.0*fftWorldScale),lod.z);
if(col==24)return Texture13.SampleLevel(Sampler13,p.xy/(13.0*fftWorldScale),lod.z);
if(col>=30) {
    FftMapSamples all=fftMapSamples(p.xy,float3(0,0,0));
    FftMapSamples low=all;low.c=0;low.cd=0;
    FftMapSamples fine=(FftMapSamples)0;fine.c=all.c;fine.cd=all.cd;
    FftSurface broadMedium=fftDecodeSamples(low),ripple=fftDecodeSamples(fine);
    if(col==30)return float4(broadMedium.displacement,broadMedium.incidentCompression);
    if(col==31)return float4(ripple.displacement,ripple.incidentCompression);
    // Armed old behavior: the fine-only field has actual horizontal compression even
    // though it must no longer add an ambient whitecap-breaking trigger.
    float3 fineLimit=fftLimitDerivative(ripple.displacement);
    float3 fineX=ripple.derivativeX*fineLimit,fineY=ripple.derivativeY*fineLimit;
    if(col==32)return float4(broadMedium.derivativeX,(1+fineX.x)*(1+fineY.y)-fineX.y*fineY.x);
    if(col==33)return float4(broadMedium.derivativeY,fftDecodeSamples(all).incidentCompression);
    FftMapSamples broad=low; broad.b=0; broad.bd=0;
    FftMapSamples medium=low; medium.a=0; medium.ad=0;
    if(col==34)return float4(ripple.derivativeX,fftDecodeSamples(broad).incidentCompression);
    return float4(ripple.derivativeY,fftDecodeSamples(medium).incidentCompression);
}
if(col>=25) {
    float2 queryPosition=p.xy;
    if(col==27 || col==28) {
        // Actual mirrored positions whenever this hull contributes; otherwise several
        // negative/positive world-period wraps exercise arbitrary signed coordinates.
        queryPosition+=float2(-18000.25,4120.5);
        if(HullParameters.x>0.5) {
            int hullIndex=(int)HullParameters.x-1;
            VesselReflection reflected=vesselReflection(p.xy,p.z,hullIndex);
            if(reflected.weight>0.0)queryPosition=reflected.samplePosition;
        }
    }
    FftSurface pairedPhysical,pairedShading;
    fftRawAndShadingSurface(queryPosition,lod,pairedPhysical,pairedShading);
    // This route always fetches all six maps independently. It does not use the new
    // paired helper or its per-cascade reuse decisions; the CPU oracle above separately
    // validates the common channel decode and the existing physical model.
    FftSurface independentPhysical=fftRawSurface(queryPosition,float3(0,0,0));
    FftSurface independentShading=fftRawSurface(queryPosition,lod);
    if(col==29)return float4(abs(pairedShading.displacement-independentPhysical.displacement),0);
    FftSurface paired=pairedPhysical,independent=independentPhysical;
    if(col==26 || col==28) {paired=pairedShading;independent=independentShading;}
    return float4(max(abs(paired.displacement-independent.displacement),
        max(abs(paired.derivativeX-independent.derivativeX),abs(paired.derivativeY-independent.derivativeY))),0);
}
FftSurface filtered=fftSurface(p.xy,p.z,p.w);
if(col==12) return float4(filtered.shadingDisplacement,filtered.unresolvedSlopeVariance);
if(col==13) return float4(filtered.shadingDerivativeX,filtered.vesselCrest);
if(col==14) return float4(filtered.shadingDerivativeY,filtered.incidentCompression);
if(col==15) return float4(normalize(cross(float3(1,0,0)+filtered.shadingDerivativeX,
    float3(0,1,0)+filtered.shadingDerivativeY)),0);
if(col==16) return float4(lod,fftShadingVariance(lod));
if(col==17) return float4(max(abs(filtered.displacement-s.displacement),
    max(abs(filtered.derivativeX-s.derivativeX),abs(filtered.derivativeY-s.derivativeY))),
    abs(filtered.incidentCompression-s.incidentCompression));
return float4(0,0,0,abs(filtered.vesselCrest-s.vesselCrest));}
)HLSL";
	{
        const std::string oldStruct="nointerpolation float4 Registered:TEXCOORD1;";
        sampleVS.replace(sampleVS.find(oldStruct),oldStruct.size(),oldStruct+"[[vk::location(2)]] nointerpolation float4 Lifted:TEXCOORD2;");
        const std::string oldReturn="return o;}";
        sampleVS.replace(sampleVS.find(oldReturn),oldReturn.size(),R"HLSL(
float f=.559713426,d=f*saturate(abs(PointData.x)/28.0),w=.65+.002*PointData.y,bow;
float3 v=fftVertexBowDisplacement(PointData.xy,PointData.z,w*d,bow);
float added=fftBowLift(float3(bow,0,0),float3(f,0,0),float3(d,0,0)).x;
o.Lifted=float4(v.xy*(w*d),(v.z*d+added)*w,fftBowLiftFoam(added,w));return o;}
)HLSL");
        const std::string oldArgument="nointerpolation float4 registered:TEXCOORD1)";
        samplePS.replace(samplePS.find(oldArgument),oldArgument.size(),
            "nointerpolation float4 registered:TEXCOORD1,[[vk::location(2)]] nointerpolation float4 lifted:TEXCOORD2)");
        const std::string oldChoice="return pixel.x>=30.0?registered:value;";
        samplePS.replace(samplePS.find(oldChoice),oldChoice.size(),"return pixel.x>=34.0?lifted:pixel.x>=30.0?registered:value;");
        const std::string before="if(col>=36) {";
        surfacePS.insert(surfacePS.find(before),R"HLSL(
if(col>=40) {
 float f=.559713426,d=f*saturate(abs(p.x)/28.0),w=.65+.002*p.y;
 float3 df=float3(d,abs(p.x)>0.0 && abs(p.x)<28.0?f/28.0*sign(p.x):0.0,0);
 float3 scale=float3(w*d,w*df.y,.002*d);
 FftSurface q=fftSurface(p.xy,p.z,p.w,scale);
 float3 added=fftBowLift(q.bow,float3(f,0,0),df);
 float height=q.displacement.z*d+added.x;
 float2 slope=float2(q.derivativeX.z,q.derivativeY.z)*d+q.displacement.z*df.yz+added.yz;
 if(col==40)return float4(q.displacement.xy*(w*d),height*w,fftBowLiftFoam(added.x,w));
 if(col==41)return float4(added,q.bow.x);
 FftSurface zero=fftSurface(p.xy,p.z,0.0,scale);
 if(col==42)return float4(q.bow,length(q.bow-zero.bow));
 return float4(slope*w+height*float2(0,.002),height*w,f*w*fftVerticalBound);
}
)HLSL");
    }

    {
        surfacePS.insert(surfacePS.find("if(col>=40) {"),R"HLSL(
if(col==49) return float4(s.contactIncident,0);
if(col==50) {
 FftMapSamples low=fftMapSamples(p.xy,float3(0,0,0));
 FftMapSamples fine=(FftMapSamples)0;fine.c=low.c;fine.cd=low.cd;low.c=0;low.cd=0;
 FftSurface incident=fftDecodeSamples(low),ripples=fftDecodeSamples(fine);
 FftSurface filtered=fftSurface(p.xy,p.z,p.w);
 return float4(max(abs(s.contactIncident-incident.contactIncident),abs(ripples.contactIncident)),
   length(filtered.contactIncident-s.contactIncident));
}
)HLSL");
    }

    {
        // Full emitted fftSurface integration. Remove only the incident/reflected field
        // difference between physical and filtered maps; generated bow/stern corrections
        // must be common to both. The scalar field itself has separate independent CPU
        // and finite-difference oracles below. This catches a missing filtered correction
        // even when vertex and physical pixel evaluations agree with one another.
        const std::string before="if(col>=40) {";
        surfacePS.insert(surfacePS.find(before),R"HLSL(
if(col==44) {
 float f=.559713426,d=f*saturate(abs(p.x)/28.0),w=.65+.002*p.y;
 float3 df=float3(d,abs(p.x)>0.0 && abs(p.x)<28.0?f/28.0*sign(p.x):0.0,0);
 float3 scale=float3(w*d,w*df.y,.002*d);
 FftSurface q=fftSurface(p.xy,p.z,p.w,scale);
 float3 lod=fftShadingLod(p.w);
 FftSurface incident,filtered;
 fftRawAndShadingSurface(p.xy,lod,incident,filtered);
 float oldBowTotal=0;
 for(int h=0;h<clamp((int)HullParameters.x,0,8);++h) {
  VesselReflection r=vesselReflection(p.xy,p.z,h);
  if(r.weight>0.0) {
   FftSurface a,b;fftRawAndShadingSurface(r.samplePosition,lod,a,b);
   incident.displacement+=vesselReflectedValue(a.displacement,r);
   incident.derivativeX+=vesselReflectedDerivative(a.displacement,a.derivativeX,a.derivativeY,float2(1,0),r);
   incident.derivativeY+=vesselReflectedDerivative(a.displacement,a.derivativeX,a.derivativeY,float2(0,1),r);
   filtered.displacement+=vesselReflectedValue(b.displacement,r);
   filtered.derivativeX+=vesselReflectedDerivative(b.displacement,b.derivativeX,b.derivativeY,float2(1,0),r);
   filtered.derivativeY+=vesselReflectedDerivative(b.displacement,b.derivativeX,b.derivativeY,float2(0,1),r);
  }
  float3 oldBow;vesselGeneratedWave(p.xy,p.z,WaterParameters.x,h,oldBow);oldBowTotal+=oldBow.x;
 }
 float n=q.displacement.z/fftVerticalBound,physicalDerivative=1.0-n*n;
 float raw=fftVerticalBound*.5*log((1.0+n)/(1.0-n));
 float rawFiltered=raw+filtered.displacement.z-incident.displacement.z;
 float expected=fftVerticalBound*tanh(rawFiltered/fftVerticalBound);
 float derivative=1.0-(expected/fftVerticalBound)*(expected/fftVerticalBound);
 float2 expectedSlope=(float2(q.derivativeX.z,q.derivativeY.z)/physicalDerivative+
   float2(filtered.derivativeX.z-incident.derivativeX.z,filtered.derivativeY.z-incident.derivativeY.z))*derivative;
 float missingCorrection=fftVerticalBound*tanh((rawFiltered-q.bow.x+oldBowTotal)/fftVerticalBound);
 return float4(abs(q.shadingDisplacement.z-expected),
   abs(float2(q.shadingDerivativeX.z,q.shadingDerivativeY.z)-expectedSlope),
   any(lod>0.0)?abs(missingCorrection-expected):0.0);
}
)HLSL");
    }
	for(unsigned stage=0;stage<2;++stage) {
		std::string vs=stage?sampleVS:basicVS,ps=stage?samplePS:surfacePS,error;std::vector<unsigned char> spirv;
		CHECK(SDL3_Shader_Retarget(vs,true));CHECK(SDL3_Shader_Retarget(ps,false));
		bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("FFT vessel VS: %s\n",error.c_str());return;}
		unsigned buffers=0;SDL3_Shader_Slots(spirv,true,resources.vertexSamplers[stage],buffers);
		CHECK(stage?(resources.vertexSamplers[stage]>=3&&resources.vertexSamplers[stage]<=4):resources.vertexSamplers[stage]==0);
		resources.vs[stage]=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs[stage]);
		compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
		if(!compiled) {printf("FFT vessel PS: %s\n",error.c_str());return;}
		resources.ps[stage]=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps[stage]);
		SDL_GPUColorTargetDescription colour={};colour.format=texture.format;
		SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs[stage];info.fragment_shader=resources.ps[stage];
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		resources.pipelines[stage]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipelines[stage]);if(!resources.pipelines[stage]) return;
	}
	SdlWaterPixelConstants water={};water.FftParameters[0]=1;water.WaterParameters[0]=4;
	WaterHullModel::Hull hull;hull.data[0][2]=7;hull.data[0][3]=4;hull.data[1][0]=1;hull.data[1][2]=20;hull.data[1][3]=6;
	hull.data[2][2]=.23f;hull.data[2][3]=sqrtf(436.0f);hull.data[3][0]=-20;hull.data[3][2]=20;
	const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(hull.data[4],outline,sizeof(outline));
	// Scalar reference only: normals come from independent double-precision finite differences,
	// not from copying the production derivative/Jacobian expressions.
	auto smooth=[](double a,double b,double x) {const double t=(std::max)(0.0,(std::min)(1.0,(x-a)/(b-a)));return t*t*(3-2*t);};
    // Separate scalar contour oracle from WaterBowShoulderTest: no production query,
    // gradient or registration helper.
    auto shoulderScalar=[&](double x,double y,double halfLength) {
        WaterHullModel::Hull selected=hull;selected.data[0][2]=7;selected.data[0][3]=4;
        selected.data[1][2]=float(halfLength);selected.data[2][3]=float(std::hypot(halfLength,6.0));
        selected.data[3][0]=float(-halfLength);selected.data[3][2]=float(halfLength);
        const float contour[8]={float(-halfLength),-6,float(halfLength),-6,float(halfLength),6,float(-halfLength),6};
        memcpy(selected.data[4],contour,sizeof(contour));
        return WaterBowShoulderTest::oracle(selected,x,y);
    };
	auto packet=[&](double x,double y,double halfLength) {
		const double along=-x-halfLength,across=y,width=2.1+along*.353553,strip=3.08,length=50.4;
		auto gaussian=[](double distance,double w) {return std::exp(-distance*distance/(w*w));};
		const double arms=gaussian(across-width,strip)*std::cos((across-width)*.40)+gaussian(across+width,strip)*std::cos((across+width)*.40);
		const double churn=gaussian(across,3.3)*std::sin(along*.30-4*1.15+.23*6.2831853)*.24;
		const double trail=smooth(0,10,along)*(1-smooth(length*.5,length,along));
		const double radius=std::sqrt(halfLength*halfLength+36);
		return (trail*(arms*1.35+churn)*smooth(.3,14,12))*(1-smooth(radius+80,radius+110,std::hypot(x,y)))+
            shoulderScalar(x,y,halfLength);
	};
	double maxValue=0,maxDerivative=0,maxNormal=0,maxVertex=0,maxIncident=0;
	double registeredStageError=0,registeredChanged=0,registeredPhysicalError=0;
    double liftStageError=0,liftHeightError=0,liftGradientError=0,liftBowError=0;
    double contactIncidentError=0;
    double shoulderFilteredError=0,shoulderMissingCorrection=0;unsigned shoulderFilteredControls=0;unsigned liftActive=0,liftZero=0,liftNonconstant=0;
	unsigned registeredPhysicalDifferences=0;
	unsigned changedStationary=0,changedMoving=0,changedGradient=0;
	unsigned visibleShallowCrests=0;double maxGeneratedFoam=0;
	std::vector<float> baseline,generatedBaseline,flatBaseline,contactBaseline;
	double idealSamplerError=0;
	double armedAnchorChange=0,maxShadingValue=0,maxShadingDerivative=0,maxShadingNormal=0,maxLod=0,maxVariance=0;
	unsigned filteredChanged=0,preservedPacket=0,armedReuse=0;double pairedError=0;
	unsigned armedFineBreaking=0,broadBreaking=0,mediumBreaking=0,physicalFineSamples=0;
	double maxSelectedCompression=0,maxFineValue=0,maxFineDerivative=0;
	const auto &varianceTable=WaterFftFiltering::SpectrumVariance();
	for(unsigned scenario=0;scenario<18;++scenario) {
        if(scenario==3)continue; // former vessel-wave disable mode was removed
		const bool hasHull=scenario!=0,moving=scenario==2 || scenario==5 || scenario>=6;
		const bool applicable=hasHull && scenario!=4,flat=scenario>=6;
		const bool contact=scenario==7 || scenario==8 || scenario==10 || scenario==11 || scenario==12;
		const bool lastSlot=scenario==5 || scenario==11;
		if(scenario==6) {std::fill(fields.begin(),fields.end(),0);if(!upload()) return;}
		if(scenario>=16) {
			std::fill(fields.begin(),fields.end(),0);
			for(unsigned pixel=0;pixel<side*side;++pixel)fields[pixel*4+1]=scenario==16?.5f:-.5f;
			if(!upload())return; // exactly +10/-10 world-unit incident height, zero derivatives
		}
		water.HullParameters[0]=hasHull?(lastSlot?8:1):0;
		water.SunColour[0]=water.SunColour[1]=water.SunColour[2]=scenario==8?1.0f:0.0f;
		memset(water.HullData,0,sizeof(water.HullData));hull.data[2][0]=moving?12:0;hull.data[0][2]=scenario==4?8:7;
		memset(water.ContactHullData,0,sizeof(water.ContactHullData));
		const float meanLength=scenario==8 || scenario==9?11.0f:20.0f;
		hull.data[1][2]=meanLength;hull.data[2][3]=sqrtf(meanLength*meanLength+36);
		hull.data[3][0]=-meanLength;hull.data[3][2]=meanLength;
        {
            const float contour[8]={-meanLength,-6,meanLength,-6,meanLength,6,-meanLength,6};
            memcpy(hull.data[4],contour,sizeof(contour));
        }
		memcpy(water.HullData+(lastSlot?70:0),&hull,sizeof(hull));
		if(contact || (scenario>=13 && scenario<=15)) {
			WaterHullModel::Hull stable=hull;stable.data[1][2]=28;stable.data[2][3]=sqrtf(820.0f);
			stable.data[3][0]=-28;stable.data[3][2]=28;
            {const float contour[8]={-28,-6,28,-6,28,6,-28,6};memcpy(stable.data[4],contour,sizeof(contour));}
			stable.data[0][2]=scenario==10?8:7;stable.data[0][3]=scenario==13?2:scenario==14?13:scenario==15?0:4;
			memcpy(water.ContactHullData+(lastSlot?70:0),&stable,sizeof(stable));
		}
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		for(unsigned stage=0;stage<2;++stage) {
			SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.targets[stage];attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
			SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipelines[stage]);
			SDL_GPUTextureSamplerBinding bindings[6]={};
			for(unsigned i=0;i<(stage?3u:6u);++i) {bindings[i].texture=resources.maps[stage?i*2:i];bindings[i].sampler=resources.sampler;}
			if(stage) {
				// Some compilers retain the unused cage declaration in VertexSource.
				// TerrainParameters.w is zero; nevertheless every reflected slot must bind.
				bindings[3]={resources.maps[0],resources.sampler};
				SDL_BindGPUVertexSamplers(pass,0,bindings,resources.vertexSamplers[stage]);SDL_PushGPUVertexUniformData(commands,0,&water,sizeof(water));
				for(unsigned row=0;row<rows;++row) {
					SDL_GPUViewport viewport={0,float(row),float(columns),1,0,1};SDL_SetGPUViewport(pass,&viewport);
					SDL_PushGPUVertexUniformData(commands,1,points[row],sizeof(points[row]));SDL_DrawGPUPrimitives(pass,3,1,0,0);
				}
			} else {
				SDL_BindGPUFragmentSamplers(pass,0,bindings,6);SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
				SDL_PushGPUFragmentUniformData(commands,1,points,sizeof(points));SDL_DrawGPUPrimitives(pass,3,1,0,0);
			}
			SDL_EndGPURenderPass(pass);
		}
		CHECK(SDL_SubmitGPUCommandBuffer(commands));const auto values=read_float_targets(frame,resources.targets,2,columns,rows);
		auto columnIndex=[](unsigned column){return column>=49?column-10:column>=12?column-6:column;};
		auto gpu=[&](unsigned row,unsigned col,unsigned c){return double(values[(row*columns+columnIndex(col))*4+c]);};
		auto raw=[&](double x,double y,double out[3],unsigned cascades=3) {
			out[0]=out[1]=out[2]=0;if(scenario>=16) {out[2]=scenario==16?10:-10;return;}if(flat) return;
			for(unsigned c=0;c<cascades;++c) {
				const double length=std::hypot(modes[c][0],modes[c][1]);
				const double a=2*WaterFftSpectrum::Pi*(modes[c][0]*x+modes[c][1]*y)/(WaterFftSpectrum::Sizes[c]*20)+phase[c];
				out[0]+=20*chop[c]*amplitude[c]*modes[c][0]/length*std::sin(a);
				out[1]+=20*chop[c]*amplitude[c]*modes[c][1]/length*std::sin(a);out[2]+=20*amplitude[c]*std::cos(a);
			}
		};
		auto surface=[&](double x,double y,double out[3]) {
			raw(x,y,out);
			if(applicable) {
				const double qx=(std::max)(-20.0,(std::min)(20.0,x)),qy=(std::max)(-6.0,(std::min)(6.0,y));
				const double distance=std::hypot(x-qx,y-qy);
				if(distance>.0001 && distance<28) {
					const double nx=(x-qx)/distance,ny=(y-qy)/distance,t=distance/28,fade=1-t*t*(3-2*t),weight=.68*fade*fade;
					double d[3];raw(2*qx-x,2*qy-y,d);const double dot=d[0]*nx+d[1]*ny;
					out[0]+=(d[0]-2*nx*dot)*weight;out[1]+=(d[1]-2*ny*dot)*weight;out[2]+=d[2]*weight;
				}
				if(moving && scenario!=10) out[2]+=packet(x,y,contact?28.0:double(meanLength));
			}
			const double bounds[3]={8/std::sqrt(2.0),8/std::sqrt(2.0),WaterFftGeometry::MaxVerticalDisplacement};
			for(unsigned c=0;c<3;++c) out[c]=bounds[c]*std::tanh(out[c]/bounds[c]);
		};
		for(unsigned row=0;row<rows;++row) {
			const double x=points[row][0],y=points[row][1];double expected[3],gradient[2][3];surface(x,y,expected);
            {
                // Columns30/32/33 independently expose the already-verified broad/medium
                // raw field. Contact uses its own vertical limiter before vessel additions.
                const double v=WaterFftGeometry::MaxVerticalDisplacement;
                const double t=std::tanh(gpu(row,30,2)/v),d=1-t*t;
                CHECK(std::abs(gpu(row,49,0)-v*t)<.00002);
                CHECK(std::abs(gpu(row,49,1)-gpu(row,32,2)*d)<.000002);
                CHECK(std::abs(gpu(row,49,2)-gpu(row,33,2)*d)<.000002);
                for(unsigned c=0;c<4;++c) {
                    contactIncidentError=(std::max)(contactIncidentError,gpu(row,50,c));
                    // Independent inlining of tanh can differ below a millionth of a
                    // world unit; this must not be mistaken for footprint/wake dependence.
                    CHECK(gpu(row,50,c)<.000002);
                }
                if(scenario>0 && !flat) for(unsigned c=0;c<3;++c)
                    CHECK(gpu(row,49,c)==baseline[(row*columns+columnIndex(49))*4+c]);
            }
			for(unsigned c=0;c<3;++c) {
				const double vertex=values[rows*columns*4+(row*columns+columnIndex(36))*4+c];
				const double error=std::abs(vertex-gpu(row,36,c));
				registeredStageError=(std::max)(registeredStageError,error);CHECK(error<.00002);
				registeredPhysicalError=(std::max)(registeredPhysicalError,double(gpu(row,39,c)));
				registeredPhysicalDifferences+=gpu(row,39,c)!=0;
				CHECK(std::abs(gpu(row,36,c))<=(c==2?WaterFftGeometry::MaxVerticalDisplacement:8/std::sqrt(2.0))+.000001);
				if(flat)CHECK(gpu(row,36,c)==gpu(row,0,c)); // exact zero horizontal chop
			}
			CHECK(gpu(row,39,3)==0);
			registeredChanged=(std::max)(registeredChanged,std::abs(gpu(row,36,2)-gpu(row,0,2)));
            {
                const double free=.559713426,mean=free*(std::min)(1.0,std::abs(x)/28.0),weight=.65+.002*y;
                const double bound=WaterFftGeometry::MaxVerticalDisplacement;
                for(unsigned c=0;c<4;++c) {
                    const double vertex=values[rows*columns*4+(row*columns+columnIndex(40))*4+c];
                    const double error=std::abs(vertex-gpu(row,40,c));liftStageError=(std::max)(liftStageError,error);CHECK(error<.00003);
                }
                CHECK(std::abs(gpu(row,40,2))<=free*weight*bound+.000003);
                CHECK(gpu(row,41,0)>=0 && gpu(row,40,3)>=0 && gpu(row,40,3)<=.65);
                CHECK(gpu(row,42,3)<.000001); // filtered lighting cannot change the physical bow
                const double lifted=(free-mean)*bound*std::tanh(gpu(row,41,3)/bound);
                CHECK(std::abs(lifted-gpu(row,41,0))<.000003);
                CHECK(std::abs(gpu(row,40,3)-.65*weight*(std::min)(1.0,lifted*.32))<.000003);
                if(!applicable || !moving || scenario==10 || mean==free) {
                    CHECK(gpu(row,41,0)==0);CHECK(gpu(row,40,3)==0);++liftZero;
                } else if(lifted>.001)++liftActive;
                {
                    // Independent scalar composition from the known Fourier modes and the
                    // old independent reflection/stern oracle. Reconstruct the raw total
                    // from its exact double limiter, remove the old bow, and evaluate the
                    // bow at independently displaced XY using varying W and D. No GPU
                    // derivative, registration helper or production lift helper is reused.
                    auto bow=[&](double px,double py) {
                        if(!applicable || !moving || scenario==10)return 0.0;
                        const double length=contact?28.0:double(meanLength);
                        return shoulderScalar(px,py,length);
                    };
                    auto composed=[&](double px,double py) {
                        double current[3];surface(px,py,current);
                        const double d=free*(std::min)(1.0,std::abs(px)/28.0),w=.65+.002*py;
                        const double mappedBow=bow(px+current[0]*w*d,py+current[1]*w*d);
                        const double rawTotal=bound*std::atanh(current[2]/bound);
                        const double registered=bound*std::tanh((rawTotal-bow(px,py)+mappedBow)/bound);
                        return w*(d*registered+(free-d)*bound*std::tanh(mappedBow/bound));
                    };
                    double independent[3];surface(x,y,independent);
                    const double expectedBow=bow(x+independent[0]*weight*mean,y+independent[1]*weight*mean);
                    const double expectedHeight=composed(x,y);
                    liftBowError=(std::max)(liftBowError,std::abs(expectedBow-gpu(row,41,3)));
                    liftHeightError=(std::max)(liftHeightError,std::abs(expectedHeight-gpu(row,40,2)));
                    // Nonconstant ideal Fourier modes differ slightly from sampled
                    // bilinear textures; use the existing physical fixture's scale of
                    // tolerances there. Constant-map strict checks remain unchanged.
                    CHECK(std::abs(expectedBow-gpu(row,41,3))<(flat?.00001:.003));
                    CHECK(std::abs(expectedHeight-gpu(row,40,2))<(flat?.00003:.004));
                    if(std::abs(x)>0.001 && std::abs(std::abs(x)-28)>0.001)for(unsigned axis=0;axis<2;++axis) {
                        const double epsilon=.0001;
                        const double expected=(composed(x+(axis==0?epsilon:0),y+(axis==1?epsilon:0))-
                            composed(x-(axis==0?epsilon:0),y-(axis==1?epsilon:0)))/(2*epsilon);
                        const double error=std::abs(expected-gpu(row,43,axis));
                        liftGradientError=(std::max)(liftGradientError,error);CHECK(error<(flat?.0001:.004));
                        if(!flat && applicable && moving && expectedBow>.01)++liftNonconstant;
                    }
                }
            }
            {
                for(unsigned c=0;c<3;++c) {
                    shoulderFilteredError=(std::max)(shoulderFilteredError,gpu(row,44,c));
                    CHECK(gpu(row,44,c)<.00003);
                }
                shoulderMissingCorrection=(std::max)(shoulderMissingCorrection,gpu(row,44,3));
                shoulderFilteredControls+=gpu(row,44,3)>.01;
            }
			// Fine-only decode keeps its physical ripple but has exactly the identity
			// breaking Jacobian. Adding fine waves, reflected fields or generated packets
			// cannot change the selected broad/medium incident compression.
			CHECK(gpu(row,31,3)==1);
			CHECK(gpu(row,30,3)==gpu(row,33,3) && gpu(row,30,3)==gpu(row,3,3));
			double selectedX[2],selectedY[2];
			for(unsigned c=0;c<2;++c) {
				const double q=std::tanh(gpu(row,30,c)/(8/std::sqrt(2.0))),limit=1-q*q;
				selectedX[c]=gpu(row,32,c)*limit;selectedY[c]=gpu(row,33,c)*limit;
			}
			const double selectedCompression=(1+selectedX[0])*(1+selectedY[1])-selectedX[1]*selectedY[0];
			maxSelectedCompression=(std::max)(maxSelectedCompression,std::abs(gpu(row,30,3)-selectedCompression));
			CHECK(std::abs(gpu(row,30,3)-selectedCompression)<.000001);
			if(scenario==0) {
				armedFineBreaking+=std::abs(gpu(row,32,3)-1)>.001;
				broadBreaking+=std::abs(gpu(row,34,3)-1)>.001;
				mediumBreaking+=std::abs(gpu(row,35,3)-1)>.001;
				physicalFineSamples+=std::abs(gpu(row,31,2))>.01 && std::abs(gpu(row,34,2))>.001;
				const double k=2*WaterFftSpectrum::Pi/WaterFftSpectrum::Sizes[2];
				const double theta=k*x/20+phase[2],a=amplitude[2]*std::sin(theta),b=amplitude[2]*std::cos(theta);
				const double fine[3]={20*chop[2]*a,0,20*b},fineX[3]={chop[2]*b*k,0,-a*k};
				for(unsigned c=0;c<3;++c) {
					maxFineValue=(std::max)(maxFineValue,std::abs(gpu(row,31,c)-fine[c]));
					maxFineDerivative=(std::max)(maxFineDerivative,std::abs(gpu(row,34,c)-fineX[c]));
					CHECK(std::abs(gpu(row,31,c)-fine[c])<.004 && std::abs(gpu(row,34,c)-fineX[c])<.003);
					CHECK(gpu(row,35,c)==0);
				}
			}
			// The resource fixture separately checks ideal trilinear interpolation at exact
			// filter coordinates. At these arbitrary world positions, read each signed map
			// sample independently: hardware interpolation has finite fractional precision,
			// and must not be mistaken for an error in the surface/Jacobian calculation.
			// Existing independent finite-difference physical checks below remain unchanged.
			double lod[3],expectedVariance=0,shading[3]={},shadingGradient[2][3]={};
			for(unsigned cascade=0;cascade<3;++cascade) {
				lod[cascade]=(std::max)(0.0,(std::min)(8.0,std::log2((std::max)(1.0,double(points[row][3])*256/(WaterFftSpectrum::Sizes[cascade]*20)))));
				const unsigned low=(std::min)(7u,unsigned(lod[cascade]));const double fraction=lod[cascade]-low;
				expectedVariance+=varianceTable[low][cascade]*(1-fraction)+varianceTable[low+1][cascade]*fraction;
				maxLod=(std::max)(maxLod,std::abs(gpu(row,16,cascade)-lod[cascade]));CHECK(std::abs(gpu(row,16,cascade)-lod[cascade])<.000002);
				double d[4],slope[4];
				for(unsigned c=0;c<4;++c) {
					const double u=x/(WaterFftSpectrum::Sizes[cascade]*20),v=y/(WaterFftSpectrum::Sizes[cascade]*20);
					d[c]=gpu(row,19+cascade*2,c);slope[c]=gpu(row,20+cascade*2,c);
					idealSamplerError=(std::max)(idealSamplerError,std::abs(d[c]-ocean_mip_sample(knownMips,cascade*2,c,u,v,lod[cascade])));
					idealSamplerError=(std::max)(idealSamplerError,std::abs(slope[c]-ocean_mip_sample(knownMips,cascade*2+1,c,u,v,lod[cascade])));
				}
				shading[0]+=20*chop[cascade]*d[0];shading[1]+=20*chop[cascade]*d[2];shading[2]+=20*d[1];
				shadingGradient[0][0]+=chop[cascade]*slope[2];shadingGradient[0][1]+=chop[cascade]*d[3];shadingGradient[0][2]+=slope[0];
				shadingGradient[1][0]+=chop[cascade]*d[3];shadingGradient[1][1]+=chop[cascade]*slope[3];shadingGradient[1][2]+=slope[1];
			}
			maxVariance=(std::max)(maxVariance,std::abs(gpu(row,16,3)-expectedVariance));CHECK(std::abs(gpu(row,16,3)-expectedVariance)<.000002);
			for(unsigned c=0;c<4;++c)CHECK(gpu(row,17,c)==0 && gpu(row,18,c)==0);
			for(unsigned column=25;column<=28;++column) for(unsigned c=0;c<4;++c) {
				pairedError=(std::max)(pairedError,std::abs(gpu(row,column,c)));CHECK(gpu(row,column,c)==0);
			}
			if(scenario==0) for(unsigned c=0;c<3;++c)armedReuse+=gpu(row,29,c)>.001;
			CHECK(gpu(row,13,3)==gpu(row,2,3) && gpu(row,14,3)==gpu(row,3,3));
			CHECK(gpu(row,12,3)>=0);
			if(row<=1 || flat) {
				for(unsigned c=0;c<3;++c) {
					CHECK(gpu(row,12,c)==gpu(row,0,c));CHECK(gpu(row,13,c)==gpu(row,1,c));CHECK(gpu(row,14,c)==gpu(row,2,c));
					CHECK(gpu(row,15,c)==gpu(row,3,c));
				}
				if(flat && std::abs(gpu(row,0,2))>.01 && row>1)++preservedPacket;
				if(row<=1)CHECK(gpu(row,12,3)==0);
			}
			if(scenario==0) {
				const double bound[3]={8/std::sqrt(2.0),8/std::sqrt(2.0),WaterFftGeometry::MaxVerticalDisplacement};
				const double verticalLimit=1-std::pow(std::tanh(shading[2]/bound[2]),2);
				for(unsigned c=0;c<3;++c) {
					const double t=std::tanh(shading[c]/bound[c]);shading[c]=bound[c]*t;
					maxShadingValue=(std::max)(maxShadingValue,std::abs(gpu(row,12,c)-shading[c]));CHECK(std::abs(gpu(row,12,c)-shading[c])<.00002);
					for(unsigned axis=0;axis<2;++axis) {
						shadingGradient[axis][c]*=1-t*t;
						maxShadingDerivative=(std::max)(maxShadingDerivative,std::abs(gpu(row,13+axis,c)-shadingGradient[axis][c]));
						CHECK(std::abs(gpu(row,13+axis,c)-shadingGradient[axis][c])<.000002);
					}
					filteredChanged+=std::abs(gpu(row,12,c)-gpu(row,0,c))>.001;
				}
				CHECK(std::abs(gpu(row,12,3)-expectedVariance*verticalLimit*verticalLimit)<.000002);
				shadingGradient[0][0]+=1;shadingGradient[1][1]+=1;double normal[3];
				for(unsigned c=0;c<3;++c)normal[c]=shadingGradient[0][(c+1)%3]*shadingGradient[1][(c+2)%3]-shadingGradient[0][(c+2)%3]*shadingGradient[1][(c+1)%3];
				const double length=std::hypot(normal[0],std::hypot(normal[1],normal[2]));
				for(unsigned c=0;c<3;++c) {maxShadingNormal=(std::max)(maxShadingNormal,std::abs(gpu(row,15,c)-normal[c]/length));CHECK(std::abs(gpu(row,15,c)-normal[c]/length)<.000002);}
			}
			const double generated=applicable && moving && scenario!=10?packet(x,y,contact?28.0:double(meanLength)):0;
			const double expectedCrest=(std::min)(1.0,(std::max)(0.0,generated)*.32);
			CHECK(std::abs(gpu(row,2,3)-expectedCrest)<.00001);
			for(unsigned c=0;c<2;++c) {
				const double damping=WaterFftGeometry::SafeDamping(1,c?2.25f:4.0f);
				const double expectedFoam=expectedCrest*.65*smooth(.15,.65,damping);
				const double difference=std::abs(gpu(row,4,c)-expectedFoam);
				maxGeneratedFoam=(std::max)(maxGeneratedFoam,difference);CHECK(difference<.00001);
				if(expectedFoam>.005)++visibleShallowCrests;
			}
			CHECK(gpu(row,4,2)==0 && gpu(row,4,3)==0);
			CHECK(gpu(row,5,0)==0 && std::abs(gpu(row,5,1)-.65)<.000001 && gpu(row,5,2)==0 && std::abs(gpu(row,5,3)-.65)<.000001);
			double incidentGradient[2][2];
			for(unsigned axis=0;axis<2;++axis) {
				double plus[3],minus[3];const double step=.0001;
				surface(x+(axis==0?step:0),y+(axis==1?step:0),plus);surface(x-(axis==0?step:0),y-(axis==1?step:0),minus);
				for(unsigned c=0;c<3;++c) gradient[axis][c]=(plus[c]-minus[c])/(2*step);
				// Only the intended ambient-breaking oracle changes: the independent
				// physical height/normal reference above still uses all three cascades.
				raw(x+(axis==0?step:0),y+(axis==1?step:0),plus,2);raw(x-(axis==0?step:0),y-(axis==1?step:0),minus,2);
				const double bound=8/std::sqrt(2.0);
				for(unsigned c=0;c<2;++c) incidentGradient[axis][c]=bound*(std::tanh(plus[c]/bound)-std::tanh(minus[c]/bound))/(2*step);
			}
			const double incident=(1+incidentGradient[0][0])*(1+incidentGradient[1][1])-incidentGradient[0][1]*incidentGradient[1][0];
			maxIncident=(std::max)(maxIncident,std::abs(gpu(row,3,3)-incident));CHECK(std::abs(gpu(row,3,3)-incident)<.0003);
			if(scenario>0 && !flat) CHECK(gpu(row,3,3)==baseline[(row*columns+3)*4+3]);
			if(flat) CHECK(gpu(row,3,3)==1);
			for(unsigned c=0;c<3;++c) {
				maxValue=(std::max)(maxValue,std::abs(gpu(row,0,c)-expected[c]));CHECK(std::abs(gpu(row,0,c)-expected[c])<.004);
				for(unsigned axis=0;axis<2;++axis) {
					const double difference=std::abs(gpu(row,axis+1,c)-gradient[axis][c]);maxDerivative=(std::max)(maxDerivative,difference);CHECK(difference<.003);
				}
				const double vertex=values[rows*columns*4+row*columns*4+c],difference=std::abs(vertex-gpu(row,0,c));
				maxVertex=(std::max)(maxVertex,difference);CHECK(difference<.00001);
			}
			gradient[0][0]+=1;gradient[1][1]+=1;double normal[3];
			for(unsigned c=0;c<3;++c) normal[c]=gradient[0][(c+1)%3]*gradient[1][(c+2)%3]-gradient[0][(c+2)%3]*gradient[1][(c+1)%3];
			const double norm=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
			for(unsigned c=0;c<3;++c) {const double difference=std::abs(gpu(row,3,c)-normal[c]/norm);maxNormal=(std::max)(maxNormal,difference);CHECK(difference<.003);}
			CHECK(std::hypot(gpu(row,0,0),gpu(row,0,1))<=8.00001 && std::abs(gpu(row,0,2))<=WaterFftGeometry::MaxVerticalDisplacement+.00001);
			const float shallow=WaterFftGeometry::SafeDamping(1,2.25f);CHECK(gpu(row,0,2)*shallow>=-2.10-.00001);
			if(scenario==1) {
				changedStationary+=std::abs(gpu(row,0,2)-baseline[row*columns*4+2])>.001;
				double difference=0;
				for(unsigned col=1;col<=2;++col) for(unsigned c=0;c<3;++c)
					difference=(std::max)(difference,std::abs(gpu(row,col,c)-baseline[(row*columns+col)*4+c]));
				changedGradient+=difference>.001;
			}
			if(flat) changedMoving+=std::abs(gpu(row,0,2))>.01;
		}
		for(float v:values) CHECK(std::isfinite(v));
		if(scenario==0) baseline=values;
		if(scenario==2) generatedBaseline=values;
		if(scenario==4) {
			double previousMaximum=0,newMaximum=0;unsigned mismatches=0;
			for(size_t i=0;i<values.size();++i)if(values[i]!=baseline[i]) {
				const double error=std::abs(double(values[i])-baseline[i]);
				if(i/4%columns<columnIndex(36))previousMaximum=(std::max)(previousMaximum,error);else newMaximum=(std::max)(newMaximum,error);
				++mismatches;
			}
			printf("registered disabled scenario%u previous/new maxima %.9g / %.9g, mismatches%u\n",scenario,previousMaximum,newMaximum,mismatches);
			// Retain the physical-field exact contract. Registered evaluations
			// permit <1e-6 world units for differently optimized uniform branches;
			// observed maxima were 4.77e-7, not a change to their physical model.
			CHECK(previousMaximum==0);
			CHECK(newMaximum<.000001);
		}
		if(scenario==5) CHECK(values==generatedBaseline);
		if(scenario==6)flatBaseline=values;
		if(scenario==7)contactBaseline=values;
		if(scenario==11 || scenario==12)CHECK(values==contactBaseline);
		if(scenario==8) {
			// Contact packet remains exact while the mean hull changes. Its incident
			// reflection variance is allowed to follow that changed mean-hull bound.
			for(size_t i=0;i<values.size();++i)if(!(i/4%columns==columnIndex(12) && i%4==3))CHECK(values[i]==contactBaseline[i]);
		}
		if(scenario==9)for(unsigned row=0;row<rows;++row)
			armedAnchorChange=(std::max)(armedAnchorChange,std::abs(double(values[row*columns*4+2])-flatBaseline[row*columns*4+2]));
		if(scenario>=13 && scenario<=15)CHECK(values==flatBaseline);
	}
    printf("contact incident: broad/medium limiter and gradients; fine/reflection/generated/footprint isolation maximum %.9g\n",contactIncidentError);
    {
        CHECK(shoulderFilteredControls>5 && shoulderMissingCorrection>.05);
        printf("sdl_gpu_frame_selfcheck: full contour shoulder FFT integration: physical/filtered generated correction %.9g, armed omitted-filter correction %.9g (%u cases); actual production vertex/pixel source, known Fourier modes, varying weight/damping, zero chop, fallback/contact/last-slot and independent scalar/derivative/foam checks retained\n",shoulderFilteredError,shoulderMissingCorrection,shoulderFilteredControls);
    }
    {
        CHECK(liftActive>5 && liftZero>20 && liftNonconstant>1);
        printf("sdl_gpu_frame_selfcheck: bow lift actual FFT VS/PS %.9g, independent bow %.9g/height %.9g/finite-difference derivative %.9g; active %u, exact zero %u and nonconstant derivative controls %u, floor bound, physical filtering and actual-height foam checked\n",liftStageError,liftBowError,liftHeightError,liftGradientError,liftActive,liftZero,liftNonconstant);
    }
	CHECK(registeredChanged>.001);
	printf("sdl_gpu_frame_selfcheck: registered physical footprint comparison max %.9g across %u nonzero values\n",registeredPhysicalError,registeredPhysicalDifferences);
	// Identical mip-zero inputs/geometry, with filtered and constant-zero-LOD calls
	// optimized separately. Keep this far below the armed .001-unit displacement
	// control; the observed maximum is 2.27e-7. Raw generated terms remain exact.
	CHECK(registeredPhysicalError<.000001);
	printf("sdl_gpu_frame_selfcheck: registered bow texture/stage parity %.8g, armed coordinate displacement %.8g; physical footprint agreement <1e-6, exact flat-chop identity and bounds checked\n",registeredStageError,registeredChanged);
	CHECK(changedStationary>=3 && changedGradient>=3 && changedMoving>=2 && armedAnchorChange>.2);
	CHECK(visibleShallowCrests>10);CHECK(filteredChanged>=5 && preservedPacket>=20);CHECK(armedReuse>=5);
	CHECK(armedFineBreaking>=5 && broadBreaking>=5 && mediumBreaking>=5 && physicalFineSamples>=5);
	printf("sdl_gpu_frame_selfcheck: FFT breaking scale: coarse/medium limiter error %.9f; fine-only compression exactly1 with %u physical ripple samples, %u armed old fine-breaking controls and %u/%u broad/medium responses; independent fine value %.9f/derivative %.9f, full physical/vessel/reflection checks retained\n",maxSelectedCompression,physicalFineSamples,armedFineBreaking,broadBreaking,mediumBreaking,maxFineValue,maxFineDerivative);
	printf("sdl_gpu_frame_selfcheck: FFT paired sampling reuse: bit-exact displacement/derivatives at arbitrary, negative-wrapped and real mirrored queries with zero/mixed/all positive LOD; maxdifference %.9f, armed missing-filter controls %u\n",pairedError,armedReuse);
	printf("sdl_gpu_frame_selfcheck: FFT footprint filtering: sampled-channel value %.9f/tangent %.9f/normal %.9f, LOD %.9f/variance %.9f; %u changed components, %u constant-field packet samples preserved, all physical outputs bit-exact across footprints0..8192; arbitrary-coordinate ideal sampler discrepancy %.9f reported separately\n",maxShadingValue,maxShadingDerivative,maxShadingNormal,maxLod,maxVariance,filteredChanged,preservedPacket,idealSamplerError);
	printf("sdl_gpu_frame_selfcheck: GPU generated vessel foam: independent packet-crest/coverage error %.8f, %u active depth4/2.25 samples; rest/disabled/zero weight/zero hull damping, bounded inputs, unchanged coverage under changed sunlight checked\n",maxGeneratedFoam,visibleShallowCrests);
	printf("sdl_gpu_frame_selfcheck: GPU FFT-vessel composition: 17 known-mode/constant atlases, value %.8f/gradient %.8f/normal %.8f/VS-PS %.8f/incident compression %.8f; coarse+medium incident compression, stable contact packet despite changing mean slice, armed old-anchor height jump %.8f, independent contour-bow/stern reference, contact level/pause/final-slot/count0/2/13 fallback, stationary reflection and bounds checked\n",maxValue,maxDerivative,maxNormal,maxVertex,maxIncident,armedAnchorChange);
}

// Direct O(N^2) Fourier sum at a few points: deliberately independent of GPU butterfly ordering,
// bit reversal, .5-per-phase normalization and checkerboard postprocessing. Complex-pair packing
// also verifies all four channels, not just a visually plausible height map.
static void direct_ocean(const std::vector<float>& initial,unsigned cascade,double px,double py,double time,double out[8],unsigned n=WaterFftSpectrum::Resolution)
{
	using C=std::complex<double>;
	C sums[4]={},unpacked[8]={};const C imaginary(0,1);
	for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
		const double kx=2*WaterFftSpectrum::Pi*(int(x)-int(n/2))/WaterFftSpectrum::Sizes[cascade];
		const double ky=2*WaterFftSpectrum::Pi*(int(y)-int(n/2))/WaterFftSpectrum::Sizes[cascade],k=std::hypot(kx,ky);
		if(k==0) continue;
		const float *h=&initial[((cascade*n+y)*n+x)*4];
		const C e=std::polar(1.0,std::sqrt(9.81*k)*time),height=C(h[0],h[1])*e+C(h[2],h[3])*std::conj(e);
		const C dx=x?height*C(0,-kx/k):C(0),dz=y?height*C(0,-ky/k):C(0);
		const C sx=x?height*C(0,kx):C(0),sz=y?height*C(0,ky):C(0);
		const C dxx=x?height*(kx*kx/k):C(0),dzz=y?height*(ky*ky/k):C(0),dxz=x&&y?height*(kx*ky/k):C(0);
		const C phase=std::polar(1.0,2*WaterFftSpectrum::Pi*((int(x)-int(n/2))*px+(int(y)-int(n/2))*py)/n);
		sums[0]+=(dx+imaginary*height)*phase;sums[1]+=(dz+imaginary*dxz)*phase;
		sums[2]+=(sx+imaginary*sz)*phase;sums[3]+=(dxx+imaginary*dzz)*phase;
		const C physical[8]={dx,height,dz,dxz,sx,sz,dxx,dzz};
		for(unsigned i=0;i<8;++i) unpacked[i]+=physical[i]*phase;
	}
	for(unsigned i=0;i<4;++i) {out[2*i]=sums[i].real();out[2*i+1]=sums[i].imag();}
	// The packed DFT alone can agree with a broken implementation: an imaginary height component
	// would silently appear in its neighbour's real channel. Independently invert all eight physical
	// spectra and require a real result, before comparing that result against the packed transform.
	for(unsigned i=0;i<8;++i) {
		CHECK(std::abs(unpacked[i].imag())<0.00000001);
		CHECK(std::abs(unpacked[i].real()-out[i])<0.00000001);
	}
}

static void check_fft_spectral_reality(const std::vector<float>& initial,unsigned Resolution=WaterFftSpectrum::Resolution)
{
	using namespace WaterFftSpectrum;using C=std::complex<double>;
	auto height=[&](const std::vector<float>& data,unsigned cascade,unsigned x,unsigned y,double time) {
		const double kx=2*Pi*(int(x)-int(Resolution/2))/Sizes[cascade],ky=2*Pi*(int(y)-int(Resolution/2))/Sizes[cascade];
		const float *h=&data[((cascade*Resolution+y)*Resolution+x)*4];
		const C e=std::polar(1.0,std::sqrt(9.81*std::hypot(kx,ky))*time);
		return C(h[0],h[1])*e+C(h[2],h[3])*std::conj(e);
	};
	double maximum=0;bool nyquistZero=true;
	for(unsigned cascade=0;cascade<3;++cascade) for(unsigned y=0;y<Resolution;++y) for(unsigned x=0;x<Resolution;++x) {
		if(x==0 || y==0) for(unsigned c=0;c<4;++c) nyquistZero &= initial[((cascade*Resolution+y)*Resolution+x)*4+c]==0;
		for(double time:{0.0,1.375,3.125}) {
			const C a=height(initial,cascade,x,y,time),b=height(initial,cascade,(Resolution-x)%Resolution,(Resolution-y)%Resolution,time);
			maximum=(std::max)(maximum,std::abs(a-std::conj(b)));
		}
	}
	CHECK(nyquistZero && maximum<0.000000000001);
	// An unpaired self-conjugate Nyquist coefficient is the precise old failure. This control must
	// violate the reality assertion even though a packed CPU/GPU FFT comparison would still pass.
	auto broken=initial;const unsigned at=(Resolution/2)*Resolution*4;
	broken[at]=1;
	const C bad=height(broken,0,0,Resolution/2,1.375);
	CHECK(std::abs(bad-std::conj(bad))>0.01);
}

static void check_gpu_fft_ocean(SdlGpuFrame *frame)
{
	using namespace WaterFftSpectrum;
	CHECK(frame->Ocean_Map(0)==NULL && frame->Ocean_Sampler()==NULL);
	CHECK(!frame->Prepare_Ocean(NAN));
	CHECK(frame->Prepare_Ocean(1.375f));if(!frame->Ocean_Map(0)) return;
	CHECK(frame->Ocean_Map(6)==NULL && frame->Ocean_Sampler()!=NULL && frame->Ocean_Updates()==1);
	std::vector<float> field=read_ocean(frame);const std::vector<float> initial=InitialSpectrum();
	const auto mipReference=ocean_mip_reference(field);double mipError=0;
	const auto mipField=check_ocean_mip_contents(frame,mipReference,mipError);
	check_ocean_fractional_mips(frame,mipReference);
	check_fft_spectral_reality(initial);
	const unsigned plane=Resolution*Resolution*4;
	auto value=[&](const std::vector<float>& data,unsigned cascade,unsigned channel,unsigned x,unsigned y) -> double {
		return data[(cascade*2+channel/4)*plane+(y*Resolution+x)*4+channel%4];
	};
	bool finite=true,nonempty=true;double largest=0,means[24]={},energies[24]={};
	for(unsigned m=0;m<6;++m) for(unsigned p=0;p<Resolution*Resolution;++p) for(unsigned c=0;c<4;++c) {
		const double v=field[m*plane+p*4+c];finite &= std::isfinite(v);means[m*4+c]+=v;energies[m*4+c]+=v*v;
	}
	for(unsigned c=0;c<24;++c) {nonempty &= energies[c]>0.00001;CHECK(std::abs(means[c]/(Resolution*Resolution))<0.00001);}
	CHECK(finite && nonempty);
	const unsigned coordinates[][2]={{0,0},{17,53},{128,128},{255,7}};
	for(unsigned cascade=0;cascade<3;++cascade) for(auto &point:coordinates) {
		double expected[8];direct_ocean(initial,cascade,point[0],point[1],1.375,expected);
		for(unsigned c=0;c<8;++c) {const double error=std::abs(value(field,cascade,c,point[0],point[1])-expected[c]);
			largest=(std::max)(largest,error);CHECK(error<0.001+std::abs(expected[c])*0.0001);}
	}
	// Project several real GPU maps onto Fourier modes independently. A wrong UV orientation, sign,
	// cascade size or derivative channel cannot satisfy dh/dx=i*kx*h and dh/dz=i*kz*h.
	// Select nonzero modes inside the separated swell/wind/ripple bands (world
	// wavelengths about 445, 94 and 23). Keep the nonzero-energy assertion below:
	// a derivative check on a deliberately excluded mode would be vacuous.
	const int modes[3][2]={{17,11},{19,11},{9,7}};
	double derivativeError=0,finiteDifferenceError=0;
	for(unsigned cascade=0;cascade<3;++cascade) {
		using C=std::complex<double>;C coefficients[3]={};
		const int mx=modes[cascade][0],my=modes[cascade][1];
		std::vector<C> ex(Resolution),ey(Resolution);
		for(unsigned i=0;i<Resolution;++i) {ex[i]=std::polar(1.0,-2*Pi*mx*i/Resolution);ey[i]=std::polar(1.0,-2*Pi*my*i/Resolution);}
		for(unsigned y=0;y<Resolution;++y) for(unsigned x=0;x<Resolution;++x) {
			const C phase=ex[x]*ey[y]/double(Resolution*Resolution);
			coefficients[0]+=value(field,cascade,1,x,y)*phase;
			coefficients[1]+=value(field,cascade,4,x,y)*phase;coefficients[2]+=value(field,cascade,5,x,y)*phase;
		}
		CHECK(std::abs(coefficients[0])>0.000001);
		for(unsigned axis=0;axis<2;++axis) {
			const double k=2*Pi*(axis?my:mx)/Sizes[cascade];
			const double error=std::abs(coefficients[axis+1]-C(0,k)*coefficients[0]);
			derivativeError=(std::max)(derivativeError,error);CHECK(error<0.00001);
			if(cascade==2) {
				const double step=Sizes[cascade]/Resolution,at=0.173;
				auto height=[&](double p){return (coefficients[0]*std::polar(1.0,k*p)).real();};
				const double numeric=(height(at-2*step)-8*height(at-step)+8*height(at+step)-height(at+2*step))/(12*step);
				const double stored=(coefficients[axis+1]*std::polar(1.0,k*at)).real();
				finiteDifferenceError=(std::max)(finiteDifferenceError,std::abs(numeric-stored));CHECK(std::abs(numeric-stored)<0.000001);
			}
		}
	}
	const uint64_t beforePause=frame->Ocean_Updates();CHECK(frame->Prepare_Ocean(1.375f));
	CHECK(frame->Ocean_Updates()==beforePause && read_ocean(frame)==field);
	for(unsigned level=1;level<=Phases;++level) CHECK(read_ocean(frame,level)==mipField[level]);
	CHECK(!frame->Prepare_Ocean(INFINITY) && frame->Ocean_Updates()==beforePause);
	SDL_GPUTexture *original=frame->Ocean_Map(0);
	CHECK(frame->Resize(83,57));CHECK(frame->Prepare_Ocean(1.375f));
	CHECK(frame->Ocean_Map(0)==original && frame->Ocean_Updates()==beforePause && read_ocean(frame)==field);
	for(unsigned level=1;level<=Phases;++level) CHECK(read_ocean(frame,level)==mipField[level]);
	CHECK(frame->Resize(WIDTH,HEIGHT));

	// Exercise the actual recorded vertex-sampler path, including updates while an earlier field
	// reader is still unsubmitted. Three vertex reads arrive as an unclamped float colour.
	const char *vs=R"HLSL(
Texture2D<float4> A:register(t0,space0);SamplerState SA:register(s0,space0);
Texture2D<float4> B:register(t1,space0);SamplerState SB:register(s1,space0);
Texture2D<float4> C:register(t2,space0);SamplerState SC:register(s2,space0);
struct Output { float4 Position:SV_Position; [[vk::location(0)]] float4 Value:TEXCOORD0; };
Output main(uint id:SV_VertexID) { Output output;output.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
float2 uv=float2(0,0);output.Value=float4(A.SampleLevel(SA,uv,2.375).y,B.SampleLevel(SB,uv,2.375).y,C.SampleLevel(SC,uv,2.375).y,1);return output; }
)HLSL";
	const char *ps="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {return value;}";
	std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error));SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);CHECK(vertex);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error));SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipelineInfo.target_info.color_target_descriptions=&colour;pipelineInfo.target_info.num_color_targets=1;
	pipelineInfo.target_info.has_depth_stencil_target=true;pipelineInfo.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);CHECK(pipeline);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=colour.format;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;textureInfo.width=textureInfo.height=4;
	textureInfo.layer_count_or_depth=textureInfo.num_levels=1;
	SDL_GPUTexture *targets[2]={SDL_CreateGPUTexture(frame->Device(),&textureInfo),SDL_CreateGPUTexture(frame->Device(),&textureInfo)};
	CHECK(targets[0] && targets[1]);
	auto draw=[&](unsigned target) {
		frame->Set_Target({targets[target],frame->Depth_For(4,4),4,4});SdlRecordedDraw record={};record.Pipeline=pipeline;record.Count=3;
		memset(frame->Stage(16,record.VertexOffset),0,16);record.VertexSamplerCount=3;
		for(unsigned i=0;i<3;++i) {record.VertexTextures[i]=frame->Ocean_Map(2*i);record.VertexSamplers[i]=frame->Ocean_Sampler();}
		record.Viewport[2]=record.Viewport[3]=4;record.Viewport[5]=1;record.Scissor[2]=record.Scissor[3]=4;frame->Record_Draw(record);
	};
	if(pipeline && targets[0] && targets[1]) {
		draw(0);CHECK(frame->Prepare_Ocean(3.125f));CHECK(frame->Ocean_Updates()==beforePause+1);draw(1);
		const auto sampled=read_float_targets(frame,targets,2,4,4),advanced=read_ocean(frame);
		const auto advancedReference=ocean_mip_reference(advanced);
		const auto advancedMips=check_ocean_mip_contents(frame,advancedReference,mipError);
		check_ocean_fractional_mips(frame,advancedReference);
		double changedMipSample=0;
		for(unsigned c=0;c<3;++c) {
			const double oldSample=ocean_mip_sample(mipReference,c*2,1,0,0,2.375);
			const double newSample=ocean_mip_sample(advancedReference,c*2,1,0,0,2.375);
			CHECK(std::abs(sampled[c]-oldSample)<0.00001);
			CHECK(std::abs(sampled[64+c]-newSample)<0.00001);
			changedMipSample=(std::max)(changedMipSample,std::abs(newSample-oldSample));
			double expected[8];direct_ocean(initial,c,17,53,3.125,expected);
			for(unsigned component=0;component<8;++component) CHECK(std::abs(value(advanced,c,component,17,53)-expected[component])<0.001+std::abs(expected[component])*0.0001);
		}
		CHECK(advanced!=field);
		CHECK(changedMipSample>0.0001 && advancedMips[2]!=mipField[2]);
	}
	CHECK(frame->Flush());frame->Set_Target(frame->Back_Buffer_Target());
	for(auto *target:targets) if(target) SDL_ReleaseGPUTexture(frame->Device(),target);
	if(pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	if(vertex) SDL_ReleaseGPUShader(frame->Device(),vertex);if(pixel) SDL_ReleaseGPUShader(frame->Device(),pixel);
	printf("sdl_gpu_frame_selfcheck: GPU FFT ocean: all24 finite channels, direct Fourier error %.8f, spectral derivative error %.9f, finite-difference mode error %.9f, independent all-mip box error %.9f; all9-mip pause/resize reuse and queued trilinear vertex-sampled field ordering checked\n",largest,derivativeError,finiteDifferenceError,mipError);
}

// B, G, R and A of pixel (x, y) in the gamma test's picture: no two pixels alike, no channel alike.
static void check_gpu_fft_quality_switch(SdlGpuFrame *frame)
{
    using namespace WaterFftSpectrum;
    CHECK(frame->Present(NULL));CHECK(frame->Prepare_Ocean(2.25f,256));
    const auto high=read_ocean(frame);SDL_GPUTexture *old=frame->Ocean_Map(0);
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;
    info.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=info.height=256;info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *reader=SDL_CreateGPUTexture(frame->Device(),&info);CHECK(reader);
    CHECK(frame->Present(NULL));
    const int32_t rect[4]={0,0,256,256};frame->Record_Blit(old,rect,reader,rect,false);
    CHECK(frame->Prepare_Ocean(2.25f,128));
    CHECK(frame->Ocean_Configuration().resolution==128 && frame->Ocean_Configuration().levels==8);
    CHECK(frame->Ocean_Map(0)!=old);
    const auto preserved=read_float_targets(frame,&reader,1,256,256);
    CHECK(preserved.size()==256*256*4 && std::equal(preserved.begin(),preserved.end(),high.begin()));
    SDL_ReleaseGPUTexture(frame->Device(),reader);
    const auto low=read_ocean(frame),initial=InitialSpectrum(ForResolution(128));
    CHECK(low.size()==6*128*128*4);check_fft_spectral_reality(initial,128);
    double maximum=0;
    for(unsigned c=0;c<3;++c) for(auto xy:{std::array<unsigned,2>{0,0},{17,53},{64,64},{127,7}}) {
        double expected[8];direct_ocean(initial,c,xy[0],xy[1],2.25,expected,128);
        for(unsigned channel=0;channel<8;++channel) {
            const double actual=low[((c*2+channel/4)*128*128+xy[1]*128+xy[0])*4+channel%4];
            const double error=std::abs(actual-expected[channel]);maximum=(std::max)(maximum,error);
            CHECK(std::isfinite(actual) && error<.001+std::abs(expected[channel])*.0001);
        }
    }
    for(unsigned level=1;level<8;++level) {
        const auto mip=read_ocean(frame,level);const unsigned side=128>>level,block=1u<<level;
        for(unsigned map=0;map<6;++map) for(unsigned channel=0;channel<4;++channel) {
            double sum=0;for(unsigned y=0;y<block;++y) for(unsigned x=0;x<block;++x) sum+=low[(map*128*128+y*128+x)*4+channel];
            CHECK(std::abs(mip[map*side*side*4+channel]-sum/(block*block))<.00001);
        }
    }
    const auto updates=frame->Ocean_Updates();CHECK(frame->Prepare_Ocean(2.25f,256));
    CHECK(frame->Ocean_Configuration().resolution==128 && frame->Ocean_Updates()==updates);
    CHECK(frame->Present(NULL));CHECK(frame->Prepare_Ocean(2.25f,256));
    CHECK(frame->Ocean_Configuration().resolution==256 && frame->Ocean_Updates()==updates+1);
    CHECK(read_ocean(frame)==high);
    CHECK(frame->Present(NULL));CHECK(frame->Prepare_Ocean(2.25f,128));
    CHECK(read_ocean(frame)==low);
    const auto beforeFailure=frame->Ocean_Updates();SDL_GPUTexture *lowTexture=frame->Ocean_Map(0);
    CHECK(frame->Present(NULL));frame->Fail_Next_Ocean_Rebuild_For_Test();
    CHECK(frame->Prepare_Ocean(2.25f,256));
    CHECK(frame->Ocean_Configuration().resolution==128 && frame->Ocean_Map(0)==lowTexture);
    CHECK(frame->Ocean_Updates()==beforeFailure && read_ocean(frame)==low);
    // A failed request is latched to avoid retrying allocation every frame; toggling
    // away and back explicitly retries it, preserving paused simulation time.
    CHECK(frame->Present(NULL));CHECK(frame->Prepare_Ocean(2.25f,256));
    CHECK(frame->Ocean_Configuration().resolution==128 && frame->Ocean_Updates()==beforeFailure);
    CHECK(frame->Prepare_Ocean(2.25f,128));
    CHECK(frame->Present(NULL));CHECK(frame->Prepare_Ocean(2.25f,256));
    CHECK(frame->Ocean_Configuration().resolution==256 && read_ocean(frame)==high);
    printf("sdl_gpu_frame_selfcheck: FFT128 direct Fourier max %.9g; all mip extents, queued old reader, same-time boundary switches and deterministic restoration checked\n",maximum);
}

static void check_gpu_water_opaque_alpha(SdlGpuFrame *frame)
{
    // Execute each generated program's actual final coverage block, then actual
    // source-alpha blending. Fractional authored and arriving vertex alpha must
    // not leak the destination through opaque refraction; shore coverage remains.
    struct Restore { WaterPerformanceSettings::Configuration saved=WaterPerformanceSettings::Applied();
        ~Restore(){WaterPerformanceSettings::Applied()=saved;} } restore;
    const unsigned width=12,height=1;
    const float fixtures[width][8]={
        {.35f,.4f,1,1, 0,0,0,0},{.35f,.4f,1,1, 1,0,0,0},
        {.35f,.4f,.25f,1, 0,0,0,0},{.35f,.4f,.25f,1, 1,0,0,0},
        {.35f,.4f,1,.125f, 0,0,0,0},{.35f,.4f,1,.125f, 1,0,0,0},
        {0,.4f,1,1, 0,0,0,0},{0,.4f,1,1, 1,0,0,0},
        {.35f,0,1,1, 0,0,0,0},{.35f,0,1,1, 1,0,0,0},
        {.0005f,.4f,1,1, 0,0,0,0},{.0005f,.4f,1,1, 1,0,0,0}};
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;
        SDL_GPUGraphicsPipeline *pipelines[2]={};SDL_GPUTexture *base=NULL,*edge=NULL,*targets[2]={};SDL_GPUSampler *sampler=NULL;
        void resetProgram(){for(auto *&p:pipelines)if(p){SDL_ReleaseGPUGraphicsPipeline(device,p);p=NULL;}
            if(pixel){SDL_ReleaseGPUShader(device,pixel);pixel=NULL;}}
        ~Resources(){resetProgram();if(vertex)SDL_ReleaseGPUShader(device,vertex);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
            if(base)SDL_ReleaseGPUTexture(device,base);if(edge)SDL_ReleaseGPUTexture(device,edge);
            for(auto *t:targets)if(t)SDL_ReleaseGPUTexture(device,t);}
    } r;r.device=frame->Device();
    SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture.width=width;texture.height=height;texture.layer_count_or_depth=texture.num_levels=1;
    r.base=SDL_CreateGPUTexture(r.device,&texture);r.edge=SDL_CreateGPUTexture(r.device,&texture);
    for(auto *&target:r.targets)target=SDL_CreateGPUTexture(r.device,&texture);
    SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;
    sampler.address_mode_u=sampler.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r.sampler=SDL_CreateGPUSampler(r.device,&sampler);CHECK(r.base&&r.edge&&r.targets[0]&&r.targets[1]&&r.sampler);
    if(!r.base||!r.edge||!r.targets[0]||!r.targets[1]||!r.sampler)return;
    for(unsigned field=0;field<2;++field){uint32_t offset=0;float *data=(float*)frame->Upload_Space(width*16,offset);
        for(unsigned x=0;x<width;++x){data[x*4]=data[x*4+1]=data[x*4+2]=1;data[x*4+3]=fixtures[x][field?2:0];}
        frame->Queue_Texture_Upload(field?r.edge:r.base,0,width,1,offset);}
    CHECK(frame->Flush());
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::string error;std::vector<unsigned char> spirv;CHECK(SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error));
    r.vertex=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vertex);if(!r.vertex)return;
    unsigned renders=0,armed=0;double maximum=0;
    for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID,ENGINE_SHADER_WATER_RIVER})for(auto quality:{WaterPerformanceSettings::Full,WaterPerformanceSettings::Off}) {
        r.resetProgram();auto &settings=WaterPerformanceSettings::Applied();settings.refraction=quality;settings.sceneReflections=false;
        PixelPipelineDescription pipeline={};std::string production;
        CHECK(EngineShader_Pixel_Program(program,pipeline,production,COMBINER_SHADER_TARGET_SDL3_GPU));
        const auto start=production.find("    float4 current;");
        const auto end=production.find('\n',production.find("    current = float4",start))+1;
        const auto define=production.find("#define WATER_REFRACTION_QUALITY "),defineEnd=production.find('\n',define);
        CHECK(start!=std::string::npos&&end>start&&define!=std::string::npos&&defineEnd!=std::string::npos);
        if(start==std::string::npos||end<=start||define==std::string::npos||defineEnd==std::string::npos)return;
        std::string ps=production.substr(define,defineEnd-define+1)+
            "Texture2D Texture0:register(t0,space2);SamplerState Sampler0:register(s0,space2);"
            "Texture2D Texture3:register(t3,space2);SamplerState Sampler3:register(s3,space2);"
            "cbuffer Queries:register(b0,space3){float4 F[24];};"
            "struct Input{float4 Diffuse;float2 TexCoord0;float2 TexCoord3;};"
            "float4 main(float4 pixel:SV_Position):SV_Target0{uint x=(uint)pixel.x;float4 a=F[x*2];"
            "Input input;input.Diffuse=float4(1,1,1,a.y);input.TexCoord3=float2((x+.5)/12.0,.5);"
            "input.TexCoord0=float2(input.TexCoord3.x*.5,.5);"
            "float4 base=Texture0.Sample(Sampler0,input.TexCoord3);float4 SceneParameters=F[x*2+1];"
            "float depth=a.w;float3 surface=float3(.2,.4,.6);"+
            production.substr(start,end-start)+"return current;}";
        bool compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
        if(!compiled){printf("opaque alpha fixture: %s\n",error.c_str());return;}
        r.pixel=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.pixel);if(!r.pixel)return;
        for(unsigned blend=0;blend<2;++blend){SDL_GPUColorTargetDescription colour={};colour.format=texture.format;
            colour.blend_state.enable_blend=blend;
            colour.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;colour.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colour.blend_state.color_blend_op=colour.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
            colour.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;colour.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
            SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=r.vertex;info.fragment_shader=r.pixel;
            info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
            r.pipelines[blend]=SDL_CreateGPUGraphicsPipeline(r.device,&info);CHECK(r.pipelines[blend]);if(!r.pipelines[blend])return;
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(r.device);CHECK(commands);if(!commands)return;
            SDL_GPUColorTargetInfo target={};target.texture=r.targets[blend];target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;
            target.clear_color={.9f,.1f,.3f,1};SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,NULL);CHECK(pass);if(!pass)return;
            SDL_BindGPUGraphicsPipeline(pass,r.pipelines[blend]);SDL_GPUTextureSamplerBinding bindings[4]={};
            for(unsigned i=0;i<4;++i){bindings[i].texture=i==3?r.edge:r.base;bindings[i].sampler=r.sampler;}
            SDL_BindGPUFragmentSamplers(pass,0,bindings,4);SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
        }
        const auto actual=read_float_targets(frame,r.targets,2,width,1);CHECK(actual.size()==width*4*2);if(actual.size()!=width*8)return;
        const bool river=program==ENGINE_SHADER_WATER_RIVER;
        for(unsigned x=0;x<width;++x){const auto &f=fixtures[x];const double authored=f[0]*(river?1:f[1]);
            const double fullCoverage=(river?f[2]:1)*std::clamp(2.0*double(f[3]),0.0,1.0);
            const double expected=fullCoverage*((quality==WaterPerformanceSettings::Off||f[4]>0)?(authored>=.001?1:0):authored);
            for(unsigned blend=0;blend<2;++blend)for(unsigned c=0;c<4;++c){const float raw[3]={.2f,.4f,.6f},background[3]={.9f,.1f,.3f};
                const double source=c==3?expected:raw[c]*(river?f[1]:1);
                const double value=c==3||!blend?source:source*expected+background[c]*(1-expected);
                const double difference=std::abs(actual[(blend*width+x)*4+c]-value);maximum=(std::max)(maximum,difference);CHECK(difference<.000003);
            }
            if(quality==WaterPerformanceSettings::Off&&!(x&1))for(unsigned blend=0;blend<2;++blend)for(unsigned c=0;c<4;++c)
                CHECK(actual[(blend*width+x)*4+c]==actual[(blend*width+x+1)*4+c]);
            armed+=quality==WaterPerformanceSettings::Off&&f[4]==0&&authored>0&&authored<1&&fullCoverage>0;
        }
    }
    CHECK(armed>=6);printf("sdl_gpu_frame_selfcheck: opaque refraction final alpha: both production programs, %u renders, fractional base/vertex alpha, absent/present colour, zero masks, shoreline fades and actual blending; max error %.9g, %u armed old-alpha controls\n",renders,maximum,armed);
}

static void pattern(unsigned int x, unsigned int y, uint8_t out[4])
{
	out[0] = (uint8_t)(x * 4);
	out[1] = (uint8_t)(y * 5);
	out[2] = (uint8_t)((x * 3 + y * 7) & 0xFF);
	out[3] = 0xFF;
}

// How many pixels are more than one level from expected in any channel.
static unsigned int mismatches(const std::vector<uint8_t> &got, const uint16_t (*ramp)[256])
{
	unsigned int bad = 0;
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			uint8_t in[4];
			pattern(x, y, in);
			const uint8_t *px = &got[(y * WIDTH + x) * 4];
			// The ramp is indexed R, G, B; the bytes are B, G, R.
			const int expect[4] = {
				(int)((ramp[2][in[0]] * 255u + 32767u) / 65535u),
				(int)((ramp[1][in[1]] * 255u + 32767u) / 65535u),
				(int)((ramp[0][in[2]] * 255u + 32767u) / 65535u),
				in[3] };
			for (int c = 0; c < 4; ++c) {
				if (abs((int)px[c] - expect[c]) > 1) {
					++bad;
					break;
				}
			}
		}
	}
	return bad;
}

#if defined(__linux__)
static void check_sun_depth_target(SdlGpuFrame *frame)
{
	SdlTarget sun;
	std::string error;
	CHECK(frame->Sun_Shadow_Target(32, sun, error));
	CHECK(sun.Colour == NULL && sun.Depth != NULL && sun.Width == 32 && sun.Height == 32);
	CHECK(frame->Target_Depth_Format(sun) == SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
	if (!sun.Depth) return;
	DepthObject caster(frame, true);
	frame->Set_Target(frame->Back_Buffer_Target());
	frame->Clear_Back_Buffer(true, true, true, 0xff123456, 0.8f, 0);
	frame->Set_Target(sun);
	frame->Clear_Back_Buffer(false, true, false, 0, 1.0f, 0);
	caster.Draw(sun, 0.25f);
	frame->Set_Target(frame->Back_Buffer_Target());
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer_Target().Colour, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4);
	for (size_t i = 0; i + 3 < pixels.size(); i += 4)
		CHECK(pixels[i] == 0x56 && pixels[i+1] == 0x34 && pixels[i+2] == 0x12 && pixels[i+3] == 255);

	const char *vs = "float4 main(uint id:SV_VertexID):SV_Position { return float4(id==1?3:-1,id==2?3:-1,0,1); }";
	const char *ps =
		"Texture2D<float> depthMap:register(t0,space2); SamplerState pointSampler:register(s0,space2);"
		"cbuffer Size:register(b0,space3) { float4 size; };"
		"float4 main(float4 p:SV_Position):SV_Target0 { float d=depthMap.SampleLevel(pointSampler,p.xy/size.xy,0); return float4(d,d,d,1); }";
	std::vector<unsigned char> spirv;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vs, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(ps, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	SDL_GPUColorTargetDescription colour = {};
	colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	SDL_GPUGraphicsPipelineCreateInfo info = {};
	info.vertex_shader = vertex; info.fragment_shader = pixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.target_info.color_target_descriptions = &colour; info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
	SDL_GPUSamplerCreateInfo samplerInfo = {};
	samplerInfo.address_mode_u = samplerInfo.address_mode_v = samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	SDL_GPUSampler *sampler = SDL_CreateGPUSampler(frame->Device(), &samplerInfo);
	CHECK(vertex && pixel && pipeline && sampler);
	if (pipeline && sampler) {
		auto sample = [&](SDL_GPUTexture *texture) {
			const SdlTarget scene = frame->Back_Buffer_Target();
			frame->Set_Target(scene);
			SdlRecordedDraw draw = {};
			draw.Pipeline = pipeline; draw.Count = 3;
			memset(frame->Stage(16, draw.VertexOffset), 0, 16);
			const float size[4] = { (float)scene.Width, (float)scene.Height, 0, 0 };
			draw.PixelConstants = frame->Constants(1, size, sizeof(size)); draw.PixelConstantsSize = sizeof(size);
			draw.SamplerCount = 1; draw.Textures[0] = texture; draw.Samplers[0] = sampler;
			draw.Viewport[2] = draw.Scissor[2] = scene.Width;
			draw.Viewport[3] = draw.Scissor[3] = scene.Height; draw.Viewport[5] = 1;
			frame->Record_Draw(draw);
		};
		auto verify = [&](float expected) {
			const SdlTarget scene = frame->Back_Buffer_Target();
			CHECK(frame->Read_Back(scene.Colour, scene.Width, scene.Height, pixels));
			if (pixels.size() != scene.Width * scene.Height * 4) { CHECK(false); return; }
			CHECK(abs((int)pixels[((scene.Height/2)*scene.Width+scene.Width/2)*4] - (int)(expected*255+0.5f)) <= 1);
			CHECK(pixels[0] == 255); // clear depth outside the triangle
		};
		sample(sun.Depth);
		SdlTarget replacement;
		CHECK(frame->Sun_Shadow_Target(64, replacement, error));
		CHECK(replacement.Depth != sun.Depth);
		verify(0.25f); // queued sampling still owns the retired 32x32 resource
		frame->Set_Target(replacement);
		frame->Clear_Back_Buffer(false, true, false, 0, 1, 0);
		caster.Draw(replacement, 0.6f);
		sample(replacement.Depth);
		SdlTarget refused;
		CHECK(!frame->Sun_Shadow_Target(0, refused, error) && refused.Depth == NULL);
		CHECK(!frame->Sun_Shadow_Target(8193, refused, error) && refused.Depth == NULL);
		verify(0.6f);
		CHECK(frame->Resize(WIDTH+16, HEIGHT+8));
		SdlTarget kept;
		CHECK(frame->Sun_Shadow_Target(64, kept, error) && kept.Depth == replacement.Depth);
		sample(kept.Depth); verify(0.6f); // window resizing does not invalidate the sun allocation
		frame->Set_Target(kept);
		frame->Clear_Back_Buffer(false, true, false, 0, 1, 0);
		sample(kept.Depth); verify(1.0f); // empty capture clears stale caster depth
		CHECK(frame->Resize(WIDTH, HEIGHT));
	}
	frame->Set_Target(frame->Back_Buffer_Target());
	CHECK(frame->Flush());
	if (pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(), pipeline);
	if (sampler) SDL_ReleaseGPUSampler(frame->Device(), sampler);
	if (vertex) SDL_ReleaseGPUShader(frame->Device(), vertex);
	if (pixel) SDL_ReleaseGPUShader(frame->Device(), pixel);
	printf("sun depth target: depth-only draw/sample, scene preservation, empty clear, retirement and resize checked\n");
}

static void check_gpu_water_shore_front(SdlGpuFrame *frame)
{
    using namespace WaterShoreFoam;
    struct Query {float position[4],normal[4],footprint[4],context[4],gates[4];};
    std::vector<Query> queries;
    const float widths[]={0,.0001f,.01f,.2f,1.f,3.f,6.283185307f,9.f};
    for(unsigned i=0;i<32;++i)queries.push_back({{float((i*7)%13)-6,float((i*3)%11)-5,float(i)*.41f,float(i)*.23f-1},
        {-1,0,widths[i%8],float(i%4)*.2f},{float(i%4)*.5f,0,0,float(i%3)*.75f},
        {2,1,.1f,1},{1,1,1,.2f}});
    Query q=queries[7];q.normal[0]=1;queries.push_back(q);
    q=queries[7];q.gates[0]=0;queries.push_back(q);q=queries[7];q.gates[1]=0;queries.push_back(q);
    q=queries[7];q.gates[2]=0;queries.push_back(q);q=queries[7];q.context[0]=.15f;queries.push_back(q);
    q=queries[7];q.context[0]=20;queries.push_back(q);q=queries[7];q.normal[0]=q.normal[1]=0;queries.push_back(q);
    const unsigned rows=unsigned(queries.size()),columns=3;CHECK(rows*sizeof(Query)<=4096);if(rows*sizeof(Query)>4096)return;
    PixelPipelineDescription description={};std::string production;
    const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU);
    CHECK(generated);if(!generated)return;
    const std::string helper=Source();const size_t at=production.find(helper);CHECK(at!=std::string::npos);if(at==std::string::npos)return;
    CHECK(production.find("terrainSlope,opticalContext.b,shoreSupport)*shoreFront.x;")!=std::string::npos);
    CHECK(production.find("foam=waterShoreBlendFrontHistory(foam,shoreRate,shoreFront.y,shoreHistory,")!=std::string::npos);
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::string ps="struct Query{float4 position,normal,footprint,context,gates;};cbuffer FrontQueries:register(b0){Query Queries["+std::to_string(rows)+"];};\n"+
        production.substr(at,helper.size())+R"FRONT(
float4 main(float4 pixel:SV_Position):SV_Target0 {
    Query q=Queries[(uint)pixel.y];float2 front;
    float4 wave=waterShoreWaveFront(q.position.xy,q.position.z,q.normal.xy,q.footprint.xy,q.footprint.zw,front);
    float rate=waterShoreRate(wave,q.context.x,q.context.y,q.normal.xy,q.context.z,q.context.w,q.gates.x)*front.x;
    if(pixel.x<1)return float4(front,rate,waterShoreFrontFresh(rate,front.y));
    if(pixel.x<2)return float4(waterShoreBlendFrontHistory(q.gates.w,rate,front.y,q.normal.w,q.gates.x,q.gates.y,q.gates.z),wave.xyz);
    float s=sin(q.position.w),c=cos(q.position.w);
    return float4(waterShoreFrontKernel(s,c),waterShoreFrontFiltered(s,c,q.normal.z),waterShoreFresh(rate),wave.w);
}
)FRONT";
    const bool placed=sdl3target_detail::place_registers(ps,false);CHECK(placed);if(!placed)return;
    struct Resources {
        SDL_GPUDevice*device;SDL_GPUShader*vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline*pipeline=nullptr;SDL_GPUTexture*target=nullptr;
        ~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
    }r;r.device=frame->Device();std::vector<unsigned char>spirv;std::string error;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);if(!r.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(ok);if(!ok){printf("shore front PS: %s\n",error.c_str());return;}
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0&&buffers==1);if(samplers!=0||buffers!=1)return;
    r.ps=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps);if(!r.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.fragment_shader=r.ps;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    r.pipeline=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipeline);if(!r.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    CHECK(frame->Flush());
    auto render=[&](){SDL_GPUCommandBuffer*cmd=SDL_AcquireGPUCommandBuffer(r.device);CHECK(cmd);if(!cmd)return std::vector<float>();
        SDL_GPUColorTargetInfo target={};target.texture=r.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass*pass=SDL_BeginGPURenderPass(cmd,&target,1,nullptr);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(cmd);return std::vector<float>();}
        SDL_BindGPUGraphicsPipeline(pass,r.pipeline);SDL_PushGPUFragmentUniformData(cmd,0,queries.data(),unsigned(rows*sizeof(Query)));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(cmd));return read_float_targets(frame,&r.target,1,columns,rows);};
    const auto actual=render(),paused=render();CHECK(actual==paused&&actual.size()==rows*columns*4);if(actual.size()!=rows*columns*4)return;
    // Independent double reference: locate the harmonic crest numerically and integrate
    // pixel support by midpoint samples, not the shader's periodic antiderivative.
    const double piD=std::acos(-1.0),tauD=2*piD;double lo=0,hi=piD*.5;
    for(unsigned n=0;n<48;++n){double mid=(lo+hi)*.5;if(std::cos(mid)+.26*std::cos(2*mid)>0)lo=mid;else hi=mid;}
    const double crest=(lo+hi)*.5,crestCos=std::cos(crest),durationPhase=piD-crest;
    auto kernel=[&](double phase){phase-=tauD*std::floor(phase/tauD);if(phase<=crest||phase>=piD)return 0.0;
        const double u=(crestCos-std::cos(phase))/(1+crestCos);return 4*u*(1-u);};
    auto average=[&](double phase,double width){if(width==0)return kernel(phase);double sum=0;
        for(unsigned i=0;i<8192;++i)sum+=kernel(phase+width*((i+.5)/8192-.5));return sum/8192;};
    auto smooth=[](double a,double b,double x){double t=(std::max)(0.0,(std::min)(1.0,(x-a)/(b-a)));return t*t*(3-2*t);};
    double maximum=0;unsigned active=0,changed=0,filtered=0;
    for(unsigned row=0;row<rows;++row){const auto&p=queries[row];double wave[4]={},mask=0,residence=0,total=0;
        const double nl=std::hypot(p.normal[0],p.normal[1]);
        for(unsigned mode=0;mode<ModeCount;++mode){const auto&m=Modes[mode];const double phase=double(p.position[0])*m.kx+double(p.position[1])*m.ky-double(p.position[2])*m.omega+m.phase;
            const double derivative=m.amplitude*(std::cos(phase)+.26*std::cos(2*phase));wave[0]+=m.amplitude*(std::sin(phase)+.13*std::sin(2*phase));
            wave[1]+=derivative*m.kx;wave[2]+=derivative*m.ky;wave[3]-=derivative*m.omega;
            const double weight=nl>1e-8?m.amplitude*m.amplitude*smooth(0,.25,-(m.kx*p.normal[0]+m.ky*p.normal[1])/std::hypot(m.kx,m.ky)/nl):0;
            const double width=std::abs(m.kx*p.footprint[0]+m.ky*p.footprint[1])+std::abs(m.kx*p.footprint[2]+m.ky*p.footprint[3]);
            const double pulse=weight*average(phase,width);mask+=pulse;residence+=pulse*durationPhase/m.omega;total+=weight;
        }
        if(mask>0)residence/=mask;if(total>0)mask/=total;
        const double depth=p.context[0],shore=p.context[1],slope=p.context[2],exposure=p.context[3],support=p.gates[0],wl=std::hypot(wave[1],wave[2]);
        double rate=0;
        if(depth>double(.15f)&&support>0&&wave[0]>0&&nl>1e-8&&wl>1e-8&&std::abs(wave[3])>1e-8){
            const double approach=smooth(0,.25,(wave[3]>0?1:-1)*(wave[1]*p.normal[0]+wave[2]*p.normal[1])/wl/nl);
            const double a=smooth(0,9,depth),beach=smooth(.4,.85,2*wave[0]/(std::max)(depth+.5,1.0))*(1-a*a);
            const double rock=1-smooth(0,6,(std::max)(0.0,shore));rate=1.2*support*smooth(.15,.8,depth)*approach*(.15+.85*exposure)
                *(beach+(rock-beach)*smooth(.35,1,slope))*smooth(0,.5,wave[0])*mask;
        }
        const double fresh=-std::expm1(-rate*residence),old=-std::expm1(-rate*.35);
        const double blend=p.gates[3]*(1-support*p.gates[2])+(std::max)(fresh,double(p.normal[3]))*p.gates[1]*p.gates[2];
        const double expected[12]={mask,residence,rate,fresh,blend,wave[0],wave[1],wave[2],kernel(p.position[3]),average(p.position[3],p.normal[2]),old,wave[3]};
        for(unsigned c=0;c<12;++c){const double error=std::abs(actual[row*12+c]-expected[c]);maximum=(std::max)(maximum,error);
            if(!(std::isfinite(actual[row*12+c])&&error<.0015))printf("shore front mismatch row=%u channel=%u actual=%.9g expected=%.9g\n",row,c,double(actual[row*12+c]),expected[c]);
            CHECK(std::isfinite(actual[row*12+c])&&error<.0015);}
        active+=rate>.02;changed+=fresh>old+.05;filtered+=std::abs(expected[8]-expected[9])>.02;
    }
    CHECK(active>5&&changed>5&&filtered>5);
    printf("sdl_gpu_frame_selfcheck: phase-locked shore front %u cases, independent integrated phase/rate/residence/blend max %.8g; %u active, %u old-cap controls, %u footprint controls, exact pause\n",rows,maximum,active,changed,filtered);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_water_shore_source(SdlGpuFrame *frame)
{
    // Production source, independently checked against double scalar height and analytic
    // source expectations. No texture/readback is added to the game path by this fixture.
    using namespace WaterShoreFoam;
    struct Query {float position[4],wave[4],context[4],shape[4];};std::vector<Query> queries;
    for(unsigned i=0;i<16;++i)queries.push_back({{float(i*13)-64,float((i*7)%11)-5,float(i)*.43f,0},
        {0,0,0,0},{2,1,1,0},{.1f,1,1,0}});
    const Query incoming={{0,0,0,0},{2,.3f,0,1},{2,1,1,0},{.1f,1,1,1}};
    queries.push_back(incoming);Query q=incoming;q.wave[3]=-1;queries.push_back(q);
    q=incoming;q.wave[0]=-1;queries.push_back(q);q=incoming;q.wave[1]=0;queries.push_back(q);
    q=incoming;q.context[0]=.15f;queries.push_back(q);q=incoming;q.context[0]=20;queries.push_back(q);
    q=incoming;q.shape[1]=0;queries.push_back(q);q=incoming;q.shape[2]=0;queries.push_back(q);
    q=incoming;q.shape[0]=1;q.context[1]=5;queries.push_back(q);q.context[1]=7;queries.push_back(q);
    q=incoming;q.context[2]=-1;queries.push_back(q);q=incoming;q.context[2]=0;q.context[3]=1;queries.push_back(q);
    q=incoming;q.shape[2]=.5f;queries.push_back(q);q=incoming;q.wave[3]=0;queries.push_back(q);
    q=incoming;q.context[0]=NAN;queries.push_back(q);q=incoming;q.wave[0]=INFINITY;queries.push_back(q);
    const unsigned rows=unsigned(queries.size()),columns=2;CHECK(sizeof(Query)*rows<=4096);
    PixelPipelineDescription description={};std::string production;
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU));
    CHECK(production.find("foam=waterShoreBlendFrontHistory(")!=std::string::npos);
    CHECK(production.find("opticalContext.r==0.0")!=std::string::npos);
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    const std::string ps="struct Query{float4 position,wave,context,shape;};cbuffer QueriesBlock:register(b0,space3){Query Queries["+std::to_string(rows)+"];};\n"+
        Source()+"float4 main(float4 pixel:SV_Position):SV_Target0{Query q=Queries[(uint)pixel.y];"
        "float4 w=waterShoreWave(q.position.xy,q.position.z);if(pixel.x<1)return w;"
        "if(q.shape.w>.5)w=q.wave;float r=waterShoreRate(w,q.context.x,q.context.y,q.context.zw,q.shape.x,q.shape.y,q.shape.z);"
        "return float4(r,waterShoreFresh(r),waterShoreBlend(.2,r,q.shape.z,1,1),waterShoreBlend(.2,r,0,1,0));}";
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;SDL_GPUTexture *target=nullptr;
        ~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
    } resources;resources.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(ok);if(!ok){printf("shore source PS: %s\n",error.c_str());return;}
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==0 && buffers==1);
    resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vs;pi.fragment_shader=resources.ps;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
    SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;CHECK(frame->Flush());
    auto render=[&]() {
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
        SDL_GPUColorTargetInfo target={};target.texture=resources.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);CHECK(pass);
        SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);SDL_PushGPUFragmentUniformData(commands,0,queries.data(),unsigned(queries.size()*sizeof(Query)));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
        return read_float_targets(frame,&resources.target,1,columns,rows);
    };
    const auto values=render(),paused=render();CHECK(values==paused);CHECK(values.size()==rows*columns*4);if(values.size()!=rows*columns*4)return;
    auto scalar=[](double x,double y,double t){double h=0;for(unsigned i=0;i<ModeCount;++i){const auto &m=Modes[i];const double p=x*m.kx+y*m.ky-t*m.omega+m.phase;h+=m.amplitude*(std::sin(p)+.13*std::sin(2*p));}return h;};
    auto smooth=[](double a,double b,double x){double t=(std::max)(0.0,(std::min)(1.0,(x-a)/(b-a)));return t*t*(3-2*t);};
    double maxWave=0,maxRate=0;unsigned active=0;
    for(unsigned row=0;row<rows;++row) {
        const Query &p=queries[row];const double x=p.position[0],y=p.position[1],t=p.position[2],eps=.0001;
        double w[4]={scalar(x,y,t),(scalar(x+eps,y,t)-scalar(x-eps,y,t))/(2*eps),
            (scalar(x,y+eps,t)-scalar(x,y-eps,t))/(2*eps),(scalar(x,y,t+eps)-scalar(x,y,t-eps))/(2*eps)};
        for(unsigned c=0;c<4;++c){const double err=std::abs(values[(row*columns)*4+c]-w[c]);maxWave=(std::max)(maxWave,err);CHECK(err<.00008);}
        if(p.shape[3]>.5f)for(unsigned c=0;c<4;++c)w[c]=p.wave[c];
        const double depth=p.context[0],shore=p.context[1],nx=p.context[2],ny=p.context[3],slope=p.shape[0],exposure=p.shape[1],support=p.shape[2];
        double expected=0;const double nl=std::hypot(nx,ny),wl=std::hypot(w[1],w[2]);
        if(std::isfinite(depth)&&std::isfinite(w[0])&&depth>.15f&&support>0&&w[0]>0&&nl>1e-8&&wl>1e-8&&std::abs(w[3])>1e-8) {
            const double incoming=smooth(0,.25,w[3]*(w[1]*nx+w[2]*ny)/nl/(std::max)(std::abs(w[3])*wl,1e-8));
            const double a=smooth(0,9,depth),beach=smooth(.4,.85,2*w[0]/(std::max)(depth+.5,1.0))*(1-a*a);
            const double rock=1-smooth(0,6,(std::max)(0.0,shore)),steep=smooth(.35,1,slope);
            expected=1.2*support*smooth(.15,.8,depth)*incoming*(.15+.85*exposure)*(beach+(rock-beach)*steep)*smooth(0,.5,w[0]);
        }
        const double fresh=-std::expm1(-expected*.35),expectedValues[4]={expected,fresh,.2*(1-support)+fresh,.2};
        for(unsigned c=0;c<4;++c){const float actual=values[(row*columns+1)*4+c];const double err=std::abs(actual-expectedValues[c]);maxRate=(std::max)(maxRate,err);CHECK(std::isfinite(actual)&&err<.00015);}
        if(expected>.02)++active;
    }
    CHECK(active>=5);
    CHECK(values[(16*columns+1)*4]>.5f && values[(17*columns+1)*4]==0 && values[(18*columns+1)*4]==0);
    printf("sdl_gpu_frame_selfcheck: coastal source %u queries: wave/independent space-time %.8f, rate/blend %.8f; incoming/outgoing, trough, exposure, dry/deep, invalid, exact pause and fallback checked\n",rows,maxWave,maxRate);
}
#endif // __linux__


static void check_gpu_authored_shore_overlay(SdlGpuFrame *frame)
{
    // Execute the production wrapper around the real two-stage fixed-function combiner.
    // A separate unwrapped program is the exact fallback oracle for colour/shroud/fog/alpha.
    CombinerDescription d={};d.StageCount=2;d.PixelPipeline.FogEnabled=true;
    for(unsigned i=0;i<2;++i){auto &s=d.Stages[i];s.TextureBound=true;s.TextureCoordinateIndex=i;
        s.ColourOperation=FF_TOP_MODULATE;s.ColourArgument1=FF_TA_TEXTURE;s.ColourArgument2=FF_TA_CURRENT;
        s.AlphaOperation=i==0?FF_TOP_MODULATE:FF_TOP_SELECTARG2;s.AlphaArgument1=FF_TA_TEXTURE;s.AlphaArgument2=FF_TA_CURRENT;}
    std::string ps[2];CHECK(CombinerShader_Generate(d,COMBINER_SHADER_TARGET_D3D11,ps[0]));ps[1]=ps[0];
    CHECK(WaterShoreOverlay::WrapPixel(ps[1]));CHECK(SDL3_Shader_Retarget(ps[0],false));CHECK(SDL3_Shader_Retarget(ps[1],false));
    const char *vs=R"OVERLAY(
struct Output {
 float4 Position:SV_Position;
 [[vk::location(0)]] float4 Diffuse:COLOR0;
 [[vk::location(1)]] float4 Specular:COLOR1;
 [[vk::location(2)]] float2 TexCoord0:TEXCOORD0;
 [[vk::location(3)]] float2 TexCoord1:TEXCOORD1;
 [[vk::location(4)]] float2 TexCoord2:TEXCOORD2;
 [[vk::location(5)]] float2 TexCoord3:TEXCOORD3;
 [[vk::location(10)]] float Fog:FOG;
};
Output main(uint id:SV_VertexID) {
 Output o;o.Position=float4(id==1?3:-1,id==2?3:-1,.5,1);
 o.Diffuse=float4(.8,.6,.4,.75);o.Specular=0;
 o.TexCoord0=o.TexCoord1=o.TexCoord2=o.TexCoord3=.5.xx;o.Fog=.4;return o;
}
)OVERLAY";
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};
        SDL_GPUTexture *target=nullptr,*textures[7]={};SDL_GPUSampler *point=nullptr;
        ~Resources(){for(auto *p:pipelines)if(p)SDL_ReleaseGPUGraphicsPipeline(device,p);
            for(auto *p:ps)if(p)SDL_ReleaseGPUShader(device,p);if(vs)SDL_ReleaseGPUShader(device,vs);
            for(auto *t:textures)if(t)SDL_ReleaseGPUTexture(device,t);if(target)SDL_ReleaseGPUTexture(device,target);
            if(point)SDL_ReleaseGPUSampler(device,point);}
    } r;r.device=frame->Device();std::vector<unsigned char> spirv;std::string error;bool ok;
    ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    r.vs=SDL3_Create_Shader(r.device,spirv,true,error);CHECK(r.vs);if(!r.vs)return;
    unsigned samplerCount[2]={};
    for(unsigned i=0;i<2;++i){ok=SDL3_Compile_HLSL_To_SPIRV(ps[i],false,spirv,error);CHECK(ok);
        if(!ok){printf("overlay PS: %s\n",error.c_str());return;}unsigned buffers=0;SDL3_Shader_Slots(spirv,false,samplerCount[i],buffers);
        CHECK(buffers==1 && samplerCount[i]<=7);r.ps[i]=SDL3_Create_Shader(r.device,spirv,false,error);CHECK(r.ps[i]);if(!r.ps[i])return;}
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=r.vs;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    for(unsigned i=0;i<2;++i){pi.fragment_shader=r.ps[i];r.pipelines[i]=SDL_CreateGPUGraphicsPipeline(r.device,&pi);CHECK(r.pipelines[i]);if(!r.pipelines[i])return;}
    const unsigned W=8,H=4,S=8;SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;
    ti.width=W;ti.height=H;ti.layer_count_or_depth=ti.num_levels=1;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    r.target=SDL_CreateGPUTexture(r.device,&ti);CHECK(r.target);if(!r.target)return;
    ti.height=S;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    for(auto &t:r.textures){t=SDL_CreateGPUTexture(r.device,&ti);CHECK(t);if(!t)return;}
    SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    r.point=SDL_CreateGPUSampler(r.device,&si);CHECK(r.point);if(!r.point)return;
    float data[7][S*S][4]={};for(unsigned y=0;y<S;++y)for(unsigned x=0;x<S;++x){
        const unsigned k=y*S+x;float raw=.25f;if(x==3)raw=.75f;if(x==4)raw=1;if(x==5)raw=-.01f;
        if(y==2 && x==4)raw=NAN;if(y==2 && x==5)raw=.999999f;
        const float wave[4]={.7f,.8f,.9f,.6f},shroud[4]={.4f,.5f,.6f,1};
        memcpy(data[0][k],wave,16);memcpy(data[1][k],shroud,16);
        data[2][k][0]=4*raw+8; // real terrain at the sampled scene hit, both wet and dry
        if(y==2 && x==2)data[2][k][0]-=2; // submerged/foreground object differs from terrain
        // Broad authored surf traverses both the narrow breaking support and the
        // analytic/FFT transition. Neither changes proven modern-patch ownership.
        data[4][k][0]=float(x%3)*.5f;data[4][k][3]=float(y%3)*.5f;data[6][k][0]=raw;
    }
    CHECK(frame->Flush());SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;bi.size=sizeof(data);
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(r.device,&bi);CHECK(transfer);if(!transfer)return;
    void *mapped=SDL_MapGPUTransferBuffer(r.device,transfer,false);CHECK(mapped);if(!mapped){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
    memcpy(mapped,data,sizeof(data));SDL_UnmapGPUTransferBuffer(r.device,transfer);
    SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(r.device);CHECK(upload);if(!upload){SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);if(!copy){SDL_CancelGPUCommandBuffer(upload);SDL_ReleaseGPUTransferBuffer(r.device,transfer);return;}
    for(unsigned i=0;i<7;++i){SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.offset=i*sizeof(data[0]);from.pixels_per_row=S;from.rows_per_layer=S;
        SDL_GPUTextureRegion to={};to.texture=r.textures[i];to.w=to.h=S;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);}
    SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(r.device,transfer);
    SdlShorePixelConstants u={};u.Combiner.FogColour[0]=.1f;u.Combiner.FogColour[1]=.2f;u.Combiner.FogColour[2]=.3f;
    u.WorldFromClip[0]=2;u.WorldFromClip[5]=-1;u.WorldFromClip[10]=4;u.WorldFromClip[12]=4.5f;u.WorldFromClip[13]=2.5f;u.WorldFromClip[14]=8;u.WorldFromClip[15]=1;
    u.Viewport[0]=2.5f;u.Viewport[1]=1.5f;u.Viewport[2]=4;u.Viewport[3]=2;
    u.Terrain[0]=-5;u.Terrain[1]=S;u.Terrain[2]=u.Terrain[3]=1;
    u.Scene[1]=1.f/W;u.Scene[2]=1.f/S;u.Scene[3]=1;u.Surface[0]=10;u.Surface[1]=11.5f;u.Surface[2]=1;
    auto render=[&](unsigned which){SDL_GPUCommandBuffer *cmd=SDL_AcquireGPUCommandBuffer(r.device);CHECK(cmd);if(!cmd)return std::vector<float>();
        SDL_GPUColorTargetInfo out={};out.texture=r.target;out.load_op=SDL_GPU_LOADOP_DONT_CARE;out.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(cmd,&out,1,nullptr);CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(cmd);return std::vector<float>();}
        SDL_BindGPUGraphicsPipeline(pass,r.pipelines[which]);SDL_GPUTextureSamplerBinding bindings[7];for(unsigned i=0;i<7;++i)bindings[i]={r.textures[i],r.point};
        SDL_BindGPUFragmentSamplers(pass,0,bindings,samplerCount[which]);SDL_PushGPUFragmentUniformData(cmd,0,&u,which?sizeof(u):sizeof(u.Combiner));
        SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(cmd));return read_float_targets(frame,&r.target,1,W,H);};
    const auto legacy=render(0);CHECK(legacy.size()==W*H*4);if(legacy.size()!=W*H*4)return;
    unsigned wet=0,dry=0,preserved=0;double maximum=0;
    for(unsigned mode=0;mode<4;++mode){u.Scene[3]=mode==1?0:1;u.Terrain[3]=mode==2?0:1;u.Surface[2]=mode==3?0:1;
        const auto actual=render(1),paused=render(1);CHECK(actual==paused && actual.size()==legacy.size());if(actual.size()!=legacy.size())return;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned sx=std::max(2u,std::min(5u,x)),sy=std::max(1u,std::min(2u,y)),k=sy*S+sx,p=(y*W+x)*4;
            // The depth resource and shader threshold are float32. Promote the actual
            // float threshold too, so equality cannot accidentally become a real hit.
            const double raw=data[6][k][0];const bool hit=std::isfinite(raw)&&raw>=0&&raw<double(.999999f);
            const bool ground=std::isfinite(data[2][k][0])&&std::abs((4*raw+8)-data[2][k][0])<.5;
            const bool removed=mode==0 && hit && ground;
            for(unsigned c=0;c<4;++c){const double expected=c==3&&removed?0:legacy[p+c];const double err=std::abs(actual[p+c]-expected);maximum=std::max(maximum,err);
                if(!(std::isfinite(actual[p+c])&&err<.000001))printf("shore-overlay mismatch mode=%u pixel=(%u,%u) channel=%u source=(%u,%u) raw=%.9g ground=%.9g expected=%.9g actual=%.9g\n",mode,x,y,c,sx,sy,raw,double(data[2][k][0]),expected,double(actual[p+c]));
                CHECK(std::isfinite(actual[p+c])&&err<.000001);}
            if(mode==0){if(removed){if(raw<.5)++wet;else ++dry;}else ++preserved;}
        }
    }
    CHECK(wet>0 && dry>0 && preserved>0);
    printf("sdl_gpu_frame_selfcheck: authored surf production wrapper: dry/wet replacement %u/%u, %u object/invalid fallbacks, max %.8f; original RGB/fog/shroud, unavailable resources and pause preserved\n",dry,wet,preserved,maximum);
}

#if defined(__linux__)
static void check_gpu_water_shore_history(SdlGpuFrame *frame)
{
    // Real production alpha sampler and non-double-counting composition, with independently
    // uploaded RGBA channels. RGB-only, wrong-level, window, time and replacement cases.
    PixelPipelineDescription description={};std::string production;
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU));
    CHECK(production.find("foam=waterShoreBlendFrontHistory(")!=std::string::npos);
    CHECK(production.find(WaterShoreFoam::HistorySource())!=std::string::npos);
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    const std::string ps=R"SHORE(
cbuffer ShoreCase:register(b0,space3) {float4 FoamHistoryArea,FoamHistoryParameters,Query;};
Texture2D<float4> Texture14:register(t0,space2);SamplerState Sampler14:register(s0,space2);
)SHORE"+std::string(WaterFoamShading::HistorySamplingSource())+WaterShoreFoam::Source()+WaterShoreFoam::HistorySource()+R"SHORE(
float4 main(float4 pixel:SV_Position):SV_Target0 {
    float2 p=pixel.xy*8.0+Query.ww;
    float retained=waterShoreHistory(p,Query.x,Query.y);
    float mixed=waterShoreBlendHistory(.2,Query.z,retained,1,1,1);
    float3 rgb=Texture14.SampleLevel(Sampler14,(p-FoamHistoryArea.xy)*FoamHistoryArea.zw,0).rgb;
    return float4(retained,mixed,rgb.r,rgb.g+rgb.b);
}
)SHORE";
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;
        SDL_GPUTexture *target=nullptr,*atlas=nullptr;SDL_GPUSampler *linear=nullptr;
        ~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(atlas)SDL_ReleaseGPUTexture(device,atlas);
            if(linear)SDL_ReleaseGPUSampler(device,linear);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
    } resources;resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(ok);if(!ok){printf("shore history PS: %s\n",error.c_str());return;}
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==1&&buffers==1);
    resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vs;pi.fragment_shader=resources.ps;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
    const unsigned side=8;SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=ti.height=side;ti.layer_count_or_depth=ti.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
    ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;resources.atlas=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.atlas);if(!resources.atlas)return;
    SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    resources.linear=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.linear);if(!resources.linear)return;
    std::vector<float> pixels(side*side*4);
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
        const size_t i=(y*side+x)*4;pixels[i]=.83f;pixels[i+1]=.17f;pixels[i+2]=.31f;
        pixels[i+3]=.1f+.02f*x+.03f*y;
    }
    auto upload=[&]() {
        SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;bi.size=unsigned(pixels.size()*sizeof(float));
        SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(resources.device,&bi);CHECK(transfer);if(!transfer)return false;
        void *mapped=SDL_MapGPUTransferBuffer(resources.device,transfer,false);CHECK(mapped);
        if(!mapped){SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
        memcpy(mapped,pixels.data(),bi.size);SDL_UnmapGPUTransferBuffer(resources.device,transfer);
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
        if(!commands){SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
        if(!copy){SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
        SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.pixels_per_row=from.rows_per_layer=side;
        SDL_GPUTextureRegion to={};to.texture=resources.atlas;to.w=to.h=side;to.d=1;
        SDL_UploadToGPUTexture(copy,&from,&to,true);SDL_EndGPUCopyPass(copy);
        const bool result=SDL_SubmitGPUCommandBuffer(commands);CHECK(result);SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return result;
    };
    CHECK(frame->Flush());if(!upload())return;
    auto render=[&](float flags,float level,float time,float offset,float rate) {
        const float uniforms[12]={0,0,1.0f/64,1.0f/64,flags,4,8,1,level,time,rate,offset};
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return std::vector<float>{};
        SDL_GPUColorTargetInfo target={};target.texture=resources.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);CHECK(pass);
        if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>{};}
        SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
        const SDL_GPUTextureSamplerBinding binding={resources.atlas,resources.linear};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
        SDL_PushGPUFragmentUniformData(commands,0,uniforms,sizeof(uniforms));SDL_DrawGPUPrimitives(pass,3,1,0,0);
        SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
        return read_float_targets(frame,&resources.target,1,side,side);
    };
    const float cases[][5]={{2,4,1,0,0},{1,4,1,0,0},{3,4,1,0,0},{2,9,1,0,0},
        {2,4,1.1f,0,0},{2,4,1,2,0},{2,4,1,-80,0},{2,4,1,0,1.2f}};
    std::vector<float> baseline;unsigned armed=0;
    auto smooth=[](double x){x=(std::max)(0.0,(std::min)(1.0,x));return x*x*(3-2*x);};
    for(unsigned run=0;run<sizeof(cases)/sizeof(cases[0]);++run) {
        const auto&c=cases[run];const auto values=render(c[0],c[1],c[2],c[3],c[4]);
        CHECK(values.size()==side*side*4);if(values.size()!=side*side*4)return;
        if(run==0){baseline=values;CHECK(values==render(c[0],c[1],c[2],c[3],c[4]));}
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
            const double px=(x+.5)*8+c[3],py=(y+.5)*8+c[3];
            const double sx=(std::max)(0.0,(std::min)(7.0,px/8-.5)),sy=(std::max)(0.0,(std::min)(7.0,py/8-.5));
            double expected=.1+.02*sx+.03*sy;
            expected=1-std::pow(1-expected,std::exp(-(std::max)(0.0,double(c[2])-1)/5));
            expected*=smooth((std::min)((std::min)(px,py),(std::min)(64-px,64-py))/24);
            if(c[0]<1.5||std::abs(c[1]-4)>.1||px<0||py<0||px>64||py>64)expected=0;
            const double fresh=1-std::exp(-double(c[4])*.35),mixed=(std::max)(fresh,expected);const size_t n=(y*side+x)*4;
            CHECK(std::isfinite(values[n])&&std::abs(values[n]-expected)<.00003);
            CHECK(std::abs(values[n+1]-mixed)<.00003);
            CHECK(std::abs(values[n+2]-.83)<.00001&&std::abs(values[n+3]-.48)<.00001);
            if(run==0&&x>=2&&x<=5&&y>=2&&y<=5){CHECK(std::abs(values[n]-.83)>.3);++armed;}
        }
    }
    // Replace A in the same bound resource: the second submission must see the new alpha,
    // while the earlier readback and all three vessel channels remain unchanged.
    for(size_t i=3;i<pixels.size();i+=4)pixels[i]=0;
    if(!upload())return;const auto cleared=render(2,4,1,0,0);CHECK(cleared.size()==baseline.size());
    if(cleared.size()==baseline.size())for(size_t i=0;i<cleared.size();i+=4) {
        CHECK(cleared[i]==0&&cleared[i+1]==0);CHECK(cleared[i+2]==baseline[i+2]&&cleared[i+3]==baseline[i+3]);
    }
    CHECK(armed==16);
    printf("sdl_gpu_frame_selfcheck: coast alpha 8x8, 8 cases, linear sampling, age hold, pause, level/window flags, source trough, no double count, upload replacement and %u armed RGB-channel controls checked\n",armed);
}
#endif // __linux__

#if defined(__linux__)
static void check_gpu_water_shore_depth_gate(SdlGpuFrame *frame)
{
    // Execute the actual generated coastal branch with a point-sampled depth texture and
    // its real raster coordinates. The independent oracle starts with uploaded depth bits,
    // not bottomPosition(), which deliberately has a different terrain-fallback contract.
    using namespace WaterShoreFoam;
    PixelPipelineDescription description={};std::string production;
    const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,COMBINER_SHADER_TARGET_SDL3_GPU);
    CHECK(generated);if(!generated)return;
    const size_t pixelAt=production.find("float2 waterScenePixel("),pixelEnd=production.find("// Depth is a point-sampled",pixelAt);
    const size_t blockAt=production.find("    float shoreRate=0.0,shoreSupport=0.0;"),blockEnd=production.find("    // At a known hull",blockAt);
    const size_t cageAt=production.find("    // Contact wash follows",blockAt),shoreAt=production.find("    if(WaterParameters.y<0.5 &&",cageAt);
    CHECK(pixelAt!=std::string::npos && pixelEnd!=std::string::npos && blockAt<cageAt && cageAt<shoreAt && shoreAt<blockEnd && blockEnd!=std::string::npos);
    if(pixelAt==std::string::npos || pixelEnd==std::string::npos || !(blockAt<cageAt && cageAt<shoreAt && shoreAt<blockEnd) || blockEnd==std::string::npos)return;
    // Keep the original source/history initialization and actual analytic depth-gated
    // branch. Cage wash has its own resources and fixture.
    const std::string block=production.substr(blockAt,cageAt-blockAt)+production.substr(shoreAt,blockEnd-shoreAt);
    CHECK(block.find("waterShoreSceneDepth(SceneParameters.w,shoreRawDepth)")!=std::string::npos);
    CHECK(block.find("floor(input.Position.xy)+0.5")!=std::string::npos);
    const std::string ps=R"SHORE(
cbuffer ShoreCase:register(b0,space3) {
    float4 Viewport,SceneParameters,WaterParameters,OpticalParameters,TerrainParameters;
    float4 CaseParameters,CaseGradient,CaseContext;
};
Texture2D<float> Texture6:register(t0,space2);SamplerState Sampler6:register(s0,space2);
struct Input {float4 Position;};
float terrainHeight(float2 p) {return 0.0;}
float waterShoreHistory(float2 p,float level,float time) {return CaseGradient.z;}
)SHORE"+Source()+production.substr(pixelAt,pixelEnd-pixelAt)+R"SHORE(
float4 main(float4 pixel:SV_Position):SV_Target0 {
    Input input;input.Position=pixel;
    float foam=.2,baseLevel=2.0,surfaceTerrainHeight=0.0;
    float2 terrainGradient=CaseGradient.xy,wavePosition=float2(0,0),parameterDx=0,parameterDy=0;
    float4 opticalContext=CaseContext;
    float time=CaseParameters.x,depth=CaseParameters.y,fftWeight=CaseParameters.w;
    float3 shoreSceneBottom=float3(0,0,CaseParameters.z);
)SHORE"+block+R"SHORE(
    float2 sampled=waterScenePixel(floor(input.Position.xy)+.5);
    return float4(shoreRate,shoreSupport,foam,sampled.x+8*sampled.y);
}
)SHORE";
    const char *vs="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vs=nullptr,*ps=nullptr;SDL_GPUGraphicsPipeline *pipeline=nullptr;
        SDL_GPUTexture *target=nullptr,*depth=nullptr;SDL_GPUSampler *point=nullptr;
        ~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(depth)SDL_ReleaseGPUTexture(device,depth);
            if(point)SDL_ReleaseGPUSampler(device,point);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(vs)SDL_ReleaseGPUShader(device,vs);if(ps)SDL_ReleaseGPUShader(device,ps);}
    } resources;resources.device=frame->Device();std::string error;std::vector<unsigned char> spirv;
    bool ok=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(ok);if(!ok)return;
    resources.vs=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs);if(!resources.vs)return;
    ok=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(ok);if(!ok){printf("shore depth PS: %s\n",error.c_str());return;}
    unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,false,samplers,buffers);CHECK(samplers==1 && buffers==1);
    resources.ps=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps);if(!resources.ps)return;
    SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vs;pi.fragment_shader=resources.ps;
    pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
    const unsigned width=8,height=4;SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;
    ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;ti.width=width;ti.height=height;ti.layer_count_or_depth=ti.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
    ti.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    resources.depth=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.depth);if(!resources.depth)return;
    SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;si.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    resources.point=SDL_CreateGPUSampler(resources.device,&si);CHECK(resources.point);if(!resources.point)return;
    float texels[width*height];std::fill(texels,texels+width*height,1.0f);
    texels[1*width+2]=.4f;texels[1*width+3]=1;texels[1*width+4]=-.001f;texels[1*width+5]=std::nextafter(.999999f,0.0f);
    texels[2*width+2]=0;texels[2*width+3]=.999999f;texels[2*width+4]=NAN;texels[2*width+5]=INFINITY;
    CHECK(frame->Flush());SDL_GPUTransferBufferCreateInfo bi={};bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;bi.size=sizeof(texels);
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(resources.device,&bi);CHECK(transfer);if(!transfer)return;
    void *mapped=SDL_MapGPUTransferBuffer(resources.device,transfer,false);CHECK(mapped);
    if(!mapped){SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return;}memcpy(mapped,texels,sizeof(texels));SDL_UnmapGPUTransferBuffer(resources.device,transfer);
    SDL_GPUCommandBuffer *upload=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(upload);
    if(!upload){SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return;}
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(upload);CHECK(copy);
    if(!copy){SDL_CancelGPUCommandBuffer(upload);SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return;}
    SDL_GPUTextureTransferInfo from={};from.transfer_buffer=transfer;from.pixels_per_row=width;from.rows_per_layer=height;
    SDL_GPUTextureRegion to={};to.texture=resources.depth;to.w=width;to.h=height;to.d=1;
    SDL_UploadToGPUTexture(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);CHECK(SDL_SubmitGPUCommandBuffer(upload));SDL_ReleaseGPUTransferBuffer(resources.device,transfer);
    float activeTime=0,troughTime=0,best=0,gx=0,gy=0;bool foundTrough=false;
    for(unsigned i=0;i<2048;++i) {
        const float time=float(i)*.05f;const Wave w=Evaluate(0,0,time);const float length=std::hypot(w.dx,w.dy);
        if(w.height<-.25f && !foundTrough){troughTime=time;foundTrough=true;}
        if(length<=1e-6f || std::abs(w.dt)<=1e-6f)continue;
        Context c;c.depth=2;c.shore=1;c.slope=.1f;c.exposure=c.support=1;
        c.nx=(w.dt>0?1.f:-1.f)*w.dx/length;c.ny=(w.dt>0?1.f:-1.f)*w.dy/length;
        const float rate=Rate(c,w)*EvaluateFront(0,0,time,c.nx,c.ny).mask;if(rate>best){best=rate;activeTime=time;gx=-.1f*c.nx;gy=-.1f*c.ny;}
    }
    CHECK(best>.1f && foundTrough);if(best<=.1f || !foundTrough)return;
    struct Case {float available,time,wet=2,bottom=0,river=0,terrain=1,context=1,offshore=0,support=1,fft=0,history=0;};
    std::vector<Case> cases;
    for(float available:{0.f,.5f,std::nextafter(.5f,1.f),1.f,NAN,INFINITY}) {
        cases.push_back({available,activeTime});cases.push_back({available,troughTime});
    }
    Case q={1,activeTime};q.wet=0;cases.push_back(q);q={1,activeTime};q.bottom=2;cases.push_back(q);
    q={1,activeTime};q.river=1;cases.push_back(q);q={1,activeTime};q.terrain=0;cases.push_back(q);
    q={1,activeTime};q.context=0;cases.push_back(q);q={1,activeTime};q.offshore=.01f;cases.push_back(q);
    q={1,activeTime};q.support=0;cases.push_back(q);
    q={1,activeTime};q.history=.8f;cases.push_back(q);q.fft=1;cases.push_back(q);
    auto smooth=[](double a,double b,double x){const double t=std::max(0.0,std::min(1.0,(x-a)/(b-a)));return t*t*(3-2*t);};
    unsigned accepted=0,invalidArmed=0,unavailableArmed=0,dry=0,object=0,spectralSuppressed=0,historyArmed=0;double maximumError=0;
    for(const Case &c:cases) {
        float uniforms[8][4]={{2.5f,1.5f,4,2},{0,1.f/width,1.f/height,c.available},{0,c.river,0,0},
            {0,0,c.context,0},{0,0,0,c.terrain},{c.time,c.wet,c.bottom,c.fft},{gx,gy,c.history,0},{c.offshore,.01f,1,c.support}};
        auto render=[&]() {
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return std::vector<float>();
            SDL_GPUColorTargetInfo target={};target.texture=resources.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);CHECK(pass);
            if(!pass){SDL_CancelGPUCommandBuffer(commands);return std::vector<float>();}
            SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);SDL_GPUTextureSamplerBinding binding={resources.depth,resources.point};
            SDL_BindGPUFragmentSamplers(pass,0,&binding,1);SDL_PushGPUFragmentUniformData(commands,0,uniforms,sizeof(uniforms));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
            return read_float_targets(frame,&resources.target,1,width,height);
        };
        const auto values=render(),paused=render();CHECK(values==paused && values.size()==width*height*4);
        if(values.size()!=width*height*4)return;
        Context context;context.depth=2;context.shore=1;context.nx=-gx;context.ny=-gy;
        context.slope=std::hypot(gx,gy);context.exposure=1;context.support=c.support*(1-c.fft);
        const Front front=EvaluateFront(0,0,c.time,context.nx,context.ny);
        const float rate=Rate(context,Evaluate(0,0,c.time))*front.mask;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const unsigned sx=std::max(2u,std::min(5u,x)),sy=std::max(1u,std::min(2u,y));const float raw=texels[sy*width+sx];
            const bool sourceContext=c.river<.5f && c.terrain>.5f && c.context>.5f && c.offshore==0 && c.support>0;
            const bool real=std::isfinite(c.available)&&c.available>.5f&&std::isfinite(raw)&&raw>=0 && raw<.999999f;
            const bool valid=sourceContext&&real;const double match=1-smooth(.5,1.5,std::abs(c.bottom));
            const double fresh=-std::expm1(-double(rate)*front.residence),history=c.history*(1-c.fft);
            const double foam=valid?.2f*(1-context.support*match)+std::max(fresh,history)*smooth(0,.8,c.wet)*match:.2f;
            const double expected[4]={valid?rate:0,valid?context.support:0,foam,sx+.5+8*(sy+.5)};
            for(unsigned channel=0;channel<4;++channel){const float actual=values[(y*width+x)*4+channel];const double error=std::abs(actual-expected[channel]);maximumError=std::max(maximumError,error);CHECK(std::isfinite(actual)&&error<.00015);}
            if(valid){++accepted;if(c.wet==0){CHECK(values[(y*width+x)*4+2]==0);++dry;}if(c.bottom==2){CHECK(values[(y*width+x)*4+2]==.2f);++object;}}
            if(valid&&c.fft==1){CHECK(values[(y*width+x)*4]==0&&values[(y*width+x)*4+1]==0&&values[(y*width+x)*4+2]==.2f);++spectralSuppressed;}
            if(valid&&c.history>0&&c.fft==0){CHECK(values[(y*width+x)*4+2]>.7f);++historyArmed;}
            if(!valid&&sourceContext) {
                CHECK(values[(y*width+x)*4+2]==.2f);
                // Armed old integration assumes fallback terrain is a hit; at a trough it
                // erases the rim. Here the actual generated branch must retain it instead.
                if(c.time==troughTime){if(c.available==1 && !real)++invalidArmed;if(c.available<=.5f)++unavailableArmed;}
            }
        }
    }
    CHECK(accepted>0 && invalidArmed>0 && unavailableArmed>0 && dry>0 && object>0 && spectralSuppressed>0 && historyArmed>0);
    printf("sdl_gpu_frame_selfcheck: coastal original-depth raster %zu cases, generated branch max %.8f; real/clear/negative/nonfinite, unavailable, offset viewport, trough, dry/object, FFT suppression of analytic source/history and exact pause checked\n",cases.size(),maximumError);
}
#endif // __linux__

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_gpu_frame_selfcheck: SKIP - SDL_Init: %s\n", SDL_GetError());
		return 77;
	}
	std::string error;
	SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, WIDTH, HEIGHT, error);
	if (frame == NULL) {
		printf("sdl_gpu_frame_selfcheck: SKIP - no GPU device here: %s\n", error.c_str());
		SDL_Quit();
		return 77;
	}
	printf("sdl_gpu_frame_selfcheck: %s\n", SDL_GetGPUDeviceDriver(frame->Device()));
	check_colour_only_target(frame);
	check_scene_depth_snapshot(frame);
	check_gpu_fft_ocean(frame);
	check_gpu_fft_quality_switch(frame);
	check_gpu_water_opaque_alpha(frame);
	check_gpu_water_vertex_uniform_order(frame);
	check_gpu_modern_water_material(frame);
	check_gpu_water_visual_uniform_order(frame);
	check_gpu_water_visual_query(frame);
#if defined(__linux__)
	check_gpu_water_visual_zero_effect_skip(frame);
#endif
	check_gpu_foam_history_upload(frame);
	check_gpu_foam_history_tiles(frame);
#if defined(__linux__)
	check_gpu_foam_history_sampling(frame);
	check_gpu_foam_microcoverage(frame);
	check_gpu_foam_irradiance(frame);
	check_gpu_foam_environment(frame);
	check_gpu_water_shore_source(frame);
#endif
#if defined(__linux__)
	check_gpu_water_shore_front(frame);
	check_gpu_water_shore_depth_gate(frame);
	check_gpu_water_shore_history(frame);
#endif
	check_gpu_authored_shore_overlay(frame);
#if defined(__linux__)
	check_gpu_foam_material(frame);
	check_gpu_coastal_material(frame);
#endif
	check_gpu_vessel_hull_damping(frame);
	check_gpu_water_wave_cage(frame);
#if defined(__linux__)
	check_gpu_water_hull_normal(frame);
#endif
	check_gpu_water_bow_shoulder(frame);
	check_gpu_water_bow_refinement(frame);
	check_gpu_fft_vessel_composition(frame);
	check_gpu_wave_model(frame);
	check_gpu_ocean_wave_model(frame);
#if defined(__linux__)
	check_gpu_water_mesh_correction(frame);
#endif
	check_gpu_water_optical_model(frame);
	check_gpu_water_optical_ray_shift(frame);
#if defined(__linux__)
	check_gpu_water_refraction_coordinates(frame);
	check_gpu_water_refraction_footprint(frame);
#endif
	check_gpu_water_material_transport(frame);
	check_gpu_water_hull_distance(frame);
#if defined(__linux__)
	check_gpu_water_hull_effects(frame);
	check_sun_depth_target(frame);
#endif

	// A colour clear: D3DCOLOR 0x80402010 is A 0x80, R 0x40, G 0x20, B 0x10.
	frame->Clear_Back_Buffer(true, true, true, 0x80402010u, 1.0f, 0);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	unsigned int wrong = 0;
	for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
		if (pixels[i] != 0x10 || pixels[i + 1] != 0x20 || pixels[i + 2] != 0x40 || pixels[i + 3] != 0x80) {
			++wrong;
		}
	}
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && wrong == 0);

	// Depth and stencil only: the colour is loaded, so it stays.
	frame->Clear_Back_Buffer(false, true, true, 0xFFFFFFFFu, 0.5f, 7);
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() >= 4 && pixels[0] == 0x10 && pixels[2] == 0x40);
	check_scene_depth(frame);

	// The gamma pass, into a target of the test's.
	std::vector<uint8_t> picture(WIDTH * HEIGHT * 4);
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			pattern(x, y, &picture[(y * WIDTH + x) * 4]);
		}
	}
	CHECK(frame->Upload_Back_Buffer(picture));

	// Refraction gets a stable pre-water snapshot, including ordered pending clears/blits.
	SDL_GPUTexture *background = frame->Water_Background(frame->Back_Buffer_Target());
	CHECK(background != NULL && background != frame->Back_Buffer());
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);
	std::vector<uint8_t> replacement(WIDTH * HEIGHT * 4, 0xFF);
	CHECK(frame->Upload_Back_Buffer(replacement));
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture); // Later water draws must not refract previously drawn water.
	frame->Clear_Back_Buffer(true, false, false, 0xFF123456u, 1.0f, 0);
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x56 && pixels[1] == 0x34
		&& pixels[2] == 0x12 && pixels[3] == 0xFF);
	CHECK(frame->Upload_Back_Buffer(picture));

	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = WIDTH;
	info.height = HEIGHT;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(target != NULL);

	uint16_t identity[3][256];
	uint16_t ramp[3][256];
	for (int i = 0; i < 256; ++i) {
		identity[0][i] = identity[1][i] = identity[2][i] = (uint16_t)(i * 257);
		ramp[0][i] = (uint16_t)((255 - i) * 257);	// red inverted
		ramp[1][i] = (uint16_t)(i * 257);			// green as it is
		ramp[2][i] = (uint16_t)((i / 2) * 257);		// blue halved
	}
	CHECK(Sdl_Gamma_Is_Identity(identity) && !Sdl_Gamma_Is_Identity(ramp));

	CHECK(frame->Present_To(target, WIDTH, HEIGHT, ramp));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	const unsigned int gamma_wrong = mismatches(pixels, ramp);
	CHECK(gamma_wrong == 0);
	// The armed control: the same output against the identity must differ nearly everywhere.
	const unsigned int control = mismatches(pixels, identity);
	CHECK(control > WIDTH * HEIGHT / 2);

	// The identity ramp is a blit: the picture back as it went in.
	CHECK(frame->Present_To(target, WIDTH, HEIGHT, identity));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// Present begins a new frame even when it did not clear the colour buffer.
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// The game also draws water into an intermediate colour target, rather than the back buffer.
	SdlTarget intermediate = { target, frame->Depth_For(WIDTH, HEIGHT), WIDTH, HEIGHT };
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(true, false, false, 0xFF102030u, 1.0f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(intermediate), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x30 && pixels[1] == 0x20
		&& pixels[2] == 0x10);
	frame->Clear_Back_Buffer(true, false, false, 0xFF445566u, 1.0f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(intermediate), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x66 && pixels[1] == 0x55
		&& pixels[2] == 0x44);
	frame->Set_Target(frame->Back_Buffer_Target());
	CHECK(frame->Read_Back(frame->Water_Background(frame->Back_Buffer_Target()), WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// A resize must replace the snapshot and match the new back-buffer dimensions.
	const SdlTarget before_shore_resize=frame->Back_Buffer_Target();
	uint64_t before_shore_serial=0,after_shore_serial=123;
	frame->Ready_Scene_Depth(before_shore_resize,before_shore_serial);
	CHECK(frame->Resize(16, 8));
	CHECK(frame->Ready_Scene_Depth(before_shore_resize,after_shore_serial)==NULL && after_shore_serial==0);
	after_shore_serial=123;
	CHECK(frame->Ready_Scene_Depth(frame->Back_Buffer_Target(),after_shore_serial)==NULL && after_shore_serial==0);
	frame->Clear_Back_Buffer(true, true, true, 0xFFAABBCCu, 0.42f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(frame->Back_Buffer_Target()), 16, 8, pixels));
	CHECK(pixels.size() == 16 * 8 * 4 && pixels[0] == 0xCC && pixels[1] == 0xBB
		&& pixels[2] == 0xAA && pixels[3] == 0xFF);
	if (frame->Depth_Usage() & SDL_GPU_TEXTUREUSAGE_SAMPLER) {
		CHECK(frame->Read_Back(frame->Scene_Depth(frame->Back_Buffer_Target()), 16, 8, pixels));
		CHECK(frame->Ready_Scene_Depth(frame->Back_Buffer_Target(),after_shore_serial)!=NULL
			&& after_shore_serial!=0 && after_shore_serial!=before_shore_serial);
		CHECK(pixels.size() == 16 * 8 * 4 && fabs(depth_at(pixels, 16, 15, 7) - 0.42f) < 0.00001f);
		DepthObject resized_object(frame);
		resized_object.Draw(frame->Back_Buffer_Target(), 0.21f);
		CHECK(frame->Present(NULL));
		CHECK(frame->Read_Back(frame->Scene_Depth(frame->Back_Buffer_Target()), 16, 8, pixels));
		CHECK(fabs(depth_at(pixels, 16, 8, 4) - 0.21f) < 0.00001f);
	}

	SDL_ReleaseGPUTexture(frame->Device(), target);
	delete frame;
	SDL_Quit();
	if (failures != 0) {
		printf("sdl_gpu_frame_selfcheck: %d FAILED (gamma: %u pixels wrong; control differed in %u)\n",
			failures, gamma_wrong, control);
		return 1;
	}
	printf("sdl_gpu_frame_selfcheck: clear, depth-only clear, gamma (control differs in %u of %u pixels) "
		"scene colour/depth snapshots, submerged object depth, per-target refresh, resource lifetime, resize and identity blit all hold\n", control, WIDTH * HEIGHT);
	return 0;
}
