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

// CommandXlat.cpp
// Translate raw input events into tactical commands
// Author: Michael S. Booth, February 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "stdlib.h"				// VC++ wants this here, or gives compile error...

#include "Common/AudioAffect.h"
#include "Common/ActionManager.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/MiscAudio.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/StatsCollector.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"

#include "GameClient/InGameUI.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Shell.h"
#include "GameClient/ControlBar.h"
#include "GameClient/SelectionInfo.h"
#include "GameClient/SelectionXlat.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"						
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GhostObject.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"


#include "Common/ThingFactory.h"
#include "GameLogic/Module/ContainModule.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/BuddyThread.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif



// Alt+F4 and the window's close button both end up as MSG_META_DEMO_INSTANT_QUIT
// (WinMain's WM_CLOSE posts it). With this off, that message had no handler at all:
// the title bar's X and Alt+F4 did nothing, and the only way out was the menus.
#define ALLOW_ALT_F4

//-------------------------------------------------------------------------------------------------
/**
 * Is the next order click a force fire?
 * Under Modern only the attack key arms it: ctrl is the "one shared pace" modifier on a move (see
 * issueMoveToLocationCommand), so a ctrl click could not ask for both.  Legacy's ctrl is still the
 * game's own force fire (InGameUI::isForceFireOn).
 */
Bool CommandXlat_isForceAttackTargeting( Bool forceAttackArmed, Bool attackMoveArmed )
{
	return forceAttackArmed && !attackMoveArmed;
}

static Bool isForceAttackTargeting( void )
{
	return CommandXlat_isForceAttackTargeting( TheInGameUI->isForceFireOn(),
																						 TheInGameUI->isInAttackMoveToMode() );
}

//-------------------------------------------------------------------------------------------------
/**
 * Does the stop key mean "cancel this building"?
 * Only for a single structure of your own that is still under construction - a mixed selection, an
 * enemy structure or anything already finished still means stop.
 */
Bool Command_stopMeansCancelConstruction( Int selectionCount, Bool locallyControlled, Bool underConstruction )
{
	return selectionCount == 1 && locallyControlled && underConstruction;
}

//-------------------------------------------------------------------------------------------------
/**
 * Is this right-button press the start of a formation line?
 * A GUI command waiting for a target owns the next click, and with nothing of your own selected
 * there is nobody to spread, so both of those leave the drag meaning nothing.
 */
Bool Command_formationDragArmed( Bool setting, Bool haveMovableSelection, Bool guiCommandPending )
{
	return setting && !guiCommandPending && haveMovableSelection;
}

//-------------------------------------------------------------------------------------------------
/**
 * Which order a finished formation line carries.
 * The armed modes turn each other off as they are armed, so at most one of these is ever set; the
 * order they are read in only decides what a line drawn out of a state nobody can reach would do.
 */
GameMessage::Type Command_formationMessage( Bool attackMoveArmed, Bool forceAttackArmed,
																					 Bool guardArmed )
{
	if( attackMoveArmed )
		return GameMessage::MSG_DO_FORMATION_ATTACKMOVETO;
	if( forceAttackArmed )
		return GameMessage::MSG_DO_FORMATION_FORCEATTACK;
	if( guardArmed )
		return GameMessage::MSG_DO_FORMATION_GUARD;
	return GameMessage::MSG_DO_FORMATION_MOVETO;
}

//-------------------------------------------------------------------------------------------------
SignalKind Command_signalKindForMeta( GameMessage::Type meta )
{
	switch( meta )
	{
		case GameMessage::MSG_META_SIGNAL_ATTACK:			return SIGNAL_ATTACK;
		case GameMessage::MSG_META_SIGNAL_DEFEND:			return SIGNAL_DEFEND;
		case GameMessage::MSG_META_SIGNAL_ATTENTION:	return SIGNAL_ATTENTION;
		default:																			return SIGNAL_KIND_COUNT;
	}
}

static Bool isFormationDragArmed( void )
{
	// a Legacy right drag is the camera scroll it always was
	if( TheGlobalData->isLegacyInput() )
		return FALSE;

	return Command_formationDragArmed( TheGlobalData->m_formationDrag,
																		 TheInGameUI->getSelectCount() > 0
																			&& TheInGameUI->areSelectedObjectsControllable(),
																		 TheInGameUI->getGUICommand() != NULL );
}


#if defined(_DEBUG) || defined(_INTERNAL)
/*non-static*/ Real TheSkateDistOverride = 0.0f;

void countObjects(Object *obj, void *userData)
{
	Int *numObjects = (Int *)userData;
	if (!numObjects || !obj)
		return;

	DEBUG_LOG(("Looking at obj %d (%s) - isEffectivelyDead()==%d, isDestroyed==%d, numObjects==%d\n",
		obj->getID(), obj->getTemplate()->getName().str(), obj->isEffectivelyDead(), obj->isDestroyed(), *numObjects));

	if (!obj->isEffectivelyDead() && !obj->isDestroyed() && !obj->isKindOf(KINDOF_INERT))
		++(*numObjects);
}

void printObjects(Object *obj, void *userData)
{
	if (!obj)
		return;

	Bool isDead = obj->isEffectivelyDead() || obj->isDestroyed();
	Bool isInert = obj->isKindOf(KINDOF_INERT);
	AsciiString statusStr = (isDead)?"Dead":(isInert)?"Inert":"Living";

	AsciiString line;
	if (obj->getName().isEmpty())
		line.format("  Obj#%d %s - (%g,%g) %s", obj->getID(), obj->getTemplate()->getName().str(),
			obj->getPosition()->x, obj->getPosition()->y, statusStr.str());
	else
		line.format("  Obj#%d %s (%s) - (%g,%g) %s", obj->getID(), obj->getTemplate()->getName().str(),
			obj->getName().str(), obj->getPosition()->x, obj->getPosition()->y, statusStr.str());
	TheScriptEngine->AppendDebugMessage(line, FALSE);
}

#endif
static Bool isSystemMessage( const GameMessage *msg );

enum{ DROPPED_MAX_PARTICLE_COUNT = 1000};

static Bool canSelectionSalvage( const Object *targetObj)
{
	if (!targetObj) {
		return FALSE;
	}

	if (!targetObj->isSalvageCrate()) {
		return FALSE;
	}

	const DrawableList *drawList = TheInGameUI->getAllSelectedDrawables();

	for (DrawableListCIt cit = drawList->begin(); cit != drawList->end(); ++cit) 
	{
		Drawable *draw = *cit;
		if (!draw) 
		{
			continue;
		}
		

		Object *obj = draw->getObject();
		if (!obj) 
		{
			continue;
		}

		if (obj->isKindOf(KINDOF_SALVAGER)) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static CanAttackResult canObjectForceAttack( Object *obj, const Object *victim, const Coord3D *pos )
{
	CanAttackResult result;
	result = obj->isAbleToAttack() ? ATTACKRESULT_POSSIBLE : ATTACKRESULT_NOT_POSSIBLE;
	if( result == ATTACKRESULT_NOT_POSSIBLE )
	{
		return ATTACKRESULT_NOT_POSSIBLE;
	}
	
	if (victim) 
	{
		result = obj->getAbleToAttackSpecificObject(ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );

		//Special case -- objects with spawn weapons have to do different checks. Stinger site with stinger soldiers is
		//the catalyst example.
    if ( obj->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
    {
      if( result != ATTACKRESULT_POSSIBLE && result != ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		  {
			  SpawnBehaviorInterface *spawnInterface = obj->getSpawnBehaviorInterface();
			  if( spawnInterface )
			  {
				  //We found the spawn interface, now get the closest slave to the target.
				  Object *slave = spawnInterface->getClosestSlave( victim->getPosition() );
				  if( slave ) 
				  {
					  result = slave->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );
				  }
			  }
		  }
      else // oh dear me. The wierd case of a garrisoncontainer being a KINDOF_SPAWNS_ARE_THE_WEAPONS... the AmericaBuildingFirebase
      {
        ContainModuleInterface *contain = obj->getContain();
        if ( contain )
        {
          Object *rider = contain->getClosestRider( victim->getPosition() );
          if ( rider )
          {
            result = rider->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER );
            if( result != ATTACKRESULT_NOT_POSSIBLE )
              return result;
          }
        }
      }

    } // end if spawnsRweapons

		
    return result;
	}
	else 
	{
		//Almost every combat unit can force attack a position. The exceptions include stationary units
		//that try to force attack in a location beyond their reach (range, LOS, etc).
		if( pos )
		{

			Object *testObj = obj;

			if( obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_SPAWNS_ARE_THE_WEAPONS ) )
			{
				SpawnBehaviorInterface *spawnInterface = obj->getSpawnBehaviorInterface();
				if( spawnInterface )
				{
					//We found the spawn interface, now get the closest slave to the target.
					Object *slave = spawnInterface->getClosestSlave( pos );
					if( slave )
					{
						testObj = slave;
					}
				}
        else
        {
    			result = obj->getAbleToUseWeaponAgainstTarget( ATTACK_NEW_TARGET, NULL, pos, CMD_FROM_PLAYER );
          if( result != ATTACKRESULT_POSSIBLE ) // oh dear me. The wierd case of a garrisoncontainer being a KINDOF_SPAWNS_ARE_THE_WEAPONS... the AmericaBuildingFirebase
          {
            ContainModuleInterface *contain = obj->getContain();
            if ( contain )
            {
              Object *rider = contain->getClosestRider( pos );
              if ( rider )
                testObj = rider;
            }
          }
        }
			}
			//Now evaluate the testObj again to see if it is capable of force attacking the pos.
			result = testObj->getAbleToUseWeaponAgainstTarget( ATTACK_NEW_TARGET, NULL, pos, CMD_FROM_PLAYER );
			return result;
		}
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

//-------------------------------------------------------------------------------------------------
static CanAttackResult canAnyForceAttack(const DrawableList *allSelected, const Object *victim, const Coord3D *pos )
{
	// check to make sure that allSelected can attack obj.
	for (DrawableListCIt cit = allSelected->begin(); cit != allSelected->end(); ++cit) 
	{
		Drawable *draw = *cit;
		if (!draw) 
		{
			continue;
		}
		
		Object *obj = draw->getObject();
		if (!obj) 
		{
			continue;
		}

		return canObjectForceAttack( obj, victim, pos );
	}

	return ATTACKRESULT_NOT_POSSIBLE;
}

//-------------------------------------------------------------------------------------------------
void pickAndPlayUnitVoiceResponse( const DrawableList *list, GameMessage::Type msgType, PickAndPlayInfo *info )
{
	if (!list) 
	{
		return;
	}

	const AudioEventRTS* soundToPlayPtr = NULL;

	Object *objectWithSound = NULL;
	Bool skip = false;

	Object *target = NULL;
	if( info && info->m_drawTarget )
	{
		target = info->m_drawTarget->getObject();
	}
	
	//Now, loop through all the drawables (even if you find a match on the first.
	//The innards are responsible for "upgrading" a sound that is played based on
	//priorities. For example, the voice move or voice crush. Voice move gets set
	//when we have a null event -- but if any of the units can crush the specified
	//target, then it changes to VoiceCrush.
	for( DrawableListCIt it = list->begin(); it != list->end(); ++it )
	{
		Object *obj = (*it)->getObject();

		if (obj->isKindOf( KINDOF_IGNORED_IN_GUI ))
			continue;

		// Use the object instead of the drawable to get the thing template from. This way, we get the 
		// sounds even if the thing is disguised as something else. (Ala bomb truck.)
		const ThingTemplate *templ = obj->getTemplate();
		if (!templ) 
		{
			return;
		}

		switch (msgType) 
		{
			case GameMessage::MSG_DOCK:
				soundToPlayPtr = templ->getPerUnitSound("VoiceSupply");
				objectWithSound = obj;
				skip = true;
				break;
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
			case GameMessage::MSG_CREATE_SELECTED_GROUP:
				soundToPlayPtr = templ->getVoiceSelect();
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_EVACUATE:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceUnload" );
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_DO_REPAIR:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceRepair" );
				objectWithSound = obj;
				skip = true;
				break;
#ifdef ALLOW_SURRENDER
			case GameMessage::MSG_PICK_UP_PRISONER:
				soundToPlayPtr = templ->getPerUnitSound( "VoicePickup" );
				objectWithSound = obj;
				skip = true;
				break;
#endif
			case GameMessage::MSG_COMBATDROP_AT_LOCATION:
			case GameMessage::MSG_COMBATDROP_AT_OBJECT:
				soundToPlayPtr = templ->getPerUnitSound( "VoiceCombatDrop" );
				objectWithSound = obj;
				skip = true;
				break;
			case GameMessage::MSG_ENTER:
				if( target && target->isKindOf( KINDOF_HEAL_PAD ) )
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceGetHealed" );
				}
				else if( target && target->isKindOf( KINDOF_STRUCTURE ) )
				{
					if( obj->getRelationship( target ) == ENEMIES )
					{
						//Saboteurs
						soundToPlayPtr = templ->getPerUnitSound( "VoiceEnterHostile" );
					}
					else
					{
						soundToPlayPtr = templ->getPerUnitSound( "VoiceGarrison" );
					}
				}
				// order matters: we want to know if I consider it to be an ally, not vice versa
				else if( target && obj->getRelationship(target) != ALLIES )
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceEnterHostile" );
				}
				else
				{
					soundToPlayPtr = templ->getPerUnitSound( "VoiceEnter" );
				}
				objectWithSound = obj;
				skip = true;
				break;
			
			case GameMessage::MSG_DO_MOVETO:
			case GameMessage::MSG_DO_ATTACKMOVETO:
			case GameMessage::MSG_GET_REPAIRED:
			case GameMessage::MSG_GET_HEALED:
			case GameMessage::MSG_DO_SALVAGE:
			{
				AIUpdateInterface *ai = obj->getAI();
				if( ai )
				{
					//This flag determines if the object has started moving yet... if not
					//it's a good initial check.
					Bool isEffectivelyMoving = ai->isMoving() || ai->isWaitingForPath();

					if( TheInGameUI->isInWaypointMode() )
					{
						if( isEffectivelyMoving )
						{
							//Don't want to play the sound unless he's not moving!
							continue;
						}
					}

					//Default to voice move (it'll be selected if we can't find a crush case as we iterate
					//through the rest of the drawables)
					if( !soundToPlayPtr )
					{	
						soundToPlayPtr = templ->getVoiceMove();
						objectWithSound = obj;
					}
					if( TheInGameUI->isInForceMoveToMode() && target )
					{
						if( obj->canCrushOrSquish( target ) )
						{
							//Change it to voice crush because we are intentionally trying to crush this!
							soundToPlayPtr = templ->getPerUnitSound( "VoiceCrush" );
							objectWithSound = obj;
							skip = true;
						}
					}
					if (msgType == GameMessage::MSG_DO_SALVAGE) 
					{
						const AudioEventRTS *tempSound = templ->getPerUnitSound( "VoiceSalvage" );
						if (TheAudio->isValidAudioEvent(tempSound))
						{
							soundToPlayPtr = tempSound;
							objectWithSound = obj;
							skip = true;
						}
					}
					// Special case for GLA worker to use a different set of move voices when he has received the worker shoes upgrade
					Player *player = obj->getControllingPlayer();
					static const UpgradeTemplate *workerShoeTemplate = TheUpgradeCenter->findUpgrade( "Upgrade_GLAWorkerShoes" );
					if (player && player->hasUpgradeComplete(workerShoeTemplate))
					{
						if (obj->isKindOf(KINDOF_INFANTRY) && obj->isKindOf(KINDOF_DOZER) && obj->isKindOf(KINDOF_HARVESTER)) // Only Workers fit all 3 
						{
							soundToPlayPtr = templ->getPerUnitSound( "VoiceMoveUpgraded" );
							objectWithSound = obj;
							skip = true;
						}
					}
				}
				break;
			}

			case GameMessage::MSG_RESUME_CONSTRUCTION:
			case GameMessage::MSG_DOZER_CONSTRUCT:
			case GameMessage::MSG_DOZER_CONSTRUCT_LINE:
			{
				soundToPlayPtr = templ->getPerUnitSound( "VoiceBuildResponse" );
				objectWithSound = obj;
				skip = true;

				break;
			}

			case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
			{
				soundToPlayPtr = templ->getPerUnitSound( "VoiceBombard" );
				objectWithSound = obj;
				skip = true;

				if (TheAudio->isValidAudioEvent(soundToPlayPtr)) {
					break;
				} else {
					// clear out the sound to play, and drop into the attack object logic.
					soundToPlayPtr = NULL;
				}
			}
			
			case GameMessage::MSG_SWITCH_WEAPONS:
			{
				if( info && info->m_weaponSlot )
				{
					switch( *info->m_weaponSlot )
					{
						case PRIMARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoicePrimaryWeaponMode" );
							break;
						case SECONDARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceSecondaryWeaponMode" );
							break;
						case TERTIARY_WEAPON:
							soundToPlayPtr = templ->getPerUnitSound( "VoiceTertiaryWeaponMode" );
							break;
					}
					objectWithSound = obj;
					skip = true;
				}
				break;
			}
			
			case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
			case GameMessage::MSG_DO_ATTACK_OBJECT:
			case GameMessage::MSG_DO_WEAPON_AT_OBJECT:
			{
				if( !soundToPlayPtr )
				{
					//Low priority sounds -- only do this if uninitialized.
					if( info && info->m_air )
						soundToPlayPtr = templ->getVoiceAttackAir();
					else
						soundToPlayPtr = templ->getVoiceAttack();

					objectWithSound = obj;
				}

				//Look for a specialty weapon fired via command button!
				Bool specialtyWeapon = FALSE;
				if( msgType == GameMessage::MSG_DO_WEAPON_AT_OBJECT )
				{
					specialtyWeapon = TRUE;
				}

				Weapon *weapon = obj->getCurrentWeapon();
				if( info && info->m_weaponSlot )
				{
					weapon = obj->getWeaponInWeaponSlot( *info->m_weaponSlot );
				}
				if( weapon )
				{
					switch( weapon->getDamageType() )
					{
// this stays, even if ALLOW_SURRENDER is not defed, since flashbangs still use 'em
						case DAMAGE_SURRENDER:
							if( target && target->isKindOf( KINDOF_STRUCTURE ) )
							{
								//We are attempting to take over a building with rangers and flashbangs!
								soundToPlayPtr = templ->getPerUnitSound( "VoiceClearBuilding" );
							}
							else
							{
								//Special for subdue attacks.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceSubdue" );
							}
							objectWithSound = obj;
							skip = true;
							break;
						case DAMAGE_DISARM:
							//Special for mine clearing attacks.
							soundToPlayPtr = templ->getPerUnitSound( "VoiceDisarm" );
							objectWithSound = obj;
							skip = true;
							break;
						case DAMAGE_KILLPILOT:
							if( specialtyWeapon )
							{
								//Special for sniping vehicle pilots.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceSnipePilot" );
								objectWithSound = obj;
								skip = true;
							}
							break;
						case DAMAGE_MELEE:
							if( specialtyWeapon )
							{
								//Special for stabbing
								soundToPlayPtr = templ->getPerUnitSound( "VoiceMelee" );
								objectWithSound = obj;
								skip = true;
							}
							break;
					}
				}
				break;
			}
			
			case GameMessage::MSG_DO_WEAPON_AT_LOCATION:
			{

				if( !soundToPlayPtr )
				{
					//Low priority sounds -- only do this if uninitialized.
					if( info && info->m_air )
						soundToPlayPtr = templ->getVoiceAttackAir();
					else
						soundToPlayPtr = templ->getVoiceAttack();
					objectWithSound = obj;
				}

				//Check for possibility of a higher priority sound!
				Weapon *weapon = obj->getCurrentWeapon();
				if( info && info->m_weaponSlot )
				{
					weapon = obj->getWeaponInWeaponSlot( *info->m_weaponSlot );
				}
				if( weapon )
				{
					switch( weapon->getDamageType() )
					{
// this stays, even if ALLOW_SURRENDER is not defed, since flashbangs still use 'em
						case DAMAGE_SURRENDER:
							break;
						case DAMAGE_DISARM:
							//Special for mine clearing attacks.
							soundToPlayPtr = templ->getPerUnitSound( "VoiceDisarm" );
							objectWithSound = obj;
							break;
						//these are specific to the guicommand based ground attacks, the toxin sprinkler and the firestorm wall thing
						//hence the additional check for it being a non-primary weapon
 						case DAMAGE_FLAME:
							if (weapon->getWeaponSlot() != PRIMARY_WEAPON)
							{
 								soundToPlayPtr = templ->getPerUnitSound( "VoiceFlameLocation" );
 								objectWithSound = obj;
							}
 							break;
 						case DAMAGE_POISON:
							if (weapon->getWeaponSlot() != PRIMARY_WEAPON)
							{
 								soundToPlayPtr = templ->getPerUnitSound( "VoicePoisonLocation" );
 								objectWithSound = obj;
							}
 							break;
						default:
							if( !weapon->getName().compare( "ComancheRocketPodWeapon" ) )
							{
								//Special case for comanche rocket pods.
								soundToPlayPtr = templ->getPerUnitSound( "VoiceFireRocketPods" );
								objectWithSound = obj;
								skip = true;
							}
					}
				}
				break;
			}
			case GameMessage::MSG_DO_GUARD_POSITION:
			case GameMessage::MSG_DO_GUARD_OBJECT:
				soundToPlayPtr = templ->getVoiceGuard();
				objectWithSound = obj;
				skip = true;
				break;

			case GameMessage::MSG_DO_SPECIAL_POWER:
			case GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION:
			case GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT:
			{
				if( info && info->m_specialPowerType != SPECIAL_INVALID )
				{
					SpecialPowerModuleInterface *spmInterface = obj->findSpecialPowerModuleInterface( info->m_specialPowerType );
					if( spmInterface )
					{
						objectWithSound = obj;
						soundToPlayPtr = &spmInterface->getInitiateSound();
						skip = TRUE;
					}
				}
				break;
			}

			case GameMessage::MSG_INTERNET_HACK:
				objectWithSound = obj;
				soundToPlayPtr = templ->getPerUnitSound( "VoiceHackInternet" );
				skip = TRUE;
				break;

			default:
				DEBUG_LOG(("Requested to add voice of message type %d, but don't know how - jkmcd\n", msgType));
				break;
		}
		if( skip )
		{
			//The unit sound doesn't have any sort of priority or special case, so simply play the first one that comes along.
			break;
		}
	
	}//next drawable in list
	



	if (!soundToPlayPtr)
		return;

	AudioEventRTS soundToPlay = *soundToPlayPtr;

	// to prevent voice stepping.
	if( objectWithSound )
	{
		soundToPlay.setObjectID( objectWithSound->getID() );
		TheAudio->addAudioEvent(&soundToPlay);

		// This seems really hacky, and MarkL admits that it is. However, we do this so that we 
		// can "randomly" pick a different sound the next time, if we have 3 or more sounds. - jkmcd
		((AudioEventRTS*)soundToPlayPtr)->setPlayingAudioIndex( soundToPlay.getPlayingAudioIndex() );

		if( objectWithSound->testStatus( OBJECT_STATUS_IS_CARBOMB ) )
		{
			//Additional sounds for terrorists in cars.
			switch (msgType) 
			{
				case GameMessage::MSG_DO_FORCE_ATTACK_GROUND:
				case GameMessage::MSG_DO_FORCE_ATTACK_OBJECT:
				case GameMessage::MSG_DO_ATTACK_OBJECT:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarAttackVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;

				case GameMessage::MSG_DO_MOVETO:
				case GameMessage::MSG_DO_ATTACKMOVETO:
				case GameMessage::MSG_GET_REPAIRED:
				case GameMessage::MSG_GET_HEALED:
				case GameMessage::MSG_DO_SALVAGE:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarMoveVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;

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
				case GameMessage::MSG_CREATE_SELECTED_GROUP:
					soundToPlay = TheAudio->getMiscAudio()->m_terroristInCarSelectVoice;
					soundToPlay.setObjectID( objectWithSound->getID() );
					TheAudio->addAudioEvent(&soundToPlay);
					break;
			}
		}
	}
}


//------------------------------------------------------------------------------------
/**
 * Find a suitable command center to view.
 */

struct CommandCenterLocator
{
	Bool atLeastOne;
	Bool isCommandCenter;
	Int val;
	Coord3D loc;

	CommandCenterLocator() : atLeastOne(false), isCommandCenter(false), val(-1)
	{
		loc.zero();
	}
};

void findCommandCenterOrMostExpensiveBuilding(Object* obj, void* vccl)
{
	if (!obj) {
		return;
	}

	CommandCenterLocator *ccl = (CommandCenterLocator*) vccl;

	// here's the deal. We want to get the first Command Center in the list.
	// Barring that, we want the most expensive structure we currently own.
	if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
		ccl->isCommandCenter = true;
		ccl->loc = *obj->getPosition();
	} else if (!ccl->isCommandCenter) {
		if (!obj->isKindOf(KINDOF_STRUCTURE)) {
			return;
		}

		Int costToBuild = obj->getTemplate()->calcCostToBuild(obj->getControllingPlayer());
		if (costToBuild > ccl->val) {
			ccl->val = costToBuild;
			ccl->loc = *obj->getPosition();
		}
	}

	ccl->atLeastOne = true;
}

