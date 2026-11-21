// test_fracture.cpp: the pre-fractured structures' files and holes (W3DDevice/GameClient/FractureCore.h).
// A hit takes what it lands on and never what something else still stands on; the files read back as
// lwfrag.py and lwcollapse.py write them, and a broken one is refused.

#include "test_harness.h"

#include "W3DDevice/GameClient/FractureCore.h"

#include <string.h>
#include <vector>

using namespace Fracture;

namespace {

/** A unit cube fragment at (x, y, z), its box and centroid, with these neighbours.  No triangles: the
	support rule and the hole ask only for boxes and centroids. */
void addCube( Asset &a, float x, float y, float z, const std::vector<unsigned> &neighbours )
{
	Fragment f;
	memset( &f, 0, sizeof( f ) );
	f.centroid[0] = x; f.centroid[1] = y; f.centroid[2] = z;
	for( int k = 0; k < 3; ++k )
	{
		f.boxMin[k] = f.centroid[k] - 0.5f;
		f.boxMax[k] = f.centroid[k] + 0.5f;
	}
	f.volume = 1.0f;
	f.firstNeighbour = (unsigned)a.neighbours.size();
	f.neighbours = (unsigned)neighbours.size();
	a.neighbours.insert( a.neighbours.end(), neighbours.begin(), neighbours.end() );
	f.grounded = f.boxMin[2] < GROUNDED_BELOW;
	a.fragments.push_back( f );
}

/** Two columns three high, x 0 and x 1, z 1 (on the ground), 4 and 7, joined only at the top:
	0 1 2 the left, 3 4 5 the right, 2 and 5 touching. */
Asset twoColumns()
{
	Asset a;
	addCube( a, 0, 0, 1, { 1 } );
	addCube( a, 0, 0, 4, { 0, 2 } );
	addCube( a, 0, 0, 7, { 1, 5 } );
	addCube( a, 1, 0, 1, { 4 } );
	addCube( a, 1, 0, 4, { 3, 5 } );
	addCube( a, 1, 0, 7, { 4, 2 } );
	a.volume = 6.0f;
	return a;
}

void put( std::vector<unsigned char> &b, const void *data, size_t n )
{
	const unsigned char *p = (const unsigned char *)data;
	b.insert( b.end(), p, p + n );
}
void putU32( std::vector<unsigned char> &b, unsigned v ) { put( b, &v, 4 ); }
void putU16( std::vector<unsigned char> &b, unsigned short v ) { put( b, &v, 2 ); }
void putF32( std::vector<unsigned char> &b, float v ) { put( b, &v, 4 ); }

/// One material and two fragments as lwfrag.py writes them: a one-triangle fragment on the ground and
/// one at z 10 beside it, neighbours of each other.
std::vector<unsigned char> smallLwfrag()
{
	std::vector<unsigned char> b;
	put( b, "LWFR", 4 );
	putU32( b, 1 );
	putU32( b, 1 );
	putU32( b, 2 );
	for( int k = 0; k < 6; ++k ) putF32( b, k < 3 ? 0.0f : 11.0f );
	char name[32] = "LWFort_Stone.tga";
	put( b, name, 32 );
	b.push_back( 0 );
	b.push_back( 0 );
	putU16( b, 0 );
	for( int i = 0; i < 2; ++i )
	{
		const float z = i == 0 ? 0.0f : 10.0f;
		putF32( b, 0.3f ); putF32( b, 0.3f ); putF32( b, z + 0.3f );
		putF32( b, 2.0f );
		b.push_back( 1 );
		b.push_back( i == 0 ? FRAGMENT_GROUND : 0 );
		putU16( b, (unsigned short)( 4 + i ) );
		putU32( b, 1 );
		putU32( b, 3 );
		putU32( b, 1 );
		putU32( b, (unsigned)( 1 - i ) );
		putF32( b, 0.5f );
		const float corner[3][3] = { { 0, 0, z }, { 1, 0, z }, { 0, 1, z + 1 } };
		for( int v = 0; v < 3; ++v )
		{
			for( int k = 0; k < 3; ++k ) putF32( b, corner[v][k] );
			putF32( b, 0 ); putF32( b, 0 ); putF32( b, 1 );
			putF32( b, 0.5f ); putF32( b, 0.5f );
		}
		putU16( b, 0 ); putU16( b, 1 ); putU16( b, 2 ); putU16( b, 0 );
	}
	return b;
}

}	// namespace

/** A file lwfrag.py writes reads back: the counts, the neighbours, a fragment's box from its vertices,
	and which stand on the ground. */
