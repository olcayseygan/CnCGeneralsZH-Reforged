/*
** The 3D grid behind any-direction rays at the Ray Traced shadow level (dx11raygrid.h has what it
** is for).  Six steps a frame, all over buffers the backend's caster pass already filled:
**
**   length   one thread: how many slots this pass wrote, as the groups of the dispatches after it
**   bounds   the stamped triangles' box in the sun's clip space, clamped to the sun's box
**   shape    one thread: the cells, near cubic in world units, as many as the budget allows
**   count    each triangle adds one to every cell its plane crosses inside its box
**   scan     the counts to starts, a block of 1024 cells a group and then the blocks
**   fill     the same walk as the count, taking a slot off each cell's count as it writes
**
** Count and fill are one program with a pass constant, so the cells a triangle lands in are the
** same float sums both times: two programs compiled apart can round a plane test differently, and a
** cell filled but not counted writes into its neighbour's list.  The fill leaves every count at
** nought, so a cell's list runs from its start to the next cell's.
**
** The walk is Amanatides and Woo's over the cells in grid units, the ray clipped to the grid's box
** first, each cell's triangles tested with Moller and Trumbore's test in the sun's clip space, any hit
** within the reach ending it.  A triangle listed in a cell the ray passes is a hit wherever along the
** ray it is struck, which is all an any-hit ray asks.  The ground is the backend's heightfield march
** taken off the sun's direction: strides of up to eight units, sixty-four at most, the ray's own
** cell skipped and a cell's twist given back, as the sun's trace does.
**
** ponytail: a cell's list is every triangle whose plane crosses it inside the triangle's box, not an
** exact triangle and box test, so a long thin diagonal triangle is listed in cells it misses; and a
** triangle the size of the map walks its whole box in one thread.  The upgrade for either is a
** binned BVH; nothing the game streams has needed it yet.  Cut casters (leaves, fences, the
** foliage) are streamed beside the solid ones with their stamp negated, their texture coordinates
** and a slice of the backend's cut alpha in a second buffer, and a hit on one counts only where the
** texel at the hit passes its alpha reference: nearest texel of a 512 square copy, the texture's
** alpha alone (not its vertex colour's), which is the sun map's cut to within a texel.
**
** Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
*/

#include "dx11raygrid.h"
#include "dx11backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Cells the grid may hold, a power of two from 2^20, and the references over all of them.  The
// starts of the cell after the last used one are read, so the shape keeps a block of 1024 spare.
// Twilight Flame's drawlooks view, 1280x720 on an RTX 2060, 2026-11-23, build against two hemisphere
// rays a pixel (40 and 150 units): 2^20 cells 0.37 against 6.29 ms, 2^21 0.59 against 5.15, 2^22
// 1.03 against 4.50.  The scan and the fill grow with the cells, the walk shrinks.
static const unsigned GRID_CELLS = 1u << 21;
static const unsigned GRID_BLOCKS = GRID_CELLS / 1024;
static const unsigned GRID_BUDGET = GRID_CELLS - 1024;
static const unsigned GRID_REFERENCE_SLOTS = 1u << 23;

static const char RG_COMMON[] =
	"cbuffer RayGrid : register(b1)\n"
	"{\n"
	"    row_major float4x4 RgSunFromWorld;\n"
	"    row_major float4x4 RgWorldFromClip;\n"	// the probe's
	"    float4 RgScreen;\n"						// the probe's: width, height and their reciprocals
	"    float4 RgSun;\n"							// the probe's: the way to the sun, world units a unit of sun depth
	"    float4 RgGround;\n"						// height cells per world unit, border cells, world units a height byte, highest ground
	"    float4 RgTuning;\n"						// ground held, stamp, triangle slots, reference slots
	"    float4 RgBuild;\n"						// world units a unit of sun clip x, y and z; cell budget
	"    float4 RgProbe;\n"						// normal offset, depth bias, seed, the 2D grid's reference slots
	"    float4 RgKeepLow;\n"						// the box the grid keeps to in the sun's clip space (Set_Keep)
	"    float4 RgKeepHigh;\n"
	"};\n"
	// Low corner in sun clip space and cells used; cells per sun clip unit and a cell's world size;
	// cells along x, y, z and one when anything was binned.
	"Buffer<float4> RgShape : register(t20);\n";

