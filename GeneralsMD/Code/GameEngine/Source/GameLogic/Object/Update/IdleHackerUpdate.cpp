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

// FILE: IdleHackerUpdate.cpp ////////////////////////////////////////////////////////////////////
//
// A hacker standing about is income nobody switched on.  The computer's own hackers have been put to
// work since AIPlayer's putToHacking; a player's stood where he left them until he remembered.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/HackInternetAIUpdate.h"
#include "GameLogic/Module/IdleHackerUpdate.h"

//-------------------------------------------------------------------------------------------------
IdleHackerUpdateModuleData::IdleHackerUpdateModuleData()
{
	m_idleFrames = 10 * LOGICFRAMES_PER_SECOND;
	m_centerRange = 150.0f;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void IdleHackerUpdateModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "IdleTime",			INI::parseDurationUnsignedInt,	NULL, offsetof( IdleHackerUpdateModuleData, m_idleFrames ) },
		{ "CenterRange",	INI::parseReal,									NULL, offsetof( IdleHackerUpdateModuleData, m_centerRange ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
IdleHackerUpdate::IdleHackerUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_idleSinceFrame = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
IdleHackerUpdate::~IdleHackerUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
/** The nearest of the owner's finished Internet Centers in range that has a seat for this hacker,
	* ties to the lower id so every machine picks the same one. */
Object *IdleHackerUpdate::findInternetCenter()
{
	Object *me = getObject();
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( me, getIdleHackerUpdateModuleData()->m_centerRange, FROM_CENTER_2D );
	MemoryPoolObjectHolder hold( iter );

	Object *best = NULL;
	Real bestDistSqr = 0.0f;
	for( Object *other = iter->first(); other; other = iter->next() )
	{
		if( !other->isKindOf( KINDOF_FS_INTERNET_CENTER ) || other->getControllingPlayer() != me->getControllingPlayer() ||
				other->isEffectivelyDead() || other->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || other->testStatus( OBJECT_STATUS_SOLD ) )
			continue;
		const ContainModuleInterface *contain = other->getContain();
		if( contain == NULL || !contain->isValidContainerFor( me, TRUE ) )	// a center may carry no seats
			continue;
		const Real distSqr = ThePartitionManager->getDistanceSquared( me, other, FROM_CENTER_2D );
		if( best == NULL || distSqr < bestDistSqr || ( distSqr == bestDistSqr && other->getID() < best->getID() ) )
		{
			best = other;
			bestDistSqr = distSqr;
		}
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime IdleHackerUpdate::update( void )
{
	Object *me = getObject();
	AIUpdateInterface *ai = me->getAI();
	const UnsignedInt now = TheGameLogic->getFrame();

	// Checked every frame: an order that keeps the hacker busy for a single frame still resets the clock.
	if( me->getControllingPlayer()->getPlayerType() != PLAYER_HUMAN || me->isContained() || me->isEffectivelyDead() || !ai->isIdle() ||
			ai->getHackInternetAIInterface()->isHeldByPlayer() )
	{
		m_idleSinceFrame = now;
		return UPDATE_SLEEP_NONE;
	}
	if( now - m_idleSinceFrame < getIdleHackerUpdateModuleData()->m_idleFrames )
		return UPDATE_SLEEP_NONE;
	m_idleSinceFrame = now;

	Object *center = findInternetCenter();
	DEBUG_LOG(( "IDLEHACKER: frame %d hacker %d %s %d\n", now, me->getID(), center ? "enters" : "hacks in place", center ? center->getID() : 0 ));
	if( center )
		ai->aiEnter( center, CMD_FROM_AI );
	else
		ai->aiHackInternet( CMD_FROM_AI );
	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
void IdleHackerUpdate::crc( Xfer *xfer )
{
	UpdateModule::crc( xfer );
	xfer->xferUnsignedInt( &m_idleSinceFrame );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void IdleHackerUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UpdateModule::xfer( xfer );

	xfer->xferUnsignedInt( &m_idleSinceFrame );
}

// ------------------------------------------------------------------------------------------------
void IdleHackerUpdate::loadPostProcess( void )
{
	UpdateModule::loadPostProcess();
}
