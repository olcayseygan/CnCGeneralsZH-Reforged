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

#include "engineshader.h"
#include "ffvertex.h"
#include "sdl3target.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterScreenReflection.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/ModernWaterMaterial.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterHullVisual.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterFoamShading.h"
#include "Platform/WaterFoamEnvironment.h"
#include "Platform/WaterShoreFoam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** The terrain and the roads are eight variations on one program: a base colour, and then a chain of
** things multiplied into it.  Written out of the -dx11dump disassembly, one line of ps_1_1 each:
**
**     terrain.pso         lrp r0, v0.w, t1, t0 ; mul r0, r0, v0
**     terrainnoise.pso    the same, then mul r0, r0, t2
**     terrainnoise2.pso   the same, then mul r0, r0, t3
**     fterrain.pso        mul r0, t1, t0 ; mul r0, r0, v0
**     fterrain0.pso       mov r0, t1 ; mul r0, r0, v0
**     fterrainnoise.pso   fterrain, then mul r0, r0, t2
**     fterrainnoise2.pso  the same, then mul r0, r0, t3
**     roadnoise2.pso      mul r0, t0, t1 ; mul r0, r0, t2 ; mul r0, r0, v0
**
** lrp dest, a, b, c is c + a * (b - c), so v0.w weights the second layer over the first: that is
** the terrain blending one ground texture into the next by the alpha in its own vertices.  The road
** one multiplies the vertex colour in last rather than second, which is the same arithmetic until a
** term saturates.
*/
static const char * const TERRAIN_CHAIN[] = { "input.Diffuse", NULL };
static const char * const TERRAIN_NOISE_CHAIN[] = { "input.Diffuse", "texel2", NULL };
static const char * const TERRAIN_NOISE_2_CHAIN[] = { "input.Diffuse", "texel2", "texel3", NULL };
static const char * const ROAD_NOISE_2_CHAIN[] = { "texel2", "input.Diffuse", NULL };

static const char * const TERRAIN_OPENING = "lerp(texel0, texel1, input.Diffuse.a)";
static const char * const FLAT_TERRAIN_OPENING = "texel1 * texel0";
static const char * const FLAT_TERRAIN_BASE_OPENING = "texel1";
static const char * const ROAD_NOISE_2_OPENING = "texel0 * texel1";

// One entry per shipped shader this can write.  The file name is the whole key: the engine loads
// each of them from a fixed path and there is exactly one shader per file.  Opening and Chain are
// set for the multiply-chain programs above and null for the ones written out by hand.
struct EngineShaderEntry
{
	EngineShaderProgram Program;
	const char * FileName;
	const char * Name;
	const char * Opening;
	const char * const * Chain;
};

static const EngineShaderEntry ENGINE_SHADERS[] = {
	{ ENGINE_SHADER_TREES, "trees.vso", "engine:trees", NULL, NULL },
	{ ENGINE_SHADER_WATER_TRAPEZOID, "trapezoid water ps.1.1", "engine:trapezoidwater",
		NULL, NULL },
	{ ENGINE_SHADER_WATER_RIVER, "river water ps.1.1", "engine:riverwater", NULL, NULL },
	{ ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID, "classic trapezoid water ps.1.1", "engine:classictrapezoidwater", NULL, NULL },
	{ ENGINE_SHADER_WATER_CLASSIC_RIVER, "classic river water ps.1.1", "engine:classicriverwater", NULL, NULL },
	{ ENGINE_SHADER_WATER_REFLECTION, "water reflection ps.1.1", "engine:waterreflection",
		NULL, NULL },
	{ ENGINE_SHADER_TERRAIN, "terrain.pso", "engine:terrain",
		TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE, "terrainnoise.pso", "engine:terrainnoise",
		TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE_2, "terrainnoise2.pso", "engine:terrainnoise2",
		TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN, "fterrain.pso", "engine:flatterrain",
		FLAT_TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_BASE, "fterrain0.pso", "engine:flatterrainbase",
		FLAT_TERRAIN_BASE_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE, "fterrainnoise.pso", "engine:flatterrainnoise",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE_2, "fterrainnoise2.pso", "engine:flatterrainnoise2",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_ROAD_NOISE_2, "roadnoise2.pso", "engine:roadnoise2",
		ROAD_NOISE_2_OPENING, ROAD_NOISE_2_CHAIN },
	{ ENGINE_SHADER_MONOCHROME, "monochrome.pso", "engine:monochrome", NULL, NULL },
	{ ENGINE_SHADER_WATER_TRAPEZOID_FOAM, "trapezoid water foam ps.1.1", "engine:trapezoidwaterfoam",
		NULL, NULL },
	{ ENGINE_SHADER_WATER_RIVER_FOAM, "river water foam ps.1.1", "engine:riverwaterfoam", NULL, NULL }
};

static const unsigned ENGINE_SHADER_COUNT = sizeof(ENGINE_SHADERS) / sizeof(ENGINE_SHADERS[0]);

static const EngineShaderEntry * entry_for(EngineShaderProgram program)
{
	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		if (ENGINE_SHADERS[entry].Program == program) {
			return &ENGINE_SHADERS[entry];
		}
	}
	return NULL;
}

// The declaration every transcribed vertex program opens with.  The constant bank is the engine's
// own float4 registers, so the shader indexes them by the same number the SetVertexShaderConstantF
// call used and the transcription reads like the assembly it came from.
static void write_preamble(std::string & hlsl)
{
	char line[128];
	snprintf(line, sizeof(line), "cbuffer EngineConstants : register(b0)\n{\n    float4 c[%u];\n};\n\n",
		ENGINE_SHADER_CONSTANTS);
	hlsl += line;
}

// ffvertex's output structure, member for member (append_output_structure).  See the file comment for why
// it cannot differ: it had two coordinate sets where ffvertex writes MAXIMUM_VERTEX_STAGES, which put Fog
// on another location than every pixel program reads it from, and Direct3D 12 refuses such a pair (X1).
static void write_output_structure(std::string & hlsl)
{
	hlsl +=
		"struct Output\n"
		"{\n"
		"    float4 Position : SV_Position;\n"
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n";
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}
	hlsl +=
		"    float Fog : FOG;\n"
		"};\n"
		"\n";
}

// The coordinate sets a transcription does not write: zero, as ffvertex writes a set it has no source for.
static void write_unused_coordinates(std::string & hlsl, unsigned first)
{
	for (unsigned stage = first; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		hlsl += line;
	}
}

/*
** Trees.vso, transcribed from the vs_1_1 the translator disassembles:
**
**     dcl_position v0, dcl_blendweight v1, dcl_blendindices v2, dcl_texcoord v7
**     mov r2, v1.wwzw       ; a float3 input reads w as one, so this is (1, 1, v1.z, 1)
**     add r2, v0, -r2       ; r2.z is the vertex height above the tree's own base
**     mov a0.x, v1          ; the sway type, rounded
**     mov r0, c8[a0.x]      ; c8 is the no-sway slot, c9 to c18 the ten breezes
**     mad r1, r2.zzzw, r0, v0
**     m4x4 oPos, r1, c4
**     mov r2, v1.yyyw
**     mul oD0, v2, r2       ; the tree's lit shade, with the vertex alpha kept
**     mov oT0, v7
**     add r1, v0, c32
**     mul oT1, r1, c33      ; the shroud coordinate
**
** The declaration beside it (W3DTreeBuffer::initData) puts position in register 0, the normal slot
** in 1 and the D3DCOLOR in 2, which is the byte layout of DX8_FVF_XYZNDUV1.  Nothing in the tree
** vertex is a normal: nx is the sway type, ny the shade the push-aside left, nz the base height.
**
** oFog is never written, so the fog factor is one and the trees are drawn unfogged, which is what
** the Direct3D 9 device does with this shader bound.
*/
static void write_trees(std::string & hlsl)
{
	write_preamble(hlsl);
	hlsl +=
		"struct Input\n"
		"{\n"
		"    float3 Position : POSITION;\n"
		"    float3 Sway : NORMAL;\n"
		"    float4 Diffuse : COLOR0;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"};\n"
		"\n";
	write_output_structure(hlsl);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n"
		"    int sway_type = (int)(input.Sway.x + 0.5);\n"
		"    float height = input.Position.z - input.Sway.z;\n"
		"    float4 swayed = float4(height * c[8 + sway_type].xyz + input.Position, 1.0);\n"
		"    output.Position = float4(dot(swayed, c[4]), dot(swayed, c[5]),\n"
		"        dot(swayed, c[6]), dot(swayed, c[7]));\n"
		"    output.Diffuse = input.Diffuse * float4(input.Sway.yyy, 1.0);\n"
		"    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n"
		"    output.TexCoord0 = input.TexCoord0;\n"
		"    output.TexCoord1 = ((float4(input.Position, 1.0) + c[32]) * c[33]).xy;\n";
	write_unused_coordinates(hlsl, 2);
	hlsl +=
		"    output.Fog = 1.0;\n"
		"    return output;\n"
		"}\n";
}

// ffshader's declarations, member for member and in the same order, so a transcribed pixel program
// is bound with the same constant buffer, the same textures and the same samplers as a generated
// one.  Reading fewer of them than are declared costs nothing.
static void write_pixel_preamble(std::string & hlsl, bool volumetric = false,
	const char * declarations = NULL)
{
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"Texture2D Texture%u : register(t%u);\n"
			"SamplerState Sampler%u : register(s%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}

	hlsl +=
		"cbuffer CombinerConstants : register(b0)\n"
		"{\n"
		"    float4 TextureFactor;\n"
		"    float4 FogColour;\n"
		"    float4 AlphaReference;\n";
	{
		// The whole of DX11BackendClass::PixelConstantBlock up to the sun, in its order.  Declared
		// whether this program lights with it or not, because what follows is found by offset.
		char line[256];
		snprintf(line, sizeof(line),
			"    float4 NormalLightDirection[%u];\n"
			"    float4 NormalLightDiffuse[%u];\n"
			"    float4 NormalMapParameters;\n"
			"    float4 TerrainSunDirection;\n",
			NORMAL_MAPPED_LIGHTS, NORMAL_MAPPED_LIGHTS);
		hlsl += line;
	}
	// The shadow fields close the block, in the backend's order, and every transcribed program
	// declares them whether it reads them or not: a field is found by what comes before it.
	hlsl +=
		"    row_major float4x4 ShadowFromClip;\n"
		"    float4 ShadowParameters;\n"
		"    float4 ShadowViewport;\n"
		"    float4 ShadowSoftness;\n"
		"    float4 Sky;\n"
		"    float4 SkyUp;\n";
	// The smoke's field follows them, on the Direct3D 11 text alone (VOLUMETRIC_SAMPLING).
	if (volumetric) {
		hlsl += VOLUMETRIC_CONSTANTS;
	}
	hlsl += "};\n";
	hlsl += SHADOW_SAMPLING;
	if (volumetric) {
		hlsl += VOLUMETRIC_SAMPLING;
	}
	if (declarations != NULL) {
		hlsl += declarations;
	}
	hlsl +=
		"\n"
		"struct Input\n"
		"{\n"
		"    float4 Position  : SV_Position;\n"
		"    float4 Diffuse   : COLOR0;\n"
		"    float4 Specular  : COLOR1;\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}

	hlsl += "    float Fog        : FOG;\n";
	hlsl +=
		"};\n"
		"\n";
	hlsl +=
		"float4 main(Input input) : SV_Target\n"
		"{\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"    float4 texel%u = Texture%u.Sample(Sampler%u, input.TexCoord%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}
}

/*
** The trapezoid water, the ps.1.1 W3DWater assembles for every flat water body:
**
**     tex t0 ; the water texture
**     tex t1 ; white highlights on black
**     tex t2 ; the same highlights, tiled harder, on the camera space position
**     tex t3 ; the shroud, or white where there is none
**     mul r0, v0, t0
**     mad r0.rgb, t1, t2, r0
**     mul r0.rgb, r0, t3
**
** ps_1_1's registers hold [-1, 1] (PixelShader1xMaxValue 1, the documented minimum for ps 1.0 to 1.3),
** and only a _sat modifier or the final write clamps to [0, 1].  Every value here is a product or sum
** of texels and vertex colours in [0, 1], so none goes below zero, and the saturate on each line is the
** clamp at one: the same result.  Measured against the tests' ps_1_1 interpreter on captured draws (A3e-3).
** The alpha is the vertex alpha times the water texture's and the shroud never touches it: a
** shrouded stretch of water is dark, not transparent.
*/
static void write_trapezoid_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current = saturate(input.Diffuse * texel0);\n"
		"    current.rgb = saturate(texel1.rgb * texel2.rgb + current.rgb);\n"
		"    current.rgb = saturate(current.rgb * texel3.rgb);\n";
}

