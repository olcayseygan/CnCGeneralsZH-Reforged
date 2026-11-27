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

// FILE: MainMenu.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: Main menu window callbacks
//
// The menu is one rail on the right in the menu language (docs/plans/MENU-REDESIGN.md, package 4;
// the design is experiments/proofs/2026-11-24/screens/main-menu/ in the wrapper): the game's name,
// the open list's buttons, the leaving button set apart.  Solo Play and Load swap the rail's list;
// a side, Generals' Challenge and Mods open a panel to the rail's left.  EA's drop-downs and their
// window transitions are gone: every window comes from MainMenu.wnd (Tools/mainmenu_layout.py) and
// is placed here at one scale from the screen's right edge (GameClient/MainMenuLayout.h) and drawn
// here.  Multiplayer opens the LAN lobby; online play is entered from the launcher, which starts the
// game with -online and -onlinelogin (Shell::showShell), and only such a game has an ONLINE key.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "gamespy/ghttp/ghttp.h"

#include "Lib/BaseType.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "Common/UserPreferences.h"
#include "Common/version.h"
#include "Common/GameLOD.h"
#include "Common/FileSystem.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ExtendedMessageBox.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/MainMenuLayout.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GUIUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/HotKey.h"
#include "GameClient/GameClient.h"
#include "GameClient/GUICallbacks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"

#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"

#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/Online/OnlineQueues.h"

#include "GameClient/CDCheck.h"
//Added By Saad
//for accessing the InGameUI
#include "GameClient/InGameUI.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

static Bool raiseMessageBoxes = TRUE;
static Bool campaignSelected = FALSE;
#if defined _DEBUG || defined _INTERNAL || defined _PROFILE
static GameWindow *buttonCampaign = NULL;
#ifdef TEST_COMPRESSION
static GameWindow *buttonCompressTest = NULL;
void DoCompressTest( void );
#endif // TEST_COMPRESSION
#endif

extern Bool skirmishDirectStrike;
extern Bool skirmishApocalypse;

// window pointers --------------------------------------------------------------------------------
static NameKeyType mainMenuID = NAMEKEY_INVALID;
static GameWindow *parentMainMenu = NULL;

// the main rail (MapBorder2)
static GameWindow *buttonSinglePlayer = NULL;
static GameWindow *buttonMultiPlayer = NULL;
static GameWindow *buttonOnline = NULL;
static GameWindow *buttonLoadReplay = NULL;
static GameWindow *buttonOptions = NULL;
static GameWindow *buttonCredits = NULL;
static GameWindow *buttonExit = NULL;
// the Solo Play rail (MapBorder)
static GameWindow *buttonUSA = NULL;
static GameWindow *buttonGLA = NULL;
static GameWindow *buttonChina = NULL;
static GameWindow *buttonChallenge = NULL;
static GameWindow *buttonSkirmish = NULL;
static GameWindow *buttonMods = NULL;
static GameWindow *buttonSingleBack = NULL;
// the Load rail (MapBorder3)
static GameWindow *buttonLoad = NULL;
static GameWindow *buttonReplay = NULL;
static GameWindow *buttonLoadReplayBack = NULL;
// the campaign panel: the tabs (MapBorder6), the mission tiles (MapBorder5), the difficulty keys (MapBorder4)
static GameWindow *buttonCampaignGenerals = NULL;
static GameWindow *buttonCampaignZH = NULL;
enum { MISSION_BUTTONS = 7 };
static GameWindow *buttonMission[MISSION_BUTTONS];
static AsciiString missionNames[MISSION_BUTTONS];
static GameWindow *buttonEasy = NULL;
static GameWindow *buttonMedium = NULL;
static GameWindow *buttonHard = NULL;
// the Mods panel (MapBorder7)
static GameWindow *buttonModeDirectStrike = NULL;
static GameWindow *buttonModeApocalypse = NULL;
static GameWindow *labelVersion = NULL;
static GameWindow *buttonWorldBuilder = NULL;
static GameWindow *getUpdate = NULL;

// the frames, each an outer and an inner window as EA nested them
enum MenuFrame
{
	FRAME_MAIN,				// MapBorder2
	FRAME_SOLO,				// MapBorder
	FRAME_LOAD,				// MapBorder3
	FRAME_MULTI,			// MapBorder1, EA's Multiplayer list: never shown
	FRAME_TABS,				// MapBorder6
	FRAME_TILES,			// MapBorder5
	FRAME_KEYS,				// MapBorder4
	FRAME_MODS,				// MapBorder7
	FRAME_COUNT
};
static const char *const FRAME_NAMES[ FRAME_COUNT ][ 2 ] =
{
	{ "MainMenu.wnd:MapBorder2", "MainMenu.wnd:EarthMap2" },
	{ "MainMenu.wnd:MapBorder", "MainMenu.wnd:EarthMap" },
	{ "MainMenu.wnd:MapBorder3", "MainMenu.wnd:EarthMap3" },
	{ "MainMenu.wnd:MapBorder1", "MainMenu.wnd:EarthMap1" },
	{ "MainMenu.wnd:MapBorder6", "MainMenu.wnd:EarthMap6" },
	{ "MainMenu.wnd:MapBorder5", "MainMenu.wnd:EarthMap5" },
	{ "MainMenu.wnd:MapBorder4", "MainMenu.wnd:EarthMap4" },
	{ "MainMenu.wnd:MapBorder7", "MainMenu.wnd:EarthMap7" },
};
static GameWindow *frames[ FRAME_COUNT ][ 2 ];

// EA's windows the rail does without: hidden whenever the menu shows
static const char *const RETIRED_WINDOWS[] =
{
	"MainMenu.wnd:MainMenuRuler", "MainMenu.wnd:Logo", "MainMenu.wnd:GreenDot", "MainMenu.wnd:Clock",
	"MainMenu.wnd:WinGrowMarker", "MainMenu.wnd:ButtonGetMapPack", "MainMenu.wnd:ButtonGetUpdate",
	"MainMenu.wnd:ButtonUSARecentSave", "MainMenu.wnd:ButtonUSALoadGame", "MainMenu.wnd:ButtonGLARecentSave",
	"MainMenu.wnd:ButtonGLALoadGame", "MainMenu.wnd:ButtonChinaRecentSave", "MainMenu.wnd:ButtonChinaLoadGame",
	"MainMenu.wnd:WinFactionTraining", "MainMenu.wnd:WinFactionTrainingSmall", "MainMenu.wnd:WinFactionTrainingMedium",
	"MainMenu.wnd:WinFactionSkirmish", "MainMenu.wnd:WinFactionSkirmishSmall", "MainMenu.wnd:WinFactionSkirmishMedium",
	"MainMenu.wnd:WinFactionUS", "MainMenu.wnd:WinFactionUSSmall", "MainMenu.wnd:WinFactionUSMedium",
	"MainMenu.wnd:WinFactionGLA", "MainMenu.wnd:WinFactionGLASmall", "MainMenu.wnd:WinFactionGLAMedium",
	"MainMenu.wnd:WinFactionChina", "MainMenu.wnd:WinFactionChinaSmall", "MainMenu.wnd:WinFactionChinaMedium",
	"MainMenu.wnd:ButtonCampaignBack", "MainMenu.wnd:ButtonMissionBack", "MainMenu.wnd:ButtonDiffBack",
	"MainMenu.wnd:StaticTextSelectDifficulty",
};

// what the rail shows and what it has opened
enum RailList { RAIL_MAIN, RAIL_SOLO, RAIL_LOAD };
enum RailPanel { PANEL_NONE, PANEL_CAMPAIGN, PANEL_MODS };
enum RailSide { SIDE_USA, SIDE_GLA, SIDE_CHINA, SIDE_CHALLENGE };
static Int railList = RAIL_MAIN;
static Int railPanel = PANEL_NONE;
static Int railSide = SIDE_USA;
static Bool campaignTabs = FALSE;		// the side's original Generals campaign is installed
static Bool generalsOpen = FALSE;		// its tab is the open one
static Int missionCount = 0;
static Int missionPicked = 0;

static Bool buttonPushed = FALSE;
static Bool isShuttingDown = FALSE;
static Bool startGame = FALSE;
static Bool launchChallengeMenu = FALSE;
static Bool FirstTimeRunningTheGame = TRUE;

//Added by Saad
void AcceptResolution();
void DeclineResolution();
GameWindow *resAcceptMenu = NULL;
extern DisplaySettings oldDispSettings, newDispSettings;
extern Bool dispChanged;

static void layOutMenu( void );

void HandleCanceledDownload( Bool resetDropDown )
{
	buttonPushed = FALSE;
	if( resetDropDown )
	{
		railList = RAIL_MAIN;
		railPanel = PANEL_NONE;
		layOutMenu();
	}
}

static void quitCallback( void )
{
	buttonPushed = TRUE;
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_SELECTED]);
	TheShell->pop();
	TheGameEngine->setQuitting( TRUE );

	if (TheGameLogic->isInGame())
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
}


void setupGameStart(AsciiString mapName, GameDifficulty diff)
{
	TheCampaignManager->setGameDifficulty(diff);
	buttonPushed = TRUE;

	if (launchChallengeMenu)
	{
		if (TheChallengeGenerals)
			TheChallengeGenerals->setCurrentDifficulty(diff);

		campaignSelected = TRUE;
		TheShell->push( AsciiString("Menus/ChallengeMenu.wnd") );
	}
	else
	{
		startGame = TRUE;
		TheWritableGlobalData->m_pendingFile = mapName;
		TheShell->reverseAnimatewindow();
		TheTransitionHandler->setGroup("FadeWholeScreen");
	}
}

void prepareCampaignGame(GameDifficulty diff)
{
	OptionPreferences pref;
	pref.setCampaignDifficulty(diff);
	pref.write();
	TheScriptEngine->setGlobalDifficulty(diff);

	buttonPushed = FALSE;
	setupGameStart(TheCampaignManager->getCurrentMap(), diff );
}