static void viewCommandCenter( void )
{
	Player* localPlayer = ThePlayerList->getLocalPlayer();
	if (!localPlayer) 
		return;

	CommandCenterLocator ccl;
	localPlayer->iterateObjects(findCommandCenterOrMostExpensiveBuilding, &ccl);

	if (ccl.atLeastOne) {
		TheTacticalView->lookAt(&ccl.loc);
	} else {
		// @todo. Find their starting position and look at that instead?		
	}
}



//----------------- Select and View Hero -----------------------------------

struct HeroHolder
{
	Object *hero;
};

void amIAHero(Object* obj, void* heroHolder)
{


	if (!obj || ((HeroHolder*)heroHolder)->hero != NULL) 
	{
		return;
	}

	if (obj->isKindOf( KINDOF_HERO )) 
	{
		((HeroHolder*)heroHolder)->hero = obj;
	}
}



static Object *iNeedAHero( void )
{
	Player* localPlayer = ThePlayerList->getLocalPlayer();
	if (!localPlayer) 
		return NULL;

	HeroHolder heroHolder;
	heroHolder.hero = NULL;

	localPlayer->iterateObjects(amIAHero, (void*)&heroHolder);

	return heroHolder.hero;

}

//------------------------------------------------------------------------------------
/**
 * Create DO_MOVE_TO messages for each selected object, instructing it to move to the given location.
 */
GameMessage::Type CommandTranslator::issueMoveToLocationCommand( const Coord3D *pos, Drawable *drawableInWay,
																																 CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;
	Object *obj = drawableInWay ? drawableInWay->getObject() : NULL;

	Bool isForceAttackable = FALSE;
	if (obj) {
		isForceAttackable = obj->isKindOf(KINDOF_FORCEATTACKABLE);
	}

	if (m_teamExists)
	{
		Bool forceAttackHere = isForceAttackTargeting() && isForceAttackable;

		// the guard key posts the selection where it is pointed.  Under shift it joins the list as its
		// last order: clear this, then sit there
		if( TheInGameUI->isGuardArmed() )
		{
			msgType = GameMessage::MSG_DO_GUARD_POSITION;
		}
		else if( TheInGameUI->isInAttackMoveToMode())
		{
			msgType = GameMessage::MSG_DO_ATTACKMOVETO;
		}
		else if( TheInGameUI->isInForceMoveToMode() )
		{
			msgType = GameMessage::MSG_DO_FORCEMOVETO;
		}
		else if( forceAttackHere )
		{
			msgType = GameMessage::MSG_DO_ATTACK_OBJECT;
		}
		else
		{
			msgType = GameMessage::MSG_DO_MOVETO;
		}
		if( commandType == DO_COMMAND )
		{
			GameMessage *movemsg = TheMessageStream->appendMessage( msgType );
			if (msgType == GameMessage::MSG_DO_ATTACK_OBJECT)
				movemsg->appendObjectIDArgument( obj->getID() );
			else
				movemsg->appendLocationArgument( *pos );

			// ctrl on an attack move click asks for one shared pace - the force attack modifier is
			// the ctrl key, and it means nothing else while the attack move cursor is up
			if (msgType == GameMessage::MSG_DO_ATTACKMOVETO)
				movemsg->appendBooleanArgument( TheInGameUI->isInForceAttackMode() );

			// a posted unit holds its spot and shoots what walks into range, rather than chasing it
			// off the post the player put it on
			if (msgType == GameMessage::MSG_DO_GUARD_POSITION)
				movemsg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );

		}  // end if
	}
	
	// only make sounds if we really did the command messages
	if( commandType == DO_COMMAND )
	{
		PickAndPlayInfo info;
		info.m_drawTarget = drawableInWay;
		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_MOVETO, &info );
	}  // end if
	
	if(TheStatsCollector)
		TheStatsCollector->incrementMoveCount();

	// return the actual msg type used
	return msgType;

}

//------------------------------------------------------------------------------------
/// @todo Play attack command response sound on client-side to hide latency
GameMessage::Type CommandTranslator::createAttackMessage( Drawable *draw, 
																													Drawable *other,
																													CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	// the drawable must have an object to be able to attack
	if( draw->getObject() == NULL )
		return msgType;

	// the target must have an object to be attacked
	if( other->getObject() == NULL )
		return msgType;

	// insert object attack command message into stream
	msgType = GameMessage::MSG_DO_ATTACK_OBJECT;

	// only make the message if we are really doing a command
	if( commandType == DO_COMMAND )
	{
		GameMessage *attackmsg = TheMessageStream->appendMessage( msgType );

		attackmsg->appendObjectIDArgument( other->getObject()->getID() );	// must pass object IDs to logic

	}  // end if

	// return the message type created
	return msgType;

}

//------------------------------------------------------------------------------------
/**
 * Create DO_ATTACK_GROUND_OBJECT messages for each selected object, instructing it to attack the given enemy.
 * Return TRUE if any attacks actually occurred.
 */
GameMessage::Type CommandTranslator::issueAttackCommand( Drawable *target,	
																												 CommandEvaluateType commandType,
																												 GUICommandType command )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if (target == NULL)
		return msgType;

	// you cannot attack an enemy that has no object representation
	Object *targetObj = target->getObject();
	if( !targetObj )
		return msgType;
	
	if( m_teamExists )
	{

		//DEBUG_LOG(("issuing team-attack cmd against %s\n",enemy->getTemplate()->getName().str()));

		// insert team attack command message into stream
		switch( command )
		{
#ifdef ALLOW_SURRENDER
			case GUICOMMANDMODE_PICK_UP_PRISONER:
				msgType = GameMessage::MSG_PICK_UP_PRISONER;
				break;
#endif
			case GUI_COMMAND_NONE:
				msgType = GameMessage::MSG_DO_ATTACK_OBJECT;
				break;
			default:
				DEBUG_ASSERTCRASH( 0, ("issueAttackCommand was passed in a GUICommandType type that isn't supported yet...") );
				return msgType;
		}

		// only create the message if our command type is DO_COMMAND
		if( commandType == DO_COMMAND )
		{
			GameMessage *attackMsg;

			attackMsg = TheMessageStream->appendMessage( msgType );
		
			attackMsg->appendObjectIDArgument( targetObj->getID() );	// must pass target object ID to logic

			// if we have a stats collector, inrement the stats
			if(TheStatsCollector)
				TheStatsCollector->incrementAttackCount();
		}  // end if
	}
	else
	{
		DEBUG_LOG(("issuing NON-team-attack cmd against %s\n",target->getTemplate()->getName().str()));

		// send single attack command for selected drawable
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();

		// loop through all the selected drawables
		Drawable *draw;
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			draw = *it;
			msgType = createAttackMessage(draw, target, commandType );
		}
	}
	
	// only make sounds if the command was for real
	if( commandType == DO_COMMAND )
	{
		PickAndPlayInfo info;
		info.m_air = targetObj->isUsingAirborneLocomotor();
		info.m_drawTarget = target;
		pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );
	}  // end if

	// return the actual message type created
	return msgType;

}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueSpecialPowerCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos, Object* ignoreSelObj )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( !command || !command->getSpecialPowerTemplate())
	{
		return msgType;
	}

	Drawable* sourceDraw = ignoreSelObj ? ignoreSelObj->getDrawable() : TheInGameUI->getFirstSelectedDrawable();
	ObjectID specificSource = ignoreSelObj ? ignoreSelObj->getID() : INVALID_ID;

	if( BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{
		// OBJECT BASED SPECIAL
		if (!command->isValidObjectTarget(sourceDraw, target))
			return msgType;

		msgType = GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendObjectIDArgument( target->getObject()->getID() );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );

			// say   something like " I think I'll put some dynamite on that there tank."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		}
	}
	else if( BitTest( command->getOptions(), NEED_TARGET_POS ) )
	{
		//LOCATION BASED SPECIAL
		msgType = GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendLocationArgument( *pos );
			msg->appendRealArgument( INVALID_ANGLE ); //We don't use the angle (unless we're using a construction special in PlaceEventTranslator).
			//Object in way.... some specials care, others don't
			ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			msg->appendObjectIDArgument( targetID );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );

			// say   something like " I think I'll put a timed charge on the ground, here."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );
		
		}
	}
	else
	{
		//NO TARGET SPECIAL
		msgType = GameMessage::MSG_DO_SPECIAL_POWER;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
			msg->appendIntegerArgument( command->getOptions() );
			msg->appendObjectIDArgument( specificSource );

			// say   something like " I think I'll set down my laptop and hack some cash from a bank in the Cayman Islands."
			PickAndPlayInfo info;
			info.m_drawTarget = target;
			info.m_specialPowerType = command->getSpecialPowerTemplate()->getSpecialPowerType();
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), msgType, &info );
		
		}
	}

	if( command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT && commandType == DO_COMMAND )
	{
		Object *obj = sourceDraw->getObject();
		SpecialPowerUpdateInterface *spUpdate = obj->findSpecialPowerWithOverridableDestination();
		if( spUpdate )
		{
			//Deselect the drawables before posting the selection message.
			TheInGameUI->deselectAllDrawables();

			//Because we just launched a special power via shortcut, and the special power accepts input
			//from the player (particle uplink cannon, spectre gunship), simply select the object now.
			//--------------------
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND );
			// creating a new team so pass in true
			teamMsg->appendBooleanArgument( TRUE );
			teamMsg->appendObjectIDArgument( obj->getID() );

			TheInGameUI->selectDrawable( obj->getDrawable() );
		}
	}

	

	return msgType;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueCombatDropCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos )
{
	if( !command )
	{
		return GameMessage::MSG_INVALID;
	}

	if( target != NULL && BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{

		// OBJECT BASED SPECIAL
		if (!command->isValidObjectTarget(TheInGameUI->getFirstSelectedDrawable(), target))
			return GameMessage::MSG_INVALID;

		GameMessage::Type msgType = GameMessage::MSG_COMBATDROP_AT_OBJECT;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg = TheMessageStream->appendMessage( msgType );
			ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			msg->appendObjectIDArgument( targetID );
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_COMBATDROP_AT_OBJECT );
		}
		return msgType;
	}
	else if ( BitTest( command->getOptions(), NEED_TARGET_POS ) )
	{
		GameMessage::Type msgType = GameMessage::MSG_COMBATDROP_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg = TheMessageStream->appendMessage( msgType );
			msg->appendLocationArgument( *pos );
			pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_COMBATDROP_AT_LOCATION );
		}
		return msgType;
	}
	else
	{
		return GameMessage::MSG_INVALID;
	}
}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::issueFireWeaponCommand( const CommandButton *command, CommandEvaluateType commandType, Drawable *target, const Coord3D *pos )
{
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	if( !command )
	{
		return msgType;
	}

	if( BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
	{
		//OBJECT BASED FIRE WEAPON
		if (!target || !target->getObject())
			return msgType;

		if (!command->isValidObjectTarget(TheInGameUI->getFirstSelectedDrawable(), target))
			return msgType;

		if( BitTest( command->getOptions(), ATTACK_OBJECTS_POSITION ) )
		{
			//Actually, you know what.... we want to attack the object's location instead.
			msgType = GameMessage::MSG_DO_WEAPON_AT_LOCATION;
			if( commandType == DO_COMMAND )
			{
				GameMessage *msg;
				msg = TheMessageStream->appendMessage( msgType );
				msg->appendIntegerArgument( command->getWeaponSlot() );
				msg->appendLocationArgument( *pos );
				msg->appendIntegerArgument( command->getMaxShotsToFire() );
				//Object in way.... some location weapons care, others don't
				ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
				msg->appendObjectIDArgument( targetID );
			}
		}
		else
		{
			msgType = GameMessage::MSG_DO_WEAPON_AT_OBJECT;
			if( commandType == DO_COMMAND )
			{
				GameMessage *msg;
				msg = TheMessageStream->appendMessage( msgType );
				msg->appendIntegerArgument( command->getWeaponSlot() );
				ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
				msg->appendObjectIDArgument( targetID );
				msg->appendIntegerArgument( command->getMaxShotsToFire() );

				//play a unit specific sound?
				PickAndPlayInfo info;
				WeaponSlotType slot = command->getWeaponSlot();
				info.m_weaponSlot = &slot;
 				pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_WEAPON_AT_OBJECT, &info );
			}
		}
	}
	else if( BitTest( command->getOptions(), NEED_TARGET_POS ) )
	{
		//LOCATION BASED FIRE WEAPON
		msgType = GameMessage::MSG_DO_WEAPON_AT_LOCATION;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			msg->appendIntegerArgument( command->getWeaponSlot() );
			msg->appendLocationArgument( *pos );
			msg->appendIntegerArgument( command->getMaxShotsToFire() );
			//Object in way.... some location weapons care, others don't
			ObjectID targetID = (target && target->getObject()) ? target->getObject()->getID() : INVALID_ID;
			msg->appendObjectIDArgument( targetID );
		}
	}
	else
	{
		//NO TARGET WEAPON
		msgType = GameMessage::MSG_DO_WEAPON;
		if( commandType == DO_COMMAND )
		{
			GameMessage *msg;
			msg = TheMessageStream->appendMessage( msgType );
			DEBUG_ASSERTCRASH( (command->getSpecialPowerTemplate()), ("No Special Power Weapon here to 'do' with! ML"));
			msg->appendIntegerArgument( command->getSpecialPowerTemplate()->getID() );
		}
	}
	
	return msgType;
}

