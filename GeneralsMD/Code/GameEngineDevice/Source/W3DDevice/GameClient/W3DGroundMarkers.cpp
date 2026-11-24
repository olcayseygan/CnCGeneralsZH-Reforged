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
#include "GameLogic/Module/SwarmStrikeSpecialPower.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
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
static const Real MANEUVER_EXIT = 260.0f;			///< how much of a turning drop's straight exit off its circle is drawn
static const Real MISSILE_WAIT = 60.0f;
static const Real CANNON_BEAM_WAIT = 4.0f;				///< seconds past the beam's travel a cannon's marker waits for the beam
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
static const Tint TINT_BLUE = { 0.46f, 0.62f, 1.00f };	///< the EMP: Generals blue lifted to read on sand
static const Tint TINT_TOXIN = { 0.50f, 0.93f, 0.30f };	///< anthrax, the retail cursor's green
static const Tint TINT_GAMMA = { 0.36f, 0.96f, 0.78f };	///< Anthrax Gamma
static const Tint TINT_LED = { 0.22f, 0.88f, 0.80f };		///< Turkiye's LED line, as on its command bar
static const Tint TINT_LACQ = { 0.80f, 0.14f, 0.11f };	///< China's red lacquer
static const Tint TINT_STEEL = { 0.60f, 0.61f, 0.64f };	///< a hairline in plain steel
static const Tint TINT_DARK = { 0.05f, 0.05f, 0.06f };
static const Tint TINT_SCORCH = { 0.10f, 0.08f, 0.06f };
static const Tint TINT_HEAL = { 0.62f, 0.95f, 0.55f };	///< the repair's crosses and teeth
static const Tint TINT_RADAR = { 0.75f, 0.95f, 0.75f };

/** The side whose finish a power's frame wears: America's satin steel, China's crisp terraced
	* chamfer, the GLA's rusty uneven bevel, Turkiye's one flat facet with its turquoise LED line. */
enum Finish { FINISH_USA, FINISH_CHINA, FINISH_GLA, FINISH_TUR };

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
	// every power's own glyph, one picture each (Tools/ground_markers.py GLYPHS)
	P_GLYPH_PARA, P_GLYPH_CRATE, P_GLYPH_LEAFLET, P_GLYPH_VOLLEY, P_GLYPH_BOMBS, P_GLYPH_BLAST,
	P_GLYPH_FLAME, P_GLYPH_SCUD, P_GLYPH_BEAM, P_GLYPH_MISSILE, P_GLYPH_BALLISTIC, P_GLYPH_ARTILLERY,
	P_GLYPH_SWARM, P_GLYPH_DRONES, P_GLYPH_BOLT, P_GLYPH_JAM, P_GLYPH_MINE, P_GLYPH_FRENZY,
	P_GLYPH_WRENCH, P_GLYPH_SATELLITE, P_GLYPH_UAV, P_GLYPH_EYE, P_GLYPH_RADAR, P_GLYPH_SCRAMBLE,
	P_GLYPH_AMBUSH, P_GLYPH_TOXIN, P_GLYPH_CLEAN, P_GLYPH_DEMINE,
	// the sides' bands, four layers each in Finish order after America's (Shell, SteelA, SteelB, Inlay)
	P_SIDE_BANDS,
	P_HAIR = P_SIDE_BANDS + 12, P_SOLID,
	P_STUD_CHINA, P_STUD_GLA, P_STUD_TUR,
	// the parts every power's frame is built of (Tools/ground_markers.py shapes)
	P_SHAPE_TILE, P_SHAPE_DISC, P_SHAPE_TRI, P_SHAPE_PARA, P_SHAPE_PLUS, P_SHAPE_CROSS, P_SHAPE_DIAMOND,
	P_SHAPE_RING, P_SHAPE_TONGUE, P_SHAPE_TANK, P_SHAPE_BLOCK, P_SHAPE_LID,
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
	"MarkerGlyphPara", "MarkerGlyphCrate", "MarkerGlyphLeaflet", "MarkerGlyphVolley", "MarkerGlyphBombs", "MarkerGlyphBlast",
	"MarkerGlyphFlame", "MarkerGlyphScud", "MarkerGlyphBeam", "MarkerGlyphMissile", "MarkerGlyphBallistic", "MarkerGlyphArtillery",
	"MarkerGlyphSwarm", "MarkerGlyphDrones", "MarkerGlyphBolt", "MarkerGlyphJam", "MarkerGlyphMine", "MarkerGlyphFrenzy",
	"MarkerGlyphWrench", "MarkerGlyphSatellite", "MarkerGlyphUav", "MarkerGlyphEye", "MarkerGlyphRadar", "MarkerGlyphScramble",
	"MarkerGlyphAmbush", "MarkerGlyphToxin", "MarkerGlyphClean", "MarkerGlyphDemine",
	"MarkerBandShellChina", "MarkerBandSteelAChina", "MarkerBandSteelBChina", "MarkerBandInlayChina",
	"MarkerBandShellGla", "MarkerBandSteelAGla", "MarkerBandSteelBGla", "MarkerBandInlayGla",
	"MarkerBandShellTur", "MarkerBandSteelATur", "MarkerBandSteelBTur", "MarkerBandInlayTur",
	"MarkerHair", "MarkerSolid",
	"MarkerStudChina", "MarkerStudGla", "MarkerStudTur",
	"MarkerShapeTile", "MarkerShapeDisc", "MarkerShapeTri", "MarkerShapePara", "MarkerShapePlus", "MarkerShapeCross", "MarkerShapeDiamond",
	"MarkerShapeRing", "MarkerShapeTongue", "MarkerShapeTank", "MarkerShapeBlock", "MarkerShapeLid",
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
enum Glyph
{
	GLYPH_NONE, GLYPH_GUARD, GLYPH_DROP, GLYPH_STRIKE, GLYPH_BARRAGE, GLYPH_NUKE, GLYPH_RETICLE,
	// every power's own, after the spinning ones; GLYPH_PARA + i draws P_GLYPH_PARA + i
	GLYPH_PARA, GLYPH_CRATE, GLYPH_LEAFLET, GLYPH_VOLLEY, GLYPH_BOMBS, GLYPH_BLAST,
	GLYPH_FLAME, GLYPH_SCUD, GLYPH_BEAM, GLYPH_MISSILE, GLYPH_BALLISTIC, GLYPH_ARTILLERY,
	GLYPH_SWARM, GLYPH_DRONES, GLYPH_BOLT, GLYPH_JAM, GLYPH_MINE, GLYPH_FRENZY,
	GLYPH_WRENCH, GLYPH_SATELLITE, GLYPH_UAV, GLYPH_EYE, GLYPH_RADAR, GLYPH_SCRAMBLE,
	GLYPH_AMBUSH, GLYPH_TOXIN, GLYPH_CLEAN, GLYPH_DEMINE
};
enum SuperKind { SUPER_NONE, SUPER_MISSILES, SUPER_CANNON };

/** Every ground-aimed power's own marker (the 2026-11-23 decals' unique/DESIGNS.md): its frame, its
	* aim, click and after-click motion.  DESIGN_NONE keeps the zone and glyph (the guard). */
enum Design
{
	DESIGN_NONE,
	DESIGN_PARADROP, DESIGN_CRATE, DESIGN_LEAFLET, DESIGN_A10, DESIGN_FUELAIR, DESIGN_SPYSAT, DESIGN_SPYDRONE,
	DESIGN_REPAIR, DESIGN_SPECTRE, DESIGN_B3, DESIGN_PARTICLE, DESIGN_LASER, DESIGN_CRUISE, DESIGN_AMBULANCE,
	DESIGN_COMANCHE, DESIGN_DEMINE,
	DESIGN_ARTILLERY, DESIGN_MINES, DESIGN_EMP, DESIGN_CCARPET, DESIGN_NAPALM, DESIGN_FRENZY, DESIGN_NEUTRON,
	DESIGN_HNAPALM, DESIGN_HNUKE, DESIGN_TANKDROP,
	DESIGN_SCUD, DESIGN_ANTHRAX, DESIGN_AMBUSH, DESIGN_GPS, DESIGN_RADAR,
	DESIGN_KARGU, DESIGN_UAV, DESIGN_TAYFUN, DESIGN_SWARM, DESIGN_BLIND, DESIGN_RAPID, DESIGN_ALLYAID, DESIGN_COMMANDO
};
/// a general's or a caster's variant of a design
enum Variant { VARIANT_NONE, VARIANT_MOAB, VARIANT_NUKE, VARIANT_CHEM, VARIANT_DEMO, VARIANT_BERET };

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
	Bool fromCaster;					///< the lane starts at the caster (a swarm), and no delivery plane flies it
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
	// the power's own frame (drawDesign): which, in whose finish, a general's variant, a heading where
	// the frame has one and no lane gives it, the caster, and what stands for a drop and a fallen item
	Int design, finish, variant;
	Real hx, hy;
	Real cx0, cy0;
	Int dropPiece, fallenPiece;
	Int swarmCount;
	Real planeSpeed;					///< the delivery plane's cruise speed, nought when the preview had none
	Bool beamSeen;						///< a cannon's beam is on the ground at (beamX, beamY)
	Real beamX, beamY;
	std::vector<Fallen> beamTrail;	///< where it has burned, for the scorch
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
		lane( FALSE ), gap( FALSE ), arrow( FALSE ), exitStub( FALSE ), fromCaster( FALSE ), ex( 0.0f ), ey( 0.0f ), endX( 0.0f ), endY( 0.0f ),
		ux( 1.0f ), uy( 0.0f ), L( 0.0f ), half( 0.0f ), chevUntil( 0.0f ),
		turn( FALSE ), rmin( 0.0f ), rmax( 0.0f ), rnom( 0.0f ), side( 1.0f ), lastPast( 0.0f ),
		lead( INVALID_ID ), planesSeen( FALSE ), initialCarried( 0 ), carried( 0 ), pastT( FALSE ), pastTMs( 0 ),
		hook( FALSE ), inTurn( FALSE ), circleX( 0.0f ), circleY( 0.0f ), circleR( 0.0f ), planeAngle( 0.0f ), dropSpacing( 0.0f ),
		orbit( FALSE ), Ro( 0.0f ), gunship( INVALID_ID ), design( 0 ), finish( 0 ), variant( 0 ), hx( 0.0f ), hy( 0.0f ),
		cx0( 0.0f ), cy0( 0.0f ), dropPiece( P_DROP ), fallenPiece( P_DROP ), swarmCount( 3 ), planeSpeed( 0.0f ), beamSeen( FALSE ), beamX( 0.0f ), beamY( 0.0f ),
		gridMs( 0 ), laneMs( 0 ), zoneWet( FALSE ), laneWet( FALSE ), wet( FALSE )
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

/// whether this power is the caster's SwarmStrikeSpecialPower (the Kargu swarm, the swarm munition)
static Bool findSwarmPower( const Object *caster, const SpecialPowerTemplate *power )
{
	static const NameKeyType key_SwarmStrikeSpecialPower = NAMEKEY( "SwarmStrikeSpecialPower" );
	for( BehaviorModule **m = caster->getBehaviorModules(); *m; ++m )
		if( (*m)->getModuleNameKey() == key_SwarmStrikeSpecialPower
				&& ( (const SpecialPowerModule *)( *m ) )->getSpecialPowerTemplate() == power )
			return TRUE;
	return FALSE;
}

/** How many munitions a swarm power sends, read as SwarmStrikeSpecialPower reads it: the first
	* UpgradeTargetCount whose sciences the caster's player all owns, else TargetCount; read only. */
static Int swarmTargetCount( const Object *caster, const SpecialPowerTemplate *power )
{
	if( caster == NULL || power == NULL )
		return 3;
	const Player *player = caster->getControllingPlayer();
	const ModuleInfo &info = caster->getTemplate()->getBehaviorModuleInfo();
	for( Int i = 0; i < info.getCount(); ++i )
	{
		if( info.getNthName( i ).compareNoCase( "SwarmStrikeSpecialPower" ) != 0 )
			continue;
		const SwarmStrikeSpecialPowerModuleData *data = (const SwarmStrikeSpecialPowerModuleData *)info.getNthData( i );
		if( data == NULL || data->m_specialPowerTemplate != power )
			continue;
		const SwarmStrikeSpecialPowerModuleData::UpgradeTargetCount *up = findFirstOwnedEntry( player, data->m_upgradeTargetCount );
		return up != NULL ? up->m_count : data->m_targetCount;
	}
	return 3;
}

static DeliverPayloadAIUpdate *findDeliverPayload( Object *obj )
{
	static const NameKeyType key_DeliverPayloadAIUpdate = NAMEKEY( "DeliverPayloadAIUpdate" );
	return (DeliverPayloadAIUpdate *)obj->findUpdateModule( key_DeliverPayloadAIUpdate );
}

/** Every power's own glyph, by a piece of its template's name, first match wins: the cursor type is
	* shared (the leaflets, the MOAB and the Kargu swarm all aim with the Daisy Cutter's) and so is the
	* power's enum (the fork's Turkiye powers borrow retail ones), but every general's copy of a power
	* keeps its name after a prefix (AirF_, Nuke_, Infa_, Tank_, SupW_, Lazr_, Slth_, Demo_, Early_). */
struct PowerGlyph
{
	const char *name;
	Glyph glyph;
};
static const PowerGlyph POWER_GLYPHS[] =
{
	{ "SpectreGunship", GLYPH_RETICLE },
	{ "ParticleUplinkCannon", GLYPH_BEAM },	{ "LaserCannon", GLYPH_BEAM },
	{ "CruiseMissile", GLYPH_MISSILE },			{ "TurkiyeTayfun", GLYPH_BALLISTIC },
	{ "NeutronMissile", GLYPH_NUKE },				{ "NukeDrop", GLYPH_NUKE },					{ "HelixNukeBomb", GLYPH_NUKE },
	{ "ScudStorm", GLYPH_SCUD },
	{ "TurkiyeKarguSwarm", GLYPH_DRONES },	{ "TurkiyeSwarmMunition", GLYPH_SWARM },
	{ "TurkiyeElectronicBlinding", GLYPH_JAM },	{ "TurkiyeUavRecon", GLYPH_UAV },
	{ "TurkiyeAllyAid", GLYPH_CRATE },			{ "TurkiyeRapidDeployment", GLYPH_PARA },	{ "TurkiyeCommandoRaid", GLYPH_PARA },
	{ "Paradrop", GLYPH_PARA },							{ "CrateDrop", GLYPH_CRATE },				{ "LeafletDrop", GLYPH_LEAFLET },
	{ "A10Thunderbolt", GLYPH_VOLLEY },			{ "CarpetBomb", GLYPH_BOMBS },			{ "DaisyCutter", GLYPH_BLAST },
	{ "NapalmBomb", GLYPH_FLAME },					{ "NapalmStrike", GLYPH_FLAME },		{ "AnthraxBomb", GLYPH_TOXIN },
	{ "EMPPulse", GLYPH_BOLT },							{ "ClusterMines", GLYPH_MINE },
	{ "ArtilleryBarrage", GLYPH_ARTILLERY },	{ "BattleshipBombardment", GLYPH_ARTILLERY },
	{ "Frenzy", GLYPH_FRENZY },							{ "EmergencyRepair", GLYPH_WRENCH },
	{ "SpySatellite", GLYPH_SATELLITE },		{ "SpyDrone", GLYPH_EYE },					{ "RadarVanScan", GLYPH_RADAR },
	{ "GPSScrambler", GLYPH_SCRAMBLE },			{ "RebelAmbush", GLYPH_AMBUSH },		{ "TerrorCell", GLYPH_AMBUSH },
	{ "AmbulanceCleanupArea", GLYPH_CLEAN },
};

static Glyph powerGlyph( const SpecialPowerTemplate *power )
{
	if( power == NULL )
		return GLYPH_NONE;
	const AsciiString name = power->getName();
	for( Int i = 0; i < (Int)ARRAY_SIZE( POWER_GLYPHS ); ++i )
		if( strstr( name.str(), POWER_GLYPHS[ i ].name ) != NULL )
			return POWER_GLYPHS[ i ].glyph;
	return GLYPH_NONE;
}

struct PowerDesign
{
	const char *name;		///< a part of the power template's name
	Design design;
	Int finish;					///< the side that owns it, when the caster says nothing
};

// first match wins: the longer names before the ones they contain
static const PowerDesign POWER_DESIGNS[] =
{
	{ "SpectreGunship", DESIGN_SPECTRE, FINISH_USA },					{ "ParticleUplinkCannon", DESIGN_PARTICLE, FINISH_USA },
	{ "LaserCannon", DESIGN_LASER, FINISH_USA },							{ "CruiseMissile", DESIGN_CRUISE, FINISH_USA },
	{ "TurkiyeTayfun", DESIGN_TAYFUN, FINISH_TUR },						{ "HelixNukeBomb", DESIGN_HNUKE, FINISH_CHINA },
	{ "NapalmBomb", DESIGN_HNAPALM, FINISH_CHINA },						{ "NeutronMissile", DESIGN_NEUTRON, FINISH_CHINA },
	{ "NukeDrop", DESIGN_NEUTRON, FINISH_GLA },								{ "ScudStorm", DESIGN_SCUD, FINISH_GLA },
	{ "TurkiyeKarguSwarm", DESIGN_KARGU, FINISH_TUR },				{ "TurkiyeSwarmMunition", DESIGN_SWARM, FINISH_TUR },
	{ "TurkiyeElectronicBlinding", DESIGN_BLIND, FINISH_TUR },	{ "TurkiyeUavRecon", DESIGN_UAV, FINISH_TUR },
	{ "TurkiyeAllyAid", DESIGN_ALLYAID, FINISH_TUR },					{ "TurkiyeRapidDeployment", DESIGN_RAPID, FINISH_TUR },
	{ "TurkiyeCommandoRaid", DESIGN_COMMANDO, FINISH_TUR },		{ "TankParadrop", DESIGN_TANKDROP, FINISH_CHINA },
	{ "Paradrop", DESIGN_PARADROP, FINISH_USA },							{ "CrateDrop", DESIGN_CRATE, FINISH_USA },
	{ "LeafletDrop", DESIGN_LEAFLET, FINISH_USA },						{ "A10Thunderbolt", DESIGN_A10, FINISH_USA },
	{ "ChinaCarpetBomb", DESIGN_CCARPET, FINISH_CHINA },			{ "CarpetBomb", DESIGN_B3, FINISH_USA },
	{ "DaisyCutter", DESIGN_FUELAIR, FINISH_USA },						{ "NapalmStrike", DESIGN_NAPALM, FINISH_CHINA },
	{ "AnthraxBomb", DESIGN_ANTHRAX, FINISH_GLA },						{ "EMPPulse", DESIGN_EMP, FINISH_CHINA },
	{ "ClusterMines", DESIGN_MINES, FINISH_CHINA },						{ "ArtilleryBarrage", DESIGN_ARTILLERY, FINISH_CHINA },
	{ "BattleshipBombardment", DESIGN_ARTILLERY, FINISH_USA },	{ "Frenzy", DESIGN_FRENZY, FINISH_CHINA },
	{ "EmergencyRepair", DESIGN_REPAIR, FINISH_USA },					{ "SpySatellite", DESIGN_SPYSAT, FINISH_USA },
	{ "SpyDrone", DESIGN_SPYDRONE, FINISH_USA },							{ "RadarVanScan", DESIGN_RADAR, FINISH_GLA },
	{ "GPSScrambler", DESIGN_GPS, FINISH_GLA },								{ "RebelAmbush", DESIGN_AMBUSH, FINISH_GLA },
	{ "TerrorCell", DESIGN_AMBUSH, FINISH_GLA },							{ "AmbulanceCleanupArea", DESIGN_AMBULANCE, FINISH_USA },
};

static Bool startsWith( const char *s, const char *prefix )
{
	return s != NULL && strncmp( s, prefix, strlen( prefix ) ) == 0;
}

/** The power's own design and the finish it wears.  The finish is the caster's side (a China
	* general's paradrop wears China's), the table's when no caster is known.  The weapon cursors with no
	* power name their design by the cursor: clear mines, and the scatter area of the Comanche's rocket
	* pods or the Maroon Beret's Kargu drones. */
static void pickDesign( Marker &m, const SpecialPowerTemplate *power, const Object *caster )
{
	m.design = DESIGN_NONE;
	m.finish = FINISH_USA;
	m.variant = VARIANT_NONE;
	const char *casterName = caster ? caster->getTemplate()->getName().str() : NULL;
	if( power != NULL )
	{
		const AsciiString name = power->getName();
		for( Int i = 0; i < (Int)ARRAY_SIZE( POWER_DESIGNS ); ++i )
			if( strstr( name.str(), POWER_DESIGNS[ i ].name ) != NULL )
			{
				m.design = POWER_DESIGNS[ i ].design;
				m.finish = POWER_DESIGNS[ i ].finish;
				break;
			}
		if( startsWith( name.str(), "AirF_" ) || startsWith( casterName, "AirF_" ) )
			m.variant = VARIANT_MOAB;
		if( startsWith( name.str(), "Nuke_" ) || startsWith( casterName, "Nuke_" ) )
			m.variant = VARIANT_NUKE;
	}
	else if( m.type == RADIUSCURSOR_CLEARMINES )
		m.design = DESIGN_DEMINE;
	else if( m.type == RADIUSCURSOR_ATTACK_SCATTER_AREA )
	{
		const Bool beret = casterName != NULL && strstr( casterName, "MaroonBeret" ) != NULL;
		m.design = beret ? DESIGN_KARGU : DESIGN_COMANCHE;
		m.finish = beret ? FINISH_TUR : FINISH_USA;
		m.variant = beret ? VARIANT_BERET : VARIANT_NONE;
	}
	if( m.design == DESIGN_NONE && m.type == RADIUSCURSOR_AMBULANCE )
		m.design = DESIGN_AMBULANCE;
	if( startsWith( casterName, "Chem_" ) )
		m.variant = VARIANT_CHEM;
	else if( startsWith( casterName, "Demo_" ) )
		m.variant = VARIANT_DEMO;

	const Player *owner = caster ? caster->getControllingPlayer() : NULL;
	if( owner != NULL )
	{
		const AsciiString base = owner->getBaseSide().isEmpty() ? owner->getSide() : owner->getBaseSide();
		if( startsWith( base.str(), "China" ) )
			m.finish = FINISH_CHINA;
		else if( startsWith( base.str(), "GLA" ) )
			m.finish = FINISH_GLA;
		else if( startsWith( base.str(), "Turk" ) )
			m.finish = FINISH_TUR;
		else if( startsWith( base.str(), "USA" ) || startsWith( base.str(), "America" ) )
			m.finish = FINISH_USA;
	}
}

