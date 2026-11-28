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

#include "dx11post.h"
#include "postshader.h"

#include "dx11device.h"
#include "dx11resource.h"
#include "dx11runtime.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <windows.h>

typedef HRESULT (WINAPI *D3DCompileFunction)(LPCVOID source_data, SIZE_T source_size,
	LPCSTR source_name, const D3D_SHADER_MACRO * defines, ID3DInclude * include,
	LPCSTR entry_point, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob ** code,
	ID3DBlob ** error_messages);

// The same hand binding dx11backend.cpp does, and deliberately its own copy: this is a separate
// static library and linking it against the backend to share twelve lines would drag the whole
// state translation in behind them.
static const char * const COMPILER_MODULE = "d3dcompiler_47.dll";
static const char * const ENTRY_POINT = "main";
static const char * const VERTEX_PROFILE = "vs_4_0";
static const char * const PIXEL_PROFILE = "ps_4_0";

static const unsigned FULL_SCREEN_VERTEX_COUNT = 3;

// The swap chain's own format for the eight bit stages, so the last pass in a chain writes what it
// reads and nothing converts anything on the way out.
static const DXGI_FORMAT EIGHT_BIT_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

// Half floats rather than full ones: the scene is a colour, sixteen bits of it are more range than
// this game's art can fill, and it halves the bandwidth of every pass over the frame.
static const DXGI_FORMAT FLOAT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

// Bloom runs at a quarter of the window on each axis.  A glow is low frequency by definition, so
// the resolution it is computed at is the one thing about it nobody can see, and a quarter costs a
// sixteenth of the samples.
static const unsigned BLOOM_DIVISOR = 4;

// Pairs of blur passes, one horizontal and one vertical, the n-th pair stepping n texels between
// its taps.  Two reach about fifteen pixels of the finished frame; the Glow option's Medium asks
// for three, High and Ultra for four, about twenty five and thirty five.  More than four and a
// fire's halo covers the units fighting in it.
static const unsigned BLOOM_BLUR_PASSES = 2;
static const unsigned BLOOM_BLUR_PASS_LIMIT = 4;

// How far past the threshold the bright pass takes to reach full strength.  A pixel just over it
// keeps the square of its excess, so a flame flickering across the threshold fades its glow in
// and out instead of switching it.
static const float BLOOM_KNEE = 0.5f;

// What counts as bright enough to bleed, what the bleed is worth when it is added back, and where
// the tone curve starts bending.
//
// A threshold of one means brighter than white, which is a thing an eight bit target could not have
// said at all, and it was worth checking that the game ever gets there rather than assuming it.
// Eight inferno cannons firing into a column does not: at a threshold of one the frame comes back
// unchanged even with the intensity at four, because a fire a few pixels across never stacks enough
// additive particles to pass white.  Thirty of them does, and at the same threshold of one the
// fireballs come back with white centres and a glow on the ground around them.  So the float target
// earns its place, and the threshold stays where it is: a lower one blooms pale buildings and the
// sky, which is the look this is trying not to have.
//
// An intensity of 1.5 against a quiet frame with nothing burning costs half a level a channel,
// which is the cost of having this on when there is nothing for it to do.
//
// Those two are the defaults.  The game replaces them every frame from its Glow option, which
// also raises what an additive draw is worth (DX11Post_Additive_Gain): that, and not a lower
// threshold, is what gets fire and lasers past white while the art stays under it.
static const float BLOOM_THRESHOLD = 1.0f;
static const float BLOOM_INTENSITY = 1.5f;
static const float TONE_CURVE_KNEE = 0.8f;

static float BloomThreshold = BLOOM_THRESHOLD;
static float BloomIntensity = BLOOM_INTENSITY;
static float BloomAdditiveGain = 1.0f;
static unsigned BloomBlurPasses = BLOOM_BLUR_PASSES;
static float BloomSaturation = 1.0f;

// The bright pass's colour is pushed this far from its own grey at most.  ZH Enhanced's ReShade bloom
// runs at 1.53; past about 3 a fire's halo turns a red no flame has.
static const float BLOOM_SATURATION_LIMIT = 3.0f;

void DX11Post_Set_Bloom(float threshold, float intensity, float additive_gain, unsigned blur_passes,
	float saturation)
{
	BloomThreshold = (threshold > 0.0f) ? threshold : 0.0f;
	BloomIntensity = (intensity > 0.0f) ? intensity : 0.0f;
	BloomAdditiveGain = (additive_gain > 1.0f) ? additive_gain : 1.0f;
	BloomBlurPasses = (blur_passes < 1) ? 1
		: (blur_passes > BLOOM_BLUR_PASS_LIMIT) ? BLOOM_BLUR_PASS_LIMIT : blur_passes;
	BloomSaturation = (saturation < 0.0f) ? 0.0f
		: (saturation > BLOOM_SATURATION_LIMIT) ? BLOOM_SATURATION_LIMIT : saturation;
}

// Clarity: local contrast on the luminance, measured against a blur of it about fifteen pixels wide
// at 1080p (two of the bloom's blur pairs at a quarter of the window), so what it lifts is the shape
// of a tank, a crater or a cliff rather than the grain of a texture, which the sharpen pass owns.
// Zero, the default and what the tests start from, is no pass at all and leaves the bloom's programs
// drawing what they drew.  The game sets it every frame from the LightingStyle option.
static const unsigned CLARITY_BLUR_PASSES = 2;
static const float CLARITY_LIMIT = 2.0f;
static float ClarityStrength = 0.0f;

void DX11Post_Set_Clarity(float strength)
{
	ClarityStrength = (strength < 0.0f) ? 0.0f : (strength > CLARITY_LIMIT) ? CLARITY_LIMIT : strength;
}

float DX11Post_Additive_Gain()
{
	return (BloomIntensity > 0.0f) ? BloomAdditiveGain : 1.0f;
}

// How far the grade bends the picture at full strength: the power the S raises each side of its
// pivot to, less one, what is left of the colour around its grey, and how far a channel moves cool
// for each step under the pivot (a step over it moves nothing).
static const float GRADE_CONTRAST = 0.18f;
static const float GRADE_SATURATION = 0.86f;
static const float GRADE_SPLIT = 0.05f;

static float GradeAmount = 0.0f;

void DX11Post_Set_Grade(float amount)
{
	GradeAmount = (amount < 0.0f) ? 0.0f : (amount > 1.0f) ? 1.0f : amount;
}

static bool InternalHdr = false;

void DX11Post_Set_Internal_Hdr(bool on)
{
	InternalHdr = on;
}

static float HazeColour[3] = { 0.0f, 0.0f, 0.0f };
static float HazeTop = 0.0f;
static float HazeThickness = 1.0f;
static float HazeStrength = 0.0f;
static float HazeReach = 1.0f;
static float HazeWater = -1.0e6f;

static float HazeSun[4] = { 0.0f, 0.0f, 1.0f, 0.0f };

void DX11Post_Set_Haze(const float colour[3], float top, float thickness, float strength, float reach,
	float water, const float toward_sun[3], float warmth)
{
	HazeWater = water;
	HazeColour[0] = colour[0];
	HazeColour[1] = colour[1];
	HazeColour[2] = colour[2];
	HazeTop = top;
	HazeThickness = thickness;
	HazeStrength = strength;
	HazeReach = reach;
	HazeSun[0] = toward_sun[0];
	HazeSun[1] = toward_sun[1];
	HazeSun[2] = toward_sun[2];
	HazeSun[3] = warmth;
}

static ID3D11ShaderResourceView * HazeShroudSource = NULL;
static float HazeShroudRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

void DX11Post_Set_Haze_Shroud(ID3D11ShaderResourceView * view, const float rect[4])
{
	HazeShroudSource = view;
	memcpy(HazeShroudRect, rect, sizeof(HazeShroudRect));
}

static float AtmosphereHorizon[3] = { 0.0f, 0.0f, 0.0f };
static float AtmosphereTint[3] = { 0.0f, 0.0f, 0.0f };
static float AtmosphereFade[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
static float AtmosphereMapSize[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static ID3D11ShaderResourceView * AtmosphereSky = NULL;

void DX11Post_Set_Atmosphere_Edge(float border, float reach)
{
	AtmosphereMapSize[2] = border;
	AtmosphereMapSize[3] = reach;
}

void DX11Post_Set_Atmosphere(const float horizon[3], const float tint[3], float start, float scale,
	float strength, float ground, float map_width, float map_height, ID3D11ShaderResourceView * sky)
{
	AtmosphereMapSize[0] = map_width;
	AtmosphereMapSize[1] = map_height;
	memcpy(AtmosphereHorizon, horizon, sizeof(AtmosphereHorizon));
	memcpy(AtmosphereTint, tint, sizeof(AtmosphereTint));
	AtmosphereFade[0] = start;
	AtmosphereFade[1] = scale;
	AtmosphereFade[2] = strength;
	AtmosphereFade[3] = ground;
	AtmosphereSky = sky;
}

static DX11PostSunShafts SunShafts = { { 0.0f, 0.0f, 0.0f }, 0.0f };
// The sun's matrix and bias handed to the frame's Sun_Shafts_Scene.
static float SunShaftSunFromWorld[16] = { 0.0f };
static float SunShaftWorldFromSun[16] = { 0.0f };
static float SunShaftBias = 0.0f;
static float SunShaftShadowStrength = 0.0f;
// The terrain heights' mapping (cells a world unit, border cells, world units a byte) and one when the
// heights are bound.
static float SunShaftTerrain[4] = { 0.0f };

// The inverse of a row-vector matrix by Gauss-Jordan with partial pivoting; false, and the identity, when
// it has none.
static bool invert_shaft_matrix(const float m[16], float out[16])
{
	double work[4][8];
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			work[row][column] = m[row * 4 + column];
			work[row][column + 4] = (row == column) ? 1.0 : 0.0;
		}
	}
	for (unsigned column = 0; column < 4; ++column) {
		unsigned pivot = column;
		for (unsigned row = column + 1; row < 4; ++row) {
			if (fabs(work[row][column]) > fabs(work[pivot][column])) {
				pivot = row;
			}
		}
		if (fabs(work[pivot][column]) < 1.0e-12) {
			for (unsigned index = 0; index < 16; ++index) {
				out[index] = (index % 5 == 0) ? 1.0f : 0.0f;
			}
			return false;
		}
		for (unsigned index = 0; index < 8; ++index) {
			const double held = work[column][index];
			work[column][index] = work[pivot][index];
			work[pivot][index] = held;
		}
		const double scale = 1.0 / work[column][column];
		for (unsigned index = 0; index < 8; ++index) {
			work[column][index] *= scale;
		}
		for (unsigned row = 0; row < 4; ++row) {
			if (row != column) {
				const double factor = work[row][column];
				for (unsigned index = 0; index < 8; ++index) {
					work[row][index] -= factor * work[column][index];
				}
			}
		}
	}
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			out[row * 4 + column] = static_cast<float>(work[row][column + 4]);
		}
	}
	return true;
}

// The most steps a march takes, whatever it is handed.
static const float SUN_SHAFT_MOST_SAMPLES = 32.0f;

static float clamp_shaft(float value, float lo, float hi)
{
	return (value < lo) ? lo : (value > hi) ? hi : value;
}

// The hard bounds, whatever the game or a tuning key hands in, so no value can blow the picture out:
// the strength, the band, the one-sided share, the radius and the glare stop at the owner's most (a gap
// about +30 levels, the glare about +8), the lift as a whole under SHAFT_MOST_LIFT in the composite, the
// dimming under a share of the receivers' own shadow.  The colour is scaled so its brightest channel is
// at most one, so a map sun brighter than white does not lift the ceiling with it.  A key can make it
// weaker or reshape it, never brighter.
void DX11Post_Set_Sun_Shafts(const DX11PostSunShafts & shafts)
{
	SunShafts = shafts;
	float brightest = 1.0f;
	for (unsigned channel = 0; channel < 3; ++channel) {
		brightest = (shafts.Colour[channel] > brightest) ? shafts.Colour[channel] : brightest;
	}
	for (unsigned channel = 0; channel < 3; ++channel) {
		SunShafts.Colour[channel] = clamp_shaft(shafts.Colour[channel] / brightest, 0.0f, 1.0f);
	}
	SunShafts.Strength = clamp_shaft(shafts.Strength, 0.0f, 1.3f);
	SunShafts.Band = clamp_shaft(shafts.Band, 0.0f, 0.12f);
	SunShafts.Radius = clamp_shaft(shafts.Radius, 1.0f, 60.0f);
	SunShafts.StreakWidth = clamp_shaft(shafts.StreakWidth, 0.0f, 100.0f);
	SunShafts.StreakLength = clamp_shaft(shafts.StreakLength, 1.0f, 1000.0f);
	SunShafts.Slack = clamp_shaft(shafts.Slack, 0.0f, 100.0f);
	SunShafts.Side = clamp_shaft(shafts.Side, 0.0f, 0.3f);
	SunShafts.Dark = clamp_shaft(shafts.Dark, 0.0f, 0.25f);
	SunShafts.DarkCap = clamp_shaft(shafts.DarkCap, 0.0f, 0.35f);
	SunShafts.ShadedMost = clamp_shaft(shafts.ShadedMost, 0.0f, 4.0f);
	SunShafts.Top = clamp_shaft(shafts.Top, 0.0f, 1000.0f);
	SunShafts.Glare = clamp_shaft(shafts.Glare, 0.0f, 0.08f);
	SunShafts.Samples = (shafts.Samples < 1.0f) ? 1.0f
		: (shafts.Samples > SUN_SHAFT_MOST_SAMPLES) ? SUN_SHAFT_MOST_SAMPLES : floorf(shafts.Samples);
	SunShafts.Falloff = clamp_shaft(shafts.Falloff, 0.0001f, 1000.0f);
	SunShafts.WaterCount = (shafts.WaterCount < 0.0f) ? 0.0f
		: (shafts.WaterCount > DX11_POST_SUN_SHAFT_WATER_AREAS) ? (float)DX11_POST_SUN_SHAFT_WATER_AREAS : floorf(shafts.WaterCount);
}

static DX11PostWarp Warps[DX11_POST_WARP_LIMIT];
static unsigned WarpCount = 0;

void DX11Post_Set_Warps(const DX11PostWarp * warps, unsigned count)
{
	WarpCount = (count > DX11_POST_WARP_LIMIT) ? DX11_POST_WARP_LIMIT : count;
	for (unsigned index = 0; index < WarpCount; ++index) {
		Warps[index] = warps[index];
	}
}

// How dark a fully occluded pixel goes, how far a neighbour may be in front before it counts as a
// different object rather than a corner, and how wide the ring reaches at one unit of depth.  The
// clip planes come from the frame itself.
static const float AO_STRENGTH = 0.45f;
static const float AO_RADIUS = 14.0f;
static const float AO_SPREAD = 26.0f;

struct PostConstantBlock
{
	// One over the size of the texture being sampled, and its size.  Per pass rather than per
	// frame, because the bloom passes do not run at the size of the window.
	float TexelSize[4];

	// The step a separable blur takes, already in texture coordinates.  Then two numbers the
	// prologue's text could not grow fields for without moving every cached program's key: z is the
	// bloom's saturation, which the bright pass reads; w is the clarity's strength in the composite,
	// and in a blur pass whether it carries the clarity's luminance along (above nought) or leaves it.
	float BlurDirection[4];

	// Threshold, intensity, knee.
	float Tuning[4];

	// How many warps, and the width of the frame over its height.  Then per warp the centre, the
	// radius and the pull, and the ring's radius, width and strength.
	float WarpInfo[4];
	float WarpCentre[DX11_POST_WARP_LIMIT][4];
	float WarpRing[DX11_POST_WARP_LIMIT][4];

	// The grade's amount, contrast, saturation and warm/cool split.
	float Grade[4];

