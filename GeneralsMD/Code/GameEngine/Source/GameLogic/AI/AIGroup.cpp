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

// AIGroup.cpp
// Encapsulation of a simple group of AI agents
// Author: Michael S. Booth, January 2002
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "Common/ActionManager.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Line2D.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CrowdModel.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/OverchargeBehavior.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "Lib/FixBoundary.h"		// group goals, paths and lane offsets are still float

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

/**
 * NOTE: Only AI objects (ie: having an AIUpdate module) can be in
 * AIGroups.  It is ASSUMED that an object cannot morph from having
 * an AIUpdate module to not having one... (MSB)

	NOTE: This comment has been wrong for about ten years now.  Any Object can be in an AIGroup
 */

/**
 * Constructor
 */
AIGroup::AIGroup( void )
{
//	DEBUG_LOG(("***AIGROUP %x is being constructed.\n", this));
	m_groundPath = NULL;
	m_speed = 0.0f;
	m_matchSpeeds = TRUE;
	m_dirty = false;
	m_id = TheAI->getNextGroupID();
	m_memberListSize = 0;
	m_memberList.clear();
	//DEBUG_LOG(( "AIGroup #%d created\n", m_id ));
}

/**
 * Destructor
 */
AIGroup::~AIGroup()
{
//	DEBUG_LOG(("***AIGROUP %x is being destructed.\n", this));
	// disassociate each member from the group
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); /* empty */ )
	{
		Object *member = *i;
		if (member)
		{
			member->leaveGroup();
			i = m_memberList.begin();	// jump back to the beginning, cause ai->leaveGroup will remove this element. 
		}
		else
		{
			i = m_memberList.erase(i);
		}
	}
	if (m_groundPath) {
		m_groundPath->deleteInstance();
		m_groundPath = NULL;
	}
	//DEBUG_LOG(( "AIGroup #%d destroyed\n", m_id ));
}

/**
 * Return this group's unique ID
 */
UnsignedInt AIGroup::getID( void )
{
	return m_id;
}

/**
 * Return the group IDs for every member in this group
 */
const VecObjectID& AIGroup::getAllIDs( void ) const
{
	m_lastRequestedIDList.clear();
	for (std::list<Object *>::const_iterator cit = m_memberList.begin(); cit != m_memberList.end(); ++cit)
	{
		if ((*cit) == NULL)
			continue;

		m_lastRequestedIDList.push_back((*cit)->getID());
	}

	return m_lastRequestedIDList;
}


/**
 * Return the speed of the group's slowest member
 */
Real AIGroup::getSpeed( void )
{
	if (m_dirty)
		recompute();

	return m_speed;
}

/**
 * Return true if object is in this group
 */
Bool AIGroup::isMember( Object *obj )
{
	std::list<Object *>::iterator i = std::find( m_memberList.begin(), m_memberList.end(), obj );

	if (i == m_memberList.end())
		return false;

	return true;
}

/**
 * Add object to group.
 * Only allow AI agents into the group.
 */
void AIGroup::add( Object *obj )
{
//	DEBUG_LOG(("***AIGROUP %x is adding Object %x (%s).\n", this, obj, obj->getTemplate()->getName().str()));
	DEBUG_ASSERTCRASH(obj != NULL, ("trying to add null obj to AIGroup"));
	if (obj == NULL)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();

	//If this object doesn't have an AIUpdateInterface, then 
	//don't add it to the group UNLESS it is a structure! Structures
	//with AIUpdateInterfaces also issue similar commands, but those
	//commands don't need AI updates... they are instant commands like
	//evacuate or triggering certain special powers...
	KindOfMaskType validNonAIKindofs;
	validNonAIKindofs.set(KINDOF_STRUCTURE);
	validNonAIKindofs.set(KINDOF_ALWAYS_SELECTABLE);
	if( ai == NULL && !obj->isAnyKindOf( validNonAIKindofs ) )
	{
		return;
	}

	// add to group's list of objects
	m_memberList.push_back( obj );
	++m_memberListSize;
//	DEBUG_LOG(("***AIGROUP %x has size %u now.\n", this, m_memberListSize));

	obj->enterGroup( this );

	// list has changed, properties need recomputation
	m_dirty = true;
}

/**
 * Remove object from group
 */
Bool AIGroup::remove( Object *obj )
{
//	DEBUG_LOG(("***AIGROUP %x is removing Object %x (%s).\n", this, obj, obj->getTemplate()->getName().str()));
	std::list<Object *>::iterator i = std::find( m_memberList.begin(), m_memberList.end(), obj );

	// make sure object is actually in the group
	if (i == m_memberList.end())
		return FALSE;

	// remove it
	m_memberList.erase( i );
	--m_memberListSize;
//	DEBUG_LOG(("***AIGROUP %x has size %u now.\n", this, m_memberListSize));

	// tell object to forget about group
	obj->leaveGroup();

	// list has changed, properties need recomputation
	m_dirty = true;

	// if the group is empty, no-one is using it any longer, so destroy it
	if (isEmpty()) {
		TheAI->destroyGroup( this );
		return TRUE;
	}

	return FALSE;
}

/**
 * If the group contains any objects not owned by ownerPlayer, return TRUE.
 */
Bool AIGroup::containsAnyObjectsNotOwnedByPlayer( const Player *ownerPlayer )
{
	ListObjectPtrIt it;

	for (it = m_memberList.begin(); it != m_memberList.end(); ++it) {
		Object *obj = (*it);
		if (!obj) {
			continue;
		}

		if (obj->getControllingPlayer() != ownerPlayer) {
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * Remove any objects that aren't owned by the player, and return true if the group was destroyed due to emptiness
 */
Bool AIGroup::removeAnyObjectsNotOwnedByPlayer( const Player *ownerPlayer )
{
	ListObjectPtrIt it;

	for (it = m_memberList.begin(); it != m_memberList.end(); /* empty */) {
		Object *obj = (*it);
		if (!obj) {
			++it;			// the increment is in the body of this loop, so skipping it spins forever
			continue;
		}

		if (obj->getControllingPlayer() != ownerPlayer) {
			// Advance the iterator first, its about to become invalid.
			++it;

			if (remove(obj)) {
				return TRUE;
			}
			continue;
		}

		++it;
	}

	return FALSE;
}


/**
 * Compute the centroid of the group, in fixed point
 */
static Bool groupCenterFix( const std::list<Object *> &members, FCoord3D *center )
{
	Int count = 0;
	center->zero();

	std::list<Object *>::const_iterator i;
	for( i = members.begin(); i != members.end(); ++i )
	{
		if( (*i)->isDisabledByType( DISABLED_HELD) )
		{
			continue; // don't bother counting riders in the center calculation.
		}
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			center->add( *(*i)->getPositionFix() );
			++count;
		}
	}

	if (count == 0 && !members.empty())
	{
		/*
			if there are no AIs (eg, the team consists of a faction bldg), we can get here.

			This was originally used to offset the centers of objects moving (still used for that) and non-ais can't move.  
			So if you have a mix of ai's & not ai's, you want just the ais.
			But it seems reasonable that if there are no ai's, it returns the center of the other stuff.  Cause they won't be moving anyway.
		*/
		for( i = members.begin(); i != members.end(); ++i )
		{
			if( (*i)->isDisabledByType( DISABLED_HELD) )
			{
				continue; // don't bother counting riders in the center calculation.
			}
			center->add( *(*i)->getPositionFix() );
			++count;
		}
	}

	if (count > 0)
	{
		const Fix n( count );
		center->x /= n;
		center->y /= n;
		center->z /= n;
	}

	return count > 0;
}

/**
 * Compute the centroid of the group
 */
Bool AIGroup::getCenter( Coord3D *center )
{
	FCoord3D c;
	Bool ok = groupCenterFix( m_memberList, &c );
	*center = c.toCoord3D();	// P5: the center goes on to the pathfinder and the move calls
	return ok;
}

Bool AIGroup::getMinMaxAndCenter( Coord2D *min, Coord2D *max, Coord3D *center )
{
	Int count = 0;
	FCoord2D lo, hi;
	lo.set( FIX_MAX, FIX_MAX );
	hi.set( -FIX_MAX, -FIX_MAX );
	FCoord3D sum;
	sum.zero();

	std::list<Object *>::iterator i;
	FormationID id= NO_FORMATION_ID;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		if( (*i)->isDisabledByType( DISABLED_HELD) ) 
		{
			continue; // don't bother counting riders in the center calculation.
		}
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			const FCoord3D *objPos = (*i)->getPositionFix();
			sum.add( *objPos );

			//Calculate the bounding coordinates of all units
			lo.x = fixMin( lo.x, objPos->x );
			hi.x = fixMax( hi.x, objPos->x );
			lo.y = fixMin( lo.y, objPos->y );
			hi.y = fixMax( hi.y, objPos->y );
			FormationID curID = (*i)->getFormationID() ;
			if (count==0) {
				id = curID;
			} else if (curID != id) {
				// a formation only if every member is in the same one; this compared id with itself,
				// so the first member alone decided it
				id = NO_FORMATION_ID;
			}

			count++;
		}
	}

	if (count > 0)
	{
		const Fix n( count );
		sum.x /= n;
		sum.y /= n;
		sum.z /= n;
	}
	// P5: the box and the center go on to the ground path planner
	*min = lo.toCoord2D();
	*max = hi.toCoord2D();
	*center = sum.toCoord3D();
	Bool isFormation = (id!=NO_FORMATION_ID);
	if (count<2) isFormation = false;
	return isFormation;
}


/**
 * Compute the speed of the team (its slowest member's speed),
 * and find the leader (closest to center of group).
 */
void AIGroup::recompute( void )
{
	Fix closeDist = FIX_MAX;
	Fix dx, dy, dist;
	const FCoord3D *objPos;
	FCoord3D center;

	groupCenterFix( m_memberList, &center );

	if (m_groundPath) {
		m_groundPath->deleteInstance();
		m_groundPath = NULL;
	}

	m_speed = 9999999999.9f;

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		// don't consider immobile things for leadership
		if ((*i)->isKindOf(KINDOF_IMMOBILE))
			continue;

		if( (*i)->isDisabledByType( DISABLED_HELD) ) 
		{
			continue; // don't bother counting riders in the max speed calculation.
		}
		Object *obj = (*i);
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai)
		{

			//
			// speed is slowest speed ... we won't consider slow speeds from objects that
			// are being penalized by their damage state, if we did the whole group would slow
			// down to that slowest penalized speed, instead only those objects that are penalized
			// will fall out of "formation" ... bummer for them!
			//
			Real maxSpeed = ai->getCurLocomotorSpeed();
			if( m_speed > maxSpeed &&
				  IS_CONDITION_BETTER( obj->getBodyModule()->getDamageState(), TheGlobalData->m_movementPenaltyDamageState ) )
				m_speed = maxSpeed;

			// leader is closest to the group's center
			objPos = obj->getPositionFix();
			dx = objPos->x - center.x;
			dy = objPos->y - center.y;
			dist = dx*dx + dy*dy;
			if (dist < closeDist)
			{
				closeDist = dist;
			}
		}
	}
	// clear "dirty bit" - data is up-to-date
	m_dirty = false;
}

/**
 * Return the number of objects in the group
 */
Int AIGroup::getCount( void )
{
	return m_memberListSize;
}

/**
 * Returns true if the group has no members
 */
Bool AIGroup::isEmpty( void ) const
{
	return m_memberList.empty();
}

/**
 * Given a destination location, compute the destination position for
 * this object such that it keeps its relative position with the group.
 */
void AIGroup::computeIndividualDestination( Coord3D *dest, const Coord3D *groupDest, 
																					 Object *obj, const Coord3D *center, Bool isFormation )
{
	Coord2D v;

	// compute vector from "group center" to self
	// P5: the offset is laid out in float against the float destination the pathfinder takes
	const FCoord3D *pos = obj->getPositionFix();
	if (isFormation) {
		obj->getFormationOffset(&v);
	}	else {
		v.x = fixToReal( pos->x ) - center->x;
		v.y = fixToReal( pos->y ) - center->y;
	}
	Real length = v.length();
	const Real maxLength = fixToReal( 6 * obj->getGeometryInfo().getBoundingCircleRadiusFix() );
	if (length > maxLength) {
		length = maxLength;
	}
	v.normalize();
	v.x *= length;
	v.y *= length;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(groupDest);

	// move to same offset at destination
	/// @todo use fast int->real type cast here later
	dest->x = groupDest->x + v.x;
	dest->y = groupDest->y + v.y;
	dest->z = fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( dest->x ), fixFromReal( dest->y ), layer ) );
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai && ai->isDoingGroundMovement()) {
		if (isFormation) {
			TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), dest, NULL);
		}	else {
			TheAI->pathfinder()->adjustDestination(obj, ai->getLocomotorSet(), dest, groupDest);
		}
		TheAI->pathfinder()->updateGoal(obj, dest, LAYER_GROUND);
	}

}

static const Int PATH_DIAMETER_IN_CELLS = 6;

//-------------------------------------------------------------------------------------------------
// Internal function for moving a group of infantry as a column.
//

/**
 * Move to given position(s)
 */
Bool AIGroup::friend_computeGroundPath( const Coord3D *pos, CommandSourceType cmdSource )

{

	if (m_dirty)
		recompute();

	std::list<Object *>::iterator i;
	// compute current centroid of the team
	Coord3D center;
	Coord2D min;
	Coord2D max;
	Real dx, dy;

	if (TheGlobalData->m_debugAI==AI_DEBUG_TERRAIN) return false;

	Bool closeEnough = false;
	getMinMaxAndCenter( &min, &max, &center );
	Real distSqr = 4*sqr(fixToReal(TheAI->getAiData()->m_distanceRequiresGroup));	// P7

	Int numInfantry = 0;
	Int numVehicles = 0; 
	Object *centerVehicle = NULL;
	Real distSqrCenterVeh = distSqr*10;
	/* The trunk is planned from the member that is ALREADY closest to the destination, not from
		 the one nearest the middle of the group.  Starting it in the middle makes every unit that
		 is ahead of the middle drive backwards to join the route, straight across the main body,
		 and jam the very column it is trying to join. */
	Object *leadVehicle = NULL;
	Real distSqrLeadVeh = 0.0f;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = (*i);
		TheAI->pathfinder()->removeGoal(obj);
		if (obj->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		if( obj->getAI()==NULL )
		{	
			continue;
		}	 
		if( obj->isKindOf( KINDOF_INFANTRY ) )
		{	
 			numInfantry++;
		} else if (obj->isKindOf( KINDOF_VEHICLE)) {
			if (obj->isKindOf(KINDOF_AIRCRAFT)) {
				continue;
			}
			numVehicles++;
		} else {
			continue;
		}
		// Note - we are getting the closest of ANY type of unit for later testing intentionally. jba.
		Coord3D unitPos = (*i)->getPositionFix()->toCoord3D();	// P5: measured against the float goal

		dx = unitPos.x-pos->x;
		dy = unitPos.y-pos->y;
		if (dx*dx+dy*dy<distSqr) {
			distSqr = dx*dx+dy*dy;
		}

		// find the object closest to where the group has been told to go.
		if (leadVehicle==NULL || dx*dx+dy*dy<distSqrLeadVeh) {
			leadVehicle = (*i);
			distSqrLeadVeh = dx*dx+dy*dy;
		}

		// find object closest to the center.
		dx = unitPos.x-center.x;
		dy = unitPos.y-center.y;
 		if (centerVehicle==NULL || dx*dx+dy*dy<distSqrCenterVeh) {
			centerVehicle = (*i);
			distSqrCenterVeh = dx*dx+dy*dy;
		}
	}

	if(centerVehicle==NULL) return false;
	center = centerVehicle->getPositionFix()->toCoord3D();	// P5

	dx = max.x - min.x;
	dy = max.y - min.y;
	if (dx*dx + dy*dy > sqr(fixToReal(TheAI->getAiData()->m_distanceRequiresGroup))) {	// P7
		distSqr = dx*dx+dy*dy;
	}
	if (distSqr < sqr(fixToReal(TheAI->getAiData()->m_minDistanceForGroup))) {	// P7
		return false;
	}
	if (distSqr>sqr(fixToReal(TheAI->getAiData()->m_distanceRequiresGroup))) {	// P7
		closeEnough = true;
	}
	if (numInfantry>6) {
		closeEnough = true;
	}
	if (numVehicles>4) {
		closeEnough = true;
	}

	if (!closeEnough) {
		Bool isPassable = true;
		// see if all units have an unobstructed path to the center.  
		// If so, then they are close enough.
		for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
		{
			Object *obj = (*i);
			if (!obj->isKindOf(KINDOF_INFANTRY)) {
				continue;
			}
			AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
			if (ai)
			{
				if (!TheAI->pathfinder()->isLinePassable(obj, 
								ai->getLocomotorSet().getValidSurfaces(), obj->getLayer(), obj->getPositionFix()->toCoord3D(),	// P5
								center, false, true)) {
					isPassable = false;
				}
			}
		}
		if (isPassable) closeEnough = true;
	}
	if (!closeEnough) return false;
	
	Coord3D trunkStart = (leadVehicle != NULL) ? leadVehicle->getPositionFix()->toCoord3D() : center;	// P5
	m_groundPath = TheAI->pathfinder()->findGroundPath(&trunkStart, pos, PATH_DIAMETER_IN_CELLS, false);
	return m_groundPath!=NULL;

}