/// the family, tint and glyph a cursor type draws; describe() then gives the power its own glyph
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
		case RADIUSCURSOR_AMBULANCE:
			m.family = FAMILY_AREA;		m.tint = TINT_WARM;		m.glyph = GLYPH_BARRAGE;
			break;
		case RADIUSCURSOR_CLEARMINES:		// a weapon's cursor: no power to name it
			m.family = FAMILY_AREA;		m.tint = TINT_WARM;		m.glyph = GLYPH_DEMINE;
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
	if( data == NULL )
		return;
	// The orbit is the gunship's own GunshipOrbitRadius (SpectreGunshipUpdate, 250 on every retail
	// gunship).  The deployment module's field of that name has no INI key and is zero; it is only the
	// push out of the corner below, read as SpectreGunshipDeploymentUpdate::initiateIntentToDoSpecialPower
	// reads it
	const SpectreGunshipUpdateModuleData *ship = (const SpectreGunshipUpdateModuleData *)findTemplateModule(
		TheThingFactory->findTemplate( data->m_gunshipTemplateName ), "SpectreGunshipUpdate" );
	if( ship == NULL || ship->m_gunshipOrbitRadius <= 0.0f )
		return;
	m.orbit = TRUE;
	m.Ro = ship->m_gunshipOrbitRadius;
	m.k = bandUnit( m.Ro );
	const Real push = data->m_gunshipOrbitRadius;

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
	if( !setLaneEnds( m, m.x - nx * ( dist + push ), m.y - ny * ( dist + push ), m.x - nx * m.Ro, m.y - ny * m.Ro ) )
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
	m.fromCaster = FALSE;
	m.planeSpeed = 0.0f;
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
	if( findSwarmPower( caster, power ) )
	{
		// the Kargu drones scramble off the caster's own runways and the swarm munition flies from it,
		// so the run starts at the building, not at the map's edge, and ends in the dive
		if( setLaneEnds( m, caster->getPosition()->x, caster->getPosition()->y, m.x, m.y ) )
		{
			m.exitStub = FALSE;
			m.fromCaster = TRUE;
		}
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
	m.planeSpeed = speed;

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

/// a unit heading from (fx, fy) to (tx, ty) into the marker's own, nought when they meet
static void setHeading( Marker &m, Real fx, Real fy, Real tx, Real ty )
{
	const Real dx = tx - fx, dy = ty - fy;
	const Real l = (Real)sqrt( dx * dx + dy * dy );
	m.hx = l > 1.0f ? dx / l : 0.0f;
	m.hy = l > 1.0f ? dy / l : 0.0f;
}

/** What a design changes in the marker: its tint where it is not its family's, what stands for a drop
	* and an item that fell, the arrow at the rim (a frame that is no circle has none), and a heading:
	* the shells' flight from the far corner, the airlift's from the nearest edge, a line from the caster. */
static void describeDesign( Marker &m, const Object *caster, const SpecialPowerTemplate *power )
{
	m.hx = 0.0f;
	m.hy = 0.0f;
	m.dropPiece = P_DROP;
	m.fallenPiece = P_DROP;
	if( caster )
	{
		m.cx0 = caster->getPosition()->x;
		m.cy0 = caster->getPosition()->y;
	}
	else
	{
		m.cx0 = m.x - 300.0f;
		m.cy0 = m.y - 300.0f;
	}
	// a missile out of the caster's own silo (the cruise missile, the nuke, the Scud Storm) flies from
	// there, never from the map's edge the OCL module's default create location names
	if( m.lane && caster && findTemplateModule( caster->getTemplate(), "MissileLauncherBuildingUpdate" ) != NULL )
		m.lane = FALSE;
	Coord3D target;
	target.x = m.x;  target.y = m.y;  target.z = m.z;
	switch( m.design )
	{
		case DESIGN_PARADROP:	m.fallenPiece = P_SHAPE_RING;	break;
		case DESIGN_CRATE:		m.dropPiece = P_SHAPE_DISC;	m.fallenPiece = P_SHAPE_PLUS;	m.arrow = FALSE;	break;
		case DESIGN_LEAFLET:	m.fallenPiece = P_SHAPE_DISC;	break;
		case DESIGN_A10:			m.arrow = FALSE;	break;
		case DESIGN_B3:				m.dropPiece = -1;	m.fallenPiece = P_SHAPE_CROSS;	m.arrow = FALSE;	m.tint = TINT_ORANGE;	break;
		case DESIGN_CCARPET:
			m.dropPiece = P_SHAPE_DIAMOND;
			m.fallenPiece = P_SHAPE_CROSS;
			m.arrow = FALSE;
			m.tint = m.variant == VARIANT_NUKE ? TINT_RED : TINT_ORANGE;
			break;
		case DESIGN_NAPALM:		m.arrow = FALSE;	break;
		case DESIGN_TANKDROP:	m.dropPiece = P_SHAPE_DISC;	m.fallenPiece = P_SHAPE_TANK;	m.arrow = FALSE;	break;
		case DESIGN_RAPID:		m.dropPiece = P_SHAPE_PARA;	m.fallenPiece = P_SHAPE_PARA;	m.arrow = FALSE;	m.tint = TINT_GOLD;	break;
		case DESIGN_COMMANDO:	m.dropPiece = P_SHAPE_PARA;	m.fallenPiece = P_SHAPE_DISC;	m.arrow = FALSE;	m.tint = TINT_GOLD;	break;
		case DESIGN_ALLYAID:
			m.tint = TINT_GOLD;
			if( caster )
			{
				const Coord3D edge = TheTerrainLogic->findClosestEdgePoint( caster->getPosition() );
				setHeading( m, edge.x, edge.y, m.x, m.y );
			}
			break;
		case DESIGN_ARTILLERY:
		{
			const Coord3D corner = TheTerrainLogic->findFarthestEdgePoint( &target );
			setHeading( m, corner.x, corner.y, m.x, m.y );
			break;
		}
		case DESIGN_COMANCHE:
		case DESIGN_CRUISE:
			setHeading( m, m.cx0, m.cy0, m.x, m.y );
			m.tint = m.design == DESIGN_CRUISE ? TINT_RED : TINT_AMBER;
			break;
		case DESIGN_EMP:			m.tint = TINT_BLUE;	break;
		case DESIGN_ANTHRAX:	m.tint = m.variant == VARIANT_CHEM ? TINT_GAMMA : TINT_TOXIN;	break;
		case DESIGN_KARGU:		m.tint = TINT_ORANGE;	break;
		case DESIGN_SWARM:
			m.tint = TINT_ORANGE;
			m.swarmCount = swarmTargetCount( caster, power );
			break;
		case DESIGN_LASER:
		case DESIGN_TAYFUN:
		case DESIGN_PARTICLE:	m.tint = TINT_RED;	break;
		case DESIGN_BLIND:		m.tint = TINT_AMBER;	break;
		case DESIGN_UAV:
		case DESIGN_SPYSAT:
		case DESIGN_SPYDRONE:
		case DESIGN_RADAR:
		case DESIGN_GPS:
		case DESIGN_AMBULANCE:
		case DESIGN_DEMINE:
		case DESIGN_REPAIR:		m.tint = TINT_WARM;	break;
		default:	break;
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
	// the power's own glyph; the leaflets are a drop, though they aim with the Daisy Cutter's cursor
	const Glyph own = powerGlyph( aim.power );
	if( own != GLYPH_NONE )
		m.glyph = own;
	if( own == GLYPH_LEAFLET )
	{
		m.family = FAMILY_DROP;
		m.tint = TINT_GOLD;
		m.glyphS = 0.27f * aim.radius;
	}

	pickDesign( m, aim.power, caster );
	describeDesign( m, caster, aim.power );

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
		// as long as a slow plane would take to come in from its edge over the whole run, never less
		// than PLANE_WAIT: a far target is not given up while its planes are still on their way in
		// planeSpeed is the locomotor's, per logic frame; a tick is a wall 1/LOGICFRAMES_PER_SECOND
		const Real speed = m.planeSpeed > 0.01f ? m.planeSpeed * (Real)LOGICFRAMES_PER_SECOND : 60.0f;
		const Real wait = MAX( PLANE_WAIT, 1.5f * ( m.L + 600.0f ) / speed + 3.0f );
		if( tl > wait )
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
		m.pingTint = m.tint;
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
	if( m.super == SUPER_CANNON )
	{
		// the beam itself, while its cannon says it burns (ParticleUplinkCannonUpdate::getBurningSpot,
		// const): the marker's head and scorch follow it, and it stays until the beam is gone
		static const NameKeyType key_ParticleUplinkCannonUpdate = NAMEKEY( "ParticleUplinkCannonUpdate" );
		const Object *caster = TheGameLogic->findObjectByID( m.caster );
		const ParticleUplinkCannonUpdate *cannon = caster ? (const ParticleUplinkCannonUpdate *)caster->findUpdateModule( key_ParticleUplinkCannonUpdate ) : NULL;
		Coord3D spot;
		Real radius = 0.0f;
		if( cannon != NULL && cannon->getBurningSpot( &spot, &radius ) )
		{
			m.beamSeen = TRUE;
			m.beamX = spot.x;
			m.beamY = spot.y;
			const Fallen *last = m.beamTrail.empty() ? NULL : &m.beamTrail.back();
			if( ( last == NULL || ( last->x - spot.x ) * ( last->x - spot.x ) + ( last->y - spot.y ) * ( last->y - spot.y ) > 16.0f )
					&& m.beamTrail.size() < 400 )
			{
				Fallen f;
				f.x = spot.x;
				f.y = spot.y;
				m.beamTrail.push_back( f );
			}
			m.fadeMs = 0;		// burning: no fade yet, whatever the impact said
		}
		else if( m.beamSeen && m.fadeMs == 0 )
			startFade( m, now, END_FADE );
		else if( !m.beamSeen && m.impactMs != 0 && secondsSince( now, m.impactMs ) > CANNON_BEAM_WAIT )
			startFade( m, now, END_FADE );		// the beam never came: a cannon that lost its power
	}
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
			if( obj->getID() < m.firstNewID )
				continue;
			// a nuke's, a Scud's or the cruise missile's (NeutronMissileUpdate, fired by the silo's OCL as a
			// temporary weapon, so it may stand on no team of the player's: the silo as producer says whose),
			// or any other projectile the silo fired
			const Bool fromSilo = m.caster != INVALID_ID && obj->getProducerID() == m.caster;
			if( !fromSilo && !obj->isLocallyControlled() )
				continue;
			const Bool missileUpdate = obj->findUpdateModule( key_NeutronMissileUpdate ) != NULL;
			if( ( !missileUpdate && !( fromSilo && obj->isKindOf( KINDOF_PROJECTILE ) ) ) || isClaimed( obj->getID() ) )
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
			// a missile that has gone off may linger a while dead (its death modules) before it is removed
			if( obj == NULL || obj->isEffectivelyDead() )
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
		m.pingTint = TINT_GOLD;
		// gone between 0.1 and 0.5 s after it lands; a cannon waits for its beam (above), which in a
		// network game the run-ahead can show after the travel frames are counted
		if( m.super != SUPER_CANNON || m.beamSeen )
			startFade( m, now + 100, 0.4f );
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
		reach = MAX( reach, 2.0f * m.rmax + MANEUVER_EXIT + 40.0f );		// the turn and the straight exit off it (drawManeuver)
	if( m.design != DESIGN_NONE )		// a capsule, a wide box, a racetrack: the frames reach past the radius
		reach = MAX( reach, 1.8f * R + 40.0f * m.k );
	if( m.beamSeen )		// a driven beam goes where the player steers it: its head and every scorch it left
	{
		Real far2 = ( m.beamX - m.x ) * ( m.beamX - m.x ) + ( m.beamY - m.y ) * ( m.beamY - m.y );
		for( size_t i = 0; i < m.beamTrail.size(); ++i )
			far2 = MAX( far2, ( m.beamTrail[ i ].x - m.x ) * ( m.beamTrail[ i ].x - m.x ) + ( m.beamTrail[ i ].y - m.y ) * ( m.beamTrail[ i ].y - m.y ) );
		reach = MAX( reach, (Real)sqrt( far2 ) + 20.0f * m.k );
	}
	// a square wider than MAX_GRID_SIDE heights would be cut short by fillSquare and refilled every take
	return MIN( reach + 4.0f, 0.5f * GROUND_STEP * (Real)( MAX_GRID_SIDE - 1 ) - GRID_PAD - 2.0f * GROUND_STEP );
}

/// a frame with a line from its caster and no lane to lay it on: the cruise missile's route, the rocket pods' guide
static Bool designDrawsRoute( const Marker &m )
{
	return !m.lane && ( m.design == DESIGN_CRUISE || m.design == DESIGN_COMANCHE );
}

/// seconds an area power's own marker holds after the click before its fade: as long as its after-click runs
static Real designHold( const Marker &m )
{
	switch( m.design )
	{
		case DESIGN_NONE:			return AREA_HOLD;
		case DESIGN_EMP:
		case DESIGN_SPYDRONE:
		case DESIGN_COMANCHE:
		case DESIGN_AMBUSH:
		case DESIGN_KARGU:
		case DESIGN_HNAPALM:
		case DESIGN_HNUKE:		return 2.0f;
		case DESIGN_ARTILLERY:
		case DESIGN_MINES:
		case DESIGN_REPAIR:
		case DESIGN_AMBULANCE:
		case DESIGN_UAV:
		case DESIGN_SWARM:		return 3.0f;
		default:							return 2.5f;
	}
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
	if( m.lane )
	{
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
		return;
	}
	if( !designDrawsRoute( m ) )
		return;
	// a route from the caster with no lane (the cruise missile's from its launcher, bent up to 110 units
	// either side; the Comanche's guide line): the lane grids hold its ground, a terrain cell apart across
	const Real across = m.design == DESIGN_CRUISE ? 135.0f : 24.0f;
	const Real dx = m.x - m.cx0, dy = m.y - m.cy0;
	const Real length = (Real)sqrt( dx * dx + dy * dy );
	if( length < 1.0f )
		return;
	const Real ux = dx / length, uy = dy / length;
	const Real key[ 5 ] = { m.cx0, m.cy0, m.x, m.y, across };
	Bool same = m.laneGrid.valid() && now - m.laneMs <= 3000;
	for( Int i = 0; same && i < 5; ++i )
		same = fabs( key[ i ] - m.laneKey[ i ] ) < 2.0f;
	if( !same )
	{
		const Real x0 = m.cx0 + uy * across, y0 = m.cy0 - ux * across;		// the caster, across to the right
		const Int rows = MIN( 32, (Int)ceilf( 2.0f * across / GROUND_STEP ) + 1 );
		const Int wetRows = MIN( 16, (Int)ceilf( 2.0f * across / WATER_STEP ) + 1 );
		m.laneGrid.fill( x0, y0, ux, uy, GROUND_STEP, MIN( 1000, (Int)ceilf( length / GROUND_STEP ) + 2 ), 2.0f * across / (Real)( rows - 1 ), rows );
		m.laneWet = m.laneWater.fill( x0, y0, ux, uy, WATER_STEP, MIN( 500, (Int)ceilf( length / WATER_STEP ) + 2 ), 2.0f * across / (Real)( wetRows - 1 ), wetRows, TRUE );
		for( Int i = 0; i < 5; ++i )
			m.laneKey[ i ] = key[ i ];
		m.laneMs = now;
	}
	m.wet = m.wet || m.laneWet;
}

/** A barrage from the far corner: the shells' carriers its OCL sent (DeliverPayload objects with the
	* caster as producer, gone once they drop, SelfDestructObject).  The marker holds while any is on its
	* way, which is about ten seconds, and the impacts start when the last has dropped (impactMs). */
static void followCarriers( Marker &m, UnsignedInt now, Real tl )
{
	if( !m.planesSeen )
	{
		if( tl > PLANE_WAIT )
		{
			startFade( m, now, AREA_FADE );		// no carrier seen: the power's own ten seconds are up
			return;
		}
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			if( obj->getID() < m.firstNewID || !obj->isLocallyControlled() )
				continue;
			if( m.caster != INVALID_ID && obj->getProducerID() != m.caster )
				continue;
			DeliverPayloadAIUpdate *ai = findDeliverPayload( obj );
			if( ai == NULL || isClaimed( obj->getID() ) )
				continue;
			m.planes.push_back( obj->getID() );
		}
		if( m.planes.empty() )
			return;
		m.planesSeen = TRUE;
	}
	if( m.impactMs != 0 )
		return;
	for( size_t i = 0; i < m.planes.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m.planes[ i ] );
		if( obj != NULL && !obj->isEffectivelyDead() )
			return;
	}
	m.impactMs = now;		// the last shell is falling: the impacts run, then the fade
	startFade( m, now + 2400, AREA_FADE );
}

