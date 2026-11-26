// The rally point (docs: the 2026-11-24 rally DESIGN.md; in the mouse cursors' hand since 2026-11-26, the
// pennant since that evening, the owner's pick of three in experiments/rally-refined).  A production
// building's rally point is a hairline pole with the side's slim pennant on it, a fine ring on the ground
// round its foot, and a dashed rail from the building's door to it, the dashes creeping towards the
// point; several buildings on one point share one ring.  Every stroke holds its width on the screen: an
// offset towards the screen's top is drawn 1/foreshortening long on the ground, and anything standing
// upright on the screen as tall.  Every piece comes out of ReforgedRallyMarkers.dds and is laid
// on the ground the ground markers' way (W3DGroundMarkers): textured quads in the terrain pass, depth
// tested with each vertex pulled towards the eye, never depth written, one texture stage times the
// vertex colour, alpha blended and the ping added, so fixed-function on every device.  The quads that
// stand on water go after the water, at its surface.  The shroud only dims it: a rally point is the
// player's own and is never blacked out.
//
// W3DWaypointBuffer reads the selected buildings at the sync point and hands their paths over
// (W3DRallyMarkers_addPath); the take keeps them with the ground under them, notes a rally point that
// moved (the click), and the draw runs beside the tick on what the take left and the wall clock.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "W3DDevice/GameClient/W3DRallyMarkers.h"
#include "W3DDevice/GameClient/W3DGroundMarkers.h"		// GROUND_MARKS_ZOOM

#include <math.h>
#include <vector>

#include "Lib/Clock.h"
#include "WWMath/matrix3d.h"
#include "Common/AudioEventRTS.h"		// BodyModule.h holds these by value and leaves them to PreRTS.h,
#include "GameClient/RadiusDecal.h"		// which no device file includes (W3DGroundMarkers does the same)
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BodyModule.h"	// BODY_RUBBLE: a fallen bridge takes no rail
#include "Common/Debug.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"

namespace {

///////////////////////////////////////////////////////////////////////////////////////////////////
// CONSTANTS (DESIGN.md; lengths in k unless they say otherwise)
///////////////////////////////////////////////////////////////////////////////////////////////////

const Real RALLY_PI = 3.14159265358979f;
/// world units a k: K_BASE times GROUND_MARKS_ZOOM, the size the rally point drew at the closest zoom while
/// it followed the camera, now the same in the world at every zoom (owner, 2026-11-26).  Until then it was
/// 0.8 times the zoom held to 1 to 3.5
const Real K_BASE = 0.45f;
const Real K_WORLD = K_BASE * GROUND_MARKS_ZOOM;
const Real LIFT = 0.35f;								///< world units out of the dirt
const Real PULL = 3.0f;									///< world units towards the eye at zoom 1, times the zoom past it (depth only)
const Real GROUND_STEP = 10.0f;					///< world units: no quad laid on the ground is longer
const Real SAMPLE_STEP = 5.0f;					///< world units between two heights taken under a rail
const Real NO_WATER = -1.0e30f;
const Real SHARE_DISTANCE = 8.0f;				///< world units: rally points closer are one
// a k is about 2.24 pixels across the screen at 1080 (the v3 frames: the old frame's 42k were 94 pixels),
// so the prototype's pixels are these over 2.24.  A stroke's width is its band piece's across, of which the
// dark line is 0.95 and the fill 0.45 (rally_markers.py bands)
const Real KEY = 0.45f;									///< the cursors' one pixel dark line
const Real RING_R = 12.0f;							///< the ring's radius on the ground, to the middle of its stroke
const Real RING_ACROSS = 1.8f;					///< its stroke: 3.8 pixels of dark, 1.8 of fill
const Real POLE_ACROSS = 1.7f;
const Real POLE_H = 25.0f;							///< up the screen from the foot
const Real FOOT = 4.0f;									///< the foot's dot, round on the screen
const Real DASH_ACROSS = 1.9f;
const Real DASH_LEN = 3.1f, DASH_GAP = 2.2f;	///< along the rail, on the ground
const Real DASH_SPEED = 12.0f;					///< k a second they creep towards the point
const Real DASH_RUSH = 30.0f;						///< k they run extra in the first 0.6 s after a click
const Real FRAME_REACH = RING_R + 1.0f;	///< the ring's outer edge, about
const Real RAIL_STOP = RING_R + 2.5f;		///< the rail stops this short of the point, just off the ring
const Real FS_MIN = 0.45f;							///< the least foreshortening a stroke is held against
/// what a ring and its ping (out to 1.9 of the ring) cover round the point, before the snap
const Real BEACON_REACH = 1.9f * FRAME_REACH + 1.0f;
/// and the flag: the pole up the screen over the least foreshortening, the pennant beside it (13.5 x 58.9)
const Real FLAG_REACH = 61.0f;
const Real SNAP = 0.25f;								///< s, the ring's snap on a click
const Real SNAP_GROW = 0.5f;						///< and how much larger it starts
const Real GROW = 0.4f;									///< s, the rail growing from the door
const Real HOIST_X = 0.4f;							///< the pennant's hoist on the pole's fill, right of the middle
const Real HOIST_Y = POLE_H - KEY;			///< its top edge a dark line under the pole's top
const Real PENNANT_W = 15.0f, PENNANT_H = 9.0f, PENNANT_AU = 1.0f / 15.0f, PENNANT_AV = 1.0f / 9.0f;	///< rally_markers.py's, whole texels at 4 a k
const Real FOG_SHADE = 0.8f;
const Real SHROUD_SHADE = 0.55f;
const UnsignedInt SEEN_KEEP_MS = 1000;	///< a building not handed over this long is forgotten: selected again it grows again

/// the side's pennant: America swallow-tailed, China stepped, the GLA torn, Turkiye cut at 30 degrees
enum { FINISH_AMERICA, FINISH_CHINA, FINISH_GLA, FINISH_TURKIYE, FINISH_COUNT };

///////////////////////////////////////////////////////////////////////////////////////////////////
// SMALL HELPERS
///////////////////////////////////////////////////////////////////////////////////////////////////

struct Tint
{
	Real r, g, b;
};
const Tint TINT_WHITE = { 1.0f, 1.0f, 1.0f };

Real clamp01( Real x )
{
	return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x );
}

Real easeOut( Real x )
{
	const Real r = 1.0f - clamp01( x );
	return 1.0f - r * r * r;
}

Real easeBack( Real x )
{
	const Real c = 1.70158f, t = clamp01( x ) - 1.0f;
	return 1.0f + ( c + 1.0f ) * t * t * t + c * t * t;
}

Tint scaled( const Tint &t, Real f )
{
	Tint o = { MIN( 1.0f, t.r * f ), MIN( 1.0f, t.g * f ), MIN( 1.0f, t.b * f ) };
	return o;
}

UnsignedInt argb( const Tint &t, Real alpha )
{
	const UnsignedInt a = (UnsignedInt)( clamp01( alpha ) * 255.0f + 0.5f );
	const UnsignedInt r = (UnsignedInt)( clamp01( t.r ) * 255.0f + 0.5f );
	const UnsignedInt g = (UnsignedInt)( clamp01( t.g ) * 255.0f + 0.5f );
	const UnsignedInt b = (UnsignedInt)( clamp01( t.b ) * 255.0f + 0.5f );
	return ( a << 24 ) | ( r << 16 ) | ( g << 8 ) | b;
}