static void clampToMap(Coord3D *dest, PlayerType pt)
// Clamps to the player's current visible map area. jba. [8/28/2003] 
{
	Region3D extent;
	if (pt==PLAYER_COMPUTER) {
		// AI gets to operate inside the pathable shrouded area. [8/28/2003]
		TheTerrainLogic->getMaximumPathfindExtent(&extent);
	} else {
		// Human player has to stay within the visible map.
		TheTerrainLogic->getExtent(&extent);
	}

	extent.hi.x -= PATHFIND_CELL_SIZE_F;
	extent.hi.y -= PATHFIND_CELL_SIZE_F;
	extent.lo.x += PATHFIND_CELL_SIZE_F;
	extent.lo.y += PATHFIND_CELL_SIZE_F;
	if (!extent.isInRegionNoZ(dest)) {
		// clamp to in region. [8/28/2003]	
		if (dest->x < extent.lo.x) {
			dest->x = extent.lo.x;
		}
		if (dest->y < extent.lo.y) {
			dest->y = extent.lo.y;
		}
		if (dest->x > extent.hi.x) {
			dest->x = extent.hi.x;
		}
		if (dest->y > extent.hi.y) {
			dest->y = extent.hi.y;
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Internal function for moving a group of infantry as a column.
//

/**
 * Move to given position(s)
 */
Bool AIGroup::friend_moveInfantryToPos( const Coord3D *pos, CommandSourceType cmdSource )

{
	if (m_groundPath==NULL) return false;

	Int numColumns = 3;
	Int halfNumColumns = numColumns/2;
	Real dx, dy;
	Coord3D center;
	if (!getCenter( &center )) return false;

	// Get the start & end vectors for the path.
	Coord3D startPoint = *m_groundPath->getFirstNode()->getPosition();
	Real farEnoughSqr = sqr(PATH_DIAMETER_IN_CELLS*PATHFIND_CELL_SIZE_F);
	PathNode *startNode = NULL;
	PathNode *node;
	for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
		dx = node->getPosition()->x - startPoint.x;	
		dy = node->getPosition()->y - startPoint.y;
		if (dx*dx+dy*dy>farEnoughSqr) {
			startNode = node;
			break;
		}
	}
	Coord3D endPoint = *m_groundPath->getLastNode()->getPosition();
	PathNode *endNode = NULL;		
	for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
		Real dx = node->getPosition()->x - endPoint.x;	
		Real dy = node->getPosition()->y - endPoint.y;
		if (dx*dx+dy*dy>farEnoughSqr) {
			endNode = node;
		}
	}
	if (startNode==NULL || endNode==NULL) {
		m_groundPath->deleteInstance();
		m_groundPath = NULL;
		return false;
	}
	
	Coord2D startVector;
	startVector.x = startNode->getPosition()->x - startPoint.x;
	startVector.y = startNode->getPosition()->y - startPoint.y;
	startVector.normalize();

	Coord2D endVector;
	endVector.x = endPoint.x - endNode->getPosition()->x;
	endVector.y = endPoint.y - endNode->getPosition()->y;
	endVector.normalize();

	Coord2D startVectorNormal;
	startVectorNormal.x = -startVector.y;
	startVectorNormal.y = startVector.x;
	startVectorNormal.normalize();

	Coord2D endVectorNormal;
	endVectorNormal.x = -endVector.y;
	endVectorNormal.y = endVector.x;
	endVectorNormal.normalize();

	Bool useEndVector = false;
	Int unitsToPath = 0;
	// Move.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);
	MemoryPoolObjectHolder iterHolder2;
	SimpleObjectIterator *iter2 = newInstance(SimpleObjectIterator);
	iterHolder2.hold(iter2);
	std::list<Object *>::iterator i;
	PlayerType controllingPlayerType = PLAYER_COMPUTER;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	
	{
		if ((*i)->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		if( !(*i)->isKindOf( KINDOF_INFANTRY ) )
		{	
			continue;
		}
		if( (*i)->getAI()==NULL )
		{	
			continue;
		}
		if ( (*i)->isKindOf( KINDOF_MOB_NEXUS ) )
		{
			return FALSE;// means I did NOT do a column group pathfind, 
			//so the nexus will have a far-away goal position for the mobsters to aim at
		}
		if ((*i)->getControllingPlayer()) {
			controllingPlayerType = (*i)->getControllingPlayer()->getPlayerType();
		}
		Coord3D unitPos = (*i)->getPositionFix()->toCoord3D();	// P5: laid against the float ground path
		TheAI->pathfinder()->removeGoal(*i);
		dx = unitPos.x - center.x;
		dy = unitPos.y - center.y;
		// Sort by the dot product of normal.
		iter->insertFix((*i), fixFromReal(dx*startVectorNormal.x+dy*startVectorNormal.y));	// P5
		unitsToPath++;

		// If units are closer to the end vector than the start vector, use the end vector.
		Real distToEndSqr;
		Real distToStartSqr;
		dx = unitPos.x - endPoint.x;
		dy = unitPos.y - endPoint.y;
		distToEndSqr = dx*dx + dy*dy;
		dx = unitPos.x - startPoint.x;
		dy = unitPos.y - startPoint.y;
		distToStartSqr = dx*dx + dy*dy;
		if (distToStartSqr>distToEndSqr) {
			useEndVector = true;
		}

	}
	if (unitsToPath<TheAI->getAiData()->m_minInfantryForGroup) {
		return false;
	}

	Object *theUnit;
	if (useEndVector) {
		// resort unsing the end vector.
		startVector = endVector;
		startVectorNormal =	endVectorNormal;
		for (theUnit = iter->first(); theUnit; theUnit = iter->next()) iter2->insertFix(theUnit, Fix(0));
		iter->makeEmpty();
		for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
		{
			Coord3D unitPos = theUnit->getPositionFix()->toCoord3D();	// P5
			dx = unitPos.x - center.x;
			dy = unitPos.y - center.y;
			// Sort by the dot product of normal.
			iter->insertFix(theUnit, fixFromReal(dx*startVectorNormal.x+dy*startVectorNormal.y));	// P5
		}
		iter2->makeEmpty();
	}


	iter->sort(ITER_SORTED_FAR_TO_NEAR);
	Int curIndex = 0;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		Int divisor = ((unitsToPath+1)/numColumns);
		if (divisor<1) divisor=1;
		Int columnDelta = 1-(curIndex/divisor);  // 1, 0, -1 from left to right across column. jba.
		if (columnDelta<-halfNumColumns) columnDelta=-halfNumColumns;
		divisor = ((unitsToPath+3)/5);
		if (divisor<1) divisor=1;
		Int fiveColumnDelta = 2-(curIndex/divisor);  // 2, 1, 0, -1, -2 from left to right across column. jba.
		if (fiveColumnDelta<-2) fiveColumnDelta=-2;
		if (unitsToPath<16) {
			fiveColumnDelta = columnDelta;
		}

		ai->setTmpValue( (fiveColumnDelta<<16)|(columnDelta&0x00ffff));
		// Sort next pass by the dot product of start vector.
		dx, dy;
		dx = fixToReal(theUnit->getPositionFix()->x) - center.x;	// P5
		dy = fixToReal(theUnit->getPositionFix()->y) - center.y;
		Int adjust = 0;
		LocomotorPriority movePriority = LOCO_MOVES_FRONT;
		if (ai->getCurLocomotor()) {
			movePriority = ai->getCurLocomotor()->getMovePriority();
			if (movePriority == LOCO_MOVES_MIDDLE) {
				adjust = -100*PATHFIND_CELL_SIZE_F;
			} else if (movePriority == LOCO_MOVES_BACK) {
				adjust = -200*PATHFIND_CELL_SIZE_F;
			}
		}
		iter2->insertFix(theUnit, fixFromReal(adjust + dx*startVector.x + dy*startVector.y));	// P5
		curIndex++;

	}

	iter2->sort(ITER_SORTED_FAR_TO_NEAR);
	// Even out columns by priority.
	Int group;
	Int column3[3] = {0,0,0};	
	Int column5[5] = {0,0,0,0,0};	
	for (group = LOCO_MOVES_FRONT; group>=LOCO_MOVES_BACK; group--) {
		for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
		{
			AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
			Int tmp = ai->getTmpValue();
			LocomotorPriority movePriority = LOCO_MOVES_MIDDLE;
			if (ai->getCurLocomotor()) {
				movePriority = ai->getCurLocomotor()->getMovePriority();
			}
			if (group!=movePriority) continue;
			Int fiveColumnDelta = tmp>>16;
			Int columnDelta = (Short)(tmp & 0xFFFF);

			Int i;
			Int min3 = 10000;
			Int min5 = 10000;
			for (i=0; i<3; i++) if (column3[i]<min3) min3 = column3[i];
			for (i=0; i<5; i++) if (column5[i]<min5) min5 = column5[i];
			Int delta = 10000;
			Int best = -1;
			for (i=0; i<3; i++) {
				if (column3[i]==min3) {
					Int dx = (1+columnDelta)-i;
					if (dx<0) dx = -dx;
					if (dx<delta) {
						delta = dx;
						best = i;
					}
				}
			}
			if (best >= 0) {
				column3[best]++;
				columnDelta = best-1;
			}

			delta = 10000;
			best = -1;
			for (i=0; i<5; i++) {
				if (column5[i]==min5) {
					Int dx = (2+fiveColumnDelta)-i;
					if (dx<0) dx = -dx;
					if (dx<delta) {
						delta = dx;
						best = i;
					}
				}
			}
			if (best >= 0) {
				column5[best]++;
				fiveColumnDelta = best-2;
			}

			if (unitsToPath<16) {
				fiveColumnDelta = columnDelta;
			}
			ai->setTmpValue( (fiveColumnDelta<<16)|(columnDelta&0x00ffff));
		}
	}

	curIndex = 0;
	Int columnFactor[5] = {0,0,0,0,0};
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		Int tmp = ai->getTmpValue();
		Int fiveColumnDelta = tmp>>16;
		Int columnDelta = (Short)(tmp & 0xFFFF);
 		Int factor = columnFactor[fiveColumnDelta+2];
		columnFactor[fiveColumnDelta+2] = factor+1;

		std::vector<Coord3D> path;
		PathNode *node = startNode;
		PathNode *previousNode = m_groundPath->getFirstNode();
		Coord3D prevPos = theUnit->getPositionFix()->toCoord3D();	// P5
		while (node) {
			Coord3D dest = *node->getPosition();
			PathNode *tmpNode;
			PathNode *nextNode=NULL;
			for (tmpNode = node->getNextOptimized(); tmpNode; tmpNode=tmpNode->getNextOptimized()) {
				Real dx = tmpNode->getPosition()->x - dest.x;	
				Real dy = tmpNode->getPosition()->y - dest.y;
				if (dx*dx+dy*dy>farEnoughSqr) {
					nextNode = tmpNode;
					break;
				}
			}
			if (nextNode==NULL) break;
			Coord2D cornerVectorNormal;
			cornerVectorNormal.y = nextNode->getPosition()->x - previousNode->getPosition()->x;
			cornerVectorNormal.x = -(nextNode->getPosition()->y - previousNode->getPosition()->y);
			cornerVectorNormal.normalize();

			Coord2D cornerVector;
			cornerVector.x = nextNode->getPosition()->x - previousNode->getPosition()->x;
			cornerVector.y = nextNode->getPosition()->y - previousNode->getPosition()->y;

			Real offset = PATHFIND_CELL_SIZE_F*2.1f/halfNumColumns;
			dest.x += offset * columnDelta * cornerVectorNormal.x;
			dest.y += offset * columnDelta * cornerVectorNormal.y;
 			if (factor&1) {
				dest.x += 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.x;
				dest.y += 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.y;
			} else {
				dest.x -= 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.x;
				dest.y -= 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.y;
			}

			Coord2D curVector;
			curVector.x = dest.x-prevPos.x;
			curVector.y = dest.y-prevPos.y;

			clampToMap(&dest, controllingPlayerType);
			// Make sure that this dest is going in the same direction as the vector.
			if (cornerVector.x*curVector.x + cornerVector.y*curVector.y > 0) {
				path.push_back( dest );
				prevPos = dest;
			}
			node=node->getNextOptimized();

			for (tmpNode = previousNode->getNextOptimized(); tmpNode && tmpNode!=node; tmpNode=tmpNode->getNextOptimized()) {
				Real dx = tmpNode->getPosition()->x - node->getPosition()->x;	
				Real dy = tmpNode->getPosition()->y - node->getPosition()->y;
				if (dx*dx+dy*dy>farEnoughSqr) {
					previousNode = tmpNode;
				}
			}
		}

		Coord3D dest = *pos;
		if (fiveColumnDelta<-2) fiveColumnDelta=-2;
		if (fiveColumnDelta>2) fiveColumnDelta=2;
		Real offset = PATHFIND_CELL_SIZE_F*2.2f;

		dest.x += offset * fiveColumnDelta * endVectorNormal.x;
		dest.y += offset * fiveColumnDelta * endVectorNormal.y;
		if (factor&1) {
			dest.x += PATHFIND_CELL_SIZE_F * endVectorNormal.x;
			dest.y += PATHFIND_CELL_SIZE_F * endVectorNormal.y;
		}

		LocomotorPriority movePriority = LOCO_MOVES_MIDDLE;
		if (ai->getCurLocomotor()) {
			movePriority = ai->getCurLocomotor()->getMovePriority();
		}
		Int delta = movePriority - LOCO_MOVES_FRONT;
		dest.x += delta*PATHFIND_CELL_SIZE_F*endVector.x;
		dest.y += delta*PATHFIND_CELL_SIZE_F*endVector.y;

		dest.x -= factor*offset*endVector.x;
		dest.y -= factor*offset*endVector.y;
		dest.z = fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( dest.x ), fixFromReal( dest.y ), layer ) );	// P5

		while (path.size()>0) {
			Coord2D curVector;
			prevPos = path[path.size()-1];
			curVector.x = dest.x-prevPos.x;
			curVector.y = dest.y-prevPos.y;

			// Make sure that this dest is going in the same direction as the vector.
			if (endVector.x*curVector.x + endVector.y*curVector.y <= 0) {
				path.pop_back();
			}	else {
				break;
			}
		}
		clampToMap(&dest, controllingPlayerType);
		TheAI->pathfinder()->adjustDestination(theUnit, ai->getLocomotorSet(), &dest, NULL);
		TheAI->pathfinder()->updateGoal(theUnit, &dest, LAYER_GROUND);
		path.push_back(dest);
		ai->aiFollowPath( &path, NULL, cmdSource );
	}
	return true;
}

/**
 * Move to given position(s)
 */