	// The ground haze: its top, thickness, strength and reach, its colour, and the matrix taking
	// the frame's clip space back to the world.  Only the haze program declares them (at c13, in a
	// cbuffer of its own over the same buffer), so every other pass compiles from the text it
	// always did.
	float Haze[4];
	float HazeColour[4];
	float WorldFromClip[16];
	// Then the way toward the sun with its warmth.
	float HazeSun[4];
	// The shroud light's world rectangle (origin, one over size).
	float HazeShroudRect[4];
	// How the scene's projection scales x and y, for the occlusion in world units to find its reach
	// on the screen.  Only that program declares it.
	float WorldAoProjection[4];
	// The sun shafts' own, from c22, which only their programs declare: the matrix taking the world
	// into the sun's clip space (c22), the layer (ground, top, falloff, steps, c26), the sun's bearing
	// with the glare and the map's bias (c27), the scale, band, dark and slack (c28), the glare's
	// steepness knee, the water count and the one-sided share (c29), the colour with the strength (c30),
	// then the water areas' boxes (c31) and heights (c39, in x).
	float SunFromWorld[16];
	float Shafts[4];
	float ShaftSun[4];
	float ShaftLook[4];
	float ShaftGlare[4];
	float ShaftColour[4];
	float ShaftWater[DX11_POST_SUN_SHAFT_WATER_AREAS][4];
	float ShaftWaterHeight[DX11_POST_SUN_SHAFT_WATER_AREAS][4];
	// The dimming's most (a share of the receivers' shadow), the most shaded dust a ray counts, the
	// layer's floor and the normal offset of the surface's own sun read (c47).
	float ShaftLimit[4];
	// The band's radius across the bearing and the streaks' width and length (c48).
	float ShaftBand[4];
	// The terrain heights' mapping and whether they are bound (c49), and the sun's clip space back to
	// the world (c50), which turns a sun map texel and its depth into the caster that wrote it.
	float ShaftTerrain[4];
	float WorldFromSun[16];
};

// A triangle big enough to cover the screen, made out of nothing.  No vertex buffer and no input
// layout: SV_VertexID walks 0, 1, 2 and the arithmetic turns them into (0,0), (2,0), (0,2) in
// texture coordinates, whose triangle covers the whole of the unit square.
static const char * const VERTEX_SHADER_SOURCE = PostShader_Vertex_Source();
static const char * const PIXEL_SHADER_PROLOGUE = PostShader_Pixel_Prologue();
static const char * const COPY_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_COPY);
static const char * const TONE_MAP_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_TONE_MAP);
static const char * const TONE_MAP_WIDE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_TONE_MAP_WIDE);
static const char * const FXAA_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_FXAA);
static const char * const SHARPEN_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SHARPEN);
static const char * const SHARPEN_MULTISAMPLED_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SHARPEN_MULTISAMPLED);
static const char * const AO_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_AO);
static const char * const AO_SCENE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_AO_SCENE);
static const char * const WORLD_AO_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO);
static const char * const WORLD_AO_BLUR_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO_BLUR);
static const char * const WORLD_AO_UPSAMPLE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WORLD_AO_UPSAMPLE);
static const char * const HAZE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HAZE);
static const char * const SKY_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_SKY);
static const char * const ATMOSPHERE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_ATMOSPHERE);
static const char * const BLOOM_EXTRACT_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_EXTRACT);
static const char * const BLOOM_BLUR_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_BLUR);
static const char * const BLOOM_COMPOSITE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_COMPOSITE);
static const char * const BLOOM_COMPOSITE_WIDE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_BLOOM_COMPOSITE_WIDE);
static const char * const WARP_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_WARP);
static const char * const HEAT_SPLAT_VERTEX_SOURCE = PostShader_Heat_Splat_Vertex_Source();
static const char * const HEAT_SPLAT_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HEAT_SPLAT);
static const char * const HEAT_HAZE_SHADER_BODY = PostShader_Pixel_Body(POST_SHADER_HEAT_HAZE);

// The shroud's three programs (Soften_Shroud, Shroud_Memory_Scene).  First the easing, a texel a
// cell: Source is the terrain's shroud texture as logic last wrote it, Extra the level each cell
// showed last frame, and each channel moves toward the texture's by BlurDirection.x of the gap
// (one less the decay over the frame's seconds) but never by less than BlurDirection.y (the frame's
// share of a whole swing), so a change runs out in a few tenths of a second whatever the frame rate
// and lands exactly.  Sixteen bits a channel, so a slow frame rate's small steps are not lost.
static const char * const SHROUD_EASE_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int3 texel = int3(input.Position.xy, 0);\n"
	"    float4 wanted = Source.Load(texel);\n"
	"    float4 held = Extra.Load(texel);\n"
	"    float4 gap = wanted - held;\n"
	"    float4 step = min(abs(gap), max(abs(gap) * BlurDirection.x, BlurDirection.y));\n"
	"    return held + sign(gap) * step;\n"
	"}\n";

// Then the eased cells up to BlurDirection.x texels a cell through a cubic B-spline over the
// sixteen round each texel, which is about a gaussian of 0.58 cells: the fog's edge is a round ramp
// with no step in its slope, where the texture's own bilinear filter drew a straight one cell ramp
// that turned a corner at every cell and showed the grid as a staircase.  The spline is centred, as
// the bilinear ramp was, so the edge sits where the logic puts it.  It never overshoots, so clear
// ground stays at one and shrouded at nought.
static const char * const SHROUD_SOFTEN_SHADER_BODY =
	"float4 spline(float f)\n"
	"{\n"
	"    float g = 1.0 - f;\n"
	"    float f2 = f * f;\n"
	"    float f3 = f2 * f;\n"
	"    return float4(g * g * g, 3.0 * f3 - 6.0 * f2 + 4.0, -3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0, f3) / 6.0;\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 at = input.Position.xy / BlurDirection.x - 0.5;\n"
	"    float2 base = floor(at);\n"
	"    float4 across = spline(at.x - base.x);\n"
	"    float4 down = spline(at.y - base.y);\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    float4 sum = 0.0;\n"
	"    for (int y = 0; y < 4; ++y)\n"
	"    {\n"
	"        float4 row = 0.0;\n"
	"        for (int x = 0; x < 4; ++x)\n"
	"            row += across[x] * Source.Load(int3(clamp(int2(base) + int2(x - 1, y - 1), int2(0, 0), last), 0));\n"
	"        sum += down[y] * row;\n"
	"    }\n"
	"    return saturate(sum);\n"
	"}\n";

// Last the memory, blended over the scene target by its alpha at the haze's moment, before the haze:
// ground and the ghosts of buildings in the fog are what the player last saw, and they read as that
// when most of their colour has gone and they sit a little darker and cooler than the shroud's
// multiply alone leaves them.  How far into the fog a pixel lies is its light (t2, the soft picture
// the terrain multiplied by, so the two edges are one) between clear and the fog's level
// (BlurDirection.x); under the shroud it is black already and stays so.  Source is a copy of the
// scene, Extra the depth.  Sky and anything in sight take nothing, so their samples are untouched.
static const char * const SHROUD_MEMORY_SHADER_BODY =
	"cbuffer MemoryConstants : register(b1)\n"
	"{\n"
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n"
	"    float4 HazeSun : packoffset(c19);\n"
	"    float4 HazeShroudRect : packoffset(c20);\n"
	"};\n"
	"Texture2D Shroud : register(t2);\n"
	"static const float MEMORY_GREY = 0.7;\n"
	"static const float MEMORY_DIM = 0.88;\n"
	"static const float3 MEMORY_TINT = float3(0.94, 0.98, 1.06);\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Load(int3(input.Position.xy, 0)).r;\n"
	"    if (raw >= 0.99999 || HazeSun.z <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n"
	"    float2 here = world.xy / world.w;\n"
	// The shroud texel is its light times the map's shroud colour: the brightest channel is the light
	// whatever the colour, and the colour over it is put back on the recalled grey, so a tinted fog
	// (the Apocalypse map's red) keeps its hue.  A white shroud reads as it always did.
	"    float3 shroud = Shroud.SampleLevel(Sampler, (here - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n"
	"    float lit = max(shroud.r, max(shroud.g, shroud.b));\n"
	"    float memory = saturate((1.0 - lit) / max(1.0 - BlurDirection.x, 0.0001));\n"
	"    if (memory <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float3 scene = Source.Load(int3(input.Position.xy, 0)).rgb;\n"
	"    float grey = dot(scene, float3(0.299, 0.587, 0.114));\n"
	// the R5G6B5 shroud keeps a white fog's red and blue a step under its green: a hue that close to
	// white is white, so an untinted shroud draws exactly as before
	"    float3 hue = shroud / max(lit, 0.0001);\n"
	"    if (min(hue.r, min(hue.g, hue.b)) > 0.97) hue = float3(1.0, 1.0, 1.0);\n"
	"    float3 recalled = lerp(scene, grey * MEMORY_TINT, MEMORY_GREY) * MEMORY_DIM * hue;\n"
	"    return float4(recalled, memory);\n"
	"}\n";

