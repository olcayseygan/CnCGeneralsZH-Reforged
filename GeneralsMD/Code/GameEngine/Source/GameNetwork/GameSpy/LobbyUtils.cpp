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
// a list narrower than this at the height's scale drops the seats column, as the design's 4:3 rule says
static const Real GAME_LIST_FULL_WIDTH = 900.0f;
static const Int GAME_COLUMN_PERCENT_NARROW[ GAME_COLUMNS ] = { 43, 29, 0, 3, 3, 8, 14, 0 };
static const Real GAME_ROW_HEIGHT = 40.0f;		///< the mockup's, at 1080
static const Real GAME_HEAD_HEIGHT = 36.0f;		///< the column heads' band over the list
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
		mapName = TheGameText->fetch("GUI:DirectStrikeLower");
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

	if (col == COLUMN_PING || col == COLUMN_LIGHT)
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
	if (col == COLUMN_NUMPLAYERS)
	{
		TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:NumberOfPlayers"), 10, NULL, 2.0f ); // the text and width are the only params used.  the others are the default values.
		return;
	}
	if (col == COLUMN_PASSWORD)
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
enum { HEAD_GAME, HEAD_MAP, HEAD_PLAYERS, HEAD_PING, HEAD_COUNT };
static const Int HEAD_COLUMN[ HEAD_COUNT ] = { COLUMN_NAME, COLUMN_MAP, COLUMN_NUMPLAYERS, COLUMN_LIGHT };
static const char *const HEAD_KEY[ HEAD_COUNT ] = { "GUI:OnlineLobbyColGame", "GUI:OnlineMapLabel", "GUI:OnlineLobbyColPlayers",
	"GUI:DiplomacyPing" };
static DisplayString *s_gameHeads[ HEAD_COUNT ];
static DisplayString *s_noGames = NULL;			///< "No open games on EU1. Host one."
static DisplayString *s_joiningChip = NULL;

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

