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

// JetAIUpdate.cpp //////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_LOCOMOTORSET_NAMES

#include "Common/ActionManager.h"
#include "Common/GlobalData.h"
#include "Common/MiscAudio.h"
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CountermeasuresBehavior.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/ParkingPlaceBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

const Real BIGNUM = 99999.0f;

/*
	A runway is flown, not followed point to point. Taking off, the jet rolls down the centre line at
	full throttle, the wheels leave at JET_LIFTOFF_SHARE of the runway and the climb builds up over
	JET_ROTATE_SHARE more of it to a steady JET_CLIMB_SLOPE; the takeoff is done at the airfield's
	approach height. Landing, it steers for a point on the extended centre line ahead of it, down a
	JET_GLIDE_SLOPE glide that rounds out over JET_FLARE_DIST to a level touchdown on the runway's
	threshold, and rolls out slowing to JET_ROLLOUT_END_SHARE of MinSpeed at its far end. A jet that
	cannot line up from where it is (beside the runway, past it, across it, or too high) flies a
	circuit first, at its corner speed: out along the side of the runway two turn radii off the centre
	line, round onto a JET_FINAL_DIST final, and in, slowing to MinSpeed over the threshold. Its
	height follows the same glide measured along the circuit, so it comes down through the turns, and
	a jet above the glide comes down to it no steeper than JET_STEEPEST_DESCENT.
*/
static const Real JET_LIFTOFF_SHARE = 0.55f;
static const Real JET_ROTATE_SHARE = 0.25f;
static const Real JET_CLIMB_SLOPE = 0.21f;		// tan 12 degrees
static const Real JET_GLIDE_SLOPE = 0.176f;		// tan 10 degrees
static const Real JET_FLARE_DIST = 50.0f;
static const Real JET_ROLLOUT_END_SHARE = 0.2f;
static const Real JET_MIN_LOOKAHEAD = 60.0f;		// the steering point runs at least this far ahead on the centre line
static const Real JET_GO_AROUND_HEIGHT = 10.0f;	// past the threshold higher than this, it goes round
static const Real JET_LINE_UP_SLACK = 8.0f;			// this far off the centre line it lands anyway
static const Real JET_LINE_UP_ROOM = 3.0f;			// otherwise it needs this many times its distance off it to line up in
static const Real JET_HIGH_ON_FINAL = 20.0f;		// more than this above the glide, it flies the circuit instead of diving
static const Real JET_STEEPEST_DESCENT = 0.27f;	// tan 15 degrees: a jet above its glide comes down to it no steeper
static const Real JET_FINAL_DIST = 150.0f;			// the straight final after the circuit's turn in
// the jet's height servo settles this many frames of travel short of a height target moving away
// under it, so a target this far ahead holds the jet on a slope (measured: 10 u/s down at 60 u/s)
static const Real JET_HEIGHT_LAG_FRAMES = LOGICFRAMES_PER_GAME_SECOND * 0.1f;

/**
	Height above the runway, dist before the point where it meets it, of a straight slope that rounds
	out over roundDist into the level, capped at maxHeight: the glide down to a touchdown, or read the
	other way, the climb from a liftoff. No Object involved, so test_gameengine can link to it.
*/
Real JetRunway_slopeHeight( Real dist, Real maxHeight, Real slope, Real roundDist )
{
	if (dist <= 0.0f)
		return 0.0f;
	Real height = (dist < roundDist) ? slope * dist * dist / (2.0f * roundDist) : slope * (dist - 0.5f * roundDist);
	return (height < maxHeight) ? height : maxHeight;
}

static const Real JET_TAXI_ARC_STEP = PI / 8.0f;		// a rounded taxi corner gets a route point every this much of its turn
static const Real JET_TAXI_LEG_SHARE = 0.45f;			// and takes no more than this share of either leg beside it
static const Real JET_TAXI_HAIRPIN = 0.01f;				// one plus the cosine of a turn this near to straight back is left a corner

/**
	Round every corner of a taxi route (the first point where the jet stands) into an arc of radius,
	or of as much of one as fits in JET_TAXI_LEG_SHARE of the shorter leg beside it, laid down as
	route points JET_TAXI_ARC_STEP apart round the turn. A jet follows its route point to point, so
	it turns along the arc instead of reaching the corner and swinging round it. The ends stay put,
	and so does a corner that turns straight back. No Object involved, so test_gameengine can link
	to it.
*/
void JetTaxi_roundCorners( std::vector<Coord3D>& route, Real radius )
{
	if (route.size() < 3)
		return;

	std::vector<Coord3D> rounded;
	rounded.push_back(route.front());
	for (size_t i = 1; i + 1 < route.size(); ++i)
	{
		const Coord3D& from = route[i - 1];
		const Coord3D& corner = route[i];
		const Coord3D& to = route[i + 1];
		Real inX = corner.x - from.x;
		Real inY = corner.y - from.y;
		Real outX = to.x - corner.x;
		Real outY = to.y - corner.y;
		Real inLength = sqrt(inX*inX + inY*inY);
		Real outLength = sqrt(outX*outX + outY*outY);
		Real cross = (inX*outY - inY*outX) / (inLength * outLength);	// positive turning left
		Real dot = (inX*outX + inY*outY) / (inLength * outLength);
		Real turn = ATan2(fabs(cross), dot);
		if (turn < 0.5f * JET_TAXI_ARC_STEP || 1.0f + dot < JET_TAXI_HAIRPIN)
		{
			rounded.push_back(corner);
			continue;
		}
		inX /= inLength;
		inY /= inLength;

		Real halfTurnTan = fabs(cross) / (1.0f + dot);
		Real shorterLeg = (inLength < outLength) ? inLength : outLength;
		Real tangent = radius * halfTurnTan;
		if (tangent > JET_TAXI_LEG_SHARE * shorterLeg)
			tangent = JET_TAXI_LEG_SHARE * shorterLeg;
		Real arcRadius = tangent / halfTurnTan;
		Real side = (cross > 0.0f) ? 1.0f : -1.0f;

		// the arc leaves the leg in where it is tangent to it, round a centre a radius to the side
		Real startX = corner.x - inX * tangent;
		Real startY = corner.y - inY * tangent;
		Real centreX = startX - inY * arcRadius * side;
		Real centreY = startY + inX * arcRadius * side;
		Real startAngle = ATan2(startY - centreY, startX - centreX);
		Int steps = (Int)ceil(turn / JET_TAXI_ARC_STEP);
		for (Int step = 0; step <= steps; ++step)
		{
			Real angle = startAngle + side * turn * step / steps;
			Coord3D point;
			point.x = centreX + arcRadius * Cos(angle);
			point.y = centreY + arcRadius * Sin(angle);
			point.z = corner.z;
			rounded.push_back(point);
		}
	}
	rounded.push_back(route.back());
	route.swap(rounded);
}

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-------------------------------------------------------------------------------------------------
enum TaxiType
{
	FROM_HANGAR,
	FROM_PARKING,
	TO_PARKING
};

//-------------------------------------------------------------------------------------------------
enum JetAIStateType
{
	// note that these must be distinct (numerically) from AIStateType. ick.
	JETAISTATETYPE_FIRST = 1000,

	TAXI_FROM_HANGAR,
	TAKING_OFF_AWAIT_CLEARANCE,
	TAXI_TO_TAKEOFF,
	PAUSE_BEFORE_TAKEOFF,
	TAKING_OFF,
	LANDING_AWAIT_CLEARANCE,
	LANDING,
	TAXI_FROM_LANDING,
	ORIENT_FOR_PARKING_PLACE,
	RELOAD_AMMO,
	RETURNING_FOR_LANDING,
	RETURN_TO_DEAD_AIRFIELD,
	CIRCLING_DEAD_AIRFIELD,

	JETAISTATETYPE_LAST
};


//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::getFlag( FlagType f ) const 
{ 
	return (m_flags & (1<<f)) != 0; 
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::setFlag( FlagType f, Bool v) 
{ 
	if (v) 
		m_flags |= (1<<f); 
	else 
		m_flags &= ~(1<<f); 
}


//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isOutOfSpecialReloadAmmo() const
{
	const Object* jet = getObject();
	// if we have at least one special reload weapon,
	// AND all such weapons are out of ammo,
	// return true.
	Int specials = 0;
	Int out = 0;
	for( Int i = 0; i < WEAPONSLOT_COUNT;	i++ )
	{
		const Weapon* weapon = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
		if (weapon == NULL || weapon->getReloadType() != RETURN_TO_BASE_TO_RELOAD)
			continue;
		++specials;
		if (weapon->getStatus() == OUT_OF_AMMO)
			++out;
	}
	return specials > 0 && out == specials;
}

//-------------------------------------------------------------------------------------------------
static ParkingPlaceBehaviorInterface* getPP(ObjectID id, Object** airfieldPP = NULL)
{
	if (airfieldPP)
		*airfieldPP = NULL;

	// the Kargu base's drones roll off a superweapon's runways, which is no airfield to anyone else
	Object* airfield = TheGameLogic->findObjectByID( id );
	if (airfield == NULL || airfield->isEffectivelyDead() || airfield->testStatus(OBJECT_STATUS_SOLD) ||
			!(airfield->isKindOf(KINDOF_FS_AIRFIELD) || airfield->isKindOf(KINDOF_FS_SUPERWEAPON)))
		return NULL;

	if (airfieldPP)
		*airfieldPP = airfield;

	ParkingPlaceBehaviorInterface* pp = NULL;
	for (BehaviorModule** i = airfield->getBehaviorModules(); *i; ++i)
	{
		if ((pp = (*i)->getParkingPlaceBehaviorInterface()) != NULL)
			break;
	}

	return pp;
}

//-------------------------------------------------------------------------------------------------
class PartitionFilterHasParkingPlace : public PartitionFilter
{
private:
	ObjectID m_id;
public:
	PartitionFilterHasParkingPlace(ObjectID id) : m_id(id) { }
protected:
#if defined(_DEBUG) || defined(_INTERNAL)
	virtual const char* debugGetName() { return "PartitionFilterHasParkingPlace"; }
#endif
	virtual Bool allow(Object *objOther)
	{
		// ask, don't reserve: EA reserved here, so every airfield the search tried on its way to the
		// nearest kept a space for this jet, empty on the ground and closed to everyone, until it died
		ParkingPlaceBehaviorInterface* pp = getPP(objOther->getID());
		return pp != NULL && pp->canReserveSpace(m_id);
	}
};

//-------------------------------------------------------------------------------------------------
static Object* findSuitableAirfield(Object* jet)
{
	PartitionFilterAcceptByKindOf					filterKind(MAKE_KINDOF_MASK(KINDOF_FS_AIRFIELD), KINDOFMASK_NONE);
	PartitionFilterRejectByObjectStatus		filterStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNDER_CONSTRUCTION ), OBJECT_STATUS_MASK_NONE );
	PartitionFilterRejectByObjectStatus		filterStatusTwo( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_SOLD ), OBJECT_STATUS_MASK_NONE ); // Independent to make it an OR
	PartitionFilterRelationship						filterTeam(jet, PartitionFilterRelationship::ALLOW_ALLIES);
	PartitionFilterAlive									filterAlive;
	PartitionFilterSameMapStatus					filterMapStatus(jet);
	PartitionFilterHasParkingPlace				filterPP(jet->getID());

	PartitionFilter *filters[16];
	Int numFilters = 0;
	filters[numFilters++] = &filterKind;
	filters[numFilters++] = &filterStatus;
	filters[numFilters++] = &filterStatusTwo;
	filters[numFilters++] = &filterTeam;
	filters[numFilters++] = &filterAlive;
	filters[numFilters++] = &filterPP;
	filters[numFilters++] = &filterMapStatus;
	filters[numFilters] = NULL;

	// the one it picked is reserved, as the filter used to do, so nobody takes the space on the way there
	Object* airfield = ThePartitionManager->getClosestObject( jet, HUGE_DIST, FROM_CENTER_2D, filters );
	ParkingPlaceBehaviorInterface* pp = airfield ? getPP(airfield->getID()) : NULL;
	if (pp != NULL && !pp->reserveSpace(jet->getID(), 0.0f, NULL))
		return NULL;
	return airfield;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/*
	Success: we have runway clearance
	Failure: no runway clearance
*/
class JetAwaitingRunwayState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetAwaitingRunwayState, "JetAwaitingRunwayState")		
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};
private:
	const Bool m_landing;

