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

// FILE: MarkAndRainBehavior.cpp /////////////////////////////////////////////////////////////////
//
// A kill an eye saw speeds up the guns that reach it.  Everything it reads is logic state (the
// object list, the radars, sight ranges, relationships), it writes only the guns' temporary weapon
// bonus, and it touches the guns in ObjectID order.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <algorithm>

#define DEFINE_WEAPONBONUSCONDITION_NAMES	// TheWeaponBonusNames, for BonusConditionType

#include "GameLogic/Weapon.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/MarkAndRainBehavior.h"
#include "GameLogic/Module/RadarCoverageBehavior.h"

MarkAndRainBehavior *MarkAndRainBehavior::s_first = NULL;

//-------------------------------------------------------------------------------------------------
/** an eye of owner's works for player */
static Bool serves( const Player *owner, const Player *player )
{
	return owner == player || owner->getRelationship( player->getDefaultTeam() ) == ALLIES;
}

//-------------------------------------------------------------------------------------------------
/** A radar of player's or an ally's covers pos, or a SpotterSight unit of theirs sees it: the
	* Spotter, the Cobra, the Bayraktar, the Maroon Beret.  An eye counts where Object::look would
	* let it look: alive, not shut in a transport, not jammed. */
static Bool isMarked( const Player *player, const Coord3D *pos )
{
	if( RadarCoverageBehavior::isCovered( player, pos ) )
		return TRUE;

	for( const Object *eye = TheGameLogic->getFirstObject(); eye; eye = eye->getNextObject() )
	{
		if( !eye->isSpotter() || eye->isEffectivelyDead() || eye->isDestroyed() || eye->isSightJammed() )
			continue;
		const Player *owner = eye->getControllingPlayer();
		if( !owner || !serves( owner, player ) )
			continue;
		const Object *box = eye->getContainedBy();
		if( box && box->getContain() && !box->getContain()->isGarrisonable() )
			continue;

		const Real sight = eye->getShroudClearingRange();
		const Real dx = pos->x - eye->getPosition()->x;
		const Real dy = pos->y - eye->getPosition()->y;
		if( dx * dx + dy * dy <= sight * sight )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
MarkAndRainBehaviorModuleData::MarkAndRainBehaviorModuleData()
{
	m_bonusConditionType = WEAPONBONUSCONDITION_MARK_AND_RAIN;
	m_bonusDuration.setRaw( 0 );
	m_fx = NULL;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void MarkAndRainBehaviorModuleData::buildFieldParse( MultiIniFieldParse& p )
{
	BehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "BonusConditionType",	INI::parseIndexList,	TheWeaponBonusNames, offsetof( MarkAndRainBehaviorModuleData, m_bonusConditionType ) },
		{ "BonusDuration",			SPEED_FIELD( MarkAndRainBehaviorModuleData, m_bonusDuration ) },
		{ "FX",									INI::parseFXList,			NULL, offsetof( MarkAndRainBehaviorModuleData, m_fx ) },
		{ 0, 0, 0, 0 }
	};
	p.add( dataFieldParse );
}

//-------------------------------------------------------------------------------------------------
MarkAndRainBehavior::MarkAndRainBehavior( Thing *thing, const ModuleData* moduleData ) : BehaviorModule( thing, moduleData )
{
	m_next = s_first;
	s_first = this;
}

//-------------------------------------------------------------------------------------------------
MarkAndRainBehavior::~MarkAndRainBehavior( void )
{
	MarkAndRainBehavior **link = &s_first;
	while( *link != this )
		link = &(*link)->m_next;
	*link = m_next;
}

//-------------------------------------------------------------------------------------------------
/** An enemy unit or building died at victim's place.  Every live gun that is its enemy and reaches
	* the spot with its longest weapon starts (or restarts) its burst, if an eye of its player's or an
	* ally's held the spot. */
/*static*/ void MarkAndRainBehavior::onDeath( const Object *victim )
{
	if( !s_first || victim->isKindOf( KINDOF_PROJECTILE ) || victim->isKindOf( KINDOF_HULK )
			|| !( victim->isKindOf( KINDOF_INFANTRY ) || victim->isKindOf( KINDOF_VEHICLE ) || victim->isKindOf( KINDOF_STRUCTURE ) ) )
		return;

	const Coord3D *pos = victim->getPosition();
	std::vector<MarkAndRainBehavior *> guns;
	for( MarkAndRainBehavior *gun = s_first; gun; gun = gun->m_next )
	{
		const Object *obj = gun->getObject();
		if( obj == victim || obj->isEffectivelyDead() || obj->isDestroyed() || !obj->getControllingPlayer()
				|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->getRelationship( victim ) != ENEMIES )
			continue;
		const Real reach = obj->getLargestWeaponRange();
		const Real dx = pos->x - obj->getPosition()->x;
		const Real dy = pos->y - obj->getPosition()->y;
		if( dx * dx + dy * dy <= reach * reach )
			guns.push_back( gun );
	}
	if( guns.empty() )
		return;

	// the list's order changes with a load; the wake frames the bonus sets must not
	std::sort( guns.begin(), guns.end(), []( MarkAndRainBehavior *a, MarkAndRainBehavior *b )
		{ return a->getObject()->getID() < b->getObject()->getID(); } );

	// per player index: 0 not asked yet, 1 an eye held the spot, 2 none did
	UnsignedByte marked[ MAX_PLAYER_COUNT ] = { 0 };
	for( size_t i = 0; i < guns.size(); ++i )
	{
		Object *obj = guns[ i ]->getObject();
		const Player *player = obj->getControllingPlayer();
		const Int index = player->getPlayerIndex();
		if( marked[ index ] == 0 )
			marked[ index ] = isMarked( player, pos ) ? 1 : 2;
		if( marked[ index ] != 1 )
			continue;

		const MarkAndRainBehaviorModuleData *data = guns[ i ]->getMarkAndRainBehaviorModuleData();
		obj->doTempWeaponBonus( data->m_bonusConditionType, data->m_bonusDuration );
		FXList::doFXObj( data->m_fx, obj );
		// one line a burst: how long the guns spend in it is the line's frames against the match's
		DEBUG_LOG(( "MARK AND RAIN frame %d player %d gun %d for %d frames\n", TheGameLogic->getFrame(), index,
			obj->getID(), (Int)(UnsignedInt)data->m_bonusDuration ));
	}
}

//-------------------------------------------------------------------------------------------------
void MarkAndRainBehavior::crc( Xfer *xfer )
{
	BehaviorModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
//-------------------------------------------------------------------------------------------------
void MarkAndRainBehavior::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	BehaviorModule::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
void MarkAndRainBehavior::loadPostProcess( void )
{
	BehaviorModule::loadPostProcess();
}
