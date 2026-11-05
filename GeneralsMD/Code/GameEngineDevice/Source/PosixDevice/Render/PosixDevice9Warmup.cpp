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
// Bounded first-use work for common fixed-function draws. Descriptions and render states are
// local values: no binding, upload, draw, Present, camera movement or random selection occurs.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlProgramCache.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "engineshader.h"

#include <SDL3/SDL.h>
#include <cstring>

namespace {
VertexPipelineDescription vertexDescription(unsigned fvf, unsigned stages, bool lit, unsigned lights = 0)
{
	VertexPipelineDescription v;
	std::memset(&v, 0, sizeof(v));
	v.FVF = fvf;
	v.LightingEnabled = lit;
	v.ColourVertexEnabled = true;
	v.LocalViewer = true;
	const bool litVertexColour = lit && fvf == (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	v.DiffuseMaterialSource = (fvf & D3DFVF_DIFFUSE) && (!lit || litVertexColour) ? D3DMCS_COLOR1 : D3DMCS_MATERIAL;
	// The common lit single-UV coloured mesh uses vertex ambient too; multi-UV meshes use material.
	v.AmbientMaterialSource = litVertexColour ? D3DMCS_COLOR1 : D3DMCS_MATERIAL;
	v.LightCount = lit ? lights : 0;
	for (unsigned i = 0; i < v.LightCount; ++i)
		v.Lights[i].Type = i == 0 ? D3DLIGHT_DIRECTIONAL : D3DLIGHT_POINT;
	v.StageCount = stages;
	for (unsigned i = 0; i < stages; ++i) v.Stages[i].TextureCoordinateIndex = i;
	return v;
}

CombinerDescription pixelDescription(unsigned mode, bool alpha, bool receives)
{
	CombinerDescription p;
	std::memset(&p, 0, sizeof(p));
	p.StageCount = mode == 1 || mode == 2 ? 2 : 1;
	p.PixelPipeline.AlphaTestEnabled = alpha;
	p.PixelPipeline.AlphaFunction = D3DCMP_GREATEREQUAL;
	p.ShadowReceiving = receives;
	for (unsigned i = 0; i < p.StageCount; ++i) {
		CombinerStage &s = p.Stages[i];
		s.ColourOperation = s.AlphaOperation = D3DTOP_MODULATE;
		s.ColourArgument0 = s.AlphaArgument0 = D3DTA_CURRENT;
		s.ColourArgument1 = s.AlphaArgument1 = D3DTA_TEXTURE;
		s.ColourArgument2 = s.AlphaArgument2 = i ? D3DTA_CURRENT : D3DTA_DIFFUSE;
		s.TextureCoordinateIndex = i;
		s.TextureBound = true;
	}
	if (mode == 1) p.Stages[1].AlphaOperation = D3DTOP_SELECTARG2; // shroud preserves object alpha
	if (mode == 2) p.Stages[1].TextureCoordinateIndex = 0;
	if (mode == 3) {
		CombinerStage &s = p.Stages[0];
		s.ColourOperation = s.AlphaOperation = D3DTOP_SELECTARG1;
		s.ColourArgument1 = s.AlphaArgument1 = D3DTA_DIFFUSE;
		s.ColourArgument2 = s.AlphaArgument2 = D3DTA_CURRENT;
		s.TextureBound = false;
	}
	return p;
}
}

RenderWarmupStats PosixDevice9::Warm_Up_Render_Pipelines(bool modernWater, unsigned pipelineLimit, unsigned budgetMS)
{
	RenderWarmupStats result;
	if (!Gpu || !Programs || !Pipelines || !pipelineLimit || !budgetMS) return result;
	// Even direct callers cannot turn this optional loading work into an unbounded permutation walk.
	if (pipelineLimit > 192) pipelineLimit = 192;
	if (budgetMS > 3000) budgetMS = 3000;
	const Uint64 start = SDL_GetTicksNS();
	const unsigned programsBefore = Programs->Programs_Built(), pipelinesBefore = Pipelines->Pipelines_Built();
	auto limited = [&]() {
		const bool stop = result.Attempts >= pipelineLimit || SDL_GetTicksNS() - start >= Uint64(budgetMS) * 1000000;
		result.Limited = result.Limited || stop;
		return stop;
	};
	bool shadersOnly = true;
	auto warm = [&](const VertexPipelineDescription &v, const CombinerDescription &p, unsigned blend,
					unsigned cull, int enginePixel = ENGINE_SHADER_NONE, bool fft = false,
					bool caster = false, int engineVertex = ENGINE_SHADER_NONE) {
		if (limited()) return;
		if (!shadersOnly) ++result.Attempts;
		const SdlProgram &vs = engineVertex != ENGINE_SHADER_NONE ? Programs->Engine_Vertex_Program(engineVertex)
			: fft ? Programs->Water_Vertex_Program(v) : Programs->Vertex_Program(v);
		const SdlProgram &ps = caster ? Programs->Sun_Pixel_Program(p, enginePixel)
			: enginePixel != ENGINE_SHADER_NONE ? Programs->Engine_Pixel_Program(enginePixel, p.PixelPipeline)
			: Programs->Pixel_Program(p);
		if (!vs.Shader || !ps.Shader) { ++result.Failed; return; }
		if (shadersOnly) return;
		RenderUInt32 rs[RENDER_STATE_COUNT] = {};
		rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
		rs[D3DRS_CULLMODE] = cull;
		rs[D3DRS_ZENABLE] = (v.FVF & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW ? D3DZB_FALSE : D3DZB_TRUE;
		rs[D3DRS_ZWRITEENABLE] = blend ? 0 : 1;
		rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
		rs[D3DRS_COLORWRITEENABLE] = 0xF;
		rs[D3DRS_ALPHABLENDENABLE] = blend != 0;
		rs[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
		rs[D3DRS_DESTBLEND] = blend == 2 ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA;
		rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
		SdlPipelineKey key;
		std::string refusal;
		if (!Sdl_Pipeline_Key(rs, D3DPT_TRIANGLELIST, vs.Shader, ps.Shader, v.FVF,
			caster ? 0 : SdlGpuFrame::Target_Format(),
			caster ? unsigned(SDL_GPU_TEXTUREFORMAT_D32_FLOAT) : Gpu->Depth_Format(), key, refusal)
			|| !Pipelines->Pipeline(key)) ++result.Failed;
	};

	auto visit = [&]() {
		// Water's large program is a measured opening hitch. Request its actual generated description,
		// including the four-coordinate FFT wrapper, before spending the budget on smaller programs.
		VertexPipelineDescription water = vertexDescription(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2, 4, false);
		for (unsigned i = 2; i < 4; ++i) {
			water.Stages[i].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
			water.Stages[i].TextureTransformFlags = D3DTTFF_COUNT2;
		}
		const int waterPixel = modernWater ? ENGINE_SHADER_WATER_TRAPEZOID : ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID;
		if (shadersOnly) {
			warm(water, pixelDescription(0, false, false), 1, D3DCULL_NONE, waterPixel);
			if (modernWater)
				warm(water, pixelDescription(0, false, false), 1, D3DCULL_NONE, waterPixel, true);
		}

		// Common opaque/alpha-tested unit and building meshes, including the sun-receiving variants.
		const unsigned formats[] = {D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1,
			D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2,
			D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1,
			D3DFVF_XYZ | D3DFVF_NORMAL};
		for (unsigned fvf : formats) {
			for (unsigned lights : {1u, 2u, 3u, 4u, 0u}) {
				if (!shadersOnly && lights == 0) continue;
				for (unsigned mode = 0; mode < (shadersOnly && fvf == formats[1] ? 3u : 1u); ++mode)
					for (bool alpha : {false, true})
						for (bool receives : {false, true})
							for (unsigned cull : {D3DCULL_CCW, D3DCULL_NONE})
								warm(vertexDescription(fvf, mode ? 2 : 1, true, lights),
									pixelDescription(mode, alpha, receives), 0, cull);
			}
			if (!shadersOnly && fvf == formats[0])
				for (unsigned cull : {D3DCULL_NONE, D3DCULL_CCW}) {
					warm(water, pixelDescription(0, false, false), 1, cull, waterPixel);
					if (modernWater)
						warm(water, pixelDescription(0, false, false), 1, cull, waterPixel, true);
				}
		}

		// Environment-mapped and scrolling materials use a different vertex program with the same
		// fragment program. The descriptions are the renderer's standard texture-transform modes.
		for (unsigned lights : {1u, 2u, 3u, 4u})
			for (unsigned coordinates : {0u, unsigned(D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR)})
				for (bool alpha : {false, true})
					for (bool receives : {false, true}) {
						auto v = vertexDescription(formats[0], 1, true, lights);
						v.Stages[0].TextureCoordinateIndex = coordinates;
						v.Stages[0].TextureTransformFlags = D3DTTFF_COUNT2;
						warm(v, pixelDescription(0, alpha, receives), 0, D3DCULL_CCW);
					}

		// Depth-only casters and the tree vertex program; no shadow target or scene is created here.
		for (unsigned fvf : {formats[0], formats[1], formats[2]})
			for (bool alpha : {false, true}) {
				const auto v = vertexDescription(fvf, 1, false);
				const auto p = pixelDescription(0, alpha, false);
				warm(v, p, 0, D3DCULL_CCW, ENGINE_SHADER_NONE, false, true);
				if (fvf == formats[2]) warm(v, p, 0, D3DCULL_CCW, ENGINE_SHADER_NONE, false, true, ENGINE_SHADER_TREES);
			}

		// Unlit transparent world geometry, smoke sprites and the interface. Their data remains live;
		// only the reusable programs and pipeline state are created, with no particle simulation.
		for (unsigned fvf : {unsigned(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1), formats[2], formats[1],
				unsigned(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1), unsigned(D3DFVF_XYZRHW | D3DFVF_DIFFUSE)})
			for (bool receives : {false, true})
				for (unsigned blend = 0; blend <= 2; ++blend)
					for (unsigned cull : {D3DCULL_CCW, D3DCULL_NONE}) {
						const auto v = vertexDescription(fvf, 1, false);
						warm(v, pixelDescription(0, false, receives), blend, cull);
						if (!(fvf & D3DFVF_NORMAL)) warm(v, pixelDescription(3, false, receives), blend, cull);
					}

	};
	visit();
	shadersOnly = false;
	visit();
	result.Programs = Programs->Programs_Built() - programsBefore;
	result.Pipelines = Pipelines->Pipelines_Built() - pipelinesBefore;
	result.Milliseconds = double(SDL_GetTicksNS() - start) / 1.0e6;
	return result;
}

RenderWarmupStats Preload_Render_Pipelines(IDirect3DDevice9 *device, bool modernWater)
{
	return device ? static_cast<PosixDevice9 *>(device)->Warm_Up_Render_Pipelines(modernWater) : RenderWarmupStats{};
}
