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

// FILE: JetSlowDeathBehavior.cpp /////////////////////////////////////////////////////////////////
// Author: Colin Day
// Desc:   Death sequence for jets
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/JetSlowDeathBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehaviorModuleData::JetSlowDeathBehaviorModuleData( void )
{

	m_fxOnGroundDeath = NULL;
	m_oclOnGroundDeath = NULL;

	m_fxInitialDeath = NULL;
	m_oclInitialDeath = NULL;

	m_delaySecondaryFromInitialDeath = 0;
	m_fxSecondary = NULL;
	m_oclSecondary = NULL;

	m_fxHitGround = NULL;
	m_oclHitGround = NULL;

	m_delayFinalBlowUpFromHitGround = 0;
	m_fxFinalBlowUp = NULL;
	m_oclFinalBlowUp = NULL;

	m_rollRate = 0.0f;
	m_rollRateDelta = 1.0f;
	m_pitchRate = 0.0f;
	m_fallHowFast = 0.0f;

}  // end JetSlowDeathBehaviorModuleData

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void JetSlowDeathBehaviorModuleData::buildFieldParse( MultiIniFieldParse &p )
{
  SlowDeathBehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{

		{ "FXOnGroundDeath",	INI::parseFXList,	NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fxOnGroundDeath ) },
		{ "OCLOnGroundDeath", INI::parseObjectCreationList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_oclOnGroundDeath ) },

		{ "FXInitialDeath",	INI::parseFXList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fxInitialDeath ) },
		{ "OCLInitialDeath", INI::parseObjectCreationList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_oclInitialDeath ) },

		{ "DelaySecondaryFromInitialDeath",	INI::parseDurationUnsignedInt, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_delaySecondaryFromInitialDeath ) },
		{ "FXSecondary",	INI::parseFXList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fxSecondary ) },
		{ "OCLSecondary", INI::parseObjectCreationList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_oclSecondary ) },

		{ "FXHitGround", INI::parseFXList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fxHitGround ) },
		{ "OCLHitGround", INI::parseObjectCreationList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_oclHitGround ) },

		{ "DelayFinalBlowUpFromHitGround", INI::parseDurationUnsignedInt, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_delayFinalBlowUpFromHitGround ) },
		{ "FXFinalBlowUp", INI::parseFXList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fxFinalBlowUp ) },
		{ "OCLFinalBlowUp", INI::parseObjectCreationList, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_oclFinalBlowUp ) },

		{ "DeathLoopSound", INI::parseAudioEventRTS, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_deathLoopSound ) },