TEST(fracture_lwfrag_reads_back_what_lwfrag_py_writes)
{
	const std::vector<unsigned char> b = smallLwfrag();
	Asset a;
	CHECK( parse( &b[0], b.size(), a ) );
	CHECK_EQ( 1, (int)a.materials.size() );
	CHECK_STR( "LWFort_Stone.tga", a.materials[0].texture );
	CHECK_EQ( 2, (int)a.fragments.size() );
	CHECK_EQ( 6, (int)a.vertices.size() );
	CHECK_EQ( 1u, a.neighbours[a.fragments[0].firstNeighbour] );
	CHECK_EQ( 5, (int)a.fragments[1].part );
	CHECK_NEAR( 10.0f, a.fragments[1].boxMin[2], 1e-6f );
	CHECK_NEAR( 11.0f, a.fragments[1].boxMax[2], 1e-6f );
	CHECK( a.fragments[0].grounded );
	CHECK( !a.fragments[1].grounded );
	CHECK_NEAR( 4.0f, a.volume, 1e-6f );
}

/// A cut-off file, a wrong magic and a triangle past its vertices are refused.
TEST(fracture_lwfrag_refuses_a_broken_file)
{
	std::vector<unsigned char> b = smallLwfrag();
	Asset a;
	CHECK( !parse( &b[0], b.size() - 1, a ) );
	std::vector<unsigned char> magic = b;
	magic[0] = 'X';
	CHECK( !parse( &magic[0], magic.size(), a ) );
	std::vector<unsigned char> index = b;
	index[b.size() - 8] = 7;		// the last triangle's first index, of three vertices
	CHECK( !parse( &index[0], index.size(), a ) );
}

/** A hit on the left column's middle takes it, since the left top still hangs from the right top.  The
	right column's middle then cannot go: both tops would be left on nothing. */
TEST(fracture_a_hit_takes_what_it_lands_on_and_keeps_everything_standing)
{
	const Asset a = twoColumns();
	std::vector<unsigned char> standing( 6, 1 ), kept( 6, 0 );
	std::vector<unsigned> out;
	const float middle[3] = { 0, 0, 4 };
	knockOut( a, standing, kept, middle, 0.8f, 100.0f, out );
	CHECK_EQ( 1, (int)out.size() );
	CHECK_EQ( 1u, out[0] );		// the left top still hangs from the right one
	const float rightMiddle[3] = { 1, 0, 4 };
	knockOut( a, standing, kept, rightMiddle, 0.8f, 100.0f, out );
	CHECK_EQ( 0, (int)out.size() );		// both tops would be left on nothing
	CHECK_EQ( 1, (int)standing[4] );
}

/// A blast over the whole of one column takes it, nearest first, but stops short of leaving the other
/// column's top without its way down.
TEST(fracture_a_wide_blast_stops_at_what_the_rest_stands_on)
{
	const Asset a = twoColumns();
	std::vector<unsigned char> standing( 6, 1 ), kept( 6, 0 );
	std::vector<unsigned> out;
	const float left[3] = { -0.6f, 0, 4 };
	knockOut( a, standing, kept, left, 3.2f, 100.0f, out );
	// 1 goes first (nearest); the right middle, in reach too, would leave the tops on nothing; then 0 and 2
	CHECK_EQ( 3, (int)out.size() );
	for( int i = 3; i < 6; ++i )
		CHECK_EQ( 1, (int)standing[i] );
	const float right[3] = { 1, 0, 4 };
	knockOut( a, standing, kept, right, 3.6f, 100.0f, out );
	CHECK_EQ( 1, (int)out.size() );
	CHECK_EQ( 5u, out[0] );		// its top; the middle and the foot hold each other up
}

/// A kept fragment (a gun) never goes, the budget caps the volume, and a point beside the structure
/// takes the one fragment within reach.
TEST(fracture_a_hit_spares_the_gun_and_keeps_to_its_budget)
{
	const Asset a = twoColumns();
	std::vector<unsigned char> standing( 6, 1 ), kept( 6, 0 );
	kept[2] = 1;
	std::vector<unsigned> out;
	const float top[3] = { -0.4f, 0, 7 };		// 0.9 from the right top's box: out of a 0.8 hole's reach
	knockOut( a, standing, kept, top, 0.8f, 100.0f, out );
	CHECK_EQ( 0, (int)out.size() );
	knockOut( a, standing, kept, top, 3.6f, 1.5f, out );
	CHECK_EQ( 1, (int)out.size() );		// one cube's worth of budget
	const float beside[3] = { 0, 0, 10.1f };
	std::vector<unsigned char> fresh( 6, 1 ), none( 6, 0 );
	knockOut( a, fresh, none, beside, 3.0f, 100.0f, out );
	CHECK_EQ( 1, (int)out.size() );
	CHECK_EQ( 2u, out[0] );
}