/*
** The river water, the same four stages with the sparkles kept apart from the base colour:
**
**     mul r0.rgb, v0, t0    ; the water, tinted by the vertex colour
**     mov r0.a, t0          ; the vertex alpha carries the shroud and must not fade the water
**     mul r1, t1, t2
**     add r1.rgb, r1, t3
**     mul r1.rgb, r1, v0.a  ; the sparkles and the edge glow do get darkened by the shroud
**     +mul r0.a, r0, t3
**     add r0.rgb, r0, r1
**
** The + pairs that alpha instruction with the colour one above it, so it reads r0.a as the previous
** line left it and writes the shroud's alpha into it.
*/
static void write_river_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current;\n"
		"    current.rgb = saturate(input.Diffuse.rgb * texel0.rgb);\n"
		"    current.a = saturate(texel0.a * texel3.a);\n"
		"    float3 sparkle = saturate(texel1.rgb * texel2.rgb);\n"
		"    sparkle = saturate(sparkle + texel3.rgb);\n"
		"    sparkle = saturate(sparkle * input.Diffuse.a);\n"
		"    current.rgb = saturate(current.rgb + sparkle);\n";
}

/*
** The water with foam on it (the Water Foam option), Direct3D 11 only: the SDL3 text is the plain water.
**
** How deep the water is under a pixel comes out of the scene's depth, the soft particles' copy at
** t7, which W3DWater takes right before it draws: the ground and every opaque object are in it and
** the water is not.  The pixel and what stands behind it both go back to the world through
** WorldFromClip, and the depth is the height between them.  The backend uploads that matrix and
** ShadowViewport for every program that reads the scene's depth, sun's map or not; a cleared depth
** or no copy at all (a null view reads zero) counts as deep water: no foam band, no edge.
**
** Where the foam sits comes from TexCoord2, which both waters fill with the camera position taken
** back to the world, times NOISE_REPEAT_FACTOR (1/16), plus the scroll: times sixteen it is the
** world drifting with the water, interpolated exactly, where the depth's way back would shimmer.
** The swell's phase rides in TextureFactor.g and turns the two noise layers round circles in
** opposite directions, so the flecks boil instead of sliding.  A feathered trapezoid hands over the
** curve the shoreline tiles wrote into the frame's alpha (TextureFactor.r times 32 is the depth
** that turns opaque, .a the deepest opacity) and W3DWater blends on the source alpha in place of
** the destination's, so the shallow foam is not faded out with the water around it.
**
** The sun's shadow lands on the surface last but the shroud, through SHADOW_APPLY, the way the
** ground takes it.  Every number to tune is a WATER_FOAM_ constant at the top of the text.
*/
static const char * const WATER_FOAM_SAMPLING =
	"Texture2D SceneDepth : register(t7);\n"
	"\n"
	"static const float  WATER_FOAM_SHORE_DEPTH = 3.0;   // world units of water the lace at the shore reaches through\n"
	"static const float  WATER_FOAM_SHORE = 0.75;        // lace strength at the water's edge\n"
	"static const float  WATER_FOAM_EDGE = 0.6;          // world units over which the water fades into the bank\n"
	"static const float  WATER_FOAM_FINE = 0.16;         // lace, cells per world unit of its coarsest octave\n"
	"static const float  WATER_FOAM_BROAD = 0.022;       // the patches whitecaps gather in\n"
	"static const float  WATER_FOAM_SWIRL = 0.9;         // how far a cycle moves the lace, in cells\n"
	"static const float  WATER_FOAM_CAPS = 0.32;         // whitecap strength\n"
	"static const float  WATER_FOAM_CAP_LOW = 0.52;      // broad patch value where whitecaps start\n"
	"static const float  WATER_FOAM_CAP_HIGH = 0.74;     // and where they are at full strength\n"
	"static const float  WATER_FOAM_OPACITY = 0.85;      // how opaque full foam makes the water\n"
	"static const float3 WATER_FOAM_COLOUR = float3(0.90, 0.94, 0.96);\n"
	"\n"
	"float foam_hash(float2 p)\n"
	"{\n"
	"    float3 q = frac(p.xyx * 0.1031);\n"
	"    q += dot(q, q.yzx + 33.33);\n"
	"    return frac((q.x + q.y) * q.z);\n"
	"}\n"
	"\n"
	"float foam_noise(float2 p)\n"
	"{\n"
	"    float2 i = floor(p);\n"
	"    float2 f = frac(p);\n"
	"    f = f * f * (3.0 - 2.0 * f);\n"
	"    return lerp(lerp(foam_hash(i), foam_hash(i + float2(1.0, 0.0)), f.x),\n"
	"                lerp(foam_hash(i + float2(0.0, 1.0)), foam_hash(i + float2(1.0, 1.0)), f.x), f.y);\n"
	"}\n"
	"\n"
	"float3 foam_world(float2 pixel, float depth)\n"
	"{\n"
	"    float2 ndc = float2(pixel.x * ShadowViewport.x * 2.0 - 1.0, 1.0 - pixel.y * ShadowViewport.y * 2.0);\n"
	"    float4 world = mul(float4(ndc, depth, 1.0), WorldFromClip);\n"
	"    return world.xyz / world.w;\n"
	"}\n"
	"\n"
	"float foam_water_depth(float4 position)\n"
	"{\n"
	"    float scene = SceneDepth.Load(int3(position.xy, 0)).r;\n"
	"    if (ShadowViewport.x <= 0.0 || scene <= 0.0 || scene >= 1.0) return 1000.0;\n"
	"    return max(foam_world(position.xy, position.z).z - foam_world(position.xy, scene).z, 0.0);\n"
	"}\n"
	"\n"
	// Foam is a web of thin veins.  Value noise thresholded on its own shows its lattice as square blocks,
	// so every octave turns away from the last and the veins come from where the noise crosses its middle.
	// The fine octave's crease goes under a pixel from the top of the camera, and there it crawls as the
	// swell moves it, so it hands over to its own mean as a pixel grows past a quarter of its cell.
	"float foam_lace(float2 p)\n"
	"{\n"
	"    float pixel = max(length(ddx(p)), length(ddy(p))) * 2.03;\n"
	"    float2x2 turn = float2x2(0.8, 0.6, -0.6, 0.8);\n"
	"    float2 q = mul(turn, p);\n"
	"    float wander = foam_noise(q * 0.5 + 7.3);\n"
	"    q += float2(wander, foam_noise(q.yx * 0.5 + 19.1)) * 1.6;\n"
	"    float one = 1.0 - abs(2.0 * foam_noise(q) - 1.0);\n"
	"    q = mul(turn, q) * 2.03 + 3.7;\n"
	"    float two = 1.0 - abs(2.0 * foam_noise(q) - 1.0);\n"
	"    return one * lerp(two, 0.64, saturate((pixel - 0.25) * 2.0));\n"
	"}\n"
	"\n"
	"float foam_amount(float2 world, float depth, float phase)\n"
	"{\n"
	"    float angle = phase * 6.2831853;\n"
	"    float2 swirl = float2(cos(angle), sin(angle)) * WATER_FOAM_SWIRL;\n"
	"    float lace = foam_lace(world * WATER_FOAM_FINE + swirl);\n"
	"    float aa = fwidth(lace);\n"
	// Bent and doubled, or the patches gather on the broad noise's own lattice as faint squares.
	"    float2 patch = mul(float2x2(0.8, -0.6, 0.6, 0.8), world) * WATER_FOAM_BROAD - swirl.yx * 0.1;\n"
	"    patch += float2(foam_noise(patch * 0.61 + 3.1), foam_noise(patch.yx * 0.61 + 11.7)) * 1.5;\n"
	"    float broad = foam_noise(patch) * 0.65 + foam_noise(mul(float2x2(0.6, 0.8, -0.8, 0.6), patch) * 2.3 + 5.3) * 0.35;\n"
	"    float caps = smoothstep(0.55 - aa, 0.95 + aa, lace) * smoothstep(WATER_FOAM_CAP_LOW, WATER_FOAM_CAP_HIGH, broad) * WATER_FOAM_CAPS;\n"
	"    // The lace thickens toward the edge and only there does it close up, so the shore laps.\n"
	"    float shore = 1.0 - saturate(depth / WATER_FOAM_SHORE_DEPTH);\n"
	"    float band = smoothstep(0.7 - 0.55 * shore - aa, 0.95 - 0.45 * shore + aa, lace) * sqrt(shore) * WATER_FOAM_SHORE;\n"
	"    return saturate(max(caps, band));\n"
	"}\n"
	"\n";

// The foam, the feather and the bank's fade, after the water's own colour and before the shadow.
// shade is how bright the vertex colour lets the foam be, which carries the river's shroud.
static void write_water_foam(std::string & hlsl, const char * edge_alpha)
{
	hlsl +=
		"    float water_depth = foam_water_depth(input.Position);\n"
		"    float foam = foam_amount(input.TexCoord2 * 16.0, water_depth, TextureFactor.g);\n"
		"    if (TextureFactor.a > 0.0) {\n"
		"        current.a = min(water_depth / max(TextureFactor.r * 32.0, 0.01), TextureFactor.a);\n"
		"    }\n"
		"    float shade = max(input.Diffuse.r, max(input.Diffuse.g, input.Diffuse.b));\n"
		"    current.rgb = lerp(current.rgb, WATER_FOAM_COLOUR * shade, foam);\n"
		"    current.a = lerp(current.a, ";
	hlsl += edge_alpha;
	hlsl +=
		", foam * WATER_FOAM_OPACITY) * saturate(water_depth / WATER_FOAM_EDGE);\n";
	hlsl += SHADOW_APPLY;
}

static void write_trapezoid_water_foam(std::string & hlsl)
{
	write_pixel_preamble(hlsl, true, WATER_FOAM_SAMPLING);
	hlsl +=
		"    float4 current = saturate(input.Diffuse * texel0);\n"
		"    current.rgb = saturate(texel1.rgb * texel2.rgb + current.rgb);\n";
	write_water_foam(hlsl, "1.0");
	hlsl += "    current.rgb = saturate(current.rgb * texel3.rgb);\n";
}

// The river's edge texture fades its ends as well as its banks, and the foam stops where it does.
static void write_river_water_foam(std::string & hlsl)
{
	write_pixel_preamble(hlsl, true, WATER_FOAM_SAMPLING);
	hlsl +=
		"    float4 current;\n"
		"    current.rgb = saturate(input.Diffuse.rgb * texel0.rgb);\n"
		"    current.a = saturate(texel0.a * texel3.a);\n"
		"    float3 sparkle = saturate(texel1.rgb * texel2.rgb);\n"
		"    sparkle = saturate(sparkle + texel3.rgb);\n"
		"    sparkle = saturate(sparkle * input.Diffuse.a);\n"
		"    current.rgb = saturate(current.rgb + sparkle);\n";
	write_water_foam(hlsl, "texel3.a");
}

bool EngineShader_Reads_Scene_Depth(EngineShaderProgram program)
{
	return program == ENGINE_SHADER_WATER_TRAPEZOID_FOAM || program == ENGINE_SHADER_WATER_RIVER_FOAM;
}

/*
** The reflection W3DWater lays over map water once the water itself is drawn:
**
**     def c1, 0.333333, 0.333333, 0.333333, 0
**     tex t0 ; the mirrored scene, looked up through a projected stage
**     dp3 r1.rgb, t0, c1
**     mov r1.rgb, 1-r1
**     add r1.rgb, r1, r1
**     add r1.rgb, r1, r1
**     mul r0.rgb, r1, c0
**     mov r0.rgb, 1-r0
**     +mov r0.a, c0
**
** The result multiplies the water already drawn, and the mirror was cleared to white, so open sky
** leaves the water its own colour.  Whatever stands in the mirror darkens it by the same amount
** whatever its own colour: a sunlit palm is nearly as bright as the white around it and used to
** vanish from the water the moment the fog lifted off it.  The two doublings clamp at one each, which
** is the saturate.  D3D9 reads the strength out of c0.  A transcribed program shares the combiners'
** constant buffer and not the engine's register bank, so here it is the texture factor's alpha,
** which W3DWater sets to the same value.
*/
static void write_water_reflection(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float coverage = saturate((1.0 - dot(texel0.rgb, float3(0.333333, 0.333333, 0.333333))) * 4.0);\n"
		"    float4 current = float4((1.0 - TextureFactor.a * coverage).xxx, TextureFactor.a);\n";
}