public:
	JetAwaitingRunwayState( StateMachine *machine, Bool landing ) : m_landing(landing), State( machine, "JetAwaitingRunwayState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!m_landing);
		jetAI->friend_setLandingInProgress(m_landing);
		jetAI->friend_setAllowCircling(true);
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
		{
			// No airfield left to wait for.  Succeeding here says "the runway is yours", and the
			// aircraft then flies the rest of a landing that has nowhere to land, so fail instead
			// and let the machine take the dead-airfield route.
			return STATE_FAILURE;
		}

		// gotta reserve a space in order to reserve a runway
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), NULL))
		{
			DEBUG_ASSERTCRASH(m_landing, ("hmm, this should never happen for taking-off things"));
			return STATE_FAILURE;
		}
		
		if (pp->reserveRunway(jet->getID(), m_landing))
		{
			return STATE_SUCCESS;
		}
		else if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) && !m_landing )
		{
			//If we're trying to take off an aircraft carrier and fail to reserve a 
			//runway, it's because we need to be at the front of the carrier queue.
			//Therefore, we need to move forward whenever possible until we are in
			//the front.
			Coord3D bestPos;
			if( pp->calcBestParkingAssignment( jet->getID(), &bestPos ) )
			{
				jetAI->friend_setTaxiInProgress(true);
				jetAI->friend_setAllowAirLoco(false);
				jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);
				
				jetAI->destroyPath();
				Path *movePath;
				movePath = newInstance(Path);
				Coord3D pos = *jet->getPosition();
				movePath->prependNode( &pos, LAYER_GROUND );
				movePath->markOptimized();
				movePath->appendNode( &bestPos, LAYER_GROUND );

				TheAI->pathfinder()->setDebugPath(movePath);

				jetAI->friend_setPath( movePath );
				DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));
				jetAI->getCurLocomotor()->setUsePreciseZPos(true);
				jetAI->getCurLocomotor()->setUltraAccurate(true);
				jetAI->getCurLocomotor()->setAllowInvalidPosition(true);
				jetAI->ignoreObstacleID(jet->getProducerID());
			}
		}

		// can't get a runway? gotta wait.
		jetAI->setLocomotorGoalNone();
		return STATE_CONTINUE;
	}

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jetAI)
		{
			jetAI->friend_setTakeoffInProgress(false);
			jetAI->friend_setLandingInProgress(false);
			jetAI->friend_setAllowCircling(false);
		}
	}

};
EMPTY_DTOR(JetAwaitingRunwayState)

//-------------------------------------------------------------------------------------------------
/*
	Success: a new suitable airfield has appeared
	Failure: shouldn't normally happen
*/
class JetOrHeliCirclingDeadAirfieldState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliCirclingDeadAirfieldState, "JetOrHeliCirclingDeadAirfieldState")		
protected:
	// snapshot interface	 STUBBED.
	// The state will check immediately after a load game, but I think that's ok.  jba.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

private:
	Int m_checkAirfield;

	enum
	{
		// only recheck for new airfields every second or so
		HOW_OFTEN_TO_CHECK = LOGICFRAMES_PER_GAME_SECOND
	};

public:
	JetOrHeliCirclingDeadAirfieldState( StateMachine *machine ) : 
		State( machine, "JetOrHeliCirclingDeadAirfieldState"),
		m_checkAirfield(0) { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}
		
		// obscure case: if the jet wasn't spawned, but just placed directly on the map,
		// it might not have an owning airfield, and it might be trying to return
		// simply due to being idle, not out of ammo. so check and don't die in that
		// case, but just punt back out to idle.
		// Only a jet that never had an airfield, though. Returning for landing clears the producer of
		// one whose airfield is gone, and that jet, still carrying a bomb, used to be punted out here
		// too: idle, back ten seconds later, punted again, never burning and never landing, a damaged
		// Aurora orbiting a full base for the rest of the match. It circles the ruin with the rest now.
		// One with no fuel to burn (a Comanche, a Chinook) would hang over the ruin for good, so that
		// one still goes back to idle as EA had it.
		if (!jetAI->isOutOfSpecialReloadAmmo() && jet->getProducerID() == INVALID_ID &&
				(!jetAI->friend_hadAirfield() || jetAI->friend_getOutOfAmmoDamagePerSecond() <= 0.0f))
		{
			return STATE_FAILURE;
		}

		// just stay where we are.
		jetAI->setLocomotorGoalNone();

		m_checkAirfield = HOW_OFTEN_TO_CHECK;

		//Play the "low fuel" voice whenever the craft is circling above the airfield.
		AudioEventRTS soundToPlay = *jet->getTemplate()->getPerUnitSound( "VoiceLowFuel" );
		soundToPlay.setObjectID( jet->getID() );
		TheAudio->addAudioEvent( &soundToPlay );

		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		// just stay where we are.
		jetAI->setLocomotorGoalNone();
		
		Real damageRate = jetAI->friend_getOutOfAmmoDamagePerSecond();
		if (damageRate > 0)
		{
			// convert to damage/sec to damage/frame
			damageRate *= GAME_SECONDS_PER_LOGICFRAME_REAL;
			// since it's a percentage, multiply times the max health
			damageRate *= jet->getBodyModule()->getMaxHealth();

			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
			damageInfo.in.m_deathType = DEATH_NORMAL;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = damageRate;
			jet->attemptDamage( &damageInfo );
		}

		if (--m_checkAirfield <= 0)
		{
			m_checkAirfield = HOW_OFTEN_TO_CHECK;
			Object* airfield = findSuitableAirfield( jet );
			if (airfield)
			{
				jet->setProducer(airfield);
				return STATE_SUCCESS;
			}
		}

		return STATE_CONTINUE;
	}

};
EMPTY_DTOR(JetOrHeliCirclingDeadAirfieldState)

//-------------------------------------------------------------------------------------------------
/*
	Success: we returned to the dead-airfield location
	Failure: shouldn't normally happen
*/
class JetOrHeliReturningToDeadAirfieldState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReturningToDeadAirfieldState, "JetOrHeliReturningToDeadAirfieldState")		
public:
	JetOrHeliReturningToDeadAirfieldState( StateMachine *machine ) : AIInternalMoveToState( machine, "JetOrHeliReturningToDeadAirfieldState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		setAdjustsDestination(true);
		m_goalPosition = *jetAI->friend_getProducerLocation();

		return AIInternalMoveToState::onEnter();
	}

};
EMPTY_DTOR(JetOrHeliReturningToDeadAirfieldState)

//-------------------------------------------------------------------------------------------------
// This solution uses the 
// http://www.faqs.org/faqs/graphics/algorithms-faq/ 
// Subject 1.03
static Bool intersectInfiniteLine2D
(
	Real ax, Real ay, Real ao, 
	Real cx, Real cy, Real co, 
	Real& ix, Real& iy
)
{
	Real bx = ax + Cos(ao);
	Real by = ay + Sin(ao);
	Real dx = cx + Cos(co);
	Real dy = cy + Sin(co);

	Real denom = ((bx - ax) * (dy - cy) - (by - ay) * (dx - cx));
	if (denom == 0.0f) 
	{
		// the lines are parallel.
		return false;
	}

	// The lines intersect.
	Real r = ((ay - cy) * (dx - cx) - (ax - cx) * (dy - cy) ) / denom;
	ix = ax + r * (bx - ax);
	iy = ay + r * (by - ay);
	return true;
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class JetOrHeliTaxiState : public AIMoveOutOfTheWayState 
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliTaxiState, "JetOrHeliTaxiState")		
private:
	TaxiType m_taxiMode;
public:
	JetOrHeliTaxiState( StateMachine *machine, TaxiType m ) : m_taxiMode(m), AIMoveOutOfTheWayState( machine ) { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;
		
		jetAI->setCanPathThroughUnits(true);
		jetAI->friend_setTakeoffInProgress(m_taxiMode != TO_PARKING);
		jetAI->friend_setLandingInProgress(m_taxiMode == TO_PARKING);
		jetAI->friend_setTaxiInProgress(true);
		
		if( m_taxiMode == TO_PARKING )
		{
			//Instantly reload flares.
			CountermeasuresBehaviorInterface *cbi = jet->getCountermeasuresBehaviorInterface();
			if( cbi )
			{
				cbi->reloadCountermeasures();
			}
		}

		jetAI->friend_setAllowAirLoco(false);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);
		DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_FAILURE;	// no airfield left to taxi on
		
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_FAILURE;	// full?
		
		Coord3D intermedPt;
		Bool intermed = false;
		Real orient = ATan2(ppinfo.runwayPrep.y - ppinfo.parkingSpace.y, ppinfo.runwayPrep.x - ppinfo.parkingSpace.x);
		if (fabs(stdAngleDiff(orient, ppinfo.parkingOrientation)) > PI/128)
		{
			intermedPt.z = (ppinfo.parkingSpace.z + ppinfo.runwayPrep.z) * 0.5f;
			intermed = intersectInfiniteLine2D(
				ppinfo.parkingSpace.x, ppinfo.parkingSpace.y, ppinfo.parkingOrientation,
				ppinfo.runwayPrep.x, ppinfo.runwayPrep.y, ppinfo.parkingOrientation + PI/2,
				intermedPt.x, intermedPt.y);
		}

		jetAI->destroyPath();
		Path *movePath;
		movePath = newInstance(Path);
		Coord3D pos = *jet->getPosition();
		movePath->prependNode( &pos, LAYER_GROUND );
		movePath->markOptimized();

		if (m_taxiMode == TO_PARKING)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//We're on an aircraft carrier.
				const std::vector<Coord3D> *pTaxiLocations = pp->getTaxiLocations( jet->getID() );
				if( pTaxiLocations )
				{
					std::vector<Coord3D>::const_iterator it;
					for( it = pTaxiLocations->begin(); it != pTaxiLocations->end(); it++ )
					{
						movePath->appendNode( &*it, LAYER_GROUND );
					}
				}

				//We just landed... see if we can get a better space forward so we don't stop and pause 
				//at our initially assigned spot.
				Coord3D pos;
				pp->calcBestParkingAssignment( jet->getID(), &pos );

				movePath->appendNode( &pos, LAYER_GROUND );
			}
			else
			{
				//We're on a normal airfield
				movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				if (intermed)
					movePath->appendNode( &intermedPt, LAYER_GROUND );
				movePath->appendNode( &ppinfo.parkingSpace, LAYER_GROUND );
			}
		}
		else if (m_taxiMode == FROM_PARKING)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				if( !(ppinfo.runwayStart == ppinfo.runwayPrep) )	
				{
					movePath->appendNode( &ppinfo.runwayStart, LAYER_GROUND );
				}
			}
			else
			{
				if (intermed)
					movePath->appendNode( &intermedPt, LAYER_GROUND );
				movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				movePath->appendNode( &ppinfo.runwayStart, LAYER_GROUND );
			}
		}
		else if (m_taxiMode == FROM_HANGAR)
		{
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//Aircraft carrier
				if( jet->testStatus( OBJECT_STATUS_REASSIGN_PARKING ) )
				{
					//This status means we are being reassigned a parking space. We're not actually moving from the
					//hangar. So simply move to the new parking spot which was just switched from under us in
					//FlightDeckBehavior::update()
					jet->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_REASSIGN_PARKING ) );
					movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				}
				else
				{
					const std::vector<Coord3D> *pCreationLocations = pp->getCreationLocations( jet->getID() );
					if( !pCreationLocations )
					{
						DEBUG_CRASH( ("No creation locations specified for runway for JetAIBehavior -- taxiing from hanger (Kris).") );
						return STATE_FAILURE;
					}
					std::vector<Coord3D>::const_iterator it;
					Bool firstNode = TRUE;
					for( it = pCreationLocations->begin(); it != pCreationLocations->end(); it++ )
					{
						if( firstNode )
						{
							//Skip the first node because it's the creation location.
							firstNode = FALSE;
							continue;
						}
						movePath->appendNode( &*it, LAYER_GROUND );
					}
					movePath->appendNode( &ppinfo.runwayPrep, LAYER_GROUND );
				}
			}
			else
			{
				//Airfield
 				movePath->appendNode( &ppinfo.parkingSpace, LAYER_GROUND );
			}
		}

		// a jet on its wheels turns along each corner instead of reaching it and swinging round
		Locomotor* taxiLoco = jetAI->getCurLocomotor();
		if (taxiLoco->isGroundVehicle(jet))
		{
			std::vector<Coord3D> route;
			for (const PathNode* node = movePath->getFirstNode(); node; node = node->getNext())
				route.push_back(*node->getPosition());
			JetTaxi_roundCorners(route, taxiLoco->calcWheelTurnRadius(jet));
			movePath->deleteInstance();
			movePath = newInstance(Path);
			movePath->prependNode( &route[0], LAYER_GROUND );
			movePath->markOptimized();
			for (size_t i = 1; i < route.size(); ++i)
				movePath->appendNode( &route[i], LAYER_GROUND );
		}

		m_waitingForPath = FALSE;
		TheAI->pathfinder()->setDebugPath(movePath);

		setAdjustsDestination(false);	// precision is necessary

		jetAI->friend_setPath( movePath );
		DEBUG_ASSERTCRASH(jetAI->getCurLocomotor(), ("no loco"));
		jetAI->getCurLocomotor()->setUsePreciseZPos(true);
		jetAI->getCurLocomotor()->setUltraAccurate(true);
		jetAI->getCurLocomotor()->setAllowInvalidPosition(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		StateReturnType ret = AIMoveOutOfTheWayState::onEnter();
		return ret;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		if( m_taxiMode == TO_PARKING || m_taxiMode == FROM_HANGAR )
		{
			//Keep checking to see if there is a better spot as it moves forward. If we find a better spot, then
			//append the position to our move.
			ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
			Coord3D bestPos;
			Int oldIndex, newIndex;
			// Check pp for null, as it is possible for your airfield to get destroyed while taxiing.jba [8/27/2003]
			if( pp!=NULL && pp->calcBestParkingAssignment( jet->getID(), &bestPos, &oldIndex, &newIndex ) )
			{
				Path *path = jetAI->friend_getPath();
				if( path )
				{
					path->appendNode( &bestPos, LAYER_GROUND );
				}
			}
		}

		return AIMoveOutOfTheWayState::update();
	}

	virtual void onExit( StateExitType status )
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jetAI)
		{
			jetAI->getCurLocomotor()->setUsePreciseZPos(false);
			jetAI->getCurLocomotor()->setUltraAccurate(false);
			jetAI->getCurLocomotor()->setAllowInvalidPosition(false);
			jetAI->friend_setTakeoffInProgress(false);
			jetAI->friend_setLandingInProgress(false);
			jetAI->friend_setTaxiInProgress(false);
			jetAI->setCanPathThroughUnits(false);
		}

		AIMoveOutOfTheWayState::onExit(status);
	}

};
EMPTY_DTOR(JetOrHeliTaxiState)

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class JetTakeoffOrLandingState : public AIFollowPathState 
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetTakeoffOrLandingState, "JetTakeoffOrLandingState")		
private:
	Real			m_maxSpeed;
