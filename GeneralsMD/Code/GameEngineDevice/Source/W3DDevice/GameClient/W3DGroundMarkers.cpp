// The Machined Reticle ground markers (docs: the 2026-11-23 decals SPEC).  Every mark is a piece of
// one atlas, ReforgedGroundMarkers.dds, laid on the ground as textured quads in the terrain pass, the
// order marks' way (W3DInGameUI::drawOrderRings): depth tested with each vertex pulled towards the
// eye, never depth written, multiplied by the shroud's texture where the ground was.  One texture
// stage times the vertex colour, alpha blended, the ping additive, the shroud on a second stage:
// fixed-function on Direct3D 9, Direct3D 11 and SDL3 alike, and no program of its own.  Every motion
// is a per-piece transform, colour or alpha worked out here from Clock_Milliseconds.
//
// What is drawn is read at the sync point (W3DGroundMarkers_take, from W3DInGameUI::recordDraw):
// the aimed cursor and the clicks InGameUI kept, the planes, missiles and gunships those clicks sent,
// the selected guards, the move and attack clicks, and the ground under every one of them as a grid
// of heights.  Logic state is read there and never written, and no logic decision looks at any of it.
// The draw (W3DGroundMarkers_draw) runs beside the tick and reads only what the take left.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "W3DDevice/GameClient/W3DGroundMarkers.h"

#include <math.h>
#include <vector>

#include "Lib/Clock.h"
#include "WWMath/matrix3d.h"
#include "Common/AudioEventRTS.h"		// the module headers below hold these by value and leave them to PreRTS.h,
#include "GameClient/RadiusDecal.h"		// which no device file includes
#include "GameLogic/Damage.h"
#include "Common/Debug.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/AIGuard.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DeliverPayloadAIUpdate.h"
#include "GameLogic/Module/OCLSpecialPower.h"
#include "GameLogic/Module/ParticleUplinkCannonUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SpectreGunshipDeploymentUpdate.h"
#include "GameLogic/Module/SpectreGunshipUpdate.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// CONSTANTS
///////////////////////////////////////////////////////////////////////////////////////////////////

static const Real MARKER_PI = 3.14159265358979f;
static const Real MARKER_DEG = MARKER_PI / 180.0f;
static const Real MARKER_LIFT = 0.35f;					///< out of the dirt, as the order marks
static const Real MARKER_PULL = 3.0f;					///< towards the eye: the build grid's, whose lines also run ten units between two samples
static const Real GROUND_STEP = 10.0f;					///< the heights' lattice, a terrain cell; no quad laid on it is longer
static const Real WATER_STEP = 20.0f;					///< the water's, coarser: a shore is decided quad by quad
static const Real GRID_PAD = 60.0f;						///< a square is taken this much wider, so a moving cursor keeps it a while
static const Int MAX_GRID_SIDE = 161;
static const Real NO_WATER = -1.0e30f;
static const Real CLICK_K = 0.65f;						///< a move or attack click's band unit
static const Real CLICK_REACH = 60.0f;					///< its ring at its widest, chevrons included
static const UnsignedInt CLICK_LIFE_MS = 1500;		///< the dot's, the order ring's 1500 ms
static const UnsignedInt WHEEL_EASE_MS = 400;			///< a guard wheel step, as InGameUI::setRadiusCursor eases it
static const Real AREA_HOLD = 1.5f;						///< seconds an area power holds the snapped look after the click
static const Real AREA_FADE = 0.3f;
static const Real END_FADE = 0.5f;						///< a delivery's, a gunship's and a superweapon's last fade
static const Real PLANE_WAIT = 10.0f;					///< seconds a click waits for what it sent before it gives up
static const Real MISSILE_WAIT = 60.0f;
static const Real GUARD_LIFE = 5.0f;
static const Int MAX_SHOTS = 24;
static const Int MAX_GUARDS = 16;

///////////////////////////////////////////////////////////////////////////////////////////////////
// TINTS
///////////////////////////////////////////////////////////////////////////////////////////////////

struct Tint
{
	Real r, g, b;
};
static const Tint TINT_WHITE = { 1.00f, 1.00f, 1.00f };	///< steel pieces
static const Tint TINT_WARM = { 0.95f, 0.90f, 0.80f };	///< guard, move, friendly area powers
static const Tint TINT_GOLD = { 0.97f, 0.79f, 0.24f };	///< paradrops and drops
static const Tint TINT_AMBER = { 0.99f, 0.62f, 0.17f };	///< offensive area powers
static const Tint TINT_ORANGE = { 0.98f, 0.43f, 0.12f };	///< air strikes, Spectre
static const Tint TINT_RED = { 0.93f, 0.19f, 0.12f };		///< superweapons, attack

static Real clamp01( Real x )
{
	return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x );
}

static Real smoothstep( Real a, Real b, Real x )
{
	const Real t = clamp01( ( x - a ) / ( b - a ) );
	return t * t * ( 3.0f - 2.0f * t );
}

static Real easeOut( Real x )
{
	const Real r = 1.0f - clamp01( x );
	return 1.0f - r * r * r;
}

/// a tint brightened or dimmed, each channel clamped to one
static Tint scaled( const Tint &t, Real f )
{
	Tint o = { MIN( 1.0f, t.r * f ), MIN( 1.0f, t.g * f ), MIN( 1.0f, t.b * f ) };
	return o;
}

/// the ping's: half way to white
static Tint pingTintOf( const Tint &t )
{
	Tint o = { 0.5f * t.r + 0.5f, 0.5f * t.g + 0.5f, 0.5f * t.b + 0.5f };
	return o;
}

static UnsignedInt argb( const Tint &t, Real alpha )
{
	const UnsignedInt a = (UnsignedInt)( clamp01( alpha ) * 255.0f + 0.5f );
	const UnsignedInt r = (UnsignedInt)( clamp01( t.r ) * 255.0f + 0.5f );
	const UnsignedInt g = (UnsignedInt)( clamp01( t.g ) * 255.0f + 0.5f );
	const UnsignedInt b = (UnsignedInt)( clamp01( t.b ) * 255.0f + 0.5f );
	return ( a << 24 ) | ( r << 16 ) | ( g << 8 ) | b;
}

/// seconds from `then` to `now` on the wall clock, negative while `then` is still to come
static Real secondsSince( UnsignedInt now, UnsignedInt then )
{
	return (Real)(Int)( now - then ) / 1000.0f;
}