void AIGroup::friend_moveFormationToPos( const Coord3D *pos, CommandSourceType cmdSource )
{
	Real dx, dy;
	Coord3D center;
	if (!getCenter( &center )) return;


	PathNode *startNode = NULL;
	PathNode *endNode = NULL;
	Coord3D endPoint = *pos;
	if (m_groundPath) {	
		// Get the start & end vectors for the path.
		Coord3D startPoint = *m_groundPath->getFirstNode()->getPosition();
		Real farEnoughSqr = sqr(PATH_DIAMETER_IN_CELLS*PATHFIND_CELL_SIZE_F);
		PathNode *node;
		for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
			dx = node->getPosition()->x - startPoint.x;	
			dy = node->getPosition()->y - startPoint.y;
			if (dx*dx+dy*dy>farEnoughSqr) {
				startNode = node;
				break;
			}
		}
		endPoint = *m_groundPath->getLastNode()->getPosition();
		for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
			dx = node->getPosition()->x - endPoint.x;	
			dy = node->getPosition()->y - endPoint.y;
			if (dx*dx+dy*dy>farEnoughSqr) {
				endNode = node;
			}
		}
		PathNode *tmpNode = endNode;
		while (tmpNode) {
			if (tmpNode == startNode) {
				endNode = NULL;
			}
			tmpNode = tmpNode->getNextOptimized();
		}
		if (startNode==NULL || endNode==NULL) {
			m_groundPath->deleteInstance();
			m_groundPath = NULL;
			startNode = NULL;
			endNode = NULL;
		}
	}

	
	// Move.
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	
	{
		if ((*i)->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		Object *theUnit = (*i);
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		// a group can hold things that do not steer themselves - a formation move means nothing to them
		if (ai == NULL)
		{
			continue;
		}

		Bool isDifferentFormation = false;
		Coord2D offset;
		if (isDifferentFormation) {
			const FCoord3D *pos = theUnit->getPositionFix();
			offset.x = fixToReal( pos->x ) - center.x;	// P5: the offset is added to float path points
			offset.y = fixToReal( pos->y ) - center.y;
			theUnit->setFormationOffset(offset);
		}
		theUnit->getFormationOffset(&offset);
		if (startNode) {
			std::vector<Coord3D> path;
			PathNode *node = startNode;
			while (node) {
				Coord3D dest = *node->getPosition();
				dest.x += offset.x;
				dest.y += offset.y;

				path.push_back( dest );
				if (node==endNode) break;
				node=node->getNextOptimized();
			}

			Coord3D dest = endPoint;
			dest.x += offset.x;
			dest.y += offset.y;

			TheAI->pathfinder()->adjustDestination(theUnit, ai->getLocomotorSet(), &dest, NULL);
			TheAI->pathfinder()->updateGoal(theUnit, &dest, LAYER_GROUND);
			path.push_back(dest);
			ai->aiFollowPath( &path, NULL, cmdSource );
		}	else {
			Coord3D dest = endPoint;
			dest.x += offset.x;
			dest.y += offset.y;
			ai->aiMoveToPosition( &dest, cmdSource );
		}

	}
}
//-------------------------------------------------------------------------------------------------
// Internal function for moving a group of vehicles as a column.
//

/**
 * Move to given position(s)
 */
Bool AIGroup::friend_moveVehicleToPos( const Coord3D *pos, CommandSourceType cmdSource )

{

	if (m_groundPath==NULL) return false;

	Real dx, dy;
	Coord3D center;
	if (!getCenter( &center )) return false;

	if (!m_groundPath) {
		return false;
	}

	Int numColumns = 2;


	// Get the start & end vectors for the path.
	Coord3D startPoint = *m_groundPath->getFirstNode()->getPosition();
	Real farEnoughSqr = sqr(PATH_DIAMETER_IN_CELLS*PATHFIND_CELL_SIZE_F);
	PathNode *startNode = NULL;
	PathNode *node;
	for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
		Real dx = node->getPosition()->x - startPoint.x;	
		Real dy = node->getPosition()->y - startPoint.y;
		if (dx*dx+dy*dy>farEnoughSqr) {
			startNode = node;
			break;
		}
	}
	Coord3D endPoint = *m_groundPath->getLastNode()->getPosition();
	PathNode *endNode = NULL;		
	for (node = m_groundPath->getFirstNode(); node; node=node->getNextOptimized()) {
		Real dx = node->getPosition()->x - endPoint.x;	
		Real dy = node->getPosition()->y - endPoint.y;
		if (dx*dx+dy*dy>farEnoughSqr) {
			endNode = node;
		}
	}
	if (endNode == m_groundPath->getFirstNode()) {
		endNode = NULL;
	}
	if (startNode==NULL || endNode==NULL) {
		m_groundPath->deleteInstance();
		m_groundPath = NULL;
		return false;
	}
	
	Coord2D startVector;
	startVector.x = startNode->getPosition()->x - startPoint.x;
	startVector.y = startNode->getPosition()->y - startPoint.y;
	startVector.normalize();

	Coord2D endVector;
	endVector.x = endPoint.x - endNode->getPosition()->x;
	endVector.y = endPoint.y - endNode->getPosition()->y;
	endVector.normalize();

	Coord2D startVectorNormal;
	startVectorNormal.x = -startVector.y;
	startVectorNormal.y = startVector.x;
	startVectorNormal.normalize();

	Coord2D endVectorNormal;
	endVectorNormal.x = -endVector.y;
	endVectorNormal.y = endVector.x;
	endVectorNormal.normalize();

	Int unitsToPath = 0;
	Bool useEndVector = false;
	// Move.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);
	MemoryPoolObjectHolder iterHolder2;
	SimpleObjectIterator *iter2 = newInstance(SimpleObjectIterator);
	iterHolder2.hold(iter2);
	PlayerType controllingPlayerType = PLAYER_COMPUTER;
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	
	{
		if ((*i)->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		if( !(*i)->isKindOf( KINDOF_VEHICLE ) )
		{	
			continue;
		}
		if( (*i)->getAI()==NULL )
		{	
			continue;
		}
		if( !(*i)->getAI()->isDoingGroundMovement() )
		{	
			continue;
		}	 
		if ((*i)->getControllingPlayer()) {
			controllingPlayerType = (*i)->getControllingPlayer()->getPlayerType();
		}
		Coord3D unitPos = (*i)->getPositionFix()->toCoord3D();	// P5: laid against the float ground path
		TheAI->pathfinder()->removeGoal(*i);
		Real dx, dy;
		dx = unitPos.x - center.x;
		dy = unitPos.y - center.y;
		// Sort by the dot product of normal.
		iter->insertFix((*i), fixFromReal(dx*startVectorNormal.x+dy*startVectorNormal.y));	// P5
		unitsToPath++;

		// If units are closer to the end vector than the start vector, use the end vector.
		Real distToEndSqr;
		Real distToStartSqr;
		dx = unitPos.x - endPoint.x;
		dy = unitPos.y - endPoint.y;
		distToEndSqr = dx*dx + dy*dy;
		dx = unitPos.x - startPoint.x;
		dy = unitPos.y - startPoint.y;
		distToStartSqr = dx*dx + dy*dy;
		if (distToStartSqr>distToEndSqr) {
			useEndVector = true;
		}
	}

	if (unitsToPath<TheAI->getAiData()->m_minVehiclesForGroup) {
		return false;
	}

	Object *theUnit;
	if (useEndVector) {
		// resort unsing the end vector.
		startVector = endVector;
		startVectorNormal =	endVectorNormal;
		for (theUnit = iter->first(); theUnit; theUnit = iter->next()) iter2->insertFix(theUnit, Fix(0));
		iter->makeEmpty();
		for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
		{
			Coord3D unitPos = theUnit->getPositionFix()->toCoord3D();	// P5
			dx = unitPos.x - center.x;
			dy = unitPos.y - center.y;
			// Sort by the dot product of normal.
			iter->insertFix(theUnit, fixFromReal(dx*startVectorNormal.x+dy*startVectorNormal.y));	// P5
		}
		iter2->makeEmpty();
	}

	iter->sort(ITER_SORTED_FAR_TO_NEAR);
	Int curIndex = 0;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		Int divisor = ((unitsToPath+1)/numColumns);
		if (divisor<1) divisor=1;
		Int columnDelta = 1-(curIndex/divisor);  // 0, 1 from left to right across column. jba.
		if (columnDelta==0) columnDelta=-1;
		divisor = ((unitsToPath+1)/3);
		if (divisor<1) divisor=1;
		Int threeColumnDelta = (curIndex/divisor);  // 0, 1, 2 from left to right across column. jba.
		threeColumnDelta = 1-threeColumnDelta; // 1, 0, -1
		if (threeColumnDelta<-1) threeColumnDelta=-1;
		if (unitsToPath<5) {
			threeColumnDelta = columnDelta;
		}

		ai->setTmpValue( (threeColumnDelta<<16)|(columnDelta&0x00ffff));
		// Sort next pass by the dot product of start vector.
		Real dx, dy;
		dx = fixToReal(theUnit->getPositionFix()->x) - center.x;	// P5
		dy = fixToReal(theUnit->getPositionFix()->y) - center.y;
		Int adjust = 0;
#if 0
		LocomotorPriority movePriority = LOCO_MOVES_FRONT;
		if (ai->getCurLocomotor()) {
			movePriority = ai->getCurLocomotor()->getMovePriority();
			if (movePriority == LOCO_MOVES_MIDDLE) {
				adjust = -100*PATHFIND_CELL_SIZE_F;
			} else if (movePriority == LOCO_MOVES_BACK) {
				adjust = -200*PATHFIND_CELL_SIZE_F;
			}
		}
#endif 
		iter2->insertFix(theUnit, fixFromReal(adjust + dx*startVector.x + dy*startVector.y));	// P5
		curIndex++;

	}

	iter2->sort(ITER_SORTED_FAR_TO_NEAR);
	// Even out columns by priority.
	Int group;
	Int column2[3] = {0,0,0};	
	Int column3[3] = {0,0,0};	
	for (group = LOCO_MOVES_FRONT; group>=LOCO_MOVES_BACK; group--) {
		for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
		{
			AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
			Int tmp = ai->getTmpValue();
#if 0
			LocomotorPriority movePriority = LOCO_MOVES_MIDDLE;
			if (ai->getCurLocomotor()) {
				movePriority = ai->getCurLocomotor()->getMovePriority();
			}
			if (group!=movePriority) continue;	
#endif 
			Int threeColumnDelta = tmp>>16;
			Int columnDelta = (Short)(tmp & 0xFFFF);

			Int i;
			Int min2 = 10000;
			Int min3 = 10000;
			for (i=0; i<3; i+=2) if (column2[i]<min2) min2 = column2[i];
			for (i=0; i<3; i++) if (column3[i]<min3) min3 = column3[i];
			Int delta = 10000;
			Int best = -1;
			for (i=0; i<3; i+=2) {
				if (column2[i]==min2) {
					Int dx = (1+columnDelta)-i;
					if (dx<0) dx = -dx;
					if (dx<delta) {
						delta = dx;
						best = i;
					}
				}
			}
			if (best >= 0) {
				column2[best]++;
				columnDelta = best-1;
			}

			delta = 10000;
			best = -1;
			for (i=0; i<3; i++) {
				if (column3[i]==min3) {
					Int dx = (1+threeColumnDelta)-i;
					if (dx<0) dx = -dx;
					if (dx<delta) {
						delta = dx;
						best = i;
					}
				}
			}
			if (best >= 0) {
				column3[best]++;
				threeColumnDelta = best-1;
			}

			if (unitsToPath<5) {
				threeColumnDelta = columnDelta;
			}
			ai->setTmpValue( (threeColumnDelta<<16)|(columnDelta&0x00ffff));
		}
	}




	curIndex = 0;
	Int columnFactor[5] = {0,0,0,0,0};
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	for (theUnit = iter2->first(); theUnit; theUnit = iter2->next())
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		Int tmp = ai->getTmpValue();
		Int threeColumnDelta = tmp>>16;
		Int columnDelta = (Short)(tmp & 0xFFFF);
 		Int factor = columnFactor[threeColumnDelta+2];
		columnFactor[threeColumnDelta+2] = factor+1;

		std::vector<Coord3D> path;
		PathNode *node = startNode;
		PathNode *previousNode = m_groundPath->getFirstNode();
		Coord3D prevPos = theUnit->getPositionFix()->toCoord3D();	// P5
		while (node) {
			Coord3D dest = *node->getPosition();
			PathNode *tmpNode;
			PathNode *nextNode=NULL;
			for (tmpNode = node->getNextOptimized(); tmpNode; tmpNode=tmpNode->getNextOptimized()) {
				Real dx = tmpNode->getPosition()->x - dest.x;	
				Real dy = tmpNode->getPosition()->y - dest.y;
				if (dx*dx+dy*dy>farEnoughSqr) {
					nextNode = tmpNode;
					break;
				}
			}
			if (nextNode==NULL) break;
			Coord2D cornerVectorNormal;
			cornerVectorNormal.y = nextNode->getPosition()->x - previousNode->getPosition()->x;
			cornerVectorNormal.x = -(nextNode->getPosition()->y - previousNode->getPosition()->y);
			cornerVectorNormal.normalize();

			Coord2D cornerVector;
			cornerVector.x = nextNode->getPosition()->x - previousNode->getPosition()->x;
			cornerVector.y = nextNode->getPosition()->y - previousNode->getPosition()->y;

			Real offset = PATHFIND_CELL_SIZE_F*1.5f;
			dest.x += offset * columnDelta * cornerVectorNormal.x;
			dest.y += offset * columnDelta * cornerVectorNormal.y;
 			if (factor&1) {
				dest.x += 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.x;
				dest.y += 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.y;
			} else {
				dest.x -= 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.x;
				dest.y -= 0.5f*PATHFIND_CELL_SIZE_F * cornerVectorNormal.y;
			}

			Coord2D curVector;
			curVector.x = dest.x-prevPos.x;
			curVector.y = dest.y-prevPos.y;
			clampToMap(&dest, controllingPlayerType);
			// Make sure that this dest is going in the same direction as the vector.
			if (cornerVector.x*curVector.x + cornerVector.y*curVector.y > 0) {
				path.push_back( dest );
				prevPos = dest;
			}

			node=node->getNextOptimized();

			for (tmpNode = previousNode->getNextOptimized(); tmpNode && tmpNode!=node; tmpNode=tmpNode->getNextOptimized()) {
				Real dx = tmpNode->getPosition()->x - node->getPosition()->x;	
				Real dy = tmpNode->getPosition()->y - node->getPosition()->y;
				if (dx*dx+dy*dy>farEnoughSqr) {
					previousNode = tmpNode;
				}
			}
		}

		Coord3D dest = *pos;
		if (threeColumnDelta<-3) threeColumnDelta=-3;
		if (threeColumnDelta>3) threeColumnDelta=3;
		Real offset = PATHFIND_CELL_SIZE_F*3.2f;
		if (unitsToPath<5) {
			offset = PATHFIND_CELL_SIZE_F*1.5f;
		}
		dest.x += offset * threeColumnDelta * endVectorNormal.x;
		dest.y += offset * threeColumnDelta * endVectorNormal.y;
		if (factor&1) {
			dest.x += PATHFIND_CELL_SIZE_F * endVectorNormal.x;
			dest.y += PATHFIND_CELL_SIZE_F * endVectorNormal.y;
		}
#if 0
		LocomotorPriority movePriority = LOCO_MOVES_MIDDLE;
		if (ai->getCurLocomotor()) {
			movePriority = ai->getCurLocomotor()->getMovePriority();
		}
		Int delta = movePriority - LOCO_MOVES_FRONT;
		dest.x += delta*PATHFIND_CELL_SIZE_F*endVector.x;
		dest.y += delta*PATHFIND_CELL_SIZE_F*endVector.y;
#endif
		dest.x -= factor*offset*endVector.x;
		dest.y -= factor*offset*endVector.y;
		dest.z = fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( dest.x ), fixFromReal( dest.y ), layer ) );	// P5

		while (path.size()>0) {
			Coord2D curVector;
			prevPos = path[path.size()-1];
			curVector.x = dest.x-prevPos.x;
			curVector.y = dest.y-prevPos.y;

			// Make sure that this dest is going in the same direction as the vector.
			if (endVector.x*curVector.x + endVector.y*curVector.y <= 0) {
				path.pop_back();
			}	else {
				break;
			}
		}
		clampToMap(&dest, controllingPlayerType);
		TheAI->pathfinder()->adjustDestination(theUnit, ai->getLocomotorSet(), &dest, NULL);
		TheAI->pathfinder()->updateGoal(theUnit, &dest, LAYER_GROUND);
		path.push_back(dest);
		ai->aiFollowPath( &path, NULL, cmdSource );
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
// AI Command Interface implementation for AIGroup
//
const Int STD_WAYPOINT_CLAMP_MARGIN = ( PATHFIND_CELL_SIZE_F * 4.0f );
const Int STD_AIRCRAFT_EXTRA_MARGIN = ( PATHFIND_CELL_SIZE_F * 10.0f );

void clampWaypointPosition( Coord3D &position, Int margin )
{
	Region3D mapExtent;
	TheTerrainLogic->getExtent(&mapExtent);
  
  // trim some fat off of all sides,
  mapExtent.hi.x -= margin;
  mapExtent.hi.y -= margin;
  mapExtent.lo.x += margin;
  mapExtent.lo.y += margin;

	if ( mapExtent.isInRegionNoZ( &position ) == FALSE )
  {
    if ( position.x > mapExtent.hi.x )
      position.x = mapExtent.hi.x;
    else if ( position.x < mapExtent.lo.x )
      position.x = mapExtent.lo.x;

    if ( position.y > mapExtent.hi.y )
      position.y = mapExtent.hi.y;
    else if ( position.y < mapExtent.lo.y )
      position.y = mapExtent.lo.y;

    position.z = fixToReal( TheTerrainLogic->getGroundHeightFix( fixFromReal( position.x ), fixFromReal( position.y ) ) );	// P5: a float goal
  }
}