/*
** The grey the campaign briefings and some cutscenes lay over the whole view, ScreenBWFilter's:
**
**     tex t0                ; the frame, rendered into a texture first
**     dp3 r1, t0, c0        ; c0 is (0.3, 0.59, 0.11), the luminance
**     mul r1, r1, c1        ; c1 tints it: white, red or green, alpha one
**     lrp r0, c2, r1, t0    ; c2 is the fade, the same value in all four
**
** D3D9 reads the tint and the fade out of c1 and c2.  Here they ride in the texture factor, tint in
** the colour and fade in the alpha, which ScreenBWFilter sets beside the constants.  c2.w is one
** whatever the fade, so the alpha comes out as the luminance at every step of it.
*/
static void write_monochrome(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float luminance = saturate(dot(texel0.rgb, float3(0.3, 0.59, 0.11)));\n"
		"    float4 tinted = saturate(luminance * float4(TextureFactor.rgb, 1.0));\n"
		"    float4 current = lerp(texel0, tinted, float4(TextureFactor.aaa, 1.0));\n";
}

// ps_1_1's registers hold [-1, 1] and only _sat or the final write clamps to [0, 1] (see the trapezoid
// water above).  A chain of products of values in [0, 1] never goes below zero, so the clamp that
// matters is the one at the top, and it applies at every step, not only the last: a chain that
// overflows in the middle and comes back down is a different colour with the clamps than without them.
static void write_multiply_chain(std::string & hlsl, const EngineShaderEntry & entry,
	bool volumetric)
{
	write_pixel_preamble(hlsl, volumetric);

	hlsl += "    float4 current = saturate(";
	hlsl += entry.Opening;
	hlsl += ");\n";

	for (const char * const * step = entry.Chain; *step != NULL; ++step) {
		hlsl += "    current = saturate(current * ";
		hlsl += *step;
		hlsl += ");\n";
	}

	// The ground is where a shadow is read, so every transcribed program that paints it takes one.
	hlsl += volumetric ? VOLUMETRIC_SHADOW_APPLY : SHADOW_APPLY;
}

bool EngineShader_Paints_Ground(EngineShaderProgram program)
{
	return program == ENGINE_SHADER_TERRAIN || program == ENGINE_SHADER_TERRAIN_NOISE
		|| program == ENGINE_SHADER_TERRAIN_NOISE_2 || program == ENGINE_SHADER_FLAT_TERRAIN
		|| program == ENGINE_SHADER_FLAT_TERRAIN_BASE || program == ENGINE_SHADER_FLAT_TERRAIN_NOISE
		|| program == ENGINE_SHADER_FLAT_TERRAIN_NOISE_2 || program == ENGINE_SHADER_ROAD_NOISE_2;
}

static char lowered(char character)
{
	return (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') : character;
}

// The last path component, lowered, compared against the table.  The engine writes its paths with
// backslashes and mixed case, and neither is worth carrying into the table.
EngineShaderProgram EngineShader_From_File(const char * file_path)
{
	if (file_path == NULL) {
		return ENGINE_SHADER_NONE;
	}

	const char * name = file_path;
	for (const char * step = file_path; *step != '\0'; ++step) {
		if (*step == '\\' || *step == '/') {
			name = step + 1;
		}
	}

	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		const char * wanted = ENGINE_SHADERS[entry].FileName;
		const char * found = name;
		while (*wanted != '\0' && lowered(*found) == *wanted) {
			++wanted;
			++found;
		}
		if (*wanted == '\0' && *found == '\0') {
			return ENGINE_SHADERS[entry].Program;
		}
	}
	return ENGINE_SHADER_NONE;
}

bool EngineShader_Vertex_Program(EngineShaderProgram program, std::string & hlsl,
	VertexShaderTarget target)
{
	// Transcribed for the D3D11 profile only: on D3D9 the shipped .vso ran on the device itself.
	// SDL3 GPU is the D3D11 text with its bindings rewritten.
	if (target == VERTEX_SHADER_TARGET_D3D9) {
		hlsl.clear();
		return false;
	}
	if (target == VERTEX_SHADER_TARGET_SDL3_GPU) {
		return EngineShader_Vertex_Program(program, hlsl, VERTEX_SHADER_TARGET_D3D11)
			&& SDL3_Shader_Retarget(hlsl, true);
	}

	hlsl.clear();
	switch (program) {
	case ENGINE_SHADER_TREES:
		write_trees(hlsl);
		return true;
	default:
		return false;
	}
}

static bool write_engine_pixel_program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, bool volumetric);

// SDL map water has a dedicated, explicitly packed uniform block (SdlWaterPixelConstants).
// Runtime-generated normal detail, terrain depth, refraction and the coloured planar reflection
// are composed before the existing shore/shroud alpha blend. No additional assets are required.
static void write_modern_water(std::string &hlsl, bool river, bool sdl = false)
{
	const auto quality = sdl ? WaterPerformanceSettings::Applied() : WaterPerformanceSettings::Configuration();
	hlsl.clear();
	if (sdl) {
		hlsl += std::string("#define WATER_SCENE_REFLECTIONS ") + (quality.sceneReflections ? "1\n" : "0\n");
		hlsl += "#define WATER_REFRACTION_QUALITY " + std::to_string(int(quality.refraction)) + "\n";
	}
	for (unsigned stage = 0; stage < 15u; ++stage) {
		char line[128];
		snprintf(line, sizeof(line), "Texture2D Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
		hlsl += line;
	}
	hlsl += WaterFftSampling::UniformSource("CombinerConstants",0,true,sdl);
	hlsl += R"HLSL(struct Input
{
    float4 Position : SV_Position;
    float4 Diffuse : COLOR0;
    float4 Specular : COLOR1;
)HLSL";
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[80];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}
	hlsl += R"HLSL(    float Fog : FOG;
};
// The legacy scene targets use UNORM rather than sRGB. Decode their colours for light transport,
// then encode the composed water once before the legacy fog and shoreline blend.
float3 waterLinear(float3 rgb) { return pow(max(rgb, float3(0, 0, 0)), 2.2); }
float3 waterDisplay(float3 rgb) { return pow(saturate(rgb), 1.0 / 2.2); }
float oceanHash(float2 p)
{
    float3 q = frac(float3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float oceanNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(oceanHash(cell), oceanHash(cell+float2(1,0)), f.x),
        lerp(oceanHash(cell+float2(0,1)), oceanHash(cell+float2(1,1)), f.x), f.y);
}
// Broad radiance belongs to the reflected sky direction. It therefore follows the evolving
// surface normal instead of looking like a second texture sliding across the water plane.
float3 oceanSky(float3 ray, float3 light, float daylight)
{
    float horizon = pow(1.0-saturate(ray.z), 4.0);
    float2 sunAxis = normalize(light.xy + float2(0.00001,0));
    float2 skyPlane = float2(dot(ray.xy,sunAxis),dot(ray.xy,float2(-sunAxis.y,sunAxis.x)))
        / (0.45+abs(ray.z));
    float cloud = oceanNoise(skyPlane*2.7+float2(7.8,2.3))*0.7
        + oceanNoise(skyPlane*6.1+float2(0.4,13.7))*0.3;
    // A single broad cloud bank lights only the facets reflecting that portion of the sky.
    // Its direction follows the sky's sun-relative frame, independent of the camera and waves.
    float3 cloudDirection=normalize(float3(-sunAxis.y,sunAxis.x,0.85));
    float cloudBank=pow(saturate(dot(ray,cloudDirection)),48.0);
    cloud = smoothstep(0.46,0.73,cloud) * (0.04+1.5*cloudBank);
    // A finite bright part of the sky produces broken silver facets. An azimuth-uniform bright
    // horizon instead illuminates every crest equally, making the waves look like pale stripes.
    float3 radiance = lerp(float3(0.016,0.055,0.110),float3(0.023,0.070,0.130),horizon);
    radiance += cloud*float3(1.85,1.96,2.04);
    radiance += pow(saturate(dot(ray,light)),12.0)*float3(0.24,0.18,0.10);
    // Downward reflected rays see adjacent water. Keep the horizon transition continuous, but
    // do not extend a bright sky over the whole lower hemisphere.
    radiance = lerp(float3(0.001,0.005,0.010),radiance,smoothstep(-0.12,0.18,ray.z));
    return radiance * (0.06+0.94*daylight);
}
float3 oceanEnvironment(float3 ray,float3 light,float daylight,float roughness)
{
    // The unresolved ripple distribution broadens every reflected light source, including the
    // sky. Sampling its sharp horizon/cloud field only once makes small normal changes flicker.
    float3 tangent=normalize(cross(ray,float3(0,0,1))+float3(0.00001,0,0));
    float3 bitangent=cross(ray,tangent);
    float cone=max(roughness,0.22)*0.85;
    float3 value=oceanSky(ray,light,daylight)*0.4;
    value+=oceanSky(normalize(ray+tangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray-tangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray+bitangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray-bitangent*cone),light,daylight)*0.15;
    return value;
}
float terrainHeight(float2 p)
{
    float2 uv = (p + TerrainParameters.xx + 5.0) / max(TerrainParameters.y, 1.0);
    return Texture2.SampleLevel(Sampler2, uv, 0.0).r * TerrainParameters.z;
}
float terrainSun(float3 world, float3 light)
{
    float visible = 1.0;
    if (TerrainParameters.w > 0.5) {
        float distance = 12.0;
        for (int i = 0; i < 6; ++i) {
            float3 ray = world + light * distance;
            visible = min(visible, smoothstep(-0.6, 1.2, ray.z - terrainHeight(ray.xy)));
            distance *= 2.0;
        }
    }
    return visible;
}
float2 pixelOffset(float2 offset, float2 dx, float2 dy)
{
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float inverse = abs(determinant) > 0.00001 ? 1.0 / determinant : 0.0;
    return float2(offset.x * dy.y - offset.y * dy.x, dx.x * offset.y - dx.y * offset.x) * inverse;
}
float3 scenePosition(float2 pixel, float deviceDepth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 p = mul(float4(uv * float2(2, -2) + float2(-1, 1), deviceDepth, 1), WorldFromClip);
    return p.xyz / p.w;
}
// Viewport.xy already contains the legacy +0.5; these are source pixel-centre bounds.
float2 waterScenePixel(float2 pixel)
{
    float2 firstPixel = max(float2(0.5, 0.5), Viewport.xy);
    float2 lastPixel = min(1.0 / SceneParameters.yz - 0.5,
        Viewport.xy + Viewport.zw - 1.0);
    return clamp(pixel, firstPixel, lastPixel);
}
// Depth is a point-sampled independent scene snapshot, including submerged objects.
// Preserve the existing terrain fallback when no actual scene hit is available.
float3 bottomPosition(float2 pixel, float3 surface, float baseLevel)
{
    float h = TerrainParameters.w > 0.5 ? terrainHeight(surface.xy) : baseLevel - 20.0;
    float3 fallback = float3(surface.xy, min(h, baseLevel));
    if (SceneParameters.w < 0.5) return fallback;
    pixel = waterScenePixel(floor(pixel) + 0.5);
    float d = Texture6.SampleLevel(Sampler6, pixel * SceneParameters.yz, 0).r;
    if (d >= 0.999999) return fallback;
    return scenePosition(pixel, d);
}
float2 waterRefractionPixels(float opticalDepth, float3 view, float3 normal, float2 dx, float2 dy)
{
    float3 incident = -view;
    float3 underwater = refract(incident, normal, 0.75);
    // A downward ray meets a plane D below its origin at -D * ray.xy/ray.z.
    // Subtract the incident-ray hit from the refracted-ray hit in that order.
    // The local screen Jacobian remains the existing bounded perspective approximation.
    float2 displacement = opticalDepth * (incident.xy / min(incident.z, -0.1)
        - underwater.xy / min(underwater.z, -0.1));
    float2 pixels = clamp(pixelOffset(displacement, dx, dy), float2(-8, -8), float2(8, 8));
    return pixels * smoothstep(0.15, 2.5, opticalDepth);
}
struct WaterRefractionSample
{
    float2 pixel;
    float3 bottom;
    float3 colour;
    float accepted;
};
// Colour reconstruction includes only taps with a real submerged scene hit.
// Clear/invalid depth cannot use the terrain fallback as evidence for displaced colour.
bool waterRefractionHit(float2 texel, float3 world, float3 opticalWorld, float3 view, out float3 hit)
{
    hit = float3(0, 0, 0);
    float d = Texture6.SampleLevel(Sampler6, texel * SceneParameters.yz, 0).r;
    if (!(d >= 0.0 && d < 0.999999)) return false;
    hit = scenePosition(texel, d);
    return hit.z <= min(world.z, opticalWorld.z) - 0.08
        && dot(hit-world, -view) >= 0.0 && dot(hit-opticalWorld, -view) >= 0.0;
}
WaterRefractionSample waterRefractedScene(float2 originalPixel, float2 offset,
    float3 world, float3 opticalWorld, float3 view, float baseLevel, float3 originalBottom)
{
    WaterRefractionSample result;
    // SV_Position is a texel centre in production. Snap the fallback explicitly so it never
    // introduces an unvalidated fractional colour footprint, including in diagnostic callers.
    result.pixel = waterScenePixel(floor(originalPixel) + 0.5);
    result.bottom = originalBottom;
    result.accepted = 0.0;
    result.colour = float3(0, 0, 0);
    // An unavailable scene colour binds a 1x1 white placeholder, while SceneParameters.yz
    // still describe the render target. Do not Load it at target-sized pixel coordinates.
    // The caller uses its scattering fallback when SceneParameters.x is zero.
    [branch] if (!(SceneParameters.x > 0.5)) return result;
    [branch] if (SceneParameters.w > 0.5) {
        float2 candidate = waterScenePixel(originalPixel + offset);
        float2 lower = floor(candidate - 0.5) + 0.5;
        float2 f = frac(candidate - 0.5);
        float4 weights = float4((1-f.x)*(1-f.y), f.x*(1-f.y), (1-f.x)*f.y, f.x*f.y);
        int nearest = (f.x >= 0.5 ? 1 : 0) + (f.y >= 0.5 ? 2 : 0);
        float2 nearestTexel = waterScenePixel(lower + float2(nearest & 1, (nearest >> 1) & 1));
        float3 nearestHit;
        // Preserve a valid nearest scene hit for transport. A rejected colour neighbour must
        // not switch the entire optical ray back to the original pixel: at grazing angles that
        // discontinuously changes absorption and also contaminates neighbouring derivatives.
        bool nearestValid = waterRefractionHit(nearestTexel, world, opticalWorld, view, nearestHit);
        [branch] if (nearestValid) {
            // A mask rather than validWeights[nearest]: FXC cannot write a vector at a runtime index,
            // and DXC's HLSL 2021 takes no vector ternary.
            float4 validWeights = weights * float4(int4(0, 1, 2, 3) == nearest);
            bool completeFootprint = true;
            [unroll] for (int i = 0; i < 4; ++i) {
                [branch] if (i != nearest && weights[i] > 0.0) {
                    float2 texel = waterScenePixel(lower + float2(i & 1, (i >> 1) & 1));
                    float3 neighbourHit;
                    bool valid = all(texel == nearestTexel)
                        || waterRefractionHit(texel, world, opticalWorld, view, neighbourHit);
                    validWeights[i] = valid ? weights[i] : 0.0;
                    completeFootprint = completeFootprint && valid;
                }
            }
            result.pixel = candidate;
            result.bottom = nearestHit;
            result.accepted = 1.0;
            [branch] if (completeFootprint) {
                result.colour = Texture5.SampleLevel(Sampler5, candidate * SceneParameters.yz, 0).rgb;
            } else {
                // Depth-aware bilinear reconstruction: every contributing texel has passed
                // the same actual-depth, submerged-height and forward-plane checks. The valid
                // nearest texel guarantees at least one quarter of the original weight.
                float3 colour = float3(0, 0, 0);
                [unroll] for (int i = 0; i < 4; ++i) {
                    [branch] if (validWeights[i] > 0.0) {
                        float2 texel = waterScenePixel(lower + float2(i & 1, (i >> 1) & 1));
                        colour += Texture5.Load(int3(int2(floor(texel)), 0)).rgb * validWeights[i];
                    }
                }
                result.colour = colour / dot(validWeights, float4(1, 1, 1, 1));
            }
            return result;
        }
    }
    // An invalid nearest hit returns the original colour/actual bottom. An unavailable depth
    // snapshot keeps that original colour and the caller's terrain fallback without refraction.
    [branch] if (all(frac(result.pixel - 0.5) == 0.0))
        result.colour = Texture5.Load(int3(int2(floor(result.pixel)), 0)).rgb;
    else
        result.colour = Texture5.SampleLevel(Sampler5, result.pixel * SceneParameters.yz, 0).rgb;
    return result;
}
)HLSL";
	if (sdl) {
		hlsl += WaterScreenReflection::ShaderSource();
		hlsl += R"HLSL(
float4 waterMirrorTexel(int2 pixel, int2 size)
{
    float3 colour=Texture1.Load(int3(clamp(pixel,int2(0,0),size-1),0)).rgb;
    // Legacy terrain cannot supply coverage alpha. Resolve the black-clear convention
    // BEFORE filtering, so a partially covered texel does not become an opaque black rim.
    float coverage=saturate(max(max(colour.r,colour.g),colour.b)*64.0);
    return float4(colour*coverage,coverage);
}
float4 waterMirrorFiltered(float2 uv, int2 size)
{
    float2 p=saturate(uv)*float2(size)-0.5, f=frac(p);
    int2 base=int2(floor(p));
    float4 c00=waterMirrorTexel(base,size), c10=waterMirrorTexel(base+int2(1,0),size);
    float4 c01=waterMirrorTexel(base+int2(0,1),size), c11=waterMirrorTexel(base+int2(1,1),size);
    // Preserve the existing display-space texture filtering, but carry coverage with it.
    return lerp(lerp(c00,c10,f.x),lerp(c01,c11,f.x),f.y);
}
)HLSL";
	}
	hlsl += WaterSurfaceModel::ShaderSource();
	hlsl += WaterSurfaceModel::OceanShaderSource();
	hlsl += WaterOpticalModel::GetHlslSource();
	hlsl += ModernWaterMaterial::OpticalSource();
	hlsl += ModernWaterMaterial::BubbleSource();
	hlsl += WaterHullModel::GetHlslSource();
	hlsl += WaterFftSampling::PixelSource(WaterFftSpectrum::ForResolution(quality.activeFftResolution));
	hlsl += WaterWaveCageShader::Source(false);
	char oceanBound[96];
	snprintf(oceanBound, sizeof(oceanBound), "static const float oceanDisplacementBound = %.9g;\n",
		(double)WaterSurfaceModel::OceanMaxDisplacement);
	hlsl += oceanBound;
	hlsl += R"HLSL(
