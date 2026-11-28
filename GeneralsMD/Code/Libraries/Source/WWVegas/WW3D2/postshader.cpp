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

#include "postshader.h"

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
	// grey, and a pixel darker than the pivot leans to blue by how far it is under it.  A brighter one
	// keeps its hue: leaning those to red pushed the brightest pixels, lit snow first, toward cream.
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
	"    graded += float3(1.0, 0.25, -1.0) * min(grey - GRADE_PIVOT, 0.0) * Grade.w;\n"
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
	"    float counted = 0.0;\n"
	"    for (int tap = 0; tap < AO_TAPS; ++tap)\n"
	"    {\n"
	"        float angle = turn + 2.39996 * (float)tap;\n"
	"        float along = ((float)tap + 0.5) / (float)AO_TAPS;\n"
	// A tap off the frame knows nothing, so it is left out of the sum and out of the count it is divided
	// by.  Given the edge's depth instead, a tap above the frame stood over ground that recedes that way,
	// and the whole top of the frame went dark; counted as open sky, a unit's shade halved at an edge
	// and quartered in a corner.
	"        int2 at = texel + int2(float2(cos(angle), sin(angle)) * reach * along);\n"
	"        if (any(at < 0) || any(at >= int2(TexelSize.zw))) continue;\n"
	"        counted += 1.0;\n"
	"        float3 to = world_of(at) - here;\n"
	"        float squared = dot(to, to);\n"
	"        float fade = saturate(1.0 - squared / (AO_RADIUS * AO_RADIUS));\n"
	"        closed += fade * fade * saturate(dot(to, facing) * rsqrt(squared + 0.01) - AO_BIAS);\n"
	"    }\n"
	"    float kept = saturate(1.0 - AO_STRENGTH * closed / max(counted, 1.0));\n"
	// The fog the terrain draws, as the haze reads it: under the shroud nothing, in the fog little.
	// the brightest channel is the shroud's light whatever colour the map gives it
	"    float3 shroudTexel = WorldAoShroud.SampleLevel(Sampler, (here.xy - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n"
	"    float lit = lerp(1.0, max(shroudTexel.r, max(shroudTexel.g, shroudTexel.b)), HazeSun.z);\n"
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
	"    float3 shroudTexel = HazeShroud.SampleLevel(Sampler, (world.xy - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n"
	"    float shroudLight = max(shroudTexel.r, max(shroudTexel.g, shroudTexel.b));\n"
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
// of a cliff past the edge is a black smear a tenth of the frame high.  When the terrain draws land past
// its edge (MapSize.zw, DX11Post_Set_Atmosphere_Edge) there is ground out there to see, and the closing
// waits for the heightmap's own edge and then grows over the land's reach, so the land shows by the map
// and is all horizon where it ends; a closing at the playable edge drew a ruler line along it with flat
// haze past.  Only depth still at the clear is sky, and
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
	"    float3 shroudTexel = Shroud.SampleLevel(Sampler, (here - HazeShroudRect.xy) * HazeShroudRect.zw, 0.0).rgb;\n"
	"    float lit = lerp(1.0, max(shroudTexel.r, max(shroudTexel.g, shroudTexel.b)), HazeSun.z);\n"
	"    float2 outside = max(-here, here - MapSize.xy);\n"
	"    float off = (MapSize.w > 0.0)\n"
	"        ? smoothstep(MapSize.z, MapSize.z + MapSize.w, length(max(outside, 0.0)))\n"
	"        : smoothstep(-0.5 * SHROUD_CELL, 0.5 * SHROUD_CELL, max(outside.x, outside.y));\n"
	"    float closing = max(smoothstep(FAR_CLOSE, 1.0, dot(ray, axis) / (axisLength * axisLength)), off);\n"
	"    return float4(haze_along(ray.z / max(reach, 0.0001)) * lerp(lit, 1.0, closing), max(amount, closing));\n"
	"}\n";

