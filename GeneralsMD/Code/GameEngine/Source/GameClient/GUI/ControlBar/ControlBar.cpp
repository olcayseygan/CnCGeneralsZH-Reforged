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

// FILE: ControlBar.cpp ///////////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive command interface
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicThread.h"
#include "Lib/Clock.h"

#include <map>
#define DEFINE_GUI_COMMMAND_NAMES
#define DEFINE_COMMAND_OPTION_NAMES
#define DEFINE_WEAPONSLOTTYPE_NAMES
#define DEFINE_RADIUSCURSOR_NAMES

#include "Common/ActionManager.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GameType.h"
#include "Common/MultiplayerSettings.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Override.h"
#include "Common/PlayerTemplate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "Common/Recorder.h"
#include "Common/Radar.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/OCLUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/ScriptEngine.h"

#include "GameClient/AnimateWindowManager.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Drawable.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/HotKey.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/GameInfo.h"

#include <algorithm>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////
ControlBar *TheControlBar = NULL;

const Image* ControlBar::m_rankVeteranIcon	= NULL;
const Image* ControlBar::m_rankEliteIcon		= NULL;
const Image* ControlBar::m_rankHeroicIcon		= NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////
// CommandButton //////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const FieldParse CommandButton::s_commandButtonFieldParseTable[] = 
{

	{ "Command",							CommandButton::parseCommand, NULL, offsetof( CommandButton, m_command ) },
	{ "Options",							INI::parseBitString32,		   TheCommandOptionNames, offsetof( CommandButton, m_options ) },
	{ "Object",								INI::parseThingTemplate,		 NULL, offsetof( CommandButton, m_thingTemplate ) },
	{ "Upgrade",							INI::parseUpgradeTemplate,	 NULL, offsetof( CommandButton, m_upgradeTemplate ) },
	{ "WeaponSlot",						INI::parseLookupList,				 TheWeaponSlotTypeNamesLookupList, offsetof( CommandButton, m_weaponSlot ) },
	{ "MaxShotsToFire",				INI::parseInt,							 NULL, offsetof( CommandButton, m_maxShotsToFire ) },
	{ "Science",							INI::parseScienceVector,					 NULL, offsetof( CommandButton, m_science ) },
	{ "SpecialPower",					INI::parseSpecialPowerTemplate,			 NULL, offsetof( CommandButton, m_specialPower ) },
	{ "TextLabel",						INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_textLabel ) },
	{ "DescriptLabel",				INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_descriptionLabel ) },
	{ "PurchasedLabel",				INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_purchasedLabel ) },
	{ "ConflictingLabel",			INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_conflictingLabel ) },
	{ "ButtonImage",					INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_buttonImageName ) },
	{ "CursorName",						INI::parseAsciiString,			 NULL, offsetof( CommandButton, m_cursorName ) },
	{ "InvalidCursorName",		INI::parseAsciiString,       NULL, offsetof( CommandButton, m_invalidCursorName ) },
	{ "ButtonBorderType",			INI::parseLookupList,				 CommandButtonMappedBorderTypeNames, offsetof( CommandButton, m_commandButtonBorder ) },
	{ "RadiusCursorType",			INI::parseIndexList,				 TheRadiusCursorNames, offsetof( CommandButton, m_radiusCursor ) },
	{ "UnitSpecificSound",		INI::parseAudioEventRTS,		 NULL, offsetof( CommandButton, m_unitSpecificSound ) }, 

	{ NULL,						NULL,												 NULL, 0 }  // keep this last

};
static void commandButtonTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	TheControlBar->showBuildTooltipLayout(window);
}