float terrainWaveAttenuation(float depth, float offshore)
{
    float shore = saturate(depth/9.0);
    shore = shore*shore*(3.0-2.0*shore);
    return lerp(shore, saturate((depth-0.15)/oceanDisplacementBound), offshore);
}
)HLSL";
	hlsl += WaterHullVisual::Source();
	hlsl += WaterHullModel::GetEffectsHlslSource();
	hlsl += R"HLSL(
// Fresh visible contact uses the posed triangle slice at the raster surface, including
// horizontal chop and geometric interpolation. It does not alter pressure or old wakes.
// Contact material for the current posed triangle slice.
// Query admission and hull accumulation remain with each caller.
float3 oceanMeshContactFoam(WaterVisualResult contact,float2 local,float4 motion,float2 axis,
    float speed,float moving,float collision,float time,float incomingHeight,float footprint,float waveImpact)
{
    float2 velocity=float2(dot(motion.xy,axis),dot(motion.xy,float2(-axis.y,axis.x)));
    float blend=smoothstep(-0.25,0.25,velocity.x/max(speed,0.001));
    float2 bow=lerp(contact.negative,contact.positive,blend);
    float width=max((contact.maxY-contact.minY)*0.5,1.0);
    float aa=clamp(footprint*0.6,0.25,1.5);
    float wash=oceanNoise(local*float2(0.065,0.32)+float2(-time*0.12,motion.z*9.0));
    float flecks=oceanNoise(local*0.48+float2(time*0.08,-time*0.13));
    float sideWidth=1.0+moving*min(2.0,sqrt(width)*0.35)+collision*1.3;
    float rim=1.0-smoothstep(sideWidth,sideWidth+aa,contact.distance);
    float streak=smoothstep(0.43,0.68,wash)*(0.2+0.8*smoothstep(0.25,0.65,flecks));
    // Wave contact supplies density once; the common foam material supplies fine
    // breakup. Repeated independent birth masks previously erased most idle contact.
    float sideFoam=max(rim*streak*moving*0.38,
        waterHullContactCoverage(contact.distance,footprint,incomingHeight,waveImpact,width));
    float outer=1.0-smoothstep(sideWidth,sideWidth+3.5,contact.distance);
    float bowDistance=length((local-bow)/float2(max(width*0.8,3.0),max(width*0.9,3.0)));
    float bowRegion=exp(-bowDistance*bowDistance*1.4);
    float bowWidth=2.0+moving*min(7.0,3.0+sqrt(width)*0.55);
    float bowFoam=(1.0-smoothstep(bowWidth,bowWidth+aa,contact.distance))*bowRegion
        *moving*(0.45+0.55*smoothstep(0.25,0.7,flecks));
    return float3(sideFoam,bowFoam*0.82,outer*(collision*0.18+moving*0.25)+bowFoam*0.4);
}
float2 oceanMeshHullEffects(float3 world,float level,float time,float offshore,
    float incomingHeight,float footprint,float waveImpact)
{
    float foam=0.0,turbulence=0.0;
    if(offshore<=0.001 || abs(world.z-level)>fftVerticalBound) return float2(0,0);
    float collision=waterHullContactResponse(incomingHeight,waveImpact);
    for(int h=0;h<clamp((int)HullParameters.x,0,8);++h) {
        float4 meta=VisualHullMeta[h];
        if(meta.w<0.5 || meta.y<1.0) continue;
        int index=h*10;
        float4 position=WaterContactRegister(index),axis=WaterContactRegister(index+1);
        if(position.w<3.0 || abs(level-position.z)>0.1) continue;
        float2 delta=world.xy-position.xy;
        if(dot(delta,delta)>(meta.z+12.0)*(meta.z+12.0)) continue;
        float4 motion=WaterContactRegister(index+2);
        float speed=length(motion.xy),moving=smoothstep(0.3,18.0,speed);
        // Both fresh-contact contributions are exactly zero; keep the checked geometry query for every active hull.
        if(moving==0.0 && collision==0.0) continue;
        float2 local=float2(dot(delta,axis.xy),dot(delta,float2(-axis.y,axis.x)));
        WaterVisualResult contact=WaterVisualQuery(h,float3(local,world.z-level));
        if(contact.hasSegment<0.5 || contact.distance>12.0) continue;
        float3 effects=oceanMeshContactFoam(contact,local,motion,axis.xy,
            speed,moving,collision,time,incomingHeight,footprint,waveImpact);
        foam=max(foam,max(effects.x,effects.y));
        turbulence=max(turbulence,effects.z);
    }
    return saturate(float2(foam,turbulence))*offshore;
}
)HLSL";
	hlsl += WaterFoamShading::HistorySamplingSource();
	hlsl += WaterFoamShading::Source();
	hlsl += WaterFoamShading::MaterialSource();
	hlsl += WaterFoamEnvironment::HlslSource();
	hlsl += WaterShoreFoam::Source();
	hlsl += WaterShoreFoam::HistorySource();
	hlsl += R"HLSL(