static void updateShot( Marker &m, UnsignedInt now, InGameUI *ui )
{
	const Real tl = secondsSince( now, m.clickMs );
	m.darts.clear();
	m.wet = FALSE;		// the darts and takeGround say it again
	if( m.lane && m.family != FAMILY_SPECTRE && !m.fromCaster )
		followPlanes( m, now, tl );
	else if( m.family == FAMILY_SPECTRE )
		followGunship( m, now, tl );
	else if( m.super != SUPER_NONE )
		followSuperweapon( m, now, tl );
	else if( m.family == FAMILY_GUARD )
		followGuard( m, now, ui, tl );
	else if( m.design == DESIGN_ARTILLERY )
		followCarriers( m, now, tl );
	else
		startFade( m, m.clickMs + (UnsignedInt)( designHold( m ) * 1000.0f ), AREA_FADE );

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
	// once, on the first take under the Reforged look (the shell already, so InGameUI knows before the
	// first aim or delivery); a Classic player never pays the sheet's 11 MB, nor its managed copy
	if( TheGlobalData != NULL && !TheGlobalData->m_classicGraphics )
		loadAtlas();
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
	* first set and the shroud's on the second.  Quads are kept until flush, which draws them in batches,
	* so the additive ones can be gathered while the alpha blended ones are laid and drawn after them. */
class MarkerQuads
{
public:
	MarkerQuads( void ) : m_pull( 0.0f ), m_shrouded( FALSE ),
		m_shroudOffsetX( 0.0f ), m_shroudOffsetY( 0.0f ), m_shroudScaleX( 0.0f ), m_shroudScaleY( 0.0f ) { }

	void begin( const Vector3 &eye, Real pull, W3DShroud *shroud )
	{
		m_verts.clear();
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

	void flush( void );

private:
	enum { BATCH_QUADS = 4096 };		///< 16384 vertices, well inside an unsigned short index
	struct Vert
	{
		Vector3 pos;
		UnsignedInt diffuse;
		Real u, v;
	};
	void drawBatch( Int first, Int quads );
	std::vector<Vert> m_verts;
	Vector3 m_eye;
	Real m_pull;
	Bool m_shrouded;
	Real m_shroudOffsetX, m_shroudOffsetY, m_shroudScaleX, m_shroudScaleY;
};

void MarkerQuads::flush( void )
{
	const Int total = (Int)m_verts.size() / 4;
	for( Int first = 0; first < total; first += BATCH_QUADS )
		drawBatch( first, MIN( (Int)BATCH_QUADS, total - first ) );
	m_verts.clear();
}

void MarkerQuads::drawBatch( Int first, Int quads )
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
static MarkerQuads theAddQuads;		///< the additive ones: pings, glows, beam heads; drawn after the rest

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
	Bool additive;					///< what is laid now goes to theAddQuads
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
	( s_ctx.additive ? theAddQuads : theMarkerQuads ).add( p, u, v, col );
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

/** A band round (cx, cy) whose radius the caller gives at every angle: scallops, a zigzag, a churning
	* cloud's edge, a ragged ring, a gear's teeth.  At most maxStepDeg a quad (a zigzag of 24 teeth wants
	* two degrees) and GROUND_STEP along the ground; lit pairs take the light from the radial. */
template< class RadiusAt >
static void radialStrip( Int piece, Real cx, Real cy, const RadiusAt &radiusAt, Real across, Real a0, Real a1,
												 const Tint &tint, Real alpha, Bool lit, Real maxStepDeg = 2.0f )
{
	const Real span = a1 - a0;
	if( alpha <= 0.002f || span <= 0.0f )
		return;
	const Real rGuess = MAX( 1.0f, radiusAt( a0 ) );
	Int n = MAX( (Int)ceilf( span / ( maxStepDeg * MARKER_DEG ) ), (Int)ceilf( span * rGuess / GROUND_STEP ) );
	n = MAX( 1, MIN( 1440, n ) );
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	Real ta = a0, ra = radiusAt( ta );
	for( Int i = 0; i < n; ++i )
	{
		const Real tb = a0 + span * (Real)( i + 1 ) / (Real)n;
		const Real rb = radiusAt( tb );
		const Real ca = (Real)cos( ta ), sa = (Real)sin( ta );
		const Real cb = (Real)cos( tb ), sb = (Real)sin( tb );
		const Real roa = ra + 0.5f * across, ria = MAX( 0.0f, ra - 0.5f * across );
		const Real rob = rb + 0.5f * across, rib = MAX( 0.0f, rb - 0.5f * across );
		const Real x[ 4 ] = { cx + roa * ca, cx + rob * cb, cx + rib * cb, cx + ria * ca };
		const Real y[ 4 ] = { cy + roa * sa, cy + rob * sb, cy + rib * sb, cy + ria * sa };
		const Real wa = lit ? lightOf( ca, sa ) : 1.0f;
		const Real wb = lit ? lightOf( cb, sb ) : 1.0f;
		const UnsignedInt col[ 4 ] = { argb( tint, alpha * wa ), argb( tint, alpha * wb ), argb( tint, alpha * wb ), argb( tint, alpha * wa ) };
		emit( piece, x, y, pu, pv, col );
		ta = tb;
		ra = rb;
	}
}

/** A band along a polyline, row 0 on the right of the way it runs (outside a counter-clockwise
	* polygon), its corners mitred so a hexagon or a stepped box stays one band; lit pairs take the
	* light from each edge's right normal.  Each edge is cut into quads no longer than GROUND_STEP. */
static void polyStrip( Int piece, const Real *px, const Real *py, Int n, Bool closed, Real across,
											 const Tint &tint, Real alpha, Bool lit, Real shift = 0.0f )
{
	if( alpha <= 0.002f || n < 2 )
		return;
	enum { MAX_POINTS = 96 };
	n = MIN( n, (Int)MAX_POINTS );
	Real ox[ MAX_POINTS ], oy[ MAX_POINTS ];		// each corner's outward offset for half the width
	const Real half = 0.5f * across;
	for( Int i = 0; i < n; ++i )
	{
		const Bool hasIn = closed || i > 0;
		const Bool hasOut = closed || i < n - 1;
		const Int ip = ( i + n - 1 ) % n, in = ( i + 1 ) % n;
		Real nx = 0.0f, ny = 0.0f, ix = 0.0f, iy = 0.0f, qx = 0.0f, qy = 0.0f;
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
		nx = ix + qx;
		ny = iy + qy;
		const Real nl = (Real)sqrt( nx * nx + ny * ny );
		if( nl < 1.0e-4f )
		{
			nx = qx;  ny = qy;
		}
		else
		{
			nx /= nl;  ny /= nl;
		}
		// the mitre: as long as it must be for the edges either side to keep their width, within reason
		const Real cosine = MAX( 0.35f, nx * qx + ny * qy );
		ox[ i ] = nx * half / cosine;
		oy[ i ] = ny * half / cosine;
	}
	const Real pv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	const Int edges = closed ? n : n - 1;
	for( Int e = 0; e < edges; ++e )
	{
		const Int a = e, b = ( e + 1 ) % n;
		const Real dx = px[ b ] - px[ a ], dy = py[ b ] - py[ a ];
		const Real len = (Real)sqrt( dx * dx + dy * dy );
		if( len < 1.0e-3f )
			continue;
		const Real w = lit ? lightOf( dy / len, -dx / len ) : 1.0f;
		const UnsignedInt c = argb( tint, alpha * w );
		const UnsignedInt col[ 4 ] = { c, c, c, c };
		const Int m = MAX( 1, MIN( 200, (Int)ceilf( len / GROUND_STEP ) ) );
		const Real s = half > 1.0e-4f ? shift / half : 0.0f;		// the centre line moved outward by `shift`
		for( Int j = 0; j < m; ++j )
		{
			const Real f0 = (Real)j / (Real)m, f1 = (Real)( j + 1 ) / (Real)m;
			const Real o0x = ox[ a ] + ( ox[ b ] - ox[ a ] ) * f0, o0y = oy[ a ] + ( oy[ b ] - oy[ a ] ) * f0;
			const Real o1x = ox[ a ] + ( ox[ b ] - ox[ a ] ) * f1, o1y = oy[ a ] + ( oy[ b ] - oy[ a ] ) * f1;
			const Real cx0 = px[ a ] + dx * f0 + o0x * s, cy0 = py[ a ] + dy * f0 + o0y * s;
			const Real cx1 = px[ a ] + dx * f1 + o1x * s, cy1 = py[ a ] + dy * f1 + o1y * s;
			const Real x[ 4 ] = { cx0 + o0x, cx1 + o1x, cx1 - o1x, cx0 - o0x };
			const Real y[ 4 ] = { cy0 + o0y, cy1 + o1y, cy1 - o1y, cy0 - o0y };
			emit( piece, x, y, pu, pv, col );
		}
	}
}

/// the piece a flat fill samples: opaque white
static Int solidPiece( void )
{
	return P_SOLID;
}

/** A flat fill: a fan from (cx, cy) out to the outline, its alpha going from alphaCentre at the middle
	* to alphaEdge at the outline, cut into quads a terrain cell at most so it lies on the ground.  A
	* radar's afterglow, a camera's footprint, a gas cloud's haze, a scorch; additive (s_ctx.additive)
	* with alphaEdge nought it is a soft glow. */
static void fillFan( Real cx, Real cy, const Real *px, const Real *py, Int n, Bool closed, const Tint &tint,
										 Real alphaCentre, Real alphaEdge )
{
	if( n < 2 || ( alphaCentre <= 0.002f && alphaEdge <= 0.002f ) )
		return;
	const Int piece = solidPiece();
	const Real pu[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	const Real pv[ 4 ] = { 0.5f, 0.5f, 0.5f, 0.5f };
	const Int edges = closed ? n : n - 1;
	for( Int e = 0; e < edges; ++e )
	{
		const Int a = e, b = ( e + 1 ) % n;
		const Real ra = (Real)sqrt( ( px[ a ] - cx ) * ( px[ a ] - cx ) + ( py[ a ] - cy ) * ( py[ a ] - cy ) );
		const Real rb = (Real)sqrt( ( px[ b ] - cx ) * ( px[ b ] - cx ) + ( py[ b ] - cy ) * ( py[ b ] - cy ) );
		const Real el = (Real)sqrt( ( px[ b ] - px[ a ] ) * ( px[ b ] - px[ a ] ) + ( py[ b ] - py[ a ] ) * ( py[ b ] - py[ a ] ) );
		const Int rings = MAX( 1, MIN( 64, (Int)ceilf( MAX( ra, rb ) / GROUND_STEP ) ) );
		const Int along = MAX( 1, MIN( 64, (Int)ceilf( el / GROUND_STEP ) ) );
		for( Int r = 0; r < rings; ++r )
		{
			const Real f0 = (Real)r / (Real)rings, f1 = (Real)( r + 1 ) / (Real)rings;
			const UnsignedInt c0 = argb( tint, alphaCentre + ( alphaEdge - alphaCentre ) * f0 );
			const UnsignedInt c1 = argb( tint, alphaCentre + ( alphaEdge - alphaCentre ) * f1 );
			const UnsignedInt col[ 4 ] = { c0, c0, c1, c1 };
			for( Int s = 0; s < along; ++s )
			{
				const Real e0 = (Real)s / (Real)along, e1 = (Real)( s + 1 ) / (Real)along;
				const Real ax = px[ a ] + ( px[ b ] - px[ a ] ) * e0 - cx, ay = py[ a ] + ( py[ b ] - py[ a ] ) * e0 - cy;
				const Real bx = px[ a ] + ( px[ b ] - px[ a ] ) * e1 - cx, by = py[ a ] + ( py[ b ] - py[ a ] ) * e1 - cy;
				const Real x[ 4 ] = { cx + ax * f0, cx + bx * f0, cx + bx * f1, cx + ax * f1 };
				const Real y[ 4 ] = { cy + ay * f0, cy + by * f0, cy + by * f1, cy + ay * f1 };
				emit( piece, x, y, pu, pv, col );
			}
		}
	}
}

/// a disc as a fan of `sides`
static void fillDisc( Real cx, Real cy, Real r, const Tint &tint, Real alphaCentre, Real alphaEdge, Int sides = 24 )
{
	enum { MAX_SIDES = 48 };
	sides = MAX( 6, MIN( (Int)MAX_SIDES, sides ) );
	Real x[ MAX_SIDES ], y[ MAX_SIDES ];
	for( Int i = 0; i < sides; ++i )
	{
		const Real a = 2.0f * MARKER_PI * (Real)i / (Real)sides;
		x[ i ] = cx + r * (Real)cos( a );
		y[ i ] = cy + r * (Real)sin( a );
	}
	fillFan( cx, cy, x, y, sides, TRUE, tint, alphaCentre, alphaEdge );
}

/// a soft additive glow: bright in the middle, nothing at r
static void glowDisc( Real cx, Real cy, Real r, const Tint &tint, Real alpha )
{
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	fillDisc( cx, cy, r, tint, alpha, 0.0f );
	s_ctx.additive = was;
}

/// a ping: a thin additive ring growing a little and fading, `age` seconds into its `life`
static void pingRing( Real cx, Real cy, Real r, Real k, Real age, const Tint &tint, Real life = 0.45f, Real grow = 0.14f, Real width = 6.0f )
{
	if( age < 0.0f || age >= life )
		return;
	const Real e = easeOut( age / life );
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	ring( P_PING, cx, cy, r * ( 1.0f + grow * e ), width * k, pingTintOf( tint ), 0.9f * ( 1.0f - e ), FALSE );
	s_ctx.additive = was;
}

/// a side's band layer: 0 shell, 1 steel lit from outside, 2 steel lit from inside, 3 inlay
static Int bandPiece( Int finish, Int layer )
{
	if( finish == FINISH_USA )
	{
		static const Int usa[ 4 ] = { P_BAND_SHELL, P_BAND_STEEL_A, P_BAND_STEEL_B, P_BAND_INLAY };
		return usa[ layer ];
	}
	return P_SIDE_BANDS + ( finish - 1 ) * 4 + layer;
}

static Int studPiece( Int finish )
{
	switch( finish )
	{
		case FINISH_CHINA:	return P_STUD_CHINA;
		case FINISH_GLA:		return P_STUD_GLA;
		case FINISH_TUR:		return P_STUD_TUR;
		default:						return P_STUD;
	}
}

/** The thin and refined look (the owner's pick, 2026-11-24): every power's band is drawn at BAND_THIN
	* of the width its design names, its dark shell only a faint shadow (SHELL_FAINT), and its coloured
	* inlay from a strip INLAY_WIDE times as wide as the steel's, so the colour keeps its presence on the
	* thinner steel.  The guard's zone and the lanes keep their own widths. */
static const Real BAND_THIN = 0.5f;
static const Real SHELL_FAINT = 0.28f;
static const Real INLAY_WIDE = 1.8f;
static const Real LED_SHIFT = -0.19f;		///< Turkiye's LED line, times the steel's width, from the band's middle
static const Real LED_WIDTH = 0.06f;

/// the three tiers round a circle from a0 to a1, in a side's finish; the inlay glows by `glow`
static void bandArc( Int finish, Real cx, Real cy, Real r, Real across, Real a0, Real a1, const Tint &tint, Real alpha,
										 Real glow = 1.0f, Bool inlay = TRUE )
{
	const Real s = BAND_THIN * across;
	arcStrip( bandPiece( finish, 0 ), cx, cy, r, s, a0, a1, FALSE, TINT_WHITE, SHELL_FAINT * alpha, FALSE );
	arcStrip( bandPiece( finish, 2 ), cx, cy, r, s, a0, a1, FALSE, TINT_WHITE, alpha, FALSE );
	arcStrip( bandPiece( finish, 1 ), cx, cy, r, s, a0, a1, FALSE, TINT_WHITE, alpha, TRUE );
	if( inlay )
		arcStrip( bandPiece( finish, 3 ), cx, cy, r, INLAY_WIDE * s, a0, a1, FALSE, scaled( tint, glow ), alpha, FALSE );
	if( finish == FINISH_TUR )
		arcStrip( P_HAIR, cx, cy, r + LED_SHIFT * s, LED_WIDTH * s * 4.0f, a0, a1, FALSE, TINT_LED, alpha, FALSE );
}

static void bandCircle( Int finish, Real cx, Real cy, Real r, Real across, const Tint &tint, Real alpha, Real glow = 1.0f )
{
	bandArc( finish, cx, cy, r, across, 0.0f, 2.0f * MARKER_PI, tint, alpha, glow );
}

template< class RadiusAt >
struct ShiftedRadius
{
	const RadiusAt &f;
	Real d;
	ShiftedRadius( const RadiusAt &fn, Real by ) : f( fn ), d( by ) { }
	Real operator()( Real a ) const { return f( a ) + d; }
};

/// the three tiers round a radius profile (radialStrip), in a side's finish
template< class RadiusAt >
static void bandRadial( Int finish, Real cx, Real cy, const RadiusAt &radiusAt, Real across, const Tint &tint, Real alpha,
												Real glow = 1.0f, Real stepDeg = 2.0f, Real a0 = 0.0f, Real a1 = 2.0f * MARKER_PI )
{
	const Real s = BAND_THIN * across;
	radialStrip( bandPiece( finish, 0 ), cx, cy, radiusAt, s, a0, a1, TINT_WHITE, SHELL_FAINT * alpha, FALSE, stepDeg );
	radialStrip( bandPiece( finish, 2 ), cx, cy, radiusAt, s, a0, a1, TINT_WHITE, alpha, FALSE, stepDeg );
	radialStrip( bandPiece( finish, 1 ), cx, cy, radiusAt, s, a0, a1, TINT_WHITE, alpha, TRUE, stepDeg );
	radialStrip( bandPiece( finish, 3 ), cx, cy, radiusAt, INLAY_WIDE * s, a0, a1, scaled( tint, glow ), alpha, FALSE, stepDeg );
	if( finish == FINISH_TUR )
		radialStrip( P_HAIR, cx, cy, ShiftedRadius<RadiusAt>( radiusAt, LED_SHIFT * s ), LED_WIDTH * s * 4.0f, a0, a1, TINT_LED, alpha, FALSE, stepDeg );
}

/// the three tiers along a polygon or a polyline (polyStrip), in a side's finish
static void bandPoly( Int finish, const Real *px, const Real *py, Int n, Bool closed, Real across, const Tint &tint, Real alpha,
											Real glow = 1.0f, Bool inlay = TRUE )
{
	const Real s = BAND_THIN * across;
	polyStrip( bandPiece( finish, 0 ), px, py, n, closed, s, TINT_WHITE, SHELL_FAINT * alpha, FALSE );
	polyStrip( bandPiece( finish, 2 ), px, py, n, closed, s, TINT_WHITE, alpha, FALSE );
	polyStrip( bandPiece( finish, 1 ), px, py, n, closed, s, TINT_WHITE, alpha, TRUE );
	if( inlay )
		polyStrip( bandPiece( finish, 3 ), px, py, n, closed, INLAY_WIDE * s, scaled( tint, glow ), alpha, FALSE );
	if( finish == FINISH_TUR )
		polyStrip( P_HAIR, px, py, n, closed, LED_WIDTH * s * 4.0f, TINT_LED, alpha, FALSE, LED_SHIFT * s );
}

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
	// the refined look (2026-11-24): a thinner band, the shell a faint shadow, the inlay a wider core
	ring( P_BAND_SHELL, x, y, R, 12.0f * k, TINT_WHITE, 0.3f * al, FALSE );
	ring( P_BAND_STEEL_B, x, y, R, 12.0f * k, TINT_WHITE, al, FALSE );
	ring( P_BAND_STEEL_A, x, y, R, 12.0f * k, TINT_WHITE, al, TRUE );
	const Real glow = ( tl >= 0.0f || quiet ) ? 1.0f : 0.78f + 0.22f * (Real)sin( 7.5f * t );
	ring( P_BAND_INLAY, x, y, R, 20.0f * k, scaled( m.tint, glow ), al, FALSE );

	const Int studs = quiet ? 4 : 8;
	const Real flash = tl >= 0.0f ? MAX( 0.0f, 1.0f - tl / 0.5f ) : 0.0f;
	const Tint dot = scaled( m.tint, 1.0f + 0.4f * flash );
	for( Int j = 0; j < studs; ++j )
	{
		const Real a = yaw + 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / (Real)studs;
		const Real sx = x + R * (Real)cos( a ), sy = y + R * (Real)sin( a );
		quadPiece( P_STUD, sx, sy, yaw, 12.0f * k, 12.0f * k, TINT_WHITE, al );
		quadPiece( P_STUD_DOT, sx, sy, yaw, 4.0f * k, 4.0f * k, dot, al );
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
							 4.5f * k, ( major ? 17.0f : 9.0f ) * k, m.tint, al );
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
		arcPiece( P_BRACKET, x, y, rb, yaw + 0.25f * MARKER_PI + swing + (Real)j * 0.5f * MARKER_PI, halfAngle, 9.0f * k, bracket, al, FALSE );
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
		default:						base = P_GLYPH_PARA + ( m.glyph - GLYPH_PARA );	break;
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
	const Real side = 1.8f * m.glyphS;		// the refined look's smaller centre symbol
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

/** The Spectre's orbit, all of it turning the way the gunship flies: counter-clockwise seen from above.
	* SpectreGunshipUpdate steers at m_satellitePosition = target + Ro (n1 p + n2 apogee), p the unit
	* vector from the target to the ship and apogee = (-p.y, p.x), p turned a quarter counter-clockwise,
	* so the point it chases always leads it about 23 degrees counter-clockwise round the target (n1 0.7).
	* World x east, y north, z up and the camera looking down, so that is counter-clockwise on screen
	* too.  The dashes used to run clockwise against the chevrons and the orbit read as turning both
	* ways; they go with the gunship now. */
static void drawOrbit( const Marker &m, Real t, Real alpha )
{
	const Real k = m.k, Ro = m.Ro, yaw = s_ctx.yaw;
	ring( P_BAND_SHELL, m.x, m.y, Ro, 20.0f * k, TINT_WHITE, 0.84f * alpha, FALSE );
	const Real step = 2.0f * MARKER_PI / 18.0f;
	for( Int j = 0; j < 18; ++j )
		arcPiece( P_ORBIT_DASH, m.x, m.y, Ro, yaw + 0.9f * t + (Real)j * step, step * 0.31f * 18.0f / 17.0f, 7.0f * k, TINT_WHITE, alpha, FALSE );
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

static void hairLine( Real ax, Real ay, Real bx, Real by, Real width, const Tint &tint, Real alpha );
static void dashLine( Real ax, Real ay, Real bx, Real by, Real width, Real period, Real duty, Real phase, const Tint &tint, Real alpha );

/** The flight round a turning drop as DeliverPayloadAIUpdate flies it: in along the lane, round the
	* circle it holds over the target (turnPoint at rnom) while the late drops fall, then HeadOffMapState's
	* straight line off the map from wherever the last drop left it.  A dashed exit with a chevron, the
	* drop span's two ends ticked across the track, and, while no real plane is seen, a ghost dart flying
	* it at the plane's own speed (sped up only so one pass takes at most four seconds). */
static void drawManeuver( const Marker &m, Real t, Real est )
{
	if( m.drops.empty() )
		return;
	const Real k = m.k, L = m.L;
	const Real lastA = m.lastPast;
	Real qx, qy, qh;
	turnPoint( m, m.rnom, lastA, qx, qy, qh );
	const Real ox = (Real)cos( qh ), oy = (Real)sin( qh );
	dashLine( qx, qy, qx + ox * MANEUVER_EXIT, qy + oy * MANEUVER_EXIT, 3.0f * k, 22.0f * k, 0.5f, 60.0f * k * t, m.tint, 0.8f * est );
	quadPiece( P_CHEVRON, qx + ox * ( MANEUVER_EXIT + 10.0f * k ), qy + oy * ( MANEUVER_EXIT + 10.0f * k ), qh, 12.0f * k, 18.0f * k,
						 m.tint, est, CHEVRON_ANCHOR_U, 0.5f );

	// the drop span: where the first bomb leaves the plane and where the last does
	const Real ends[ 2 ] = { m.drops.front(), m.drops.back() };
	for( Int e = 0; e < 2; ++e )
	{
		const Real d = ends[ e ];
		Real px, py, h;
		if( d >= 0.0f )
		{
			px = m.x - m.ux * d;
			py = m.y - m.uy * d;
			h = (Real)atan2( m.uy, m.ux );
		}
		else
			turnPoint( m, m.rnom, -d, px, py, h );
		const Real nx = -(Real)sin( h ), ny = (Real)cos( h );
		hairLine( px - nx * 18.0f * k, py - ny * 18.0f * k, px + nx * 18.0f * k, py + ny * 18.0f * k, 3.0f * k, m.tint, est );
	}

	if( !m.darts.empty() )
		return;
	const Real lead = MIN( L, 600.0f );
	const Real total = lead + lastA + MANEUVER_EXIT;
	const Real real = m.planeSpeed > 0.01f ? m.planeSpeed * (Real)LOGICFRAMES_PER_SECOND : 150.0f;
	const Real v = MAX( real, total / 4.0f );
	const Real period = total / v + 0.5f;
	const Real s = (Real)fmod( t, period ) * v;
	if( s > total )
		return;
	Real gx, gy, gh;
	if( s < lead )
	{
		const Real d = lead - s;
		gx = m.x - m.ux * d;
		gy = m.y - m.uy * d;
		gh = (Real)atan2( m.uy, m.ux );
	}
	else if( s < lead + lastA )
		turnPoint( m, m.rnom, s - lead, gx, gy, gh );
	else
	{
		const Real e = s - lead - lastA;
		gx = qx + ox * e;
		gy = qy + oy * e;
		gh = qh;
	}
	const Real edge = MIN( clamp01( s / 80.0f ), clamp01( ( total - s ) / 80.0f ) );
	drawDart( gx, gy, gh, 0.8f, k, m.tint, 0.7f * est * edge );
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
				if( m.dropPiece == P_SHAPE_DISC )		// crates land round: a hollow ring, not the bombers' diamond
					quadPiece( P_SHAPE_RING, px, py, yaw, 18.0f * k, 18.0f * k, m.tint, est );
				else
					quadPiece( P_DROP_ESTIMATE, px, py, yaw, 24.0f * k, 24.0f * k, m.tint, est );
			}
			drawManeuver( m, t, est );
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
	else if( m.dropPiece >= 0 )
	{
		// what stands for a drop is the power's own: a crate tile, a trooper, a stepped plate
		const Real ang = m.dropPiece == P_DROP ? yaw : (Real)atan2( m.uy, m.ux );
		const Real side = ( m.dropPiece == P_DROP ? 24.0f : 15.0f ) * k;		// a power's own piece: a small crisp mark
		for( size_t i = 0; i < m.drops.size(); ++i )
		{
			const Real d = m.drops[ i ];
			if( d < 0.0f || (Int)i < released || L - d > grow * ( L + R ) )
				continue;
			quadPiece( m.dropPiece, m.x - m.ux * d, m.y - m.uy * d, ang, side, side, m.tint, app );
		}
	}

	const Int circlePiece = m.dropPiece >= 0 ? m.dropPiece : P_DROP;
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
			const Real side = ( circlePiece == P_DROP ? 24.0f : 15.0f ) * k;
			quadPiece( circlePiece, m.circleX + m.circleR * (Real)cos( a ), m.circleY + m.circleR * (Real)sin( a ), yaw,
								 side, side, m.tint, app );
		}
	}

	// where each item really fell, as the power's own piece: a canopy, a plus on a crate, a tank
	const Tint fallenTint = scaled( m.tint, 0.6f );
	const Bool ownFallen = m.fallenPiece != P_DROP;
	for( size_t i = 0; i < m.fallen.size(); ++i )
		quadPiece( m.fallenPiece, m.fallen[ i ].x, m.fallen[ i ].y, yaw, ( ownFallen ? 14.0f : 18.0f ) * k, ( ownFallen ? 14.0f : 18.0f ) * k,
							 ownFallen ? m.tint : fallenTint, ( ownFallen ? 0.8f : 0.5f ) * fade );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// EVERY POWER'S OWN MARKER
// One frame per power, built of band strips round circles, radius profiles and polygons in the
// side's finish and of the atlas's parts; t from the press, tl from the click, every motion a
// transform, colour or alpha of a piece.  The designs are the 2026-11-23 decals' unique/DESIGNS.md.
///////////////////////////////////////////////////////////////////////////////////////////////////

static Real hashOf( Int i, Real s = 0.0f )
{
	const Real v = (Real)sin( (Real)i * 127.1f + s * 311.7f + 0.5f ) * 43758.5453f;
	return v - (Real)floor( v );
}

static Real easeBack( Real x, Real o = 1.7f )
{
	const Real u = clamp01( x ) - 1.0f;
	return 1.0f + ( o + 1.0f ) * u * u * u + o * u * u;
}

/// a value in [0, period)
static Real wrap( Real v, Real period )
{
	const Real f = (Real)fmod( v, period );
	return f < 0.0f ? f + period : f;
}

static const Real BLOCK_U = 24.0f / 18.0f;		///< MarkerShapeBlock's box fills 18 by 12 of its 24k canvas
static const Real BLOCK_V = 2.0f;

/// one marker's draw: where, how big, when, in what finish, and which way its frame faces
struct DesignDraw
{
	const Marker *m;
	Real x, y, R, k, t, tl, tc, app, snap, yaw, fade;
	Real hx, hy;					///< the frame's heading: the run's, the shells', the caster's, or the screen's right
	Real frac;						///< a countdown's share still to come
	Real impact;					///< seconds since a superweapon landed, below nought before
	Bool aim;
	Int fin;
	Tint tint;

	/// a point `r` from the middle at `a` counter-clockwise from the screen's right
	void at( Real a, Real r, Real &px, Real &py ) const
	{
		px = x + r * (Real)cos( yaw + a );
		py = y + r * (Real)sin( yaw + a );
	}
	/// a point `u` to the screen's right of the middle and `v` up it
	void screen( Real u, Real v, Real &px, Real &py ) const
	{
		const Real c = (Real)cos( yaw ), s = (Real)sin( yaw );
		px = x + c * u - s * v;
		py = y + s * u + c * v;
	}
	/// a point `s` along the heading from the middle and `q` to its left
	void along( Real s, Real q, Real &px, Real &py ) const
	{
		px = x + hx * s - hy * q;
		py = y + hy * s + hx * q;
	}
	Real heading( void ) const { return (Real)atan2( hy, hx ); }
};

enum { MAX_POLY = 96 };
struct Poly
{
	Real x[ MAX_POLY ], y[ MAX_POLY ];
	Int n;
	Poly() : n( 0 ) { }
	void add( Real px, Real py )
	{
		if( n < MAX_POLY )
		{
			x[ n ] = px;
			y[ n ] = py;
			++n;
		}
	}
	/// (u, v) in a frame turned to `ang` round (cx, cy)
	void addLocal( Real cx, Real cy, Real ang, Real u, Real v )
	{
		const Real c = (Real)cos( ang ), s = (Real)sin( ang );
		add( cx + c * u - s * v, cy + s * u + c * v );
	}
};

/// a box with stepped (notched) corners `st` deep, China's, turned to `ang`, counter-clockwise
static Poly steppedBox( Real cx, Real cy, Real ang, Real hx, Real hy, Real st )
{
	Poly p;
	p.addLocal( cx, cy, ang, hx - st, -hy );				p.addLocal( cx, cy, ang, hx - st, -hy + st );
	p.addLocal( cx, cy, ang, hx, -hy + st );				p.addLocal( cx, cy, ang, hx, hy - st );
	p.addLocal( cx, cy, ang, hx - st, hy - st );		p.addLocal( cx, cy, ang, hx - st, hy );
	p.addLocal( cx, cy, ang, -hx + st, hy );				p.addLocal( cx, cy, ang, -hx + st, hy - st );
	p.addLocal( cx, cy, ang, -hx, hy - st );				p.addLocal( cx, cy, ang, -hx, -hy + st );
	p.addLocal( cx, cy, ang, -hx + st, -hy + st );	p.addLocal( cx, cy, ang, -hx + st, -hy );
	return p;
}

static void bandSegment( const DesignDraw &d, Real ax, Real ay, Real bx, Real by, Real across, const Tint &tint, Real alpha,
												 Real glow = 1.0f, Bool inlay = TRUE )
{
	const Real px[ 2 ] = { ax, bx }, py[ 2 ] = { ay, by };
	bandPoly( d.fin, px, py, 2, FALSE, across, tint, alpha * d.app, glow, inlay );
}

static void hairLine( Real ax, Real ay, Real bx, Real by, Real width, const Tint &tint, Real alpha )
{
	const Real dx = bx - ax, dy = by - ay;
	const Real l = (Real)sqrt( dx * dx + dy * dy );
	if( l < 1.0e-3f || alpha <= 0.002f )
		return;
	lineStrip( P_HAIR, ax, ay, dx / l, dy / l, 0.0f, l, 0.5f * width, width, FALSE, tint, FlatAlpha( alpha ), GROUND_STEP );
}

/// a line in dashes `period` apart, `duty` of each drawn, moved along by `phase`
static void dashLine( Real ax, Real ay, Real bx, Real by, Real width, Real period, Real duty, Real phase, const Tint &tint, Real alpha )
{
	const Real dx = bx - ax, dy = by - ay;
	const Real l = (Real)sqrt( dx * dx + dy * dy );
	if( l < 1.0e-3f || alpha <= 0.002f || period <= 0.0f )
		return;
	const Real ux = dx / l, uy = dy / l;
	for( Real s = wrap( phase, period ) - period; s < l; s += period )
	{
		const Real s0 = MAX( 0.0f, s ), s1 = MIN( l, s + duty * period );
		if( s1 > s0 )
			lineStrip( P_HAIR, ax, ay, ux, uy, s0, s1, 0.5f * width, width, FALSE, tint, FlatAlpha( alpha ), GROUND_STEP );
	}
}

static void softRing( Real cx, Real cy, Real r, Real width, const Tint &tint, Real alpha )
{
	if( alpha <= 0.002f || r <= 0.0f )
		return;
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	ring( P_PING, cx, cy, r, width, tint, alpha, FALSE );
	s_ctx.additive = was;
}

static const Real STUD_SMALL = 0.62f;		///< the refined look's fasteners, against the guard's
static const Real PART_SMALL = 0.68f;		///< and its parts (part()), against the sizes the designs name

static void studAt( const DesignDraw &d, Real x, Real y, Real scale = 1.0f, Real alpha = 1.0f )
{
	scale *= STUD_SMALL;
	const Real s = 18.0f * d.k * scale;
	quadPiece( studPiece( d.fin ), x, y, d.yaw, s, s, TINT_WHITE, alpha * d.app );
	if( d.fin == FINISH_USA )
		quadPiece( P_STUD_DOT, x, y, d.yaw, 6.0f * d.k * scale, 6.0f * d.k * scale, d.tint, alpha * d.app );
}

static void part( Int piece, Real x, Real y, Real ang, Real sx, Real sy, const Tint &tint, Real alpha )
{
	// the refined look: every part a smaller, crisper mark than the designs first drew it
	quadPiece( piece, x, y, ang, PART_SMALL * sx, PART_SMALL * sy, tint, alpha );
}

/// the run's position of a delivery plane (its first dart) along the heading from the target, or a big negative
static Real planeAlong( const DesignDraw &d )
{
	if( d.m->darts.empty() )
		return -1.0e9f;
	const Dart &dart = d.m->darts.front();
	return ( dart.x - d.x ) * d.hx + ( dart.y - d.y ) * d.hy;
}

// ---------------------------------------------------------------- America

/// a canopy from above: scalloped gores breathing while aiming, snapping open on the click
static void drawParadrop( const DesignDraw &d )
{
	const Int n = d.fin == FINISH_CHINA ? 6 : 8;
	const Real k = d.k, R = d.R, step = 2.0f * MARKER_PI / (Real)n;
	const Real rotc = d.yaw + 0.15f * d.tc;
	Real sc, bulge;
	if( d.aim )
	{
		const Real b = (Real)sin( 2.0f * MARKER_PI * 0.55f * d.t );
		sc = 0.93f + 0.02f * b;
		bulge = 0.05f + 0.025f * b;
	}
	else
	{
		sc = 0.93f + 0.07f * easeBack( d.tl / 0.3f, 2.6f );
		bulge = 0.065f;
	}
	const Real Rs = R * sc;
	auto canopy = [=]( Real a ) -> Real
	{
		const Real loc = wrap( a - rotc + 0.5f * step, step ) - 0.5f * step;
		const Real f = loc / ( 0.5f * step );
		return Rs * ( 1.0f + bulge * ( 1.0f - f * f ) );
	};
	for( Int j = 0; j < n; ++j )
	{
		const Real a = rotc + ( (Real)j + 0.5f ) * step;
		hairLine( d.x + 0.17f * Rs * (Real)cos( a ), d.y + 0.17f * Rs * (Real)sin( a ), d.x + Rs * (Real)cos( a ), d.y + Rs * (Real)sin( a ),
							2.4f * k, TINT_STEEL, 0.85f * d.app );
	}
	bandRadial( d.fin, d.x, d.y, canopy, 18.0f * k, d.tint, d.app );
	bandCircle( d.fin, d.x, d.y, 0.15f * Rs, 12.0f * k, d.tint, d.app );
	for( Int j = 0; j < n; ++j )
	{
		const Real a = rotc + ( (Real)j + 0.5f ) * step;
		studAt( d, d.x + Rs * (Real)cos( a ), d.y + Rs * (Real)sin( a ), 0.8f );
	}
}

/// a bay at the target; the crates are the drops (crate tiles), lighting one by one like a manifest
static void drawCrate( const DesignDraw &d )
{
	// the crates scatter round the point: a round bay, round drop marks
	const Real k = d.k, R = d.R;
	bandCircle( d.fin, d.x, d.y, 0.36f * R, 14.0f * k, d.tint, d.app );
	for( Int i = 0; i < 4; ++i )
	{
		Real px, py;
		const Real a = 0.5f * MARKER_PI * (Real)i;
		d.along( 0.36f * R * (Real)cos( a ), 0.36f * R * (Real)sin( a ), px, py );
		studAt( d, px, py, 0.8f );
	}
	const Marker &m = *d.m;
	if( !d.aim || m.drops.empty() )
		return;
	const Int lit = (Int)wrap( d.t * 4.0f, (Real)( m.drops.size() + 3 ) );
	for( Int i = 0; i < (Int)m.drops.size() && i < lit; ++i )
	{
		const Real dd = m.drops[ i ];
		if( dd < 0.0f )
			continue;
		part( P_SHAPE_DISC, d.x - d.hx * dd, d.y - d.hy * dd, d.heading(), 22.0f * k, 22.0f * k, scaled( d.tint, 1.3f ), 0.8f * d.app );
	}
}

/// sheets fluttering round the zone; on the click they burst out and settle inside, then fade
static void drawLeaflet( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Tint paper = { 0.97f, 0.88f, 0.62f };
	const Real step = 2.0f * MARKER_PI / 36.0f;
	for( Int j = 0; j < 36; ++j )
	{
		const Real a = d.yaw - 0.1f * d.t + (Real)j * step;
		arcStrip( P_HAIR, d.x, d.y, 0.86f * R, 3.0f * k, a, a + 0.3f * step, TRUE, d.tint, 0.7f * d.app, FALSE );
	}
	for( Int i = 0; i < 24; ++i )
	{
		const Real a0 = 2.0f * MARKER_PI * (Real)i / 24.0f + 0.22f * d.tc;
		const Real fl = 0.55f * (Real)sin( 3.1f * d.t + 1.7f * (Real)i );
		const Real rad = R * ( 0.97f + 0.03f * (Real)sin( 2.3f * d.t + (Real)i ) );
		Real px, py;
		d.at( a0, rad, px, py );
		Real ang = d.yaw + a0 + 0.5f * MARKER_PI + fl, sc = 1.0f, alpha = 1.0f;
		if( !d.aim )
		{
			const Real out = easeOut( d.tl / 0.3f );
			const Real fall = smoothstep( 0.3f + 0.03f * i, 1.6f + 0.03f * i, d.tl );
			Real bx, by, lx, ly;
			d.at( a0, 1.18f * R, bx, by );
			d.at( 2.0f * MARKER_PI * hashOf( i + 11 ), 0.75f * R * (Real)sqrt( hashOf( i + 3 ) ), lx, ly );
			const Real ox = px + ( bx - px ) * out, oy = py + ( by - py ) * out;
			px = ox + ( lx - ox ) * fall;
			py = oy + ( ly - oy ) * fall;
			ang += 2.5f * fall * ( hashOf( i ) - 0.5f ) - fl * fall;
			sc = 1.0f - 0.25f * fall;
			alpha = 1.0f - smoothstep( 2.0f + 0.02f * i, 2.4f + 0.02f * i, d.tl );
		}
		part( P_SHAPE_TILE, px, py, ang, 16.0f * k * sc, 11.0f * k * sc, paper, alpha * d.app );
	}
}

/// a missile strike on a circle: a round frame, strafe ticks streaming along the run inside it, six missiles converging
static void drawA10( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, h = d.heading();
	const Real rr = R * ( 1.0f + 0.08f * ( 1.0f - d.snap ) );
	const Real ph = d.aim ? 110.0f * d.t : 110.0f * d.tc + 520.0f * MAX( 0.0f, MIN( d.tl, 1.9f ) - 0.5f );
	const Bool burst = !d.aim && d.tl > 0.5f && d.tl < 1.9f;
	const Real pitch = 16.0f * k;
	for( Int row = -1; row <= 1; row += 2 )
	{
		const Real q = (Real)row * 0.19f * R;
		const Real reach = (Real)sqrt( MAX( 0.0f, ( rr - 0.12f * R ) * ( rr - 0.12f * R ) - q * q ) );
		for( Real s = -reach + wrap( ph, pitch ); s < reach - 8.0f * k; s += pitch )
		{
			Real ax, ay, bx, by;
			d.along( s, q, ax, ay );
			d.along( s + 8.0f * k, q, bx, by );
			hairLine( ax, ay, bx, by, 2.4f * k, scaled( d.tint, burst ? 1.3f : 0.8f ), d.app );
		}
	}
	bandCircle( d.fin, d.x, d.y, rr, 16.0f * k, d.tint, d.app );
	for( Int j = 0; j < 4; ++j )
	{
		Real px, py;
		d.along( rr * (Real)cos( 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)j ), rr * (Real)sin( 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)j ), px, py );
		studAt( d, px, py, 0.9f );
	}
	const Real rc = 0.34f * R - 0.2f * R * d.snap;
	for( Int j = 0; j < 6; ++j )
	{
		const Real a = h + MARKER_PI / 6.0f + (Real)j * MARKER_PI / 3.0f;
		quadPiece( P_CHEVRON, d.x + rc * (Real)cos( a ), d.y + rc * (Real)sin( a ), a + MARKER_PI, 10.0f * k, 15.0f * k, d.tint, d.app,
							 CHEVRON_ANCHOR_U, 0.5f );
	}
	if( d.aim )
		return;
	for( Int p = 0; p < 3; ++p )
	{
		const Real t0 = 0.5f + 0.5f * (Real)p;
		const Real fr = ( d.tl - t0 ) / 0.5f;
		if( fr >= 0.0f && fr < 1.0f )
			for( Int side = -1; side <= 1; side += 2 )
			{
				Real px, py;
				d.along( -520.0f * ( 1.0f - fr ), (Real)side * ( 26.0f + 8.0f * p ) * ( 1.0f - fr ), px, py );
				drawDart( px, py, h - (Real)side * 0.05f, 0.55f, k, d.tint, d.app );
			}
		pingRing( d.x, d.y, 0.2f * R, k, d.tl - t0 - 0.5f, d.tint, 0.4f, 1.2f, 4.0f );
	}
}

