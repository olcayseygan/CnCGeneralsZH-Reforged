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

// FILE: ControlBarCommand.cpp ////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Methods specific to the control bar unit commands
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/NameKeyGenerator.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "Common/BuildAssistant.h"
#include "Common/GameEngine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/MobNexusContain.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Module/VeterancyGainCreate.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Weapon.h"

#include "GameClient/InGameUI.h"
#include "GameClient/Drawable.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/HotKey.h"
#include "GameClient/Image.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static GameWindow *commandWindows[ MAX_COMMANDS_PER_SET ];
Bool commandWindowsInitialized = FALSE;
/** Translucent black scrim swept off the cameo as production progresses.
	*
	* It is drawn with the *inverse* clock - covered at 0%, fully uncovered at 100% - and that is the
	* whole point. The forward clock (`W3DDisplay::drawRectClock`) early-outs on `percent < 1` and
	* draws nothing at all, so an upgrade you had just clicked looked exactly like one you had not:
	* the queue entry existed, the money was spent, and the button gave back no sign of it until the
	* first percent ticked over. Starting covered means the click shows on screen the instant it
	* lands, and the cameo reads as "uncovering" rather than "filling up with black". */
static Color BuildClockColor = GameMakeColor(0,0,0,120);

//-------------------------------------------------------------------------------------------------
/** Logic frames to the whole seconds they will really take, rounded up and never down to zero: a
	* button that still has work left on it must not read "0s".
	*
	* Real seconds, not "logic frames over thirty". The logic rate is a knob in this fork (the game
	* speed keys move it between 5 and 200), and a build time is a promise about how long you will
	* be waiting - so at double speed a 20s barracks has to read 10s, and in a match that has sunk to
	* a third of its rate it has to read 60s. The rate is the one measured over the last second, not
	* the one asked for (`GameEngine::getLogicFramesPerSecond`). `LOGICFRAMES_PER_SECOND` is
	* only the fallback for the frames before the engine has a rate at all. */
//-------------------------------------------------------------------------------------------------
Int ControlBar_secondsFromFramesAt( Real frames, Int logicFps )
{
	if( frames <= 0.0f )
		return 0;

	if( logicFps <= 0 )
		logicFps = LOGICFRAMES_PER_SECOND;

	Int seconds = (Int)( frames / logicFps + 0.999f );
	return seconds < 1 ? 1 : seconds;
}

Int ControlBar_secondsFromFrames( Real frames )
{
	return ControlBar_secondsFromFramesAt( frames,
										TheGameEngine ? TheGameEngine->getLogicFramesPerSecond() : 0 );
}

//-------------------------------------------------------------------------------------------------
/** Is any selected building of the local player's researching or holding 'upgrade' in its queue?
	* Hands back the queue entry when the upgrade is the one being worked on right now (that is the
	* one that has progress to show), and NULL when it is merely waiting its turn. */
//-------------------------------------------------------------------------------------------------
static Bool findUpgradeInSelection( const UpgradeTemplate *upgrade, const ProductionEntry **head )
{
	if( head )
		*head = NULL;

	if( upgrade == NULL || TheInGameUI == NULL || ThePlayerList == NULL )
		return FALSE;

	Bool found = FALSE;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		Object *obj = (*it)->getObject();
		if( obj == NULL || obj->getControllingPlayer() != ThePlayerList->getLocalPlayer() )
			continue;

		ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		if( pu == NULL || pu->isUpgradeInQueue( upgrade ) == FALSE )
			continue;

		found = TRUE;

		const ProductionEntry *first = pu->firstProduction();
		if( head && first && first->getProductionType() == PRODUCTION_UPGRADE &&
				first->getProductionUpgrade() == upgrade )
		{
			*head = first;
			break;			// this one is actually working on it, no better answer to be had
		}
	}

	return found;
}
// STATIC DATA STORAGE ////////////////////////////////////////////////////////////////////////////
ControlBar::ContainEntry ControlBar::m_containData[ MAX_COMMANDS_PER_SET ];

//-------------------------------------------------------------------------------------------------
/** Note, this iterate callback assumes that the inventory exit buttons appear in a
	* continuous order in the layout of the command set */
//-------------------------------------------------------------------------------------------------
struct PopulateInvButtonData
{
	Int currIndex;					 ///< index that represents the control we're talking about
	Int maxIndex;					   ///< this is the last valid control we can use
	GameWindow **controls;   ///< the controls
	Object *transport;			 ///< the transport
};

//-------------------------------------------------------------------------------------------------
/** Used for the callback iterator on transport contents to do the actual GUI fill */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateInvDataCallback( Object *obj, void *userData )
{
	PopulateInvButtonData *data = (PopulateInvButtonData *)userData;

	//
	// if we're beyond the max the GUI can support, design needs to change the parameters
	// of the transport object to carry less things
	//
	if( data->currIndex > data->maxIndex )
	{

		DEBUG_ASSERTCRASH( 0, ("There is not enough GUI slots to hold the # of items inside a '%s'\n", 
													data->transport->getTemplate()->getName().str()) );
		return;

	}  // end if

	// get the window control that we're going to put our smiling faces in
	GameWindow *control = data->controls[ data->currIndex ];
	DEBUG_ASSERTCRASH( control, ("populateInvDataCallback: Control not found\n") );

	// assign our control and object id to the transport data
	m_containData[ data->currIndex ].control = control;
	m_containData[ data->currIndex ].objectID = obj->getID();
	data->currIndex++;

	// fill out the control enabled, hilite, and pushed images
	const Image *image;
	image = obj->getTemplate()->getButtonImage();
	GadgetButtonSetEnabledImage( control, image );

	//No longer used
	//image = TheMappedImageCollection->findImageByName( obj->getTemplate()->getInventoryImageName( INV_IMAGE_HILITE ) );
	//GadgetButtonSetHiliteImage( control, image );
	//image = TheMappedImageCollection->findImageByName( obj->getTemplate()->getInventoryImageName( INV_IMAGE_PUSHED ) );
	//GadgetButtonSetHiliteSelectedImage( control, image );

	//Show the contained object's veterancy symbol!
	image = calculateVeterancyOverlayForObject( obj );
	GadgetButtonDrawOverlayImage( control, image );

	// enable the control
	control->winEnable( TRUE );

}  // end populateInvDataCallback

//-------------------------------------------------------------------------------------------------
/** Transports have an extra special manipulation of the user interface.  They get to look
	* at the available command set, and any of the commands that are TransportExit commands *AND*
	* there is actually an object to represent that slot contained in the transport, the
	* inventory picture of the contained object will be displayed in the window control for
	* that TransportExit command.  Also, transports will HIDE any TransportExit controls found
	* in the command set that represent slots that *DO NOT EXIST* for the transport (that is,
	* the transport can only hold 4 things, but the GUI has buttons for 8 things).  For slots
	* that are empty but present in the transport the UI will show a disabled button to show
	* the user that there is an open "slot" */
