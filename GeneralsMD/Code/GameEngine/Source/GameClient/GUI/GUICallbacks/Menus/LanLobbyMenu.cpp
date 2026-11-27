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
// FILE: LanLobbyMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/BaseType.h"
#include "Common/crc.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/QuotedPrintable.h"
#include "Common/UserPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameInfoWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/LANGameInfo.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameClient/Display.h"
#include "GameClient/UIStyle.h"

Bool LANisShuttingDown = false;
Bool LANbuttonPushed = false;
Bool LANSocketErrorDetected = FALSE;
char *LANnextScreen = NULL;

static Int	initialGadgetDelay = 2;
static Bool justEntered = FALSE;



LANPreferences::LANPreferences( void )
{
	// note, the superclass will put this in the right dir automatically, this is just a leaf name
	load("Network.ini");
}

LANPreferences::~LANPreferences()
{
}

UnicodeString LANPreferences::getUserName(void)
{
	UnicodeString ret;
	LANPreferences::const_iterator it = find("UserName");
	if (it == end())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}

	ret = QuotedPrintableToUnicodeString(it->second);
	ret.trim();
	if (ret.isEmpty())
	{
		IPEnumeration IPs;
		ret.translate(IPs.getMachineName());
		return ret;
	}
	
	return ret;
}

Int LANPreferences::getPreferredColor(void)
{
	Int ret;
	LANPreferences::const_iterator it = find("Color");
	if (it == end())
	{
		return -1;
	}

	ret = atoi(it->second.str());
	if (ret < -1 || ret >= TheMultiplayerSettings->getNumColors())
		ret = -1;

	return ret;
}

Int LANPreferences::getPreferredFaction(void)
{
	Int ret;
	LANPreferences::const_iterator it = find("PlayerTemplate");
	if (it == end())
	{
		return PLAYERTEMPLATE_RANDOM;
	}

	ret = PlayerTemplateFromPreference(it->second);
	if (ret == PLAYERTEMPLATE_OBSERVER || !IsValidPlayerTemplatePick(ret))
		ret = PLAYERTEMPLATE_RANDOM;

	if (ret >= 0)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(ret);
		if (!fac)
			ret = PLAYERTEMPLATE_RANDOM;
		else if (fac->getStartingBuilding().isEmpty())
			ret = PLAYERTEMPLATE_RANDOM;
	}

	return ret;
}

Bool LANPreferences::usesSystemMapDir(void)
{
	OptionPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

AsciiString LANPreferences::getPreferredMap(void)
{
	AsciiString ret;
	LANPreferences::const_iterator it = find("Map");
	if (it == end())
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}

	ret = QuotedPrintableToAsciiString(it->second);
	ret.trim();
	// a mode's generated map is that mode room's, never a classic room's starting map
	if (ret.isEmpty() || !isValidMap(ret, TRUE) || IsDirectStrikeMap(ret) || IsApocalypseMap(ret))
	{
		ret = getDefaultMap(TRUE);
		return ret;
	}
	
	return ret;
}

Int LANPreferences::getNumRemoteIPs(void)
{
	Int ret;
	LANPreferences::const_iterator it = find("NumRemoteIPs");
	if (it == end())
	{
		ret = 0;
		return ret;
	}

	ret = atoi(it->second.str());
	return ret;
}

UnicodeString LANPreferences::getRemoteIPEntry(Int i)
{
	UnicodeString ret;
	AsciiString key;
	key.format("RemoteIP%d", i);

	AsciiString ipstr;
	AsciiString asciientry;

	LANPreferences::const_iterator it = find(key.str());
	if (it == end())
	{
		asciientry = "";
		return ret;
	}

	asciientry = it->second;

	asciientry.nextToken(&ipstr, ":");
	asciientry.set(asciientry.str() + 1); // skip the ':'

	ret.translate(ipstr);
	if (asciientry.getLength() > 0)
	{
		ret.concat(u"(");
		ret.concat(QuotedPrintableToUnicodeString(asciientry));
		ret.concat(u")");
	}

	return ret;
}

static const char superweaponRestrictionKey[] = "SuperweaponRestrict";