#ifdef CIRCLE_FOR_LANDING
	Coord3D		m_circleForLandingPos;
#endif
	Bool			m_landing;
	Bool			m_landingSoundPlayed;

public:
	JetTakeoffOrLandingState( StateMachine *machine, Bool landing ) : m_landing(landing), AIFollowPathState( machine, "JetTakeoffOrLandingState" ) { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (!jetAI)
			return STATE_FAILURE;

		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!m_landing);
		jetAI->friend_setLandingInProgress(m_landing);
		jetAI->friend_setAllowAirLoco(true);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		Locomotor* loco = jetAI->getCurLocomotor();
		DEBUG_ASSERTCRASH(loco, ("no loco"));
		loco->setMaxLift(BIGNUM);
		BodyDamageType bdt = jet->getBodyModule()->getDamageState();
		m_maxSpeed = loco->getMaxSpeedForCondition(bdt);
		m_landingSoundPlayed = FALSE;
		if (m_landing)
		{
			loco->setMaxSpeed(loco->getMinSpeed());
		}
		else
		{
			// full throttle down the runway; the height it is sent to keeps it on the wheels until liftoff
			loco->setNoSlowDownAsApproachingDest(true);
		}
		loco->setUsePreciseZPos(true);
		loco->setUltraAccurate(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_FAILURE;	// no airfield left to take off from or land on
		
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
		{
			// it's full.
			return STATE_FAILURE;
		}
		
		// only check this for landing; we might have already given up the reservation to the guy behind us for takeoff
		if (m_landing)
		{
			if (!pp->reserveRunway(jet->getID(), m_landing))
			{
				DEBUG_CRASH(("we should never get to this state unless we have a runway available"));
				return STATE_FAILURE;
			}
		}
		
		std::vector<Coord3D> path;
		if (m_landing)
		{
#ifdef CIRCLE_FOR_LANDING
			m_circleForLandingPos = ppinfo.runwayApproach;
			m_circleForLandingPos.z = (ppinfo.runwayEnd.z + ppinfo.runwayApproach.z)*0.5f;
#else
			path.push_back(ppinfo.runwayApproach);
#endif
			if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
			{
				//Assigned to an aircraft carrier which has separate landing strips.
				path.push_back( ppinfo.runwayLandingStart );
				path.push_back( ppinfo.runwayLandingEnd );
			}
			else
			{
				//Assigned to an airstrip -- land the same way we took off but in reverse.
				path.push_back(ppinfo.runwayEnd);
				path.push_back(ppinfo.runwayStart);
			}
		}
		else
		{
			ppinfo.runwayEnd.z = ppinfo.runwayApproach.z;
			path.push_back(ppinfo.runwayEnd);
			path.push_back(ppinfo.runwayExit);
		}

		setAdjustsDestination(false);	// precision is necessary
		setAdjustFinalDestination(false); // especially at the endpoint!

		jetAI->friend_setGoalPath( &path );

		StateReturnType ret = AIFollowPathState::onEnter();
		
		return ret;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;
		
		if (m_landing)
		{
#ifdef CIRCLE_FOR_LANDING
			if (jet->getPosition()->z > m_circleForLandingPos.z)
			{
				const Real THRESH = 4.0f;
				jetAI->getCurLocomotor()->setAltitudeChangeThresholdForCircling(THRESH);
				jetAI->setLocomotorGoalPositionExplicit(m_circleForLandingPos);
				return STATE_CONTINUE;
			}
			else
#endif
			{
				jetAI->getCurLocomotor()->setMaxLift(BIGNUM);
#ifdef CIRCLE_FOR_LANDING
				jetAI->getCurLocomotor()->setAltitudeChangeThresholdForCircling(0);
#endif
			}

			if( !m_landingSoundPlayed )
			{
				ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
				Real zPos = jet->getPosition()->z;
				Real zSlop = 0.25f;
				PathfindLayerEnum layer = TheTerrainLogic->getHighestLayerForDestination( jet->getPosition() );
				Real groundZ = TheTerrainLogic->getLayerHeight( jet->getPosition()->x, jet->getPosition()->y, layer );
				if( pp )
				{
					groundZ += pp->getLandingDeckHeightOffset();
				}
				
				if( zPos - zSlop <= groundZ )
				{
					m_landingSoundPlayed = TRUE;
					AudioEventRTS soundToPlay = TheAudio->getMiscAudio()->m_aircraftWheelScreech;
					soundToPlay.setPosition( jet->getPosition() );
					TheAudio->addAudioEvent( &soundToPlay );
				}
			}

			ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
			if (pp)
				return flyLanding(jet, jetAI, pp);
		}
		else
		{
			ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
			if (pp)
			{
				pp->transferRunwayReservationToNextInLineForTakeoff(jet->getID());
				return flyTakeoff(jet, jetAI, pp);
			}

			// the airfield can be sold or destroyed under a jet on its takeoff roll: no runway left
			// to measure against, so give it full lift and let it go
			jetAI->getCurLocomotor()->setMaxLift(BIGNUM);
		}

		StateReturnType ret = AIFollowPathState::update();
		return ret;
	}

private:

	/// steer down the runway's centre line and climb off it (see JET_LIFTOFF_SHARE)
	StateReturnType flyTakeoff(Object* jet, JetAIUpdate* jetAI, ParkingPlaceBehaviorInterface* pp)
	{
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		pp->calcPPInfo( jet->getID(), &ppinfo );
		const Coord3D& start = ppinfo.runwayStart;
		const Coord3D& end = ppinfo.runwayEnd;
		Real ux = end.x - start.x;
		Real uy = end.y - start.y;
		Real length = sqrt(ux*ux + uy*uy);
		if (length < 1.0f)
			return STATE_SUCCESS;
		ux /= length;
		uy /= length;

		// runwayStart may be the prep spot of a jet that was in line; the runway itself is runwayTakeoffDist long
		Real runway = ppinfo.runwayTakeoffDist;
		const Coord3D* pos = jet->getPosition();
		Real along = runway + (pos->x - end.x) * ux + (pos->y - end.y) * uy;
		Real runwayZ = end.z;
		if (along < runway && runway > 0.0f)
			runwayZ = start.z + (end.z - start.z) * ((along > 0.0f) ? along / runway : 0.0f);
		// off a deck, the climb starts from the deck the jet is drawn on
		Real deckOffset = pp->getLandingDeckHeightOffset();
		Real climbHeight = ppinfo.runwayExit.z - end.z - deckOffset;
		if (climbHeight < JET_GO_AROUND_HEIGHT)
			climbHeight = JET_GO_AROUND_HEIGHT;

		// a few frames of travel ahead, for the height to lead the lift that has to reach it
		Real speed = jet->getPhysics()->getForwardSpeed2D();
		Real lead = speed * JET_HEIGHT_LAG_FRAMES;
		Real height = JetRunway_slopeHeight(along + lead - JET_LIFTOFF_SHARE * runway, climbHeight, JET_CLIMB_SLOPE,
																				JET_ROTATE_SHARE * runway);
		if (height >= climbHeight)
			return STATE_SUCCESS;

		Coord3D goal;
		Real ahead = along - runway + calcLookAhead(jet, jetAI->getCurLocomotor());
		goal.x = end.x + ux * ahead;
		goal.y = end.y + uy * ahead;
		goal.z = (height > 0.0f) ? runwayZ + deckOffset + height : runwayZ;
		jetAI->setLocomotorGoalPositionExplicit(goal);
		return STATE_CONTINUE;
	}

	/// fly the circuit onto the final, down the glide slope, and roll out (see JET_GLIDE_SLOPE)
	StateReturnType flyLanding(Object* jet, JetAIUpdate* jetAI, ParkingPlaceBehaviorInterface* pp)
	{
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		pp->calcPPInfo( jet->getID(), &ppinfo );
		// a carrier lands on its own strip; an airfield the way it took off, the other way round
		Bool deck = jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET );
		const Coord3D& touch = deck ? ppinfo.runwayLandingStart : ppinfo.runwayEnd;
		const Coord3D& stop = deck ? ppinfo.runwayLandingEnd : ppinfo.runwayStart;
		Real ux = stop.x - touch.x;
		Real uy = stop.y - touch.y;
		Real runway = sqrt(ux*ux + uy*uy);
		if (runway < 1.0f)
			return STATE_SUCCESS;
		ux /= runway;
		uy /= runway;

		Locomotor* loco = jetAI->getCurLocomotor();
		BodyDamageType bdt = jet->getBodyModule()->getDamageState();
		Real minSpeed = loco->getMinSpeed();
		Real circuitSpeed = loco->getJetCornerSpeed(bdt);
		if (circuitSpeed < minSpeed)
			circuitSpeed = minSpeed;
		Real rate = loco->getJetMaxYawRate(bdt, circuitSpeed);
		Real radius = (rate > 0.0f) ? circuitSpeed / rate : 0.0f;
		Real deckOffset = pp->getLandingDeckHeightOffset();
		// the turn onto the final starts a radius further out than the final itself
		Real turnIn = JET_FINAL_DIST + radius;

		const Coord3D* pos = jet->getPosition();
		Real along = (pos->x - touch.x) * ux + (pos->y - touch.y) * uy;	// negative short of the threshold
		Real across = (pos->y - touch.y) * ux - (pos->x - touch.x) * uy;	// positive to the left
		Real runwayZ = stop.z;
		if (along < runway)
			runwayZ = touch.z + (stop.z - touch.z) * ((along > 0.0f) ? along / runway : 0.0f);
		Real height = pos->z - runwayZ - deckOffset;
		Real offLine = stdAngleDiff(jet->getOrientation(), ATan2(uy, ux));
		Real speed = jet->getPhysics()->getForwardSpeed2D();
		Real lead = speed * JET_HEIGHT_LAG_FRAMES;

		// the distance flown to the threshold, round what is left of the turn onto the centre line
		Real toThreshold = -along + radius * fabs(offLine);
		Real glide = JetRunway_slopeHeight(toThreshold - lead, BIGNUM, JET_GLIDE_SLOPE, JET_FLARE_DIST);
		Bool linedUp = along < 0.0f && fabs(offLine) < PI / 2.0f && height <= glide + JET_HIGH_ON_FINAL &&
			(fabs(across) <= JET_LINE_UP_SLACK || -along >= JET_LINE_UP_ROOM * fabs(across));
		Bool rollingOut = along >= 0.0f && height <= JET_GO_AROUND_HEIGHT;

		Coord3D goal;
		Real wantSpeed = circuitSpeed;
		if (linedUp || rollingOut || along <= -turnIn)
		{
			// on the final, or far enough out to come round onto it: steer for a point ahead on the centre line
			Real ahead = along + calcLookAhead(jet, loco);
			goal.x = touch.x + ux * ahead;
			goal.y = touch.y + uy * ahead;

			if (along >= 0.0f)
			{
				if (along >= runway - JET_LINE_UP_SLACK)
					return STATE_SUCCESS;
				// slow down the length of the runway, to a crawl at its end
				Real endSpeed = JET_ROLLOUT_END_SHARE * minSpeed;
				wantSpeed = endSpeed + (minSpeed - endSpeed) * (runway - along) / runway;
			}
			else if (-along < turnIn)
			{
				// down the final from the circuit speed to MinSpeed over the threshold
				wantSpeed = minSpeed + (circuitSpeed - minSpeed) * (-along / turnIn);
			}
		}
		else
		{
			// the circuit: out along the side of the runway the jet is heading for, two radii off the
			// centre line, to where the turn in starts
			Real sideways = across + 2.0f * radius * Sin(offLine);
			Real side = (sideways >= 0.0f) ? 2.0f * radius : -2.0f * radius;
			goal.x = touch.x - ux * turnIn - uy * side;
			goal.y = touch.y - uy * turnIn + ux * side;
			Real toTurn = sqrt(sqr(goal.x - pos->x) + sqr(goal.y - pos->y));
			glide = JetRunway_slopeHeight(toTurn + PI * radius + turnIn - lead, BIGNUM, JET_GLIDE_SLOPE, JET_FLARE_DIST);
		}

		// never climb back up to the glide, and come down to it no steeper than JET_STEEPEST_DESCENT
		Real wantHeight = (glide < height) ? glide : height;
		Real steepest = height - JET_HEIGHT_LAG_FRAMES * JET_STEEPEST_DESCENT * speed;
		if (wantHeight < steepest && !rollingOut)
			wantHeight = steepest;
		goal.z = (wantHeight > 0.0f) ? runwayZ + deckOffset + wantHeight : runwayZ;

		loco->setMaxSpeed(wantSpeed);
		jetAI->setLocomotorGoalPositionExplicit(goal);
		return STATE_CONTINUE;
	}

	/// how far ahead on the centre line to steer for: twice the jet's turn radius at its speed, at least JET_MIN_LOOKAHEAD
	static Real calcLookAhead(Object* jet, Locomotor* loco)
	{
		Real speed = jet->getPhysics()->getForwardSpeed2D();
		if (speed < loco->getMinSpeed())
			speed = loco->getMinSpeed();
		Real rate = loco->getJetMaxYawRate(jet->getBodyModule()->getDamageState(), speed);
		Real look = (rate > 0.0f) ? 2.0f * speed / rate : JET_MIN_LOOKAHEAD;
		return (look > JET_MIN_LOOKAHEAD) ? look : JET_MIN_LOOKAHEAD;
	}