static MessageBoxReturnType cancelStartBecauseOfNoCD( void *userData )
{
	return MB_RETURN_CLOSE;
}

static MessageBoxReturnType checkCDCallback( void *userData )
{
	if (!IsFirstCDPresent())
	{
		return MB_RETURN_KEEPOPEN;
	}
	else
	{
		prepareCampaignGame((GameDifficulty)(Int)(intptr_t)userData);
		return MB_RETURN_CLOSE;
	}
}

static void doGameStart( void )
{
	startGame = FALSE;

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData();

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	GameEngine_appendNewGameRates( msg, TheGlobalData->m_missionGameSpeedPercent );
	InitRandom(0);

	isShuttingDown = TRUE;
}

static void checkCDBeforeCampaign(GameDifficulty diff)
{
	if (!IsFirstCDPresent())
	{
		// popup a dialog asking for a CD
		ExMessageBoxOkCancel(TheGameText->fetch("GUI:InsertCDPrompt"), TheGameText->fetch("GUI:InsertCDMessage"),
			(void *)diff, checkCDCallback, cancelStartBecauseOfNoCD);
	}
	else
	{
		prepareCampaignGame(diff);
	}
}

/// Skirmish, or a Mods card: the skirmish room in that mode (SkirmishGameOptionsMenu.cpp)
static void enterSkirmishRoom( Int mode )
{
	if( campaignSelected || buttonPushed )
		return;
	skirmishDirectStrike = mode == HOST_MODE_DIRECT_STRIKE;
	skirmishApocalypse = mode == HOST_MODE_APOCALYPSE;
	buttonPushed = TRUE;
	campaignSelected = TRUE;
#ifdef _CAMPEA_DEMO
	TheCampaignManager->setCampaign( "MD_CAMPEA_DEMO" );
	checkCDBeforeCampaign(DIFFICULTY_NORMAL);
	return;
#endif
	TheShell->push( AsciiString("Menus/SkirmishGameOptionsMenu.wnd") );
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_SKIRMISH_SELECTED]);
}

