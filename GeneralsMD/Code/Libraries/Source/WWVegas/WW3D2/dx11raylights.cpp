/*
** Shadowed point lights at the Ray Traced shadow level, and DX11BackendClass's half of it
** (Ray_Lights_Pending, Trace_Ray_Lights).
**
** The lights are the slots RTS3DScene's handBlastLights hands over every frame (the Firelight
** option's fires first, then the FX lists' pulses nearest the camera), with their own colours,
** reaches and falloff: the pass's sum is BLAST_LIGHT_SAMPLING's line for line, the facing turned to
** the eye and the knee on the brightest channel, so a pixel no ray was stopped for takes the gain
** the programs would have given it.  The facing comes from the depth's neighbours, the shorter step
** each way, where the programs took a triangle's own derivatives.
**
** Three dispatches.  The first finds, per pixel, the lights that reach it and queues one ray job
** for each of a quarter of them (the slot number modulo four, by the pixel's place in its two by two
** quad).  The second runs the jobs, one a thread, from one unit off the surface toward the light and
** LIGHT_CLEARANCE short of it, so the hull a burning wreck's light sits in does not stop every ray,
** and marks a stopped light on the quad.  The ray aims at least LIGHT_LIFT (a tenth) of the light's
** reach above the ground under it: an FX pulse sits on the ground it burst on, and a China nuke's on
** Dark Night's square, aimed at that point, lost the light behind every kerb and rise (576 005 lit
** pixels darkened at the flash's peak, 89 315 aimed 45 units up, the stone tower's shadow kept).
** The third sums the lights each pixel keeps.  The trace is a dispatch of its own because the
** grid walk takes so many registers that, inlined into the per-pixel program, it slowed every pixel on the screen: one program for all three ran at about
** 2 ms on a 25 light fight whether it cast 60 000 rays or 15 000.
**
** A light any pixel of a quad found blocked is taken off all four, so a shadow's edge is two pixels
** wide and an edge between a unit and the ground behind it can take one pixel the wrong answer.  The
** rays are any-hit through the casters and over the ground (ray_occluded), a cut caster (leaves,
** fences) only where its texel is solid; the light is taken whole, diffuse and ambient.
** A ray tests the first 64 triangles of each cell it crosses (RAY_LIGHT_LIST_CAP), so a light can
** slip through a gap in a crowded cell.  Past MAX_JOBS a frame's rays are dropped and those lights
** stay unshadowed.
**
** The gain goes onto the scene as the scene plus the scene times the gain, then a MIN blend against
** white clamps where a light reached, which is what saturate(colour * (1 + gain)) did in the
** programs.  Some things differ from the programs.  A vehicle's headlight and a blast over the same
** pixel multiply here, (1 + h)(1 + b), where they were added.  The pass runs before the batched
** trees are drawn (Direct3D11_Ray_Traced_Lights from RTS3DScene::Flush), since no level ever lit
** them: lit here, the night maps' blue foliage went grey under a nuke.  The light lands after the
** fog, where the programs lit before it, so fogged ground near a blast brightens a little more.  And on a half float scene (bloom on) an opaque additive draw over one in a lit pixel
** is clamped to one with the rest.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#include "dx11raylights.h"
#include "dx11backend.h"
#include "dx11raygrid.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static const unsigned MAX_JOBS = 1u << 20;	///< a frame's rays; the busiest fight measured cast about 90 000
#define MAX_JOBS_TEXT "1048576"
static const unsigned MAX_SIDE = 4096;		///< the jobs' twelve bit coordinates; a wider screen keeps the programs' lights
// The triangles a ray tests in one grid cell.  A tank's cells hold hundreds, and every ray to a
// light at its gun or its wreck crossed them: on a 25 light fight the trace took 2.15 ms whole and
// 0.29 at 64, which let 0.3% of the stopped rays through on Dark Night's square.
static const std::string RAY_LIGHT_LIST_CAP = "#define RG_LIST_CAP 64\n";

// What the three programs share: the block, the depth, the pixel's world point and facing, a light's
// share at a pixel.
static const char RAY_LIGHT_COMMON[] =
	"#define LIGHT_OFFSET 1.0\n"			// world units a ray starts off its surface, as the occlusion's do
	"#define LIGHT_CLEARANCE 6.0\n"		// world units short of the light a ray stops
	"#define LIGHT_LIFT 0.1\n"				// share of a light's reach its rays aim above the ground under it
	"#define MAX_JOBS " MAX_JOBS_TEXT "\n"
	"#define TILE_COUNTS 4\n"
	"cbuffer Lights : register(b0)\n"
	"{\n"
	"    row_major float4x4 LtWorldFromClip;\n"
	"    float4 LtScreen;\n"				// width, height and their reciprocals
	"    float4 LtTuning;\n"				// lights, probe on, tiles across, tiles down
	"    float4 LtPosition[" BLAST_LIGHT_SLOTS_TEXT "];\n"
	"    float4 LtDiffuse[" BLAST_LIGHT_SLOTS_TEXT "];\n"
	"    float4 LtAmbient[" BLAST_LIGHT_SLOTS_TEXT "];\n"
	"};\n"
	"Texture2D<float> LtDepth : register(t0);\n"
	"\n"
	"float3 lt_world(int2 texel, float raw)\n"
	"{\n"
	"    float2 ndc = float2((texel.x + 0.5) * LtScreen.z * 2.0 - 1.0, 1.0 - (texel.y + 0.5) * LtScreen.w * 2.0);\n"
	"    float4 world = mul(float4(ndc, raw, 1.0), LtWorldFromClip);\n"
	"    return world.xyz / world.w;\n"
	"}\n"
	"float3 lt_world_of(int2 texel)\n"
	"{\n"
	"    int2 inside = clamp(texel, int2(0, 0), int2(LtScreen.xy) - 1);\n"
	"    return lt_world(inside, LtDepth.Load(int3(inside, 0)));\n"
	"}\n"
	"float3 lt_shorter(float3 a, float3 b) { return dot(a, a) < dot(b, b) ? a : b; }\n"
	// The surface at a pixel, turned to the eye.
	"float3 lt_facing(int2 texel, float3 here)\n"
	"{\n"
	"    float3 across = lt_shorter(here - lt_world_of(texel - int2(1, 0)), lt_world_of(texel + int2(1, 0)) - here);\n"
	"    float3 down = lt_shorter(here - lt_world_of(texel - int2(0, 1)), lt_world_of(texel + int2(0, 1)) - here);\n"
	"    float3 facing = cross(across, down);\n"
	"    float3 surface = facing * rsqrt(max(dot(facing, facing), 1e-20));\n"
	"    return (dot(surface, lt_world(texel, 0.0) - here) < 0.0) ? -surface : surface;\n"
	"}\n"
	// The pixel's world point when it is solid and some light reaches it.
	"bool lt_lit_point(int2 texel, out float3 here)\n"
	"{\n"
	"    here = float3(0.0, 0.0, 0.0);\n"
	"    if (texel.x >= (int)LtScreen.x || texel.y >= (int)LtScreen.y) return false;\n"
	"    float raw = LtDepth.Load(int3(texel, 0));\n"
	"    if (raw >= 0.99999) return false;\n"
	"    here = lt_world(texel, raw);\n"
	"    bool near_one = false;\n"
	"    int count = (int)LtTuning.x;\n"
	"    [loop] for (int i = 0; i < count && !near_one; ++i)\n"
	"        near_one = distance(LtPosition[i].xyz, here) < LtPosition[i].w;\n"
	"    return near_one;\n"
	"}\n"
	"float3 lt_reaching(int i, float3 here, float3 surface)\n"
	"{\n"
	"    float3 to = LtPosition[i].xyz - here;\n"
	"    float dist = length(to);\n"
	"    float reach = LtPosition[i].w;\n"
	"    if (dist >= reach) return float3(0.0, 0.0, 0.0);\n"
	"    float inner = LtDiffuse[i].w;\n"
	"    float fall = saturate(1.0 - (dist - inner) / max(reach - inner, 0.001));\n"
	"    float shade = saturate(dot(surface, to / max(dist, 0.001)));\n"
	"    return fall * (shade * LtDiffuse[i].rgb + LtAmbient[i].rgb);\n"
	"}\n"
	"uint lt_tile(int2 texel)\n"
	"{\n"
	"    uint2 tile = min(uint2(texel) / uint2(160, 144), uint2(LtTuning.zw) - 1);\n"
	"    return (tile.y * (uint)LtTuning.z + tile.x) * TILE_COUNTS;\n"
	"}\n"
	"\n";

// The jobs: x and y in twelve bits each, the slot in five, one uint a ray.  The count is the first
// word of the arguments buffer and the trace's group count the second, kept up as jobs are taken.
static const char RAY_LIGHT_QUEUE_PROGRAM[] =
	"RWBuffer<uint> LtJobs : register(u0);\n"
	"RWByteAddressBuffer LtArguments : register(u1);\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID)\n"
	"{\n"
	"    int2 texel = int2(id.xy);\n"
	"    float3 here;\n"
	"    if (!lt_lit_point(texel, here)) return;\n"
	"    float3 from = here + lt_facing(texel, here) * LIGHT_OFFSET;\n"
	"    int corner = (int)((id.y & 1) * 2 + (id.x & 1));\n"
	"    int count = (int)LtTuning.x;\n"
	"    [loop] for (int i = corner; i < count; i += 4) {\n"
	"        if (distance(LtPosition[i].xyz, here) >= LtPosition[i].w) continue;\n"
	"        if (distance(LtPosition[i].xyz, from) <= LIGHT_CLEARANCE) continue;\n"
	"        uint slot;\n"
	"        LtArguments.InterlockedAdd(0, 1, slot);\n"
	"        if (slot >= MAX_JOBS) return;\n"
	"        LtJobs[slot] = id.x | (id.y << 12) | ((uint)i << 24);\n"
	"        uint previous;\n"
	"        LtArguments.InterlockedMax(4, slot / 64 + 1, previous);\n"
	"    }\n"
	"}\n";

static const char RAY_LIGHT_TRACE_PROGRAM[] =
	"Buffer<uint> LtJobs : register(t1);\n"
	"ByteAddressBuffer LtArguments : register(t2);\n"
	"RWTexture2D<uint> LtBlocked : register(u0);\n"
	"RWBuffer<uint> LtTiles : register(u1);\n"
	"[numthreads(64, 1, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID)\n"
	"{\n"
	"    if (id.x >= min(LtArguments.Load(0), MAX_JOBS)) return;\n"
	"    uint job = LtJobs[id.x];\n"
	"    int2 texel = int2(job & 4095, (job >> 12) & 4095);\n"
	"    uint i = job >> 24;\n"
	"    float3 here = lt_world(texel, LtDepth.Load(int3(texel, 0)));\n"
	"    float3 from = here + lt_facing(texel, here) * LIGHT_OFFSET;\n"
	// An FX pulse sits on the ground it burst on, and a ray to a point on the ground grazes every rise
	// and kerb on the way. The flash it stands for is a fireball, so the ray aims at least
	// LIGHT_LIFT of its reach above the ground under it.
	"    float3 target = LtPosition[i].xyz;\n"
	"    if (RgTuning.x > 0.5) {\n"
	"        float twist;\n"
	"        int2 cell;\n"
	"        target.z = max(target.z, rg_ground_at(target.xy, twist, cell) + LtPosition[i].w * LIGHT_LIFT);\n"
	"    }\n"
	"    float3 way = target - from;\n"
	"    float span = length(way);\n"
	"    bool stopped = ray_occluded(from, way / span, span - LIGHT_CLEARANCE);\n"
	"    if (stopped) InterlockedOr(LtBlocked[uint2(texel) >> 1], 1u << i);\n"
	"    if (LtTuning.y > 0.5) {\n"
	"        uint tile = lt_tile(texel);\n"
	"        InterlockedAdd(LtTiles[tile + 2], 1);\n"
	"        if (stopped) InterlockedAdd(LtTiles[tile + 3], 1);\n"
	"    }\n"
	"}\n";

static const char RAY_LIGHT_SHADE_PROGRAM[] =
	"Texture2D<uint> LtBlocked : register(t1);\n"
	"RWTexture2D<float4> LtGain : register(u0);\n"
	"RWBuffer<uint> LtTiles : register(u1);\n"
	"groupshared uint LtGroup[2];\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)\n"
	"{\n"
	"    if (index < 2) LtGroup[index] = 0;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    int2 texel = int2(id.xy);\n"
	"    float3 here;\n"
	"    float3 whole = float3(0.0, 0.0, 0.0);\n"
	"    float3 kept = float3(0.0, 0.0, 0.0);\n"
	"    if (lt_lit_point(texel, here)) {\n"
	"        float3 surface = lt_facing(texel, here);\n"
	"        uint blocked = LtBlocked[uint2(texel) >> 1];\n"
	"        int count = (int)LtTuning.x;\n"
	"        [loop] for (int i = 0; i < count; ++i) {\n"
	"            float3 light = lt_reaching(i, here, surface);\n"
	"            whole += light;\n"
	"            if ((blocked & (1u << (uint)i)) == 0) kept += light;\n"
	"        }\n"
	"    }\n"
	// BLAST_LIGHT_SAMPLING's knee.
	"    float peak = max(kept.r, max(kept.g, kept.b));\n"
	"    float over = max(peak - 0.5, 0.0);\n"
	"    float held = min(peak, 0.5) + 0.25 * over / (0.25 + over);\n"
	"    float3 gain = kept * (held / max(peak, 1e-4));\n"
	"    bool reached = any(whole > 0.0);\n"
	"    if (texel.x < (int)LtScreen.x && texel.y < (int)LtScreen.y)\n"
	"        LtGain[texel] = float4(gain, reached ? 1.0 : 0.0);\n"
	"    if (LtTuning.y > 0.5) {\n"
	"        if (reached) InterlockedAdd(LtGroup[0], 1);\n"
	"        if (any(whole - kept > 1e-4)) InterlockedAdd(LtGroup[1], 1);\n"
	"    }\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	// A group is 8 pixels a side and a tile 160 by 144, so a group lies in one tile.
	"    if (index < 2 && LtTuning.y > 0.5) {\n"
	"        if (LtGroup[index] != 0) InterlockedAdd(LtTiles[lt_tile(texel) + index], LtGroup[index]);\n"
	"    }\n"
	"}\n";

static const char RAY_LIGHT_MULTIPLY_PROGRAM[] =
	"Texture2D<float4> LtGain : register(t0);\n"
	"float4 main(float4 position : SV_Position) : SV_Target\n"
	"{\n"
	"    float4 gain = LtGain.Load(int3(position.xy, 0));\n"
	"    if (gain.a < 0.5) discard;\n"
	"    return float4(gain.rgb, 1.0);\n"
	"}\n";

static const char RAY_LIGHT_CLAMP_PROGRAM[] =
	"Texture2D<float4> LtGain : register(t0);\n"
	"float4 main(float4 position : SV_Position) : SV_Target\n"
	"{\n"
	"    if (LtGain.Load(int3(position.xy, 0)).a < 0.5) discard;\n"
	"    return float4(1.0, 1.0, 1.0, 1.0);\n"
	"}\n";

DX11RayLightClass::DX11RayLightClass()
	: Refused(false), EnvironmentWanted(true), ProbeWanted(false), Width(0), Height(0), Queue(NULL), Trace(NULL),
	  Shade(NULL), Multiply(NULL), Clamp(NULL), LightBlend(NULL), ClampBlend(NULL), ConstantBuffer(NULL), Jobs(NULL), JobsWrite(NULL),
	  JobsRead(NULL), Arguments(NULL), ArgumentsWrite(NULL), ArgumentsRead(NULL), Blocked(NULL), BlockedWrite(NULL),
	  BlockedRead(NULL), Texture(NULL), Write(NULL), Read(NULL), Hits(NULL), HitsWrite(NULL), FrameIndex(0),
	  CountsTilesX(0), CountsTilesY(0), CountsRead(false), LastLights(0), LastMS(0.0f), Timed(false)
{
	memset(Frames, 0, sizeof(Frames));
	memset(Counts, 0, sizeof(Counts));
	const char * wanted = getenv("ZHR_RT_LIGHTS");
	EnvironmentWanted = wanted == NULL || strcmp(wanted, "0") != 0;
	ProbeWanted = getenv("ZHR_RT_LIGHTS_PROBE") != NULL;
}

void DX11RayLightClass::Release_Sized()
{
	IUnknown ** const held[] = { (IUnknown **)&BlockedWrite, (IUnknown **)&BlockedRead, (IUnknown **)&Blocked,
		(IUnknown **)&Write, (IUnknown **)&Read, (IUnknown **)&Texture };
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	Width = Height = 0;
}

void DX11RayLightClass::Release()
{
	Release_Sized();
	IUnknown ** const held[] = { (IUnknown **)&Queue, (IUnknown **)&Trace, (IUnknown **)&Shade,
		(IUnknown **)&Multiply, (IUnknown **)&Clamp, (IUnknown **)&LightBlend, (IUnknown **)&ClampBlend, (IUnknown **)&ConstantBuffer,
		(IUnknown **)&JobsWrite, (IUnknown **)&JobsRead, (IUnknown **)&Jobs, (IUnknown **)&ArgumentsWrite,
		(IUnknown **)&ArgumentsRead, (IUnknown **)&Arguments, (IUnknown **)&HitsWrite, (IUnknown **)&Hits };
	for (unsigned index = 0; index < sizeof(held) / sizeof(held[0]); ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	for (unsigned index = 0; index < QUERY_FRAMES; ++index) {
		Frame & frame = Frames[index];
		IUnknown ** const queries[] = { (IUnknown **)&frame.Disjoint, (IUnknown **)&frame.Start,
			(IUnknown **)&frame.End, (IUnknown **)&frame.Readback };
		for (unsigned query = 0; query < sizeof(queries) / sizeof(queries[0]); ++query) {
			if (*queries[query] != NULL) {
				(*queries[query])->Release();
				*queries[query] = NULL;
			}
		}
		frame.Issued = frame.Probed = false;
	}
}

static bool make_compute(DX11BackendClass & backend, ID3D11Device * device, const std::string & source, const char * name,
	ID3D11ComputeShader ** program)
{
	std::vector<unsigned char> code;
	return backend.Compile_Program(source, name, "cs_5_0", code)
		&& SUCCEEDED(device->CreateComputeShader(&code[0], code.size(), NULL, program));
}

static bool make_pixel(DX11BackendClass & backend, ID3D11Device * device, const char * source, const char * name,
	ID3D11PixelShader ** program)
{
	std::vector<unsigned char> code;
	return backend.Compile_Program(source, name, "ps_5_0", code)
		&& SUCCEEDED(device->CreatePixelShader(&code[0], code.size(), NULL, program));
}

bool DX11RayLightClass::Make(DX11BackendClass & backend, ID3D11Device * device, unsigned width, unsigned height)
{
	if (Refused) {
		return false;
	}
	bool made = true;
	if (Queue == NULL) {
		const std::string common = RAY_LIGHT_COMMON;
		made = make_compute(backend, device, common + RAY_LIGHT_QUEUE_PROGRAM, "raylightsqueue", &Queue)
			&& make_compute(backend, device, RAY_LIGHT_LIST_CAP + DX11RayGridClass::Trace_Program() + common + RAY_LIGHT_TRACE_PROGRAM,
				"raylightstrace", &Trace)
			&& make_compute(backend, device, common + RAY_LIGHT_SHADE_PROGRAM, "raylightsshade", &Shade)
			&& make_pixel(backend, device, RAY_LIGHT_MULTIPLY_PROGRAM, "raylightsmultiply", &Multiply)
			&& make_pixel(backend, device, RAY_LIGHT_CLAMP_PROGRAM, "raylightsclamp", &Clamp);
		D3D11_BLEND_DESC blend;
		memset(&blend, 0, sizeof(blend));
		blend.RenderTarget[0].BlendEnable = TRUE;
		blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MIN;
		blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
		blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
			| D3D11_COLOR_WRITE_ENABLE_BLUE;
		made = made && SUCCEEDED(device->CreateBlendState(&blend, &ClampBlend));
		// The scene plus the scene times the light.  A multiply by one plus the light would be clamped
		// to one as a source factor on an eight bit scene, which is the scene without bloom.
		blend.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
		blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		made = made && SUCCEEDED(device->CreateBlendState(&blend, &LightBlend));
		D3D11_BUFFER_DESC constants;
		memset(&constants, 0, sizeof(constants));
		constants.ByteWidth = sizeof(Constants);
		constants.Usage = D3D11_USAGE_DEFAULT;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		made = made && SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));

		D3D11_BUFFER_DESC jobs;
		memset(&jobs, 0, sizeof(jobs));
		jobs.ByteWidth = MAX_JOBS * sizeof(unsigned);
		jobs.Usage = D3D11_USAGE_DEFAULT;
		jobs.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		D3D11_UNORDERED_ACCESS_VIEW_DESC jobs_write;
		memset(&jobs_write, 0, sizeof(jobs_write));
		jobs_write.Format = DXGI_FORMAT_R32_UINT;
		jobs_write.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		jobs_write.Buffer.NumElements = MAX_JOBS;
		D3D11_SHADER_RESOURCE_VIEW_DESC jobs_read;
		memset(&jobs_read, 0, sizeof(jobs_read));
		jobs_read.Format = DXGI_FORMAT_R32_UINT;
		jobs_read.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		jobs_read.Buffer.NumElements = MAX_JOBS;
		made = made && SUCCEEDED(device->CreateBuffer(&jobs, NULL, &Jobs))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Jobs, &jobs_write, &JobsWrite))
			&& SUCCEEDED(device->CreateShaderResourceView(Jobs, &jobs_read, &JobsRead));

		// The job count, then the trace's dispatch: groups across, one, one.
		D3D11_BUFFER_DESC arguments;
		memset(&arguments, 0, sizeof(arguments));
		arguments.ByteWidth = 4 * sizeof(unsigned);
		arguments.Usage = D3D11_USAGE_DEFAULT;
		arguments.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		arguments.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_write;
		memset(&arguments_write, 0, sizeof(arguments_write));
		arguments_write.Format = DXGI_FORMAT_R32_TYPELESS;
		arguments_write.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		arguments_write.Buffer.NumElements = 4;
		arguments_write.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		D3D11_SHADER_RESOURCE_VIEW_DESC arguments_read;
		memset(&arguments_read, 0, sizeof(arguments_read));
		arguments_read.Format = DXGI_FORMAT_R32_TYPELESS;
		arguments_read.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		arguments_read.BufferEx.NumElements = 4;
		arguments_read.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		made = made && SUCCEEDED(device->CreateBuffer(&arguments, NULL, &Arguments))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Arguments, &arguments_write, &ArgumentsWrite))
			&& SUCCEEDED(device->CreateShaderResourceView(Arguments, &arguments_read, &ArgumentsRead));

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
		for (unsigned index = 0; index < QUERY_FRAMES && made; ++index) {
			Frame & frame = Frames[index];
			D3D11_QUERY_DESC query;
			memset(&query, 0, sizeof(query));
			query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			made = SUCCEEDED(device->CreateQuery(&query, &frame.Disjoint));
			query.Query = D3D11_QUERY_TIMESTAMP;
			made = made && SUCCEEDED(device->CreateQuery(&query, &frame.Start))
				&& SUCCEEDED(device->CreateQuery(&query, &frame.End));
			D3D11_BUFFER_DESC staging;
			memset(&staging, 0, sizeof(staging));
			staging.ByteWidth = TILES * TILE_COUNTS * sizeof(unsigned);
			staging.Usage = D3D11_USAGE_STAGING;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			made = made && SUCCEEDED(device->CreateBuffer(&staging, NULL, &frame.Readback));
		}
	}
	if (made && (Texture == NULL || Width != width || Height != height)) {
		Release_Sized();
		D3D11_TEXTURE2D_DESC description;
		memset(&description, 0, sizeof(description));
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		made = SUCCEEDED(device->CreateTexture2D(&description, NULL, &Texture))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Texture, NULL, &Write))
			&& SUCCEEDED(device->CreateShaderResourceView(Texture, NULL, &Read));
		// A bit a slot for each two by two quad: the lights a ray found something in front of.
		description.Width = (width + 1) / 2;
		description.Height = (height + 1) / 2;
		description.Format = DXGI_FORMAT_R32_UINT;
		made = made && SUCCEEDED(device->CreateTexture2D(&description, NULL, &Blocked))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Blocked, NULL, &BlockedWrite))
			&& SUCCEEDED(device->CreateShaderResourceView(Blocked, NULL, &BlockedRead));
		Width = width;
		Height = height;
	}
	if (!made) {
		Release();
		Refused = true;
	}
	return made;
}

/* Every issued frame whose queries are back, oldest first, so the newest is what is kept. */
void DX11RayLightClass::Collect(ID3D11DeviceContext * context)
{
	const UINT later = D3D11_ASYNC_GETDATA_DONOTFLUSH;
	for (unsigned step = 1; step <= QUERY_FRAMES; ++step) {
		Frame & frame = Frames[(FrameIndex + step) % QUERY_FRAMES];
		if (!frame.Issued) {
			continue;
		}
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
		UINT64 start = 0, end = 0;
		if (context->GetData(frame.Disjoint, &disjoint, sizeof(disjoint), later) != S_OK
				|| context->GetData(frame.Start, &start, sizeof(start), later) != S_OK
				|| context->GetData(frame.End, &end, sizeof(end), later) != S_OK) {
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
			CountsTilesX = frame.TilesX;
			CountsTilesY = frame.TilesY;
		}
		frame.Issued = false;
		LastLights = frame.Lights;
		if (!disjoint.Disjoint && disjoint.Frequency > 0) {
			LastMS = static_cast<float>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(disjoint.Frequency));
			Timed = true;
		}
	}
}

