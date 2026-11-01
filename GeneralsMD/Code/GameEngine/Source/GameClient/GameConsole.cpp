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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: GameConsole.cpp ////////////////////////////////////////////////////////////////////////
// Desc: The drop-down console.  Drawn with drawFillRect and DisplayStrings the way GraphDraw is,
//       rather than through a .wnd layout, so it needs no data file and works in the shell too.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameConsole.h"

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/OptionsCatalog.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/UserPreferences.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"

#include "GameClient/CinemaDirector.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/HotKey.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/GameText.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/View.h"

GameConsole *TheGameConsole = NULL;

static const char *CONSOLE_FONT_NAME = "Courier";
static const Int CONSOLE_FONT_POINT_SIZE = 12;
static const Bool CONSOLE_FONT_BOLD = FALSE;

static const Real CONSOLE_HEIGHT_FRACTION = 0.45f;	///< share of the screen the panel covers
static const Int CONSOLE_PADDING = 6;
static const Int CONSOLE_LINE_GAP = 2;								///< pixels between rows on top of the font height
static const Int CONSOLE_EDGE_THICKNESS = 2;

static const Int CONSOLE_MAX_ROWS = 64;								///< display strings kept alive, one per drawable row
static const Int CONSOLE_SCROLLBACK_LIMIT = 256;
static const Int CONSOLE_HISTORY_LIMIT = 32;

static const WideChar *CONSOLE_PROMPT = u"> ";
static const WideChar *CONSOLE_CURSOR = u"_";
static const WideChar CONSOLE_FIRST_PRINTABLE_CHAR = u' ';

static const Color CONSOLE_PANEL_COLOR = GameMakeColor( 0, 0, 0, 190 );	///< Diplomacy.wnd's parent
static const Color CONSOLE_EDGE_COLOR = GameMakeColor( 47, 55, 168, 255 );
static const Color CONSOLE_INPUT_COLOR = GameMakeColor( 255, 255, 255, 255 );
static const Color CONSOLE_TEXT_COLOR = GameMakeColor( 190, 190, 190, 255 );
static const Color CONSOLE_SHADOW_COLOR = GameMakeColor( 0, 0, 0, 0 );

static const Int CHEAT_PANEL_AMOUNTS = 3;			///< the most ready amounts a row of the panel offers

struct ConsoleCheat
{
	const char *name;
	CheatKind kind;
	Int defaultAmount;	///< what a bare command gives; zero marks a toggle
	const char *help;
	const char *label;	///< its row on the cheat panel
	Int amounts[ CHEAT_PANEL_AMOUNTS ];	///< the panel's keys for it, zero for none; a toggle has one on/off key
};

static const ConsoleCheat CONSOLE_CHEATS[] =
{
	{ "money",				CHEAT_MONEY,						10000,	"money [n]       add cash, 10000 by default",	"GUI:CheatMoney",	{ 1000, 10000, 100000 } },
	{ "points",				CHEAT_GENERAL_POINTS,		1,			"points [n]      add general's points",	"GUI:CheatPoints",	{ 1, 5, 10 } },
	{ "rankup",				CHEAT_RANK_UP,					1,			"rankup [n]      raise the general's rank",	"GUI:CheatRankUp",	{ 1 } },
	{ "heroic",				CHEAT_HEROIC,						1,			"heroic          every unit you have goes heroic",	"GUI:CheatHeroic",	{ 1 } },
	{ "reveal",				CHEAT_REVEAL_MAP,				1,			"reveal          lift the shroud for good",	"GUI:CheatReveal",	{ 1 } },
	{ "power",				CHEAT_INFINITE_POWER,		0,			"power           never run short of power",	"GUI:CheatPower",	{ 0 } },
	{ "nocooldown",		CHEAT_NO_COOLDOWN,			0,			"nocooldown      generals powers and superweapons always ready",	"GUI:CheatNoCooldown",	{ 0 } },
	{ "god",					CHEAT_GOD_MODE,					0,			"god             your side takes no damage",	"GUI:CheatGod",	{ 0 } },
	{ "instantbuild",	CHEAT_INSTANT_BUILD,		0,			"instantbuild    build, train and upgrade in one frame",	"GUI:CheatInstantBuild",	{ 0 } },
	{ "onehitkill",		CHEAT_ONE_HIT_KILL,			0,			"onehitkill      every hit you land kills",	"GUI:CheatOneHitKill",	{ 0 } },
};

static const Int CONSOLE_CHEAT_COUNT = sizeof( CONSOLE_CHEATS ) / sizeof( CONSOLE_CHEATS[ 0 ] );

//-------------------------------------------------------------------------------------------------
/** Cheats exist only in a campaign or skirmish match being played; anywhere else the console does
	* not mention them at all. */
//-------------------------------------------------------------------------------------------------
static Bool areCheatsAvailable( void )
{
	return TheGameLogic->isInGame() && !TheGameLogic->isInMultiplayerGame()
		&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK;
}