static void shutdownComplete( WindowLayout *layout )
{
	isShuttingDown = FALSE;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end if

//=================================================================================================
// The rail: where every window stands, and how each is drawn
//=================================================================================================

using MainMenuLayout::Rect;

/// a 1080 length on this screen, by its height
static Int len( Real at1080 )
{
	return MainMenuLayout::length( at1080, TheDisplay->getHeight() );
}

static IRegion2D onScreen( const Rect &r1080 )
{
	const Rect r = MainMenuLayout::onScreen( r1080, TheDisplay->getWidth(), TheDisplay->getHeight() );
	IRegion2D region;
	region.lo.x = r.left;
	region.lo.y = r.top;
	region.hi.x = r.right;
	region.hi.y = r.bottom;
	return region;
}

static IRegion2D windowRegion( GameWindow *window )
{
	IRegion2D r;
	Int w, h;
	window->winGetScreenPosition( &r.lo.x, &r.lo.y );
	window->winGetSize( &w, &h );
	r.hi.x = r.lo.x + w;
	r.hi.y = r.lo.y + h;
	return r;
}

/** Arial at a size given at 1080, on the menu's scale (the height's), never under the menu
	* language's floor.  UIStyle::menuFont scales by the smaller axis, which at 4:3 would set the
	* rail's words a quarter smaller than its boxes. */
static GameFont *railFont( Int px1080, Bool bold )
{
	const Int em = max( (Int)UIStyle::MENU_TEXT_FLOOR, len( (Real)px1080 ) );
	return TheFontLibrary->getFont( AsciiString( "Arial" ), UIStyle::menuFontPoints( em ), bold );
}

/// put window on a 1080 rect, its parent already placed
static void place( GameWindow *window, const Rect &r1080 )
{
	if( window == NULL )
		return;
	const IRegion2D r = onScreen( r1080 );
	Int px = 0, py = 0;
	if( window->winGetParent() )
		window->winGetParent()->winGetScreenPosition( &px, &py );
	window->winSetPosition( r.lo.x - px, r.lo.y - py );
	window->winSetSize( r.hi.x - r.lo.x, r.hi.y - r.lo.y );
}

static void placeFrame( Int frame, const Rect &r1080 )
{
	place( frames[ frame ][ 0 ], r1080 );
	place( frames[ frame ][ 1 ], r1080 );
}

static void showFrame( Int frame, Bool show )
{
	for( Int i = 0; i < 2; ++i )
		if( frames[ frame ][ i ] )
			frames[ frame ][ i ]->winHide( !show );
}

/// a window and its frames all shown
static Bool isShown( GameWindow *window )
{
	for( ; window; window = window->winGetParent() )
		if( window->winIsHidden() )
			return FALSE;
	return TRUE;
}

/// one line of text drawn by the menu itself; made on first use, freed with the menu
static DisplayString *menuText( DisplayString *&words, const UnicodeString &text, GameFont *font )
{
	if( words == NULL )
		words = TheDisplayStringManager->newDisplayString();
	if( words->getFont() != font )
		words->setFont( font );
	if( words->getText().compare( text ) != 0 )
		words->setText( text );
	return words;
}

static void freeText( DisplayString *&words )
{
	if( words )
		TheDisplayStringManager->freeDisplayString( words );
	words = NULL;
}

static DisplayString *textFamily = NULL;
static DisplayString *textWordmark[ 2 ] = { NULL, NULL };
static DisplayString *textCaption = NULL;
static DisplayString *textPanelTitle = NULL;
static DisplayString *textMissionLabel = NULL;
static DisplayString *textDifficultyLabel = NULL;
static DisplayString *textTip = NULL;
static DisplayString *textCard[ 2 ][ 3 ] = { { NULL, NULL, NULL }, { NULL, NULL, NULL } };

static void freeMenuTexts( void )
{
	freeText( textFamily );
	freeText( textWordmark[ 0 ] );
	freeText( textWordmark[ 1 ] );
	freeText( textCaption );
	freeText( textPanelTitle );
	freeText( textMissionLabel );
	freeText( textDifficultyLabel );
	freeText( textTip );
	for( Int c = 0; c < 2; ++c )
		for( Int i = 0; i < 3; ++i )
			freeText( textCard[ c ][ i ] );
}

static Color shadowColor( void )
{
	return GameMakeColor( 0, 0, 0, 160 );
}

/// every button the menu draws and the tooltip each carries (Tools/mainmenu_layout.py TOOLTIPS)
struct MenuButton
{
	GameWindow **window;
	const char *tip;
	Bool more;				///< opens a list or a panel: a chevron at its right
	Bool onRail;			///< a rail button, else a panel's
};
static const MenuButton MENU_BUTTONS[] =
{
	{ &buttonSinglePlayer, "TOOLTIP:MainMenuSolo", TRUE, TRUE },
	{ &buttonMultiPlayer, "TOOLTIP:MainMenuMulti", FALSE, TRUE },
	{ &buttonOnline, "TOOLTIP:MainMenuOnline", FALSE, TRUE },
	{ &buttonLoadReplay, "TOOLTIP:MainMenuLoad", TRUE, TRUE },
	{ &buttonOptions, "TOOLTIP:MainMenuOptions", FALSE, TRUE },
	{ &buttonCredits, "TOOLTIP:MainMenuCredits", FALSE, TRUE },
	{ &buttonExit, "TOOLTIP:MainMenuExit", FALSE, TRUE },
	{ &buttonUSA, "TOOLTIP:MainMenuUSA", TRUE, TRUE },
	{ &buttonGLA, "TOOLTIP:MainMenuGLA", TRUE, TRUE },
	{ &buttonChina, "TOOLTIP:MainMenuChina", TRUE, TRUE },
	{ &buttonChallenge, "TOOLTIP:MainMenuChallenge", TRUE, TRUE },
	{ &buttonSkirmish, "TOOLTIP:MainMenuSkirmish", FALSE, TRUE },
	{ &buttonMods, "TOOLTIP:MainMenuMods", TRUE, TRUE },
	{ &buttonSingleBack, "TOOLTIP:MainMenuBack", FALSE, TRUE },
	{ &buttonLoad, "TOOLTIP:MainMenuLoadGame", FALSE, TRUE },
	{ &buttonReplay, "TOOLTIP:MainMenuReplay", FALSE, TRUE },
	{ &buttonLoadReplayBack, "TOOLTIP:MainMenuBack", FALSE, TRUE },
	{ &buttonCampaignGenerals, "TOOLTIP:MainMenuGenerals", FALSE, FALSE },
	{ &buttonCampaignZH, "TOOLTIP:MainMenuZH", FALSE, FALSE },
	{ &buttonMission[ 0 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 1 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 2 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 3 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 4 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 5 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonMission[ 6 ], "TOOLTIP:MainMenuMission", FALSE, FALSE },
	{ &buttonEasy, "TOOLTIP:MainMenuEasy", FALSE, FALSE },
	{ &buttonMedium, "TOOLTIP:MainMenuMedium", FALSE, FALSE },
	{ &buttonHard, "TOOLTIP:MainMenuHard", FALSE, FALSE },
	{ &buttonModeDirectStrike, "TOOLTIP:MainMenuMods", FALSE, FALSE },
	{ &buttonModeApocalypse, "TOOLTIP:MainMenuMods", FALSE, FALSE },
};

static const MenuButton *menuButton( GameWindow *window )
{
	for( Int i = 0; i < (Int)ARRAY_SIZE( MENU_BUTTONS ); ++i )
		if( window && *MENU_BUTTONS[ i ].window == window )
			return &MENU_BUTTONS[ i ];
	return NULL;
}

/// the rail's buttons in the open list, the leaving one last
static Int railButtons( GameWindow *list[ 8 ] )
{
	Int n = 0;
	if( railList == RAIL_SOLO )
	{
		GameWindow *solo[] = { buttonUSA, buttonGLA, buttonChina, buttonChallenge, buttonSkirmish, buttonMods, buttonSingleBack };
		for( Int i = 0; i < (Int)ARRAY_SIZE( solo ); ++i )
			list[ n++ ] = solo[ i ];
	}
	else if( railList == RAIL_LOAD )
	{
		list[ n++ ] = buttonLoad;
		list[ n++ ] = buttonReplay;
		list[ n++ ] = buttonLoadReplayBack;
	}
	else
	{
		list[ n++ ] = buttonSinglePlayer;
		list[ n++ ] = buttonMultiPlayer;
		// only a game the launcher started on its online part has a service to dial
		if( buttonOnline && OnlineIsEnabled() )
			list[ n++ ] = buttonOnline;
		list[ n++ ] = buttonLoadReplay;
		list[ n++ ] = buttonOptions;
		list[ n++ ] = buttonCredits;
		list[ n++ ] = buttonExit;
	}
	return n;
}

static Bool railCaption( void )
{
	return railList != RAIL_MAIN;
}

static Rect railRect( void )
{
	GameWindow *list[ 8 ];
	return MainMenuLayout::railRect( railCaption(), railButtons( list ) - 1 );
}

/// the lit key Enter presses on the rail; Medium and Direct Strike light themselves in their panels
static GameWindow *railPrimary( void )
{
	if( railList == RAIL_MAIN )
		return buttonMultiPlayer;
	if( railList == RAIL_LOAD )
		return buttonLoad;
	return NULL;
}

/// what Enter presses: the rail's lit key, or the open panel's
static GameWindow *enterButton( void )
{
	if( railPanel == PANEL_CAMPAIGN )
		return buttonMedium;
	if( railPanel == PANEL_MODS )
		return buttonModeDirectStrike;
	return railPrimary();
}

/// the rail button whose panel is open, green
static GameWindow *railOpen( void )
{
	if( railPanel == PANEL_MODS )
		return buttonMods;
	if( railPanel != PANEL_CAMPAIGN )
		return NULL;
	GameWindow *sides[] = { buttonUSA, buttonGLA, buttonChina, buttonChallenge };
	return sides[ railSide ];
}

static MainMenuLayout::CampaignPanel campaignPanel( void )
{
	const Bool challenge = railSide == SIDE_CHALLENGE;
	return MainMenuLayout::campaignPanel( campaignTabs && !challenge, !challenge );
}

/// the open panel on the board, FALSE with none
static Bool panelRect( Rect &r )
{
	if( railPanel == PANEL_CAMPAIGN )
	{
		r = campaignPanel().panel;
		return TRUE;
	}
	if( railPanel == PANEL_MODS )
	{
		r = MainMenuLayout::modsPanel( railRect().bottom );
		return TRUE;
	}
	return FALSE;
}

/// a check-like button's pushed state, the open side's and the picked mission's
static void setPicked( GameWindow *window, Bool picked )
{
	if( window && BitTest( window->winGetStatus(), WIN_STATUS_CHECK_LIKE ) )
		GadgetCheckLikeButtonSetVisualCheck( window, picked );
}

/** The side buttons and the mission tiles hold their choice as their pushed state, so what the
	* panel shows is the gadgets' own state and not only a picture (ui_walk.py reads it). */
static void showPicked( void )
{
	GameWindow *open = railOpen();
	GameWindow *sides[] = { buttonUSA, buttonGLA, buttonChina, buttonChallenge, buttonMods };
	for( Int i = 0; i < (Int)ARRAY_SIZE( sides ); ++i )
		setPicked( sides[ i ], sides[ i ] == open );
	for( Int m = 0; m < MISSION_BUTTONS; ++m )
		setPicked( buttonMission[ m ], railPanel == PANEL_CAMPAIGN && m == missionPicked && m < missionCount );
}

/// every window where the rail and its panel put it, the rest hidden
static void layOutMenu( void )
{
	if( parentMainMenu == NULL )
		return;
	for( Int i = 0; i < (Int)ARRAY_SIZE( RETIRED_WINDOWS ); ++i )
	{
		GameWindow *win = TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( RETIRED_WINDOWS[ i ] ) );
		if( win )
			win->winHide( TRUE );
	}
	for( Int f = 0; f < FRAME_COUNT; ++f )
		showFrame( f, FALSE );
	if( buttonOnline )
		buttonOnline->winHide( TRUE );

	// the rail
	const Int railFrame = railList == RAIL_SOLO ? FRAME_SOLO : railList == RAIL_LOAD ? FRAME_LOAD : FRAME_MAIN;
	GameWindow *list[ 8 ];
	const Int count = railButtons( list );
	placeFrame( railFrame, railRect() );
	showFrame( railFrame, TRUE );
	for( Int b = 0; b < count; ++b )
	{
		place( list[ b ], MainMenuLayout::railButton( railCaption(), b, count - 1 ) );
		if( list[ b ] )
			list[ b ]->winHide( FALSE );
	}
	if( labelVersion )
	{
		place( labelVersion, MainMenuLayout::rect( MainMenuLayout::CONTENT_RIGHT - 240, MainMenuLayout::FAMILY_TOP,
			MainMenuLayout::CONTENT_RIGHT, MainMenuLayout::FAMILY_TOP + 18 ) );
		labelVersion->winHide( FALSE );
	}

	if( railPanel == PANEL_CAMPAIGN )
	{
		const MainMenuLayout::CampaignPanel p = campaignPanel();
		const Int left = p.panel.left + MainMenuLayout::PANEL_PAD, right = p.panel.right - MainMenuLayout::PANEL_PAD;
		if( p.tabsTop >= 0 )
		{
			placeFrame( FRAME_TABS, MainMenuLayout::rect( left, p.tabsTop, right, p.tabsTop + MainMenuLayout::TAB_HEIGHT ) );
			showFrame( FRAME_TABS, TRUE );
			place( buttonCampaignGenerals, MainMenuLayout::campaignTab( 0, p.tabsTop ) );
			place( buttonCampaignZH, MainMenuLayout::campaignTab( 1, p.tabsTop ) );
		}
		if( p.tilesTop >= 0 )
		{
			placeFrame( FRAME_TILES, MainMenuLayout::rect( left, p.tilesTop, right, p.tilesTop + MainMenuLayout::TILE_HEIGHT ) );
			showFrame( FRAME_TILES, TRUE );
			for( Int m = 0; m < MISSION_BUTTONS; ++m )
			{
				if( buttonMission[ m ] == NULL )
					continue;
				buttonMission[ m ]->winHide( m >= missionCount );
				if( m < missionCount )
					place( buttonMission[ m ], MainMenuLayout::missionTile( m, missionCount, p.tilesTop ) );
			}
		}
		placeFrame( FRAME_KEYS, MainMenuLayout::rect( left, p.keysTop, right, p.keysTop + MainMenuLayout::BUTTON_HEIGHT ) );
		showFrame( FRAME_KEYS, TRUE );
		place( buttonEasy, MainMenuLayout::difficultyKey( 0, p.keysTop ) );
		place( buttonMedium, MainMenuLayout::difficultyKey( 1, p.keysTop ) );
		place( buttonHard, MainMenuLayout::difficultyKey( 2, p.keysTop ) );
	}
	else if( railPanel == PANEL_MODS )
	{
		const Int foot = railRect().bottom;
		const Rect a = MainMenuLayout::modeCard( 0, foot ), b = MainMenuLayout::modeCard( 1, foot );
		placeFrame( FRAME_MODS, MainMenuLayout::rect( a.left, a.top, b.right, b.bottom ) );
		showFrame( FRAME_MODS, TRUE );
		place( buttonModeDirectStrike, a );
		place( buttonModeApocalypse, b );
	}
	showPicked();
}

/// the campaign the manager is on, its missions into the tiles; the first one picked
static void loadMissions( void )
{
	Campaign *campaign = TheCampaignManager->getCurrentCampaign();
	Mission *mission = campaign ? campaign->getNextMission( NULL ) : NULL;
	missionCount = 0;
	for( Int i = 0; i < MISSION_BUTTONS; ++i )
	{
		missionNames[ i ].clear();
		if( mission )
		{
			missionNames[ i ] = mission->m_name;
			mission = campaign->getNextMission( mission );
			missionCount = i + 1;
		}
	}
	missionPicked = 0;
}

/// The side's original Generals campaign is offered when Campaign.ini names one and its first
/// mission's map is installed.  Leaves the campaign manager on it, or on none.
static Bool generalsCampaignInstalled( const char *campaign )
{
	TheCampaignManager->setCampaign( campaign );
	Mission *first = TheCampaignManager->getCurrentMission();
	return first && TheFileSystem->doesFileExist( first->m_mapName.str() );
}

static const char *const ZERO_HOUR_CAMPAIGNS[] = { "USA", "GLA", "China" };
static const char *const GENERALS_CAMPAIGNS[] = { "GEN_USA", "GEN_GLA", "GEN_CHINA" };

/// a side's panel: its Zero Hour campaign open, the original one on a tab when installed
static void openCampaign( Int side )
{
	railSide = side;
	railPanel = PANEL_CAMPAIGN;
	generalsOpen = FALSE;
	if( side == SIDE_CHALLENGE )
	{
		campaignTabs = FALSE;
		TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
		missionCount = 0;
	}
	else
	{
		campaignTabs = generalsCampaignInstalled( GENERALS_CAMPAIGNS[ side ] );
		TheCampaignManager->setCampaign( ZERO_HOUR_CAMPAIGNS[ side ] );
#ifdef _CAMPEA_DEMO
		static const char *const DEMO[] = { "MD_USA_1_DEMO", "MD_USA_2_DEMO", "MD_GLA_3_DEMO" };
		TheCampaignManager->setCampaign( DEMO[ side ] );
#endif
		loadMissions();
	}
	layOutMenu();
}

static void closePanel( void )
{
	if( railPanel == PANEL_CAMPAIGN )
		TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
	railPanel = PANEL_NONE;
	layOutMenu();
}

static void showList( Int list )
{
	railList = list;
	railPanel = PANEL_NONE;
	layOutMenu();
}

/// a difficulty key: the picked mission, or Generals' Challenge's menu, at that difficulty
static void startAtDifficulty( GameDifficulty diff )
{
	if( buttonPushed )
		return;
	launchChallengeMenu = railSide == SIDE_CHALLENGE;
	if( !launchChallengeMenu )
	{
		Campaign *campaign = TheCampaignManager->getCurrentCampaign();
		if( campaign == NULL || missionPicked >= missionCount )
			return;
		// the campaign starts here and carries on from it as it would have; every campaign mission
		// starts at rank 0 whatever came before (CampaignManager::getRankPoints)
		TheCampaignManager->setCampaignAndMission( campaign->m_name, missionNames[ missionPicked ] );
	}
	checkCDBeforeCampaign( diff );
}

//-------------------------------------------------------------------------------------------------
// Drawing
//-------------------------------------------------------------------------------------------------

static void drawNothing( GameWindow *, WinInstanceData * )
{
}

static void fillRegion( const IRegion2D &r, Color color )
{
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, color );
}

