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
// place, as the owner's card hand of 2026-11-21.  Money, the strip map and the wave clock in one plate
// at the top centre; along the bottom the tier keys, the hero's card, the tier's units as cards, the
// upgrades deck and the abilities key.  No words: numbers, pictures and colour, names in tooltips.
// Drawn in code at the render's sync point like GroupPanel; every purchase goes out as
// MSG_LANEWAR_ORDER and the logic checks it again.
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
	* HUD took it, a click on one of its boxes, a card picked up, a placement or a plot unit picked up.
	* `right` is the right button, which drops whatever is held. */
Bool LaneHud_press( const ICoord2D *mouse, Bool right );

/** The left button let go after a press the HUD took: a card dragged onto the plot is bought there,
	* a plot unit dragged to another slot moves, one dragged back onto the hand is sold. */
void LaneHud_release( const ICoord2D *mouse );

/// Escape: drops a card, a picked up unit and the open panels; TRUE if it dropped one
Bool LaneHud_escape( void );

/// the command centre key (and Home): the camera to the player's own plot; FALSE off a lane map
Bool LaneHud_viewHome( void );

/// Tab down or up (MetaEventTranslator): while held, every other seat's income and plot down the right edge
void LaneHud_holdScoreboard( Bool held );

/** The abilities panel over the abilities key: one row a roster unit's button the director presses,
	* its switch sending LANEWAR_ORDER_ABILITY.  Closed by default, by Escape and by a right click. */
Bool LaneHud_abilitiesPanelIsOpen( void );
void LaneHud_abilitiesPanelSetOpen( Bool open );

/// the camera turned side-on to the lane, the team's own end on the left (the owner's rule of 2026-11-21)
void LaneHud_turnToLane( Int team );

/** For a script driving the HUD with the mouse (the control socket's `lanehud`): JSON members naming
	* the local player's team and seat, the jets the strip map last showed in the air, the screen point
	* at the middle of his slot (grid, column, row), where the camera
	* stands now, and the chips of the row the last draw raised over a plot unit, each as
	* [kind, x, y, w, h] with kind 0 rank, 1 upgrade, 2 passenger, 3 room.  Client only. */
void LaneHud_describe( Int grid, Int col, Int row, char *out, Int size );

#endif // __LANEHUD_H_