static const char RG_TRACE[] =
	// A caller may define RG_LIST_CAP before this text to test at most that many triangles a cell.
	"#ifndef RG_LIST_CAP\n#define RG_LIST_CAP 0x7fffffff\n#endif\n"
	"Buffer<float4> RgCorners : register(t16);\n"
	"Buffer<uint> RgStarts : register(t17);\n"
	"Buffer<uint> RgBlocks : register(t18);\n"
	"Buffer<uint> RgItems : register(t19);\n"
	"Texture2D<float> RgHeights : register(t21);\n"
	"Buffer<float4> RgCutCoordinates : register(t22);\n"
	"Texture2DArray<float> RgCutAlpha : register(t23);\n"
	"\n"
	"uint rg_cell_start(uint cell) { return RgStarts[cell] + RgBlocks[cell >> 10]; }\n"
	"\n"
	// A cut caster's texel at the hit's weights on its second and third corners, against its reference.
	"bool rg_cut_passes(uint corner, float u, float v)\n"
	"{\n"
	"    float4 first = RgCutCoordinates[corner];\n"
	"    float2 at = first.xy * (1.0 - u - v) + RgCutCoordinates[corner + 1].xy * u + RgCutCoordinates[corner + 2].xy * v;\n"
	"    uint width, height, slices;\n"
	"    RgCutAlpha.GetDimensions(width, height, slices);\n"
	"    int2 texel = min(int2(frac(at) * float2(width, height)), int2(width, height) - 1);\n"
	"    return RgCutAlpha.Load(int4(texel, (int)first.z, 0)) >= first.w;\n"
	"}\n"
	"\n"
	"bool rg_triangle_hit(uint item, float3 o, float3 d, float reach)\n"
	"{\n"
	"    uint corner = item * 3;\n"
	"    float4 a = RgCorners[corner];\n"
	"    if (abs(a.w) != RgTuning.y) return false;\n"	// a slot an older pass left, listed by a cell its count missed
	"    float3 e1 = RgCorners[corner + 1].xyz - a.xyz;\n"
	"    float3 e2 = RgCorners[corner + 2].xyz - a.xyz;\n"
	"    float3 p = cross(d, e2);\n"
	"    float det = dot(e1, p);\n"
	"    if (det == 0.0) return false;\n"
	"    float inv = 1.0 / det;\n"
	"    float3 s = o - a.xyz;\n"
	"    float u = dot(s, p) * inv;\n"
	"    if (u < 0.0 || u > 1.0) return false;\n"
	"    float3 q = cross(s, e1);\n"
	"    float v = dot(d, q) * inv;\n"
	"    if (v < 0.0 || u + v > 1.0) return false;\n"
	"    float at = dot(e2, q) * inv;\n"
	"    if (at <= 0.0 || at > reach) return false;\n"
	"    if (a.w > 0.0) return true;\n"	// a solid caster; a cut one's stamp is negated
	"    return rg_cut_passes(corner, u, v);\n"
	"}\n"
	"\n"
	"// A world ray against the solid casters: origin, unit direction, reach in world units.\n"
	"bool ray_hits_casters(float3 origin, float3 direction, float reach)\n"
	"{\n"
	"    float4 shape = RgShape[2];\n"
	"    if (shape.w < 0.5) return false;\n"
	"    float3 low = RgShape[0].xyz;\n"
	"    float3 scale = RgShape[1].xyz;\n"
	"    int3 dims = int3(shape.xyz);\n"
	"    float3 o = mul(float4(origin, 1.0), RgSunFromWorld).xyz;\n"
	"    float3 d = mul(float4(direction, 0.0), RgSunFromWorld).xyz;\n"
	"    float3 go = (o - low) * scale;\n"
	"    float3 gd = d * scale;\n"
	"    gd = (abs(gd) < 1e-20) ? ((gd >= 0.0) ? 1e-20 : -1e-20) : gd;\n"
	"    float3 inv = 1.0 / gd;\n"
	"    float3 to_low = -go * inv;\n"
	"    float3 to_high = (float3(dims) - go) * inv;\n"
	"    float3 lo3 = min(to_low, to_high);\n"
	"    float3 hi3 = max(to_low, to_high);\n"
	"    float enter = max(max(lo3.x, lo3.y), max(lo3.z, 0.0));\n"
	"    float leave = min(min(hi3.x, hi3.y), min(hi3.z, reach));\n"
	"    if (enter > leave) return false;\n"
	"    int3 cell = clamp(int3(floor(go + gd * enter)), int3(0, 0, 0), dims - 1);\n"
	"    int3 step = (gd > 0.0) ? int3(1, 1, 1) : int3(-1, -1, -1);\n"
	"    float3 delta = abs(inv);\n"
	"    float3 next = (float3(cell + ((gd > 0.0) ? int3(1, 1, 1) : int3(0, 0, 0))) - go) * inv;\n"
	"    int walk = dims.x + dims.y + dims.z;\n"
	"    [loop] for (int k = 0; k < walk; ++k) {\n"
	"        uint index = uint((cell.z * dims.y + cell.y) * dims.x + cell.x);\n"
	"        uint first = rg_cell_start(index);\n"
	"        uint last = min(min(rg_cell_start(index + 1), (uint)RgTuning.w), first + RG_LIST_CAP);\n"
	"        [loop] for (uint i = first; i < last; ++i)\n"
	"            if (rg_triangle_hit(RgItems[i], o, d, reach)) return true;\n"
	"        if (next.x <= next.y && next.x <= next.z) {\n"
	"            if (next.x > leave) break;\n"
	"            cell.x += step.x; next.x += delta.x;\n"
	"            if (cell.x < 0 || cell.x >= dims.x) break;\n"
	"        } else if (next.y <= next.z) {\n"
	"            if (next.y > leave) break;\n"
	"            cell.y += step.y; next.y += delta.y;\n"
	"            if (cell.y < 0 || cell.y >= dims.y) break;\n"
	"        } else {\n"
	"            if (next.z > leave) break;\n"
	"            cell.z += step.z; next.z += delta.z;\n"
	"            if (cell.z < 0 || cell.z >= dims.z) break;\n"
	"        }\n"
	"    }\n"
	"    return false;\n"
	"}\n"
	"\n"
	"// The ground as the sun's trace takes it: a bilinear patch over the cell's corners, a quarter of\n"
	"// the cell's twist given back for the two triangles it is drawn as.\n"
	"float rg_ground_at(float2 xy, out float twist, out int2 at)\n"
	"{\n"
	"    uint width, height;\n"
	"    RgHeights.GetDimensions(width, height);\n"
	"    int2 last = int2(width, height) - 1;\n"
	"    float2 grid = xy * RgGround.x + RgGround.y;\n"
	"    float2 cell = floor(grid);\n"
	"    float2 t = grid - cell;\n"
	"    at = int2(cell);\n"
	"    float h00 = RgHeights.Load(int3(clamp(at, int2(0, 0), last), 0));\n"
	"    float h10 = RgHeights.Load(int3(clamp(at + int2(1, 0), int2(0, 0), last), 0));\n"
	"    float h01 = RgHeights.Load(int3(clamp(at + int2(0, 1), int2(0, 0), last), 0));\n"
	"    float h11 = RgHeights.Load(int3(clamp(at + int2(1, 1), int2(0, 0), last), 0));\n"
	"    float scale = 255.0 * RgGround.z;\n"
	"    twist = abs(h00 + h11 - h10 - h01) * 0.25 * scale;\n"
	"    return lerp(lerp(h00, h10, t.x), lerp(h01, h11, t.x), t.y) * scale;\n"
	"}\n"
	"\n"
	"// The same ray against the heightfield, marched; the cell the ray leaves is its own surface.\n"
	"bool ray_hits_ground(float3 origin, float3 direction, float reach)\n"
	"{\n"
	"    if (RgTuning.x < 0.5) return false;\n"
	"    float twist;\n"
	"    int2 own;\n"
	"    rg_ground_at(origin.xy, twist, own);\n"
	"    float stride = clamp(reach / 16.0, 1.0, 8.0);\n"
	"    int steps = min((int)ceil(reach / stride), 64);\n"
	"    [loop] for (int k = 1; k <= steps; ++k) {\n"
	"        float3 at = origin + direction * min(stride * k, reach);\n"
	"        if (at.z > RgGround.w && direction.z >= 0.0) break;\n"
	"        int2 cell;\n"
	"        float height = rg_ground_at(at.xy, twist, cell);\n"
	"        if (all(cell == own)) continue;\n"
	"        if (height > at.z + 1.0 + twist) return true;\n"
	"    }\n"
	"    return false;\n"
	"}\n"
	"\n"
	"bool ray_occluded(float3 origin, float3 direction, float reach)\n"
	"{\n"
	// Shader model 5 evaluates both sides of || and &&, so the ground is marched only past a miss here.
	"    if (ray_hits_casters(origin, direction, reach)) return true;\n"
	"    return ray_hits_ground(origin, direction, reach);\n"
	"}\n";

// The pass's triangles are the slots from nought up to the first that holds another pass's stamp,
// since stream out writes from the start of the buffer and every pass has a stamp of its own: a
// binary search finds the end in nineteen reads, and the per-triangle dispatches run that many
// threads instead of every slot.
static const char RG_LENGTH_PROGRAM[] =
	"Buffer<float4> RgCorners : register(t16);\n"
	"RWBuffer<uint> RgArgumentsW : register(u0);\n"
	"[numthreads(1, 1, 1)]\n"
	"void main()\n"
	"{\n"
	"    uint low = 0;\n"
	"    uint high = (uint)RgTuning.z;\n"
	"    [loop] while (low < high) {\n"
	"        uint middle = (low + high) / 2;\n"
	"        if (abs(RgCorners[middle * 3].w) == RgTuning.y) low = middle + 1;\n"
	"        else high = middle;\n"
	"    }\n"
	"    RgArgumentsW[0] = (low + 63) / 64;\n"
	"    RgArgumentsW[1] = 1;\n"
	"    RgArgumentsW[2] = 1;\n"
	"    RgArgumentsW[3] = low;\n"
	"}\n";