//-------------------------------------------------------------------------------------------------
/** Hands the cheat to the logic as a message and says what it will do.  A toggle reads the local
	* player's bit before the message lands, which is safe because only this machine sends one. */
//-------------------------------------------------------------------------------------------------
static AsciiString runCheat( const ConsoleCheat &cheat, AsciiString arguments )
{
	const Bool isToggle = cheat.defaultAmount == 0;
	const Int amount = arguments.isEmpty() ? cheat.defaultAmount : atoi( arguments.str() );

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CHEAT );
	msg->appendIntegerArgument( cheat.kind );
	msg->appendIntegerArgument( amount );

	AsciiString result;
	if( isToggle )
		result.format( "%s %s", cheat.name, ThePlayerList->getLocalPlayer()->hasCheat( cheat.kind ) ? "off" : "on" );
	else
		result.format( "%s done", cheat.name );
	return result;
}

//-------------------------------------------------------------------------------------------------
/** The game speed as a share of normal: 100 or turbo mode's 200 in a skirmish or mission, whichever is
	* nearer, 17 to 666 in a replay.  It changes how
	* often a logic frame runs and nothing inside one, so a network game, which runs at the pace the
	* whole room agrees on, is the one place it is refused. */
//-------------------------------------------------------------------------------------------------
static const Int SPEED_MIN_PERCENT = 17;		///< 10 logic frames a second
static const Int SPEED_MAX_PERCENT = 666;		///< 400 logic frames a second
static const Int SPEED_NORMAL_PERCENT = 100;

static AsciiString runSpeed( AsciiString arguments )
{
	AsciiString result;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInMultiplayerGame() )
	{
		result = "speed: single-player matches only";
		return result;
	}

	if( !arguments.isEmpty() )
	{
		Int percent = arguments.compareNoCase( "reset" ) == 0 ? SPEED_NORMAL_PERCENT : atoi( arguments.str() );
		if( percent < SPEED_MIN_PERCENT ) percent = SPEED_MIN_PERCENT;
		if( percent > SPEED_MAX_PERCENT ) percent = SPEED_MAX_PERCENT;
		Int fps = percent * LOGICFRAMES_PER_SECOND / SPEED_NORMAL_PERCENT;
		// a match someone plays runs at normal speed or in turbo mode; a replay keeps the whole range
		if( TheGameLogic->isInSkirmishGame() || TheGameLogic->isInSinglePlayerGame() )
			fps = ClampPlayerGameSpeed( fps );
		TheGameEngine->setFramesPerSecondLimit( fps );
	}

	result.format( "speed %d%%", TheGameEngine->getFramesPerSecondLimit() * SPEED_NORMAL_PERCENT / LOGICFRAMES_PER_SECOND );
	return result;
}

//-------------------------------------------------------------------------------------------------
/** "Key = value (range)" for a row of TheOptionCatalog, the value this run is using. */
//-------------------------------------------------------------------------------------------------
static AsciiString describeOption( const OptionDef &def )
{
	AsciiString result;
	if( def.kind == OPTION_BOOL )
		result.format( "%s = %s (yes/no)", def.iniKey, formatOptionValue( def, def.get() ).str() );
	else
		result.format( "%s = %s (%d..%d)", def.iniKey, formatOptionValue( def, def.get() ).str(), def.lo, def.hi );
	return result;
}

static AsciiString unknownOption( const char *command, AsciiString key )
{
	AsciiString result;
	result.format( "%s: no setting called '%s'; 'get' lists them", command, key.str() );
	return result;
}

//-------------------------------------------------------------------------------------------------
/** Any setting in the options catalog by its Options.ini key, written to Options.ini the way the
	* options menu's Accept writes it.  A row that shows the moment its GlobalData field changes is
	* changed now too; the rest wait for the next launch, because the device reset or shell rebuild
	* the menu runs after them is not the console's to start. */
//-------------------------------------------------------------------------------------------------
static AsciiString runSetOption( AsciiString arguments )
{
	AsciiString key;
	arguments.nextToken( &key );
	arguments.trim();

	const OptionDef *def = findOptionDef( key.str() );
	if( def == NULL )
		return unknownOption( "set", key );

	AsciiString result;
	Int value;
	if( !parseOptionText( *def, arguments.str(), &value ) )
	{
		if( def->kind == OPTION_BOOL )
			result.format( "set: %s takes yes or no, not '%s'", def->iniKey, arguments.str() );
		else
			result.format( "set: %s takes a whole number from %d to %d, not '%s'", def->iniKey, def->lo, def->hi, arguments.str() );
		return result;
	}

	OptionPreferences pref;
	pref[ AsciiString( def->iniKey ) ] = formatOptionValue( *def, value );
	pref.write();

	if( def->apply != APPLY_LIVE )
	{
		result.format( "%s = %s saved, from the next launch", def->iniKey, formatOptionValue( *def, value ).str() );
		return result;
	}

	def->set( value );
	return describeOption( *def );
}