//-------------------------------------------------------------------------------------------------
/** The place a command of `type` always stands at, or -1 for one that takes the next free place. */
//-------------------------------------------------------------------------------------------------
static Int fixedCommandPlace( Int type )
{
	switch( type )
	{
		case GUI_COMMAND_STOP:									return COMMAND_PLACE_STOP;
		case GUI_COMMAND_ATTACK_MOVE:						return COMMAND_PLACE_ATTACK_MOVE;
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:	return COMMAND_PLACE_GUARD;
		case GUI_COMMAND_EVACUATE:							return COMMAND_PLACE_EJECT;
		case GUI_COMMAND_SET_RALLY_POINT:				return COMMAND_PLACE_RALLY;
		case GUI_COMMAND_SELL:									return COMMAND_PLACE_SELL;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/** What the key of `place` did before the grid took it, MSG_INVALID for a key that pressed a cell
	already.  The grid keeps it for the place's own order and for a place with nothing in it, so S
	still stops a selection whose bar has no stop button, and still calls off a building going up. */
//-------------------------------------------------------------------------------------------------
static GameMessage::Type orderAtPlace( Int place )
{
	switch( place )
	{
		case COMMAND_PLACE_ATTACK:			return GameMessage::MSG_META_TOGGLE_FORCEATTACK;
		case COMMAND_PLACE_STOP:				return GameMessage::MSG_META_STOP;
		case COMMAND_PLACE_ATTACK_MOVE:	return GameMessage::MSG_META_TOGGLE_ATTACKMOVE;
		case COMMAND_PLACE_GUARD:				return GameMessage::MSG_META_TOGGLE_GUARD;
		case COMMAND_PLACE_HOLD:				return GameMessage::MSG_META_HOLD_POSITION;
		case COMMAND_PLACE_MOVE:				return GameMessage::MSG_META_TOGGLE_MOVE;
	}
	return GameMessage::MSG_INVALID;
}

//-------------------------------------------------------------------------------------------------
static GameWindow *barWindow( const char *name )
{
	return TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( name ) );
}

/** Shown, and every parent of it shown.  winIsHidden stops at the window itself, and a context
	parent hides its buttons by hiding itself. */
static Bool barWindowShown( GameWindow *window )
{
	if( window == NULL || window->winIsHidden() )
		return FALSE;
	for( GameWindow *parent = window->winGetParent(); parent; parent = parent->winGetParent() )
		if( parent->winIsHidden() )
			return FALSE;
	return TRUE;
}

/** The button a hidden command group still offers at `place`: cancel while a building goes up,
	sell or the rally point on a building counting down to a unit. */
static GameWindow *contextButtonAtPlace( Int place )
{
	if( place == COMMAND_PLACE_STOP )
	{
		GameWindow *cancel = barWindow( "ControlBar.wnd:ButtonCancelConstruction" );
		if( barWindowShown( cancel ) )
			return cancel;
	}

	GameWindow *sell = barWindow( "ControlBar.wnd:OCLTimerSellButton" );
	if( !barWindowShown( sell ) )
		return NULL;
	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( sell );
	if( command && fixedCommandPlace( command->getCommandType() ) == place )
		return sell;
	return NULL;
}

//-------------------------------------------------------------------------------------------------
Int ControlBar_commandGroup( const CommandButton *command )
{
	switch( command->getCommandType() )
	{
		case GUI_COMMAND_UNIT_BUILD:			return COMMAND_GROUP_PRODUCTION;
		case GUI_COMMAND_EXIT_CONTAINER:	return COMMAND_GROUP_PASSENGER;
		case GUI_COMMAND_DOZER_CONSTRUCT:	break;
		default:													return COMMAND_GROUP_ABILITY;
	}

	const ThingTemplate *structure = command->getThingTemplate();
	if( structure == NULL )
		return COMMAND_GROUP_UTILITY;
	// a fake carries FS_FAKE and nothing else; three of the five are decoys of factories, so the fake
	// set stands on the row the real factories stand on
	if( structure->isKindOf( KINDOF_FS_FACTORY ) || structure->isKindOf( KINDOF_FS_BARRACKS ) ||
			structure->isKindOf( KINDOF_FS_WARFACTORY ) || structure->isKindOf( KINDOF_FS_AIRFIELD ) ||
			structure->isKindOf( KINDOF_COMMANDCENTER ) || structure->isKindOf( KINDOF_FS_FAKE ) )
		return COMMAND_GROUP_PRODUCTION;
	// the demo trap is no FS_ anything, only DEMOTRAP
	if( structure->isKindOf( KINDOF_FS_BASE_DEFENSE ) || structure->isKindOf( KINDOF_DEMOTRAP ) )
		return COMMAND_GROUP_DEFENSE;
	return COMMAND_GROUP_UTILITY;
}

//-------------------------------------------------------------------------------------------------
Int ControlBar_namedCommandPlace( const char *buttonName )
{
	static const struct { const char *name; Int place; } NAMED[] =
	{
		{ "Command_DisarmMinesAtPosition",				COMMAND_PLACE_CLEAR_MINES },
		{ "Command_UpgradeGLAWorkerFakeCommandSet",	COMMAND_PLACE_FAKE_STRUCTURES },
		{ "Command_UpgradeGLAWorkerRealCommandSet",	COMMAND_PLACE_FAKE_STRUCTURES },
		{ "Demo_Command_TertiarySuicide",					COMMAND_PLACE_EXPLOSIVE }
	};
	for( Int each = 0; each < (Int)ARRAY_SIZE( NAMED ); each++ )
		if( strcmp( NAMED[ each ].name, buttonName ) == 0 )
			return NAMED[ each ].place;
	return -1;
}

//-------------------------------------------------------------------------------------------------
static Int freeInRow( const Bool *taken, Int row )
{
	Int free = 0;
	for( Int column = 0; column < COMMAND_PLACE_COLUMNS; column++ )
		free += taken[ row * COMMAND_PLACE_COLUMNS + column ] ? 0 : 1;
	return free;
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar_commandPlaces( const Int *types, const Int *groups, const Int *pinned, Int count, Int *places )
{
	enum { ROWS = COMMAND_PLACE_COUNT / COMMAND_PLACE_COLUMNS };
	// passengers from the bottom right corner outward, a column of the A and Z rows at a time, then
	// along Q from the right.  The places they get are handed out in reading order
	static const Int CORNER[ COMMAND_PLACE_COUNT ] =
	{
		COMMAND_PLACE_H, COMMAND_PLACE_N, COMMAND_PLACE_G, COMMAND_PLACE_B, COMMAND_PLACE_F, COMMAND_PLACE_V,
		COMMAND_PLACE_D, COMMAND_PLACE_C, COMMAND_PLACE_S, COMMAND_PLACE_X, COMMAND_PLACE_A, COMMAND_PLACE_Z,
		COMMAND_PLACE_Y, COMMAND_PLACE_T, COMMAND_PLACE_R, COMMAND_PLACE_E, COMMAND_PLACE_W, COMMAND_PLACE_Q
	};

	Bool taken[ COMMAND_PLACE_COUNT ] = { FALSE };
	Bool fights = FALSE;
	for( Int slot = 0; slot < count; slot++ )
	{
		places[ slot ] = -1;
		fights = fights || types[ slot ] == GUI_COMMAND_ATTACK_MOVE;
	}
	taken[ COMMAND_PLACE_ATTACK ] = taken[ COMMAND_PLACE_HOLD ] = taken[ COMMAND_PLACE_MOVE ] = fights;

	for( Int slot = 0; slot < count; slot++ )
	{
		const Int place = pinned[ slot ] >= 0 ? pinned[ slot ] : fixedCommandPlace( types[ slot ] );
		if( place >= 0 && !taken[ place ] )
		{
			places[ slot ] = place;
			taken[ place ] = TRUE;
		}
	}

	Int passengers = 0;
	Bool builds = FALSE;
	for( Int slot = 0; slot < count; slot++ )
	{
		if( places[ slot ] >= 0 || types[ slot ] == GUI_COMMAND_NONE )
			continue;
		passengers += groups[ slot ] == COMMAND_GROUP_PASSENGER ? 1 : 0;
		builds = builds || groups[ slot ] == COMMAND_GROUP_PRODUCTION;
	}

	Bool corner[ COMMAND_PLACE_COUNT ] = { FALSE };
	for( Int each = 0; each < COMMAND_PLACE_COUNT && passengers > 0; each++ )
	{
		if( !taken[ CORNER[ each ] ] )
		{
			corner[ CORNER[ each ] ] = taken[ CORNER[ each ] ] = TRUE;
			passengers--;
		}
	}
	Int next = 0;
	for( Int slot = 0; slot < count; slot++ )
	{
		if( places[ slot ] >= 0 || types[ slot ] == GUI_COMMAND_NONE || groups[ slot ] != COMMAND_GROUP_PASSENGER )
			continue;
		while( next < COMMAND_PLACE_COUNT && !corner[ next ] )
			next++;
		if( next < COMMAND_PLACE_COUNT )
			places[ slot ] = next++;
	}

	// the rows, one group at a time; a group that outgrows its row goes on in the next row down with
	// room, and only then in the rows above, nearest first, so it still reads in slot order after its
	// own row: a command center's powers run on from A to Z rather than back up beside its dozer
	static const Int GROUPS[] = { COMMAND_GROUP_PRODUCTION, COMMAND_GROUP_DEFENSE, COMMAND_GROUP_UTILITY, COMMAND_GROUP_ABILITY };
	for( Int each = 0; each < (Int)ARRAY_SIZE( GROUPS ); each++ )
	{
		const Int group = GROUPS[ each ];
		const Int own = group == COMMAND_GROUP_ABILITY ? ( builds ? 1 : 0 ) : group;
		Int row = own;
		for( Int slot = 0; slot < count; slot++ )
		{
			if( places[ slot ] >= 0 || types[ slot ] == GUI_COMMAND_NONE || groups[ slot ] != group )
				continue;
			if( freeInRow( taken, row ) == 0 )
			{
				for( Int step = 1; step < 2 * ROWS; step++ )
				{
					const Int other = step < ROWS ? own + step : own - ( step - ROWS + 1 );
					if( other >= 0 && other < ROWS && freeInRow( taken, other ) > 0 )
					{
						row = other;
						break;
					}
				}
			}
			for( Int column = 0; column < COMMAND_PLACE_COLUMNS; column++ )
			{
				const Int place = row * COMMAND_PLACE_COLUMNS + column;
				if( !taken[ place ] )
				{
					places[ slot ] = place;
					taken[ place ] = TRUE;
					break;
				}
			}
		}
	}
	return fights;
}

//-------------------------------------------------------------------------------------------------
void ControlBar_orderKeyPlaces( const Int *places, Int count, Bool fights, Int *keys )
{
	for( Int key = 0; key < ORDER_KEY_EXTRAS; key++ )
		keys[ key ] = -1;
	if( !fights )
		return;

	Bool taken[ COMMAND_PLACE_COUNT ] = { FALSE };
	for( Int slot = 0; slot < count; slot++ )
		if( places[ slot ] >= 0 )
			taken[ places[ slot ] ] = TRUE;
	taken[ COMMAND_PLACE_ATTACK ] = taken[ COMMAND_PLACE_HOLD ] = taken[ COMMAND_PLACE_MOVE ] = TRUE;

	static const Int OWN[ ORDER_KEY_EXTRAS ] = { COMMAND_PLACE_H };
	for( Int key = 0; key < ORDER_KEY_EXTRAS; key++ )
		if( !taken[ OWN[ key ] ] )
			keys[ key ] = OWN[ key ];
	for( Int key = 0; key < ORDER_KEY_EXTRAS; key++ )
		if( keys[ key ] >= 0 )
			taken[ keys[ key ] ] = TRUE;

	for( Int key = 0; key < ORDER_KEY_EXTRAS; key++ )
	{
		if( keys[ key ] >= 0 )
			continue;
		static const Int SPARE[] = { COMMAND_PLACE_B, COMMAND_PLACE_N };
		for( Int each = 0; each < (Int)ARRAY_SIZE( SPARE ) && keys[ key ] < 0; each++ )
			if( !taken[ SPARE[ each ] ] )
				keys[ key ] = SPARE[ each ];
		for( Int place = 0; place < COMMAND_PLACE_COUNT && keys[ key ] < 0; place++ )
			if( !taken[ place ] )
				keys[ key ] = place;
		if( keys[ key ] >= 0 )
			taken[ keys[ key ] ] = TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
void ControlBar::getOrderKeyPlaces( Int *keys ) const
{
	Int places[ MAX_COMMANDS_PER_SET ];
	const Bool fights = getCommandPlaces( places );
	ControlBar_orderKeyPlaces( places, MAX_COMMANDS_PER_SET, fights, keys );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::getCommandPlaces( Int *places ) const
{
	// a context without the command group hides the group's parent.  Its buttons stay as the last
	// selection set them and are not on the bar.
	const Bool groupShown = !m_contextParent[ CP_COMMAND ]->winIsHidden();
	Int types[ MAX_COMMANDS_PER_SET ];
	Int groups[ MAX_COMMANDS_PER_SET ];
	Int pinned[ MAX_COMMANDS_PER_SET ];
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		GameWindow *window = m_commandWindows[ slot ];
		const CommandButton *command = ( groupShown && window && !BitTest( window->winGetStatus(), WIN_STATUS_HIDDEN ) )
																	 ? (const CommandButton *)GadgetButtonGetData( window ) : NULL;
		types[ slot ] = command ? command->getCommandType() : GUI_COMMAND_NONE;
		groups[ slot ] = command ? ControlBar_commandGroup( command ) : COMMAND_GROUP_ABILITY;
		pinned[ slot ] = command ? ControlBar_namedCommandPlace( command->getName().str() ) : -1;
	}
	return ControlBar_commandPlaces( types, groups, pinned, MAX_COMMANDS_PER_SET, places );
}

//-------------------------------------------------------------------------------------------------
Int ControlBar::resolveCommandSlot( Int place ) const
{
	Int places[ MAX_COMMANDS_PER_SET ];
	getCommandPlaces( places );
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
		if( places[ slot ] == place )
			return slot;
	return SLOT_NOTHING;

}  // end resolveCommandSlot

//-------------------------------------------------------------------------------------------------
ControlBar::PressOutcome ControlBar::peekCommandButtonPress( Int place, GameWindow **button )
{
	*button = NULL;

	const Int slot = resolveCommandSlot( place );
	if( slot == SLOT_NOTHING )
		return PRESS_DOES_NOTHING;

	GameWindow *win = m_commandWindows[ slot ];
	*button = win;
	return BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) ? PRESS_FIRES : PRESS_IS_REFUSED;

}  // end peekCommandButtonPress

//-------------------------------------------------------------------------------------------------
/** Press the command at a grid place.  Mirrors HotKeyManager::executeHotKey: an empty place does
	nothing at all, an enabled button gets the same GBM_SELECTED the mouse would send, and a
	disabled one just makes the rejection noise.  The places of the shared orders go through the
	orders' own messages instead, see orderAtPlace. */
//-------------------------------------------------------------------------------------------------
void ControlBar::pressCommandButton( Int place )
{
	if( place < 0 || place >= COMMAND_PLACE_COUNT )
		return;

	// the page's stance key stands where no button does
	Int keys[ ORDER_KEY_EXTRAS ];
	getOrderKeyPlaces( keys );
	for( Int key = 0; key < ORDER_KEY_EXTRAS; key++ )
	{
		if( keys[ key ] == place )
		{
			TheInGameUI->pressOrderKey( key );
			return;
		}
	}

	const Int slot = resolveCommandSlot( place );
	GameWindow *win = ( slot == SLOT_NOTHING ) ? NULL : m_commandWindows[ slot ];
	if( win == NULL )
		win = contextButtonAtPlace( place );
	const CommandButton *command = win ? (const CommandButton *)GadgetButtonGetData( win ) : NULL;
	const GameMessage::Type order = orderAtPlace( place );
	if( order != GameMessage::MSG_INVALID && ( command == NULL || fixedCommandPlace( command->getCommandType() ) == place ) )
	{
		TheMessageStream->appendMessage( order );
		return;
	}

	if( win == NULL )
		return;

	if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
	{
		TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED,
																						(WindowMsgData)win, win->winGetWindowId() );

		AudioEventRTS buttonClick( "GUIClick" );
		if( TheAudio )
			TheAudio->addAudioEvent( &buttonClick );
	}
	else
	{
		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
			TheAudio->addAudioEvent( &disabledClick );
	}

}  // end pressCommandButton

/* LOGIC-THREAD.md phase 2, M2: what the tick asks of the bar waits here for LogicThread::runTick to
	 call flushLogicCalls on the main thread.  Keyed on isInGameLogicUpdate rather than on the thread, so
	 -logicthread 0 and 1 run one path, as the drawables' deferred work does. */
static Bool s_logicMarkedUIDirty = FALSE;
static std::vector< std::function<void()> > s_logicCalls;

//-------------------------------------------------------------------------------------------------
void ControlBar::callFromLogic( const std::function<void()>& call )
{
	if( TheGameLogic->isInGameLogicUpdate() )
		s_logicCalls.push_back( call );
	else
		call();
}

//-------------------------------------------------------------------------------------------------
void ControlBar::flushLogicCalls( void )
{
	// a call may queue nothing more: none of them runs inside a tick
	for( size_t i = 0; i < s_logicCalls.size(); ++i )
		s_logicCalls[ i ]();
	s_logicCalls.clear();

	if( s_logicMarkedUIDirty )
	{
		s_logicMarkedUIDirty = FALSE;
		TheControlBar->markUIDirty();
	}
}

/// mark the UI as dirty so the context of everything is re-evaluated
void ControlBar::markUIDirty( void )
{
	if( TheGameLogic->isInGameLogicUpdate() )
	{
		s_logicMarkedUIDirty = TRUE;
		return;
	}

	LOGIC_CENSUS( CENSUS_CONTROLBAR );
  m_UIDirty = TRUE;

#if defined( _INTERNAL ) || defined( _DEBUG )
	UnsignedInt now = TheGameLogic->getFrame();
	if( now == m_lastFrameMarkedDirty )
	{
		//Do nothing.
	}
	else if( now == m_lastFrameMarkedDirty + 1 )
	{
		m_consecutiveDirtyFrames++;
	}
	else
	{
		m_consecutiveDirtyFrames = 1;
	}
	m_lastFrameMarkedDirty = now;

	if( m_consecutiveDirtyFrames > 20 )
	{
		DEBUG_CRASH( ("Serious flaw in interface system! Either new code or INI has caused the interface to be marked dirty every frame. This problem actually causes the interface to completely lockup not allowing you to click normal game buttons.") );
	}

#endif
}


//-------------------------------------------------------------------------------------------------
/** Whose promotion screen is being shown.  Playing, it is your own and nothing else.
	*
	* Watching - an observer, or a player knocked out who stayed to watch - the screen used to be
	* populated with the watcher's own player, who owns no command sets at all, so the whole screen
	* came up blank.  It follows the field instead: whatever is selected names its owner, and with
	* nothing selected it is the player being watched, or the first side still in the match. */
//-------------------------------------------------------------------------------------------------
/** The player a watcher has picked out by clicking one of his things, NULL when nothing is
	* selected or the selection belongs to nobody who is still playing.  It is what narrows the
	* whole screen to one player: the side the bar wears and whose promotion screen the key opens. */
Player *ControlBar::getSelectedPlayer( void )
{
	if( ThePlayerList->getLocalPlayer()->isPlayerActive() )
		return NULL;

	const DrawableList *selection = TheInGameUI->getAllSelectedDrawables();
	if( selection == NULL || selection->empty() )
		return NULL;

	Object *selected = selection->front()->getObject();
	Player *owner = selected ? selected->getControllingPlayer() : NULL;
	if( owner == NULL || !owner->isPlayableSide() || !owner->isPlayerActive() )
		return NULL;

	return owner;
}

//-------------------------------------------------------------------------------------------------
/** Watching, the selection drives the bar's contents: clicking a unit watches its owner and his
	* panel comes up, clicking empty ground puts everybody back.  The bar itself stays ControlBar Pro's
	* observer bar throughout, as the owner asked; it no longer changes into the watched side's. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateWatchedPlayer( void )
{
	Player *selected = getSelectedPlayer();
	if( selected == m_watchedSelection )
		return;

	watchPlayer( selected );

	// the general's stars open the promotion screen of the player who is selected, and there is
	// nobody's to open with nothing selected
	static NameKeyType buttonGeneralID = NAMEKEY( "ControlBar.wnd:ButtonGeneral" );
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralID );
	if( buttonGeneral )
		buttonGeneral->winEnable( selected != NULL );
}

void ControlBar::watchPlayer( Player *player )
{
	setObserverLookAtPlayer( player );

	if( player )
		showObserverPlayerInfo();
	else
		showObserverPlayerList();

	// a seat clicked at the top clears the selection before it gets here, so what is selected is
	// read again rather than taken from the caller
	m_watchedSelection = getSelectedPlayer();
}

Player *ControlBar::getWatchedPlayer( void )
{
	Player *local = ThePlayerList->getLocalPlayer();
	if( local->isPlayerActive() )
		return local;

	Player *selected = getSelectedPlayer();
	if( selected )
		return selected;

	if( m_observerLookAtPlayer )
		return m_observerLookAtPlayer;

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); i++ )
	{
		Player *candidate = ThePlayerList->getNthPlayer( i );
		if( candidate != local && candidate->isPlayerActive() && candidate->isPlayableSide() )
			return candidate;
	}

	return local;
}

/** Puts one rank's command set on its windows, slot i on window i.  Every promotion screen numbers its
	* windows down the columns the same way, so a set's empty slots and its columns are the ones its
	* author laid out.  The caller has hidden every window. */
void ControlBar::populatePurchaseScienceRank( Player *player, const CommandSet *commandSet, GameWindow **windows, Int windowCount )
{
	for( Int i = 0; i < windowCount; i++ )
	{
		const CommandButton *commandButton = commandSet->getCommandButton( i );
		GameWindow *window = windows[ i ];

		if( commandButton == NULL || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
			continue;

		window->winHide( FALSE );
		window->winEnable( FALSE );
		setControlCommand( window, commandButton );
		if( commandButton->getScienceVec().empty() )
			continue;

		ScienceType science = commandButton->getScienceVec()[ 0 ];
		if( player->isScienceDisabled( science ) )
			continue;
		if( player->isScienceHidden( science ) )
		{
			window->winHide( TRUE );
			continue;
		}
		if( !player->hasScience( science ) && TheScienceStore->playerHasPrereqsForScience( player, science ) && TheScienceStore->getSciencePurchaseCost( science ) <= player->getSciencePurchasePoints() )
			window->winEnable( TRUE );
		if( player->hasScience( science ) )
			window->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
		else
			window->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
		if( !TheScienceStore->playerHasRootPrereqsForScience( player, science ) )
			window->winHide( TRUE );
	}
}

void ControlBar::populatePurchaseScience( Player* player )
{
//	TheInGameUI->deselectAllDrawables();

	const CommandSet *commandSet1;
	const CommandSet *commandSet3;
	const CommandSet *commandSet8;
	Int i;
	if(TheScriptEngine->isGameEnding())
		return;
	// get command set
	if(!player ||!player->getPlayerTemplate() || player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1().isEmpty() || 
			player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3().isEmpty() ||
			player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8().isEmpty())
		return;
	commandSet1 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	commandSet3 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	commandSet8 = TheControlBar->findCommandSet(player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		m_sciencePurchaseWindowsRank1[i]->winHide(TRUE);
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		m_sciencePurchaseWindowsRank3[i]->winHide(TRUE);
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
		m_sciencePurchaseWindowsRank8[i]->winHide(TRUE);


	// if no command set match is found hide all the buttons
	if( commandSet1 == NULL ||
			commandSet3 == NULL ||
			commandSet8 == NULL )
		return;

	populatePurchaseScienceRank( player, commandSet1, m_sciencePurchaseWindowsRank1, MAX_PURCHASE_SCIENCE_RANK_1 );
	populatePurchaseScienceRank( player, commandSet3, m_sciencePurchaseWindowsRank3, MAX_PURCHASE_SCIENCE_RANK_3 );
	populatePurchaseScienceRank( player, commandSet8, m_sciencePurchaseWindowsRank8, MAX_PURCHASE_SCIENCE_RANK_8 );

	//
	// Somebody else's screen is a read-out, not a shop: a watcher sees what that general has bought
	// and what is still open to him, and cannot press any of it.
	//
	if( player != ThePlayerList->getLocalPlayer() )
	{
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
			m_sciencePurchaseWindowsRank1[ i ]->winEnable( FALSE );
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
			m_sciencePurchaseWindowsRank3[ i ]->winEnable( FALSE );
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
			m_sciencePurchaseWindowsRank8[ i ]->winEnable( FALSE );
	}

	GameWindow *win = NULL;
	UnicodeString tempUS;
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextRankPointsAvailable" ) );
	if(win)
	{
		tempUS.format(u"%d", player->getSciencePurchasePoints());
		GadgetStaticTextSetText(win, tempUS);
	}
	
// redundant to StaticTextTitle in the Zero Hour context
/*
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextLevel" ) );
	if(win)
	{
		tempUS.format(TheGameText->fetch("SCIENCE:Rank"), player->getRankLevel());
		GadgetStaticTextSetText(win, tempUS);
	}
*/
	
	// hash the name once, not on every render frame this panel is open
	static const NameKeyType key_progressBarExperience = TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:ProgressBarExperience" );
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], key_progressBarExperience );
	if(win)
	{
		GadgetProgressBarSetProgress(win, player->getRankProgressPercent());
	}

	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:StaticTextTitle" ) );
	if(win)
	{
		AsciiString tempAs;

		tempAs.format("SCIENCE:Rank%d", player->getRankLevel());
		GadgetStaticTextSetText(win, TheGameText->fetch(tempAs));
	}
	
	
	//
	// to avoid a one frame delay where windows may become enabled/disabled, run the update
	// at once to get it all in the correct state immediately
	//
	updateContextPurchaseScience();
/*
	// get the side select buttons
	GameWindow* win = m_contextParent[ CP_PURCHASE_SCIENCE ];

	Color color = GameMakeColor(255, 255, 255, 255);

	/// @todo srj -- evil hack testing code. do not imitate.
	ScienceVec purchasable, potential;
	TheScienceStore->getPurchasableSciences(player, purchasable, potential);
	GadgetListBoxReset(win);
	for (ScienceVec::const_iterator it = purchasable.begin(); it != purchasable.end(); ++it)
	{
		ScienceType st = *it;
		UnicodeString u;
		u.translate(TheScienceStore->getInternalNameForScience(st));
		GadgetListBoxAddEntryText(win, u, color, -1, -1);
	}
	for (ScienceVec::const_iterator it2 = potential.begin(); it2 != potential.end(); ++it2)
	{
		ScienceType st = *it2;
		AsciiString foo = "(Not Yet)";
		foo.concat(TheScienceStore->getInternalNameForScience(st));
		UnicodeString u;
		u.translate(foo);
		GadgetListBoxAddEntryText(win, u, color, -1, -1);
	}
	GadgetListBoxAddEntryText(win, UnicodeString(L"Cancel"), color, -1, -1);*/

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextPurchaseScience( void )
{
	GameWindow *win =NULL;
	Player *player = getWatchedPlayer();
	// hash the name once, not on every render frame this panel is open
	static const NameKeyType key_progressBarExperience = TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:ProgressBarExperience" );
	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], key_progressBarExperience );
	if(win)
	{
		GadgetProgressBarSetProgress(win, player->getRankProgressPercent());
	}

//	win = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], TheNameKeyGenerator->nameToKey( "ControlBar.wnd:TextEntryGeneralName" ) );
//	if(win)
//	{
//		UnicodeString temp = GadgetTextEntryGetText(win);
//		if(temp.compare(player->getGeneralName()) != 0)
//			player->setGeneralName(temp);
//	}
/*
	/// @todo srj -- evil hack testing code. do not imitate.
	Object *obj = m_currentSelectedDrawable->getObject();

	if( obj == NULL )
		return;

	// sanity
	if( obj->isKindOf( KINDOF_COMMANDCENTER ) == FALSE )
		switchToContext( CB_CONTEXT_NONE, NULL );
	
	GameWindow* win = m_contextParent[ CP_PURCHASE_SCIENCE ];

	Int selected;
	GadgetListBoxGetSelected( win, &selected );
	if( selected != -1 )
	{
		UnicodeString usci = GadgetListBoxGetText( win, selected, 0 );
		AsciiString sci;
		sci.translate(usci);
		ScienceType st = usci.getCharAt(0) == '(' ? SCIENCE_INVALID : TheScienceStore->getScienceFromInternalName(sci);

		if (st != SCIENCE_INVALID)
		{
			GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_PURCHASE_SCIENCE );
			msg->appendIntegerArgument( st );
		}

		switchToContext( CB_CONTEXT_NONE, NULL );
	}
*/

}

//-------------------------------------------------------------------------------------------------
/** parse command definition */
//-------------------------------------------------------------------------------------------------
void CommandButton::parseCommand( INI* ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();
	Int i;

	for( i = 0; TheGuiCommandNames[ i ]; i++ )
	{

		if( strcasecmp( TheGuiCommandNames[ i ], token ) == 0 )
		{

			GUICommandType *command = (GUICommandType *)store;
			*command = (GUICommandType)i;
			return;

		}  // end if

	}  // end for i

	// if we're here the command was not found
	throw INI_INVALID_DATA;

}  // end parseCommand

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton::CommandButton( void )
{

	m_command = GUI_COMMAND_NONE;
	m_thingTemplate = NULL;
	m_upgradeTemplate = NULL;
	m_weaponSlot = PRIMARY_WEAPON;
	m_maxShotsToFire = 0x7fffffff;	// huge number
	m_science.clear();
	m_specialPower = NULL;
	m_buttonImage = NULL;

	//Code renderer handles these states now.
	//m_disabledImage = NULL;
	//m_hiliteImage = NULL;
	//m_pushedImage = NULL;

	m_flashCount = 0;

	// Added by Sadullah Nader
	// The purpose is to initialize these variable to values that are zero or empty

	m_conflictingLabel.clear();
	m_cursorName.clear();
	m_descriptionLabel.clear();
	m_invalidCursorName.clear();
	m_name.clear();
	m_options = 0;
	m_purchasedLabel.clear();
	m_textLabel.clear();

	// End Add
	
	m_window = NULL;
	m_commandButtonBorder = COMMAND_BUTTON_BORDER_NONE;
	//m_prev = NULL;
	m_next = NULL;			
	m_radiusCursor = RADIUSCURSOR_NONE;													
	
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton::~CommandButton( void )
{
	
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidRelationshipTarget(Relationship r) const
{
	UnsignedInt mask = 0;
	if (r == ENEMIES) mask |= NEED_TARGET_ENEMY_OBJECT;
	else if (r == ALLIES) mask |= NEED_TARGET_ALLY_OBJECT;
	else if (r == NEUTRAL) mask |= NEED_TARGET_NEUTRAL_OBJECT;

	return (m_options & mask) != 0;
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Player* sourcePlayer, const Object* targetObj) const
{
	if (!sourcePlayer || !targetObj)
		return false;

	Relationship r = sourcePlayer->getRelationship(targetObj->getTeam());

	return isValidRelationshipTarget(r);
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Object* sourceObj, const Object* targetObj) const
{
	if (!sourceObj || !targetObj)
		return false;

	Relationship r = sourceObj->getRelationship(targetObj);

	return isValidRelationshipTarget(r);
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidToUseOn(const Object *sourceObj, const Object *targetObj, const Coord3D *targetLocation, CommandSourceType commandSource) const
{
	if (m_upgradeTemplate) {
		// @todo: Make a const version of pui. We're not altering the production queue, so this const-cast
		// is okay.
		ProductionUpdateInterface *pui = const_cast<Object*>(sourceObj)->getProductionUpdateInterface();
		if (pui) {
			const ProductionEntry *pe = pui->firstProduction();
			while (pe) {
				if (pe->getProductionUpgrade() != NULL) 
					return false;
				pe = pui->nextProduction(pe);
			}
			return sourceObj->affectedByUpgrade(m_upgradeTemplate) && !sourceObj->hasUpgrade(m_upgradeTemplate);
		}
		// No ProductionUpdateInterface means we can't do this.
		return false;
	}

	if( BitTest( m_options, COMMAND_OPTION_NEED_OBJECT_TARGET ) && !targetObj ) 
	{
		return false;
	}

	Coord3D pos;
	if( targetLocation )
	{
		pos.set( targetLocation );
	}

	if( BitTest( m_options, NEED_TARGET_POS ) && !targetLocation ) 
	{
		if( targetObj )
		{
			pos.set( targetObj->getPosition() );
		}
		else
		{
			return false;
		}
	}
	
	if( BitTest( m_options, COMMAND_OPTION_NEED_OBJECT_TARGET ) ) 
	{
		return TheActionManager->canDoSpecialPowerAtObject( sourceObj, targetObj, commandSource, m_specialPower, m_options, false );
	}

	if( BitTest( m_options, NEED_TARGET_POS ) ) 
	{
		return TheActionManager->canDoSpecialPowerAtLocation( sourceObj, &pos, commandSource, m_specialPower, NULL, m_options, false );
	}

	return TheActionManager->canDoSpecialPower( sourceObj, m_specialPower, commandSource, m_options, false );
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isReady(const Object *sourceObj) const
{
	SpecialPowerModuleInterface *mod = sourceObj->getSpecialPowerModule( m_specialPower );
	if( mod && mod->getPercentReady() == 1.0f ) 
		return true;
	
	if (m_upgradeTemplate && sourceObj->affectedByUpgrade(m_upgradeTemplate) && !sourceObj->hasUpgrade(m_upgradeTemplate))
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isValidObjectTarget(const Drawable* source, const Drawable* target) const
{
	return isValidObjectTarget(source ? source->getObject() : NULL, target ? target->getObject() : NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// CommandSet /////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** These are the fields you can define in a command set, they correspond to physical
	* buttons in the GUI */
//-------------------------------------------------------------------------------------------------
const FieldParse CommandSet::m_commandSetFieldParseTable[] = 
{
	
	{ "1",			CommandSet::parseCommandButton, (void *)0,		offsetof( CommandSet, m_command ) },
	{ "2",			CommandSet::parseCommandButton, (void *)1,		offsetof( CommandSet, m_command ) },
	{ "3",			CommandSet::parseCommandButton, (void *)2,		offsetof( CommandSet, m_command ) },
	{ "4",			CommandSet::parseCommandButton, (void *)3,		offsetof( CommandSet, m_command ) },
	{ "5",			CommandSet::parseCommandButton, (void *)4,		offsetof( CommandSet, m_command ) },
	{ "6",			CommandSet::parseCommandButton, (void *)5,		offsetof( CommandSet, m_command ) },
	{ "7",			CommandSet::parseCommandButton, (void *)6,		offsetof( CommandSet, m_command ) },
	{ "8",			CommandSet::parseCommandButton, (void *)7,		offsetof( CommandSet, m_command ) },
	{ "9",			CommandSet::parseCommandButton, (void *)8,		offsetof( CommandSet, m_command ) },
	{ "10",			CommandSet::parseCommandButton, (void *)9,		offsetof( CommandSet, m_command ) },
	{ "11",			CommandSet::parseCommandButton, (void *)10,		offsetof( CommandSet, m_command ) },
	{ "12",			CommandSet::parseCommandButton, (void *)11,		offsetof( CommandSet, m_command ) },
	{ "13",			CommandSet::parseCommandButton, (void *)12,		offsetof( CommandSet, m_command ) },
	{ "14",			CommandSet::parseCommandButton, (void *)13,		offsetof( CommandSet, m_command ) },
	{ "15",			CommandSet::parseCommandButton, (void *)14,		offsetof( CommandSet, m_command ) },
	{ "16",			CommandSet::parseCommandButton, (void *)15,		offsetof( CommandSet, m_command ) },
	{ "17",			CommandSet::parseCommandButton, (void *)16,		offsetof( CommandSet, m_command ) },
	{ "18",			CommandSet::parseCommandButton, (void *)17,		offsetof( CommandSet, m_command ) },
	{ NULL,			NULL,														 NULL,				0	}  // keep this last

};

//-------------------------------------------------------------------------------------------------
Bool CommandButton::isContextCommand() const
{
	return BitTest( m_options, CONTEXTMODE_COMMAND );
}

//-------------------------------------------------------------------------------------------------
// bleah. shouldn't be const, but is. sue me. (srj)
void CommandButton::copyImagesFrom( const CommandButton *button, Bool markUIDirtyIfChanged ) const
{
	if( m_buttonImage != button->getButtonImage() )
	{
		m_buttonImage = button->getButtonImage();

		//Code renderer handles these states now.
		//m_disabledImage = button->getDisabledImage();
		//m_hiliteImage = button->getHiliteImage();
		//m_pushedImage = button->getPushedImage();

		if( markUIDirtyIfChanged )
		{
			TheControlBar->markUIDirty();
		}
	}
}

//-------------------------------------------------------------------------------------------------
// bleah. shouldn't be const, but is. sue me. (Kris) -snork!
void CommandButton::copyButtonTextFrom( const CommandButton *button, Bool shortcutButton, Bool markUIDirtyIfChanged ) const
{
	//This function was added to change the strings when you upgrade from a DaisyCutter to a MOAB. All other special
	//powers are the same.
	Bool change = FALSE;
	if( shortcutButton )
	{
		//Not the best code, but conflicting label means shortcut label (most won't have any string specified).
		if( button->getConflictingLabel().isNotEmpty() && m_textLabel.compare( button->getConflictingLabel() ) )
		{
			m_textLabel = button->getConflictingLabel();
			change = TRUE;
		}
	}
	else
	{	
		//Copy the text from the purchase science button if it exists (most won't).
		if( button->getTextLabel().isNotEmpty() && m_textLabel.compare( button->getTextLabel() ) )
		{
			m_textLabel = button->getTextLabel();
			change = TRUE;
		}
	}
	if( button->getDescriptionLabel().isNotEmpty() && m_descriptionLabel.compare( button->getDescriptionLabel() ) )
	{
		m_descriptionLabel = button->getDescriptionLabel();
		change = TRUE;
	}
	if( markUIDirtyIfChanged && change )
	{
		TheControlBar->markUIDirty();
	}
}

//-------------------------------------------------------------------------------------------------
/** Parse a single command button definition */
//-------------------------------------------------------------------------------------------------
void CommandSet::parseCommandButton( INI* ini, void *instance, void *store, const void *userData )
{
	const char *token = ini->getNextToken();

	// get find the command button from this name
	const CommandButton *commandButton = TheCommandStore->findCommandButton( AsciiString( token ) );
	// get the index to store the command at, and the command array itself
	const CommandButton **buttonArray = (const CommandButton **)store;
	Int buttonIndex = (Int)(intptr_t)userData;

	if( commandButton == NULL )
	{
		/* A CommandButton.ini can lack a button a CommandSet names, and the unknown name threw and
			 stopped the game at start.  The slot keeps what it held: empty for a new set and for a
			 patched one, which parseCommandSetDefinition clears first, the parent's button for a map.ini
			 override, which starts as a copy.  Every reader already passes over an empty slot, and the
			 outcome depends only on the files, which are in the INI and map checksums. */
		DEBUG_LOG(( "[LINE: %d - FILE: '%s'] CommandSet %s slot %d names unknown button '%s'; the slot is skipped.\n",
								ini->getLineNum(), ini->getFilename().str(), ((CommandSet *)instance)->getName().str(),
								buttonIndex + 1, token ));
		return;
	}  // end if

	// sanity
	DEBUG_ASSERTCRASH( buttonIndex < MAX_COMMANDS_PER_SET, ("parseCommandButton: button index '%d' out of range\n", 
										 buttonIndex) );

	// save it
	buttonArray[ buttonIndex ] = commandButton;

}  // end parseCommand

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandSet::CommandSet(const AsciiString& name) :
	m_name(name),
	m_next(NULL)
{
	friend_clearCommands();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CommandSet::friend_clearCommands()
{
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		m_command[ i ] = NULL;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
const CommandButton* CommandSet::getCommandButton(Int i) const 
{ 
	const CommandButton* button;
  // Check for TheGameLogic == null, cause it is in Worldbuilder, and wb gets command bar info. jba.
	if (TheGameLogic && TheGameLogic->findControlBarOverride(m_name, i, button))
		return button;

	return m_command[i]; 
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CommandSet::friend_addToList(CommandSet** listHead)
{
	m_next = *listHead;
	*listHead = this;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandSet::~CommandSet( void )
{

}  // end ~CommandSet

///////////////////////////////////////////////////////////////////////////////////////////////////
// ControlBar /////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ControlBar::ControlBar( void )
{
	Int i;
	m_controlBarSchemeManager = NULL;
	m_isObserverCommandBar = FALSE;
	m_observerLookAtPlayer = NULL;
	m_watchedSelection = NULL;
	m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;
	m_buildToolTipLayout = NULL;
	m_showBuildToolTipLayout = FALSE;

	// Added By Sadullah Nader
	// initializing vars to zero
	m_animateDownWin1Pos.x = m_animateDownWin1Pos.y = 0;
	m_animateDownWin1Size.x = m_animateDownWin1Size.y = 0;
	m_animateDownWin2Pos.x = m_animateDownWin2Pos.y = 0;
	m_animateDownWin2Size.x = m_animateDownWin2Size.y = 0;

	m_animateDownWindow = NULL;
	m_animTime = 0;

	m_currContext = CB_CONTEXT_NONE;
	m_defaultControlBarPosition.x = m_defaultControlBarPosition.y = 0;
	m_genStarFlash = FALSE;
	m_purchaseScienceOpen = FALSE;
  m_genStarOff = NULL;
	m_genStarOn  = NULL;
	m_UIDirty    = FALSE;
	//
	m_buildUpClockColor = GameMakeColor(0,0,0,100);
	m_commandBarBorderColor = GameMakeColor(0,0,0,100);
	for( i = 0; i < NUM_CONTEXT_PARENTS; i++ )
		m_contextParent[ i ] = NULL;
	// an empty panel is a panel with no plate, which is what the builder tool wants
	for( i = 0; i < CB_PANEL_COUNT; i++ )
	{
		m_panelRect[ i ].lo.x = m_panelRect[ i ].lo.y = 0;
		m_panelRect[ i ].hi.x = m_panelRect[ i ].hi.y = 0;
	}
	m_panelOrigin.x = m_panelOrigin.y = 0;
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		m_commandWindows[ i ] = NULL;
		m_commandUpgradeRank[ i ] = -1;
	// removed from multiplayer branch
		//m_commandMarkers[ i ] = NULL;
	}

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		m_sciencePurchaseWindowsRank1[i] = NULL;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		m_sciencePurchaseWindowsRank3[i] = NULL;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
		m_sciencePurchaseWindowsRank8[i] = NULL;

	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		m_specialPowerShortcutButtons[i] = NULL;
		m_specialPowerShortcutButtonParents[i] = NULL;
	}

	m_specialPowerShortcutParent = NULL;
	m_specialPowerShortcutRow = -1;
	m_specialPowerShortcutRowMs = 0;
	m_purchaseScienceColumn = -1;
	m_purchaseScienceColumnMs = 0;
	m_specialPowerLayout = NULL;
	m_scienceLayout = NULL;
	m_rightHUDWindow = NULL;
	m_rightHUDCameoWindow = NULL;
	for( i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; i++ )
		m_rightHUDUpgradeCameos[i];
	m_rightHUDUnitSelectParent = NULL;
	m_communicatorButton = NULL;
	m_currentSelectedDrawable = NULL;
	m_currContext = CB_CONTEXT_NONE;
	m_upgradeSpreadFrame = 0;
	m_upgradeSpreadEntries = 0;
	for( i = 0; i < UPGRADE_SPREAD_MAX; i++ )
	{
		m_upgradeSpreadID[ i ] = INVALID_ID;
		m_upgradeSpreadCount[ i ] = 0;
	}
	for( i = 0; i < MAX_MULTI_SELECT_GROUPS; i++ )
	{
		m_multiSelectGroupTemplate[ i ] = NULL;
		m_multiSelectGroupSize[ i ] = 0;
		m_multiSelectGroupFirst[ i ] = INVALID_DRAWABLE_ID;
	}
	m_multiSelectGroupCount = 0;
	m_multiSelectFocus = 0;
	m_rallyPointDrawableID = INVALID_DRAWABLE_ID;
	m_standInBuilderID = INVALID_DRAWABLE_ID;
	m_displayedConstructPercent = -1.0f;
	m_displayedOCLTimerSeconds = 0;
	m_displayedQueueCount = 0;
	resetBuildQueueData();
	resetContainData();
	m_lastRecordedInventoryCount = 0;
	
	m_videoManager = NULL;
	m_animateWindowManager = NULL;
	m_generalsScreenAnimate = NULL;
	m_animateWindowManagerForGenShortcuts = NULL;
	m_flash = FALSE;
	m_toggleButtonUpIn = NULL;
	m_toggleButtonUpOn = NULL;
	m_toggleButtonUpPushed = NULL;
	m_toggleButtonDownIn = NULL;
	m_toggleButtonDownOn = NULL;
	m_toggleButtonDownPushed = NULL;
	
	m_generalButtonEnable = NULL;
	m_generalButtonHighlight = NULL;
	m_genArrow = NULL;
	m_sideSelectAnimateDown = FALSE;
	updateCommanBarBorderColors(GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED,GAME_COLOR_UNDEFINED);
	m_radarAttackGlowWindow = NULL;
	m_remainingRadarAttackGlowFrames = 0;
	m_trayLaidOutMask = TRAY_NOT_LAID_OUT;
	m_trayLaidOutFor = NULL;
	m_trayFramePlaced = FALSE;
	m_radarFitWidth = m_radarFitHeight = 0.0f;
	m_radarColumnShift = 0;
	for( Int fit = 0; fit < RADAR_FIT_COUNT; fit++ )
		m_radarFitBox[ fit ].lo.x = m_radarFitBox[ fit ].lo.y = m_radarFitBox[ fit ].hi.x = m_radarFitBox[ fit ].hi.y = 0;

#if defined( _INTERNAL ) || defined( _DEBUG )
	m_lastFrameMarkedDirty = 0;
	m_consecutiveDirtyFrames = 0;
#endif

}  // end ControlBar

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ControlBar::~ControlBar( void )
{

	if (m_rightHUDCameoWindow && m_rightHUDCameoWindow->winGetUserData())
		delete m_rightHUDCameoWindow->winGetUserData();

	// the layouts, the animation managers and every window pointer, in one place
	shutdownWindows();

	m_genArrow = NULL;

	if( m_controlBarSchemeManager )
		delete m_controlBarSchemeManager;
	m_controlBarSchemeManager = NULL;

}  // end ~ControlBar
void ControlBarPopupDescriptionUpdateFunc( WindowLayout *layout, void *param );

//-------------------------------------------------------------------------------------------------
// Three-panel control bar layout -----------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//
// ControlBar.wnd is authored at 800x600 and GameWindowManagerScript stretches every coordinate by
// the display size over that, separately in x and y.  On a 16:9 screen that is a 1.33x horizontal
// smear: square cameos come out as rectangles and one strip eats the whole bottom of the screen.
// layoutPanels throws the stretch away.  It recovers each window's authored rectangle, then puts it
// back at ONE uniform scale inside one of three panels - radar hard left, command grid centred,
// selection hard right.  Nothing is distorted and the world shows through the two gaps.
//

static const Real CONTROL_BAR_DESIGN_W = 800.0f;
static const Real CONTROL_BAR_DESIGN_H = 600.0f;
static const Real CONTROL_BAR_DESIGN_TOP = 404.0f;		///< the highest any panel reaches

//
// Every rectangle below was measured off the artwork rather than off the screen, and the artwork
// stops a pixel or two inside its own edges: two of the nine plates start at design x 1 rather than
// 0, two more end at 798 rather than 800, and seven of the nine end at y 597 to 599 rather than 600.
// Multiplied up that is a hairline of battlefield down both sides of the screen and along the
// bottom.  Anything this close to an edge is that edge.
//
static const Int CONTROL_BAR_EDGE_SNAP = 4;		///< design units

//
// The authored 800x600 rectangle each panel covers, art and all - the three plate rectangles below
// unioned over the three sides, run down to the bottom edge of the screen.  It is what the panel's
// input-blocking pane becomes, so a click on the artwork does not reach the world behind it.
//
static const IRegion2D thePanelDesignRect[ ControlBar::CB_PANEL_COUNT ] =
{
	{ {   0, 410 }, { 216, 600 } },		// CB_PANEL_LEFT   - radar
	{ { 167, 417 }, { 624, 600 } },		// CB_PANEL_CENTER - money, power, toolbar column, command grid
	{ { 609, 421 }, { 800, 600 } },		// CB_PANEL_RIGHT  - selection portrait and the general's tabs
};

/// where each panel is pinned: the fraction of the screen its anchor lands on...
static const Real thePanelAnchorFraction[ ControlBar::CB_PANEL_COUNT ] = { 0.0f, 0.5f, 1.0f };
/// ...and which authored x that anchor is, so at 4:3 the three plates reassemble the shipped bar
static const Real thePanelAnchorDesignX[ ControlBar::CB_PANEL_COUNT ] = { 0.0f, 400.0f, 800.0f };

//-------------------------------------------------------------------------------------------------
/** ControlBar Pro's bar (xezon and FAS, MIT; Data/Window/ControlBarPro.txt), the bar of every side.
	* It was painted at 1920x1080 and goes on screen in
	* three pieces: the radar pinned bottom left, the command grid bottom centre and the selection with the general's buttons
	* bottom right.  Each is drawn at the screen's height alone, 1080 lines to one, so it keeps the
	* painting's shape and never stretches across the width, and from 4:3 to 32:9 the world shows
	* between them.  The pieces are the three panels with their anchors (thePanelAnchorFraction,
	* thePanelAnchorDesignX); a window goes to a piece by which side of the seams its middle lies, and
	* drawPainting cuts the painting at the same seams, which fall in the gaps of every side's art.
	* Fills the transform from 800x600 design units, where the .wnd loader's stretch is taken back to,
	* to the screen.  Not static: the test calls it. */
//-------------------------------------------------------------------------------------------------
static const Real HUD_ART_ASPECT = 1920.0f / 1080.0f;		///< what ControlBar Pro was painted at
static const Real theHudSeamDesignX[ ControlBar::CB_PANEL_COUNT - 1 ] = { 182.0f, 616.0f };	///< drawPainting's seams

void ControlBarHudPieceTransform( Int panel, Real dispW, Real dispH, Real *sx, Real *sy, Real *originX )
{
	*sy = dispH / CONTROL_BAR_DESIGN_H;
	*sx = *sy * HUD_ART_ASPECT * CONTROL_BAR_DESIGN_H / CONTROL_BAR_DESIGN_W;
	*originX = dispW * thePanelAnchorFraction[ panel ] - thePanelAnchorDesignX[ panel ] * *sx;
}

/// the piece of ControlBar Pro's bar a window whose middle is at this design x belongs to
Int ControlBarHudPieceForDesignX( Real designCenterX )
{
	if( designCenterX < theHudSeamDesignX[ 0 ] )
		return ControlBar::CB_PANEL_LEFT;
	return designCenterX > theHudSeamDesignX[ 1 ] ? ControlBar::CB_PANEL_RIGHT : ControlBar::CB_PANEL_CENTER;
}

//-------------------------------------------------------------------------------------------------
/** One scale for both axes, the smaller of the two, so nothing anywhere in the HUD is distorted.
	* Everything that used to work this out for itself now asks here. */
//-------------------------------------------------------------------------------------------------
Real ControlBarUniformScaleFor( Int displayWidth, Int displayHeight )
{
	if( displayWidth <= 0 || displayHeight <= 0 )
		return 1.0f;

	const Real loadScaleX = (Real)displayWidth / CONTROL_BAR_DESIGN_W;
	const Real loadScaleY = (Real)displayHeight / CONTROL_BAR_DESIGN_H;
	const Real s = loadScaleX < loadScaleY ? loadScaleX : loadScaleY;

	return s < 1.0f ? 1.0f : s;
}

//-------------------------------------------------------------------------------------------------
Real ControlBarUniformScale( void )
{
	if( TheDisplay == NULL )
		return 1.0f;

	return ControlBarUniformScaleFor( TheDisplay->getWidth(), TheDisplay->getHeight() );
}

//-------------------------------------------------------------------------------------------------
Real ControlBarHudScaleFor( Int displayWidth, Int displayHeight )
{
	return (Real)displayHeight / CONTROL_BAR_DESIGN_H * CONTROL_BAR_HUD_PERCENT / 100.0f;
}

//-------------------------------------------------------------------------------------------------
Real ControlBarHudPageScale( void )
{
	return ControlBarHudScale() * 100.0f / CONTROL_BAR_HUD_PERCENT;
}

//-------------------------------------------------------------------------------------------------
Real ControlBarHudScale( void )
{
	if( TheDisplay == NULL )
		return 1.0f;

	return ControlBarHudScaleFor( TheDisplay->getWidth(), TheDisplay->getHeight() );
}

//-------------------------------------------------------------------------------------------------
/** One window of a layout being taken out of the loader's stretched space, and everything under it.
	* Positions are relative to the parent, so both the parent's old and its new screen origin travel
	* down the recursion - the same walk placeInPanel does, without the panels and the plate art. */
//-------------------------------------------------------------------------------------------------
static void layoutUniformWindow( GameWindow *win, Real originX, Real originY, Real sx, Real sy,
																 Real loadScaleX, Real loadScaleY,
																 Int oldParentX, Int oldParentY,
																 Int newParentX, Int newParentY )
{
	ICoord2D rel, size;
	win->winGetPosition( &rel.x, &rel.y );
	win->winGetSize( &size.x, &size.y );

	const Int oldX = oldParentX + rel.x;
	const Int oldY = oldParentY + rel.y;

	// what the window was authored at, recovered by dividing the loader's own two scales back out
	const Real designX = oldX / loadScaleX;
	const Real designY = oldY / loadScaleY;
	const Real designW = size.x / loadScaleX;
	const Real designH = size.y / loadScaleY;

	const Int newX = REAL_TO_INT_FLOOR( originX + designX * sx );
	const Int newY = REAL_TO_INT_FLOOR( originY + designY * sy );

	win->winSetPosition( newX - newParentX, newY - newParentY );
	win->winSetSize( REAL_TO_INT_CEIL( designW * sx ), REAL_TO_INT_CEIL( designH * sy ) );

	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
		layoutUniformWindow( child, originX, originY, sx, sy, loadScaleX, loadScaleY,
												 oldX, oldY, newX, newY );
}

//-------------------------------------------------------------------------------------------------
void ControlBarLayoutUniform( GameWindow *root, Real anchorFracX, Real anchorFracY )
{
	if( root == NULL || TheDisplay == NULL )
		return;

	const Real dispW = (Real)TheDisplay->getWidth();
	const Real dispH = (Real)TheDisplay->getHeight();
	if( dispW <= 0.0f || dispH <= 0.0f )
		return;

	// what the loader already multiplied every coordinate by, so it can be divided back out
	const Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;
	const Real loadScaleY = dispH / CONTROL_BAR_DESIGN_H;

	// the rank screen and power bars at ControlBar Pro's pieces' scale
	Real sx, sy, unusedOriginX;
	ControlBarHudPieceTransform( ControlBar::CB_PANEL_RIGHT, dispW, dispH, &sx, &sy, &unusedOriginX );

	//
	// The anchor is a point that does not move: the same fraction of the screen and of the design
	// space.  (1,1) keeps the bottom right corner where it was however wide the screen is, which is
	// what a bar hanging off the right edge above the command bar wants.
	//
	const Real originX = dispW * anchorFracX - CONTROL_BAR_DESIGN_W * anchorFracX * sx;
	const Real originY = dispH * anchorFracY - CONTROL_BAR_DESIGN_H * anchorFracY * sy;

	ICoord2D rootOrigin;
	root->winGetScreenPosition( &rootOrigin.x, &rootOrigin.y );

	//
	// The root's own position is already absolute, so its "parent" is the screen at both ends of the
	// walk - what it was, and what it becomes, are both the origin.
	//
	GameWindow *parent = root->winGetParent();
	ICoord2D parentPos;
	parentPos.x = parentPos.y = 0;
	if( parent )
		parent->winGetScreenPosition( &parentPos.x, &parentPos.y );

	layoutUniformWindow( root, originX, originY, sx, sy, loadScaleX, loadScaleY,
											 parentPos.x, parentPos.y, parentPos.x, parentPos.y );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBarPanelDesignToScreen( Int panel, const IRegion2D *design,
																		Int displayWidth, Int displayHeight, IRegion2D *rectOut )
{
	if( panel < 0 || panel >= ControlBar::CB_PANEL_COUNT || design == NULL || rectOut == NULL )
		return FALSE;
	if( displayWidth <= 0 || displayHeight <= 0 )
		return FALSE;

	const Real dispW = (Real)displayWidth;
	const Real dispH = (Real)displayHeight;

	//
	// One scale for both axes, the smaller of the two so the three panels always fit side by side.
	// At 4:3 that is exactly what the .wnd loader would have used and the panels still meet; at
	// anything wider they shrink together and leave the middle of the screen bottom open instead of
	// stretching to fill it.  ControlBarUniformScale() is this same number for the display that is
	// actually up; here the size is a parameter, because the tests ask about screens we are not on.
	//
	// ControlBar Pro's pieces
	Real sx, sy, originX;
	ControlBarHudPieceTransform( panel, dispW, dispH, &sx, &sy, &originX );

	rectOut->lo.x = REAL_TO_INT_FLOOR( originX + design->lo.x * sx );
	rectOut->hi.x = REAL_TO_INT_CEIL ( originX + design->hi.x * sx );
	rectOut->lo.y = REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - design->lo.y ) * sy );
	rectOut->hi.y = REAL_TO_INT_CEIL ( dispH - ( CONTROL_BAR_DESIGN_H - design->hi.y ) * sy );

	//
	// The screen has three edges the bar touches and each belongs to one panel: the left panel owns
	// the left edge, the right panel the right, and all three stand on the bottom.  The centre
	// panel's own left and right are interior - at 4:3 they are where it meets the other two - so
	// they are left where the arithmetic put them.
	//
	if( panel == ControlBar::CB_PANEL_LEFT && design->lo.x <= CONTROL_BAR_EDGE_SNAP )
		rectOut->lo.x = 0;
	if( panel == ControlBar::CB_PANEL_RIGHT &&
			design->hi.x >= REAL_TO_INT( CONTROL_BAR_DESIGN_W ) - CONTROL_BAR_EDGE_SNAP )
		rectOut->hi.x = displayWidth;
	if( design->hi.y >= REAL_TO_INT( CONTROL_BAR_DESIGN_H ) - CONTROL_BAR_EDGE_SNAP )
		rectOut->hi.y = displayHeight;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** What layoutPanels remembers about one window: the 800x600 rectangle it was authored at, and the
	* screen rectangle it was last given.  If a window is not where we left it, something else moved
	* it - ControlBarScheme::init does, in the loader's own stretched space - and its authored
	* rectangle has to be read back out of that. */
//-------------------------------------------------------------------------------------------------
struct ControlBarPanelPlacement
{
	Bool known;
	Int panel;						///< which of the three it was put in
	Real designX, designY, designW, designH;
	Int placedX, placedY, placedW, placedH;
};
typedef std::map< GameWindow *, ControlBarPanelPlacement > ControlBarPanelPlacementMap;
static ControlBarPanelPlacementMap theControlBarPlacement;

//-------------------------------------------------------------------------------------------------
/** Which piece of ControlBar Pro's bar a direct child of ControlBarParent belongs to: a window goes
	* by its middle, and the observer's panes with the radar they are drawn beside. */
//-------------------------------------------------------------------------------------------------
static Int panelForWindow( const char *shortName, Real designCenterX )
{
	return strncmp( shortName, "ObserverPlayer", 14 ) == 0 ? ControlBar::CB_PANEL_LEFT : ControlBarHudPieceForDesignX( designCenterX );
}

//-------------------------------------------------------------------------------------------------
// The plates -------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//
// Three per side, cut from that side's shipped painting of the bar.  See ControlBar.h for what the
// readout rectangle is and why it is the thing that gets anchored.
//

struct ControlBarPlateSet
{
	const char *sidePrefix;		///< matched against the scheme's Side, so every China general fits China
	ControlBarPlate plate[ ControlBar::CB_PANEL_COUNT ];
};

//
// Every one of the nine plates paints its own minimise tab, arrow and all, because each is that
// side's own composition rather than a cut of the shipped bar - and none of them paints it where
// ControlBar.wnd puts ButtonLarge (666,445 to 714,473).  Left alone the button drew a second tab
// beside the painted one, up to nineteen design units across and twelve down at 1920x1080.  The
// rectangle below is the tab as the art paints it, measured off the targa: the button takes it, and
// the image it draws lands on the painting instead of next to it.
//
static const ControlBarPlateSet thePlateSets[] =
{
	{ "America",
		{
			{ "ReforgedBarAmericaLeft.tga",		{ {   0, 412 }, { 183, 599 } }, 0, 0, 0, 293, 301 },
			/* This painting is 170 design units tall for a 180-unit slot, and it is the plate's own
				 composition rather than a straight cut of the shipped bar: its grid field lines up with
				 the command buttons (494-589) exactly where it stands, and its money box does not line
				 up with the scheme's readout.  So it stands on the bottom of the screen, where a plate
				 belongs, and the readout moves down into the box - the box's dark interior measures
				 448.5 to 465.4, middle 456.6, where ControlBarScheme.ini puts the readout's middle at
				 447.5.  Nine.  (It was ten while the readout was landing at ControlBar.wnd's 446.5
				 instead; see the slide note in layoutPanels.)

				 The power bar moves for the same reason and is the same kind of measurement.  This art
				 paints the rail in two halves - a dark groove at texels 73 to 80 and the silver tube
				 that reads as the gauge at 81 to 85, design 479.8 to 482.3 - where
				 ControlBarScheme.ini puts America's 6-unit power window at 470 to 476, on the pale
				 ledge above the whole assembly.  At 1280x720 that left the bar's left half on the
				 terrain, over the plate's sloping top edge.  Ten sits the bar on the tube, which is
				 the part of the rail that looks like a slot to read a bar in, with the tube's own top
				 edge showing above it; five only got it as far as the dark groove and left the tube
				 standing empty underneath, and eight covered the tube to its last texel.  Twelve is
				 the ceiling - past that the bar is off the rail and on the button field.  China and
				 GLA stay at 0 - their windows already cover their own rails, top highlight to bottom
				 band.

				 The grid moves the same way and for the same reason.  The painting's striped field
				 runs 224.6 to 614.2, and the fourteen command buttons are 223 to 603: the first
				 column overhangs the left bezel by a unit and a half while eleven units stand empty
				 on the right.  Six to the right centres the block in the field. */
			{ "ReforgedBarAmericaCenter.tga",	{ { 180, 429 }, { 623, 599 } }, 9, 10, 6, 706, 271 },
			{ "ReforgedBarAmericaRight.tga",	{ { 610, 433 }, { 800, 599 } }, 0, 0, 0, 304, 268,
																			{ { 648, 434 }, { 717, 460 } } },
		}
	},
	{ "China",
		{
			{ "ReforgedBarChinaLeft.tga",		{ {   1, 417 }, { 196, 598 } }, 0, 0, 0, 315, 295 },
			{ "ReforgedBarChinaCenter.tga",	{ { 176, 433 }, { 617, 597 } }, 0, 0, 0, 718, 269 },
			{ "ReforgedBarChinaRight.tga",	{ { 611, 424 }, { 798, 598 } }, 0, 0, 0, 303, 284,
																		{ { 639, 430 }, { 686, 463 } } },
		}
	},
	{ "GLA",
		{
			{ "ReforgedBarGLALeft.tga",		{ {   0, 416 }, { 215, 599 } }, 0, 0, 0, 345, 295 },
			/* No shift: this plate paints its box at design 443.2 to 459.3, middle 451.3, and
				 ControlBarScheme.ini already puts GLA's readout at 443 to 462, middle 452.5. */
			{ "ReforgedBarGLACenter.tga",	{ { 168, 437 }, { 617, 599 } }, 0, 0, 0, 720, 261 },
			{ "ReforgedBarGLARight.tga",	{ { 612, 423 }, { 799, 599 } }, 0, 0, 0, 300, 284,
																	{ { 631, 428 }, { 705, 465 } } },
		}
	},
};
static const Int NUM_PLATE_SETS = sizeof( thePlateSets ) / sizeof( thePlateSets[ 0 ] );

//-------------------------------------------------------------------------------------------------
const ControlBarPlate *ControlBarPlateForSide( const AsciiString& side, Int panel )
{
	if( side.isEmpty() || panel < 0 || panel >= ControlBar::CB_PANEL_COUNT )
		return NULL;

	// the bar is ControlBar Pro's, its scheme's painting (drawPainting), so a bar that is up wears no
	// plate; the plates are only the tests' to measure now
	if( TheControlBar != NULL )
		return NULL;

	// the boss bar is Chinese and the observer bar is American - EA built each out of that art
	if( side.startsWithNoCase( "Boss" ) )
		return &thePlateSets[ 1 ].plate[ panel ];
	if( side.startsWithNoCase( "Observer" ) )
		return &thePlateSets[ 0 ].plate[ panel ];

	for( Int i = 0; i < NUM_PLATE_SETS; i++ )
		if( side.startsWithNoCase( thePlateSets[ i ].sidePrefix ) )
			return &thePlateSets[ i ].plate[ panel ];

	return NULL;
}

//-------------------------------------------------------------------------------------------------
/** The part of a window's decorated name after the colon. */
//-------------------------------------------------------------------------------------------------
static const char *shortWindowName( GameWindow *win )
{
	const char *name = win->winGetInstanceData()->m_decoratedNameString.str();
	const char *colon = strchr( name, ':' );
	return colon ? colon + 1 : name;
}

//-------------------------------------------------------------------------------------------------
// Where a plate is transparent ---------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

static const Int TARGA_HEADER_SIZE = 18;
static const UnsignedByte TARGA_TRUE_COLOR = 2;
static const UnsignedByte TARGA_TRUE_COLOR_RLE = 10;
static const UnsignedByte TARGA_BITS_PER_PIXEL = 32;
static const UnsignedByte TARGA_TOP_ORIGIN = 0x20;
static const UnsignedByte TARGA_RLE_REPEAT = 0x80;
static const UnsignedByte TARGA_RLE_COUNT = 0x7f;
static const Int TARGA_BYTES_PER_PIXEL = 4;
static const Int TARGA_ALPHA_OFFSET = 3;

/// half-covered or more catches a click; the soft rim of a sloped edge below that lets it through
static const UnsignedByte PLATE_SOLID_ALPHA = 128;

//-------------------------------------------------------------------------------------------------
Bool ControlBarPlateMaskFromTarga( const UnsignedByte *data, Int length, Int artW, Int artH,
																	 std::vector<UnsignedByte> &mask )
{
	if( data == NULL || length < TARGA_HEADER_SIZE || artW <= 0 || artH <= 0 )
		return FALSE;

	const UnsignedByte imageType = data[ 2 ];
	const Int width = data[ 12 ] | ( data[ 13 ] << 8 );
	const Int height = data[ 14 ] | ( data[ 15 ] << 8 );
	if( data[ 1 ] != 0 || ( imageType != TARGA_TRUE_COLOR && imageType != TARGA_TRUE_COLOR_RLE ) )
		return FALSE;
	if( data[ 16 ] != TARGA_BITS_PER_PIXEL || width < artW || height < artH )
		return FALSE;
	const Bool topOrigin = ( data[ 17 ] & TARGA_TOP_ORIGIN ) != 0;

	mask.assign( artW * artH, 0 );
	Int cursor = TARGA_HEADER_SIZE + data[ 0 ];
	const Int pixelCount = width * height;
	Int pixel = 0;
	UnsignedByte alpha = 0;
	while( pixel < pixelCount )
	{
		Int run = 1;
		Bool repeated = FALSE;
		if( imageType == TARGA_TRUE_COLOR_RLE )
		{
			if( cursor >= length )
				return FALSE;
			const UnsignedByte packet = data[ cursor++ ];
			repeated = ( packet & TARGA_RLE_REPEAT ) != 0;
			run = ( packet & TARGA_RLE_COUNT ) + 1;
		}

		for( Int i = 0; i < run && pixel < pixelCount; i++, pixel++ )
		{
			if( repeated == FALSE || i == 0 )
			{
				if( cursor + TARGA_BYTES_PER_PIXEL > length )
					return FALSE;
				alpha = data[ cursor + TARGA_ALPHA_OFFSET ];
				cursor += TARGA_BYTES_PER_PIXEL;
			}

			const Int x = pixel % width;
			const Int fileRow = pixel / width;
			const Int y = topOrigin ? fileRow : height - 1 - fileRow;
			if( x < artW && y < artH )
				mask[ y * artW + x ] = alpha >= PLATE_SOLID_ALPHA;
		}
	}
	return TRUE;
}

//
// A plate's mask is read off its targa the first time a click lands on one of its panes, and kept for
// the session.  A targa that does not read leaves the mask empty, and an empty mask counts as solid
// everywhere, so that pane goes back to catching every click over its rectangle.
//
typedef std::map< const ControlBarPlate *, std::vector<UnsignedByte> > ControlBarPlateMaskMap;
static ControlBarPlateMaskMap thePlateMasks;

static const std::vector<UnsignedByte> &plateMask( const ControlBarPlate *plate )
{
	ControlBarPlateMaskMap::iterator it = thePlateMasks.find( plate );
	if( it != thePlateMasks.end() )
		return it->second;

	std::vector<UnsignedByte> &mask = thePlateMasks[ plate ];
	AsciiString path;
	path.format( "Art\\Textures\\%s", plate->filename );
	File *file = TheFileSystem ? TheFileSystem->openFile( path.str(), File::READ | File::BINARY ) : NULL;
	if( file == NULL )
	{
		DEBUG_LOG(( "CONTROLBAR PLATE MASK %s did not open, its panes catch every click\n", path.str() ));
		return mask;
	}

	const Int length = file->size();
	char *bytes = file->readEntireAndClose();
	if( ControlBarPlateMaskFromTarga( (const UnsignedByte *)bytes, length, plate->artW, plate->artH, mask ) == FALSE )
	{
		DEBUG_LOG(( "CONTROLBAR PLATE MASK %s is not a 32-bit targa of at least %dx%d, its panes catch every click\n",
								path.str(), plate->artW, plate->artH ));
		mask.clear();
	}
	delete [] bytes;
	return mask;
}


//-------------------------------------------------------------------------------------------------
void ControlBar::triggerRadarAttackGlow( void )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if( m_radarAttackGlowWindow == NULL )
		return;
	m_remainingRadarAttackGlowFrames = ConvertRetailFrames( 150 );
	m_radarAttackGlowWindow->winEnable( FALSE );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::letsClickThrough( GameWindow *window, Int x, Int y )
{
	GameWindow *frame = window->winGetParent();
	if( frame == NULL || m_controlBarSchemeManager == NULL || TheDisplay == NULL )
		return FALSE;
	if( frame->winGetWindowId() != (Int)TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" ) )
		return FALSE;

	const char *shortName = shortWindowName( window );
	if( shortName[ 0 ] != 0 && strcmp( shortName, "CenterBackground" ) != 0 )
		return FALSE;

	// a command button or anything else inside the pane is its own window and keeps its click
	if( window->winPointInChild( x, y, TRUE ) != window )
		return FALSE;

	// the plates are drawn from design rectangles and travel with the frame, the same
	// arithmetic W3DCommandBarBackgroundDraw paints them with
	ICoord2D now;
	frame->winGetScreenPosition( &now.x, &now.y );
	for( Int p = 0; p < CB_PANEL_COUNT; p++ )
	{
		const ControlBarPlate *plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentSide(), p );
		if( plate == NULL )
			plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentArtTwinSide(), p );
		IRegion2D rect;
		if( plate == NULL ||
				ControlBarPanelDesignToScreen( p, &plate->design, TheDisplay->getWidth(), TheDisplay->getHeight(), &rect ) == FALSE )
			continue;

		const Int left = rect.lo.x + now.x - m_panelOrigin.x;
		const Int top = rect.lo.y + now.y - m_panelOrigin.y;
		const Int width = rect.width();
		const Int height = rect.height();
		if( width <= 0 || height <= 0 || x < left || y < top || x >= left + width || y >= top + height )
			continue;

		const std::vector<UnsignedByte> &mask = plateMask( plate );
		if( mask.empty() )
			return FALSE;
		const Int texelX = ( x - left ) * plate->artW / width;
		const Int texelY = ( y - top ) * plate->artH / height;
		if( mask[ texelY * plate->artW + texelX ] )
			return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Place 'win' and everything under it inside 'panel'.  Positions are relative to the parent, so
	* both the parent's old and new screen origins travel down the recursion: the old one to work out
	* where the window was, the new one to say where to put it. */
//-------------------------------------------------------------------------------------------------
void ControlBar::placeInPanel( GameWindow *win, Int panel,
															 Int oldParentX, Int oldParentY,
															 Int newParentX, Int newParentY,
															 Int shiftX )
{
	const Real dispW = (Real)TheDisplay->getWidth();
	const Real dispH = (Real)TheDisplay->getHeight();
	const Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;
	const Real loadScaleY = dispH / CONTROL_BAR_DESIGN_H;
	const Real s = ControlBarUniformScale();
	Real sx, sy, originX;
	ControlBarHudPieceTransform( panel, dispW, dispH, &sx, &sy, &originX );

	ICoord2D rel, size;
	win->winGetPosition( &rel.x, &rel.y );
	win->winGetSize( &size.x, &size.y );

	ControlBarPanelPlacement &place = theControlBarPlacement[ win ];
	const Int oldX = oldParentX + rel.x;
	const Int oldY = oldParentY + rel.y;

	if( place.known == FALSE || oldX != place.placedX || oldY != place.placedY ||
			size.x != place.placedW || size.y != place.placedH )
	{
		// first sight of this window, or somebody else has moved it in the loader's stretched space
		place.designX = oldX / loadScaleX;
		place.designY = oldY / loadScaleY;
		place.designW = size.x / loadScaleX;
		place.designH = size.y / loadScaleY;
	}

	const char *shortName = shortWindowName( win );

	Int newX = REAL_TO_INT_FLOOR( originX + place.designX * sx );
	Int newY = REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - place.designY ) * sy );
	Int newW = REAL_TO_INT_CEIL( place.designW * sx );
	Int newH = REAL_TO_INT_CEIL( place.designH * sy );

	//
	// A plate is its side's own painting rather than a cut of the shipped one, so where it draws the
	// money box and the grid field is the plate's business and not ControlBarScheme.ini's.  Where
	// the two disagree the plate says by how much and the windows follow the art: the readout drops
	// into its box, and everything CenterBackground holds - the command grid, the beacon pane, the
	// observer panes - slides across into the field.  See ControlBarPlate.
	//
	const ControlBarPlate *plate = NULL;
	if( m_controlBarSchemeManager )
	{
		plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentSide(), panel );
		if( plate == NULL )
			plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentArtTwinSide(), panel );
	}

	newX += shiftX;
	if( plate && strcmp( shortName, "MoneyDisplay" ) == 0 )
		newY += REAL_TO_INT_FLOOR( plate->readoutShiftY * s );
	if( plate && strcmp( shortName, "PowerWindow" ) == 0 )
		newY += REAL_TO_INT_FLOOR( plate->powerShiftY * s );

	// the grid shift starts at CenterBackground and is inherited by everything under it
	Int childShiftX = shiftX;
	if( strcmp( shortName, "CenterBackground" ) == 0 )
		childShiftX = plate ? REAL_TO_INT_FLOOR( plate->gridShiftX * s ) : 0;

	win->winSetPosition( newX - newParentX, newY - newParentY );
	win->winSetSize( newW, newH );

	place.known = TRUE;
	place.panel = panel;
	place.placedX = newX;
	place.placedY = newY;
	place.placedW = newW;
	place.placedH = newH;

	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
		placeInPanel( child, panel, oldX, oldY, newX, newY, childShiftX );

}  // end placeInPanel

//-------------------------------------------------------------------------------------------------
/** A window moved without its place moving was read by the next layoutPanels as moved by somebody
	* else, in the loader's stretched space, and its size taken back through the loader's scale: the
	* command grid, lowered to the bottom edge, came out three quarters as wide after a watcher's
	* selection rebuilt the bar.  The children keep their own places, since they stay where they
	* stood on screen. */
//-------------------------------------------------------------------------------------------------
void ControlBar::placeWindowAt( GameWindow *window, const IRegion2D &rect )
{
	// a layout without the named window (a modded ControlBar.wnd, a panel not built yet) hands in null
	if( window == nullptr )
		return;
	ICoord2D screen, size;
	window->winGetScreenPosition( &screen.x, &screen.y );
	window->winGetSize( &size.x, &size.y );
	const Int dx = rect.lo.x - screen.x;
	const Int dy = rect.lo.y - screen.y;
	const Int width = rect.hi.x - rect.lo.x;
	const Int height = rect.hi.y - rect.lo.y;
	if( dx == 0 && dy == 0 && size.x == width && size.y == height )
		return;

	Int x = 0, y = 0;
	window->winGetPosition( &x, &y );
	window->winSetPosition( x + dx, y + dy );
	window->winSetSize( width, height );
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
	{
		child->winGetPosition( &x, &y );
		child->winSetPosition( x - dx, y - dy );
	}

	// a window made in code, a multi-selection's cell, has no place until the next layoutPanels
	// gives it one, and that one reads it as first seen whatever is written here
	ControlBarPanelPlacement &place = theControlBarPlacement[ window ];
	place.placedX += dx;
	place.placedY += dy;
	place.placedW = width;
	place.placedH = height;
}

//-------------------------------------------------------------------------------------------------
/** Where the command bar's windows actually are, for -uidrill to write down.
	*
	* The frame's own position is not the measurement: when the bar drifted, the frame held still and
	* the money readout, the toolbar column and the general's tabs climbed out of it one rebuild at a
	* time.  So this is the top edge of the highest visible thing the bar owns, which is a single
	* number that stays put on a bar that stays put.  MoneyDisplay is named as well because it is the
	* one that moved first and furthest. */
//-------------------------------------------------------------------------------------------------
static void controlBarHighestTop( GameWindow *win, Int *topOut, Int *moneyOut )
{
	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
	{
		if( !child->winIsHidden() )
		{
			Int sx, sy;
			child->winGetScreenPosition( &sx, &sy );
			if( sy < *topOut )
				*topOut = sy;
			if( strcmp( shortWindowName( child ), "MoneyDisplay" ) == 0 )
				*moneyOut = sy;
		}
		controlBarHighestTop( child, topOut, moneyOut );
	}
}

/** The first command button on the bar that has a CommandButton behind it - the thing a player's
	* pointer lands on, and the only kind of window the build tooltip is populated from. */
static GameWindow *controlBarFirstCommandButton( GameWindow *win )
{
	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
	{
		if( BitTest( child->winGetStyle(), GWS_PUSH_BUTTON ) && GadgetButtonGetData( child ) != NULL )
			return child;
		GameWindow *found = controlBarFirstCommandButton( child );
		if( found )
			return found;
	}
	return NULL;
}

void ControlBar_logPlacement( const char *tag, Int frame )
{
	if( TheControlBar == NULL || TheDisplay == NULL )
		return;

	Int top = TheDisplay->getHeight();
	Int money = -1;
	TheControlBar->forEachPlacedWindow( &top, &money );

	Int markX, markY;
	TheControlBar->getBackgroundMarkerPos( &markX, &markY );
	const ICoord2D *origin = TheControlBar->getPanelOrigin();

	/* Both halves of the shift, because only their difference moves anything: the marker position
		 recorded when the bar was built, and where that same window is on screen right now. */
	Int liveX = -1, liveY = -1;
	GameWindow *markerWin = TheWindowManager->winGetWindowFromId(
		NULL, TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) ) );
	if( markerWin )
		markerWin->winGetScreenPosition( &liveX, &liveY );

	/* The other half of the complaint was tooltips off the screen, so put a real one up - the same
		 call hovering a build button makes - and write down the rectangle it landed in.  The popup
		 stands on the radar's frame, so it goes wherever the drift takes the radar.  Kept because it
		 is the only thing that says where it landed. */
	Int tipX = -1, tipY = -1, tipW = 0, tipH = 0;
	GameWindow *master = TheControlBar->getMasterParent();
	GameWindow *button = master ? controlBarFirstCommandButton( master ) : NULL;
	if( button )
	{
		TheControlBar->populateBuildTooltipLayout( (const CommandButton *)GadgetButtonGetData( button ) );
		WindowLayout *tip = TheControlBar->getBuildTooltipLayout();
		GameWindow *tipWin = tip ? tip->getFirstWindow() : NULL;
		if( tipWin )
		{
			tipWin->winGetScreenPosition( &tipX, &tipY );
			tipWin->winGetSize( &tipW, &tipH );
		}
		if( tip )
			tip->hide( TRUE );
	}

	// what the bar shows against what is selected: an empty selection showing a command set is a
	// builder standing in, which only Reforged has
	DEBUG_LOG(("UIDRILL: frame %d %s top %d money %d origin (%d,%d) marker (%d,%d) live (%d,%d) tip (%d,%d %dx%d) screen %dx%d selected %d context %d\n",
		frame, tag, top, money, origin->x, origin->y, markX, markY, liveX, liveY,
		tipX, tipY, tipW, tipH,
		TheDisplay->getWidth(), TheDisplay->getHeight(),
		TheInGameUI ? TheInGameUI->getSelectCount() : -1, (Int)TheControlBar->getCurrentContext()));
}

void ControlBar::forEachPlacedWindow( Int *topOut, Int *moneyOut )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL )
		return;
	controlBarHighestTop( parent, topOut, moneyOut );
}

//-------------------------------------------------------------------------------------------------
/** Re-anchor the control bar as three panels at one uniform scale. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutPanels( void )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL || TheDisplay == NULL )
		return;

	const Real dispW = (Real)TheDisplay->getWidth();
	const Real dispH = (Real)TheDisplay->getHeight();

	// what the .wnd loader already multiplied every x coordinate by, so we can divide it back out
	const Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;

	Int p;
	for( p = 0; p < CB_PANEL_COUNT; p++ )
		ControlBarPanelDesignToScreen( p, &thePanelDesignRect[ p ],
																	 TheDisplay->getWidth(), TheDisplay->getHeight(),
																	 &m_panelRect[ p ] );

	// the children's positions are relative to the frame, so grab where it is before moving it
	ICoord2D barOrigin;
	parent->winGetScreenPosition( &barOrigin.x, &barOrigin.y );

	// the frame itself stays full width - it draws nothing and passes input through
	Real sx, sy, originX;
	ControlBarHudPieceTransform( CB_PANEL_LEFT, dispW, dispH, &sx, &sy, &originX );		// the same height for every piece
	const Int parentTop =
		REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - CONTROL_BAR_DESIGN_TOP ) * sy );
	parent->winSetPosition( 0, parentTop );
	parent->winSetSize( REAL_TO_INT_CEIL( dispW ), REAL_TO_INT_CEIL( dispH ) - parentTop );

	// A click that passes a transparent part of a plate lands on the frame, and a unit under it can
	// only be picked if the frame says it can be seen through; see letsClickThrough.
	parent->winSetStatus( WIN_STATUS_SEE_THRU );

	// the plates are drawn from design rectangles rather than from a window, so they need to be told
	// where the frame they belong to started out - see getPanelOrigin
	m_panelOrigin.x = 0;
	m_panelOrigin.y = parentTop;

	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		//
		// A window we placed and nobody has moved since goes by where it was authored, which the cache
		// holds.  One somebody has moved goes by where it is now: the windows ControlBarScheme::init
		// puts at its UL/LR are authored at 0,0 and were first seen there, before the scheme ran.
		//
		ICoord2D rel, size;
		child->winGetPosition( &rel.x, &rel.y );
		child->winGetSize( &size.x, &size.y );
		const Int liveX = barOrigin.x + rel.x;
		const Int liveY = barOrigin.y + rel.y;
		ControlBarPanelPlacementMap::const_iterator it = theControlBarPlacement.find( child );
		Real designCenterX = ( liveX + size.x * 0.5f ) / loadScaleX;
		if( it != theControlBarPlacement.end() && it->second.known &&
				liveX == it->second.placedX && liveY == it->second.placedY &&
				size.x == it->second.placedW && size.y == it->second.placedH )
			designCenterX = it->second.designX + it->second.designW * 0.5f;

		placeInPanel( child, panelForWindow( shortWindowName( child ), designCenterX ),
									barOrigin.x, barOrigin.y, 0, parentTop );	}

	fitRadarToMap( TRUE );

}  // end layoutPanels

/// every place layoutPanels remembers for a window and everything under it, moved across by dx
static void shiftPlaces( GameWindow *window, Int dx )
{
	ControlBarPanelPlacementMap::iterator place = theControlBarPlacement.find( window );
	if( place != theControlBarPlacement.end() )
		place->second.placedX += dx;
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
		shiftPlaces( child, dx );
}

//-------------------------------------------------------------------------------------------------
/** A window and everything under it moved across by dx, with their remembered places, so the next
	* layout reads them as where it left them and starts from their authored rectangles again.
	* placeWindowAt keeps the children where they stood; this takes them along. */
//-------------------------------------------------------------------------------------------------
static void shiftWindowTree( GameWindow *window, Int dx )
{
	if( dx == 0 )
		return;
	Int x = 0, y = 0;
	window->winGetPosition( &x, &y );
	window->winSetPosition( x + dx, y );
	shiftPlaces( window, dx );
}

//-------------------------------------------------------------------------------------------------
/** layoutPanels stands the radar in its square box.  A scheme with a radar frame then gives the
	* window the map's own shape at a fixed height, pinned to the box's bottom left where the bar
	* meets the corner of the screen: Radar::findDrawPositions then finds nothing to letterbox and a
	* click maps to the world through the same arithmetic as before.  The blocker becomes the frame
	* round it, which ControlBarSchemeManager::drawPainting draws, and the under-attack lamp at the
	* radar's top right goes where that corner went.
	*
	* 'fromLayout' is layoutPanels having just put the three windows back where the square box has
	* them, which is remembered against the bar's frame.  A new map without a new layout (update)
	* fits from what was remembered, so it never has to lay the bar out again under a slide or a
	* minimised stage. */
//-------------------------------------------------------------------------------------------------
void ControlBar::fitRadarToMap( Bool fromLayout )
{
	static NameKeyType radarID = NAMEKEY( "ControlBar.wnd:LeftHUD" );
	static NameKeyType blockID = NAMEKEY( "ControlBar.wnd:ControlBarProBlockInputRadar" );
	static NameKeyType listID = NAMEKEY( "ControlBar.wnd:ObserverPlayerListWindow" );
	static NameKeyType infoID = NAMEKEY( "ControlBar.wnd:ObserverPlayerInfoWindow" );
	static NameKeyType optionsID = NAMEKEY( "ControlBar.wnd:ButtonOptions" );
	static NameKeyType chatID = NAMEKEY( "ControlBar.wnd:PopupCommunicator" );
	GameWindow *const windows[ RADAR_FIT_COUNT ] =
	{
		TheWindowManager->winGetWindowFromId( NULL, radarID ),
		TheWindowManager->winGetWindowFromId( NULL, blockID ),
		m_radarAttackGlowWindow,
		TheWindowManager->winGetWindowFromId( NULL, listID ),
		TheWindowManager->winGetWindowFromId( NULL, infoID ),
		TheWindowManager->winGetWindowFromId( NULL, optionsID ),
		TheWindowManager->winGetWindowFromId( NULL, chatID ),
	};
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL || windows[ RADAR_FIT_RADAR ] == NULL )
		return;
	ICoord2D origin;
	parent->winGetScreenPosition( &origin.x, &origin.y );

	if( fromLayout )
	{
		// the layout has just put the column back where the square map has it
		m_radarColumnShift = 0;
		for( Int i = 0; i < RADAR_FIT_COUNT; i++ )
		{
			if( windows[ i ] == NULL )
				continue;
			IRegion2D &r = m_radarFitBox[ i ];
			windows[ i ]->winGetScreenPosition( &r.lo.x, &r.lo.y );
			windows[ i ]->winGetSize( &r.hi.x, &r.hi.y );
			r.hi.x += r.lo.x - origin.x;
			r.hi.y += r.lo.y - origin.y;
			r.lo.x -= origin.x;
			r.lo.y -= origin.y;
		}
	}

	// ControlBar::init lays the bar out before GameEngine::init makes the radar; update() fits it then
	if( TheRadar == NULL )
		return;
	const Region3D *extent = TheRadar->getMapExtent();
	m_radarFitWidth = extent->width();
	m_radarFitHeight = extent->height();

	// between maps there is no extent; a scheme without a frame keeps the square well
	const Image *const *frame = m_controlBarSchemeManager ? m_controlBarSchemeManager->getRadarFrame() : NULL;
	if( frame == NULL || m_radarFitWidth <= 0.0f || m_radarFitHeight <= 0.0f )
		return;

	IRegion2D box[ RADAR_FIT_COUNT ];
	for( Int i = 0; i < RADAR_FIT_COUNT; i++ )
	{
		box[ i ] = m_radarFitBox[ i ];
		box[ i ].lo.x += origin.x;
		box[ i ].hi.x += origin.x;
		box[ i ].lo.y += origin.y;
		box[ i ].hi.y += origin.y;
	}

	//
	// The radar's height never changes with the map: the frame stands on the scheme's radar block
	// (BlockRadar, or ControlBar.wnd's blocker where the scheme names none, the observer's 740 that
	// its column starts at), and never above the square.  The map's shape only sets the width, which
	// grows right from the square's left edge.  It stops where what rides on its right meets the next
	// piece of the bar: a side's frame at the command plate's blocker, the observer's player panel at
	// the selection's.  A map wider than that room is letterboxed by Radar::findDrawPositions inside
	// the frame instead.
	//
	// W3DLeftHUDDraw paints the radar a pixel inside the window, so it is the inside that takes the
	// map's shape; rounded, so Radar::findDrawPositions finds under a pixel to letterbox and draws none
	//
	enum { RADAR_BEZEL = 1 };
	static NameKeyType productionID = NAMEKEY( "ControlBar.wnd:ControlBarProBlockInputProduction" );
	static NameKeyType upgradesID = NAMEKEY( "ControlBar.wnd:ControlBarProBlockInputUpgrades" );
	const Bool observer = m_controlBarSchemeManager->getCurrentSide().compareNoCase( "Observer" ) == 0;
	const IRegion2D &square = box[ RADAR_FIT_RADAR ];
	IRegion2D squareOuter;
	ControlBarSchemeManager::radarFrameRect( frame, square, &squareOuter );
	IRegion2D fit;
	fit.lo.x = square.lo.x;
	fit.lo.y = MAX( square.lo.y, box[ RADAR_FIT_BLOCK ].lo.y + square.lo.y - squareOuter.lo.y );
	fit.hi.y = square.hi.y;
	const Int insideHeight = fit.hi.y - fit.lo.y - 2 * RADAR_BEZEL;

	Int room = 0;
	GameWindow *next = TheWindowManager->winGetWindowFromId( NULL, observer ? upgradesID : productionID );
	if( next != NULL && ( !observer || windows[ RADAR_FIT_INFO ] != NULL ) )
	{
		ICoord2D at;
		next->winGetScreenPosition( &at.x, &at.y );
		room = MAX( 0, at.x - ( observer ? box[ RADAR_FIT_INFO ].hi.x : squareOuter.hi.x ) );
	}
	//
	// A wide map also stops at 4:3 of the inside height, room or not: a 2:1 map stretched the radar
	// across half the bar and pushed everything riding on its right with it. Past that the map is
	// letterboxed with black bands top and bottom, as a map wider than the room already was
	//
	static const Real RADAR_MAX_ASPECT = 4.0f / 3.0f;
	const Int insideWidth = MIN( MIN( REAL_TO_INT( insideHeight * m_radarFitWidth / m_radarFitHeight + 0.5f ),
		REAL_TO_INT( insideHeight * RADAR_MAX_ASPECT + 0.5f ) ),
		square.hi.x - square.lo.x - 2 * RADAR_BEZEL + room );
	fit.hi.x = square.lo.x + insideWidth + 2 * RADAR_BEZEL;
	placeWindowAt( windows[ RADAR_FIT_RADAR ], fit );

	IRegion2D outer;
	ControlBarSchemeManager::radarFrameRect( frame, fit, &outer );
	placeWindowAt( windows[ RADAR_FIT_BLOCK ], outer );

	IRegion2D lamp = box[ RADAR_FIT_LAMP ];
	const Int dx = fit.hi.x - square.hi.x, dy = fit.lo.y - square.lo.y;
	lamp.lo.x += dx;
	lamp.hi.x += dx;
	lamp.lo.y += dy;
	lamp.hi.y += dy;
	placeWindowAt( windows[ RADAR_FIT_LAMP ], lamp );

	//
	// The observer's column stands against the frame's right edge, so a tall map's narrower frame
	// takes the column, its plate (drawPainting) and everything on it left with it, and a wide map's
	// wider one right.  The frame's top is the column's, so it only ever moves across.  A side's
	// Options and Chat stand in the middle plate and stay.
	//
	if( !observer )
		return;
	m_radarColumnShift = outer.hi.x - squareOuter.hi.x;
	for( Int i = RADAR_FIT_COLUMN_FIRST; i < RADAR_FIT_COUNT; i++ )
	{
		if( windows[ i ] == NULL )
			continue;
		ICoord2D at;
		windows[ i ]->winGetScreenPosition( &at.x, &at.y );
		shiftWindowTree( windows[ i ], box[ i ].lo.x + m_radarColumnShift - at.x );
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialzie the control bar, this is our interface to the context sinsitive GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::init( void )
{
	m_sideSelectAnimateDown = FALSE;

	// TheCommandStore loaded the buttons and sets before the game logic existed; their images are ours
	postProcessCommands();

	// Init the scheme manager, this will call it's won INI init funciton.
	m_controlBarSchemeManager = NEW ControlBarSchemeManager;
	m_controlBarSchemeManager->init();

	//Added this check because the builder uses the ControlBar, but doesn't care about
	//the GUI.
	if( TheWindowManager )
		initWindows();

}  // end init

//-------------------------------------------------------------------------------------------------
/** Drop every window the bar is holding.  Called before the windows are looked up again, and it is
	* the layouts that matter: they own windows built against the old screen size, and the animation
	* managers hold pointers into them. */
//-------------------------------------------------------------------------------------------------
void ControlBar::shutdownWindows( void )
{
	Int i;

	if( m_scienceLayout )
	{
		m_scienceLayout->destroyWindows();
		m_scienceLayout->deleteInstance();
	}
	m_scienceLayout = NULL;

	if( m_buildToolTipLayout )
	{
		m_buildToolTipLayout->destroyWindows();
		m_buildToolTipLayout->deleteInstance();
	}
	m_buildToolTipLayout = NULL;
	m_showBuildToolTipLayout = FALSE;

	if( m_specialPowerLayout )
	{
		m_specialPowerLayout->destroyWindows();
		m_specialPowerLayout->deleteInstance();
	}
	m_specialPowerLayout = NULL;

	//
	// Each of these remembers the windows it is animating, so a manager that outlived its windows
	// would walk freed memory on its next update.
	//
	if( m_videoManager )
		delete m_videoManager;
	m_videoManager = NULL;
	if( m_animateWindowManager )
		delete m_animateWindowManager;
	m_animateWindowManager = NULL;
	if( m_generalsScreenAnimate )
		delete m_generalsScreenAnimate;
	m_generalsScreenAnimate = NULL;
	if( m_animateWindowManagerForGenShortcuts )
		delete m_animateWindowManagerForGenShortcuts;
	m_animateWindowManagerForGenShortcuts = NULL;

	for( i = 0; i < NUM_CONTEXT_PARENTS; i++ )
		m_contextParent[ i ] = NULL;
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		m_commandWindows[ i ] = NULL;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		m_sciencePurchaseWindowsRank1[ i ] = NULL;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		m_sciencePurchaseWindowsRank3[ i ] = NULL;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
		m_sciencePurchaseWindowsRank8[ i ] = NULL;
	for( i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; i++ )
	{
		m_rightHUDUpgradeCameos[ i ] = NULL;
		m_rightHUDUpgrades[ i ] = NULL;
	}
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		m_specialPowerShortcutButtons[ i ] = NULL;
		m_specialPowerShortcutButtonParents[ i ] = NULL;
	}
	m_specialPowerShortcutParent = NULL;
	m_currentlyUsedSpecialPowersButtons = 0;
	m_rightHUDWindow = NULL;
	m_rightHUDCameoWindow = NULL;
	m_rightHUDUnitSelectParent = NULL;
	m_communicatorButton = NULL;
	m_radarAttackGlowWindow = NULL;
	m_remainingRadarAttackGlowFrames = 0;
	m_animateDownWindow = NULL;
	m_multiSelectTiles.clear();
	m_sideSelectAnimateDown = FALSE;

}  // end shutdownWindows

//-------------------------------------------------------------------------------------------------
/** Look up every window the bar drives, and lay the bar out.  Re-callable: a resolution change
	* rebuilds ControlBar.wnd and then calls this instead of replacing the whole ControlBar. */
//-------------------------------------------------------------------------------------------------
void ControlBar::initWindows( void )
{
	{
		shutdownWindows();

		//
		// the control bar has several windows that make up our context sensitive interface, we
		// want those parent windows so that we can easily hide and show them to make the
		// interface context sensitive
		//
		NameKeyType id;
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
		m_contextParent[ CP_MASTER ] = TheWindowManager->winGetWindowFromId( NULL, id );

		//
		// These windows are new, so anything remembered about the last set of them is about
		// addresses that may well have been handed back out.  Re-anchor the bar as three uniformly
		// scaled panels before anything reads a position off it.
		//
		theControlBarPlacement.clear();
		layoutPanels();

	m_contextParent[ CP_MASTER ]->winGetPosition(&m_defaultControlBarPosition.x, &m_defaultControlBarPosition.y);
		
		m_scienceLayout = TheWindowManager->winCreateLayout("GeneralsExpPoints.wnd");
		m_scienceLayout->hide(TRUE);

		//
		// The rank screen is one painting with the science cameos placed on it, authored at
		// 210..604 x 3..434.  Stretched separately in x and y it is a third too wide on a 16:9
		// screen and every round rank badge on it is an oval.  Centred at the top, at the command
		// bar's own uniform scale, it is the picture that was drawn.
		//
		ControlBarLayoutUniform( m_scienceLayout->getFirstWindow(), 0.5f, 0.0f );

		id = TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:GenExpParent" );

		m_contextParent[ CP_PURCHASE_SCIENCE ] = TheWindowManager->winGetWindowFromId( NULL, id );//m_scienceLayout->getFirstWindow();

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:UnderConstructionWindow" );
		m_contextParent[ CP_UNDER_CONSTRUCTION ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:OCLTimerWindow" );
		m_contextParent[ CP_OCL_TIMER ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:BeaconWindow" );
		m_contextParent[ CP_BEACON ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:CommandWindow" );
		m_contextParent[ CP_COMMAND ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ProductionQueueWindow" );
		m_contextParent[ CP_BUILD_QUEUE ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ObserverPlayerListWindow" );
		m_contextParent[ CP_OBSERVER_LIST ] = TheWindowManager->winGetWindowFromId( NULL, id );

		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ObserverPlayerInfoWindow" );
		m_contextParent[ CP_OBSERVER_INFO ] = TheWindowManager->winGetWindowFromId( NULL, id );


		// get the command windows and save for easy access later
		Int i;
		ICoord2D commandSize, commandPos;
		AsciiString windowName;
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
		{
		
			windowName.format( "ControlBar.wnd:ButtonCommand%02d", i + 1 );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_commandWindows[ i ] = 
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_COMMAND ], id );
			if (m_commandWindows[ i ])
			{
				m_commandWindows[ i ]->winGetPosition(&commandPos.x, &commandPos.y);
				m_commandWindows[ i ]->winGetSize(&commandSize.x, &commandSize.y);
				m_commandWindows[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );

				//
				// A push button drops every right-button event unless it is marked to want them, and
				// ControlBar.wnd marks none of these. Without this, right-clicking a build button
				// never reached processContextSensitiveButtonClick and nothing could be taken back
				// out of a queue from the bar. Note the status word lives twice - GameWindow keeps
				// its own copy and winSetStatus only touches that one, while GadgetPushButtonInput
				// reads the instance data's copy - so both have to be set.
				//
				m_commandWindows[ i ]->winSetStatus( WIN_STATUS_RIGHT_CLICK );
				BitSet( m_commandWindows[ i ]->winGetInstanceData()->m_status, WIN_STATUS_RIGHT_CLICK );
			}

	// removed from multiplayer branch
//			windowName.format( "ControlBar.wnd:CommandMarker%02d", i + 1 );
//			id = TheNameKeyGenerator->nameToKey( windowName.str() );
//			m_commandMarkers[ i ] = 
//				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_COMMAND ], id );
//			// set the size and position to make sure their in the same place as the buttons.
//			m_commandMarkers[i]->winSetPosition(commandPos.x -2, commandPos.y - 2);
//			m_commandMarkers[i]->winSetSize(commandSize.x + 2, commandSize.y + 2);



		}  // end for i


		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank1Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank1[ i ] = 
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			m_sciencePurchaseWindowsRank1[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank3Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank3[ i ] = 
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			m_sciencePurchaseWindowsRank3[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i
		
		for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ ) 
		{
			windowName.format( "GeneralsExpPoints.wnd:ButtonRank8Number%d", i );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_sciencePurchaseWindowsRank8[ i ] = 
				TheWindowManager->winGetWindowFromId( m_contextParent[ CP_PURCHASE_SCIENCE ], id );
			m_sciencePurchaseWindowsRank8[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		}  // end for i

		// keep a pointer to the window making up the right HUD display
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:RightHUD" );
		m_rightHUDWindow = TheWindowManager->winGetWindowFromId( NULL, id );
	
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:WinUnitSelected" );
		m_rightHUDUnitSelectParent = TheWindowManager->winGetWindowFromId( NULL, id );
		
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:CameoWindow" );
		m_rightHUDCameoWindow = TheWindowManager->winGetWindowFromId( NULL, id );
		for( i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS; i++ )
		{
			windowName.format( "ControlBar.wnd:UnitUpgrade%d", i+1 );
			id = TheNameKeyGenerator->nameToKey( windowName.str() );
			m_rightHUDUpgradeCameos[ i ] =
				TheWindowManager->winGetWindowFromId( m_rightHUDWindow, id );
			m_rightHUDUpgradeCameos[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
			m_rightHUDUpgradeCameos[ i ]->winSetTooltipFunc( commandButtonTooltip );
		}

		// the multi-select unit grid cells over the right HUD are created on demand by
		// layoutMultiSelectTiles; the windows die with the rest of the layout
		m_multiSelectTiles.clear();

//		m_transitionHandler = NEW GameWindowTransitionsHandler;
//		m_transitionHandler->load();
//		m_transitionHandler->init();

		// don't forget about the communicator button CCB
		id = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PopupCommunicator" );
		m_communicatorButton = TheWindowManager->winGetWindowFromId( NULL, id );
		setControlCommand(m_communicatorButton, findCommandButton("NonCommand_Communicator") );
		m_communicatorButton->winSetTooltipFunc(commandButtonTooltip);

		GameWindow *win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonOptions"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_Options") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonIdleWorker"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_IdleWorker") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		// retail's ControlBar.wnd has none of these, and retail's CommandButton.ini none of the buttons
		static const char *const idleButtons[][ 2 ] =
		{
			{ "ControlBar.wnd:ButtonIdleInfantry", "NonCommand_IdleInfantry" },
			{ "ControlBar.wnd:ButtonIdleVehicle", "NonCommand_IdleVehicle" },
			{ "ControlBar.wnd:ButtonIdleAircraft", "NonCommand_IdleAircraft" },
		};
		for( Int idle = 0; idle < 3; idle++ )
		{
			win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( idleButtons[ idle ][ 0 ] ) );
			const CommandButton *idleCommand = findCommandButton( idleButtons[ idle ][ 1 ] );
			if( win && idleCommand )
			{
				setControlCommand( win, idleCommand );
				win->winSetTooltipFunc( commandButtonTooltip );
			}
		}
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonPlaceBeacon"));
		if( win )
		{
			setControlCommand( win, findCommandButton( "NonCommand_Beacon" ) );
			win->winSetTooltipFunc( commandButtonTooltip );
		}
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonGeneral"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_GeneralsExperience") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonLarge"));
		if(win)
		{
			setControlCommand(win, findCommandButton("NonCommand_UpDown") );
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:PowerWindow"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:MoneyDisplay"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}
		win = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:GeneralsExp"));
		if(win)
		{
			win->winSetTooltipFunc(commandButtonTooltip);
		}

		// the radar's under-attack light flashes, as EA's did
		m_radarAttackGlowWindow = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey("ControlBar.wnd:WinUAttack"));


		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) ));
		win->winGetScreenPosition(&m_controlBarForegroundMarkerPos.x, &m_controlBarForegroundMarkerPos.y);
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:BackgroundMarker" ) ));
		win->winGetScreenPosition(&m_controlBarBackgroundMarkerPos.x,&m_controlBarBackgroundMarkerPos.y);

		if(!m_videoManager)
			m_videoManager = NEW WindowVideoManager;
		if(!m_animateWindowManager)
			m_animateWindowManager = NEW AnimateWindowManager;
		if(!m_generalsScreenAnimate)
			m_generalsScreenAnimate = NEW AnimateWindowManager;
		if(!m_animateWindowManagerForGenShortcuts)
			m_animateWindowManagerForGenShortcuts = NEW AnimateWindowManager;
		m_buildToolTipLayout = TheWindowManager->winCreateLayout( "ControlBarPopupDescription.wnd" );
		if(m_buildToolTipLayout)
		{
			m_buildToolTipLayout->hide(TRUE);
			m_buildToolTipLayout->setUpdate(ControlBarPopupDescriptionUpdateFunc);
		}
			
		m_genStarOn = TheMappedImageCollection ? (Image *)TheMappedImageCollection->findImageByName("BarButtonGenStarON") : NULL;
		m_genStarOff = TheMappedImageCollection ? (Image *)TheMappedImageCollection->findImageByName("BarButtonGenStarOFF") : NULL;
		m_genStarFlash = TRUE;
		m_lastFlashedAtPointValue = -1;

		m_rankVeteranIcon = TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron1L" ) : NULL;
		m_rankEliteIcon		= TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron2L" ) : NULL;
		m_rankHeroicIcon	= TheMappedImageCollection ? TheMappedImageCollection->findImageByName( "SSChevron3L" ) : NULL;


		


		// Initialize the Observer controls
		initObserverControls();

		// by default switch to the none context
		switchToContext( CB_CONTEXT_NONE, NULL );
	}

}  // end initWindows

//-------------------------------------------------------------------------------------------------
/** Reset the context sensitive control bar GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::reset( void )
{
	hideSpecialPowerShortcut();
	// do not destroy the rally drawable, it will get destroyed with everythign else during a reset
	m_rallyPointDrawableID = INVALID_DRAWABLE_ID;
	m_standInBuilderID = INVALID_DRAWABLE_ID;

	m_displayedConstructPercent = -1.0f;
	m_displayedOCLTimerSeconds = 0;

	m_isObserverCommandBar = FALSE; // reset us to use a normal command bar
	m_observerLookAtPlayer = NULL;
	m_watchedSelection = NULL;
	m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;	// a minimised bar is this match's, not the next one's

	if(m_buildToolTipLayout)
		m_buildToolTipLayout->hide(TRUE);
	m_showBuildToolTipLayout = FALSE;

	if(m_animateWindowManager)
		m_animateWindowManager->reset();

	if(m_animateWindowManagerForGenShortcuts)
		m_animateWindowManagerForGenShortcuts->reset();

	if(m_generalsScreenAnimate)
		m_generalsScreenAnimate->reset();


	if(m_videoManager)
		m_videoManager->reset();
	
	// go back to default context
	switchToContext( CB_CONTEXT_NONE, NULL );
	m_sideSelectAnimateDown = FALSE;
	if(m_animateDownWindow)
	{
		TheWindowManager->winDestroy( m_animateDownWindow );
		m_animateDownWindow = NULL;		
	}

	// the map's overrides go in TheCommandStore's reset; what the tick asked of the old match goes here
	s_logicCalls.clear();
	s_logicMarkedUIDirty = FALSE;

	if(TheTransitionHandler)
		TheTransitionHandler->remove("ControlBarArrow");
	m_genArrow = NULL;

	m_lastFlashedAtPointValue = -1;
	m_genStarFlash = TRUE;
}  // end reset

//-------------------------------------------------------------------------------------------------
/** Update phase, we can track if our selected object is destroyed, update button
	* percentages, status, enabled status etc */
//-------------------------------------------------------------------------------------------------
void ControlBar::update( void )
{
	//
	// This is driven by the client, once per RENDER frame, but nearly everything it recomputes -
	// button availability, build clocks, special power readiness, the general's star flash - is
	// keyed to the LOGIC frame and cannot change twice inside one tick.  With the renderer
	// uncapped that was several whole passes per tick for nothing.  Latch that work to the
	// logic frame.  The scheme/video/animation managers and the window runUpdate()s below really
	// are per-render and stay outside the latch, and so does anything the UI marks dirty, which
	// must still answer a selection change on the frame it happens.  See FINDINGS.md 7.4.
	//
	static UnsignedInt s_lastUpdateFrame = 0xffffffff;
	const UnsignedInt logicNow = TheGameLogic->getFrame();
	const Bool logicTick = (logicNow != s_lastUpdateFrame);
	s_lastUpdateFrame = logicNow;

	if( logicTick )
	{
		getStarImage();

		// EA's lamp: 150 frames, flipping every 15.  winEnable(FALSE) is the lit image
		if( m_radarAttackGlowWindow && m_remainingRadarAttackGlowFrames > 0 )
		{
			if( --m_remainingRadarAttackGlowFrames <= 0 )
				m_radarAttackGlowWindow->winEnable( TRUE );
			else if( m_remainingRadarAttackGlowFrames % ConvertRetailFrames( 15 ) == 0 )
				m_radarAttackGlowWindow->winEnable( !BitTest( m_radarAttackGlowWindow->winGetStatus(), WIN_STATUS_ENABLED ) );
		}
	}

	//
	// a general's power row expires on its own, so it cannot be waiting to eat a keystroke a minute
	// later, and it drops the moment the
	// bar it was picked against is off screen - hidden with the command bar, animated out, or
	// repopulated with fewer powers than the row names.  Otherwise a key pressed in a hurry and
	// then thought better of sits armed, and the next one fires a power instead of picking a row
	//
	if( m_specialPowerShortcutRow >= 0 )
	{
		const Int visibleRows = ( countVisibleSpecialPowerShortcuts() + SPECIAL_POWER_SHORTCUT_COLS - 1 )
														/ SPECIAL_POWER_SHORTCUT_COLS;
		if( Clock_Milliseconds() - m_specialPowerShortcutRowMs > CHORD_TIMEOUT_MS
				|| m_specialPowerShortcutParent == NULL
				|| m_specialPowerShortcutParent->winIsHidden()
				|| m_specialPowerShortcutRow >= visibleRows )
			clearSpecialPowerShortcutRow();
	}

	// the scheme lays the bar out before the map is loaded, so the radar takes the map's shape here
	const Region3D *radarExtent = TheRadar->getMapExtent();
	if( radarExtent->width() != m_radarFitWidth || radarExtent->height() != m_radarFitHeight )
		fitRadarToMap( FALSE );

	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->update();

	// Update our video manager
	if( m_videoManager )
		m_videoManager->update();

	if (m_animateWindowManager)
		m_animateWindowManager->update();

	if (m_animateWindowManager)
		{
			if (m_animateWindowManager->isFinished() && m_animateWindowManager->isReversed())
			{
				Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
				GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);
				if (window && !window->winIsHidden())
					window->winHide(TRUE);
			}
		}

	if(m_animateWindowManagerForGenShortcuts)
		m_animateWindowManagerForGenShortcuts->update();
	if (m_animateWindowManagerForGenShortcuts && m_specialPowerShortcutParent)
		{
			if (m_animateWindowManagerForGenShortcuts->isFinished() && m_animateWindowManagerForGenShortcuts->isReversed())
			{
				if (m_specialPowerShortcutParent && !m_specialPowerShortcutParent->winIsHidden())
					m_specialPowerShortcutParent->winHide(TRUE);
			}
		}



	// every other use of m_buildToolTipLayout is null-guarded; this one was not
	if( m_buildToolTipLayout && !m_buildToolTipLayout->isHidden())
	{
		m_buildToolTipLayout->runUpdate();
		m_showBuildToolTipLayout = FALSE;
	}
/*
	else if( m_buildToolTipLayout )
	{
		hideBuildTooltipLayout();
	}*/

	// walks every shortcut button and asks the player for its most ready special power object
	if( logicTick )
		updateSpecialPowerShortcut();
	rearrangeSpecialPowerShortcutGrid();
	// if we're an observer, don't do the complete update
	if( m_isObserverCommandBar)
	{
		// clicking a unit is clicking its owner's seat at the top: his seat, his side, the portrait
		updateWatchedPlayer();

		// twice a second is plenty for the observer readouts, and only on a real logic tick -
		// a bare "frame % n == 0" fires on every render frame that lands inside that one tick
		if( logicTick && (logicNow % (LOGICFRAMES_PER_SECOND/2)) == 0 )
			populateObserverInfoWindow();

		Drawable *drawToEvaluateFor = NULL;
		Bool multiSelect = FALSE;
		if( TheInGameUI->getSelectCount() > 1 )
		{
			// Attempt to isolate a Drawable here to evaluate
			// The need arises when selected is an AngryMob,
			// whose selection actually consists of varied units
			// but is represented in the UI as a single unit,
			// so we must isolate and evaluate only the Nexus 
			drawToEvaluateFor = TheGameClient->findDrawableByID( TheInGameUI->getSoloNexusSelectedDrawableID() ) ;
			multiSelect = ( drawToEvaluateFor == NULL );

		}  
		else // get the first and only drawble in the selection list
		{
			// none when nothing is selected, which is an observer's usual state: front() of an empty list
			// is undefined.  What it gave: libstdc++ and libc++ read their element count, 0; MSVC's Release
			// build reads the sentinel node's value slot, which is never written and was zeroed by the
			// game's operator new (allocateBytes), so NULL too; MSVC's Debug STL checks, reports and ends
			// the process.  NULL is the answer every Release build had: no drawable, no portrait.
			const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
			drawToEvaluateFor = selected->empty() ? NULL : selected->front();
		}
		Object *obj = drawToEvaluateFor ? drawToEvaluateFor->getObject() : NULL;
		setPortraitByObject( obj );

		// the observer never reaches evaluateContextUI, so this is what keeps the context drawable
		// (the HUD's name and health readout) on the selection; onDrawableDeselected drops it when
		// its drawable dies before the next pass gets here
		m_currentSelectedDrawable = drawToEvaluateFor;

		return;
	}
		

	const Bool flashTick = logicTick && (logicNow % ConvertRetailFrames( 10U ) == 0);

	// check flashing
	if( m_flash )
	{
		// go through all the command buttons to see which one needs to flash
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			GameWindow *button = m_commandWindows[ i ];
			if( button != NULL)
			{
				const CommandButton *commandButton = (const CommandButton *)GadgetButtonGetData(button);
				if( commandButton != NULL )
				{
					if( commandButton->getFlashCount() > 0 && flashTick )
					{
						if( commandButton->getFlashCount() % 2 == 0 )
						{
							commandButton->setFlashCount(commandButton->getFlashCount() - 1);
							button->winSetStatus( WIN_STATUS_FLASHING );
						}
						else
						{
							commandButton->setFlashCount(commandButton->getFlashCount() - 1);
							button->winClearStatus( WIN_STATUS_FLASHING );
							if( commandButton->getFlashCount() == 0 )
							{
								setFlash( FALSE );
							}
						}
					}
				}
			}
		}
	}
	
	if( isPurchaseScienceVisible() )
	{
		updateContextPurchaseScience();
		updatePurchaseScienceHotKeys();
	}
	else
		clearPurchaseScienceColumn();

	//
	// a stand-in builder is not selected, so no deselect event tells us when it dies, when a real
	// selection arrives or when BuildWithoutWorker is switched off; re-evaluate ourselves before
	// anything touches its drawable
	//
	if( m_standInBuilderID != INVALID_DRAWABLE_ID )
	{
		Drawable *standIn = TheGameClient->findDrawableByID( m_standInBuilderID );
		Object *standInObj = standIn ? standIn->getObject() : NULL;
		if( TheInGameUI->getSelectCount() > 0 || standInObj == NULL || standInObj->isEffectivelyDead()
				|| !TheGlobalData->m_buildWithoutWorker )
		{
			m_standInBuilderID = INVALID_DRAWABLE_ID;
			m_currentSelectedDrawable = NULL;
			markUIDirty();
		}
	}
	//
	// nothing selected and nothing standing in: a builder that is made or bought later, or the
	// option switched on, gets the bar, which no selection event would announce either
	//
	else if( logicTick && ( logicNow % LOGICFRAMES_PER_SECOND ) == 0 && TheInGameUI->getSelectCount() == 0
					 && m_currentSelectedDrawable == NULL && findStandInBuilder( FALSE ) )
	{
		markUIDirty();
	}

	//
	// first, if the UI is dirty repopulate the UI with what the user should see for all the
	// selected drawables
	//
	const Bool wasUIDirty = m_UIDirty;
	if( m_UIDirty )
	{
		evaluateContextUI();
		populateSpecialPowerShortcut(ThePlayerList->getLocalPlayer());
		// if we have a build tooltip layout, update it with the new data.
		repopulateBuildTooltipLayout(); 
	}

	// nothing below here can change without either a logic tick or a rebuilt UI, and the rest
	// of it is the expensive half: a full object count for the beacon cap and then a pass over
	// every command button in the current context
	if( !logicTick && !wasUIDirty )
		return;

	// enable/disable the beacon button depending on if the max has been reached
	if (ThePlayerList && ThePlayerList->getLocalPlayer() && ThePlayerList->getLocalPlayer()->getPlayerTemplate())
	{
		Int count;
		const ThingTemplate *thing = TheThingFactory->findTemplate( ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getBeaconTemplate() );
		ThePlayerList->getLocalPlayer()->countObjectsByThingTemplate( 1, &thing, false, &count );
		static NameKeyType beaconPlacementButtonID = NAMEKEY("ControlBar.wnd:ButtonPlaceBeacon");
		GameWindow *win = TheWindowManager->winGetWindowFromId(NULL, beaconPlacementButtonID);
		if (win)
		{
			if (count < TheMultiplayerSettings->getMaxBeaconsPerPlayer())
			{
				win->winEnable(TRUE);
			}
			else
			{
				win->winEnable(FALSE);
			}
		}
	}

	//
	// most control bar contexts have one selected thing that we switch on and update
	// based on if that thing changed in some way ... the exception is when multi selected
	//
	if( m_currContext == CB_CONTEXT_MULTI_SELECT )
	{

		updateContextMultiSelect();
		return;

	}  // end if

	// if nothing is selected get out of here except if we're in the Purchase science context... that requires
	// us to not have anything selected
	if( m_currentSelectedDrawable == NULL )
	{

		// we better be in the default none context
		DEBUG_ASSERTCRASH( m_currContext == CB_CONTEXT_NONE, ("ControlBar::update no selection, but not we're not showing the default NONE context\n") );
		return;

	}  // end if
	
	

	// if our selected drawable has no object get out of here
	Object *obj = NULL;
	if(m_currentSelectedDrawable)
		obj = m_currentSelectedDrawable->getObject();
	if( obj == NULL )
	{

		switchToContext( CB_CONTEXT_NONE, NULL );
		return;

	}  // end if

	switch( m_currContext )
	{

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_NONE:  
			break;

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_COMMAND:
			updateContextCommand();
			break;

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_STRUCTURE_INVENTORY:
			updateContextStructureInventory();
			break;

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_BEACON:
			updateContextBeacon();
			break;

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_UNDER_CONSTRUCTION:
			updateContextUnderConstruction();
			break;

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_OCL_TIMER:
			updateContextOCLTimer();
			break;

	}  // end switch



}  // end update

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onDrawableSelected( Drawable *draw )
{

	// set a dirty flag so next time we update we can reconstruct the UI
	markUIDirty();

	// cancel any pending GUI commands
	TheInGameUI->setGUICommand( NULL );


}  // end onDrawableSelected

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onDrawableDeselected( Drawable *draw )
{

	// set a dirty flag so next time we update we can reconstruct the UI
	markUIDirty();

	// a dying drawable is deselected and then freed inside the logic frame, and the next client
	// pass draws the HUD (InGameUI's getContextDrawable) before update can re-evaluate - drop it now
	if( m_currentSelectedDrawable == draw )
		m_currentSelectedDrawable = NULL;

	if (TheInGameUI->getSelectCount() == 0)
	{
		// we just deselected everything - cancel any pending GUI commands
		TheInGameUI->setGUICommand( NULL );
	}

	//
	// always when becoming unselected should we remove any build placement icons because if
	// we have some and are in the middle of a build process, it must obiously be over now
	// because we are no longer selecting the dozer or worker
	//
	TheInGameUI->placeBuildAvailable( NULL, NULL );

}  // end onDrawableDeselected