/// overpressure: rings ripple out of the detonator while aiming, collapse on the click, one shock
static void drawFuelAir( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Bool moab = d.m->variant == VARIANT_MOAB;
	const Int n = moab ? 4 : 3;
	bandCircle( d.fin, d.x, d.y, R, 24.0f * k, d.tint, d.app );
	if( moab )
		bandCircle( d.fin, d.x, d.y, R - 15.0f * k, 12.0f * k, TINT_RED, d.app );
	for( Int j = 0; j < 4; ++j )
	{
		Real px, py;
		d.at( 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI, R, px, py );
		studAt( d, px, py, 1.4f );
	}
	if( d.aim || d.tl < 0.25f )
		for( Int i = 0; i < n; ++i )
		{
			const Real fr = wrap( 0.42f * d.tc + (Real)i / (Real)n, 1.0f );
			Real rad = R * ( 0.12f + 0.78f * fr );
			if( !d.aim )
				rad *= 1.0f - 0.92f * d.snap;
			ring( P_HAIR, d.x, d.y, rad, 3.5f * k, d.tint, (Real)sin( MARKER_PI * fr ) * 0.95f * d.app, FALSE );
		}
	const Real det = d.aim ? 14.0f * k * ( 1.0f + 0.1f * (Real)sin( 6.0f * d.t ) ) : 15.0f * k;
	quadPiece( P_STUD, d.x, d.y, d.yaw, det, det, TINT_WHITE, d.app );
	quadPiece( P_SHAPE_DISC, d.x, d.y, d.yaw, 0.45f * det, 0.45f * det, TINT_RED, d.app );
	if( d.aim )
		return;
	for( Int j = 0; j < ( moab ? 2 : 1 ); ++j )
	{
		const Real e = easeOut( ( d.tl - 0.3f - 0.3f * j ) / 0.75f );
		if( d.tl > 0.3f + 0.3f * j && e < 1.0f )
			softRing( d.x, d.y, 1.15f * R * e, ( 14.0f - 6.0f * e ) * k, pingTintOf( d.tint ), (Real)pow( 1.0f - e, 0.7f ) * d.app );
	}
	const Real hold = smoothstep( 1.0f, 1.4f, d.tl );
	for( Int i = 0; i < n; ++i )
		ring( P_HAIR, d.x, d.y, R * ( 0.3f + 0.54f * (Real)( i + 1 ) / (Real)n ), 3.0f * k, d.tint, 0.6f * hold * d.app, FALSE );
	glowDisc( d.x, d.y, 0.9f * R, d.tint, 0.12f * ( 1.0f - smoothstep( 0.4f, 1.6f, d.tl ) ) * d.app );
}

/// an imaging footprint: a round frame with its panels and a scan bar that resolves the ground
static void drawSpySat( const DesignDraw &d )
{
	// the reveal is a circle: a round frame, the scan grid clipped to it, the panels two arcs outside it
	const Real k = d.k, R = d.R;
	const Real sc = d.aim ? 1.0f : 1.0f + 0.08f * ( 1.0f - d.snap );
	const Real u = d.aim ? 0.45f : 0.45f + 0.55f * easeBack( d.tl / 0.35f );
	const Real hs = 0.92f * R * sc;
	for( Int sx = -1; sx <= 1; sx += 2 )
	{
		const Real mid = sx < 0 ? MARKER_PI : 0.0f, half = 0.16f + 0.2f * u;
		const Real rp = hs + 0.1f * R;
		bandArc( d.fin, d.x, d.y, rp, 10.0f * k, d.yaw + mid - half, d.yaw + mid + half, TINT_STEEL, 0.8f * d.app, 1.0f, FALSE );
		for( Int c = -1; c <= 1; ++c )
		{
			Real p0x, p0y, p1x, p1y;
			const Real a = mid + (Real)c * 0.66f * half;
			d.at( a, rp - 0.05f * R, p0x, p0y );
			d.at( a, rp + 0.05f * R, p1x, p1y );
			hairLine( p0x, p0y, p1x, p1y, 1.6f * k, TINT_DARK, 0.7f * d.app );
		}
	}
	const Real yb = d.aim ? 0.92f * hs * (Real)sin( 2.0f * MARKER_PI * d.t / 1.8f ) : hs - 2.0f * hs * clamp01( d.tl / 0.8f );
	const Real in = hs - 6.0f * k;
	for( Int i = 1; i < 8; ++i )
	{
		const Real v = -hs + 2.0f * hs * (Real)i / 8.0f;
		const Real chord = (Real)sqrt( MAX( 0.0f, in * in - v * v ) );
		Real a = 0.0f;
		if( d.aim )
			a = 0.25f + 0.55f * (Real)exp( -fabs( v - yb ) / ( 0.25f * R ) );		// a grid that never vanishes
		else if( v > yb )
			a = 0.65f * ( 1.0f - 0.6f * smoothstep( 1.0f + 0.15f * i, 1.4f + 0.15f * i, d.tl ) );
		Real p0x, p0y, p1x, p1y;
		d.screen( -chord, v, p0x, p0y );
		d.screen( chord, v, p1x, p1y );
		hairLine( p0x, p0y, p1x, p1y, 2.0f * k, d.tint, a * d.app );
		if( !d.aim && hs > yb )
		{
			const Real lo = MAX( -chord, yb );
			if( chord > lo )
			{
				d.screen( v, lo, p0x, p0y );
				d.screen( v, chord, p1x, p1y );
				hairLine( p0x, p0y, p1x, p1y, 2.0f * k, d.tint, 0.65f * ( 1.0f - 0.6f * smoothstep( 1.2f + 0.15f * i, 1.6f + 0.15f * i, d.tl ) ) * d.app );
			}
		}
	}
	if( ( d.aim || d.tl < 0.9f ) && fabs( yb ) < in )
	{
		const Real chord = (Real)sqrt( in * in - yb * yb );
		Real p0x, p0y, p1x, p1y;
		d.screen( -chord, yb, p0x, p0y );
		d.screen( chord, yb, p1x, p1y );
		hairLine( p0x, p0y, p1x, p1y, 4.0f * k, d.tint, d.app );
		const Bool was = s_ctx.additive;
		s_ctx.additive = TRUE;
		hairLine( p0x, p0y, p1x, p1y, 9.0f * k, d.tint, 0.5f * d.app );
		s_ctx.additive = was;
	}
	bandCircle( d.fin, d.x, d.y, hs, 18.0f * k, d.tint, d.app );
	for( Int i = 0; i < 4; ++i )
	{
		Real px, py;
		d.at( 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)i, hs, px, py );
		studAt( d, px, py, 0.9f );
	}
}

/// a camera iris: six blades round a round opening that breathes, focus arcs hunting
static void drawSpyDrone( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	Real ap;
	if( d.aim )
		ap = ( 0.36f + 0.1f * (Real)sin( 2.1f * d.t ) ) * R;
	else
	{
		const Real a0 = ( 0.36f + 0.1f * (Real)sin( 2.1f * d.tc ) ) * R;
		ap = a0 + ( 0.8f * R - a0 ) * easeOut( d.tl / 0.4f );
	}
	const Real br = 0.7f * ( 1.0f - ap / R );
	const Real vr = ap / (Real)cos( MARKER_PI / 6.0f );
	const Real rim = R - 3.0f * k;
	Poly hex;
	for( Int j = 0; j < 6; ++j )
		hex.add( d.x + vr * (Real)cos( d.yaw + br + MARKER_PI / 6.0f + (Real)j * MARKER_PI / 3.0f ),
						 d.y + vr * (Real)sin( d.yaw + br + MARKER_PI / 6.0f + (Real)j * MARKER_PI / 3.0f ) );
	const Tint blade = { 0.52f, 0.53f, 0.56f };
	for( Int j = 0; j < 6; ++j )
	{
		const Int jn = ( j + 1 ) % 6;
		const Real aj = (Real)atan2( hex.y[ j ] - d.y, hex.x[ j ] - d.x ) + 1.0f;
		const Real an = (Real)atan2( hex.y[ jn ] - d.y, hex.x[ jn ] - d.x ) + 1.0f;
		Poly b;
		b.add( hex.x[ j ], hex.y[ j ] );
		b.add( hex.x[ jn ], hex.y[ jn ] );
		b.add( d.x + rim * (Real)cos( an ), d.y + rim * (Real)sin( an ) );
		b.add( d.x + rim * (Real)cos( 0.5f * ( aj + an ) + ( fabs( an - aj ) > MARKER_PI ? MARKER_PI : 0.0f ) ),
					 d.y + rim * (Real)sin( 0.5f * ( aj + an ) + ( fabs( an - aj ) > MARKER_PI ? MARKER_PI : 0.0f ) ) );
		b.add( d.x + rim * (Real)cos( aj ), d.y + rim * (Real)sin( aj ) );
		Real cx = 0.0f, cy = 0.0f;
		for( Int i = 0; i < b.n; ++i )
		{
			cx += b.x[ i ] / (Real)b.n;
			cy += b.y[ i ] / (Real)b.n;
		}
		fillFan( cx, cy, b.x, b.y, b.n, TRUE, blade, 0.14f * d.app, 0.14f * d.app );
		hairLine( hex.x[ j ], hex.y[ j ], d.x + rim * (Real)cos( aj ), d.y + rim * (Real)sin( aj ), 2.0f * k, TINT_DARK, 0.45f * d.app );
	}
	ring( P_HAIR, d.x, d.y, ap, 3.0f * k, d.tint, 0.9f * d.app, FALSE );		// the reveal is round, so is its opening
	bandCircle( d.fin, d.x, d.y, R, 16.0f * k, d.tint, d.app );
	Real db;
	if( d.aim )
		db = R + 16.0f * k + 9.0f * k * (Real)sin( 6.3f * d.t ) * (Real)sin( 1.7f * d.t );
	else
	{
		const Real d0 = R + 16.0f * k + 9.0f * k * (Real)sin( 6.3f * d.tc ) * (Real)sin( 1.7f * d.tc );
		db = d0 + ( R + 6.0f * k - d0 ) * d.snap;
	}
	for( Int j = 0; j < 4; ++j )
	{
		// focus brackets as short arcs of the circle they hunt round
		const Real a = d.yaw + 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI;
		const Real half = 16.0f * k / MAX( db, 1.0f );
		bandArc( d.fin, d.x, d.y, db, 7.0f * k, a - half, a + half, d.tint, d.app );
	}
	if( !d.aim )
	{
		const Real e = easeOut( d.tl / 0.5f );
		Real bx, by;
		d.screen( 3.0f * k * (Real)sin( 2.0f * d.tl ), 3.0f * k * (Real)cos( 2.6f * d.tl ), bx, by );
		ring( P_HAIR, bx, by, 7.0f * k * e, 3.0f * k, d.tint, d.app, FALSE );
		part( P_SHAPE_DISC, bx, by, d.yaw, 6.0f * k * e, 6.0f * k * e, d.tint, d.app );
	}
}

/// a gear that ratchets while aiming; on the click one tooth round, then the teeth light green
static void drawRepair( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Int n = 16;
	const Real pitch = 2.0f * MARKER_PI / (Real)n, half = 0.5f * pitch;
	struct Ratchet
	{
		static Real at( Real t, Real step )
		{
			const Real f = t / 0.5f;
			return step * ( (Real)floor( f ) + easeOut( MIN( 1.0f, ( f - (Real)floor( f ) ) / 0.25f ) ) );
		}
	};
	const Real ang = d.aim ? Ratchet::at( d.t, half ) : Ratchet::at( d.tc, half ) + pitch * easeOut( d.tl / 0.35f );
	const Real prog = d.aim ? -1.0f : (Real)n * smoothstep( 0.3f, 3.0f, d.tl );
	for( Int j = 0; j < n; ++j )
	{
		const Real a = d.yaw + ang + (Real)j * pitch;
		const Real hv = hashOf( j + 4 );
		const Bool lit = (Real)j < prog;
		const Tint tooth = lit ? TINT_HEAL : TINT_WHITE;
		Real px, py;
		if( d.fin == FINISH_CHINA )
		{
			d.at( ang + (Real)j * pitch, R + 9.0f * k, px, py );
			part( P_SHAPE_BLOCK, px, py, a, 7.0f * k * BLOCK_U, 0.55f * pitch * R * BLOCK_V, tooth, d.app );
			d.at( ang + (Real)j * pitch, R + 14.0f * k, px, py );
			part( P_SHAPE_BLOCK, px, py, a, 6.0f * k * BLOCK_U, 0.3f * pitch * R * BLOCK_V, tooth, d.app );
		}
		else if( d.fin == FINISH_GLA )
		{
			const Real len = ( 6.0f + 7.0f * hv ) * k;
			d.at( ang + (Real)j * pitch + 0.08f * ( hv - 0.5f ), R + 6.0f * k + 0.5f * len, px, py );
			part( P_SHAPE_BLOCK, px, py, a + 0.3f * ( hv - 0.5f ), len * BLOCK_U, ( 0.3f + 0.2f * hv ) * pitch * R * BLOCK_V, tooth, d.app );
		}
		else
		{
			d.at( ang + (Real)j * pitch, R + 10.0f * k, px, py );
			part( P_SHAPE_BLOCK, px, py, a, 11.0f * k * BLOCK_U, 0.42f * pitch * R * BLOCK_V, tooth, d.app );
		}
	}
	bandCircle( d.fin, d.x, d.y, R, 18.0f * k, d.tint, d.app );
	ring( P_HAIR, d.x, d.y, R - 18.0f * k, 2.4f * k, d.tint, 0.6f * d.app, FALSE );
	for( Int j = 0; j < 4; ++j )
	{
		Real px, py;
		d.at( 0.25f * MARKER_PI + ang + (Real)j * 0.5f * MARKER_PI, R - 28.0f * k, px, py );
		studAt( d, px, py );
	}
	if( d.aim )
		return;
	for( Int i = 0; i < 8; ++i )
	{
		const Real age = d.tl - 0.25f - 0.3f * (Real)i;
		if( age < 0.0f || age >= 1.0f )
			continue;
		const Real s = easeBack( age / 0.25f ) * ( 1.0f - smoothstep( 0.6f, 1.0f, age ) );
		Real px, py;
		d.at( 2.0f * MARKER_PI * hashOf( i + 9 ), 0.62f * R * (Real)sqrt( hashOf( i + 5 ) ), px, py );
		px += -s_ctx.ry * 18.0f * age;
		py += s_ctx.rx * 18.0f * age;
		part( P_SHAPE_PLUS, px, py, d.yaw, 20.0f * k * s, 20.0f * k * s, TINT_HEAL, d.app );
	}
}

