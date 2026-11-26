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

#include "GameClient/Color.h"

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

/* The game lists in the menu language (MENU-REDESIGN.md package 10, screens/game-lists/README-game-lists.md):
	 the online Custom games page and the LAN lobby draw their lists, the picked game and the heads with these,
	 every number at 1080 on the board.  The LAN list has no lock and no ping column. */
enum LobbyGameColumn
{
	LOBBY_COL_LOCK, LOBBY_COL_GAME, LOBBY_COL_MODE, LOBBY_COL_MAP, LOBBY_COL_PLAYERS, LOBBY_COL_PING, LOBBY_COL_STATE,
	LOBBY_COL_COUNT
};
/// a column's left and right edge at 1080; FALSE for a column the list does not have
Bool LobbyGameColumnSpan( Int column, Bool lan, Int &left, Int &right );
/// the column under a board x, -1 between columns or off the list
Int LobbyGameColumnAt( Int x1080, Bool lan );
enum LobbyGameState { LOBBY_STATE_OPEN, LOBBY_STATE_FULL, LOBBY_STATE_PLAYING, LOBBY_STATE_JOINING, LOBBY_STATE_OTHER_BUILD };
/// how a row reads: a join under way first, then a started game, another build, a full one
LobbyGameState LobbyGameStateOf( Bool joining, Bool playing, Bool otherBuild, Bool full );
/// a HostGameMode from a listed map's path: Direct Strike's generated islands, Apocalypse's basins, else Classic
Int LobbyMapMode( const AsciiString &map );
/// a mode's name for a row ("Classic", "Direct Strike v1.0") and a mode game's map ("Generated island")
UnicodeString LobbyModeName( Int mode );
UnicodeString LobbyMapName( const AsciiString &map );

/// a listed game as its row shows it
struct LobbyGameRow
{
	UnicodeString name;
	UnicodeString map;
	Int mode;
	Int seated;
	Int seats;
	Int ping;					///< ms, -1 unknown
	Bool password;
	LobbyGameState state;
};
/// a row of the list on its screen rectangle (LobbyDrawRows' rect); row is its index, for the text cache
void LobbyDrawGameRow( Int row, const LobbyGameRow &game, const IRegion2D &rect, Bool lan );
/** The list's head over its window, top at the board y of the head: GAMES, the count at the column's right
	 end, the column words with the sorted one green, the lit rule under them.  Under the window, the password
	 legend when lockShown; in an empty list, empty's line centred in two rows. */
void LobbyDrawGameHead( Int top1080, Bool lan, const UnicodeString &count, Int sortedColumn );
void LobbyDrawGameFoot( GameWindow *list, Bool lockShown, const UnicodeString &empty );
/// the sort a click on a column word asks: GAME by name, PING by round trip, again to turn it; TRUE when it was one
Bool LobbySortByColumn( Int column );
/// the column the online list is sorted by
Int LobbySortedColumn( void );

/// a seat of the picked game
struct LobbySeat
{
	UnicodeString name;
	UnicodeString army;			///< empty where the listing does not say
	UnicodeString record;		///< "88-41", empty for none
	Color color;
	const Image *portrait;
	Bool known;							///< the seat's colour and army are known (LAN); online lists names only
};
/// the picked game as the right column shows it
struct LobbyPicked
{
	UnicodeString name;
	UnicodeString map;
	UnicodeString line;			///< "Classic · Host DesertFox"
	Bool password;
	Int seated;
	Int seats;
	Int mode;
	const Image *preview;		///< the map's, NULL for a mode game
	Int seatCount;
	enum { SEATS = 32 };		///< MAX_SLOTS, which NetworkDefs.h holds
	LobbySeat seat[ SEATS ];
};
/** The picked game between left and right from top (board): its head, the map, its words and the seats down
	 to bottom.  lan stands the picture large over the words; online it is a thumbnail beside them, with the
	 records' legend. */
void LobbyDrawPicked( const LobbyPicked &game, Int left, Int top, Int right, Int bottom, Bool lan );
/// a section head at the board's left, top: words in 16 px bold tracked capitals, ACTIVE, and count at right in MUTED
void LobbyDrawSection( const UnicodeString &words, const UnicodeString &count, Int left, Int top, Int right, Int slot );
/// UIStyle::setMenuKey, the key's word in capitals as the designs letter them (EA's labels are not)
void LobbyMenuKey( GameWindow *key, Bool primary );
/// a text entry in the language: black under a 1 px FRAME line, cyan under the mouse, Arial at px1080
void LobbyMenuEntry( GameWindow *entry, Int px1080, Color text );
/// a chat log on a faint lift, no edge, its lines at px1080
void LobbyMenuChat( GameWindow *list, Int px1080 );
/// the text cache's 32 slots for a box drawn over a list screen (Host Game, Join Game, Direct Connect)
enum { LOBBY_TEXT_BOX = 256 };
/** A box of the message box's pattern on its screen rectangle: UIStyle::boxShell with title, the question after
	 the title on its line, the foot rule and the hint at the foot's left.  Uses LOBBY_TEXT_BOX to + 2. */
void LobbyDrawBoxHead( const IRegion2D &box, const UnicodeString &title, const UnicodeString &question, const UnicodeString &hint );
/// a box's field label, 14 px bold tracked capitals, from x centred on middle (screen)
void LobbyDrawBoxLabel( const UnicodeString &words, Int x, Int middle );
/// a screen rectangle for a board rectangle, set on a window inside its parent
void LobbyPlaceOnBoard( GameWindow *window, Int left, Int top, Int right, Int bottom );
/// words at a screen point from the lists' text cache (slot 0 to 15 is the screen's, LOBBY_TEXT_BOX on the
/// box's); right aligns their end there
void LobbyDrawWords( Int slot, const UnicodeString &text, Int px1080, Bool bold, Int x, Int middle, Color color, Bool right = FALSE );
/// frees the lists' text cache, from a screen's shutdown
void LobbyFreeText( void );

#endif // __LOBBYUTILS_H__