Int LANPreferences::getSuperweaponRestriction(void) const
{
  LANPreferences::const_iterator it = find(superweaponRestrictionKey);
  if (it == end())
  {
    return SUPERWEAPONS_ALLOW;
  }

  return SuperweaponRestrictionFromPreference( it->second );
}

void LANPreferences::setSuperweaponRestriction( Int restriction )
{
  AsciiString val;
  val.format( "%d", restriction );
  (*this)[superweaponRestrictionKey] = val;
}

static const char startingCashKey[] = "StartingCash";
Money LANPreferences::getStartingCash(void) const
{
  LANPreferences::const_iterator it = find(startingCashKey);
  if (it == end())
  {
    return TheMultiplayerSettings->getDefaultStartingMoney();
  }

  return TheMultiplayerSettings->getListedStartingMoney( strtoulAsWindows( it->second.str() ) );
}

void LANPreferences::setStartingCash( const Money & startingCash )
{
  AsciiString option;

  option.format( "%d", startingCash.countMoney() );

  (*this)[startingCashKey] = option;
}

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////


// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonClearID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType buttonDirectConnectID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType staticToolTipID = NAMEKEY_INVALID;
static NameKeyType textEntryPlayerNameID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType listboxPlayersID = NAMEKEY_INVALID;
static NameKeyType staticTextGameInfoID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanLobby = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonClear = NULL;
static GameWindow *buttonHost = NULL;
static GameWindow *buttonJoin = NULL;
static GameWindow *buttonDirectConnect = NULL;
static GameWindow *buttonEmote = NULL;
static GameWindow *staticToolTip = NULL;
static GameWindow *textEntryPlayerName = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *staticTextGameInfo = NULL;

// Create Game asks the three modes (AskGameMode) and its answer comes here, for a Classic room or one that
// stands on a Direct Strike island or an Apocalypse map from the start (TheHostGameMode, spent by the room)
static void hostRoom( Int mode )
{
	if (TheLAN == NULL)
		return;	// the lobby went while the question was up
	TheHostGameMode = mode;
	TheLAN->RequestGameCreate( UnicodeString(u""), FALSE );
}

//external declarations of the Gadgets the callbacks can use
NameKeyType listboxChatWindowID = NAMEKEY_INVALID;
GameWindow *listboxChatWindow = NULL;
GameWindow *listboxPlayers = NULL;
NameKeyType listboxGamesID = NAMEKEY_INVALID;
GameWindow *listboxGames = NULL;

// hack to disable framerate limiter in LAN games
//static Bool shellmapOn;
static Bool useFpsLimit;
static UnicodeString defaultName;

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int x, y, row, col;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	GadgetListBoxGetEntryBasedOnXY(window, x, y, row, col);

	if (row == -1 || col == -1)
	{
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}

	// Back deletes TheLAN at once and the lobby stays on screen while its exit animation plays, so the
	// pointer can still be resting on this list.
	if (!TheLAN)
		return;

	UnsignedInt playerIP = (UnsignedInt)(uintptr_t)GadgetListBoxGetItemData( window, row, col );
	LANPlayer *player = TheLAN->LookupPlayer(playerIP);
	if (!player)
	{
		DEBUG_CRASH(("No player info in listbox!"));
		//TheMouse->setCursorTooltip( TheGameText->fetch("TOOLTIP:LobbyPlayers") );
		return;
	}
	UnicodeString tooltip;
	tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getName().str(), player->getLogin().str(), player->getHost().str());
	TheMouse->setCursorTooltip( tooltip );
}


//-------------------------------------------------------------------------------------------------
// The lobby in the menu language (MENU-REDESIGN.md package 10, README-game-lists.md "lists-lan"): frame A
// round the board, the title with "Local network", the player's name at the title row's right end, the
// games (LobbyUtils.cpp's list), the chat, the picked game (GameInfoWindow.cpp) over the players, the keys.
// Every number is at 1080 on the board; Tools/lanlobby_layout.py holds the windows' rects.
//-------------------------------------------------------------------------------------------------
static GameWinDrawFunc s_panelDraw = NULL;
static Int s_pickedOffset = -2;			///< the list row the picked block shows, -1 for none
static Int s_keysLit = -1;					///< which key is lit: 1 Join Game, 2 Create Game
static Bool s_joinLit = FALSE;
static Int s_playersTop = 752;			///< the players' head on the board