static void outlineRegion( const IRegion2D &r, Color color )
{
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, 1.0f, color );
}

/** The world round the rail darkened as frame A's dim does, but only near it: strongest against the
	* rail, gone 200 px out, so the shell map stays the open area's picture.  Ten rings. */
static void dimNear( const IRegion2D &r )
{
	const Int ring = max( 1, len( 20.0f ) );
	const Int w = TheDisplay->getWidth(), h = TheDisplay->getHeight();
	for( Int i = 0; i < 10; ++i )
	{
		const Real fade = 1.0f - ( i + 0.5f ) / 10.0f;
		const Color dim = GameMakeColor( 0, 0, 0, (Int)( 120.0f * fade * sqrtf( fade ) ) );
		const Int in = i * ring, out = ( i + 1 ) * ring;
		const Int x0 = max( 0, r.lo.x - out ), x1 = min( w, r.hi.x + out );
		const Int y0 = max( 0, r.lo.y - out ), y1 = min( h, r.hi.y + out );
		const Int ix0 = r.lo.x - in, ix1 = r.hi.x + in, iy0 = max( 0, r.lo.y - in ), iy1 = min( h, r.hi.y + in );
		if( iy0 > y0 )
			TheDisplay->drawFillRect( x0, y0, x1 - x0, iy0 - y0, dim );
		if( y1 > iy1 )
			TheDisplay->drawFillRect( x0, iy1, x1 - x0, y1 - iy1, dim );
		if( ix0 > x0 )
			TheDisplay->drawFillRect( x0, iy0, ix0 - x0, iy1 - iy0, dim );
		if( x1 > ix1 )
			TheDisplay->drawFillRect( ix1, iy0, x1 - ix1, iy1 - iy0, dim );
	}
}

/** Frame A's measured edge round the rail: UIStyle::frameA without its dim of the whole world
	* (dimNear instead), on the rail's scale.  ponytail: a copy of frameA's ticks and corners; one
	* call once UIStyle can draw frame A without the dim. */
static void railFrame( const IRegion2D &panel )
{
	const Int x0 = panel.lo.x, y0 = panel.lo.y, x1 = panel.hi.x, y1 = panel.hi.y;
	UIStyle::litEdge( panel, UIStyle::EDGE );
	for( Int pass = 0; pass < 2; ++pass )
	{
		for( Int i = 0; ; i++ )
		{
			const Int step = len( (Real)( UIStyle::FRAME_TICK_STEP * i ) );
			const Int at = ( pass == 0 ? x0 : y0 ) + step;
			if( i > 0 && at > ( pass == 0 ? x1 : y1 ) )
				break;
			const Int tick = UIStyle::frameTickLength( i );
			const Int n = len( (Real)tick );
			const Color color = tick == 14 ? UIStyle::withAlpha( UIStyle::EDGE, 230 )
				: tick == 8 ? UIStyle::withAlpha( UIStyle::EDGE, 190 ) : UIStyle::withAlpha( UIStyle::FRAME, 200 );
			if( pass == 0 )
			{
				TheDisplay->drawFillRect( at, y0 - n, 1, n, color );
				TheDisplay->drawFillRect( at, y1, 1, n, color );
			}
			else
			{
				TheDisplay->drawFillRect( x0 - n, at, n, 1, color );
				TheDisplay->drawFillRect( x1, at, n, 1, color );
			}
		}
	}
	const Int r = len( (Real)UIStyle::FRAME_RETURN );
	const Int arm = 3 * r;
	UIStyle::litLine( x0 - r, y0 - r, arm, 1, UIStyle::EDGE );
	UIStyle::litLine( x0 - r, y0 - r, 1, arm, UIStyle::EDGE );
	UIStyle::litLine( x1 + r - arm, y0 - r, arm, 1, UIStyle::EDGE );
	UIStyle::litLine( x1 + r - 1, y0 - r, 1, arm, UIStyle::EDGE );
	UIStyle::litLine( x0 - r, y1 + r - 1, arm, 1, UIStyle::EDGE );
	UIStyle::litLine( x0 - r, y1 + r - arm, 1, arm, UIStyle::EDGE );
	UIStyle::litLine( x1 + r - arm, y1 + r - 1, arm, 1, UIStyle::EDGE );
	UIStyle::litLine( x1 + r - 1, y1 + r - arm, 1, arm, UIStyle::EDGE );
	UIStyle::litLine( x0 + len( 32.0f ), y0 - len( 2.0f ), len( 200.0f ), len( 3.0f ), UIStyle::EDGE_LIT );
}

/// a lit rule two pixels at 1080, across a 1080 span
static void rule( Int left, Int right, Int y, Color color )
{
	const IRegion2D r = onScreen( MainMenuLayout::rect( left, y, right, y + 2 ) );
	UIStyle::litLine( r.lo.x, r.lo.y, r.hi.x - r.lo.x, max( 1, r.hi.y - r.lo.y ), color );
}

/// words at a 1080 point, left aligned, centred on the row of height
static void drawWords( DisplayString *words, Int x1080, Int top1080, Int height1080, Color color )
{
	const IRegion2D r = onScreen( MainMenuLayout::rect( x1080, top1080, x1080, top1080 + height1080 ) );
	Int w, h;
	words->getSize( &w, &h );
	words->draw( r.lo.x, ( r.lo.y + r.hi.y - h ) / 2, color, shadowColor() );
}

/// a panel beside the rail: its glass, its lit edge (no ticks; the rail carries the scale), its
/// title and the rule under it
static void drawPanel( const Rect &panel1080, const UnicodeString &title )
{
	const IRegion2D panel = onScreen( panel1080 );
	UIStyle::glass( panel, 168 );
	UIStyle::litEdge( panel, UIStyle::withAlpha( UIStyle::EDGE, 200 ) );
	const Int left = panel1080.left + MainMenuLayout::PANEL_PAD, right = panel1080.right - MainMenuLayout::PANEL_PAD;
	drawWords( menuText( textPanelTitle, UIStyle::capitals( title ), railFont( UIStyle::MENU_TEXT_TITLE, TRUE ) ),
		left, panel1080.top + MainMenuLayout::PANEL_PAD, 50, UIStyle::INK );
	rule( left, right, panel1080.top + MainMenuLayout::PANEL_PAD + MainMenuLayout::PANEL_TITLE_STEP, UIStyle::FRAME );
}

/// the hovered button's one line: left of the rail (or of the panel it opened) for a rail button,
/// under the panel for a panel's
static void drawTooltip( void )
{
	const MenuButton *hovered = NULL;
	GameWindow *window = NULL;
	for( Int i = 0; i < (Int)ARRAY_SIZE( MENU_BUTTONS ) && hovered == NULL; ++i )
	{
		GameWindow *each = *MENU_BUTTONS[ i ].window;
		if( each && BitTest( each->winGetInstanceData()->getState(), WIN_STATE_HILITED ) && isShown( each ) )
		{
			hovered = &MENU_BUTTONS[ i ];
			window = each;
		}
	}
	if( hovered == NULL || !UIStyle::hasText( hovered->tip ) )
		return;
	DisplayString *words = menuText( textTip, TheGameText->fetch( hovered->tip ), railFont( UIStyle::MENU_TEXT_BODY, FALSE ) );
	Int tw, th;
	words->getSize( &tw, &th );
	const IRegion2D button = windowRegion( window );
	const Int w = tw + len( 36.0f ), h = len( (Real)MainMenuLayout::TOOLTIP_HEIGHT );
	Rect panel1080;
	const Bool panelOpen = panelRect( panel1080 );
	IRegion2D card;
	if( hovered->onRail )
	{
		const Int anchor = panelOpen ? onScreen( panel1080 ).lo.x : onScreen( railRect() ).lo.x;
		card.hi.x = anchor - len( (Real)MainMenuLayout::TOOLTIP_OFF );
		card.lo.x = card.hi.x - w;
		card.lo.y = ( button.lo.y + button.hi.y - h ) / 2;
	}
	else
	{
		const IRegion2D panel = onScreen( panel1080 );
		card.lo.x = max( 0, min( button.lo.x, panel.hi.x - w ) );
		card.lo.y = panel.hi.y + len( (Real)MainMenuLayout::TOOLTIP_UNDER );
	}
	card.hi.x = card.lo.x + w;
	card.hi.y = card.lo.y + h;
	UIStyle::glass( card, 200 );
	outlineRegion( card, UIStyle::EDGE );
	words->draw( card.lo.x + len( 18.0f ), ( card.lo.y + card.hi.y - th ) / 2, UIStyle::INK, shadowColor() );
}