public:

	virtual void onExit( StateExitType status )
	{
		AIFollowPathState::onExit(status);

		// just in case.
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->friend_enableAfterburners(false);

		// Paranoia checks - sometimes onExit is called when we are 
		// shutting down, and not all pieces are valid.  CurLocomotor
		// is definitely null in some cases. jba.
		Locomotor* loco = jetAI->getCurLocomotor();
		if (loco)
		{
			loco->setUsePreciseZPos(false);
			loco->setUltraAccurate(false);
			loco->setNoSlowDownAsApproachingDest(false);
			// don't restore lift if dead -- this may fight with JetSlowDeathBehavior!
			if (!jet->isEffectivelyDead())
				loco->setMaxLift(BIGNUM);
#ifdef CIRCLE_FOR_LANDING
			loco->setAltitudeChangeThresholdForCircling(0);
#endif
		}
		jetAI->ignoreObstacleID(INVALID_ID);
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (!m_landing)
		{
			if (pp && !jetAI->friend_keepsParkingSpaceWhenAirborne())
				pp->releaseSpace(jet->getID());
		}
		if (pp)
			pp->releaseRunway(jet->getID());
	}
};
EMPTY_DTOR(JetTakeoffOrLandingState)

//-------------------------------------------------------------------------------------------------
static Real calcDistSqr(const Coord3D& a, const Coord3D& b)
{
	return sqr(a.x-b.x) + sqr(a.y-b.y) + sqr(a.z-b.z);
}

//-------------------------------------------------------------------------------------------------
/*
	Success: we are on the ground at the runway start
	Failure: we are unable to get on the ground
*/
class HeliTakeoffOrLandingState : public State 
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(HeliTakeoffOrLandingState, "HeliTakeoffOrLandingState")		
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
		XferVersion currentVersion = 1;
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&m_landing);
		xfer->xferCoord3D(&m_path[0]);
		xfer->xferCoord3D(&m_path[1]);
		xfer->xferInt(&m_index);
		xfer->xferCoord3D(&m_parkingLoc);
		xfer->xferReal(&m_parkingOrientation);
	}
	virtual void loadPostProcess()
	{
		// empty. jba.
	}

private:
	Coord3D		m_path[2];
	Int				m_index;
	Coord3D		m_parkingLoc;
	Real			m_parkingOrientation;
	Bool			m_landing;
public:
	HeliTakeoffOrLandingState( StateMachine *machine, Bool landing ) : m_landing(landing), 
		State( machine, "HeliTakeoffOrLandingState" ), m_index(0)
		{ 
			m_parkingLoc.zero();
		}

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(!m_landing);
		jetAI->friend_setLandingInProgress(m_landing);
		jetAI->friend_setAllowAirLoco(true);
		jetAI->chooseLocomotorSet(LOCOMOTORSET_NORMAL);

		Locomotor* loco = jetAI->getCurLocomotor();
		DEBUG_ASSERTCRASH(loco, ("no loco"));
		loco->setUsePreciseZPos(true);
		loco->setUltraAccurate(true);
		jetAI->ignoreObstacleID(jet->getProducerID());

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_FAILURE;	// no helipad left to take off from or land on
		
		Coord3D landingApproach;
		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			if (m_landing)
			{
				m_parkingLoc = jetAI->friend_getLandingPosForHelipadStuff();
				m_parkingOrientation = jet->getOrientation();
			}
			else
			{
				m_parkingOrientation = jet->getOrientation();
				m_parkingLoc = *jet->getPosition();
			}
			landingApproach = m_parkingLoc;
			landingApproach.z += pp->getApproachHeight() + pp->getLandingDeckHeightOffset();
		}
		else
		{
			ParkingPlaceBehaviorInterface::PPInfo ppinfo;
			if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
				return STATE_FAILURE;
			m_parkingLoc = ppinfo.parkingSpace;
			m_parkingOrientation = ppinfo.parkingOrientation;
			landingApproach = m_parkingLoc;
			landingApproach.z += (ppinfo.runwayApproach.z - ppinfo.runwayEnd.z);
		}
		
		if (m_landing)
		{
			m_path[0] = landingApproach;
			m_path[1] = m_parkingLoc;
		}
		else
		{
			m_path[0] = m_parkingLoc;
			m_path[1] = landingApproach;
			m_path[1].z = landingApproach.z;
		}
		m_index = 0;

		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

// I have disabled this because it is no longer necessary and is a bit funky lookin' (srj)
#ifdef NOT_IN_USE
		// magically position it correctly.
		jet->getPhysics()->scrubVelocity2D(0);
		Coord3D hoverloc = m_path[m_index];
		hoverloc.z = jet->getPosition()->z;
#if 1
		Coord3D pos = *jet->getPosition();
		Real dx = hoverloc.x - pos.x;
		Real dy = hoverloc.y - pos.y;
		Real dSqr = dx*dx+dy*dy;
		const Real DARN_CLOSE = 0.25f;
		if (dSqr < DARN_CLOSE) 
		{
			jet->setPosition(&hoverloc);
		} 
		else 
		{
			Real dist = sqrtf(dSqr);
			if (dist<1) dist = 1;
			pos.x += PATHFIND_CELL_SIZE_F*dx/(dist*LOGICFRAMES_PER_GAME_SECOND);
			pos.y += PATHFIND_CELL_SIZE_F*dy/(dist*LOGICFRAMES_PER_GAME_SECOND);
			jet->setPosition(&pos);
		}
#else
		jet->setPosition(&hoverloc);
#endif
		jet->setOrientation(m_parkingOrientation);
#endif

		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) || !m_landing)
		{
			TheAI->pathfinder()->adjustDestination(jet, jetAI->getLocomotorSet(), &m_path[m_index]);
			TheAI->pathfinder()->updateGoal(jet, &m_path[m_index], LAYER_GROUND);
		}

		jetAI->setLocomotorGoalPositionExplicit(m_path[m_index]);

		const Real THRESH = 3.0f;
		const Real THRESH_SQR = THRESH*THRESH;
		const Coord3D* a = jet->getPosition();
		const Coord3D* b = &m_path[m_index];
		Real distSqr = calcDistSqr(*a, *b);
		if (distSqr <= THRESH_SQR)
			++m_index;
		
		if (m_index >= 2)
			return STATE_SUCCESS;

		return STATE_CONTINUE;
	}

	virtual void onExit( StateExitType status )
	{
		// just in case.
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);

		// Paranoia checks - sometimes onExit is called when we are 
		// shutting down, and not all pieces are valid.  CurLocomotor
		// is definitely null in some cases. jba.
		Locomotor* loco = jetAI->getCurLocomotor();
		if (loco)
		{
			loco->setUsePreciseZPos(false);
			loco->setUltraAccurate(false);
			// don't restore lift if dead -- this may fight with JetSlowDeathBehavior!
			if (!jet->isEffectivelyDead())
				loco->setMaxLift(BIGNUM);
		}

		jetAI->ignoreObstacleID(INVALID_ID);
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (m_landing)
		{
			jetAI->friend_setAllowAirLoco(false);
			jetAI->chooseLocomotorSet(LOCOMOTORSET_TAXIING);
		}
		else
		{
			if (pp && !jetAI->friend_keepsParkingSpaceWhenAirborne())
				pp->releaseSpace(jet->getID());
		}
	}

};
EMPTY_DTOR(HeliTakeoffOrLandingState)

//-------------------------------------------------------------------------------------------------
class JetOrHeliParkOrientState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliParkOrientState, "JetOrHeliParkOrientState")		
protected:
	// snapshot interface STUBBED.
	virtual void crc( Xfer *xfer ){};
	virtual void xfer( Xfer *xfer ){XferVersion cv = 1;	XferVersion v = cv; xfer->xferVersion( &v, cv );}
	virtual void loadPostProcess(){};

public:
	JetOrHeliParkOrientState( StateMachine *machine ) : State( machine, "JetOrHeliParkOrientState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			return STATE_SUCCESS;
		}

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(true);

		jetAI->ignoreObstacleID(jet->getProducerID());
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
		{
			return STATE_FAILURE;
		}

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_FAILURE;
		
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_FAILURE;

		const Real THRESH = 0.001f;
		if (fabs(stdAngleDiff(jet->getOrientation(), ppinfo.parkingOrientation)) <= THRESH)
			return STATE_SUCCESS;

		// magically position it correctly.
		jet->getPhysics()->scrubVelocity2D(0);
		Coord3D hoverloc = ppinfo.parkingSpace;
		if( jet->testStatus( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
		{
			hoverloc = ppinfo.runwayPrep;
		}

		hoverloc.z = jet->getPosition()->z;
		jet->setPosition(&hoverloc);

		jetAI->setLocomotorGoalOrientation(ppinfo.parkingOrientation);

		return STATE_CONTINUE;
	}

	virtual void onExit( StateExitType status )
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->ignoreObstacleID(INVALID_ID);
	}
};
EMPTY_DTOR(JetOrHeliParkOrientState)

//-------------------------------------------------------------------------------------------------
class JetPauseBeforeTakeoffState : public AIFaceState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetPauseBeforeTakeoffState, "JetPauseBeforeTakeoffState")		
protected:
	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
		XferVersion currentVersion = 2;
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&m_landing);
		xfer->xferUnsignedInt(&m_whenTakeoff);
		xfer->xferUnsignedInt(&m_whenTransfer);

		if (version <= 1)
		{
			// the pre-rewrite state kept two flags here; read past them and throw them away.
			Bool afterburners = false;
			Bool resetTimer = false;
			xfer->xferBool(&afterburners);
			xfer->xferBool(&resetTimer);
		}

		xfer->xferObjectID(&m_waitedForTaxiID);
	}
	virtual void loadPostProcess()
	{
		// empty. jba.
	}

private:
	UnsignedInt		m_whenTakeoff;
	UnsignedInt		m_whenTransfer;
	ObjectID			m_waitedForTaxiID;

	/**
		The jet on the *other* runway that we have to let go first, or NULL if nobody is in our way.
		The runway index is what makes this correct: our own runway's reservation is us, and a jet
		still taxiing on it is the one behind us, which must wait for us, not the other way round.
		(Written for two runways.  More than two would want another look.)
	*/
	Object* findJetToWaitFor() const
	{
		const Object* thisJet = getMachineOwner();
		ParkingPlaceBehaviorInterface* pp = getPP(thisJet->getProducerID());
		if (pp)
		{
			const Int thisJetRunway = pp->getRunwayIndex(thisJet->getID());
			const Int runwayCount = pp->getRunwayCount();

			for (Int runway = 0; runway < runwayCount; ++runway)
			{
				if (runway == thisJetRunway)
					continue;

				Object* otherJet = TheGameLogic->findObjectByID( pp->getRunwayReservation( runway, RESERVATION_TAKEOFF ) );
				if (otherJet == NULL)
					continue;

				AIUpdateInterface* ai = otherJet->getAIUpdateInterface();
				if (ai == NULL)
					continue;

				if (ai->getCurrentStateID() != TAXI_TO_TAKEOFF)
					continue;

				return otherJet;
			}
		}
		return NULL;
	}