/// k, the band unit, from the radius the marker is drawn for
static Real bandUnit( Real radius )
{
	return MAX( 0.9f, MIN( 2.0f, 0.0065f * radius ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE ATLAS
///////////////////////////////////////////////////////////////////////////////////////////////////

enum Piece
{
	P_CAP_INLAY, P_CAP_SHELL, P_CAP_STEEL_A, P_CAP_STEEL_B,
	P_BAND_INLAY, P_BAND_SHELL, P_BAND_STEEL_A, P_BAND_STEEL_B,
	P_BRACKET, P_CHEVRON,
	P_DART00,
	P_DART_INLAY = P_DART00 + 16,
	P_DROP, P_DROP_ESTIMATE,
	P_GLYPH_BARRAGE00,
	P_GLYPH_DROP = P_GLYPH_BARRAGE00 + 4,
	P_GLYPH_GUARD,
	P_GLYPH_NUKE00,
	P_GLYPH_RETICLE00 = P_GLYPH_NUKE00 + 8,
	P_GLYPH_STRIKE = P_GLYPH_RETICLE00 + 4,
	P_ORBIT_DASH, P_ORDER_CROSS, P_ORDER_DOT, P_ORDER_RING, P_PING, P_RAIL,
	P_SEG_INLAY, P_SEG_STEEL_A, P_SEG_STEEL_B,
	P_STUD, P_STUD_DOT, P_TICK, P_TICK_MAJOR, P_TURN_BAND, P_TURN_DASH,
	P_COUNT
};

/// the MappedImage names in ReforgedGroundMarkers.ini, in Piece order
static const char *const PIECE_NAMES[ P_COUNT ] =
{
	"MarkerBandCapInlay", "MarkerBandCapShell", "MarkerBandCapSteelA", "MarkerBandCapSteelB",
	"MarkerBandInlay", "MarkerBandShell", "MarkerBandSteelA", "MarkerBandSteelB",
	"MarkerBracket", "MarkerChevron",
	"MarkerDart00", "MarkerDart01", "MarkerDart02", "MarkerDart03", "MarkerDart04", "MarkerDart05", "MarkerDart06", "MarkerDart07",
	"MarkerDart08", "MarkerDart09", "MarkerDart10", "MarkerDart11", "MarkerDart12", "MarkerDart13", "MarkerDart14", "MarkerDart15",
	"MarkerDartInlay",
	"MarkerDrop", "MarkerDropEstimate",
	"MarkerGlyphBarrage00", "MarkerGlyphBarrage01", "MarkerGlyphBarrage02", "MarkerGlyphBarrage03",
	"MarkerGlyphDrop", "MarkerGlyphGuard",
	"MarkerGlyphNuke00", "MarkerGlyphNuke01", "MarkerGlyphNuke02", "MarkerGlyphNuke03",
	"MarkerGlyphNuke04", "MarkerGlyphNuke05", "MarkerGlyphNuke06", "MarkerGlyphNuke07",
	"MarkerGlyphReticle00", "MarkerGlyphReticle01", "MarkerGlyphReticle02", "MarkerGlyphReticle03",
	"MarkerGlyphStrike",
	"MarkerOrbitDash", "MarkerOrderCross", "MarkerOrderDot", "MarkerOrderRing", "MarkerPing", "MarkerRail",
	"MarkerSegmentInlay", "MarkerSegmentSteelA", "MarkerSegmentSteelB",
	"MarkerStud", "MarkerStudDot", "MarkerTick", "MarkerTickMajor", "MarkerTurnBand", "MarkerTurnDash",
};

static const Real CHEVRON_ANCHOR_U = 0.7777778f;	///< its tip (pieces.json); every other piece is anchored on its middle

struct Atlas
{
	Bool tried;
	Bool ok;
	TextureClass *texture;
	Real u0[ P_COUNT ], v0[ P_COUNT ], u1[ P_COUNT ], v1[ P_COUNT ];
};
static Atlas theAtlas;

static const char *const ATLAS_FILE = "Art\\Textures\\ReforgedGroundMarkers.dds";

/** The pieces' rects out of the MappedImage blocks, and the sheet itself with its three mip levels,
	* never reduced by Texture Quality, clamped so a piece on the sheet's edge never wraps.  Tried once,
	* on the main thread at the sync point: without every piece, without the file, or given the missing
	* texture for it, the markers stay off and InGameUI::groundMarkersOn keeps the retail decals.  That
	* answer is handed to InGameUI here and only read there, the logic thread included. */
static Bool loadAtlasOnce( void );
static Bool loadAtlas( void )
{
	if( theAtlas.tried )
		return theAtlas.ok;
	theAtlas.tried = TRUE;
	theAtlas.ok = loadAtlasOnce();
	if( !theAtlas.ok )
		REF_PTR_RELEASE( theAtlas.texture );
	InGameUI::setGroundMarkersLoaded( theAtlas.ok );
	return theAtlas.ok;
}

static Bool loadAtlasOnce( void )
{
	if( TheMappedImageCollection == NULL || TheFileSystem == NULL )
		return FALSE;
	if( !TheFileSystem->doesFileExist( ATLAS_FILE ) )
	{
		DEBUG_LOG(( "GROUNDMARKERS: no %s, the markers stay off\n", ATLAS_FILE ));
		return FALSE;
	}

	AsciiString file;
	for( Int p = 0; p < P_COUNT; ++p )
	{
		const Image *image = TheMappedImageCollection->findImageByName( AsciiString( PIECE_NAMES[ p ] ) );
		if( image == NULL )
		{
			DEBUG_LOG(( "GROUNDMARKERS: no piece %s in the mapped images, the markers stay off\n", PIECE_NAMES[ p ] ));
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

	// three levels: about 8:1 minified at the default zoom, one level shimmered on every bevel and stud.
	// Not reducible, or a lower Texture Quality would take the top level the bands are drawn at
	theAtlas.texture = WW3DAssetManager::Get_Instance()->Get_Texture( file.str(), MIP_LEVELS_3, WW3D_FORMAT_UNKNOWN,
																																		 true, TextureBaseClass::TEX_REGULAR, false );
	if( theAtlas.texture == NULL )
		return FALSE;
	// Get_Texture hands back a texture for any name; a file the loader could not read is the missing one
	if( theAtlas.texture->Is_Initialized() && theAtlas.texture->Is_Missing_Texture() )
	{
		DEBUG_LOG(( "GROUNDMARKERS: %s loads as the missing texture, the markers stay off\n", file.str() ));
		return FALSE;
	}
	theAtlas.texture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	theAtlas.texture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE GROUND
///////////////////////////////////////////////////////////////////////////////////////////////////

/// the ground a sheet laid point by point goes on, the order marks' (W3DInGameUI's overlayGround)
static Real groundHeight( Real x, Real y )
{
	if( TheTerrainRenderObject )
		return TheTerrainRenderObject->getDrawnHeight( x, y );
	return TheTerrainLogic->getGroundHeight( x, y );
}

/** Heights taken at the sync point over what a marker covers, bilinear between the samples at draw
	* time: the terrain's heights are logic's, and the draw runs beside the tick.  Axis a is (ax, ay),
	* axis b its left; point (i, j) is the origin plus i steps along a and j along b. */
struct HeightGrid
{
	Real ox, oy, ax, ay, stepA, stepB;
	Int na, nb;
	std::vector<Real> h;

	HeightGrid() : ox( 0.0f ), oy( 0.0f ), ax( 1.0f ), ay( 0.0f ), stepA( 1.0f ), stepB( 1.0f ), na( 0 ), nb( 0 ) { }

	Bool valid( void ) const { return na > 1 && nb > 1; }

	/// the ground's heights, or with `water` the water's surface where there is water over the ground
	/// and NO_WATER elsewhere; TRUE when any sample stands on water
	Bool fill( Real x0, Real y0, Real dirX, Real dirY, Real sa, Int countA, Real sb, Int countB, Bool water = FALSE )
	{
		ox = x0;  oy = y0;  ax = dirX;  ay = dirY;
		stepA = MAX( 0.5f, sa );  stepB = MAX( 0.5f, sb );
		na = MAX( 2, countA );  nb = MAX( 2, countB );
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
					h[ j * na + i ] = groundHeight( x, y );
			}
		return wet;
	}

	/// the sample nearest (x, y): a water grid's surface is not blended across a shore
	Real nearest( Real x, Real y ) const
	{
		Real fa, fb;
		local( x, y, fa, fb );
		const Int i = MAX( 0, MIN( na - 1, (Int)floorf( fa + 0.5f ) ) );
		const Int j = MAX( 0, MIN( nb - 1, (Int)floorf( fb + 0.5f ) ) );
		return h[ j * na + i ];
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

/** A square round (cx, cy) on the world's lattice of `step`, so a square taken again a little further
	* on samples the same points: the heights at GROUND_STEP, a terrain cell, or the water at WATER_STEP.
	* ponytail: up to 161 x 161 heights (and 81 x 81 water samples) each time a moving zone leaves the
	* square GRID_PAD gave it; sample only the annuli the zone draws if that ever shows in a frame */
static Bool fillSquare( HeightGrid &g, Real cx, Real cy, Real reach, Real step, Bool water )
{
	const Real x0 = floorf( ( cx - reach ) / step ) * step;
	const Real y0 = floorf( ( cy - reach ) / step ) * step;
	const Int nx = MIN( MAX_GRID_SIDE, (Int)ceilf( ( cx + reach - x0 ) / step ) + 1 );
	const Int ny = MIN( MAX_GRID_SIDE, (Int)ceilf( ( cy + reach - y0 ) / step ) + 1 );
	// one square for both axes: the grid's b axis is a's left, (0, 1)
	return g.fill( x0, y0, 1.0f, 0.0f, step, nx, step, ny, water );
}

/// the square still holds (cx, cy) with `reach` round it
static Bool squareCovers( const HeightGrid &g, Real cx, Real cy, Real reach )
{
	return g.valid() && g.contains( cx - reach, cy - reach ) && g.contains( cx + reach, cy + reach );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE MARKERS
///////////////////////////////////////////////////////////////////////////////////////////////////

enum Family { FAMILY_GUARD, FAMILY_AREA, FAMILY_SUPER, FAMILY_DROP, FAMILY_STRIKE, FAMILY_SPECTRE };
enum Glyph { GLYPH_NONE, GLYPH_GUARD, GLYPH_DROP, GLYPH_STRIKE, GLYPH_BARRAGE, GLYPH_NUKE, GLYPH_RETICLE };
enum SuperKind { SUPER_NONE, SUPER_MISSILES, SUPER_CANNON };

struct Dart
{
	Real x, y, heading, scale;
	HeightGrid grid;
	HeightGrid water;
};

struct Fallen
{
	Real x, y;
};

struct Missile
{
	ObjectID id;
	Real d0;			///< how far from the target it was first seen
	Real ratio;		///< the least share of that it has had left
};

/** One zone with whatever goes with it: the aimed cursor (rebuilt every sync point), a click on its
	* way to the end of what it sent, or a selected guard's quiet circle. */
struct Marker
{
	RadiusCursorType type;
	Family family;
	Tint tint;
	Glyph glyph;
	Real glyphS;
	Real x, y, z;							///< the zone's middle, and the ground there
	Real R, rFrom;						///< the radius, and the one a wheel step eases from
	UnsignedInt rMs;
	Real k;
	UnsignedInt pressMs;			///< t's zero
	UnsignedInt clickMs;			///< tl's zero, 0 while aiming
	UnsignedInt clickFrame;
	ObjectID caster;
	ObjectID firstNewID;
	Bool guarding;
	// the end
	UnsignedInt fadeMs;
	Real fadeSeconds;
	Bool dead;
	// a second ping: the lead plane over the target, a superweapon's impact
	UnsignedInt pingMs;
	Real pingR;
	Tint pingTint;
	// superweapon countdown
	SuperKind super;
	UnsignedInt cannonFrames;
	std::vector<Missile> missiles;
	Bool missileSeen;
	Real frac;
	UnsignedInt impactMs;
	// the lane from the map's edge
	Bool lane, gap, arrow, exitStub;
	Real ex, ey;							///< E
	Real endX, endY;					///< where the lane ends: the target, or the Spectre's orbit
	Real ux, uy, L;
	Real half;
	Real chevUntil;
	std::vector<Real> offsets;
	// drops along the track, and the turn
	std::vector<Real> drops;	///< d_i, positive before the target
	Bool turn;
	Real rmin, rmax, rnom, side, lastPast;
	// what the click sent
	std::vector<ObjectID> planes;
	ObjectID lead;
	Bool planesSeen;
	Int initialCarried, carried;
	Bool pastT;
	UnsignedInt pastTMs;
	Bool hook;
	Bool inTurn;
	Real circleX, circleY, circleR, planeAngle, dropSpacing;
	std::vector<Fallen> fallen;
	std::vector<Dart> darts;
	// Spectre
	Bool orbit;
	Real Ro;
	ObjectID gunship;
	// the ground, and the water over it: the parts on water are drawn after the water
	HeightGrid grid, water;
	UnsignedInt gridMs;
	HeightGrid laneGrid, laneWater;
	Real laneKey[ 5 ];
	UnsignedInt laneMs;
	Bool zoneWet, laneWet;		///< what the last fill of each found
	Bool wet;									///< some of it stands on water this sync point

	Marker() :
		type( RADIUSCURSOR_NONE ), family( FAMILY_AREA ), glyph( GLYPH_NONE ), glyphS( 0.0f ),
		x( 0.0f ), y( 0.0f ), z( 0.0f ), R( 0.0f ), rFrom( 0.0f ), rMs( 0 ), k( 1.0f ),
		pressMs( 0 ), clickMs( 0 ), clickFrame( 0 ), caster( INVALID_ID ), firstNewID( INVALID_ID ), guarding( FALSE ),
		fadeMs( 0 ), fadeSeconds( 0.5f ), dead( FALSE ), pingMs( 0 ), pingR( 0.0f ),
		super( SUPER_NONE ), cannonFrames( 0 ), missileSeen( FALSE ), frac( 1.0f ), impactMs( 0 ),
		lane( FALSE ), gap( FALSE ), arrow( FALSE ), exitStub( FALSE ), ex( 0.0f ), ey( 0.0f ), endX( 0.0f ), endY( 0.0f ),
		ux( 1.0f ), uy( 0.0f ), L( 0.0f ), half( 0.0f ), chevUntil( 0.0f ),
		turn( FALSE ), rmin( 0.0f ), rmax( 0.0f ), rnom( 0.0f ), side( 1.0f ), lastPast( 0.0f ),
		lead( INVALID_ID ), planesSeen( FALSE ), initialCarried( 0 ), carried( 0 ), pastT( FALSE ), pastTMs( 0 ),
		hook( FALSE ), inTurn( FALSE ), circleX( 0.0f ), circleY( 0.0f ), circleR( 0.0f ), planeAngle( 0.0f ), dropSpacing( 0.0f ),
		orbit( FALSE ), Ro( 0.0f ), gunship( INVALID_ID ), gridMs( 0 ), laneMs( 0 ), zoneWet( FALSE ), laneWet( FALSE ), wet( FALSE )
	{
		tint = TINT_WARM;
		pingTint = TINT_WARM;
		for( Int i = 0; i < 5; ++i )
			laneKey[ i ] = 0.0f;
	}
};

/// a move's or an attack's click: the ring closing, its chevrons, the cross, and the dot that stays
struct ClickMark
{
	Real x, y, z;
	Bool deck;							///< on a bridge's deck: flat there
	Bool attack;
	UnsignedInt bornMs;
	HeightGrid grid;
};

static Marker s_aim;
static Bool s_aimOn = FALSE;
static std::vector<Marker> s_shots;
static std::vector<Marker> s_guards;
static std::vector<ClickMark> s_clicks;
static Bool s_taken = FALSE;		///< the take ran for this picture
static Bool s_wet = FALSE;			///< something taken stands on water: the pass after the water has work
static Bool s_lastShade = FALSE;	///< the terrain pass's shadeByShroud, for the pass after the water

/// the take's own gate, which the click markers share: drawn under the Reforged graphics with the atlas
/// loaded, in a match, while scripts show the icons
static Bool markersShown( void )
{
	return InGameUI::groundMarkersOn() && TheGameLogic != NULL && TheGameLogic->isInGame()
			&& !TheGameLogic->isInShellGame() && TheGameLogic->getDrawIconUI();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// READING THE POWER (sync point, read only)
///////////////////////////////////////////////////////////////////////////////////////////////////

/// the module data a template names `name`, the first; read only
static const ModuleData *findTemplateModule( const ThingTemplate *tmpl, const char *name )
{
	if( tmpl == NULL )
		return NULL;
	const ModuleInfo &info = tmpl->getBehaviorModuleInfo();
	for( Int i = 0; i < info.getCount(); ++i )
		if( info.getNthName( i ).compareNoCase( name ) == 0 )
			return info.getNthData( i );
	return NULL;
}

/// the caster's OCLSpecialPower for this power, found the way OCLSpecialPower::findAirlift finds one
static const OCLSpecialPower *findOCLPower( const Object *caster, const SpecialPowerTemplate *power )
{
	static const NameKeyType key_OCLSpecialPower = NAMEKEY( "OCLSpecialPower" );
	for( BehaviorModule **m = caster->getBehaviorModules(); *m; ++m )
	{
		if( (*m)->getModuleNameKey() != key_OCLSpecialPower )
			continue;
		const OCLSpecialPower *sp = (const OCLSpecialPower *)( *m );
		if( sp->getSpecialPowerTemplate() == power )
			return sp;
	}
	return NULL;
}

static DeliverPayloadAIUpdate *findDeliverPayload( Object *obj )
{
	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );
	return (DeliverPayloadAIUpdate *)obj->findUpdateModule( key_DeliverPayloadAIUpdate );
}

/// the family, tint and glyph a cursor type draws; Scud Storm and the Particle Cannon have no glyph
/// of their own and take Barrage, as the spec's family table says
static void classify( Marker &m )
{
	m.super = SUPER_NONE;
	Real share = 0.235f;
	switch( m.type )
	{
		case RADIUSCURSOR_GUARD_AREA:
			m.family = FAMILY_GUARD;	m.tint = TINT_WARM;		m.glyph = GLYPH_GUARD;		share = 0.17f;
			break;
		case RADIUSCURSOR_EMERGENCY_REPAIR:
		case RADIUSCURSOR_FRIENDLY_SPECIALPOWER:
		case RADIUSCURSOR_SPYSATELLITE:
		case RADIUSCURSOR_RADAR:
		case RADIUSCURSOR_SPYDRONE:
		case RADIUSCURSOR_CLEARMINES:
		case RADIUSCURSOR_AMBULANCE:
			m.family = FAMILY_AREA;		m.tint = TINT_WARM;		m.glyph = GLYPH_BARRAGE;
			break;
		case RADIUSCURSOR_PARADROP:
			m.family = FAMILY_DROP;		m.tint = TINT_GOLD;		m.glyph = GLYPH_DROP;			share = 0.27f;
			break;
		case RADIUSCURSOR_A10STRIKE:
		case RADIUSCURSOR_CARPETBOMB:
		case RADIUSCURSOR_DAISYCUTTER:
		case RADIUSCURSOR_HELIX_NAPALM_BOMB:
		case RADIUSCURSOR_NAPALMSTRIKE:
		case RADIUSCURSOR_ANTHRAXBOMB:
			m.family = FAMILY_STRIKE;	m.tint = TINT_ORANGE;	m.glyph = GLYPH_STRIKE;		share = 0.2f;
			break;
		case RADIUSCURSOR_NUCLEARMISSILE:
			m.family = FAMILY_SUPER;	m.tint = TINT_RED;		m.glyph = GLYPH_NUKE;			share = 0.2f;
			m.super = SUPER_MISSILES;
			break;
		case RADIUSCURSOR_SCUDSTORM:
			m.family = FAMILY_SUPER;	m.tint = TINT_RED;		m.glyph = GLYPH_BARRAGE;	share = 0.2f;
			m.super = SUPER_MISSILES;
			break;
		case RADIUSCURSOR_PARTICLECANNON:
			m.family = FAMILY_SUPER;	m.tint = TINT_RED;		m.glyph = GLYPH_BARRAGE;	share = 0.2f;
			m.super = SUPER_CANNON;
			break;
		case RADIUSCURSOR_SUPERWEAPON_SCATTER_AREA:
			m.family = FAMILY_AREA;		m.tint = TINT_RED;		m.glyph = GLYPH_BARRAGE;	share = 0.2f;
			break;
		case RADIUSCURSOR_SPECTREGUNSHIP:
			m.family = FAMILY_SPECTRE;	m.tint = TINT_ORANGE;	m.glyph = GLYPH_RETICLE;	share = 0.3f;
			break;
		default:		// the weapon cursors and every offensive area power: artillery, mines, ambush, Frenzy, EMP
			m.family = FAMILY_AREA;		m.tint = TINT_AMBER;	m.glyph = GLYPH_BARRAGE;
			break;
	}
	m.glyphS = share;		// times the radius, in describe
}

static Bool setLaneEnds( Marker &m, Real ex, Real ey, Real tx, Real ty )
{
	const Real dx = tx - ex, dy = ty - ey;
	const Real L = (Real)sqrt( dx * dx + dy * dy );
	if( L < 1.0f )
		return FALSE;
	m.lane = TRUE;
	m.ex = ex;  m.ey = ey;
	m.endX = tx;  m.endY = ty;
	m.ux = dx / L;  m.uy = dy / L;
	m.L = L;
	return TRUE;
}

/** The Spectre's: the farthest corner its deployment picks, pushed out by the orbit as
	* SpectreGunshipDeploymentUpdate pushes it, to the orbit point nearest that corner. */
static void planSpectre( Marker &m, const Object *caster )
{
	const SpectreGunshipDeploymentUpdateModuleData *data =
		(const SpectreGunshipDeploymentUpdateModuleData *)findTemplateModule( caster->getTemplate(), "SpectreGunshipDeploymentUpdate" );
	if( data == NULL || data->m_gunshipOrbitRadius <= 0.0f )
		return;
	m.orbit = TRUE;
	m.Ro = data->m_gunshipOrbitRadius;
	m.k = bandUnit( m.Ro );

	Coord3D target;
	target.x = m.x;  target.y = m.y;  target.z = m.z;
	Coord3D corner;
	switch( data->m_createLoc )
	{
		case CREATE_GUNSHIP_AT_EDGE_NEAR_SOURCE:				corner = TheTerrainLogic->findClosestEdgePoint( caster->getPosition() );	break;
		case CREATE_GUNSHIP_AT_EDGE_FARTHEST_FROM_SOURCE:	corner = TheTerrainLogic->findFarthestEdgePoint( caster->getPosition() );	break;
		case CREATE_GUNSHIP_AT_EDGE_NEAR_TARGET:				corner = TheTerrainLogic->findClosestEdgePoint( &target );	break;
		case CREATE_GUNSHIP_AT_EDGE_FARTHEST_FROM_TARGET:
		default:																				corner = TheTerrainLogic->findFarthestEdgePoint( &target );	break;
	}
	const Real dx = m.x - corner.x, dy = m.y - corner.y;
	const Real dist = (Real)sqrt( dx * dx + dy * dy );
	if( dist < 1.0f )
		return;
	const Real nx = dx / dist, ny = dy / dist;
	if( !setLaneEnds( m, m.x - nx * ( dist + m.Ro ), m.y - ny * ( dist + m.Ro ), m.x - nx * m.Ro, m.y - ny * m.Ro ) )
		return;
	m.gap = FALSE;
	m.arrow = FALSE;
	m.exitStub = FALSE;
	m.half = 26.0f * m.k;
	m.chevUntil = 1.0e9f;
}

/** The lane and the drops of a power whose planes come from the edge nearest the caster, read from
	* the OCL it runs now, the plane's template and its locomotor, as the inventory's table derives
	* them.  Distances along the track: d = window - DoorDelay * speed - i * DropDelay * speed, positive
	* before the target.  Both delays are SPEED_FIELDs and the speed is per frame, so the products are
	* the same at every game speed. */
static void planLane( Marker &m, const Object *caster, const SpecialPowerTemplate *power )
{
	m.lane = FALSE;
	m.gap = TRUE;
	m.arrow = TRUE;
	m.exitStub = TRUE;
	m.turn = FALSE;
	m.drops.clear();
	m.offsets.clear();
	m.offsets.push_back( 0.0f );
	m.half = 26.0f * m.k;
	m.chevUntil = 1.0e9f;
	if( caster == NULL || power == NULL )
		return;
	if( m.family == FAMILY_SPECTRE )
	{
		planSpectre( m, caster );
		return;
	}

	const OCLSpecialPower *sp = findOCLPower( caster, power );
	if( sp == NULL || sp->isAirlift() || sp->getCreateLoc() != CREATE_AT_EDGE_NEAR_SOURCE )
		return;
	const Coord3D edge = TheTerrainLogic->findClosestEdgePoint( caster->getPosition() );
	if( !setLaneEnds( m, edge.x, edge.y, m.x, m.y ) )
		return;

	DeliveryPreview preview;
	const ObjectCreationList *ocl = sp->findOCL();
	if( ocl == NULL || !ocl->getDeliveryPreview( &preview ) )
		return;

	// the wingmen square to the run, as DeliverPayloadNugget::create spaces them: an odd index on the
	// right of the heading, an even one on its left, (index + 1) / 2 spacings out
	Real widest = 0.0f;
	for( Int i = 1; i < preview.formationSize; ++i )
	{
		const Int out = (Int)( ( ( i + 1 ) / 2 ) * preview.formationSpacing );
		const Real q = ( i % 2 ) ? -(Real)out : (Real)out;
		m.offsets.push_back( q );
		widest = MAX( widest, (Real)fabs( q ) );
	}
	m.half = widest + 26.0f * m.k;

	if( preview.payloadCount <= 0 )
		return;
	const ThingTemplate *plane = TheThingFactory->findTemplate( preview.transportName );
	const DeliverPayloadAIUpdateModuleData *data =
		(const DeliverPayloadAIUpdateModuleData *)findTemplateModule( plane, "DeliverPayloadAIUpdate" );
	if( data == NULL )
		return;
	const LocomotorTemplateVector *locos = data->findLocomotorTemplateVector( LOCOMOTORSET_NORMAL );
	if( locos == NULL || locos->empty() || locos->front() == NULL )
		return;
	const LocomotorTemplate *loco = locos->front();
	const Real speed = loco->getMaxSpeed();
	if( speed <= 0.0f )
		return;

	const Real window = preview.distToTarget + preview.preOpenDistance;
	const Real first = window - (Real)data->m_doorDelay.get() * speed;
	const Real apart = (Real)preview.dropDelay * speed;
	for( Int i = 0; i < preview.payloadCount && i < 64; ++i )
		m.drops.push_back( first - apart * (Real)i );
	if( m.drops.front() > 0.0f )
		m.chevUntil = m.L - m.drops.front() - 20.0f * m.k;

	// the turn: the plane goes idle over the target and holds EA's circle round it, counter-clockwise
	// unless CirclingRadius is below nought, at a radius between MinSpeed's and cruise's
	const Real turnRate = loco->getMaxTurnRate();
	const Real circling = loco->getCirclingRadius();
	if( m.drops.back() < 0.0f && turnRate > 0.0f )
	{
		m.side = circling < 0.0f ? -1.0f : 1.0f;
		if( circling != 0.0f )
		{
			m.rmin = (Real)fabs( circling );
			m.rmax = m.rmin;
		}
		else
		{
			m.rmin = loco->getMinSpeed() / turnRate;
			m.rmax = speed / turnRate;
		}
		if( m.rmin > m.rmax )
		{
			const Real swap = m.rmin;
			m.rmin = m.rmax;
			m.rmax = swap;
		}
		if( m.rmin < 1.0f )
			m.rmin = MIN( m.rmax, 1.0f );
		if( m.rmax >= 1.0f )
		{
			m.turn = TRUE;
			m.rnom = (Real)sqrt( m.rmin * m.rmax );
			m.lastPast = -m.drops.back();
			m.exitStub = FALSE;		// the run bends away instead of going straight on
		}
	}
}

/// everything a cursor's marker is, from what InGameUI kept; the grids are left as they were
static void describe( Marker &m, const InGameUI::GroundMarkerAim &aim )
{
	m.type = aim.type;
	m.x = aim.pos.x;
	m.y = aim.pos.y;
	m.z = groundHeight( m.x, m.y );
	m.R = aim.radius;
	m.rFrom = aim.radiusFrom;
	m.rMs = aim.radiusMs;
	m.pressMs = aim.pressMs;
	m.caster = aim.caster;
	m.k = bandUnit( aim.unwheeled );		// the wheel scales R, never k
	m.orbit = FALSE;
	classify( m );
	// S: the guard's glyph keeps its unwheeled size, as the old shield did
	const Real share = m.glyphS;
	m.glyphS = share * ( m.family == FAMILY_GUARD ? aim.unwheeled : aim.radius );

	const Object *caster = TheGameLogic->findObjectByID( aim.caster );
	planLane( m, caster, aim.power );
	// a friendly power that drops what it carries (supply, Ally Aid, Rapid Deployment) is a drop
	if( m.type == RADIUSCURSOR_FRIENDLY_SPECIALPOWER && m.lane && !m.drops.empty() )
	{
		m.family = FAMILY_DROP;
		m.tint = TINT_GOLD;
		m.glyph = GLYPH_DROP;
		m.glyphS = 0.27f * aim.radius;
	}

	m.cannonFrames = 0;
	if( m.super == SUPER_CANNON && caster )
	{
		const ParticleUplinkCannonUpdateModuleData *data =
			(const ParticleUplinkCannonUpdateModuleData *)findTemplateModule( caster->getTemplate(), "ParticleUplinkCannonUpdate" );
		m.cannonFrames = data ? (UnsignedInt)data->m_beamTravelFrames.get() : 0;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// FOLLOWING WHAT A CLICK SENT (sync point, read only)
///////////////////////////////////////////////////////////////////////////////////////////////////

static Bool isClaimed( ObjectID id )
{
	for( size_t i = 0; i < s_shots.size(); ++i )
	{
		const Marker &m = s_shots[ i ];
		if( m.gunship == id )
			return TRUE;
		for( size_t p = 0; p < m.planes.size(); ++p )
			if( m.planes[ p ] == id )
				return TRUE;
		for( size_t p = 0; p < m.missiles.size(); ++p )
			if( m.missiles[ p ].id == id )
				return TRUE;
	}
	return FALSE;
}

static void startFade( Marker &m, UnsignedInt at, Real seconds )
{
	if( m.fadeMs != 0 )
		return;
	m.fadeMs = MAX( 1u, at );
	m.fadeSeconds = seconds;
}

static Real fadeOf( const Marker &m, UnsignedInt now )
{
	if( m.fadeMs == 0 )
		return 1.0f;
	const Real s = secondsSince( now, m.fadeMs );
	if( s <= 0.0f )
		return 1.0f;
	return 1.0f - clamp01( s / m.fadeSeconds );
}

static Bool fadedOut( const Marker &m, UnsignedInt now )
{
	return m.fadeMs != 0 && secondsSince( now, m.fadeMs ) >= m.fadeSeconds;
}

/// a dart on a plane, at its drawable (the smoothed picture's position) and its heading over the ground
static void addDart( Marker &m, const Object *obj, Real scale )
{
	Dart d;
	const Drawable *draw = obj->getDrawable();
	const Coord3D *pos = draw ? draw->getPosition() : obj->getPosition();
	d.x = pos->x;
	d.y = pos->y;
	d.heading = obj->getOrientation();
	const PhysicsBehavior *physics = obj->getPhysics();
	if( physics )
	{
		const Coord3D *v = physics->getVelocity();
		if( v->x * v->x + v->y * v->y > 0.01f )
			d.heading = (Real)atan2( v->y, v->x );
	}
	d.scale = scale;
	const Real reach = 21.0f * m.k * scale + 2.0f;
	fillSquare( d.grid, d.x, d.y, reach, GROUND_STEP, FALSE );
	if( fillSquare( d.water, d.x, d.y, reach, WATER_STEP, TRUE ) )
		m.wet = TRUE;		// a plane over the sea: its dart goes after the water
	m.darts.push_back( d );
}

/** A delivery: the planes it sent (claimed by the ids the click's tick handed out), a dart on each,
	* and the lead plane's state, payload and circle. */
static void followPlanes( Marker &m, UnsignedInt now, Real tl )
{
	if( !m.planesSeen )
	{
		if( tl > PLANE_WAIT )
		{
			startFade( m, now, END_FADE );
			return;
		}
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getID() < m.firstNewID || !obj->isLocallyControlled() )
				continue;
			if( m.caster != INVALID_ID && obj->getProducerID() != m.caster )
				continue;
			DeliverPayloadAIUpdate *ai = findDeliverPayload( obj );
			if( ai == NULL || !ai->isDeliveringPayload() || isClaimed( obj->getID() ) )
				continue;
			m.planes.push_back( obj->getID() );
		}
		if( m.planes.empty() )
			return;
		m.planesSeen = TRUE;
		// the lead flies at the target itself; a wingman's is offset
		Real best = 1.0e30f;
		for( size_t i = 0; i < m.planes.size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( m.planes[ i ] );
			DeliverPayloadAIUpdate *ai = obj ? findDeliverPayload( obj ) : NULL;
			if( ai == NULL )
				continue;
			const Coord3D *t = ai->getTargetPos();
			const Real d = ( t->x - m.x ) * ( t->x - m.x ) + ( t->y - m.y ) * ( t->y - m.y );
			if( d < best )
			{
				best = d;
				m.lead = m.planes[ i ];
			}
		}
		Object *lead = TheGameLogic->findObjectByID( m.lead );
		m.initialCarried = ( lead && lead->getContain() ) ? (Int)lead->getContain()->getContainCount() : 0;
		m.carried = m.initialCarried;
	}

	for( size_t i = 0; i < m.planes.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m.planes[ i ] );
		if( obj && !obj->isEffectivelyDead() )
			addDart( m, obj, 1.0f );
	}
	if( m.darts.empty() )
	{
		startFade( m, now, END_FADE );
		return;
	}

	Object *lead = TheGameLogic->findObjectByID( m.lead );
	DeliverPayloadAIUpdate *ai = lead ? findDeliverPayload( lead ) : NULL;
	if( ai == NULL || lead->isEffectivelyDead() )
	{
		m.inTurn = FALSE;
		startFade( m, now, END_FADE );
		return;
	}

	const Int state = ai->getDeliverPayloadStateID();
	const Coord3D *p = lead->getPosition();
	const Int carried = lead->getContain() ? (Int)lead->getContain()->getContainCount() : 0;
	// each item that left fell where the plane was; DropVariance is the rails' width already
	while( m.carried > carried )
	{
		Fallen f;
		f.x = p->x;
		f.y = p->y;
		m.fallen.push_back( f );
		--m.carried;
	}
	m.carried = carried;

	if( !m.pastT && ( p->x - m.ex ) * m.ux + ( p->y - m.ey ) * m.uy >= m.L )
	{
		m.pastT = TRUE;
		m.pastTMs = now;
		m.pingMs = now;
		m.pingR = m.R;
		m.pingTint = pingTintOf( m.tint );
	}

	m.hook = ( state == CONSIDER_NEW_APPROACH || state == RECOVER_FROM_OFF_MAP );
	m.inTurn = FALSE;
	if( m.pastT && state == DELIVERING && carried > 0 && !m.drops.empty() )
	{
		// the circle it really flies: r = |v| / turn rate, the centre on the side it turns to
		const PhysicsBehavior *physics = lead->getPhysics();
		const Locomotor *loco = ai->getCurLocomotor();
		if( physics && loco && lead->getBodyModule() )
		{
			const Coord3D *v = physics->getVelocity();
			const Real speed = (Real)sqrt( v->x * v->x + v->y * v->y );
			const Real turnRate = loco->getMaxTurnRate( lead->getBodyModule()->getDamageState() );
			if( speed > 0.01f && turnRate > 0.0f )
			{
				const Real r = speed / turnRate;
				const Real lx = -v->y / speed, ly = v->x / speed;
				m.circleR = r;
				m.circleX = p->x + m.side * lx * r;
				m.circleY = p->y + m.side * ly * r;
				m.planeAngle = (Real)atan2( p->y - m.circleY, p->x - m.circleX );
				m.dropSpacing = (Real)ai->getDropDelay() * speed;
				m.inTurn = TRUE;
			}
		}
	}

	const Bool emptied = !m.drops.empty() && carried == 0;
	if( state == HEAD_OFF_MAP || state == CLEAN_UP || emptied )
		startFade( m, now, END_FADE );
}

/// the Spectre: its gunship, a dart on it until it leaves, the marker fading once it departs
static void followGunship( Marker &m, UnsignedInt now, Real tl )
{
	static const NameKeyType key_SpectreGunshipUpdate = NAMEKEY( "SpectreGunshipUpdate" );
	if( m.gunship == INVALID_ID )
	{
		if( tl > PLANE_WAIT )
		{
			startFade( m, now, END_FADE );
			return;
		}
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getID() < m.firstNewID || !obj->isLocallyControlled() )
				continue;
			if( m.caster != INVALID_ID && obj->getProducerID() != m.caster )
				continue;
			if( obj->findUpdateModule( key_SpectreGunshipUpdate ) == NULL || isClaimed( obj->getID() ) )
				continue;
			m.gunship = obj->getID();
			break;
		}
		if( m.gunship == INVALID_ID )
			return;
	}
	const Object *obj = TheGameLogic->findObjectByID( m.gunship );
	if( obj == NULL || obj->isEffectivelyDead() )
	{
		startFade( m, now, END_FADE );
		return;
	}
	addDart( m, obj, 1.15f );
	const SpectreGunshipUpdate *update = (const SpectreGunshipUpdate *)obj->findUpdateModule( key_SpectreGunshipUpdate );
	if( update == NULL || !update->isActive() )
		startFade( m, now, END_FADE );
}

/// a superweapon's countdown: the share of the flight its missiles still have, or of the beam's travel
static void followSuperweapon( Marker &m, UnsignedInt now, Real tl )
{
	if( m.impactMs != 0 )
		return;
	Bool impact = FALSE;
	if( m.super == SUPER_CANNON )
	{
		const UnsignedInt frames = MAX( 1u, m.cannonFrames );
		const UnsignedInt elapsed = TheGameLogic->getFrame() - m.clickFrame;
		m.frac = 1.0f - clamp01( (Real)elapsed / (Real)frames );
		impact = elapsed >= frames;
	}
	else
	{
		static const NameKeyType key_NeutronMissileUpdate = NAMEKEY( "NeutronMissileUpdate" );
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getID() < m.firstNewID || !obj->isLocallyControlled() )
				continue;
			if( obj->findUpdateModule( key_NeutronMissileUpdate ) == NULL || isClaimed( obj->getID() ) )
				continue;
			const Coord3D *p = obj->getPosition();
			Missile missile;
			missile.id = obj->getID();
			missile.d0 = MAX( 1.0f, (Real)sqrt( ( p->x - m.x ) * ( p->x - m.x ) + ( p->y - m.y ) * ( p->y - m.y ) + ( p->z - m.z ) * ( p->z - m.z ) ) );
			missile.ratio = 1.0f;
			m.missiles.push_back( missile );
			m.missileSeen = TRUE;
		}
		Int live = 0;
		Real left = 0.0f;
		for( size_t i = 0; i < m.missiles.size(); ++i )
		{
			Missile &missile = m.missiles[ i ];
			const Object *obj = TheGameLogic->findObjectByID( missile.id );
			if( obj == NULL )
				continue;
			++live;
			const Coord3D *p = obj->getPosition();
			const Real d = (Real)sqrt( ( p->x - m.x ) * ( p->x - m.x ) + ( p->y - m.y ) * ( p->y - m.y ) + ( p->z - m.z ) * ( p->z - m.z ) );
			missile.ratio = MIN( missile.ratio, d / missile.d0 );		// the climb out of the silo never lights a segment again
			left = MAX( left, missile.ratio );
		}
		if( m.missileSeen )
		{
			m.frac = live > 0 ? left : 0.0f;
			impact = live == 0;
		}
		else if( tl > MISSILE_WAIT )
			startFade( m, now, END_FADE );
	}

	if( impact )
	{
		m.frac = 0.0f;
		m.impactMs = now;
		m.pingMs = now;
		m.pingR = 0.9f * m.R;
		m.pingTint = pingTintOf( TINT_GOLD );
		startFade( m, now + 100, 0.4f );		// gone between 0.1 and 0.5 s after it lands
	}
}

/// a guard click: snapped, quiet at 1.4 s, gone once a selected unit holds that post
static void followGuard( Marker &m, UnsignedInt now, InGameUI *ui, Real tl )
{
	if( tl > GUARD_LIFE )
	{
		startFade( m, now, AREA_FADE );
		return;
	}
	if( tl < 1.4f )
		return;
	const DrawableList *selected = ui->getAllSelectedDrawables();
	if( selected == NULL )
		return;
	for( DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it )
	{
		Object *obj = (*it) ? (*it)->getObject() : NULL;
		const AIUpdateInterface *ai = obj ? obj->getAI() : NULL;
		if( ai == NULL || !obj->isLocallyControlled() || ai->getCurrentStateID() != AI_GUARD )
			continue;
		const Coord3D *at = ai->getGuardLocation();
		if( ( at->x - m.x ) * ( at->x - m.x ) + ( at->y - m.y ) * ( at->y - m.y ) < 100.0f )
		{
			m.dead = TRUE;		// the guard's own quiet circle stands there now
			return;
		}
	}
}

/// how far a zone reaches from its middle, everything it draws round it counted
static Real zoneReach( const Marker &m )
{
	const Real R = MAX( m.R, m.rFrom );
	Real reach = MAX( R * 1.14f + 6.0f * m.k, R + 54.0f * m.k );
	reach = MAX( reach, 1.7f * m.glyphS );
	if( m.orbit )
		reach = MAX( reach, m.Ro + 12.0f * m.k );
	if( m.turn )
		reach = MAX( reach, 2.0f * m.rmax + 40.0f );
	return reach + 4.0f;
}

/** The zone's square of heights and water, taken again when the zone left the square GRID_PAD gave it
	* or three seconds have gone (a crater moves the ground); the lane's when either end moved.  m.wet
	* gathers what stands on water, the darts' included, for the pass after the water. */
static void takeGround( Marker &m, UnsignedInt now )
{
	const Real reach = zoneReach( m );
	if( !squareCovers( m.grid, m.x, m.y, reach ) || !squareCovers( m.water, m.x, m.y, reach ) || now - m.gridMs > 3000 )
	{
		fillSquare( m.grid, m.x, m.y, reach + GRID_PAD, GROUND_STEP, FALSE );
		m.zoneWet = fillSquare( m.water, m.x, m.y, reach + GRID_PAD, WATER_STEP, TRUE );
		m.gridMs = now;
	}
	m.wet = m.wet || m.zoneWet;
	if( !m.lane )
		return;
	// the lane's: every ten units along it, five lines across it from rail to rail and a little past
	const Real across = m.half + 16.0f * m.k;
	const Real key[ 5 ] = { m.ex, m.ey, m.endX, m.endY, across };
	Bool same = m.laneGrid.valid() && now - m.laneMs <= 3000;
	for( Int i = 0; same && i < 5; ++i )
		same = fabs( key[ i ] - m.laneKey[ i ] ) < 2.0f;
	if( !same )
	{
		const Real length = m.L + m.R + 300.0f;
		const Real x0 = m.ex + m.uy * across, y0 = m.ey - m.ux * across;		// E, across to the right
		m.laneGrid.fill( x0, y0, m.ux, m.uy, GROUND_STEP, MIN( 1000, (Int)ceilf( length / GROUND_STEP ) + 1 ), 0.5f * across, 5 );
		m.laneWet = m.laneWater.fill( x0, y0, m.ux, m.uy, WATER_STEP, MIN( 500, (Int)ceilf( length / WATER_STEP ) + 1 ), across, 3, TRUE );
		for( Int i = 0; i < 5; ++i )
			m.laneKey[ i ] = key[ i ];
		m.laneMs = now;
	}
	m.wet = m.wet || m.laneWet;
}

static void updateShot( Marker &m, UnsignedInt now, InGameUI *ui )
{
	const Real tl = secondsSince( now, m.clickMs );
	m.darts.clear();
	m.wet = FALSE;		// the darts and takeGround say it again
	if( m.lane && m.family != FAMILY_SPECTRE )
		followPlanes( m, now, tl );
	else if( m.family == FAMILY_SPECTRE )
		followGunship( m, now, tl );
	else if( m.super != SUPER_NONE )
		followSuperweapon( m, now, tl );
	else if( m.family == FAMILY_GUARD )
		followGuard( m, now, ui, tl );
	else
		startFade( m, m.clickMs + (UnsignedInt)( AREA_HOLD * 1000.0f ), AREA_FADE );

	if( fadedOut( m, now ) && m.darts.empty() )
		m.dead = TRUE;
}

/// the selected guards' quiet circles, one a post
static void takeGuards( InGameUI *ui, UnsignedInt now )
{
	std::vector<Marker> old;
	old.swap( s_guards );
	const DrawableList *selected = ui->getAllSelectedDrawables();
	if( selected == NULL )
		return;
	for( DrawableList::const_iterator it = selected->begin(); it != selected->end() && (Int)s_guards.size() < MAX_GUARDS; ++it )
	{
		Object *obj = (*it) ? (*it)->getObject() : NULL;
		if( obj == NULL || !obj->isLocallyControlled() || obj->isEffectivelyDead() || obj->getContainedBy() )
			continue;
		const AIUpdateInterface *ai = obj->getAI();
		if( ai == NULL || ai->getCurrentStateID() != AI_GUARD || ai->getGuardTargetType() != GUARDTARGET_LOCATION )
			continue;
		const Coord3D *at = ai->getGuardLocation();
		Real radius = ai->getGuardRadius();
		if( radius <= 0.0f )
			radius = AIGuardMachine::getStdGuardRange( obj );
		if( radius <= 0.0f )
			continue;

		Bool known = FALSE;
		for( size_t i = 0; i < s_guards.size() && !known; ++i )
			known = fabs( s_guards[ i ].x - at->x ) < 1.0f && fabs( s_guards[ i ].y - at->y ) < 1.0f && fabs( s_guards[ i ].R - radius ) < 1.0f;
		if( known )
			continue;

		Marker m;
		for( size_t i = 0; i < old.size(); ++i )
			if( fabs( old[ i ].x - at->x ) < 1.0f && fabs( old[ i ].y - at->y ) < 1.0f && fabs( old[ i ].R - radius ) < 1.0f )
			{
				m = old[ i ];		// its grid with it
				break;
			}
		m.type = RADIUSCURSOR_GUARD_AREA;
		m.family = FAMILY_GUARD;
		m.tint = TINT_WARM;
		m.glyph = GLYPH_NONE;		// the shield over the unit stays (W3DInGameUI::drawGuardMarkers)
		m.guarding = TRUE;
		m.x = at->x;
		m.y = at->y;
		m.z = groundHeight( m.x, m.y );
		m.R = radius;
		m.rFrom = radius;
		m.rMs = 0;
		m.k = bandUnit( radius );
		m.wet = FALSE;
		takeGround( m, now );
		s_guards.push_back( m );
	}
}

/// the move and attack clicks InGameUI's hints hold, with their ground
static void takeClicks( InGameUI *ui, UnsignedInt now )
{
	std::vector<ClickMark> old;
	old.swap( s_clicks );
	for( Int i = 0; i < InGameUI::MAX_MOVE_HINTS; ++i )
	{
		const InGameUI::MoveHintStruct &hint = ui->getMoveHint( i );
		if( hint.bornMs == 0 || now - hint.bornMs >= CLICK_LIFE_MS )
			continue;
		if( !W3DGroundMarkers_drawsClickMarker( hint.kind ) )
			continue;
		// paint in the terrain pass lies under the water; the old marker goes after it there
		// (W3DWaypointBuffer::takeWaterMarks)
		if( InGameUI::isOrderMarkOnWater( hint.pos.x, hint.pos.y, NULL ) )
			continue;

		ClickMark c;
		Bool reused = FALSE;
		for( size_t j = 0; j < old.size(); ++j )
			if( old[ j ].bornMs == hint.bornMs && old[ j ].x == hint.pos.x && old[ j ].y == hint.pos.y )
			{
				c = old[ j ];
				reused = TRUE;
				break;
			}
		if( !reused )
		{
			c.x = hint.pos.x;
			c.y = hint.pos.y;
			c.bornMs = hint.bornMs;
			c.attack = hint.kind != InGameUI::ORDER_HINT_MOVE;
			const Real ground = groundHeight( c.x, c.y );
			c.deck = hint.pos.z > ground + 2.0f;		// a point well over the ground is a bridge's deck
			c.z = c.deck ? hint.pos.z : ground;
			if( !c.deck )
				fillSquare( c.grid, c.x, c.y, CLICK_REACH, GROUND_STEP, FALSE );
		}
		s_clicks.push_back( c );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE SYNC POINT
///////////////////////////////////////////////////////////////////////////////////////////////////

void W3DGroundMarkers_clearDraw( void )
{
	s_taken = FALSE;
}

void W3DGroundMarkers_reset( void )
{
	s_aimOn = FALSE;
	s_aim = Marker();
	s_shots.clear();
	s_guards.clear();
	s_clicks.clear();
	s_taken = FALSE;
}

Bool W3DGroundMarkers_drawsClickMarker( Int orderHintKind )
{
	// the take's gate: with scripts hiding the icons the take draws nothing, so the old marker stays
	if( !markersShown() )
		return FALSE;
	return orderHintKind == InGameUI::ORDER_HINT_MOVE || orderHintKind == InGameUI::ORDER_HINT_ATTACK
			|| orderHintKind == InGameUI::ORDER_HINT_FORCE_ATTACK || orderHintKind == InGameUI::ORDER_HINT_ATTACK_GROUND;
}

void W3DGroundMarkers_take( InGameUI *ui )
{
	s_taken = FALSE;
	if( ui == NULL )
		return;
	std::vector<InGameUI::GroundMarkerShot> clicks;
	ui->takeGroundMarkerShots( clicks );
	loadAtlas();		// once, in the shell already, so InGameUI knows before the first aim or delivery
	if( !markersShown() )
	{
		W3DGroundMarkers_reset();
		return;
	}
	const UnsignedInt now = MAX( 1u, (UnsignedInt)Clock_Milliseconds() );

	// the clicks: each aimed zone snaps down where it was clicked
	for( size_t i = 0; i < clicks.size(); ++i )
	{
		const InGameUI::GroundMarkerShot &click = clicks[ i ];
		Marker m;
		describe( m, click.aim );
		m.clickMs = click.clickMs;
		m.clickFrame = click.clickFrame;
		m.firstNewID = click.firstNewID;
		s_shots.push_back( m );
	}
	while( (Int)s_shots.size() > MAX_SHOTS )
		s_shots.erase( s_shots.begin() );

	for( size_t i = 0; i < s_shots.size(); ++i )
	{
		updateShot( s_shots[ i ], now, ui );
		takeGround( s_shots[ i ], now );
	}
	for( size_t i = 0; i < s_shots.size(); )
	{
		if( s_shots[ i ].dead )
			s_shots.erase( s_shots.begin() + i );
		else
			++i;
	}

	// the cursor, following the mouse
	const InGameUI::GroundMarkerAim &aim = ui->getGroundMarkerAim();
	s_aimOn = aim.type != RADIUSCURSOR_NONE && aim.endMs == 0 && aim.hasPos;
	if( s_aimOn )
	{
		describe( s_aim, aim );
		s_aim.wet = FALSE;
		takeGround( s_aim, now );
	}

	takeGuards( ui, now );
	takeClicks( ui, now );

	s_wet = s_aimOn && s_aim.wet;
	for( size_t i = 0; i < s_shots.size(); ++i )
		s_wet = s_wet || s_shots[ i ].wet;
	for( size_t i = 0; i < s_guards.size(); ++i )
		s_wet = s_wet || s_guards[ i ].wet;
	s_taken = TRUE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE QUADS
///////////////////////////////////////////////////////////////////////////////////////////////////

/** The order marks' batcher (W3DInGameUI's GroundOverlayQuads) with the atlas's coordinates on the
	* first set and the shroud's on the second. */
class MarkerQuads
{
public:
	MarkerQuads( void ) : m_quads( 0 ), m_pull( 0.0f ), m_shrouded( FALSE ),
		m_shroudOffsetX( 0.0f ), m_shroudOffsetY( 0.0f ), m_shroudScaleX( 0.0f ), m_shroudScaleY( 0.0f ) { }

	void begin( const Vector3 &eye, Real pull, W3DShroud *shroud )
	{
		m_eye = eye;
		m_pull = pull;
		m_shrouded = shroud != NULL;
		if( shroud != NULL )
		{
			m_shroudOffsetX = shroud->getCellWidth() - shroud->getDrawOriginX();
			m_shroudOffsetY = shroud->getCellHeight() - shroud->getDrawOriginY();
			m_shroudScaleX = 1.0f / ( shroud->getCellWidth() * shroud->getTextureWidth() );
			m_shroudScaleY = 1.0f / ( shroud->getCellHeight() * shroud->getTextureHeight() );
		}
	}

	void add( const Vector3 *p, const Real *u, const Real *v, const UnsignedInt *c )
	{
		if( m_quads >= MAX_QUADS )
			flush();
		Vert *out = &m_verts[ m_quads * 4 ];
		for( Int i = 0; i < 4; ++i )
		{
			out[ i ].pos = p[ i ];
			out[ i ].diffuse = c[ i ];
			out[ i ].u = u[ i ];
			out[ i ].v = v[ i ];
		}
		++m_quads;
	}

	void flush( void );

private:
	enum { MAX_QUADS = 512 };
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
		Real u, v;
	};
	Vert m_verts[ MAX_QUADS * 4 ];
	Int m_quads;
	Vector3 m_eye;
	Real m_pull;
	Bool m_shrouded;
	Real m_shroudOffsetX, m_shroudOffsetY, m_shroudScaleX, m_shroudScaleY;
};

void MarkerQuads::flush( void )
{
	if( m_quads == 0 )
		return;
	const Int quads = m_quads;
	m_quads = 0;

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
				const Vert &src = m_verts[ q * 4 + i ];
				Vector3 pos = src.pos;
				if( m_pull > 0.0f )
				{
					// along the ray to the eye, so the pixel stays where it was and only the depth moves
					Vector3 toEye = m_eye - pos;
					const Real distance = toEye.Length();
					if( distance > 2.0f * m_pull )
						pos += toEye * ( m_pull / distance );
				}
				vb->x = pos.X;
				vb->y = pos.Y;
				vb->z = pos.Z;
				vb->nx = 0.0f;
				vb->ny = 0.0f;
				vb->nz = 1.0f;
				vb->diffuse = src.diffuse;
				vb->u1 = src.u;
				vb->v1 = src.v;
				vb->u2 = m_shrouded ? ( src.pos.X + m_shroudOffsetX ) * m_shroudScaleX : 0.0f;
				vb->v2 = m_shrouded ? ( src.pos.Y + m_shroudOffsetY ) * m_shroudScaleY : 0.0f;
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

static MarkerQuads theMarkerQuads;

// The ground sheets' state (W3DInGameUI's SC_GROUND_OVERLAY_TESTED) with the atlas on: depth tested,
// never depth written, texture times vertex colour
#define SC_GROUND_MARKER ( SHADE_CNST( ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE ) )

/** The atlas on stage 0, and the shroud's texture on stage 1 times what stage 0 gave where the ground
	* took it: the second coordinate set, stage 1 reading it and no texture matrix on either, set here
	* so no earlier pass's leaves stand. */
static void setMarkerState( Bool additive, TextureClass *shroudTexture )
{
	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );
	DX8Wrapper::Set_Texture( 0, theAtlas.texture );
	DX8Wrapper::Set_Texture( 1, shroudTexture );
	ShaderClass shader( SC_GROUND_MARKER );
	if( additive )
		shader.Set_Dst_Blend_Func( ShaderClass::DSTBLEND_ONE );
	if( shroudTexture != NULL )
		shader.Set_Post_Detail_Color_Func( ShaderClass::DETAILCOLOR_SCALE );
	DX8Wrapper::Set_Shader( shader );
	DX8Wrapper::Set_Transform( D3DTS_WORLD, Matrix3D( 1 ) );
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
}

/// this picture's camera on the ground, the light and the marker's ground
struct DrawCtx
{
	UnsignedInt now;
	Real rx, ry;					///< the screen's right on the ground
	Real yaw;							///< its angle
	Real lgx, lgy;				///< the HUD's light, from the top left of the screen
	const HeightGrid *g1;		///< the ground: the zone's square, or a dart's or a click's
	const HeightGrid *g2;		///< the lane's
	const HeightGrid *w1;		///< the water over them
	const HeightGrid *w2;
	Real flatZ;
	Bool waterPass;					///< drawing after the water: only the quads standing on it
};
static DrawCtx s_ctx;

/// the point on the ground, or on the water's surface where water stands over the ground there
static Vector3 groundPoint( Real x, Real y, Bool &onWater )
{
	Real z = s_ctx.flatZ;
	if( s_ctx.g1 && s_ctx.g1->contains( x, y ) )
		z = s_ctx.g1->at( x, y );
	else if( s_ctx.g2 && s_ctx.g2->contains( x, y ) )
		z = s_ctx.g2->at( x, y );
	else if( s_ctx.g1 && s_ctx.g1->valid() )
		z = s_ctx.g1->at( x, y );
	else if( s_ctx.g2 && s_ctx.g2->valid() )
		z = s_ctx.g2->at( x, y );
	onWater = FALSE;
	const HeightGrid *water = ( s_ctx.w1 && s_ctx.w1->contains( x, y ) ) ? s_ctx.w1
													: ( s_ctx.w2 && s_ctx.w2->contains( x, y ) ) ? s_ctx.w2 : NULL;
	if( water )
	{
		const Real surface = water->nearest( x, y );
		if( surface > z )
		{
			z = surface;
			onWater = TRUE;
		}
	}
	return Vector3( x, y, z + MARKER_LIFT );
}

/// how much of a lit pair's A piece shows where the outward normal is (nx, ny)
static Real lightOf( Real nx, Real ny )
{
	return 0.5f + 0.5f * ( nx * s_ctx.lgx + ny * s_ctx.lgy );
}

/** One quad of a piece: corners on the ground, (pu, pv) in the piece's own 0..1.  A quad with two
	* corners or more on water is drawn after the water at its surface, the rest in the terrain pass:
	* painted there, a quad over the sea lay on the seabed under the water and took its tint, unlike
	* on each device.  No quad is longer than GROUND_STEP, so a shore is cut quad by quad. */
static void emit( Int piece, const Real *x, const Real *y, const Real *pu, const Real *pv, const UnsignedInt *col )
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
	theMarkerQuads.add( p, u, v, col );
}

/** A band round (cx, cy) at radius r, `across` wide, the piece's row 0 on the outer edge, from angle
	* a0 counter-clockwise to a1: a strip samples the middle column, an arc (stretchU) lays u along it.
	* One quad per six degrees or GROUND_STEP world units at most, so the cross-section never stretches
	* and no quad spans more than a terrain cell of the ridge under it. */
static void arcStrip( Int piece, Real cx, Real cy, Real r, Real across, Real a0, Real a1, Bool stretchU,
											const Tint &tint, Real alpha, Bool lit )
{
	const Real span = a1 - a0;
	if( alpha <= 0.002f || r <= 0.0f || span <= 0.0f )
		return;
	Int n = MAX( (Int)ceilf( span / ( 6.0f * MARKER_DEG ) ), (Int)ceilf( span * r / GROUND_STEP ) );
	n = MAX( 1, MIN( 480, n ) );
	const Real ro = r + 0.5f * across;
	const Real ri = MAX( 0.0f, r - 0.5f * across );
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	for( Int i = 0; i < n; ++i )
	{
		const Real ta = a0 + span * (Real)i / (Real)n;
		const Real tb = a0 + span * (Real)( i + 1 ) / (Real)n;
		const Real ca = (Real)cos( ta ), sa = (Real)sin( ta );
		const Real cb = (Real)cos( tb ), sb = (Real)sin( tb );
		const Real x[ 4 ] = { cx + ro * ca, cx + ro * cb, cx + ri * cb, cx + ri * ca };
		const Real y[ 4 ] = { cy + ro * sa, cy + ro * sb, cy + ri * sb, cy + ri * sa };
		const Real ua = stretchU ? (Real)i / (Real)n : 0.5f;
		const Real ub = stretchU ? (Real)( i + 1 ) / (Real)n : 0.5f;
		const Real pu[ 4 ] = { ua, ub, ub, ua };
		const Real wa = lit ? lightOf( ca, sa ) : 1.0f;
		const Real wb = lit ? lightOf( cb, sb ) : 1.0f;
		const UnsignedInt col[ 4 ] = { argb( tint, alpha * wa ), argb( tint, alpha * wb ), argb( tint, alpha * wb ), argb( tint, alpha * wa ) };
		emit( piece, x, y, pu, pv, col );
	}
}

static void ring( Int piece, Real cx, Real cy, Real r, Real across, const Tint &tint, Real alpha, Bool lit )
{
	arcStrip( piece, cx, cy, r, across, 0.0f, 2.0f * MARKER_PI, FALSE, tint, alpha, lit );
}

/// an arc piece bent round the circle, centred on aMid, half as wide as given each side
static void arcPiece( Int piece, Real cx, Real cy, Real rc, Real aMid, Real halfAngle, Real across,
											const Tint &tint, Real alpha, Bool lit )
{
	arcStrip( piece, cx, cy, rc, across, aMid - halfAngle, aMid + halfAngle, TRUE, tint, alpha, lit );
}

/** A quad piece sx by sy world units with its +u along `ang` and its anchor (au, av) on (cx, cy),
	* cut into squares of GROUND_STEP at most so it lies on the ground it crosses. */
static void quadPiece( Int piece, Real cx, Real cy, Real ang, Real sx, Real sy, const Tint &tint, Real alpha,
											 Real au = 0.5f, Real av = 0.5f )
{
	if( alpha <= 0.002f || sx <= 0.0f || sy <= 0.0f )
		return;
	const Real hx = (Real)cos( ang ), hy = (Real)sin( ang );		// +u
	const Real dx = hy, dy = -hx;																// +v, the heading's right
	const Int n = MAX( 1, MIN( 16, (Int)ceilf( MAX( sx, sy ) / GROUND_STEP ) ) );
	const UnsignedInt c = argb( tint, alpha );
	const UnsignedInt col[ 4 ] = { c, c, c, c };
	for( Int j = 0; j < n; ++j )
	{
		for( Int i = 0; i < n; ++i )
		{
			const Real pu0 = (Real)i / (Real)n, pu1 = (Real)( i + 1 ) / (Real)n;
			const Real pv0 = (Real)j / (Real)n, pv1 = (Real)( j + 1 ) / (Real)n;
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
}

/** A straight band out of (ox, oy) along (dx, dy) from s0 to s1, the piece's row 0 at qOuter on the
	* left and `across` wide: a strip's middle column, or u laid along it (stretchU).  Alpha by s. */
template< class AlphaAt >
static void lineStrip( Int piece, Real ox, Real oy, Real dx, Real dy, Real s0, Real s1, Real qOuter, Real across,
											 Bool stretchU, const Tint &tint, AlphaAt alphaAt, Real step )
{
	if( s1 <= s0 )
		return;
	const Real lx = -dy, ly = dx;
	// never longer than a terrain cell along the ground, whatever the caller asked
	const Int n = MAX( 1, MIN( 1200, (Int)ceilf( ( s1 - s0 ) / MIN( step, GROUND_STEP ) ) ) );
	const Real qi = qOuter - across;
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	for( Int i = 0; i < n; ++i )
	{
		const Real sa = s0 + ( s1 - s0 ) * (Real)i / (Real)n;
		const Real sb = s0 + ( s1 - s0 ) * (Real)( i + 1 ) / (Real)n;
		const Real aa = alphaAt( sa ), ab = alphaAt( sb );
		if( aa <= 0.002f && ab <= 0.002f )
			continue;
		const Real x[ 4 ] = { ox + dx * sa + lx * qOuter, ox + dx * sb + lx * qOuter, ox + dx * sb + lx * qi, ox + dx * sa + lx * qi };
		const Real y[ 4 ] = { oy + dy * sa + ly * qOuter, oy + dy * sb + ly * qOuter, oy + dy * sb + ly * qi, oy + dy * sa + ly * qi };
		const Real ua = stretchU ? (Real)i / (Real)n : 0.5f;
		const Real ub = stretchU ? (Real)( i + 1 ) / (Real)n : 0.5f;
		const Real pu[ 4 ] = { ua, ub, ub, ua };
		const UnsignedInt col[ 4 ] = { argb( tint, aa ), argb( tint, ab ), argb( tint, ab ), argb( tint, aa ) };
		emit( piece, x, y, pu, pv, col );
	}
}

/// a constant alpha for lineStrip
struct FlatAlpha
{
	Real a;
	explicit FlatAlpha( Real alpha ) : a( alpha ) { }
	Real operator()( Real ) const { return a; }
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE MARKS
///////////////////////////////////////////////////////////////////////////////////////////////////

/// turned at `rate` rad/s while aiming; on the click it eases to the nearest `step` in 0.22 s
static Real settle( Real rate, Real t, Real tl, Real step )
{
	if( tl < 0.0f )
		return rate * t;
	const Real s0 = rate * ( t - tl );
	const Real target = (Real)floor( s0 / step + 0.5f ) * step;
	return s0 + ( target - s0 ) * easeOut( tl / 0.22f );
}

static Real currentRadius( const Marker &m, UnsignedInt now )
{
	if( m.rMs == 0 || now - m.rMs >= WHEEL_EASE_MS )
		return m.R;
	const Real f = 1.0f - (Real)( now - m.rMs ) / (Real)WHEEL_EASE_MS;
	return m.R + ( m.rFrom - m.R ) * f * f * f;
}

/// a dart on a plane: the nearest of the sixteen baked headings under the camera, turned the rest of
/// the way, and the flat inlay turned to the exact heading
static void drawDart( Real x, Real y, Real heading, Real scale, Real k, const Tint &tint, Real alpha )
{
	const Real step = 22.5f * MARKER_DEG;
	const Real rel = heading - s_ctx.yaw;
	const Int r = (Int)floorf( rel / step + 0.5f );
	const Int index = ( ( r % 16 ) + 16 ) % 16;
	const Real side = 42.0f * k * scale;
	quadPiece( P_DART00 + index, x, y, s_ctx.yaw + ( rel - (Real)r * step ), side, side, TINT_WHITE, alpha );
	quadPiece( P_DART_INLAY, x, y, heading, side, side, tint, alpha );
}

/// the zone: shell, the lit steel, the inlay, the studs, the range scale and the brackets
static void drawZone( const Marker &m, Real R, Real t, Real tl, Bool quiet, Real app )
{
	const Real k = m.k, x = m.x, y = m.y, yaw = s_ctx.yaw;
	const Real al = app * ( quiet ? 0.6f : 1.0f );
	if( al <= 0.002f )
		return;
	ring( P_BAND_SHELL, x, y, R, 20.0f * k, TINT_WHITE, al, FALSE );
	ring( P_BAND_STEEL_B, x, y, R, 20.0f * k, TINT_WHITE, al, FALSE );
	ring( P_BAND_STEEL_A, x, y, R, 20.0f * k, TINT_WHITE, al, TRUE );
	const Real glow = ( tl >= 0.0f || quiet ) ? 1.0f : 0.78f + 0.22f * (Real)sin( 7.5f * t );
	ring( P_BAND_INLAY, x, y, R, 20.0f * k, scaled( m.tint, glow ), al, FALSE );

	const Int studs = quiet ? 4 : 8;
	const Real flash = tl >= 0.0f ? MAX( 0.0f, 1.0f - tl / 0.5f ) : 0.0f;
	const Tint dot = scaled( m.tint, 1.0f + 0.4f * flash );
	for( Int j = 0; j < studs; ++j )
	{
		const Real a = yaw + 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / (Real)studs;
		const Real sx = x + R * (Real)cos( a ), sy = y + R * (Real)sin( a );
		quadPiece( P_STUD, sx, sy, yaw, 18.0f * k, 18.0f * k, TINT_WHITE, al );
		quadPiece( P_STUD_DOT, sx, sy, yaw, 6.0f * k, 6.0f * k, dot, al );
	}
	if( quiet )
		return;

	const Real spin = settle( 0.35f, t, tl, 0.25f * MARKER_PI );
	const Real rc = R - 22.0f * k;
	for( Int j = 0; j < 72; ++j )
	{
		const Real a = yaw + spin + (Real)j * MARKER_PI / 36.0f;
		const Bool major = ( j % 9 ) == 0;
		quadPiece( major ? P_TICK_MAJOR : P_TICK, x + rc * (Real)cos( a ), y + rc * (Real)sin( a ), a - 0.5f * MARKER_PI,
							 6.0f * k, ( major ? 22.0f : 12.0f ) * k, m.tint, al );
	}

	Real rb, swing;
	if( tl < 0.0f )
	{
		rb = R + 30.0f * k;
		swing = 0.09f * (Real)sin( 2.2f * t );
	}
	else
	{
		rb = R + 30.0f * k - 13.0f * k * easeOut( tl / 0.22f );
		swing = 0.0f;
	}
	const Real halfAngle = ( 0.2f * R + 4.0f * k ) / rb;
	const Tint bracket = scaled( m.tint, ( tl >= 0.0f && tl < 0.4f ) ? 1.15f : 1.0f );
	for( Int j = 0; j < 4; ++j )
		arcPiece( P_BRACKET, x, y, rb, yaw + 0.25f * MARKER_PI + swing + (Real)j * 0.5f * MARKER_PI, halfAngle, 14.0f * k, bracket, al, FALSE );
}

/// the glyph in the middle: the nearest baked step of a spinning one, turned the rest of the way
static void drawGlyph( const Marker &m, Real t, Real tl, Real app )
{
	Int base = P_GLYPH_GUARD, steps = 1;
	Real stepDeg = 0.0f, rate = 0.0f;
	switch( m.glyph )
	{
		case GLYPH_NONE:		return;
		case GLYPH_GUARD:		base = P_GLYPH_GUARD;		break;
		case GLYPH_DROP:		base = P_GLYPH_DROP;		break;
		case GLYPH_STRIKE:	base = P_GLYPH_STRIKE;	break;
		case GLYPH_BARRAGE:	base = P_GLYPH_BARRAGE00;	steps = 4;	stepDeg = 22.5f;	rate = 0.6f;	break;
		case GLYPH_NUKE:		base = P_GLYPH_NUKE00;		steps = 8;	stepDeg = 15.0f;	rate = -0.4f;	break;
		case GLYPH_RETICLE:	base = P_GLYPH_RETICLE00;	steps = 4;	stepDeg = 22.5f;	rate = 0.8f;	break;
	}
	Int piece = base;
	Real turn = 0.0f;
	if( steps > 1 )
	{
		const Real step = stepDeg * MARKER_DEG;
		const Real spin = settle( rate, t, tl, step );
		const Int r = (Int)floorf( spin / step + 0.5f );
		piece = base + ( ( r % steps ) + steps ) % steps;
		turn = spin - (Real)r * step;		// the shape repeats every steps * step, the light never turns
	}
	const Real side = 2.4f * m.glyphS;
	quadPiece( piece, m.x, m.y, s_ctx.yaw + turn, side, side, m.tint, app );
}

/// twelve segments clockwise from the top, the lit ones the share of the flight still to come
static void drawCountdown( const Marker &m, Real R, Real t, Real alpha )
{
	const Real k = m.k, rc = R + 42.0f * k;
	ring( P_BAND_SHELL, m.x, m.y, rc, 22.2f * k, TINT_WHITE, alpha, FALSE );
	const Int lit = (Int)ceilf( m.frac * 12.0f );
	const Bool blinkOff = sin( 18.0f * t ) < 0.0f;
	const Real halfAngle = ( MARKER_PI / 12.0f - 0.03f ) * ( 50.0f / 48.0f );
	const Tint dim = scaled( m.tint, 0.25f );
	for( Int j = 0; j < 12; ++j )
	{
		const Real a = s_ctx.yaw + 0.5f * MARKER_PI - (Real)j * MARKER_PI / 6.0f;
		arcPiece( P_SEG_STEEL_B, m.x, m.y, rc, a, halfAngle, 14.0f * k, TINT_WHITE, alpha, FALSE );
		arcPiece( P_SEG_STEEL_A, m.x, m.y, rc, a, halfAngle, 14.0f * k, TINT_WHITE, alpha, TRUE );
		const Bool on = j < lit && !( j == lit - 1 && blinkOff );
		arcPiece( P_SEG_INLAY, m.x, m.y, rc, a, halfAngle, 14.0f * k, on ? m.tint : dim, alpha, FALSE );
	}
}

/// the Spectre's orbit: the dashes clockwise, the chevrons the way the gunship flies, counter-clockwise
/// (SpectreGunshipUpdate aims at the anticlockwise side of its line to the target, its apogee)
static void drawOrbit( const Marker &m, Real t, Real alpha )
{
	const Real k = m.k, Ro = m.Ro, yaw = s_ctx.yaw;
	ring( P_BAND_SHELL, m.x, m.y, Ro, 20.0f * k, TINT_WHITE, 0.84f * alpha, FALSE );
	const Real step = 2.0f * MARKER_PI / 18.0f;
	for( Int j = 0; j < 18; ++j )
		arcPiece( P_ORBIT_DASH, m.x, m.y, Ro, yaw - 0.9f * t + (Real)j * step, step * 0.31f * 18.0f / 17.0f, 7.0f * k, TINT_WHITE, alpha, FALSE );
	for( Int j = 0; j < 9; ++j )
	{
		const Real a = yaw + 0.9f * t + (Real)j * 2.0f * MARKER_PI / 9.0f;
		quadPiece( P_CHEVRON, m.x + Ro * (Real)cos( a ), m.y + Ro * (Real)sin( a ), a + 0.5f * MARKER_PI,
							 18.0f * k * 0.8f, 28.0f * k * 0.8f, m.tint, alpha, CHEVRON_ANCHOR_U, 0.5f );
	}
}

/// the lane's alpha at s: whole up to the target, the exit stub from 0.55 down to nothing
struct LaneAlpha
{
	Real app, L, gap, exitLen;
	Real operator()( Real s ) const
	{
		if( s <= L )
			return app;
		return app * 0.55f * clamp01( 1.0f - ( s - L - gap ) / MAX( 1.0f, exitLen ) );
	}
};

/// the lane: rails at the run's width, the chevrons marching on every ship's line, the arrow at the rim
static void drawLane( const Marker &m, Real R, Real t, Real tl, Real app )
{
	const Real k = m.k, L = m.L;
	const Real grow = smoothstep( 0.05f, 0.7f, t );
	const Real gap = m.gap ? R : 0.0f;
	const Real exitLen = ( m.gap && m.exitStub ) ? 260.0f : 0.0f;
	const Real head = grow * ( L + gap + exitLen );
	const Real a0 = 0.0f;
	const Real a1 = m.gap ? MIN( head, L - gap - 14.0f * k ) : MIN( head, L );
	const Real b0 = L + gap + 14.0f * k;
	const Real b1 = exitLen > 0.0f ? MIN( head, L + gap + exitLen ) : 0.0f;
	LaneAlpha alphaAt;
	alphaAt.app = app;
	alphaAt.L = L;
	alphaAt.gap = gap;
	alphaAt.exitLen = exitLen;

	for( Int sg = -1; sg <= 1; sg += 2 )
	{
		const Real q = (Real)sg * m.half + 4.0f * k;
		lineStrip( P_RAIL, m.ex, m.ey, m.ux, m.uy, a0, a1, q, 8.0f * k, FALSE, TINT_WHITE, alphaAt, 40.0f );
		if( b1 > b0 )
			lineStrip( P_RAIL, m.ex, m.ey, m.ux, m.uy, b0, b1, q, 8.0f * k, FALSE, TINT_WHITE, alphaAt, 40.0f );
	}

	// pairs every 46k, the second 8k behind the first, marching at 135k a second while aiming and
	// 450k after the click; each placed from the phase, so nothing is baked at the sync point
	const Real speed = ( tl >= 0.0f ? 450.0f : 135.0f ) * k;
	const Real spacing = 46.0f * k;
	const Real phase = (Real)fmod( t * speed, spacing );
	const Real limit = MIN( m.chevUntil, MAX( a1, b1 ) );
	const Real heading = (Real)atan2( m.uy, m.ux );
	const Real lx = -m.uy, ly = m.ux;
	for( size_t o = 0; o < m.offsets.size(); ++o )
	{
		const Real q = m.offsets[ o ];
		for( Real front = phase + 0.5f * spacing - spacing; front < limit + 8.0f * k; front += spacing )
		{
			for( Int pair = 0; pair < 2; ++pair )
			{
				const Real s = front - (Real)pair * 8.0f * k;
				if( s < 0.0f || s > limit )
					continue;
				if( !( ( s >= a0 && s <= a1 ) || ( s >= b0 && s <= b1 ) ) )
					continue;
				quadPiece( P_CHEVRON, m.ex + m.ux * s + lx * q, m.ey + m.uy * s + ly * q, heading,
									 18.0f * k, 28.0f * k, m.tint, alphaAt( s ), CHEVRON_ANCHOR_U, 0.5f );
			}
		}
	}

	if( !m.arrow || !m.gap || grow <= 0.95f )
		return;
	// two arms from the tip at the rim back to the rails, each a band with its cap: every shell, then
	// every steel, then every inlay, so the overlap at the tip stays clean
	const Real W = 14.0f * k;
	const Real tip = L - gap - 16.0f * k;
	const Real back = 0.42f * m.half + 8.0f * k;
	const Real La = (Real)sqrt( back * back + m.half * m.half );
	const Real ox = m.ex + m.ux * tip, oy = m.ey + m.uy * tip;
	Real dirX[ 2 ], dirY[ 2 ], w[ 2 ];
	for( Int a = 0; a < 2; ++a )
	{
		const Real sideSign = a == 0 ? 1.0f : -1.0f;
		const Real es = -back / La, eq = sideSign * m.half / La;
		dirX[ a ] = es * m.ux + eq * lx;
		dirY[ a ] = es * m.uy + eq * ly;
		w[ a ] = lightOf( -dirY[ a ], dirX[ a ] );		// the arm's left
	}
	const Real cap = 0.6f * W;		// 12k * W / 20k
	for( Int pass = 0; pass < 3; ++pass )
	{
		for( Int a = 0; a < 2; ++a )
		{
			if( pass == 0 )
			{
				lineStrip( P_BAND_SHELL, ox, oy, dirX[ a ], dirY[ a ], -0.3f * W, La, 0.5f * W, W, FALSE, TINT_WHITE, FlatAlpha( app ), 20.0f );
				lineStrip( P_CAP_SHELL, ox, oy, dirX[ a ], dirY[ a ], La, La + cap, 0.5f * W, W, TRUE, TINT_WHITE, FlatAlpha( app ), 1000.0f );
			}
			else if( pass == 1 )
			{
				lineStrip( P_BAND_STEEL_B, ox, oy, dirX[ a ], dirY[ a ], -0.3f * W, La, 0.5f * W, W, FALSE, TINT_WHITE, FlatAlpha( app ), 20.0f );
				lineStrip( P_CAP_STEEL_B, ox, oy, dirX[ a ], dirY[ a ], La, La + cap, 0.5f * W, W, TRUE, TINT_WHITE, FlatAlpha( app ), 1000.0f );
				lineStrip( P_BAND_STEEL_A, ox, oy, dirX[ a ], dirY[ a ], -0.3f * W, La, 0.5f * W, W, FALSE, TINT_WHITE, FlatAlpha( app * w[ a ] ), 20.0f );
				lineStrip( P_CAP_STEEL_A, ox, oy, dirX[ a ], dirY[ a ], La, La + cap, 0.5f * W, W, TRUE, TINT_WHITE, FlatAlpha( app * w[ a ] ), 1000.0f );
			}
			else
			{
				lineStrip( P_BAND_INLAY, ox, oy, dirX[ a ], dirY[ a ], -0.3f * W, La, 0.5f * W, W, FALSE, m.tint, FlatAlpha( app ), 20.0f );
				lineStrip( P_CAP_INLAY, ox, oy, dirX[ a ], dirY[ a ], La, La + cap, 0.5f * W, W, TRUE, m.tint, FlatAlpha( app ), 1000.0f );
			}
		}
	}
}

/// a point `a` units of arc past the target on the turn of radius r, and the heading there
static void turnPoint( const Marker &m, Real r, Real a, Real &px, Real &py, Real &heading )
{
	const Real phi = a / r;
	const Real f = r * (Real)sin( phi );
	const Real l = m.side * r * ( 1.0f - (Real)cos( phi ) );
	px = m.x + m.ux * f - m.uy * l;
	py = m.y + m.uy * f + m.ux * l;
	heading = (Real)atan2( m.uy, m.ux ) + m.side * phi;
}

/// the drops on the track, the turn's estimate while aiming, the plane's own circle once it is in it
static void drawDrops( const Marker &m, Real R, Real t, Real app, Real fade )
{
	if( !m.lane || m.drops.empty() )
		return;
	const Real k = m.k, L = m.L, yaw = s_ctx.yaw;
	const Real grow = smoothstep( 0.05f, 0.7f, t );

	if( m.turn && !m.hook )
	{
		Real est = app * smoothstep( 0.6f, 0.9f, grow );
		if( m.pastT )
			est *= 1.0f - smoothstep( 0.0f, 0.3f, secondsSince( s_ctx.now, m.pastTMs ) );
		if( est > 0.01f )
		{
			const Real lastA = m.lastPast, smax = lastA + 30.0f;
			// the fan every radius from rmin to rmax sweeps: narrow at the target, as wide as the late
			// drops can land further round
			if( m.rmax - m.rmin > 1.0f )
			{
				// cells a terrain cell along the arc; across, the fan is narrow near the target and as wide
				// as rmax - rmin only at its far end, so a terrain cell of that width is the most it needs
				const Int cols = MAX( 1, MIN( 128, (Int)ceilf( smax / GROUND_STEP ) ) );
				const Int rows = MAX( 1, MIN( 32, (Int)ceilf( ( m.rmax - m.rmin ) / GROUND_STEP ) ) );
				for( Int i = 0; i < cols; ++i )
				{
					const Real s0 = smax * (Real)i / (Real)cols, s1 = smax * (Real)( i + 1 ) / (Real)cols;
					const UnsignedInt c0 = argb( m.tint, est * clamp01( 1.0f - ( s0 - lastA ) / 30.0f ) );
					const UnsignedInt c1 = argb( m.tint, est * clamp01( 1.0f - ( s1 - lastA ) / 30.0f ) );
					for( Int j = 0; j < rows; ++j )
					{
						const Real r0 = m.rmax - ( m.rmax - m.rmin ) * (Real)j / (Real)rows;
						const Real r1 = m.rmax - ( m.rmax - m.rmin ) * (Real)( j + 1 ) / (Real)rows;
						Real x[ 4 ], y[ 4 ], hdg;
						turnPoint( m, r0, s0, x[ 0 ], y[ 0 ], hdg );
						turnPoint( m, r0, s1, x[ 1 ], y[ 1 ], hdg );
						turnPoint( m, r1, s1, x[ 2 ], y[ 2 ], hdg );
						turnPoint( m, r1, s0, x[ 3 ], y[ 3 ], hdg );
						const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
						const Real pv[ 4 ] = { (Real)j / rows, (Real)j / rows, (Real)( j + 1 ) / rows, (Real)( j + 1 ) / rows };
						const UnsignedInt col[ 4 ] = { c0, c1, c1, c0 };
						emit( P_TURN_BAND, x, y, pu, pv, col );
					}
				}
			}
			const Int dashes = (Int)floorf( smax / ( 22.0f * k ) );
			for( Int j = 0; j < dashes; ++j )
			{
				Real px, py, hdg;
				turnPoint( m, m.rnom, ( (Real)j + 0.5f ) * 22.0f * k, px, py, hdg );
				quadPiece( P_TURN_DASH, px, py, hdg, 14.0f * k, 8.0f * k, m.tint, est );
			}
			for( size_t i = 0; i < m.drops.size(); ++i )
			{
				if( m.drops[ i ] >= 0.0f )
					continue;
				Real px, py, hdg;
				turnPoint( m, m.rnom, -m.drops[ i ], px, py, hdg );
				quadPiece( P_DROP_ESTIMATE, px, py, yaw, 24.0f * k, 24.0f * k, m.tint, est );
			}
		}
	}

	const Int released = m.planesSeen ? MAX( 0, m.initialCarried - m.carried ) : 0;
	if( m.hook )
	{
		// another pass: what is left goes back to hollow on the lane before the target
		for( Int i = 0; i < m.carried && i < (Int)m.drops.size(); ++i )
		{
			const Real d = m.drops[ i ];
			if( d < 0.0f )
				continue;
			quadPiece( P_DROP_ESTIMATE, m.x - m.ux * d, m.y - m.uy * d, yaw, 24.0f * k, 24.0f * k, m.tint, app );
		}
	}
	else
	{
		for( size_t i = 0; i < m.drops.size(); ++i )
		{
			const Real d = m.drops[ i ];
			if( d < 0.0f || (Int)i < released || L - d > grow * ( L + R ) )
				continue;
			quadPiece( P_DROP, m.x - m.ux * d, m.y - m.uy * d, yaw, 24.0f * k, 24.0f * k, m.tint, app );
		}
	}

	if( m.inTurn && !m.hook && m.circleR > 0.0f && m.carried > 0 )
	{
		const Real length = ( (Real)m.carried - 0.5f ) * m.dropSpacing;
		const Real sweep = length / m.circleR;
		const Real from = m.side > 0.0f ? m.planeAngle : m.planeAngle - sweep;
		const Real to = m.side > 0.0f ? m.planeAngle + sweep : m.planeAngle;
		arcStrip( P_RAIL, m.circleX, m.circleY, m.circleR, 8.0f * k, from, to, FALSE, TINT_WHITE, app, FALSE );
		for( Int j = 0; j < m.carried; ++j )
		{
			const Real a = m.planeAngle + m.side * ( (Real)j + 0.5f ) * m.dropSpacing / m.circleR;
			quadPiece( P_DROP, m.circleX + m.circleR * (Real)cos( a ), m.circleY + m.circleR * (Real)sin( a ), yaw,
								 24.0f * k, 24.0f * k, m.tint, app );
		}
	}

	const Tint fallenTint = scaled( m.tint, 0.6f );
	for( size_t i = 0; i < m.fallen.size(); ++i )
		quadPiece( P_DROP, m.fallen[ i ].x, m.fallen[ i ].y, yaw, 18.0f * k, 18.0f * k, fallenTint, 0.5f * fade );
}

/// an additive ping, drawn after every alpha blended piece
struct PingDraw
{
	Real x, y, R, k, age;
	Tint tint;
	const HeightGrid *grid;
	const HeightGrid *water;
};

static void drawMarker( const Marker &m, std::vector<PingDraw> &pings )
{
	if( s_ctx.waterPass && !m.wet )
		return;
	const UnsignedInt now = s_ctx.now;
	const Real t = m.guarding ? 100.0f : secondsSince( now, m.pressMs );
	const Real tl = m.guarding ? 100.0f : ( m.clickMs != 0 ? secondsSince( now, m.clickMs ) : -1.0f );
	const Real fade = fadeOf( m, now );
	const Real app = ( m.guarding ? 1.0f : smoothstep( 0.0f, 0.35f, t ) ) * fade;
	const Real R = currentRadius( m, now );
	const Bool quiet = m.guarding || ( m.family == FAMILY_GUARD && tl >= 1.4f );

	s_ctx.g1 = &m.grid;
	s_ctx.g2 = m.lane ? &m.laneGrid : NULL;
	s_ctx.w1 = &m.water;
	s_ctx.w2 = m.lane ? &m.laneWater : NULL;
	s_ctx.flatZ = m.z;

	if( app > 0.002f )
	{
		if( m.lane )
			drawLane( m, R, t, tl, app );
		if( m.orbit )
			drawOrbit( m, t, ( tl >= 0.0f ? 1.0f : 0.6f ) * app );
		drawZone( m, R, t, tl, quiet, app );
		if( m.super != SUPER_NONE && tl >= 0.0f )
			drawCountdown( m, R, t, fade );
		drawGlyph( m, t, tl, app );
		drawDrops( m, R, t, app, fade );
	}

	for( size_t i = 0; i < m.darts.size(); ++i )
	{
		const Dart &d = m.darts[ i ];
		s_ctx.g1 = &d.grid;
		s_ctx.g2 = NULL;
		s_ctx.w1 = &d.water;
		s_ctx.w2 = NULL;
		drawDart( d.x, d.y, d.heading, d.scale, m.k, m.tint, 1.0f );
	}

	if( tl >= 0.0f && tl < 0.45f && !m.guarding )
	{
		PingDraw p = { m.x, m.y, R, m.k, tl, pingTintOf( m.tint ), &m.grid, &m.water };
		pings.push_back( p );
	}
	if( m.pingMs != 0 )
	{
		const Real age = secondsSince( now, m.pingMs );
		if( age >= 0.0f && age < 0.45f )
		{
			PingDraw p = { m.x, m.y, m.pingR, m.k, age, m.pingTint, &m.grid, &m.water };
			pings.push_back( p );
		}
	}
}

/// a move's or attack's click: the ring closing in 0.32 s with four chevrons on it pointing in, the
/// attack's cross, and the dot that stays for the order ring's 1.5 s
static void drawClick( const ClickMark &c )
{
	s_ctx.g1 = c.deck ? NULL : &c.grid;
	s_ctx.g2 = NULL;
	s_ctx.w1 = NULL;		// a click on water keeps the old marker after the water (takeClicks)
	s_ctx.w2 = NULL;
	s_ctx.flatZ = c.z;
	const Real age = secondsSince( s_ctx.now, c.bornMs );
	if( age < 0.0f || age >= 1.5f )
		return;
	const Real k = CLICK_K, yaw = s_ctx.yaw;
	const Tint &tint = c.attack ? TINT_RED : TINT_WARM;
	if( age < 0.75f )
	{
		const Real rr = 22.0f * k * ( 1.0f + 1.2f * ( 1.0f - easeOut( age / 0.32f ) ) );
		const Real alpha = 1.0f - smoothstep( 0.5f, 0.75f, age );
		const Real size = 54.0f * k * rr / ( 22.0f * k );
		quadPiece( P_ORDER_RING, c.x, c.y, yaw, size, size, tint, alpha );
		for( Int j = 0; j < 4; ++j )
		{
			const Real a = yaw + 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI;
			const Real rt = rr + 14.0f * k;
			quadPiece( P_CHEVRON, c.x + rt * (Real)cos( a ), c.y + rt * (Real)sin( a ), a + MARKER_PI,
								 18.0f * k * 0.9f, 28.0f * k * 0.9f, tint, alpha, CHEVRON_ANCHOR_U, 0.5f );
		}
		if( c.attack )
			quadPiece( P_ORDER_CROSS, c.x, c.y, yaw, 28.0f * k, 28.0f * k, tint, alpha );
	}
	quadPiece( P_ORDER_DOT, c.x, c.y, yaw, 14.0f * k, 14.0f * k, tint, 1.0f - smoothstep( 1.1f, 1.5f, age ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// THE DRAW (terrain pass)
///////////////////////////////////////////////////////////////////////////////////////////////////

/** One pass: in the terrain pass every quad on land (waterPass FALSE), after the water every quad on
	* it (TRUE), at the water's surface, multiplied by the same shroud the terrain pass was. */
static void drawPass( Bool waterPass, Bool shadeByShroud )
{
	if( !s_taken || !theAtlas.ok )
		return;
	if( !s_aimOn && s_shots.empty() && s_guards.empty() && ( waterPass || s_clicks.empty() ) )
		return;
	// not in the water's mirror pass: they would lie on the reflection
	if( ShaderClass::Is_Backface_Culling_Inverted() )
		return;
	s_ctx.waterPass = waterPass;

	// the camera drawing this pass, out of the wrapper's own copy of the view (setGroundOverlayState):
	// its eye for the pull, its right on the ground for the yaw and the light
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
	s_ctx.yaw = (Real)atan2( ry, rx );
	// the screen's left-up on the ground: -0.5 right + 0.62 up, up being the right turned a quarter left
	Real lgx = -0.5f * rx + 0.62f * -ry;
	Real lgy = -0.5f * ry + 0.62f * rx;
	const Real ll = (Real)sqrt( lgx * lgx + lgy * lgy );
	s_ctx.lgx = lgx / ll;
	s_ctx.lgy = lgy / ll;

	W3DShroud *shroud = ( shadeByShroud && TheTerrainRenderObject ) ? TheTerrainRenderObject->getShroud() : NULL;
	TextureClass *shroudTexture = shroud ? shroud->getShroudTexture() : NULL;
	if( shroudTexture == NULL )
		shroud = NULL;

	const Real pull = MARKER_PULL;
	setMarkerState( FALSE, shroudTexture );
	theMarkerQuads.begin( eye, pull, shroud );

	std::vector<PingDraw> pings;
	for( size_t i = 0; i < s_guards.size(); ++i )
		drawMarker( s_guards[ i ], pings );
	for( size_t i = 0; i < s_shots.size(); ++i )
		drawMarker( s_shots[ i ], pings );
	if( s_aimOn )
		drawMarker( s_aim, pings );
	if( !waterPass )
		for( size_t i = 0; i < s_clicks.size(); ++i )
			drawClick( s_clicks[ i ] );
	theMarkerQuads.flush();

	if( !pings.empty() )
	{
		setMarkerState( TRUE, shroudTexture );
		theMarkerQuads.begin( eye, pull, shroud );
		for( size_t i = 0; i < pings.size(); ++i )
		{
			const PingDraw &p = pings[ i ];
			s_ctx.g1 = p.grid;
			s_ctx.g2 = NULL;
			s_ctx.w1 = p.water;
			s_ctx.w2 = NULL;
			const Real e = easeOut( p.age / 0.45f );
			ring( P_PING, p.x, p.y, p.R * ( 1.0f + 0.14f * e ), 6.0f * p.k, p.tint, 0.9f * ( 1.0f - e ), FALSE );
		}
		theMarkerQuads.flush();
	}

	s_ctx.g1 = NULL;
	s_ctx.g2 = NULL;
	s_ctx.w1 = NULL;
	s_ctx.w2 = NULL;
	s_ctx.waterPass = FALSE;
	DX8Wrapper::Set_Texture( 0, NULL );
	DX8Wrapper::Set_Texture( 1, NULL );
	ShaderClass::Invalidate();
}

void W3DGroundMarkers_draw( Bool shadeByShroud )
{
	s_lastShade = shadeByShroud;
	drawPass( FALSE, shadeByShroud );
}

void W3DGroundMarkers_drawOnWater( void )
{
	if( s_wet )
		drawPass( TRUE, s_lastShade );
}