//-------------------------------------------------------------------------------------------------

const Image *ControlBar::getStarImage(void )
{
	if(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints() || ThePlayerList->getLocalPlayer()->getSciencePurchasePoints() <= 0)
		m_genStarFlash = FALSE;
	else
		m_lastFlashedAtPointValue = ThePlayerList->getLocalPlayer()->getSciencePurchasePoints();
	
	GameWindow *win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonGeneral" ) );
	if(!win)
		return NULL;
	if(!m_genStarFlash)
	{
		GadgetButtonSetEnabledImage(win, m_generalButtonEnable);
		return NULL;
	}

	if(TheGameLogic->getFrame()% LOGICFRAMES_PER_SECOND > LOGICFRAMES_PER_SECOND/2)
	{
		GadgetButtonSetEnabledImage(win, m_generalButtonHighlight);
		return NULL;
	}

	GadgetButtonSetEnabledImage(win, m_generalButtonEnable);

	return NULL;

}


//-------------------------------------------------------------------------------------------------
void ControlBar::onPlayerRankChanged(const Player *p)
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if (!p->isLocalPlayer())
		return;

	if(!(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints()))
	{
		if(TheTransitionHandler && TheInGameUI->getInputEnabled())
			TheTransitionHandler->setGroup("ControlBarArrow");
	}