//-------------------------------------------------------------------------------------------------
/** The freecam, a photo mode: the tactical view flies free and draws the whole map, the interface
	* goes, and the keys and the mouse belong to the camera until it lands.  Nothing of it reaches the
	* logic, so it is offered in every match, network games and replays included. */
//-------------------------------------------------------------------------------------------------
static const char *const FREECAM_HELP =
	"freecam on: W/S forward and back, A/D left and right, R up, F down, mouse turns, Shift faster; Esc or 'freecam' lands";
static const UnsignedInt FREECAM_STARTUP_FRAME = 2;	///< -freecam waits for the map's own opening view to land

static AsciiString theStartupFreeCamera;		///< -freecam's console line, run once the match is up

void GameConsole_setStartupFreeCamera( const char *pose )
{
	theStartupFreeCamera.format( "freecam %s", pose );
}

static AsciiString describeFreeCameraPose( void )
{
	Coord3D eye;
	Real heading, tilt;
	TheTacticalView->getFreeCameraPose( &eye, &heading, &tilt );
	AsciiString result;
	result.format( "%.0f %.0f %.0f %.1f %.1f", eye.x, eye.y, eye.z, heading * 180.0f / PI, tilt * 180.0f / PI );
	return result;
}

/** The observer's director camera, for an interface that has no spectator page to pick it from:
	* 'director' hands the camera to it, again gives it back. */
static AsciiString runDirector( void )
{
	AsciiString result;
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || local == NULL || local->isPlayerActive() )
	{
		result = "director: for an observer or a replay only";
		return result;
	}

	const Bool on = TheObserverCamera.getMode() != OBSERVER_CAMERA_DIRECTOR;
	TheObserverCamera.setMode( on ? OBSERVER_CAMERA_DIRECTOR : OBSERVER_CAMERA_FREE );
	result = on ? "director camera on; scrolling takes the camera back, 'director' again hands it over" : "director camera off";
	return result;
}

/** 'hidehud' toggles the interface; 'hidehud showmap=true' (or 'showmap true', or 'showmap') hides it
	* with the radar left in the bottom left corner, and 'showmap=false' is the plain hide. */
static AsciiString runHideHud( AsciiString arguments )
{
	AsciiString result;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		result = "hidehud: in a match or a replay only";
		return result;
	}

	Bool showMap = FALSE;
	if( !arguments.isEmpty() )
	{
		char text[ 64 ];
		strncpy( text, arguments.str(), sizeof( text ) - 1 );
		text[ sizeof( text ) - 1 ] = '\0';
		for( char *c = text; *c; ++c )
			if( *c == '=' )
				*c = ' ';

		char name[ 16 ], value[ 16 ] = "true";
		const Int given = sscanf( text, "%15s %15s", name, value );
		const AsciiString word( value );
		const Bool isTrue = word.compareNoCase( "true" ) == 0 || word.compareNoCase( "1" ) == 0;
		const Bool isFalse = word.compareNoCase( "false" ) == 0 || word.compareNoCase( "0" ) == 0;
		if( given < 1 || AsciiString( name ).compareNoCase( "showmap" ) != 0 || !( isTrue || isFalse ) )
		{
			result = "hidehud: takes nothing, or showmap=true to keep the radar in the bottom left corner";
			return result;
		}
		showMap = isTrue;
	}

	// with an argument it hides (again) with that setting, bare it toggles
	const Bool hide = arguments.isEmpty() ? !CinemaDirector_isHudHidden() : TRUE;
	CinemaDirector_setHudHidden( hide, showMap );
	result = hide ? "hud hidden; 'hidehud' again brings it back" : "hud shown";
	return result;
}

/** 'pause' opens the Esc menu, which stops the game, and 'resume' closes it again.  Not in a LAN or
	* internet game, where the Esc menu stops nothing. */
static AsciiString runPause( Bool pause )
{
	AsciiString result;
	const char *name = pause ? "pause" : "resume";
	if( !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() || TheGameLogic->isInMultiplayerGame() )
	{
		result.format( "%s: in a single player match or a replay only", name );
		return result;
	}

	if( TheInGameUI->isQuitMenuVisible() == pause )
	{
		result.format( "%s: already %s", name, pause ? "paused" : "running" );
		return result;
	}

	ToggleQuitMenu();
	result = pause ? "paused; 'resume' closes the menu" : "resumed";
	return result;
}

