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
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/MobMemberSlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameClient/InGameUI.h"// selection logic
#include "GameClient/Drawable.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h" 



#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


/* Where a member stands inside the mob.  The slot is the member's place in its nexus' list, so no
	 two members of a mob ever share one, whatever the mob's size or whatever else was built between
	 them.  The slots sit on a sunflower spiral - turn by the golden angle, step out by the square
	 root of the slot number - which spreads any number of them evenly over a disc without any ring
	 bookkeeping.  Before this, every member pathed to the one point the nexus was headed for, arrived
	 on top of its neighbours, and spent the rest of the trip being shoved around by the pathfinder. */
const Real MOB_FORMATION_GOLDEN_ANGLE = 2.39996f;

/* The disc is squashed along the nexus' own facing and stretched across it, because depth costs
	 the mob its fight.  A round formation thirty units deep puts its near slots in weapon range two
	 seconds before its far ones, and measured against ten Rangers that vanguard died alone: the
	 round version killed 8 of 50 Rangers over five seeds where the same build with no formation at
	 all killed 17.  Width across the line of march costs nothing, since every slot on it is the
	 same distance from what the mob is walking at. */
const Real MOB_FORMATION_DEPTH_SCALE = 0.35f;
const Real MOB_FORMATION_WIDTH_SCALE = 1.25f;

// Close enough to my slot to stop steering towards it and start looking for someone to hit.
const Real MOB_FORMATION_ARRIVED_DISTANCE = PATHFIND_CELL_SIZE_F * 1.2f;

// How far my slot has to have moved before it is worth spending another path on it.
const Real MOB_FORMATION_REPATH_DISTANCE = PATHFIND_CELL_SIZE_F * 5.0f;

// How far ahead of the nexus the formation rides on the march: twice the repath distance, so a
// member always has between one and two of them left to walk and never stops to wait for its slot.
const Real MOB_FORMATION_LOOKAHEAD = MOB_FORMATION_REPATH_DISTANCE * 2.0f;

// Speed added for each unit I am behind the nexus on the march, taken away for each unit ahead of it.
const Real MOB_PACE_GAIN = 0.2f;

// The slowest I walk to let the nexus catch up, as a fraction of its speed: 0.7 had a man ahead of his
// place dawdling at 12.6 beside his neighbours' 20, a crowd at three paces
const Real MOB_PACE_SLOWEST = 0.95f;

// Beyond this multiple of the catch-up radius I have lost the mob and stop being subtle about it.
const Real MOB_CATCH_UP_CRISIS_MULTIPLIER = 3.0f;

// The fraction of CatchUpCrisisBailTime after which I give up on my slot and walk at the nexus.
const UnsignedInt MOB_CATCH_UP_CRISIS_DIVISOR = 3;

/* How often a mob member reconsiders where it is standing.  EA already ran the expensive half of
	 update() once every sixteen frames; what it did not do was sleep, so the module was still
	 dispatched on the other fifteen - a heap pop and a re-sift each, for every member of every mob
	 alive.  An Angry Mob is a nexus and ten members, so a match with several of them per player is
	 hundreds of objects paying that for nothing.  Sleeping is what the `@todo srj use SLEEPY_UPDATE
	 here` at the top of update() has been asking for. */
const Int MOB_MEMBER_UPDATE_RATE = ConvertRetailFrames( 16 );	// also the tick of CatchUpCrisisBailTime, a count of these updates

/* m_framesToWait was the counter that got ticked to sixteen.  It now carries the one-time stagger
	 the constructor drew, and this once it has been spent. */
const Int MOB_MEMBER_STAGGER_SPENT = -1;

// How often a member told to follow a new order looks again for its nexus to start walking: the
// nexus' path search can take a few frames, and a member waiting the whole update rate for it
// starts half a second behind the mob.
const Int MOB_FOLLOW_ORDER_POLL = ConvertRetailFrames( 2 );

/* The point 'ahead' further along the path than the point on it nearest to 'from', and which way the
	 path runs at each of the two.  FALSE when there is no leg to walk.  This is what the formation
	 rides on: a slot out on the straight line to the goal is a slot on the far side of whatever the
	 path bends round, and every member then found its own way round, some by the other end. */