/** One member of a group being handed a lane, and where it stands across the direction the group is
		about to travel in.  Only groupMoveToPosition builds these, and only for as long as it takes to
		sort them. */
struct LaneSeed
{
	Object*	obj;
	Real		lat;
};

/** Sort members left to right across the group.  Object id breaks a tie, so two units standing on
		exactly the same line still get the same two lanes on every machine in a network game - the
		positions are identical everywhere but the order they arrive in is not. */
static Bool laneSeedIsLeftOf( const LaneSeed& a, const LaneSeed& b )
{
	if (a.lat != b.lat)
		return a.lat < b.lat;
	return a.obj->getID() < b.obj->getID();
}

/** How many lanes the road under a group actually carries, and where the middle of them sits.

		The count used to come from a fixed reference width - the widest answer the probe can give,
		regardless of the ground - and it was wrong in both directions.  On a lane road it handed a
		dozen tanks a dozen lanes, every one of them wider than the road, and the band then cut every
		one of them back to the same two edges: a group pressed into two files against the verges with
		nothing down the middle.  On open country it stopped at whatever the reference happened to be
		while the field went on.  Two probes at the group's own feet, one each side, cost sixteen cell
		lookups apiece and are taken once per order.

		`bias` comes back as the offset of the middle of the drivable span from the centre line, which
		is what keeps a route running along a wall from spreading into the wall: the room is measured
		separately on the two sides and the lanes are laid out in the room that exists. */
/** Ground members take their goals from one flood out of the clicked cell.  Airborne ones used to
		take the click itself, every one of them, and nothing pushes two hovering helicopters apart:
		six Comanches sent to one point ended up rotor inside rotor.  They are laid out on rings round
		the click instead, one in the middle and six to a ring after that, spaced by the widest body
		among them so the discs clear each other.  The member nearest the click takes the middle and
		the rest fill outwards, so the one already there does not cross the group to reach a ring. */
static void spreadAirborneGoals( const Coord3D& clicked, const std::vector<Object *>& members,
																 std::vector<Coord3D>& goals )
{
	// A helicopter's rotor disc is wider than the body its geometry describes, so the clearance is
	// measured in bodies and taken from the picture: at 2.4 the fuselages cleared and the discs still
	// cut through each other, at 4 six Comanches sit apart.
	const Real AIRBORNE_BODY_CLEARANCE = 4.0f;
	const Int AIRBORNE_SLOTS_PER_RING = 6;

	const Int count = (Int)members.size();
	goals.assign( count, clicked );
	if (count < 2)
		return;

	Fix widest = Fix( 0 );
	for (Int i = 0; i < count; i++)
		widest = fixMax( widest, members[ i ]->getGeometryInfo().getBoundingCircleRadiusFix() );
	const Real spacing = fixToReal( widest ) * AIRBORNE_BODY_CLEARANCE;	// P4: laid out round a float move goal
	if (spacing < 1.0f)
		return;

	// nearest the click first, so the slot in the middle goes to whoever has least distance to give up
	std::vector<LaneSeed> order;
	for (Int i = 0; i < count; i++)
	{
		LaneSeed seed;
		seed.obj = members[ i ];
		const FCoord3D *at = members[ i ]->getPositionFix();
		// squared: this is only ever sorted on, and the order is the same either way
		seed.lat = sqr( fixToReal( at->x ) - clicked.x ) + sqr( fixToReal( at->y ) - clicked.y );	// P4
		order.push_back( seed );
	}
	std::sort( order.begin(), order.end(), laneSeedIsLeftOf );

	for (Int placed = 0; placed < count; placed++)
	{
		Int ring = 0;
		Int taken = 1;												// the middle slot
		Int firstOfRing = 0;
		while (placed >= taken)
		{
			++ring;
			firstOfRing = taken;
			taken += AIRBORNE_SLOTS_PER_RING * ring;
		}

		Coord3D goal = clicked;
		if (ring > 0)
		{
			const Int slots = AIRBORNE_SLOTS_PER_RING * ring;
			const Real angle = 2.0f * PI * (Real)(placed - firstOfRing) / (Real)slots;
			goal.x += Cos( angle ) * spacing * (Real)ring;
			goal.y += Sin( angle ) * spacing * (Real)ring;
		}

		for (Int i = 0; i < count; i++)
		{
			if (members[ i ] == order[ placed ].obj)
			{
				goals[ i ] = goal;
				break;
			}
		}
	}
}

static Int crowdRoadLanes( Object *probe, const Coord3D& center, const Coord2D& dir, Real spacing,
													 Real *bias )
{
	Coord3D from = center;
	from.z = fixToReal( TheTerrainLogic->getGroundHeightFix( fixFromReal( from.x ), fixFromReal( from.y ) ) );	// P5: the lane probe takes a float point

	Coord2D left, right;
	left.x = -dir.y;	left.y = dir.x;
	right.x = dir.y;	right.y = -dir.x;

	Pathfinder *pf = TheAI->pathfinder();
	const LocomotorSet& locoSet = probe->getAIUpdateInterface()->getLocomotorSet();
	const PathfindLayerEnum layer = probe->getLayer();
	const Real leftRoom = pf->laneExtent( probe, locoSet, layer, &from, &left );
	const Real rightRoom = pf->laneExtent( probe, locoSet, layer, &from, &right );

	*bias = (leftRoom - rightRoom) * 0.5f;

	// the cap is CROWD_MAX_LANES and it is not the ground's opinion: see the comment on it
	return Crowd_laneCount( leftRoom + rightRoom, spacing, 0 );
}

/** Take a member out of the crowd march.  A slot survives a repath - that is the whole point of it,
		or a group that repaths mid-march loses its formation for the rest of the trip - so the only
		thing that ends one is the next order, and this is where an order that hands out no lanes says
		so.  Without it a unit ordered somewhere on its own would still be riding a lane it was given
		as part of a group that no longer exists. */
static void crowdClearLanes( std::list<Object *>& members )
{
	for (std::list<Object *>::iterator it = members.begin(); it != members.end(); ++it)
	{
		AIUpdateInterface *ai = (*it)->getAIUpdateInterface();
		if (ai != NULL)
			ai->clearCrowdLane();
	}
}

static const Int CROWD_TRUNK_DIAMETER = 6;			///< the widest the group's route is searched at, in cells
static const Int CROWD_PLAN_STEPS = 4;					///< a lane closes to the group's line in quarters

/** One member's lane of the group's route: every corner of `trunk` moved `offset` to its left,
		and then the member's own spot at the end.

		The corner moves along the bisector of the two legs that meet there, as far as keeps both legs
		`offset` from the group's, so the lane runs parallel to the route through the turn and the
		outside lane goes round the outside of it.  Corners behind the member are skipped, so the
		front of the group does not drive back to the start.  Returns FALSE when the lane cannot be
		laid, and the member searches for its own path.

		Where the whole offset does not fit - a wall, a building - the lane closes towards the group's
		line in quarters, and it closes over three corners, never one: each corner is held to the
		narrowest of itself and its two neighbours.  Measured corner by corner alone, a lane down a
		street took the full offset, half, the full offset again and none, one after the other, and a
		tank steered down that zigzag slows for every kink in it: the town crossing arrived 7% later on
		average for that alone. */
static Bool crowdPlanLane( Object *unit, const CrowdRoute& trunk, Real offset, const Coord3D& goal, CrowdRoute *out )
{
	out->clear();

	AIUpdateInterface *ai = unit->getAIUpdateInterface();
	const LocomotorSet& loco = ai->getLocomotorSet();
	const LocomotorSurfaceTypeMask surfaces = loco.getValidSurfaces();
	const Bool crusher = unit->getCrusherLevel() > 0;
	Pathfinder *pf = TheAI->pathfinder();

	//--- how far each corner can move out on its own ------------------------------------------------
	const Int corners = (Int)trunk.size();
	std::vector<Coord2D> shifts( corners );
	std::vector<Int> fits( corners, CROWD_PLAN_STEPS );
	for (Int k = 1; k + 1 < corners; k++)
	{
		const CrowdRoutePoint& a = trunk[ k - 1 ];
		const CrowdRoutePoint& c = trunk[ k ];
		const CrowdRoutePoint& b = trunk[ k + 1 ];
		fits[ k ] = 0;
		shifts[ k ].x = 0.0f;
		shifts[ k ].y = 0.0f;

		// a corner beside a deck edge stays on the group's line: see the chain below
		if (a.layer != c.layer || b.layer != c.layer)
			continue;
		if (!Crowd_laneCorner( a.pos, c.pos, b.pos, offset, &shifts[ k ] ))
			continue;

		for (Int q = CROWD_PLAN_STEPS; q > 0; q--)
		{
			Coord3D p = c.pos;
			p.x += shifts[ k ].x * (Real)q / (Real)CROWD_PLAN_STEPS;
			p.y += shifts[ k ].y * (Real)q / (Real)CROWD_PLAN_STEPS;
			p.z = fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( p.x ), fixFromReal( p.y ), c.layer ) );	// P5
			if (pf->validMovementPosition( crusher, c.layer, loco, &p )
						&& pf->isLinePassable( unit, surfaces, c.layer, c.pos, p, false, true ))
			{
				fits[ k ] = q;
				break;
			}
		}
	}

	//--- and no wider at a corner than at either neighbour ------------------------------------------
	std::vector<Int> shares( fits );
	for (Int k = 1; k + 1 < corners; k++)
	{
		if (fits[ k - 1 ] < shares[ k ]) shares[ k ] = fits[ k - 1 ];
		if (fits[ k + 1 ] < shares[ k ]) shares[ k ] = fits[ k + 1 ];
	}

	//--- then the lane itself, leg by leg from where the member stands -------------------------------
	Coord3D prev = unit->getPositionFix()->toCoord3D();	// P5: the lane is a float route
	PathfindLayerEnum prevLayer = unit->getLayer();
	Int prevCorner = -1;				// which of the group's corners prev was laid off, if any
	for (Int k = 1; k + 1 < corners; k++)
	{
		const CrowdRoutePoint& a = trunk[ k - 1 ];
		const CrowdRoutePoint& c = trunk[ k ];

		Coord2D in;
		in.x = c.pos.x - a.pos.x;
		in.y = c.pos.y - a.pos.y;
		if ((c.pos.x - prev.x) * in.x + (c.pos.y - prev.y) * in.y <= 0.0f)
			continue;						// already past it

		/* A leg that changes deck is driven as the group drives it or not at all.  Nothing checks a
			 straight line from one deck to another, and a lane leaving the approach a body width off the
			 group's line reaches the abutment across the riverbank beside it. */
		const Bool changesDeck = prevLayer != c.layer;
		Bool placed = FALSE;
		for (Int q = changesDeck ? 0 : shares[ k ]; q >= 0 && !placed; q--)
		{
			CrowdRoutePoint p;
			p.layer = c.layer;
			p.pos = c.pos;
			p.pos.x += shifts[ k ].x * (Real)q / (Real)CROWD_PLAN_STEPS;
			p.pos.y += shifts[ k ].y * (Real)q / (Real)CROWD_PLAN_STEPS;
			p.pos.z = fixToReal( TheTerrainLogic->getLayerHeightFix( fixFromReal( p.pos.x ), fixFromReal( p.pos.y ), p.layer ) );	// P5

			// the group's own leg, corner to corner, is the wide search's answer and is not asked again:
			// that search measures clearance by diameter, and a straight-line test by body can refuse it
			const Bool groupLeg = q == 0 && prev.x == a.pos.x && prev.y == a.pos.y;
			if (!groupLeg && (changesDeck
						|| !pf->isLinePassable( unit, surfaces, p.layer, prev, p.pos, false, true )))
				continue;
			out->push_back( p );
			prev = p.pos;
			prevLayer = p.layer;
			prevCorner = k;
			placed = TRUE;
		}

		/* Nothing on this corner can be driven to from where the lane is, so the lane closes up to
			 the group's previous corner first and takes this one from there - the outside lane at a gate
			 between two fences, which it goes through behind the others and opens out again after.
			 Stepping back in to the corner the lane was laid off is the leg the first pass already
			 measured out from it, so it is not asked again; from anywhere else it is. */
		if (!placed && a.layer == prevLayer
					&& (prevCorner == k - 1 || pf->isLinePassable( unit, surfaces, a.layer, prev, a.pos, false, true )))
		{
			out->push_back( a );
			out->push_back( c );
			prev = c.pos;
			prevLayer = c.layer;
			prevCorner = k;
			placed = TRUE;
		}
		if (!placed)
			return FALSE;
	}

	/* The route runs on to the click, and a member whose own spot is short of the click, or off to
		 one side of it, stops at the corner nearest its spot: driving the lane to its end and then back
		 sent a tank two hundred feet past its place and round again. */
	while (out->size() >= 2)
	{
		const Coord3D& last = out->back().pos;
		const Coord3D& before = (*out)[ out->size() - 2 ].pos;
		const Real lastSqr = (last.x - goal.x) * (last.x - goal.x) + (last.y - goal.y) * (last.y - goal.y);
		const Real beforeSqr = (before.x - goal.x) * (before.x - goal.x) + (before.y - goal.y) * (before.y - goal.y);
		if (beforeSqr >= lastSqr)
			break;
		out->pop_back();
	}

	// home: the member's own spot round the click, from the last corner it can see it from
	const PathfindLayerEnum goalLayer = TheTerrainLogic->getLayerForDestination( &goal );
	while (!out->empty() && (out->back().layer != goalLayer
				|| !pf->isLinePassable( unit, surfaces, goalLayer, out->back().pos, goal, false, true )))
		out->pop_back();
	if (out->empty())
		return FALSE;

	CrowdRoutePoint home;
	home.pos = goal;
	home.layer = goalLayer;
	out->push_back( home );
	return TRUE;
}

/** Hand every member of a group the distance it should sit off the centre of the road, for the
		crowd model only.

		groupMoveToPosition does the same arithmetic inline because it also has to feed the older
		lane share (setPendingLane).  Everything else that marches a group - tighten, attack-move,
		and the waypoint follow a wave actually leaves on - comes through here.  A unit with no lane
		is a unit crowdSteer returns out of on its first line. */
static void crowdSeedLanes( std::list<Object *>& members, const Coord3D& center, const Coord3D *pos )
{
	crowdClearLanes( members );		// whatever this order hands out replaces the last one entirely

	Coord2D dir;
	dir.x = pos->x - center.x;
	dir.y = pos->y - center.y;
	if (dir.length() <= 1.0f)
		return;					// a group already standing on its destination has no direction to spread across
	dir.normalize();

	std::vector<LaneSeed> across;
	Real spacing = 0.0f;
	Object *widest = NULL;
	for (std::list<Object *>::iterator it = members.begin(); it != members.end(); ++it)
	{
		Object *o = *it;
		if (o->getAIUpdateInterface() == NULL || o->isKindOf( KINDOF_IMMOBILE ))
			continue;

		LaneSeed seed;
		seed.obj = o;
		const FCoord3D *at = o->getPositionFix();
		seed.lat = (fixToReal( at->x ) - center.x) * -dir.y		// P4: lanes are float offsets handed to AIUpdate
						 + (fixToReal( at->y ) - center.y) * dir.x;
		across.push_back( seed );

		const Real body = fixToReal( 2 * o->getGeometryInfo().getBoundingCircleRadiusFix() );	// P4
		if (body > spacing)
		{
			spacing = body;
			widest = o;			// the road is measured for the biggest body in the group, not an average one
		}
	}

	spacing *= 1.15f;

	const Int count = (Int)across.size();
	if (count < 2 || spacing < 0.001f)
		return;

	std::sort( across.begin(), across.end(), laneSeedIsLeftOf );

	Real bias = 0.0f;
	Int lanes = crowdRoadLanes( widest, center, dir, spacing, &bias );
	if (lanes > count) lanes = count;		// never more lanes than bodies to put in them

	for (Int i = 0; i < count; i++)
	{
		const Int lane = (i * lanes) / count;
		const Real offset = bias + ((Real)lane - (Real)(lanes - 1) * 0.5f) * spacing;
		across[i].obj->getAIUpdateInterface()->setPendingCrowdLat( offset );
		across[i].obj->getAIUpdateInterface()->setPendingCrowdLane( i, count, spacing );
	}

	if (TheGlobalData->m_showLanes)
		DEBUG_LOG(("SHOWLANES crowdSeedLanes: members=%d lanes=%d spacing=%.1f bias=%.1f\n",
			count, lanes, spacing, bias));
}

/** Where a waypoint-follow should spread across.  doWaves parks the team, then asks for the closest
		point on the approach path, which is often under their feet: crowdSeedLanes then sees no
		direction and hands nobody a lane.  The first link is the next point they will actually drive
		at. */