//	populateSpecialPowerShortcut((Player *)p);
	m_genStarFlash = TRUE;
	/// @todo implement me
	markUIDirty();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::onPlayerSciencePurchasePointsChanged(const Player *p)
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if (!p->isLocalPlayer())
		return;
	if(!(m_lastFlashedAtPointValue > ThePlayerList->getLocalPlayer()->getSciencePurchasePoints()))
	{
		if(TheTransitionHandler && TheInGameUI->getInputEnabled())
			TheTransitionHandler->setGroup("ControlBarArrow");
	}
//	populateSpecialPowerShortcut((Player *)p);
	m_genStarFlash = TRUE;
	/// @todo implement me
	markUIDirty();
}

//-------------------------------------------------------------------------------------------------
/** Given the drawables that we have selected into our context sensitive UI, evaluate 
	* and perform all UI manipulations to make the GUI show to the user what we want them
	* to see */
//-------------------------------------------------------------------------------------------------
void ControlBar::evaluateContextUI( void )
{

	//
	// the UI has been "evaluated" and is now displaying the most current and correct
	// information to the player
	//
	m_UIDirty = FALSE;

	// if our purchase science window is up, we will want to update it by repopulating it.
	if( isPurchaseScienceVisible() )
		populatePurchaseScience( getWatchedPlayer() );

	// erase any current state of the GUI by switching out to the empty context
	switchToContext( CB_CONTEXT_NONE, NULL );

	//
	// nothing selected: with BuildWithoutWorker on, one of the player's builders stands in and its
	// command bar shows, so a structure can be placed without selecting a dozer first - the logic
	// then sends the idle builder nearest the site (MSG_DOZER_CONSTRUCT).  Off, an empty bar, as
	// 1.04 had it.  m_standInBuilderID is not cleared first: findStandInBuilder reads it to keep
	// the builder it is already showing.
	//
	if( TheInGameUI->getSelectCount() == 0 && !m_isObserverCommandBar )
	{
		Drawable *builder = findStandInBuilder( FALSE );
		m_standInBuilderID = builder ? builder->getID() : INVALID_DRAWABLE_ID;
		if( builder )
			switchToContext( CB_CONTEXT_COMMAND, builder );
		return;
	}

	m_standInBuilderID = INVALID_DRAWABLE_ID;

	// get the list of drawable IDs from the in game UI
	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();

	// sanity
	if( selectedDrawables->empty() == TRUE )
		return;

	//Make sure the selected objects are in fact, controllable! If not, then
	//we don't show any GUI commands for them!!!
	//This is used when we select enemy objects or objects on another team.
	//@todo we may want to show their portrait
	if( !TheInGameUI->areSelectedObjectsControllable() )
	{
		//Also make sure the unit isn't a garrisonable neutral civ team building!
		Drawable *draw = selectedDrawables->front();

		//sanity 
		if( !draw )
		{
			return;
		}
		Object *obj = draw->getObject();
		if( !obj )
		{
			return;
		}
		
		if (obj->getControllingPlayer()
			&& obj->getControllingPlayer()->getPlayerTemplate()
			&& obj->getControllingPlayer()->getPlayerTemplate()->getBeaconTemplate().compare(obj->getTemplate()->getName()) == 0
			)
		{
			switchToContext( CB_CONTEXT_BEACON, draw );
		}
		else
		{
			switchToContext( CB_CONTEXT_NONE, draw );
		}

		//Check for a contain interface and a enemy relationship and reject that!
		ContainModuleInterface *contain = obj->getContain();
		if( contain && contain->getContainMax() > 0 )
		{

			const Player *otherPlayer = contain->getApparentControllingPlayer(ThePlayerList->getLocalPlayer());
			if (!otherPlayer)
				otherPlayer = obj->getControllingPlayer();
			Player *player = ThePlayerList->getLocalPlayer();

			if( !player || !otherPlayer )
			{
				//Sanity.
				return;
			}
			Relationship relation = player->getRelationship( otherPlayer->getDefaultTeam() );

			//Note: All following checks already account for the fact that this object
			//isn't ours.

			//The only case we can actually see a non-controlled controlbar is a neutral garrisonable structure.
			if( !contain->isGarrisonable() || relation != NEUTRAL )
			{
				//Can't peek inside enemy/allied containers period!
				return;
			}
		}
		else
		{
			return;
		}
	}

	//
	// when we have multiple things selected, we will only display the common commands
	// in the center command bar that can be displayed with multi-units selected
	//


	Drawable *drawToEvaluateFor = NULL;
	Bool multiSelect = FALSE;


	if( TheInGameUI->getSelectCount() > 1 )
	{
		// Attempt to isolate a Drawable here to evaluate
		// The need arises when selected is an AngryMob,
		// whose selection actually consists of varied units
		// but is represented in the UI as a single unit,
		// so we must isolate and evaluate only the Nexus 
		drawToEvaluateFor = TheGameClient->findDrawableByID( TheInGameUI->getSoloNexusSelectedDrawableID() ) ;
		multiSelect = ( drawToEvaluateFor == NULL );

	}  
	else // get the first and only drawble in the selection list
		drawToEvaluateFor = selectedDrawables->front();
	


	if( multiSelect )
	{
		switchToContext( CB_CONTEXT_MULTI_SELECT, NULL );
	}  
	else if ( drawToEvaluateFor )// either we have exactly one drawable, or we have isolated one to evaluate for...
	{

		// get the first and only drawble in the selection list
		//Drawable *draw = selectedDrawables->front();

		// sanity
		//if( draw == NULL )
		//	return;

		// get object
		Object *obj = drawToEvaluateFor->getObject();
		if( obj == NULL )
			return;

		// we show no interface for objects being sold
		if( obj->getStatusBits().test( OBJECT_STATUS_SOLD ) )
			return;

		static const NameKeyType key_OCLUpdate = NAMEKEY( "OCLUpdate" );
		OCLUpdate *update = (OCLUpdate*)obj->findUpdateModule( key_OCLUpdate );
	
		//
		// a command center is context sensitive itself, if a side has *NOT* been chosen we display
		// the side select interface for command centers only, but note how under construction is
		// more important than anything
		//
		Bool contextSelected = FALSE;
		if( obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{

			switchToContext( CB_CONTEXT_UNDER_CONSTRUCTION, drawToEvaluateFor );
			contextSelected = TRUE;

		}  // end else if

		// check for a regular switch to the appropriate context
		if( contextSelected == FALSE )
		{
			ContainModuleInterface *cmi = obj->getContain();

			if( cmi && cmi->isGarrisonable() && obj->getCommandSetString().isEmpty() )
			{
				//Kris: This is a convenient section to graft an inventory commandset for
				//garrisoned troops. However, we only want to use this if we DON'T have
				//a commandset defined. If we do, then trust that the commandset will
				//handle it!

				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Relationship relationship;

				// we cannot select objects that are controlled by our enemies
				relationship = localPlayer->getRelationship( obj->getTeam() );
				if( obj->isLocallyControlled() == TRUE || relationship == NEUTRAL )
					switchToContext( CB_CONTEXT_STRUCTURE_INVENTORY, drawToEvaluateFor );

			}  // end else if
			else if( update )
			{
				switchToContext( CB_CONTEXT_OCL_TIMER, drawToEvaluateFor );
			}
			else if( obj->getCommandSetString().isEmpty() == FALSE )
			{

				switchToContext( CB_CONTEXT_COMMAND, drawToEvaluateFor );

			}  // end else if
			else if (obj->getControllingPlayer()->getPlayerTemplate()->getBeaconTemplate().compare(obj->getTemplate()->getName()) == 0)
			{
				switchToContext( CB_CONTEXT_BEACON, drawToEvaluateFor );
			}
			else 
				switchToContext( CB_CONTEXT_NONE, drawToEvaluateFor );
		}  // end else

	}  // end else	

}  // end evaluateContextUI

//-------------------------------------------------------------------------------------------------
/** The client's way to TheCommandStore.  The census hook stays, so a logic site still asking the
	bar shows up from the tick. */
//-------------------------------------------------------------------------------------------------
const CommandButton *ControlBar::findCommandButton( const AsciiString& name )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	return TheCommandStore->findCommandButton( name );
}

