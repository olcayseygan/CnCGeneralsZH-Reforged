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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: LobbyUtils.cpp
// Author: Matthew D. Campbell, Sept 2002
// Description: GameSpy lobby utils
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/version.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/UIStyle.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Image.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Mouse.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameInfo.h"

#include "GameClient/LanguageFilter.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/GUIUtil.h"

#include "Common/STLTypedefs.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
/* The game list's eight cells, the count the layout gives it.  The page draws them (drawGameList): name,
	 map, seats, a lock for a password, the host's ping light and milliseconds, and the row's chip.  The
	 light's image is as tall as a row, which is what makes a row as tall as the mockup's. */
enum {
	COLUMN_NAME = 0,
	COLUMN_MAP,
	COLUMN_NUMPLAYERS,
	COLUMN_PASSWORD,
	COLUMN_LIGHT,
	COLUMN_PING,
	COLUMN_STATE,
	COLUMN_UNUSED,
	GAME_COLUMNS
};
// at 1080, of the list's width less its scroll bar: the mockup's 390, 270, 120, 120 and 156
static const Int GAME_COLUMN_PERCENT[ GAME_COLUMNS ] = { 36, 25, 11, 3, 3, 8, 14, 0 };
static const Real GAME_ROW_HEIGHT = 40.0f;		///< the design's, at 1080
static const Real CELL_PAD = 10.0f;

static NameKeyType buttonSortAlphaID = NAMEKEY_INVALID;
static NameKeyType buttonSortPingID = NAMEKEY_INVALID;
static NameKeyType buttonSortBuddiesID = NAMEKEY_INVALID;
static NameKeyType windowSortAlphaID = NAMEKEY_INVALID;
static NameKeyType windowSortPingID = NAMEKEY_INVALID;
static NameKeyType windowSortBuddiesID = NAMEKEY_INVALID;

static GameWindow *buttonSortAlpha = NULL;
static GameWindow *buttonSortPing = NULL;
static GameWindow *buttonSortBuddies = NULL;
static GameWindow *windowSortAlpha = NULL;
static GameWindow *windowSortPing = NULL;
static GameWindow *windowSortBuddies = NULL;

static GameSortType theGameSortType = GAMESORT_ALPHA_ASCENDING;
static Bool sortBuddies = TRUE;
static void showSortIcons(void)
{
	if (windowSortAlpha && windowSortPing)
	{
		switch(theGameSortType)
		{
			case GAMESORT_ALPHA_ASCENDING:
				windowSortAlpha->winHide(FALSE);
				windowSortAlpha->winEnable(TRUE);
				windowSortPing->winHide(TRUE);
				break;
			case GAMESORT_ALPHA_DESCENDING:
				windowSortAlpha->winHide(FALSE);
				windowSortAlpha->winEnable(FALSE);
				windowSortPing->winHide(TRUE);
				break;
			case GAMESORT_PING_ASCENDING:
				windowSortPing->winHide(FALSE);
				windowSortPing->winEnable(TRUE);
				windowSortAlpha->winHide(TRUE);
				break;
			case GAMESORT_PING_DESCENDING:
				windowSortPing->winHide(FALSE);
				windowSortPing->winEnable(FALSE);
				windowSortAlpha->winHide(TRUE);
				break;
		}
	}

	if (sortBuddies)
	{
		if (windowSortBuddies)
		{
			windowSortBuddies->winHide(FALSE);
		}
	}
	else
	{
		if (windowSortBuddies)
		{
			windowSortBuddies->winHide(TRUE);
		}
	}
}
void setSortMode( GameSortType sortType ) { theGameSortType = sortType; showSortIcons(); RefreshGameListBoxes(); }
void sortByBuddies( Bool doSort ) { sortBuddies = doSort; showSortIcons(); RefreshGameListBoxes(); }

Bool HandleSortButton( NameKeyType sortButton )
{
	if (sortButton == buttonSortBuddiesID)
	{
		sortByBuddies( !sortBuddies );
		return TRUE;
	}
	else if (sortButton == buttonSortAlphaID)
	{
		if (theGameSortType == GAMESORT_ALPHA_ASCENDING)
		{
			setSortMode(GAMESORT_ALPHA_DESCENDING);
		}
		else
		{
			setSortMode(GAMESORT_ALPHA_ASCENDING);
		}
		return TRUE;
	}
	else if (sortButton == buttonSortPingID)
	{
		if (theGameSortType == GAMESORT_PING_ASCENDING)
		{
			setSortMode(GAMESORT_PING_DESCENDING);
		}
		else
		{
			setSortMode(GAMESORT_PING_ASCENDING);
		}
		return TRUE;
	}
	return FALSE;
}

// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
//static NameKeyType parentGameListSmallID = NAMEKEY_INVALID;
static NameKeyType parentGameListLargeID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyGamesSmallID = NAMEKEY_INVALID;
static NameKeyType listboxLobbyGamesLargeID = NAMEKEY_INVALID;
//static NameKeyType listboxLobbyGameInfoID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = NULL;
//static GameWindow *parentGameListSmall = NULL;
static GameWindow *parentGameListLarge = NULL;
       //GameWindow *listboxLobbyGamesSmall = NULL;
       GameWindow *listboxLobbyGamesLarge = NULL;
       //GameWindow *listboxLobbyGameInfo = NULL;

/// A listed room's map as the list names it: the mode for a Direct Strike lane map, which nobody picked
/// and nobody has before he builds it from its name at load, the cache's name for a map this machine
/// has, else the file's.
static UnicodeString roomMapName( const AsciiString &map )
{
	UnicodeString mapName;
	const MapMetaData *md = TheMapCache->findMap(map);
	// the list's path is GameSpy's portable one, '/' for '\' (GameSpyInfo::setGameOptions): its leaf names the map
	const char *leaf = strrchr(map.str(), '/');
	if (IsDirectStrikeMap(leaf ? AsciiString(leaf + 1) : map))
		mapName = ModeNameWithVersion("GUI:DirectStrikeLower", "GUI:DirectStrikeVersion");
	else if (IsApocalypseMap(map))
		mapName = ModeNameWithVersion("GUI:ApocalypseLower", "GUI:ApocalypseVersion");
	else if (IsAodMap(map))
		mapName = TheGameText->fetch("GUI:RandomMapAod");
	else if (md)
		mapName = md->m_displayName;
	else
	{
		const char *start = map.reverseFind('\\');
		mapName.translate( start ? start + 1 : map.str() );
	}
	return mapName;
}

static void gameTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString);//TheGameText->fetch("TOOLTIP:GamesBeingFormed") );
		return;
	}

	Int gameID = (Int)(intptr_t)GadgetListBoxGetItemData(window, row, 0);
	GameSpyStagingRoom *room = TheGameSpyInfo->findStagingRoomByID(gameID);
	if (!room)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:UnknownGame") );
		return;
	}

	// the row is drawn at the board's columns, not the listbox's: the column under the pointer by those
	const UIStyle::BoardFit fit = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() );
	const Int boardX = fit.scale > 0.0f ? REAL_TO_INT( ( x - fit.x ) / fit.scale ) : x;
	const Int column = LobbyGameColumnAt( boardX, FALSE );
	if (column == LOBBY_COL_PING)
	{
#if 0 //def DEBUG_LOGGING
		UnicodeString s;
		s.format(u"Ping is %d ms (cutoffs are %d ms and %d ms\n%hs local pings\n%hs remote pings",
			room->getPingAsInt(), TheGameSpyConfig->getPingCutoffGood(), TheGameSpyConfig->getPingCutoffBad(),
			TheGameSpyInfo->getPingString().str(), room->getPingString().str()
		);
		TheMouse->setCursorTooltip( s, 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
#else
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:PingInfo"), 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
#endif
		return;
	}
	if (column == LOBBY_COL_PLAYERS)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:NumberOfPlayers"), 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
		return;
	}
	if (column == LOBBY_COL_LOCK)
	{
		if (room->getHasPassword())
		{
			UnicodeString checkTooltip =TheGameText->fetch("TOOTIP:Password");
			if(!checkTooltip.compare(u"Password required to joing game"))
				checkTooltip.set(u"Password required to join game");
			TheMouse->setCursorTooltip( checkTooltip, 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
		}
		else
			TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}

	UnicodeString tooltip;

	const UnicodeString mapName = roomMapName(room->getMap());
	UnicodeString tmp;
	tooltip.format(TheGameText->fetch("TOOLTIP:GameInfoGameName"), room->getGameName().str());
	if (room->getLadderPort() != 0)
	{
		const LadderInfo *linfo = TheLadderList->findLadder(room->getLadderIP(), room->getLadderPort());
		if (linfo)
		{
			tmp.format(TheGameText->fetch("TOOLTIP:GameInfoLadderName"), linfo->name.str());
			tooltip.concat(tmp);
		}
	}
	if (room->getExeCRC() != TheGlobalData->m_exeCRC || room->getIniCRC() != TheGlobalData->m_iniCRC)
	{
		tmp.format(TheGameText->fetch("TOOLTIP:InvalidGameVersion"), mapName.str());
		tooltip.concat(tmp);
	}
	tmp.format(TheGameText->fetch("TOOLTIP:GameInfoMap"), mapName.str());
	tooltip.concat(tmp);

	AsciiString aPlayer;
	UnicodeString player;
	Int numPlayers = 0;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		GameSpyGameSlot *slot = room->getGameSpySlot(i);
		if (i == 0 && (!slot || !slot->isHuman()))
		{
			DEBUG_CRASH(("About to tooltip a non-hosted game!\n"));
		}
		if (slot && slot->isHuman())
		{
			tmp.format(TheGameText->fetch("TOOLTIP:GameInfoPlayer"), slot->getName().str(), slot->getWins(), slot->getLosses());
			tooltip.concat(tmp);
			++numPlayers;
		}
		else if (slot && slot->isAI())
		{
			++numPlayers;
			tooltip.concat(u'\n');
			tooltip.concat(SlotStateName(slot->getState(), slot->getAIRating()));
		}
	}
	DEBUG_ASSERTCRASH(numPlayers, ("Tooltipping a 0-player game!\n"));

	TheMouse->setCursorTooltip( tooltip, 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
}