// The SunShafts option's three programs.  Their constants sit past the occlusion's (c22 on), which no
// other program declares, and the helpers all three share follow them: the eye distance of a depth, the
// world point of a depth, the shroud's light over a world point, whether a point lies under one of the
// water areas' surfaces, the eye distance a half size pixel stands for (full size depth texel 2i+1, as
// the march reads it), how much sun reaches a point through the sun's map, the terrain's height under a
// point, and how much a surface is the ground.
//
// The ground: a surface facing up (its normal's height past SHAFT_GROUND_TILT) and, when the terrain's
// heights are bound (ShaftTerrain.w), standing within SHAFT_GROUND_RISE world units of them.  Only the
// ground takes the lift: a unit, a roof or a wall standing in a sunlit lane does not, so no body in the
// light is veiled.  Without the heights a flat roof or a tank's deck still counts as ground.
#define SUN_SHAFT_CONSTANTS \
	"cbuffer SunShaftConstants : register(b1)\n" \
	"{\n" \
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n" \
	"    float4 HazeSun : packoffset(c19);\n" \
	"    float4 HazeShroudRect : packoffset(c20);\n" \
	"    float4 WorldAoProjection : packoffset(c21);\n" \
	"    row_major float4x4 SunFromWorld : packoffset(c22);\n" \
	"    float4 Shafts : packoffset(c26);\n" \
	"    float4 ShaftSun : packoffset(c27);\n" \
	"    float4 ShaftLook : packoffset(c28);\n" \
	"    float4 ShaftGlare : packoffset(c29);\n" \
	"    float4 ShaftColour : packoffset(c30);\n" \
	"    float4 ShaftWater[8] : packoffset(c31);\n" \
	"    float4 ShaftWaterHeight[8] : packoffset(c39);\n" \
	"    float4 ShaftLimit : packoffset(c47);\n" \
	"    float4 ShaftBand : packoffset(c48);\n" \
	"    float4 ShaftTerrain : packoffset(c49);\n" \
	"    row_major float4x4 WorldFromSun : packoffset(c50);\n" \
	"};\n" \
	"Texture2D ShaftShroud : register(t2);\n" \
	"Texture2D SunMap : register(t3);\n" \
	"Texture2D ShaftHeights : register(t4);\n" \
	"static const float SHAFT_BOX_EDGE = 0.04;\n" \
	"static const float SHAFT_GROUND_TILT_LOW = 0.6;\n" \
	"static const float SHAFT_GROUND_TILT_HIGH = 0.8;\n" \
	"static const float SHAFT_GROUND_RISE_LOW = 2.0;\n" \
	"static const float SHAFT_GROUND_RISE_HIGH = 4.0;\n" \
	"\n" \
	"float shaft_eye_distance(float raw)\n" \
	"{\n" \
	"    return 1.0 / (raw * WorldFromClip[2][3] + WorldFromClip[3][3]);\n" \
	"}\n" \
	"\n" \
	"float3 shaft_world(float2 ndc, float raw)\n" \
	"{\n" \
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n" \
	"    return world.xyz / world.w;\n" \
	"}\n" \
	"\n" \
	"float shaft_light(float2 at)\n" \
	"{\n" \
	"    float3 texel = ShaftShroud.SampleLevel(Sampler, (at - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n" \
	"    return lerp(1.0, max(texel.r, max(texel.g, texel.b)), HazeSun.z);\n" \
	"}\n" \
	"\n" \
	"bool shaft_under_water(float3 end)\n" \
	"{\n" \
	"    int areas = (int)ShaftGlare.z;\n" \
	"    [loop]\n" \
	"    for (int area = 0; area < areas; ++area)\n" \
	"    {\n" \
	"        float4 box = ShaftWater[area];\n" \
	"        if (end.x >= box.x && end.y >= box.y && end.x <= box.z && end.y <= box.w && end.z < ShaftWaterHeight[area].x)\n" \
	"            return true;\n" \
	"    }\n" \
	"    return false;\n" \
	"}\n" \
	"\n" \
	"float shaft_half_distance(int2 half_texel)\n" \
	"{\n" \
	"    uint width;\n" \
	"    uint height;\n" \
	"    Extra.GetDimensions(width, height);\n" \
	"    int2 texel = min(half_texel * 2 + 1, int2(width, height) - 1);\n" \
	"    return shaft_eye_distance(Extra.Load(int3(texel, 0)).r);\n" \
	"}\n" \
	"\n" \
	"float shaft_sunlit(float3 at, float2 size, float2 nudge, out float inside)\n" \
	"{\n" \
	"    inside = 0.0;\n" \
	"    float4 sun = mul(float4(at, 1.0), SunFromWorld);\n" \
	"    if (sun.w <= 0.0) return 1.0;\n" \
	"    sun.xyz /= sun.w;\n" \
	"    float2 map = float2(0.5 * sun.x + 0.5, 0.5 - 0.5 * sun.y);\n" \
	"    float2 edge = min(map, 1.0 - map);\n" \
	"    inside = saturate(min(edge.x, edge.y) / SHAFT_BOX_EDGE);\n" \
	"    if (inside <= 0.0 || sun.z < 0.0 || sun.z > 1.0) { inside = 0.0; return 1.0; }\n" \
	"    float2 spot = map * size - 0.5 + nudge;\n" \
	"    int2 base = clamp(int2(floor(spot)), int2(0, 0), int2(size) - 2);\n" \
	"    float2 part = saturate(spot - base);\n" \
	"    float bias = sun.z - ShaftSun.w;\n" \
	"    float a = (bias <= SunMap.Load(int3(base, 0)).r) ? 1.0 : 0.0;\n" \
	"    float b = (bias <= SunMap.Load(int3(base + int2(1, 0), 0)).r) ? 1.0 : 0.0;\n" \
	"    float c = (bias <= SunMap.Load(int3(base + int2(0, 1), 0)).r) ? 1.0 : 0.0;\n" \
	"    float d = (bias <= SunMap.Load(int3(base + int2(1, 1), 0)).r) ? 1.0 : 0.0;\n" \
	"    return lerp(lerp(a, b, part.x), lerp(c, d, part.x), part.y);\n" \
	"}\n" \
	"\n" \
	"float shaft_ground(float2 at)\n" \
	"{\n" \
	"    uint width;\n" \
	"    uint height;\n" \
	"    ShaftHeights.GetDimensions(width, height);\n" \
	"    float2 grid = at * ShaftTerrain.x + ShaftTerrain.y;\n" \
	"    return ShaftHeights.SampleLevel(Sampler, (grid + 0.5) / float2(width, height), 0.0).r * 255.0 * ShaftTerrain.z;\n" \
	"}\n" \
	"\n" \
	"float shaft_grounded(float3 at, float3 facing)\n" \
	"{\n" \
	"    float grounded = smoothstep(SHAFT_GROUND_TILT_LOW, SHAFT_GROUND_TILT_HIGH, facing.z);\n" \
	"    if (ShaftTerrain.w > 0.0)\n" \
	"        grounded *= 1.0 - smoothstep(SHAFT_GROUND_RISE_LOW, SHAFT_GROUND_RISE_HIGH, at.z - shaft_ground(at.xy));\n" \
	"    return grounded;\n" \
	"}\n" \
	"\n"

// The march, half size into the occlusion's first target: R the lift, G the dimming, two amounts that
// travel apart through the blur and the composite, so neither can cancel the other.  Each half size pixel
// takes full size depth texel 2i+1 back to the world as the occlusion does.
//
// The lift is decided on the surface the pixel shows, in the sun's map's own space.  A surface in the
// sun's shadow, turned from the sun, outside the map's box, under water or over fog and shroud, or not the
// ground (shaft_grounded), takes none at all, and says so with R below nought (SHAFT_NEVER), which the
// blur keeps out of its neighbours' sums; the composite decides the same gate again per full size pixel.
// A sunlit patch of ground looks along the line across the sun's ground bearing (ShaftSun.xy turned a
// quarter), both ways, for the nearest shade at its own height in the sun's map: half of Shafts.w taps
// out to ShaftBand.x world units, then a bisection of the bracket the first shaded tap closes, so the
// distance moves smoothly as the camera and the map's box move and the band has no contour a tap wide.
// Shade here is a neighbour whose stored depth stands nearer the sun than the surface by more than the
// bias and ShaftLook.w world units and, with the terrain's heights, whose caster stands that far over the
// ground (shaft_shade), so rising ground across the bearing is no caster.  Each side's nearness is one less a smoothstep of
// its distance over the radius, so the band is whole at the shadow's edge and reaches nought at the
// radius with no slope: shade close on both sides, a gap between two casters, gives the nearer of the two
// whole (so a narrow gap glows down its middle), shade on one side only ShaftGlare.w of its square, and no
// shade within the radius exactly nought, open ground untouched to the bit.  A soft value noise in world
// space, ShaftBand.y across the bearing by ShaftBand.z along it, breaks the band into streaks pointing
// along the sun's way (shaft_streak); no clock moves it, so a capture is the frame a player saw.
// ShaftLook.y is its most, of the sun's colour.
//
// The dimming walks the view ray through the layer, from the map's lowest ground (ShaftLimit.z) to
// Shafts.x + Shafts.y, one step on each of Shafts.w fixed heights (with a fixed offset a pixel, the
// occlusion's interleaved gradient noise, and no clock), so a step slides along the ground as the camera
// moves instead of jumping with the stretch it was a share of.  The dust is at its thickest up to the
// median ground (Shafts.x) and thins above it.  A step reads the sun's map through a two by two filter;
// one near the box's edge fades out of the sum.  The shaded dust (ShaftLook.z, held under ShaftLimit.y
// rays, each step weighing its density, exp(-h / Shafts.z), its stretch of ray and the shroud's light
// cubed there, over a full ray's dust ShaftLook.x) is taken off a sunlit surface seen through it; a
// surface in shadow, or one turned from the sun, took its shade already.  The surface's own sun is read
// a little off it along its normal (ShaftLimit.w), as the receivers move their lookup, so a slope does not
// shade itself in speckles.  Both amounts take the shroud's light at the ray's end, cubed.
static const char * const SUN_SHAFTS_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const int SHAFT_MOST_STEPS = 32;\n"
	"static const int SHAFT_REFINE = 5;\n"
	"static const float SHAFT_NEVER = -1.0;\n"
	"static const float SHAFT_STREAK_FLOOR = 0.2;\n"
	"static const float SHAFT_STREAK_PLAIN = 0.5;\n"
	"static const float SHAFT_BEARING_STEP = 0.08726646;\n"
	"\n"
	"float shaft_shroud(float2 at)\n"
	"{\n"
	"    float lit = shaft_light(at);\n"
	"    return lit * lit * lit;\n"
	"}\n"
	"\n"
	"float3 shaft_world_of(int2 texel)\n"
	"{\n"
	"    float2 ndc = float2((texel.x + 0.5) * TexelSize.x * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * TexelSize.y * 2.0);\n"
	"    return shaft_world(ndc, Extra.Load(int3(texel, 0)).r);\n"
	"}\n"
	"\n"
	"float3 shaft_shorter(float3 a, float3 b)\n"
	"{\n"
	"    return dot(a, a) < dot(b, b) ? a : b;\n"
	"}\n"
	"\n"
	// The share of a two by two of the sun's map round a texel spot whose stored depth stands nearer the sun
	// than limit: the shade there at the surface's height, filtered so it moves smoothly with the spot.  Past
	// the map's edge it says nothing, and counts as lit.  With the terrain's heights bound, the nearest of
	// the four is taken back to the world (WorldFromSun) and counts only by how far that caster stands over
	// the ground under it, nothing below half ShaftLook.w, all of it from ShaftLook.w up: so a valley wall,
	// a ridge or a road cut, which is the terrain itself in the map, is no caster, and neither on purpose
	// is anything lower than the slack (infantry, fences, low walls), whose gaps are too small to read.
	"float shaft_shade(float2 spot, float limit, float2 size)\n"
	"{\n"
	"    if (spot.x < 0.0 || spot.y < 0.0 || spot.x > size.x - 1.0 || spot.y > size.y - 1.0) return 0.0;\n"
	"    int2 base = min(int2(spot), int2(size) - 2);\n"
	"    float2 part = saturate(spot - base);\n"
	"    float4 stored = float4(SunMap.Load(int3(base, 0)).r, SunMap.Load(int3(base + int2(1, 0), 0)).r,\n"
	"        SunMap.Load(int3(base + int2(0, 1), 0)).r, SunMap.Load(int3(base + int2(1, 1), 0)).r);\n"
	"    float4 shade = float4(stored < limit);\n"
	"    float share = lerp(lerp(shade.x, shade.y, part.x), lerp(shade.z, shade.w, part.x), part.y);\n"
	"    if (share > 0.0 && ShaftTerrain.w > 0.0)\n"
	"    {\n"
	"        float nearest = min(min(stored.x, stored.y), min(stored.z, stored.w));\n"
	"        float2 map = (spot + 0.5) / size;\n"
	"        float4 caster = mul(float4(map.x * 2.0 - 1.0, 1.0 - map.y * 2.0, nearest, 1.0), WorldFromSun);\n"
	"        caster.xyz /= caster.w;\n"
	"        float slack = max(ShaftLook.w, 0.001);\n"
	"        share *= smoothstep(0.5 * slack, slack, caster.z - shaft_ground(caster.xy));\n"
	"    }\n"
	"    return share;\n"
	"}\n"
	"\n"
	// World units from a spot to the first shade along a line (per_unit texels a world unit), out to the
	// radius: steps taps, then SHAFT_REFINE halvings of the bracket the first shaded tap closes.  The radius
	// itself when there is none.  A shadow thinner than a tap's stride can fall between two taps.
	"float shaft_edge(float2 spot, float2 per_unit, float limit, float2 size, int steps)\n"
	"{\n"
	"    float reach = ShaftBand.x;\n"
	"    float stride = reach / steps;\n"
	"    float near_d = 0.0;\n"
	"    float far_d = -1.0;\n"
	"    [loop]\n"
	"    for (int k = 1; k <= steps; ++k)\n"
	"    {\n"
	"        float d = stride * k;\n"
	"        if (shaft_shade(spot + per_unit * d, limit, size) >= 0.5) { far_d = d; near_d = d - stride; break; }\n"
	"    }\n"
	"    if (far_d < 0.0) return reach;\n"
	"    [unroll]\n"
	"    for (int i = 0; i < SHAFT_REFINE; ++i)\n"
	"    {\n"
	"        float middle = 0.5 * (near_d + far_d);\n"
	"        if (shaft_shade(spot + per_unit * middle, limit, size) >= 0.5) far_d = middle; else near_d = middle;\n"
	"    }\n"
	"    return 0.5 * (near_d + far_d);\n"
	"}\n"
	"\n"
	"float shaft_hash(int2 cell)\n"
	"{\n"
	"    uint h = ((uint)cell.x * 0x8da6b343u) ^ ((uint)cell.y * 0xd8163841u);\n"
	"    h = (h ^ (h >> 13)) * 0x5bd1e995u;\n"
	"    h ^= h >> 15;\n"
	"    return (float)(h & 0xffffu) / 65535.0;\n"
	"}\n"
	"\n"
	"float shaft_noise(float2 at)\n"
	"{\n"
	"    float2 cell = floor(at);\n"
	"    float2 f = at - cell;\n"
	"    float2 u = f * f * (3.0 - 2.0 * f);\n"
	"    int2 c = int2(cell);\n"
	"    return lerp(lerp(shaft_hash(c), shaft_hash(c + int2(1, 0)), u.x),\n"
	"        lerp(shaft_hash(c + int2(0, 1)), shaft_hash(c + int2(1, 1)), u.x), u.y);\n"
	"}\n"
	"\n"
	// The streaks' noise in a frame turned to a bearing angle about the world's origin.
	"float shaft_streak_noise(float2 at, float angle)\n"
	"{\n"
	"    float2 way = float2(cos(angle), sin(angle));\n"
	"    float2 cell = float2(dot(at, float2(-way.y, way.x)) / ShaftBand.y, dot(at, way) / max(ShaftBand.z, ShaftBand.y));\n"
	"    return shaft_noise(cell);\n"
	"}\n"
	"\n"
	// The streaks at a world point.  Their frame is turned to the sun's bearing in fixed steps of
	// SHAFT_BEARING_STEP (five degrees), faded between the two steps either side of it: a frame turned with
	// the bearing itself swept a point a thousand units out across a cell or two at each half degree the day
	// and night cycle moves the sun, and every streak re-rolled, where now a pattern holds and the next one
	// fades in over the five degrees.  Nought width is a flat SHAFT_STREAK_PLAIN, about their mean, so
	// switching them off does not brighten the band.
	"float shaft_streak(float2 at)\n"
	"{\n"
	"    if (ShaftBand.y <= 0.0) return SHAFT_STREAK_PLAIN;\n"
	"    float steps = atan2(ShaftSun.y, ShaftSun.x) / SHAFT_BEARING_STEP;\n"
	"    float below = floor(steps);\n"
	"    float n = lerp(shaft_streak_noise(at, below * SHAFT_BEARING_STEP),\n"
	"        shaft_streak_noise(at, (below + 1.0) * SHAFT_BEARING_STEP), steps - below);\n"
	"    return SHAFT_STREAK_FLOOR + (1.0 - SHAFT_STREAK_FLOOR) * smoothstep(0.35, 0.7, n);\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = min(int2(input.Position.xy) * 2 + 1, int2(TexelSize.zw) - 1);\n"
	"    float raw = Extra.Load(int3(texel, 0)).r;\n"
	"    if (raw >= 0.99999) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float2 ndc = float2((texel.x + 0.5) * TexelSize.x * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * TexelSize.y * 2.0);\n"
	"    float3 end = shaft_world(ndc, raw);\n"
	"    float3 eye = shaft_world(ndc, 0.0);\n"
	"    float3 ray = end - eye;\n"
	"    float3 look = ray / max(length(ray), 0.0001);\n"
	"    float light_end = shaft_light(end.xy);\n"
	"    float shroud_end = light_end * light_end * light_end;\n"
	// black shroud, and water: its surface draws over this pixel, so neither the bed nor the air over it is
	// what the pixel shows
	"    if (shroud_end <= 0.0 || shaft_under_water(end)) return float4(SHAFT_NEVER, 0.0, 0.0, 0.0);\n"
	"\n"
	"    uint width;\n"
	"    uint height;\n"
	"    SunMap.GetDimensions(width, height);\n"
	"    float2 size = float2(width, height);\n"
	// The surface comes from the depth as the occlusion's does, from whichever neighbour on each axis lies
	// nearer.  Its own sun is read four times a texel apart, a tent about three texels wide like the
	// receivers' soft edge: a two by two read alone stepped the dimming texel by texel along every shadow's
	// edge, a sawtooth fixed on the ground.
	"    float3 across_end = shaft_shorter(end - shaft_world_of(texel - int2(1, 0)), shaft_world_of(texel + int2(1, 0)) - end);\n"
	"    float3 down_end = shaft_shorter(end - shaft_world_of(texel - int2(0, 1)), shaft_world_of(texel + int2(0, 1)) - end);\n"
	"    float3 facing_end = cross(across_end, down_end);\n"
	"    facing_end *= rsqrt(max(dot(facing_end, facing_end), 1.0e-12));\n"
	"    if (dot(facing_end, eye - end) < 0.0) facing_end = -facing_end;\n"
	"    float3 depth_way = float3(SunFromWorld[0][2], SunFromWorld[1][2], SunFromWorld[2][2]);\n"
	"    float depth_unit = length(depth_way);\n"
	"    float3 toward_sun = -depth_way / max(depth_unit, 1.0e-6);\n"
	"    float3 surface = end + facing_end * ShaftLimit.w;\n"
	"    float sun_on = 0.0;\n"
	"    float end_inside = 0.0;\n"
	"    if (dot(facing_end, toward_sun) > 0.1)\n"
	"    {\n"
	"        sun_on = 0.25 * (shaft_sunlit(surface, size, float2(-1.0, -1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(1.0, -1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(-1.0, 1.0), end_inside)\n"
	"            + shaft_sunlit(surface, size, float2(1.0, 1.0), end_inside));\n"
	"    }\n"
	// the dimming's view of the surface: past the box's edge it counts lit, as it always did
	"    float seen_lit = (dot(facing_end, toward_sun) > 0.1) ? lerp(1.0, sun_on, end_inside) : 0.0;\n"
	// the lift's: the ground, in full sun, inside the box, under clear sky; anything less takes none
	"    float gate = end_inside * smoothstep(0.75, 1.0, sun_on) * smoothstep(0.9, 1.0, light_end);\n"
	"    if (gate > 0.0) gate *= shaft_grounded(end, facing_end);\n"
	"\n"
	"    float lift = SHAFT_NEVER;\n"
	"    if (gate > 0.0)\n"
	"    {\n"
	"        float2 across = float2(-ShaftSun.y, ShaftSun.x);\n"
	"        float4 sun = mul(float4(surface, 1.0), SunFromWorld);\n"
	"        float4 aside = mul(float4(surface + float3(across, 0.0), 1.0), SunFromWorld);\n"
	"        sun.xyz /= sun.w;\n"
	"        aside.xy /= aside.w;\n"
	"        float2 spot = float2(0.5 * sun.x + 0.5, 0.5 - 0.5 * sun.y) * size - 0.5;\n"
	"        float2 per_unit = float2(0.5 * (aside.x - sun.x), -0.5 * (aside.y - sun.y)) * size;\n"
	"        float limit = sun.z - ShaftSun.w - ShaftLook.w * depth_unit;\n"
	// half the layer's steps a side, at least three: the bisection gives the distance its precision, the taps
	// only have to find the bracket
	"        int steps = clamp((int)(Shafts.w * 0.5), 3, SHAFT_MOST_STEPS);\n"
	"        float near_one = 1.0 - smoothstep(0.0, ShaftBand.x, shaft_edge(spot, per_unit, limit, size, steps));\n"
	"        float near_two = 1.0 - smoothstep(0.0, ShaftBand.x, shaft_edge(spot, -per_unit, limit, size, steps));\n"
	"        float one = max(near_one, near_two);\n"
	"        float edge = max(min(near_one, near_two), ShaftGlare.w * one * one);\n"
	"        lift = (edge > 0.0) ? ShaftLook.y * edge * shaft_streak(end.xy) * gate * shroud_end : 0.0;\n"
	"    }\n"
	"\n"
	"    float shaded = 0.0;\n"
	"    float lowest = max(end.z, ShaftLimit.z);\n"
	"    float highest = min(eye.z, Shafts.x + Shafts.y);\n"
	"    if (seen_lit > 0.0 && highest > lowest && look.z < -0.0001)\n"
	"    {\n"
	"        float start = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715))));\n"
	"        int steps = min((int)Shafts.w, SHAFT_MOST_STEPS);\n"
	"        float rise = max(Shafts.x + Shafts.y - ShaftLimit.z, 0.0001) / Shafts.w;\n"
	"        float stretch = rise / max(-look.z, 0.1);\n"
	"        [loop]\n"
	"        for (int index = 0; index < steps; ++index)\n"
	"        {\n"
	"            float z = Shafts.x + Shafts.y - ((float)index + start) * rise;\n"
	"            if (z < highest && z > lowest)\n"
	"            {\n"
	"                float3 at = eye + ray * ((z - eye.z) / ray.z);\n"
	"                float inside;\n"
	"                float lit = shaft_sunlit(at, size, float2(0.0, 0.0), inside);\n"
	"                shaded += stretch * exp(-max(z - Shafts.x, 0.0) / Shafts.z) * inside * shaft_shroud(at.xy) * ShaftLook.x * (1.0 - lit);\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    float dim = ShaftLook.z * min(shaded, ShaftLimit.y) * seen_lit * shroud_end;\n"
	"    return float4(lift, dim, 0.0, 0.0);\n"
	"}\n";

// One axis of the shafts' blur, the occlusion's weights and stop, the stop's distance read from the depth.
// The lift and the dimming are blurred apart.  A neighbour that must never be lifted (R below nought)
// stays out of the lift's sum, and such a pixel stays so itself, so the band does not bleed off a lit
// surface onto the shade, the water or the shroud beside it.
static const char * const SUN_SHAFTS_BLUR_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const float SHAFT_WEIGHTS[5] = { 0.2, 0.18, 0.14, 0.09, 0.05 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = int2(input.Position.xy);\n"
	"    int2 step = int2(BlurDirection.xy);\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    float2 centre = Source.Load(int3(texel, 0)).rg;\n"
	"    float centre_distance = shaft_half_distance(texel);\n"
	"    float allowed = 40.0 * centre_distance / (WorldAoProjection.y * TexelSize.w);\n"
	"    float lift = max(centre.r, 0.0) * SHAFT_WEIGHTS[0];\n"
	"    float lift_total = SHAFT_WEIGHTS[0];\n"
	"    float dim = centre.g * SHAFT_WEIGHTS[0];\n"
	"    float dim_total = SHAFT_WEIGHTS[0];\n"
	"    [unroll]\n"
	"    for (int index = 1; index < 5; ++index)\n"
	"    {\n"
	"        [unroll]\n"
	"        for (int side = -1; side <= 1; side += 2)\n"
	"        {\n"
	"            int2 at = clamp(texel + step * index * side, int2(0, 0), last);\n"
	"            float2 tap = Source.Load(int3(at, 0)).rg;\n"
	"            float weight = SHAFT_WEIGHTS[index] * saturate(1.0 - abs(shaft_half_distance(at) - centre_distance) / allowed);\n"
	"            if (tap.r >= 0.0)\n"
	"            {\n"
	"                lift += tap.r * weight;\n"
	"                lift_total += weight;\n"
	"            }\n"
	"            dim += tap.g * weight;\n"
	"            dim_total += weight;\n"
	"        }\n"
	"    }\n"
	"    return float4((centre.r < 0.0) ? centre.r : lift / lift_total, dim / dim_total, 0.0, 0.0);\n"
	"}\n";

// Back to full size over the scene, the occlusion's upsample carrying the two amounts apart: the lift
// is the sun's colour added, the dimming takes that share of the scene away (the blend is ONE,
// INV_SRC_ALPHA), never more than ShaftLimit.x, a share of what the receivers' shadow takes, so a dimmed
// sunlit pixel never ends darker than the shadow beside it.  Nought in both draws the scene as it was.  A
// half size pixel that must never be lifted counts as nought lift with its full weight, and the lift's
// gate (the ground, in full sun, dry, under clear sky) is decided again for the full size pixel, so a
// shaded, wet or shrouded pixel takes exactly none whatever its half size neighbours hold.  Its distance
// stop is the occlusion's own: three times as wide let the lift run over the silhouettes of units and
// houses standing in it, which then read as ghosts.
//
// The glare is worked here, per full size pixel, and does not take the lift's gate: a shallow ray
// looking along the sun's bearing (ShaftSun.xy) takes ShaftSun.z of the colour, eased by the cube of how
// near the bearing it looks, all of it at a ray shallower than ShaftGlare.x - ShaftGlare.y of straight
// down and none past ShaftGlare.x, none over the sky, water or ground the fog of war dims.
//
// The light it adds never passes SHAFT_MOST_LIFT of the sun's colour, whatever the strength, the band and
// the glare say: a hard ceiling under any tuning, about 36 levels at full colour.
static const char * const SUN_SHAFTS_COMPOSITE_SHADER_BODY =
	SUN_SHAFT_CONSTANTS
	"static const float SHAFT_MOST_LIFT = 0.14;\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Load(int3(int2(input.Position.xy), 0)).r;\n"
	"    float wanted = shaft_eye_distance(raw);\n"
	"    float allowed = 20.0 * wanted / (WorldAoProjection.y * TexelSize.w);\n"
	"    float2 at = input.Position.xy * 0.5 - 0.75;\n"
	"    int2 last = int2(TexelSize.zw) - 1;\n"
	"    int2 corner = clamp(int2(floor(at)), int2(0, 0), last);\n"
	"    float2 part = saturate(at - corner);\n"
	"    float lift = 0.0;\n"
	"    float dim = 0.0;\n"
	"    float total = 0.0;\n"
	"    for (int y = 0; y < 2; ++y)\n"
	"    {\n"
	"        for (int x = 0; x < 2; ++x)\n"
	"        {\n"
	"            int2 half_texel = min(corner + int2(x, y), last);\n"
	"            float2 tap = Source.Load(int3(half_texel, 0)).rg;\n"
	"            float bilinear = (x == 1 ? part.x : 1.0 - part.x) * (y == 1 ? part.y : 1.0 - part.y);\n"
	"            float weight = bilinear * (0.001 + saturate(1.0 - abs(shaft_half_distance(half_texel) - wanted) / allowed));\n"
	"            lift += max(tap.r, 0.0) * weight;\n"
	"            dim += tap.g * weight;\n"
	"            total += weight;\n"
	"        }\n"
	"    }\n"
	"    lift /= total;\n"
	"    dim /= total;\n"
	"\n"
	// This pixel's own surface, its normal from the screen derivatives of its world point (taken before
	// any branch, where they are defined), for the lift's gate at full size: the half size gate let a pixel
	// on the shaded side of an edge, a shore or the shroud's border take up to half its lit neighbour's lift.
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float3 end = shaft_world(ndc, raw);\n"
	"    float3 eye = shaft_world(ndc, 0.0);\n"
	"    float3 facing = cross(ddx(end), ddy(end));\n"
	"    facing *= rsqrt(max(dot(facing, facing), 1.0e-12));\n"
	"    if (dot(facing, eye - end) < 0.0) facing = -facing;\n"
	"    bool ground = raw < 0.99999;\n"
	"    float light_end = ground ? shaft_light(end.xy) : 0.0;\n"
	"    bool dry = ground && !shaft_under_water(end);\n"
	"    if (lift > 0.0)\n"
	"    {\n"
	"        float full_gate = 0.0;\n"
	"        float3 toward_sun = -float3(SunFromWorld[0][2], SunFromWorld[1][2], SunFromWorld[2][2]);\n"
	"        if (dry && dot(facing, toward_sun) > 0.0)\n"
	"        {\n"
	"            uint width;\n"
	"            uint height;\n"
	"            SunMap.GetDimensions(width, height);\n"
	"            float inside;\n"
	"            float sun_on = shaft_sunlit(end + facing * ShaftLimit.w, float2(width, height), float2(0.0, 0.0), inside);\n"
	"            full_gate = inside * smoothstep(0.75, 1.0, sun_on) * smoothstep(0.9, 1.0, light_end) * shaft_grounded(end, facing);\n"
	"        }\n"
	"        lift *= full_gate;\n"
	"    }\n"
	"\n"
	// The glare is light in the air between the eye and whatever the pixel shows, so it lies over units and
	// walls as well as the ground; it stays under about 8 levels, too faint to veil a body.
	"    float glare = 0.0;\n"
	"    if (dry && ShaftSun.z > 0.0)\n"
	"    {\n"
	"        float3 ray = end - eye;\n"
	"        float3 look = ray / max(length(ray), 0.0001);\n"
	"        float across = dot(look.xy, look.xy);\n"
	"        float toward = (across > 0.000001) ? saturate(dot(look.xy * rsqrt(max(across, 0.000001)), ShaftSun.xy)) : 0.0;\n"
	"        float shallow = saturate((ShaftGlare.x - abs(look.z)) / max(ShaftGlare.y, 0.0001));\n"
	"        glare = ShaftSun.z * toward * toward * toward * shallow * smoothstep(0.9, 1.0, light_end)\n"
	"            * light_end * light_end * light_end;\n"
	"    }\n"
	"    float add = min(ShaftColour.a * (lift + glare), SHAFT_MOST_LIFT);\n"
	"    return float4(ShaftColour.rgb * add, min(saturate(ShaftColour.a * dim), ShaftLimit.x));\n"
	"}\n";

// The menu backdrop's last pass.  Its viewport is one panel, so the triangle's own coordinates run
// nought to one across the panel; the blur is read where the pixel stands on the screen instead
// (TexelSize is one over the back buffer's size), and the panel shows the part of the frame behind it.
// Here and not in postshader.cpp, so test_postshader's baselines stay as they are.
static const char * const BACKDROP_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return float4(Source.Sample(Sampler, input.Position.xy * TexelSize.xy).rgb, 1.0);\n"
	"}\n";

// The bloom blur's nine weights spread a pass of one texel's step to this standard deviation, in
// texels: the square root of twice the sum of each weight times its offset squared.
static const float BACKDROP_PASS_SPREAD = 1.6894f;
// The widest step a backdrop pass takes, in quarter size texels.  A wider blur takes more pairs at
// this step or under, since taps further apart than this start to show as copies.
static const float BACKDROP_STEP_MOST = 2.0f;
static const unsigned BACKDROP_PAIRS_MOST = 8;

static D3DCompileFunction compiler_function()
{
	static D3DCompileFunction compiler = NULL;
	static bool attempted = false;
	if (!attempted) {
		attempted = true;
		HMODULE module = LoadLibraryA(COMPILER_MODULE);
		if (module != NULL) {
			compiler = reinterpret_cast<D3DCompileFunction>(GetProcAddress(module, "D3DCompile"));
		}
	}
	return compiler;
}

static void release_interface(IUnknown ** object)
{
	if (*object != NULL) {
		(*object)->Release();
		*object = NULL;
	}
}

static const char * simple_effect_body(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return FXAA_SHADER_BODY;
	case DX11_POST_SHARPEN:
		return SHARPEN_SHADER_BODY;
	case DX11_POST_AO:
		return AO_SHADER_BODY;
	default:
		return COPY_SHADER_BODY;
	}
}

DX11PostProcessClass::DX11PostProcessClass()
	: Device(NULL)
	, VertexShader(NULL)
	, ToneMapShader(NULL)
	, ToneMapWideShader(NULL)
	, SharpenMultisampledShader(NULL)
	, BloomExtractShader(NULL)
	, NearPlane(1.0f)
	, FarPlane(1200.0f)
	, BloomBlurShader(NULL)
	, BloomCompositeShader(NULL)
	, BloomCompositeWideShader(NULL)
	, WarpShader(NULL)
	, SceneOcclusionShader(NULL)
	, HazeShader(NULL)
	, WrapSampler(NULL)
	, Sampler(NULL)
	, BlendState(NULL)
	, MultiplyBlendState(NULL)
	, AlphaBlendState(NULL)
	, ShaftBlendState(NULL)
	, DepthState(NULL)
	, RasterizerState(NULL)
	, ConstantBuffer(NULL)
	, ChainLength(0)
	, Width(0)
	, Height(0)
	, BloomWidth(0)
	, BloomHeight(0)
	, TargetSamples(1)
	, TargetsAreFloat(false)
	, ShadersReady(false)
	, Resolved(false)
	, Occluded(false)
	, Hazed(false)
	, HazeRefused(false)
	, HazeShroudView(NULL)
	, AtmosphereRefused(false)
	, WorldAoRefused(false)
	, SunShaftRefused(false)
	, Shafted(false)
	, ShroudRefused(false)
	, ShroudHeldIndex(0)
	, ShroudCellsWidth(0)
	, ShroudCellsHeight(0)
	, ShroudScale(1)
	, ShroudSettle(0.0f)
	, ShroudFogLight(-1.0f)
	, ShroudFogged(true)
	, ShroudRemembered(false)
	, HeatSplatBuffer(NULL)
	, HeatSplatView(NULL)
	, HeatSplatVertexShader(NULL)
	, HeatSplatShader(NULL)
	, HeatHazeShader(NULL)
	, HeatBlendState(NULL)
	, HeatHazeRefused(false)
	, BackdropShader(NULL)
	, BackdropSource(0)
	, BackdropWidth(0)
	, BackdropHeight(0)
	, BackdropBlurred(false)
	, BackdropRefused(false)
{
	memset(&BackdropCopy, 0, sizeof(BackdropCopy));
	memset(&BackdropHalf, 0, sizeof(BackdropHalf));
	memset(BackdropBlur, 0, sizeof(BackdropBlur));
	memset(&HeatTarget, 0, sizeof(HeatTarget));
	memset(HazeWorldFromClip, 0, sizeof(HazeWorldFromClip));
	memset(WorldAoShaders, 0, sizeof(WorldAoShaders));
	memset(SunShaftShaders, 0, sizeof(SunShaftShaders));
	memset(ShroudShaders, 0, sizeof(ShroudShaders));
	memset(ShroudHeld, 0, sizeof(ShroudHeld));
	memset(&ShroudSoft, 0, sizeof(ShroudSoft));
	memset(AtmosphereShaders, 0, sizeof(AtmosphereShaders));
	memset(WorldAoTargets, 0, sizeof(WorldAoTargets));
	memset(WorldAoProjection, 0, sizeof(WorldAoProjection));
	memset(PixelShaders, 0, sizeof(PixelShaders));
	memset(&SceneTarget, 0, sizeof(SceneTarget));
	memset(&SceneResolve, 0, sizeof(SceneResolve));
	memset(&SceneCopy, 0, sizeof(SceneCopy));
	memset(ChainTargets, 0, sizeof(ChainTargets));
	memset(BloomTargets, 0, sizeof(BloomTargets));
	memset(Chain, 0, sizeof(Chain));
	strcpy(Diagnostic, "post-process off");
}

DX11PostProcessClass::~DX11PostProcessClass()
{
	Shutdown();
}

bool DX11PostProcessClass::Initialise(DX11DeviceClass * device)
{
	Shutdown();
	Device = device;
	if (Device == NULL || Device->Get_Device() == NULL) {
		strcpy(Diagnostic, "post-process off: no device");
		return false;
	}

	ShadersReady = Create_Shaders() && Create_States();
	if (!ShadersReady) {
		Shutdown();
		strcpy(Diagnostic, "post-process off: the compiler refused the passes");
		return false;
	}

	Describe();
	return true;
}

void DX11PostProcessClass::Shutdown()
{
	Release_Targets();
	Release_Heat_Haze();
	Release_Shroud();
	for (unsigned index = 0; index < SHROUD_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&ShroudShaders[index]));
	}
	ShroudRefused = false;
	HeatSplats.clear();
	HeatHazeRefused = false;
	HazeShroudView = NULL;
	HazeShroudSource = NULL;
	AtmosphereSky = NULL;
	for (unsigned index = 0; index < ATMOSPHERE_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&AtmosphereShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&WrapSampler));
	release_interface(reinterpret_cast<IUnknown **>(&ConstantBuffer));
	release_interface(reinterpret_cast<IUnknown **>(&RasterizerState));
	release_interface(reinterpret_cast<IUnknown **>(&DepthState));
	release_interface(reinterpret_cast<IUnknown **>(&ShaftBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&AlphaBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&MultiplyBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&BlendState));
	release_interface(reinterpret_cast<IUnknown **>(&Sampler));
	release_interface(reinterpret_cast<IUnknown **>(&HazeShader));
	release_interface(reinterpret_cast<IUnknown **>(&BackdropShader));
	BackdropRefused = false;
	for (unsigned index = 0; index < SUN_SHAFT_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&SunShaftShaders[index]));
	}
	for (unsigned index = 0; index < WORLD_AO_PROGRAMS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&WorldAoShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&SceneOcclusionShader));
	release_interface(reinterpret_cast<IUnknown **>(&WarpShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomCompositeWideShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomCompositeShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomBlurShader));
	release_interface(reinterpret_cast<IUnknown **>(&BloomExtractShader));
	release_interface(reinterpret_cast<IUnknown **>(&ToneMapWideShader));
	release_interface(reinterpret_cast<IUnknown **>(&ToneMapShader));
	release_interface(reinterpret_cast<IUnknown **>(&SharpenMultisampledShader));
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		release_interface(reinterpret_cast<IUnknown **>(&PixelShaders[index]));
	}
	release_interface(reinterpret_cast<IUnknown **>(&VertexShader));
	Device = NULL;
	ShadersReady = false;
	Resolved = false;
	HazeRefused = false;
	AtmosphereRefused = false;
	WorldAoRefused = false;
	SunShaftRefused = false;
}