//-------------------------------------------------------------------------------------------------
void ControlBar::doTransportInventoryUI( Object *transport, const CommandSet *commandSet )
{
/// @todo srj -- remove hard-coding here, please
	//static const CommandButton *exitCommand = findCommandButton( "Command_TransportExit" );
		
	// sanity
	if( transport == NULL || commandSet == NULL )
		return;

	// get the transport contain module
	ContainModuleInterface *contain = transport->getContain();
	
	// sanity
	if( contain == NULL )
		return;

	// how many slots do we have inside the transport
	Int transportMax = contain->getContainMax();

	//
	// first, hide any windows in 'm_commandWindows' that correspond to TransportExit commands
	// within the 'commandSet' that are overflow slots (the ui could be showing more inventory
	// exit button slots than there are slots in the transport)
	//
	// The extra slots bit means that a tank that takes up three slots will make two transport
	// buttons disappear off the end to show he takes up more room.
	transportMax = transportMax - contain->getExtraSlotsInUse();

	Int firstInventoryIndex = -1;
	Int lastInventoryIndex = -1;
	Int inventoryCommandCount = 0;

	const CommandButton *commandButton;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		// our implementation doesn't necessarily make use of the max possible command buttons
		if (! m_commandWindows[ i ]) continue;

		// get command button
		commandButton = commandSet->getCommandButton(i);

		// is this an inventory exit command
		if( commandButton && commandButton->getCommandType() == GUI_COMMAND_EXIT_CONTAINER )
		{

			// record the index of the control for the first inventory exit command we found
			if( firstInventoryIndex == -1 )
				firstInventoryIndex = i;

			//
			// since we're assuming all inventory exit commands appear in a continuous order,
			// we need to also need to keep track of what is the last valid inventory commadn index
			//
			lastInventoryIndex = i;

			// increment our count of available inventory exit commands found for the set
			inventoryCommandCount++;

			// show the window, but disable by default unless something is actually loaded in there
			m_commandWindows[ i ]->winHide( FALSE );
			m_commandWindows[ i ]->winEnable( FALSE );

      
///////// poopy

			//Clear any potential veterancy rank, or else we'll see it when it's empty!
			GadgetButtonDrawOverlayImage( m_commandWindows[ i ], NULL );
			
			//Unmanned vehicles don't have any commands available -- in fact they are hidden!
 			if( transport->isDisabledByType( DISABLED_UNMANNED ) )
 			{
 				m_commandWindows[ i ]->winHide( TRUE );
 			}

      
     //  is this where we set the cameos disabled when container is subdued?

			// if we've counted more UI spots than the transport can hold, hide this command window
			if( inventoryCommandCount > transportMax )
				m_commandWindows[ i ]->winHide( TRUE );

			//
			// set the inventory exit command into the window (even if it's one of the hidden ones
			// it's OK cause we'll never see it to click it
			//
			setControlCommand( m_commandWindows[ i ], commandButton );

		}  // end if

	}  // end for i
	
	// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
	// removed from multiplayer branch
	//showCommandMarkers();

	//
	// now, iterate the contained items of the transport and for each one we find we will
	// populate a user interface button with its inventory picture and store the inventory
	// data inside the control bar so we can respond to the button when its clicked
	//
	if( lastInventoryIndex >= 0 )  // just for sanity
	{
		PopulateInvButtonData data;

		data.controls = m_commandWindows;
		data.currIndex = firstInventoryIndex;
		data.maxIndex = lastInventoryIndex;
		data.transport = transport;
		contain->iterateContained( populateInvDataCallback, &data, FALSE );

	}  // end if

	//
	// save the last recorded inventory count so we know when we have to redo the gui when
	// something exits or enters
	//
	m_lastRecordedInventoryCount = contain->getContainCount();

}  // end doTransportInventoryUI

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::populateCommand( Object *obj )
{
	const CommandSet *commandSet;
	Int i;
	Player *player = obj->getControllingPlayer();

	// reset contain data
	resetContainData();

	// reset the build queue data
	resetBuildQueueData();

	// get command set
	commandSet = TheControlBar->findCommandSet( obj->getCommandSetString() );

	// if no command set match is found hide all the buttons
	if( commandSet == NULL )
	{

		// hide all the buttons
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			if (m_commandWindows[ i ])
			{
				m_commandWindows[ i ]->winHide( TRUE );
			}

		// nothing left to do
		return;

	}  // end if

	// transports do extra special things with the user interface buttons
	if( obj->getContain()  &&  obj->getContain()->isDisplayedOnControlBar() )
		doTransportInventoryUI( obj, commandSet );

	// what goes in each slot: the command set, slot for slot, a script only button and a slot with
	// no window left empty
	const CommandButton *slot[ MAX_COMMANDS_PER_SET ];
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = commandSet->getCommandButton( i );
		slot[ i ] = ( m_commandWindows[ i ] && button && !BitTest( button->getOptions(), SCRIPT_ONLY ) ) ? button : NULL;
	}

	// a builder standing in for an empty selection offers its buildings, not its orders: nobody
	// asked that dozer to go and clear mines
	if( m_standInBuilderID != INVALID_DRAWABLE_ID && obj->getDrawable()
			&& obj->getDrawable()->getID() == m_standInBuilderID )
	{
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			if( slot[ i ] && ControlBar_namedCommandPlace( slot[ i ]->getName().str() ) == COMMAND_PLACE_CLEAR_MINES )
				slot[ i ] = NULL;
	}

	// the order of the bar's upgrades, which the command bar grid keys X C V B N
	{
		Int types[ MAX_COMMANDS_PER_SET ];
		for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
			types[ i ] = ( m_commandWindows[ i ] && slot[ i ] ) ? slot[ i ]->getCommandType() : GUI_COMMAND_NONE;
		HotKeyManager::commandBarUpgradeRanks( types, MAX_COMMANDS_PER_SET, m_commandUpgradeRank );
	}

	// populate the button with commands defined
	const CommandButton *commandButton;
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		// our implementation doesn't necessarily make use of the max possible command buttons
		if (! m_commandWindows[ i ]) continue;

		// get command button
		commandButton = slot[ i ];

		// if button is not present, just hide the window
		if( commandButton == NULL )
		{

			// hide window on interface
			m_commandWindows[ i ]->winHide( TRUE );

		}  // end if
		else
		{

			//Script only command -- don't show it in the UI.
			if( BitTest( commandButton->getOptions(), SCRIPT_ONLY ) )
			{
				m_commandWindows[ i ]->winHide( TRUE );
				continue;
			}
			//
			// inventory exit commands were a special case already taken care above ... we needed
			// to iterage through the containment for transport objects and fill out any available
			// inventory exit buttons on the UI
			//
			if( commandButton->getCommandType() != GUI_COMMAND_EXIT_CONTAINER )
			{

				// make sure the window is not hidden
				m_commandWindows[ i ]->winHide( FALSE );

				// enable by default
				m_commandWindows[ i ]->winEnable( TRUE );

				// populate the visible button with data from the command button
				setControlCommand( m_commandWindows[ i ], commandButton );

				//
				// commands that require sciences we don't have are hidden so they never show up
				// cause we can never pick "another" general technology throughout the game
				//
				if( BitTest( commandButton->getOptions(), NEED_SPECIAL_POWER_SCIENCE ) )
				{
					const SpecialPowerTemplate *power = commandButton->getSpecialPowerTemplate();

					if( power && power->getRequiredScience() != SCIENCE_INVALID )
					{
						if( commandButton->getCommandType() != GUI_COMMAND_PURCHASE_SCIENCE && 
								commandButton->getCommandType() != GUI_COMMAND_PLAYER_UPGRADE &&
								commandButton->getCommandType() != GUI_COMMAND_OBJECT_UPGRADE )
						{
							if( player->hasScience( power->getRequiredScience() ) == FALSE )
							{
								//Hide the power
								m_commandWindows[ i ]->winHide( TRUE );
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
												commandButton->copyButtonTextFrom( command, FALSE, TRUE );
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
												commandButton->copyButtonTextFrom( command, FALSE, TRUE );
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
												commandButton->copyButtonTextFrom( command, FALSE, TRUE );
												found = TRUE;
												break;
											}
										}
									}
								}
							}
						}
					}

				}  // end if			
						
			}  // end else

		}  // end else

	}  // end for i

	// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
	// removed from multiplayer branch
	//showCommandMarkers();


	//
	// for objects that have a production exit interface, we may have a rally point set.
	// if we do, we want to show that rally point in the world
	//
	ExitInterface *exit = obj->getObjectExitInterface();
	if( exit )
	{

		//
		// if a rally point is set, show the rally point, if we don't have it set hide any rally
		// point we might have visible 
		//
		showRallyPoint( exit->getRallyPoint() );

	}  // end if

	//
	// to avoid a one frame delay where windows may become enabled/disabled, run the update
	// at once to get it all in the correct state immediately
	//
	updateContextCommand();

}  // end populateCommand

