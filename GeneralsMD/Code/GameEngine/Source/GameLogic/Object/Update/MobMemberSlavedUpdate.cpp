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

///////////////////////////////////////////////////////////////////////////////////////////////////
// MobMemberSlavedUpdate.cpp ///////////////////////////////////////////////////////////////////////////
// Will obey spawner... or die trying
// Author: Mark Lorenzen, August 2002
// Desc:  Slaved unit(s) remain close to their master. Used by angry Mob members (various)
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/InGameUI.h"// selection logic
#include "GameClient/Drawable.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameClient/InGameUI.h"// selection logic
#include "GameClient/Drawable.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Lib/FixBoundary.h"



#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


/* Where a member stands inside the mob.  The slot comes from the member's own object id, which is
	 handed out in creation order and is the same number on every machine, so ten members spawned in
	 a row take ten different slots and keep them for as long as they live.  The slots sit on a
	 sunflower spiral - turn by the golden angle, step out by the square root of the slot number -
	 which spreads any number of them evenly over a disc without any ring bookkeeping.  Before this,
	 every member pathed to the one point the nexus was headed for, arrived on top of its
	 neighbours, and spent the rest of the trip being shoved around by the pathfinder. */
const Int MOB_FORMATION_SLOT_COUNT = 12;
const Fix MOB_FORMATION_GOLDEN_ANGLE = 2.39996_fx;
const Fix MOB_FORMATION_SPACING_IN_RADII = 2.2_fx;	// just over two bodies apart, so neighbours do not shove

/* The disc is squashed along the nexus' own facing and stretched across it, because depth costs
	 the mob its fight.  A round formation thirty units deep puts its near slots in weapon range two
	 seconds before its far ones, and measured against ten Rangers that vanguard died alone: the
	 round version killed 8 of 50 Rangers over five seeds where the same build with no formation at
	 all killed 17.  Width across the line of march costs nothing, since every slot on it is the
	 same distance from what the mob is walking at. */
const Fix MOB_FORMATION_DEPTH_SCALE = 0.35_fx;
const Fix MOB_FORMATION_WIDTH_SCALE = 1.25_fx;

// Close enough to my slot to stop steering towards it and start looking for someone to hit.
const Fix MOB_FORMATION_ARRIVED_DISTANCE = 12_fx;	// 1.2 pathfind cells

// How far my slot has to have moved before it is worth spending another path on it.
const Real MOB_FORMATION_REPATH_DISTANCE = PATHFIND_CELL_SIZE_F * 5.0f;

// How far ahead of the nexus I may get before I ease off and let it catch up.
const Real MOB_FORMATION_LEAD_ALLOWANCE = 25.0f;

// Beyond this multiple of the catch-up radius I have lost the mob and stop being subtle about it.
const Fix MOB_CATCH_UP_CRISIS_MULTIPLIER = Fix( 3 );

// The fraction of CatchUpCrisisBailTime after which I give up on my slot and walk at the nexus.
const UnsignedInt MOB_CATCH_UP_CRISIS_DIVISOR = 3;

/* How often a mob member reconsiders where it is standing.  EA already ran the expensive half of
	 update() once every sixteen frames; what it did not do was sleep, so the module was still
	 dispatched on the other fifteen - a heap pop and a re-sift each, for every member of every mob
	 alive.  An Angry Mob is a nexus and ten members, so a match with several of them per player is
	 hundreds of objects paying that for nothing.  Sleeping is what the `@todo srj use SLEEPY_UPDATE
	 here` at the top of update() has been asking for. */
const Int MOB_MEMBER_UPDATE_RATE = 16;

/* m_framesToWait was the counter that got ticked to sixteen.  It now carries the one-time stagger
	 the constructor drew, and this once it has been spent. */
const Int MOB_MEMBER_STAGGER_SPENT = -1;

//-------------------------------------------------------------------------------------------------
MobMemberSlavedUpdate::MobMemberSlavedUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	m_slaver = INVALID_ID;
	m_framesToWait = GameLogicRandomValue(0,20);

	// MDC: moving to GameLogicRandomValue.  This does not need to be synced, but having it so makes searches *so* much nicer.
	m_personalColor.red = GameLogicRandomValueReal( 0.2f, 0.4f );
	m_personalColor.green = GameLogicRandomValueReal( 0.2f, 0.4f );
	m_personalColor.blue = GameLogicRandomValueReal( 0.2f, 0.4f );
	
