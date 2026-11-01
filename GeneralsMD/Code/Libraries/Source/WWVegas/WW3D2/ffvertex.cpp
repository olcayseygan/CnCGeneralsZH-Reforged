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

#include "ffvertex.h"
#include "sdl3target.h"

#include <stdio.h>

// The high half of D3DTSS_TEXCOORDINDEX is the generation mode and the low half the coordinate set.
static const FixedFunctionValue COORDINATE_SET_MASK = 0xffff;

// D3DTTFF_PROJECTED sits above the count, which is the low three bits.
static const FixedFunctionValue TEXTURE_TRANSFORM_COUNT_MASK = 0x07;

// The constant registers the D3D9 profile uses, in the order they are declared.  A vs_2_0 shader
// has 256 float4 registers and this uses fewer than 70 of them, so the layout is written for
// reading rather than for packing.  Everything after the texture matrices is derived from where
// they end, because how many there are follows MAXIMUM_VERTEX_STAGES.
// The numbers themselves are in ffvertex.h, because anything driving the D3D9 program from outside
// has to fill the same registers.  These are the short names this file reads them by.
//
// The viewport's reciprocal sits before the lights because the D3D11 block declares only as many
// lights as the description has, and anything after them would move with that count.  Seven
// registers a light, the same seven whatever type it is: where it is, which way it points, its diffuse
// and specular colours, its three attenuation terms with its range, its cone, and its ambient colour.
// Every light reads its ambient, a spot light reads all seven, and the layout stays uniform so the
// register a light starts at is a multiplication rather than a running total over types.
static const unsigned REGISTERS_PER_MATRIX = VERTEX_REGISTERS_PER_MATRIX;
static const unsigned REGISTER_WORLD_VIEW_PROJECTION = VERTEX_REGISTER_WORLD_VIEW_PROJECTION;
static const unsigned REGISTER_WORLD_VIEW = VERTEX_REGISTER_WORLD_VIEW;
static const unsigned REGISTER_NORMAL_TRANSFORM = VERTEX_REGISTER_NORMAL_TRANSFORM;
static const unsigned REGISTER_TEXTURE_MATRICES = VERTEX_REGISTER_TEXTURE_MATRICES;
static const unsigned REGISTER_MATERIAL_AMBIENT = VERTEX_REGISTER_MATERIAL_AMBIENT;
static const unsigned REGISTER_MATERIAL_DIFFUSE = VERTEX_REGISTER_MATERIAL_DIFFUSE;
static const unsigned REGISTER_MATERIAL_SPECULAR = VERTEX_REGISTER_MATERIAL_SPECULAR;
static const unsigned REGISTER_MATERIAL_EMISSIVE = VERTEX_REGISTER_MATERIAL_EMISSIVE;
static const unsigned REGISTER_MATERIAL_POWER = VERTEX_REGISTER_MATERIAL_POWER;
static const unsigned REGISTER_GLOBAL_AMBIENT = VERTEX_REGISTER_GLOBAL_AMBIENT;
static const unsigned REGISTER_FOG_PARAMETERS = VERTEX_REGISTER_FOG_PARAMETERS;
static const unsigned REGISTER_VIEWPORT = VERTEX_REGISTER_VIEWPORT;
static const unsigned REGISTER_LIGHTS = VERTEX_REGISTER_LIGHTS;
static const unsigned REGISTERS_PER_LIGHT = VERTEX_REGISTERS_PER_LIGHT;

// How far the scene ambient swings per channel between a face looking straight up (one plus this) and
// one looking straight down (one minus it).  A wall keeps exactly the map's ambient, so a night map
// stays a night map and the model's overall weight stays where the art had it.  Blue swings most: the
// sky is bluer than the ambient and the ground's bounce warmer.
static const char * const HEMI_AMBIENT_SPREAD = "float3(0.25, 0.30, 0.40)";

static bool has_normal(FixedFunctionValue fvf)
{
	return (fvf & FF_FVF_NORMAL) != 0;
}

static bool has_diffuse(FixedFunctionValue fvf)
{
	return (fvf & FF_FVF_DIFFUSE) != 0;
}

static bool has_specular(FixedFunctionValue fvf)
{
	return (fvf & FF_FVF_SPECULAR) != 0;
}

static bool is_pretransformed(FixedFunctionValue fvf)
{
	return (fvf & FF_FVF_POSITION_MASK) == FF_FVF_XYZRHW;
}

