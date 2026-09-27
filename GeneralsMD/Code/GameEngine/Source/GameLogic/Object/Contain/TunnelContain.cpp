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

// FILE: TunnelContain.cpp ////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, March 2002
// Desc:   A version of OpenContain that overrides where the passengers are stored: the Owning Player's
//					TunnelTracker.  All queries about capacity and contents are also redirected.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Xfer.h"
#include "Lib/FixBoundary.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/TunnelContain.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TunnelContain::TunnelContain( Thing *thing, const ModuleData* moduleData ) : OpenContain( thing, moduleData )
{
	m_needToRunOnBuildComplete = true;
	m_isCurrentlyRegistered = FALSE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
TunnelContain::~TunnelContain()
{
}

TunnelTracker *TunnelContain::getTunnelTracker( void ) const
{
	Player *owningPlayer = getObject()->getControllingPlayer();
	return owningPlayer ? owningPlayer->getTunnelSystem() : NULL;
}

void TunnelContain::addToContainList( Object *obj )
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->addToContainList( obj );
}

//-------------------------------------------------------------------------------------------------
/** Remove 'obj' from the m_containList of objects in this module.
	* This will trigger an onRemoving event for the object that this module
	* is a part of and an onRemovedFrom event for the object being removed */
//-------------------------------------------------------------------------------------------------
void TunnelContain::removeFromContain( Object *obj, Bool exposeStealthUnits )
{

	// sanity
	if( obj == NULL )
		return;

	// trigger an onRemoving event for 'm_object' no longer containing 'itemToRemove->m_object'
	if( getObject()->getContain() )
	{
		getObject()->getContain()->onRemoving( obj );
	}
			
	// trigger an onRemovedFrom event for 'remove'
	obj->onRemovedFrom( getObject() );

	//
	// we can only remove this object from the contains list of this module if
	// it is actually contained by this module
	//
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return; //game tear down.  We do the onRemove* stuff first because this is allowed to fail but that still needs to be done

	if( ! tunnelTracker->isInContainer( obj ) )
	{
		return;
	}

	tunnelTracker->removeFromContain( obj, exposeStealthUnits );

}



//--------------------------------------------------------------------------------------------------------
/** Force all contained objects in the contained list to exit, and kick them in the pants on the way out*/
//--------------------------------------------------------------------------------------------------------
void TunnelContain::harmAndForceExitAllContained( DamageInfo *info )
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return;

	// A tunnel still being built joins the network the moment it is placed, but nobody can be inside
	// it: a Bunker Buster on the scaffold threw out and hurt everyone in every tunnel on the map.
	if( getObject()->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return;

	const ContainedItemsList *fullList = tunnelTracker->getContainedItemsList();

	// Everyone is out before anyone is harmed, as in OpenContain. A demo rider's blast can kill the last
	// tunnel, whose death kills everyone still inside instead of putting them out harmed, and a second
	// bunker buster set off by that blast finds the list already empty instead of walking it under the first.
	std::vector<Object*> exited;
	while( !fullList->empty() )
	{
		Object *obj = fullList->front();
		removeFromContain( obj, true );
		exited.push_back( obj );
	}

	for( std::vector<Object*>::iterator it = exited.begin(); it != exited.end(); ++it )
		(*it)->attemptDamage( info );

}  // end removeAllContained


//-------------------------------------------------------------------------------------------------
/** Remove all contained objects from the contained list */
//-------------------------------------------------------------------------------------------------
void TunnelContain::killAllContained( void )
{
	//
	// Take the tunnel system's list away from it before killing anything: a rider that deals fatal
	// damage on death kills the tunnel, and the tunnel's death walks this same list again while the
	// outer call is still iterating it.  Neutron Shells on a tunnel full of Terrorists does it.
	//
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return;

	// the scaffold holds nobody; Neutron Shells on one killed the whole network (see above)
	if( getObject()->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		return;

	ContainedItemsList list;
	tunnelTracker->swapContainedItemsList( list );

	ContainedItemsList::iterator it = list.begin();
	while( it != list.end() )
	{
		Object *obj = *it++;
		DEBUG_ASSERTCRASH( obj, ("Contain list must not contain NULL element") );

		removeFromContain( obj, true );
		obj->kill();
	}
}
//-------------------------------------------------------------------------------------------------
/** Remove all contained objects from the contained list */
//-------------------------------------------------------------------------------------------------
void TunnelContain::removeAllContained( Bool exposeStealthUnits )
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return;

	const ContainedItemsList *fullList = tunnelTracker->getContainedItemsList();

	Object *obj;
	ContainedItemsList::const_iterator it;
	it = (*fullList).begin();
	while( it != (*fullList).end() )
	{
		obj = *it;
		it++;
		removeFromContain( obj, exposeStealthUnits );
	}
}