static Bool walkAheadOnPath( Path *path, const Coord3D *from, Real ahead, Coord3D *out, Coord2D *dirHere, Coord2D *dirThere )
{
	PathNode *nearest = NULL;
	Real nearestT = 0.0f;
	Real nearestSqr = 1.0e30f;
	for( PathNode *node = path ? path->getFirstNode() : NULL; node && node->getNextOptimized(); node = node->getNextOptimized() )
	{
		const Coord3D *a = node->getPosition();
		const Coord3D *b = node->getNextOptimized()->getPosition();
		const Real dx = b->x - a->x;
		const Real dy = b->y - a->y;
		const Real lenSqr = dx * dx + dy * dy;
		Real t = lenSqr > 0.0f ? ( ( from->x - a->x ) * dx + ( from->y - a->y ) * dy ) / lenSqr : 0.0f;
		t = MAX( 0.0f, MIN( 1.0f, t ) );
		const Real ex = a->x + t * dx - from->x;
		const Real ey = a->y + t * dy - from->y;
		if ( ex * ex + ey * ey < nearestSqr )
		{
			nearestSqr = ex * ex + ey * ey;
			nearest = node;
			nearestT = t;
		}
	}
	if ( nearest == NULL )
		return FALSE;

	dirHere->x = dirHere->y = 0.0f;
	dirThere->x = dirThere->y = 0.0f;
	Real left = ahead;
	Real t = nearestT;
	for( PathNode *node = nearest; ; node = node->getNextOptimized(), t = 0.0f )
	{
		PathNode *next = node->getNextOptimized();
		const Coord3D *a = node->getPosition();
		const Coord3D *b = next->getPosition();
		const Real dx = b->x - a->x;
		const Real dy = b->y - a->y;
		const Real len = sqrtf( dx * dx + dy * dy );
		if ( len > 0.001f )
		{
			dirThere->x = dx / len;
			dirThere->y = dy / len;
			if ( dirHere->x == 0.0f && dirHere->y == 0.0f )
				*dirHere = *dirThere;
		}
		const Real remaining = len * ( 1.0f - t );
		if ( remaining >= left || next->getNextOptimized() == NULL )
		{
			const Real s = len > 0.001f ? MIN( 1.0f, t + left / len ) : 1.0f;
			out->x = a->x + s * dx;
			out->y = a->y + s * dy;
			out->z = a->z + s * ( b->z - a->z );
			return dirThere->x != 0.0f || dirThere->y != 0.0f;
		}
		left -= remaining;
	}
}