/// MainMenuParent's draw: everything that is not a button, behind the buttons
static void drawMenu( GameWindow *, WinInstanceData * )
{
	using namespace MainMenuLayout;
	const Rect rail1080 = railRect();
	const IRegion2D rail = onScreen( rail1080 );
	dimNear( rail );
	UIStyle::glass( rail, 168 );
	railFrame( rail );

	// the game's name: the family line (the version is LabelVersion at its right), the wordmark
	drawWords( menuText( textFamily, UnicodeString( u"COMMAND & CONQUER GENERALS" ), railFont( UIStyle::MENU_TEXT_LABEL, TRUE ) ),
		CONTENT_LEFT, FAMILY_TOP, 18, UIStyle::MUTED );
	GameFont *wordmarkFont = railFont( 50, TRUE );
	menuText( textWordmark[ 0 ], UnicodeString( u"Zero Hour" ), wordmarkFont );
	menuText( textWordmark[ 1 ], UnicodeString( u" Reforged" ), wordmarkFont );
	Int first, second, height;
	textWordmark[ 0 ]->getSize( &first, &height );
	textWordmark[ 1 ]->getSize( &second, &height );
	const IRegion2D mark = onScreen( rect( CONTENT_LEFT - 2, WORDMARK_TOP, CONTENT_RIGHT, WORDMARK_TOP + 60 ) );
	UIStyle::wordmark( textWordmark, mark.lo.x + first + second, ( mark.lo.y + mark.hi.y - height ) / 2 );
	rule( CONTENT_LEFT, CONTENT_RIGHT, HEADER_RULE, UIStyle::FRAME );

	if( railCaption() )
	{
		const char *key = railList == RAIL_SOLO ? "GUI:SinglePlayer" : "GUI:ReplayMenu";
		drawWords( menuText( textCaption, UIStyle::capitals( TheGameText->fetch( key ) ), railFont( UIStyle::MENU_TEXT_LABEL, TRUE ) ),
			CONTENT_LEFT, LIST_TOP, 20, UIStyle::ACTIVE );
	}
	// the faint rule over the leaving button
	GameWindow *list[ 8 ];
	const Int count = railButtons( list );
	const Int split = railButton( railCaption(), count - 1, count - 1 ).top - SPLIT / 2;
	const IRegion2D faint = onScreen( rect( CONTENT_LEFT, split, CONTENT_RIGHT, split ) );
	TheDisplay->drawFillRect( faint.lo.x, faint.lo.y, faint.hi.x - faint.lo.x, 1, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );

	Rect panel1080;
	if( panelRect( panel1080 ) )
	{
		if( railPanel == PANEL_CAMPAIGN )
		{
			GameWindow *open = railOpen();
			drawPanel( panel1080, open ? open->winGetText() : UnicodeString::TheEmptyString );
			const CampaignPanel p = campaignPanel();
			const Int left = panel1080.left + PANEL_PAD;
			if( p.missionLabel >= 0 )
				drawWords( menuText( textMissionLabel, UIStyle::capitals( TheGameText->fetch( "GUI:MainMenuMission" ) ),
					railFont( UIStyle::MENU_TEXT_LABEL, TRUE ) ), left, p.missionLabel, 20, UIStyle::MUTED );
			drawWords( menuText( textDifficultyLabel, UIStyle::capitals( TheGameText->fetch( "GUI:MainMenuDifficulty" ) ),
				railFont( UIStyle::MENU_TEXT_LABEL, TRUE ) ), left, p.difficultyLabel, 20, UIStyle::MUTED );
		}
		else
			drawPanel( panel1080, TheGameText->fetch( "GUI:Mods" ) );

		// the open button's green line, run across to the panel it opened
		GameWindow *open = railOpen();
		if( open )
		{
			const IRegion2D b = windowRegion( open );
			const IRegion2D panel = onScreen( panel1080 );
			const Int y = ( b.lo.y + b.hi.y ) / 2 - 1;
			UIStyle::litLine( panel.hi.x, y, b.lo.x - panel.hi.x, max( 1, len( 2.0f ) ), UIStyle::ACTIVE );
		}
	}
	drawTooltip();
}

/** A rail button, menu_button in the design: black over the glass and a FRAME edge; the lit one an
	* EDGE wash in a cyan lit edge; the open one green with a green bar; under the pointer a white lift
	* and a white bar.  Its word left aligned in 22 px bold capitals, a chevron when it opens more. */
static void drawRailButton( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D r = windowRegion( window );
	const MenuButton *entry = menuButton( window );
	const Bool enabled = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED );
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
	const Bool pressed = BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Bool primary = window == railPrimary();
	const Bool open = window == railOpen();
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	const Int bar = max( 2, len( 3.0f ) );
	fillRegion( r, GameMakeColor( 0, 0, 0, 150 ) );
	Color ink = UIStyle::INK;
	if( !enabled )
	{
		outlineRegion( r, UIStyle::FRAME );
		ink = UIStyle::MUTED;
	}
	else if( primary )
	{
		fillRegion( r, UIStyle::withAlpha( UIStyle::EDGE, pressed ? 110 : 60 ) );
		const Int line = max( 1, len( 2.0f ) );
		UIStyle::litLine( r.lo.x, r.lo.y, w, line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.hi.y - line, w, line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.lo.y, line, h, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.hi.x - line, r.lo.y, line, h, UIStyle::EDGE_LIT );
	}
	else if( open )
	{
		fillRegion( r, UIStyle::withAlpha( UIStyle::ACTIVE, 14 ) );
		outlineRegion( r, GameMakeColor( 90, 120, 40, 255 ) );
		UIStyle::litLine( r.lo.x, r.lo.y, bar, h, UIStyle::ACTIVE );
		ink = UIStyle::ACTIVE;
	}
	else if( hover || pressed )
	{
		fillRegion( r, GameMakeColor( 255, 255, 255, pressed ? 28 : 16 ) );
		outlineRegion( r, UIStyle::EDGE );
		UIStyle::litLine( r.lo.x, r.lo.y, bar, h, UIStyle::INK );
	}
	else
		outlineRegion( r, UIStyle::FRAME );

	DisplayString *words = instData->getTextDisplayString();
	if( words )
	{
		GameFont *font = railFont( 22, TRUE );
		if( words->getFont() != font )
			words->setFont( font );
		Int tw, th;
		words->getSize( &tw, &th );
		words->draw( r.lo.x + len( 28.0f ), ( r.lo.y + r.hi.y - th ) / 2, ink, shadowColor() );
	}
	if( entry && entry->more )
	{
		const Int x = r.hi.x - len( 28.0f ), cy = ( r.lo.y + r.hi.y ) / 2, c = len( 7.0f );
		const Color color = open ? UIStyle::ACTIVE : UIStyle::MUTED;
		const Real width = (Real)max( 1, len( 2.0f ) );
		TheDisplay->drawLine( x - c * 3 / 5, cy - c, x + c * 2 / 5, cy, width, color );
		TheDisplay->drawLine( x + c * 2 / 5, cy, x - c * 3 / 5, cy + c, width, color );
	}
}

/// the campaign panel's tabs: Generals, Zero Hour
static void drawCampaignTab( GameWindow *window, WinInstanceData *instData )
{
	const Bool open = ( window == buttonCampaignGenerals ) == generalsOpen;
	UIStyle::menuTab( windowRegion( window ), instData->getTextDisplayString(), open,
		BitTest( instData->getState(), WIN_STATE_HILITED ) );
}

/// a mission tile: its number, the picked one green with a lit foot
static void drawMissionTile( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D r = windowRegion( window );
	Bool picked = FALSE;
	for( Int m = 0; m < MISSION_BUTTONS; ++m )
		if( buttonMission[ m ] == window )
			picked = m == missionPicked;
	fillRegion( r, GameMakeColor( 0, 0, 0, 150 ) );
	if( picked )
	{
		fillRegion( r, UIStyle::withAlpha( UIStyle::ACTIVE, 14 ) );
		outlineRegion( r, GameMakeColor( 90, 120, 40, 255 ) );
		const Int bar = max( 2, len( 3.0f ) );
		UIStyle::litLine( r.lo.x, r.hi.y - bar, r.hi.x - r.lo.x, bar, UIStyle::ACTIVE );
	}
	else if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
	{
		fillRegion( r, GameMakeColor( 255, 255, 255, 16 ) );
		outlineRegion( r, UIStyle::EDGE );
	}
	else
		outlineRegion( r, UIStyle::FRAME );
	DisplayString *words = instData->getTextDisplayString();
	if( words )
	{
		GameFont *font = railFont( UIStyle::MENU_TEXT_TITLE, FALSE );
		if( words->getFont() != font )
			words->setFont( font );
		Int tw, th;
		words->getSize( &tw, &th );
		words->draw( ( r.lo.x + r.hi.x - tw ) / 2, ( r.lo.y + r.hi.y - th ) / 2, picked ? UIStyle::ACTIVE : UIStyle::INK, shadowColor() );
	}
}

/** The mode's picture from flat shapes, the mode picker's (GUIUtil.cpp drawModeEmblem): a lane
	* between two forts on an island, a walled basin in red fog.  ponytail: a copy of the picker's two
	* pictures; one function once package 3a's mode box lands its own in the menu language. */