/// a listed game as its row reads
static Bool lanRow( LANGameInfo *game, LobbyGameRow &row )
{
	if (game == NULL)
		return FALSE;
	row.name = game->getPlayerName( 0 );
	row.mode = LobbyMapMode( game->getMap() );
	row.map = LobbyMapName( game->getMap() );
	row.seated = 0;
	row.seats = 0;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot( i );
		if (slot == NULL || slot->getState() == SLOT_CLOSED)
			continue;
		++row.seats;
		if (slot->isOccupied())
			++row.seated;
	}
	row.ping = -1;
	row.password = FALSE;
	// a LAN announcement carries no build: the host refuses another build's join, and says so
	row.state = LobbyGameStateOf( FALSE, game->isGameInProgress(), FALSE, row.seated >= row.seats );
	return TRUE;
}

static void drawLanGameRow( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip )
{
	(void)clip;
	LobbyGameRow game;
	if (TheLAN && lanRow( TheLAN->LookupGameByListOffset( row ), game ))
		LobbyDrawGameRow( row, game, rect, TRUE );
}

static void drawLanGames( GameWindow *window, WinInstanceData * )
{
	LobbyDrawRows( window, drawLanGameRow );
	LobbyDrawGameFoot( window, FALSE, TheGameText->fetch( "GUI:LobbyEmptyLan" ) );
}

static void drawLanPlayerRow( GameWindow *list, Int row, const IRegion2D &rect, const IRegion2D &clip )
{
	IRegion2D inset = rect;
	inset.lo.x += UIStyle::px( 2.0f );
	LobbyDrawCell( list, row, 0, inset, clip, LOBBY_ALIGN_LEFT );
}

/// the players, the local one gold
static void drawLanPlayers( GameWindow *window, WinInstanceData * )
{
	Int local = -1;
	for (Int row = 0; TheLAN && row < GadgetListBoxGetNumEntries( window ); ++row)
		if ((UnsignedInt)(uintptr_t)GadgetListBoxGetItemData( window, row, 0 ) == TheLAN->GetLocalIP())
			local = row;
	LobbyDrawRows( window, drawLanPlayerRow, local );
}

/// the panel: frame A on its glass, the head, the games', the chat's and the players' heads, the foot's hint
static void drawLanPanel( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D panel = UIStyle::boardRect( UIStyle::MENU_PANEL_LEFT, UIStyle::MENU_PANEL_TOP, UIStyle::MENU_PANEL_RIGHT,
		UIStyle::MENU_PANEL_BOTTOM );
	UIStyle::glass( panel, 168 );
	UIStyle::frameA( panel );

	// the title in 40 px bold spaced capitals from the content's 60, "Local network" after it, the lit rule
	GameFont *titleFont = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE, TRUE );
	const Int titleMiddle = UIStyle::boardY( 70.0f );
	const UnicodeString title = UIStyle::capitals( TheGameText->fetch( "GUI:LANLobby" ) );
	const Int titleWidth = UIStyle::drawTracked( title, titleFont, UIStyle::boardX( 60.0f ), titleMiddle - titleFont->height / 2,
		UIStyle::INK, 4.0f );
	LobbyDrawWords( 13, TheGameText->fetch( "GUI:LocalNetwork" ), 20, FALSE, UIStyle::boardX( 60.0f ) + titleWidth + UIStyle::px( 24.0f ),
		titleMiddle + UIStyle::px( 4.0f ), UIStyle::MUTED );
	UIStyle::litLine( panel.lo.x, UIStyle::boardY( (Real)UIStyle::MENU_HEADER_RULE ), panel.hi.x - panel.lo.x, max( 1, UIStyle::px( 2.0f ) ),
		UIStyle::FRAME );
	GameFont *label = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	const UnicodeString nameLabel = UIStyle::capitals( TheGameText->fetch( "GUI:LobbyPlayerName" ) );
	UIStyle::drawTracked( nameLabel, label, UIStyle::boardX( 1346.0f ) - UIStyle::trackedWidth( nameLabel, label, UIStyle::MENU_LABEL_TRACKING ),
		titleMiddle - label->height / 2, UIStyle::INK, UIStyle::MENU_LABEL_TRACKING );

	// the games' head with its count; the chat's; the players' with theirs
	Int open = 0, playing = 0;
	for (LANGameInfo *game = TheLAN ? TheLAN->LookupGameByListOffset( 0 ) : NULL; game; game = game->getNext())
	{
		LobbyGameRow row;
		lanRow( game, row );
		if (row.state == LOBBY_STATE_PLAYING)
			++playing;
		else if (row.state == LOBBY_STATE_OPEN)
			++open;
	}
	UnicodeString count;
	if (playing > 0)
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCount" ), open, playing );
	else
		count.format( TheGameText->fetch( "GUI:OnlineLobbyCountOpen" ), open );
	LobbyDrawGameHead( 132, TRUE, count, -1 );
	LobbyDrawSection( TheGameText->fetch( "GUI:RoomChat" ), UnicodeString::TheEmptyString, UIStyle::ONLINE_LEFT, 454, 1310, 14 );
	count.format( u"%d", listboxPlayers ? GadgetListBoxGetNumEntries( listboxPlayers ) : 0 );
	LobbyDrawSection( TheGameText->fetch( "GUI:Players" ), count, 1334, s_playersTop, 1860, 15 );

	const Int hintLeft = 684;
	if (!LANbuttonPushed)
		LobbyDrawWords( 12, TheGameText->fetch( s_joinLit ? "GUI:LobbyHintJoin" : "GUI:LobbyHintHost" ), UIStyle::MENU_TEXT_HINT, FALSE,
			UIStyle::boardX( (Real)hintLeft ), UIStyle::boardY( 1002.0f ), UIStyle::MUTED );
	(void)window;
	(void)instData;
}

