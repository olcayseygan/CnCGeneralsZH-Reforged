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

// FILE: SupplyWarehouseDockUpdate.h /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   The action of this dock update is identifying who is docking and either taking Boxes away or giving them
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GlobalData.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/AIPathfind.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SupplyWarehouseDockUpdateModuleData::SupplyWarehouseDockUpdateModuleData( void )
{
	m_startingBoxesData = 1;
	m_deleteWhenEmpty = FALSE;

	//
	// Forty seconds a box standing on its own, and every cash building inside 250 feet takes a
	// share off that: one supply centre next to it halves the wait, two thirds it.  The numbers are
	// module data rather than constants so a mod can turn the whole thing off with RegenDelay = 0,
	// and the defaults apply to every warehouse the game already ships without touching one INI
	// file - which matters, because the INI files are in the multiplayer checksum.
	//
	m_regenDelay = 40 * LOGICFRAMES_PER_SECOND;
	m_regenRadius = Fix( 250 );
	m_regenMaxBoxes = -1;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void SupplyWarehouseDockUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	DockUpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "StartingBoxes",	INI::parseInt,	NULL, offsetof( SupplyWarehouseDockUpdateModuleData, m_startingBoxesData ) },
		{ "DeleteWhenEmpty",	INI::parseBool,	NULL, offsetof( SupplyWarehouseDockUpdateModuleData, m_deleteWhenEmpty ) },
		{ "RegenDelay",				INI::parseDurationUnsignedInt, NULL, offsetof( SupplyWarehouseDockUpdateModuleData, m_regenDelay ) },
		{ "RegenRadius",			INI::parseFix,	NULL, FIX_OFFSET( SupplyWarehouseDockUpdateModuleData, m_regenRadius ) },
		{ "RegenMaxBoxes",		INI::parseInt,	NULL, offsetof( SupplyWarehouseDockUpdateModuleData, m_regenMaxBoxes ) },
		{ 0, 0, 0, 0 }
	};

  p.add(dataFieldParse);

}  // end buildFieldParse


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SupplyWarehouseDockUpdate::SupplyWarehouseDockUpdate( Thing *thing, const ModuleData* moduleData ) : DockUpdate( thing, moduleData )
{
	m_boxesStored = getSupplyWarehouseDockUpdateModuleData()->m_startingBoxesData;
	m_nextRegenFrame = 0;
}

SupplyWarehouseDockUpdate::~SupplyWarehouseDockUpdate()
{
}

void SupplyWarehouseDockUpdate::onObjectCreated()
{
	Drawable *draw = getObject()->getDrawable();
	if( draw )
	{
		draw->updateDrawableSupplyStatus( getSupplyWarehouseDockUpdateModuleData()->m_startingBoxesData, m_boxesStored );
	}
}

//-------------------------------------------------------------------------------------------------
/** How many cash buildings stand close enough to work this supply point.
	*
	* Anybody's: the point is neutral ground and the question is how much of a base has grown around
	* it, not whose.  Two players expanding onto the same patch both make it richer and then have to
	* decide which of them keeps it, which is the whole reason for putting the money back. */
//-------------------------------------------------------------------------------------------------
Int SupplyWarehouseDockUpdate::countNearbyCollectors( void ) const
{
	const SupplyWarehouseDockUpdateModuleData *data = getSupplyWarehouseDockUpdateModuleData();
	if( data->m_regenRadius <= Fix( 0 ) || ThePartitionManager == NULL )
		return 0;

	Object *self = (Object *)getObject();

	PartitionFilterAlive fAlive;
	PartitionFilterSameMapStatus fMap( self );
	PartitionFilter *filters[] = { &fAlive, &fMap, NULL };

	SimpleObjectIterator *iter =
		ThePartitionManager->iterateObjectsInRangeFix( self, data->m_regenRadius, FROM_CENTER_2D, filters );
	MemoryPoolObjectHolder hold( iter );

	Int count = 0;
	for( Object *o = iter->first(); o; o = iter->next() )
	{
		if( o == self || !o->isKindOf( KINDOF_CASH_GENERATOR ) )
			continue;
		if( o->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || o->testStatus( OBJECT_STATUS_SOLD ) )
			continue;			// a half-built centre is not working anything yet
		++count;
	}

	return count;
}

