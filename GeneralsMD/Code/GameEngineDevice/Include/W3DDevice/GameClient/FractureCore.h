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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FractureCore.h: a structure cut into fragments that together are its model (.lwfrag), the death it
// was baked to come down by (.lwcollapse), and the hole a hit knocks into it.  The formats are the
// wrapper's experiments/proofs/2026-11-20/fracture-look/README.md, written by lwfrag.py and
// lwcollapse.py there; both are little endian, as every target this builds for is.
//
// The hole is the client's alone (W3DFractureDraw): the logic only says where a hit landed and how
// wide its blast was, and health alone decides when the structure falls.  So a hit takes the
// fragments where it landed and never one whose loss would leave another standing fragment with no
// way down to the ground through its neighbours: a hole, never a collapse.  Nothing here knows the
// engine: test_fracture includes it bare.

#pragma once

#ifndef FRACTURE_CORE_H
#define FRACTURE_CORE_H

#include <algorithm>
#include <math.h>
#include <string.h>
#include <vector>

namespace Fracture {

enum { MATERIAL_INNER = 1, MATERIAL_ALPHA_TEST = 2, MATERIAL_DECAL = 4 };
enum { FRAGMENT_GROUND = 1 };
enum { COLLAPSE_STAYS = 1, COLLAPSE_KICKED = 2 };

/// A fragment whose lowest point is under this stands on the ground, flag or not: the fortress's
/// block sits on a 2.5 high plinth ring with nothing under its middle (README, the support rule).
const float GROUNDED_BELOW = 3.0f;

struct Material
{
	char texture[33];					///< LWFort_Stone.tga, LWFrag_InnerStone.tga
	unsigned char flags;			///< MATERIAL_*
	unsigned char cls;				///< an inner material's class, 1 stone to 5 sand
};

struct Vertex { float pos[3]; float normal[3]; float uv[2]; };		///< model space, V bottom-up
struct Triangle { unsigned short index[3]; unsigned short material; };	///< indices into its fragment's vertices

struct Fragment
{
	float centroid[3];
	float volume;
	unsigned char cls;
	unsigned char flags;			///< FRAGMENT_*
	unsigned short part;			///< the W3D mesh, in file order, that holds most of it
	unsigned firstNeighbour, neighbours;
	unsigned firstVertex, vertices;
	unsigned firstTriangle, triangles;
	float boxMin[3], boxMax[3];
	bool grounded;
};

struct Asset
{
	float boxMin[3], boxMax[3];
	std::vector<Material> materials;
	std::vector<Fragment> fragments;
	std::vector<unsigned> neighbours;		///< every fragment's, one after another
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;
	float volume;												///< all of it
};

/// One key of a baked fall: where the fragment's centroid is (model space) and how it is turned.
struct Key { float pos[3]; float rot[4]; };		///< quaternion x y z w

struct Track
{
	unsigned start, count, firstKey;		///< the keys of frames start .. start + count - 1
	unsigned char flags;								///< COLLAPSE_*
};

struct Collapse
{
	float angle;									///< the way it falls, degrees from model +x anticlockwise
	unsigned fps, frames;
	std::vector<Track> tracks;		///< one a fragment
	std::vector<Key> keys;
};

namespace detail {

struct Reader
{
	const unsigned char *at, *end;
	bool ok;
	bool take( void *out, size_t n )
	{
		if( !ok || (size_t)( end - at ) < n ) { ok = false; return false; }
		memcpy( out, at, n );
		at += n;
		return true;
	}
	size_t left() const { return ok ? (size_t)( end - at ) : 0; }
	unsigned u32() { unsigned v = 0; take( &v, 4 ); return v; }
	unsigned short u16() { unsigned short v = 0; take( &v, 2 ); return v; }
	unsigned char u8() { unsigned char v = 0; take( &v, 1 ); return v; }
	float f32() { float v = 0.0f; take( &v, 4 ); return v; }
	short s16() { short v = 0; take( &v, 2 ); return v; }
};

inline float distanceSquared( const float a[3], const float b[3] )
{
	const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

}	// namespace detail

/// Reads a .lwfrag, version 1.  FALSE, and out half filled, for anything it does not hold up to.
inline bool parse( const unsigned char *data, size_t size, Asset &out )
{
	detail::Reader r = { data, data + size, true };
	char magic[4];
	if( !r.take( magic, 4 ) || memcmp( magic, "LWFR", 4 ) != 0 || r.u32() != 1 )
		return false;
	const unsigned materials = r.u32(), fragments = r.u32();
	for( int k = 0; k < 3; ++k ) out.boxMin[k] = r.f32();
	for( int k = 0; k < 3; ++k ) out.boxMax[k] = r.f32();
	if( !r.ok || materials > 1024 || (size_t)materials * 36 > r.left() || (size_t)fragments * 32 > r.left() )
		return false;
	out.materials.resize( materials );
	for( unsigned m = 0; m < materials; ++m )
	{
		Material &mat = out.materials[m];
		r.take( mat.texture, 32 );
		mat.texture[32] = 0;
		mat.flags = r.u8();
		mat.cls = r.u8();
		r.u16();
	}
	out.fragments.resize( fragments );
	out.neighbours.clear();
	out.vertices.clear();
	out.triangles.clear();
	out.volume = 0.0f;
	for( unsigned i = 0; i < fragments && r.ok; ++i )
	{
		Fragment &f = out.fragments[i];
		for( int k = 0; k < 3; ++k ) f.centroid[k] = r.f32();
		f.volume = r.f32();
		f.cls = r.u8();
		f.flags = r.u8();
		f.part = r.u16();
		const unsigned adjacent = r.u32(), vertices = r.u32(), triangles = r.u32();
		if( !r.ok || (size_t)adjacent * 8 + (size_t)vertices * 32 + (size_t)triangles * 8 > r.left() || vertices > 65535 )
			return false;
		f.firstNeighbour = (unsigned)out.neighbours.size();
		f.neighbours = adjacent;
		for( unsigned k = 0; k < adjacent; ++k )
		{
			out.neighbours.push_back( r.u32() );
			r.f32();		// the shared face's area: the support rule only asks whether they touch
		}
		f.firstVertex = (unsigned)out.vertices.size();
		f.vertices = vertices;
		for( int k = 0; k < 3; ++k ) { f.boxMin[k] = 1e30f; f.boxMax[k] = -1e30f; }
		for( unsigned k = 0; k < vertices; ++k )
		{
			Vertex v;
			r.take( &v, sizeof( v ) );
			for( int c = 0; c < 3; ++c )
			{
				f.boxMin[c] = std::min( f.boxMin[c], v.pos[c] );
				f.boxMax[c] = std::max( f.boxMax[c], v.pos[c] );
			}
			out.vertices.push_back( v );
		}
		f.firstTriangle = (unsigned)out.triangles.size();
		f.triangles = triangles;
		for( unsigned k = 0; k < triangles; ++k )
		{
			Triangle t;
			if( !r.take( &t, sizeof( t ) ) || t.index[0] >= vertices || t.index[1] >= vertices || t.index[2] >= vertices || t.material >= materials )
				return false;
			out.triangles.push_back( t );
		}
		f.grounded = ( f.flags & FRAGMENT_GROUND ) != 0 || ( vertices > 0 && f.boxMin[2] < GROUNDED_BELOW );
		out.volume += f.volume;
	}
	for( size_t k = 0; k < out.neighbours.size(); ++k )
		if( out.neighbours[k] >= fragments )
			return false;
	return r.ok && r.at == r.end;
}

/// Reads a .lwcollapse, version 1, for an asset of this many fragments.
/** One version 1 block: a whole collapse, falling one way. */
inline bool parseCollapse( const unsigned char *data, size_t size, size_t fragments, Collapse &out )
{
	detail::Reader r = { data, data + size, true };
	char magic[4];
	if( !r.take( magic, 4 ) || memcmp( magic, "LWCO", 4 ) != 0 || r.u32() != 1 )
		return false;
	out.angle = 0.0f;
	const unsigned count = r.u32();
	out.fps = r.u16();
	out.frames = r.u16();
	const float unit = r.f32();
	const unsigned total = r.u32();
	if( !r.ok || count != fragments || out.fps == 0 || (size_t)count * 12 + (size_t)total * 14 != r.left() )
		return false;
	out.tracks.resize( count );
	for( unsigned i = 0; i < count; ++i )
	{
		Track &t = out.tracks[i];
		t.start = r.u16();
		t.count = r.u16();
		t.firstKey = r.u32();
		t.flags = r.u8();
		r.u8();
		r.u16();
		if( (size_t)t.firstKey + t.count > total )
			return false;
	}
	out.keys.resize( total );
	for( unsigned k = 0; k < total; ++k )
	{
		Key &key = out.keys[k];
		for( int c = 0; c < 3; ++c ) key.pos[c] = r.s16() * unit;
		float length = 0.0f;
		for( int c = 0; c < 4; ++c ) { key.rot[c] = r.s16() / 32767.0f; length += key.rot[c] * key.rot[c]; }
		length = length > 0.0f ? 1.0f / sqrtf( length ) : 1.0f;
		for( int c = 0; c < 4; ++c ) key.rot[c] *= length;
	}
	return r.ok;
}

/** A .lwcollapse file, version 1 (one way down) or 2: the same header with a track count where the
	key total was, then a table of (angle, offset, size) and the version 1 blocks one after another,
	one a way it can fall. */
inline bool parseCollapses( const unsigned char *data, size_t size, size_t fragments, std::vector<Collapse> &out )
{
	const size_t HEADER = 24, TABLE_ENTRY = 12;
	out.clear();
	detail::Reader r = { data, data + size, true };
	char magic[4];
	if( !r.take( magic, 4 ) || memcmp( magic, "LWCO", 4 ) != 0 )
		return false;
	const unsigned version = r.u32();
	if( version == 1 )
	{
		out.resize( 1 );
		return parseCollapse( data, size, fragments, out[0] );
	}
	if( version != 2 )
		return false;
	r.u32();		// fragments, fps, frames and the unit again: every block carries its own
	r.u16();
	r.u16();
	r.f32();
	const unsigned count = r.u32();
	if( !r.ok || count == 0 || HEADER + (size_t)count * TABLE_ENTRY > size )
		return false;
	out.resize( count );
	for( unsigned t = 0; t < count; ++t )
	{
		const float angle = r.f32();
		const unsigned offset = r.u32(), length = r.u32();
		if( !r.ok || (size_t)offset + length > size || !parseCollapse( data + offset, length, fragments, out[t] ) )
		{
			out.clear();
			return false;
		}
		out[t].angle = angle;
	}
	return true;
}

/** The way down nearest a direction (degrees, model space): the tower falls away from its killer. */
inline size_t nearestCollapse( const std::vector<Collapse> &collapses, float angle )
{
	size_t best = 0;
	float bestOff = 361.0f;
	for( size_t i = 0; i < collapses.size(); ++i )
	{
		float off = fmodf( collapses[i].angle - angle, 360.0f );
		off = off < 0.0f ? off + 360.0f : off;
		off = off > 180.0f ? 360.0f - off : off;
		if( off < bestOff )
		{
			bestOff = off;
			best = i;
		}
	}
	return best;
}

/** A fragment's baked pose at a frame after the death, between keys.  FALSE while it still stands as
	built: a stump that never moves, or a frame before its track starts.  After its last key it rests
	there.  The writer keeps a track's quaternions on one hemisphere, so a plain blend never flips. */
inline bool trackPose( const Collapse &c, unsigned fragment, float frame, float pos[3], float rot[4] )
{
	const Track &t = c.tracks[fragment];
	if( t.count == 0 || frame < (float)t.start )
		return false;
	float local = frame - (float)t.start;
	const float last = (float)( t.count - 1 );
	local = local < last ? local : last;
	const unsigned i = (unsigned)local;
	const unsigned j = i + 1 < t.count ? i + 1 : i;
	const float blend = local - (float)i;
	const Key &a = c.keys[t.firstKey + i], &b = c.keys[t.firstKey + j];
	float length = 0.0f;
	for( int k = 0; k < 3; ++k ) pos[k] = a.pos[k] + ( b.pos[k] - a.pos[k] ) * blend;
	for( int k = 0; k < 4; ++k ) { rot[k] = a.rot[k] + ( b.rot[k] - a.rot[k] ) * blend; length += rot[k] * rot[k]; }
	length = length > 0.0f ? 1.0f / sqrtf( length ) : 1.0f;
	for( int k = 0; k < 4; ++k ) rot[k] *= length;
	return true;
}

/// A box in the world, as a piece of the ruin lies.
struct Box { float low[3], high[3]; };

/** How far each piece of a ruin falls to rest on what is under it: lowest first, a piece is held by the
	ground under it (ground[i]) or by a lower box reaching at least inset across it on both axes, at
	that box's top or its own foot, whichever is lower; one with more than minGap of air below it drops
	by that much, and the pieces above it see it where it lands.  Boxes: a piece held by a neighbour's
	corner it does not touch stays where it is, never sinks.  0 for a piece that stays. */
inline void dropGaps( const std::vector<Box> &boxes, const std::vector<float> &ground, float minGap, float inset,
	std::vector<float> &gaps )
{
	struct Order
	{
		float z;
		unsigned i;
		bool operator<( const Order &o ) const { return z < o.z || ( z == o.z && i < o.i ); }
	};
	std::vector<Order> order( boxes.size() );
	for( size_t i = 0; i < boxes.size(); ++i )
	{
		order[i].z = boxes[i].low[2];
		order[i].i = (unsigned)i;
	}
	std::sort( order.begin(), order.end() );
	gaps.assign( boxes.size(), 0.0f );
	std::vector<Box> landed( boxes );
	for( size_t k = 0; k < order.size(); ++k )
	{
		Box &p = landed[order[k].i];
		float support = ground[order[k].i];
		for( size_t j = 0; j < k && p.low[2] - support > minGap; ++j )
		{
			const Box &q = landed[order[j].i];
			if( q.high[0] - inset > p.low[0] && q.low[0] + inset < p.high[0] && q.high[1] - inset > p.low[1] && q.low[1] + inset < p.high[1] )
				support = std::max( support, std::min( q.high[2], p.low[2] ) );
		}
		const float gap = p.low[2] - support;
		if( gap <= minGap )
			continue;
		gaps[order[k].i] = gap;
		p.low[2] -= gap;
		p.high[2] -= gap;
	}
}

/// How many standing fragments reach the ground through standing neighbours.
inline unsigned supportedCount( const Asset &a, const std::vector<unsigned char> &standing,
	std::vector<unsigned> &queue, std::vector<unsigned char> &seen )
{
	const size_t n = a.fragments.size();
	seen.assign( n, 0 );
	queue.clear();
	for( size_t i = 0; i < n; ++i )
		if( standing[i] && a.fragments[i].grounded )
		{
			seen[i] = 1;
			queue.push_back( (unsigned)i );
		}
	for( size_t q = 0; q < queue.size(); ++q )
	{
		const Fragment &f = a.fragments[queue[q]];
		for( unsigned k = 0; k < f.neighbours; ++k )
		{
			const unsigned j = a.neighbours[f.firstNeighbour + k];
			if( standing[j] && !seen[j] )
			{
				seen[j] = 1;
				queue.push_back( j );
			}
		}
	}
	return (unsigned)queue.size();
}

/// How many of a fragment's nine samples lie in the sphere: its centroid and its box's corners pulled
/// 40% of the way in, as destruction.py samples a blast.
inline int samplesInside( const Fragment &f, const float point[3], float radius )
{
	const float r2 = radius * radius;
	int inside = detail::distanceSquared( f.centroid, point ) <= r2 ? 1 : 0;
	for( int corner = 0; corner < 8; ++corner )
	{
		float p[3];
		for( int k = 0; k < 3; ++k )
		{
			const float edge = ( corner >> k ) & 1 ? f.boxMax[k] : f.boxMin[k];
			p[k] = edge + ( f.centroid[k] - edge ) * 0.4f;
		}
		inside += detail::distanceSquared( p, point ) <= r2 ? 1 : 0;
	}
	return inside;
}

/// The squared distance from a point to a fragment's box, 0 inside it.
inline float boxDistanceSquared( const Fragment &f, const float point[3] )
{
	float d = 0.0f;
	for( int k = 0; k < 3; ++k )
	{
		const float out = point[k] < f.boxMin[k] ? f.boxMin[k] - point[k] : ( point[k] > f.boxMax[k] ? point[k] - f.boxMax[k] : 0.0f );
		d += out * out;
	}
	return d;
}

/** The fragments a hit at point (model space) with a hole of this radius knocks out, nearest first,
	and takes them out of standing: those whose centroid or at least half their samples lie in the hole
	(a hit lands on the surface, so half the hole is air and the centroid rule is what reaches a fortress
	block's ten units), or when there
	is none the one fragment nearest the point whose box comes within the radius.  Never a kept one
	(a gun), never more volume than budget, and never one whose loss would leave a fragment that still
	reaches the ground unable to.  A fragment that stood on nothing from the start (a part the cut left
	loose) is never taken either, since its loss would change nothing that count can see. */
inline void knockOut( const Asset &a, std::vector<unsigned char> &standing, const std::vector<unsigned char> &kept,
	const float point[3], float radius, float budget, std::vector<unsigned> &out )
{
	out.clear();
	struct Candidate
	{
		float d2;
		unsigned i;
		bool operator<( const Candidate &o ) const { return d2 < o.d2 || ( d2 == o.d2 && i < o.i ); }
	};
	std::vector<Candidate> candidates;
	float nearestBox = radius * radius;
	int nearest = -1;
	for( size_t i = 0; i < a.fragments.size(); ++i )
	{
		if( !standing[i] || kept[i] )
			continue;
		const Fragment &f = a.fragments[i];
		const float d2 = detail::distanceSquared( f.centroid, point );
		if( d2 <= radius * radius || samplesInside( f, point, radius ) * 2 >= 9 )
		{
			Candidate c = { d2, (unsigned)i };
			candidates.push_back( c );
		}
		else
		{
			const float box = boxDistanceSquared( f, point );
			if( box < nearestBox )
			{
				nearestBox = box;
				nearest = (int)i;
			}
		}
	}
	if( candidates.empty() && nearest >= 0 )
	{
		Candidate c = { 0.0f, (unsigned)nearest };
		candidates.push_back( c );
	}
	std::sort( candidates.begin(), candidates.end() );

	std::vector<unsigned> queue;
	std::vector<unsigned char> seen;
	unsigned supported = supportedCount( a, standing, queue, seen );
	float used = 0.0f;
	for( size_t c = 0; c < candidates.size(); ++c )
	{
		const unsigned i = candidates[c].i;
		if( used + a.fragments[i].volume > budget )
			continue;
		standing[i] = 0;
		const unsigned now = supportedCount( a, standing, queue, seen );
		if( now + 1 == supported )
		{
			supported = now;
			used += a.fragments[i].volume;
			out.push_back( i );
		}
		else
			standing[i] = 1;
	}
}

}	// namespace Fracture

#endif // FRACTURE_CORE_H