// The bright pass, downsampling as it goes: four taps of the full size frame averaged into one
// quarter size texel, then the threshold taken off its brightest channel.  What survives is the
// amount by which something was brighter than white, which is a quantity an eight bit scene target
// could not have held, carried in the pixel's own hue so an orange fire glows orange.  Over the
// first Tuning.w past the threshold it keeps the square of that excess (the soft knee), so it
// starts at nothing with no step and the art, which never passes white, keeps nothing.
//
// Then the glow is pushed away from its own grey by BlurDirection.z (the Glow option's 1.5; one is
// the hue as it was).  Added back to the ground round a fire, a glow carrying the fire's own hue
// still lands as cream, because the tone curve folds its red into the same last fifth as the
// green; a glow with less green and blue in it than the fire comes out of that fold orange.  ZH
// Enhanced's ReShade bloom does the same at 1.53, and that is where its orange halos come from.
//
// The alpha carries the clarity's luminance: the tone-mapped grey of the same four taps, which the
// first blur pairs spread for the composite to measure each pixel against.
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
	"    float3 glow = colour * (kept / max(bright, 0.0001));\n"
	"    float glowGrey = dot(glow, float3(0.299, 0.587, 0.114));\n"
	"    glow = max(lerp(glowGrey.xxx, glow, BlurDirection.z), 0.0);\n"
	"    return float4(glow, dot(tone_curve(colour), float3(0.299, 0.587, 0.114)));\n"
	"}\n";

// One half of a separable gaussian.  The direction comes in as a constant, so the same program is
// the horizontal pass and the vertical one.  The alpha, the clarity's luminance, is blurred only by
// the pairs that say so (BlurDirection.w) and passed through by the rest, so the clarity measures
// against the same width at every Glow level.
static const char * const BLOOM_BLUR_SHADER_BODY =
	"static const float WEIGHTS[5] =\n"
	"    { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };\n"
	"\n"
	"float4 main(VertexOutput input) : SV_TARGET\n"
	"{\n"
	"    float2 step = BlurDirection.xy;\n"
	"    float4 centre = Source.Sample(Sampler, input.Texture);\n"
	"    float4 sum = centre * WEIGHTS[0];\n"
	"    for (int index = 1; index < 5; ++index) {\n"
	"        float2 offset = step * index;\n"
	"        sum += Source.Sample(Sampler, input.Texture + offset) * WEIGHTS[index];\n"
	"        sum += Source.Sample(Sampler, input.Texture - offset) * WEIGHTS[index];\n"
	"    }\n"
	"    return float4(sum.rgb, (BlurDirection.w > 0.0) ? sum.a : centre.a);\n"
	"}\n";

// Clarity, ZH Enhanced's other ReShade pass taken over: local contrast on the luminance, the way
// Clarity.fx's overlay blend does it.  Detail is how much brighter the pixel's own tone-mapped grey
// is than the blur of it round about; the grey moves by that times the strength times its distance
// from the nearer end (the overlay's weight), so black and white stay put and a mid tone moves the
// most.  The pixel keeps its hue: the colour is scaled, not added to.  Under about a tenth of white
// it fades out, as Clarity.fx's blend-if-dark of 25 does, or the shade beside a lit wall goes to
// black.  The glow is not in the detail (plain is the scene without it), so a fire's halo is not
// read as an edge and does not ring.
#define CLARITY_TEXT \
	"float3 clarity(float3 colour, float3 plain, float blurred)\n" \
	"{\n" \
	"    float3 weights = float3(0.299, 0.587, 0.114);\n" \
	"    float detail = dot(plain, weights) - blurred;\n" \
	"    float grey = dot(saturate(colour), weights);\n" \
	"    float moved = grey + BlurDirection.w * min(grey, 1.0 - grey) * detail * smoothstep(0.04, 0.12, grey);\n" \
	"    return saturate(colour * (saturate(moved) / max(grey, 0.0001)));\n" \
	"}\n" \
	"\n"