//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::createEnterMessage( Drawable *enter, 
																												 CommandEvaluateType commandType )
{
	GameMessage::Type msgType = GameMessage::MSG_ENTER;

	// if we're just evaluating then get out of here without actually doing the action
	if( commandType == EVALUATE_ONLY )
		return msgType;

	if (!enter || !enter->getObject())
		return msgType;

	// sanity
	DEBUG_ASSERTCRASH( commandType == DO_COMMAND, ("createEnterMessage - commandType is not DO_COMMAND\n") );

	if( m_teamExists )
	{
		PickAndPlayInfo info;
		info.m_drawTarget = enter;
		pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), msgType, &info );

		GameMessage *enterMsg = TheMessageStream->appendMessage( msgType );
		enterMsg->appendObjectIDArgument( INVALID_ID );		// 0 means current "selection team" of this player
		enterMsg->appendObjectIDArgument( enter->getObject()->getID() );

	}  // end if
	else
	{
		DEBUG_CRASH(("Shouldn't get here. jkmcd"));
	}

	// return the type of the message used
	return msgType;

}  // end createEnterMessage

//====================================================================================
CommandTranslator::CommandTranslator() : 
	m_objective(0),
	m_teamExists(false),
	m_mouseRightDown(0),
	m_mouseRightUp(0),
	m_formationDragArmed(FALSE)
{
	m_mouseRightDragAnchor.x = 0;
	m_mouseRightDragAnchor.y = 0;
	m_mouseRightDragLift.x = 0;
	m_mouseRightDragLift.y = 0;
	m_formationDragAnchor.x = 0;
	m_formationDragAnchor.y = 0;
}

//====================================================================================
CommandTranslator::~CommandTranslator()
{
}

//-------------------------------------------------------------------------------------------------
/**
 * The button that drew a formation line came back up.  A drag that travelled far enough to be a
 * line becomes the order its armed mode names; one that never did was a click, and the click
 * message behind this release gives that order instead.
 */
void CommandTranslator::finishFormationDrag( const ICoord2D& lift )
{
	const Bool wasDrag = TheInGameUI->isFormationDragging();

	if( wasDrag )
		TheInGameUI->addFormationDragPoint( lift );

	// copied out before the drag is cleared, which throws the curve away
	std::vector<ICoord2D> curve = TheInGameUI->getFormationDragPoints();

	// the preview goes with the drag; from here on the units' own goals are what gets drawn,
	// so the picture carries on without a break
	TheInGameUI->clearFormationDrag();
	m_formationDragArmed = FALSE;

	if( !wasDrag || curve.size() < 2 )
		return;

	// the same curve means "attack along this" while one of the attack modes is armed, which
	// is the artillery gesture: a line of fire instead of a line of tanks.  Attack move
	// walks it and shoots what it meets; the attack key fires on the line where it stands
	const GameMessage::Type formationType =
		Command_formationMessage( TheInGameUI->isInAttackMoveToMode(),
															TheInGameUI->isForceAttackArmed(),
															TheInGameUI->isGuardArmed() );

	// the traced curve becomes world points; who stands where along it is decided on
	// the logic side, where every machine decides it the same way
	std::vector<Coord3D> line;
	for( std::vector<ICoord2D>::const_iterator it = curve.begin(); it != curve.end(); ++it )
	{
		Coord3D world;
		TheTacticalView->screenToTerrain( &(*it), &world );
		line.push_back( world );
	}

	// a line drawn with the attack key across enemies is aimed at them: each one it crosses goes on
	// the target list, in the order the line meets them, and the ground under the line is not shot
	// at.  Only a line that crosses nobody fires on the ground along it
	const Bool aimedAtTargets = formationType == GameMessage::MSG_DO_FORMATION_FORCEATTACK
															&& TheInGameUI->issueAttackLine( line ) > 0;
	if( !aimedAtTargets )
	{
		// a line drawn with shift is the next order on the units' list
		if( TheInGameUI->isInWaypointMode() )
			TheInGameUI->markNextOrderQueued( ORDER_QUEUE_APPEND );

		GameMessage *newMsg = TheMessageStream->appendMessage( formationType );
		for( std::vector<Coord3D>::const_iterator it = line.begin(); it != line.end(); ++it )
			newMsg->appendLocationArgument( *it );
	}

	TheInGameUI->spendOrderKey();

	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if( selected && !selected->empty() )
		pickAndPlayUnitVoiceResponse( selected, GameMessage::MSG_DO_MOVETO );
}



//-------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::evaluateForceAttack( Drawable *draw, const Coord3D *pos, CommandEvaluateType type )
{
	// evaluateForceAttack is used to determine whether or not we can force attack the 
	// given target, and if we can, to issue the appropriate command.

	GameMessage::Type retVal = GameMessage::MSG_INVALID;
	if( !draw && !pos ) 
	{
		return retVal;
	}

	const DrawableList *allSelected = TheInGameUI->getAllSelectedDrawables();

	if( draw ) 
	{
		Object *obj = draw ? draw->getObject() : NULL;
		if( !obj ) 
		{
			return retVal;
		}

		CanAttackResult result = canAnyForceAttack( allSelected, obj, pos );

		if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			retVal = GameMessage::MSG_DO_FORCE_ATTACK_OBJECT;

			if( type == DO_COMMAND )
			{
				pickAndPlayUnitVoiceResponse( allSelected, retVal );
				if( TheInGameUI->isInWaypointMode() )
					TheInGameUI->markNextOrderQueued( ORDER_QUEUE_APPEND );
				GameMessage *newMsg = TheMessageStream->appendMessage( retVal );
				newMsg->appendObjectIDArgument( obj->getID() );

			}
			else if( type == DO_HINT ) 
			{
				retVal = GameMessage::MSG_DO_FORCE_ATTACK_OBJECT_HINT;
				// Don't need the message back, cause there is nothing to append to it.
				TheMessageStream->appendMessage( retVal );
			}
		}
		else if( result == ATTACKRESULT_INVALID_SHOT && type == DO_HINT )
		{
			retVal = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
			TheMessageStream->appendMessage( retVal );
		}
	} 
	else if( pos ) 
	{
		CanAttackResult result = canAnyForceAttack( allSelected, NULL, pos );

		if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			retVal = GameMessage::MSG_DO_FORCE_ATTACK_GROUND;

			if( type == DO_COMMAND )
			{
				pickAndPlayUnitVoiceResponse( allSelected, retVal );
				if( TheInGameUI->isInWaypointMode() )
					TheInGameUI->markNextOrderQueued( ORDER_QUEUE_APPEND );
				GameMessage *newMsg = TheMessageStream->appendMessage( retVal );
				newMsg->appendLocationArgument( *pos );
			}
			else if( type == DO_HINT ) 
			{
				retVal = GameMessage::MSG_DO_FORCE_ATTACK_GROUND_HINT;
				// Don't need the message back, cause there is nothing to append to it.
				TheMessageStream->appendMessage( retVal );
			}
		}
		else if( result == ATTACKRESULT_INVALID_SHOT && type == DO_HINT )
		{
			retVal = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
			TheMessageStream->appendMessage( retVal );
		}
	}

	return retVal;
}

// ------------------------------------------------------------------------------------------------
/** This method and the order of operations in the check here, determine what command would
	* actually happen (if type parameter == DO_COMMAND) if the user clicked on the drawable
	* 'draw'.  If type == DO_HINT, then the user hasn't actually clicked, but has moused over
	* the drawable 'draw' and we want to generate a hint message as to what the actual
	* command would be if clicked
	* NOTE: draw can be NULL, in which case we give a hint for the location */
// ------------------------------------------------------------------------------------------------
GameMessage::Type CommandTranslator::evaluateContextCommand( Drawable *draw, 
																														 const Coord3D *pos, 
																														 CommandEvaluateType type )
{
	Object *obj = draw ? draw->getObject() : NULL;
	Drawable *drawableInWay = draw;

	//This piece of code is used to prevent interaction with unselectable objects or masked objects. When we
	//call this function, we typically pass in both a position and a drawable (if applicable), so if the
	//drawable is invalid... then convert it to a position to be evaluated instead.
	//Added: shrubberies are the exception for interactions...
	//Removed: GS Took out ObjectStatusUnselectable, since that status only prevents selection, not everything
	if( obj == NULL || 
			obj->getStatusBits().test( OBJECT_STATUS_MASKED ) && 
			!obj->isKindOf(KINDOF_SHRUBBERY) && !obj->isKindOf(KINDOF_FORCEATTACKABLE) ) 
	{
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate 
		//a position interaction.
		draw = NULL;
		obj = NULL;
	}  //  end if

	// If the thing is a mine, and is locally controlled, then we should issue a moveto to its location.
	if (obj && obj->isLocallyControlled() && obj->isKindOf(KINDOF_MINE)) {
		draw = NULL;
		obj = NULL;
	}

	if( TheInGameUI->isInForceMoveToMode() ) 
	{
		//Nulling out the draw and obj pointer will force the remainder of this code to evaluate 
		//a position interaction.
		draw = NULL;
		obj = NULL;
	} else if (isForceAttackTargeting() ) {
		// setting the drawableInWay to draw will allow us to force attack in the issue move command
		// if there is a location to which we should attack.
		drawableInWay = draw;
	}
	
	GameMessage::Type msgType = GameMessage::MSG_INVALID;

	// Then we should determine if the game currently prefers selection events. If it does, then return
	// the invalid message.
	if (obj) {
		if (obj->isLocallyControlled() && TheInGameUI->isInPreferSelectionMode()) {
			return msgType;
		}
	}

	// Kris: Now that we can select non-controllable units/structures, don't allow any actions to be performed.
	const CommandButton *command = TheInGameUI->getGUICommand();
	if( TheInGameUI->areSelectedObjectsControllable() 
			|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT))
	{
		GameMessage *hintMessage;

		// shift used to turn every click into a point on the path.  Now whatever the click orders - an
		// attack, a capture, a ride in a transport - goes on the end of the units' list, and the logic
		// keeps it there (OrderQueue.h).  A click that orders nothing is dropped by the logic with it
		if( TheInGameUI->isInWaypointMode() && type == DO_COMMAND )
			TheInGameUI->markNextOrderQueued( ORDER_QUEUE_APPEND );

		CanAttackResult result;

		if(command && 
			(command->isContextCommand() 
				|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER
				|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT))
		{
			if( obj && obj->isKindOf( KINDOF_SHRUBBERY ) && !BitTest( command->getOptions(), ALLOW_SHRUBBERY_TARGET ) )
			{
				//If our object is a shrubbery, and we don't allow targetting it... then null it out.
				//Nulling out the draw and obj pointer will force the remainder of this code to evaluate 
				//a position interaction.
				draw = NULL;
				obj = NULL;
			}

			if( obj && obj->isKindOf( KINDOF_MINE ) && !BitTest( command->getOptions(), ALLOW_MINE_TARGET ) )
			{
				//If our object is a mine, and we don't allow targetting it... then null it out.
				//Nulling out the draw and obj pointer will force the remainder of this code to evaluate 
				//a position interaction.
				draw = NULL;
				obj = NULL;
			}
			
			//Kris: September 27, 2002
			//Added relationship tests to make sure we're not attempting a context-command on a restricted relationship.
			//This case prevents rebels from using tranq darts on allies.
			if( obj && BitTest( command->getOptions(), COMMAND_OPTION_NEED_OBJECT_TARGET ) )
			{
				// a building out of sight is judged on who held it when it was last seen, the way
				// the ActionManager judges the order itself
				const Player *localPlayer = ThePlayerList->getLocalPlayer();
				const ObjectSeenState *seen = obj->getSeenStateFor( localPlayer->getPlayerIndex() );
				const Team *seenTeam = seen ? TheTeamFactory->findTeamByID( seen->teamID ) : NULL;
				Relationship relationship = localPlayer->getRelationship( seenTeam ? seenTeam : obj->getTeam() );
				switch( relationship )
				{
					case ALLIES:
						if( !BitTest( command->getOptions(), NEED_TARGET_ALLY_OBJECT ) )
						{
							draw = NULL;
							obj = NULL;
						}	
						break;
					case ENEMIES:
						if( !BitTest( command->getOptions(), NEED_TARGET_ENEMY_OBJECT ) )
						{
							draw = NULL;
							obj = NULL;
						}	
						break;
					case NEUTRAL:
						if( !BitTest( command->getOptions(), NEED_TARGET_NEUTRAL_OBJECT ) )
						{
							draw = NULL;
							obj = NULL;
						}	
						break;
				}
			}

			Bool currentlyValid = FALSE;
			ObjectID objectID = obj ? obj->getID() : INVALID_ID;
			switch( command->getCommandType() )
			{
				//Kris: June 06, 2002
				//This is a GUI command button that triggers a mode. In any of these modes, only one specific action 
				//can occur. If the mouse isn't over a valid target, then the conditions aren't met and the code will
				//cause an invalid version of the cursor to be shown -- and should the user click, the action won't take place.
				case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
					currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB, obj, InGameUI::SELECTION_ANY );
					break;
				case GUICOMMANDMODE_HIJACK_VEHICLE:
					currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_HIJACK_VEHICLE, obj, InGameUI::SELECTION_ANY );
					break;
				case GUICOMMANDMODE_SABOTAGE_BUILDING:
					currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SABOTAGE_BUILDING, obj, InGameUI::SELECTION_ANY );
					break;
#ifdef ALLOW_SURRENDER
				case GUICOMMANDMODE_PICK_UP_PRISONER:
					currentlyValid = TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_PICK_UP_PRISONER, obj, InGameUI::SELECTION_ANY );
					break;
