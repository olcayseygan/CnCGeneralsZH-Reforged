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

// FILE: AssistedTargetingUpdate.cpp /////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood, September 2002
// Desc:   Outside influences can tell me to attack something out of my normal targeting range
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_WEAPONSLOTTYPE_NAMES
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/AssistedTargetingUpdate.h"
#include "GameLogic/Module/LaserUpdate.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AssistedTargetingUpdateModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  UpdateModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] = 
	{
		{ "AssistingClipSize",		INI::parseInt,		NULL, offsetof( AssistedTargetingUpdateModuleData, m_clipSize ) },
		{ "AssistingWeaponSlot",	INI::parseLookupList,	TheWeaponSlotTypeNamesLookupList, offsetof( AssistedTargetingUpdateModuleData, m_weaponSlot ) },
		{ "LaserFromAssisted",		INI::parseAsciiString,				NULL, offsetof( AssistedTargetingUpdateModuleData, m_laserFromAssistedName ) },
		{ "LaserToTarget",				INI::parseAsciiString,				NULL, offsetof( AssistedTargetingUpdateModuleData, m_laserToTargetName ) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AssistedTargetingUpdate::AssistedTargetingUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_laserFromAssisted = NULL;
	m_laserToTarget = NULL;
	m_assistVictimID = INVALID_ID;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AssistedTargetingUpdate::~AssistedTargetingUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool AssistedTargetingUpdate::isFreeToAssist( const Object *victimObject ) const
{
	const AssistedTargetingUpdateModuleData *md = getAssistedTargetingUpdateModuleData();
	const Object *me = getObject();
	if( !me->isAbleToAttack() )
		return FALSE;// This will cover under construction among other things

	// An underpowered, EMPed or hacked battery has no running AI to take the order, and one already on a
	// target of its own is not free.  Both used to draw the beams and never fire the clip.
	if( me->isDisabled() || me->testStatus( OBJECT_STATUS_IS_ATTACKING ) )
		return FALSE;

	// The reload times of my two weapons are tied together, so Ready is indicitive of either.
	Bool ready = me->getCurrentWeapon() && me->getCurrentWeapon()->getStatus() == READY_TO_FIRE;
	if( !ready )
		return FALSE;

	// The lock forces the assisting weapon whatever the victim is, so that weapon has to be able to hit it
	// from here.  A requester firing at aircraft 350 out and asking 200 further reaches past the assist
	// weapon's 450, and a requester shooting down a missile asks with a weapon the assist one is not.
	Weapon *assistWeapon = me->getWeaponInWeaponSlot( md->m_weaponSlot );
	return (assistWeapon->getAntiMask() & WeaponSet::getVictimAntiMask( victimObject )) != 0
		&& assistWeapon->isWithinTargetPitch( me, victimObject )
		&& assistWeapon->isWithinAttackRange( me, victimObject )
		&& assistWeapon->estimateWeaponDamage( me, victimObject ) > 0.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AssistedTargetingUpdate::assistAttack( const Object *requestingObject, Object *victimObject )
{
	const AssistedTargetingUpdateModuleData *md = getAssistedTargetingUpdateModuleData();
	Object *me = getObject();
	if( !me->getAI() )
		return;

	// lock it just till the weapon is empty or the attack is "done"
	me->setWeaponLock( md->m_weaponSlot, LOCKED_TEMPORARILY );
	me->getAI()->aiAttackObject( victimObject, md->m_clipSize, CMD_FROM_AI );
	m_assistVictimID = victimObject->getID();
	setWakeFrame( me, UPDATE_SLEEP_NONE );


	if( m_laserFromAssisted )
		makeFeedbackLaser( m_laserFromAssisted, requestingObject, me );
	if( m_laserToTarget )
		makeFeedbackLaser( m_laserToTarget, me, victimObject );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AssistedTargetingUpdate::makeFeedbackLaser( const ThingTemplate *laserTemplate, const Object *from, const Object *to )
{
	if( !getObject()->getControllingPlayer() )
		return;

	Team *laserTeam = getObject()->getControllingPlayer()->getDefaultTeam();
	Object *laser = TheThingFactory->newObject( laserTemplate, laserTeam );
	if( !laser )
		return;

	// Give it a good basis in reality to ensure it can draw when on screen.
	laser->setPositionFix(from->getPositionFix());
	
	Drawable *draw = laser->getDrawable();
	static const NameKeyType key_LaserUpdate = NAMEKEY( "LaserUpdate" );
	LaserUpdate *update = (LaserUpdate*)draw->findClientUpdateModule( key_LaserUpdate );
	if( !update )
	{
		TheGameLogic->destroyObject( laser );
		return;
	}

	// the laser is a client update; it draws in float
	Coord3D fromPos = from->getPositionFix()->toCoord3D();
	Coord3D toPos = to->getPositionFix()->toCoord3D();
	update->initLaser( getObject(), to, &fromPos, &toPos, "" );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime AssistedTargetingUpdate::update( void )
{

  const AssistedTargetingUpdateModuleData *d = getAssistedTargetingUpdateModuleData();

	m_laserFromAssisted = TheThingFactory->findTemplate( d->m_laserFromAssistedName );

	// ...and the other laser gets the other name.  Both of these read m_laserFromAssistedName, so
	// the LaserToTarget line in the INI was parsed, stored and never looked at, and the beam to the
	// victim was drawn with the template meant for the beam from the spotter.
	m_laserToTarget = TheThingFactory->findTemplate( d->m_laserToTargetName );

	if( m_assistVictimID == INVALID_ID )
		return UPDATE_SLEEP_FOREVER;

	// Only the last shot of a clip lets go of the lock.  An assist that ended any other way, the victim
	// killed by somebody else after one missile, left the battery shooting its own targets with the
	// long-range assist weapon until it next emptied a clip.
	Object *me = getObject();
	WeaponSlotType currentSlot;
	me->getCurrentWeapon( &currentSlot );
	if( me->isCurWeaponLocked() && currentSlot == d->m_weaponSlot )
	{
		Object *goal = me->getAI()->getGoalObject();
		if( me->testStatus( OBJECT_STATUS_IS_ATTACKING ) && goal && goal->getID() == m_assistVictimID )
			return UPDATE_SLEEP_NONE;

		me->releaseWeaponLock( LOCKED_TEMPORARILY );
	}

	m_assistVictimID = INVALID_ID;
	return UPDATE_SLEEP_FOREVER;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AssistedTargetingUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: m_assistVictimID */
// ------------------------------------------------------------------------------------------------
void AssistedTargetingUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	if( version >= 2 )
		xfer->xferObjectID( &m_assistVictimID );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AssistedTargetingUpdate::loadPostProcess( void )
{
  const AssistedTargetingUpdateModuleData *d = getAssistedTargetingUpdateModuleData();

	m_laserFromAssisted = TheThingFactory->findTemplate( d->m_laserFromAssistedName );
	m_laserToTarget = TheThingFactory->findTemplate( d->m_laserToTargetName );		// see update()

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
