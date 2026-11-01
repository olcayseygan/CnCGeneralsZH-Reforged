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

// FILE: FactionPicker.h //////////////////////////////////////////////////////////////////////////
//
// The staging rooms' army screen.  A click on a seat's army box opens it instead of the box's own
// list: every army the box lists, grouped under its side, a large portrait and what the army does,
// and a Random whose pool the player can tick by hand.  The pick goes back through the box, so each
// room's own handling of a new army (a skirmish seat, a LAN or online request to the host) runs as
// it did for the list.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _FACTIONPICKER_H_
#define _FACTIONPICKER_H_

#include "Lib/BaseType.h"

class GameWindow;

/// every seat's ComboBoxPlayerTemplate<n> in this room opens the screen when clicked
void FactionPickerAttach( GameWindow *parent, const char *layoutFilename );
/// the room is going away: an open screen goes with it
void FactionPickerDetach( void );
/// the box's Random line carries the seat's own Random, a pool included, so the box shows it and
/// the screen opens on it
void FactionPickerSyncRandom( GameWindow *combo, Int pick );
/// -factionpicker: the next room that attaches opens the screen on its first seat, custom shows the
/// pool, and a side's name (America, China, GLA, Turkiye) narrows the pool to that side as a click on
/// its heading does
void FactionPickerOpenOnStart( Bool custom, const char *side );

/// a side's tick box is ticked while every army of the side (its bits) is in the pool
Bool FactionPickerSideTicked( UnsignedInt pool, UnsignedInt side );
/// a click on a side's tick box: the whole side in, or out when it was all in already
UnsignedInt FactionPickerToggleSide( UnsignedInt pool, UnsignedInt side );
/// the army list's bar, in pixels: where the thumb stands below the track's top at a scroll
Int FactionPickerThumbTop( Int scroll, Int scrollMax, Int track, Int thumb );
/// the scroll a thumb held at thumbTop means, reaching both ends of the list however it rounds
Int FactionPickerScrollAtThumb( Int thumbTop, Int scrollMax, Int track, Int thumb );

#endif // _FACTIONPICKER_H_