static void drawModePicture( Int mode, const IRegion2D &b )
{
	const Int bw = b.hi.x - b.lo.x, bh = b.hi.y - b.lo.y;
	const Color blue = GameMakeColor( 70, 120, 220, 255 ), red = GameMakeColor( 210, 60, 50, 255 );
	const Int u = max( 1, min( bw, bh ) / 12 );
	const Int cx = b.lo.x + bw / 2, cy = b.lo.y + bh / 2;
	if( mode == HOST_MODE_DIRECT_STRIKE )
	{
		TheDisplay->drawFillRect( b.lo.x, b.lo.y, bw, bh, GameMakeColor( 18, 38, 64, 255 ) );
		TheDisplay->drawFillRect( b.lo.x + u, cy - 2 * u, bw - 2 * u, 4 * u, GameMakeColor( 92, 104, 70, 255 ) );
		TheDisplay->drawFillRect( b.lo.x + 2 * u, cy - 2 * u, 3 * u, 4 * u, blue );
		TheDisplay->drawFillRect( b.hi.x - 5 * u, cy - 2 * u, 3 * u, 4 * u, red );
		for( Int i = 0; i < 3; ++i )
		{
			TheDisplay->drawFillRect( cx - ( 4 - i ) * u * 3 / 2, cy - u / 2, u, u, blue );
			TheDisplay->drawFillRect( cx + ( 2 + i ) * u * 3 / 2 - u, cy - u / 2, u, u, red );
		}
	}
	else
	{
		TheDisplay->drawFillRect( b.lo.x, b.lo.y, bw, bh, GameMakeColor( 96, 20, 18, 255 ) );
		const Int r = min( bw, bh ) / 3;
		TheDisplay->drawFillRect( cx - r, cy - r, 2 * r, 2 * r, GameMakeColor( 58, 54, 48, 255 ) );
		TheDisplay->drawOpenRect( cx - r, cy - r, 2 * r, 2 * r, (Real)max( 2, u / 2 ), GameMakeColor( 200, 200, 200, 255 ) );
		const Int s = max( 2, u );
		TheDisplay->drawFillRect( cx - u - s / 2, cy - u - s / 2, s, s, blue );
		TheDisplay->drawFillRect( cx + u - s / 2, cy - u - s / 2, s, s, red );
		TheDisplay->drawFillRect( cx - u - s / 2, cy + u - s / 2, s, s, GameMakeColor( 80, 170, 80, 255 ) );
		TheDisplay->drawFillRect( cx + u - s / 2, cy + u - s / 2, s, s, GameMakeColor( 230, 140, 40, 255 ) );
		for( Int i = 0; i < 6; ++i )
		{
			const Int ex = b.lo.x + u + ( i % 3 ) * ( bw - 3 * u ) / 2, ey = i < 3 ? b.lo.y + u : b.hi.y - 2 * u;
			TheDisplay->drawFillRect( ex, ey, s, s, GameMakeColor( 230, 120, 100, 255 ) );
		}
	}
}

/// a Mods card, mode.py's: the picture, the name with its version tag, the fact line; Direct
/// Strike lit as the card Enter picks
static void drawModeCard( GameWindow *window, WinInstanceData *instData )
{
	const Bool strike = window == buttonModeDirectStrike;
	const Int card = strike ? 0 : 1;
	const IRegion2D r = windowRegion( window );
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
	const Bool pressed = BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
	fillRegion( r, GameMakeColor( 0, 0, 0, 150 ) );
	if( strike )
	{
		fillRegion( r, UIStyle::withAlpha( UIStyle::EDGE, pressed ? 110 : 60 ) );
		const Int line = max( 1, len( 2.0f ) );
		UIStyle::litLine( r.lo.x, r.lo.y, w, line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.hi.y - line, w, line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.lo.y, line, h, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.hi.x - line, r.lo.y, line, h, UIStyle::EDGE_LIT );
	}
	else if( hover || pressed )
	{
		fillRegion( r, GameMakeColor( 255, 255, 255, pressed ? 28 : 16 ) );
		outlineRegion( r, UIStyle::EDGE_LIT );
	}
	else
		outlineRegion( r, UIStyle::EDGE );

	IRegion2D picture;
	picture.lo.x = r.lo.x + len( 10.0f );
	picture.lo.y = r.lo.y + len( 10.0f );
	picture.hi.x = r.hi.x - len( 10.0f );
	picture.hi.y = r.lo.y + len( 184.0f );
	drawModePicture( strike ? HOST_MODE_DIRECT_STRIKE : HOST_MODE_APOCALYPSE, picture );

	Bool tagged = FALSE;
	const UnicodeString tag = TheGameText->fetch( strike ? "GUI:DirectStrikeVersion" : "GUI:ApocalypseVersion", &tagged );
	DisplayString *name = menuText( textCard[ card ][ 0 ], UIStyle::capitals( TheGameText->fetch( strike ? "GUI:GameModeDirectStrike" : "GUI:GameModeApocalypse" ) ),
		railFont( UIStyle::MENU_TEXT_KEY, TRUE ) );
	DisplayString *version = menuText( textCard[ card ][ 1 ], tagged ? tag : UnicodeString::TheEmptyString, railFont( UIStyle::MENU_TEXT_LABEL, FALSE ) );
	DisplayString *fact = menuText( textCard[ card ][ 2 ], TheGameText->fetch( strike ? "GUI:GameModeDirectStrikeFact" : "GUI:GameModeApocalypseFact" ),
		railFont( UIStyle::MENU_TEXT_BODY - 2, FALSE ) );
	Int nw, nh, vw = 0, vh = 0, fw, fh;
	name->getSize( &nw, &nh );
	if( version->getTextLength() )
		version->getSize( &vw, &vh );
	fact->getSize( &fw, &fh );
	const Int gap = vw ? len( 8.0f ) : 0;
	const Int nameY = r.lo.y + len( 206.0f );
	const Int left = ( r.lo.x + r.hi.x - nw - gap - vw ) / 2;
	name->draw( left, nameY, UIStyle::INK, shadowColor() );
	if( vw )
		version->draw( left + nw + gap, nameY + nh - vh, UIStyle::MUTED, shadowColor() );
	fact->draw( ( r.lo.x + r.hi.x - fw ) / 2, nameY + nh + len( 12.0f ), UIStyle::MUTED, shadowColor() );
}

/// the version at the rail's top right, a 14 px MUTED word
static void drawVersion( GameWindow *window, WinInstanceData *instData )
{
	DisplayString *words = instData->getTextDisplayString();
	if( words == NULL || words->getTextLength() == 0 )
		return;
	GameFont *font = railFont( UIStyle::MENU_TEXT_LABEL, FALSE );
	if( words->getFont() != font )
		words->setFont( font );
	const IRegion2D r = windowRegion( window );
	Int tw, th;
	words->getSize( &tw, &th );
	words->draw( r.hi.x - tw, ( r.lo.y + r.hi.y - th ) / 2, UIStyle::MUTED, shadowColor() );
}

/// each window's look, its words in capitals, and no gadget tooltip (drawTooltip draws them)
static void dressMenu( void )
{
	parentMainMenu->winSetDrawFunc( drawMenu );
	for( Int f = 0; f < FRAME_COUNT; ++f )
		for( Int i = 0; i < 2; ++i )
			if( frames[ f ][ i ] )
				frames[ f ][ i ]->winSetDrawFunc( drawNothing );
	for( Int i = 0; i < (Int)ARRAY_SIZE( MENU_BUTTONS ); ++i )
	{
		GameWindow *button = *MENU_BUTTONS[ i ].window;
		if( button == NULL )
			continue;
		button->winSetTooltip( UnicodeString::TheEmptyString );
		if( MENU_BUTTONS[ i ].onRail )
		{
			button->winSetDrawFunc( drawRailButton );
			GadgetButtonSetText( button, UIStyle::capitals( button->winGetText() ) );
		}
	}
	// a side and a mission tile stay pushed while picked (showPicked); they answer on the press
	GameWindow *picks[] = { buttonUSA, buttonGLA, buttonChina, buttonChallenge, buttonMods };
	for( Int i = 0; i < (Int)ARRAY_SIZE( picks ); ++i )
		GadgetButtonEnableCheckLike( picks[ i ], TRUE, FALSE );
	buttonCampaignGenerals->winSetDrawFunc( drawCampaignTab );
	buttonCampaignZH->winSetDrawFunc( drawCampaignTab );
	// the tabs' words in the font menuTab draws them in, so the window measures what is drawn
	buttonCampaignGenerals->winSetFont( UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE ) );
	buttonCampaignZH->winSetFont( UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE ) );
	GadgetButtonSetText( buttonCampaignGenerals, UIStyle::capitals( TheGameText->fetch( "GUI:CampaignGenerals" ) ) );
	GadgetButtonSetText( buttonCampaignZH, UIStyle::capitals( TheGameText->fetch( "GUI:CampaignZeroHour" ) ) );
	for( Int m = 0; m < MISSION_BUTTONS; ++m )
	{
		UnicodeString number;
		number.format( u"%d", m + 1 );
		GadgetButtonSetText( buttonMission[ m ], number );
		buttonMission[ m ]->winSetDrawFunc( drawMissionTile );
		buttonMission[ m ]->winEnable( TRUE );
		GadgetButtonEnableCheckLike( buttonMission[ m ], TRUE, FALSE );
	}
	UIStyle::setMenuKey( buttonEasy, FALSE );
	UIStyle::setMenuKey( buttonMedium, TRUE );
	UIStyle::setMenuKey( buttonHard, FALSE );
	buttonModeDirectStrike->winSetDrawFunc( drawModeCard );
	buttonModeApocalypse->winSetDrawFunc( drawModeCard );
	if( labelVersion )
	{
		labelVersion->winSetDrawFunc( drawVersion );
		UnicodeString version;
		if( TheVersion )
			version.translate( TheVersion->getReleaseVersion() );
		GadgetStaticTextSetText( labelVersion, version );
	}
}

static GameWindow *findWindow( const char *name )
{
	return TheWindowManager->winGetWindowFromId( parentMainMenu, TheNameKeyGenerator->nameToKey( AsciiString( name ) ) );
}

/** Every window the rail needs.  A mod's own MainMenu.wnd is retail's, without the panels, the Mods
	* cards or the version label; the menu then keeps EA's look rather than face missing windows. */
