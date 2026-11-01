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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: LookAtXlat.h ///////////////////////////////////////////////////////////
// Author: Steven Johnson, Dec 2001

#pragma once

#ifndef _H_LookAtXlat
#define _H_LookAtXlat

#include "GameClient/InGameUI.h"

//-----------------------------------------------------------------------------
class LookAtTranslator : public GameMessageTranslator
{
public:
	LookAtTranslator();
	~LookAtTranslator();
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg);
	/// where the right-button pan was started from, or NULL if no such pan is running
	const ICoord2D* getScrollAnchor( void );
	/// a right drag is panning the camera, so the release that ends it is not a click
	Bool isRightDragPanning( void ) const { return m_isScrolling && m_scrollType == SCROLL_RMB; }
	Bool hasMouseMovedRecently( void );
	/// scrolling, turning or tilting the camera by hand, with the mouse or the keys, right now
	Bool isMovingCamera( void ) const { return m_isScrolling || m_isRotating || m_isPitching; }
	void setCurrentPos( const ICoord2D& pos );
	/// one step of the held scroll; the frame tick's, and a picture's drawn beside a tick (LOGIC-THREAD.md phase 4)
	void scrollView( void );
	/// Local device gesture, never a gameplay command. Pixel is the pointer in game pixels;
	/// pan is the desired content displacement in those pixels, scale > 1 magnifies the view.
	/// Zero displacement and scale 1 only query eligibility. The input device owns gesture phases.
	Bool applyCameraGesture( const ICoord2D &pixel, Real panX, Real panY, Real zoomScale );

	void resetModes(); //Used when disabling input, so when we reenable it we aren't stuck in a mode.

private:
	enum
	{
		MAX_VIEW_LOCS = 8
	};
	// a right click gives an order and a right drag pans; the middle button turns the camera
	enum
	{
		SCROLL_NONE = 0,
		SCROLL_KEY,
		SCROLL_SCREENEDGE,
		SCROLL_RMB				// right-button drag pan
	};
	ICoord2D m_anchor;
	ICoord2D m_originalAnchor;
	ICoord2D m_currentPos;
	Bool m_rightPanArmed;			// the right button is down and has not yet moved far enough to be a pan
	Bool m_isScrolling;				// set to true if we are in the act of RMB scrolling
	Bool m_isRotating;					// set to true if we are in the act of MMB rotating
	Real m_freeRotateAngle;		// heading the drag has asked for, before SnapCameraRotateTo45 quantizes it
	Bool m_isPitching;					// set to true if we are in the act of ALT pitch rotation
	Bool m_isChangingFOV;			// set to true if we are in the act of changing the field of view
	UnsignedInt m_timestamp;				// set when button goes down
	DrawableID m_lastPlaneID;
	ViewLocation m_viewLocation[ MAX_VIEW_LOCS ];
	Int m_scrollType;
	void setScrolling( Int );
	void stopScrolling( void );
	Bool networkCameraDue( const ViewLocation &view );
	UnsignedInt m_lastMouseMoveFrame;
	UnsignedInt m_cameraSentFrame;		///< the logic frame this player's camera last went to the other machines
	ViewLocation m_cameraSent;				///< and where it was then
};	

extern LookAtTranslator *TheLookAtTranslator;

/** A view pitch kept where the player's own tilt (Alt+middle drag, Alt+wheel) may put it: the
	* camera between 20 and 60 degrees above the ground. */
extern Real LookAt_clampPlayerPitch( Real pitch );

// EDGE SCROLL ////////////////////////////////////////////////////////////////////////////////////
// The map scrolls while the pointer is in a band along the screen's edges, faster the deeper in
// it goes.  Free functions so the arithmetic can be tested without a display.

/** How many pixels in from each edge the band reaches on a display this tall: three percent of
	* the height, and never less than the three pixels the game shipped with. */
extern Int EdgeScroll_bandForHeight( Int displayHeight );

/** The share of the full scroll speed at this many pixels from an edge: nothing outside the band,
	* all of it on the last three pixels, a straight ramp between. */
extern Real EdgeScroll_strength( Int distanceFromEdge, Int band );

#endif