/// the players under the picked game, or the whole column while nothing is picked
static void placeLanPlayers( Bool picked )
{
	s_playersTop = picked ? 752 : 130;
	LobbyPlaceOnBoard( listboxPlayers, 1334, s_playersTop + 34, 1860, 946 );
}

/* Each frame: the first game is picked while games exist and none is; the picked block follows the pick; Join
	 Game takes any game, lit for one it can take, else Create Game is the lit key. */
static void syncLanPicked( void )
{
	if (TheLAN == NULL || listboxGames == NULL || buttonJoin == NULL || buttonHost == NULL)
		return;
	Int selected = -1;
	GadgetListBoxGetSelected( listboxGames, &selected );
	if (selected < 0 && GadgetListBoxGetNumEntries( listboxGames ) > 0)
	{
		GadgetListBoxSetSelected( listboxGames, 0 );
		selected = 0;
	}
	LANGameInfo *game = selected >= 0 ? TheLAN->LookupGameByListOffset( selected ) : NULL;
	const Int offset = game ? selected : -1;
	if (offset != s_pickedOffset)
	{
		s_pickedOffset = offset;
		placeLanPlayers( game != NULL );
		if (game)
			RefreshGameInfoWindow( game, game->getName() );
		else
			HideGameInfoWindow( TRUE );
	}
	LobbyGameRow row;
	s_joinLit = lanRow( game, row ) && row.state == LOBBY_STATE_OPEN;
	buttonJoin->winEnable( game != NULL );
	const Int lit = s_joinLit ? 1 : 2;
	if (lit != s_keysLit)
	{
		s_keysLit = lit;
		LobbyMenuKey( buttonJoin, lit == 1 );
		LobbyMenuKey( buttonHost, lit == 2 );
	}
}

