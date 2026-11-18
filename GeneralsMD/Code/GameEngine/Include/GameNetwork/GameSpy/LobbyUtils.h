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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: LobbyUtils.h //////////////////////////////////////////////////////
// Generals lobby utils
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#ifndef __LOBBYUTILS_H__
#define __LOBBYUTILS_H__

class GameWindow;
class DisplayString;
class Image;
class GameSpyStagingRoom;

GameWindow *GetGameListBox( void );
GameWindow *GetGameInfoListBox( void );
NameKeyType GetGameListBoxID( void );
NameKeyType GetGameInfoListBoxID( void );
void GrabWindowInfo( void );
void ReleaseWindowInfo( void );
void RefreshGameInfoListBox( GameWindow *mainWin, GameWindow *win );
void RefreshGameListBoxes( void );
void ToggleGameListType( void );

void playerTemplateComboBoxTooltip(GameWindow *wndComboBox, WinInstanceData *instData, UnsignedInt mouse);
void playerTemplateListBoxTooltip(GameWindow *wndListBox, WinInstanceData *instData, UnsignedInt mouse);

enum GameSortType
{
	GAMESORT_ALPHA_ASCENDING = 0,
	GAMESORT_ALPHA_DESCENDING,
	GAMESORT_PING_ASCENDING,
	GAMESORT_PING_DESCENDING,
	GAMESORT_MAX,
};

Bool HandleSortButton( NameKeyType sortButton );
void PopulateLobbyPlayerListbox(void);

/* The custom game lobby's lists in the online pages' look (ONLINE-UI-REDESIGN.md, "Custom games").  A
	 page draws a list's rows itself from its cells: LobbyDrawRows walks the rows the listbox would show,
	 draws the selection in the list's own hilite art and hands each row's screen rectangle to draw, with
	 the display clipped to clip (the list's inside), so the listbox's clicks, scrolling and tooltips
	 land on what is drawn.  litRow is drawn in the selection's art without being selected, which a
	 list whose selection means something (the players list's whisper targets) needs. */
typedef void (*LobbyRowDraw)( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip );
void LobbyDrawRows( GameWindow *list, LobbyRowDraw draw, Int litRow = -1 );
enum LobbyAlign { LOBBY_ALIGN_LEFT, LOBBY_ALIGN_CENTER, LOBBY_ALIGN_RIGHT };
/// a cell of a row inside its column: its text in its colour and the list's font, or its image at the
/// height of a ping light, centred on the row
void LobbyDrawCell( GameWindow *list, Int row, Int column, const IRegion2D &rect, const IRegion2D &clip, LobbyAlign align );
/// the left edge and width of a column, from the list's left edge x
void LobbyColumnSpan( GameWindow *list, Int x, Int column, Int &left, Int &width );
/// gives an empty list this many columns at these percentages of its width
void LobbySetColumns( GameWindow *list, Int columns, const Int *percent );
/// the server's name from its welcome ("EU1"), else the -online host short: "eu1.example.org" is EU1;
/// empty without -online
UnicodeString LobbyServerLabel( void );
/// the game a join is out for, whose row reads Joining; 0 for none
void LobbySetJoiningGame( Int gameID );
/** Every seat the host offers is taken, by the numbers the listing reports (the host's np, no and mp, which
	 the count column prints), not by counting the listed seats against the map: a build before the game
	 list's names and profile IDs were packed together (GameSpyInfo::setGameOptions) listed an open seat
	 as a computer. */
Bool LobbyGameFull( const GameSpyStagingRoom *game );

#endif // __LOBBYUTILS_H__
