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

// Pipelines and samplers (PosixDevice/Render/SdlPipelineCache, A3c):
//   - the vertex layout: attributes at D3's locations and offsets, and every stride equal to D3DX's own
//     vertex size (d3dx9posix.cpp's Get_FVF_Vertex_Size, an independent computation) for all thirteen of
//     dx8fvf.h's formats; a blend-weighted position refused;
//   - the pipeline key from a headless device's own states: D3D9's defaults as SDL3 state, a disabled
//     blend as one key whatever its factors, D3D9's BOTHSRCALPHA shorthand, separate alpha, two-sided
//     stencil onto the counter-clockwise face, a fan drawn as a list, point fill refused;
//   - the sampler description: D3D9's default, anisotropic, a mip filter of NONE held at MAXMIPLEVEL,
//     border addressing refused;
//   - on this machine's GPU (skipped without one): pipelines and samplers the device accepts, and a
//     repeat is the same object.
//
// WHAT THIS DOES NOT PROVE: that the pipeline draws what D3D9 would (winding, blending, depth): the
// reference's comparison (A3b) does that, a draw at a time.

#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlProgramCache.h"
#include "d3dx9runtime.h"
#include "dx8fvf.h"
#include "ffshader.h"
#include "ffvertex.h"

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
	parameters.BackBufferWidth = 32;
	parameters.BackBufferHeight = 32;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice9 *device = NULL;
	if (d3d == NULL || Render_Failed(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &parameters, &device))) return NULL;
	return static_cast<PosixDevice9 *>(device);
}

// The device's render states as a row, read back through the D3D9 call.
static void read_states(PosixDevice9 *device, RenderUInt32 states[256])
{
	for (int i = 0; i < 256; ++i) {
		states[i] = 0;
		device->GetRenderState((D3DRENDERSTATETYPE)i, &states[i]);
	}
}

static void check_layout()
{
	SdlVertexLayout layout;
	std::string refusal;
	CHECK(Sdl_Vertex_Layout(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1, layout, refusal));
	CHECK(layout.AttributeCount == 4);
	CHECK(layout.Location[0] == 0 && layout.Offset[0] == 0 && layout.Format[0] == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
	CHECK(layout.Location[1] == 1 && layout.Offset[1] == 12);
	CHECK(layout.Location[2] == 2 && layout.Offset[2] == 24 && layout.Format[2] == SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM);
	CHECK(layout.Location[3] == 4 && layout.Offset[3] == 28 && layout.Format[3] == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);
	CHECK(layout.Stride == 36);

	// Pre-transformed, with a specular colour at COLOR1's location and a one-float second set.
	CHECK(Sdl_Vertex_Layout(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE1(1), layout, refusal));
	CHECK(layout.AttributeCount == 5 && layout.Offset[1] == 16 && layout.Location[2] == 3 && layout.Offset[2] == 20
		&& layout.Format[2] == SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM && layout.Location[3] == 4 && layout.Offset[3] == 24
		&& layout.Location[4] == 5 && layout.Offset[4] == 32 && layout.Format[4] == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT);
	CHECK(layout.Stride == 36);

	CHECK(!Sdl_Vertex_Layout(D3DFVF_XYZB2 | D3DFVF_NORMAL, layout, refusal) && !refusal.empty());

	// Every engine format's stride against D3DX's own vertex size.
	static const RenderUInt32 FORMATS[] = { DX8_FVF_XYZ, DX8_FVF_XYZN, DX8_FVF_XYZNUV1, DX8_FVF_XYZNUV2, DX8_FVF_XYZNDUV1,
		DX8_FVF_XYZNDUV2, DX8_FVF_XYZDUV1, DX8_FVF_XYZDUV2, DX8_FVF_XYZUV1, DX8_FVF_XYZUV2, DX8_FVF_XYZNDUV1TG3,
		DX8_FVF_XYZNUV2DMAP, DX8_FVF_XYZNDCUBEMAP };
	for (size_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); ++i) {
		CHECK(Sdl_Vertex_Layout(FORMATS[i], layout, refusal));
		CHECK(layout.Stride == Get_FVF_Vertex_Size(FORMATS[i]));
	}
}

