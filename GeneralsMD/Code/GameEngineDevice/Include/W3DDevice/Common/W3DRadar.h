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

// FILE: W3DRadar.h ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, January 2002
// Desc:   W3D radar implementation, this has the necessary device dependent drawing
//				 necessary for the radar

///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DRADAR_H_
#define __W3DRADAR_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <vector>
#include <map>
#include "Common/Radar.h"
#include "Common/RadarShroudCache.h"
#include "GameClient/ObserverCamera.h"
#include "WW3D2/ww3dformat.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class TextureClass;
class TerrainLogic;

// PROTOTYPES /////////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
/** W3D radar class.  This has the device specific implementation of the radar such as
	* the drawing routines */
//-------------------------------------------------------------------------------------------------
class W3DRadar : public Radar
{

public:

	W3DRadar( void );
	~W3DRadar( void );

	virtual void init( void );																		///< subsystem init
	virtual void update( void );																	///< subsystem update
	virtual void reset( void );																		///< subsystem reset

	virtual void newMap( TerrainLogic *terrain );				///< reset radar for new map

	void draw( Int pixelX, Int pixelY, Int width, Int height );		///< draw the radar

	virtual void clearShroud();
	virtual void setShroudLevel(Int x, Int y, CellShroudStatus setting);

	virtual void refreshTerrain( TerrainLogic *terrain );

	virtual void notifyViewChanged( void );				///< the camera view changed

protected:

	void drawSingleBeaconEvent( Int pixelX, Int pixelY, Int width, Int height, Int index );
	void drawSingleGenericEvent( Int pixelX, Int pixelY, Int width, Int height, Int index );

	void initializeTextureFormats( void );				///< find format to use for the radar texture
	void deleteResources( void );									///< delete resources used
	void drawEvents( Int pixelX, Int pixelY, Int width, Int height);		///< draw all of the radar events
	void drawRadarContacts( Int pixelX, Int pixelY, Int width, Int height );	///< the Turkiye radar's fogged contacts, this sweep and the last
	void drawHeroIcon( Int pixelX, Int pixelY, Int width, Int height, const Coord3D *pos );	//< draw a hero icon
	void drawReinforcementLines( Int pixelX, Int pixelY, Int width, Int height );	///< a line from each unit walking to its squad to where it walks
	void drawOrderLines( Int pixelX, Int pixelY, Int width, Int height );	///< the selection's rally points and queued orders
	void drawLandmarks( Int pixelX, Int pixelY, Int width, Int height );	///< the money and the tech buildings, the lobby preview's icons
	void collectOrderLines( void );								///< copy the selection's rally points and orders into m_cachedOrderLines
	void collectLandmarks( void );								///< copy the scouted supply sources and tech buildings into m_cachedLandmarks
	void uploadShroud( void );										///< the cache's changed cells into the shroud texture, smoothed
	void drawViewBox( Int pixelX, Int pixelY, Int width, Int height );  ///< draw view box
	void drawBoxLines( const ICoord2D *corners );	///< a view box's lines through its four corners in pixels
	void buildTerrainTexture( TerrainLogic *terrain );	 ///< create the terrain texture of the radar
	void drawIcons( Int pixelX, Int pixelY, Int width, Int height );	///< draw all of the radar icons
	void collectBlips( const RadarObject *listHead, Bool calcHero = FALSE );	///< copy what an object list shows into m_blips
	void drawBlips( Int pixelX, Int pixelY, Int width, Int height );	///< draw m_blips at the window's own pixels
	void interpolateColorForHeight( RGBColor *color, 
																	Real height, 
																	Real hiZ, 
																	Real midZ, 
																	Real loZ );		///< "shade" color according to height value
	void reconstructViewBox( void );							///< remake the view box
	void radarToPixel( const ICoord2D *radar, ICoord2D *pixel,
										 Int radarUpperLeftX, Int radarUpperLeftY,
										 Int radarWidth, Int radarHeight );  ///< convert radar coord to pixel location