static const Coord3D *crowdWaypointAim( const Coord3D& center, const Waypoint *way )
{
	if (way == NULL)
		return NULL;
	const Coord3D *pos = way->getLocation();
	Coord2D d;
	d.x = pos->x - center.x;
	d.y = pos->y - center.y;
	if (d.length() > 1.0f)
		return pos;
	if (way->getNumLinks() > 0)
	{
		const Waypoint *next = way->getLink( 0 );
		if (next != NULL)
			return next->getLocation();
	}
	return pos;
}

static void crowdSeedLanesAlongWaypoint( AIGroup *group, std::list<Object *>& members, const Waypoint *way )
{
	if (group == NULL || way == NULL)
		return;
	Coord2D min, max;
	Coord3D center;
	group->getMinMaxAndCenter( &min, &max, &center );
	crowdSeedLanes( members, center, crowdWaypointAim( center, way ) );
}

/**
 * Move to given position(s)
 */
void AIGroup::groupMoveToPosition( const Coord3D *p_posIn, Bool addWaypoint, CommandSourceType cmdSource )
{

  Coord3D position = *p_posIn;
  Coord3D *pos = &position;

	Bool didInfantry = false;
	Bool didVehicles = false;
	// compute current centroid of the team
	Coord3D center;
	Coord2D min;
	Coord2D max;
	Coord3D dest;
	Bool tightenGroup = FALSE;

	Bool isFormation = getMinMaxAndCenter( &min, &max, &center );
	// a queued waypoint moves the members by their own offsets, which holds the shape anyway, so the
	// formation the player made survives it instead of being dropped by the first alt-click
	const Bool keepFormation = addWaypoint && isFormation;
	if (addWaypoint)
  {
    isFormation = false;
  }

	//
	// A player's move order means "everybody go there", and with a big selection the group
	// machinery was answering a different question. friend_moveInfantryToPos walks the group down
	// one shared path in three or five columns, and computeIndividualDestination keeps each
	// member's offset from the group centroid (normalized to a fixed radius, so a wide selection
	// lands strung out on an arc): two hundred units asked to attack-move drew a line instead of
	// arriving, and with three hundred most of them never got a workable goal cell at all.
	//
	// So the point itself is the goal for every member. Pathfinder::floodGroupGoals hands out the
	// free cells round it, the middle to whoever gets there first and the edge to the slow, and stops
	// at a cliff; a member it cannot place falls back to its move state's own spiral. They pile onto
	// the spot instead of holding a shape. Explicit formations - the ones the player asked for by name - still keep
	// theirs, and the AI still moves its teams the old way.
	//
	// A queued waypoint (alt-click) is the exception, and it has to be: the whole thing rests on the
	// move state finding a free cell near the spot, and AIFollowPathState only adjusts the *last*
	// point of the path - every point before it is driven at exactly as given
	// (setAdjustsDestination(false), AIStates.cpp:3398). Handing a dozen tanks the same intermediate
	// cell means eleven of them never arrive at it and the queue stops there, which is a group that
	// walks to the first waypoint and stands. So a queued point keeps each member's own offset.
	const Bool gatherOnPoint = ( cmdSource == CMD_FROM_PLAYER && !isFormation && !addWaypoint );

	/* The group path and the orders that ride on it, timed: it is one search plus a move order for
		 every member, all of it inside the one logic frame the order arrives on, and a skirmish AI
		 team taking an approach path is the slowest frame left in a four-player match. */
#ifdef DEBUG_LOGGING
	Int64 corridorStart, corridorEnd, corridorFreq;
	QueryPerformanceCounter( (LARGE_INTEGER *)&corridorStart );
#endif
	if (!addWaypoint && !isFormation && !gatherOnPoint) {
		friend_computeGroundPath(pos, cmdSource);
		didInfantry = friend_moveInfantryToPos(pos, cmdSource);
		didVehicles = friend_moveVehicleToPos(pos, cmdSource);
	}
#ifdef DEBUG_LOGGING
	QueryPerformanceCounter( (LARGE_INTEGER *)&corridorEnd );
	QueryPerformanceFrequency( (LARGE_INTEGER *)&corridorFreq );
	const Real corridorMS = corridorFreq > 0
		? (Real)((double)(corridorEnd - corridorStart) * 1000.0 / (double)corridorFreq) : 0.0f;
#endif
	if (m_dirty)
		recompute();

	std::list<Object *>::iterator i;
	if( !isFormation && !gatherOnPoint && cmdSource == CMD_FROM_PLAYER && TheGlobalData->m_groupMoveClickToGatherFactor > 0.0f )
	{
		ScaleRect2D( &min, &max, TheGlobalData->m_groupMoveClickToGatherFactor );

		if( Coord3DInsideRect2D( pos, &min, &max ) )
		{
			tightenGroup = TRUE;
		}
	}

  Real extraMargin = 0.0f;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	
	{
    const Object *groupMember = (*i);

    if ( groupMember->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )//helicopter
    {
      isFormation = FALSE;
      extraMargin = MAX( extraMargin, fixToReal( groupMember->getGeometryInfo().getMajorRadiusFix() ) );	// P5: a float waypoint margin
    }
    else if ( groupMember->isKindOf( KINDOF_AIRCRAFT ) )// fixed wing aircraft only
    {
			if ( groupMember->getAI() && groupMember->getAI()->isDoingGroundMovement() == FALSE ) //if unit is airborne
      {
				tightenGroup = FALSE;	// Don't tighten aircraft.  It is a bad idea. jba.
				isFormation = FALSE;//then keep spread formation after move
      }

      extraMargin = MAX( extraMargin, STD_AIRCRAFT_EXTRA_MARGIN );
		}
	} 
  
  Int margin = STD_WAYPOINT_CLAMP_MARGIN + extraMargin;
  clampWaypointPosition( position, margin );

  


	if (tightenGroup)
	{
		isFormation = false;
		if (!addWaypoint) {
			Int dx = (max.x-min.x)/PATHFIND_CELL_SIZE_F;
			Int dy = (max.x-min.x)/PATHFIND_CELL_SIZE_F;	// retail typo kept on purpose: fixing it gathers groups far more often, which players read as wrong paths
			Int cells = (dx*dy);
			if (cells<2000) {
				groupTightenToPosition(pos, false, cmdSource);
				return;
			}
		}
	}

	if (isFormation) {
		friend_computeGroundPath(pos, cmdSource);
		friend_moveFormationToPos(pos, cmdSource);
		return;
	}

	// Move.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);
	const Fix goalX = fixFromReal( pos->x ), goalY = fixFromReal( pos->y );	// P5: the ordered point
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Fix dx, dy;
		if ((*i)->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		if( (*i)->isKindOf( KINDOF_IMMOBILE ) )
		{	
			continue;
		}
		if( (*i)->getAI()==NULL )
		{	
			continue;
		}
		if ((*i)->isKindOf(KINDOF_INFANTRY) && didInfantry) {
			continue;
		}
		if ((*i)->isKindOf(KINDOF_VEHICLE) && didVehicles) 
		{
			if( (*i)->getAI()->isDoingGroundMovement() )
			{	
				Object *obj = (*i);
				if( !obj->isKindOf( KINDOF_CLIFF_JUMPER ) )
				{
					//Not a cliff-jumper-offer unit.
					continue;
				}
			}	 
		}
		const FCoord3D *unitPos = (*i)->getPositionFix();
		TheAI->pathfinder()->removeGoal(*i);
		dx = unitPos->x - goalX;
		dy = unitPos->y - goalY;
		// adjust so units are sorted first by move priority.
		Fix adjust = Fix(0);
#if 0	 // Nope.  jba.
		LocomotorPriority movePriority = LOCO_MOVES_FRONT;
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai->getCurLocomotor()) {
			movePriority = ai->getCurLocomotor()->getMovePriority();
			if (movePriority == LOCO_MOVES_MIDDLE) {
				adjust = 100*100*PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F;
			} else if (movePriority == LOCO_MOVES_BACK) {
				adjust = 200*200*PATHFIND_CELL_SIZE_F*PATHFIND_CELL_SIZE_F;
			}
		}
#endif 
		iter->insertFix((*i), adjust + dx*dx+dy*dy);
	}

	Coord3D goalPos = *pos;
	iter->sort(ITER_SORTED_NEAR_TO_FAR);

	/* Where each member rides across its route.  Every unit is handed its own route starting under
		 its own tracks, so measured against that route each of them is dead centre and none of them
		 has any reason to prefer a side - which is how a selection ends up in single file.  The order
		 the members sit in across the group is the only shape there is at this point, and it is the
		 shape the player just drew a box round.

		 The lane is a distance, not a share of the group.  The first version divided each member's
		 offset by the group's own half width, and a selection standing in a tight blob has almost no
		 half width, so twenty-five tanks were spread across thirty feet - less than two of them.  Now
		 members are sorted across the direction of travel and spaced by the size of the largest body
		 in the group, which is the only spacing that means anything to a tank.

		 The band cannot hold an arbitrary number of them, so the count of lanes is capped at what the
		 widest possible band fits and the rest of the group queues behind: members are cut into that
		 many blocks in order, so whoever was on the left is still on the left and the ones sharing a
		 lane simply follow each other.  A narrow road squeezes all of it - laneOffset scales by the
		 room actually measured, so half a band is half the offset, and a doorway is single file
		 again without anybody deciding to queue. */
	Coord2D groupDir;
	groupDir.x = pos->x - center.x;
	groupDir.y = pos->y - center.y;
	const Coord3D groupCenter = center;
	Bool spreadLanes = groupDir.length() > 1.0f;
	crowdClearLanes( m_memberList );	// this order replaces the last one, spread or no spread

	// the group's one route and every member's place across it, for crowdPlanLane below
	CrowdRoute trunkRoute;
	std::vector<Object *> laneMembers;
	std::vector<Real> laneOffsets;
	Real laneSpacing = 0.0f;
	if (spreadLanes)
	{
		groupDir.normalize();

		std::vector<LaneSeed> across;
		Real spacing = 0.0f;
		Object *widest = NULL;
		for (Object *o = iter->first(); o; o = iter->next())
		{
			if (o->getAIUpdateInterface() == NULL)
				continue;

			LaneSeed seed;
			seed.obj = o;
			const FCoord3D *at = o->getPositionFix();
			seed.lat = (fixToReal( at->x ) - groupCenter.x) * -groupDir.y		// P4: lanes are float offsets handed to AIUpdate
							 + (fixToReal( at->y ) - groupCenter.y) * groupDir.x;
			across.push_back( seed );

			Real body = fixToReal( 2 * o->getGeometryInfo().getBoundingCircleRadiusFix() );	// P4
			if (body > spacing)
			{
				spacing = body;
				widest = o;
			}
		}

		// a little air between bodies, or the lanes are exactly touching and the collision push
		// undoes the spread the frame it is applied
		spacing *= 1.15f;

		Int count = across.size();
		if (count > 1 && spacing > 0.001f)
		{
			std::sort( across.begin(), across.end(), laneSeedIsLeftOf );

			// the lane count is the road's, measured here
			Real bias = 0.0f;
			Int lanes = crowdRoadLanes( widest, groupCenter, groupDir, spacing, &bias );
			if (lanes > count) lanes = count;

			for (Int i = 0; i < count; i++)
			{
				// blocks, not round-robin: whoever was left of somebody stays left of them, and a
				// group with more members than lanes puts the extra ones behind rather than beside
				Int lane = (i * lanes) / count;
				Real offset = bias + ((Real)lane - (Real)(lanes - 1) * 0.5f) * spacing;
				Real u = Pathfinder_groupLane( offset );
				across[i].obj->getAIUpdateInterface()->setPendingLane( u );

				/* The crowd model takes the offset as it stands.  The old model had to turn it into a
					 share of a width guessed here and re-measured somewhere else, and the two never agreed:
					 the conversion is where the spacing this loop just worked out was thrown away. */
				across[i].obj->getAIUpdateInterface()->setPendingCrowdLat( offset );
				/* And the slot the offset came from, which is the part that survives.  The offset is
					 measured here, once, on the ground the group happens to be standing on; the slot is
					 re-fitted to the band every frame, so the group is as many abreast as the road under
					 it carries rather than as many as its car park did. */
				across[i].obj->getAIUpdateInterface()->setPendingCrowdLane( i, count, spacing );

				// centred on the group's route, which its wide search already put in the middle of the road
				laneMembers.push_back( across[i].obj );
				laneOffsets.push_back( ((Real)lane - (Real)(lanes - 1) * 0.5f) * spacing );
				laneSpacing = spacing;

				if (TheGlobalData->m_showLanes)
				{
					DEBUG_LOG(("SHOWLANES   unit %d lat=%.1f lane %d/%d offset=%.1f u=%.2f\n",
						across[i].obj->getID(), across[i].lat, lane, lanes, offset, u));
				}
			}
		}
		else
		{
			spreadLanes = FALSE;
		}

		/* The overlay draws what happened on the ground; this says whether this function is where it
			 happened at all.  A player order that never reaches here hands nobody a lane, and from the
			 camera that looks exactly like an order that reached here and was refused. */
		if (TheGlobalData->m_showLanes)
		{
			DEBUG_LOG(("SHOWLANES groupMoveToPosition: spread=%d members=%d spacing=%.1f\n",
				spreadLanes ? 1 : 0, count, spacing));
		}
	}

	/* A player's gather takes every ground member's goal from one flood out of the clicked cell, so a
		 click on the lip of a cliff keeps the group on top of it (Pathfinder::floodGroupGoals). The
		 members go in iterator order, which is the order the loop below walks, so it reads them back
		 with a cursor. */
	std::vector<Object *> floodMembers;
	std::vector<Coord3D> floodGoals;
	std::vector<Object *> airMembers;
	std::vector<Coord3D> airGoals;
	if (gatherOnPoint)
	{
		for (Object *o = iter->first(); o; o = iter->next())
		{
			if (o->getAIUpdateInterface()->isDoingGroundMovement())
				floodMembers.push_back( o );
			else
				airMembers.push_back( o );
		}
		TheAI->pathfinder()->floodGroupGoals( &goalPos, floodMembers, floodGoals );
		spreadAirborneGoals( goalPos, airMembers, airGoals );

		/* The route every vehicle's lane is laid across: see setPlannedCrowdRoute.  Retail's group
			 trunk, the wide search that keeps a column clear of walls, from the middle of the group,
			 where the lane offsets were measured.  Narrower when that finds nothing, and aimed at the
			 first free cell the flood handed out when the click itself is on a building. */
		if (!laneMembers.empty())
		{
			// from the member standing nearest the middle: the middle itself can be a cliff edge or a
			// rock, which the search starts from as ground nothing else is connected to
			Object *trunkUnit = NULL;
			Real bestSqr = 1.0e30f;
			for (Int m = 0; m < (Int)laneMembers.size(); m++)
			{
				const Coord3D p = laneMembers[m]->getPositionFix()->toCoord3D();	// P5: measured against the float trunk start
				const Real d = (p.x - groupCenter.x) * (p.x - groupCenter.x) + (p.y - groupCenter.y) * (p.y - groupCenter.y);
				if (d < bestSqr && !laneMembers[m]->isKindOf( KINDOF_INFANTRY ))
				{
					bestSqr = d;
					trunkUnit = laneMembers[m];
				}
			}
			const Coord3D trunkStart = (trunkUnit != NULL) ? trunkUnit->getPositionFix()->toCoord3D() : groupCenter;	// P5

			Path *trunk = NULL;
			for (Int attempt = 0; attempt < 2 && trunk == NULL; attempt++)
			{
				const Coord3D *aim = (attempt == 0) ? pos : (floodGoals.empty() ? NULL : &floodGoals[0]);
				if (aim == NULL)
					break;
				for (Int diameter = CROWD_TRUNK_DIAMETER; diameter >= 2 && trunk == NULL; diameter -= 2)
					trunk = TheAI->pathfinder()->findGroundPath( &trunkStart, aim, diameter, false );
			}
			if (trunk != NULL)
			{
				Crowd_routeFromPath( trunk, &trunkRoute );
				trunk->deleteInstance();

				/* Not over a bridge.  Every lane has to close to the group's line to get onto a deck, and
					 closing them all at the corner before it sends the whole group at one point at once:
					 twenty Crusaders over the Kandahar bridge on Golden Oasis took 2.3 times the blocked
					 frames their own paths do, and one of them never got across. */
				for (Int k = 0; k < (Int)trunkRoute.size(); k++)
				{
					if (trunkRoute[k].layer > LAYER_GROUND)
					{
						trunkRoute.clear();
						break;
					}
				}
			}
			if (TheGlobalData->m_showLanes)
			{
				Real routeLen = 0.0f;
				for (Int k = 1; k < (Int)trunkRoute.size(); k++)
				{
					const Real dx = trunkRoute[k].pos.x - trunkRoute[k-1].pos.x;
					const Real dy = trunkRoute[k].pos.y - trunkRoute[k-1].pos.y;
					routeLen += (Real)sqrt( dx * dx + dy * dy );
				}
				const Real sx = pos->x - trunkStart.x, sy = pos->y - trunkStart.y;
				DEBUG_LOG(("SHOWLANES group route: %d corners, %.0f long against %.0f straight\n", (Int)trunkRoute.size(),
					routeLen, (Real)sqrt( sx * sx + sy * sy )));
			}
		}
	}
	Int floodCursor = 0;
	Int airCursor = 0;

	// through the tunnel network when that is shorter; see TunnelTracker::findTunnelShortcut
	Object *tunnelEntrance = NULL;
	if (gatherOnPoint && iter->first() != NULL)
	{
		const Real walkX = goalPos.x - groupCenter.x;
		const Real walkY = goalPos.y - groupCenter.y;
		tunnelEntrance = iter->first()->getControllingPlayer()->getTunnelSystem()->findTunnelShortcut( &groupCenter, &goalPos,
			(Real)sqrt( walkX * walkX + walkY * walkY ) );
	}

	// Works better if you let the near units get the first paths... jba.
	// Move the ones nearest the goal first.  Reduces collision problems later.
	Object *theUnit;
	Bool firstUnit = true;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		if (!keepFormation)
			theUnit->setFormationID(NO_FORMATION_ID);
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();

		if (firstUnit) {
			if (isFormation) {
				Coord2D v;
				theUnit->getFormationOffset(&v);
				goalPos.x -= v.x;
				goalPos.y -= v.y;
			}	else {
				center = theUnit->getPositionFix()->toCoord3D();	// P5
			}
			firstUnit = false;
		}
		if (gatherOnPoint)
		{
			dest = goalPos;
			if (floodCursor < (Int)floodMembers.size() && floodMembers[floodCursor] == theUnit)
				dest = floodGoals[floodCursor++];
			else if (airCursor < (Int)airMembers.size() && airMembers[airCursor] == theUnit)
				dest = airGoals[airCursor++];

			if (tunnelEntrance != NULL && ai->takeTunnelTrip( tunnelEntrance, &dest, TUNNEL_TRIP_MOVE, cmdSource ))
			{
				ai->clearCrowdLane();
				continue;
			}

			// a vehicle drives its lane of the group's route; a foot soldier walks through the crowd
			if (!trunkRoute.empty() && !theUnit->isKindOf( KINDOF_INFANTRY ) && ai->isDoingGroundMovement())
			{
				for (Int m = 0; m < (Int)laneMembers.size(); m++)
				{
					if (laneMembers[m] != theUnit)
						continue;
					CrowdRoute lane;
					const Bool laid = crowdPlanLane( theUnit, trunkRoute, laneOffsets[m], dest, &lane );
					if (TheGlobalData->m_showLanes)
						DEBUG_LOG(("SHOWLANES plan: unit %d offset %.1f %s, %d corners of %d\n", theUnit->getID(),
							laneOffsets[m], laid ? "laid" : "refused", (Int)lane.size(), (Int)trunkRoute.size()));
					if (laid)
					{
						ai->setPlannedCrowdRoute( lane );
						// the lane is the path itself now, so the band rides its centre and nothing re-fits it
						ai->setPendingCrowdLat( 0.0f );
						ai->setPendingCrowdLane( 0, 1, laneSpacing );
					}
					break;
				}
			}
		}
		else
			computeIndividualDestination( &dest, &goalPos, theUnit, &center, isFormation );

		if( cmdSource == CMD_FROM_PLAYER && theUnit->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH ) && ai->canAutoAcquire() )
		{
			//When ordering a combat stealth unit to move, there is a single special case we want to handle.
			//When a stealth unit is currently not stealthed and doesn't autoacquire while stealthed,
			//then when the player specifically orders the unit to stop, we want to not autoacquire until
			//he is able to stealth again. Of course, if he's detected, then don't bother trying.
			if( !theUnit->getStatusBits().test( OBJECT_STATUS_STEALTHED ) && !theUnit->getStatusBits().test( OBJECT_STATUS_DETECTED ) )
			{
				//Not stealthed, not detected -- so do auto-acquire while stealthed?
				if( !ai->canAutoAcquireWhileStealthed() )
				{
          StealthUpdate *stealth = theUnit->getStealth();
					if( stealth )
					{
						//Delay the mood check time (for autoacquire) until after the unit can stealth again.
						UnsignedInt stealthFrames = stealth->getStealthDelay();
						//Skew it a little due to having a large group selected.
						UnsignedInt randomFrames = GameLogicRandomValue( 0, LOGICFRAMES_PER_SECOND );
						ai->setNextMoodCheckTime( TheGameLogic->getFrame() + stealthFrames + randomFrames );
					}
				}
			}
		}

		if( !addWaypoint )
		{
			ai->aiMoveToPosition( &dest, cmdSource );
		}
		else
		{
			ai->aiFollowPathAppend(&dest, cmdSource);
		}
	}

	/* Where a slow group order actually went. The corridor above is one search for everybody; this
		 loop is the part that scales with the selection, because a member the corridor did not take
		 care of gets its own destination adjusted and its own path. */
