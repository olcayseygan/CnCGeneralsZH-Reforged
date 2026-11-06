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
	virtual void drawBuildGrid( void );						///< the pathfinder's build grid under a pending structure
	virtual void drawAttackCircleFill( void );		///< the wash on the ground inside an attack circle
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
	void takeMoveHintTransforms( void );

	// the build grid's patch, read at the sync point (recordDraw) and drawn by drawBuildGrid
	enum { BUILD_GRID_RADIUS = 28, BUILD_GRID_CELLS = BUILD_GRID_RADIUS * 2 + 1 };
	Int		m_buildGridCellX;		///< the patch's first pathfinder cell
	Int		m_buildGridCellY;
	Bool	m_buildGridBlocked[ BUILD_GRID_CELLS ][ BUILD_GRID_CELLS ];	///< [y][x], washed red
	void takeBuildGrid( void );

	// recordDraw's lists, replayed by draw
	Display::DrawCalls m_overWorldDraw;		///< preDraw to the guard markers, drawn where the world is
	Display::DrawCalls m_dragBoxDraw;		///< the drag box, drawn where the pointer is
	Display::DrawCalls m_postDrawCalls;		///< postDraw
	Display::DrawCalls m_hudOverlayDraw;	///< drawHudOverlay

};  // end class W3DInGameUI

#endif  // end __W3DINGAMEUI_H_