/// the look on the master's windows, from init
static void initLanLook( void )
{
	GameWindow *panel = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "LanLobbyMenu.wnd:GadgetParent" ) );
	if (panel && panel->winGetDrawFunc() != drawLanPanel)
	{
		s_panelDraw = panel->winGetDrawFunc();
		panel->winSetDrawFunc( drawLanPanel );
	}
	(void)s_panelDraw;
	if (listboxGames)
	{
		static const Int percent[ 2 ] = { 100, 0 };
		LobbySetColumns( listboxGames, 2, percent );
		listboxGames->winSetDrawFunc( drawLanGames );
	}
	if (listboxPlayers)
	{
		GadgetListBoxSetFont( listboxPlayers, UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ) );
		listboxPlayers->winSetDrawFunc( drawLanPlayers );
	}
	LobbyMenuChat( listboxChatWindow, UIStyle::MENU_TEXT_LABEL + 2 );
	LobbyMenuEntry( textEntryChat, UIStyle::MENU_TEXT_BODY, UIStyle::INK );
	LobbyMenuEntry( textEntryPlayerName, UIStyle::MENU_TEXT_ROW_FIGURE, UIStyle::GOLD );
	GameWindow *keys[] = { buttonBack, buttonDirectConnect, buttonClear, buttonEmote };
	for (Int k = 0; k < (Int)ARRAY_SIZE( keys ); ++k)
		LobbyMenuKey( keys[ k ], FALSE );
	s_pickedOffset = -2;
	s_keysLit = -1;
	s_joinLit = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Lobby Menu */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuInit( WindowLayout *layout, void *userData )
{
	LANnextScreen = NULL;
	LANbuttonPushed = false;
	LANisShuttingDown = false;

	// get the ids for our controls
	parentLanLobbyID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:LanLobbyMenuParent" ) );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonBack" ) );
	buttonClearID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonClear" ) );
	buttonHostID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonHost" ) );
	buttonJoinID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonJoin" ) );
	buttonDirectConnectID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonDirectConnect" ) );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ButtonEmote" ) );
	staticToolTipID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:StaticToolTip" ) );
	textEntryPlayerNameID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:TextEntryPlayerName" ) );
	textEntryChatID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:TextEntryChat" ) );
	listboxPlayersID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxPlayers" ) );
	listboxChatWindowID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxChatWindowLanLobby" ) );
	listboxGamesID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:ListboxGames" ) );
	staticTextGameInfoID = TheNameKeyGenerator->nameToKey( AsciiString( "LanLobbyMenu.wnd:StaticTextGameInfo" ) );


	// Get pointers to the window buttons
	parentLanLobby = TheWindowManager->winGetWindowFromId( NULL, parentLanLobbyID );
	buttonBack = TheWindowManager->winGetWindowFromId( NULL,  buttonBackID);
	buttonClear = TheWindowManager->winGetWindowFromId( NULL,  buttonClearID);
	buttonHost = TheWindowManager->winGetWindowFromId( NULL, buttonHostID );
	buttonJoin = TheWindowManager->winGetWindowFromId( NULL, buttonJoinID );
	buttonDirectConnect = TheWindowManager->winGetWindowFromId( NULL, buttonDirectConnectID );
	buttonEmote = TheWindowManager->winGetWindowFromId( NULL,buttonEmoteID  );
	staticToolTip = TheWindowManager->winGetWindowFromId( NULL, staticToolTipID );
	textEntryPlayerName = TheWindowManager->winGetWindowFromId( NULL, textEntryPlayerNameID );
	textEntryChat = TheWindowManager->winGetWindowFromId( NULL, textEntryChatID );
	listboxPlayers = TheWindowManager->winGetWindowFromId( NULL, listboxPlayersID );
	listboxChatWindow = TheWindowManager->winGetWindowFromId( NULL, listboxChatWindowID );
	listboxGames = TheWindowManager->winGetWindowFromId( NULL, listboxGamesID );
	staticTextGameInfo = TheWindowManager->winGetWindowFromId( NULL, staticTextGameInfoID );
	listboxPlayers->winSetTooltipFunc(playerTooltip);
	initLanLook();

	// Show Menu
	layout->hide( FALSE );

	// Init LAN API Singleton
	if (!TheLAN)
	{
		TheLAN = NEW LANAPI();	/// @todo clh delete TheLAN and 
		useFpsLimit = TheGlobalData->m_useFpsLimit;
	}
	else
	{
		TheWritableGlobalData->m_useFpsLimit = useFpsLimit;
		TheLAN->reset();
	}

	// Choose an IP address, then initialize the LAN singleton
	UnsignedInt IP = TheGlobalData->m_defaultIP;
	IPEnumeration IPs;

	if (!IP)
	{
		EnumeratedIP *IPlist = IPs.getAddresses();
		/*
		while (IPlist && IPlist->getNext())
		{
			IPlist = IPlist->getNext();
		}
		*/
		DEBUG_ASSERTCRASH(IPlist, ("No IP addresses found!"));
		if (!IPlist)
		{
			/// @todo: display error and exit lan lobby if no IPs are found
		}

		//UnicodeString str;
		//str.format(L"Local IP chosen: %hs", IPlist->getIPstring().str());
		//GadgetListBoxAddEntryText(listboxChatWindow, str, chatSystemColor, -1, 0);
		IP = IPlist->getIP();
	}
	else
	{
		/*
		UnicodeString str;
		str.format(L"Default local IP: %d.%d.%d.%d",
			(IP >> 24),
			(IP >> 16) & 0xFF,
			(IP >> 8) & 0xFF,
			IP & 0xFF);
		GadgetListBoxAddEntryText(listboxChatWindow, str, chatSystemColor, -1, 0);
		*/
	}

	// TheLAN->init() sets us to be in a LAN menu screen automatically.
	TheLAN->init();
	if (TheLAN->SetLocalIP(IP) == FALSE) {
		LANSocketErrorDetected = TRUE;
	}

	//Initialize the gadgets on the window
	//UnicodeString	txtInput;
	//txtInput.translate(IPs.getMachineName());
	LANPreferences prefs;
	/* -lanname beats the preferences file, because two copies on one machine share that file and a
		 host denies the second one as a duplicate name.  See parseLanName in CommandLine.cpp. */
	if (TheGlobalData->m_lanPlayerName.isNotEmpty())
		defaultName.translate( TheGlobalData->m_lanPlayerName );
	else
		defaultName = prefs.getUserName();
	while (defaultName.getLength() > g_lanPlayerNameLength)
		defaultName.removeLastChar();
	GadgetTextEntrySetText( textEntryPlayerName, defaultName);
	// Clear the text entry line
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	GadgetListBoxReset(listboxPlayers);
	GadgetListBoxReset(listboxGames);

	while (defaultName.getLength() > g_lanPlayerNameLength)
		defaultName.removeLastChar();
	TheLAN->RequestSetName(defaultName);
	TheLAN->RequestLocations();

	/*
	UnicodeString unicodeChat;

	unicodeChat = L"Local IP list:";
	GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);

	IPlist = IPs.getAddresses();
	while (IPlist)
	{
		unicodeChat.translate(IPlist->getIPstring());
		GadgetListBoxAddEntryText(listboxChatWindow, unicodeChat, chatSystemColor, -1, 0);
		IPlist = IPlist->getNext();
	}
	*/

	// Set Keyboard to Main Parent
	//TheWindowManager->winSetFocus( parentLanLobby );
	TheWindowManager->winSetFocus( textEntryChat );
	CreateLANGameInfoWindow(staticTextGameInfo);

	//TheShell->showShellMap(FALSE);
	//shellmapOn = FALSE;
	// coming out of a game, re-load the shell map
	TheShell->showShellMap(TRUE);
		
	// check for MOTD
	TheLAN->checkMOTD();
	layout->hide(FALSE);
	layout->bringForward();

	justEntered = TRUE;
	initialGadgetDelay = 2;
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("LanLobbyMenu.wnd:GadgetParent"));
	if(win)
		win->winHide(TRUE);

	
	// animate controls
	//TheShell->registerWithAnimateManager(parentLanLobby, WIN_ANIMATION_SLIDE_TOP, TRUE);
