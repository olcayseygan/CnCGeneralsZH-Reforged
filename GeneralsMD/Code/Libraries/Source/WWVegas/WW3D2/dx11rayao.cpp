/*
** Ray traced ambient occlusion for the Ambient Occlusion option at the Ray Traced shadow level,
** and DX11BackendClass's half of it (Trace_Ray_Occlusion, Time_Ray_Occlusion).
**
** The option's own first pass guesses what closes a pixel off from the sky out of fourteen depth
** taps: what the screen hides it cannot see, a building's back and a tank's far side count for
** nothing, and a tall building behind a unit's feet has to be ruled out by a bias.  Here every half
** size pixel sends AO_RAYS cosine weighted rays over the hemisphere its surface faces the camera
** with, through the ray grid's solid casters and over the ground's heightfield (ray_occluded), and
** keeps one minus the share that struck something.  Each ray's length is drawn between a quarter of
** AO_REACH and the whole of it, so a blocker at distance d is counted by the rays longer than d: the
** occlusion falls off with distance without the any-hit rays ever knowing it.  The directions and
** lengths are stratified and turned per pixel by the same interleaved noise the first pass turns its
** taps by, and turned again every frame.
**
** Two rays a pixel leave each one at nought, a half or one, and the option's blur alone left that as
** grain the eye catches the moment the camera moves (Grandma_babs, 2026-11-27: "a lot of noise").
** So a second pass folds each frame into the ones before it (RAY_AO_ACCUMULATE_PROGRAM): the pixel's
** world point goes back through the last traced camera, the history is read there bilinear where its
** eye distance agrees, and the frame weighs one over the frames held, up to AO_FRAMES.  What history
** says is first clamped between the least and the most this frame's rays found over the three by
** three half size pixels round it, widened by a quarter each way, so where a tank drove off the
** open ground drops most of its shade in a frame instead of trailing it.  The cell cap's shimmer
** below (which 32 triangles a crowded cell tests changes with the fill's order) is folded away with
** the grain.  ponytail: the history follows the camera only, so a moving unit's own surface fails
** the distance test and keeps one frame's rays and the blur, as before; motion vectors are the
** upgrade.
**
** What comes out holds the first pass's texture in its first two channels: the light kept, faded
** by the shroud as the first pass fades it, and the distance along the eye's axis.  The option's
** depth-stopped blur and upsample then take the rest off and multiply it onto the opaque scene,
** before the water and everything translucent, so what darkens is what darkened before.
**
** The time goes in the triangle lists of the cells round the ray's start, not in the walk: four rays
** cost 1.85 times two, a reach of 16 against 24 saved a sixth.  Cut casters (leaves, fences, the
** foliage) close off where their texel is solid, as the grid's hit test has it.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#include "dx11rayao.h"
#include "dx11backend.h"
#include "dx11raygrid.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// A ray tests the first 32 triangles of each cell it walks, as the lights' test 64.  Pine needles and
// grass put hundreds in a cell, and every occlusion ray starting under them walked those lists:
// with the cut casters in the grid the pass took 3.37 ms on Dark Night's square and 1.83 on
// Heartland Shield, against 0.98 and 1.13 at 32 for 7% and 6% fewer rays stopped (16 took
// another 0.2 ms off and 11% of the stops).  ponytail: which 32 is the fill's order, which changes
// frame to frame, so a crowded cell can shimmer; a list sorted nearest first is the upgrade.
static const char RAY_AO_LIST_CAP[] = "#define RG_LIST_CAP 32\n";

// The frames a pixel's history folds at most: the frame weighs at least a sixteenth, so a shade that
// changes under a still camera has mostly settled in half a second at 60 frames a second, and what is
// left of the per-frame turn on a parked camera is about a seventh of one frame's grain before the blur.
static const float AO_FRAMES = 16.0f;

// What the trace and the fold share: the block, the depth and a pixel's world point.
static const char RAY_AO_COMMON[] =
	"cbuffer Occlusion : register(b0)\n"
	"{\n"
	"    row_major float4x4 AoWorldFromClip;\n"
	"    row_major float4x4 AoPreviousClipFromWorld;\n"
	"    float4 AoScreen;\n"				// full width, height and their reciprocals
	"    float4 AoHalf;\n"				// half width, height; tiles across, tiles down
	"    float4 AoShroudRect;\n"
	"    float4 AoTuning;\n"				// shroud held, probe on, frame number, history held
	"    float4 AoTemporal;\n"			// world units a pixel spans per unit of eye distance, most frames folded
	"};\n"
	"Texture2D<float> AoDepth : register(t0);\n"
	"Texture2D AoShroud : register(t1);\n"
	"SamplerState AoSampler : register(s0);\n"
	"\n"
	"float3 ao_world(int2 texel, float raw)\n"
	"{\n"
	"    float2 ndc = float2((texel.x + 0.5) * AoScreen.z * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * AoScreen.w * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), AoWorldFromClip);\n"
	"    return world.xyz / world.w;\n"
	"}\n"
	// The first pass's fade: under the shroud nothing, in the fog little.
	"float ao_faded(float kept, float3 here)\n"
	"{\n"
	"    if (AoTuning.x < 0.5) return kept;\n"
	"    float lit = AoShroud.SampleLevel(AoSampler, (here.xy - AoShroudRect.xy) * AoShroudRect.zw, 0.0).g;\n"
	"    return lerp(1.0, kept, lit * lit * lit);\n"
	"}\n"
	"\n";

static const char RAY_AO_PROGRAM[] =
	"#define AO_RAYS 2\n"			// 1.28 ms on Twilight Flame's drawlooks view, 4 cost 2.36: the cost is per ray
	"#define AO_REACH 24.0\n"			// world units, beside the first pass's 22
	"#define AO_STRENGTH 1.0\n"
	"#define AO_OFFSET 1.0\n"			// world units a ray starts off its surface, as the sun's do
	"#define TILE_COUNTS 5\n"
	"RWTexture2D<float2> AoOut : register(u0);\n"
	"RWBuffer<uint> AoTiles : register(u1);\n"
	"groupshared uint AoGroup[TILE_COUNTS];\n"
	"\n"
	"float3 ao_world_of(int2 texel) { return ao_world(texel, AoDepth.Load(int3(texel, 0))); }\n"
	"float3 ao_shorter(float3 a, float3 b) { return dot(a, a) < dot(b, b) ? a : b; }\n"
	"uint ao_hash(uint x)\n"
	"{\n"
	"    x = x * 747796405u + 2891336453u;\n"
	"    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;\n"
	"    return (w >> 22u) ^ w;\n"
	"}\n"
	"\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex, uint3 group : SV_GroupID)\n"
	"{\n"
	"    if (index < TILE_COUNTS) AoGroup[index] = 0;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    int2 half_pixel = int2(id.xy);\n"
	"    if (id.x < (uint)AoHalf.x && id.y < (uint)AoHalf.y) {\n"
	"        int2 texel = min(half_pixel * 2 + 1, int2(AoScreen.xy) - 1);\n"
	"        float raw = AoDepth.Load(int3(texel, 0));\n"
	"        float distance_along = 1.0 / (raw * AoWorldFromClip[2][3] + AoWorldFromClip[3][3]);\n"
	"        float kept = 1.0;\n"
	"        if (raw < 0.99999) {\n"
	"            float3 here = ao_world(texel, raw);\n"
	"            float3 across = ao_shorter(here - ao_world_of(texel - int2(1, 0)), ao_world_of(texel + int2(1, 0)) - here);\n"
	"            float3 down = ao_shorter(here - ao_world_of(texel - int2(0, 1)), ao_world_of(texel + int2(0, 1)) - here);\n"
	"            float3 normal = cross(across, down);\n"
	"            float size = length(normal);\n"
	"            normal = (size > 1e-6) ? normal / size : float3(0.0, 0.0, 1.0);\n"
	"            if (dot(normal, ao_world(texel, 0.0) - here) < 0.0) normal = -normal;\n"
	"            float3 from = here + normal * AO_OFFSET;\n"
	"            float3 side = abs(normal.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);\n"
	"            float3 tangent = normalize(cross(side, normal));\n"
	"            float3 bitangent = cross(normal, tangent);\n"
	// The first pass's turn, and two more numbers from a hash of the same pixel, each moved on every
// frame by its own irrational step, so the fold below sees new strata each frame.
	"            float frame = AoTuning.z;\n"
	"            float turn = frac(52.9829189 * frac(dot(float2(half_pixel), float2(0.06711056, 0.00583715))) + frame * 0.61803399);\n"
	"            uint key = ao_hash(uint(half_pixel.y) * 4096u + uint(half_pixel.x));\n"
	"            float spin = frac((key & 0xffff) * (1.0 / 65536.0) + frame * 0.75487767);\n"
	"            float stretch = frac((key >> 16) * (1.0 / 65536.0) + frame * 0.56984029);\n"
	"            uint struck = 0;\n"
	"            uint by_ground = 0;\n"
	"            [loop] for (int k = 0; k < AO_RAYS; ++k) {\n"
	"                float ring = (k + turn) / AO_RAYS;\n"
	"                float angle = 6.2831853 * frac(spin + k * 0.61803399);\n"
	"                float r = sqrt(ring);\n"
	"                float3 way = normalize(tangent * (r * cos(angle)) + bitangent * (r * sin(angle))\n"
	"                    + normal * sqrt(saturate(1.0 - ring)));\n"
	"                float reach = AO_REACH * (0.25 + 0.75 * frac(stretch + k * 0.38196601));\n"
	// ray_occluded's two halves, apart so the probe can tell them; an if, since || marches both.
	"                if (ray_hits_casters(from, way, reach)) struck += 1;\n"
	"                else if (ray_hits_ground(from, way, reach)) { struck += 1; by_ground += 1; }\n"
	"            }\n"
	"            kept = saturate(1.0 - AO_STRENGTH * struck / (float)AO_RAYS);\n"
	// Unfaded: the fold fades what it folded.  The probe counts this frame's rays faded, as before.
	"            if (AoTuning.y > 0.5) {\n"
	"                InterlockedAdd(AoGroup[0], 1);\n"
	"                InterlockedAdd(AoGroup[1], (uint)(ao_faded(kept, here) * 256.0));\n"
	"                InterlockedAdd(AoGroup[2], AO_RAYS);\n"
	"                InterlockedAdd(AoGroup[3], struck - by_ground);\n"
	"                InterlockedAdd(AoGroup[4], by_ground);\n"
	"            }\n"
	"        }\n"
	"        AoOut[half_pixel] = float2(kept, distance_along);\n"
	"    }\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	// A group is 16 full size pixels a side, and a tile 160 by 144, so a group lies in one tile.
	"    if (AoTuning.y > 0.5 && index < TILE_COUNTS && AoGroup[index] != 0) {\n"
	"        uint2 tile = min(group.xy * 16 / uint2(160, 144), uint2(AoHalf.zw) - 1);\n"
	"        InterlockedAdd(AoTiles[(tile.y * (uint)AoHalf.z + tile.x) * TILE_COUNTS + index], AoGroup[index]);\n"
	"    }\n"
	"}\n";

// This frame's rays folded into the history, a half size pixel a thread.  A history texel counts
// only where its eye distance is within eight pixels' width of ground of where this pixel's world
// point stood from the last camera, so a unit that moved off the ground behind it, or ground
// uncovered at the frame's edge, starts again from this frame.
static const char RAY_AO_ACCUMULATE_PROGRAM[] =
	"Texture2D<float2> AoRaw : register(t2);\n"
	"Texture2D<float4> AoHistory : register(t3);\n"
	"RWTexture2D<float4> AoFolded : register(u0);\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID)\n"
	"{\n"
	"    int2 half_pixel = int2(id.xy);\n"
	"    int2 last = int2(AoHalf.xy) - 1;\n"
	"    if (any(half_pixel > last)) return;\n"
	"    float2 now = AoRaw.Load(int3(half_pixel, 0));\n"
	"    int2 texel = min(half_pixel * 2 + 1, int2(AoScreen.xy) - 1);\n"
	"    float raw = AoDepth.Load(int3(texel, 0));\n"
	"    if (raw >= 0.99999) { AoFolded[half_pixel] = float4(1.0, now.y, 1.0, 0.0); return; }\n"
	"    float low = now.x;\n"
	"    float high = now.x;\n"
	"    [unroll] for (int y = -1; y <= 1; ++y) {\n"
	"        [unroll] for (int x = -1; x <= 1; ++x) {\n"
	"            float near_kept = AoRaw.Load(int3(clamp(half_pixel + int2(x, y), int2(0, 0), last), 0)).x;\n"
	"            low = min(low, near_kept);\n"
	"            high = max(high, near_kept);\n"
	"        }\n"
	"    }\n"
	"    float3 here = ao_world(texel, raw);\n"
	"    float history = now.x;\n"
	"    float frames = 0.0;\n"
	"    float4 before = mul(float4(here, 1.0), AoPreviousClipFromWorld);\n"
	"    if (AoTuning.w > 0.5 && before.w > 0.0) {\n"
	"        float2 ndc = before.xy / before.w;\n"
	// A half size pixel holds full size texel 2i+1, so its centre is at 2i+1.5, the upsample's 0.75.
	"        float2 at = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * AoScreen.xy * 0.5 - 0.75;\n"
	"        int2 corner = int2(floor(at));\n"
	"        float2 part = at - corner;\n"
	"        float allowed = 8.0 * AoTemporal.x * before.w;\n"
	"        float folded = 0.0;\n"
	"        float counted = 0.0;\n"
	"        float total = 0.0;\n"
	"        [unroll] for (int ty = 0; ty < 2; ++ty) {\n"
	"            [unroll] for (int tx = 0; tx < 2; ++tx) {\n"
	"                int2 tap = corner + int2(tx, ty);\n"
	"                if (any(tap < 0) || any(tap > last)) continue;\n"
	"                float4 held = AoHistory.Load(int3(tap, 0));\n"
	"                if (held.w <= 0.0 || abs(held.y - before.w) > allowed) continue;\n"
	"                float weight = (tx == 1 ? part.x : 1.0 - part.x) * (ty == 1 ? part.y : 1.0 - part.y);\n"
	"                folded += held.z * weight;\n"
	"                counted += held.w * weight;\n"
	"                total += weight;\n"
	"            }\n"
	"        }\n"
	"        if (total > 0.05) {\n"
	"            history = folded / total;\n"
	"            frames = counted / total;\n"
	"        }\n"
	"    }\n"
	"    frames = min(frames + 1.0, AoTemporal.y);\n"
// A quarter past the box each way: two rays a pixel give nought, a half or one, and over faint
// shade all nine read one most frames, which a plain box would snap the history to.
	"    float kept = lerp(clamp(history, low - 0.25, high + 0.25), now.x, 1.0 / frames);\n"
	"    AoFolded[half_pixel] = float4(ao_faded(kept, here), now.y, kept, frames);\n"
	"}\n";

// a times b, row vectors as the backend's matrices are.
static void ao_multiply(const float a[16], const float b[16], float out[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += a[row * 4 + index] * b[index * 4 + column];
			}
			out[row * 4 + column] = sum;
		}
	}
}

DX11RayOcclusionClass::DX11RayOcclusionClass()
	: Refused(false), Wanted(true), ProbeWanted(false), ProbeTooWide(false), QueriesMade(false), QueriesRefused(false), Width(0), Height(0), Program(NULL),
	  Accumulate(NULL), ConstantBuffer(NULL), Sampler(NULL), Texture(NULL), Write(NULL), Read(NULL), HistoryIndex(0), HistoryHeld(false),
	  FrameNumber(0), Hits(NULL), HitsWrite(NULL),
	  FrameIndex(0), TilesX(0), TilesY(0), CountsRead(false), LastTraced(false), WholeMS(0.0f), RaysMS(0.0f)
{
	memset(History, 0, sizeof(History));
	memset(HistoryWrite, 0, sizeof(HistoryWrite));
	memset(HistoryRead, 0, sizeof(HistoryRead));
	memset(HistoryClipFromWorld, 0, sizeof(HistoryClipFromWorld));
	memset(Frames, 0, sizeof(Frames));
	memset(Counts, 0, sizeof(Counts));
	const char * wanted = getenv("ZHR_RT_AO");
	Wanted = wanted == NULL || strcmp(wanted, "0") != 0;
	ProbeWanted = getenv("ZHR_RT_AO_PROBE") != NULL;
}

void DX11RayOcclusionClass::Release()
{
	IUnknown ** const held[] = { (IUnknown **)&Program, (IUnknown **)&Accumulate, (IUnknown **)&ConstantBuffer, (IUnknown **)&Sampler,
		(IUnknown **)&Write, (IUnknown **)&Read, (IUnknown **)&Texture, (IUnknown **)&HitsWrite, (IUnknown **)&Hits,
		(IUnknown **)&HistoryWrite[0], (IUnknown **)&HistoryRead[0], (IUnknown **)&History[0],
		(IUnknown **)&HistoryWrite[1], (IUnknown **)&HistoryRead[1], (IUnknown **)&History[1] };
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	for (unsigned index = 0; index < QUERY_FRAMES; ++index) {
		Frame & frame = Frames[index];
		IUnknown ** const queries[] = { (IUnknown **)&frame.Disjoint, (IUnknown **)&frame.Start, (IUnknown **)&frame.Traced,
			(IUnknown **)&frame.End, (IUnknown **)&frame.Readback };
		for (unsigned query = 0; query < sizeof(queries) / sizeof(queries[0]); ++query) {
			if (*queries[query] != NULL) {
				(*queries[query])->Release();
				*queries[query] = NULL;
			}
		}
		frame.Open = frame.Issued = frame.WasTraced = frame.Probed = false;
	}
	QueriesMade = false;
	QueriesRefused = false;
	HistoryHeld = false;
	Width = Height = 0;
}

bool DX11RayOcclusionClass::Make_Queries(ID3D11Device * device)
{
	if (QueriesMade) {
		return true;
	}
	if (QueriesRefused) {
		return false;
	}
	bool made = true;
	for (unsigned index = 0; index < QUERY_FRAMES && made; ++index) {
		Frame & frame = Frames[index];
		D3D11_QUERY_DESC query;
		memset(&query, 0, sizeof(query));
		query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		made = SUCCEEDED(device->CreateQuery(&query, &frame.Disjoint));
		query.Query = D3D11_QUERY_TIMESTAMP;
		made = made && SUCCEEDED(device->CreateQuery(&query, &frame.Start))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.Traced))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.End));
		D3D11_BUFFER_DESC staging;
		memset(&staging, 0, sizeof(staging));
		staging.ByteWidth = TILES * TILE_COUNTS * sizeof(unsigned);
		staging.Usage = D3D11_USAGE_STAGING;
		staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		made = made && SUCCEEDED(device->CreateBuffer(&staging, NULL, &frame.Readback));
	}
	// A device that refused some keeps what it made until Release and is not asked again.
	QueriesMade = made;
	QueriesRefused = !made;
	return made;
}

bool DX11RayOcclusionClass::Make(DX11BackendClass & backend, ID3D11Device * device, unsigned width, unsigned height)
{
	if (Refused) {
		return false;
	}
	bool made = true;
	if (Program == NULL) {
		std::vector<unsigned char> code;
		made = backend.Compile_Program(RAY_AO_LIST_CAP + DX11RayGridClass::Trace_Program() + RAY_AO_COMMON + RAY_AO_PROGRAM, "rayao", "cs_5_0", code)
			&& SUCCEEDED(device->CreateComputeShader(&code[0], code.size(), NULL, &Program));
		made = made && backend.Compile_Program(std::string(RAY_AO_COMMON) + RAY_AO_ACCUMULATE_PROGRAM, "rayaofold", "cs_5_0", code)
			&& SUCCEEDED(device->CreateComputeShader(&code[0], code.size(), NULL, &Accumulate));
		D3D11_BUFFER_DESC constants;
		memset(&constants, 0, sizeof(constants));
		constants.ByteWidth = sizeof(Constants);
		constants.Usage = D3D11_USAGE_DEFAULT;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		made = made && SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));
		D3D11_SAMPLER_DESC sampler;
		memset(&sampler, 0, sizeof(sampler));
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		made = made && SUCCEEDED(device->CreateSamplerState(&sampler, &Sampler));
		D3D11_BUFFER_DESC hits;
		memset(&hits, 0, sizeof(hits));
		hits.ByteWidth = TILES * TILE_COUNTS * sizeof(unsigned);
		hits.Usage = D3D11_USAGE_DEFAULT;
		hits.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		D3D11_UNORDERED_ACCESS_VIEW_DESC write;
		memset(&write, 0, sizeof(write));
		write.Format = DXGI_FORMAT_R32_UINT;
		write.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		write.Buffer.NumElements = TILES * TILE_COUNTS;
		made = made && SUCCEEDED(device->CreateBuffer(&hits, NULL, &Hits))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Hits, &write, &HitsWrite));
	}
	const unsigned half_width = (width + 1) / 2;
	const unsigned half_height = (height + 1) / 2;
	if (made && (Texture == NULL || Width != width || Height != height)) {
		IUnknown ** const old[] = { (IUnknown **)&Write, (IUnknown **)&Read, (IUnknown **)&Texture,
			(IUnknown **)&HistoryWrite[0], (IUnknown **)&HistoryRead[0], (IUnknown **)&History[0],
			(IUnknown **)&HistoryWrite[1], (IUnknown **)&HistoryRead[1], (IUnknown **)&History[1] };
		for (unsigned index = 0; index < sizeof(old) / sizeof(old[0]); ++index) {
			if (*old[index] != NULL) {
				(*old[index])->Release();
				*old[index] = NULL;
			}
		}
		D3D11_TEXTURE2D_DESC description;
		memset(&description, 0, sizeof(description));
		description.Width = half_width;
		description.Height = half_height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R32G32_FLOAT;	// the first pass's targets'
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		made = SUCCEEDED(device->CreateTexture2D(&description, NULL, &Texture))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Texture, NULL, &Write))
			&& SUCCEEDED(device->CreateShaderResourceView(Texture, NULL, &Read));
		description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		for (unsigned index = 0; index < 2 && made; ++index) {
			made = SUCCEEDED(device->CreateTexture2D(&description, NULL, &History[index]))
				&& SUCCEEDED(device->CreateUnorderedAccessView(History[index], NULL, &HistoryWrite[index]))
				&& SUCCEEDED(device->CreateShaderResourceView(History[index], NULL, &HistoryRead[index]));
		}
		HistoryHeld = false;
		Width = width;
		Height = height;
	}
	if (!made) {
		Release();
		Refused = true;
	}
	return made;
}

ID3D11ShaderResourceView * DX11RayOcclusionClass::Trace(DX11BackendClass & backend, ID3D11Device * device,
	ID3D11DeviceContext * context, ID3D11ShaderResourceView * depth, unsigned width, unsigned height,
	const float world_from_clip[16], const float clip_from_world[16], float projection_y,
	ID3D11ShaderResourceView * shroud, const float shroud_rect[4])
{
	if (!Wanted || depth == NULL || width == 0 || height == 0 || projection_y == 0.0f || !Make(backend, device, width, height)) {
		return NULL;
	}
	const unsigned tiles_x = (width + TILE_WIDTH - 1) / TILE_WIDTH;
	const unsigned tiles_y = (height + TILE_HEIGHT - 1) / TILE_HEIGHT;
	ProbeTooWide = ProbeWanted && tiles_x * tiles_y > TILES;
	const bool probing = ProbeWanted && !ProbeTooWide;
	Constants block;
	memset(&block, 0, sizeof(block));
	memcpy(block.WorldFromClip, world_from_clip, sizeof(block.WorldFromClip));
	block.Screen[0] = static_cast<float>(width);
	block.Screen[1] = static_cast<float>(height);
	block.Screen[2] = 1.0f / static_cast<float>(width);
	block.Screen[3] = 1.0f / static_cast<float>(height);
	block.Half[0] = static_cast<float>((width + 1) / 2);
	block.Half[1] = static_cast<float>((height + 1) / 2);
	block.Half[2] = static_cast<float>(tiles_x);
	block.Half[3] = static_cast<float>(tiles_y);
	memcpy(block.ShroudRect, shroud_rect, sizeof(block.ShroudRect));
	block.Tuning[0] = (shroud != NULL) ? 1.0f : 0.0f;
	block.Tuning[1] = probing ? 1.0f : 0.0f;
	// Kept small, so the steps it multiplies stay exact in a float.
	FrameNumber = (FrameNumber + 1) % 4096;
	block.Tuning[2] = static_cast<float>(FrameNumber);
	block.Tuning[3] = HistoryHeld ? 1.0f : 0.0f;
	memcpy(block.PreviousClipFromWorld, HistoryClipFromWorld, sizeof(block.PreviousClipFromWorld));
	block.Temporal[0] = 2.0f / (fabsf(projection_y) * static_cast<float>(height));
	block.Temporal[1] = AO_FRAMES;
	context->UpdateSubresource(ConstantBuffer, 0, NULL, &block, 0, 0);

	if (probing) {
		const UINT zero[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(HitsWrite, zero);
	}
	ID3D11ShaderResourceView * reads[2] = { depth, shroud };
	ID3D11UnorderedAccessView * writes[2] = { Write, HitsWrite };
	context->CSSetConstantBuffers(0, 1, &ConstantBuffer);
	context->CSSetShaderResources(0, 2, reads);
	context->CSSetSamplers(0, 1, &Sampler);
	context->CSSetUnorderedAccessViews(0, 2, writes, NULL);
	context->CSSetShader(Program, NULL, 0);
	const UINT groups_x = (static_cast<UINT>(block.Half[0]) + 7) / 8;
	const UINT groups_y = (static_cast<UINT>(block.Half[1]) + 7) / 8;
	context->Dispatch(groups_x, groups_y, 1);
	Frame & frame = Frames[FrameIndex];
	if (frame.Open) {
		context->End(frame.Traced);
	}

	// The fold: this frame's rays and last frame's history in, the other history out.
	ID3D11UnorderedAccessView * no_writes[2] = { NULL, NULL };
	context->CSSetUnorderedAccessViews(0, 2, no_writes, NULL);
	const unsigned written = HistoryIndex ^ 1u;
	ID3D11ShaderResourceView * folding[4] = { depth, shroud, Read, HistoryRead[HistoryIndex] };
	context->CSSetShaderResources(0, 4, folding);
	context->CSSetUnorderedAccessViews(0, 1, &HistoryWrite[written], NULL);
	context->CSSetShader(Accumulate, NULL, 0);
	context->Dispatch(groups_x, groups_y, 1);
	HistoryIndex = written;
	HistoryHeld = true;
	memcpy(HistoryClipFromWorld, clip_from_world, sizeof(HistoryClipFromWorld));

	ID3D11ShaderResourceView * no_reads[4] = { NULL, NULL, NULL, NULL };
	ID3D11Buffer * no_buffer = NULL;
	ID3D11SamplerState * no_sampler = NULL;
	context->CSSetUnorderedAccessViews(0, 1, no_writes, NULL);
	context->CSSetShaderResources(0, 4, no_reads);
	context->CSSetConstantBuffers(0, 1, &no_buffer);
	context->CSSetSamplers(0, 1, &no_sampler);
	context->CSSetShader(NULL, NULL, 0);

	if (frame.Open) {
		if (probing) {
			context->CopyResource(frame.Readback, Hits);
			frame.Probed = true;
			frame.TilesX = tiles_x;
			frame.TilesY = tiles_y;
		}
	}
	return HistoryRead[HistoryIndex];
}

void DX11RayOcclusionClass::Time(ID3D11Device * device, ID3D11DeviceContext * context, bool begin, bool traced)
{
	if (Refused || !Make_Queries(device)) {
		return;
	}
	if (begin) {
		Report(context);
		Frame & previous = Frames[FrameIndex];
		if (previous.Open) {
			context->End(previous.Disjoint);
			previous.Open = false;
		}
		FrameIndex = (FrameIndex + 1) % QUERY_FRAMES;
		Frame & frame = Frames[FrameIndex];
		if (frame.Issued) {
			return;	// not back yet: this frame goes untimed rather than overwrite it
		}
		frame.WasTraced = false;
		frame.Probed = false;
		context->Begin(frame.Disjoint);
		context->End(frame.Start);
		frame.Open = true;
		return;
	}
	Frame & frame = Frames[FrameIndex];
	if (!frame.Open) {
		return;
	}
	context->End(frame.End);
	context->End(frame.Disjoint);
	frame.Open = false;
	frame.Issued = true;
	frame.WasTraced = traced;
}

std::string DX11RayOcclusionClass::Report(ID3D11DeviceContext * context)
{
	if (Refused) {
		return "; ao rays refused by the device";
	}
	if (!QueriesMade || context == NULL) {
		return "";
	}
	const UINT later = D3D11_ASYNC_GETDATA_DONOTFLUSH;
	// Oldest first, so of two frames back in one call the newer is what is kept.
	for (unsigned step = 1; step <= QUERY_FRAMES; ++step) {
		Frame & frame = Frames[(FrameIndex + step) % QUERY_FRAMES];
		if (!frame.Issued) {
			continue;
		}
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
		UINT64 start = 0, traced = 0, end = 0;
		if (context->GetData(frame.Disjoint, &disjoint, sizeof(disjoint), later) != S_OK
				|| context->GetData(frame.Start, &start, sizeof(start), later) != S_OK
				|| context->GetData(frame.End, &end, sizeof(end), later) != S_OK
				|| (frame.WasTraced && context->GetData(frame.Traced, &traced, sizeof(traced), later) != S_OK)) {
			continue;
		}
		if (frame.Probed) {
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (FAILED(context->Map(frame.Readback, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
				continue;
			}
			memcpy(Counts, mapped.pData, sizeof(Counts));
			context->Unmap(frame.Readback, 0);
			CountsRead = true;
			TilesX = frame.TilesX;
			TilesY = frame.TilesY;
		}
		frame.Issued = false;
		LastTraced = frame.WasTraced;
		if (!disjoint.Disjoint && disjoint.Frequency > 0) {
			const double to_ms = 1000.0 / static_cast<double>(disjoint.Frequency);
			WholeMS = static_cast<float>(static_cast<double>(end - start) * to_ms);
			RaysMS = frame.WasTraced ? static_cast<float>(static_cast<double>(traced - start) * to_ms) : 0.0f;
		}
	}
	char line[256];
	if (LastTraced) {
		snprintf(line, sizeof(line), "; ao ray traced, %.3f ms whole (rays %.3f)", WholeMS, RaysMS);
	} else {
		snprintf(line, sizeof(line), "; ao from the screen, %.3f ms whole", WholeMS);
	}
	std::string report = line;
	if (ProbeTooWide) {
		report += "; ao probe off, the screen has more tiles than the probe holds";
	}
	if (CountsRead && LastTraced) {
		unsigned totals[TILE_COUNTS] = { 0, 0, 0, 0, 0 };
		std::string tiles;
		for (unsigned tile = 0; tile < TilesX * TilesY; ++tile) {
			const unsigned * count = &Counts[tile * TILE_COUNTS];
			for (unsigned field = 0; field < TILE_COUNTS; ++field) {
				totals[field] += count[field];
			}
			// Mean light kept in hundredths, then the share of rays struck in hundredths.
			snprintf(line, sizeof(line), "%s%u/%u", (tile % TilesX == 0) ? (tile == 0 ? "" : " | ") : " ",
				count[0] ? (count[1] * 100u) / (256u * count[0]) : 100u,
				count[2] ? ((count[3] + count[4]) * 100u) / count[2] : 0u);
			tiles += line;
		}
		snprintf(line, sizeof(line),
			"; ao probe: %u half size pixels, %u rays, %u stopped by casters, %u by the ground, mean kept %.3f; "
			"tiles %ux%u of %ux%u px, kept/struck in hundredths, rows top down: ",
			totals[0], totals[2], totals[3], totals[4], totals[0] ? totals[1] / (256.0 * totals[0]) : 1.0,
			TilesX, TilesY, static_cast<unsigned>(TILE_WIDTH), static_cast<unsigned>(TILE_HEIGHT));
		report += line;
		report += tiles;
	}
	return report;
}

/* The Ambient Occlusion option's term from rays, at the Ray Traced level only and only on a frame
	 whose caster pass built the grid; NULL otherwise, and the option's first pass runs as it always
	 has.  The scene is bound again before it returns, as the option's passes expect to find it. */
