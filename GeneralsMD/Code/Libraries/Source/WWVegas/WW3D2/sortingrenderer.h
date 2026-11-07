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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef SORTING_RENDERER_H
#define SORTING_RENDERER_H

#include "always.h"
#include <string.h>

class SortingNodeStruct;
class SphereClass;

class SortingRendererClass
{
	static bool _EnableTriangleDraw;

	static void Flush_Sorting_Pool();
	static void Insert_To_Sorting_Pool(SortingNodeStruct* state);

public:
	static void Insert_Triangles(
		const SphereClass& bounding_sphere,
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Insert_Triangles(
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	/// Quads laid out four vertices each, in order, from min_vertex_index; the pool sorts one entry
	/// a quad and does not read the index buffer.  glow: the quads' normals hold a fire's glow, which
	/// the Direct3D 11 backend adds after the shade (Direct3D11_Set_Smoke_Glow).  fire: a fire or
	/// explosion system's quads, each particle's age and seed in v2 (Direct3D11_Set_Fire_Look).
	/// emissive: quads that take no shadow and no smoke shade (Direct3D11_Set_Emissive).
	/// hard_edge: quads whose soft particle fade is a plain depth test (SOFT_PARTICLE_SAMPLING).
	static void Insert_Quads(
		unsigned short quad_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count,
		bool glow = false,
		bool fire = false,
		bool emissive = false,
		bool hard_edge = false);

	static void Insert_VolumeParticle(
		const SphereClass& bounding_sphere,
		unsigned short start_index, 
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count,
		unsigned short layerCount);

	static void Flush();
	static void Deinit();
	
	static void SetMinVertexBufferSize( unsigned val );

	/// Polygons refused since the start because the pool's node list was full; they are not drawn.
	static unsigned Get_Refused_Polygon_Count();

	/// Phases of every flush since the reset.  Depth and the radix are sort, filling the dynamic
	/// buffers is copy, and Draw_Triangles is draw.  A frame flushes more than once.
	static void Reset_Flush_Profile();
	static void Get_Flush_Profile(unsigned &entries, float &sort_ms, float &copy_ms, float &draw_ms);

	/// Where the flush can hand work to more threads: work(0..count-1, context), returning once all of
	/// it is done.  WW3D2 cannot see the game's job pool, so the device layer installs one; without it
	/// the work runs inline.  The work never allocates and never touches the device.
	typedef void (*ParallelForFunc)(int count, int granularity, void (*work)(int index, void *context), void *context);
	static void Set_Parallel_For(ParallelForFunc parallel_for);
	/// The same pool for the rest of WW3D2 (the skin fill), under the same rules.
	static void Parallel_For(int count, int granularity, void (*work)(int index, void *context), void *context);

	/// A depth as an unsigned key that orders the way the float does, negatives first and -0 just
	/// below +0; the flush's radix sort buckets on it.  The bits come out through memcpy so no
	/// aliasing rule is bent.
	static unsigned _Depth_Sort_Key(float depth)
	{
		unsigned bits;
		memcpy(&bits,&depth,sizeof(bits));
		const unsigned magnitude=bits&0x7FFFFFFFu;
		return (bits&0x80000000u) ? 0x7FFFFFFFu-magnitude : 0x80000000u+magnitude;
	}

	/// The pixels a sorted entry can touch, as an inclusive range of the SORT_TILE_GRID by
	/// SORT_TILE_GRID tiles the viewport is cut into.  Two entries in no common tile share no pixel.
	enum { SORT_TILE_GRID = 64 };
	struct SortTiles { unsigned char x0, x1, y0, y1; };
	/// The tiles an entry's corners reach through clip, the world, view and projection its draw
	/// multiplies them by as sixteen floats in D3DX order.  Each corner points at its x, y and z:
	/// three are a triangle, four a quad.  The range is widened by a margin past the rasterizer's
	/// snapping, and a corner at or behind the eye, or one that comes out NaN, covers the whole grid.
	static SortTiles _Entry_Tiles(const float* const* corners, unsigned corner_count, const float* clip);
	/// An entry's draw state for _Group_Runs with this bit set is one nothing may pass and that
	/// joins no earlier run: it stays exactly where the depth sort put it.
	enum { SORT_STATE_FIXED = 0x8000 };
	/// The order a flush draws its depth sorted entries in.  An entry joins the latest earlier run of
	/// its own draw state when every entry it would pass lies in other tiles, so each pixel still
	/// blends the same draws in the same order; otherwise it stays where the sort put it.  Writes the
	/// drawing order, entry indices, into order[] and returns how many runs that makes.
	static unsigned _Group_Runs(const unsigned short* states, const SortTiles* tiles, unsigned count,
		unsigned* order);

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }
};

#endif