/// the gunship's circle: the orbit (drawOrbit) it rides, a round reticle and the fire line between
static void drawSpectre( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	const Real Ro = m.orbit ? m.Ro : 250.0f;
	Real gx, gy, gh;
	const Bool real = !m.darts.empty();
	if( real )
	{
		gx = m.darts.front().x;
		gy = m.darts.front().y;
		gh = m.darts.front().heading;
	}
	else
	{
		const Real phi = d.yaw + 0.9f * d.t + 1.2f;
		gx = d.x + Ro * (Real)cos( phi );
		gy = d.y + Ro * (Real)sin( phi );
		gh = phi + 0.5f * MARKER_PI;
		drawDart( gx, gy, gh, 1.0f, k, d.tint, 0.85f * d.app );
	}
	if( d.aim )
		dashLine( gx, gy, d.x, d.y, 3.0f * k, 24.0f * k, 0.35f, 0.0f, d.tint, 0.75f * d.app );
	else
	{
		const Bool was = s_ctx.additive;
		s_ctx.additive = TRUE;
		dashLine( gx, gy, d.x, d.y, 3.0f * k, 34.0f * k, 0.4f, 900.0f * d.tl, pingTintOf( d.tint ), 0.9f * d.app );
		s_ctx.additive = was;
		for( Int i = 0; i < 14; ++i )
		{
			Real px, py;
			d.at( 2.0f * MARKER_PI * hashOf( i + 13 ), 0.8f * R * (Real)sqrt( hashOf( i + 2 ) ), px, py );
			pingRing( px, py, 6.0f * k, k, d.tl - 0.3f - 0.2f * (Real)i, d.tint, 0.35f, 1.5f, 3.0f );
		}
	}
	bandCircle( d.fin, d.x, d.y, R, 14.0f * k, d.tint, d.app );
	const Real spin = 0.5f * d.tc + MARKER_PI / 8.0f * d.snap;
	const Real rt = 0.62f * R - 0.15f * R * d.snap;
	for( Int j = 0; j < 4; ++j )
	{
		const Real a = spin + (Real)j * 0.5f * MARKER_PI;
		Real px, py;
		d.at( a, rt, px, py );
		part( P_TICK_MAJOR, px, py, d.yaw + a - 0.5f * MARKER_PI, 8.0f * k, 22.0f * k, d.tint, d.app );
	}
	part( P_SHAPE_DISC, d.x, d.y, d.yaw, 8.0f * k, 8.0f * k, d.tint, d.app );
}

/// a bomb ladder: rails as wide as the drop variance, a rung for every drop, each burning as the plane passes
static void drawB3( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, h = d.heading(), hw = 52.0f;
	if( m.drops.empty() )
		return;
	const Real first = m.drops.front(), last = m.drops.back();
	const Real grow = smoothstep( 0.05f, 0.7f, d.t );
	for( Int side = -1; side <= 1; side += 2 )
	{
		Real ax, ay, bx, by;
		d.along( -( first + 30.0f ), (Real)side * hw, ax, ay );
		d.along( -( last - 30.0f ), (Real)side * hw, bx, by );
		bandSegment( d, ax, ay, ax + ( bx - ax ) * grow, ay + ( by - ay ) * grow, 9.0f * k, d.tint, 1.0f );
	}
	const Real plane = planeAlong( d );
	const Real wave = wrap( d.t * 7.0f, (Real)m.drops.size() + 4.0f );
	for( Int i = 0; i < (Int)m.drops.size(); ++i )
	{
		const Real dd = m.drops[ i ];
		Real px, py;
		d.along( -dd, 0.0f, px, py );
		const Bool passed = plane > -dd;
		Tint rung = scaled( d.tint, 0.5f );
		if( passed )
			rung = TINT_SCORCH;
		else if( !d.aim || fabs( (Real)i - wave ) < 1.5f )
			rung = scaled( d.tint, 1.2f );
		// a thin see-through rung still spans the corridor
		quadPiece( P_SHAPE_TILE, px, py, h, 4.0f * k, 2.0f * hw - 8.0f * k, rung, 0.7f * d.app );
		if( passed )
			pingRing( px, py, 30.0f, k, ( plane + dd ) / 320.0f, d.tint, 0.5f, 0.8f, 5.0f );
		else
			part( P_SHAPE_DISC, px, py, h, 9.0f * k, 9.0f * k, scaled( d.tint, 0.9f ), d.app );
	}
	quadPiece( P_CHEVRON, d.x, d.y, h, 15.0f * k, 26.0f * k, d.tint, d.app, CHEVRON_ANCHOR_U, 0.5f );
}

/// a gyroscope charging: rings of arcs spinning both ways round a core; the beam's head wanders after
static void drawParticle( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	bandCircle( d.fin, d.x, d.y, R, 10.0f * k, d.tint, d.app );
	ring( P_HAIR, d.x, d.y, R - 9.0f * k, 3.0f * k, TINT_STEEL, d.app, FALSE );
	const Real rads[ 2 ] = { 0.68f * R, 0.44f * R }, spds[ 2 ] = { -0.8f, 1.25f }, wide[ 2 ] = { 0.55f, 0.75f };
	for( Int r = 0; r < 2; ++r )
	{
		const Real rad = d.aim ? rads[ r ] : rads[ r ] * ( 1.0f - d.snap );
		if( rad < 3.0f * k )
			continue;
		const Real phase = d.yaw + spds[ r ] * d.tc + ( d.aim ? 0.0f : spds[ r ] * 4.0f * d.tl );
		for( Int j = 0; j < 3; ++j )
		{
			const Real c = phase + (Real)j * 2.0f * MARKER_PI / 3.0f;
			bandArc( d.fin, d.x, d.y, rad, 9.0f * k, c - wide[ r ], c + wide[ r ], d.tint, d.app );
		}
	}
	const Real inner = d.aim ? 0.76f * R : 0.76f * R - 0.5f * R * d.snap;
	for( Int j = 0; j < 3; ++j )
	{
		Real ax, ay, bx, by;
		d.at( 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / 3.0f, inner, ax, ay );
		d.at( 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / 3.0f, R - 9.0f * k, bx, by );
		bandSegment( d, ax, ay, bx, by, 7.0f * k, d.tint, 1.0f );
	}
	quadPiece( P_STUD, d.x, d.y, d.yaw, 13.0f * k, 13.0f * k, TINT_WHITE, d.app );
	part( P_SHAPE_DISC, d.x, d.y, d.yaw, 9.0f * k, 9.0f * k, scaled( d.tint, 0.85f + 0.3f * (Real)sin( 9.0f * d.t ) ), d.app );
	if( d.aim || d.tl < 0.25f )
		return;
	const Real tb = d.tl - 0.25f;
	// the head: the real beam where the click's cannon is seen firing (followSuperweapon), else a
	// wander that never leaves the zone
	Real bx = 0.0f, by = 0.0f;
	if( m.beamSeen )
	{
		for( size_t i = 0; i < m.beamTrail.size(); ++i )
			part( P_SHAPE_DISC, m.beamTrail[ i ].x, m.beamTrail[ i ].y, d.yaw, 12.0f * k, 12.0f * k, TINT_SCORCH, 0.6f * d.app );
		bx = m.beamX;
		by = m.beamY;
	}
	else
		for( Int i = 0; i <= 30; ++i )
		{
			const Real s = tb * (Real)i / 30.0f;
			const Real u = 0.28f * R * (Real)sin( 1.1f * s ) + 0.08f * R * (Real)sin( 5.0f * s );
			const Real v = 0.34f * R * (Real)sin( 0.8f * s + 0.3f ) - 0.34f * R * (Real)sin( 0.3f );
			d.screen( u, v, bx, by );
			if( i < 30 )
				part( P_SHAPE_DISC, bx, by, d.yaw, 12.0f * k, 12.0f * k, TINT_SCORCH, 0.6f * d.app );
		}
	const Real flick = 0.8f + 0.2f * (Real)sin( 40.0f * d.t );
	const Tint hot = { 1.0f, 0.95f, 0.9f };
	glowDisc( bx, by, 16.0f * k, pingTintOf( d.tint ), 0.9f * flick * d.app );
	glowDisc( bx, by, 7.0f * k, hot, d.app );
	for( Int j = 0; j < 4; ++j )
		pingRing( bx, by, 8.0f * k, k, wrap( tb - 0.35f * (Real)j, 1.4f ), d.tint, 0.7f, 3.0f, 3.0f );
}

/// a crosshair with no ring: long hairlines with calibration ticks, a small ring collapsing to a point
static void drawLaser( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	const Real g = d.aim ? ( 0.3f + 0.1f * (Real)sin( 3.0f * d.t ) ) * R : ( 0.3f + 0.1f * (Real)sin( 3.0f * d.tc ) ) * R * ( 1.0f - 0.8f * d.snap );
	for( Int j = 0; j < 4; ++j )
	{
		const Real a = 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI;
		Real ax, ay, bx, by;
		d.at( a, g, ax, ay );
		d.at( a, 1.7f * R, bx, by );
		bandSegment( d, ax, ay, bx, by, 5.0f * k, d.tint, 1.0f );
		const Real span = 1.6f * R - g - 6.0f * k;
		for( Int i = 0; i < 9 && span > 0.0f; ++i )
		{
			const Real rr = g + 6.0f * k + wrap( (Real)i * 0.17f * R - 0.25f * R * d.t, MAX( 1.0f, span ) );
			Real px, py;
			d.at( a, rr, px, py );
			part( P_TICK, px, py, d.yaw + a, 6.0f * k, 12.0f * k, d.tint, 0.9f * d.app );
		}
		d.at( a, 1.75f * R, bx, by );
		quadPiece( P_CHEVRON, bx, by, d.yaw + a, 10.0f * k, 15.0f * k, d.tint, d.app, CHEVRON_ANCHOR_U, 0.5f );
		Real sx, sy;
		d.at( (Real)j * 0.5f * MARKER_PI, 1.2f * R, sx, sy );
		studAt( d, sx, sy, 0.8f );
	}
	const Real hd = d.aim ? ( 0.2f + 0.02f * (Real)sin( 6.0f * d.t ) ) * R : 0.2f * R * ( 1.0f - 0.8f * d.snap );
	bandCircle( d.fin, d.x, d.y, hd, 6.0f * k, d.tint, d.app );
	if( d.aim || d.tl < 0.25f )
		return;
	const Real tb = clamp01( ( d.tl - 0.25f ) / 2.2f );
	Real ax, ay, ex, ey;
	d.at( 0.35f + MARKER_PI, 0.7f * R, ax, ay );
	d.at( 0.35f, 0.7f * R, ex, ey );
	Real bx = ax + ( ex - ax ) * tb, by = ay + ( ey - ay ) * tb;
	if( m.beamSeen && !m.beamTrail.empty() )
	{
		// the trench the real beam cut, segment by segment
		bx = m.beamX;
		by = m.beamY;
		for( size_t i = 1; i < m.beamTrail.size(); ++i )
			hairLine( m.beamTrail[ i - 1 ].x, m.beamTrail[ i - 1 ].y, m.beamTrail[ i ].x, m.beamTrail[ i ].y, 9.0f * k, TINT_SCORCH, 0.75f * d.app );
	}
	else
	{
		hairLine( ax, ay, bx, by, 9.0f * k, TINT_SCORCH, 0.75f * d.app );
		const Bool was = s_ctx.additive;
		s_ctx.additive = TRUE;
		hairLine( ax, ay, bx, by, 3.0f * k, d.tint, 0.5f * d.app );
		s_ctx.additive = was;
	}
	const Tint hot = { 1.0f, 0.96f, 0.92f };
	glowDisc( bx, by, 12.0f * k, pingTintOf( d.tint ), 0.9f * d.app );
	glowDisc( bx, by, 5.0f * k, hot, d.app );
}

/// a routed missile: dashes from the launcher through two waypoints to a ring whose quarters count down
static void drawCruise( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Marker &m = *d.m;
	Real vx = m.cx0 - d.x, vy = m.cy0 - d.y;
	const Real vl = MAX( 1.0f, (Real)sqrt( vx * vx + vy * vy ) );
	const Real lx = -vy / vl, ly = vx / vl;
	Poly route;
	route.add( m.cx0, m.cy0 );
	route.add( d.x + vx * 0.62f + lx * 110.0f, d.y + vy * 0.62f + ly * 110.0f );
	route.add( d.x + vx * 0.28f - lx * 70.0f, d.y + vy * 0.28f - ly * 70.0f );
	route.add( d.x, d.y );
	Real lens[ 3 ], tot = 0.0f;
	for( Int i = 0; i < 3; ++i )
	{
		lens[ i ] = (Real)sqrt( ( route.x[ i + 1 ] - route.x[ i ] ) * ( route.x[ i + 1 ] - route.x[ i ] ) + ( route.y[ i + 1 ] - route.y[ i ] ) * ( route.y[ i + 1 ] - route.y[ i ] ) );
		tot += lens[ i ];
	}
	const Real progress = d.aim ? 0.0f : clamp01( 1.0f - d.frac ) * tot;
	const Real stop = MIN( tot - 0.85f * R, smoothstep( 0.05f, 0.7f, d.t ) * tot );
	const Real phase = 60.0f * k * ( d.aim ? d.t : d.tc );
	Real acc = 0.0f;
	for( Int i = 0; i < 3; ++i )
	{
		const Real s0 = MAX( acc, progress ), s1 = MIN( acc + lens[ i ], stop );
		if( s1 > s0 && lens[ i ] > 1.0f )
		{
			const Real ux = ( route.x[ i + 1 ] - route.x[ i ] ) / lens[ i ], uy = ( route.y[ i + 1 ] - route.y[ i ] ) / lens[ i ];
			dashLine( route.x[ i ] + ux * ( s0 - acc ), route.y[ i ] + uy * ( s0 - acc ), route.x[ i ] + ux * ( s1 - acc ), route.y[ i ] + uy * ( s1 - acc ),
								3.0f * k, 22.0f * k, 0.5f, phase - ( s0 - acc ) - acc, d.tint, 0.85f * d.app );
		}
		if( i > 0 )
			part( P_SHAPE_DISC, route.x[ i ], route.y[ i ], d.yaw, 11.0f * k, 11.0f * k, d.tint, d.app );
		acc += lens[ i ];
	}
	// the blast is round: four quarter arcs, clockwise from the top, the lit ones the flight still to come
	const Real rc = 0.62f * R * 1.2f;
	bandArc( d.fin, d.x, d.y, rc, 12.0f * k, 0.0f, 2.0f * MARKER_PI, d.tint, d.app, 1.0f, FALSE );
	const Int lit = d.aim ? 4 : (Int)ceilf( 4.0f * d.frac );
	const Bool blinkOff = !d.aim && sin( 18.0f * d.t ) < 0.0f;
	const Real gap = 0.06f;
	for( Int e = 0; e < 4; ++e )
	{
		const Bool on = e < lit && !( e == lit - 1 && blinkOff );
		const Real a1 = d.yaw + 0.5f * MARKER_PI - 0.5f * MARKER_PI * (Real)e;		// top, then clockwise
		arcStrip( bandPiece( d.fin, 3 ), d.x, d.y, rc, INLAY_WIDE * BAND_THIN * 12.0f * k, a1 - 0.5f * MARKER_PI + gap, a1 - gap, FALSE,
							on ? d.tint : scaled( d.tint, 0.25f ), d.app, FALSE );
	}
	Real ca;
	if( d.aim )
	{
		const Real f = d.t / 0.5f;
		ca = 0.25f * MARKER_PI * ( (Real)floor( f ) + easeOut( MIN( 1.0f, ( f - (Real)floor( f ) ) / 0.3f ) ) );
	}
	else
		ca = 0.25f * MARKER_PI * (Real)floor( d.tc / 0.5f + 1.0f );
	for( Int j = 0; j < 4; ++j )
	{
		Real px, py;
		d.at( ca + (Real)j * 0.5f * MARKER_PI, 0.26f * R, px, py );
		part( P_TICK_MAJOR, px, py, d.yaw + ca + (Real)j * 0.5f * MARKER_PI - 0.5f * MARKER_PI, 8.0f * k, 0.28f * R, d.tint, d.app );
	}
	if( d.aim )
		return;
	bandCircle( d.fin, d.x, d.y, 0.22f * R * d.snap + 0.1f, 5.0f * k, d.tint, d.app );
	if( progress > 0.0f && progress < tot && m.darts.empty() )
	{
		acc = 0.0f;
		for( Int i = 0; i < 3; ++i )
		{
			if( progress <= acc + lens[ i ] )
			{
				const Real f = ( progress - acc ) / MAX( 1.0f, lens[ i ] );
				drawDart( route.x[ i ] + ( route.x[ i + 1 ] - route.x[ i ] ) * f, route.y[ i ] + ( route.y[ i + 1 ] - route.y[ i ] ) * f,
									(Real)atan2( route.y[ i + 1 ] - route.y[ i ], route.x[ i + 1 ] - route.x[ i ] ), 1.0f, k, d.tint, d.app );
				break;
			}
			acc += lens[ i ];
		}
	}
}

/// a decontamination spray: nozzles round the rim, a spiral drawn in, wash rings going out after
static void drawAmbulance( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Real sc = d.aim ? 1.0f : 1.0f - 0.05f * d.snap;
	bandCircle( d.fin, d.x, d.y, R * sc, 10.0f * k, d.tint, d.app );
	const Int pulse = (Int)( d.t * 10.0f ) % 12;
	for( Int j = 0; j < 12; ++j )
	{
		const Real a = (Real)j * MARKER_PI / 6.0f;
		Real px, py;
		d.at( a, R * sc - 11.0f * k, px, py );
		part( P_SHAPE_TONGUE, px, py, d.yaw + a + MARKER_PI, 17.0f * k, 17.0f * k, scaled( d.tint, j == pulse ? 1.35f : 0.8f ), d.app );
	}
	const Real spd = d.aim ? 1.0f : 1.8f;
	for( Int a = 0; a < 3; ++a )
		for( Int j = 0; j < 8; ++j )
		{
			const Real fr = wrap( (Real)j / 8.0f + 0.35f * d.t * spd, 1.0f );
			const Real rad = 0.8f * R * ( 1.0f - fr );
			const Real ang = 2.0f * MARKER_PI * (Real)a / 3.0f + 2.6f * rad / R + spd * d.t * 0.4f;
			Real px, py;
			d.at( ang, rad, px, py );
			const Real s = ( 2.4f + 6.4f * ( 1.0f - fr ) ) * k;
			part( P_SHAPE_DISC, px, py, d.yaw, s, s, d.tint, ( d.aim ? 0.85f : 0.5f ) * d.app );
		}
	if( d.aim )
		return;
	const Tint water = { 0.7f, 0.92f, 1.0f };
	for( Int j = 0; j < 6; ++j )
	{
		const Real age = d.tl - 0.2f - 0.45f * (Real)j;
		if( age >= 0.0f && age < 1.1f )
			softRing( d.x, d.y, 0.95f * R * age / 1.1f, 5.0f * k, water, 0.7f * ( 1.0f - age / 1.1f ) * d.app );
	}
	const Real s = easeBack( d.tl / 0.3f );
	const Tint medic = { 0.95f, 0.3f, 0.25f };
	part( P_SHAPE_PLUS, d.x, d.y, d.yaw, 30.0f * k * s, 30.0f * k * s, medic, d.app );
}

/// a rocket salvo's spread: the scatter circle, the line of fire from the unit, pips landing near to far
static void drawComanche( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, h = d.heading();
	const Real sc = d.aim ? 1.0f : 1.0f + 0.1f * ( 1.0f - d.snap );
	const Real a = R * sc, b = R * sc;		// the rockets scatter over a circle
	const Marker &m = *d.m;
	Real gx, gy;
	d.along( -a - 8.0f * k, 0.0f, gx, gy );
	dashLine( m.cx0, m.cy0, gx, gy, 3.0f * k, 18.0f * k, 0.5f, 60.0f * k * d.t, d.tint, 0.8f * d.app );
	bandCircle( d.fin, d.x, d.y, a, 10.0f * k, d.tint, d.app );
	Real px, py;
	d.along( a, 0.0f, px, py );
	studAt( d, px, py, 0.9f );
	d.along( -a, 0.0f, px, py );
	studAt( d, px, py, 0.9f );
	Real ps[ 24 ], qs[ 24 ];
	for( Int i = 0; i < 24; ++i )
	{
		const Real rr = (Real)sqrt( hashOf( i + 1 ) ), aa = 2.0f * MARKER_PI * hashOf( i + 31 );
		ps[ i ] = rr * (Real)cos( aa ) * a * 0.92f;
		qs[ i ] = rr * (Real)sin( aa ) * b * 0.92f;
	}
	for( Int i = 0; i < 24; ++i )
	{
		Int rank = 0;
		for( Int j = 0; j < 24; ++j )
			if( ps[ j ] < ps[ i ] || ( ps[ j ] == ps[ i ] && j < i ) )
				++rank;
		d.along( ps[ i ], qs[ i ], px, py );
		if( d.aim )
		{
			const Bool lit = sin( 7.0f * d.t + 5.1f * (Real)i ) > 0.4f;
			part( P_SHAPE_DISC, px, py, h, 7.0f * k, 7.0f * k, scaled( d.tint, lit ? 1.2f : 0.45f ), d.app );
			continue;
		}
		const Real t0 = 0.35f + 0.05f * (Real)rank;
		if( d.tl > t0 )
		{
			pingRing( px, py, 5.0f * k, k, d.tl - t0, d.tint, 0.35f, 2.2f, 2.6f );
			part( P_SHAPE_DISC, px, py, h, 8.0f * k, 8.0f * k, TINT_SCORCH, 0.8f * d.app );
		}
		else
			part( P_SHAPE_DISC, px, py, h, 7.0f * k, 7.0f * k, scaled( d.tint, 1.2f ), d.app );
	}
}

