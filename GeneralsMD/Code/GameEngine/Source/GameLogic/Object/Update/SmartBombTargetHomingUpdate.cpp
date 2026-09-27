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

// FILE: SmartBombTargetHomingUpdate.cpp /////////////////////////////////////////////////////////////////////////
// Author: Mark Lorenzen, July 2003
// Desc:   Update that will fudge a falling object's position just slightly, to make it find its target better
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/SmartBombTargetHomingUpdate.h"
#include "Lib/FixBoundary.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SmartBombTargetHomingUpdate::SmartBombTargetHomingUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
  m_targetReceived = FALSE;
  m_target.zero();
	setWakeFrame( getObject(), UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SmartBombTargetHomingUpdate::~SmartBombTargetHomingUpdate( void )
{
}
//#define CRISS_CROSS_GEOMETRY


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SmartBombTargetHomingUpdate::SetTargetPosition( const Coord3D& target )
{

  // Ensure that we have been passed a real-world location
  DEBUG_ASSERTCRASH( target.length() > 0.0f, ("SmartBombTargetHomingUpdate::SetTargetPosition() received a zero coord") );
  if ( ! (target.length() > 0.0f) )
    return;

  m_target.x = target.x;
  m_target.y = target.y;
  m_target.z = target.z;

  m_targetReceived = TRUE;

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SmartBombTargetHomingUpdate::update( void )
{

  if ( ! m_targetReceived )
    return UPDATE_SLEEP_NONE;

	const SmartBombTargetHomingUpdateModuleData* d = getSmartBombTargetHomingUpdateModuleData();
  if ( ! d )
    return UPDATE_SLEEP_NONE;

  Object *self = getObject();
  if ( ! self )
    return UPDATE_SLEEP_NONE;


  if ( ! self->isSignificantlyAboveTerrain() )
    return UPDATE_SLEEP_NONE;

  FCoord3D pos = *self->getPositionFix();

  Fix statusCoeff = fixMax( Fix( 0 ), fixMin( Fix( 1 ), d->m_courseCorrectionScalar ) );
  Fix targetCoeff = Fix( 1 ) - statusCoeff;

  // P8: the target is saved in float
  pos.x = fixFromReal( m_target.x ) * targetCoeff + pos.x * statusCoeff;
  pos.y = fixFromReal( m_target.y ) * targetCoeff + pos.y * statusCoeff;

  self->setPositionFix( &pos );

  return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SmartBombTargetHomingUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the target, or a bomb falling at save time drops unguided after the load */
// ------------------------------------------------------------------------------------------------
void SmartBombTargetHomingUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	if( version >= 2 )
	{
		xfer->xferBool( &m_targetReceived );
		xfer->xferCoord3D( &m_target );
	}


}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SmartBombTargetHomingUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