//-------------------------------------------------------------------------------------------------
/** Iterate the contained list and call the callback on each of the objects */
//-------------------------------------------------------------------------------------------------
void TunnelContain::iterateContained( ContainIterateFunc func, void *userData, Bool reverse )
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->iterateContained( func, userData, reverse );
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onContaining( Object *obj, Bool wasSelected )
{
	OpenContain::onContaining( obj, wasSelected );

	// objects inside a building are held
	obj->setDisabled( DISABLED_HELD );

	obj->getControllingPlayer()->getAcademyStats()->recordUnitEnteredTunnelNetwork();
  


  
  obj->handlePartitionCellMaintenance();


}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onRemoving( Object *obj ) 
{
	OpenContain::onRemoving(obj);

	// object is no longer held inside a garrisoned building
	obj->clearDisabled( DISABLED_HELD );

	/* Put the object back in the world at the tunnel's position.

		 This used to register it with the partition manager and unhide its drawable by hand, which
		 does most of what is needed and skips the rest: whatever the object had attached to it - the
		 flame on a flame trooper, a rider's own drawable - stayed hidden, so a unit that came out of a
		 tunnel inside the fog carried invisible attachments around with it.  addOrRemoveObjFromWorld
		 is the one that puts every piece of an object back. */
	obj->setPositionFix( getObject()->getPositionFix() );
	obj->setSafeOcclusionFrame( TheGameLogic->getFrame() + obj->getTemplate()->getOcclusionDelay() );
	addOrRemoveObjFromWorld( obj, TRUE );

	doUnloadSound();
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onSelling()
{
	// A TunnelContain tells everyone to leave if this is the last tunnel
	Player *owningPlayer = getObject()->getControllingPlayer();
	if( owningPlayer == NULL )
		return;
	TunnelTracker *tunnelTracker = owningPlayer->getTunnelSystem();
	if( tunnelTracker == NULL )
		return;
	
	// We are the last tunnel, so kick everyone out.  This makes tunnels act like Palace and Bunker
	// rather than killing the occupants as if the last tunnel died.
	if( tunnelTracker->friend_getTunnelCount() == 1 )
		removeAllContained(FALSE);// Can't be order to exit, as I have no time to organize their exits.
	// If they don't go right now, I will delete them in a moment

	// Unregister after the kick out, or else the unregistering will activate a cavein-kill.
	// We need to do this in case someone sells their last two tunnels at the same time.
	if( m_isCurrentlyRegistered )
	{
		tunnelTracker->onTunnelDestroyed( getObject() );
		m_isCurrentlyRegistered = FALSE;
	}
}

//-------------------------------------------------------------------------------------------------
// The whole tunnel network shares one passenger list, so a passenger belongs to the mouth it walked
// into, not to the one that was told to unload. Either answer means it may leave through this one.
Bool TunnelContain::isContained( const Object *obj ) const
{
	if( OpenContain::isContained( obj ) )
		return TRUE;

	const ContainedItemsList *items = getContainedItemsList();
	return items != NULL && std::find( items->begin(), items->end(), obj ) != items->end();
}

//-------------------------------------------------------------------------------------------------
Bool TunnelContain::isValidContainerFor(const Object* obj, Bool checkCapacity) const
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	return tunnelTracker ? tunnelTracker->isValidContainerFor( obj, checkCapacity ) : FALSE;
}

UnsignedInt TunnelContain::getContainCount() const
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	return tunnelTracker ? tunnelTracker->getContainCount() : 0;
}