void DX11PostProcessClass::Set_Chain(const DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned count)
{
	ChainLength = (count > DX11_POST_CHAIN_LIMIT) ? DX11_POST_CHAIN_LIMIT : count;
	for (unsigned index = 0; index < ChainLength; ++index) {
		Chain[index] = effects[index];
	}
	Describe();
}

bool DX11PostProcessClass::Scene_Is_Float() const
{
	return ChainLength > 0 && Chain[0] == DX11_POST_BLOOM;
}

static bool compile_pixel_shader(D3DCompileFunction compiler, ID3D11Device * device,
	const char * body, ID3D11PixelShader ** shader)
{
	const std::string source = std::string(PIXEL_SHADER_PROLOGUE) + body;
	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(source.c_str(), source.size(), "dx11post.ps", NULL, NULL, ENTRY_POINT,
		PIXEL_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, shader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	return SUCCEEDED(result);
}

bool DX11PostProcessClass::Create_Shaders()
{
	D3DCompileFunction compiler = compiler_function();
	if (compiler == NULL) {
		return false;
	}

	ID3DBlob * code = NULL;
	ID3DBlob * errors = NULL;
	HRESULT result = compiler(VERTEX_SHADER_SOURCE, strlen(VERTEX_SHADER_SOURCE), "dx11post.vs",
		NULL, NULL, ENTRY_POINT, VERTEX_PROFILE, 0, 0, &code, &errors);
	release_interface(reinterpret_cast<IUnknown **>(&errors));
	if (FAILED(result) || code == NULL) {
		release_interface(reinterpret_cast<IUnknown **>(&code));
		return false;
	}
	result = Device->Get_Device()->CreateVertexShader(code->GetBufferPointer(),
		code->GetBufferSize(), NULL, &VertexShader);
	release_interface(reinterpret_cast<IUnknown **>(&code));
	if (FAILED(result)) {
		return false;
	}

	ID3D11Device * device = Device->Get_Device();
	for (unsigned index = 0; index < DX11_POST_SIMPLE_EFFECTS; ++index) {
		if (!compile_pixel_shader(compiler, device,
				simple_effect_body(static_cast<DX11PostEffect>(index)), &PixelShaders[index])) {
			return false;
		}
	}

	// The menu backdrop's here too, so the first menu panel pays no compile; refused, the menus keep
	// their plain dim and the chain is untouched.
	BackdropRefused = !compile_pixel_shader(compiler, device, BACKDROP_SHADER_BODY, &BackdropShader);

	return compile_pixel_shader(compiler, device, TONE_MAP_SHADER_BODY, &ToneMapShader)
		&& compile_pixel_shader(compiler, device, TONE_MAP_WIDE_SHADER_BODY, &ToneMapWideShader)
		&& compile_pixel_shader(compiler, device, SHARPEN_MULTISAMPLED_SHADER_BODY,
			&SharpenMultisampledShader)
		&& compile_pixel_shader(compiler, device, BLOOM_EXTRACT_SHADER_BODY, &BloomExtractShader)
		&& compile_pixel_shader(compiler, device, BLOOM_BLUR_SHADER_BODY, &BloomBlurShader)
		&& compile_pixel_shader(compiler, device, BLOOM_COMPOSITE_SHADER_BODY,
			&BloomCompositeShader)
		&& compile_pixel_shader(compiler, device, BLOOM_COMPOSITE_WIDE_SHADER_BODY,
			&BloomCompositeWideShader)
		&& compile_pixel_shader(compiler, device, WARP_SHADER_BODY, &WarpShader)
		&& compile_pixel_shader(compiler, device, AO_SCENE_SHADER_BODY, &SceneOcclusionShader);
}

bool DX11PostProcessClass::Create_States()
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_SAMPLER_DESC sampler;
	ZeroMemory(&sampler, sizeof(sampler));
	// Bilinear, because FXAA samples between texels on purpose and the bloom passes read a smaller
	// texture than they write.  At the one to one mapping the full size passes have, a sample on a
	// texel centre still returns that texel exactly.
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device->CreateSamplerState(&sampler, &Sampler))) {
		return false;
	}
	sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	if (FAILED(device->CreateSamplerState(&sampler, &WrapSampler))) {
		return false;
	}

	D3D11_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(device->CreateBlendState(&blend, &BlendState))) {
		return false;
	}

	// The scene's colour times the pass's, its alpha kept.
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	if (FAILED(device->CreateBlendState(&blend, &MultiplyBlendState))) {
		return false;
	}

	// The pass's colour over the scene's by the pass's alpha, the scene's alpha kept.
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	if (FAILED(device->CreateBlendState(&blend, &AlphaBlendState))) {
		return false;
	}

	// The sun shafts' composite: the pass's colour added to the scene dimmed by the pass's alpha, the
	// scene's alpha kept, so the light shaded dust takes off and the light a gap gives back go on in one
	// pass and a pass of nought leaves the scene as it was.
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	if (FAILED(device->CreateBlendState(&blend, &ShaftBlendState))) {
		return false;
	}

	D3D11_DEPTH_STENCIL_DESC depth;
	ZeroMemory(&depth, sizeof(depth));
	if (FAILED(device->CreateDepthStencilState(&depth, &DepthState))) {
		return false;
	}

	D3D11_RASTERIZER_DESC rasterizer;
	ZeroMemory(&rasterizer, sizeof(rasterizer));
	rasterizer.FillMode = D3D11_FILL_SOLID;
	// The full screen triangle is written once and its winding is whatever the arithmetic gives; not
	// culling means the pass cannot be lost to a sign somewhere in it.
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rasterizer, &RasterizerState))) {
		return false;
	}

	D3D11_BUFFER_DESC constants;
	ZeroMemory(&constants, sizeof(constants));
	constants.ByteWidth = sizeof(PostConstantBlock);
	constants.Usage = D3D11_USAGE_DYNAMIC;
	constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	return SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));
}

