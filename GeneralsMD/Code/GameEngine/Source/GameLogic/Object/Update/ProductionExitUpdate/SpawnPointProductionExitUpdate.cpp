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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: SpawnPointProductionExitUpdate.cpp /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, April 2002
// Desc:		Hand off produced Units to me so I can Exit them into the world with my specific style
//					This instance puts guys at named bones.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameState.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/SpawnPointProductionExitUpdate.h"

#include "WWMath/matrix3d.h"		///< @todo Replace with our own matrix library

//-------------------------------------------------------------------------------------------------
SpawnPointProductionExitUpdate::SpawnPointProductionExitUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_bonesInitialized = FALSE;
	m_spawnPointCount = 0;
	for( Int positionIndex = 0; positionIndex < MAX_SPAWN_POINTS; positionIndex++ )
	{
		m_worldCoordSpawnPoints[positionIndex].zero();
		m_worldAngleSpawnPoints[positionIndex] = 0.0f;
		m_spawnPointOccupier[positionIndex] = INVALID_ID;
	}
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
SpawnPointProductionExitUpdate::~SpawnPointProductionExitUpdate()
{
}

//-------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::exitObjectViaDoor( Object *newObj, ExitDoorType exitDoor )
{
	DEBUG_ASSERTCRASH(exitDoor == DOOR_1, ("multiple exit doors not supported here"));

	if( !m_bonesInitialized )
		initializeBonePositions();

	Object *creationObject = getObject();
	if (creationObject)
	{
		// positionIndex is used after the loop; VC6 for-scope let it escape.
		Int positionIndex;
		for( positionIndex = 0; positionIndex < m_spawnPointCount; positionIndex++ )
		{
			if( m_spawnPointOccupier[positionIndex] == INVALID_ID )
				break;
		}
		if( positionIndex == m_spawnPointCount )
		{
			DEBUG_CRASH( ("A SpawnPoint exit thought it had room but then failed") );
			return;
		}

		Coord3D createPoint;
		Real createAngle;

		// Get the location from our bone array
		createPoint = m_worldCoordSpawnPoints[positionIndex];

		// make sure the point is on the terrain
		createPoint.z = TheTerrainLogic ? TheTerrainLogic->getLayerHeight( createPoint.x, createPoint.y, creationObject->getLayer()) : 0.0f;

		// get the angle
		createAngle = m_worldAngleSpawnPoints[positionIndex];

		// record him as taking this spot
		m_spawnPointOccupier[positionIndex] = newObj->getID();

		// put him there
		newObj->setPosition( &createPoint );
		newObj->setOrientation( createAngle );
		newObj->setLayer(creationObject->getLayer());

		/** @todo This really should be automatically wrapped up in an actication sequence
		for objects in general */
		// tell the AI about it
		TheAI->pathfinder()->addObjectToPathfindMap( newObj );

		// You are stuck here, little man.
		newObj->setDisabled( DISABLED_HELD );
	}
}

//-------------------------------------------------------------------------------------------------
ExitDoorType SpawnPointProductionExitUpdate::reserveDoorForExit( const ThingTemplate* objType, Object *specificObject )
{
	if( !m_bonesInitialized )
		initializeBonePositions();

	if( !m_bonesInitialized )
		return DOOR_NONE_AVAILABLE; // Init failure

	revalidateOccupiers();

	for( Int positionIndex = 0; positionIndex < m_spawnPointCount; positionIndex++ )
	{
		if( m_spawnPointOccupier[positionIndex] == INVALID_ID )
			return DOOR_1;
	}

	return DOOR_NONE_AVAILABLE;
}

//-------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::unreserveDoorForExit( ExitDoorType exitDoor )
{
	/* nothing */
}

//-------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::initializeBonePositions()
{
	Object *me = getObject();
	Drawable *myDrawable = me->getDrawable();

	// This fundamental failure will result in this never ever thinking it is free
	if( myDrawable == NULL )
		return;

	Matrix3D boneTransforms[MAX_SPAWN_POINTS];
	// matrixIndex is used after the loop; VC6 for-scope let it escape.
	Int matrixIndex;
	for( matrixIndex = 0; matrixIndex < MAX_SPAWN_POINTS; matrixIndex++ )
		boneTransforms[matrixIndex].Make_Identity();

	// Get all the bones of the right name
	const SpawnPointProductionExitUpdateModuleData* md = getSpawnPointProductionExitUpdateModuleData();
	m_spawnPointCount = myDrawable->getPristineBonePositions( md->m_spawnPointBoneNameData.str(), 1, NULL, boneTransforms, MAX_SPAWN_POINTS );

	for( matrixIndex = 0; matrixIndex < m_spawnPointCount; matrixIndex++ )
	{
		Matrix3D *currentTransform = &(boneTransforms[matrixIndex]);
		// Convert their matrix one by one
		me->convertBonePosToWorldPos( NULL, currentTransform, NULL, currentTransform );

		// Then save the world coord and angle
		m_worldCoordSpawnPoints[matrixIndex].x = currentTransform->Get_X_Translation();
		m_worldCoordSpawnPoints[matrixIndex].y = currentTransform->Get_Y_Translation();
		m_worldCoordSpawnPoints[matrixIndex].z = 0; //set at creation time

		m_worldAngleSpawnPoints[matrixIndex] = currentTransform->Get_Z_Rotation();
	}

	m_bonesInitialized = TRUE;
}

//-------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::revalidateOccupiers()
{
	for( Int positionIndex = 0; positionIndex < m_spawnPointCount; positionIndex++ )
	{
		if( m_spawnPointOccupier[positionIndex] == INVALID_ID )
			continue;

		if( TheGameLogic->findObjectByID( m_spawnPointOccupier[positionIndex] ) == NULL )
			m_spawnPointOccupier[positionIndex] = INVALID_ID;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: The spawn points themselves.  They are read off the drawable's bones the first time a door is
	*    asked for, in whatever model state the drawable is in then, and version 1 left them to be read
	*    again after a load, from the loaded drawable's state: a loaded game could put its spawn at
	*    other points than the game that was saved, or have none. */
// ------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	if( version >= 2 )
	{
		xfer->xferBool( &m_bonesInitialized );
		xfer->xferInt( &m_spawnPointCount );
		if( m_spawnPointCount < 0 || m_spawnPointCount > MAX_SPAWN_POINTS )
			throw SC_INVALID_DATA;
		for( Int i = 0; i < m_spawnPointCount; ++i )
		{
			xfer->xferCoord3D( &m_worldCoordSpawnPoints[ i ] );
			xfer->xferReal( &m_worldAngleSpawnPoints[ i ] );
		}
	}
	// version 1: the bone data is read again the first time a door is asked for

	// spawn point occupants
	xfer->xferUser( &m_spawnPointOccupier, sizeof( ObjectID ) * MAX_SPAWN_POINTS );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SpawnPointProductionExitUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