Real secondsSince( UnsignedInt now, UnsignedInt then )
{
	return (Real)(Int)( now - then ) / 1000.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE ATLAS
///////////////////////////////////////////////////////////////////////////////////////////////////

enum Piece
{
	P_BAND_INLAY, P_BAND_SHELL, P_PING, P_FOOT,
	P_PENNANT,
	P_COUNT = P_PENNANT + FINISH_COUNT
};

const char *const PIECE_NAMES[ P_COUNT ] =
{
	"RallyBandInlay", "RallyBandShell", "RallyPing", "RallyFoot",
	"RallyPennantAmerica", "RallyPennantChina", "RallyPennantGla", "RallyPennantTurkiye",
};

struct Atlas
{
	Bool tried;
	Bool ok;
	TextureClass *texture;
	Real u0[ P_COUNT ], v0[ P_COUNT ], u1[ P_COUNT ], v1[ P_COUNT ];
};
Atlas theAtlas;

const char *const ATLAS_FILE = "Art\\Textures\\ReforgedRallyMarkers.dds";

/** Every piece's rect out of the MappedImage blocks and the sheet with its four levels, never
	* reduced by Texture Quality, clamped.  Tried once; without any of it the rally point stays EA's. */
Bool loadAtlasOnce( void )
{
	if( TheMappedImageCollection == NULL || TheFileSystem == NULL )
		return FALSE;
	if( !TheFileSystem->doesFileExist( ATLAS_FILE ) )
	{
		DEBUG_LOG(( "RALLYMARKERS: no %s, the rally point stays EA's\n", ATLAS_FILE ));
		return FALSE;
	}
	AsciiString file;
	for( Int p = 0; p < P_COUNT; ++p )
	{
		const Image *image = TheMappedImageCollection->findImageByName( AsciiString( PIECE_NAMES[ p ] ) );
		if( image == NULL )
		{
			DEBUG_LOG(( "RALLYMARKERS: no piece %s in the mapped images, the rally point stays EA's\n", PIECE_NAMES[ p ] ));
			return FALSE;
		}
		const Region2D *uv = image->getUV();
		theAtlas.u0[ p ] = uv->lo.x;
		theAtlas.v0[ p ] = uv->lo.y;
		theAtlas.u1[ p ] = uv->hi.x;
		theAtlas.v1[ p ] = uv->hi.y;
		if( p == 0 )
			file = image->getFilename();
	}
	theAtlas.texture = WW3DAssetManager::Get_Instance()->Get_Texture( file.str(), MIP_LEVELS_4, WW3D_FORMAT_UNKNOWN,
																																		 true, TextureBaseClass::TEX_REGULAR, false );
	if( theAtlas.texture == NULL )
		return FALSE;
	if( theAtlas.texture->Is_Initialized() && theAtlas.texture->Is_Missing_Texture() )
	{
		DEBUG_LOG(( "RALLYMARKERS: %s loads as the missing texture, the rally point stays EA's\n", file.str() ));
		return FALSE;
	}
	theAtlas.texture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	theAtlas.texture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	return TRUE;
}

Bool loadAtlas( void )
{
	if( theAtlas.tried )
		return theAtlas.ok;
	theAtlas.tried = TRUE;
	theAtlas.ok = loadAtlasOnce();
	if( !theAtlas.ok )
		REF_PTR_RELEASE( theAtlas.texture );
	InGameUI::setRallyMarkersLoaded( theAtlas.ok );
	return theAtlas.ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE GROUND
///////////////////////////////////////////////////////////////////////////////////////////////////

Real groundHeight( Real x, Real y )
{
	if( TheTerrainRenderObject )
		return TheTerrainRenderObject->getDrawnHeight( x, y );
	return TheTerrainLogic->getGroundHeight( x, y );
}

/** Heights taken at the sync point, bilinear between the samples at draw time (the ground markers'
	* HeightGrid): axis a is (ax, ay), axis b its left; point (i, j) is the origin plus i steps along a
	* and j along b.  A water grid holds the surface where water stands over the ground, NO_WATER elsewhere. */
struct RallyGrid
{
	Real ox, oy, ax, ay, stepA, stepB;
	Int na, nb;
	std::vector<Real> h;

	RallyGrid() : ox( 0.0f ), oy( 0.0f ), ax( 1.0f ), ay( 0.0f ), stepA( 1.0f ), stepB( 1.0f ), na( 0 ), nb( 0 ) { }

	Bool valid( void ) const { return na > 1 && nb > 1; }

	/// with `bridges` a healthy bridge's deck where one spans the point, so a rail runs over it, not under it
	Bool fill( Real x0, Real y0, Real dirX, Real dirY, Real sa, Int countA, Real sb, Int countB, Bool water, Bool bridges = FALSE )
	{
		ox = x0;  oy = y0;  ax = dirX;  ay = dirY;
		stepA = MAX( 0.5f, sa );  stepB = MAX( 0.5f, sb );
		na = MAX( 2, MIN( 400, countA ) );  nb = MAX( 2, MIN( 400, countB ) );
		h.resize( na * nb );
		Bool wet = FALSE;
		for( Int j = 0; j < nb; ++j )
			for( Int i = 0; i < na; ++i )
			{
				const Real x = ox + ax * stepA * i - ay * stepB * j;
				const Real y = oy + ay * stepA * i + ax * stepB * j;
				if( water )
				{
					Real z;
					const Bool on = InGameUI::isOrderMarkOnWater( x, y, &z );
					h[ j * na + i ] = on ? z : NO_WATER;
					wet = wet || on;
				}
				else
				{
					Real z = groundHeight( x, y );
					if( bridges )
					{
						// the deck itself: getHighestLayerForDestination answers only for a point already on top
						Coord3D at;
						at.set( x, y, z );
						Bridge *bridge = TheTerrainLogic->findBridgeAt( &at );
						if( bridge != NULL && bridge->peekBridgeInfo()->curDamageState != BODY_RUBBLE )
							z = MAX( z, bridge->getBridgeHeight( &at, NULL ) );
					}
					h[ j * na + i ] = z;
				}
			}
		return wet;
	}

	void local( Real x, Real y, Real &fa, Real &fb ) const
	{
		const Real dx = x - ox, dy = y - oy;
		fa = ( dx * ax + dy * ay ) / stepA;
		fb = ( -dx * ay + dy * ax ) / stepB;
	}

	Bool contains( Real x, Real y ) const
	{
		if( !valid() )
			return FALSE;
		Real fa, fb;
		local( x, y, fa, fb );
		return fa >= 0.0f && fb >= 0.0f && fa <= (Real)( na - 1 ) && fb <= (Real)( nb - 1 );
	}

	Real nearest( Real x, Real y ) const
	{
		Real fa, fb;
		local( x, y, fa, fb );
		const Int i = MAX( 0, MIN( na - 1, (Int)floorf( fa + 0.5f ) ) );
		const Int j = MAX( 0, MIN( nb - 1, (Int)floorf( fb + 0.5f ) ) );
		return h[ j * na + i ];
	}

	Real at( Real x, Real y ) const
	{
		Real fa, fb;
		local( x, y, fa, fb );
		fa = MAX( 0.0f, MIN( (Real)( na - 1 ), fa ) );
		fb = MAX( 0.0f, MIN( (Real)( nb - 1 ), fb ) );
		const Int i = MIN( na - 2, (Int)floorf( fa ) );
		const Int j = MIN( nb - 2, (Int)floorf( fb ) );
		const Real s = fa - (Real)i, t = fb - (Real)j;
		const Real *row0 = &h[ j * na ];
		const Real *row1 = row0 + na;
		const Real top = row0[ i ] + s * ( row0[ i + 1 ] - row0[ i ] );
		const Real bottom = row1[ i ] + s * ( row1[ i + 1 ] - row1[ i ] );
		return top + t * ( bottom - top );
	}
};

/// the ground and the water over one stretch of what is drawn
struct Patch
{
	RallyGrid ground, water;
	Bool wet;
	Patch() : wet( FALSE ) { }
};

/// a square round (cx, cy) on the world's lattice, so a point taken again samples the same heights
void fillSquare( Patch &p, Real cx, Real cy, Real reach )
{
	// coarser steps rather than more of them for a wide reach: at most 161 a side
	const Real step = MAX( GROUND_STEP, 2.0f * reach / 158.0f );
	const Real x0 = floorf( ( cx - reach ) / step ) * step;
	const Real y0 = floorf( ( cy - reach ) / step ) * step;
	const Int n = (Int)ceilf( ( cx + reach - x0 ) / step ) + 1;
	p.ground.fill( x0, y0, 1.0f, 0.0f, step, n, step, n, FALSE );
	const Real ws = 2.0f * step;
	const Real wx0 = floorf( ( cx - reach ) / ws ) * ws;
	const Real wy0 = floorf( ( cy - reach ) / ws ) * ws;
	const Int wn = (Int)ceilf( ( cx + reach - wx0 ) / ws ) + 1;
	p.wet = p.water.fill( wx0, wy0, 1.0f, 0.0f, ws, wn, ws, wn, TRUE );
	if( !p.wet )
		p.water = RallyGrid();
}

/// a band along the segment a to b, `half` either side of it
void fillAlong( Patch &p, Real ax, Real ay, Real bx, Real by, Real half )
{
	const Real dx = bx - ax, dy = by - ay;
	const Real len = MAX( 1.0e-3f, (Real)sqrt( dx * dx + dy * dy ) );
	const Real ux = dx / len, uy = dy / len;
	const Real pad = half;
	const Real x0 = ax - ux * pad + uy * half;		// minus the left
	const Real y0 = ay - uy * pad - ux * half;
	// a grid holds 400 samples a side: a rail across the map spaces them out rather than run off the grid
	const Real step = MAX( SAMPLE_STEP, ( len + 2.0f * pad ) / 398.0f );
	const Int na = (Int)ceilf( ( len + 2.0f * pad ) / step ) + 1;
	const Int nb = (Int)ceilf( 2.0f * half / SAMPLE_STEP ) + 1;
	p.ground.fill( x0, y0, ux, uy, step, na, SAMPLE_STEP, nb, FALSE, TRUE );
	p.wet = p.water.fill( x0, y0, ux, uy, 2.0f * step, na / 2 + 2, 2.0f * SAMPLE_STEP, nb / 2 + 2, TRUE );
	if( !p.wet )
		p.water = RallyGrid();
}

/// how the local player's shroud dims a point there: clear, fogged or black
Real shroudShade( Real x, Real y )
{
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( local == NULL || ThePartitionManager == NULL )
		return 1.0f;
	Coord3D at;
	at.set( x, y, 0.0f );
	switch( ThePartitionManager->getShroudStatusForPlayer( local->getPlayerIndex(), &at ) )
	{
		case CELLSHROUD_FOGGED:		return FOG_SHADE;
		case CELLSHROUD_SHROUDED:	return SHROUD_SHADE;
		default:									return 1.0f;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// WHAT THE TAKE KEEPS
///////////////////////////////////////////////////////////////////////////////////////////////////

/// a building handed over: where its rally point stood and when it moved
struct Seen
{
	ObjectID id;
	Real x, y;
	UnsignedInt firstMs, clickMs, lastMs;
	Bool hadRally;
};

/// a rail: the path as handed over (door first), its ground, and how it is drawn
struct Rail
{
	std::vector<Real> x, y;
	Real length;
	std::vector<Patch> patches;
	std::vector<Real> shade;			///< every GROUND_STEP along it
	Tint tint;
	UnsignedInt growMs;
	UnsignedInt clickMs;					///< 0, or the click its chevrons rush after
	Real alpha;
	Real stop;										///< world units short of the end it stops
	Bool wet;
};

/// a ring, with its flag unless a ghost
struct Beacon
{
	Real x, y, z;
	Bool deck;
	Int finish;
	Tint tint;
	UnsignedInt clickMs;			///< 0, or the click it snapped on
	Real shade;
	Real alpha;
	Real scale;
	Bool ghost;
	Bool flag;
	Patch patch;
};

/// what W3DWaypointBuffer handed over this sync point
struct Handed
{
	ObjectID id;
	std::vector<Real> x, y, z;
	Bool hasRally;
	Bool placement;
	Int finish;
	Tint tint;
};

std::vector<Handed> s_handed;
std::vector<Seen> s_seen;
std::vector<Rail> s_rails;
std::vector<Beacon> s_beacons;
const Real s_k = K_WORLD;
Real s_zoom = 1.0f;			///< the camera's at the take, for the depth pull alone
Bool s_taken = FALSE;
Bool s_wet = FALSE;

Tint tintOf( const Vector3 &c )
{
	Tint t = { c.X, c.Y, c.Z };
	return t;
}

Seen *findSeen( ObjectID id )
{
	for( size_t i = 0; i < s_seen.size(); ++i )
		if( s_seen[ i ].id == id )
			return &s_seen[ i ];
	return NULL;
}

/// a rail along the given points, its ground and its shade along it taken now
void makeRail( const std::vector<Real> &x, const std::vector<Real> &y, const Tint &tint, UnsignedInt growMs,
							 UnsignedInt clickMs, Real alpha, Real stop )
{
	if( x.size() < 2 )
		return;
	Rail r;
	r.x = x;
	r.y = y;
	r.length = 0.0f;
	for( size_t i = 0; i + 1 < x.size(); ++i )
		r.length += (Real)sqrt( ( x[ i + 1 ] - x[ i ] ) * ( x[ i + 1 ] - x[ i ] ) + ( y[ i + 1 ] - y[ i ] ) * ( y[ i + 1 ] - y[ i ] ) );
	if( r.length <= stop + 1.0f )
		return;
	r.tint = tint;
	r.growMs = growMs;
	r.clickMs = clickMs;
	r.alpha = alpha;
	r.stop = stop;
	r.wet = FALSE;
	// a dash held to its screen width is at most DASH_ACROSS / FS_MIN across on the ground, its cap a dark line out
	const Real half = ( 0.5f * DASH_ACROSS / FS_MIN + KEY ) * s_k + 2.0f;
	for( size_t i = 0; i + 1 < x.size(); ++i )
	{
		r.patches.push_back( Patch() );
		fillAlong( r.patches.back(), x[ i ], y[ i ], x[ i + 1 ], y[ i + 1 ], half );
		r.wet = r.wet || r.patches.back().wet;
	}
	// ponytail: one shroud lookup a terrain cell along the rail each sync point; keep them per building
	// if a whole base selected ever shows up in the sync point's cost
	for( Real s = 0.0f; s <= r.length + GROUND_STEP; s += GROUND_STEP )
	{
		Real run = 0.0f, px = x.back(), py = y.back();
		for( size_t i = 0; i + 1 < x.size(); ++i )
		{
			const Real dx = x[ i + 1 ] - x[ i ], dy = y[ i + 1 ] - y[ i ];
			const Real l = (Real)sqrt( dx * dx + dy * dy );
			if( run + l >= s )
			{
				const Real f = l > 1.0e-3f ? ( s - run ) / l : 0.0f;
				px = x[ i ] + dx * f;
				py = y[ i ] + dy * f;
				break;
			}
			run += l;
		}
		r.shade.push_back( shroudShade( px, py ) );
	}
	s_wet = s_wet || r.wet;
	s_rails.push_back( r );
}

void makeBeacon( Real x, Real y, Real z, Int finish, const Tint &tint, UnsignedInt clickMs, Real alpha,
								 Real scale, Bool ghost, Bool flag )
{
	Beacon b;
	b.x = x;
	b.y = y;
	const Real ground = groundHeight( x, y );
	b.deck = z > ground + 2.0f;		// a point well over the ground is a bridge's deck: flat there
	b.z = b.deck ? z : ground;
	b.finish = MAX( 0, MIN( (Int)FINISH_COUNT - 1, finish ) );
	b.tint = tint;
	b.clickMs = clickMs;
	b.shade = shroudShade( x, y );
	b.alpha = alpha;
	b.scale = scale;
	b.ghost = ghost;
	b.flag = flag;
	// the ring grows by up to SNAP_GROW on a click (drawRing); the flag never does
	if( !b.deck )
		fillSquare( b.patch, x, y, MAX( BEACON_REACH * ( 1.0f + SNAP_GROW ), flag ? FLAG_REACH : 0.0f )
														* s_k * MAX( 1.0f, scale ) );
	s_wet = s_wet || b.patch.wet;
	s_beacons.push_back( b );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE QUADS
///////////////////////////////////////////////////////////////////////////////////////////////////

/** The ground markers' batcher without the shroud's coordinates: quads kept until flush, drawn in
	* batches of 4096 off the shared dynamic buffers. */
class RallyQuads
{
public:
	RallyQuads( void ) : m_pull( 0.0f ) { }

	void begin( const Vector3 &eye, Real pull )
	{
		m_verts.clear();
		m_eye = eye;
		m_pull = pull;
	}

	void add( const Vector3 *p, const Real *u, const Real *v, const UnsignedInt *c )
	{
		for( Int i = 0; i < 4; ++i )
		{
			Vert out;
			out.pos = p[ i ];
			out.diffuse = c[ i ];
			out.u = u[ i ];
			out.v = v[ i ];
			m_verts.push_back( out );
		}
	}

	Bool empty( void ) const { return m_verts.empty(); }

	void flush( void )
	{
		const Int total = (Int)m_verts.size() / 4;
		for( Int first = 0; first < total; first += BATCH_QUADS )
			drawBatch( first, MIN( (Int)BATCH_QUADS, total - first ) );
		m_verts.clear();
	}

private:
	enum { BATCH_QUADS = 4096 };
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
		Real u, v;
	};

	void drawBatch( Int first, Int quads )
	{
		DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, quads * 4 );
		DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, quads * 6 );
		{
			DynamicVBAccessClass::WriteLockClass vbLock( &vbAccess );
			DynamicIBAccessClass::WriteLockClass ibLock( &ibAccess );
			VertexFormatXYZNDUV2 *vb = vbLock.Get_Formatted_Vertex_Array();
			UnsignedShort *ib = ibLock.Get_Index_Array();
			if( vb == NULL || ib == NULL )
				return;
			for( Int q = 0; q < quads; ++q )
			{
				for( Int i = 0; i < 4; ++i, ++vb )
				{
					const Vert &src = m_verts[ ( first + q ) * 4 + i ];
					Vector3 pos = src.pos;
					Vector3 toEye = m_eye - pos;
					const Real distance = toEye.Length();
					if( m_pull > 0.0f && distance > 2.0f * m_pull )
						pos += toEye * ( m_pull / distance );		// along the ray to the eye: only the depth moves
					vb->x = pos.X;
					vb->y = pos.Y;
					vb->z = pos.Z;
					vb->nx = 0.0f;
					vb->ny = 0.0f;
					vb->nz = 1.0f;
					vb->diffuse = src.diffuse;
					vb->u1 = src.u;
					vb->v1 = src.v;
					vb->u2 = 0.0f;
					vb->v2 = 0.0f;
				}
				*ib++ = (UnsignedShort)( q * 4 );
				*ib++ = (UnsignedShort)( q * 4 + 1 );
				*ib++ = (UnsignedShort)( q * 4 + 2 );
				*ib++ = (UnsignedShort)( q * 4 );
				*ib++ = (UnsignedShort)( q * 4 + 2 );
				*ib++ = (UnsignedShort)( q * 4 + 3 );
			}
		}
		DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
		DX8Wrapper::Set_Vertex_Buffer( vbAccess );
		DX8Wrapper::Draw_Triangles( 0, quads * 2, 0, quads * 4 );
	}

	std::vector<Vert> m_verts;
	Vector3 m_eye;
	Real m_pull;
};

RallyQuads theQuads;
RallyQuads theAddQuads;

#define SC_RALLY_MARKER ( SHADE_CNST( ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE ) )

void setState( Bool additive )
{
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	DX8Wrapper::Set_Texture( 0, theAtlas.texture );
	DX8Wrapper::Set_Texture( 1, NULL );
	ShaderClass shader( SC_RALLY_MARKER );
	if( additive )
		shader.Set_Dst_Blend_Func( ShaderClass::DSTBLEND_ONE );
	DX8Wrapper::Set_Shader( shader );
	DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( 1 ) );
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
}

/// this picture's camera on the ground, the light, and the ground of what is laid now
struct DrawCtx
{
	UnsignedInt now;
	Real rx, ry;				///< the screen's right on the ground
	Real ux, uy;				///< and its up
	Real yaw;
	Real fs;						///< how much the ground towards the screen's top is foreshortened, FS_MIN to 1
	Real lgx, lgy;			///< the HUD's light, from the top left of the screen
	const Patch *patches;
	Int patchCount;
	Real flatZ;
	Bool waterPass;
	Bool additive;
};
DrawCtx s_ctx;

/** The looping animations' clock: the wall clock wrapped every 1000 s, so it stays small and positive.
	* Clock_Milliseconds counts from boot, and taken whole into a float it stepped at days of uptime and went
	* negative past 24.8 days, where pow( sin, 0.7 ) made NaN.  The wrap costs one jump every 16 minutes. */
Real animationSeconds( void )
{
	return (Real)( s_ctx.now % 1000000u ) / 1000.0f;
}

Vector3 groundPoint( Real x, Real y, Bool &onWater )
{
	Real z = s_ctx.flatZ;
	const RallyGrid *water = NULL;
	Bool found = FALSE;
	for( Int i = 0; i < s_ctx.patchCount; ++i )
	{
		const Patch &p = s_ctx.patches[ i ];
		if( !found && p.ground.contains( x, y ) )
		{
			z = p.ground.at( x, y );
			found = TRUE;
		}
		if( water == NULL && p.wet && p.water.contains( x, y ) )
			water = &p.water;
	}
	if( !found && s_ctx.patchCount > 0 && s_ctx.patches[ 0 ].ground.valid() )
		z = s_ctx.patches[ 0 ].ground.at( x, y );
	onWater = FALSE;
	if( water )
	{
		const Real surface = water->nearest( x, y );
		if( surface > z )
		{
			z = surface;
			onWater = TRUE;
		}
	}
	return Vector3( x, y, z + LIFT );
}

Real lightOf( Real nx, Real ny )
{
	return 0.5f + 0.5f * ( nx * s_ctx.lgx + ny * s_ctx.lgy );
}

/// one quad of a piece: corners on the ground, (pu, pv) in the piece's own 0..1; a quad with two
/// corners or more on water goes in the pass after the water
void emit( Int piece, const Real *x, const Real *y, const Real *pu, const Real *pv, const UnsignedInt *col )
{
	Vector3 p[ 4 ];
	Real u[ 4 ], v[ 4 ];
	Int wet = 0;
	for( Int i = 0; i < 4; ++i )
	{
		Bool onWater;
		p[ i ] = groundPoint( x[ i ], y[ i ], onWater );
		if( onWater )
			++wet;
	}
	if( ( wet >= 2 ) != ( s_ctx.waterPass != FALSE ) )
		return;
	const Real u0 = theAtlas.u0[ piece ], du = theAtlas.u1[ piece ] - u0;
	const Real v0 = theAtlas.v0[ piece ], dv = theAtlas.v1[ piece ] - v0;
	for( Int i = 0; i < 4; ++i )
	{
		u[ i ] = u0 + pu[ i ] * du;
		v[ i ] = v0 + pv[ i ] * dv;
	}
	( s_ctx.additive ? theAddQuads : theQuads ).add( p, u, v, col );
}

/** A quad piece sx by sy world units, its +u along `ang`, +v the heading's right, its anchor (au, av)
	* on (cx, cy), cut into squares of GROUND_STEP at most so it lies on the ground it crosses. */
void quadPiece( Int piece, Real cx, Real cy, Real ang, Real sx, Real sy, const Tint &tint, Real alpha,
								Real au = 0.5f, Real av = 0.5f )
{
	if( alpha <= 0.002f || sx <= 0.0f || sy <= 0.0f )
		return;
	const Real hx = (Real)cos( ang ), hy = (Real)sin( ang );
	const Real dx = hy, dy = -hx;
	const Int nu = MAX( 1, MIN( 16, (Int)ceilf( sx / GROUND_STEP ) ) );
	const Int nv = MAX( 1, MIN( 16, (Int)ceilf( sy / GROUND_STEP ) ) );
	const UnsignedInt c = argb( tint, alpha );
	const UnsignedInt col[ 4 ] = { c, c, c, c };
	for( Int j = 0; j < nv; ++j )
		for( Int i = 0; i < nu; ++i )
		{
			const Real pu0 = (Real)i / (Real)nu, pu1 = (Real)( i + 1 ) / (Real)nu;
			const Real pv0 = (Real)j / (Real)nv, pv1 = (Real)( j + 1 ) / (Real)nv;
			const Real pu[ 4 ] = { pu0, pu1, pu1, pu0 };
			const Real pv[ 4 ] = { pv0, pv0, pv1, pv1 };
			Real x[ 4 ], y[ 4 ];
			for( Int k = 0; k < 4; ++k )
			{
				const Real a = ( pu[ k ] - au ) * sx;
				const Real b = ( pv[ k ] - av ) * sy;
				x[ k ] = cx + hx * a + dx * b;
				y[ k ] = cy + hy * a + dy * b;
			}
			emit( piece, x, y, pu, pv, col );
		}
}

/// an upright piece: +u along the screen's right, its texture's top up the screen
void uprightPiece( Int piece, Real cx, Real cy, Real sx, Real sy, const Tint &tint, Real alpha, Real au = 0.5f, Real av = 0.5f )
{
	quadPiece( piece, cx, cy, s_ctx.yaw, sx, sy, tint, alpha, au, av );
}

/** A band along a polyline, row 0 on the right of the way it runs (outside a counter-clockwise
	* polygon), its corners mitred so it stays one band, each edge cut into quads of GROUND_STEP at most.
	* `across` is its width on the screen in world units: the part of each offset that runs towards the
	* screen's top is drawn 1/fs long, so the ground's foreshortening gives it back and a ring's top and
	* bottom stay as thick as its sides.  colourAt( s, light ) gives a quad's colour from how far along the
	* line its middle is and how much of the HUD's light its edge's right normal takes. */
template< class ColourAt >
void polyStrip( Int piece, const Real *px, const Real *py, Int n, Bool closed, Real across, const ColourAt &colourAt )
{
	if( n < 2 )
		return;
	enum { MAX_POINTS = 32 };
	n = MIN( n, (Int)MAX_POINTS );
	Real ox[ MAX_POINTS ], oy[ MAX_POINTS ];
	const Real half = 0.5f * across;
	for( Int i = 0; i < n; ++i )
	{
		const Bool hasIn = closed || i > 0;
		const Bool hasOut = closed || i < n - 1;
		const Int ip = ( i + n - 1 ) % n, in = ( i + 1 ) % n;
		Real ix = 0.0f, iy = 0.0f, qx = 0.0f, qy = 0.0f;
		if( hasIn )
		{
			const Real dx = px[ i ] - px[ ip ], dy = py[ i ] - py[ ip ];
			const Real l = MAX( 1.0e-4f, (Real)sqrt( dx * dx + dy * dy ) );
			ix = dy / l;  iy = -dx / l;
		}
		if( hasOut )
		{
			const Real dx = px[ in ] - px[ i ], dy = py[ in ] - py[ i ];
			const Real l = MAX( 1.0e-4f, (Real)sqrt( dx * dx + dy * dy ) );
			qx = dy / l;  qy = -dx / l;
		}
		if( !hasIn )
		{
			ix = qx;  iy = qy;
		}
		if( !hasOut )
		{
			qx = ix;  qy = iy;
		}
		Real nx = ix + qx, ny = iy + qy;
		const Real nl = (Real)sqrt( nx * nx + ny * ny );
		if( nl < 1.0e-4f )
		{
			nx = qx;  ny = qy;
		}
		else
		{
			nx /= nl;  ny /= nl;
		}
		const Real cosine = MAX( 0.35f, nx * qx + ny * qy );
		const Real wx = nx * half / cosine, wy = ny * half / cosine;
		const Real along = wx * s_ctx.rx + wy * s_ctx.ry;
		const Real up = ( wx * s_ctx.ux + wy * s_ctx.uy ) / s_ctx.fs;
		ox[ i ] = along * s_ctx.rx + up * s_ctx.ux;
		oy[ i ] = along * s_ctx.ry + up * s_ctx.uy;
	}
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	const Int edges = closed ? n : n - 1;
	Real run = 0.0f;
	for( Int e = 0; e < edges; ++e )
	{
		const Int a = e, b = ( e + 1 ) % n;
		const Real dx = px[ b ] - px[ a ], dy = py[ b ] - py[ a ];
		const Real len = (Real)sqrt( dx * dx + dy * dy );
		if( len < 1.0e-3f )
			continue;
		const Real light = lightOf( dy / len, -dx / len );
		const Int m = MAX( 1, MIN( 1000, (Int)ceilf( len / GROUND_STEP ) ) );		// 10000 units: past any map's diagonal
		for( Int j = 0; j < m; ++j )
		{
			const Real f0 = (Real)j / (Real)m, f1 = (Real)( j + 1 ) / (Real)m;
			const UnsignedInt c = colourAt( run + len * 0.5f * ( f0 + f1 ), light );
			if( ( c >> 24 ) == 0 )
				continue;
			const UnsignedInt col[ 4 ] = { c, c, c, c };
			const Real cx0 = px[ a ] + dx * f0, cy0 = py[ a ] + dy * f0;
			const Real cx1 = px[ a ] + dx * f1, cy1 = py[ a ] + dy * f1;
			const Real o0x = ox[ a ] + ( ox[ b ] - ox[ a ] ) * f0, o0y = oy[ a ] + ( oy[ b ] - oy[ a ] ) * f0;
			const Real o1x = ox[ a ] + ( ox[ b ] - ox[ a ] ) * f1, o1y = oy[ a ] + ( oy[ b ] - oy[ a ] ) * f1;
			const Real x[ 4 ] = { cx0 + o0x, cx1 + o1x, cx1 - o1x, cx0 - o0x };
			const Real y[ 4 ] = { cy0 + o0y, cy1 + o1y, cy1 - o1y, cy0 - o0y };
			emit( piece, x, y, pu, pv, col );
		}
		run += len;
	}
}

/// one colour the whole way, lit or not
struct FlatColour
{
	Tint tint;
	Real alpha;
	Bool lit;
	FlatColour( const Tint &t, Real a, Bool l ) : tint( t ), alpha( a ), lit( l ) { }
	UnsignedInt operator()( Real, Real light ) const { return argb( tint, alpha * ( lit ? light : 1.0f ) ); }
};

/// a thin additive ring growing and fading: the click's ping
void pingRing( Real cx, Real cy, Real r, Real across, const Tint &tint, Real alpha )
{
	if( alpha <= 0.002f || r <= 0.0f )
		return;
	const Int n = MAX( 24, MIN( 240, (Int)ceilf( 2.0f * RALLY_PI * r / GROUND_STEP ) ) );
	const Real ro = r + 0.5f * across, ri = MAX( 0.0f, r - 0.5f * across );
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	const UnsignedInt c = argb( tint, alpha );
	const UnsignedInt col[ 4 ] = { c, c, c, c };
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	for( Int i = 0; i < n; ++i )
	{
		const Real ta = 2.0f * RALLY_PI * (Real)i / (Real)n, tb = 2.0f * RALLY_PI * (Real)( i + 1 ) / (Real)n;
		const Real ca = (Real)cos( ta ), sa = (Real)sin( ta ), cb = (Real)cos( tb ), sb = (Real)sin( tb );
		const Real x[ 4 ] = { cx + ro * ca, cx + ro * cb, cx + ri * cb, cx + ri * ca };
		const Real y[ 4 ] = { cy + ro * sa, cy + ro * sb, cy + ri * sb, cy + ri * sa };
		emit( P_PING, x, y, pu, pv, col );
	}
	s_ctx.additive = was;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE DRAW
///////////////////////////////////////////////////////////////////////////////////////////////////

/** The rail from a to b world units along it into px, py, the path's corners between kept: one dash.
	* Either end past the rail's own is held to it. */
Int railSpan( const Rail &r, Real a, Real b, Real *px, Real *py, Int maxPoints )
{
	Int n = 0;
	Real run = 0.0f;
	for( size_t i = 0; i + 1 < r.x.size() && n < maxPoints; ++i )
	{
		const Real dx = r.x[ i + 1 ] - r.x[ i ], dy = r.y[ i + 1 ] - r.y[ i ];
		const Real l = (Real)sqrt( dx * dx + dy * dy );
		const Real s0 = run;
		run += l;
		const Bool last = i + 2 >= r.x.size();
		if( l < 1.0e-3f || ( run < a && !last ) )
			continue;
		if( n == 0 )
		{
			const Real f = clamp01( ( a - s0 ) / l );
			px[ n ] = r.x[ i ] + dx * f;
			py[ n ] = r.y[ i ] + dy * f;
			++n;
		}
		if( run >= b || last )
		{
			const Real f = clamp01( ( b - s0 ) / l );
			px[ n ] = r.x[ i ] + dx * f;
			py[ n ] = r.y[ i ] + dy * f;
			return n + 1;
		}
		px[ n ] = r.x[ i + 1 ];
		py[ n ] = r.y[ i + 1 ];
		++n;
	}
	return n;
}

Real railShade( const Rail &r, Real s )
{
	if( r.shade.empty() )
		return 1.0f;
	const Int i = MAX( 0, MIN( (Int)r.shade.size() - 1, (Int)( s / GROUND_STEP + 0.5f ) ) );
	return r.shade[ i ];
}

/** The rail: dashes of the owner's colour in their dark line, from the door to just off the ring, creeping
	* towards the point and running on after a click; it grows out of the door when new. */
void drawRail( const Rail &r )
{
	s_ctx.patches = r.patches.empty() ? NULL : &r.patches[ 0 ];
	s_ctx.patchCount = (Int)r.patches.size();
	s_ctx.flatZ = 0.0f;
	const Real k = s_k;
	const Real grow = easeOut( secondsSince( s_ctx.now, r.growMs ) / GROW );
	const Real visible = MIN( r.length - r.stop, grow * r.length );
	if( visible <= 0.0f )
		return;

	const Real t = animationSeconds();
	const Real tl = r.clickMs ? secondsSince( s_ctx.now, r.clickMs ) : 10.0f;
	const Real pitch = ( DASH_LEN + DASH_GAP ) * k;
	const Real travelled = ( DASH_SPEED * t + DASH_RUSH * MIN( 0.6f, MAX( 0.0f, tl ) ) / 0.6f ) * k;
	const FlatColour line( TINT_WHITE, 0.85f * r.alpha, FALSE );
	enum { MAX_POINTS = 8 };
	Real px[ MAX_POINTS ], py[ MAX_POINTS ];
	for( Real s = fmodf( travelled, pitch ) - pitch; s < visible; s += pitch )
	{
		const Real a = MAX( 0.0f, s ), b = MIN( visible, s + DASH_LEN * k );
		if( b - a < 0.25f * k )
			continue;
		// the dark line runs a line's width past either end, as round any of the cursors' strokes
		Int n = railSpan( r, a - KEY * k, b + KEY * k, px, py, MAX_POINTS );
		polyStrip( P_BAND_SHELL, px, py, n, FALSE, DASH_ACROSS * k, line );
		n = railSpan( r, a, b, px, py, MAX_POINTS );
		polyStrip( P_BAND_INLAY, px, py, n, FALSE, DASH_ACROSS * k,
							 FlatColour( scaled( r.tint, railShade( r, 0.5f * ( a + b ) ) ), r.alpha, FALSE ) );
	}
}

/// local frame coordinates (x right, y up the screen, in world units) to the ground
void toWorld( const Beacon &b, Real lx, Real ly, Real &wx, Real &wy )
{
	wx = b.x + lx * s_ctx.rx + ly * s_ctx.ux;
	wy = b.y + lx * s_ctx.ry + ly * s_ctx.uy;
}

/** The ring on the ground round the point, a circle there and so an ellipse on the screen, its stroke held
	* to one width all round; on a click it snaps in from larger, flashes and sends one faint ping out. */
void drawRing( const Beacon &b, Real tl )
{
	const Real k = s_k;
	const Real snap = ( tl >= 0.0f ) ? 1.0f + SNAP_GROW * ( 1.0f - easeOut( tl / SNAP ) ) : 1.0f;
	const Real radius = RING_R * snap * b.scale * k;
	const Real a = b.alpha * ( b.ghost ? 0.5f : 1.0f );
	const Real flash = ( tl >= 0.0f && tl < 0.5f ) ? 1.0f + 0.4f * ( 1.0f - tl / 0.5f ) : 1.0f;
	const Tint tint = scaled( b.tint, b.shade * flash );
	enum { SIDES = 28 };
	Real px[ SIDES ], py[ SIDES ];
	for( Int i = 0; i < SIDES; ++i )
	{
		const Real ang = 2.0f * RALLY_PI * (Real)i / (Real)SIDES;
		px[ i ] = b.x + radius * (Real)cos( ang );
		py[ i ] = b.y + radius * (Real)sin( ang );
	}
	const Real across = RING_ACROSS * b.scale * k;
	polyStrip( P_BAND_SHELL, px, py, SIDES, TRUE, across, FlatColour( TINT_WHITE, 0.85f * a, FALSE ) );
	polyStrip( P_BAND_INLAY, px, py, SIDES, TRUE, across, FlatColour( tint, a, FALSE ) );

	if( tl >= 0.0f && tl < 0.5f )
	{
		const Tint ping = { 0.5f * tint.r + 0.5f, 0.5f * tint.g + 0.5f, 0.5f * tint.b + 0.5f };
		const Real e = easeOut( tl / 0.5f );
		pingRing( b.x, b.y, FRAME_REACH * k * ( 1.0f + 0.9f * e ), across, ping, 0.6f * ( 1.0f - e ) );
	}
}

/** The hairline pole standing on the point and the side's pennant rippling off its top, upright on the
	* screen and held to their size there: on a click the pole drops in and the pennant unfurls. */
void drawFlag( const Beacon &b, Real t, Real tl )
{
	const Real k = s_k * b.scale;
	const Real up = k / s_ctx.fs;			// a k up the screen, on the ground
	const Real grow = tl >= 0.0f ? 0.4f + 0.6f * easeOut( tl / 0.18f ) : 1.0f;
	Real px[ 2 ], py[ 2 ];
	toWorld( b, 0.0f, -KEY * up, px[ 0 ], py[ 0 ] );
	toWorld( b, 0.0f, ( POLE_H * grow + KEY ) * up, px[ 1 ], py[ 1 ] );
	polyStrip( P_BAND_SHELL, px, py, 2, FALSE, POLE_ACROSS * k, FlatColour( TINT_WHITE, 0.85f * b.alpha, FALSE ) );
	px[ 0 ] = b.x;
	py[ 0 ] = b.y;
	toWorld( b, 0.0f, POLE_H * grow * up, px[ 1 ], py[ 1 ] );
	polyStrip( P_BAND_INLAY, px, py, 2, FALSE, POLE_ACROSS * k, FlatColour( scaled( TINT_WHITE, 0.78f * b.shade ), b.alpha, FALSE ) );

	// the hoist's top on the pole, then a mesh of columns rippling with a faint wave that runs to the fly
	Real hx, hy;
	toWorld( b, HOIST_X * k, HOIST_Y * grow * up, hx, hy );
	const Real unfurl = tl >= 0.0f ? MAX( 0.05f, easeBack( ( tl - 0.08f ) / 0.4f ) ) : 1.0f;
	enum { COLS = 8 };
	const Int rows = MAX( 1, MIN( 8, (Int)ceilf( PENNANT_H * up / GROUND_STEP ) ) );
	const Int piece = P_PENNANT + b.finish;
	const Tint tint = scaled( b.tint, b.shade );
	Real lx[ COLS + 1 ], wave[ COLS + 1 ];
	UnsignedInt colour[ COLS + 1 ];
	for( Int i = 0; i <= COLS; ++i )
	{
		const Real pu = (Real)i / (Real)COLS;
		lx[ i ] = ( pu - PENNANT_AU ) * PENNANT_W;
		const Real f = clamp01( lx[ i ] / ( PENNANT_W * ( 1.0f - PENNANT_AU ) ) );
		const Real phase = 2.0f * RALLY_PI * ( f * 1.1f - t / 0.85f );
		wave[ i ] = 0.5f * f * (Real)sin( phase );
		colour[ i ] = argb( scaled( tint, 0.93f + 0.07f * (Real)cos( phase ) ), b.alpha );
	}
	for( Int j = 0; j < rows; ++j )
	{
		const Real pv0 = (Real)j / (Real)rows, pv1 = (Real)( j + 1 ) / (Real)rows;
		for( Int i = 0; i < COLS; ++i )
		{
			const Real pu[ 4 ] = { (Real)i / COLS, (Real)( i + 1 ) / COLS, (Real)( i + 1 ) / COLS, (Real)i / COLS };
			const Real pv[ 4 ] = { pv0, pv0, pv1, pv1 };
			const Int col[ 4 ] = { i, i + 1, i + 1, i };
			Real x[ 4 ], y[ 4 ];
			UnsignedInt c[ 4 ];
			for( Int q = 0; q < 4; ++q )
			{
				const Real ax = lx[ col[ q ] ] * k * unfurl;
				const Real ay = ( -( pv[ q ] - PENNANT_AV ) * PENNANT_H + wave[ col[ q ] ] ) * up;
				x[ q ] = hx + ax * s_ctx.rx + ay * s_ctx.ux;
				y[ q ] = hy + ax * s_ctx.ry + ay * s_ctx.uy;
				c[ q ] = colour[ col[ q ] ];
			}
			emit( piece, x, y, pu, pv, c );
		}
	}
}

void drawBeacon( const Beacon &b )
{
	s_ctx.patches = b.deck ? NULL : &b.patch;
	s_ctx.patchCount = b.deck ? 0 : 1;
	s_ctx.flatZ = b.z;
	const Real t = animationSeconds();
	Real tl = b.clickMs ? secondsSince( s_ctx.now, b.clickMs ) : -1.0f;
	if( tl > 2.0f )
		tl = -1.0f;
	drawRing( b, tl );
	if( b.flag )
		drawFlag( b, t, tl );
	// the foot's white dot over the pole's foot, round on the screen; a ghost's marks where it aims
	const Real foot = FOOT * s_k * b.scale;
	uprightPiece( P_FOOT, b.x, b.y, foot, foot / s_ctx.fs, scaled( TINT_WHITE, b.shade ), b.alpha * ( b.ghost ? 0.5f : 1.0f ) );
}

void drawPass( Bool waterPass )
{
	if( !s_taken || !theAtlas.ok || ( s_rails.empty() && s_beacons.empty() ) )
		return;
	if( ShaderClass::Is_Backface_Culling_Inverted() )
		return;		// not in the water's mirror
	s_ctx.waterPass = waterPass;

	Matrix4x4 view;
	DX8Wrapper::Get_Transform( D3DTS_VIEW, view );
	Vector3 eye;
	for( Int j = 0; j < 3; ++j )
		eye[ j ] = -( view[ 0 ][ j ] * view[ 0 ][ 3 ] + view[ 1 ][ j ] * view[ 1 ][ 3 ] + view[ 2 ][ j ] * view[ 2 ][ 3 ] );
	Real rx = view[ 0 ][ 0 ], ry = view[ 0 ][ 1 ];
	const Real rl = (Real)sqrt( rx * rx + ry * ry );
	if( rl > 1.0e-4f )
	{
		rx /= rl;
		ry /= rl;
	}
	else
	{
		rx = 1.0f;
		ry = 0.0f;
	}
	s_ctx.now = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );
	s_ctx.rx = rx;
	s_ctx.ry = ry;
	s_ctx.ux = -ry;
	s_ctx.uy = rx;
	s_ctx.yaw = (Real)atan2( ry, rx );
	// the view's third row is the camera's axis along its sight: its height is the sine of how steeply the
	// camera looks down, which is how much the ground towards the screen's top is foreshortened
	s_ctx.fs = MAX( FS_MIN, MIN( 1.0f, (Real)fabs( view[ 2 ][ 2 ] ) ) );
	Real lgx = -0.5f * rx + 0.62f * -ry;
	Real lgy = -0.5f * ry + 0.62f * rx;
	const Real ll = (Real)sqrt( lgx * lgx + lgy * lgy );
	s_ctx.lgx = lgx / ll;
	s_ctx.lgy = lgy / ll;
	s_ctx.additive = FALSE;
	// the pull grows with the zoom as the order marks' does (W3DInGameUI's ORDER_MARK_PULL): the depth
	// buffer is coarser that far out, and a fixed pull let the ground bite into the mark's edges
	const Real pull = PULL * MAX( 1.0f, s_zoom );
	theQuads.begin( eye, pull );
	theAddQuads.begin( eye, pull );

	for( size_t i = 0; i < s_rails.size(); ++i )
		if( !waterPass || s_rails[ i ].wet )
			drawRail( s_rails[ i ] );
	for( size_t i = 0; i < s_beacons.size(); ++i )
		if( !waterPass || s_beacons[ i ].patch.wet )
			drawBeacon( s_beacons[ i ] );

	if( !theQuads.empty() )
	{
		setState( FALSE );
		theQuads.flush();
	}
	if( !theAddQuads.empty() )
	{
		setState( TRUE );
		theAddQuads.flush();
	}
	s_ctx.patches = NULL;
	s_ctx.patchCount = 0;
	s_ctx.waterPass = FALSE;
	DX8Wrapper::Set_Texture( 0, NULL );
	ShaderClass::Invalidate();
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE SYNC POINT
///////////////////////////////////////////////////////////////////////////////////////////////////

Bool W3DRallyMarkers_on( void )
{
	if( TheGlobalData == NULL || TheGlobalData->m_classicGraphics )
		return FALSE;
	if( !loadAtlas() )
		return FALSE;
	return TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
		&& TheGameLogic->getDrawIconUI() && !CinemaDirector_hidesHud();
}

Int W3DRallyMarkers_finishOf( const AsciiString &side )
{
	if( side.startsWithNoCase( "China" ) )
		return FINISH_CHINA;
	if( side.startsWithNoCase( "GLA" ) )
		return FINISH_GLA;
	if( side.startsWithNoCase( "Turkiye" ) )
		return FINISH_TURKIYE;
	return FINISH_AMERICA;
}

void W3DRallyMarkers_beginTake( void )
{
	s_handed.clear();
	s_taken = FALSE;
}

void W3DRallyMarkers_addPath( ObjectID building, const Vector3 *points, Int count, Bool hasRally, Int finish, const Vector3 &color )
{
	if( count < 1 )
		return;
	Handed h;
	h.id = building;
	for( Int i = 0; i < count; ++i )
	{
		h.x.push_back( points[ i ].X );
		h.y.push_back( points[ i ].Y );
		h.z.push_back( points[ i ].Z );
	}
	h.hasRally = hasRally;
	h.placement = FALSE;
	h.finish = finish;
	h.tint = tintOf( color );
	s_handed.push_back( h );
}

void W3DRallyMarkers_addPlacement( const Vector3 &door, const Vector3 &natural, Int finish, const Vector3 &color )
{
	Handed h;
	h.id = INVALID_ID;
	h.x.push_back( door.X );
	h.y.push_back( door.Y );
	h.z.push_back( door.Z );
	h.x.push_back( natural.X );
	h.y.push_back( natural.Y );
	h.z.push_back( natural.Z );
	h.hasRally = FALSE;
	h.placement = TRUE;
	h.finish = finish;
	h.tint = tintOf( color );
	s_handed.push_back( h );
}

void W3DRallyMarkers_endTake( void )
{
	// Classic switched on or off in a match: the control bar looks again, so EA's flag comes back or goes
	// with the selection as it stands (ControlBar::showRallyPoint asks InGameUI::rallyMarkersOn)
	static Int lastOn = -1;
	const Int on = InGameUI::rallyMarkersOn() ? 1 : 0;
	if( lastOn >= 0 && on != lastOn && TheControlBar != NULL )
		TheControlBar->markUIDirty();
	lastOn = on;

	s_rails.clear();
	s_beacons.clear();
	s_wet = FALSE;
	if( !W3DRallyMarkers_on() )
	{
		s_seen.clear();
		return;
	}
	const UnsignedInt now = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );
	s_zoom = TheTacticalView->getZoom();

	// each building's point: a new one grows its rail, a moved one is a click
	struct Point
	{
		Real x, y, z;
		Int finish;
		Tint tint;
		UnsignedInt clickMs;
	};
	std::vector<Point> points;
	for( size_t i = 0; i < s_handed.size(); ++i )
	{
		const Handed &h = s_handed[ i ];
		if( h.placement )
		{
			// the rail out of where the door will be, and a quiet frame past the natural rally point along
			// the way out: at the point itself the ghost's own model stood over most of it
			const Real scale = 0.8f;
			Real dx = h.x.back() - h.x[ 0 ], dy = h.y.back() - h.y[ 0 ];
			const Real len = (Real)sqrt( dx * dx + dy * dy );
			if( len > 1.0f )
			{
				dx /= len;
				dy /= len;
			}
			const Real push = ( FRAME_REACH + 6.0f ) * s_k * scale;
			std::vector<Real> px( h.x ), py( h.y );
			px.back() += dx * push;
			py.back() += dy * push;
			makeRail( px, py, h.tint, now - 1000u, 0, 0.6f, scale * RAIL_STOP * s_k );
			makeBeacon( px.back(), py.back(), h.z.back(), h.finish, h.tint, 0, 0.6f, scale, FALSE, FALSE );
			continue;
		}
		Seen *seen = findSeen( h.id );
		const Real rx = h.x.back(), ry = h.y.back();
		if( seen == NULL )
		{
			Seen s;
			s.id = h.id;
			s.x = rx;
			s.y = ry;
			s.firstMs = now;
			s.clickMs = 0;
			s.hadRally = h.hasRally;
			s_seen.push_back( s );
			seen = &s_seen.back();
		}
		else if( h.hasRally && ( !seen->hadRally || fabs( seen->x - rx ) > 1.0f || fabs( seen->y - ry ) > 1.0f ) )
			seen->clickMs = now;
		seen->x = rx;
		seen->y = ry;
		seen->hadRally = h.hasRally;
		seen->lastMs = now;
		if( !h.hasRally )
			continue;
		const UnsignedInt growMs = MAX( seen->firstMs, seen->clickMs );
		makeRail( h.x, h.y, h.tint, growMs, seen->clickMs, 1.0f, RAIL_STOP * s_k );

		// buildings sharing a point share its ring: their rails meeting there say how many
		Bool shared = FALSE;
		for( size_t p = 0; p < points.size() && !shared; ++p )
		{
			const Real dx = points[ p ].x - rx, dy = points[ p ].y - ry;
			if( dx * dx + dy * dy <= SHARE_DISTANCE * SHARE_DISTANCE )
			{
				points[ p ].clickMs = MAX( points[ p ].clickMs, seen->clickMs );
				shared = TRUE;
			}
		}
		if( !shared )
		{
			Point p = { rx, ry, h.z.back(), h.finish, h.tint, seen->clickMs };
			points.push_back( p );
		}
	}
	for( size_t p = 0; p < points.size(); ++p )
		makeBeacon( points[ p ].x, points[ p ].y, points[ p ].z, points[ p ].finish, points[ p ].tint,
								points[ p ].clickMs, 1.0f, 1.0f, FALSE, TRUE );

	// the cursor aiming a rally point: a ghost frame under it, ghost rails from each building's natural point
	if( TheMouse != NULL && TheMouse->getMouseCursor() == Mouse::SET_RALLY_POINT )
	{
		ICoord2D pixel = TheMouse->getMouseStatus()->pos;
		Coord3D aim;
		if( TheTacticalView->screenToTerrain( &pixel, &aim ) )
		{
			Int finish = FINISH_AMERICA;
			Tint tint = TINT_WHITE;
			Bool any = FALSE;
			for( size_t i = 0; i < s_handed.size(); ++i )
			{
				const Handed &h = s_handed[ i ];
				if( h.placement )
					continue;
				// the natural rally point: the last point without a rally, the second with one (the first on a helipad)
				const Int n = (Int)h.x.size();
				const Int nat = h.hasRally ? ( n >= 3 ? 1 : 0 ) : n - 1;
				std::vector<Real> gx, gy;
				gx.push_back( h.x[ nat ] );
				gy.push_back( h.y[ nat ] );
				gx.push_back( aim.x );
				gy.push_back( aim.y );
				makeRail( gx, gy, h.tint, now - 1000u, 0, 0.4f, RAIL_STOP * s_k );
				if( !any )
				{
					finish = h.finish;
					tint = h.tint;
					any = TRUE;
				}
			}
			if( any )
				makeBeacon( aim.x, aim.y, aim.z, finish, tint, 0, 1.0f, 1.0f, TRUE, FALSE );
		}
	}

	for( size_t i = 0; i < s_seen.size(); )
	{
		if( now - s_seen[ i ].lastMs > SEEN_KEEP_MS )
			s_seen.erase( s_seen.begin() + i );
		else
			++i;
	}
	s_taken = TRUE;
}

void W3DRallyMarkers_draw( void )
{
	drawPass( FALSE );
}

void W3DRallyMarkers_drawOnWater( void )
{
	if( s_wet )
		drawPass( TRUE );
}

void W3DRallyMarkers_reset( void )
{
	s_handed.clear();
	s_seen.clear();
	s_rails.clear();
	s_beacons.clear();
	s_taken = FALSE;
	s_wet = FALSE;
}
