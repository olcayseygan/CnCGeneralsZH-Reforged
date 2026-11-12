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
#include "Common/version.h"
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
#include "GameClient/GadgetPushButton.h"
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

struct ConsoleCheat
{
	const char *name;
	CheatKind kind;
	Int defaultAmount;	///< what a bare command gives; zero marks a toggle
	const char *help;
};

/// "rankup max": more ranks than any general has, which Player::setRankLevel clamps to the top one
static const Int CHEAT_RANK_MAX = 100;

static const ConsoleCheat CONSOLE_CHEATS[] =
{
	{ "money",					CHEAT_MONEY,						10000,	"money [n]       add cash, 10000 by default" },
	{ "points",					CHEAT_GENERAL_POINTS,		1,			"points [n]      add general's points" },
	{ "freepoints",			CHEAT_UNLIMITED_POINTS,	0,			"freepoints      sciences cost no general's points" },
	{ "rankup",					CHEAT_RANK_UP,					1,			"rankup [n|max]  raise the general's rank, max to the top one" },
	{ "heroic",					CHEAT_HEROIC,						1,			"heroic          every unit you have goes heroic" },
	{ "reveal",					CHEAT_REVEAL_MAP,				1,			"reveal          lift the shroud for good" },
	{ "power",					CHEAT_INFINITE_POWER,		0,			"power           never run short of power" },
	{ "nocooldown",			CHEAT_NO_COOLDOWN,			0,			"nocooldown      generals powers and superweapons always ready" },
	{ "god",						CHEAT_GOD_MODE,					0,			"god             your side takes no damage" },
	{ "instantbuild",		CHEAT_INSTANT_BUILD,		0,			"instantbuild    build, train and upgrade in one frame" },
	{ "onehitkill",			CHEAT_ONE_HIT_KILL,			0,			"onehitkill      every hit you land kills" },
	{ "buildanywhere",	CHEAT_BUILD_ANYWHERE,		0,			"buildanywhere   structures stand up finished where you put them" },
};

static const Int CONSOLE_CHEAT_COUNT = sizeof( CONSOLE_CHEATS ) / sizeof( CONSOLE_CHEATS[ 0 ] );

//-------------------------------------------------------------------------------------------------
/** A match being played or watched, replays included.  The animated map behind the main menu is a
	* game to the logic too, which offered the cheats and the speed command on the main menu, and the
	* load before a match is not one yet. */
//-------------------------------------------------------------------------------------------------
static Bool isInMatch( void )
{
	return TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap();
}

//-------------------------------------------------------------------------------------------------
/** Cheats exist only in a campaign or skirmish match being played; anywhere else the console does
	* not mention them at all. */
//-------------------------------------------------------------------------------------------------
static Bool areCheatsAvailable( void )
{
	return isInMatch() && !TheGameLogic->isInMultiplayerGame()
		&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK;
}

/// what 'help' prints, a command's continuation lines carrying its flag
struct ConsoleHelpLine
{
	Bool matchOnly;
	const char *text;
};

static const ConsoleHelpLine CONSOLE_HELP[] =
{
	{ FALSE,	"help          this list" },
	{ FALSE,	"clear         empty the scrollback" },
	{ FALSE,	"echo <text>   print the text back" },
	{ FALSE,	"version       the game's version and build date" },
	{ FALSE,	"get [key]     a setting by its Options.ini key; no key lists them all" },
	{ FALSE,	"set <key> <v> change a setting and save it, e.g. 'set ShowNetBox no' hides the" },
	{ FALSE,	"              top right info strip" },
	{ TRUE,		"speed [n]     game speed in percent of retail, 100 125 150 200, 'reset' the native one" },
	{ TRUE,		"freecam       photo mode: fly the camera anywhere, the whole map drawn, no interface." },
	{ TRUE,		"              W/S forward and back, A/D left and right, R up, F down, mouse turns," },
	{ TRUE,		"              Shift faster; Esc or 'freecam' again lands.  'freecam x y z heading tilt'" },
	{ TRUE,		"              flies to a pose, angles in degrees" },
	{ TRUE,		"director      observer: the director camera on, again gives the camera back" },
	{ TRUE,		"pause, resume open and close the Esc menu, single player only" },
	{ TRUE,		"hidehud       the interface off, again brings it back; showmap=true keeps the radar" },
};