//-------------------------------------------------------------------------------------------------
/** reset transport data */
//-------------------------------------------------------------------------------------------------
void ControlBar::resetContainData( void )
{
	Int i;

	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{

		m_containData[ i ].control = NULL;
		m_containData[ i ].objectID = INVALID_ID;

	}  // end for i

}  // end resetTransportData

//-------------------------------------------------------------------------------------------------
/** reset the build queue data we use to die queue entires to control */
//-------------------------------------------------------------------------------------------------
void ControlBar::resetBuildQueueData( void )
{
	Int i;

	for( i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
	{

		m_queueData[ i ].control = NULL;
		m_queueData[ i ].type = PRODUCTION_INVALID;
		m_queueData[ i ].productionID = PRODUCTIONID_INVALID;
		m_queueData[ i ].upgradeToResearch = NULL;

	}  // end for i

}  // end resetBuildQueue

//-------------------------------------------------------------------------------------------------
/** attach the ButtonQueueNN windows to m_queueData and wipe every button back to its empty,
	* disabled state.  Shared by the production build queue and the multi-select unit list,
	* which reuse the same windows. */
//-------------------------------------------------------------------------------------------------
void ControlBar::resetBuildQueueButtons( void )
{
	static NameKeyType buildQueueIDs[ MAX_BUILD_QUEUE_BUTTONS ];
	static Bool idsInitialized = FALSE;
	Int i;

	// reset the build queue data
	resetBuildQueueData();

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

		// disable window by default
		m_queueData[ i ].control->winEnable( FALSE );

		//Clear the status because this button doesn't use it -- and if it's set, it'll
		//become invisible meaning the image that was there will be showed.
		m_queueData[ i ].control->winClearStatus( WIN_STATUS_USE_OVERLAY_STATES );

		// set the text of the window to nothing by default
		GadgetButtonSetText( m_queueData[ i ].control, UnicodeString( u"" ) );

		//Clear any potential veterancy rank, or else we'll see it when it's empty!
		GadgetButtonDrawOverlayImage( m_queueData[ i ].control, NULL );

		//Clear any multi-select focus border left behind
		GadgetButtonSetBorder( m_queueData[ i ].control, GAME_COLOR_UNDEFINED, FALSE );
		GadgetButtonSetCount( m_queueData[ i ].control, 0 );

	}  // end for i

}  // end resetBuildQueueButtons

//-------------------------------------------------------------------------------------------------
UnsignedInt getQueueCap( const ProductionUpdateInterface *pu )
{
	return MIN( pu->getMaxQueueEntries(), (UnsignedInt)MAX_BUILD_QUEUE_BUTTONS );
}

//-------------------------------------------------------------------------------------------------
/** The build queue, EA's: the first MAX_BUILD_QUEUE_BUTTONS entries of the producer's queue
	* on the queue buttons, each a cancel of its own entry. */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateBuildQueue( Object *producer )
{
	static const CommandButton *cancelUnitCommand = findCommandButton( "Command_CancelUnitCreate" );
	static const CommandButton *cancelUpgradeCommand = findCommandButton( "Command_CancelUpgradeCreate" );

	resetBuildQueueButtons();

	ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
	Int windowIndex = 0;
	for( const ProductionEntry *production = pu->firstProduction();
			 production && windowIndex < MAX_BUILD_QUEUE_BUTTONS;
			 production = pu->nextProduction( production ), windowIndex++ )
	{
		GameWindow *control = m_queueData[ windowIndex ].control;
		control->winEnable( TRUE );
		control->winSetStatus( WIN_STATUS_USE_OVERLAY_STATES );
		if( production->getProductionType() == PRODUCTION_UNIT )
		{
			setControlCommand( control, cancelUnitCommand );
			m_queueData[ windowIndex ].type = PRODUCTION_UNIT;
			m_queueData[ windowIndex ].productionID = production->getProductionID();
			GadgetButtonSetEnabledImage( control, production->getProductionObject()->getButtonImage() );
			GadgetButtonDrawOverlayImage( control, calculateVeterancyOverlayForThing( production->getProductionObject() ) );
		}
		else
		{
			setControlCommand( control, cancelUpgradeCommand );
			m_queueData[ windowIndex ].type = PRODUCTION_UPGRADE;
			m_queueData[ windowIndex ].upgradeToResearch = production->getProductionUpgrade();
			GadgetButtonSetEnabledImage( control, production->getProductionUpgrade()->getButtonImage() );
		}
	}

	m_displayedQueueCount = pu->getProductionCount();

}  // end populateBuildQueue