static const char RG_BOUNDS_PROGRAM[] =
	"Buffer<float4> RgCorners : register(t16);\n"
	"RWBuffer<uint> RgBoundsW : register(u0);\n"
	"groupshared uint RgGroupBounds[6];\n"
	"// A float's order as a uint, so InterlockedMin finds the smallest; the largest is the smallest negated.\n"
	"uint rg_order(float f) { uint u = asuint(f); return (u & 0x80000000u) != 0 ? ~u : (u | 0x80000000u); }\n"
	"[numthreads(64, 1, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)\n"
	"{\n"
	"    if (index < 6) RgGroupBounds[index] = 0xffffffffu;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    if (id.x < (uint)RgTuning.z) {\n"
	"        float4 a = RgCorners[id.x * 3];\n"
	"        float4 b = RgCorners[id.x * 3 + 1];\n"
	"        float4 c = RgCorners[id.x * 3 + 2];\n"
	"        float3 low = min(a.xyz, min(b.xyz, c.xyz));\n"
	"        float3 high = max(a.xyz, max(b.xyz, c.xyz));\n"
	"        bool valid = abs(a.w) == RgTuning.y && abs(b.w) == RgTuning.y && abs(c.w) == RgTuning.y\n"
	"            && all(high >= RgKeepLow.xyz) && all(low <= RgKeepHigh.xyz)\n"
	"            && !any(isnan(low)) && !any(isnan(high));\n"
	"        if (valid) {\n"
	"            low = max(low, RgKeepLow.xyz);\n"
	"            high = min(high, RgKeepHigh.xyz);\n"
	"            InterlockedMin(RgGroupBounds[0], rg_order(low.x));\n"
	"            InterlockedMin(RgGroupBounds[1], rg_order(low.y));\n"
	"            InterlockedMin(RgGroupBounds[2], rg_order(low.z));\n"
	"            InterlockedMin(RgGroupBounds[3], rg_order(-high.x));\n"
	"            InterlockedMin(RgGroupBounds[4], rg_order(-high.y));\n"
	"            InterlockedMin(RgGroupBounds[5], rg_order(-high.z));\n"
	"        }\n"
	"    }\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    if (index < 6 && RgGroupBounds[index] != 0xffffffffu) InterlockedMin(RgBoundsW[index], RgGroupBounds[index]);\n"
	"}\n";

static const char RG_SHAPE_PROGRAM[] =
	"Buffer<uint> RgBounds : register(t0);\n"
	"RWBuffer<float4> RgShapeW : register(u0);\n"
	"float rg_unorder(uint u) { return asfloat((u & 0x80000000u) != 0 ? (u & 0x7fffffffu) : ~u); }\n"
	"[numthreads(1, 1, 1)]\n"
	"void main()\n"
	"{\n"
	"    if (RgBounds[0] == 0xffffffffu) {\n"
	"        RgShapeW[0] = float4(0.0, 0.0, 0.0, 0.0);\n"
	"        RgShapeW[1] = float4(0.0, 0.0, 0.0, 0.0);\n"
	"        RgShapeW[2] = float4(1.0, 1.0, 1.0, 0.0);\n"
	"        return;\n"
	"    }\n"
	"    float3 low = float3(rg_unorder(RgBounds[0]), rg_unorder(RgBounds[1]), rg_unorder(RgBounds[2]));\n"
	"    float3 high = -float3(rg_unorder(RgBounds[3]), rg_unorder(RgBounds[4]), rg_unorder(RgBounds[5]));\n"
	"    float3 pad = (high - low) * 1e-4 + 1e-6;\n"
	"    low -= pad;\n"
	"    high += pad;\n"
	"    float3 extent = max((high - low) * RgBuild.xyz, 1e-3);\n"
	"    float budget = RgBuild.w;\n"
	"    float cell = pow(extent.x * extent.y * extent.z / budget, 1.0 / 3.0);\n"
	"    float3 dims = clamp(floor(extent / cell), 1.0, 1024.0);\n"
	"    // An axis held at one cell leaves the other two too many: widen until the product fits.\n"
	"    [loop] for (int k = 0; k < 64 && dims.x * dims.y * dims.z > budget; ++k) {\n"
	"        cell *= 1.1;\n"
	"        dims = clamp(floor(extent / cell), 1.0, 1024.0);\n"
	"    }\n"
	"    RgShapeW[0] = float4(low, dims.x * dims.y * dims.z);\n"
	"    RgShapeW[1] = float4(dims / (high - low), cell);\n"
	"    RgShapeW[2] = float4(dims, 1.0);\n"
	"}\n";

static const char RG_BIN_PROGRAM[] =
	"Buffer<float4> RgCorners : register(t16);\n"
	"Buffer<uint> RgStarts : register(t17);\n"
	"Buffer<uint> RgBlocks : register(t18);\n"
	"RWBuffer<uint> RgCountsW : register(u0);\n"
	"RWBuffer<uint> RgItemsW : register(u1);\n"
	"cbuffer RayGridPass : register(b2) { float4 RgPass; };\n"	// x: nought counts, one fills
	"[numthreads(64, 1, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID)\n"
	"{\n"
	"    float4 shape = RgShape[2];\n"
	"    if (id.x >= (uint)RgTuning.z || shape.w < 0.5) return;\n"
	"    float4 a = RgCorners[id.x * 3];\n"
	"    float4 b = RgCorners[id.x * 3 + 1];\n"
	"    float4 c = RgCorners[id.x * 3 + 2];\n"
	"    if (abs(a.w) != RgTuning.y || abs(b.w) != RgTuning.y || abs(c.w) != RgTuning.y) return;\n"
	"    float3 low = RgShape[0].xyz;\n"
	"    float3 scale = RgShape[1].xyz;\n"
	"    int3 dims = int3(shape.xyz);\n"
	"    float3 ga = (a.xyz - low) * scale;\n"
	"    float3 gb = (b.xyz - low) * scale;\n"
	"    float3 gc = (c.xyz - low) * scale;\n"
	"    float3 lowest = min(ga, min(gb, gc));\n"
	"    float3 highest = max(ga, max(gb, gc));\n"
	"    if (any(highest < 0.0) || any(lowest >= float3(dims))) return;\n"
	"    // The plane's reach over a cell's half diagonal, with a little to spare for the rounding.\n"
	"    float3 n = cross(gb - ga, gc - ga);\n"
	"    float reach = 0.5 * (abs(n.x) + abs(n.y) + abs(n.z)) * 1.001;\n"
	"    if (!(reach > 0.0)) return;\n"
	"    int3 first = clamp(int3(floor(lowest)), int3(0, 0, 0), dims - 1);\n"
	"    int3 last = clamp(int3(floor(highest)), int3(0, 0, 0), dims - 1);\n"
	"    bool filling = RgPass.x > 0.5;\n"
	"    [loop] for (int z = first.z; z <= last.z; ++z) {\n"
	"        [loop] for (int y = first.y; y <= last.y; ++y) {\n"
	"            [loop] for (int x = first.x; x <= last.x; ++x) {\n"
	"                if (abs(dot(n, float3(x, y, z) + 0.5 - ga)) > reach) continue;\n"
	"                uint cell = uint((z * dims.y + y) * dims.x + x);\n"
	"                if (!filling) {\n"
	"                    InterlockedAdd(RgCountsW[cell], 1);\n"
	"                    continue;\n"
	"                }\n"
	"                uint before;\n"
	"                InterlockedAdd(RgCountsW[cell], 0xffffffffu, before);\n"
	"                if (before == 0 || before > 0x7fffffffu) continue;\n"	// a cell the count missed, once or again
	"                uint slot = RgStarts[cell] + RgBlocks[cell >> 10] + before - 1;\n"
	"                if (slot < (uint)RgTuning.w) RgItemsW[slot] = id.x;\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"}\n";