/// a selected row in the list's own art: EA's hilite bar, its ends at their width and its middle stretched
static void drawSelection( GameWindow *list, Int left, Int top, Int right, Int bottom )
{
	const Image *leftEnd, *rightEnd, *middle;
	if (!BitTest( list->winGetStatus(), WIN_STATUS_ENABLED ))
	{
		leftEnd = GadgetListBoxGetDisabledSelectedItemImageLeft( list );
		rightEnd = GadgetListBoxGetDisabledSelectedItemImageRight( list );
		middle = GadgetListBoxGetDisabledSelectedItemImageCenter( list );
	}
	else if (BitTest( list->winGetInstanceData()->getState(), WIN_STATE_HILITED ))
	{
		leftEnd = GadgetListBoxGetHiliteSelectedItemImageLeft( list );
		rightEnd = GadgetListBoxGetHiliteSelectedItemImageRight( list );
		middle = GadgetListBoxGetHiliteSelectedItemImageCenter( list );
	}
	else
	{
		leftEnd = GadgetListBoxGetEnabledSelectedItemImageLeft( list );
		rightEnd = GadgetListBoxGetEnabledSelectedItemImageRight( list );
		middle = GadgetListBoxGetEnabledSelectedItemImageCenter( list );
	}
	if (!leftEnd || !rightEnd || !middle)
	{
		TheDisplay->drawOpenRect( left, top, right - left, bottom - top, 1.0f, UIStyle::EDGE );
		return;
	}
	const Int leftWidth = leftEnd->getImageWidth();
	const Int rightWidth = rightEnd->getImageWidth();
	TheWindowManager->winDrawImage( leftEnd, left, top, left + leftWidth, bottom );
	if (right - rightWidth > left + leftWidth)
		TheWindowManager->winDrawImage( middle, left + leftWidth, top, right - rightWidth, bottom );
	TheWindowManager->winDrawImage( rightEnd, right - rightWidth, top, right, bottom );
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

		Bool selected = row == litRow;
		if (data->multiSelect)
		{
			for (Int i = 0; data->selections && i < data->listLength && data->selections[i] >= 0 && !selected; ++i)
				selected = data->selections[i] == row;
		}
		else
			selected = data->selectPos == row;

		TheDisplay->setClipRegion( &clip );
		if (selected)
			drawSelection( list, rect.lo.x + 1, rect.lo.y, rect.hi.x, rect.hi.y );
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

/// a game's row: name and map from the left, seats and the light at the right of theirs, the chip last
static void drawGameRow( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip )
{
	LobbyDrawCell( list, row, COLUMN_NAME, rect, clip, LOBBY_ALIGN_LEFT );
	LobbyDrawCell( list, row, COLUMN_MAP, rect, clip, LOBBY_ALIGN_LEFT );
	LobbyDrawCell( list, row, COLUMN_NUMPLAYERS, rect, clip, LOBBY_ALIGN_RIGHT );
	LobbyDrawCell( list, row, COLUMN_PASSWORD, rect, clip, LOBBY_ALIGN_CENTER );
	LobbyDrawCell( list, row, COLUMN_LIGHT, rect, clip, LOBBY_ALIGN_RIGHT );
	LobbyDrawCell( list, row, COLUMN_PING, rect, clip, LOBBY_ALIGN_LEFT );

	// Open, Full, or Joining while the join for this game is out
	Color color = UIStyle::ACTIVE;
	DisplayString *chip = cellText( list, row, COLUMN_STATE, &color );
	if (s_joiningGame && s_joiningChip && (Int)(intptr_t)GadgetListBoxGetItemData( list, row ) == s_joiningGame)
	{
		chip = s_joiningChip;
		color = UIStyle::ACTIVE;
	}
	if (chip)
	{
		GameFont *face = UIStyle::chipFont();
		if (chip->getFont() != face)
			chip->setFont( face );
		Int left, width, w, h;
		LobbyColumnSpan( list, rect.lo.x, COLUMN_STATE, left, width );
		chip->getSize( &w, &h );
		// UIStyle::chip's box round the label
		const Int chipWidth = w + 2 * UIStyle::tallPx( 8.0f );
		const Int chipHeight = h + UIStyle::tallPx( 5.0f ) + UIStyle::tallPx( 4.0f );
		IRegion2D inside = clip;
		chip->setClipRegion( &inside );
		UIStyle::chip( chip, left + width - UIStyle::tallPx( CELL_PAD ) - chipWidth,
			rect.lo.y + ( rect.hi.y - rect.lo.y - chipHeight ) / 2, color, UIStyle::FRAME );
	}

	if (rect.hi.y <= clip.hi.y)
		TheDisplay->drawFillRect( rect.lo.x, rect.hi.y - 1, rect.hi.x - rect.lo.x, 1, UIStyle::GRID );
}

/// the game list: the column heads on their rule over it, its rows, or the line saying it is empty
static void drawGameList( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Int band = UIStyle::tallPx( GAME_HEAD_HEIGHT );
	const Int pad = UIStyle::tallPx( CELL_PAD );
	for (Int head = 0; head < HEAD_COUNT; ++head)
	{
		Int left, columnWidth, w, h;
		LobbyColumnSpan( window, x, HEAD_COLUMN[ head ], left, columnWidth );
		if (columnWidth <= 0 || s_gameHeads[ head ] == NULL)
			continue;
		s_gameHeads[ head ]->getSize( &w, &h );
		Int at = left + pad;
		if (head == HEAD_PLAYERS)
			at = left + columnWidth - pad - w;
		else if (head == HEAD_PING)		// from where the light starts
			at = left + columnWidth - min( UIStyle::tallPx( LIGHT_SIZE ), UIStyle::tallPx( GAME_ROW_HEIGHT ) );
		s_gameHeads[ head ]->draw( at, y - band + ( band - h ) / 2, UIStyle::INK, UIStyle::SHADOW );
	}
	Int last, lastWidth;
	LobbyColumnSpan( window, x, GAME_COLUMNS - 1, last, lastWidth );
	TheDisplay->drawFillRect( x, y - 1, last + lastWidth - x, 1, UIStyle::FRAME );

	if (GadgetListBoxGetNumEntries( window ) > 0)
	{
		LobbyDrawRows( window, drawGameRow );
		return;
	}
	if (s_noGames && OnlineIsEnabled())
	{
		Int w, h;
		s_noGames->getSize( &w, &h );
		s_noGames->draw( x + ( width - w ) / 2, y + ( height - h ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
	}
}

/* The list under its column heads.  The layout puts it right under its box's heading, so it moves down by
	 the heads' band, counted from the box's top: a second call, or a layout that already leaves the band,
	 lands where it is. */
static void freeGameListText( void );
static void placeGameList( void )
{
	GameWindow *list = listboxLobbyGamesLarge;
	freeGameListText();
	if (!list || !parentGameListLarge)
		return;
	Int boxX, boxY, x, y, width, height;
	parentGameListLarge->winGetScreenPosition( &boxX, &boxY );
	list->winGetScreenPosition( &x, &y );
	list->winGetSize( &width, &height );
	const Int top = boxY + UIStyle::px( 56.0f ) + UIStyle::tallPx( GAME_HEAD_HEIGHT );
	const Int bottom = y + height;
	if (top != y && bottom - top > UIStyle::tallPx( GAME_ROW_HEIGHT ))
	{
		Int left, above;
		list->winGetPosition( &left, &above );
		list->winSetPosition( left, above + top - y );
		height = bottom - top;
		list->winSetSize( width, height );
	}
	LobbySetColumns( list, GAME_COLUMNS,
		width >= UIStyle::tallPx( GAME_LIST_FULL_WIDTH ) ? GAME_COLUMN_PERCENT : GAME_COLUMN_PERCENT_NARROW );
	list->winSetDrawFunc( drawGameList );

	// the heads fitted to their columns, the empty line, the joining chip
	GameFont *body = UIStyle::readableFont( UIStyle::TEXT_BODY );
	for (Int head = 0; head < HEAD_COUNT; ++head)
	{
		Int left, columnWidth;
		LobbyColumnSpan( list, x, HEAD_COLUMN[ head ], left, columnWidth );
		if (head == HEAD_PING)		// the light's column and the milliseconds' together
		{
			Int pingLeft, pingWidth;
			LobbyColumnSpan( list, x, COLUMN_PING, pingLeft, pingWidth );
			columnWidth += pingWidth;
		}
		s_gameHeads[ head ] = TheDisplayStringManager->newDisplayString();
		s_gameHeads[ head ]->setFont( body );
		s_gameHeads[ head ]->setText( UIStyle::fitKey( HEAD_KEY[ head ], body, max( columnWidth - 2 * UIStyle::tallPx( CELL_PAD ), 1 ) ) );
	}
	const UnicodeString empty = TheGameText->fetch( "GUI:OnlineLobbyEmpty" );
	s_noGames = TheDisplayStringManager->newDisplayString();
	s_noGames->setFont( body );
	s_noGames->setText( UIStyle::fitText( empty, body, max( width - 2 * UIStyle::tallPx( CELL_PAD ), 1 ) ) );
	s_joiningChip = TheDisplayStringManager->newDisplayString();
	s_joiningChip->setFont( UIStyle::chipFont() );
	s_joiningChip->setText( TheGameText->fetch( "GUI:OnlineLobbyJoining" ) );
}

static void freeGameListText( void )
{
	for (Int head = 0; head < HEAD_COUNT; ++head)
	{
		if (s_gameHeads[ head ])
			TheDisplayStringManager->freeDisplayString( s_gameHeads[ head ] );
		s_gameHeads[ head ] = NULL;
	}
	if (s_noGames)
		TheDisplayStringManager->freeDisplayString( s_noGames );
	if (s_joiningChip)
		TheDisplayStringManager->freeDisplayString( s_joiningChip );
	s_noGames = s_joiningChip = NULL;
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

/* A game's row (drawGameRow draws it): an open game in INK with an Open chip, a full one MUTED with a
	 Full chip, a started one MUTED with a Playing chip, one of another build or data MUTED with no chip (its
	 tooltip says why it cannot be joined). */
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



	// a typed name and a map's are cut with an ellipsis, never abbreviated
	GameFont *font = win->winGetFont();
	const Int pad = 2 * UIStyle::tallPx( CELL_PAD );
	Int index = GadgetListBoxAddEntryText(win, UIStyle::fitName(gameName, font, GadgetListBoxGetColumnWidth(win, COLUMN_NAME) - pad),
		gameColor, -1, COLUMN_NAME);
	GadgetListBoxSetItemData(win, (void *)(intptr_t)game->getID(), index);

	UnicodeString s;
	const UnicodeString mapName = roomMapName(game->getMap());
	GadgetListBoxAddEntryText(win, UIStyle::fitName(mapName, font, GadgetListBoxGetColumnWidth(win, COLUMN_MAP) - pad),
		UIStyle::MUTED, index, COLUMN_MAP);

	s.format(u"%d / %d", game->getReportedNumPlayers(), game->getReportedMaxPlayers());
	GadgetListBoxAddEntryText(win, s, gameColor, index, COLUMN_NUMPLAYERS);

	if (game->getHasPassword())
	{
		const Image *img = TheMappedImageCollection->findImageByName("Password");
		if (img)
			GadgetListBoxAddEntryImage(win, img, index, COLUMN_PASSWORD, img->getImageHeight(), img->getImageWidth());
	}

	if (playing)
		GadgetListBoxAddEntryText(win, TheGameText->fetch("GUI:OnlineLobbyPlaying"), UIStyle::MUTED, index, COLUMN_STATE);
	else if (!otherBuild)
		GadgetListBoxAddEntryText(win, TheGameText->fetch(full ? "GUI:OnlineLobbyFull" : "GUI:Open"),
			full ? UIStyle::MUTED : UIStyle::ACTIVE, index, COLUMN_STATE);

	Int ping = game->getPingAsInt();
	// -online: every route runs through the relay, so the pinger's figure means nothing (it reads 1000 for
	// everyone); the host's own round trip to the server, as the server last reported it, is the part of the
	// lag a game list can tell, and a host whose card the lobby has not heard is a dash with no light
	if (OnlineIsEnabled())
	{
		GameSlot *host = game->getSlot(0);
		OnlinePlayerInfo card;
		AsciiString hostNick;
		if (host && host->isHuman())
			hostNick.translate(host->getName());
		ping = hostNick.isNotEmpty() && OnlineGetPlayerInfo(hostNick, card) ? card.ping : -1;
	}
	if (ping >= 0)
		s.format(u"%d", ping);
	else
		s = UnicodeString(u"\x2013");
	GadgetListBoxAddEntryText(win, s, gameColor, index, COLUMN_PING);
	// the light's cell is a row high, which is what sets the row's height
	const Image *light = UIStyle::pingImage(ping);
	GadgetListBoxAddEntryImage(win, light, index, COLUMN_LIGHT, UIStyle::tallPx(GAME_ROW_HEIGHT) - 1,
		light ? light->getImageWidth() : UIStyle::tallPx(LIGHT_SIZE));

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
