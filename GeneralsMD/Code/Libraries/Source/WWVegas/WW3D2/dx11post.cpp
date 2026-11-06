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

#include "dx11device.h"
#include "dx11resource.h"
#include "dx11runtime.h"

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

void DX11Post_Set_Bloom(float threshold, float intensity, float additive_gain, unsigned blur_passes)
{
	BloomThreshold = (threshold > 0.0f) ? threshold : 0.0f;
	BloomIntensity = (intensity > 0.0f) ? intensity : 0.0f;
	BloomAdditiveGain = (additive_gain > 1.0f) ? additive_gain : 1.0f;
	BloomBlurPasses = (blur_passes < 1) ? 1
		: (blur_passes > BLOOM_BLUR_PASS_LIMIT) ? BLOOM_BLUR_PASS_LIMIT : blur_passes;
}

float DX11Post_Additive_Gain()
{
	return (BloomIntensity > 0.0f) ? BloomAdditiveGain : 1.0f;
}

// How far the grade bends the picture at full strength: the power the S raises each side of its
// pivot to, less one, what is left of the colour around its grey, and how far a channel moves warm
// or cool for each step away from the pivot.
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
static float AtmosphereMapSize[2] = { 0.0f, 0.0f };
static ID3D11ShaderResourceView * AtmosphereSky = NULL;

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

	// The step a separable blur takes, already in texture coordinates.
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
};

// A triangle big enough to cover the screen, made out of nothing.  No vertex buffer and no input
// layout: SV_VertexID walks 0, 1, 2 and the arithmetic turns them into (0,0), (2,0), (0,2) in
// texture coordinates, whose triangle covers the whole of the unit square.
static const char * const VERTEX_SHADER_SOURCE =
	"struct VertexOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Texture : TEXCOORD0;\n"
	"};\n"
	"\n"
	"VertexOutput main(uint index : SV_VertexID)\n"
	"{\n"
	"    VertexOutput output;\n"
	"    output.Texture = float2((index << 1) & 2, index & 2);\n"
	"    output.Position = float4(output.Texture.x * 2.0 - 1.0,\n"
	"        1.0 - output.Texture.y * 2.0, 0.0, 1.0);\n"
	"    return output;\n"
	"}\n";

static const char * const PIXEL_SHADER_PROLOGUE =
	"struct VertexOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Texture : TEXCOORD0;\n"
	"};\n"
	"\n"
	"Texture2D Source : register(t0);\n"
	"Texture2D Extra : register(t1);\n"
	"SamplerState Sampler : register(s0);\n"
	"\n"
	"cbuffer PostConstants : register(b0)\n"
	"{\n"
	"    float4 TexelSize;\n"
	"    float4 BlurDirection;\n"
	"    float4 Tuning;\n"
	"    float4 WarpInfo;\n"
	"    float4 WarpCentre[4];\n"
	"    float4 WarpRing[4];\n"
	"    float4 Grade;\n"
	"};\n"
	"\n"
	// Below the knee nothing moves, so a frame with nothing overbright in it comes back out as the
	// frame that went in and the tone curve costs the picture nothing.  From the knee upward every
	// value between it and infinity is folded into the gap between the knee and white, which is
	// what an eight bit buffer could not do: there, everything over white was white.
	"float3 tone_curve(float3 colour)\n"
	"{\n"
	"    float knee = Tuning.z;\n"
	"    float headroom = max(1.0 - knee, 0.0001);\n"
	"    float3 excess = max(colour - knee, 0.0);\n"
	"    return min(colour, knee) + headroom * (1.0 - exp(-excess / headroom));\n"
	"}\n"
	"\n"
	// The lighting style's grade, on the curve's output, so everything it sees is already between
	// nought and one.  The S turns about the grey a battlefield frame averages (under a third of
	// white, not a half, or the whole picture goes darker; at a third a forest and a snowed-in town
	// still lost an eighth of their brightness): black, white and that grey stay where they
	// are, what is below it sinks and what is above it rises.  Then the colour goes toward its own
	// grey, and a pixel brighter than the pivot leans to red, a darker one to blue, by how far it is
	// from it.
	"static const float GRADE_PIVOT = 0.28;\n"
	"float3 grade(float3 colour)\n"
	"{\n"
	"    if (Grade.x <= 0.0) return colour;\n"
	"    float3 c = saturate(colour);\n"
	"    float k = 1.0 + Grade.y;\n"
	"    float3 low = GRADE_PIVOT * pow(max(c, 0.00001) / GRADE_PIVOT, k);\n"
	"    float3 high = 1.0 - (1.0 - GRADE_PIVOT) * pow(max(1.0 - c, 0.00001) / (1.0 - GRADE_PIVOT), k);\n"
	"    float3 graded = (c < GRADE_PIVOT) ? low : high;\n"
	"    float grey = dot(graded, float3(0.299, 0.587, 0.114));\n"
	"    graded = lerp(grey.xxx, graded, Grade.z);\n"
	"    graded += float3(1.0, 0.25, -1.0) * (grey - GRADE_PIVOT) * Grade.w;\n"
	"    return lerp(c, saturate(graded), Grade.x);\n"
	"}\n"
	"\n";

static const char * const COPY_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return Source.Sample(Sampler, input.Texture);\n"
	"}\n";

static const char * const TONE_MAP_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return float4(grade(tone_curve(Source.Sample(Sampler, input.Texture).rgb)), 1.0);\n"
	"}\n";

// Internal HDR Tone Mapping.  tone_curve's room runs out fast, a channel at 1.3 is within two percent
// of white, so a fire whose channels all pass that is one flat white however hot it is.  Here a pixel
// past white first has its whole colour scaled back, so that its brightest channel sits at one plus a
// share of the doublings it is past white, and then takes tone_curve as any other pixel does.  The
// ratio of its channels goes through unchanged, each doubling is a little more channel past the knee,
// so orange climbs through yellow toward white across five or six doublings instead of half of one.
// A pixel with no channel past white is what tone_curve made it, which away from a fire is the ground,
// the walls and nearly all the rest of the art.  Team colour stripes do pass white, and so does what a
// glow or the Firelight option lights past it, so those come out more coloured than they did.
//
// Text of its own and not in the prologue: the haze's and the occlusion's programs are keyed in the
// shader cache by a source that takes the prologue in, and this leaves them as they were.
#define WIDE_CURVE_TEXT \
	"static const float WIDE_RISE = 0.3;\n" \
	"float3 wide_curve(float3 colour)\n" \
	"{\n" \
	"    float peak = max(colour.r, max(colour.g, colour.b));\n" \
	"    if (peak > 1.0) colour *= (1.0 + WIDE_RISE * log2(peak)) / peak;\n" \
	"    return tone_curve(colour);\n" \
	"}\n" \
	"\n"

static const char * const TONE_MAP_WIDE_SHADER_BODY =
	WIDE_CURVE_TEXT
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    return float4(grade(wide_curve(Source.Sample(Sampler, input.Texture).rgb)), 1.0);\n"
	"}\n";

