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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DRadar.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, January 2002
// Desc:   W3D radar implementation, this has the necessary device dependent drawing
//				 necessary for the radar
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/LogicThread.h"
#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"

#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/PartitionManager.h"

#include <algorithm>

#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Image.h"
#include "GameClient/Line2D.h"
#include "GameClient/MapUtil.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/RadarContactMarks.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/Water.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/texture.h"
#include "WW3D2/dx8caps.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// The terrain picture's size.  The window shows it at about 220 to 440 pixels, so 256 is near one
// texel a pixel; it is sampled 3x3 a texel as the 128 one was, four times the old samples a map.
enum { RADAR_TERRAIN_TEXTURE_SIZE = 256 };

// The shroud texture holds two texels a shroud cell each way, never more than this on a side, and
// uploadShroud fills them from the cells softened over their neighbours: the fog's edge is a ramp
// the CPU computes, the same on every device, not whatever the sampler makes of one texel a cell.
// 512 keeps a changed frame's upload to a megabyte; Direct3D 11 refreshes the whole texture.  The
// dark rises from a floor at a clear cell's edge to full this many cells into the fog; the floor,
// in eighths of the cell's own alpha, keeps a narrow strip of fog or shroud visibly dark.
enum { RADAR_SHROUD_MAX_SCALE = 2, RADAR_SHROUD_MAX_TEXTURE = 512, RADAR_SHROUD_RAMP_CELLS = 3,
			 RADAR_SHROUD_FLOOR_EIGHTHS = 3 };

// The lobby map preview has marked the money with a dollar coin and the tech buildings with a star
// since 2003 (SkirmishGameOptionsMenu.cpp, "Cash" and "TecBuilding"), so the minimap uses the same
// two images.  Sized against the radar and clamped; landmarks of one kind closer than this many
// radar cells are one place, a dock's eight piles one dollar.
enum { LANDMARK_ICON_RADAR_DIVISOR = 20, LANDMARK_ICON_MIN_SIZE = 9, LANDMARK_MERGE_CELLS = 6 };

// Whose the local player is shown an object is, by the rules Radar::addObject picks the radar
// colour with: a disguise passes for its disguise to anyone not allied with its owner, and a
// container answers for a stealth garrison.  Asked at the sync point, so it follows a disguise or
// a garrison that changed after the add, and a save load.
static const Player *apparentPlayerOf( const Object *obj )
{
	const Player *player = obj->getControllingPlayer();
	Player *local = ThePlayerList->getLocalPlayer();
	if( obj->isKindOf( KINDOF_DISGUISER ) && obj->getStealth() && obj->getStealth()->isDisguised() && player && local )
	{
		const Player *disguised = ThePlayerList->getNthPlayer( obj->getStealth()->getDisguisedPlayerIndex() );
		if( disguised && player->getRelationship( local->getDefaultTeam() ) != ALLIES && local->isPlayerActive() )
			player = disguised;
	}
	if( obj->getContain() )
	{
		const Player *shown = obj->getContain()->getApparentControllingPlayer( local );
		if( shown )
			player = shown;
	}
	return player;
}

// a unit's dot: 3 pixels on the 220 pixel radar of a 1080p HUD, the old 2 texels' worth
static Int blipDotSize( Int width, Int height )
{
	return max( 3, REAL_TO_INT_FLOOR( max( width, height ) / 72.0f + 0.5f ) );
}