float4 main(Input input) : SV_Target
{
    float2 screenUV = (input.Position.xy - Viewport.xy) / max(Viewport.zw, float2(1.0, 1.0));
    float4 worldH = mul(float4(screenUV * float2(2.0, -2.0) + float2(-1.0, 1.0), input.Position.z, 1.0), WorldFromClip);
    float3 world = worldH.xyz / worldH.w;
    float2 dx = ddx(world.xy), dy = ddy(world.xy);
    float time = WaterParameters.x;
    float fftWeight=FftParameters.x*saturate(input.Specular.x);
    // Horizontal chop changes world.xy. Spectral samples must remain on the original surface
    // parameters, as the vertex stage used them, rather than swimming along the displaced mesh.
    float2 wavePosition=FftParameters.x>0.5?input.TexCoord2:world.xy;
    float2 parameterDx=ddx(wavePosition),parameterDy=ddy(wavePosition);
    // Projected flat patches reuse UV0 for a render-only boundary/LOD height correction.
    // Their original material mapping is world.xy/150. Legacy grids and rivers keep their UVs.
    float meshCorrection=OpticalParameters.w>0.5?input.TexCoord0.x:0.0;
    float2 materialUV=OpticalParameters.w>0.5?world.xy/150.0:input.TexCoord0;
    float4 base = Texture0.Sample(Sampler0, materialUV);
    float baseLevel = input.TexCoord1.y;
    float3 bottom = bottomPosition(input.Position.xy, world, baseLevel);
    float3 shoreSceneBottom=bottom; // un-refracted scene hit used by the guarded terrain-match heuristic
    float depth = max(world.z - bottom.z, 0.0);
    float3 view = normalize(CameraPosition.xyz - world + float3(0, 0, 0.00001));
    float2 contextUV = (world.xy + TerrainParameters.xx + 5.0) / max(TerrainParameters.y, 1.0);
    float4 opticalContext=Texture4.SampleLevel(Sampler4,contextUV,0);
    float offshore = opticalContext.r * OpticalParameters.z * (1.0 - WaterParameters.y);
    // The mesh and this derivative share every mode, phase, dispersion and crest harmonic.
    // Interpolated attenuation includes shores/structures, and its product derivative is retained.
    float2 shoreRippleSlope;
    float3 legacyWave = waterWave(wavePosition, time, shoreRippleSlope);
    float4 oceanSwell;
    float4 oceanSurface = waterOceanSurface(wavePosition, time, oceanSwell);
    float3 oceanWave = oceanSurface.xyz;
    float3 wave = lerp(legacyWave, oceanWave, offshore);
    FftSurface spectral=(FftSurface)0;
    )HLSL";
    hlsl+=R"HLSL(float3 bowLiftField=0;float3 fftDampingField=float3(0,0,0);float2 fftWeightSlope=0;)HLSL";
    hlsl+=R"HLSL(
    float fftHorizontalWeight=0.0;
    float2 fftHorizontalWeightSlope=0;
    if(FftParameters.x>0.5) {
        float freeDamping=saturate(input.Specular.y),fftOffshore=saturate(input.Specular.w);
        fftDampingField=vesselApplyHullDamping(wavePosition,baseLevel,
            float3(freeDamping,fftParameterGradient(freeDamping,parameterDx,parameterDy)),
            float3(fftOffshore,fftParameterGradient(fftOffshore,parameterDx,parameterDy)));
        fftWeightSlope=fftParameterGradient(fftWeight,parameterDx,parameterDy);
        fftHorizontalWeight=fftWeight*fftOffshore;
        fftHorizontalWeightSlope=fftWeightSlope*fftOffshore
            +fftParameterGradient(fftOffshore,parameterDx,parameterDy)*fftWeight;
        float3 horizontalScale=float3(fftHorizontalWeight*fftDampingField.x,
            fftHorizontalWeightSlope*fftDampingField.x+fftDampingField.yz*fftHorizontalWeight);
        )HLSL";
    hlsl+=R"HLSL(spectral=fftSurface(wavePosition,baseLevel,max(length(parameterDx),length(parameterDy)),horizontalScale);
        bowLiftField=fftBowLift(spectral.bow,float3(freeDamping,fftParameterGradient(freeDamping,parameterDx,parameterDy)),fftDampingField);)HLSL";
    hlsl+=R"HLSL(
    }
    float footprint=max(length(dx),length(dy));
    float foamFootprint=max(length(parameterDx),length(parameterDy));
    float hullWave=lerp(wave.x,spectral.contactIncident.x,fftWeight);
    float waveImpact=lerp(length(wave.yz),length(spectral.contactIncident.yz),fftWeight);
    float3 hullEffects=oceanHullEffects(wavePosition,baseLevel,time,offshore,hullWave,foamFootprint,waveImpact);
    hullEffects.x=waterFoamContactPatches(wavePosition,hullEffects.x,foamFootprint,time);
)HLSL";
	hlsl += R"HLSL(
    float2 meshHullEffects=oceanMeshHullEffects(world,baseLevel,time,offshore,hullWave,footprint,waveImpact);
    meshHullEffects.x=waterFoamContactPatches(wavePosition,meshHullEffects.x,foamFootprint,time);
    hullEffects.y=max(hullEffects.y,meshHullEffects.y);
)HLSL";
	hlsl += R"HLSL(    float3 historyFoam=waterFoamHistory(wavePosition,baseLevel,time,foamFootprint);
    historyFoam*=smoothstep(0.0,1.5,depth)*(1.0-WaterParameters.y);
    float attenuation = saturate(input.TexCoord1.x);
    // Flat water carries obstacle-only attenuation in its otherwise unused diffuse red channel.
    // Reconstruct the terrain profile at fragment density, avoiding large triangular shading
    // facets from differentiating the coarse mesh's interpolated shore attenuation.
    float surfaceTerrainHeight=TerrainParameters.w>0.5?terrainHeight(world.xy):baseLevel-20.0;
    float localTerrain = TerrainParameters.w > 0.5
        ? terrainWaveAttenuation(baseLevel-surfaceTerrainHeight, offshore) : 1.0;
    attenuation = lerp(attenuation, min(localTerrain, input.Diffuse.r), offshore);
    attenuation = min(attenuation,lerp(1.0,hullEffects.z,offshore));
    float determinant = parameterDx.x * parameterDy.y - parameterDx.y * parameterDy.x;
    float invDet = abs(determinant) > 0.000001 ? 1.0 / determinant : 0.0;
    float ou = ddx(offshore), ov = ddy(offshore);
    float2 offshoreSlope = float2(ou*parameterDy.y-ov*parameterDx.y, ov*parameterDx.x-ou*parameterDy.x)*invDet;
    wave.yz += (oceanWave.x-legacyWave.x)*offshoreSlope;
    float du = ddx(attenuation), dv = ddy(attenuation);
    float2 attenuationSlope = float2(du*parameterDy.y - dv*parameterDx.y, dv*parameterDx.x - du*parameterDy.x) * invDet;
    float zu = ddx(baseLevel), zv = ddy(baseLevel);
    float2 baseSlope = float2(zu*parameterDy.y - zv*parameterDx.y, zv*parameterDx.x - zu*parameterDy.x) * invDet;
    float cu=ddx(meshCorrection),cv=ddy(meshCorrection);
    float2 correctionSlope=float2(cu*parameterDy.y-cv*parameterDx.y,cv*parameterDx.x-cu*parameterDy.x)*invDet;
    float2 slope = baseSlope + wave.yz * attenuation + wave.x * attenuationSlope + correctionSlope;
)HLSL";
	hlsl += R"HLSL(    float3 normal = normalize(float3(-slope, 1.0));
    float continuousHeight=baseLevel+wave.x*attenuation+meshCorrection;
    WaveCage cage=waveCage(wavePosition,baseLevel);
    float cageAmount=cage.support*FftParameters.x;
    float2 cageAmountSlope=fftParameterGradient(cageAmount,parameterDx,parameterDy);
    float3 cageObstacle=float3(saturate(input.Diffuse.r),fftParameterGradient(input.Diffuse.r,parameterDx,parameterDy));
    if(cageAmount>0.0)
        cageObstacle=vesselApplyHullDamping(wavePosition,baseLevel,cageObstacle,float3(1,0,0));
    WaveCageSurface cagePhysical=waveCageSurface(cage,wavePosition,baseLevel,spectral.displacement,
        spectral.derivativeX,spectral.derivativeY,cageObstacle,parameterDx,parameterDy);
    WaveCageSurface cageShaded=waveCageSurface(cage,wavePosition,baseLevel,spectral.shadingDisplacement,
        spectral.shadingDerivativeX,spectral.shadingDerivativeY,cageObstacle,parameterDx,parameterDy);
    float fftCompression=1.0;
    float fftDamping=0.0;
    float3 resolvedNormal=normal;
    float unresolvedSlopeVariance=0.0;
    if(FftParameters.x>0.5) {
        fftDamping=fftDampingField.x;
        float2 weightSlope=fftWeightSlope;
        float2 dampingSlope=fftDampingField.yz;
        float horizontalWeight=fftHorizontalWeight*fftDamping;
        float2 horizontalSlope=fftHorizontalWeightSlope*fftDamping+dampingSlope*fftHorizontalWeight;
        float oldHeight=continuousHeight-baseLevel;
        )HLSL";
    hlsl+=R"HLSL(float newHeight=spectral.displacement.z*fftDamping+bowLiftField.x;)HLSL";
    hlsl+=R"HLSL(
        float2 newSlope=float2(spectral.derivativeX.z,spectral.derivativeY.z)*fftDamping
            )HLSL";
    hlsl+=R"HLSL(+spectral.displacement.z*dampingSlope+bowLiftField.yz;)HLSL";
    hlsl+=R"HLSL(
        float2 heightSlope=baseSlope+lerp(slope-baseSlope,newSlope,fftWeight)
            +(newHeight-oldHeight)*weightSlope;
        float3 tangentX=float3(1.0+spectral.derivativeX.x*horizontalWeight+spectral.displacement.x*horizontalSlope.x,
            spectral.derivativeX.y*horizontalWeight+spectral.displacement.y*horizontalSlope.x,heightSlope.x);
        float3 tangentY=float3(spectral.derivativeY.x*horizontalWeight+spectral.displacement.x*horizontalSlope.y,
            1.0+spectral.derivativeY.y*horizontalWeight+spectral.displacement.y*horizontalSlope.y,heightSlope.y);
        float3 oldDelta=float3(spectral.displacement.xy*horizontalWeight,lerp(oldHeight,newHeight,fftWeight));
        float3 cageDelta=float3(cagePhysical.displacement.xy*fftWeight,lerp(oldHeight,cagePhysical.displacement.z,fftWeight));
        float3 cageX=float3(1,0,baseSlope.x)+cagePhysical.derivativeX*fftWeight
            +float3(cagePhysical.displacement.xy,cagePhysical.displacement.z-oldHeight)*weightSlope.x;
        float3 cageY=float3(0,1,baseSlope.y)+cagePhysical.derivativeY*fftWeight
            +float3(cagePhysical.displacement.xy,cagePhysical.displacement.z-oldHeight)*weightSlope.y;
        cageX.z+=(slope.x-baseSlope.x)*(1.0-fftWeight);
        cageY.z+=(slope.y-baseSlope.y)*(1.0-fftWeight);
        tangentX=lerp(tangentX,cageX,cageAmount)+(cageDelta-oldDelta)*cageAmountSlope.x;
        tangentY=lerp(tangentY,cageY,cageAmount)+(cageDelta-oldDelta)*cageAmountSlope.y;
        normal=normalize(cross(tangentX,tangentY));
        // Filter the same displacement derivatives for reflected/direct light only. Keep
        // level-zero geometry, refraction, contact and optical path reconstruction intact.
        )HLSL";
    hlsl+=R"HLSL(float shadedHeight=spectral.shadingDisplacement.z*fftDamping+bowLiftField.x;)HLSL";
    hlsl+=R"HLSL(
        float2 shadedSlope=float2(spectral.shadingDerivativeX.z,spectral.shadingDerivativeY.z)*fftDamping
            )HLSL";
    hlsl+=R"HLSL(+spectral.shadingDisplacement.z*dampingSlope+bowLiftField.yz;)HLSL";
    hlsl+=R"HLSL(
        float2 shadedHeightSlope=baseSlope+lerp(slope-baseSlope,shadedSlope,fftWeight)
            +(shadedHeight-oldHeight)*weightSlope;
        float3 shadedX=float3(1.0+spectral.shadingDerivativeX.x*horizontalWeight+spectral.shadingDisplacement.x*horizontalSlope.x,
            spectral.shadingDerivativeX.y*horizontalWeight+spectral.shadingDisplacement.y*horizontalSlope.x,shadedHeightSlope.x);
        float3 shadedY=float3(spectral.shadingDerivativeY.x*horizontalWeight+spectral.shadingDisplacement.x*horizontalSlope.y,
            1.0+spectral.shadingDerivativeY.y*horizontalWeight+spectral.shadingDisplacement.y*horizontalSlope.y,shadedHeightSlope.y);
        float3 shadedOldDelta=float3(spectral.shadingDisplacement.xy*horizontalWeight,lerp(oldHeight,shadedHeight,fftWeight));
        float3 shadedCageDelta=float3(cageShaded.displacement.xy*fftWeight,lerp(oldHeight,cageShaded.displacement.z,fftWeight));
        float3 shadedCageX=float3(1,0,baseSlope.x)+cageShaded.derivativeX*fftWeight
            +float3(cageShaded.displacement.xy,cageShaded.displacement.z-oldHeight)*weightSlope.x;
        float3 shadedCageY=float3(0,1,baseSlope.y)+cageShaded.derivativeY*fftWeight
            +float3(cageShaded.displacement.xy,cageShaded.displacement.z-oldHeight)*weightSlope.y;
        shadedCageX.z+=(slope.x-baseSlope.x)*(1.0-fftWeight);
        shadedCageY.z+=(slope.y-baseSlope.y)*(1.0-fftWeight);
        shadedX=lerp(shadedX,shadedCageX,cageAmount)+(shadedCageDelta-shadedOldDelta)*cageAmountSlope.x;
        shadedY=lerp(shadedY,shadedCageY,cageAmount)+(shadedCageDelta-shadedOldDelta)*cageAmountSlope.y;
        resolvedNormal=normalize(cross(shadedX,shadedY));
        unresolvedSlopeVariance=spectral.unresolvedSlopeVariance*fftWeight*fftWeight*fftDamping*fftDamping;
        continuousHeight=baseLevel+lerp(oldHeight,lerp(newHeight,cagePhysical.displacement.z,cageAmount),fftWeight);
        fftCompression=lerp(1.0,spectral.incidentCompression,saturate(input.Specular.w));
    }
    // Reconstruct the same continuous wave between mesh vertices for optical integration. The
    // rendered scene bottom stays exact; using coarse triangle heights here exposes their edges
    // through absorption on the campaign shell's deliberately shallow offshore terrain. Move on
    // the camera ray, with a bounded correction at grazing angles, retaining raster occlusion.
    float opticalShift = WaterOpticalRayShift(continuousHeight-world.z,
        view.z, offshore, oceanDisplacementBound, dot(world-bottom,view), length(CameraPosition.xyz-world));
    float3 opticalWorld = world + view*opticalShift;
    float opticalDepth = max(opticalWorld.z-bottom.z,0.0);
    // Floor clearance flattens the mesh at the bank, but the incident wave pattern
    // must still catch light across the shallows. Restore its terrain-damped lighting
    // slopes with the same spectral phases, retaining wet-edge and obstacle/hull shelter.
    // Geometry, refraction and full offshore shading keep their original normals.
    float shoreRippleLimit = min(smoothstep(0.0,0.75,depth),min(input.Diffuse.r,hullEffects.z));
    float shoreRippleWeight = opticalContext.b * OpticalParameters.z * TerrainParameters.w
        * (1.0-WaterParameters.y) * (1.0-offshore) * (1.0-cageAmount*fftWeight);
    float2 shoreRipple = shoreRippleWeight * lerp(
        shoreRippleSlope * max(0.0,shoreRippleLimit-max(localTerrain,attenuation)),
        float2(spectral.shadingDerivativeX.z,spectral.shadingDerivativeY.z)
            * max(0.0,shoreRippleLimit-max(localTerrain,fftDamping)),fftWeight);
    float3 waveLightingNormal = resolvedNormal;
    if(any(shoreRipple!=0.0))
        resolvedNormal = normalize(resolvedNormal + float3(-shoreRipple*resolvedNormal.z,0.0));
    float materialBubbleWeight=modernWaterBubbleWeight(MaterialParameters.y,MaterialParameters.z,depth);
    ModernWaterBubbles materialBubbles=(ModernWaterBubbles)0;
    if(materialBubbleWeight>0.0) {
        materialBubbles=modernWaterBubbles(wavePosition,time,footprint);
        float3 capNormal=float3(-materialBubbles.slope*materialBubbleWeight,0);
        normal=normalize(normal+capNormal);
        resolvedNormal=normalize(resolvedNormal+capNormal);
        waveLightingNormal=normalize(waveLightingNormal+capNormal);
    }
    float3 light = normalize(SunDirection.xyz + float3(0, 0, 0.00001));
    float nDotV = max(dot(resolvedNormal, view), 0.001);
    float nDotL = saturate(dot(resolvedNormal, light));
    float3 halfway = normalize(view + light + float3(0, 0, 0.00001));
    float nDotH = saturate(dot(resolvedNormal, halfway));
    float vDotH = saturate(dot(view, halfway));
    float fresnel = 0.025 + 0.975 * pow(1.0 - saturate(nDotV), 5.0);
    // Keep the bed readable at the shallow bank instead of covering it with bright sky bands.
    fresnel = 0.025 + (fresnel - 0.025) * lerp(lerp(0.35, 1.0, smoothstep(3.0, 18.0, depth)), 1.0, offshore);
    float variance = dot(ddx(resolvedNormal), ddx(resolvedNormal)) + dot(ddy(resolvedNormal), ddy(resolvedNormal));
    float roughness = clamp(sqrt(lerp(0.016, 0.036, offshore) + variance * 0.24), 0.126, 0.34);
    roughness=lerp(roughness,max(roughness,0.27),max(hullEffects.y,historyFoam.z)*0.5);
    // Isotropic missing slope variance broadens alpha-squared (roughness^4 in this GGX
    // convention). Removing unresolved normals must not turn the surface into a mirror.
    if(unresolvedSlopeVariance>0.0) roughness=pow(min(1.0,pow(roughness,4.0)+0.5*unresolvedSlopeVariance),0.25);
    float a2 = pow(roughness, 4.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(3.14159265 * denominator * denominator, 0.0000001);
    float k = roughness * roughness * 0.5;
    float masking = nDotV / (nDotV * (1.0 - k) + k);
    masking *= nDotL / max(nDotL * (1.0 - k) + k, 0.0001);
    float sunFresnel = 0.02 + 0.98 * pow(1.0 - vDotH, 5.0);
    float sunVisible = terrainSun(world, light);
    float3 highlight = waterLinear(SunColour.rgb) * 2.0 * (distribution * masking * sunFresnel * sunVisible / max(4.0 * nDotV, 0.001));
    highlight = highlight / (1.0 + highlight * 0.6);
    float daylight = saturate(max(max(SunColour.r, SunColour.g), SunColour.b));
    // Integrate the short crossing ripples into the environment lobe. Their shared spectrum
    // still supplies physical height, refraction normals and directional sun glints, while the
    // broad sky follows the resolved swell surface instead of tracing every fine ripple edge.
    float rippleResolution=0.88/(1.0+footprint*0.18);
    float3 environmentWave=lerp(oceanSwell.xyz,oceanWave,rippleResolution);
    float2 swellSlope=baseSlope+environmentWave.yz*attenuation+environmentWave.x*attenuationSlope+correctionSlope;
    float3 environmentNormal=normalize(lerp(normal,normalize(float3(-swellSlope,1)),offshore));
    environmentNormal=normalize(lerp(environmentNormal,waveLightingNormal,fftWeight));
    if(any(shoreRipple!=0.0))
        environmentNormal=normalize(environmentNormal+float3(-shoreRipple*environmentNormal.z,0.0));
    // The remaining fine-slope energy is ~0.045 for this spectrum (including its packet
    // envelope); retaining a fraction of its amplitude leaves (1-fraction^2) as roughness.
    float environmentRoughness=sqrt(roughness*roughness+0.045*(1.0-rippleResolution*rippleResolution)*offshore*(1.0-fftWeight));
    float3 reflectedRay = reflect(-view, environmentNormal);
    float3 sky = lerp(float3(0.49, 0.62, 0.68), float3(0.23, 0.38, 0.48), saturate(reflectedRay.z)) * (0.06 + daylight * 0.94);
    float2 reflectionOffset = float2((ReflectionArea.x + 1.0) * 0.5, (1.0 - ReflectionArea.w) * 0.5);
    float2 reflectionScale = max((ReflectionArea.zw - ReflectionArea.xy) * 0.5, float2(0.001, 0.001));
    float2 reflectionUV = (screenUV - reflectionOffset) / reflectionScale;
    float2 distortion = pixelOffset(resolvedNormal.xy * 8.0, dx, dy) / max(Viewport.zw, float2(1, 1)) / reflectionScale;
    float2 blur = roughness * 0.008 / reflectionScale;
)HLSL";
	if (sdl) hlsl += R"HLSL(    float4 mirror=0.0, filteredMirror=0.0;
    float2 mirrorFilterSupport=abs(blur);
    bool filteredPlanar=WATER_SCENE_REFLECTIONS && MaterialParameters.w>0.5 && WaterParameters.z>0.5;