//-------------------------------------------------------------------------------------------------
/** The queue stands where the portrait does while the producer has anything in it, the first
	* entry's build clock on its button, and the portrait comes back when it empties. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateBuildQueue( Object *obj, ProductionUpdateInterface *pu )
{
	GameWindow *queue = m_contextParent[ CP_BUILD_QUEUE ];
	const Bool busy = pu && pu->firstProduction() != NULL;
	if( !busy )
	{
		if( queue->winIsHidden() == FALSE )
		{
			queue->winHide( TRUE );
			setPortraitByObject( obj );
		}
		return;
	}

	// the portrait is put back by anything that re-evaluates the selection, so it is taken off
	// every frame the queue stands in its place, as EA did
	setPortraitByObject( NULL );
	if( queue->winIsHidden() )
	{
		queue->winHide( FALSE );
		populateBuildQueue( obj );
	}
	else if( pu->getProductionCount() != m_displayedQueueCount )
		populateBuildQueue( obj );

	GadgetButtonDrawInverseClock( m_queueData[ 0 ].control, pu->firstProduction()->getPercentComplete(), m_buildUpClockColor );

}  // end updateBuildQueue


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextCommand( void )
{
 	Object *obj = NULL;
	Int i;

	// get object
	if( m_currentSelectedDrawable )
		obj = m_currentSelectedDrawable->getObject();

	//
	// the contents of objects are ususally showed on the UI, when those contents change
	// we always to update the UI
	//
	ContainModuleInterface *contain = obj ? obj->getContain() : NULL;
	if( contain && contain->getContainMax() > 0 && 
			m_lastRecordedInventoryCount != contain->getContainCount() )
	{

		// record this ast the last known number
		m_lastRecordedInventoryCount = contain->getContainCount();

		// re-evaluate the UI because something has changed
		evaluateContextUI();
					
	}  // end if, transport

	// get production update for those objects that have one
	ProductionUpdateInterface *pu = obj ? obj->getProductionUpdateInterface() : NULL;

	// a lone producer with something queued shows its queue where its portrait was, as the game
	// shipped
	if( m_currContext == CB_CONTEXT_COMMAND )
		updateBuildQueue( obj, pu );
	else if( m_contextParent[ CP_BUILD_QUEUE ]->winIsHidden() == FALSE )
	{
		m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );
		setPortraitByObject( obj );
	}

	// evaluate each command on whether or not it should be enabled
	for( i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		GameWindow *win;
		const CommandButton *command;

		// our implementation doesn't necessarily make use of the max possible command buttons
		if (! m_commandWindows[ i ]) continue;

		// get the window
		win = m_commandWindows[ i ];

		// only consider commands for windows that are actually shown
		//`tbd: fix the bug here, that is that if we don't change the unit, we won't attempt to show 
		// these.
		if( win->winIsHidden() == TRUE )
			continue;

		// get the command from the control
		command = (const CommandButton *)GadgetButtonGetData(win);
		//command = (const CommandButton *)win->winGetUserData();
		if( command == NULL )
			continue;

		//
		// the clock survives until it is taken off, so every pass starts with a clean button and
		// whatever is still running below puts its own clock back on
		//
		GadgetButtonClearClock( win );
		win->winClearStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );

		// the spinning repeat mark: some selected factory builds this unit on repeat
		const Bool repeating = command->getCommandType() == GUI_COMMAND_UNIT_BUILD &&
													 selectionRepeatsUnit( command->getThingTemplate() );
		GadgetButtonSetRepeat( win, repeating );

		//
		// a unit build button wears a radial fill over the cameo while its unit is the one being
		// built, and right-click cancels the last one queued
		//
		if( command->getCommandType() == GUI_COMMAND_UNIT_BUILD && pu )
		{
			//
			// a queue belongs to one building. With several producers selected the bar shows the
			// representative's queue, which is a lie about the rest of the selection, so the radial
			// fill stays off (and right-click cancel is refused, see
			// ControlBar::cancelLastQueuedUnit).  The queue is counted on the queue buttons over the
			// portrait, not on the button.
			//
			if( m_currContext != CB_CONTEXT_MULTI_SELECT )
			{
				Int queued = 0;
				for( const ProductionEntry *p = pu->firstProduction(); p; p = pu->nextProduction( p ) )
					if( p->getProductionType() == PRODUCTION_UNIT &&
							p->getProductionObject() == command->getThingTemplate() )
						queued++;
				if( queued > 0 && obj->isLocallyControlled() )
					win->winSetStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );

				const ProductionEntry *first = pu->firstProduction();
				if( first && first->getProductionType() == PRODUCTION_UNIT &&
						first->getProductionObject() == command->getThingTemplate() )
					GadgetButtonDrawInverseClock( win, first->getPercentComplete(), BuildClockColor );
			}
		}

		//
		// an upgrade button wears the same radial fill while its upgrade is the one being
		// researched. An upgrade is never queued twice on one building, so there is no count badge
		// to go with it. The producer is looked for across the whole selection, not just on the
		// representative: with four barracks selected the one actually paying for the upgrade is
		// rarely the one the bar is drawn from, and the button showed no progress at all.
		//
		Bool upgradeQueuedHere = FALSE;
		if( command->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE ||
				command->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE )
		{
			const ProductionEntry *entry = NULL;
			Bool queuedHere = findUpgradeInSelection( command->getUpgradeTemplate(), &entry );

			if( queuedHere )
			{
				upgradeQueuedHere = TRUE;
				if( entry )
					GadgetButtonDrawInverseClock( win, entry->getPercentComplete(), BuildClockColor );
				else
					/* queued but not started - no percentage to show, so show the full scrim. Without
						 it a waiting upgrade is indistinguishable from one that was never ordered. */
					GadgetButtonDrawInverseClock( win, 0, BuildClockColor );
			}
		}

// LORENZEN COMMENTED THIS OUT 8/11
    // Reason: ExitCameos can be greyed out when the container object gets subdued 

//		// ignore transport/structure inventory commands, they are handled elsewhere
//		if( command->getCommandType() == GUI_COMMAND_EXIT_CONTAINER )
//		{
//			win->winSetStatus( WIN_STATUS_ALWAYS_COLOR ); //Don't let these buttons render in grayscale ever!
//			continue;
//		}
//		else
		{
			win->winClearStatus( WIN_STATUS_NOT_READY );
			win->winClearStatus( WIN_STATUS_ALWAYS_COLOR );
		}

		// is the command available - to any of the selected, not just to the representative
		CommandAvailability availability = getGroupCommandAvailability( command, obj, win );

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

		//
		// The button an upgrade is cancelled from is the button that upgrade is bought from, and a
		// disabled window never sees a mouse message at all (GameWindow::winPointInChild skips it),
		// so the one thing you could not right-click was the upgrade you wanted to take back.
		// Leave it enabled and merely colourless while it is being researched; a left click on it
		// costs nothing, ProductionUpdate::queueUpgrade refuses to queue the same upgrade twice.
		//
		if( upgradeQueuedHere && availability != COMMAND_HIDDEN )
		{
			win->winEnable( TRUE );
			win->winSetStatus( WIN_STATUS_ALWAYS_COLOR );
		}

		// the same for a unit on repeat that cannot be built right now, whatever the reason (money,
		// a lost prerequisite, the cap, a full queue, an unpowered factory): Ctrl+Alt click has to
		// reach the button to stop it, and a plain click is refused by the build path below with
		// the message it already has. Build buttons come back RESTRICTED far more often than
		// CANT_AFFORD (getCommandAvailability), so testing CANT_AFFORD alone left repeat stuck on.
		if( repeating && ( availability == COMMAND_CANT_AFFORD || availability == COMMAND_RESTRICTED ) )
			win->winEnable( TRUE );

		//Determine by the production type of this button, whether or not the created object
		//will have a veterancy rank
		if( command->getCommandType() != GUI_COMMAND_EXIT_CONTAINER )
		{
			//Already handled for contained members -- see ControlBar::populateButtonProc()
			const Image *image = calculateVeterancyOverlayForThing( command->getThingTemplate() );
			GadgetButtonDrawOverlayImage( win, image );
		}
    
		//
		// for check-like commands we will keep the push button "pushed" or "unpushed" depending
		// on the current running status of the command
		//
		if( BitTest( command->getOptions(), CHECK_LIKE ))
		{

			// sanity, check like commands should have windows that are check like as well
			DEBUG_ASSERTCRASH( BitTest( win->winGetStatus(), WIN_STATUS_CHECK_LIKE ),	
												 ("updateContextCommand: Error, gadget window for command '%s' is not check-like!\n",
												 command->getName().str()) );

			if( availability == COMMAND_ACTIVE )
				GadgetCheckLikeButtonSetVisualCheck( win, TRUE );
			else
				GadgetCheckLikeButtonSetVisualCheck( win, FALSE );

		}  // end if

	}  // end for i
	
	// After Every change to the m_commandWIndows, we need to show fill in the missing blanks with the images
	// removed from multiplayer branch
	//showCommandMarkers();