#endif
				case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
				{
					Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
					if( unit )
						currentlyValid = TheInGameUI->canSelectedObjectsDoSpecialPower( command, obj, pos, InGameUI::SELECTION_ANY, command->getOptions(), unit );
					else
						currentlyValid = false;
					break;
				}
				case GUI_COMMAND_SPECIAL_POWER:
					currentlyValid = TheInGameUI->canSelectedObjectsDoSpecialPower( command, obj, pos, InGameUI::SELECTION_ANY, command->getOptions(), NULL );
					break;
				case GUI_COMMAND_FIRE_WEAPON:
					currentlyValid = TheInGameUI->canSelectedObjectsEffectivelyUseWeapon( command, obj, pos, InGameUI::SELECTION_ANY );
					break;
				case GUI_COMMAND_COMBATDROP:
					currentlyValid = !obj ? TRUE : TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_COMBATDROP_INTO, obj, InGameUI::SELECTION_ANY );
					break;
			}

			if( currentlyValid )
			{
				if( type == DO_COMMAND || type == EVALUATE_ONLY )
				{
					switch( command->getCommandType() )
					{
						case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
						case GUICOMMANDMODE_HIJACK_VEHICLE:
						case GUICOMMANDMODE_SABOTAGE_BUILDING:
							msgType = createEnterMessage( draw, type );
							break;
#ifdef ALLOW_SURRENDER
						case GUICOMMANDMODE_PICK_UP_PRISONER:
							msgType = issueAttackCommand( draw, type, command->getCommandType() );
							break;
#endif
						case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
						{
							Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
							if( unit )
								msgType = issueSpecialPowerCommand( command, type, draw, pos, unit );
							break;
						}
						case GUI_COMMAND_SPECIAL_POWER://lorenzen
							msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
							break;
						case GUI_COMMAND_FIRE_WEAPON:
							msgType = issueFireWeaponCommand( command, type, draw, pos );
							break;
						case GUI_COMMAND_COMBATDROP:
							msgType = issueCombatDropCommand( command, type, draw, pos );
							break;
					}

					// NULL out the GUI command if we're actually doing something
					if( type == DO_COMMAND )
					{
						TheInGameUI->setGUICommand( NULL );
					}

				}
				else
				{
					msgType = GameMessage::MSG_VALID_GUICOMMAND_HINT;
					hintMessage = TheMessageStream->appendMessage( msgType );
					hintMessage->appendObjectIDArgument( objectID );
				}
			}
			else	// not currently valid
			{
				msgType = GameMessage::MSG_INVALID_GUICOMMAND_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( objectID );
			}

		}	// if a special power
		else if( command && (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT
						 || command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT) )
		{
			//We're using the build placement interface to determine where to build our special power item.
			//Because of that, we only care about DO_COMMAND. The context evaluation and hint feedback system
			//is already taken care of. But what we need to do is trigger the special power to actually build
			//the object and reset the timer.
			if( type == DO_COMMAND )
			{
				switch( command->getCommandType() )
				{
					case GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT:
					{
						Object* unit = ThePlayerList->getLocalPlayer()->findMostReadyShortcutSpecialPowerOfType( command->getSpecialPowerTemplate()->getSpecialPowerType() );
						if( unit )
							msgType = issueSpecialPowerCommand( command, type, draw, pos, unit );
						break;
					}
					case GUI_COMMAND_SPECIAL_POWER://lorenzen
						msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
						break;
				}
			}
		}

		
		// ********************************************************************************************
		else if( TheInGameUI->canSelectedObjectsOverrideSpecialPowerDestination( pos, InGameUI::SELECTION_ANY, SPECIAL_INVALID ) )
		{
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// do the command
				msgType = GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION;
				if( type == DO_COMMAND )
				{
					GameMessage *gameMsg = TheMessageStream->appendMessage( msgType );

					gameMsg->appendLocationArgument( *pos );
					gameMsg->appendIntegerArgument( SPECIAL_INVALID );
					gameMsg->appendObjectIDArgument( INVALID_ID );	// no specific source

				}  // end if

			}  // end if
			else
			{

				// generate a hint message
				msgType = GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );

			}  // end else
		}
		
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_RESUME_CONSTRUCTION, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// do the command
				msgType = GameMessage::MSG_RESUME_CONSTRUCTION;
				if( type == DO_COMMAND )
				{
					GameMessage *resumeMsg = TheMessageStream->appendMessage( msgType );

					resumeMsg->appendObjectIDArgument( obj->getID() );

					pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_RESUME_CONSTRUCTION );

				}  // end if

			}  // end if
			else
			{

				// generate a hint message
				msgType = GameMessage::MSG_RESUME_CONSTRUCTION_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DOCK_AT, 
																											obj, 
																											InGameUI::SELECTION_ALL ) )
		{

			//
			// The actual logic is simply to AIUpdate::dock with the target, the hint is the
			// only part that needs to be more specific.
			//
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// Give the dock command
				msgType = GameMessage::MSG_DOCK;
				if( type == DO_COMMAND )
				{
					GameMessage *dockMsg = TheMessageStream->appendMessage( msgType );

					dockMsg->appendObjectIDArgument( obj->getID() );

 					// only make sounds if we really did the command messages
 					pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DOCK);
				}  // end if

			}
			else
			{

				// make the hint
				msgType = GameMessage::MSG_DOCK_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}

		}
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_REPAIR_OBJECT, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// do the command
				msgType = GameMessage::MSG_DO_REPAIR;
				if( type == DO_COMMAND )
				{
					GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

					healMsg->appendObjectIDArgument( obj->getID() );
			
					pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_DO_REPAIR );

				}  // end if

			}  // end if
			else
			{

				// generate a hint message
				msgType = GameMessage::MSG_DO_REPAIR_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_GET_REPAIRED_AT, obj, InGameUI::SELECTION_ANY ) )
		{

			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// do the command
				msgType = GameMessage::MSG_GET_REPAIRED;
				if( type == DO_COMMAND )
				{
					GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

					healMsg->appendObjectIDArgument( obj->getID() );

					
					pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_GET_REPAIRED );


				}  // end if

			}  // end if
			else
			{

				// generate a hint message
				msgType = GameMessage::MSG_GET_REPAIRED_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_GET_HEALED_AT, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// do the command
				msgType = GameMessage::MSG_GET_HEALED;
				if( type == DO_COMMAND )
				{
					GameMessage *healMsg = TheMessageStream->appendMessage( msgType );

					healMsg->appendObjectIDArgument( obj->getID() );
				
					pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_GET_HEALED );

				}  // end if

			}  // end if
			else
			{

				// generate hint message
				msgType = GameMessage::MSG_GET_HEALED_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType);
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && draw->getObject() && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_HIJACK_VEHICLE, 
																											draw->getObject(), 
																											InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// Now, this just tricks the AI  into making the hijacker run towards the target vehicle
        // I must add a test to keep him from actually entering an enemy vehicle (contained)... Lorenzen
        msgType = createEnterMessage( draw, type );

			}  // end if
			else
			{
				
				msgType = GameMessage::MSG_HIJACK_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( draw->getObject()->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// issue the command (convert to carbomb is nearly identical to enter)
				msgType = createEnterMessage( draw, type );

			}  // end if
			else
			{
				
				msgType = GameMessage::MSG_CONVERT_TO_CARBOMB_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else
		}
		// ********************************************************************************************
		else if( draw && draw->getObject() && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SABOTAGE_BUILDING, 
																											draw->getObject(), 
																											InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
        msgType = createEnterMessage( draw, type );
			}  // end if
			else
			{
				msgType = GameMessage::MSG_SABOTAGE_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( draw->getObject()->getID() );
			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && canSelectionSalvage(obj) )
		{
			GameMessage *msg;
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY ) {
				msgType = GameMessage::MSG_DO_SALVAGE;
				if (type == DO_COMMAND) {
					msg = TheMessageStream->appendMessage(msgType);
					msg->appendLocationArgument(*obj->getPosition());
					pickAndPlayUnitVoiceResponse(TheInGameUI->getAllSelectedDrawables(), msgType);
				} 

			} else {
				msgType = GameMessage::MSG_DO_SALVAGE_HINT;
				msg = TheMessageStream->appendMessage(msgType);
				msg->appendLocationArgument(*obj->getPosition());
			} 

		}  // end else if
		// ********************************************************************************************
		else if( draw && !isForceAttackTargeting() && 
						 TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_ENTER_OBJECT, obj, InGameUI::SELECTION_ANY, true ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// issue the command
				msgType = createEnterMessage( draw, type );

			}  // end if
			else
			{

				msgType = GameMessage::MSG_ENTER_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );
			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && (result = TheInGameUI->getCanSelectedObjectsAttack( InGameUI::ACTIONTYPE_ATTACK_OBJECT, obj, InGameUI::SELECTION_ANY, isForceAttackTargeting() )) == ATTACKRESULT_POSSIBLE )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				
				// issue the attack order			
				msgType = issueAttackCommand( draw, type );

			}  // end if
			else
			{

				// Generate an Attack hint
				msgType = GameMessage::MSG_DO_ATTACK_OBJECT_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
		// ********************************************************************************************
		else if( draw && result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
		{
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				
				// issue the attack order			
				msgType = issueAttackCommand( draw, type );

			}  // end if
			else
			{

				// Generate an Attack hint
				msgType = GameMessage::MSG_DO_ATTACK_OBJECT_AFTER_MOVING_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}
		// ********************************************************************************************
		else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_CAPTURE_BUILDING, obj, InGameUI::SELECTION_ANY ) )
		{
			
			//@TODO: Kris
			//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
			Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
			const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
			if( set )
			{
				for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
				{
					// get command button
					const CommandButton *command = set->getCommandButton(i);
					if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
					{
						SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
						if( type == DO_COMMAND || type == EVALUATE_ONLY )
						{
							if( spType == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING ||
									spType == SPECIAL_INFANTRY_CAPTURE_BUILDING )
							{
								//Issue the capture building command
								msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
								break;
							}
						}
						else if( spType == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING )
						{
							//Issue the black lotus hack hint for capturing a building.
							msgType = GameMessage::MSG_HACK_HINT; 
							hintMessage = TheMessageStream->appendMessage( msgType );
							hintMessage->appendObjectIDArgument( obj->getID() );
						}
						else if( spType == SPECIAL_INFANTRY_CAPTURE_BUILDING )
						{
							//Issue the infantry hint for capturing a building
							msgType = GameMessage::MSG_CAPTUREBUILDING_HINT;
							hintMessage = TheMessageStream->appendMessage( msgType );
							hintMessage->appendObjectIDArgument( obj->getID() );
						}
					}
				}
			}
		}  // end else if
		// ********************************************************************************************
		else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DISABLE_VEHICLE_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				//@TODO: Kris
				//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
				Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
				const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
				if( set )
				{
					for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
					{
						// get command button
						const CommandButton *command = set->getCommandButton(i);
						if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
						{
							SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
							if( spType == SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK )
							{
								msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
								break;
							}
						}
					}
				}
			}  
			else
			{
				msgType = GameMessage::MSG_HACK_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );
			}  

		}  // end else if
		// ********************************************************************************************
		else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_STEAL_CASH_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				//@TODO: Kris
				//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
				Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
				const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
				if( set )
				{
					for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
					{
						// get command button
						const CommandButton *command = set->getCommandButton(i);
						if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
						{
							SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
							if( spType == SPECIAL_BLACKLOTUS_STEAL_CASH_HACK )
							{
								msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
								break;
							}
						}
					}
				}
			}  // end if
			else
			{
				msgType = GameMessage::MSG_HACK_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );
			}  

		}  // end else if
		// ********************************************************************************************
		else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_DISABLE_BUILDING_VIA_HACKING, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				//@TODO: Kris
				//PRELIMINARY CODE FOR HOOKING IN AUTO SPECIALS --- WILL BE REDONE!
				Object *source = TheInGameUI->getFirstSelectedDrawable()->getObject();
				const CommandSet *set = TheControlBar->findCommandSet( source->getCommandSetString() );
				if( set )
				{
					for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
					{
						// get command button
						const CommandButton *command = set->getCommandButton(i);
						if( command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER )
						{
							SpecialPowerType spType = command->getSpecialPowerTemplate()->getSpecialPowerType();
							if( spType == SPECIAL_HACKER_DISABLE_BUILDING )
							{
								msgType = issueSpecialPowerCommand( command, type, draw, pos, NULL );
								break;
							}
						}
					}
				}
			}  // end if
			else
			{
				msgType = GameMessage::MSG_HACK_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );
			}  
		}  // end else if
#ifdef ALLOW_SURRENDER
		// ********************************************************************************************
		else if( draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_PICK_UP_PRISONER, obj, InGameUI::SELECTION_ANY ) )
		{
			
			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{

				// issue the command 
				msgType = issueAttackCommand( draw, type, GUICOMMANDMODE_PICK_UP_PRISONER );
				
			}  // end if
			else
			{
				
				msgType = GameMessage::MSG_PICK_UP_PRISONER_HINT;
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendObjectIDArgument( obj->getID() );

			}  // end else

		}  // end else if
#endif
		// ********************************************************************************************
		else if ( pos && !draw && TheInGameUI->canSelectedObjectsDoAction( InGameUI::ACTIONTYPE_SET_RALLY_POINT, NULL, InGameUI::SELECTION_ALL, FALSE ))
		{
			msgType = GameMessage::MSG_SET_RALLY_POINT;

			if (type == DO_COMMAND) {
				const DrawableList *allSelectedDrawables = TheInGameUI->getAllSelectedDrawables();
				
				for (DrawableList::const_iterator it = allSelectedDrawables->begin(); it != allSelectedDrawables->end(); ++it) {
					Drawable *draw = (*it);
					if (draw && draw->getObject()) {
						GameMessage *newMsg = TheMessageStream->appendMessage(msgType);
						newMsg->appendObjectIDArgument(draw->getObject()->getID());
						newMsg->appendLocationArgument(*pos);
					}
				}
			} else if (type == DO_HINT) {
				msgType = GameMessage::MSG_SET_RALLY_POINT_HINT;
				hintMessage = TheMessageStream->appendMessage(msgType);
				hintMessage->appendLocationArgument(*pos);
			}
		}

		// ********************************************************************************************
		else if( draw && result == ATTACKRESULT_INVALID_SHOT )
		{
			msgType = GameMessage::MSG_IMPOSSIBLE_ATTACK_HINT;
			hintMessage = TheMessageStream->appendMessage( msgType );
			hintMessage->appendLocationArgument( *pos );
		}  // end else if

		// ********************************************************************************************
		else
		{

			//
			// NOTE: If you change this command evaluation function in what it will do
			// if there is nothing picked ... you might want to edit the logic of the
			// selection translator in that you can only select objects if there is
			// no "interesting" command to do with the picked drawable ... which is determined
			// by what we return in this function by default
			//

			//Before we issue a move order or hint, check to see if we can even move there!
			Bool validQuickPath = FALSE;
			// Make sure to only to the check if the shroud is CLEARED.  If it is fogged or shrouded, SKIP THE CHECK.  jba [3/11/2003]
			if( ThePartitionManager->getShroudStatusForPlayer( ThePlayerList->getLocalPlayer()->getPlayerIndex(), pos ) != CELLSHROUD_CLEAR )
			{
				//If it's in the shroud, pretend we can move there -- skip the check.
				validQuickPath = TRUE;
			}
			else
			{
				//Can we path there?
				const DrawableList *allSelectedDrawables = TheInGameUI->getAllSelectedDrawables();
				for( DrawableList::const_iterator it = allSelectedDrawables->begin(); it != allSelectedDrawables->end(); ++it ) 
				{
					Object *obj = (*it) ? (*it)->getObject() : NULL;
					AIUpdateInterface *ai = obj ? obj->getAI() : NULL;
					if( ai )
          {
            if ( ai->isQuickPathAvailable( pos ) ) 
					  { 
						  validQuickPath = TRUE;
						  break;
					  }
            // Wait! there are some units that CAN moveTo positions that Quickpath will reject,
            // namely, Colonel Burton and the CombatBike. Both have CLIFF locomotors.
            // We must detect whether the position is valid for these, before just invalidating the cursor,
            // out of hand.
            if ( ai->hasLocomotorForSurface( LOCOMOTORSURFACE_CLIFF ) )
            {
              if ( TheTerrainLogic->isCliffCell( pos->x, pos->y ) )
              {
						    validQuickPath = TRUE;// yeah, not really quick, but you know...
						    break;
              }
            }
          }


				}
			}

			if( type == DO_COMMAND || type == EVALUATE_ONLY )
			{
				// issue command
				// Note: If draw is valid, then its one of ours and we don't have something more specific
				// to do. Therefore, lets not issue a move command, and instead we'll return that there
				// wasn't a command for us to perform.  Under shift it is a point on the path all the same.

				if ( draw == NULL || TheInGameUI->isInWaypointMode() )
					msgType = issueMoveToLocationCommand( pos, drawableInWay, type );
			}  // end if
			else
			{
				if( !validQuickPath )
				{
					msgType = GameMessage::MSG_DO_INVALID_HINT;
				}
				else if( TheInGameUI->isInWaypointMode() )
				{
					//Waypoint mode
					msgType = GameMessage::MSG_ADD_WAYPOINT_HINT;
				}
				else if( TheInGameUI->isInAttackMoveToMode() )
				{
					//THIS CODE WILL NEVER EVER GET CALLED! -- it's a context command now (READ: rip code out)
					//Attack move
					msgType = GameMessage::MSG_DO_ATTACKMOVETO_HINT;
				}
				else
				{
					//Normal and forced move.
					msgType = GameMessage::MSG_DO_MOVETO_HINT;
				}
				hintMessage = TheMessageStream->appendMessage( msgType );
				hintMessage->appendLocationArgument( *pos );

			}  // end else

		}  // end else

	}  // end if

	// return the message type
	return msgType;

}  // end evaluateContextCommand


//-------------------------------------------------------------------------------------------------
/** Single-player test hook: make this player the one at the keyboard.  Any AI behind it is thrown
  * away first, so nothing fights you for its units, and the HUD, money and shroud become its own.
  * Returns FALSE for a player you cannot sensibly play: yourself, the neutral side, an observer,
  * a beaten player - and for anything that is not a single-player game. */