static void check_keys(PosixDevice9 *device)
{
	RenderUInt32 states[256];
	SdlPipelineKey key, other;
	std::string refusal;
	const uint32_t colour = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	const uint32_t depth = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;

	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.Cull == SDL_GPU_CULLMODE_BACK && key.Fill == SDL_GPU_FILLMODE_FILL);
	CHECK(key.DepthTest && key.DepthWrite && key.DepthCompare == SDL_GPU_COMPAREOP_LESS_OR_EQUAL);
	CHECK(!key.BlendEnable && key.WriteMask == 0xF && !key.StencilEnable);
	CHECK(key.Primitive == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);

	// A disabled blend is one key, whatever the factors say.
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, other, refusal));
	CHECK(SdlPipelineKeyEqual()(key, other) && SdlPipelineKeyHash()(key) == SdlPipelineKeyHash()(other));

	// BOTHSRCALPHA: source alpha and one minus it, for colour and alpha alike.
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BOTHSRCALPHA);
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.BlendEnable && key.SourceColour == SDL_GPU_BLENDFACTOR_SRC_ALPHA
		&& key.DestinationColour == SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
		&& key.SourceAlpha == key.SourceColour && key.AlphaOperation == SDL_GPU_BLENDOP_ADD);
	CHECK(!SdlPipelineKeyEqual()(key, other));

	// Separate alpha.
	device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 1);
	device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
	device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
	device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_MAX);
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.SourceAlpha == SDL_GPU_BLENDFACTOR_ZERO && key.DestinationAlpha == SDL_GPU_BLENDFACTOR_ONE
		&& key.AlphaOperation == SDL_GPU_BLENDOP_MAX && key.SourceColour == SDL_GPU_BLENDFACTOR_SRC_ALPHA);
	device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, 0);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);

	// Two-sided stencil: the counter-clockwise face takes the CCW_ states.
	device->SetRenderState(D3DRS_STENCILENABLE, 1);
	device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, 1);
	device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
	device->SetRenderState(D3DRS_CCW_STENCILPASS, D3DSTENCILOP_DECR);
	device->SetRenderState(D3DRS_STENCILMASK, 0x0F);
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.StencilEnable && key.FrontPass == SDL_GPU_STENCILOP_INCREMENT_AND_WRAP
		&& key.BackPass == SDL_GPU_STENCILOP_DECREMENT_AND_WRAP && key.StencilRead == 0x0F);
	// And with no depth-stencil at all, neither depth nor stencil.
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, 0, key, refusal));
	CHECK(!key.StencilEnable && !key.DepthTest && !key.DepthWrite);
	device->SetRenderState(D3DRS_STENCILENABLE, 0);

	// A fan is drawn as a list (the draw expands it); point fill is refused.
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLEFAN, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.Primitive == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);
	device->SetRenderState(D3DRS_FILLMODE, D3DFILL_POINT);
	read_states(device, states);
	refusal.clear();
	CHECK(!Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal) && !refusal.empty());
	device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
	read_states(device, states);
	CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, NULL, NULL, D3DFVF_XYZ, colour, depth, key, refusal));
	CHECK(key.Cull == SDL_GPU_CULLMODE_FRONT);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
}