static unsigned texture_coordinate_set_count(FixedFunctionValue fvf)
{
	return (fvf & FF_FVF_TEXCOUNT_MASK) >> FF_FVF_TEXCOUNT_SHIFT;
}

// A material source resolves to a constant or to a vertex colour.  D3DMCS_COLOR1 reads the diffuse
// vertex colour, and only where the format carries one and D3DRS_COLORVERTEX allows it: D3D9 falls
// back to the material in both of those cases rather than reading a register that is not there.
// D3DMCS_COLOR2 is the specular vertex colour, which no format in the game carries, so a
// description asking for it is refused rather than quietly given the material instead.
static bool material_source_expression(FixedFunctionValue source, const char * material_constant,
	const VertexPipelineDescription & description, std::string & expression)
{
	switch (source) {
	case FF_MCS_MATERIAL:
		expression = material_constant;
		return true;

	case FF_MCS_COLOR1:
		if (has_diffuse(description.FVF) && description.ColourVertexEnabled) {
			expression = "input.Diffuse";
		}
		else {
			expression = material_constant;
		}
		return true;

	case FF_MCS_COLOR2:
		// The vertex's specular colour, where it has one and COLORVERTEX is on; the material's
		// otherwise, as D3DMCS_COLOR1 does with the diffuse ("D3DMATERIALCOLORSOURCE").
		if (has_specular(description.FVF) && description.ColourVertexEnabled) {
			expression = "input.Specular";
		}
		else {
			expression = material_constant;
		}
		return true;

	default:
		return false;
	}
}

static void append_light(std::string & body, unsigned index, FixedFunctionValue type, const char * accumulator,
	bool local_viewer)
{
	char line[1024];

	snprintf(line, sizeof(line),
		"    {\n"
		"        float3 to_light;\n"
		"        float attenuation;\n");
	body += line;

	if (type == FF_LIGHT_DIRECTIONAL) {
		// A directional light's direction is the way the light travels, so the vector towards it
		// is the negative of it and there is nothing to attenuate.
		snprintf(line, sizeof(line),
			"        to_light = -normalize(Light%uDirection.xyz);\n"
			"        attenuation = 1.0;\n", index);
		body += line;
	}
	else {
		// A point light and a spot light share the distance and the attenuation; the spot's cone
		// is the extra term below.
		snprintf(line, sizeof(line),
			"        float3 offset = Light%uPosition.xyz - view_position.xyz;\n"
			"        float distance = length(offset);\n"
			"        to_light = offset / max(distance, 0.0001);\n"
			"        attenuation = 1.0 / max(Light%uAttenuation.x + Light%uAttenuation.y * distance"
			" + Light%uAttenuation.z * distance * distance, 0.0001);\n"
			"        attenuation *= step(distance, Light%uAttenuation.w);\n",
			index, index, index, index, index);
		body += line;
	}

	if (type == FF_LIGHT_SPOT) {
		// D3D9's cone is a smooth falloff between the inner and the outer cosine raised to the
		// falloff power.  Light%uSpot carries cos(theta/2), cos(phi/2) and the falloff.
		snprintf(line, sizeof(line),
			"        float spot_cosine = dot(-to_light, normalize(Light%uDirection.xyz));\n"
			"        attenuation *= pow(saturate((spot_cosine - Light%uSpot.y)"
			" / max(Light%uSpot.x - Light%uSpot.y, 0.0001)), Light%uSpot.z);\n",
			index, index, index, index, index);
		body += line;
	}

	// The light's own ambient, attenuated and coned like the rest of it ("Ambient Lighting": the
	// ambient sum is Atten * Spot * La over the lights).  W3D gives point lights one: the light
	// environment's (dx8wrapper.cpp, getPointAmbient) and the dynamic lights W3DDisplay makes.
	snprintf(line, sizeof(line), "        ambient_light += Light%uAmbient.rgb * attenuation;\n", index);
	body += line;

	snprintf(line, sizeof(line),
		"        float lambert = max(dot(view_normal, to_light), 0.0);\n"
		"        %s += Light%uDiffuse.rgb * lambert * attenuation;\n",
		accumulator, index);
	body += line;

	// The specular term is Blinn's half vector ("Specular Lighting"): between the light and the eye,
	// where the eye is the vertex's own direction to the camera with D3DRS_LOCALVIEWER, which is D3D9's
	// default and which the engine never turns off, and the fixed (0, 0, -1) without it.  The page says
	// (0, 0, 1); Windows' own D3D9 (WARP and REF, Tests/ffreference/knownprobe_windows.cpp, N27) draws with (0, 0, -1), which is
	// also where the local viewer's direction points for a vertex straight ahead.
	body += local_viewer
		? "        float3 half_vector = normalize(to_light + normalize(-view_position.xyz));\n"
		: "        float3 half_vector = normalize(to_light + float3(0.0, 0.0, -1.0));\n";
	snprintf(line, sizeof(line),
		"        float highlight = pow(max(dot(view_normal, half_vector), 0.0), MaterialPower.x);\n"
		"        specular_light += Light%uSpecular.rgb * highlight * attenuation"
		" * step(0.0001, lambert);\n"
		"    }\n", index);
	body += line;
}