#ifdef DEBUG_LOGGING
	Int64 ordersEnd;
	QueryPerformanceCounter( (LARGE_INTEGER *)&ordersEnd );
	if( corridorFreq > 0 )
	{
		const Real ordersMS =
			(Real)((double)(ordersEnd - corridorEnd) * 1000.0 / (double)corridorFreq);
		if( corridorMS + ordersMS > 5.0f )
			DEBUG_LOG(("SLOW GROUPMOVE: %d members, corridor %.1fms, orders %.1fms\n",
				m_memberListSize, corridorMS, ordersMS));
	}
#endif
}

//-------------------------------------------------------------------------------------------------
// AI Command Interface implementation for AIGroup
//

/**
 * Scatter
 */
void AIGroup::groupScatter( CommandSourceType cmdSource )
{
	if (m_dirty)
		recompute();

	std::list<Object *>::iterator i;
	// compute current centroid of the team
	Coord3D center;
	Coord2D min;
	Coord2D max;
	Coord3D dest;

	getMinMaxAndCenter( &min, &max, &center );
	const Fix centerX = fixFromReal( center.x ), centerY = fixFromReal( center.y );

	// Move.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Fix dx, dy;
		if ((*i)->isDisabledByType( DISABLED_HELD ) ) 
		{
			continue; // don't bother telling the occupants to move.
		}
		if( (*i)->isKindOf( KINDOF_IMMOBILE ) )
		{	
			continue;
		}
		if( (*i)->getAI()==NULL )
		{	
			continue;
		}
		const FCoord3D *unitPos = (*i)->getPositionFix();
		TheAI->pathfinder()->removeGoal(*i);
		dx = unitPos->x - centerX;
		dy = unitPos->y - centerY;
		iter->insertFix((*i), dx*dx+dy*dy);
	}

	iter->sort(ITER_SORTED_FAR_TO_NEAR);
	Object *theUnit;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		center.x -= 0.01f;
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		Coord3D unitPos = theUnit->getPositionFix()->toCoord3D();	// P4: the scatter goal is a float move
		Coord2D delta;
		dest = unitPos;
		delta.x = unitPos.x - center.x;
		delta.y = unitPos.y - center.y;
		delta.normalize();
		const Real reach = fixToReal( 4 * theUnit->getGeometryInfo().getBoundingCircleRadiusFix() );	// P4
		dest.x += delta.x*reach;
		dest.y += delta.y*reach;
		ai->aiMoveToPosition( &dest, cmdSource );
	}
}


const Real CIRCLE = ( 2.0f * PI );

void getHelicopterOffset( Coord3D& posOut, Int idx )
{
  if (idx == 0)
    return;
  
  Real assumedHeliDiameter = 70.0f;
  Real radius = assumedHeliDiameter;
  Real circumference = radius * CIRCLE;
  Real angle = 0;
  Real angleBetweenEachChopper = assumedHeliDiameter / circumference * CIRCLE;
  for (Int h = 1; h < idx; ++h )
  {
    angle += angleBetweenEachChopper;

    if ( angle > CIRCLE )
    {
      radius += assumedHeliDiameter;
      circumference = radius * CIRCLE;
      angleBetweenEachChopper = assumedHeliDiameter / circumference * CIRCLE;
      angle -= CIRCLE;
    }
  }

  Coord3D tempCtr = posOut;
  posOut.x = tempCtr.x + (Sin(angle) * radius);
  posOut.y = tempCtr.y + (Cos(angle) * radius);

}


/**
 * Move to given position(s), tightening the formation
 */
void AIGroup::groupTightenToPosition( const Coord3D *pos, Bool addWaypoint, CommandSourceType cmdSource )
{		
	//Kris: Disabled (because its not used to make a logical difference)
	//Bool outsideOfBounds = true;
	Coord3D center;
	Coord2D min;
	Coord2D max;
	if( cmdSource == CMD_FROM_PLAYER && TheGlobalData->m_groupMoveClickToGatherFactor > 0.0f )
	{
		getMinMaxAndCenter( &min, &max, &center );
		//Kris: Disabled (because its not used to make a logical difference)
		//if( Coord3DInsideRect2D( pos, &min, &max ) )
		//{
		//	outsideOfBounds = FALSE;
		//}
	}
	/* The computer's armies come through here rather than groupMoveToPosition, so this is where they
		 are handed their lanes. */
	if (!addWaypoint)
	{
		Coord2D tMin, tMax;
		Coord3D tCenter;
		getMinMaxAndCenter( &tMin, &tMax, &tCenter );
		crowdSeedLanes( m_memberList, tCenter, pos );
	}

	// Tighten.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	std::list<Object *>::iterator i;
	const Fix goalX = fixFromReal( pos->x ), goalY = fixFromReal( pos->y );	// P5: the ordered point
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	{
		Fix dx, dy;
		const FCoord3D *unitPos = (*i)->getPositionFix();
		if ((*i)->isDisabledByType( DISABLED_HELD ) )
		{
			continue; // don't bother telling the occupants to move.
		}
		if( (*i)->isKindOf( KINDOF_IMMOBILE ) )
		{	
			continue;
		}
		if( (*i)->getAI()==NULL )
		{	
			continue;
		}
		dx = unitPos->x - goalX;
		dy = unitPos->y - goalY;
		iter->insertFix((*i), dx*dx+dy*dy);
	}

	iter->sort(ITER_SORTED_NEAR_TO_FAR);
	// Works better if you let the near units get the first paths... jba.

  // Need a special case for helicopters, which do tighten when in groups
  // but who do not reserve ground when they pathfind
  // so we will send each new helicopter found in this list to a discrete
  // offset from 'pos' from the s_helicopterFormation table
  // a more elegant solution should have been added to AIPathfind, but given
  // the late date, this is much safer.

  Int heliIdx = 0;
	Object *theUnit;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		if( !addWaypoint )
		{
      if ( theUnit->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) ) //NEW
      {
        Coord3D heliOffs = *pos;
        getHelicopterOffset( heliOffs, heliIdx++ );
        ai->aiTightenToPosition( &heliOffs, CMD_FROM_AI );//NEW
      }
      else
  			ai->aiTightenToPosition( pos, cmdSource );
		}
		else
		{
			ai->aiFollowPathAppend(pos, cmdSource);
		}
	}

	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		Coord3D unitPos = theUnit->getPositionFix()->toCoord3D();	// P5
		TheAI->pathfinder()->updatePos(theUnit, &unitPos);
	}
}




/**
 * Start following the path from the given point
 */
void AIGroup::groupFollowWaypointPath( const Waypoint *way, CommandSourceType cmdSource )
{
	crowdSeedLanesAlongWaypoint( this, m_memberList, way );
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiFollowWaypointPath( way, cmdSource );
		}
	}
}

/**
 * Start following the path from the given point
 */
void AIGroup::groupFollowWaypointPathExact( const Waypoint *way, CommandSourceType cmdSource )
{
	crowdSeedLanesAlongWaypoint( this, m_memberList, way );
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiFollowWaypointPathExact( way, cmdSource );
		}
	}
}

/**
 * Move to given position and unload transports.
 */
void AIGroup::groupMoveToAndEvacuate( const Coord3D *pos, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiMoveToAndEvacuate( pos, cmdSource );
		}
	}
}

/**
 * Move to given position and unload transports.
 * transport returns and deletes itself.
 */
void AIGroup::groupMoveToAndEvacuateAndExit( const Coord3D *pos, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiMoveToAndEvacuateAndExit( pos, cmdSource );
		}
	}
}

/**
 * Start following the path from the given point
 */
void AIGroup::groupFollowWaypointPathAsTeam( const Waypoint *way, CommandSourceType cmdSource )
{
	/* A wave leaves on this order, not on groupMoveToPosition.  Without a lane every member drives
		 the centre of the same road, which is the single file the crowd model was written to stop. */
	crowdSeedLanesAlongWaypoint( this, m_memberList, way );
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiFollowWaypointPathAsTeam( way, cmdSource );
		}
	}
}

/**
 * Start following the path from the given point
 */
void AIGroup::groupFollowWaypointPathAsTeamExact( const Waypoint *way, CommandSourceType cmdSource )
{
	crowdSeedLanesAlongWaypoint( this, m_memberList, way );
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiFollowWaypointPathExactAsTeam( way, cmdSource );
		}
	}
}

//Callback for groupIdle -- contained buildings.
void makeMemberStop( Object *obj, void* userData )
{
	CommandSourceType cmdSource = *((CommandSourceType*)userData);
	if( obj )
	{
		AIUpdateInterface *ai = obj->getAI();
		if( ai )
		{
			ai->aiIdle( cmdSource );
		}
	}
}

/**
 * Enter the idle state.
 */
void AIGroup::groupIdle(CommandSourceType cmdSource)
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = *i;
		
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai)
		{
			ai->aiIdle(cmdSource);

			if( cmdSource == CMD_FROM_PLAYER && obj->getStatusBits().test( OBJECT_STATUS_CAN_STEALTH ) && ai->canAutoAcquire() )
			{
				//When ordering a combat stealth unit to stop, there is a single special case we want to handle.
				//When a stealth unit is currently not stealthed and doesn't autoacquire while stealthed,
				//then when the player specifically orders the unit to stop, we want to not autoacquire until
				//he is able to stealth again. Of course, if he's detected, then don't bother trying.
				if( !obj->getStatusBits().test( OBJECT_STATUS_STEALTHED ) && !obj->getStatusBits().test( OBJECT_STATUS_DETECTED ) )
				{
					//Not stealthed, not detected -- so do auto-acquire while stealthed?
					if( !ai->canAutoAcquireWhileStealthed() )
					{
            StealthUpdate *stealth = obj->getStealth();
						if( stealth )
						{
							//Delay the mood check time (for autoacquire) until after the unit can stealth again.
							UnsignedInt stealthFrames = stealth->getStealthDelay();
							//Skew it a little due to having a large group selected.
							UnsignedInt randomFrames = GameLogicRandomValue( 0, LOGICFRAMES_PER_SECOND );
							ai->setNextMoodCheckTime( TheGameLogic->getFrame() + stealthFrames + randomFrames );
						}
					}
				}
			}
		}
		else
		{
			//Handle garrisoned buildings.  Stop is for the ones shooting out of them: passengers who
			//may not fire are there for the building's own job, and stopping them ended the hacking in
			//an Internet Center until every hacker was taken out and put back.
			ContainModuleInterface *contain = obj->getContain();
			if( contain && contain->isPassengerAllowedToFire() )
			{
				contain->iterateContained( makeMemberStop, &cmdSource, false );
			}
		}

		//Also handle slaves. If we have slaves, then order them to stop too!
		SpawnBehaviorInterface *spawnInterface = obj->getSpawnBehaviorInterface();
		if( spawnInterface )
		{
			spawnInterface->orderSlavesToGoIdle( cmdSource );
			//Do we need to delay mood check?
		}

	}
}

/**
 * Follow the path defined by the given array of points
 */
void AIGroup::groupFollowPath( const std::vector<Coord3D>* path, Object *ignoreObject, CommandSourceType cmdSource )
{
}

/**
 * Attack given object
 */
/**
 * Attack given object
 */