bool DX11PostProcessClass::Create_Target(Target & target, unsigned width, unsigned height,
	DXGI_FORMAT format, unsigned samples)
{
	ID3D11Device * device = Device->Get_Device();

	D3D11_TEXTURE2D_DESC description;
	ZeroMemory(&description, sizeof(description));
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = format;
	description.SampleDesc.Count = samples;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET
		| (samples > 1 ? 0 : D3D11_BIND_SHADER_RESOURCE);

	if (FAILED(device->CreateTexture2D(&description, NULL, &target.Texture))) {
		return false;
	}
	if (FAILED(device->CreateRenderTargetView(target.Texture, NULL, &target.View))) {
		return false;
	}
	return samples > 1
		|| SUCCEEDED(device->CreateShaderResourceView(target.Texture, NULL, &target.Resource));
}

bool DX11PostProcessClass::Create_Targets(unsigned width, unsigned height, bool floating,
	unsigned samples)
{
	const DXGI_FORMAT scene_format = floating ? FLOAT_FORMAT : EIGHT_BIT_FORMAT;
	if (!Create_Target(SceneTarget, width, height, scene_format, samples)
			|| (samples > 1 && !Create_Target(SceneResolve, width, height, scene_format))) {
		Release_Targets();
		return false;
	}

	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		if (!Create_Target(ChainTargets[index], width, height, EIGHT_BIT_FORMAT)) {
			Release_Targets();
			return false;
		}
	}

	// A window narrower than the divisor would ask for a zero wide texture, which is refused.
	BloomWidth = (width / BLOOM_DIVISOR) > 0 ? (width / BLOOM_DIVISOR) : 1;
	BloomHeight = (height / BLOOM_DIVISOR) > 0 ? (height / BLOOM_DIVISOR) : 1;
	if (floating) {
		const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
		for (unsigned index = 0; index < bloom_target_count; ++index) {
			if (!Create_Target(BloomTargets[index], BloomWidth, BloomHeight, FLOAT_FORMAT)) {
				Release_Targets();
				return false;
			}
		}
	}

	Width = width;
	Height = height;
	TargetSamples = samples;
	TargetsAreFloat = floating;
	return true;
}

void DX11PostProcessClass::Release_Target(Target & target)
{
	release_interface(reinterpret_cast<IUnknown **>(&target.Resource));
	release_interface(reinterpret_cast<IUnknown **>(&target.View));
	release_interface(reinterpret_cast<IUnknown **>(&target.Texture));
}

void DX11PostProcessClass::Release_Targets()
{
	Release_Target(SceneTarget);
	Release_Target(SceneResolve);
	Release_Target(SceneCopy);
	Release_Target(WorldAoTargets[0]);
	Release_Target(WorldAoTargets[1]);
	Release_Target(HeatTarget);
	const unsigned chain_target_count = sizeof(ChainTargets) / sizeof(ChainTargets[0]);
	for (unsigned index = 0; index < chain_target_count; ++index) {
		Release_Target(ChainTargets[index]);
	}
	const unsigned bloom_target_count = sizeof(BloomTargets) / sizeof(BloomTargets[0]);
	for (unsigned index = 0; index < bloom_target_count; ++index) {
		Release_Target(BloomTargets[index]);
	}
	Release_Backdrop();
	Width = 0;
	Height = 0;
	BloomWidth = 0;
	BloomHeight = 0;
	TargetSamples = 1;
	TargetsAreFloat = false;
}

unsigned DX11PostProcessClass::Scene_Samples() const
{
	return (Device != NULL) ? Device->Get_Sample_Count() : 1;
}

bool DX11PostProcessClass::Ensure_Buffers()
{
	const unsigned samples = Scene_Samples();
	if (!ShadersReady || Device == NULL || (ChainLength == 0 && samples == 1)) {
		return false;
	}

	const unsigned width = Device->Get_Width();
	const unsigned height = Device->Get_Height();
	if (width == 0 || height == 0) {
		return false;
	}

	const bool floating = Scene_Is_Float();
	if (SceneTarget.View != NULL && Width == width && Height == height
			&& TargetsAreFloat == floating && TargetSamples == samples) {
		return true;
	}

	// A resolution change makes the old set the wrong size, and a chain that gained or lost bloom
	// makes the scene target the wrong format.  Both are for the rest of the run, not for a frame.
	Release_Targets();
	if (!Create_Targets(width, height, floating, samples)) {
		Release_Targets();
		strcpy(Diagnostic, "post-process off: the device refused the offscreen targets");
		return false;
	}
	Describe();
	return true;
}

ID3D11RenderTargetView * DX11PostProcessClass::Scene_View()
{
	return Ensure_Buffers() ? SceneTarget.View : NULL;
}

ID3D11ShaderResourceView * DX11PostProcessClass::Scene_Source()
{
	if (SceneResolve.Texture == NULL) {
		return SceneTarget.Resource;
	}
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneResolve.Texture, SceneTarget.Texture);
	return SceneResolve.Resource;
}

void DX11PostProcessClass::Begin_Frame()
{
	Resolved = false;
}

void DX11PostProcessClass::Open_Frame()
{
	Occluded = false;
	Hazed = false;
	Shafted = false;
	ShroudRemembered = false;
	BackdropBlurred = false;
}

bool DX11PostProcessClass::Wants_Atmosphere_Programs() const
{
	return AtmosphereFade[2] > 0.0f && AtmosphereShaders[ATMOSPHERE_PROGRAMS - 1] == NULL && !AtmosphereRefused
		&& ShadersReady;
}

const char * DX11PostProcessClass::Atmosphere_Program_Source(unsigned index) const
{
	static const std::string sources[ATMOSPHERE_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + SKY_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + ATMOSPHERE_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_Atmosphere_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&AtmosphereShaders[index]))) {
		AtmosphereShaders[index] = NULL;
		AtmosphereRefused = true;
	}
}

void DX11PostProcessClass::Compile_Atmosphere_Programs()
{
	const char * const bodies[ATMOSPHERE_PROGRAMS] = { SKY_SHADER_BODY, ATMOSPHERE_SHADER_BODY };
	for (unsigned index = 0; index < ATMOSPHERE_PROGRAMS; ++index) {
		if (AtmosphereShaders[index] == NULL && !compile_pixel_shader(compiler_function(), Device->Get_Device(),
				bodies[index], &AtmosphereShaders[index])) {
			AtmosphereShaders[index] = NULL;
			AtmosphereRefused = true;
		}
	}
}

bool DX11PostProcessClass::Draw_Atmosphere(unsigned index, ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	if (AtmosphereFade[2] <= 0.0f || AtmosphereShaders[index] == NULL || Resolved || !ShadersReady
			|| SceneTarget.View == NULL || Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	// The game pushes the shroud every frame this is on.  The haze, when it ran, has taken it for the
	// frame's view already.
	if (!Hazed) {
		HazeShroudView = HazeShroudSource;
	}

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = AtmosphereShaders[index];
	pass.Extra = depth;
	pass.Blend = depth != NULL;
	pass.World = true;
	pass.Atmosphere = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	return true;
}

bool DX11PostProcessClass::Sky_Scene(const float world_from_clip[16])
{
	return Draw_Atmosphere(0, NULL, world_from_clip);
}

bool DX11PostProcessClass::Atmosphere_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	return Draw_Atmosphere(1, depth, world_from_clip);
}

bool DX11PostProcessClass::Wants_Haze_Program() const
{
	return HazeStrength > 0.0f && HazeShader == NULL && !HazeRefused && ShadersReady;
}

const char * DX11PostProcessClass::Haze_Program_Source() const
{
	static const std::string source = std::string(PIXEL_SHADER_PROLOGUE) + HAZE_SHADER_BODY;
	return source.c_str();
}

void DX11PostProcessClass::Load_Haze_Program(const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL, &HazeShader))) {
		HazeShader = NULL;
		HazeRefused = true;
	}
}

void DX11PostProcessClass::Compile_Haze_Program()
{
	if (HazeShader == NULL && !compile_pixel_shader(compiler_function(), Device->Get_Device(),
			HAZE_SHADER_BODY, &HazeShader)) {
		HazeShader = NULL;
		HazeRefused = true;
	}
}

bool DX11PostProcessClass::Wants_World_Ao_Programs() const
{
	return WorldAoShaders[WORLD_AO_PROGRAMS - 1] == NULL && !WorldAoRefused && ShadersReady;
}

const char * DX11PostProcessClass::World_Ao_Program_Source(unsigned index) const
{
	static const std::string sources[WORLD_AO_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_BLUR_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + WORLD_AO_UPSAMPLE_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_World_Ao_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&WorldAoShaders[index]))) {
		WorldAoShaders[index] = NULL;
		WorldAoRefused = true;
	}
}

void DX11PostProcessClass::Compile_World_Ao_Programs()
{
	const char * const bodies[WORLD_AO_PROGRAMS] = { WORLD_AO_SHADER_BODY, WORLD_AO_BLUR_SHADER_BODY,
		WORLD_AO_UPSAMPLE_SHADER_BODY };
	for (unsigned index = 0; index < WORLD_AO_PROGRAMS; ++index) {
		if (!compile_pixel_shader(compiler_function(), Device->Get_Device(), bodies[index], &WorldAoShaders[index])) {
			WorldAoShaders[index] = NULL;
			WorldAoRefused = true;
		}
	}
}