static bool append_texture_coordinates(std::string & body,
	const VertexPipelineDescription & description)
{
	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		const VertexStageDescription & source = description.Stages[stage];
		const FixedFunctionValue generation = source.TextureCoordinateIndex & ~COORDINATE_SET_MASK;
		const unsigned set = source.TextureCoordinateIndex & COORDINATE_SET_MASK;

		char line[1024];
		switch (generation) {
		case FF_TSS_TCI_PASSTHRU:
			if (set >= texture_coordinate_set_count(description.FVF)) {
				// The format does not carry the set the stage is asking for, which the engine does
				// on purpose: a shadow quad has a texture stage on and no coordinates in its
				// vertices.  D3D9 reads zeroes there, so this does too.  Refusing instead left 202
				// draws a match out of the picture.
				snprintf(line, sizeof(line),
					"    float4 generated%u = float4(0.0, 0.0, 0.0, 1.0);\n", stage);
			}
			else if ((source.TextureTransformFlags & TEXTURE_TRANSFORM_COUNT_MASK) != FF_TTFF_DISABLE) {
				// Through a texture transform D3D9 pads a two-element set as (u, v, 1, 0), so the
				// matrix's third row is the one that translates it ("Texture Coordinate Formats"), and
				// W3D's scrolling mappers put their scroll there (mapper.cpp: "According to the docs
				// this should work since its 2D").  Padded (u, v, 0, 1), the scroll read the fourth
				// row, which is zero, and every scrolling texture stood still.
				snprintf(line, sizeof(line),
					"    float4 generated%u = float4(input.TexCoord%u, 1.0, 0.0);\n", stage, set);
			}
			else {
				snprintf(line, sizeof(line),
					"    float4 generated%u = float4(input.TexCoord%u, 0.0, 1.0);\n", stage, set);
			}
			break;

		case FF_TSS_TCI_CAMERASPACEPOSITION:
			snprintf(line, sizeof(line), "    float4 generated%u = float4(view_position.xyz, 1.0);\n",
				stage);
			break;

		case FF_TSS_TCI_CAMERASPACENORMAL:
			snprintf(line, sizeof(line), "    float4 generated%u = float4(view_normal, 1.0);\n",
				stage);
			break;

		case FF_TSS_TCI_CAMERASPACEREFLECTIONVECTOR:
			// R = 2(N.E)N - E, E the unit direction to the eye: the vertex's own with D3DRS_LOCALVIEWER,
			// and the fixed (0, 0, -1) without it, as Windows' D3D9 draws it (knownprobe_windows.cpp, N14).
			// reflect(I, N) is I - 2(N.I)N, so I is -E: the camera-space position, or (0, 0, 1).
			snprintf(line, sizeof(line), description.LocalViewer
				? "    float4 generated%u = float4(reflect(normalize(view_position.xyz), view_normal), 1.0);\n"
				: "    float4 generated%u = float4(reflect(float3(0.0, 0.0, 1.0), view_normal), 1.0);\n",
				stage);
			break;

		default:
			return false;
		}
		body += line;

		const FixedFunctionValue transform_count = source.TextureTransformFlags & TEXTURE_TRANSFORM_COUNT_MASK;
		if (transform_count != FF_TTFF_DISABLE) {
			snprintf(line, sizeof(line), "    generated%u = mul(generated%u, TextureMatrix%u);\n",
				stage, stage, stage);
			body += line;

			if ((source.TextureTransformFlags & FF_TTFF_PROJECTED) != 0) {
				// A projected transform divides by the last coordinate the count named, which for
				// every projected stage the game sets is the third.
				snprintf(line, sizeof(line),
					"    generated%u.xy /= max(generated%u.z, 0.0001);\n", stage, stage);
				body += line;
			}
		}

		snprintf(line, sizeof(line), "    output.TexCoord%u = generated%u.xy;\n", stage, stage);
		body += line;
	}
	return true;
}