// The backend's two step prefix sum, here over GRID_CELLS: each block of 1024 cells its own starts
// and total, then one group of 1024 threads the blocks, GRID_BLOCKS / 1024 each.
static const char RG_SCAN_PROGRAM[] =
	"RWBuffer<uint> RgCountsW : register(u0);\n"
	"RWBuffer<uint> RgStartsW : register(u1);\n"
	"RWBuffer<uint> RgBlocksW : register(u2);\n"
	"groupshared uint RgSums[1024];\n"
	"[numthreads(1024, 1, 1)]\n"
	"void main(uint3 group : SV_GroupID, uint3 id : SV_GroupThreadID)\n"
	"{\n"
	"    uint cell = group.x * 1024 + id.x;\n"
	"    uint count = RgCountsW[cell];\n"
	"    RgSums[id.x] = count;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    for (uint step = 1; step < 1024; step <<= 1) {\n"
	"        uint before = (id.x >= step) ? RgSums[id.x - step] : 0;\n"
	"        GroupMemoryBarrierWithGroupSync();\n"
	"        RgSums[id.x] += before;\n"
	"        GroupMemoryBarrierWithGroupSync();\n"
	"    }\n"
	"    RgStartsW[cell] = RgSums[id.x] - count;\n"
	"    if (id.x == 1023) RgBlocksW[group.x] = RgSums[1023];\n"
	"}\n";

static const char RG_BLOCK_PROGRAM[] =
	"RWBuffer<uint> RgBlocksW : register(u0);\n"
	"groupshared uint RgSums[1024];\n"
	"[numthreads(1024, 1, 1)]\n"
	"void main(uint3 id : SV_GroupThreadID)\n"
	"{\n"
	"    uint own[PER];\n"
	"    uint total = 0;\n"
	"    [unroll] for (uint k = 0; k < PER; ++k) { own[k] = RgBlocksW[id.x * PER + k]; total += own[k]; }\n"
	"    RgSums[id.x] = total;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    for (uint step = 1; step < 1024; step <<= 1) {\n"
	"        uint before = (id.x >= step) ? RgSums[id.x - step] : 0;\n"
	"        GroupMemoryBarrierWithGroupSync();\n"
	"        RgSums[id.x] += before;\n"
	"        GroupMemoryBarrierWithGroupSync();\n"
	"    }\n"
	"    uint start = RgSums[id.x] - total;\n"
	"    [unroll] for (uint j = 0; j < PER; ++j) { RgBlocksW[id.x * PER + j] = start; start += own[j]; }\n"
	"    if (id.x == 1023) RgBlocksW[PER * 1024] = RgSums[1023];\n"
	"}\n";

// The probe's half: a pixel back to the world through the frame's camera and its surface's normal,
// the way the sun's trace takes them, and the counts one group adds at a time.
static const char RG_PROBE_COMMON[] =
	"Texture2D<float> RgDepth : register(t0);\n"
	"RWBuffer<uint> RgHits : register(u0);\n"
	"#define PROBE_COUNTS 13\n"
	"groupshared uint RgGroupHits[PROBE_COUNTS];\n"
	"float3 rg_world_at(int2 pixel)\n"
	"{\n"
	"    pixel = clamp(pixel, int2(0, 0), int2(RgScreen.xy) - 1);\n"
	"    float depth = RgDepth.Load(int3(pixel, 0));\n"
	"    float2 ndc = float2(pixel.x * RgScreen.z * 2.0 - 1.0, 1.0 - pixel.y * RgScreen.w * 2.0);\n"
	"    float4 world = mul(float4(ndc, depth, 1.0), RgWorldFromClip);\n"
	"    return world.xyz / world.w;\n"
	"}\n"
	"float3 rg_shorter(float3 here, float3 ahead, float3 behind)\n"
	"{\n"
	"    float3 forward = ahead - here;\n"
	"    float3 backward = here - behind;\n"
	"    return dot(forward, forward) < dot(backward, backward) ? forward : backward;\n"
	"}\n"
	"float3 rg_normal_at(int2 pixel, float3 here)\n"
	"{\n"
	"    float3 across = rg_shorter(here, rg_world_at(pixel + int2(1, 0)), rg_world_at(pixel - int2(1, 0)));\n"
	"    float3 down = rg_shorter(here, rg_world_at(pixel + int2(0, 1)), rg_world_at(pixel - int2(0, 1)));\n"
	"    float3 normal = cross(across, down);\n"
	"    float size = length(normal);\n"
	"    return (size > 1e-6) ? normal / size : float3(0.0, 0.0, 1.0);\n"
	"}\n"
	"void rg_count(uint index) { InterlockedAdd(RgGroupHits[index], 1); }\n";

// Sun rays: the 2D grid's answer (one column, a triangle nearer the sun than the point by the bias),
// as the backend's trace gives it for a solid caster, against this grid's along the same ray.
static const char RG_PROBE_SUN_PROGRAM[] =
	"Buffer<float4> OldCorners : register(t1);\n"
	"Buffer<uint> OldStarts : register(t2);\n"
	"Buffer<uint> OldCounts : register(t3);\n"
	"Buffer<uint> OldItems : register(t4);\n"
	"Buffer<uint> OldBlocks : register(t5);\n"
	"bool old_grid_hit(float3 sun)\n"
	"{\n"
	"    int2 cell = clamp(int2(floor((sun.xy * 0.5 + 0.5) * 512)), 0, 511);\n"
	"    uint index = cell.y * 512 + cell.x;\n"
	"    uint first = OldStarts[index] + OldBlocks[index >> 10];\n"
	"    uint last = min(first + OldCounts[index], (uint)RgProbe.w);\n"
	"    float reach = sun.z - RgProbe.y / RgSun.w;\n"
	"    [loop] for (uint i = first; i < last; ++i) {\n"
	"        uint t = OldItems[i] * 3;\n"
	"        float3 a = OldCorners[t].xyz;\n"
	"        float3 b = OldCorners[t + 1].xyz;\n"
	"        float3 c = OldCorners[t + 2].xyz;\n"
	"        float2 ab = b.xy - a.xy;\n"
	"        float2 ac = c.xy - a.xy;\n"
	"        float2 ap = sun.xy - a.xy;\n"
	"        float area = ab.x * ac.y - ab.y * ac.x;\n"
	"        float u = (ap.x * ac.y - ap.y * ac.x) / area;\n"
	"        float v = (ab.x * ap.y - ab.y * ap.x) / area;\n"
	"        if (u < 0.0 || v < 0.0 || u + v > 1.0) continue;\n"
	"        if (a.z + u * (b.z - a.z) + v * (c.z - a.z) < reach) return true;\n"
	"    }\n"
	"    return false;\n"
	"}\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)\n"
	"{\n"
	"    if (index < PROBE_COUNTS) RgGroupHits[index] = 0;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    int2 pixel = int2(id.xy);\n"
	"    if (id.x < (uint)RgScreen.x && id.y < (uint)RgScreen.y && RgDepth.Load(int3(pixel, 0)) < 1.0) {\n"
	"        float3 here = rg_world_at(pixel);\n"
	"        float3 normal = rg_normal_at(pixel, here);\n"
	"        if (dot(normal, RgSun.xyz) < 0.0) normal = -normal;\n"
	"        float3 from = here + normal * RgProbe.x;\n"
	"        float3 sun = mul(float4(from, 1.0), RgSunFromWorld).xyz;\n"
	"        if (all(abs(sun.xy) <= 1.0) && sun.z >= 0.0 && sun.z <= 1.0) {\n"
	"            bool old_hit = old_grid_hit(sun);\n"
	"            bool new_hit = ray_hits_casters(from + RgSun.xyz * RgProbe.y, RgSun.xyz, 1e6);\n"
	"            rg_count(0);\n"
	"            if (old_hit) rg_count(1);\n"
	"            if (new_hit) rg_count(2);\n"
	"            if (old_hit && new_hit) rg_count(3);\n"
	"            if (old_hit && !new_hit) rg_count(4);\n"
	"            if (new_hit && !old_hit) rg_count(5);\n"
	"            if (!new_hit && ray_hits_ground(from, RgSun.xyz, 512.0)) rg_count(6);\n"
	"        }\n"
	"    }\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    if (index < PROBE_COUNTS && RgGroupHits[index] != 0) InterlockedAdd(RgHits[index], RgGroupHits[index]);\n"
	"}\n";