// FXAA, the 2009 shape of it: read the luminance of the four diagonal neighbours and the centre,
// leave the pixel alone unless the spread between them says an edge runs through it, work out which
// way the edge lies from which pair of corners is brighter, and average along it.  Two samples give
// the cheap answer and four the better one; if the four sample answer has strayed outside the
// luminance range the neighbourhood had, it has blurred across something and the two sample answer
// is taken instead.
//
// It runs on the finished world with no depth and no motion vectors, which is why it can sit here
// at all.  What it cannot do is recover an edge the frame never had: a thin wire drawn into one
// pixel column stays one pixel wide.
static const char * const FXAA_SHADER_BODY =
	"static const float3 LUMINANCE_WEIGHTS = float3(0.299, 0.587, 0.114);\n"
	"static const float EDGE_THRESHOLD = 0.125;\n"
	"static const float EDGE_THRESHOLD_MINIMUM = 0.0416;\n"
	"static const float SPAN_MAXIMUM = 8.0;\n"
	"static const float REDUCE_SCALE = 0.125;\n"
	"static const float REDUCE_MINIMUM = 0.0078125;\n"
	"\n"
	"float luminance(float3 colour)\n"
	"{\n"
	"    return dot(colour, LUMINANCE_WEIGHTS);\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 texel = TexelSize.xy;\n"
	"    float3 centre = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float3 northWest = Source.Sample(Sampler, input.Texture + float2(-texel.x, -texel.y)).rgb;\n"
	"    float3 northEast = Source.Sample(Sampler, input.Texture + float2(texel.x, -texel.y)).rgb;\n"
	"    float3 southWest = Source.Sample(Sampler, input.Texture + float2(-texel.x, texel.y)).rgb;\n"
	"    float3 southEast = Source.Sample(Sampler, input.Texture + float2(texel.x, texel.y)).rgb;\n"
	"\n"
	"    float centreLuminance = luminance(centre);\n"
	"    float northWestLuminance = luminance(northWest);\n"
	"    float northEastLuminance = luminance(northEast);\n"
	"    float southWestLuminance = luminance(southWest);\n"
	"    float southEastLuminance = luminance(southEast);\n"
	"\n"
	"    float lowest = min(centreLuminance, min(min(northWestLuminance, northEastLuminance),\n"
	"        min(southWestLuminance, southEastLuminance)));\n"
	"    float highest = max(centreLuminance, max(max(northWestLuminance, northEastLuminance),\n"
	"        max(southWestLuminance, southEastLuminance)));\n"
	"    float spread = highest - lowest;\n"
	"    if (spread < max(EDGE_THRESHOLD_MINIMUM, highest * EDGE_THRESHOLD)) {\n"
	"        return float4(centre, 1.0);\n"
	"    }\n"
	"\n"
	"    float2 direction;\n"
	"    direction.x = -((northWestLuminance + northEastLuminance)\n"
	"        - (southWestLuminance + southEastLuminance));\n"
	"    direction.y = (northWestLuminance + southWestLuminance)\n"
	"        - (northEastLuminance + southEastLuminance);\n"
	"\n"
	"    float reduction = max((northWestLuminance + northEastLuminance + southWestLuminance\n"
	"        + southEastLuminance) * 0.25 * REDUCE_SCALE, REDUCE_MINIMUM);\n"
	"    float shortest = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduction);\n"
	"    direction = clamp(direction * shortest, -SPAN_MAXIMUM, SPAN_MAXIMUM) * texel;\n"
	"\n"
	"    float3 inner = 0.5 * (Source.Sample(Sampler, input.Texture + direction * (1.0 / 3.0 - 0.5)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + direction * (2.0 / 3.0 - 0.5)).rgb);\n"
	"    float3 outer = inner * 0.5\n"
	"        + 0.25 * (Source.Sample(Sampler, input.Texture + direction * -0.5).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + direction * 0.5).rgb);\n"
	"\n"
	"    float outerLuminance = luminance(outer);\n"
	"    float3 result = (outerLuminance < lowest || outerLuminance > highest) ? inner : outer;\n"
	"    return float4(result, 1.0);\n"
	"}\n";

// Unsharp mask: the pixel plus what it has that a blur of its neighbours does not.  The amount is
// low on purpose.  This runs over the world and the world alone, but the world holds thin wires and
// aerials as well as ground, and anything stronger rings around them.
//
// A multisampled scene has no FXAA before this, and FXAA was what softened the ground's texture
// grain at one sample: sharpened at 0.35 the 4x ground measured half as grainy again as -msaa 0
// (mean absolute Laplacian 36.3 against 24.1 on one crop, 58.5 against 39.9 on another).  There the
// same pass runs at -0.25, a light pull toward the neighbours, which brought five ground crops to
// within 10% of -msaa 0.
#define SHARPEN_SHADER_TEXT(amount) \
	"static const float SHARPEN_AMOUNT = " amount ";\n" \
	"\n" \
	"float4 main(VertexOutput input) : SV_TARGET\n" \
	"{\n" \
	"    float2 texel = TexelSize.xy;\n" \
	"    float4 centre = Source.Sample(Sampler, input.Texture);\n" \
	"    float3 left = Source.Sample(Sampler, input.Texture + float2(-texel.x, 0.0)).rgb;\n" \
	"    float3 right = Source.Sample(Sampler, input.Texture + float2(texel.x, 0.0)).rgb;\n" \
	"    float3 up = Source.Sample(Sampler, input.Texture + float2(0.0, -texel.y)).rgb;\n" \
	"    float3 down = Source.Sample(Sampler, input.Texture + float2(0.0, texel.y)).rgb;\n" \
	"    float3 neighbours = left + right + up + down;\n" \
	"    float3 sharpened = centre.rgb + (centre.rgb - neighbours * 0.25) * SHARPEN_AMOUNT;\n" \
	"    // never past the brightest or the darkest of the five: overshooting them drew a white rim\n" \
	"    // around every dark wreck inside a nuke's white cloud\n" \
	"    float3 lowest = min(centre.rgb, min(min(left, right), min(up, down)));\n" \
	"    float3 highest = max(centre.rgb, max(max(left, right), max(up, down)));\n" \
	"    return float4(clamp(sharpened, lowest, highest), centre.a);\n" \
	"}\n"
static const char * const SHARPEN_SHADER_BODY = SHARPEN_SHADER_TEXT("0.35");
static const char * const SHARPEN_MULTISAMPLED_SHADER_BODY = SHARPEN_SHADER_TEXT("-0.25");
#undef SHARPEN_SHADER_TEXT