Int TunnelContain::getContainMax( void ) const 
{ 
	TunnelTracker *tunnelTracker = getTunnelTracker();
	return tunnelTracker ? tunnelTracker->getContainMax() : 0;
}

//-------------------------------------------------------------------------------------------------
Bool TunnelContain::getContainerPipsToShow( Int& numTotal, Int& numFull )
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	numTotal = getContainMax();
	numFull = tunnelTracker ? tunnelTracker->getResidentCount() : 0;
	return true;
}

const ContainedItemsList* TunnelContain::getContainedItemsList() const
{
	TunnelTracker *tunnelTracker = getTunnelTracker();
	return tunnelTracker ? tunnelTracker->getContainedItemsList() : NULL;
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::orderAllPassengersToExit( CommandSourceType commandSource, Bool instantly )
{
	if( getTunnelTracker() == NULL )
		return;

	OpenContain::orderAllPassengersToExit( commandSource, instantly );
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::orderAllPassengersToIdle( CommandSourceType commandSource )
{
	if( getTunnelTracker() == NULL )
		return;

	OpenContain::orderAllPassengersToIdle( commandSource );
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////


//-------------------------------------------------------------------------------------------------
void TunnelContain::scatterToNearbyPosition(Object* obj)
{
	Object *theContainer = getObject();

	//
	// for now we will just set the position of the object that is being removed from us
	// at a random angle away from our center out some distance
	//
	
	//
	// pick an angle that is in the view of the current camera position so that
	// the thing will come out "toward" the player and they can see it
	// NOPE, can't do that ... all players screen angles will be different, unless
	// we maintain the angle of each players screen in the player structure or something
	//
	// the random draws stay float, there is no fixed point random yet  // P8
	Fix angle = fixFromReal( GameLogicRandomValueReal( 0.0f, 2.0f * PI ) );
//	angle = TheTacticalView->getAngle();
//	angle -= GameLogicRandomValueReal( PI / 3.0f, 2.0f * (PI / 3.0F) );

	Real minRadius = fixToReal( theContainer->getGeometryInfo().getBoundingCircleRadiusFix() );
	Real maxRadius = minRadius + minRadius / 2.0f;
	const FCoord3D *containerPos = theContainer->getPositionFix();
	Fix dist = fixFromReal( GameLogicRandomValueReal( minRadius, maxRadius ) );

	FCoord3D pos;
	pos.x = dist * fixCos( angle ) + containerPos->x;
	pos.y = dist * fixSin( angle ) + containerPos->y;
	pos.z = TheTerrainLogic->getGroundHeightFix( pos.x, pos.y );

	// set orientation
	obj->setOrientationFix( angle );

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if( ai )
	{
		// set position of the object at center of building and move them toward pos
		obj->setPositionFix( containerPos );
		ai->ignoreObstacle(theContainer);
		Coord3D goal = pos.toCoord3D();	// P4: the AI move calls are float
 		ai->aiMoveToPosition( &goal, CMD_FROM_AI );

	}  // end if
	else
	{

		// no ai, just set position at the target pos
		obj->setPositionFix( &pos );

	}  // end else
}

//-------------------------------------------------------------------------------------------------
/** The die callback. */
//-------------------------------------------------------------------------------------------------
void TunnelContain::onDie( const DamageInfo * damageInfo )
{
	// override the onDie we inherit from OpenContain. no super call.
	if (!getTunnelContainModuleData()->m_dieMuxData.isDieApplicable(getObject(), damageInfo))
		return;

	if( !m_isCurrentlyRegistered )
		return;//it isn't registered as a tunnel

	Player *owningPlayer = getObject()->getControllingPlayer();
	if( owningPlayer == NULL )
		return;
	TunnelTracker *tunnelTracker = owningPlayer->getTunnelSystem();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->onTunnelDestroyed( getObject() );
	m_isCurrentlyRegistered = FALSE;
}  

//-------------------------------------------------------------------------------------------------
void TunnelContain::onDelete( void )
{
	// Being sold is a straight up delete.  no death

	if( !m_isCurrentlyRegistered )
		return;//it isn't registered as a tunnel

	Player *owningPlayer = getObject()->getControllingPlayer();
	if( owningPlayer == NULL )
		return;
	TunnelTracker *tunnelTracker = owningPlayer->getTunnelSystem();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->onTunnelDestroyed( getObject() );
	m_isCurrentlyRegistered = FALSE;
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onCreate( void )
{
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onObjectCreated()
{
	//Kris: July 29, 2003
	//Added this function to support the sneak attack (which doesn't call onBuildComplete).
	if( ! shouldDoOnBuildComplete() )
		return;

	m_needToRunOnBuildComplete = false;

	Player *owningPlayer = getObject()->getControllingPlayer();
	if( owningPlayer == NULL )
		return;
	TunnelTracker *tunnelTracker = owningPlayer->getTunnelSystem();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->onTunnelCreated( getObject() );
	m_isCurrentlyRegistered = TRUE;
}

//-------------------------------------------------------------------------------------------------
void TunnelContain::onBuildComplete( void )
{
	//Kris: July 29, 2003
	//Obsolete -- onObjectCreated handles it before this function gets called.
	/*
	if( ! shouldDoOnBuildComplete() )
		return;

	m_needToRunOnBuildComplete = false;

	Player *owningPlayer = getObject()->getControllingPlayer();
	if( owningPlayer == NULL )
		return;
	TunnelTracker *tunnelTracker = owningPlayer->getTunnelSystem();
	if( tunnelTracker == NULL )
		return;

	tunnelTracker->onTunnelCreated( getObject() );
	m_isCurrentlyRegistered = TRUE;
	*/
} 

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void TunnelContain::onCapture( Player *oldOwner, Player *newOwner )
{
	if( m_isCurrentlyRegistered )
	{
		TunnelTracker *oldTunnelTracker = oldOwner->getTunnelSystem();
		if( oldTunnelTracker )
		{
			DEBUG_ASSERTCRASH( oldTunnelTracker->getContainCount() == 0, ("You shouldn't force a capture of a Tunnel with people in it. Future ExitFromContainer scripts will fail."));
			oldTunnelTracker->onTunnelDestroyed(getObject());
		}

		TunnelTracker *newTunnelTracker = newOwner->getTunnelSystem();
		if( newTunnelTracker )
		{
			newTunnelTracker->onTunnelCreated(getObject());
		}
	}

	// extend base class
	OpenContain::onCapture( oldOwner, newOwner );
}

// ------------------------------------------------------------------------------------------------
/** Per frame update */
// ------------------------------------------------------------------------------------------------
UpdateSleepTime TunnelContain::update( void )
{
	// the healing of the units within the tunnel system is driven by the player, once a frame for
	// the whole network - see TunnelTracker::healObjects.
	OpenContain::update();

	Object *obj = getObject();
	Player *controllingPlayer = NULL;
	if (obj)
	{
		controllingPlayer = obj->getControllingPlayer();
	}
	if (controllingPlayer)
	{
		TunnelTracker *tunnelSystem = controllingPlayer->getTunnelSystem();

		// check for attacked.
		BodyModuleInterface *body = obj->getBodyModule();
		if (body) {
			const DamageInfo *info = body->getLastDamageInfo();
			if (info) {
				if (body->getLastDamageTimestamp() + LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame()) {
					// winner.
					ObjectID attackerID = info->in.m_sourceID;
					Object *attacker = TheGameLogic->findObjectByID(attackerID);
					if( attacker )
					{
						if (obj->getRelationship(attacker) == ENEMIES) {
							tunnelSystem->updateNemesis(attacker);
						}
					}
				}
			}
		}
	}
	return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void TunnelContain::crc( Xfer *xfer )
{

	// extend base class
	OpenContain::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void TunnelContain::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	OpenContain::xfer( xfer );

	// need to run on build complete
	xfer->xferBool( &m_needToRunOnBuildComplete );

	// Currently registered with owning player
	xfer->xferBool( &m_isCurrentlyRegistered );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void TunnelContain::loadPostProcess( void )
{

	// extend base class
	OpenContain::loadPostProcess();

}  // end loadPostProcess
