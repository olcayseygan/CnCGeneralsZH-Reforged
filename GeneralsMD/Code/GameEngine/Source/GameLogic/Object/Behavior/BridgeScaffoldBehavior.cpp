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

// FILE: BridgeScaffoldBehavior.cpp ///////////////////////////////////////////////////////////////
// Author: Colin Day, September 2002
// Desc:   Bridge scaffold
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/Xfer.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BridgeScaffoldBehavior.h"
#include "Lib/FixBoundary.h"

static FCoord3D toFix( const Coord3D &c )
{
	FCoord3D f;
	f.set( fixFromReal( c.x ), fixFromReal( c.y ), fixFromReal( c.z ) );
	return f;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
BridgeScaffoldBehavior::BridgeScaffoldBehavior( Thing *thing, const ModuleData *moduleData )
											: UpdateModule( thing, moduleData )
{

	m_targetMotion = STM_STILL;
	m_createPos.zero();
	m_riseToPos.zero();
	m_buildPos.zero();
	m_targetPos.zero();
	m_lateralSpeed = 1.0f;
	m_verticalSpeed = 1.0f;

}  // end BridgeScaffoldBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
BridgeScaffoldBehavior::~BridgeScaffoldBehavior( void )
{

}  // end ~BridgeScaffoldBehavior

// ------------------------------------------------------------------------------------------------
/** Set all of the target positions that we're care about as a moving scaffold object */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::setPositions( const Coord3D *createPos,
																					 const Coord3D *riseToPos,
																					 const Coord3D *buildPos )
{

	m_createPos = *createPos;
	m_riseToPos = *riseToPos;
	m_buildPos = *buildPos;

}  // end setPositions

// ------------------------------------------------------------------------------------------------
/** Set us moving to the right target position for the requested motion type */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::setMotion( ScaffoldTargetMotion targetMotion ) 
{

	// save the target motion type
	m_targetMotion = targetMotion;

	// given the target motion, pick a destination target
	switch( m_targetMotion )
	{

		// --------------------------------------------------------------------------------------------
		case STM_RISE: 
		case STM_TEAR_DOWN_ACROSS:
			m_targetPos = m_riseToPos; 
			break;

		// --------------------------------------------------------------------------------------------
		case STM_BUILD_ACROSS:
			m_targetPos = m_buildPos;
			break;

		// --------------------------------------------------------------------------------------------
		case STM_SINK:
			m_targetPos = m_createPos;
			break;

	}  // end switch

}  // end setMotion

// ------------------------------------------------------------------------------------------------
/** Whatever our current state of motion is, reverse it */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::reverseMotion( void )
{

	switch( m_targetMotion )
	{

		case STM_STILL:
			setMotion( STM_TEAR_DOWN_ACROSS );
			break;

		case STM_RISE:
			setMotion( STM_SINK );
			break;

		case STM_BUILD_ACROSS:
			setMotion( STM_TEAR_DOWN_ACROSS );
			break;

		case STM_TEAR_DOWN_ACROSS:
			setMotion( STM_BUILD_ACROSS );
			break;

		case STM_SINK:
			setMotion( STM_RISE );
			break;

	}  // end switch

}  // end reverseMotion

// ------------------------------------------------------------------------------------------------
/** The update method */
// ------------------------------------------------------------------------------------------------
UpdateSleepTime BridgeScaffoldBehavior::update( void )
{

	// do nothing if we're not in motion
	if( m_targetMotion == STM_STILL )
		return UPDATE_SLEEP_NONE;

	// get our info
	Object *us = getObject();
	const FCoord3D ourPos = *us->getPositionFix();

	// the scaffold's positions and speeds are saved as float; the motion itself is fixed point
	const FCoord3D targetPos = toFix( m_targetPos );

	// compute direction vector from our position to the target position
	FCoord3D dirV = targetPos;
	dirV.sub( ourPos );

	// use normalized direction vector "v" to do the pulling movement
	FCoord3D v = dirV;
	Fix len = v.length();
	if( len != Fix( 0 ) )
		v.set( v.x / len, v.y / len, v.z / len );

	// depending on our motion type, we move at different speeds
	Fix topSpeed = Fix( 1 );
	const Coord3D *start = &m_createPos, *end = &m_createPos;
	switch( m_targetMotion )
	{

		case STM_RISE:
			topSpeed = fixFromReal( m_verticalSpeed );
			start = &m_createPos;
			end = &m_riseToPos;
			break;

		case STM_SINK:
			topSpeed = fixFromReal( m_verticalSpeed );
			start = &m_riseToPos;
			end = &m_createPos;
			break;

		case STM_BUILD_ACROSS:
			topSpeed = fixFromReal( m_lateralSpeed );
			start = &m_riseToPos;
			end = &m_buildPos;
			break;

		case STM_TEAR_DOWN_ACROSS:
			topSpeed = fixFromReal( m_lateralSpeed );
			start = &m_buildPos;
			end = &m_riseToPos;
			break;

	}  // end switch

	// adjust speed so it's slower at the end of motion
	const FCoord3D fxEnd = toFix( *end );
	FCoord3D speedVector = fxEnd;
	speedVector.sub( toFix( *start ) );
	Fix totalDistance = speedVector.length() / Fix( 4 );
	speedVector = fxEnd;
	speedVector.sub( ourPos );
	Fix ourDistance = speedVector.length();
	// a zero-length leg divided by zero in float and ran at top speed
	Fix speed = totalDistance > Fix( 0 ) ? (ourDistance / totalDistance) * topSpeed : topSpeed;
	Fix minSpeed = topSpeed * 0.08_fx;
	if( speed < minSpeed )
		speed = minSpeed;
	if( speed > topSpeed )
		speed = topSpeed;

	//
	// make sure that speed can't get so incredibly small that we never finish our
	// movement no matter what the speed and distance are
	//
	if( speed < 0.001_fx )
		speed = 0.001_fx;

	// compute the new position given the speed
	FCoord3D newPos = v;
	newPos.scale( speed );
	newPos.add( ourPos );

	//
	// will this new position push us beyond our target destination, we will take the vector
	// from the new position to the destination and the vector from our current present position
	// tot he destination and dot them togehter ... if the result is < 0 then we have will
	// overshoot the distance if we use the new position
	//
	FCoord3D tooFarVector = targetPos;
	tooFarVector.sub( newPos );
	if( tooFarVector.x * dirV.x + tooFarVector.y * dirV.y + tooFarVector.z * dirV.z <= Fix( 0 ) )
	{

		// use the destination position
		newPos = targetPos;

		//
		// we have reached our target position, switch motion to the next position in 
		// the chain (which may be stay still and don't move anymore)
		//
		switch( m_targetMotion )
		{

			case STM_RISE: setMotion( STM_BUILD_ACROSS ); break;
			case STM_BUILD_ACROSS: setMotion( STM_STILL ); break;
			case STM_TEAR_DOWN_ACROSS: setMotion( STM_SINK ); break;

			case STM_SINK:
			{

				// we are done with a sinking motion, destroy the scaffold object as our job is done
				TheGameLogic->destroyObject( us );
				break;

			}  // end case

		}  // end switch

	}  // end if

	// set the new position
	us->setPositionFix( &newPos );

	// do not sleep
	return UPDATE_SLEEP_NONE;

}  // end update

// ------------------------------------------------------------------------------------------------
/** STATIC MEMBER:
	* Helper function to retrieve a bridge scaffold interface from an object if one is present */
// ------------------------------------------------------------------------------------------------
BridgeScaffoldBehaviorInterface *BridgeScaffoldBehavior::getBridgeScaffoldBehaviorInterfaceFromObject( Object *obj )
{

	// santiy
	if( obj == NULL )
		return NULL;

	// get the bridge tower behavior interface
	BridgeScaffoldBehaviorInterface *bridgeScaffoldInterface = NULL;
	BehaviorModule **bmi;
	for( bmi = obj->getBehaviorModules(); *bmi; ++bmi )
	{

		bridgeScaffoldInterface = (*bmi)->getBridgeScaffoldBehaviorInterface();
		if( bridgeScaffoldInterface )
			return bridgeScaffoldInterface;

	}  // end for bmi

	// interface not found
	return NULL;

}  // end getBridgeScaffoldBehaviorInterfaceFromObject

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// target motion
	xfer->xferUser( &m_targetMotion, sizeof( ScaffoldTargetMotion ) );

	// create pos
	xfer->xferCoord3D( &m_createPos );

	// rise to pos
	xfer->xferCoord3D( &m_riseToPos );

	// build pos
	xfer->xferCoord3D( &m_buildPos );

	// lateral speed
	xfer->xferReal( &m_lateralSpeed );

	// vertical speed
	xfer->xferReal( &m_verticalSpeed );

	// current target pos
	xfer->xferCoord3D( &m_targetPos );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void BridgeScaffoldBehavior::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
