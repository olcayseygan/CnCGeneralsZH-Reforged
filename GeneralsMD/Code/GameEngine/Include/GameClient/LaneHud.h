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

// FILE: LaneHud.h ////////////////////////////////////////////////////////////////////////////////
//
// The Lane War HUD (docs/plans/LANE-MODE.md in the wrapper): on a lane map it takes the command bar's
// place.  The wave order and countdown at the top, the enemy plots down the right edge, the strip map
// at the bottom centre, the buy panel bottom left, money and the keys bottom right.  Drawn in code
// at the render's sync point like GroupPanel; every purchase goes out as MSG_LANEWAR_ORDER and the
// logic checks it again.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEHUD_H_
#define __LANEHUD_H_

#include "Lib/BaseType.h"

/// the HUD is up: a lane map in a match, the command bar hidden for it
Bool LaneHud_isShowing( void );

/// from InGameUI::preDraw: the placement ghost, and the command bar kept down
void LaneHud_preDraw( void );

/// from InGameUI::postDraw: the whole HUD
void LaneHud_draw( void );

/** A mouse press at `mouse` (PlaceEventTranslator, before placement and selection): TRUE when the
	* HUD took it, a click on one of its boxes, a placement, a sale or a plot unit picked up.  `right`
	* is the right button. */
Bool LaneHud_press( const ICoord2D *mouse, Bool right );

/// the left button let go after a press the HUD took: a picked up plot unit goes down there
void LaneHud_release( const ICoord2D *mouse );

/// Escape: drops a placement, a picked up unit, sell mode or the abilities panel; TRUE if it dropped one
Bool LaneHud_escape( void );

/// the command centre key (and Home): the camera to the player's own plot; FALSE off a lane map
Bool LaneHud_viewHome( void );

/** The abilities panel over the economy box: a code-built box, closed by default, toggled by the
	* Abilities key, closed by Escape or a right click.  Empty for now; LaneHud.cpp's
	* abilitiesPanelDraw and abilitiesPanelClick are where its contents go. */
Bool LaneHud_abilitiesPanelIsOpen( void );
void LaneHud_abilitiesPanelSetOpen( Bool open );

#endif // __LANEHUD_H_
