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

// FILE: LaneWarEndScreen.h ///////////////////////////////////////////////////////////////////////
//
// A Lane War match's own end, the owner's concept B ("the hand"), in place of the stock Victory and
// Defeat windows and the score screen, which a lane match no longer shows (LaneWarStats.h has the
// logic's side of it).  Three steps over the running world:
//
//   the finale, while the beaten fortress comes down (LANEWAR_END_COLLAPSE): the camera goes to it
//     and the edges darken;
//   the result, once the match is decided (LANEWAR_END_OVER): the world dims and the result card is
//     dealt over two card backs, the general, the word and the match's length;
//   the results screen, a couple of seconds later or on a click: a tall card for every seat with
//     its general, its best unit and its seven numbers, the timeline of the middle and the fortresses,
//     and the keys (back to menu; rematch and watch replay drawn but not wired yet).
//
// Client only: it copies what it shows from the logic at the render's sync point.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LANEWARENDSCREEN_H_
#define __LANEWARENDSCREEN_H_

#include "Lib/BaseType.h"

/// at the render's sync point (InGameUI::preDraw): the match's end and every seat's record
void LaneWarEndScreen_preDraw( void );

/// over the HUD (InGameUI::draw)
void LaneWarEndScreen_draw( void );

/// the result or the results screen is up: the match is decided
Bool LaneWarEndScreen_isShowing( void );

/** Wall seconds into the finale, while the beaten fortress comes down; -1 outside it.  For the HUD,
	* whose hand lifts away card by card over it (README-end.md, B's sequence). */
Real LaneWarEndScreen_finale( void );

/** A mouse press at `mouse` (PlaceEventTranslator, before the HUD): TRUE whenever the match is decided,
	* which takes every press.  A left press opens the results screen, and on it Back to menu leaves. */
Bool LaneWarEndScreen_press( const ICoord2D *mouse, Bool right );

#endif // __LANEWARENDSCREEN_H_