//	TheShell->registerWithAnimateManager(buttonHost, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonJoin, WIN_ANIMATION_SLIDE_LEFT, TRUE, 400);
//	TheShell->registerWithAnimateManager(buttonDirectConnect, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//	//TheShell->registerWithAnimateManager(buttonOptions, WIN_ANIMATION_SLIDE_LEFT, TRUE, 1);
//	TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

} // GameLobbyMenuInit

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	LANisShuttingDown = false;

	// hide the layout; the panel drew through the fade, so its words go only now
	layout->hide( TRUE );
	LobbyFreeText();

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != NULL) );

	if (LANnextScreen != NULL)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuShutdown( WindowLayout *layout, void *userData )
{
	/* A name that came from -lanname is this copy's, not this machine's: writing it back would put
		 the second copy's name in the preferences both copies read. */
	if (TheGlobalData->m_lanPlayerName.isEmpty())
	{
		LANPreferences prefs;
		prefs["UserName"] = UnicodeStringToQuotedPrintable(GadgetTextEntryGetText( textEntryPlayerName ));
		prefs.write();
	}

	DestroyGameInfoWindow();
	// hide menu
	//layout->hide( TRUE );

	// the shell can be torn down during the exit animation, when Back has already deleted TheLAN
	if (TheLAN)
		TheLAN->RequestLobbyLeave( true );

	// Reset the LAN singleton
	//TheLAN->reset();

	// our shutdown is complete
	//TheShell->shutdownComplete( layout );
	TheWritableGlobalData->m_useFpsLimit = useFpsLimit;

	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;

	LANSocketErrorDetected = FALSE;

	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanLobbyFade");
	//if(	shellmapOn)
//		TheShell->showShellMap(TRUE);
}  // LanLobbyMenuShutdown