// Ambient occlusion out of the frame's own depth, and nothing else.  Every pixel asks a ring of
// neighbours how much nearer to the eye they are: at the foot of a wall half of them are in front
// and the pixel comes out dark, on open ground none are and it comes out as it was.  There are no
// normals here and no random kernel - the ring is fixed and turned by the pixel's place on the
// screen, which is what keeps twelve taps from banding.
//
// It cannot see behind a surface, so a neighbour nearer by more than the radius is ignored: that is
// a different object in front, not a corner.
#define AO_OCCLUSION_FUNCTIONS \
	"static const int AO_TAPS = 12;\n" \
	"static const float AO_STRENGTH = 0.45;\n" \
	"static const float AO_PI2 = 6.2831853;\n" \
	"\n" \
	"float linear_depth(float2 at)\n" \
	"{\n" \
	"    float raw = Extra.Sample(Sampler, at).r;\n" \
	"    float near_plane = Tuning.x;\n" \
	"    float far_plane = Tuning.y;\n" \
	"    return near_plane * far_plane / max(far_plane - raw * (far_plane - near_plane), 0.0001);\n" \
	"}\n" \
	"\n" \
	"float occlusion_at(VertexOutput input)\n" \
	"{\n" \
	"    float centre = linear_depth(input.Texture);\n" \
	"    float radius = Tuning.z;\n" \
	"    if (centre >= Tuning.y * 0.99) return 0.0;\n" \
	"    float turn = frac(sin(dot(input.Position.xy, float2(12.9898, 78.233))) * 43758.5453) * AO_PI2;\n" \
	"    float spread = TexelSize.x * Tuning.w / max(centre, 1.0);\n" \
	"    float occlusion = 0.0;\n" \
	"    for (int tap = 0; tap < AO_TAPS; ++tap)\n" \
	"    {\n" \
	"        float angle = turn + AO_PI2 * (float)tap / (float)AO_TAPS;\n" \
	"        float reach = 0.35 + 0.65 * frac((float)tap * 0.618);\n" \
	"        float2 at = input.Texture + float2(cos(angle), sin(angle)) * spread * reach;\n" \
	"        float neighbour = linear_depth(at);\n" \
	"        float nearer = centre - neighbour;\n" \
	"        occlusion += (nearer > 0.05 && nearer < radius) ? (1.0 - nearer / radius) : 0.0;\n" \
	"    }\n" \
	"    return saturate(occlusion / (float)AO_TAPS);\n" \
	"}\n" \
	"\n"

// The pass over the finished frame, for a frame whose scene never reached Occlude_Scene (a screen
// filter held the target).  Fire, glow and a nuke's cloud are in the frame by now and write no
// depth, so the depth under them is the ground and the wrecks behind them; darkened by it a white
// cloud showed every tree and wreck under it as a grey line drawing.  Keyed on the dimmest channel:
// a white cloud has all three high, sunlit sand and concrete keep a low blue and keep their corners.
// An orange fireball keeps a low blue too, which is why the occlusion normally runs before the
// particles are drawn at all.
static const char * const AO_SHADER_BODY =
	AO_OCCLUSION_FUNCTIONS
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float bright = min(scene.r, min(scene.g, scene.b));\n"
	"    float occlusion = occlusion_at(input) * (1.0 - smoothstep(0.7, 0.95, bright));\n"
	"    return float4(scene * (1.0 - occlusion * AO_STRENGTH), 1.0);\n"
	"}\n";

// The same occlusion as a factor the scene target is multiplied by, mid-scene, after the opaque
// world and before the particles (Occlude_Scene), so no particle takes a corner's shade.  The glow
// cards, muzzle flashes and fading units drawn before it write no depth either, so the key on the
// dimmest channel stays, read from a copy of the scene.
static const char * const AO_SCENE_SHADER_BODY =
	AO_OCCLUSION_FUNCTIONS
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float bright = min(scene.r, min(scene.g, scene.b));\n"
	"    float occlusion = occlusion_at(input) * (1.0 - smoothstep(0.7, 0.95, bright));\n"
	"    return float4((1.0 - occlusion * AO_STRENGTH).xxx, 1.0);\n"
	"}\n";

// The Ambient Occlusion option, in three programs compiled on first use.  The depth goes back to the
// world, so the reach is in world units and a corner reads the same at every zoom: a tank's skirt is
// dark to the same few units of ground whether it fills the frame or is thirty pixels long.  A tap
// that stands higher over the pixel's own surface than the bias closes off sky, weighted by how
// steeply and fading out at the radius, so open ground and a flat roof come out untouched and a
// tall building behind a unit's feet casts nothing on them.  The surface comes from the depth
// itself, from whichever neighbour on each axis lies nearer, or a pixel on a silhouette would take
// the slope between the two objects for its own.
//
// All of it runs at half size.  Each half size pixel reads one full size depth texel, and it keeps
// the distance along the eye's axis beside its occlusion so the blur and the upsample need not
// read depth again per tap.  Depth is Load, never Sample: filtered depth across a silhouette is a
// surface nobody drew.
#define WORLD_AO_CONSTANTS \
	"cbuffer WorldAoConstants : register(b1)\n" \
	"{\n" \
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n" \
	"    float4 HazeSun : packoffset(c19);\n" \
	"    float4 HazeShroudRect : packoffset(c20);\n" \
	"    float4 WorldAoProjection : packoffset(c21);\n" \
	"};\n" \
	"Texture2D WorldAoShroud : register(t2);\n" \
	"\n" \
	"float eye_distance(float raw)\n" \
	"{\n" \
	"    return 1.0 / (raw * WorldFromClip[2][3] + WorldFromClip[3][3]);\n" \
	"}\n" \
	"\n"

static const char * const WORLD_AO_SHADER_BODY =
	WORLD_AO_CONSTANTS
	"static const int AO_TAPS = 14;\n"
	"static const float AO_RADIUS = 22.0;\n"
	"static const float AO_MOST_PIXELS = 120.0;\n"
	"static const float AO_BIAS = 0.12;\n"
	"static const float AO_STRENGTH = 2.8;\n"
	"\n"
	"float3 world_at(int2 texel, float raw)\n"
	"{\n"
	"    float2 ndc = float2((texel.x + 0.5) * TexelSize.x * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * TexelSize.y * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n"
	"    return world.xyz / world.w;\n"
	"}\n"
	"\n"
	// Past the frame's edge Load reads 0, the near plane, a long way off, so the slope never takes it.
	"float3 world_of(int2 texel)\n"
	"{\n"
	"    return world_at(texel, Extra.Load(int3(texel, 0)).r);\n"
	"}\n"
	"\n"
	"float3 shorter(float3 a, float3 b)\n"
	"{\n"
	"    return dot(a, a) < dot(b, b) ? a : b;\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = min(int2(input.Position.xy) * 2 + 1, int2(TexelSize.zw) - 1);\n"
	"    float raw = Extra.Load(int3(texel, 0)).r;\n"
	"    float distance_along = eye_distance(raw);\n"
	"    if (raw >= 0.99999) return float4(1.0, distance_along, 0.0, 0.0);\n"
	"    float3 here = world_at(texel, raw);\n"
	"    float3 across = shorter(here - world_of(texel - int2(1, 0)), world_of(texel + int2(1, 0)) - here);\n"
	"    float3 down = shorter(here - world_of(texel - int2(0, 1)), world_of(texel + int2(0, 1)) - here);\n"
	"    float3 facing = normalize(cross(across, down));\n"
	"    if (dot(facing, world_at(texel, 0.0) - here) < 0.0) facing = -facing;\n"
	"\n"
	"    float2 reach = AO_RADIUS * 0.5 * WorldAoProjection.xy * TexelSize.zw / distance_along;\n"
	"    reach *= min(1.0, AO_MOST_PIXELS / reach.y);\n"
	"    float turn = frac(52.9829189 * frac(dot(input.Position.xy, float2(0.06711056, 0.00583715)))) * 6.2831853;\n"
	"    float closed = 0.0;\n"
	"    for (int tap = 0; tap < AO_TAPS; ++tap)\n"
	"    {\n"
	"        float angle = turn + 2.39996 * (float)tap;\n"
	"        float along = ((float)tap + 0.5) / (float)AO_TAPS;\n"
	// A tap off the frame knows nothing and closes nothing.  Given the edge's depth instead, a tap above
	// the frame stood over ground that recedes that way, and the whole top of the frame went dark.
	"        int2 at = texel + int2(float2(cos(angle), sin(angle)) * reach * along);\n"
	"        if (any(at < 0) || any(at >= int2(TexelSize.zw))) continue;\n"
	"        float3 to = world_of(at) - here;\n"
	"        float squared = dot(to, to);\n"
	"        float fade = saturate(1.0 - squared / (AO_RADIUS * AO_RADIUS));\n"
	"        closed += fade * fade * saturate(dot(to, facing) * rsqrt(squared + 0.01) - AO_BIAS);\n"
	"    }\n"
	"    float kept = saturate(1.0 - AO_STRENGTH * closed / (float)AO_TAPS);\n"
	// The fog the terrain draws, as the haze reads it: under the shroud nothing, in the fog little.
	"    float lit = lerp(1.0, WorldAoShroud.SampleLevel(Sampler, (here.xy - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).g, HazeSun.z);\n"
	"    return float4(lerp(1.0, kept, lit * lit * lit), distance_along, 0.0, 0.0);\n"
	"}\n";