static void append_constants_d3d9(std::string & hlsl, const VertexPipelineDescription & description)
{
	// Long enough for the widest block written below.  A short buffer here truncates the last
	// declaration rather than failing, and the compiler then reports a syntax error on the line
	// after it, which says nothing about where the fault is.
	char line[1024];
	// row_major for the reason the D3D11 block gives: a D3D9 matrix is stored by rows and HLSL
	// assumes columns.
	snprintf(line, sizeof(line),
		"row_major float4x4 WorldViewProjection : register(c%u);\n"
		"row_major float4x4 WorldView : register(c%u);\n"
		"row_major float4x4 NormalTransform : register(c%u);\n",
		REGISTER_WORLD_VIEW_PROJECTION, REGISTER_WORLD_VIEW, REGISTER_NORMAL_TRANSFORM);
	hlsl += line;

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		snprintf(line, sizeof(line), "row_major float4x4 TextureMatrix%u : register(c%u);\n",
			stage, REGISTER_TEXTURE_MATRICES + stage * REGISTERS_PER_MATRIX);
		hlsl += line;
	}

	snprintf(line, sizeof(line),
		"float4 MaterialAmbient : register(c%u);\n"
		"float4 MaterialDiffuse : register(c%u);\n"
		"float4 MaterialSpecular : register(c%u);\n"
		"float4 MaterialEmissive : register(c%u);\n"
		"float4 MaterialPower : register(c%u);\n"
		"float4 GlobalAmbient : register(c%u);\n"
		"float4 FogParameters : register(c%u);\n"
		"float4 ViewportInverse : register(c%u);\n",
		REGISTER_MATERIAL_AMBIENT, REGISTER_MATERIAL_DIFFUSE, REGISTER_MATERIAL_SPECULAR,
		REGISTER_MATERIAL_EMISSIVE, REGISTER_MATERIAL_POWER, REGISTER_GLOBAL_AMBIENT,
		REGISTER_FOG_PARAMETERS, REGISTER_VIEWPORT);
	hlsl += line;

	for (unsigned index = 0; index < description.LightCount; ++index) {
		const unsigned base = REGISTER_LIGHTS + index * REGISTERS_PER_LIGHT;
		snprintf(line, sizeof(line),
			"float4 Light%uPosition : register(c%u);\n"
			"float4 Light%uDirection : register(c%u);\n"
			"float4 Light%uDiffuse : register(c%u);\n"
			"float4 Light%uSpecular : register(c%u);\n"
			"float4 Light%uAttenuation : register(c%u);\n"
			"float4 Light%uSpot : register(c%u);\n"
			"float4 Light%uAmbient : register(c%u);\n",
			index, base, index, base + 1, index, base + 2,
			index, base + 3, index, base + 4, index, base + 5, index, base + 6);
		hlsl += line;
	}
}

static void append_constants_d3d11(std::string & hlsl,
	const VertexPipelineDescription & description)
{
	// row_major, because every matrix the engine has is a D3D9 matrix and D3D9 stores a matrix by
	// rows.  HLSL's default is by columns, so without this each one arrives transposed and the
	// whole world is drawn through the wrong basis - which is not subtly wrong, it is a screen with
	// nothing recognisable on it.
	hlsl +=
		"cbuffer VertexPipeline : register(b0)\n"
		"{\n"
		"    row_major float4x4 WorldViewProjection;\n"
		"    row_major float4x4 WorldView;\n"
		"    row_major float4x4 NormalTransform;\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char matrix[64];
		snprintf(matrix, sizeof(matrix), "    row_major float4x4 TextureMatrix%u;\n", stage);
		hlsl += matrix;
	}

	hlsl +=
		"    float4 MaterialAmbient;\n"
		"    float4 MaterialDiffuse;\n"
		"    float4 MaterialSpecular;\n"
		"    float4 MaterialEmissive;\n"
		"    float4 MaterialPower;\n"
		"    float4 GlobalAmbient;\n"
		"    float4 FogParameters;\n"
		"    float4 ViewportInverse;\n";

	for (unsigned index = 0; index < description.LightCount; ++index) {
		char line[1024];
		snprintf(line, sizeof(line),
			"    float4 Light%uPosition;\n"
			"    float4 Light%uDirection;\n"
			"    float4 Light%uDiffuse;\n"
			"    float4 Light%uSpecular;\n"
			"    float4 Light%uAttenuation;\n"
			"    float4 Light%uSpot;\n"
			"    float4 Light%uAmbient;\n",
			index, index, index, index, index, index, index);
		hlsl += line;
	}
	hlsl += "};\n";
}

