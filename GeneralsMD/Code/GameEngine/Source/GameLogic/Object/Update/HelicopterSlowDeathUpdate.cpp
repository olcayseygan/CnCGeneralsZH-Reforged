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

// FILE: HelicopterSlowDeathBehavior.cpp ////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Helicoptor slow deaths
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/EjectPilotDie.h"
#include "GameLogic/Module/HelicopterSlowDeathUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "Lib/FixBoundary.h"

//-------------------------------------------------------------------------------------------------
// Matrix3D::In_Place_Pre_Rotate_Z in fixed point: the rotation part turns about the world's z, the
// translation stays where it is
static void preRotateZ( FixMatrix3D &m, Fix theta )
{
	const Fix c = fixCos( theta );
	const Fix s = fixSin( theta );
	for( Int col = 0; col < 3; ++col )
	{
		const Fix a = m.m[ 0 ][ col ];
		const Fix b = m.m[ 1 ][ col ];
		m.m[ 0 ][ col ] = c * a - s * b;
		m.m[ 1 ][ col ] = s * a + c * b;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Helicopter slow death update module data ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HelicopterSlowDeathBehaviorModuleData::HelicopterSlowDeathBehaviorModuleData( void )
{
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_attachParticleBone.clear();
	m_bladeBone.clear();
	m_bladeObjectName.clear();
	m_finalRubbleObject.clear();
	//

	m_spiralOrbitTurnRate = Fix( 0 );
	m_spiralOrbitForwardSpeed = Fix( 0 );
	m_spiralOrbitForwardSpeedDamping = Fix( 1 );  // no damping
	m_minSelfSpin = Fix( 0 );
	m_maxSelfSpin = Fix( 0 );
	m_selfSpinUpdateDelay = Fix( 0 );
	m_selfSpinUpdateAmount = Fix( 0 );
	m_fallHowFast = Fix( 0 );
	m_minBladeFlyOffDelay = Fix( 0 );
	m_maxBladeFlyOffDelay = Fix( 0 );

	m_attachParticleSystem = NULL;
	m_attachParticleLoc.x = 0.0f;
	m_attachParticleLoc.y = 0.0f;
	m_attachParticleLoc.z = 0.0f;
	m_oclEjectPilot = NULL;
	m_fxBlade = NULL;
	m_oclBlade = NULL;
	m_fxHitGround = NULL;
	m_oclHitGround = NULL;
	m_fxFinalBlowUp = NULL;
	m_oclFinalBlowUp = NULL;
	m_delayFromGroundToFinalDeath = Fix( 0 );
	m_maxBraking = Fix( 99999 );

}  // end HelicopterSlowDeathBehaviorModuleData

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void HelicopterSlowDeathBehaviorModuleData::buildFieldParse( MultiIniFieldParse &p ) 
{
  SlowDeathBehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "SpiralOrbitTurnRate",	INI::parseAngularVelocityFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_spiralOrbitTurnRate ) },
		{ "SpiralOrbitForwardSpeed", INI::parseVelocityFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_spiralOrbitForwardSpeed ) },
		{ "SpiralOrbitForwardSpeedDamping", INI::parseFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_spiralOrbitForwardSpeedDamping ) },
		{ "MinSelfSpin", INI::parseAngularVelocityFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_minSelfSpin ) },
		{ "MaxSelfSpin", INI::parseAngularVelocityFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_maxSelfSpin ) },
		{ "SelfSpinUpdateDelay", INI::parseDurationFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_selfSpinUpdateDelay ) },
		{ "SelfSpinUpdateAmount", INI::parseAngleFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_selfSpinUpdateAmount ) },
		{ "FallHowFast", INI::parsePercentToFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_fallHowFast ) },
		{ "MinBladeFlyOffDelay", INI::parseDurationFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_minBladeFlyOffDelay ) },
		{ "MaxBladeFlyOffDelay", INI::parseDurationFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_maxBladeFlyOffDelay ) },
		{ "AttachParticle", INI::parseParticleSystemTemplate, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_attachParticleSystem ) },
		{ "AttachParticleBone", INI::parseAsciiString, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_attachParticleBone ) },
		{ "AttachParticleLoc", INI::parseCoord3D, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_attachParticleLoc ) },
		{ "BladeObjectName", INI::parseAsciiString, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_bladeObjectName ) },
		{ "BladeBoneName", INI::parseAsciiString, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_bladeBone ) },
		{ "OCLEjectPilot", INI::parseObjectCreationList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_oclEjectPilot ) },
		{ "FXBlade", INI::parseFXList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_fxBlade ) },
		{ "OCLBlade", INI::parseObjectCreationList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_oclBlade ) },
		{ "FXHitGround", INI::parseFXList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_fxHitGround ) },
		{ "OCLHitGround", INI::parseObjectCreationList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_oclHitGround ) },
		{ "FXFinalBlowUp", INI::parseFXList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_fxFinalBlowUp ) },
		{ "OCLFinalBlowUp", INI::parseObjectCreationList, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_oclFinalBlowUp ) },
		{ "DelayFromGroundToFinalDeath", INI::parseDurationFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_delayFromGroundToFinalDeath ) },
		{ "FinalRubbleObject", INI::parseAsciiString, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_finalRubbleObject ) },
		{ "SoundDeathLoop", INI::parseAudioEventRTS, NULL, offsetof( HelicopterSlowDeathBehaviorModuleData, m_deathSound) },
		{ "MaxBraking", INI::parseAccelerationFix, NULL, FIX_OFFSET( HelicopterSlowDeathBehaviorModuleData, m_maxBraking) },

		{ 0, 0, 0, 0 }

	};

  p.add(dataFieldParse);

}  // end buildFieldParse