ID3D11ShaderResourceView * DX11BackendClass::Trace_Ray_Occlusion(const float world_from_clip[16],
	ID3D11ShaderResourceView * shroud, const float shroud_rect[4])
{
	if (!RayShadowsWanted || !RayGrid.Built() || Device == NULL) {
		// A frame without rays leaves the history behind the scene it was traced over.
		RayOcclusion.Forget();
		return NULL;
	}
	Resolve_Scene_Depth();
	ID3D11ShaderResourceView * depth = Device->Get_Depth_Texture();
	if (depth == NULL) {
		return NULL;
	}
	ID3D11DeviceContext * context = Device->Get_Context();
	const bool ground = TerrainHeightsView != NULL && TerrainLight[33] > 0.0f;
	const float ground_numbers[4] = { TerrainLight[33], TerrainLight[34], TerrainLight[35],
		static_cast<float>(TerrainHeightsHighest) * TerrainLight[35] };
	// The depth is read, so it cannot stay bound as the depth the frame tests against.
	context->OMSetRenderTargets(0, NULL, NULL);
	RayGrid.Bind(context, ground ? TerrainHeightsView : NULL, ground_numbers);
	float clip_from_world[16];
	ao_multiply(SceneView, SceneProjection, clip_from_world);
	ID3D11ShaderResourceView * traced = RayOcclusion.Trace(*this, Device->Get_Device(), context, depth,
		Device->Get_Width(), Device->Get_Height(), world_from_clip, clip_from_world, SceneProjection[5], shroud, shroud_rect);
	RayGrid.Unbind(context);
	// Taken, as Trace_Ray_Shadows takes RayCaptured: a frame whose caster pass does not run (a
	// refused sun box, a filter's frame) must not trace the casters of the last one that did.
	RayGrid.Invalidate();
	Resume_Scene();
	return traced;
}

void DX11BackendClass::Time_Ray_Occlusion(bool begin, bool traced)
{
	if (RayShadowsWanted && Device != NULL) {
		RayOcclusion.Time(Device->Get_Device(), Device->Get_Context(), begin, traced);
	}
}