std::string DX11RayLightClass::Report(ID3D11DeviceContext * context)
{
	if (Refused) {
		return "; lights refused by the device";
	}
	if (!Timed || context == NULL) {
		return "";
	}
	Collect(context);
	char line[128];
	snprintf(line, sizeof(line), "; lights ray traced, %u in the last, %.3f ms", LastLights, LastMS);
	return line;
}

std::string DX11RayLightClass::Probe_Report(ID3D11DeviceContext * context)
{
	if (context == NULL) {
		return "";
	}
	Collect(context);
	if (!CountsRead || LastLights == 0) {
		return "";
	}
	unsigned totals[TILE_COUNTS] = { 0, 0, 0, 0 };
	std::string tiles;
	char line[256];
	for (unsigned tile = 0; tile < CountsTilesX * CountsTilesY; ++tile) {
		const unsigned * count = &Counts[tile * TILE_COUNTS];
		for (unsigned field = 0; field < TILE_COUNTS; ++field) {
			totals[field] += count[field];
		}
		// Hundredths of the tile's pixels lit, then hundredths of those a ray took light from.
		const unsigned pixels = TILE_WIDTH * TILE_HEIGHT;
		snprintf(line, sizeof(line), "%s%u/%u", (tile % CountsTilesX == 0) ? (tile == 0 ? "" : " | ") : " ",
			(count[0] * 100u) / pixels, count[0] ? (count[1] * 100u) / count[0] : 0u);
		tiles += line;
	}
	snprintf(line, sizeof(line),
		"%u lights, %.3f ms, %u pixels lit, %u of them darkened by a ray, %u rays, %u stopped; "
		"tiles %ux%u of %ux%u px, lit/darkened in hundredths, rows top down: ",
		LastLights, LastMS, totals[0], totals[1], totals[2], totals[3], CountsTilesX, CountsTilesY,
		static_cast<unsigned>(TILE_WIDTH), static_cast<unsigned>(TILE_HEIGHT));
	return std::string(line) + tiles;
}