//-------------------------------------------------------------------------------------------------
MobMemberSlavedUpdate::MobMemberSlavedUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	m_slaver = INVALID_ID;
	m_framesToWait = GameLogicRandomValue(0,ConvertRetailFrames(20));

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
	m_followOrder = FALSE;

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
void MobMemberSlavedUpdate::onSlaverOrdered()
{
	m_followOrder = TRUE;
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
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


	/* EA wiped the primary's firing states off every upgraded mob member on every look, firing or
		 not, which cut a member's firing animation off every half second in the middle of a fight.
		 Only a member that is not attacking can be holding a stale one. */
	const ModelConditionFlags flags = myDraw->getModelConditionFlags();
	if (flags.anyIntersectionWith(MAKE_MODELCONDITION_MASK(MODELCONDITION_WEAPONSET_PLAYER_UPGRADE))
		&& !me->testStatus( OBJECT_STATUS_IS_ATTACKING ))
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


	Coord3D slotPosition;
	Real slotAhead = 0.0f;
	Coord2D marchDir;
	computeSlotPosition( master, &slotPosition, &slotAhead, &marchDir );

	/* A group order moved the nexus and left me out of it (AIGroup's followsNexusInGroup).  Once the
		 nexus is walking I take my place at once, whatever I was doing: it is the player's or the
		 computer's new order, and an old target or a goal still within the repath distance must not
		 hold me back.  A nexus that went idle instead found no way there, and the order is over. */
	const Bool followOrderNow = m_followOrder && masterAI->isMoving();
	if ( followOrderNow || ( m_followOrder && masterAI->isIdle() ) )
		m_followOrder = FALSE;
	if ( followOrderNow )
	{
		m_primaryVictimID = INVALID_ID;
		primaryVictim = NULL;
	}

	Coord3D slotDelta = slotPosition;
	slotDelta.sub( me->getPosition() );
	const Real distanceToSlot = slotDelta.length();

	const Real distanceToMaster = sqrtf( ThePartitionManager->getDistanceSquared( me, master, FROM_CENTER_3D ) );

	/* On the march, how far I am behind the nexus along the way it is walking.  Plain distance from
		 the nexus cannot say that: a member whose slot sits out on the wing is far away without being
		 late, and one that has got ahead reads as lost and sprints further ahead.  Nor can the two
		 distances left along the paths, which come from different searches and jump by a hundred units
		 from one look to the next.  The way it is walking is its path where it stands, not the line to
		 its goal, which round a corner points through the wall. */
	const Bool marching = masterAI->isMoving() && myAI->isMoving() && ! myAI->isAttacking() && ( marchDir.x != 0.0f || marchDir.y != 0.0f );
	Real lag = 0.0f;
	if ( marching )
	{
		const Coord3D *myPos = me->getPosition();
		const Coord3D *masterPos = master->getPosition();
		lag = ( masterPos->x - myPos->x ) * marchDir.x + ( masterPos->y - myPos->y ) * marchDir.y;
	}
	const Bool lostTheMob = marching ? lag > data->m_mustCatchUpRadius : distanceToMaster > data->m_mustCatchUpRadius;

	/* One body, one speed.  EA rolled a die here, and switching between the walk and the run on a
		 threshold was no better: the run outpaces the nexus, the walk loses to it, so the mob surged
		 back and forth around the threshold every couple of seconds.  A member holds the nexus' own
		 speed and closes any gap gradually, never on the run: the run (AngryMobPanicLocomotor, 32
		 against the nexus' 18) sent one man sprinting past the crowd every time he fell a cell behind.
		 The walk is a ninth faster than the nexus, plain or with the GLA rank 6 talent, which is room
		 enough to catch up. */
	if ( lostTheMob )
	{
		myAI->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		myAI->setDesiredSpeed( FAST_AS_POSSIBLE );
	}
	else
	{
		if ( marching )
		{
			// level with my own place in the formation, not with the nexus, or the depth flattens out
			const Real masterSpeed = masterAI->getCurLocomotor()->getMaxSpeedForCondition( master->getBodyModule()->getDamageState() );
			const Real pace = MAX( masterSpeed + ( lag + slotAhead ) * MOB_PACE_GAIN, masterSpeed * MOB_PACE_SLOWEST );
			myAI->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
			myAI->setDesiredSpeed( pace );
		}
		else
		{
			myAI->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
			myAI->setDesiredSpeed( FAST_AS_POSSIBLE );
		}
	}

	/* The formation is for the road and nothing else.  A nexus that has stopped is a nexus in a
		 fight, and a member that walks to a spot on the ground during one is a member not throwing
		 anything: against ten Rangers, holding the slot through the fight as well cost the mob more
		 than half its kills over five seeds, 8 of 50 against the 17 the same build managed with no
		 formation at all.  So the slot is abandoned the moment the mob stops or the member picks a
		 target, and the only thing that overrides that is having lost the mob altogether. */
	const Bool holdFormation = ( masterAI->isMoving() && ! myAI->isAttacking() ) || lostTheMob || followOrderNow;

	Bool steered = FALSE;
	if ( holdFormation && ( distanceToSlot > MOB_FORMATION_ARRIVED_DISTANCE || followOrderNow ) )
	{
		Coord3D goalDelta = *myAI->getGoalPosition();
		goalDelta.sub( &slotPosition );

		if ( followOrderNow || goalDelta.length() > MOB_FORMATION_REPATH_DISTANCE )// only if I am not headed there already
		{
			/* An attack move stays an attack move.  A plain move here took the attack out of a
				 mob ordered on its nexus alone: the members walked to their slots past the enemy
				 without a shot. */
			if ( myAI->getCurrentStateID() == AI_ATTACK_MOVE_TO || masterAI->getCurrentStateID() == AI_ATTACK_MOVE_TO )
				myAI->aiAttackMoveToPosition( &slotPosition, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			else
				myAI->aiMoveToPosition( &slotPosition, CMD_FROM_AI );
			steered = TRUE;
		}
	}

	if ( distanceToMaster > data->m_mustCatchUpRadius * MOB_CATCH_UP_CRISIS_MULTIPLIER )// critically far, now!
	{
		++ m_catchUpCrisisTimer; // I'm way too far from the nexus this frame

		/* EA killed the member on this timer.  That is the straggler dying for the crime of having
			 had a building in the way, and it is why a mob that walked past a wall arrived four men
			 short.  It gives up on its slot and walks straight at the nexus instead, which is what
			 CatchUpCrisisBailTime says it does in the first place. */
		if ( m_catchUpCrisisTimer > data->m_catchUpCrisisBailTime / MOB_CATCH_UP_CRISIS_DIVISOR )
		{
			myAI->aiMoveToPosition( master->getPosition(), CMD_FROM_AI );
		}
	}
	else
	{
		m_catchUpCrisisTimer = 0; // I'm not too far from the nexus this frame
	}

	// a member just sent to its place has no path yet and so is not moving; it is not idle either
	if ( ! myAI->isMoving() && ! steered ) // give me something to do while I'm standing here...
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

	return UPDATE_SLEEP( m_followOrder ? MOB_FOLLOW_ORDER_POLL : MOB_MEMBER_UPDATE_RATE );
}


//-------------------------------------------------------------------------------------------------
// Where I stand in the mob: my own place around wherever the mob as a whole is going.
//-------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::computeSlotPosition( Object *master, Coord3D *position, Real *alongTravel, Coord2D *marchDir )
{
	marchDir->x = marchDir->y = 0.0f;
	Object *me = getObject();
	AIUpdateInterface *myAI = me->getAIUpdateInterface();
	AIUpdateInterface *masterAI = master->getAIUpdateInterface();

	Real forwardX = Cos( master->getOrientation() );
	Real forwardY = Sin( master->getOrientation() );
	*position = *master->getPosition();

	/* On the march the slot rides a little ahead of the nexus, on its line to the goal, and the
		 formation faces that line.  With every slot out at the goal instead, five members pathed from
		 one spot to five spots a few metres apart, the paths shared their cells, and the pace control
		 pulled everyone level with the nexus: the mob walked as one overlapping knot (closest pair
		 under one metre) and spread out only on arrival. */
	if ( masterAI->isMoving() )
	{
		Coord3D toGoal = *masterAI->getGoalPosition();
		toGoal.sub( master->getPosition() );
		toGoal.z = 0.0f;
		const Real distToGoal = toGoal.length();
		const Coord3D *goal = masterAI->getGoalPosition();
		Coord2D dirThere;
		if ( goal->x * goal->x + goal->y * goal->y < 1.0f ) // a nasty error has sent the nexus' goal to map origin
		{
			// keep the nexus' own spot and facing
		}
		else if ( walkAheadOnPath( masterAI->getPath(), master->getPosition(), MOB_FORMATION_LOOKAHEAD, position, marchDir, &dirThere ) )
		{
			// down the nexus' own path, facing the way it runs there
			forwardX = dirThere.x;
			forwardY = dirThere.y;
		}
		else if ( distToGoal > MOB_FORMATION_LOOKAHEAD )
		{
			forwardX = toGoal.x / distToGoal;
			forwardY = toGoal.y / distToGoal;
			position->x += forwardX * MOB_FORMATION_LOOKAHEAD;
			position->y += forwardY * MOB_FORMATION_LOOKAHEAD;
			marchDir->x = forwardX;
			marchDir->y = forwardY;
		}
		else
		{
			*position = *masterAI->getGoalPosition();
			if ( distToGoal > 1.0f )
			{
				forwardX = toGoal.x / distToGoal;
				forwardY = toGoal.y / distToGoal;
				marchDir->x = forwardX;
				marchDir->y = forwardY;
			}
		}
	}

	const Int slot = master->getSpawnBehaviorInterface()->getSlaveIndex( me->getID() );
	const Real spacing = me->getGeometryInfo().getBoundingCircleRadius() * getMobMemberSlavedUpdateModuleData()->m_formationSpacing;
	const Real angle = slot * MOB_FORMATION_GOLDEN_ANGLE;
	const Real radius = spacing * sqrtf( slot + 0.5f );

	*alongTravel = radius * Cos( angle ) * MOB_FORMATION_DEPTH_SCALE;
	const Real acrossTravel = radius * Sin( angle ) * MOB_FORMATION_WIDTH_SCALE;

	position->x += *alongTravel * forwardX - acrossTravel * forwardY;
	position->y += *alongTravel * forwardY + acrossTravel * forwardX;
	position->z = TheTerrainLogic->getGroundHeight( position->x, position->y );

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
	* 1: Initial version
	* 2: m_followOrder */
// ------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
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

	// a group order still waiting for the nexus to walk
	if( version >= 2 )
		xfer->xferBool( &m_followOrder );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MobMemberSlavedUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