// Two random rays a pixel over the hemisphere the camera sees, cosine weighted: one of an ambient
// occlusion's reach and one of a point light's.
static const char RG_PROBE_HEMISPHERE_PROGRAM[] =
	"uint rg_hash(uint x)\n"
	"{\n"
	"    x = x * 747796405u + 2891336453u;\n"
	"    uint w = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;\n"
	"    return (w >> 22u) ^ w;\n"
	"}\n"
	"float3 rg_hemisphere(float3 normal, uint key)\n"
	"{\n"
	"    uint h1 = rg_hash(key);\n"
	"    uint h2 = rg_hash(h1);\n"
	"    float u1 = h1 * (1.0 / 4294967296.0);\n"
	"    float u2 = h2 * (1.0 / 4294967296.0);\n"
	"    float r = sqrt(u1);\n"
	"    float phi = 6.2831853 * u2;\n"
	"    float3 side = abs(normal.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);\n"
	"    float3 tangent = normalize(cross(side, normal));\n"
	"    float3 bitangent = cross(normal, tangent);\n"
	"    return normalize(tangent * (r * cos(phi)) + bitangent * (r * sin(phi)) + normal * sqrt(saturate(1.0 - u1)));\n"
	"}\n"
	"[numthreads(8, 8, 1)]\n"
	"void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)\n"
	"{\n"
	"    if (index < PROBE_COUNTS) RgGroupHits[index] = 0;\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    int2 pixel = int2(id.xy);\n"
	"    if (id.x < (uint)RgScreen.x && id.y < (uint)RgScreen.y && RgDepth.Load(int3(pixel, 0)) < 1.0) {\n"
	"        float3 here = rg_world_at(pixel);\n"
	"        float3 normal = rg_normal_at(pixel, here);\n"
	"        float2 ndc = float2(pixel.x * RgScreen.z * 2.0 - 1.0, 1.0 - pixel.y * RgScreen.w * 2.0);\n"
	"        float4 eye = mul(float4(ndc, 0.0, 1.0), RgWorldFromClip);\n"
	"        if (dot(normal, eye.xyz / eye.w - here) < 0.0) normal = -normal;\n"
	"        float3 from = here + normal * RgProbe.x;\n"
	"        uint key = (id.y * 8192u + id.x) * 2u + (uint)RgProbe.z * 0x9e3779b9u;\n"
	"        float3 near_way = rg_hemisphere(normal, key);\n"
	"        float3 far_way = rg_hemisphere(normal, key + 1u);\n"
	"        rg_count(7);\n"
	"        if (ray_hits_casters(from, near_way, 40.0)) rg_count(8);\n"
	"        else if (ray_hits_ground(from, near_way, 40.0)) rg_count(9);\n"
	"        rg_count(10);\n"
	"        if (ray_hits_casters(from, far_way, 150.0)) rg_count(11);\n"
	"        else if (ray_hits_ground(from, far_way, 150.0)) rg_count(12);\n"
	"    }\n"
	"    GroupMemoryBarrierWithGroupSync();\n"
	"    if (index < PROBE_COUNTS && RgGroupHits[index] != 0) InterlockedAdd(RgHits[index], RgGroupHits[index]);\n"
	"}\n";

enum { PROGRAM_BOUNDS, PROGRAM_SHAPE, PROGRAM_BIN, PROGRAM_SCAN, PROGRAM_BLOCK, PROGRAM_PROBE_SUN,
	PROGRAM_PROBE_HEMISPHERE, PROGRAM_LENGTH };

// The readback's layout: the shape's three float4, the references, the triangles, then the probe's
// counts.
static const unsigned READBACK_SHAPE = 0;
static const unsigned READBACK_REFERENCES = 12;
static const unsigned READBACK_TRIANGLES = 13;
static const unsigned READBACK_COUNTS = 14;

std::string DX11RayGridClass::Trace_Program()
{
	return std::string(RG_COMMON) + RG_TRACE;
}

DX11RayGridClass::DX11RayGridClass()
	: Refused(false), ProbeWanted(false), BuiltThisFrame(false), FrameIndex(0), Seed(0), Corners(NULL),
	  CutCoordinates(NULL), CutAlpha(NULL),
	  ConstantBuffer(NULL), Arguments(NULL), ArgumentsWrite(NULL), References(0), CountsRead(false), BuildMS(0.0f),
	  Triangles(0), SunProbeMS(0.0f), HemisphereProbeMS(0.0f)
{
	memset(StepMS, 0, sizeof(StepMS));
	memset(&Block, 0, sizeof(Block));
	Keep_Whole();
	memset(Buffers, 0, sizeof(Buffers));
	memset(Writes, 0, sizeof(Writes));
	memset(Reads, 0, sizeof(Reads));
	memset(Programs, 0, sizeof(Programs));
	memset(PassBuffers, 0, sizeof(PassBuffers));
	memset(Frames, 0, sizeof(Frames));
	memset(Shape, 0, sizeof(Shape));
	memset(Counts, 0, sizeof(Counts));
}

static bool make_grid_buffer(ID3D11Device * device, unsigned elements, DXGI_FORMAT format, unsigned element_size,
	ID3D11Buffer ** buffer, ID3D11UnorderedAccessView ** write, ID3D11ShaderResourceView ** read)
{
	D3D11_BUFFER_DESC description;
	memset(&description, 0, sizeof(description));
	description.ByteWidth = elements * element_size;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	D3D11_UNORDERED_ACCESS_VIEW_DESC write_description;
	memset(&write_description, 0, sizeof(write_description));
	write_description.Format = format;
	write_description.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	write_description.Buffer.NumElements = elements;
	D3D11_SHADER_RESOURCE_VIEW_DESC read_description;
	memset(&read_description, 0, sizeof(read_description));
	read_description.Format = format;
	read_description.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	read_description.Buffer.NumElements = elements;
	return SUCCEEDED(device->CreateBuffer(&description, NULL, buffer))
		&& SUCCEEDED(device->CreateUnorderedAccessView(*buffer, &write_description, write))
		&& SUCCEEDED(device->CreateShaderResourceView(*buffer, &read_description, read));
}