void AIGroup::groupAttackObjectPrivate( Bool forced, Object *victim, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!victim) {
		// Hard to kill em if they're already dead.  jba
		return;
	}
	const FCoord3D victimPos = *victim->getPositionFix();
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )	{
		Fix dx, dy;
		const FCoord3D *unitPos = (*i)->getPositionFix();
		// Held units were skipped here on the grounds that there is no point telling a passenger to
		// walk somewhere - but this is the attack order, not a move, and a unit that is held can
		// still shoot.  A bunkered Battle Bus ignored every attack you gave it.
		dx = unitPos->x - victimPos.x;
		dy = unitPos->y - victimPos.y;
		iter->insertFix((*i), dx*dx+dy*dy);
	}

	iter->sort(ITER_SORTED_NEAR_TO_FAR);
	// Works better if you let the near units get the first paths... jba.
	Object *theUnit;
	for (theUnit = iter->first(); theUnit; theUnit = iter->next())
	{
		//Determine if this object is a garrisoned container capable of firing! 
		//If so, order everyone inside to attack as well!
		ContainModuleInterface *contain = theUnit->getContain();
		if( contain && contain->isPassengerAllowedToFire() )
		{
			//Loop through each member and order them to attack the same target (if possible)
			const ContainedItemsList* items = contain->getContainedItemsList();
			if (items)
			{
				for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				{
					Object* garrisonedMember = *it;
					CanAttackResult result = garrisonedMember->getAbleToAttackSpecificObject( forced ? ATTACK_NEW_TARGET_FORCED : ATTACK_NEW_TARGET, victim, cmdSource );
					if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
					{
						AIUpdateInterface *memberAI = garrisonedMember->getAI();
						if( memberAI )
						{
							if (forced)
								memberAI->aiForceAttackObject( victim, maxShotsToFire, cmdSource );
							else
								memberAI->aiAttackObject( victim, maxShotsToFire, cmdSource );
						}
					}
				}
			}
		}
		
		//Do a check to see if we have a hive object that has slaved objects.
		SpawnBehaviorInterface *spawnInterface = theUnit->getSpawnBehaviorInterface();
		if( spawnInterface && !spawnInterface->doSlavesHaveFreedom() )
		{
			spawnInterface->orderSlavesToAttackTarget( victim, maxShotsToFire, cmdSource );
		}

		//Order the specific group object to attack!
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		if( ai && theUnit != victim )
		{
			if (forced)
				ai->aiForceAttackObject( victim, maxShotsToFire, cmdSource );
			else
				ai->aiAttackObject( victim, maxShotsToFire, cmdSource );
		}
	}
}

/**
 * Attack the given team
 */
void AIGroup::groupAttackTeam( const Team *team, Int maxShotsToFire, CommandSourceType cmdSource )
{
	if (!team) {
		// Hard to kill em if they're already dead.  jba
		return;
	}
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiAttackTeam( team, maxShotsToFire, cmdSource );
		}
	}
}

/**
 * Attack given spot
 */
void AIGroup::groupAttackPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource )
{
	Coord3D attackPos;
	if( pos )
	{
		attackPos = *pos;
	}
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		if( !pos )
		{
			//If you specify a NULL position, it means you are attacking your own location.
			attackPos = (*i)->getPositionFix()->toCoord3D();	// P6: the weapon and attack orders take a float spot
		}

		//This code allows garrisoned buildings to force attack a ground position
		//-----------------------------------------------------------------------
		//Determine if this object is a garrisoned container capable of firing! 
		//If so, order everyone inside to attack as well!
		ContainModuleInterface *contain = (*i)->getContain();
		if( contain && contain->isPassengerAllowedToFire() )
		{
			//Loop through each member and order them to attack the same target (if possible)
			const ContainedItemsList* items = contain->getContainedItemsList();
			if (items)
			{
				for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
				{
					Object* garrisonedMember = *it;
					CanAttackResult result = garrisonedMember->getAbleToUseWeaponAgainstTarget( ATTACK_NEW_TARGET, NULL, &attackPos, cmdSource ) ;
					if( result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING )
					{
						AIUpdateInterface *memberAI = garrisonedMember->getAI();
						if( memberAI )
						{
							memberAI->aiAttackPosition( &attackPos, maxShotsToFire, cmdSource );
						}
					}
				}
			}
		}

		//Also handle slaves. If we have slaves, then order them to stop too!
		SpawnBehaviorInterface *spawnInterface = (*i)->getSpawnBehaviorInterface();
		if( spawnInterface && !spawnInterface->doSlavesHaveFreedom() )
		{
			spawnInterface->orderSlavesToAttackPosition( &attackPos, maxShotsToFire, cmdSource );
		}

		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiAttackPosition( &attackPos, maxShotsToFire, cmdSource );
		}
	}
}

/**
 * Attack move to a location
 */
void AIGroup::groupAttackMoveToPosition( const Coord3D *pos, Int maxShotsToFire, CommandSourceType cmdSource, Bool matchSpeeds )
{
	//
	// This used to hand every member the same single coordinate, so a group attack move arrived as
	// a queue rather than a group: everyone pathed to one point, at their own top speed.  Spread the
	// destination the way groupMoveToPosition does (each member keeps its offset from the group
	// centroid).
	//
	// Holding the ground units to the speed of the slowest one keeps the group together on the way
	// in, but it also means one damaged truck walks the tanks in at its own pace, and that is not
	// always what you asked for. So it is the player's call, taken from the ctrl key on the click:
	// ctrl-click an attack move and they arrive together, click it plainly and they each go at
	// their own speed. AIAttackMoveToState::onEnter reads it back off the group.
	//
	m_matchSpeeds = matchSpeeds;

	if (m_dirty)
		recompute();

	Coord3D center;
	Coord2D min;
	Coord2D max;
	getMinMaxAndCenter( &min, &max, &center );

	/* An attack move is a march that expects to be interrupted, which is exactly the order that
		 wants the crowd model and was not getting it: nothing here handed out a lane, and a unit with
		 no lane is one crowdSteer returns out of on its first line.  So a group told to fight its way
		 across a map drove there in single file while the same group told to walk there spread out. */
	crowdSeedLanes( m_memberList, center, pos );

	// through the tunnel network when that is shorter, and fighting again from the far mouth; see
	// TunnelTracker::findTunnelShortcut.  A player's order or a computer's, never a script's: a mission
	// script sends its units the way the mission was written for.
	Object *tunnelEntrance = NULL;
	if ((cmdSource == CMD_FROM_PLAYER || cmdSource == CMD_FROM_AI) && !m_memberList.empty())
	{
		const Real walkX = pos->x - center.x;
		const Real walkY = pos->y - center.y;
		tunnelEntrance = m_memberList.front()->getControllingPlayer()->getTunnelSystem()->findTunnelShortcut( &center, pos,
			(Real)sqrt( walkX * walkX + walkY * walkY ) );
	}

	// path the members closest to the goal first; it leaves fewer of them to collide on arrival.
	MemoryPoolObjectHolder iterHolder;
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	iterHolder.hold(iter);
	const Fix goalX = fixFromReal( pos->x ), goalY = fixFromReal( pos->y );	// P5: the ordered point
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *member = *i;
		if (member->isDisabledByType( DISABLED_HELD ))
			continue;			// don't bother telling the occupants to move
		if (member->isKindOf( KINDOF_IMMOBILE ))
			continue;
		if (member->getAIUpdateInterface() == NULL)
			continue;

		TheAI->pathfinder()->removeGoal( member );
		const Fix dx = member->getPositionFix()->x - goalX;
		const Fix dy = member->getPositionFix()->y - goalY;
		iter->insertFix( member, dx*dx + dy*dy );
	}
	iter->sort( ITER_SORTED_NEAR_TO_FAR );

	Coord3D goalPos = *pos;
	Bool firstUnit = true;
	Object *theUnit;
	for( theUnit = iter->first(); theUnit; theUnit = iter->next() )
	{
		AIUpdateInterface *ai = theUnit->getAIUpdateInterface();
		if (firstUnit)
		{
			// the member nearest the goal defines the shape; everyone else keeps its offset from it.
			center = theUnit->getPositionFix()->toCoord3D();	// P5
			firstUnit = false;
		}

		//
		// As with a plain move (see groupMoveToPosition): a player's attack move sends everyone to
		// the point and lets each unit take the nearest free cell to it, rather than holding the
		// selection's shape on the way in - which a large group could only do by spreading along
		// an arc.
		//
		Coord3D dest;
		if (cmdSource == CMD_FROM_PLAYER)
			dest = goalPos;
		else
			computeIndividualDestination( &dest, &goalPos, theUnit, &center, FALSE );

		const TunnelTripEnd tripEnd = theUnit->isAbleToAttack() ? TUNNEL_TRIP_ATTACK_MOVE : TUNNEL_TRIP_MOVE;
		if (tunnelEntrance != NULL && ai->takeTunnelTrip( tunnelEntrance, &dest, tripEnd, cmdSource ))
		{
			ai->clearCrowdLane();
			continue;
		}

		// the speed of the slowest member is picked up by AIAttackMoveToState::onEnter, which runs
		// inside this order - the move state resets the desired speed, so it cannot be set here.
		if (theUnit->isAbleToAttack())
			ai->aiAttackMoveToPosition( &dest, maxShotsToFire, cmdSource );
		else
			ai->aiMoveToPosition( &dest, cmdSource );
	}
}

/**
 * Begin "seek and destroy"
 */
void AIGroup::groupHunt( CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiHunt( cmdSource );
		}
	}
}


/**
 * Repair the given object
 */
void AIGroup::groupRepair( Object *obj, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiRepair( obj, cmdSource );
		}
	}
}

/**
	* Resume construction on object
	*/
void AIGroup::groupResumeConstruction( Object *obj, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiResumeConstruction( obj, cmdSource );
		}
	}
}

/**
 * Get healed at the heal depot
 */
void AIGroup::groupGetHealed( Object *healDepot, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGetHealed( healDepot, cmdSource );
		}
	}
}

/**
 * Get repaired at the repair depot
 */
void AIGroup::groupGetRepaired( Object *repairDepot, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGetRepaired( repairDepot, cmdSource );
		}
	}
}

/**
 * Enter the given object
 */
void AIGroup::groupEnter( Object *obj, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiEnter( obj, cmdSource );
		}
	}
}

/**
 * Get near given object and wait for enter clearance
 */
void AIGroup::groupDock( Object *obj, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiDock( obj, cmdSource );
		}
	}
}

/**
 * Get out of whatever it is inside of
 */
void AIGroup::groupExit( Object *objectToExit,  CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiExit( objectToExit, cmdSource );
		}
	}
}

/**
 * Empty its contents
 */
void AIGroup::groupEvacuate( CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			if( (*i)->isKindOf( KINDOF_AIRCRAFT ) && (*i)->isAirborneTarget() )
			{
				//Calculate the highest point on the ground to drop off troops (chinook or other air transports)
				const FCoord3D *at = (*i)->getPositionFix();
				Coord3D pos = at->toCoord3D();	// P4: the drop-off spot is a float move goal
				PathfindLayerEnum layerAtDest = TheTerrainLogic->getHighestLayerForDestinationFix( at );
				pos.z = fixToReal( TheTerrainLogic->getLayerHeightFix( at->x, at->y, layerAtDest ) );
				ai->aiMoveToAndEvacuate( &pos, cmdSource );
			}
			else
			{
				ai->aiEvacuate( FALSE, cmdSource );
			}
		}
		else if( (*i)->isKindOf( KINDOF_STRUCTURE ) )
		{
			//Buildings don't normally have AIUpdateInterfaces. In this special
			//case, simple call the function directly. Special powers work in a similar
			//manner.
			ContainModuleInterface *contain = (*i)->getContain();
			if( contain )
			{
				contain->orderAllPassengersToExit( cmdSource, FALSE );
			}
		}
	}
}

/**
	* Execute railed transport behavior
	*/
void AIGroup::groupExecuteRailedTransport( CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();

		if( ai )
			ai->aiExecuteRailedTransport( cmdSource );

	}  // end for i

}  // end groupExecuteRailedTransport

///< life altering state change, if this AI can do it
void AIGroup::groupGoProne( const DamageInfo *damageInfo, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGoProne( damageInfo, cmdSource );
		}
	}
}

/**
 * Guard the given spot
 */
void AIGroup::groupGuardPosition( const Coord3D *pos, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (!pos) {
		return;
	}

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGuardPosition( pos, guardMode, cmdSource );
		}
	}
}

/**
 * Guard the given object
 */
void AIGroup::groupGuardObject( Object *objToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (!objToGuard) {
		return;
	}

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGuardObject( objToGuard, guardMode, cmdSource );
		}
	}
}

/**
 * Guard the given area
 */
void AIGroup::groupGuardArea( const PolygonTrigger *areaToGuard, GuardMode guardMode, CommandSourceType cmdSource )
{
	if (!areaToGuard) {
		return;
	}

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiGuardArea( areaToGuard, guardMode, cmdSource );
		}
	}
}

/**
 * Attack the given area
 */
void AIGroup::groupAttackArea( const PolygonTrigger *areaToGuard, CommandSourceType cmdSource )
{
	if (!areaToGuard) {
		return;
	}
	
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiAttackArea( areaToGuard, cmdSource );
		}
	}
}

void AIGroup::groupHackInternet( CommandSourceType cmdSource )				///< Begin hacking the internet for free cash from the heavens.
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->aiHackInternet( cmdSource );
		}
	}
}


void AIGroup::groupCreateFormation( CommandSourceType cmdSource )				///< Create a formation.
{
	Coord3D center;
	Coord2D min;
	Coord2D max;
	Bool isFormation = getMinMaxAndCenter( &min, &max, &center );
	std::list<Object *>::iterator i;
	FormationID id = TheAI->getNextFormationID();

	Int count = 0;
	FormationID countID = NO_FORMATION_ID;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		count++;
		countID = (*i)->getFormationID();
	}
	if (count==1 && countID!=NO_FORMATION_ID) {
		isFormation = true;
	}

	if (isFormation) {
		id = NO_FORMATION_ID;
	}

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = (*i);
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			const FCoord3D *pos = obj->getPositionFix();
			Coord2D offset;
			offset.x = fixToReal( pos->x ) - center.x;	// P5: the offset is added to float path goals
			offset.y = fixToReal( pos->y ) - center.y;
			obj->setFormationID(id);
			obj->setFormationOffset(offset);
		}
	}
}

/**
 * The unit(s)/structure will perform it's special power -- special powers triggered by buildings
 * don't use AIUpdateInterfaces!!! No special power uses an AIUpdateInterface immediately, but special
 * abilities, which are derived from special powers do... and are unit triggered. Those do have AI.
 */
void AIGroup::groupDoSpecialPower( UnsignedInt specialPowerID, UnsignedInt commandOptions )
{
	//
	// This is the no target, no position version.
	//
	// Firing a special power can destroy the group it was ordered from. The rebel ambush over water
	// drowns every rebel; their slow death calls deselect(), which empties this list to keep the
	// selection and the group in step. Walking m_memberList while that happens walks freed nodes.
	// So take the members by id first and drive the copy - each one looked up again, because by the
	// time its turn comes an earlier member's power may have killed it.
	//
	const VecObjectID members = getAllIDs();
	for( VecObjectID::const_iterator i = members.begin(); i != members.end(); ++i )
	{
		//Special powers do a lot of different things, but the top level stuff doesn't use
		//ai interface code. It finds the special power module and calls it directly for each object.
		Object *object = TheGameLogic->findObjectByID( *i );
		if( object == NULL || object->isEffectivelyDead() )
			continue;
		const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID( specialPowerID );
		if( spTemplate )
		{
			// Have to justify the execution in case someone changed their button
			if( spTemplate->getRequiredScience() != SCIENCE_INVALID )
			{
				if( !object->getControllingPlayer()->hasScience(spTemplate->getRequiredScience()) )
					continue;// Nice try, smacktard.
			}

			SpecialPowerModuleInterface *mod = object->getSpecialPowerModule( spTemplate );
			if( mod )
			{
				if( TheActionManager->canDoSpecialPower( object, spTemplate, CMD_FROM_PLAYER, commandOptions ) )
				{
					mod->doSpecialPower( commandOptions );

					object->friend_setUndetectedDefector( FALSE );// My secret is out
				}
			}
		}
	}
}

/**
 * The unit(s)/structure will perform it's special power -- special powers triggered by buildings
 * don't use AIUpdateInterfaces!!! No special power uses an AIUpdateInterface immediately, but special
 * abilities, which are derived from special powers do... and are unit triggered. Those do have AI.
 */