//	// if we have a build tooltip layout, update it with the new data.
//	repopulateBuildTooltipLayout(); 

}  // end updatecontextCommand

//-------------------------------------------------------------------------------------------------
const Image* ControlBar::calculateVeterancyOverlayForThing( const ThingTemplate *thingTemplate )
{
	VeterancyLevel level = LEVEL_REGULAR;

	if( !thingTemplate )
	{
		return NULL;
	}

	Player *player = ThePlayerList->getLocalPlayer();
	if( !player )
	{
		return NULL;
	}

	//See if the thingTemplate has a VeterancyGainCreate
	//This is HORROR CODE and needs to be optimized!
	const VeterancyGainCreateModuleData *data = NULL;
	AsciiString modName;
	const ModuleInfo& mi = thingTemplate->getBehaviorModuleInfo();
	for( Int modIdx = 0; modIdx < mi.getCount(); ++modIdx )
	{
		modName = mi.getNthName(modIdx);
		if( !modName.compare( "VeterancyGainCreate" ) )
		{
			data = (const VeterancyGainCreateModuleData*)mi.getNthData( modIdx );

			//It does, so see if the player has that upgrade
			if( data )
			{ 
				//If no science is specified, he gets it automatically (or check the science).
				if( data->m_scienceRequired == SCIENCE_INVALID || player->hasScience( data->m_scienceRequired ) )
				{
					//We do! So now check to see what the veterancy level would be.
					if( data->m_startingLevel > level )
					{
						level = data->m_startingLevel;
					}
				}
			}
		}
	}

	//Return the appropriate image (including NULL if no veterancy levels)
	switch( level )
	{
		case LEVEL_VETERAN:
			return m_rankVeteranIcon;
		case LEVEL_ELITE:
			return m_rankEliteIcon;
		case LEVEL_HEROIC:
			return m_rankHeroicIcon;
	}
	return NULL;;
}

//-------------------------------------------------------------------------------------------------
const Image* ControlBar::calculateVeterancyOverlayForObject( const Object *obj )
{
	if( !obj )
	{
		return NULL;
	}

	VeterancyLevel level = obj->getVeterancyLevel();

	//Return the appropriate image (including NULL if no veterancy levels)
	switch( level )
	{
		case LEVEL_VETERAN:
			return m_rankVeteranIcon;
		case LEVEL_ELITE:
			return m_rankEliteIcon;
		case LEVEL_HEROIC:
			return m_rankHeroicIcon;
	}
	return NULL;;
}

//-------------------------------------------------------------------------------------------------
static Int getRappellerCount(Object* obj)
{
	Int num = 0;
	const ContainedItemsList* items = obj->getContain() ? obj->getContain()->getContainedItemsList() : NULL;
	if (items)
	{
		for (ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
		{
			if ((*it)->isKindOf(KINDOF_CAN_RAPPEL))
			{
				++num;
			}
		}
	}
	return num;
}

//-------------------------------------------------------------------------------------------------
/** Ask a whole multi-selection whether it can do this, instead of asking its representative.
	*
	* The command bar picks one member of the focused type group to drive the context
	* (ControlBar::setMultiSelectFocus), and every button was judged against that one object. Select
	* four barracks where the first already has its upgrade and the button greys out, even though
	* three of them are waiting to buy it - and clicking it would have gone to the first one anyway.
	*
	* So: take the best availability any member of the selection can offer, and hand back the
	* member that offered it so processCommandUI can send the order there. The focused type is asked
	* first, so the member handed back is one the player is looking at whenever it can do the job.
	* Outside a multi-selection this is exactly getCommandAvailability on the one selected object. */
//-------------------------------------------------------------------------------------------------
CommandAvailability ControlBar::getGroupCommandAvailability( const CommandButton *command,
																														 Object *obj,
																														 GameWindow *win,
																														 Object **ableObj ) const
{
	if( ableObj )
		*ableObj = obj;

	CommandAvailability best = getCommandAvailability( command, obj, win );

	//
	// Hidden means the command does not belong on this bar at all - a different answer in kind
	// from "this one cannot right now" - so a hidden representative stays hidden. Everything else
	// is a matter of degree and the most permissive answer in the selection wins.
	//
	if( best == COMMAND_AVAILABLE || best == COMMAND_HIDDEN )
		return best;

	//
	// only a multi-selection has more than one object to ask. The other contexts that can hold
	// several selected drawables (an angry mob, whose nexus stands in for its members) send the
	// order to the representative no matter what, so lighting a button off another member there
	// would offer a click that goes nowhere.
	//
	if( m_currContext != CB_CONTEXT_MULTI_SELECT || TheInGameUI == NULL )
		return best;

	//
	// The focused type goes first so the order the button is sent to stays the one the player is
	// looking at, but the scan does not stop there: four airfields with one of them full is a
	// selection that can still take a plane, and asking only the focused type - or worse, only the
	// representative - greyed the button out over the one factory that happened to be full. The
	// click path (processCommandUI, GUI_COMMAND_UNIT_BUILD) already spreads the batch over every
	// selected factory that can take it, so the button has to be lit whenever any of them can.
	//
	const ThingTemplate *focused = ( m_multiSelectFocus >= 0 && m_multiSelectFocus < m_multiSelectGroupCount )
																 ? m_multiSelectGroupTemplate[ m_multiSelectFocus ] : NULL;

	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( Int pass = 0; pass < 2; pass++ )
	{
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			Drawable *draw = *it;
			if( draw == NULL )
				continue;

			// pass 0 is the focused type, pass 1 is everything else
			const Bool sameType = ( focused == NULL || draw->getTemplate() == focused );
			if( ( pass == 0 ) != sameType )
				continue;

			Object *other = draw->getObject();
			if( other == NULL || other == obj )
				continue;

			CommandAvailability avail = getCommandAvailability( command, other, win );
			if( avail == COMMAND_HIDDEN )
				continue;			// it is on the bar for the selection; this member just cannot say

			if( commandAvailabilityRank( avail ) > commandAvailabilityRank( best ) )
			{
				best = avail;
				if( ableObj )
					*ableObj = other;
				if( best == COMMAND_AVAILABLE )
					return best;
			}
		}

		// with no focused type the first pass already walked the whole selection
		if( focused == NULL )
			break;
	}

	return best;

}  // end getGroupCommandAvailability