bool DX11RayGridClass::Make(DX11BackendClass & backend, ID3D11Device * device)
{
	if (ConstantBuffer != NULL) {
		return true;
	}
	if (Refused || device == NULL) {
		return false;
	}
	const std::string common = RG_COMMON;
	char per[64];
	snprintf(per, sizeof(per), "#define PER %u\n", GRID_BLOCKS / 1024);
	const std::string sources[PROGRAMS] = {
		common + RG_BOUNDS_PROGRAM,
		common + RG_SHAPE_PROGRAM,
		common + RG_BIN_PROGRAM,
		RG_SCAN_PROGRAM,
		std::string(per) + RG_BLOCK_PROGRAM,
		common + RG_TRACE + RG_PROBE_COMMON + RG_PROBE_SUN_PROGRAM,
		common + RG_TRACE + RG_PROBE_COMMON + RG_PROBE_HEMISPHERE_PROGRAM,
		common + RG_LENGTH_PROGRAM,
	};
	static const char * const names[PROGRAMS] = { "raygrid bounds", "raygrid shape", "raygrid bin", "raygrid scan",
		"raygrid blocks", "raygrid sun probe", "raygrid hemisphere probe", "raygrid length" };
	bool made = true;
	for (unsigned index = 0; index < PROGRAMS && made; ++index) {
		std::vector<unsigned char> code;
		made = backend.Compile_Program(sources[index], names[index], "cs_5_0", code)
			&& SUCCEEDED(device->CreateComputeShader(&code[0], code.size(), NULL, &Programs[index]));
	}

	const unsigned elements[BUFFERS] = { 6, 3, GRID_CELLS, GRID_CELLS, GRID_BLOCKS + 1, GRID_REFERENCE_SLOTS, PROBE_COUNTS };
	for (unsigned index = 0; index < BUFFERS && made; ++index) {
		const bool shape = index == SHAPE;
		made = make_grid_buffer(device, elements[index], shape ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32_UINT,
			shape ? 4 * sizeof(float) : sizeof(unsigned), &Buffers[index], &Writes[index], &Reads[index]);
	}

	if (made) {
		D3D11_BUFFER_DESC constants;
		memset(&constants, 0, sizeof(constants));
		constants.ByteWidth = sizeof(Constants);
		constants.Usage = D3D11_USAGE_DEFAULT;
		constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		made = SUCCEEDED(device->CreateBuffer(&constants, NULL, &ConstantBuffer));
		constants.ByteWidth = 4 * sizeof(float);
		constants.Usage = D3D11_USAGE_IMMUTABLE;
		for (unsigned pass = 0; pass < 2 && made; ++pass) {
			const float mode[4] = { static_cast<float>(pass), 0.0f, 0.0f, 0.0f };
			D3D11_SUBRESOURCE_DATA data;
			memset(&data, 0, sizeof(data));
			data.pSysMem = mode;
			made = SUCCEEDED(device->CreateBuffer(&constants, &data, &PassBuffers[pass]));
		}
	}

	if (made) {
		D3D11_BUFFER_DESC arguments;
		memset(&arguments, 0, sizeof(arguments));
		arguments.ByteWidth = 4 * sizeof(unsigned);
		arguments.Usage = D3D11_USAGE_DEFAULT;
		arguments.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		arguments.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		D3D11_UNORDERED_ACCESS_VIEW_DESC write;
		memset(&write, 0, sizeof(write));
		write.Format = DXGI_FORMAT_R32_UINT;
		write.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		write.Buffer.NumElements = 4;
		made = SUCCEEDED(device->CreateBuffer(&arguments, NULL, &Arguments))
			&& SUCCEEDED(device->CreateUnorderedAccessView(Arguments, &write, &ArgumentsWrite));
	}

	for (unsigned index = 0; index < QUERY_FRAMES && made; ++index) {
		Frame & frame = Frames[index];
		D3D11_QUERY_DESC query;
		memset(&query, 0, sizeof(query));
		query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		made = SUCCEEDED(device->CreateQuery(&query, &frame.Disjoint));
		query.Query = D3D11_QUERY_TIMESTAMP;
		made = made && SUCCEEDED(device->CreateQuery(&query, &frame.Start))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.Steps[0]))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.Steps[1]))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.Steps[2]))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.Binned))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.ProbeStart))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.SunProbed))
			&& SUCCEEDED(device->CreateQuery(&query, &frame.HemisphereProbed));
		D3D11_BUFFER_DESC staging;
		memset(&staging, 0, sizeof(staging));
		staging.ByteWidth = (READBACK_COUNTS + PROBE_COUNTS) * sizeof(unsigned);
		staging.Usage = D3D11_USAGE_STAGING;
		staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		made = made && SUCCEEDED(device->CreateBuffer(&staging, NULL, &frame.Readback));
	}

	if (!made) {
		Release();
		Refused = true;
		return false;
	}
	ProbeWanted = getenv("ZHR_RT_GRID") != NULL;
	return true;
}

void DX11RayGridClass::Release()
{
	for (unsigned index = 0; index < BUFFERS; ++index) {
		IUnknown ** const held[3] = { (IUnknown **)&Writes[index], (IUnknown **)&Reads[index], (IUnknown **)&Buffers[index] };
		for (unsigned view = 0; view < 3; ++view) {
			if (*held[view] != NULL) {
				(*held[view])->Release();
				*held[view] = NULL;
			}
		}
	}
	for (unsigned index = 0; index < PROGRAMS; ++index) {
		if (Programs[index] != NULL) {
			Programs[index]->Release();
			Programs[index] = NULL;
		}
	}
	IUnknown ** const held[5] = { (IUnknown **)&ConstantBuffer, (IUnknown **)&PassBuffers[0], (IUnknown **)&PassBuffers[1],
		(IUnknown **)&ArgumentsWrite, (IUnknown **)&Arguments };
	for (unsigned index = 0; index < 5; ++index) {
		if (*held[index] != NULL) {
			(*held[index])->Release();
			*held[index] = NULL;
		}
	}
	for (unsigned index = 0; index < QUERY_FRAMES; ++index) {
		Frame & frame = Frames[index];
		IUnknown ** const queries[10] = { (IUnknown **)&frame.Disjoint, (IUnknown **)&frame.Start, (IUnknown **)&frame.Binned,
			(IUnknown **)&frame.ProbeStart, (IUnknown **)&frame.SunProbed, (IUnknown **)&frame.HemisphereProbed,
			(IUnknown **)&frame.Readback, (IUnknown **)&frame.Steps[0], (IUnknown **)&frame.Steps[1],
			(IUnknown **)&frame.Steps[2] };
		for (unsigned query = 0; query < 10; ++query) {
			if (*queries[query] != NULL) {
				(*queries[query])->Release();
				*queries[query] = NULL;
			}
		}
		frame.Open = frame.Issued = frame.Probed = false;
	}
	Corners = NULL;
	CutCoordinates = NULL;
	CutAlpha = NULL;
	BuiltThisFrame = false;
}

void DX11RayGridClass::Set_Keep(const float low[3], const float high[3])
{
	// Never past the sun's own box, x and y -1 to 1 and depth 0 to 1.
	static const float SUN_LOW[3] = { -1.0f, -1.0f, 0.0f };
	static const float SUN_HIGH[3] = { 1.0f, 1.0f, 1.0f };
	for (unsigned axis = 0; axis < 3; ++axis) {
		Block.KeepLow[axis] = (low[axis] > SUN_LOW[axis]) ? low[axis] : SUN_LOW[axis];
		Block.KeepHigh[axis] = (high[axis] < SUN_HIGH[axis]) ? high[axis] : SUN_HIGH[axis];
	}
}

void DX11RayGridClass::Keep_Whole()
{
	const float low[3] = { -1e30f, -1e30f, -1e30f };
	const float high[3] = { 1e30f, 1e30f, 1e30f };
	Set_Keep(low, high);
}