//	Drawable *myDraw = getObject()->getDrawable();
//	if ( myDraw )
//		myDraw->colorTint( &m_personalColor );

	m_mobState = MOB_STATE_NONE;
	m_primaryVictimID = INVALID_ID;
	m_squirrellinessRatio = 0;
	m_isSelfTasking = FALSE;
	m_catchUpCrisisTimer = 0;

	// MDC: moving to GameLogicRandomValue.  This does not need to be synced, but having it so makes searches *so* much nicer.
	//getObject()->getDrawable()->setInstanceScale(GameLogicRandomValueReal( 5.0f, 1.5f ));

} 

//-------------------------------------------------------------------------------------------------
MobMemberSlavedUpdate::~MobMemberSlavedUpdate( void )
{
} 

//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::onObjectCreated()
{

	const MobMemberSlavedUpdateModuleData* data = getMobMemberSlavedUpdateModuleData();
	m_squirrellinessRatio = MIN(MAX_SQUIRRELLINESS, MAX(0, data->m_squirrellinessRatio));

}

//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::onEnslave( const Object *slaver )
{
	startSlavedEffects( slaver );
}

//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::onSlaverDie( const DamageInfo *info )
{
	stopSlavedEffects();
}

//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::onSlaverDamage( const DamageInfo *info )
{
	// Only slaves with a ProneUpdate will even care.
	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
	if( ai )
		ai->aiGoProne( info, CMD_FROM_AI );
}
 