//-------------------------------------------------------------------------------------------------
// The lists drawn in the online pages' look
//-------------------------------------------------------------------------------------------------
static const Real LIGHT_SIZE = 16.0f;		///< a ping light or a lock in a row, at 1080

static Int s_joiningGame = 0;

/* The lists' words: one DisplayString a slot, kept between frames, so a word that does not change is not
	 rendered again.  0 to 15 are the screens', 16 to 63 the picked game's, 64 to 255 the rows', 8 a row, and
	 LOBBY_TEXT_BOX on a box's over the screen, which draws in the same frame. */
enum { LOBBY_TEXTS = LOBBY_TEXT_BOX + 32, LOBBY_TEXT_PICKED = 16, LOBBY_TEXT_ROWS = 64, LOBBY_ROW_TEXTS = 8 };
static DisplayString *s_lobbyText[ LOBBY_TEXTS ];
static UnicodeString s_lobbySource[ LOBBY_TEXTS ];	///< drawFitted's words before the cut
static Int s_lobbyWidest[ LOBBY_TEXTS ];

static DisplayString *lobbyWords( Int slot, const UnicodeString &text, GameFont *font )
{
	slot = ( slot % LOBBY_TEXTS + LOBBY_TEXTS ) % LOBBY_TEXTS;
	DisplayString *&s = s_lobbyText[ slot ];
	if (s == NULL)
		s = TheDisplayStringManager->newDisplayString();
	if (s->getFont() != font)
		s->setFont( font );
	if (s->getText().compare( text ) != 0)
	{
		s->setText( text );
		s_lobbySource[ slot ].clear();
	}
	return s;
}

void LobbyDrawWords( Int slot, const UnicodeString &text, Int px1080, Bool bold, Int x, Int middle, Color color, Bool right )
{
	if (text.isEmpty())
		return;
	DisplayString *s = lobbyWords( slot, text, UIStyle::menuFont( px1080, bold ) );
	Int w, h;
	s->getSize( &w, &h );
	s->draw( right ? x - w : x, middle - h / 2, color, UIStyle::withAlpha( GameMakeColor( 0, 0, 0, 255 ), 160 ) );
}

/// words cut to a width with an ellipsis (a name never shortens otherwise), at a board point
static void drawFitted( Int slot, const UnicodeString &text, Int px1080, Bool bold, Int x, Int middle, Int widest, Color color )
{
	if (text.isEmpty())
		return;
	GameFont *font = UIStyle::menuFont( px1080, bold );
	slot = ( slot % LOBBY_TEXTS + LOBBY_TEXTS ) % LOBBY_TEXTS;
	DisplayString *s = s_lobbyText[ slot ];
	// measuring the cut is the dear part: done again only when the words, the width or the face change
	if (s == NULL || s->getFont() != font || s_lobbyWidest[ slot ] != widest || s_lobbySource[ slot ].compare( text ) != 0)
	{
		s = lobbyWords( slot, UIStyle::fitName( text, font, max( widest, 1 ) ), font );
		s_lobbySource[ slot ] = text;
		s_lobbyWidest[ slot ] = widest;
	}
	Int w, h;
	s->getSize( &w, &h );
	s->draw( x, middle - h / 2, color, UIStyle::withAlpha( GameMakeColor( 0, 0, 0, 255 ), 160 ) );
}

void LobbyFreeText( void )
{
	for (Int i = 0; i < LOBBY_TEXTS; ++i)
	{
		if (s_lobbyText[ i ])
			TheDisplayStringManager->freeDisplayString( s_lobbyText[ i ] );
		s_lobbyText[ i ] = NULL;
		s_lobbySource[ i ].clear();
	}
}

void LobbySetJoiningGame( Int gameID )
{
	s_joiningGame = gameID;
}

Bool LobbyGameFull( const GameSpyStagingRoom *game )
{
	const Int seats = game->getReportedMaxPlayers();
	return seats > 0 && game->getReportedNumPlayers() + game->getReportedNumObservers() >= seats;
}

UnicodeString LobbyServerLabel( void )
{
	// the name the server gives itself in its welcome, which the site may write in UTF-8
	AsciiString name, city;
	Int pingMs;
	if (OnlineGetServerInfo( name, city, pingMs ) && name.isNotEmpty())
	{
		WideChar text[ 64 ];
		WideCharFromUtf8( name.str(), text, sizeof( text ) / sizeof( text[ 0 ] ) );
		return UnicodeString( text );
	}

	// else a host name by its first label in capitals, an address whole
	const AsciiString host = TheGlobalData->m_onlineHost;
	Bool named = FALSE;
	for (const char *c = host.str(); *c; ++c)
		named = named || isalpha( (unsigned char)*c );
	AsciiString label = host;
	if (named && strchr( host.str(), '.' ))
	{
		AsciiString rest = host, first;
		rest.nextToken( &first, "." );
		label.clear();
		for (const char *c = first.str(); *c; ++c)
			label.concat( (char)toupper( (unsigned char)*c ) );
	}
	UnicodeString wide;
	wide.translate( label );
	return wide;
}

/// the picked row: an EDGE wash and a lit 1 px EDGE_LIT edge round it
static void drawSelection( GameWindow *list, Int left, Int top, Int right, Int bottom )
{
	(void)list;
	TheDisplay->drawFillRect( left, top, right - left, bottom - top, UIStyle::withAlpha( UIStyle::EDGE, 70 ) );
	IRegion2D edge;
	edge.lo.x = left;
	edge.lo.y = top;
	edge.hi.x = right - 1;
	edge.hi.y = bottom - 1;
	UIStyle::litEdge( edge, UIStyle::EDGE_LIT );
}