// One axis of the blur that takes the tap pattern's grain off, stopping at a change of distance so a
// tank's dark skirt does not bleed over the roof of the tank.  What counts as a change is twenty half
// size pixels' width on the ground, which is the same ground at every zoom and on the isometric
// camera, whose eye stands thousands of units back with a narrow lens: there a tolerance taken as a
// share of the distance let the skirt straight over the hull.
static const char * const WORLD_AO_BLUR_SHADER_BODY =
	WORLD_AO_CONSTANTS
	"static const float WEIGHTS[5] = { 0.2, 0.18, 0.14, 0.09, 0.05 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    int2 texel = int2(input.Position.xy);\n"
	"    int2 step = int2(BlurDirection.xy);\n"
	"    float2 centre = Source.Load(int3(texel, 0)).rg;\n"
	"    float allowed = 40.0 * centre.g / (WorldAoProjection.y * TexelSize.w);\n"
	"    float sum = centre.r * WEIGHTS[0];\n"
	"    float total = WEIGHTS[0];\n"
	"    for (int index = 1; index < 5; ++index)\n"
	"    {\n"
	"        float2 a = Source.Load(int3(clamp(texel + step * index, int2(0, 0), int2(TexelSize.zw) - 1), 0)).rg;\n"
	"        float2 b = Source.Load(int3(clamp(texel - step * index, int2(0, 0), int2(TexelSize.zw) - 1), 0)).rg;\n"
	"        float weightA = WEIGHTS[index] * saturate(1.0 - abs(a.g - centre.g) / allowed);\n"
	"        float weightB = WEIGHTS[index] * saturate(1.0 - abs(b.g - centre.g) / allowed);\n"
	"        sum += a.r * weightA + b.r * weightB;\n"
	"        total += weightA + weightB;\n"
	"    }\n"
	"    return float4(sum / total, centre.g, 0.0, 0.0);\n"
	"}\n";

// Back to full size as a factor the scene is multiplied by.  The four half size pixels round this
// one weigh by nearness as a bilinear filter would and by how close their distance is to this
// pixel's, with a floor: a one pixel antenna that matches none of them still gets the filter's
// answer rather than a division by nothing.  A half size pixel holds the depth of full size texel
// 2i+1, so its centre is at 2i+1.5, which is the 0.75 in the position.
static const char * const WORLD_AO_UPSAMPLE_SHADER_BODY =
	WORLD_AO_CONSTANTS
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float wanted = eye_distance(Extra.Load(int3(int2(input.Position.xy), 0)).r);\n"
	"    float allowed = 20.0 * wanted / (WorldAoProjection.y * TexelSize.w);\n"
	"    float2 at = input.Position.xy * 0.5 - 0.75;\n"
	"    int2 corner = max(int2(floor(at)), int2(0, 0));\n"
	"    float2 part = saturate(at - corner);\n"
	"    float sum = 0.0;\n"
	"    float total = 0.0;\n"
	"    for (int y = 0; y < 2; ++y)\n"
	"    {\n"
	"        for (int x = 0; x < 2; ++x)\n"
	"        {\n"
	"            float2 tap = Source.Load(int3(corner + int2(x, y), 0)).rg;\n"
	"            float bilinear = (x == 1 ? part.x : 1.0 - part.x) * (y == 1 ? part.y : 1.0 - part.y);\n"
	"            float weight = bilinear * (0.001 + saturate(1.0 - abs(tap.g - wanted) / allowed));\n"
	"            sum += tap.r * weight;\n"
	"            total += weight;\n"
	"        }\n"
	"    }\n"
	"    return float4((sum / total).xxx, 1.0);\n"
	"}\n";