/* The receivers of the opaque scene take no point lights while this frame's rays will light them:
	 the sun's rays are coming (Ray_Shade_Pending), the pass has not been refused and there is a light. */
bool DX11BackendClass::Ray_Lights_Pending() const
{
	return RayShadowsWanted && RayLights.Wanted() && BlastLightCount > 0 && Ray_Shade_Pending()
		&& Device != NULL && Device->Get_Width() <= MAX_SIDE && Device->Get_Height() <= MAX_SIDE;
}

/* Once the opaque world but the batched trees is down and before the sun's rays shade it, so the
	 sun's dark pixel guard reads the lit scene as it read the programs' output.  True when it bound targets of its own; the
	 caller binds the scene again. */
bool DX11BackendClass::Trace_Ray_Lights()
{
	if (!Ray_Lights_Pending() || Device == NULL || DepthResolveVertexShader == NULL || RayApplyBlend == NULL) {
		return false;
	}
	ID3D11DeviceContext * context = Device->Get_Context();
	const unsigned width = Device->Get_Width();
	const unsigned height = Device->Get_Height();
	float world_from_clip[16];
	if (!Scene_World_From_Clip(world_from_clip) || !RayLights.Make(*this, Device->Get_Device(), width, height)) {
		return false;
	}
	Resolve_Scene_Depth();
	ID3D11ShaderResourceView * depth = Device->Get_Depth_Texture();
	if (depth == NULL) {
		return false;
	}
	RayLights.Collect(context);
	DX11RayLightClass::Frame & frame = RayLights.Frames[RayLights.FrameIndex];
	const bool timing = !frame.Issued;
	if (timing) {
		context->Begin(frame.Disjoint);
		context->End(frame.Start);
	}

	const unsigned tiles_x = (width + DX11RayLightClass::TILE_WIDTH - 1) / DX11RayLightClass::TILE_WIDTH;
	const unsigned tiles_y = (height + DX11RayLightClass::TILE_HEIGHT - 1) / DX11RayLightClass::TILE_HEIGHT;
	const bool probing = RayLights.Probing() && tiles_x * tiles_y <= DX11RayLightClass::TILES;
	DX11RayLightClass::Constants block;
	memset(&block, 0, sizeof(block));
	memcpy(block.WorldFromClip, world_from_clip, sizeof(block.WorldFromClip));
	block.Screen[0] = static_cast<float>(width);
	block.Screen[1] = static_cast<float>(height);
	block.Screen[2] = 1.0f / static_cast<float>(width);
	block.Screen[3] = 1.0f / static_cast<float>(height);
	block.Tuning[0] = static_cast<float>(BlastLightCount);
	block.Tuning[1] = probing ? 1.0f : 0.0f;
	block.Tuning[2] = static_cast<float>(tiles_x);
	block.Tuning[3] = static_cast<float>(tiles_y);
	for (unsigned slot = 0; slot < BlastLightCount; ++slot) {
		memcpy(block.Position[slot], &BlastLights[slot][0], sizeof(float) * 4);
		memcpy(block.Diffuse[slot], &BlastLights[slot][4], sizeof(float) * 4);
		memcpy(block.Ambient[slot], &BlastLights[slot][8], sizeof(float) * 4);
	}
	context->UpdateSubresource(RayLights.ConstantBuffer, 0, NULL, &block, 0, 0);
	const UINT no_jobs[4] = { 0, 0, 1, 1 };
	context->UpdateSubresource(RayLights.Arguments, 0, NULL, no_jobs, 0, 0);
	const UINT zero[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(RayLights.BlockedWrite, zero);
	if (probing) {
		context->ClearUnorderedAccessViewUint(RayLights.HitsWrite, zero);
	}

	// The depth is read, so it cannot stay bound as the depth the frame tests against.
	context->OMSetRenderTargets(0, NULL, NULL);
	ID3D11UnorderedAccessView * const no_writes[2] = { NULL, NULL };
	ID3D11ShaderResourceView * const no_reads[3] = { NULL, NULL, NULL };
	context->CSSetConstantBuffers(0, 1, &RayLights.ConstantBuffer);
	context->CSSetShaderResources(0, 1, &depth);

	ID3D11UnorderedAccessView * queue_writes[2] = { RayLights.JobsWrite, RayLights.ArgumentsWrite };
	context->CSSetUnorderedAccessViews(0, 2, queue_writes, NULL);
	context->CSSetShader(RayLights.Queue, NULL, 0);
	context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
	context->CSSetUnorderedAccessViews(0, 2, no_writes, NULL);

	const bool ground = TerrainHeightsView != NULL && TerrainLight[33] > 0.0f;
	const float ground_numbers[4] = { TerrainLight[33], TerrainLight[34], TerrainLight[35],
		static_cast<float>(TerrainHeightsHighest) * TerrainLight[35] };
	RayGrid.Bind(context, ground ? TerrainHeightsView : NULL, ground_numbers);
	ID3D11ShaderResourceView * trace_reads[2] = { RayLights.JobsRead, RayLights.ArgumentsRead };
	ID3D11UnorderedAccessView * trace_writes[2] = { RayLights.BlockedWrite, RayLights.HitsWrite };
	context->CSSetShaderResources(1, 2, trace_reads);
	context->CSSetUnorderedAccessViews(0, 2, trace_writes, NULL);
	context->CSSetShader(RayLights.Trace, NULL, 0);
	context->DispatchIndirect(RayLights.Arguments, sizeof(UINT));
	context->CSSetUnorderedAccessViews(0, 2, no_writes, NULL);
	context->CSSetShaderResources(1, 2, no_reads);
	RayGrid.Unbind(context);

	ID3D11UnorderedAccessView * shade_writes[2] = { RayLights.Write, RayLights.HitsWrite };
	context->CSSetShaderResources(1, 1, &RayLights.BlockedRead);
	context->CSSetUnorderedAccessViews(0, 2, shade_writes, NULL);
	context->CSSetShader(RayLights.Shade, NULL, 0);
	context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
	ID3D11Buffer * no_buffer = NULL;
	context->CSSetUnorderedAccessViews(0, 2, no_writes, NULL);
	context->CSSetShaderResources(0, 2, no_reads);
	context->CSSetConstantBuffers(0, 1, &no_buffer);
	context->CSSetShader(NULL, NULL, 0);

	ID3D11RenderTargetView * scene = Device->Get_Scene_View();
	context->OMSetRenderTargets(1, &scene, NULL);
	D3D11_VIEWPORT viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(width);
	viewport.Height = static_cast<float>(height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);
	context->OMSetDepthStencilState(RayApplyDepth, 0);
	context->RSSetState(RayApplyRasterizer);
	context->IASetInputLayout(NULL);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(DepthResolveVertexShader, NULL, 0);
	context->PSSetShaderResources(0, 1, &RayLights.Read);
	context->OMSetBlendState(RayLights.LightBlend, NULL, 0xffffffff);
	context->PSSetShader(RayLights.Multiply, NULL, 0);
	context->Draw(3, 0);
	context->OMSetBlendState(RayLights.ClampBlend, NULL, 0xffffffff);
	context->PSSetShader(RayLights.Clamp, NULL, 0);
	context->Draw(3, 0);
	context->PSSetShaderResources(0, 1, no_reads);

	if (timing) {
		context->End(frame.End);
		context->End(frame.Disjoint);
		if (probing) {
			context->CopyResource(frame.Readback, RayLights.Hits);
		}
		frame.Issued = true;
		frame.Probed = probing;
		frame.Lights = BlastLightCount;
		frame.TilesX = tiles_x;
		frame.TilesY = tiles_y;
		RayLights.FrameIndex = (RayLights.FrameIndex + 1) % DX11RayLightClass::QUERY_FRAMES;
	}
	Forget_Bindings();
	return true;
}