void LobbyDrawRows( GameWindow *list, LobbyRowDraw draw, Int litRow )
{
	ListboxData *data = list ? (ListboxData *)list->winGetUserData() : NULL;
	if (!data || !data->listData || !draw)
		return;
	Int x, y, width, height;
	list->winGetScreenPosition( &x, &y );
	list->winGetSize( &width, &height );
	if (data->slider && !data->slider->winIsHidden())
	{
		Int sliderWidth, sliderHeight;
		data->slider->winGetSize( &sliderWidth, &sliderHeight );
		width -= sliderWidth + 3;
	}
	IRegion2D clip;
	clip.lo.x = x + 1;
	clip.lo.y = y + 1;
	clip.hi.x = x + width - 1;
	clip.hi.y = y + height - 1;

	// rows where W3DListBox puts them: four pixels in, scrolled by displayPos, a pixel between two
	Int top = y + 4 - data->displayPos;
	for (Int row = 0; row < data->endPos && top < clip.hi.y; ++row)
	{
		IRegion2D rect;
		rect.lo.x = x;
		rect.lo.y = top;
		rect.hi.x = x + width;
		rect.hi.y = top + data->listData[row].height + 1;
		top = rect.hi.y;
		if (rect.hi.y <= clip.lo.y)
			continue;

		Bool selected = FALSE;
		if (data->multiSelect)
		{
			for (Int i = 0; data->selections && i < data->listLength && data->selections[i] >= 0 && !selected; ++i)
				selected = data->selections[i] == row;
		}
		else
			selected = data->selectPos == row;

		TheDisplay->setClipRegion( &clip );
		// the list's states: the local player's own row gold, the picked one lit, the one under the pointer lifted
		const MouseIO *mouse = TheMouse ? TheMouse->getMouseStatus() : NULL;
		const Bool hovered = mouse && mouse->pos.x >= clip.lo.x && mouse->pos.x < clip.hi.x
			&& mouse->pos.y >= max( rect.lo.y, clip.lo.y ) && mouse->pos.y < min( rect.hi.y, clip.hi.y );
		if (row == litRow)
			UIStyle::menuRow( rect, UIStyle::ROW_LOCAL );
		else if (selected)
			drawSelection( list, rect.lo.x + 1, rect.lo.y, rect.hi.x, rect.hi.y );
		if (hovered && row != litRow)
			UIStyle::menuRow( rect, UIStyle::ROW_HOVER );
		draw( list, row, rect, clip );
	}
	TheDisplay->enableClipping( FALSE );
}

void LobbyColumnSpan( GameWindow *list, Int x, Int column, Int &left, Int &width )
{
	ListboxData *data = list ? (ListboxData *)list->winGetUserData() : NULL;
	left = x;
	width = 0;
	if (!data || !data->columnWidth || column < 0 || column >= data->columns)
		return;
	for (Int each = 0; each < column; ++each)
		left += data->columnWidth[each];
	width = data->columnWidth[column];
}

/// the text of a cell and its colour, NULL for an image or an empty cell
static DisplayString *cellText( GameWindow *list, Int row, Int column, Color *color )
{
	ListboxData *data = list ? (ListboxData *)list->winGetUserData() : NULL;
	if (!data || row < 0 || row >= data->endPos || column < 0 || column >= data->columns || !data->listData[row].cell)
		return NULL;
	const ListEntryCell &cell = data->listData[row].cell[column];
	if (cell.cellType != LISTBOX_TEXT || cell.data == NULL)
		return NULL;
	if (color)
		*color = cell.color;
	return (DisplayString *)cell.data;
}

void LobbyDrawCell( GameWindow *list, Int row, Int column, const IRegion2D &rect, const IRegion2D &clip, LobbyAlign align )
{
	ListboxData *data = list ? (ListboxData *)list->winGetUserData() : NULL;
	if (!data || row < 0 || row >= data->endPos || column < 0 || column >= data->columns || !data->listData[row].cell)
		return;
	const ListEntryCell &cell = data->listData[row].cell[column];
	Int left, width;
	LobbyColumnSpan( list, rect.lo.x, column, left, width );
	if (width <= 0 || cell.data == NULL)
		return;

	// the column, inside the list
	IRegion2D box;
	box.lo.x = max( left, clip.lo.x );
	box.hi.x = min( left + width, clip.hi.x );
	box.lo.y = max( rect.lo.y, clip.lo.y );
	box.hi.y = min( rect.hi.y, clip.hi.y );
	if (box.hi.x <= box.lo.x || box.hi.y <= box.lo.y)
		return;

	// text keeps a pad from its column's edge; a light or a lock sits on the edge
	const Bool text = cell.cellType == LISTBOX_TEXT;
	const Int pad = text ? UIStyle::tallPx( CELL_PAD ) : 0;
	const Int rowHeight = rect.hi.y - rect.lo.y;
	Int w, h;
	if (text)
	{
		DisplayString *words = (DisplayString *)cell.data;
		if (words->getFont() != list->winGetFont())
			words->setFont( list->winGetFont() );
		words->getSize( &w, &h );
	}
	else
	{
		const Image *image = (const Image *)cell.data;
		h = min( UIStyle::tallPx( LIGHT_SIZE ), rowHeight );
		w = image->getImageHeight() > 0 ? h * image->getImageWidth() / image->getImageHeight() : h;
	}
	Int at = left + pad;
	if (align == LOBBY_ALIGN_CENTER)
		at = left + ( width - w ) / 2;
	else if (align == LOBBY_ALIGN_RIGHT)
		at = left + width - pad - w;
	const Int top = rect.lo.y + ( rowHeight - h ) / 2;

	if (text)
	{
		DisplayString *words = (DisplayString *)cell.data;
		words->setClipRegion( &box );
		words->draw( at, top, cell.color, UIStyle::SHADOW );
	}
	else
	{
		TheDisplay->setClipRegion( &box );
		TheWindowManager->winDrawImage( (const Image *)cell.data, at, top, at + w, top + h );
		IRegion2D inside = clip;
		TheDisplay->setClipRegion( &inside );
	}
}

void LobbySetColumns( GameWindow *list, Int columns, const Int *percent )
{
	ListboxData *data = list ? (ListboxData *)list->winGetUserData() : NULL;
	if (!data || columns < 2 || !percent)
		return;
	if (columns != data->columns)
	{
		// a row's cells are as many as the columns it was made under
		GadgetListBoxReset( list );
		delete[] data->columnWidth;
		delete[] data->columnWidthPercentage;
		data->columns = columns;
		data->columnWidth = NEW Int[ columns ];
		data->columnWidthPercentage = NEW Int[ columns ];
	}
	for (Int each = 0; each < columns; ++each)
		data->columnWidthPercentage[ each ] = percent[ each ];
	// the listbox works its widths out from the percentages whenever it is sized
	Int width, height;
	list->winGetSize( &width, &height );
	list->winSetSize( width, height );
}

//-------------------------------------------------------------------------------------------------
// The game lists in the menu language (MENU-REDESIGN.md package 10): every number at 1080 on the board
//-------------------------------------------------------------------------------------------------
namespace
{
	/// README-game-lists.md "Columns": lock, GAME, MODE, MAP, PLAYERS, PING, STATE, from the list's 60
	const Int GAME_COLUMN_EDGES[ LOBBY_COL_COUNT ][ 2 ] =
	{
		{ 60, 86 }, { 94, 440 }, { 452, 620 }, { 632, 850 }, { 862, 1012 }, { 1024, 1136 }, { 1160, 1286 }
	};
	Int bx( Int x1080 ) { return UIStyle::boardX( (Real)x1080 ); }
	Int by( Int y1080 ) { return UIStyle::boardY( (Real)y1080 ); }
	/// the board x of a screen x, for a click
	Int boardOfX( Int x )
	{
		const Real scale = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() ).scale;
		return REAL_TO_INT( ( x - UIStyle::boardX( 0.0f ) ) / ( scale > 0.0f ? scale : 1.0f ) );
	}
	/// words centred on a screen point
	void drawCentredWords( Int slot, const UnicodeString &text, Int px1080, Bool bold, Int x, Int middle, Color color )
	{
		if (text.isEmpty())
			return;
		DisplayString *s = lobbyWords( slot, text, UIStyle::menuFont( px1080, bold ) );
		Int w, h;
		s->getSize( &w, &h );
		s->draw( x - w / 2, middle - h / 2, color, GameMakeColor( 0, 0, 0, 160 ) );
	}
	/* A listed game's round trip.  -online: every route runs through the relay, so the pinger's figure means
		 nothing (it reads 1000 for everyone); the host's own round trip to the server, as the server last
		 reported it, is the part of the lag a game list can tell, and a host whose card the lobby has not heard
		 is unknown. */
	Int rowPing( GameSpyStagingRoom *game )
	{
		if (!OnlineIsEnabled())
			return game->getPingAsInt();
		GameSlot *host = game->getSlot( 0 );
		OnlinePlayerInfo card;
		AsciiString hostNick;
		if (host && host->isHuman())
			hostNick.translate( host->getName() );
		return hostNick.isNotEmpty() && OnlineGetPlayerInfo( hostNick, card ) ? card.ping : -1;
	}
	const char *const HEAD_KEYS[ LOBBY_COL_COUNT ] = { NULL, "GUI:OnlineLobbyColGame", "GUI:OnlineHeadMode", "GUI:OnlineMapLabel",
		"GUI:OnlineLobbyColPlayers", "GUI:DiplomacyPing", "GUI:DisconnectState" };
}