bool DX11PostProcessClass::Ensure_World_Ao_Targets(unsigned & half_width, unsigned & half_height)
{
	half_width = (Width + 1) / 2;
	half_height = (Height + 1) / 2;
	if (WorldAoTargets[0].Texture == NULL
			&& (!Create_Target(WorldAoTargets[0], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT)
				|| !Create_Target(WorldAoTargets[1], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT))) {
		Release_Target(WorldAoTargets[0]);
		Release_Target(WorldAoTargets[1]);
		return false;
	}
	return true;
}

ID3D11ShaderResourceView * DX11PostProcessClass::World_Ao_Shroud(float rect[4]) const
{
	memcpy(rect, HazeShroudRect, sizeof(HazeShroudRect));
	return HazeShroudSource;
}

// The traced occlusion's texture is the half size targets' size, or it is not used.
static bool half_sized(ID3D11ShaderResourceView * view, unsigned width, unsigned height)
{
	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	ID3D11Texture2D * texture = NULL;
	bool same = false;
	if (resource != NULL && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		D3D11_TEXTURE2D_DESC description;
		texture->GetDesc(&description);
		same = description.Width == width && description.Height == height;
		texture->Release();
	}
	if (resource != NULL) {
		resource->Release();
	}
	return same;
}

bool DX11PostProcessClass::World_Occlusion_Ready() const
{
	return !Occluded && !Resolved && ShadersReady && SceneTarget.View != NULL
		&& Device->Get_Scene_View() == SceneTarget.View && !WorldAoRefused
		&& WorldAoShaders[WORLD_AO_PROGRAMS - 1] != NULL;
}

bool DX11PostProcessClass::World_Occlude_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16], float projection_x, float projection_y, ID3D11ShaderResourceView * traced,
	bool * took_traced)
{
	if (took_traced != NULL) {
		*took_traced = false;
	}
	if (!World_Occlusion_Ready()) {
		return false;
	}
	unsigned half_width = 0;
	unsigned half_height = 0;
	if (!Ensure_World_Ao_Targets(half_width, half_height)) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	WorldAoProjection[0] = projection_x;
	WorldAoProjection[1] = projection_y;
	// Read and left for the haze, which comes later in the same frame and takes it for good.
	HazeShroudView = HazeShroudSource;

	if (traced != NULL && !half_sized(traced, half_width, half_height)) {
		traced = NULL;
	}
	PassSetup pass;
	if (traced == NULL) {
		memset(&pass, 0, sizeof(pass));
		pass.Shader = WorldAoShaders[0];
		pass.Extra = depth;
		pass.World = true;
		pass.Destination = WorldAoTargets[0].View;
		pass.DestinationWidth = half_width;
		pass.DestinationHeight = half_height;
		pass.SourceWidth = Width;
		pass.SourceHeight = Height;
		Draw_Pass(pass);
	}

	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[1];
	pass.World = true;
	pass.Source = (traced != NULL) ? traced : WorldAoTargets[0].Resource;
	pass.Destination = WorldAoTargets[1].View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	pass.BlurX = 1.0f;
	Draw_Pass(pass);

	pass.Source = WorldAoTargets[1].Resource;
	pass.Destination = WorldAoTargets[0].View;
	pass.BlurX = 0.0f;
	pass.BlurY = 1.0f;
	Draw_Pass(pass);

	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[2];
	pass.Source = WorldAoTargets[0].Resource;
	pass.Extra = depth;
	pass.World = true;
	pass.Multiply = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	Draw_Pass(pass);
	Occluded = true;
	if (took_traced != NULL) {
		*took_traced = traced != NULL;
	}
	return true;
}

bool DX11PostProcessClass::Haze_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	if (HazeStrength <= 0.0f || Hazed || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	// Loaded the first time the prototype asks for it (Load_Haze_Program), so a refusal costs the
	// haze alone and a launch without the switch loads nothing more.
	if (HazeShader == NULL) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));

	// Borrowed for this frame only: the shroud's texture (and so its mirror) can be freed by the
	// next map or a shroud reset, and a frame that set none draws with none.
	HazeShroudView = HazeShroudSource;
	HazeShroudSource = NULL;

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = HazeShader;
	pass.Extra = depth;
	pass.Blend = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	Hazed = true;
	return true;
}

bool DX11PostProcessClass::Wants_Sun_Shaft_Programs() const
{
	return SunShafts.Strength > 0.0f && SunShaftShaders[SUN_SHAFT_PROGRAMS - 1] == NULL && !SunShaftRefused
		&& ShadersReady;
}

const char * DX11PostProcessClass::Sun_Shaft_Program_Source(unsigned index) const
{
	static const std::string sources[SUN_SHAFT_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + SUN_SHAFTS_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + SUN_SHAFTS_BLUR_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + SUN_SHAFTS_COMPOSITE_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_Sun_Shaft_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&SunShaftShaders[index]))) {
		SunShaftShaders[index] = NULL;
		SunShaftRefused = true;
	}
}

void DX11PostProcessClass::Compile_Sun_Shaft_Programs()
{
	const char * const bodies[SUN_SHAFT_PROGRAMS] = { SUN_SHAFTS_SHADER_BODY, SUN_SHAFTS_BLUR_SHADER_BODY,
		SUN_SHAFTS_COMPOSITE_SHADER_BODY };
	for (unsigned index = 0; index < SUN_SHAFT_PROGRAMS; ++index) {
		if (SunShaftShaders[index] == NULL && !compile_pixel_shader(compiler_function(), Device->Get_Device(),
				bodies[index], &SunShaftShaders[index])) {
			SunShaftShaders[index] = NULL;
			SunShaftRefused = true;
		}
	}
}

bool DX11PostProcessClass::Sun_Shafts_Scene(ID3D11ShaderResourceView * depth, const float world_from_clip[16],
	ID3D11ShaderResourceView * sun_map, const float sun_from_world[16], float bias, float shadow_strength,
	float projection_x, float projection_y, ID3D11ShaderResourceView * heights, const float * heights_mapping)
{
	if (SunShafts.Strength <= 0.0f || Shafted || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View || SunShaftRefused
			|| SunShaftShaders[0] == NULL || SunShaftShaders[1] == NULL || SunShaftShaders[2] == NULL || sun_map == NULL
			|| depth == NULL || ShaftBlendState == NULL) {
		return false;
	}
	unsigned half_width = 0;
	unsigned half_height = 0;
	if (!Ensure_World_Ao_Targets(half_width, half_height)) {
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	memcpy(SunShaftSunFromWorld, sun_from_world, sizeof(SunShaftSunFromWorld));
	invert_shaft_matrix(sun_from_world, SunShaftWorldFromSun);
	if (heights_mapping == NULL) {
		heights = NULL;
	}
	SunShaftTerrain[0] = (heights != NULL) ? heights_mapping[0] : 0.0f;
	SunShaftTerrain[1] = (heights != NULL) ? heights_mapping[1] : 0.0f;
	SunShaftTerrain[2] = (heights != NULL) ? heights_mapping[2] : 0.0f;
	SunShaftTerrain[3] = (heights != NULL) ? 1.0f : 0.0f;
	SunShaftBias = bias;
	SunShaftShadowStrength = shadow_strength;
	WorldAoProjection[0] = projection_x;
	WorldAoProjection[1] = projection_y;
	// The haze, when it ran, has taken the frame's shroud already, as for the atmosphere.
	if (!Hazed) {
		HazeShroudView = HazeShroudSource;
	}

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = SunShaftShaders[0];
	pass.Extra = depth;
	pass.World = true;
	pass.SunMap = sun_map;
	pass.Heights = heights;
	pass.Destination = WorldAoTargets[0].View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	// The shafts' own blur, which carries the lift and the dimming apart and reads the stop's distance
	// from the depth, since both channels of the half size targets hold an amount.
	memset(&pass, 0, sizeof(pass));
	pass.Shader = SunShaftShaders[1];
	pass.World = true;
	pass.Extra = depth;
	pass.Source = WorldAoTargets[0].Resource;
	pass.Destination = WorldAoTargets[1].View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	pass.BlurX = 1.0f;
	Draw_Pass(pass);

	pass.Source = WorldAoTargets[1].Resource;
	pass.Destination = WorldAoTargets[0].View;
	pass.BlurX = 0.0f;
	pass.BlurY = 1.0f;
	Draw_Pass(pass);

	memset(&pass, 0, sizeof(pass));
	pass.Shader = SunShaftShaders[2];
	pass.Source = WorldAoTargets[0].Resource;
	pass.Extra = depth;
	pass.World = true;
	pass.Shafts = true;
	// the composite decides the lift's gate again per full size pixel, so it reads both maps too
	pass.SunMap = sun_map;
	pass.Heights = heights;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	Draw_Pass(pass);
	Shafted = true;
	return true;
}

bool DX11PostProcessClass::Occlude_Scene(ID3D11ShaderResourceView * depth)
{
	if (Occluded || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	bool wanted = false;
	for (unsigned index = 0; index < ChainLength; ++index) {
		wanted = wanted || Chain[index] == DX11_POST_AO;
	}
	if (!wanted) {
		return false;
	}
	// The pass keys on the scene's colour and cannot read the target it multiplies, so it reads a
	// copy, made the first time and kept with the other targets.
	if (SceneCopy.Texture == NULL
			&& !Create_Target(SceneCopy, Width, Height, TargetsAreFloat ? FLOAT_FORMAT : EIGHT_BIT_FORMAT)) {
		Release_Target(SceneCopy);
		return false;
	}
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneCopy.Texture, SceneTarget.Texture);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = SceneOcclusionShader;
	pass.Source = SceneCopy.Resource;
	pass.Extra = depth;
	pass.Occlusion = true;
	pass.Multiply = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	Occluded = true;
	return true;
}

// The soft picture's texels a cell, and the most texels it may have on a side: four is ten world
// units a texel at the game's forty unit cells, a terrain cell, and a 1024 cell map would want 4096.
static const unsigned SHROUD_SOFT_SCALE = 4;
static const unsigned SHROUD_SOFT_MOST = 4096;
static const DXGI_FORMAT SHROUD_FORMAT = DXGI_FORMAT_R16G16B16A16_UNORM;
// The easing: the decay's time constant, the floor that ends a whole swing in this long, and how long
// after the last write the passes keep running, which is past the longest swing the two allow.
static const float SHROUD_EASE_SECONDS = 0.1f;
static const float SHROUD_SWING_SECONDS = 0.4f;
static const float SHROUD_SETTLE_SECONDS = 0.6f;

void DX11PostProcessClass::Release_Shroud()
{
	Release_Target(ShroudHeld[0]);
	Release_Target(ShroudHeld[1]);
	Release_Target(ShroudSoft);
	ShroudCellsWidth = 0;
	ShroudCellsHeight = 0;
	ShroudSettle = 0.0f;
	ShroudFogLight = -1.0f;
}

bool DX11PostProcessClass::Wants_Shroud_Programs() const
{
	return ShroudShaders[SHROUD_PROGRAMS - 1] == NULL && !ShroudRefused && ShadersReady;
}

const char * DX11PostProcessClass::Shroud_Program_Source(unsigned index) const
{
	static const std::string sources[SHROUD_PROGRAMS] = {
		std::string(PIXEL_SHADER_PROLOGUE) + SHROUD_EASE_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + SHROUD_SOFTEN_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + SHROUD_MEMORY_SHADER_BODY
	};
	return sources[index].c_str();
}

void DX11PostProcessClass::Load_Shroud_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (bytecode == NULL || FAILED(Device->Get_Device()->CreatePixelShader(bytecode, size, NULL,
			&ShroudShaders[index]))) {
		ShroudShaders[index] = NULL;
		ShroudRefused = true;
	}
}

ID3D11ShaderResourceView * DX11PostProcessClass::Soften_Shroud(ID3D11ShaderResourceView * cells,
	float seconds, bool written, float fog_light, bool fogged)
{
	ShroudFogLight = -1.0f;
	// Off (Classic, a map going, a device reset): the pictures go, and the next map's levels start
	// where its logic has them rather than easing out of the last map's.
	if (cells == NULL) {
		Release_Shroud();
		return NULL;
	}
	if (!ShadersReady || ShroudRefused || ShroudShaders[SHROUD_PROGRAMS - 1] == NULL) {
		return NULL;
	}

	ID3D11Resource * resource = NULL;
	cells->GetResource(&resource);
	ID3D11Texture2D * texture = NULL;
	if (resource == NULL || FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&texture))) {
		release_interface(reinterpret_cast<IUnknown **>(&resource));
		return NULL;
	}
	D3D11_TEXTURE2D_DESC description;
	texture->GetDesc(&description);
	texture->Release();
	resource->Release();

	// A new map's grid, or the first call: new pictures, and the levels start where logic has them.
	bool fresh = false;
	if (ShroudSoft.Texture == NULL || description.Width != ShroudCellsWidth || description.Height != ShroudCellsHeight) {
		Release_Shroud();
		const unsigned side = (description.Width > description.Height) ? description.Width : description.Height;
		unsigned scale = SHROUD_SOFT_SCALE;
		while (scale > 1 && side * scale > SHROUD_SOFT_MOST) {
			--scale;
		}
		if (!Create_Target(ShroudHeld[0], description.Width, description.Height, SHROUD_FORMAT)
				|| !Create_Target(ShroudHeld[1], description.Width, description.Height, SHROUD_FORMAT)
				|| !Create_Target(ShroudSoft, description.Width * scale, description.Height * scale, SHROUD_FORMAT)) {
			Release_Shroud();
			return NULL;
		}
		ShroudCellsWidth = description.Width;
		ShroudCellsHeight = description.Height;
		ShroudScale = scale;
		ShroudHeldIndex = 0;
		fresh = true;
	}

	ShroudFogLight = fog_light;
	if (written || fresh) {
		ShroudSettle = SHROUD_SETTLE_SECONDS;
	}
	// The last fogged cell cleared still eases out over the settle; after it every texel is one.
	ShroudFogged = fogged || ShroudSettle > 0.0f;
	if (ShroudSettle <= 0.0f) {
		return ShroudSoft.Resource;
	}
	if (seconds < 0.0f) {
		seconds = 0.0f;
	}
	ShroudSettle -= seconds;

	ID3D11DeviceContext * context = Device->Get_Context();
	ID3D11RenderTargetView * saved_target = NULL;
	ID3D11DepthStencilView * saved_depth = NULL;
	context->OMGetRenderTargets(1, &saved_target, &saved_depth);
	D3D11_VIEWPORT saved_viewport;
	UINT saved_viewports = 1;
	context->RSGetViewports(&saved_viewports, &saved_viewport);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = ShroudShaders[0];
	pass.Source = cells;
	pass.Extra = ShroudHeld[ShroudHeldIndex].Resource;
	pass.Destination = ShroudHeld[1 - ShroudHeldIndex].View;
	pass.DestinationWidth = ShroudCellsWidth;
	pass.DestinationHeight = ShroudCellsHeight;
	pass.SourceWidth = ShroudCellsWidth;
	pass.SourceHeight = ShroudCellsHeight;
	pass.BlurX = fresh ? 1.0f : 1.0f - expf(-seconds / SHROUD_EASE_SECONDS);
	pass.BlurY = fresh ? 1.0f : seconds / SHROUD_SWING_SECONDS;
	Draw_Pass(pass);
	ShroudHeldIndex = 1 - ShroudHeldIndex;

	memset(&pass, 0, sizeof(pass));
	pass.Shader = ShroudShaders[1];
	pass.Source = ShroudHeld[ShroudHeldIndex].Resource;
	pass.Destination = ShroudSoft.View;
	pass.DestinationWidth = ShroudCellsWidth * ShroudScale;
	pass.DestinationHeight = ShroudCellsHeight * ShroudScale;
	pass.SourceWidth = ShroudCellsWidth;
	pass.SourceHeight = ShroudCellsHeight;
	pass.BlurX = static_cast<float>(ShroudScale);
	Draw_Pass(pass);

	context->OMSetRenderTargets(1, &saved_target, saved_depth);
	if (saved_viewports == 1) {
		context->RSSetViewports(1, &saved_viewport);
	}
	release_interface(reinterpret_cast<IUnknown **>(&saved_target));
	release_interface(reinterpret_cast<IUnknown **>(&saved_depth));
	return ShroudSoft.Resource;
}