//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu update method */
//-------------------------------------------------------------------------------------------------
void LanLobbyMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_ENTERED_FROM_GAME);
	}

	if(justEntered)
	{
		if(initialGadgetDelay == 1)
		{
			TheTransitionHandler->setGroup("LanLobbyFade");
			initialGadgetDelay = 2;
			justEntered = FALSE;
		}
		else
			initialGadgetDelay--;
	}

	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (TheShell->isAnimFinished() && !LANbuttonPushed && TheLAN)
		TheLAN->update();
	if (!LANbuttonPushed)
		syncLanPicked();

	if (LANSocketErrorDetected == TRUE) {
		LANSocketErrorDetected = FALSE;
		DEBUG_LOG(("SOCKET ERROR!  BAILING!\n"));
		// The socket opened, but the system put it on another adapter than the LAN IP chosen (issue 146).
		const UnsignedInt redirectedIP = TheLAN->GetRedirectedIP();
		if (redirectedIP)
		{
			const UnsignedInt askedIP = TheLAN->GetLocalIP();
			UnicodeString asked, bound, text;
			asked.format(u"%d.%d.%d.%d", askedIP >> 24, (askedIP >> 16) & 0xff, (askedIP >> 8) & 0xff, askedIP & 0xff);
			bound.format(u"%d.%d.%d.%d", redirectedIP >> 24, (redirectedIP >> 16) & 0xff, (redirectedIP >> 8) & 0xff, redirectedIP & 0xff);
			text.format(TheGameText->fetch("GUI:SocketRedirected").str(), asked.str(), bound.str());
			MessageBoxOk(TheGameText->fetch("GUI:NetworkError"), text, NULL);
		}
		else
			MessageBoxOk(TheGameText->fetch("GUI:NetworkError"), TheGameText->fetch("GUI:SocketError"), NULL);

		// we have a socket problem, back out to the main menu.
		TheWindowManager->winSendSystemMsg(buttonBack->winGetParent(), GBM_SELECTED,
																			 (WindowMsgData)buttonBack, buttonBackID);
	}

	
}// LanLobbyMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (LANbuttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							(WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}// LanLobbyMenuInput