//-------------------------------------------------------------------------------------------------
Bool takeControlOfPlayer( Player *p )
{
	if (p == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInReplayGame())
		return FALSE;
	if (p == ThePlayerList->getLocalPlayer() || p == ThePlayerList->getNeutralPlayer())
		return FALSE;
	if (!p->isPlayerActive() || !p->isPlayableSide())
		return FALSE;

	/* Throwing away the AIPlayer and moving the keyboard seat are the logic's to do, on the frame the
		 message runs, so a replay does them too.  Called from here they happened on this machine only
		 and no recording of the match played back.  The message goes out as the player being left. */
	GameMessage *takeOver = TheMessageStream->appendMessage( GameMessage::MSG_CHEAT );
	takeOver->appendIntegerArgument( CHEAT_TAKE_CONTROL );
	takeOver->appendIntegerArgument( p->getPlayerIndex() );

	ThePlayerList->setLocalPlayer(p);
	TheInGameUI->deselectAllDrawables();
	ThePartitionManager->refreshShroudForLocalPlayer();
	TheControlBar->initSpecialPowershortcutBar(p);
	TheControlBar->setControlBarSchemeByPlayer(p);
	// initSpecialPowershortcutBar only builds the bar, hidden.  Left alone it is filled in by the
	// control bar's next update and then slid in over half a second: this puts the general's
	// powers up on this frame instead.
	TheControlBar->showSpecialPowerShortcutInstantly(p);
	TheControlBar->markUIDirty();
	TheGameClient->updateFakeDrawables();
	TheInGameUI->message(UnicodeString(L"Now playing: %s"), p->getPlayerDisplayName().str());
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
//====================================================================================
/**
 * The Command Translator translates mouse events into object command messages
 * such as move_to, attack, etc.
 */
GameMessageDisposition CommandTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessage::Type t = msg->getType();
	GameMessageDisposition disp = KEEP_MESSAGE;

	// letting go of Tab puts the scoreboard away, input or no input; the press is the command map's
	// DIPLOMACY
	if (t == GameMessage::MSG_RAW_KEY_UP && msg->getArgument( 0 )->integer == KEY_TAB)
		TheInGameUI->closeScoreboard();

	// We want to always be able to get to the options menu even during no input times and a clear game data message should always go through
	if (t != GameMessage::MSG_META_OPTIONS && t != GameMessage::MSG_CLEAR_GAME_DATA &&
			!TheInGameUI->getInputEnabled() && !isSystemMessage(msg)) 
	{
		return DESTROY_MESSAGE;
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	ExtentModType extentModType = EXTENTMOD_INVALID;
	Real extentModAmount = 0.0f;
#endif

	switch (t)
	{
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_MATCHING_UNITS:
		{
			//
			// One press takes every unit of the selected types that is on the screen; pressed twice
			// it takes them across the whole map.  The same shape the category keys have - one press
			// for what you can see, a second for everything you own - and it is a shape you can lean
			// on: the wide selection is a decision, not something that happens to you because the
			// screen happened to hold nothing new.
			//
			enum { SELECT_MATCHING_AGAIN_MS = 500 };		///< real time: the client frame rate is uncapped
			static UnsignedInt s_lastMatchingMs = 0;

			const UnsignedInt now = timeGetTime();
			const Bool again = ( s_lastMatchingMs != 0
														&& now - s_lastMatchingMs <= SELECT_MATCHING_AGAIN_MS );
			s_lastMatchingMs = now;

			if( again )
				TheInGameUI->selectMatchingAcrossMap();
			else
				TheInGameUI->selectUnitsMatchingCurrentSelection();

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_NEXT_UNIT:
		{
			/* because list is prepended, iterate through backwards */
			
			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == NULL )
				break;

			// if nothing is selected
			Drawable *temp;

			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the last drawable
				for( temp = TheGameClient->firstDrawable(); temp->getNextDrawable() != NULL; temp = temp->getNextDrawable() )
				{
				}
				// temp is the last drawable
				for( temp; temp != NULL; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && !object->isKindOf( KINDOF_NO_SELECT ) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );

						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->lookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = NULL;
				Bool hack = FALSE;
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the previous selectable drawable
					temp = selectedDrawable->getPrevDrawable();
					//temp = selectedDrawable;
					for( ; temp != selectedDrawable; temp = temp->getPrevDrawable() )
					{
						if( hack == TRUE )
						{
							temp = temp->getNextDrawable();
							hack = FALSE;
						}
						// if temp is null, set it to the last drawable and loop back to selected drawable
						if( temp == NULL )
						{
							for(temp = selectedDrawable; temp->getNextDrawable() != NULL; temp = temp->getNextDrawable() )
							{
							}
							hack = TRUE;
						}
						// else search for a previous selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() && !tempObject->isKindOf( KINDOF_NO_SELECT ) )
							{
								newDrawable = temp;
								break;
								//temp = selectedDrawable; // same as break
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != NULL )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						// select the unit
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->lookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_PREV_UNIT:
		{
			/* because list is prepended, iterate through forwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == NULL )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the first drawable
				temp = TheGameClient->firstDrawable();
				for( temp; temp != NULL; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && !object->isKindOf( KINDOF_NO_SELECT ) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );
						
						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->lookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = NULL;
				TheGameClient->getDrawableList();
				Bool hack = FALSE; // takes care of when for loop skips firstdrawable
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the next selectable drawable
					temp = selectedDrawable->getNextDrawable();
					//temp = selectedDrawable;
					for( temp; temp != selectedDrawable; temp = temp->getNextDrawable() )
					{
						if( hack == TRUE )
						{
							temp = TheGameClient->firstDrawable();
							hack = FALSE;
							if( temp == selectedDrawable )
							{
								break;
							}
						}
						// if temp is null, set it to the first drawable and loop forward to selected drawable
						if( temp == NULL )
						{
							temp = TheGameClient->firstDrawable();
							hack = TRUE;
							const Object *tempObject = temp->getObject();
							// must take case of this case here or else the loop will break without getting newDrawable
							if( tempObject && temp->getNextDrawable() == selectedDrawable && !temp->isSelected() 
								&& tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() && !tempObject->isKindOf( KINDOF_NO_SELECT ) )
							{
								newDrawable = temp;
								break;
							}
						}
						// else search for a next selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && !temp->isSelected() && tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() )
							{
								newDrawable = temp;
								break;
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != NULL )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();
						// select the unit
						
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->lookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;

		}	// end select previous unit

		//-----------------------------------------------------------------------------------------
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER:
		{
			// same list the control bar's idle-worker button cycles; when every dozer and worker
			// has a job the list is empty and selectNextIdleWorker returns without doing anything.
			TheInGameUI->selectNextIdleWorker();

			disp = DESTROY_MESSAGE;
			break;
		}		// end select next idle worker

		case GameMessage::MSG_META_COMMAND_SLOT01:
		case GameMessage::MSG_META_COMMAND_SLOT02:
		case GameMessage::MSG_META_COMMAND_SLOT03:
		case GameMessage::MSG_META_COMMAND_SLOT04:
		case GameMessage::MSG_META_COMMAND_SLOT05:
		case GameMessage::MSG_META_COMMAND_SLOT06:
		case GameMessage::MSG_META_COMMAND_SLOT07:
		case GameMessage::MSG_META_COMMAND_SLOT08:
		case GameMessage::MSG_META_COMMAND_SLOT09:
		case GameMessage::MSG_META_COMMAND_SLOT10:
		case GameMessage::MSG_META_COMMAND_SLOT11:
		case GameMessage::MSG_META_COMMAND_SLOT12:
		case GameMessage::MSG_META_COMMAND_SLOT13:
		case GameMessage::MSG_META_COMMAND_SLOT14:
		{
			// a watcher has no commands to press, and the top row picks the spectator's stat instead,
			// the keys Dota's spectator uses for the same list
			const Int slot = t - GameMessage::MSG_META_COMMAND_SLOT01;
			if( !TheInGameUI->pickSpectatorStat( slot ) )
				TheControlBar->pressCommandButton( slot );
			disp = DESTROY_MESSAGE;
			break;
		}		// end command bar grid slots

		case GameMessage::MSG_META_SHORTCUT_SLOT01:
		case GameMessage::MSG_META_SHORTCUT_SLOT02:
		case GameMessage::MSG_META_SHORTCUT_SLOT03:
		case GameMessage::MSG_META_SHORTCUT_SLOT04:
		case GameMessage::MSG_META_SHORTCUT_SLOT05:
		case GameMessage::MSG_META_SHORTCUT_SLOT06:
		case GameMessage::MSG_META_SHORTCUT_SLOT07:
		case GameMessage::MSG_META_SHORTCUT_SLOT08:
		case GameMessage::MSG_META_SHORTCUT_SLOT09:
		case GameMessage::MSG_META_SHORTCUT_SLOT10:
		case GameMessage::MSG_META_SHORTCUT_SLOT11:
		{
			// the general's power shortcut bar, one key per visible button
			TheControlBar->pressSpecialPowerShortcut( t - GameMessage::MSG_META_SHORTCUT_SLOT01 );
			disp = DESTROY_MESSAGE;
			break;
		}		// end special power shortcut slots

		case GameMessage::MSG_META_SELECT_NEXT_WORKER:
		{
			/* because list is prepended, iterate through backwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == NULL )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the last drawable
				for( temp = TheGameClient->firstDrawable(); temp->getNextDrawable() != NULL; temp = temp->getNextDrawable() )
				{
				}
				// temp is the last drawable
				for( temp; temp != NULL; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					// make sure you select only workers
					else if( object && object->isLocallyControlled() && !object->isContained() && object->isKindOf(KINDOF_DOZER) )
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( object->getID() );
						TheInGameUI->selectDrawable( temp );

						// play the units sound
						AudioEventRTS soundEvent = *temp->getTemplate()->getVoiceSelect();
						soundEvent.setObjectID(object->getID());
						TheAudio->addAudioEvent( &soundEvent );

						// center on the unit
						TheTacticalView->lookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = NULL;
				Bool hack = FALSE;
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the previous selectable drawable
					temp = selectedDrawable->getPrevDrawable();
					//temp = selectedDrawable;
					for( ; temp != selectedDrawable; temp = temp->getPrevDrawable() )
					{
						if( hack == TRUE )
						{
							temp = temp->getNextDrawable();
							hack = FALSE;
						}
						// if temp is null, set it to the last drawable and loop back to selected drawable
						if( temp == NULL )
						{
							for(temp = selectedDrawable; temp->getNextDrawable() != NULL; temp = temp->getNextDrawable() )
							{
							}
							hack = TRUE;
						}
						// else search for a previous selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && tempObject->isLocallyControlled() && !tempObject->isContained() && tempObject->isKindOf( KINDOF_DOZER ) )
							{
								newDrawable = temp;
								break;
								//temp = selectedDrawable; // same as break
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != NULL )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();

						// select the unit
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so pass in value true
						teamMsg->appendBooleanArgument( TRUE );
						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );
						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->lookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}		// end select next worker

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_PREV_WORKER:
		{
			/* because list is prepended, iterate through forwards */

			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == NULL )
				break;

			Drawable *temp;
			// if nothing is selected
			if( TheInGameUI->getSelectCount() == 0 )
			{
				// get the first drawable
				temp = TheGameClient->firstDrawable();
				for( temp; temp != NULL; temp = temp->getPrevDrawable() )
				{
					const Object *object = temp->getObject();
					// if you've reached the end of the list, don't select anything
					if( !object )
					{
						break;
					}
					else if( object && object->isMobile() && object->isLocallyControlled() && !object->isContained() && object->isKindOf( KINDOF_DOZER ))
					{
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group or add to group? Passed in value is true if we are creating a new group.
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( object->getID() );
						
						TheInGameUI->selectDrawable( temp );

						// center on the unit
						TheTacticalView->lookAt(temp->getPosition());
						break;
					}
				}
			}
			else
			{
				Drawable *newDrawable = NULL;
				TheGameClient->getDrawableList();
				Bool hack = FALSE; // takes care of when for loop skips firstdrawable
				Drawable *selectedDrawable = TheInGameUI->getFirstSelectedDrawable();
				Object *selectedObject = selectedDrawable->getObject();
				if( selectedObject->isLocallyControlled() )
				{
					// find the next selectable drawable
					temp = selectedDrawable->getNextDrawable();
					//temp = selectedDrawable;
					for( temp; temp != selectedDrawable; temp = temp->getNextDrawable() )
					{
						if( hack == TRUE )
						{
							temp = TheGameClient->firstDrawable();
							hack = FALSE;
							if( temp == selectedDrawable )
							{
								break;
							}
						}
						// if temp is null, set it to the first drawable and loop forward to selected drawable
						if( temp == NULL )
						{
							temp = TheGameClient->firstDrawable();
							hack = TRUE;
							const Object *tempObject = temp->getObject();
							// must take case of this case here or else the loop will break without getting newDrawable
							if( tempObject && temp->getNextDrawable() == selectedDrawable && !temp->isSelected() 
								&& tempObject->isMobile() && tempObject->isLocallyControlled() && !tempObject->isContained() )
							{
								newDrawable = temp;
								break;
							}
						}
						// else search for a next selectable drawable
						else
						{
							const Object *tempObject = temp->getObject();
							if( tempObject && !temp->isSelected() && tempObject->isMobile()
								  && tempObject->isLocallyControlled() && !tempObject->isContained() && tempObject->isKindOf( KINDOF_DOZER ) )
							{
								newDrawable = temp;
								break;
							}
						}
					}
					//if there is another selectable unit, select it
					if(newDrawable != NULL )
					{
						//deselect other units
						TheInGameUI->deselectAllDrawables();
						// select the unit
						
						// create a new group.
						GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );

						//New group so passed in value true
						teamMsg->appendBooleanArgument( TRUE );

						teamMsg->appendObjectIDArgument( newDrawable->getObject()->getID() );

						TheInGameUI->selectDrawable( newDrawable );

						// center on the unit
						TheTacticalView->lookAt(newDrawable->getPosition());
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;

		}	// end select previous worker

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_HERO:
		{
			// if there is nothing on the screen, bail
			if( TheGameClient->firstDrawable() == NULL )
				break;

			Object *hero = iNeedAHero();

			if ( hero == NULL )
				break;

			if ( hero->isContained() )
				hero = hero->getContainedBy();

			Drawable *heroDraw = hero->getDrawable();

			if ( heroDraw == NULL )
				break;

			TheInGameUI->deselectAllDrawables();

			// create a new group.
			GameMessage *teamMsg = TheMessageStream->appendMessage( GameMessage::MSG_CREATE_SELECTED_GROUP );
			
			//New group so pass in value true
			teamMsg->appendBooleanArgument( TRUE );
			teamMsg->appendObjectIDArgument( hero->getID() );
			TheInGameUI->selectDrawable( heroDraw );

			// center on the unit
			TheTacticalView->lookAt(heroDraw->getPosition());

			disp = DESTROY_MESSAGE;
			break;
		}
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_COMMAND_CENTER:
			viewCommandCenter();
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_VIEW_LAST_RADAR_EVENT:
		{
//			Player *player = ThePlayerList->getLocalPlayer();

			// if the local player has a radar, center on last event (if any)
			// Excuse me?  You don't need radar for the spacebar.  That's silly.
//			if( TheRadar->isRadarForced() || ( TheRadar->isRadarHidden() == false && player->hasRadar() ) )
			{
				Coord3D lastEvent;

				if( TheRadar->getLastEventLoc( &lastEvent ) )
					TheTacticalView->lookAt( &lastEvent );

			}  // end if

			disp = DESTROY_MESSAGE;
			break;

		}  // end view last radar event

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SELECT_ALL:
		case GameMessage::MSG_META_SELECT_ALL_AIRCRAFT:
		case GameMessage::MSG_META_SELECT_ALL_MILITARY:
		case GameMessage::MSG_META_SELECT_ALL_VEHICLES:
		case GameMessage::MSG_META_SELECT_ALL_BARRACKS:
		case GameMessage::MSG_META_SELECT_ALL_WARFACTORIES:
		case GameMessage::MSG_META_SELECT_ALL_AIRFIELDS:
		{
			// one category per key; selectAllUnitsByType takes the screen first and the
			// whole map when the screen has nothing new left, so a second press widens
			KindOfMaskType requiredKindofs;
			KindOfMaskType disqualifyingKindofs;
			disqualifyingKindofs.set(KINDOF_IGNORES_SELECT_ALL);
			switch( t )
			{
				case GameMessage::MSG_META_SELECT_ALL_BARRACKS:     requiredKindofs.set(KINDOF_FS_BARRACKS);   break;
				case GameMessage::MSG_META_SELECT_ALL_WARFACTORIES: requiredKindofs.set(KINDOF_FS_WARFACTORY); break;
				case GameMessage::MSG_META_SELECT_ALL_AIRFIELDS:    requiredKindofs.set(KINDOF_FS_AIRFIELD);   break;
				default:
					// units: never dozers, harvesters or structures
					disqualifyingKindofs.set(KINDOF_DOZER);
					disqualifyingKindofs.set(KINDOF_HARVESTER);
					disqualifyingKindofs.set(KINDOF_STRUCTURE);
					if( t == GameMessage::MSG_META_SELECT_ALL_AIRCRAFT )
						requiredKindofs.set(KINDOF_AIRCRAFT);
					else if( t == GameMessage::MSG_META_SELECT_ALL_MILITARY )
					{
						//
						// Q W E under the left hand is infantry / vehicles / aircraft - one key per
						// arm, so each one narrows the selection to something you can give a single
						// order to. This used to be KINDOF_CAN_ATTACK, which is every armed unit
						// there is: it swept up the tanks and the aircraft the other two keys are
						// for and left no way to grab the foot soldiers on their own. Ctrl+A is
						// still there for everything at once.
						//
						requiredKindofs.set(KINDOF_INFANTRY);
					}
					else if( t == GameMessage::MSG_META_SELECT_ALL_VEHICLES )
					{
						requiredKindofs.set(KINDOF_VEHICLE);
						disqualifyingKindofs.set(KINDOF_AIRCRAFT);
					}
					break;
			}

			//Kris: Patch 1.03. We need to deselect all the units if any of the units we have selected
			//are incompatible with the select all type we are triggering. This is a fix for the SCUDSTORM
			//exploit. (Generalized: anything outside the category drops the old selection first.)
			const DrawableList *drawList = TheInGameUI->getAllSelectedDrawables();
			for( DrawableListCIt it = drawList->begin(); it != drawList->end(); ++it )
			{
				if( !(*it)->isKindOfMulti( requiredKindofs, disqualifyingKindofs ) )
				{
					TheInGameUI->deselectAllDrawables();
					break;
				}
			}

			TheInGameUI->selectAllUnitsByType(requiredKindofs, disqualifyingKindofs);

			disp = DESTROY_MESSAGE;
			break;

		}  // end select all

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_SCATTER:
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_DO_SCATTER);
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_STOP:
		{
			//
			// stop one structure of your own that is still going up and there is nothing to stop yet -
			// what you mean is call the whole thing off, which is what the command bar's cancel button
			// on that same structure does, refund and all.
			//
			const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
			Int selectionCount = selected ? selected->size() : 0;
			const Drawable *onlyDrawable = ( selectionCount == 1 ) ? selected->front() : NULL;
			const Object *onlyObject = onlyDrawable ? onlyDrawable->getObject() : NULL;

			if( Command_stopMeansCancelConstruction( selectionCount,
																							 onlyObject && onlyObject->isLocallyControlled(),
																							 onlyObject && onlyObject->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) ) )
				TheMessageStream->appendMessage(GameMessage::MSG_DOZER_CANCEL_CONSTRUCT);
			else
				// This message always works on the currently selected team
				TheMessageStream->appendMessage(GameMessage::MSG_DO_STOP);

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CREATE_FORMATION:
			// This message always works on the currently selected team
			TheMessageStream->appendMessage(GameMessage::MSG_CREATE_FORMATION);

			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEPLOY:
			#ifdef _DEBUG
			DEBUG_ASSERTCRASH(FALSE, ("unimplemented meta command MSG_META_DEPLOY !"));
			#endif
			/// @todo srj implement me
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_FOLLOW:
			#ifdef _DEBUG
			DEBUG_ASSERTCRASH(FALSE, ("unimplemented meta command MSG_META_FOLLOW !"));
			#endif
			/// @todo srj implement me
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		/* MDC - no such thing as chat to players right now - not until we have a diplomacy screen
		case GameMessage::MSG_META_CHAT_PLAYERS:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				ToggleInGameChat();
				SetInGameChatType( INGAME_CHAT_PLAYERS );
			}
			disp = DESTROY_MESSAGE;
			break;
		*/

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CHAT_ALLIES:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				// a watcher, or a player beaten, has no team: his line goes to everyone, and
				// ConnectionManager::processChat shows it to the watchers alone
				ToggleInGameChat();
				SetInGameChatType( ThePlayerList->getLocalPlayer()->isPlayerActive() ? INGAME_CHAT_ALLIES : INGAME_CHAT_EVERYONE );
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_CHAT_EVERYONE:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				ToggleInGameChat();
				SetInGameChatType( INGAME_CHAT_EVERYONE );
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DIPLOMACY:
			// Tab is the scoreboard in any game that has seats, up while the key is held; the diplomacy
			// screen, with its mute buttons, stays on the command bar's own button
			if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && TheGameInfo)
			{
				TheInGameUI->openScoreboard();
			}
			else if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame())
			{
				ToggleDiplomacy( FALSE );
			}
			else if( TheShell && TheShell->isShellActive() && TheGameSpyBuddyMessageQueue)
				GameSpyToggleOverlay(GSOVERLAY_BUDDY);
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_PLACE_BEACON:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive() &&
				(TheGlobalData->m_netMinPlayers==0 || TheGameInfo->isMultiPlayer()))
			{
				Int count;
				const ThingTemplate *thing = TheThingFactory->findTemplate( ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getBeaconTemplate() );
				ThePlayerList->getLocalPlayer()->countObjectsByThingTemplate( 1, &thing, false, &count );
				DEBUG_LOG(("MSG_META_PLACE_BEACON - Player already has %d beacons active\n", count));
				if (count < TheMultiplayerSettings->getMaxBeaconsPerPlayer())
				{
					const CommandButton *commandButton = TheControlBar->findCommandButton( "Command_PlaceBeacon" );
					TheInGameUI->setGUICommand( commandButton );
				}
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_REMOVE_BEACON:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame())
			{
				TheMessageStream->appendMessage( GameMessage::MSG_REMOVE_BEACON );
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		// Smoke for the allies where the cursor points: on the radar, the map spot it stands for,
		// otherwise the ground under it.
		case GameMessage::MSG_META_SIGNAL_ATTACK:
		case GameMessage::MSG_META_SIGNAL_DEFEND:
		case GameMessage::MSG_META_SIGNAL_ATTENTION:
			if (TheGameLogic->isInMultiplayerGame() && !TheGameLogic->isInReplayGame() &&
				ThePlayerList->getLocalPlayer()->isPlayerActive())
			{
				const ICoord2D *cursor = &TheMouse->getMouseStatus()->pos;
				Coord3D world;
				if( TheRadar->screenPixelToWorld( cursor, &world ) || TheTacticalView->screenToTerrain( cursor, &world ) )
				{
					GameMessage *signalMsg = TheMessageStream->appendMessage( GameMessage::MSG_PLACE_SIGNAL );
					signalMsg->appendLocationArgument( world );
					signalMsg->appendIntegerArgument( Command_signalKindForMeta( t ) );
				}
			}
			disp = DESTROY_MESSAGE;
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_OPTIONS:
		{

			//
			// Escape is "take that back" before it is "open the menu": a structure waiting to be
			// placed, a targeting command armed off the command bar, or a half-typed builder chord
			// are all dropped first, and the pause menu only comes up on a press with nothing
			// pending.
			//
			Bool cancelledSomething = FALSE;

			// the general's promotion screen closes on escape like every other overlay in the game
			if( TheControlBar && TheControlBar->isPurchaseScienceVisible() )
			{
				TheControlBar->hidePurchaseScience();
				cancelledSomething = TRUE;
			}

			if( TheControlBar && TheControlBar->isChordArmed() )
			{
				TheControlBar->dropChord();
				cancelledSomething = TRUE;
			}

			// a general's power row picked but not yet spent goes the same way
			if( TheControlBar && TheControlBar->clearSpecialPowerShortcutRow() )
				cancelledSomething = TRUE;

			if( TheInGameUI->getPendingPlaceType() != NULL )
			{
				TheInGameUI->placeBuildAvailable( NULL, NULL );
				cancelledSomething = TRUE;
			}

			if( TheInGameUI->getGUICommand() != NULL )
			{
				TheInGameUI->setGUICommand( NULL );
				cancelledSomething = TRUE;
			}

			if( TheInGameUI->isSignalArmed() )
			{
				TheInGameUI->disarmSignal();
				cancelledSomething = TRUE;
			}

			if( cancelledSomething == FALSE )
				ToggleQuitMenu();

			disp = DESTROY_MESSAGE;
			break;
		}

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_LOWER_DETAILS:
		{
			if (TheGlobalData)
			{
				static Bool isLowDetails = FALSE;
				static Bool oldShadowVolumsValue = TRUE;
				static Bool oldLightMapValue = TRUE;
				static Bool oldCloudMap = TRUE;
				static Bool oldBehindBuildingMarkers = TRUE;
				static Int oldMaxParticleCount = 0;
				if(isLowDetails)
				{
					TheWritableGlobalData->m_useShadowVolumes = oldShadowVolumsValue;
					TheWritableGlobalData->m_useLightMap = oldLightMapValue;
					TheWritableGlobalData->m_useCloudMap = oldCloudMap;
					TheWritableGlobalData->m_maxParticleCount = oldMaxParticleCount;
					TheGameLogic->setShowBehindBuildingMarkers(oldBehindBuildingMarkers);
					if(TheInGameUI)
						TheInGameUI->message("GUI:ReturnGraphicsToPreviousSettings");
				}
				else
				{
					oldShadowVolumsValue = TheGlobalData->m_useShadowVolumes;
					TheWritableGlobalData->m_useShadowVolumes = FALSE;

					oldLightMapValue = TheGlobalData->m_useLightMap;
					TheWritableGlobalData->m_useLightMap = FALSE;

					oldCloudMap = TheGlobalData->m_useCloudMap;
					TheWritableGlobalData->m_useCloudMap = FALSE;
					
					oldBehindBuildingMarkers = TheGameLogic->getShowBehindBuildingMarkers();
					TheGameLogic->setShowBehindBuildingMarkers(FALSE);

					oldMaxParticleCount = TheGlobalData->m_maxParticleCount;
					TheWritableGlobalData->m_maxParticleCount = DROPPED_MAX_PARTICLE_COUNT;
					
					if(TheInGameUI)
						TheInGameUI->message("GUI:DetailsSetToLowest");
				}
			}  
			disp = DESTROY_MESSAGE;
			break;
		}  

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_CONTROL_BAR:
		{
			if(TheShell->isShellActive())
			{
				WindowLayout *win = TheShell->top();
				if(win)
				{
					win->hide(!win->isHidden());
				}

			}
			else
			{
				if (!(TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK))
				{
					Bool hide = false;
					if (TheWindowManager)
					{
						Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
						GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);

						if (window)
							hide = !window->winIsHidden();
					}

					ToggleControlBar();
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_ATTACKMOVE:
			TheInGameUI->toggleAttackMoveToMode( );
			break;

		//-----------------------------------------------------------------------------------------
		// the attack key, which arms force fire the same way the key above arms an attack move
		case GameMessage::MSG_META_TOGGLE_FORCEATTACK:
			TheInGameUI->toggleForceAttackArmed( );
			break;

		//-----------------------------------------------------------------------------------------
		// the guard key, armed the same way again: the next order click posts the selection where it
		// is pointed instead of walking them there and leaving them idle
		case GameMessage::MSG_META_TOGGLE_GUARD:
			TheInGameUI->toggleGuardArmed( );
			break;

		//-----------------------------------------------------------------------------------------
		// the general's promotion screen. It is one click away on the stars button and nowhere on
		// the keyboard, which is the wrong way round for something you open the moment a promotion
		// lands - and the star only flashes until you look at it.
		case GameMessage::MSG_META_TOGGLE_PURCHASE_SCIENCE:
			if( TheControlBar )
				TheControlBar->togglePurchaseScience();
			disp = DESTROY_MESSAGE;
			break;

		//
		// Pause and game speed. Both are local-only: they change how fast THIS machine steps its
		// logic, so they are refused outright in a network game where every peer has to agree.
		// Replays and single player are fair game.
		//
		case GameMessage::MSG_META_TOGGLE_PAUSE:
		{
			if( TheGameLogic && !TheGameLogic->isInMultiplayerGame() )
			{
				Bool paused = !TheGameLogic->isGamePaused();
				TheGameLogic->setGamePaused( paused );
				// a paused replay is still watched: the camera moves and the strip's play button takes
				// its click, and a replay has no orders for the input lock to hold back
				if( paused && TheGameLogic->isInReplayGame() )
					TheInGameUI->setInputEnabled( TRUE );
				TheInGameUI->message( paused ? "GUI:GamePaused" : "GUI:GameResumed" );
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//
		// Hold position: every selected unit guards the spot it is standing on and does not
		// pursue. The location has to be resolved per unit on the logic side, so this carries
		// only the guard mode - see MSG_DO_HOLD_POSITION in GameLogicDispatch.
		//
		case GameMessage::MSG_META_HOLD_POSITION:
		{
			if( TheInGameUI->getSelectCount() > 0 )
			{
				GameMessage *holdMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_HOLD_POSITION );
				holdMsg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		case GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_LEFT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraRotatingLeft(), ("Setting rotate camera left, but it's already set!"));
			TheInGameUI->setCameraRotateLeft( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ROTATE_LEFT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraRotatingLeft(), ("Clearing rotate camera left, but it's already clear!"));
			TheInGameUI->setCameraRotateLeft( false );
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ROTATE_RIGHT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraRotatingRight(), ("Setting rotate camera right, but it's already set!"));
			TheInGameUI->setCameraRotateRight( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ROTATE_RIGHT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraRotatingRight(), ("Clearing rotate camera right, but it's already clear!"));
			TheInGameUI->setCameraRotateRight( false );
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_IN:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraZoomingIn(), ("Setting zoom camera in, but it's already set!"));
			TheInGameUI->setCameraZoomIn( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ZOOM_IN:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraZoomingIn(), ("Clearing zoom camera in, but it's already clear!"));
			TheInGameUI->setCameraZoomIn( false );
			break;
		case GameMessage::MSG_META_BEGIN_CAMERA_ZOOM_OUT:
			DEBUG_ASSERTCRASH(!TheInGameUI->isCameraZoomingOut(), ("Setting zoom camera out, but it's already set!"));
			TheInGameUI->setCameraZoomOut( true );
			break;
		case GameMessage::MSG_META_END_CAMERA_ZOOM_OUT:
			DEBUG_ASSERTCRASH(TheInGameUI->isCameraZoomingOut(), ("Clearing zoom camera out, but it's already clear!"));
			TheInGameUI->setCameraZoomOut( false );
			break;
		case GameMessage::MSG_META_CAMERA_RESET:
			TheInGameUI->resetCamera();
			break;
		case GameMessage::MSG_META_TOGGLE_CAMERA_TRACKING_DRAWABLE:
			TheInGameUI->setCameraTrackingDrawable( true );
			break;
        //--------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY:
		{
			if( TheGlobalData )
			{
				#if !defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)//may be defined in GameCommon.h
				if (TheGameLogic->isInReplayGame())
				#endif
				{
					TheWritableGlobalData->m_TiVOFastMode = 1 - TheGlobalData->m_TiVOFastMode;
					TheInGameUI->message( UnicodeString( L"m_TiVOFastMode: %s" ),
																TheGlobalData->m_TiVOFastMode ? L"ON" : L"OFF" );
				}
			}  // end if

			disp = DESTROY_MESSAGE;
			break;

		}  // end toggle special power delays

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)//may be defined in GameCommon.h
    case GameMessage::MSG_CHEAT_RUNSCRIPT1:
    case GameMessage::MSG_CHEAT_RUNSCRIPT2:      
    case GameMessage::MSG_CHEAT_RUNSCRIPT3:
    case GameMessage::MSG_CHEAT_RUNSCRIPT4:
    case GameMessage::MSG_CHEAT_RUNSCRIPT5:
    case GameMessage::MSG_CHEAT_RUNSCRIPT6:
    case GameMessage::MSG_CHEAT_RUNSCRIPT7:
    case GameMessage::MSG_CHEAT_RUNSCRIPT8:
    case GameMessage::MSG_CHEAT_RUNSCRIPT9:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if( TheScriptEngine ) 
				{
					Int script = t - GameMessage::MSG_CHEAT_RUNSCRIPT1 + 1;
					AsciiString scriptName;
					scriptName.format("KEY_F%d", script);
					TheScriptEngine->runScript(scriptName);
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_TOGGLE_SPECIAL_POWER_DELAYS:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if( TheGlobalData )
				{

					TheWritableGlobalData->m_specialPowerUsesDelay = 1 - TheGlobalData->m_specialPowerUsesDelay;
					TheInGameUI->message( UnicodeString( L"Special Power (Superweapon) Delay: %s" ),
																TheGlobalData->m_specialPowerUsesDelay ? L"ON" : L"OFF" );

				}  // end if

				disp = DESTROY_MESSAGE;
			}
			break;

		}  // end toggle special power delays
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_SWITCH_TEAMS:							
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				if (TheGameLogic->isInGame())
				{
					Int idx = -1;
					for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
					{
						if (ThePlayerList->getNthPlayer(i) == ThePlayerList->getLocalPlayer())
						{
							idx = i;
							break;
						}
					}
					//
					// The local player is not always one of the players: watching a replay from the
					// observer seat is the ordinary way to get here that way.  idx then kept whatever
					// was on the stack and went straight into getNthPlayer as an index.  Start at the
					// first player instead, which is where the cycle would have wrapped to anyway.
					//
					if (idx < 0)
						idx = 0;
					Int idxOrig = idx;
					do 
					{
						++idx;
						if (idx >= ThePlayerList->getPlayerCount())
							idx = 0;

						if (idx == idxOrig)
							break;

					} while (ThePlayerList->getNthPlayer(idx) == ThePlayerList->getNeutralPlayer());
					
					ThePlayerList->setLocalPlayer(ThePlayerList->getNthPlayer(idx));
					TheInGameUI->deselectAllDrawables();
					// the fog you are handed has to be the fog of the player you just became
					TheGhostObjectManager->setLocalPlayerIndex(idx);
					ThePartitionManager->refreshShroudForLocalPlayer();
					TheControlBar->initSpecialPowershortcutBar(ThePlayerList->getLocalPlayer());
					TheControlBar->setControlBarSchemeByPlayer(ThePlayerList->getLocalPlayer());
					TheGameClient->updateFakeDrawables();
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		} 
    //--------------------------------------------------------------------------------------
    case GameMessage::MSG_CHEAT_KILL_SELECTION:						
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				// THIS CALLS THE STANDARD DEBUG MESSAGE, WHICH IS CALLED:
				TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_SELECTION );
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_INSTANT_BUILD:						
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				// Doesn't make a valid network message
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				localPlayer->toggleInstantBuild();
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_ADD_CASH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Money *money = localPlayer->getMoney();
				money->deposit( 10000 );
				// consumed here, like every cheat around it.  Without this the message went on down
				// the translator chain and onto the command list, where nothing acts on it.
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_GIVE_ALL_SCIENCES:				
		{ 
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *player = ThePlayerList->getLocalPlayer();
				if (player)
				{ 
					// cheese festival: do NOT imitate this code. it is for debug purposes only.
					std::vector<AsciiString> v = TheScienceStore->friend_getScienceNames();
					for (int i = 0; i < v.size(); ++i) 
					{
						ScienceType st = TheScienceStore->getScienceFromInternalName(v[i]);
						if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st))
						{
							player->grantScience(st);
						}
					}
				}
				TheInGameUI->message( UnicodeString( L"Granting all sciences!" ));
				disp = DESTROY_MESSAGE;
			}
			break;
		}
    case GameMessage::MSG_CHEAT_GIVE_SCIENCEPURCHASEPOINTS:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *player = ThePlayerList->getLocalPlayer();
				if (player)
					player->addSciencePurchasePoints(1);
				TheInGameUI->message( UnicodeString( L"Adding a SciencePurchasePoint" ));
				disp = DESTROY_MESSAGE;
			}
			break;
		}
		case GameMessage::MSG_CHEAT_SHOW_HEALTH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				TheWritableGlobalData->m_showObjectHealth = 1 - TheGlobalData->m_showObjectHealth;
				TheInGameUI->message( UnicodeString( L"Object Health %s" ),
															TheGlobalData->m_showObjectHealth ? L"ON" : L"OFF" );
			}
			break;
	
		}
		case GameMessage::MSG_CHEAT_TOGGLE_MESSAGE_TEXT:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{

				// toggle the message state
				TheInGameUI->toggleMessages();

				// when messages get turned on, display a message
				if( TheInGameUI->isMessagesOn() )
					TheInGameUI->message( TheGameText->fetch( "GUI:MessagesOn" ) );

				disp = DESTROY_MESSAGE;
			}
			break;

		}  // end clear message text