// The scene and the glow added, then the tone curve, then eight bits.  One pass rather than two
// because the addition is what makes a value overbright and the curve is what brings it back, and
// splitting them would mean writing the sum into a float target nobody else reads.  The clarity
// goes on between the curve and the grade, where Clarity.fx stood in ZH Enhanced's chain: on the
// picture as it would reach the screen.  Off, it is one untaken branch.
#define BLOOM_COMPOSITE_TEXT(curve) \
	CLARITY_TEXT \
	"float4 main(VertexOutput input) : SV_TARGET\n" \
	"{\n" \
	"    float3 scene = Source.Sample(Sampler, input.Texture).rgb;\n" \
	"    float4 glow = Extra.Sample(Sampler, input.Texture);\n" \
	"    float3 colour = " curve "(scene + glow.rgb * Tuning.y);\n" \
	"    if (BlurDirection.w > 0.0) colour = clarity(colour, tone_curve(scene), glow.a);\n" \
	"    return float4(grade(colour), 1.0);\n" \
	"}\n"

static const char * const BLOOM_COMPOSITE_SHADER_BODY = BLOOM_COMPOSITE_TEXT("tone_curve");

static const char * const BLOOM_COMPOSITE_WIDE_SHADER_BODY =
	WIDE_CURVE_TEXT
	BLOOM_COMPOSITE_TEXT("wide_curve");
#undef BLOOM_COMPOSITE_TEXT
#undef CLARITY_TEXT

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

const char * PostShader_Pixel_Prologue() { return PIXEL_SHADER_PROLOGUE; }
const char * PostShader_Vertex_Source() { return VERTEX_SHADER_SOURCE; }
const char * PostShader_Heat_Splat_Vertex_Source() { return HEAT_SPLAT_VERTEX_SOURCE; }

const char * PostShader_Pixel_Body(PostShaderProgram program)
{
	switch (program) {
	case POST_SHADER_COPY: return COPY_SHADER_BODY;
	case POST_SHADER_TONE_MAP: return TONE_MAP_SHADER_BODY;
	case POST_SHADER_TONE_MAP_WIDE: return TONE_MAP_WIDE_SHADER_BODY;
	case POST_SHADER_FXAA: return FXAA_SHADER_BODY;
	case POST_SHADER_SHARPEN: return SHARPEN_SHADER_BODY;
	case POST_SHADER_SHARPEN_MULTISAMPLED: return SHARPEN_MULTISAMPLED_SHADER_BODY;
	case POST_SHADER_AO: return AO_SHADER_BODY;
	case POST_SHADER_AO_SCENE: return AO_SCENE_SHADER_BODY;
	case POST_SHADER_WORLD_AO: return WORLD_AO_SHADER_BODY;
	case POST_SHADER_WORLD_AO_BLUR: return WORLD_AO_BLUR_SHADER_BODY;
	case POST_SHADER_WORLD_AO_UPSAMPLE: return WORLD_AO_UPSAMPLE_SHADER_BODY;
	case POST_SHADER_HAZE: return HAZE_SHADER_BODY;
	case POST_SHADER_SKY: return SKY_SHADER_BODY;
	case POST_SHADER_ATMOSPHERE: return ATMOSPHERE_SHADER_BODY;
	case POST_SHADER_BLOOM_EXTRACT: return BLOOM_EXTRACT_SHADER_BODY;
	case POST_SHADER_BLOOM_BLUR: return BLOOM_BLUR_SHADER_BODY;
	case POST_SHADER_BLOOM_COMPOSITE: return BLOOM_COMPOSITE_SHADER_BODY;
	case POST_SHADER_BLOOM_COMPOSITE_WIDE: return BLOOM_COMPOSITE_WIDE_SHADER_BODY;
	case POST_SHADER_WARP: return WARP_SHADER_BODY;
	case POST_SHADER_HEAT_SPLAT: return HEAT_SPLAT_SHADER_BODY;
	case POST_SHADER_HEAT_HAZE: return HEAT_HAZE_SHADER_BODY;
	default: return NULL;
	}
}

std::string PostShader_Pixel_Source(PostShaderProgram program)
{
	const char * body = PostShader_Pixel_Body(program);
	return body != NULL ? std::string(PIXEL_SHADER_PROLOGUE) + body : std::string();
}

PostShaderProgram PostShader_Simple_Effect(DX11PostEffect effect)
{
	switch (effect) {
	case DX11_POST_FXAA: return POST_SHADER_FXAA;
	case DX11_POST_SHARPEN: return POST_SHADER_SHARPEN;
	case DX11_POST_AO: return POST_SHADER_AO;
	default: return POST_SHADER_COPY;
	}
}