Bool LobbyGameColumnSpan( Int column, Bool lan, Int &left, Int &right )
{
	left = right = -1;
	if (column < 0 || column >= LOBBY_COL_COUNT || ( lan && ( column == LOBBY_COL_LOCK || column == LOBBY_COL_PING ) ))
		return FALSE;
	left = GAME_COLUMN_EDGES[ column ][ 0 ];
	right = GAME_COLUMN_EDGES[ column ][ 1 ];
	return TRUE;
}

Int LobbyGameColumnAt( Int x1080, Bool lan )
{
	for (Int column = 0; column < LOBBY_COL_COUNT; ++column)
	{
		Int left, right;
		if (LobbyGameColumnSpan( column, lan, left, right ) && x1080 >= left && x1080 < right)
			return column;
	}
	return -1;
}

LobbyGameState LobbyGameStateOf( Bool joining, Bool playing, Bool otherBuild, Bool full )
{
	if (joining)
		return LOBBY_STATE_JOINING;
	if (playing)
		return LOBBY_STATE_PLAYING;
	if (otherBuild)
		return LOBBY_STATE_OTHER_BUILD;
	return full ? LOBBY_STATE_FULL : LOBBY_STATE_OPEN;
}

Int LobbyMapMode( const AsciiString &map )
{
	// the online list's path is GameSpy's portable one, '/' for '\': its leaf names a Direct Strike island
	const char *leaf = strrchr( map.str(), '/' );
	if (IsDirectStrikeMap( leaf ? AsciiString( leaf + 1 ) : map ))
		return HOST_MODE_DIRECT_STRIKE;
	if (IsApocalypseMap( map ))
		return HOST_MODE_APOCALYPSE;
	return HOST_MODE_CLASSIC;
}

UnicodeString LobbyModeName( Int mode )
{
	if (mode == HOST_MODE_DIRECT_STRIKE)
		return ModeNameWithVersion( "GUI:DirectStrikeLower", "GUI:DirectStrikeVersion" );
	if (mode == HOST_MODE_APOCALYPSE)
		return ModeNameWithVersion( "GUI:ApocalypseLower", "GUI:ApocalypseVersion" );
	return TheGameText->fetch( "GUI:LobbyModeClassic" );
}

UnicodeString LobbyMapName( const AsciiString &map )
{
	const Int mode = LobbyMapMode( map );
	if (mode == HOST_MODE_DIRECT_STRIKE)
		return TheGameText->fetch( "GUI:LobbyGeneratedIsland" );
	if (mode == HOST_MODE_APOCALYPSE)
		return TheGameText->fetch( "GUI:LobbyGeneratedBasin" );
	return roomMapName( map );
}

void LobbyDrawGameRow( Int row, const LobbyGameRow &game, const IRegion2D &rect, Bool lan )
{
	const Int slot = LOBBY_TEXT_ROWS + ( row % ( ( LOBBY_TEXT_BOX - LOBBY_TEXT_ROWS ) / LOBBY_ROW_TEXTS ) ) * LOBBY_ROW_TEXTS;
	const Int mid = ( rect.lo.y + rect.hi.y ) / 2;
	const Bool dim = game.state == LOBBY_STATE_PLAYING || game.state == LOBBY_STATE_OTHER_BUILD;
	const Color ink = dim ? UIStyle::MUTED : UIStyle::INK;
	Int left, right;

	if (!lan && game.password)
	{
		const Image *lock = TheMappedImageCollection->findImageByName( "Password" );
		LobbyGameColumnSpan( LOBBY_COL_LOCK, lan, left, right );
		const Int side = UIStyle::px( 16.0f );
		const Int x = ( bx( left ) + bx( right ) - side ) / 2;
		if (lock)
			TheWindowManager->winDrawImage( lock, x, mid - side / 2, x + side, mid - side / 2 + side );
	}
	LobbyGameColumnSpan( LOBBY_COL_GAME, lan, left, right );
	drawFitted( slot, game.name, UIStyle::MENU_TEXT_BODY, FALSE, bx( left ), mid, bx( right ) - bx( left ), ink );
	LobbyGameColumnSpan( LOBBY_COL_MODE, lan, left, right );
	drawFitted( slot + 1, LobbyModeName( game.mode ), UIStyle::MENU_TEXT_HINT, FALSE, bx( left ), mid, bx( right ) - bx( left ),
		game.mode != HOST_MODE_CLASSIC && !dim ? UIStyle::INK : UIStyle::MUTED );
	LobbyGameColumnSpan( LOBBY_COL_MAP, lan, left, right );
	drawFitted( slot + 2, game.map, UIStyle::MENU_TEXT_HINT, FALSE, bx( left ), mid, bx( right ) - bx( left ), UIStyle::MUTED );

	// the seats: the taken figure, the row's key figure, then "/ m" and a quiet bar
	UnicodeString figure;
	figure.format( u"%d", game.seated );
	LobbyDrawWords( slot + 3, figure, UIStyle::MENU_TEXT_ROW_FIGURE, FALSE, bx( 896 ), mid, ink, TRUE );
	figure.format( u"/ %d", game.seats );
	LobbyDrawWords( slot + 4, figure, UIStyle::MENU_TEXT_HINT, FALSE, bx( 900 ), mid + UIStyle::px( 2.0f ), UIStyle::MUTED );
	UIStyle::track( bx( 942 ), mid, bx( 1008 ) - bx( 942 ), max( 1, UIStyle::px( 2.0f ) ),
		game.seats > 0 ? (Real)game.seated / game.seats : 0.0f, game.state == LOBBY_STATE_OPEN ? UIStyle::ACTIVE : UIStyle::MUTED );

	if (!lan)
	{
		if (game.ping >= 0)
			figure.format( u"%d", game.ping );
		else
			figure = UnicodeString( u"\x2013" );
		LobbyDrawWords( slot + 5, figure, UIStyle::MENU_TEXT_BODY, FALSE, bx( 1114 ), mid, ink, TRUE );
		if (const Image *light = UIStyle::pingImage( game.ping ))
		{
			const Int side = UIStyle::px( 16.0f );
			TheWindowManager->winDrawImage( light, bx( 1129 ) - side / 2, mid - side / 2, bx( 1129 ) - side / 2 + side, mid - side / 2 + side );
		}
	}

	const char *stateKey = "GUI:Open";
	Color stateColor = UIStyle::ACTIVE;
	Bool bold = TRUE;
	switch (game.state)
	{
		case LOBBY_STATE_FULL:				stateKey = "GUI:OnlineLobbyFull"; stateColor = UIStyle::MUTED; bold = FALSE; break;
		case LOBBY_STATE_PLAYING:			stateKey = "GUI:OnlineLobbyPlaying"; stateColor = UIStyle::MUTED; bold = FALSE; break;
		case LOBBY_STATE_JOINING:			stateKey = "GUI:OnlineLobbyJoining"; stateColor = UIStyle::EDGE_LIT; break;
		case LOBBY_STATE_OTHER_BUILD:	stateKey = "GUI:LobbyOtherBuild"; stateColor = UIStyle::MUTED; bold = FALSE; break;
		default: break;
	}
	LobbyGameColumnSpan( LOBBY_COL_STATE, lan, left, right );
	drawFitted( slot + 6, TheGameText->fetch( stateKey ), UIStyle::MENU_TEXT_HINT, bold, bx( left ), mid, bx( right ) - bx( left ), stateColor );
}

void LobbyDrawSection( const UnicodeString &words, const UnicodeString &count, Int left, Int top, Int right, Int slot )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
	const Int middle = by( top + 13 );
	UIStyle::drawTracked( UIStyle::capitals( words ), font, bx( left ), middle - font->height / 2, UIStyle::ACTIVE,
		UIStyle::MENU_LABEL_TRACKING );
	LobbyDrawWords( slot, count, UIStyle::MENU_TEXT_BODY, FALSE, bx( right ), middle, UIStyle::MUTED, TRUE );
}