// The prototype ground haze, blended over the scene target by its alpha mid-scene, at the moment
// the occlusion goes on.  The pixel's depth goes back to the world the way the headlights take it
// (HEADLIGHT_SAMPLING in ffshader.h); how far it lies below the haze's top decides how much of the
// haze it takes, eased in so the layer has no edge, and a pixel further from the eye takes up to
// half again more, which is what keeps the near hill sides clear of a flat wash.  Nothing drawn
// (depth still at the clear) takes none, and neither does anything under the water's surface
// (HazeColour.w): water writes no depth, so what the pass sees there is the bed, and hazed by its
// depth the water went flat grey.  The haze eases out smoothly from three units above the water to
// two below it.  Cut off within two units above, the pale hazed sand met the dark water as a hard
// line; ending eight units above, the maps' dark wet sand feather just over the water stayed
// clear and stood out as a band against the hazed sand.  So it reaches onto the wet strip and the
// shallowest bed, and the bed under any real depth of water stays clear.
//
// The layer is never thicker than HAZE_LOW_CAP of the full amount, a little under what Golden
// Oasis's low plateau takes: the bank under it went milky at the full amount, and still a little
// at 0.62.
//
// The layer is not even, and it holds still: nothing moves it.  Four points along the last stretch
// of the eye's ray inside the layer read two octaves of value noise fixed to the world, each at its
// own height so the patches part with parallax as the camera moves.  Their mean, eased so the
// patches have soft edges and clear gaps, scales the amount between 0.65 and 1.35 of the even layer
// (one on average, so a map's average haze is what it was; 0.3 to 1.7 went to a flat milky plate on
// the low bank), and the dense patches come out a touch paler, which keeps them apart from the
// map's darker cloud shadows.
//
// The shroud has already darkened the ground under the haze by its light (t2, the very texture the
// terrain multiplies by, through the terrain's own mapping, so the two cannot disagree; a copy read
// through W3DShroud::getShroudLevel did, by cells and by a stair-step edge), so the haze takes
// that light too, its colour once and its amount three times over: in the fog
// (about half light) the haze keeps an eighth of itself at half its brightness, so it can never
// come out paler than the fogged ground it lies on, shrouded ground stays black, and water and its
// bank in the fog match.  Only the colour taking it, the haze's own paleness still lifted the
// fogged ground the way it lifts ground in sight, and the fog read as bright patches.
// Looking toward the sun the colour goes a little warmer, away from it a little cooler.
static const char * const HAZE_SHADER_BODY =
	"cbuffer HazeConstants : register(b1)\n"
	"{\n"
	"    float4 Haze : packoffset(c13);\n"
	"    float4 HazeColour : packoffset(c14);\n"
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n"
	"    float4 HazeSun : packoffset(c19);\n"
	"    float4 HazeShroudRect : packoffset(c20);\n"
	"};\n"
	"Texture2D HazeShroud : register(t2);\n"
	"static const float HAZE_PATCH = 1.0 / 320.0;\n"
	"static const float HAZE_LOW_CAP = 0.5;\n"
	"\n"
	"float haze_hash(float2 p)\n"
	"{\n"
	"    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);\n"
	"}\n"
	"\n"
	"float haze_noise(float2 p)\n"
	"{\n"
	"    float2 cell = floor(p);\n"
	"    float2 f = frac(p);\n"
	"    f = f * f * (3.0 - 2.0 * f);\n"
	"    float a = haze_hash(cell);\n"
	"    float b = haze_hash(cell + float2(1.0, 0.0));\n"
	"    float c = haze_hash(cell + float2(0.0, 1.0));\n"
	"    float d = haze_hash(cell + float2(1.0, 1.0));\n"
	"    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);\n"
	"}\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Sample(Sampler, input.Texture).r;\n"
	"    if (raw >= 0.99999) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n"
	"    world.xyz /= world.w;\n"
	"    float4 eye = mul(float4(ndc, 0.0, 1.0), WorldFromClip);\n"
	"    eye.xyz /= eye.w;\n"
	"    float below = saturate((Haze.x - world.z) / max(Haze.y, 0.0001));\n"
	"    if (below <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
	"    float shroudLight = HazeShroud.SampleLevel(Sampler, (world.xy - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).g;\n"
	"    float reach = saturate(distance(world.xyz, eye.xyz) / max(Haze.w, 0.0001));\n"
	"    float layer = min(below * below * (3.0 - 2.0 * below), HAZE_LOW_CAP);\n"
	"    float amount = Haze.z * layer * (0.65 + 0.35 * reach);\n"
	"    float bank = saturate((world.z - HazeColour.w + 2.0) / 5.0);\n"
	"    bank = bank * bank * (3.0 - 2.0 * bank);\n"
	"    amount *= bank;\n"
	"\n"
	"    float3 ray = world.xyz - eye.xyz;\n"
	"    float entry = (eye.z > Haze.x && abs(ray.z) > 0.0001) ? saturate((Haze.x - eye.z) / ray.z) : 0.0;\n"
	"    float patches = 0.0;\n"
	"    for (int tap = 0; tap < 4; ++tap)\n"
	"    {\n"
	"        float3 at = eye.xyz + ray * lerp(entry, 1.0, ((float)tap + 0.5) * 0.25);\n"
	"        float2 q = at.xy * HAZE_PATCH + at.z * 0.011;\n"
	"        patches += haze_noise(q) * 0.7 + haze_noise(q * 2.7 + 17.0) * 0.3;\n"
	"    }\n"
	"    float dense = smoothstep(0.28, 0.72, patches * 0.25);\n"
	"    amount *= lerp(0.65, 1.35, dense);\n"
	"\n"
	"    float2 look = ray.xy * rsqrt(max(dot(ray.xy, ray.xy), 0.0001));\n"
	"    float2 sun = HazeSun.xy * rsqrt(max(dot(HazeSun.xy, HazeSun.xy), 0.0001));\n"
	"    float toward = dot(look, sun);\n"
	"    float3 colour = HazeColour.rgb * (1.0 + HazeSun.w * toward * float3(0.08, 0.02, -0.07));\n"
	"    colour *= 1.0 + 0.06 * (dense - 0.5);\n"
	"    float lit = lerp(1.0, shroudLight, HazeSun.z);\n"
	"    colour *= lit;\n"
	"    amount *= lit * lit * lit;\n"
	"    return float4(colour, saturate(amount));\n"
	"}\n";

// The Atmosphere option's two programs.  Their constants lie in the haze's slots, the pass writes them
// over for these draws (PassSetup::Atmosphere): c13 is the fade's start, scale, strength and ground
// height, c14 the horizon, c21 the map's size, and BlurDirection the sky's tint.
#define ATMOSPHERE_CONSTANTS \
	"cbuffer AtmosphereConstants : register(b1)\n" \
	"{\n" \
	"    float4 Fade : packoffset(c13);\n" \
	"    float4 Horizon : packoffset(c14);\n" \
	"    row_major float4x4 WorldFromClip : packoffset(c15);\n" \
	"    float4 HazeSun : packoffset(c19);\n" \
	"    float4 HazeShroudRect : packoffset(c20);\n" \
	"    float4 MapSize : packoffset(c21);\n" \
	"};\n" \
	"static const float BELOW_DIM = 0.55;\n" \
	"static const float BELOW_REACH = 0.25;\n" \
	"\n" \
	"float3 haze_along(float up)\n" \
	"{\n" \
	"    return Horizon.rgb * lerp(BELOW_DIM, 1.0, saturate(1.0 + up / BELOW_REACH));\n" \
	"}\n" \
	"\n" \
	"float3 world_at(float2 ndc, float raw)\n" \
	"{\n" \
	"    float4 world = mul(float4(ndc, raw, 1.0), WorldFromClip);\n" \
	"    return world.xyz / world.w;\n" \
	"}\n" \
	"\n"

// The sky, over the whole frame before anything is drawn on it, so whatever the frame leaves empty holds
// it and there is no depth to read: water and glass past the ground blend over it, and a multisampled
// edge resolves between it and what stands in front.  Painted later out of the depth, it took the water
// past the terrain's window and left black samples on every skyline under MSAA.  The eye's ray reads
// EA's own time of day texture from WaterSet, on a plane overhead, opening out of the horizon's colour.
// Below the horizon, past the map's edge or past the far plane, the ray takes that colour dimming the
// further down it looks, not the black a frame clears to and not one flat sheet of it; the ground's fade
// takes the same colour by the same ray, so the two meet with no line.
static const char * const SKY_SHADER_BODY =
	ATMOSPHERE_CONSTANTS
	"Texture2D Sky : register(t3);\n"
	"SamplerState SkySampler : register(s1);\n"
	"static const float SKY_REPEAT = 0.35;\n"
	"static const float SKY_OPEN = 0.30;\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float3 ray = world_at(ndc, 1.0) - world_at(ndc, 0.0);\n"
	"    float reach = max(length(ray), 0.0001);\n"
	"    float up = ray.z / reach;\n"
	"    float open = smoothstep(0.0, SKY_OPEN, up);\n"
	"    float2 plane = ray.xy / (max(up, 0.05) * reach) * SKY_REPEAT;\n"
	"    float3 sky = Sky.SampleLevel(SkySampler, plane, 3.0 * (1.0 - open)).rgb * BlurDirection.rgb;\n"
	"    return float4(lerp(haze_along(up), sky, open), 1.0);\n"
	"}\n";