//-------------------------------------------------------------------------------------------------
const CommandSet *ControlBar::findCommandSet( const AsciiString& name )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	return TheCommandStore->findCommandSet( name );
}

//-------------------------------------------------------------------------------------------------
/** How many units this click on a build button is worth.  Three rungs, taken from Beyond All
	Reason's build menu, which is the only RTS that treats the batch size as a first class thing:
	shift is a squad, control is a queue, both together is everything the bank will pay for.  The
	same number runs the right button, so whatever a click queued, one right-click of the same
	shape takes back out. */
//-------------------------------------------------------------------------------------------------
Int getBuildBatchCount( void )
{
	if( TheKeyboard->isCtrl() )
		return TheKeyboard->isShift() ? CTRL_SHIFT_BUILD_QUEUE_COUNT : CTRL_BUILD_QUEUE_COUNT;

	return TheKeyboard->isShift() ? SHIFT_BUILD_QUEUE_COUNT : 1;

}  // end getBuildBatchCount

//-------------------------------------------------------------------------------------------------
/** Process a button click for the context sensitive GUI */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processContextSensitiveButtonClick( GameWindow *button,
																																GadgetGameMessage gadgetMessage )
{

	//
	// clicking anything on the bar ends a half-typed general's power row - the player has clearly
	// moved on, and the keys go back to picking rows instead of one of them firing a power.  The
	// keyboard path spends its own row before it sends this, so it is already clear by here
	//
	clearSpecialPowerShortcutRow();

	//
	// The right button only ever takes things back out of a queue - the build queue panel no
	// longer sits in the right HUD to click on. It never runs the command: firing a build or a
	// special power off the right button would make every mis-click on the bar expensive.
	//
	if( gadgetMessage == GBM_SELECTED_RIGHT )
	{
		const CommandButton *command = (const CommandButton *)GadgetButtonGetData( button );
		if( command == NULL )
			return CBC_COMMAND_NOT_USED;

		if( command->getCommandType() == GUI_COMMAND_UNIT_BUILD )
		{
			// a modifier takes the same batch back out that it would have queued
			Int wanted = getBuildBatchCount();
			while( wanted-- > 0 && cancelLastQueuedUnit( command->getThingTemplate() ) )
				;
			return CBC_COMMAND_USED;
		}

		if( command->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
				command->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE )
		{
			cancelQueuedUpgrade( command->getUpgradeTemplate() );
			return CBC_COMMAND_USED;
		}

		return CBC_COMMAND_NOT_USED;
	}

	// call command processing method
	return processCommandUI( button, gadgetMessage );

}  // end processContextSensitiveButtonClick

//-------------------------------------------------------------------------------------------------
/** Take 'upgrade' back out of the queue of whichever selected object is researching it. An upgrade
	* is only ever queued once per building, so unlike a unit there is no "last one" to pick. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::cancelQueuedUpgrade( const UpgradeTemplate *upgrade )
{
	if( upgrade == NULL )
		return FALSE;

	Bool cancelled = FALSE;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *candidate = (*it)->getObject();
		if( candidate == NULL || candidate->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
			continue;

		ProductionUpdateInterface *pu = candidate->getProductionUpdateInterface();
		if( pu == NULL || pu->isUpgradeInQueue( upgrade ) == FALSE )
			continue;

		// the producer travels with the message, as it does for a cancelled unit
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CANCEL_UPGRADE );
		msg->appendIntegerArgument( upgrade->getUpgradeNameKey() );
		msg->appendObjectIDArgument( candidate->getID() );
		cancelled = TRUE;
		// an object upgrade is bought per building and one click buys it for the whole selection,
		// so one click takes it back off the whole selection too - keep going
	}

	return cancelled;

}  // end cancelQueuedUpgrade

//-------------------------------------------------------------------------------------------------
/** Cancel the last queued production of 'thing' on the representative producer */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::cancelLastQueuedUnit( const ThingTemplate *thing )
{
	if( thing == NULL )
		return FALSE;

	// with several producers selected there is no one queue on the bar to take from, and the
	// buttons show no queue either, so there is nothing here to cancel
	if( m_currContext == CB_CONTEXT_MULTI_SELECT )
		return FALSE;

	//
	// Look across every selected producer, not just the representative one. A shift-click spreads
	// a batch over all the selected factories, so cancelling only ever on the representative left
	// the units queued on the others unreachable from the bar.
	//
	Object *producer = NULL;
	ProductionUpdateInterface *pu = NULL;
	ProductionID last = PRODUCTIONID_INVALID;

	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *candidate = (*it)->getObject();
		if( candidate == NULL || candidate->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
			continue;

		ProductionUpdateInterface *cpu = candidate->getProductionUpdateInterface();
		if( cpu == NULL )
			continue;

		ProductionID candidateLast = PRODUCTIONID_INVALID;
		for( const ProductionEntry *p = cpu->firstProduction(); p; p = cpu->nextProduction( p ) )
			if( p->getProductionType() == PRODUCTION_UNIT && p->getProductionObject() == thing )
				candidateLast = p->getProductionID();

		if( candidateLast == PRODUCTIONID_INVALID )
			continue;

		// prefer the one the bar is actually showing, otherwise take the first that has any
		producer = candidate;
		pu = cpu;
		last = candidateLast;
		if( m_currentSelectedDrawable && candidate == m_currentSelectedDrawable->getObject() )
			break;
	}

	if( pu == NULL || last == PRODUCTIONID_INVALID )
		return FALSE;

	// the producer travels with the message: in a multi-selection the logic cannot tell
	// which selected object the queue belongs to otherwise
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CANCEL_UNIT_CREATE );
	msg->appendIntegerArgument( last );
	msg->appendObjectIDArgument( producer->getID() );
	return TRUE;

}  // end cancelLastQueuedUnit

//-------------------------------------------------------------------------------------------------
/** The local player's builder that stands in for an empty selection: an idle one if there is
	* one, else any live one.  (Player::iterateObjects callback + driver.) */