// The coordinate sets the vertex format actually carries.  Direct3D 9 let a shader declare an
// input the format did not supply; D3D11 refuses the input layout for it.
static void append_input_coordinate_sets(std::string & hlsl, unsigned coordinate_sets)
{
	for (unsigned set = 0; set < coordinate_sets && set < MAXIMUM_VERTEX_STAGES; ++set) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", set, set);
		hlsl += line;
	}
}

// What every generated vertex program writes, whatever it was generated from.  Shader model 4
// links the stages by slot in declaration order, so this structure and ffshader's input structure
// are one thing in two files: a program that writes a different set links against nothing.
static void append_output_structure(std::string & hlsl, bool for_d3d11)
{
	hlsl +=
		"struct Output\n"
		"{\n";
	hlsl += for_d3d11 ? "    float4 Position : SV_Position;\n" : "    float4 Position : POSITION;\n";
	hlsl +=
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}

	hlsl += "    float Fog : FOG;\n";
	hlsl +=
		"};\n"
		"\n";
}

// The whole program for a pre-transformed vertex.  D3D9 takes x and y as pixels inside the
// viewport, z as the depth it will write and the fourth float as the reciprocal of w; clip space
// wants the opposite of all four, so the position is mapped back and multiplied by w, which is what
// keeps the texture coordinates perspective correct on the rare quad whose w is not one.
static bool generate_pretransformed(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl)
{
	const bool for_d3d11 = (target == VERTEX_SHADER_TARGET_D3D11);
	const unsigned coordinate_sets = texture_coordinate_set_count(description.FVF);

	hlsl = "// Generated from a fixed-function vertex pipeline description.  See ffvertex.h.\n";
	if (for_d3d11) {
		append_constants_d3d11(hlsl, description);
	}
	else {
		append_constants_d3d9(hlsl, description);
	}

	hlsl +=
		"\n"
		"struct Input\n"
		"{\n"
		"    float4 Position : POSITION;\n";
	if (has_diffuse(description.FVF)) {
		hlsl += "    float4 Diffuse  : COLOR0;\n";
	}
	if (has_specular(description.FVF)) {
		hlsl += "    float4 Specular : COLOR1;\n";
	}
	append_input_coordinate_sets(hlsl, coordinate_sets);
	hlsl +=
		"};\n"
		"\n";
	append_output_structure(hlsl, for_d3d11);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n"
		"    float reciprocal_w = (input.Position.w == 0.0) ? 1.0 : input.Position.w;\n"
		"    float w = 1.0 / reciprocal_w;\n"
		// Straight into clip space with no half-pixel of its own.  D3D9 and D3D11 disagree about
		// where a pixel's centre is and every screen space quad in this engine carries D3D9's own
		// -0.5 for it, but the correction belongs to the whole frame rather than to this path:
		// DX11BackendClass::Set_Viewport shifts the viewport half a pixel and every draw comes out
		// where Direct3D 9 puts it, the transcribed .vso programs included.  Adding a half here as
		// well was tried and measured - Alpine Assault went from 5.99% to 9.51% with both.
		"    float2 normalised = input.Position.xy * ViewportInverse.xy;\n"
		"    output.Position = float4((normalised.x * 2.0 - 1.0) * w,\n"
		"        (1.0 - normalised.y * 2.0) * w, input.Position.z * w, w);\n";

	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		const unsigned set = (stage < description.StageCount)
			? (description.Stages[stage].TextureCoordinateIndex & COORDINATE_SET_MASK)
			: 0;
		char line[128];
		if (stage < description.StageCount && set < coordinate_sets) {
			snprintf(line, sizeof(line), "    output.TexCoord%u = input.TexCoord%u;\n", stage, set);
		}
		else {
			snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		}
		hlsl += line;
	}

	if (has_diffuse(description.FVF)) {
		hlsl += "    output.Diffuse = input.Diffuse;\n";
	}
	else {
		hlsl += "    output.Diffuse = float4(1.0, 1.0, 1.0, 1.0);\n";
	}

	// An unlit vertex's specular colour is its own, which the pixel adds with D3DRS_SPECULARENABLE.
	hlsl += has_specular(description.FVF)
		? "    output.Specular = input.Specular;\n"
		: "    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n";
	hlsl +=
		"    output.Fog = 1.0;\n"
		"    return output;\n"
		"}\n";
	return true;
}

