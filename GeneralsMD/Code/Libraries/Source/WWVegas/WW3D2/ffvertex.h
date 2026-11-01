/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

/*
** The fixed-function vertex pipeline, written out as HLSL.
**
** ffshader.h does this for the pixel half.  This is the other half and it is the larger one: the
** transform, the vertex lighting, the texture coordinate generation and the fog factor all live in
** the fixed-function vertex pipeline, and D3D11 has none of it.
**
** What the game actually asks for was counted rather than assumed, the same way the combiners
** were.  Three light types, two material sources, four coordinate generation modes and texture
** transforms with a count and an optional projection.  Everything outside that list is refused
** rather than approximated, and a refusal is visible - the draw stays on the pipeline it was on -
** where a wrong approximation is a picture nobody can explain.
**
** It generates for both profiles on purpose.  Asked for D3D9 it writes a vs_2_0-shaped shader with
** float4 constant registers and a POSITION output, which can be bound on the existing device and
** compared against the fixed-function pipeline it replaces, one frame each.  Asked for D3D11 it
** writes the same body against a constant buffer with an SV_Position output.  The comparison is
** the whole reason phase 2 is being done in this order.
*/

#ifndef FFVERTEX_H
#define FFVERTEX_H

#include "ffstate.h"

#include <string>

// The engine's widest vertex format carries four coordinate sets and the water uses all four
// stages: the river texture, the sparkles, the noise generated from the camera space position, and
// the shroud.  Everything else in the game sets two.  A fifth is a refusal.
const unsigned MAXIMUM_VERTEX_STAGES = 4;

// D3D9 allows eight simultaneous lights.  The scenes measured never light a draw with more than
// three, and every register past this one is a register the D3D9 profile does not have to spare.
const unsigned MAXIMUM_VERTEX_LIGHTS = 4;

// Where each constant sits in the D3D9 profile's register file, which is what
// SetVertexShaderConstantF is given.  The D3D11 profile puts the same values in one constant buffer
// in this order instead, so the two halves of the generator share the layout and nothing else.
// Public because anything driving the D3D9 program from outside has to fill the same registers, and
// a layout agreed by two files that cannot see each other is a layout that drifts.
const unsigned VERTEX_REGISTERS_PER_MATRIX = 4;
const unsigned VERTEX_REGISTER_WORLD_VIEW_PROJECTION = 0;
const unsigned VERTEX_REGISTER_WORLD_VIEW = 4;
const unsigned VERTEX_REGISTER_NORMAL_TRANSFORM = 8;
const unsigned VERTEX_REGISTER_TEXTURE_MATRICES = 12;
const unsigned VERTEX_REGISTER_MATERIAL_AMBIENT =
	VERTEX_REGISTER_TEXTURE_MATRICES + MAXIMUM_VERTEX_STAGES * VERTEX_REGISTERS_PER_MATRIX;
const unsigned VERTEX_REGISTER_MATERIAL_DIFFUSE = VERTEX_REGISTER_MATERIAL_AMBIENT + 1;
const unsigned VERTEX_REGISTER_MATERIAL_SPECULAR = VERTEX_REGISTER_MATERIAL_AMBIENT + 2;
const unsigned VERTEX_REGISTER_MATERIAL_EMISSIVE = VERTEX_REGISTER_MATERIAL_AMBIENT + 3;
const unsigned VERTEX_REGISTER_MATERIAL_POWER = VERTEX_REGISTER_MATERIAL_AMBIENT + 4;
const unsigned VERTEX_REGISTER_GLOBAL_AMBIENT = VERTEX_REGISTER_MATERIAL_AMBIENT + 5;
const unsigned VERTEX_REGISTER_FOG_PARAMETERS = VERTEX_REGISTER_MATERIAL_AMBIENT + 6;
const unsigned VERTEX_REGISTER_VIEWPORT = VERTEX_REGISTER_MATERIAL_AMBIENT + 7;
const unsigned VERTEX_REGISTER_LIGHTS = VERTEX_REGISTER_MATERIAL_AMBIENT + 8;
const unsigned VERTEX_REGISTERS_PER_LIGHT = 7;

struct VertexLightDescription
{
	// D3DLIGHT_DIRECTIONAL, D3DLIGHT_POINT or D3DLIGHT_SPOT.
	FixedFunctionValue Type;
};

struct VertexStageDescription
{
	// D3DTSS_TEXCOORDINDEX whole: the generation mode in the high half, the coordinate set in the
	// low one.  Unlike the pixel half, both matter here - generating the coordinate is this
	// shader's job now.
	FixedFunctionValue TextureCoordinateIndex;

