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

// HackInternetAIUpdate.cpp ////////////
// Author: Kris Morness, June 2002
// Desc:   State machine that handles internet hacking (free cash)

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameText.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "Lib/FixBoundary.h"
//#include "GameLogic/PartitionManager.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIStateMachine* HackInternetAIUpdate::makeStateMachine()
{
	return newInstance(HackInternetStateMachine)( getObject(), "HackInternetBasicAI");
}

//-------------------------------------------------------------------------------------------------
HackInternetAIUpdate::HackInternetAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_hasPendingCommand = false;	
	m_hackProgress = -1.0f;
} 

//-------------------------------------------------------------------------------------------------
HackInternetAIUpdate::~HackInternetAIUpdate( void )
{
} 

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isIdle() const
{
	// we need to do this because we enter an idle state briefly between takeoff/landing in these cases,
	// but scripting relies on us never claiming to be "idle"...
	if (m_hasPendingCommand)
		return false;

	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isHacking() const
{
	if( getStateMachine()->getCurrentStateID() == HACK_INTERNET )
	{
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool HackInternetAIUpdate::isHackingPackingOrUnpacking() const
{
	if( getStateMachine()->getCurrentStateID() == HACK_INTERNET || 
			getStateMachine()->getCurrentStateID() == PACKING ||
			getStateMachine()->getCurrentStateID() == UNPACKING )
	{
		return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime HackInternetAIUpdate::update( void )
{
	// have to call our parent's isIdle, because we override it to never return true
	// when we have a pending command...
	if( AIUpdateInterface::isIdle() )
	{
		if( m_hasPendingCommand )
		{
			AICommandParms parms( AICMD_MOVE_TO_POSITION, CMD_FROM_AI );	// values don't matter, will be wiped by next line
			m_pendingCommand.reconstitute( parms );
			m_hasPendingCommand = false;

 			aiDoCommand(&parms);
		}
	}

	/*UpdateSleepTime ret =*/ AIUpdateInterface::update();
	//return (mine < ret) ? mine : ret;
	/// @todo srj -- someday, make sleepy. for now, must not sleep.
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// A hacker already at work, or unpacking to start, ignores a repeat hack order. Without this a
	// hack order given to a selection with one idle hacker in it packed up every busy one and
	// started it again from nothing. A hacker packing up still takes it, which cancels the pack.
	if( parms->m_cmd == AICMD_HACK_INTERNET &&
			( getStateMachine()->getCurrentStateID() == HACK_INTERNET || getStateMachine()->getCurrentStateID() == UNPACKING ) )
		return;

	//If our hacker is currently packing up his gear, we need to prevent him
	//from moving until completed. In order to accomplish this, we'll detect,
	//then 
	if( getStateMachine()->getCurrentStateID() == HACK_INTERNET || getStateMachine()->getCurrentStateID() == PACKING )
	{
		// nuke any existing pending cmd
		m_pendingCommand.store(*parms);
		m_hasPendingCommand = true;

		if( getStateMachine()->getCurrentStateID() == HACK_INTERNET )
		{
			getStateMachine()->clear();
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState( PACKING );
		}
		return;
	}

	m_hasPendingCommand = false;
	AIUpdateInterface::aiDoCommand(parms);
}


//-------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::hackInternet()
{
	//if (m_hackInternetStateMachine)
	//	m_hackInternetStateMachine->deleteInstance();
	//m_hackInternetStateMachine = NULL;

	// must make the state machine AFTER initing the other stuff, since it may inquire of its values...
	//m_hackInternetStateMachine = newInstance(HackInternetStateMachine)( getObject() );
	//m_hackInternetStateMachine->initDefaultState();
#ifdef _DEBUG
	//m_hackInternetStateMachine->setName("HackInternetSpecificAI");
#endif
		getStateMachine()->setState(UNPACKING);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getUnpackTime() const
{
	// Not yet contained at the time this is queried
	return getHackInternetAIUpdateModuleData()->m_unpackTime;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getPackTime() const
{
	if( getObject()->getContainedBy() != NULL )
		return 0; //We don't need to pack if exiting a building

	return getHackInternetAIUpdateModuleData()->m_packTime; 
}

// ------------------------------------------------------------------------------------------------
UnsignedInt HackInternetAIUpdate::getCashUpdateDelay() const
{
	if( getObject()->getContainedBy() != NULL )
		return getHackInternetAIUpdateModuleData()->m_cashUpdateDelayFast; 
	else
		return getHackInternetAIUpdateModuleData()->m_cashUpdateDelay; 
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the pending command keeps its source */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

 // extend base class
	AIUpdateInterface::xfer(xfer);
	xfer->xferBool(&m_hasPendingCommand);
	if (m_hasPendingCommand) {
		m_pendingCommand.doXfer(xfer);
		if (version < 2)
			m_pendingCommand.setCommandSource(CMD_FROM_AI);
	}
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HackInternetAIUpdate::loadPostProcess( void )
{
 // extend base class
	AIUpdateInterface::loadPostProcess();
}  // end loadPostProcess


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
HackInternetStateMachine::HackInternetStateMachine( Object *owner, AsciiString name ) : AIStateMachine( owner, "HackInternetStateMachine" )
{
	//HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	// order matters: first state is the default state.
	defineState( UNPACKING,						newInstance(UnpackingState)( this ), HACK_INTERNET, HACK_INTERNET );
	defineState( HACK_INTERNET,				newInstance(HackInternetState)( this ), PACKING, PACKING );
	defineState( PACKING,							newInstance(PackingState)( this ), AI_IDLE, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
HackInternetStateMachine::~HackInternetStateMachine()
{
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void UnpackingState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void UnpackingState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void UnpackingState::loadPostProcess( void )
{
}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
StateReturnType UnpackingState::onEnter()
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	owner->clearModelConditionFlags( MAKE_MODELCONDITION_MASK3( MODELCONDITION_PACKING, MODELCONDITION_FIRING_A, MODELCONDITION_UNPACKING ) );
	
	owner->setModelConditionState( MODELCONDITION_UNPACKING );

	AudioEventRTS sound = *owner->getTemplate()->getPerUnitSound( "UnitUnpack" );
	sound.setObjectID( owner->getID() );
	TheAudio->addAudioEvent( &sound );
	
	Real variationFactor = ai->getPackUnpackVariationFactor();
	Real variation = GameLogicRandomValueReal( 1.0f - variationFactor, 1.0f + variationFactor );
	m_framesRemaining = ai->getUnpackTime() * variation; //In frames
	owner->getDrawable()->setAnimationLoopDuration( m_framesRemaining );

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType UnpackingState::update()
{
	Object *owner = getMachineOwner();
//	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	// This is a bit hacky, no pun intended, but if this Update is engeged specialability (disablebuilding)
	// The unpacking modelconditionflag gets cleared by specialability::cleanup() after my onEnter() sets it!
	// Why HackInterent wasn't included within specialability I can't figure out, but... too late to change now.
	owner->setModelConditionState( MODELCONDITION_UNPACKING );

	if( m_framesRemaining > 0 )
	{
		m_framesRemaining--;
	}
	else
	{
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void UnpackingState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_UNPACKING );
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void PackingState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void PackingState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void PackingState::loadPostProcess( void )
{
}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
StateReturnType PackingState::onEnter() 
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	owner->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_FIRING_A ), 
																				 MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );

	AudioEventRTS sound = *owner->getTemplate()->getPerUnitSound( "UnitPack" );
	sound.setObjectID( owner->getID() );
	TheAudio->addAudioEvent( &sound );
	
	Real variationFactor = ai->getPackUnpackVariationFactor();
	Real variation = GameLogicRandomValueReal( 1.0f - variationFactor, 1.0f + variationFactor );
	m_framesRemaining = ai->getPackTime() * variation; //In frames
	owner->getDrawable()->setAnimationLoopDuration( m_framesRemaining );
	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType PackingState::update()
{
//	Object *owner = getMachineOwner();
//	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();

	if( m_framesRemaining > 0 )
	{
		m_framesRemaining--;
	}
	else
	{
		return STATE_SUCCESS;
	}

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void PackingState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_PACKING );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HackInternetState::crc( Xfer *xfer )
{
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void HackInternetState::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	xfer->xferUnsignedInt(&m_framesRemaining);
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HackInternetState::loadPostProcess( void )
{
}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
StateReturnType HackInternetState::onEnter()
{
	//Go into the hack internet stance.
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}
	
	owner->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ), 
																				 MAKE_MODELCONDITION_MASK( MODELCONDITION_FIRING_A ) );

	m_framesRemaining = ai->getCashUpdateDelay();
	ai->setHackProgress( 0.0f );

	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
StateReturnType HackInternetState::update()
{
	Object *owner = getMachineOwner();
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( !ai )
	{
		return STATE_FAILURE;
	}

	if( owner->isDisabledByType( DISABLED_HACKED ) )
	{
		//Don't hack while hacked, hehe.
		return STATE_CONTINUE;
	}

	//
	// Where the next payout has got to, so a bar can fill over the hacker's head (see
	// Drawable::drawHealthBar). The delay is the only clock the player has on this and it was
	// entirely invisible - a hacker either dropped cash or looked idle.
	//
	{
		UnsignedInt full = ai->getCashUpdateDelay();
		ai->setHackProgress( full > 0 ? 1.0f - INT_TO_REAL( m_framesRemaining ) / INT_TO_REAL( full )
																	: -1.0f );
	}

	if( m_framesRemaining > 0 )
	{
		//Decrement frame counter.
		m_framesRemaining--;
	}
	else
	{
		//We have waited the full amount of the delay, so hack some cash from the heavens!
		
		//Add cash
		Money *money = owner->getControllingPlayer()->getMoney();
		if( money )
		{
			ExperienceTracker *xp = owner->getExperienceTracker();
			if( xp )
			{
				UnsignedInt amount = 0;
				switch( xp->getVeterancyLevel() )
				{
					case LEVEL_HEROIC:
						amount = ai->getHeroicCashAmount();
						if( amount )
						{
							break;
						}
						//If entry missing, fall through!
					case LEVEL_ELITE:
						amount = ai->getEliteCashAmount();
						if( amount )
						{
							break;
						}
						//If entry missing, fall through!
					case LEVEL_VETERAN:
						amount = ai->getVeteranCashAmount();
						if( amount )
						{
							break;
						}
						//If entry missing, fall through!
					case LEVEL_REGULAR:
						amount = ai->getRegularCashAmount();
						if( amount )
						{
							break;
						}
						//If entry missing, fall through!
					default:
						amount = 1;
						break;
				}
				owner->getControllingPlayer()->earnIncome( amount, FALSE );

				//Grant the unit some experience for a successful hack.
				xp->addExperiencePoints( ai->getXpPerCashUpdate() );

				// OY LOOK!  I AM USING LOCAL PLAYER.  Do not put anything other than TheInGameUI->addFloatingText in the block this controls!!!
				// Whether the figure shows is whether this watcher can see the hacker.  Spelling the
				// rule out as "mine, or not stealthed, or detected" reads the same for a player but
				// hides it from an ally who shares the sight and from an observer who sees everything.
				//
				// A hacker working from inside an Internet Center has no drawable on screen at all -
				// a garrisoned unit is hidden by its container - so asking the hacker whether it can
				// be seen answered no for the whole time the money was coming in.  isLogicallyVisible
				// asks about the thing on screen, which is the building.
				Object *internetCenter = owner->getContainedBy();
				Bool displayMoney = owner->isLogicallyVisible();

				if( displayMoney )
				{
					// OY LOOK!  I AM USING LOCAL PLAYER.  Do not put anything other than TheInGameUI->addFloatingText in the block this controls!!!
					//Display cash income floating over the hacker.
					UnicodeString moneyString;
					moneyString.format( TheGameText->fetch( "GUI:AddCash" ), amount );
					// client text: leaves fixed point here
					Coord3D pos = owner->getPositionFix()->toCoord3D();
					pos.z += 20.0f; //add a little z to make it show up above the unit.


          if ( internetCenter )
          {
            Real width = fixToReal( internetCenter->getGeometryInfo().getMajorRadiusFix() ) * 0.3f;
            Real depth = fixToReal( internetCenter->getGeometryInfo().getMinorRadiusFix() ) * 0.3f;
            pos.x += GameClientRandomValue(-width,width);
            pos.y += GameClientRandomValue(-depth,depth);
          }


					TheInGameUI->addFloatingText( moneyString, &pos, GameMakeColor( 0, 255, 0, 255 ) );
				}

				AudioEventRTS sound = *(owner->getTemplate()->getPerUnitSound( "UnitCashPing" ));
				sound.setObjectID( owner->getID() );
				TheAudio->addAudioEvent( &sound );
			}
		}


		//Reset timer and start a new cycle.
		m_framesRemaining = ai->getCashUpdateDelay();
		
	}

	//This is a persistent state until told otherwise.
	return STATE_CONTINUE;
}

//-------------------------------------------------------------------------------------------------
void HackInternetState::onExit( StateExitType status )
{
	Object *owner = getMachineOwner();
	owner->clearModelConditionState( MODELCONDITION_FIRING_A );

	// packed up: no payout is on the way, so take the bar down with the animation
	HackInternetAIUpdate *ai = (HackInternetAIUpdate*)owner->getAIUpdateInterface();
	if( ai )
		ai->setHackProgress( -1.0f );
}