bool VertexShader_Generate(const VertexPipelineDescription & description,
	VertexShaderTarget target, std::string & hlsl)
{
	// The SDL3 GPU program is the D3D11 one with its bindings rewritten, so everything below only
	// ever sees the two profiles it was written for.
	if (target == VERTEX_SHADER_TARGET_SDL3_GPU) {
		return VertexShader_Generate(description, VERTEX_SHADER_TARGET_D3D11, hlsl)
			&& SDL3_Shader_Retarget(hlsl, true);
	}
	if (description.StageCount > MAXIMUM_VERTEX_STAGES
		|| description.LightCount > MAXIMUM_VERTEX_LIGHTS) {
		return false;
	}

	// Lighting needs a normal to light.  D3D9 lights a vertex with no normal as though the normal
	// were zero, which is black, and no draw in the game asks for that.
	if (description.LightingEnabled && !has_normal(description.FVF)) {
		return false;
	}
	if (description.SmokeGlow
		&& (description.LightingEnabled || !has_normal(description.FVF) || is_pretransformed(description.FVF))) {
		return false;
	}
	if (description.SoftParticle
		&& (texture_coordinate_set_count(description.FVF) < 2 || is_pretransformed(description.FVF))) {
		return false;
	}
	if (description.FireLook
		&& (texture_coordinate_set_count(description.FVF) < 2 || is_pretransformed(description.FVF)
			|| description.StageCount > 1)) {
		return false;
	}
	if (description.HemiAmbient && (!description.LightingEnabled || is_pretransformed(description.FVF))) {
		return false;
	}

	// A pre-transformed vertex has been through the transform, the lighting and the coordinate
	// generation already: its position is in pixels, its colour is in the vertex, and D3D9 reads
	// the texture coordinates straight out of the format whatever the stage's generation mode
	// says.  All the shader has to do is put the pixels back into clip space, so it is written
	// here whole rather than sharing the body below.
	if (is_pretransformed(description.FVF)) {
		return generate_pretransformed(description, target, hlsl);
	}

	for (unsigned index = 0; index < description.LightCount; ++index) {
		const FixedFunctionValue type = description.Lights[index].Type;
		if (type != FF_LIGHT_DIRECTIONAL && type != FF_LIGHT_POINT && type != FF_LIGHT_SPOT) {
			return false;
		}
	}

	std::string body;
	body +=
		"    float4 model_position = float4(input.Position, 1.0);\n"
		"    float4 view_position = mul(model_position, WorldView);\n";

	if (has_normal(description.FVF)) {
		body += "    float3 view_normal = normalize(mul(float4(input.Normal, 0.0), NormalTransform).xyz);\n";
	}
	else {
		// Nothing reads it, but the coordinate generation modes name it and a shader that declares
		// it unconditionally is one branch fewer here.  The compiler drops it when it is unused.
		body += "    float3 view_normal = float3(0.0, 0.0, 1.0);\n";
	}

	if (!append_texture_coordinates(body, description)) {
		return false;
	}

	if (description.LightingEnabled) {
		// D3D9's vertex colour is
		//     sum(atten * spot * Ldiffuse * Cdiffuse * N.L)
		//   + Cambient * (Gambient + sum(atten * spot * Lambient))
		//   + Cemissive
		// so the scene ambient is spent once, through the ambient material.  Seeding this sum with
		// it instead spends it twice - once scaled by the diffuse material and once by the ambient
		// one - and every lit model comes out too bright.  It reads as a lighting mood rather than
		// as a bug, which is why it survived six maps of zero-refusal runs: Alpine Assault at dusk
		// was 13.2% from the Direct3D 9 frame and Golden Oasis in daylight 2.57%, and the geometry
		// was in the right place in both.
		//
		// Each light's own ambient is in that second sum (ambient_light, summed in append_light).  It
		// was left out once on the belief that W3D's lights all have a black one; its point lights do
		// not (dx8wrapper.cpp's light environment, W3DDisplay's dynamic lights).
		// A point or spot light is summed on its own and folded in after the loop.  The split served
		// the normal mapped path, which is gone; it stays because the text is what the shader cache
		// is keyed by.
		body += "    float3 diffuse_light = float3(0.0, 0.0, 0.0);\n";
		body += "    float3 local_light = float3(0.0, 0.0, 0.0);\n";
		body += "    float3 specular_light = float3(0.0, 0.0, 0.0);\n";
		body += "    float3 ambient_light = float3(0.0, 0.0, 0.0);\n";
		for (unsigned index = 0; index < description.LightCount; ++index) {
			const FixedFunctionValue type = description.Lights[index].Type;
			append_light(body, index, type, type == FF_LIGHT_DIRECTIONAL ? "diffuse_light" : "local_light",
				description.LocalViewer);
		}
		body += "    diffuse_light += local_light;\n";

		std::string diffuse;
		std::string ambient;
		std::string emissive;
		if (!material_source_expression(description.DiffuseMaterialSource, "MaterialDiffuse",
				description, diffuse)
			|| !material_source_expression(description.AmbientMaterialSource, "MaterialAmbient",
				description, ambient)
			|| !material_source_expression(description.EmissiveMaterialSource, "MaterialEmissive",
				description, emissive)) {
			return false;
		}

		std::string scene_ambient = "GlobalAmbient.rgb";
		if (description.HemiAmbient) {
			body += std::string("    float3 hemi_ambient = GlobalAmbient.rgb * (1.0 + ") + HEMI_AMBIENT_SPREAD
				+ " * dot(view_normal, MaterialPower.yzw));\n";
			scene_ambient = "hemi_ambient";
		}

		body += "    output.Diffuse.rgb = saturate(" + diffuse + ".rgb * diffuse_light + "
			+ ambient + ".rgb * (" + scene_ambient + " + ambient_light) + " + emissive + ".rgb);\n";
		body += "    output.Diffuse.a = " + diffuse + ".a;\n";

		if (description.SpecularEnabled) {
			std::string specular;
			if (!material_source_expression(description.SpecularMaterialSource, "MaterialSpecular",
					description, specular)) {
				return false;
			}
			body += "    output.Specular = float4(saturate(" + specular
				+ ".rgb * specular_light), 0.0);\n";
		}
		else {
			body += "    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n";
		}
	}
	else {
		// With D3DRS_LIGHTING off the diffuse colour is the vertex colour where there is one and
		// white where there is not, which is what an unlit draw with no colour comes out as.
		if (has_diffuse(description.FVF)) {
			body += "    output.Diffuse = input.Diffuse;\n";
		}
		else {
			body += "    output.Diffuse = float4(1.0, 1.0, 1.0, 1.0);\n";
		}
		// And the specular colour is the vertex's where there is one, which the pixel adds with
		// D3DRS_SPECULARENABLE (dx8renderer.cpp gives a mesh with a second colour array one).
		if (description.SmokeGlow) {
			body += "    output.Specular = float4(input.Normal, 0.0);\n";
		}
		else {
			body += has_specular(description.FVF)
				? "    output.Specular = input.Specular;\n"
				: "    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n";
		}
	}

	// The sprite's half width, for the pixel half's fade; nothing else reads the specular alpha.
	if (description.SoftParticle) {
		body += "    output.Specular.a = input.TexCoord1.x;\n";
	}

	if (description.FogEnabled) {
		// FogParameters carries the start, the end and the density; the mode picks which two of
		// them are read.  D3D9's factor is the weight of the unfogged colour, so 1 is no fog.
		switch (description.FogVertexMode) {
		case FF_FOG_LINEAR:
			body += "    output.Fog = saturate((FogParameters.y - view_position.z)"
				" / max(FogParameters.y - FogParameters.x, 0.0001));\n";
			break;
		case FF_FOG_EXP:
			body += "    output.Fog = saturate(exp(-FogParameters.z * view_position.z));\n";
			break;
		case FF_FOG_EXP2:
			body += "    output.Fog = saturate(exp(-FogParameters.z * FogParameters.z"
				" * view_position.z * view_position.z));\n";
			break;
		default:
			return false;
		}
	}
	else {
		body += "    output.Fog = 1.0;\n";
	}

	const bool for_d3d11 = (target == VERTEX_SHADER_TARGET_D3D11);

	hlsl = "// Generated from a fixed-function vertex pipeline description.  See ffvertex.h.\n";
	if (for_d3d11) {
		append_constants_d3d11(hlsl, description);
	}
	else {
		append_constants_d3d9(hlsl, description);
	}

	// The input signature carries exactly what the vertex format carries, and no more.  Direct3D 9
	// let a shader declare an input the format did not supply and read zeroes out of it; D3D11
	// refuses the input layout outright, which is a draw that does not happen rather than a draw
	// that looks slightly wrong - 62534 of them in two minutes of Tournament Desert, which is every
	// model in the game.  What the format leaves out is filled in below with the value the fixed
	// function pipeline would have used.
	const unsigned coordinate_sets = texture_coordinate_set_count(description.FVF);

	hlsl +=
		"\n"
		"struct Input\n"
		"{\n";
	// Position stays three floats even for a pretransformed format, where the layout element has
	// four: the extra one is the reciprocal homogeneous w and the body does not read it.
	hlsl += "    float3 Position : POSITION;\n";
	if (has_normal(description.FVF)) {
		hlsl += "    float3 Normal   : NORMAL;\n";
	}
	if (has_diffuse(description.FVF)) {
		hlsl += "    float4 Diffuse  : COLOR0;\n";
	}
	if (has_specular(description.FVF)) {
		hlsl += "    float4 Specular : COLOR1;\n";
	}
	append_input_coordinate_sets(hlsl, coordinate_sets);
	hlsl +=
		"};\n"
		"\n";
	append_output_structure(hlsl, for_d3d11);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n";
	hlsl += body;

	// The stages that were not generated still have an output to fill, or the structure carries
	// whatever was on the stack and the pixel shader samples it.
	for (unsigned stage = description.StageCount; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		hlsl += line;
	}
	// The fire look's age and seed, in the output the missing second stage left empty.
	if (description.FireLook) {
		hlsl += "    output.TexCoord1 = input.TexCoord1;\n";
	}

	hlsl +=
		"    output.Position = mul(model_position, WorldViewProjection);\n"
		"    return output;\n"
		"}\n";
	return true;
}