	WW3DFormat m_terrainTextureFormat;						///< format to use for terrain texture
	Image *m_terrainImage;												///< terrain image abstraction for drawing
	TextureClass *m_terrainTexture;								///< terrain background texture	

	// One object on the radar as update saw it at the sync point: draw runs beside the tick and may
	// not ask the object.  World position, a structure's footprint (half sizes and facing, zero for
	// a unit's dot) and the colour with the stealth blink already in its alpha.
	struct Blip { Real x; Real y; Real halfLength; Real halfWidth; Real angle; Color color; };
	std::vector<Blip> m_blips;
	Bool m_overlayDrawn;													///< draw showed the blips since update last collected them
	Bool m_eventsDrawn;														///< draw showed the events since update last played their sounds

	WW3DFormat m_shroudTextureFormat;							///< format to use for shroud texture
	Image *m_shroudImage;													///< shroud image abstraction for drawing
	TextureClass *m_shroudTexture;								///< shroud texture, one texel a shroud cell
	RadarShroudCache m_shroudCache;								///< what the shroud texture should say, in main memory
	Int m_shroudWidth;														///< the cache's size, a power of two over the shroud's cells
	Int m_shroudHeight;
	Int m_shroudTextureWidth;											///< the cache size the texture was made for, caught up in draw
	Int m_shroudTextureHeight;
	Int m_shroudScale;														///< texels a shroud cell each way, so the edge is a smooth ramp
	std::vector<Int> m_shroudBlur;								///< scratch for uploadShroud: the cells softened over their neighbours, times the tent's weight
	std::vector<Int> m_shroudRows;								///< scratch for uploadShroud: the tent across the rows, before down the columns
	Real m_shroudCellWidth;												///< world size of one shroud cell
	Real m_shroudCellHeight;

	Int m_textureWidth;														///< width of the terrain texture
	Int m_textureHeight;													///< height of the terrain texture

	//
	// we want to keep a flag that tells us when to reconstruct the view box, we want
	// to reconstruct the box on map change, and when the camera changes height
	// or orientation.  We want to avoid making the view box every frame because 
	// the 4 points visible on the edge of the screen will "jitter" unevenly as we
	// translate real world coords to integer radar spots
	//
	Bool m_reconstructViewBox;										///< true when we need to reconstruct the box
	ICoord2D m_viewBox[ 4 ];											///< radar cell points for the 4 corners of view box

	// By value, not by pointer into the object.  The list is rebuilt in update and read by every
	// draw until the next, so a hero killed in between left a pointer into a
	// deleted Object for the icon pass to read.
	std::list<Coord3D> m_cachedHeroPosList;					//< cache of hero positions for drawing icons in radar overlay

	// The local player's units walking to their hotkey squad, where each is and where it walks, copied
	// with the hero positions at the sync point for the same reason
	struct ReinforcementLine { Coord3D from; Coord3D to; };
	std::vector<ReinforcementLine> m_cachedReinforcementLines;
	std::vector<ICoord2D> m_destinationPixels;		///< draw's scratch: each line's destination in window pixels

	// the selected production buildings' rally points and the selected units' queued orders, one leg
	// a line, copied at the sync point
	std::vector<ReinforcementLine> m_cachedOrderLines;

	// the supply sources and tech buildings on ground the drawn player has seen, copied once a second
	struct Landmark { Coord3D pos; Bool supply; };
	std::vector<Landmark> m_cachedLandmarks;
	UnsignedInt m_landmarkFrame;									///< the logic frame m_cachedLandmarks was taken on
	// each landmark as the drawn player last saw it: one in the fog keeps its last picture, a pile
	// emptied or a derrick blown up there drops off only once its ground is clear again
	std::map<ObjectID, Landmark> m_seenLandmarks;
	Int m_landmarkPlayer;													///< the drawn player m_seenLandmarks belongs to
};


#endif // __W3DRADAR_H_