/// minesweeping: a hazard-tape ring, a detector arm swinging like a pendulum, the finds struck out
static void drawDemine( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Int n = 24;
	const Real step = 2.0f * MARKER_PI / (Real)n;
	bandArc( d.fin, d.x, d.y, R, 18.0f * k, 0.0f, 2.0f * MARKER_PI, d.tint, d.app, 1.0f, FALSE );
	for( Int j = 0; j < n; ++j )
		arcStrip( bandPiece( d.fin, 3 ), d.x, d.y, R, 18.0f * k, d.yaw + (Real)j * step, d.yaw + (Real)( j + 1 ) * step, FALSE,
							( j & 1 ) ? TINT_AMBER : TINT_DARK, d.app, FALSE );
	Real phi, dphi;
	if( d.aim )
	{
		phi = 0.5f * MARKER_PI + 1.2f * (Real)sin( 2.2f * d.t );
		dphi = (Real)cos( 2.2f * d.t );
	}
	else
	{
		phi = 0.5f * MARKER_PI + 1.2f * (Real)sin( 2.2f * d.tc ) + 3.2f * d.tl;
		dphi = 1.0f;
	}
	const Real trail = dphi > 0.0f ? -1.0f : 1.0f;
	for( Int i = 0; i < 10; ++i )
	{
		const Real a0 = phi + trail * 0.09f * (Real)i, a1 = phi + trail * 0.09f * (Real)( i + 1 );
		const Real alpha = 0.22f * ( 1.0f - ( (Real)i + 0.5f ) / 10.0f ) * ( 1.0f - ( (Real)i + 0.5f ) / 10.0f );
		Poly w;
		for( Int s = 0; s <= 2; ++s )
		{
			Real px, py;
			d.at( a0 + ( a1 - a0 ) * (Real)s / 2.0f, R - 8.0f * k, px, py );
			w.add( px, py );
		}
		fillFan( d.x, d.y, w.x, w.y, w.n, FALSE, d.tint, alpha * d.app, alpha * d.app );
	}
	Real hx, hy;
	d.at( phi, 0.68f * R, hx, hy );
	bandSegment( d, d.x, d.y, hx, hy, 6.0f * k, d.tint, 1.0f, 1.0f, FALSE );
	d.at( phi, 0.78f * R, hx, hy );
	bandCircle( d.fin, hx, hy, 0.13f * R, 8.0f * k, d.tint, d.app );
	studAt( d, d.x, d.y, 1.1f );
	if( d.aim )
		return;
	for( Int i = 0; i < 5; ++i )
	{
		const Real age = d.tl - 0.3f - 0.32f * (Real)i;
		if( age < 0.0f )
			continue;
		Real px, py;
		d.at( 2.0f * MARKER_PI * hashOf( i + 23 ), 0.65f * R * (Real)sqrt( hashOf( i + 17 ) ), px, py );
		if( age < 0.7f )
			part( P_SHAPE_DISC, px, py, d.yaw, 10.0f * k, 10.0f * k, scaled( TINT_AMBER, sin( 30.0f * age ) > 0.0f ? 1.3f : 0.6f ), d.app );
		else
			part( P_SHAPE_CROSS, px, py, d.yaw, 15.0f * k, 15.0f * k, d.tint, ( 1.0f - smoothstep( 1.4f, 1.9f, age ) ) * d.app );
	}
}

// ---------------------------------------------------------------- China

/// bracketing fire on the barrage's circle: a round frame, over and short arcs closing in along the shells' flight
static void drawArtillery( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, h = d.heading();
	const Real rr = 0.9f * R;
	bandCircle( d.fin, d.x, d.y, rr, 18.0f * k, d.tint, d.app );
	ring( P_HAIR, d.x, d.y, rr - 10.0f * k, 3.0f * k, TINT_LACQ, d.app, FALSE );
	for( Int j = 0; j < 6; ++j )
	{
		Real px, py;
		const Real a = (Real)j * MARKER_PI / 3.0f;
		d.along( rr * (Real)cos( a ), rr * (Real)sin( a ), px, py );
		studAt( d, px, py, 0.9f );
	}
	const Real b = d.aim ? 0.68f * R * ( 1.0f - (Real)pow( wrap( 0.8f * d.t, 1.0f ), 0.7f ) )
											 : 0.68f * R * ( 1.0f - (Real)pow( wrap( 0.8f * d.tc, 1.0f ), 0.7f ) ) * ( 1.0f - d.snap );
	if( b > 6.0f * k )
		for( Int side = -1; side <= 1; side += 2 )
		{
			// the over and the short: arcs of the barrage's own circle, ahead and behind along the flight
			const Real mid = side > 0 ? h : h + MARKER_PI;
			const Real half = MIN( 0.6f, 0.42f * R / b );
			bandArc( d.fin, d.x, d.y, b, 10.0f * k, mid - half, mid + half, d.tint, d.app );
		}
	for( Int j = 0; j < 3; ++j )
	{
		// the shells' arrival marches in while aiming and while they are on their way
		const Bool incoming = d.aim || d.impact < 0.0f;
		const Real s = -rr - 22.0f * k - wrap( (Real)j * 22.0f * k + ( incoming ? 50.0f * k * d.t : 0.0f ), 66.0f * k );
		Real px, py;
		d.along( s, 0.0f, px, py );
		quadPiece( P_CHEVRON, px, py, h, 13.0f * k, 20.0f * k, d.tint, d.app, CHEVRON_ANCHOR_U, 0.5f );
	}
	// the impacts from when the last carrier dropped (followCarriers), or from the click when none was seen
	const Real te = d.impact >= 0.0f ? d.impact : ( d.m->planesSeen ? -1.0f : d.tl );
	if( d.aim || te < 0.0f )
		return;
	for( Int i = 0; i < 15; ++i )
	{
		const Real t0 = 0.75f * (Real)( i / 5 ) + 0.09f * (Real)( i % 5 );
		if( te <= t0 )
			continue;
		Real px, py;
		d.at( 2.0f * MARKER_PI * hashOf( i + 43 ), 0.7f * R * (Real)sqrt( hashOf( i + 41 ) ), px, py );
		part( P_SHAPE_DISC, px, py, d.yaw, 13.0f * k, 13.0f * k, TINT_SCORCH, 0.6f * d.app );
		pingRing( px, py, 8.0f * k, k, te - t0, d.tint, 0.4f, 1.6f, 3.6f );
	}
}

/// a minefield: the round field it seeds with a ring per mine; the mines blink armed after the click
static void drawMines( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	bandCircle( d.fin, d.x, d.y, R, 18.0f * k, d.tint, d.app );
	for( Int i = 0; i < 6; ++i )
	{
		Real px, py;
		d.at( (Real)i * MARKER_PI / 3.0f, R, px, py );
		studAt( d, px, py, 1.1f );
	}
	const Real sp = 0.33f * R;
	Int index = 0;
	for( Int a = -2; a <= 2; ++a )
		for( Int b = -2; b <= 2; ++b )
		{
			if( a + b > 2 || a + b < -2 )
				continue;
			const Real u = sp * ( (Real)a + 0.5f * (Real)b ), v = sp * 0.866f * (Real)b;
			const Real dist = (Real)sqrt( u * u + v * v ) / sp;
			const Int i = index++;
			Real px, py;
			d.screen( u, v, px, py );
			Bool lit;
			if( d.aim )
				lit = hashOf( i, (Real)floor( d.t * 5.0f ) ) > 0.62f;
			else
				lit = d.tl > 0.08f * dist + 0.01f * (Real)i;
			part( P_SHAPE_RING, px, py, d.yaw, 0.3f * R, 0.3f * R, scaled( d.tint, lit ? 1.15f : 0.4f ), ( lit ? 1.0f : 0.8f ) * d.app );
			if( !d.aim && d.tl > 0.08f * dist + 0.2f )
			{
				const Bool blink = sin( 4.0f * d.t + 1.3f * (Real)i ) > 0.55f;
				part( P_SHAPE_DISC, px, py, d.yaw, 10.0f * k, 10.0f * k, scaled( TINT_LACQ, blink ? 1.25f : 0.7f ), d.app );
			}
		}
}

/// a lightning ring: a zigzag strobing between four capacitors; on the click a knot, then one pulse
static void drawEmp( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, yaw = d.yaw;
	const Real n = 24.0f;
	const Real ph = d.aim ? 0.5f * (Real)( (Int)floor( d.t * 8.0f ) % 2 ) : 0.0f;
	Real sc = 1.0f;
	if( !d.aim )
		sc = d.tl < 0.8f ? 1.0f - 0.85f * d.snap : smoothstep( 0.8f, 1.2f, d.tl );
	auto zig = [=]( Real a ) -> Real
	{
		const Real g = wrap( ( a - yaw ) * n / ( 2.0f * MARKER_PI ) + ph, 1.0f );
		const Real tri = (Real)fabs( 2.0f * g - 1.0f );
		return R * sc + 6.0f * k * ( 2.0f * tri - 1.0f ) * MAX( sc, 0.2f );
	};
	const Real za = ( d.aim || d.tl < 0.25f || d.tl > 0.8f ) ? 1.0f : 0.4f;
	if( R * sc > 4.0f * k )
		bandRadial( d.fin, d.x, d.y, zig, 8.0f * k, d.tint, za * d.app, 1.0f, 1.5f );
	for( Int j = 0; j < 4; ++j )
	{
		const Real a = 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI;
		Real px, py;
		d.at( a, R + 18.0f * k, px, py );
		part( P_SHAPE_RING, px, py, yaw, 20.0f * k, 20.0f * k, TINT_STEEL, d.app );		// round capacitors on a round pulse
		part( P_SHAPE_DISC, px, py, yaw, 8.0f * k, 8.0f * k, d.tint, d.app );
	}
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	if( d.aim )
	{
		const Real t = d.t;
		for( Int j = 0; j < 4; ++j )
		{
			if( hashOf( j, (Real)floor( d.t * 7.0f ) ) <= 0.45f )
				continue;
			const Real a0 = yaw + 0.25f * MARKER_PI + (Real)j * 0.5f * MARKER_PI + 0.15f;
			auto jag = [=]( Real a ) -> Real
			{
				return R + 18.0f * k + 5.0f * k * (Real)sin( a * 41.0f + t * 50.0f ) * (Real)sin( a * 17.0f - t * 31.0f );
			};
			radialStrip( P_HAIR, d.x, d.y, jag, 3.0f * k, a0, a0 + 0.5f * MARKER_PI - 0.3f, pingTintOf( d.tint ), 0.9f * d.app, FALSE, 1.5f );
		}
	}
	else
	{
		const Real e = easeOut( ( d.tl - 0.25f ) / 0.5f );
		if( d.tl > 0.25f && e < 1.0f )
		{
			auto shock = [=]( Real a ) -> Real
			{
				const Real g = wrap( ( a - yaw ) * n / ( 2.0f * MARKER_PI ), 1.0f );
				return 1.15f * R * e + 7.0f * k * ( 2.0f * (Real)fabs( 2.0f * g - 1.0f ) - 1.0f );
			};
			radialStrip( P_PING, d.x, d.y, shock, 8.0f * k, 0.0f, 2.0f * MARKER_PI, pingTintOf( d.tint ), (Real)pow( 1.0f - e, 0.6f ) * d.app, FALSE, 1.5f );
		}
		if( d.tl > 0.6f )
		{
			const Real tick = (Real)floor( d.t * 20.0f );
			for( Int i = 0; i < 20; ++i )
			{
				Real px, py;
				d.at( 2.0f * MARKER_PI * hashOf( i, tick ), 0.92f * R * (Real)sqrt( hashOf( i + 40, tick ) ), px, py );
				part( P_SHAPE_TILE, px, py, yaw, ( 4.0f + 6.0f * hashOf( i + 7, tick ) ) * k, 2.5f * k, d.tint, 0.35f * d.app );
			}
		}
	}
	s_ctx.additive = was;
}

/// a chain of terraced plates (the drops) between red lacquer bands; the Nuke General's carry a trefoil
static void drawChinaCarpet( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, h = d.heading();
	const Bool nuke = m.variant == VARIANT_NUKE;
	if( !m.drops.empty() )
	{
		const Real grow = smoothstep( 0.05f, 0.7f, d.t );
		for( Int side = -1; side <= 1; side += 2 )
		{
			Real ax, ay, bx, by;
			d.along( -( m.drops.front() + 40.0f ), (Real)side * 46.0f, ax, ay );
			d.along( 10.0f, (Real)side * 46.0f, bx, by );
			bandSegment( d, ax, ay, ax + ( bx - ax ) * grow, ay + ( by - ay ) * grow, 8.0f * k, TINT_LACQ, 1.0f );
		}
	}
	const Real plane = planeAlong( d );
	const Real wave = wrap( 6.0f * d.t, (Real)m.drops.size() + 3.0f );
	for( Int i = 0; i < (Int)m.drops.size(); ++i )
	{
		const Real dd = m.drops[ i ];
		if( dd < 0.0f )
			continue;
		Real px, py;
		d.along( -dd, 0.0f, px, py );
		if( plane > -dd )
		{
			pingRing( px, py, nuke ? 34.0f : 22.0f, k, ( plane + dd ) / 300.0f, d.tint, 0.5f, 0.9f, 5.0f );
			if( nuke )
				pingRing( px, py, 12.0f, k, ( plane + dd ) / 300.0f - 0.15f, d.tint, 0.5f, 2.5f, 4.0f );
			part( P_SHAPE_DIAMOND, px, py, h, 24.0f * k, 24.0f * k, TINT_SCORCH, d.app );
			continue;
		}
		if( d.aim && fabs( (Real)i - wave ) < 1.2f )
			part( P_SHAPE_DIAMOND, px, py, h, 28.0f * k, 28.0f * k, scaled( d.tint, 1.3f ), d.app );
		if( nuke )
			part( P_GLYPH_NUKE00, px, py, d.yaw, 16.0f * k, 16.0f * k, TINT_DARK, 0.85f * d.app );
	}
	part( P_SHAPE_PLUS, d.x, d.y, h + 0.25f * MARKER_PI, 30.0f * k, 30.0f * k, d.tint, d.app );
}

/// a fire carpet: a wide terraced box across the run, a wavy flame front that sweeps through it
static void drawNapalm( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, h = d.heading();
	const Real hd = 0.55f * R, hw = 1.15f * R;
	Real pos = -0.45f * R;
	if( !d.aim )
	{
		pos = -0.45f * R + 0.9f * R * smoothstep( 0.2f, 1.3f, d.tl );
		const Real cell = 0.17f * R;
		for( Int gi = -4; gi <= 4; ++gi )
			for( Int gj = -7; gj <= 7; ++gj )
			{
				const Real cs = ( (Real)gi + 0.5f ) * cell, cq = ( (Real)gj + 0.5f ) * cell;
				if( cs > pos || fabs( cs ) > hd - 8.0f * k || fabs( cq ) > hw - 8.0f * k )
					continue;
				const Real hv = hashOf( gi * 31 + gj );
				const Real fl = 0.7f + 0.3f * (Real)sin( 13.0f * d.t + 6.28f * hv );
				Real px, py;
				d.along( cs, cq, px, py );
				part( P_SHAPE_TONGUE, px, py, h, 15.0f * k * fl, 12.0f * k * fl, scaled( d.tint, 1.1f ), d.app );
			}
	}
	Poly front;
	for( Int i = 0; i <= 40; ++i )
	{
		const Real q = -hw + 6.0f * k + ( 2.0f * hw - 12.0f * k ) * (Real)i / 40.0f;
		Real s;
		if( d.aim )
			s = pos + 6.0f * k * (Real)sin( q / ( 0.11f * R ) - 6.0f * d.t );
		else
			s = pos + 6.0f * k * (Real)sin( q / ( 0.11f * R ) - 6.0f * d.t ) * ( 1.0f - d.snap ) + 3.0f * k * (Real)sin( q / ( 0.07f * R ) - 14.0f * d.t ) * d.snap;
		Real px, py;
		d.along( s, q, px, py );
		front.add( px, py );
	}
	bandPoly( d.fin, front.x, front.y, front.n, FALSE, 7.0f * k, d.tint, d.app, 1.2f );
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	polyStrip( P_HAIR, front.x, front.y, front.n, FALSE, 10.0f * k, pingTintOf( d.tint ), 0.5f * d.app, FALSE );
	s_ctx.additive = was;
	const Poly box = steppedBox( d.x, d.y, h, hd, hw, 0.14f * R );
	bandPoly( d.fin, box.x, box.y, box.n, TRUE, 18.0f * k, d.tint, d.app );
	const Poly lac = steppedBox( d.x, d.y, h, hd - 10.0f * k, hw - 10.0f * k, 0.14f * R );
	polyStrip( P_HAIR, lac.x, lac.y, lac.n, TRUE, 3.0f * k, TINT_LACQ, d.app, FALSE );
	for( Int sx = -1; sx <= 1; sx += 2 )
		for( Int sy = -1; sy <= 1; sy += 2 )
		{
			Real px, py;
			d.along( (Real)sx * hd, (Real)sy * 0.5f * hw, px, py );
			studAt( d, px, py, 0.9f );
		}
}

/// a war drum: a sunburst beating twice like a heart, red lacquer and gold drum studs inside
static void drawFrenzy( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Real per = d.aim ? 0.9f : 0.55f;
	const Real tt = wrap( d.t, per );
	const Real p = (Real)exp( -( tt / 0.06f ) * ( tt / 0.06f ) ) + 0.6f * (Real)exp( -( ( tt - 0.2f ) / 0.06f ) * ( ( tt - 0.2f ) / 0.06f ) );
	const Real fade = d.aim ? 1.0f : 1.0f - 0.5f * smoothstep( 1.5f, 3.4f, d.tl );
	const Real Rs = R * ( 1.0f + 0.035f * p + ( d.aim ? 0.0f : 0.15f * ( 1.0f - d.snap ) ) );
	bandCircle( d.fin, d.x, d.y, Rs, 18.0f * k, d.tint, fade * d.app, 0.75f + 0.35f * p );
	for( Int j = 0; j < 20; ++j )
	{
		const Real a = (Real)j * 2.0f * MARKER_PI / 20.0f;
		Real px, py;
		d.at( a, Rs + ( 12.0f + 2.5f * p ) * k, px, py );
		const Real s = ( 16.0f + 5.0f * p ) * k;
		part( P_SHAPE_TRI, px, py, d.yaw + a, s, s, scaled( d.tint, 0.8f + 0.5f * p ), fade * d.app );
	}
	ring( P_HAIR, d.x, d.y, 0.72f * Rs, 5.0f * k, TINT_LACQ, fade * d.app, FALSE );
	for( Int j = 0; j < 8; ++j )
	{
		Real px, py;
		d.at( MARKER_PI / 8.0f + (Real)j * 0.25f * MARKER_PI, 0.84f * Rs, px, py );
		studAt( d, px, py, 1.2f, fade );
	}
	glowDisc( d.x, d.y, Rs, d.tint, 0.12f * p * fade * d.app );
}

/// nuclear: the trefoil turns, slams upright on the click; twelve arc segments count the flight down
static void drawNeutron( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	Real rot = -0.4f * d.tc;
	if( !d.aim )
	{
		const Real target = (Real)floor( rot / ( 2.0f * MARKER_PI / 3.0f ) + 0.5f ) * 2.0f * MARKER_PI / 3.0f;
		rot += ( target - rot ) * d.snap;
	}
	const Real sc = d.aim ? 1.0f : 1.0f + 0.2f * ( 1.0f - d.snap );
	const Real step = 15.0f * MARKER_DEG;
	const Int r = (Int)floorf( rot / step + 0.5f );
	const Int piece = P_GLYPH_NUKE00 + ( ( r % 8 ) + 8 ) % 8;
	// a smaller see-through trefoil: the ground shows through it
	quadPiece( piece, d.x, d.y, d.yaw + ( rot - (Real)r * step ), 1.15f * R * sc, 1.15f * R * sc, d.tint, 0.45f * d.app );
	bandCircle( d.fin, d.x, d.y, R, 24.0f * k, d.tint, d.app );
	ring( P_HAIR, d.x, d.y, R, 3.0f * k, TINT_LACQ, d.app, FALSE );
	const Int lit = (Int)ceilf( 12.0f * d.frac );
	const Bool blinkOff = !d.aim && sin( 18.0f * d.t ) < 0.0f;
	const Real rc = R + 26.0f * k;
	for( Int j = 0; j < 12; ++j )
	{
		// twelve arc segments of a ring round the blast, never square blocks on a round strike
		const Real a = d.yaw + 0.5f * MARKER_PI - (Real)j * MARKER_PI / 6.0f;
		const Real half = 0.3f * ( 2.0f * MARKER_PI / 12.0f );
		bandArc( d.fin, d.x, d.y, rc, 14.0f * k, a - half, a + half, d.tint, d.app, 1.0f, FALSE );
		Bool on;
		if( d.aim )
			on = j == (Int)floor( d.t * 8.0f ) % 12;
		else
			on = j < lit && !( j == lit - 1 && blinkOff );
		arcStrip( bandPiece( d.fin, 3 ), d.x, d.y, rc, INLAY_WIDE * BAND_THIN * 14.0f * k, a - 0.85f * half, a + 0.85f * half, FALSE,
							on ? d.tint : scaled( d.tint, 0.25f ), d.app, FALSE );
	}
}

/// the hover offset of a marker under a Helix, still once the bomb is away
static void helixSway( const DesignDraw &d, Real phase, Real &cx, Real &cy )
{
	const Real t = d.aim ? d.t : d.tc;
	const Real hold = d.aim ? 1.0f : 1.0f - d.snap;
	d.screen( 9.0f * d.k * (Real)sin( 1.3f * t + phase ) * hold, 6.0f * d.k * (Real)sin( 1.9f * t ) * hold, cx, cy );
}

/// a hovering drop: the ring sways with the Helix, a ring falls on a loop, flame tongues burst out
static void drawHelixNapalm( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	Real cx, cy;
	helixSway( d, 0.0f, cx, cy );
	if( !d.aim && d.tl >= 0.5f )
	{
		const Real e = easeOut( ( d.tl - 0.5f ) / 0.4f );
		fillDisc( cx, cy, 0.3f * R, TINT_SCORCH, 0.5f * d.app, 0.0f );
		glowDisc( cx, cy, R, d.tint, 0.4f * d.app );
		for( Int j = 0; j < 8; ++j )
		{
			const Real a = (Real)j * 0.25f * MARKER_PI + 0.2f;
			const Real len = ( 0.3f + 0.75f * e ) * R * ( 0.85f + 0.15f * (Real)sin( 11.0f * d.t + (Real)j ) );
			const Real px = cx + ( 0.1f * R + 0.5f * len ) * (Real)cos( d.yaw + a ), py = cy + ( 0.1f * R + 0.5f * len ) * (Real)sin( d.yaw + a );
			part( P_SHAPE_TONGUE, px, py, d.yaw + a, len * 1.6f, len * 0.55f, scaled( d.tint, 1.15f ), d.app );
		}
	}
	bandCircle( d.fin, cx, cy, R, 10.0f * k, d.tint, d.app );
	for( Int j = 0; j < 6; ++j )
	{
		const Real a = d.yaw + MARKER_PI / 6.0f + (Real)j * MARKER_PI / 3.0f;
		part( P_SHAPE_TONGUE, cx + ( R + 10.0f * k ) * (Real)cos( a ), cy + ( R + 10.0f * k ) * (Real)sin( a ), a, 16.0f * k, 16.0f * k, d.tint, d.app );
	}
	if( d.aim )
	{
		const Real fr = wrap( 0.7f * d.t, 1.0f );
		ring( P_HAIR, cx, cy, R * ( 1.0f - fr ), 3.0f * k, TINT_WARM, 0.6f * (Real)sin( MARKER_PI * fr ) * d.app, FALSE );
	}
	else if( d.tl < 0.5f )
		ring( P_HAIR, cx, cy, 0.8f * R * ( 1.0f - d.tl / 0.5f ), 3.5f * k, TINT_WARM, 0.9f * d.app, FALSE );
	part( P_SHAPE_DISC, cx, cy, d.yaw, 11.0f * k, 11.0f * k, d.tint, d.app );
}