bool DX11PostProcessClass::Shroud_Memory_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16])
{
	// No fog anywhere (an observer with it off, a map revealed): a copy of the scene for nothing.
	if (ShroudFogLight < 0.0f || !ShroudFogged || ShroudRemembered || Resolved || !ShadersReady || HazeShroudSource == NULL
			|| ShroudShaders[2] == NULL || SceneTarget.View == NULL || Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	// The pass reads the colour it blends over, so it reads a copy, as Occlude_Scene does.
	if (SceneCopy.Texture == NULL
			&& !Create_Target(SceneCopy, Width, Height, TargetsAreFloat ? FLOAT_FORMAT : EIGHT_BIT_FORMAT)) {
		Release_Target(SceneCopy);
		return false;
	}
	DX11Resource_Copy_Readable(Device->Get_Context(), SceneCopy.Texture, SceneTarget.Texture);
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	// Read and left for the haze, which comes after in the same frame and takes it for good.
	HazeShroudView = HazeShroudSource;

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = ShroudShaders[2];
	pass.Source = SceneCopy.Resource;
	pass.Extra = depth;
	pass.Blend = true;
	pass.World = true;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.BlurX = ShroudFogLight;
	Draw_Pass(pass);
	ShroudRemembered = true;
	return true;
}

void DX11PostProcessClass::Set_Heat_Splats(const float * splats, unsigned count)
{
	if (count > DIRECT3D11_HEAT_SPLAT_LIMIT) {
		count = DIRECT3D11_HEAT_SPLAT_LIMIT;
	}
	HeatSplats.assign(splats, splats + count * DIRECT3D11_HEAT_SPLAT_FLOATS);
}

void DX11PostProcessClass::Release_Heat_Haze()
{
	release_interface(reinterpret_cast<IUnknown **>(&HeatBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&HeatHazeShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatVertexShader));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatView));
	release_interface(reinterpret_cast<IUnknown **>(&HeatSplatBuffer));
}

bool DX11PostProcessClass::Wants_Heat_Haze_Programs() const
{
	return ShadersReady && HeatHazeShader == NULL && !HeatHazeRefused;
}

const char * DX11PostProcessClass::Heat_Haze_Program_Source(unsigned index) const
{
	static const std::string sources[HEAT_HAZE_PROGRAMS] = {
		std::string(HEAT_SPLAT_VERTEX_SOURCE),
		std::string(PIXEL_SHADER_PROLOGUE) + HEAT_SPLAT_SHADER_BODY,
		std::string(PIXEL_SHADER_PROLOGUE) + HEAT_HAZE_SHADER_BODY
	};
	return sources[index].c_str();
}

const char * DX11PostProcessClass::Heat_Haze_Program_Profile(unsigned index) const
{
	return (index == 0) ? VERTEX_PROFILE : PIXEL_PROFILE;
}

void DX11PostProcessClass::Load_Heat_Haze_Program(unsigned index, const void * bytecode, SIZE_T size)
{
	if (HeatHazeRefused) {
		return;
	}
	ID3D11Device * device = Device->Get_Device();
	HRESULT result = E_FAIL;
	if (bytecode != NULL) {
		result = (index == 0) ? device->CreateVertexShader(bytecode, size, NULL, &HeatSplatVertexShader)
			: device->CreatePixelShader(bytecode, size, NULL, (index == 1) ? &HeatSplatShader : &HeatHazeShader);
	}
	// the last program in makes the rest of what the pass needs
	if (FAILED(result) || (index + 1 == HEAT_HAZE_PROGRAMS && !Create_Heat_Haze())) {
		Release_Heat_Haze();
		HeatHazeRefused = true;
	}
}

void DX11PostProcessClass::Compile_Heat_Haze_Programs()
{
	D3DCompileFunction compiler = compiler_function();
	for (unsigned index = 0; index < HEAT_HAZE_PROGRAMS; ++index) {
		ID3DBlob * code = NULL;
		ID3DBlob * errors = NULL;
		const char * source = Heat_Haze_Program_Source(index);
		HRESULT result = (compiler == NULL) ? E_FAIL : compiler(source, strlen(source), "dx11heat", NULL, NULL,
			ENTRY_POINT, Heat_Haze_Program_Profile(index), 0, 0, &code, &errors);
		release_interface(reinterpret_cast<IUnknown **>(&errors));
		const bool made = SUCCEEDED(result) && code != NULL;
		Load_Heat_Haze_Program(index, made ? code->GetBufferPointer() : NULL, made ? code->GetBufferSize() : 0);
		release_interface(reinterpret_cast<IUnknown **>(&code));
	}
}

bool DX11PostProcessClass::Create_Heat_Haze()
{
	ID3D11Device * device = Device->Get_Device();
	D3D11_BUFFER_DESC buffer;
	ZeroMemory(&buffer, sizeof(buffer));
	buffer.ByteWidth = DIRECT3D11_HEAT_SPLAT_LIMIT * DIRECT3D11_HEAT_SPLAT_FLOATS * sizeof(float);
	buffer.Usage = D3D11_USAGE_DYNAMIC;
	buffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(device->CreateBuffer(&buffer, NULL, &HeatSplatBuffer))) {
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC view;
	ZeroMemory(&view, sizeof(view));
	view.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	view.Buffer.FirstElement = 0;
	view.Buffer.NumElements = DIRECT3D11_HEAT_SPLAT_LIMIT * DIRECT3D11_HEAT_SPLAT_FLOATS / 4;
	if (FAILED(device->CreateShaderResourceView(HeatSplatBuffer, &view, &HeatSplatView))) {
		return false;
	}

	// Every splat's shift and weight added to what the target holds.
	D3D11_BLEND_DESC blend;
	ZeroMemory(&blend, sizeof(blend));
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	return SUCCEEDED(device->CreateBlendState(&blend, &HeatBlendState));
}

bool DX11PostProcessClass::Heat_Haze_Scene(ID3D11ShaderResourceView * depth)
{
	if (HeatSplats.empty()) {
		return false;
	}
	const unsigned count = static_cast<unsigned>(HeatSplats.size() / DIRECT3D11_HEAT_SPLAT_FLOATS);
	std::vector<float> splats;
	splats.swap(HeatSplats);	// spent whether or not this frame can draw them
	if (Resolved || !ShadersReady || HeatHazeRefused || depth == NULL || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View) {
		return false;
	}
	// loaded when the switch was first seen on (Direct3D11_Set_Heat_Splats), never on a frame's draw
	if (HeatHazeShader == NULL || HeatBlendState == NULL) {
		return false;
	}
	const unsigned half_width = (Width + 1) / 2;
	const unsigned half_height = (Height + 1) / 2;
	if (HeatTarget.Texture == NULL
			&& !Create_Target(HeatTarget, half_width, half_height, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
		Release_Target(HeatTarget);
		return false;
	}
	if (SceneCopy.Texture == NULL
			&& !Create_Target(SceneCopy, Width, Height, TargetsAreFloat ? FLOAT_FORMAT : EIGHT_BIT_FORMAT)) {
		Release_Target(SceneCopy);
		return false;
	}

	ID3D11DeviceContext * context = Device->Get_Context();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(HeatSplatBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return false;
	}
	memcpy(mapped.pData, &splats[0], splats.size() * sizeof(float));
	context->Unmap(HeatSplatBuffer, 0);

	const float nothing[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HeatTarget.View, nothing);
	context->OMSetRenderTargets(1, &HeatTarget.View, NULL);
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(half_width);
	viewport.Height = static_cast<float>(half_height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);
	context->OMSetBlendState(HeatBlendState, nothing, 0xffffffff);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);
	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(HeatSplatVertexShader, NULL, 0);
	context->VSSetShaderResources(0, 1, &HeatSplatView);
	context->PSSetShader(HeatSplatShader, NULL, 0);
	context->PSSetShaderResources(1, 1, &depth);
	context->Draw(count * 6, 0);
	ID3D11ShaderResourceView * none = NULL;
	context->VSSetShaderResources(0, 1, &none);
	context->PSSetShaderResources(1, 1, &none);

	DX11Resource_Copy_Readable(context, SceneCopy.Texture, SceneTarget.Texture);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = HeatHazeShader;
	pass.Source = SceneCopy.Resource;
	pass.Extra = HeatTarget.Resource;
	pass.Destination = SceneTarget.View;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);
	return true;
}

void DX11PostProcessClass::Draw_Pass(const PassSetup & pass)
{
	ID3D11DeviceContext * context = Device->Get_Context();

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	PostConstantBlock block;
	block.TexelSize[0] = 1.0f / static_cast<float>(pass.SourceWidth);
	block.TexelSize[1] = 1.0f / static_cast<float>(pass.SourceHeight);
	block.TexelSize[2] = static_cast<float>(pass.SourceWidth);
	block.TexelSize[3] = static_cast<float>(pass.SourceHeight);
	block.BlurDirection[0] = pass.BlurX;
	block.BlurDirection[1] = pass.BlurY;
	block.BlurDirection[2] = pass.Saturation;
	block.BlurDirection[3] = pass.Clarity;
	block.Tuning[0] = BloomThreshold;
	block.Tuning[1] = BloomIntensity;
	block.Tuning[2] = TONE_CURVE_KNEE;
	block.Tuning[3] = BLOOM_KNEE;
	if (pass.Occlusion) {
		block.Tuning[0] = NearPlane;
		block.Tuning[1] = FarPlane;
		block.Tuning[2] = AO_RADIUS;
		block.Tuning[3] = AO_SPREAD;
	}
	block.WarpInfo[0] = static_cast<float>(WarpCount);
	block.WarpInfo[1] = static_cast<float>(pass.SourceWidth) / static_cast<float>(pass.SourceHeight);
	block.WarpInfo[2] = 0.0f;
	block.WarpInfo[3] = 0.0f;
	for (unsigned index = 0; index < DX11_POST_WARP_LIMIT; ++index) {
		const DX11PostWarp & warp = Warps[index];
		block.WarpCentre[index][0] = warp.CentreX;
		block.WarpCentre[index][1] = warp.CentreY;
		block.WarpCentre[index][2] = warp.Radius;
		block.WarpCentre[index][3] = warp.Pull;
		block.WarpRing[index][0] = warp.RingRadius;
		block.WarpRing[index][1] = warp.RingWidth;
		block.WarpRing[index][2] = warp.RingStrength;
		block.WarpRing[index][3] = warp.RingSquash;
	}
	block.Grade[0] = pass.Ungraded ? 0.0f : GradeAmount;
	block.Grade[1] = GRADE_CONTRAST;
	block.Grade[2] = GRADE_SATURATION;
	block.Grade[3] = GRADE_SPLIT;
	block.Haze[0] = HazeTop;
	block.Haze[1] = HazeThickness;
	block.Haze[2] = HazeStrength;
	block.Haze[3] = HazeReach;
	block.HazeColour[0] = HazeColour[0];
	block.HazeColour[1] = HazeColour[1];
	block.HazeColour[2] = HazeColour[2];
	block.HazeColour[3] = HazeWater;
	memcpy(block.WorldFromClip, HazeWorldFromClip, sizeof(block.WorldFromClip));
	memcpy(block.HazeSun, HazeSun, sizeof(block.HazeSun));
	// the program reads only the sun's bearing, so its z says whether a shroud texture is bound
	block.HazeSun[2] = (HazeShroudView != NULL) ? 1.0f : 0.0f;
	memcpy(block.HazeShroudRect, HazeShroudRect, sizeof(block.HazeShroudRect));
	memcpy(block.WorldAoProjection, WorldAoProjection, sizeof(block.WorldAoProjection));
	memcpy(block.SunFromWorld, SunShaftSunFromWorld, sizeof(block.SunFromWorld));
	block.Shafts[0] = SunShafts.Ground;
	block.Shafts[1] = SunShafts.Top;
	block.Shafts[2] = SunShafts.Falloff;
	block.Shafts[3] = SunShafts.Samples;
	block.ShaftSun[0] = SunShafts.TowardSun[0];
	block.ShaftSun[1] = SunShafts.TowardSun[1];
	block.ShaftSun[2] = SunShafts.Glare;
	block.ShaftSun[3] = SunShaftBias;
	block.ShaftLook[0] = SunShafts.Scale;
	block.ShaftLook[1] = SunShafts.Band;
	block.ShaftLook[2] = SunShafts.Dark;
	block.ShaftLook[3] = SunShafts.Slack;
	block.ShaftGlare[0] = SunShafts.ShallowTop;
	block.ShaftGlare[1] = SunShafts.ShallowSpan;
	block.ShaftGlare[2] = SunShafts.WaterCount;
	block.ShaftGlare[3] = SunShafts.Side;
	block.ShaftLimit[0] = SunShafts.DarkCap * SunShaftShadowStrength;
	block.ShaftLimit[1] = SunShafts.ShadedMost;
	block.ShaftLimit[2] = SunShafts.Floor;
	block.ShaftLimit[3] = SunShafts.NormalOffset;
	block.ShaftBand[0] = SunShafts.Radius;
	block.ShaftBand[1] = SunShafts.StreakWidth;
	block.ShaftBand[2] = SunShafts.StreakLength;
	memcpy(block.ShaftTerrain, SunShaftTerrain, sizeof(block.ShaftTerrain));
	memcpy(block.WorldFromSun, SunShaftWorldFromSun, sizeof(block.WorldFromSun));
	block.ShaftBand[3] = 0.0f;
	for (unsigned channel = 0; channel < 3; ++channel) {
		block.ShaftColour[channel] = SunShafts.Colour[channel];
	}
	block.ShaftColour[3] = SunShafts.Strength;
	memcpy(block.ShaftWater, SunShafts.WaterBoxes, sizeof(block.ShaftWater));
	for (unsigned area = 0; area < DX11_POST_SUN_SHAFT_WATER_AREAS; ++area) {
		block.ShaftWaterHeight[area][0] = SunShafts.WaterHeights[area];
		block.ShaftWaterHeight[area][1] = 0.0f;
		block.ShaftWaterHeight[area][2] = 0.0f;
		block.ShaftWaterHeight[area][3] = 0.0f;
	}
	if (pass.Atmosphere) {
		memcpy(block.Haze, AtmosphereFade, sizeof(block.Haze));
		memcpy(block.HazeColour, AtmosphereHorizon, sizeof(AtmosphereHorizon));
		memcpy(block.BlurDirection, AtmosphereTint, sizeof(AtmosphereTint));
		memcpy(block.WorldAoProjection, AtmosphereMapSize, sizeof(AtmosphereMapSize));
	}
	memcpy(mapped.pData, &block, sizeof(block));
	context->Unmap(ConstantBuffer, 0);

	// No depth: the pass covers every pixel exactly once and a depth buffer left bound would test
	// the triangle against the scene it is reading.
	context->OMSetRenderTargets(1, &pass.Destination, NULL);

	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = static_cast<float>(pass.DestinationX);
	viewport.TopLeftY = static_cast<float>(pass.DestinationY);
	viewport.Width = static_cast<float>(pass.DestinationWidth);
	viewport.Height = static_cast<float>(pass.DestinationHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->OMSetBlendState(pass.Shafts ? ShaftBlendState : pass.Multiply ? MultiplyBlendState
		: pass.Blend ? AlphaBlendState : BlendState, blend_factor, 0xffffffff);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);

	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(VertexShader, NULL, 0);
	context->PSSetShader(pass.Shader, NULL, 0);
	context->PSSetSamplers(0, 1, &Sampler);
	ID3D11ShaderResourceView * resources[2] = { pass.Source, pass.Extra };
	context->PSSetShaderResources(0, 2, resources);
	context->PSSetConstantBuffers(0, 1, &ConstantBuffer);
	const bool haze = pass.World || pass.Shader == HazeShader;
	if (haze) {
		// The same buffer at b1 as well, where the haze program reads its own fields of it, and
		// the shroud at t2.
		context->PSSetConstantBuffers(1, 1, &ConstantBuffer);
		context->PSSetShaderResources(2, 1, &HazeShroudView);
	}
	if (pass.Atmosphere) {
		context->PSSetShaderResources(3, 1, &AtmosphereSky);
		context->PSSetSamplers(1, 1, &WrapSampler);
	}
	if (pass.SunMap != NULL) {
		context->PSSetShaderResources(3, 1, &pass.SunMap);
	}
	if (pass.Heights != NULL) {
		context->PSSetShaderResources(4, 1, &pass.Heights);
	}

	context->Draw(FULL_SCREEN_VERTEX_COUNT, 0);

	if (pass.Heights != NULL) {
		ID3D11ShaderResourceView * no_heights = NULL;
		context->PSSetShaderResources(4, 1, &no_heights);
	}

	if (haze) {
		ID3D11ShaderResourceView * no_haze_resource = NULL;
		context->PSSetShaderResources(2, 1, &no_haze_resource);
	}
	if (pass.Atmosphere || pass.SunMap != NULL) {
		ID3D11ShaderResourceView * no_sky = NULL;
		context->PSSetShaderResources(3, 1, &no_sky);
	}

	// The texture this pass read is the target the next one writes, and D3D11 unbinds a resource
	// silently when it is bound both ways at once.  Letting go here says which of the two bindings
	// was meant.
	ID3D11ShaderResourceView * no_resources[2] = { NULL, NULL };
	context->PSSetShaderResources(0, 2, no_resources);
}