///////////////////////////////////////////////////////////////////////////////////////////////////
// Helicopter slow death update ///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//
// we use these to guide the direction of the circular orbit, as well as the direction the
// object self spins
//
#define ORBIT_DIRECTION_LEFT  ( 1.0f)
#define ORBIT_DIRECTION_RIGHT (-1.0f)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HelicopterSlowDeathBehavior::HelicopterSlowDeathBehavior( Thing *thing, const ModuleData *moduleData )
													: SlowDeathBehavior( thing, moduleData )
{

	m_orbitDirection = ORBIT_DIRECTION_LEFT;
	m_forwardAngle = 0.0f;
	m_forwardSpeed = 0.0f;
	m_selfSpin = 0.0f;
	m_selfSpinTowardsMax = FALSE;
	m_lastSelfSpinUpdateFrame = 0;
	m_bladeFlyOffFrame = 0;
	m_hitGroundFrame = 0;

}  // end HelicopterSlowDeathBehavior

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
HelicopterSlowDeathBehavior::~HelicopterSlowDeathBehavior( void )
{

}  // end ~HelicopterSlowDeathBehavior

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void HelicopterSlowDeathBehavior::beginSlowDeath( const DamageInfo *damageInfo )
{

	// extending functionality
	SlowDeathBehavior::beginSlowDeath( damageInfo );

	// stop the current movement sound
	if (getObject()->getDrawable()) 
	{
		getObject()->getDrawable()->stopAmbientSound();
	}

	// get the module data
	const HelicopterSlowDeathBehaviorModuleData *modData = getHelicopterSlowDeathBehaviorModuleData();

	m_deathSound = modData->m_deathSound;
	
	if (m_deathSound.getEventName().isEmpty() == false)
	{
		m_deathSound.setObjectID(getObject()->getID());
		m_deathSound.setPlayingHandle(TheAudio->addAudioEvent(&m_deathSound));
	}


	// pick a frame we will fly the blade off at
	const Fix bladeFlyOffDelay = GameLogicRandomValueFix( modData->m_minBladeFlyOffDelay,
																												modData->m_maxBladeFlyOffDelay );
	m_bladeFlyOffFrame = (UnsignedInt)( bladeFlyOffDelay.raw() >> Fix::FRAC_BITS );

	// for now, make it always fall to the left
	m_orbitDirection = ORBIT_DIRECTION_LEFT;

	//
	// record the current forward angle of the object to be the start angle of our downward
	// spin circle angle
	//
	m_forwardAngle = fixToReal( getObject()->getOrientationFix() );	// P4: it steers a physics force

	// set or forward speed in the spiral orbit speed to that specified
	m_forwardSpeed = fixToReal( modData->m_spiralOrbitForwardSpeed );	// P4: the spiral state is xfer'd float

	// start our self spinning at the min self spin rate
	m_selfSpin = fixToReal( modData->m_minSelfSpin );	// P4: the spiral state is xfer'd float
	
	// we will start off changing the self spin towards the MaxSelfSpin
	m_selfSpinTowardsMax = TRUE;

	// the fall of the helicopter
	if( !getObject()->getAIUpdateInterface() )
	{
		return;
	}
	Locomotor *locomotor = getObject()->getAIUpdateInterface()->getCurLocomotor();
	locomotor->setMaxLift( -TheGlobalData->m_gravity * fixToReal( Fix( 1 ) - modData->m_fallHowFast ) );	// P4: locomotor
	locomotor->setMaxBraking( fixToReal( modData->m_maxBraking ) );	// P4: locomotor

	// attach particle system to bone if present
	if( modData->m_attachParticleSystem )
	{
		ParticleSystem *pSys = TheParticleSystemManager->createParticleSystem( modData->m_attachParticleSystem );
		if( pSys )
		{
			
			// where do the offset attachment to
			if( modData->m_attachParticleBone.isEmpty() == FALSE )
			{
				Drawable *draw = getObject()->getDrawable();

				if( draw )
				{
					Coord3D pos;

					if( draw->getPristineBonePositions( modData->m_attachParticleBone.str(), 0, &pos, NULL, 1 ) )
						pSys->setPosition( &pos );

				}  // end if

			}  // end if
			else
			{

				// use location coord specified ... it will be zero if not given which is center of obj anyway
				pSys->setPosition( &modData->m_attachParticleLoc );

			}  // end else

			// attach the particle system to the object
			pSys->attachToObject( getObject() );

		}  // end if

	}  // end if



}  // end beginSlowDeath