std::string VertexShader_Key(const VertexPipelineDescription & description)
{
	std::string key;
	char field[64];

	snprintf(field, sizeof(field), "%lu:%u%u%u:%lu,%lu,%lu,%lu",
		(unsigned long)description.FVF, description.LightingEnabled ? 1u : 0u,
		description.SpecularEnabled ? 1u : 0u, description.ColourVertexEnabled ? 1u : 0u,
		(unsigned long)description.DiffuseMaterialSource, (unsigned long)description.AmbientMaterialSource,
		(unsigned long)description.EmissiveMaterialSource, (unsigned long)description.SpecularMaterialSource);
	key += field;

	for (unsigned index = 0; index < description.LightCount; ++index) {
		snprintf(field, sizeof(field), ":L%lu", (unsigned long)description.Lights[index].Type);
		key += field;
	}

	for (unsigned stage = 0; stage < description.StageCount; ++stage) {
		snprintf(field, sizeof(field), ":T%lu,%lu", (unsigned long)description.Stages[stage].TextureCoordinateIndex,
			(unsigned long)description.Stages[stage].TextureTransformFlags);
		key += field;
	}

	// Every lit program writes the halfway vector, the local viewer's or the fixed one, whether or not
	// its specular is kept; so every lit program's key says which, and a key names one text.
	if (description.LightingEnabled && description.LocalViewer) {
		key += ":V";
	}
	// An unlit program reads LOCALVIEWER only through a reflection vector, and then only its absence
	// changes the text, so only that is keyed: every key the engine makes (LOCALVIEWER on) stays as it was.
	if (!description.LightingEnabled && !description.LocalViewer) {
		for (unsigned stage = 0; stage < description.StageCount; ++stage) {
			if ((description.Stages[stage].TextureCoordinateIndex & ~COORDINATE_SET_MASK) == FF_TSS_TCI_CAMERASPACEREFLECTIONVECTOR) {
				key += ":E";
				break;
			}
		}
	}

	snprintf(field, sizeof(field), ":F%u,%lu", description.FogEnabled ? 1u : 0u,
		description.FogEnabled ? description.FogVertexMode : 0ul);
	key += field;
	if (description.SmokeGlow) {
		key += ":G";
	}
	if (description.SoftParticle) {
		key += ":Z";
	}
	if (description.FireLook) {
		key += ":B";
	}
	if (description.HemiAmbient) {
		key += ":H";
	}
	return key;
}
