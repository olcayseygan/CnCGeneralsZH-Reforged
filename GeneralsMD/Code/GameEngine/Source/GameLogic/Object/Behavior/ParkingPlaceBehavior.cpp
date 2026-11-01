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

// FILE: ParkingPlaceBehavior.cpp ///////////////////////////////////////////////////////////////////////
// Author:	Steven Johnson, June 2002
// Desc:  
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/CRCDebug.h"
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ParkingPlaceBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "Common/Team.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ParkingPlaceBehavior::ParkingPlaceBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_gotInfo = false;
	
	//Added By Sadullah Nader
	//Initializations 
	
	m_heliRallyPoint.zero();
	
	//

	m_heliRallyPointExists = FALSE;
	m_nextScrambleFrame = 0;
	m_nextHealFrame = FOREVER;
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ParkingPlaceBehavior::~ParkingPlaceBehavior( void )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::buildInfo()
{
	if (m_gotInfo)
		return;

	if (getObject()->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) ||
			getObject()->testStatus(OBJECT_STATUS_SOLD))
		return;

	const ParkingPlaceBehaviorModuleData* d = getParkingPlaceBehaviorModuleData();
	ProductionUpdateInterface* pu = getObject()->getProductionUpdateInterface();

	m_spaces.reserve(d->m_numRows * d->m_numCols);

	Int door = 0;
	{
		ParkingPlaceInfo info;
		for (Int row = 0; row < d->m_numRows; ++row)
		{
			for (Int col = 0; col < d->m_numCols; ++col)
			{
				AsciiString tmp;
				Matrix3D mtx;
				
				tmp.format("Runway%dPark%dHan",col+1,row+1);
				getObject()->getSingleLogicalBonePosition(tmp.str(), &info.m_hangarStart, &mtx);
				info.m_hangarStartOrient = mtx.Get_Z_Rotation();

				tmp.format("Runway%dParking%d",col+1,row+1);
				getObject()->getSingleLogicalBonePosition(tmp.str(), &info.m_location, &mtx);
				info.m_orientation = mtx.Get_Z_Rotation();

				tmp.format("Runway%dPrep%d",col+1,row+1);
				getObject()->getSingleLogicalBonePosition(tmp.str(), &info.m_prep, NULL);

				info.m_runway = col;
				info.m_door = (ExitDoorType)door++;
				info.m_objectInSpace = INVALID_ID;
				info.m_reservedForExit = false;
				if (pu)
					pu->setHoldDoorOpen(info.m_door, false);

				m_spaces.push_back(info);
			}
		}
	}

	if (d->m_hasRunways)
	{
		RunwayInfo info;
		m_runways.reserve(d->m_numCols);
		for (Int col = 0; col < d->m_numCols; ++col)
		{
			AsciiString tmp;

			tmp.format("RunwayStart%d",col+1);
			getObject()->getSingleLogicalBonePosition(tmp.str(), &info.m_start, NULL);

			tmp.format("RunwayEnd%d",col+1);
			getObject()->getSingleLogicalBonePosition(tmp.str(), &info.m_end, NULL);
			
			info.m_inUseBy = INVALID_ID;
			info.m_nextInLineForTakeoff = INVALID_ID;
			info.m_wasInLine = false;
			m_runways.push_back(info);
		}
	}

	m_gotInfo = true;
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::purgeDead()
{
	buildInfo();

	ProductionUpdateInterface* pu = getObject()->getProductionUpdateInterface();
	{
		for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
		{
			if (it->m_objectInSpace != INVALID_ID)
			{
				Object* obj = TheGameLogic->findObjectByID(it->m_objectInSpace);
				if (obj == NULL || obj->isEffectivelyDead())
				{
					it->m_objectInSpace = INVALID_ID;
					it->m_reservedForExit = false;
					if (pu)
						pu->setHoldDoorOpen(it->m_door, false);
				}
			}
		}
	}

	{
		for (std::vector<RunwayInfo>::iterator it = m_runways.begin(); it != m_runways.end(); ++it)
		{
			if (it->m_inUseBy != INVALID_ID)
			{
				Object* obj = TheGameLogic->findObjectByID(it->m_inUseBy);
				if (obj == NULL || obj->isEffectivelyDead())
				{
					it->m_inUseBy = INVALID_ID;
					it->m_wasInLine = false;
				}
			}
			if (it->m_nextInLineForTakeoff != INVALID_ID)
			{
				Object* obj = TheGameLogic->findObjectByID(it->m_nextInLineForTakeoff);
				if (obj == NULL || obj->isEffectivelyDead())
				{
					it->m_nextInLineForTakeoff = INVALID_ID;
				}
			}
		}
	}

	{
		Bool anythingPurged = false;
		for (std::list<HealingInfo>::iterator it = m_healing.begin(); it != m_healing.end(); /*++it*/)
		{
			if (it->m_gettingHealedID != INVALID_ID)
			{
				Object* objToHeal = TheGameLogic->findObjectByID(it->m_gettingHealedID);
				if (objToHeal == NULL || objToHeal->isEffectivelyDead())
				{
					it = m_healing.erase(it);
					anythingPurged = true;
				}
				else
				{
					++it;
				}
			}
		}
		if (anythingPurged)
			resetWakeFrame();
	}	
}