//-------------------------------------------------------------------------------------------------
/** Put a box back now and then, faster the more of a base has grown around this point. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SupplyWarehouseDockUpdate::update()
{
	UpdateSleepTime ret = DockUpdate::update();

	const SupplyWarehouseDockUpdateModuleData *data = getSupplyWarehouseDockUpdateModuleData();
	if( data->m_regenDelay == 0 )
		return ret;

	Int ceiling = data->m_regenMaxBoxes;
	if( ceiling < 0 )
		ceiling = data->m_startingBoxesData;
	if( m_boxesStored >= ceiling )
	{
		m_nextRegenFrame = 0;			// full: the clock starts again when somebody takes one
		return ret;
	}

	const UnsignedInt now = TheGameLogic->getFrame();

	//
	// The wait is worked out once, when the clock starts - counting what is standing around is a
	// range query and a supply point does not need one every frame.  So a centre built while a box
	// is already on its way speeds up the box after it rather than the one in flight, which nobody
	// can see and which saves an eighth of a millisecond a frame on a map full of warehouses.
	//
	if( m_nextRegenFrame == 0 )
	{
		const Int collectors = countNearbyCollectors();
		const UnsignedInt wait = data->m_regenDelay / (UnsignedInt)( collectors + 1 );
		m_nextRegenFrame = now + ( wait > 0 ? wait : 1 );
	}

	if( now < m_nextRegenFrame )
		return ret;

	/* One box at a time was money nobody noticed: a hundred dollars every forty seconds is less
		 than a harvester earns on the trip it takes to go and collect it, so a mined-out point stayed
		 mined out as far as anybody playing was concerned. A refill is worth six hundred now, which
		 is a trip worth making and a reason to keep somebody standing on the ground. */
	const Int REGEN_CASH = 600;
	const Int boxValue = TheGlobalData->m_baseValuePerSupplyBox;
	Int boxes = ( boxValue > 0 ) ? ( REGEN_CASH / boxValue ) : 1;
	if( boxes < 1 )
		boxes = 1;
	if( m_boxesStored + boxes > ceiling )
		boxes = ceiling - m_boxesStored;

	m_boxesStored += boxes;
	m_nextRegenFrame = 0;			// re-measured against the new count next time round

	Drawable *draw = getObject()->getDrawable();
	if( draw )
		draw->updateDrawableSupplyStatus( data->m_startingBoxesData, m_boxesStored );

	return ret;
}