//-------------------------------------------------------------------------------------------------
/** Lan Lobby menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanLobbyMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_OPENED);
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				SignalUIInteraction(SHELL_SCRIPT_HOOK_LAN_CLOSED);
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:
		case GLM_DOUBLE_CLICKED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID ) 
				{
					int rowSelected = mData2;
				
					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLAN->RequestGameJoin(theGame);
						}
					}
				}
				break;
			}
		case GLM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxGamesID ) 
				{
					int rowSelected = mData2;
					if( rowSelected < 0 )
					{
						HideGameInfoWindow(TRUE);
						break;
					}
					LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
					if (theGame)
						RefreshGameInfoWindow(theGame, theGame->getName());
					else
						HideGameInfoWindow(TRUE);

				}
				break;
			}
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					//shellmapOn = TRUE;
					LANbuttonPushed = true;
					DEBUG_LOG(("Back was hit - popping to main menu\n"));
					TheShell->pop();
					delete TheLAN;
					TheLAN = NULL;
					//TheTransitionHandler->reverse("LanLobbyFade");

				} //if ( controlID == buttonBack )
				else if ( controlID == buttonHostID )
				{
					// which of the three modes the room is for; the answer creates it (hostRoom)
					AskGameMode( hostRoom, FALSE );

				}//else if ( controlID == buttonHostID )
				else if ( controlID == buttonClearID )
				{
					GadgetTextEntrySetText(textEntryPlayerName, UnicodeString::TheEmptyString);
					TheWindowManager->winSendSystemMsg( window, 
																						GEM_UPDATE_TEXT,
																						(WindowMsgData)textEntryPlayerName, 
																						0 );

				}
				else if ( controlID == buttonJoinID )
				{

					//TheShell->push( AsciiString("Menus/LanGameOptionsMenu.wnd") );

					int rowSelected = -1;
					GadgetListBoxGetSelected( listboxGames, &rowSelected );

					if (rowSelected >= 0)
					{
						LANGameInfo * theGame = TheLAN->LookupGameByListOffset(rowSelected);
						if (theGame)
						{
							TheLAN->RequestGameJoin(theGame);
						}
					}
					else
					{
						GadgetListBoxAddEntryText(listboxChatWindow, TheGameText->fetch("LAN:ErrorNoGameSelected") , chatSystemColor, -1, 0);
					}

				} //else if ( controlID == buttonJoinID )
				else if ( controlID == buttonEmoteID )
				{
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();
					// Echo the user's input to the chat window
					if (!txtInput.isEmpty()) {
//						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);
					}
				} //if ( controlID == buttonEmote )
				else if (controlID == buttonDirectConnectID)
				{
					TheLAN->RequestLobbyLeave( false );
					TheShell->push(AsciiString("Menus/NetworkDirectConnect.wnd"));
				}
				
				break;
			}// case GBM_SELECTED:
	
		case GEM_UPDATE_TEXT:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					const WideChar *c = txtInput.str();
					while (c && (WideCharIsSpace(*c)))
						c++;

					if (c)
						txtInput = UnicodeString(c);
					else
						txtInput = UnicodeString::TheEmptyString;

					while (txtInput.getLength() > g_lanPlayerNameLength)
						txtInput.removeLastChar();
					
					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == u',')
						txtInput.removeLastChar(); // we use , for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == u':')
						txtInput.removeLastChar(); // we use : for strtok's so we can't allow them in names.  :(

					if (!txtInput.isEmpty() && txtInput.getCharAt(txtInput.getLength()-1) == u';')
						txtInput.removeLastChar(); // we use ; for strtok's so we can't allow them in names.  :(

					// send it over the network
					if (!txtInput.isEmpty())
						TheLAN->RequestSetName(txtInput);
					else
						{
							TheLAN->RequestSetName(defaultName);
						}

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}// if ( controlID == textEntryPlayerNameID )
				break;
			}//case GEM_UPDATE_TEXT:
		case GEM_EDIT_DONE:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// Take the user's input and echo it into the chat window as well as
				// send it to the other clients on the lan
				if ( controlID == textEntryChatID )
				{
					
					// read the user's input
					txtInput.set(GadgetTextEntryGetText( textEntryChat ));
					// Clear the text entry line
					GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
					// Clean up the text (remove leading/trailing chars, etc)
					while (!txtInput.isEmpty() && WideCharIsSpace(txtInput.getCharAt(0)))
						txtInput = UnicodeString(txtInput.str()+1);

					// Echo the user's input to the chat window
					if (!txtInput.isEmpty())
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);
					else if (s_joinLit && buttonJoin)
						// Enter on an empty line joins the picked game, as the foot's hint says
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)buttonJoin, buttonJoinID );

				}// if ( controlID == textEntryChatID )
				/*
				else if ( controlID == textEntryPlayerNameID )
				{
					// grab the user's name
					txtInput.set(GadgetTextEntryGetText( textEntryPlayerName ));

					// Clean up the text (remove leading/trailing chars, etc)
					txtInput.trim();

					// send it over the network
					if (!txtInput.isEmpty())
						TheLAN->RequestSetName(txtInput);

					// Put the whitespace-free version in the box
					GadgetTextEntrySetText( textEntryPlayerName, txtInput );

				}// if ( controlID == textEntryPlayerNameID )
				*/
				break;
			}
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// LanLobbyMenuSystem