// The fade, blended over the scene target by its alpha after the haze.  It goes by eye distance past a
// start, and nothing reads the clock, so only the camera shifts it.  The start is the eye's distance to
// its focus and a little over, and never less than the constant: at the game's own tilt the middle of
// the frame and below it stay clean at any zoom, and only the ground beyond fades.  The focus has a
// ceiling, or a camera near level would push the start past all the far ground, and jump as it crossed
// level.  Over the last of the far plane's depth it closes to the horizon whatever the shroud says, or
// the far plane's cut shows as a line.  Short of that the colour takes the shroud's light (t2, what the
// terrain multiplied by), so fogged ground comes out the same fade as ground in sight times that light
// and black ground stays black.  Cutting the amount by the light as the haze does kept the fog dark
// and left the ground in sight a pale disc inside it, on Alpine Assault's far base at the flat tilt.
// The ring past the map (MapSize, the playable map from the world's origin) has no shroud cells and
// draws black, which against a sky stood as a black wall round the world on every map: it goes into the
// horizon's colour across the one shroud cell (SHROUD_CELL, the game's PartitionCellSize) over which the
// shroud's filter itself falls to the border's black, half inside the map's edge and half out.  A fade
// starting at the edge and running on past that left the black showing first, and at a flat tilt 100 units
// of a cliff past the edge is a black smear a tenth of the frame high.  Only depth still at the clear is sky, and
// takes none; ground in the far plane's last sliver read as sky at 0.99999 and stayed unfaded, a hairline
// tracing a ridge whose body had gone.  Below the horizon the same empty depth gets the sky's colour again,
// because the sea runs on past the terrain, writes no depth and draws black out there: Bombardment Beach
// had a black line across the whole frame.  Depth is Load: filtered across a skyline it is a distance nothing
// stands at.  Under MSAA it is one sample's for the whole pixel; the fade is gentle enough there that the
// edge does not show it.
static const char * const ATMOSPHERE_SHADER_BODY =
	ATMOSPHERE_CONSTANTS
	"Texture2D Shroud : register(t2);\n"
	"static const float FOCUS_MARGIN = 1.1;\n"
	"static const float FOCUS_MOST = 2000.0;\n"
	"static const float FAR_CLOSE = 0.85;\n"
	"static const float SHROUD_CELL = 40.0;\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float raw = Extra.Load(int3(input.Position.xy, 0)).r;\n"
	"    float2 ndc = float2(input.Texture.x * 2.0 - 1.0, 1.0 - input.Texture.y * 2.0);\n"
	"    float3 eye = world_at(ndc, 0.0);\n"
	"    float3 ray = world_at(ndc, raw) - eye;\n"
	"    float reach = length(ray);\n"
	"    if (raw >= 1.0) return float4(haze_along(ray.z / max(reach, 0.0001)), ray.z < 0.0 ? 1.0 : 0.0);\n"
	"    float3 centreEye = world_at(float2(0.0, 0.0), 0.0);\n"
	"    float3 axis = world_at(float2(0.0, 0.0), 1.0) - centreEye;\n"
	"    float axisLength = length(axis);\n"
	"    float focus = (axis.z < 0.0) ? min((Fade.w - centreEye.z) / axis.z * axisLength, FOCUS_MOST) : FOCUS_MOST;\n"
	"    float past = max(reach - max(focus * FOCUS_MARGIN, Fade.x), 0.0);\n"
	"    float amount = Fade.z * (1.0 - exp(-past / Fade.y));\n"
	"    float2 here = (eye + ray).xy;\n"
	"    float lit = lerp(1.0, Shroud.SampleLevel(Sampler, (here - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).g, HazeSun.z);\n"
	"    float2 outside = max(-here, here - MapSize.xy);\n"
	"    float closing = max(smoothstep(FAR_CLOSE, 1.0, dot(ray, axis) / (axisLength * axisLength)),\n"
	"        smoothstep(-0.5 * SHROUD_CELL, 0.5 * SHROUD_CELL, max(outside.x, outside.y)));\n"
	"    return float4(haze_along(ray.z / max(reach, 0.0001)) * lerp(lit, 1.0, closing), max(amount, closing));\n"
	"}\n";

// The bright pass, downsampling as it goes: four taps of the full size frame averaged into one
// quarter size texel, then the threshold taken off its brightest channel.  What survives is the
// amount by which something was brighter than white, which is a quantity an eight bit scene target
// could not have held, carried in the pixel's own hue so an orange fire glows orange.  Over the
// first Tuning.w past the threshold it keeps the square of that excess (the soft knee), so it
// starts at nothing with no step and the art, which never passes white, keeps nothing.
static const char * const BLOOM_EXTRACT_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 texel = TexelSize.xy;\n"
	"    float3 sum = Source.Sample(Sampler, input.Texture + float2(-texel.x, -texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(texel.x, -texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(-texel.x, texel.y)).rgb\n"
	"        + Source.Sample(Sampler, input.Texture + float2(texel.x, texel.y)).rgb;\n"
	"    float3 colour = sum * 0.25;\n"
	"    float bright = max(colour.r, max(colour.g, colour.b));\n"
	"    float over = max(bright - Tuning.x, 0.0);\n"
	"    float knee = max(Tuning.w, 0.0001);\n"
	"    float kept = (over < knee) ? over * over / (2.0 * knee) : over - knee * 0.5;\n"
	"    return float4(colour * (kept / max(bright, 0.0001)), 1.0);\n"
	"}\n";

// One half of a separable gaussian.  The direction comes in as a constant, so the same program is
// the horizontal pass and the vertical one.
static const char * const BLOOM_BLUR_SHADER_BODY =
	"static const float WEIGHTS[5] =\n"
	"    { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 step = BlurDirection.xy;\n"
	"    float3 sum = Source.Sample(Sampler, input.Texture).rgb * WEIGHTS[0];\n"
	"    for (int index = 1; index < 5; ++index) {\n"
	"        float2 offset = step * index;\n"
	"        sum += Source.Sample(Sampler, input.Texture + offset).rgb * WEIGHTS[index];\n"
	"        sum += Source.Sample(Sampler, input.Texture - offset).rgb * WEIGHTS[index];\n"
	"    }\n"
	"    return float4(sum, 1.0);\n"
	"}\n";

// The scene and the glow added, then the tone curve, then eight bits.  One pass rather than two
// because the addition is what makes a value overbright and the curve is what brings it back, and
// splitting them would mean writing the sum into a float target nobody else reads.
static const char * const BLOOM_COMPOSITE_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float3 glow = Extra.Sample(Sampler, input.Texture).rgb;\n"
	"    return float4(grade(tone_curve(scene + glow * Tuning.y)), 1.0);\n"
	"}\n";

static const char * const BLOOM_COMPOSITE_WIDE_SHADER_BODY =
	WIDE_CURVE_TEXT
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n"
	"    float3 glow = Extra.Sample(Sampler, input.Texture).rgb;\n"
	"    return float4(grade(wide_curve(scene + glow * Tuning.y)), 1.0);\n"
	"}\n";