Bool SupplyWarehouseDockUpdate::action( Object* docker, Object *drone )
{
	if( m_boxesStored == 0 )
		return FALSE;

	// Make sure that the docker is at least reasonably close to the dock.
	// Basically, one bounding diameter of space or less between us.
	Real closeEnoughSqr = sqr(docker->getGeometryInfo().getBoundingCircleRadius()*2);
	Real curDistSqr = ThePartitionManager->getDistanceSquared(docker, getObject(), FROM_BOUNDINGSPHERE_2D);
	if (curDistSqr > closeEnoughSqr) {
		DEBUG_LOG(("Failing dock, dist %f, not close enough(%f).\n", sqrt(curDistSqr), sqrt(closeEnoughSqr)));
		// Make it twitch a little.
		Coord3D newPos = *docker->getPosition();
		Real range = 0.4*PATHFIND_CELL_SIZE_F;
		newPos.x += GameLogicRandomValue(-range, range);
		newPos.y += GameLogicRandomValue(-range, range);
		docker->setPosition(&newPos);
		return FALSE;  //not close enough.
	}
	
	// getAIUpdateInterface() can be NULL here and was dereferenced blind (the sibling
	// SupplyCenterDockUpdate does check). The no-room path below puts the box back.
	AIUpdateInterface *dockerAI = docker->getAIUpdateInterface();
	SupplyTruckAIInterface *ai = dockerAI ? dockerAI->getSupplyTruckAIInterface() : NULL;

	//
	// The whole load in one visit. A box per action delay left a worker parked here for MaxBoxes
	// times SupplyWarehouseActionDelay to collect what one trip pays, and the walk was already the
	// expensive half. The supply centre end has always emptied a truck in one go
	// (SupplyCenterDockUpdate's `while( loseOneBox() )`); this is the same on the pick-up end.
	//
	Int taken = 0;
	while( ai && m_boxesStored > 0 )
	{
		--m_boxesStored;// so the docker sees that I am shy by one box (or empty) from within his gainOneBox()
		if( !ai->gainOneBox( m_boxesStored ) )
		{
			++m_boxesStored; //take it back, since he had no room for it
											 //this is important so that I have one less boxes as perceived by the docker when he gains one
			break;
		}
		++taken;

		if( !supplyDockHasNextBox( m_boxesStored, ai->getNumberBoxes(), ai->getMaxBoxes() ) )
			break;
	}

	if( taken == 0 )
		return FALSE; //nobody to gain the boxes, or nowhere to put them

	if( m_boxesStored == 0 && getSupplyWarehouseDockUpdateModuleData()->m_deleteWhenEmpty )
	{
		TheGameLogic->destroyObject( getObject() );
		return FALSE; //Yer done.  And so am I.
	}

	Drawable *draw = getObject()->getDrawable();
	if( draw )
	{
		draw->updateDrawableSupplyStatus( getSupplyWarehouseDockUpdateModuleData()->m_startingBoxesData, m_boxesStored );
	}

	// A full docker, or an empty warehouse: either way there is no second action to wait for.
	return FALSE;
}

void SupplyWarehouseDockUpdate::setDockCrippled( Bool setting )
{
	// At this level, Crippling means I kill any activeDocker between enter and exit.
	if( setting )
	{
		if( m_activeDocker != INVALID_ID )
		{
			Object *victim = TheGameLogic->findObjectByID( m_activeDocker );
			if( victim )
			{
				if( m_dockerInside )
				{
					if( !victim->isUsingAirborneLocomotor() )
						victim->kill();
				}
				else
				{
					// Else, he was between Approach and Enter.  Lucky guy.  Tell him to stop, but then
					// remind him that he wants to try again later
					SupplyTruckAIInterface* supplyTruckAI = victim->getAI()->getSupplyTruckAIInterface();
					if( supplyTruckAI )
					{
						victim->getAI()->aiIdle( CMD_FROM_AI );
						supplyTruckAI->setForceWantingState( TRUE );
					}
				}
			}
		}
	}

	DockUpdate::setDockCrippled( setting );
}

Int SupplyWarehouseDockUpdate::getSupplyCashValue( void ) const
{
	return m_boxesStored * TheGlobalData->m_baseValuePerSupplyBox;
}

void SupplyWarehouseDockUpdate::setCashValue( Int cashValue )
{
	// A script can tell us our set value, and we need to figure out the boxes needed to provide that.
	m_boxesStored = ceil(cashValue / (float)TheGlobalData->m_baseValuePerSupplyBox);
	Drawable *draw = getObject()->getDrawable();
	if( draw )
	{
		draw->updateDrawableSupplyStatus( getSupplyWarehouseDockUpdateModuleData()->m_startingBoxesData, m_boxesStored );
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseDockUpdate::crc( Xfer *xfer )
{

	// extend base class
	DockUpdate::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseDockUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DockUpdate::xfer( xfer );

	// boxes stored
	xfer->xferInt( &m_boxesStored );

	// when the next box arrives (version 2)
	if( version >= 2 )
		xfer->xferUnsignedInt( &m_nextRegenFrame );
	else
		m_nextRegenFrame = 0;

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SupplyWarehouseDockUpdate::loadPostProcess( void )
{

	// extend base class
	DockUpdate::loadPostProcess();

	// update the drawable supply status
	const SupplyWarehouseDockUpdateModuleData *modData = getSupplyWarehouseDockUpdateModuleData();
	Object *us = getObject();
	Drawable *draw = us->getDrawable();
	if( draw )
		draw->updateDrawableSupplyStatus( modData->m_startingBoxesData, m_boxesStored );

}  // end loadPostProcess