void LobbyDrawGameHead( Int top1080, Bool lan, const UnicodeString &count, Int sortedColumn )
{
	LobbyDrawSection( TheGameText->fetch( "GUI:Games" ), count, UIStyle::ONLINE_LEFT, top1080, 1310, 0 );
	const Int middle = by( top1080 + 46 );
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	for (Int column = LOBBY_COL_GAME; column < LOBBY_COL_COUNT; ++column)
	{
		Int left, right;
		if (!LobbyGameColumnSpan( column, lan, left, right ))
			continue;
		DisplayString *words = lobbyWords( column, UIStyle::capitals( TheGameText->fetch( HEAD_KEYS[ column ] ) ), font );
		Int x = bx( left );
		// the ping's figure ends at the light: its word stands right over it
		if (column == LOBBY_COL_PING)
			x = bx( right ) - UIStyle::trackedWidth( words->getText(), font, UIStyle::MENU_LABEL_TRACKING );
		UIStyle::columnHeadAt( words, x, middle, UIStyle::MUTED, column == sortedColumn );
	}
	UIStyle::litLine( bx( UIStyle::ONLINE_LEFT ), by( top1080 + 61 ), bx( 1286 ) - bx( UIStyle::ONLINE_LEFT ), 1, UIStyle::FRAME );
}

void LobbyDrawGameFoot( GameWindow *list, Bool lockShown, const UnicodeString &empty )
{
	if (list == NULL)
		return;
	Int x, y, width, height;
	list->winGetScreenPosition( &x, &y );
	list->winGetSize( &width, &height );
	if (GadgetListBoxGetNumEntries( list ) == 0 && !empty.isEmpty())
		drawCentredWords( 8, empty, UIStyle::MENU_TEXT_BODY, FALSE, x + width / 2, y + UIStyle::px( 40.0f ), UIStyle::MUTED );
	if (lockShown)
	{
		const Int middle = y + height + UIStyle::px( 14.0f );
		const Int side = UIStyle::px( 16.0f );
		if (const Image *lock = TheMappedImageCollection->findImageByName( "Password" ))
			TheWindowManager->winDrawImage( lock, bx( 65 ), middle - side / 2, bx( 65 ) + side, middle - side / 2 + side );
		LobbyDrawWords( 9, TheGameText->fetch( "GUI:LobbyPassword" ), UIStyle::MENU_TEXT_HINT, FALSE, bx( 90 ), middle, UIStyle::MUTED );
	}
}

Int LobbySortedColumn( void )
{
	return theGameSortType == GAMESORT_PING_ASCENDING || theGameSortType == GAMESORT_PING_DESCENDING ? LOBBY_COL_PING : LOBBY_COL_GAME;
}

Bool LobbySortByColumn( Int column )
{
	if (column == LOBBY_COL_GAME)
		setSortMode( theGameSortType == GAMESORT_ALPHA_ASCENDING ? GAMESORT_ALPHA_DESCENDING : GAMESORT_ALPHA_ASCENDING );
	else if (column == LOBBY_COL_PING)
		setSortMode( theGameSortType == GAMESORT_PING_ASCENDING ? GAMESORT_PING_DESCENDING : GAMESORT_PING_ASCENDING );
	else
		return FALSE;
	return TRUE;
}

/// the map's preview fitted in its box keeping its shape, edged; a mode game's mode in words on dark glass
static void drawPicture( const LobbyPicked &game, const IRegion2D &box )
{
	TheDisplay->drawFillRect( box.lo.x, box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, GameMakeColor( 0, 0, 0, 150 ) );
	const Int w = box.hi.x - box.lo.x, h = box.hi.y - box.lo.y;
	if (game.preview && game.preview->getImageWidth() > 0 && game.preview->getImageHeight() > 0)
	{
		const Real iw = (Real)game.preview->getImageWidth(), ih = (Real)game.preview->getImageHeight();
		const Real fit = min( w / iw, h / ih );
		const Int dw = REAL_TO_INT( iw * fit ), dh = REAL_TO_INT( ih * fit );
		const Int x = box.lo.x + ( w - dw ) / 2, y = box.lo.y + ( h - dh ) / 2;
		TheWindowManager->winDrawImage( game.preview, x, y, x + dw, y + dh );
	}
	else
		drawCentredWords( LOBBY_TEXT_PICKED, LobbyModeName( game.mode ), UIStyle::MENU_TEXT_BODY, TRUE, box.lo.x + w / 2,
			box.lo.y + h / 2, UIStyle::MUTED );
	TheDisplay->drawOpenRect( box.lo.x, box.lo.y, w, h, 1.0f, UIStyle::FRAME );
}

void LobbyDrawPicked( const LobbyPicked &game, Int left, Int top, Int right, Int bottom, Bool lan )
{
	UnicodeString count;
	count.format( u"%d / %d", game.seated, game.seats );
	LobbyDrawSection( TheGameText->fetch( "GUI:LobbyPickedGame" ), count, left, top, right, 10 );
	Int slot = LOBBY_TEXT_PICKED + 1;

	Int wordsLeft, nameY, seatsTop;
	if (lan)
	{
		drawPicture( game, UIStyle::boardRect( left + 6, top + 38, right - 6, top + 302 ) );
		wordsLeft = left;
		nameY = top + 327;
		seatsTop = top + 404;
	}
	else
	{
		drawPicture( game, UIStyle::boardRect( left, top + 38, left + 220, top + 151 ) );
		wordsLeft = left + 236;
		nameY = top + 54;
		seatsTop = top + 162;
	}
	const Int wordsWidth = bx( right ) - bx( wordsLeft );
	drawFitted( slot++, game.name, UIStyle::MENU_TEXT_ROW_FIGURE, FALSE, bx( wordsLeft ), by( nameY ), wordsWidth, UIStyle::INK );
	drawFitted( slot++, game.map, UIStyle::MENU_TEXT_BODY, FALSE, bx( wordsLeft ), by( nameY + 28 ), wordsWidth, UIStyle::INK );
	drawFitted( slot++, game.line, UIStyle::MENU_TEXT_HINT, FALSE, bx( wordsLeft ), by( nameY + 52 ), wordsWidth, UIStyle::MUTED );
	if (game.password && !lan)
	{
		const Int side = UIStyle::px( 16.0f );
		const Int middle = by( nameY + 80 );
		if (const Image *lock = TheMappedImageCollection->findImageByName( "Password" ))
			TheWindowManager->winDrawImage( lock, bx( wordsLeft ), middle - side / 2, bx( wordsLeft ) + side, middle - side / 2 + side );
		LobbyDrawWords( slot, TheGameText->fetch( "GUI:LobbyPassword" ), UIStyle::MENU_TEXT_HINT, FALSE, bx( wordsLeft + 24 ),
			middle, UIStyle::MUTED );
	}
	++slot;

	// the seats down to bottom, 28 a row or tighter to fit, and the line under them
	const Int lineHeight = 26;
	const Int room = max( 0, bottom - seatsTop - lineHeight );
	Int shown = min( game.seatCount, room / 18 );
	const Int pitch = shown > 0 ? min( 28, room / shown ) : 28;
	for (Int i = 0; i < shown; ++i)
	{
		const LobbySeat &seat = game.seat[ i ];
		const Int top1080 = seatsTop + i * pitch;
		const IRegion2D r = UIStyle::boardRect( left, top1080, right, top1080 + pitch );
		const Int middle = ( r.lo.y + r.hi.y ) / 2;
		if (i % 2)
			TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, GameMakeColor( 255, 255, 255, 7 ) );
		const Int bar = UIStyle::px( pitch - 8.0f );
		TheDisplay->drawFillRect( bx( left + 4 ), middle - bar / 2, max( 2, UIStyle::px( 3.0f ) ), bar,
			seat.known ? seat.color : UIStyle::MUTED );
		if (seat.portrait)
		{
			const Int side = UIStyle::px( 22.0f );
			UIStyle::coverImage( seat.portrait, bx( left + 14 ), middle - side / 2, side, side );
		}
		drawFitted( slot++, seat.name, UIStyle::MENU_TEXT_BODY, FALSE, bx( left + 52 ), middle, bx( left + 250 ) - bx( left + 52 ),
			seat.known ? UIStyle::readable( seat.color ) : UIStyle::INK );
		drawFitted( slot++, seat.army, UIStyle::MENU_TEXT_HINT, FALSE, bx( left + 258 ), middle, bx( right - 70 ) - bx( left + 258 ),
			UIStyle::MUTED );
		LobbyDrawWords( slot++, seat.record, UIStyle::MENU_TEXT_HINT, FALSE, bx( right - 8 ), middle, UIStyle::INK, TRUE );
	}
	const Int lineMiddle = by( seatsTop + shown * pitch + lineHeight / 2 );
	const Int open = game.seats - game.seated;
	if (open > 0)
	{
		UnicodeString words;
		words.format( TheGameText->fetch( "GUI:LobbyOpenSeats" ), open );
		LobbyDrawWords( LOBBY_TEXT_PICKED + 46, words, UIStyle::MENU_TEXT_HINT, FALSE, bx( left + 52 ), lineMiddle, UIStyle::ACTIVE );
	}
	if (!lan)
	{
		// the records' legend, for a figure nobody reads unaided
		GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
		const UnicodeString legend = UIStyle::capitals( TheGameText->fetch( "GUI:LobbyWinsLosses" ) );
		UIStyle::drawTracked( legend, font, bx( right - 8 ) - UIStyle::trackedWidth( legend, font, UIStyle::MENU_LABEL_TRACKING ),
			lineMiddle - font->height / 2, UIStyle::MUTED, UIStyle::MENU_LABEL_TRACKING );
	}
}