//-------------------------------------------------------------------------------------------------
UpdateSleepTime MobMemberSlavedUpdate::update( void )
{
	/* The constructor drew a number so that the ten members of one mob would not all do their
		 expensive frame together.  Keep that, but spend it as a first sleep instead of as a counter
		 that has to be ticked every frame in order to be read. */
	if( m_framesToWait >= 0 )
	{
		const Int stagger = 1 + (m_framesToWait % MOB_MEMBER_UPDATE_RATE);
		m_framesToWait = MOB_MEMBER_STAGGER_SPENT;
		return UPDATE_SLEEP( stagger );
	}

	const MobMemberSlavedUpdateModuleData* data = getMobMemberSlavedUpdateModuleData();
	Object *me = getObject();
	if( !me )
	{
		return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
	}

	Object *master = TheGameLogic->findObjectByID( m_slaver );
	if( master == NULL )
	{
		stopSlavedEffects();

		//TheGameLogic->destroyObject( me );
		me->kill();
		// EA's note here read "you cannot return SLEEP_FOREVER unless you make yourself sleepy..."
		// - so now that we are sleepy, a member whose nexus is gone stops being dispatched at all
		return UPDATE_SLEEP_FOREVER;
	}

	AIUpdateInterface *myAI = me->getAIUpdateInterface();
	AIUpdateInterface *masterAI = master->getAIUpdateInterface();
	if( ! myAI || ! masterAI)
	{
		return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
	}

	Drawable *myDraw = me->getDrawable();
	Drawable *masterDraw = master->getDrawable();
	if ( ! myDraw || ! masterDraw)
	{
		return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
	}

//	myDraw->colorTint( &m_personalColor );


	const ModelConditionFlags flags = myDraw->getModelConditionFlags();
	if (flags.anyIntersectionWith(MAKE_MODELCONDITION_MASK(MODELCONDITION_WEAPONSET_PLAYER_UPGRADE)))
	{
		ModelConditionFlags clearFlags;
		clearFlags.clear();
		clearFlags.set( MODELCONDITION_RELOADING_A );
		clearFlags.set( MODELCONDITION_BETWEEN_FIRING_SHOTS_A );
		clearFlags.set( MODELCONDITION_PREATTACK_A );
		clearFlags.set( MODELCONDITION_FIRING_A );
		clearFlags.set( MODELCONDITION_USING_WEAPON_A );
		myDraw->clearModelConditionFlags( clearFlags );
	}



	Locomotor *locomotor = myAI->getCurLocomotor();
	if( !locomotor )
	{
		return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
	}

	Object *victim = getObject()->getAIUpdateInterface()->getCurrentVictim();
	Object *masterVictim = master->getAIUpdateInterface()->getCurrentVictim();

	if (masterVictim)
	{
		m_primaryVictimID = masterVictim->getID();
	}

	Object *primaryVictim = TheGameLogic->findObjectByID(m_primaryVictimID);


	//now, we don't know if master is standing still or going somewhere, so
	Real masterPathDistToGoal = masterAI->getLocomotorDistanceToGoal();
	Real myPathDistToGoal = myAI->getLocomotorDistanceToGoal();


	Coord3D slotPosition;
	computeSlotPosition( master, &slotPosition );

	FCoord3D slotDelta;
	slotDelta.set( fixFromReal( slotPosition.x ), fixFromReal( slotPosition.y ), fixFromReal( slotPosition.z ) );	// P5
	slotDelta.sub( *me->getPositionFix() );
	const Fix distanceToSlot = slotDelta.length();

	const Fix distanceToMaster = fixSqrt( ThePartitionManager->getDistanceSquaredFix( me, master, FROM_CENTER_3D ) );
	const Fix mustCatchUpRadius = fixFromReal( data->m_mustCatchUpRadius );	// P3
	const Bool lostTheMob = distanceToMaster > mustCatchUpRadius;

	/* One body, one speed.  A member further from the nexus than it is allowed to be runs; a member
		 that is closer to the shared destination than the nexus is eases off rather than arriving
		 alone and standing there.  EA rolled a die here instead, which is why a third of the mob was
		 always ambling while the rest of it ran. */
	if ( lostTheMob )
		myAI->chooseLocomotorSet( LOCOMOTORSET_PANIC );
	else if ( masterAI->isMoving() && myPathDistToGoal + MOB_FORMATION_LEAD_ALLOWANCE < masterPathDistToGoal )
		myAI->chooseLocomotorSet( LOCOMOTORSET_WANDER );
	else
		myAI->chooseLocomotorSet( LOCOMOTORSET_NORMAL );

	/* The formation is for the road and nothing else.  A nexus that has stopped is a nexus in a
		 fight, and a member that walks to a spot on the ground during one is a member not throwing
		 anything: against ten Rangers, holding the slot through the fight as well cost the mob more
		 than half its kills over five seeds, 8 of 50 against the 17 the same build managed with no
		 formation at all.  So the slot is abandoned the moment the mob stops or the member picks a
		 target, and the only thing that overrides that is having lost the mob altogether. */
	const Bool holdFormation = ( masterAI->isMoving() && ! myAI->isAttacking() ) || lostTheMob;

	if ( holdFormation && distanceToSlot > MOB_FORMATION_ARRIVED_DISTANCE )
	{
		Coord3D goalDelta = *myAI->getGoalPosition();
		goalDelta.sub( &slotPosition );

		if ( goalDelta.length() > MOB_FORMATION_REPATH_DISTANCE )// only if I am not headed there already
		{
			myAI->aiMoveToPosition( &slotPosition, CMD_FROM_AI );
		}
	}

	if ( distanceToMaster > mustCatchUpRadius * MOB_CATCH_UP_CRISIS_MULTIPLIER )// critically far, now!
	{
		++ m_catchUpCrisisTimer; // I'm way too far from the nexus this frame

		/* EA killed the member on this timer.  That is the straggler dying for the crime of having
			 had a building in the way, and it is why a mob that walked past a wall arrived four men
			 short.  It gives up on its slot and walks straight at the nexus instead, which is what
			 CatchUpCrisisBailTime says it does in the first place. */
		if ( m_catchUpCrisisTimer > data->m_catchUpCrisisBailTime / MOB_CATCH_UP_CRISIS_DIVISOR )
		{
			const Coord3D masterPos = master->getPositionFix()->toCoord3D();	// P4
			myAI->aiMoveToPosition( &masterPos, CMD_FROM_AI );
		}
	}
	else
	{
		m_catchUpCrisisTimer = 0; // I'm not too far from the nexus this frame
	}

	if ( ! myAI->isMoving() ) // give me something to do while I'm standing here...
	{
		SpawnBehaviorInterface *spawnerBehavior = master->getSpawnBehaviorInterface();

		if ( spawnerBehavior ) // if I have a mommy
		{

			if ( masterAI->isIdle() ) // if controlling player has pressed stop, we stop! That's it!
			{
				myAI->aiIdle(CMD_FROM_AI);
				primaryVictim = NULL;
				m_primaryVictimID = INVALID_ID;
				return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
			}

			if ( spawnerBehavior->maySpawnSelfTaskAI( m_squirrellinessRatio ) ) // if mommy says it is okay
			{
				if ( myAI->getLastCommandSource() != CMD_FROM_AI ) // I may have been told to attack directly more recently
				{
					Object *newTarget = myAI->getNextMoodTarget( FALSE, FALSE );
					if ( newTarget && ( newTarget != victim) ) // if there is someone else around to attack
					{
						victim = newTarget;
						myAI->aiAttackObject( newTarget, 999, CMD_FROM_AI ); // go ahead and do it
						m_isSelfTasking = TRUE;
					}
				}

			}

			if ( ! victim ) // If I still don't have anyone to shoot at
			{
				if ( primaryVictim ) // I remember the last target
				{
					myAI->aiAttackObject( primaryVictim, 999, CMD_FROM_AI );
				}
				else if( ! masterAI->isAttacking())// there Is no previous target and master isn't attacking
				{
			///		myAI->aiIdle( CMD_FROM_AI );// auto acquire mode
				}
				m_isSelfTasking = FALSE;
			}
		}
		else
		{
			DEBUG_ASSERTCRASH(( spawnerBehavior != NULL ),("Hey!, why for this mob member got no spawner? MLorenzen"));
		}
	}

	return UPDATE_SLEEP( MOB_MEMBER_UPDATE_RATE );
}


