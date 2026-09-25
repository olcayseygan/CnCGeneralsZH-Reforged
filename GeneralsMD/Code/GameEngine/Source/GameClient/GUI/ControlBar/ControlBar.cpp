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

// FILE: ControlBar.cpp ///////////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive command interface
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

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
#include "Common/OVERRIDE.h"
#include "Common/PlayerTemplate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ProductionPrerequisite.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "Common/Recorder.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"

static Bool builderIsFree( AIUpdateInterface *ai, DozerAIInterface *dozer );	// defined with findStandInBuilder
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
/** The key command bar slot 'slot' answers to right now, MK_NONE when none does.  Read live out of
	the meta map, which is where CommandMap.ini puts the grid: a second copy of those letters here
	is a copy that goes stale, and one did - the chord's second keys stayed Q A W S E D R F long
	after the bottom row of the grid had moved to Z X C V B N M, so half the cells answered to a
	letter that was not written on them and the letter that was fell straight through to the button
	underneath.  Rebinding a slot in Options > Keyboard now moves its chord with it. */
//-------------------------------------------------------------------------------------------------
static MappableKeyType getGridHotKey( Int slot )
{
	if( TheMetaMap == NULL || slot < 0 || slot >= MAX_COMMANDS_PER_SET )
		return MK_NONE;

	const GameMessage::Type wanted =
		(GameMessage::Type)( GameMessage::MSG_META_COMMAND_SLOT01 + slot );

	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
		if( rec->m_meta == wanted && rec->m_modState == 0 )
			return rec->m_key;

	return MK_NONE;

}  // end getGridHotKey

//-------------------------------------------------------------------------------------------------
/** Which slot a press on the grid key for 'index' lands on with the structure chord as it
	stands, or SLOT_ARMS_CHORD for a press that would only start one, or SLOT_NOTHING.  Changes
	nothing: pressCommandButton acts on the answer and peekCommandButtonPress reports it. */
//-------------------------------------------------------------------------------------------------
Int ControlBar::resolveGridPress( Int index, Int chordGroup, Bool hasStructures, Bool indexIsStructure )
{
	if( index < 0 || index >= MAX_COMMANDS_PER_SET )
		return SLOT_NOTHING;

	//
	// a builder's structures are reached by a two-key chord so the whole set can stay on
	// screen: Q arms the structures in columns 1-4 (slots 0-7), W the ones in columns 5-7
	// (slots 8-13), and the next grid key picks the cell inside that group by its own
	// position - Q-Q, Q-Z, ... W-Q (= T's cell), W-Z (= B's cell) ...  A structure has no other
	// way in: its own letter on its own is the second half of a chord nobody started.  Q and W
	// are whatever the grid binds to slots 0 and 2, so with W A S D on the camera they are Q and E.
	//
	if( !hasStructures )
		return index;

	if( chordGroup < 0 )
	{
		if( index == CHORD_SLOT_Q || index == CHORD_SLOT_W )
			return SLOT_ARMS_CHORD;

		//
		// Every structure is two keys, and only two.  The key painted on a cell is the *second*
		// of its pair, so on its own it used to fall through to whatever sits in the slot that
		// key names - which for a builder is another structure.  So the same building could be
		// put up either by the chord written on it or by one bare letter nobody wrote anywhere,
		// and a key pressed after a Q that had already been dropped built something.
		//
		return indexIsStructure ? SLOT_NOTHING : index;
	}

	if( index >= CHORD_GROUP_SIZE )
		return SLOT_NOTHING;		// the second key must be one of the first group's cells (Q W E R A S D F)
	index += chordGroup * CHORD_GROUP_SIZE;
	return index < MAX_COMMANDS_PER_SET ? index : SLOT_NOTHING;

}  // end resolveGridPress

//-------------------------------------------------------------------------------------------------
Int ControlBar::resolveCommandSlot( Int index, Bool *hasStructures ) const
{
	*hasStructures = FALSE;
	Bool indexIsStructure = FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		GameWindow *w = m_commandWindows[ i ];
		if( w == NULL || BitTest( w->winGetStatus(), WIN_STATUS_HIDDEN ) )
			continue;
		const CommandButton *c = (const CommandButton *)GadgetButtonGetData( w );
		if( c == NULL || c->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT )
			continue;
		*hasStructures = TRUE;
		if( i == index )
			indexIsStructure = TRUE;
	}
	return resolveGridPress( index, m_chordGroup, *hasStructures, indexIsStructure );

}  // end resolveCommandSlot

//-------------------------------------------------------------------------------------------------
ControlBar::PressOutcome ControlBar::peekCommandButtonPress( Int index, GameWindow **button )
{
	*button = NULL;

	Bool hasStructures = FALSE;
	const Int slot = resolveCommandSlot( index, &hasStructures );
	if( slot == SLOT_NOTHING )
		return PRESS_DOES_NOTHING;

	if( slot == SLOT_ARMS_CHORD )
	{
		// the first cell of the group this key would arm that the second key could press
		const Int first = ( index == CHORD_SLOT_Q ? 0 : 1 ) * CHORD_GROUP_SIZE;
		for( Int i = first; i < first + CHORD_GROUP_SIZE && i < MAX_COMMANDS_PER_SET; i++ )
		{
			GameWindow *w = m_commandWindows[ i ];
			if( w && !BitTest( w->winGetStatus(), WIN_STATUS_HIDDEN )
					&& BitTest( w->winGetStatus(), WIN_STATUS_ENABLED ) )
			{
				*button = w;
				break;
			}
		}
		return PRESS_ARMS_CHORD;
	}

	GameWindow *win = m_commandWindows[ slot ];
	if( win == NULL || BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
		return PRESS_DOES_NOTHING;

	*button = win;
	return BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) ? PRESS_FIRES : PRESS_IS_REFUSED;

}  // end peekCommandButtonPress

//-------------------------------------------------------------------------------------------------
/** Press a command bar button by slot index.  Mirrors HotKeyManager::executeHotKey: a hidden
	slot does nothing at all, an enabled one gets the same GBM_SELECTED the mouse would send,
	and a disabled one just makes the rejection noise. */
