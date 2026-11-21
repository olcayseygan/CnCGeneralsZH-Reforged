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

// FILE: W3DInGameUI.h ////////////////////////////////////////////////////////
//
// W3D implementation of the in game user interface, the in game ui is 
// responsible for 
//
// Author: Colin Day, April 2001
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __W3DINGAMEUI_H_
#define __W3DINGAMEUI_H_

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "W3DDevice/GameClient/W3DView.h"
#include "WW3D2/render2d.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/line3d.h"

class HAnimClass;

// W3DInGameUI ----------------------------------------------------------------
/** Implementation for the W3D game user interface.  This singleton is 
  * responsible for all user interaction and feedback with as part of the
	* user interface */
//-----------------------------------------------------------------------------
class W3DInGameUI : public InGameUI
{

public:

	W3DInGameUI();
	virtual ~W3DInGameUI();

	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );		///< Initialize the in-game user interface
	virtual void update( void );	//< Update the UI by calling preDraw(), draw(), and postDraw()
	virtual void reset( void );		///< Reset
	//-----------------------------------------------------------------------------------------------

	virtual void draw( void ); ///< Render the in-game user interface
	Bool isWorldSpaceUI( const RenderObjClass *robj, const Drawable *draw ) const; ///< scene models belonging to the cursor and order feedback
	virtual void recordDraw( void ); ///< what draw asks of logic, run at the sync point (LOGIC-THREAD.md phase 3)

protected:

	/// factory for views
	virtual View *createView( void ) { return NEW W3DView; }

	virtual void drawSelectionRegion( void );			///< draw the selection region on screen
	virtual void drawAttackHints( View *view );		///< draw attack hint visual feedback
	virtual void drawPlaceAngle( View *view ); 		///< draw place building angle if needed
	virtual void drawBuildGrid( Bool shadeByShroud );						///< the pathfinder's build grid under a pending structure
	virtual void drawLanePlotGrids( Bool shadeByShroud );				///< the slot grid of every Lane War build plot
	virtual void drawAttackCircleFill( Bool shadeByShroud );		///< the wash on the ground inside an attack circle
	virtual void drawOrderRings( Bool shadeByShroud );	///< the dot on each unit's destination and the click markers, in the terrain pass
	void takeOrderRings( void );									///< their ground and fade, read at the sync point (recordDraw)
	std::vector< Vector3 > m_orderRingPoints;			///< a destination dot: the rim's and the colour's inside and outside, ORDER_RING_SEGMENTS + 1 of those fours a dot
	/** A live order marker (InGameUI::createMoveHint).  It moves every picture and the ground's heights
		* are logic's, so the ground under it is a grid of heights taken at the sync point and laid over
		* what it can still cover: depth tested against the hills, a plane up to 60 units across sank
		* into them. */
	enum { ORDER_MARKER_GRID = 17 };
	struct OrderMarker
	{
		Vector3 at;									///< the point, on the ground or a bridge's deck; one on water is W3DWaypointBuffer's
		Real reach;									///< the grid's half width round it; 0 on a deck, flat at at.Z
		Real ground[ ORDER_MARKER_GRID * ORDER_MARKER_GRID ];	///< [y][x], at.X - reach to at.X + reach
		UnsignedInt rgb;						///< InGameUI::orderKindColor
		UnsignedInt bornMs;
	};
	std::vector< OrderMarker > m_orderMarkers;
	Real m_orderMarkScale = 1.0f;									///< the camera's zoom at the sync point, so a marker keeps its size on the screen
	std::vector< UnsignedInt > m_orderRingColors;	///< one a ring, the order's rgb; the fade is the draw's own clock
	std::vector< UnsignedInt > m_orderRingBornMs;	///< one a ring, when it was placed
	virtual void drawFormationLine( void );				///< the line an armed left drag is spreading the selection along
	virtual void drawAttackCircle( void );				///< the circle a left drag is sweeping targets out of
	void drawGuardMarkers( void );								///< the shield over each of the local player's guarding units
	std::vector< Coord3D > m_guardMarkerTops;			///< the guarding units' tops, taken once a logic frame
	UnsignedInt m_guardMarkersFrame = ~0u;				///< the logic frame they were taken on
	const Player *m_guardMarkersPlayer = NULL;		///< and the local player they were taken for

	RenderObjClass *m_buildingPlacementAnchor;
	RenderObjClass *m_buildingPlacementArrow;

	// the move rings, EA's own
	void drawMoveHints( View *view );
	RenderObjClass *m_moveHintRenderObj[ MAX_MOVE_HINTS ];
	HAnimClass		 *m_moveHintAnim[ MAX_MOVE_HINTS ];
	Matrix3D				m_moveHintTransform[ MAX_MOVE_HINTS ];	///< each ring laid on the ground at the sync point (recordDraw)
	Bool						m_moveHintShrouded[ MAX_MOVE_HINTS ];		///< its point under black shroud then, so it is not drawn
	void takeMoveHintTransforms( void );

	// the build grid's patch, read at the sync point (recordDraw) and drawn by drawBuildGrid
	enum { BUILD_GRID_RADIUS = 28, BUILD_GRID_CELLS = BUILD_GRID_RADIUS * 2 + 1 };
	Int		m_buildGridCellX;		///< the patch's first pathfinder cell
	Int		m_buildGridCellY;
	enum { BUILD_CELL_CLEAR = 0, BUILD_CELL_OBJECT, BUILD_CELL_GROUND };	///< what a cell of the patch is washed as
	UnsignedByte m_buildGridBlocked[ BUILD_GRID_CELLS ][ BUILD_GRID_CELLS ];	///< [y][x]: red for an object, striped for the ground
	void takeBuildGrid( void );

	// recordDraw's lists, replayed by draw
	Display::DrawCalls m_overWorldDraw;		///< preDraw to the guard markers, drawn where the world is
	Display::DrawCalls m_dragBoxDraw;		///< the drag box, drawn where the pointer is
	Display::DrawCalls m_postDrawCalls;		///< postDraw
	Display::DrawCalls m_hudOverlayDraw;	///< drawHudOverlay

};  // end class W3DInGameUI

#endif  // end __W3DINGAMEUI_H_
