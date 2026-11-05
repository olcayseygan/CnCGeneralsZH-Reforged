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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11backend.cpp by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

// The draw's resolve (decision 7, phase A3c): the device's D3D9 state, as set, into D3's generator
// descriptions and the constants their programs read.  It reads the state the way dx11backend reads its
// mirror of the same state (Build_Combiner_Description, Build_Vertex_Description, Upload_Constants), so
// the two backends hand the generators the same thing; dx11backend's own additions (normal maps, the
// shadow map) are not A3's.  And D3D9's documented initial states, which the draw is the first to read.

#include "PosixDevice9.h"
#include "PosixResources9.h"
#include "SdlConstants.h"
#include "SdlCreationLog.h"
#include "SdlGpuFrame.h"
#include "SdlPipelineCache.h"
#include "SdlProgramCache.h"
#include "SdlResourceMirror.h"

#include "engineshader.h"
#include "ffshader.h"
#include "ffvertex.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterFoamHistory.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <cmath>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void PosixDevice9::Shore_Begin(bool enabled)
{
	Shore_End();
	ShoreActive=enabled;
}
void PosixDevice9::Shore_End()
{
	ShoreActive=ShoreHasPatch=ShoreHasTrack=false;ShoreRegions.clear();
}
void PosixDevice9::Shore_Patch(const WaterShoreOverlay::Quad *patch)
{
	ShoreHasPatch=ShoreActive && patch && WaterShoreOverlay::Convex(*patch);
	if(ShoreHasPatch) ShorePatch=*patch;
}
void PosixDevice9::Shore_Track(const WaterShoreOverlay::Quad *track)
{
	ShoreHasTrack=ShoreActive && track && WaterShoreOverlay::Convex(*track);
	if(ShoreHasTrack) ShoreTrack=*track;
}
void PosixWaterShoreBegin(IDirect3DDevice9 *d,bool e) {if(d) static_cast<PosixDevice9*>(d)->Shore_Begin(e);}
void PosixWaterShoreEnd(IDirect3DDevice9 *d) {if(d) static_cast<PosixDevice9*>(d)->Shore_End();}
void PosixWaterShorePatch(IDirect3DDevice9 *d,const WaterShoreOverlay::Quad *q) {if(d) static_cast<PosixDevice9*>(d)->Shore_Patch(q);}
void PosixWaterShoreTrack(IDirect3DDevice9 *d,const WaterShoreOverlay::Quad *q) {if(d) static_cast<PosixDevice9*>(d)->Shore_Track(q);}

//-------------------------------------------------------------------------------------------------
// D3D9's initial state, from its documentation of each state.  A float state holds its float's bits.
//-------------------------------------------------------------------------------------------------