//-------------------------------------------------------------------------------------------------
struct StandInBuilderSearch
{
	Object *idle;
	Object *any;
};

static void findStandInBuilderProc( Object *obj, void *userData )
{
	StandInBuilderSearch *s = (StandInBuilderSearch *)userData;
	if( obj->isEffectivelyDead() || obj->getDrawable() == NULL )
		return;
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	DozerAIInterface *dozer = ai ? ai->getDozerAIInterface() : NULL;
	if( dozer == NULL )
		return;
	if( s->any == NULL )
		s->any = obj;
	// free = no build/repair task and not hauling supplies; walking somewhere does not count
	const SupplyTruckAIInterface *supply = ai->getSupplyTruckAIInterface();
	if( s->idle == NULL && !dozer->isAnyTaskPending() && !( supply && supply->isCurrentlyFerryingSupplies() ) )
		s->idle = obj;
}

Drawable *ControlBar::findStandInBuilder( Bool freeOnly )
{
	// off, the bar is 1.04's: nothing selected is an empty bar, and a build needs a selected worker
	if( !TheGlobalData->m_buildWithoutWorker )
		return NULL;

	Player *player = ThePlayerList->getLocalPlayer();
	if( player == NULL )
		return NULL;

	//
	// The builder already standing in keeps the bar for as long as it is still a builder.  The
	// sweep below answers "the first idle one", and idleness changes on its own: a dozer somewhere
	// across the base finishing a building goes idle and used to take the bar off the one you were
	// working with.  That drops the page you were on and takes the structure off your cursor, in
	// the middle of placing it, because something happened somewhere else.  For a GLA worker it
	// also keeps the fake-structure page it was switched to.
	//
	if( m_standInBuilderID != INVALID_DRAWABLE_ID )
	{
		Drawable *held = TheGameClient->findDrawableByID( m_standInBuilderID );
		Object *obj = held ? held->getObject() : NULL;
		if( obj && obj->getControllingPlayer() == player )
		{
			StandInBuilderSearch check;
			check.idle = NULL;
			check.any = NULL;
			findStandInBuilderProc( obj, &check );
			if( check.any && ( !freeOnly || check.idle ) )
				return held;
		}
	}

	StandInBuilderSearch s;
	s.idle = NULL;
	s.any = NULL;
	player->iterateObjects( findStandInBuilderProc, &s );

	Object *pick = s.idle ? s.idle : ( freeOnly ? NULL : s.any );
	return pick ? pick->getDrawable() : NULL;

}  // end findStandInBuilder

//-------------------------------------------------------------------------------------------------
/** Press the index'th general's power shortcut button, as a mouse click would */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::clearSpecialPowerShortcutRow( void )
{
	if( m_specialPowerShortcutRow < 0 )
		return FALSE;

	m_specialPowerShortcutRow = -1;
	m_specialPowerShortcutRowMs = 0;
	return TRUE;

}  // end clearSpecialPowerShortcutRow

//-------------------------------------------------------------------------------------------------
/** How many of the slots carry a power right now.  populateSpecialPowerShortcut() fills them from
	* the corner and hides the rest, so the ones in use are always the first n - and n is not
	* m_currentlyUsedSpecialPowersButtons, which is how many the general's command set holds in all. */
//-------------------------------------------------------------------------------------------------
Int ControlBar::countVisibleSpecialPowerShortcuts( void )
{
	Int count = 0;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons && i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		GameWindow *win = m_specialPowerShortcutButtons[ i ];
		if( win == NULL || win->winIsHidden() )
			break;
		count++;
	}
	return count;

}  // end countVisibleSpecialPowerShortcuts

//-------------------------------------------------------------------------------------------------
/** Which tray slot a press on shortcut key 'index' lands on with the row chord as it stands, or
	SLOT_ARMS_CHORD for a press that names a row, or SLOT_NOTHING.  Changes nothing. */
//-------------------------------------------------------------------------------------------------
Int ControlBar::resolveTrayPress( Int index, Int armedRow, Int visibleSlots )
{
	if( index < 0 )
		return SLOT_NOTHING;

	//
	// what the keys can reach is what is on screen, not the size of the command set the general
	// could eventually fill: a USA player with three powers has one row, and arming the second
	// one would swallow the key after it with nothing to spend it on
	//
	const Int rows = ( visibleSlots + SPECIAL_POWER_SHORTCUT_COLS - 1 ) / SPECIAL_POWER_SHORTCUT_COLS;

	// nothing pending: this press names a row and stops there
	if( armedRow < 0 )
		return index < rows ? SLOT_ARMS_CHORD : SLOT_NOTHING;

	// a row is pending: this press names the power in it
	const Int slot = armedRow * SPECIAL_POWER_SHORTCUT_COLS + index;
	if( index >= SPECIAL_POWER_SHORTCUT_COLS || slot >= visibleSlots )
		return SLOT_NOTHING;

	return slot;

}  // end resolveTrayPress

//-------------------------------------------------------------------------------------------------
Int ControlBar::resolveSpecialPowerShortcutSlot( Int index )
{
	if( m_specialPowerShortcutParent == NULL || m_specialPowerShortcutParent->winIsHidden() )
		return SLOT_NOTHING;

	const Int slot = resolveTrayPress( index, m_specialPowerShortcutRow, countVisibleSpecialPowerShortcuts() );
	if( slot < 0 )
		return slot;

	GameWindow *win = m_specialPowerShortcutButtons[ slot ];
	return ( win == NULL || win->winIsHidden() ) ? SLOT_NOTHING : slot;

}  // end resolveSpecialPowerShortcutSlot

//-------------------------------------------------------------------------------------------------
ControlBar::PressOutcome ControlBar::peekSpecialPowerShortcutPress( Int index, GameWindow **button )
{
	*button = NULL;

	const Int slot = resolveSpecialPowerShortcutSlot( index );
	if( slot == SLOT_NOTHING )
		return PRESS_DOES_NOTHING;

	if( slot == SLOT_ARMS_CHORD )
	{
		// the first power in the row this key would name that the second key could fire
		const Int visible = countVisibleSpecialPowerShortcuts();
		const Int first = index * SPECIAL_POWER_SHORTCUT_COLS;
		for( Int i = first; i < first + SPECIAL_POWER_SHORTCUT_COLS && i < visible; i++ )
		{
			GameWindow *w = m_specialPowerShortcutButtons[ i ];
			if( w && !w->winIsHidden() && BitTest( w->winGetStatus(), WIN_STATUS_ENABLED ) )
			{
				*button = w;
				break;
			}
		}
		return PRESS_ARMS_CHORD;
	}

	*button = m_specialPowerShortcutButtons[ slot ];
	return BitTest( (*button)->winGetStatus(), WIN_STATUS_ENABLED ) ? PRESS_FIRES : PRESS_IS_REFUSED;

}  // end peekSpecialPowerShortcutPress

//-------------------------------------------------------------------------------------------------
/** One key cannot reach eleven powers laid out three to a row, so it takes two: the first press
	* picks the row - a group of three, standing as a column: F1 the one against the console, F2 the
	* one right of it - and the second picks the power in it, F1 being the bottom one.  Until a row is picked only the head of each row is labelled,
	* with the key that picks that row; once one is, the labels move onto its three powers. */
//-------------------------------------------------------------------------------------------------
void ControlBar::pressSpecialPowerShortcut( Int index )
{
	// a press that cannot reach the tray at all leaves a pending row alone
	if( index < 0 || m_specialPowerShortcutParent == NULL || m_specialPowerShortcutParent->winIsHidden() )
		return;

	const Int slot = resolveSpecialPowerShortcutSlot( index );

	if( slot == SLOT_ARMS_CHORD )
	{
		m_specialPowerShortcutRow = index;
		m_specialPowerShortcutRowMs = Clock_Milliseconds();
		return;
	}

	//
	// a row that was pending is spent by this press either way - a key that names no power in
	// that row simply cancels
	//
	clearSpecialPowerShortcutRow();
	if( slot == SLOT_NOTHING )
		return;

	GameWindow *win = m_specialPowerShortcutButtons[ slot ];

	if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
	{
		// the bar's parent runs ControlBarSystem, the same callback a real click reaches
		TheWindowManager->winSendSystemMsg( m_specialPowerShortcutParent, GBM_SELECTED,
																				(WindowMsgData)win, win->winGetWindowId() );
		AudioEventRTS buttonClick( "GUIGenShortcutClick" );
		if( TheAudio )
			TheAudio->addAudioEvent( &buttonClick );
	}
	else
	{
		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
			TheAudio->addAudioEvent( &disabledClick );
	}

}  // end pressSpecialPowerShortcut

//-------------------------------------------------------------------------------------------------
/** Process a button click for the context sensitive GUI */
//-------------------------------------------------------------------------------------------------
CBCommandStatus ControlBar::processContextSensitiveButtonTransition( GameWindow *button, 
																																GadgetGameMessage gadgetMessage )
{

	// call command processing method
	return processCommandTransitionUI( button, gadgetMessage );

}  // end processContextSensitiveButtonClick


//-------------------------------------------------------------------------------------------------
/** Switch the user interface to the new context specified and fill out any of the
	* art and/or buttons that we need to for the new context using data from the object
	* passed in */
//-------------------------------------------------------------------------------------------------
void ControlBar::switchToContext( ControlBarContext context, Drawable *draw )
{

	// restore the right hud to a plain window
	setPortraitByObject( NULL );

	Object *obj = draw ? draw->getObject() : NULL;
	setPortraitByObject( obj );

	// if we're switching context, we have to repopulate the hotkey manager
	if(TheHotKeyManager)
		TheHotKeyManager->reset();

	// ... and also remove any radius cursor that is active.
	TheInGameUI->setRadiusCursorNone();

	// save a pointer for the currently selected drawable
	m_currentSelectedDrawable = draw;

	if (IsInGameChatActive() == FALSE && TheGameLogic && !TheGameLogic->isInShellGame()) {
		TheWindowManager->winSetFocus( NULL );
	}

	// hide/un-hide the appropriate windows for the context
	switch( context )
	{

		//-------------------------------------------------------------------------------------------------
		case CB_CONTEXT_NONE:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			//Clear any potentially flashing buttons!
			for( int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			{
				// the implementation won't necessarily use the max number of windows possible
				if (m_commandWindows[ i ]) 
				{
					m_commandWindows[ i ]->winClearStatus( WIN_STATUS_FLASHING );
				}
			}
			// if there is a current selected drawable then we wil display a selection portrait if present
			if( draw )
			{
				//Get the current thing template.
				const ThingTemplate *thing = draw->getTemplate();

				//Special case -- if we are a GLA hole, then get the rebuild building template
				Object *obj = draw->getObject();
				if( obj && obj->isKindOf( KINDOF_REBUILD_HOLE ) )
				{
					RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
					if( rhbi )
					{
						thing = rhbi->getRebuildTemplate();
					}
				}

				//Set the correct portrait.
				setPortraitByObject( obj );
			}

			// do not show any rally point marker
			showRallyPoint( NULL );
			
			break;

		}  // end none

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_COMMAND:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );
			
			// fill the specific UI info
			populateCommand( draw->getObject() );

			// the selection portrait; the build queue panel never takes the right HUD over
			if( obj )
				setPortraitByObject( obj );

			break;

		}  // end command

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_STRUCTURE_INVENTORY:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateStructureInventory( draw->getObject() );

			break;

		}  // end inventory

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_BEACON:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( FALSE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			
			// fill the specific UI info			
			populateBeacon( draw->getObject() );

			break;

		}  // end beacon

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_UNDER_CONSTRUCTION:
		{

			// show or hide the right window groups
			//m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( FALSE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateUnderConstruction( draw->getObject() );

			break;

		}  // end under construction

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_OCL_TIMER:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( FALSE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );

			// fill the specific UI info
			populateOCLTimer( draw->getObject() );

			break;

		}  // end under construction

		//---------------------------------------------------------------------------------------------
		case CB_CONTEXT_MULTI_SELECT:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( FALSE );		// multi select shows common commands
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( TRUE );


			// fill the specific UI info
			populateMultiSelect();

			break;

		}  // end multi select
		case CB_CONTEXT_OBSERVER_LIST:
		{

			// show or hide the right window groups
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
			m_contextParent[ CP_COMMAND ]->winHide( TRUE );
			m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
			m_contextParent[ CP_BEACON ]->winHide( TRUE );
			m_contextParent[ CP_UNDER_CONSTRUCTION ]->winHide( TRUE );
			m_contextParent[ CP_OCL_TIMER ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_INFO ]->winHide( TRUE );
			m_contextParent[ CP_OBSERVER_LIST ]->winHide( FALSE );


			// fill the specific UI info
			populateObserverList();
			break;

		}  // end multi select

		//---------------------------------------------------------------------------------------------
		default:
		{

			DEBUG_ASSERTCRASH( 0, ("ControlBar::switchToContext, unknown context '%d'\n", context) );
			break;

		}  // end default

	}  // end switch

	// save our context
	m_currContext = context;

}  // end switchToContext

void ControlBar::setCommandBarBorder( GameWindow *button, CommandButtonMappedBorderType type)
{
	if(!button)
		return;

	switch( type )
	{
		case COMMAND_BUTTON_BORDER_BUILD:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderBuildColor);
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_UPGRADE:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderUpgradeColor );
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_ACTION:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderActionColor);
			break;
		}
		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_SYSTEM:
		{
			GadgetButtonSetBorder(button, m_commandButtonBorderSystemColor);
			break;
		}

		//-------------------------------------------------------------------------------------------------
		case COMMAND_BUTTON_BORDER_NONE:
		default:
			GadgetButtonSetBorder(button, GAME_COLOR_UNDEFINED, FALSE);
	}
}


//-------------------------------------------------------------------------------------------------
/** Set the command data into the control */
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
void ControlBar::setControlCommand( GameWindow *button, const CommandButton *commandButton )
{

	// the window must be a gadget button
	if( button->winGetInputFunc() != GadgetPushButtonInput )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: Window is not a button\n") );
		return;

	}  // end if

	// sanity
	if( commandButton == NULL )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: NULL commandButton passed in\n") );
		return;

	}  // end if

	//
	// set the button gadget control to be a normal button or a check like button if
	// the command says it needs one
	//
	if( BitTest( commandButton->getOptions(), CHECK_LIKE ))
		GadgetButtonEnableCheckLike( button, TRUE, FALSE );
	else
		GadgetButtonEnableCheckLike( button, FALSE, FALSE );

	//
	// set the imagry ... note that for 99% of the command buttons it's sufficient to specify
	// only the disabled, enabled, hilite, and hilite pushed images.  For push-like buttons
	// we actually utilize all the state available to a GameWindow.  We will replicate the
	// hilite pushed image to be the enabled pushed image ... and we will also replicate
	// the disabled image to be the disabled pushed image.  For complete control over all
	// the states of these buttons we would add additional lines to the INI for a command
	// button and store those additional images in the command button 
	//
	if( commandButton->getButtonImage() )
		GadgetButtonSetEnabledImage( button, commandButton->getButtonImage() );

	//if( commandButton->getDisabledImage() )
	//{
	//	GadgetButtonSetDisabledImage( button, commandButton->getDisabledImage() );
	//	GadgetButtonSetDisabledSelectedImage( button, commandButton->getDisabledImage() );
	//}  //end if
	//if( commandButton->getHiliteImage() )
	//	GadgetButtonSetHiliteImage( button, commandButton->getHiliteImage() );
	//if( commandButton->getPushedImage() )
	//{
	//	GadgetButtonSetHiliteSelectedImage( button, commandButton->getPushedImage() );
	//	GadgetButtonSetEnabledSelectedImage( button, commandButton->getPushedImage() );
	//}  // end if

	// set the text
	if( commandButton->getTextLabel().isEmpty() == FALSE || !commandButton->getScienceVec().empty()) 
	{
		button->winSetTooltipFunc(commandButtonTooltip);
	}
	else
	{
		// a button with no label has no build tooltip either - clear the func, the window is
		// recycled and would otherwise keep the one the previous occupant installed.
		button->winSetTooltipFunc( NULL );
		GadgetButtonSetText( button, UnicodeString( u"" ) );
	}

	// save the command in the user data of the window
	GadgetButtonSetData(button, (void*)commandButton);
	//button->winSetUserData( commandButton );

	// a recycled button must not keep the previous occupant's count badge
	GadgetButtonSetCount( button, 0 );

	setCommandBarBorder(button, commandButton->getCommandButtonMappedBorderType());
	
	// A builder standing in for an empty selection lends the keys its buildings and nothing else.
	// Nobody selected it, so its orders keep no letter: H with nothing selected is the command map's
	// (view the command center), not the GLA worker's switch to fake buildings, and S stops nothing.
	// Its buildings give up a letter the command map uses bare too (issue 128): with the grid on they
	// sit on Q and W, and with nothing selected those still select everything and every aircraft.
	// the command bar window slot the button sits in, -1 off the bar
	Int barSlot = -1;
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
		if( m_commandWindows[ slot ] == button )
			barSlot = slot;
	const Bool standIn = barSlot >= 0 && m_standInBuilderID != INVALID_DRAWABLE_ID && m_currentSelectedDrawable
		&& m_currentSelectedDrawable->getID() == m_standInBuilderID;
	const Bool standInOrder = standIn && commandButton->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT;

	// the key is the one the player gave the button on the launcher's Hotkeys page, or else the
	// game's own, the letter after the '&' in the button's label - the English label's in every
	// language, so a Turkish or German player presses what an English one does - and the button
	// wears it in its top left corner.  With the command bar grid on, a bar button's key is its
	// window slot's letter unless the player gave it one.
	if( TheHotKeyManager )
	{
		WideChar letter = standInOrder ? 0 : HotKeyManager::playerButtonKey( commandButton->getName(),
			TheGameText ? TheGameText->fetchEnglishHotKey( commandButton->getTextLabel() ) : 0,
			commandButton->getCommandType(), barSlot, barSlot >= 0 ? m_commandUpgradeRank[ barSlot ] : -1 );
		if( standIn && letter != 0 && HotKeyManager::commandMapHolds( HotKeyManager::nameOf( letter ) ) )
			letter = 0;
		// the badge shows a letter only while this window holds the key: one that lost it to a
		// stronger or earlier button shows none, and one that takes it wipes the badge it took it from
		GameWindow *displaced = NULL;
		const AsciiString key = letter != 0 ? HotKeyManager::nameOf( letter ) : AsciiString::TheEmptyString;
		if( letter != 0 && TheHotKeyManager->addHotKey( button, key, barSlot, &displaced ) )
		{
			const WideChar text[ 2 ] = { letter, 0 };
			GadgetButtonSetText( button, UnicodeString( text ) );
			button->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );
			const UnicodeString shown = displaced ? displaced->winGetText() : UnicodeString::TheEmptyString;
			if( shown.getLength() == 1 && HotKeyManager::nameOf( shown.getCharAt( 0 ) ) == key )
			{
				GadgetButtonSetText( displaced, UnicodeString( u"" ) );
				displaced->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
			}
		}
		else
		{
			GadgetButtonSetText( button, UnicodeString( u"" ) );
			button->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
		}
	}

	GadgetButtonSetAltSound(button, "GUICommandBarClick");

}  // end setControlCommand

//-------------------------------------------------------------------------------------------------
void CommandButton::cacheButtonImage()
{
	if (!TheMappedImageCollection) {
		return;
	}
	if( m_buttonImageName.isNotEmpty() )
	{
		m_buttonImage = TheMappedImageCollection->findImageByName( m_buttonImageName );
		DEBUG_ASSERTCRASH( m_buttonImage, ("CommandButton: %s is looking for button image %s but can't find it. Skipping...", m_name.str(), m_buttonImageName.str() ) );
		m_buttonImageName.clear();	// we're done with this, so nuke it
	}
}

//-------------------------------------------------------------------------------------------------
/** post process step, after all commands and command sets are loaded */
//-------------------------------------------------------------------------------------------------
void ControlBar::postProcessCommands( void )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	for ( CommandButton *button = TheCommandStore->friend_getCommandButtons(); button; button = button->friend_getNext() )
	{
		// a map.ini's edit of a button is an override hanging off it, not a button in this list
		for ( Overridable *o = button; o; o = o->friend_getNextOverride() )
			static_cast<CommandButton *>( o )->cacheButtonImage();
	}
}

//-------------------------------------------------------------------------------------------------
/** set the command for the button identified by the window name
	* NOTE that parent may be NULL, it only helps to speed up the search for a particular
	* window ID */
//-------------------------------------------------------------------------------------------------
void ControlBar::setControlCommand( const AsciiString& buttonWindowName, GameWindow *parent,
																		const CommandButton *commandButton )
{
	UnsignedInt winID = TheNameKeyGenerator->nameToKey( buttonWindowName );
	GameWindow *win = TheWindowManager->winGetWindowFromId( parent, winID );

	if( win == NULL )
	{

		DEBUG_ASSERTCRASH( 0, ("setControlCommand: Unable to find window '%s'\n", buttonWindowName.str()) );
		return;

	}  // end if

	// call the workhorse
	setControlCommand( win, commandButton );

}  // end setControlCommand

//-------------------------------------------------------------------------------------------------
/** show/hide the portrait window image */
//-------------------------------------------------------------------------------------------------
void ControlBar::setPortraitByImage( const Image *image )
{

	if( image )
	{
		m_rightHUDUnitSelectParent->winHide(FALSE);
		m_rightHUDCameoWindow->winSetEnabledImage( 0, image );
		//m_rightHUDWindow->winSetEnabledImage( 0, image );
		m_rightHUDWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winSetStatus( WIN_STATUS_IMAGE );
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);

	}  // end if
	else
	{
		m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDUnitSelectParent->winHide(TRUE);
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);
		//m_rightHUDWindow->winSetEnabledImage( 0, image );
		//m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );

	}

}  // end setPortraitByImage

//-------------------------------------------------------------------------------------------------
/** show/hide the portrait image by object.  We like to use this method as opposed to the
	* plain image one above so that we can build more intelligence into what portrait to
	* show for an object given its current state or object type */
//-------------------------------------------------------------------------------------------------
void ControlBar::setPortraitByObject( Object *obj )
{
	// a builder standing in for an empty selection lends the bar its buildings and nothing else: its
	// portrait, name and health beside it read as that worker being selected, so the well stays empty
	if( obj && m_standInBuilderID != INVALID_DRAWABLE_ID && obj->getDrawable()
			&& obj->getDrawable()->getID() == m_standInBuilderID )
		obj = NULL;

	// the multi-select unit grid lives over this same HUD; a plain portrait means it must
	// go, and so must the selection-count badge a one-type selection put on the portrait.
	// updateMultiSelectStrip re-applies its own look right after calling in here.
	for( size_t tile = 0; tile < m_multiSelectTiles.size(); tile++ )
		if( m_multiSelectTiles[ tile ] )
			m_multiSelectTiles[ tile ]->winHide( TRUE );
	GadgetButtonSetCount( m_rightHUDCameoWindow, 0 );

	if( obj )
	{
		if( obj->isKindOf( KINDOF_SHOW_PORTRAIT_WHEN_CONTROLLED ) && !obj->isLocallyControlled() )
		{
			//Handles civ vehicles without terrorists in them
			setPortraitByObject( NULL );
			return;
		}

		const ThingTemplate *thing = obj->getTemplate();
		Player *player = obj->getControllingPlayer();
		
		//If we have an enemy stealth disguised unit, swap portraits!
		Drawable *draw = obj->getDrawable();
		if( draw && draw->getStealthLook() == STEALTHLOOK_DISGUISED_ENEMY )
		{
			thing = draw->getTemplate();
			if( thing->isKindOf( KINDOF_SHOW_PORTRAIT_WHEN_CONTROLLED ) )
			{
				//If a bomb truck disguises as a civ vehicle, don't use it's portrait (or else you'll see the terrorist).
				setPortraitByObject( NULL );
				return;
			}
      StealthUpdate *stealth = obj->getStealth();
			if( stealth && stealth->isDisguised() )
			{
				//Fake player upgrades too!
				player = ThePlayerList->getNthPlayer( stealth->getDisguisedPlayerIndex() );
			}
		}
		
		const Image* portrait = thing->getSelectedPortraitImage();

		m_rightHUDUnitSelectParent->winHide(FALSE);
		// enable the window window as an image window and set the image
		m_rightHUDCameoWindow->winSetEnabledImage( 0, portrait );

		//Display the veterancy rank of the object on the portrait.
		const Image *image = calculateVeterancyOverlayForObject( obj );
		GadgetButtonDrawOverlayImage( m_rightHUDCameoWindow, image );

		//m_rightHUDWindow->winSetEnabledImage( 0, portrait );
		m_rightHUDWindow->winClearStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winSetStatus( WIN_STATUS_IMAGE );

		// a player's upgrades are his research, which nobody else can see on the battlefield, so they
		// light only for the local player, his allies and a watcher; an enemy's or a neutral's portrait
		// lights only what the unit carries itself, its drones and add-ons.  Retail lit them all
		Player *local = ThePlayerList->getLocalPlayer();
		const Bool researchShown = player && ( !local->isPlayerActive() || player == local
																					 || local->getRelationship( player->getDefaultTeam() ) == ALLIES );

		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
		{
			AsciiString upgradeName = thing->getUpgradeCameoName(i);
			if(upgradeName.isEmpty())
			{
				m_rightHUDUpgradeCameos[i]->winHide(TRUE);
				continue;
			}
			const UpgradeTemplate *ut =  TheUpgradeCenter->findUpgrade(upgradeName);
			if(!ut)
			{
				m_rightHUDUpgradeCameos[i]->winHide(TRUE);
				continue;
			}

			m_rightHUDUpgradeCameos[i]->winHide(FALSE);
			m_rightHUDUpgradeCameos[i]->winSetEnabledImage( 0, ut->getButtonImage() );
			m_rightHUDUpgrades[i] = ut;
			if( obj->hasUpgrade(ut) )
			{
				//Object level upgrades
				m_rightHUDUpgradeCameos[i]->winEnable( TRUE );
			}
			else if( researchShown && player->hasUpgradeComplete( ut ) )
			{
				//Player level upgrades
				m_rightHUDUpgradeCameos[i]->winEnable( TRUE );
			}
			else
			{
				//Failure
				m_rightHUDUpgradeCameos[i]->winEnable( FALSE );
			}
		}
	

	}  // end if
	else
	{
		m_rightHUDUnitSelectParent->winHide(TRUE);
		m_rightHUDWindow->winSetStatus( WIN_STATUS_IMAGE );
		m_rightHUDCameoWindow->winClearStatus( WIN_STATUS_IMAGE );
		for(Int i = 0; i < MAX_UPGRADE_CAMEO_UPGRADES; ++i)
			m_rightHUDUpgradeCameos[i]->winHide(TRUE);

		//Clear any overlay the portrait had on it.
		GadgetButtonDrawOverlayImage( m_rightHUDCameoWindow, NULL );
	}

}  // end setPortraitByObject

// ------------------------------------------------------------------------------------------------
/** Show a rally point marker at the world location specified.  If no location is specified
	* any marker that we might have visible is hidden */
// ------------------------------------------------------------------------------------------------
void ControlBar::showRallyPoint( const Coord3D *loc )
{

	// if loc is NULL, destroy any rally point drawble we have shown
	if( loc == NULL )
	{

		// destroy rally point drawable if present
		if( m_rallyPointDrawableID != INVALID_DRAWABLE_ID )
			TheGameClient->destroyDrawable( TheGameClient->findDrawableByID( m_rallyPointDrawableID ) );
		m_rallyPointDrawableID = INVALID_DRAWABLE_ID;

	}  // end if
	else
	{
		Drawable *marker = NULL;

		// create a rally point drawble if necessary
		if( m_rallyPointDrawableID == INVALID_DRAWABLE_ID )
		{

			const ThingTemplate* ttn = TheThingFactory->findTemplate("RallyPointMarker");
			if (!ttn)
				return;
			marker = TheThingFactory->newDrawable( ttn );
			DEBUG_ASSERTCRASH( marker, ("showRallyPoint: Unable to create rally point drawable\n") );
			if (marker)
			{
				marker->setDrawableStatus(DRAWABLE_STATUS_NO_SAVE);
				m_rallyPointDrawableID = marker->getID();
			}

		}  // end if
		else
			marker = TheGameClient->findDrawableByID( m_rallyPointDrawableID );

		// sanity
		DEBUG_ASSERTCRASH( marker, ("showRallyPoint: No rally point marker found\n" ) );

		// set the position of the rally point drawble to the position passed in
		marker->setPosition( loc );
		marker->setOrientation( TheGlobalData->m_downwindAngle );//To blow down wind -- ML

		// set the marker colors to that of the local player
		Player *player = ThePlayerList->getLocalPlayer();

		if (TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT)
			marker->setIndicatorColor( player->getPlayerNightColor() );
		else
			marker->setIndicatorColor( player->getPlayerColor() );

	}  // end else

}  // end showRallyPoint