void AIGroup::groupDoSpecialPowerAtLocation( UnsignedInt specialPowerID, const Coord3D *location, Real angle, const Object *objectInWay, UnsignedInt commandOptions )
{
  

	//
	// This one requires a position.
	//
	// The rebel ambush over the ocean drowns every rebel, and their slow death calls deselect(),
	// which destroys this list to keep the selection in step with the group - M Lorenzen noted that
	// in 2003 and stepped the iterator forward before firing to cope with it. That covers the member
	// that just went away and nothing else: a list emptied outright leaves the iterator on a freed
	// node. Take the members by id instead - see groupDoSpecialPower above.
	//
	const VecObjectID members = getAllIDs();
	for( VecObjectID::const_iterator i = members.begin(); i != members.end(); ++i )
	{
		//Special powers do a lot of different things, but the top level stuff doesn't use
		//ai interface code. It finds the special power module and calls it directly for each object.

		Object *object = TheGameLogic->findObjectByID( *i );
		if( object == NULL || object->isEffectivelyDead() )
			continue;

    const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID( specialPowerID );
		if( spTemplate )
		{
			// Have to justify the execution in case someone changed their button
			if( spTemplate->getRequiredScience() != SCIENCE_INVALID )
			{
				if( !object->getControllingPlayer()->hasScience(spTemplate->getRequiredScience()) )
					continue;// Nice try, smacktard.
			}

			SpecialPowerModuleInterface *mod = object->getSpecialPowerModule( spTemplate );
			if( mod )
			{
				if( TheActionManager->canDoSpecialPowerAtLocation( object, location, CMD_FROM_PLAYER, spTemplate, objectInWay, commandOptions ) )
				{
					mod->doSpecialPowerAtLocation( location, angle, commandOptions );

					object->friend_setUndetectedDefector( FALSE );// My secret is out
				}
			}
		}

	}
}

/**
 * The unit(s)/structure will perform it's special power -- special powers triggered by buildings
 * don't use AIUpdateInterfaces!!! No special power uses an AIUpdateInterface immediately, but special
 * abilities, which are derived from special powers do... and are unit triggered. Those do have AI.
 */
void AIGroup::groupDoSpecialPowerAtObject( UnsignedInt specialPowerID, Object *target, UnsignedInt commandOptions )
{
	//This one requires a target; by id for the same reason - see groupDoSpecialPower above.
	const VecObjectID members = getAllIDs();
	for( VecObjectID::const_iterator i = members.begin(); i != members.end(); ++i )
	{
		//Special powers do a lot of different things, but the top level stuff doesn't use
		//ai interface code. It finds the special power module and calls it directly for each object.

		Object *object = TheGameLogic->findObjectByID( *i );
		if( object == NULL || object->isEffectivelyDead() )
			continue;
		const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID( specialPowerID );
		if( spTemplate )
		{
			// Have to justify the execution in case someone changed their button
			if( spTemplate->getRequiredScience() != SCIENCE_INVALID )
			{
				if( !object->getControllingPlayer()->hasScience(spTemplate->getRequiredScience()) )
					continue;// Nice try, smacktard.
			}

			SpecialPowerModuleInterface *mod = object->getSpecialPowerModule( spTemplate );
			if( mod )
			{
				if( TheActionManager->canDoSpecialPowerAtObject( object, target, CMD_FROM_PLAYER, spTemplate, commandOptions ) )
				{
					mod->doSpecialPowerAtObject( target, commandOptions );

					object->friend_setUndetectedDefector( FALSE );// My secret is out
				}
			}
		}
	}
}

#ifdef ALLOW_SURRENDER
void AIGroup::groupSurrender( const Object *objWeSurrenderedTo, Bool surrender, CommandSourceType cmdSource )
{
	//This is currently only activated via test key
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->setSurrendered(objWeSurrenderedTo, surrender);
		}
	}
}
#endif

void AIGroup::groupCheer( CommandSourceType cmdSource )
{
	//This is currently only activated via test key
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *object = (*i);
		//This allows all special conditions states to reset after a specified delay. Assume
		//only one works at a time (it'll clear any others).
		object->setSpecialModelConditionState( MODELCONDITION_SPECIAL_CHEERING, LOGICFRAMES_PER_SECOND * 3 );
	}
}

/** The checks ControlBar makes before it shows the sell button.  The order lands on whatever is
	* selected when the logic frame runs, and a selection hotkey pressed in the same frame as the
	* button used to sell a tech building, a scaffold or a garrisoned civilian building. */
static Bool mayPlayerSell( const Object *obj )
{
	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return FALSE;
	if( obj->testScriptStatusBit( OBJECT_STATUS_SCRIPT_UNSELLABLE ) || obj->isDisabledByType( DISABLED_SUBDUED ) )
		return FALSE;

	const CommandSet *commandSet = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( commandSet == NULL )
		return FALSE;

	for( Int buttonIndex = 0; buttonIndex < MAX_COMMANDS_PER_SET; buttonIndex++ )
	{
		const CommandButton *button = commandSet->getCommandButton( buttonIndex );
		if( button && button->getCommandType() == GUI_COMMAND_SELL )
			return TRUE;
	}
	return FALSE;
}

/**
	* Sell all things in the group ... if possible
	*/
void AIGroup::groupSell( CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i, thisIterator;
	Object *obj;

	for( i = m_memberList.begin(); i != m_memberList.end(); /*empty*/ )
	{

		// work off of 'thisIterator' as we may change the contents of this list
		thisIterator = i;
		++i;

		// get object
		obj = *thisIterator;

		if( cmdSource == CMD_FROM_PLAYER && !mayPlayerSell( obj ) )
			continue;

		// try to sell object
		TheBuildAssistant->sellObject( obj );

	}  // end for, i

}

/**
	* Tell all things in the group to toggle overcharge ... if possible 
	*/
void AIGroup::groupToggleOvercharge( CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *obj;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		obj = *i;

		OverchargeBehaviorInterface *obi;
		for( BehaviorModule **bmi = obj->getBehaviorModules(); *bmi; ++bmi )
		{

			obi = (*bmi)->getOverchargeBehaviorInterface();
			if( obi )
				obi->toggle();

		}  // end for

	}  // end for, i

}

#ifdef ALLOW_SURRENDER
/**
	* Pick up prisoners of war
	*/
void AIGroup::groupPickUpPrisoner( Object *prisoner, enum CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *obj;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		obj = *i;
		
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai )
			ai->aiPickUpPrisoner( prisoner, cmdSource );

	}  // end for, i

}
#endif

#ifdef ALLOW_SURRENDER
/**
	* Return to prison
	*/
void AIGroup::groupReturnToPrison( Object *prison, enum CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *obj;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		obj = *i;
		
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai )
			ai->aiReturnPrisoners( prison, cmdSource );

	}  // end for, i
}
#endif

/**
	* Combat drop
	*/
void AIGroup::groupCombatDrop( Object *target, const Coord3D &pos, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *obj;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		obj = *i;

		// do action
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( ai )
			ai->aiCombatDrop( target, pos, cmdSource );

	}  // end for, i

}

//-------------------------------------------------------------------------------------
// Used by scripts to issue a command button order - Note that it's possible that some 
// commands are not AI commands!
//-------------------------------------------------------------------------------------
void AIGroup::groupDoCommandButton( const CommandButton *commandButton, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *source;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		source = *i;
		
		source->doCommandButton( commandButton, cmdSource );
	}  // end for, i
}


//-------------------------------------------------------------------------------------
// Used by scripts to issue a command button order - Note that it's possible that some 
// commands are not AI commands!
//-------------------------------------------------------------------------------------
void AIGroup::groupDoCommandButtonAtPosition( const CommandButton *commandButton, const Coord3D *pos, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *source;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		source = *i;
		
		source->doCommandButtonAtPosition( commandButton, pos, cmdSource );
	}  // end for, i
}

//-------------------------------------------------------------------------------------
// Used by scripts to issue a command button order - Note that it's possible that some 
// commands are not AI commands!
//-------------------------------------------------------------------------------------
void AIGroup::groupDoCommandButtonUsingWaypoints( const CommandButton *commandButton, const Waypoint *way, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *source;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		source = *i;
		
		source->doCommandButtonUsingWaypoints( commandButton, way, cmdSource );
	}  // end for, i
}

//-------------------------------------------------------------------------------------
// Used by scripts to issue a command button order - Note that it's possible that some 
// commands are not AI commands!
//-------------------------------------------------------------------------------------
void AIGroup::groupDoCommandButtonAtObject( const CommandButton *commandButton, Object *obj, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	Object *source;

	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{

		// get object
		source = *i;
		
		source->doCommandButtonAtObject( commandButton, obj, cmdSource );
	}  // end for, i
}


/**
 * Set the behavior modifier for this agent
 */
void AIGroup::setAttitude( AttitudeType tude )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		AIUpdateInterface *ai = (*i)->getAIUpdateInterface();
		if (ai)
		{
			ai->setAttitude( tude );
		}
	}
}

/**
 * Get the current behavior modifier state
 */
AttitudeType AIGroup::getAttitude( void ) const
{
	return AI_PASSIVE;
}

void AIGroup::setMineClearingDetail( Bool set )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		if (set)
			(*i)->setWeaponSetFlag(WEAPONSET_MINE_CLEARING_DETAIL);
		else
			(*i)->clearWeaponSetFlag(WEAPONSET_MINE_CLEARING_DETAIL);
	}
}

Bool AIGroup::setWeaponLockForGroup( WeaponSlotType weaponSlot, WeaponLockType lockType )
{
	Bool any = false;
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		// a permanent lock is the switch-weapon button, and only members that have the button take it
		if( lockType == LOCKED_PERMANENTLY && !(*i)->canSwitchToWeapon( weaponSlot ) )
			continue;
		if ((*i)->setWeaponLock( weaponSlot, lockType ))
			any = true;
	}
	return any;
}

void AIGroup::releaseWeaponLockForGroup(WeaponLockType lockType)
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		(*i)->releaseWeaponLock(lockType);
	}
}

//This function loops through the AIGroup setting the weaponset only for those units that
//have the specified weaponset. If a member doesn't have the weaponset, nothing happens for
//that unit.
void AIGroup::setWeaponSetFlag( WeaponSetType wst )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = (*i);
		//First check to see if our object even has the specified weaponset. It's very
		//likely that a selected group won't all have the same weaponset options, so
		//only set it for those members that have it.
		WeaponSetFlags flags;
		flags.set( wst );
		const WeaponTemplateSet* set = obj->getTemplate()->findWeaponTemplateSet( flags );
		if( set )
		{
			obj->setWeaponSetFlag( wst );
		}
	}
}

void AIGroup::queueUpgrade( const UpgradeTemplate *upgrade )
{
	if (!upgrade)
		return;

	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *thisMember = (*i);
		// make sure that the this object can actually build the upgrade
		// There is an extra check for Object type only.  These are the same checks as in
		// ControlCommandProcessing when the message was going out.  We are just revalidating on the
		// way in to stop cheaters.
		if( ! TheUpgradeCenter->canAffordUpgrade( thisMember->getControllingPlayer(), upgrade, FALSE ) )
		{
			continue;
		}
		if( upgrade->getUpgradeType() == UPGRADE_TYPE_OBJECT )
		{
			if( thisMember->hasUpgrade( upgrade )  || !thisMember->affectedByUpgrade( upgrade ) )
				continue;
		}
		
		// Ever think to check if this thing can actually build the upgrade to "stop cheaters"?
		if( !thisMember->canProduceUpgrade(upgrade) )
			continue;// They have faked their button; go out of sync. (Cheater will execute it, non cheater will not execute it.)

		// producer must have a production update
		ProductionUpdateInterface *pu = thisMember->getProductionUpdateInterface();
		if( pu == NULL )
			continue;

		if ( pu->canQueueUpgrade( upgrade ) == CANMAKE_QUEUE_FULL )
			continue;//So we don't charge them for something that we can't build... happy happy

		
		// queue the upgrade "research"
		pu->queueUpgrade( upgrade );
	}
}

//------------------------------------------------------------------------------------------------------------
Bool AIGroup::isIdle( void ) const
{
	Bool isIdle = true;
	std::list<Object *>::const_iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = *i;
		if (!obj) {
			continue;
		}

		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (!ai) {
			continue;
		}

		//Kris: Optimization
		isIdle = ai->isIdle() || obj->isEffectivelyDead();
		if( !isIdle )
		{
			//Don't bother continuing if even one of our members is not idle.
			return false;
		}
	}

	return isIdle;
}

//------------------------------------------------------------------------------------------------------------
//Definition of busy -- when explicitly in the busy state. Moving or attacking is not considered busy!
//------------------------------------------------------------------------------------------------------------
Bool AIGroup::isBusy( void ) const
{
	Bool isBusy = true;
	std::list<Object *>::const_iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = *i;
		if( !obj ) 
		{
			continue;
		}

		const AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if( !ai ) 
		{
			continue;
		}


		//Kris: Optimization
		isBusy = ai->isBusy() && !obj->isEffectivelyDead();
		if( !isBusy )
		{
			//Don't bother continuing if even one of our members is not busy.
			return false;
		}
	}

	return isBusy;
}

//------------------------------------------------------------------------------------------------------------
// return true iff all group members are dead
//------------------------------------------------------------------------------------------------------------
Bool AIGroup::isGroupAiDead( void ) const
{
	Bool isDead = true;
	std::list<Object *>::const_iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *obj = *i;
		if (!obj) {
			continue;
		}

		isDead = (isDead && obj->isEffectivelyDead());
	}

	return isDead;
}

// Returns an object that can perform the special power. Useful for making queries on the Action Manager
//------------------------------------------------------------------------------------------------------------
Object *AIGroup::getSpecialPowerSourceObject( UnsignedInt specialPowerID )
{
	std::list<Object *>::iterator i;
	const SpecialPowerTemplate *spTemplate = TheSpecialPowerStore->findSpecialPowerTemplateByID( specialPowerID );
	if( spTemplate )
	{
		for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
		{
			Object *object = (*i);
			SpecialPowerModuleInterface *mod = object->getSpecialPowerModule( spTemplate );
			if( mod )
				return object;
		}
	}
	return NULL;
}

// Returns an object that has a command button for the GUI command type.
//------------------------------------------------------------------------------------------------------------
Object *AIGroup::getCommandButtonSourceObject( GUICommandType type )
{
	std::list<Object *>::iterator it;
	
	for( it = m_memberList.begin(); it != m_memberList.end(); ++it )
	{
		Object *object = (*it);
		if (!object) {
			continue;
		}

		const CommandSet *commandSet = TheControlBar->findCommandSet( object->getCommandSetString() );
		if (!commandSet) {
			continue;
		}

		const CommandButton *commandButton;
		for(Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
		{
			commandButton = commandSet->getCommandButton(i);
			if(commandButton && (commandButton->getCommandType() == type)) {
				return object;
			}
		}
	}

	return NULL;
}

//------------------------------------------------------------------------------------------------------------
void AIGroup::groupSetEmoticon( const AsciiString &name, Int duration )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *object = (*i);
		Drawable *draw = object->getDrawable();
		if( draw )
		{
			draw->setEmoticon( name, duration );
		}
	}
}

//-----------------------------------------------------------------------------
void AIGroup::groupOverrideSpecialPowerDestination( SpecialPowerType spType, const Coord3D *loc, CommandSourceType cmdSource )
{
	std::list<Object *>::iterator i;
	for( i = m_memberList.begin(); i != m_memberList.end(); ++i )
	{
		Object *object = (*i);
		if( object )
		{
			SpecialPowerUpdateInterface *spuInterface = object->findSpecialPowerWithOverridableDestinationActive( spType );
			if( spuInterface )
			{
				spuInterface->setSpecialPowerOverridableDestination( loc );
			}
		}
	}
}

//-----------------------------------------------------------------------------
void AIGroup::crc( Xfer *xfer )
{
	ObjectID id = INVALID_ID;
	for (std::list<Object *>::iterator it = m_memberList.begin(); it != m_memberList.end(); ++it)
	{
		if (*it)
			id = (*it)->getID();
		xfer->xferUser(&id, sizeof(ObjectID));
		CRCGEN_LOG(("CRC after AI AIGroup m_memberList for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));
	}

	xfer->xferUnsignedInt( &m_memberListSize );
	CRCGEN_LOG(("CRC after AI AIGroup m_memberListSize for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));

	id = INVALID_ID;	// Used to be leader id, unused now. jba.
	xfer->xferObjectID( &id );
	CRCGEN_LOG(("CRC after AI AIGroup m_leader for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));
	xfer->xferReal( &m_speed );
	CRCGEN_LOG(("CRC after AI AIGroup m_speed for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));
	xfer->xferBool( &m_dirty );
	CRCGEN_LOG(("CRC after AI AIGroup m_dirty for frame %d is 0x%8.8X\n", TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));

	xfer->xferUnsignedInt( &m_id );
	CRCGEN_LOG(("CRC after AI AIGroup m_id (%d) for frame %d is 0x%8.8X\n", m_id, TheGameLogic->getFrame(), ((XferCRC *)xfer)->getCRC()));

}  // end crc

//-----------------------------------------------------------------------------
void AIGroup::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

}  // end xfer

//-----------------------------------------------------------------------------
void AIGroup::loadPostProcess( void )
{

}  // end loadPostProcess