#if WATER_SCENE_REFLECTIONS
    [branch] if (filteredPlanar) {
        uint mirrorWidth,mirrorHeight;
        Texture1.GetDimensions(mirrorWidth,mirrorHeight);
        int2 mirrorSize=max(int2(mirrorWidth,mirrorHeight),int2(1,1));
        filteredMirror=waterMirrorFiltered(reflectionUV+distortion,mirrorSize)*0.5;
        filteredMirror+=waterMirrorFiltered(reflectionUV+distortion+blur,mirrorSize)*0.25;
        filteredMirror+=waterMirrorFiltered(reflectionUV+distortion-blur,mirrorSize)*0.25;
        mirrorFilterSupport+=1.0/float2(mirrorSize);
    } else if (WaterParameters.z>0.5) {
        // Keep the original reconstruction in Legacy.
        mirror=Texture1.Sample(Sampler1,saturate(reflectionUV+distortion))*0.5;
        mirror+=Texture1.Sample(Sampler1,saturate(reflectionUV+distortion+blur))*0.25;
        mirror+=Texture1.Sample(Sampler1,saturate(reflectionUV+distortion-blur))*0.25;
    }
#endif
    float2 border = min(reflectionUV, 1.0 - reflectionUV);
    // Legacy terrain passes preserve destination alpha for shore blending, including in the
    // mirror. Its transparent-black clear gives RGB coverage even when geometry cannot write A.
    float mirrorCoverage = filteredPlanar ? 1.0 : saturate(max(max(mirror.r, mirror.g), mirror.b) * 64.0);
    if (filteredPlanar)
        border=min(reflectionUV+distortion,1.0-reflectionUV-distortion)-mirrorFilterSupport;
    float coverage = saturate(min(border.x, border.y) * 30.0) * mirrorCoverage * WaterParameters.z;
    coverage *= 1.0 - smoothstep(0.5, 2.0, abs(baseLevel - WaterParameters.w));
    float3 skyRadiance=waterLinear(sky);
    if(offshore>0.0) skyRadiance=lerp(skyRadiance,oceanEnvironment(reflectedRay,light,daylight,environmentRoughness),offshore);
    float3 reflection;
    [branch] if (filteredPlanar) {
        // Remove coverage before decoding, then restore it in linear light. Decoding once
        // keeps the old interior colour filtering and avoids twelve gamma conversions.
        float3 planarRadiance=waterLinear(saturate(filteredMirror.rgb/max(filteredMirror.a,1e-6)))
            *filteredMirror.a;
        reflection=skyRadiance*(1.0-coverage*filteredMirror.a)+coverage*planarRadiance;
    } else reflection=lerp(skyRadiance,waterLinear(mirror.rgb),coverage);
    // MaterialParameters.w selects the traced reflection; hybrid also retains planar visibility.
    WaterSsrSample ssr = (WaterSsrSample)0;
#if WATER_SCENE_REFLECTIONS
    ssr = waterScreenReflection(world, view, environmentNormal, environmentRoughness, input.Position.xy);
#endif
    // A main-camera hit cannot certify a surface hidden from that camera (a bridge underside,
    // for example). Retain the planar contribution in hybrid until geometric correspondence
    // is available. Trust includes empty sky, so it is independent of the RGB coverage heuristic.
    float2 tracedMirrorUV=reflectionUV+distortion;
    float2 tracedMirrorBorder=min(tracedMirrorUV,1.0-tracedMirrorUV)-mirrorFilterSupport;
    float mirrorTrust=saturate(min(tracedMirrorBorder.x,tracedMirrorBorder.y)*30.0)*WaterParameters.z;
    mirrorTrust*=1.0-smoothstep(0.5,2.0,abs(baseLevel-WaterParameters.w));
    float ssrWeight=ssr.confidence*lerp(1.0,0.35,saturate(mirrorTrust));
    reflection = lerp(reflection, waterLinear(ssr.colour), ssrWeight);
)HLSL";
	else hlsl += R"HLSL(    float4 mirror = Texture1.Sample(Sampler1, saturate(reflectionUV + distortion)) * 0.5;
    mirror += Texture1.Sample(Sampler1, saturate(reflectionUV + distortion + blur)) * 0.25;
    mirror += Texture1.Sample(Sampler1, saturate(reflectionUV + distortion - blur)) * 0.25;
    float2 border = min(reflectionUV, 1.0 - reflectionUV);
    // Legacy terrain passes preserve destination alpha for shore blending, including in the
    // mirror. Its transparent-black clear gives RGB coverage even when geometry cannot write A.
    float mirrorCoverage = saturate(max(max(mirror.r, mirror.g), mirror.b) * 64.0);
    float coverage = saturate(min(border.x, border.y) * 30.0) * mirrorCoverage * WaterParameters.z;
    coverage *= 1.0 - smoothstep(0.5, 2.0, abs(baseLevel - WaterParameters.w));
    float3 skyRadiance=waterLinear(sky);
    if(offshore>0.0) skyRadiance=lerp(skyRadiance,oceanEnvironment(reflectedRay,light,daylight,environmentRoughness),offshore);
    float3 reflection = lerp(skyRadiance, waterLinear(mirror.rgb), coverage);
)HLSL";
	hlsl += R"HLSL(    // Refraction uses the actual optical depth and shared colour/depth viewport bounds.
)HLSL";
	if (sdl) hlsl += R"HLSL(    float2 refractionPixels = 0.0;
    WaterRefractionSample refractedScene = (WaterRefractionSample)0;
    refractedScene.bottom = bottom;