void LobbyPlaceOnBoard( GameWindow *window, Int left, Int top, Int right, Int bottom )
{
	if (window == NULL)
		return;
	const IRegion2D r = UIStyle::boardRect( left, top, right, bottom );
	Int x = 0, y = 0;
	if (GameWindow *above = window->winGetParent())
		above->winGetScreenPosition( &x, &y );
	window->winSetPosition( r.lo.x - x, r.lo.y - y );
	const Int width = r.hi.x - r.lo.x, height = r.hi.y - r.lo.y;
	window->winSetSize( width, height );
	// a combo's parts keep their own sizes: the field takes the width less the caret block, a square
	if (BitTest( window->winGetStyle(), GWS_COMBO_BOX ))
	{
		if (GameWindow *button = GadgetComboBoxGetDropDownButton( window ))
		{
			button->winSetSize( height, height );
			button->winSetPosition( width - height, 0 );
		}
		if (GameWindow *edit = GadgetComboBoxGetEditBox( window ))
		{
			edit->winSetSize( width - height, height );
			edit->winSetPosition( 0, 0 );
		}
		if (GameWindow *list = GadgetComboBoxGetListBox( window ))
		{
			Int listWidth, listHeight;
			list->winGetSize( &listWidth, &listHeight );
			list->winSetSize( width, listHeight );
		}
	}
}

void LobbyDrawBoxHead( const IRegion2D &box, const UnicodeString &title, const UnicodeString &question, const UnicodeString &hint )
{
	DisplayString *words = lobbyWords( LOBBY_TEXT_BOX, UIStyle::capitals( title ), UIStyle::menuFont( UIStyle::MENU_TEXT_BOX_TITLE, TRUE ) );
	UIStyle::boxShell( box, words, FALSE );
	Int w, h;
	words->getSize( &w, &h );
	LobbyDrawWords( LOBBY_TEXT_BOX + 1, question, UIStyle::MENU_TEXT_BODY, FALSE, box.lo.x + UIStyle::px( UIStyle::BOX_PAD ) + w
		+ UIStyle::px( 18.0f ), box.lo.y + UIStyle::px( 46.0f ), UIStyle::INK );
	UIStyle::boxFootRule( box );
	LobbyDrawWords( LOBBY_TEXT_BOX + 2, hint, UIStyle::MENU_TEXT_HINT, FALSE, box.lo.x + UIStyle::px( UIStyle::BOX_PAD ),
		box.hi.y - UIStyle::px( 44.0f ), UIStyle::MUTED );
}

void LobbyDrawBoxLabel( const UnicodeString &words, Int x, Int middle )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	UIStyle::drawTracked( UIStyle::capitals( words ), font, x, middle - font->height / 2, UIStyle::INK, UIStyle::MENU_LABEL_TRACKING );
}

void LobbyMenuKey( GameWindow *key, Bool primary )
{
	if (key == NULL)
		return;
	UIStyle::setMenuKey( key, primary );
	const UnicodeString words = UIStyle::capitals( key->winGetText() );
	if (words.compare( key->winGetText() ) != 0)
		GadgetButtonSetText( key, words );
}

void LobbyMenuEntry( GameWindow *entry, Int px1080, Color text )
{
	if (entry == NULL)
		return;
	entry->winClearStatus( WIN_STATUS_IMAGE );
	entry->winSetDrawFunc( TheWindowManager->getTextEntryDrawFunc() );
	const Color well = GameMakeColor( 0, 0, 0, 140 );
	GadgetTextEntrySetEnabledColor( entry, well );
	GadgetTextEntrySetEnabledBorderColor( entry, UIStyle::FRAME );
	GadgetTextEntrySetHiliteColor( entry, well );
	GadgetTextEntrySetHiliteBorderColor( entry, UIStyle::EDGE_LIT );
	GadgetTextEntrySetDisabledColor( entry, GameMakeColor( 0, 0, 0, 100 ) );
	GadgetTextEntrySetDisabledBorderColor( entry, UIStyle::withAlpha( UIStyle::FRAME, 140 ) );
	GadgetTextEntrySetFont( entry, UIStyle::menuFont( px1080 ) );
	GadgetTextEntrySetTextColor( entry, text );
}

void LobbyMenuChat( GameWindow *list, Int px1080 )
{
	if (list == NULL)
		return;
	const Color lift = GameMakeColor( 255, 255, 255, 6 );
	const Color none = GameMakeColor( 0, 0, 0, 0 );
	GadgetListBoxSetColors( list,
		lift, none, lift, none,
		lift, none, lift, none,
		lift, none, lift, none );
	list->winClearStatus( WIN_STATUS_IMAGE );
	list->winSetDrawFunc( TheWindowManager->getListBoxDrawFunc() );
	GadgetListBoxSetFont( list, UIStyle::menuFont( px1080 ) );
}

/// an online row: the staging room behind it as LobbyDrawGameRow wants it
static void drawGameRow( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip )
{
	(void)clip;
	const Int id = (Int)(intptr_t)GadgetListBoxGetItemData( list, row );
	GameSpyStagingRoom *room = TheGameSpyInfo->findStagingRoomByID( id );
	if (room == NULL)
		return;
	LobbyGameRow game;
	game.name = room->getGameName();
	game.map = LobbyMapName( room->getMap() );
	game.mode = LobbyMapMode( room->getMap() );
	game.seated = room->getReportedNumPlayers();
	game.seats = room->getReportedMaxPlayers();
	game.ping = rowPing( room );
	game.password = room->getHasPassword();
	game.state = LobbyGameStateOf( s_joiningGame && id == s_joiningGame, room->isGameInProgress(),
		room->getExeCRC() != TheGlobalData->m_exeCRC || room->getIniCRC() != TheGlobalData->m_iniCRC, LobbyGameFull( room ) );
	LobbyDrawGameRow( row, game, rect, FALSE );
}

/// the online list: its head over it, its rows, the legend and the empty line under them
static void drawGameList( GameWindow *window, WinInstanceData *instData )
{
	(void)instData;
	Int open = 0, playing = 0;
	Bool locks = FALSE;
	StagingRoomMap *games = TheGameSpyInfo->getStagingRoomList();
	for (StagingRoomMap::iterator it = games->begin(); it != games->end(); ++it)
	{
		GameSpyStagingRoom *game = it->second;
		if (!game)
			continue;
		locks = locks || game->getHasPassword();
		if (game->isGameInProgress())
			++playing;
		else if (!LobbyGameFull( game ) && game->getExeCRC() == TheGlobalData->m_exeCRC && game->getIniCRC() == TheGlobalData->m_iniCRC)
			++open;
	}
	UnicodeString count;
	if (playing > 0)
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCount" ), open, playing );
	else
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCountOpen" ), open );
	LobbyDrawGameHead( 184, FALSE, count, LobbySortedColumn() );
	LobbyDrawRows( window, drawGameRow );

	UnicodeString empty;
	if (OnlineIsEnabled())
		empty.format( TheGameText->fetch( "GUI:LobbyEmptyOnline" ), LobbyServerLabel().str() );
	LobbyDrawGameFoot( window, locks, empty );
}

/// the list's rows 40 high at the board's scale, drawn by drawGameList
static void freeGameListText( void );
static void placeGameList( void )
{
	freeGameListText();
	if (!listboxLobbyGamesLarge)
		return;
	LobbySetColumns( listboxLobbyGamesLarge, GAME_COLUMNS, GAME_COLUMN_PERCENT );
	listboxLobbyGamesLarge->winSetDrawFunc( drawGameList );
}

static void freeGameListText( void )
{
	LobbyFreeText();
	s_joiningGame = 0;
}

static Bool isSmall = TRUE;

GameWindow *GetGameListBox( void )
{
	return listboxLobbyGamesLarge;
}