// ------------------------------------------------------------------------------------------------
/** Show a rally point marker at the world location specified.  If no location is specified
	* any marker that we might have visible is hidden */
// ------------------------------------------------------------------------------------------------
void ControlBar::showIdleButtons( Bool show )
{
	static const NameKeyType ids[ 4 ] =
	{
		NAMEKEY( "ControlBar.wnd:ButtonIdleWorker" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleInfantry" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleVehicle" ),
		NAMEKEY( "ControlBar.wnd:ButtonIdleAircraft" ),
	};
	for( Int i = 0; i < 4; i++ )
	{
		GameWindow *win = TheWindowManager->winGetWindowFromId( NULL, ids[ i ] );
		if( win )
			win->winHide( !show );
	}
}

void ControlBar::setControlBarSchemeByPlayer(Player *p)
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayer(p);

	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralID );

	if( !p->isPlayerActive() )
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( CB_CONTEXT_OBSERVER_LIST, NULL );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		showIdleButtons( FALSE );
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( CB_CONTEXT_NONE, NULL );
		m_isObserverCommandBar = FALSE;
		// what was showing is gone, and no selection event will bring it back: the selection's or
		// the stand-in builder's command set has to be evaluated again
		markUIDirty();

		showIdleButtons( TRUE );
		if (buttonGeneral)
		{
			buttonGeneral->winHide(FALSE);
			buttonGeneral->winEnable(TRUE);
		}
	}
	restoreStageAfterScheme();
}

//-------------------------------------------------------------------------------------------------
/** A scheme change lays the bar out again at full size.  A bar the player minimised stays that way:
	* switching seats, or clicking another player's unit while watching, used to throw the whole bar
	* back up over the battlefield the player had just cleared it off.  A new match starts at full
	* size because reset() puts the stage back first. */
//-------------------------------------------------------------------------------------------------
void ControlBar::restoreStageAfterScheme( void )
{
	switchControlBarStage( m_currentControlBarStage == CONTROL_BAR_STAGE_LOW ? CONTROL_BAR_STAGE_LOW
																																					: CONTROL_BAR_STAGE_DEFAULT );
}

void ControlBar::setControlBarSchemeByPlayerTemplate( const PlayerTemplate *pt)
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(pt);

	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralID );

	if(pt == ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionObserver")))
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( CB_CONTEXT_OBSERVER_LIST, NULL );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		showIdleButtons( FALSE );
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( CB_CONTEXT_NONE, NULL );
		m_isObserverCommandBar = FALSE;
		// what was showing is gone, and no selection event will bring it back: the selection's or
		// the stand-in builder's command set has to be evaluated again
		markUIDirty();

		showIdleButtons( TRUE );
		if (buttonGeneral)
		{
			buttonGeneral->winHide(FALSE);
			buttonGeneral->winEnable(TRUE);
		}
	}
	restoreStageAfterScheme();

	hidePurchaseScience();
}

void ControlBar::setControlBarSchemeByName(const AsciiString& name)
{
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarScheme( name );
		switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT);

}

void ControlBar::preloadAssets( TimeOfDay timeOfDay )
{
	if (m_controlBarSchemeManager)
		m_controlBarSchemeManager->preloadAssets( timeOfDay );
}

void ControlBar::updateBuildQueueDisabledImages( const Image *image )
{
	if(!image)
		return;
	// We have to do this because the build queue data might have been reset
	static NameKeyType buildQueueIDs[ MAX_BUILD_QUEUE_BUTTONS ];
	static Bool idsInitialized = FALSE;
	Int i;

	// get name key ids for the build queue buttons
	if( idsInitialized == FALSE )
	{
		AsciiString buttonName;

		for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
		{
			
			buttonName.format( "ControlBar.wnd:ButtonQueue%02d", i + 1 );
			buildQueueIDs[ i ] = TheNameKeyGenerator->nameToKey( buttonName );

		}  // end for i

		idsInitialized = TRUE;

	}  // end if

	// get window pointers to all the buttons for the build queue
	for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
	{

		// get window commented out cause I believe we already set this.  We'll see in a few minutes
		m_queueData[ i ].control = TheWindowManager->winGetWindowFromId( m_contextParent[ CP_BUILD_QUEUE ],
																																		 buildQueueIDs[ i ] );

		GadgetButtonSetDisabledImage( m_queueData[ i ].control, image );

	}  // end for i

}

void ControlBar::updateRightHUDImage( const Image *image )
{
	if(!m_rightHUDWindow || !image)
		return;
	m_rightHUDWindow->winSetEnabledImage(0, image);

}

void ControlBar::updateBuildUpClockColor( Color color)
{
	m_buildUpClockColor = color;
}



void ControlBar::updateCommanBarBorderColors(Color build, Color action, Color upgrade, Color system )
{
	m_commandButtonBorderBuildColor = build;
	m_commandButtonBorderActionColor = action;
	m_commandButtonBorderUpgradeColor = upgrade;
	m_commandButtonBorderSystemColor = system;
}

// ---------------------------------------------------------------------------------------
// hides the communicator button
void ControlBar::hideCommunicator( Bool b )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	//sanity
	if( m_communicatorButton != NULL )
		m_communicatorButton->winHide( b );
}

// ---------------------------------------------------------------------------------------
// Outside hook so when the genera's head is pushed, we can switch to the purchase science
// context
void ControlBar::updatePurchaseScience( void )
{
//	if(m_generalsScreenAnimate && TheGlobalData->m_animateWindows)
//	{
//		Bool wasFinished = m_generalsScreenAnimate->isFinished();
//		m_generalsScreenAnimate->update();
//		if (m_generalsScreenAnimate->isFinished() && !wasFinished && m_generalsScreenAnimate->isReversed())
//			m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide(TRUE);
//	}
}

void ControlBar::showPurchaseScience( void )
{

	if(TheScriptEngine->isGameEnding())
		return;

	//
	// Watching, the screen is one player's in particular and the selection is what names him.  With
	// nothing selected there is nobody to open it on, so the key does nothing rather than putting up
	// a general the watcher did not ask for.  The painting behind it is his side's, which is the
	// same scheme the bar is already wearing by then - evaluateContextUI put it on when he was
	// selected.
	//
	Player *watched = getWatchedPlayer();
	if( watched != ThePlayerList->getLocalPlayer() && getSelectedPlayer() == NULL )
		return;

	populatePurchaseScience(watched);
	m_genStarFlash = FALSE;
	if( m_purchaseScienceOpen )
		return;
	m_purchaseScienceOpen = TRUE;
	//switchToContext(CB_CONTEXT_PURCHASE_SCIENCE, NULL);
	m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide(FALSE);
	// the screen's foot (to 434 of 600) runs under the command bar's top edge (416), and the first
	// window under the pointer takes the click, so the screen goes in front of the bar while it is up
	m_contextParent[ CP_PURCHASE_SCIENCE ]->winBringToTop();
	if (TheGlobalData->m_animateWindows)
	{
		// a fade still running out is ended first, or it would hide the screen it just showed
		TheTransitionHandler->remove("GenExpFade");
		TheTransitionHandler->setGroup("GenExpFade");
	}
		//m_generalsScreenAnimate->registerGameWindow( m_contextParent[ CP_PURCHASE_SCIENCE ], WIN_ANIMATION_SLIDE_TOP, TRUE, 200 );

}

void ControlBar::hidePurchaseScience( void )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	clearPurchaseScienceColumn();

	const Bool wasOpen = m_purchaseScienceOpen;
	m_purchaseScienceOpen = FALSE;

	//
	// Closing plays the opening backwards: the painted screen runs its fade in reverse, which hides
	// it at the end.  The window stays up meanwhile, which is why the open state is ours and not its
	// hidden flag.
	//
	if( wasOpen && m_contextParent[ CP_PURCHASE_SCIENCE ] && TheGlobalData->m_animateWindows )
	{
		TheTransitionHandler->reverse( "GenExpFade" );
		return;
	}

	//
	// The fade drives winHide on this window itself, frame by frame, and it holds the window hidden
	// for its first few frames.  So the screen's own hidden flag is not the question "is it open" -
	// a second press of the key while the fade was still running read the window as hidden and
	// opened it again, and a press after that was undone the moment the fade's next frame put the
	// window back.  We keep the answer ourselves, and the fade is taken off before we close.
	//
	if( TheTransitionHandler )
		TheTransitionHandler->remove( "GenExpFade" );

	if( m_contextParent[ CP_PURCHASE_SCIENCE ] )
	{
		m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
	}
}

Bool ControlBar::isPurchaseScienceVisible( void )
{
	return m_purchaseScienceOpen && m_contextParent[ CP_PURCHASE_SCIENCE ] != NULL;
}

//-------------------------------------------------------------------------------------------------
/** The button 'depth' rows down a promotion screen column: the 1 point science on top, the three
	* 3 point ones under it, the 5 point one at the bottom.  The 3 point block is the only one five
	* columns wide, so the fifth column has a top and a bottom that do not exist. */
//-------------------------------------------------------------------------------------------------
/** Which of the screen's three rows a column and depth land in - 1, 3 or 8 for the point cost the
	* row sells, 0 where the layout has no button - and the index inside that row's window array.
	* A free function so the mapping can be checked without standing a ControlBar up. */
Int ControlBar_purchaseScienceRank( Int column, Int depth, Int *indexOut )
{
	if( indexOut )
		*indexOut = -1;

	if( column < 0 || column >= PURCHASE_SCIENCE_COLUMNS || depth < 0 )
		return 0;

	if( depth == 0 )
	{
		if( column >= MAX_PURCHASE_SCIENCE_RANK_1 )
			return 0;
		if( indexOut )
			*indexOut = column;
		return 1;
	}

	if( depth <= PURCHASE_SCIENCE_RANK_3_PER_COLUMN )
	{
		const Int slot = column * PURCHASE_SCIENCE_RANK_3_PER_COLUMN + ( depth - 1 );
		if( slot >= MAX_PURCHASE_SCIENCE_RANK_3 )
			return 0;
		if( indexOut )
			*indexOut = slot;
		return 3;
	}

	if( depth == PURCHASE_SCIENCE_COLUMN_DEPTH - 1 )
	{
		if( column >= MAX_PURCHASE_SCIENCE_RANK_8 )
			return 0;
		if( indexOut )
			*indexOut = column;
		return 8;
	}

	return 0;

}  // end ControlBar_purchaseScienceRank

GameWindow *ControlBar::purchaseScienceWindow( Int column, Int depth )
{
	Int index = -1;
	switch( ControlBar_purchaseScienceRank( column, depth, &index ) )
	{
		case 1:	return m_sciencePurchaseWindowsRank1[ index ];
		case 3:	return m_sciencePurchaseWindowsRank3[ index ];
		case 8:	return m_sciencePurchaseWindowsRank8[ index ];
	}

	return NULL;

}  // end purchaseScienceWindow

//-------------------------------------------------------------------------------------------------
/** What a press on that column would buy: the topmost science in it that is on screen and can be
	* bought this instant.  populatePurchaseScience() has already done that arithmetic - it enables
	* exactly the ones the player has the prerequisites and the points for - so a science already
	* owned, still locked, or too expensive is skipped and the key reaches the next one down. */
//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::purchaseScienceCandidate( Int column )
{
	for( Int depth = 0; depth < PURCHASE_SCIENCE_COLUMN_DEPTH; depth++ )
	{
		GameWindow *win = purchaseScienceWindow( column, depth );
		if( win == NULL || win->winIsHidden() )
			continue;
		if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
			return win;
	}

	return NULL;

}  // end purchaseScienceCandidate

//-------------------------------------------------------------------------------------------------
/** forget a marked column, so the next number marks one again */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::clearPurchaseScienceColumn( void )
{
	if( m_purchaseScienceColumn < 0 )
		return FALSE;

	m_purchaseScienceColumn = -1;
	m_purchaseScienceColumnMs = 0;
	return TRUE;

}  // end clearPurchaseScienceColumn

//-------------------------------------------------------------------------------------------------
/** Paint the column numbers onto the promotion screen.  With nothing marked every column wears its
	* key on the science that key would buy next; once a column is marked it is the only number left
	* on the screen, which is what says the same key again spends the point. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updatePurchaseScienceHotKeys( void )
{
	if( !isPurchaseScienceVisible() )
	{
		clearPurchaseScienceColumn();
		return;
	}

	// a column marked and then thought better of expires on its own, like a powers tray row
	if( m_purchaseScienceColumn >= 0
			&& Clock_Milliseconds() - m_purchaseScienceColumnMs > CHORD_TIMEOUT_MS )
		clearPurchaseScienceColumn();

	// the promotion screen ships its own big yellow font; the key reads like a command bar label
	GameWindow *model = m_commandWindows[ 0 ];

	for( Int column = 0; column < PURCHASE_SCIENCE_COLUMNS; column++ )
	{
		GameWindow *candidate = purchaseScienceCandidate( column );

		// the number keys pick teams on this screen as they did in 1.04 (SelectionXlat), so a column
		// key here would name a key that buys nothing
		UnicodeString label;

		for( Int depth = 0; depth < PURCHASE_SCIENCE_COLUMN_DEPTH; depth++ )
		{
			GameWindow *win = purchaseScienceWindow( column, depth );
			if( win == NULL )
				continue;

			if( win == candidate && label.isEmpty() == FALSE )
			{
				if( model )
				{
					win->winSetFont( model->winGetFont() );
					win->winSetEnabledTextColors( model->winGetEnabledTextColor(), model->winGetEnabledTextBorderColor() );
					win->winSetHiliteTextColors( model->winGetHiliteTextColor(), model->winGetHiliteTextBorderColor() );
				}

				// the shortcut flag is what puts the label in the corner instead of the middle
				win->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );
				GadgetButtonSetText( win, label );
			}
			else
			{
				win->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
				GadgetButtonSetText( win, UnicodeString( u"" ) );
			}
		}
	}

}  // end updatePurchaseScienceHotKeys

//-------------------------------------------------------------------------------------------------
/** A promotion point is spent for good, so the number keys buy in two presses like the general's
	* powers fire in two: the first press marks the next science that column will sell you, the same
	* key again buys it.  Another number marks that column instead, Escape closes the screen, and a
	* mark left alone for CHORD_TIMEOUT_MS drops by itself. */
//-------------------------------------------------------------------------------------------------
void ControlBar::pressPurchaseScienceColumn( Int column )
{
	if( !isPurchaseScienceVisible() || column < 0 || column >= PURCHASE_SCIENCE_COLUMNS )
		return;

	GameWindow *win = purchaseScienceCandidate( column );

	// a first press, or a press on some other column: this one only marks
	if( m_purchaseScienceColumn != column )
	{
		m_purchaseScienceColumn = column;
		m_purchaseScienceColumnMs = Clock_Milliseconds();
		updatePurchaseScienceHotKeys();

		// a column with nothing left to sell says so rather than sitting there armed
		if( win == NULL && TheAudio )
		{
			AudioEventRTS disabledClick( "GUIClickDisabled" );
			TheAudio->addAudioEvent( &disabledClick );
			clearPurchaseScienceColumn();
		}
		return;
	}

	// the same key again: buy what it marked
	clearPurchaseScienceColumn();
	if( win == NULL )
		return;

	// GenExpParent runs GeneralsExpPointsSystem, the callback a real click on the cameo reaches
	TheWindowManager->winSendSystemMsg( m_contextParent[ CP_PURCHASE_SCIENCE ], GBM_SELECTED,
																			(WindowMsgData)win, win->winGetWindowId() );
	if( TheAudio )
	{
		AudioEventRTS buttonClick( "GUIGenShortcutClick" );
		TheAudio->addAudioEvent( &buttonClick );
	}

	updatePurchaseScienceHotKeys();

}  // end pressPurchaseScienceColumn

void ControlBar::togglePurchaseScience( void )
{
	if( isPurchaseScienceVisible() )
		hidePurchaseScience();
	else
		showPurchaseScience();
}

void ControlBar::toggleControlBarStage( void )
{
	if(m_currentControlBarStage == CONTROL_BAR_STAGE_DEFAULT )
		switchControlBarStage(CONTROL_BAR_STAGE_LOW);
	else
		switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT);
}

// Functions for repositioning/resizing the control bar
void ControlBar::switchControlBarStage( ControlBarStages stage )
{
	if(stage < CONTROL_BAR_STAGE_DEFAULT || stage >= MAX_CONTROL_BAR_STAGES)
		return;
	// Retail returned here during playback, so a replay never laid its observer bar out: the command
	// grid and the portrait plate stayed up, empty, over the battlefield.  A replay is watched like
	// any observer seat, and the spectator page's replay strip stands where the grid was.
	switch (stage) {
	case CONTROL_BAR_STAGE_DEFAULT:
		setDefaultControlBarConfig();
		break;
//	case CONTROL_BAR_STAGE_SQUISHED:
//		setSquishedControlBarConfig();
//		break;
	case CONTROL_BAR_STAGE_LOW:
		setLowControlBarConfig();
		break;
	case CONTROL_BAR_STAGE_HIDDEN:
		setHiddenControlBar();
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("ControlBar::switchControlBarStage we were passed in a stage that's not supported %d", stage));
	}
	
}
void ControlBar::setDefaultControlBarConfig( void )
{
	m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;
	//
	// Full height, like the low stage and like ShowControlBar: the bar is drawn over the world and
	// does not crop it.  Retail cropped the view to 80% here, and since the stage is reset every
	// time the HUD scheme is rebuilt - taking over another player, a player dying into the observer
	// bar - that shrank the viewport in the middle of a match and slid the picture, for good:
	// nothing put the height back once ShowControlBar had run at the start of the game.
	//
	TheTacticalView->setHeight((Int)(TheDisplay->getHeight()));
	m_contextParent[ CP_MASTER ]->winSetPosition(m_defaultControlBarPosition.x, m_defaultControlBarPosition.y);
	m_contextParent[ CP_MASTER ]->winHide(FALSE);

	repopulateBuildTooltipLayout();
	setUpDownImages();

}

void ControlBar::setSquishedControlBarConfig( void )
{
	if(m_currentControlBarStage == CONTROL_BAR_STAGE_SQUISHED)
		return;
	m_currentControlBarStage = CONTROL_BAR_STAGE_SQUISHED;
	m_contextParent[ CP_MASTER ]->winSetPosition(m_defaultControlBarPosition.x, m_defaultControlBarPosition.y);
	
	repopulateBuildTooltipLayout();	
	TheTacticalView->setHeight((Int)(TheDisplay->getHeight())); 
	m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(ThePlayerList->getLocalPlayer()->getPlayerTemplate(), TRUE);
}

void ControlBar::setLowControlBarConfig( void )
{

	m_currentControlBarStage = CONTROL_BAR_STAGE_LOW;

	TheTacticalView->setHeight((Int)(TheDisplay->getHeight()));

	// EA's: the whole bar drops to a tenth of the screen from the bottom, its top edge still showing
	m_contextParent[ CP_MASTER ]->winSetPosition( m_defaultControlBarPosition.x,
		REAL_TO_INT( TheDisplay->getHeight() - 0.1f * TheDisplay->getHeight() ) );
	m_contextParent[ CP_MASTER ]->winHide(FALSE);
	setUpDownImages();

}

void ControlBar::setHiddenControlBar( void )
{
	m_currentControlBarStage = CONTROL_BAR_STAGE_HIDDEN;
	m_contextParent[ CP_MASTER ]->winHide(TRUE);
}
// removed from multiplayer test
//void ControlBar::showCommandMarkers( void )
//{
//	for(Int i =0; i < MAX_COMMANDS_PER_SET; ++i)
//	{
//		if(m_commandWindows[i]->winIsHidden())
//			m_commandMarkers[i]->winHide(FALSE);
//		else
//			m_commandMarkers[i]->winHide(TRUE);
//	}
//}
//
void ControlBar::updateCommandMarkerImage( const Image *image )
{
	// removed from multiplayer branch

//	// we don't mind if the image is null, that way we can not draw anything
	//	for(Int i =0; i < MAX_COMMANDS_PER_SET; ++i)
	//	{
	//		m_commandMarkers[i]->winSetEnabledImage(0, image);
	//	}
	
}
void ControlBar::updateSlotExitImage( const Image *image )
{
	//Hardcoding values here Not a good thing but there's no other way right now.
	if(!image)
		return;

	//Kris:
	//Other than this being a completely ridiculously retarded idea, I'm not inclined
	//to recode this in a better way, yikes! Btw, I DID NOT CODE THIS! But this is
	//what this does: The button images are overridden by a faction specific icon.
	//The proper way to fix this would be to make a commandbutton option and loop
	//through all buttons on init to replace the icon. We need a system like this
	//for neutral buildings which can have a different empty inventory icon based
	//on the faction player.

	// the button image is the client's field on a logic-owned button
	CommandButton *cmdButton = TheCommandStore->findNonConstCommandButton( "Command_StructureExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = TheCommandStore->findNonConstCommandButton( "Command_TransportExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = TheCommandStore->findNonConstCommandButton( "Command_BunkerExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = TheCommandStore->findNonConstCommandButton( "Command_FireBaseExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

}

void ControlBar::updateUpDownImages( const Image *toggleButtonUpIn, const Image *toggleButtonUpOn, const Image *toggleButtonUpPushed,
																		 const Image *toggleButtonDownIn, const Image *toggleButtonDownOn, const Image *toggleButtonDownPushed,
																		 const Image *generalButtonEnable, const Image *generalButtonHighlight  )
{
	m_toggleButtonUpIn = toggleButtonUpIn;
	m_toggleButtonUpOn = toggleButtonUpOn;
	m_toggleButtonUpPushed = toggleButtonUpPushed;
	m_toggleButtonDownIn = toggleButtonDownIn;
	m_toggleButtonDownOn = toggleButtonDownOn;
	m_toggleButtonDownPushed = toggleButtonDownPushed;


	m_generalButtonEnable = generalButtonEnable;
	m_generalButtonHighlight = generalButtonHighlight;

	setUpDownImages();
}

void ControlBar::setUpDownImages( void )
{
	GameWindow *win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonLarge" ) );
	if(!win)
		return;

	// we only care if it's in it's low state, else we put the default images up
	if(m_currentControlBarStage == CONTROL_BAR_STAGE_LOW)
	{
		GadgetButtonSetEnabledImage(win, m_toggleButtonUpOn);
		GadgetButtonSetHiliteImage(win, m_toggleButtonUpIn);
		GadgetButtonSetHiliteSelectedImage(win, m_toggleButtonUpPushed);
		return;
	}

	GadgetButtonSetEnabledImage(win, m_toggleButtonDownOn);
	GadgetButtonSetHiliteImage(win, m_toggleButtonDownIn);
	GadgetButtonSetHiliteSelectedImage(win, m_toggleButtonDownPushed);

}

void ControlBar::getForegroundMarkerPos(Int *x, Int *y)
{
	*x = m_controlBarForegroundMarkerPos.x;
	*y = m_controlBarForegroundMarkerPos.y;
}
void ControlBar::getBackgroundMarkerPos(Int *x, Int *y)
{
	*x = m_controlBarBackgroundMarkerPos.x;
	*y = m_controlBarBackgroundMarkerPos.y;
}

void ControlBar::drawTransitionHandler( void )
{
//	if(m_transitionHandler)
//		m_transitionHandler->draw();
}
void ControlBar::initSpecialPowershortcutBar( Player *player)
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );

	// Second loop further down reuses this i under VC6 for-scope.
	Int i;
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		m_specialPowerShortcutButtonParents[i] = NULL;
		m_specialPowerShortcutButtons[i] = NULL;
	}

	if(m_specialPowerLayout)
	{
		m_specialPowerLayout->destroyWindows();
		m_specialPowerLayout->deleteInstance();
		m_specialPowerLayout = NULL;
	}
	m_specialPowerShortcutParent = NULL;
	m_currentlyUsedSpecialPowersButtons = 0;
	const PlayerTemplate *pt = player->getPlayerTemplate();

	if(!player || !pt|| !player->isLocalPlayer()
			|| pt->getSpecialPowerShortcutButtonCount() == 0  
			|| pt->getSpecialPowerShortcutWinName().isEmpty()
			|| !player->isPlayerActive())
		return;
	m_currentlyUsedSpecialPowersButtons = pt->getSpecialPowerShortcutButtonCount();
	AsciiString layoutName, tempName, windowName, parentName;
	layoutName = pt->getSpecialPowerShortcutWinName();
	m_specialPowerLayout = TheWindowManager->winCreateLayout(layoutName);
	m_specialPowerLayout->hide(TRUE);

	tempName = layoutName;
	tempName.concat(":GenPowersShortcutBarParent");
	NameKeyType id = TheNameKeyGenerator->nameToKey( tempName );
	m_specialPowerShortcutParent = TheWindowManager->winGetWindowFromId( NULL, id );//m_scienceLayout->getFirstWindow();

	//
	// The bar is authored at 752..800 x 3..429 and the loader stretches x and y by different
	// amounts, so on a 16:9 screen every slot came out a third wider than it is tall - and both
	// strips measure their cameos off these slots, so the whole HUD inherited the smear.  Put it
	// back at the command bar's own uniform scale, pinned to the bottom right corner, which is
	// where it was authored and where the bar's right hand panel now sits.
	//
	ControlBarLayoutUniform( m_specialPowerShortcutParent, 1.0f, 1.0f );

	// a new layout: the stepped tray's frame is placed by the first arrangement under a scheme with a tray
	m_trayLaidOutMask = TRAY_NOT_LAID_OUT;
	m_trayFramePlaced = FALSE;

	tempName = layoutName;
	tempName.concat(":ButtonCommand%d");
	parentName = layoutName;
	parentName.concat(":ButtonParent%d");
	m_currentlyUsedSpecialPowersButtons = MIN(pt->getSpecialPowerShortcutButtonCount(), MAX_SPECIAL_POWER_SHORTCUTS);
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		windowName.format( tempName, i+1 );
		id = TheNameKeyGenerator->nameToKey( windowName.str() );
		m_specialPowerShortcutButtons[ i ] = 
			TheWindowManager->winGetWindowFromId( m_specialPowerShortcutParent, id );
		m_specialPowerShortcutButtons[ i ]->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		// Oh god... this is a total hack for shortcut buttons to handle rendering text top left corner...
		m_specialPowerShortcutButtons[ i ]->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );

		// the F-key label should read like the command bar's grid letters, not the big yellow
		// font the shortcut layout ships with: borrow a command button's font and text colors
		GameWindow *model = m_commandWindows[ 0 ];
		if( model )
		{
			GameWindow *b = m_specialPowerShortcutButtons[ i ];
			b->winSetFont( model->winGetFont() );
			b->winSetEnabledTextColors( model->winGetEnabledTextColor(), model->winGetEnabledTextBorderColor() );
			b->winSetDisabledTextColors( model->winGetDisabledTextColor(), model->winGetDisabledTextBorderColor() );
			b->winSetHiliteTextColors( model->winGetHiliteTextColor(), model->winGetHiliteTextBorderColor() );
		}

		windowName.format( parentName, i+1 );
		id = TheNameKeyGenerator->nameToKey( windowName.str() );
		m_specialPowerShortcutButtonParents[ i ] =
			TheWindowManager->winGetWindowFromId( m_specialPowerShortcutParent, id );
	}  // end for i

	arrangeSpecialPowerShortcutGrid();

}

//-------------------------------------------------------------------------------------------------
/** One slot's plate, from the scheme's PowerTrayImage pieces: the cell under the power and the
	* edges, corners and step on whichever of its sides the tray's outline runs (TraySlot).  A slot
	* draws only inside its own window, so the slots together are the outline and nothing overlaps. */