void DX11RayGridClass::Upload(ID3D11DeviceContext * context)
{
	context->UpdateSubresource(ConstantBuffer, 0, NULL, &Block, 0, 0);
}

void DX11RayGridClass::Build(DX11BackendClass & backend, ID3D11Device * device, ID3D11DeviceContext * context,
	ID3D11ShaderResourceView * corners, unsigned stamp, unsigned triangle_slots, const float sun_from_world[16])
{
	BuiltThisFrame = false;
	if (corners == NULL || !Make(backend, device)) {
		return;
	}
	Report(context);
	Frame & previous = Frames[FrameIndex];
	if (previous.Open) {
		context->End(previous.Disjoint);
		previous.Open = false;
		previous.Issued = true;
	}
	FrameIndex = (FrameIndex + 1) % QUERY_FRAMES;
	Frame & frame = Frames[FrameIndex];
	frame.Issued = false;
	frame.Probed = false;
	context->Begin(frame.Disjoint);
	context->End(frame.Start);
	frame.Open = true;

	// A column of the sun's matrix is that clip axis per world unit, so its length's inverse is the
	// world units one unit of the axis spans.
	memcpy(Block.SunFromWorld, sun_from_world, sizeof(Block.SunFromWorld));
	for (unsigned axis = 0; axis < 3; ++axis) {
		const float per_unit = sqrtf(sun_from_world[axis] * sun_from_world[axis]
			+ sun_from_world[4 + axis] * sun_from_world[4 + axis] + sun_from_world[8 + axis] * sun_from_world[8 + axis]);
		Block.Build[axis] = (per_unit > 0.0f) ? 1.0f / per_unit : 1.0f;
	}
	Block.Build[3] = static_cast<float>(GRID_BUDGET);
	Block.Tuning[0] = 0.0f;
	Block.Tuning[1] = static_cast<float>(stamp);
	Block.Tuning[2] = static_cast<float>(triangle_slots);
	Block.Tuning[3] = static_cast<float>(GRID_REFERENCE_SLOTS);
	Upload(context);
	Corners = corners;

	const UINT none[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
	const UINT zero[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(Writes[BOUNDS], none);
	context->ClearUnorderedAccessViewUint(Writes[COUNTS], zero);
	ID3D11UnorderedAccessView * no_writes[3] = { NULL, NULL, NULL };
	ID3D11ShaderResourceView * no_reads[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
	context->CSSetConstantBuffers(1, 1, &ConstantBuffer);
	context->CSSetShaderResources(16, 1, &Corners);

	context->CSSetUnorderedAccessViews(0, 1, &ArgumentsWrite, NULL);
	context->CSSetShader(Programs[PROGRAM_LENGTH], NULL, 0);
	context->Dispatch(1, 1, 1);

	context->CSSetUnorderedAccessViews(0, 1, &Writes[BOUNDS], NULL);
	context->CSSetShader(Programs[PROGRAM_BOUNDS], NULL, 0);
	context->DispatchIndirect(Arguments, 0);

	context->CSSetUnorderedAccessViews(0, 1, &Writes[SHAPE], NULL);
	context->CSSetShaderResources(0, 1, &Reads[BOUNDS]);
	context->CSSetShader(Programs[PROGRAM_SHAPE], NULL, 0);
	context->Dispatch(1, 1, 1);
	context->CSSetUnorderedAccessViews(0, 1, no_writes, NULL);
	context->CSSetShaderResources(0, 1, no_reads);
	context->End(frame.Steps[0]);

	ID3D11UnorderedAccessView * bin_writes[2] = { Writes[COUNTS], Writes[ITEMS] };
	context->CSSetShaderResources(20, 1, &Reads[SHAPE]);
	context->CSSetUnorderedAccessViews(0, 2, bin_writes, NULL);
	context->CSSetConstantBuffers(2, 1, &PassBuffers[0]);
	context->CSSetShader(Programs[PROGRAM_BIN], NULL, 0);
	context->DispatchIndirect(Arguments, 0);
	context->End(frame.Steps[1]);

	ID3D11UnorderedAccessView * scan_writes[3] = { Writes[COUNTS], Writes[STARTS], Writes[BLOCKS] };
	context->CSSetUnorderedAccessViews(0, 3, scan_writes, NULL);
	context->CSSetShader(Programs[PROGRAM_SCAN], NULL, 0);
	context->Dispatch(GRID_BLOCKS, 1, 1);
	context->CSSetUnorderedAccessViews(0, 3, no_writes, NULL);
	context->CSSetUnorderedAccessViews(0, 1, &Writes[BLOCKS], NULL);
	context->CSSetShader(Programs[PROGRAM_BLOCK], NULL, 0);
	context->Dispatch(1, 1, 1);
	context->CSSetUnorderedAccessViews(0, 1, no_writes, NULL);
	context->End(frame.Steps[2]);

	ID3D11ShaderResourceView * fill_reads[2] = { Reads[STARTS], Reads[BLOCKS] };
	context->CSSetShaderResources(17, 2, fill_reads);
	context->CSSetUnorderedAccessViews(0, 2, bin_writes, NULL);
	context->CSSetConstantBuffers(2, 1, &PassBuffers[1]);
	context->CSSetShader(Programs[PROGRAM_BIN], NULL, 0);
	context->DispatchIndirect(Arguments, 0);

	context->CSSetUnorderedAccessViews(0, 3, no_writes, NULL);
	context->CSSetShaderResources(16, 6, no_reads);
	ID3D11Buffer * no_buffers[2] = { NULL, NULL };
	context->CSSetConstantBuffers(1, 2, no_buffers);
	context->CSSetShader(NULL, NULL, 0);

	context->End(frame.Binned);
	context->CopySubresourceRegion(frame.Readback, 0, READBACK_SHAPE * sizeof(unsigned), 0, 0, Buffers[SHAPE], 0, NULL);
	D3D11_BOX total;
	total.left = GRID_BLOCKS * sizeof(unsigned);
	total.right = total.left + sizeof(unsigned);
	total.top = 0;
	total.bottom = 1;
	total.front = 0;
	total.back = 1;
	context->CopySubresourceRegion(frame.Readback, 0, READBACK_REFERENCES * sizeof(unsigned), 0, 0, Buffers[BLOCKS], 0, &total);
	total.left = 3 * sizeof(unsigned);
	total.right = total.left + sizeof(unsigned);
	context->CopySubresourceRegion(frame.Readback, 0, READBACK_TRIANGLES * sizeof(unsigned), 0, 0, Arguments, 0, &total);
	BuiltThisFrame = true;
}

void DX11RayGridClass::Bind(ID3D11DeviceContext * context, ID3D11ShaderResourceView * heights, const float ground[4])
{
	const bool held = heights != NULL && ground[0] > 0.0f;
	memcpy(Block.Ground, ground, sizeof(Block.Ground));
	Block.Tuning[0] = held ? 1.0f : 0.0f;
	Upload(context);
	// No shape reads as nought, an empty grid: a pass bound while the grid is stale meets no caster.
	ID3D11ShaderResourceView * reads[BOUND_READS] = { Corners, Reads[STARTS], Reads[BLOCKS], Reads[ITEMS],
		BuiltThisFrame ? Reads[SHAPE] : NULL, held ? heights : NULL, CutCoordinates, CutAlpha };
	context->CSSetShaderResources(16, BOUND_READS, reads);
	context->CSSetConstantBuffers(1, 1, &ConstantBuffer);
}

void DX11RayGridClass::Unbind(ID3D11DeviceContext * context)
{
	ID3D11ShaderResourceView * no_reads[BOUND_READS] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
	ID3D11Buffer * no_buffer = NULL;
	context->CSSetShaderResources(16, BOUND_READS, no_reads);
	context->CSSetConstantBuffers(1, 1, &no_buffer);
}

void DX11RayGridClass::Probe(ID3D11DeviceContext * context, ID3D11ShaderResourceView * depth,
	const float world_from_clip[16], unsigned width, unsigned height, const float sun[4], float normal_offset,
	float depth_bias, unsigned old_reference_slots, ID3D11ShaderResourceView * heights, const float ground[4],
	ID3D11ShaderResourceView * const old_grid[5])
{
	if (!Probing() || depth == NULL) {
		return;
	}
	Frame & frame = Frames[FrameIndex];
	if (!frame.Open) {
		return;
	}
	memcpy(Block.WorldFromClip, world_from_clip, sizeof(Block.WorldFromClip));
	Block.Screen[0] = static_cast<float>(width);
	Block.Screen[1] = static_cast<float>(height);
	Block.Screen[2] = 1.0f / static_cast<float>(width);
	Block.Screen[3] = 1.0f / static_cast<float>(height);
	memcpy(Block.Sun, sun, sizeof(Block.Sun));
	Block.Probe[0] = normal_offset;
	Block.Probe[1] = depth_bias;
	Block.Probe[2] = static_cast<float>(Seed++ & 0xffff);
	Block.Probe[3] = static_cast<float>(old_reference_slots);
	Bind(context, heights, ground);

	ID3D11ShaderResourceView * reads[6] = { depth, old_grid[0], old_grid[1], old_grid[2], old_grid[3], old_grid[4] };
	context->CSSetShaderResources(0, 6, reads);
	const UINT zero[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(Writes[HITS], zero);
	context->CSSetUnorderedAccessViews(0, 1, &Writes[HITS], NULL);
	const UINT groups_x = (width + 7) / 8;
	const UINT groups_y = (height + 7) / 8;
	context->End(frame.ProbeStart);
	context->CSSetShader(Programs[PROGRAM_PROBE_SUN], NULL, 0);
	context->Dispatch(groups_x, groups_y, 1);
	context->End(frame.SunProbed);
	context->CSSetShader(Programs[PROGRAM_PROBE_HEMISPHERE], NULL, 0);
	context->Dispatch(groups_x, groups_y, 1);
	context->End(frame.HemisphereProbed);

	ID3D11UnorderedAccessView * no_write = NULL;
	ID3D11ShaderResourceView * no_reads[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
	context->CSSetUnorderedAccessViews(0, 1, &no_write, NULL);
	context->CSSetShaderResources(0, 6, no_reads);
	context->CSSetShader(NULL, NULL, 0);
	Unbind(context);
	context->CopySubresourceRegion(frame.Readback, 0, READBACK_COUNTS * sizeof(unsigned), 0, 0, Buffers[HITS], 0, NULL);
	context->End(frame.Disjoint);
	frame.Open = false;
	frame.Issued = true;
	frame.Probed = true;
}

std::string DX11RayGridClass::Report(ID3D11DeviceContext * context)
{
	if (Refused) {
		return "; grid refused by the device";
	}
	if (ConstantBuffer == NULL || context == NULL) {
		return "; grid not made";
	}
	const UINT later = D3D11_ASYNC_GETDATA_DONOTFLUSH;
	for (unsigned index = 0; index < QUERY_FRAMES; ++index) {
		Frame & frame = Frames[index];
		if (!frame.Issued) {
			continue;
		}
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
		UINT64 start = 0, binned = 0, probe_start = 0, sun_probed = 0, hemisphere_probed = 0;
		UINT64 steps[BUILD_STEPS - 1] = { 0, 0, 0 };
		if (context->GetData(frame.Disjoint, &disjoint, sizeof(disjoint), later) != S_OK
				|| context->GetData(frame.Start, &start, sizeof(start), later) != S_OK
				|| context->GetData(frame.Steps[0], &steps[0], sizeof(steps[0]), later) != S_OK
				|| context->GetData(frame.Steps[1], &steps[1], sizeof(steps[1]), later) != S_OK
				|| context->GetData(frame.Steps[2], &steps[2], sizeof(steps[2]), later) != S_OK
				|| context->GetData(frame.Binned, &binned, sizeof(binned), later) != S_OK) {
			continue;
		}
		if (frame.Probed && (context->GetData(frame.ProbeStart, &probe_start, sizeof(probe_start), later) != S_OK
				|| context->GetData(frame.SunProbed, &sun_probed, sizeof(sun_probed), later) != S_OK
				|| context->GetData(frame.HemisphereProbed, &hemisphere_probed, sizeof(hemisphere_probed), later) != S_OK)) {
			continue;
		}
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (FAILED(context->Map(frame.Readback, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
			continue;
		}
		const unsigned * read = static_cast<const unsigned *>(mapped.pData);
		memcpy(Shape, &read[READBACK_SHAPE], sizeof(Shape));
		References = read[READBACK_REFERENCES];
		Triangles = read[READBACK_TRIANGLES];
		if (frame.Probed) {
			memcpy(Counts, &read[READBACK_COUNTS], sizeof(Counts));
			CountsRead = true;
		}
		context->Unmap(frame.Readback, 0);
		frame.Issued = false;
		if (!disjoint.Disjoint && disjoint.Frequency > 0) {
			const double to_ms = 1000.0 / static_cast<double>(disjoint.Frequency);
			BuildMS = static_cast<float>(static_cast<double>(binned - start) * to_ms);
			const UINT64 marks[BUILD_STEPS + 1] = { start, steps[0], steps[1], steps[2], binned };
			for (unsigned step = 0; step < BUILD_STEPS; ++step) {
				StepMS[step] = static_cast<float>(static_cast<double>(marks[step + 1] - marks[step]) * to_ms);
			}
			if (frame.Probed) {
				SunProbeMS = static_cast<float>(static_cast<double>(sun_probed - probe_start) * to_ms);
				HemisphereProbeMS = static_cast<float>(static_cast<double>(hemisphere_probed - sun_probed) * to_ms);
			}
		}
	}
	char line[640];
	snprintf(line, sizeof(line),
		"; grid %.0fx%.0fx%.0f cells (%.0f of %u), cell %.2f world units, %u triangles, %u references (%u slots)%s, "
		"build %.3f ms (bounds %.3f, count %.3f, scan %.3f, fill %.3f)",
		Shape[8], Shape[9], Shape[10], Shape[3], GRID_BUDGET, Shape[7], Triangles, References, GRID_REFERENCE_SLOTS,
		References > GRID_REFERENCE_SLOTS ? " OVERFLOWED" : "", BuildMS, StepMS[0], StepMS[1], StepMS[2], StepMS[3]);
	std::string report = line;
	if (CountsRead) {
		snprintf(line, sizeof(line),
			"; grid probe: %u sun rays, the 2D grid stopped %u, the 3D grid %u, both %u, 2D only %u, 3D only %u, "
			"3D ground %u, %.3f ms; hemisphere 40 units: %u rays, %u stopped by casters, %u by the ground; "
			"150 units: %u rays, %u by casters, %u by the ground; both hemisphere rays %.3f ms",
			Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5], Counts[6], SunProbeMS,
			Counts[7], Counts[8], Counts[9], Counts[10], Counts[11], Counts[12], HemisphereProbeMS);
		report += line;
	}
	return report;
}