// A blast bending the picture, one read per pixel whatever the number of warps: every warp adds
// to where the pixel reads from, and the frame is sampled once at the sum.
//
// The pull reads from further out than the pixel is, so the picture slides toward the centre.  Its
// shape is nothing at the centre, where the direction is undefined, and nothing at the radius, so
// there is no seam; in between it peaks a third of the way out at the strength asked for.  The ring
// reads from nearer the centre, so what was inside it is carried outward on its crest, and across
// its width it falls off as a gaussian, which keeps the band soft rather than a cut line.
//
// The ring is a dome's outline rather than a circle.  Above the centre a hemisphere seen from a
// raised camera shows its round top, as tall as it is wide; below the centre what shows is its
// footprint on the ground nearest the camera, flattened by the pitch.  So the lower half of the
// ring is measured with the vertical squeezed by RingSquash, and it pushes along that ellipse's
// normal, which is what makes the wave run flat across the ground in front of the blast and stand
// up behind it.
//
// The whole shift dies away over the last 8% of the frame toward every edge.  A read past the edge
// comes back as the edge's own texel, and on film that drew the border out into streaks thirty to
// forty pixels long; held to zero at the edge, a pixel near it never reads further out than it is
// from the edge, as long as no warp moves the picture by more than 8% of the frame.
static const char * const WARP_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float aspect = WarpInfo.y;\n"
	"    float2 shift = float2(0.0, 0.0);\n"
	"    for (int index = 0; index < 4; ++index)\n"
	"    {\n"
	"        if (index >= (int)WarpInfo.x) break;\n"
	"        float4 centre = WarpCentre[index];\n"
	"        float4 ring = WarpRing[index];\n"
	"        float2 away = (input.Texture - centre.xy) * float2(aspect, 1.0);\n"
	"        float span = length(away);\n"
	"        float2 outward = away / max(span, 0.0001);\n"
	"        float reach = saturate(span / max(centre.z, 0.0001));\n"
	"        float inward = centre.w * centre.z * 6.75 * reach * (1.0 - reach) * (1.0 - reach);\n"
	"        float squash = (away.y > 0.0) ? max(ring.w, 0.05) : 1.0;\n"
	"        float2 domed = float2(away.x, away.y / squash);\n"
	"        float domeSpan = length(domed);\n"
	"        float2 domeNormal = float2(domed.x, domed.y / squash);\n"
	"        domeNormal /= max(length(domeNormal), 0.0001);\n"
	"        float across = (domeSpan - ring.x) / max(ring.y, 0.0001);\n"
	"        float push = ring.z * centre.z * exp(-across * across);\n"
	"        shift += outward * inward - domeNormal * push;\n"
	"    }\n"
	"    shift.x /= aspect;\n"
	"    float2 border = min(input.Texture, 1.0 - input.Texture);\n"
	"    float2 settle = saturate(border / 0.08);\n"
	"    shift *= settle.x * settle.y;\n"
	"    return float4(Source.Sample(Sampler, input.Texture + shift).rgb, 1.0);\n"
	"}\n";

// -heathaze.  Every flame particle is a splat, six vertices made out of SV_VertexID like the full
// screen triangle, its numbers read out of a buffer four float4 a splat (dx11runtime.h has the
// layout).  The splat writes a shift in texture coordinates into rg and its weight into b, added up
// over every splat that covers the pixel; the composite divides by the weight once it passes one,
// so a stack of flames shimmers as hard as one does and only the edges fade.  A laser's splats
// carry the beam's direction on the screen and leave a band along it with no weight, so the air
// round a beam bends and the beam itself, which wrote no depth to hide it, stays where it is.
//
// The shift is one field over the whole screen rather than per splat, so overlapping flames bend
// the same way and add up instead of cancelling.  Its waves run up the screen with the time: heat
// rises.  The tuning is here: HEAT_FREQUENCY is the waves across the frame's height, HEAT_RISE how
// fast they climb, HEAT_SOFTNESS_* how a surface nearer than the flame takes the shimmer off.
static const char * const HEAT_SPLAT_VERTEX_SOURCE =
	"Buffer<float4> Splats : register(t0);\n"
	"\n"
	"struct SplatOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Corner : TEXCOORD0;\n"
	"    float2 Screen : TEXCOORD1;\n"
	"    float4 Heat : TEXCOORD2;\n"
	"    float4 Planes : TEXCOORD3;\n"
	"    float4 Across : TEXCOORD4;\n"
	"    float2 Offset : TEXCOORD5;\n"
	"};\n"
	"\n"
	"static const float2 CORNERS[6] =\n"
	"{\n"
	"    float2(-1.0, -1.0), float2(-1.0, 1.0), float2(1.0, -1.0),\n"
	"    float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0)\n"
	"};\n"
	"\n"
	"SplatOutput main(uint index : SV_VertexID)\n"
	"{\n"
	"    uint splat = index / 6;\n"
	"    float2 corner = CORNERS[index % 6];\n"
	"    float4 shape = Splats.Load(splat * 4);\n"
	"    SplatOutput output;\n"
	"    float2 clip = shape.xy + corner * shape.zw;\n"
	"    output.Position = float4(clip, 0.0, 1.0);\n"
	"    output.Corner = corner;\n"
	"    output.Screen = clip * float2(0.5, -0.5) + 0.5;\n"
	"    output.Heat = Splats.Load(splat * 4 + 1);\n"
	"    output.Planes = Splats.Load(splat * 4 + 2);\n"
	"    output.Across = Splats.Load(splat * 4 + 3);\n"
	"    output.Offset = corner * shape.zw;\n"
	"    return output;\n"
	"}\n";

// Extra (t1) is the scene's depth here, read texel by texel at full size under the half size pixel.
static const char * const HEAT_SPLAT_SHADER_BODY =
	"struct SplatOutput\n"
	"{\n"
	"    float4 Position : SV_POSITION;\n"
	"    float2 Corner : TEXCOORD0;\n"
	"    float2 Screen : TEXCOORD1;\n"
	"    float4 Heat : TEXCOORD2;\n"
	"    float4 Planes : TEXCOORD3;\n"
	"    float4 Across : TEXCOORD4;\n"
	"    float2 Offset : TEXCOORD5;\n"
	"};\n"
	"\n"
	"static const float HEAT_FREQUENCY = 50.0;\n"
	"static const float HEAT_RISE = 2.0;\n"
	"\n"
	"float linear_depth(float depth, float4 planes)\n"
	"{\n"
	"    return planes.x * planes.y / max(planes.y - depth * (planes.y - planes.x), 0.0001);\n"
	"}\n"
	"\n"
	"float4 main(SplatOutput input) : SV_TARGET\n"
	"{\n"
	"    float reach = dot(input.Corner, input.Corner);\n"
	"    if (reach >= 1.0) discard;\n"
	"    float weight = (1.0 - reach) * (1.0 - reach) * (1.0 - reach);\n"
	"    uint width, height;\n"
	"    Extra.GetDimensions(width, height);\n"
	"    int2 texel = min(int2(input.Position.xy * 2.0), int2(width - 1, height - 1));\n"
	"    float scene = linear_depth(Extra.Load(int3(texel, 0)).r, input.Planes);\n"
	"    float flame = linear_depth(input.Heat.x, input.Planes);\n"
	// Only what lies well behind the flames bends: a unit standing in the fire, or in front of
	// it, keeps its outline.
	"    weight *= saturate((scene - flame - input.Planes.z) / max(input.Planes.w, 0.0001));\n"
	"    float aspect = (float)width / (float)height;\n"
	// A beam's splat: no weight within Across.z of the beam's line, in shares of the frame's
	// height, coming up to full over Across.w further out.  The composite reads nothing where the
	// weight is nought, so the beam is neither moved nor smeared onto the air beside it.  Never
	// narrower than one texel of the half size target, or its bilinear read leaks weight onto a
	// beam seen from far off and the beam wobbles.
	"    if (dot(input.Across.xy, input.Across.xy) > 0.0)\n"
	"    {\n"
	"        float2 along = normalize(input.Across.xy * float2(aspect, 1.0));\n"
	"        float2 offset = input.Offset * float2(aspect, 1.0) * 0.5;\n"
	"        float off = abs(offset.x * along.y - offset.y * along.x);\n"
	"        float still = max(input.Across.z, 2.0 / (float)height);\n"
	"        weight *= saturate((off - still) / max(input.Across.w, 0.0001));\n"
	"    }\n"
	"    if (weight <= 0.0) discard;\n"
	"    float2 p = float2(input.Screen.x * aspect, input.Screen.y) * HEAT_FREQUENCY;\n"
	"    float t = input.Heat.z * HEAT_RISE;\n"
	// Bands across the column climbing it, each swaying sideways a little along its length, so the
	// air above a fire wavers left and right as it rises.
	"    float rise = p.y + t * 3.0 + 0.8 * sin(p.x * 0.35 + t * 0.7);\n"
	"    float2 shift;\n"
	"    shift.x = sin(rise) + 0.3 * sin(rise * 1.9 + p.x * 0.5 - t * 1.3);\n"
	"    shift.y = 0.25 * sin(rise * 0.6 + p.x * 0.4 - t * 0.9);\n"
	"    return float4(shift * input.Heat.y * weight, weight, 0.0);\n"
	"}\n";