#if WATER_REFRACTION_QUALITY == 0
    refractionPixels = waterRefractionPixels(opticalDepth, view, normal, dx, dy);
    refractedScene = waterRefractedScene(input.Position.xy, refractionPixels,
        world, opticalWorld, view, baseLevel, bottom);
#elif WATER_REFRACTION_QUALITY == 1
    // Undistorted transmission reads the original scene exactly once. Depth remains
    // independent for submerged objects, intersections and foam.
    refractedScene.pixel = waterScenePixel(floor(input.Position.xy)+0.5);
    [branch] if (SceneParameters.x>0.5)
        refractedScene.colour = Texture5.Load(int3(int2(floor(refractedScene.pixel)),0)).rgb;
#endif
)HLSL";
	else hlsl += R"HLSL(    float2 refractionPixels = waterRefractionPixels(opticalDepth, view, normal, dx, dy);
    WaterRefractionSample refractedScene = waterRefractedScene(input.Position.xy, refractionPixels,
        world, opticalWorld, view, baseLevel, bottom);
)HLSL";
	hlsl += R"HLSL(    bottom = refractedScene.bottom;
    float bedDepth = max(opticalWorld.z-bottom.z, 0.0);
    float3 scene = waterLinear(refractedScene.colour);
)HLSL";
	if (sdl) hlsl += "#if WATER_REFRACTION_QUALITY != 2\n";
	hlsl += R"HLSL(    // Material detail is fixed to the *actual* submerged scene position. Keep object materials,
    // steep cliff faces, map tint, and visibility; only exposed bed terrain receives this detail.
    float bedHeight = terrainHeight(bottom.xy);
    float2 bedSlope = float2(terrainHeight(bottom.xy+float2(5,0))-terrainHeight(bottom.xy-float2(5,0)),
        terrainHeight(bottom.xy+float2(0,5))-terrainHeight(bottom.xy-float2(0,5))) * 0.1;
    float terrainMask = (1.0-smoothstep(0.6, 2.6, abs(bottom.z-bedHeight)))
        * (1.0-smoothstep(0.5, 1.6, length(bedSlope))) * TerrainParameters.w * SceneParameters.w;
    float2 bedUV = bottom.xy / 48.0;
    float4 material = Texture7.Sample(Sampler7, bedUV);
    float4 broadMaterial = Texture7.Sample(Sampler7, bedUV * 0.173 + float2(0.31,0.67));
    float gravel = saturate(WaterParameters.y * (0.25 + 0.8*broadMaterial.a) + length(bedSlope)*0.2);
    float detail = lerp(material.r / 0.55, material.g / 0.45, gravel);
    float2 bedRelief = float2(Texture7.Sample(Sampler7, bedUV+float2(1.0/256.0,0)).b - Texture7.Sample(Sampler7, bedUV-float2(1.0/256.0,0)).b,
        Texture7.Sample(Sampler7, bedUV+float2(0,1.0/256.0)).b - Texture7.Sample(Sampler7, bedUV-float2(0,1.0/256.0)).b);
    float3 bedNormal = normalize(float3(-bedSlope - bedRelief*gravel*1.8, 1.0));
    float bedLight = 0.7 + 0.45*saturate(dot(bedNormal, light));
    float rockVariation = lerp(0.82, 1.12, broadMaterial.a);
    float bedLuminance = dot(scene, float3(0.2126,0.7152,0.0722));
    scene = lerp(scene, bedLuminance*float3(1.10,1.0,0.80), terrainMask*gravel*0.38);
    scene *= lerp(1.0, clamp(detail*bedLight*rockVariation, 0.38, 1.65), terrainMask * 0.85);
    // Caustics use the evolving surface curvature at the light's entry point. Keep the modulation
    // restrained and attenuate it by depth/footprint to avoid a painted luminous network at RTS zoom.
    float3 causticWave = waterWave(bottom.xy-light.xy/max(light.z,0.2)*bedDepth, time);
    float causticFootprint = max(length(ddx(bottom.xy)), length(ddy(bottom.xy)));
    float3 causticNormal = normalize(float3(-causticWave.yz*attenuation, 1));
    float3 sunRay = refract(-light, causticNormal, 0.75);
    float2 bend = sunRay.xy / min(sunRay.z, -0.1) * bedDepth;
    float2 bx = ddx(bottom.xy), by = ddy(bottom.xy);
    float2 cx = bx+ddx(bend), cy = by+ddy(bend);
    float focused = abs(bx.x*by.y-bx.y*by.x) / max(abs(cx.x*cy.y-cx.y*cy.x),0.0001);
    float caustic = lerp(1.0, clamp(focused, 0.82, 1.55), exp(-bedDepth*0.055)
        * sunVisible * daylight / (1.0 + causticFootprint*0.45));
    // Absorption follows the underwater view-ray distance, including submerged object geometry.
    // Campaign seas can have deliberately flat, shallow terrain beneath full-size ships. Honour
    // their authored opacity offshore, while preserving the clear optical profile at coasts and
    // in enclosed pools. Actual scene ray length still governs submerged objects and refraction.
    float3 transmission = WaterOpticalTransmission(length(bottom-opticalWorld), WaterParameters.y,
        offshore, OpticalParameters.x, OpticalParameters.y);
)HLSL";
	if (sdl) hlsl += R"HLSL(#else
    float3 transmission = 0.0;
    float caustic = 1.0;
#endif
)HLSL";
	hlsl += R"HLSL(    // Preserve the current face-light signal, but apply it only to water's in-scattered source.
    // Transmitted terrain/object colour must not receive a second surface-light multiplier.
    float3 swellNormal=normalize(float3(-oceanSwell.yz*attenuation-correctionSlope,1));
    swellNormal=normalize(lerp(swellNormal,normal,fftWeight));
    float swellLight=saturate(dot(swellNormal,light));
    float3 scatter=WaterOpticalScatteringSource(WaterParameters.y,offshore,daylight*sunVisible,swellLight);
    scatter=modernWaterMaterialSource(scatter,MaterialColor,MaterialParameters.x,daylight*sunVisible,swellLight);
    if(MaterialParameters.y>0.0) {
        float materialPatch=oceanNoise(wavePosition*0.009+float2(time*0.008,-time*0.006));
        transmission=modernWaterMaterialTransmission(transmission,MaterialColor.rgb,MaterialParameters.y,
            length(bottom-opticalWorld),materialPatch);
    }
    float3 body=WaterOpticalBody(scene,transmission,scatter,caustic,SceneParameters.x);
    float foamNoise = smoothstep(0.1, 0.8, lerp(wave.x,spectral.contactIncident.x,fftWeight)*0.5+0.5);
    float edgeDistance = depth / max(length(float2(ddx(depth), ddy(depth))), 0.01);
    float contact = (1.0 - smoothstep(0.2, 1.35, edgeDistance)) * saturate(depth * 4.0)
        * max(TerrainParameters.w, SceneParameters.w);
    float foam = contact * (0.25 + 0.75 * foamNoise) * (0.2 + 0.8 * daylight);
    // Evaluate terrain derivatives outside the conditional so all neighbouring fragments
    // participate. Their world-space gradient gives the direction toward open water.
    float terrainDu=ddx(surfaceTerrainHeight),terrainDv=ddy(surfaceTerrainHeight);
    float2 terrainGradient=float2(terrainDu*parameterDy.y-terrainDv*parameterDx.y,
        terrainDv*parameterDx.x-terrainDu*parameterDy.x)*invDet;
    float shoreRate=0.0,shoreSupport=0.0;
    float shoreHistory=waterShoreHistory(wavePosition,baseLevel,time)*(1.0-fftWeight);
    float shoreRetained=0.0;
    // Same source composition with the coastal term omitted; retain this one scalar
    // through the real masks/max operations to attribute its final material weight.
    float foamWithoutShore=foam;
    // Contact wash follows the arriving broad FFT face on the deformed surface.
    // It is a bounded material approximation, not the paper's spray-particle solver.
    float cageWash=0.0;
    float broadSlope=length(spectral.contactIncident.yz);
    float terrainSlope=length(terrainGradient);
    float arrival=dot(spectral.contactIncident.yz,-terrainGradient)/max(broadSlope*terrainSlope,0.00001);
    float cageTerrainMatch=1.0-smoothstep(0.5,1.5,abs(shoreSceneBottom.z-terrainHeight(shoreSceneBottom.xy)));
    float cageWet=smoothstep(0.015,0.18,depth)*cageTerrainMatch*saturate(cageObstacle.x);
    float cageContact=1.0-smoothstep(0.2,2.5,max(0.0,world.z-surfaceTerrainHeight));
    float cageCrest=smoothstep(0.05,0.7,spectral.contactIncident.x)*smoothstep(0.0,0.35,arrival);
    float cagePores=smoothstep(0.24,0.72,oceanNoise(world.xy*0.6));
    cageWash=cageAmount*fftWeight*cageWet*cageContact*cageCrest*lerp(0.45,1.0,cagePores);
    foam=max(foam*(1.0-cageAmount*fftWeight*cageTerrainMatch),cageWash*0.72);
)HLSL";
	hlsl+=R"HLSL(
    if(WaterParameters.y<0.5 && OpticalParameters.z>0.5
        && TerrainParameters.w>0.5 && SceneParameters.w>0.5
        && opticalContext.r==0.0 && (opticalContext.a>0.0 || shoreHistory>0.0)) {
        // Test the original raster pixel, not the refracted candidate or bottomPosition's
        // terrain fallback. Clear/invalid depth must keep the prior rim unchanged.
        float2 shorePixel=waterScenePixel(floor(input.Position.xy)+0.5);
        float shoreRawDepth=Texture6.SampleLevel(Sampler6,shorePixel*SceneParameters.yz,0).r;
        if(waterShoreSceneDepth(SceneParameters.w,shoreRawDepth)) {
            shoreSupport=opticalContext.a*(1.0-fftWeight);
            float terrainDepth=max(0.0,baseLevel-surfaceTerrainHeight);
            float terrainSlope=length(terrainGradient);
            float shoreDistance=min(opticalContext.g*100.0,terrainDepth/max(terrainSlope,0.01));
            // These three phases follow only the remaining analytic mesh contribution.
            // Derivatives were evaluated outside this branch; filter their leading faces
            // without expanding terrain/wet support or creating a permanent coast rim.
            float2 shoreFront;
            float4 shoreWave=waterShoreWaveFront(wavePosition,time,-terrainGradient,parameterDx,parameterDy,shoreFront);
            shoreRate=waterShoreRate(shoreWave,terrainDepth,shoreDistance,-terrainGradient,
                terrainSlope,opticalContext.b,shoreSupport)*shoreFront.x;
            // Terrain matching is a heuristic: larger object/terrain separation keeps the
            // previous rim, while thin objects and nonplanar terrain need native validation.
            // The original real scene hit is independently required above; wetness still
            // gates emission, and terrain metadata alone cannot paint onto a dry fragment.
            float terrainMatch=1.0-smoothstep(0.5,1.5,
                abs(shoreSceneBottom.z-terrainHeight(shoreSceneBottom.xy)));
            shoreRetained=shoreHistory*smoothstep(0.0,0.8,depth)*terrainMatch;
            foam=waterShoreBlendFrontHistory(foam,shoreRate,shoreFront.y,shoreHistory,shoreSupport,smoothstep(0.0,0.8,depth),terrainMatch);
            foamWithoutShore*=1.0-saturate(shoreSupport)*saturate(terrainMatch);
        }
    }
    // At a known hull, use its irregular wave wash instead of also drawing the generic scene
    // intersection rim. Otherwise that continuous depth edge fills every intended foam gap.
    foam *= lerp(1.0,hullEffects.z,offshore);
    foamWithoutShore*=lerp(1.0,hullEffects.z,offshore);
    // Curvature of the undamped ocean model selects breaking crests. Including attenuation in
    // this derivative would invent bright rings around a ship's damping footprint.
    float curvature = -oceanSwell.w;
    float crest = smoothstep(0.045, 0.11, curvature) * smoothstep(1.0, 2.4, oceanSwell.x);
    // A narrow band at the swell's turning slope forms a crest line, rather than filling the
    // whole high-curvature cap with a disconnected white patch. Pixel derivatives soften its
    // edges as this world-space band becomes smaller than a pixel at RTS distances.
    float crestSlope=dot(oceanSwell.yz,float2(0.9304,0.3665));
    float crestBand=1.0-smoothstep(0.025,0.085+fwidth(crestSlope)*0.65,abs(crestSlope));
    float foamPatch = oceanNoise(world.xy*0.065 + float2(time*0.06,-time*0.025));
    float foamBreakup = smoothstep(0.35, 0.7, oceanNoise(world.xy*0.38+float2(-time*0.13,time*0.08)));
    float crestFoam = crest * crestBand * smoothstep(0.53, 0.76, foamPatch) * offshore
        * foamBreakup * smoothstep(0.06, 0.25, attenuation) * (0.2+0.8*daylight);
    // Ambient breaking follows the long/medium part of the geometry's horizontal
    // compression. Fine ripples shape its appearance without triggering whitecaps.
    float fftFoam=saturate(pow(max(0.0,1.2-fftCompression),7.2))
        *smoothstep(0.15,0.65,fftDamping)*saturate(input.Specular.w)*(0.2+0.8*daylight);
    crestFoam=lerp(crestFoam,fftFoam,fftWeight);
    foam = max(foam, crestFoam*0.48);
    foamWithoutShore=max(foamWithoutShore,crestFoam*0.48);
    // Generated bow/stern crests come from the displaced mesh's physical packet. Their
    // coverage is independent of illumination and joins the same lit surface as hull wash
    // and history; putting it through generic crest shading dimmed this response twice.
    float generatedFoam=vesselGeneratedFoam(spectral.vesselCrest,fftWeight,fftDamping);
    float freshFoam=max(hullEffects.x,generatedFoam);
    float vesselFoam=max(freshFoam,historyFoam.x);
    // Fresh hull wash can cover an old trail. Blend its age continuously as that source
    // becomes dominant instead of imprinting old holes into newly generated foam.
    float foamAge=historyFoam.y*24.0*(1.0-smoothstep(0.25,1.0,
        freshFoam/max(historyFoam.x,0.0001)));
    // Current solid hulls mask old trails if they cross another vessel's waterline.
    vesselFoam*=smoothstep(0.0,0.006,hullEffects.z);
)HLSL";
	hlsl += R"HLSL(
    // Actual triangle contact may meet the visible hull inside the mean footprint.
    // Keep the old mask for trails and pressure foam, and merge fresh contact after it.
    if(meshHullEffects.x>0.0) {
        freshFoam=max(freshFoam,meshHullEffects.x);
        vesselFoam=max(vesselFoam,meshHullEffects.x);
        foamAge=historyFoam.y*24.0*(1.0-smoothstep(0.25,1.0,
            freshFoam/max(historyFoam.x,0.0001)));
    }
)HLSL";
	hlsl += R"HLSL()HLSL";
    hlsl+=R"HLSL(    // This is foam on the additional displaced bow shoulder, not mean-plane hull wash.
    // Its height already includes F-D; the unchanged opaque scene depth hides solid hulls.
    float liftedBowFoam=fftBowLiftFoam(bowLiftField.x,fftWeight);
    if(liftedBowFoam>0.0) {
        freshFoam=max(freshFoam,liftedBowFoam);
        vesselFoam=max(vesselFoam,liftedBowFoam);
        foamAge=historyFoam.y*24.0*(1.0-smoothstep(0.25,1.0,freshFoam/max(historyFoam.x,0.0001)));
    }
    vesselFoam=waterFoamMicroCoverage(wavePosition,vesselFoam,foamFootprint,foamAge,time);)HLSL";
    hlsl+=R"HLSL(
    foam=max(foam,vesselFoam);
    foamWithoutShore=max(foamWithoutShore,vesselFoam);
)HLSL";
	hlsl+=R"HLSL(
    float shoreMaterialWeight=waterFoamCoastWeight(foam,foamWithoutShore,vesselFoam);
    // Analytic fine normals can face away from the eye between geometric vertices. Fresnel alone
    // approaches one there and paints a bright band on a hidden wave face. Smith visibility
    // smoothly removes that reflected energy while retaining the underlying water body.
    float oceanViewCosine = saturate(dot(environmentNormal,view));
    float environmentK=environmentRoughness*environmentRoughness*0.5;
    float oceanViewVisibility = oceanViewCosine / max(oceanViewCosine*(1.0-environmentK)+environmentK,0.0001);
    float environmentFresnel=0.025+0.975*pow(1.0-oceanViewCosine,5.0);
    float visibleFresnel = lerp(fresnel,environmentFresnel*oceanViewVisibility,offshore);
    float3 surface = lerp(body, reflection, visibleFresnel) + highlight;
    if(materialBubbleWeight>0.0) {
        float film=saturate(materialBubbles.rim*0.16+materialBubbles.cap*0.045+materialBubbles.ripple*0.065)*materialBubbleWeight;
        surface=lerp(surface,modernWaterBubbleFilm(MaterialColor.rgb)*(0.12+0.88*daylight*sunVisible),film);
    }
    // Foam replaces transmission and the clear-water material locally. Its diffuse and rough
    // reflected light follow the same displaced surface; neither is additive white emission.
    float foamDiffuse=(0.22+0.78*saturate(dot(normal,light)))*(0.12+0.88*daylight);
    float3 foamAlbedo=lerp(float3(0.67,0.73,0.71),float3(0.79,0.83,0.81),historyFoam.z);
    // Newly deposited contact foam shares the fresh material even on a stationary
    // ship. Turbulence alone previously left it at the older, darker endpoint.
    float foamFreshness=max(max(historyFoam.z,hullEffects.y),1.0-smoothstep(2.0,8.0,foamAge));
    float3 vesselAlbedo=lerp(float3(0.75,0.80,0.77),float3(0.88,0.91,0.87),foamFreshness);
    // Preserve the authored visible directional-light increment. Offshore foam replaces
    // its ambient with hemispherical illumination from the same sky as the water reflection.
    float3 vesselIrradiance=waterFoamIrradiance(normal,light,HullParameters.yzw,SunColour.rgb,
            sunVisible*FoamFillLights[0].w,FoamFillLights[0].xyz,FoamFillLights[1].rgb,
            FoamFillLights[2].xyz,FoamFillLights[3].rgb);
 )HLSL";
	hlsl+=R"HLSL(
    if(vesselFoam>0.0 && offshore>0.0)
        vesselIrradiance=waterFoamReplaceAmbient(vesselIrradiance,HullParameters.yzw,
            waterFoamSkyIrradiance(normal,light,daylight),offshore);
 )HLSL";
	hlsl+=R"HLSL(
    float3 vesselDiffuse=waterLinear(vesselAlbedo)*vesselIrradiance;
    float3 vesselRadiance=vesselDiffuse;
    float3 shoreRadiance=waterLinear(foamAlbedo)*foamDiffuse;
    if(vesselFoam>0.0 || shoreMaterialWeight>0.0) {
        // The existing sky and planar scene remain coherent with the waves. Foam has a
        // broader material lobe, including reflected objects; retaining sharp mirror samples
        // here would turn its diffuse body into a glossy painted patch.
        float foamRoughness=max(environmentRoughness,0.55);
        float3 foamSky=waterLinear(sky);
        if(offshore>0.0) foamSky=lerp(foamSky,
            oceanEnvironment(reflectedRay,light,daylight,foamRoughness),offshore);
        float foamLevelCoverage=WaterParameters.z
            *(1.0-smoothstep(0.5,2.0,abs(baseLevel-WaterParameters.w)));
)HLSL";
	if (sdl) hlsl += R"HLSL(        float3 foamReflection=foamSky;