static RenderUInt32 float_bits(float value)
{
	RenderUInt32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

void PosixDevice9::Set_Default_States()
{
	RenderUInt32 *rs = RenderStates;
	rs[D3DRS_ZENABLE] = Parameters.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
	rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
	rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	rs[D3DRS_ZWRITEENABLE] = 1;
	rs[D3DRS_ALPHATESTENABLE] = 0;
	rs[D3DRS_LASTPIXEL] = 1;
	rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	rs[D3DRS_CULLMODE] = D3DCULL_CCW;
	rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	rs[D3DRS_ALPHAREF] = 0;
	rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_DITHERENABLE] = 0;
	rs[D3DRS_ALPHABLENDENABLE] = 0;
	rs[D3DRS_FOGENABLE] = 0;
	rs[D3DRS_SPECULARENABLE] = 0;
	rs[D3DRS_FOGCOLOR] = 0;
	rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
	rs[D3DRS_FOGSTART] = float_bits(0.0f);
	rs[D3DRS_FOGEND] = float_bits(1.0f);
	rs[D3DRS_FOGDENSITY] = float_bits(1.0f);
	rs[D3DRS_RANGEFOGENABLE] = 0;
	rs[D3DRS_STENCILENABLE] = 0;
	rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_STENCILREF] = 0;
	rs[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
	rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
	rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFFu;
	rs[D3DRS_CLIPPING] = 1;
	rs[D3DRS_LIGHTING] = 1;
	rs[D3DRS_AMBIENT] = 0;
	rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
	rs[D3DRS_COLORVERTEX] = 1;
	rs[D3DRS_LOCALVIEWER] = 1;
	rs[D3DRS_NORMALIZENORMALS] = 0;
	rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
	rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
	rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
	rs[D3DRS_CLIPPLANEENABLE] = 0;
	rs[D3DRS_POINTSIZE] = float_bits(1.0f);
	rs[D3DRS_POINTSIZE_MIN] = float_bits(1.0f);
	rs[D3DRS_POINTSPRITEENABLE] = 0;
	rs[D3DRS_POINTSCALEENABLE] = 0;
	rs[D3DRS_MULTISAMPLEANTIALIAS] = 1;
	rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFFu;
	rs[D3DRS_POINTSIZE_MAX] = float_bits(64.0f);
	rs[D3DRS_COLORWRITEENABLE] = 0x0000000Fu;
	rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
	rs[D3DRS_SCISSORTESTENABLE] = 0;
	rs[D3DRS_SLOPESCALEDEPTHBIAS] = 0;
	rs[D3DRS_DEPTHBIAS] = 0;
	rs[D3DRS_TWOSIDEDSTENCILMODE] = 0;
	rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_SEPARATEALPHABLENDENABLE] = 0;
	rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
	rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
	rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFFu;

	// Stage 0 modulates the texture with the diffuse colour and takes the texture's alpha; every other
	// stage is disabled.  Each stage reads the coordinate set of its own number.
	for (int stage = 0; stage < TEXTURE_STAGE_COUNT; ++stage) {
		RenderUInt32 *ts = TextureStageStates[stage];
		ts[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		ts[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		ts[D3DTSS_COLORARG2] = D3DTA_CURRENT;
		ts[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		ts[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		ts[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		ts[D3DTSS_TEXCOORDINDEX] = (RenderUInt32)stage;
		ts[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
		ts[D3DTSS_COLORARG0] = D3DTA_CURRENT;
		ts[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
		ts[D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}
	for (int sampler = 0; sampler < SAMPLER_COUNT; ++sampler) {
		RenderUInt32 *ss = SamplerStates[sampler];
		ss[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
		ss[D3DSAMP_BORDERCOLOR] = 0;
		ss[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
		ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
		ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
		ss[D3DSAMP_MIPMAPLODBIAS] = 0;
		ss[D3DSAMP_MAXMIPLEVEL] = 0;
		ss[D3DSAMP_MAXANISOTROPY] = 1;
	}
}

//-------------------------------------------------------------------------------------------------
// The descriptions.
//-------------------------------------------------------------------------------------------------

bool PosixDevice9::Stage_Ends_Cascade(unsigned int stage) const
{
	// A disabled stage ends it; so does one whose COLORARG1 is the texture when none is bound.  That
	// second case is D3D9's documented one (FFReference's N20, from the D3DTA and texture blending
	// pages); ffshader's own answer for a stage with no texture is opaque white, which D3 left for a
	// Windows measurement to settle, and a stage that reads the texture only elsewhere still gets it.
	const RenderUInt32 *ts = TextureStageStates[stage];
	return ts[D3DTSS_COLOROP] == D3DTOP_DISABLE
		|| ((ts[D3DTSS_COLORARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE && Textures[stage] == NULL);
}

void PosixDevice9::Build_Combiner_Description(CombinerDescription &description) const
{
	// memset first, as dx11backend's does: descriptions are compared with memcmp, padding included.
	memset(&description, 0, sizeof(description));

	// The alpha test and the fog are pipeline state under D3D9 and instructions in the program here.
	description.PixelPipeline.AlphaTestEnabled = RenderStates[D3DRS_ALPHATESTENABLE] != 0;
	description.PixelPipeline.AlphaFunction = RenderStates[D3DRS_ALPHAFUNC];
	if (SunActive && RenderStates[D3DRS_ALPHABLENDENABLE]) {
		description.PixelPipeline.AlphaTestEnabled = true;
		const bool addsLight = RenderStates[D3DRS_DESTBLEND] == D3DBLEND_ONE || RenderStates[D3DRS_SRCBLEND] == D3DBLEND_ONE;
		description.PixelPipeline.AlphaFunction = addsLight ? D3DCMP_NEVER : D3DCMP_GREATEREQUAL;
	}
	description.PixelPipeline.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;
	description.SpecularAdd = RenderStates[D3DRS_SPECULARENABLE] != 0;

	if (Stage_Ends_Cascade(0)) {
		// No texturing: D3D9 draws the diffuse colour and its alpha.  The generator ends its chain at a
		// disabled stage and would refuse, so this is the one-stage combiner that means the same.
		CombinerStage &stage = description.Stages[0];
		stage.ColourOperation = D3DTOP_SELECTARG1;
		stage.ColourArgument1 = D3DTA_DIFFUSE;
		stage.ColourArgument2 = D3DTA_CURRENT;
		stage.ColourArgument0 = D3DTA_CURRENT;
		stage.AlphaOperation = D3DTOP_SELECTARG1;
		stage.AlphaArgument1 = D3DTA_DIFFUSE;
		stage.AlphaArgument2 = D3DTA_CURRENT;
		stage.AlphaArgument0 = D3DTA_CURRENT;
		stage.TextureCoordinateIndex = 0;
		stage.TextureBound = false;
		description.StageCount = 1;
		return;
	}

	for (unsigned index = 0; index < MAXIMUM_COMBINER_STAGES; ++index) {
		const RenderUInt32 *ts = TextureStageStates[index];
		if (Stage_Ends_Cascade(index)) {
			break;
		}
		CombinerStage &stage = description.Stages[index];
		stage.ColourOperation = ts[D3DTSS_COLOROP];
		stage.ColourArgument0 = ts[D3DTSS_COLORARG0];
		stage.ColourArgument1 = ts[D3DTSS_COLORARG1];
		stage.ColourArgument2 = ts[D3DTSS_COLORARG2];
		stage.AlphaOperation = ts[D3DTSS_ALPHAOP];
		stage.AlphaArgument0 = ts[D3DTSS_ALPHAARG0];
		stage.AlphaArgument1 = ts[D3DTSS_ALPHAARG1];
		stage.AlphaArgument2 = ts[D3DTSS_ALPHAARG2];
		stage.TextureCoordinateIndex = ts[D3DTSS_TEXCOORDINDEX];
		stage.TextureBound = Textures[index] != NULL;
		description.StageCount = index + 1;
	}
}

// A material source as D3D9 reads it: COLOR1 or COLOR2 names the vertex's diffuse or specular colour,
// and with COLORVERTEX off, or a vertex that has not got that colour, the material's own is used
// ("D3DMATERIALCOLORSOURCE").  The generator reads no vertex specular, so COLOR2 it can take only as that
// fallback.
static RenderUInt32 material_source(RenderUInt32 source, RenderUInt32 fvf, bool colour_vertex)
{
	if (source == D3DMCS_COLOR1 && (!colour_vertex || (fvf & D3DFVF_DIFFUSE) == 0)) {
		return D3DMCS_MATERIAL;
	}
	if (source == D3DMCS_COLOR2 && (!colour_vertex || (fvf & D3DFVF_SPECULAR) == 0)) {
		return D3DMCS_MATERIAL;
	}
	return source;
}

bool PosixDevice9::Build_Vertex_Description(VertexPipelineDescription &description, std::string *refusal) const
{
	memset(&description, 0, sizeof(description));
	description.FVF = FVF;
	// Pretransformed vertices skip transform and lighting altogether.
	description.LightingEnabled = RenderStates[D3DRS_LIGHTING] != 0 && (FVF & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW;
	description.SpecularEnabled = RenderStates[D3DRS_SPECULARENABLE] != 0;
	description.LocalViewer = RenderStates[D3DRS_LOCALVIEWER] != 0;
	description.ColourVertexEnabled = RenderStates[D3DRS_COLORVERTEX] != 0;
	const bool colour_vertex = description.ColourVertexEnabled;
	description.DiffuseMaterialSource = material_source(RenderStates[D3DRS_DIFFUSEMATERIALSOURCE], FVF, colour_vertex);
	description.AmbientMaterialSource = material_source(RenderStates[D3DRS_AMBIENTMATERIALSOURCE], FVF, colour_vertex);
	description.EmissiveMaterialSource = material_source(RenderStates[D3DRS_EMISSIVEMATERIALSOURCE], FVF, colour_vertex);
	description.SpecularMaterialSource = material_source(RenderStates[D3DRS_SPECULARMATERIALSOURCE], FVF, colour_vertex);

	// The enabled lights, packed down in order: the program declares them contiguously, and
	// Build_Constants packs their fields the same way.  Unlit, the program has none.
	description.LightCount = 0;
	for (unsigned index = 0; index < LIGHT_COUNT && description.LightingEnabled; ++index) {
		if (!LightsEnabled[index]) {
			continue;
		}
		if (description.LightCount == MAXIMUM_VERTEX_LIGHTS) {
			if (refusal != NULL) *refusal = "more lights enabled than the generator carries";
			return false;
		}
		description.Lights[description.LightCount].Type = Lights[index].Type;
		++description.LightCount;
	}

	// The same stages the combiner walk takes: the program only carries coordinates for those.  A
	// disabled stage 0 is the one-stage diffuse combiner, which reads no coordinates but has a stage.
	// With a pixel shader bound the cascade is the program's, and every stage still gets its coordinates
	// from its TEXCOORDINDEX: the engine's programs sample stages whose COLOROP is DISABLE (the terrain's
	// cloud and noise, the water's highlights and shroud), as dx11backend's every_stage has it (A3e-3:
	// without this they sampled at (0, 0), found against the tests' shader interpreter).
	const bool every_stage = PixelShader != NULL;
	description.StageCount = 0;
	for (unsigned index = 0; index < MAXIMUM_VERTEX_STAGES; ++index) {
		if (!every_stage && index > 0 && Stage_Ends_Cascade(index)) {
			break;
		}
		if (!every_stage && index == 0 && Stage_Ends_Cascade(0)) {
			description.StageCount = 1;
			break;
		}
		description.Stages[index].TextureCoordinateIndex = TextureStageStates[index][D3DTSS_TEXCOORDINDEX];
		description.Stages[index].TextureTransformFlags = TextureStageStates[index][D3DTSS_TEXTURETRANSFORMFLAGS];
		description.StageCount = index + 1;
	}

	description.FogEnabled = RenderStates[D3DRS_FOGENABLE] != 0;
	description.FogVertexMode = RenderStates[D3DRS_FOGVERTEXMODE];
	// The engine fogs per vertex, LINEAR (dx8wrapper's defaults); the generator has no table fog, and no
	// fog factor taken from the specular alpha, which is D3D9's answer with both modes NONE.
	if (description.FogEnabled && RenderStates[D3DRS_FOGTABLEMODE] != D3DFOG_NONE) {
		if (refusal != NULL) *refusal = "table fog";
		return false;
	}
	if (description.FogEnabled && description.FogVertexMode == D3DFOG_NONE) {
		if (refusal != NULL) *refusal = "fog from the specular alpha (both fog modes NONE)";
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
// The constants.  Row major, D3D9's order, which is how the programs declare their matrices.
//-------------------------------------------------------------------------------------------------

static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// A light is set in world space and lit in camera space: its position and direction go through the
// view matrix once per light, as the fixed-function pipeline carries them.
static void transform_point(const float source[3], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column] + source[1] * matrix[4 + column]
			+ source[2] * matrix[8 + column] + matrix[12 + column];
	}
}

// Visual-only inverse for reconstructing the water surface from depth. Double intermediates keep
// distant camera translations stable; this does not reach simulation or replay arithmetic.
static bool water_inverse(const float source[16], float result[16])
{
	double rows[4][8];
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 8; ++c)
			rows[r][c] = c < 4 ? source[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for (unsigned c = 0; c < 4; ++c) {
		unsigned pivot = c;
		for (unsigned r = c + 1; r < 4; ++r)
			if (fabs(rows[r][c]) > fabs(rows[pivot][c])) pivot = r;
		if (!isfinite(rows[pivot][c]) || fabs(rows[pivot][c]) < 1e-15) return false;
		for (unsigned k = 0; k < 8; ++k) {
			const double saved = rows[c][k]; rows[c][k] = rows[pivot][k]; rows[pivot][k] = saved;
		}
		const double scale = rows[c][c];
		for (unsigned k = 0; k < 8; ++k) rows[c][k] /= scale;
		for (unsigned r = 0; r < 4; ++r) {
			if (r == c) continue;
			const double factor = rows[r][c];
			for (unsigned k = 0; k < 8; ++k) rows[r][k] -= factor * rows[c][k];
		}
	}
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 4; ++c) result[r * 4 + c] = (float)rows[r][c + 4];
	return true;
}

static void transform_direction(const float source[3], const float matrix[16], float result[4])
{
	for (unsigned column = 0; column < 4; ++column) {
		result[column] = source[0] * matrix[column] + source[1] * matrix[4 + column] + source[2] * matrix[8 + column];
	}
}

// The inverse transpose of the upper three by three, for the normals: the cofactors over the
// determinant.  A singular matrix gives the identity, as dx11backend's does.
static void inverse_transpose(const float s[16], float result[16])
{
	const float a = s[0], b = s[1], c = s[2];
	const float d = s[4], e = s[5], f = s[6];
	const float g = s[8], h = s[9], i = s[10];
	const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	memset(result, 0, 16 * sizeof(float));
	result[0] = result[5] = result[10] = result[15] = 1.0f;
	if (determinant == 0.0f) {
		return;
	}
	const float scale = 1.0f / determinant;
	result[0] = (e * i - f * h) * scale;
	result[1] = (f * g - d * i) * scale;
	result[2] = (d * h - e * g) * scale;
	result[4] = (c * h - b * i) * scale;
	result[5] = (a * i - c * g) * scale;
	result[6] = (b * g - a * h) * scale;
	result[8] = (b * f - c * e) * scale;
	result[9] = (c * d - a * f) * scale;
	result[10] = (a * e - b * d) * scale;
}

static void colour_of(RenderUInt32 argb, float out[4])
{
	out[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
	out[1] = (float)((argb >> 8) & 0xFF) / 255.0f;
	out[2] = (float)(argb & 0xFF) / 255.0f;
	out[3] = (float)((argb >> 24) & 0xFF) / 255.0f;
}

static void colour_value(const D3DCOLORVALUE &colour, float out[4])
{
	out[0] = colour.r;
	out[1] = colour.g;
	out[2] = colour.b;
	out[3] = colour.a;
}

void PosixDevice9::Build_Constants(SdlVertexConstants &vertex, SdlPixelConstants &pixel) const
{
	memset(&vertex, 0, sizeof(vertex));
	memset(&pixel, 0, sizeof(pixel));

	const float *world = &Transforms[D3DTS_WORLD]._11;
	const float *view = &Transforms[D3DTS_VIEW]._11;
	const float *projection = &Transforms[D3DTS_PROJECTION]._11;
	float world_view[16];
	multiply(world, view, world_view);
	multiply(world_view, projection, vertex.WorldViewProjection);
	memcpy(vertex.WorldView, world_view, sizeof(world_view));
	inverse_transpose(world_view, vertex.NormalTransform);
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		memcpy(vertex.TextureMatrix[stage], &Transforms[D3DTS_TEXTURE0 + stage]._11, 16 * sizeof(float));
	}

	colour_value(Material.Ambient, vertex.MaterialAmbient);
	colour_value(Material.Diffuse, vertex.MaterialDiffuse);
	colour_value(Material.Specular, vertex.MaterialSpecular);
	colour_value(Material.Emissive, vertex.MaterialEmissive);
	vertex.MaterialPower[0] = Material.Power;
	colour_of(RenderStates[D3DRS_AMBIENT], vertex.GlobalAmbient);

	// Fog start, end and density are floats carried in DWORD states.
	memcpy(&vertex.FogParameters[0], &RenderStates[D3DRS_FOGSTART], sizeof(float));
	memcpy(&vertex.FogParameters[1], &RenderStates[D3DRS_FOGEND], sizeof(float));
	memcpy(&vertex.FogParameters[2], &RenderStates[D3DRS_FOGDENSITY], sizeof(float));
	vertex.ViewportInverse[0] = Viewport.Width != 0 ? 1.0f / (float)Viewport.Width : 0.0f;
	vertex.ViewportInverse[1] = Viewport.Height != 0 ? 1.0f / (float)Viewport.Height : 0.0f;

	// The enabled lights in the order Build_Vertex_Description packed them.  The attenuation and the
	// spot cone as DX8Wrapper hands them to the Direct3D 11 backend: {a0, a1, a2, range} and
	// {cos(theta / 2), cos(phi / 2), falloff, 0}.
	unsigned slot = 0;
	for (unsigned index = 0; index < LIGHT_COUNT && slot < MAXIMUM_VERTEX_LIGHTS; ++index) {
		if (!LightsEnabled[index]) {
			continue;
		}
		const D3DLIGHT9 &light = Lights[index];
		const float position[3] = { light.Position.x, light.Position.y, light.Position.z };
		const float direction[3] = { light.Direction.x, light.Direction.y, light.Direction.z };
		float (*fields)[4] = vertex.LightFields[slot];
		transform_point(position, view, fields[0]);
		transform_direction(direction, view, fields[1]);
		colour_value(light.Diffuse, fields[2]);
		colour_value(light.Specular, fields[3]);
		fields[4][0] = light.Attenuation0;
		fields[4][1] = light.Attenuation1;
		fields[4][2] = light.Attenuation2;
		fields[4][3] = light.Range;
		fields[5][0] = cosf(light.Theta * 0.5f);
		fields[5][1] = cosf(light.Phi * 0.5f);
		fields[5][2] = light.Falloff;
		fields[5][3] = 0.0f;
		colour_value(light.Ambient, fields[6]);
		++slot;
	}

	colour_of(RenderStates[D3DRS_TEXTUREFACTOR], pixel.TextureFactor);
	colour_of(RenderStates[D3DRS_FOGCOLOR], pixel.FogColour);
	// A whole level, as D3D9 compares it: the program rounds the pixel's alpha to a level first.
	pixel.AlphaReference[0] = SunActive && RenderStates[D3DRS_ALPHABLENDENABLE] ? 8.0f : (float)(RenderStates[D3DRS_ALPHAREF] & 0xFF);
}

bool PosixDevice9::Water_Foam_History_Available() const
{
	// History is an optional CPU-authored image. Never turn a stale stage binding, attachment,
	// invalid mapping or preceding ocean's atlas into a required input of another water draw.
	if (PixelShaderConstants[1][1] > 0.5f || PixelShaderConstants[89][0] <= 0.5f
		|| Textures[14] == NULL || Textures[14]->GetType() != D3DRTYPE_TEXTURE) return false;
	for (unsigned int c = 0; c < 4; ++c)
		if (!std::isfinite(PixelShaderConstants[88][c]) || !std::isfinite(PixelShaderConstants[89][c])) return false;
	if (PixelShaderConstants[88][2] <= 0 || PixelShaderConstants[88][3] <= 0 || PixelShaderConstants[89][2] <= 0) return false;
	const float flags=PixelShaderConstants[89][0];
	if(flags>7 || std::floor(flags)!=flags || (static_cast<unsigned>(flags)&3u)==0u) return false;
	D3DSURFACE_DESC desc;
	if(static_cast<IDirect3DTexture9 *>(Textures[14])->GetLevelDesc(0,&desc)!=D3D_OK
		|| desc.Width==0 || desc.Height==0 || (desc.Usage&D3DUSAGE_RENDERTARGET)!=0) return false;
	// The packed format has two equal logical tiles; malformed/stale bindings must not
	// reinterpret a middle row as a history/direction boundary. Legacy atlases remain valid.
	return (static_cast<unsigned>(flags)&4u)==0u || (desc.Height%2==0
		&& desc.Width<=WaterFoamHistory::MaxAtlasSide && desc.Height<=2*WaterFoamHistory::MaxAtlasSide);
}

void PosixDevice9::Build_Water_Constants(const SdlPixelConstants &pixel_constants, SdlWaterPixelConstants &water) const
{
	memset(&water, 0, sizeof(water));
	water.Combiner = pixel_constants;
	float view_projection[16], inverse_view[16];
	multiply(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11, view_projection);
	if (!water_inverse(view_projection, water.WorldFromClip)
		|| !water_inverse(&Transforms[D3DTS_VIEW]._11, inverse_view)) {
		memset(water.WorldFromClip, 0, sizeof(water.WorldFromClip));
		memset(inverse_view, 0, sizeof(inverse_view));
		for (unsigned i = 0; i < 4; ++i) water.WorldFromClip[i * 5] = inverse_view[i * 5] = 1.0f;
	}
	water.CameraPosition[0] = inverse_view[12];
	water.CameraPosition[1] = inverse_view[13];
	water.CameraPosition[2] = inverse_view[14];
	// Both SV_Position.z and sampled scene depth include the viewport's depth range. Compose its
	// inverse into the unprojection so custom MinZ/MaxZ use the same shader path as the normal range.
	const float depth_range = Viewport.MaxZ - Viewport.MinZ;
	if (depth_range > 1.0e-8f) {
		for (unsigned int column = 0; column < 4; ++column) {
			const float z_row = water.WorldFromClip[8 + column] / depth_range;
			water.WorldFromClip[12 + column] -= Viewport.MinZ * z_row;
			water.WorldFromClip[8 + column] = z_row;
		}
	}
	// Project SSR through the inverse of the exact matrix used for depth reconstruction.
	if (!water_inverse(water.WorldFromClip, water.ClipFromWorld)) {
		memset(water.ClipFromWorld, 0, sizeof(water.ClipFromWorld));
		for (unsigned i = 0; i < 4; ++i) water.ClipFromWorld[i * 5] = 1.0f;
	}
	water.Viewport[0] = (float)Viewport.X + 0.5f;
	water.Viewport[1] = (float)Viewport.Y + 0.5f;
	water.Viewport[2] = (float)Viewport.Width;
	water.Viewport[3] = (float)Viewport.Height;
	memcpy(water.WaterParameters, PixelShaderConstants[1], sizeof(water.WaterParameters));
	memcpy(water.ReflectionArea, PixelShaderConstants[2], sizeof(water.ReflectionArea));
	memcpy(water.SunDirection, PixelShaderConstants[3], sizeof(water.SunDirection));
	memcpy(water.SunColour, PixelShaderConstants[4], sizeof(water.SunColour));
	memcpy(water.TerrainParameters, PixelShaderConstants[5], sizeof(water.TerrainParameters));
	memcpy(water.OpticalParameters, PixelShaderConstants[6], sizeof(water.OpticalParameters));
	memcpy(water.HullParameters, PixelShaderConstants[7], sizeof(water.HullParameters));
	memcpy(water.HullData, PixelShaderConstants[8], sizeof(water.HullData));
	memcpy(water.ContactHullData, PixelShaderConstants[96], sizeof(water.ContactHullData));
	memcpy(water.FoamFillLights, PixelShaderConstants[90], sizeof(water.FoamFillLights));
	memcpy(water.MaterialColor, PixelShaderConstants[ModernWaterMaterial::RegisterColor], sizeof(water.MaterialColor));
	memcpy(water.MaterialParameters, PixelShaderConstants[ModernWaterMaterial::RegisterParameters], sizeof(water.MaterialParameters));
	if (Water_Foam_History_Available()) {
		memcpy(water.FoamHistoryArea, PixelShaderConstants[88], sizeof(water.FoamHistoryArea));
		memcpy(water.FoamHistoryParameters, PixelShaderConstants[89], sizeof(water.FoamHistoryParameters));
	}
	// River draws must never inherit a preceding ocean draw's hull field.
	if (water.WaterParameters[1] > 0.5f) water.HullParameters[0] = 0;
}

void PosixDevice9::Build_Water_Visual_Constants(const SdlWaterPixelConstants &water, SdlWaterVisualPixelConstants &visual) const
{
	visual.Water=water;
	memcpy(visual.VisualHullMeta,PixelShaderConstants[WaterHullVisualLayout::MetadataRegister],sizeof(visual.VisualHullMeta));
	// River and empty hull draws must not activate a preceding ocean's packet metadata.
	if(water.HullParameters[0]<=0) memset(visual.VisualHullMeta,0,sizeof(visual.VisualHullMeta));
}

//-------------------------------------------------------------------------------------------------
// The draws (A3c): recorded on the GPU with a window, nothing without one.
//-------------------------------------------------------------------------------------------------

// The vertices a primitive count reads; for an indexed draw, the indices.
static unsigned vertices_of(D3DPRIMITIVETYPE type, unsigned count)
{
	switch (type) {
		case D3DPT_POINTLIST:		return count;
		case D3DPT_LINELIST:		return count * 2;
		case D3DPT_LINESTRIP:		return count + 1;
		case D3DPT_TRIANGLELIST:	return count * 3;
		case D3DPT_TRIANGLESTRIP:	return count + 2;
		case D3DPT_TRIANGLEFAN:		return count + 2;
		default:					return 0;
	}
}

// A fan as a list: triangle k is (0, k + 1, k + 2) of the fan's vertices, D3D9's winding.
template <class Index>
static void expand_fan(const Index *fan, unsigned first, unsigned count, Index *list)
{
	for (unsigned k = 0; k < count; ++k) {
		list[k * 3] = fan != NULL ? fan[0] : (Index)first;
		list[k * 3 + 1] = fan != NULL ? fan[k + 1] : (Index)(first + k + 1);
		list[k * 3 + 2] = fan != NULL ? fan[k + 2] : (Index)(first + k + 2);
	}
}

static bool is_dynamic(IDirect3DVertexBuffer9 *buffer)
{
	D3DVERTEXBUFFER_DESC desc;
	return buffer->GetDesc(&desc) == D3D_OK && (desc.Usage & D3DUSAGE_DYNAMIC) != 0;
}

static bool is_dynamic(IDirect3DIndexBuffer9 *buffer)
{
	D3DINDEXBUFFER_DESC desc;
	return buffer->GetDesc(&desc) == D3D_OK && (desc.Usage & D3DUSAGE_DYNAMIC) != 0;
}

void PosixDevice9::Refuse_Draw(const std::string &reason)
{
	if (SunActive) SunFailed = true;
	unsigned int &count = DrawRefusals[reason];
	if (count++ == 0) {
		fprintf(stderr, "PosixDevice9: a draw refused: %s\n", reason.c_str());
	}
}

// A development aid until A3d's capture: ZH_GPU_TRACE="1024x1024" prints the state of each draw whose
// stage-0 texture is that size, once per distinct program pair and texture, with its first vertices.
void PosixDevice9::Trace_Draw_If_Asked(const DrawCall &call, const std::string &programs,
	PosixVertexBuffer9 *vertex_buffer, unsigned int stride)
{
	static const char *trace = getenv("ZH_GPU_TRACE");
	if (trace == NULL || Textures[0] == NULL || Textures[0]->GetType() != D3DRTYPE_TEXTURE) {
		return;
	}
	if (strcmp(trace, "alphatest") == 0) {
		// Every alpha-tested draw: the trees, the foliage, the cut-out models.
		if (RenderStates[D3DRS_ALPHATESTENABLE] == 0) {
			return;
		}
	}
	else {
		unsigned int width = 0, height = 0;
		if (sscanf(trace, "%ux%u", &width, &height) != 2) {
			return;
		}
		D3DSURFACE_DESC desc;
		static_cast<IDirect3DTexture9 *>(Textures[0])->GetLevelDesc(0, &desc);
		if (desc.Width != width || desc.Height != height) {
			return;
		}
	}
	static std::set<std::string> seen;
	char texture_name[32];
	snprintf(texture_name, sizeof(texture_name), "%p", (void *)Textures[0]);
	if (!seen.insert(programs + " " + texture_name).second || seen.size() > 64) {
		return;
	}
	fprintf(stderr, "TRACE draw: type %u, %u primitives, FVF 0x%x, stride %u, programs %s\n", (unsigned)call.Type,
		call.PrimitiveCount, (unsigned)FVF, stride, programs.c_str());
	for (unsigned int stage = 0; stage < 3; ++stage) {
		const RenderUInt32 *ts = TextureStageStates[stage];
		unsigned int w = 0, h = 0, format = 0;
		if (Textures[stage] != NULL && Textures[stage]->GetType() == D3DRTYPE_TEXTURE) {
			D3DSURFACE_DESC d;
			static_cast<IDirect3DTexture9 *>(Textures[stage])->GetLevelDesc(0, &d);
			w = d.Width; h = d.Height; format = d.Format;
		}
		fprintf(stderr, "TRACE   stage %u: colour op %u (%x,%x,%x) alpha op %u (%x,%x,%x) tci 0x%x ttff %u; texture %ux%u fmt %u;"
			" sampler u%u v%u min%u mag%u mip%u\n", stage, (unsigned)ts[D3DTSS_COLOROP], (unsigned)ts[D3DTSS_COLORARG0],
			(unsigned)ts[D3DTSS_COLORARG1], (unsigned)ts[D3DTSS_COLORARG2], (unsigned)ts[D3DTSS_ALPHAOP],
			(unsigned)ts[D3DTSS_ALPHAARG0], (unsigned)ts[D3DTSS_ALPHAARG1], (unsigned)ts[D3DTSS_ALPHAARG2],
			(unsigned)ts[D3DTSS_TEXCOORDINDEX], (unsigned)ts[D3DTSS_TEXTURETRANSFORMFLAGS], w, h, format,
			(unsigned)SamplerStates[stage][D3DSAMP_ADDRESSU], (unsigned)SamplerStates[stage][D3DSAMP_ADDRESSV],
			(unsigned)SamplerStates[stage][D3DSAMP_MINFILTER], (unsigned)SamplerStates[stage][D3DSAMP_MAGFILTER],
			(unsigned)SamplerStates[stage][D3DSAMP_MIPFILTER]);
	}
	const RenderUInt32 *rs = RenderStates;
	fprintf(stderr, "TRACE   lighting %u colorvertex %u sources d%u a%u e%u s%u; blend %u %u/%u op %u; alphatest %u ref %u func %u;"
		" z %u/%u; cull %u; fog %u; tfactor 0x%08x; ambient 0x%08x; specular %u\n", (unsigned)rs[D3DRS_LIGHTING],
		(unsigned)rs[D3DRS_COLORVERTEX], (unsigned)rs[D3DRS_DIFFUSEMATERIALSOURCE], (unsigned)rs[D3DRS_AMBIENTMATERIALSOURCE],
		(unsigned)rs[D3DRS_EMISSIVEMATERIALSOURCE], (unsigned)rs[D3DRS_SPECULARMATERIALSOURCE], (unsigned)rs[D3DRS_ALPHABLENDENABLE],
		(unsigned)rs[D3DRS_SRCBLEND], (unsigned)rs[D3DRS_DESTBLEND], (unsigned)rs[D3DRS_BLENDOP], (unsigned)rs[D3DRS_ALPHATESTENABLE],
		(unsigned)rs[D3DRS_ALPHAREF], (unsigned)rs[D3DRS_ALPHAFUNC], (unsigned)rs[D3DRS_ZENABLE], (unsigned)rs[D3DRS_ZWRITEENABLE],
		(unsigned)rs[D3DRS_CULLMODE], (unsigned)rs[D3DRS_FOGENABLE], (unsigned)rs[D3DRS_TEXTUREFACTOR], (unsigned)rs[D3DRS_AMBIENT],
		(unsigned)rs[D3DRS_SPECULARENABLE]);
	fprintf(stderr, "TRACE   material diffuse %.2f %.2f %.2f %.2f ambient %.2f %.2f %.2f emissive %.2f %.2f %.2f; lights on:",
		Material.Diffuse.r, Material.Diffuse.g, Material.Diffuse.b, Material.Diffuse.a, Material.Ambient.r, Material.Ambient.g,
		Material.Ambient.b, Material.Emissive.r, Material.Emissive.g, Material.Emissive.b);
	for (unsigned int i = 0; i < LIGHT_COUNT; ++i) {
		if (LightsEnabled[i]) fprintf(stderr, " %u(type %u diffuse %.2f %.2f %.2f)", i, (unsigned)Lights[i].Type,
			Lights[i].Diffuse.r, Lights[i].Diffuse.g, Lights[i].Diffuse.b);
	}
	fprintf(stderr, "\n");
	const uint8_t *bytes = NULL;
	if (call.UserVertices != NULL) {
		bytes = (const uint8_t *)call.UserVertices;
	}
	else if (vertex_buffer != NULL) {
		const unsigned int first = call.Indexed ? (unsigned int)call.BaseVertex + call.MinVertex : call.StartVertex;
		bytes = vertex_buffer->storage().bytes() + StreamOffsets[0] + (size_t)first * stride;
	}
	for (unsigned int v = 0; bytes != NULL && v < 3; ++v) {
		fprintf(stderr, "TRACE   vertex %u:", v);
		for (unsigned int word = 0; word < stride / 4; ++word) {
			uint32_t bits;
			memcpy(&bits, bytes + (size_t)v * stride + word * 4, 4);
			float value;
			memcpy(&value, &bits, 4);
			fprintf(stderr, " %08x(%g)", bits, value);
		}
		fprintf(stderr, "\n");
	}
}

SDL_GPUTexture *PosixDevice9::Gpu_Texture_Of(IDirect3DSurface9 *surface, std::string &refusal)
{
	if (surface == BackBuffer) {
		return Gpu->Back_Buffer();
	}
	// A level of a render-target texture draws into that texture's GPU copy, which its draws then sample.
	IDirect3DTexture9 *texture = NULL;
	if (surface->GetContainer(IID_IDirect3DTexture9, (void **)&texture) == D3D_OK && texture != NULL) {
		IDirect3DSurface9 *level0 = NULL;
		texture->GetSurfaceLevel(0, &level0);
		const bool first_level = level0 == surface;
		if (level0 != NULL) {
			level0->Release();
		}
		SDL_GPUTexture *result = NULL;
		if (first_level) {
			result = Mirrors->Texture(texture, refusal);
		}
		else {
			refusal = "a texture level other than the first as a render target";
		}
		texture->Release();
		return result;
	}
	return Mirrors->Surface(surface, false, refusal);
}

bool PosixDevice9::Resolve_Target(SdlTarget &target, std::string &refusal)
{
	if (Sun_Target(target)) return true;
	if (RenderTargets[1] != NULL || RenderTargets[2] != NULL || RenderTargets[3] != NULL) {
		refusal = "more than one render target";
		return false;
	}
	IDirect3DSurface9 *colour = RenderTargets[0];
	if (colour == NULL) {
		refusal = "no render target";
		return false;
	}
	if (colour == BackBuffer) {
		target = Gpu->Back_Buffer_Target();
	}
	else {
		D3DSURFACE_DESC desc;
		colour->GetDesc(&desc);
		target.Colour = Gpu_Texture_Of(colour, refusal);
		target.Width = desc.Width;
		target.Height = desc.Height;
		if (target.Colour == NULL) {
			return false;
		}
		target.Depth = NULL;
	}
	// The depth-stencil: the implicit one where it fits, a size-matched scratch one where it does not
	// (D3D9 lets a smaller target borrow the bigger surface; SDL3 GPU wants one size a pass), and a
	// created depth surface's own.  With none bound, the draw's depth state is off and any one serves.
	if (DepthStencil != NULL && DepthStencil != DepthSurface) {
		D3DSURFACE_DESC desc;
		DepthStencil->GetDesc(&desc);
		if (desc.Width == target.Width && desc.Height == target.Height) {
			target.Depth = Mirrors->Surface(DepthStencil, true, refusal);
			return target.Depth != NULL;
		}
	}
	target.Depth = Gpu->Depth_For(target.Width, target.Height);
	if (target.Depth == NULL) {
		refusal = "no depth-stencil of the target's size";
		return false;
	}
	return true;
}

bool PosixDevice9::Engine_Program_Of(const void *shader, const char *stage, int &program)
{
	const std::string name = Engine_Name_Of(shader);
	program = EngineShader_From_File(name.c_str());
	if (program == ENGINE_SHADER_NONE) {
		Refuse_Draw(std::string("a ") + stage + " shader with no transcription: "
			+ (name.empty() ? std::string("one never registered") : name));
		return false;
	}
	return true;
}

RenderResult PosixDevice9::DrawPrimitive(D3DPRIMITIVETYPE type, unsigned int start_vertex, unsigned int primitive_count)
{
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.StartVertex = start_vertex;
	return Gpu_Draw(call);
}

RenderResult PosixDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, int base_vertex, unsigned int min_vertex,
	unsigned int vertex_count, unsigned int start_index, unsigned int primitive_count)
{
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.Indexed = true;
	call.BaseVertex = base_vertex;
	call.MinVertex = min_vertex;
	call.VertexCount = vertex_count;
	call.StartIndex = start_index;
	return Gpu_Draw(call);
}

RenderResult PosixDevice9::DrawPrimitiveUP(D3DPRIMITIVETYPE type, unsigned int primitive_count, const void *vertices,
	unsigned int stride)
{
	if (vertices == NULL) {
		if (SunActive) SunFailed = true;
		return D3DERR_INVALIDCALL;
	}
	DrawCall call;
	memset(&call, 0, sizeof(call));
	call.Type = type;
	call.PrimitiveCount = primitive_count;
	call.UserVertices = vertices;
	call.UserStride = stride;
	const RenderResult result = Gpu_Draw(call);
	// D3D9: "After calling DrawPrimitiveUP, the stream 0 settings ... are set to NULL."
	Posix_Bind(Streams[0], (IDirect3DVertexBuffer9 *)NULL);
	StreamOffsets[0] = 0;
	StreamStrides[0] = 0;
	return result;
}

RenderResult PosixDevice9::Gpu_Draw(const DrawCall &call)
{
	// PERF1: the CPU time spent here, added to the frame's when ZH_GPU_TIMING asks.
	struct DrawTimer
	{
		double *Into;
		Uint64 Start;
		explicit DrawTimer(double *into) : Into(into), Start(into != NULL ? SDL_GetTicksNS() : 0) {}
		~DrawTimer() { if (Into != NULL) *Into += (double)(SDL_GetTicksNS() - Start) / 1.0e6; }
	} timer(Timing_Is_Asked() ? &TimingDrawMs : NULL);
	// PERF1's hitch hunt: with ZH_GPU_CREATION_LOG, a draw over 20 ms says how long each of its phases took.
	struct PhaseClock
	{
		bool On;
		double Start, Last;
		double Took[8];
		const char *Names[8];
		int Count;
		double Parts[3];	// inside the copies: textures, samplers, buffers
		int Rounds;
		explicit PhaseClock(bool on) : On(on), Start(on ? Sdl_Now_Ms() : 0.0), Last(Start), Count(0), Rounds(0) { Parts[0] = Parts[1] = Parts[2] = 0.0; }
		double Now() const { return On ? Sdl_Now_Ms() : 0.0; }
		void Part(int which, double since) { if (On) Parts[which] += Sdl_Now_Ms() - since; }
		void Mark(const char *name)
		{
			if (!On || Count == 8) return;
			const double now = Sdl_Now_Ms();
			Names[Count] = name;
			Took[Count++] = now - Last;
			Last = now;
		}
		~PhaseClock()
		{
			const double now = Sdl_Now_Ms();
			if (!On || now - Start <= 20.0) return;
			char line[512];
			int used = snprintf(line, sizeof(line), "PosixDevice9 create: t %10.1f ms  SLOW DRAW %8.2f ms:", Start, now - Start);
			for (int i = 0; i < Count && used > 0 && (size_t)used < sizeof(line); ++i) {
				used += snprintf(line + used, sizeof(line) - used, " %s %.2f", Names[i], Took[i]);
			}
			if (used > 0 && (size_t)used < sizeof(line)) {
				snprintf(line + used, sizeof(line) - used, " rest %.2f (copies: textures %.2f samplers %.2f buffers %.2f rounds %d)",
					now - Last, Parts[0], Parts[1], Parts[2], Rounds);
			}
			Sdl_Creation_Log_Line(line);
		}
	} phases(Sdl_Creation_Log_Asked());

	const unsigned int reads = vertices_of(call.Type, call.PrimitiveCount);
	if (reads == 0 && call.PrimitiveCount != 0) {
		if (SunActive) SunFailed = true;
		return D3DERR_INVALIDCALL;
	}
	if (Gpu == NULL || call.PrimitiveCount == 0) {
		return D3D_OK;		// -headless: drawn into a window nobody sees
	}

	// The engine's own programs (A3e).  The device never runs D3D bytecode: a bound shader is drawn from
	// D3's transcription of the program it was registered as (Platform/EngineShaderName.h), the other half
	// from the generators, as the Direct3D 11 backend draws them.  A shader that cannot be named is refused
	// by its name, never drawn as something close.
	int vertex_engine = ENGINE_SHADER_NONE;
	int pixel_engine = ENGINE_SHADER_NONE;
	if (VertexShader != NULL && !Engine_Program_Of(VertexShader, "vertex", vertex_engine)) {
		return D3D_OK;
	}
	if (PixelShader != NULL && !Engine_Program_Of(PixelShader, "pixel", pixel_engine)) {
		return D3D_OK;
	}
	if (vertex_engine != ENGINE_SHADER_NONE && pixel_engine != ENGINE_SHADER_NONE) {
		Refuse_Draw("an engine vertex program with an engine pixel program (a pair the game does not make)");
		return D3D_OK;
	}
	// The stream's layout is the FVF's with a transcribed program too: D3's Trees reads the tree buffer's
	// FVF slots, as the Direct3D 11 backend lays it out, and its D3D8 declaration only made the shader.
	if (FVF == 0) {
		Refuse_Draw("a vertex declaration and no FVF");
		return D3D_OK;
	}
	if (RenderStates[D3DRS_CLIPPLANEENABLE] != 0) {
		Refuse_Draw("user clip planes");
		return D3D_OK;
	}

	Mirrors->Collect_Dead();
	if (Gpu->Batch_Is_Full()) {
		if (!Gpu->Flush()) { Refuse_Draw("batch submission failed"); return D3D_OK; }
	}
	phases.Mark("dead+full");

	// Where it draws (A3d): the back buffer, or a render target's GPU texture.
	std::string target_refusal;
	SdlTarget target;
	if (!Resolve_Target(target, target_refusal)) {
		Refuse_Draw("the render target: " + target_refusal);
		return D3D_OK;
	}

	// The programs and the pipeline.
	std::string refusal;
	SdlVertexLayout layout;
	if (!Sdl_Vertex_Layout(FVF, layout, refusal)) {
		Refuse_Draw("the vertex format: " + refusal);
		return D3D_OK;
	}
	CombinerDescription combiner;
	VertexPipelineDescription vertex;
	Build_Combiner_Description(combiner);
	combiner.ShadowReceiving = Sun_Receiving(vertex_engine, pixel_engine, target);
	combiner.DrawEffect = SunActive ? SunCasterEffect : DRAW_EFFECT_NONE;
	combiner.SmokeGlow = SmokeGlow && vertex_engine == ENGINE_SHADER_NONE && pixel_engine == ENGINE_SHADER_NONE
		&& !RenderStates[D3DRS_LIGHTING] && (FVF & D3DFVF_NORMAL) && !(FVF & D3DFVF_XYZRHW);
	if (vertex_engine == ENGINE_SHADER_NONE && !Build_Vertex_Description(vertex, &refusal)) {
		Refuse_Draw(refusal);
		return D3D_OK;
	}
	if (vertex_engine == ENGINE_SHADER_NONE) vertex.SmokeGlow = combiner.SmokeGlow;
	// The dedicated generated water vertex path deliberately avoids arbitrary engine-program
	// pairing. Only this path may bind native FFT textures to the vertex stage. A sun caster
	// pass keeps the ordinary vertex program its depth-only pixel variant was made against.
	const bool fft_requested = !SunActive && pixel_engine == ENGINE_SHADER_WATER_TRAPEZOID
		&& vertex_engine == ENGINE_SHADER_NONE && !vertex.LightingEnabled
		&& PixelShaderConstants[6][2] > 0.5f && PixelShaderConstants[6][3] > 0.5f;
	bool fft_water = fft_requested && Gpu->Prepare_Ocean(PixelShaderConstants[1][0]*WaterFftSampling::TimeScale,
		WaterPerformanceSettings::Applied().requestedFftResolution);
	if (fft_water) WaterPerformanceSettings::Applied().activeFftResolution = Gpu->Ocean_Configuration().resolution;
	const SdlProgram *fft_vertex = fft_water ? &Programs->Water_Vertex_Program(vertex) : NULL;
	if (fft_vertex && fft_vertex->Shader == NULL) fft_water = false;
	const SdlProgram &vertex_program = vertex_engine != ENGINE_SHADER_NONE
		? Programs->Engine_Vertex_Program(vertex_engine)
		: fft_water ? *fft_vertex : Programs->Vertex_Program(vertex);
	const SdlProgram *pixel_program = SunActive
		? &Programs->Sun_Pixel_Program(combiner, pixel_engine)
		: pixel_engine != ENGINE_SHADER_NONE
			? &Programs->Engine_Pixel_Program(pixel_engine, combiner.PixelPipeline)
			: &Programs->Pixel_Program(combiner);
	const auto retry_without_sun = [&](const char *reason) {
		// Generated receivers add an optional program variant. Ground programs share the
		// established engine path; their existing source keeps its own failure handling.
		if (!combiner.ShadowReceiving || pixel_engine != ENGINE_SHADER_NONE || !vertex_program.Shader)
			return false;
		Fail_Sun_Receiver(reason);
		combiner.ShadowReceiving = false;
		pixel_program = &Programs->Pixel_Program(combiner);
		return pixel_program->Shader != NULL;
	};
	if (!pixel_program->Shader) retry_without_sun("pixel program refused");
	const SdlProgram *legacy_pixel_program=pixel_program;
	const ShoreRegion *shore_region=NULL;
	SDL_GPUTexture *shore_depth=NULL;
	bool shore_overlay=false;
	if(!SunActive && ShoreActive && ShoreHasTrack && pixel_engine==ENGINE_SHADER_NONE && vertex_engine==ENGINE_SHADER_NONE
		&& RenderStates[D3DRS_FILLMODE]==D3DFILL_SOLID && !RenderStates[D3DRS_ZWRITEENABLE] && RenderStates[D3DRS_ALPHABLENDENABLE]
		&& RenderStates[D3DRS_SRCBLEND]==D3DBLEND_SRCALPHA && RenderStates[D3DRS_DESTBLEND]==D3DBLEND_INVSRCALPHA
		&& !combiner.ShadowReceiving && !combiner.SoftParticle && !combiner.DrawEffect && !combiner.SmokeGlow
		&& combiner.StageCount<=2) {
		WaterShoreOverlay::View view;
		view.colour=reinterpret_cast<std::uintptr_t>(target.Colour);view.depth=reinterpret_cast<std::uintptr_t>(target.Depth);
		view.width=target.Width;view.height=target.Height;
		shore_depth=Gpu->Ready_Scene_Depth(target,view.capture);
		SdlPixelConstants unused={};SdlWaterPixelConstants water;Build_Water_Constants(unused,water);
		memcpy(view.worldFromClip,water.WorldFromClip,sizeof(view.worldFromClip));
		memcpy(view.viewport,water.Viewport,sizeof(view.viewport));
		if(shore_depth) for(const ShoreRegion &region:ShoreRegions) {
			if(WaterShoreOverlay::SameView(view,region.view) && WaterShoreOverlay::Contains(region.patch,ShoreTrack)) {
				// Upload failures are optional-feature failures. Keep the original .wak draw intact.
				SDL_GPUTexture *height=Mirrors->Texture(region.height.get(),refusal);
				SDL_GPUTexture *optical=Mirrors->Texture(region.optical.get(),refusal);
				if(height && optical && height!=target.Colour && height!=target.Depth && optical!=target.Colour && optical!=target.Depth) {
					const SdlProgram &candidate=Programs->Shore_Pixel_Program(combiner);
					if(candidate.Shader) {pixel_program=&candidate;shore_region=&region;shore_overlay=true;}
				}
				break;
			}
		}
	}
	if (vertex_program.Shader == NULL || pixel_program->Shader == NULL) {
		Refuse_Draw(vertex_program.Shader == NULL ? "a vertex program refused" : "a pixel program refused");
		return D3D_OK;
	}
	if (pixel_program->SamplerSlots > SdlRecordedDraw::MAXIMUM_SAMPLERS)
		retry_without_sun("receiver sampler count");
	if (pixel_program->SamplerSlots > SdlRecordedDraw::MAXIMUM_SAMPLERS
		|| vertex_program.SamplerSlots > (fft_water ? 4u : 0u)) {
		Refuse_Draw("more sampler slots than a draw binds");
		return D3D_OK;
	}
	const bool modern_water_pixel=pixel_engine==ENGINE_SHADER_WATER_TRAPEZOID || pixel_engine==ENGINE_SHADER_WATER_RIVER;
	if(!Sdl_Draw_Uniform_Buffers_Supported(fft_water,modern_water_pixel,
		vertex_program.UniformBuffers,pixel_program->UniformBuffers))
		retry_without_sun("receiver uniform layout");
	if(!pixel_program->Shader || !Sdl_Draw_Uniform_Buffers_Supported(fft_water,modern_water_pixel,
		vertex_program.UniformBuffers,pixel_program->UniformBuffers)) {
		Refuse_Draw("uniform blocks without an explicit draw binding");
		return D3D_OK;
	}
	// Every pass has the frame's depth-stencil, so every pipeline has its format; a draw with no depth
	// surface bound has D3D9's answer instead, depth and stencil off.
	const RenderUInt32 *states = RenderStates;
	RenderUInt32 without_depth[RENDER_STATE_COUNT];
	if (DepthStencil == NULL && !SunActive) {
		memcpy(without_depth, RenderStates, sizeof(without_depth));
		without_depth[D3DRS_ZENABLE] = D3DZB_FALSE;
		without_depth[D3DRS_ZWRITEENABLE] = 0;
		without_depth[D3DRS_STENCILENABLE] = 0;
		states = without_depth;
	}
	RenderUInt32 caster_states[RENDER_STATE_COUNT];
	if (SunActive) {
		memcpy(caster_states, states, sizeof(caster_states));
		caster_states[D3DRS_ZENABLE] = D3DZB_TRUE;
		caster_states[D3DRS_ZWRITEENABLE] = 1;
		caster_states[D3DRS_STENCILENABLE] = 0;
		states = caster_states;
	}
	// The modern flat-water shader already refracts and attenuates the scene. The legacy
	// destination-alpha mask would blend the unattenuated riverbed into it a second time.
	RenderUInt32 water_states[RENDER_STATE_COUNT];
	if (pixel_engine == ENGINE_SHADER_WATER_TRAPEZOID && states[D3DRS_SRCBLEND] == D3DBLEND_DESTALPHA
		&& states[D3DRS_DESTBLEND] == D3DBLEND_INVDESTALPHA) {
		memcpy(water_states, states, sizeof(water_states));
		water_states[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
		water_states[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
		states = water_states;
	}
	SdlPipelineKey key;
	if (!Sdl_Pipeline_Key(states, call.Type, vertex_program.Shader, pixel_program->Shader, FVF,
		target.Colour ? SdlGpuFrame::Target_Format() : 0, Gpu->Target_Depth_Format(target), key, refusal)) {
		Refuse_Draw("the pipeline: " + refusal);
		return D3D_OK;
	}
	phases.Mark("target+programs");
	SdlRecordedDraw draw;
	memset(&draw, 0, sizeof(draw));
	draw.Pipeline = Pipelines->Pipeline(key, combiner.ShadowReceiving && pixel_engine == ENGINE_SHADER_NONE);
	if (!draw.Pipeline && retry_without_sun("pipeline refused")) {
		if (Sdl_Pipeline_Key(states, call.Type, vertex_program.Shader, pixel_program->Shader, FVF,
			target.Colour ? SdlGpuFrame::Target_Format() : 0, Gpu->Target_Depth_Format(target), key, refusal))
			draw.Pipeline = Pipelines->Pipeline(key);
	}
	if(!draw.Pipeline && shore_overlay) {
		shore_overlay=false;pixel_program=legacy_pixel_program;
		if(Sdl_Pipeline_Key(states,call.Type,vertex_program.Shader,pixel_program->Shader,FVF,
			target.Colour ? SdlGpuFrame::Target_Format() : 0,Gpu->Target_Depth_Format(target),key,refusal))
			draw.Pipeline=Pipelines->Pipeline(key);
	}
	phases.Mark("pipeline");
	if (draw.Pipeline == NULL) {
		Refuse_Draw("the GPU refused a pipeline");
		return D3D_OK;
	}

	// The vertex and index sources.
	const bool user = call.UserVertices != NULL;
	const unsigned int stride = user ? call.UserStride : StreamStrides[0];
	PosixVertexBuffer9 *vertex_buffer = user ? NULL : static_cast<PosixVertexBuffer9 *>(Streams[0]);
	PosixIndexBuffer9 *index_buffer = call.Indexed ? static_cast<PosixIndexBuffer9 *>(Indices) : NULL;
	if ((!user && vertex_buffer == NULL) || (call.Indexed && index_buffer == NULL)) {
		if (SunActive) SunFailed = true;
		return D3DERR_INVALIDCALL;
	}
	if (stride != layout.Stride) {
		Refuse_Draw("a stream stride that is not the FVF's");
		return D3D_OK;
	}
	const bool fan = call.Type == D3DPT_TRIANGLEFAN;
	const bool stage_vertices = user || is_dynamic(vertex_buffer);
	const bool stage_indices = call.Indexed && (fan || is_dynamic(index_buffer));
	const unsigned int index_size = (call.Indexed && index_buffer->format() == D3DFMT_INDEX32) ? 4 : 2;
	const bool water_draw = pixel_engine == ENGINE_SHADER_WATER_TRAPEZOID || pixel_engine == ENGINE_SHADER_WATER_RIVER;
	const auto &water_settings = WaterPerformanceSettings::Applied();
	const bool ssr_colour = water_settings.sceneReflections && PixelShaderConstants[95][3] > .5f;
	const bool water_colour = WaterPerformanceSettings::SceneColourRequired(water_settings.refraction, ssr_colour);
	if (water_draw) Gpu->Prepare_Water_Snapshot(target, water_colour, true);
	SDL_GPUTexture *water_background = water_draw && water_colour ? Gpu->Water_Background(target) : NULL;
	SDL_GPUTexture *water_depth = water_draw ? Gpu->Water_Depth(target) : NULL;
	bool water_history = water_draw && Water_Foam_History_Available();
	draw.VertexSamplerCount = vertex_program.SamplerSlots;
	for (unsigned int slot = 0; slot < draw.VertexSamplerCount && slot < 3; ++slot) {
		draw.VertexTextures[slot] = Gpu->Ocean_Map(slot * 2);
		draw.VertexSamplers[slot] = Gpu->Ocean_Sampler();
	}

	// The GPU copies.  Bringing one up to date can flush the batch, which leaves the ones already asked
	// for marked as used by the batch before: so ask again until a round flushes nothing.
	for (int round = 0; round < 3; ++round) {
		const uint64_t batch = Gpu->Batch();
		++phases.Rounds;
		draw.SamplerCount = pixel_program->SamplerSlots;
		for (unsigned int slot = 0; slot < pixel_program->SamplerSlots; ++slot) {
			const int texture_stage = pixel_program->SlotTexture[slot];
			const int sampler_stage = pixel_program->SlotSampler[slot];
			// Ground programs declare these slots even when their receiving strength is zero.
			// Bind neutral textures then, never material textures or the active attachment.
			const bool ground = EngineShader_Paints_Ground(static_cast<EngineShaderProgram>(pixel_engine));
			if (texture_stage == 5 && (combiner.ShadowReceiving || ground)) {
				draw.Textures[slot] = combiner.ShadowReceiving ? Sun_Texture() : Mirrors->White();
				draw.Samplers[slot] = combiner.ShadowReceiving ? Sun_Sampler() : Samplers->Sampler(SamplerStates[0]);
				if (!draw.Textures[slot] || !draw.Samplers[slot]) {
					if (combiner.ShadowReceiving) {
						Fail_Sun_Receiver("depth texture or sampler unavailable");
						return Gpu_Draw(call); // the failure latch bounds this retry
					}
					Refuse_Draw("sun receiver binding");
					return D3D_OK;
				}
				continue;
			}
			if (texture_stage == 6 && (combiner.ShadowReceiving || ground)) {
				const bool smoke = combiner.ShadowReceiving && Gpu->Sun_Smoke_Texture();
				draw.Textures[slot] = smoke ? Gpu->Sun_Smoke_Texture() : Mirrors->White();
				draw.Samplers[slot] = smoke ? Gpu->Sun_Smoke_Sampler() : Samplers->Sampler(SamplerStates[0]);
				if (!draw.Textures[slot] || !draw.Samplers[slot]) {
					Refuse_Draw("smoke receiver binding");
					return D3D_OK;
				}
				continue;
			}
			if (water_draw && texture_stage >= 8 && texture_stage < 14) {
				draw.Textures[slot] = fft_water ? Gpu->Ocean_Map(texture_stage - 8) : Mirrors->White();
				draw.Samplers[slot] = fft_water ? Gpu->Ocean_Sampler() : Samplers->Sampler(SamplerStates[0]);
				if (!draw.Textures[slot] || !draw.Samplers[slot]) return D3D_OK;
				continue;
			}
			if (texture_stage < 0 || texture_stage >= SAMPLER_COUNT || sampler_stage < 0 || sampler_stage >= SAMPLER_COUNT) {
				Refuse_Draw("a program slot past the device's stages");
				return D3D_OK;
			}
			IDirect3DBaseTexture9 *texture = water_draw && texture_stage == 14 && !water_history ? NULL : Textures[texture_stage];
			if(shore_region && pixel_program!=legacy_pixel_program && texture_stage==2) texture=shore_region->height.get();
			if(shore_region && pixel_program!=legacy_pixel_program && texture_stage==4) texture=shore_region->optical.get();
			double since = phases.Now();
			draw.Textures[slot] = shore_region && pixel_program!=legacy_pixel_program && texture_stage==6
				? (shore_depth ? shore_depth : Mirrors->White())
				: water_draw && texture_stage == 5
				? (water_background ? water_background : Mirrors->White())
				: water_draw && texture_stage == 6
				? (water_depth ? water_depth : Mirrors->White())
				: (texture != NULL ? Mirrors->Texture(texture, refusal) : Mirrors->White());
			phases.Part(0, since);
			if (water_draw && texture_stage == 14 && (draw.Textures[slot] == NULL
				|| draw.Textures[slot] == target.Colour || draw.Textures[slot] == target.Depth)) {
				// Failure of an optional history upload must retain the working water fallback.
				water_history = false;
				draw.Textures[slot] = Mirrors->White();
			}
			if(shore_region && pixel_program!=legacy_pixel_program && (texture_stage==2||texture_stage==4||texture_stage==6)
				&& (!draw.Textures[slot]||draw.Textures[slot]==target.Colour||draw.Textures[slot]==target.Depth)) {
				shore_overlay=false;draw.Textures[slot]=Mirrors->White();
			}
			if (draw.Textures[slot] != NULL && (draw.Textures[slot] == target.Colour || draw.Textures[slot] == target.Depth)) {
				// Undefined in D3D9, and an error on SDL3 GPU: the pass would read what it writes.
				Refuse_Draw("sampling the render target it draws into");
				return D3D_OK;
			}
			if (draw.Textures[slot] == NULL) {
				Refuse_Draw("the texture: " + refusal);
				return D3D_OK;
			}
			since = phases.Now();
			RenderUInt32 water_sampler[SAMPLER_STATE_COUNT];
			const RenderUInt32 *sampler_states = SamplerStates[sampler_stage];
			if ((water_draw && (texture_stage == 2 || texture_stage == 4 || texture_stage == 5 || texture_stage == 6 || texture_stage == 7 || texture_stage == 14))
				|| (shore_region && pixel_program!=legacy_pixel_program && (texture_stage==2||texture_stage==4||texture_stage==6))) {
				memcpy(water_sampler, sampler_states, sizeof(water_sampler));
				sampler_states = water_sampler;
				water_sampler[D3DSAMP_ADDRESSU] = water_sampler[D3DSAMP_ADDRESSV]
					= texture_stage == 7 ? D3DTADDRESS_WRAP : D3DTADDRESS_CLAMP;
				water_sampler[D3DSAMP_MINFILTER] = water_sampler[D3DSAMP_MAGFILTER]
					= texture_stage == 6 ? D3DTEXF_POINT : D3DTEXF_LINEAR;
				water_sampler[D3DSAMP_MIPFILTER] = texture_stage == 7 ? D3DTEXF_LINEAR : D3DTEXF_NONE;
			}
			draw.Samplers[slot] = Samplers->Sampler(sampler_states);
			phases.Part(1, since);
			if (draw.Samplers[slot] == NULL) {
				Refuse_Draw("the sampler: " + Samplers->Refusal());
				return D3D_OK;
			}
		}
		if (draw.VertexSamplerCount > 3) {
			// The cage reads the same terrain resource as pixel stage 2. Resolve it
			// inside this retry loop: updating a mirror can flush the current batch.
			const double texture_since=phases.Now();
			draw.VertexTextures[3]=Textures[2] ? Mirrors->Texture(Textures[2],refusal) : Mirrors->White();
			phases.Part(0,texture_since);
			if (!draw.VertexTextures[3]) { Refuse_Draw("the vertex terrain texture: "+refusal);return D3D_OK; }
			if (draw.VertexTextures[3]==target.Colour || draw.VertexTextures[3]==target.Depth) {
				Refuse_Draw("sampling the render target as vertex terrain");return D3D_OK;
			}
			RenderUInt32 terrain_sampler[SAMPLER_STATE_COUNT];
			memcpy(terrain_sampler,SamplerStates[2],sizeof(terrain_sampler));
			terrain_sampler[D3DSAMP_ADDRESSU]=terrain_sampler[D3DSAMP_ADDRESSV]=D3DTADDRESS_CLAMP;
			terrain_sampler[D3DSAMP_MINFILTER]=terrain_sampler[D3DSAMP_MAGFILTER]=D3DTEXF_LINEAR;
			terrain_sampler[D3DSAMP_MIPFILTER]=D3DTEXF_NONE;
			const double sampler_since=phases.Now();
			draw.VertexSamplers[3]=Samplers->Sampler(terrain_sampler);
			phases.Part(1,sampler_since);
			if (!draw.VertexSamplers[3]) { Refuse_Draw("the vertex terrain sampler: "+Samplers->Refusal());return D3D_OK; }
		}
		const double buffers_since = phases.Now();
		if (!stage_vertices) {
			draw.VertexBuffer = Mirrors->Buffer(vertex_buffer, vertex_buffer->storage(), refusal);
		}
		if (call.Indexed && !stage_indices) {
			draw.IndexBuffer = Mirrors->Buffer(index_buffer, index_buffer->storage(), refusal);
		}
		phases.Part(2, buffers_since);
		if ((!stage_vertices && draw.VertexBuffer == NULL) || (call.Indexed && !stage_indices && draw.IndexBuffer == NULL)) {
			Refuse_Draw("the buffer: " + refusal);
			return D3D_OK;
		}
		if (Gpu->Batch() == batch) {
			break;
		}
	}

	phases.Mark("copies");
	// The vertices: a static buffer's copy is bound where the stream points, and a dynamic buffer's or
	// the caller's bytes are staged.  An indexed draw reads vertices MinVertex to MinVertex + VertexCount - 1 past
	// its base, and only those are staged: the stream is then bound MinVertex vertices before them, so each index
	// still lands on its own vertex.  The engine's dynamic buffer is one buffer filled front to back through a
	// frame, and copying from the base instead (every vertex before MinVertex too) copied the whole buffer so far
	// on every draw: in a particle-heavy frame nine bytes in ten were never read, and a Steam Deck's frames went from
	// 15 to 60 ms and more.  The binding cannot start before the stream does, so a draw whose vertices would land
	// too near its start still stages from its base, as before.
	if (stage_vertices) {
		unsigned int first = 0;
		unsigned int count = reads;
		unsigned int skipped = 0;		// vertices before MinVertex, not staged
		if (!user) {
			if (call.Indexed && call.BaseVertex < 0) {
				Refuse_Draw("a negative base vertex into a dynamic buffer");
				return D3D_OK;
			}
			first = call.Indexed ? (unsigned int)call.BaseVertex : call.StartVertex;
			count = call.Indexed ? call.MinVertex + call.VertexCount : reads;
			if (call.Indexed) {
				skipped = call.MinVertex;
			}
		}
#ifndef NDEBUG
		// Debug builds check what the staging rests on: no index of the draw below MinVertex, none past its range.
		if (!user && call.Indexed) {
			const size_t index_start = (size_t)call.StartIndex * index_size;
			if (index_start + (size_t)reads * index_size <= index_buffer->storage().length()) {
				const uint8_t *indices = index_buffer->storage().bytes() + index_start;
				for (unsigned int k = 0; k < reads; ++k) {
					const unsigned int index = index_size == 4 ? ((const uint32_t *)indices)[k] : ((const uint16_t *)indices)[k];
					if (index < call.MinVertex || index >= call.MinVertex + call.VertexCount) {
						fprintf(stderr, "PosixDevice9: a draw reads index %u outside MinVertex %u + VertexCount %u (start index %u, "
							"%u primitives, base vertex %d, programs %s | %s)\n", index, call.MinVertex, call.VertexCount,
							call.StartIndex, call.PrimitiveCount, call.BaseVertex, vertex_program.Key.c_str(), pixel_program->Key.c_str());
						abort();
					}
				}
			}
		}
#endif
		const size_t start = user ? 0 : (size_t)StreamOffsets[0] + (size_t)first * stride;
		const size_t size = (size_t)count * stride;
		if (!user && start + size > vertex_buffer->storage().length()) {
			if (SunActive) SunFailed = true;
			return D3DERR_INVALIDCALL;
		}
		const uint8_t *source = user ? (const uint8_t *)call.UserVertices : vertex_buffer->storage().bytes() + start;
		const size_t shift = (size_t)skipped * stride;
		uint32_t staged = 0;
		if (shift != 0 && Gpu->Stage_Offset_Next() >= shift) {
			memcpy(Gpu->Stage((uint32_t)(size - shift), staged), source + shift, size - shift);
			draw.VertexOffset = staged - (uint32_t)shift;
		}
		else {
			memcpy(Gpu->Stage((uint32_t)size, staged), source, size);
			draw.VertexOffset = staged;
		}
		draw.First = 0;
		draw.BaseVertex = 0;
	}
	else {
		draw.VertexOffset = StreamOffsets[0];
		draw.First = call.StartVertex;
		draw.BaseVertex = call.BaseVertex;
	}

	// The indices.
	if (call.Indexed) {
		const size_t start = (size_t)call.StartIndex * index_size;
		if (start + (size_t)reads * index_size > index_buffer->storage().length()) {
			if (SunActive) SunFailed = true;
			return D3DERR_INVALIDCALL;
		}
		const uint8_t *source = index_buffer->storage().bytes() + start;
		draw.IndexSize = index_size;
		if (fan) {
			draw.Count = call.PrimitiveCount * 3;
			uint8_t *list = Gpu->Stage(draw.Count * index_size, draw.IndexOffset);
			if (index_size == 4) {
				expand_fan((const uint32_t *)source, 0, call.PrimitiveCount, (uint32_t *)list);
			}
			else {
				expand_fan((const uint16_t *)source, 0, call.PrimitiveCount, (uint16_t *)list);
			}
			draw.First = 0;
		}
		else if (stage_indices) {
			draw.Count = reads;
			memcpy(Gpu->Stage(reads * index_size, draw.IndexOffset), source, (size_t)reads * index_size);
			draw.First = 0;
		}
		else {
			draw.Count = reads;
			draw.IndexOffset = 0;
			draw.First = call.StartIndex;
		}
	}
	else if (fan) {
		// Staged vertices start at the fan's first; a static buffer's copy is indexed from its start.
		const unsigned int first = stage_vertices ? 0 : call.StartVertex;
		draw.Count = call.PrimitiveCount * 3;
		if (first + reads > 0xFFFF) {
			draw.IndexSize = 4;
			expand_fan((const uint32_t *)NULL, first, call.PrimitiveCount, (uint32_t *)Gpu->Stage(draw.Count * 4, draw.IndexOffset));
		}
		else {
			draw.IndexSize = 2;
			expand_fan((const uint16_t *)NULL, first, call.PrimitiveCount, (uint16_t *)Gpu->Stage(draw.Count * 2, draw.IndexOffset));
		}
		draw.First = 0;
	}
	else {
		draw.Count = reads;
	}

	Trace_Draw_If_Asked(call, vertex_program.Key + " | " + pixel_program->Key,
		vertex_buffer, stride);
	if (Capture_Is_Asked()) {
		// The signature: both programs' keys, and the pipeline key's bytes (the pipeline's states, formats
		// and target format), so every pipeline the game makes has a draw to replay.  The key's shaders are
		// left out of the hash: they are the programs already named, and their addresses change run to run.
		SdlPipelineKey stable = key;
		stable.VertexShader = NULL;
		stable.PixelShader = NULL;
		uint64_t hash = 14695981039346656037ull;
		const uint8_t *key_bytes = (const uint8_t *)&stable;
		for (size_t i = 0; i < sizeof(stable); ++i) {
			hash = (hash ^ key_bytes[i]) * 1099511628211ull;
		}
		char pipeline[40];
		snprintf(pipeline, sizeof(pipeline), " | pipeline %016llx", (unsigned long long)hash);
		unsigned int sampled = 0;
		for (unsigned int slot = 0; slot < pixel_program->SamplerSlots; ++slot) {
			sampled |= 1u << pixel_program->SlotTexture[slot];
		}
		Capture_Draw(call, vertex_program.Key + " | " + pixel_program->Key + pipeline, stride, reads, sampled,
			target.Width, target.Height);
	}

	phases.Mark("staging");
	// The constants, pushed only when they change.
	SdlVertexConstants vertex_constants;
	SdlPixelConstants pixel_constants;
	Build_Constants(vertex_constants, pixel_constants);
	if (vertex_program.UniformBuffers != 0 && vertex_engine != ENGINE_SHADER_NONE) {
		// A transcribed vertex program reads the engine's own register bank, c0 to c95, as its b0.
		const uint32_t size = ENGINE_SHADER_CONSTANTS * sizeof(VertexShaderConstants[0]);
		draw.VertexConstants = Gpu->Constants(0, VertexShaderConstants, size);
		draw.VertexConstantsSize = size;
	}
	else if (vertex_program.UniformBuffers != 0) {
		draw.VertexConstants = Gpu->Constants(0, &vertex_constants, sizeof(vertex_constants));
		draw.VertexConstantsSize = sizeof(vertex_constants);
	}
	if (pixel_program->UniformBuffers != 0 &&
		(pixel_engine == ENGINE_SHADER_WATER_TRAPEZOID || pixel_engine == ENGINE_SHADER_WATER_RIVER)) {
		SdlWaterPixelConstants water;
		Build_Water_Constants(pixel_constants, water);
		if (fft_water && !Textures[2]) water.TerrainParameters[3]=0.0f;
		water.SceneParameters[0] = water_background ? 1.0f : 0.0f;
		water.SceneParameters[1] = 1.0f / (float)target.Width;
		water.SceneParameters[2] = 1.0f / (float)target.Height;
		water.SceneParameters[3] = water_depth ? 1.0f : 0.0f;
		water.FftParameters[0] = fft_water ? 1.0f : 0.0f;
		if (!water_history) water.FoamHistoryParameters[0] = 0.0f;
		if(pixel_program->UniformBuffers==4) {
			SdlWaterVisualPixelConstants visual;
			Build_Water_Visual_Constants(water,visual);
			draw.PixelConstants=Gpu->Constants(1,&visual,sizeof(visual));
			draw.PixelConstantsSize=sizeof(visual);
			for(unsigned block=0;block<WaterHullVisualLayout::BlockCount;++block) {
				const unsigned first=WaterHullVisualLayout::PacketRegister+WaterHullVisualLayout::BlockOffset(block);
				const unsigned bytes=WaterHullVisualLayout::BlockRegisters(block)*16;
				draw.PixelWaterConstants[block]=Gpu->Constants(1,PixelShaderConstants[first],bytes,block+1);
				draw.PixelWaterConstantsSize[block]=bytes;
			}
		} else {
			draw.PixelConstants = Gpu->Constants(1, &water, sizeof(water));
			draw.PixelConstantsSize = sizeof(water);
		}
		if (fft_water) {
			// The generated water vertex program reads precisely the same per-draw hulls and
			// water time as its pixel program. Alias the recorded bytes so a later object's
			// constants cannot change already queued displacement or its matching normals.
			draw.VertexWaterConstants = draw.PixelConstants;
			draw.VertexWaterConstantsSize = sizeof(SdlWaterPixelConstants);
		}
	}
	else if(pixel_program->UniformBuffers!=0 && shore_region && pixel_program!=legacy_pixel_program) {
		SdlShorePixelConstants shore={};shore.Combiner=pixel_constants;
		memcpy(shore.WorldFromClip,shore_region->view.worldFromClip,sizeof(shore.WorldFromClip));
		memcpy(shore.Viewport,shore_region->view.viewport,sizeof(shore.Viewport));
		memcpy(shore.Terrain,shore_region->terrain,sizeof(shore.Terrain));
		shore.Scene[1]=1.0f/target.Width;shore.Scene[2]=1.0f/target.Height;shore.Scene[3]=shore_overlay?1.0f:0.0f;
		shore.Surface[0]=ShoreTrack.level;shore.Surface[1]=ShoreTrack.level+1.5f;shore.Surface[2]=1;
		draw.PixelConstants=Gpu->Constants(1,&shore,sizeof(shore));draw.PixelConstantsSize=sizeof(shore);
	}
	else if (pixel_program->UniformBuffers != 0
		&& (pixel_engine != ENGINE_SHADER_NONE || combiner.ShadowReceiving || combiner.DrawEffect)) {
		SdlEnginePixelConstants engine_constants;
		memset(&engine_constants, 0, sizeof(engine_constants));
		engine_constants.Combiner = pixel_constants;
		if (combiner.ShadowReceiving) Build_Sun_Constants(engine_constants);
		if (combiner.DrawEffect) engine_constants.DrawEffectParameters[3] = SunCasterOpacity;
		draw.PixelConstants = Gpu->Constants(1, &engine_constants, sizeof(engine_constants));
		draw.PixelConstantsSize = sizeof(engine_constants);
	}
	else if (pixel_program->UniformBuffers != 0) {
		draw.PixelConstants = Gpu->Constants(1, &pixel_constants, sizeof(pixel_constants));
		draw.PixelConstantsSize = sizeof(pixel_constants);
	}

	// D3D9's pixel centres are at integer coordinates, SDL3 GPU's (Metal's, Vulkan's) at half-integers:
	// the viewport moves half a pixel right and down, as dx11backend's Set_Viewport does.  The scissor
	// keeps the draw inside the viewport D3D9 would have clipped it to.
	draw.Viewport[0] = (float)Viewport.X + 0.5f;
	draw.Viewport[1] = (float)Viewport.Y + 0.5f;
	draw.Viewport[2] = (float)Viewport.Width;
	draw.Viewport[3] = (float)Viewport.Height;
	draw.Viewport[4] = Viewport.MinZ;
	draw.Viewport[5] = Viewport.MaxZ;
	// The scissor is the viewport cut to the target: SDL3 GPU validates it against the attachment, and
	// D3D9 draws nothing outside the target either.
	const int32_t right = (int32_t)(Viewport.X + Viewport.Width) < (int32_t)target.Width
		? (int32_t)(Viewport.X + Viewport.Width) : (int32_t)target.Width;
	const int32_t bottom = (int32_t)(Viewport.Y + Viewport.Height) < (int32_t)target.Height
		? (int32_t)(Viewport.Y + Viewport.Height) : (int32_t)target.Height;
	draw.Scissor[0] = (int32_t)Viewport.X;
	draw.Scissor[1] = (int32_t)Viewport.Y;
	draw.Scissor[2] = right > draw.Scissor[0] ? right - draw.Scissor[0] : 0;
	draw.Scissor[3] = bottom > draw.Scissor[1] ? bottom - draw.Scissor[1] : 0;
	if (draw.Scissor[2] == 0 || draw.Scissor[3] == 0) {
		return D3D_OK;		// the viewport is off the target: nothing to draw
	}
	phases.Mark("constants");
	Gpu->Set_Target(target);
	draw.StencilReference = RenderStates[D3DRS_STENCILREF] & 0xFF;
	draw.BlendFactor = RenderStates[D3DRS_BLENDFACTOR];
	Gpu->Record_Draw(draw);
	if (combiner.ShadowReceiving) {
		if (pixel_engine == ENGINE_SHADER_NONE) ++SunSolidReceivers;
		else ++SunGroundReceivers;
		if (Sun_Camera_Space()) ++SunCameraReceivers;
		if (RenderStates[D3DRS_ALPHABLENDENABLE]) ++SunAlphaReceivers;
	}
	if (SunActive && combiner.DrawEffect == DRAW_EFFECT_CLOAK_SHADOW) ++SunCloakCasters;
	// Latch only a genuinely accepted modern flat-water draw with the actual scene snapshot.
	// The ledger lasts one WaterRender call, includes actual targets, and owns texture references.
	if(ShoreActive && ShoreHasPatch && pixel_engine==ENGINE_SHADER_WATER_TRAPEZOID && water_depth
		&& PixelShaderConstants[6][2]>.5f && PixelShaderConstants[6][3]>.5f && PixelShaderConstants[5][3]>.5f
		&& PixelShaderConstants[1][1]<.5f && RenderStates[D3DRS_FILLMODE]==D3DFILL_SOLID && Textures[2] && Textures[4]) {
		ShoreRegion region;region.patch=ShorePatch;
		region.view.colour=reinterpret_cast<std::uintptr_t>(target.Colour);region.view.depth=reinterpret_cast<std::uintptr_t>(target.Depth);
		region.view.width=target.Width;region.view.height=target.Height;
		if(Gpu->Ready_Scene_Depth(target,region.view.capture)==water_depth) {
			SdlWaterPixelConstants water;Build_Water_Constants(pixel_constants,water);
			memcpy(region.view.worldFromClip,water.WorldFromClip,sizeof(region.view.worldFromClip));
			memcpy(region.view.viewport,water.Viewport,sizeof(region.view.viewport));
			memcpy(region.terrain,water.TerrainParameters,sizeof(region.terrain));
			bool found=false;
			for(const ShoreRegion &old:ShoreRegions) if(WaterShoreOverlay::SameView(old.view,region.view)
				&& old.patch.level==region.patch.level && memcmp(old.patch.xy,region.patch.xy,sizeof(old.patch.xy))==0) {found=true;break;}
			if(!found && ShoreRegions.size()<WaterShoreOverlay::MaxRegions) {
				Textures[2]->AddRef();Textures[4]->AddRef();
				region.height=std::shared_ptr<IDirect3DBaseTexture9>(Textures[2],[](IDirect3DBaseTexture9 *t){t->Release();});
				region.optical=std::shared_ptr<IDirect3DBaseTexture9>(Textures[4],[](IDirect3DBaseTexture9 *t){t->Release();});
				ShoreRegions.push_back(region);
			}
		}
	}
	++DrawsRecorded;
	if (vertex_engine != ENGINE_SHADER_NONE || pixel_engine != ENGINE_SHADER_NONE) {
		++EngineProgramDraws[EngineShader_Name((EngineShaderProgram)(vertex_engine != ENGINE_SHADER_NONE
			? vertex_engine : pixel_engine))];
	}
	return D3D_OK;
}