static const char *const MATCH_ONLY_COMMANDS[] =
	{ "speed", "freecam", "director", "pause", "resume", "hidehud", "cheats", "trainer" };

static Bool isMatchOnlyCommand( const AsciiString &command )
{
	for( Int i = 0; i < (Int)ARRAY_SIZE( MATCH_ONLY_COMMANDS ); ++i )
		if( command == MATCH_ONLY_COMMANDS[ i ] )
			return TRUE;
	for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
		if( command == CONSOLE_CHEATS[ i ].name )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Hands the cheat to the logic as a message and says what it will do.  A toggle reads the local
	* player's bit before the message lands, which is safe because only this machine sends one. */
//-------------------------------------------------------------------------------------------------
static AsciiString runCheat( const ConsoleCheat &cheat, AsciiString arguments )
{
	const Bool isToggle = cheat.defaultAmount == 0;
	Int amount = arguments.isEmpty() ? cheat.defaultAmount : atoi( arguments.str() );
	if( cheat.kind == CHEAT_RANK_UP && arguments.compareNoCase( "max" ) == 0 )
		amount = CHEAT_RANK_MAX;

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
/** The game speed in percent of retail's pace: one of the player speeds (100, 125, 150, 200), the
	* nearest, in a skirmish or mission, 10 to 400 logic frames a second in a replay.  "reset" is the
	* native tick, GAME_SPEED_PERCENT.  It changes how often a logic frame runs and nothing inside one, so
	* a network game, which runs at the pace the whole room agrees on, is the one place it is refused. */
//-------------------------------------------------------------------------------------------------
static const Int SPEED_MIN_FPS = 10;
static const Int SPEED_MAX_FPS = 400;

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
		const Int percent = arguments.compareNoCase( "reset" ) == 0 ? PLAYER_GAME_SPEED_PERCENT_DEFAULT : atoi( arguments.str() );
		Int fps;
		if( TheGameLogic->isInSkirmishGame() || TheGameLogic->isInSinglePlayerGame() )
			fps = GameEngine_framesPerSecondForSpeed( ClampPlayerGameSpeedPercent( percent ) );
		else
		{
			// a replay keeps the whole range
			fps = percent * LOGICFRAMES_PER_SECOND / GAME_SPEED_PERCENT;
			if( fps < SPEED_MIN_FPS ) fps = SPEED_MIN_FPS;
			if( fps > SPEED_MAX_FPS ) fps = SPEED_MAX_FPS;
		}
		TheGameEngine->setFramesPerSecondLimit( fps );
	}

	result.format( "speed %d%%", TheGameEngine->getFramesPerSecondLimit() * GAME_SPEED_PERCENT / LOGICFRAMES_PER_SECOND );
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
		m_cheatLayout( NULL )
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
/** GameClient::update, which reads the keyboard, does not run while a map loads, and the load screen's
	* frames skipped the console's draw too, so it could not be opened during a load at all.  Nothing
	* else wants a key there, which is why this one reader can take them all. */
//-------------------------------------------------------------------------------------------------
void GameConsole::readLoadScreenKeys( void )
{
	if( TheKeyboard == NULL )
		return;

	TheKeyboard->UPDATE();
	for( KeyboardIO *io = TheKeyboard->getFirstKey(); io->key != KEY_NONE; ++io )
	{
		if( io->key != KEY_TICK )
		{
			if( m_isOpen )
				handleKey( io->key, io->state );
		}
		else if( BitTest( io->state, KEY_STATE_DOWN ) && !BitTest( io->state, KEY_STATE_AUTOREPEAT ) )
			toggle();
		io->status = KeyboardIO::STATUS_USED;
	}
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

	const Bool inMatch = isInMatch();
	if( !inMatch && isMatchOnlyCommand( command ) )
	{
		AsciiString refusal;
		refusal.format( "%s: works only in a match", command.str() );
		printLine( refusal );
		return;
	}

	if( command == "help" )
	{
		for( Int i = 0; i < (Int)ARRAY_SIZE( CONSOLE_HELP ); ++i )
			if( inMatch || !CONSOLE_HELP[ i ].matchOnly )
				printLine( AsciiString( CONSOLE_HELP[ i ].text ) );
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

	// what the options menu's corner says, with the build date that tells two builds of it apart
	if( command == "version" )
	{
		AsciiString line;
		line.format( "Reforged %s, built %s", TheVersion->getReleaseVersion().str(), TheVersion->getAsciiBuildTime().str() );
		printLine( line );
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
// The trainer: Trainer.wnd's draggable window, and on it, the talents' way (ControlBar.cpp), push
// buttons whose own draw lays the side's Tab screen pieces down (<TooltipImage>TabSolo, TabExit and
// TabSend, Tools/hud_command_bar.py tab_panel): three tabs in the painting's header band, a row a
// cheat in its well, a toggle's row green while it is on, the open tab ringed in the side's colour.
// A scheme without the Tab screen (Classic) gets the same layout in grey plates.  Every rect is in
// the solo painting's texels scaled to 1000 wide, one a pixel at 1080.
//-------------------------------------------------------------------------------------------------
namespace
{
	enum { TRAINER_DESIGN_W = 1000, TRAINER_DESIGN_H = 518 };
	enum { TRAINER_TAB_PLAYER, TRAINER_TAB_UNITS, TRAINER_TAB_MAP, TRAINER_TAB_COUNT };
	enum { TRAINER_ROW_BUTTONS = 4 };

	struct TrainerRow
	{
		Int tab;
		const char *label;
		const char *cheat;									///< the console command its buttons run
		Int amounts[ TRAINER_ROW_BUTTONS ];	///< a button each, zero past the last; a toggle's one button is a zero
	};

	const TrainerRow TRAINER_ROWS[] =
	{
		{ TRAINER_TAB_PLAYER,	"GUI:CheatMoney",						"money",					{ 1000, 10000, 100000, 1000000 } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatPoints",					"points",					{ 1, 5, 10 } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatUnlimitedPoints",	"freepoints",			{ 0 } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatRankUp",					"rankup",					{ 1, CHEAT_RANK_MAX } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatPower",						"power",					{ 0 } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatInstantBuild",		"instantbuild",		{ 0 } },
		{ TRAINER_TAB_PLAYER,	"GUI:CheatNoCooldown",			"nocooldown",			{ 0 } },
		{ TRAINER_TAB_UNITS,	"GUI:CheatGod",							"god",						{ 0 } },
		{ TRAINER_TAB_UNITS,	"GUI:CheatOneHitKill",			"onehitkill",			{ 0 } },
		{ TRAINER_TAB_UNITS,	"GUI:CheatHeroic",					"heroic",					{ 1 } },
		{ TRAINER_TAB_MAP,		"GUI:CheatReveal",					"reveal",					{ 1 } },
		{ TRAINER_TAB_MAP,		"GUI:CheatBuildAnywhere",		"buildanywhere",	{ 0 } },
	};
	enum { TRAINER_ROW_COUNT = sizeof( TRAINER_ROWS ) / sizeof( TRAINER_ROWS[ 0 ] ) };
	const char *const TRAINER_TAB_LABELS[ TRAINER_TAB_COUNT ] = { "GUI:CheatTabPlayer", "GUI:CheatTabUnits", "GUI:CheatTabMap" };
	const char *const TRAINER_TAB_ACTIONS[ TRAINER_TAB_COUNT ] = { "tab player", "tab units", "tab map" };

	/// the buttons: the tabs, the close, then every row's in table order
	enum { TRAINER_BUTTON_CLOSE = TRAINER_TAB_COUNT, TRAINER_FIRST_CHEAT_BUTTON,
				 TRAINER_MAX_BUTTONS = TRAINER_FIRST_CHEAT_BUTTON + TRAINER_ROW_COUNT * TRAINER_ROW_BUTTONS };

	// the painting's places: the header band's three cells, the socket at the bottom right where the Tab
	// screen has Main Menu, the rule at the bottom left, the well the rows stand in
	const Int theTrainerTabX[ TRAINER_TAB_COUNT ][ 2 ] = { { 92, 272 }, { 367, 547 }, { 672, 852 } };
	const Int theTrainerTabY[ 2 ] = { 15, 55 };
	const Int theTrainerClose[ 4 ] = { 844, 470, 960, 506 };
	const Int theTrainerTitle[ 4 ] = { 72, 450, 260, 474 };
	const Int TRAINER_ROW_TOP = 82, TRAINER_ROW_PITCH = 51, TRAINER_ROW_H = 43;
	const Int theTrainerRowX[ 2 ] = { 28, 972 };
	const Int TRAINER_LAMP_W = 6;
	const Int theTrainerLabelX[ 2 ] = { 48, 440 };
	const Int TRAINER_BUTTON_W = 116, TRAINER_BUTTON_GAP = 10, TRAINER_BUTTONS_RIGHT = 960, TRAINER_BUTTON_INSET = 2;

	const Color TRAINER_WHITE = GameMakeColor( 240, 240, 240, 255 );
	const Color TRAINER_DIM = GameMakeColor( 200, 198, 192, 255 );		// the talents' rejected text
	const Color TRAINER_GOLD = GameMakeColor( 242, 194, 48, 255 );		// the talents' waiting gold
	const Color TRAINER_GREEN = GameMakeColor( 72, 224, 64, 255 );
	const Color TRAINER_TRAY = GameMakeColor( 255, 255, 255, 14 );
	const Color TRAINER_TRAY_ON = GameMakeColor( 72, 224, 64, 30 );

	Int theTrainerTab = TRAINER_TAB_PLAYER;		///< the tab on show, kept while the panel is shut
	GameWindow *theTrainerButtons[ TRAINER_MAX_BUTTONS ];
	FittedLabel theTrainerButtonLabels[ TRAINER_MAX_BUTTONS ];
	FittedLabel theTrainerRowLabels[ TRAINER_ROW_COUNT ];
	FittedLabel theTrainerTitleLabel;

	const ConsoleCheat *findConsoleCheat( const char *name )
	{
		for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
			if( strcmp( CONSOLE_CHEATS[ i ].name, name ) == 0 )
				return &CONSOLE_CHEATS[ i ];
		return NULL;
	}

	Bool isToggleRow( Int row )
	{
		const ConsoleCheat *cheat = findConsoleCheat( TRAINER_ROWS[ row ].cheat );
		return cheat && cheat->defaultAmount == 0;
	}

	Bool isRowOn( Int row )
	{
		const ConsoleCheat *cheat = findConsoleCheat( TRAINER_ROWS[ row ].cheat );
		const Player *local = ThePlayerList->getLocalPlayer();
		return cheat && cheat->defaultAmount == 0 && local && local->hasCheat( cheat->kind );
	}

	Int rowButtonCount( Int row )
	{
		Int n = 1;
		while( n < TRAINER_ROW_BUTTONS && TRAINER_ROWS[ row ].amounts[ n ] != 0 )
			++n;
		return n;
	}

	/// cheat button 'index' (from the first one): its row and its place in the row; FALSE past the last
	Bool trainerCheatButton( Int index, Int *row, Int *slot )
	{
		for( Int r = 0; r < TRAINER_ROW_COUNT; ++r )
		{
			const Int n = rowButtonCount( r );
			if( index < n )
			{
				*row = r;
				*slot = index;
				return TRUE;
			}
			index -= n;
		}
		return FALSE;
	}

	/// the row's place among its tab's rows, top down
	Int rowPlaceInTab( Int row )
	{
		Int place = 0;
		for( Int r = 0; r < row; ++r )
			if( TRAINER_ROWS[ r ].tab == TRAINER_ROWS[ row ].tab )
				++place;
		return place;
	}

	IRegion2D designRect( Int x0, Int y0, Int x1, Int y1 )
	{
		IRegion2D r;
		r.lo.x = x0;
		r.lo.y = y0;
		r.hi.x = x1;
		r.hi.y = y1;
		return r;
	}

	IRegion2D rowRect( Int row )
	{
		const Int top = TRAINER_ROW_TOP + rowPlaceInTab( row ) * TRAINER_ROW_PITCH;
		return designRect( theTrainerRowX[ 0 ], top, theTrainerRowX[ 1 ], top + TRAINER_ROW_H );
	}

	/// button 'button' in design texels; FALSE for none
	Bool trainerButtonRect( Int button, IRegion2D *r )
	{
		if( button < TRAINER_TAB_COUNT )
		{
			*r = designRect( theTrainerTabX[ button ][ 0 ], theTrainerTabY[ 0 ], theTrainerTabX[ button ][ 1 ], theTrainerTabY[ 1 ] );
			return TRUE;
		}
		if( button == TRAINER_BUTTON_CLOSE )
		{
			*r = designRect( theTrainerClose[ 0 ], theTrainerClose[ 1 ], theTrainerClose[ 2 ], theTrainerClose[ 3 ] );
			return TRUE;
		}
		Int row, slot;
		if( !trainerCheatButton( button - TRAINER_FIRST_CHEAT_BUTTON, &row, &slot ) )
			return FALSE;
		const IRegion2D tray = rowRect( row );
		const Int right = TRAINER_BUTTONS_RIGHT - ( rowButtonCount( row ) - 1 - slot ) * ( TRAINER_BUTTON_W + TRAINER_BUTTON_GAP );
		*r = designRect( right - TRAINER_BUTTON_W, tray.lo.y + TRAINER_BUTTON_INSET, right, tray.hi.y - TRAINER_BUTTON_INSET );
		return TRUE;
	}

	/// what the button sends: a console line for a cheat's, a name for the log otherwise
	std::string trainerButtonAction( Int button )
	{
		if( button < TRAINER_TAB_COUNT )
			return TRAINER_TAB_ACTIONS[ button ];
		if( button == TRAINER_BUTTON_CLOSE )
			return "close";
		Int row, slot;
		if( !trainerCheatButton( button - TRAINER_FIRST_CHEAT_BUTTON, &row, &slot ) )
			return std::string();
		const TrainerRow &r = TRAINER_ROWS[ row ];
		const Int amount = r.amounts[ slot ];
		char text[ 64 ];
		if( amount == 0 )
			return r.cheat;
		if( amount == CHEAT_RANK_MAX )
			sprintf( text, "%s max", r.cheat );
		else
			sprintf( text, "%s %d", r.cheat, amount );
		return text;
	}

	/// "+1,000,000"
	UnicodeString plusAmount( Int amount )
	{
		char digits[ 16 ];
		sprintf( digits, "%d", amount );
		const Int length = (Int)strlen( digits );
		UnicodeString text( u"+" );
		for( Int i = 0; i < length; ++i )
		{
			if( i > 0 && ( length - i ) % 3 == 0 )
				text.concat( u',' );
			text.concat( (WideChar)digits[ i ] );
		}
		return text;
	}

	UnicodeString trainerButtonText( Int button )
	{
		if( button < TRAINER_TAB_COUNT )
			return TheGameText->fetch( TRAINER_TAB_LABELS[ button ] );
		if( button == TRAINER_BUTTON_CLOSE )
			return TheGameText->fetch( "GUI:CheatPanelClose" );
		Int row, slot;
		if( !trainerCheatButton( button - TRAINER_FIRST_CHEAT_BUTTON, &row, &slot ) )
			return UnicodeString::TheEmptyString;
		const Int amount = TRAINER_ROWS[ row ].amounts[ slot ];
		if( isToggleRow( row ) )
			return TheGameText->fetch( isRowOn( row ) ? "GUI:CheatOn" : "GUI:CheatOff" );
		if( amount == CHEAT_RANK_MAX )
			return TheGameText->fetch( "GUI:CheatRankMax" );
		if( rowButtonCount( row ) == 1 )
			return TheGameText->fetch( "GUI:CheatApply" );
		return plusAmount( amount );
	}

	/// a ring 'thick' pixels wide inside the rect, as drawCommandTab rings the open command tab
	void drawTrainerRing( const IRegion2D &r, Int thick, Color color )
	{
		const Int w = r.hi.x - r.lo.x, h = r.hi.y - r.lo.y;
		if( w <= 2 * thick || h <= 2 * thick )
			return;
		TheDisplay->drawFillRect( r.lo.x, r.lo.y, w, thick, color );
		TheDisplay->drawFillRect( r.lo.x, r.hi.y - thick, w, thick, color );
		TheDisplay->drawFillRect( r.lo.x, r.lo.y + thick, thick, h - 2 * thick, color );
		TheDisplay->drawFillRect( r.hi.x - thick, r.lo.y + thick, thick, h - 2 * thick, color );
	}

	/// the scheme's Tab screen piece, <piece><state>; NULL under a scheme without that screen
	const Image *tabPiece( const char *piece, const char *state )
	{
		const ControlBarSchemeManager *schemes = TheControlBar ? TheControlBar->getControlBarSchemeManager() : NULL;
		if( schemes == NULL )
			return NULL;
		AsciiString name;
		name.format( "%s%s", piece, state );
		return schemes->getTabImage( name.str() );
	}

	/// Trainer.wnd:Parent's draw: the solo painting, or plain plates; the title over the painting's
	/// rule, and the open tab's rows, each a tray with its name, lit green while its toggle is on
	void drawTrainerWindow( GameWindow *window, WinInstanceData *instData )
	{
		ICoord2D origin, size;
		window->winGetScreenPosition( &origin.x, &origin.y );
		window->winGetSize( &size.x, &size.y );
		const Real s = size.x / (Real)TRAINER_DESIGN_W;
		struct
		{
			ICoord2D o;
			Real s;
			IRegion2D operator()( const IRegion2D &d ) const
			{
				return designRect( o.x + REAL_TO_INT_FLOOR( d.lo.x * s ), o.y + REAL_TO_INT_FLOOR( d.lo.y * s ),
													 o.x + REAL_TO_INT_FLOOR( d.hi.x * s ), o.y + REAL_TO_INT_FLOOR( d.hi.y * s ) );
			}
		} map = { origin, s };

		const Image *painting = tabPiece( "Solo", "" );
		if( painting )
			TheDisplay->drawImage( painting, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
		else
		{
			const IRegion2D well = map( designRect( 12, 68, 988, 445 ) );
			TheDisplay->drawFillRect( origin.x, origin.y, size.x, size.y, GameMakeColor( 34, 36, 40, 235 ) );
			TheDisplay->drawFillRect( well.lo.x, well.lo.y, well.hi.x - well.lo.x, well.hi.y - well.lo.y, GameMakeColor( 18, 19, 22, 235 ) );
			TheDisplay->drawOpenRect( origin.x, origin.y, size.x, size.y, 2.0f, GameMakeColor( 110, 114, 120, 255 ) );
		}

		drawFittedLabel( theTrainerTitleLabel, TheGameText->fetch( "GUI:CheatPanelTitle" ), REAL_TO_INT( 16 * s + 0.5f ),
										 map( designRect( theTrainerTitle[ 0 ], theTrainerTitle[ 1 ], theTrainerTitle[ 2 ], theTrainerTitle[ 3 ] ) ),
										 TRAINER_DIM, FALSE, FITTED_ALIGN_LEFT );

		for( Int row = 0; row < TRAINER_ROW_COUNT; ++row )
		{
			if( TRAINER_ROWS[ row ].tab != theTrainerTab )
				continue;
			const Bool on = isRowOn( row );
			const IRegion2D tray = map( rowRect( row ) );
			TheDisplay->drawFillRect( tray.lo.x, tray.lo.y, tray.hi.x - tray.lo.x, tray.hi.y - tray.lo.y, on ? TRAINER_TRAY_ON : TRAINER_TRAY );
			if( on )
				TheDisplay->drawFillRect( tray.lo.x, tray.lo.y, REAL_TO_INT( TRAINER_LAMP_W * s + 0.5f ), tray.hi.y - tray.lo.y, TRAINER_GREEN );
			const IRegion2D well = map( designRect( theTrainerLabelX[ 0 ], rowRect( row ).lo.y, theTrainerLabelX[ 1 ], rowRect( row ).hi.y ) );
			drawFittedLabel( theTrainerRowLabels[ row ], TheGameText->fetch( TRAINER_ROWS[ row ].label ), REAL_TO_INT( 18 * s + 0.5f ),
											 well, on ? TRAINER_GREEN : TRAINER_WHITE, FALSE, FITTED_ALIGN_LEFT );
		}
	}

	/// a button's draw: its piece in the scheme's off, hover (H) or pushed (P) state, a toggle that is on
	/// pushed and ringed green, the open tab pushed and ringed in the side's colour, the text gold under
	/// the pointer
	void drawTrainerButton( GameWindow *window, WinInstanceData *instData )
	{
		const Int button = (Int)(intptr_t)GadgetButtonGetData( window ) - 1;
		if( button < 0 || button >= TRAINER_MAX_BUTTONS )
			return;
		ICoord2D pos, size;
		window->winGetScreenPosition( &pos.x, &pos.y );
		window->winGetSize( &size.x, &size.y );
		const IRegion2D r = designRect( pos.x, pos.y, pos.x + size.x, pos.y + size.y );
		const Real s = TheDisplay->getHeight() / 1080.0f;

		const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
		const Bool pressed = BitTest( instData->getState(), WIN_STATE_SELECTED );
		Int row = -1, slot = 0;
		if( button >= TRAINER_FIRST_CHEAT_BUTTON )
			trainerCheatButton( button - TRAINER_FIRST_CHEAT_BUTTON, &row, &slot );
		const Bool openTab = button == theTrainerTab;
		const Bool on = row >= 0 && isRowOn( row );

		const Image *image = tabPiece( row >= 0 ? "Send" : "Exit", pressed || on || openTab ? "P" : ( hover ? "H" : "" ) );
		if( image )
			TheDisplay->drawImage( image, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
		else
		{
			TheDisplay->drawFillRect( r.lo.x, r.lo.y, size.x, size.y, pressed || on || openTab ? GameMakeColor( 58, 60, 66, 255 ) : GameMakeColor( 44, 46, 50, 255 ) );
			TheDisplay->drawOpenRect( r.lo.x, r.lo.y, size.x, size.y, 1.0f, hover ? TRAINER_GOLD : GameMakeColor( 110, 114, 120, 255 ) );
		}

		const Int ring = MAX( 1, REAL_TO_INT( 2 * s + 0.5f ) );
		if( on )
			drawTrainerRing( r, ring, TRAINER_GREEN );
		else if( openTab && TheControlBar )
			drawTrainerRing( r, ring, TheControlBar->getBuildBorderColor() );

		const Color text = on ? TRAINER_GREEN : ( hover ? TRAINER_GOLD : ( openTab ? TRAINER_WHITE : TRAINER_DIM ) );
		IRegion2D well = r;
		well.lo.x += 3 * ring;
		well.hi.x -= 3 * ring;
		drawFittedLabel( theTrainerButtonLabels[ button ], trainerButtonText( button ),
										 REAL_TO_INT( ( button < TRAINER_FIRST_CHEAT_BUTTON ? 17 : 15 ) * s + 0.5f ), well, text, FALSE );
	}

	/// the open tab's buttons up, every other tab's down
	void showTrainerTab( void )
	{
		for( Int button = TRAINER_FIRST_CHEAT_BUTTON; button < TRAINER_MAX_BUTTONS; ++button )
		{
			Int row, slot;
			if( theTrainerButtons[ button ] && trainerCheatButton( button - TRAINER_FIRST_CHEAT_BUTTON, &row, &slot ) )
				theTrainerButtons[ button ]->winHide( TRAINER_ROWS[ row ].tab != theTrainerTab );
		}
	}

	WindowMsgHandledType cheatWindowSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
	{
		if( msg != GBM_SELECTED )
			return MSG_IGNORED;
		TheGameConsole->runCheatWindowButton( (Int)(intptr_t)GadgetButtonGetData( (GameWindow *)mData1 ) - 1 );
		return MSG_HANDLED;
	}
}

//-------------------------------------------------------------------------------------------------
void GameConsole::runCheatWindowButton( Int button )
{
	if( button >= 0 && button < TRAINER_TAB_COUNT )
	{
		theTrainerTab = button;
		showTrainerTab();
	}
	else if( button == TRAINER_BUTTON_CLOSE )
		closeCheatPanel();
	else if( button >= TRAINER_FIRST_CHEAT_BUTTON && button < TRAINER_MAX_BUTTONS )
		runCheatPanelAction( trainerButtonAction( button ) );
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
	for( Int button = 0; button < TRAINER_MAX_BUTTONS; ++button )
		theTrainerButtons[ button ] = NULL;		// gone with their parent
}

//-------------------------------------------------------------------------------------------------
/** The trainer: Window/Trainer.wnd's draggable window grown to the solo painting's shape at the
	* screen's height over 1080, across the middle, and a push button for every tab and cheat laid on
	* it.  Everything after that is drawn live from the local player, so nothing is refreshed here.
	* The windows are thrown away when it shuts, never from inside their own callback. */
//-------------------------------------------------------------------------------------------------
void GameConsole::updateCheatWindow( void )
{
	if( !m_cheatPanelOpen )
	{
		resetCheatWindow();
		return;
	}
	if( m_cheatLayout != NULL )
		return;

	m_cheatLayout = TheWindowManager->winCreateLayout( AsciiString( "Trainer.wnd" ) );
	GameWindow *parent = m_cheatLayout ? m_cheatLayout->getFirstWindow() : NULL;
	if( parent == NULL )
	{
		printLine( AsciiString( "trainer: Window/Trainer.wnd is missing" ) );
		resetCheatWindow();
		m_cheatPanelOpen = FALSE;
		return;
	}

	const Real s = TheDisplay->getHeight() / 1080.0f;
	const Int width = REAL_TO_INT( TRAINER_DESIGN_W * s ), height = REAL_TO_INT( TRAINER_DESIGN_H * s );
	Int x, y;
	parent->winGetPosition( &x, &y );
	parent->winSetSize( width, height );
	parent->winSetPosition( MAX( 0, ( TheDisplay->getWidth() - width ) / 2 ), y );
	parent->winSetDrawFunc( drawTrainerWindow );
	parent->winSetSystemFunc( cheatWindowSystem );

	for( Int button = 0; button < TRAINER_MAX_BUTTONS; ++button )
	{
		theTrainerButtons[ button ] = NULL;
		IRegion2D r;
		if( !trainerButtonRect( button, &r ) )
			continue;
		WinInstanceData inst;
		inst.init();
		BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
		const Int left = REAL_TO_INT_FLOOR( r.lo.x * s ), top = REAL_TO_INT_FLOOR( r.lo.y * s );
		GameWindow *window = TheWindowManager->gogoGadgetPushButton( parent, WIN_STATUS_ENABLED, left, top,
			REAL_TO_INT_FLOOR( r.hi.x * s ) - left, REAL_TO_INT_FLOOR( r.hi.y * s ) - top, &inst, NULL, TRUE );
		if( window == NULL )
			continue;
		GadgetButtonSetData( window, (void *)(intptr_t)( button + 1 ) );
		window->winSetDrawFunc( drawTrainerButton );
		theTrainerButtons[ button ] = window;
	}
	showTrainerTab();
	m_cheatLayout->hide( FALSE );

	// where each button landed, so a script driving the game over -control knows where to click; a
	// cheat's is the line it sends, and it answers only while its tab is open
	for( Int button = 0; button < TRAINER_MAX_BUTTONS; ++button )
	{
		if( theTrainerButtons[ button ] == NULL )
			continue;
		Int bx, by, bw, bh;
		theTrainerButtons[ button ]->winGetScreenPosition( &bx, &by );
		theTrainerButtons[ button ]->winGetSize( &bw, &bh );
		DEBUG_LOG(( "Cheat window: \"%s\" at %d %d\n", trainerButtonAction( button ).c_str(), bx + bw / 2, by + bh / 2 ));
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
