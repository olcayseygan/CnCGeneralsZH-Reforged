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

// RandomMapTemplate.cpp
// A random map in a shipped map's image. The generator lays out an open skeleton for the players and
// the size asked for (the starts, their money, the AI's paths); this file copies the shipped map's own
// base round every start (its plateau, cliffs and ramps, its docks and derricks, turned to face the
// middle as the start does), gives it the shipped map's large features at the sizes it measures them
// (its river and bridges with the town that stands on it, its sea or islands, its lakes, the canyon
// and mesa of its middle, its other towns copied whole, its tech buildings, its roads), cuts a ramp
// to anything left cut off, then fills the local detail by guided patch quilting from the shipped
// map's own heights, textures, blends and scenery.
//
// The port of Tools/RandomMapLab/synthmap.py with mapfeatures.py's inventory and mapstyle.py's
// reader; the docstrings there argue every constant kept here. Where this differs it says so.
// Everything a machine decides here it decides from the two maps' bytes and the seed: randomness is
// the generator's hashCell, every cost the quilting compares is an integer, ties go to the lower
// index, and the trig is DetTrig's.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <queue>
#include "Common/RandomMapTemplate.h"
#include "Common/crc.h"
#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/MapObject.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/FPUControl.h"
#include "Lib/Trig.h"
#include "Compression.h"

// windef.h's 16-bit pointer qualifiers, empty macros that eat the words as names
#undef near
#undef far

namespace
{

//-----------------------------------------------------------------------------
// Constants (synthmap.py's names)
//-----------------------------------------------------------------------------

const Int MAP_SIDE_MAX = 2048;			// a template side longer than this is not a map
const Int PATCH = 48;					// cells a side
const Int OVERLAP = 12;				// cells two neighbouring patches share
const Int COARSE = 4;					// the first pass tries every fourth offset over every fourth cell
const Int COARSE_SKIP = 2;				// of those cells every second each way, which halves the time an 8-player map takes
const Int REFINE = 8;					// best coarse offsets whose neighbours the full comparison tries (16 in the prototype)
const Int FINE_STEP = 2;				// the full comparison reads every second cell each way (every cell in the prototype)
const Int DETAIL_RADIUS = 10;
const Int RIM_CELLS = 3;
const Int SEAM_BAND = 2;
const Real PAD_INNER = 24.0f, PAD_OUTER = 34.0f;
const Real SITE_INNER = 7.0f, SITE_OUTER = 12.0f;
const Real COPY_FADE = 10.0f;			// cells over which a copied pad or crossing fades into the map round it
const Real PAD_MARGIN = 12.0f;			// a pad's reach past the exemplar's base plateau
const Real PAD_MOST = 90.0f;			// the furthest a base plateau reaches from its start in the exemplar
const Real PAD_OPEN = 24.0f;			// the base's reach on open ground
const Real PAD_CLIFF = 0.25f;			// of a base plateau's rim, the share that must fall away for it to be a plateau
const Real PAD_SPACING = 0.45f;			// of the nearest two starts' distance, the most a pad's radius may be
const Real PAD_SCALE_LEAST = 0.5f;
const Real PAD_BEARING_SLACK = 0.05f;	// radians a seat, the misses within which bases count as facing the seats as well
const Int PAD_CORRIDOR = 28;				// cells between two plateau bases, Twilight Flame's narrowest way from one to the next; a narrower way is filled
const Int JOIN_FADE = 16;				// cells of the two bases' summed distance over which a filled way falls back to the ground past it: eight cells aside
const Real PAD_SITE_LEAST = 20.0f;		// cells a dock or derrick pulled in off the map's edge keeps from its start, past the command centre
const Real PAD_SHORE = 4.0f;			// cells a pad stays off the exemplar's water
const Real RAMP_STEEP = 8.0f;			// bytes a cell, the steepest a cut ramp climbs
const Int WIDE_RAMP_HALF = 16;			// cells either side of its walk, a scaled plateau's ramp: Twilight Flame's 22 and the cliffs' growth
const Int WIDE_RAMP_BLEND = 4;			// cells over which a widened ramp fades into the ground beside it
const Real WIDE_RAMP_STEEP = 6.0f;		// bytes a cell along a widened ramp, so its corners stay under the cliff span across it too
const Int CROSS_WET = 100;				// water cells in a town's box that put the town on its river
const Int CROSS_STEP = 8;				// cells between the places tried for a crossing along its river
const Real CALM_EASE = 20.0f;			// cells over which a river's wander comes back past a crossing
const Real BRIDGE_DECK = 4.0f;			// cells a bridge's deck takes along its river
const Real BRIDGE_END = 4.0f;			// cells round a river bridge's end raised to its bank
const Real CROSS_GUARD = 0.3f;			// how far into a pad's fade a crossing may reach
const Int CROSS_LEAST = 20;				// cells a side, the least of a crossing cut down to fit: a bridge and its banks
const Int CROSS_CROPS = 6;				// the cuts of a crossing tried, the whole of it first
const Real CROSS_CROP[CROSS_CROPS] = { 1.0f, 0.75f, 0.55f, 0.4f, 0.3f, 0.2f };
const Int REPAIR_ROUNDS = 4;
const Real SLOPE_REACH = 100.0f;
const Int PILE_FLAT = 4;
const Int TECH_FLAT = 8;
const Real MIN_RELIEF = 60.0f;

const Real WALL_CELLS = 3.0f;
const Real MESA_RAMP = 22.0f;
const Real END_CELLS = 25.0f;
const Real FORD_AT = 0.6f;
const Real FORD_CELLS = 8.0f;
const Real SHORE_CELLS = 6.0f;
const Real VALLEY_CELLS = 20.0f;
const Int RIVER_TRIES = 6;
const Real RIVER_MIN = 0.3f;
const Real JUNCTION_CLEAR = 40.0f;
const Real BRANCH_TURN = 0.6f;
const Int BRANCH_TRIES = 6;
const Real BRANCH_SETTLE = 40.0f;
const Int LAKE_CELLS = 200;
const Int LAKE_POINTS = 24;
const Real SEA_FLOOR = 8.0f;
const Real COAST_WOBBLE = 8.0f;
const Real CAUSEWAY_CELLS = 12.0f;
const Real ROAD_CLIMB = 0.5f;
const Int ROAD_LEAST = 6;
const Int ROAD_SPACING = 2;
const Real ROAD_TOLERANCE = 1.5f;
const Int ROAD_CLEAR = 3;
const Real ROAD_MIN = 60.0f;
// a road is pulled straight up to this many cells ahead, where the straight line costs no more than
// this many thousandths of the path it replaces (so a pull never climbs what the path went round)
const Int ROAD_PULL_REACH = 64;
const Int ROAD_PULL_SLACK = 1100;
// a bend kept closer than this to where two roads meet folds into the meeting point: a sliver shorter
// than half a road's width turns the renderer's tee inside out
const Real ROAD_JOIN_SNAP = 3.0f;
// how far from where it was cut a road looks for the road it ran into, in cells
const Int ROAD_JOIN_REACH = 6;
// a copied road end flagged to join another road, with none this near, is a stub left in the open
const Real ROAD_STUB_REACH = 3.0f;
const Real COAST_EDGE = 0.4f;
const Real FULL_EDGE = 0.9f;
const Real RIVER_SLOPE = 3.0f;
const Int TOWN_MARGIN = 10;
const Int TOWN_STEP = 8;
const Int TOWN_RELIEF = 50;
const Int TOWN_LEAST = 30;				// cells a side, the least of a town cut down to fit
const Int PATH_CELLS = 4;
// what the quilt matches a cell on beside its height, a bit each (Exemplar::ground): the target's
// GROUND_STRIP is a planned road's clearance, the exemplar's a road strip's texture class
const UnsignedByte GROUND_CLIFF = 1, GROUND_WET = 2, GROUND_SHORE = 4, GROUND_STRIP = 8;
// a mismatch's price in quarters of a cliff's: a sea laid on land's ground, bank sand off the shore,
// and a road strip where no road runs (one way: plain ground under a new road costs nothing)
const Int PRICE_CLIFF = 4, PRICE_WET = 4, PRICE_SHORE = 2, PRICE_STRIP = 4;
const Int SHORE_GROUND = 4;				// cells off its water a bank's ground lies: Heartland Shield's bank sand in 2849 of 2855 cells
const Int STRIP_LIFT = 4;				// a class this many times as common along the roads as over the map is a road's strip
// cells round a start on a template whose bases are not raised that the quilt never copies: that
// ground is the flattest there is, so a flat map matched it everywhere (Bombardment Beach's start dirt
// on 43% of its maps against 12% of it); a pad brings it along. PAD_INNER: 34 left Bombardment's eight
// seats too little of it, 9% against the template's 12%
const Int BASE_GROUND = 24;
const Int BRIDGE_CLEAR = 6;
// cells from a cell of the wrong steepness to the exemplar's cell whose texture it takes instead: past
// it a new cliff takes the exemplar's commonest cliff texture and new flat ground keeps its own
const Int TEXTURE_REACH = 16;
const Int CLUMP_CELLS = 5;
const Int TREE_TRIES = 40;

// mapfeatures.py
const Int CANYON_DROP = 25;
const Int CANYON_CELLS = 400;
const Real LONG_SHAPE = 3.0f;
const Int MESA_RISE = 25;
const Int MESA_CELLS = 300;
const Real MESA_CLIFF = 0.3f;
const Int HIGH_RISE = 40;
const Int TOWN_GAP = 20;
const Int TOWN_BUILDINGS = 5;
const Real SEA_SHARE = 0.9f;
const Real SEA_WET = 0.3f;
const Int JOIN_CELLS = 3;
const Int EXIT_CELLS = 4;

// mapstyle.py
const Int PLATEAU_TOLERANCE = 6;

const Real MAP_XY = 10.0f;					// MAP_XY_FACTOR
const Real MAP_Z = 10.0f / 16.0f;			// MAP_HEIGHT_SCALE
const Real CLIFF_SPAN = 9.8f;				// world units across a cell's corners that make it a cliff
const Real PI_R = 3.14159265358979f;
const Int RMG_NUM_BLEND_TILES = 16192;

//-----------------------------------------------------------------------------
// Little things
//-----------------------------------------------------------------------------

typedef std::vector<Real> Field;
typedef std::vector<UnsignedByte> Mask;

struct YX
{
	Real y, x;
};

inline YX yx( Real y, Real x )
{
	YX p;
	p.y = y;
	p.x = x;
	return p;
}

/// RandomMapGenerator.cpp's hashCell, so the template draws from the same kind of numbers.
UnsignedInt synthHash( Int seed, Int x, Int y )
{
	UnsignedInt h = (UnsignedInt)seed * 374761393U;
	h += (UnsignedInt)x * 668265263U;
	h ^= h >> 13;
	h += (UnsignedInt)y * 2246822519U;
	h ^= h >> 16;
	h *= 2654435761U;
	return h ^ (h >> 15);
}

Real hashUnit( Int seed, Int x, Int y )
{
	return (Real)( (double)synthHash( seed, x, y ) / 4294967296.0 );
}

inline Real fade( Real x )
{
	if( x < 0.0f ) x = 0.0f;
	if( x > 1.0f ) x = 1.0f;
	return x * x * ( 3.0f - 2.0f * x );
}

inline Int clampi( Int v, Int lo, Int hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

inline Real clampr( Real v, Real lo, Real hi )
{
	return v < lo ? lo : ( v > hi ? hi : v );
}

inline Real hypotr( Real a, Real b )
{
	return sqrtf( a * a + b * b );
}

/// An angle's distance from another, either way round.
Real turnBetween( Real a, Real b )
{
	Real d = a - b;
	d -= 2.0f * PI_R * floorf( d / ( 2.0f * PI_R ) + 0.5f );
	return fabsf( d );
}

/// numpy's rint for the heights: halves to even is not worth reproducing, halves go up
inline Int roundi( Real v )
{
	return (Int)floorf( v + 0.5f );
}

/// numpy's median: the middle value, or the mean of the two middle ones
Real medianOf( std::vector<Real>& v )
{
	if( v.empty() )
		return 0.0f;
	size_t mid = v.size() / 2;
	std::nth_element( v.begin(), v.begin() + mid, v.end() );
	Real upper = v[mid];
	if( v.size() % 2 )
		return upper;
	Real lower = *std::max_element( v.begin(), v.begin() + mid );
	return 0.5f * ( lower + upper );
}

/// Thrown by every read past the end of a map's bytes; the synthesis refuses the template.
struct ShortMap { Int pos, count, size; };

/// n bytes at pos into out (only checked when out is NULL), or ShortMap when the bytes end first.
void readBytes( const std::vector<char>& d, Int pos, void *out, Int n )
{
	if( pos < 0 || n < 0 || (size_t)pos + (size_t)n > d.size() )
	{
		ShortMap fault = { pos, n, (Int)d.size() };
		throw fault;
	}
	if( out && n > 0 )
		memcpy( out, &d[pos], n );
}

Int readInt( const std::vector<char>& d, Int pos ) { Int v; readBytes( d, pos, &v, 4 ); return v; }
Real readReal( const std::vector<char>& d, Int pos ) { Real v; readBytes( d, pos, &v, 4 ); return v; }
UnsignedShort readU16( const std::vector<char>& d, Int pos ) { UnsignedShort v; readBytes( d, pos, &v, 2 ); return v; }

/// An ascii string at pos (a short length, then the bytes); returns where it ends.
Int readAscii( const std::vector<char>& d, Int pos, AsciiString *out )
{
	Int length = readU16( d, pos );
	if( pos + 2 + length > (Int)d.size() )
	{
		ShortMap fault = { pos + 2, length, (Int)d.size() };
		throw fault;
	}
	if( out )
	{
		std::string text( &d[0] + pos + 2, length );
		*out = text.c_str();
	}
	return pos + 2 + length;
}

void putBytes( std::vector<char>& out, const void *p, Int n ) { const char *c = (const char *)p; out.insert( out.end(), c, c + n ); }
void putInt( std::vector<char>& out, Int v ) { putBytes( out, &v, 4 ); }
void putReal( std::vector<char>& out, Real v ) { putBytes( out, &v, 4 ); }
void putU16( std::vector<char>& out, UnsignedShort v ) { putBytes( out, &v, 2 ); }
void putByte( std::vector<char>& out, UnsignedByte v ) { putBytes( out, &v, 1 ); }
void putAscii( std::vector<char>& out, const char *s ) { UnsignedShort n = (UnsignedShort)strlen( s ); putU16( out, n ); putBytes( out, s, n ); }

//-----------------------------------------------------------------------------
// Grids
//-----------------------------------------------------------------------------

/// Mean over a (2r+1) square, edges clamped (synthmap.box).
void boxMean( const Field& a, Int W, Int H, Int r, Field& out )
{
	Field across( a.size() );
	const double inv = 1.0 / (double)( 2 * r + 1 );
	for( Int y = 0; y < H; y++ )
	{
		const Real *row = &a[y * W];
		double sum = 0.0;
		for( Int k = -r; k <= r; k++ )
			sum += row[clampi( k, 0, W - 1 )];
		for( Int x = 0; x < W; x++ )
		{
			across[y * W + x] = (Real)( sum * inv );
			sum += row[clampi( x + r + 1, 0, W - 1 )] - row[clampi( x - r, 0, W - 1 )];
		}
	}
	out.resize( a.size() );
	for( Int x = 0; x < W; x++ )
	{
		double sum = 0.0;
		for( Int k = -r; k <= r; k++ )
			sum += across[clampi( k, 0, H - 1 ) * W + x];
		for( Int y = 0; y < H; y++ )
		{
			out[y * W + x] = (Real)( sum * inv );
			sum += across[clampi( y + r + 1, 0, H - 1 ) * W + x] - across[clampi( y - r, 0, H - 1 ) * W + x];
		}
	}
}

void maskToField( const Mask& m, Field& out )
{
	out.resize( m.size() );
	for( size_t i = 0; i < m.size(); i++ )
		out[i] = m[i] ? 1.0f : 0.0f;
}

/// box(m, r) > 0: any cell of the (2r+1) square set.
void maskGrow( const Mask& m, Int W, Int H, Int r, Mask& out )
{
	std::vector<Int> across( m.size() );
	for( Int y = 0; y < H; y++ )
	{
		Int count = 0;
		for( Int k = 0; k <= r && k < W; k++ )
			count += m[y * W + k];
		for( Int x = 0; x < W; x++ )
		{
			across[y * W + x] = count;
			if( x + r + 1 < W ) count += m[y * W + x + r + 1];
			if( x - r >= 0 ) count -= m[y * W + x - r];
		}
	}
	out.assign( m.size(), 0 );
	for( Int x = 0; x < W; x++ )
	{
		Int count = 0;
		for( Int k = 0; k <= r && k < H; k++ )
			count += across[k * W + x];
		for( Int y = 0; y < H; y++ )
		{
			out[y * W + x] = count > 0;
			if( y + r + 1 < H ) count += across[( y + r + 1 ) * W + x];
			if( y - r >= 0 ) count -= across[( y - r ) * W + x];
		}
	}
}

/// WorldHeightMapData::initCliffFlagsFromHeights: a cell whose corners differ by more than the cliff span.
void spanCliff( const Field& h, Int W, Int H, Mask& out )
{
	out.assign( h.size(), 0 );
	for( Int y = 0; y < H - 1; y++ )
	{
		for( Int x = 0; x < W - 1; x++ )
		{
			Real a = h[y * W + x], b = h[y * W + x + 1], c = h[( y + 1 ) * W + x], d = h[( y + 1 ) * W + x + 1];
			Real lo = std::min( std::min( a, b ), std::min( c, d ) );
			Real hi = std::max( std::max( a, b ), std::max( c, d ) );
			out[y * W + x] = ( hi - lo ) * MAP_Z > CLIFF_SPAN;
		}
	}
}

/// The pathfinder's growth of every cliff by a cell all round; what is left is walkable.
void walkable( const Mask& cliff, Int W, Int H, Mask& out )
{
	Mask grown;
	maskGrow( cliff, W, H, 1, grown );
	out.resize( cliff.size() );
	for( size_t i = 0; i < cliff.size(); i++ )
		out[i] = !grown[i];
}

/// Cells walkable from one cell, four ways.
void reach( const Mask& passable, Int W, Int H, Int start, Mask& seen )
{
	seen.assign( passable.size(), 0 );
	if( !passable[start] )
	{
		seen[start] = 1;
		return;
	}
	std::vector<Int> queue;
	queue.reserve( passable.size() / 4 );
	queue.push_back( start );
	seen[start] = 1;
	for( size_t head = 0; head < queue.size(); head++ )
	{
		Int i = queue[head];
		Int x = i % W, y = i / W;
		const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
		for( Int k = 0; k < 4; k++ )
		{
			if( n[k] >= 0 && passable[n[k]] && !seen[n[k]] )
			{
				seen[n[k]] = 1;
				queue.push_back( n[k] );
			}
		}
	}
}

/// Each cell's nearest seed's class, four-way steps and no further than reach, else -1: one flood from
/// every seed at once, the seeds in raster order, and the first to come to a cell keeps it.
void nearestClass( const Mask& seed, const std::vector<Int>& cls, Int W, Int H, Int reach, std::vector<Int>& out )
{
	out.assign( W * H, -1 );
	std::vector<Int> steps( W * H, -1 ), queue;
	queue.reserve( W * H );
	for( Int i = 0; i < W * H; i++ )
	{
		if( !seed[i] ) continue;
		out[i] = cls[i];
		steps[i] = 0;
		queue.push_back( i );
	}
	for( size_t head = 0; head < queue.size(); head++ )
	{
		Int i = queue[head];
		if( steps[i] >= reach ) continue;
		Int x = i % W, y = i / W;
		const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
		for( Int k = 0; k < 4; k++ )
		{
			if( n[k] >= 0 && steps[n[k]] < 0 )
			{
				steps[n[k]] = steps[i] + 1;
				out[n[k]] = out[i];
				queue.push_back( n[k] );
			}
		}
	}
}

/// scipy's label: four-way pieces of the set, numbered from 1 in raster order of their first cell.
Int labelPieces( const Mask& m, Int W, Int H, std::vector<Int>& labels )
{
	labels.assign( m.size(), 0 );
	Int count = 0;
	std::vector<Int> queue;
	for( Int s = 0; s < W * H; s++ )
	{
		if( !m[s] || labels[s] )
			continue;
		count++;
		queue.clear();
		queue.push_back( s );
		labels[s] = count;
		for( size_t head = 0; head < queue.size(); head++ )
		{
			Int i = queue[head];
			Int x = i % W, y = i / W;
			const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
			for( Int k = 0; k < 4; k++ )
			{
				if( n[k] >= 0 && m[n[k]] && !labels[n[k]] )
				{
					labels[n[k]] = count;
					queue.push_back( n[k] );
				}
			}
		}
	}
	return count;
}

/// binary_dilation with the cross, k times: every cell k four-way steps or fewer from the set.
void dilateCross( const Mask& m, Int W, Int H, Int k, Mask& out )
{
	out = m;
	std::vector<Int> frontier, next;
	for( Int i = 0; i < W * H; i++ )
		if( m[i] )
			frontier.push_back( i );
	for( Int step = 0; step < k; step++ )
	{
		next.clear();
		for( size_t f = 0; f < frontier.size(); f++ )
		{
			Int i = frontier[f];
			Int x = i % W, y = i / W;
			const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
			for( Int j = 0; j < 4; j++ )
			{
				if( n[j] >= 0 && !out[n[j]] )
				{
					out[n[j]] = 1;
					next.push_back( n[j] );
				}
			}
		}
		frontier.swap( next );
	}
}

/// synthmap.line_cells, ORed into out.
void lineCells( YX a, YX z, Int W, Int H, Mask& out )
{
	Int steps = (Int)hypotr( z.y - a.y, z.x - a.x );
	if( steps < 1 ) steps = 1;
	for( Int s = 0; s <= steps; s++ )
	{
		Int y = (Int)( a.y + ( z.y - a.y ) * s / steps );
		Int x = (Int)( a.x + ( z.x - a.x ) * s / steps );
		out[clampi( y, 0, H - 1 ) * W + clampi( x, 0, W - 1 )] = 1;
	}
}

/// Distance from a cell to the nearest set cell, or a large number for an empty set.
Real distanceToSet( const std::vector<Int>& setCells, Int W, Int y, Int x )
{
	Int best = 0x7fffffff;
	for( size_t k = 0; k < setCells.size(); k++ )
	{
		Int dy = setCells[k] / W - y, dx = setCells[k] % W - x;
		Int d2 = dy * dy + dx * dx;
		if( d2 < best ) best = d2;
	}
	return best == 0x7fffffff ? 1.0e9f : sqrtf( (Real)best );
}

/// A segment's distance from a point.
Real segmentDistance( Real py, Real px, YX a, YX z )
{
	Real dy = z.y - a.y, dx = z.x - a.x;
	Real len2 = dy * dy + dx * dx;
	Real t = len2 > 0.0f ? ( ( py - a.y ) * dy + ( px - a.x ) * dx ) / len2 : 0.0f;
	t = clampr( t, 0.0f, 1.0f );
	return hypotr( py - ( a.y + t * dy ), px - ( a.x + t * dx ) );
}

//-----------------------------------------------------------------------------
// The CkMp file as names and chunks
//-----------------------------------------------------------------------------

struct Chunk
{
	UnsignedInt id;
	UnsignedShort version;
	std::vector<char> body;
};

void splitChunks( const std::vector<char>& data, Int pos, Int end, std::vector<Chunk>& out )
{
	out.clear();
	while( pos + 10 <= end )
	{
		Chunk c;
		readBytes( data, pos, &c.id, 4 );
		readBytes( data, pos + 4, &c.version, 2 );
		Int size = readInt( data, pos + 6 );
		c.body.resize( size < 0 ? 0 : size );
		readBytes( data, pos + 10, c.body.empty() ? NULL : &c.body[0], size );
		out.push_back( c );
		pos += 10 + size;
	}
}

void joinChunks( const std::vector<Chunk>& chunks, std::vector<char>& out )
{
	for( size_t i = 0; i < chunks.size(); i++ )
	{
		putBytes( out, &chunks[i].id, 4 );
		putBytes( out, &chunks[i].version, 2 );
		putInt( out, (Int)chunks[i].body.size() );
		if( !chunks[i].body.empty() )
			putBytes( out, &chunks[i].body[0], (Int)chunks[i].body.size() );
	}
}

struct RawMap
{
	std::vector<AsciiString> names;
	std::vector<UnsignedInt> ids;
	std::vector<Chunk> chunks;

	Bool parse( const std::vector<char>& data )
	{
		if( data.size() < 8 || memcmp( &data[0], "CkMp", 4 ) != 0 )
			return FALSE;
		Int count = readInt( data, 4 );
		Int pos = 8;
		for( Int i = 0; i < count; i++ )
		{
			UnsignedByte length;
			readBytes( data, pos, &length, 1 );
			char text[256];
			readBytes( data, pos + 1, text, length );
			text[length] = 0;
			pos += 1 + length;
			UnsignedInt id;
			readBytes( data, pos, &id, 4 );
			pos += 4;
			names.push_back( AsciiString( text ) );
			ids.push_back( id );
		}
		splitChunks( data, pos, (Int)data.size(), chunks );
		return TRUE;
	}

	AsciiString nameOf( UnsignedInt id ) const
	{
		for( size_t i = 0; i < ids.size(); i++ )
			if( ids[i] == id )
				return names[i];
		return AsciiString::TheEmptyString;
	}

	/// The id of a name, a new one past the largest if the table has none.
	UnsignedInt idOf( const char *name )
	{
		UnsignedInt largest = 0;
		for( size_t i = 0; i < ids.size(); i++ )
		{
			if( names[i] == name )
				return ids[i];
			largest = std::max( largest, ids[i] );
		}
		names.push_back( AsciiString( name ) );
		ids.push_back( largest + 1 );
		return largest + 1;
	}

	Chunk *chunk( const char *name )
	{
		for( size_t i = 0; i < chunks.size(); i++ )
			if( nameOf( chunks[i].id ) == name )
				return &chunks[i];
		return NULL;
	}

	void bytes( std::vector<char>& out ) const
	{
		out.clear();
		putBytes( out, "CkMp", 4 );
		putInt( out, (Int)names.size() );
		std::vector< std::pair<UnsignedInt, Int> > order;
		for( size_t i = 0; i < ids.size(); i++ )
			order.push_back( std::make_pair( ids[i], (Int)i ) );
		std::sort( order.begin(), order.end() );
		for( size_t k = 0; k < order.size(); k++ )
		{
			const AsciiString& name = names[order[k].second];
			putByte( out, (UnsignedByte)name.getLength() );
			putBytes( out, name.str(), name.getLength() );
			putBytes( out, &order[k].first, 4 );
		}
		joinChunks( chunks, out );
	}
};

/// A Dict's entries as where their values sit, and where the dict ends.
struct DictEntry
{
	UnsignedInt key;
	Int type;
	Int pos;		///< of the value
	Int size;
};

Int readDict( const std::vector<char>& body, Int pos, std::vector<DictEntry>& out )
{
	out.clear();
	Int count = readU16( body, pos );
	pos += 2;
	for( Int i = 0; i < count; i++ )
	{
		UnsignedInt keyAndType;
		readBytes( body, pos, &keyAndType, 4 );
		pos += 4;
		DictEntry e;
		e.key = keyAndType >> 8;
		e.type = keyAndType & 0xff;
		e.pos = pos;
		if( e.type == Dict::DICT_BOOL ) e.size = 1;
		else if( e.type == Dict::DICT_INT || e.type == Dict::DICT_REAL ) e.size = 4;
		else if( e.type == Dict::DICT_ASCIISTRING ) e.size = 2 + readU16( body, pos );
		else e.size = 2 + 2 * readU16( body, pos );
		pos += e.size;
		readBytes( body, e.pos, NULL, e.size );
		out.push_back( e );
	}
	return pos;
}

//-----------------------------------------------------------------------------
// A map, read the way the game reads a shipped one (mapstyle.StyleMap)
//-----------------------------------------------------------------------------

struct TexClass
{
	AsciiString name;
	Int first, count, width;
};

struct WaterArea
{
	std::vector<YX> points;		///< playable cells
	Real surface;				///< world units
};

struct MapThing
{
	AsciiString name;			///< the template
	Real x, y;					///< playable cells
	Real angle;
	Int flags;
	Int chunk;					///< in the ObjectsList chunk's own list
	Int waypointID;				///< 0 for none
	AsciiString waypointName;
};

struct SynthMap
{
	RawMap raw;
	Int W, H, border, playX, playY, playable;
	std::vector<UnsignedByte> heights;
	Int blendVersion;
	std::vector<Short> tiles, blends, extras;
	Mask cliff;					///< painted bits, or the heights' span where the file has none
	Int tailPos;				///< where the texture table starts in BlendTileData's body
	std::vector<TexClass> classes;
	std::vector<YX> starts;		///< playable cells, by player
	std::vector<YX> supplies, piles, derricks;
	std::vector<WaterArea> water;
	std::vector<MapThing> things;	///< every Object, in file order
	std::vector<Chunk> objectChunks;
	Int weather;

	Int idx( Int y, Int x ) const { return y * W + x; }

	Bool load( const std::vector<char>& data )
	{
		if( !raw.parse( data ) )
			return FALSE;
		weather = 0;
		blendVersion = 0;
		Bool haveHeights = FALSE;
		std::vector< std::pair<Int, YX> > numbered;

		for( size_t c = 0; c < raw.chunks.size(); c++ )
		{
			const Chunk& chunk = raw.chunks[c];
			AsciiString name = raw.nameOf( chunk.id );
			const std::vector<char>& b = chunk.body;
			if( name == "HeightMapData" )
			{
				Int v = chunk.version, pos = 0;
				W = readInt( b, 0 );
				H = readInt( b, 4 );
				pos = 8;
				border = 0;
				if( v >= 3 ) { border = readInt( b, pos ); pos += 4; }
				playX = W - 2 * border;
				playY = H - 2 * border;
				if( v >= 4 )
				{
					Int n = readInt( b, pos );
					pos += 4;
					if( n > 0 )
					{
						playX = readInt( b, pos );
						playY = readInt( b, pos + 4 );
					}
					pos += 8 * n;
				}
				Int size = readInt( b, pos );
				pos += 4;
				if( W <= 0 || H <= 0 || W > MAP_SIDE_MAX || H > MAP_SIDE_MAX || size != W * H || playX <= 0 || playY <= 0 )
					return FALSE;
				readBytes( b, pos, NULL, size );
				heights.assign( (const UnsignedByte *)&b[pos], (const UnsignedByte *)&b[pos] + size );
				playable = std::min( playX, playY );
				haveHeights = TRUE;
			}
		}
		if( !haveHeights )
			return FALSE;

		for( size_t c = 0; c < raw.chunks.size(); c++ )
		{
			const Chunk& chunk = raw.chunks[c];
			AsciiString name = raw.nameOf( chunk.id );
			const std::vector<char>& b = chunk.body;
			if( name == "BlendTileData" )
			{
				Int v = chunk.version;
				blendVersion = v;
				Int size = readInt( b, 0 );
				Int pos = 4;
				if( size != W * H || v < 5 )
					return FALSE;
				tiles.resize( size );
				blends.resize( size );
				extras.assign( size, 0 );
				readBytes( b, pos, &tiles[0], size * 2 ); pos += size * 2;
				readBytes( b, pos, &blends[0], size * 2 ); pos += size * 2;
				if( v >= 6 ) { readBytes( b, pos, &extras[0], size * 2 ); pos += size * 2; }
				pos += size * 2;						// cliff info
				cliff.assign( size, 0 );
				if( v >= 7 )
				{
					Int row = v == 7 ? ( W + 1 ) / 8 : ( W + 7 ) / 8;
					readBytes( b, pos, NULL, row * H );
					for( Int y = 0; y < H; y++ )
						for( Int x = 0; x < W && ( x >> 3 ) < row; x++ )
							cliff[y * W + x] = ( (UnsignedByte)b[pos + y * row + ( x >> 3 )] >> ( x & 7 ) ) & 1;
					pos += row * H;
				}
				else
				{
					Field hf( heights.begin(), heights.end() );
					spanCliff( hf, W, H, cliff );
				}
				tailPos = pos;
				pos += 4 + 4 + 4;					// bitmap tiles, blended tiles, cliff infos
				Int count = readInt( b, pos );
				pos += 4;
				for( Int i = 0; i < count; i++ )
				{
					TexClass t;
					t.first = readInt( b, pos );
					t.count = readInt( b, pos + 4 );
					pos += 16;
					pos = readAscii( b, pos, &t.name );
					t.width = (Int)sqrtf( (Real)t.count + 0.5f );
					classes.push_back( t );
				}
			}
			else if( name == "WorldInfo" )
			{
				std::vector<DictEntry> entries;
				readDict( b, 0, entries );
				for( size_t i = 0; i < entries.size(); i++ )
					if( raw.nameOf( entries[i].key ) == "weather" && entries[i].type == Dict::DICT_INT )
						weather = readInt( b, entries[i].pos );
			}
			else if( name == "ObjectsList" )
			{
				splitChunks( b, 0, (Int)b.size(), objectChunks );
				for( size_t o = 0; o < objectChunks.size(); o++ )
				{
					const std::vector<char>& ob = objectChunks[o].body;
					if( raw.nameOf( objectChunks[o].id ) != "Object" )
						continue;
					MapThing t;
					t.x = readReal( ob, 0 ) / MAP_XY;
					t.y = readReal( ob, 4 ) / MAP_XY;
					t.angle = readReal( ob, 12 );
					t.flags = readInt( ob, 16 );
					Int end = readAscii( ob, 20, &t.name );
					t.chunk = (Int)o;
					t.waypointID = 0;
					if( objectChunks[o].version >= 2 )
					{
						std::vector<DictEntry> entries;
						readDict( ob, end, entries );
						for( size_t i = 0; i < entries.size(); i++ )
						{
							AsciiString key = raw.nameOf( entries[i].key );
							if( key == "waypointID" )
								t.waypointID = readInt( ob, entries[i].pos );
							else if( key == "waypointName" && entries[i].type == Dict::DICT_ASCIISTRING )
								readAscii( ob, entries[i].pos, &t.waypointName );
						}
					}
					things.push_back( t );

					if( t.waypointID )
					{
						Int player = 0;
						if( sscanf( t.waypointName.str(), "Player_%d_Start", &player ) == 1 && t.waypointName.endsWith( "_Start" ) )
							numbered.push_back( std::make_pair( player, yx( t.y, t.x ) ) );
						continue;
					}
					AsciiString lower = t.name;
					lower.toLower();
					if( strstr( lower.str(), "supplydock" ) )
						supplies.push_back( yx( t.y, t.x ) );
					else if( strstr( lower.str(), "supplypile" ) )
						piles.push_back( yx( t.y, t.x ) );
					else if( lower == "techoilderrick" )
						derricks.push_back( yx( t.y, t.x ) );
				}
			}
			else if( name == "PolygonTriggers" )
			{
				Int v = chunk.version;
				Int count = readInt( b, 0 );
				Int pos = 4;
				for( Int i = 0; i < count; i++ )
				{
					pos = readAscii( b, pos, NULL );
					if( v >= 4 )
						pos = readAscii( b, pos, NULL );
					pos += 4;
					Bool isWater = FALSE;
					if( v >= 2 ) { UnsignedByte flag; readBytes( b, pos, &flag, 1 ); isWater = flag != 0; pos += 1; }
					if( v >= 3 ) pos += 5;
					Int points = readInt( b, pos );
					pos += 4;
					if( isWater && points > 2 )
					{
						WaterArea area;
						area.surface = -1.0e9f;
						for( Int p = 0; p < points; p++ )
						{
							Int px = readInt( b, pos + 12 * p ), py = readInt( b, pos + 12 * p + 4 ), pz = readInt( b, pos + 12 * p + 8 );
							area.points.push_back( yx( py / MAP_XY, px / MAP_XY ) );
							area.surface = std::max( area.surface, (Real)pz );
						}
						water.push_back( area );
					}
					pos += 12 * points;
				}
			}
		}
		std::stable_sort( numbered.begin(), numbered.end(),
			[]( const std::pair<Int, YX>& a, const std::pair<Int, YX>& b ) { return a.first < b.first; } );
		for( size_t i = 0; i < numbered.size(); i++ )
			starts.push_back( numbered[i].second );
		return blendVersion != 0 && starts.size() >= 2;
	}

	/// mapstyle.water_grid over the given areas: cell centres inside an area whose ground is under its surface.
	void waterGrid( const std::vector<WaterArea>& areas, Mask& out ) const
	{
		out.assign( W * H, 0 );
		std::vector<Real> crossings;
		for( size_t a = 0; a < areas.size(); a++ )
		{
			const std::vector<YX>& p = areas[a].points;
			Real lo = 1.0e9f, hi = -1.0e9f;
			for( size_t i = 0; i < p.size(); i++ ) { lo = std::min( lo, p[i].y ); hi = std::max( hi, p[i].y ); }
			Int from = std::max( 0, (Int)lo ), to = std::min( H - border, (Int)hi + 1 );
			for( Int cy = from; cy < to; cy++ )
			{
				Real py = cy + 0.5f;
				crossings.clear();
				for( size_t i = 0; i < p.size(); i++ )
				{
					const YX& a1 = p[( i + p.size() - 1 ) % p.size()];
					const YX& a2 = p[i];
					if( ( a1.y > py ) != ( a2.y > py ) )
						crossings.push_back( a1.x + ( py - a1.y ) * ( a2.x - a1.x ) / ( a2.y - a1.y ) );
				}
				std::sort( crossings.begin(), crossings.end() );
				for( size_t k = 0; k + 1 < crossings.size(); k += 2 )
				{
					Int x0 = std::max( 0, (Int)ceilf( crossings[k] - 0.5f ) );
					Int x1 = (Int)floorf( crossings[k + 1] - 0.5f );
					for( Int cx = x0; cx <= x1; cx++ )
					{
						Int gx = cx + border, gy = cy + border;
						if( gx >= W || gy >= H )
							continue;
						if( heights[gy * W + gx] * MAP_Z < areas[a].surface )
							out[gy * W + gx] = 1;
					}
				}
			}
		}
	}

	/// The playable rectangle.
	void playMask( Mask& out ) const
	{
		out.assign( W * H, 0 );
		for( Int y = border; y < border + playY && y < H; y++ )
			for( Int x = border; x < border + playX && x < W; x++ )
				out[y * W + x] = 1;
	}
};

/// Pathfinder::classifyMapCell's call and the growth after it, with the water: what a tank walks on.
void passableGrid( const SynthMap& m, const Mask& wet, Mask& out )
{
	Mask grown;
	maskGrow( m.cliff, m.W, m.H, 1, grown );
	out.assign( m.W * m.H, 0 );
	for( Int y = 0; y < m.H - 1; y++ )
		for( Int x = 0; x < m.W - 1; x++ )
			out[y * m.W + x] = !wet[y * m.W + x] && !grown[y * m.W + x];
}

//-----------------------------------------------------------------------------
// A map's two levels (synthmap.Grid)
//-----------------------------------------------------------------------------

struct Levels
{
	std::vector<Int> startY, startX;	///< grid cells: int(y + 0.5) + border
	Real plateau, bowl, scale;
	Real cy, cx;
	Field h, t, guide;

	void init( const SynthMap& m )
	{
		h.assign( m.heights.begin(), m.heights.end() );
		for( size_t i = 0; i < m.starts.size(); i++ )
		{
			startY.push_back( (Int)( m.starts[i].y + 0.5f ) + m.border );
			startX.push_back( (Int)( m.starts[i].x + 0.5f ) + m.border );
		}
		std::vector<Real> medians, around;
		for( size_t i = 0; i < startY.size(); i++ )
		{
			around.clear();
			for( Int y = startY[i] - 3; y <= startY[i] + 3; y++ )
				for( Int x = startX[i] - 3; x <= startX[i] + 3; x++ )
					around.push_back( h[clampi( y, 0, m.H - 1 ) * m.W + clampi( x, 0, m.W - 1 )] );
			medians.push_back( medianOf( around ) );
		}
		plateau = medianOf( medians );
		cy = m.border + m.playY / 2.0f;
		cx = m.border + m.playX / 2.0f;
		std::vector<Real> middle;
		for( Int y = 0; y < m.H; y++ )
			for( Int x = 0; x < m.W; x++ )
				if( hypotr( x + 0.5f - cx, y + 0.5f - cy ) <= 0.25f * m.playable )
					middle.push_back( h[y * m.W + x] );
		bowl = medianOf( middle );
		if( plateau - bowl <= MIN_RELIEF )
		{
			// bases not raised: the ground level is the map's, not its middle's
			std::vector<Real> play;
			for( Int y = m.border; y < m.border + m.playY; y++ )
				for( Int x = m.border; x < m.border + m.playX; x++ )
					play.push_back( h[y * m.W + x] );
			bowl = medianOf( play );
		}
		scale = std::max( plateau - bowl, MIN_RELIEF );
		setHeights( h, m.W, m.H, FALSE );
	}

	void setHeights( const Field& heights, Int W, Int H, Bool round )
	{
		h = heights;
		if( round )
			for( size_t i = 0; i < h.size(); i++ )
				h[i] = (Real)clampi( roundi( h[i] ), 0, 255 );
		t.resize( h.size() );
		for( size_t i = 0; i < h.size(); i++ )
			t[i] = clampr( ( h[i] - bowl ) / scale, -0.6f, 2.2f );
		boxMean( t, W, H, 2, guide );
	}
};

//-----------------------------------------------------------------------------
// The inventory (mapfeatures.inventory, and what of mapstyle.profile it reads)
//-----------------------------------------------------------------------------

enum WaterKind { WATER_RIVER, WATER_LAKE, WATER_SEA, WATER_COAST };

struct InvWater
{
	Int kind;
	Int area;
	Real length, width, winding;
	Real depth, bank, surface;		///< bytes
	Real centreShare, nearest, startClear, share;
	Bool reachesEdge;
	Real edges[4];					///< low y, high x, high y, low x
	Int joins;						///< the longer river this one runs into, or -1
};

struct InvCanyon { Real floorDz, width, length, winding, centreShare; Bool reachesEdge; Int arms; };
struct InvMesa { Int area; Real radius, topDz, centreShare; Int exits; Mask near; };
struct InvBridge { AsciiString name; Real length; Int over; Real endsDz[2]; Real centreShare; Int onMesa; Int water; };
struct InvRoad { AsciiString name; Int segments; Real length, outsideLength, segmentLength; Int flags[2]; };
struct InvTown { Int buildings; Real box[4]; Real centreShare; std::vector<YX> houses; };	///< houses: grid cells, border in
struct InvTech { AsciiString name; Real centreShare; Int onMesa; };

enum { OVER_WATER, OVER_GAP, OVER_GROUND };

struct Inventory
{
	Real startH, middleH;
	Bool hasHigh;
	Real highDz, highShare;
	std::vector<InvCanyon> canyons;
	std::vector<InvMesa> mesas;
	std::vector<InvWater> water;
	std::vector<InvBridge> bridges;
	std::vector<InvRoad> roads;
	std::vector<InvTown> towns;
	std::vector<InvTech> tech;
	Int pileCount;
	Real pileDz;
	std::vector<AsciiString> pileCompany;	///< what stands with half the piles or more
	Bool split;
	Int roadExits;
	Mask roadLines;		///< the cells every road segment's line crosses, towns' streets too
};

/// Length along the cells' main axis, mean width, and how far the centre line wanders (mapfeatures.shape).
void shapeOf( const std::vector<Int>& cells, Int W, Real *lengthOut, Real *widthOut, Real *windingOut )
{
	Int n = (Int)cells.size();
	double mx = 0, my = 0;
	for( Int i = 0; i < n; i++ ) { mx += cells[i] % W; my += cells[i] / W; }
	mx /= n; my /= n;
	double sxx = 0, syy = 0, sxy = 0;
	for( Int i = 0; i < n; i++ )
	{
		double dx = cells[i] % W - mx, dy = cells[i] / W - my;
		sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
	}
	double a = sxx / std::max( n - 1, 1 ), c = syy / std::max( n - 1, 1 ), b = sxy / std::max( n - 1, 1 );
	double lambda = 0.5 * ( a + c ) + sqrt( 0.25 * ( a - c ) * ( a - c ) + b * b );
	double ax = b, ay = lambda - a;
	if( fabs( ax ) + fabs( ay ) < 1e-12 ) { ax = lambda - c; ay = b; }
	if( fabs( ax ) + fabs( ay ) < 1e-12 ) { ax = 1.0; ay = 0.0; }
	double norm = sqrt( ax * ax + ay * ay );
	ax /= norm; ay /= norm;
	double lo = 1e30, hi = -1e30;
	std::vector<double> along( n ), across( n );
	for( Int i = 0; i < n; i++ )
	{
		double dx = cells[i] % W - mx, dy = cells[i] / W - my;
		along[i] = dx * ax + dy * ay;
		across[i] = -dx * ay + dy * ax;
		lo = std::min( lo, along[i] );
		hi = std::max( hi, along[i] );
	}
	double length = hi - lo + 1.0;
	double sums[10] = { 0 };
	Int counts[10] = { 0 };
	for( Int i = 0; i < n; i++ )
	{
		Int bin = clampi( (Int)( ( along[i] - lo ) / length * 10.0 ), 0, 9 );
		sums[bin] += across[i];
		counts[bin]++;
	}
	double mean = 0, sq = 0;
	Int bins = 0;
	for( Int k = 0; k < 10; k++ )
		if( counts[k] ) { double v = sums[k] / counts[k]; mean += v; sq += v * v; bins++; }
	mean /= std::max( bins, 1 );
	double var = sq / std::max( bins, 1 ) - mean * mean;
	*lengthOut = (Real)length;
	*widthOut = (Real)( n / length );
	*windingOut = (Real)sqrt( std::max( var, 0.0 ) );
}

struct Places
{
	Real cy, cx, half;
	std::vector<YX> starts;	///< grid cells
	void where( Real y, Real x, Real *centreShare, Real *nearest ) const
	{
		*centreShare = hypotr( y - cy, x - cx ) / half;
		Real best = 1.0e9f;
		for( size_t i = 0; i < starts.size(); i++ )
			best = std::min( best, hypotr( y - starts[i].y, x - starts[i].x ) );
		if( nearest ) *nearest = best;
	}
};

typedef Bool (*CivilianTest)( const char *name );

void takeInventory( const SynthMap& m, const Levels& lv, CivilianTest civilian, Inventory& inv )
{
	const Int W = m.W, H = m.H, b = m.border;
	Mask wetAll, grid, play;
	m.waterGrid( m.water, wetAll );
	passableGrid( m, wetAll, grid );
	m.playMask( play );

	Places places;
	places.cy = b + m.playY / 2.0f;
	places.cx = b + m.playX / 2.0f;
	places.half = m.playable / 2.0f;
	for( size_t i = 0; i < m.starts.size(); i++ )
		places.starts.push_back( yx( (Real)lv.startY[i], (Real)lv.startX[i] ) );

	inv.startH = lv.plateau;
	std::vector<Real> middle;
	for( Int y = 0; y < H; y++ )
		for( Int x = 0; x < W; x++ )
			if( play[y * W + x] && hypotr( x + 0.5f - places.cx, y + 0.5f - places.cy ) <= 0.25f * m.playable )
				middle.push_back( m.heights[y * W + x] );
	inv.middleH = medianOf( middle );

	Mask dry( W * H );
	Int dryCount = 0, playCount = 0;
	for( Int i = 0; i < W * H; i++ )
	{
		dry[i] = play[i] && !wetAll[i];
		dryCount += dry[i];
		playCount += play[i];
	}

	// high ground
	std::vector<Real> high;
	for( Int i = 0; i < W * H; i++ )
		if( dry[i] && m.heights[i] >= inv.startH + HIGH_RISE )
			high.push_back( m.heights[i] );
	inv.hasHigh = high.size() > 0.02f * dryCount;
	inv.highShare = (Real)high.size() / std::max( dryCount, 1 );
	inv.highDz = inv.hasHigh ? medianOf( high ) - inv.startH : 0.0f;


	auto reachesEdge = [&]( const std::vector<Int>& cells ) -> Bool
	{
		Int ylo = H, yhi = -1, xlo = W, xhi = -1;
		for( size_t k = 0; k < cells.size(); k++ )
		{
			Int y = cells[k] / W, x = cells[k] % W;
			ylo = std::min( ylo, y ); yhi = std::max( yhi, y ); xlo = std::min( xlo, x ); xhi = std::max( xhi, x );
		}
		return ylo <= b + 2 || xlo <= b + 2 || yhi >= b + m.playY - 3 || xhi >= b + m.playX - 3;
	};
	auto meanOf = [&]( const std::vector<Int>& cells, Real *y, Real *x )
	{
		double sy = 0, sx = 0;
		for( size_t k = 0; k < cells.size(); k++ ) { sy += cells[k] / W; sx += cells[k] % W; }
		*y = (Real)( sy / cells.size() );
		*x = (Real)( sx / cells.size() );
	};

	// canyons: dry floor well below the middle, long and thin
	{
		Mask low( W * H );
		for( Int i = 0; i < W * H; i++ ) low[i] = dry[i] && m.heights[i] < inv.middleH - CANYON_DROP;
		std::vector<Int> labels;
		Int count = labelPieces( low, W, H, labels );
		std::vector< std::vector<Int> > pieces( count + 1 );
		for( Int i = 0; i < W * H; i++ ) if( labels[i] ) pieces[labels[i]].push_back( i );
		for( Int k = 1; k <= count; k++ )
		{
			if( (Int)pieces[k].size() < CANYON_CELLS ) continue;
			InvCanyon c;
			shapeOf( pieces[k], W, &c.length, &c.width, &c.winding );
			if( c.length < LONG_SHAPE * c.width ) continue;
			std::vector<Real> floor;
			for( size_t j = 0; j < pieces[k].size(); j++ ) floor.push_back( m.heights[pieces[k][j]] );
			c.floorDz = medianOf( floor ) - inv.middleH;
			Real y, x;
			meanOf( pieces[k], &y, &x );
			places.where( y, x, &c.centreShare, NULL );
			c.reachesEdge = reachesEdge( pieces[k] );
			// its arms: the runs of canyon on a ring round the middle, so a cross counts four
			Mask piece( W * H, 0 ), grown;
			for( size_t j = 0; j < pieces[k].size(); j++ ) piece[pieces[k][j]] = 1;
			maskGrow( piece, W, H, 2, grown );
			const Int samples = 360;
			const Real ring = 0.6f * places.half;
			Int runs = 0;
			Bool previous = FALSE, first = FALSE;
			for( Int q = 0; q < samples; q++ )
			{
				Real angle = 2.0f * PI_R * q / samples;
				Int y = clampi( roundi( places.cy + ring * Sin( angle ) ), 0, H - 1 );
				Int x = clampi( roundi( places.cx + ring * Cos( angle ) ), 0, W - 1 );
				Bool on = grown[y * W + x] != 0;
				if( q == 0 ) first = on;
				else if( on && !previous ) runs++;
				previous = on;
			}
			if( first && !previous ) runs++;
			if( first && previous && runs == 0 ) runs = 1;
			c.arms = runs;
			inv.canyons.push_back( c );
		}
	}

	// mesas: ground between the middle and the starts that no base plateau touches
	{
		Mask plateau( W * H ), band( W * H );
		for( Int i = 0; i < W * H; i++ )
		{
			plateau[i] = dry[i] && fabsf( m.heights[i] - inv.startH ) <= 6.0f;
			band[i] = dry[i] && m.heights[i] > inv.middleH + MESA_RISE && m.heights[i] < inv.startH - MESA_RISE;
		}
		std::vector<Int> labels;
		Int count = labelPieces( band, W, H, labels );
		Mask nearPlateau;
		dilateCross( plateau, W, H, 2, nearPlateau );
		std::vector<UnsignedByte> touched( count + 1, 0 );
		for( Int i = 0; i < W * H; i++ ) if( nearPlateau[i] && band[i] ) touched[labels[i]] = 1;
		std::vector< std::vector<Int> > pieces( count + 1 );
		for( Int i = 0; i < W * H; i++ ) if( labels[i] ) pieces[labels[i]].push_back( i );
		for( Int k = 1; k <= count; k++ )
		{
			if( touched[k] || (Int)pieces[k].size() < MESA_CELLS ) continue;
			Mask piece( W * H, 0 ), grown;
			for( size_t j = 0; j < pieces[k].size(); j++ ) piece[pieces[k][j]] = 1;
			dilateCross( piece, W, H, 2, grown );
			Mask ways( W * H, 0 );
			Int rim = 0, rimCliff = 0;
			for( Int i = 0; i < W * H; i++ )
			{
				if( grown[i] && !piece[i] && play[i] )
				{
					rim++;
					rimCliff += m.cliff[i];
					ways[i] = grid[i] && !m.cliff[i];
				}
			}
			if( rim == 0 || (Real)rimCliff / rim < MESA_CLIFF ) continue;
			std::vector<Int> wayLabels;
			InvMesa mesa;
			mesa.exits = labelPieces( ways, W, H, wayLabels );
			mesa.area = (Int)pieces[k].size();
			mesa.radius = sqrtf( mesa.area / PI_R );
			std::vector<Real> top;
			for( size_t j = 0; j < pieces[k].size(); j++ ) top.push_back( m.heights[pieces[k][j]] );
			mesa.topDz = medianOf( top ) - inv.middleH;
			Real y, x;
			meanOf( pieces[k], &y, &x );
			places.where( y, x, &mesa.centreShare, NULL );
			dilateCross( piece, W, H, 4, mesa.near );
			inv.mesas.push_back( mesa );
		}
	}

	// water, area by area; its distance from a start is in start spacings
	Real spacing = 1.0e9f;
	for( size_t i = 0; i < places.starts.size(); i++ )
		for( size_t j = i + 1; j < places.starts.size(); j++ )
			spacing = std::min( spacing, hypotr( places.starts[i].y - places.starts[j].y, places.starts[i].x - places.starts[j].x ) );
	if( spacing > 1.0e8f ) spacing = (Real)m.playable;
	std::vector<Mask> wetMasks;
	for( size_t a = 0; a < m.water.size(); a++ )
	{
		std::vector<WaterArea> one( 1, m.water[a] );
		Mask wet;
		m.waterGrid( one, wet );
		std::vector<Int> cells;
		for( Int i = 0; i < W * H; i++ )
		{
			wet[i] = wet[i] && play[i];
			if( wet[i] ) cells.push_back( i );
		}
		if( cells.size() < 50 ) continue;
		InvWater body;
		shapeOf( cells, W, &body.length, &body.width, &body.winding );
		body.surface = m.water[a].surface / MAP_Z;
		Mask grown;
		dilateCross( wet, W, H, 6, grown );
		std::vector<Real> bank, bed;
		for( Int i = 0; i < W * H; i++ )
		{
			if( grown[i] && !wet[i] && play[i] ) bank.push_back( m.heights[i] );
			if( wet[i] ) bed.push_back( m.heights[i] );
		}
		Real sides[4] = { 0, 0, 0, 0 };
		for( Int x = b; x < b + m.playX; x++ ) { sides[0] += wet[b * W + x]; sides[2] += wet[( b + m.playY - 1 ) * W + x]; }
		for( Int y = b; y < b + m.playY; y++ ) { sides[1] += wet[y * W + b + m.playX - 1]; sides[3] += wet[y * W + b]; }
		sides[0] /= m.playX; sides[2] /= m.playX; sides[1] /= m.playY; sides[3] /= m.playY;
		Real lo = std::min( std::min( sides[0], sides[1] ), std::min( sides[2], sides[3] ) );
		Real hi = std::max( std::max( sides[0], sides[1] ), std::max( sides[2], sides[3] ) );
		body.kind = lo >= FULL_EDGE ? WATER_SEA : hi >= COAST_EDGE ? WATER_COAST :
			body.length >= 4.0f * body.width ? WATER_RIVER : (Real)cells.size() >= SEA_WET * playCount ? WATER_SEA : WATER_LAKE;
		for( Int k = 0; k < 4; k++ ) body.edges[k] = sides[k];
		Real y, x;
		meanOf( cells, &y, &x );
		places.where( y, x, &body.centreShare, &body.nearest );
		Real gap = 1.0e9f;
		for( size_t s = 0; s < places.starts.size(); s++ )
			gap = std::min( gap, distanceToSet( cells, W, (Int)places.starts[s].y, (Int)places.starts[s].x ) );
		body.startClear = gap / spacing;
		body.area = (Int)cells.size();
		body.depth = body.surface - medianOf( bed );
		body.bank = bank.empty() ? 0.0f : medianOf( bank ) - body.surface;
		body.reachesEdge = reachesEdge( cells );
		body.share = (Real)cells.size() / playCount;
		body.joins = -1;
		inv.water.push_back( body );
		wetMasks.push_back( wet );
	}
	// a river that runs into a longer one is its branch
	for( size_t i = 0; i < inv.water.size(); i++ )
	{
		if( inv.water[i].kind != WATER_RIVER ) continue;
		Mask grown;
		dilateCross( wetMasks[i], W, H, JOIN_CELLS, grown );
		Int best = -1;
		for( size_t j = 0; j < inv.water.size(); j++ )
		{
			if( j == i || inv.water[j].kind != WATER_RIVER ) continue;
			Bool longer = inv.water[j].length > inv.water[i].length ||
				( inv.water[j].length == inv.water[i].length && j < i );
			if( !longer ) continue;
			Bool meets = FALSE;
			for( Int k = 0; k < W * H && !meets; k++ ) meets = grown[k] && wetMasks[j][k];
			if( meets && ( best < 0 || inv.water[j].length > inv.water[best].length ) )
				best = (Int)j;
		}
		inv.water[i].joins = best;
	}

	// bridges and roads, as the engine pairs them: a POINT1 object and the POINT2 one after it
	struct Segment { AsciiString name; Real x1, y1, x2, y2; Int f1, f2; };
	std::vector<Segment> segments;
	inv.roadLines.assign( W * H, 0 );
	for( size_t k = 0; k + 1 < m.things.size(); k++ )
	{
		const MapThing&p = m.things[k];
		const MapThing&q = m.things[k + 1];
		Real length = hypotr( q.x - p.x, q.y - p.y );
		if( ( p.flags & FLAG_BRIDGE_POINT1 ) && ( q.flags & FLAG_BRIDGE_POINT2 ) )
		{
			Int ay = (Int)p.y + b, ax = (Int)p.x + b, zy = (Int)q.y + b, zx = (Int)q.x + b;
			Int steps = std::max( (Int)length, 1 );
			Int under = 255;
			Bool wet = FALSE;
			std::vector<Int> line;
			for( Int s = 0; s <= steps; s++ )
			{
				Int y = clampi( ay + ( zy - ay ) * s / steps, 0, H - 1 ), x = clampi( ax + ( zx - ax ) * s / steps, 0, W - 1 );
				line.push_back( y * W + x );
				under = std::min( under, (Int)m.heights[y * W + x] );
				wet = wet || wetAll[y * W + x];
			}
			Int a = clampi( ay, 0, H - 1 ) * W + clampi( ax, 0, W - 1 ), z = clampi( zy, 0, H - 1 ) * W + clampi( zx, 0, W - 1 );
			InvBridge bridge;
			bridge.name = p.name;
			bridge.length = length;
			bridge.over = wet ? OVER_WATER : under < std::min( m.heights[a], m.heights[z] ) - 15 ? OVER_GAP : OVER_GROUND;
			bridge.endsDz[0] = m.heights[a] - inv.middleH;
			bridge.endsDz[1] = m.heights[z] - inv.middleH;
			places.where( 0.5f * ( ay + zy ), 0.5f * ( ax + zx ), &bridge.centreShare, NULL );
			bridge.onMesa = -1;
			for( size_t s = 0; s < inv.mesas.size() && bridge.onMesa < 0; s++ )
				if( inv.mesas[s].near[a] || inv.mesas[s].near[z] )
					bridge.onMesa = (Int)s;
			bridge.water = -1;
			if( wet )
			{
				Int most = -1;
				for( size_t w = 0; w < wetMasks.size(); w++ )
				{
					Int crossed = 0;
					for( size_t s = 0; s < line.size(); s++ ) crossed += wetMasks[w][line[s]];
					if( crossed > most ) { most = crossed; bridge.water = (Int)w; }
				}
			}
			inv.bridges.push_back( bridge );
		}
		else if( ( p.flags & FLAG_ROAD_POINT1 ) && ( q.flags & FLAG_ROAD_POINT2 ) )
		{
			Segment s;
			s.name = p.name;
			s.x1 = p.x + b; s.y1 = p.y + b; s.x2 = q.x + b; s.y2 = q.y + b;
			s.f1 = p.flags; s.f2 = q.flags;
			segments.push_back( s );
			lineCells( yx( s.y1, s.x1 ), yx( s.y2, s.x2 ), W, H, inv.roadLines );
		}
	}

	// towns: civilian buildings no more than TOWN_GAP apart
	std::vector<Int> houses;
	for( size_t k = 0; k < m.things.size(); k++ )
		if( !m.things[k].waypointID && m.things[k].flags == 0 && civilian && civilian( m.things[k].name.str() ) )
			houses.push_back( (Int)k );
	if( !houses.empty() )
	{
		Mask marks( W * H, 0 ), grown;
		for( size_t k = 0; k < houses.size(); k++ )
		{
			const MapThing&t = m.things[houses[k]];
			marks[clampi( (Int)t.y + b, 0, H - 1 ) * W + clampi( (Int)t.x + b, 0, W - 1 )] = 1;
		}
		dilateCross( marks, W, H, TOWN_GAP / 2, grown );
		std::vector<Int> labels;
		Int count = labelPieces( grown, W, H, labels );
		std::vector< std::vector<Int> > members( count + 1 );
		for( size_t k = 0; k < houses.size(); k++ )
		{
			const MapThing&t = m.things[houses[k]];
			members[labels[clampi( (Int)t.y + b, 0, H - 1 ) * W + clampi( (Int)t.x + b, 0, W - 1 )]].push_back( houses[k] );
		}
		for( Int k = 1; k <= count; k++ )
		{
			if( (Int)members[k].size() < TOWN_BUILDINGS ) continue;
			InvTown town;
			town.buildings = (Int)members[k].size();
			town.box[0] = town.box[1] = 1.0e9f;
			town.box[2] = town.box[3] = -1.0e9f;
			Real sy = 0, sx = 0;
			for( size_t j = 0; j < members[k].size(); j++ )
			{
				const MapThing&t = m.things[members[k][j]];
				town.box[0] = std::min( town.box[0], t.x ); town.box[1] = std::min( town.box[1], t.y );
				town.box[2] = std::max( town.box[2], t.x ); town.box[3] = std::max( town.box[3], t.y );
				sy += t.y + b; sx += t.x + b;
				town.houses.push_back( yx( t.y + b, t.x + b ) );
			}
			places.where( sy / members[k].size(), sx / members[k].size(), &town.centreShare, NULL );
			inv.towns.push_back( town );
		}
	}

	// the roads outside the towns, and where roads leave the map
	auto inTown = [&]( Real x, Real y ) -> Bool
	{
		for( size_t t = 0; t < inv.towns.size(); t++ )
		{
			const Real *box = inv.towns[t].box;
			if( box[0] - TOWN_GAP <= x - b && x - b <= box[2] + TOWN_GAP && box[1] - TOWN_GAP <= y - b && y - b <= box[3] + TOWN_GAP )
				return TRUE;
		}
		return FALSE;
	};
	std::vector<YX> exits;
	for( size_t k = 0; k < segments.size(); k++ )
	{
		const Real ends[2][2] = { { segments[k].x1, segments[k].y1 }, { segments[k].x2, segments[k].y2 } };
		for( Int e = 0; e < 2; e++ )
		{
			Real x = ends[e][0], y = ends[e][1];
			Real edge = std::min( std::min( x - b, y - b ), std::min( b + m.playX - 1 - x, b + m.playY - 1 - y ) );
			if( edge > EXIT_CELLS ) continue;
			Bool fresh = TRUE;
			for( size_t j = 0; j < exits.size(); j++ ) fresh = fresh && hypotr( x - exits[j].x, y - exits[j].y ) > TOWN_GAP;
			if( fresh ) exits.push_back( yx( y, x ) );
		}
	}
	inv.roadExits = (Int)exits.size();
	for( size_t k = 0; k < segments.size(); k++ )
	{
		size_t r = 0;
		while( r < inv.roads.size() && !( inv.roads[r].name == segments[k].name ) ) r++;
		if( r == inv.roads.size() )
		{
			InvRoad road;
			road.name = segments[k].name;
			road.segments = 0;
			road.length = road.outsideLength = 0.0f;
			inv.roads.push_back( road );
		}
		InvRoad& road = inv.roads[r];
		Real length = hypotr( segments[k].x2 - segments[k].x1, segments[k].y2 - segments[k].y1 );
		road.segments++;
		road.length += length;
		if( !inTown( 0.5f * ( segments[k].x1 + segments[k].x2 ), 0.5f * ( segments[k].y1 + segments[k].y2 ) ) )
			road.outsideLength += length;
	}
	for( size_t r = 0; r < inv.roads.size(); r++ )
	{
		InvRoad& road = inv.roads[r];
		road.segmentLength = road.length / road.segments;
		// the end flags most of its pairs carry, the first seen of equal counts
		std::vector< std::pair<Int, Int> > pairs;
		std::vector<Int> counts;
		for( size_t k = 0; k < segments.size(); k++ )
		{
			if( !( segments[k].name == road.name ) ) continue;
			std::pair<Int, Int> p( segments[k].f1, segments[k].f2 );
			size_t j = std::find( pairs.begin(), pairs.end(), p ) - pairs.begin();
			if( j == pairs.size() ) { pairs.push_back( p ); counts.push_back( 0 ); }
			counts[j]++;
		}
		size_t most = 0;
		for( size_t j = 1; j < counts.size(); j++ ) if( counts[j] > counts[most] ) most = j;
		road.flags[0] = pairs[most].first;
		road.flags[1] = pairs[most].second;
	}

	// tech buildings but the derricks, which are money
	for( size_t k = 0; k < m.things.size(); k++ )
	{
		const MapThing&t = m.things[k];
		if( t.waypointID || t.flags != 0 || !t.name.startsWithNoCase( "tech" ) || t.name == "TechOilDerrick" )
			continue;
		InvTech tech;
		tech.name = t.name;
		Int cy = clampi( (Int)t.y + b, 0, H - 1 ), cx = clampi( (Int)t.x + b, 0, W - 1 );
		places.where( (Real)cy, (Real)cx, &tech.centreShare, NULL );
		tech.onMesa = -1;
		for( size_t s = 0; s < inv.mesas.size() && tech.onMesa < 0; s++ )
			if( inv.mesas[s].near[cy * W + cx] )
				tech.onMesa = (Int)s;
		inv.tech.push_back( tech );
	}

	// what stands with the small piles
	inv.pileCount = (Int)m.piles.size();
	inv.pileDz = 0.0f;
	if( !m.piles.empty() )
	{
		std::vector<Real> dz;
		std::vector<AsciiString> names;
		std::vector<Int> counts;
		for( size_t p = 0; p < m.piles.size(); p++ )
		{
			dz.push_back( m.heights[clampi( (Int)m.piles[p].y + b, 0, H - 1 ) * W + clampi( (Int)m.piles[p].x + b, 0, W - 1 )] - inv.startH );
			for( size_t k = 0; k < houses.size(); k++ )
			{
				const MapThing&t = m.things[houses[k]];
				if( hypotr( t.x - m.piles[p].x, t.y - m.piles[p].y ) > 10.0f ) continue;
				size_t j = 0;
				while( j < names.size() && !( names[j] == t.name ) ) j++;
				if( j == names.size() ) { names.push_back( t.name ); counts.push_back( 0 ); }
				counts[j]++;
			}
		}
		inv.pileDz = medianOf( dz );
		for( size_t j = 0; j < names.size(); j++ )
			if( counts[j] * 2 >= (Int)m.piles.size() )
				inv.pileCompany.push_back( names[j] );
	}

	// whether some start reaches another only over a bridge
	{
		std::vector<Int> labels;
		labelPieces( grid, W, H, labels );
		inv.split = FALSE;
		for( size_t i = 1; i < places.starts.size(); i++ )
			inv.split = inv.split || labels[lv.startY[i] * W + lv.startX[i]] != labels[lv.startY[0] * W + lv.startX[0]];
	}
}

//-----------------------------------------------------------------------------
// The exemplar: the map, its inventory, and what the quilting reads
//-----------------------------------------------------------------------------

struct BlendEntry
{
	Int tile;
	char flags[6];
	Int custom;
};

struct Scenery
{
	AsciiString name;
	Real fx, fy, angle;
	Bool tree;
};

struct Exemplar
{
	SynthMap m;
	Levels lv;
	Inventory inv;
	std::vector<Int> cls;					///< texture class a cell
	std::vector<Int> classFirst, classWidth;
	Int bitmapTiles;
	std::vector<char> tableHead;			///< BlendTileData from the class count to the blend entries
	std::vector<BlendEntry> entries;
	std::vector<Int> blendClass, blendShape;
	std::vector< std::pair< std::string, Int > > shapes;	///< flags and custom edge class, sorted
	std::vector<Int> sceneryFirst;		///< a cell's first entry in scenery, W*H+1 of them
	std::vector<Scenery> scenery;
	std::vector<Int> trees;				///< trees a cell
	Real treeDensity;
	Mask town;							///< ground the quilt never copies: towns, and the bases of a flat template
	Mask ground;						///< GROUND_ bits a cell: cliff, water, shore, road strip
	Bool hasWater;
	Field detail;						///< t less its own blur: the detail the quilting copies
	std::vector<Int> guide64, detail256;
	std::vector<Int> blendOver;			///< cells of class c carrying a blend of class k, at c * classes + k
	std::vector<Int> classRank;			///< a class's place by how much more often it lies over the others than under
	Mask steep;							///< the exemplar's cliffs by its own heights, cliffOf's rule
	Mask cliffTexture;					///< a class whose name says cliff
	std::vector<Int> steepClass;		///< the class of the nearest steep cell painted as cliff, or the commonest such; -1 none
	std::vector<Int> openClass;			///< the class of the nearest cell not steep, -1 past TEXTURE_REACH

	Bool load( const std::vector<char>& data, CivilianTest civilian )
	{
		if( !m.load( data ) )
			return FALSE;
		const Int W = m.W, H = m.H, b = m.border;
		lv.init( m );

		// the classes and the blend table
		Int tilesMax = 0;
		for( size_t c = 0; c < m.classes.size(); c++ )
			tilesMax = std::max( tilesMax, m.classes[c].first + m.classes[c].count );
		std::vector<Int> owner( tilesMax + 1, 0 );
		for( size_t c = 0; c < m.classes.size(); c++ )
		{
			for( Int t = m.classes[c].first; t < m.classes[c].first + m.classes[c].count; t++ )
				owner[t] = (Int)c;
			classFirst.push_back( m.classes[c].first );
			classWidth.push_back( std::max( m.classes[c].width, 1 ) );
		}
		cls.resize( W * H );
		for( Int i = 0; i < W * H; i++ )
			cls[i] = owner[clampi( m.tiles[i] >> 2, 0, tilesMax )];

		const Chunk *blend = NULL;
		for( size_t c = 0; c < m.raw.chunks.size(); c++ )
			if( m.raw.nameOf( m.raw.chunks[c].id ) == "BlendTileData" )
				blend = &m.raw.chunks[c];
		const std::vector<char>& tail = blend->body;
		Int pos = m.tailPos;
		bitmapTiles = readInt( tail, pos );
		Int blended = readInt( tail, pos + 4 );
		Int classes = readInt( tail, pos + 12 );
		Int head = pos + 12;
		pos += 16;
		for( Int c = 0; c < classes; c++ )
			pos = readAscii( tail, pos + 16, NULL );
		Int edgeClasses = readInt( tail, pos + 4 );
		pos += 8;
		for( Int c = 0; c < edgeClasses; c++ )
			pos = readAscii( tail, pos + 12, NULL );
		tableHead.assign( tail.begin() + head, tail.begin() + pos );
		BlendEntry none;
		none.tile = 0;
		memset( none.flags, 0, 6 );
		none.custom = -1;
		entries.push_back( none );
		for( Int e = 1; e < blended; e++ )
		{
			BlendEntry entry;
			entry.tile = readInt( tail, pos );
			readBytes( tail, pos + 4, entry.flags, 6 );
			entry.custom = readInt( tail, pos + 10 );
			entries.push_back( entry );
			pos += 18;
		}
		for( size_t e = 0; e < entries.size(); e++ )
		{
			blendClass.push_back( owner[clampi( entries[e].tile >> 2, 0, tilesMax )] );
			if( e > 0 )
				shapes.push_back( std::make_pair( std::string( entries[e].flags, 6 ), entries[e].custom ) );
		}
		std::sort( shapes.begin(), shapes.end() );
		shapes.erase( std::unique( shapes.begin(), shapes.end() ), shapes.end() );
		blendShape.push_back( 0 );
		for( size_t e = 1; e < entries.size(); e++ )
		{
			std::pair< std::string, Int > key( std::string( entries[e].flags, 6 ), entries[e].custom );
			blendShape.push_back( (Int)( std::lower_bound( shapes.begin(), shapes.end(), key ) - shapes.begin() ) );
		}

		// which class the exemplar lays over which, for the blends blendChunk works out again: the count of
		// each pair, and one order of all of them for the pairs the exemplar never puts side by side
		const Int NC = std::max( (Int)m.classes.size(), 1 );
		blendOver.assign( NC * NC, 0 );
		for( Int i = 0; i < W * H; i++ )
		{
			const Short layer[2] = { m.blends[i], m.extras[i] };
			for( Int l = 0; l < 2; l++ )
				if( layer[l] > 0 && layer[l] < (Int)entries.size() && blendClass[layer[l]] != cls[i] )
					blendOver[cls[i] * NC + blendClass[layer[l]]]++;
		}
		std::vector< std::pair<Int, Int> > net;
		for( Int k = 0; k < NC; k++ )
		{
			Int over = 0;
			for( Int c = 0; c < NC; c++ )
				over += blendOver[c * NC + k] - blendOver[k * NC + c];
			net.push_back( std::make_pair( over, k ) );
		}
		std::sort( net.begin(), net.end() );
		classRank.assign( NC, 0 );
		for( Int r = 0; r < NC; r++ )
			classRank[net[r].second] = r;

		// the exemplar's cliffs by its heights, the rule the new map's are found by (its cliff bits are also
		// painted under flat sea and river beds), and for each cell the class of the nearest cliff painted
		// as cliff and of the nearest ground that is no cliff, which blendChunk paints a cell with when the
		// new heights made it the other one
		spanCliff( lv.h, W, H, steep );
		cliffTexture.assign( NC, 0 );
		for( size_t c = 0; c < m.classes.size(); c++ )
		{
			AsciiString lower = m.classes[c].name;
			lower.toLower();
			cliffTexture[c] = strstr( lower.str(), "cliff" ) != NULL;
		}
		Mask rockFace( W * H ), notSteep( W * H );
		std::vector<Int> faces( NC, 0 );
		for( Int i = 0; i < W * H; i++ )
		{
			rockFace[i] = steep[i] && cliffTexture[cls[i]];
			notSteep[i] = !steep[i];
			faces[cls[i]] += rockFace[i];
		}
		Int commonest = -1;
		for( Int c = 0; c < NC; c++ )
			if( faces[c] > 0 && ( commonest < 0 || faces[c] > faces[commonest] ) )
				commonest = c;
		nearestClass( rockFace, cls, W, H, TEXTURE_REACH, steepClass );
		nearestClass( notSteep, cls, W, H, TEXTURE_REACH, openClass );
		for( Int i = 0; i < W * H; i++ )
			if( steepClass[i] < 0 )
				steepClass[i] = commonest;

		// the scenery that moves with the ground, by the cell it stands on
		static const char *const theScenery[] = { "Tree", "Rocks", "RockCluster", "Tent", "Amb_", "AmbientWind", "WoodTower", "QuonsetHut" };
		std::vector< std::pair<Int, Int> > keyed;	// cell, thing
		for( size_t k = 0; k < m.things.size(); k++ )
		{
			const MapThing&t = m.things[k];
			if( t.waypointID ) continue;
			Bool match = FALSE;
			for( Int s = 0; s < 8 && !match; s++ ) match = t.name.startsWith( theScenery[s] );
			if( !match ) continue;
			Int cy = (Int)t.y + b, cx = (Int)t.x + b;
			if( cy < 0 || cx < 0 || cy >= H || cx >= W ) continue;
			keyed.push_back( std::make_pair( cy * W + cx, (Int)k ) );
		}
		std::stable_sort( keyed.begin(), keyed.end(),
			[]( const std::pair<Int, Int>& a, const std::pair<Int, Int>& c ) { return a.first < c.first; } );
		sceneryFirst.assign( W * H + 1, 0 );
		trees.assign( W * H, 0 );
		for( size_t k = 0; k < keyed.size(); k++ )
		{
			const MapThing&t = m.things[keyed[k].second];
			Scenery s;
			s.name = t.name;
			s.fx = t.x - floorf( t.x );
			s.fy = t.y - floorf( t.y );
			s.angle = t.angle;
			s.tree = t.name.startsWith( "Tree" );
			scenery.push_back( s );
			sceneryFirst[keyed[k].first + 1]++;
			trees[keyed[k].first] += s.tree;
		}
		for( Int i = 0; i < W * H; i++ )
			sceneryFirst[i + 1] += sceneryFirst[i];

		// trees a cell where trees may stand: the playable ground off the pads and the water
		Mask wetAll;
		m.waterGrid( m.water, wetAll );
		Int open = 0, standing = 0;
		for( Int y = b; y < b + m.playY; y++ )
		{
			for( Int x = b; x < b + m.playX; x++ )
			{
				Bool ok = !wetAll[y * W + x];
				for( size_t s = 0; s < lv.startY.size() && ok; s++ )
					ok = hypotr( (Real)( y - lv.startY[s] ), (Real)( x - lv.startX[s] ) ) > PAD_OUTER - 9.0f;
				if( !ok ) continue;
				open++;
				standing += trees[y * W + x];
			}
		}
		treeDensity = (Real)standing / std::max( open, 1 );

		takeInventory( m, lv, civilian, inv );

		// a town's ground goes only with its stamp
		town.assign( W * H, 0 );
		for( size_t t = 0; t < inv.towns.size(); t++ )
		{
			const Real *box = inv.towns[t].box;
			for( Int y = std::max( (Int)box[1] + b - TOWN_MARGIN, 0 ); y < std::min( (Int)box[3] + b + TOWN_MARGIN + 1, H ); y++ )
				for( Int x = std::max( (Int)box[0] + b - TOWN_MARGIN, 0 ); x < std::min( (Int)box[2] + b + TOWN_MARGIN + 1, W ); x++ )
					town[y * W + x] = 1;
		}
		// and a base not raised over the map goes only with its pad
		if( lv.plateau - lv.bowl <= MIN_RELIEF )
			for( size_t s = 0; s < lv.startY.size(); s++ )
				for( Int y = std::max( lv.startY[s] - BASE_GROUND, 0 ); y <= std::min( lv.startY[s] + BASE_GROUND, H - 1 ); y++ )
					for( Int x = std::max( lv.startX[s] - BASE_GROUND, 0 ); x <= std::min( lv.startX[s] + BASE_GROUND, W - 1 ); x++ )
						if( ( y - lv.startY[s] ) * ( y - lv.startY[s] ) + ( x - lv.startX[s] ) * ( x - lv.startX[s] ) <= BASE_GROUND * BASE_GROUND )
							town[y * W + x] = 1;

		// what the quilt matches besides height. The water and its bank: a sea laid on the map takes the
		// exemplar's sea bed, and the bank sand stays by water. The road strips: the classes far commoner
		// along the roads than anywhere, which the quilt otherwise scattered as broken paths over open ground.
		Mask shore, alongRoads;
		maskGrow( wetAll, W, H, SHORE_GROUND, shore );
		maskGrow( inv.roadLines, W, H, 1, alongRoads );
		std::vector<Int64> along( m.classes.size() + 1, 0 ), over( m.classes.size() + 1, 0 );
		Int64 alongAll = 0, overAll = 0;
		hasWater = FALSE;
		for( Int y = b; y < b + m.playY; y++ )
			for( Int x = b; x < b + m.playX; x++ )
			{
				const Int i = y * W + x;
				over[cls[i]]++;
				overAll++;
				along[cls[i]] += alongRoads[i];
				alongAll += alongRoads[i];
				hasWater = hasWater || wetAll[i];
			}
		ground.resize( W * H );
		for( Int i = 0; i < W * H; i++ )
		{
			const Bool strip = alongAll > 0 && along[cls[i]] * overAll > STRIP_LIFT * over[cls[i]] * alongAll;
			ground[i] = ( m.cliff[i] ? GROUND_CLIFF : 0 ) | ( wetAll[i] ? GROUND_WET : 0 ) | ( shore[i] ? GROUND_SHORE : 0 ) | ( strip ? GROUND_STRIP : 0 );
		}

		Field blur;
		boxMean( lv.t, W, H, DETAIL_RADIUS, blur );
		detail.resize( W * H );
		guide64.resize( W * H );
		detail256.resize( W * H );
		for( Int i = 0; i < W * H; i++ )
		{
			detail[i] = lv.t[i] - blur[i];
			detail256[i] = roundi( detail[i] * 256.0f );
			guide64[i] = roundi( lv.guide[i] * 64.0f );
		}
		return TRUE;
	}

	/// The tile WorldBuilder paints a class with at a cell (the generator's tileIndexForCell).
	Int tileFor( Int c, Int y, Int x ) const
	{
		Int w = classWidth[c];
		return ( ( classFirst[c] + ( x / 2 ) % w + w * ( ( y / 2 ) % w ) ) << 2 ) + 2 * ( y & 1 ) + ( x & 1 );
	}
};

//-----------------------------------------------------------------------------
// The skeleton's frame (synthmap.Frame) and the plan compose() makes
//-----------------------------------------------------------------------------

struct WaterPoly
{
	std::vector<YX> points;		///< grid cells
	Real surface;				///< bytes
};

struct RiverLine
{
	Int index;
	Real surface, half;
	std::vector<YX> points, sites;
};

struct PlannedBridge { AsciiString name; YX a, z; };
struct PlannedRoad { AsciiString name; std::vector<YX> points; };
struct Stamp { Int ty, tx, sy, sx, rows, cols; };
struct PlannedTech { AsciiString name; Int y, x; };

/** A piece of the exemplar laid on the new map whole: turned by quarters, perhaps mirrored, scaled
	about two anchors (grid cells of each map). A pad is the circle round a start; a crossing is the
	box round a town on its river, in source cells. */
struct Copy
{
	YX target, source;
	Int turn;					///< quarter turns, 0 to 3
	Bool mirror;				///< x turned over before the turn
	Real spin;					///< radians turned after the quarter turns, 0 for a pad: a crossing follows its river
	Real spinCos, spinSin;
	Real scale;					///< target cells a source cell
	Real radius;				///< target cells round the target anchor a pad reaches; 0 for a crossing
	Real core;					///< target cells round the target anchor the large features keep off; 0 for a crossing
	Real lift;					///< bytes from a source height to the target's
	Real pivot, heightScale;	///< source heights scale about the pivot: a scaled plateau keeps its ramps' slope
	Bool raised;				///< a plateau base, its cliffs falling away round it
	const Field *region;		///< a pad's share of each source cell, the exemplar's size
	Int regionW, regionH;
	Int box[4];					///< a crossing's source cells: y0, x0, y1, x1, the last two past the end

	YX toTarget( Real sy, Real sx ) const
	{
		Real dy = ( sy - source.y ) * scale, dx = ( sx - source.x ) * scale;
		if( mirror ) dx = -dx;
		for( Int q = 0; q < turn; q++ ) { Real t = dx; dx = -dy; dy = t; }
		Real x = dx * spinCos - dy * spinSin;
		dy = dx * spinSin + dy * spinCos;
		return yx( target.y + dy, target.x + x );
	}

	YX toSource( Real ty, Real tx ) const
	{
		Real dy = ( ty - target.y ) / scale, dx = ( tx - target.x ) / scale;
		Real x = dx * spinCos + dy * spinSin;
		dy = dy * spinCos - dx * spinSin;
		dx = x;
		for( Int q = 0; q < turn; q++ ) { Real t = dy; dy = -dx; dx = t; }
		if( mirror ) dx = -dx;
		return yx( source.y + dy, source.x + dx );
	}

	void spinBy( Real a )
	{
		spin = a;
		spinCos = Cos( a );
		spinSin = Sin( a );
	}

	/// The copy's turn to the nearest quarter, 0 to 3, plus 4 when it is mirrored: what a blend's corners follow.
	UnsignedByte quarterTurn() const
	{
		Int quarters = turn + roundi( spin / ( 0.5f * PI_R ) );
		return (UnsignedByte)( ( quarters & 3 ) | ( mirror ? 4 : 0 ) );
	}

	/// An angle on the source map (radians, x towards y) as it lies on the target.
	Real angle( Real a ) const
	{
		if( mirror ) a = PI_R - a;
		return a + turn * 0.5f * PI_R + spin;
	}

	Real regionAt( Real sy, Real sx ) const
	{
		Int y = roundi( sy ), x = roundi( sx );
		if( y < 0 || x < 0 || y >= regionH || x >= regionW ) return 0.0f;
		return ( *region )[y * regionW + x];
	}

	/// How much of the target cell the copy holds: 1 inside, fading to 0 over fadeCells at its edge.
	Real weight( Int y, Int x, Real fadeCells ) const
	{
		YX s = toSource( (Real)y, (Real)x );
		if( region )
			return regionAt( s.y, s.x );
		Real inside = std::min( std::min( s.y - box[0], box[2] - 1 - s.y ), std::min( s.x - box[1], box[3] - 1 - s.x ) ) * scale;
		return inside < 0.0f ? 0.0f : fade( ( inside + 1.0f ) / fadeCells );
	}

	/// Whether an object at a source cell comes along: in the pad short of its fade, or in the box.
	Bool carries( Real sy, Real sx ) const
	{
		if( region )
			return regionAt( sy, sx ) >= 0.9f;
		return box[0] <= sy && sy < box[2] && box[1] <= sx && sx < box[3];
	}

	/// The target cells it may touch, y0, x0, y1, x1 inclusive, inside a W by H map.
	void bounds( Int W, Int H, Int out[4] ) const
	{
		Real y0 = target.y - radius, x0 = target.x - radius, y1 = target.y + radius, x1 = target.x + radius;
		if( radius <= 0.0f )
		{
			y0 = x0 = 1.0e9f;
			y1 = x1 = -1.0e9f;
			for( Int c = 0; c < 4; c++ )
			{
				YX p = toTarget( (Real)box[c < 2 ? 0 : 2], (Real)box[c % 2 ? 3 : 1] );
				y0 = std::min( y0, p.y ); y1 = std::max( y1, p.y ); x0 = std::min( x0, p.x ); x1 = std::max( x1, p.x );
			}
		}
		out[0] = clampi( (Int)floorf( y0 ) - 1, 0, H - 1 );
		out[1] = clampi( (Int)floorf( x0 ) - 1, 0, W - 1 );
		out[2] = clampi( (Int)ceilf( y1 ) + 1, 0, H - 1 );
		out[3] = clampi( (Int)ceilf( x1 ) + 1, 0, W - 1 );
	}
};

struct Plan
{
	Mask gentle;				///< slopes compose made, kept from the detail like a ramp
	Mask keepout;				///< no copied scenery
	Mask wet;					///< inside a water polygon
	Field level;				///< that polygon's surface, bytes: under it is water; -1 elsewhere
	std::vector<WaterPoly> water;
	std::vector<RiverLine> lines;
	std::vector<PlannedRoad> roads;
	std::vector<PlannedBridge> bridges;
	std::vector<Stamp> stamps;
	std::vector<Copy> copies;
	std::vector<Int> fixedSource;	///< the exemplar cell a copy put here, -1 for none
	std::vector<UnsignedByte> fixedTurn;	///< how that copy turned the exemplar, Copy::quarterTurn
	Field fixedWeight;				///< how much of the cell's height is the copy's
	std::vector<UnsignedByte> townDone;	///< the exemplar's towns a crossing already brought
	std::vector<PlannedTech> tech;
	Bool hasMesa;
	Int mesaY, mesaX;
	std::vector<AsciiString> notes;

	void init( Int cells )
	{
		gentle.assign( cells, 0 );
		keepout.assign( cells, 0 );
		wet.assign( cells, 0 );
		level.assign( cells, -1.0f );
		fixedSource.assign( cells, -1 );
		fixedTurn.assign( cells, 0 );
		fixedWeight.assign( cells, 0.0f );
		hasMesa = FALSE;
		mesaY = mesaX = 0;
	}

	void note( const char *format, ... )
	{
		char text[256];
		va_list args;
		va_start( args, format );
		vsnprintf( text, sizeof(text), format, args );
		va_end( args );
		notes.push_back( AsciiString( text ) );
	}
};

struct Frame
{
	Int W, H, border, playable;
	Real cy, cx, k, half, seatRing, middle;
	Mask play;
	std::vector<YX> sites;				///< starts, docks, derricks (grid cells)
	std::vector<YX> starts;				///< grid cells as ints
	std::vector<YX> docks;				///< the supply docks, grid cells as ints
	Field guard;						///< 1 on a start pad, dock or derrick, fading out over ten cells

	Real padRadius;						///< the template's base copied round each start, cells

	void init( const SynthMap& m, const Levels& lv, Real pads )
	{
		W = m.W; H = m.H; border = m.border; playable = m.playable;
		cy = lv.cy; cx = lv.cx;
		k = clampr( m.playable / 356.0f, 0.4f, 1.0f );
		// the base the copy brings, however small it was scaled; the skeleton's own without one
		padRadius = pads > 0.0f ? pads : PAD_OUTER;
		half = m.playable / 2.0f;
		m.playMask( play );
		seatRing = 0.0f;
		for( size_t i = 0; i < lv.startY.size(); i++ )
		{
			starts.push_back( yx( (Real)lv.startY[i], (Real)lv.startX[i] ) );
			seatRing += hypotr( lv.startY[i] - cy, lv.startX[i] - cx );
		}
		seatRing /= lv.startY.size();
		middle = 0.55f * seatRing;
		sites = starts;
		guard.assign( W * H, 0.0f );
		for( size_t i = 0; i < starts.size(); i++ )
			guardAround( starts[i], padRadius );
		for( size_t i = 0; i < m.supplies.size(); i++ )
		{
			YX p = yx( m.supplies[i].y + border, m.supplies[i].x + border );
			sites.push_back( p );
			guardAround( p, SITE_OUTER );
			docks.push_back( yx( (Real)( (Int)m.supplies[i].y + border ), (Real)( (Int)m.supplies[i].x + border ) ) );
		}
		for( size_t i = 0; i < m.derricks.size(); i++ )
		{
			YX p = yx( m.derricks[i].y + border, m.derricks[i].x + border );
			sites.push_back( p );
			guardAround( p, SITE_OUTER );
		}
	}

	void guardAround( YX p, Real r )
	{
		Int reachCells = (Int)( r + 11.0f );
		for( Int y = std::max( 0, (Int)p.y - reachCells ); y <= std::min( H - 1, (Int)p.y + reachCells ); y++ )
			for( Int x = std::max( 0, (Int)p.x - reachCells ); x <= std::min( W - 1, (Int)p.x + reachCells ); x++ )
				guard[y * W + x] = std::max( guard[y * W + x], fade( ( r + 10.0f - hypotr( y - p.y, x - p.x ) ) / 10.0f ) );
	}

	/// Of 32 bearings turned by the seed, the line through the middle (moved offset to its side) furthest
	/// from every site, a start's pad counted as wide as it is.
	Real axis( Int seed, Int salt, Real offset ) const
	{
		Real turn = hashUnit( seed, salt, 1 );
		Real bestClear = -1.0e9f, bestAngle = 0.0f;
		for( Int q = 0; q < 32; q++ )
		{
			Real angle = PI_R * ( q + turn ) / 32.0f;
			Real cosine = Cos( angle ), sine = Sin( angle );
			Real clear = 1.0e9f;
			for( size_t i = 0; i < sites.size(); i++ )
				clear = std::min( clear, fabsf( ( sites[i].y - cy ) * cosine - ( sites[i].x - cx ) * sine - offset )
					- ( i < starts.size() ? padRadius : SITE_OUTER ) );
			if( clear > bestClear )
			{
				bestClear = clear;
				bestAngle = angle;
			}
		}
		return bestAngle;
	}

	static YX point( Real angle, Real py, Real px, Real s, Real c )
	{
		Real sine = Sin( angle ), cosine = Cos( angle );
		return yx( py + s * sine - c * cosine, px + s * cosine + c * sine );
	}

	Bool guarded( Int i ) const { return guard[i] > 0.01f; }
};

/// s along a line through (py, px) at angle, c across it.
struct Along
{
	Real sine, cosine, py, px;
	Along( Real angle, Real y, Real x ) : sine( Sin( angle ) ), cosine( Cos( angle ) ), py( y ), px( x ) { }
	Real s( Int y, Int x ) const { return ( y - py ) * sine + ( x - px ) * cosine; }
	Real c( Int y, Int x ) const { return ( x - px ) * sine - ( y - py ) * cosine; }
};

//-----------------------------------------------------------------------------
// compose(): the exemplar's large features into the skeleton
//-----------------------------------------------------------------------------

struct Composer
{
	const Exemplar& src;
	const SynthMap& skm;
	const Levels& skel;
	Frame f;
	Int seed;
	Int W, H;
	/// protection()'s pad and site rings on the skeleton, which the towns keep off. Its slope term is
	/// left out: the skeleton is plains, so that term marked the upper half of every gentle hill
	/// within SLOPE_REACH of a start, a fifth of a small map, and left no ground for a town, while the
	/// ramps it meant are the copied pads', laid over the skeleton before the towns.
	Field padsBefore;
	std::vector<Copy> pads;			///< the exemplar's base round every start

	/// The crossing cut to one of CROSS_CROP's shares.
	struct Crop
	{
		Int box[4];					///< source cells y0, x0, y1, x1, the last two past the end
		YX pivot;					///< the river's middle in the cut, where it meets the generated line
		Real along, across;			///< the cut's reach from the pivot along the river and across it
	};

	/// The exemplar's towns that stand on its river, as one box with the river through it.
	struct Crossing
	{
		Bool found;
		Int box[4];					///< source cells y0, x0, y1, x1, the last two past the end
		Real angle;					///< the river's way through the box, radians, x towards y
		YX centre;					///< the river's middle in the box, source cells
		Mask wet;					///< the exemplar's water, its size
		std::vector<Int> towns;
		std::vector<Int> bridges;	///< the first of each pair of bridge objects inside the box
		Crop crops[CROSS_CROPS];
	};
	Crossing cross;

	Composer( const Exemplar& exemplar, const SynthMap& skeletonMap, const Levels& skeletonLevels, Int s, const std::vector<Copy>& basePads ) :
		src( exemplar ), skm( skeletonMap ), skel( skeletonLevels ), seed( s ), pads( basePads )
	{
		// the features keep off a pad's core, its base less the margin, the frame's guard fading over the
		// cells past it; a canyon may cut a raised pad's cliffs, as it runs between the bases on the exemplar
		Real core = 0.0f;
		for( size_t p = 0; p < pads.size(); p++ )
			core = std::max( core, pads[p].core );
		f.init( skm, skel, core );
		W = skm.W;
		H = skm.H;
		findCrossing();
	}

	void findCrossing()
	{
		const Int SW = src.m.W, SH = src.m.H, sb = src.m.border;
		cross.found = FALSE;
		src.m.waterGrid( src.m.water, cross.wet );
		Int box[4] = { SH, SW, 0, 0 };
		for( size_t t = 0; t < src.inv.towns.size(); t++ )
		{
			const Real *b = src.inv.towns[t].box;
			Int y0 = std::max( (Int)b[1] + sb - TOWN_MARGIN, 0 ), x0 = std::max( (Int)b[0] + sb - TOWN_MARGIN, 0 );
			Int y1 = std::min( (Int)b[3] + sb + TOWN_MARGIN + 1, SH ), x1 = std::min( (Int)b[2] + sb + TOWN_MARGIN + 1, SW );
			Int wetCells = 0;
			for( Int y = y0; y < y1; y++ )
				for( Int x = x0; x < x1; x++ )
					wetCells += cross.wet[y * SW + x];
			if( wetCells < CROSS_WET ) continue;
			cross.towns.push_back( (Int)t );
			box[0] = std::min( box[0], y0 ); box[1] = std::min( box[1], x0 );
			box[2] = std::max( box[2], y1 ); box[3] = std::max( box[3], x1 );
		}
		if( cross.towns.empty() )
			return;
		memcpy( cross.box, box, sizeof(box) );
		std::vector<Int> cells;
		for( Int y = box[0]; y < box[2]; y++ )
			for( Int x = box[1]; x < box[3]; x++ )
				if( cross.wet[y * SW + x] ) cells.push_back( y * SW + x );
		double my = 0, mx = 0, sxx = 0, syy = 0, sxy = 0;
		for( size_t k = 0; k < cells.size(); k++ ) { my += cells[k] / SW; mx += cells[k] % SW; }
		my /= cells.size(); mx /= cells.size();
		for( size_t k = 0; k < cells.size(); k++ )
		{
			double dy = cells[k] / SW - my, dx = cells[k] % SW - mx;
			sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
		}
		cross.angle = 0.5f * ATan2( (Real)( 2.0 * sxy ), (Real)( sxx - syy ) );
		cross.centre = yx( (Real)my, (Real)mx );
		for( size_t p = 0; p + 1 < src.m.things.size(); p++ )
			if( ( src.m.things[p].flags & FLAG_BRIDGE_POINT1 ) && ( src.m.things[p + 1].flags & FLAG_BRIDGE_POINT2 ) && bridgeIn( box, (Int)p, 0 ) )
				cross.bridges.push_back( (Int)p );
		cross.found = TRUE;

		// the river towns' houses and the water, summed over the box, for the cuts
		const Int BW = box[3] - box[1], BH = box[2] - box[0];
		Mask houseCells( BW * BH, 0 );
		for( size_t k = 0; k < cross.towns.size(); k++ )
		{
			const std::vector<YX>& houses = src.inv.towns[cross.towns[k]].houses;
			for( size_t j = 0; j < houses.size(); j++ )
			{
				Int y = (Int)houses[j].y - box[0], x = (Int)houses[j].x - box[1];
				if( y >= 0 && y < BH && x >= 0 && x < BW ) houseCells[y * BW + x]++;
			}
		}
		std::vector<Int> houseSum( ( BW + 1 ) * ( BH + 1 ), 0 ), wetSum( ( BW + 1 ) * ( BH + 1 ), 0 );
		for( Int y = 0; y < BH; y++ )
			for( Int x = 0; x < BW; x++ )
			{
				Int i = ( y + 1 ) * ( BW + 1 ) + x + 1, up = y * ( BW + 1 ) + x + 1, left = ( y + 1 ) * ( BW + 1 ) + x, both = y * ( BW + 1 ) + x;
				houseSum[i] = houseCells[y * BW + x] + houseSum[up] + houseSum[left] - houseSum[both];
				wetSum[i] = cross.wet[( y + box[0] ) * SW + x + box[1]] + wetSum[up] + wetSum[left] - wetSum[both];
			}
		for( Int at = 0; at < CROSS_CROPS; at++ )
			cropCrossing( at, houseSum, wetSum );
	}

	/// Whether both ends of the bridge pair starting at thing p stand inside the source box, margin
	/// cells or more in from its edge.
	Bool bridgeIn( const Int box[4], Int p, Int margin ) const
	{
		Bool inside = TRUE;
		for( Int g = 0; g < 2; g++ )
		{
			Real y = src.m.things[p + g].y + src.m.border, x = src.m.things[p + g].x + src.m.border;
			inside = inside && box[0] + margin <= y && y < box[2] - margin && box[1] + margin <= x && x < box[3] - margin;
		}
		return inside;
	}

	/** The crossing's box cut to CROSS_CROP[at] of its size: of the windows of that size with the
		river in them, the one holding the most of the crossing's bridges whole and clear of the fade a
		copy's edge blends over, then the most of the river towns' houses, ties to the lowest row and
		column. A bridge end on that fade would stand on the carved bed, under the water; such a bridge
		stays the crossing's and layCrossing raises its ends to the bank. The pivot is the middle
		of the water in the cut, so the cut's river lies on the generated line wherever the cut sits. */
	void cropCrossing( Int at, const std::vector<Int>& houseSum, const std::vector<Int>& wetSum )
	{
		const Int BW = cross.box[3] - cross.box[1], margin = (Int)COPY_FADE;
		Crop& crop = cross.crops[at];
		Int size[2];
		for( Int a = 0; a < 2; a++ )
		{
			Int lo = cross.box[a], hi = cross.box[a + 2];
			size[a] = std::min( hi - lo, std::max( roundi( ( hi - lo ) * CROSS_CROP[at] ), CROSS_LEAST ) );
		}
		auto sum = [&]( const std::vector<Int>& s, const Int b[4] ) -> Int
		{
			Int y0 = b[0] - cross.box[0], x0 = b[1] - cross.box[1], y1 = b[2] - cross.box[0], x1 = b[3] - cross.box[1];
			return s[y1 * ( BW + 1 ) + x1] - s[y0 * ( BW + 1 ) + x1] - s[y1 * ( BW + 1 ) + x0] + s[y0 * ( BW + 1 ) + x0];
		};
		// a bridge the window holds counts above every house: the bridges handed back to the river often
		// find no crossing of their own clear of the sites
		Int most = -1;
		for( Int y = cross.box[0]; y + size[0] <= cross.box[2]; y++ )
		{
			for( Int x = cross.box[1]; x + size[1] <= cross.box[3]; x++ )
			{
				const Int window[4] = { y, x, y + size[0], x + size[1] };
				if( sum( wetSum, window ) < CROSS_WET )
					continue;
				Int bridged = 0;
				for( size_t k = 0; k < cross.bridges.size(); k++ )
					bridged += bridgeIn( window, cross.bridges[k], margin );
				Int worth = sum( houseSum, window ) + bridged * 0x10000;
				if( worth <= most )
					continue;
				most = worth;
				memcpy( crop.box, window, sizeof(window) );
			}
		}
		if( most < 0 )
		{
			// no window that small holds CROSS_WET of the river: the box's middle
			for( Int a = 0; a < 2; a++ )
			{
				Real middle = a == 0 ? cross.centre.y : cross.centre.x;
				Int from = clampi( roundi( middle - 0.5f * size[a] ), cross.box[a], cross.box[a + 2] - size[a] );
				crop.box[a] = from;
				crop.box[a + 2] = from + size[a];
			}
		}
		/* A cut too small to hold any bridge is grown over the one nearest its middle. Handed back to
			the river, the bridges of a small map found no crossing clear of the sites and the river was
			left with one, or none; the grown bridge's ends lie on the fade and layCrossing raises them. */
		Int nearest = -1;
		Real least = 1.0e9f;
		const Real midY = 0.5f * ( crop.box[0] + crop.box[2] ), midX = 0.5f * ( crop.box[1] + crop.box[3] );
		for( size_t k = 0; k < cross.bridges.size(); k++ )
		{
			if( bridgeIn( crop.box, cross.bridges[k], 0 ) ) { nearest = -1; break; }
			const MapThing& one = src.m.things[cross.bridges[k]];
			const MapThing& two = src.m.things[cross.bridges[k] + 1];
			Real d = hypotr( 0.5f * ( one.y + two.y ) + src.m.border - midY, 0.5f * ( one.x + two.x ) + src.m.border - midX );
			if( d < least ) { least = d; nearest = cross.bridges[k]; }
		}
		for( Int g = 0; nearest >= 0 && g < 2; g++ )
		{
			const MapThing& end = src.m.things[nearest + g];
			crop.box[0] = std::min( crop.box[0], (Int)( end.y + src.m.border ) );
			crop.box[1] = std::min( crop.box[1], (Int)( end.x + src.m.border ) );
			crop.box[2] = std::max( crop.box[2], (Int)( end.y + src.m.border ) + 1 );
			crop.box[3] = std::max( crop.box[3], (Int)( end.x + src.m.border ) + 1 );
		}
		Int wetCells = 0, wetY = 0, wetX = 0;
		for( Int y = crop.box[0]; y < crop.box[2]; y++ )
			for( Int x = crop.box[1]; x < crop.box[3]; x++ )
				if( cross.wet[y * src.m.W + x] ) { wetCells++; wetY += y; wetX += x; }
		crop.pivot = wetCells ? yx( (Real)wetY / wetCells, (Real)wetX / wetCells ) : cross.centre;
		crop.along = crop.across = 0.0f;
		for( Int c = 0; c < 4; c++ )
		{
			Real cy = (Real)crop.box[c < 2 ? 0 : 2] - crop.pivot.y, cx = (Real)crop.box[c % 2 ? 3 : 1] - crop.pivot.x;
			crop.along = std::max( crop.along, fabsf( cx * Cos( cross.angle ) + cy * Sin( cross.angle ) ) );
			crop.across = std::max( crop.across, fabsf( cy * Cos( cross.angle ) - cx * Sin( cross.angle ) ) );
		}
	}

	/// The copy's ground at the exemplar's height over its ground level, faded into what is there.
	void layCopy( Field& h, Plan& plan, const Copy& copy ) const
	{
		const Int SW = src.m.W, SH = src.m.H;
		Int area[4];
		copy.bounds( W, H, area );
		for( Int y = area[0]; y <= area[2]; y++ )
		{
			for( Int x = area[1]; x <= area[3]; x++ )
			{
				Real w = copy.weight( y, x, COPY_FADE );
				Int i = y * W + x;
				// where two copies meet, the one that holds the cell harder keeps it
				if( w <= 0.0f || w <= plan.fixedWeight[i] ) continue;
				YX s = copy.toSource( (Real)y, (Real)x );
				Int o = clampi( roundi( s.y ), 0, SH - 1 ) * SW + clampi( roundi( s.x ), 0, SW - 1 );
				const Real copied = copy.pivot + copy.lift + ( src.lv.h[o] - copy.pivot ) * copy.heightScale;
				h[i] = w * copied + ( 1.0f - w ) * h[i];
				plan.fixedWeight[i] = std::max( plan.fixedWeight[i], w );
				if( w >= 0.5f )
				{
					plan.fixedSource[i] = o;
					plan.fixedTurn[i] = copy.quarterTurn();
				}
			}
		}
		plan.copies.push_back( copy );
	}

	/** The exemplar's plateau bases are one stretch of high ground with the basin below it; copied
		round the skeleton's starts they stand apart, and a way between two of them narrower than
		PAD_CORRIDOR is where Twilight Flame's armies queued. Such a way is raised to the lower
		plateau's height, so the bases join as they do on the exemplar; a wider one stays a valley.
		The raised way falls back to the ground past it over JOIN_FADE, where a hard edge left a wall
		of 160 bytes in one cell; the distances are a 3-4 chamfer, round rather than diamond-cornered. */
	void joinPlateaus( Field& h ) const
	{
		const Int SW = src.m.W, SH = src.m.H, FAR = 0x3fffffff;
		const Int inside = 3 * PAD_CORRIDOR, reachSum = 3 * ( PAD_CORRIDOR + JOIN_FADE );
		std::vector< std::vector<Int> > steps( pads.size() );
		std::vector<Real> tops( pads.size(), 0.0f );
		for( size_t p = 0; p < pads.size(); p++ )
		{
			if( !pads[p].raised ) continue;
			const Copy& c = pads[p];
			const Real top = src.lv.h[clampi( roundi( c.source.y ), 0, SH - 1 ) * SW + clampi( roundi( c.source.x ), 0, SW - 1 )];
			const Real half = 0.5f * ( top + c.pivot );
			tops[p] = c.pivot + c.lift + ( top - c.pivot ) * c.heightScale;
			std::vector<Int>& d = steps[p];
			d.assign( W * H, FAR );
			Int area[4];
			c.bounds( W, H, area );
			for( Int y = area[0]; y <= area[2]; y++ )
				for( Int x = area[1]; x <= area[3]; x++ )
				{
					if( c.weight( y, x, COPY_FADE ) < 0.5f ) continue;
					YX s = c.toSource( (Real)y, (Real)x );
					if( src.lv.h[clampi( roundi( s.y ), 0, SH - 1 ) * SW + clampi( roundi( s.x ), 0, SW - 1 )] < half ) continue;
					d[y * W + x] = 0;
				}
			// three a straight step, four a diagonal one: a forward and a backward raster pass
			for( Int y = 0; y < H; y++ )
				for( Int x = 0; x < W; x++ )
				{
					Int i = y * W + x, v = d[i];
					if( x > 0 ) v = std::min( v, d[i - 1] + 3 );
					if( y > 0 )
					{
						v = std::min( v, d[i - W] + 3 );
						if( x > 0 ) v = std::min( v, d[i - W - 1] + 4 );
						if( x + 1 < W ) v = std::min( v, d[i - W + 1] + 4 );
					}
					d[i] = v;
				}
			for( Int y = H - 1; y >= 0; y-- )
				for( Int x = W - 1; x >= 0; x-- )
				{
					Int i = y * W + x, v = d[i];
					if( x + 1 < W ) v = std::min( v, d[i + 1] + 3 );
					if( y + 1 < H )
					{
						v = std::min( v, d[i + W] + 3 );
						if( x + 1 < W ) v = std::min( v, d[i + W + 1] + 4 );
						if( x > 0 ) v = std::min( v, d[i + W - 1] + 4 );
					}
					d[i] = v;
				}
		}
		// each pair's least summed distance, its real gap (rmgJoinShare)
		const size_t n = pads.size();
		std::vector<Int> pairLeast( n * n, FAR );
		for( Int i = 0; i < W * H; i++ )
			for( size_t p = 0; p < n; p++ )
			{
				if( steps[p].empty() || steps[p][i] >= inside ) continue;
				for( size_t q = p + 1; q < n; q++ )
					if( !steps[q].empty() )
						pairLeast[p * n + q] = std::min( pairLeast[p * n + q], steps[p][i] + steps[q][i] );
			}
		// each cell rises by the most any pair lifts it, so the order of the pairs does not matter
		for( Int i = 0; i < W * H; i++ )
		{
			Real rise = 0.0f;
			for( size_t p = 0; p < pads.size(); p++ )
			{
				if( steps[p].empty() || steps[p][i] >= reachSum ) continue;
				for( size_t q = p + 1; q < pads.size(); q++ )
				{
					if( steps[q].empty() ) continue;
					const Real gap = std::min( tops[p], tops[q] ) - h[i];
					if( gap <= 0.0f ) continue;
					rise = std::max( rise, rmgJoinShare( pairLeast[p * n + q], steps[p][i] + steps[q][i] ) * gap );
				}
			}
			h[i] += rise;
		}
	}

	// The skeleton's own lakes filled to their rims: the exemplar's water takes their place.
	void fillLakes( Field& h ) const
	{
		Mask filled( W * H, 0 );
		for( size_t a = 0; a < skm.water.size(); a++ )
		{
			std::vector<WaterArea> one( 1, skm.water[a] );
			Mask wet, grown;
			skm.waterGrid( one, wet );
			Bool any = FALSE;
			for( Int i = 0; i < W * H && !any; i++ ) any = wet[i] != 0;
			if( !any ) continue;
			dilateCross( wet, W, H, 3, grown );
			std::vector<Real> rim;
			for( Int i = 0; i < W * H; i++ ) if( grown[i] && !wet[i] ) rim.push_back( h[i] );
			Real level = medianOf( rim );
			for( Int i = 0; i < W * H; i++ ) if( wet[i] ) { h[i] = level; filled[i] = 1; }
		}
		Mask near;
		maskGrow( filled, W, H, 4, near );
		Field smooth;
		boxMean( h, W, H, 3, smooth );
		for( Int i = 0; i < W * H; i++ ) if( near[i] ) h[i] = smooth[i];
	}

	/** A canyon out of the middle and a mesa in it, as the inventory has them (synthmap.carve_middle). The
		canyon keeps the template's arms for its share of players, a cross on Twilight Flame's eight, and
		each arm runs out through a gap between two starts, so neighbours meet it on the way to each other. */
	void carveMiddle( Field& h, Plan& plan, const InvCanyon *canyon, const InvMesa *mesa ) const
	{
		const Int seats = (Int)f.starts.size();
		std::vector<Real> bearings;
		for( Int s = 0; s < seats; s++ )
			bearings.push_back( ATan2( f.starts[s].y - f.cy, f.starts[s].x - f.cx ) );
		std::sort( bearings.begin(), bearings.end() );
		Int arms = 2;
		if( canyon )
		{
			Int templateArms = std::max( canyon->arms, 2 );
			arms = clampi( roundi( (Real)templateArms * seats / (Real)src.m.starts.size() ), 2, templateArms );
		}
		std::vector<Real> angles;
		const Int first = (Int)( synthHash( seed, 77, 4 ) % (UnsignedInt)seats );
		Real firstGap = bearings[( first + 1 ) % seats] - bearings[first];
		firstGap -= 2.0f * PI_R * floorf( firstGap / ( 2.0f * PI_R ) );
		for( Int a = 0; a < arms; a++ )
		{
			if( arms > seats )
			{
				angles.push_back( bearings[first] + 0.5f * firstGap + 2.0f * PI_R * a / arms );
				continue;
			}
			Int j = ( first + ( a * seats ) / arms ) % seats;
			Real gap = bearings[( j + 1 ) % seats] - bearings[j];
			gap -= 2.0f * PI_R * floorf( gap / ( 2.0f * PI_R ) );
			if( gap <= 0.0f ) gap = 2.0f * PI_R;
			angles.push_back( bearings[j] + 0.5f * gap );
		}
		const Real angle = angles[0];
		Real shift = ( hashUnit( seed, 77, 2 ) - 0.5f ) * 16.0f * f.k;
		Real my = f.cy + shift * Sin( angle ), mx = f.cx + shift * Cos( angle );
		Along al( angle, my, mx );
		Field shaped( W * H, 0.0f );
		Field out = h;
		Real mesaA = 0.0f, mesaB = 0.0f, moat = 0.0f;
		if( mesa )
		{
			Real r = std::min( mesa->radius * f.k, 0.3f * f.middle );
			mesaA = arms > 2 ? r : 1.15f * r;
			mesaB = arms > 2 ? r : 0.87f * r;
		}
		if( canyon )
		{
			Real half = 0.45f * canyon->width * f.k;
			moat = mesa ? 0.55f * canyon->width * f.k : 0.0f;
			Real reachS = canyon->reachesEdge ? 0.75f * std::min( H, W ) : f.middle - END_CELLS * f.k;
			Real wave = std::max( 0.5f * canyon->length, 120.0f );
			const Real band = half + WALL_CELLS + 1.4f * canyon->winding + 1.0f;
			std::vector<Along> spokes;
			std::vector<Real> phases;
			for( Int a = 0; a < arms; a++ )
			{
				spokes.push_back( Along( angles[a], my, mx ) );
				phases.push_back( hashUnit( seed, 77, 3 + a ) * 2.0f * PI_R );
			}
			for( Int y = 0; y < H; y++ )
			{
				for( Int x = 0; x < W; x++ )
				{
					Int i = y * W + x;
					Real cut = 0.0f;
					for( Int a = 0; a < arms; a++ )
					{
						Real s = spokes[a].s( y, x ), c = spokes[a].c( y, x );
						if( s < -band || fabsf( c ) > band ) continue;
						Real wander = 1.4f * canyon->winding * Sin( 2.0f * PI_R * s / wave + phases[a] ) *
							fade( ( s - mesaA ) / std::max( 2.0f * moat, 1.0f ) );
						Real ford = fabsf( s - FORD_AT * f.middle );
						Real arm = fade( ( half + WALL_CELLS - fabsf( c - wander ) ) / WALL_CELLS ) * ( 1.0f - 0.9f * fade( ( FORD_CELLS - ford ) / 6.0f ) );
						if( !canyon->reachesEdge )
							arm *= fade( ( reachS - s ) / ( END_CELLS * f.k ) );
						cut = std::max( cut, arm );
					}
					if( mesa )
					{
						Real s = al.s( y, x ), c = al.c( y, x );
						Real ellipse = hypotr( s / ( mesaA + moat ), c / ( mesaB + moat ) );
						cut = std::max( cut, fade( ( 1.0f - ellipse ) * ( mesaB + moat ) / WALL_CELLS ) );
					}
					cut *= 1.0f - f.guard[i];
					if( cut <= 0.0f ) continue;
					Real floor = skel.bowl + canyon->floorDz + 0.3f * ( h[i] - skel.bowl );
					out[i] = ( 1.0f - cut ) * out[i] + cut * floor;
					shaped[i] = std::max( shaped[i], cut );
					if( cut > 0.05f && cut < 0.95f ) plan.keepout[i] = 1;
				}
			}
		}
		if( mesa )
		{
			for( Int y = 0; y < H; y++ )
			{
				for( Int x = 0; x < W; x++ )
				{
					Int i = y * W + x;
					Real s = al.s( y, x ), c = al.c( y, x );
					Real e = hypotr( s / mesaA, c / mesaB );
					Real ends = fabsf( s ) / std::max( hypotr( s, c ), 1e-6f );
					if( mesa->exits < 2 && s <= 0.0f ) ends = 0.0f;
					Real e8 = ends * ends; e8 *= e8; e8 *= e8;
					Real width = WALL_CELLS + ( MESA_RAMP - WALL_CELLS ) * e8;
					Real lift = fade( ( 1.0f - e ) * mesaB / width + 1.0f ) * ( 1.0f - f.guard[i] );
					if( lift <= 0.0f ) continue;
					out[i] = ( 1.0f - lift ) * out[i] + lift * ( skel.bowl + mesa->topDz + 0.3f * ( h[i] - skel.bowl ) );
					shaped[i] = std::max( shaped[i], lift );
				}
			}
			plan.hasMesa = TRUE;
			plan.mesaY = roundi( my );
			plan.mesaX = roundi( mx );
			Real top = skel.bowl + mesa->topDz;
			// every bridge the template throws over a gap goes to the island, across the land between two arms
			std::vector<const InvBridge *> ours;
			for( size_t k = 0; k < src.inv.bridges.size(); k++ )
				if( src.inv.bridges[k].over == OVER_GAP && src.inv.bridges[k].onMesa >= 0 )
					ours.push_back( &src.inv.bridges[k] );
			std::vector<Real> wedges( angles );
			std::sort( wedges.begin(), wedges.end() );
			for( size_t i = 0; i < ours.size(); i++ )
			{
				Int w = (Int)( ( i * wedges.size() ) / ours.size() );
				Real gap = wedges[( w + 1 ) % wedges.size()] - wedges[w];
				gap -= 2.0f * PI_R * floorf( gap / ( 2.0f * PI_R ) );
				if( gap <= 0.0f ) gap = 2.0f * PI_R;
				Real heading = wedges[w] + 0.5f * gap;
				Real from = std::min( mesaA, mesaB ) - 4.0f;
				YX a = Frame::point( heading, my, mx, from, 0.0f );
				Real span = std::max( ours[i]->length * f.k, moat + 2.0f * WALL_CELLS + 8.0f );
				YX z = Frame::point( heading, my, mx, from + span, 0.0f );
				disc( out, a, 4.0f, top );
				PlannedBridge bridge;
				bridge.name = ours[i]->name;
				bridge.a = a;
				bridge.z = z;
				plan.bridges.push_back( bridge );
			}
		}
		for( Int i = 0; i < W * H; i++ )
			if( shaped[i] > 0.05f && shaped[i] < 0.95f ) plan.gentle[i] = 1;
		h.swap( out );
	}

	/** A canyon off the middle as the exemplar's: its width, floor and winding along a line as far from
		the middle as it lay, to the edges where it reached them, kept off every pad, dock and derrick. */
	void carveCanyonAside( Field& h, Plan& plan, const InvCanyon& canyon, Int number ) const
	{
		const Int salt = 160 + 7 * number;
		const Real sign = ( synthHash( seed, salt, 5 ) & 1 ) ? 1.0f : -1.0f;
		const Real side = sign * std::min( canyon.centreShare, 0.8f ) * f.half;
		const Real angle = f.axis( seed, salt, side );
		const YX middle = Frame::point( angle, f.cy, f.cx, 0.0f, side );
		const Along al( angle, middle.y, middle.x );
		const Real reachS = canyon.reachesEdge ? hypotr( (Real)H, (Real)W ) : 0.5f * canyon.length * f.k;
		const Real half = std::max( 0.45f * canyon.width * f.k, 3.0f );
		const Real amp = 1.4f * canyon.winding;
		const Real wave = std::max( 0.5f * canyon.length, 120.0f );
		const Real phase = hashUnit( seed, salt, 3 ) * 2.0f * PI_R;
		const Real band = half + WALL_CELLS + amp + 1.0f;
		for( Int y = 0; y < H; y++ )
		{
			for( Int x = 0; x < W; x++ )
			{
				const Real s = al.s( y, x ), c = al.c( y, x );
				if( fabsf( s ) > reachS || fabsf( c ) > band ) continue;
				const Int i = y * W + x;
				const Real wander = amp * Sin( 2.0f * PI_R * s / wave + phase );
				Real cut = fade( ( half + WALL_CELLS - fabsf( c - wander ) ) / WALL_CELLS );
				if( !canyon.reachesEdge )
					cut *= fade( ( reachS - fabsf( s ) ) / ( END_CELLS * f.k ) );
				cut *= 1.0f - f.guard[i];
				if( cut <= 0.0f ) continue;
				const Real floor = std::min( skel.bowl + canyon.floorDz + 0.3f * ( h[i] - skel.bowl ), h[i] );
				h[i] = ( 1.0f - cut ) * h[i] + cut * floor;
				if( cut > 0.05f && cut < 0.95f )
				{
					plan.keepout[i] = 1;
					plan.gentle[i] = 1;
				}
			}
		}
	}

	void disc( Field& h, YX p, Real r, Real value ) const
	{
		for( Int y = std::max( 0, (Int)( p.y - r ) - 1 ); y <= std::min( H - 1, (Int)( p.y + r ) + 1 ); y++ )
			for( Int x = std::max( 0, (Int)( p.x - r ) - 1 ); x <= std::min( W - 1, (Int)( p.x + r ) + 1 ); x++ )
				if( hypotr( y - p.y, x - p.x ) <= r )
					h[y * W + x] = value;
	}

	// disc, raising only: ground already over the value keeps its height
	void discAtLeast( Field& h, YX p, Real r, Real value ) const
	{
		for( Int y = std::max( 0, (Int)( p.y - r ) - 1 ); y <= std::min( H - 1, (Int)( p.y + r ) + 1 ); y++ )
			for( Int x = std::max( 0, (Int)( p.x - r ) - 1 ); x <= std::min( W - 1, (Int)( p.x + r ) + 1 ); x++ )
				if( hypotr( y - p.y, x - p.x ) <= r )
					h[y * W + x] = std::max( h[y * W + x], value );
	}

	void discMask( Mask& m, YX p, Real r ) const
	{
		for( Int y = std::max( 0, (Int)( p.y - r ) - 1 ); y <= std::min( H - 1, (Int)( p.y + r ) + 1 ); y++ )
			for( Int x = std::max( 0, (Int)( p.x - r ) - 1 ); x <= std::min( W - 1, (Int)( p.x + r ) + 1 ); x++ )
				if( hypotr( y - p.y, x - p.x ) <= r )
					m[y * W + x] = 1;
	}

	// A lake as the exemplar's: an ellipse of its length and width as far from the middle as it was.
	void carveLake( Field& h, Plan& plan, Int index ) const
	{
		const InvWater& lake = src.inv.water[index];
		Real a = std::max( 0.5f * lake.length * f.k, 6.0f );
		Real b = std::max( 0.5f * lake.width * f.k, 5.0f );
		Real tilt = hashUnit( seed, 90 + index, 2 ) * PI_R;
		Mask wetNear, taken( W * H );
		maskGrow( plan.wet, W, H, 6, wetNear );
		for( Int i = 0; i < W * H; i++ ) taken[i] = wetNear[i] || plan.keepout[i] || !f.play[i];
		std::vector<Int> count;
		integral( taken, count );
		Int reachCells = (Int)( std::max( a, b ) + SHORE_CELLS ) + 1;
		struct Place { Real key; Int y, x; bool operator<( const Place& o ) const { return key != o.key ? key < o.key : ( y != o.y ? y < o.y : x < o.x ); } };
		std::vector<Place> places;
		for( Int my = reachCells; my < H - reachCells; my += TOWN_STEP )
		{
			for( Int mx = reachCells; mx < W - reachCells; mx += TOWN_STEP )
			{
				if( windowSum( count, my - reachCells, mx - reachCells, my + reachCells, mx + reachCells ) )
					continue;
				Real share = hypotr( my - f.cy, mx - f.cx ) / f.half;
				Place p;
				p.key = fabsf( share - std::min( lake.centreShare, 0.8f ) ) + 0.05f * hashUnit( seed, 90 + index, my * 4096 + mx );
				p.y = my;
				p.x = mx;
				places.push_back( p );
			}
		}
		std::sort( places.begin(), places.end() );
		Real bound = std::max( a, b ) * ( 1.0f + SHORE_CELLS / b ) + 2.0f;
		Int found = -1;
		for( size_t p = 0; p < places.size() && found < 0; p++ )
		{
			Along al( tilt, (Real)places[p].y, (Real)places[p].x );
			Bool clear = TRUE;
			for( Int y = std::max( 0, places[p].y - (Int)bound ); y <= std::min( H - 1, places[p].y + (Int)bound ) && clear; y++ )
				for( Int x = std::max( 0, places[p].x - (Int)bound ); x <= std::min( W - 1, places[p].x + (Int)bound ) && clear; x++ )
					if( inland( al, y, x, a, b ) > -SHORE_CELLS && f.guarded( y * W + x ) )
						clear = FALSE;
			if( clear ) found = (Int)p;
		}
		if( found < 0 )
		{
			plan.note( "lake of %d cells (no clear ground)", lake.area );
			return;
		}
		Int my = places[found].y, mx = places[found].x;
		Along al( tilt, (Real)my, (Real)mx );
		Real rimLow = 1.0e9f;
		for( Int y = std::max( 0, my - (Int)bound ); y <= std::min( H - 1, my + (Int)bound ); y++ )
			for( Int x = std::max( 0, mx - (Int)bound ); x <= std::min( W - 1, mx + (Int)bound ); x++ )
			{
				Real in = inland( al, y, x, a, b );
				if( in < -1.0f && in > -SHORE_CELLS ) rimLow = std::min( rimLow, h[y * W + x] );
			}
		Real surface = std::min( skel.bowl + lake.surface - src.lv.bowl, rimLow - 2.0f );
		Real bed = std::max( surface - lake.depth, 1.0f );
		// past the shore the target climbs RIVER_SLOPE a cell, so far enough out nothing is cut
		Real far = std::max( a, b ) * ( 1.0f + ( SHORE_CELLS + ( 255.0f - surface ) / RIVER_SLOPE ) / b ) + 2.0f;
		Field out = h;
		for( Int y = std::max( 0, my - (Int)far ); y <= std::min( H - 1, my + (Int)far ); y++ )
		{
			for( Int x = std::max( 0, mx - (Int)far ); x <= std::min( W - 1, mx + (Int)far ); x++ )
			{
				Int i = y * W + x;
				Real in = inland( al, y, x, a, b );
				Real target = in >= 0.0f ? surface + 3.0f - ( surface + 3.0f - bed ) * fade( in / SHORE_CELLS )
					: surface + 3.0f + RIVER_SLOPE * std::max( -in - SHORE_CELLS, 0.0f );
				out[i] = h[i] - ( 1.0f - f.guard[i] ) * std::max( h[i] - target, 0.0f );
				if( in > -2.0f )
				{
					plan.wet[i] = 1;
					plan.level[i] = std::max( plan.level[i], surface );
				}
				if( in < 0.0f && out[i] < h[i] - 0.5f ) plan.gentle[i] = 1;
			}
		}
		WaterPoly poly;
		for( Int k = 0; k < LAKE_POINTS; k++ )
		{
			Real t = 2.0f * PI_R * k / LAKE_POINTS;
			poly.points.push_back( Frame::point( tilt, (Real)my, (Real)mx, ( a + 2.0f ) * Cos( t ), ( b + 2.0f ) * Sin( t ) ) );
		}
		poly.surface = surface;
		plan.water.push_back( poly );
		h.swap( out );
	}

	static Real inland( const Along& al, Int y, Int x, Real a, Real b )
	{
		return ( 1.0f - hypotr( al.s( y, x ) / a, al.c( y, x ) / b ) ) * b;
	}

	void integral( const Mask& m, std::vector<Int>& out ) const
	{
		out.assign( ( W + 1 ) * ( H + 1 ), 0 );
		for( Int y = 0; y < H; y++ )
			for( Int x = 0; x < W; x++ )
				out[( y + 1 ) * ( W + 1 ) + x + 1] = m[y * W + x] + out[y * ( W + 1 ) + x + 1] + out[( y + 1 ) * ( W + 1 ) + x] - out[y * ( W + 1 ) + x];
	}

	/// Set cells in rows [y0, y1) and columns [x0, x1), the window clipped to the grid.
	Int windowSum( const std::vector<Int>& count, Int y0, Int x0, Int y1, Int x1 ) const
	{
		y0 = clampi( y0, 0, H ); y1 = clampi( y1, 0, H ); x0 = clampi( x0, 0, W ); x1 = clampi( x1, 0, W );
		if( y1 <= y0 || x1 <= x0 ) return 0;
		return count[y1 * ( W + 1 ) + x1] - count[y0 * ( W + 1 ) + x1] - count[y1 * ( W + 1 ) + x0] + count[y0 * ( W + 1 ) + x0];
	}

	// The sea along the edges the exemplar's touches, turned a quarter at a time (synthmap.carve_sea).
	void carveSea( Field& h, Plan& plan, Int index ) const
	{
		const InvWater& sea = src.inv.water[index];
		Real lowest = std::min( std::min( sea.edges[0], sea.edges[1] ), std::min( sea.edges[2], sea.edges[3] ) );
		if( lowest >= FULL_EDGE )
		{
			carveIslands( h, plan, index );
			return;
		}
		const Int bd = skm.border;
		const Real y0 = (Real)bd, x0 = (Real)bd, y1 = (Real)( bd + skm.playY - 1 ), x1 = (Real)( bd + skm.playX - 1 );
		const Real margin = SHORE_CELLS + VALLEY_CELLS;
		/* The coast bends round the sites instead of stopping short of the nearest: every start's and
			site's guard and a beach past it stay land whatever the width, so one pad near the edge
			no longer takes the whole sea away. The width is then the share's worth of the cells left. */
		Field fixedLand, fromStarts;
		seaLand( f.padRadius + 10.0f + SHORE_CELLS, SITE_OUTER + 10.0f + SHORE_CELLS, fixedLand, fromStarts );
		Int playCount = 0;
		for( Int i = 0; i < W * H; i++ ) playCount += f.play[i];
		Real bestWidth = 0.0f, bestTie = 0.0f;
		Int bestTurn = -1, bestSea = 0;
		Field bestD, d( W * H );
		std::vector<Int> bestFull, bestWet;
		Bool bestCorner = FALSE;
		YX bestCornerAt = yx( 0, 0 );
		std::vector<Real> order;
		for( Int turn = 0; turn < 4; turn++ )
		{
			std::vector<Int> wetEdges, full;
			for( Int i = 0; i < 4; i++ )
			{
				if( sea.edges[i] >= COAST_EDGE ) wetEdges.push_back( ( i + turn ) % 4 );
				if( sea.edges[i] >= FULL_EDGE ) full.push_back( ( i + turn ) % 4 );
			}
			if( wetEdges.empty() )
			{
				Int most = 0;
				for( Int i = 1; i < 4; i++ ) if( sea.edges[i] > sea.edges[most] ) most = i;
				wetEdges.push_back( ( most + turn ) % 4 );
			}
			Bool corner = FALSE;
			YX cornerAt = yx( 0, 0 );
			if( full.empty() && wetEdges.size() >= 2 )
			{
				Int e0 = std::min( wetEdges[0], wetEdges[1] ), e1 = std::max( wetEdges[0], wetEdges[1] );
				corner = TRUE;
				if( e0 == 0 && e1 == 1 ) cornerAt = yx( y0, x1 );
				else if( e0 == 1 && e1 == 2 ) cornerAt = yx( y1, x1 );
				else if( e0 == 2 && e1 == 3 ) cornerAt = yx( y1, x0 );
				else if( e0 == 0 && e1 == 3 ) cornerAt = yx( y0, x0 );
				else corner = FALSE;
			}
			Real phase = hashUnit( seed, 95, turn ) * 6.283f;
			order.clear();
			for( Int y = 0; y < H; y++ )
			{
				for( Int x = 0; x < W; x++ )
				{
					Int i = y * W + x;
					Real edge[4] = { y - y0, x1 - x, y1 - y, x - x0 };
					Real v;
					if( !full.empty() )
					{
						v = 1.0e9f;
						for( size_t e = 0; e < full.size(); e++ ) v = std::min( v, edge[full[e]] );
					}
					else if( corner )
						v = hypotr( y - cornerAt.y, x - cornerAt.x );
					else
						v = edge[wetEdges[0]];
					v += COAST_WOBBLE * Sin( 2.0f * PI_R * ( y + x ) / 97.0f + phase );
					d[i] = v;
					// a cell is sea once the width passes its v, unless it is held land
					if( f.play[i] && fixedLand[i] < 0.0f ) order.push_back( v );
				}
			}
			if( order.empty() )
				continue;
			size_t at = std::min( (size_t)( sea.share * playCount ), order.size() - 1 );
			std::nth_element( order.begin(), order.begin() + at, order.end() );
			Real width = order[at];
			Int seaCells = (Int)at;
			// the most sea, then the coast that bends least round the land it holds: the narrowest
			Real tie = hashUnit( seed, 96, turn );
			if( bestTurn < 0 || seaCells > bestSea || ( seaCells == bestSea && ( width < bestWidth || ( width == bestWidth && tie < bestTie ) ) ) )
			{
				bestTurn = turn; bestWidth = width; bestTie = tie; bestSea = seaCells;
				bestD = d; bestFull = full; bestWet = wetEdges; bestCorner = corner; bestCornerAt = cornerAt;
			}
		}
		if( bestTurn < 0 || bestWidth <= 0.0f )
		{
			plan.note( "sea of %d cells (no coast clear of the sites)", sea.area );
			return;
		}
		Real reachCells = bestWidth + COAST_WOBBLE + margin + 10.0f;
		const Real xe = x1 + bd, ye = y1 + bd;
		YX strips[4][4] = {
			{ yx( 0, 0 ), yx( 0, xe ), yx( reachCells + y0, xe ), yx( reachCells + y0, 0 ) },
			{ yx( 0, x1 - reachCells ), yx( 0, xe ), yx( ye, xe ), yx( ye, x1 - reachCells ) },
			{ yx( y1 - reachCells, 0 ), yx( y1 - reachCells, xe ), yx( ye, xe ), yx( ye, 0 ) },
			{ yx( 0, 0 ), yx( 0, reachCells + x0 ), yx( ye, reachCells + x0 ), yx( ye, 0 ) } };
		std::vector< std::vector<YX> > boxes;
		if( !bestFull.empty() )
		{
			for( size_t e = 0; e < bestFull.size(); e++ )
				boxes.push_back( std::vector<YX>( strips[bestFull[e]], strips[bestFull[e]] + 4 ) );
		}
		else if( bestCorner )
		{
			Real sy = bestCornerAt.y > f.cy ? 1.0f : -1.0f, sx = bestCornerAt.x > f.cx ? 1.0f : -1.0f;
			Real far = reachCells + bd;
			std::vector<YX> box;
			box.push_back( yx( bestCornerAt.y + sy * bd, bestCornerAt.x + sx * bd ) );
			box.push_back( yx( bestCornerAt.y + sy * bd, bestCornerAt.x - sx * far ) );
			box.push_back( yx( bestCornerAt.y - sy * far, bestCornerAt.x - sx * far ) );
			box.push_back( yx( bestCornerAt.y - sy * far, bestCornerAt.x + sx * bd ) );
			boxes.push_back( box );
		}
		else
			boxes.push_back( std::vector<YX>( strips[bestWet[0]], strips[bestWet[0]] + 4 ) );
		for( Int i = 0; i < W * H; i++ ) bestD[i] = std::max( bestD[i] - bestWidth, fixedLand[i] );
		laySea( h, plan, sea, bestD, boxes );
	}

	/** Ground a sea leaves dry whatever its size, as inland cells (over 0 is land): startLand round each
		start, siteLand round each dock and derrick and the middle, and unless the exemplar's water parts
		the starts, a causeway from each start to the middle and from each dock and derrick to the
		nearer of the middle and its nearest start, so nothing stands on an island of its own. fromStarts
		is each cell's distance to the nearest start. */
	void seaLand( Real startLand, Real siteLand, Field& fixedLand, Field& fromStarts ) const
	{
		fixedLand.assign( W * H, 0.0f );
		fromStarts.assign( W * H, 0.0f );
		const YX middle = yx( f.cy, f.cx );
		std::vector<YX> coreSites, anchors;
		for( size_t k = f.starts.size(); k < f.sites.size(); k++ )
		{
			YX p = yx( (Real)(Int)f.sites[k].y, (Real)(Int)f.sites[k].x );
			YX to = middle;
			Real best = hypotr( p.y - middle.y, p.x - middle.x );
			for( size_t s = 0; s < f.starts.size(); s++ )
			{
				Real d = hypotr( p.y - f.starts[s].y, p.x - f.starts[s].x );
				if( d < best ) { best = d; to = f.starts[s]; }
			}
			coreSites.push_back( p );
			anchors.push_back( to );
		}
		const YX core = yx( (Real)(Int)f.cy, (Real)(Int)f.cx );
		const Bool causeways = !src.inv.split;
		for( Int y = 0; y < H; y++ )
		{
			for( Int x = 0; x < W; x++ )
			{
				Int i = y * W + x;
				Real ds = 1.0e9f, droad = 1.0e9f;
				for( size_t s = 0; s < f.starts.size(); s++ )
				{
					ds = std::min( ds, hypotr( y - f.starts[s].y, x - f.starts[s].x ) );
					if( causeways )
						droad = std::min( droad, segmentDistance( (Real)y, (Real)x, f.starts[s], middle ) );
				}
				Real dsite = hypotr( y - core.y, x - core.x );
				for( size_t s = 0; s < coreSites.size(); s++ )
				{
					dsite = std::min( dsite, hypotr( y - coreSites[s].y, x - coreSites[s].x ) );
					if( causeways )
						droad = std::min( droad, segmentDistance( (Real)y, (Real)x, coreSites[s], anchors[s] ) );
				}
				fromStarts[i] = ds;
				fixedLand[i] = std::max( std::max( siteLand - dsite, CAUSEWAY_CELLS - droad ), startLand - ds );
			}
		}
	}

	// An island map's sea: land round every start, dock and derrick and the middle (synthmap.carve_islands).
	void carveIslands( Field& h, Plan& plan, Int index ) const
	{
		const InvWater& sea = src.inv.water[index];
		/* A dock's or derrick's guard on land, its beach inside the guard's fade, and the starts' land the
			radius's: the floors are cells, not shares, so on a small map with every seat filled a
			beach-wide margin more round every site was a third of the sea. */
		Field fixedLand, fromStarts;
		seaLand( 0.0f, SITE_OUTER + 10.0f, fixedLand, fromStarts );
		Real phase = hashUnit( seed, 95, 0 ) * 6.283f;
		for( Int y = 0; y < H; y++ )
		{
			for( Int x = 0; x < W; x++ )
			{
				Int i = y * W + x;
				Real wobble = COAST_WOBBLE * Sin( 2.0f * PI_R * ( y + x ) / 97.0f + phase ) * Sin( 2.0f * PI_R * ( y - x ) / 131.0f );
				fixedLand[i] += wobble;
				fromStarts[i] -= wobble;		// land = max(radius - ds, fixed - wobble) + wobble
			}
		}
		// land(radius) = max(radius - fromStarts + wobble, fixedLand), as fromStarts already holds -wobble;
		// the least a beach past the pad, whatever the pad was scaled to
		Real least = f.padRadius + SHORE_CELLS;
		Real radius = least;
		Int playCount = 0;
		for( Int i = 0; i < W * H; i++ ) playCount += f.play[i];
		for( Real trial = least; trial < 0.5f * std::min( H, W ); trial += 2.0f )
		{
			Int seaCells = 0;
			for( Int i = 0; i < W * H; i++ )
				if( f.play[i] && std::max( trial - fromStarts[i], fixedLand[i] ) < 0.0f )
					seaCells++;
			if( (Real)seaCells / playCount < sea.share )
				break;
			radius = trial;
		}
		Field inlandField( W * H );
		for( Int i = 0; i < W * H; i++ )
			inlandField[i] = std::max( radius - fromStarts[i], fixedLand[i] );
		std::vector< std::vector<YX> > boxes( 1 );
		boxes[0].push_back( yx( 0, 0 ) );
		boxes[0].push_back( yx( 0, (Real)( W - 1 ) ) );
		boxes[0].push_back( yx( (Real)( H - 1 ), (Real)( W - 1 ) ) );
		boxes[0].push_back( yx( (Real)( H - 1 ), 0 ) );
		laySea( h, plan, sea, inlandField, boxes );
	}

	// The sea where inland is under 0: its bed, a beach, higher ground cut back (synthmap.lay_sea).
	void laySea( Field& h, Plan& plan, const InvWater& sea, const Field& inlandField, const std::vector< std::vector<YX> >& boxes ) const
	{
		/* The exemplar's bowl is the median of every playable height, and on an island map half of those
			are sea bed: the bowl comes out under the land and the sea a byte or two under the skeleton's
			ground, a coast flat at the waterline. The exemplar's own bank over its water bounds it. */
		Real surface = std::max( std::min( skel.bowl + sea.surface - src.lv.bowl, skel.bowl - std::max( sea.bank, 4.0f ) ), SEA_FLOOR );
		Real bed = std::max( surface - std::max( sea.depth, 6.0f ), 0.0f );
		Mask footprint( W * H, 0 );
		for( size_t k = 0; k < boxes.size(); k++ )
		{
			WaterPoly poly;
			Real ylo = 1.0e9f, yhi = -1.0e9f, xlo = 1.0e9f, xhi = -1.0e9f;
			for( size_t p = 0; p < boxes[k].size(); p++ )
			{
				YX q = yx( clampr( boxes[k][p].y, 0.0f, (Real)( H - 1 ) ), clampr( boxes[k][p].x, 0.0f, (Real)( W - 1 ) ) );
				poly.points.push_back( q );
				ylo = std::min( ylo, q.y ); yhi = std::max( yhi, q.y ); xlo = std::min( xlo, q.x ); xhi = std::max( xhi, q.x );
			}
			poly.surface = surface;
			plan.water.push_back( poly );
			for( Int y = (Int)ylo; y <= (Int)yhi && y < H; y++ )
				for( Int x = (Int)xlo; x <= (Int)xhi && x < W; x++ )
					footprint[y * W + x] = 1;
		}
		Int got = 0, playCount = 0;
		for( Int i = 0; i < W * H; i++ )
		{
			Real in = inlandField[i];
			Real target = in < 0.0f ? surface - ( surface - bed ) * fade( -in / SHORE_CELLS )
				: surface + 2.0f + RIVER_SLOPE * std::max( in - 2.0f, 0.0f );
			Real out = h[i] - ( 1.0f - f.guard[i] ) * std::max( h[i] - target, 0.0f );
			if( footprint[i] )
			{
				if( in >= 0.0f && out < surface + 2.0f ) out = surface + 2.0f;
				if( in < 1.0f ) plan.wet[i] = 1;
				plan.level[i] = std::max( plan.level[i], surface );
				if( in >= 0.0f && out < h[i] - 0.5f ) plan.gentle[i] = 1;
				got += in < 1.0f && f.play[i];
			}
			playCount += f.play[i];
			h[i] = out;
		}
		if( got < 0.8f * sea.share * playCount )
			plan.note( "sea at %d%% of the map against the exemplar's %d%%", 100 * got / std::max( playCount, 1 ), (Int)( 100.0f * sea.share ) );
	}

	// How a trial layout of water treats the starts (synthmap.judge).
	void judge( const Field& out, const Plan& trial, Real *clearOut, Bool *splitOut ) const
	{
		Mask wet( W * H ), cliff, walk;
		std::vector<Int> wetCells;
		for( Int i = 0; i < W * H; i++ )
		{
			wet[i] = trial.wet[i] && out[i] < trial.level[i];
			if( wet[i] ) wetCells.push_back( i );
		}
		Real spacing = 1.0e9f;
		for( size_t i = 0; i < f.starts.size(); i++ )
			for( size_t j = i + 1; j < f.starts.size(); j++ )
				spacing = std::min( spacing, hypotr( f.starts[i].y - f.starts[j].y, f.starts[i].x - f.starts[j].x ) );
		Real gap = 1.0e9f;
		for( size_t s = 0; s < f.starts.size(); s++ )
			gap = std::min( gap, distanceToSet( wetCells, W, (Int)f.starts[s].y, (Int)f.starts[s].x ) );
		*clearOut = gap / spacing;
		spanCliff( out, W, H, cliff );
		walkable( cliff, W, H, walk );
		for( Int i = 0; i < W * H; i++ ) walk[i] = walk[i] && !wet[i];
		Mask seen;
		Int first = (Int)f.starts[0].y * W + (Int)f.starts[0].x;
		reach( walk, W, H, first, seen );
		Bool split = FALSE;
		for( size_t s = 1; s < f.starts.size(); s++ )
		{
			Int i = (Int)f.starts[s].y * W + (Int)f.starts[s].x;
			// a start standing on unwalkable ground is in a piece of its own, as scipy labels it
			split = split || !seen[i] || ( !walk[first] && !walk[i] ) ;
		}
		*splitOut = split;
	}

	struct LineSpec
	{
		Real angle, my, mx, sLo, sHi;
		Bool anchored;
		Int salt;
		Real calmAt, calmReach;		///< no wander within calmReach of calmAt, where the crossing lies; 0 for none
		Real calmWidth;				///< the crossing's reach across the river
		Int crop[4];				///< the crossing's box as it fitted
		Int cropAt;					///< which of the crossing's cuts that is, CROSS_CROPS for none
	};

	/// The crossing laid along the line with its middle at s, turned so its river runs the line's way,
	/// mirrored or not as the seed says.
	Copy crossingCopy( const LineSpec& line, Real s ) const
	{
		Copy best;
		best.turn = 0;
		best.mirror = ( synthHash( seed, 150, 0 ) & 1 ) != 0;
		best.spinBy( 0.0f );
		best.spinBy( line.angle - best.angle( cross.angle ) );
		best.target = Frame::point( line.angle, line.my, line.mx, s, 0.0f );
		best.source = cross.crops[line.cropAt].pivot;
		best.scale = 1.0f;
		best.radius = best.core = 0.0f;
		best.lift = best.pivot = 0.0f;
		best.heightScale = 1.0f;
		best.raised = FALSE;
		best.region = NULL;
		best.regionW = best.regionH = 0;
		memcpy( best.box, line.crop, sizeof(best.box) );
		return best;
	}

	/// The first place along the line, out from its middle, where the crossing lies clear of every
	/// pad, dock and derrick and inside the map; the whole of it, or the first of its cuts that fits.
	Bool calmSpot( LineSpec& line ) const
	{
		for( Int crop = 0; crop < CROSS_CROPS; crop++ )
		{
			const Crop& cut = cross.crops[crop];
			memcpy( line.crop, cut.box, sizeof(line.crop) );
			line.cropAt = crop;
			for( Int step = 0; step * CROSS_STEP < f.half; step++ )
			{
				for( Int sign = -1; sign <= 1; sign += 2 )
				{
					if( step == 0 && sign > 0 ) continue;
					Real s = (Real)( sign * step * CROSS_STEP );
					Copy c = crossingCopy( line, s );
					Int area[4];
					c.bounds( W, H, area );
					Bool clear = TRUE;
					for( Int y = area[0]; y <= area[2] && clear; y += 2 )
						for( Int x = area[1]; x <= area[3] && clear; x += 2 )
							if( c.weight( y, x, COPY_FADE ) > 0.0f )
								clear = f.play[y * W + x] && f.guard[y * W + x] < CROSS_GUARD;
					if( !clear ) continue;
					line.calmAt = s;
					line.calmReach = cut.along + CALM_EASE;
					line.calmWidth = cut.across;
					return TRUE;
				}
			}
		}
		line.cropAt = CROSS_CROPS;
		return FALSE;
	}

	/// The river's own bridges less those the crossing on the line brings.
	void bridgesOutside( const LineSpec& line, const std::vector<const InvBridge *>& all, std::vector<const InvBridge *>& out ) const
	{
		out = all;
		if( line.calmReach <= 0.0f )
			return;
		size_t inside = 0;
		for( size_t k = 0; k < cross.bridges.size(); k++ )
			inside += bridgeIn( line.crop, cross.bridges[k], 0 );
		out.resize( out.size() - std::min( out.size(), inside ) );
	}

	/// The crossing on the river: its ground at the river's surface, its water, its bridges, its towns.
	void layCrossing( Field& h, Plan& plan, const LineSpec& line, Real surface, const InvWater& river ) const
	{
		Copy c = crossingCopy( line, line.calmAt );
		c.lift = surface - river.surface;
		layCopy( h, plan, c );
		Int area[4];
		c.bounds( W, H, area );
		const Int SW = src.m.W;
		for( Int y = area[0]; y <= area[2]; y++ )
		{
			for( Int x = area[1]; x <= area[3]; x++ )
			{
				Int i = y * W + x;
				if( c.weight( y, x, COPY_FADE ) < 0.5f ) continue;
				YX s = c.toSource( (Real)y, (Real)x );
				Int o = clampi( roundi( s.y ), 0, src.m.H - 1 ) * SW + clampi( roundi( s.x ), 0, SW - 1 );
				plan.wet[i] = cross.wet[o];
				plan.level[i] = cross.wet[o] ? surface : -1.0f;
			}
		}
		/* Every bridge inside the cut stays the crossing's: handed back to the river it found no
			crossing clear of the sites and was lost. One whose end falls on the fade ring would stand
			on the carved bed there, so its ends are raised to the bank, as the river does its own. */
		const Int sb = src.m.border;
		const Real bank = surface + std::max( river.bank, 4.0f );
		for( size_t k = 0; k < cross.bridges.size(); k++ )
		{
			if( !bridgeIn( line.crop, cross.bridges[k], 0 ) ) continue;
			const MapThing& one = src.m.things[cross.bridges[k]];
			const MapThing& two = src.m.things[cross.bridges[k] + 1];
			PlannedBridge bridge;
			bridge.name = one.name;
			bridge.a = c.toTarget( one.y + sb, one.x + sb );
			bridge.z = c.toTarget( two.y + sb, two.x + sb );
			if( !bridgeIn( line.crop, cross.bridges[k], (Int)COPY_FADE ) )
			{
				discAtLeast( h, bridge.a, BRIDGE_END, bank );
				discAtLeast( h, bridge.z, BRIDGE_END, bank );
			}
			plan.bridges.push_back( bridge );
		}
		// a river town is done whatever the cut carried: stamped again it would stand twice where the cut
		// kept part of it, and bring its river banks onto dry ground; what the cut left is said
		for( size_t k = 0; k < cross.towns.size(); k++ )
		{
			const InvTown& town = src.inv.towns[cross.towns[k]];
			Int carried = 0;
			for( size_t j = 0; j < town.houses.size(); j++ )
				carried += c.carries( town.houses[j].y, town.houses[j].x );
			if( carried < town.buildings )
				plan.note( "town of %d buildings on the river (%d on the crossing)", town.buildings, carried );
			plan.townDone[cross.towns[k]] = 1;
		}
	}

	void riverBridges( Int index, std::vector<const InvBridge *>& out ) const
	{
		for( size_t k = 0; k < src.inv.bridges.size(); k++ )
			if( src.inv.bridges[k].over == OVER_WATER && src.inv.bridges[k].water == index )
				out.push_back( &src.inv.bridges[k] );
	}

	/// A river's line; one that carries the crossing calms where the crossing finds room.
	LineSpec riverLine( const InvWater& river, Int salt, Real side, Bool edge, Bool crossing ) const
	{
		LineSpec line;
		line.angle = f.axis( seed, salt, side );
		YX m = Frame::point( line.angle, f.cy, f.cx, 0.0f, side );
		line.my = m.y;
		line.mx = m.x;
		Real length = edge ? 0.5f * hypotr( (Real)H, (Real)W ) + 20.0f : 0.5f * river.length * f.k;
		line.sLo = -length;
		line.sHi = length;
		line.anchored = FALSE;
		line.salt = salt;
		line.calmAt = line.calmReach = line.calmWidth = 0.0f;
		memset( line.crop, 0, sizeof(line.crop) );
		line.cropAt = CROSS_CROPS;
		if( crossing )
			calmSpot( line );
		return line;
	}

	Real riverSurface( const InvWater& river ) const
	{
		return std::max( skel.bowl + river.surface - src.lv.bowl, river.depth + 2.0f );
	}

	/* A river along line (synthmap.carve_river); meetsOut says whether its water ran onto water already
		there, dammedOut how many cells of its bed a pad, dock or derrick held dry. */
	void carveRiver( const Field& h, Plan& plan, const InvWater& river, Int index, const LineSpec& line,
		const std::vector<const InvBridge *>& bridges, Bool hasSurface, Real givenSurface, Field& out, Bool *meetsOut,
		Int *dammedOut = NULL ) const
	{
		Real surface = hasSurface ? givenSurface : riverSurface( river );
		Real bed = surface - river.depth;
		Real bank = surface + std::max( river.bank, 4.0f );
		Along al( line.angle, line.my, line.mx );
		Real phase = hashUnit( seed, line.salt, 3 ) * 2.0f * PI_R;
		Real wave = std::max( river.length / 1.5f, 120.0f );
		Real amp = 1.4f * river.winding * f.k;
		const Bool anchored = line.anchored;
		const Bool calm = line.calmReach > 0.0f;
		auto wander = [&]( Real at ) -> Real
		{
			Real settle = anchored ? fade( at / BRANCH_SETTLE ) : 1.0f;
			if( calm )
				settle *= 1.0f - fade( ( line.calmReach - fabsf( at - line.calmAt ) ) / CALM_EASE );
			return amp * Sin( 2.0f * PI_R * at / wave + phase ) * settle;
		};
		auto onPlay = [&]( YX p ) -> Bool
		{
			Int y = clampi( roundi( p.y ), 0, H - 1 ), x = clampi( roundi( p.x ), 0, W - 1 );
			return f.play[y * W + x] != 0;
		};
		// a bridge at along, square across the river there, its ends reach either side of the middle
		auto bridgeEnds = [&]( Real at, Real reach, YX *a, YX *z )
		{
			Real centre = wander( at );
			Real turn = ATan( ( wander( at + 1.0f ) - wander( at - 1.0f ) ) / 2.0f );
			*a = Frame::point( line.angle, line.my, line.mx, at - reach * Sin( turn ), centre + reach * Cos( turn ) );
			*z = Frame::point( line.angle, line.my, line.mx, at + reach * Sin( turn ), centre - reach * Cos( turn ) );
		};

		Real s0 = line.sLo, s1 = line.sHi;
		Bool anyInside = FALSE;
		for( Real v = line.sLo; v < line.sHi; v += 2.0f )
		{
			if( !onPlay( Frame::point( line.angle, line.my, line.mx, v, wander( v ) ) ) ) continue;
			if( !anyInside ) { s0 = v; anyInside = TRUE; }
			s1 = v;
		}
		if( anchored ) s0 = std::max( s0, JUNCTION_CLEAR );

		// evenly along it, each moved to the nearest place clear of every site and of the water already there
		std::vector<Real> sites;
		std::vector<const InvBridge *> ours;
		for( size_t i = 0; i < bridges.size(); i++ )
		{
			Real even = s0 + ( i + 1 ) * ( s1 - s0 ) / ( bridges.size() + 1 );
			Bool placed = FALSE;
			for( Int n = 0; n <= 24 && !placed; n++ )
			{
				for( Int sign = -1; sign <= 1 && !placed; sign += 2 )
				{
					if( n == 0 && sign == 1 ) continue;
					Real nudge = (Real)( sign * 4 * n );
					if( n == 0 ) nudge = 0.0f;
					if( nudge > 48.0f ) continue;
					// the deck and a ramp's length past each end on open ground, off the pads' cores
					YX a, z;
					bridgeEnds( even + nudge, 0.5f * bridges[i]->length + BRIDGE_DECK, &a, &z );
					Real span = hypotr( z.y - a.y, z.x - a.x );
					Bool ok = TRUE;
					for( Real t = 0.0f; t <= span && ok; t += 1.0f )
					{
						Int y = roundi( a.y + ( z.y - a.y ) * t / span ), x = roundi( a.x + ( z.x - a.x ) * t / span );
						Int c = y * W + x;
						ok = y >= 0 && y < H && x >= 0 && x < W && f.play[c] && f.guard[c] < CROSS_GUARD && !plan.wet[c];
					}
					for( size_t s = 0; s < sites.size() && ok; s++ ) ok = fabsf( even + nudge - sites[s] ) > 30.0f;
					// past the crossing's fade, where the river still runs straight out of it
					ok = ok && ( !calm || fabsf( even + nudge - line.calmAt ) > line.calmReach - CALM_EASE + COPY_FADE + BRIDGE_DECK );
					if( ok )
					{
						sites.push_back( even + nudge );
						ours.push_back( bridges[i] );
						placed = TRUE;
					}
				}
			}
			if( !placed )
				plan.note( "%s bridge (no crossing clear of the sites)", bridges[i]->name.str() );
		}

		/* A river the exemplar bridges keeps a bridge of its own however crowded the map: where the pads
			leave no clear crossing, the one whose deck runs least into a pad's fade, never over a pad
			itself; over the crossing's own water when that is all the river has. */
		if( ours.empty() && !bridges.empty() )
		{
			Real best = 1.0f, bestAt = 0.0f;
			for( Real v = s0; v <= s1; v += 2.0f )
			{
				YX a, z;
				bridgeEnds( v, 0.5f * bridges[0]->length + BRIDGE_DECK, &a, &z );
				Real span = hypotr( z.y - a.y, z.x - a.x ), worst = 0.0f;
				for( Real t = 0.0f; t <= span && worst <= best; t += 1.0f )
				{
					Int y = roundi( a.y + ( z.y - a.y ) * t / span ), x = roundi( a.x + ( z.x - a.x ) * t / span );
					Int c = y * W + x;
					Bool open = y >= 0 && y < H && x >= 0 && x < W && f.play[c];
					worst = open ? std::max( worst, f.guard[c] ) : 1.0f;
				}
				// of two as clear, the one nearer the middle of the map
				if( worst < best || ( worst == best && worst < 1.0f && fabsf( v ) < fabsf( bestAt ) ) )
				{
					best = worst;
					bestAt = v;
				}
			}
			if( best < 1.0f )
			{
				sites.push_back( bestAt );
				ours.push_back( bridges[0] );
			}
		}

		// a river through a crossing keeps the exemplar's width, or it would not meet the town's banks
		Real half = std::max( 0.5f * ( river.width * ( calm ? 1.0f : f.k ) - SHORE_CELLS ), 3.0f );
		Real reachC = half + SHORE_CELLS + VALLEY_CELLS + ( 255.0f - bank ) / RIVER_SLOPE + amp + 2.0f;
		out = h;
		Bool meets = FALSE;
		Int dammed = 0;
		Real sFrom = anchored ? line.sLo - VALLEY_CELLS : line.sLo;
		for( Int y = 0; y < H; y++ )
		{
			for( Int x = 0; x < W; x++ )
			{
				Real s = al.s( y, x );
				if( s < sFrom || s > line.sHi ) continue;
				Real c = al.c( y, x );
				if( fabsf( c ) > reachC ) continue;
				Int i = y * W + x;
				Real narrow = 1.0f;
				for( size_t k = 0; k < sites.size(); k++ )
				{
					Real to = std::min( 1.0f, std::max( ours[k]->length - 2.0f * SHORE_CELLS - 4.0f, 6.0f ) / ( 2.0f * half ) );
					narrow = std::min( narrow, 1.0f - ( 1.0f - to ) * fade( ( 24.0f - fabsf( s - sites[k] ) ) / 12.0f ) );
				}
				Real inner = half * narrow;
				Real edge = inner + 0.5f * SHORE_CELLS + 2.0f;		// the polygon's reach
				Real d = fabsf( c - wander( s ) );
				Real outside = d - inner - SHORE_CELLS - VALLEY_CELLS;
				Real target = d < inner ? bed :
					d < inner + SHORE_CELLS ? bed + ( surface + 3.0f - bed ) * ( d - inner ) / SHORE_CELLS :
					outside < 0.0f ? surface + 3.0f + ( bank - surface - 3.0f ) * fade( ( d - inner - SHORE_CELLS ) / VALLEY_CELLS ) :
					bank + RIVER_SLOPE * outside;
				// a branch fades out behind its junction, in the main river's bed rather than across the map
				Real ends = std::min( anchored ? VALLEY_CELLS + ( s - line.sLo ) : s - line.sLo, line.sHi - s );
				Real w = fade( ends / VALLEY_CELLS ) * ( 1.0f - f.guard[i] );
				Bool within = s >= line.sLo && s <= line.sHi;
				// and behind it is the main river's far bank, dry ground no lower than the plane, not a trench
				if( !within )
					target = std::max( target, surface + 1.0f );
				Real cut = h[i] - w * std::max( h[i] - target, 0.0f );
				out[i] = std::min( h[i], cut );
				/* Past the waterline the ground keeps over the plane, falling away at a bank's slope: a canyon
					or low ground beside the river would sit under it outside the polygon, the water hanging
					over dry ground. As much as the cut, so it fades where the cut does, at the river's ends
					and into a pad's guard, rather than stop in a step. */
				if( d >= edge && !plan.wet[i] )
				{
					Real levee = surface + 1.0f - RIVER_SLOPE * ( d - edge );
					if( levee > out[i] )
						out[i] += w * ( levee - out[i] );
				}
				/* The plane lies over the whole band, but the water only where the cut went under it: where a
					pad, dock or derrick held its ground the bed is dry, and wet there would freeze a trench
					and tell the towns and bridges there is water where there is none. The level stays, so
					the ground under the plane is still lifted clear of it rather than left a pond. */
				if( within && d < inner + 0.5f * SHORE_CELLS )
				{
					if( out[i] < surface )
					{
						meets = meets || plan.wet[i];
						plan.wet[i] = 1;
					}
					else if( d < inner && f.guard[i] > 0.5f )
						dammed++;
					plan.level[i] = std::max( plan.level[i], surface );
				}
				if( d >= inner && out[i] < h[i] - 0.5f ) plan.gentle[i] = 1;
			}
		}
		if( meetsOut ) *meetsOut = meets;
		if( dammedOut ) *dammedOut = dammed;

		// the water area: the waterline either side of the bed, every eight cells along
		WaterPoly poly;
		std::vector<YX> right;
		Real width = half + 0.5f * SHORE_CELLS + 2.0f;
		for( Real v = line.sLo; v < line.sHi + 8.0f; v += 8.0f )
		{
			// over the crossing as wide as its box: the town's ground stands over the plane, its river under it
			Real reachW = calm && fabsf( v - line.calmAt ) < line.calmReach ? std::max( width, line.calmWidth ) : width;
			YX l = Frame::point( line.angle, line.my, line.mx, v, wander( v ) + reachW );
			YX r = Frame::point( line.angle, line.my, line.mx, v, wander( v ) - reachW );
			poly.points.push_back( yx( clampr( l.y, 0.0f, (Real)( H - 1 ) ), clampr( l.x, 0.0f, (Real)( W - 1 ) ) ) );
			right.push_back( yx( clampr( r.y, 0.0f, (Real)( H - 1 ) ), clampr( r.x, 0.0f, (Real)( W - 1 ) ) ) );
		}
		for( Int k = (Int)right.size() - 1; k >= 0; k-- ) poly.points.push_back( right[k] );
		poly.surface = surface;
		plan.water.push_back( poly );
		RiverLine record;
		record.index = index;
		record.surface = surface;
		record.half = half;
		for( Real v = s0; v < s1; v += 4.0f )
			record.points.push_back( Frame::point( line.angle, line.my, line.mx, v, wander( v ) ) );
		for( size_t k = 0; k < sites.size(); k++ )
			record.sites.push_back( Frame::point( line.angle, line.my, line.mx, sites[k], wander( sites[k] ) ) );
		plan.lines.push_back( record );

		for( size_t k = 0; k < sites.size(); k++ )
		{
			YX a, z;
			bridgeEnds( sites[k], 0.5f * ours[k]->length, &a, &z );
			disc( out, a, BRIDGE_END, bank );
			disc( out, z, BRIDGE_END, bank );
			discMask( plan.gentle, a, 10.0f );
			discMask( plan.gentle, z, 10.0f );
			PlannedBridge bridge;
			bridge.name = ours[k]->name;
			bridge.a = a;
			bridge.z = z;
			plan.bridges.push_back( bridge );
		}
	}

	struct RiverScore
	{
		Bool meets, splits;
		Int blocked;		///< 2 the line runs through a pad, 1 a dock, derrick or pad's edge holds part of its bed dry
		Bool shape;			///< it reaches the edge where the exemplar's does not, or the other way
		Bool near;
		Int cropAt;			///< the cut of the crossing that found room on it, CROSS_CROPS for none: the less cut, the more town
		Real off, clear;
		bool operator<( const RiverScore& o ) const
		{
			// the river whole and between the starts first; the town on it where that leaves room
			if( meets != o.meets ) return !meets;
			if( splits != o.splits ) return !splits;
			if( blocked != o.blocked ) return blocked < o.blocked;
			if( shape != o.shape ) return !shape;
			if( near != o.near ) return !near;
			if( cropAt != o.cropAt ) return cropAt < o.cropAt;
			if( off != o.off ) return off < o.off;
			return -clear < -o.clear;
		}
		Bool sameHead( const RiverScore& o ) const
		{
			return meets == o.meets && splits == o.splits && blocked == o.blocked && shape == o.shape && near == o.near && cropAt == o.cropAt;
		}
	};

	// carve_river where its nearest start stands as far from the water as the exemplar's (synthmap.place_river).
	void placeRiver( Field& h, Plan& plan, Int index, Int order, Bool crossing ) const
	{
		const InvWater& river = src.inv.water[index];
		Int salt = 78 + 7 * order;
		Real want = river.startClear;
		Bool maySplit = src.inv.split;
		Real first = hashUnit( seed, salt, 4 ) < 0.5f ? 1.0f : -1.0f;
		// the crossing brings its own bridges; the rest go along the river
		std::vector<const InvBridge *> allBridges, outside;
		riverBridges( index, allBridges );
		struct Ranked { RiverScore score; LineSpec line; Int number; };
		std::vector<Ranked> ranked;
		static const Real theSteps[] = { 0.0f, 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.45f, -0.45f };
		Field out;
		for( Int st = 0; st < 9; st++ )
		{
			Real share = river.centreShare + theSteps[st];
			if( share < -0.01f || share > 0.9f ) continue;
			share = std::max( share, 0.0f );
			for( Int sg = 0; sg < 2; sg++ )
			{
				Real sign = sg == 0 ? first : -first;
				for( Int e = 0; e < 2; e++ )
				{
					Bool edge = e == 0 ? river.reachesEdge : !river.reachesEdge;
					Plan trial = plan;
					LineSpec line = riverLine( river, salt, sign * share * f.half, edge, crossing );
					Bool meets = FALSE;
					Int dammed = 0;
					bridgesOutside( line, allBridges, outside );
					carveRiver( h, trial, river, index, line, outside, FALSE, 0.0f, out, &meets, &dammed );
					Real clear;
					Bool split;
					judge( out, trial, &clear, &split );
					/* A line through a pad leaves the river dry where the pad keeps its ground, the worst; one
						over a dock or derrick is dammed there right across, two pools under one plane: the bed
						the carve could not cut is counted, the river's wander and width included. */
					Bool throughPad = FALSE;
					for( Real v = line.sLo; v < line.sHi && !throughPad; v += 4.0f )
					{
						YX p = Frame::point( line.angle, line.my, line.mx, v, 0.0f );
						for( size_t s = 0; s < f.starts.size() && !throughPad; s++ )
							throughPad = hypotr( p.y - f.starts[s].y, p.x - f.starts[s].x ) < f.padRadius + 0.5f * COPY_FADE;
					}
					Ranked r;
					r.score.blocked = throughPad ? 2 : dammed > 0 ? 1 : 0;
					r.score.shape = edge != river.reachesEdge;
					r.score.cropAt = line.cropAt;
					r.score.meets = meets;
					r.score.splits = split && !maySplit;
					r.score.near = clear < want;
					r.score.off = fabsf( theSteps[st] ) + ( edge != river.reachesEdge ? 0.05f : 0.0f );
					r.score.clear = clear;
					r.line = line;
					r.number = (Int)ranked.size();
					ranked.push_back( r );
				}
			}
		}
		std::stable_sort( ranked.begin(), ranked.end(), []( const Ranked& a, const Ranked& b ) { return a.score < b.score; } );
		// of the best few, the one that leaves the fewest towns and tech buildings unbuilt
		Int bestNotes = 0x7fffffff, best = 0;
		for( Int rank = 0; rank < RIVER_TRIES && rank < (Int)ranked.size(); rank++ )
		{
			if( !ranked[rank].score.sameHead( ranked[0].score ) ) break;
			Plan trial = plan;
			layRiver( h, trial, index, ranked[rank].line, allBridges, out );
			stampTowns( out, trial );
			placeTech( out, trial );
			if( (Int)trial.notes.size() < bestNotes )
			{
				bestNotes = (Int)trial.notes.size();
				best = rank;
			}
		}
		layRiver( h, plan, index, ranked[best].line, allBridges, out );
		h.swap( out );
		if( crossing && ranked[best].line.calmReach <= 0.0f )
			plan.note( "town of %d buildings on the river (no room along it)", src.inv.towns[cross.towns[0]].buildings );
	}

	/// carveRiver, and the crossing where the line calms for it.
	void layRiver( const Field& h, Plan& plan, Int index, const LineSpec& line, const std::vector<const InvBridge *>& allBridges, Field& out ) const
	{
		const InvWater& river = src.inv.water[index];
		std::vector<const InvBridge *> outside;
		bridgesOutside( line, allBridges, outside );
		carveRiver( h, plan, river, index, line, outside, FALSE, 0.0f, out, NULL );
		if( line.calmReach > 0.0f )
			layCrossing( out, plan, line, riverSurface( river ), river );
	}

	// A river that runs into a longer one, from a junction away from its bridges (synthmap.place_branch).
	void placeBranch( Field& h, Plan& plan, Int index ) const
	{
		const InvWater& river = src.inv.water[index];
		const RiverLine *main = NULL;
		for( size_t k = 0; k < plan.lines.size(); k++ )
			if( plan.lines[k].index == river.joins )
				main = &plan.lines[k];
		if( !main )
		{
			plan.note( "river branch of %d cells (its main river was not built)", river.area );
			return;
		}
		RiverLine mainLine = *main;
		Real want = river.startClear;
		Bool maySplit = src.inv.split;
		std::vector<const InvBridge *> bridges;
		riverBridges( index, bridges );
		const std::vector<YX>& points = mainLine.points;
		Int quarter = (Int)points.size() / 5;
		struct Ranked { Real clearness; Int k; Real angle, length; };
		std::vector<Ranked> ranked;
		Real wantLength = river.reachesEdge ? 2.0f * std::min( H, W ) : river.length * f.k;
		for( Int k = quarter; k < (Int)points.size() - quarter; k += 3 )
		{
			if( k < 1 || k + 1 >= (Int)points.size() ) continue;
			Real jy = points[k].y, jx = points[k].x;
			Real nearest = 1.0e9f;
			for( size_t s = 0; s < mainLine.sites.size(); s++ )
				nearest = std::min( nearest, hypotr( jy - mainLine.sites[s].y, jx - mainLine.sites[s].x ) );
			if( nearest < JUNCTION_CLEAR ) continue;
			Real course = ATan2( points[k + 1].y - points[k - 1].y, points[k + 1].x - points[k - 1].x );
			for( Int q = 0; q < 16; q++ )
			{
				Real angle = 2.0f * PI_R * ( q + hashUnit( seed, 85, k ) ) / 16.0f;
				Real turn = angle - course + PI_R;
				turn = turn - 2.0f * PI_R * floorf( turn / ( 2.0f * PI_R ) );
				turn = fabsf( turn - PI_R );
				if( turn < BRANCH_TURN || turn > PI_R - BRANCH_TURN ) continue;
				Real length = 0.0f;
				while( length < wantLength )
				{
					Real y = jy + ( length + 4.0f ) * Sin( angle ), x = jx + ( length + 4.0f ) * Cos( angle );
					if( y < 0.0f || x < 0.0f || (Int)y >= H || (Int)x >= W || !f.play[(Int)y * W + (Int)x] ) break;
					length += 4.0f;
				}
				if( length < 0.5f * river.length * f.k ) continue;
				Real clear = 1.0e9f;
				for( size_t s = 0; s < f.sites.size(); s++ )
				{
					Real y = f.sites[s].y, x = f.sites[s].x;
					Real along = ( y - jy ) * Sin( angle ) + ( x - jx ) * Cos( angle );
					Real d = 0.0f <= along && along <= length ? fabsf( ( y - jy ) * Cos( angle ) - ( x - jx ) * Sin( angle ) ) : hypotr( y - jy, x - jx );
					clear = std::min( clear, d );
				}
				Ranked r;
				r.clearness = -clear;
				r.k = k;
				r.angle = angle;
				r.length = length;
				ranked.push_back( r );
			}
		}
		std::stable_sort( ranked.begin(), ranked.end(), []( const Ranked& a, const Ranked& b )
		{
			if( a.clearness != b.clearness ) return a.clearness < b.clearness;
			if( a.k != b.k ) return a.k < b.k;
			return a.angle < b.angle;
		} );
		Bool have = FALSE;
		LineSpec bestLine;
		Int bestSplit = 0, bestBridge = 0, bestNear = 0, bestNotes = 0;
		Real bestClearness = 0.0f;
		Field out;
		for( Int r = 0; r < BRANCH_TRIES && r < (Int)ranked.size(); r++ )
		{
			LineSpec line;
			line.angle = ranked[r].angle;
			line.my = points[ranked[r].k].y;
			line.mx = points[ranked[r].k].x;
			line.sLo = -mainLine.half - SHORE_CELLS;
			line.sHi = ranked[r].length + 20.0f;
			line.anchored = TRUE;
			line.salt = 85;
			line.calmAt = line.calmReach = line.calmWidth = 0.0f;
			memset( line.crop, 0, sizeof(line.crop) );
			line.cropAt = CROSS_CROPS;
			Plan trial = plan;
			carveRiver( h, trial, river, index, line, bridges, TRUE, mainLine.surface, out, NULL );
			Real clear;
			Bool split;
			judge( out, trial, &clear, &split );
			/* The bridges already over the rivers have their ends raised to the bank, and a branch carved
				after them cuts through whatever it passes: an end its cut lowers or wets anywhere on the
				ground raised for it is a deck diving into the water. */
			Int sBridge = 0;
			for( size_t k = 0; k < plan.bridges.size(); k++ )
			{
				const YX ends[2] = { plan.bridges[k].a, plan.bridges[k].z };
				for( Int g = 0; g < 2; g++ )
				{
					Bool hit = FALSE;
					const Int ey = roundi( ends[g].y ), ex = roundi( ends[g].x ), reach = (Int)BRIDGE_END - 1;
					for( Int dy = -reach; dy <= reach && !hit; dy++ )
						for( Int dx = -reach; dx <= reach && !hit; dx++ )
						{
							Int y = ey + dy, x = ex + dx;
							if( dy * dy + dx * dx > reach * reach || y < 0 || y >= H || x < 0 || x >= W ) continue;
							Int i = y * W + x;
							hit = ( trial.wet[i] && !plan.wet[i] ) || out[i] < h[i] - 0.5f;
						}
					sBridge += hit;
				}
			}
			Int sSplit = split && !maySplit, sNear = clear < want, sNotes = (Int)trial.notes.size();
			Bool better = !have || sSplit < bestSplit || ( sSplit == bestSplit && ( sBridge < bestBridge || ( sBridge == bestBridge &&
				( sNear < bestNear || ( sNear == bestNear && ( sNotes < bestNotes || ( sNotes == bestNotes && ranked[r].clearness < bestClearness ) ) ) ) ) ) );
			if( better )
			{
				have = TRUE;
				bestLine = line;
				bestSplit = sSplit; bestBridge = sBridge; bestNear = sNear; bestNotes = sNotes; bestClearness = ranked[r].clearness;
			}
		}
		if( !have )
		{
			plan.note( "river branch of %d cells (no way off the main river clear of the sites)", river.area );
			return;
		}
		// a working bridge is worth more than the branch
		if( bestBridge > 0 )
		{
			plan.note( "river branch of %d cells (no way off the main river clear of its bridges)", river.area );
			return;
		}
		carveRiver( h, plan, river, index, bestLine, bridges, TRUE, mainLine.surface, out, NULL );
		h.swap( out );
	}

	// Each of the exemplar's towns copied whole to a spot as far from the middle as it was (synthmap.stamp_towns).
	void stampTowns( Field& h, Plan& plan ) const
	{
		const Int b = src.m.border;
		Mask taken( W * H );
		for( Int i = 0; i < W * H; i++ )
			taken[i] = plan.keepout[i] || plan.wet[i] || f.guarded( i ) || !f.play[i] || padsBefore[i] > 0.05f
				// nor on any copy's ground, fade and all: flattened there a town would level a pad's ramps
				|| plan.fixedWeight[i] > 0.0f;
		std::vector<Int> order;
		for( size_t t = 0; t < src.inv.towns.size(); t++ ) order.push_back( (Int)t );
		std::stable_sort( order.begin(), order.end(), [&]( Int a, Int c ) { return src.inv.towns[a].buildings > src.inv.towns[c].buildings; } );
		std::vector<Int> count;
		for( size_t n = 0; n < order.size(); n++ )
		{
			const InvTown& town = src.inv.towns[order[n]];
			if( plan.townDone[order[n]] )
				continue;
			Int fullY = std::max( (Int)town.box[1] + b - TOWN_MARGIN, 0 ), fullX = std::max( (Int)town.box[0] + b - TOWN_MARGIN, 0 );
			Int fullRows = std::min( (Int)( town.box[3] - town.box[1] ) + 2 * TOWN_MARGIN + 1, src.m.H - fullY );
			Int fullCols = std::min( (Int)( town.box[2] - town.box[0] ) + 2 * TOWN_MARGIN + 1, src.m.W - fullX );
			integral( taken, count );
			Int spotY = -1, spotX = -1, sy = fullY, sx = fullX, rows = fullRows, cols = fullCols;
			Real best = 0.0f;
			// the whole town, or where there is no room for it the part of it with the most houses, cut down twice
			static const Real theCrops[] = { 1.0f, 0.75f, 0.55f };
			for( Int crop = 0; crop < 3 && spotY < 0; crop++ )
			{
				rows = std::min( fullRows, std::max( roundi( fullRows * theCrops[crop] ), TOWN_LEAST ) );
				cols = std::min( fullCols, std::max( roundi( fullCols * theCrops[crop] ), TOWN_LEAST ) );
				Int most = -1;
				for( Int y = fullY; y <= fullY + fullRows - rows; y++ )
					for( Int x = fullX; x <= fullX + fullCols - cols; x++ )
					{
						Int houses = 0;
						for( size_t j = 0; j < town.houses.size(); j++ )
							houses += y <= town.houses[j].y && town.houses[j].y < y + rows && x <= town.houses[j].x && town.houses[j].x < x + cols;
						if( houses <= most ) continue;
						most = houses;
						sy = y;
						sx = x;
					}
				for( Int ty = 6; ty < H - rows - 6; ty += TOWN_STEP )
				{
					for( Int tx = 6; tx < W - cols - 6; tx += TOWN_STEP )
					{
						if( windowSum( count, ty - 6, tx - 6, ty + rows + 6, tx + cols + 6 ) )
							continue;
						Real share = hypotr( ty + rows / 2.0f - f.cy, tx + cols / 2.0f - f.cx ) / f.half;
						Real score = fabsf( share - town.centreShare ) + 0.05f * hashUnit( seed, 79 + (Int)n, ty * 4096 + tx );
						if( spotY >= 0 && score >= best ) continue;
						Real lo = 1.0e9f, hi = -1.0e9f;
						for( Int y = ty; y < ty + rows; y++ )
							for( Int x = tx; x < tx + cols; x++ )
							{
								lo = std::min( lo, h[y * W + x] );
								hi = std::max( hi, h[y * W + x] );
							}
						if( hi - lo <= TOWN_RELIEF )
						{
							spotY = ty;
							spotX = tx;
							best = score;
						}
					}
				}
			}
			if( spotY < 0 )
			{
				plan.note( "town of %d buildings (no clear ground %d x %d)", town.buildings, cols, rows );
				continue;
			}
			std::vector<Real> ground;
			for( Int y = spotY; y < spotY + rows; y++ )
				for( Int x = spotX; x < spotX + cols; x++ )
					ground.push_back( h[y * W + x] );
			Real base = medianOf( ground );
			for( Int y = std::max( 0, spotY - 9 ); y < std::min( H, spotY + rows + 9 ); y++ )
			{
				for( Int x = std::max( 0, spotX - 9 ); x < std::min( W, spotX + cols + 9 ); x++ )
				{
					Real inside = (Real)std::min( std::min( y - spotY, spotY + rows - 1 - y ), std::min( x - spotX, spotX + cols - 1 - x ) );
					Real w = fade( ( inside + 8.0f ) / 8.0f );
					h[y * W + x] = w * base + ( 1.0f - w ) * h[y * W + x];
				}
			}
			Stamp stamp;
			stamp.ty = spotY; stamp.tx = spotX; stamp.sy = sy; stamp.sx = sx; stamp.rows = rows; stamp.cols = cols;
			plan.stamps.push_back( stamp );
			for( Int y = spotY; y < spotY + rows; y++ )
				for( Int x = spotX; x < spotX + cols; x++ )
					plan.keepout[y * W + x] = 1;
			for( Int y = std::max( spotY - 10, 0 ); y < std::min( spotY + rows + 10, H ); y++ )
				for( Int x = std::max( spotX - 10, 0 ); x < std::min( spotX + cols + 10, W ); x++ )
					taken[y * W + x] = 1;
		}
	}

	// The exemplar's tech buildings, as far from the middle as they were, on flat dry ground (synthmap.place_tech).
	void placeTech( const Field& h, Plan& plan ) const
	{
		Mask barred, lines( W * H, 0 ), near;
		maskGrow( plan.wet, W, H, 2, barred );
		for( size_t k = 0; k < plan.bridges.size(); k++ )
			lineCells( plan.bridges[k].a, plan.bridges[k].z, W, H, lines );
		maskGrow( lines, W, H, BRIDGE_CLEAR, near );
		for( Int i = 0; i < W * H; i++ ) barred[i] = barred[i] || near[i];
		for( size_t n = 0; n < src.inv.tech.size(); n++ )
		{
			const InvTech& tech = src.inv.tech[n];
			if( tech.onMesa >= 0 && plan.hasMesa )
			{
				PlannedTech placed;
				placed.name = tech.name;
				placed.y = plan.mesaY;
				placed.x = plan.mesaX;
				plan.tech.push_back( placed );
				continue;
			}
			Real turn = hashUnit( seed, 80, (Int)n );
			Real radius = std::min( tech.centreShare, 0.8f ) * f.half;
			Bool done = FALSE;
			for( Int q = 0; q < 64 && !done; q++ )
			{
				Real angle = 2.0f * PI_R * ( q + turn ) / 64.0f;
				Int y = (Int)( f.cy + radius * Sin( angle ) ), x = (Int)( f.cx + radius * Cos( angle ) );
				if( std::min( y, x ) < TECH_FLAT || y + TECH_FLAT >= H || x + TECH_FLAT >= W ) continue;
				Bool ok = TRUE;
				Real lo = 1.0e9f, hi = -1.0e9f;
				for( Int yy = y - TECH_FLAT; yy <= y + TECH_FLAT && ok; yy++ )
					for( Int xx = x - TECH_FLAT; xx <= x + TECH_FLAT && ok; xx++ )
					{
						Int i = yy * W + xx;
						ok = f.play[i] && !plan.keepout[i] && !plan.wet[i] && !barred[i] && f.guard[i] < 0.01f;
						lo = std::min( lo, h[i] );
						hi = std::max( hi, h[i] );
					}
				if( !ok || hi - lo > 12.0f ) continue;
				PlannedTech placed;
				placed.name = tech.name;
				placed.y = y;
				placed.x = x;
				plan.tech.push_back( placed );
				for( Int yy = y - TECH_FLAT; yy <= y + TECH_FLAT; yy++ )
					for( Int xx = x - TECH_FLAT; xx <= x + TECH_FLAT; xx++ )
						plan.keepout[yy * W + xx] = 1;
				done = TRUE;
			}
			if( !done )
				plan.note( "%s (no flat ground)", tech.name.str() );
		}
	}

	// The exemplar's road network outside its towns, rebuilt between our places (synthmap.lay_roads). One
	// Dijkstra from every place at once rather than one each: the joins are the places whose regions meet,
	// which is all a minimum spanning tree of the places needs.
	void layRoads( const Field& h, Plan& plan ) const
	{
		std::vector<const InvRoad *> roads;
		Real total = 0.0f;
		for( size_t r = 0; r < src.inv.roads.size(); r++ )
			if( src.inv.roads[r].outsideLength > 0.0f )
			{
				roads.push_back( &src.inv.roads[r] );
				total += src.inv.roads[r].outsideLength;
			}
		if( roads.empty() ) return;
		Real scale = 2.0f * f.half / ( 0.5f * ( src.m.playX + src.m.playY ) );
		Real wanted = scale * total;
		if( wanted < ROAD_MIN )
		{
			plan.note( "roads outside the towns (the exemplar's are %d cells long)", (Int)wanted );
			return;
		}
		Mask cliff, walk, wetNear, open( W * H );
		Field rounded( h.size() );
		for( size_t i = 0; i < h.size(); i++ ) rounded[i] = (Real)roundi( h[i] );
		spanCliff( rounded, W, H, cliff );
		walkable( cliff, W, H, walk );
		maskGrow( plan.wet, W, H, 2, wetNear );
		for( Int i = 0; i < W * H; i++ ) open[i] = walk[i] && !wetNear[i] && f.play[i];

		std::vector<YX> nodes;
		for( size_t s = 0; s < f.starts.size(); s++ ) nodes.push_back( f.starts[s] );
		for( size_t s = 0; s < plan.stamps.size(); s++ )
			nodes.push_back( yx( (Real)( plan.stamps[s].ty + plan.stamps[s].rows / 2 ), (Real)( plan.stamps[s].tx + plan.stamps[s].cols / 2 ) ) );
		for( size_t s = 0; s < plan.tech.size(); s++ ) nodes.push_back( yx( (Real)plan.tech[s].y, (Real)plan.tech[s].x ) );
		for( size_t s = 0; s < f.docks.size(); s++ ) nodes.push_back( f.docks[s] );
		// as many ways off the map as the exemplar's roads have, evenly round the edge from a bearing the seed turns
		if( src.inv.roadExits > 0 )
		{
			Mask playNear;
			Mask notPlay( W * H );
			for( Int i = 0; i < W * H; i++ ) notPlay[i] = !f.play[i];
			maskGrow( notPlay, W, H, 1, playNear );
			std::vector<Int> rim;
			std::vector<Real> bearings;
			for( Int i = 0; i < W * H; i++ )
				if( f.play[i] && playNear[i] && open[i] )
				{
					rim.push_back( i );
					bearings.push_back( ATan2( i / W - f.cy, i % W - f.cx ) );
				}
			for( Int k = 0; k < src.inv.roadExits && !rim.empty(); k++ )
			{
				Real angle = 2.0f * PI_R * ( k + hashUnit( seed, 97, 0 ) ) / src.inv.roadExits + PI_R;
				angle = angle - 2.0f * PI_R * floorf( angle / ( 2.0f * PI_R ) ) - PI_R;
				size_t pick = 0;
				Real bestOff = 1.0e9f;
				for( size_t r = 0; r < rim.size(); r++ )
				{
					Real off = bearings[r] - angle + PI_R;
					off = fabsf( off - 2.0f * PI_R * floorf( off / ( 2.0f * PI_R ) ) - PI_R );
					if( off < bestOff ) { bestOff = off; pick = r; }
				}
				nodes.push_back( yx( (Real)( rim[pick] / W ), (Real)( rim[pick] % W ) ) );
			}
		}
		if( nodes.size() < 2 ) return;
		for( size_t k = 0; k < plan.bridges.size(); k++ )
		{
			const YX ends[2] = { plan.bridges[k].a, plan.bridges[k].z };
			for( Int e = 0; e < 2; e++ )
				for( Int y = (Int)ends[e].y - 2; y <= (Int)ends[e].y + 2; y++ )
					for( Int x = (Int)ends[e].x - 2; x <= (Int)ends[e].x + 2; x++ )
						if( y >= 0 && x >= 0 && y < H && x < W ) open[y * W + x] = 1;
		}
		for( size_t k = 0; k < nodes.size(); k++ )
			for( Int y = (Int)nodes[k].y - 2; y <= (Int)nodes[k].y + 2; y++ )
				for( Int x = (Int)nodes[k].x - 2; x <= (Int)nodes[k].x + 2; x++ )
					if( y >= 0 && x >= 0 && y < H && x < W ) open[y * W + x] = 1;

		// a cell's cost: 1, and half a unit for each byte a cell the ground climbs (np.gradient), in thousandths
		std::vector<Int> cost( W * H );
		for( Int y = 0; y < H; y++ )
		{
			for( Int x = 0; x < W; x++ )
			{
				Real gy = y == 0 ? h[W + x] - h[x] : y == H - 1 ? h[y * W + x] - h[( y - 1 ) * W + x] : 0.5f * ( h[( y + 1 ) * W + x] - h[( y - 1 ) * W + x] );
				Real gx = x == 0 ? h[y * W + 1] - h[y * W] : x == W - 1 ? h[y * W + x] - h[y * W + x - 1] : 0.5f * ( h[y * W + x + 1] - h[y * W + x - 1] );
				cost[y * W + x] = roundi( 1000.0f * ( 1.0f + ROAD_CLIMB * hypotr( gy, gx ) ) );
			}
		}
		std::vector<Int> bridgeTo( W * H, -1 );
		std::vector<Int> bridgeCost( W * H, 0 );
		for( size_t k = 0; k < plan.bridges.size(); k++ )
		{
			Int a = clampi( (Int)plan.bridges[k].a.y, 0, H - 1 ) * W + clampi( (Int)plan.bridges[k].a.x, 0, W - 1 );
			Int z = clampi( (Int)plan.bridges[k].z.y, 0, H - 1 ) * W + clampi( (Int)plan.bridges[k].z.x, 0, W - 1 );
			Int c = roundi( 1000.0f * hypotr( plan.bridges[k].z.y - plan.bridges[k].a.y, plan.bridges[k].z.x - plan.bridges[k].a.x ) );
			bridgeTo[a] = z; bridgeCost[a] = c;
			bridgeTo[z] = a; bridgeCost[z] = c;
		}

		const Int64 INF = (Int64)1 << 60;
		std::vector<Int64> dist( W * H, INF );
		std::vector<Int> owner( W * H, -1 ), previous( W * H, -1 );
		typedef std::pair<Int64, Int> Item;
		std::priority_queue< Item, std::vector<Item>, std::greater<Item> > queue;
		std::vector<Int> nodeCell;
		for( size_t k = 0; k < nodes.size(); k++ )
		{
			Int i = clampi( (Int)nodes[k].y, 0, H - 1 ) * W + clampi( (Int)nodes[k].x, 0, W - 1 );
			nodeCell.push_back( i );
			if( dist[i] == 0 ) continue;
			dist[i] = 0;
			owner[i] = (Int)k;
			queue.push( Item( 0, i ) );
		}
		static const Int theDy[8] = { 0, 0, 1, -1, 1, 1, -1, -1 }, theDx[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
		while( !queue.empty() )
		{
			Item top = queue.top();
			queue.pop();
			Int i = top.second;
			if( top.first != dist[i] ) continue;
			Int y = i / W, x = i % W;
			for( Int n = 0; n < 9; n++ )
			{
				Int j, step;
				if( n < 8 )
				{
					Int ny = y + theDy[n], nx = x + theDx[n];
					if( ny < 0 || nx < 0 || ny >= H || nx >= W ) continue;
					j = ny * W + nx;
					if( !open[i] || !open[j] ) continue;
					step = ( cost[i] + cost[j] ) / 2;
					if( n >= 4 ) step = step * 1414 / 1000;
				}
				else
				{
					j = bridgeTo[i];
					if( j < 0 ) continue;
					step = bridgeCost[i];
				}
				if( dist[i] + step < dist[j] )
				{
					dist[j] = dist[i] + step;
					owner[j] = owner[i];
					previous[j] = i;
					queue.push( Item( dist[j], j ) );
				}
			}
		}
		// where two places' regions meet, the cheapest crossing is their join
		struct Join { Int64 cost; Int a, b; Int u, v; };
		std::map< std::pair<Int, Int>, Join > meets;
		for( Int i = 0; i < W * H; i++ )
		{
			if( owner[i] < 0 ) continue;
			Int y = i / W, x = i % W;
			for( Int n = 0; n < 9; n++ )
			{
				Int j, step;
				if( n < 8 )
				{
					Int ny = y + theDy[n], nx = x + theDx[n];
					if( ny < 0 || nx < 0 || ny >= H || nx >= W ) continue;
					j = ny * W + nx;
					if( !open[i] || !open[j] ) continue;
					step = ( cost[i] + cost[j] ) / 2;
					if( n >= 4 ) step = step * 1414 / 1000;
				}
				else
				{
					j = bridgeTo[i];
					if( j < 0 ) continue;
					step = bridgeCost[i];
				}
				if( owner[j] < 0 || owner[j] <= owner[i] ) continue;
				Int64 c = dist[i] + step + dist[j];
				std::pair<Int, Int> key( owner[i], owner[j] );
				std::map< std::pair<Int, Int>, Join >::iterator it = meets.find( key );
				if( it == meets.end() || c < it->second.cost )
				{
					Join join;
					join.cost = c; join.a = owner[i]; join.b = owner[j]; join.u = i; join.v = j;
					meets[key] = join;
				}
			}
		}
		std::vector<Join> joins;
		for( std::map< std::pair<Int, Int>, Join >::iterator it = meets.begin(); it != meets.end(); ++it )
			joins.push_back( it->second );
		std::stable_sort( joins.begin(), joins.end(), []( const Join& a, const Join& b ) { return a.cost < b.cost; } );
		// Kruskal: the tree's joins first, then the rest, each cheapest first
		std::vector<Int> parent( nodes.size() );
		for( size_t k = 0; k < parent.size(); k++ ) parent[k] = (Int)k;
		auto root = [&]( Int k ) { while( parent[k] != k ) k = parent[k] = parent[parent[k]]; return k; };
		std::vector<Join> tree, others;
		for( size_t k = 0; k < joins.size(); k++ )
		{
			Int ra = root( joins[k].a ), rb = root( joins[k].b );
			if( ra != rb ) { parent[ra] = rb; tree.push_back( joins[k] ); }
			else others.push_back( joins[k] );
		}
		tree.insert( tree.end(), others.begin(), others.end() );

		// where a road stops: a pad, a town's box, a tech building's ground, a dock
		Mask stop( W * H, 0 );
		for( size_t s = 0; s < f.starts.size(); s++ ) discMask( stop, f.starts[s], PAD_INNER - 0.01f );
		for( size_t s = 0; s < plan.stamps.size(); s++ )
			for( Int y = plan.stamps[s].ty; y < plan.stamps[s].ty + plan.stamps[s].rows; y++ )
				for( Int x = plan.stamps[s].tx; x < plan.stamps[s].tx + plan.stamps[s].cols; x++ )
					stop[y * W + x] = 1;
		for( size_t s = 0; s < plan.tech.size(); s++ ) discMask( stop, yx( (Real)plan.tech[s].y, (Real)plan.tech[s].x ), TECH_FLAT + 2 - 0.01f );
		for( size_t s = 0; s < f.docks.size(); s++ ) discMask( stop, f.docks[s], SITE_INNER - 0.01f );
		Mask used( W * H, 0 ), onRoad( W * H, 0 ), junction( W * H, 0 );
		auto stepCost = [&]( Int i, Int j ) -> Int
		{
			Int step = ( cost[i] + cost[j] ) / 2;
			return i / W != j / W && i % W != j % W ? step * 1414 / 1000 : step;
		};
		auto spaceOut = [&]( Int c )
		{
			Int y = c / W, x = c % W;
			for( Int yy = std::max( y - ROAD_SPACING, 0 ); yy <= std::min( y + ROAD_SPACING, H - 1 ); yy++ )
				for( Int xx = std::max( x - ROAD_SPACING, 0 ); xx <= std::min( x + ROAD_SPACING, W - 1 ); xx++ )
					used[yy * W + xx] = 1;
		};
		// the cell of a laid road nearest c, nearest first then lowest index; -1 when the spacing there came from
		// a run that laid nothing. It looks further than the spacing: the spacing is also grown round the run,
		// and the road laid along it may have been pulled a few cells off
		auto roadNear = [&]( Int c ) -> Int
		{
			Int y = c / W, x = c % W, best = -1, bestD = ROAD_JOIN_REACH + 1;
			for( Int yy = std::max( y - ROAD_JOIN_REACH, 0 ); yy <= std::min( y + ROAD_JOIN_REACH, H - 1 ); yy++ )
				for( Int xx = std::max( x - ROAD_JOIN_REACH, 0 ); xx <= std::min( x + ROAD_JOIN_REACH, W - 1 ); xx++ )
				{
					Int d = std::max( abs( yy - y ), abs( xx - x ) );
					if( onRoad[yy * W + xx] && d < bestD ) { bestD = d; best = yy * W + xx; }
				}
			return best;
		};
		// the cells from a to z, one a step on the longer axis, rounded half away from zero
		auto walkLine = [&]( Int a, Int z, std::vector<Int>& out )
		{
			Int ay = a / W, ax = a % W, dy = z / W - ay, dx = z % W - ax;
			Int steps = std::max( abs( dy ), abs( dx ) );
			for( Int s = 1; s <= steps; s++ )
			{
				Int y = ay + ( 2 * dy * s + ( dy >= 0 ? steps : -steps ) ) / ( 2 * steps );
				Int x = ax + ( 2 * dx * s + ( dx >= 0 ? steps : -steps ) ) / ( 2 * steps );
				out.push_back( y * W + x );
			}
		};
		// Dijkstra's octilinear path pulled straight: from each kept cell to the farthest cell ahead that a
		// straight line reaches over open ground, clear of stops and other roads, for little more than the path
		// costs; a dogleg on even ground costs the same as its straight line, so it goes
		auto pull = [&]( const std::vector<Int>& piece, std::vector<Int>& out )
		{
			std::vector<Int64> along( piece.size(), 0 );
			for( size_t c = 1; c < piece.size(); c++ ) along[c] = along[c - 1] + stepCost( piece[c - 1], piece[c] );
			out.assign( 1, piece[0] );
			std::vector<Int> line;
			for( Int k = 0; k + 1 < (Int)piece.size(); )
			{
				Int next = k + 1;
				for( Int j = std::min( (Int)piece.size() - 1, k + ROAD_PULL_REACH ); j > k + 1; j-- )
				{
					line.clear();
					walkLine( piece[k], piece[j], line );
					Int64 spent = 0;
					Bool clear = TRUE;
					for( size_t c = 0; c < line.size() && clear; c++ )
					{
						spent += stepCost( c ? line[c - 1] : piece[k], line[c] );
						clear = c + 1 == line.size() || ( open[line[c]] && !stop[line[c]] && !used[line[c]] );
					}
					if( clear && spent * 1000 <= ( along[j] - along[k] ) * ROAD_PULL_SLACK ) { next = j; break; }
				}
				line.clear();
				walkLine( piece[k], piece[next], line );
				out.insert( out.end(), line.begin(), line.end() );
				k = next;
			}
		};
		std::vector< std::vector<Int> > polylines;
		Int laid = 0;
		for( size_t k = 0; k < tree.size() && laid < wanted; k++ )
		{
			// from place b to the crossing, over it, and on to place a
			std::vector<Int> run, back;
			for( Int c = tree[k].v; c >= 0; c = previous[c] ) back.push_back( c );
			for( Int c = (Int)back.size() - 1; c >= 0; c-- ) run.push_back( back[c] );
			for( Int c = tree[k].u; c >= 0; c = previous[c] ) run.push_back( c );
			// a piece that leaves or runs into an earlier road's spacing starts or ends on that road, so the two
			// share a point and the renderer joins them
			std::vector<Int> piece, pulled;
			Int head = -1;
			const size_t before = polylines.size();
			auto lay = [&]( Int tail )
			{
				if( (Int)piece.size() > ROAD_LEAST )
				{
					pull( piece, pulled );
					if( head >= 0 ) { pulled.insert( pulled.begin(), head ); junction[head] = 1; }
					if( tail >= 0 ) { pulled.push_back( tail ); junction[tail] = 1; }
					for( size_t c = 0; c < pulled.size(); c++ ) onRoad[pulled[c]] = 1;
					polylines.push_back( pulled );
				}
				piece.clear();
				head = -1;
			};
			for( size_t r = 0; r < run.size(); r++ )
			{
				Int y = run[r] / W, x = run[r] % W;
				Bool jump = r > 0 && std::max( abs( y - run[r - 1] / W ), abs( x - run[r - 1] % W ) ) > 1;
				if( stop[run[r]] || used[run[r]] || jump )
				{
					lay( !jump && !stop[run[r]] ? roadNear( run[r] ) : -1 );
					if( !stop[run[r]] && !used[run[r]] ) piece.push_back( run[r] );
					continue;
				}
				if( piece.empty() && r > 0 && used[run[r - 1]] && !stop[run[r - 1]] )
					head = roadNear( run[r - 1] );
				piece.push_back( run[r] );
			}
			lay( -1 );
			// the spacing round what was laid, as drawn, and round the rest of the run, which later runs share
			for( size_t p = before; p < polylines.size(); p++ )
				for( size_t c = 0; c < polylines[p].size(); c++ ) spaceOut( polylines[p][c] );
			for( size_t r = 0; r < run.size(); r++ )
				if( !stop[run[r]] ) spaceOut( run[r] );
			laid = 0;
			for( size_t p = 0; p < polylines.size(); p++ ) laid += (Int)polylines[p].size();
		}
		// the templates by share of length, the longest roads to the most used
		std::stable_sort( polylines.begin(), polylines.end(), []( const std::vector<Int>& a, const std::vector<Int>& b ) { return a.size() > b.size(); } );
		std::vector<Real> owed;
		for( size_t r = 0; r < roads.size(); r++ ) owed.push_back( roads[r]->outsideLength / total * laid );
		Mask mask( W * H, 0 );
		for( size_t p = 0; p < polylines.size(); p++ )
		{
			size_t pick = 0;
			for( size_t r = 1; r < owed.size(); r++ ) if( owed[r] > owed[pick] ) pick = r;
			owed[pick] -= polylines[p].size();
			std::vector<YX> cells;
			for( size_t c = 0; c < polylines[p].size(); c++ ) cells.push_back( yx( (Real)( polylines[p][c] / W ), (Real)( polylines[p][c] % W ) ) );
			// where a later road meets this one is a point of its own, simplified up to and on from, so both
			// roads hold exactly that point; a bend that close to it folds in
			std::vector<YX> simple;
			for( Int from = 0, to = 1; to < (Int)cells.size(); to++ )
			{
				if( to + 1 < (Int)cells.size() && !junction[polylines[p][to]] ) continue;
				size_t first = simple.size();
				simplify( cells, from, to, simple );
				for( size_t s = simple.size() - 1; s > first; s-- )
				{
					Real toStart = hypotr( simple[s].y - cells[from].y, simple[s].x - cells[from].x );
					Real toEnd = hypotr( simple[s].y - cells[to].y, simple[s].x - cells[to].x );
					if( toStart < ROAD_JOIN_SNAP || toEnd < ROAD_JOIN_SNAP ) simple.erase( simple.begin() + s );
				}
				from = to;
			}
			simple.push_back( cells.back() );
			PlannedRoad road;
			road.name = roads[pick]->name;
			Real step = std::max( roads[pick]->segmentLength, 1.0f );
			// as many pieces an edge as the exemplar's mean segment fits best, not the most it fits
			for( size_t s = 0; s + 1 < simple.size(); s++ )
			{
				Int pieces = std::max( roundi( hypotr( simple[s + 1].y - simple[s].y, simple[s + 1].x - simple[s].x ) / step ), 1 );
				for( Int q = 0; q < pieces; q++ )
					road.points.push_back( yx( simple[s].y + ( simple[s + 1].y - simple[s].y ) * q / pieces, simple[s].x + ( simple[s + 1].x - simple[s].x ) * q / pieces ) );
			}
			road.points.push_back( simple.back() );
			plan.roads.push_back( road );
			for( size_t c = 0; c < polylines[p].size(); c++ ) mask[polylines[p][c]] = 1;
		}
		Mask clear;
		maskGrow( mask, W, H, ROAD_CLEAR, clear );
		for( Int i = 0; i < W * H; i++ ) if( clear[i] ) plan.keepout[i] = 1;
	}

	/// Douglas and Peucker over cells[from..to], every point kept but cells[to] appended to out.
	static void simplify( const std::vector<YX>& cells, Int from, Int to, std::vector<YX>& out )
	{
		if( to - from < 2 )
		{
			for( Int k = from; k < to; k++ ) out.push_back( cells[k] );
			return;
		}
		const YX& a = cells[from];
		const YX& z = cells[to];
		Real span = hypotr( z.y - a.y, z.x - a.x );
		if( span == 0.0f ) span = 1.0f;
		Int far = from;
		Real farthest = -1.0f;
		for( Int k = from; k <= to; k++ )
		{
			Real d = fabsf( ( z.y - a.y ) * ( cells[k].x - a.x ) - ( z.x - a.x ) * ( cells[k].y - a.y ) ) / span;
			if( d > farthest ) { farthest = d; far = k; }
		}
		if( farthest <= ROAD_TOLERANCE )
		{
			out.push_back( a );
			return;
		}
		simplify( cells, from, far, out );
		simplify( cells, far, to, out );
	}

	Plan compose( Field& h )
	{
		const Inventory& inv = src.inv;
		Plan plan;
		plan.init( W * H );
		plan.townDone.assign( inv.towns.size(), 0 );
		fillLakes( h );
		Real shortest = (Real)std::min( src.m.playX, src.m.playY );
		for( size_t p = 0; p < pads.size(); p++ )
			layCopy( h, plan, pads[p] );
		joinPlateaus( h );

		if( inv.hasHigh )
			plan.note( "high ground %+d over %d%% (only the pads carry it)", (Int)inv.highDz, (Int)( 100.0f * inv.highShare ) );

		const InvCanyon *canyon = NULL;
		const InvMesa *mesa = NULL;
		std::vector<const InvCanyon *> aside;
		for( size_t k = 0; k < inv.canyons.size(); k++ )
		{
			if( !canyon && inv.canyons[k].length >= 0.5f * shortest && inv.canyons[k].centreShare < 0.3f )
				canyon = &inv.canyons[k];
			else
				aside.push_back( &inv.canyons[k] );
		}
		// the mesa nearest the middle is the island; a smaller one off it is a ledge of the same canyon
		for( size_t k = 0; k < inv.mesas.size(); k++ )
			if( inv.mesas[k].centreShare < 0.3f && ( !mesa || inv.mesas[k].centreShare < mesa->centreShare ) )
				mesa = &inv.mesas[k];
		if( canyon || mesa )
			carveMiddle( h, plan, canyon, mesa );
		for( size_t k = 0; k < aside.size(); k++ )
			carveCanyonAside( h, plan, *aside[k], (Int)k );

		// the sea first, as the rivers run into it, then the rivers longest first and their branches, then the lakes
		std::vector<UnsignedByte> built( inv.water.size(), 0 );
		for( size_t k = 0; k < inv.water.size(); k++ )
		{
			if( inv.water[k].kind == WATER_SEA || inv.water[k].kind == WATER_COAST )
			{
				carveSea( h, plan, (Int)k );
				built[k] = 1;
			}
		}
		std::vector<Int> rivers, mains;
		for( size_t k = 0; k < inv.water.size(); k++ )
			if( inv.water[k].kind == WATER_RIVER && inv.water[k].length >= RIVER_MIN * shortest )
				rivers.push_back( (Int)k );
		for( size_t k = 0; k < rivers.size(); k++ )
			if( std::find( rivers.begin(), rivers.end(), inv.water[rivers[k]].joins ) == rivers.end() )
				mains.push_back( rivers[k] );
		std::stable_sort( mains.begin(), mains.end(), [&]( Int a, Int b ) { return inv.water[a].length > inv.water[b].length; } );
		for( size_t k = 0; k < mains.size(); k++ )
			placeRiver( h, plan, mains[k], (Int)k, cross.found && k == 0 );
		for( size_t k = 0; k < rivers.size(); k++ )
			if( std::find( mains.begin(), mains.end(), rivers[k] ) == mains.end() )
				placeBranch( h, plan, rivers[k] );
		for( size_t k = 0; k < rivers.size(); k++ ) built[rivers[k]] = 1;
		for( size_t k = 0; k < inv.water.size(); k++ )
		{
			if( inv.water[k].kind == WATER_LAKE && inv.water[k].area >= LAKE_CELLS )
			{
				carveLake( h, plan, (Int)k );
				built[k] = 1;
			}
		}
		static const char *const theKinds[] = { "river", "lake", "sea", "coast" };
		for( size_t k = 0; k < inv.water.size(); k++ )
			if( !built[k] )
				plan.note( "%s of %d cells", theKinds[inv.water[k].kind], inv.water[k].area );

		stampTowns( h, plan );
		placeTech( h, plan );
		layRoads( h, plan );
		return plan;
	}
};

//-----------------------------------------------------------------------------
// What of the skeleton keeps its own height (synthmap.protection)
//-----------------------------------------------------------------------------

void protection( const SynthMap& m, const Levels& lv, Field& keep, Field& pads )
{
	const Int W = m.W, H = m.H;
	pads.assign( W * H, 0.0f );
	auto ring = [&]( Real py, Real px, Real inner, Real outer )
	{
		for( Int y = std::max( 0, (Int)( py - outer ) - 1 ); y <= std::min( H - 1, (Int)( py + outer ) + 1 ); y++ )
			for( Int x = std::max( 0, (Int)( px - outer ) - 1 ); x <= std::min( W - 1, (Int)( px + outer ) + 1 ); x++ )
				pads[y * W + x] = std::max( pads[y * W + x], clampr( ( outer - hypotr( y - py, x - px ) ) / ( outer - inner ), 0.0f, 1.0f ) );
	};
	for( size_t s = 0; s < lv.startY.size(); s++ )
		ring( (Real)lv.startY[s], (Real)lv.startX[s], PAD_INNER, PAD_OUTER );
	for( size_t s = 0; s < m.supplies.size(); s++ )
		ring( (Real)( (Int)( m.supplies[s].y + 0.5f ) + m.border ), (Real)( (Int)( m.supplies[s].x + 0.5f ) + m.border ), SITE_INNER, SITE_OUTER );
	for( size_t s = 0; s < m.derricks.size(); s++ )
		ring( (Real)( (Int)( m.derricks[s].y + 0.5f ) + m.border ), (Real)( (Int)( m.derricks[s].x + 0.5f ) + m.border ), SITE_INNER, SITE_OUTER );

	Mask cliff, walk;
	spanCliff( lv.h, W, H, cliff );
	walkable( cliff, W, H, walk );
	Mask slope( W * H, 0 );
	for( Int y = 0; y < H; y++ )
	{
		for( Int x = 0; x < W; x++ )
		{
			Int i = y * W + x;
			Real t = lv.t[i];
			if( !walk[i] || !( ( t > 0.15f && t < 0.85f ) || ( t > 1.1f && t < 1.45f ) ) ) continue;
			Real near = 1.0e9f;
			for( size_t s = 0; s < lv.startY.size(); s++ )
				near = std::min( near, hypotr( (Real)( y - lv.startY[s] ), (Real)( x - lv.startX[s] ) ) );
			slope[i] = near < SLOPE_REACH;
		}
	}
	Field slopeField, slopeMean;
	maskToField( slope, slopeField );
	boxMean( slopeField, W, H, 2, slopeMean );
	keep.resize( W * H );
	for( Int i = 0; i < W * H; i++ )
		keep[i] = std::max( pads[i], clampr( slopeMean[i] * 2.0f - 0.5f, 0.0f, 1.0f ) );
}

//-----------------------------------------------------------------------------
// Quilting
//-----------------------------------------------------------------------------

/// Column per row of the cheapest top-to-bottom path through error (rows x cols, row major).
void minCut( const std::vector<Int>& error, Int rows, Int cols, std::vector<Int>& path )
{
	std::vector<Int> total( error );
	for( Int r = 1; r < rows; r++ )
	{
		for( Int c = 0; c < cols; c++ )
		{
			Int best = total[( r - 1 ) * cols + c];
			if( c > 0 ) best = std::min( best, total[( r - 1 ) * cols + c - 1] );
			if( c + 1 < cols ) best = std::min( best, total[( r - 1 ) * cols + c + 1] );
			total[r * cols + c] += best;
		}
	}
	path.assign( rows, 0 );
	Int c = 0;
	for( Int k = 1; k < cols; k++ ) if( total[( rows - 1 ) * cols + k] < total[( rows - 1 ) * cols + c] ) c = k;
	path[rows - 1] = c;
	for( Int r = rows - 2; r >= 0; r-- )
	{
		Int from = path[r + 1];
		Int lo = std::max( from - 1, 0 ), hi = std::min( from + 2, cols );
		Int best = lo;
		for( Int k = lo + 1; k < hi; k++ ) if( total[r * cols + k] < total[r * cols + best] ) best = k;
		path[r] = best;
	}
}

void positions( Int size, std::vector<Int>& out )
{
	out.clear();
	for( Int p = 0; p < size - PATCH; p += PATCH - OVERLAP ) out.push_back( p );
	out.push_back( size - PATCH );
	std::sort( out.begin(), out.end() );
	out.erase( std::unique( out.begin(), out.end() ), out.end() );
}

/// One source cell for every target cell. The stamps go first and are never written over.
Int quilt( const Exemplar& src, const SynthMap& skm, const Levels& skel, const Field& keep, Int seed,
	const std::vector<Stamp>& stamps, const std::vector<Int>& fixedSource, const Mask& treesAllowed, Int64 treeRateQ16,
	const Mask& targetGround, std::vector<Int>& syMap, std::vector<Int>& sxMap )
{
	const Int TW = skm.W, TH = skm.H, SW = src.m.W, SH = src.m.H;
	std::vector<Int> weight( TW * TH ), targetGuide( TW * TH );
	Mask targetCliff;
	spanCliff( skel.h, TW, TH, targetCliff );
	for( Int i = 0; i < TW * TH; i++ )
	{
		weight[i] = roundi( 4.0f * ( 1.0f + 4.0f * keep[i] ) );
		targetGuide[i] = roundi( skel.guide[i] * 64.0f );
		targetCliff[i] |= targetGround[i];
	}
	// a target's ground bits against a source's, in quarters of a cliff
	Int price[16][16];
	for( Int t = 0; t < 16; t++ )
		for( Int s = 0; s < 16; s++ )
		{
			const Int differ = s ^ t;
			price[t][s] = PRICE_CLIFF * ( ( differ & GROUND_CLIFF ) != 0 ) + PRICE_WET * ( ( differ & GROUND_WET ) != 0 ) +
				PRICE_SHORE * ( ( differ & GROUND_SHORE ) != 0 ) + PRICE_STRIP * ( ( s & GROUND_STRIP ) && !( t & GROUND_STRIP ) );
		}

	// source patches touching a town or reaching past the playable ground (the border is the edge
	// cells drawn out, stripes in a quilt) are never picked
	std::vector<Int> townCount( ( SW + 1 ) * ( SH + 1 ), 0 );
	for( Int y = 0; y < SH; y++ )
		for( Int x = 0; x < SW; x++ )
			townCount[( y + 1 ) * ( SW + 1 ) + x + 1] = src.town[y * SW + x] + townCount[y * ( SW + 1 ) + x + 1] + townCount[( y + 1 ) * ( SW + 1 ) + x] - townCount[y * ( SW + 1 ) + x];
	const Int sb = src.m.border;
	auto banned = [&]( Int sy, Int sx ) -> Bool
	{
		if( sy < sb || sx < sb || sy + PATCH > sb + src.m.playY || sx + PATCH > sb + src.m.playX )
			return TRUE;
		return townCount[( sy + PATCH ) * ( SW + 1 ) + sx + PATCH] - townCount[sy * ( SW + 1 ) + sx + PATCH]
			- townCount[( sy + PATCH ) * ( SW + 1 ) + sx] + townCount[sy * ( SW + 1 ) + sx] > 0;
	};

	// the coarse pass reads every fourth cell of every fourth offset, so it reads one grid a quarter the size
	const Int ny = ( SH - PATCH ) / COARSE + 1, nx = ( SW - PATCH ) / COARSE + 1;
	const Int GH = ( SH + COARSE - 1 ) / COARSE, GW = ( SW + COARSE - 1 ) / COARSE;
	std::vector<Int> coarseGuide( GH * GW ), coarseCliff( GH * GW );
	for( Int y = 0; y < GH; y++ )
		for( Int x = 0; x < GW; x++ )
		{
			coarseGuide[y * GW + x] = src.guide64[( y * COARSE ) * SW + x * COARSE];
			coarseCliff[y * GW + x] = src.ground[( y * COARSE ) * SW + x * COARSE];
		}
	std::vector<Byte> coarseBanned( ny * nx );
	for( Int y = 0; y < ny; y++ )
		for( Int x = 0; x < nx; x++ )
			coarseBanned[y * nx + x] = banned( y * COARSE, x * COARSE );

	syMap.assign( TW * TH, 0 );
	sxMap.assign( TW * TH, 0 );
	std::vector<Int> tOut( TW * TH, 0 ), clsOut( TW * TH, 0 );
	Mask filled( TW * TH, 0 ), locked( TW * TH, 0 );
	for( size_t s = 0; s < stamps.size(); s++ )
	{
		const Stamp& st = stamps[s];
		for( Int y = 0; y < st.rows; y++ )
			for( Int x = 0; x < st.cols; x++ )
			{
				Int t = ( st.ty + y ) * TW + st.tx + x, o = ( st.sy + y ) * SW + st.sx + x;
				syMap[t] = st.sy + y;
				sxMap[t] = st.sx + x;
				tOut[t] = src.detail256[o];
				clsOut[t] = src.cls[o];
				filled[t] = locked[t] = 1;
			}
	}
	for( Int t = 0; t < TW * TH; t++ )
	{
		Int o = fixedSource[t];
		if( o < 0 ) continue;
		syMap[t] = o / SW;
		sxMap[t] = o % SW;
		tOut[t] = src.detail256[o];
		clsOut[t] = src.cls[o];
		filled[t] = locked[t] = 1;
	}
	std::vector<Int> used( SW * SH, 0 );
	std::vector<Int> s1( ny * nx ), s2( ny * nx ), sc( ny * nx );
	std::vector<Int64> coarseCost( ny * nx );
	std::vector<Int> order( ny * nx );
	std::vector<Int> xs, ys;
	positions( TW, xs );
	positions( TH, ys );
	Int patchNo = 0;
	std::vector<Int> candidates, error( PATCH * PATCH ), transposed, path;
	Mask fresh( PATCH * PATCH );
	for( size_t row = 0; row < ys.size(); row++ )
	{
		for( size_t col = 0; col < xs.size(); col++ )
		{
			const Int ty = ys[row], tx = xs[col];

			// coarse: a source patch may be lifted or lowered as a whole, at a price
			std::fill( s1.begin(), s1.end(), 0 );
			std::fill( s2.begin(), s2.end(), 0 );
			std::fill( sc.begin(), sc.end(), 0 );
			Int64 total = 0;
			for( Int a = 0; a < PATCH / COARSE; a += COARSE_SKIP )
			{
				for( Int b = 0; b < PATCH / COARSE; b += COARSE_SKIP )
				{
					Int t = ( ty + a * COARSE ) * TW + tx + b * COARSE;
					const Int g = targetGuide[t], w = weight[t];
					const Int *p = price[targetCliff[t]];
					total += w;
					for( Int cy = 0; cy < ny; cy++ )
					{
						const Int *guideRow = &coarseGuide[( cy + a ) * GW + b];
						const Int *cliffRow = &coarseCliff[( cy + a ) * GW + b];
						Int *r1 = &s1[cy * nx], *r2 = &s2[cy * nx], *rc = &sc[cy * nx];
						for( Int cx = 0; cx < nx; cx++ )
						{
							Int d = g - guideRow[cx];
							r1[cx] += w * d;
							r2[cx] += w * d * d;
							rc[cx] += w * p[cliffRow[cx]];
						}
					}
				}
			}
			for( Int k = 0; k < ny * nx; k++ )
			{
				Int64 a1 = s1[k];
				coarseCost[k] = coarseBanned[k] ? ( (Int64)1 << 62 ) :
					( (Int64)s2[k] * 1024 - a1 * a1 * 1024 / total ) / total + ( a1 * a1 * 512 / total ) / total + (Int64)2458 * 256 * sc[k] / total;
				order[k] = k;
			}
			std::partial_sort( order.begin(), order.begin() + std::min( REFINE, ny * nx ), order.end(),
				[&]( Int a, Int b ) { return coarseCost[a] != coarseCost[b] ? coarseCost[a] < coarseCost[b] : a < b; } );

			// full: every offset round the best coarse ones, every second cell, the overlap with what is placed
			candidates.clear();
			for( Int k = 0; k < std::min( REFINE, ny * nx ); k++ )
			{
				Int baseY = COARSE * ( order[k] / nx ), baseX = COARSE * ( order[k] % nx );
				for( Int dy = -COARSE / 2; dy <= COARSE / 2; dy++ )
					for( Int dx = -COARSE / 2; dx <= COARSE / 2; dx++ )
					{
						Int sy = baseY + dy, sx = baseX + dx;
						if( sy >= 0 && sx >= 0 && sy <= SH - PATCH && sx <= SW - PATCH )
							candidates.push_back( sy * SW + sx );
					}
			}
			std::sort( candidates.begin(), candidates.end() );
			candidates.erase( std::unique( candidates.begin(), candidates.end() ), candidates.end() );

			Int64 wTotal = 0, freeCells = 0, haveCells = 0;
			for( Int i = 0; i < PATCH; i += FINE_STEP )
				for( Int j = 0; j < PATCH; j += FINE_STEP )
				{
					Int t = ( ty + i ) * TW + tx + j;
					wTotal += weight[t];
					haveCells += filled[t];
					freeCells += treesAllowed[t] && !filled[t];
				}
			const Int64 expected = treeRateQ16 * freeCells;
			Int64 bestCost = 0;
			Int best = -1;
			for( size_t k = 0; k < candidates.size(); k++ )
			{
				const Int sy = candidates[k] / SW, sx = candidates[k] % SW;
				if( banned( sy, sx ) ) continue;
				Int64 a1 = 0, a2 = 0, cl = 0, over = 0, held = 0;
				for( Int i = 0; i < PATCH; i += FINE_STEP )
				{
					const Int *tg = &targetGuide[( ty + i ) * TW + tx];
					const UnsignedByte *tc = &targetCliff[( ty + i ) * TW + tx];
					const Int *tw = &weight[( ty + i ) * TW + tx];
					const Int *sg = &src.guide64[( sy + i ) * SW + sx];
					const UnsignedByte *scl = &src.ground[( sy + i ) * SW + sx];
					const UnsignedByte *have = &filled[( ty + i ) * TW + tx];
					const UnsignedByte *allowed = &treesAllowed[( ty + i ) * TW + tx];
					const Int *det = &src.detail256[( sy + i ) * SW + sx];
					const Int *out = &tOut[( ty + i ) * TW + tx];
					const Int *scls = &src.cls[( sy + i ) * SW + sx];
					const Int *ocls = &clsOut[( ty + i ) * TW + tx];
					const Int *trees = &src.trees[( sy + i ) * SW + sx];
					for( Int j = 0; j < PATCH; j += FINE_STEP )
					{
						Int d = tg[j] - sg[j];
						a1 += tw[j] * d;
						a2 += tw[j] * d * d;
						cl += tw[j] * price[tc[j]][scl[j]];
						if( have[j] )
						{
							Int e = det[j] - out[j];
							over += e * e / 16 + 614 * ( scls[j] != ocls[j] );
						}
						else if( allowed[j] )
							held += trees[j];
					}
				}
				Int64 cost = ( a2 * 1024 - a1 * a1 * 1024 / wTotal ) / wTotal + ( a1 * a1 * 512 / wTotal ) / wTotal + 2458 * 256 * cl / wTotal;
				if( haveCells )
					cost += 2 * 1024 * over / haveCells;
				Int64 off = held * 65536 - expected;
				cost += 838861 * ( off * off / std::max( expected, (Int64)65536 ) ) / 65536;
				cost += (Int64)83886 * used[sy * SW + sx];
				cost += cost * ( synthHash( seed, patchNo, candidates[k] ) >> 16 ) / 1638400;
				if( best < 0 || cost < bestCost )
				{
					best = candidates[k];
					bestCost = cost;
				}
			}
			if( best < 0 )
			{
				// every candidate touched a town: the coarse best, town or not, rather than a hole
				best = COARSE * ( order[0] / nx ) * SW + COARSE * ( order[0] % nx );
			}
			const Int sy = best / SW, sx = best % SW;
			for( Int y = std::max( sy - 4, 0 ); y < std::min( sy + 5, SH ); y++ )
				for( Int x = std::max( sx - 4, 0 ); x < std::min( sx + 5, SW ); x++ )
					used[y * SW + x]++;

			// the seam: a minimum cut through the left overlap and one through the top
			for( Int i = 0; i < PATCH; i++ )
				for( Int j = 0; j < PATCH; j++ )
				{
					Int t = ( ty + i ) * TW + tx + j, o = ( sy + i ) * SW + sx + j;
					Int e = src.detail256[o] - tOut[t];
					error[i * PATCH + j] = e * e / 16 + 614 * ( src.cls[o] != clsOut[t] );
					fresh[i * PATCH + j] = 1;
				}
			if( col > 0 )
			{
				Int width = xs[col - 1] + PATCH - tx;
				std::vector<Int> part( PATCH * width );
				for( Int i = 0; i < PATCH; i++ )
					for( Int j = 0; j < width; j++ )
						part[i * width + j] = error[i * PATCH + j];
				minCut( part, PATCH, width, path );
				for( Int i = 0; i < PATCH; i++ )
					for( Int j = 0; j < PATCH; j++ )
						if( j < path[i] ) fresh[i * PATCH + j] = 0;
			}
			if( row > 0 )
			{
				Int height = ys[row - 1] + PATCH - ty;
				transposed.assign( PATCH * height, 0 );
				for( Int i = 0; i < height; i++ )
					for( Int j = 0; j < PATCH; j++ )
						transposed[j * height + i] = error[i * PATCH + j];
				minCut( transposed, PATCH, height, path );
				for( Int i = 0; i < PATCH; i++ )
					for( Int j = 0; j < PATCH; j++ )
						if( i < path[j] ) fresh[i * PATCH + j] = 0;
			}
			for( Int i = 0; i < PATCH; i++ )
			{
				for( Int j = 0; j < PATCH; j++ )
				{
					Int t = ( ty + i ) * TW + tx + j, o = ( sy + i ) * SW + sx + j;
					Bool take = ( fresh[i * PATCH + j] || !filled[t] ) && !locked[t];
					if( !take ) continue;
					syMap[t] = sy + i;
					sxMap[t] = sx + j;
					tOut[t] = src.detail256[o];
					clsOut[t] = src.cls[o];
					filled[t] = 1;
				}
			}
			patchNo++;
		}
	}
	return patchNo;
}

//-----------------------------------------------------------------------------
// Writing the map
//-----------------------------------------------------------------------------

/// The dict every placed scenery object carries (RandomMapGenerator.cpp's writeNeutralObject).
void neutralObject( RawMap& raw, const char *name, Real cellX, Real cellY, Int border, Real angle, Int serial, std::vector<Chunk>& out )
{
	Chunk c;
	c.id = raw.idOf( "Object" );
	c.version = 3;
	std::vector<char>& b = c.body;
	putReal( b, ( cellX - border ) * MAP_XY );
	putReal( b, ( cellY - border ) * MAP_XY );
	putReal( b, 0.0f );
	putReal( b, angle );
	putInt( b, 0 );
	putAscii( b, name );
	putU16( b, 11 );
	char unique[128];
	snprintf( unique, sizeof(unique), "%s synth%d", name, serial );
	struct { const char *key; Int type; Int value; const char *text; } entries[] = {
		{ "objectInitialHealth", Dict::DICT_INT, 100, NULL },
		{ "objectEnabled", Dict::DICT_BOOL, 1, NULL },
		{ "objectIndestructible", Dict::DICT_BOOL, 0, NULL },
		{ "objectUnsellable", Dict::DICT_BOOL, 0, NULL },
		{ "objectPowered", Dict::DICT_BOOL, 1, NULL },
		{ "objectRecruitableAI", Dict::DICT_BOOL, 1, NULL },
		{ "objectName", Dict::DICT_ASCIISTRING, 0, "" },
		{ "originalOwner", Dict::DICT_ASCIISTRING, 0, "team" },
		{ "uniqueID", Dict::DICT_ASCIISTRING, 0, unique },
		{ "objectLayer", Dict::DICT_ASCIISTRING, 0, "" },
		{ "objectSelectable", Dict::DICT_BOOL, 1, NULL } };
	for( Int e = 0; e < 11; e++ )
	{
		UnsignedInt keyAndType = ( raw.idOf( entries[e].key ) << 8 ) | entries[e].type;
		putBytes( b, &keyAndType, 4 );
		if( entries[e].type == Dict::DICT_INT ) putInt( b, entries[e].value );
		else if( entries[e].type == Dict::DICT_BOOL ) putByte( b, (UnsignedByte)entries[e].value );
		else putAscii( b, entries[e].text );
	}
	out.push_back( c );
}

/// An Object from the exemplar at cell (cy, cx) of the new map's grid: neutral, nameless, its own id.
void movedObject( const SynthMap& from, Int thing, RawMap& raw, Real cy, Real cx, Int border, Int serial, std::vector<Chunk>& out )
{
	const MapThing&t = from.things[thing];
	const Chunk& chunk = from.objectChunks[t.chunk];
	const std::vector<char>& body = chunk.body;
	Int end = readAscii( body, 20, NULL );
	Chunk c;
	c.id = raw.idOf( "Object" );
	c.version = 3;
	putReal( c.body, ( cx - border ) * MAP_XY );
	putReal( c.body, ( cy - border ) * MAP_XY );
	c.body.insert( c.body.end(), body.begin() + 8, body.begin() + end );
	std::vector<DictEntry> entries;
	if( chunk.version >= 2 )
		readDict( body, end, entries );
	putU16( c.body, (UnsignedShort)entries.size() );
	char unique[128];
	snprintf( unique, sizeof(unique), "%s synth%d", t.name.str(), serial );
	for( size_t e = 0; e < entries.size(); e++ )
	{
		AsciiString key = from.raw.nameOf( entries[e].key );
		UnsignedInt keyAndType = ( raw.idOf( key.str() ) << 8 ) | entries[e].type;
		putBytes( c.body, &keyAndType, 4 );
		if( key == "uniqueID" ) putAscii( c.body, unique );
		else if( key == "originalOwner" ) putAscii( c.body, "team" );
		else if( key == "objectName" ) putAscii( c.body, "" );
		else c.body.insert( c.body.end(), body.begin() + entries[e].pos, body.begin() + entries[e].pos + entries[e].size );
	}
	out.push_back( c );
}

/* A blend's six flag bytes (horizontal, vertical, right diagonal, left diagonal, inverted, long
	diagonal) say which corners of its cell the blended texture covers, the corners numbered as the
	terrain draws them: 0 (x, y), 1 (x + 1, y), 2 (x + 1, y + 1), 3 (x, y + 1). A copy turned or
	mirrored moves the corners, and the flags are those of the corners where they land. */
enum { BLEND_HORIZONTAL, BLEND_VERTICAL, BLEND_RIGHT, BLEND_LEFT, BLEND_INVERTED, BLEND_LONG };
const char BLEND_INVERTED_BIT = 0x1;
const char BLEND_FLIPPED_BIT = 0x2;		// FLIPPED_MASK: a side blend cut the way the cell's diagonal blend cuts it

static Int blendCorners( const char flags[6] )
{
	const Bool inverted = ( flags[BLEND_INVERTED] & BLEND_INVERTED_BIT ) != 0;
	const Bool longOne = flags[BLEND_LONG] != 0;
	if( flags[BLEND_HORIZONTAL] ) return inverted ? 0x9 : 0x6;
	if( flags[BLEND_VERTICAL] ) return inverted ? 0x3 : 0xC;
	if( flags[BLEND_RIGHT] ) return inverted ? ( longOne ? 0x7 : 0x2 ) : ( longOne ? 0xE : 0x4 );
	if( flags[BLEND_LEFT] ) return inverted ? ( longOne ? 0xB : 0x1 ) : ( longOne ? 0xD : 0x8 );
	return 0;
}

static void blendFlagsFor( Int corners, char flags[6] )
{
	struct Shape { Int corners; Int kind; Bool inverted, longOne; };
	static const Shape theShapes[] = {
		{ 0x6, BLEND_HORIZONTAL, FALSE, FALSE }, { 0x9, BLEND_HORIZONTAL, TRUE, FALSE },
		{ 0xC, BLEND_VERTICAL, FALSE, FALSE }, { 0x3, BLEND_VERTICAL, TRUE, FALSE },
		{ 0x4, BLEND_RIGHT, FALSE, FALSE }, { 0x2, BLEND_RIGHT, TRUE, FALSE },
		{ 0xE, BLEND_RIGHT, FALSE, TRUE }, { 0x7, BLEND_RIGHT, TRUE, TRUE },
		{ 0x8, BLEND_LEFT, FALSE, FALSE }, { 0x1, BLEND_LEFT, TRUE, FALSE },
		{ 0xD, BLEND_LEFT, FALSE, TRUE }, { 0xB, BLEND_LEFT, TRUE, TRUE },
	};
	for( size_t k = 0; k < sizeof(theShapes) / sizeof(theShapes[0]); k++ )
	{
		if( theShapes[k].corners != corners ) continue;
		const char forcedFlip = flags[BLEND_INVERTED] & ~BLEND_INVERTED_BIT;
		memset( flags, 0, BLEND_LONG );
		flags[theShapes[k].kind] = 1;
		flags[BLEND_INVERTED] = forcedFlip | ( theShapes[k].inverted ? BLEND_INVERTED_BIT : 0 );
		flags[BLEND_LONG] = theShapes[k].longOne ? 1 : 0;
		return;
	}
}

/// The corners a blend covers after Copy::quarterTurn: mirrored across x first, then turned.
static Int turnedCorners( Int corners, Int quarterTurn )
{
	Int out = 0;
	for( Int c = 0; c < 4; c++ )
	{
		if( !( corners & ( 1 << c ) ) ) continue;
		Int to = ( quarterTurn & 4 ) ? c ^ 1 : c;
		out |= 1 << ( ( to + quarterTurn ) & 3 );
	}
	return out;
}

/// The most of these corners a blend can paint: the corners themselves when the renderer has the shape,
/// else the largest shape inside them (two opposite corners, or all four, have none).
static Int drawableCorners( Int corners )
{
	static const Int theDrawn[] = { 0x6, 0x9, 0xC, 0x3, 0x4, 0x2, 0xE, 0x7, 0x8, 0x1, 0xD, 0xB };
	Int best = 0, most = 0;
	for( size_t k = 0; k < sizeof(theDrawn) / sizeof(theDrawn[0]); k++ )
	{
		const Int c = theDrawn[k], count = ( c & 1 ) + ( ( c >> 1 ) & 1 ) + ( ( c >> 2 ) & 1 ) + ( ( c >> 3 ) & 1 );
		if( ( c & ~corners ) == 0 && count > most )
		{
			best = c;
			most = count;
		}
	}
	return best;
}

/** BlendTileData version 8: every cell's class and blends from its source cell, the tiles worked out
	again for where the cell landed, the blends turned as the copy that brought them, the exemplar's
	texture table and the cliff bits (synthmap.blend_chunk).

	A blend is drawn for the cell's neighbours, so it holds only where they are the source cell's
	neighbours. Where the quilt's seams, a pad's rim, a town stamp or a spun crossing put a cell beside
	ground from elsewhere, the copied blend pointed at a neighbour that was gone and the new one got
	none, a hard stair-stepped edge: 16% of the class edges on Heartland Shield's and Twilight Flame's
	maps against 6% and 3% on the shipped ones. So a neighbour is checked by its class against the one
	the source cell had there, turned as the copy turned, and a corner any changed neighbour touches is
	worked out again from the classes round it now, the way the exemplar lays them: a class goes over
	another where the exemplar more often puts it over than under, and by its rank where the two never
	meet. A cell whose neighbours all match keeps its blends byte for byte.

	And the texture follows the final heights: a cell they made a cliff whose source is not a cliff
	painted as one takes the class of the nearest that is, and flat ground copied from a cliff face
	takes the class of the nearest flat ground (Exemplar::steepClass, openClass). Twilight Flame's maps
	had 59% of their steep cells in dirt, sand and grass against the exemplar's 14%.

	The faces of a cliff class get their mapping from the final heights (rmgCliffMapping). The
	exemplar's own entries are no use to copy: Twilight Flame declares 7326 and no cell points at one,
	its faces being shallow enough for the plane, and the pads and canyons laid here are not. */
Bool blendChunk( const Exemplar& src, Int W, Int H, const std::vector<Int>& syMap, const std::vector<Int>& sxMap,
	const std::vector<UnsignedByte>& turns, const Mask& cliff, const std::vector<Int>& heights, std::vector<char>& out )
{
	const Int SW = src.m.W, SH = src.m.H, NC = (Int)src.classRank.size();
	// the exemplar's shapes, then each turned one the copies need, in the order the cells ask for them
	std::vector< std::pair< std::string, Int > > shapes = src.shapes;
	std::map< std::pair<Int, Int>, Int > turned;
	auto shapeTurned = [&]( Int shape, Int quarterTurn ) -> Int
	{
		if( quarterTurn == 0 ) return shape;
		std::pair<Int, Int> key( shape, quarterTurn );
		std::map< std::pair<Int, Int>, Int >::const_iterator it = turned.find( key );
		if( it != turned.end() ) return it->second;
		char flags[6];
		memcpy( flags, shapes[shape].first.data(), 6 );
		blendFlagsFor( turnedCorners( blendCorners( flags ), quarterTurn ), flags );
		std::pair< std::string, Int > wanted( std::string( flags, 6 ), shapes[shape].second );
		Int index = (Int)( std::find( shapes.begin(), shapes.end(), wanted ) - shapes.begin() );
		if( index == (Int)shapes.size() ) shapes.push_back( wanted );
		turned[key] = index;
		return index;
	};
	// and a shape worked out here, by its corners, its cut and the custom edge class of the blend it replaces
	std::map< std::pair<Int, Int>, Int > made;
	auto shapeFor = [&]( Int corners, Bool flipped, Int custom ) -> Int
	{
		std::pair<Int, Int> key( corners | ( flipped ? 16 : 0 ), custom );
		std::map< std::pair<Int, Int>, Int >::const_iterator it = made.find( key );
		if( it != made.end() ) return it->second;
		char flags[6] = { 0, 0, 0, 0, (char)( flipped ? BLEND_FLIPPED_BIT : 0 ), 0 };
		blendFlagsFor( corners, flags );
		std::pair< std::string, Int > wanted( std::string( flags, 6 ), custom );
		Int index = (Int)( std::find( shapes.begin(), shapes.end(), wanted ) - shapes.begin() );
		if( index == (Int)shapes.size() ) shapes.push_back( wanted );
		made[key] = index;
		return index;
	};
	// whether the exemplar lays class k over class a
	auto liesOver = [&]( Int k, Int a ) -> Bool
	{
		const Int up = src.blendOver[a * NC + k], down = src.blendOver[k * NC + a];
		return up != down ? up > down : src.classRank[k] > src.classRank[a];
	};

	// each cell's class: its source cell's, unless the heights made it a cliff the source is not or the other way round
	std::vector<Int> ground( W * H );
	Mask moved( W * H, 0 );
	for( Int i = 0; i < W * H; i++ )
	{
		const Int o = syMap[i] * SW + sxMap[i];
		Int c = src.cls[o];
		const Bool rock = src.steep[o] && src.cliffTexture[c];
		if( cliff[i] && !rock && src.steepClass[o] >= 0 ) c = src.steepClass[o];
		else if( !cliff[i] && rock && src.openClass[o] >= 0 ) c = src.openClass[o];
		ground[i] = c;
		moved[i] = c != src.cls[o];
	}

	// the eight neighbours, and the corners each touches (0 (x, y), 1 (x + 1, y), 2 (x + 1, y + 1), 3 (x, y + 1))
	static const Int theDY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 }, theDX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const Int theTouch[8] = { 0x1, 0x3, 0x2, 0x9, 0x6, 0x8, 0xC, 0x4 };
	std::vector<Short> layer0( W * H );
	std::vector<Int> tiles[2], shapeOf[2];
	for( Int l = 0; l < 2; l++ )
	{
		tiles[l].assign( W * H, 0 );
		shapeOf[l].assign( W * H, -1 );
	}
	for( Int y = 0; y < H; y++ )
	{
		for( Int x = 0; x < W; x++ )
		{
			const Int i = y * W + x, o = syMap[i] * SW + sxMap[i], mine = ground[i];
			layer0[i] = (Short)src.tileFor( mine, y, x );
			const Short entry[2] = { src.m.blends[o], src.m.extras[o] };

			// the corners a changed neighbour touches: every corner of a cell whose own class changed
			Int loose = moved[i] ? 0xF : 0;
			for( Int d = 0; d < 8 && loose != 0xF; d++ )
			{
				const Int ny = y + theDY[d], nx = x + theDX[d];
				if( ny < 0 || nx < 0 || ny >= H || nx >= W ) continue;
				// the neighbour's offset on the source, the copy's turn undone (Copy::toSource)
				Int sy = theDY[d], sx = theDX[d];
				for( Int q = 0; q < ( turns[i] & 3 ); q++ ) { Int t = sy; sy = -sx; sx = t; }
				if( turns[i] & 4 ) sx = -sx;
				sy += syMap[i];
				sx += sxMap[i];
				if( sy < 0 || sx < 0 || sy >= SH || sx >= SW || src.cls[sy * SW + sx] != ground[ny * W + nx] )
					loose |= theTouch[d];
			}
			if( loose == 0 )
			{
				for( Int l = 0; l < 2; l++ )
				{
					Int e = entry[l];
					if( e <= 0 || e >= (Int)src.entries.size() ) continue;
					tiles[l][i] = src.tileFor( src.blendClass[e], y, x );
					shapeOf[l][i] = shapeTurned( src.blendShape[e], turns[i] );
				}
				continue;
			}

			// the copied blends on the corners that still hold, and on the others whatever the classes round them lay over this one
			struct Layer { Int cls, corners, custom; };
			Layer layer[10];
			Int count = 0;
			auto add = [&]( Int k, Int corners, Int custom )
			{
				if( k == mine || corners == 0 ) return;
				for( Int n = 0; n < count; n++ )
					if( layer[n].cls == k ) { layer[n].corners |= corners; return; }
				layer[count].cls = k;
				layer[count].corners = corners;
				layer[count].custom = custom;
				count++;
			};
			for( Int l = 0; l < 2 && !moved[i]; l++ )
			{
				Int e = entry[l];
				if( e <= 0 || e >= (Int)src.entries.size() ) continue;
				const std::pair< std::string, Int >& shape = src.shapes[src.blendShape[e]];
				char flags[6];
				memcpy( flags, shape.first.data(), 6 );
				add( src.blendClass[e], turnedCorners( blendCorners( flags ), turns[i] ) & ~loose, shape.second );
			}
			for( Int d = 0; d < 8; d++ )
			{
				const Int ny = y + theDY[d], nx = x + theDX[d];
				if( ny < 0 || nx < 0 || ny >= H || nx >= W || !( theTouch[d] & loose ) ) continue;
				const Int k = ground[ny * W + nx];
				if( k != mine && liesOver( k, mine ) )
					add( k, theTouch[d] & loose, -1 );
			}

			// lowest first, as the renderer stacks them; it has two, and keeps the classes on top
			for( Int a = 1; a < count; a++ )
				for( Int b = a; b > 0 && src.classRank[layer[b].cls] < src.classRank[layer[b - 1].cls]; b-- )
					std::swap( layer[b], layer[b - 1] );
			const Int first = std::max( count - 2, 0 );
			Bool cutFlipped = FALSE;
			for( Int n = first; n < count; n++ )
			{
				layer[n].corners = drawableCorners( layer[n].corners );
				const Int c = layer[n].corners;
				cutFlipped = cutFlipped || c == 0x1 || c == 0x4 || c == 0xB || c == 0xE;
			}
			// a side blend takes the cut of a diagonal it shares the cell with, as RMGLayout::buildBlends does
			for( Int n = first; n < count; n++ )
			{
				const Int c = layer[n].corners;
				if( c == 0 ) continue;
				const Bool side = c == 0x3 || c == 0x6 || c == 0x9 || c == 0xC;
				const Int l = n - first;
				tiles[l][i] = src.tileFor( layer[n].cls, y, x );
				shapeOf[l][i] = shapeFor( c, side && count - first == 2 && cutFlipped, layer[n].custom );
			}
		}
	}
	const Int nShapes = std::max( (Int)shapes.size(), 1 );
	std::vector<Int> keys[2];
	for( Int l = 0; l < 2; l++ )
	{
		keys[l].assign( W * H, 0 );
		for( Int i = 0; i < W * H; i++ )
			if( shapeOf[l][i] >= 0 )
				keys[l][i] = tiles[l][i] * nShapes + shapeOf[l][i] + 1;
	}
	std::vector<Int> table( 1, 0 );
	table.insert( table.end(), keys[0].begin(), keys[0].end() );
	table.insert( table.end(), keys[1].begin(), keys[1].end() );
	std::sort( table.begin(), table.end() );
	table.erase( std::unique( table.begin(), table.end() ), table.end() );
	if( (Int)table.size() > RMG_NUM_BLEND_TILES )
		return FALSE;
	std::vector<Short> layers[2];
	for( Int l = 0; l < 2; l++ )
	{
		layers[l].resize( W * H );
		for( Int i = 0; i < W * H; i++ )
			layers[l][i] = (Short)( std::lower_bound( table.begin(), table.end(), keys[l][i] ) - table.begin() );
	}
	// the heights as the file holds them, and the cells whose class is a cliff's
	std::vector<Int> stored( W * H ), sheetFirst( W * H, -1 ), sheetWidth( W * H, 0 );
	for( Int i = 0; i < W * H; i++ )
	{
		stored[i] = (UnsignedByte)heights[i];
		if( src.cliffTexture[ground[i]] )
		{
			sheetFirst[i] = src.classFirst[ground[i]];
			sheetWidth[i] = src.classWidth[ground[i]];
		}
	}
	std::vector<Short> cliffIndex;
	std::vector<RMGCliffInfo> cliffInfo;
	rmgCliffMapping( stored, W, H, sheetFirst, sheetWidth, cliffIndex, cliffInfo );

	out.clear();
	putInt( out, W * H );
	putBytes( out, &layer0[0], W * H * 2 );
	putBytes( out, &layers[0][0], W * H * 2 );
	putBytes( out, &layers[1][0], W * H * 2 );
	putBytes( out, &cliffIndex[0], W * H * 2 );
	Int row = ( W + 7 ) / 8;
	std::vector<UnsignedByte> bits( row * H, 0 );
	for( Int y = 0; y < H; y++ )
		for( Int x = 0; x < W; x++ )
			if( cliff[y * W + x] )
				bits[y * row + ( x >> 3 )] |= (UnsignedByte)( 1 << ( x & 7 ) );
	putBytes( out, &bits[0], row * H );
	putInt( out, src.bitmapTiles );
	putInt( out, (Int)table.size() );
	putInt( out, (Int)cliffInfo.size() );
	putBytes( out, &src.tableHead[0], (Int)src.tableHead.size() );
	for( size_t k = 1; k < table.size(); k++ )
	{
		Int tile = ( table[k] - 1 ) / nShapes, shape = ( table[k] - 1 ) % nShapes;
		putInt( out, tile );
		putBytes( out, shapes[shape].first.data(), 6 );
		putInt( out, shapes[shape].second );
		putInt( out, 0x7ADA0000 );
	}
	for( size_t k = 1; k < cliffInfo.size(); k++ )
	{
		putInt( out, cliffInfo[k].m_tileIndex );
		for( Int c = 0; c < 4; c++ )
		{
			putReal( out, cliffInfo[k].m_u[c] );
			putReal( out, cliffInfo[k].m_v[c] );
		}
		putByte( out, cliffInfo[k].m_flip ? 1 : 0 );
		putByte( out, 0 );
	}
	return TRUE;
}

/// PolygonTriggers without the skeleton's water areas and with the new ones, ids none of the others has.
Bool waterTriggers( const Chunk& chunk, const std::vector<WaterPoly>& areas, Int border, std::vector<char>& out )
{
	if( chunk.version != 4 )
		return FALSE;
	const std::vector<char>& b = chunk.body;
	Int pos = 4, count = 0, largest = 0;
	std::vector<char> kept;
	for( Int i = 0; i < readInt( b, 0 ); i++ )
	{
		Int begin = pos;
		pos = readAscii( b, pos, NULL );
		pos = readAscii( b, pos, NULL );
		largest = std::max( largest, readInt( b, pos ) );
		Bool isWater = b[pos + 4] != 0;
		pos += 4 + 1 + 1 + 4;
		pos += 4 + 12 * readInt( b, pos );
		if( !isWater )
		{
			kept.insert( kept.end(), b.begin() + begin, b.begin() + pos );
			count++;
		}
	}
	out.clear();
	putInt( out, count + (Int)areas.size() );
	out.insert( out.end(), kept.begin(), kept.end() );
	for( size_t a = 0; a < areas.size(); a++ )
	{
		char name[32];
		snprintf( name, sizeof(name), "Water%d", (Int)a + 1 );
		putAscii( out, name );
		putAscii( out, "" );
		putInt( out, largest + 1 + (Int)a );
		putByte( out, 1 );
		putByte( out, 0 );
		putInt( out, 0 );
		putInt( out, (Int)areas[a].points.size() );
		Int z = (Int)( areas[a].surface * MAP_Z + 0.5f );
		for( size_t p = 0; p < areas[a].points.size(); p++ )
		{
			putInt( out, (Int)( ( areas[a].points[p].x - border ) * MAP_XY + 0.5f ) );
			putInt( out, (Int)( ( areas[a].points[p].y - border ) * MAP_XY + 0.5f ) );
			putInt( out, z );
		}
	}
	return TRUE;
}

/// A colour for a texture class by what its name says it is, for the preview.
void textureColour( const AsciiString& name, Real rgb[3] )
{
	AsciiString lower = name;
	lower.toLower();
	const char *n = lower.str();
	const UnsignedByte *c;
	static const UnsignedByte theSnow[3] = { 228, 230, 236 }, theRock[3] = { 122, 114, 104 }, theSand[3] = { 196, 176, 128 },
		theGrass[3] = { 92, 122, 62 }, theDirt[3] = { 142, 116, 80 }, thePaved[3] = { 150, 150, 146 }, theOther[3] = { 132, 118, 94 };
	if( strstr( n, "snow" ) ) c = theSnow;
	else if( strstr( n, "cliff" ) || strstr( n, "rock" ) ) c = theRock;
	else if( strstr( n, "grass" ) ) c = theGrass;
	else if( strstr( n, "sand" ) ) c = theSand;
	else if( strstr( n, "dirt" ) || strstr( n, "mud" ) ) c = theDirt;
	else if( strstr( n, "concrete" ) || strstr( n, "asphalt" ) || strstr( n, "road" ) ) c = thePaved;
	else c = theOther;
	for( Int k = 0; k < 3; k++ ) rgb[k] = c[k];
}

void writePreview( const Exemplar& src, const SynthMap& skm, const std::vector<Int>& heights, const std::vector<Int>& cls,
	const Mask& wet, const Mask& cliff, const std::vector<YX>& starts, std::vector<char>& tga )
{
	const Int pixels = RandomMapGenerator::PREVIEW_PIXELS;
	tga.clear();
	char header[18];
	memset( header, 0, sizeof(header) );
	header[2] = 2;
	header[12] = (char)( pixels & 0xff ); header[13] = (char)( ( pixels >> 8 ) & 0xff );
	header[14] = (char)( pixels & 0xff ); header[15] = (char)( ( pixels >> 8 ) & 0xff );
	header[16] = 24;
	tga.insert( tga.end(), header, header + 18 );
	std::vector< std::vector<Real> > colours;
	for( size_t c = 0; c < src.m.classes.size(); c++ )
	{
		Real rgb[3];
		textureColour( src.m.classes[c].name, rgb );
		colours.push_back( std::vector<Real>( rgb, rgb + 3 ) );
	}
	const Int W = skm.W;
	Real cellsPerPixel = (Real)skm.playable / pixels;
	for( Int row = 0; row < pixels; row++ )
	{
		Int py = pixels - 1 - row;
		for( Int column = 0; column < pixels; column++ )
		{
			Int x = std::min( (Int)( column * cellsPerPixel ) + skm.border, skm.W - 2 );
			Int y = std::min( (Int)( py * cellsPerPixel ) + skm.border, skm.H - 2 );
			Int i = y * W + x;
			Real rgb[3] = { colours[cls[i]][0], colours[cls[i]][1], colours[cls[i]][2] };
			// light from the north-west, so the relief reads
			Real slope = (Real)( heights[i] - heights[i + W + 1] );
			Real shade = clampr( 0.9f + 0.04f * slope + 0.25f * ( heights[i] - 60.0f ) / 160.0f, 0.5f, 1.35f );
			for( Int k = 0; k < 3; k++ ) rgb[k] *= shade;
			if( wet[i] ) { rgb[0] = 48.0f; rgb[1] = 86.0f; rgb[2] = 130.0f; }
			else if( cliff[i] ) for( Int k = 0; k < 3; k++ ) rgb[k] *= 0.7f;
			for( size_t s = 0; s < starts.size(); s++ )
			{
				Real dx = (Real)x - starts[s].x, dy = (Real)y - starts[s].y;
				if( dx * dx + dy * dy < 9.0f * cellsPerPixel * cellsPerPixel )
					rgb[0] = rgb[1] = rgb[2] = 240.0f;
			}
			tga.push_back( (char)(UnsignedByte)clampr( rgb[2], 0.0f, 255.0f ) );
			tga.push_back( (char)(UnsignedByte)clampr( rgb[1], 0.0f, 255.0f ) );
			tga.push_back( (char)(UnsignedByte)clampr( rgb[0], 0.0f, 255.0f ) );
		}
	}
}

/// The furthest cell out along a bearing, between two radii, where ok holds over a 5x5 that is flat.
Bool placeOnBearing( const std::vector<Int>& h, const Mask& ok, Int W, Int H, Real cy, Real cx, Real angle,
	Real inner, Real outer, Int *yOut, Int *xOut )
{
	for( Real r = outer; r > inner; r -= 1.0f )
	{
		Int y = (Int)( cy + r * Sin( angle ) + 0.5f ), x = (Int)( cx + r * Cos( angle ) + 0.5f );
		if( y < 2 || x < 2 || y + 2 >= H || x + 2 >= W ) continue;
		Bool all = TRUE;
		Int lo = 255, hi = 0;
		for( Int yy = y - 2; yy <= y + 2; yy++ )
			for( Int xx = x - 2; xx <= x + 2; xx++ )
			{
				all = all && ok[yy * W + xx];
				lo = std::min( lo, h[yy * W + xx] );
				hi = std::max( hi, h[yy * W + xx] );
			}
		if( all && hi - lo <= 4 )
		{
			*yOut = y;
			*xOut = x;
			return TRUE;
		}
	}
	return FALSE;
}

/// Flatten a disc to its median, the way a site's footprint wants it.
void levelDisc( std::vector<Int>& h, Int W, Int H, Int cy, Int cx, Int radius )
{
	std::vector<Real> values;
	for( Int y = std::max( 0, cy - radius ); y <= std::min( H - 1, cy + radius ); y++ )
		for( Int x = std::max( 0, cx - radius ); x <= std::min( W - 1, cx + radius ); x++ )
			if( ( y - cy ) * ( y - cy ) + ( x - cx ) * ( x - cx ) <= radius * radius )
				values.push_back( (Real)h[y * W + x] );
	Int level = (Int)medianOf( values );
	for( Int y = std::max( 0, cy - radius ); y <= std::min( H - 1, cy + radius ); y++ )
		for( Int x = std::max( 0, cx - radius ); x <= std::min( W - 1, cx + radius ); x++ )
			if( ( y - cy ) * ( y - cy ) + ( x - cx ) * ( x - cx ) <= radius * radius )
				h[y * W + x] = level;
}

void cliffOf( const std::vector<Int>& h, Int W, Int H, Mask& out )
{
	Field f( h.begin(), h.end() );
	spanCliff( f, W, H, out );
}

/** Each exemplar start's base as a share of every exemplar cell: the ground level with the start that
	is nearer it than any other start and no further than PAD_MOST, PAD_MARGIN past that for the cliffs
	and the feet of the ramps, fading out over COPY_FADE, never within PAD_SHORE of the water. The
	reach is the furthest cell from the start the base holds half of; the edge is the height of the
	ground the base fades into, which the copy sets on the new map's ground: on a plateau the ground
	its cliffs fall to, so the copy stands as high over the new map as the base stood over its own. */
void baseRegions( const Exemplar& src, std::vector<Field>& regions, std::vector<Real>& reaches, std::vector<Real>& edges,
	std::vector<UnsignedByte>& raised )
{
	const Int SW = src.m.W, SH = src.m.H;
	const std::vector<Int>& ys = src.lv.startY;
	const std::vector<Int>& xs = src.lv.startX;
	Mask wet, shore;
	src.m.waterGrid( src.m.water, wet );
	maskGrow( wet, SW, SH, (Int)PAD_SHORE, shore );
	const Int out = (Int)( PAD_MARGIN + COPY_FADE );
	std::vector<Int> queue, steps( SW * SH );
	std::vector<Real> ring, below;
	for( size_t t = 0; t < ys.size(); t++ )
	{
		const Real level = src.lv.h[ys[t] * SW + xs[t]];
		// the level ground first, then outward eight ways a cell a step; level ground that seldom falls
		// away at its rim is open ground or a hollow, and the base on it no more than PAD_OPEN across
		Real most = PAD_MOST;
		Bool plateau = FALSE, high = FALSE;
		for( Int attempt = 0; attempt < 2; attempt++ )
		{
			std::fill( steps.begin(), steps.end(), -1 );
			queue.clear();
			queue.push_back( ys[t] * SW + xs[t] );
			steps[queue[0]] = 0;
			Int fallingRim = 0, otherRim = 0;
			for( Int pass = 0; pass < 2; pass++ )
			{
				if( pass == 1 )
				{
					queue.clear();
					for( Int i = 0; i < SW * SH; i++ )
					{
						if( steps[i] != 0 ) continue;
						queue.push_back( i );
						const Int y = i / SW, x = i % SW;
						const Int n[4] = { x + 1 < SW ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < SH ? i + SW : -1, y > 0 ? i - SW : -1 };
						for( Int j = 0; j < 4; j++ )
						{
							if( n[j] < 0 || steps[n[j]] == 0 || shore[n[j]] ) continue;
							if( src.lv.h[n[j]] < level - PLATEAU_TOLERANCE ) fallingRim++;
							else otherRim++;
						}
					}
					plateau = most == PAD_MOST && fallingRim >= PAD_CLIFF * ( fallingRim + otherRim );
				}
				for( size_t head = 0; head < queue.size(); head++ )
				{
					Int i = queue[head], y = i / SW, x = i % SW;
					if( pass == 1 && steps[i] >= out ) continue;
					for( Int dy = -1; dy <= 1; dy++ )
						for( Int dx = -1; dx <= 1; dx++ )
						{
							Int ny = y + dy, nx = x + dx, n = ny * SW + nx;
							if( ( dx == 0 && dy == 0 ) || ny < 0 || nx < 0 || ny >= SH || nx >= SW || steps[n] >= 0 || shore[n] ) continue;
							if( pass == 0 )
							{
								if( ( dx != 0 && dy != 0 ) || fabsf( src.lv.h[n] - level ) > PLATEAU_TOLERANCE ) continue;
								Real mine = hypotr( (Real)( ny - ys[t] ), (Real)( nx - xs[t] ) );
								Bool nearest = mine <= most;
								for( size_t o = 0; o < ys.size() && nearest; o++ )
									nearest = o == t || mine <= hypotr( (Real)( ny - ys[o] ), (Real)( nx - xs[o] ) );
								if( !nearest ) continue;
								steps[n] = 0;
							}
							else
								steps[n] = steps[i] + 1;
							queue.push_back( n );
						}
				}
			}
			// a plateau stands over the ground its cliffs fall to; level ground walled in all round is a
			// hollow, and copied as open ground
			ring.clear();
			below.clear();
			for( Int i = 0; i < SW * SH; i++ )
			{
				if( steps[i] < out - 2 ) continue;
				ring.push_back( src.lv.h[i] );
				if( src.lv.h[i] < level - PLATEAU_TOLERANCE ) below.push_back( src.lv.h[i] );
			}
			high = plateau && below.size() * 4 >= ring.size();
			if( high || most == PAD_OPEN ) break;
			most = PAD_OPEN;
		}
		Field region( SW * SH, 0.0f );
		Real reach = 0.0f;
		for( Int i = 0; i < SW * SH; i++ )
		{
			if( steps[i] < 0 ) continue;
			region[i] = fade( ( out - steps[i] ) / COPY_FADE );
			if( region[i] >= 0.5f )
				reach = std::max( reach, hypotr( (Real)( i / SW - ys[t] ), (Real)( i % SW - xs[t] ) ) );
		}
		regions.push_back( region );
		reaches.push_back( reach );
		edges.push_back( high ? medianOf( below ) : medianOf( ring ) );
		raised.push_back( high );
	}
}

/** The exemplar's base round every start: one of its starts for all of them, and of the eight ways a
	square turns over the one that faces the middle as the start does, scaled down where the starts
	stand closer than the bases are wide. */
void planPads( const Exemplar& src, const Levels& skel, Int W, Int H, Int seed, const std::vector<Field>& regions,
	const std::vector<Real>& reaches, const std::vector<Real>& edges, const std::vector<UnsignedByte>& raised,
	std::vector<Copy>& pads )
{
	// a map whose bases stand on plateaus copies a plateau round every start
	const Bool anyRaised = std::find( raised.begin(), raised.end(), 1 ) != raised.end();
	// the usual base sets the scale; a larger one reaches into its neighbour's fade
	std::vector<Real> sorted( reaches );
	Real reachCells = medianOf( sorted );
	if( reachCells <= COPY_FADE )
		return;
	Real spacing = 1.0e9f;
	for( size_t i = 0; i < skel.startY.size(); i++ )
		for( size_t j = i + 1; j < skel.startY.size(); j++ )
			spacing = std::min( spacing, hypotr( (Real)( skel.startY[i] - skel.startY[j] ), (Real)( skel.startX[i] - skel.startX[j] ) ) );
	const Real scale = clampr( PAD_SPACING * spacing / reachCells, PAD_SCALE_LEAST, 1.0f );
	std::vector<Real> bearings;
	for( size_t s = 0; s < skel.startY.size(); s++ )
		bearings.push_back( ATan2( skel.startY[s] - skel.cy, skel.startX[s] - skel.cx ) );
	auto facing = [&]( const Copy& c, Int t ) -> Real
	{
		return c.angle( ATan2( src.lv.startY[t] - src.lv.cy, src.lv.startX[t] - src.lv.cx ) );
	};
	/* One base for every seat: two of Twilight Flame's stand over ground 27 bytes apart and keep their
		docks 30 and 48 cells out, and seats that took them turn about stood that far apart. The base
		whose turns face every seat best, the misses summed in whole milliradians; of those within
		PAD_BEARING_SLACK a seat of the best, the one carrying a dock nearest its start, then the lowest. */
	const Int SW = src.m.W, sb = src.m.border;
	Int64 leastCost = -1;
	std::vector<Int64> costs( src.lv.startY.size(), -1 );
	std::vector<Int> dockCells( src.lv.startY.size(), 0x7fffffff );
	for( size_t t = 0; t < src.lv.startY.size(); t++ )
	{
		if( anyRaised && !raised[t] ) continue;
		Int64 cost = 0;
		for( size_t s = 0; s < bearings.size(); s++ )
		{
			Real least = 1.0e9f;
			for( Int d = 0; d < 8; d++ )
			{
				Copy c;
				c.turn = d & 3;
				c.mirror = d >= 4;
				c.spinBy( 0.0f );
				least = std::min( least, turnBetween( facing( c, (Int)t ), bearings[s] ) );
			}
			cost += roundi( least * 1000.0f );
		}
		costs[t] = cost;
		if( leastCost < 0 || cost < leastCost ) leastCost = cost;
		for( size_t k = 0; k < src.m.supplies.size(); k++ )
		{
			const Int y = roundi( src.m.supplies[k].y + sb ), x = roundi( src.m.supplies[k].x + sb );
			if( y < 0 || x < 0 || y >= src.m.H || x >= SW || regions[t][y * SW + x] < 0.9f ) continue;
			dockCells[t] = std::min( dockCells[t], roundi( hypotr( (Real)( y - src.lv.startY[t] ), (Real)( x - src.lv.startX[t] ) ) ) );
		}
	}
	Int chosen = -1;
	const Int64 slack = (Int64)roundi( PAD_BEARING_SLACK * 1000.0f ) * (Int64)bearings.size();
	for( size_t t = 0; t < costs.size(); t++ )
	{
		if( costs[t] < 0 || costs[t] > leastCost + slack ) continue;
		if( chosen < 0 || dockCells[t] < dockCells[chosen] || ( dockCells[t] == dockCells[chosen] && costs[t] < costs[chosen] ) )
			chosen = (Int)t;
	}
	if( chosen < 0 )
		return;
	const Int t = chosen;
	for( size_t s = 0; s < bearings.size(); s++ )
	{
		Copy best;
		Real bestError = 1.0e9f;
		UnsignedInt bestTie = 0;
		for( Int d = 0; d < 8; d++ )
		{
			Copy c;
			c.turn = d & 3;
			c.mirror = d >= 4;
			c.spinBy( 0.0f );
			Real error = turnBetween( facing( c, t ), bearings[s] );
			UnsignedInt tie = synthHash( seed, 140 + (Int)s, t * 8 + d );
			if( error > bestError + 1.0e-3f || ( error > bestError - 1.0e-3f && tie >= bestTie ) )
				continue;
			best = c;
			bestError = error;
			bestTie = tie;
		}
		best.source = yx( (Real)src.lv.startY[t], (Real)src.lv.startX[t] );
		best.region = &regions[t];
		best.radius = ( reaches[t] + COPY_FADE ) * scale + 1.0f;
		best.core = ( reaches[t] - PAD_MARGIN ) * scale + 1.0f;
		best.lift = skel.bowl - edges[t];
		// a plateau scaled down keeps its shape: lower by as much as it is narrower, so its
		// ramps climb no steeper than the exemplar's
		best.pivot = edges[t];
		best.heightScale = raised[t] ? scale : 1.0f;
		best.target = yx( (Real)skel.startY[s], (Real)skel.startX[s] );
		best.scale = scale;
		best.regionW = src.m.W;
		best.regionH = src.m.H;
		memset( best.box, 0, sizeof(best.box) );
		pads.push_back( best );
	}
	/* A plateau base is copied at the exemplar's size, or as much smaller as the starts stand closer
		than the exemplar's do, so two bases sit side by side the way they do there; a way between two
		plateaus too narrow to drive is filled to their height afterwards (Composer::joinPlateaus). The
		spacing rule above is the floor. */
	for( size_t p = 0; p < pads.size(); p++ )
		pads[p].raised = anyRaised;
	if( !anyRaised )
		return;
	Real exemplarSpacing = 1.0e9f;
	for( size_t i = 0; i < src.lv.startY.size(); i++ )
		for( size_t j = i + 1; j < src.lv.startY.size(); j++ )
			if( raised[i] && raised[j] )
				exemplarSpacing = std::min( exemplarSpacing, hypotr( (Real)( src.lv.startY[i] - src.lv.startY[j] ), (Real)( src.lv.startX[i] - src.lv.startX[j] ) ) );
	const Real s = clampr( spacing / exemplarSpacing, scale, 1.0f );
	for( size_t p = 0; p < pads.size(); p++ )
	{
		const size_t t = pads[p].region - &regions[0];
		pads[p].scale = s;
		pads[p].radius = ( reaches[t] + COPY_FADE ) * s + 1.0f;
		pads[p].core = ( reaches[t] - PAD_MARGIN ) * s + 1.0f;
		pads[p].heightScale = s;
	}
}

/// 0 for a supply dock, 1 for an oil derrick, -1 for anything else: SynthMap::load's two kinds of money.
Int moneyKind( const AsciiString& name )
{
	AsciiString lower = name;
	lower.toLower();
	if( strstr( lower.str(), "supplydock" ) ) return 0;
	return lower == "techoilderrick" ? 1 : -1;
}

/** Where an object a pad carries lands (grid cells), or FALSE when it does not come. A rigid base on a
	start nearer the edge than the exemplar's put its dock past the map: Twilight Flame's 50 cells
	behind its start, a 140-cell map's start 24 from the edge. Such an object is pulled back along the
	line to the start until it stands SITE_INNER inside the playable area, and dropped when that brings
	it within PAD_SITE_LEAST of the start or onto the exemplar's cliff. carryMoney and the objects ask
	this one question, so the money the reach is repaired to and the money written out agree. */
Bool padSite( const Exemplar& src, const Copy& pad, Real sy, Real sx, const SynthMap& skm, YX& at )
{
	if( !pad.carries( sy, sx ) )
		return FALSE;
	at = pad.toTarget( sy, sx );
	const Real from[2] = { pad.target.y, pad.target.x }, to[2] = { at.y, at.x };
	const Real lo[2] = { skm.border + SITE_INNER, skm.border + SITE_INNER };
	const Real hi[2] = { skm.border + skm.playY - SITE_INNER, skm.border + skm.playX - SITE_INNER };
	Real k = 1.0f;
	for( Int a = 0; a < 2; a++ )
	{
		if( to[a] >= lo[a] && to[a] <= hi[a] ) continue;
		if( from[a] < lo[a] || from[a] > hi[a] ) return FALSE;
		k = std::min( k, ( ( to[a] < lo[a] ? lo[a] : hi[a] ) - from[a] ) / ( to[a] - from[a] ) );
	}
	if( k >= 1.0f )
		return TRUE;
	at = yx( from[0] + ( to[0] - from[0] ) * k, from[1] + ( to[1] - from[1] ) * k );
	if( hypotr( at.y - from[0], at.x - from[1] ) < PAD_SITE_LEAST )
		return FALSE;
	const YX s = pad.toSource( at.y, at.x );
	for( Int dy = -1; dy <= 1; dy++ )
		for( Int dx = -1; dx <= 1; dx++ )
			if( src.m.cliff[clampi( roundi( s.y ) + dy, 0, src.m.H - 1 ) * src.m.W + clampi( roundi( s.x ) + dx, 0, src.m.W - 1 )] )
				return FALSE;
	return TRUE;
}

/** The docks and derricks: the skeleton's off the pads, the exemplar's the pads carry. A kind some pad
	brings none of onto the map, Twilight Flame's derricks out in its bowl or an open base's docks past
	its reach, keeps the skeleton's under every pad instead, so no seat loses its home dock or its
	derricks to the copy; one pad short decides for all, so the seats stay alike. The skeleton's kept
	under a pad are listed in levelled for a flat disc on the copied ground. */
void carryMoney( const Exemplar& src, const std::vector<Copy>& pads, SynthMap& skm, Bool fellBack[2], std::vector<YX>& levelled )
{
	const Int b = skm.border, sb = src.m.border;
	std::vector<YX> *mine[2] = { &skm.supplies, &skm.derricks };
	const std::vector<YX> *theirs[2] = { &src.m.supplies, &src.m.derricks };
	for( Int kind = 0; kind < 2; kind++ )
	{
		Bool every = !pads.empty();
		YX at;
		for( size_t c = 0; c < pads.size() && every; c++ )
		{
			Bool any = FALSE;
			for( size_t k = 0; k < theirs[kind]->size() && !any; k++ )
				any = padSite( src, pads[c], ( *theirs[kind] )[k].y + sb, ( *theirs[kind] )[k].x + sb, skm, at );
			every = any;
		}
		fellBack[kind] = !every;
		std::vector<YX> kept;
		for( size_t k = 0; k < mine[kind]->size(); k++ )
		{
			const YX& p = ( *mine[kind] )[k];
			Bool covered = FALSE;
			for( size_t c = 0; c < pads.size() && !covered; c++ )
				covered = pads[c].weight( roundi( p.y + b ), roundi( p.x + b ), COPY_FADE ) > 0.0f;
			if( !covered || fellBack[kind] ) kept.push_back( p );
			if( covered && fellBack[kind] ) levelled.push_back( yx( p.y + b, p.x + b ) );
		}
		for( size_t c = 0; c < pads.size() && !fellBack[kind]; c++ )
		{
			for( size_t k = 0; k < theirs[kind]->size(); k++ )
			{
				const YX& p = ( *theirs[kind] )[k];
				if( padSite( src, pads[c], p.y + sb, p.x + sb, skm, at ) )
					kept.push_back( yx( at.y - b, at.x - b ) );
			}
		}
		mine[kind]->swap( kept );
	}
}

/** A plateau copied smaller keeps its ramps' climb but not their width, and an army queues at the
	narrow mouth; one copied at full size keeps only the ramps inside its region, where the exemplar's
	base opens onto the basin along far more. Every way down off a plateau is widened to
	WIDE_RAMP_HALF either side of its walk, each cell beside it at the height of the walk where it
	passes: the ramp's own climb, as wide as the exemplar's. The ways down are the first cells a walk from the start reaches halfway
	down, a ramp's width apart; each walk runs on from there to the ground. */
void widenRamps( std::vector<Int>& heights, Int W, Int H, Int border, const Plan& plan, const std::vector<Copy>& pads, Real ground )
{
	const Int EXIT_APART = 4 * WIDE_RAMP_HALF, MOST_EXITS = 4;
	Mask cliff, walk;
	cliffOf( heights, W, H, cliff );
	walkable( cliff, W, H, walk );
	std::vector<Int> previous( W * H ), path, seen( W * H, 0 );
	Field want( W * H ), weight( W * H );
	for( size_t p = 0; p < pads.size(); p++ )
	{
		const Int start = clampi( roundi( pads[p].target.y ), 0, H - 1 ) * W + clampi( roundi( pads[p].target.x ), 0, W - 1 );
		const Real top = (Real)heights[start];
		const Real half = 0.5f * ( top + ground ), foot = ground + 0.1f * ( top - ground );
		if( top - ground < 2.0f * PLATEAU_TOLERANCE ) continue;
		// a way off over the border leads nowhere
		auto open = [&]( Int i ) -> Bool
		{
			Int y = i / W, x = i % W;
			return y >= border && y < H - border && x >= border && x < W - border && walk[i] && !( plan.wet[i] && heights[i] < plan.level[i] );
		};
		// from the start to the cells halfway down, the plateau's ways off it
		std::fill( previous.begin(), previous.end(), -2 );
		std::vector<Int> queue( 1, start ), exits;
		previous[start] = -1;
		for( size_t head = 0; head < queue.size(); head++ )
		{
			Int i = queue[head], x = i % W, y = i / W;
			if( heights[i] < half )
			{
				Bool apart = TRUE;
				for( size_t e = 0; e < exits.size() && apart; e++ )
					apart = hypotr( (Real)( exits[e] / W - y ), (Real)( exits[e] % W - x ) ) >= EXIT_APART;
				if( apart && (Int)exits.size() < MOST_EXITS ) exits.push_back( i );
				continue;
			}
			const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
			for( Int j = 0; j < 4; j++ )
			{
				// a pad's own ways down, not a neighbour's across the high ground they share
				if( n[j] < 0 || previous[n[j]] != -2 || !open( n[j] ) ) continue;
				if( hypotr( (Real)( n[j] / W ) - pads[p].target.y, (Real)( n[j] % W ) - pads[p].target.x ) > pads[p].radius ) continue;
				previous[n[j]] = i;
				queue.push_back( n[j] );
			}
		}
		const Real out = (Real)( WIDE_RAMP_HALF + WIDE_RAMP_BLEND );
		std::fill( weight.begin(), weight.end(), 0.0f );
		// a straight ramp from the plateau's rim to the foot, its climb no steeper than WIDE_RAMP_STEEP
		auto layRamp = [&]( Int rim, Int base )
		{
			const Real high = (Real)heights[rim], low = (Real)heights[base];
			const Real cy = 0.5f * ( rim / W + base / W ), cx = 0.5f * ( rim % W + base % W );
			const Real span = hypotr( (Real)( base / W - rim / W ), (Real)( base % W - rim % W ) );
			if( span < 1.0f ) return;
			const Real uy = ( base / W - rim / W ) / span, ux = ( base % W - rim % W ) / span;
			const Real reach = 0.5f * std::max( span, ( high - low ) / WIDE_RAMP_STEEP );
			const Int box = (Int)( reach + out ) + 1;
			for( Int y = std::max( (Int)cy - box, 0 ); y <= std::min( (Int)cy + box, H - 1 ); y++ )
				for( Int x = std::max( (Int)cx - box, 0 ); x <= std::min( (Int)cx + box, W - 1 ); x++ )
				{
					Real s = ( y - cy ) * uy + ( x - cx ) * ux, t = fabsf( ( x - cx ) * uy - ( y - cy ) * ux );
					Real ends = reach + WIDE_RAMP_BLEND - fabsf( s );
					if( t >= out || ends <= 0.0f ) continue;
					Int i = y * W + x;
					Real w = fade( ( out - t ) / WIDE_RAMP_BLEND ) * fade( ends / WIDE_RAMP_BLEND );
					if( w <= weight[i] ) continue;
					weight[i] = w;
					want[i] = high + ( low - high ) * clampr( ( s + reach ) / ( 2.0f * reach ), 0.0f, 1.0f );
				}
		};
		/* The exemplar's bases open onto the middle along a hundred cells; a copied plateau keeps only the
			ramps inside its region. One more runs straight at the middle of the map unless a way down
			already leaves the plateau on that side. */
		{
			const Real my = 0.5f * ( H - 1 ), mx = 0.5f * ( W - 1 );
			const Real toward = hypotr( my - start / W, mx - start % W );
			if( toward >= 1.0f )
			{
				const Real uy = ( my - start / W ) / toward, ux = ( mx - start % W ) / toward;
				Int rim = start, base = -1;
				for( Real d = 1.0f; d < toward && base < 0; d += 1.0f )
				{
					Int i = clampi( roundi( start / W + uy * d ), 0, H - 1 ) * W + clampi( roundi( start % W + ux * d ), 0, W - 1 );
					if( (Real)heights[i] > top - PLATEAU_TOLERANCE ) rim = i;
					else if( (Real)heights[i] <= foot ) base = i;
				}
				Bool covered = FALSE;
				for( size_t e = 0; e < exits.size() && !covered; e++ )
					covered = hypotr( (Real)( exits[e] / W - rim / W ), (Real)( exits[e] % W - rim % W ) ) < EXIT_APART;
				if( base >= 0 && !covered && open( base ) )
					layRamp( rim, base );
			}
		}
		for( size_t e = 0; e < exits.size(); e++ )
		{
			// up to the start, then on down to the foot
			path.clear();
			for( Int i = exits[e]; i >= 0; i = previous[i] ) path.push_back( i );
			std::reverse( path.begin(), path.end() );
			std::vector<Int> down( 1, exits[e] ), from( 1, -1 );
			const Int mark = (Int)( p * MOST_EXITS + e ) + 1;
			seen[exits[e]] = mark;
			Int end = -1;
			for( size_t head = 0; head < down.size() && end < 0; head++ )
			{
				Int i = down[head], x = i % W, y = i / W;
				if( heights[i] <= foot ) { end = (Int)head; break; }
				const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
				for( Int j = 0; j < 4; j++ )
				{
					if( n[j] < 0 || seen[n[j]] == mark || heights[n[j]] >= half || !open( n[j] ) ) continue;
					seen[n[j]] = mark;
					down.push_back( n[j] );
					from.push_back( (Int)head );
				}
			}
			size_t upper = path.size();
			for( Int k = end; k > 0; k = from[k] ) path.push_back( down[k] );
			std::reverse( path.begin() + upper, path.end() );
			Int lo = 0;
			while( lo + 1 < (Int)path.size() && heights[path[lo + 1]] > top - PLATEAU_TOLERANCE ) lo++;
			layRamp( path[lo], path.back() );
		}
		for( Int i = 0; i < W * H; i++ )
		{
			if( weight[i] <= 0.0f || ( plan.wet[i] && heights[i] < plan.level[i] ) ) continue;
			heights[i] = clampi( roundi( weight[i] * want[i] + ( 1.0f - weight[i] ) * heights[i] ), 0, 255 );
		}
	}
}

/** Where a start, dock, derrick or tech building is cut off from the first start, a ramp along the
	cheapest way to the ground that start reaches: through a cliff at a price, never through water nor
	the border, which the pathfinder does not walk. Returns the ramps cut. */
Int repairReach( std::vector<Int>& heights, Int W, Int H, const Plan& plan, const Mask& decks, const Mask& play, Int firstStart,
	const std::vector<YX>& targets )
{
	const Int RAMP_HALF = 4, RAMP_BLEND = 3, CLIFF_COST = 6;
	Int cut = 0;
	Mask cliff, walk, passable( W * H ), wet( W * H ), reached;
	std::vector<Int> cost( W * H ), previous( W * H );
	Bool stale = TRUE;
	for( Int round = 0; round < REPAIR_ROUNDS; round++ )
	{
		Bool fixedAny = FALSE;
		for( size_t k = 0; k < targets.size(); k++ )
		{
			if( stale )
			{
				cliffOf( heights, W, H, cliff );
				walkable( cliff, W, H, walk );
				for( Int i = 0; i < W * H; i++ )
				{
					wet[i] = plan.wet[i] && heights[i] < plan.level[i] && !decks[i];
					passable[i] = ( ( walk[i] && !wet[i] ) || decks[i] ) && play[i];
				}
				reach( passable, W, H, firstStart, reached );
				stale = FALSE;
			}
			Int ty = clampi( (Int)targets[k].y, 0, H - 1 ), tx = clampi( (Int)targets[k].x, 0, W - 1 );
			Bool near = FALSE;
			for( Int y = std::max( ty - 2, 0 ); y <= std::min( ty + 2, H - 1 ) && !near; y++ )
				for( Int x = std::max( tx - 2, 0 ); x <= std::min( tx + 2, W - 1 ) && !near; x++ )
					near = reached[y * W + x] != 0;
			if( near ) continue;

			// Dijkstra from the target to the nearest reached cell
			std::fill( cost.begin(), cost.end(), 0x7fffffff );
			std::priority_queue< std::pair<Int, Int>, std::vector< std::pair<Int, Int> >, std::greater< std::pair<Int, Int> > > open;
			Int from = ty * W + tx, end = -1;
			cost[from] = 0;
			previous[from] = -1;
			open.push( std::make_pair( 0, from ) );
			while( !open.empty() && end < 0 )
			{
				std::pair<Int, Int> top = open.top();
				open.pop();
				Int i = top.second;
				if( top.first > cost[i] ) continue;
				if( reached[i] ) { end = i; break; }
				Int x = i % W, y = i / W;
				const Int n[4] = { x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1 };
				for( Int j = 0; j < 4; j++ )
				{
					if( n[j] < 0 || wet[n[j]] || !play[n[j]] ) continue;
					Int step = top.first + ( passable[n[j]] ? 1 : CLIFF_COST );
					if( step >= cost[n[j]] ) continue;
					cost[n[j]] = step;
					previous[n[j]] = i;
					open.push( std::make_pair( step, n[j] ) );
				}
			}
			if( end < 0 ) continue;
			std::vector<Int> path;
			for( Int i = end; i >= 0; i = previous[i] ) path.push_back( i );
			Int first = -1, last = -1;
			for( Int p = 0; p < (Int)path.size(); p++ )
				if( !passable[path[p]] ) { if( first < 0 ) first = p; last = p; }
			if( first < 0 ) continue;
			// long enough that the climb stays under RAMP_STEEP a cell
			Int lo = first, hi = last;
			while( ( lo > 0 || hi + 1 < (Int)path.size() ) &&
				( hi - lo ) * RAMP_STEEP < abs( heights[path[hi]] - heights[path[lo]] ) + RAMP_STEEP )
			{
				if( lo > 0 ) lo--;
				if( hi + 1 < (Int)path.size() ) hi++;
			}
			Real low = (Real)heights[path[lo]], high = (Real)heights[path[hi]];
			Field want( W * H, -1.0f ), weight( W * H, 0.0f );
			for( Int p = lo; p <= hi; p++ )
			{
				Real level = low + ( high - low ) * ( p - lo ) / std::max( hi - lo, 1 );
				Int py = path[p] / W, px = path[p] % W;
				for( Int y = std::max( py - RAMP_HALF - RAMP_BLEND, 0 ); y <= std::min( py + RAMP_HALF + RAMP_BLEND, H - 1 ); y++ )
					for( Int x = std::max( px - RAMP_HALF - RAMP_BLEND, 0 ); x <= std::min( px + RAMP_HALF + RAMP_BLEND, W - 1 ); x++ )
					{
						Real w = fade( ( RAMP_HALF + RAMP_BLEND - hypotr( (Real)( y - py ), (Real)( x - px ) ) ) / RAMP_BLEND );
						Int i = y * W + x;
						if( w > weight[i] ) { weight[i] = w; want[i] = level; }
					}
			}
			for( Int i = 0; i < W * H; i++ )
				if( weight[i] > 0.0f )
					heights[i] = clampi( roundi( weight[i] * want[i] + ( 1.0f - weight[i] ) * heights[i] ), 0, 255 );
			cut++;
			fixedAny = TRUE;
			stale = TRUE;
		}
		if( !fixedAny ) break;
	}
	return cut;
}

}	// namespace

/** How much of the way up to the lower plateau a cell between two copied plateau bases is raised
	(Composer::joinPlateaus). sum is the cell's two 3-4 chamfer distances added, pairLeast the least
	such sum over the map: three times the real gap between the two bases, along the line between
	them. Whether the pair is joined at all is decided on that gap, PAD_CORRIDOR; the fade over
	JOIN_FADE only shapes the joined way's shoulders. Fading on the sum alone also filled a valley a
	few cells wider than PAD_CORRIDOR part way across, half its height at a gap of 36 cells. */
Real rmgJoinShare( Int pairLeast, Int sum )
{
	const Int inside = 3 * PAD_CORRIDOR, reachSum = 3 * ( PAD_CORRIDOR + JOIN_FADE );
	if( pairLeast >= inside || sum >= reachSum )
		return 0.0f;
	return sum < inside ? 1.0f : fade( (Real)( reachSum - sum ) / ( 3 * JOIN_FADE ) );
}

//-----------------------------------------------------------------------------
// The synthesis
//-----------------------------------------------------------------------------

/** The exemplar, unpacked the way the map loader unpacks it. */
static Bool unpackExemplar( const std::vector<char>& exemplarBytes, std::vector<char>& raw )
{
	if( !CompressionManager::isDataCompressed( &exemplarBytes[0], (Int)exemplarBytes.size() ) )
	{
		raw = exemplarBytes;
		return TRUE;
	}
	Int size = CompressionManager::getUncompressedSize( &exemplarBytes[0], (Int)exemplarBytes.size() );
	raw.resize( size );
	std::vector<char> packed( exemplarBytes );
	return CompressionManager::decompressData( &packed[0], (Int)packed.size(), &raw[0], size ) == size;
}

Bool rmgSynthesise( const RandomMapSettings& clamped, const std::vector<char>& exemplarBytes,
	RMGCivilianTest civilian, std::vector<char>& mapBytes, std::vector<char> *previewBytes,
	RMGTemplateReport *report )
{
	mapBytes.clear();
	UnsignedInt callersFPMode = getFPMode();
	setFPMode();

	std::vector<char> raw;
	if( !unpackExemplar( exemplarBytes, raw ) )
	{
		restoreFPMode( callersFPMode );
		return FALSE;
	}

	// the template is a file off the disk: bytes that end early refuse it rather than read past it
	Exemplar src;
	Bool loaded = FALSE;
	try
	{
		loaded = src.load( raw, civilian );
	}
	catch( const ShortMap& fault )
	{
		DEBUG_LOG(( "random map: the template '%s' ends at byte %d of a chunk, %d wanted at %d\n",
			clamped.m_template.str(), fault.size, fault.count, fault.pos ));
	}
	if( !loaded )
	{
		DEBUG_LOG(( "random map: the template '%s' is not a skirmish map this reader knows\n", clamped.m_template.str() ));
		restoreFPMode( callersFPMode );
		return FALSE;
	}

	// the skeleton: the generator's open layout for these players, its seats, money and the AI's ways
	std::vector<char> skeletonBytes;
	rmgBuildSkeleton( clamped, skeletonBytes );
	SynthMap skm;
	if( !skm.load( skeletonBytes ) )
	{
		restoreFPMode( callersFPMode );
		return FALSE;
	}
	Levels skel;
	skel.init( skm );
	const Int W = skm.W, H = skm.H, b = skm.border;
	const Int seed = clamped.m_seed;

	// the exemplar's base round every start, with its docks and derricks in place of the skeleton's
	std::vector<Field> regions;
	std::vector<Real> reaches, edges;
	std::vector<UnsignedByte> raised;
	baseRegions( src, regions, reaches, edges, raised );
	std::vector<Copy> basePads;
	planPads( src, skel, W, H, seed, regions, reaches, edges, raised, basePads );
	Bool moneyFellBack[2];
	std::vector<YX> levelledMoney;
	carryMoney( src, basePads, skm, moneyFellBack, levelledMoney );

	// the exemplar's large features first, then what of the skeleton keeps its own height
	Composer composer( src, skm, skel, seed, basePads );
	{
		Field keepBefore;
		protection( skm, skel, keepBefore, composer.padsBefore );
	}
	Field h = skel.h;
	Plan plan = composer.compose( h );
	skel.setHeights( h, W, H, TRUE );
	const Frame& f = composer.f;

	{
		Mask lines( W * H, 0 ), near;
		for( size_t k = 0; k < plan.bridges.size(); k++ )
			lineCells( plan.bridges[k].a, plan.bridges[k].z, W, H, lines );
		maskGrow( lines, W, H, BRIDGE_CLEAR, near );
		for( Int i = 0; i < W * H; i++ ) if( near[i] ) plan.keepout[i] = 1;
	}
	Field keep, pads;
	protection( skm, skel, keep, pads );
	Mask cliffSkel, walkSkel, wetNear, gentle( W * H );
	spanCliff( skel.h, W, H, cliffSkel );
	walkable( cliffSkel, W, H, walkSkel );
	for( Int i = 0; i < W * H; i++ ) gentle[i] = plan.gentle[i] && walkSkel[i];
	{
		Field gentleField, gentleMean;
		maskToField( gentle, gentleField );
		boxMean( gentleField, W, H, 2, gentleMean );
		maskGrow( plan.wet, W, H, 2, wetNear );
		for( Int i = 0; i < W * H; i++ )
		{
			keep[i] = std::max( keep[i], clampr( gentleMean[i] * 2.0f - 0.5f, 0.0f, 1.0f ) );
			keep[i] = std::max( keep[i], plan.fixedWeight[i] );
			if( wetNear[i] ) keep[i] = 1.0f;
		}
	}
	// no tree on the AI's ways about the map
	Mask paths( W * H, 0 ), pathsNear;
	{
		std::map<Int, YX> where;
		for( size_t k = 0; k < skm.things.size(); k++ )
			if( skm.things[k].waypointID )
				where[skm.things[k].waypointID] = yx( skm.things[k].y + b, skm.things[k].x + b );
		const Chunk *links = NULL;
		for( size_t c = 0; c < skm.raw.chunks.size(); c++ )
			if( skm.raw.nameOf( skm.raw.chunks[c].id ) == "WaypointsList" )
				links = &skm.raw.chunks[c];
		if( links )
		{
			Int count = readInt( links->body, 0 );
			for( Int k = 0; k < count; k++ )
			{
				Int a = readInt( links->body, 4 + 8 * k ), z = readInt( links->body, 8 + 8 * k );
				if( where.count( a ) && where.count( z ) )
					lineCells( where[a], where[z], W, H, paths );
			}
		}
		maskGrow( paths, W, H, PATH_CELLS, pathsNear );
	}
	Mask treesAllowed( W * H );
	Int allowedCount = 0, treeGround = 0;
	for( Int i = 0; i < W * H; i++ )
	{
		treesAllowed[i] = f.play[i] && pads[i] < 0.9f && keep[i] < 0.5f && !pathsNear[i] && !plan.keepout[i] && !wetNear[i];
		allowedCount += treesAllowed[i];
		treeGround += f.play[i] && pads[i] < 0.9f && !plan.wet[i];
	}
	Real inTowns = 0.0f;
	for( size_t s = 0; s < plan.stamps.size(); s++ )
		for( Int y = plan.stamps[s].sy; y < plan.stamps[s].sy + plan.stamps[s].rows; y++ )
			for( Int x = plan.stamps[s].sx; x < plan.stamps[s].sx + plan.stamps[s].cols; x++ )
				inTowns += src.trees[y * src.m.W + x];
	Real treesWanted = std::max( src.treeDensity * treeGround - inTowns, 0.0f );
	Int64 treeRateQ16 = allowedCount ? (Int64)( treesWanted * 65536.0f / allowedCount ) : 0;

	// the ground the quilt matches besides height: the water that will be water (plan.wet holds dry ground
	// a pad's guard kept, so it is the composed height under the plane), its bank, and the roads' clearance.
	// An exemplar with no water has no bed or bank to match, and a price every patch pays alike only moves the jitter.
	Mask targetGround( W * H, 0 );
	{
		Mask wetSkel( W * H, 0 ), roads( W * H, 0 ), shore, clear;
		if( src.hasWater )
			for( Int i = 0; i < W * H; i++ ) wetSkel[i] = plan.wet[i] && skel.h[i] < plan.level[i];
		maskGrow( wetSkel, W, H, SHORE_GROUND, shore );
		for( size_t k = 0; k < plan.roads.size(); k++ )
			for( size_t p = 0; p + 1 < plan.roads[k].points.size(); p++ )
				lineCells( plan.roads[k].points[p], plan.roads[k].points[p + 1], W, H, roads );
		maskGrow( roads, W, H, ROAD_CLEAR, clear );
		for( Int i = 0; i < W * H; i++ )
			targetGround[i] = ( wetSkel[i] ? GROUND_WET : 0 ) | ( shore[i] ? GROUND_SHORE : 0 ) | ( clear[i] ? GROUND_STRIP : 0 );
	}
	std::vector<Int> syMap, sxMap;
	Int patches = quilt( src, skm, skel, keep, seed, plan.stamps, plan.fixedSource, treesAllowed, treeRateQ16, targetGround, syMap, sxMap );
	Mask stamped( W * H, 0 );
	for( size_t s = 0; s < plan.stamps.size(); s++ )
		for( Int y = plan.stamps[s].ty; y < plan.stamps[s].ty + plan.stamps[s].rows; y++ )
			for( Int x = plan.stamps[s].tx; x < plan.stamps[s].tx + plan.stamps[s].cols; x++ )
				stamped[y * W + x] = 1;
	for( Int i = 0; i < W * H; i++ )
		if( plan.fixedSource[i] >= 0 ) stamped[i] = 1;

	// heights: the skeleton's, with the detail on top, seams smoothed; none at a skeleton cliff or on kept ground
	const Int SW = src.m.W;
	Field detail( W * H ), detailSmooth;
	for( Int i = 0; i < W * H; i++ ) detail[i] = src.detail[syMap[i] * SW + sxMap[i]];
	boxMean( detail, W, H, 1, detailSmooth );
	Mask seam( W * H, 0 ), seamNear;
	for( Int y = 0; y < H; y++ )
		for( Int x = 0; x < W; x++ )
		{
			Int i = y * W + x;
			Int dy = syMap[i] - y, dx = sxMap[i] - x;
			if( x + 1 < W && ( syMap[i + 1] - y != dy || sxMap[i + 1] - ( x + 1 ) != dx ) ) seam[i] = 1;
			if( y + 1 < H && ( syMap[i + W] - ( y + 1 ) != dy || sxMap[i + W] - x != dx ) ) seam[i] = 1;
		}
	maskGrow( seam, W, H, SEAM_BAND, seamNear );
	Field cliffField, rim;
	maskToField( cliffSkel, cliffField );
	boxMean( cliffField, W, H, RIM_CELLS, rim );
	std::vector<Int> heights( W * H );
	for( Int i = 0; i < W * H; i++ )
	{
		Real d = seamNear[i] && !stamped[i] ? detailSmooth[i] : detail[i];
		Real t = skel.t[i] + ( 1.0f - clampr( rim[i] * 4.0f, 0.0f, 1.0f ) ) * d;
		Real value = skel.bowl + t * skel.scale;
		value = keep[i] * skel.h[i] + ( 1.0f - keep[i] ) * value;
		// ground under a sea's plane but not the sea's would be a pond
		if( plan.level[i] >= 0.0f && !plan.wet[i] )
			value = std::max( value, plan.level[i] + 1.0f );
		heights[i] = clampi( roundi( value ), 0, 255 );
	}
	widenRamps( heights, W, H, b, plan, basePads, skel.bowl );
	// the skeleton's money kept under a pad stands on the copy's ground, levelled as the skeleton levelled it
	for( size_t k = 0; k < levelledMoney.size(); k++ )
		levelDisc( heights, W, H, roundi( levelledMoney[k].y ), roundi( levelledMoney[k].x ), PILE_FLAT );
	Mask cliff;
	cliffOf( heights, W, H, cliff );

	// the skeleton's money and seats stay, its dressing goes, and on a copy all but its waypoints and the
	// money a pad fell back on (carryMoney); the piles move
	auto underCopy = [&]( Real gy, Real gx, Bool crossingsOnly ) -> Bool
	{
		for( size_t c = 0; c < plan.copies.size(); c++ )
			if( ( !crossingsOnly || plan.copies[c].radius <= 0.0f ) && plan.copies[c].weight( roundi( gy ), roundi( gx ), COPY_FADE ) > 0.0f )
				return TRUE;
		return FALSE;
	};
	std::vector<Chunk> kept, piles;
	for( size_t k = 0; k < skm.things.size(); k++ )
	{
		const MapThing&t = skm.things[k];
		const Chunk& chunk = skm.objectChunks[t.chunk];
		if( t.name.startsWith( "Tree" ) || t.name.startsWith( "Rocks" ) || t.name.startsWith( "RockCluster" ) ||
				t.name.startsWith( "AmbientWaves" ) || ( t.flags & FLAG_ROAD_FLAGS ) || ( civilian && !t.waypointID && civilian( t.name.str() ) ) )
			continue;
		const Int kind = moneyKind( t.name );
		if( !t.waypointID && underCopy( t.y + b, t.x + b, kind >= 0 && moneyFellBack[kind] ) )
			continue;
		( t.name == "SupplyPileSmall" ? piles : kept ).push_back( chunk );
	}
	// the objects that came out of the skeleton, for the snapping below: a waypoint or a pile by its template
	Mask levelWet( W * H ), decks( W * H, 0 ), lines( W * H, 0 );
	for( Int i = 0; i < W * H; i++ ) levelWet[i] = plan.wet[i] && heights[i] < plan.level[i];
	for( size_t k = 0; k < plan.bridges.size(); k++ )
		lineCells( plan.bridges[k].a, plan.bridges[k].z, W, H, lines );
	maskGrow( lines, W, H, 1, decks );
	Mask walk, passable( W * H ), reached, openGround( W * H );
	walkable( cliff, W, H, walk );
	// the border is no ground: the pathfinder keeps to the playable area
	for( Int i = 0; i < W * H; i++ ) passable[i] = ( ( walk[i] && !levelWet[i] ) || decks[i] ) && f.play[i];
	const Int firstStart = (Int)f.starts[0].y * W + (Int)f.starts[0].x;
	reach( passable, W, H, firstStart, reached );
	for( Int i = 0; i < W * H; i++ ) openGround[i] = passable[i] && reached[i] && !plan.keepout[i] && !levelWet[i];

	// the piles: where the exemplar keeps its own on high ground, a pair halfway to the next seat and a
	// pair a third of the way, with what stands beside the exemplar's; otherwise where the skeleton put them
	std::vector<YX> pileSites;
	Bool everyGap = TRUE;
	struct Added { AsciiString name; Real x, y, angle; };
	std::vector<Added> added;
	if( src.inv.pileCount && src.inv.pileDz >= HIGH_RISE )
	{
		std::vector<Int> seats;
		for( size_t s = 0; s < f.starts.size(); s++ ) seats.push_back( (Int)s );
		std::vector<Real> bearings;
		for( size_t s = 0; s < f.starts.size(); s++ ) bearings.push_back( ATan2( f.starts[s].y - skel.cy, f.starts[s].x - skel.cx ) );
		std::stable_sort( seats.begin(), seats.end(), [&]( Int a, Int c ) { return bearings[a] < bearings[c]; } );
		Real outer = skm.playable / 2.0f - 12.0f;
		Mask high( W * H );
		for( Int i = 0; i < W * H; i++ ) high[i] = openGround[i] && heights[i] > skel.plateau + 0.25f * skel.scale;
		static const Real theNudges[] = { 0.0f, 0.05f, -0.05f, 0.1f, -0.1f };
		for( size_t k = 0; k < seats.size(); k++ )
		{
			Real a = bearings[seats[k]];
			Real gap = bearings[seats[( k + 1 ) % seats.size()]] - a;
			gap = gap - 2.0f * PI_R * floorf( gap / ( 2.0f * PI_R ) );
			// halfway, and a pair nearer the seat the gap follows: one pair a seat and a half of two, whatever the count
			const Real shares[2] = { 0.5f, 0.38f };
			for( Int s = 0; s < 2; s++ )
			{
				Bool found = FALSE;
				Real angle = 0.0f;
				Int y = 0, x = 0;
				for( Int g = 0; g < 2 && !found; g++ )
					for( Int n = 0; n < 5 && !found; n++ )
					{
						angle = a + ( shares[s] + theNudges[n] ) * gap;
						found = placeOnBearing( heights, g == 0 ? high : openGround, W, H, skel.cy, skel.cx, angle, 0.6f * f.seatRing, outer, &y, &x );
					}
				if( !found )
				{
					everyGap = FALSE;
					continue;
				}
				Real ty = Cos( angle ) * 4.0f, tx = -Sin( angle ) * 4.0f;
				pileSites.push_back( yx( y + ty, x + tx ) );
				pileSites.push_back( yx( y - ty, x - tx ) );
				for( size_t c = 0; c < src.inv.pileCompany.size(); c++ )
				{
					Added one;
					one.name = src.inv.pileCompany[c];
					one.x = x + Cos( angle ) * ( 6.0f + 6.0f * c ) + 0.5f;
					one.y = y + Sin( angle ) * ( 6.0f + 6.0f * c ) + 0.5f;
					one.angle = angle + PI_R;
					added.push_back( one );
				}
			}
		}
	}
	/* A pile at every site, the skeleton's then dropped: moving the few the copies left fed only the
		first gap or two, four piles by one seat and none by the rest. A gap with no room for its sites
		keeps the skeleton's piles where they stood, so no gap goes bare while the next has four. */
	Int serial = 0;
	const Bool atSites = !pileSites.empty() && everyGap;
	if( !atSites )
		kept.insert( kept.end(), piles.begin(), piles.end() );
	for( size_t k = 0; k < pileSites.size() && atSites; k++ )
	{
		neutralObject( skm.raw, "SupplyPileSmall", pileSites[k].x, pileSites[k].y, b, 0.0f, ++serial, kept );
		levelDisc( heights, W, H, (Int)pileSites[k].y, (Int)pileSites[k].x, PILE_FLAT );
	}
	for( size_t k = 0; k < plan.tech.size(); k++ )
	{
		levelDisc( heights, W, H, plan.tech[k].y, plan.tech[k].x, TECH_FLAT );
		Added one;
		one.name = plan.tech[k].name;
		one.x = plan.tech[k].x + 0.5f;
		one.y = plan.tech[k].y + 0.5f;
		one.angle = 0.0f;
		added.push_back( one );
	}

	// every start, dock, derrick and tech building the first start cannot walk to gets a ramp
	std::vector<YX> mustReach( f.starts );
	for( size_t s = 0; s < skm.supplies.size(); s++ ) mustReach.push_back( yx( skm.supplies[s].y + b, skm.supplies[s].x + b ) );
	for( size_t s = 0; s < skm.derricks.size(); s++ ) mustReach.push_back( yx( skm.derricks[s].y + b, skm.derricks[s].x + b ) );
	for( size_t s = 0; s < plan.tech.size(); s++ ) mustReach.push_back( yx( (Real)plan.tech[s].y, (Real)plan.tech[s].x ) );
	const Int ramps = repairReach( heights, W, H, plan, decks, f.play, firstStart, mustReach );

	// a waypoint or a pile the water, a new cliff or a pocket now holds goes to the nearest ground the starts share
	cliffOf( heights, W, H, cliff );
	walkable( cliff, W, H, walk );
	for( Int i = 0; i < W * H; i++ ) levelWet[i] = plan.wet[i] && heights[i] < plan.level[i];
	for( Int i = 0; i < W * H; i++ ) passable[i] = ( ( walk[i] && !levelWet[i] ) || decks[i] ) && f.play[i];
	Mask ground, wetClear;
	reach( passable, W, H, firstStart, ground );
	maskGrow( levelWet, W, H, 3, wetClear );
	for( Int i = 0; i < W * H; i++ ) ground[i] = ground[i] && !wetClear[i] && !decks[i];
	Int snapped = 0;
	for( size_t k = 0; k < kept.size(); k++ )
	{
		Chunk& c = kept[k];
		AsciiString name;
		readAscii( c.body, 20, &name );
		if( !( name == "*Waypoints/Waypoint" ) && !( name == "SupplyPileSmall" ) )
			continue;
		Int cy = (Int)( readReal( c.body, 4 ) / MAP_XY ) + b, cx = (Int)( readReal( c.body, 0 ) / MAP_XY ) + b;
		cy = clampi( cy, 0, H - 1 );
		cx = clampi( cx, 0, W - 1 );
		if( ground[cy * W + cx] )
			continue;
		Int best = -1, bestD = 0x7fffffff;
		for( Int r = 1; r < std::max( W, H ) && ( best < 0 || bestD > r * r ); r++ )
		{
			for( Int y = cy - r; y <= cy + r; y++ )
			{
				if( y < 0 || y >= H ) continue;
				const Bool edgeRow = y == cy - r || y == cy + r;
				for( Int x = cx - r; x <= cx + r; x += edgeRow ? 1 : 2 * r )
				{
					if( x < 0 || x >= W || !ground[y * W + x] ) continue;
					Int d = ( y - cy ) * ( y - cy ) + ( x - cx ) * ( x - cx );
					if( d < bestD ) { bestD = d; best = y * W + x; }
				}
			}
		}
		if( best < 0 ) continue;
		Real wx = ( best % W - b + 0.5f ) * MAP_XY, wy = ( best / W - b + 0.5f ) * MAP_XY;
		memcpy( &c.body[0], &wx, 4 );
		memcpy( &c.body[4], &wy, 4 );
		snapped++;
	}

	// the exemplar's scenery wherever its cell landed off the pads, the kept slopes, the features' edges and the water
	std::vector<YX> taken( pileSites );
	for( size_t k = 0; k < plan.tech.size(); k++ ) taken.push_back( yx( (Real)plan.tech[k].y, (Real)plan.tech[k].x ) );
	for( Int y = 0; y < H; y++ )
	{
		for( Int x = 0; x < W; x++ )
		{
			Int i = y * W + x;
			Int o = syMap[i] * SW + sxMap[i];
			if( src.sceneryFirst[o] == src.sceneryFirst[o + 1] ) continue;
			if( !f.play[i] || pads[i] >= 0.9f || keep[i] >= 0.95f || plan.keepout[i] || wetNear[i] || stamped[i] ) continue;
			// nothing on a wall the new ground made where the exemplar's cell was not one
			if( cliff[i] && !src.m.cliff[o] ) continue;
			Bool near = FALSE;
			for( size_t s = 0; s < taken.size() && !near; s++ )
				near = fabsf( y - taken[s].y ) < 6.0f && fabsf( x - taken[s].x ) < 6.0f;
			if( near ) continue;
			for( Int s = src.sceneryFirst[o]; s < src.sceneryFirst[o + 1]; s++ )
			{
				const Scenery& sc = src.scenery[s];
				if( sc.tree && !treesAllowed[i] ) continue;
				Added one;
				one.name = sc.name;
				one.x = x + sc.fx;
				one.y = y + sc.fy;
				one.angle = sc.angle;
				added.push_back( one );
			}
		}
	}
	Mask standing( W * H, 0 ), standingNear;
	for( size_t k = 0; k < added.size(); k++ )
		if( added[k].name.startsWith( "Tree" ) )
			standing[clampi( (Int)added[k].y, 0, H - 1 ) * W + clampi( (Int)added[k].x, 0, W - 1 )] = 1;
	Int standingCount = 0;
	for( Int i = 0; i < W * H; i++ ) standingCount += standing[i];
	maskGrow( standing, W, H, 1, standingNear );
	Mask freeGround( W * H );
	for( Int i = 0; i < W * H; i++ ) freeGround[i] = treesAllowed[i] && !standingNear[i] && !cliff[i];
	for( size_t s = 0; s < taken.size(); s++ )
		for( Int y = std::max( (Int)taken[s].y - 6, 0 ); y < std::min( (Int)taken[s].y + 7, H ); y++ )
			for( Int x = std::max( (Int)taken[s].x - 6, 0 ); x < std::min( (Int)taken[s].x + 7, W ); x++ )
				freeGround[y * W + x] = 0;

	// clumps of the exemplar's own trees on the allowed ground for what the quilting could not bring
	Int wantedTrees = roundi( treesWanted ) - standingCount;
	{
		std::vector<Int> treeCells;
		for( Int o = 0; o < src.m.W * src.m.H; o++ ) if( src.trees[o] ) treeCells.push_back( o );
		std::vector<Int> cells;
		for( Int i = 0; i < W * H; i++ ) if( freeGround[i] ) cells.push_back( i );
		Int planted = 0;
		std::vector< std::pair<Int, Int> > clump;	// target offset cell, scenery index
		for( Int attempt = 0; attempt < TREE_TRIES * std::max( wantedTrees, 1 ) && planted < wantedTrees && !cells.empty() && !treeCells.empty(); attempt++ )
		{
			Int cell = cells[synthHash( seed, 501, attempt ) % cells.size()];
			Int sourceCell = treeCells[synthHash( seed, 502, attempt ) % treeCells.size()];
			Int y = cell / W, x = cell % W, sy = sourceCell / SW, sx = sourceCell % SW;
			if( src.cls[sourceCell] != src.cls[syMap[cell] * SW + sxMap[cell]] ) continue;
			clump.clear();
			Bool fits = TRUE;
			for( Int ty = sy - CLUMP_CELLS; ty <= sy + CLUMP_CELLS && fits; ty++ )
				for( Int tx = sx - CLUMP_CELLS; tx <= sx + CLUMP_CELLS && fits; tx++ )
				{
					if( ty < 0 || tx < 0 || ty >= src.m.H || tx >= SW ) continue;
					Int o = ty * SW + tx;
					if( !src.trees[o] ) continue;
					Int py = y + ty - sy, px = x + tx - sx;
					fits = py >= 0 && px >= 0 && py < H && px < W && freeGround[py * W + px];
					for( Int s = src.sceneryFirst[o]; s < src.sceneryFirst[o + 1] && fits; s++ )
						if( src.scenery[s].tree )
							clump.push_back( std::make_pair( py * W + px, s ) );
				}
			if( !fits ) continue;
			for( size_t k = 0; k < clump.size(); k++ )
			{
				Int py = clump[k].first / W, px = clump[k].first % W;
				const Scenery& sc = src.scenery[clump[k].second];
				Added one;
				one.name = sc.name;
				one.x = px + sc.fx;
				one.y = py + sc.fy;
				one.angle = sc.angle;
				added.push_back( one );
				planted++;
				for( Int yy = std::max( py - 1, 0 ); yy < std::min( py + 2, H ); yy++ )
					for( Int xx = std::max( px - 1, 0 ); xx < std::min( px + 2, W ); xx++ )
						freeGround[yy * W + xx] = 0;
			}
		}
	}

	// every object goes in neutral; no tower, hut or tent where the new ground has a cliff
	cliffOf( heights, W, H, cliff );
	Int trees = 0, scenery = 0;
	for( size_t k = 0; k < added.size(); k++ )
	{
		const Added& a = added[k];
		Int i = clampi( (Int)a.y, 0, H - 1 ) * W + clampi( (Int)a.x, 0, W - 1 );
		Bool building = a.name.startsWith( "Tent" ) || a.name.startsWith( "WoodTower" ) || a.name.startsWith( "QuonsetHut" );
		if( building && cliff[i] ) continue;
		neutralObject( skm.raw, a.name.str(), a.x, a.y, b, a.angle, ++serial, kept );
		trees += a.name.startsWith( "Tree" );
		scenery++;
	}

	// the bridges, each the exemplar's own pair of objects for its template, moved to its two ends
	for( size_t k = 0; k < plan.bridges.size(); k++ )
	{
		for( size_t p = 0; p + 1 < src.m.things.size(); p++ )
		{
			const MapThing&one = src.m.things[p];
			const MapThing&two = src.m.things[p + 1];
			if( ( one.flags & FLAG_BRIDGE_POINT1 ) && ( two.flags & FLAG_BRIDGE_POINT2 ) && one.name == plan.bridges[k].name )
			{
				movedObject( src.m, (Int)p, skm.raw, plan.bridges[k].a.y, plan.bridges[k].a.x, b, ++serial, kept );
				movedObject( src.m, (Int)p + 1, skm.raw, plan.bridges[k].z.y, plan.bridges[k].z.x, b, ++serial, kept );
				break;
			}
		}
	}

	// the roads, a pair of objects a segment, the exemplar's own pair with the end flags most of its pairs carry
	Int segments = 0;
	for( size_t k = 0; k < plan.roads.size(); k++ )
	{
		const InvRoad *road = NULL;
		for( size_t r = 0; r < src.inv.roads.size(); r++ )
			if( src.inv.roads[r].name == plan.roads[k].name )
				road = &src.inv.roads[r];
		Int pair = -1;
		for( size_t p = 0; p + 1 < src.m.things.size(); p++ )
		{
			const MapThing&one = src.m.things[p];
			const MapThing&two = src.m.things[p + 1];
			if( !( one.flags & FLAG_ROAD_POINT1 ) || !( two.flags & FLAG_ROAD_POINT2 ) || !( one.name == plan.roads[k].name ) )
				continue;
			if( pair < 0 ) pair = (Int)p;
			if( one.flags == road->flags[0] && two.flags == road->flags[1] ) { pair = (Int)p; break; }
		}
		if( pair < 0 ) continue;
		// both ends of a road say they join: where one meets a road of its own kind the renderer counts the
		// shared point and ignores the flag, where it meets another kind it lays the blend between them, and in
		// the open it fades the road out instead of cutting it square
		const std::vector<YX>& points = plan.roads[k].points;
		for( size_t s = 0; s + 1 < points.size(); s++ )
		{
			movedObject( src.m, pair, skm.raw, points[s].y, points[s].x, b, ++serial, kept );
			movedObject( src.m, pair + 1, skm.raw, points[s + 1].y, points[s + 1].x, b, ++serial, kept );
			for( Int e = 0; e < 2; e++ )
			{
				if( e == 0 ? s != 0 : s + 2 != points.size() ) continue;
				Chunk& end = kept[kept.size() - 2 + e];
				Int flags;
				memcpy( &flags, &end.body[16], 4 );
				flags |= FLAG_ROAD_JOIN;
				memcpy( &end.body[16], &flags, 4 );
			}
			segments++;
		}
	}

	// A copy takes a road pair only with both ends on it, so an exemplar road that runs off a copy leaves the
	// pairs it led to cut: a chain ending at the copy's edge, or a stub flagged to join a road that stayed
	// behind. After each copy its road pairs are peeled from every such loose end back to a junction, and
	// what is closed inside the copy stays. Shared ends are the exemplar's own points, compared exactly.
	struct CopiedRoad { size_t at; Int thing; YX ends[2]; };
	std::vector<CopiedRoad> copiedRoads;
	std::vector<Int> cutRoads;
	std::vector< std::pair<YX, YX> > emittedRoads;
	for( size_t k = 0; k < plan.roads.size(); k++ )
		for( size_t s = 0; s + 1 < plan.roads[k].points.size(); s++ )
			emittedRoads.push_back( std::make_pair( plan.roads[k].points[s], plan.roads[k].points[s + 1] ) );
	auto offRoad = []( const YX& e, const YX& a, const YX& z ) -> Real
	{
		Real dy = z.y - a.y, dx = z.x - a.x, span = dy * dy + dx * dx;
		Real t = span > 0.0f ? clampr( ( ( e.y - a.y ) * dy + ( e.x - a.x ) * dx ) / span, 0.0f, 1.0f ) : 0.0f;
		return hypotr( e.y - a.y - t * dy, e.x - a.x - t * dx );
	};
	auto sameSpot = [&]( Int i, Int j ) -> Bool
	{
		return src.m.things[i].x == src.m.things[j].x && src.m.things[i].y == src.m.things[j].y;
	};
	Int copied = 0;
	auto peelCutRoads = [&]()
	{
		const size_t n = copiedRoads.size();
		std::vector<Bool> loose( 2 * n, FALSE ), alive( n, TRUE );
		for( size_t k = 0; k < n; k++ )
			for( Int e = 0; e < 2; e++ )
			{
				const Int t = copiedRoads[k].thing + e;
				for( size_t d = 0; d < cutRoads.size(); d++ )
					loose[2 * k + e] = loose[2 * k + e] || sameSpot( t, cutRoads[d] ) || sameSpot( t, cutRoads[d] + 1 );
				if( !( src.m.things[t].flags & FLAG_ROAD_JOIN ) ) continue;
				Bool near = FALSE;
				for( size_t j = 0; j < emittedRoads.size() && !near; j++ )
					near = offRoad( copiedRoads[k].ends[e], emittedRoads[j].first, emittedRoads[j].second ) <= ROAD_STUB_REACH;
				for( size_t j = 0; j < n && !near; j++ )
					near = j != k && offRoad( copiedRoads[k].ends[e], copiedRoads[j].ends[0], copiedRoads[j].ends[1] ) <= ROAD_STUB_REACH;
				loose[2 * k + e] = loose[2 * k + e] || !near;
			}
		for( Bool peeled = TRUE; peeled; )
		{
			peeled = FALSE;
			for( size_t k = 0; k < n; k++ )
			{
				if( !alive[k] || !( loose[2 * k] || loose[2 * k + 1] ) ) continue;
				// gone; the pair beyond its other end becomes the loose end, unless several meet there
				const Int other = copiedRoads[k].thing + ( loose[2 * k] ? 1 : 0 );
				Int meets = 0, beyond = -1;
				for( size_t j = 0; j < n; j++ )
					for( Int g = 0; g < 2; g++ )
						if( j != k && alive[j] && sameSpot( other, copiedRoads[j].thing + g ) ) { meets++; beyond = 2 * (Int)j + g; }
				alive[k] = FALSE;
				if( meets == 1 ) loose[beyond] = TRUE;
				peeled = TRUE;
			}
		}
		for( size_t k = n; k-- > 0; )
		{
			if( alive[k] )
			{
				emittedRoads.push_back( std::make_pair( copiedRoads[k].ends[0], copiedRoads[k].ends[1] ) );
				continue;
			}
			kept.erase( kept.begin() + copiedRoads[k].at, kept.begin() + copiedRoads[k].at + 2 );
			copied -= 2;
		}
		copiedRoads.clear();
		cutRoads.clear();
	};

	// the towns: every object of the exemplar inside each stamped box, moved with it; a road only with both ends inside
	for( size_t s = 0; s < plan.stamps.size(); s++ )
	{
		const Stamp& st = plan.stamps[s];
		const Int sb = src.m.border;
		size_t k = 0;
		while( k < src.m.things.size() )
		{
			const MapThing&t = src.m.things[k];
			Int group = ( t.flags & FLAG_ROAD_POINT1 ) && k + 1 < src.m.things.size() ? 2 : 1;
			size_t first = k;
			k += group;
			if( t.flags & FLAG_BRIDGE_FLAGS ) continue;
			if( t.waypointID || t.name.startsWithNoCase( "supply" ) || t.name.startsWithNoCase( "tech" ) || strstr( t.name.str(), "aypoint" ) )
				continue;
			Bool inside = TRUE, onCliff = FALSE;
			Real cys[2], cxs[2];
			for( Int g = 0; g < group; g++ )
			{
				const MapThing&u = src.m.things[first + g];
				Real cy = u.y + sb, cx = u.x + sb;
				inside = inside && st.sy <= cy && cy < st.sy + st.rows && st.sx <= cx && cx < st.sx + st.cols;
				cys[g] = cy - st.sy + st.ty;
				cxs[g] = cx - st.sx + st.tx;
				if( inside )
					onCliff = onCliff || cliff[clampi( (Int)cys[g], 0, H - 1 ) * W + clampi( (Int)cxs[g], 0, W - 1 )];
			}
			const Bool road = ( t.flags & FLAG_ROAD_POINT1 ) && group == 2;
			if( !inside || onCliff )
			{
				if( road ) cutRoads.push_back( (Int)first );
				continue;
			}
			if( road )
			{
				CopiedRoad one = { kept.size(), (Int)first, { yx( cys[0], cxs[0] ), yx( cys[1], cxs[1] ) } };
				copiedRoads.push_back( one );
			}
			for( Int g = 0; g < group; g++ )
			{
				movedObject( src.m, (Int)( first + g ), skm.raw, cys[g], cxs[g], b, ++serial, kept );
				copied++;
			}
		}
		peelCutRoads();
	}

	// what stands on a copy goes with it, turned as it is: a pad brings its base's money and dressing (no
	// building when it shrank, they would stand in each other), a crossing its town; the bridges are planned
	for( size_t c = 0; c < plan.copies.size(); c++ )
	{
		const Copy& copy = plan.copies[c];
		const Bool pad = copy.radius > 0.0f;
		const Int sb = src.m.border;
		size_t k = 0;
		while( k < src.m.things.size() )
		{
			const MapThing&t = src.m.things[k];
			Int group = ( t.flags & ( FLAG_ROAD_POINT1 | FLAG_BRIDGE_POINT1 ) ) && k + 1 < src.m.things.size() ? 2 : 1;
			size_t first = k;
			k += group;
			if( ( t.flags & FLAG_BRIDGE_FLAGS ) || t.waypointID || strstr( t.name.str(), "aypoint" ) )
				continue;
			const Bool money = t.name.startsWithNoCase( "supply" ) || t.name.startsWithNoCase( "tech" );
			if( !pad && money )
				continue;
			if( pad && copy.scale < 1.0f && group == 1 && !money && civilian && civilian( t.name.str() ) )
				continue;
			// a kind of money the pads fell back on is the skeleton's (carryMoney)
			const Int kind = moneyKind( t.name );
			if( kind >= 0 && moneyFellBack[kind] )
				continue;
			// money where padSite puts it; the rest where it fell, if it fell on the map and not on a new wall
			Bool inside = TRUE;
			YX ats[2];
			for( Int g = 0; g < group && inside; g++ )
			{
				const MapThing&u = src.m.things[first + g];
				if( pad && money )
				{
					inside = padSite( src, copy, u.y + sb, u.x + sb, skm, ats[g] );
					continue;
				}
				inside = copy.carries( u.y + sb, u.x + sb );
				ats[g] = copy.toTarget( u.y + sb, u.x + sb );
				const Int ty = roundi( ats[g].y ), tx = roundi( ats[g].x );
				inside = inside && ty >= 0 && tx >= 0 && ty < H && tx < W;
				if( inside && pad && group == 1 )
				{
					const Int o = clampi( roundi( u.y + sb ), 0, src.m.H - 1 ) * src.m.W + clampi( roundi( u.x + sb ), 0, src.m.W - 1 );
					inside = !cliff[ty * W + tx] || src.m.cliff[o];
				}
			}
			const Bool road = ( t.flags & FLAG_ROAD_POINT1 ) && group == 2;
			if( !inside )
			{
				if( road ) cutRoads.push_back( (Int)first );
				continue;
			}
			if( road )
			{
				CopiedRoad one = { kept.size(), (Int)first, { ats[0], ats[1] } };
				copiedRoads.push_back( one );
			}
			for( Int g = 0; g < group; g++ )
			{
				const MapThing&u = src.m.things[first + g];
				const YX at = ats[g];
				movedObject( src.m, (Int)( first + g ), skm.raw, at.y, at.x, b, ++serial, kept );
				Real turned = copy.angle( u.angle );
				memcpy( &kept.back().body[12], &turned, 4 );
				copied++;
			}
		}
		peelCutRoads();
	}

	// the chunks: heights, textures, objects, water, the exemplar's light and weather
	RawMap& out = skm.raw;
	Chunk *heightChunk = out.chunk( "HeightMapData" );
	for( Int i = 0; i < W * H; i++ )
		heightChunk->body[heightChunk->body.size() - W * H + i] = (char)(UnsignedByte)heights[i];
	cliffOf( heights, W, H, cliff );
	Chunk *blend = out.chunk( "BlendTileData" );
	std::vector<char> blendBody;
	if( !blendChunk( src, W, H, syMap, sxMap, plan.fixedTurn, cliff, heights, blendBody ) )
	{
		DEBUG_LOG(( "random map: '%s' needs more blends than the renderer holds\n", clamped.m_template.str() ));
		restoreFPMode( callersFPMode );
		return FALSE;
	}
	blend->version = 8;
	blend->body.swap( blendBody );
	Chunk *objects = out.chunk( "ObjectsList" );
	objects->version = 3;
	objects->body.clear();
	joinChunks( kept, objects->body );
	Chunk *triggers = out.chunk( "PolygonTriggers" );
	std::vector<char> triggerBody;
	if( !triggers || !waterTriggers( *triggers, plan.water, b, triggerBody ) )
	{
		restoreFPMode( callersFPMode );
		return FALSE;
	}
	triggers->body.swap( triggerBody );
	for( size_t c = 0; c < src.m.raw.chunks.size(); c++ )
	{
		if( src.m.raw.nameOf( src.m.raw.chunks[c].id ) != "GlobalLighting" ) continue;
		Chunk *light = out.chunk( "GlobalLighting" );
		light->version = src.m.raw.chunks[c].version;
		light->body = src.m.raw.chunks[c].body;
	}
	Chunk *world = out.chunk( "WorldInfo" );
	{
		std::vector<DictEntry> entries;
		readDict( world->body, 0, entries );
		for( size_t e = 0; e < entries.size(); e++ )
			if( out.nameOf( entries[e].key ) == "weather" && entries[e].type == Dict::DICT_INT )
				memcpy( &world->body[entries[e].pos], &src.m.weather, 4 );
	}
	out.bytes( mapBytes );

	// every start walks to every other, every dock, derrick and tech building, over the bridges and round the water
	for( Int i = 0; i < W * H; i++ ) levelWet[i] = plan.wet[i] && heights[i] < plan.level[i];
	walkable( cliff, W, H, walk );
	for( Int i = 0; i < W * H; i++ ) passable[i] = ( ( walk[i] && !levelWet[i] ) || decks[i] ) && f.play[i];
	reach( passable, W, H, firstStart, reached );
	auto cutOff = [&]( Real py, Real px ) -> Bool
	{
		for( Int y = (Int)py - 2; y <= (Int)py + 2; y++ )
			for( Int x = (Int)px - 2; x <= (Int)px + 2; x++ )
				if( y >= 0 && x >= 0 && y < H && x < W && reached[y * W + x] )
					return FALSE;
		return TRUE;
	};
	RMGTemplateReport r;
	r.m_cutOffStarts = r.m_cutOffSites = r.m_cutOffTech = 0;
	for( size_t s = 0; s < f.starts.size(); s++ ) r.m_cutOffStarts += cutOff( f.starts[s].y, f.starts[s].x );
	for( size_t s = 0; s < skm.supplies.size(); s++ ) r.m_cutOffSites += cutOff( (Int)( skm.supplies[s].y + 0.5f ) + b, (Int)( skm.supplies[s].x + 0.5f ) + b );
	for( size_t s = 0; s < skm.derricks.size(); s++ ) r.m_cutOffSites += cutOff( (Int)( skm.derricks[s].y + 0.5f ) + b, (Int)( skm.derricks[s].x + 0.5f ) + b );
	for( size_t s = 0; s < plan.tech.size(); s++ ) r.m_cutOffTech += cutOff( (Real)plan.tech[s].y, (Real)plan.tech[s].x );
	r.m_bridges = (Int)plan.bridges.size();
	r.m_rivers = (Int)plan.lines.size();
	r.m_towns = (Int)plan.stamps.size();
	r.m_tech = (Int)plan.tech.size();
	r.m_roads = segments;
	r.m_trees = trees;
	r.m_scenery = scenery;
	r.m_patches = patches;
	r.m_notBuilt = (Int)plan.notes.size();
	r.m_pads = (Int)basePads.size();
	r.m_ramps = ramps;
	for( size_t n = 0; n < plan.notes.size(); n++ )
	{
		r.m_notes.concat( plan.notes[n] );
		r.m_notes.concat( "\n" );
	}
	DEBUG_LOG(( "random map: '%s' seed %d, %d players, %d cells: %d patches, %d scenery (%d trees, want %d), "
		"%d tech, %d bridges, %d rivers, %d road segments, %d towns, %d copies (%d objects), %d water areas, %d ramps cut, %d waypoints moved; "
		"cut off: starts %d, docks and derricks %d, tech %d\n",
		clamped.m_template.str(), seed, clamped.m_numPlayers, clamped.m_playableCells, patches, scenery, trees,
		roundi( treesWanted ), (Int)plan.tech.size(), (Int)plan.bridges.size(), (Int)plan.lines.size(), segments, (Int)plan.stamps.size(),
		(Int)plan.copies.size(), copied, (Int)plan.water.size(), ramps, snapped, r.m_cutOffStarts, r.m_cutOffSites, r.m_cutOffTech ));
	for( size_t n = 0; n < plan.notes.size(); n++ )
		DEBUG_LOG(( "random map:   not built: %s\n", plan.notes[n].str() ));
	if( report )
		*report = r;

	if( previewBytes )
	{
		std::vector<Int> cls( W * H );
		for( Int i = 0; i < W * H; i++ ) cls[i] = src.cls[syMap[i] * SW + sxMap[i]];
		writePreview( src, skm, heights, cls, levelWet, cliff, f.starts, *previewBytes );
	}

	restoreFPMode( callersFPMode );
	return TRUE;
}

//-----------------------------------------------------------------------------
// The engine's side: the template off the file system, its CRC, its name
//-----------------------------------------------------------------------------

static Bool readTemplate( const AsciiString& templatePath, std::vector<char>& bytes )
{
	File *file = TheFileSystem->openFile( templatePath.str(), File::READ | File::BINARY );
	if( file == NULL )
		return FALSE;
	Int size = file->size();
	char *contents = file->readEntireAndClose();
	bytes.assign( contents, contents + size );
	delete [] contents;
	return size > 0;
}

static UnsignedInt crcOfTemplate( const std::vector<char>& bytes )
{
	CRC crc;
	crc.computeCRC( &bytes[0], (Int)bytes.size() );
	UnsignedInt value = crc.get();
	return value == 0 ? 1 : value;		// 0 is "not installed"
}

Bool rmgTemplateCRC( const AsciiString& templatePath, UnsignedInt *crcOut )
{
	// read once a run: the name of every generated map asks, and the file does not change under a running game
	static std::map<AsciiString, UnsignedInt> theCRCs;
	std::map<AsciiString, UnsignedInt>::const_iterator it = theCRCs.find( templatePath );
	if( it != theCRCs.end() )
	{
		*crcOut = it->second;
		return it->second != 0;
	}
	std::vector<char> bytes;
	*crcOut = 0;
	if( TheFileSystem == NULL || !readTemplate( templatePath, bytes ) )
		return FALSE;
	UnsignedInt value = crcOfTemplate( bytes );
	theCRCs[templatePath] = value;
	*crcOut = value;
	return TRUE;
}

struct TemplateShape { Int cellsX, cellsY, seats; };

Bool rmgTemplateShape( const AsciiString& templatePath, Int *cellsX, Int *cellsY, Int *seats )
{
	// the size buttons ask on every click; a map that would not load is kept as no seats
	static std::map<AsciiString, TemplateShape> theShapes;
	std::map<AsciiString, TemplateShape>::const_iterator it = theShapes.find( templatePath );
	if( it == theShapes.end() )
	{
		TemplateShape shape = { 0, 0, 0 };
		std::vector<char> bytes, raw;
		if( TheFileSystem == NULL || !readTemplate( templatePath, bytes ) )
			return FALSE;
		SynthMap map;
		try
		{
			if( unpackExemplar( bytes, raw ) && map.load( raw ) )
			{
				shape.cellsX = map.playX;
				shape.cellsY = map.playY;
				shape.seats = (Int)map.starts.size();
			}
		}
		catch( const ShortMap& fault )
		{
			DEBUG_LOG(( "random map: the template '%s' ends at byte %d of a chunk, %d wanted at %d\n",
				templatePath.str(), fault.size, fault.count, fault.pos ));
		}
		it = theShapes.insert( std::make_pair( templatePath, shape ) ).first;
	}
	*cellsX = it->second.cellsX;
	*cellsY = it->second.cellsY;
	*seats = it->second.seats;
	return it->second.seats > 0;
}

Bool rmgTemplateToken( const AsciiString& templatePath, AsciiString& tokenOut )
{
	AsciiString lower = templatePath;
	lower.toLower();
	if( !lower.startsWith( "maps\\" ) || !lower.endsWith( ".map" ) )
		return FALSE;
	const char *rest = lower.str() + 5;
	const char *slash = strchr( rest, '\\' );
	if( slash == NULL || slash == rest )
		return FALSE;
	std::string folder( rest, slash - rest );
	std::string file( slash + 1 );
	if( file != folder + ".map" )
		return FALSE;
	tokenOut = folder.c_str();
	return TRUE;
}

void rmgTemplateFromToken( const AsciiString& token, AsciiString& templatePathOut )
{
	templatePathOut.format( "maps\\%s\\%s.map", token.str(), token.str() );
	templatePathOut.toLower();
}

/// The object INI's answer: a structure that is not a bridge, supply or tech building.
static Bool engineCivilian( const char *name )
{
	if( strncasecmp( name, "supply", 6 ) == 0 || strncasecmp( name, "tech", 4 ) == 0 )
		return FALSE;
	const ThingTemplate *thing = TheThingFactory->findTemplate( AsciiString( name ), FALSE );
	return thing && thing->isKindOf( KINDOF_STRUCTURE ) && !thing->isKindOf( KINDOF_BRIDGE );
}

void rmgGenerateFromTemplate( const RandomMapSettings& clamped, std::vector<char>& mapBytes, std::vector<char> *previewBytes )
{
	mapBytes.clear();
	/* The towns are told apart by the object INI, so without it there is nothing to build: the same
		name must never stand for two maps. Another INI may call other things a building, which the
		join's INI checksum refuses before any map is built. */
	if( TheThingFactory == NULL )
		return;
	// the CRC of the bytes read now, not the one the name was checked against: another file may have
	// been put under the path since
	std::vector<char> exemplar;
	if( !readTemplate( clamped.m_template, exemplar ) || crcOfTemplate( exemplar ) != clamped.m_templateCRC )
	{
		DEBUG_LOG(( "random map: template '%s' with CRC %08X is not installed here\n", clamped.m_template.str(), clamped.m_templateCRC ));
		return;
	}
	if( !rmgSynthesise( clamped, exemplar, engineCivilian, mapBytes, previewBytes, NULL ) )
		mapBytes.clear();
}