static Bool findMenuWindows( void )
{
	buttonSinglePlayer = findWindow( "MainMenu.wnd:ButtonSinglePlayer" );
	buttonMultiPlayer = findWindow( "MainMenu.wnd:ButtonMultiplayer" );
	buttonOnline = findWindow( "MainMenu.wnd:ButtonOnline" );
	buttonLoadReplay = findWindow( "MainMenu.wnd:ButtonLoadReplay" );
	buttonOptions = findWindow( "MainMenu.wnd:ButtonOptions" );
	buttonCredits = findWindow( "MainMenu.wnd:ButtonCredits" );
	buttonExit = findWindow( "MainMenu.wnd:ButtonExit" );
	buttonUSA = findWindow( "MainMenu.wnd:ButtonUSA" );
	buttonGLA = findWindow( "MainMenu.wnd:ButtonGLA" );
	buttonChina = findWindow( "MainMenu.wnd:ButtonChina" );
	buttonChallenge = findWindow( "MainMenu.wnd:ButtonChallenge" );
	buttonSkirmish = findWindow( "MainMenu.wnd:ButtonSkirmish" );
	buttonMods = findWindow( "MainMenu.wnd:ButtonMods" );
	buttonSingleBack = findWindow( "MainMenu.wnd:ButtonSingleBack" );
	buttonLoad = findWindow( "MainMenu.wnd:ButtonLoadGame" );
	buttonReplay = findWindow( "MainMenu.wnd:ButtonReplay" );
	buttonLoadReplayBack = findWindow( "MainMenu.wnd:ButtonLoadReplayBack" );
	buttonCampaignGenerals = findWindow( "MainMenu.wnd:ButtonCampaignGenerals" );
	buttonCampaignZH = findWindow( "MainMenu.wnd:ButtonCampaignZH" );
	for( Int m = 0; m < MISSION_BUTTONS; ++m )
	{
		AsciiString name;
		name.format( "MainMenu.wnd:ButtonMission%d", m + 1 );
		buttonMission[ m ] = findWindow( name.str() );
	}
	buttonEasy = findWindow( "MainMenu.wnd:ButtonEasy" );
	buttonMedium = findWindow( "MainMenu.wnd:ButtonMedium" );
	buttonHard = findWindow( "MainMenu.wnd:ButtonHard" );
	buttonModeDirectStrike = findWindow( "MainMenu.wnd:ButtonModeDirectStrike" );
	buttonModeApocalypse = findWindow( "MainMenu.wnd:ButtonModeApocalypse" );
	labelVersion = findWindow( "MainMenu.wnd:LabelVersion" );
	buttonWorldBuilder = findWindow( "MainMenu.wnd:ButtonWorldBuilder" );
	getUpdate = findWindow( "MainMenu.wnd:ButtonGetUpdate" );
	for( Int f = 0; f < FRAME_COUNT; ++f )
		for( Int i = 0; i < 2; ++i )
			frames[ f ][ i ] = findWindow( FRAME_NAMES[ f ][ i ] );

	for( Int i = 0; i < (Int)ARRAY_SIZE( MENU_BUTTONS ); ++i )
		if( *MENU_BUTTONS[ i ].window == NULL && MENU_BUTTONS[ i ].window != &buttonOnline )
			return FALSE;
	for( Int f = 0; f < FRAME_COUNT; ++f )
		if( frames[ f ][ 0 ] == NULL || frames[ f ][ 1 ] == NULL )
			return FALSE;
	return TRUE;
}

static Bool menuComplete = FALSE;

//-------------------------------------------------------------------------------------------------
/** Initialize the main menu */
//-------------------------------------------------------------------------------------------------
void MainMenuInit( WindowLayout *layout, void *userData )
{
	TheWritableGlobalData->m_breakTheMovie = FALSE;

	TheShell->showShellMap(TRUE);
	TheMouse->setVisibility(TRUE);
	buttonPushed = FALSE;
	isShuttingDown = FALSE;
	startGame = FALSE;
	campaignSelected = FALSE;
	launchChallengeMenu = FALSE;

	mainMenuID = TheNameKeyGenerator->nameToKey( AsciiString( "MainMenu.wnd:MainMenuParent" ) );
	parentMainMenu = TheWindowManager->winGetWindowFromId( NULL, mainMenuID );
	menuComplete = findMenuWindows();
	DEBUG_ASSERTCRASH( menuComplete, ("MainMenu.wnd is not the fork's: the rail's windows are missing\n") );

	// every entry, the first or back from a screen pushed over the menu, opens on the main list
	railList = RAIL_MAIN;
	railPanel = PANEL_NONE;
	if( menuComplete )
	{
		dressMenu();
		layOutMenu();
	}

#if defined _DEBUG || defined _INTERNAL || defined _PROFILE
	WinInstanceData instData;
#ifdef TEST_COMPRESSION
	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Compress/Decompress Maps";
	instData.setTooltipText(UnicodeString(u"Only Used in Debug and Internal!"));
	buttonCompressTest = TheWindowManager->gogoGadgetPushButton( parentMainMenu,
																									 WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
																									 25, 175,
																									 400, 400,
																									 &instData, NULL, TRUE );
#endif // TEST_COMPRESSION

	instData.init();
	BitSet( instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	instData.m_textLabelString = "Debug: Load Map";

	instData.setTooltipText(UnicodeString(u"Only Used in Debug and Internal!"));
	buttonCampaign = TheWindowManager->gogoGadgetPushButton( parentMainMenu,
																									 WIN_STATUS_ENABLED,
																									 25, 54,
																									 180, 26,
																									 &instData, NULL, TRUE );
#endif

	layout->hide( FALSE );

	if (TheGameSpyPeerMessageQueue && !TheGameSpyPeerMessageQueue->isConnected())
	{
		DEBUG_LOG(("Tearing down GameSpy from MainMenuInit()\n"));
		TearDownGameSpy();
	}
	if (TheMapCache)
		TheMapCache->updateCache();

	TheShell->loadScheme("MainMenu");
	raiseMessageBoxes = TRUE;

	if(FirstTimeRunningTheGame)
	{
		TheTransitionHandler->reverse("FadeWholeScreen");
		FirstTimeRunningTheGame = FALSE;
	}

	layout->bringForward();
	// set keyboard focus to main parent: Escape and Enter come here
	TheWindowManager->winSetFocus( parentMainMenu );

}  // end MainMenuInit

//-------------------------------------------------------------------------------------------------
/** Main menu shutdown method */
//-------------------------------------------------------------------------------------------------
void MainMenuShutdown( WindowLayout *layout, void *userData )
{
	if (!startGame)
		isShuttingDown = TRUE;

	CancelPatchCheckCallback();

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;

	if( popImmediate )
	{
		shutdownComplete( layout );
		return;

	}  //end if

	if (!startGame)
		TheShell->reverseAnimatewindow();
}  // end MainMenuShutdown

extern Bool DontShowMainMenu;

////////////////////////////////////////////////////////////////////////////
//Added By Sadullah Nader
//Added as a fix to the resolution change
//Allows the user to confirm the change, goes back to the previous mode
//if the time to change expires.
////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
// Accept Resolution callback method
//-------------------------------------------------------------------------------------------------
void AcceptResolution()
{
	//Keep new settings and bail with setting the display changed flag
	//set to off
	oldDispSettings = newDispSettings;
	dispChanged = FALSE;
}

//-------------------------------------------------------------------------------------------------
// Decline Resolution callback method
//-------------------------------------------------------------------------------------------------
void DeclineResolution()
{
	//Revert back to old resolution and reset all necessary
	//parts of the shell

	if (TheDisplay->setDisplayMode(oldDispSettings.xRes, oldDispSettings.yRes,
										oldDispSettings.bitDepth, oldDispSettings.windowed))
	{
		dispChanged = FALSE;
		newDispSettings = oldDispSettings;

		TheWritableGlobalData->m_xResolution = newDispSettings.xRes;
		TheWritableGlobalData->m_yResolution = newDispSettings.yRes;

		TheHeaderTemplateManager->headerNotifyResolutionChange();
		TheMouse->mouseNotifyResolutionChange();

		AsciiString prefString;
		prefString.format("%d %d", newDispSettings.xRes, newDispSettings.yRes);

		OptionPreferences optionPref;
		optionPref["Resolution"] = prefString;
		optionPref.write();

		// delete the shell
		delete TheShell;
		TheShell = NULL;

		// create the shell
		TheShell = MSGNEW("GameClientSubsystem") Shell;
		if( TheShell )
			TheShell->init();

		TheInGameUI->notifyResolutionChange();

		//
		// Same rule as accepting the change: in a match the thing on screen is the battlefield, and
		// the command bar comes back hidden from its layouts.  Pushing the main menu here put the
		// menu over a running game.
		//
		if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
		{
			// A new Shell is born active, and an active shell makes WindowXlat mark every mouse and
			// key message used before a game translator sees it.  See applyPendingShellRebuild.
			TheShell->hideShell();

			ShowControlBar( TRUE );
		}
		else
			TheShell->push( AsciiString("Menus/MainMenu.wnd") );
	}
}

//-------------------------------------------------------------------------------------------------
// Accept/Decline Resolution dialog method: the message box in the menu language, as every other
//-------------------------------------------------------------------------------------------------
void DoResolutionDialog()
{
	UnicodeString resolutionNew;
	UnicodeString resTimerString = TheGameText->fetch("GUI:Resolution");
	resolutionNew.format(u": %dx%d\n", newDispSettings.xRes , newDispSettings.yRes);
	resTimerString.concat(resolutionNew);

	resAcceptMenu = MessageBoxOkCancel( TheGameText->fetch("GUI:Resolution"), resTimerString, AcceptResolution, DeclineResolution );
}

//-------------------------------------------------------------------------------------------------
/** Main menu update method */
//-------------------------------------------------------------------------------------------------
void DownloadMenuUpdate( WindowLayout *layout, void *userData );
void MainMenuUpdate( WindowLayout *layout, void *userData )
{
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
	{
		return;
	}

	if (TheDownloadManager && !TheDownloadManager->isDone())
	{
		TheDownloadManager->update();
		DownloadMenuUpdate(layout, userData);
	}

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = FALSE;
	}

	HTTPThinkWrapper();
	GameSpyUpdateOverlays();

	if (startGame && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		doGameStart();
	}

	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
	}

}  // end MainMenuUpdate