#endif

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_FORCEMOVE:
			DEBUG_ASSERTCRASH(!TheInGameUI->isInForceMoveToMode(), ("forceMoveToMode mismatch"));
			TheInGameUI->setForceMoveMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_FORCEMOVE:
			DEBUG_ASSERTCRASH(TheInGameUI->isInForceMoveToMode(), ("forceMoveToMode mismatch"));
			TheInGameUI->setForceMoveMode( false );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_WAYPOINTS:
//			DEBUG_ASSERTCRASH( !TheInGameUI->isInWaypointMode(), ("Setting m_waypointMode but it's already set!") );
			TheInGameUI->setWaypointMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_PREFER_SELECTION:
			TheInGameUI->setPreferSelectionMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_PREFER_SELECTION:
			TheInGameUI->setPreferSelectionMode( false );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_WAYPOINTS:
//			DEBUG_ASSERTCRASH( TheInGameUI->isInWaypointMode(), ("Clearing m_waypointMode but it's already clear!") );
			TheInGameUI->setWaypointMode( false );
			break;


		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_BEGIN_FORCEATTACK:
			TheInGameUI->setForceAttackMode( true );
			break;

		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_END_FORCEATTACK:
			TheInGameUI->setForceAttackMode( false );
			break;

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_ALL_CHEER:
		{
			if ( TheGameLogic->isInMultiplayerGame() )
			{
				TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_allCheerSound);
				disp = DESTROY_MESSAGE;
				TheMessageStream->appendMessage( GameMessage::MSG_DO_CHEER );
			}
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_TAKE_SCREENSHOT:
		{
			if (TheDisplay)
				TheDisplay->takeScreenShot();
			disp = DESTROY_MESSAGE;
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_SELECTED_GROUP:
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
			// weed out unit responses for things we don't own.
			DrawableList listOfUnits = *TheInGameUI->getAllSelectedDrawables();
			for (DrawableListIt it = listOfUnits.begin(); it != listOfUnits.end(); /* empty */) {
				Drawable *draw = *it;
				if (draw->getObject() && draw->getObject()->isLocallyControlled()) {
					// This thing can emit a unit response.
					++it;
					continue;
				} else {
					// remove it.
					it = listOfUnits.erase(it);
				}
			}
			if (!listOfUnits.empty()) {
				pickAndPlayUnitVoiceResponse( &listOfUnits, GameMessage::MSG_CREATE_SELECTED_GROUP );
			}

			m_teamExists = true;
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND:
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
			m_teamExists = true;
			break;
		}


		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_DESTROY_SELECTED_GROUP:
		{
			m_teamExists = false;
			break;
		}

		// --------------------------------------------------------------------------------------------
		// An object is mouseover'd.  A hint of a command may be generated if something is selected
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_MOUSEOVER_DRAWABLE_HINT:
		{
			const CommandButton *command = TheInGameUI->getGUICommand();
			if( TheInGameUI->getSelectCount() > 0 
					|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT) ) // If something is selected
			{
				/// @todo This as well as the one in GameMessage::MSG_DRAWABLE_PICKED below should possibly have a generalized CanAttack instead of simply checking isEnemyOf
				Drawable *draw = TheGameClient->findDrawableByID( msg->getArgument( 0 )->drawableID );
				if( draw )
				{

					//
					// given the drawable that we have moused over, pretend that we have actually
					// "picked" that drawable, depending on what we have selected, our mode,
					// and what "picked" is we would do a command ... but here we only will generate
					// the hint message, *not* the actual command
					//
					if (isForceAttackTargeting()) {
						evaluateForceAttack(draw, draw->getPosition(), DO_HINT );
					} else {
						evaluateContextCommand( draw, draw->getPosition(), DO_HINT );
					}

					// Do not eat this message, as it itself has another purpose in HintSpy

				}
			}
		}
		break;
		
		//-----------------------------------------------------------------------------
		// Terrain is mouseover'd.  A hint of a command may be generated if something is selected
		//
		case GameMessage::MSG_MOUSEOVER_LOCATION_HINT:
		{
			Coord3D position = msg->getArgument( 0 )->location;

			//
			// This message occurs whenever the mouse cursor moves off of a drawable, in which
			// case we want to turn off the pick hint.
			//
			if (isForceAttackTargeting()) {
				evaluateForceAttack( NULL, &position, DO_HINT );
			} else {
				evaluateContextCommand( NULL, &position, DO_HINT );
			}

			// Do not eat this message, as it will do something itself at HintSpy
			break;

		}  // end case GameMessage::MSG_MOUSEOVER_LOCATION_HINT

		//-----------------------------------------------------------------------------
		// A double click is a button press too, and the second press of one used to arrive here
		// as nothing at all.  The anchor and the press time then still held the first click's
		// values, so the button-up that followed measured against the wrong moment and a plain
		// single click right after a double click was read as a drag.
		case GameMessage::MSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			// There are two ways in which we can ignore this as a deselect:
			// 1) 2-D position on screen
			// 2) Time has exceeded the time which we allow for this to be a click.
			m_mouseRightDragAnchor = msg->getArgument( 0 )->pixel;
			m_mouseRightDown = (UnsignedInt) msg->getArgument( 2 )->integer;

			// attack move and guard draw their line with the left button only
			m_formationDragAnchor = m_mouseRightDragAnchor;
			m_formationDragArmed = isFormationDragArmed() && !TheInGameUI->isLineOrderArmed();

			break;
		}

		//-----------------------------------------------------------------------------
		// Attack move and guard are aimed with the left button, so with either armed a left drag draws
		// the same line a right drag does.  The attack key keeps its left drag for the attack circle.
		case GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		{
			if( TheInGameUI->isLineOrderArmed() )
			{
				m_formationDragAnchor = msg->getArgument( 0 )->pixel;
				m_formationDragArmed = isFormationDragArmed();
			}
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP:
		{
			if( m_formationDragArmed )
				finishFormationDrag( msg->getArgument( 0 )->pixel );
			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			// the line only appears once the cursor has travelled far enough that this is a drag and
			// not a click that wobbled
			if( m_formationDragArmed )
			{
				const ICoord2D& here = msg->getArgument( 0 )->pixel;
				if( TheInGameUI->isFormationDragging() )
				{
					// once it is a drag, every position message is a point on the curve
					TheInGameUI->addFormationDragPoint( here );
				}
				else if( (UnsignedInt)abs( here.x - m_formationDragAnchor.x ) > TheMouse->m_dragTolerance
								 || (UnsignedInt)abs( here.y - m_formationDragAnchor.y ) > TheMouse->m_dragTolerance )
				{
					TheInGameUI->addFormationDragPoint( m_formationDragAnchor );
					TheInGameUI->addFormationDragPoint( here );
				}
			}

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			// register this event for determining if the click was fast or short enough not to be a drag
			m_mouseRightDragLift = msg->getArgument( 0 )->pixel;
			m_mouseRightUp = (UnsignedInt) msg->getArgument( 2 )->integer;

			if( m_formationDragArmed )
				finishFormationDrag( m_mouseRightDragLift );

			// a structure waiting to be placed is dropped by the same release, over in
			// SelectionXlat, which sees it whether this was a click or a drag

			break;
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK:
		{
			if( TheGlobalData->m_doubleClickAttackMove && !TheGlobalData->isLegacyInput() )
			{
				// create the message and append arguments for a guard location
				GameMessage *newMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_GUARD_POSITION );
				Coord3D pos;
				TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos );
				newMsg->appendLocationArgument(pos);
				newMsg->appendIntegerArgument(GUARDMODE_NORMAL);
				
				ThePlayerList->getLocalPlayer()->getAcademyStats()->recordDoubleClickAttackMoveOrderGiven();

        TheInGameUI->triggerDoubleClickAttackMoveGuardHint();

				break;
			}
			//intentional fall through
		}
		case GameMessage::MSG_MOUSE_RIGHT_CLICK:
		{
			// The right button is the order button, always.  It used to depend on UseAlternateMouse,
			// which is gone: a click here commands, a drag draws a formation line, and neither of
			// them scrolls.
			//
			// The one exception is the attack, attack move or guard key.  Those orders are aimed with the
			// left button, so a right click while one is armed puts the key down and gives no order.
			//
			// A Legacy right button never orders at all: it scrolls and deselects, which LookAtXlat and
			// SelectionXlat do.
			if( TheGlobalData->isLegacyInput() )
				break;

			if( TheInGameUI->isOrderKeyArmed() )
			{
				TheInGameUI->clearAttackMoveToMode();
				disp = DESTROY_MESSAGE;
				break;
			}

			if (TheMouse->isClick(&m_mouseRightDragAnchor, &m_mouseRightDragLift,
					NULL, NULL,
					m_mouseRightDown, m_mouseRightUp))
			{
				Bool isPoint = (msg->getArgument(0)->pixelRegion.height() == 0 && msg->getArgument(0)->pixelRegion.width() == 0);

				// NOTE: RIGHT_CLICK is not transmitted if AREA_SELECTION or DRAWABLE_PICKED occurs.
				// If we see this msg, no object was clicked on, therefore clicked on ground.
				// Issue move command to all currently selected objects.

				// sanity
				if( TheTacticalView == NULL )
					break;

				// translate from screen coordinates to terrain coords
				Coord3D pos;
				TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos );

				const CommandButton *command = TheInGameUI->getGUICommand();
				Bool controllable = TheInGameUI->areSelectedObjectsControllable()
														|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT);
				if (isPoint && controllable)
				{
					UnsignedInt pickType = getPickTypesForContext( isForceAttackTargeting() );
					Drawable *draw = TheTacticalView->pickDrawable(&msg->getArgument(0)->pixelRegion.lo, 
																													isForceAttackTargeting(), 
																													(PickType) pickType);
					if (isForceAttackTargeting()) {
						evaluateForceAttack( draw, &pos, DO_COMMAND );
					} else {
						evaluateContextCommand( draw, &pos, DO_COMMAND );
					}

					disp = DESTROY_MESSAGE;
					TheInGameUI->clearAttackMoveToMode();
				}
			}

			break;					
		}

		//-----------------------------------------------------------------------------
		case GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK:
		{
			// Legacy's orders are on the left button, so its double-click guard is here.  Modern's is on
			// the right button, and this case does nothing of its own.
			if( TheGlobalData->isLegacyInput() && TheGlobalData->m_doubleClickAttackMove )
			{
				// create the message and append arguments for a guard location
				GameMessage *newMsg = TheMessageStream->appendMessage( GameMessage::MSG_DO_GUARD_POSITION );
				Coord3D pos;
				TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos );
				newMsg->appendLocationArgument(pos);
				newMsg->appendIntegerArgument(GUARDMODE_NORMAL);

				ThePlayerList->getLocalPlayer()->getAcademyStats()->recordDoubleClickAttackMoveOrderGiven();

				TheInGameUI->triggerDoubleClickAttackMoveGuardHint();

				break;
			}
			//intentional fall through
		}
		case GameMessage::MSG_MOUSE_LEFT_CLICK:
		{
			Bool isPoint = (msg->getArgument(0)->pixelRegion.height() == 0 && msg->getArgument(0)->pixelRegion.width() == 0);

			// NOTE: LEFT_CLICK is not transmitted if AREA_SELECTION or DRAWABLE_PICKED occurs.
			// If we see this msg, no object was clicked on, therefore clicked on ground.
			// Issue move command to all currently selected objects.

			// sanity
			if( TheTacticalView == NULL )
				break;

			// translate from screen coordinates to terrain coords
			Coord3D pos;
			TheTacticalView->screenToTerrain( &msg->getArgument( 0 )->pixel, &pos );

			const CommandButton *command = TheInGameUI->getGUICommand();
			// maintain this as the list of GUI button initiated commands that fire with left click
  			Bool isFiringGUICommand = (command	&& (command->getCommandType() == GUI_COMMAND_SPECIAL_POWER
  												|| command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT
 												|| command->getCommandType() == GUI_COMMAND_FIRE_WEAPON
												|| command->getCommandType() == GUI_COMMAND_COMBATDROP
												|| command->getCommandType() == GUICOMMANDMODE_HIJACK_VEHICLE
												|| command->getCommandType() == GUICOMMANDMODE_CONVERT_TO_CARBOMB));

			// The left button selects and nothing else.  The exceptions are a GUI command that is
			// already armed and waiting for a target, and the attack, attack move and guard keys: all of
			// them are aimed with the left button, because the right one cancels them.  In Legacy the left
			// button is the order button as well, as it was in the game as shipped.
			const Bool isOrderKey = TheInGameUI->isOrderKeyArmed();
			if( !isFiringGUICommand && !isOrderKey && !TheGlobalData->isLegacyInput() )
				break;

			Bool controllable = TheInGameUI->areSelectedObjectsControllable()
													|| (command && command->getCommandType() == GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT);
			if (isPoint && controllable)
			{
				UnsignedInt pickType = getPickTypesForContext( isForceAttackTargeting() );
				Drawable *draw = TheTacticalView->pickDrawable(&msg->getArgument(0)->pixelRegion.lo, 
																												isForceAttackTargeting(), 
																												(PickType) pickType);

				if (isForceAttackTargeting()) {
					evaluateForceAttack( draw, &pos, DO_COMMAND );
				} else {
					evaluateContextCommand( draw, &pos, DO_COMMAND );
				}

				disp = DESTROY_MESSAGE;
				TheInGameUI->spendOrderKey();

				//issueMoveToLocationCommand( &pos, draw, DO_COMMAND );
			}
			break;

		}  // end case GameMessage::MSG_MOUSE_LEFT_CLICK



		//--------------------------------------------------------------------------------------
		/* Single-player test hook: hand the next player over to the keyboard.  Its AI is deleted
			 outright, so nothing fights you for its units - press again to walk on to the player
			 after it, and eventually back around to your own.
			 ponytail: only the AIPlayer goes away - the map's own scripts can still order that
			 player's teams about; kill the script lists too if that ever gets in the way. */
		case GameMessage::MSG_META_SWITCH_CONTROL:
		{
			Int count = ThePlayerList->getPlayerCount();
			Int idx = 0;
			for (Int i = 0; i < count; ++i)
			{
				if (ThePlayerList->getNthPlayer(i) == ThePlayerList->getLocalPlayer())
				{
					idx = i;
					break;
				}
			}

			for (Int i = 1; i < count; ++i)
			{
				if (takeControlOfPlayer(ThePlayerList->getNthPlayer((idx + i) % count)))
					break;
			}
			disp = DESTROY_MESSAGE;
			break;
		}