public:
	JetPauseBeforeTakeoffState( StateMachine *machine ) : 
		AIFaceState(machine, false), 
		m_whenTakeoff(0), 
		m_whenTransfer(0),
		m_waitedForTaxiID(INVALID_ID)
	{ 
		// nothing
	}

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(true);
		jetAI->friend_setLandingInProgress(false);

		m_whenTakeoff = 0;
		m_whenTransfer = 0;
		m_waitedForTaxiID = INVALID_ID;

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_SUCCESS;	// no airfield? just skip this step.
		
		ParkingPlaceBehaviorInterface::PPInfo ppinfo;
		if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
			return STATE_SUCCESS;	// full?

		getMachine()->setGoalPosition(&ppinfo.runwayEnd);
		
		return AIFaceState::onEnter();
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if (jet->isEffectivelyDead())
			return STATE_FAILURE;

		// always call this.
		StateReturnType superStatus = AIFaceState::update();

		Object* otherJet = findJetToWaitFor();
		if (otherJet != NULL)
		{
			if (m_waitedForTaxiID == INVALID_ID)
			{
				// remember who we are waiting for, so a jet that starts taxiing after we picked
				// ours cannot make us wait a second time (that was the endless-wait bug).
				m_waitedForTaxiID = otherJet->getID();
			}

			if (m_waitedForTaxiID == otherJet->getID())
				return STATE_CONTINUE;
		}

		const UnsignedInt now = TheGameLogic->getFrame();

		if (m_whenTakeoff == 0)
		{
			// whoever we were waiting for is out of the way: set the countdown, once.
			if (m_waitedForTaxiID == INVALID_ID)
			{
				m_waitedForTaxiID = jet->getID();	// just so we don't pick up anyone else
				m_whenTransfer = now + 1;
			}
			else
			{
				m_whenTransfer = now + 2;	// 2 seems odd, but is correct
			}

			// take off soon, but never before the runway has been handed on.
			m_whenTakeoff = now + jetAI->friend_getTakeoffPause();
			if (m_whenTakeoff < m_whenTransfer)
				m_whenTakeoff = m_whenTransfer;

			jetAI->friend_enableAfterburners(true);
		}
		else
		{
			// once we start the final wait, release the runways for guys behind us, so they can start taxiing
			if (now >= m_whenTransfer)
			{
				ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
				if (pp)
				{
					pp->transferRunwayReservationToNextInLineForTakeoff(jet->getID());
				}
				m_whenTransfer = ~0u;	// hand it on once, not every frame afterwards
			}

			if (now >= m_whenTakeoff)
				return superStatus;
		}

		return STATE_CONTINUE;
	}

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		AIFaceState::onExit(status);
	}

};
EMPTY_DTOR(JetPauseBeforeTakeoffState)

//-------------------------------------------------------------------------------------------------
class JetOrHeliReloadAmmoState : public State
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReloadAmmoState, "JetOrHeliReloadAmmoState")		
private:
	UnsignedInt		m_reloadTime;
	UnsignedInt		m_reloadDoneFrame;

protected:

	// snapshot interface
	virtual void crc( Xfer *xfer )
	{
		// empty. jba.
	}

	virtual void xfer( Xfer *xfer )
	{
		// version
		XferVersion currentVersion = 1;
		XferVersion version = currentVersion;
		xfer->xferVersion( &version, currentVersion );

		// set on create. xfer->xferBool(&m_landing);
		xfer->xferUnsignedInt(&m_reloadTime);
		xfer->xferUnsignedInt(&m_reloadDoneFrame);
	}
	virtual void loadPostProcess()
	{
		// empty. jba.
	}

public:
	JetOrHeliReloadAmmoState( StateMachine *machine ) : State( machine, "JetOrHeliReloadAmmoState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		if( !jetAI )
			return STATE_FAILURE;

		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
		jetAI->friend_setUseSpecialReturnLoco(false);
		
		m_reloadTime = 0;
		for (Int i = 0; i < WEAPONSLOT_COUNT;	++i)
		{
			const Weapon* w = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
			if (w == NULL)
				continue;
			
			Int remaining = w->getRemainingAmmo();
			Int clipSize = w->getClipSize();
			Int rt = w->getClipReloadTime(jet);
			if (clipSize > 0)
			{
				// bias by amount empty.
				Int needed = clipSize - remaining;
				rt = (rt * needed) / clipSize;
			}
			if (rt > m_reloadTime)
				m_reloadTime = rt;
		}
		
		if (m_reloadTime < 1)
			m_reloadTime = 1;
		m_reloadDoneFrame = m_reloadTime + TheGameLogic->getFrame();
		return STATE_CONTINUE;
	}

	virtual StateReturnType update()
	{
		Object* jet = getMachineOwner();

		// the pad can be destroyed while the aircraft is sitting on it, rearming
		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
			return STATE_FAILURE;

		UnsignedInt now = TheGameLogic->getFrame();
		Bool allDone = true;
		for (Int i = 0; i < WEAPONSLOT_COUNT;	++i)
		{
			Weapon* w = jet->getWeaponInWeaponSlot((WeaponSlotType)i);
			if (w == NULL)
				continue;
			
			if (now >= m_reloadDoneFrame)
				w->setClipPercentFull(1.0f, false);
			else
				w->setClipPercentFull((Real)(m_reloadTime - (m_reloadDoneFrame - now)) / m_reloadTime, false);

			if (w->getRemainingAmmo() != w->getClipSize())
				allDone = false;
		}

		if (allDone)
		{
			return STATE_SUCCESS;
		}

		return STATE_CONTINUE;
	}

	virtual void onExit(StateExitType status)
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();
		jetAI->friend_setTakeoffInProgress(false);
		jetAI->friend_setLandingInProgress(false);
	}

};
EMPTY_DTOR(JetOrHeliReloadAmmoState)

//-------------------------------------------------------------------------------------------------
/*
	Success: we are close enough to a friendly airfield to land
	Failure: we are unable to get close enough to a friendly airfield to land
*/
class JetOrHeliReturnForLandingState : public AIInternalMoveToState
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JetOrHeliReturnForLandingState, "JetOrHeliReturnForLandingState")		
public:
	JetOrHeliReturnForLandingState( StateMachine *machine ) : AIInternalMoveToState( machine, "JetOrHeliReturnForLandingState") { }

	virtual StateReturnType onEnter()
	{
		Object* jet = getMachineOwner();
		JetAIUpdate* jetAI = (JetAIUpdate*)jet->getAIUpdateInterface();

		ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
		if (pp == NULL)
		{
			// nuke the producer id, since it's dead
			jet->setProducer(NULL);

			Object* airfield = findSuitableAirfield( jet );
			pp = airfield ? getPP(airfield->getID()) : NULL;
			if (airfield && pp)
			{
				jet->setProducer(airfield);
			}
			else
			{
				return STATE_FAILURE;
			}
		}
		
		if (jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		{
			m_goalPosition = jetAI->friend_getLandingPosForHelipadStuff();
		}
		else
		{
			ParkingPlaceBehaviorInterface::PPInfo ppinfo;
			if (!pp->reserveSpace(jet->getID(), jetAI->friend_getParkingOffset(), &ppinfo))
				return STATE_FAILURE;

			m_goalPosition = jetAI->friend_needsRunway() ? ppinfo.runwayApproach : ppinfo.parkingSpace;
		}
		setAdjustsDestination(false);		// precision is necessary

		return AIInternalMoveToState::onEnter();
	}
};
EMPTY_DTOR(JetOrHeliReturnForLandingState)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class JetAIStateMachine : public AIStateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( JetAIStateMachine, "JetAIStateMachine" );

public:
	JetAIStateMachine( Object *owner, AsciiString name );

};