	// D3DTSS_TEXTURETRANSFORMFLAGS: a count of how many coordinates the texture matrix produces,
	// optionally with D3DTTFF_PROJECTED.  D3DTTFF_DISABLE means the matrix is not applied at all.
	FixedFunctionValue TextureTransformFlags;
};

struct VertexPipelineDescription
{
	// The flexible vertex format the draw is reading, which says whether there is a normal and
	// whether there is a vertex colour to read D3DMCS_COLOR out of.
	FixedFunctionValue FVF;

	bool LightingEnabled;
	bool SpecularEnabled;

	// D3DRS_LOCALVIEWER, whose Direct3D 9 default is TRUE and which the engine never turns off: the
	// specular halfway vector is taken towards the vertex's own direction to the eye rather than the
	// fixed (0, 0, 1).  Only a lit, specular program reads it.  Initialised here for a caller that fills
	// the rest field by field.
	bool LocalViewer = false;

	// D3DRS_COLORVERTEX.  With it off the vertex colour is ignored whatever the material sources
	// say, which is how a lit draw with a colour in its vertices still comes out unlit by it.
	bool ColourVertexEnabled;

	// D3DMCS_MATERIAL, D3DMCS_COLOR1 or D3DMCS_COLOR2, one each: the material's colour, or the
	// vertex's diffuse or specular one where the format has it and COLORVERTEX is on.
	FixedFunctionValue DiffuseMaterialSource;
	FixedFunctionValue AmbientMaterialSource;
	FixedFunctionValue EmissiveMaterialSource;
	FixedFunctionValue SpecularMaterialSource;

	unsigned LightCount;
	VertexLightDescription Lights[MAXIMUM_VERTEX_LIGHTS];

	unsigned StageCount;
	VertexStageDescription Stages[MAXIMUM_VERTEX_STAGES];

	bool FogEnabled;

	// D3DRS_FOGVERTEXMODE: D3DFOG_LINEAR, D3DFOG_EXP or D3DFOG_EXP2.  D3DFOG_NONE with the fog
	// enabled is table fog, which is the pixel half's business and not this one's.
	FixedFunctionValue FogVertexMode;

	// An unlit draw whose normal is not a normal: the sorted smoke billboards put the glow of the
	// fires near them there, and the program hands it on in the specular colour, which an unlit draw
	// with no second colour in its vertices otherwise leaves black.  The pixel half adds it after the
	// shade (CombinerDescription::SmokeGlow).  Refused on a lit draw or a format with no normal.
	// Initialised here for a caller that fills the rest field by field.
	bool SmokeGlow = false;

	// A sorted particle billboard that fades into the scene's depth: the sorting pool writes the
	// sprite's half width into the first of the second coordinate set (sortingrenderer.cpp,
	// Copy_Batch_Vertices), and the program hands it on in the specular colour's alpha, which the
	// pixel half's fade reads (SOFT_PARTICLE_SAMPLING).  Refused on a format with one set or none.
	bool SoftParticle = false;

	// A fire or explosion billboard (CombinerDescription::FireLook): the second coordinate set
	// carries the particle's age and seed in v, and the program hands the whole set on in
	// TexCoord1, which a one-stage draw leaves empty.  Refused on a format with one set or none, a
	// pretransformed one, or a draw with a second stage, whose coordinates that output is.
	bool FireLook = false;

	// A lit draw under the Hemispheric Ambient option: the scene ambient comes down from the sky and
	// up from the ground instead of evenly.  The world's up in camera space rides in MaterialPower.yzw,
	// which nothing else reads, so every other program keeps its text and the block keeps its layout.
	// Refused on an unlit or a pretransformed draw.
	bool HemiAmbient = false;
};

// Which profile the generated text is for.  The two differ in the output semantic and in how the
// constants are declared; the arithmetic between them is the same text.  SDL3_GPU is the D3D11 text
// with its bindings rewritten for SDL3's GPU API (sdl3target.h), for the Metal and Vulkan backend.
enum VertexShaderTarget
{
	VERTEX_SHADER_TARGET_D3D9,
	VERTEX_SHADER_TARGET_D3D11,
	VERTEX_SHADER_TARGET_SDL3_GPU
};

bool VertexShader_Generate(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl);

// Two descriptions with the same key generate the same text, so the key is what a cache is built
// on.  It carries no matrix, no colour and no light direction: those are constants the draw
// uploads, not shapes of the program.
std::string VertexShader_Key(const VertexPipelineDescription & description);

#endif // FFVERTEX_H