GameWindow *GetGameInfoListBox( void )
{
	return NULL;
}

NameKeyType GetGameListBoxID( void )
{
	return listboxLobbyGamesLargeID;
}

NameKeyType GetGameInfoListBoxID( void )
{
	return NAMEKEY_INVALID;
}

void GrabWindowInfo( void )
{
	isSmall = TRUE;
	parentID = NAMEKEY( "WOLCustomLobby.wnd:WOLLobbyMenuParent" );
	parent = TheWindowManager->winGetWindowFromId(NULL, parentID);

//	parentGameListSmallID = NAMEKEY( "WOLCustomLobby.wnd:ParentGameListSmall" );
//	parentGameListSmall = TheWindowManager->winGetWindowFromId(NULL, parentGameListSmallID);

	parentGameListLargeID = NAMEKEY( "WOLCustomLobby.wnd:ParentGameListLarge" );
	parentGameListLarge = TheWindowManager->winGetWindowFromId(NULL, parentGameListLargeID);

	listboxLobbyGamesSmallID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGames" );
//	listboxLobbyGamesSmall = TheWindowManager->winGetWindowFromId(NULL, listboxLobbyGamesSmallID);
//	listboxLobbyGamesSmall->winSetTooltipFunc(gameTooltip);

	listboxLobbyGamesLargeID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGamesLarge" );
	listboxLobbyGamesLarge = TheWindowManager->winGetWindowFromId(NULL, listboxLobbyGamesLargeID);
	listboxLobbyGamesLarge->winSetTooltipFunc(gameTooltip);
	placeGameList();
//
//	listboxLobbyGameInfoID = NAMEKEY( "WOLCustomLobby.wnd:ListboxGameInfo" );
//	listboxLobbyGameInfo = TheWindowManager->winGetWindowFromId(NULL, listboxLobbyGameInfoID);

	buttonSortAlphaID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortAlpha");
	buttonSortPingID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortPing");
	buttonSortBuddiesID = NAMEKEY("WOLCustomLobby.wnd:ButtonSortBuddies");
	windowSortAlphaID = NAMEKEY("WOLCustomLobby.wnd:WindowSortAlpha");
	windowSortPingID = NAMEKEY("WOLCustomLobby.wnd:WindowSortPing");
	windowSortBuddiesID = NAMEKEY("WOLCustomLobby.wnd:WindowSortBuddies");

	buttonSortAlpha = TheWindowManager->winGetWindowFromId(parent, buttonSortAlphaID);
	buttonSortPing = TheWindowManager->winGetWindowFromId(parent, buttonSortPingID);
	buttonSortBuddies = TheWindowManager->winGetWindowFromId(parent, buttonSortBuddiesID);
	windowSortAlpha = TheWindowManager->winGetWindowFromId(parent, windowSortAlphaID);
	windowSortPing = TheWindowManager->winGetWindowFromId(parent, windowSortPingID);
	windowSortBuddies = TheWindowManager->winGetWindowFromId(parent, windowSortBuddiesID);

	showSortIcons();
}

void ReleaseWindowInfo( void )
{
	freeGameListText();
	isSmall = TRUE;
	parent = NULL;
//	parentGameListSmall = NULL;
	parentGameListLarge = NULL;
//	listboxLobbyGamesSmall = NULL;
	listboxLobbyGamesLarge = NULL;
//	listboxLobbyGameInfo = NULL;

	buttonSortAlpha = NULL;
	buttonSortPing = NULL;
	buttonSortBuddies = NULL;
	windowSortAlpha = NULL;
	windowSortPing = NULL;
	windowSortBuddies = NULL;
}

typedef std::set<GameSpyStagingRoom *> BuddyGameSet;
static BuddyGameSet *theBuddyGames = NULL;
static void populateBuddyGames(void)
{
	BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
	theBuddyGames = NEW BuddyGameSet;
	if (!m)
	{
		return;
	}
	for (BuddyInfoMap::const_iterator bit = m->begin(); bit != m->end(); ++bit)
	{
		BuddyInfo info = bit->second;
		if (info.m_status == GP_STAGING)
		{
			StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
			for (StagingRoomMap::iterator srmIt = srm->begin(); srmIt != srm->end(); ++srmIt)
			{
				GameSpyStagingRoom *game = srmIt->second;
				game->cleanUpSlotPointers();
				const GameSpyGameSlot *slot = game->getGameSpySlot(0);
				if (slot && slot->getName() == info.m_locationString)
				{
					theBuddyGames->insert(game);
					break;
				}
			}
		}
	}
}

static void clearBuddyGames(void)
{
	if (theBuddyGames)
		delete theBuddyGames;
	theBuddyGames = NULL;
}

struct GameSortStruct
{
	// multiset calls its comparator through a const reference.
	bool operator()(GameSpyStagingRoom *g1, GameSpyStagingRoom *g2) const
	{
		// sort CRC mismatches to the bottom.  Both lines were wrong: each said "good" while testing
		// for a mismatch, and the second read g1 twice, so the two were always equal, the xor below
		// was always false and this branch never sorted anything at all.
		Bool g1Good = (g1->getExeCRC() == TheGlobalData->m_exeCRC && g1->getIniCRC() == TheGlobalData->m_iniCRC);
		Bool g2Good = (g2->getExeCRC() == TheGlobalData->m_exeCRC && g2->getIniCRC() == TheGlobalData->m_iniCRC);
		if ( g1Good ^ g2Good )
		{
			return g1Good;
		}

		// sort games with private ladders to the bottom
		Bool g1UnknownLadder = (g1->getLadderPort() && TheLadderList->findLadder(g1->getLadderIP(), g1->getLadderPort()) == NULL);
		Bool g2UnknownLadder = (g2->getLadderPort() && TheLadderList->findLadder(g2->getLadderIP(), g2->getLadderPort()) == NULL);
		if ( g1UnknownLadder ^ g2UnknownLadder )
		{
			return g2UnknownLadder;
		}

		// sort full games to the bottom
		Bool g1Full = LobbyGameFull(g1);
		Bool g2Full = LobbyGameFull(g2);
		if ( g1Full ^ g2Full )
		{
			return g2Full;
		}

		if (sortBuddies)
		{
			Bool g1HasBuddies = (theBuddyGames->find(g1) != theBuddyGames->end());
			Bool g2HasBuddies = (theBuddyGames->find(g2) != theBuddyGames->end());
			if ( g1HasBuddies ^ g2HasBuddies )
			{
				return g1HasBuddies;
			}
		}

		switch(theGameSortType)
		{
		case GAMESORT_ALPHA_ASCENDING:
			return WideCharICmp(g1->getGameName().str(), g2->getGameName().str()) < 0;
			break;
		case GAMESORT_ALPHA_DESCENDING:
			return WideCharICmp(g1->getGameName().str(),g2->getGameName().str()) > 0;
			break;
		case GAMESORT_PING_ASCENDING:
			return g1->getPingAsInt() < g2->getPingAsInt();
			break;
		case GAMESORT_PING_DESCENDING:
			return g1->getPingAsInt() > g2->getPingAsInt();
			break;
		}
		return false;
	}
};

/* A game's row (drawGameRow draws it, LobbyDrawGameRow's states: Open, Full, Playing, Joining, Other build). */
static Int insertGame( GameWindow *win, GameSpyStagingRoom *game )
{
	game->cleanUpSlotPointers();
	const Bool full = LobbyGameFull(game);
	const Bool playing = game->isGameInProgress();
	const Bool otherBuild = game->getExeCRC() != TheGlobalData->m_exeCRC || game->getIniCRC() != TheGlobalData->m_iniCRC;
	const Color gameColor = (full || playing || otherBuild) ? UIStyle::MUTED : UIStyle::INK;
	UnicodeString gameName = game->getGameName();
	
	if(TheGameSpyInfo->getDisallowAsianText())
	{
		const WideChar *buff = gameName.str();
		Int length =  gameName.getLength();	
		for(Int i = 0; i < length; ++i)
		{
			if(buff[i] >= 256)
				return -1;
		}
	}
	else if(TheGameSpyInfo->getDisallowNonAsianText())
	{
		const WideChar *buff = gameName.str();
		Int length =  gameName.getLength();	
		Bool hasUnicode = FALSE;
		for(Int i = 0; i < length; ++i)
		{
			if(buff[i] >= 256)
			{
				hasUnicode = TRUE;
				break;
			}
		}
		if(!hasUnicode)
			return -1;
	}



	// the row is drawn from the room (drawGameRow); the cells keep its name for the listbox and, with a
	// blank image a row high, its height of 40 at the board's scale
	Int index = GadgetListBoxAddEntryText(win, gameName, gameColor, -1, COLUMN_NAME);
	GadgetListBoxSetItemData(win, (void *)(intptr_t)game->getID(), index);
	GadgetListBoxAddEntryImage(win, NULL, index, COLUMN_LIGHT, UIStyle::px(GAME_ROW_HEIGHT) - 1, UIStyle::px(LIGHT_SIZE));
	return index;
}