#if WATER_SCENE_REFLECTIONS
        foamReflection=waterFoamPlanarReflection(foamSky,reflectionUV,distortion,
            reflectionScale,foamRoughness,foamLevelCoverage);
#endif
)HLSL";
	else hlsl += R"HLSL(        float3 foamReflection=waterFoamPlanarReflection(foamSky,reflectionUV,distortion,
            reflectionScale,foamRoughness,foamLevelCoverage);
)HLSL";
	hlsl += R"HLSL(        vesselRadiance=waterFoamRoughMaterial(vesselDiffuse,foamReflection,
            environmentFresnel,oceanViewCosine,foamRoughness);
        // Share the filtered incident reflection; exchange Fresnel only once.
        if(shoreMaterialWeight>0.0)
)HLSL";
	hlsl+="            shoreRadiance=waterFoamRoughMaterial(waterLinear(foamAlbedo)*vesselIrradiance,\n";
	hlsl+=R"HLSL(                foamReflection,environmentFresnel,oceanViewCosine,foamRoughness);
    }
    // Mix the lit surface contributions; independently mixing albedo and irradiance
    // before multiplying would add cross terms where vessel and crest foam overlap.
    if(shoreMaterialWeight>0.0)
        surface=waterDisplay(waterFoamCoastLayer(surface,waterLinear(foamAlbedo)*foamDiffuse,
            vesselRadiance,shoreRadiance,foam,vesselFoam,shoreMaterialWeight));
    else
        surface=waterDisplay(waterFoamLayer(surface,waterLinear(foamAlbedo)*foamDiffuse,
            vesselRadiance,foam,vesselFoam));
    float4 current;
)HLSL";
	if (river) {
		hlsl += "    float4 edge = Texture3.Sample(Sampler3, float2(2.0*input.TexCoord0.x, input.TexCoord0.y));\n";
		hlsl += "    current = float4(saturate(surface) * input.Diffuse.a, edge.a * saturate(depth * 2.0) * lerp(base.a, step(0.001, base.a), " + std::string(sdl ? "WATER_REFRACTION_QUALITY == 2 ? 1.0 : SceneParameters.x" : "SceneParameters.x") + "));\n";
	} else {
		hlsl += "    float4 edge = Texture3.Sample(Sampler3, input.TexCoord3);\n";
		hlsl += "    current = float4(saturate(surface) * edge.rgb, saturate(depth * 2.0) * lerp(input.Diffuse.a * base.a, step(0.001, input.Diffuse.a * base.a), " + std::string(sdl ? "WATER_REFRACTION_QUALITY == 2 ? 1.0 : SceneParameters.x" : "SceneParameters.x") + "));\n";
	}
}

bool EngineShader_Pixel_Program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, CombinerShaderTarget target)
{
	// As the vertex half: D3D11 only, and SDL3 GPU as the D3D11 text rebound, without the smoke the
	// SDL3 backend has no map for.
	if (target == COMBINER_SHADER_TARGET_D3D9) {
		hlsl.clear();
		return false;
	}
	const bool modern_water = EngineShader_Is_Modern_Water(program);
	if (target == COMBINER_SHADER_TARGET_SDL3_GPU) {
#if defined(__linux__)
		if (modern_water) {
			write_modern_water(hlsl, program == ENGINE_SHADER_WATER_RIVER, true);
			if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) return false;
			hlsl += "    return current;\n}\n";
			return SDL3_Shader_Retarget(hlsl, false);
		}
#endif // __linux__
		return write_engine_pixel_program(program, pipeline, hlsl, EngineShader_Paints_Ground(program))
			&& SDL3_Shader_Retarget(hlsl, false);
	}
	// Direct3D 11 draws the Modern water from the same text, bound by dx11water.cpp.  The Classic
	// style registers the classic names, whose transcription is the one this target always wrote.
	if (modern_water) {
		write_modern_water(hlsl, program == ENGINE_SHADER_WATER_RIVER);
		if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) return false;
		hlsl += "    return current;\n}\n";
		return true;
	}
	return write_engine_pixel_program(program, pipeline, hlsl, true);
}

static bool write_engine_pixel_program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, bool volumetric)
{
	hlsl.clear();

	const EngineShaderEntry * entry = entry_for(program);
	if (entry != NULL && entry->Opening != NULL) {
		write_multiply_chain(hlsl, *entry, volumetric);
	}
	else if (program == ENGINE_SHADER_WATER_TRAPEZOID || program == ENGINE_SHADER_WATER_CLASSIC_TRAPEZOID) {
		write_trapezoid_water(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_RIVER || program == ENGINE_SHADER_WATER_CLASSIC_RIVER) {
		write_river_water(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_REFLECTION) {
		write_water_reflection(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_TRAPEZOID_FOAM) {
		if (volumetric) {
			write_trapezoid_water_foam(hlsl);
		}
		else {
			write_trapezoid_water(hlsl);	// SDL3: no scene depth and no WorldFromClip in its block
		}
	}
	else if (program == ENGINE_SHADER_WATER_RIVER_FOAM) {
		if (volumetric) {
			write_river_water_foam(hlsl);
		}
		else {
			write_river_water(hlsl);
		}
	}
	else if (program == ENGINE_SHADER_MONOCHROME) {
		write_monochrome(hlsl);
	}
	else {
		return false;
	}

	if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) {
		return false;
	}

	hlsl +=
		"    return current;\n"
		"}\n";
	return true;
}

const char * EngineShader_Name(EngineShaderProgram program)
{
	const EngineShaderEntry * entry = entry_for(program);
	return (entry != NULL) ? entry->Name : "engine:none";
}
