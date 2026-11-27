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

// FILE: MapPicker.h //////////////////////////////////////////////////////////////////////////////
//
// The rooms' map pickers in the menu language (MENU-REDESIGN.md package 7, the design in
// experiments/proofs/2026-11-24/screens/pickers/README-pickers.md): the map list and the Random
// tab's map type list drawn as grids of map pictures over their own listboxes, which stay the model
// (rows, item data, the selection), the picked map's words beside the preview, the tabs with their
// counts and the foot's legend and hint.  The skirmish, LAN and online pickers share it; the layout is
// Tools/mapselect_layout.py's.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _MAPPICKER_H_
#define _MAPPICKER_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

#include <vector>

class GameWindow;
class GameInfo;
class WinInstanceData;
class MapMetaData;

/** A picker's init, after it found its windows and before it fills its list: the list becomes the
	* grid, the preview takes each map's shape, Cancel and Accept become the foot's keys.  minPlayers
	* is what the picker fills its lists with (past eight seats only the maps that hold them); skirmish
	* lists the user's single player maps on Unofficial too and shows the medals. */
void MapPickerInit( const char *layoutFilename, GameWindow *parent, GameWindow *listbox, GameWindow *preview,
	const GameInfo *room, Int minPlayers, Bool skirmish );
/// the Random tab's map type list as the same grid, its two heads in the menu's capitals
void MapPickerAttachTypes( GameWindow *listbox, GameWindow *sizeHead, GameWindow *typeHead );
/// the picker is going: forget its windows and free its words
void MapPickerShutdown( void );
/// FitLobbyRoomMap's turn for the picker's preview: TRUE when window is it, placed at map's shape
Bool MapPickerFitPreview( GameWindow *window, const MapMetaData *map );
/// a tab of the picker (Official, Unofficial, Random) or a size box: the menu's tab, a list tab's
/// map count after its name, the Random tab's reason while it is off
void MapPickerDrawTab( GameWindow *window, WinInstanceData *instData );

/// a grid's shape on the 1080 board: columns of tiles across left..right, rows of them on screen
struct MapPickerShape
{
	Int columns;
	Int tileWidth;
	Int tileHeight;
	Int rows;
};
const Int MAP_PICKER_GAP = 8;					///< between tiles
const Int MAP_PICKER_INSET = 6;				///< a tile's picture inside it
const Int MAP_PICKER_NAME_ROOM = 50;	///< two lines of name under the picture
/** The design's rule: of four column counts from startColumns the one whose square pictures, with
	* their two name lines, leave the least height over wins, and the rows then share that remainder. */
MapPickerShape MapPickerGridShape( Int left, Int right, Int top, Int bottom, Int startColumns );
/// the first row on screen as a list opens: the picked row's row the last one shown, or the top
Int MapPickerFirstRow( Int picked, Int columns, Int rows, Int total );
/// a tile on the 1080 board and the list row it shows
struct MapPickerTile
{
	Int left, top, right, bottom;
	Int row;
};
/** The tiles on screen of a list of total rows from its first row of tiles, the grid's area from top to
	* the seat column's foot: the cells the grid draws and clicks, through its own placing. */
void MapPickerPlaceTiles( Int total, Int first, Int top, Int startColumns, std::vector< MapPickerTile > &tiles );

// The Random tab's state for the grid, kept in GUIUtil.cpp with the tab
Bool RandomMapTabIsOpen( void );
/// the shipped map a type list row stands for, empty for Surprise me
AsciiString RandomMapTabTemplateAt( Int row );
AsciiString RandomMapTabTemplate( void );
Int RandomMapTabSize( void );
/// the players a generated map is built for, the room's seated players, two to eight
Int RandomMapTabPlayers( void );
/// the picker's tabs: 0 Official, 1 Unofficial, 2 Random; NULL while no picker is up
GameWindow *RandomMapTabButton( Int which );

#endif // _MAPPICKER_H_