//-------------------------------------------------------------------------------------------------
// note: called from client, so MUST NOT modify self in any way, or desyncs will occur
Bool ParkingPlaceBehavior::hasReservedSpace(ObjectID id) const
{
	if (!m_gotInfo)
		return false;

	if (id == INVALID_ID)	// shouldn't call this way, but Weapon mistakenly does sometimes, so check for it
		return false;

	for (std::vector<ParkingPlaceInfo>::const_iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
			return true;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
/* What reserveSpace would answer, without taking the space: the airfield search asked by reserving,
	 so every airfield it tried on the way to the nearest kept a space for that jet until it died.
	 A dead occupant counts as gone, as purgeDead would make it. Helicopters need a space here too
	 (they park on it), so there is no helipad shortcut as in hasAvailableSpaceFor. */
Bool ParkingPlaceBehavior::canReserveSpace(ObjectID id) const
{
	if (!m_gotInfo || id == INVALID_ID)
		return false;

	for (std::vector<ParkingPlaceInfo>::const_iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
			return true;
	}

	for (std::vector<ParkingPlaceInfo>::const_iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace != INVALID_ID)
		{
			Object* obj = TheGameLogic->findObjectByID(it->m_objectInSpace);
			if (obj == NULL || obj->isEffectivelyDead())
				return true;
		}
		else if (!it->m_reservedForExit)
		{
			return true;
		}
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
Int ParkingPlaceBehavior::getSpaceIndex( ObjectID id ) const
{
	if( id == INVALID_ID )
	{
		return -1;
	}

	Int index = 0;
	for( std::vector<ParkingPlaceInfo>::const_iterator it = m_spaces.begin(); it != m_spaces.end(); it++, index++ )
	{
		if (it->m_objectInSpace == id)
		{
			return index;
		}
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
ParkingPlaceBehavior::ParkingPlaceInfo* ParkingPlaceBehavior::findPPI(ObjectID id)
{
	DEBUG_ASSERTCRASH(id != INVALID_ID, ("call findEmptyPPI instead"));

	if (!m_gotInfo || id == INVALID_ID)
		return NULL;

	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
			return &(*it);
	}

	return NULL;
}

//-------------------------------------------------------------------------------------------------
ParkingPlaceBehavior::ParkingPlaceInfo* ParkingPlaceBehavior::findEmptyPPI()
{
	if (!m_gotInfo)
		return NULL;

	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == INVALID_ID && it->m_reservedForExit == false)
			return &(*it);
	}

	return NULL;
}

//-------------------------------------------------------------------------------------------------
// note: called from client, so MUST NOT modify self in any way, or desyncs will occur
Bool ParkingPlaceBehavior::shouldReserveDoorWhenQueued(const ThingTemplate* thing) const
{
	if (thing->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		return false;

	return true;
}

//-------------------------------------------------------------------------------------------------
// note: called from client, so MUST NOT modify self in any way, or desyncs will occur
Bool ParkingPlaceBehavior::hasAvailableSpaceFor(const ThingTemplate* thing) const
{
	if (!m_gotInfo)	// degenerate case, shouldn't happen, but just in case...
		return false;
	
	if (thing->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
		return true;

	for (std::vector<ParkingPlaceInfo>::const_iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		ObjectID id = it->m_objectInSpace;

		// since this is const, and we can't purge the dead safely, just peek and see if we have a dead thing.
		if (id != INVALID_ID)
		{
			Object* obj = TheGameLogic->findObjectByID(id);
			if (obj == NULL || obj->isEffectivelyDead())
			{
				id = INVALID_ID;
			}
		}

		if (id == INVALID_ID && it->m_reservedForExit == false)
		{
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool ParkingPlaceBehavior::reserveSpace(ObjectID id, Real parkingOffset, ParkingPlaceBehaviorInterface::PPInfo* info)
{
	buildInfo();
	purgeDead();

	const ParkingPlaceBehaviorModuleData* d = getParkingPlaceBehaviorModuleData();

	ParkingPlaceInfo* ppi = findPPI(id);
	if (ppi == NULL)
	{
		ppi = findEmptyPPI();
		if (ppi == NULL)
		{
			DEBUG_CRASH(("No parking places!"));
			return false;	// nothing available
		}
	}

	ppi->m_objectInSpace = id;
	ppi->m_reservedForExit = false;
	
	if( d->m_landingDeckHeightOffset )
	{
		Object *obj = TheGameLogic->findObjectByID( id );
		if( obj )
		{
			obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) );
		}
	}

	if (info) 
	{
		calcPPInfo( id, info );
		if (parkingOffset != 0.0f)
		{
			info->parkingSpace.x += parkingOffset * Cos(ppi->m_orientation);
			info->parkingSpace.y += parkingOffset * Sin(ppi->m_orientation);
		}
	}

	ProductionUpdateInterface* pu = getObject()->getProductionUpdateInterface();
	if (pu)
		pu->setHoldDoorOpen(ppi->m_door, true);

	return true;
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::calcPPInfo( ObjectID id, PPInfo *info )
{

	ParkingPlaceInfo* ppi = findPPI( id );
	if( !ppi )
	{
		//Utter failure.
		return;
	}
	const RunwayInfo& rr = m_runways[ ppi->m_runway ];

	if( info )
	{
		const ParkingPlaceBehaviorModuleData* d = getParkingPlaceBehaviorModuleData();
		info->parkingSpace = d->m_parkInHangars ? ppi->m_hangarStart : ppi->m_location;
		info->runwayPrep = ppi->m_prep;
		info->parkingOrientation = d->m_parkInHangars ? ppi->m_hangarStartOrient : ppi->m_orientation;
		info->runwayStart = rr.m_start;
		info->runwayEnd = rr.m_end;
		info->runwayApproach = rr.m_end;
		//const Real APPROACH_DIST = 0.5f;	// no, too short, planes may not have enough space to drop altitude
		const Real APPROACH_DIST = 0.75f;
		info->runwayApproach.x += (rr.m_end.x - rr.m_start.x) * APPROACH_DIST;
		info->runwayApproach.y += (rr.m_end.y - rr.m_start.y) * APPROACH_DIST;
		info->runwayApproach.z = rr.m_end.z + d->m_approachHeight + d->m_landingDeckHeightOffset;
		info->runwayExit = info->runwayApproach;
		info->hangarInternal = ppi->m_hangarStart;
		info->hangarInternalOrient = ppi->m_hangarStartOrient;

		//Cache the runway's takeoff distance used by JetAIUpdate for calculating lift.
		Coord3D vector = info->runwayStart;
		vector.sub( &info->runwayEnd );
		info->runwayTakeoffDist = vector.length();

		for (std::vector<RunwayInfo>::iterator it = m_runways.begin(); it != m_runways.end(); ++it)
		{
			if (it->m_inUseBy == id)
			{
				if (it->m_wasInLine)
				{
					info->runwayStart = info->runwayPrep;
				}
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::releaseSpace(ObjectID id)
{
	buildInfo();
	purgeDead();

	ProductionUpdateInterface* pu = getObject()->getProductionUpdateInterface();
	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
		{
			it->m_objectInSpace = INVALID_ID;
			it->m_reservedForExit = false;
			if (pu)
				pu->setHoldDoorOpen(it->m_door, false);
			break;
		}
	}

	Object *obj = TheGameLogic->findObjectByID( id );
	if( obj )
	{
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) );
	}

}

//-------------------------------------------------------------------------------------------------
Int ParkingPlaceBehavior::getRunwayIndex(ObjectID id)
{
	purgeDead();

	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
		{
			return it->m_runway;
		}
	}
	return InvalidRunway;
}

//-------------------------------------------------------------------------------------------------
ObjectID ParkingPlaceBehavior::getRunwayReservation( Int runway, RunwayReservationType type )
{
	//Note: We don't care about type because these runways share the runway for taking off and landing.
	buildInfo();
	purgeDead();
	return m_runways[runway].m_inUseBy;
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::transferRunwayReservationToNextInLineForTakeoff(ObjectID id)
{
	buildInfo();
	purgeDead();
	for (std::vector<RunwayInfo>::iterator it = m_runways.begin(); it != m_runways.end(); ++it)
	{
		if (it->m_inUseBy == id)
		{
			if (it->m_nextInLineForTakeoff != INVALID_ID)
			{
				it->m_inUseBy = it->m_nextInLineForTakeoff;
				it->m_wasInLine = true;
				it->m_nextInLineForTakeoff = INVALID_ID;
			}
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool ParkingPlaceBehavior::reserveRunway(ObjectID id, Bool forLanding)
{
	buildInfo();
	purgeDead();

	Int runway = -1;
	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace == id)
		{
			runway = it->m_runway;
			break;
		}
	}

	if (runway == -1)
	{
		DEBUG_CRASH(("only planes with reserved spaces can reserve runways"));
		return false;
	}

	RunwayInfo& info = m_runways[runway];
	if (info.m_inUseBy == id)
	{
		return true;
	}
	else if (info.m_inUseBy == INVALID_ID)
	{
		info.m_inUseBy = id;

		if (info.m_nextInLineForTakeoff == id)
		{
			info.m_nextInLineForTakeoff = INVALID_ID;
			info.m_wasInLine = true;
		}
		else
		{
			info.m_wasInLine = false;
		}

		return true;
	}
	else if (!forLanding && info.m_nextInLineForTakeoff == INVALID_ID)
	{
		info.m_nextInLineForTakeoff = id;
		return false;	// yes, that's right
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::releaseRunway(ObjectID id)
{
	buildInfo();
	purgeDead();

	for (std::vector<RunwayInfo>::iterator it = m_runways.begin(); it != m_runways.end(); ++it)
	{
		if (it->m_inUseBy == id)
		{
			it->m_inUseBy = INVALID_ID;
			it->m_wasInLine = false;
		}
		if (it->m_nextInLineForTakeoff == id)
		{
			it->m_nextInLineForTakeoff = INVALID_ID;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// don't really need to autoheal every frame....
const Int HEAL_RATE_FRAMES = LOGICFRAMES_PER_SECOND / 5;

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::resetWakeFrame()
{
	if (m_healing.empty())
	{
		m_nextHealFrame = FOREVER;
	}
	else
	{
		m_nextHealFrame = TheGameLogic->getFrame() + HEAL_RATE_FRAMES;
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::setHealee(Object* healee, Bool add)
{
	if (add)
	{
		for (std::list<HealingInfo>::const_iterator it = m_healing.begin(); it != m_healing.end(); ++it)
		{
			if (it->m_gettingHealedID == healee->getID())
				return;
		}
		HealingInfo info;
		info.m_gettingHealedID = healee->getID();
		info.m_healStartFrame = TheGameLogic->getFrame();
		m_healing.push_back(info);
		resetWakeFrame();
	}
	else
	{
		for (std::list<HealingInfo>::iterator it = m_healing.begin(); it != m_healing.end(); /*++it*/)
		{
			if (it->m_gettingHealedID == healee->getID())
			{
				it = m_healing.erase(it);
				resetWakeFrame();
			}
			else
			{
				++it;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::defectAllParkedUnits(Team* newTeam, UnsignedInt detectionTime)
{
	buildInfo();
	purgeDead();

	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace != INVALID_ID)
		{
			Object* obj = TheGameLogic->findObjectByID(it->m_objectInSpace);
			if (obj == NULL || obj->isEffectivelyDead())
				continue;

			// srj sez: evil. fix better someday. 
			static NameKeyType jetKey = TheNameKeyGenerator->nameToKey("JetAIUpdate");
			JetAIUpdate* ju = (JetAIUpdate *)obj->findUpdateModule(jetKey);
			Bool takeoffOrLanding = ju ? ju->friend_isTakeoffOrLandingInProgress() : false;

			if (obj->isAboveTerrain() && !takeoffOrLanding)
			{
				// if the new team is a different controlling player, this guys loses his space.
				if (newTeam->getControllingPlayer() != obj->getControllingPlayer())
				{
					releaseSpace(obj->getID());
					if (obj->getProducerID() == getObject()->getID())
						obj->setProducer(NULL);
				}
			}
			else
			{
				obj->defect(newTeam, detectionTime);
			}
		}
	}

	purgeDead();
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::killAllParkedUnits()
{
	buildInfo();
	purgeDead();

	for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
	{
		if (it->m_objectInSpace != INVALID_ID)
		{
			Object* obj = TheGameLogic->findObjectByID(it->m_objectInSpace);
			if (obj == NULL || obj->isEffectivelyDead())
				continue;

			// srj sez: evil. fix better someday. 
			static NameKeyType jetKey = TheNameKeyGenerator->nameToKey("JetAIUpdate");
			JetAIUpdate* ju = (JetAIUpdate *)obj->findUpdateModule(jetKey);
			Bool takeoffOrLanding = ju ? ju->friend_isTakeoffOrLandingInProgress() : false;

			if (obj->isAboveTerrain() && !takeoffOrLanding)
				continue;
		
			obj->kill();
		}
	}

	purgeDead();
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::onDie( const DamageInfo *damageInfo )
{
	m_scrambles.clear();
	killAllParkedUnits();
}

//-------------------------------------------------------------------------------------------------
// the old owner's scramble was aimed at the old owner's enemies
void ParkingPlaceBehavior::onCapture( Player *oldOwner, Player *newOwner )
{
	if( !m_scrambles.empty() )
		holdHangarDoorsOpen( false );
	m_scrambles.clear();
}

//-------------------------------------------------------------------------------------------------
// one scrambled jet every this many frames, the runways in turn, so each runway sends one every
// half second and a jet is 15 down its runway, about a drone's length, when the next one starts
const UnsignedInt SCRAMBLE_INTERVAL_FRAMES = LOGICFRAMES_PER_SECOND / 4;
// the model's hangars: the first rows of spaces, one hangar to a runway in each row
const Int HANGAR_ROWS = 2;
// a hangar turned more than 60 degrees off its runway is a side shed with no door (the second
// hangar of the Chinese field's runway 2): a jet would appear in its open bay and pivot onto the
// runway, so that runway's jets all come out of its other hangar
const Real HANGAR_FACING_MIN_COS = 0.5f;
// the hangar doors take two seconds to open; the first jet rolls when they are half way
const UnsignedInt HANGAR_DOOR_LEAD_FRAMES = LOGICFRAMES_PER_SECOND;

//-------------------------------------------------------------------------------------------------
/// hold every hangar door open, or let go of those no jet holds a space behind
void ParkingPlaceBehavior::holdHangarDoorsOpen( Bool hold )
{
	ProductionUpdateInterface* pu = getObject()->getProductionUpdateInterface();
	const Int hangars = HANGAR_ROWS * (Int)m_runways.size();
	for( Int i = 0; i < hangars; ++i )
	{
		if( hold || m_spaces[ i ].m_objectInSpace == INVALID_ID )
			pu->setHoldDoorOpen( m_spaces[ i ].m_door, hold );
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::scramble( const ThingTemplate *jet, ObjectID target, const Coord3D *pos )
{
	if( m_scrambles.empty() )
	{
		holdHangarDoorsOpen( true );
		m_nextScrambleFrame = TheGameLogic->getFrame() + HANGAR_DOOR_LEAD_FRAMES;
	}
	ScrambleInfo info;
	info.m_jet = jet;
	info.m_target = target;
	info.m_pos = *pos;
	m_scrambles.push_back( info );
}

//-------------------------------------------------------------------------------------------------
/** The next scrambled jet, when its turn has come: it comes out of one of its runway's two hangars,
	* whose doors the swarm holds open, and starts its takeoff roll at once (JetAIUpdate::
	* friend_takeOffNow), straight out of the door onto the runway, with no taxi or queue for the
	* runway, so a swarm is in the air within seconds rather than four at a time out of the hangars.
	* The runways take turns by the count left, and a runway's hangars by the count after that, a side
	* shed that does not face down its runway left out; a jet
	* holds one of the spaces, the runway's, until it is airborne; the field has a space for every jet
	* of a swarm. */
//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::launchScramble()
{
	UnsignedInt now = TheGameLogic->getFrame();
	if( m_scrambles.empty() || now < m_nextScrambleFrame )
		return;

	const Int runway = (Int)m_scrambles.size() % (Int)m_runways.size();
	ParkingPlaceInfo* space = NULL;
	for( std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it )
	{
		if( it->m_runway == runway && it->m_objectInSpace == INVALID_ID && !it->m_reservedForExit )
		{
			space = &(*it);
			break;
		}
	}
	if( space == NULL )
		return;

	const ScrambleInfo& next = m_scrambles.front();
	Object *jet = TheThingFactory->newObject( next.m_jet, getObject()->getControllingPlayer()->getDefaultTeam() );
	jet->setProducer( getObject() );
	space->m_objectInSpace = jet->getID();
	Int hangarRow = ( (Int)m_scrambles.size() / (Int)m_runways.size() ) % HANGAR_ROWS;
	const RunwayInfo& strip = m_runways[ runway ];
	Coord3D along;
	along.set( strip.m_end.x - strip.m_start.x, strip.m_end.y - strip.m_start.y, 0.0f );
	const Real hangarOrient = m_spaces[ hangarRow * (Int)m_runways.size() + runway ].m_hangarStartOrient;
	if( Cos( hangarOrient ) * along.x + Sin( hangarOrient ) * along.y < HANGAR_FACING_MIN_COS * along.length() )
		hangarRow = ( hangarRow + 1 ) % HANGAR_ROWS;
	const ParkingPlaceInfo& hangar = m_spaces[ hangarRow * (Int)m_runways.size() + runway ];
	jet->setPosition( &hangar.m_hangarStart );
	jet->setOrientation( hangar.m_hangarStartOrient );
	TheAI->pathfinder()->addObjectToPathfindMap( jet );

	JetAIUpdate *jetAI = jet->getAIUpdateInterface()->getJetAIUpdate();
	jetAI->friend_setKamikazeArea( &next.m_pos );
	// a script's order: a human player's unit given CMD_FROM_AI will not fly out of its sight to chase
	// (AIAttackApproachTargetState), and this target is a map away. On the ground the jet keeps it as
	// its pending command and flies it once airborne.
	Object *target = TheGameLogic->findObjectByID( next.m_target );
	if( target && !target->isEffectivelyDead() )
		jetAI->aiAttackObject( target, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
	else
		jetAI->aiAttackPosition( &next.m_pos, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
	jetAI->friend_takeOffNow();
	DEBUG_LOG(( "SCRAMBLE frame %d '%s' %d from hangar %d down runway %d at %d (%.0f,%.0f), %d to go\n", now,
		next.m_jet->getName().str(), jet->getID(), hangarRow, runway, next.m_target, next.m_pos.x, next.m_pos.y,
		(Int)m_scrambles.size() - 1 ));

	m_scrambles.pop_front();
	m_nextScrambleFrame = now + SCRAMBLE_INTERVAL_FRAMES;
	if( m_scrambles.empty() )
		holdHangarDoorsOpen( false );
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime ParkingPlaceBehavior::update()
{
	// alas, we need to keep the buildInfo and dead-purged stuff pretty much up to date, for
	// the client to be able to peek at. at this late date, the most expedient way is to ensure 
	// our update is run every frame, and do this manually. the extra cost should be trivial, since
	// there are generally at most only a few airfields at any given time.
	buildInfo();
	purgeDead();
	launchScramble();

	UnsignedInt now = TheGameLogic->getFrame();
	if (now >= m_nextHealFrame)
	{
		m_nextHealFrame = now + HEAL_RATE_FRAMES;
		const ParkingPlaceBehaviorModuleData* d = getParkingPlaceBehaviorModuleData();
		for (std::list<HealingInfo>::iterator it = m_healing.begin(); it != m_healing.end(); /*++it*/)
		{
			if (it->m_gettingHealedID != INVALID_ID)
			{
				Object* objToHeal = TheGameLogic->findObjectByID(it->m_gettingHealedID);
				if (objToHeal == NULL || objToHeal->isEffectivelyDead())
				{
					it = m_healing.erase(it);
				}
				else
				{
					DamageInfo healInfo;
					healInfo.in.m_damageType = DAMAGE_HEALING;
					healInfo.in.m_deathType = DEATH_NONE;
					healInfo.in.m_sourceID = getObject()->getID();
  				healInfo.in.m_amount = HEAL_RATE_FRAMES * d->m_healAmount * SECONDS_PER_LOGICFRAME_REAL * objToHeal->getHandicapHealthFactor();

//          if ( objToHeal->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
//            healInfo.in.m_amount += HEAL_RATE_FRAMES * d->m_extraHealAmount4Helicopters * SECONDS_PER_LOGICFRAME_REAL;

					BodyModuleInterface *body = objToHeal->getBodyModule();
					body->attemptHealing( &healInfo );
					++it;
				}
			}
		}
	}

	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
ExitDoorType ParkingPlaceBehavior::reserveDoorForExit( const ThingTemplate* objType, Object *specificObject )
{
	buildInfo();
	purgeDead();

	if (objType->isKindOf(KINDOF_PRODUCED_AT_HELIPAD))
	{
		return DOOR_NONE_NEEDED;
	}

	// note that this never allocates a per-unit parking space, even if specificObject is null.
	// this is by design and is assumed by exitObjectViaDoor.
	ParkingPlaceInfo* ppi = findEmptyPPI();
	if (ppi)
	{
		ppi->m_objectInSpace = INVALID_ID;
		ppi->m_reservedForExit = true;
		return ppi->m_door;
	}

	return DOOR_NONE_AVAILABLE;
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::exitObjectViaDoor( Object *newObj, ExitDoorType exitDoor ) ///< Here is the thing I want you to exit
{
	if (exitDoor != DOOR_NONE_NEEDED)
	{
		ParkingPlaceInfo* ppi = NULL;
		for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
		{
			if (it->m_objectInSpace == INVALID_ID && it->m_reservedForExit == TRUE && it->m_door == exitDoor)
			{
				ppi = &(*it);
				break;
			}
		}

		if (!ppi)
		{
			DEBUG_CRASH(("could not find the space. what?"));
			return;
		}

		ppi->m_objectInSpace = newObj->getID();
		ppi->m_reservedForExit = false;
	}

	/// @todo srj -- this is evil. fix.
	static NameKeyType jetKey = TheNameKeyGenerator->nameToKey( "JetAIUpdate" );
	JetAIUpdate* ju = (JetAIUpdate *)newObj->findUpdateModule( jetKey );
	Real parkingOffset = ju ? ju->friend_getParkingOffset() : 0.0f;
	Bool producedAtHelipad = newObj->isKindOf(KINDOF_PRODUCED_AT_HELIPAD);

	PPInfo ppinfo;
	DUMPMATRIX3D(getObject()->getTransformMatrix());
	DUMPCOORD3D(getObject()->getPosition());
	if (producedAtHelipad)
	{
		CRCDEBUG_LOG(("Produced at helipad (door = %d)\n", exitDoor));
		DEBUG_ASSERTCRASH(exitDoor == DOOR_NONE_NEEDED, ("Hmm, unlikely"));
		Matrix3D mtx;
#ifdef DEBUG_CRASHING
		Bool boneOk =
#endif
			getObject()->getSingleLogicalBonePosition("HeliPark01", &ppinfo.hangarInternal, &mtx);

		DEBUG_ASSERTCRASH(boneOk, ("Could not get bone!"));
		ppinfo.hangarInternalOrient = mtx.Get_Z_Rotation();
		ppinfo.parkingSpace = ppinfo.hangarInternal;
		ppinfo.parkingOrientation = ppinfo.hangarInternalOrient;
	}
	else
	{
		CRCDEBUG_LOG(("Produced at hangar (door = %d)\n", exitDoor));
		DEBUG_ASSERTCRASH(exitDoor != DOOR_NONE_NEEDED, ("Hmm, unlikely"));
		if (!reserveSpace(newObj->getID(), parkingOffset, &ppinfo)) //&loc, &orient, NULL, NULL, NULL, NULL, &hangarInternal, &hangOrient))
		{
			DEBUG_CRASH(("no spaces available, how did we get here?"));
			ppinfo.parkingSpace = *getObject()->getPosition();
			ppinfo.parkingOrientation = getObject()->getOrientation();
		}
	}
	DUMPCOORD3D(&ppinfo.hangarInternal);
	DUMPREAL(ppinfo.hangarInternalOrient);
	DUMPCOORD3D(&ppinfo.parkingSpace);
	DUMPREAL(ppinfo.parkingOrientation);

	newObj->setPosition( &ppinfo.hangarInternal );
	newObj->setOrientation( ppinfo.hangarInternalOrient );
	TheAI->pathfinder()->addObjectToPathfindMap( newObj );

	AIUpdateInterface  *ai = newObj->getAIUpdateInterface();
	if( ai )
	{
		Bool movedToRallyPoint = FALSE;
		if( producedAtHelipad )
		{
			const Coord3D *rallyPoint = getRallyPoint();
			if( rallyPoint )
			{
				ai->aiMoveToPosition( rallyPoint, CMD_FROM_AI );
				movedToRallyPoint = TRUE;
			}
		}
		if( !movedToRallyPoint )
		{
			if( !newObj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
			{
				std::vector<Coord3D> exitPath;
				exitPath.push_back(ppinfo.parkingSpace);
				ai->aiFollowExitProductionPath( &exitPath, getObject(), CMD_FROM_AI );
			}
			else
			{
				// Lorenzen sez: aiMoveToPosition has an added benefit. 
				// It invokes the pathfinder to find a vacant destination.
	      ai->aiMoveToPosition( &ppinfo.parkingSpace, CMD_FROM_AI );
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::unreserveDoorForExit( ExitDoorType exitDoor )
{
	if (exitDoor != DOOR_NONE_NEEDED)
	{
		for (std::vector<ParkingPlaceInfo>::iterator it = m_spaces.begin(); it != m_spaces.end(); ++it)
		{
			if (it->m_door == exitDoor)
			{
				//DEBUG_ASSERTCRASH(it->m_reservedForExit, ("ParkingPlaceBehavior::unreserveDoorForExit: door %d was not reserved\n",exitDoor));
				it->m_objectInSpace = INVALID_ID;
				it->m_reservedForExit = false;
				return;
			}
		}

		DEBUG_CRASH(("ParkingPlaceBehavior::unreserveDoorForExit: door %d was not found\n",exitDoor));
	}
}

//-------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::setRallyPoint( const Coord3D *pos )
{
	m_heliRallyPointExists = TRUE;
	m_heliRallyPoint.set( pos );
	// nothing
}

//-------------------------------------------------------------------------------------------------
const Coord3D* ParkingPlaceBehavior::getRallyPoint( void ) const
{
	if( m_heliRallyPointExists )
	{
		return &m_heliRallyPoint;
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------
//We only use this for the helipad -- therefore the exit position is the helipad creation point.
//-------------------------------------------------------------------------------------------------
Bool ParkingPlaceBehavior::getExitPosition( Coord3D& exitPosition ) const
{
	Matrix3D mtx;
	return getObject()->getSingleLogicalBonePosition("HeliPark01", &exitPosition, &mtx );
}

//-------------------------------------------------------------------------------------------------
Bool ParkingPlaceBehavior::getNaturalRallyPoint( Coord3D& rallyPoint, Bool offset ) const
{
	Matrix3D mtx;
	return getObject()->getSingleLogicalBonePosition("HeliPark01", &rallyPoint, &mtx );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 4: the scramble queue */
// ------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::xfer( Xfer *xfer )
{
	Int i;

	// version
	const XferVersion currentVersion = 4;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	if( xfer->getXferMode() == XFER_LOAD )
	{
		// first, build our info, so it won't be overwritten later.
		buildInfo();
	}

	// spaces info count and data
	UnsignedByte spacesCount = m_spaces.size();
	xfer->xferUnsignedByte( &spacesCount );
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// save all elements
		std::vector< ParkingPlaceInfo >::iterator it;
		for( it = m_spaces.begin(); it != m_spaces.end(); ++it )
		{

			// object in this space
			xfer->xferObjectID( &((*it).m_objectInSpace) );

			// reserved for exit
			xfer->xferBool( &((*it).m_reservedForExit) );

		}  // end for, it

	}  // end if, save
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		ObjectID objectID;
		Bool reservedForExit;

		// read all elements
		std::vector< ParkingPlaceInfo >::iterator it;
		it = m_spaces.begin();
		for( i = 0; i < spacesCount; ++i )
		{

			// read object id
			xfer->xferObjectID( &objectID );

			// read reserved flag
			xfer->xferBool( &reservedForExit );

			// store in vector if the vector does indeed still have room for this entry
			if( it != m_spaces.end() )
			{

				(*it).m_objectInSpace = objectID;
				(*it).m_reservedForExit = reservedForExit;
				++it;

			}  // end if

		}  // end for, i

	}  // end else, load

	// runways cound and info
	UnsignedByte runwaysCount = m_runways.size();
	xfer->xferUnsignedByte( &runwaysCount );
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// save all elements
		std::vector< RunwayInfo >::iterator it;
		for( it = m_runways.begin(); it != m_runways.end(); ++it )
		{

			// save object ID
			xfer->xferObjectID( &((*it).m_inUseBy) );
			xfer->xferObjectID( &((*it).m_nextInLineForTakeoff) );
			xfer->xferBool( &((*it).m_wasInLine) );

		}  // end for, it

	}  // end if, save
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		// read all elements
		std::vector< RunwayInfo >::iterator it;
		it = m_runways.begin();
		for( i = 0; i < runwaysCount; ++i )
		{

			ObjectID inUseBy;
			ObjectID nextInLineForTakeoff;
			Bool wasInLine;

			// read object ID
			xfer->xferObjectID( &inUseBy );
			xfer->xferObjectID( &nextInLineForTakeoff );
			xfer->xferBool( &wasInLine );

			// store in vector if the vector does indeed still have room for this entry
			if( it != m_runways.end() )
			{

				(*it).m_inUseBy = inUseBy;
				(*it).m_nextInLineForTakeoff = nextInLineForTakeoff;
				(*it).m_wasInLine = wasInLine;
				++it;

			}  // end if

		}  // end for, i

	}  // end else, load

	// healees
	UnsignedByte healCount = m_healing.size();
	xfer->xferUnsignedByte( &healCount );
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// save all elements
		std::list< HealingInfo >::iterator it;
		for( it = m_healing.begin(); it != m_healing.end(); ++it )
		{

			// save object ID
			xfer->xferObjectID( &((*it).m_gettingHealedID) );
			xfer->xferUnsignedInt( &((*it).m_healStartFrame) );

		}  // end for, it

	}  // end if, save
	else if( xfer->getXferMode() == XFER_LOAD )
	{
		// read all elements
		m_healing.clear();
		for( i = 0; i < healCount; ++i )
		{
			HealingInfo info;

			// read object ID
			xfer->xferObjectID( &info.m_gettingHealedID );
			xfer->xferUnsignedInt( &info.m_healStartFrame );
			m_healing.push_back(info);

		}  // end for, i

	}  // end else, load

	if (version >= 2)
	{
		xfer->xferCoord3D(&m_heliRallyPoint);
		xfer->xferBool(&m_heliRallyPointExists);
	}

	if (version >= 3)
	{
		xfer->xferUnsignedInt(&m_nextHealFrame);
	}
	else
	{
		// old file .. just wake it up right away, and it will fall into the correct pattern
		if (xfer->getXferMode() == XFER_LOAD)
		{
			m_nextHealFrame = 0;
		}
	}

	if (version >= 4)
	{
		xfer->xferUnsignedInt(&m_nextScrambleFrame);
		Int scrambleCount = (Int)m_scrambles.size();
		xfer->xferInt(&scrambleCount);
		if (xfer->getXferMode() == XFER_SAVE)
		{
			for (std::list<ScrambleInfo>::iterator it = m_scrambles.begin(); it != m_scrambles.end(); ++it)
			{
				AsciiString name = it->m_jet->getName();
				xfer->xferAsciiString(&name);
				xfer->xferObjectID(&it->m_target);
				xfer->xferCoord3D(&it->m_pos);
			}
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			m_scrambles.clear();
			for (i = 0; i < scrambleCount; ++i)
			{
				AsciiString name;
				ScrambleInfo info;
				xfer->xferAsciiString(&name);
				xfer->xferObjectID(&info.m_target);
				xfer->xferCoord3D(&info.m_pos);
				info.m_jet = TheThingFactory->findTemplate(name);
				m_scrambles.push_back(info);
			}
		}
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParkingPlaceBehavior::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

	// no, this is bad.. it is NOT SAFE to call setWakeFrame from the xfer system. crap. (srj)
	// make sure we are awake... old save games let us sleep
	//setWakeFrame(getObject(), UPDATE_SLEEP_NONE); 

}  // end loadPostProcess