// @todo srj -- RollRate and RollRateDelta and PitchRate should use parseAngularVelocityReal
		{ "RollRate",	INI::parseReal, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_rollRate ) },
		{ "RollRateDelta", INI::parsePercentToReal, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_rollRateDelta ) },
		{ "PitchRate", INI::parseReal, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_pitchRate ) },
		{ "FallHowFast", INI::parsePercentToReal, NULL, offsetof( JetSlowDeathBehaviorModuleData, m_fallHowFast ) },

		{ 0, 0, 0, 0 }

	};

  p.add( dataFieldParse );

}  // end buildFieldParse

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehavior::JetSlowDeathBehavior( Thing *thing, const ModuleData *moduleData )
										: SlowDeathBehavior( thing, moduleData )
{

	m_timerDeathFrame = 0;
	m_timerOnGroundFrame = 0;
	m_fallDeadlineFrame = 0;
	m_rollRate = 0.0f;

}  // end JetSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
JetSlowDeathBehavior::~JetSlowDeathBehavior( void )
{

}  // end ~JetSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::onDie( const DamageInfo *damageInfo )
{
	Object *us = getObject();
	
	// if the jet is on the ground we do just our ground fx death
	if( us->isSignificantlyAboveTerrain() == FALSE || us->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
	{
		const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();
		
		// execute fx
		FXList::doFXObj( modData->m_fxOnGroundDeath, us );

		// execute ocl
		ObjectCreationList::create( modData->m_oclOnGroundDeath, us, NULL );

		// destroy object
		TheGameLogic->destroyObject( us );

	}  // end if
	else
	{

		// extend base class for slow death and begin the slow death behavior
		SlowDeathBehavior::onDie( damageInfo );

	}  // end else

	getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) );

}  // end onDie

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::beginSlowDeath( const DamageInfo *damageInfo )
{

	// extend functionality
	SlowDeathBehavior::beginSlowDeath( damageInfo );

	// get our info
	Object *us = getObject();
	const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();

	// record the frame we died on
	m_timerDeathFrame = TheGameLogic->getFrame();

	// ... and the latest the fall is allowed to hold the fireball back to
	const UnsignedInt MAX_FALL_SECONDS = 15;
	m_fallDeadlineFrame = m_timerDeathFrame + MAX_FALL_SECONDS * LOGICFRAMES_PER_SECOND;

	// do some effects
	FXList::doFXObj( modData->m_fxInitialDeath, us );
	ObjectCreationList::create( modData->m_oclInitialDeath, us, NULL );

	// start audio loop playing
	m_deathLoopSound = modData->m_deathLoopSound;
	if( m_deathLoopSound.getEventName().isEmpty() == FALSE )
	{

		m_deathLoopSound.setObjectID( us->getID() );
		m_deathLoopSound.setPlayingHandle( TheAudio->addAudioEvent( &m_deathLoopSound ) );

	}  // end if

	// initialize our roll rate to that defined as the initial value in the module data
	m_rollRate = modData->m_rollRate;

	/* A wing does not stop working the moment the engine behind it stops. The lift used to drop
		 straight to the module's FallHowFast on the frame of the kill, so a plane doing two hundred
		 knots fell out of the sky on the spot and hit the ground almost under where it was shot,
		 which is the one thing a plane cannot do. It keeps its lift here and gives it up over the
		 next couple of seconds in update(), so it carries on forward first and noses over after. */
	Locomotor *locomotor = us->getAIUpdateInterface()->getCurLocomotor();
	locomotor->setMaxLift( -TheGlobalData->m_gravity );

	// do not allow the jet to turn anymore
	locomotor->setMaxTurnRate( 0.0f );

}  // end beginSlowDeath

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime JetSlowDeathBehavior::update( void )
{

	// extend functionality of base class
	SlowDeathBehavior::update();

	// if the death is not activated, do nothing else
	if( isSlowDeathActivated() == FALSE )
		return UPDATE_SLEEP_NONE;

	// get object info
	Object *us = getObject();
	const JetSlowDeathBehaviorModuleData *modData = getJetSlowDeathBehaviorModuleData();

	// roll us around in the air
	PhysicsBehavior *physics = us->getPhysics();
	DEBUG_ASSERTCRASH( physics, ("JetSlowDeathBehavior::beginSlowDeath - '%s' has no physics\n",
															us->getTemplate()->getName().str()) );
	if( physics )
		physics->setRollRate( m_rollRate );

	// adjust the roll rate over time
	m_rollRate *= modData->m_rollRateDelta;

	// do effects for death while in the air
	if( m_timerOnGroundFrame == 0 )
	{
		/* Bleed the lift away towards what the module data asked for. A tenth of the gap a frame
			 puts the plane at the falling speed the data names about two seconds after it is hit,
			 having flown a plane's length or three further forward on the way.

			 With a floor under it, because FallHowFast defaults to nought and the delivery aircraft -
			 the B-52, the Spectre gunship, the cargo plane, the A-10 - never set it. Nought means the
			 lift exactly cancels gravity, so those four did not fall at all: they flew on at cruising
			 height, burning, until the destruction timer went off and blew them up in mid-air.  Half
			 of gravity is a plane coming down, which is the only thing a dead plane does. */
		AIUpdateInterface *ai = us->getAIUpdateInterface();
		Locomotor *locomotor = ai ? ai->getCurLocomotor() : NULL;
		if( locomotor )
		{
			const Real LIFT_BLEED_PER_FRAME = 0.1f;
			const Real MIN_FALL_FRACTION = 0.5f;
			const Real fallFraction = max( MIN_FALL_FRACTION, modData->m_fallHowFast );
			const Real fallingLift = -TheGlobalData->m_gravity * (1.0f - fallFraction);
			const Real currentLift = locomotor->getMaxLift( us->getBodyModule()->getDamageState() );
			locomotor->setMaxLift( currentLift + (fallingLift - currentLift) * LIFT_BLEED_PER_FRAME );

			/* Ultra-accurate flying may take three times the lift, which is still more than gravity,
				 and a delivery plane flies it all the way off the map once its load is down; a jet on
				 final approach does too. Shot down on the way home, a B-52 burned and blew apart at
				 cruising height. Its AI keeps running while it falls and can switch the mode back on,
				 so it goes off every frame. */
			locomotor->setUltraAccurate( FALSE );
		}

		/* And the explosion waits for the ground. The base class destroys the object on a timer
			 whatever is happening to it, which for a plane still in the air is the mid-air blast again
			 by another route; the fireball belongs where the airframe lands. Held off only up to the
			 deadline set when it died, so one that never arrives - shot down over the edge of the
			 world, or held up by something driving its locomotor - still goes away. */
		const UnsignedInt now = TheGameLogic->getFrame();
		if( now < m_fallDeadlineFrame && now + 1 >= getDestructionFrame() )
			setDestructionFrame( now + 2 );

		const FCoord3D *usPos = us->getPositionFix();
		Coord3D layerPos = usPos->toCoord3D(); // P5
		PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(&layerPos);
		us->setLayer(layer);
		Fix height;
		if (layer == LAYER_GROUND)
		{
			// (this is more efficient than getGroundHeight because the info is cached)
			height = us->getHeightAboveTerrainFix();
		}
		else
		{
			Fix layerHeight = TheTerrainLogic->getLayerHeightFix( usPos->x, usPos->y, layer );
			height = usPos->z - layerHeight;
			// slop a little bit for bridges, since we tend to end up fractionally
			// above 'em, and it's easier to just slop it here
			if (height >= Fix( 0 ) && height <= Fix( 1 ))
				height = Fix( 0 );
		}

		

		Bool hitATree = FALSE;
		// Here we want to make sure we crash if we collide with a tree on the way down
		PhysicsBehavior *phys = us->getPhysics();
		if ( m_timerOnGroundFrame == 0 && phys )
		{
			ObjectID treeID = phys->getLastCollidee();
			Object *tree = TheGameLogic->findObjectByID( treeID );
			if ( tree )
			{
				if (tree->isKindOf( KINDOF_SHRUBBERY ) )
				hitATree = TRUE;
			}
		}



		// when we've hit the ground, we're totally done
		if( height <= Fix( 0 ) || hitATree )
		{

			// stop the death looping sound at the right time
			TheAudio->removeAudioEvent( m_deathLoopSound.getPlayingHandle() );

			// do some effects
			FXList::doFXObj( modData->m_fxHitGround, us );
			ObjectCreationList::create( modData->m_oclHitGround, us, NULL );

			// we are now on the ground
			m_timerOnGroundFrame = TheGameLogic->getFrame();

			// start us rolling on another axis too
			if( physics )
				physics->setPitchRate( modData->m_pitchRate );

		}  // end if

		// timers for the secondary effect
		if( m_timerDeathFrame != 0 && 
				TheGameLogic->getFrame() - m_timerDeathFrame >= modData->m_delaySecondaryFromInitialDeath )
		{

			// do some effects
			FXList::doFXObj( modData->m_fxSecondary, us );
			ObjectCreationList::create( modData->m_oclSecondary, us, NULL );

			// clear the death frame timer since we've already executed the event now
			m_timerDeathFrame = 0;

		}  //end if

	}  // end if
	else
	{
		// we are on the ground, pay attention to the final explosion timers
		if( TheGameLogic->getFrame() - m_timerOnGroundFrame >= modData->m_delayFinalBlowUpFromHitGround )
		{

			// do some effects
			FXList::doFXObj( modData->m_fxFinalBlowUp, us );
			ObjectCreationList::create( modData->m_oclFinalBlowUp, us, NULL );

			// we're all done now
			TheGameLogic->destroyObject( us );

		}  // end if

	}  // end else

	return UPDATE_SLEEP_NONE;

}  // end update

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class 
	SlowDeathBehavior::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the deadline the fall may hold the explosion back to */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SlowDeathBehavior::xfer( xfer );

	// timer death frame
	xfer->xferUnsignedInt( &m_timerDeathFrame );

	// on ground frame
	xfer->xferUnsignedInt( &m_timerOnGroundFrame );

	// roll rate
	xfer->xferReal( &m_rollRate );

	// how long the fall may hold the explosion back for; a version 1 save has no such deadline, so
	// a plane loaded in mid-fall blows up on the timer it was saved with
	if( version >= 2 )
		xfer->xferUnsignedInt( &m_fallDeadlineFrame );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JetSlowDeathBehavior::loadPostProcess( void )
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}  // end loadPostProcess