//-------------------------------------------------------------------------------------------------
void ControlBar::pressCommandButton( Int index )
{
	// a press that names no slot at all leaves an armed chord alone
	if( index < 0 || index >= MAX_COMMANDS_PER_SET )
		return;

	Bool hasStructures = FALSE;
	const Int slot = resolveCommandSlot( index, &hasStructures );

	if( slot == SLOT_ARMS_CHORD )
	{
		m_chordGroup = ( index == CHORD_SLOT_Q ) ? 0 : 1;
		m_chordStartMs = timeGetTime();
		m_chordDrawableID = m_currentSelectedDrawable ? m_currentSelectedDrawable->getID()
																								 : INVALID_DRAWABLE_ID;
		markUIDirty();		// the group that is armed greys the other one out
		return;
	}

	// an armed chord is spent by its second key whether or not that key found a cell
	if( hasStructures && m_chordGroup >= 0 )
		dropChord();

	if( slot == SLOT_NOTHING )
		return;

	GameWindow *win = m_commandWindows[ slot ];
	if( win == NULL || BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
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

//-------------------------------------------------------------------------------------------------
/** The second key of a structure chord is the cell's own position inside the group, so it is
	whatever the grid has bound to slots 0..7 - Q Z W X E C R V on the shipped map, which is what is
	painted on the cells.  MetaEventTranslator hands the raw key here while a chord is armed. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::handleChordKey( Int mappableKey )
{
	if( m_chordGroup < 0 )
		return FALSE;

	for( Int i = 0; i < CHORD_GROUP_SIZE; i++ )
	{
		const MappableKeyType key = getGridHotKey( i );

		if( key != MK_NONE && (Int)key == mappableKey )
		{
			pressCommandButton( i );	// resolves the chord: i + group * CHORD_GROUP_SIZE
			return TRUE;
		}
	}

	//
	// Any other key means the player is done with the chord, so drop it and let the key through.
	// It used to be left armed: nothing in the game cleared it except another grid key or a
	// context change, so a Q pressed and thought better of stayed armed for the rest of the
	// game and the next A or S - attack move, stop - silently became "place this structure",
	// which took the selection away and looked like the click had been eaten.
	//
	dropChord();
	return FALSE;

}  // end handleChordKey

//-------------------------------------------------------------------------------------------------
/** Forget a half-typed structure chord. */
//-------------------------------------------------------------------------------------------------
void ControlBar::dropChord( void )
{
	if( m_chordGroup < 0 )
		return;

	m_chordGroup = -1;
	m_chordDrawableID = INVALID_DRAWABLE_ID;
	markUIDirty();		// the greyed-out half of the structures comes back

}  // end dropChord

//-------------------------------------------------------------------------------------------------
/** Build the menu and back buttons a paged builder shows.  They carry no thing template and no
	text label of their own - the page they open is told by the cameo of its first structure, and
	by the grid letter setControlCommand paints in the corner. */
//-------------------------------------------------------------------------------------------------
void ControlBar::makeBuildPageButtons( void )
{
	AsciiString name;
	for( Int page = 0; page < BUILD_PAGE_COUNT; page++ )
	{
		name.format( "NonCommand_BuildPage%d", page + 1 );
		m_buildPageButton[ page ] = newCommandButton( name );
		m_buildPageButton[ page ]->friend_setCommandType( GUI_COMMAND_BUILD_PAGE );
		m_buildPageButton[ page ]->friend_setBorderType( COMMAND_BUTTON_BORDER_SYSTEM );
	}

	m_buildPageBackButton = newCommandButton( AsciiString( "NonCommand_BuildPageBack" ) );
	m_buildPageBackButton->friend_setCommandType( GUI_COMMAND_BUILD_PAGE );
	m_buildPageBackButton->friend_setBorderType( COMMAND_BUTTON_BORDER_SYSTEM );

}  // end makeBuildPageButtons

//-------------------------------------------------------------------------------------------------
/** Show 'page' of the selected builder's structures, BUILD_PAGE_ROOT for the menu buttons. */
//-------------------------------------------------------------------------------------------------
void ControlBar::setBuildPage( Int page )
{
	if( page < BUILD_PAGE_ROOT || page >= BUILD_PAGE_COUNT )
		page = BUILD_PAGE_ROOT;

	if( m_buildPage == page )
		return;

	m_buildPage = page;
	markUIDirty();

}  // end setBuildPage

/// mark the UI as dirty so the context of everything is re-evaluated
void ControlBar::markUIDirty( void )
{ 
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
/** Watching, the selection drives the whole bar: clicking a unit watches its owner.  The
	* portrait's panel comes up and the bar wears his side's metal, and clicking empty ground puts
	* everybody and the watcher's own plain bar back.
	*
	* Before this, picking up somebody's tank told you nothing about him: the bar stayed on whatever
	* side had last been chosen off the list, and the money plate with it. */
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

	const PlayerTemplate *wear = player ? player->getPlayerTemplate()
																			: ThePlayerList->getLocalPlayer()->getPlayerTemplate();
	if( m_controlBarSchemeManager && wear && wear->getSide().compare( m_watchedSide ) != 0 )
	{
		// a scheme lays the whole bar out again, so it is set when the side really changes and not
		// on every click
		m_watchedSide = wear->getSide();
		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate( wear );
		restoreStageAfterScheme();
	}

	// a seat clicked at the top clears the selection before it gets here, so what is selected is
	// read again rather than taken from the caller; the portrait's panel is the selection's
	m_watchedSelection = getSelectedPlayer();
	if( m_currentControlBarStage == CONTROL_BAR_STAGE_DEFAULT )
		showPanel( CB_PANEL_RIGHT, m_watchedSelection != NULL );
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

	// populate the button with commands defined
	const CommandButton *commandButton;
	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
	{

		// get command button
		commandButton = commandSet1->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == NULL || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank1[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank1[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank1[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank1[ i ], commandButton );
			if (!commandButton->getScienceVec().empty())
			{
				ScienceType	st = commandButton->getScienceVec()[ 0 ];

				if( player->isScienceDisabled( st ) )
				{
					//A script has deemed this science disabled.
					m_sciencePurchaseWindowsRank1[ i ]->winEnable( FALSE );
				}
				else if( player->isScienceHidden( st ) )
				{
					//A script has deemed this science unavailable, thus hidden
					m_sciencePurchaseWindowsRank1[ i ]->winHide( TRUE );
				}
				else
				{
					//Handle normal game logic cases!
					if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
					{
						m_sciencePurchaseWindowsRank1[ i ]->winEnable( TRUE );
					}
					if(player->hasScience(st))
					{
						m_sciencePurchaseWindowsRank1[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
					}
					else
					{
						m_sciencePurchaseWindowsRank1[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
					}
					if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
						m_sciencePurchaseWindowsRank1[ i ]->winHide(TRUE);
				}
			}
		}  // end else

	}  // end for

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
	{

		// get command button
		commandButton = commandSet3->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == NULL || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank3[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank3[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank3[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank3[ i ], commandButton );
			ScienceType	st = SCIENCE_INVALID; 
			ScienceVec sv = commandButton->getScienceVec();
			if (! sv.empty())
			{
				st = sv[ 0 ];
			}

			if( player->isScienceDisabled( st ) )
			{
				//A script has deemed this science disabled.
				m_sciencePurchaseWindowsRank3[ i ]->winEnable( FALSE );
			}
			else if( player->isScienceHidden( st ) )
			{
				//A script has deemed this science unavailable, thus hidden
				m_sciencePurchaseWindowsRank3[ i ]->winHide( TRUE );
			}
			else
			{
				//Handle normal game logic cases!
				if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
				{
					m_sciencePurchaseWindowsRank3[ i ]->winEnable( TRUE );
				}
				if(player->hasScience(st))
				{
					m_sciencePurchaseWindowsRank3[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				else
				{
					m_sciencePurchaseWindowsRank3[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
					m_sciencePurchaseWindowsRank3[ i ]->winHide(TRUE);
			}

		}  // end else

	}  // end for

	for( i = 0; i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
	{

		// get command button
		commandButton = commandSet8->getCommandButton(i);

		// if button is not present, just hide the window
		if( commandButton == NULL || BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
		{
			// hide window on interface
			m_sciencePurchaseWindowsRank8[ i ]->winHide( TRUE );
		}  // end if
		else
		{
			// make sure the window is not hidden
			m_sciencePurchaseWindowsRank8[ i ]->winHide( FALSE );

			// Disable by default
			m_sciencePurchaseWindowsRank8[ i ]->winEnable( FALSE );

			// populate the visible button with data from the command button

			setControlCommand( m_sciencePurchaseWindowsRank8[ i ], commandButton );
			ScienceType	st = SCIENCE_INVALID; 
			st = commandButton->getScienceVec()[ 0 ];
			if( player->isScienceDisabled( st ) )
			{
				//A script has deemed this science disabled.
				m_sciencePurchaseWindowsRank8[ i ]->winEnable( FALSE );
			}
			else if( player->isScienceHidden( st ) )
			{
				//A script has deemed this science unavailable, thus hidden
				m_sciencePurchaseWindowsRank8[ i ]->winHide( TRUE );
			}
			else
			{
				//Handle normal game logic cases!
				if(!player->hasScience(st) && TheScienceStore->playerHasPrereqsForScience(player, st) && TheScienceStore->getSciencePurchaseCost(st) <= player->getSciencePurchasePoints())
				{
					m_sciencePurchaseWindowsRank8[ i ]->winEnable( TRUE );
				}
				if(player->hasScience(st))
				{
					m_sciencePurchaseWindowsRank8[ i ]->winSetStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				else
				{
					m_sciencePurchaseWindowsRank8[ i ]->winClearStatus(WIN_STATUS_ALWAYS_COLOR);
				}
				if(!TheScienceStore->playerHasRootPrereqsForScience(player, st))
					m_sciencePurchaseWindowsRank8[ i ]->winHide(TRUE);
			}

		}  // end else

	}  // end for

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
		tempUS.format(L"%d", player->getSciencePurchasePoints());
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
		Int progress;
		progress = ((player->getSkillPoints() - player->getSkillPointsLevelDown()) * 100) /(player->getSkillPointsLevelUp() - player->getSkillPointsLevelDown());
		GadgetProgressBarSetProgress(win, progress);
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
		Int progress;
		progress = ((player->getSkillPoints() - player->getSkillPointsLevelDown()) * 100) /(player->getSkillPointsLevelUp() - player->getSkillPointsLevelDown());
		GadgetProgressBarSetProgress(win, progress);
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

		if( stricmp( TheGuiCommandNames[ i ], token ) == 0 )
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
	const CommandButton *commandButton = TheControlBar->findCommandButton( AsciiString( token ) );
	if( commandButton == NULL )
	{

		DEBUG_CRASH(( "[LINE: %d - FILE: '%s'] Unknown command '%s' found in command set\n",
								  ini->getLineNum(), ini->getFilename().str(), token ));
		throw INI_INVALID_DATA;

	}  // end if

	// get the index to store the command at, and the command array itself
	const CommandButton **buttonArray = (const CommandButton **)store;
	Int buttonIndex = (Int)userData;

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
	m_commandButtons = NULL;
	m_commandSets = NULL;
	m_controlBarSchemeManager = NULL;
	m_isObserverCommandBar = FALSE;
	m_observerLookAtPlayer = NULL;
	m_watchedSide.clear();
	m_watchedSelection = NULL;
	m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;
	m_buildToolTipLayout = NULL;
	m_showBuildToolTipLayout = FALSE;
	m_boardCardButton = NULL;
	m_boardCardOwner = NULL;
	m_boardCardShown = FALSE;
	m_boardCardWasShown = FALSE;

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
		m_panelHidden[ i ] = FALSE;
		m_panelSlide[ i ] = 0.0f;
		m_panelSlideTo[ i ] = 0.0f;
		m_panelDropCap[ i ] = 0;
	}
	m_panelSlideMs = 0;
	m_panelOrigin.x = m_panelOrigin.y = 0;
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		m_commandWindows[ i ] = NULL;
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
	m_borrowedTrayCount = 0;
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
	for( i = 0; i < BUILD_PAGE_COUNT; i++ )
		m_buildPageButton[ i ] = NULL;
	m_buildPageBackButton = NULL;
	m_chordGroup = -1;
	m_chordStartMs = 0;
	m_chordDrawableID = INVALID_DRAWABLE_ID;
	m_standInBuilderID = INVALID_DRAWABLE_ID;
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
	m_buildPage = BUILD_PAGE_ROOT;
	m_buildPageObjectID = INVALID_ID;
	m_rallyPointDrawableID = INVALID_DRAWABLE_ID;
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

	m_radarAttackGlowOn = FALSE;
	m_remainingRadarAttackGlowFrames = 0;
	m_radarAttackGlowWindow = NULL;
	m_pageSolidsActive = FALSE;

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

	// destroy all the command set definitions
	CommandSet *set;
	while( m_commandSets )
	{
		set = m_commandSets->friend_getNext();
		m_commandSets->deleteInstance();
		m_commandSets = set;

	}  // end while

	// destroy all our command button definitions
	CommandButton *button;
	while( m_commandButtons )
	{
		button = m_commandButtons->friend_getNext();
		m_commandButtons->deleteInstance();
		m_commandButtons = button;

	}  // end while

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
/** One window of a layout being taken out of the loader's stretched space, and everything under it.
	* Positions are relative to the parent, so both the parent's old and its new screen origin travel
	* down the recursion - the same walk placeInPanel does, without the panels and the plate art. */
//-------------------------------------------------------------------------------------------------
static void layoutUniformWindow( GameWindow *win, Real originX, Real originY, Real s,
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

	const Int newX = REAL_TO_INT_FLOOR( originX + designX * s );
	const Int newY = REAL_TO_INT_FLOOR( originY + designY * s );

	win->winSetPosition( newX - newParentX, newY - newParentY );
	win->winSetSize( REAL_TO_INT_CEIL( designW * s ), REAL_TO_INT_CEIL( designH * s ) );

	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
		layoutUniformWindow( child, originX, originY, s, loadScaleX, loadScaleY,
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
	const Real s = ControlBarUniformScale();

	//
	// The anchor is a point that does not move: the same fraction of the screen and of the design
	// space.  (1,1) keeps the bottom right corner where it was however wide the screen is, which is
	// what a bar hanging off the right edge above the command bar wants.
	//
	const Real originX = dispW * anchorFracX - CONTROL_BAR_DESIGN_W * anchorFracX * s;
	const Real originY = dispH * anchorFracY - CONTROL_BAR_DESIGN_H * anchorFracY * s;

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

	layoutUniformWindow( root, originX, originY, s, loadScaleX, loadScaleY,
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
	const Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;
	const Real loadScaleY = dispH / CONTROL_BAR_DESIGN_H;
	const Real s = loadScaleX < loadScaleY ? loadScaleX : loadScaleY;

	const Real originX = dispW * thePanelAnchorFraction[ panel ]
											 - thePanelAnchorDesignX[ panel ] * s;

	rectOut->lo.x = REAL_TO_INT_FLOOR( originX + design->lo.x * s );
	rectOut->hi.x = REAL_TO_INT_CEIL ( originX + design->hi.x * s );
	rectOut->lo.y = REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - design->lo.y ) * s );
	rectOut->hi.y = REAL_TO_INT_CEIL ( dispH - ( CONTROL_BAR_DESIGN_H - design->hi.y ) * s );

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
	Int panel;						///< which of the three it was put in, so a panel can be hidden as a unit
	Bool weHid;						///< TRUE while the minimised bar is what is hiding it, and not the context
	Real designX, designY, designW, designH;
	Int placedX, placedY, placedW, placedH;
	Int slideApplied;			///< how far down applyPanelSlide has actually moved this one, in pixels
	ICoord2D inset;				///< how far insetPlacedWindow has put it inside the placed rectangle, each way, in pixels
};
typedef std::map< GameWindow *, ControlBarPanelPlacement > ControlBarPanelPlacementMap;
static ControlBarPanelPlacementMap theControlBarPlacement;

//-------------------------------------------------------------------------------------------------
/** How far layoutPanels has to lift a window to put it back where applyPanelSlide found it.  It is
	* what was recorded on the window, never what the panel's slide says now: applyPanelSlide leaves
	* windows behind - a plate marker is never moved, and a panel minimised with immediate = TRUE is
	* marked hidden before anything moves - and taking the panel's offset off one of those lifts a
	* window that never went down.  The bar then climbed a little further off the bottom of the
	* screen every time the layout was rebuilt, and the tooltip, placed against BackgroundMarker,
	* climbed with it until it left the screen.  Not static: the test calls it. */
//-------------------------------------------------------------------------------------------------
Int ControlBar_slideToUndo( Int slideAppliedToWindow, Int panelSlideOffsetNow )
{
	return slideAppliedToWindow;
}

//-------------------------------------------------------------------------------------------------
/** Which panel a direct child of ControlBarParent belongs to.  Everything not named here rides in
	* the middle, which is where the .wnd already puts the money, power and command windows - and
	* where the plate art puts the column of worker/beacon/chat/options slots.
	*
	* The three GameWinBlockInput panes have no name at all, and one of them sits over each panel in
	* the authored layout, so they go by where they were drawn: the pane that used to cover the radar
	* keeps covering the radar. */
//-------------------------------------------------------------------------------------------------
static Int panelForWindow( const char *shortName, Real designCenterX )
{
	static const char *leftNames[] =
	{
		"LeftHUD", "WinUAttack", "BackgroundMarker", "ForegroundMarker", "OnTopDraw", NULL
	};
	static const char *rightNames[] =
	{
		"RightHUD", "GeneralsExp", "ExpBarForeground", "ButtonGeneral",
		"ButtonSmall", "ButtonMedium", "ButtonLarge", NULL
	};

	if( shortName[ 0 ] == 0 )
	{
		for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
			if( designCenterX >= thePanelDesignRect[ p ].lo.x &&
					designCenterX <= thePanelDesignRect[ p ].hi.x )
				return p;
		return ControlBar::CB_PANEL_CENTER;
	}

	for( const char **n = leftNames; *n; n++ )
		if( strcmp( shortName, *n ) == 0 )
			return ControlBar::CB_PANEL_LEFT;

	for( const char **n = rightNames; *n; n++ )
		if( strcmp( shortName, *n ) == 0 )
			return ControlBar::CB_PANEL_RIGHT;

	return ControlBar::CB_PANEL_CENTER;
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
void ControlBar::setPageSolids( const std::vector< IRegion2D > *solids, const std::vector< IRegion2D > *holes )
{
	m_pageSolidsActive = solids != NULL;
	if( solids )
		m_pageSolids = *solids;
	else
		m_pageSolids.clear();
	if( holes )
		m_pageHoles = *holes;
	else
		m_pageHoles.clear();
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

	// the CSS page is what is drawn, so what it drew solid is what is solid.  Asked before the
	// children, because a see-through child the page does not draw - the observer's info window
	// spans the whole centre - would otherwise keep a click on bare battlefield
	if( m_pageSolidsActive )
	{
		// the page's own buttons hand their clicks on to the page, which takes them as clicks on
		// the world, even where they stand on a solid panel
		for( size_t hole = 0; hole < m_pageHoles.size(); hole++ )
		{
			const IRegion2D &rect = m_pageHoles[ hole ];
			if( x >= rect.lo.x && y >= rect.lo.y && x < rect.hi.x && y < rect.hi.y )
				return TRUE;
		}
		for( size_t solid = 0; solid < m_pageSolids.size(); solid++ )
		{
			const IRegion2D &rect = m_pageSolids[ solid ];
			if( x >= rect.lo.x && y >= rect.lo.y && x < rect.hi.x && y < rect.hi.y )
				return FALSE;
		}
		return TRUE;
	}

	// a command button or anything else inside the pane is its own window and keeps its click
	if( window->winPointInChild( x, y, TRUE ) != window )
		return FALSE;

	// the plates are drawn from design rectangles and travel with the frame and the slide, the same
	// arithmetic W3DCommandBarBackgroundDraw paints them with
	ICoord2D now;
	frame->winGetScreenPosition( &now.x, &now.y );
	for( Int p = 0; p < CB_PANEL_COUNT; p++ )
	{
		if( m_panelHidden[ p ] )
			continue;

		const ControlBarPlate *plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentSide(), p );
		if( plate == NULL )
			plate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentArtTwinSide(), p );
		IRegion2D rect;
		if( plate == NULL ||
				ControlBarPanelDesignToScreen( p, &plate->design, TheDisplay->getWidth(), TheDisplay->getHeight(), &rect ) == FALSE )
			continue;

		const Int left = rect.lo.x + now.x - m_panelOrigin.x;
		const Int top = rect.lo.y + now.y - m_panelOrigin.y + getPanelSlideOffset( p );
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
	const Real originX = dispW * thePanelAnchorFraction[ panel ]
											 - thePanelAnchorDesignX[ panel ] * s;

	ICoord2D rel, size;
	win->winGetPosition( &rel.x, &rel.y );
	win->winGetSize( &size.x, &size.y );

	// a window insetPlacedWindow put inside its place is read as the whole place, or every rebuild
	// would take the inset for what it was authored at and shrink it again
	ControlBarPanelPlacement &place = theControlBarPlacement[ win ];
	size.x += 2 * place.inset.x;
	size.y += 2 * place.inset.y;
	const Int oldX = oldParentX + rel.x - place.inset.x;
	const Int oldY = oldParentY + rel.y - place.inset.y;

	if( place.known == FALSE || oldX != place.placedX || oldY != place.placedY ||
			size.x != place.placedW || size.y != place.placedH )
	{
		// first sight of this window, or somebody else has moved it in the loader's stretched space
		place.designX = oldX / loadScaleX;
		place.designY = oldY / loadScaleY;
		place.designW = size.x / loadScaleX;
		place.designH = size.y / loadScaleY;
	}

	Int newX = REAL_TO_INT_FLOOR( originX + place.designX * s );
	Int newY = REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - place.designY ) * s );
	Int newW = REAL_TO_INT_CEIL( place.designW * s );
	Int newH = REAL_TO_INT_CEIL( place.designH * s );

	const char *shortName = shortWindowName( win );

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

	//
	// The unnamed children are the GameWinBlockInput panes: their only job is to keep clicks on the
	// bar out of the world, so they become exactly their panel.  CenterBackground blocks input as
	// well as holding the command windows, and is authored full width, so it gets the same
	// treatment - left alone it would swallow every click over the gaps we just opened up.
	//
	if( shortName[ 0 ] == 0 || strcmp( shortName, "CenterBackground" ) == 0 )
	{
		newX = m_panelRect[ panel ].lo.x;
		newW = m_panelRect[ panel ].width();
		if( shortName[ 0 ] == 0 )
		{
			newY = m_panelRect[ panel ].lo.y;
			newH = m_panelRect[ panel ].height();
		}
	}

	win->winSetPosition( newX - newParentX, newY - newParentY );
	win->winSetSize( newW, newH );

	place.known = TRUE;
	place.panel = panel;
	place.weHid = FALSE;
	place.slideApplied = 0;
	place.inset.x = place.inset.y = 0;
	place.placedX = newX;
	place.placedY = newY;
	place.placedW = newW;
	place.placedH = newH;

	for( GameWindow *child = win->winGetChild(); child; child = child->winGetNext() )
		placeInPanel( child, panel, oldX, oldY, newX, newY, childShiftX );

}  // end placeInPanel

/// how long a panel takes to leave or return, in milliseconds
static const Real PANEL_SLIDE_MS = 220.0f;

//-------------------------------------------------------------------------------------------------
/** The three marker windows draw nothing of their own - they are five pixels square and exist only
	* to hang a draw callback on, and one of those callbacks paints all three plates.  They are
	* authored over the radar, so taking the left panel away by its windows would take the painting
	* off the panel that is staying too. */
//-------------------------------------------------------------------------------------------------
static Bool isPlateMarker( GameWindow *win )
{
	const char *shortName = shortWindowName( win );
	return strcmp( shortName, "BackgroundMarker" ) == 0 ||
				 strcmp( shortName, "ForegroundMarker" ) == 0 ||
				 strcmp( shortName, "OnTopDraw" ) == 0;
}

//-------------------------------------------------------------------------------------------------
Int ControlBar::getPanelSlideOffset( Int panel ) const
{
	if( panel < 0 || panel >= CB_PANEL_COUNT || TheDisplay == NULL )
		return 0;
	if( m_panelSlide[ panel ] <= 0.0f )
		return 0;

	//
	// Far enough that the top edge of the frame is off the bottom of the screen, so everything is -
	// except a panel with a cap, which travels only as far as the cap says and keeps the strip above
	// it on screen.  The selection panel has one: it carries the button that puts the bar back, and
	// it stops with that button's row standing on the bottom edge.  It takes the same quarter of a
	// second to travel the shorter distance, so all three still move as one.
	//
	const Real drop = ( m_panelDropCap[ panel ] > 0 )
											? (Real)m_panelDropCap[ panel ]
											: (Real)( TheDisplay->getHeight() - m_panelOrigin.y );
	return REAL_TO_INT_FLOOR( m_panelSlide[ panel ] * drop );
}

//-------------------------------------------------------------------------------------------------
void ControlBar::insetPlacedWindow( GameWindow *window, const ICoord2D &inset )
{
	ControlBarPanelPlacement &place = theControlBarPlacement.find( window )->second;
	if( place.inset.x == inset.x && place.inset.y == inset.y )
		return;

	// where placeInPanel put it in its parent, which the parent carries with it wherever it goes
	const ControlBarPanelPlacement &parent = theControlBarPlacement.find( window->winGetParent() )->second;
	window->winSetPosition( place.placedX - parent.placedX + inset.x, place.placedY - parent.placedY + inset.y );
	window->winSetSize( place.placedW - 2 * inset.x, place.placedH - 2 * inset.y );
	place.inset = inset;
}

ICoord2D ControlBar::getPlacedInset( GameWindow *window ) const
{
	return theControlBarPlacement.find( window )->second.inset;
}

//-------------------------------------------------------------------------------------------------
/** A window moved without its place moving was read by the next layoutPanels as moved by somebody
	* else, in the loader's stretched space, and its size taken back through the loader's scale: the
	* command grid, lowered to the bottom edge, came out three quarters as wide after a watcher's
	* selection rebuilt the bar, and the promotion screen measures its cells off it. */
//-------------------------------------------------------------------------------------------------
static void lowerPlaces( GameWindow *window, Int shift )
{
	theControlBarPlacement.find( window )->second.placedY += shift;
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
		lowerPlaces( child, shift );
}

void ControlBar::lowerPlacedWindow( GameWindow *window, Int shift )
{
	Int x = 0, y = 0;
	window->winGetPosition( &x, &y );
	window->winSetPosition( x, y + shift );
	lowerPlaces( window, shift );
}

//-------------------------------------------------------------------------------------------------
/** Put every window where its panel's slide says it should be.
	*
	* layoutPanels has already recorded which panel each direct child of the frame went into and the
	* screen position it was given, so this is that position plus however far the panel has travelled.
	* What is inside those children keeps whatever the context system last decided, and comes back
	* saying the same thing - they move as one piece and are never individually touched. */
//-------------------------------------------------------------------------------------------------
void ControlBar::applyPanelSlide( void )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL )
		return;

	Int offset[ CB_PANEL_COUNT ];
	for( Int p = 0; p < CB_PANEL_COUNT; p++ )
		offset[ p ] = getPanelSlideOffset( p );

	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		ControlBarPanelPlacementMap::iterator it = theControlBarPlacement.find( child );
		if( it == theControlBarPlacement.end() || it->second.known == FALSE )
			continue;
		if( isPlateMarker( child ) )
			continue;

		ControlBarPanelPlacement &place = it->second;
		const Int panel = place.panel;
		if( panel < 0 || panel >= CB_PANEL_COUNT )
			continue;

		//
		// Clear of the bottom edge and staying there: stop drawing it at all.  Only what we hid do
		// we ever un-hide - the context system hides windows of its own all the time (the unused
		// minimise buttons, a beacon pane, an empty portrait) and a blanket winHide(FALSE) over the
		// panel put every one of them back on screen.  Which is why a window that was already hidden
		// when the panel went down is not counted as ours: hiding it changes nothing and remembering
		// it puts it on screen the first time the bar is minimised and brought back.  ControlBar.wnd
		// authors ButtonSmall and ButtonMedium hidden, and that is where the stray "S" and "M" over
		// the battlefield came from.
		//
		if( m_panelHidden[ panel ] )
		{
			if( !place.weHid && !child->winIsHidden() )
			{
				child->winHide( TRUE );
				place.weHid = TRUE;
			}
			continue;
		}

		if( place.weHid )
		{
			child->winHide( FALSE );
			place.weHid = FALSE;
		}

		ICoord2D pos;
		child->winGetPosition( &pos.x, &pos.y );
		const Int wantY = place.placedY - m_panelOrigin.y + offset[ panel ];
		if( pos.y != wantY )
			child->winSetPosition( pos.x, wantY );

		//
		// What the window is actually carrying, which is not the same as what its panel's slide says.
		// The two continues above leave windows behind - a plate marker never moves at all, and a
		// panel minimised with immediate = TRUE is marked hidden before anything is moved - and
		// layoutPanels used to take the panel's offset back off every one of them.  A window that
		// never went down came back up, and placeInPanel then read that position as the rectangle it
		// was authored at: the bar climbed a little further off the bottom of the screen every time
		// the layout was rebuilt, and the tooltip, which is placed relative to BackgroundMarker,
		// climbed with it until it left the screen.
		//
		place.slideApplied = offset[ panel ];
	}
}

//-------------------------------------------------------------------------------------------------
/** Step the slide.  Wall clock rather than logic frames: this is a piece of the interface moving,
	* and it should take the same quarter of a second whatever the game is doing. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updatePanelSlide( void )
{
	const UnsignedInt now = timeGetTime();
	const UnsignedInt sinceMs = ( m_panelSlideMs == 0 || now < m_panelSlideMs ) ? 0 : now - m_panelSlideMs;
	m_panelSlideMs = now;

	Bool moving = FALSE;
	for( Int p = 0; p < CB_PANEL_COUNT; p++ )
		if( m_panelSlide[ p ] != m_panelSlideTo[ p ] )
			moving = TRUE;

	if( !moving )
		return;

	const Real step = ( sinceMs > 0 ) ? (Real)sinceMs / PANEL_SLIDE_MS : 0.0f;

	for( Int p = 0; p < CB_PANEL_COUNT; p++ )
	{
		if( m_panelSlide[ p ] < m_panelSlideTo[ p ] )
		{
			m_panelSlide[ p ] += step;
			if( m_panelSlide[ p ] >= m_panelSlideTo[ p ] )
			{
				m_panelSlide[ p ] = m_panelSlideTo[ p ];
				// arrived - off screen, unless the cap kept a strip of it on
				m_panelHidden[ p ] = ( m_panelDropCap[ p ] <= 0 );
			}
		}
		else if( m_panelSlide[ p ] > m_panelSlideTo[ p ] )
		{
			m_panelSlide[ p ] -= step;
			if( m_panelSlide[ p ] <= m_panelSlideTo[ p ] )
				m_panelSlide[ p ] = m_panelSlideTo[ p ];
		}
	}

	applyPanelSlide();

}  // end updatePanelSlide

//-------------------------------------------------------------------------------------------------
/** Send one whole panel off the bottom of the screen, or bring it back. */
//-------------------------------------------------------------------------------------------------
void ControlBar::showPanel( Int panel, Bool show, Bool immediate )
{
	if( panel < 0 || panel >= CB_PANEL_COUNT )
		return;

	m_panelSlideTo[ panel ] = show ? 0.0f : 1.0f;

	// coming back it is on screen from the first frame of the slide, going it is on screen until the
	// last: only a panel that has arrived at the bottom is hidden
	if( show )
		m_panelHidden[ panel ] = FALSE;

	if( immediate )
	{
		m_panelSlide[ panel ] = m_panelSlideTo[ panel ];
		m_panelHidden[ panel ] = !show && ( m_panelDropCap[ panel ] <= 0 );
	}

	m_panelSlideMs = timeGetTime();
	applyPanelSlide();

}  // end showPanel

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
		 call hovering a build button makes - and write down the rectangle it landed in.  This is what
		 cleared the build popup: it is placed against BackgroundMarker's *live* screen position, and
		 the drift never moves that window, so with the bar's children two hundred pixels high the
		 popup was measured in exactly the same place as with them home.  Kept because it is the only
		 thing that says so. */
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

	DEBUG_LOG(("UIDRILL: frame %d %s top %d money %d origin (%d,%d) marker (%d,%d) live (%d,%d) tip (%d,%d %dx%d) screen %dx%d\n",
		frame, tag, top, money, origin->x, origin->y, markX, markY, liveX, liveY,
		tipX, tipY, tipW, tipH,
		TheDisplay->getWidth(), TheDisplay->getHeight()));
}

void ControlBar::forEachPlacedWindow( Int *topOut, Int *moneyOut )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL )
		return;
	controlBarHighestTop( parent, topOut, moneyOut );
}

//-------------------------------------------------------------------------------------------------
/** Put the slide away: every window back up by however far it actually travelled, and all three
	* panels declared home.
	*
	* Anything that reads a window's screen position has to run after this.  ControlBarScheme::init
	* is the one that does - it asks each parent where it is on screen and writes the money readout,
	* the two tabs and the toolbar column their own positions relative to that answer - and it used
	* to ask while the bar was still on its way down.  The answer was the slid position, so the
	* children came out right for a slid parent; then layoutPanels lifted the parent and every one of
	* those children rode up with it, and placeInPanel read the lifted position as the rectangle they
	* were authored at.  A few pixels a rebuild.  Toggle the bar and change side a dozen times and
	* the money box, the toolbar column and the general's tabs are stranded a third of the way up an
	* empty screen with the plates still painted at the bottom, and the build tooltip - which is
	* placed against BackgroundMarker - has left the top of the picture.
	*
	* Taken off per window rather than per panel, and by what each window is actually carrying:
	* applyPanelSlide leaves windows behind (a plate marker never moves, a panel minimised with
	* immediate = TRUE is marked hidden before anything moves), and lifting one of those lifts a
	* window that never went down.  Not by calling applyPanelSlide with the slide zeroed either -
	* that puts every window back at the y layoutPanels last handed out, which throws away exactly
	* the ControlBarScheme.ini positions the bar is being rebuilt to wear.
	*/
//-------------------------------------------------------------------------------------------------
void ControlBar::clearPanelSlide( void )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];

	for( GameWindow *slid = parent ? parent->winGetChild() : NULL; slid; slid = slid->winGetNext() )
	{
		ControlBarPanelPlacementMap::iterator it = theControlBarPlacement.find( slid );
		if( it == theControlBarPlacement.end() || it->second.known == FALSE )
			continue;
		ControlBarPanelPlacement &place = it->second;
		if( place.panel < 0 || place.panel >= CB_PANEL_COUNT )
			continue;
		if( place.weHid )
		{
			slid->winHide( FALSE );
			place.weHid = FALSE;
		}
		const Int undo = ControlBar_slideToUndo( place.slideApplied, getPanelSlideOffset( place.panel ) );
		if( undo != 0 )
		{
			ICoord2D pos;
			slid->winGetPosition( &pos.x, &pos.y );
			slid->winSetPosition( pos.x, pos.y - undo );
			place.slideApplied = 0;
		}
	}

	for( Int q = 0; q < CB_PANEL_COUNT; q++ )
	{
		m_panelSlide[ q ] = 0.0f;
		m_panelSlideTo[ q ] = 0.0f;
		m_panelHidden[ q ] = FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
/** Re-anchor the control bar as three panels at one uniform scale.
	*
	* A panel that is away, or on its way, is not where this function last put it - and this function
	* reads a window's position to work out what it was authored at.  So the slide comes off first
	* and goes back on at the end, and a bar that was minimised when the side changed is minimised
	* again without the trip down. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutPanels( void )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL || TheDisplay == NULL )
		return;

	Bool wasAway[ CB_PANEL_COUNT ];
	Int q;
	for( q = 0; q < CB_PANEL_COUNT; q++ )
		wasAway[ q ] = ( m_panelSlideTo[ q ] > 0.0f );

	clearPanelSlide();

	const Real dispW = (Real)TheDisplay->getWidth();
	const Real dispH = (Real)TheDisplay->getHeight();

	// what the .wnd loader already multiplied every x coordinate by, so we can divide it back out
	const Real loadScaleX = dispW / CONTROL_BAR_DESIGN_W;

	//
	// One scale for both axes, the smaller of the two so the three panels always fit side by side.
	// At 4:3 that is exactly the old scale and the panels still meet; at anything wider they shrink
	// together and leave the middle of the screen bottom open instead of stretching to fill it.
	//
	const Real s = ControlBarUniformScale();

	Int p;
	for( p = 0; p < CB_PANEL_COUNT; p++ )
		ControlBarPanelDesignToScreen( p, &thePanelDesignRect[ p ],
																	 TheDisplay->getWidth(), TheDisplay->getHeight(),
																	 &m_panelRect[ p ] );

	// the children's positions are relative to the frame, so grab where it is before moving it
	ICoord2D barOrigin;
	parent->winGetScreenPosition( &barOrigin.x, &barOrigin.y );

	// the frame itself stays full width - it draws nothing and passes input through
	const Int parentTop =
		REAL_TO_INT_FLOOR( dispH - ( CONTROL_BAR_DESIGN_H - CONTROL_BAR_DESIGN_TOP ) * s );
	parent->winSetPosition( 0, parentTop );
	parent->winSetSize( REAL_TO_INT_CEIL( dispW ), REAL_TO_INT_CEIL( dispH ) - parentTop );

	// A click that passes a transparent part of a plate lands on the frame, and a unit under it can
	// only be picked if the frame says it can be seen through; see letsClickThrough.
	parent->winSetStatus( WIN_STATUS_SEE_THRU );

	// the plates are drawn from design rectangles rather than from a window, so they need to be told
	// where the frame they belong to started out - see getPanelOrigin
	m_panelOrigin.x = 0;
	m_panelOrigin.y = parentTop;

	//
	// Two of the three panels go all the way off the bottom.  The selection panel stops with the row
	// its minimise tab is in standing on the bottom edge: the button that puts the bar back is in
	// that row, and so is the plate that paints it, so the tab a minimised player clicks is real
	// artwork and not a button floating over the battlefield.
	//
	for( p = 0; p < CB_PANEL_COUNT; p++ )
		m_panelDropCap[ p ] = 0;
	if( m_controlBarSchemeManager )
	{
		const ControlBarPlate *rightPlate =
			ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentSide(), CB_PANEL_RIGHT );
		if( rightPlate == NULL )
			rightPlate = ControlBarPlateForSide( m_controlBarSchemeManager->getCurrentArtTwinSide(),
																					 CB_PANEL_RIGHT );
		if( rightPlate && rightPlate->minTab.height() > 0 )
			m_panelDropCap[ CB_PANEL_RIGHT ] =
				REAL_TO_INT_FLOOR( ( CONTROL_BAR_DESIGN_H - rightPlate->minTab.hi.y ) * s );
	}

	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		// the nameless panes go by where they were authored, so ask the cache before it is rewritten
		ControlBarPanelPlacementMap::const_iterator it = theControlBarPlacement.find( child );
		Real designCenterX;
		if( it != theControlBarPlacement.end() && it->second.known )
			designCenterX = it->second.designX + it->second.designW * 0.5f;
		else
		{
			ICoord2D rel, size;
			child->winGetPosition( &rel.x, &rel.y );
			child->winGetSize( &size.x, &size.y );
			designCenterX = ( barOrigin.x + rel.x + size.x * 0.5f ) / loadScaleX;
		}

		placeInPanel( child, panelForWindow( shortWindowName( child ), designCenterX ),
									barOrigin.x, barOrigin.y, 0, parentTop );
	}

	// whatever was away before is away again, without being watched leaving a second time
	for( q = 0; q < CB_PANEL_COUNT; q++ )
		if( wasAway[ q ] )
			showPanel( q, FALSE, TRUE );

}  // end layoutPanels

//-------------------------------------------------------------------------------------------------
/** Initialzie the control bar, this is our interface to the context sinsitive GUI */
//-------------------------------------------------------------------------------------------------
void ControlBar::init( void )
{
	INI ini;
	m_sideSelectAnimateDown = FALSE;
	// load the command buttons
	ini.load( AsciiString( "Data\\INI\\Default\\CommandButton.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\CommandButton.ini" ), INI_LOAD_OVERWRITE, NULL );

	// load the command sets
	ini.load( AsciiString( "Data\\INI\\CommandSet.ini" ), INI_LOAD_OVERWRITE, NULL );

	// post process step after loading the command buttons and command sets
	postProcessCommands();

	// the builder's page menu buttons are ours, not the data's
	makeBuildPageButtons();

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
		m_rightHUDUpgradeCameos[ i ] = NULL;
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
		// the bar carries no beacon button; nothing below shows it again
		win = TheWindowManager->winGetWindowFromId(NULL,TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonPlaceBeacon"));
		win->winHide(TRUE);
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
	if(m_radarAttackGlowWindow)
		m_radarAttackGlowWindow->winEnable(TRUE);
	m_radarAttackGlowOn = FALSE;
	m_remainingRadarAttackGlowFrames = 0;

	m_displayedConstructPercent = -1.0f;
	m_displayedOCLTimerSeconds = 0;

	m_buildPage = BUILD_PAGE_ROOT;
	m_buildPageObjectID = INVALID_ID;

	m_isObserverCommandBar = FALSE; // reset us to use a normal command bar
	m_observerLookAtPlayer = NULL;
	m_watchedSide.clear();
	m_watchedSelection = NULL;
	m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;	// a minimised bar is this match's, not the next one's

	// the next match has its own sides, and a mod switch reloads the artwork these point at
	m_borrowedTrayCount = 0;

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

	// Remove any overridden sets.
	CommandSet *set, *nextSet;
	set = m_commandSets;
	while (set) {
		Bool possibleAdjustment = FALSE;
		nextSet = set->friend_getNext();
		if (set == m_commandSets) {
			possibleAdjustment = TRUE;
		}

		Overridable *stillValid = set->deleteOverrides();
		if (stillValid == NULL && possibleAdjustment) {
			m_commandSets = nextSet;
		}

		set = nextSet;
	}

	// Remove any overridden command buttons.
	CommandButton *button, *nextButton;
	button = m_commandButtons;
	while (button) {
		Bool possibleAdjustment = FALSE;
		nextButton = button->friend_getNext();
		if (button == m_commandButtons) {
			possibleAdjustment = TRUE;
		}

		Overridable *stillValid = button->deleteOverrides();
		if (stillValid == NULL && possibleAdjustment) {
			m_commandButtons = nextButton;
		}

		button = nextButton;
	}
	if(TheTransitionHandler)
		TheTransitionHandler->remove("ControlBarArrow");
	m_genArrow = NULL;

	m_lastFlashedAtPointValue = -1;
	m_genStarFlash = TRUE;
}  // end reset

//-------------------------------------------------------------------------------------------------
/** How far a unit is into its current veterancy rank, 0..100, or -1 when there is no next rank to
	* fill towards (top rank, or a template with no thresholds - both mean "draw no bar"). */
//-------------------------------------------------------------------------------------------------
Int ControlBar_experiencePercent( Int currentExp, Int levelExp, Int nextLevelExp )
{
	if( nextLevelExp <= levelExp )
		return -1;

	Int percent = ( currentExp - levelExp ) * 100 / ( nextLevelExp - levelExp );
	return min( 100, max( 0, percent ) );
}

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
	// uncapped that was several whole passes per tick for nothing, and updateRadarAttackGlow()
	// was actively wrong: it burns one frame off its own countdown per call, so the "under
	// attack" radar glow blinked (fps/30)x too fast and went dark early.  Latch that work to the
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
		updateRadarAttackGlow();
	}

	//
	// a chord the player armed and then walked away from expires on its own, so it cannot be
	// waiting to eat a keystroke a minute later
	//
	if( m_chordGroup >= 0 && timeGetTime() - m_chordStartMs > CHORD_TIMEOUT_MS )
	{
		dropChord();
	}

	//
	// ... and neither does a chord whose builder is gone: the command bar is now showing
	// something else, so the cell the second key would pick is not the one that was armed
	//
	if( m_chordGroup >= 0 )
	{
		const DrawableID nowShowing = m_currentSelectedDrawable ? m_currentSelectedDrawable->getID()
																													 : INVALID_DRAWABLE_ID;
		if( nowShowing != m_chordDrawableID )
			dropChord();
	}

	//
	// a general's power row goes the same way: it expires on its own, and it drops the moment the
	// bar it was picked against is off screen - hidden with the command bar, animated out, or
	// repopulated with fewer powers than the row names.  Otherwise a key pressed in a hurry and
	// then thought better of sits armed, and the next one fires a power instead of picking a row
	//
	if( m_specialPowerShortcutRow >= 0 )
	{
		const Int visibleRows = ( countVisibleSpecialPowerShortcuts() + SPECIAL_POWER_SHORTCUT_COLS - 1 )
														/ SPECIAL_POWER_SHORTCUT_COLS;
		if( timeGetTime() - m_specialPowerShortcutRowMs > CHORD_TIMEOUT_MS
				|| m_specialPowerShortcutParent == NULL
				|| m_specialPowerShortcutParent->winIsHidden()
				|| m_specialPowerShortcutRow >= visibleRows )
			clearSpecialPowerShortcutRow();
	}

	// the minimised panels going or coming back
	updatePanelSlide();

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
			drawToEvaluateFor = TheInGameUI->getAllSelectedDrawables()->front();
		Object *obj = drawToEvaluateFor ? drawToEvaluateFor->getObject() : NULL;
		setPortraitByObject( obj );
		
		return;
	}
		

	const Bool flashTick = logicTick && (logicNow % 10 == 0);

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
	// a stand-in builder is not selected, so no deselect event tells us when it dies or when a
	// real selection arrives; re-evaluate ourselves before anything touches its drawable
	//
	if( m_standInBuilderID != INVALID_DRAWABLE_ID )
	{
		Drawable *standIn = TheGameClient->findDrawableByID( m_standInBuilderID );
		Object *standInObj = standIn ? standIn->getObject() : NULL;
		if( TheInGameUI->getSelectCount() > 0 || standInObj == NULL || standInObj->isEffectivelyDead() )
		{
			m_standInBuilderID = INVALID_DRAWABLE_ID;
			m_currentSelectedDrawable = NULL;
			markUIDirty();
		}
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
	// the experience bar under the portrait. The rank chevron on the cameo says which rank a
	// unit holds and nothing about how close the next one is, and that is the half of it you
	// decide with - whether this tank is one kill off veteran or has just got there. Only for a
	// single selection: with a group selected the portrait is one member standing in for all of
	// them, and its experience would be read as the group's.
	//
	if( m_rightHUDCameoWindow )
	{
		Int xpPercent = -1;
		Object *portraitObj = ( TheInGameUI && TheInGameUI->getSelectCount() == 1 &&
														m_currentSelectedDrawable ) ? m_currentSelectedDrawable->getObject()
																												: NULL;
		const ExperienceTracker *xp = portraitObj ? portraitObj->getExperienceTracker() : NULL;
		if( xp && portraitObj->isLocallyControlled() && xp->isTrainable() &&
				xp->getVeterancyLevel() < LEVEL_LAST )
		{
			const ThingTemplate *tmpl = portraitObj->getTemplate();
			xpPercent = ControlBar_experiencePercent( xp->getCurrentExperience(),
																							 tmpl->getExperienceRequired( xp->getVeterancyLevel() ),
																							 tmpl->getExperienceRequired( xp->getVeterancyLevel() + 1 ) );
		}

		GadgetButtonSetBar( m_rightHUDCameoWindow, xpPercent, GameMakeColor( 255, 200, 40, 255 ) );
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
	// nothing selected: one of the player's builders stands in and its command bar shows, so
	// a structure can be placed without selecting a dozer first - the logic then sends the
	// idle builder nearest the site (MSG_DOZER_CONSTRUCT).  m_standInBuilderID is not cleared
	// first: findStandInBuilder reads it to keep the builder it is already showing.
	//
	if( TheInGameUI->getSelectCount() == 0 )
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
/** Find a command button of the given name if present */
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::findNonConstCommandButton( const AsciiString& name )
{
	
	for( const CommandButton *command = m_commandButtons; command; command = command->getNext() )
		if( command->getName() == name )
			return const_cast<CommandButton*>((const CommandButton*)command->getFinalOverride());

	return NULL;  // not found

}  // end findCommandButton

//-------------------------------------------------------------------------------------------------
/** Allocate a new command button, assign name, and tie to list */
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::newCommandButton( const AsciiString& name )
{
	CommandButton *newButton;

	// allocate new button
	newButton = newInstance(CommandButton);

	// assign name
	newButton->setName(name);

	// link to list
	newButton->friend_addToList(&m_commandButtons);

	// return the new button
	return newButton;

}  // end newCommandButton

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandButton *ControlBar::newCommandButtonOverride( CommandButton *buttonToOverride )
{
	if (!buttonToOverride) {
		return NULL;
	}

	CommandButton *newOverride;

	// allocate new button
	newOverride = newInstance(CommandButton);

	*newOverride = *buttonToOverride;

	newOverride->markAsOverride();
	buttonToOverride->setNextOverride(newOverride);

	return newOverride;
}

//-------------------------------------------------------------------------------------------------
/** Parse a command set */
//-------------------------------------------------------------------------------------------------
/*static*/ void ControlBar::parseCommandSetDefinition( INI *ini )
{
	AsciiString name;
	CommandSet *commandSet;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	commandSet = TheControlBar->findNonConstCommandSet( name );
	if( commandSet == NULL )
	{

		// allocate a new item
		commandSet = TheControlBar->newCommandSet( name );
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
			commandSet->markAsOverride();
		}
	}  // end if
	else if( ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES )
	{
		//Holy crap, this sucks to debug!!!
		//If you have two different command sets, the previous
		//code would simply allow you to define multiple command set
		//with the same name, and just nuke the old button with the new one.
		//So, I (KM) have added this assert to notify in case of two same-name
		//command set.
		DEBUG_CRASH(( "[LINE: %d in '%s'] Duplicate commandset %s found!", ini->getLineNum(), ini->getFilename().str(), name.str() ));
		throw INI_INVALID_DATA;

		//@todo SUPPORT OVERRIDES -- JM
	} else {
		commandSet = TheControlBar->newCommandSetOverride(commandSet);
	}

	// sanity
	DEBUG_ASSERTCRASH( commandSet, ("parseCommandSetDefinition: Unable to allocate set '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( commandSet, commandSet->friend_getFieldParse() );

}  // end parseCommandSetDefinition

//-------------------------------------------------------------------------------------------------
/** Find existing command set by name */
//-------------------------------------------------------------------------------------------------
CommandSet* ControlBar::findNonConstCommandSet( const AsciiString& name )
{
	CommandSet* set;

	for( set = m_commandSets; set != NULL; set = set->friend_getNext() )
		if( set->getName() == name )
			return const_cast<CommandSet*>((const CommandSet *) set);

	return NULL;  // set not found

}
//-------------------------------------------------------------------------------------------------
/** find existing command button if present	*/
//-------------------------------------------------------------------------------------------------
const CommandButton *ControlBar::findCommandButton( const AsciiString& name ) 
{ 
	CommandButton *btn =  findNonConstCommandButton(name); 
	if( btn )
	{
		btn = (CommandButton *)btn->friend_getFinalOverride();
	}
	return btn; 
}

//-------------------------------------------------------------------------------------------------
/** Find existing command set by name */
//-------------------------------------------------------------------------------------------------
const CommandSet *ControlBar::findCommandSet( const AsciiString& name ) 
{ 
	CommandSet *set = findNonConstCommandSet(name); 
	if (set)
		set = (CommandSet*)set->friend_getFinalOverride();
	return set; 
}

//-------------------------------------------------------------------------------------------------
/** Allocate a new command set, link to list, initialize to default, and return it */
//-------------------------------------------------------------------------------------------------
CommandSet *ControlBar::newCommandSet( const AsciiString& name )
{
	// allocate a new set
	CommandSet* set = newInstance(CommandSet)(name);
	// add it to the list.
	set->friend_addToList(&m_commandSets);
	// return the newly created set
	return set;

}  // end newCommandSet

//-------------------------------------------------------------------------------------------------
/** Create an overridden command set. */
//-------------------------------------------------------------------------------------------------
CommandSet *ControlBar::newCommandSetOverride( CommandSet *setToOverride )
{
	if (!setToOverride) {
		return NULL;
	}

	// allocate a new set
	CommandSet* set = newInstance(CommandSet)(setToOverride->getName());

	// it's an override; DON'T add it to the main list.
	// !!! DO NOT DO THIS !!! -- > set->friend_addToList(&m_commandSets); <-- !!! DO NOT DO THIS !!!

	*set = *setToOverride;
	set->markAsOverride();

	setToOverride->setNextOverride(set);

	return set;
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

/** a builder is free for a job when it has no build/repair task and is not hauling supplies -
	walking somewhere on a plain move order does not make it busy */
static Bool builderIsFree( AIUpdateInterface *ai, DozerAIInterface *dozer )
{
	if( dozer->isAnyTaskPending() )
		return FALSE;
	const SupplyTruckAIInterface *supply = ai->getSupplyTruckAIInterface();
	if( supply && supply->isCurrentlyFerryingSupplies() )
		return FALSE;
	return TRUE;
}

static void findStandInBuilderProc( Object *obj, void *userData )
{
	StandInBuilderSearch *s = (StandInBuilderSearch *)userData;
	if( obj == NULL || obj->isEffectivelyDead() || obj->getDrawable() == NULL )
		return;
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	AIUpdateInterface *ai = obj->getAI();
	DozerAIInterface *dozer = ai ? ai->getDozerAIInterface() : NULL;
	if( dozer == NULL )
		return;
	if( s->any == NULL )
		s->any = obj;
	if( s->idle == NULL && builderIsFree( ai, dozer ) )
		s->idle = obj;
}

Drawable *ControlBar::findStandInBuilder( Bool freeOnly )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( player == NULL )
		return NULL;

	//
	// The builder already standing in keeps the bar for as long as it is still a builder.  The
	// sweep below answers "the first idle one", and idleness changes on its own: a dozer somewhere
	// across the base finishing a building goes idle and used to take the bar off the one you were
	// working with.  That drops a half-typed chord and takes the structure off your cursor, in the
	// middle of placing it, because something happened somewhere else.
	//
	if( m_standInBuilderID != INVALID_DRAWABLE_ID && TheGameClient )
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
/** The tray a general's power shortcut stands in: the background of one of that bar's slots, taken
	* off the slot itself.  Each side ships its own bar and its own tray art, so reading it here is
	* what keeps anything that borrows the look - both HUD strips do - on the same side's metal as
	* the bar underneath them, without naming a single image.
	*
	* Watching a match there is no such bar: FactionObserver ships neither a shortcut layout nor a
	* button count, so both strips used to fall back to a flat black plate for the whole match.  The
	* side the observer bar is looking at answers for it instead - one side for the whole HUD, the
	* one the bar itself is showing. */
//-------------------------------------------------------------------------------------------------
const Image *ControlBar::getSpecialPowerTrayImage( void )
{
	if( m_specialPowerShortcutParent && m_specialPowerShortcutButtonParents[ 0 ] )
		return m_specialPowerShortcutButtonParents[ 0 ]->winGetEnabledImage( 0 );

	const BorrowedTray *borrowed = borrowTray( specialPowerTraySide() );
	return borrowed ? borrowed->image : NULL;

}  // end getSpecialPowerTrayImage

//-------------------------------------------------------------------------------------------------
/** Any template of this side that ships a general's power bar - the generals of one side share that
	* layout, so which one comes back does not matter, only that it carries the side's tray.  The
	* shell's bar names its side with "Small" on the end; that is the same art at another size. */
//-------------------------------------------------------------------------------------------------
const PlayerTemplate *ControlBar::templateForSide( AsciiString side )
{
	if( side.isEmpty() || ThePlayerTemplateStore == NULL )
		return NULL;

	if( side.endsWithNoCase( "Small" ) )
		for( Int c = 0; c < 5; c++ )
			side.removeLastChar();

	for( Int t = 0; t < ThePlayerTemplateStore->getPlayerTemplateCount(); t++ )
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate( t );
		if( pt && pt->getSide().compareNoCase( side ) == 0
				&& !pt->getSpecialPowerShortcutWinName().isEmpty() )
			return pt;
	}

	return NULL;

}  // end templateForSide

//-------------------------------------------------------------------------------------------------
/** Whose metal the HUD wears when this bar has none of its own.  The answer is the bar on screen and
	* nothing else: the scheme manager knows which side's artwork it is drawing, so the strips wear
	* that side even while watching, where the seat at the keyboard has no side at all.
	*
	* The scheme names a side, not a general, and the generals of one side share a shortcut layout, so
	* any template of that side hands over the same tray.  The observer bar's own side is "Observer",
	* which no template carries, but every plate on that bar is another side's - so the scheme sharing
	* its right hand plate names the side it is really wearing, and the strips follow the bar rather
	* than the seat.  Only if even that finds nothing does whoever is being watched, and then the first
	* side still in the match, answer: a plate of plain black is worse than somebody else's metal, and
	* it was what the strips wore for whole matches. */
//-------------------------------------------------------------------------------------------------
const PlayerTemplate *ControlBar::specialPowerTraySide( void )
{
	if( m_controlBarSchemeManager )
	{
		const PlayerTemplate *own = templateForSide( m_controlBarSchemeManager->getCurrentSide() );
		if( own )
			return own;

		const PlayerTemplate *worn = templateForSide( m_controlBarSchemeManager->getCurrentArtTwinSide() );
		if( worn )
			return worn;
	}

	// the bar wears artwork no side owns; the seat it is showing is the next best answer
	if( m_observerLookAtPlayer && m_observerLookAtPlayer->getPlayerTemplate()
			&& !m_observerLookAtPlayer->getPlayerTemplate()->getSpecialPowerShortcutWinName().isEmpty() )
		return m_observerLookAtPlayer->getPlayerTemplate();

	if( ThePlayerList == NULL )
		return NULL;

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); i++ )
	{
		Player *p = ThePlayerList->getNthPlayer( i );
		if( p == NULL || !p->isPlayerActive() || !p->isPlayableSide() )
			continue;

		const PlayerTemplate *pt = p->getPlayerTemplate();
		if( pt && !pt->getSpecialPowerShortcutWinName().isEmpty() )
			return pt;
	}

	return NULL;

}  // end specialPowerTraySide

//-------------------------------------------------------------------------------------------------
/** Read a side's tray out of its own shortcut layout, once.  Nothing of the layout survives the
	* call: the Image belongs to the mapped image collection and the size is two integers, so the
	* windows go straight back.  The lookup is by layout name rather than by template, since the
	* generals of one side share a bar. */
//-------------------------------------------------------------------------------------------------
const ControlBar::BorrowedTray *ControlBar::borrowTray( const PlayerTemplate *pt )
{
	if( pt == NULL || pt->getSpecialPowerShortcutWinName().isEmpty() )
		return NULL;

	const AsciiString layoutName = pt->getSpecialPowerShortcutWinName();

	//
	// the side at the keyboard is read off the bar it is already showing.  Not an optimization: the
	// generals of one side share a layout, and a second copy of it would put a second window under
	// every one of its names for as long as this call runs
	//
	if( m_specialPowerLayout && m_specialPowerShortcutButtonParents[ 0 ]
			&& m_specialPowerLayout->getFilename().compareNoCase( layoutName ) == 0 )
		return NULL;

	for( Int i = 0; i < m_borrowedTrayCount; i++ )
		if( m_borrowedTrays[ i ].layout.compareNoCase( layoutName ) == 0 )
			return m_borrowedTrays[ i ].image ? &m_borrowedTrays[ i ] : NULL;

	if( m_borrowedTrayCount >= MAX_BORROWED_TRAYS )
		return NULL;			// more sides on one screen than any game ships: they keep the plain plate

	BorrowedTray *slot = &m_borrowedTrays[ m_borrowedTrayCount++ ];
	slot->layout = layoutName;
	slot->image = NULL;
	slot->size.x = 0;
	slot->size.y = 0;

	WindowLayout *layout = TheWindowManager->winCreateLayout( layoutName );
	if( layout == NULL )
		return NULL;					// a mod naming a layout it does not ship: remembered as having no tray
	layout->hide( TRUE );

	// the size read off it below is the size the strips draw a tray at, so it has to come out of the
	// same un-stretched space the bar itself is put in - see initSpecialPowershortcutBar
	ControlBarLayoutUniform( layout->getFirstWindow(), 1.0f, 1.0f );

	AsciiString parentName = layoutName;
	parentName.concat( ":ButtonParent1" );

	//
	// looked up inside our own copy of the bar rather than across every window on screen: a player
	// who was knocked out still has his own bar loaded, and it carries these very names
	//
	GameWindow *slotWindow = TheWindowManager->winGetWindowFromId( layout->getFirstWindow(),
																			TheNameKeyGenerator->nameToKey( parentName ) );
	if( slotWindow )
	{
		slot->image = slotWindow->winGetEnabledImage( 0 );
		slotWindow->winGetSize( &slot->size.x, &slot->size.y );
	}

	layout->destroyWindows();
	layout->deleteInstance();

	return slot->image ? slot : NULL;

}  // end borrowTray

//-------------------------------------------------------------------------------------------------
/** That tray at the size the bar itself draws it, the hole in its artwork the cameo fills, and the
	* step a row of them runs at.  All of it read back off the loaded bar, which the loader has
	* already scaled to the running resolution - so nothing here is in 800x600 pixels, and anything
	* borrowing the look comes out the size of the powers themselves at every resolution.
	*
	* Any of the four out-parameters may be NULL.  FALSE when there is no bar and no layout to
	* borrow one from - the strips then keep the plain plate they had before any of this. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::getSpecialPowerTrayLayout( ICoord2D *traySize, ICoord2D *cameoSize,
																						ICoord2D *cameoOffset, Int *columnStep )
{
	if( m_specialPowerShortcutParent && m_specialPowerShortcutButtonParents[ 0 ] )
	{
		Int slotWidth, slotHeight;
		m_specialPowerShortcutButtonParents[ 0 ]->winGetSize( &slotWidth, &slotHeight );
		return trayLayoutFromSlot( slotWidth, slotHeight, traySize, cameoSize, cameoOffset, columnStep );
	}

	// measured off the very layout the tray came out of, so the two always agree
	const BorrowedTray *borrowed = borrowTray( specialPowerTraySide() );
	if( borrowed == NULL )
		return FALSE;

	return trayLayoutFromSlot( borrowed->size.x, borrowed->size.y,
														 traySize, cameoSize, cameoOffset, columnStep );

}  // end getSpecialPowerTrayLayout

//-------------------------------------------------------------------------------------------------
/** The artwork's proportions, applied to whatever size a slot came out at. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::trayLayoutFromSlot( Int slotWidth, Int slotHeight, ICoord2D *traySize,
																		 ICoord2D *cameoSize, ICoord2D *cameoOffset, Int *columnStep )
{
	if( slotWidth <= 0 || slotHeight <= 0 )
		return FALSE;

	//
	// where the cameo goes is the hole in the tray artwork, not a guess: SATraySmall is the 60x56
	// patch at 413,1 of SAControlBar512_001.tga and its transparent middle runs x 4..48, y 9..46.
	// Those are fractions of the art, so they hold at whatever size the loader gave the tray
	//
	const Int ART_WIDTH = 60;
	const Int ART_HEIGHT = 56;
	const Int HOLE_X = 4;
	const Int HOLE_Y = 9;
	const Int HOLE_WIDTH = 45;
	const Int HOLE_HEIGHT = 38;

	if( traySize )
	{
		traySize->x = slotWidth;
		traySize->y = slotHeight;
	}

	if( cameoSize )
	{
		cameoSize->x = ( slotWidth * HOLE_WIDTH + ART_WIDTH / 2 ) / ART_WIDTH;
		cameoSize->y = ( slotHeight * HOLE_HEIGHT + ART_HEIGHT / 2 ) / ART_HEIGHT;
	}

	if( cameoOffset )
	{
		cameoOffset->x = ( slotWidth * HOLE_X + ART_WIDTH / 2 ) / ART_WIDTH;
		cameoOffset->y = ( slotHeight * HOLE_Y + ART_HEIGHT / 2 ) / ART_HEIGHT;
	}

	//
	// A row steps a whole tray, the way a column already does.  The slots used to slide an eighth of
	// a tray back under each other so a row read as one run of metal, and what that actually did was
	// lay the neighbour's rail over the left edge of every cameo but the last: the general's powers
	// and the superweapon countdowns both came out overlapping.  Side by side they read as the boxes
	// they are, and the queue column down the left has looked like that all along.
	//
	if( columnStep )
		*columnStep = slotWidth;

	return TRUE;

}  // end getSpecialPowerTrayLayout

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
	* picks the row - F1 the row in the corner, F2 the one above it - and the second picks the power
	* in it, F1 being the rightmost.  Until a row is picked only the head of each row is labelled,
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
		m_specialPowerShortcutRowMs = timeGetTime();
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

	//
	// a half-typed structure chord used to be dropped here.  It cannot be: evaluateContextUI
	// erases the bar by switching to CB_CONTEXT_NONE before it rebuilds it, and arming the
	// chord calls markUIDirty() itself to grey the other group out - so the chord killed
	// itself on the very next frame and the second key never had a chord to resolve.
	// ControlBar::update drops it instead, when the drawable driving the bar is no longer the
	// one the chord was armed on.
	//

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
/** The letter currently bound to command bar slot 'slot' (0 based), or an empty string when the
	slot has no unmodified binding.  Read live out of the meta map so rebinding the key in
	Options > Keyboard re-labels the button. */
//-------------------------------------------------------------------------------------------------
static UnicodeString getMetaKeyLabel( GameMessage::Type wanted )
{
	UnicodeString label;

	if( TheMetaMap == NULL || TheKeyboard == NULL )
		return label;

	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
	{
		// only a plain, unmodified key makes a readable short label
		if( rec->m_meta != wanted || rec->m_modState != 0 )
			continue;

		// function keys have no printable character; name them
		if( rec->m_key >= MK_F1 && rec->m_key <= MK_F10 )
			label.format( L"F%d", rec->m_key - MK_F1 + 1 );
		else if( rec->m_key == MK_F11 )
			label.set( L"F11" );
		else if( rec->m_key == MK_F12 )
			label.set( L"F12" );
		else
		{
			WideChar c = TheKeyboard->getPrintableKey( (UnsignedByte)rec->m_key, 0 );
			if( c )
			{
				if( c >= L'a' && c <= L'z' )
					c -= (L'a' - L'A');

				WideChar text[ 2 ] = { c, 0 };
				label.set( text );
			}
		}
		break;
	}

	return label;

}  // end getMetaKeyLabel

static UnicodeString getGridHotKeyLabel( Int slot )
{
	return getMetaKeyLabel( (GameMessage::Type)(GameMessage::MSG_META_COMMAND_SLOT01 + slot) );
}

//-------------------------------------------------------------------------------------------------
/** The letter a button's label marks with '&', in capitals: the key HotKeyManager presses that
	* button with under Legacy input.  Read from the untranslated label, because a translation marks
	* letters of its own - the Turkish one marks every label's first letter, which put K on three
	* GLA structures and S on five. */
//-------------------------------------------------------------------------------------------------
static UnicodeString getLabelHotKeyLabel( const AsciiString& textLabel )
{
	UnicodeString label;
	const UnicodeString text = TheGameText->fetchUntranslated( textLabel.str() );
	const WideChar *marker = wcschr( text.str(), L'&' );
	if( marker && marker[ 1 ] )
	{
		const WideChar letter[ 2 ] = { (WideChar)towupper( marker[ 1 ] ), 0 };
		label.set( letter );
	}
	return label;
}

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
		GadgetButtonSetText( button, UnicodeString( L"" ) );
	}

	// save the command in the user data of the window
	GadgetButtonSetData(button, (void*)commandButton);
	//button->winSetUserData( commandButton );

	// a recycled button must not keep the previous occupant's count badge, nor its seconds label:
	// only the command context re-stamps those every frame, so a button that ends up in any other
	// context (a garrisoned building's exit cameos, a beacon, an OCL timer) would wear the build
	// time of whatever used to live in that window.
	GadgetButtonSetCount( button, 0 );
	GadgetButtonSetSeconds( button, 0 );
	GadgetButtonSetCost( button, 0 );
	GadgetButtonSetPower( button, 0 );

	setCommandBarBorder(button, commandButton->getCommandButtonMappedBorderType());
	
	//
	// The '&' letter buried in each localized button label and the grid keys are two rival input
	// schemes for the same buttons: the letter fires on KEY_UP out of HotKeyTranslator, the grid
	// keys on KEY_DOWN out of MetaEventTranslator, so leaving both live makes one keystroke do two
	// things.  Modern is the grid and registers no letters.  Legacy is the letters, which is what the
	// game shipped with, and each button wears its own letter the way a Modern one wears its grid key.
	//
	const Bool legacyInput = TheGlobalData->isLegacyInput();
	if( legacyInput && TheHotKeyManager )
	{
		AsciiString hotKey = TheHotKeyManager->searchHotKey(
			TheGameText->fetchUntranslated( commandButton->getTextLabel().str() ) );
		if( hotKey.isNotEmpty() )
			TheHotKeyManager->addHotKey( button, hotKey );
	}

	// paint the key in the button's top left corner.  Only the real command bar slots get one -
	// the communicator, options and science buttons are not on the grid.
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		if( m_commandWindows[ slot ] != button )
			continue;

		UnicodeString label = legacyInput ? getLabelHotKeyLabel( commandButton->getTextLabel() )
																			: getGridHotKeyLabel( slot );

		// structures are reached by a chord: Q or W picks the group (columns 1-4 or 5-7), then
		// the key of the cell's own position inside that group - paint both, "QQ", "QZ", "WQ" ...
		// Legacy has no chords, and a structure is its one letter.
		if( commandButton->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && !label.isEmpty() && !legacyInput )
		{
			Int base = ( slot < CHORD_GROUP_SIZE ) ? 0 : CHORD_GROUP_SIZE;
			UnicodeString chord = getGridHotKeyLabel( base == 0 ? CHORD_SLOT_Q : CHORD_SLOT_W );
			chord.concat( getGridHotKeyLabel( slot - base ) );
			label = chord;
		}

		if( label.isEmpty() )
			button->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
		else
			button->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );

		GadgetButtonSetText( button, label );
		break;
	}
	//
	// the stop button does not live on the grid: MSG_META_STOP presses it whether or not grid hot
	// keys are switched on, and on a building of yours that is still going up it is the cancel key,
	// so the button wears that letter in both modes.
	//
	if( commandButton->getCommandType() == GUI_COMMAND_STOP )
	{
		UnicodeString stopKey = legacyInput ? getLabelHotKeyLabel( commandButton->getTextLabel() )
																				: getMetaKeyLabel( GameMessage::MSG_META_STOP );

		if( stopKey.isEmpty() )
			button->winClearStatus( WIN_STATUS_SHORTCUT_BUTTON );
		else
			button->winSetStatus( WIN_STATUS_SHORTCUT_BUTTON );

		GadgetButtonSetText( button, stopKey );
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
	for ( CommandButton *button = m_commandButtons; button; button = button->friend_getNext() ) 
	{
		button->cacheButtonImage();
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
			if( obj->hasUpgrade(ut) )
			{
				//Object level upgrades
				m_rightHUDUpgradeCameos[i]->winEnable( TRUE );
			}
			else if( player && player->hasUpgradeComplete( ut ) )
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
void ControlBar::setControlBarSchemeByPlayer(Player *p)
{
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayer(p);

	static NameKeyType buttonIdleWorkerID = NAMEKEY("ControlBar.wnd:ButtonIdleWorker");
	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonIdleWorker = TheWindowManager->winGetWindowFromId( NULL, buttonIdleWorkerID );
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralID );

	if( !p->isPlayerActive() )
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( CB_CONTEXT_OBSERVER_LIST, NULL );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		if (buttonIdleWorker)
			buttonIdleWorker->winHide(TRUE);
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( CB_CONTEXT_NONE, NULL );
		m_isObserverCommandBar = FALSE;

		if (buttonIdleWorker)
			buttonIdleWorker->winHide(FALSE);
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
	if(m_controlBarSchemeManager)
		m_controlBarSchemeManager->setControlBarSchemeByPlayerTemplate(pt);

	static NameKeyType buttonIdleWorkerID = NAMEKEY("ControlBar.wnd:ButtonIdleWorker");
	static NameKeyType buttonGeneralID = NAMEKEY("ControlBar.wnd:ButtonGeneral");
	GameWindow *buttonIdleWorker = TheWindowManager->winGetWindowFromId( NULL, buttonIdleWorkerID );
	GameWindow *buttonGeneral = TheWindowManager->winGetWindowFromId( NULL, buttonGeneralID );

	if(pt == ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionObserver")))
	{
		m_isObserverCommandBar = TRUE;
		switchToContext( CB_CONTEXT_OBSERVER_LIST, NULL );
		DEBUG_LOG(("We're loading the Observer Command Bar\n"));

		if (buttonIdleWorker)
			buttonIdleWorker->winHide(TRUE);
		if (buttonGeneral)
			buttonGeneral->winEnable(FALSE);
	}
	else
	{
		switchToContext( CB_CONTEXT_NONE, NULL );
		m_isObserverCommandBar = FALSE;

		if (buttonIdleWorker)
			buttonIdleWorker->winHide(FALSE);
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
	TheInGameUI->openPromotionPage();
	// the fade holds the screen hidden for nine frames and draws the side's old painting of it fading
	// in, which the page has replaced
	if (TheGlobalData->m_animateWindows && !TheInGameUI->isPromotionPageShown())
		TheTransitionHandler->setGroup("GenExpFade");
		//m_generalsScreenAnimate->registerGameWindow( m_contextParent[ CP_PURCHASE_SCIENCE ], WIN_ANIMATION_SLIDE_TOP, TRUE, 200 );

}

void ControlBar::hidePurchaseScience( void )
{
	clearPurchaseScienceColumn();

	m_purchaseScienceOpen = FALSE;

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
//	if (!TheGlobalData->m_animateWindows)
//		{
//			if( m_contextParent[ CP_PURCHASE_SCIENCE ] )
//			{
//				m_contextParent[ CP_PURCHASE_SCIENCE ]->winHide( TRUE );
//			}
//		}
//		else
//		{
//			//if (m_generalsScreenAnimate->isFinished())
//			if(TheTransitionHandler->isFinished())
//				TheTransitionHandler->reverse("GenExpFade");
//				//m_generalsScreenAnimate->reverseAnimateWindow();
//		}
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

	// a column marked and then thought better of expires on its own, like an armed builder chord
	if( m_purchaseScienceColumn >= 0
			&& timeGetTime() - m_purchaseScienceColumnMs > CHORD_TIMEOUT_MS )
		clearPurchaseScienceColumn();

	// the promotion screen ships its own big yellow font; the key reads like a command bar label
	GameWindow *model = m_commandWindows[ 0 ];

	for( Int column = 0; column < PURCHASE_SCIENCE_COLUMNS; column++ )
	{
		GameWindow *candidate = purchaseScienceCandidate( column );

		// Legacy's number keys pick groups, as they did in the game as shipped, so its columns wear no key
		UnicodeString label;
		if( !TheGlobalData->isLegacyInput() &&
				( m_purchaseScienceColumn < 0 || m_purchaseScienceColumn == column ) )
			label = getMetaKeyLabel( (GameMessage::Type)( GameMessage::MSG_META_SELECT_TEAM1 + column ) );

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
				GadgetButtonSetText( win, UnicodeString( L"" ) );
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
		m_purchaseScienceColumnMs = timeGetTime();
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

	// the three panels the minimised bar stands down.  Watching, the middle is the player list, which
	// the spectator page and the Tab scoreboard have taken over, and the right is the selection's portrait
	showPanel( CB_PANEL_LEFT, TRUE );
	showPanel( CB_PANEL_CENTER, !m_isObserverCommandBar );
	showPanel( CB_PANEL_RIGHT, !m_isObserverCommandBar || getSelectedPlayer() != NULL );

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

	//
	// Minimised is all three panels gone, not the whole bar slid down a tenth of the screen with its
	// top edge still showing.  Sliding it left a strip of metal, a readable money box and the top of
	// the radar dish lying along the bottom of the picture, which is neither the bar nor out of the
	// way.  The one thing that stays is the button that puts it all back: applyPanelSlide rides it
	// down with the selection panel and stops it on the bottom edge of the screen.
	//
	TheTacticalView->setHeight((Int)(TheDisplay->getHeight()));
	m_contextParent[ CP_MASTER ]->winSetPosition(m_defaultControlBarPosition.x, m_defaultControlBarPosition.y);
	m_contextParent[ CP_MASTER ]->winHide(FALSE);

	showPanel( CB_PANEL_LEFT, FALSE );
	showPanel( CB_PANEL_CENTER, FALSE );
	showPanel( CB_PANEL_RIGHT, FALSE );

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

	CommandButton *cmdButton = findNonConstCommandButton( "Command_StructureExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_TransportExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_BunkerExit" );
	if(cmdButton)
		cmdButton->setButtonImage(image);

	cmdButton = findNonConstCommandButton( "Command_FireBaseExit" );
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
enum{
	RADAR_ATTACK_GLOW_FRAMES = 150,
	RADAR_ATTACK_GLOW_NUM_TIMES = 15  ///< number of times we'll flash
};

void ControlBar::triggerRadarAttackGlow( void )
{
	if(!m_radarAttackGlowWindow)
		return;
	m_radarAttackGlowOn = TRUE;
	m_remainingRadarAttackGlowFrames = RADAR_ATTACK_GLOW_FRAMES;
	if(BitTest(m_radarAttackGlowWindow->winGetStatus(),WIN_STATUS_ENABLED) == TRUE)
		m_radarAttackGlowWindow->winEnable(FALSE);
}

void ControlBar::updateRadarAttackGlow ( void )
{
	if(!m_radarAttackGlowOn || !m_radarAttackGlowWindow)
		return;
	m_remainingRadarAttackGlowFrames--;
	if(m_remainingRadarAttackGlowFrames <= 0)
	{
		m_radarAttackGlowOn = FALSE;
		m_radarAttackGlowWindow->winEnable(TRUE);
		return;
	}
	
	if(m_remainingRadarAttackGlowFrames % RADAR_ATTACK_GLOW_NUM_TIMES == 0)
	{
		m_radarAttackGlowWindow->winEnable(!BitTest(m_radarAttackGlowWindow->winGetStatus(),WIN_STATUS_ENABLED));
	}

	
}
void ControlBar::initSpecialPowershortcutBar( Player *player)
{

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
/** The shipped layout is one slot wide and eleven tall, so a general late in a game runs his powers
	* from the radar all the way to the top of the screen - a column nobody can take in at a glance.
	* The same slots are re-laid here as rows of SPECIAL_POWER_SHORTCUT_COLS, filling right to left
	* and then upward, so the first shortcut keeps the corner it has always been in and the bar grows
	* sideways instead of climbing.
	*
	* Every measurement is read back off the layout - each side ships its own bar, and the loader has
	* already scaled it to the running resolution - so nothing here is in 800x600 pixels. */
//-------------------------------------------------------------------------------------------------
void ControlBar::arrangeSpecialPowerShortcutGrid( void )
{
	if( m_specialPowerShortcutParent == NULL || m_currentlyUsedSpecialPowersButtons < 2 )
		return;

	//
	// the two lowest slots give the step the artwork was drawn with: the column runs upward, so
	// the second slot sits above the first and the difference is one row
	//
	Int firstX, firstY, secondX, secondY, slotWidth, slotHeight;
	m_specialPowerShortcutButtonParents[ 0 ]->winGetPosition( &firstX, &firstY );
	m_specialPowerShortcutButtonParents[ 1 ]->winGetPosition( &secondX, &secondY );
	m_specialPowerShortcutButtonParents[ 0 ]->winGetSize( &slotWidth, &slotHeight );

	const Int rowStep = firstY - secondY;
	if( rowStep <= 0 || slotWidth <= 0 || slotHeight <= 0 )
		return;

	ICoord2D cameoSize, cameoOffset;
	Int columnStep;
	if( getSpecialPowerTrayLayout( NULL, &cameoSize, &cameoOffset, &columnStep ) == FALSE )
		return;

	const Int columns = MIN( m_currentlyUsedSpecialPowersButtons, (Int)SPECIAL_POWER_SHORTCUT_COLS );
	const Int widen = ( columns - 1 ) * columnStep;

	//
	// the bar itself has to cover the columns we are adding.  winPointInChild only descends into
	// the children of a window the cursor is already inside, so a button hanging off the left edge
	// of its parent would draw fine and never take a click.
	//
	Int barX, barY, barWidth, barHeight;
	m_specialPowerShortcutParent->winGetPosition( &barX, &barY );
	m_specialPowerShortcutParent->winGetSize( &barWidth, &barHeight );
	m_specialPowerShortcutParent->winSetSize( barWidth + widen, barHeight );
	m_specialPowerShortcutParent->winSetPosition( barX - widen, barY );

	//
	// slot positions are relative to that bar, so widening it to the left moves every slot right by
	// the same amount first: the rightmost column has to land back where the old column stood
	//
	for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		GameWindow *slot = m_specialPowerShortcutButtonParents[ i ];
		GameWindow *button = m_specialPowerShortcutButtons[ i ];
		if( slot == NULL || button == NULL )
			continue;

		const Int column = i % SPECIAL_POWER_SHORTCUT_COLS;
		const Int row = i / SPECIAL_POWER_SHORTCUT_COLS;

		slot->winSetPosition( firstX + widen - column * columnStep, firstY - row * rowStep );

		//
		// the cameo fills that hole exactly, rounded to the nearest pixel.  The layout put it at
		// 1,7 39x27, which sat over the frame on the left and left a gap under it
		//
		button->winSetSize( cameoSize.x, cameoSize.y );
		button->winSetPosition( cameoOffset.x, cameoOffset.y );
	}

	//
	// children draw from the tail of the list back to the head, so the head is the one on top.
	// Bringing them to the top in reverse leaves slot 1 at the head and the order running down
	// the list from there: in a row the slot to the right covers the one to its left, and the row
	// nearest the corner covers the row above it.
	//
	for( Int j = MAX_SPECIAL_POWER_SHORTCUTS - 1; j >= 0; j-- )
		if( m_specialPowerShortcutButtonParents[ j ] )
			m_specialPowerShortcutButtonParents[ j ]->winBringToTop();
}

//-------------------------------------------------------------------------------------------------
/** The page draws the cell behind each power, so the slot's own tray picture, in the side's art,
	* is not drawn under it. */
static void drawNoTray( GameWindow *window, WinInstanceData *instData )
{
}

Int ControlBar::placeSpecialPowerShortcutGrid( const ICoord2D *corner, const ICoord2D &cell, Int gap )
{
	if( m_specialPowerShortcutParent == NULL || m_specialPowerShortcutButtonParents[ 0 ] == NULL )
		return 0;

	if( corner == NULL || m_currentlyUsedSpecialPowersButtons < 1 )
	{
		if( !m_specialPowerShortcutParent->winIsHidden() )
			m_specialPowerShortcutParent->winHide( TRUE );
		return 0;
	}

	// the powers showing, not the slots the side has: m_currentlyUsedSpecialPowersButtons is eleven
	// for a general with one power ready
	const Int shown = countVisibleSpecialPowerShortcuts();
	if( shown < 1 )
		return 0;

	// the bar covers every cell, or a cell off its edge draws and never takes a click, and it is only
	// as big as the powers shown, so the battlefield round them takes its own clicks.  Its position is
	// its own parent's, and the layout's root is not the screen
	const Int columns = MIN( shown, (Int)SPECIAL_POWER_SHORTCUT_COLS );
	const Int rows = ( shown + SPECIAL_POWER_SHORTCUT_COLS - 1 ) / SPECIAL_POWER_SHORTCUT_COLS;
	const Int width = columns * cell.x + ( columns - 1 ) * gap;
	const Int height = rows * cell.y + ( rows - 1 ) * gap;
	Int parentX = 0, parentY = 0;
	if( m_specialPowerShortcutParent->winGetParent() )
		m_specialPowerShortcutParent->winGetParent()->winGetScreenPosition( &parentX, &parentY );
	m_specialPowerShortcutParent->winSetPosition( corner->x - width - parentX, corner->y - height - parentY );
	m_specialPowerShortcutParent->winSetSize( width, height );
	m_specialPowerShortcutParent->winSetDrawFunc( drawNoTray );
	if( m_specialPowerShortcutParent->winIsHidden() )
		m_specialPowerShortcutParent->winHide( FALSE );

	// the first power in the corner, the row running left from it and the next row over it: the
	// order the row keys count in.  Each cameo fills its cell
	for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
	{
		GameWindow *slot = m_specialPowerShortcutButtonParents[ i ];
		GameWindow *button = m_specialPowerShortcutButtons[ i ];
		if( slot == NULL || button == NULL )
			continue;

		const Int column = i % SPECIAL_POWER_SHORTCUT_COLS;
		const Int row = i / SPECIAL_POWER_SHORTCUT_COLS;
		slot->winSetPosition( width - ( column + 1 ) * cell.x - column * gap, height - ( row + 1 ) * cell.y - row * gap );
		slot->winSetSize( cell.x, cell.y );
		slot->winSetDrawFunc( drawNoTray );
		button->winSetSize( cell.x, cell.y );
		button->winSetPosition( 0, 0 );
	}
	return shown;
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
							for( i = 0; !found && i < MAX_PURCHASE_SCIENCE_RANK_1; i++ )
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
							for( i = 0; !found && i < MAX_PURCHASE_SCIENCE_RANK_3; i++ )
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
							for( i = 0; !found && i < MAX_PURCHASE_SCIENCE_RANK_8; i++ )
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
	// behind a science are left out: a general's power is bought, never captured.
	//
	for( Int t = 0; t < ThePlayerTemplateStore->getPlayerTemplateCount(); ++t )
	{
		const PlayerTemplate *otherFaction = ThePlayerTemplateStore->getNthPlayerTemplate( t );
		if( otherFaction == player->getPlayerTemplate() || otherFaction->getSpecialPowerShortcutCommandSet().isEmpty() )
			continue;
		const CommandSet *otherSet = findCommandSet( otherFaction->getSpecialPowerShortcutCommandSet() );
		if( otherSet == NULL )
			continue;

		for( Int b = 0; b < MAX_COMMANDS_PER_SET && currentButton < MAX_SPECIAL_POWER_SHORTCUTS; ++b )
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
			if( player->findMostReadyShortcutSpecialPowerOfType( type ) == NULL )
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
				win->winHide( TRUE );
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

		// the button's text: the key that presses it (SHORTCUT_SLOTnn, F1..), then how many
		// of its superweapons are ready - a "1" is superfluous (Lorenzen)
		UnicodeString text;

		//
		// a power takes two keys, so only the half of them that the next press can reach is
		// labelled: with no row pending that is the head of each row, carrying the key that
		// picks the row; with one pending it is that row's powers, carrying their own keys.
		// Which key a slot number means is up to CommandMap.ini - getMetaKeyLabel returns
		// nothing for an unbound one.
		//
		Int keySlot = -1;
		if( m_specialPowerShortcutRow < 0 )
		{
			if( i % SPECIAL_POWER_SHORTCUT_COLS == 0 )
				keySlot = i / SPECIAL_POWER_SHORTCUT_COLS;
		}
		else if( i / SPECIAL_POWER_SHORTCUT_COLS == m_specialPowerShortcutRow )
			keySlot = i % SPECIAL_POWER_SHORTCUT_COLS;

		// the power keys are this fork's; Legacy's powers are clicked, and wear only their ready count
		if( keySlot >= 0 && keySlot < MAX_SPECIAL_POWER_SHORTCUTS && !TheGlobalData->isLegacyInput() )
			text = getMetaKeyLabel( (GameMessage::Type)(GameMessage::MSG_META_SHORTCUT_SLOT01 + keySlot) );

		const SpecialPowerTemplate *spTemplate = command->getSpecialPowerTemplate();
		Int numReady = 0;
		if( spTemplate )
			numReady = ThePlayerList->getLocalPlayer()->countReadyShortcutSpecialPowersOfType( spTemplate->getSpecialPowerType() );
		if( numReady > 1 )
		{
			UnicodeString count;
			count.format( L"%d", numReady );
			if( !text.isEmpty() )
				text.concat( L" " );
			text.concat( count );
		}

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
	if(!m_specialPowerShortcutParent)
		return;
	
	m_specialPowerShortcutParent->winHide(TRUE);
		
}