static AsciiString runFreeCamera( AsciiString arguments )
{
	AsciiString result;
	if( TheTacticalView == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		result = "freecam: in a match or a replay only";
		return result;
	}

	Real pose[ 5 ];
	const Int given = arguments.isEmpty() ? 0
		: sscanf( arguments.str(), "%f %f %f %f %f", &pose[ 0 ], &pose[ 1 ], &pose[ 2 ], &pose[ 3 ], &pose[ 4 ] );
	if( !arguments.isEmpty() && given != 5 )
	{
		result = "freecam: takes nothing, or x y z heading tilt with the angles in degrees";
		return result;
	}

	if( given == 5 )
	{
		TheTacticalView->setFreeCamera( TRUE );
		Coord3D eye;
		eye.set( pose[ 0 ], pose[ 1 ], pose[ 2 ] );
		TheTacticalView->setFreeCameraPose( &eye, pose[ 3 ] * PI / 180.0f, pose[ 4 ] * PI / 180.0f );
	}
	else if( TheTacticalView->isFreeCamera() )
	{
		// where it landed, in the form 'freecam x y z heading tilt' takes back
		result.format( "freecam off, was at %s", describeFreeCameraPose().str() );
		TheTacticalView->setFreeCamera( FALSE );
		return result;
	}
	else
	{
		TheTacticalView->setFreeCamera( TRUE );
	}

	if( TheGameConsole )
		TheGameConsole->closeCheatPanel();	// it would be in every picture
	result = FREECAM_HELP;
	return result;
}

//-------------------------------------------------------------------------------------------------
GameConsole::GameConsole()
	: m_isOpen( FALSE ),
		m_historyCursor( 0 ),
		m_font( NULL ),
		m_lineStrings( NULL ),
		m_lineStringCount( 0 ),
		m_cheatPanelOpen( FALSE ),
		m_cheatLayout( NULL ),
		m_cheatList( NULL ),
		m_cheatListState( -1 )
{
	m_font = TheFontLibrary->getFont( AsciiString( CONSOLE_FONT_NAME ),
																		CONSOLE_FONT_POINT_SIZE,
																		CONSOLE_FONT_BOLD );

	m_lineStrings = new DisplayString *[ CONSOLE_MAX_ROWS ];
	for( Int row = 0; row < CONSOLE_MAX_ROWS; ++row )
	{
		m_lineStrings[ row ] = TheDisplayStringManager->newDisplayString();
		m_lineStrings[ row ]->setFont( m_font );
	}
	m_lineStringCount = CONSOLE_MAX_ROWS;

	printLine( AsciiString( "Zero Hour Reforged console.  'help' lists what there is." ) );
}

//-------------------------------------------------------------------------------------------------
GameConsole::~GameConsole()
{
	for( Int row = 0; row < m_lineStringCount; ++row )
		TheDisplayStringManager->freeDisplayString( m_lineStrings[ row ] );

	delete [] m_lineStrings;
	m_lineStrings = NULL;
	m_lineStringCount = 0;
}

//-------------------------------------------------------------------------------------------------
void GameConsole::toggle( void )
{
	m_isOpen = !m_isOpen;
	if( !m_isOpen )
		m_inputLine.clear();
	DEBUG_LOG(( "Console: %s\n", m_isOpen ? "open" : "shut" ));
}