/// A baked track stands before its start, blends between keys and rests on its last.
TEST(fracture_lwcollapse_track_blends_and_rests)
{
	std::vector<unsigned char> b;
	put( b, "LWCO", 4 );
	putU32( b, 1 );
	putU32( b, 2 );
	putU16( b, 30 );
	putU16( b, 3 );
	putF32( b, 1.0f / 64.0f );
	putU32( b, 2 );
	putU16( b, 0 ); putU16( b, 0 ); putU32( b, 0 ); b.push_back( COLLAPSE_STAYS ); b.push_back( 0 ); putU16( b, 0 );
	putU16( b, 1 ); putU16( b, 2 ); putU32( b, 0 ); b.push_back( 0 ); b.push_back( 0 ); putU16( b, 0 );
	const short keys[2][7] = { { 0, 0, 640, 0, 0, 0, 32767 }, { 64, 0, 0, 0, 0, 32767, 0 } };
	put( b, keys, sizeof( keys ) );
	Collapse c;
	CHECK( parseCollapse( &b[0], b.size(), 2, c ) );
	CHECK( !parseCollapse( &b[0], b.size(), 3, c ) );
	CHECK( parseCollapse( &b[0], b.size(), 2, c ) );
	float pos[3], rot[4];
	CHECK( !trackPose( c, 0, 2.0f, pos, rot ) );		// a stump
	CHECK( !trackPose( c, 1, 0.5f, pos, rot ) );		// before its start
	CHECK( trackPose( c, 1, 1.5f, pos, rot ) );
	CHECK_NEAR( 0.5f, pos[0], 1e-4f );
	CHECK_NEAR( 5.0f, pos[2], 1e-4f );
	CHECK_NEAR( 0.70711f, rot[2], 1e-3f );
	CHECK( trackPose( c, 1, 9.0f, pos, rot ) );
	CHECK_NEAR( 1.0f, pos[0], 1e-4f );
	CHECK_NEAR( 1.0f, rot[2], 1e-4f );

	// version 2: the same block twice, falling toward +x and toward -y, as lwcollapse.py writes it
	std::vector<unsigned char> v2;
	put( v2, "LWCO", 4 );
	putU32( v2, 2 );
	putU32( v2, 2 );
	putU16( v2, 30 );
	putU16( v2, 3 );
	putF32( v2, 1.0f / 64.0f );
	putU32( v2, 2 );
	const unsigned first = 24 + 2 * 12;
	putF32( v2, 0.0f ); putU32( v2, first ); putU32( v2, (unsigned)b.size() );
	putF32( v2, 270.0f ); putU32( v2, first + (unsigned)b.size() ); putU32( v2, (unsigned)b.size() );
	put( v2, &b[0], b.size() );
	put( v2, &b[0], b.size() );
	std::vector<Collapse> ways;
	CHECK( parseCollapses( &v2[0], v2.size(), 2, ways ) );
	CHECK_EQ( 2, (int)ways.size() );
	CHECK_NEAR( 270.0f, ways[1].angle, 1e-4f );
	CHECK_EQ( 0, (int)nearestCollapse( ways, 30.0f ) );
	CHECK_EQ( 1, (int)nearestCollapse( ways, -100.0f ) );		// 260: nearer 270 than 360
	CHECK_EQ( 0, (int)nearestCollapse( ways, 350.0f ) );
	CHECK( !parseCollapses( &v2[0], v2.size() - 1, 2, ways ) );
	CHECK( parseCollapses( &b[0], b.size(), 2, ways ) );
	CHECK_EQ( 1, (int)ways.size() );
}

/// A ruin piece over air drops onto what is under it, and the one resting on it comes down with it.
TEST(fracture_a_ruin_piece_over_air_drops_and_takes_its_load_with_it)
{
	const Box pieces[4] =
	{
		{ { 0, 0, 0 }, { 10, 10, 4 } },				// on the ground
		{ { 0, 0, 4 }, { 10, 10, 8 } },				// on the first
		{ { 20, 0, 9 }, { 30, 10, 12 } },			// 9 units of air under it
		{ { 22, 2, 12 }, { 28, 8, 14 } },			// on the third
	};
	const std::vector<Box> boxes( pieces, pieces + 4 );
	const std::vector<float> ground( 4, 0.0f );
	std::vector<float> gaps;
	dropGaps( boxes, ground, 1.5f, 0.5f, gaps );
	CHECK_NEAR( 0.0f, gaps[0], 1e-6f );
	CHECK_NEAR( 0.0f, gaps[1], 1e-6f );
	CHECK_NEAR( 9.0f, gaps[2], 1e-6f );
	CHECK_NEAR( 9.0f, gaps[3], 1e-6f );
	std::vector<float> higher( 4, 8.0f );		// ground up to 1 unit under the third: it stays
	dropGaps( boxes, higher, 1.5f, 0.5f, gaps );
	CHECK_NEAR( 0.0f, gaps[2], 1e-6f );
	CHECK_NEAR( 0.0f, gaps[3], 1e-6f );
	const Box corner[2] = { { { 0, 0, 0 }, { 10, 10, 4 } }, { { 9.8f, 0, 6 }, { 20, 10, 8 } } };		// a sliver across: not held
	dropGaps( std::vector<Box>( corner, corner + 2 ), std::vector<float>( 2, 0.0f ), 1.5f, 0.5f, gaps );
	CHECK_NEAR( 6.0f, gaps[1], 1e-6f );
}