static Int nextPowerOfTwo( Int n )
{
	Int p = 1;
	while( p < n )
		p <<= 1;
	return p;
}

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static WW3DFormat findFormat(const WW3DFormat formats[])
{
	for( Int i = 0; formats[ i ] != WW3D_FORMAT_UNKNOWN; i++ )
	{

		if( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( formats[ i ] ) )
		{

			return formats[ i ];

		}  // end if

	}  // end for i
	DEBUG_CRASH(("WW3DRadar: No appropriate texture format\n") );
	return WW3D_FORMAT_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
/** Find the texture format we're going to use for the radar.  The texture format must
	* be supported by the hardware.  The "more preferred" formats appear at the top of
	* the format tables in order from most preferred to least preferred */
//-------------------------------------------------------------------------------------------------
void W3DRadar::initializeTextureFormats( void )
{
	const WW3DFormat terrainFormats[] = 
	{
		WW3D_FORMAT_R8G8B8,
		WW3D_FORMAT_X8R8G8B8,
		WW3D_FORMAT_R5G6B5,
		WW3D_FORMAT_X1R5G5B5,
		WW3D_FORMAT_UNKNOWN				// keep this one last
	};
	const WW3DFormat shroudFormats[] =
	{
		WW3D_FORMAT_A8R8G8B8,
		WW3D_FORMAT_A4R4G4B4,
		WW3D_FORMAT_UNKNOWN				// keep this one last
	};

	// find a format for the terrain texture
	m_terrainTextureFormat = findFormat(terrainFormats);

	// find a format for the shroud texture
	m_shroudTextureFormat = findFormat(shroudFormats);

}  // end initializeTextureFormats

//-------------------------------------------------------------------------------------------------
/** Delete resources used specifically in this W3D radar implemetation */
//-------------------------------------------------------------------------------------------------
void W3DRadar::deleteResources( void )
{

	//
	// delete terrain resources used
	//
	if( m_terrainTexture )
		m_terrainTexture->Release_Ref();
	m_terrainTexture = NULL;
	if( m_terrainImage )
		m_terrainImage->deleteInstance();
	m_terrainImage = NULL;

	//
	// delete shroud resources used
	//
	if( m_shroudTexture )
		m_shroudTexture->Release_Ref();
	m_shroudTexture = NULL;
	if( m_shroudImage )
		m_shroudImage->deleteInstance();
	m_shroudImage = NULL;

}  // end deleteResources

//-------------------------------------------------------------------------------------------------
/** Reconstruct the view box given the current camera settings */
//-------------------------------------------------------------------------------------------------
void W3DRadar::reconstructViewBox( void )
{
	Coord3D world[ 4 ];
	Int i;

	// get the 4 points of the view corners in the 3D world at the average Z height in the map
	TheTacticalView->getScreenCornerWorldPointsAtZ( &world[ 0 ],
																									&world[ 1 ],
																									&world[ 2 ],
																									&world[ 3 ],
																									getTerrainAverageZ() );

	// world offsets, not radar cells: a cell is several pixels wide once the minimap zooms in
	for( i = 0; i < 4; i++ )
	{

		//
		// store these points in the view box array which contains a first position
		// of (0,0) and then offsets for each additional entry point
		//
		if( i == 0 )
		{

			m_viewBox[ i ].x = 0;
			m_viewBox[ i ].y = 0;

		}  // end if
		else
		{

			m_viewBox[ i ].x = world[ i ].x - world[ i - 1 ].x;
			m_viewBox[ i ].y = world[ i ].y - world[ i - 1 ].y;

		}  // end else

	}  // end for i

	m_reconstructViewBox = FALSE;

}  // end reconstructViewBox

//-------------------------------------------------------------------------------------------------
/** A world spot in screen pixels, under the zoom draw set up.  Off the picture when the minimap is
	* zoomed in and the spot lies outside what it shows; every caller clips or skips. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::worldToPixel( const Coord3D *world, ICoord2D *pixel ) const
{
	Real x, y;
	m_drawTransform.toPixel( world->x, world->y, &x, &y );
	pixel->x = REAL_TO_INT_FLOOR( x );
	pixel->y = REAL_TO_INT_FLOOR( y );
}

/// a pixel inside the radar's picture
static Bool insidePicture( const ICoord2D &pixel, Int pixelX, Int pixelY, Int width, Int height )
{
	return pixel.x >= pixelX && pixel.x < pixelX + width && pixel.y >= pixelY && pixel.y < pixelY + height;
}


//-------------------------------------------------------------------------------------------------
/** Draw a hero icon at a position, given radar box upper left location and dimensions.  */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawHeroIcon( Int pixelX, Int pixelY, Int width, Int height, const Coord3D *pos )
{
	// get the hero icon image
	static const Image *image = (Image *)TheMappedImageCollection->findImageByName("HeroReticle");
	if (image != NULL)
	{
		// convert world to screen coords; a hero outside what a zoomed minimap shows has no icon
		ICoord2D offsetScreen;
		worldToPixel( pos, &offsetScreen );
		if( !insidePicture( offsetScreen, pixelX, pixelY, width, height ) )
			return;

		// shift from an upper left to a center focus for the icon
		int iconWidth = image->getImageWidth();
		int iconHeight = image->getImageHeight();
		offsetScreen.x -= (iconWidth / 2) - 1;
		offsetScreen.y -= iconHeight / 2; 

		// draw the icon
		TheDisplay->drawImage( image, offsetScreen.x , offsetScreen.y, offsetScreen.x + iconWidth, offsetScreen.y + iconHeight );
	}
}

static void fillClipped( const IRegion2D &clip, Int x, Int y, Int size, UnsignedInt colour );

//-------------------------------------------------------------------------------------------------
/** A dashed white line from each of the local player's units walking to its hotkey squad to where
	* it walks, and a white dot where it walks to, from the copy update took at the sync point: draw
	* runs beside the tick.  Each dash and the dot sit on a dark keyline a pixel wider all round, so
	* they read on snow and on dark ground alike.  The dashes are laid from the unit's end, so as the
	* line shortens they stay put under the unit and the last one at the destination is the one that
	* changes. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawReinforcementLines( Int pixelX, Int pixelY, Int width, Int height )
{
	const Real dashOn = 6.0f;
	const Real dashOff = 4.0f;
	const Real lineWidth = 3.0f;
	const Real keylineWidth = lineWidth + 2.0f;
	const Int dot = 5;
	const Color lineColor = GameMakeColor( 255, 255, 255, 255 );
	const Color keylineColor = GameMakeColor( 12, 12, 12, 255 );

	IRegion2D clip;
	clip.lo.x = pixelX;
	clip.lo.y = pixelY;
	clip.hi.x = pixelX + width;
	clip.hi.y = pixelY + height;

	for( size_t i = 0; i < m_cachedReinforcementLines.size(); ++i )
	{
		const ReinforcementLine &line = m_cachedReinforcementLines[ i ];
		ICoord2D from, to;
		worldToPixel( &line.from, &from );
		worldToPixel( &line.to, &to );

		const Real dx = INT_TO_REAL( to.x - from.x );
		const Real dy = INT_TO_REAL( to.y - from.y );
		const Real length = sqrtf( dx * dx + dy * dy );
		if( length >= 1.0f )
		{
			const Real ux = dx / length;
			const Real uy = dy / length;

			for( Real along = 0.0f; along < length; along += dashOn + dashOff )
			{
				const Real end = min( along + dashOn, length );
				ICoord2D dashFrom, dashTo, clippedFrom, clippedTo;

				// the keyline runs a pixel past each end of the dash, as well as to either side of it
				dashFrom.x = from.x + REAL_TO_INT( ux * (along - 1.0f) );
				dashFrom.y = from.y + REAL_TO_INT( uy * (along - 1.0f) );
				dashTo.x = from.x + REAL_TO_INT( ux * (end + 1.0f) );
				dashTo.y = from.y + REAL_TO_INT( uy * (end + 1.0f) );
				if( ClipLine2D( &dashFrom, &dashTo, &clippedFrom, &clippedTo, &clip ) )
					TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, keylineWidth, keylineColor );

				dashFrom.x = from.x + REAL_TO_INT( ux * along );
				dashFrom.y = from.y + REAL_TO_INT( uy * along );
				dashTo.x = from.x + REAL_TO_INT( ux * end );
				dashTo.y = from.y + REAL_TO_INT( uy * end );
				if( ClipLine2D( &dashFrom, &dashTo, &clippedFrom, &clippedTo, &clip ) )
					TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, lineWidth, lineColor );
			}
		}

		// the destination, on top of the last dash
		fillClipped( clip, to.x - (dot + 2) / 2, to.y - (dot + 2) / 2, dot + 2, keylineColor );
		fillClipped( clip, to.x - dot / 2, to.y - dot / 2, dot, lineColor );
	}

	// The square sits where the squad gathers, so the units that already arrived were under it.
	// Their dots go over it again; only a dot that touches a destination square is drawn twice.
	if( m_cachedReinforcementLines.empty() || width <= 0 || height <= 0 )
		return;
	const Int blipDot = blipDotSize( width, height );
	const Int reach = (dot + 2) / 2 + blipDot / 2 + 1;
	m_destinationPixels.resize( m_cachedReinforcementLines.size() );
	for( size_t i = 0; i < m_cachedReinforcementLines.size(); ++i )
		worldToPixel( &m_cachedReinforcementLines[ i ].to, &m_destinationPixels[ i ] );
	for( size_t b = 0; b < m_blips.size(); ++b )
	{
		const Blip &blip = m_blips[ b ];
		if( blip.halfLength > 0.0f || blip.x < 0.0f || blip.y < 0.0f || blip.x >= m_mapExtent.width() || blip.y >= m_mapExtent.height() )
			continue;
		Real px, py;
		m_drawTransform.toPixel( blip.x, blip.y, &px, &py );
		const Int bx = REAL_TO_INT_FLOOR( px );
		const Int by = REAL_TO_INT_FLOOR( py );
		for( size_t i = 0; i < m_destinationPixels.size(); ++i )
		{
			const ICoord2D &to = m_destinationPixels[ i ];
			if( abs( bx - to.x ) <= reach && abs( by - to.y ) <= reach )
			{
				fillClipped( clip, bx - blipDot / 2, by - blipDot / 2, blipDot, blip.color );
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The selection's plans as thin lines: a production building's rally point, and every leg of a
	* unit's queued orders, from the copy update took.  White like the selection's dots, on a dark
	* keyline, with a small square where each leg ends. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawOrderLines( Int pixelX, Int pixelY, Int width, Int height )
{
	if( m_cachedOrderLines.empty() )
		return;

	const Color lineColor = GameMakeColor( 255, 255, 255, 220 );
	const Color keylineColor = GameMakeColor( 12, 12, 12, 160 );
	const Int dot = 3;

	IRegion2D clip;
	clip.lo.x = pixelX;
	clip.lo.y = pixelY;
	clip.hi.x = pixelX + width;
	clip.hi.y = pixelY + height;

	// keylines first, so a leg's dark edge never cuts the white of the one before it
	for( Int pass = 0; pass < 2; ++pass )
	{
		for( size_t i = 0; i < m_cachedOrderLines.size(); ++i )
		{
			const ReinforcementLine &line = m_cachedOrderLines[ i ];
			ICoord2D from, to, clippedFrom, clippedTo;
			worldToPixel( &line.from, &from );
			worldToPixel( &line.to, &to );

			if( pass == 0 )
			{
				if( ClipLine2D( &from, &to, &clippedFrom, &clippedTo, &clip ) )
					TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, 3.0f, keylineColor );
				fillClipped( clip, to.x - (dot + 2) / 2, to.y - (dot + 2) / 2, dot + 2, keylineColor );
			}
			else
			{
				if( ClipLine2D( &from, &to, &clippedFrom, &clippedTo, &clip ) )
					TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, 1.0f, lineColor );
				fillClipped( clip, to.x - dot / 2, to.y - dot / 2, dot, lineColor );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The money and the buildings a rifleman takes, with the lobby preview's dollar and star, as the
	* drawn player last saw them.  update sorts the dollars before the stars, so the batch changes
	* image once. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawLandmarks( Int pixelX, Int pixelY, Int width, Int height )
{
	if( m_cachedLandmarks.empty() )
		return;

	static const Image *supplyImage = TheMappedImageCollection->findImageByName( "Cash" );
	static const Image *captureImage = TheMappedImageCollection->findImageByName( "TecBuilding" );

	const Int iconSize = min( (Int)SUPPLY_TECH_SIZE, max( (Int)LANDMARK_ICON_MIN_SIZE, width / LANDMARK_ICON_RADAR_DIVISOR ) );
	for( size_t i = 0; i < m_cachedLandmarks.size(); ++i )
	{
		const Image *image = m_cachedLandmarks[ i ].supply ? supplyImage : captureImage;
		if( image == NULL )
			continue;

		ICoord2D pixel;
		worldToPixel( &m_cachedLandmarks[ i ].pos, &pixel );
		if( !insidePicture( pixel, pixelX, pixelY, width, height ) )
			continue;		// outside what a zoomed minimap shows

		// the whole icon inside the picture, so one at the map's edge stays off the frame
		const Int left = max( pixelX, min( pixelX + width - iconSize, pixel.x - iconSize / 2 ) );
		const Int top = max( pixelY, min( pixelY + height - iconSize, pixel.y - iconSize / 2 ) );
		TheDisplay->drawImage( image, left, top, left + iconSize, top + iconSize );
	}
}

//-------------------------------------------------------------------------------------------------
/** A filled square cut to the radar's picture: drawFillRect clips to nothing, and a contact at the
	* map's edge would otherwise spill onto the frame. */
//-------------------------------------------------------------------------------------------------
static void fillClipped( const IRegion2D &clip, Int x, Int y, Int size, UnsignedInt colour )
{
	Int x0 = max( x, clip.lo.x );
	Int y0 = max( y, clip.lo.y );
	Int x1 = min( x + size, clip.hi.x );
	Int y1 = min( y + size, clip.hi.y );
	if( x1 > x0 && y1 > y0 )
		TheDisplay->drawFillRect( x0, y0, x1 - x0, y1 - y0, colour );
}

//-------------------------------------------------------------------------------------------------
/** A convex quad cut to the radar's picture, filled a pixel row at a time: a row covers the
	* pixels whose centres lie inside the quad at that row's centre, so the edges come out crisp and
	* nothing reaches past the clip.  Each row is a drawFillRect; draw() opens a 2D batch so they
	* share one draw call. */
//-------------------------------------------------------------------------------------------------
static void fillQuadClipped( const IRegion2D &clip, const Real *cx, const Real *cy, UnsignedInt colour )
{
	Real top = cy[ 0 ], bottom = cy[ 0 ];
	for( Int i = 1; i < 4; ++i )
	{
		top = min( top, cy[ i ] );
		bottom = max( bottom, cy[ i ] );
	}

	const Int rowFrom = max( (Int)clip.lo.y, REAL_TO_INT_CEIL( top - 0.5f ) );
	const Int rowTo = min( (Int)clip.hi.y - 1, REAL_TO_INT_FLOOR( bottom - 0.5f ) );
	for( Int row = rowFrom; row <= rowTo; ++row )
	{
		const Real yc = row + 0.5f;
		Real left = 1e30f, right = -1e30f;
		for( Int i = 0; i < 4; ++i )
		{
			const Int j = (i + 1) & 3;
			const Real ya = cy[ i ], yb = cy[ j ];
			if( (yc >= ya && yc < yb) || (yc >= yb && yc < ya) )
			{
				const Real x = cx[ i ] + (yc - ya) * (cx[ j ] - cx[ i ]) / (yb - ya);
				left = min( left, x );
				right = max( right, x );
			}
		}
		if( left > right )
			continue;

		const Int x0 = max( (Int)clip.lo.x, REAL_TO_INT_CEIL( left - 0.5f ) );
		const Int x1 = min( (Int)clip.hi.x - 1, REAL_TO_INT_FLOOR( right - 0.5f ) );
		if( x1 >= x0 )
			TheDisplay->drawFillRect( x0, row, x1 - x0 + 1, 1, colour );
	}
}

//-------------------------------------------------------------------------------------------------
/** The radar's contacts on the minimap: this sweep's as a red square on a dark keyline with a dim
	* ring round it, the sweep before as a small red square fading out as the next sweep comes due.
	* A SIHA recon area is a dashed green ring, and a contact it saw an amber square, as recon_v1 has
	* them: amber says the type is known. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawRadarContacts( Int pixelX, Int pixelY, Int width, Int height )
{
	const std::vector<RadarContact> &fresh = RadarContactMarks_fresh();
	const std::vector<RadarContact> &echoes = RadarContactMarks_echoes();
	const std::vector<RadarReconArea> &areas = RadarContactMarks_areas();
	if( fresh.empty() && echoes.empty() && areas.empty() )
		return;

	IRegion2D clip;
	clip.lo.x = pixelX;
	clip.lo.y = pixelY;
	clip.hi.x = pixelX + width;
	clip.hi.y = pixelY + height;

	const Int dashes = 48;
	const Color areaColour = (Color)( (RadarContactLook::AreaGreen & 0x00FFFFFF) | 0xE6000000 );
	for( size_t i = 0; i < areas.size(); ++i )
	{
		ICoord2D centre;
		worldToPixel( &areas[ i ].pos, &centre );
		const Real rx = areas[ i ].radius * m_drawTransform.scaleX;
		const Real ry = areas[ i ].radius * m_drawTransform.scaleY;
		for( Int s = 0; s < dashes; s += 2 )
		{
			Real a0 = TWO_PI * s / dashes;
			Real a1 = TWO_PI * (s + 1) / dashes;
			ICoord2D from, to, clippedFrom, clippedTo;
			from.x = centre.x + REAL_TO_INT( Cos( a0 ) * rx );
			from.y = centre.y + REAL_TO_INT( Sin( a0 ) * ry );
			to.x = centre.x + REAL_TO_INT( Cos( a1 ) * rx );
			to.y = centre.y + REAL_TO_INT( Sin( a1 ) * ry );
			if( ClipLine2D( &from, &to, &clippedFrom, &clippedTo, &clip ) )
				TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, 1.0f, areaColour );
		}
	}

	// the ring is opaque, a darker shade of the contact's red: twelve translucent segments blended
	// twice where they meet and the ring came out beaded
	const UnsignedInt red = RadarContactLook::Colour & 0x00FFFFFF;
	const Color ringColour = GameMakeColor( (UnsignedByte)(((red >> 16) & 0xFF) * 3 / 5), (UnsignedByte)(((red >> 8) & 0xFF) * 3 / 5),
																					(UnsignedByte)((red & 0xFF) * 3 / 5), 255 );
	const Color keyline = GameMakeColor( 8, 10, 8, 200 );
	const Int pip = RadarContactLook::MinimapPip;
	const Real ringRadius = RadarContactLook::MinimapRing * 0.5f;
	const Int ringSegments = 12;
	const Int dot = RadarContactLook::MinimapReconDot;

	for( size_t i = 0; i < fresh.size(); ++i )
	{
		ICoord2D pixel;
		worldToPixel( &fresh[ i ].pos, &pixel );

		if( fresh[ i ].recon )
		{
			fillClipped( clip, pixel.x - (dot + 2) / 2, pixel.y - (dot + 2) / 2, dot + 2, keyline );
			fillClipped( clip, pixel.x - dot / 2, pixel.y - dot / 2, dot, RadarContactLook::Amber );
			continue;
		}

		for( Int s = 0; s < ringSegments; ++s )
		{
			Real a0 = TWO_PI * s / ringSegments;
			Real a1 = TWO_PI * (s + 1) / ringSegments;
			ICoord2D from, to, clippedFrom, clippedTo;
			from.x = pixel.x + REAL_TO_INT( Cos( a0 ) * ringRadius );
			from.y = pixel.y + REAL_TO_INT( Sin( a0 ) * ringRadius );
			to.x = pixel.x + REAL_TO_INT( Cos( a1 ) * ringRadius );
			to.y = pixel.y + REAL_TO_INT( Sin( a1 ) * ringRadius );
			if( ClipLine2D( &from, &to, &clippedFrom, &clippedTo, &clip ) )
				TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y, 1.0f, ringColour );
		}
		fillClipped( clip, pixel.x - (pip + 2) / 2, pixel.y - (pip + 2) / 2, pip + 2, keyline );
		fillClipped( clip, pixel.x - pip / 2, pixel.y - pip / 2, pip, RadarContactLook::Colour );
	}

	const UnsignedInt echoAlpha = REAL_TO_UNSIGNEDBYTE( 255.0f * (1.0f - RadarContactMarks_sweepAge()) );
	const Int echo = RadarContactLook::MinimapEcho;
	for( size_t i = 0; i < echoes.size(); ++i )
	{
		if( echoes[ i ].recon )
			continue;		// the area sees it again next sweep, as on the ground
		ICoord2D pixel;
		worldToPixel( &echoes[ i ].pos, &pixel );
		fillClipped( clip, pixel.x - echo / 2, pixel.y - echo / 2, echo, red | (echoAlpha << 24) );
	}
}

//-------------------------------------------------------------------------------------------------
/** Draw a "box" into the texture passed in that represents the viewable area for
	* the tactical display into the game world */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawViewBox( Int pixelX, Int pixelY, Int width, Int height )
{
	ICoord2D ulScreen;
	Coord3D ulWorld;

	// convert top left of screen into world position
	TheTacticalView->getOrigin( &ulScreen.x, &ulScreen.y );
	TheTacticalView->screenToWorldAtZ( &ulScreen, &ulWorld, getTerrainAverageZ() );

	//
	// using our view box offset array, walk from that corner to each of the others in world
	// units and turn each into screen pixels: upper left, upper right, lower right, lower left
	//
	ICoord2D corners[ 4 ];
	Coord3D world = ulWorld;
	worldToPixel( &world, &corners[ 0 ] );
	for( Int corner = 1; corner < 4; corner++ )
	{
		world.x += m_viewBox[ corner ].x;
		world.y += m_viewBox[ corner ].y;
		worldToPixel( &world, &corners[ corner ] );
	}

	// zoomed in, the box stops at the picture's edge: past it is map the picture does not show
	IRegion2D picture;
	picture.lo.x = pixelX;
	picture.lo.y = pixelY;
	picture.hi.x = pixelX + width;
	picture.hi.y = pixelY + height;
	drawBoxLines( corners, m_zoomView.zoom > 1.0f ? &picture : NULL );

}  // end drawViewBox

//-------------------------------------------------------------------------------------------------
/** The view box's four lines through corners in pixels, upper left, upper right, lower right and
	* lower left, clipped to the whole window available for the radar and not just its image, or to
	* the picture when one is given */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawBoxLines( const ICoord2D *corners, const IRegion2D *picture )
{
	const Real lineWidth = 1.0f;
	const Color topColor = GameMakeColor( 225, 225, 0, 255 );
	const Color bottomColor = GameMakeColor( 158, 158, 0, 255 );
	// each line's colour at its start and its end: the top light, the bottom dark, the sides between
	const Color fromColor[ 4 ] = { topColor, topColor, bottomColor, bottomColor };
	const Color toColor[ 4 ] = { topColor, bottomColor, bottomColor, topColor };

	IRegion2D clipRegion;
	ICoord2D radarWindowSize, radarWindowScreenPos;
	m_radarWindow->winGetSize( &radarWindowSize.x, &radarWindowSize.y );
	m_radarWindow->winGetScreenPosition( &radarWindowScreenPos.x, &radarWindowScreenPos.y );
	clipRegion.lo.x = radarWindowScreenPos.x;
	clipRegion.lo.y = radarWindowScreenPos.y;
	clipRegion.hi.x = radarWindowScreenPos.x + radarWindowSize.x;
	clipRegion.hi.y = radarWindowScreenPos.y + radarWindowSize.y;
	if( picture )
		clipRegion = *picture;

	for( Int line = 0; line < 4; line++ )
	{
		ICoord2D start = corners[ line ];
		ICoord2D end = corners[ ( line + 1 ) % 4 ];
		ICoord2D clipStart, clipEnd;
		if( ClipLine2D( &start, &end, &clipStart, &clipEnd, &clipRegion ) )
			TheDisplay->drawLine( clipStart.x, clipStart.y, clipEnd.x, clipEnd.y, lineWidth, fromColor[ line ], toColor[ line ] );
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::drawSingleBeaconEvent( Int pixelX, Int pixelY, Int width, Int height, Int index )
{
	RadarEvent *event = &(m_event[index]);
	ICoord2D tri[ 3 ];
	ICoord2D start, end;
	Real angle, addAngle;
	Color startColor, endColor;
	Real lineWidth = 1.0f;
	UnsignedInt currentFrame = TheGameLogic->getFrame();
	UnsignedInt frameDiff;							// frames the event has been alive for
	Real maxEventSize = width / 10.0f;   // max size of the event marker
	Int minEventSize = 6;     // min size of the event marker
	Int eventSize;									 // current size of a marker to draw
	const Real TIME_FROM_FULL_SIZE_TO_SMALL_SIZE = LOGICFRAMES_PER_SECOND * 1.5;
	Real totalAnglesToSpin = 2.0f * PI;  ///< spin around this many angles going from big to small
	UnsignedByte r, g, b, a;

	// setup screen clipping region
	IRegion2D clipRegion;
	clipRegion.lo.x = pixelX;
	clipRegion.lo.y = pixelY;
	clipRegion.hi.x = pixelX + width;
	clipRegion.hi.y = pixelY + height;

	// get the difference in frame from the current frame to the frame we were created on
	frameDiff = currentFrame - event->createFrame;

	// compute the size of the event marker, it is largest when it starts and smallest at the end
	eventSize = REAL_TO_INT( maxEventSize * ( 1.0f - frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE) );;

	// we never let the event size get too small
	if( eventSize < minEventSize )
		eventSize = minEventSize;

	// compute how much "angle" we will add to each point to make it rotate as it's getting small
	addAngle = -totalAnglesToSpin * (frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE);

	// create a triangle around the event, its size in radar cells of the unzoomed picture and its
	// centre where the zoomed one shows the spot
	ICoord2D centre;
	worldToPixel( &event->worldLoc, &centre );
	const Real cellX = width / (Real)RADAR_CELL_WIDTH;
	const Real cellY = height / (Real)RADAR_CELL_HEIGHT;
	for( Int k = 0; k < 3; ++k )
	{
		angle = 2.0f * PI / 3.0f * k - addAngle;
		tri[ k ].x = centre.x + REAL_TO_INT( DOUBLE_TO_REAL( Cos( angle ) ) * eventSize * cellX );
		tri[ k ].y = centre.y - REAL_TO_INT( DOUBLE_TO_REAL( Sin( angle ) ) * eventSize * cellY );
	}

	//
	// make the colors we're going to use, when we're at our smallest size we will start to
	// fade the alpha away to transparent so that at our lifetime frame we are completely gone
	//

	// color 1 ------------------
	r = event->color1.red;
	g = event->color1.green;
	b = event->color1.blue;
	a = event->color1.alpha;
	if( currentFrame > event->fadeFrame )
	{
		
		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) / 
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}  // end if
	startColor = GameMakeColor( r, g, b, a );

	// color 2 ------------------
	r = event->color2.red;
	g = event->color2.green;
	b = event->color2.blue;
	a = event->color2.alpha;
	if( currentFrame > event->fadeFrame )
	{
		
		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) / 
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}  // end if
	endColor = GameMakeColor( r, g, b, a );

	// draw the lines
	if( ClipLine2D( &tri[ 0 ], &tri[ 1 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 1 ], &tri[ 2 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 2 ], &tri[ 0 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/** An alarm's mark, in window pixels around its place: told apart by shape before colour, and as
	* big as what it is about, never the half-radar triangle EA spun down over a second and a half.
	*   under attack     a red ring with a second ring pulsing out of it once a second, for as long
	*                    as the hits keep coming (Radar::tryEvent keeps it alive)
	*   structure lost   a cross where the building stood
	*   built, upgraded  a tick
	*   stealth          a diamond; one of yours revealed has a dot in it
	*   ally signal      a square with a dot, whatever type it borrowed
	*   anything else    EA's triangle, small and still
	* Every stroke lies on a dark keyline a pixel wider all round, as the reinforcement lines do, so
	* it reads on snow and on dark ground alike.  A new mark pops in at twice its size over a fifth
	* of a second; it fades over the event's last half second as EA's did. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawSingleGenericEvent( Int pixelX, Int pixelY, Int width, Int height, Int index )
{
	const RadarEvent *event = &m_event[ index ];

	// half sizes at the 220 pixel radar of a 1080p HUD, by RadarEventMark: unit, harvester, structure, signal
	static const Real HALF_SIZE[ RADAR_MARK_COUNT ] = { 4.0f, 5.0f, 7.0f, 6.0f };
	const Real POP_FRAMES = LOGICFRAMES_PER_SECOND * 0.2f;
	const Real PULSE_FRAMES = LOGICFRAMES_PER_SECOND * 1.0f;
	const Real strokeWidth = 2.0f;

	IRegion2D clip;
	clip.lo.x = pixelX;
	clip.lo.y = pixelY;
	clip.hi.x = pixelX + width;
	clip.hi.y = pixelY + height;

	const UnsignedInt currentFrame = TheGameLogic->getFrame();
	const Real age = currentFrame > event->createFrame ? (Real)( currentFrame - event->createFrame ) : 0.0f;

	Real fade = 1.0f;
	if( currentFrame > event->fadeFrame && event->dieFrame > event->fadeFrame )
		fade = 1.0f - (Real)( currentFrame - event->fadeFrame ) / (Real)( event->dieFrame - event->fadeFrame );
	if( fade <= 0.0f )
		return;

	const Real scale = max( 1.0f, max( width, height ) / 220.0f );
	const Int mark = event->mark < RADAR_MARK_COUNT ? event->mark : RADAR_MARK_UNIT;
	Real s = HALF_SIZE[ mark ] * scale;
	if( age < POP_FRAMES )
		s *= 2.0f - age / POP_FRAMES;

	ICoord2D centre;
	worldToPixel( &event->worldLoc, &centre );
	const Real cx = (Real)centre.x;
	const Real cy = (Real)centre.y;

	const Color stroke = GameMakeColor( (UnsignedByte)event->color1.red, (UnsignedByte)event->color1.green,
																			(UnsignedByte)event->color1.blue, REAL_TO_UNSIGNEDBYTE( event->color1.alpha * fade ) );
	const Color keyline = GameMakeColor( 8, 10, 8, REAL_TO_UNSIGNEDBYTE( 200.0f * fade ) );

	// A polyline through points given as offsets from the centre, keyline first, then the stroke.
	// ClipLine2D clips the centre line only, so each pass clips to the picture shrunk by half its
	// width and no stroke spills onto the frame.  The keyline runs a pixel past each end, as the
	// reinforcement lines' dashes do, so an open stroke's ends are edged too.
	auto polyline = [&]( const Real *xy, Int points, Bool closed, Color colour, Real lineWidth, Bool keylined )
	{
		const Int segments = closed ? points : points - 1;
		for( Int pass = keylined ? 0 : 1; pass < 2; ++pass )
		{
			const Real passWidth = pass == 0 ? lineWidth + 2.0f : lineWidth;
			const Int inset = REAL_TO_INT_CEIL( passWidth * 0.5f );
			IRegion2D passClip = clip;
			passClip.lo.x += inset;
			passClip.lo.y += inset;
			passClip.hi.x -= inset;
			passClip.hi.y -= inset;
			const Real reach = pass == 0 ? 1.0f : 0.0f;
			for( Int k = 0; k < segments; ++k )
			{
				const Int j = ( k + 1 ) % points;
				Real x0 = cx + xy[ 2 * k ], y0 = cy + xy[ 2 * k + 1 ];
				Real x1 = cx + xy[ 2 * j ], y1 = cy + xy[ 2 * j + 1 ];
				const Real dx = x1 - x0, dy = y1 - y0;
				const Real length = sqrtf( dx * dx + dy * dy );
				if( length > 0.001f )
				{
					x0 -= dx / length * reach;
					y0 -= dy / length * reach;
					x1 += dx / length * reach;
					y1 += dy / length * reach;
				}
				ICoord2D from, to, clippedFrom, clippedTo;
				from.x = REAL_TO_INT_FLOOR( x0 + 0.5f );
				from.y = REAL_TO_INT_FLOOR( y0 + 0.5f );
				to.x = REAL_TO_INT_FLOOR( x1 + 0.5f );
				to.y = REAL_TO_INT_FLOOR( y1 + 0.5f );
				if( ClipLine2D( &from, &to, &clippedFrom, &clippedTo, &passClip ) )
					TheDisplay->drawLine( clippedFrom.x, clippedFrom.y, clippedTo.x, clippedTo.y,
																passWidth, pass == 0 ? keyline : colour );
			}
		}
	};
	auto ring = [&]( Real radius, Color colour, Real lineWidth, Bool keylined )
	{
		enum { SEGMENTS = 16 };
		Real xy[ 2 * SEGMENTS ];
		for( Int k = 0; k < SEGMENTS; ++k )
		{
			xy[ 2 * k ] = Cos( TWO_PI * k / SEGMENTS ) * radius;
			xy[ 2 * k + 1 ] = Sin( TWO_PI * k / SEGMENTS ) * radius;
		}
		polyline( xy, SEGMENTS, TRUE, colour, lineWidth, keylined );
	};
	auto dot = [&]( Int size, Color colour )
	{
		fillClipped( clip, centre.x - ( size + 2 ) / 2, centre.y - ( size + 2 ) / 2, size + 2, keyline );
		fillClipped( clip, centre.x - size / 2, centre.y - size / 2, size, colour );
	};

	if( mark == RADAR_MARK_SIGNAL )
	{
		const Real square[] = { -s, -s, s, -s, s, s, -s, s };
		polyline( square, 4, TRUE, stroke, strokeWidth, TRUE );
		dot( max( 2, REAL_TO_INT( 2.0f * scale ) ), stroke );
		return;
	}

	switch( event->type )
	{
		case RADAR_EVENT_UNDER_ATTACK:
		{
			ring( s, stroke, strokeWidth, TRUE );
			// the pulse: out to two and a half times the ring and gone, once a second
			const Real phase = fmodf( age, PULSE_FRAMES ) / PULSE_FRAMES;
			const Color pulse = GameMakeColor( (UnsignedByte)event->color1.red, (UnsignedByte)event->color1.green,
																				 (UnsignedByte)event->color1.blue,
																				 REAL_TO_UNSIGNEDBYTE( event->color1.alpha * fade * ( 1.0f - phase ) ) );
			ring( s * ( 1.0f + 1.5f * phase ), pulse, 1.0f, FALSE );
			break;
		}

		case RADAR_EVENT_STRUCTURE_LOST:
		{
			const Real a[] = { -s, -s, s, s };
			const Real b[] = { -s, s, s, -s };
			polyline( a, 2, FALSE, stroke, strokeWidth, TRUE );
			polyline( b, 2, FALSE, stroke, strokeWidth, TRUE );
			break;
		}

		case RADAR_EVENT_CONSTRUCTION:
		case RADAR_EVENT_UPGRADE:
		{
			// screen y runs down: from the left, down to the foot and up to the top right
			const Real tick[] = { -s, 0.0f, -0.3f * s, 0.7f * s, s, -0.8f * s };
			polyline( tick, 3, FALSE, stroke, strokeWidth, TRUE );
			break;
		}

		case RADAR_EVENT_STEALTH_DISCOVERED:
		case RADAR_EVENT_STEALTH_NEUTRALIZED:
		{
			const Real diamond[] = { 0.0f, -s, s, 0.0f, 0.0f, s, -s, 0.0f };
			polyline( diamond, 4, TRUE, stroke, strokeWidth, TRUE );
			if( event->type == RADAR_EVENT_STEALTH_NEUTRALIZED )
				dot( max( 2, REAL_TO_INT( 2.0f * scale ) ), stroke );
			break;
		}

		default:
		{
			// infiltration, information, battle plan and whatever a script asks for: EA's triangle
			const Real triangle[] = { 0.0f, -s, 0.87f * s, 0.5f * s, -0.87f * s, 0.5f * s };
			polyline( triangle, 3, TRUE, stroke, strokeWidth, TRUE );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Draw all the radar events */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawEvents( Int pixelX, Int pixelY, Int width, Int height )
{
	Int i;

	// update plays the sound of an event drawn for the first time: the sound asks the player list who
	// may hear it, and draw runs inside the render
	m_eventsDrawn = TRUE;

	for( i = 0;  i < MAX_RADAR_EVENTS; i++ )
	{

		// only 'active' events actually have something to draw
		if( m_event[ i ].active == TRUE && m_event[ i ].type != RADAR_EVENT_FAKE )
		{

			if ( m_event[ i ].type == RADAR_EVENT_BEACON_PULSE )
				drawSingleBeaconEvent( pixelX, pixelY, width, height, i );
			else
				drawSingleGenericEvent( pixelX, pixelY, width, height, i );

		}  // end if

	}  // end for i

}  // end drawEvents


//-------------------------------------------------------------------------------------------------
/** Draw all the radar icons */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawIcons( Int pixelX, Int pixelY, Int width, Int height )
{
	// draw the hero icons
	std::list<Coord3D>::const_iterator iter = m_cachedHeroPosList.begin();
	while (iter != m_cachedHeroPosList.end())
	{
		drawHeroIcon( pixelX, pixelY, width, height, &(*iter) );
		++iter;
	}
}

//-------------------------------------------------------------------------------------------------
/** Copy what an object list shows on the radar into m_blips, in list order, which is priority order:
	* a later blip draws over an earlier one as a later pixel used to overwrite an earlier one.  Every
	* rule of what shows and in what colour is EA's, unchanged; only the output moved from four texels
	* in a 128x128 texture to a record draw turns into pixels. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::collectBlips( const RadarObject *listHead, Bool calcHero )
{

	// sanity
	if( listHead == NULL )
		return;

	const Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
	Player *viewer = ThePlayerList->getNthPlayer( playerIndex );

	for( const RadarObject *rObj = listHead; rObj; rObj = rObj->friend_getNext() )
	{

		if (rObj->isTemporarilyHidden())
			continue;

		// get object
		const Object *obj = rObj->friend_getObject();

		// cache hero object positions for drawing in icon layer
		if( calcHero && obj->isHero() )
		{
			m_cachedHeroPosList.push_back(*obj->getPosition());
		}

		// the local list is the local player's own units, so no shroud to ask: a unit walking to its
		// hotkey squad gets its line
		if( calcHero && obj->getReinforcementSquad() >= 0 )
		{
			const Coord3D *dest = obj->getReinforcementDestination();
			if( dest )
			{
				ReinforcementLine line;
				line.from = *obj->getPosition();
				line.to = *dest;
				m_cachedReinforcementLines.push_back( line );
			}
		}
    Bool skip = FALSE;

		// check for shrouded status
		if (obj->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR)
			skip = TRUE;	//object is fogged or shrouded, don't render it.

 		//
 		// objects with a local only unit priority will only appear on the radar if they
 		// are controlled by the local player, or if the local player is an observer (cause
		// they are godlike and can see everything)
 		//


 		if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY &&
 				obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive() )
 			skip = TRUE;

    if ( skip )
      continue;

    // get the color we're going to draw in
		Color c = clientColor( rObj->getColor() );

		// What the local player has selected is white.  Otherwise the plain colours, when the key
		// asked for them: whoever the screen is drawn for green, his allies blue, his enemies red,
		// everyone else in their own colour.  The stealth blink below keeps working on either.
		const Drawable *drawable = obj->getDrawable();
		if( drawable && drawable->isSelected() )
		{
			c = GameMakeColor( 255, 255, 255, 255 );
		}
		else if( isSimpleColors() && viewer )
		{
			// the side the local player is shown, asked now as Radar::addObject asks it for the
			// colour: a disguised Bomb Truck or a stealth-garrisoned building keeps the side it
			// pretends to be on, and a disguise or garrison that changed since the add is caught
			const Player *apparent = apparentPlayerOf( obj );
			const Team *team = apparent == obj->getControllingPlayer() ? obj->getTeam() : (apparent ? apparent->getDefaultTeam() : NULL);
			const Relationship relation = team ? viewer->getRelationship( team ) : NEUTRAL;
			if( apparent == viewer )
				c = GameMakeColor( 40, 210, 40, 255 );
			else if( relation == ALLIES )
				c = GameMakeColor( 50, 120, 255, 255 );
			else if( relation == ENEMIES )
				c = GameMakeColor( 230, 30, 30, 255 );
		}

		
		
		// adjust the alpha for stealth units so they "fade/blink" on the radar for the controller
		// if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY )
		// ML-- What the heck is this? local-only and neutral-observier-viewed units are stealthy?? Since when?	
		// Now it twinkles for any stealthed object, whether locally controlled or neutral-observier-viewed
		if( obj->testStatus( OBJECT_STATUS_STEALTHED ) )
		{
      // an enemy of whoever the screen is drawn for: the local player, or the followed one with fog on
      if ( ThePlayerList->getNthPlayer( playerIndex )->getRelationship(obj->getTeam()) == ENEMIES )
        if( !obj->testStatus( OBJECT_STATUS_DETECTED ) && !obj->testStatus( OBJECT_STATUS_DISGUISED ) )
				  skip = TRUE;

			UnsignedByte r, g, b, a;
			GameGetColorComponents( c, &r, &g, &b, &a );

			const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;
			const UnsignedByte minAlpha = 32;
			
      if (skip)
        continue;

			Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition / 2.0f);
			if( alphaScale > 0.0f )
				a = REAL_TO_UNSIGNEDBYTE( ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha );
			else
				a = REAL_TO_UNSIGNEDBYTE( (alphaScale * (255.0f - minAlpha)) + minAlpha );
			c = GameMakeColor( r, g, b, a );

		}  // end if

		Blip blip;
		const Coord3D *pos = obj->getPosition();
		blip.x = pos->x;
		blip.y = pos->y;
		blip.halfLength = 0.0f;
		blip.halfWidth = 0.0f;
		blip.angle = 0.0f;
		blip.color = c;

		// a structure shows its footprint: a box's own sides, anything round its radius both ways
		if( obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			const GeometryInfo &geom = obj->getGeometryInfo();
			blip.halfLength = geom.getMajorRadius();
			blip.halfWidth = geom.getGeomType() == GEOMETRY_BOX ? geom.getMinorRadius() : geom.getMajorRadius();
			blip.angle = obj->getOrientation();
		}

		m_blips.push_back( blip );

	}  // end for

}  // end collectBlips

//-------------------------------------------------------------------------------------------------
/** The selection's plans, read where the 3D view's own lines read them (W3DWaypointBuffer): each
	* leg of the selected units' orders off InGameUI's order hints, and each selected production
	* building of the local player's from where it stands to its rally point, the one just sent and
	* not yet run by logic first. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::collectOrderLines( void )
{
	m_cachedOrderLines.clear();
	if( TheInGameUI == NULL )
		return;

	const std::vector<InGameUI::OrderHint> &hints = TheInGameUI->getOrderHints();
	for( size_t i = 0; i < hints.size(); ++i )
	{
		if( hints[ i ].mark == InGameUI::ORDER_MARK_NONE )
			continue;
		ReinforcementLine line;
		line.from = hints[ i ].from;
		line.to = hints[ i ].to;
		m_cachedOrderLines.push_back( line );
	}

	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || !obj->isLocallyControlled() )
			continue;
		ExitInterface *exit = obj->getObjectExitInterface();
		if( exit == NULL )
			continue;
		const Coord3D *rally = TheInGameUI->getPredictedRallyPoint( obj->getID() );
		if( rally == NULL )
			rally = exit->getRallyPoint();
		if( rally == NULL )
			continue;
		ReinforcementLine line;
		line.from = *obj->getPosition();
		line.to = *rally;
		m_cachedOrderLines.push_back( line );
	}
}

//-------------------------------------------------------------------------------------------------
/** Every supply source and tech building as the drawn player last saw it, one mark a place.
	* Neither kind need be on the radar's own lists (an oil derrick names no radar priority), so this
	* walks the objects, and only once a second: they stand still.  One in plain sight is taken as it
	* is; one under the fog keeps the picture the player had, so a pile emptied or a derrick destroyed
	* there stays marked until its ground is clear again and the player can see it is gone. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::collectLandmarks( void )
{
	const Int playerIndex = TheObserverCamera.getShroudPlayerIndex();
	if( playerIndex != m_landmarkPlayer )
	{
		m_seenLandmarks.clear();
		m_landmarkPlayer = playerIndex;
	}

	// what the player sees now, and what he meets for the first time under fog
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		// KINDOF_CAPTURABLE is on every faction structure too, for Black Lotus; the tech building
		// kind is what names the ones a rifleman walks into
		const Bool supply = obj->isKindOf( KINDOF_SUPPLY_SOURCE );
		if( !supply && !obj->isKindOf( KINDOF_TECH_BUILDING ) )
			continue;
		const ObjectShroudStatus status = obj->getShroudedStatus( playerIndex );
		if( status >= OBJECTSHROUD_SHROUDED )
			continue;
		std::map<ObjectID, Landmark>::iterator seen = m_seenLandmarks.find( obj->getID() );
		if( status <= OBJECTSHROUD_PARTIAL_CLEAR )
		{
			if( obj->isEffectivelyDead() )
			{
				if( seen != m_seenLandmarks.end() )
					m_seenLandmarks.erase( seen );
				continue;
			}
			Landmark &landmark = m_seenLandmarks[ obj->getID() ];
			landmark.pos = *obj->getPosition();
			landmark.supply = supply;
		}
		else if( seen == m_seenLandmarks.end() && !obj->isEffectivelyDead() )
		{
			Landmark &landmark = m_seenLandmarks[ obj->getID() ];
			landmark.pos = *obj->getPosition();
			landmark.supply = supply;
		}
	}

	// one that is gone goes off the map once the player can see its ground
	for( std::map<ObjectID, Landmark>::iterator it = m_seenLandmarks.begin(); it != m_seenLandmarks.end(); )
	{
		const Object *obj = TheGameLogic->findObjectByID( it->first );
		if( (obj == NULL || obj->isEffectivelyDead()) &&
				ThePartitionManager->getShroudStatusForPlayer( playerIndex, &it->second.pos ) == CELLSHROUD_CLEAR )
			m_seenLandmarks.erase( it++ );
		else
			++it;
	}

	// a dock's eight piles are one dollar
	m_cachedLandmarks.clear();
	const Real mergeX = m_mapExtent.width() / RADAR_CELL_WIDTH * LANDMARK_MERGE_CELLS;
	const Real mergeY = m_mapExtent.height() / RADAR_CELL_HEIGHT * LANDMARK_MERGE_CELLS;
	for( std::map<ObjectID, Landmark>::const_iterator it = m_seenLandmarks.begin(); it != m_seenLandmarks.end(); ++it )
	{
		const Landmark &landmark = it->second;
		Bool marked = FALSE;
		for( size_t i = 0; i < m_cachedLandmarks.size() && !marked; ++i )
			marked = m_cachedLandmarks[ i ].supply == landmark.supply &&
				fabs( m_cachedLandmarks[ i ].pos.x - landmark.pos.x ) < mergeX && fabs( m_cachedLandmarks[ i ].pos.y - landmark.pos.y ) < mergeY;
		if( !marked )
			m_cachedLandmarks.push_back( landmark );
	}

	// all the dollars, then all the stars: draw changes image once
	std::stable_partition( m_cachedLandmarks.begin(), m_cachedLandmarks.end(), []( const Landmark &l ) { return l.supply; } );
}

//-------------------------------------------------------------------------------------------------
/** The blips update collected, drawn at the radar picture's own pixels every frame: a unit a square
	* dot sized to the picture, so it reads at 1080p and grows with HUD Size; a structure its
	* footprint, turned as it stands, never smaller than a dot.  Only inside draw()'s 2D batch do
	* they come to one draw call; outside one, every dot and row is a draw call of its own. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawBlips( Int pixelX, Int pixelY, Int width, Int height )
{
	if( m_blips.empty() || width <= 0 || height <= 0 )
		return;

	// a footprint grows with the zoom, a unit's dot keeps the picture's size
	const Real toPixel = (m_drawTransform.scaleX + m_drawTransform.scaleY) * 0.5f;

	const Int dot = blipDotSize( width, height );
	const Real minHalf = dot * 0.5f;

	IRegion2D clip;
	clip.lo.x = pixelX;
	clip.lo.y = pixelY;
	clip.hi.x = pixelX + width;
	clip.hi.y = pixelY + height;

	// Every structure first, then every unit, each group in list order.  The local list comes after
	// the main one and holds the local player's structures as well, and drawn in plain list order
	// their footprints covered everyone else's dots.  In the old 2x2 texels a structure was a dot
	// like any other and covered nothing.
	for( Int pass = 0; pass < 2; ++pass )
	{
		const Bool structures = (pass == 0);
		for( size_t i = 0; i < m_blips.size(); ++i )
		{
			const Blip &blip = m_blips[ i ];
			if( (blip.halfLength > 0.0f) != structures )
				continue;

			// a centre off the map shows nothing, as legalRadarPoint used to drop it
			if( blip.x < 0.0f || blip.y < 0.0f || blip.x >= m_mapExtent.width() || blip.y >= m_mapExtent.height() )
				continue;

			// +y in the world is up on the radar
			Real px, py;
			m_drawTransform.toPixel( blip.x, blip.y, &px, &py );

			if( structures )
			{
				const Real halfLength = max( blip.halfLength * toPixel, minHalf );
				const Real halfWidth = max( blip.halfWidth * toPixel, minHalf );
				const Real c = Cos( blip.angle );
				const Real s = -Sin( blip.angle );
				Real cx[ 4 ], cy[ 4 ];
				cx[ 0 ] = px + c * halfLength - s * halfWidth;	cy[ 0 ] = py + s * halfLength + c * halfWidth;
				cx[ 1 ] = px + c * halfLength + s * halfWidth;	cy[ 1 ] = py + s * halfLength - c * halfWidth;
				cx[ 2 ] = px - c * halfLength + s * halfWidth;	cy[ 2 ] = py - s * halfLength - c * halfWidth;
				cx[ 3 ] = px - c * halfLength - s * halfWidth;	cy[ 3 ] = py - s * halfLength + c * halfWidth;
				fillQuadClipped( clip, cx, cy, blip.color );
			}
			else
			{
				fillClipped( clip, REAL_TO_INT_FLOOR( px ) - dot / 2, REAL_TO_INT_FLOOR( py ) - dot / 2, dot, blip.color );
			}
		}
	}

}  // end drawBlips

//-------------------------------------------------------------------------------------------------
/** Shade the color passed in using the height parameter to lighten and darken it.  Colors
	* will be interpolated using the value "height" across the range from loZ to hiZ.  The
	* midZ is the "middle" point, height values above it will be lightened, while 
	* lower ones are darkened. */
//-------------------------------------------------------------------------------------------------	
void W3DRadar::interpolateColorForHeight( RGBColor *color,	
																					Real height, 
																					Real hiZ,
																					Real midZ,
																					Real loZ )
{
	const Real howBright = 0.95f;  // bigger is brighter (0.0 to 1.0)
	const Real howDark   = 0.60f;  // bigger is darker (0.0 to 1.0)
	
	// sanity on map height (flat maps bomb)
	if (hiZ == midZ)
		hiZ = midZ+0.1f;
	if (midZ == loZ)
		loZ = midZ-0.1f;
	if (hiZ == loZ)
		hiZ = loZ+0.2f;

	Real t;
	RGBColor colorTarget;

	// if "over" the middle height, interpolate lighter
	if( height >= midZ )
	{

		// how far are we from the middleZ towards the hi Z
		t = (height - midZ) / (hiZ - midZ);

		// compute what our "lightest" color possible we want to use is
		colorTarget.red = color->red + (1.0f - color->red) * howBright;
		colorTarget.green = color->green + (1.0f - color->green) * howBright;
		colorTarget.blue = color->blue + (1.0f - color->blue) * howBright;

	}  // end if
	else  // interpolate darker
	{

		// how far are we from the middleZ towards the low Z
		t = (midZ - height) / (midZ - loZ);

		// compute what the "darkest" color possible we want to use is
		colorTarget.red = color->red + (0.0f - color->red) * howDark;
		colorTarget.green = color->green + (0.0f - color->green) * howDark;
		colorTarget.blue = color->blue + (0.0f - color->blue) * howDark;

	}  // end else

	// interpolate toward the target color
	color->red = color->red + (colorTarget.red - color->red) * t;
	color->green = color->green + (colorTarget.green - color->green) * t;
	color->blue = color->blue + (colorTarget.blue - color->blue) * t;

	// keep the color real
	if( color->red < 0.0f )
		color->red = 0.0f;
	if( color->red > 1.0f )
		color->red = 1.0f;
	if( color->green < 0.0f )
		color->green = 0.0f;
	if( color->green > 1.0f )
		color->green = 1.0f;
	if( color->blue < 0.0f )
		color->blue = 0.0f;
	if( color->blue > 1.0f )
		color->blue = 1.0f;

}  // end interpolateColorForHeight

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS /////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRadar::W3DRadar( void )
{

	m_terrainTextureFormat = WW3D_FORMAT_UNKNOWN;
	m_terrainImage = NULL;
	m_terrainTexture = NULL;

	m_overlayDrawn = FALSE;
	m_eventsDrawn = FALSE;

	m_shroudTextureFormat = WW3D_FORMAT_UNKNOWN;
	m_shroudImage = NULL;
	m_shroudTexture = NULL;
	m_shroudWidth = RADAR_CELL_WIDTH;
	m_shroudHeight = RADAR_CELL_HEIGHT;
	m_shroudTextureWidth = 0;
	m_shroudTextureHeight = 0;
	m_shroudScale = 1;
	m_shroudCellWidth = 0.0f;
	m_shroudCellHeight = 0.0f;
	m_landmarkFrame = 0;
	m_landmarkPlayer = -1;

	m_textureWidth = RADAR_TERRAIN_TEXTURE_SIZE;
	m_textureHeight = RADAR_TERRAIN_TEXTURE_SIZE;

	m_reconstructViewBox = TRUE;
	for( Int i = 0; i < 4; i++ )
	{

		m_viewBox[ i ].x = 0;
		m_viewBox[ i ].y = 0;

	}  // end for

}  // end W3DRadar

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRadar::~W3DRadar( void )
{

	// delete resources used for the W3D radar
	deleteResources();

}  // end ~W3DRadar

//-------------------------------------------------------------------------------------------------
/** Radar initialization */
//-------------------------------------------------------------------------------------------------
void W3DRadar::init( void )
{
	ICoord2D size;
	Region2D uv;

	// extending functionality
	Radar::init();

	// gather specific texture format information
	initializeTextureFormats();

	// allocate our terrain texture
	// poolify
	m_terrainTexture = MSGNEW("TextureClass") TextureClass( m_textureWidth, m_textureHeight, 
																			 m_terrainTextureFormat, MIP_LEVELS_1 );
	DEBUG_ASSERTCRASH( m_terrainTexture, ("W3DRadar: Unable to allocate terrain texture\n") );

	// the edge texels are not to bleed into the opposite edge
	m_terrainTexture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	m_terrainTexture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );

	// the shroud is written in main memory and handed to its texture once a frame; draw makes the
	// texture, and makes it again whenever a map's shroud grid needs a different size
	m_shroudCache.setSize( m_shroudWidth, m_shroudHeight );

	//
	// create images used for rendering and set them up with the textures
	//

	//
	// the terrain image, note the UV coords change it from (0,0) in the upper left
	// to (0,0) in the lower left cause that's how we are initially oriented in the
	// world (positive X to the right and positive Y up)
	//
	m_terrainImage = newInstance(Image);
	uv.lo.x = 0.0f;
	uv.lo.y = 1.0f;
	uv.hi.x = 1.0f;
	uv.hi.y = 0.0f;
	m_terrainImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );
	m_terrainImage->setRawTextureData( m_terrainTexture );
	m_terrainImage->setUV( &uv );
	m_terrainImage->setTextureWidth( m_textureWidth );
	m_terrainImage->setTextureHeight( m_textureHeight );
	size.x = m_textureWidth;
	size.y = m_textureHeight;
	m_terrainImage->setImageSize( &size );

	// the shroud image; draw gives it its texture and its UVs
	m_shroudImage = newInstance(Image);
	m_shroudImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );

}  // end init

//-------------------------------------------------------------------------------------------------
/** Reset the radar to the initial empty state ready for new data */
//-------------------------------------------------------------------------------------------------
void W3DRadar::reset( void )
{

	// extending functionality, call base class
	Radar::reset();

	// clear our texture data, but do not delete the resources
	SurfaceClass *surface;

	surface = m_terrainTexture->Get_Surface_Level();
	if( surface )
	{
		surface->Clear();
		REF_PTR_RELEASE(surface);
	}

	// the objects those positions belonged to are gone with the match that had them
	m_blips.clear();
	m_cachedHeroPosList.clear();
	m_cachedReinforcementLines.clear();
	m_cachedOrderLines.clear();
	m_cachedLandmarks.clear();
	m_seenLandmarks.clear();
	m_landmarkFrame = 0;
	m_landmarkPlayer = -1;

	// don't call Clear(); that wips to transparent. do this instead.
	//gs Dude, it's called CLEARshroud.  It needs to clear the shroud.
	clearShroud();

}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update */
//-------------------------------------------------------------------------------------------------
void W3DRadar::update( void )
{

	// extend base class
	Radar::update();

	// the sound of each event the last draw showed for the first time
	if( m_eventsDrawn )
	{
		m_eventsDrawn = FALSE;
		for( Int i = 0; i < MAX_RADAR_EVENTS; i++ )
		{
			if( m_event[ i ].active == FALSE || m_event[ i ].type == RADAR_EVENT_FAKE )
				continue;
			if( m_event[ i ].soundPlayed == FALSE && m_event[ i ].type != RADAR_EVENT_BEACON_PULSE )
			{
				// a lost building pings the radar under EVA's line; the rest keep EA's RadarEvent, which
				// the retail data leaves without a sound.  An attack has its own, played as it is made
				static AudioEventRTS eventSound("RadarEvent");
				static AudioEventRTS lostSound("RadarPing");
				TheAudio->addAudioEvent( m_event[ i ].type == RADAR_EVENT_STRUCTURE_LOST ? &lostSound : &eventSound );
			}
			m_event[ i ].soundPlayed = TRUE;
		}
	}

	// Collect the blips every update while the radar is shown; they used to go into a texture every
	// twelve frames and stepped across the map.  Here, not in draw: the blips ask each object where it
	// is, whether the drawn player sees it and whether it is stealthed, and draw runs inside the
	// render, beside the tick once it runs there (LOGIC-THREAD.md phase 3).
	if( m_overlayDrawn )
	{
		m_overlayDrawn = FALSE;

		m_blips.clear();
		m_cachedHeroPosList.clear();
		m_cachedReinforcementLines.clear();
		collectBlips( getObjectList() );
		collectBlips( getLocalObjectList(), TRUE );
		collectOrderLines();

		const UnsignedInt frame = TheGameLogic->getFrame();
		if( frame < m_landmarkFrame || frame - m_landmarkFrame >= LOGICFRAMES_PER_SECOND || m_landmarkFrame == 0 )
		{
			m_landmarkFrame = frame == 0 ? 1 : frame;
			collectLandmarks();
		}

	}  // end if

}  // end update

//-------------------------------------------------------------------------------------------------
/** Reset the radar for the new map data being given to it */
//-------------------------------------------------------------------------------------------------
void W3DRadar::newMap( TerrainLogic *terrain )
{

	//
	// extending functionality, call the base class ... this will cause a reset of the
	// system which will clear out our textures but not free them
	//
	Radar::newMap( terrain );

	// sanity
	if( terrain == NULL )
		return;

	// build terrain texture
	buildTerrainTexture( terrain );

}  // end newMap

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::buildTerrainTexture( TerrainLogic *terrain )
{
	SurfaceClass *surface;
	RGBColor waterColor;

	// we will want to reconstruct our new view box now
	m_reconstructViewBox = TRUE;

	// setup our water color
	waterColor.red = TheWaterTransparency->m_radarColor.red;
	waterColor.green = TheWaterTransparency->m_radarColor.green;
	waterColor.blue = TheWaterTransparency->m_radarColor.blue;

	// get the terrain surface to draw in
	surface = m_terrainTexture->Get_Surface_Level();
	DEBUG_ASSERTCRASH( surface, ("W3DRadar: Can't get surface for terrain texture\n") );

	// one lock for the whole texture - see the note below about what EA measured here
	Int surfacePitch;
	void *surfaceBits = surface->Lock( &surfacePitch );
	const UnsignedInt bytesPerPixel = surface->Get_Bytes_Per_Pixel();

	// build the terrain
	RGBColor sampleColor;
	RGBColor color;
	Int i, j, samples;
	Int x, y;
	Coord3D worldPoint;
	Bridge *bridge;

	// a texel's corner in the world, as radarToWorld2D does for a radar cell, at the texture's own
	// resolution rather than the 128 radar cells
	const Real texelWidth = m_mapExtent.width() / m_textureWidth;
	const Real texelHeight = m_mapExtent.height() / m_textureHeight;
	auto texelToWorld2D = [&]( Int tx, Int ty, Coord3D *world )
	{
		world->x = tx * texelWidth;
		world->y = ty * texelHeight;
		world->z = 0.0f;
	};

	for( y = 0; y < m_textureHeight; y++ )
	{

		for( x = 0; x < m_textureWidth; x++ )
		{

			// what point are we inspecting
			texelToWorld2D( x, y, &worldPoint );

			// check to see if this point is part of a working bridge
			Bool workingBridge = FALSE;
			bridge = TheTerrainLogic->findBridgeAt( &worldPoint );
			if( bridge != NULL )
			{
				Object *obj = TheGameLogic->findObjectByID( bridge->peekBridgeInfo()->bridgeObjectID );

				if( obj )
				{
					BodyModuleInterface *body = obj->getBodyModule();

					if( body->getDamageState() != BODY_RUBBLE )
						workingBridge = TRUE;

				}  // end if

			}  // end if

			// create a color based on the Z height of the map
			Real waterZ;
			if( workingBridge == FALSE && terrain->isUnderwater( worldPoint.x, worldPoint.y, &waterZ ) )
			{
				const Int waterSamplesAway = 1;		// how many "tiles" from the center tile we will sample away
																					// to average a color for the tile color

				sampleColor.red = sampleColor.green = sampleColor.blue = 0.0f;
				samples = 0;

				for( j = y - waterSamplesAway; j <= y + waterSamplesAway; j++ )
				{

					if( j >= 0 && j < m_textureHeight )
					{

						for( i = x - waterSamplesAway; i <= x + waterSamplesAway; i++ )
						{

							if( i >= 0 && i < m_textureWidth )
							{

								// the the world point we are concerned with
								texelToWorld2D( i, j, &worldPoint );
								
								// get color for this Z and add to our sample color
                Real underwaterZ;
								if( terrain->isUnderwater( worldPoint.x, worldPoint.y, NULL, &underwaterZ ) )
								{
									// this is our "color" for water
									color = waterColor;									

									// interpolate the water color for height in the water table
									interpolateColorForHeight( &color, underwaterZ, waterZ,
																						 waterZ,
																						 m_mapExtent.lo.z );

									// add color to our samples
									sampleColor.red += color.red;
									sampleColor.green += color.green;
									sampleColor.blue += color.blue;
									samples++;

								}  // end if

							}  // end if

						}  // end for i

					}  // end if

				}  // end for j

				// prevent divide by zeros
				if( samples == 0 )
					samples = 1;

				// set the color to an average of the colors read
				color.red = sampleColor.red / (Real)samples;
				color.green = sampleColor.green / (Real)samples;
				color.blue = sampleColor.blue / (Real)samples;

			}  // end if
			else  // regular terrain ...
			{
				const Int samplesAway = 1;  // how many "tiles" from the center tile we will sample away
																		// to average a color for the tile color

				sampleColor.red = sampleColor.green = sampleColor.blue = 0.0f;
				samples = 0;

				for( j = y - samplesAway; j <= y + samplesAway; j++ )
				{

					if( j >= 0 && j < m_textureHeight )
					{

						for( i = x - samplesAway; i <= x + samplesAway; i++ )
						{

							if( i >= 0 && i < m_textureWidth )
							{

								// the the world point we are concerned with, on the ground
								texelToWorld2D( i, j, &worldPoint );
								worldPoint.z = TheTerrainLogic->getGroundHeight( worldPoint.x, worldPoint.y );

								// get the color we're going to use here																
								if( workingBridge )
								{
									AsciiString bridgeTName = bridge->getBridgeTemplateName();
									TerrainRoadType *bridgeTemplate = TheTerrainRoads->findBridge( bridgeTName );
									
									// sanity
									DEBUG_ASSERTCRASH( bridgeTemplate, ("W3DRadar::buildTerrainTexture - Can't find bridge template for '%s'\n", bridgeTName.str()) );

									// use bridge color
									if ( bridgeTemplate )
										color = bridgeTemplate->getRadarColor();
									else
										color.setFromInt(0xffffffff);
									//
									// we won't use the height of the terrain at this sample point, we will
									// instead use the height for the entire bridge
									//
									Real bridgeHeight = (bridge->peekBridgeInfo()->fromLeft.z + 
																			 bridge->peekBridgeInfo()->fromRight.z +
																			 bridge->peekBridgeInfo()->toLeft.z +
																			 bridge->peekBridgeInfo()->toRight.z) / 4.0f;

									// interpolate the color, but use the bridge height, not the terrain height
									interpolateColorForHeight( &color, bridgeHeight,
																						 getTerrainAverageZ(),
																						 m_mapExtent.hi.z, m_mapExtent.lo.z );

								}  // end if
								else
								{

									// get the color at this point
									TheTerrainVisual->getTerrainColorAt( worldPoint.x, worldPoint.y, &color );

									// interpolate the color for height
									interpolateColorForHeight( &color, worldPoint.z, getTerrainAverageZ(), 
																						 m_mapExtent.hi.z, m_mapExtent.lo.z );

								}  // end else

								// add color to our samples
								sampleColor.red += color.red;
								sampleColor.green += color.green;
								sampleColor.blue += color.blue;
								samples++;

							}  // end if

						}  // end for i

					}  // end if

				}  // end for j

				// prevent divide by zeros
				if( samples == 0 )
					samples = 1;

				// set the color to an average of the colors read
				color.red = sampleColor.red / (Real)samples;
				color.green = sampleColor.green / (Real)samples;
				color.blue = sampleColor.blue / (Real)samples;

			}  // end else

			//
			// draw the pixel for the terrain at this point, note that because of the orientation
			// of our world we draw it with positive y in the "up" direction
			//
			// FYI: I tried making this faster by pulling out all the code inside DrawPixel
			// and locking only once ... but it made absolutely *no* performance difference,
			// the sampling and interpolation algorithm for generating pretty looking terrain
			// and water for the radar is just, well, expensive.
			//
			// That was measured against a 2003 driver writing straight to an AGP surface.  Here
			// every LockRect crosses d3d8to9 into the Direct3D 9 runtime, so the lock is hoisted
			// anyway - the sampling above is still the expensive half, and is untouched.
			surface->Draw_Pixel( x, y, GameMakeColor( color.red * 255,
																							  color.green * 255,
																							  color.blue * 255,
																							  255 ),
												 bytesPerPixel, surfaceBits, surfacePitch );

		}  // end for x

	}  // end for y

	// all done with the surface
	surface->Unlock();
	REF_PTR_RELEASE(surface);

}  // end buildTerrainTexture

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::clearShroud()
{
	LOGIC_AFTER_TICK( [this]() { clearShroud(); } );
	LOGIC_CENSUS( CENSUS_RADAR );
#if defined(_DEBUG) || defined(_INTERNAL)
	if (!TheGlobalData->m_shroudOn)
		return;
#endif

	// fill to clear, shroud will make black.  Don't want to make something black that logic can't clear
	m_shroudCache.clear( 0 );
}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::setShroudLevel(Int shroudX, Int shroudY, CellShroudStatus setting)
{
	LOGIC_AFTER_TICK( [this, shroudX, shroudY, setting]() { setShroudLevel( shroudX, shroudY, setting ); } );
	LOGIC_CENSUS( CENSUS_RADAR );
#if defined(_DEBUG) || defined(_INTERNAL)
	if (!TheGlobalData->m_shroudOn)
		return;
#endif

	W3DShroud* shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : NULL;
	if (!shroud)
		return;

	// One byte a shroud cell, which uploadShroud smooths into the texture.  The cells come from the
	// partition grid over the map's active extent, which can be smaller than the shroud's own grid
	// over the height map, so the count is the extent's.  The first cell of a map sizes the cache,
	// one texel over each way for the edge copy below.
	if( shroud->getCellWidth() <= 0.0f || shroud->getCellHeight() <= 0.0f )
		return;
	const Int cellsX = REAL_TO_INT_CEIL( m_mapExtent.width() / shroud->getCellWidth() );
	const Int cellsY = REAL_TO_INT_CEIL( m_mapExtent.height() / shroud->getCellHeight() );
	if( cellsX <= 0 || cellsY <= 0 )
		return;
	const Int wantWidth = nextPowerOfTwo( cellsX + 1 );
	const Int wantHeight = nextPowerOfTwo( cellsY + 1 );
	if( wantWidth != m_shroudWidth || wantHeight != m_shroudHeight )
	{
		m_shroudWidth = wantWidth;
		m_shroudHeight = wantHeight;
		m_shroudCache.setSize( m_shroudWidth, m_shroudHeight );
	}
	m_shroudCellWidth = shroud->getCellWidth();
	m_shroudCellHeight = shroud->getCellHeight();

	//Logic is saying shroud.  We can add alpha levels here in client if needed.  
	// W3DShroud is a 0-255 alpha byte.  Logic shroud is a double reference count.
	Int alpha;
	if( setting == CELLSHROUD_SHROUDED )
		alpha = 255;
	else if( setting == CELLSHROUD_FOGGED )
		alpha = 127;///< @todo placeholder to get feedback on logic work while graphic side being decided
	else
		alpha = 0;

	// this used to lock the shroud texture once per pixel, through DrawPixel, and it is called
	// every time any unit's line of sight crosses a shroud cell boundary.  The cache takes the
	// writes and draw() hands the changed rectangle to the texture in one lock.  A cell on the far
	// edge is copied one texel out, so the filter does not fade the map's last cells toward clear.
	const Int lastX = shroudX == cellsX - 1 ? shroudX + 1 : shroudX;
	const Int lastY = shroudY == cellsY - 1 ? shroudY + 1 : shroudY;
	for( Int y = shroudY; y <= lastY; y++ )
		for( Int x = shroudX; x <= lastX; x++ )
			m_shroudCache.setAlpha( x, y, (UnsignedByte)alpha );
}

//-------------------------------------------------------------------------------------------------
/** The cache's changed cells into the shroud texture, one lock of the changed rectangle a frame.
	* Each cell is first softened with a tent RADAR_SHROUD_RAMP_CELLS wide each way, then every texel
	* is the bilinear blend of the softened cells round its centre, in integers, remapped so the dark
	* rises from nothing at a clear cell's edge to full that many cells in, and never darker than its
	* own cell: the ramp lies on the dark side of the edge.  The picture is made here, so the
	* Direct3D 9 and 11 devices show the same one.  A changed cell moves texels up to the tent's
	* radius plus one cell away, and that whole rectangle is rewritten. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::uploadShroud( void )
{
	if( m_shroudTexture == NULL || !m_shroudCache.isDirty() || m_shroudCellWidth <= 0.0f || m_shroudCellHeight <= 0.0f )
		return;

	// the map's cells; the cache's last column and row past them are copies, and beyond is unused
	const Int cellsX = min( m_shroudWidth, max( 1, REAL_TO_INT_CEIL( m_mapExtent.width() / m_shroudCellWidth ) ) );
	const Int cellsY = min( m_shroudHeight, max( 1, REAL_TO_INT_CEIL( m_mapExtent.height() / m_shroudCellHeight ) ) );

	// a changed cell reaches texels as far as the kernel's radius plus the blend's one cell
	const Int reach = RADAR_SHROUD_RAMP_CELLS + 1;
	const Int x0 = max( 0, m_shroudCache.getDirtyMinX() - reach );
	const Int y0 = max( 0, m_shroudCache.getDirtyMinY() - reach );
	const Int x1 = min( m_shroudWidth - 1, m_shroudCache.getDirtyMaxX() + reach );
	const Int y1 = min( m_shroudHeight - 1, m_shroudCache.getDirtyMaxY() + reach );
	if( x1 < x0 || y1 < y0 )
	{
		m_shroudCache.markClean();
		return;
	}

	// The softened cells, 256 times their alpha, over the rectangle and one more all round, which
	// the blend reaches into: a tent of radius RADAR_SHROUD_RAMP_CELLS each way (1 2 3 4 3 2 1),
	// across the rows first and then down the columns.
	const Int R = RADAR_SHROUD_RAMP_CELLS;
	const Int bx0 = x0 - 1, by0 = y0 - 1;
	const Int bw = x1 - x0 + 3, bh = y1 - y0 + 3;
	const Int rows = bh + 2 * R;
	m_shroudRows.resize( bw * rows );
	for( Int j = 0; j < rows; ++j )
	{
		const Int sy = max( 0, min( cellsY - 1, by0 - R + j ) );
		for( Int i = 0; i < bw; ++i )
		{
			Int sum = 0;
			for( Int d = -R; d <= R; ++d )
				sum += m_shroudCache.getAlpha( max( 0, min( cellsX - 1, bx0 + i + d ) ), sy ) * (R + 1 - abs( d ));
			m_shroudRows[ j * bw + i ] = sum;
		}
	}
	m_shroudBlur.resize( bw * bh );
	for( Int j = 0; j < bh; ++j )
	{
		for( Int i = 0; i < bw; ++i )
		{
			Int sum = 0;
			for( Int d = -R; d <= R; ++d )
				sum += m_shroudRows[ (j + R + d) * bw + i ] * (R + 1 - abs( d ));
			m_shroudBlur[ j * bw + i ] = sum;
		}
	}
	const Int blurWeight = (R + 1) * (R + 1) * (R + 1) * (R + 1);	// the tent's sum, squared

	// no surface while the device is lost: the cache stays dirty and the next frame writes it
	SurfaceClass *surface = m_shroudTexture->Get_Surface_Level();
	if( surface == NULL )
		return;
	SurfaceClass::SurfaceDescription sd;
	surface->Get_Description( sd );
	const Int bytesPerPixel = Get_Bytes_Per_Pixel( sd.Format );
	const Int s = m_shroudScale;
	const Int left = x0 * s, top = y0 * s, right = (x1 + 1) * s, bottom = (y1 + 1) * s;
	Int pitch;
	UnsignedByte *bits = (UnsignedByte *)surface->Lock_Rect( &pitch, left, top, right, bottom );
	if( bits )
	{
		// A texel lies a quarter cell off its cell's centre at scale 2, so its blend is three parts
		// its own cell and one part the neighbour on that side, each way; at scale 1 it is the cell.
		const Int own = s == 1 ? 4 : 3;
		const Int next = 4 - own;
		for( Int ty = top; ty < bottom; ++ty )
		{
			const Int j = ty / s;
			const Int nj = (s == 1 || (ty % s) == 0) ? j - 1 : j + 1;
			const Int r0 = j - by0;
			const Int r1 = max( 0, min( bh - 1, nj - by0 ) );
			UnsignedByte *row = bits + (ty - top) * pitch;
			for( Int tx = left; tx < right; ++tx, row += bytesPerPixel )
			{
				const Int i = tx / s;
				const Int ni = (s == 1 || (tx % s) == 0) ? i - 1 : i + 1;
				const Int c0 = i - bx0;
				const Int c1 = max( 0, min( bw - 1, ni - bx0 ) );
				const Int blend = own * (own * m_shroudBlur[ r0 * bw + c0 ] + next * m_shroudBlur[ r0 * bw + c1 ])
					+ next * (own * m_shroudBlur[ r1 * bw + c0 ] + next * m_shroudBlur[ r1 * bw + c1 ]);
				// The softened value is half way at an edge and whole RADAR_SHROUD_RAMP_CELLS in, so
				// twice it less the cell's own alpha runs from nothing at the edge to the cell's alpha
				// that far into the dark.  Clamped to the cell: a cell the player sees stays clear to
				// its edge, and a lone clear cell in the shroud is not dimmed.  A shrouded cell beside
				// fog comes out at the fog's alpha at the edge, so the two ramps join.  The ramp never
				// drops a dark cell below RADAR_SHROUD_FLOOR_EIGHTHS of its own alpha: a strip of fog
				// two cells wide between two sight circles, or a lone dark cell, reads as nothing but
				// ramp edge and would otherwise draw as clear ground over objects it hides.
				const Int cellAlpha = m_shroudCache.getAlpha( max( 0, min( cellsX - 1, i ) ), max( 0, min( cellsY - 1, j ) ) );
				const Int scale = blurWeight * 16;
				const Int twice = (Int)(((Int64)blend * 2 + scale / 2) / scale);
				const Int least = cellAlpha * RADAR_SHROUD_FLOOR_EIGHTHS / 8;
				const UnsignedInt alpha = (UnsignedInt)min( cellAlpha, max( least, twice - cellAlpha ) );
				// black with the alpha, as RadarShroudCache::flushTo writes it
				switch( bytesPerPixel )
				{
					case 1: *row = (UnsignedByte)alpha; break;
					case 2: *(UnsignedShort *)row = (UnsignedShort)((alpha >> 4) << 12); break;
					case 4: *(UnsignedInt *)row = alpha << 24; break;
				}
			}
		}
		surface->Unlock();
		m_shroudCache.markClean();
	}
	REF_PTR_RELEASE( surface );
}

//-------------------------------------------------------------------------------------------------
/** Actually draw the radar at the screen coordinates provided 
	* NOTE about how drawing works: The radar images are computed at samples across the
	* map and are built into a "square" texture area.  At the time of drawing and computing
	* radar<->world coords we consider the "ratio" of width to height of the map dimensions
	* so that when we draw we preserve the aspect ratio of the map and don't squish it in
	* any direction that would cause the map to be distorted.  Extra blank space is drawn
	* around the radar images to keep the whole radar area covered when the map displayed
	* is "long" or "tall" */
//-------------------------------------------------------------------------------------------------
void W3DRadar::draw( Int pixelX, Int pixelY, Int width, Int height )
{
	// W3DLeftHUDDraw, the one caller, asked whether the local player has a radar, from the sync point's
	// copy inside the overlap window
	//
	// given a upper left corner at pixelX|Y and a width and height to draw into, figure out
	// where we should start and end the image so that the final drawn image has the
	// same ratio as the map and isn't stretched or distorted
	//
	ICoord2D ul, lr;
	findDrawPositions( pixelX, pixelY, width, height, &ul, &lr );

	// Everything below is fills, lines and images, no text, so one 2D batch holds the whole radar:
	// a run of untextured calls shares one draw and a change of image flushes first, order kept.
	// Without it every blip dot and footprint row was its own draw call.  The batch does not nest
	// (endBatch2D turns batching off), and W3DLeftHUDDraw, the one caller, draws outside one.
	TheDisplay->beginBatch2D();

	Int scaledWidth = lr.x - ul.x;
	Int scaledHeight = lr.y - ul.y;

	// draw black border areas where we need map
	Color fillColor = GameMakeColor( 0, 0, 0, 255 );
	Color lineColor = GameMakeColor( 50, 50, 50, 255 );
	if( ul.x == pixelX && ul.y == pixelY )
	{
		// a window already cut to the map's shape (ControlBar::fitRadarToMap) has no bands to fill, and
		// their edge lines would fall a pixel outside the map, on the frame
	}
	else if( m_mapExtent.width()/width >= m_mapExtent.height()/height )
	{

		// draw horizontal bars at top and bottom
		TheDisplay->drawFillRect( pixelX, pixelY, width, ul.y - pixelY - 1, fillColor );
		TheDisplay->drawFillRect( pixelX, lr.y + 1, width, pixelY + height - lr.y - 1, fillColor);
		TheDisplay->drawLine(pixelX, ul.y, pixelX + width, ul.y, 1, lineColor);
		TheDisplay->drawLine(pixelX, lr.y + 1, pixelX + width, lr.y + 1, 1, lineColor);

	}  // end if
	else
	{

		// draw vertical bars to the left and right
		TheDisplay->drawFillRect( pixelX, pixelY, ul.x - pixelX - 1, height, fillColor );
		TheDisplay->drawFillRect( lr.x + 1, pixelY, width - (lr.x - pixelX) - 1, height, fillColor );
		TheDisplay->drawLine(ul.x, pixelY, ul.x, pixelY + height, 1, lineColor);
		TheDisplay->drawLine(lr.x + 1, pixelY, lr.x + 1, pixelY + height, 1, lineColor);

	}  // end else

	// Everything below goes through the one transform input uses as well (Radar::localPixelToWorld),
	// so a zoomed minimap draws and is clicked the same.  The images show the zoomed part of the map
	// as shares of it: u from the left, v from the bottom, +y up as the terrain image is laid out.
	m_drawTransform.set( m_zoomView, m_mapExtent.width(), m_mapExtent.height(), ul, lr );
	const Real shownLeft = m_zoomView.x / m_mapExtent.width();
	const Real shownRight = shownLeft + 1.0f / m_zoomView.zoom;
	const Real shownBottom = m_zoomView.y / m_mapExtent.height();
	const Real shownTop = shownBottom + 1.0f / m_zoomView.zoom;

	// draw the terrain texture
	Region2D terrainUV;
	terrainUV.lo.x = shownLeft;
	terrainUV.lo.y = shownTop;
	terrainUV.hi.x = shownRight;
	terrainUV.hi.y = shownBottom;
	m_terrainImage->setUV( &terrainUV );
	TheDisplay->drawImage( m_terrainImage, ul.x, ul.y, lr.x, lr.y );

	// the money and the tech buildings are part of the ground: under every dot and footprint, and
	// under the fog like the ground they stand on
	drawLandmarks( ul.x, ul.y, scaledWidth, scaledHeight );

	// the objects, as update last collected them
	m_overlayDrawn = TRUE;
	drawBlips( ul.x, ul.y, scaledWidth, scaledHeight );

	// draw the shroud image
#if defined(_DEBUG) || defined(_INTERNAL)
	if( TheGlobalData->m_shroudOn && m_shroudCellWidth > 0.0f )
#else
	if( m_shroudCellWidth > 0.0f )
#endif
	{
		// Several texels a shroud cell, which uploadShroud fills from the cells softened over their
		// neighbours, then sampled bilinearly: the fog's edge is a smooth ramp about two cells wide
		// rather than the cell steps one bilinear texel a cell left on every diagonal.  The cache
		// picks its size when a map's first cell arrives; a new texture starts out owing the cache's
		// whole surface, which setSize marked dirty.
		if( m_shroudTexture == NULL || m_shroudTextureWidth != m_shroudWidth || m_shroudTextureHeight != m_shroudHeight )
		{
			REF_PTR_RELEASE( m_shroudTexture );
			m_shroudTextureWidth = 0;
			m_shroudTextureHeight = 0;
			m_shroudScale = RADAR_SHROUD_MAX_SCALE;
			while( m_shroudScale > 1 && max( m_shroudWidth, m_shroudHeight ) * m_shroudScale > RADAR_SHROUD_MAX_TEXTURE )
				m_shroudScale >>= 1;
			m_shroudTexture = MSGNEW("TextureClass") TextureClass( m_shroudWidth * m_shroudScale, m_shroudHeight * m_shroudScale,
																					 m_shroudTextureFormat, MIP_LEVELS_1 );
		}
		if( m_shroudTexture && (m_shroudTextureWidth != m_shroudWidth || m_shroudTextureHeight != m_shroudHeight) )
		{
			// plain bilinear on both devices, whatever the player's texture filter option says
			m_shroudTexture->Get_Filter().Set_Min_Filter( TextureFilterClass::FILTER_TYPE_FAST );
			m_shroudTexture->Get_Filter().Set_Mag_Filter( TextureFilterClass::FILTER_TYPE_FAST );
			m_shroudTexture->Get_Filter().Set_Mip_Mapping( TextureFilterClass::FILTER_TYPE_NONE );
			m_shroudTexture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
			m_shroudTexture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
			m_shroudTextureWidth = m_shroudWidth;
			m_shroudTextureHeight = m_shroudHeight;

			ICoord2D size;
			size.x = m_shroudWidth * m_shroudScale;
			size.y = m_shroudHeight * m_shroudScale;
			m_shroudImage->setRawTextureData( m_shroudTexture );
			m_shroudImage->setTextureWidth( size.x );
			m_shroudImage->setTextureHeight( size.y );
			m_shroudImage->setImageSize( &size );
		}

		// the map's part of the grid, and of that the part the zoom shows, +y up as on the terrain image
		const Real gridU = m_mapExtent.width() / (m_shroudCellWidth * m_shroudWidth);
		const Real gridV = m_mapExtent.height() / (m_shroudCellHeight * m_shroudHeight);
		Region2D uv;
		uv.lo.x = shownLeft * gridU;
		uv.lo.y = shownTop * gridV;
		uv.hi.x = shownRight * gridU;
		uv.hi.y = shownBottom * gridV;
		m_shroudImage->setUV( &uv );

		// hand the frame's shroud changes to the texture, one lock for all of them
		uploadShroud();

		if( m_shroudTexture )
			TheDisplay->drawImage( m_shroudImage, ul.x, ul.y, lr.x, lr.y );
	}

	// over the shroud: the destination is the player's own order, and it may lie in the dark
	drawReinforcementLines( ul.x, ul.y, scaledWidth, scaledHeight );

	// the selection's rally points and queued orders, over the shroud for the same reason
	drawOrderLines( ul.x, ul.y, scaledWidth, scaledHeight );

	// the radar's contacts go over the shroud, which would dim them in the fog and black them out in
	// the shroud, and so over the unit dots of the overlay as well.  A contact is only ever a unit the
	// shown player cannot see, so it never covers its own dot; it can cover a neighbour's.  The icons
	// still go on top
	drawRadarContacts( ul.x, ul.y, scaledWidth, scaledHeight );

	// draw any icons
	drawIcons( ul.x, ul.y, scaledWidth, scaledHeight );

	// draw any radar events
	drawEvents( ul.x, ul.y, scaledWidth, scaledHeight );

	// The box used to be rebuilt only when the zoom or the angle had changed, so panning the
	// camera left it where it was until you happened to zoom or turn.  The view tells us now,
	// whatever it was that moved.
	if( m_reconstructViewBox )
		reconstructViewBox();

	// draw the view region on top of the radar reconstructing if necessary.  -directorrecord draws no
	// radar while its panes are up, so there is one view to box
	drawViewBox( ul.x, ul.y, scaledWidth, scaledHeight );

	TheDisplay->endBatch2D();

}  // end draw

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::refreshTerrain( TerrainLogic *terrain )
{
	LOGIC_AFTER_TICK( [this, terrain]() { refreshTerrain( terrain ); } );

	// extend base class
	Radar::refreshTerrain( terrain );

	// rebuild the entire terrain texture
	buildTerrainTexture( terrain );

}  // end refreshTerrain





///The following is an "archive" of an attempt to foil the mapshroud hack... saved for later, since it is too close to release to try it


/*
 *
	void W3DRadar::renderObjectList( const RadarObject *listHead, TextureClass *texture, Bool calcHero )
{

	// sanity
	if( listHead == NULL || texture == NULL )
		return;

	// get surface for texture to render into
	SurfaceClass *surface = texture->Get_Surface_Level();

	// loop through all objects and draw
	ICoord2D radarPoint;

	Player *player = ThePlayerList->getLocalPlayer();
	Int playerIndex=0;
	if (player)
		playerIndex=player->getPlayerIndex();

	UnsignedByte minAlpha = 8;

	if( calcHero )
	{
		// clear all entries from the cached hero object list
		m_cachedHeroPosList.clear();
	}

	for( const RadarObject *rObj = listHead; rObj; rObj = rObj->friend_getNext() )
	{
    UnsignedByte h = (UnsignedByte)(rObj->isTemporarilyHidden());
    if ( h )
			continue;

    UnsignedByte a = 0;

		// get object
		const Object *obj = rObj->friend_getObject();
		UnsignedByte r = 1;   // all decoys

		// cache hero object positions for drawing in icon layer
		if( calcHero && obj->isHero() )
		{
			m_cachedHeroPosList.push_back(obj->getPosition());
		}

		// get the color we're going to draw in
		UnsignedInt c = 0xfe000000;// this is a decoy
    c |= (UnsignedInt)( obj->testStatus( OBJECT_STATUS_STEALTHED ) );//so is this

		// check for shrouded status
		UnsignedByte k =  (UnsignedByte)(obj->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR);
    if ( k || a)
			continue;	//object is fogged or shrouded, don't render it.

 		//
 		// objects with a local only unit priority will only appear on the radar if they
 		// are controlled by the local player, or if the local player is an observer (cause
		// they are godlike and can see everything)
 		//
 		if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY &&
 				obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive() )
 			continue;

    UnsignedByte g = c|a;
    UnsignedByte b = h|a;
		// get object position
		const Coord3D *pos = obj->getPosition();

		// compute object position as a radar blip
		radarPoint.x = pos->x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
		radarPoint.y = pos->y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);


		const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;
		

		
		// adjust the alpha for stealth units so they "fade/blink" on the radar for the controller
		// if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY )
		// ML-- What the heck is this? local-only and neutral-observier-viewed units are stealthy?? Since when?	
		// Now it twinkles for any stealthed object, whether locally controlled or neutral-observier-viewed
    c = clientColor( rObj->getColor() );

		if( g & r )
		{
		  Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition * 0.5f);
      minAlpha <<= 2; // decoy

 			if ( ( obj->isLocallyControlled() == (Bool)a ) // another decoy, comparing the return of this non-inline with a local
        && !obj->testStatus( OBJECT_STATUS_DISGUISED ) 
        && !obj->testStatus( OBJECT_STATUS_DETECTED ) 
        && ++a != 0 // The trick is that this increment does not occur unless all three above conditions are true
        && minAlpha == 32  // tricksy hobbit decoy
        && c != 0 )        // ditto
      {
        g = (UnsignedByte)(rObj->getColor());
        continue;
      }

      a |= k | b;
			GameGetColorComponentsWithCheatSpy( c, &r, &g, &b, &a );//this function does not touch the low order bit in 'a' 

			
			if( alphaScale > 0.0f )
				a = REAL_TO_UNSIGNEDBYTE( ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha );
			else
				a = REAL_TO_UNSIGNEDBYTE( (alphaScale * (255.0f - minAlpha)) + minAlpha );
			c = GameMakeColor( r, g, b, a );

		}  // end if



		
		// draw the blip, but make sure the points are legal
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->DrawPixel( radarPoint.x, radarPoint.y, c );

		radarPoint.x++;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->DrawPixel( radarPoint.x, radarPoint.y, c );

		radarPoint.y++;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->DrawPixel( radarPoint.x, radarPoint.y, c );

		radarPoint.x--;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->DrawPixel( radarPoint.x, radarPoint.y, c );




	}  // end for
	REF_PTR_RELEASE(surface);

}  // end renderObjectList


 *
 */

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::notifyViewChanged( void )
{
	m_reconstructViewBox = TRUE;
}