// Source is the copy of the scene, Extra the summed shifts at half size.  A pixel no splat touched
// reads its own texel, so the frame outside the shimmer is the frame that came in.  The shift dies
// away over the last 2% toward every edge, the warp's rule, so nothing reads past the frame.
static const char * const HEAT_HAZE_SHADER_BODY =
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float4 heat = Extra.Sample(Sampler, input.Texture);\n"
	"    float2 shift = heat.xy / max(heat.z, 1.0);\n"
	// Never read from a pixel the splats left alone, which is where a unit in the fire stands, so
	// its colour is not pulled out over the ground beside it.
	"    shift *= saturate(Extra.Sample(Sampler, input.Texture + shift).z * 4.0);\n"
	"    float2 border = min(input.Texture, 1.0 - input.Texture);\n"
	"    float2 settle = saturate(border / 0.02);\n"
	"    shift *= settle.x * settle.y;\n"
	"    return Source.Sample(Sampler, input.Texture + shift);\n"
	"}\n";

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

const char * DX11Post_Effect_Name(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA:
		return "fxaa";
	case DX11_POST_SHARPEN:
		return "sharpen";
	case DX11_POST_AO:
		return "ao";
	case DX11_POST_BLOOM:
		return "bloom";
	default:
		return "copy";
	}
}

bool DX11Post_Parse_Chain(const char * text, DX11PostEffect effects[DX11_POST_CHAIN_LIMIT],
	unsigned & count)
{
	count = 0;
	if (text == NULL) {
		return false;
	}

	std::string remaining(text);
	while (!remaining.empty()) {
		const std::string::size_type separator = remaining.find(',');
		std::string name = remaining.substr(0, separator);
		remaining = (separator == std::string::npos) ? std::string() : remaining.substr(separator + 1);

		if (name.empty()) {
			continue;
		}
		if (name == "off" || name == "none") {
			count = 0;
			return true;
		}
		if (count == DX11_POST_CHAIN_LIMIT) {
			count = 0;
			return false;
		}

		if (name == "copy") {
			effects[count++] = DX11_POST_COPY;
		}
		else if (name == "fxaa") {
			effects[count++] = DX11_POST_FXAA;
		}
		else if (name == "sharpen") {
			effects[count++] = DX11_POST_SHARPEN;
		}
		else if (name == "ao") {
			effects[count++] = DX11_POST_AO;
		}
		else if (name == "bloom") {
			// Bloom decides the format the scene is kept in and hands the frame back in eight bits,
			// so a chain cannot reach it after something else has already read the scene. Refusing
			// beats quietly reordering: "fxaa,bloom" is a request somebody meant, and it is not one
			// this can answer.
			if (count != 0) {
				count = 0;
				return false;
			}
			effects[count++] = DX11_POST_BLOOM;
		}
		else {
			count = 0;
			return false;
		}
	}
	return true;
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
	, HeatSplatBuffer(NULL)
	, HeatSplatView(NULL)
	, HeatSplatVertexShader(NULL)
	, HeatSplatShader(NULL)
	, HeatHazeShader(NULL)
	, HeatBlendState(NULL)
	, HeatHazeRefused(false)
{
	memset(&HeatTarget, 0, sizeof(HeatTarget));
	memset(HazeWorldFromClip, 0, sizeof(HazeWorldFromClip));
	memset(WorldAoShaders, 0, sizeof(WorldAoShaders));
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
	release_interface(reinterpret_cast<IUnknown **>(&AlphaBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&MultiplyBlendState));
	release_interface(reinterpret_cast<IUnknown **>(&BlendState));
	release_interface(reinterpret_cast<IUnknown **>(&Sampler));
	release_interface(reinterpret_cast<IUnknown **>(&HazeShader));
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

bool DX11PostProcessClass::World_Occlude_Scene(ID3D11ShaderResourceView * depth,
	const float world_from_clip[16], float projection_x, float projection_y)
{
	if (Occluded || Resolved || !ShadersReady || SceneTarget.View == NULL
			|| Device->Get_Scene_View() != SceneTarget.View || WorldAoRefused
			|| WorldAoShaders[WORLD_AO_PROGRAMS - 1] == NULL) {
		return false;
	}
	const unsigned half_width = (Width + 1) / 2;
	const unsigned half_height = (Height + 1) / 2;
	if (WorldAoTargets[0].Texture == NULL
			&& (!Create_Target(WorldAoTargets[0], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT)
				|| !Create_Target(WorldAoTargets[1], half_width, half_height, DXGI_FORMAT_R32G32_FLOAT))) {
		Release_Target(WorldAoTargets[0]);
		Release_Target(WorldAoTargets[1]);
		return false;
	}
	memcpy(HazeWorldFromClip, world_from_clip, sizeof(HazeWorldFromClip));
	WorldAoProjection[0] = projection_x;
	WorldAoProjection[1] = projection_y;
	// Read and left for the haze, which comes later in the same frame and takes it for good.
	HazeShroudView = HazeShroudSource;

	PassSetup pass;
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

	memset(&pass, 0, sizeof(pass));
	pass.Shader = WorldAoShaders[1];
	pass.World = true;
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
	block.BlurDirection[2] = 0.0f;
	block.BlurDirection[3] = 0.0f;
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
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(pass.DestinationWidth);
	viewport.Height = static_cast<float>(pass.DestinationHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->OMSetBlendState(pass.Multiply ? MultiplyBlendState : pass.Blend ? AlphaBlendState : BlendState,
		blend_factor, 0xffffffff);
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

	context->Draw(FULL_SCREEN_VERTEX_COUNT, 0);

	if (haze) {
		ID3D11ShaderResourceView * no_haze_resource = NULL;
		context->PSSetShaderResources(2, 1, &no_haze_resource);
	}
	if (pass.Atmosphere) {
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
	Draw_Pass(pass);

	pass.Shader = BloomBlurShader;
	pass.SourceWidth = BloomWidth;
	pass.SourceHeight = BloomHeight;
	unsigned source = 0;
	for (unsigned iteration = 0; iteration < BloomBlurPasses; ++iteration) {
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
		if (BloomIntensity > 0.0f) {
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
