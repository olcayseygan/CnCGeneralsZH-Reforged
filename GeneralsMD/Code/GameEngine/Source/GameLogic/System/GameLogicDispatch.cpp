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

// FILE: GameLogicDispatch.cpp ////////////////////////////////////////////////////////////////////
// Author: Mike Booth, Colin Day
// Description: Message logic to drive the game play
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CRCDebug.h"
#include "Common/DrawnPath.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/StatsCollector.h"
#include "Common/Radar.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/AI.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/TerrainLogic.h"

//-------------------------------------------------------------------------------------------------
/** Which builder takes a structure job: the free one nearest the site, or failing that the
	* nearest one at all.  Free = no build/repair task and not hauling supplies; a builder that
	* is merely walking somewhere counts.  Fed from the selection, or from every builder of the
	* player when nothing is selected (the control bar's stand-in builder context). */
//-------------------------------------------------------------------------------------------------
struct BuilderPick
{
	Coord3D loc;
	Object *idle;
	Real idleDistSqr;
	Object *any;
	Real anyDistSqr;
};

static void considerBuilder( Object *candidate, BuilderPick *pick )
{
	if( candidate == NULL || candidate->isEffectivelyDead() )
		return;
	if( candidate->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || candidate->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	AIUpdateInterface *ai = candidate->getAI();
	DozerAIInterface *dozer = ai ? ai->getDozerAIInterface() : NULL;
	if( dozer == NULL )
		return;

	Real dx = candidate->getPosition()->x - pick->loc.x;
	Real dy = candidate->getPosition()->y - pick->loc.y;
	Real distSqr = dx*dx + dy*dy;
	if( distSqr < pick->anyDistSqr )
	{
		pick->any = candidate;
		pick->anyDistSqr = distSqr;
	}
	const SupplyTruckAIInterface *supply = ai->getSupplyTruckAIInterface();
	Bool hauling = supply && supply->isCurrentlyFerryingSupplies();
	if( !dozer->isAnyTaskPending() && !hauling && distSqr < pick->idleDistSqr )
	{
		pick->idle = candidate;
		pick->idleDistSqr = distSqr;
	}
}

static void considerBuilderProc( Object *obj, void *userData )
{
	considerBuilder( obj, (BuilderPick *)userData );
}
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"

#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GuiCallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/Shell.h"
#include "GameClient/Module/BeaconClientUpdate.h"
#include "GameClient/LookAtXlat.h"

#include "GameNetwork/NetworkInterface.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


#define MAX_PATH_SUBJECTS 64
static Bool theBuildPlan = false;
static Object *thePlanSubject[ MAX_PATH_SUBJECTS ];
static int thePlanSubjectCount = 0;
//static WindowLayout *background = NULL;

// ------------------------------------------------------------------------------------------------
/** Issue the movement command to the object */
// ------------------------------------------------------------------------------------------------
static void doMoveTo( Object *obj, const Coord3D *pos )
{
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	DEBUG_ASSERTCRASH(ai, ("Attemped doMoveTo() on an Object with no AI\n"));
	if (ai)
	{
		if (theBuildPlan)
		{
			int i;

			// if this object isn't in the buildPlan set, add it
			for( i=0; i<thePlanSubjectCount; i++ )
				if (thePlanSubject[i] == obj)
					break;

			if (i == thePlanSubjectCount)
				thePlanSubject[ thePlanSubjectCount++ ] = obj;

			ai->queueWaypoint( pos );
		}
		else
		{
			ai->clearWaypointQueue();
			obj->leaveGroup();
			obj->releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.
			ai->aiMoveToPosition( pos, CMD_FROM_PLAYER );
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void doSetRallyPoint( Object *obj, const Coord3D& pos )
{
	Bool isLocalPlayer = obj->isLocallyControlled();

	//
	// we must be able to find a path from the object to the point they have chosen, cause setting
	// a rally point at a invalid location would suck.  To be super nice, we have to make sure
	// that every type of object that can be created from the thing setting the rally point
	// can actually find a path from the thing to the point
	//

	// to see the never-finished code to check all locomotor sets, see past revs of GUICommandTranslator.cpp -MDC

	//
	// for now, just use the basic human locomotor ... and enable the above code when Steven
	// tells me how to get the locomotor sets based on a thing template (CBD)
	//
	NameKeyType key = NAMEKEY( "BasicHumanLocomotor" );
	LocomotorSet locomotorSet;
	locomotorSet.addLocomotor( TheLocomotorStore->findLocomotorTemplate( key ) );
	if( TheAI->pathfinder()->clientSafeQuickDoesPathExist( locomotorSet, obj->getPosition(), &pos ) == FALSE )
	{

		// user feedback
		if( isLocalPlayer )
		{

			// display error message to user
			TheInGameUI->message( TheGameText->fetch( "GUI:RallyPointNoPath" ) );

			// play the no can do sound
			static AudioEventRTS rallyNotSet("UnableToSetRallyPoint");
			rallyNotSet.setPosition(&pos);
			TheAudio->addAudioEvent(&rallyNotSet);

		}  // end if

		return;

	}  // end if

	// feedback to the player
	if( isLocalPlayer )
	{

		// print a message to the user
		UnicodeString info;
		info.format( TheGameText->fetch( "GUI:RallyPointSet" ), 
								 obj->getTemplate()->getDisplayName().str() );
		TheInGameUI->message( info );

		// play a sound for setting the rally point
		static AudioEventRTS rallyPointSet("RallyPointSet");
		rallyPointSet.setPosition(&pos);
		rallyPointSet.setPlayerIndex(obj->getControllingPlayer()->getPlayerIndex());
		TheAudio->addAudioEvent(&rallyPointSet);

		// mark the UI as dirty so that we re-evaluate the selection and show the rally point
		Drawable *draw = obj->getDrawable();
		if( draw && draw->isSelected() )
			TheControlBar->markUIDirty();

	}  // end if

	// if this object has a ProductionExitUpdate interface, we are setting a rally point
	ExitInterface *exitInterface = obj->getObjectExitInterface();
	if( exitInterface )
	{
		// set the rally point
		exitInterface->setRallyPoint( &pos );

	}

}

static Object * getSingleObjectFromSelection(const AIGroup *currentlySelectedGroup)
{
	// an empty group is not the same as no group: the iterator below was taken from an empty
	// vector and dereferenced
	if( currentlySelectedGroup && !currentlySelectedGroup->isEmpty() )
	{
		const VecObjectID& selectedObjects = currentlySelectedGroup->getAllIDs();
		DEBUG_ASSERTCRASH(selectedObjects.size() == 1, ("Trying to get single object from multiple selection!"));
		VecObjectID::const_iterator it = selectedObjects.begin();
		return TheGameLogic->findObjectByID(*it);
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::closeWindows( void )
{
	HideDiplomacy();
	ResetDiplomacy();
	HideInGameChat();
	ResetInGameChat();
	TheControlBar->hidePurchaseScience();
	TheControlBar->hideSpecialPowerShortcut();
	HideQuitMenu();
	
	// hide the options menu
	NameKeyType buttonID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonBack" );
	GameWindow *button = TheWindowManager->winGetWindowFromId( NULL, buttonID );
	GameWindow *window = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey("OptionsMenu.wnd:OptionsMenuParent") );
	if(window)
		TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																			(WindowMsgData)button, buttonID );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::clearGameData( Bool showScoreScreen )
{
	if( !isInGame() )
	{
		DEBUG_CRASH(("We tried to clear the game data when we weren't in a game"));
		return;
	}
	
	setClearingGameData( TRUE );

//	m_background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
//	DEBUG_ASSERTCRASH(m_background,("We Couldn't Load Menus/BlankWindow.wnd"));
//	m_background->hide(FALSE);
//	m_background->bringForward();
	// reset the game engine to accept data for a new game
	if(TheStatsCollector)
		TheStatsCollector->writeFileEnd();

	TheScriptActions->closeWindows(FALSE); // Close victory or defeat windows.

	Bool shellGame = FALSE;
	if ((!isInShellGame() || !isInGame()) && showScoreScreen)
	{
		shellGame = TRUE;
		TheTransitionHandler->setGroup("FadeWholeScreen");
		TheShell->push("Menus/ScoreScreen.wnd");
		TheShell->showShell(FALSE); // by passing in false, we don't want to run the Init on the shell screen we just pushed on
		TheTransitionHandler->reverse("FadeWholeScreen");

		void FixupScoreScreenMovieWindow( void );
		FixupScoreScreenMovieWindow();
	}

	TheGameEngine->reset();
	setGameMode(GAME_NONE);
//	m_background->bringForward();
//	if(shellGame)

	
	if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
	{
		TheGameEngine->setQuitting(TRUE);
	}

	HideControlBar();
	closeWindows();

	TheMouse->setVisibility(TRUE);

	if(m_background)
	{
		m_background->destroyWindows();
		m_background->deleteInstance();
		m_background = NULL;
	}

	setClearingGameData( FALSE );
	
}

// ------------------------------------------------------------------------------------------------
/** Prepare for a new game */
// ------------------------------------------------------------------------------------------------
void GameLogic::prepareNewGame( Int gameMode, GameDifficulty diff, Int rankPoints )
{
	//Added By Sadullah Nader
	//Fix for loading game scene

	//Kris: Commented this out, but leaving it around incase it bites us later. I cleaned up the 
	//      nomenclature. Look for setLoadingMap() and setLoadingSave()
	//setGameLoading(TRUE);

	TheScriptEngine->setGlobalDifficulty(diff);

	if(!m_background)
	{
		m_background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
		DEBUG_ASSERTCRASH(m_background,("We Couldn't Load Menus/BlankWindow.wnd"));
		m_background->hide(FALSE);
		m_background->bringForward();
	}
	m_background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	TheGameLogic->setGameMode( gameMode );
	if (!TheGlobalData->m_pendingFile.isEmpty())
	{
		TheWritableGlobalData->m_mapName = TheGlobalData->m_pendingFile;
		TheWritableGlobalData->m_pendingFile.clear();
	}

	m_rankPointsToAddAtGameStart = rankPoints;
	DEBUG_LOG(("GameLogic::prepareNewGame() - m_rankPointsToAddAtGameStart = %d\n", m_rankPointsToAddAtGameStart));

	// If we're about to start a game, hide the shell.
	if(!TheGameLogic->isInShellGame())
		TheShell->hideShell();

	m_startNewGame = FALSE;

}  // end prepareNewGame

//-------------------------------------------------------------------------------------------------
/** What each smoke signal says on an ally's screen, indexed by SignalKind.  The smoke is the sender's
	* colour, so the kind is told apart by the mark it lays on the ground.  No signal uses
	* RADAR_EVENT_UNDER_ATTACK, because Radar::tryEvent refuses a real attack warning within ten
	* seconds of one of those. */
//-------------------------------------------------------------------------------------------------
struct SignalLook
{
	RadarEventType radarEvent;
	const char *announcementLabel;
};

static const SignalLook SIGNAL_LOOKS[ SIGNAL_KIND_COUNT ] =
{
	{ RADAR_EVENT_BATTLE_PLAN,	"GUI:SignalAttackPlaced" },
	{ RADAR_EVENT_CONSTRUCTION,	"GUI:SignalDefendPlaced" },
	{ RADAR_EVENT_INFORMATION,	"GUI:SignalAttentionPlaced" },
};

static const char *SIGNAL_SMOKE_TEMPLATE = "BeaconSmokeFFFFFF";
/// the smoke, the mark on the ground and the radar ping all go together, as long as the feed's line
static const Real SIGNAL_SECONDS = 10.0f;

/// the smoke is fed for the first half of the signal and its last puff fades out over the second
static const UnsignedInt SIGNAL_HALF_FRAMES = (UnsignedInt)( SIGNAL_SECONDS * LOGICFRAMES_PER_SECOND / 2 );

static const UnsignedInt SIGNAL_COOLDOWN_FRAMES = LOGICFRAMES_PER_SECOND;

// The beacon template draws a column nine units wide that needs five seconds to climb, which at the
// signal's first three seconds and the default camera height is a dark speck.  Measured on screen,
// not derived: these make it a plume a tank's width across that a player finds at a glance.
static const Real SIGNAL_SMOKE_SIZE_SCALE = 5.0f;
static const Real SIGNAL_SMOKE_DENSITY_SCALE = 3.0f;
static const Real SIGNAL_SMOKE_RISE_SCALE = 3.0f;
static const Real SIGNAL_SMOKE_ALPHA_MIN = 0.6f;
static const Real SIGNAL_SMOKE_ALPHA_MAX = 0.8f;

//-------------------------------------------------------------------------------------------------
/** Is a signal on this frame too soon after the player's last one?  A last frame ahead of now is
	* left over from an earlier match, whose clock ran further, and holds nothing back. */
//-------------------------------------------------------------------------------------------------
Bool Signal_isThrottled( UnsignedInt now, UnsignedInt lastSignalFrame )
{
	return now >= lastSignalFrame && now - lastSignalFrame < SIGNAL_COOLDOWN_FRAMES;
}

//-------------------------------------------------------------------------------------------------
/** The heroic cheat: buildings and anything else that never ranks up are left alone. */
//-------------------------------------------------------------------------------------------------
static void makeObjectHeroic( Object *obj, void *userData )
{
	ExperienceTracker *tracker = obj->getExperienceTracker();
	if( tracker->isTrainable() )
		tracker->setVeterancyLevel( LEVEL_HEROIC );
}

//-------------------------------------------------------------------------------------------------
/** This message handles dispatches object command messages to the
  * appropriate objects.
	* @todo Rename this to "CommandProcessor", or similiar. */
//-------------------------------------------------------------------------------------------------
void GameLogic::logicMessageDispatcher( GameMessage *msg, AIGroup *orderedGroup )
{
#ifdef _DEBUG
	DEBUG_ASSERTCRASH(msg != NULL && msg != (GameMessage*)0xdeadbeef, ("bad msg"));
#endif

	//
	// This used to be an assertion, which is nothing at all in a release build, and the very next
	// thing the function does with a network message is thisPlayer->getCurrentSelectionAsAIGroup().
	// The index is not generated here for anything that comes from outside: a replay file supplies it
	// with a raw fread whose failure leaves the -1 it was seeded with, and a network command gets it
	// from the sending machine.  An index naming nobody is a command that cannot be carried out, so
	// drop it rather than dereference the NULL.
	//
	Player *thisPlayer = ThePlayerList->getNthPlayer( msg->getPlayerIndex() );
	if( thisPlayer == NULL )
	{
		DEBUG_CRASH( ("logicMessageDispatcher: Processing message from unknown player (player index '%d')",
			msg->getPlayerIndex()) );
		return;
	}

	AIGroup *currentlySelectedGroup = orderedGroup;

	if (isInGame() && orderedGroup == NULL)
	{
		if (msg->getType() >= GameMessage::MSG_BEGIN_NETWORK_MESSAGES && msg->getType() <= GameMessage::MSG_END_NETWORK_MESSAGES)
		{
			if (msg->getType() != GameMessage::MSG_LOGIC_CRC && msg->getType() != GameMessage::MSG_SET_REPLAY_CAMERA)
			{
				currentlySelectedGroup = TheAI->createGroup(); // can't do this outside a game - it'll cause sync errors galore.
				CRCGEN_LOG(( "Creating AIGroup %d in GameLogic::logicMessageDispatcher()\n", (currentlySelectedGroup)?currentlySelectedGroup->getID():0 ));
				thisPlayer->getCurrentSelectionAsAIGroup(currentlySelectedGroup);

				// We can't issue commands to groups that contain units that don't belong the issuing player, so pretend like 
				// there's nothing selected. Also, if currentlySelectedGroup is empty, go ahead and delete it, so that we can skip
				// any processing on it.
				if (currentlySelectedGroup->isEmpty())
				{
					TheAI->destroyGroup(currentlySelectedGroup);
					currentlySelectedGroup = NULL;
				}

				// If there are any units that the player doesn't own, then remove them from the "currentlySelectedGroup"
				if (currentlySelectedGroup)
					if (currentlySelectedGroup->removeAnyObjectsNotOwnedByPlayer(thisPlayer))
						currentlySelectedGroup = NULL;

				if(TheStatsCollector)
					TheStatsCollector->collectMsgStats(msg);
			}
		}
	}

#ifdef DEBUG_LOGGING
	AsciiString commandName;

	commandName = msg->getCommandAsAsciiString();
	if (msg->getType() < GameMessage::MSG_BEGIN_NETWORK_MESSAGES || msg->getType() > GameMessage::MSG_END_NETWORK_MESSAGES)
	{
		commandName.concat(" (NON-LOGIC-MESSAGE!!!)");
	}
	else if (msg->getType() == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		commandName = " (CRC message!)";
	}
#if 0
	if (commandName.isNotEmpty() /*&& msg->getType() != GameMessage::MSG_FRAME_TICK*/)
	{
		DEBUG_LOG(("Frame %d: GameLogic::logicMessageDispatcher() saw a %s from player %d (%ls)\n", getFrame(), commandName.str(),
			msg->getPlayerIndex(), thisPlayer->getPlayerDisplayName().str()));
	}
#endif
#endif // DEBUG_LOGGING

	// process the message
	GameMessage::Type msgType = msg->getType();

	//
	// The shift queue (fork).  A queued order goes into the sender's order queue and comes back through
	// here later with its units named in orderedGroup; an order given without shift ends the list for
	// the units it went to.  An order already coming round from the queue is not looked at again.
	// See OrderQueue.h.
	//
	if( orderedGroup == NULL && msgType != GameMessage::MSG_QUEUE_NEXT_ORDER
			&& thisPlayer->getOrderQueue()->takeMessage( msg, currentlySelectedGroup, thisPlayer ) )
		return;		// the queue destroyed the group

	// An order can take every unit out of the group - a sale, a unit boarding, a unit dying - and
	// AIGroup::remove destroys a group the moment it is empty.  After the switch the pointer is only
	// good while the AI still has a group by this id; the replay's selection copy below read a freed
	// one and crashed playback.
	const UnsignedInt selectedGroupID = currentlySelectedGroup ? currentlySelectedGroup->getID() : 0;

	switch( msgType )
	{
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_QUEUE_NEXT_ORDER:
		{
			thisPlayer->getOrderQueue()->setNextOrderMode( msg->getArgument( 0 )->integer );
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_NEW_GAME:
		{
			if( !IsReadyToStartNewGame( isInGame(), isClearingGameData(), isLoadingMap() ) )
			{
				DEBUG_CRASH( ("MSG_NEW_GAME while the last one is still going (inGame=%d, clearingData=%d, loadingMap=%d)",
					isInGame(), isClearingGameData(), isLoadingMap()) );
				break;
			}

			//DEBUG_ASSERTCRASH(msg->getArgumentCount() == 1 || msg->getArgumentCount() == 2, ("%d arguments to MSG_NEW_GAME", msg->getArgumentCount()));
			Int gameMode = msg->getArgument( 0 )->integer;
			Int rankPoints = 0;
			GameDifficulty diff = DIFFICULTY_NORMAL;
			if ( msg->getArgumentCount() >= 2 )
				diff = (GameDifficulty)msg->getArgument( 1 )->integer;
			if ( msg->getArgumentCount() >= 3 )
				rankPoints = msg->getArgument( 2 )->integer;
			
			if ( msg->getArgumentCount() >= 4 )
			{
				Int maxFPS = msg->getArgument( 3 )->integer;
				if (maxFPS < 1 || maxFPS > 1000)
					maxFPS = TheGlobalData->m_framesPerSecondLimit;
				DEBUG_LOG(("Setting max FPS limit to %d FPS\n", maxFPS));
				TheGameEngine->setFramesPerSecondLimit(maxFPS);
				TheWritableGlobalData->m_useFpsLimit = true;
			}

			/* Seed again from the number the start was seeded with.  Every start seeds when it appends
				 this message, and the logic acts on it a pass later, after the shell map's scripts have
				 run once more on the freshly seeded stream - a draw a copy with -noshellmap, or a replay
				 started from the command line, never made.  The recorder writes this same number. */
			InitRandom( GetGameLogicRandomSeed() );

			// prepare for new game
			prepareNewGame( gameMode, diff, rankPoints );

			// start new game
			startNewGame( FALSE );

			break;

		}  // end new game

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_CLEAR_GAME_DATA:
		{

#if defined(_DEBUG) || defined(_INTERNAL)
			if (TheDisplay && TheGlobalData->m_dumpAssetUsage)
				TheDisplay->dumpAssetUsage(TheGlobalData->m_mapName.str());
#endif

			if (currentlySelectedGroup)
				TheAI->destroyGroup(currentlySelectedGroup);
			currentlySelectedGroup = NULL;
			TheGameLogic->clearGameData();
			break;

		}  // end clear game data

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_PATH_BUILD:
		{
			DEBUG_LOG(("META: begin path build\n"));
			DEBUG_ASSERTCRASH(!theBuildPlan, ("mismatched theBuildPlan"));

			if (theBuildPlan == false)
			{
				theBuildPlan = true;
				thePlanSubjectCount = 0;
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_PATH_BUILD:
		{
			DEBUG_LOG(("META: end path build\n"));
			DEBUG_ASSERTCRASH(theBuildPlan, ("mismatched theBuildPlan"));

			// tell everyone who participated in the plan to move
			for( int i=0; i<thePlanSubjectCount; i++ )
			{
				AIUpdateInterface *ai = thePlanSubject[i]->getAIUpdateInterface();
				if (ai)
					ai->executeWaypointQueue();
			}

			theBuildPlan = false;
			thePlanSubjectCount = 0;

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_SET_RALLY_POINT:
		{
			Object *obj = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
			Coord3D dest = msg->getArgument( 1 )->location;
			// sanity, the player must control the object. The object id comes straight off the
			// message, and every sibling case here (MSG_CANCEL_UNIT_CREATE, MSG_DOZER_CANCEL_CONSTRUCT)
			// checks it - without it a doctored client could set rally points on enemy factories.
			if (obj && obj->getControllingPlayer() == thisPlayer)
			{
				doSetRallyPoint( obj, dest );
			}

			break;

		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_WEAPON:
		{
			WeaponSlotType weaponSlot = (WeaponSlotType)msg->getArgument( 0 )->integer;
			Int maxShotsToFire = msg->getArgument( 1 )->integer;
			
			// lock it just till the weapon is empty or the attack is "done"
			if( currentlySelectedGroup && currentlySelectedGroup->setWeaponLockForGroup( weaponSlot, LOCKED_TEMPORARILY ))
			{
				currentlySelectedGroup->groupAttackPosition( NULL, maxShotsToFire, CMD_FROM_PLAYER );
			}

			break;
		}  

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_COMBATDROP_AT_OBJECT:
		{
			Object *targetObject = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// issue command for either single object or for selected group
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupCombatDrop( targetObject, 
																								 *targetObject->getPosition(), 
																								 CMD_FROM_PLAYER );

/*
			if( sourceObject && targetObject )
			{
				AIUpdateInterface* sourceAI = sourceObject->getAIUpdateInterface();
				if (sourceAI)
				{
					sourceAI->aiCombatDrop( targetObject, *targetObject->getPosition(), CMD_FROM_PLAYER );
				}
			}
*/

			break;

		}  // end GameMessage::MSG_COMBATDROP_AT_OBJECT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_COMBATDROP_AT_LOCATION:
		{
			Coord3D targetLoc = msg->getArgument( 0 )->location;

			if( currentlySelectedGroup )
				currentlySelectedGroup->groupCombatDrop( NULL, targetLoc, CMD_FROM_PLAYER );

/*
			if( sourceObject )
			{
				AIUpdateInterface* sourceAI = sourceObject->getAIUpdateInterface();
				if (sourceAI)
				{
					sourceAI->aiCombatDrop( NULL, targetLoc, CMD_FROM_PLAYER );
				}
			}
*/

			break;

		}  // end GameMessage::MSG_COMBATDROP_AT_LOCATION

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
		{
			// Lock the weapon choice to the right weapon, then give an attack command

			WeaponSlotType weaponSlot = (WeaponSlotType)msg->getArgument( 0 )->integer;
			Object *targetObject = TheGameLogic->findObjectByID( msg->getArgument( 1 )->objectID );
			Int maxShotsToFire = msg->getArgument( 2 )->integer;

			// sanity
			if( targetObject == NULL )
				break;
			

			// issue command for either single object or for selected group
			if( currentlySelectedGroup )
			{
					// lock it just till the weapon is empty or the attack is "done"
				if (currentlySelectedGroup->setWeaponLockForGroup( weaponSlot, LOCKED_TEMPORARILY ))
					currentlySelectedGroup->groupAttackObject( targetObject, maxShotsToFire, CMD_FROM_PLAYER );
			}  // end if, command for group
			break;

		}  // end do weapon at object

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_SWITCH_WEAPONS:
		{
			// use the selected group
			WeaponSlotType weaponSlot = (WeaponSlotType)msg->getArgument( 0 )->integer;
			// lock until un-switched, or switched to something else.
 			if( currentlySelectedGroup )
				currentlySelectedGroup->setWeaponLockForGroup( weaponSlot, LOCKED_PERMANENTLY );
			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_SET_MINE_CLEARING_DETAIL:
		{
			if( currentlySelectedGroup )
			{
				currentlySelectedGroup->setMineClearingDetail(true);
			}
			break;
		}

		case GameMessage::MSG_ENABLE_RETALIATION_MODE:
		{
			// Turns retaliation mode on or off for the player who sent the message.  The message used
			// to carry a player index as well, and the case obeyed it - so a doctored one could set
			// anybody's retaliation mode.  Player::update only ever posts this for itself, so the index
			// was always the sender's own and the argument is gone.
			const Bool enableRetaliation = msg->getArgument( 0 )->boolean;
			if( thisPlayer )
			{
				thisPlayer->setLogicalRetaliationModeEnabled( enableRetaliation );
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
		{
			WeaponSlotType weaponSlot = (WeaponSlotType)msg->getArgument( 0 )->integer;
			Coord3D targetLoc = msg->getArgument( 1 )->location;
			Int maxShotsToFire = msg->getArgument( 2 )->integer;

			// issue command for either single object or for selected group
			if( currentlySelectedGroup )
			{
					// lock it just till the weapon is empty or the attack is "done"
				if (currentlySelectedGroup->setWeaponLockForGroup( weaponSlot, LOCKED_TEMPORARILY ))
 					currentlySelectedGroup->groupAttackPosition( &targetLoc, maxShotsToFire, CMD_FROM_PLAYER );


			}  // end if, command for group

			break;

		}  //end do weapon at location

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SPECIAL_POWER:
		{

			// first argument is the special power ID
			UnsignedInt specialPowerID = msg->getArgument( 0 )->integer;

			// Command button options -- special power may care about variance options
			UnsignedInt options = msg->getArgument( 1 )->integer;

			// check for possible specific source, ignoring selection.
			ObjectID sourceID = msg->getArgument(2)->objectID;
			Object* source = TheGameLogic->findObjectByID(sourceID);
			if (source != NULL)
			{
				if( !isPlayerCommandingOwnObject( thisPlayer, source->getControllingPlayer() ) )
				{
					DEBUG_CRASH( ("MSG_DO_SPECIAL_POWER: a command from player %d named object %d, which it does not control",
						msg->getPlayerIndex(), (Int)sourceID) );
					break;
				}

				AIGroup* theGroup = TheAI->createGroup();
				theGroup->add(source);
				theGroup->groupDoSpecialPower( specialPowerID, options );
				TheAI->destroyGroup(theGroup);
			}
			else
			{
				//Use the selected group!
				if( currentlySelectedGroup )
				{
					currentlySelectedGroup->groupDoSpecialPower( specialPowerID, options );
				}
			}
			break;

		}  // end do special 

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
		{
			// first argument is the special power ID
			UnsignedInt specialPowerID = msg->getArgument( 0 )->integer;

			// Location argument 2 is destination
			Coord3D targetCoord = msg->getArgument(1)->location;

			// Angle argument 3 is the orientation of the special power (if applicable)
			Real angle = msg->getArgument(2)->real;

			// Object in way -- if applicable (some specials care, others don't)
			ObjectID objectID = msg->getArgument( 3 )->objectID;
			Object *objectInWay = TheGameLogic->findObjectByID( objectID );

			// Command button options -- special power may care about variance options
			UnsignedInt options = msg->getArgument( 4 )->integer;

			// check for possible specific source, ignoring selection.
			ObjectID sourceID = msg->getArgument(5)->objectID;
			Object* source = TheGameLogic->findObjectByID(sourceID);
			if (source != NULL)
			{
				if( !isPlayerCommandingOwnObject( thisPlayer, source->getControllingPlayer() ) )
				{
					DEBUG_CRASH( ("MSG_DO_SPECIAL_POWER_AT_LOCATION: a command from player %d named object %d, which it does not control",
						msg->getPlayerIndex(), (Int)sourceID) );
					break;
				}

				AIGroup* theGroup = TheAI->createGroup();
				theGroup->add(source);
				theGroup->groupDoSpecialPowerAtLocation( specialPowerID, &targetCoord, angle, objectInWay, options );
				TheAI->destroyGroup(theGroup);
			}
			else
			{
				//Use the selected group!
				if( currentlySelectedGroup )
				{
					currentlySelectedGroup->groupDoSpecialPowerAtLocation( specialPowerID, &targetCoord, angle, objectInWay, options );
				}
			}
			break;

		}  // end do special at location

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
		{
			// first argument is the special power ID
			UnsignedInt specialPowerID = msg->getArgument( 0 )->integer;

			// argument 2 is target object
			ObjectID targetID = msg->getArgument(1)->objectID;
			Object *target = TheGameLogic->findObjectByID( targetID );
			if( !target )
			{
				break;
			}

			// Command button options -- special power may care about variance options
			UnsignedInt options = msg->getArgument( 2 )->integer;
			
			// check for possible specific source, ignoring selection.
			ObjectID sourceID = msg->getArgument(3)->objectID;
			Object* source = TheGameLogic->findObjectByID(sourceID);
			if (source != NULL)
			{
				if( !isPlayerCommandingOwnObject( thisPlayer, source->getControllingPlayer() ) )
				{
					DEBUG_CRASH( ("MSG_DO_SPECIAL_POWER_AT_OBJECT: a command from player %d named object %d, which it does not control",
						msg->getPlayerIndex(), (Int)sourceID) );
					break;
				}

				AIGroup* theGroup = TheAI->createGroup();
				theGroup->add(source);
				theGroup->groupDoSpecialPowerAtObject( specialPowerID, target, options );
				TheAI->destroyGroup(theGroup);
			}
			else
			{
				if( currentlySelectedGroup )
				{
					currentlySelectedGroup->groupDoSpecialPowerAtObject( specialPowerID, target, options );
				}
			}
			break;

		}  // end do special at object
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_ATTACKMOVETO:
		{
			Coord3D dest = msg->getArgument( 0 )->location;

			// ctrl held on the click means "arrive together" - see AIGroup::groupAttackMoveToPosition
			Bool matchSpeeds = ( msg->getArgumentCount() > 1 ) ? msg->getArgument( 1 )->boolean : FALSE;

			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupAttackMoveToPosition( &dest, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER, matchSpeeds );
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_FORCEMOVETO:
		{
			Coord3D dest = msg->getArgument( 0 )->location;

			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupMoveToPosition( &dest, false, CMD_FROM_PLAYER );
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		// MSG_DO_SALVAGE is intentionally set up to mimic the moveto.
		case GameMessage::MSG_DO_SALVAGE:
		case GameMessage::MSG_DO_MOVETO:
		{
			Coord3D dest = msg->getArgument( 0 )->location;

			if( currentlySelectedGroup )
			{
				//DEBUG_LOG(("GameLogicDispatch - got a MSG_DO_MOVETO command\n"));
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupMoveToPosition( &dest, false, CMD_FROM_PLAYER );
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_ADD_WAYPOINT:
		{
			Coord3D dest = msg->getArgument( 0 )->location;

			if( currentlySelectedGroup )
			{
				//DEBUG_LOG(("GameLogicDispatch - got a MSG_DO_MOVETO command\n"));
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupMoveToPosition( &dest, true, CMD_FROM_PLAYER );
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_GUARD_POSITION:
		{
			Coord3D loc = msg->getArgument( 0 )->location;
			GuardMode gm = (GuardMode)msg->getArgument( 1 )->integer;
			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->groupGuardPosition(&loc, gm, CMD_FROM_PLAYER);
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		//
		// Hold position (fork). MSG_DO_GUARD_POSITION guards ONE point with the whole group, which
		// makes everyone walk to it; hold position means each unit guards where it already stands,
		// so the position is resolved here, per member.
		//
		case GameMessage::MSG_DO_HOLD_POSITION:
		{
			GuardMode gm = (GuardMode)msg->getArgument( 0 )->integer;
			if (currentlySelectedGroup)
			{
				const VecObjectID& ids = currentlySelectedGroup->getAllIDs();
				for (VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it)
				{
					Object *obj = TheGameLogic->findObjectByID( *it );
					if (!obj || obj->getControllingPlayer() != thisPlayer)
						continue;

					AIUpdateInterface *ai = obj->getAIUpdateInterface();
					if (ai)
						ai->aiGuardPosition( obj->getPosition(), gm, CMD_FROM_PLAYER );
				}
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		//
		// Formation move (fork).  The player drags a line and the selection spreads itself along
		// it, one unit per station, which is the one thing the removed group movement work was
		// trying to do for them and could not: where the units end up is now drawn rather than
		// guessed at.  The stations are handed out in the order the units already stand along the
		// line, so nobody crosses anybody on the way in, and the whole thing is resolved here so
		// the message is the drawn curve instead of a list of orders.
		//
		case GameMessage::MSG_DO_FORMATION_MOVETO:
		case GameMessage::MSG_DO_FORMATION_ATTACKMOVETO:
		case GameMessage::MSG_DO_FORMATION_FORCEATTACK:
		case GameMessage::MSG_DO_FORMATION_GUARD:
		{
			if (currentlySelectedGroup == NULL)
				break;

			const Bool attackAlong = (msg->getType() == GameMessage::MSG_DO_FORMATION_ATTACKMOVETO);
			const Bool fireAlong = (msg->getType() == GameMessage::MSG_DO_FORMATION_FORCEATTACK);
			const Bool guardAlong = (msg->getType() == GameMessage::MSG_DO_FORMATION_GUARD);

			// the curve the cursor traced, however many corners the player's hand put in it
			std::vector<Coord3D> path;
			const Int pointCount = msg->getArgumentCount();
			for (Int p = 0; p < pointCount; p++)
				path.push_back( msg->getArgument( p )->location );

			if (path.size() < 2)
				break;

			std::vector<Object *> movers;
			const VecObjectID& ids = currentlySelectedGroup->getAllIDs();
			for (VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it)
			{
				Object *obj = TheGameLogic->findObjectByID( *it );
				if (!obj || obj->getControllingPlayer() != thisPlayer)
					continue;
				if (obj->isKindOf( KINDOF_IMMOBILE ) || obj->getAIUpdateInterface() == NULL)
					continue;
				movers.push_back( obj );
			}

			if (movers.empty())
				break;

			// arc length up to each point, so a station is a distance along the whole curve and not a
			// fraction of one segment - a hand-drawn line has segments of every size
			std::vector<Real> arc;
			buildPathArcLengths( path, arc );

			const Real span = arc.back();
			if (span < 1.0f)
				break;

			orderAlongPath( movers, path, arc );

			currentlySelectedGroup->releaseWeaponLockForGroup( LOCKED_TEMPORARILY );

			const Int count = movers.size();
			for (Int i = 0; i < count; i++)
			{
				// one unit stands where the curve ends, everyone else divides it evenly
				const Real t = (count == 1) ? 1.0f : ((Real)i / (Real)(count - 1));

				Coord3D station;
				pointAlongPath( path, arc, span * t, &station );
				station.z = TheTerrainLogic->getGroundHeight( station.x, station.y );

				if (guardAlong)
					movers[ i ]->getAIUpdateInterface()->aiGuardPosition( &station, GUARDMODE_GUARD_WITHOUT_PURSUIT, CMD_FROM_PLAYER );
				else if (fireAlong)
					movers[ i ]->getAIUpdateInterface()->aiAttackPosition( &station, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
				else if (attackAlong)
					movers[ i ]->getAIUpdateInterface()->aiAttackMoveToPosition( &station, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
				else
					movers[ i ]->getAIUpdateInterface()->aiMoveToPosition( &station, CMD_FROM_PLAYER );
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_GUARD_OBJECT:
		{
			Object* obj = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
			if (!obj)
				break;

			GuardMode gm = (GuardMode)msg->getArgument( 1 )->integer;
			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->groupGuardObject(obj, gm, CMD_FROM_PLAYER);
			}

			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_STOP:
		{
			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->groupIdle(CMD_FROM_PLAYER);
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SCATTER:
		{
			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->groupScatter(CMD_FROM_PLAYER);
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_FORMATION:
		{
			if (currentlySelectedGroup)
			{
				currentlySelectedGroup->groupCreateFormation(CMD_FROM_PLAYER);
			}

			break;
		}
		
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_CLEAR_INGAME_POPUP_MESSAGE:
		{
		
			if( TheInGameUI )
			{
				TheInGameUI->clearPopupMessageData();
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_CHEER:
		{
			//All selected units play cheer animation.
			if( currentlySelectedGroup )
			{
				currentlySelectedGroup->groupCheer( CMD_FROM_PLAYER );
			}
			break;
		}
		
#if defined(_DEBUG) || defined(_INTERNAL) || defined (_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DEBUG_KILL_SELECTION:
		{
			//All selected units die
			if( currentlySelectedGroup )
			{
				const VecObjectID& selectedObjects = currentlySelectedGroup->getAllIDs();
				for (VecObjectID::const_iterator it = selectedObjects.begin(); it != selectedObjects.end(); ++it)
				{
					Object *obj = findObjectByID(*it);
					if (obj)
					{
						obj->kill();
					}
				}
			}
			break;
		}
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DEBUG_HURT_OBJECT:
		{
			Object* objToHurt = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
			if (objToHurt)
			{
				DamageInfo damageInfo;
				damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
				damageInfo.in.m_deathType = DEATH_NORMAL;
				damageInfo.in.m_sourceID = INVALID_ID;
				damageInfo.in.m_amount = objToHurt->getBodyModule()->getMaxHealth() / 10.0f;
				objToHurt->attemptDamage( &damageInfo );
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DEBUG_KILL_OBJECT:
		{
			Object* objToHurt = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
			if (objToHurt)
			{
				objToHurt->kill();
			}
			break;
		}
#endif




#ifdef ALLOW_SURRENDER
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SURRENDER:
		{
			//All selected units surrender
			if( currentlySelectedGroup )
			{
				Object* objWeSurrenderedTo = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
				Bool surrender = msg->getArgument( 1 )->boolean;
				currentlySelectedGroup->groupSurrender( objWeSurrenderedTo, surrender, CMD_FROM_PLAYER );
			}
			break;
		}
#endif

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_ENTER:
		{
			Object *enter = TheGameLogic->findObjectByID( msg->getArgument( 1 )->objectID );

			// sanity
			if( enter == NULL )
				break;

			if( currentlySelectedGroup )
			{
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupEnter( enter, CMD_FROM_PLAYER );
			}

			break;

		}  // end GameMessage::MSG_ENTER

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_EXIT:
		{
			Object *objectWantingToExit = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );
			Object *objectContainingExiter = getSingleObjectFromSelection(currentlySelectedGroup);

			// sanity
			if( objectWantingToExit == NULL )
				break;

			if( objectContainingExiter == NULL )
				break;

			// sanity, the player must actually control this object
			if( objectWantingToExit->getControllingPlayer() != thisPlayer )
				break;
			
			objectWantingToExit->releaseWeaponLock(LOCKED_TEMPORARILY);	// release any temporary locks.

			// exit whatever objectWantingToExit is INSIDE of
			AIUpdateInterface *ai = objectWantingToExit->getAIUpdateInterface();
			if( ai )
				ai->aiExit( objectContainingExiter, CMD_FROM_PLAYER );
			// Just like Enter, Exit needs to know the thing to exit.  This can no longer be assumed because of the Tunnel system.
			// If you do not specify the thing to Exit, it will Exit the thing it thinks it is in.  For a tunnel network,
			// that will be the specific Tunnel it entered.  (Scripts can talk directly to the guy to say Get Out Regardless)

			break;

		}  // end GameMessage::MSG_EXIT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_EVACUATE:
		{
			// issue command for either single object or for selected group
//			AIGroup *group = TheAI->findGroup( *selectedGroupID );
			if( currentlySelectedGroup )
			{
				//Coord3D pos;
				//Bool hasArgs = FALSE;
				//hasArgs = (msg->getArgumentCount() > 0);

				//if (hasArgs)
				//	pos = msg->getArgument(0)->location;

				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.

				// evacuate message is for the selected group
				//if (hasArgs)
				//	currentlySelectedGroup->groupMoveToAndEvacuate( &pos, CMD_FROM_PLAYER );
				//else
				currentlySelectedGroup->groupEvacuate( CMD_FROM_PLAYER );

// no, this is bad, don't do here, do when POSTING message
//			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_EVACUATE );

			}  // end if, command for group

			break;

		}  // end GameMessage::MSG_EVACUATE

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_EXECUTE_RAILED_TRANSPORT:
		{
		
			// issue command to currently selected objects
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupExecuteRailedTransport( CMD_FROM_PLAYER );

			break;

		}  // end GameMessage::MSG_EXECUTE_RAILED_TRANSPORT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_INTERNET_HACK:
		{
//			ObjectID sourceID = msg->getArgument( 0 )->objectID;
			if( currentlySelectedGroup )
			{
				currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
				currentlySelectedGroup->groupHackInternet( CMD_FROM_PLAYER );
			}
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_GET_REPAIRED:
		{
			Object *repairDepot = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// sanity
			if( repairDepot == NULL )
				break;

			// tell the currently selected group to go get repaired
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupGetRepaired( repairDepot, CMD_FROM_PLAYER );

			break;

		}  // end get repaired

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_DOCK:
		{
			Object *dockBuilding = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// sanity
			if( dockBuilding == NULL )
				break;

			// tell the currently selected group to go get repaired
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupDock( dockBuilding, CMD_FROM_PLAYER );

			break;

		}  // end get repaired

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_GET_HEALED:
		{
			Object *healDest = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// sanity
			if( healDest == NULL )
				break;

			// tell the currently selected group to enter the building for healing
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupGetHealed( healDest, CMD_FROM_PLAYER );

			break;

		}  // end get repaired

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_REPAIR:
		{
			Object *repairTarget = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// sanity
			if( repairTarget == NULL )
				break;

			//
			// tell the currently selected group of objects to go repair the target object, note
			// that only one of them will actually go ahead and do the repair
			//
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupRepair( repairTarget, CMD_FROM_PLAYER );

			break;

		}  // end get repaired

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_RESUME_CONSTRUCTION:
		{
			Object *constructTarget = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// sanity
			if( constructTarget == NULL )
				break;

			//
			// tell the currently selected group of objects to resume construction on
			// the target object, note that only one of them will go off and resume construction
			// on the target
			//
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupResumeConstruction( constructTarget, CMD_FROM_PLAYER );

// no, this is bad, don't do here, do when POSTING message
//		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msg->getType() );

			break;

		}  // end resume construction
		
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION:
		{
			const Coord3D *loc = &msg->getArgument( 0 )->location;
			SpecialPowerType spType = (SpecialPowerType)msg->getArgument( 1 )->integer;

			ObjectID sourceID = msg->getArgument(2)->objectID;
			Object* source = TheGameLogic->findObjectByID(sourceID);
			if (source != NULL)
			{
				if( !isPlayerCommandingOwnObject( thisPlayer, source->getControllingPlayer() ) )
				{
					DEBUG_CRASH( ("MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION: a command from player %d named object %d, which it does not control",
						msg->getPlayerIndex(), (Int)sourceID) );
					break;
				}

				AIGroup* theGroup = TheAI->createGroup();
				theGroup->add(source);
				theGroup->groupOverrideSpecialPowerDestination( spType, loc, CMD_FROM_PLAYER );
				TheAI->destroyGroup(theGroup);
			}
			else
			{
				if( currentlySelectedGroup )
				{
					currentlySelectedGroup->groupOverrideSpecialPowerDestination( spType, loc, CMD_FROM_PLAYER );
				}
			}

			// This case had no break and fell into MSG_DO_ATTACK_OBJECT, which reads argument 0 as an
			// object id - here that argument is a Coord3D, so the union handed findObjectByID the raw
			// float bits of the x coordinate.
			break;

		}  // end override special power destination

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_ATTACK_OBJECT:
		{
			Object *enemy = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// Check enemy, as it is possible that he died this frame.
			if (enemy)
			{
				if (currentlySelectedGroup)
				{

					// how many units the order actually reached, against how many the player had
					// selected on their own screen: the two disagreeing is what "only one of them
					// went" looks like from the outside
					DEBUG_LOG(("attack order: %d units on %s\n", currentlySelectedGroup->getCount(),
										 enemy->getTemplate()->getName().str()));

					currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
					currentlySelectedGroup->groupAttackObject( enemy, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );

				}

			}

			break;

		}  // end GameMessage::MSG_DO_ATTACK_GROUND_OBJECT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
		{
			Object *enemy = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			// Check enemy, as it is possible that he died this frame.
			if (enemy) 
			{
				if (currentlySelectedGroup)
				{
					currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);	// release any temporary locks.
					currentlySelectedGroup->groupForceAttackObject( enemy, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
				}

			}

			break;

		}  // end GameMessage::MSG_DO_ATTACK_GROUND_OBJECT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
		{
			const Coord3D *pos = &msg->getArgument( 0 )->location;

			if (currentlySelectedGroup)
			{

				/////////////////////////////////////////////////////////////////////
				//Lorenzen sez: unclear, yet how to solve this for all cases
				//Kris: This code was added to allow the toxin tractor to force attack
				//      while contaminating. When this change was made, it was causing
				//      rangers and scud launchers to reset to primary weapon mode whenever
				//      force attacking while not idle. I fixed this by enforcing the 
				//      temporary and permanent modes that are already set when attempting
				//      the new lock. In this case, the temp lock attempt will fail whenever
				//      a permanent lock is in effect, thus fixing the ranger and scud and
				//      allowing the tox tractor to work as well.
				Bool forceAttackRequiresPrimaryWeapon = !currentlySelectedGroup->isIdle();
				if ( forceAttackRequiresPrimaryWeapon )
				{
					currentlySelectedGroup->setWeaponLockForGroup( PRIMARY_WEAPON, LOCKED_TEMPORARILY );
					currentlySelectedGroup->groupAttackPosition( pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
					currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);
				}
				else
				///////////////////////////////////////////////////////////////////
				{
					currentlySelectedGroup->releaseWeaponLockForGroup(LOCKED_TEMPORARILY);
					currentlySelectedGroup->groupAttackPosition( pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
				}

			
			}

			break;

		}  // end GameMessage::MSG_DO_ATTACK_GROUND_OBJECT

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_QUEUE_UPGRADE:
		{
			const UpgradeTemplate *upgradeT = TheUpgradeCenter->findUpgradeByKey( (NameKeyType)(msg->getArgument( 1 )->integer) );
			if (!upgradeT)	// sanity
				break;

			//
			// Argument 0 is the building the command bar picked to research this. It matters for a
			// multi-selection: a player upgrade is researched exactly once, so handing the whole
			// group to `queueUpgrade` below always lands it on the first member of the selection -
			// the first one to pass the affordability check withdraws the money and every other
			// member then fails it. That is why clicking four upgrades on four selected barracks
			// stacked all four on one of them. The bar already chose the least loaded building;
			// honour that choice whenever the player controls the named object, and fall back to
			// the old group-wide path otherwise. An object upgrade is bought per building, and the
			// bar now sends one message per building it wants it on, so the group-wide path must
			// not fire for those either - it would queue N messages on N buildings each.
			// The named object is authorized by the ownership check below and the revalidation
			// after it, not by being in the selection: the bar also sends this from the stand-in
			// builder's bar, where nothing is selected at all - which is where the GLA worker's
			// fake-buildings toggle lives, so requiring a selection left the worker stuck on
			// whichever page it was last put on.
			//
			Object *producer = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			if( producer && producer->getControllingPlayer() == thisPlayer )
			{
				// same revalidation AIGroup::queueUpgrade does per member, to stop cheaters
				if( !TheUpgradeCenter->canAffordUpgrade( producer->getControllingPlayer(), upgradeT, FALSE ) )
					break;
				if( upgradeT->getUpgradeType() == UPGRADE_TYPE_OBJECT &&
						( producer->hasUpgrade( upgradeT ) || !producer->affectedByUpgrade( upgradeT ) ) )
					break;
				if( !producer->canProduceUpgrade( upgradeT ) )
					break;
				ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
				if( pu == NULL || pu->canQueueUpgrade( upgradeT ) == CANMAKE_QUEUE_FULL )
					break;

				pu->queueUpgrade( upgradeT );
				break;
			}

			if (currentlySelectedGroup)
				currentlySelectedGroup->queueUpgrade( upgradeT );

			break;

		}  // end queue upgrade

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_CANCEL_UPGRADE:
		{
			// as above: an explicit producer means the click came from somewhere other than that
			// building's own command bar, and the ownership check below authorizes it
			Object *producer = msg->getArgumentCount() > 1
												 ? TheGameLogic->findObjectByID( msg->getArgument( 1 )->objectID )
												 : getSingleObjectFromSelection(currentlySelectedGroup);
			const UpgradeTemplate *upgradeT = TheUpgradeCenter->findUpgradeByKey( (NameKeyType)(msg->getArgument( 0 )->integer) );

			// sanity
			if( producer == NULL || upgradeT == NULL )
				break;

			// the player must actually control the producer object
			if( producer->getControllingPlayer() != thisPlayer )
				break;

			// producer must have a production update
				ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
				if( pu == NULL )
				break;

			// cancel the upgrade
			pu->cancelUpgrade( upgradeT );

			break;

		}  // end cancel upgrade

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_QUEUE_UNIT_CREATE:
		{
			Object *producer = NULL;
			const ThingTemplate *whatToCreate;

			// get data from the message.  Argument 1 was a production ID the build button minted
			// from the factory's counter, which moved the counter on the sender's machine only, so
			// the next ID a script or a flight deck minted there differed from everyone else's.  The
			// ID is minted below, in logic, on every machine alike.
			whatToCreate = TheThingFactory->findByTemplateID( msg->getArgument( 0 )->integer );

			// an explicit producer (multi-select build) must be one of the selected objects
			if( msg->getArgumentCount() > 2 && currentlySelectedGroup )
			{
				ObjectID producerID = msg->getArgument( 2 )->objectID;
				const VecObjectID& ids = currentlySelectedGroup->getAllIDs();
				if( std::find( ids.begin(), ids.end(), producerID ) != ids.end() )
					producer = TheGameLogic->findObjectByID( producerID );
			}
			else
				producer = getSingleObjectFromSelection(currentlySelectedGroup);

			// sanity
			if ( producer == NULL || whatToCreate == NULL )
				break;

			// get the production interface for the producer
			ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
			if( pu == NULL )
			{

				DEBUG_ASSERTCRASH( 0, ("MSG_QUEUE_UNIT_CREATE: Producer '%s' doesn't have a unit production interface\n", 
															producer->getTemplate()->getName().str()) );
				break;

			}  // end if

			// queue the build
			pu->queueCreateUnit( whatToCreate, pu->requestUniqueUnitID() );

			break;

		}  // end GameMessage::MSG_QUEUE_UNIT_CREATE

		//-------------------------------------------------------------------------------------------------
		case GameMessage::MSG_CANCEL_UNIT_CREATE:
		{
			Object *producer = NULL;
			ProductionID productionID = (ProductionID)msg->getArgument( 0 )->integer;

			//
			// An explicit producer travels with the message when the click did not come from the
			// selected building's own bar - a multi-selection's queue, or the global production
			// strip, which cancels on a building the player never selected. The ownership check
			// below is what authorizes it; membership of the current selection is not required.
			//
			if( msg->getArgumentCount() > 1 )
				producer = TheGameLogic->findObjectByID( msg->getArgument( 1 )->objectID );
			else
				producer = getSingleObjectFromSelection(currentlySelectedGroup);
			
			// sanity
			if( producer == NULL )
				break;
				
			// sanity, the player must control the producer			
			if( producer->getControllingPlayer() != thisPlayer )
				break;

			// get the unit production interface
			ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
			if( pu == NULL )
				break;			// break, not return: the tail of this function destroys currentlySelectedGroup

			// cancel the production
			pu->cancelUnitCreate( productionID );

			break;

		}  // end GameMessage::MSG_CANCEL_UNIT_CREATE

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DOZER_CONSTRUCT:
		case GameMessage::MSG_DOZER_CONSTRUCT_LINE:
		{
			const ThingTemplate *place;
			Coord3D loc;
			Real angle;

			// get player, what to place, and location
			Object *constructorObject = getSingleObjectFromSelection(currentlySelectedGroup);
			place = TheThingFactory->findByTemplateID( msg->getArgument( 0 )->integer );
			loc = msg->getArgument( 1 )->location;
			angle = msg->getArgument( 2 )->real;

			//
			// the job goes to the idle builder nearest the site - among the selected builders,
			// or, with nothing selected (stand-in builder command bar), among all the player's
			// builders.  A builder already on a job is only taken when no idle one exists.
			//
			{
				BuilderPick pick;
				pick.loc = loc;
				pick.idle = pick.any = NULL;
				pick.idleDistSqr = pick.anyDistSqr = 1e30f;
				if( currentlySelectedGroup )
				{
					const VecObjectID& ids = currentlySelectedGroup->getAllIDs();
					for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
						considerBuilder( TheGameLogic->findObjectByID( *it ), &pick );
				}
				if( pick.any == NULL && thisPlayer )
					thisPlayer->iterateObjects( considerBuilderProc, &pick );

				Object *chosen = pick.idle ? pick.idle : pick.any;
				if( chosen )
					constructorObject = chosen;
			}

			if( place == NULL || constructorObject == NULL )
				break;  //These are not crashes, as the object may have died before this message came in

			// sanity, the player must control the builder
			if( constructorObject->getControllingPlayer() != thisPlayer )
				break;

			//
			// Re-check prerequisites and money on the logic side. This used to go straight to
			// buildObjectNow, whose Money::withdraw clamps to the balance - so a client that
			// skipped the UI's own check could put up structures it had not paid for.
			//
			if( TheBuildAssistant->canMakeUnit( constructorObject, place ) != CANMAKE_OK )
				break;

			//
			// And re-check the ground, which nobody was checking here at all.  The client asks
			// before it sends, and the answer it gets is several frames old by the time the order
			// arrives - on a network game, as old as the link is slow.  So two structures ordered
			// onto the same spot in that window both found it empty and both went up, one inside
			// the other; holding shift on a bad connection did it every time.  Asked here, the
			// first one is already standing when the second order lands.
			//
			// Only the ground: no pathfind and no shroud, because those two answer differently a
			// third of a second apart and refusing an order for that would be a new bug.  The line
			// build checks every tile after the first for itself (buildTiledLocations).
			//
			if( TheBuildAssistant->isLocationLegalToBuild( &loc, place, angle,
																										BuildAssistant::TERRAIN_RESTRICTIONS |
																										BuildAssistant::NO_OBJECT_OVERLAP,
																										constructorObject, NULL ) != LBC_OK )
				break;

			if( msg->getType() == GameMessage::MSG_DOZER_CONSTRUCT )
			{

				TheBuildAssistant->buildObjectNow( constructorObject, place, &loc, angle, 
																					 constructorObject->getControllingPlayer() );

			}  // end if
			else
			{
				Coord3D locEnd;

				// get the end of the line location in the world
				locEnd = msg->getArgument( 3 )->location;

				// place the line of structures, the end location being present will make it happen
				TheBuildAssistant->buildObjectLineNow( constructorObject, place, &loc, &locEnd, angle, 
																							 constructorObject->getControllingPlayer() );

			}  // end else

			// place the sound for putting a building down

			static AudioEventRTS placeBuilding(AsciiString("PlaceBuilding"));
			placeBuilding.setObjectID(constructorObject->getID());
			TheAudio->addAudioEvent( &placeBuilding );


// no, this is bad, don't do here, do when POSTING message
//		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msg->getType() );

			break;

		}  // end build start

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DOZER_CANCEL_CONSTRUCT:
		{

			// get the building to cancel construction on
			Object *building = getSingleObjectFromSelection(currentlySelectedGroup);
			if( building == NULL )
				break;

			// the player sending this message must actually control this building
			if( building->getControllingPlayer() != thisPlayer )
				break;
			
			// Check to make sure it is actually under construction
			if( !building->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) )
				break;

			// OK, refund the money to the player, unless it is a rebuilding Hole.
			if( !building->testStatus(OBJECT_STATUS_RECONSTRUCTING))
			{
				Money *money = thisPlayer->getMoney();
				UnsignedInt amount = building->getTemplate()->calcCostToBuild( thisPlayer );
				money->deposit( amount );
			}

			//
			// Destroy the building ... killing the
			// building will automatically cause the dozer also cancel its own building 
			// behavior and it will go on its merry way doing other assigned tasks
			//
			building->kill();

			break;

		}  // end cancel dozer construction

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_SELL:
		{

			// use the selected group
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupSell( CMD_FROM_PLAYER );

			break;

		}  // end sell

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_TOGGLE_OVERCHARGE:
		{

			// use the selected group
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupToggleOvercharge( CMD_FROM_PLAYER );

			break;

		}  // end toggle overcharge

#ifdef ALLOW_SURRENDER
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_PICK_UP_PRISONER:
		{
			Object *prisoner = TheGameLogic->findObjectByID( msg->getArgument( 0 )->objectID );

			if( prisoner )
			{

				// use selected group
				if( currentlySelectedGroup )
					currentlySelectedGroup->groupPickUpPrisoner( prisoner, CMD_FROM_PLAYER );

			}  // end if

			break;

		}  // end pick up prisoner
#endif

#ifdef ALLOW_SURRENDER
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_RETURN_TO_PRISON:
		{

			// use selected group
			if( currentlySelectedGroup )
				currentlySelectedGroup->groupReturnToPrison( NULL, CMD_FROM_PLAYER );

			break;

		}  // end return to prison
#endif

		//---------------------------------------------------------------------------------------------
		// No sound does exactly the same logical processing as the usual message. Just double them up.
		case GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND:
		case GameMessage::MSG_CREATE_SELECTED_GROUP:
		{
			Bool createNewGroup = msg->getArgument( 0 )->boolean;
			Player *player = ThePlayerList->getNthPlayer(msg->getPlayerIndex());

			if (player == NULL) {
				DEBUG_CRASH(("GameLogicDispatch - MSG_CREATE_SELECTED_GROUP had an invalid player nubmer"));
				break;
			}

			Bool firstObject = TRUE;

			for (Int i = 1; i < msg->getArgumentCount(); ++i) {
				Object *obj = TheGameLogic->findObjectByID( msg->getArgument( i )->objectID );
				if (!obj) {
					continue;
				}

				TheGameLogic->selectObject(obj, createNewGroup && firstObject, player->getPlayerMask());
				firstObject = FALSE;
			}
			
			break;

		}  // end build start

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_REMOVE_FROM_SELECTED_GROUP:
		{
			Player *player = ThePlayerList->getNthPlayer(msg->getPlayerIndex());

			if (player == NULL) {
				DEBUG_CRASH(("GameLogicDispatch - MSG_CREATE_SELECTED_GROUP had an invalid player nubmer"));
				break;
			}

			for (Int i = 0; i < msg->getArgumentCount(); ++i) {
				ObjectID objID = msg->getArgument(i)->objectID;
				Object *objToRemove = TheGameLogic->findObjectByID(objID);
				if (!objToRemove) {
					continue;
				}
				
				TheGameLogic->deselectObject(objToRemove, player->getPlayerMask());
			}

			break;

		}  // end MSG_REMOVE_FROM_SELECTED_GROUP

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_DESTROY_SELECTED_GROUP:
		{
			Player *player = ThePlayerList->getNthPlayer(msg->getPlayerIndex());
			if (player != NULL)
			{
				player->setCurrentlySelectedAIGroup(NULL);
			}

			break;

		}  // end build start

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_SELECTED_GROUP_COMMAND:
		{

			break;

		}  // end selected group command
		
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_PLACE_BEACON:
		{
			// how many does this player have active?
			Coord3D pos = msg->getArgument( 0 )->location;
			Region3D r;
			TheTerrainLogic->getExtent(&r);
			if (!r.isInRegionNoZ(&pos))
				pos = TheTerrainLogic->findClosestEdgePoint(&pos);
			const ThingTemplate *thing = TheThingFactory->findTemplate( thisPlayer->getPlayerTemplate()->getBeaconTemplate() );
			if (thing && !TheVictoryConditions->hasSinglePlayerBeenDefeated(thisPlayer))
			{
				Int count;
				thisPlayer->countObjectsByThingTemplate( 1, &thing, false, &count );
				DEBUG_LOG(("Player already has %d beacons active\n", count));
				if (count >= TheMultiplayerSettings->getMaxBeaconsPerPlayer())
				{
					if (thisPlayer == ThePlayerList->getLocalPlayer())
					{
						// tell the user
						TheInGameUI->message( TheGameText->fetch("GUI:TooManyBeacons") );

						// play a sound
						static AudioEventRTS aSound("BeaconPlacementFailed");
						aSound.setPosition(&pos);
						aSound.setPlayerIndex(thisPlayer->getPlayerIndex());
						TheAudio->addAudioEvent(&aSound);
					}

					break;
				}
				Object *object = TheThingFactory->newObject( thing, thisPlayer->getDefaultTeam() );
				object->setPosition( &pos );
				object->setProducer(NULL);

				if (thisPlayer->getRelationship( ThePlayerList->getLocalPlayer()->getDefaultTeam() ) == ALLIES || ThePlayerList->getLocalPlayer()->isPlayerObserver())
				{
					// tell the user
					UnicodeString s;
					s.format(TheGameText->fetch("GUI:BeaconPlaced"), thisPlayer->getPlayerDisplayName().str());
					TheInGameUI->playerMessage( thisPlayer, s );

					// play a sound
					static AudioEventRTS aSound("BeaconPlaced");
					aSound.setPlayerIndex(thisPlayer->getPlayerIndex());
					aSound.setPosition(&pos);
					TheAudio->addAudioEvent(&aSound);

					// beacons are a rare event; play a nifty radar event thingie
					TheRadar->createEvent( object->getPosition(), RADAR_EVENT_INFORMATION );
					
					if (ThePlayerList->getLocalPlayer()->getRelationship(thisPlayer->getDefaultTeam()) == ALLIES)
						TheEva->setShouldPlay(EVA_BeaconDetected);

					TheControlBar->markUIDirty(); // check if we should grey out the button
				}
				else
				{

					Int updateCount = 0;
					static NameKeyType nameKeyClientUpdate = NAMEKEY("BeaconClientUpdate");
					ClientUpdateModule ** clientModules = object->getDrawable()->getClientUpdateModules();
					if (clientModules)
					{
						while (*clientModules)
						{
							if ((*clientModules)->getModuleNameKey() == nameKeyClientUpdate)
							{
								(*(BeaconClientUpdate **)clientModules)->hideBeacon();
								++updateCount;
							}

							++clientModules;
						}
					}
					DEBUG_ASSERTCRASH(updateCount == 1, ("Saw %d update modules for the beacon!", updateCount));

				}
			}
			else
			{
				// tell the user
				TheInGameUI->message( TheGameText->fetch("GUI:BeaconPlacementFailed") );

				// play a sound
				static AudioEventRTS aSound("BeaconPlacementFailed");
				aSound.setPosition(&pos);
				aSound.setPlayerIndex(thisPlayer->getPlayerIndex());
				TheAudio->addAudioEvent(&aSound);
			}
			break;
		} // end beacon placement

		// --------------------------------------------------------------------------------------------
		// A smoke signal is a picture and a line of text on the machines allowed to see it, and
		// nothing else: no object is made and no logic state moves, so a machine that draws it and a
		// machine that does not stay in step.
		case GameMessage::MSG_PLACE_SIGNAL:
		{
			Int kind = msg->getArgument( 1 )->integer;
			if( kind < 0 || kind >= SIGNAL_KIND_COUNT )
				break;

			// one a second per player, held here rather than at the key so every machine applies it
			// to every sender, including one that was built without it
			static UnsignedInt lastSignalFrame[ MAX_PLAYER_COUNT ];
			Int senderIndex = thisPlayer->getPlayerIndex();
			if( Signal_isThrottled( getFrame(), lastSignalFrame[ senderIndex ] ) )
				break;
			lastSignalFrame[ senderIndex ] = getFrame();

			Player *localPlayer = ThePlayerList->getLocalPlayer();
			Bool mutualAllies = thisPlayer->getRelationship( localPlayer->getDefaultTeam() ) == ALLIES &&
				localPlayer->getRelationship( thisPlayer->getDefaultTeam() ) == ALLIES;
			if( thisPlayer != localPlayer && !mutualAllies && !localPlayer->isPlayerObserver() )
				break;

			const SignalLook &look = SIGNAL_LOOKS[ kind ];
			Coord3D pos = msg->getArgument( 0 )->location;

			ParticleSystem *smoke = TheParticleSystemManager->createParticleSystem(
				TheParticleSystemManager->findTemplate( SIGNAL_SMOKE_TEMPLATE ) );
			if( smoke )
			{
				smoke->setPosition( &pos );
				smoke->tintAllColors( clientPlayerColor( thisPlayer ) );
				// the tint leaves the first key alone, and in this template that key is an orange flash
				smoke->m_colorKey[ 0 ].color = smoke->m_colorKey[ 1 ].color;
				smoke->setFiniteSystemLifetime( SIGNAL_HALF_FRAMES );
				smoke->setLifetimeRange( SIGNAL_HALF_FRAMES, SIGNAL_HALF_FRAMES );
				// the template's second alpha key is its fade to nothing, timed for a five second puff
				smoke->m_alphaKey[ 1 ].frame = SIGNAL_HALF_FRAMES;
				smoke->m_alphaKey[ 0 ].var.setRange( SIGNAL_SMOKE_ALPHA_MIN, SIGNAL_SMOKE_ALPHA_MAX );
				smoke->setSizeMultiplier( SIGNAL_SMOKE_SIZE_SCALE );
				smoke->setBurstCountMultiplier( SIGNAL_SMOKE_DENSITY_SCALE );
				Coord3D rise = { 1.0f, 1.0f, SIGNAL_SMOKE_RISE_SCALE };
				smoke->setVelocityMultiplier( &rise );
				TheInGameUI->addSignalMark( (SignalKind)kind, pos, clientPlayerColor( thisPlayer ), smoke->getSystemID() );
			}

			TheRadar->createEvent( &pos, look.radarEvent, SIGNAL_SECONDS );

			UnicodeString announcement;
			announcement.format( TheGameText->fetch( look.announcementLabel ), thisPlayer->getPlayerDisplayName().str() );
			TheInGameUI->playerMessage( thisPlayer, announcement );

			static AudioEventRTS signalSound( "BeaconPlaced" );
			signalSound.setPlayerIndex( thisPlayer->getPlayerIndex() );
			signalSound.setPosition( &pos );
			TheAudio->addAudioEvent( &signalSound );
			break;
		}

		// --------------------------------------------------------------------------------------------
		// The console's cheats come through here rather than acting from the console, so a replay of
		// a game that used them records them and plays back the same.  A network game never takes one.
		case GameMessage::MSG_CHEAT:
		{
			// Refused in a network game, and so in its playback too, or a cheat every machine
			// ignored during the match would be applied when the replay of it plays.  The recorded
			// mode, not isMultiplayer(): the recorder counts a skirmish as multiplayer.
			const Int originalMode = (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
															 ? TheRecorder->getGameMode() : getGameMode();
			if( originalMode == GAME_LAN || originalMode == GAME_INTERNET )
				break;

			CheatKind kind = (CheatKind)msg->getArgument( 0 )->integer;
			Int amount = msg->getArgument( 1 )->integer;
			switch( kind )
			{
				case CHEAT_MONEY:
					thisPlayer->getMoney()->deposit( amount );
					break;

				case CHEAT_GENERAL_POINTS:
					thisPlayer->addSciencePurchasePoints( amount );
					break;

				case CHEAT_RANK_UP:
					thisPlayer->setRankLevel( thisPlayer->getRankLevel() + amount );
					break;

				case CHEAT_HEROIC:
					thisPlayer->iterateObjects( makeObjectHeroic, NULL );
					break;

				case CHEAT_REVEAL_MAP:
					ThePartitionManager->revealMapForPlayerPermanently( thisPlayer->getPlayerIndex() );
					break;

				case CHEAT_INFINITE_POWER:
					thisPlayer->toggleCheat( kind );
					thisPlayer->onPowerBrownOutChange( !thisPlayer->getEnergy()->hasSufficientPower() );
					break;

				case CHEAT_NO_COOLDOWN:
				case CHEAT_GOD_MODE:
				case CHEAT_INSTANT_BUILD:
				case CHEAT_ONE_HIT_KILL:
					thisPlayer->toggleCheat( kind );
					break;

				/* The logic half of takeControlOfPlayer, a message so the recording has it: the AIPlayer
					 goes, the keyboard seat moves, and every object looks again, or the base just left goes
					 dark behind you - a building's looking mask was worked out when it was built. */
				case CHEAT_TAKE_CONTROL:
				{
					Player *target = ThePlayerList->getNthPlayer( amount );
					if( target == NULL )
						break;
					target->setPlayerType( PLAYER_HUMAN, FALSE );
					ThePlayerList->setKeyboardPlayer( target );
					for( Object *obj = getFirstObject(); obj; obj = obj->getNextObject() )
						obj->handlePartitionCellMaintenance();
					if( target == ThePlayerList->getLocalPlayer() )
						ThePartitionManager->refreshShroudForLocalPlayer();
					break;
				}
			}
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_REMOVE_BEACON:
		{
	
			AIGroup *allSelectedObjects = NULL;
			allSelectedObjects = TheAI->createGroup();
			thisPlayer->getCurrentSelectionAsAIGroup(allSelectedObjects); // need to act on all objects, so we can hide teammates' beacons.
			if( allSelectedObjects )
			{
				const VecObjectID& selectedObjects = allSelectedObjects->getAllIDs();
				for (VecObjectID::const_iterator it = selectedObjects.begin(); it != selectedObjects.end(); ++it)
				{
					Object *beacon = findObjectByID(*it);
					if (beacon)
					{
						const ThingTemplate *thing = TheThingFactory->findTemplate( beacon->getControllingPlayer()->getPlayerTemplate()->getBeaconTemplate() );
						if (thing->isEquivalentTo(beacon->getTemplate()))
						{
							if (beacon->getControllingPlayer() == thisPlayer)
							{
								TheGameLogic->destroyObject(beacon); // the owner is telling it to go away.  such is life.

								TheControlBar->markUIDirty(); // check if we should un-grey out the button
							}
							else if (thisPlayer == ThePlayerList->getLocalPlayer())
							{
								Drawable *beaconDrawable = beacon->getDrawable();
								if (beaconDrawable)
								{

									static NameKeyType nameKeyClientUpdate = NAMEKEY("BeaconClientUpdate");
									ClientUpdateModule ** clientModules = beaconDrawable->getClientUpdateModules();
									if (clientModules)
									{
										while (*clientModules)
										{
											if ((*clientModules)->getModuleNameKey() == nameKeyClientUpdate)
												(*(BeaconClientUpdate **)clientModules)->hideBeacon();

											++clientModules;
										}
									}
								}
							}
						}
					}
				}
				if (allSelectedObjects->isEmpty())
				{
					TheAI->destroyGroup(allSelectedObjects);
					allSelectedObjects = NULL;
				}
			}
			break;
		} // end beacon removal

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_SET_BEACON_TEXT:
		{
			if( currentlySelectedGroup )
			{
				const VecObjectID& selectedObjects = currentlySelectedGroup->getAllIDs();
				for (VecObjectID::const_iterator it = selectedObjects.begin(); it != selectedObjects.end(); ++it)
				{
					Object *beacon = findObjectByID(*it);
					if (beacon)
					{
						Drawable *beaconDrawable = beacon->getDrawable();
						if (beaconDrawable)
						{
							UnicodeString s;
							for( int i=0; i<msg->getArgumentCount(); i++ )
							{
								s.concat( msg->getArgument(i)->wChar );
							}

							if (s.isEmpty())
								beaconDrawable->clearCaptionText();
							else
								beaconDrawable->setCaptionText(s);
						}
					}
				}
			}
			break;
		} // end beacon text

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_SELF_DESTRUCT:
		{
			if (msg->getArgument(0)->boolean)
			{
				// transfer control to any living ally
				// i is used after the loop; VC6 for-scope let it escape.
				Int i;
				for (i =0; i<ThePlayerList->getPlayerCount(); ++i)
				{
					if (i != msg->getPlayerIndex())
					{
						Player *otherPlayer = ThePlayerList->getNthPlayer(i);
						if (thisPlayer->getRelationship(otherPlayer->getDefaultTeam()) == ALLIES &&
							otherPlayer->getRelationship(thisPlayer->getDefaultTeam()) == ALLIES)
						{
							if (TheVictoryConditions->hasSinglePlayerBeenDefeated(otherPlayer))
								continue;

							// a living ally!  hooray!
							otherPlayer->transferAssetsFromThat(thisPlayer);
							thisPlayer->killPlayer(); // just to be safe (and to kill beacons etc that don't transfer)
							break;
						}
					}
				}
				if (i == ThePlayerList->getPlayerCount())
				{
					// didn't find any allies.  die, loner!
					thisPlayer->killPlayer();
				}
			}
			else
			{
				thisPlayer->killPlayer();
			}
			// There is no reason to do any notification here, it now takes place in the victory conditions.
			// bonehead.
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_SET_REPLAY_CAMERA:
		{
			// Where this player's camera is, out of a replay or over the network from a match going on.
			// Only a watcher's camera is ever moved by it, and that is the observer camera's to do on
			// its own frame when it is following this player; the logic only passes it on.
			ViewLocation loc;
			const Coord3D &pos = msg->getArgument( 0 )->location;
			loc.init( pos.x, pos.y, pos.z, msg->getArgument( 1 )->real, msg->getArgument( 2 )->real, msg->getArgument( 3 )->real );
			TheObserverCamera.notePlayerView( thisPlayer->getPlayerIndex(), loc );

			// a replay shows the recorded player's pointer too, while the watcher is not using his own
			if (TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK && TheObserverCamera.getMode() == OBSERVER_CAMERA_PLAYER
					&& TheObserverCamera.getFollowedPlayerIndex() == thisPlayer->getPlayerIndex() && !TheLookAtTranslator->hasMouseMovedRecently())
			{
				TheMouse->setCursor( (Mouse::MouseCursor)(msg->getArgument( 4 )->integer) );
				ICoord2D mousePos = msg->getArgument( 5 )->pixel;
				TheMouse->setPosition( mousePos.x, mousePos.y );
				TheLookAtTranslator->setCurrentPos( mousePos );
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_TEAM0:
		case GameMessage::MSG_CREATE_TEAM1:
		case GameMessage::MSG_CREATE_TEAM2:
		case GameMessage::MSG_CREATE_TEAM3:
		case GameMessage::MSG_CREATE_TEAM4:
		case GameMessage::MSG_CREATE_TEAM5:
		case GameMessage::MSG_CREATE_TEAM6:
		case GameMessage::MSG_CREATE_TEAM7:
		case GameMessage::MSG_CREATE_TEAM8:
		case GameMessage::MSG_CREATE_TEAM9:
		{
			Int playerIndex = msg->getPlayerIndex();
			Player *player = ThePlayerList->getNthPlayer(playerIndex);
			DEBUG_ASSERTCRASH(player != NULL, ("Could not find player for create team message"));

			if (player == NULL)
			{
				break;
			}

			player->processCreateTeamGameMessage(msg->getType() - GameMessage::MSG_CREATE_TEAM0, msg);
			break;
		} // end create team command

		case GameMessage::MSG_SELECT_TEAM0:
		case GameMessage::MSG_SELECT_TEAM1:
		case GameMessage::MSG_SELECT_TEAM2:
		case GameMessage::MSG_SELECT_TEAM3:
		case GameMessage::MSG_SELECT_TEAM4:
		case GameMessage::MSG_SELECT_TEAM5:
		case GameMessage::MSG_SELECT_TEAM6:
		case GameMessage::MSG_SELECT_TEAM7:
		case GameMessage::MSG_SELECT_TEAM8:
		case GameMessage::MSG_SELECT_TEAM9:
		{
			Int playerIndex = msg->getPlayerIndex();
			Player *player = ThePlayerList->getNthPlayer(playerIndex);
			DEBUG_ASSERTCRASH(player != NULL, ("Could not find player for select team message"));

			if (player == NULL)
			{
				break;
			}

			player->processSelectTeamGameMessage(msg->getType() - GameMessage::MSG_SELECT_TEAM0, msg);
			break;
		}

		case GameMessage::MSG_ADD_TEAM0:
		case GameMessage::MSG_ADD_TEAM1:
		case GameMessage::MSG_ADD_TEAM2:
		case GameMessage::MSG_ADD_TEAM3:
		case GameMessage::MSG_ADD_TEAM4:
		case GameMessage::MSG_ADD_TEAM5:
		case GameMessage::MSG_ADD_TEAM6:
		case GameMessage::MSG_ADD_TEAM7:
		case GameMessage::MSG_ADD_TEAM8:
		case GameMessage::MSG_ADD_TEAM9:
		{
			Int playerIndex = msg->getPlayerIndex();
			Player *player = ThePlayerList->getNthPlayer(playerIndex);
			DEBUG_ASSERTCRASH(player != NULL, ("Could not find player for add team message"));

			if (player == NULL)
			{
				break;
			}

			player->processAddTeamGameMessage(msg->getType() - GameMessage::MSG_ADD_TEAM0, msg);
			break;
		}


		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_LOGIC_CRC:
		{
			if (TheNetwork)
			{
				Int slotIndex = -1;
				for (Int i=0; i<MAX_SLOTS; ++i)
				{
					if (thisPlayer->getPlayerType() == PLAYER_HUMAN && TheNetwork->getPlayerName(i) == thisPlayer->getPlayerDisplayName())
					{
						slotIndex = i;
						break;
					}
				}

				if (slotIndex < 0 || !TheNetwork->isPlayerConnected(slotIndex))
					break;

				if (thisPlayer->isLocalPlayer())
				{
#if defined(_DEBUG) || defined(_INTERNAL)
					// don't even put this in release, cause someone might hack it.
					if (!TheDebugIgnoreSyncErrors)
					{
#endif
						m_shouldValidateCRCs = TRUE;
#if defined(_DEBUG) || defined(_INTERNAL)
					}
#endif
				}

				//UnsignedInt oldCRC = m_cachedCRCs[msg->getPlayerIndex()];
				UnsignedInt newCRC = msg->getArgument(0)->integer;
				//DEBUG_LOG(("Recieved CRC of %8.8X from %ls on frame %d\n", newCRC,
					//thisPlayer->getPlayerDisplayName().str(), m_frame));
				m_cachedCRCs[msg->getPlayerIndex()] = newCRC; // to mask problem: = (oldCRC < newCRC)?newCRC:oldCRC;
			}
			else if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK)
			{
				UnsignedInt newCRC = msg->getArgument(0)->integer;
				//DEBUG_LOG(("Saw CRC of %X from player %d.  Our CRC is %X.  Arg count is %d\n",
					//newCRC, thisPlayer->getPlayerIndex(), getCRC(), msg->getArgumentCount()));

				TheRecorder->handleCRCMessage(newCRC, thisPlayer->getPlayerIndex(), (msg->getArgument(1)->boolean));
			}
			break;

		}  // end CRC message

		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_PURCHASE_SCIENCE:
		{
			ScienceType science = (ScienceType)msg->getArgument( 0 )->integer;

			// sanity
			if( science == SCIENCE_INVALID )
				break;
			
			thisPlayer->attemptToPurchaseScience(science);

			break;

		}  // end pick specialized science

	}  // end switch

	if( currentlySelectedGroup && TheAI->findGroup( selectedGroupID ) == NULL )
		currentlySelectedGroup = NULL;

	/**/ /// @todo: multiplayer semantics
	if (currentlySelectedGroup && orderedGroup == NULL && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK && TheGlobalData->m_useCameraInReplay && TheControlBar->getObserverLookAtPlayer() == thisPlayer /*&& !TheRecorder->isMultiplayer()*/)
	{
		const VecObjectID& selectedObjects = currentlySelectedGroup->getAllIDs();
		TheInGameUI->deselectAllDrawables();
		for (VecObjectID::const_iterator it = selectedObjects.begin(); it != selectedObjects.end(); ++it)
		{
			const Object *obj = findObjectByID(*it);
			if (obj)
			{
				Drawable *draw = obj->getDrawable();
				if (draw)
					TheInGameUI->selectDrawable(draw);
			}
		}
	}
	/**/

	if( currentlySelectedGroup != NULL )
	{
		TheAI->destroyGroup(currentlySelectedGroup);
	}

}  // end logicMessageDispatches