/// a small round warning plate under the hovering Helix; a ring drops and three shocks go out
static void drawHelixNuke( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	Real cx, cy;
	helixSway( d, 1.0f, cx, cy );
	bandCircle( d.fin, cx, cy, R, 16.0f * k, d.tint, d.app );
	ring( P_HAIR, cx, cy, R - 10.0f * k, 3.0f * k, TINT_LACQ, d.app, FALSE );
	for( Int i = 0; i < 6; ++i )
	{
		const Real a = d.yaw + 0.5f * MARKER_PI + (Real)i * MARKER_PI / 3.0f;
		studAt( d, cx + R * (Real)cos( a ), cy + R * (Real)sin( a ), 0.9f );
	}
	const Real flash = d.aim ? 0.0f : MAX( 0.0f, 1.0f - (Real)fabs( d.tl - 0.55f ) / 0.3f );
	const Tint hot = { 1.0f, 0.95f, 0.85f };
	const Tint tre = { d.tint.r + ( hot.r - d.tint.r ) * flash, d.tint.g + ( hot.g - d.tint.g ) * flash, d.tint.b + ( hot.b - d.tint.b ) * flash };
	quadPiece( P_GLYPH_NUKE00, cx, cy, d.yaw, 0.55f * R, 0.55f * R, tre, 0.7f * d.app );
	if( d.aim || d.tl < 0.5f )
	{
		const Real fr = d.aim ? wrap( 0.6f * d.t, 1.0f ) : d.tl / 0.5f;
		const Real s = d.aim ? 1.9f - 1.6f * fr : 1.6f - 1.4f * fr;
		ring( P_HAIR, cx, cy, R * s, 3.0f * k, d.tint, ( d.aim ? 0.7f * (Real)sin( MARKER_PI * fr ) : 0.9f ) * d.app, FALSE );
	}
	if( d.aim )
		return;
	for( Int j = 0; j < 3; ++j )
	{
		const Real t0 = 0.5f + 0.17f * (Real)j;
		const Real e = easeOut( ( d.tl - t0 ) / 0.8f );
		if( d.tl > t0 && e < 1.0f )
			softRing( cx, cy, 1.5f * R * e, ( 10.0f - 4.0f * j ) * k, pingTintOf( d.tint ), (Real)pow( 1.0f - e, 0.7f ) * d.app );
	}
}

/// a round landing ring on a four-point harness: it swings while aiming and slams down on the click
static void drawTankDrop( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Real sway = d.aim ? 0.14f * (Real)sin( 1.7f * d.t ) : 0.14f * (Real)sin( 1.7f * d.tc ) * ( 1.0f - d.snap );
	const Real sc = d.aim ? 1.0f : 1.0f + 0.25f * ( 1.0f - d.snap ) - 0.05f * (Real)sin( MARKER_PI * clamp01( ( d.tl - 0.22f ) / 0.2f ) );
	const Real h = d.heading() + sway, hs = 0.8f * R * sc;
	for( Int i = 0; i < 4; ++i )
	{
		const Real a = h + 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)i;
		const Real ca = (Real)cos( a ), sa = (Real)sin( a );
		hairLine( d.x + 0.92f * hs * ca, d.y + 0.92f * hs * sa, d.x + 0.2f * hs * ca, d.y + 0.2f * hs * sa, 2.4f * k, TINT_STEEL, 0.9f * d.app );
	}
	bandCircle( d.fin, d.x, d.y, hs, 16.0f * k, d.tint, d.app );
	bandCircle( d.fin, d.x, d.y, 0.14f * R * sc, 10.0f * k, d.tint, d.app );
	for( Int i = 0; i < 4; ++i )
	{
		const Real a = h + 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)i;
		studAt( d, d.x + hs * (Real)cos( a ), d.y + hs * (Real)sin( a ), 0.8f );
	}
	if( d.aim )
		return;
	const Real e = easeOut( ( d.tl - 0.2f ) / 0.5f );
	if( d.tl > 0.2f && e < 1.0f )
		softRing( d.x, d.y, hs * ( 1.0f + 0.35f * e ), 6.0f * k, pingTintOf( d.tint ), 0.9f * ( 1.0f - e ) * d.app );
}

// ---------------------------------------------------------------- GLA

/// nine missiles: round impact marks inside a welded ragged ring, a round lamp per missile counting down
static void drawScud( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, yaw = d.yaw;
	auto ragged = [=]( Real a ) -> Real
	{
		const Real p = a - yaw;
		return R * ( 1.0f + 0.022f * (Real)sin( 7.0f * p + 1.0f ) + 0.014f * (Real)sin( 13.0f * p + 2.0f ) );
	};
	bandRadial( d.fin, d.x, d.y, ragged, 20.0f * k, d.tint, d.app, 1.0f, 3.0f );
	for( Int i = 0; i < 5; ++i )
	{
		const Real a = 2.0f * MARKER_PI * hashOf( i + 61 );
		Real px, py;
		d.at( a, ragged( yaw + a ), px, py );
		studAt( d, px, py, 1.2f + 0.5f * hashOf( i + 63 ) );		// odd-sized round bolts, no square plates on a round strike
	}
	for( Int i = 0; i < 10; ++i )		// hazard slashes on the top edge only
	{
		const Real a = 1.15f + 0.085f * (Real)i;
		Real px, py;
		d.at( a, ragged( yaw + a ) + 13.0f * k, px, py );
		part( P_SHAPE_PARA, px, py, yaw + a + 0.5f * MARKER_PI, 9.0f * k, 7.0f * k, ( i & 1 ) ? TINT_DARK : TINT_AMBER, d.app );
	}
	const Int nl = d.aim ? 9 : (Int)ceilf( 9.0f * d.frac );
	const Bool blinkOff = !d.aim && sin( 18.0f * d.t ) < 0.0f;
	for( Int i = 0; i < 9; ++i )
	{
		const Real a = 0.3f + 2.0f * MARKER_PI * ( (Real)i + 0.35f * hashOf( i + 71 ) ) / 9.0f;
		Real px, py;
		d.at( a, R + 26.0f * k, px, py );
		part( P_SHAPE_RING, px, py, yaw + a, 15.0f * k, 15.0f * k, TINT_STEEL, d.app );
		const Bool on = i < nl && !( i == nl - 1 && blinkOff );
		part( P_SHAPE_DISC, px, py, yaw + a, 9.0f * k, 9.0f * k, on ? d.tint : scaled( d.tint, 0.22f ), d.app );
	}
	for( Int i = 0; i < 9; ++i )
	{
		Real px, py;
		d.at( 2.0f * MARKER_PI * hashOf( i + 83 ), 0.72f * R * (Real)sqrt( hashOf( i + 81 ) ), px, py );
		const Real rot = 2.0f * MARKER_PI * hashOf( i + 85 ) + ( d.aim ? 0.0f : 0.5f * ( 1.0f - d.snap ) );
		const Bool lit = !d.aim || hashOf( i, (Real)floor( d.t * 7.0f ) ) > 0.5f;
		part( P_SHAPE_RING, px, py, yaw + rot, 20.0f * k, 20.0f * k, scaled( d.tint, lit ? 1.1f : 0.4f ), 0.75f * d.app );
		if( d.impact >= 0.0f )
			pingRing( px, py, 16.0f * k, k, d.impact - 0.08f * (Real)( ( i * 5 ) % 9 ), d.tint, 0.5f, 1.4f, 5.0f );
	}
}

/// a gas cloud: a lumpy edge that churns, a haze inside, bubbles drifting up and popping
static void drawAnthrax( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, yaw = d.yaw, t = d.t;
	const Bool gamma = d.m->variant == VARIANT_CHEM;
	const Real sc = d.aim ? 1.0f : 0.62f + 0.38f * easeBack( d.tl / 0.35f, 2.2f );
	auto churn = [=]( Real a ) -> Real
	{
		const Real p = a - yaw;
		return R * sc * ( 1.0f + 0.06f * (Real)sin( 5.0f * p + 0.7f * t ) + 0.04f * (Real)sin( 8.0f * p - 1.1f * t + 1.0f )
											+ 0.03f * (Real)sin( 13.0f * p + 1.7f * t ) );
	};
	Poly edge;
	for( Int i = 0; i < 48; ++i )
	{
		const Real a = yaw + 2.0f * MARKER_PI * (Real)i / 48.0f;
		const Real r = churn( a );
		edge.add( d.x + r * (Real)cos( a ), d.y + r * (Real)sin( a ) );
	}
	const Real haze = ( 0.06f + 0.025f * (Real)sin( 2.0f * t ) ) * d.app;		// a light see-through haze
	fillFan( d.x, d.y, edge.x, edge.y, edge.n, TRUE, d.tint, haze, haze );
	bandRadial( d.fin, d.x, d.y, churn, 10.0f * k, d.tint, d.app, 1.0f, 3.0f );
	const Int nb = gamma ? 20 : 12;
	const Real spd = d.aim ? 1.0f : 2.0f;
	for( Int i = 0; i < nb; ++i )
	{
		const Real bu = 0.75f * R * ( 2.0f * hashOf( i + 91 ) - 1.0f );
		const Real life = 1.6f + hashOf( i + 92 );
		const Real age = wrap( t * spd + hashOf( i + 93 ) * life, life );
		const Real bv = 0.75f * R * ( 2.0f * hashOf( i + 94 ) - 1.0f ) + 30.0f * age;
		if( bu * bu + bv * bv > 0.64f * R * R )
			continue;
		Real px, py;
		d.screen( bu, bv, px, py );
		const Real rad = ( 3.0f + 4.0f * age / life ) * k;
		if( age > life - 0.25f )
			pingRing( px, py, rad, k, age - ( life - 0.25f ), d.tint, 0.25f, 1.0f, 2.0f );
		else
			part( P_SHAPE_RING, px, py, yaw, rad * 3.0f, rad * 3.0f, d.tint, 0.9f * d.app );
	}
	const Real studA[ 4 ] = { 0.4f, 2.2f, 3.9f, 5.3f };
	for( Int i = 0; i < 4; ++i )
	{
		Real px, py;
		d.at( studA[ i ], churn( yaw + studA[ i ] ), px, py );
		studAt( d, px, py, 0.9f );
	}
}

/// tunnel hatches: lids jiggle while aiming, pop off one after another, rebels run out of the holes
static void drawAmbush( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	for( Int i = 0; i < 14; ++i )
	{
		const Real hv = hashOf( i + 1 ), hv2 = hashOf( i + 57 );
		const Real c = d.yaw + 2.0f * MARKER_PI * ( (Real)i + 0.5f ) / 14.0f;
		const Real half = ( 0.2f + 0.18f * hv ) * 2.0f * MARKER_PI / 14.0f;
		bandArc( d.fin, d.x, d.y, R + 4.0f * k * ( hv2 - 0.5f ), ( 12.0f + 8.0f * hv ) * k, c - half, c + half, d.tint, d.app );
	}
	static const Real holeA[ 6 ] = { 0.3f, 1.5f, 2.6f, 3.7f, 4.6f, 5.6f };
	static const Int perm[ 6 ] = { 3, 0, 5, 1, 4, 2 };
	for( Int i = 0; i < 6; ++i )
	{
		Real hx, hy;
		d.at( holeA[ i ], 0.55f * R * ( 0.7f + 0.5f * hashOf( i + 101 ) ), hx, hy );
		fillDisc( hx, hy, 6.5f * k, TINT_DARK, 0.55f * d.app, 0.55f * d.app, 12 );		// a small see-through pit
		bandCircle( d.fin, hx, hy, 7.0f * k, 6.0f * k, m.variant == VARIANT_CHEM ? TINT_TOXIN : TINT_STEEL, d.app );
		Real ox = 0.0f, oy = 0.0f, la = 1.0f;
		if( d.aim )
		{
			ox = 1.4f * k * (Real)sin( 9.0f * d.t + 2.1f * (Real)i );
			oy = 1.4f * k * (Real)cos( 7.0f * d.t + (Real)i );
		}
		else
		{
			const Real t0 = 0.1f + 0.12f * (Real)perm[ i ];
			const Real e = easeOut( ( d.tl - t0 ) / 0.3f );
			const Real a = 2.0f * MARKER_PI * hashOf( i + 111 );
			ox = 16.0f * k * e * (Real)cos( a );
			oy = 16.0f * k * e * (Real)sin( a );
			la = 1.0f - smoothstep( 0.25f, 0.6f, d.tl - t0 );
			const Real toward = (Real)atan2( d.y - hy, d.x - hx );
			for( Int r = 0; r < 3; ++r )
			{
				const Real age = d.tl - t0 - 0.15f - 0.12f * (Real)r;
				if( age <= 0.0f || age >= 1.0f )
					continue;
				const Real e2 = easeOut( age ) * 0.7f;
				const Real side = 10.0f * k * (Real)( r - 1 ) * e2;
				const Real px = hx + ( d.x - hx ) * e2 - (Real)sin( toward ) * side;
				const Real py = hy + ( d.y - hy ) * e2 + (Real)cos( toward ) * side;
				quadPiece( P_CHEVRON, px, py, toward, 11.0f * k, 17.0f * k, d.tint, ( 1.0f - smoothstep( 0.6f, 1.0f, age ) ) * d.app,
									 CHEVRON_ANCHOR_U, 0.5f );
			}
		}
		if( la > 0.02f )
		{
			part( P_SHAPE_LID, hx + ox, hy + oy, d.yaw + 0.6f + (Real)i, 23.0f * k, 23.0f * k, TINT_WHITE, la * d.app );
			if( m.variant == VARIANT_DEMO && sin( 8.0f * d.t + (Real)i ) > 0.3f )
				part( P_SHAPE_DISC, hx + ox, hy + oy, d.yaw, 6.0f * k, 6.0f * k, TINT_RED, la * d.app );
		}
	}
}

/// a glitching ring: segments jitter off their radius, static blocks pop; after the click it hides
static void drawGps( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	Real amp, a;
	Int tick;
	if( d.aim )
	{
		amp = 1.0f;  a = 1.0f;  tick = (Int)floor( d.t * 10.0f );
	}
	else if( d.tl < 0.35f )
	{
		amp = 3.0f;  a = 1.0f;  tick = (Int)floor( d.t * 20.0f );
	}
	else
	{
		tick = (Int)floor( d.tl * 12.0f );
		const Bool glitch = hashOf( tick, 7.0f ) > 0.82f;
		amp = glitch ? 2.0f : 0.4f;
		a = glitch ? 0.8f : 0.25f;
	}
	const Real step = 2.0f * MARKER_PI / 20.0f;
	for( Int i = 0; i < 20; ++i )
	{
		const Real off = 6.0f * k * amp * ( hashOf( i, (Real)tick * 4.7f ) - 0.5f );
		const Real ang = d.yaw + ( (Real)i + 0.5f ) * step + 0.05f * amp * ( hashOf( i + 30, (Real)tick * 9.3f ) - 0.5f );
		bandArc( d.fin, d.x, d.y, R + off, 12.0f * k, ang - 0.4f * step, ang + 0.4f * step, d.tint, a * d.app );
	}
	for( Int i = 0; i < 10; ++i )
	{
		const Real bu = 1.3f * R * ( 2.0f * hashOf( i, (Real)tick + 0.5f ) - 1.0f );
		const Real bv = 1.3f * R * ( 2.0f * hashOf( i + 50, (Real)tick + 0.5f ) - 1.0f );
		const Real r = (Real)sqrt( bu * bu + bv * bv );
		if( r < 0.55f * R || r > 1.25f * R )
			continue;
		// static as short arcs of the ring it scrambles, never square blocks
		const Real len = ( 3.0f + 8.0f * hashOf( i + 9, (Real)tick ) ) * k * (Real)sqrt( amp );
		const Real ac = d.yaw + (Real)atan2( bv, bu );
		arcStrip( P_HAIR, d.x, d.y, r, 3.0f * k, ac - 0.5f * len / r, ac + 0.5f * len / r, TRUE, d.tint, 0.65f * a * d.app, FALSE );
	}
	const Real spin = 0.4f * d.tc;
	for( Int j = 0; j < 3; ++j )
	{
		const Real c = d.yaw + spin + (Real)j * 2.0f * MARKER_PI / 3.0f;
		bandArc( d.fin, d.x, d.y, 0.18f * R, 8.0f * k, c - 0.7f, c + 0.7f, d.tint, a * d.app );
	}
}

/// a radar scope of welded plates: a sweep with afterglow, one fast turn on the click, blips
static void drawRadar( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	static const Real cuts[ 7 ] = { 0.0f, 0.8f, 1.9f, 2.7f, 3.5f, 4.6f, 5.4f };
	for( Int i = 0; i < 7; ++i )
	{
		const Real g0 = ( 2.0f + 3.0f * hashOf( i + 121 ) ) * k / R;
		const Real g1 = ( 2.0f + 3.0f * hashOf( ( i + 1 ) % 7 + 121 ) ) * k / R;
		const Real a0 = cuts[ i ] + g0, a1 = ( i < 6 ? cuts[ i + 1 ] : 2.0f * MARKER_PI ) - g1;
		bandArc( d.fin, d.x, d.y, R + 1.5f * k * ( hashOf( i + 131 ) - 0.5f ), ( 16.0f + 5.0f * hashOf( i + 141 ) ) * k, d.yaw + a0, d.yaw + a1, d.tint, d.app );
		if( ( i & 1 ) == 0 )
		{
			Real px, py;
			d.at( cuts[ i ] + 0.25f, R, px, py );
			studAt( d, px, py, 0.85f );
		}
	}
	ring( P_HAIR, d.x, d.y, 0.34f * R, 3.0f * k, TINT_STEEL, 0.7f * d.app, FALSE );
	ring( P_HAIR, d.x, d.y, 0.67f * R, 3.0f * k, TINT_STEEL, 0.7f * d.app, FALSE );
	const Real phi = d.aim ? 0.9f * d.t : 0.9f * d.tc + 2.0f * MARKER_PI * easeOut( d.tl / 1.1f ) + 0.9f * MAX( 0.0f, d.tl - 1.1f );
	for( Int i = 0; i < 10; ++i )
	{
		const Real a0 = phi - 0.11f * (Real)( i + 1 ), a1 = phi - 0.11f * (Real)i;
		const Real f = 1.0f - ( (Real)i + 0.5f ) / 10.0f;
		Poly w;
		for( Int s = 0; s <= 2; ++s )
		{
			Real px, py;
			d.at( a0 + ( a1 - a0 ) * (Real)s / 2.0f, R - 6.0f * k, px, py );
			w.add( px, py );
		}
		fillFan( d.x, d.y, w.x, w.y, w.n, FALSE, TINT_RADAR, 0.32f * f * f * d.app, 0.32f * f * f * d.app );
	}
	Real lx, ly;
	d.at( phi, R - 6.0f * k, lx, ly );
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	hairLine( d.x, d.y, lx, ly, 3.0f * k, TINT_RADAR, 0.8f * d.app );
	s_ctx.additive = was;
	for( Int i = 0; i < 9; ++i )
	{
		const Real ba = 2.0f * MARKER_PI * hashOf( i + 133 );
		const Real a = (Real)exp( -wrap( phi - ba, 2.0f * MARKER_PI ) * 1.4f ) * ( d.aim ? 0.35f : 1.0f );
		if( a < 0.03f )
			continue;
		Real px, py;
		d.at( ba, 0.85f * R * (Real)sqrt( hashOf( i + 131 ) ), px, py );
		part( P_SHAPE_DISC, px, py, d.yaw, 8.0f * k, 8.0f * k, TINT_RADAR, a * d.app );
	}
	studAt( d, d.x, d.y, 1.0f );
}

// ---------------------------------------------------------------- Turkiye

/// a swarm: small drones circling in two rings inside a broken circle; on the click they dive in
static void drawKargu( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Int nd = d.m->variant == VARIANT_BERET ? 3 : 12;
	const Real shrink = d.aim ? 1.0f : 1.0f - 0.06f * smoothstep( 0.2f, 1.5f, d.tl );
	const Real rr = R * shrink;
	const Real gap = 0.12f * R / MAX( 1.0f, rr );
	for( Int j = 0; j < 6; ++j )
	{
		const Real a = d.yaw + (Real)j * MARKER_PI / 3.0f;
		bandArc( d.fin, d.x, d.y, rr, 14.0f * k, a + gap, a + MARKER_PI / 3.0f - gap, d.tint, d.app );
		studAt( d, d.x + 0.98f * rr * (Real)cos( a ), d.y + 0.98f * rr * (Real)sin( a ), 0.85f );
	}
	for( Int i = 0; i < nd; ++i )
	{
		const Int ringNo = i % 2;
		const Real rad0 = ( ringNo == 0 ? 0.55f : 0.82f ) * R;
		const Real w = ringNo == 0 ? 0.8f : -0.6f;
		const Real a0 = 2.0f * MARKER_PI * (Real)( i / 2 ) / (Real)MAX( 1, nd / 2 ) + (Real)ringNo * 0.3f;
		const Real lr = 0.35f * R * (Real)sqrt( hashOf( i + 141 ) ), la = 2.0f * MARKER_PI * hashOf( i + 143 );
		Real px, py, h, sz = 1.0f;
		const Real dive = d.aim ? -1.0f : ( d.tl - 0.08f * (Real)i ) / 0.6f;
		if( dive < 0.0f )
		{
			const Real a = a0 + w * d.t;
			d.at( a, rad0 + 4.0f * k * (Real)sin( 3.1f * d.t + 1.9f * (Real)i ), px, py );
			h = d.yaw + a + ( w > 0.0f ? 0.5f : -0.5f ) * MARKER_PI;
		}
		else if( dive < 1.0f )
		{
			const Real a1 = a0 + w * ( d.tc + 0.08f * (Real)i );
			const Real a = a1 + ( w > 0.0f ? 1.8f : -1.8f ) * dive;
			const Real rad = rad0 + ( lr - rad0 ) * dive * dive;
			Real lx, ly;
			d.at( a, rad, px, py );
			d.at( la, lr, lx, ly );
			px += ( lx - px ) * dive * dive * dive;
			py += ( ly - py ) * dive * dive * dive;
			h = d.yaw + a + ( w > 0.0f ? 0.5f : -0.5f ) * MARKER_PI - ( w > 0.0f ? 0.9f : -0.9f ) * dive;
			sz = 1.0f - 0.35f * dive;
		}
		else
		{
			d.at( la, lr, px, py );
			pingRing( px, py, 7.0f * k, k, d.tl - 0.08f * (Real)i - 0.6f, d.tint, 0.4f, 1.6f, 3.6f );
			continue;
		}
		part( P_SHAPE_TRI, px, py, h, 26.0f * k * sz, 26.0f * k * sz, scaled( d.tint, 1.15f ), d.app );		// drones keep their presence
	}
}