ID3D11ShaderResourceView * DX11PostProcessClass::Run_Bloom(ID3D11RenderTargetView * destination,
	ID3D11ShaderResourceView * scene)
{
	const float bloom_texel_x = 1.0f / static_cast<float>(BloomWidth);
	const float bloom_texel_y = 1.0f / static_cast<float>(BloomHeight);

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));

	// The bright pass reads the full size scene and writes the quarter size target, so it is the
	// one pass whose source and destination sizes differ.
	pass.Shader = BloomExtractShader;
	pass.Source = scene;
	pass.Destination = BloomTargets[0].View;
	pass.DestinationWidth = BloomWidth;
	pass.DestinationHeight = BloomHeight;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.Saturation = BloomSaturation;
	Draw_Pass(pass);
	pass.Saturation = 0.0f;

	// The clarity's blur is the first two pairs, whatever the glow asks for; with the glow off
	// those two pairs are all that run.
	const bool clarity = (ClarityStrength > 0.0f);
	unsigned pairs = (BloomIntensity > 0.0f) ? BloomBlurPasses : 0;
	if (clarity && pairs < CLARITY_BLUR_PASSES) {
		pairs = CLARITY_BLUR_PASSES;
	}

	pass.Shader = BloomBlurShader;
	pass.SourceWidth = BloomWidth;
	pass.SourceHeight = BloomHeight;
	unsigned source = 0;
	for (unsigned iteration = 0; iteration < pairs; ++iteration) {
		pass.Clarity = (clarity && iteration < CLARITY_BLUR_PASSES) ? 1.0f : 0.0f;
		// Each pair steps a texel wider than the last.  What the earlier pairs left is smooth
		// enough by then that the taps landing between texels miss nothing, and the widths add
		// up in squares, which is how four pairs reach twice as far as two.
		const float spread = static_cast<float>(iteration + 1);
		for (unsigned axis = 0; axis < 2; ++axis) {
			const unsigned other = 1 - source;
			pass.Source = BloomTargets[source].Resource;
			pass.Destination = BloomTargets[other].View;
			pass.BlurX = (axis == 0) ? bloom_texel_x * spread : 0.0f;
			pass.BlurY = (axis == 0) ? 0.0f : bloom_texel_y * spread;
			Draw_Pass(pass);
			source = other;
		}
	}

	pass.Shader = InternalHdr ? BloomCompositeWideShader : BloomCompositeShader;
	pass.Source = scene;
	pass.Extra = BloomTargets[source].Resource;
	pass.Destination = destination;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	pass.BlurX = 0.0f;
	pass.BlurY = 0.0f;
	pass.Clarity = ClarityStrength;
	Draw_Pass(pass);

	return ChainTargets[0].Resource;
}

bool DX11PostProcessClass::Finish(const DX11PostEffect * effects, unsigned count)
{
	if (Resolved || count == 0 || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	// A blast on screen puts one more pass on the end of the chain, so whatever was last writes a
	// chain target instead of the screen and the warp writes the screen from it.
	const bool warp = (WarpCount > 0);

	unsigned first = 0;
	ID3D11ShaderResourceView * const scene = Scene_Source();
	ID3D11ShaderResourceView * source = scene;
	if (effects[0] == DX11_POST_BLOOM) {
		// Bloom is the only effect that reads the half float scene, and the frame is eight bits by
		// the time it hands over. If it is also the last thing in the chain it writes the screen.
		const bool alone = (count == 1) && !warp;
		ID3D11RenderTargetView * bloom_destination = alone ? back_buffer : ChainTargets[0].View;
		if (BloomIntensity > 0.0f || ClarityStrength > 0.0f) {
			// The clarity's blur rides the bloom's targets, so with the glow off and the clarity
			// on this still runs, with nothing in the glow.
			source = Run_Bloom(bloom_destination, scene);
		} else {
			// Bloom switched off in the options: no glow, but the scene is still half floats and
			// still has to come down to eight bits, the same way Copy_Through brings it down.  The
			// chain keeps its shape so turning it back on costs no rebuild of the targets.
			PassSetup tone;
			memset(&tone, 0, sizeof(tone));
			tone.Shader = InternalHdr ? ToneMapWideShader : ToneMapShader;
			tone.Source = scene;
			tone.Destination = bloom_destination;
			tone.DestinationWidth = Width;
			tone.DestinationHeight = Height;
			tone.SourceWidth = Width;
			tone.SourceHeight = Height;
			Draw_Pass(tone);
			source = ChainTargets[0].Resource;
		}
		if (alone) {
			Resolved = true;
			return true;
		}
		first = 1;
	}

	// Each remaining pass reads one eight bit target and writes the other, and the last one writes
	// the swap chain instead, so an odd or an even chain both end up on the screen.
	unsigned destination = (first == 0) ? 0u : 1u;
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	for (unsigned index = first; index < count; ++index) {
		const bool last = (index + 1 == count) && !warp;
		pass.Shader = (effects[index] == DX11_POST_SHARPEN && TargetSamples > 1)
			? SharpenMultisampledShader : PixelShaders[effects[index]];
		// The occlusion pass is the one effect that reads something other than the frame: the
		// frame's own depth, which the device keeps as a texture as well as a depth buffer.
		pass.Extra = (effects[index] == DX11_POST_AO) ? Device->Get_Depth_Texture() : NULL;
		pass.Occlusion = (effects[index] == DX11_POST_AO);
		pass.Source = source;
		pass.Destination = last ? back_buffer : ChainTargets[destination].View;
		Draw_Pass(pass);
		source = ChainTargets[destination].Resource;
		destination = 1 - destination;
	}

	if (warp) {
		pass.Shader = WarpShader;
		pass.Extra = NULL;
		pass.Occlusion = false;
		pass.Source = source;
		pass.Destination = back_buffer;
		Draw_Pass(pass);
	}

	Resolved = true;
	return true;
}

bool DX11PostProcessClass::Run_Chain()
{
	// The scene took its occlusion before the particles, and a multisampled one has no use for
	// FXAA; a chain of nothing else, or none at all over a multisampled scene, still has to bring
	// the scene to the screen.  Without either the chain runs as it was set, and an empty one with
	// it, since an empty chain at one sample has no scene target to bring.
	const bool multisampled = Scene_Samples() > 1;
	DX11PostEffect effects[DX11_POST_CHAIN_LIMIT];
	unsigned count = 0;
	for (unsigned index = 0; index < ChainLength; ++index) {
		if ((Occluded && Chain[index] == DX11_POST_AO) || (multisampled && Chain[index] == DX11_POST_FXAA)) {
			continue;
		}
		effects[count++] = Chain[index];
	}
	if (count == 0) {
		effects[count++] = DX11_POST_COPY;
	}
	return Finish(effects, count);
}

bool DX11PostProcessClass::Copy_Through()
{
	if (Resolved || !Ensure_Buffers()) {
		return false;
	}

	ID3D11RenderTargetView * back_buffer = Device->Get_Back_Buffer_View();
	if (back_buffer == NULL) {
		return false;
	}

	// The tone curve rather than the copy when the scene is half float, because the swap chain
	// cannot hold what the scene target holds and a raw write would clamp it. In eight bits the
	// copy is exact, which is what the plumbing test measures.
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = TargetsAreFloat ? ToneMapShader : PixelShaders[DX11_POST_COPY];
	pass.Ungraded = true;
	pass.Source = Scene_Source();
	pass.Destination = back_buffer;
	pass.DestinationWidth = Width;
	pass.DestinationHeight = Height;
	pass.SourceWidth = Width;
	pass.SourceHeight = Height;
	Draw_Pass(pass);

	Resolved = true;
	return true;
}

void DX11PostProcessClass::Release_Backdrop()
{
	Release_Target(BackdropCopy);
	Release_Target(BackdropHalf);
	Release_Target(BackdropBlur[0]);
	Release_Target(BackdropBlur[1]);
	BackdropWidth = 0;
	BackdropHeight = 0;
	BackdropBlurred = false;
}

static unsigned half_of(unsigned size)
{
	return (size / 2) > 0 ? (size / 2) : 1;
}

ID3D11Resource * DX11PostProcessClass::Backdrop_Back()
{
	ID3D11RenderTargetView * back_buffer = (Device != NULL) ? Device->Get_Back_Buffer_View() : NULL;
	if (!ShadersReady || BackdropRefused || BackdropShader == NULL || back_buffer == NULL) {
		return NULL;
	}

	ID3D11Resource * back = NULL;
	back_buffer->GetResource(&back);
	if (back == NULL) {
		return NULL;
	}
	D3D11_TEXTURE2D_DESC description;
	static_cast<ID3D11Texture2D *>(back)->GetDesc(&description);
	const unsigned width = description.Width;
	const unsigned height = description.Height;

	// The copy has to be the back buffer's own format for CopyResource; a resize makes all four the
	// wrong size.
	bool fits = BackdropCopy.Texture != NULL && BackdropWidth == width && BackdropHeight == height;
	if (fits) {
		D3D11_TEXTURE2D_DESC held;
		BackdropCopy.Texture->GetDesc(&held);
		fits = held.Format == description.Format;
	}
	if (!fits) {
		Release_Backdrop();
		if (!Create_Target(BackdropCopy, width, height, description.Format)
				|| !Create_Target(BackdropHalf, half_of(width), half_of(height), EIGHT_BIT_FORMAT)
				|| !Create_Target(BackdropBlur[0], half_of(half_of(width)), half_of(half_of(height)), EIGHT_BIT_FORMAT)
				|| !Create_Target(BackdropBlur[1], half_of(half_of(width)), half_of(half_of(height)), EIGHT_BIT_FORMAT)) {
			Release_Backdrop();
			back->Release();
			return NULL;
		}
		BackdropWidth = width;
		BackdropHeight = height;
	}
	return back;
}

bool DX11PostProcessClass::Backdrop_Ready()
{
	ID3D11Resource * back = Backdrop_Back();
	if (back == NULL) {
		return false;
	}
	back->Release();
	return true;
}

bool DX11PostProcessClass::Blur_Backdrop(float radius)
{
	if (BackdropBlurred) {
		return true;
	}
	ID3D11Resource * back = Backdrop_Back();
	if (back == NULL) {
		return false;
	}
	const unsigned width = BackdropWidth;
	const unsigned height = BackdropHeight;
	const unsigned half_width = half_of(width);
	const unsigned half_height = half_of(height);
	const unsigned quarter_width = half_of(half_width);
	const unsigned quarter_height = half_of(half_height);
	Device->Get_Context()->CopyResource(BackdropCopy.Texture, back);
	back->Release();

	// Two halvings, each a bilinear read on the corner four texels share, which is their average: a
	// four by four box from the full size to the quarter, where one read would skip twelve of sixteen.
	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = PixelShaders[DX11_POST_COPY];
	pass.Source = BackdropCopy.Resource;
	pass.Destination = BackdropHalf.View;
	pass.DestinationWidth = half_width;
	pass.DestinationHeight = half_height;
	pass.SourceWidth = width;
	pass.SourceHeight = height;
	Draw_Pass(pass);
	pass.Source = BackdropHalf.Resource;
	pass.Destination = BackdropBlur[0].View;
	pass.DestinationWidth = quarter_width;
	pass.DestinationHeight = quarter_height;
	pass.SourceWidth = half_width;
	pass.SourceHeight = half_height;
	Draw_Pass(pass);

	// The spread asked for in quarter size texels, made of pairs of equal steps no wider than
	// BACKDROP_STEP_MOST: the spreads of the pairs add in squares.
	const float spread = (radius > 0.0f ? radius : 0.0f) * static_cast<float>(quarter_height) / 1080.0f;
	const float steps = spread / (BACKDROP_PASS_SPREAD * BACKDROP_STEP_MOST);
	unsigned pairs = static_cast<unsigned>(ceilf(steps * steps));
	pairs = (pairs < 1) ? 1 : (pairs > BACKDROP_PAIRS_MOST) ? BACKDROP_PAIRS_MOST : pairs;
	const float step = spread / (BACKDROP_PASS_SPREAD * sqrtf(static_cast<float>(pairs)));

	pass.Shader = BloomBlurShader;
	pass.SourceWidth = quarter_width;
	pass.SourceHeight = quarter_height;
	unsigned source = 0;
	for (unsigned pair = 0; pair < pairs; ++pair) {
		for (unsigned axis = 0; axis < 2; ++axis) {
			const unsigned other = 1 - source;
			pass.Source = BackdropBlur[source].Resource;
			pass.Destination = BackdropBlur[other].View;
			pass.BlurX = (axis == 0) ? step / static_cast<float>(quarter_width) : 0.0f;
			pass.BlurY = (axis == 0) ? 0.0f : step / static_cast<float>(quarter_height);
			Draw_Pass(pass);
			source = other;
		}
	}
	BackdropSource = source;
	BackdropBlurred = true;
	return true;
}

bool DX11PostProcessClass::Draw_Backdrop(unsigned x0, unsigned y0, unsigned x1, unsigned y1, float radius)
{
	if (!Blur_Backdrop(radius)) {
		return false;
	}
	if (x1 > BackdropWidth) {
		x1 = BackdropWidth;
	}
	if (y1 > BackdropHeight) {
		y1 = BackdropHeight;
	}
	if (x0 >= x1 || y0 >= y1) {
		return false;
	}

	PassSetup pass;
	memset(&pass, 0, sizeof(pass));
	pass.Shader = BackdropShader;
	pass.Source = BackdropBlur[BackdropSource].Resource;
	pass.Destination = Device->Get_Back_Buffer_View();
	pass.DestinationX = x0;
	pass.DestinationY = y0;
	pass.DestinationWidth = x1 - x0;
	pass.DestinationHeight = y1 - y0;
	pass.SourceWidth = BackdropWidth;
	pass.SourceHeight = BackdropHeight;
	Draw_Pass(pass);
	return true;
}

void DX11PostProcessClass::Describe()
{
	if (!ShadersReady) {
		return;
	}
	const unsigned samples = Scene_Samples();
	if (ChainLength == 0 && samples == 1) {
		strcpy(Diagnostic, "post-process off");
		return;
	}

	std::string names = (ChainLength == 0) ? "copy" : "";
	for (unsigned index = 0; index < ChainLength; ++index) {
		if (index > 0) {
			names += ",";
		}
		names += DX11Post_Effect_Name(Chain[index]);
	}
	char multisampling[64] = "";
	if (samples > 1) {
		snprintf(multisampling, sizeof(multisampling), ", %ux multisampled without fxaa, sharpen softened",
			samples);
	}
	snprintf(Diagnostic, sizeof(Diagnostic), "post-process %s over %ux%u, scene in %s%s", names.c_str(),
		Width, Height, TargetsAreFloat ? "half floats" : "eight bits", multisampling);
}