//-------------------------------------------------------------------------------------------------
/** What's the status between 'obj' and the 'command' at present.  Can we do it?  Are
	* we already doing it?  Can ya dig it? */
//-------------------------------------------------------------------------------------------------
CommandAvailability ControlBar::getCommandAvailability( const CommandButton *command, 
																												Object *obj, 
																												GameWindow *win,
																												GameWindow *applyToWin,
																												Bool forceDisabledEvaluation ) const
{
	if(	command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT 
			|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT )
	{
		// A shortcut whose SpecialPower named a power that does not exist (INI::parseSpecialPowerTemplate)
		// stands for nothing:
		// it is hidden like a shortcut with no building behind it.
		if (ThePlayerList && ThePlayerList->getLocalPlayer() && command->getSpecialPowerTemplate())
			obj = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
		else
			obj = NULL;
	}

	//If we modify the button (like a gadget clock overlay), then sometimes we may wish to apply it to a specific different button.
	//But if we don't specify anything (default), then make them the same.
	if( !applyToWin )
	{
		applyToWin = win;
	}

	if (obj == NULL)
		return COMMAND_HIDDEN;	// probably better than crashing....

	Player *player = obj->getControllingPlayer();

	if (obj->testScriptStatusBit(OBJECT_STATUS_SCRIPT_DISABLED) || obj->testScriptStatusBit(OBJECT_STATUS_SCRIPT_UNPOWERED))
	{
		// if the object status is disabled or unpowered, you cannot do anything to it.
		return COMMAND_HIDDEN;
	}
	
	//Unmanned vehicles don't have any commands available -- in fact they are hidden!
 	if( obj->isDisabledByType( DISABLED_UNMANNED ) )
 	{
 		return COMMAND_HIDDEN;
 	}

	//It's possible for command buttons to be a single use only type of a button -- like detonating a nuke from a convoy truck.
	if( obj->hasSingleUseCommandBeenUsed() )
	{
		return COMMAND_RESTRICTED;
	}
	
	if( BitTest( command->getOptions(), MUST_BE_STOPPED ) )
	{
		//This button can only be activated when the unit isn't moving!
		AIUpdateInterface *ai = obj->getAI();
		if( ai && ai->isMoving() )
		{
			return COMMAND_RESTRICTED;
		}
	}
 
	//Other disabled objects are unable to use buttons -- so gray them out.
	Bool disabled = obj->isDisabled();
	
	// if we are only disabled by being underpowered, and this button doesn't care, well, fix it
	if (disabled
			&& BitTest(command->getOptions(), IGNORES_UNDERPOWERED) 
			&& obj->getDisabledFlags().test(DISABLED_UNDERPOWERED)
			&& obj->getDisabledFlags().count() == 1)
	{
		disabled = false;
	}

 	if (disabled && !forceDisabledEvaluation)
 	{

		GUICommandType commandType = command->getCommandType();
		if( commandType != GUI_COMMAND_SELL && 
				commandType != GUI_COMMAND_EVACUATE &&
				commandType != GUI_COMMAND_EXIT_CONTAINER && 
				commandType != GUI_COMMAND_BEACON_DELETE && 
				commandType != GUI_COMMAND_SET_RALLY_POINT && 
				commandType != GUI_COMMAND_STOP && 
				commandType != GUI_COMMAND_SWITCH_WEAPON )
		{
			if( getCommandAvailability( command, obj, win, applyToWin, TRUE ) == COMMAND_HIDDEN )
			{
				return COMMAND_HIDDEN;
			}
 			return COMMAND_RESTRICTED;
		}
 	}

	// if the command requires an upgrade and we don't have it we can't do it
	if( BitTest( command->getOptions(), NEED_UPGRADE ) )
	{
		const UpgradeTemplate *upgradeT = command->getUpgradeTemplate();
		if (upgradeT)
		{
			// upgrades come in the form of player upgrades and object upgrades
			if( upgradeT->getUpgradeType() == UPGRADE_TYPE_PLAYER )
			{
				if( player->hasUpgradeComplete( upgradeT ) == FALSE )
					return COMMAND_RESTRICTED;
			}
			else if( upgradeT->getUpgradeType() == UPGRADE_TYPE_OBJECT && 
							 obj->hasUpgrade( upgradeT ) == FALSE )
			{
				return COMMAND_RESTRICTED;
			}
		}
	}

	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu && pu->firstProduction() && BitTest( command->getOptions(), NOT_QUEUEABLE ) )
	{
		//This button is designated so that it is incapable of building this upgrade/object
		//when anything is in the production queue.
		return COMMAND_RESTRICTED;
	}

	//
	// The queue is shown on the nine build-queue buttons, so they are the cap (getQueueCap).
	//
	// >=, not ==: a shift-click queues several at once, so an exact-equality test stopped firing
	// once the count stepped past the cap.
	//
	Bool queueMaxed = pu ? ( pu->getProductionCount() >= getQueueCap( pu ) ) : FALSE;

	switch( command->getCommandType() )
	{
		case GUI_COMMAND_DOZER_CONSTRUCT:
		{
      const ThingTemplate * whatToBuild = command->getThingTemplate();
			// if the command is a dozer construct task and the object dozer is building anything
			// this command is not available
			if(whatToBuild)
			{
				BuildableStatus bStatus = whatToBuild->getBuildable();
				if (bStatus == BSTATUS_NO || (bStatus == BSTATUS_ONLY_BY_AI && obj->getControllingPlayer()->getPlayerType() != PLAYER_COMPUTER))
					return COMMAND_HIDDEN;
			}

			// sanity, non dozer object
			if( obj->isKindOf( KINDOF_DOZER ) == FALSE )
				return COMMAND_RESTRICTED;

			// get the dozer ai update interface
			DozerAIInterface* dozerAI = NULL;
			if( obj->getAIUpdateInterface() == NULL )
				return COMMAND_RESTRICTED;

			dozerAI = obj->getAIUpdateInterface()->getDozerAIInterface();

			DEBUG_ASSERTCRASH( dozerAI != NULL, ("Something KINDOF_DOZER must have a Dozer-like AIUpdate") );
			if( dozerAI == NULL )
				return COMMAND_RESTRICTED;

			//
			// A builder already on a job used to grey out every structure button it had, so
			// queueing the next building meant waiting for it or picking another dozer by hand.
			// The button stays live: MSG_DOZER_CONSTRUCT hands the job to the idle builder
			// nearest the site and only re-tasks a busy one when there is no idle one.
			//

			// return whether or not the player can build this thing
			if( player->canBuild( whatToBuild ) == FALSE )
				return COMMAND_RESTRICTED;

			if( !player->canAffordBuild( whatToBuild ) )
			{
				return COMMAND_RESTRICTED;//COMMAND_CANT_AFFORD;
			}
      

			break;
		}  

		case GUI_COMMAND_SELL:
		{
			// if this is a sell command, is the object marked as "This cannot be sold?"
			// if so, remove the button, otherwise, its available
			if (obj->testScriptStatusBit(OBJECT_STATUS_SCRIPT_UNSELLABLE))
				return COMMAND_HIDDEN;

    //since the container can be subdued, , M Lorenzen 8/11
      if ( obj->isDisabledByType( DISABLED_SUBDUED ) )
        return COMMAND_RESTRICTED;

			break;
		}

		case GUI_COMMAND_UNIT_BUILD:
		{
			// command is a unit build
			if(command->getThingTemplate())
			{
				BuildableStatus bStatus = command->getThingTemplate()->getBuildable();
				if (bStatus == BSTATUS_NO || (bStatus == BSTATUS_ONLY_BY_AI && obj->getControllingPlayer()->getPlayerType() != PLAYER_COMPUTER))
					return COMMAND_HIDDEN;
			}

			if( queueMaxed )
			{
				return COMMAND_RESTRICTED;
			}

			// return whether or not the player can build this thing
			//NOTE: Player::canBuild() only checks prerequisites!
			if( player->canBuild( command->getThingTemplate() ) == FALSE )
				return COMMAND_RESTRICTED;

			CanMakeType makeType = TheBuildAssistant->canMakeUnit( obj, command->getThingTemplate() );
			if( makeType == CANMAKE_MAXED_OUT_FOR_PLAYER || makeType == CANMAKE_PARKING_PLACES_FULL )
			{
				//Disable the button if the player has a max amount of these units in build queue or existance.
				return COMMAND_RESTRICTED;
			}
			if( makeType == CANMAKE_NO_MONEY )
			{
				return COMMAND_RESTRICTED; //COMMAND_CANT_AFFORD;
			}

			break;
		}  

		case GUI_COMMAND_PLAYER_UPGRADE:
		{
			if( queueMaxed || !canResearchWithStatus( obj->getStatusBits() ) )
			{
				return COMMAND_RESTRICTED;
			}
			// if we can build it, we must also NOT already have it or be building it
			if( player->hasUpgradeComplete( command->getUpgradeTemplate() ) == TRUE ||
					player->hasUpgradeInProduction( command->getUpgradeTemplate() ) == TRUE )
				return COMMAND_CANT_AFFORD;//COMMAND_RESTRICTED;

			// if this is an upgrade create we must be able to build it.
			if( TheUpgradeCenter->canAffordUpgrade( player, command->getUpgradeTemplate() ) == FALSE )
				return COMMAND_RESTRICTED;//COMMAND_CANT_AFFORD;

			for( Int i = 0; i < command->getScienceVec().size(); i++ )
			{
				ScienceType st = command->getScienceVec()[ i ];
				if( !player->hasScience( st ) )
				{
					return COMMAND_RESTRICTED;
				}
			}
			break;
		} 

		case GUI_COMMAND_OBJECT_UPGRADE:
		{
			if( queueMaxed || !canResearchWithStatus( obj->getStatusBits() ) )
			{
				return COMMAND_RESTRICTED;
			}
			// no production update, can't possibly do this command
			if( pu == NULL )
			{
				DEBUG_CRASH(("Objects that have Object-Level Upgrades must also have ProductionUpdate. Just cuz."));
				return COMMAND_RESTRICTED;
			}

			//
			// if this object already has this upgrade, or is researching it already in the queue
			// we will disable the button so you can't build another one
			//
			if( obj->hasUpgrade( command->getUpgradeTemplate() ) == TRUE ||
					pu->isUpgradeInQueue( command->getUpgradeTemplate() ) == TRUE ||
					obj->affectedByUpgrade( command->getUpgradeTemplate() ) == FALSE )
				return COMMAND_CANT_AFFORD;//COMMAND_RESTRICTED;

			if( TheUpgradeCenter->canAffordUpgrade( player, command->getUpgradeTemplate() ) == FALSE )
				return COMMAND_RESTRICTED;//COMMAND_CANT_AFFORD;

			for( Int i = 0; i < command->getScienceVec().size(); i++ )
			{
				ScienceType st = command->getScienceVec()[ i ];
				if( !player->hasScience( st ) )
				{
					return COMMAND_RESTRICTED;
				}
			}
			break;
		} 

		case GUI_COMMAND_FIRE_WEAPON:
		{
			AIUpdateInterface *ai = obj->getAIUpdateInterface();

			// no ai, can't possibly fire weapon
			if( ai == NULL )
				return COMMAND_RESTRICTED;

			// ask the ai if the weapon is ready to fire
			const Weapon* w = obj->getWeaponInWeaponSlot( command->getWeaponSlot() );

			// changed this to Log rather than Crash, because this can legitimately happen now for
			// dozers and workers with mine-clearing stuff... (srj)
			//DEBUG_ASSERTLOG( w, ("Unit %s's CommandButton %s is trying to access weaponslot %d, but doesn't have a weapon there in its FactionUnit ini entry.\n", 
			//	obj->getTemplate()->getName().str(), command->getName().str(), (Int)command->getWeaponSlot() ) );
			
			UnsignedInt now = TheGameLogic->getFrame();

			/// @Kris -- We need to show the button as always available for anything with a 0 clip reload time.
			if( w && w->getClipReloadTime( obj ) == 0 )
			{
				return COMMAND_AVAILABLE;
			}

			if( w == NULL																	// No weapon
				|| w->getStatus() != READY_TO_FIRE					// Weapon not ready
				|| w->getPossibleNextShotFrame() == now			// Weapon ready, but could fire this exact frame (handle button flicker since it may be going to fire anyway)
/// @todo srj -- not sure why this next check is necessary, but the Comanche missile buttons will flicker without it. figure out someday.
/// @todo ml  -- and note: that the "now-1" below causes zero-clip-reload weapons to never be ready, so I added this
/// If you make changes to this code, make sure that the DragonTank's firewall weapon can be retargeted while active,
/// that is, while the tank is squirting out flames all over the floor, you can click the firewall button (or "F"), 
/// and re-target the firewall without having to stop or move in-betwen.. Thanks for reading 
				|| (w->getPossibleNextShotFrame()==now-1) 	
				)
			{
				if ( w != NULL )
				{
					// only draw the clock when reloading a clip, not when merely between shots, since that's usually a tiny amount of time
					if ( w->getStatus() == RELOADING_CLIP)
					{
						Int percent = w->getPercentReadyToFire() * 100;
						GadgetButtonDrawInverseClock( applyToWin, percent, m_buildUpClockColor );
					}
					return COMMAND_NOT_READY;
				}
				else
				{
					// if this is a mine-clearing button but we don't have the right weaponset,
					// just declare it available... we'll switch weaponsets when the time comes
					if (
						(command->getOptions() & USES_MINE_CLEARING_WEAPONSET) != 0
						&& !obj->testWeaponSetFlag(WEAPONSET_MINE_CLEARING_DETAIL)
					)
					{
						return COMMAND_AVAILABLE;
					}

					// no weapon in the slot means "gray me out"
					return COMMAND_RESTRICTED;
				}
			} 

			break;
		}

		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
			// always available
			break;	

		case GUI_COMMAND_COMBATDROP:
		{
			if( getRappellerCount(obj) <= 0 )
				return COMMAND_RESTRICTED;
			break;
		}

		case GUI_COMMAND_EXIT_CONTAINER:
		{
			
			//
			// this method is really used as a per frame update to see if we should enable
			// disable a control ... inventory of objects shows as buttons have that enable
			// disable logic handled elsewhere, where if the contained count of the entire
			// container changes the UI is completely repopulated
			//

    //since the container can be subdued, the above is no longer true, M Lorenzen 8/11
      if ( obj->isDisabledByType( DISABLED_SUBDUED ) )
        return COMMAND_RESTRICTED;

			break;
		} 

		case GUI_COMMAND_EVACUATE:
		{

			// if we have no contained objects we can't evacuate anything
			if( !obj->getContain() || obj->getContain()->getContainCount() <= 0 )
				return COMMAND_RESTRICTED;

      if ( obj->isDisabledByType( DISABLED_SUBDUED ) )
        return COMMAND_RESTRICTED;


			break;
		}  

		case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
		{
			DockUpdateInterface *dui = obj->getDockUpdateInterface();

			// if the dock is closed or not present this command is invalid
			if( dui == NULL || dui->isDockOpen() == FALSE )
				return COMMAND_RESTRICTED;
			break;
		}  

		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT:
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT:
		{
			// sanity
			DEBUG_ASSERTCRASH( command->getSpecialPowerTemplate() != NULL,
												 ("The special power in the command '%s' is NULL\n", command->getName().str()) );

			// a silo whose missile is silenced has no such power at all, and no fire button
			if( SuperweaponMissileSilencedInMatch( command->getSpecialPowerTemplate()->getSpecialPowerType() ) )
				return COMMAND_HIDDEN;

			// get special power module from the object to execute it
			SpecialPowerModuleInterface *mod = obj->getSpecialPowerModule( command->getSpecialPowerTemplate() );

			if( mod == NULL )
			{
				// sanity ... we must have a module for the special power, if we don't somebody probably
				// forgot to put it in the object
				DEBUG_CRASH(( "Object %s does not contain special power module (%s) to execute.  Did you forget to add it to the object INI?\n",
											obj->getTemplate()->getName().str(), command->getSpecialPowerTemplate()->getName().str() ));

				// ...and in a release build that assert is nothing, so control used to fall out of
				// this chain and report the button as available - a power the object cannot even
				// perform was drawn ready to fire. There is nothing behind it, so hide it.
				return COMMAND_HIDDEN;
			}

			// Pro Rules: the power is there and the button stays dead
			if( ProRulesRefuseSpecialPower( obj->getControllingPlayer(), command->getSpecialPowerTemplate()->getSpecialPowerType() ) )
				return COMMAND_RESTRICTED;

			if( mod->isReady() == FALSE )
			{
				Int percent =  mod->getPercentReady() * 100;

				GadgetButtonDrawInverseClock( applyToWin, percent, m_buildUpClockColor );
				return COMMAND_NOT_READY;
			}
			else if( SpecialAbilityUpdate *spUpdate = obj->findSpecialAbilityUpdate( command->getSpecialPowerTemplate()->getSpecialPowerType() ) )
			{
				if( spUpdate && spUpdate->isPowerCurrentlyInUse( command ) )
				{
					return COMMAND_RESTRICTED;
				}
			}
			else if( mod->getSpecialPowerTemplate()->getSpecialPowerType() == SPECIAL_CHANGE_BATTLE_PLANS )
			{
				static NameKeyType key_BattlePlanUpdate = NAMEKEY( "BattlePlanUpdate" );
				BattlePlanUpdate *update = (BattlePlanUpdate*)obj->findUpdateModule( key_BattlePlanUpdate );
				if( update && update->getCommandOption() & command->getOptions() )
				{
					return COMMAND_ACTIVE;
				}
			}

			break;
		}  

		case GUI_COMMAND_TOGGLE_OVERCHARGE:
		{
			OverchargeBehaviorInterface *obi;
			// search object behavior mdoules
			for( BehaviorModule **bmi = obj->getBehaviorModules(); *bmi; ++bmi )
			{
				// we're looking for the overcharge interface
				obi = (*bmi)->getOverchargeBehaviorInterface();
				if( obi )
				{
					if( obi->isOverchargeActive() )
						return COMMAND_ACTIVE;
				} 
			}  
			break;
		} 

		// switch weapon command
		case GUI_COMMAND_SWITCH_WEAPON:
		{
			// ask the ai which weapon is in the current slot
			const Weapon* w = obj->getWeaponInWeaponSlot( command->getWeaponSlot() );

			DEBUG_ASSERTCRASH( w, ("Unit %s's CommandButton %s is trying to access weaponslot %d, but doesn't have a weapon there in its FactionUnit ini entry.", 
				obj->getTemplate()->getName().str(), command->getName().str(), (Int)command->getWeaponSlot() ) );
			
			if( w == NULL)
				return COMMAND_RESTRICTED;

			const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
			for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
			{
				Drawable *draw = *it;
				if( draw && draw->getObject() && draw->getObject()->isLocallyControlled() && draw->getObject()->getCurrentWeapon())
				{
					WeaponSlotType wslot = draw->getObject()->getCurrentWeapon()->getWeaponSlot();
					if (wslot != command->getWeaponSlot())
						return COMMAND_AVAILABLE;
				}
			}
			
			return COMMAND_ACTIVE;
		}
		
		case GUI_COMMAND_HACK_INTERNET:
		{
			AIUpdateInterface *ai = obj->getAI();
			if( ai )
			{
				HackInternetAIInterface *hackAI = ai->getHackInternetAIInterface();
				if( hackAI && hackAI->isHackingPackingOrUnpacking() )
				{
					return COMMAND_RESTRICTED;
				}
			}
			return COMMAND_AVAILABLE;
		}
		
		case GUI_COMMAND_STOP:
		{
			if( !BitTest( command->getOptions(), OPTION_ONE ) )
			{
				return COMMAND_AVAILABLE;
			}

			//We're dealing with a strategy center stop button. Only show the button
			//if we're in bombardment mode (to stop the artillery cannon).
			static NameKeyType key_BattlePlanUpdate = NAMEKEY( "BattlePlanUpdate" );
			BattlePlanUpdate *bpUpdate = (BattlePlanUpdate*)obj->findUpdateModule( key_BattlePlanUpdate );
			if( bpUpdate && bpUpdate->getActiveBattlePlan() != PLANSTATUS_BOMBARDMENT )
			{
				return COMMAND_RESTRICTED;
			}
			return COMMAND_AVAILABLE;
		}
		
		case GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE:
		{
			//We can *always* select a unit :)
			return COMMAND_AVAILABLE;
		}
	}
	
	// all is well with the command
	return COMMAND_AVAILABLE;

}  // end getCommandAvailability

