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

// FILE: TunnelTracker.cpp ///////////////////////////////////////////////////////////
// The part of a Player's brain that holds the communal Passenger list of all tunnels.
// Author: Graham Smallwood, March, 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/TunnelTracker.h"
#include "Common/Xfer.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/TunnelContain.h"


// ------------------------------------------------------------------------
TunnelTracker::TunnelTracker()
{
	m_tunnelCount = 0;
	m_containListSize = 0;
	m_curNemesisID = INVALID_ID;
	m_nemesisTimestamp = 0;
}

// ------------------------------------------------------------------------
TunnelTracker::~TunnelTracker()
{
	m_tunnelIDs.clear();
}

// ------------------------------------------------------------------------
void TunnelTracker::iterateContained( ContainIterateFunc func, void *userData, Bool reverse )
{
	if (reverse)
	{
		// note that this has to be smart enough to handle items in the list being deleted
		// via the callback function.
		for(ContainedItemsList::reverse_iterator it = m_containList.rbegin(); it != m_containList.rend(); )
		{
			// save the obj...
			Object* obj = *it;
			
			// incr the iterator BEFORE calling the func (if the func removes the obj,
			// the iterator becomes invalid)
			++it;
			
			// call it
			(*func)( obj, userData );
		}
	}
	else
	{
		// note that this has to be smart enough to handle items in the list being deleted
		// via the callback function.
		for(ContainedItemsList::iterator it = m_containList.begin(); it != m_containList.end(); )
		{
			// save the obj...
			Object* obj = *it;
			
			// incr the iterator BEFORE calling the func (if the func removes the obj,
			// the iterator becomes invalid)
			++it;
			
			// call it
			(*func)( obj, userData );
		}
	}
}

// ------------------------------------------------------------------------
Int TunnelTracker::getContainMax() const
{
	return TheGlobalData->m_maxTunnelCapacity;
}

// ------------------------------------------------------------------------
void TunnelTracker::swapContainedItemsList( ContainedItemsList& newList )
{
	m_containList.swap( newList );
	m_containListSize = (Int)m_containList.size();
}

// ------------------------------------------------------------------------
void TunnelTracker::updateNemesis(const Object *target)
{
	if (getCurNemesis()==NULL) {
		if (target) {
			if (target->isKindOf(KINDOF_VEHICLE) || target->isKindOf(KINDOF_STRUCTURE) ||
				target->isKindOf(KINDOF_INFANTRY) || target->isKindOf(KINDOF_AIRCRAFT)) {
					m_curNemesisID = target->getID();
					m_nemesisTimestamp = TheGameLogic->getFrame();
			}
		}
	} else if (getCurNemesis()==target) {
		m_nemesisTimestamp = TheGameLogic->getFrame();
	}
}

// ------------------------------------------------------------------------
Object *TunnelTracker::getCurNemesis(void)
{
	if (m_curNemesisID == INVALID_ID) {
		return NULL;
	}		
	if (m_nemesisTimestamp + 4*LOGICFRAMES_PER_SECOND < TheGameLogic->getFrame()) {
		m_curNemesisID = INVALID_ID;
		return NULL;
	}
	Object *target = TheGameLogic->findObjectByID(m_curNemesisID);
	if (target) {
		//If the enemy unit is stealthed and not detected, then we can't attack it!
	if( target->testStatus( OBJECT_STATUS_STEALTHED ) && 
			!target->testStatus( OBJECT_STATUS_DETECTED ) &&
			!target->testStatus( OBJECT_STATUS_DISGUISED ) )
		{
			target = NULL;
		}
	}
	if (target && target->isEffectivelyDead()) {
		target = NULL;
	}
	if (target == NULL) {
		m_curNemesisID = INVALID_ID;
	}
	return target;
}