static void check_samplers()
{
	RenderUInt32 states[14];
	memset(states, 0, sizeof(states));
	states[D3DSAMP_ADDRESSU] = states[D3DSAMP_ADDRESSV] = states[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
	states[D3DSAMP_MAGFILTER] = states[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
	states[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
	states[D3DSAMP_MAXANISOTROPY] = 1;
	SDL_GPUSamplerCreateInfo info;
	std::string refusal;
	CHECK(Sdl_Sampler_Description(states, &info, refusal));
	CHECK(info.min_filter == SDL_GPU_FILTER_NEAREST && info.address_mode_u == SDL_GPU_SAMPLERADDRESSMODE_REPEAT);
	CHECK(info.min_lod == 0.0f && info.max_lod == 0.25f && !info.enable_anisotropy);

	states[D3DSAMP_MAXMIPLEVEL] = 2;
	CHECK(Sdl_Sampler_Description(states, &info, refusal));
	CHECK(info.min_lod == 2.0f && info.max_lod == 2.25f);	// no mip filter: that level, and minification kept

	states[D3DSAMP_MINFILTER] = D3DTEXF_ANISOTROPIC;
	states[D3DSAMP_MIPFILTER] = D3DTEXF_LINEAR;
	states[D3DSAMP_MAXANISOTROPY] = 8;
	states[D3DSAMP_ADDRESSV] = D3DTADDRESS_CLAMP;
	CHECK(Sdl_Sampler_Description(states, &info, refusal));
	CHECK(info.enable_anisotropy && info.max_anisotropy == 8.0f && info.min_filter == SDL_GPU_FILTER_LINEAR
		&& info.mipmap_mode == SDL_GPU_SAMPLERMIPMAPMODE_LINEAR && info.address_mode_v == SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
		&& info.max_lod > 2.0f);
	// MAXANISOTROPY as the game sets it (16), past the caps' 16, and 0: held to 1..16.
	states[D3DSAMP_MAXANISOTROPY] = 16;
	CHECK(Sdl_Sampler_Description(states, &info, refusal) && info.enable_anisotropy && info.max_anisotropy == 16.0f);
	states[D3DSAMP_MAXANISOTROPY] = 64;
	CHECK(Sdl_Sampler_Description(states, &info, refusal) && info.max_anisotropy == 16.0f);
	states[D3DSAMP_MAXANISOTROPY] = 0;
	CHECK(Sdl_Sampler_Description(states, &info, refusal) && info.max_anisotropy == 1.0f);
	// A magnification filter of ANISOTROPIC alone turns it on too; LINEAR with MAXANISOTROPY 16 does not.
	states[D3DSAMP_MAXANISOTROPY] = 16;
	states[D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
	states[D3DSAMP_MAGFILTER] = D3DTEXF_ANISOTROPIC;
	CHECK(Sdl_Sampler_Description(states, &info, refusal) && info.enable_anisotropy && info.mag_filter == SDL_GPU_FILTER_LINEAR);
	states[D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
	CHECK(Sdl_Sampler_Description(states, &info, refusal) && !info.enable_anisotropy && info.max_anisotropy == 1.0f);
	states[D3DSAMP_MINFILTER] = D3DTEXF_ANISOTROPIC;

	states[D3DSAMP_ADDRESSU] = D3DTADDRESS_BORDER;
	refusal.clear();
	CHECK(!Sdl_Sampler_Description(states, &info, refusal) && !refusal.empty());
}

int main()
{
	check_layout();
	check_samplers();
	IDirect3D9 *d3d = NULL;
	PosixDevice9 *device = make_device(d3d);
	CHECK(device != NULL);
	if (device == NULL) return 1;
	check_keys(device);

	int result = 0;
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_pipeline_state_selfcheck: the GPU part SKIPPED - SDL_Init: %s\n", SDL_GetError());
		result = 77;
	}
	std::string error;
	SdlGpuFrame *frame = result == 0 ? SdlGpuFrame::Create(NULL, 16, 16, error) : NULL;
	if (result == 0 && frame == NULL) {
		printf("sdl_pipeline_state_selfcheck: the GPU part SKIPPED - no GPU device: %s\n", error.c_str());
		result = 77;
	}
	if (frame != NULL) {
		SdlProgramCache *programs = new SdlProgramCache(frame->Device());
		SdlPipelineCache *pipelines = new SdlPipelineCache(frame->Device());
		SdlSamplerCache *samplers = new SdlSamplerCache(frame->Device());
		device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		CombinerDescription combiner;
		VertexPipelineDescription vertex;
		device->Build_Combiner_Description(combiner);
		CHECK(device->Build_Vertex_Description(vertex));
		SDL_GPUShader *vs = programs->Vertex_Program(vertex).Shader;
		SDL_GPUShader *ps = programs->Pixel_Program(combiner).Shader;
		CHECK(vs != NULL && ps != NULL);
		RenderUInt32 states[256];
		read_states(device, states);
		SdlPipelineKey key;
		std::string refusal;
		CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLELIST, vs, ps, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1,
			SdlGpuFrame::Target_Format(), SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, key, refusal));
		SDL_GPUGraphicsPipeline *first = pipelines->Pipeline(key);
		CHECK(first != NULL);
		CHECK(pipelines->Pipeline(key) == first && pipelines->Pipelines_Built() == 1);
		// A blended, stencilled, strip pipeline too.
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		device->SetRenderState(D3DRS_STENCILENABLE, 1);
		read_states(device, states);
		CHECK(Sdl_Pipeline_Key(states, D3DPT_TRIANGLESTRIP, vs, ps, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1,
			SdlGpuFrame::Target_Format(), SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, key, refusal));
		SDL_GPUGraphicsPipeline *second = pipelines->Pipeline(key);
		CHECK(second != NULL && second != first && pipelines->Pipelines_Built() == 2);

		RenderUInt32 sampler_states[14];
		memset(sampler_states, 0, sizeof(sampler_states));
		sampler_states[D3DSAMP_ADDRESSU] = sampler_states[D3DSAMP_ADDRESSV] = sampler_states[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
		sampler_states[D3DSAMP_MAGFILTER] = sampler_states[D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
		sampler_states[D3DSAMP_MIPFILTER] = D3DTEXF_LINEAR;
		SDL_GPUSampler *sampler = samplers->Sampler(sampler_states);
		CHECK(sampler != NULL && samplers->Sampler(sampler_states) == sampler);
		sampler_states[D3DSAMP_ADDRESSU] = D3DTADDRESS_BORDER;
		CHECK(samplers->Sampler(sampler_states) == NULL && !samplers->Refusal().empty());
		// Alternating stage settings must reuse the exact sampler (or refusal) without
		// hashing every call. Eviction only drops this shortcut, never the owned entry.
		RenderUInt32 recent_states[4][14];
		SDL_GPUSampler *recent_samplers[4];
		for (unsigned i = 0; i < 4; ++i) {
			memcpy(recent_states[i], sampler_states, sizeof(sampler_states));
			recent_states[i][D3DSAMP_ADDRESSU] = i == 3 ? D3DTADDRESS_BORDER : D3DTADDRESS_WRAP;
			recent_states[i][D3DSAMP_MAXMIPLEVEL] = i;
			recent_samplers[i] = samplers->Sampler(recent_states[i]);
			CHECK((recent_samplers[i] != NULL) == (i != 3));
		}
		// Settle the ring after the unrelated keys used above (hits do not change its
		// insertion order), then a four-key working set no longer needs the table.
		for (unsigned i = 0; i < 4; ++i)
			CHECK(samplers->Sampler(recent_states[i]) == recent_samplers[i]);
		const size_t populated = samplers->Table_Lookups();
		for (unsigned round = 0; round < 16; ++round)
			for (unsigned i = 0; i < 4; ++i)
				CHECK(samplers->Sampler(recent_states[i]) == recent_samplers[i]);
		CHECK(samplers->Table_Lookups() == populated);
		for (unsigned i = 4; i < 12; ++i) {
			recent_states[0][D3DSAMP_MAXMIPLEVEL] = i;
			CHECK(samplers->Sampler(recent_states[0]) != NULL);
		}
		recent_states[0][D3DSAMP_MAXMIPLEVEL] = 0;
		const size_t evicted = samplers->Table_Lookups();
		CHECK(samplers->Sampler(recent_states[0]) == recent_samplers[0]);
		CHECK(samplers->Table_Lookups() == evicted + 1);
		// Include high bits (float-valued sampler state) in the exact cache identity.
		recent_states[0][D3DSAMP_MIPMAPLODBIAS] = 0x3f000000u;
		CHECK(samplers->Sampler(recent_states[0]) != NULL);
		CHECK(samplers->Table_Lookups() == evicted + 2);
		recent_states[0][D3DSAMP_MIPMAPLODBIAS] = 0;
		CHECK(samplers->Sampler(recent_states[0]) == recent_samplers[0]);
		CHECK(samplers->Table_Lookups() == evicted + 2);

		printf("sdl_pipeline_state_selfcheck: %s: %u pipelines built\n", SDL_GetGPUDeviceDriver(frame->Device()),
			pipelines->Pipelines_Built());
		delete samplers;
		delete pipelines;
		delete programs;
		delete frame;
		SDL_Quit();
	}
	device->Release();
	d3d->Release();
	if (failures != 0) {
		printf("sdl_pipeline_state_selfcheck: %d FAILED\n", failures);
		return 1;
	}
	printf("sdl_pipeline_state_selfcheck: layout, keys and samplers hold\n");
	return result;
}