//-------------------------------------------------------------------------------------------------
void GameConsole::close( void )
{
	m_isOpen = FALSE;
	m_inputLine.clear();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::printLine( UnicodeString line )
{
	m_scrollback.push_back( line );
	while( (Int)m_scrollback.size() > CONSOLE_SCROLLBACK_LIMIT )
		m_scrollback.pop_front();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::printLine( AsciiString line )
{
	UnicodeString wide;
	wide.translate( line );
	printLine( wide );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::handleKey( UnsignedByte key, UnsignedShort keyState )
{
	if( !BitTest( keyState, KEY_STATE_DOWN ) )
		return;

	switch( key )
	{
		case KEY_ESC:
			close();
			return;

		case KEY_ENTER:
		case KEY_KPENTER:
			submitInputLine();
			return;

		case KEY_BACKSPACE:
			m_inputLine.removeLastChar();
			return;

		case KEY_UP:
			recallHistory( -1 );
			return;

		case KEY_DOWN:
			recallHistory( 1 );
			return;
	}

	// a modified key is a shortcut, not something to type
	if( BitTest( keyState, KEY_STATE_CONTROL | KEY_STATE_ALT ) )
		return;

	const Int shiftedTable = BitTest( keyState, KEY_STATE_SHIFT ) ? 1 : 0;
	const WideChar printable = TheKeyboard->getPrintableKey( key, shiftedTable );
	if( printable >= CONSOLE_FIRST_PRINTABLE_CHAR )
		m_inputLine.concat( printable );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::recallHistory( Int direction )
{
	if( m_history.empty() )
		return;

	m_historyCursor += direction;
	if( m_historyCursor < 0 )
		m_historyCursor = 0;
	if( m_historyCursor > (Int)m_history.size() )
		m_historyCursor = (Int)m_history.size();

	if( m_historyCursor == (Int)m_history.size() )
		m_inputLine.clear();
	else
		m_inputLine.translate( m_history[ m_historyCursor ] );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::submitInputLine( void )
{
	AsciiString commandLine;
	commandLine.translate( m_inputLine );
	commandLine.trim();
	m_inputLine.clear();

	if( commandLine.isEmpty() )
		return;

	UnicodeString typed;
	typed.translate( commandLine );
	UnicodeString echoed( CONSOLE_PROMPT );
	echoed.concat( typed );
	printLine( echoed );

	m_history.push_back( commandLine );
	while( (Int)m_history.size() > CONSOLE_HISTORY_LIMIT )
		m_history.erase( m_history.begin() );
	m_historyCursor = (Int)m_history.size();

	runCommand( commandLine );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::runCommand( AsciiString commandLine )
{
	AsciiString arguments = commandLine;
	AsciiString command;
	arguments.nextToken( &command );
	command.toLower();
	arguments.trim();

	if( command == "help" )
	{
		printLine( AsciiString( "help          this list" ) );
		printLine( AsciiString( "clear         empty the scrollback" ) );
		printLine( AsciiString( "echo <text>   print the text back" ) );
		printLine( AsciiString( "speed [n]     game speed in percent, 100 is normal, 'reset' goes back" ) );
		printLine( AsciiString( "get [key]     a setting by its Options.ini key; no key lists them all" ) );
		printLine( AsciiString( "set <key> <v> change a setting and save it, e.g. 'set ShowNetBox no' hides the" ) );
		printLine( AsciiString( "              top right info strip" ) );
		printLine( AsciiString( "freecam       photo mode: fly the camera anywhere, the whole map drawn, no interface." ) );
		printLine( AsciiString( "              W/S forward and back, A/D left and right, R up, F down, mouse turns," ) );
		printLine( AsciiString( "              Shift faster; Esc or 'freecam' again lands.  'freecam x y z heading tilt'" ) );
		printLine( AsciiString( "              flies to a pose, angles in degrees" ) );
		printLine( AsciiString( "director      observer: the director camera on, again gives the camera back" ) );
		printLine( AsciiString( "pause, resume open and close the Esc menu, single player only" ) );
		printLine( AsciiString( "hidehud       the interface off, again brings it back; showmap=true keeps the radar" ) );
		if( areCheatsAvailable() )
		{
			printLine( AsciiString( "cheats        single-player cheats" ) );
			printLine( AsciiString( "trainer       the cheats on a panel, one click each" ) );
		}
		return;
	}

	const Bool cheatsAvailable = areCheatsAvailable();
	if( cheatsAvailable && command == "cheats" )
	{
		for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
			printLine( AsciiString( CONSOLE_CHEATS[ i ].help ) );
		printLine( AsciiString( "trainer         all of these on a panel; trainer again, X or Esc closes it" ) );
		return;
	}

	// the console drops out of the way so the panel is under the pointer at once
	if( cheatsAvailable && command == "trainer" )
	{
		m_cheatPanelOpen = !m_cheatPanelOpen;
		if( m_cheatPanelOpen )
			close();
		return;
	}

	for( Int i = 0; cheatsAvailable && i < CONSOLE_CHEAT_COUNT; ++i )
	{
		if( command == CONSOLE_CHEATS[ i ].name )
		{
			printLine( runCheat( CONSOLE_CHEATS[ i ], arguments ) );
			return;
		}
	}

	if( command == "clear" )
	{
		m_scrollback.clear();
		return;
	}

	if( command == "echo" )
	{
		printLine( arguments );
		return;
	}

	if( command == "speed" )
	{
		printLine( runSpeed( arguments ) );
		return;
	}

	if( command == "freecam" )
	{
		printLine( runFreeCamera( arguments ) );
		// out of the way of the picture, and of the keys the camera now takes
		if( TheTacticalView && TheTacticalView->isFreeCamera() )
			close();
		return;
	}

	if( command == "pause" || command == "resume" )
	{
		printLine( runPause( command == "pause" ) );
		// out of the way of the menu it opens
		if( TheInGameUI->isQuitMenuVisible() )
			close();
		return;
	}

	if( command == "director" )
	{
		printLine( runDirector() );
		return;
	}

	if( command == "hidehud" )
	{
		printLine( runHideHud( arguments ) );
		return;
	}

	if( command == "get" )
	{
		if( arguments.isEmpty() )
		{
			for( Int i = 0; i < TheOptionCatalogCount; ++i )
				printLine( describeOption( TheOptionCatalog[ i ] ) );
			return;
		}
		const OptionDef *def = findOptionDef( arguments.str() );
		printLine( def == NULL ? unknownOption( "get", arguments ) : describeOption( *def ) );
		return;
	}

	if( command == "set" )
	{
		printLine( runSetOption( arguments ) );
		return;
	}

	UnicodeString unknown( u"unknown command: " );
	UnicodeString name;
	name.translate( command );
	unknown.concat( name );
	printLine( unknown );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::render( void )
{
	if( !theStartupFreeCamera.isEmpty() && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()
			&& TheGameLogic->getFrame() >= FREECAM_STARTUP_FRAME )
	{
		const AsciiString line = theStartupFreeCamera;
		theStartupFreeCamera.clear();
		runCommand( line );
		DEBUG_LOG(( "-freecam: %s\n", line.str() ));
	}

	renderCheatPanel();		// under the console, which covers it when it drops

	if( !m_isOpen )
		return;

	const Int screenWidth = TheDisplay->getWidth();
	const Int panelHeight = REAL_TO_INT( TheDisplay->getHeight() * CONSOLE_HEIGHT_FRACTION );
	const Int lineHeight = m_font->height + CONSOLE_LINE_GAP;

	// EA's diplomacy and chat frame: its see-through black over a blue edge
	TheDisplay->drawFillRect( 0, 0, screenWidth, panelHeight, CONSOLE_PANEL_COLOR );
	TheDisplay->drawFillRect( 0, panelHeight - CONSOLE_EDGE_THICKNESS,
														screenWidth, CONSOLE_EDGE_THICKNESS, CONSOLE_EDGE_COLOR );

	Int y = panelHeight - CONSOLE_EDGE_THICKNESS - CONSOLE_PADDING - lineHeight;

	UnicodeString prompt( CONSOLE_PROMPT );
	prompt.concat( m_inputLine );
	prompt.concat( CONSOLE_CURSOR );
	m_lineStrings[ 0 ]->setText( prompt );
	m_lineStrings[ 0 ]->draw( CONSOLE_PADDING, y, CONSOLE_INPUT_COLOR, CONSOLE_SHADOW_COLOR );

	Int rowsThatFit = (y - CONSOLE_PADDING) / lineHeight;
	if( rowsThatFit > m_lineStringCount - 1 )
		rowsThatFit = m_lineStringCount - 1;

	Int row = 0;
	ScrollbackLines::reverse_iterator it = m_scrollback.rbegin();
	while( row < rowsThatFit && it != m_scrollback.rend() )
	{
		y -= lineHeight;
		m_lineStrings[ row + 1 ]->setText( *it );
		m_lineStrings[ row + 1 ]->draw( CONSOLE_PADDING, y, CONSOLE_TEXT_COLOR, CONSOLE_SHADOW_COLOR );
		++row;
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
/** The trainer's window while it is open.  It shuts itself wherever the cheats are refused, so a
	* panel left open does not follow the player into a replay. */
//-------------------------------------------------------------------------------------------------
void GameConsole::renderCheatPanel( void )
{
	if( m_cheatPanelOpen && !areCheatsAvailable() )
		m_cheatPanelOpen = FALSE;
	updateCheatWindow();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::runCheatPanelAction( const std::string &action )
{
	AsciiString arguments( action.c_str() );
	AsciiString name;
	arguments.nextToken( &name );
	arguments.trim();
	for( Int i = 0; !name.isEmpty() && areCheatsAvailable() && i < CONSOLE_CHEAT_COUNT; ++i )
	{
		if( name == CONSOLE_CHEATS[ i ].name )
		{
			const AsciiString result = runCheat( CONSOLE_CHEATS[ i ], arguments );
			printLine( result );
			DEBUG_LOG(( "Cheat panel: %s\n", result.str() ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The trainer's rows: a toggle is one row, an amount cheat one row for each ready amount. */
//-------------------------------------------------------------------------------------------------
struct CheatWindowRow
{
	Int cheat;
	Int amount;		///< zero for a toggle
};

static const Int CHEAT_WINDOW_MAX_ROWS = CONSOLE_CHEAT_COUNT * CHEAT_PANEL_AMOUNTS;

static Int cheatWindowRows( CheatWindowRow *rows )
{
	Int count = 0;
	for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
	{
		const ConsoleCheat &cheat = CONSOLE_CHEATS[ i ];
		for( Int each = 0; each < CHEAT_PANEL_AMOUNTS && ( each == 0 || cheat.amounts[ each ] != 0 ); ++each )
		{
			rows[ count ].cheat = i;
			rows[ count ].amount = cheat.amounts[ each ];
			++count;
			if( cheat.defaultAmount == 0 )
				break;
		}
	}
	return count;
}

static std::string cheatWindowAction( const CheatWindowRow &row )
{
	const ConsoleCheat &cheat = CONSOLE_CHEATS[ row.cheat ];
	if( row.amount == 0 )
		return cheat.name;
	char text[ 64 ];
	sprintf( text, "%s %d", cheat.name, row.amount );
	return text;
}

/// EA's diplomacy text colours: white, and the green its rows light in
static const Color CHEAT_WINDOW_TEXT_COLOR = GameMakeColor( 254, 254, 254, 255 );
static const Color CHEAT_WINDOW_ON_COLOR = GameMakeColor( 3, 196, 0, 255 );

static WindowMsgHandledType cheatWindowSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	static const NameKeyType buttonHideID = NAMEKEY( "Trainer.wnd:ButtonHide" );
	switch( msg )
	{
		case GBM_SELECTED:
			if( ((GameWindow *)mData1)->winGetWindowId() == buttonHideID )
				TheGameConsole->closeCheatPanel();
			return MSG_HANDLED;
		case GLM_SELECTED:
			TheGameConsole->runCheatWindowRow( (Int)mData2 );
			return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
void GameConsole::runCheatWindowRow( Int row )
{
	CheatWindowRow rows[ CHEAT_WINDOW_MAX_ROWS ];
	if( row < 0 || row >= cheatWindowRows( rows ) )
		return;
	runCheatPanelAction( cheatWindowAction( rows[ row ] ) );
	GadgetListBoxSetSelected( m_cheatList, -1 );	// so a second click on the same row runs it again
}

//-------------------------------------------------------------------------------------------------
void GameConsole::resetCheatWindow( void )
{
	if( m_cheatLayout )
	{
		m_cheatLayout->destroyWindows();
		m_cheatLayout->deleteInstance();
	}
	m_cheatLayout = NULL;
	m_cheatList = NULL;
	m_cheatListState = -1;
}

//-------------------------------------------------------------------------------------------------
/** The trainer: Window/Trainer.wnd, EA's diplomacy frame with one list row for
	* each click, and a toggle's row green while it is on.  The windows are thrown away when it
	* shuts, never from inside their own callback. */
//-------------------------------------------------------------------------------------------------
void GameConsole::updateCheatWindow( void )
{
	if( !m_cheatPanelOpen )
	{
		resetCheatWindow();
		return;
	}

	CheatWindowRow rows[ CHEAT_WINDOW_MAX_ROWS ];
	const Int rowCount = cheatWindowRows( rows );
	if( m_cheatLayout == NULL )
	{
		m_cheatLayout = TheWindowManager->winCreateLayout( AsciiString( "Trainer.wnd" ) );
		GameWindow *parent = m_cheatLayout ? m_cheatLayout->getFirstWindow() : NULL;
		m_cheatList = parent ? TheWindowManager->winGetWindowFromId( parent, NAMEKEY( "Trainer.wnd:ListboxCheats" ) ) : NULL;
		if( m_cheatList == NULL )
		{
			printLine( AsciiString( "trainer: Window/Trainer.wnd is missing" ) );
			resetCheatWindow();
			m_cheatPanelOpen = FALSE;
			return;
		}
		parent->winSetSystemFunc( cheatWindowSystem );
		for( Int row = 0; row < rowCount; ++row )
			GadgetListBoxAddEntryText( m_cheatList, TheGameText->fetch( CONSOLE_CHEATS[ rows[ row ].cheat ].label ),
																 CHEAT_WINDOW_TEXT_COLOR, -1, 0 );
		m_cheatLayout->hide( FALSE );
	}

	const Player *local = ThePlayerList->getLocalPlayer();
	Int state = 0;
	for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
		if( CONSOLE_CHEATS[ i ].defaultAmount == 0 && local->hasCheat( CONSOLE_CHEATS[ i ].kind ) )
			state |= 1 << i;
	if( state == m_cheatListState )
		return;
	const Bool firstFill = m_cheatListState < 0;
	m_cheatListState = state;

	for( Int row = 0; row < rowCount; ++row )
	{
		const ConsoleCheat &cheat = CONSOLE_CHEATS[ rows[ row ].cheat ];
		const Bool on = rows[ row ].amount == 0 && ( state & ( 1 << rows[ row ].cheat ) ) != 0;
		UnicodeString text;
		if( rows[ row ].amount == 0 )
			text = TheGameText->fetch( on ? "GUI:CheatOn" : "GUI:CheatOff" );
		else if( cheat.amounts[ 1 ] == 0 )
			text = TheGameText->fetch( "GUI:CheatApply" );
		else
			text.format( u"+%d", rows[ row ].amount );
		const Color color = on ? CHEAT_WINDOW_ON_COLOR : CHEAT_WINDOW_TEXT_COLOR;
		GadgetListBoxAddEntryText( m_cheatList, TheGameText->fetch( cheat.label ), color, row, 0 );
		GadgetListBoxAddEntryText( m_cheatList, text, color, row, 1 );
	}

	// where each row landed, so a script driving the game over -control knows where to click
	if( firstFill )
	{
		const ListboxData *list = (const ListboxData *)m_cheatList->winGetUserData();
		Int x, y, width, height;
		m_cheatList->winGetScreenPosition( &x, &y );
		m_cheatList->winGetSize( &width, &height );
		for( Int row = 0; list && row < rowCount && row < list->endPos; ++row )
		{
			const Int top = row > 0 ? list->listData[ row - 1 ].listHeight : 0;
			DEBUG_LOG(( "Cheat window: \"%s\" at %d %d\n", cheatWindowAction( rows[ row ] ).c_str(),
									x + width / 2, y + ( top + list->listData[ row ].listHeight ) / 2 ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** While the freecam flies every key is the camera's, so nothing reaches a hotkey or an order:
	* W/A/S/D/R/F are held for the flight, Esc lands on its release (the press is eaten too, so the
	* quit menu never sees either half), and F12 still takes a picture. */
//-------------------------------------------------------------------------------------------------
static UnsignedInt theFreeCameraKeys = 0;

static GameMessageDisposition translateFreeCameraKey( UnsignedByte key, UnsignedShort keyState )
{
	const Bool down = BitTest( keyState, KEY_STATE_DOWN );
	UnsignedInt bit = 0;
	switch( key )
	{
		case KEY_W: bit = View::FREECAM_FORWARD; break;
		case KEY_S: bit = View::FREECAM_BACK; break;
		case KEY_A: bit = View::FREECAM_LEFT; break;
		case KEY_D: bit = View::FREECAM_RIGHT; break;
		case KEY_R: bit = View::FREECAM_UP; break;
		case KEY_F: bit = View::FREECAM_DOWN; break;

		case KEY_ESC:
			if( !down )
			{
				TheGameConsole->printLine( runFreeCamera( AsciiString::TheEmptyString ) );
				theFreeCameraKeys = 0;
			}
			return DESTROY_MESSAGE;

		case KEY_F12:
			return KEEP_MESSAGE;
	}

	if( bit != 0 )
	{
		if( down )
			theFreeCameraKeys |= bit;
		else
			theFreeCameraKeys &= ~bit;
		TheTacticalView->setFreeCameraKeys( theFreeCameraKeys );
	}
	return DESTROY_MESSAGE;
}

//-------------------------------------------------------------------------------------------------
GameMessageDisposition GameConsoleTranslator::translateGameMessage( const GameMessage *msg )
{
	if( TheGameConsole == NULL )
		return KEEP_MESSAGE;

	const Bool freeCamera = TheTacticalView && TheTacticalView->isFreeCamera();
	if( !freeCamera )
		theFreeCameraKeys = 0;

	switch( msg->getType() )
	{
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			const UnsignedByte key = msg->getArgument( 0 )->integer;
			const UnsignedShort keyState = msg->getArgument( 1 )->integer;

			// The key above Tab belongs to the console whether it is open or shut, unless a button on
			// screen is labelled with it: Contra's Special Powers menu is "&`", and its tooltip sends the
			// player to this key.  Shift still reaches the console.  The button is asked for by name and
			// not by what the layout types here, which on a Turkish keyboard is a double quote.
			if( key == KEY_TICK )
			{
				if( BitTest( keyState, KEY_STATE_DOWN ) && !BitTest( keyState, KEY_STATE_AUTOREPEAT ) )
				{
					const Bool plain = !BitTest( keyState, KEY_STATE_CONTROL | KEY_STATE_SHIFT | KEY_STATE_ALT );
					if( TheGameConsole->isOpen() || !plain || !TheHotKeyManager->executeHotKey( HotKeyManager::nameOf( u'`' ) ) )
						TheGameConsole->toggle();
				}
				return DESTROY_MESSAGE;
			}

			if( TheGameConsole->isOpen() )
			{
				// a flight key let go while the console had it would otherwise stay held
				if( freeCamera && theFreeCameraKeys != 0 )
				{
					theFreeCameraKeys = 0;
					TheTacticalView->setFreeCameraKeys( 0 );
				}
				TheGameConsole->handleKey( key, keyState );
				return DESTROY_MESSAGE;
			}

			if( freeCamera )
				return translateFreeCameraKey( key, keyState );

			// Esc shuts the cheat panel rather than opening the quit menu: the press is eaten, and the
			// release shuts it so that release is not left to open the menu either
			if( key == KEY_ESC && TheGameConsole->isCheatPanelOpen() )
			{
				if( !BitTest( keyState, KEY_STATE_DOWN ) )
					TheGameConsole->closeCheatPanel();
				return DESTROY_MESSAGE;
			}
			return KEEP_MESSAGE;
		}

		// the wheel carries no position, and the cursor's moves are everybody's
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		case GameMessage::MSG_RAW_MOUSE_WHEEL:
			return freeCamera ? DESTROY_MESSAGE : KEEP_MESSAGE;
	}

	// the freecam's mouse only turns the view (W3DView reads the pointer itself): no click selects,
	// orders or scrolls anything while it flies
	if( freeCamera && msg->getType() > GameMessage::MSG_RAW_MOUSE_BEGIN && msg->getType() < GameMessage::MSG_RAW_MOUSE_END )
		return DESTROY_MESSAGE;

	return KEEP_MESSAGE;
}