// ------------------------------------------------------------------------
Bool TunnelTracker::isValidContainerFor(const Object* obj, Bool checkCapacity) const
{
	//October 11, 2002 -- Kris : Dustin wants ALL units to be able to use tunnels!
	// srj sez: um, except aircraft. 
	if (obj && !obj->isKindOf(KINDOF_AIRCRAFT))
	{
		if (checkCapacity)
		{
			Int containMax = getContainMax();
			Int containCount = getContainCount();
			return ( containCount < containMax );
		}
		else
		{
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------
void TunnelTracker::addToContainList( Object *obj )
{
	m_containList.push_back(obj);
	++m_containListSize;
}

// ------------------------------------------------------------------------
void TunnelTracker::removeFromContain( Object *obj, Bool exposeStealthUnits )
{

	ContainedItemsList::iterator it = std::find(m_containList.begin(), m_containList.end(), obj);
	if (it != m_containList.end())
	{
		// note that this invalidates the iterator!
		m_containList.erase(it);
		--m_containListSize;
	}	

}

// ------------------------------------------------------------------------
Bool TunnelTracker::isInContainer( Object *obj )
{
	return (std::find(m_containList.begin(), m_containList.end(), obj) != m_containList.end()) ;
}

// ------------------------------------------------------------------------
// A tunnel joins the network the frame its foundation is laid, and one coming back up out of a GLA
// hole is a foundation too, so a ghost nobody has built yet, or one being sold, is on the list; a
// unit sent there walked into the scaffolding.
static Bool isFinishedTunnel( const Object *tunnel )
{
	return !tunnel->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) && !tunnel->testStatus( OBJECT_STATUS_SOLD );
}

// ------------------------------------------------------------------------
void TunnelTracker::onTunnelCreated( const Object *newTunnel )
{
	m_tunnelCount++;
	m_tunnelIDs.push_back( newTunnel->getID() );
}

// ------------------------------------------------------------------------
void TunnelTracker::onTunnelDestroyed( const Object *deadTunnel )
{
	//
	// m_tunnelCount is unsigned, so a decrement at zero does not go negative, it goes to four
	// billion - and the branch below then takes the "there is still a tunnel standing" path with
	// nothing standing.
	//
	if( m_tunnelCount > 0 )
		m_tunnelCount--;
	m_tunnelIDs.remove( deadTunnel->getID() );

	if( m_tunnelCount == 0 )
	{
		// Kill everyone in our contain list.  Cave in!
		iterateContained( destroyObject, NULL, FALSE );
		m_containList.clear();
		m_containListSize = 0;
	}
	else
	{
		// The mouth named here is the one a trip with nowhere quiet to surface comes out of, so a
		// finished one wins over a foundation or a rebuild out of a hole; with nothing but building
		// sites left, the first of them.  The loop below copes with a null tunnel.
		Object *validTunnel = NULL;
		for( std::list<ObjectID>::const_iterator it = m_tunnelIDs.begin(); it != m_tunnelIDs.end(); ++it )
		{
			Object *tunnel = TheGameLogic->findObjectByID( *it );
			if( tunnel == NULL )
				continue;
			if( validTunnel == NULL )
				validTunnel = tunnel;
			if( isFinishedTunnel( tunnel ) )
			{
				validTunnel = tunnel;
				break;
			}
		}
		// Otherwise, make sure nobody inside remembers the dead tunnel as the one they entered 
		// (scripts need to use so there must be something valid here)
		for(ContainedItemsList::iterator it = m_containList.begin(); it != m_containList.end(); )
		{
			Object* obj = *it;
			++it;
			if( obj->getContainedBy() == deadTunnel )
				obj->onContainedBy( validTunnel );
		}
	}
}

// ------------------------------------------------------------------------
// A mouth that took a hit this recently is not one to send anybody through: they would come out
// into whatever is shooting at it.
static const UnsignedInt TUNNEL_UNDER_FIRE_FRAMES = 2 * LOGICFRAMES_PER_SECOND;

Object *TunnelTracker::findQuietTunnelNear( const Coord3D *pos ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *nearest = NULL;
	Real nearestSqr = 0.0f;
	for( std::list<ObjectID>::const_iterator it = m_tunnelIDs.begin(); it != m_tunnelIDs.end(); ++it )
	{
		Object *tunnel = TheGameLogic->findObjectByID( *it );
		if( tunnel == NULL || tunnel->isEffectivelyDead() )
			continue;

		if( !isFinishedTunnel( tunnel ) )
			continue;

		// the stamp starts at 0xffffffff, which the sum wraps to just under the window
		if( tunnel->getBodyModule()->getLastDamageTimestamp() + TUNNEL_UNDER_FIRE_FRAMES > now )
			continue;

		const Real distSqr = ThePartitionManager->getDistanceSquared( tunnel, pos, FROM_CENTER_2D );
		if( nearest == NULL || distSqr < nearestSqr )
		{
			nearest = tunnel;
			nearestSqr = distSqr;
		}
	}
	return nearest;
}

// ------------------------------------------------------------------------
Bool TunnelTracker::hasTunnelTraveller() const
{
	for( ContainedItemsList::const_iterator it = m_containList.begin(); it != m_containList.end(); ++it )
	{
		const AIUpdateInterface *ai = (*it)->getAI();
		if( ai != NULL && ai->hasTunnelTrip() )
			return TRUE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------
Int TunnelTracker::getResidentCount() const
{
	Int residents = 0;
	for( ContainedItemsList::const_iterator it = m_containList.begin(); it != m_containList.end(); ++it )
	{
		const AIUpdateInterface *ai = (*it)->getAI();
		if( ai == NULL || !ai->hasTunnelTrip() )
			++residents;
	}
	return residents;
}

// ------------------------------------------------------------------------
/** Whoever moves - a player's selection, a computer's wave, a unit falling back - decides once for
		the whole group, from its middle: deciding member by member split a selection, the back of it
		walking while the front went underground.  A network with no free place is not looked at at all,
		whatever fills it was put there to stay.  One free place is enough for any group, since a unit
		passing through leaves by the far mouth the frame after it arrives: sixteen went through one place
		as fast as through ten (tunnelqueue.txt against tunnelshortcut.txt).

		Any way through that is shorter than the walk is taken.  It used to have to come in under 70% of
		it, and a rally point with a tunnel beside the factory and another beside the point still walked.
		The legs to and from the tunnels are straight lines.  `walk` is the caller's to measure: the
		straight line for a move order, the length of the path for a wave that follows one.
		ponytail: straight lines, not path lengths; a tunnel across a river the walk has to go round
		looks no better than one across open ground.  A path search when that matters. */
Object *TunnelTracker::findTunnelShortcut( const Coord3D *from, const Coord3D *to, Real walk ) const
{
	if( (Int)getContainCount() >= getContainMax() )
		return NULL;

	Object *entrance = findQuietTunnelNear( from );
	Object *exit = findQuietTunnelNear( to );
	if( entrance == NULL || exit == entrance )
		return NULL;

	const Real toEntrance = (Real)sqrt( ThePartitionManager->getDistanceSquared( entrance, from, FROM_CENTER_2D ) );
	const Real fromExit = (Real)sqrt( ThePartitionManager->getDistanceSquared( exit, to, FROM_CENTER_2D ) );
	if( toEntrance + fromExit >= walk )
		return NULL;

	return entrance;
}

// ------------------------------------------------------------------------
void TunnelTracker::destroyObject( Object *obj, void * )
{
	// Now that tunnels consider ContainedBy to be "the tunnel you entered", I need to say goodbye
	// llike other contain types so they don't look us up on their deletion and crash
	obj->onRemovedFrom( obj->getContainedBy() );
	TheGameLogic->destroyObject( obj );
}

// ------------------------------------------------------------------------
	// heal all the objects within the tunnel system using the iterateContained function.
	// this used to be driven by every TunnelContain's own update, so a network of five tunnels
	// healed everyone inside it five times a frame - the more tunnels you owned, the faster your
	// units healed.  The player drives it now, once.
void TunnelTracker::healObjects()
{
	if( m_containListSize == 0 )
		return;

	Real framesForFullHeal = getFramesForFullHeal();
	if( framesForFullHeal <= 0.0f )
		return;

	iterateContained(healObject, &framesForFullHeal, FALSE);
}

// ------------------------------------------------------------------------
	// the shortest full-heal time of the tunnels still standing
Real TunnelTracker::getFramesForFullHeal() const
{
	Real minFrames = 0.0f;

	for( std::list<ObjectID>::const_iterator it = m_tunnelIDs.begin(); it != m_tunnelIDs.end(); ++it )
	{
		const Object *tunnelObj = TheGameLogic->findObjectByID( *it );
		if( tunnelObj == NULL )
			continue;

		const ContainModuleInterface *contain = tunnelObj->getContain();
		DEBUG_ASSERTCRASH( contain != NULL, ("a tunnel with no contain module") );
		if( !contain->isTunnelContain() )
			continue;

		const Real frames = ((const TunnelContain *)contain)->getFullTimeForHeal();
		if( minFrames == 0.0f || frames < minFrames )
			minFrames = frames;
	}

	return minFrames;
}

// ------------------------------------------------------------------------
	// heal one object within the tunnel network system
void TunnelTracker::healObject( Object *obj, void *frames)
{
	
	// a unit only passing through on its way somewhere was not sent in to be mended; the heal is for
	// the ones the player put inside
	const AIUpdateInterface *ai = obj->getAI();
	if( ai != NULL && ai->hasTunnelTrip() )
		return;

	//get the number of frames to heal
	Real *framesForFullHeal = (Real*)frames;

	// setup the healing damageInfo structure with all but the amount
	DamageInfo healInfo;
	healInfo.in.m_damageType = DAMAGE_HEALING;
	healInfo.in.m_deathType = DEATH_NONE;
	//healInfo.in.m_sourceID = getObject()->getID();

	// get body module of the thing to heal
	BodyModuleInterface *body = obj->getBodyModule();

	// if we've been in here long enough ... set our health to max
	if( TheGameLogic->getFrame() - obj->getContainedByFrame() >= *framesForFullHeal )
	{
	
		// set the amount to max just to be sure we're at the top
		healInfo.in.m_amount = body->getMaxHealth();
		
		// set max health
		body->attemptHealing( &healInfo );

	}  // end if
	else
	{
		//
		// given the *whole* time it would take to heal this object, lets pretend that the
		// object is at zero health ... and give it a sliver of health as if it were at 0 health
		// and would be fully healed at 'framesForFullHeal'
		//
		healInfo.in.m_amount = body->getMaxHealth() / *framesForFullHeal;

		// do the healing
		body->attemptHealing( &healInfo );

	}  // end else
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TunnelTracker::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TunnelTracker::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// tunnel object id list
	xfer->xferSTLObjectIDList( &m_tunnelIDs );

	// contain list count
	xfer->xferInt( &m_containListSize );

	// contain list data
	ObjectID objectID;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		ContainedItemsList::const_iterator it;

		for( it = m_containList.begin(); it != m_containList.end(); ++it )
		{

			objectID = (*it)->getID();
			xfer->xferObjectID( &objectID );

		}  // end for, it

	}  // end if, save
	else
	{

		for( UnsignedShort i = 0; i < m_containListSize; ++i )
		{

			xfer->xferObjectID( &objectID );
			m_xferContainList.push_back( objectID );

		}  // end for, i

	}  // end else, load

	// tunnel count
	xfer->xferUnsignedInt( &m_tunnelCount );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void TunnelTracker::loadPostProcess( void )
{

	// sanity, the contain list should be empty until we post process the id list
	if( m_containList.size() != 0 )
	{

		DEBUG_CRASH(( "TunnelTracker::loadPostProcess - m_containList should be empty but is not\n" ));
		throw SC_INVALID_DATA;

	}  // end if

	// translate each object ids on the xferContainList into real object pointers in the contain list
	Object *obj;
	std::list< ObjectID >::const_iterator it;
	for( it = m_xferContainList.begin(); it != m_xferContainList.end(); ++it )
	{

		obj = TheGameLogic->findObjectByID( *it );
		if( obj == NULL )
		{

			DEBUG_CRASH(( "TunnelTracker::loadPostProcess - Unable to find object ID '%d'\n", *it ));
			throw SC_INVALID_DATA;

		}  // end if

		// push on the back of the contain list
		m_containList.push_back( obj );

		// Crap.  This is in OpenContain as a fix, but not here.
		{
			// remove object from its group (if any)
			obj->leaveGroup();
			
			// remove rider from partition manager
			ThePartitionManager->unRegisterObject( obj );
			
			// hide the drawable associated with rider
			if( obj->getDrawable() )
				obj->getDrawable()->setDrawableHidden( true );
			
			// remove object from pathfind map
			if( TheAI )
				TheAI->pathfinder()->removeObjectFromPathfindMap( obj );
			
		}
	}  // end for, it

	// we're done with the xfer contain list now
	m_xferContainList.clear();

}  // end loadPostProcess