//-------------------------------------------------------------------------------------------------
static void drawPowerTraySlot( GameWindow *window, WinInstanceData *instData )
{
	const ControlBar::TraySlot *slot = (const ControlBar::TraySlot *)window->winGetUserData();
	const Image *const *tray = TheControlBar->getControlBarSchemeManager()->getPowerTray();
	if( slot == NULL || tray == NULL )
		return;

	Int x, y;
	window->winGetScreenPosition( &x, &y );
	const Int x0 = x + slot->cell.lo.x, y0 = y + slot->cell.lo.y;
	const Int x1 = x + slot->cell.hi.x, y1 = y + slot->cell.hi.y;
	const Int l = slot->edgeLeft, t = slot->edgeTop, b = slot->edgeBottom;
	struct Piece { Int image, lx, ly, hx, hy; Bool wanted; };
	const Piece pieces[] =
	{
		{ ControlBarScheme::TRAY_CELL, x0, y0, x1, y1, TRUE },
		{ ControlBarScheme::TRAY_EDGE_TOP, x0, y0 - t, x1 - slot->stepRight, y0, t > 0 },
		{ ControlBarScheme::TRAY_EDGE_LEFT, x0 - l, y0, x0, y1 - slot->stepBelow, l > 0 },
		{ ControlBarScheme::TRAY_STEP, x0 - l, y1 - slot->stepBelow, x0, y1, slot->stepBelow > 0 },
		{ ControlBarScheme::TRAY_CORNER_TL, x0 - l, y0 - t, x0, y0, l > 0 && t > 0 },
		{ ControlBarScheme::TRAY_EDGE_BOTTOM, x0, y1, x1, y1 + b, b > 0 },
		{ ControlBarScheme::TRAY_CORNER_BL, x0 - l, y1, x0, y1 + b, l > 0 && b > 0 },
	};
	for( Int i = 0; i < (Int)( sizeof( pieces ) / sizeof( pieces[ 0 ] ) ); i++ )
	{
		// a piece the art leaves out leaves the battlefield showing there
		const Piece &p = pieces[ i ];
		if( p.wanted && tray[ p.image ] )
			TheDisplay->drawImage( tray[ p.image ], p.lx, p.ly, p.hx, p.hy );
	}
}

//-------------------------------------------------------------------------------------------------
/** With a scheme that names PowerTrayImage, the powers stand SPECIAL_POWER_SHORTCUT_COLS to a row
	* in the scheme's cells, hard against the right edge of the screen, the first row on the general's
	* tab and the rest climbing from it.  Slot i is row i / COLS and place i % COLS counted in from the
	* edge, the order the two-key chords already read, so a row that is not full is short on the
	* inside and the tray steps there.  Each slot window is its cell plus the edges the outline runs
	* along it, which is what keeps a click on the plate off the world and lets one through beside it.
	* A slot hidden mid-row (COMMAND_HIDDEN) gives its place to the next one shown.  The first
	* arrangement under a scheme with a tray stands the frame at the tallest tray's size, before
	* anything slides it in, and it stays there, so the slide carries the slots and a new layout never
	* fights it.
	*
	* A scheme without the pieces keeps the shipped column, stretched with the bar on the
	* right edge and climbing from the top of the bar as 1.04's did.  Either way the frame takes no
	* click: it runs well past the powers, and once took every click over the battlefield and the
	* promotion screen beside them. */
//-------------------------------------------------------------------------------------------------
void ControlBar::arrangeSpecialPowerShortcutGrid( void )
{
	if( m_specialPowerShortcutParent == NULL )
		return;
	m_specialPowerShortcutParent->winSetStatus( WIN_STATUS_NO_INPUT );

	const Image *const *tray = m_controlBarSchemeManager->getPowerTray();
	m_trayLaidOutMask = visibleSpecialPowerShortcutMask();
	m_trayLaidOutFor = tray;
	if( tray == NULL )
		return;

	ICoord2D origin, cell;
	Int left, top, foot, bottom;
	powerTrayMetrics( tray, &origin, &cell, &left, &top, &foot, &bottom );
	if( !m_trayFramePlaced )
	{
		m_specialPowerShortcutParent->winSetPosition( origin.x, origin.y );
		m_specialPowerShortcutParent->winSetSize( TheDisplay->getWidth() - origin.x, bottom - origin.y );
		m_trayFramePlaced = TRUE;
	}

	Int shown[ MAX_SPECIAL_POWER_SHORTCUTS ];
	Int count = 0;
	for( Int slotIndex = 0; slotIndex < MAX_SPECIAL_POWER_SHORTCUTS; slotIndex++ )
	{
		if( m_trayLaidOutMask & ( 1u << slotIndex ) )
			shown[ count++ ] = slotIndex;
	}

	const Int cols = SPECIAL_POWER_SHORTCUT_COLS;
	const Int rows = ( count + cols - 1 ) / cols;
	Int rowLength[ MAX_SPECIAL_POWER_SHORTCUTS / SPECIAL_POWER_SHORTCUT_COLS + 2 ];
	for( Int r = 0; r <= rows; r++ )
		rowLength[ r ] = r < rows ? MIN( cols, count - r * cols ) : 0;

	const Int right = TheDisplay->getWidth();
	for( Int i = 0; i < count; i++ )
	{
		GameWindow *slotWin = m_specialPowerShortcutButtonParents[ shown[ i ] ];
		GameWindow *button = m_specialPowerShortcutButtons[ shown[ i ] ];
		const Int row = i / cols, place = i % cols;
		const Bool covered = place < rowLength[ row + 1 ];
		const Bool innermost = place == rowLength[ row ] - 1;

		TraySlot &slot = m_traySlot[ i ];
		slot.edgeLeft = innermost ? left : 0;
		slot.edgeTop = covered ? 0 : top;
		slot.edgeBottom = row == 0 ? foot : 0;
		// the short row above ends over this cell's outer neighbour: the step closes the corner there
		slot.stepRight = !covered && place > 0 && place - 1 < rowLength[ row + 1 ] ? left : 0;
		// and this, the short row's innermost cell, stands on a longer row
		slot.stepBelow = innermost && row > 0 && place + 1 < rowLength[ row - 1 ] ? top : 0;

		IRegion2D screen;
		screen.hi.x = right - place * cell.x;
		screen.lo.x = screen.hi.x - cell.x - slot.edgeLeft;
		screen.hi.y = bottom - foot - row * cell.y + slot.edgeBottom;
		screen.lo.y = screen.hi.y - slot.edgeBottom - cell.y - slot.edgeTop;
		slot.cell.lo.x = slot.edgeLeft;
		slot.cell.lo.y = slot.edgeTop;
		slot.cell.hi.x = slot.edgeLeft + cell.x;
		slot.cell.hi.y = slot.edgeTop + cell.y;

		slotWin->winSetPosition( screen.lo.x - origin.x, screen.lo.y - origin.y );
		slotWin->winSetSize( screen.hi.x - screen.lo.x, screen.hi.y - screen.lo.y );
		slotWin->winSetUserData( &slot );
		slotWin->winSetDrawFunc( drawPowerTraySlot );

		Int bw, bh;
		button->winGetSize( &bw, &bh );
		button->winSetPosition( slot.edgeLeft + ( cell.x - bw ) / 2, slot.edgeTop + ( cell.y - bh ) / 2 );
	}
}

//-------------------------------------------------------------------------------------------------
/** The slots showing a power, one bit a slot. */
//-------------------------------------------------------------------------------------------------
UnsignedInt ControlBar::visibleSpecialPowerShortcutMask( void )
{
	UnsignedInt mask = 0;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons && i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		if( !m_specialPowerShortcutButtons[ i ]->winIsHidden() )
			mask |= 1u << i;
	}
	return mask;
}

//-------------------------------------------------------------------------------------------------
/** Lay the tray out again when a power was bought, spent or hidden, or the scheme changed its
	* pieces; populateSpecialPowerShortcut and updateSpecialPowerShortcut are what change those. */
//-------------------------------------------------------------------------------------------------
void ControlBar::rearrangeSpecialPowerShortcutGrid( void )
{
	if( m_specialPowerShortcutParent == NULL )
		return;
	if( visibleSpecialPowerShortcutMask() != m_trayLaidOutMask
			|| m_controlBarSchemeManager->getPowerTray() != m_trayLaidOutFor )
		arrangeSpecialPowerShortcutGrid();
}

//-------------------------------------------------------------------------------------------------
/** The tray's numbers on this screen: the pieces at the screen's height over 1080, and the
	* frame's top left at rest, the tallest tray's box standing on the general's tab against the
	* right edge. */
//-------------------------------------------------------------------------------------------------
void ControlBar::powerTrayMetrics( const Image *const *tray, ICoord2D *origin, ICoord2D *cell,
																	 Int *left, Int *top, Int *foot, Int *bottom )
{
	const Real k = TheDisplay->getHeight() / 1080.0f;
	cell->x = REAL_TO_INT_CEIL( tray[ ControlBarScheme::TRAY_CELL ]->getImageWidth() * k );
	cell->y = REAL_TO_INT_CEIL( tray[ ControlBarScheme::TRAY_CELL ]->getImageHeight() * k );
	const Image *edgeLeft = tray[ ControlBarScheme::TRAY_EDGE_LEFT ];
	const Image *edgeTop = tray[ ControlBarScheme::TRAY_EDGE_TOP ];
	const Image *edgeFoot = tray[ ControlBarScheme::TRAY_EDGE_BOTTOM ];
	*left = edgeLeft ? REAL_TO_INT_CEIL( edgeLeft->getImageWidth() * k ) : 0;
	*top = edgeTop ? REAL_TO_INT_CEIL( edgeTop->getImageHeight() * k ) : 0;
	*foot = edgeFoot ? REAL_TO_INT_CEIL( edgeFoot->getImageHeight() * k ) : 0;

	const Int cols = SPECIAL_POWER_SHORTCUT_COLS;
	const Int rows = ( MAX_SPECIAL_POWER_SHORTCUTS + cols - 1 ) / cols;
	*bottom = TheDisplay->getHeight() - REAL_TO_INT( ( 1080 - POWER_TRAY_FOOT_Y ) * k );
	origin->x = TheDisplay->getWidth() - cols * cell->x - *left;
	origin->y = *bottom - *foot - rows * cell->y - *top;
}

//-------------------------------------------------------------------------------------------------
/** Control of a player changed mid-game, so the general's powers bar has to be there this frame.
  * populateSpecialPowerShortcut() slides it in over half a second whenever it finds it hidden -
  * unhiding it first means it simply appears, already filled in. */
//-------------------------------------------------------------------------------------------------
void ControlBar::showSpecialPowerShortcutInstantly( Player *player )
{
	if( m_specialPowerShortcutParent && player
			&& (player->hasAnyShortcutSpecialPower() || hasAnyShortcutSelection())
			&& m_contextParent[ CP_MASTER ] && !m_contextParent[ CP_MASTER ]->winIsHidden() )
	{
		m_specialPowerShortcutParent->winHide( FALSE );
	}
	populateSpecialPowerShortcut( player );
}

void ControlBar::populateSpecialPowerShortcut( Player *player)
{
	const CommandSet *commandSet;
	Int i;

	//
	// An armed row survives this. markUIDirty() runs the bar through here again for anything that
	// touches the interface at all - a promotion, a unit finishing, a structure captured, a stealth
	// unit blinking - and dozens of those happen a minute, so clearing the row here meant the second
	// key of the chord almost never found one. What actually invalidates a row is the bar going away
	// or coming back with fewer powers than the row names, and update() drops it for both.
	//
	if(!player || !player->getPlayerTemplate()
			|| !player->isLocalPlayer() || m_currentlyUsedSpecialPowersButtons == 0
			|| m_specialPowerShortcutButtons == NULL || m_specialPowerShortcutButtonParents == NULL)
		return;
	for( i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; ++i )
	{
		if (m_specialPowerShortcutButtons[i])
			m_specialPowerShortcutButtons[i]->winHide(TRUE);
		if (m_specialPowerShortcutButtonParents[i])
			m_specialPowerShortcutButtonParents[i]->winHide(TRUE);
		
	}
	
	// get command set
	if(player->getPlayerTemplate()->getSpecialPowerShortcutCommandSet().isEmpty() )
		return;
	commandSet = TheControlBar->findCommandSet(player->getPlayerTemplate()->getSpecialPowerShortcutCommandSet()); // TEMP WILL CHANGE TO PROPER WAY ONCE WORKING
	if(!commandSet)
		return;
	// populate the button with commands defined
	Int currentButton = 0;
	std::vector<SpecialPowerType> shownPowerTypes;
	const CommandButton *commandButton;
	for( i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{

		// get command button
		commandButton = commandSet->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == NULL )
		{
			continue;
			// hide window on interface
			//m_specialPowerShortcutButtons[ i ]->winHide( TRUE );

		}  // end if
		else
		{

			if( BitTest( commandButton->getOptions(), NEED_UPGRADE ) )
			{
				const UpgradeTemplate *upgrade = commandButton->getUpgradeTemplate();
				if( upgrade && !ThePlayerList->getLocalPlayer()->hasUpgradeComplete( upgrade->getUpgradeMask() ) )
				{
					//Kris: 8/13/03 - Don't show shortcut buttons that require upgrades we don't have. As far as 
					//I know, only the radar van scan has this. The MOAB is handled differently (sciences).
					continue;
				}
			}

			//
			// commands that require sciences we don't have are hidden so they never show up
			// cause we can never pick "another" general technology throughout the game
			//
			if( BitTest( commandButton->getOptions(), NEED_SPECIAL_POWER_SCIENCE ) )
			{
				const SpecialPowerTemplate *power = commandButton->getSpecialPowerTemplate();

				if( !power )
				{
					//Should have the power.. button is probably missing the SpecialPower = xxx entry.
					DEBUG_CRASH( ("CommandButton %s needs a SpecialPower entry, but it's either incorrect or missing.", commandButton->getName().str()) );
					continue;
				}

				//We just need to find something that has the power.
				Object *obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( commandButton->getSpecialPowerTemplate()->getSpecialPowerType() );
				if( !obj )
				{
					continue;
				}

				if( power->getRequiredScience() != SCIENCE_INVALID )
				{
					if( player->hasScience( power->getRequiredScience() ) == FALSE )
					{
						//Hide the power
						//m_specialPowerShortcutButtons[ i ]->winHide( TRUE );
						continue;
					}
					else
					{
						//The player does have the special power! Now determine if the images require
						//enhancement based on upgraded versions. This is determined by the command
						//button specifying a vector of sciences in the command button.
						Int bestIndex = -1;
						ScienceType science;
						for( Int scienceIndex = 0; scienceIndex < commandButton->getScienceVec().size(); ++scienceIndex )
						{
							science = commandButton->getScienceVec()[ scienceIndex ];
							
							//Keep going until we reach the end or don't have the required science!
							if( player->hasScience( science ) )
							{
								bestIndex = scienceIndex;
							}
							else
							{
								break;
							}
						}

						if( bestIndex != -1 )
						{
							//Now get the best sciencetype.
							science = commandButton->getScienceVec()[ bestIndex ];

							const CommandSet *commandSet1;
							const CommandSet *commandSet3;
							const CommandSet *commandSet8;
							Int i;

							// get command set
							if( !player || !player->getPlayerTemplate() 
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1().isEmpty()
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3().isEmpty()
									|| player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8().isEmpty() )
							{
								continue;
							}
							commandSet1 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank1() ); 
							commandSet3 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank3() ); 
							commandSet8 = TheControlBar->findCommandSet( player->getPlayerTemplate()->getPurchaseScienceCommandSetRank8() ); 

							if( !commandSet1 || !commandSet3 || !commandSet8 )
							{
								continue;
							}

							Bool found = FALSE;
							for( i = 0; !found && i < MAX_COMMANDS_PER_SET; i++ )
							{
								const CommandButton *command = commandSet1->getCommandButton( i );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
							for( i = 0; !found && i < MAX_COMMANDS_PER_SET; i++ )
							{
								const CommandButton *command = commandSet3->getCommandButton( i );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
							for( i = 0; !found && i < MAX_COMMANDS_PER_SET; i++ )
							{
								const CommandButton *command = commandSet8->getCommandButton( i );
								if( command && command->getCommandType() == GUI_COMMAND_PURCHASE_SCIENCE )
								{
									//All purchase sciences specify a single science.
									if( command->getScienceVec().empty() )
									{
										DEBUG_CRASH( ("Commandbutton %s is a purchase science button without any science! Please add it.", command->getName().str() ) );
									}
									else if( command->getScienceVec()[0] == science )
									{
										commandButton->copyImagesFrom( command, TRUE );
										commandButton->copyButtonTextFrom( command, TRUE, TRUE );
										found = TRUE;
										break;
									}
								}
							}
						}
					}
				}
			}  // end if			
			else if( commandButton->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
			{
				//Make sure we actually have an object of type that we want to be able to select.
				Object *obj = ThePlayerList->getLocalPlayer()->findAnyExistingObjectWithThingTemplate( commandButton->getThingTemplate() );
				if( !obj )
				{
					continue;
				}
			}

			if( commandButton->getSpecialPowerTemplate() )
				shownPowerTypes.push_back( commandButton->getSpecialPowerTemplate()->getSpecialPowerType() );

			// make sure the window is not hidden
			m_specialPowerShortcutButtons[ currentButton ]->winHide( FALSE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winHide( FALSE );
			// enable by default
			m_specialPowerShortcutButtons[ currentButton ]->winEnable( TRUE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winEnable( TRUE );

			// populate the visible button with data from the command button
			setControlCommand( m_specialPowerShortcutButtons[ currentButton ], commandButton );
			GadgetButtonSetAltSound(m_specialPowerShortcutButtons[ currentButton ], "GUIGenShortcutClick");
			currentButton++;

		}  // end else

	}  // end for i

	//
	// A superweapon the player took rather than built: a GLA player who captures a nuclear silo
	// owns a power his own faction's shortcut set has no button for, so the silo could be fired
	// only by finding it on the map and selecting it.  Every other faction's shortcut set is asked
	// for a button that fires a power this player now holds and nothing above has shown.  Powers
	// behind a science are left out: a general's power is bought, never captured.  "Holds" means
	// the very power the button names, not one of its kind: the few power types are shared by
	// unrelated powers, and a type match put another faction's button in the tray that update then
	// hid, an empty slot left behind.
	//
	for( Int t = 0; t < ThePlayerTemplateStore->getPlayerTemplateCount(); ++t )
	{
		const PlayerTemplate *otherFaction = ThePlayerTemplateStore->getNthPlayerTemplate( t );
		if( otherFaction == player->getPlayerTemplate() || otherFaction->getSpecialPowerShortcutCommandSet().isEmpty() )
			continue;
		const CommandSet *otherSet = findCommandSet( otherFaction->getSpecialPowerShortcutCommandSet() );
		if( otherSet == NULL )
			continue;

		for( Int b = 0; b < MAX_COMMANDS_PER_SET && currentButton < m_currentlyUsedSpecialPowersButtons; ++b )
		{
			const CommandButton *captured = otherSet->getCommandButton( b );
			if( captured == NULL || !BitTest( captured->getOptions(), NEED_SPECIAL_POWER_SCIENCE ) )
				continue;
			const SpecialPowerTemplate *power = captured->getSpecialPowerTemplate();
			if( power == NULL || power->getRequiredScience() != SCIENCE_INVALID )
				continue;
			const SpecialPowerType type = power->getSpecialPowerType();
			if( std::find( shownPowerTypes.begin(), shownPowerTypes.end(), type ) != shownPowerTypes.end() )
				continue;
			const Object *holder = player->findMostReadyShortcutSpecialPowerOfType( type );
			if( holder == NULL || holder->getSpecialPowerModule( power ) == NULL )
				continue;

			shownPowerTypes.push_back( type );
			m_specialPowerShortcutButtons[ currentButton ]->winHide( FALSE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winHide( FALSE );
			m_specialPowerShortcutButtons[ currentButton ]->winEnable( TRUE );
			m_specialPowerShortcutButtonParents[ currentButton ]->winEnable( TRUE );
			setControlCommand( m_specialPowerShortcutButtons[ currentButton ], captured );
			GadgetButtonSetAltSound( m_specialPowerShortcutButtons[ currentButton ], "GUIGenShortcutClick" );
			currentButton++;
		}
	}

	if(m_contextParent[ CP_MASTER ] && !m_contextParent[ CP_MASTER ]->winIsHidden() && m_specialPowerShortcutParent->winIsHidden())
	{
		showSpecialPowerShortcut();
		animateSpecialPowerShortcut(TRUE);
	}
	updateSpecialPowerShortcut();
	rearrangeSpecialPowerShortcutGrid();
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::hasAnyShortcutSelection() const
{
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;

		win = m_specialPowerShortcutButtons[ i ];
		if( win->winIsHidden() == TRUE )
			continue;

		// get the command from the control
		command = (const CommandButton *)GadgetButtonGetData(win);
		if( !command )
			continue;

		if( command->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		{
			//We found one, so we'll always show shortcuts!
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::updateSpecialPowerShortcut( void )
{
	if(!m_specialPowerShortcutParent || !m_specialPowerShortcutButtons 
	   || !ThePlayerList || !ThePlayerList->getLocalPlayer())
		return;

	Bool hasShortcutSelectionButtons = hasAnyShortcutSelection();
	Bool hasAnyShortcutSpecialPower = ThePlayerList->getLocalPlayer()->hasAnyShortcutSpecialPower();

	Bool hasValidShortcutButton = hasShortcutSelectionButtons || hasAnyShortcutSpecialPower;

	if( hasValidShortcutButton
		  && m_specialPowerShortcutParent->winIsHidden() 
			&& m_contextParent[ CP_MASTER ] 
			&& !m_contextParent[ CP_MASTER ]->winIsHidden() )
	{
		showSpecialPowerShortcut();
		animateSpecialPowerShortcut(TRUE);
	}
	else if( !hasValidShortcutButton 
					 && !m_specialPowerShortcutParent->winIsHidden()
					 && m_animateWindowManagerForGenShortcuts
					 && m_animateWindowManagerForGenShortcuts->isFinished() )
	{
		animateSpecialPowerShortcut(FALSE);		
	}

	if(m_specialPowerShortcutParent->winIsHidden())
		return;

	if(!ThePlayerList->getLocalPlayer()->isPlayerActive())
	{
		hideSpecialPowerShortcut();
		return;
	}
	if(m_contextParent[ CP_MASTER ] && !m_contextParent[ CP_MASTER ]->winIsHidden() && m_specialPowerShortcutParent->winIsHidden())
		showSpecialPowerShortcut();

	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;
		// get the window
		win = m_specialPowerShortcutButtons[ i ];

		if( win->winIsHidden() == TRUE )
			continue;
		// get the command from the control
		command = (const CommandButton *)GadgetButtonGetData(win);
		//command = (const CommandButton *)win->winGetUserData();
		if( command == NULL )
			continue;

		// the clock is no longer eaten by the draw, so take last pass's off first
		GadgetButtonClearClock( win );

		win->winClearStatus( WIN_STATUS_NOT_READY );
		win->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
		

		// is the command available

		CommandAvailability availability = COMMAND_RESTRICTED;

		const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();
		Object *obj = NULL; 
		if( spTemplate )
		{
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
			availability = getCommandAvailability( command, obj, win );
		}
		else if( command->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE )
		{
			availability = COMMAND_HIDDEN;
			Object *obj = ThePlayerList->getLocalPlayer()->findAnyExistingObjectWithThingTemplate( command->getThingTemplate() );
			if( obj )
			{
				//Make command available if it isn't a special power template shortcut power.
				availability = COMMAND_AVAILABLE;

				UnsignedInt mostReadyPercentage;
				obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerForThing( command->getThingTemplate(), mostReadyPercentage );
				if( obj )
				{
					//Ugh... hacky.
					//Look for a command button for a special power and if so, then get the command availability for it.
					const CommandSet *commandSet = TheControlBar->findCommandSet( obj->getCommandSetString() );
					if( commandSet )
					{
						for( Int commandIndex = 0; commandIndex < MAX_COMMANDS_PER_SET; commandIndex++ )
						{
							const CommandButton *evalButton = commandSet->getCommandButton( commandIndex );
							GameWindow *evalButtonWin = m_commandWindows[ commandIndex ];
							if( evalButton && evalButton->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
							{
								//We want to evaluate the special powerbutton... but apply the clock overlay to our button!
								availability = getCommandAvailability( evalButton, obj, evalButtonWin, win );
								break;
							}
						}
					}
				}
			}
		}

		// enable/disable the window control
		switch( availability )
		{
			case COMMAND_HIDDEN:
				// the slot's frame goes with its button, or the tray shows an empty slot
				win->winHide( TRUE );
				m_specialPowerShortcutButtonParents[ i ]->winHide( TRUE );
				break;
			case COMMAND_RESTRICTED:
				win->winEnable( FALSE );
				break;
			case COMMAND_NOT_READY:
				win->winEnable( FALSE );
				win->winSetStatus( WIN_STATUS_NOT_READY );
				break;
			case COMMAND_CANT_AFFORD:
				win->winEnable( FALSE );
				win->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
				break;
			default:
				win->winEnable( TRUE );
				break;
		}

	}
}

//-------------------------------------------------------------------------------------------------
void ControlBar::drawSpecialPowerShortcutMultiplierText()
{
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; i++ )
	{
		GameWindow *win;
		const CommandButton *command;
		// get the window
		win = m_specialPowerShortcutButtons[ i ];

		if( win->winIsHidden() == TRUE )
			continue;
		// get the command from the control
		command = (const CommandButton *)GadgetButtonGetData(win);
		//command = (const CommandButton *)win->winGetUserData();
		if( command == NULL )
			continue;

		// the button's text: how many of its superweapons are ready, as the game shipped - a "1" is
		// superfluous (Lorenzen)
		UnicodeString text;

		const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();
		Int numReady = 0;
		if( spTemplate )
			numReady = ThePlayerList->getLocalPlayer()->countReadyShortcutSpecialPowersOfType( spTemplate->getSpecialPowerType() );
		if( numReady > 1 )
			text.format( u"%d", numReady );

		GadgetButtonSetText( win, text );
	}
}

void ControlBar::animateSpecialPowerShortcut( Bool isOn )
{
	if(!m_specialPowerShortcutParent || !m_animateWindowManagerForGenShortcuts || !m_currentlyUsedSpecialPowersButtons)
		return;
	Bool dontAnimate = TRUE;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; ++i )
	{
		if (m_specialPowerShortcutButtons[i]->winGetUserData())
		{
			dontAnimate = FALSE;
			break;
		}
	}
	if(dontAnimate)
		return;

	if(isOn)
	{	
		m_animateWindowManagerForGenShortcuts->reset();
		m_animateWindowManagerForGenShortcuts->registerGameWindow(m_specialPowerShortcutParent,WIN_ANIMATION_SLIDE_RIGHT,TRUE,500,0);
	}
	else
	{
		m_animateWindowManagerForGenShortcuts->reverseAnimateWindow();
	}
}

void ControlBar::showSpecialPowerShortcut( void )
{
	if(TheScriptEngine->isGameEnding() || !m_specialPowerShortcutParent 
		||!m_specialPowerShortcutButtons || !ThePlayerList || !ThePlayerList->getLocalPlayer())
		return;
	Bool dontAnimate = TRUE;
	for( Int i = 0; i < m_currentlyUsedSpecialPowersButtons; ++i )
	{
		if (m_specialPowerShortcutButtons[i]->winGetUserData())
		{
			dontAnimate = FALSE;
			break;
		}
	}
	if( dontAnimate || (!ThePlayerList->getLocalPlayer()->hasAnyShortcutSpecialPower() && !hasAnyShortcutSelection()) )
		return;
	m_specialPowerShortcutParent->winHide(FALSE);
	populateSpecialPowerShortcut(ThePlayerList->getLocalPlayer());
		
}

void ControlBar::hideSpecialPowerShortcut( void )
{
	LOGIC_CENSUS( CENSUS_CONTROLBAR );
	if(!m_specialPowerShortcutParent)
		return;
	
	m_specialPowerShortcutParent->winHide(TRUE);
		
}