//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime HelicopterSlowDeathBehavior::update( void )
{
/// @todo srj use SLEEPY_UPDATE here
	// call the base class cause we're extending functionality
	SlowDeathBehavior::update();

	// get out of here if we're not activated yet
	if( isSlowDeathActivated() == FALSE )
		return UPDATE_SLEEP_NONE;

	// get the module data
	const HelicopterSlowDeathBehaviorModuleData *modData = getHelicopterSlowDeathBehaviorModuleData();

	// get the helicopter for easy access in here
	Object *copter = getObject();

	// update self spin and orbit if we haven't hit the ground yet
	if( m_hitGroundFrame == 0 )
	{

		//
		// change the angle of the helicoptor based on the spiral rate for the turning on the
		// large downward spin circle
		//

		//copter->setOrientation( copter->getOrientation() + m_selfSpin * m_orbitDirection );

		FixMatrix3D xfrm = *copter->getTransformMatrixFix();
		preRotateZ( xfrm, fixFromReal( m_selfSpin * m_orbitDirection ) );	// P4: the spin is xfer'd float state
		copter->setTransformMatrixFix( &xfrm );

		//
		// over time we change the rate at which we self spin around our center of gravity ... we
		// will ping pong back and forth between the MinSelfSpin and MaxSelfSpin defined in INI
		//
		if( modData->m_selfSpinUpdateDelay != Fix( 0 ) &&
				Fix( (Int)( TheGameLogic->getFrame() - m_lastSelfSpinUpdateFrame ) ) > modData->m_selfSpinUpdateDelay )
		{
			// P4: the spin is xfer'd float state
			const Real spinStep = fixToReal( modData->m_selfSpinUpdateAmount ) / LOGICFRAMES_PER_SECOND;
			const Real maxSelfSpin = fixToReal( modData->m_maxSelfSpin );
			const Real minSelfSpin = fixToReal( modData->m_minSelfSpin );

			// update the self spin
			if( m_selfSpinTowardsMax == TRUE )
			{

				// we're going towards the max self spin, increase it
				m_selfSpin += spinStep;
				if( m_selfSpin > maxSelfSpin )
				{

					m_selfSpin = maxSelfSpin;  // cap at max
					m_selfSpinTowardsMax = FALSE;					// now start changing spin towards min again

				}  // end if

			}  // end if
			else
			{
			
				// we're going towards the min self spin, decrease it
				m_selfSpin -= spinStep;
				if( m_selfSpin < minSelfSpin )
				{

					m_selfSpin = minSelfSpin;  // cap at min
					m_selfSpinTowardsMax = TRUE;					// now start chaning spin towards max again

				}  // end if

			}  // end else

			// we have made a change to the self spinning
			m_lastSelfSpinUpdateFrame = TheGameLogic->getFrame();

		}  // end if

		// get the physics update module
		PhysicsBehavior *physics = copter->getPhysics();
		DEBUG_ASSERTCRASH( physics, ("HelicopterSlowDeathBehavior: object '%s' does not have a physics module\n", 
																 copter->getTemplate()->getName().str()) );

		//
		// apply a force to the helicopter pushing it in a forward motion	according to the
		// forward angle & speed we are keeping track of to make our downward circle (that forward angle
		// is *NOT* the angle the object is facing
		//
		Coord3D force;
		force.x = DOUBLE_TO_REAL( Cos( m_forwardAngle ) ) * m_forwardSpeed;
		force.y = DOUBLE_TO_REAL( Sin( m_forwardAngle ) ) * m_forwardSpeed;
		force.z = 0.0f;
		physics->applyMotiveForce( &force );

		// update our forward angle for travelling along the large spiral downward circle
		m_forwardAngle += (fixToReal( modData->m_spiralOrbitTurnRate ) * m_orbitDirection);	// P4: the spiral state is xfer'd float

		// adjust our forward spiral orbit by the damping factor specified
		m_forwardSpeed *= fixToReal( modData->m_spiralOrbitForwardSpeedDamping );	// P4: the spiral state is xfer'd float

		// is it time to have the blade fly off
		if( m_bladeFlyOffFrame > 0 )
		{
			// decrement count
			m_bladeFlyOffFrame--;
			if( m_bladeFlyOffFrame <= 0 )
			{
				// the bone is the client's and the FX and OCL take a float position
				Coord3D bladePos = copter->getPositionFix()->toCoord3D();

				// get the blade position from the bone in the model
				Drawable *draw = copter->getDrawable();
				if( draw )
				{

					draw->getPristineBonePositions( modData->m_bladeBone.str(), 0, &bladePos, NULL, 1 );
					bladePos = copter->convertBonePosToWorldPosFix( bladePos ).toCoord3D();

				}  // end if
		
				// create the blades flying through the air
	//			const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList( "OCL_ComancheBlades" );
	//			ObjectCreationList::create( ocl, &bladePos );
		
				// run the fx at the blade position
				FXList::doFXPos( modData->m_fxBlade, &bladePos );
				ObjectCreationList::create( modData->m_oclBlade, copter, &bladePos, NULL, INVALID_ANGLE );

				//				
				// if we have (potentially) a pilot ejection, do it here.
				// note that we call EjectPilotDie::ejectPilot() rather than ObjectCreationList::create(),
				// because the former makes the right sounds, and also constrains to veteran-or-better status.
				//
				if( modData->m_oclEjectPilot && copter->getVeterancyLevel() > LEVEL_REGULAR )
					EjectPilotDie::ejectPilot( modData->m_oclEjectPilot, copter, NULL );

			}  // end if

		}  // endif

	}  // end if, not on ground



	Bool hitATree = FALSE;
	// Here we want to make sure we crash if we collide with a tree on the way down
	PhysicsBehavior *phys = copter->getPhysics();
	if ( m_hitGroundFrame == 0 && phys )
	{
		ObjectID treeID = phys->getLastCollidee();
		Object *tree = TheGameLogic->findObjectByID( treeID );
		if ( tree )
		{
			if (tree->isKindOf( KINDOF_SHRUBBERY ) )
			hitATree = TRUE;
		}
	}



	// when we hit the ground
	const FCoord3D *pos = copter->getPositionFix();
	if (m_hitGroundFrame == 0)
	{
		// srj sez: if we haven't yet hit the ground, adjust our layer properly so we crash on bridges
		FCoord3D tmpPt = *pos;
		tmpPt.z = Fix( 99999 );
		PathfindLayerEnum newLayer = TheTerrainLogic->getHighestLayerForDestinationFix(&tmpPt);
		copter->setLayer(newLayer);

		Fix ground = TheTerrainLogic->getLayerHeightFix( pos->x, pos->y, newLayer );

		if (pos->z <= ground + Fix( 1 ) || hitATree )
		{
			
			// mark the frame we hit the ground on
			m_hitGroundFrame = TheGameLogic->getFrame();

			// A copter down before its blades flew off never reached the ejection above, which only
			// runs in the air: a veteran Comanche shot down low lost its pilot (upstream #175).
			if( m_bladeFlyOffFrame > 0 )
			{
				m_bladeFlyOffFrame = 0;
				if( modData->m_oclEjectPilot && copter->getVeterancyLevel() > LEVEL_REGULAR )
					EjectPilotDie::ejectPilot( modData->m_oclEjectPilot, copter, NULL );
			}

			// make hit ground effect
			FXList::doFXObj( modData->m_fxHitGround, copter );
			ObjectCreationList::create( modData->m_oclHitGround, copter, NULL );

			// hold the copter in place now
			copter->setDisabled( DISABLED_HELD );

			// special damage state ... has the blades no on anymore
			Drawable *draw = copter->getDrawable();
			if( draw )
				draw->setModelConditionState( MODELCONDITION_SPECIAL_DAMAGED );

			// Stop the sound from playing.
			TheAudio->removeAudioEvent(m_deathSound.getPlayingHandle());

		}  // end if
	}

	// if we're on the ground, see if it's time for our final boom
	if( m_hitGroundFrame && 
			Fix( (Int)( TheGameLogic->getFrame() - m_hitGroundFrame ) ) > modData->m_delayFromGroundToFinalDeath )
	{

		// make effect
		FXList::doFXObj( modData->m_fxFinalBlowUp, copter );
		ObjectCreationList::create( modData->m_oclFinalBlowUp, copter, NULL );

		// we are now all done, destroy us and make a rubble shell copter
		const ThingTemplate* ttn = TheThingFactory->findTemplate(modData->m_finalRubbleObject);
		Object *rubble = TheThingFactory->newObject( ttn, copter->getTeam() );
		if( rubble )
		{

			rubble->setTransformMatrixFix( copter->getTransformMatrixFix() );

		}  // end if

		// destroy the copter finally
		TheGameLogic->destroyObject( copter );

	}  // end if

	return UPDATE_SLEEP_NONE;

}  // end update

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void HelicopterSlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class
	SlowDeathBehavior::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void HelicopterSlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SlowDeathBehavior::xfer( xfer );

	// orbit direction
	xfer->xferInt( &m_orbitDirection );

	// forward angle
	xfer->xferReal( &m_forwardAngle );

	// forward speed
	xfer->xferReal( &m_forwardSpeed );

	// self spin
	xfer->xferReal( &m_selfSpin );

	// self sping towards max
	xfer->xferBool( &m_selfSpinTowardsMax );

	// last self spin update frame
	xfer->xferUnsignedInt( &m_lastSelfSpinUpdateFrame );

	// blade fly off frame
	xfer->xferUnsignedInt( &m_bladeFlyOffFrame );

	// hit ground frame
	xfer->xferUnsignedInt( &m_hitGroundFrame );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void HelicopterSlowDeathBehavior::loadPostProcess( void )
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

	// the death loop plays from the start of the spiral until the ground; a save carries neither the
	// event nor its handle, so a copter loaded mid-spiral fell in silence
	if( isSlowDeathActivated() && m_hitGroundFrame == 0 )
	{
		m_deathSound = getHelicopterSlowDeathBehaviorModuleData()->m_deathSound;
		if( m_deathSound.getEventName().isEmpty() == false )
		{
			m_deathSound.setObjectID( getObject()->getID() );
			m_deathSound.setPlayingHandle( TheAudio->addAudioEvent( &m_deathSound ) );
		}
	}

}  // end loadPostProcess