//-------------------------------------------------------------------------------------------------
JetAIStateMachine::JetAIStateMachine(Object *owner, AsciiString name) : AIStateMachine(owner, name)
{
	defineState( RETURNING_FOR_LANDING, newInstance(JetOrHeliReturnForLandingState)( this ), LANDING_AWAIT_CLEARANCE, RETURN_TO_DEAD_AIRFIELD );
	defineState( TAKING_OFF_AWAIT_CLEARANCE, newInstance(JetAwaitingRunwayState)( this, false ), TAXI_TO_TAKEOFF, AI_IDLE );
	defineState( TAXI_TO_TAKEOFF, newInstance(JetOrHeliTaxiState)( this, FROM_PARKING ), PAUSE_BEFORE_TAKEOFF, AI_IDLE );
	defineState( PAUSE_BEFORE_TAKEOFF, newInstance(JetPauseBeforeTakeoffState)( this ), TAKING_OFF, AI_IDLE );
	defineState( TAKING_OFF, newInstance(JetTakeoffOrLandingState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( LANDING_AWAIT_CLEARANCE, newInstance(JetAwaitingRunwayState)( this, true ), LANDING, AI_IDLE );
	defineState( LANDING, newInstance(JetTakeoffOrLandingState)( this, true ), TAXI_FROM_LANDING, AI_IDLE );
	defineState( TAXI_FROM_LANDING, newInstance(JetOrHeliTaxiState)( this, TO_PARKING ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( TAXI_FROM_HANGAR, newInstance(JetOrHeliTaxiState)( this, FROM_HANGAR ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( ORIENT_FOR_PARKING_PLACE, newInstance(JetOrHeliParkOrientState)( this ), RELOAD_AMMO, AI_IDLE );
	defineState( RELOAD_AMMO, newInstance(JetOrHeliReloadAmmoState)( this ), AI_IDLE, AI_IDLE );
	defineState( RETURN_TO_DEAD_AIRFIELD, newInstance(JetOrHeliReturningToDeadAirfieldState)( this ), CIRCLING_DEAD_AIRFIELD, RETURN_TO_DEAD_AIRFIELD );
	// a free space found goes straight to landing on it: through idle, a jet still armed waited out
	// ReturnToBaseIdleTime holding the space, and one with no such time never came down at all
	defineState( CIRCLING_DEAD_AIRFIELD, newInstance(JetOrHeliCirclingDeadAirfieldState)( this ), RETURNING_FOR_LANDING, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
JetAIStateMachine::~JetAIStateMachine()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
class HeliAIStateMachine : public AIStateMachine
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( HeliAIStateMachine, "HeliAIStateMachine" );

public:
	HeliAIStateMachine( Object *owner, AsciiString name );

};

//-------------------------------------------------------------------------------------------------
HeliAIStateMachine::HeliAIStateMachine(Object *owner, AsciiString name) : AIStateMachine(owner, name)
{
	defineState( RETURNING_FOR_LANDING, newInstance(JetOrHeliReturnForLandingState)( this ), LANDING_AWAIT_CLEARANCE, RETURN_TO_DEAD_AIRFIELD );
	defineState( TAKING_OFF_AWAIT_CLEARANCE, newInstance(SuccessState)( this ), TAKING_OFF, AI_IDLE );
	defineState( TAKING_OFF, newInstance(HeliTakeoffOrLandingState)( this, false ), AI_IDLE, AI_IDLE );
	defineState( LANDING_AWAIT_CLEARANCE, newInstance(SuccessState)( this ), ORIENT_FOR_PARKING_PLACE, AI_IDLE );
	defineState( ORIENT_FOR_PARKING_PLACE, newInstance(JetOrHeliParkOrientState)( this ), LANDING, AI_IDLE );
	defineState( LANDING, newInstance(HeliTakeoffOrLandingState)( this, true ), RELOAD_AMMO, TAKING_OFF );
	defineState( RELOAD_AMMO, newInstance(JetOrHeliReloadAmmoState)( this ), AI_IDLE, TAKING_OFF );
	defineState( RETURN_TO_DEAD_AIRFIELD, newInstance(JetOrHeliReturningToDeadAirfieldState)( this ), CIRCLING_DEAD_AIRFIELD, RETURN_TO_DEAD_AIRFIELD );
	defineState( CIRCLING_DEAD_AIRFIELD, newInstance(JetOrHeliCirclingDeadAirfieldState)( this ), AI_IDLE, AI_IDLE );
	defineState( TAXI_FROM_HANGAR, newInstance(JetOrHeliTaxiState)( this, FROM_HANGAR ), AI_IDLE, AI_IDLE );
}

//-------------------------------------------------------------------------------------------------
HeliAIStateMachine::~HeliAIStateMachine()
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
JetAIUpdateModuleData::JetAIUpdateModuleData()
{
	m_outOfAmmoDamagePerSecond = 0;
	m_needsRunway = true;
	m_keepsParkingSpaceWhenAirborne = true;
	m_takeoffDistForMaxLift = 0.0f;
	m_minHeight = 0.0f;
	m_parkingOffset = 0.0f;
	m_sneakyOffsetWhenAttacking = 0.0f;
	m_takeoffPause = 0;
	m_attackingLoco = LOCOMOTORSET_NORMAL;
	m_returningLoco = LOCOMOTORSET_NORMAL;
	m_attackLocoPersistTime = 0;
	m_attackersMissPersistTime = 0;
	m_lockonTime = 0;
	m_lockonCursor.clear();
	m_lockonInitialDist = 100;
	m_lockonFreq = ConvertRetailPerFrameRate(0.5f);
	m_lockonAngleSpin = 720;
	m_returnToBaseIdleTime = 0;
	m_kamikazeRange = 0.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void JetAIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  AIUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "OutOfAmmoDamagePerSecond",			INI::parsePercentToReal, NULL, offsetof( JetAIUpdateModuleData, m_outOfAmmoDamagePerSecond ) },
		{ "NeedsRunway",									INI::parseBool, NULL, offsetof( JetAIUpdateModuleData, m_needsRunway ) },
		{ "KeepsParkingSpaceWhenAirborne",INI::parseBool, NULL, offsetof( JetAIUpdateModuleData, m_keepsParkingSpaceWhenAirborne ) },
		{ "TakeoffDistForMaxLift",				INI::parsePercentToReal, NULL, offsetof( JetAIUpdateModuleData, m_takeoffDistForMaxLift ) },
		{ "TakeoffPause",									INI::parseDurationUnsignedInt, NULL, offsetof( JetAIUpdateModuleData, m_takeoffPause ) },
		{ "MinHeight",										INI::parseReal, NULL, offsetof( JetAIUpdateModuleData, m_minHeight ) },
		{ "ParkingOffset",								INI::parseReal, NULL, offsetof( JetAIUpdateModuleData, m_parkingOffset ) },
		{ "SneakyOffsetWhenAttacking",		INI::parseReal, NULL, offsetof( JetAIUpdateModuleData, m_sneakyOffsetWhenAttacking ) },
		{ "AttackLocomotorType",					INI::parseIndexList, TheLocomotorSetNames, offsetof( JetAIUpdateModuleData, m_attackingLoco ) },
		{ "AttackLocomotorPersistTime",		INI::parseDurationUnsignedInt, NULL, offsetof( JetAIUpdateModuleData, m_attackLocoPersistTime ) },
		{ "AttackersMissPersistTime",			INI::parseDurationUnsignedInt, NULL, offsetof( JetAIUpdateModuleData, m_attackersMissPersistTime ) },
		{ "ReturnForAmmoLocomotorType",		INI::parseIndexList, TheLocomotorSetNames, offsetof( JetAIUpdateModuleData, m_returningLoco ) },
		{ "LockonTime",										INI::parseDurationUnsignedInt, NULL, offsetof( JetAIUpdateModuleData, m_lockonTime ) },
		{ "LockonCursor",									INI::parseAsciiString, NULL, offsetof( JetAIUpdateModuleData, m_lockonCursor ) },
		{ "LockonInitialDist",						INI::parseReal, NULL, offsetof( JetAIUpdateModuleData, m_lockonInitialDist ) },
		// ticks scale with frames squared over LockonTime, so per frame it halves
		{ "LockonFreq",										INI::parseRetailPerFrameReal, NULL, offsetof( JetAIUpdateModuleData, m_lockonFreq ) },
		{ "LockonAngleSpin",							INI::parseAngleReal, NULL, offsetof( JetAIUpdateModuleData, m_lockonAngleSpin ) },
		{ "LockonBlinky",									INI::parseBool, NULL, offsetof( JetAIUpdateModuleData, m_lockonBlinky ) },
		{ "ReturnToBaseIdleTime",					INI::parseDurationUnsignedInt, NULL, offsetof( JetAIUpdateModuleData, m_returnToBaseIdleTime ) },
		{ "KamikazeRange",								INI::parseReal, NULL, offsetof( JetAIUpdateModuleData, m_kamikazeRange ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
AIStateMachine* JetAIUpdate::makeStateMachine()
{
	if (getJetAIUpdateModuleData()->m_needsRunway)
		return newInstance(JetAIStateMachine)( getObject(), "JetAIStateMachine");
	else
		return newInstance(HeliAIStateMachine)( getObject(), "HeliAIStateMachine");
}

//-------------------------------------------------------------------------------------------------
JetAIUpdate::JetAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_flags = 0;
	m_afterburnerSound = *(getObject()->getTemplate()->getPerUnitSound("Afterburner"));
	m_afterburnerSound.setObjectID(getObject()->getID());
	m_attackLocoExpireFrame = 0;
	m_attackersMissExpireFrame = 0;
	m_untargetableExpireFrame = 0;
	m_returnToBaseFrame = 0;
	m_lockonDrawable = NULL;
	m_landingPosForHelipadStuff.zero();
	m_kamikazeArea.zero();

	//Added By Sadullah Nader
	//Initializations missing and needed 
	m_producerLocation.zero();
	//
	m_enginesOn = TRUE;
}

//-------------------------------------------------------------------------------------------------
JetAIUpdate::~JetAIUpdate()
{
	if (m_lockonDrawable)
	{
		TheGameClient->destroyDrawable(m_lockonDrawable);
		m_lockonDrawable = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isIdle() const
{
	// we need to do this because we enter an idle state briefly between takeoff/landing in these cases,
	// but scripting relies on us never claiming to be "idle"...
	if (getFlag(HAS_PENDING_COMMAND))
		return false;

	return AIUpdateInterface::isIdle();
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isReloading() const
{
	StateID stateID = getStateMachine()->getCurrentStateID();
	if( stateID == RELOAD_AMMO )
	{
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isTaxiingToParking() const
{
	StateID stateID = getStateMachine()->getCurrentStateID();
	switch( stateID )
	{
		case TAXI_FROM_HANGAR:
		case TAXI_FROM_LANDING:
		case ORIENT_FOR_PARKING_PLACE:
		case RELOAD_AMMO:
		case TAKING_OFF_AWAIT_CLEARANCE:
		case TAXI_TO_TAKEOFF:
		case PAUSE_BEFORE_TAKEOFF:
		case TAKING_OFF:
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::onObjectCreated()
{
	AIUpdateInterface::onObjectCreated();
	friend_setAllowAirLoco(false);
	chooseLocomotorSet(LOCOMOTORSET_TAXIING);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::onDelete()
{
	AIUpdateInterface::onDelete();
	ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID());
	if (pp)
		pp->releaseSpace(getObject()->getID());
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::getProducerLocation()
{
	if (getFlag(HAS_PRODUCER_LOCATION))
		return;

	Object* jet = getObject();
	Object* airfield = TheGameLogic->findObjectByID( jet->getProducerID() );
	if (airfield == NULL)
		m_producerLocation = *jet->getPosition();
	else
		m_producerLocation = *airfield->getPosition();

	/*
		if we aren't allowed to fly, then we should be parked (or at least taxiing),
		which implies we have a parking place reserved. If we don't, it's probably
		because we were directly spawned via script (or directly placed on the map).
		So, check to see if we have no parking place, and if not, quietly enable flight.
	*/
	ParkingPlaceBehaviorInterface* pp = getPP(jet->getProducerID());
	if (!pp || !pp->hasReservedSpace(jet->getID()))
	{
		friend_setAllowAirLoco(true);
		chooseLocomotorSet(LOCOMOTORSET_NORMAL);
	}
	else
	{
		friend_setAllowAirLoco(false);
		chooseLocomotorSet(LOCOMOTORSET_TAXIING);
	}

	setFlag(HAS_PRODUCER_LOCATION, true);

}

//-------------------------------------------------------------------------------------------------
/// a kamikaze looking for a new target weighs this many of the dearest in its area
static const Int KAMIKAZE_RETARGET_CANDIDATES = 32;

/// what the other live drones of one kind and owner are heading for
struct KamikazeHeadings
{
	const Object* self;
	std::vector<ObjectID> targets;
};

static void collectKamikazeHeading(Object* obj, void* userData)
{
	KamikazeHeadings* headings = (KamikazeHeadings*)userData;
	// the template itself: isEquivalentTo would take the jet a drone is a reskin of as well
	if (obj == headings->self || obj->isEffectivelyDead() ||
			obj->getTemplate()->getFinalOverride() != headings->self->getTemplate()->getFinalOverride())
		return;
	headings->targets.push_back(obj->getAIUpdateInterface()->getJetAIUpdate()->friend_getKamikazeTargetID());
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime JetAIUpdate::update()
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();

	getProducerLocation();

	Object* jet = getObject();

	ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID());
	if (pp != NULL)
		setFlag(HAD_AIRFIELD, true);

	// a kamikaze whose target is gone takes the dearest enemy left in the area it was sent to that no
	// other drone of its swarm is heading for, the dearest of all when every one has a drone already, or
	// dives into the area, rather than flying home with its one shot (notifyVictimIsDead) to land and wait
	if (d->m_kamikazeRange > 0.0f && getFlag(ALLOW_AIR_LOCO) && !getFlag(TAKEOFF_IN_PROGRESS) &&
			!getFlag(LANDING_IN_PROGRESS) && !getFlag(HAS_PENDING_COMMAND) && AIUpdateInterface::isIdle())
	{
		std::vector<Object*> found;
		Weapon_findMostValuableEnemies(jet, &m_kamikazeArea, d->m_kamikazeRange, KAMIKAZE_RETARGET_CANDIDATES, found);
		KamikazeHeadings headings;
		headings.self = jet;
		jet->getControllingPlayer()->iterateObjects(collectKamikazeHeading, &headings);
		Object* pick = found.empty() ? NULL : found.front();
		for (std::vector<Object*>::const_iterator it = found.begin(); it != found.end(); ++it)
		{
			if (std::find(headings.targets.begin(), headings.targets.end(), (*it)->getID()) == headings.targets.end())
			{
				pick = *it;
				break;
			}
		}
		DEBUG_LOG(("KAMIKAZE frame %d '%s' %d lost its target at (%.0f,%.0f), %s %d\n", TheGameLogic->getFrame(), jet->getTemplate()->getName().str(),
			jet->getID(), jet->getPosition()->x, jet->getPosition()->y, pick ? pick->getTemplate()->getName().str() : "dives into the area",
			pick ? (Int)pick->getID() : 0));
		// a script's order, as the scramble gave (ParkingPlaceBehavior::launchScramble), so it chases
		if (pick)
			aiAttackObject(pick, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
		else
			aiAttackPosition(&m_kamikazeArea, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
	}

	// If idle & out of ammo, return
	// have to call our parent's isIdle, because we override it to never return true
	// when we have a pending command...
	UnsignedInt now = TheGameLogic->getFrame();

	// srj sez: not 100% sure on this. calling RELOAD_AMMO "idle" allows us to get healed while reloading,
	// but might have other side effects we didn't want. if this does prove to cause a bug, be sure
	// that jets (and ESPECIALLY comanches) are still getting healed at airfields.
	if (AIUpdateInterface::isIdle() || getStateMachine()->getCurrentStateID() == RELOAD_AMMO)
	{
		if (pp != NULL)
		{
			if (!getFlag(ALLOW_AIR_LOCO) && 
					!getFlag(HAS_PENDING_COMMAND) &&
						jet->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) &&
						jet->getBodyModule()->getHealth() == jet->getBodyModule()->getMaxHealth())
			{
				// we're completely healed, so take off again
				pp->setHealee(jet, false);
				friend_setAllowAirLoco(true);
				getStateMachine()->clear();
				setLastCommandSource( CMD_FROM_AI );
				getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );

				// and then go where the helipad's rally point says
				Object *airfield = TheGameLogic->findObjectByID( jet->getProducerID() );
				if (airfield)
				{
					ExitInterface *exitInterface = airfield->getObjectExitInterface();
					if (exitInterface)
					{
						const Coord3D *rallyPoint = exitInterface->getRallyPoint();
						if (rallyPoint)
							aiMoveToPosition( rallyPoint, CMD_FROM_AI );
					}
				}
			}
			else
			{
				pp->setHealee(jet, !getFlag(ALLOW_AIR_LOCO));
			}
		}

		// note that we might still have weapons with ammo, but still be forced to return to reload.
		if (isOutOfSpecialReloadAmmo() && getFlag(ALLOW_AIR_LOCO))
		{
			m_returnToBaseFrame = 0;

			// this is really a "just-in-case" to ensure the targeter list doesn't spin out of control (srj)
			pruneDeadTargeters();

			setFlag(USE_SPECIAL_RETURN_LOCO, true);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
		else if (getFlag(HAS_PENDING_COMMAND) 
			// srj sez: if we are reloading ammo, wait will we are done before processing the pending command.
			&& getStateMachine()->getCurrentStateID() != RELOAD_AMMO)
		{
			m_returnToBaseFrame = 0;

			AICommandParms parms(AICMD_MOVE_TO_POSITION, CMD_FROM_AI);	// values don't matter, will be wiped by next line
			m_mostRecentCommand.reconstitute(parms);
			setFlag(HAS_PENDING_COMMAND, false);


 			aiDoCommand(&parms);
		}
		else if (m_returnToBaseFrame != 0 && now >= m_returnToBaseFrame && getFlag(ALLOW_AIR_LOCO))
		{
			m_returnToBaseFrame = 0;
			DEBUG_ASSERTCRASH(isOutOfSpecialReloadAmmo() == false, ("Hmm, this seems unlikely -- isOutOfSpecialReloadAmmo()==false"));
			setFlag(USE_SPECIAL_RETURN_LOCO, false);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
		else if (m_returnToBaseFrame  == 0 && d->m_returnToBaseIdleTime > 0 && getFlag(ALLOW_AIR_LOCO))
		{
			m_returnToBaseFrame = now + d->m_returnToBaseIdleTime;
		}
	}
	else
	{
		if (pp != NULL)
		{
			pp->setHealee(getObject(), false);
		}
		m_returnToBaseFrame = 0;
		if (getFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD) && 
						isOutOfSpecialReloadAmmo() && getFlag(ALLOW_AIR_LOCO))
		{

			setFlag(USE_SPECIAL_RETURN_LOCO, true);
			setFlag(HAS_PENDING_COMMAND, true);
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			setLastCommandSource( CMD_FROM_AI );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
		}
	}

	Real minHeight = friend_getMinHeight();
	if( pp )
	{
		minHeight += pp->getLandingDeckHeightOffset();
	}

	Drawable* draw = jet->getDrawable();
	if (draw != NULL)
	{
		StateID id = getStateMachine()->getCurrentStateID();
		Bool needToCheckMinHeight = (id >= JETAISTATETYPE_FIRST && id <= JETAISTATETYPE_LAST) || 
																	!jet->isAboveTerrain() ||
																	!getFlag(ALLOW_AIR_LOCO);
		if( needToCheckMinHeight || jet->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
		{
			Real ht = jet->isAboveTerrain() ? jet->getHeightAboveTerrain() : 0;
			if (ht < minHeight)
			{
				Matrix3D tmp(1);
				tmp.Set_Z_Translation(minHeight - ht);
				draw->setInstanceMatrix(&tmp);
			}
			else
			{
				draw->setInstanceMatrix(NULL);
			}
		}
		else
		{
			draw->setInstanceMatrix(NULL);
		}
	}

	PhysicsBehavior* physics = jet->getPhysics();
	if (physics->getVelocityMagnitude() > 0 && getFlag(ALLOW_AIR_LOCO))
		jet->setModelConditionState(MODELCONDITION_JETEXHAUST);
	else
		jet->clearModelConditionState(MODELCONDITION_JETEXHAUST);

	if (jet->testStatus(OBJECT_STATUS_IS_ATTACKING))
	{
		m_attackLocoExpireFrame = now + d->m_attackLocoPersistTime;
		m_attackersMissExpireFrame = now + d->m_attackersMissPersistTime;
	}
	else
	{
		if (m_attackLocoExpireFrame != 0 && now >= m_attackLocoExpireFrame)
		{
			m_attackLocoExpireFrame = 0;
			// the timer ran out and nothing put the normal locomotor back, so an Aurora that had
			// gone supersonic to attack stayed supersonic for the rest of its life
			chooseLocomotorSet(LOCOMOTORSET_NORMAL);
		}
		if (m_attackersMissExpireFrame != 0 && now >= m_attackersMissExpireFrame)
		{
			m_attackersMissExpireFrame = 0;
		}
	}

	if (m_untargetableExpireFrame != 0 && now >= m_untargetableExpireFrame)
	{
		m_untargetableExpireFrame = 0;
	}

	positionLockon();
	
	if (m_attackLocoExpireFrame != 0)
	{
		chooseLocomotorSet(d->m_attackingLoco);
	}
	else if (getFlag(USE_SPECIAL_RETURN_LOCO))
	{
		chooseLocomotorSet(d->m_returningLoco);
	}

	
	if( !jet->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
	{
		Drawable *draw = jet->getDrawable();
		if( draw )
		{
			if( getFlag(TAKEOFF_IN_PROGRESS) 
					|| getFlag(LANDING_IN_PROGRESS) 
					|| getObject()->isSignificantlyAboveTerrain() 
					|| isMoving() 
					|| isWaitingForPath() )
			{
				if( !m_enginesOn )
				{
					//We just started moving, therefore turn on the engines!			
					draw->enableAmbientSound( TRUE );
					m_enginesOn = TRUE;
				}
			}
			else if( m_enginesOn )
			{
				//We're no longer moving, so turn off the engines!
				draw->enableAmbientSound( FALSE );
				m_enginesOn = FALSE;
			}
		}
	}


	/*UpdateSleepTime ret =*/ AIUpdateInterface::update();

	// a kamikaze's shot is the jet itself: the projectile it just let go flies on from where it was
	if (d->m_kamikazeRange > 0.0f && getFlag(ALLOW_AIR_LOCO) && isOutOfSpecialReloadAmmo())
	{
		DEBUG_LOG(("KAMIKAZE frame %d '%s' %d dives from (%.0f,%.0f) at %.0f above the ground\n", TheGameLogic->getFrame(),
			jet->getTemplate()->getName().str(), jet->getID(), jet->getPosition()->x, jet->getPosition()->y, jet->getHeightAboveTerrain()));
		TheGameLogic->destroyObject(jet);
	}

	//return (mine < ret) ? mine : ret;
	/// @todo srj -- someday, make sleepy. for now, must not sleep.
	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::chooseLocomotorSet(LocomotorSetType wst)
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	if (!getFlag(ALLOW_AIR_LOCO))
	{
		wst = LOCOMOTORSET_TAXIING;
	}
	else if (m_attackLocoExpireFrame != 0)
	{
		wst = d->m_attackingLoco;
	}
	else if (getFlag(USE_SPECIAL_RETURN_LOCO))
	{
		wst = d->m_returningLoco;
	}
	return AIUpdateInterface::chooseLocomotorSet(wst);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::setLocomotorGoalNone()
{
	if ((getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
			&& getFlag(ALLOW_AIR_LOCO) && !getFlag(ALLOW_CIRCLING))
	{
		Object* jet = getObject();
		Coord3D desiredPos = *jet->getPosition();
		const Coord3D* dir = jet->getUnitDirectionVector2D();
		desiredPos.x += dir->x * 1000.0f;
		desiredPos.y += dir->y * 1000.0f;
		setLocomotorGoalPositionExplicit(desiredPos);
	}
	else
	{
		AIUpdateInterface::setLocomotorGoalNone();
	}
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::getSneakyTargetingOffset(Coord3D* offset) const
{
	if (m_attackersMissExpireFrame != 0 && TheGameLogic->getFrame() < m_attackersMissExpireFrame)
	{
		if (offset)
		{
			const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
			const Object* jet = getObject();
			const Coord3D* dir = jet->getUnitDirectionVector2D();
			offset->x = dir->x * d->m_sneakyOffsetWhenAttacking;
			offset->y = dir->y * d->m_sneakyOffsetWhenAttacking;
			offset->z = 0.0f;
		}
		return true;
	}
	else
	{
		return false;
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::pruneDeadTargeters()
{
	if (!m_targetedBy.empty())
	{
		for (std::list<ObjectID>::iterator it = m_targetedBy.begin(); it != m_targetedBy.end(); /* empty */ ) 
		{
			if (TheGameLogic->findObjectByID(*it) == NULL)
			{
				it = m_targetedBy.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::positionLockon()
{
	if (!m_lockonDrawable)
		return;

	if (m_untargetableExpireFrame == 0)
	{
		TheGameClient->destroyDrawable(m_lockonDrawable);
		m_lockonDrawable = NULL;
		return;
	}

	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt remaining = m_untargetableExpireFrame - now;
	UnsignedInt elapsed = d->m_lockonTime - remaining;

	Coord3D pos = *getObject()->getPosition();
	Real frac = (Real)remaining / (Real)d->m_lockonTime;
	Real finalDist = getObject()->getGeometryInfo().getBoundingCircleRadius();
	Real dist = finalDist + (d->m_lockonInitialDist - finalDist) * frac;
	Real angle = d->m_lockonAngleSpin * frac;

	pos.x += Cos(angle) * dist;
	pos.y += Sin(angle) * dist;
	// pos.z is untouched

	m_lockonDrawable->setPosition(&pos);
	Real dx = getObject()->getPosition()->x - pos.x;
	Real dy = getObject()->getPosition()->y - pos.y;
	if (dx || dy)
		m_lockonDrawable->setOrientation(ATan2(dy, dx));

	// the Gaussian sum, to avoid keeping a running total:
	//
	//		1+2+3+...n = n*(n+1)/2
	//
	Real elapsedTimeSumPrev = 0.5f * (elapsed-1) * (elapsed);
	Real elapsedTimeSumCurr = elapsedTimeSumPrev + elapsed;
	Real factor = d->m_lockonFreq / d->m_lockonTime;
	Bool lastPhase = ((Int)(factor * elapsedTimeSumPrev) & 1) != 0;
	Bool thisPhase = ((Int)(factor * elapsedTimeSumCurr) & 1) != 0;

	if (lastPhase && (!thisPhase)) 
	{
		AudioEventRTS lockonSound = TheAudio->getMiscAudio()->m_lockonTickSound;
		lockonSound.setObjectID(getObject()->getID());
		TheAudio->addAudioEvent(&lockonSound);
		if (d->m_lockonBlinky)
			m_lockonDrawable->setDrawableHidden(false);
	}
	else
	{
		if (d->m_lockonBlinky)
			m_lockonDrawable->setDrawableHidden(true);
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::buildLockonDrawableIfNecessary()
{
	if (m_untargetableExpireFrame == 0)
		return;

	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	if (d->m_lockonCursor.isNotEmpty() && m_lockonDrawable == NULL)
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(d->m_lockonCursor);
		if (tt)
		{
			m_lockonDrawable = TheThingFactory->newDrawable(tt);
		}
	}
	positionLockon();
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::addTargeter(ObjectID id, Bool add)
{
	const JetAIUpdateModuleData* d = getJetAIUpdateModuleData();
	UnsignedInt lockonTime = d->m_lockonTime;
	if (lockonTime != 0)
	{
		std::list<ObjectID>::iterator it = std::find(m_targetedBy.begin(), m_targetedBy.end(), id);
		if (add)
		{
			if (it == m_targetedBy.end())
			{
				m_targetedBy.push_back(id); 
				if (m_untargetableExpireFrame == 0 && m_targetedBy.size() == 1)
				{
					m_untargetableExpireFrame = TheGameLogic->getFrame() + lockonTime;
					buildLockonDrawableIfNecessary();
				}
			}
		}
		else
		{
			if (it != m_targetedBy.end())
			{
				m_targetedBy.erase(it); 
				if (m_targetedBy.empty())
				{
					m_untargetableExpireFrame = 0;
				}
			}
		}
	}
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::isTemporarilyPreventingAimSuccess() const
{
	return m_untargetableExpireFrame != 0 && (TheGameLogic->getFrame() < m_untargetableExpireFrame);
}

//----------------------------------------------------------------------------------------
Bool JetAIUpdate::isAllowedToMoveAwayFromUnit() const
{
	// parked (or landing) units don't get to do this.
	if (!getFlag(ALLOW_AIR_LOCO) || getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
		return false;

	return AIUpdateInterface::isAllowedToMoveAwayFromUnit();
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isDoingGroundMovement(void) const
{
	// srj per jba: Air units should never be doing ground movement, even when taxiing...
	// (exception: see getTreatAsAircraftForLocoDistToGoal)
	return false;
}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::getTreatAsAircraftForLocoDistToGoal() const
{
	// exception to isDoingGroundMovement: should never treat as aircraft for dist-to-goal when taxiing.
	if (getFlag(TAXI_IN_PROGRESS))
	{
		return false;
	}
	else
	{
		return AIUpdateInterface::getTreatAsAircraftForLocoDistToGoal();
	}
}

//----------------------------------------------------------------------------------------
/** A scrambled jet skips the taxi, the wait for the runway and the pause on it: the order it was
	* given stays pending, as on any jet on the ground, and is flown once it is airborne. */
void JetAIUpdate::friend_takeOffNow()
{
	getStateMachine()->clear();
	getStateMachine()->setState( TAKING_OFF );
	friend_enableAfterburners( true );
}

//----------------------------------------------------------------------------------------
/**
 * Follow the path defined by the given array of points
 */
void JetAIUpdate::privateFollowPath( std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource, Bool exitProduction )
{
	if (exitProduction)
	{
		getStateMachine()->clear();
		if( ignoreObject )
			ignoreObstacle( ignoreObject );
		setLastCommandSource( cmdSource );
		if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
			getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );
		else
			getStateMachine()->setState( TAXI_FROM_HANGAR );
	}
	else
	{
		AIUpdateInterface::privateFollowPath(path, ignoreObject, cmdSource, exitProduction);
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::privateFollowPathAppend( const Coord3D *pos, CommandSourceType cmdSource )
{
	// nothing yet... might need to override. not sure. (srj)
	AIUpdateInterface::privateFollowPathAppend(pos, cmdSource);
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::doLandingCommand(Object *airfield, CommandSourceType cmdSource)
{
	if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
	{
		m_landingPosForHelipadStuff = *airfield->getPosition();

		Coord3D tmp;
		FindPositionOptions options;
		options.maxRadius = airfield->getGeometryInfo().getBoundingCircleRadius() * 10.0f;
		if (ThePartitionManager->findPositionAround(&m_landingPosForHelipadStuff, &options, &tmp))
			m_landingPosForHelipadStuff = tmp;
	}

	for (BehaviorModule** i = airfield->getBehaviorModules(); *i; ++i)
	{
		ParkingPlaceBehaviorInterface* pp = (*i)->getParkingPlaceBehaviorInterface();
		if (pp == NULL)
			continue;
		
		if (getObject()->isKindOf(KINDOF_PRODUCED_AT_HELIPAD) ||
				pp->reserveSpace(getObject()->getID(), friend_getParkingOffset(), NULL))
		{
			// if we had a space at another airfield, release it
			ParkingPlaceBehaviorInterface* oldPP = getPP(getObject()->getProducerID());
			if (oldPP != NULL && oldPP != pp)
			{
				oldPP->releaseSpace(getObject()->getID());
			}

			getObject()->setProducer(airfield);
			DEBUG_ASSERTCRASH(isOutOfSpecialReloadAmmo() == false, ("Hmm, this seems unlikely -- isOutOfSpecialReloadAmmo()==false"));
			setFlag(USE_SPECIAL_RETURN_LOCO, false);
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			setLastCommandSource( cmdSource );
			getStateMachine()->setState(RETURNING_FOR_LANDING);
			return;
		}
	}
}

//----------------------------------------------------------------------------------------
void JetAIUpdate::notifyVictimIsDead()
{
	if (getJetAIUpdateModuleData()->m_needsRunway)
		m_returnToBaseFrame = TheGameLogic->getFrame();
}

//----------------------------------------------------------------------------------------
/**
 * Enter the given object
 */
void JetAIUpdate::privateEnter( Object *objectToEnter, CommandSourceType cmdSource )
{
	// we are already landing. just ignore it.
	if (getFlag(LANDING_IN_PROGRESS))
		return;

	if( !TheActionManager->canEnterObject( getObject(), objectToEnter, cmdSource, DONT_CHECK_CAPACITY ) )
		return;

	doLandingCommand(objectToEnter, cmdSource);
}

//----------------------------------------------------------------------------------------
/**
 * Get repaired at the repair depot
 */
void JetAIUpdate::privateGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
{
	// we are already landing. just ignore it.
	if (getFlag(LANDING_IN_PROGRESS))
		return;

	// sanity, if we can't get repaired from here get out of here
	if( TheActionManager->canGetRepairedAt( getObject(), repairDepot, cmdSource ) == FALSE )
		return;

	// dock with the repair depot
	doLandingCommand( repairDepot, cmdSource );

}

//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::isParkedAt(const Object* obj) const
{
	// Helipad units count too: a Comanche on the pad has that airfield as its producer, and one told
	// to repair at the pad it was already repairing on took off and flew a circuit back to it.
	if (!getFlag(ALLOW_AIR_LOCO) &&
			obj != NULL)
	{
		Object* airfield;
		ParkingPlaceBehaviorInterface* pp = getPP(getObject()->getProducerID(), &airfield);
		if (pp != NULL && airfield != NULL && airfield == obj)
		{
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	// call this from aiDoCommand as well as update, because this can
	// be called before update ever is... if the unit is placed on a map,
	// and a script tells it to do something with a condition of TRUE!
	getProducerLocation();

	if (!isAllowedToRespondToAiCommands(parms))
		return;

	// note that we always store this, even if nothing will be "pending".
	m_mostRecentCommand.store(*parms);

	if (getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS))
	{
		// have to wait for takeoff or landing to complete, just store the sucker
		setFlag(HAS_PENDING_COMMAND, true);
		return;
	}
	else if (parms->m_cmd == AICMD_IDLE && getStateMachine()->getCurrentStateID() == RELOAD_AMMO)
	{
		// uber-special-case... if we are told to idle, but are reloading ammo, ignore it for now,
		// since we're already doing "nothing" and responding to this will cease our reload...
		// don't just return, tho, in case we were (say) reloading during a guard stint.
		setFlag(HAS_PENDING_COMMAND, true);
		return;
	}
	else if( parms->m_cmd == AICMD_IDLE && getObject()->isAirborneTarget() && !getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
	{
		getStateMachine()->clear();
		setLastCommandSource( CMD_FROM_AI );
		getStateMachine()->setState( RETURNING_FOR_LANDING );
		return;
	}
	else if (!getFlag(ALLOW_AIR_LOCO))
	{
		switch (parms->m_cmd)
		{
			case AICMD_IDLE:
			case AICMD_BUSY:
			case AICMD_FOLLOW_EXITPRODUCTION_PATH:
				// don't need (or want) to take off for these
				break;

			/* A jet parked with no ammo used to take off the moment it was given an attack order, fly
				 to the target, find it had nothing to shoot with and fly home again.  Hold the order
				 instead: it is carried out when the rearming finishes, which is what the player meant. */
			case AICMD_ATTACKMOVE_TO_POSITION:
			case AICMD_ATTACK_AREA:
			case AICMD_ATTACK_OBJECT:
			case AICMD_ATTACK_POSITION:
			case AICMD_ATTACK_TEAM:
			case AICMD_FORCE_ATTACK_OBJECT:
			case AICMD_GUARD_AREA:
			case AICMD_GUARD_OBJECT:
			case AICMD_GUARD_POSITION:
			case AICMD_GUARD_RETALIATE:
			case AICMD_HUNT:
				if (isOutOfSpecialReloadAmmo())
				{
					setFlag(HAS_PENDING_COMMAND, true);
					return;
				}
				// falls through

			case AICMD_ENTER:
			case AICMD_GET_REPAIRED:

				// if we're already parked at the airfield in question, just ignore.
				if (isParkedAt(parms->m_obj))
					return;
			
				// else fall thru to the default case!

			default:
			{
				// nuke any existing pending cmd
				m_mostRecentCommand.store(*parms);
				setFlag(HAS_PENDING_COMMAND, true);

				getStateMachine()->clear();
				setLastCommandSource( CMD_FROM_AI );
				getStateMachine()->setState( TAKING_OFF_AWAIT_CLEARANCE );
				
				return;
			}
		}
	}

	switch (parms->m_cmd)
	{
		case AICMD_GUARD_POSITION:
		case AICMD_GUARD_OBJECT:
		case AICMD_GUARD_AREA:
		case AICMD_HUNT:
		case AICMD_GUARD_RETALIATE:
		// like hunt, an attack-move is a standing order: if we run dry partway through it,
		// go reload and then pick the order back up, rather than dropping it and idling.
		case AICMD_ATTACKMOVE_TO_POSITION:
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, true);
			break;
		default:
			setFlag(ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD, false);
			break;
	}

	setFlag(HAS_PENDING_COMMAND, false);
	AIUpdateInterface::aiDoCommand(parms);
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_setAllowAirLoco(Bool allowAirLoco) 
{ 
	setFlag(ALLOW_AIR_LOCO, allowAirLoco); 
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_enableAfterburners(Bool v)
{
	Object* jet = getObject();
	if (v)
	{
		jet->setModelConditionState(MODELCONDITION_JETAFTERBURNER);
		if (!m_afterburnerSound.isCurrentlyPlaying())
		{
			m_afterburnerSound.setObjectID(jet->getID());
			m_afterburnerSound.setPlayingHandle(TheAudio->addAudioEvent(&m_afterburnerSound));
		}
	}
	else
	{
		jet->clearModelConditionState(MODELCONDITION_JETAFTERBURNER);
		if (m_afterburnerSound.isCurrentlyPlaying())
		{
			TheAudio->removeAudioEvent(m_afterburnerSound.getPlayingHandle());
		}
	}
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_addWaypointToGoalPath( const Coord3D &bestPos )
{
	privateFollowPathAppend( &bestPos, CMD_FROM_AI );
}

//-------------------------------------------------------------------------------------------------
AICommandType JetAIUpdate::friend_getPendingCommandType() const 
{ 
	if( getFlag( HAS_PENDING_COMMAND ) )
	{
		return m_mostRecentCommand.getCommandType();
	}
	return AICMD_NO_COMMAND;
}

//-------------------------------------------------------------------------------------------------
/** The order this aircraft accepted but has not started yet.  Taking off clears the state machine
	* and parks the command here, so between the player's click and the wheels leaving the ground the
	* order exists nowhere else. */
//-------------------------------------------------------------------------------------------------
AICommandType JetAIUpdate::friend_getHeldOrder( ObjectID& targetID, Coord3D& targetPos ) const
{
	if( !getFlag( HAS_PENDING_COMMAND ) )
		return AICMD_NO_COMMAND;

	targetID = m_mostRecentCommand.getTargetObjectID();
	targetPos = m_mostRecentCommand.getTargetPosition();
	return m_mostRecentCommand.getCommandType();
}

//-------------------------------------------------------------------------------------------------
/** On its way home, on the ground, or filling its racks: everything between running dry and being
	* ready to fly out again.  An aircraft in here is not refusing the order it was given, it is in the
	* middle of the trip that lets it carry the order out. */
//-------------------------------------------------------------------------------------------------
Bool JetAIUpdate::friend_isRearming() const
{
	switch( getStateMachine()->getCurrentStateID() )
	{
		case RETURNING_FOR_LANDING:
		case RETURN_TO_DEAD_AIRFIELD:
		case CIRCLING_DEAD_AIRFIELD:
		case LANDING_AWAIT_CLEARANCE:
		case LANDING:
		case TAXI_FROM_LANDING:
		case ORIENT_FOR_PARKING_PLACE:
		case RELOAD_AMMO:
			return TRUE;
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void JetAIUpdate::friend_purgePendingCommand()
{
	setFlag(HAS_PENDING_COMMAND, false);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Save engine on/off state.
	* 3: TheSuperHackers @bugfix Save the helipad landing position.
	* 4: the most recent command keeps its source
	* 5: a kamikaze's area */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::xfer( Xfer *xfer )
{

  // version
  XferVersion currentVersion = 5;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );
 
 // extend base class
	AIUpdateInterface::xfer(xfer);


	xfer->xferCoord3D(&m_producerLocation);
	m_mostRecentCommand.doXfer(xfer);
	if (version < 4)
		m_mostRecentCommand.setCommandSource(CMD_FROM_AI);
	xfer->xferUnsignedInt(&m_attackLocoExpireFrame);
	xfer->xferUnsignedInt(&m_attackersMissExpireFrame);
	xfer->xferUnsignedInt(&m_returnToBaseFrame);
	xfer->xferSTLObjectIDList(&m_targetedBy);

	xfer->xferUnsignedInt(&m_untargetableExpireFrame);

	// Set on create.
	//AudioEventRTS						m_afterburnerSound;		///< Sound when afterburners on

	AsciiString drawName;
	if (m_lockonDrawable) {
		drawName = m_lockonDrawable->getTemplate()->getName();
	}
	xfer->xferAsciiString(&drawName);
	if (drawName.isNotEmpty() && m_lockonDrawable==NULL) 
	{
		const ThingTemplate* tt = TheThingFactory->findTemplate(drawName);
		if (tt)
		{
			m_lockonDrawable = TheThingFactory->newDrawable(tt);
		}
	}
	xfer->xferInt(&m_flags);

	if( version >= 2 )
	{
		xfer->xferBool( &m_enginesOn );
	}
	else
	{
		//We don't have to be accurate -- this is a patch.
		if( getFlag(TAKEOFF_IN_PROGRESS) || getFlag(LANDING_IN_PROGRESS) || getObject()->isSignificantlyAboveTerrain() || getObject()->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
		{
			m_enginesOn = TRUE;
		}
		else
		{
			m_enginesOn = FALSE;
		}
	}

	if( version >= 3 )
	{
		xfer->xferCoord3D( &m_landingPosForHelipadStuff );
	}

	if( version >= 5 )
	{
		xfer->xferCoord3D( &m_kamikazeArea );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JetAIUpdate::loadPostProcess( void )
{
	//When drawables are created, so are their ambient sounds. After loading, only turn off the
	//ambient sound if the engine is off.
	if( !m_enginesOn )
	{
		Drawable *draw = getObject()->getDrawable();
		if( draw )
		{
			draw->stopAmbientSound();
		}
	}

	// extend base class
	AIUpdateInterface::loadPostProcess();
}  // end loadPostProcess