/// Escape goes up one: a panel closes, a list goes back to the main one
static Bool menuBack( void )
{
	if( !menuComplete || buttonPushed )
		return FALSE;
	if( railPanel != PANEL_NONE )
		closePanel();
	else if( railList != RAIL_MAIN )
		showList( RAIL_MAIN );
	else
		return FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Main menu input callback: Escape goes up one, Enter presses the lit key */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuInput( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GWM_CHAR || !menuComplete )
		return MSG_IGNORED;
	const UnsignedByte key = (UnsignedByte)mData1;
	const UnsignedByte state = (UnsignedByte)mData2;
	if( key == KEY_ESC )
	{
		if( BitTest( state, KEY_STATE_UP ) )
			menuBack();
		return MSG_HANDLED;
	}
	if( key == KEY_ENTER || key == KEY_KPENTER )
	{
		GameWindow *lit = enterButton();
		if( BitTest( state, KEY_STATE_UP ) && lit && isShown( lit ) )
			TheWindowManager->winSendSystemMsg( parentMainMenu, GBM_SELECTED, (WindowMsgData)lit, lit->winGetWindowId() );
		return MSG_HANDLED;
	}
	return MSG_IGNORED;

}  // end MainMenuInput

//-------------------------------------------------------------------------------------------------
/* Starts the map editor beside the game, without waiting for it.  WorldBuilder is a Windows program:
	 off Windows there is none to start, so this says it failed and the menu shows its "could not
	 load" box, as it does on Windows when the exe is missing. */
static Bool startWorldBuilder( const char *exe )
{
#if defined(_WIN32)
	return _spawnl(_P_NOWAIT, exe, exe, NULL) >= 0;
#else
	(void)exe;
	return FALSE;
#endif
}

//-------------------------------------------------------------------------------------------------
// The main menu's destroy tears GameSpy, ghttp and the DNS check down, which is right when the
// shell goes for good. The options menu rebuilds the shell after a resolution change, and in an
// online match that destroy came a frame later and took the session out from under the match
// (TheGameInfo NULL in GameLogic::update). The rebuild names the live main menu here first, and
// its destroy keeps everything up. ponytail: ghttp's reference from this menu is never returned
// after that, so ghttp stays started until the process ends.
//-------------------------------------------------------------------------------------------------
static GameWindow *mainMenuKeptOnline = NULL;

void MainMenuKeepOnlineThroughRebuild( void )
{
	mainMenuKeptOnline = ( mainMenuID != NAMEKEY_INVALID )
		? TheWindowManager->winGetWindowFromId( NULL, mainMenuID )
		: NULL;
}

/// the side a side button opens, -1 for any other
static Int sideOf( GameWindow *control )
{
	if( control == buttonUSA )
		return SIDE_USA;
	if( control == buttonGLA )
		return SIDE_GLA;
	if( control == buttonChina )
		return SIDE_CHINA;
	if( control == buttonChallenge )
		return SIDE_CHALLENGE;
	return -1;
}

//-------------------------------------------------------------------------------------------------
/** Main menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType MainMenuSystem( GameWindow *window, UnsignedInt msg,
										 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		//---------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			ghttpStartup();
			break;
		}  // end case

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			freeMenuTexts();
			parentMainMenu = NULL;
			menuComplete = FALSE;
			// Thrown away by a resolution change in a match, not left: the online session, its relay
			// and the game info the running match reads every frame stay up.
			if( window != NULL && window == mainMenuKeptOnline )
			{
				mainMenuKeptOnline = NULL;
				DEBUG_LOG(("MainMenuSystem(GWM_DESTROY) - shell rebuilt in a match, GameSpy kept\n"));
				break;
			}
			ghttpCleanup();
			DEBUG_LOG(("Tearing down GameSpy from MainMenuSystem(GWM_DESTROY)\n"));
			TearDownGameSpy();
			StopAsyncDNSCheck(); // kill off the async DNS check thread in case it is still running
			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GBM_MOUSE_ENTERING:
		case GBM_MOUSE_LEAVING:
		{
			// the shell map's scripts listen for these; the faction logos EA flashed are gone
			const Bool entering = msg == GBM_MOUSE_ENTERING;
			GameWindow *control = (GameWindow *)mData1;
			if( control == buttonOnline )
				TheScriptEngine->signalUIInteract(TheShellHookNames[entering ? SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_HIGHLIGHTED : SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_UNHIGHLIGHTED]);
			else if( control == buttonMultiPlayer )
				TheScriptEngine->signalUIInteract(TheShellHookNames[entering ? SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_HIGHLIGHTED : SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_UNHIGHLIGHTED]);
			else if( control == buttonOptions )
				TheScriptEngine->signalUIInteract(TheShellHookNames[entering ? SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_HIGHLIGHTED : SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_UNHIGHLIGHTED]);
			else if( control == buttonExit )
				TheScriptEngine->signalUIInteract(TheShellHookNames[entering ? SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_HIGHLIGHTED : SHELL_SCRIPT_HOOK_MAIN_MENU_EXIT_UNHIGHLIGHTED]);
			break;
		}
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{

			GameWindow *control = (GameWindow *)mData1;

			if( buttonPushed )
				break;

#if defined _DEBUG || defined _INTERNAL || defined _PROFILE
			if( control == buttonCampaign )
			{
				buttonPushed = TRUE;
				TheShell->push(AsciiString( "Menus/MapSelectMenu.wnd" ));
				break;
			}
#ifdef TEST_COMPRESSION
			if( control == buttonCompressTest )
			{
				DoCompressTest();
				break;
			}
#endif // TEST_COMPRESSION
#endif
			if( !menuComplete )
				break;

			const Int side = sideOf( control );
			if( control == buttonSinglePlayer )
				showList( RAIL_SOLO );
			else if( control == buttonLoadReplay )
				showList( RAIL_LOAD );
			else if( control == buttonSingleBack || control == buttonLoadReplayBack )
				menuBack();
			else if( control == buttonMultiPlayer )
			{
				// straight to the LAN lobby (owner, 2026-11-25); Direct Connect is a key in there
				buttonPushed = TRUE;
				TheShell->push( AsciiString("Menus/LanLobbyMenu.wnd") );
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_NETWORK_SELECTED]);
			}
			else if( control == buttonOnline )
			{
				// back to the online screens the launcher's start opened: login, then the hub
				buttonPushed = TRUE;
				StartPatchCheck();
			}
			else if( control == buttonCredits )
			{
				buttonPushed = TRUE;
				TheShell->push( "Menus/CreditsMenu.wnd" );
			}
			else if( control == buttonLoad )
			{
				buttonPushed = TRUE;
				TheShell->push(AsciiString("Menus/SaveLoad.wnd"));
			}
			else if( control == buttonReplay )
			{
				buttonPushed = TRUE;
				TheShell->push(AsciiString("Menus/ReplayMenu.wnd"));
			}
			else if( control == buttonSkirmish )
				enterSkirmishRoom( HOST_MODE_CLASSIC );
			else if( control == buttonMods )
			{
				TheCampaignManager->setCampaign( AsciiString::TheEmptyString );
				railPanel = PANEL_MODS;
				layOutMenu();
			}
			else if( control == buttonModeDirectStrike )
				enterSkirmishRoom( HOST_MODE_DIRECT_STRIKE );
			else if( control == buttonModeApocalypse )
				enterSkirmishRoom( HOST_MODE_APOCALYPSE );
			else if( side >= 0 )
				openCampaign( side );
			else if( control == buttonCampaignZH || control == buttonCampaignGenerals )
			{
				if( railSide != SIDE_CHALLENGE )
				{
					generalsOpen = control == buttonCampaignGenerals;
					TheCampaignManager->setCampaign( generalsOpen ? GENERALS_CAMPAIGNS[ railSide ] : ZERO_HOUR_CAMPAIGNS[ railSide ] );
					loadMissions();
					layOutMenu();
				}
			}
			else if( control == buttonEasy )
				startAtDifficulty( DIFFICULTY_EASY );
			else if( control == buttonMedium )
				startAtDifficulty( DIFFICULTY_NORMAL );
			else if( control == buttonHard )
				startAtDifficulty( DIFFICULTY_HARD );
			else if( control == buttonOptions )
			{
				TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_OPTIONS_SELECTED]);

				// load the options menu
				WindowLayout *optLayout = TheShell->getOptionsLayout(TRUE);
				DEBUG_ASSERTCRASH(optLayout != NULL, ("unable to get options menu layout"));
				optLayout->runInit();
				optLayout->hide(FALSE);
				optLayout->bringForward();
			}  // end else if
			else if( control == buttonWorldBuilder && buttonWorldBuilder )
			{
#if defined _DEBUG
				if(!startWorldBuilder("WorldBuilderD.exe"))
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),NULL);
#elif defined  _INTERNAL
				if(!startWorldBuilder("WorldBuilderI.exe"))
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),NULL);
#else
				if(!startWorldBuilder("WorldBuilder.exe"))
					MessageBoxOk(TheGameText->fetch("GUI:WorldBuilder"), TheGameText->fetch("GUI:WorldBuilderLoadFailed"),NULL);
#endif
			}
			else if( control == getUpdate && getUpdate )
			{
				StartDownloadingPatches();
			}
			else if( control == buttonExit )
			{
				// every game asks first, windowed or not, with keys of its own (QuitMenu's "EXIT?" is
				// about leaving a match)
				QuitMessageBoxYesNo(TheGameText->fetch("GUI:MainMenuExitTitle"), TheGameText->fetch("GUI:MainMenuExitMessage"), quitCallback, NULL);
			}  // end else if
			else
			{
				for( Int m = 0; m < missionCount; ++m )
					if( control == buttonMission[ m ] )
						missionPicked = m;
				showPicked();
			}

			break;

		}  // end selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end MainMenuSystem