#ifdef ALLOW_ALT_F4
		case GameMessage::MSG_META_DEMO_INSTANT_QUIT:
    {
			if (TheGameLogic->isInGame())
			{
				if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
				{
					TheRecorder->stopRecording();
				}
				TheGameLogic->clearGameData();
			}
			TheGameEngine->setQuitting(TRUE);
			disp = DESTROY_MESSAGE;
			break;
    }
#endif



		//------------------------------------------------------------------------------- DEMO MESSAGES

#if defined(_DEBUG) || defined(_INTERNAL)
		//------------------------------------------------------------------------- BEGIN DEMO MESSAGES
		//------------------------------------------------------------------------- BEGIN DEMO MESSAGES
		//------------------------------------------------------------------------- BEGIN DEMO MESSAGES
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_SWITCH_TEAMS:
		{
			if (TheGameLogic->isInGame())
			{
				Int idx;
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					if (ThePlayerList->getNthPlayer(i) == ThePlayerList->getLocalPlayer())
					{
						idx = i;
						break;
					}
				}
				Int idxOrig = idx;
				do 
				{

					++idx;
					if (idx >= ThePlayerList->getPlayerCount())
						idx = 0;

					if (idx == idxOrig)
					{
						break;
					}

				} while (ThePlayerList->getNthPlayer(idx) == ThePlayerList->getNeutralPlayer());
				
				ThePlayerList->setLocalPlayer(ThePlayerList->getNthPlayer(idx));
				TheInGameUI->deselectAllDrawables();
				// the fog you are handed has to be the fog of the player you just became
				TheGhostObjectManager->setLocalPlayerIndex(idx);
				ThePartitionManager->refreshShroudForLocalPlayer();
				TheControlBar->initSpecialPowershortcutBar(ThePlayerList->getLocalPlayer());
				TheControlBar->setControlBarSchemeByPlayer(ThePlayerList->getLocalPlayer());
			}
			disp = DESTROY_MESSAGE;
			break;
		} 

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_SWITCH_TEAMS_BETWEEN_CHINA_USA:
		{
			Player *p = ThePlayerList->getLocalPlayer();
			AsciiString side;
			side.set(p->getSide());

			if(side.compareNoCase("America") == 0)
			{
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					Player *pt = ThePlayerList->getNthPlayer(i);
					if(pt->getSide().compareNoCase("China") == 0)
					{
						ThePlayerList->setLocalPlayer(pt);
						TheInGameUI->deselectAllDrawables();
						ThePartitionManager->refreshShroudForLocalPlayer();
						TheControlBar->setControlBarSchemeByPlayer(ThePlayerList->getLocalPlayer());

						break;
					}
				}
			}

			if(side.compareNoCase("China") == 0)
			{
				for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
				{
					Player *pt = ThePlayerList->getNthPlayer(i);
					if(pt->getSide().compareNoCase("America") == 0)
					{
						ThePlayerList->setLocalPlayer(pt);
						TheInGameUI->deselectAllDrawables();
						ThePartitionManager->refreshShroudForLocalPlayer();
						TheControlBar->setControlBarSchemeByPlayer(ThePlayerList->getLocalPlayer());
						break;
					}
				}
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOD_DECREASE:
		{
			TheGameClient->adjustLOD(-1);
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOD_INCREASE:
		{
			TheGameClient->adjustLOD(1);
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_HELP:
		{
			// for demo, they don't want the help menu showing up.  I left this as a block comment because we
			// might want to add back in the help menu at some point in time.
/*
			if (TheWindowManager && TheNameKeyGenerator)
			{
				GameWindow *motd = TheWindowManager->winGetWindowFromId(NULL, (Int)TheNameKeyGenerator->nameToKey(AsciiString("MOTD.wnd:MOTD")));
				if (motd)
					motd->winHide(!motd->winIsHidden());
			}*/
  
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_BEHIND_BUILDINGS:
		{
			if (TheGlobalData)
			{

				if(TheGameLogic->getShowBehindBuildingMarkers())
				{
					TheGameLogic->setShowBehindBuildingMarkers(FALSE);
					TheInGameUI->message("GUI:ShowBehindBuildings");
				}
				else
				{
					TheGameLogic->setShowBehindBuildingMarkers(TRUE);
					TheInGameUI->message("GUI:HideBehindBuildings");
				}
			}  
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_LETTERBOX:
		{
			if(TheShell->isShellActive())
			{
				WindowLayout *win = TheShell->top();
				if(win)
				{
					win->hide(!win->isHidden());
				}

			}
			else
			{
				Bool hide = false;
				if (TheWindowManager)
				{
					Int id = (Int)TheNameKeyGenerator->nameToKey(AsciiString("ControlBar.wnd:ControlBarParent"));
					GameWindow *window = TheWindowManager->winGetWindowFromId(NULL, id);

					if (window)
						hide = !window->winIsHidden();
				}

				if (hide)
					HideControlBar();
				else
					ShowControlBar(FALSE);
				TheDisplay->toggleLetterBox();
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MESSAGE_TEXT:
		{

			// toggle the message state
			TheInGameUI->toggleMessages();

			// when messages get turned on, display a message
			if( TheInGameUI->isMessagesOn() )
				TheInGameUI->message( TheGameText->fetch( "GUI:MessagesOn" ) );

			disp = DESTROY_MESSAGE;
			break;

		}  // end clear message text

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MOTION_BLUR_ZOOM:
		{	Int mode;
			if ((mode=TheTacticalView->getViewFilterType()) == FT_VIEW_MOTION_BLUR_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
			}
			else 
			{
				static Bool saturate = false;
				FilterModes mode = FM_VIEW_MB_IN_AND_OUT_ALPHA;
				if (saturate) {
					mode = FM_VIEW_MB_IN_AND_OUT_SATURATE;
				}
				if (TheTacticalView->getCameraLock()!=0) {
					mode = FM_VIEW_MB_PAN_ALPHA;
				}
				saturate = !saturate;
				Coord3D curpos;
				TheTacticalView->getPosition(&curpos);
				curpos.x += 200;
				curpos.y += 200;
				TheTacticalView->setViewFilterPos(&curpos); 

				TheTacticalView->setViewFilterMode(mode); 						 
				TheTacticalView->setViewFilter(FT_VIEW_MOTION_BLUR_FILTER);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_BW_VIEW:
		{   //We're not testing BW mode anymore, so use this message for toggling wireframe mode.
			static mode=0;
			if (mode == 0)
			{	//First turn on wireframe
				TheTacticalView->set3DWireFrameMode(TRUE);
				mode++;
				disp = DESTROY_MESSAGE;
				break;
			}
			if (mode == 1)
			{
				if ((TheTacticalView->getViewFilterType()) == FT_VIEW_CROSSFADE)
				{	//mode already set, turn it off
					TheTacticalView->setViewFilterMode(FM_NULL_MODE);
					TheTacticalView->setViewFilter(FT_NULL_FILTER);
					TheTacticalView->setFadeParameters(0,-1);
				}
				else
				{
					TheScriptEngine->doFreezeTime();

					//TheTacticalView->setViewFilterMode(FM_VIEW_CROSSFADE_CIRCLE);
					TheTacticalView->setViewFilterMode(FM_VIEW_CROSSFADE_FB_MASK);
					TheTacticalView->setViewFilter(FT_VIEW_CROSSFADE);
					TheTacticalView->setFadeParameters(60,-1);
					TheTacticalView->set3DWireFrameMode(FALSE);
					mode++;
				}
			}
			else
			if (mode == 2)
			{
				TheScriptEngine->doUnfreezeTime();
				mode=0;	//reset everything
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_RED_VIEW:
		{	
			if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				TheTacticalView->setFadeParameters(30,-1);
			}
			else
			{
				TheTacticalView->setViewFilterMode(FM_VIEW_BW_RED_AND_WHITE);
				TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
				TheTacticalView->setFadeParameters(30,1);
			}

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_GREEN_VIEW:
		{	
			if ((TheTacticalView->getViewFilterType()) == FT_VIEW_BW_FILTER)
			{	//mode already set, turn it off
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				TheTacticalView->setFadeParameters(30,-1);
			}
			else
			{
				TheTacticalView->setViewFilterMode(FM_VIEW_BW_GREEN_AND_WHITE);
				TheTacticalView->setViewFilter(FT_VIEW_BW_FILTER);
				TheTacticalView->setFadeParameters(30,1);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_CYCLE_LOD_LEVEL:
		{
			static Int level=DYNAMIC_GAME_LOD_VERY_HIGH;

			level--;

			if (level < DYNAMIC_GAME_LOD_LOW)
				level = DYNAMIC_GAME_LOD_VERY_HIGH;

			TheGameLODManager->setDynamicLODLevel((DynamicGameLODLevel)level);

			UnicodeString lodName;
			lodName.format(L"Dynamic Game Detail %hs",TheGameLODManager->getDynamicGameLODLevelName((DynamicGameLODLevel)level));
			TheInGameUI->message(lodName);

			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DUMP_ASSETS:
		{
			TheDisplay->dumpModelAssets("UsedMapAssets.txt");
			disp = DESTROY_MESSAGE;
			break;
		}  
		
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SHADOW_VOLUMES:
		{
			TheWritableGlobalData->m_useShadowVolumes = !TheGlobalData->m_useShadowVolumes;
			TheWritableGlobalData->m_useShadowDecals = !TheGlobalData->m_useShadowDecals;
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_FOGOFWAR:
		{
			TheWritableGlobalData->m_fogOfWarOn = !TheGlobalData->m_fogOfWarOn;
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_SELECTION:
		{
			TheMessageStream->appendMessage( GameMessage::MSG_DEBUG_KILL_SELECTION );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_ALL_ENEMIES:
		{
			for (Drawable *d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
			{
				Object* obj = d->getObject();
				if (obj && obj->getControllingPlayer() && obj->getControllingPlayer()->getRelationship(ThePlayerList->getLocalPlayer()->getDefaultTeam()) == ENEMIES)
				{
					obj->kill();
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_GIVE_VETERANCY:
		case GameMessage::MSG_META_DEBUG_TAKE_VETERANCY:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{

				const DrawableList *list = TheInGameUI->getAllSelectedDrawables();
				for (DrawableListCIt it = list->begin(); it != list->end(); ++it) 
				{
					Drawable *pDraw = *it;
					if (pDraw) 
					{
						Object *pObject = pDraw->getObject();
						if (pObject)
						{
							ExperienceTracker *et = pObject->getExperienceTracker();
							if (et)
							{
								if (et->isTrainable())
								{
									VeterancyLevel oldVet = et->getVeterancyLevel();
									VeterancyLevel newVet = oldVet;
									if (t == GameMessage::MSG_META_DEBUG_GIVE_VETERANCY)
									{
										if (oldVet < LEVEL_LAST)
										{
											newVet = (VeterancyLevel)((Int)oldVet + 1);
										}
									}
									else
									{
										if (oldVet > LEVEL_FIRST)
										{
											newVet = (VeterancyLevel)((Int)oldVet - 1);
										}
									}
									et->setVeterancyLevel(newVet);
								}
							}
						}
					}
				}
				disp = DESTROY_MESSAGE;
			}
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_INCR_ANIM_SKATE_SPEED:
		{
			TheSkateDistOverride += 0.25f;
			TheInGameUI->message( UnicodeString( L"Skate Distance Override is now %f" ), TheSkateDistOverride );
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DECR_ANIM_SKATE_SPEED:
		{
			TheSkateDistOverride -= 0.25f;
			TheInGameUI->message( UnicodeString( L"Skate Distance Override is now %f" ), TheSkateDistOverride );
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_CYCLE_EXTENT_TYPE:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_TYPE;
			if (!extentModAmount) extentModAmount = 1.0f;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = 1.0f;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MAJOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = -1.0f;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MAJOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MAJOR;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = 1.0f;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_MINOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = -1.0f;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_MINOR_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_MINOR;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = 1.0f;
		case GameMessage::MSG_META_DEBUG_INCREASE_EXTENT_HEIGHT_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = EXTENT_BIG_CHANGE;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = -1.0f;
		case GameMessage::MSG_META_DEBUG_DECREASE_EXTENT_HEIGHT_BIG:
			if (extentModType == EXTENTMOD_INVALID) extentModType = EXTENTMOD_HEIGHT;
			if (!extentModAmount) extentModAmount = -EXTENT_BIG_CHANGE;
		{
			const DrawableList *list = TheInGameUI->getAllSelectedDrawables();
			for (DrawableListCIt it = list->begin(); it != list->end(); ++it) 
			{
				Drawable *pDraw = *it;
				if (pDraw) 
				{
					Object *pObject = pDraw->getObject();
					if (pObject)
					{
						const GeometryInfo oldGeometry = pObject->getGeometryInfo();
						
						GeometryInfo newGeometry = oldGeometry;
						newGeometry.tweakExtents(extentModType, extentModAmount);
						
						AsciiString msg;
						msg.format("Extent %s --> %s   %d %g",oldGeometry.getDescriptiveString().str(), newGeometry.getDescriptiveString().str(),	extentModType, extentModAmount);

						UnicodeString umsg;
						umsg.translate(msg);
						TheInGameUI->message(umsg);
						DEBUG_LOG(("%ls\n", msg.str()));

						pObject->setGeometryInfo( newGeometry );
					}
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_LOCK_CAMERA_TO_SELECTION:
		{
			ObjectID id = INVALID_ID;
			Drawable *d = NULL;
			for (d = TheGameClient->firstDrawable(); d; d = d->getNextDrawable())
			{
				if (d->isSelected() && d->getObject())
				{
					id = d->getObject()->getID();
					break;
				}
			}
			if (id != 0 && TheTacticalView->getCameraLock() == id)
			{
				if (TheTacticalView->getCameraLockDrawable())
				{	
					id = INVALID_ID;	// toggle it
					d=NULL;
					TheTacticalView->forceRedraw();	//reset camera to normal
				}
			}
			else
			{
				d = NULL;
			}
			TheTacticalView->setCameraLock(id);
			TheTacticalView->setCameraLockDrawable(d);
			disp = DESTROY_MESSAGE;
			break;
		}	

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SOUND:
		{
			if (TheAudio->isOn(AudioAffect_Sound))
			{
				TheDisplay->stopMovie();
				TheInGameUI->stopMovie();
				TheAudio->setOn(false, AudioAffect_All);
			}
			else
			{
				TheAudio->setOn(true, AudioAffect_All);
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_TRACKMARKS:
		{
			TheWritableGlobalData->m_makeTrackMarks = !TheGlobalData->m_makeTrackMarks;
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_WATERPLANE:
		{
			TheWritableGlobalData->m_useWaterPlane = !TheGlobalData->m_useWaterPlane;
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TIME_OF_DAY:
		{
			TimeOfDay tod = TimeOfDay((Int) TheGlobalData->m_timeOfDay + 1);
			if (tod >= TIME_OF_DAY_COUNT)
				tod = TIME_OF_DAY_FIRST;
			if (TheWritableGlobalData->setTimeOfDay(tod))
			{
				TheGameClient->setTimeOfDay(TheGlobalData->m_timeOfDay);
				if (TheGlobalData->m_forceModelsToFollowTimeOfDay)
				{
					for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
					{
						Drawable* d = obj->getDrawable();
						if (d)
						{
							// this just forces a refresh.
							ModelConditionFlags empty;
							d->clearAndSetModelConditionFlags(empty, empty);
						}
					}
        } 
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_REMOVE_PREREQ:
		{
			// Doesn't make a valid network message
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			localPlayer->toggleIgnorePrereqs();
			disp = DESTROY_MESSAGE;
			break;
    } 

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_INSTANT_BUILD:
		{
			// Doesn't make a valid network message
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			localPlayer->toggleInstantBuild();
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_FREE_BUILD:
		{
			// Doesn't make a valid network message
			Player *localPlayer = ThePlayerList->getLocalPlayer();
			localPlayer->toggleFreeBuild();
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_RENDER:
		{
			TheWritableGlobalData->m_disableRender = !TheGlobalData->m_disableRender;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_ADD_CASH:
		{
			if ( !TheGameLogic->isInMultiplayerGame() )
			{
				Player *localPlayer = ThePlayerList->getLocalPlayer();
				Money *money = localPlayer->getMoney();
				money->deposit( 10000 );
				disp = DESTROY_MESSAGE;		// same omission as the cheat message above
			}
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_RUNSCRIPT1:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT2:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT3:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT4:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT5:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT6:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT7:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT8:
		case GameMessage::MSG_META_DEMO_RUNSCRIPT9:
		{
			if( TheScriptEngine ) 
			{
				Int script = t - GameMessage::MSG_META_DEMO_RUNSCRIPT1 + 1;
				AsciiString scriptName;
				scriptName.format("KEY_F%d", script);
				TheScriptEngine->runScript(scriptName);
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_KILL_AREA_SELECTION:
		{
			for( int i=1; i<msg->getArgumentCount(); i++ )
			{
				Object *obj = TheGameLogic->findObjectByID( msg->getArgument( i )->objectID );
				if( obj )
				{
					obj->kill();
				}
			}
		}
		break;

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE1:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE2:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE3:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE4:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE5:
		case GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE6:
		case GameMessage::MSG_META_DEMO_NEXT_OBJECTIVE_MOVIE:
		{
			if (TheGameLogic->isInGame())
			{
				if (t == GameMessage::MSG_META_DEMO_NEXT_OBJECTIVE_MOVIE)
				{
					m_objective++;
					if (m_objective > 6)
						m_objective = 1;
				}
				else
				{
					m_objective = t - GameMessage::MSG_META_DEMO_PLAY_OBJECTIVE_MOVIE1 + 1;
				}
				AsciiString name;
				name.format("DemoObjective%02d", m_objective);
				TheInGameUI->playMovie(name);
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MILITARY_SUBTITLES:
		{
			
			TheInGameUI->militarySubtitle("MSG:Testing", 10000);  // use some innocuous string
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_MUSIC:
		{
			if (TheAudio->isMusicPlaying()) {
				TheAudio->stopAudio(AudioAffect_Music);
				TheInGameUI->message( UnicodeString( L"Stopping Music" ));
			} else {
				TheAudio->resumeAudio(AudioAffect_Music);
				TheInGameUI->message( UnicodeString( L"Resuming Music" ));
			}

			disp = DESTROY_MESSAGE;
			break;
		} 

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_MUSIC_NEXT_TRACK:
		{
			TheAudio->nextMusicTrack();
			UnicodeString ustr;
			ustr.format(L"Playing Track: %hs", TheAudio->getMusicTrackName().str());
			TheInGameUI->message( ustr );
			disp = DESTROY_MESSAGE;
			break;
		} 
		
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_MUSIC_PREV_TRACK:
		{
			TheAudio->prevMusicTrack();
			UnicodeString ustr;
			ustr.format(L"Playing Track: %hs", TheAudio->getMusicTrackName().str());
			TheInGameUI->message( ustr );
			disp = DESTROY_MESSAGE;
			break;
		} 

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
#ifdef PERF_TIMERS
		case GameMessage::MSG_META_DEMO_TOGGLE_METRICS:
		{
			TheWritableGlobalData->m_showMetrics = !TheGlobalData->m_showMetrics;
			if (TheGlobalData->m_showMetrics) {
				TheDisplay->setDebugDisplayCallback(StatMetricsDisplay);
			} else {
				TheDisplay->setDebugDisplayCallback(NULL);
			}
			break;
		}
#endif // #ifdef PERF_TIMERS

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_AI_DEBUG:
		{
			TheWritableGlobalData->m_debugAI = (AIDebugOptions)((Int)TheGlobalData->m_debugAI + 1);
			if (TheGlobalData->m_debugAI >= AI_DEBUG_END) 
				TheWritableGlobalData->m_debugAI=AI_DEBUG_NONE;
			UnicodeString line;
			line.format(L"Level %d", TheGlobalData->m_debugAI);
			TheInGameUI->message( UnicodeString( L"Debug AI Mode is %s" ), TheGlobalData->m_debugAI ? line.str() : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}
		
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SUPPLY_CENTER_PLACEMENT:
		{
			TheWritableGlobalData->m_debugSupplyCenterPlacement = !TheWritableGlobalData->m_debugSupplyCenterPlacement;

			TheInGameUI->message( UnicodeString( L"Log SupplyCenter Placement is %s" ), TheGlobalData->m_debugSupplyCenterPlacement ? L"ON" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_SCIENCEPURCHASEPOINTS:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->addSciencePurchasePoints(1);
			TheInGameUI->message( UnicodeString( L"Adding a SciencePurchasePoint" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_ALL_SCIENCES:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
			{
				// cheese festival: do NOT imitate this code. it is for debug purposes only.
				std::vector<AsciiString> v = TheScienceStore->friend_getScienceNames();
				for (int i = 0; i < v.size(); ++i) 
				{
					ScienceType st = TheScienceStore->getScienceFromInternalName(v[i]);
					if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st))
					{
						player->grantScience(st);
					}
				}
			}
			TheInGameUI->message( UnicodeString( L"Granting all sciences!" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_GIVE_RANKLEVEL:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->setRankLevel(player->getRankLevel() + 1);
			TheInGameUI->message( UnicodeString( L"Adding a RankLevel" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TAKE_RANKLEVEL:
		{
			Player *player = ThePlayerList->getLocalPlayer();
			if (player)
				player->setRankLevel(player->getRankLevel() - 1);
			TheInGameUI->message( UnicodeString( L"Subtracting a RankLevel" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_CAMERA_DEBUG:
		{
			TheWritableGlobalData->m_debugCamera = !TheGlobalData->m_debugCamera;
			TheInGameUI->message( UnicodeString( L"Debug Camera Mode is %s" ), TheGlobalData->m_debugCamera ? L"On" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_NO_DRAW:
		{
			TheWritableGlobalData->m_noDraw = REAL_TO_INT(pow(2, 32) - 1);
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DUMP_PLAYER_OBJECTS:
		case GameMessage::MSG_META_DEBUG_DUMP_ALL_PLAYER_OBJECTS:
		{
			AsciiString line;
			line.format("*******************************");
			TheScriptEngine->AppendDebugMessage(line, FALSE);
			line.format("Dumping player object counts");
			TheScriptEngine->AppendDebugMessage(line, FALSE);
			for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
			{
				Player *pPlayer = ThePlayerList->getNthPlayer(i);
				if (!pPlayer || !pPlayer->getPlayerTemplate() || !pPlayer->getPlayerTemplate()->isPlayableSide())
					continue;

				Int numObjects = 0;
				pPlayer->iterateObjects( countObjects, &numObjects );
				line.format("Player %d (%ls) has %d non-dead objects", i, pPlayer->getPlayerDisplayName().str(), numObjects);
				TheScriptEngine->AppendDebugMessage(line, FALSE);

				if (numObjects && (numObjects <= 5 || t == GameMessage::MSG_META_DEBUG_DUMP_ALL_PLAYER_OBJECTS))
				{
					line = "Objects are:";
					TheScriptEngine->AppendDebugMessage(line, FALSE);
					pPlayer->iterateObjects( printObjects, NULL );
				}
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_TOGGLE_NETWORK:
		{
			if (TheNetwork != NULL) {
				TheNetwork->toggleNetworkOn();
			}
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_PARTICLEDEBUG:
		{
			if (TheDisplay->getDebugDisplayCallback() == ParticleSystemDebugDisplay)
				TheDisplay->setDebugDisplayCallback(NULL);
			else
				TheDisplay->setDebugDisplayCallback(ParticleSystemDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}
		
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_VISIONDEBUG:
		{
			TheWritableGlobalData->m_debugVisibility = !TheGlobalData->m_debugVisibility;
			TheInGameUI->message( UnicodeString( L"Debug Vision Mode is %s" ), TheGlobalData->m_debugVisibility? L"On" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_PROJECTILEDEBUG:
		{
			TheWritableGlobalData->m_debugProjectilePath = !TheGlobalData->m_debugProjectilePath;
			TheInGameUI->message( UnicodeString( L"Debug Projectile Path Mode is %s" ), TheGlobalData->m_debugProjectilePath? L"On" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_THREATDEBUG:
		{
			TheWritableGlobalData->m_debugThreatMap = !TheGlobalData->m_debugThreatMap;
			if (TheGlobalData->m_debugThreatMap) {
				TheWritableGlobalData->m_debugCashValueMap = false;
			}
			TheInGameUI->message( UnicodeString( L"Debug Threat Map is %s" ), TheGlobalData->m_debugThreatMap? L"On" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_CASHMAPDEBUG:
		{
			TheWritableGlobalData->m_debugCashValueMap = !TheGlobalData->m_debugCashValueMap ;
			if (TheGlobalData->m_debugCashValueMap) {
				TheWritableGlobalData->m_debugThreatMap = false;
			}
			TheInGameUI->message( UnicodeString( L"Debug Cash Value Map is %s" ), TheGlobalData->m_debugCashValueMap? L"On" : L"OFF" );
			disp = DESTROY_MESSAGE;
			break;
		}
		
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_GRAPHICALFRAMERATEBAR:
		{
			TheWritableGlobalData->m_debugShowGraphicalFramerate = !TheGlobalData->m_debugShowGraphicalFramerate;
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SHOW_EXTENTS:
			TheWritableGlobalData->m_showCollisionExtents = 1 - TheGlobalData->m_showCollisionExtents;
			TheInGameUI->message( UnicodeString( L"Show Object Extents %s" ),
				                    TheGlobalData->m_showCollisionExtents ? L"ON" : L"OFF" );
			break;

    //------------------------------------------------------------------------------- DEMO MESSAGES
    //-----------------------------------------------------------------------------------------
    case GameMessage::MSG_META_DEBUG_SHOW_AUDIO_LOCATIONS:
      TheWritableGlobalData->m_showAudioLocations = 1 - TheGlobalData->m_showAudioLocations;
      TheInGameUI->message( UnicodeString( L"Show AudioLocations %s" ),
                            TheGlobalData->m_showAudioLocations ? L"ON" : L"OFF" );
      break;       

    //------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SHOW_HEALTH:
		{

			TheWritableGlobalData->m_showObjectHealth = 1 - TheGlobalData->m_showObjectHealth;
			TheInGameUI->message( UnicodeString( L"Object Health %s" ),
				                    TheGlobalData->m_showObjectHealth ? L"ON" : L"OFF" );
			break;
	
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PLAY_CAMEO_MOVIE:
		{
			if (TheGameLogic->isInGame())
			{
				if(TheInGameUI->cameoVideoBuffer() == NULL)
					TheInGameUI->playCameoMovie("CameoMovie");
				else
					TheInGameUI->stopCameoMovie();
			}
			disp = DESTROY_MESSAGE;
			break;
		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_ZOOM_LOCK:
		{

			if( TheTacticalView )
			{

				TheTacticalView->setZoomLimited( !TheTacticalView->isZoomLimited() );
				TheInGameUI->message( UnicodeString( L"Camera Zoom Limit: %s" ),
				                      TheTacticalView->isZoomLimited() ? L"ON" : L"OFF" );

			}  // end if

			disp = DESTROY_MESSAGE;
			break;

		}  

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_SPECIAL_POWER_DELAYS:
		{

			if( TheGlobalData )
			{

				TheWritableGlobalData->m_specialPowerUsesDelay = 1 - TheGlobalData->m_specialPowerUsesDelay;
				TheInGameUI->message( UnicodeString( L"Special Power (Superweapon) Delay: %s" ),
															TheGlobalData->m_specialPowerUsesDelay ? L"ON" : L"OFF" );

			}  // end if

			disp = DESTROY_MESSAGE;
			break;

		}  // end toggle special power delays

#ifdef ALLOW_SURRENDER
		//------------------------------------------------------------------------------- DEMO MESSAGES
		//---------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TEST_SURRENDER:
		{
			GameMessage* msg = TheMessageStream->appendMessage( GameMessage::MSG_DO_SURRENDER );
			msg->appendObjectIDArgument(INVALID_ID);			// zero means "surrender to anyone"
			msg->appendBooleanArgument(true);
			disp = DESTROY_MESSAGE;
			break;
		}
#endif

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_BATTLE_CRY:
		{
			TheAudio->addAudioEvent(&TheAudio->getMiscAudio()->m_battleCrySound);

			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_AVI:
		{
			if (TheDisplay)
				TheDisplay->toggleMovieCapture();
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_VTUNE_ON:
		{
			TheScriptEngine->setEnableVTune(true);
			TheInGameUI->message( UnicodeString( L"VTune Gathering is ON" ));
			disp = DESTROY_MESSAGE;
			break;
		}


		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_VTUNE_OFF:
		{
			TheScriptEngine->setEnableVTune(false);
			TheInGameUI->message( UnicodeString( L"VTune Gathering is OFF" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		// --------------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_TOGGLE_FEATHER_WATER:
		{
			//TheScriptEngine->setEnableVTune(false);
			//TheInGameUI->message( UnicodeString( L"VTune Gathering is OFF" ));
			--TheWritableGlobalData->m_featherWater;
			if (TheGlobalData->m_featherWater < 0) 
				TheWritableGlobalData->m_featherWater = 5;
			
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------------- DEMO MESSAGES
		//-----------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_WIN:
		{
			TheScriptEngine->debugVictory();
			TheInGameUI->message( UnicodeString( L"Instant Win" ));
			disp = DESTROY_MESSAGE;
			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_TOGGLE_DEBUG_STATS:
		{
			if (TheDisplay->getDebugDisplayCallback() == StatDebugDisplay)
				TheDisplay->setDebugDisplayCallback(NULL);
			else
				TheDisplay->setDebugDisplayCallback(StatDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}
		
		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_SLEEPY_UPDATE_PERFORMANCE:
		{
			TheInGameUI->message( UnicodeString(L"Number of Sleepy Modules: %d."), TheGameLogic->getNumberSleepyUpdates() );
			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_OBJECT_ID_PERFORMANCE:
		{
			static __int64 startTime64;
			static __int64 endTime64,freq64;
			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			Int numberLookups = 10000;
			for( Int testindex = 1; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			double timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d ObjectID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 100000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d ObjectID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 1000000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Object *objPtr = TheGameLogic->findObjectByID((ObjectID)testindex);
				objPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d ObjectID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameLogic->getObjectIDCounter() );

			break;
		}

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEBUG_DRAWABLE_ID_PERFORMANCE:
		{
			static __int64 startTime64;
			static __int64 endTime64,freq64;
			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			Int numberLookups = 10000;
			for( Int testindex = 1; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			double timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d DrawableID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 100000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d DrawableID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() );


			QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);
			QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
			numberLookups = 1000000;
			for( testindex = 1; testindex < numberLookups; testindex++ )
			{
				Drawable *drawPtr = TheGameClient->findDrawableByID((DrawableID)testindex);
				drawPtr++;
			}
			QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
			timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

			TheInGameUI->message( UnicodeString(L"Time to run %d DrawableID lookups is %f.  Next index is %d."), numberLookups, timeToUpdate, (Int)TheGameClient->getDrawableIDCounter() );

			break;
		}

		//--------------------------------------------------------------------------- END DEMO MESSAGES
		//--------------------------------------------------------------------------- END DEMO MESSAGES
		//--------------------------------------------------------------------------- END DEMO MESSAGES
#endif // #if defined(_DEBUG) || defined(_INTERNAL)

		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
#if defined(_INTERNAL) || defined(_DEBUG) 
		case GameMessage::MSG_META_DEMO_TOGGLE_AUDIODEBUG:
		{
			if (TheDisplay->getDebugDisplayCallback() == AudioDebugDisplay)
				TheDisplay->setDebugDisplayCallback(NULL);
			else
				TheDisplay->setDebugDisplayCallback(AudioDebugDisplay);
			disp = DESTROY_MESSAGE;
			break;
		}

#endif//defined(_INTERNAL) || defined(_DEBUG) 
		
#ifdef DUMP_PERF_STATS
		//------------------------------------------------------------------------DEMO MESSAGES
		//-----------------------------------------------------------------------------------------
		case GameMessage::MSG_META_DEMO_PERFORM_STATISTICAL_DUMP:
			//Dump performance statistics for this frame.
			TheWritableGlobalData->m_dumpPerformanceStatistics = TRUE;
			TheInGameUI->message( UnicodeString( L"Statistics dump made on frame: %d" ), TheGameLogic->getFrame() );
			break;
#endif // DUMP_PERF_STATS


	}  // end switch( msg->type )
	

	return disp;

}  // end CommandTranslator

static Bool isSystemMessage( const GameMessage *msg )
{
	if (!msg) {
		return false;
	}
	GameMessage::Type msgType = msg->getType();

	switch (msgType)
	{
		case GameMessage::MSG_DESTROY_SELECTED_GROUP:
		case GameMessage::MSG_LOGIC_CRC:
		case GameMessage::MSG_SET_REPLAY_CAMERA:
		case GameMessage::MSG_FRAME_TICK:
			return TRUE;
	}
	return FALSE;
}