//-------------------------------------------------------------------------------------------------
// Where I stand in the mob: my own place around wherever the mob as a whole is going.
//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::computeSlotPosition( Object *master, Coord3D *position )
{
	Object *me = getObject();
	AIUpdateInterface *myAI = me->getAIUpdateInterface();
	AIUpdateInterface *masterAI = master->getAIUpdateInterface();

	FCoord3D pos = *master->getPositionFix();
	if ( masterAI->isMoving() )
	{
		const Coord3D *goal = masterAI->getGoalPosition();	// P4
		pos.set( fixFromReal( goal->x ), fixFromReal( goal->y ), fixFromReal( goal->z ) );
		if ( pos.length() < Fix( 1 ) ) // a nasty error has sent the nexus to map origin
			pos = *master->getPositionFix();
	}

	const Int slot = ((Int)me->getID()) % MOB_FORMATION_SLOT_COUNT;
	const Fix spacing = me->getGeometryInfo().getBoundingCircleRadiusFix() * MOB_FORMATION_SPACING_IN_RADII;
	const Fix angle = Fix( slot ) * MOB_FORMATION_GOLDEN_ANGLE;
	const Fix radius = spacing * fixSqrt( Fix( slot ) + 0.5_fx );

	const Fix alongTravel = radius * fixCos( angle ) * MOB_FORMATION_DEPTH_SCALE;
	const Fix acrossTravel = radius * fixSin( angle ) * MOB_FORMATION_WIDTH_SCALE;

	const Fix facing = master->getOrientationFix();
	const Fix forwardX = fixCos( facing );
	const Fix forwardY = fixSin( facing );

	pos.x += alongTravel * forwardX - acrossTravel * forwardY;
	pos.y += alongTravel * forwardY + acrossTravel * forwardX;
	pos.z = TheTerrainLogic->getGroundHeightFix( pos.x, pos.y );
	*position = pos.toCoord3D();	// P5: the pathfinder adjusts it in float

	// A slot that lands in a cliff or a building is a member walking into a wall until the mob dies.
	TheAI->pathfinder()->adjustToPossibleDestination( me, myAI->getLocomotorSet(), position );
}




//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::startSlavedEffects( const Object *slaver )
{
	if( slaver == NULL )
		return;

	m_slaver = slaver->getID();
	
	// mark selves as not selectable
	//getObject()->setStatus( OBJECT_STATUS_UNSELECTABLE );

}

//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::stopSlavedEffects()
{
	m_slaver = INVALID_ID;

	/// @todo Just a thought.  Our Status bits on objects really need to be reference counts so you don't clear someone else's flag
	getObject()->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
	getObject()->clearDisabled( DISABLED_HELD );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// slaves
	xfer->xferObjectID( &m_slaver );

	// frames to wait
	xfer->xferInt( &m_framesToWait );

	// mob state
	xfer->xferUser( &m_mobState, sizeof( MobStates ) );

	// personal color
	xfer->xferRGBColor( &m_personalColor );

	// primary victim
	xfer->xferObjectID( &m_primaryVictimID );

	// squirrelliness ration
	xfer->xferReal( &m_squirrellinessRatio );

	// is self tasking
	xfer->xferBool( &m_isSelfTasking );
	
	// catch up crisis timer
  xfer->xferUnsignedInt( &m_catchUpCrisisTimer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