void RefreshGameListBox( GameWindow *win, Bool showMap )
{
	if (!win)
		return;

	// save off selection
	Int selectedIndex = -1;
	Int indexToSelect = -1;
	Int selectedID = 0;
	GadgetListBoxGetSelected(win, &selectedIndex);
	if (selectedIndex != -1 )
	{
		selectedID = (Int)(intptr_t)GadgetListBoxGetItemData(win, selectedIndex);
	}
	int prevPos = GadgetListBoxGetTopVisibleEntry( win );

	// empty listbox
	GadgetListBoxReset(win);

	// sort our games
	typedef std::multiset<GameSpyStagingRoom *, GameSortStruct> SortedGameList;
	SortedGameList sgl;
	StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
	populateBuddyGames();
	for (StagingRoomMap::iterator srmIt = srm->begin(); srmIt != srm->end(); ++srmIt)
	{
		sgl.insert(srmIt->second);
	}

	// populate listbox
	for (SortedGameList::iterator sglIt = sgl.begin(); sglIt != sgl.end(); ++sglIt)
	{
		GameSpyStagingRoom *game = *sglIt;
		if (game)
		{
			Int index = insertGame(win, game);
			if (game->getID() == selectedID)
			{
				indexToSelect = index;
			}
		}
	}

	clearBuddyGames();

	// restore selection
	GadgetListBoxSetSelected(win, indexToSelect); // even for -1, so we can disable the 'Join Game' button
//	if(prevPos > 10)
		GadgetListBoxSetTopVisibleEntry( win, prevPos  );//+ 1

	if (indexToSelect < 0 && selectedID)
	{
		TheWindowManager->winSetLoneWindow(NULL);
	}
}

void RefreshGameInfoListBox( GameWindow *mainWin, GameWindow *win )
{
//	if (!mainWin || !win)
//		return;
//
//	GadgetListBoxReset(win);
//
//	Int selected = -1;
//	GadgetListBoxGetSelected(mainWin, &selected);
//	if (selected < 0)
//	{
//		return;
//	}
//
//	Int selectedID = (Int)GadgetListBoxGetItemData(mainWin, selected);
//	if (selectedID < 0)
//	{
//		return;
//	}
//
//	StagingRoomMap *srm = TheGameSpyInfo->getStagingRoomList();
//	StagingRoomMap::iterator srmIt = srm->find(selectedID);
//	if (srmIt != srm->end())
//	{
//		GameSpyStagingRoom *theRoom = srmIt->second;
//		theRoom->cleanUpSlotPointers();
//
//		// game name
////		GadgetListBoxAddEntryText(listboxLobbyGameInfo, theRoom->getGameName(), GameSpyColor[GSCOLOR_DEFAULT], -1);
//
//		const LadderInfo * li = TheLadderList->findLadder(theRoom->getLadderIP(), theRoom->getLadderPort());
//		if (li)
//		{
//			UnicodeString tmp;
//			tmp.format(TheGameText->fetch("TOOLTIP:LadderName"), li->name.str());
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, tmp, GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//		else if (theRoom->getLadderPort())
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:UnknownLadder"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//		else
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:NoLadder"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//
//		if (theRoom->getExeCRC() != TheGlobalData->m_exeCRC || theRoom->getIniCRC() != TheGlobalData->m_iniCRC)
//		{
//			GadgetListBoxAddEntryText(listboxLobbyGameInfo, TheGameText->fetch("TOOLTIP:InvalidGameVersionSingleLine"), GameSpyColor[GSCOLOR_DEFAULT], -1);
//		}
//
//		// map name
//		UnicodeString mapName;
//		const MapMetaData *md = TheMapCache->findMap(theRoom->getMap());
//		if (md)
//		{
//			mapName = md->m_displayName;
//		}
//		else
//		{
//			const char *start = theRoom->getMap().reverseFind('\\');
//			if (start)
//			{
//				++start;
//			}
//			else
//			{
//				start = theRoom->getMap().str();
//			}
//			mapName.translate( start );
//		}
//
//		GadgetListBoxAddEntryText(listboxLobbyGameInfo, mapName, GameSpyColor[GSCOLOR_DEFAULT], -1);
//
//		// player list (rank, win/loss, side)
//		for (Int i=0; i<MAX_SLOTS; ++i)
//		{
//			const GameSpyGameSlot *slot = theRoom->getGameSpySlot(i);
//			if (slot && slot->isHuman())
//			{
//				UnicodeString theName, theRating, thePlayerTemplate;
//				Int colorIdx = slot->getColor();
//				theName = slot->getName();
//				theRating.format(L" (%d-%d)", slot->getWins(), slot->getLosses());
//				const PlayerTemplate * pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
//				if (pt)
//				{
//					thePlayerTemplate = pt->getDisplayName();
//				}
//				else
//				{
//					thePlayerTemplate = TheGameText->fetch("GUI:Random");
//				}
//
//				UnicodeString theText;
//				theText.format(L"%ls - %ls - %ls", theName.str(), thePlayerTemplate.str(), theRating.str());
//
//				Int theColor = GameSpyColor[GSCOLOR_DEFAULT];
//				const MultiplayerColorDefinition *mcd = TheMultiplayerSettings->getColor(colorIdx);
//				if (mcd)
//				{
//					theColor = mcd->getColor();
//				}
//
//				GadgetListBoxAddEntryText(listboxLobbyGameInfo, theText, theColor, -1);
//			}
//		}
//	}

}

void RefreshGameListBoxes( void )
{
	GameWindow *main = GetGameListBox();
	GameWindow *info = GetGameInfoListBox();

	RefreshGameListBox( main, (info == NULL) );

	if (info)
	{
		RefreshGameInfoListBox( main, info );
	}
}

void ToggleGameListType( void )
{
	isSmall = !isSmall;
	if(isSmall)
	{
		parentGameListLarge->winHide(TRUE);
//		parentGameListSmall->winHide(FALSE);
	}
	else
	{
		parentGameListLarge->winHide(FALSE);
//		parentGameListSmall->winHide(TRUE);
	}

	RefreshGameListBoxes();
}

// for use by GameWindow::winSetTooltipFunc
// displays the Army Tooltip for the player templates
void playerTemplateComboBoxTooltip(GameWindow *wndComboBox, WinInstanceData *instData, UnsignedInt mouse)
{
	Int index = 0;
	GadgetComboBoxGetSelectedPos(wndComboBox, &index);
	Int templateNum = (Int)(intptr_t)GadgetComboBoxGetItemData(wndComboBox, index);
	UnicodeString ustringTooltip;
	if (IsRandomPlayerTemplate(templateNum))
	{
			// the "Random" template is always first
			ustringTooltip = TheGameText->fetch("TOOLTIP:BioStrategyLong_Random");
	}
	else
	{
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		if (playerTemplate)
		{
			ustringTooltip = TheGameText->fetch(playerTemplate->getTooltip());
		}
	}
	TheMouse->setCursorTooltip(ustringTooltip);
}

// -----------------------------------------------------------------------------

// for use by GameWindow::winSetTooltipFunc
// displays the Army Tooltip for the player templates
void playerTemplateListBoxTooltip(GameWindow *wndListBox, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);
	GadgetListBoxGetEntryBasedOnXY(wndListBox, x, y, row, col);
	if (row == -1 || col == -1)
		return;

	Int templateNum = (Int)(intptr_t)GadgetListBoxGetItemData(wndListBox, row, col);
	UnicodeString ustringTooltip;
	if (IsRandomPlayerTemplate(templateNum))
	{
			// the "Random" template is always first
			ustringTooltip = TheGameText->fetch("TOOLTIP:BioStrategyLong_Random");
	}
	else
	{
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		if (playerTemplate)
		{
			ustringTooltip = TheGameText->fetch(playerTemplate->getTooltip());
		}
	}

	// use no tooltip delay here
	TheMouse->setCursorTooltip(ustringTooltip, 0);
}