/// a loiter pattern: a racetrack the UAV flies, its camera footprint panning across the zone
static void drawUav( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	const Real cxo = 0.42f * R, rc = 0.5f * R;
	Poly track;
	for( Int i = 0; i <= 12; ++i )
	{
		const Real a = -0.5f * MARKER_PI + MARKER_PI * (Real)i / 12.0f;
		Real px, py;
		d.screen( cxo + rc * (Real)cos( a ), rc * (Real)sin( a ), px, py );
		track.add( px, py );
	}
	for( Int i = 0; i <= 12; ++i )
	{
		const Real a = 0.5f * MARKER_PI + MARKER_PI * (Real)i / 12.0f;
		Real px, py;
		d.screen( -cxo + rc * (Real)cos( a ), rc * (Real)sin( a ), px, py );
		track.add( px, py );
	}
	const Real atr = d.aim ? 0.9f : 1.0f;
	// the area it watches is a circle; the loiter track inside it is the flight, so a hairline
	bandCircle( d.fin, d.x, d.y, R, 12.0f * k, d.tint, d.app );
	polyStrip( P_HAIR, track.x, track.y, track.n, TRUE, 3.0f * k, d.tint, 0.85f * atr * d.app, FALSE );
	// the UAV on it, round at 170 units a second
	const Real perim = 4.0f * cxo + 2.0f * MARKER_PI * rc;
	Real p = wrap( d.t * 170.0f, perim ), u, v, hu;
	if( p < 2.0f * cxo )
	{
		u = -cxo + p;  v = -rc;  hu = 0.0f;
	}
	else if( ( p -= 2.0f * cxo ) < MARKER_PI * rc )
	{
		const Real a = -0.5f * MARKER_PI + p / rc;
		u = cxo + rc * (Real)cos( a );  v = rc * (Real)sin( a );  hu = a + 0.5f * MARKER_PI;
	}
	else if( ( p -= MARKER_PI * rc ) < 2.0f * cxo )
	{
		u = cxo - p;  v = rc;  hu = MARKER_PI;
	}
	else
	{
		p -= 2.0f * cxo;
		const Real a = 0.5f * MARKER_PI + p / rc;
		u = -cxo + rc * (Real)cos( a );  v = rc * (Real)sin( a );  hu = a + 0.5f * MARKER_PI;
	}
	Real ux, uy;
	d.screen( u, v, ux, uy );
	drawDart( ux, uy, d.yaw + hu, 0.9f, k, d.tint, atr * d.app );
	Real tx = d.x, ty = d.y;
	if( !d.aim )
		d.screen( 0.25f * R * (Real)sin( 0.9f * d.tl ), 0.2f * R * (Real)cos( 0.7f * d.tl ) - 0.2f * R, tx, ty );
	const Real dx = tx - ux, dy = ty - uy;
	const Real dl = MAX( 1.0f, (Real)sqrt( dx * dx + dy * dy ) );
	const Real nx = dx / dl, ny = dy / dl, qx = -ny, qy = nx;
	const Real wid = d.aim ? 0.32f : 0.5f;
	const Real nearD = 0.3f * dl, farD = MIN( dl + 0.25f * R, 1.25f * dl );
	Poly fp;
	fp.add( ux + nx * nearD - qx * 0.08f * R, uy + ny * nearD - qy * 0.08f * R );
	fp.add( ux + nx * farD - qx * wid * R, uy + ny * farD - qy * wid * R );
	fp.add( ux + nx * farD + qx * wid * R, uy + ny * farD + qy * wid * R );
	fp.add( ux + nx * nearD + qx * 0.08f * R, uy + ny * nearD + qy * 0.08f * R );
	Real cx = 0.0f, cy = 0.0f;
	for( Int i = 0; i < 4; ++i )
	{
		cx += 0.25f * fp.x[ i ];
		cy += 0.25f * fp.y[ i ];
	}
	const Real fa = ( d.aim ? 0.13f : 0.16f ) * d.app;
	fillFan( cx, cy, fp.x, fp.y, fp.n, TRUE, d.tint, fa, fa );
	polyStrip( P_HAIR, fp.x, fp.y, fp.n, TRUE, 3.0f * k, d.tint, ( d.aim ? 0.75f : 0.85f ) * d.app, FALSE );
	const Real bd = 1.41f * ( d.aim ? 0.3f * R : 0.3f * R - 0.12f * R * d.snap );
	for( Int j = 0; j < 4; ++j )
	{
		// focus brackets as arcs round the aim, not square corners
		const Real a = d.yaw + 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)j;
		const Real half = 12.0f * k / MAX( bd, 1.0f );
		bandArc( d.fin, d.x, d.y, bd, 6.0f * k, a - half, a + half, d.tint, d.app );
	}
}

/// a ballistic descent: rings falling onto a round target, six arcs of it counting down
static void drawTayfun( const DesignDraw &d )
{
	const Real k = d.k, R = d.R;
	bandArc( d.fin, d.x, d.y, R, 18.0f * k, 0.0f, 2.0f * MARKER_PI, d.tint, d.app, 1.0f, FALSE );
	const Int lit = d.aim ? 6 : (Int)ceilf( 6.0f * d.frac );
	const Bool blinkOff = !d.aim && sin( 18.0f * d.t ) < 0.0f;
	for( Int m = 0; m < 6; ++m )
	{
		const Int order = ( ( 1 - m ) % 6 + 6 ) % 6;		// clockwise from the top arc
		const Bool on = order < lit && !( order == lit - 1 && blinkOff );
		const Real a0 = d.yaw + (Real)m * MARKER_PI / 3.0f;
		arcStrip( bandPiece( d.fin, 3 ), d.x, d.y, R, INLAY_WIDE * BAND_THIN * 18.0f * k, a0 + 0.05f, a0 + MARKER_PI / 3.0f - 0.05f, FALSE,
							on ? d.tint : scaled( d.tint, 0.22f ), d.app, FALSE );
	}
	const Real rt = d.aim ? R + 24.0f * k : R + 24.0f * k - 12.0f * k * d.snap;
	for( Int j = 0; j < 3; ++j )
	{
		const Real a = 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / 3.0f;
		Real px, py;
		d.at( a, rt, px, py );
		part( P_SHAPE_TRI, px, py, d.yaw + a + MARKER_PI, 20.0f * k, 20.0f * k, d.tint, d.app );
	}
	const Bool landed = !d.aim && d.frac <= 0.0f;
	if( !landed )
	{
		const Real spd = d.aim ? 0.5f : 1.1f;
		for( Int i = 0; i < 3; ++i )
		{
			const Real fr = wrap( d.t * spd + (Real)i / 3.0f, 1.0f );
			ring( P_HAIR, d.x, d.y, R * ( 2.2f - 2.0f * fr ), 3.0f * k, d.tint, 0.75f * (Real)sin( MARKER_PI * fr ) * d.app, FALSE );
		}
	}
	part( P_SHAPE_DISC, d.x, d.y, d.yaw, 11.0f * k, 11.0f * k, d.tint, d.app );
}

/// loitering munitions: lock rings roam the zone, snap onto targets on the click, then the dives
static void drawSwarm( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	for( Int j = 0; j < 8; ++j )
	{
		// eight arcs of the strike's circle, stepped in and out like armour panels
		const Real off = ( ( j & 1 ) ? 2.4f : -2.4f ) * k;
		const Real a0 = d.yaw + (Real)j * 0.25f * MARKER_PI;
		bandArc( d.fin, d.x, d.y, R + off, 14.0f * k, a0 + 0.04f, a0 + 0.25f * MARKER_PI - 0.04f, d.tint, d.app );
	}
	// as many as the strike sends: up to seven with the rank 9 card and the third tier (TalentsReforged.ini)
	static const Real targetU[ 7 ] = { 0.15f, -0.45f, 0.5f, -0.2f, 0.35f, -0.62f, 0.62f };
	static const Real targetV[ 7 ] = { 0.05f, 0.3f, -0.35f, -0.55f, 0.55f, -0.15f, 0.18f };
	const Int n = MAX( 1, MIN( 7, m.swarmCount ) );
	for( Int i = 0; i < n; ++i )
	{
		const Real fi = (Real)i;
		const Real t = d.aim ? d.t : d.tc;
		const Real ru = 0.6f * R * (Real)sin( 0.7f * t * ( 1.0f + 0.2f * fi ) + fi ), rv = 0.55f * R * (Real)sin( 0.9f * t * ( 1.0f + 0.13f * fi ) + 2.0f * fi );
		Real u = ru, v = rv;
		Bool solid = FALSE;
		if( !d.aim )
		{
			const Real e = easeOut( d.tl / 0.3f );
			u = ru + ( targetU[ i ] * R - ru ) * e;
			v = rv + ( targetV[ i ] * R - rv ) * e;
			solid = e >= 1.0f;
		}
		Real cx, cy;
		d.screen( u, v, cx, cy );
		ring( P_HAIR, cx, cy, 10.0f * k, 3.0f * k, d.tint, d.app, FALSE );		// a round lock on a round strike
		if( !solid )
			continue;
		for( Int j = 0; j < 3; ++j )
		{
			const Real a = 0.5f * MARKER_PI + (Real)j * 2.0f * MARKER_PI / 3.0f;
			part( P_SHAPE_TRI, cx + 17.0f * k * (Real)cos( d.yaw + a ), cy + 17.0f * k * (Real)sin( d.yaw + a ), d.yaw + a + MARKER_PI,
						11.0f * k, 11.0f * k, d.tint, d.app );
		}
		const Real t0 = 0.55f + 0.4f * fi;
		const Real fr = ( d.tl - t0 ) / 0.45f;
		if( fr >= 0.0f && fr < 1.0f )
			ring( P_HAIR, cx, cy, ( 40.0f - 34.0f * fr ) * k, 3.0f * k, d.tint, 0.9f * d.app, FALSE );
		pingRing( cx, cy, 10.0f * k, k, d.tl - t0 - 0.45f, d.tint, 0.4f, 1.4f, 4.0f );
		if( d.tl > t0 + 0.45f )
			part( P_SHAPE_DISC, cx, cy, d.yaw, 11.0f * k, 11.0f * k, TINT_SCORCH, 0.6f * d.app );
	}
}

/// jamming: interference bars scrolling through the round field it blinds, a wave running round inside
static void drawBlind( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, yaw = d.yaw;
	const Real hy = 0.92f * R;
	const Real ys = d.aim ? 1.0f : ( d.tl < 0.25f ? 1.0f - 0.9f * d.snap : easeBack( ( d.tl - 0.25f ) / 0.4f, 2.2f ) );
	const Int tick = (Int)floor( d.t * 12.0f );
	const Real inset = 8.0f * k;
	for( Int j = 0; j < 12; ++j )
	{
		const Real v = ( wrap( (Real)j * 0.17f * R + 35.0f * d.t, 2.0f * hy ) - hy ) * MAX( ys, 0.05f );
		const Real av = (Real)fabs( v );
		if( av > R - inset - 2.0f * k )
			continue;
		const Real ext = (Real)sqrt( MAX( 0.0f, ( R - inset ) * ( R - inset ) - av * av ) );		// clipped to the circle
		if( ext < 4.0f * k )
			continue;
		Real ax, ay, bx, by;
		d.screen( -ext, v, ax, ay );
		d.screen( ext, v, bx, by );
		const Real w = ( 2.4f + 5.2f * hashOf( j + 151 ) ) * k;
		if( d.aim || d.tl <= 0.6f )
			hairLine( ax, ay, bx, by, w, d.tint, ( d.aim ? 0.75f : 0.9f ) * d.app );
		else
			dashLine( ax, ay, bx, by, w, 0.3f * R, 0.4f + 0.5f * hashOf( j + 3, (Real)tick ), 40.0f * k * hashOf( j, (Real)tick ), d.tint, 0.9f * d.app );
	}
	const Real amp = d.aim ? 3.0f * k : 3.0f * k + 4.0f * k * smoothstep( 0.3f, 1.2f, d.tl );
	const Real t = d.t;
	auto wave = [=]( Real a ) -> Real { return 0.62f * R + amp * (Real)sin( 10.0f * ( a - yaw ) - 3.0f * t ); };
	radialStrip( P_HAIR, d.x, d.y, wave, 3.0f * k, 0.0f, 2.0f * MARKER_PI, d.tint, 0.9f * d.app, FALSE, 2.0f );
	const Bool was = s_ctx.additive;
	s_ctx.additive = TRUE;
	radialStrip( P_HAIR, d.x, d.y, wave, 7.0f * k, 0.0f, 2.0f * MARKER_PI, pingTintOf( d.tint ), 0.4f * d.app, FALSE, 2.0f );
	s_ctx.additive = was;
	bandCircle( d.fin, d.x, d.y, R, 18.0f * k, d.tint, d.app );
	for( Int j = 0; j < 4; ++j )
	{
		Real px, py;
		d.at( 0.25f * MARKER_PI + 0.5f * MARKER_PI * (Real)j, R, px, py );
		studAt( d, px, py, 0.9f );
	}
}

/// a jump wedge: an acute arrowhead along the run; the troopers (the drops) light tip to tail
static void drawRapid( const DesignDraw &d )
{
	const Marker &m = *d.m;
	const Real k = d.k, R = d.R;
	Poly w;
	Real px, py;
	d.along( -0.33f * R, 0.5f * R, px, py );		w.add( px, py );
	d.along( 0.62f * R, 0.0f, px, py );				w.add( px, py );
	d.along( -0.33f * R, -0.5f * R, px, py );		w.add( px, py );
	ring( P_HAIR, d.x, d.y, 0.62f * R, 3.0f * k, d.tint, 0.8f * d.app, FALSE );		// the landing circle the wedge points across
	bandPoly( d.fin, w.x, w.y, w.n, FALSE, 16.0f * k, d.tint, d.app );
	studAt( d, w.x[ 0 ], w.y[ 0 ], 0.9f );
	studAt( d, w.x[ 2 ], w.y[ 2 ], 0.9f );
	if( !d.aim || m.drops.empty() )
		return;
	const Int n = (Int)m.drops.size();
	const Int lit = (Int)wrap( d.t * 6.0f, (Real)( n + 2 ) );
	if( lit < n && m.drops[ lit ] >= 0.0f )
		part( P_SHAPE_PARA, d.x - d.hx * m.drops[ lit ], d.y - d.hy * m.drops[ lit ], d.heading(), 28.0f * k, 28.0f * k, scaled( d.tint, 1.35f ), d.app );
}

/// a line abreast: four planes' tracks side by side, a pallet at the end of each, between two brackets
static void drawAllyAid( const DesignDraw &d )
{
	const Real k = d.k, h = d.heading();
	static const Real offs[ 4 ] = { -105.0f, -35.0f, 35.0f, 105.0f };
	const Real grow = smoothstep( 0.05f, 0.7f, d.t );
	const Real sp = d.aim ? -1.0e9f : -800.0f + 1200.0f * ( d.tl - 0.3f ) / 2.3f;
	for( Int i = 0; i < 4; ++i )
	{
		const Real o = offs[ i ];
		Real ax, ay, bx, by;
		d.along( -600.0f * grow, o, ax, ay );
		d.along( -26.0f * k, o, bx, by );
		hairLine( ax, ay, bx, by, 3.0f * k, d.tint, 0.6f * d.app );
		for( Real s = -600.0f * grow + wrap( 80.0f * k * d.t, 44.0f * k ); s < -30.0f * k; s += 44.0f * k )
		{
			Real px, py;
			d.along( s, o, px, py );
			quadPiece( P_CHEVRON, px, py, h, 12.0f * k, 18.0f * k, d.tint, 0.9f * d.app, CHEVRON_ANCHOR_U, 0.5f );
		}
		const Real bob = d.aim ? 1.0f + 0.07f * (Real)sin( 3.0f * d.t + 1.6f * (Real)i )
													 : 1.0f + 0.35f * ( 1.0f - easeOut( ( d.tl - 0.06f * (Real)i ) / 0.2f ) );
		Real px, py;
		d.along( 0.0f, o, px, py );
		part( P_SHAPE_PARA, px, py, h + 0.5f * MARKER_PI, 30.0f * k * bob, 30.0f * k * bob, d.tint, d.app );
		if( sp > 0.0f )
			part( P_SHAPE_PLUS, px, py, d.yaw, 16.0f * k, 16.0f * k, TINT_WHITE, d.app );
		if( sp > -800.0f && sp < 400.0f )
		{
			d.along( sp, o, px, py );
			drawDart( px, py, h, 1.1f, k, d.tint, d.app );
		}
	}
	const Real bw = 140.0f;
	for( Int side = -1; side <= 1; side += 2 )
	{
		// the drop's ends as arcs of the circle round the row, not square brackets
		const Real mid = h + (Real)side * 0.5f * MARKER_PI;
		const Real half = 34.0f * k / bw;
		bandArc( d.fin, d.x, d.y, bw, 8.0f * k, mid - half, mid + half, d.tint, d.app );
	}
}

/// an insertion: the landing circle, arc brackets stalking round it in quarter turns
static void drawCommando( const DesignDraw &d )
{
	const Real k = d.k, R = d.R, h = d.heading();
	const Real rr = 0.8f * R;
	Real px, py;
	bandCircle( d.fin, d.x, d.y, rr, 12.0f * k, d.tint, d.app );
	Real ang;
	if( d.aim )
	{
		const Real f = d.t / 0.7f;
		ang = ( (Real)floor( f ) + easeOut( MIN( 1.0f, ( f - (Real)floor( f ) ) / 0.25f ) ) ) * 0.5f * MARKER_PI;
	}
	else
		ang = (Real)floor( d.tc / 0.7f + 1.0f ) * 0.5f * MARKER_PI;
	const Real rb = d.aim ? 1.18f * rr : 1.18f * rr - 0.1f * rr * d.snap;
	for( Int j = 0; j < 4; ++j )
	{
		const Real aa = h + ang + (Real)j * 0.5f * MARKER_PI;
		const Real half = 14.0f * k / MAX( rb, 1.0f );
		bandArc( d.fin, d.x, d.y, rb, 6.0f * k, aa - half, aa + half, d.tint, d.app );
	}
	const Real ps[ 3 ] = { 0.12f * R, -0.25f * R, -0.25f * R }, pq[ 3 ] = { 0.0f, 0.25f * R, -0.25f * R };
	for( Int i = 0; i < 3; ++i )
	{
		d.along( ps[ i ], pq[ i ], px, py );
		const Bool lit = d.aim && (Int)floor( d.t * 3.0f ) % 3 == i;
		part( P_SHAPE_DISC, px, py, h, ( i == 0 ? 20.0f : 14.0f ) * k, ( i == 0 ? 20.0f : 14.0f ) * k,
					scaled( d.tint, lit ? 1.3f : 0.9f ), d.app );
		if( i == 0 || d.aim )
			continue;
		const Real e = easeOut( ( d.tl - 0.8f ) / 1.2f );
		if( d.tl > 0.8f )
			for( Int s = 0; s < 24; ++s )
			{
				const Real a0 = 2.0f * MARKER_PI * (Real)s / 24.0f;
				arcStrip( P_HAIR, px, py, 0.85f * R * e, 3.0f * k, a0, a0 + 0.6f * 2.0f * MARKER_PI / 24.0f, TRUE, d.tint, 0.75f * d.app, FALSE );
			}
	}
}

/// one marker's frame, by its design
static void drawDesign( const Marker &m, Real R, Real t, Real tl, Real app, Real fade )
{
	DesignDraw d;
	d.m = &m;
	d.x = m.x;
	d.y = m.y;
	d.R = R;
	d.k = m.k;
	d.t = t;
	d.tl = tl;
	d.aim = tl < 0.0f;
	d.tc = d.aim ? t : t - tl;
	d.app = app;
	d.fade = fade;
	d.snap = d.aim ? 0.0f : easeOut( tl / 0.22f );
	d.yaw = s_ctx.yaw;
	d.fin = m.finish;
	d.tint = m.tint;
	if( m.lane )
	{
		d.hx = m.ux;
		d.hy = m.uy;
	}
	else if( m.hx != 0.0f || m.hy != 0.0f )
	{
		d.hx = m.hx;
		d.hy = m.hy;
	}
	else
	{
		d.hx = s_ctx.rx;
		d.hy = s_ctx.ry;
	}
	// a countdown: the flight followSuperweapon reads, or the cannon's frames; else three seconds
	if( m.missileSeen || m.super == SUPER_CANNON || m.impactMs != 0 )
		d.frac = m.frac;
	else
		d.frac = d.aim ? 1.0f : MAX( 0.0f, 1.0f - tl / 3.0f );
	d.impact = m.impactMs != 0 ? secondsSince( s_ctx.now, m.impactMs ) : -1.0f;

	switch( m.design )
	{
		case DESIGN_PARADROP:		drawParadrop( d );		break;
		case DESIGN_CRATE:			drawCrate( d );				break;
		case DESIGN_LEAFLET:		drawLeaflet( d );			break;
		case DESIGN_A10:				drawA10( d );					break;
		case DESIGN_FUELAIR:		drawFuelAir( d );			break;
		case DESIGN_SPYSAT:			drawSpySat( d );			break;
		case DESIGN_SPYDRONE:		drawSpyDrone( d );		break;
		case DESIGN_REPAIR:			drawRepair( d );			break;
		case DESIGN_SPECTRE:		drawSpectre( d );			break;
		case DESIGN_B3:					drawB3( d );					break;
		case DESIGN_PARTICLE:		drawParticle( d );		break;
		case DESIGN_LASER:			drawLaser( d );				break;
		case DESIGN_CRUISE:			drawCruise( d );			break;
		case DESIGN_AMBULANCE:	drawAmbulance( d );		break;
		case DESIGN_COMANCHE:		drawComanche( d );		break;
		case DESIGN_DEMINE:			drawDemine( d );			break;
		case DESIGN_ARTILLERY:	drawArtillery( d );		break;
		case DESIGN_MINES:			drawMines( d );				break;
		case DESIGN_EMP:				drawEmp( d );					break;
		case DESIGN_CCARPET:		drawChinaCarpet( d );	break;
		case DESIGN_NAPALM:			drawNapalm( d );			break;
		case DESIGN_FRENZY:			drawFrenzy( d );			break;
		case DESIGN_NEUTRON:		drawNeutron( d );			break;
		case DESIGN_HNAPALM:		drawHelixNapalm( d );	break;
		case DESIGN_HNUKE:			drawHelixNuke( d );		break;
		case DESIGN_TANKDROP:		drawTankDrop( d );		break;
		case DESIGN_SCUD:				drawScud( d );				break;
		case DESIGN_ANTHRAX:		drawAnthrax( d );			break;
		case DESIGN_AMBUSH:			drawAmbush( d );			break;
		case DESIGN_GPS:				drawGps( d );					break;
		case DESIGN_RADAR:			drawRadar( d );				break;
		case DESIGN_KARGU:			drawKargu( d );				break;
		case DESIGN_UAV:				drawUav( d );					break;
		case DESIGN_TAYFUN:			drawTayfun( d );			break;
		case DESIGN_SWARM:			drawSwarm( d );				break;
		case DESIGN_BLIND:			drawBlind( d );				break;
		case DESIGN_RAPID:			drawRapid( d );				break;
		case DESIGN_ALLYAID:		drawAllyAid( d );			break;
		case DESIGN_COMMANDO:		drawCommando( d );		break;
		default:								break;
	}
}

static void drawMarker( const Marker &m )
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

	const Bool along = m.lane || designDrawsRoute( m );		// the lane's grids, or a route's in them
	s_ctx.g1 = &m.grid;
	s_ctx.g2 = along ? &m.laneGrid : NULL;
	s_ctx.w1 = &m.water;
	s_ctx.w2 = along ? &m.laneWater : NULL;
	s_ctx.flatZ = m.z;

	if( app > 0.002f )
	{
		if( m.lane )
			drawLane( m, R, t, tl, app );
		if( m.orbit )
			drawOrbit( m, t, ( tl >= 0.0f ? 1.0f : 0.6f ) * app );
		if( m.design != DESIGN_NONE && !m.guarding )
			drawDesign( m, R, t, tl, app, fade );
		else
		{
			drawZone( m, R, t, tl, quiet, app );
			if( m.super != SUPER_NONE && tl >= 0.0f )
				drawCountdown( m, R, t, fade );
			drawGlyph( m, t, tl, app );
		}
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

	// the pings, additive, drawn after every alpha blended piece
	s_ctx.g1 = &m.grid;
	s_ctx.g2 = NULL;
	s_ctx.w1 = &m.water;
	s_ctx.w2 = NULL;
	if( !m.guarding )
		pingRing( m.x, m.y, R, m.k, tl, m.tint );
	if( m.pingMs != 0 )
		pingRing( m.x, m.y, m.pingR, m.k, secondsSince( now, m.pingMs ), m.pingTint );
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
	s_ctx.additive = FALSE;
	theMarkerQuads.begin( eye, pull, shroud );
	theAddQuads.begin( eye, pull, shroud );

	for( size_t i = 0; i < s_guards.size(); ++i )
		drawMarker( s_guards[ i ] );
	for( size_t i = 0; i < s_shots.size(); ++i )
		drawMarker( s_shots[ i ] );
	if( s_aimOn )
		drawMarker( s_aim );
	if( !waterPass )
		for( size_t i = 0; i < s_clicks.size(); ++i )
			drawClick( s_clicks[ i ] );
	s_ctx.additive = FALSE;

	if( !theMarkerQuads.empty() )
	{
		setMarkerState( FALSE, shroudTexture );
		theMarkerQuads.flush();
	}
	if( !theAddQuads.empty() )
	{
		setMarkerState( TRUE, shroudTexture );
		theAddQuads.flush();
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
