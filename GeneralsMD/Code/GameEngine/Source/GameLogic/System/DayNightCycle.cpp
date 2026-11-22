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

// FILE: DayNightCycle.cpp ////////////////////////////////////////////////////////////////////////
//
// The day and night cycle; DayNightCycle.h says what it does.  Every answer is a function of the
// logic frame, the match's speed and an ObjectID, all read fresh on every call: a game second is
// LOGICFRAMES_PER_GAME_SECOND ticks of this match, never a value kept from another one.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/DayNightCycle.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

static Int theOverrideForTests = -1;

/// ticks a game second at this match's speed, read every time
static UnsignedInt gameSecond( void )
{
	const UnsignedInt frames = (UnsignedInt)LOGICFRAMES_PER_GAME_SECOND;
	return frames > 0 ? frames : 1;
}

/// the wall second the re-looks of one step are spread over, in ticks
static UnsignedInt spreadFrames( void )
{
	return LOGICFRAMES_PER_SECOND > 0 ? (UnsignedInt)LOGICFRAMES_PER_SECOND : 1;
}

/// where that frame falls in its cycle, in ticks from the start of the day
static UnsignedInt intoCycle( UnsignedInt frame )
{
	return frame % ( (UnsignedInt)DAYNIGHT_CYCLE_SECONDS * gameSecond() );
}

static const UnsignedInt DUSK_START = DAYNIGHT_DAY_SECONDS;
static const UnsignedInt NIGHT_START = DUSK_START + DAYNIGHT_DUSK_SECONDS;
static const UnsignedInt DAWN_START = NIGHT_START + DAYNIGHT_NIGHT_SECONDS;

/// the four sight steps in game seconds into the cycle, in order, and the step each one leaves
static const UnsignedInt STEP_SECONDS[ 4 ] =
{
	DUSK_START + DAYNIGHT_DUSK_STEP_1_SECONDS,
	DUSK_START + DAYNIGHT_DUSK_STEP_2_SECONDS,
	DAWN_START + DAYNIGHT_DAWN_STEP_1_SECONDS,
	DAWN_START + DAYNIGHT_DAWN_STEP_2_SECONDS,
};

static const Real SIGHT_SCALES[ DAYNIGHT_SIGHT_STEPS ] = { 1.0f, 0.9f, 0.8f };

//-------------------------------------------------------------------------------------------------
Bool DayNight_isOn( void )
{
	if( theOverrideForTests >= 0 )
		return theOverrideForTests != 0;
	// one saved Bool, settled at the start against the Apocalypse and Lane War (GameLogic::startNewGame)
	return TheGameLogic != NULL && TheGameLogic->isDayNight();
}

//-------------------------------------------------------------------------------------------------
void DayNight_setOverrideForTests( Int on )
{
	theOverrideForTests = on;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt DayNight_cycleFrames( void )
{
	return (UnsignedInt)DAYNIGHT_CYCLE_SECONDS * gameSecond();
}

//-------------------------------------------------------------------------------------------------
Int DayNight_phase( UnsignedInt frame )
{
	if( !DayNight_isOn() )
		return DAYNIGHT_DAY;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	if( t < DUSK_START * s )
		return DAYNIGHT_DAY;
	if( t < NIGHT_START * s )
		return DAYNIGHT_DUSK;
	if( t < DAWN_START * s )
		return DAYNIGHT_NIGHT;
	return DAYNIGHT_DAWN;
}

//-------------------------------------------------------------------------------------------------
Real DayNight_amount( UnsignedInt frame )
{
	const Int phase = DayNight_phase( frame );
	if( phase == DAYNIGHT_DAY )
		return 0.0f;
	if( phase == DAYNIGHT_NIGHT )
		return 1.0f;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	const Bool dusk = phase == DAYNIGHT_DUSK;
	const UnsignedInt start = ( dusk ? DUSK_START : DAWN_START ) * s;
	const UnsignedInt length = ( dusk ? DAYNIGHT_DUSK_SECONDS : DAYNIGHT_DAWN_SECONDS ) * s;
	const Real u = (Real)( t - start ) / (Real)length;
	const Real eased = u * u * ( 3.0f - 2.0f * u );
	return dusk ? eased : 1.0f - eased;
}

//-------------------------------------------------------------------------------------------------
Int DayNight_sightStep( UnsignedInt frame )
{
	if( !DayNight_isOn() )
		return 0;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	if( t < STEP_SECONDS[ 0 ] * s )
		return 0;
	if( t < STEP_SECONDS[ 1 ] * s )
		return 1;
	if( t < STEP_SECONDS[ 2 ] * s )
		return 2;
	if( t < STEP_SECONDS[ 3 ] * s )
		return 1;
	return 0;
}

//-------------------------------------------------------------------------------------------------
Real DayNight_sightScale( UnsignedInt frame )
{
	return SIGHT_SCALES[ DayNight_sightStep( frame ) ];
}

//-------------------------------------------------------------------------------------------------
Real DayNight_sightScaleFor( ObjectID id, UnsignedInt frame )
{
	const UnsignedInt late = (UnsignedInt)id % spreadFrames();
	return DayNight_sightScale( frame >= late ? frame - late : 0 );
}

//-------------------------------------------------------------------------------------------------
/** A 32-bit integer mix (the lowbias32 finaliser): the same bits on every machine, and neighbouring
		ObjectIDs land far apart, so a row of buildings built together does not light in order. */
//-------------------------------------------------------------------------------------------------
static UnsignedInt mixId( UnsignedInt x )
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

UnsignedInt DayNight_lightDelayFrames( ObjectID id, Int kind, Bool dawn )
{
	static const UnsignedInt LONGEST[ DAYNIGHT_LIGHT_KINDS ] =
		{ DAYNIGHT_BUILDING_DELAY_SECONDS, DAYNIGHT_LAMP_DELAY_SECONDS, DAYNIGHT_VEHICLE_DELAY_SECONDS };
	const UnsignedInt longest = LONGEST[ kind >= 0 && kind < DAYNIGHT_LIGHT_KINDS ? kind : DAYNIGHT_LIGHT_BUILDING ];
	const UnsignedInt salt = dawn ? 0x5bd1e995U : 0x1b873593U;
	return mixId( (UnsignedInt)id ^ salt ) % ( longest * gameSecond() + 1 );
}

//-------------------------------------------------------------------------------------------------
/** For the picture's writer: a NIGHT model condition driven from this is set from the logic on the
		logic frame, because the logic reads launch bones from the condition state. */
//-------------------------------------------------------------------------------------------------
Bool DayNight_lightsOn( ObjectID id, Int kind, UnsignedInt frame )
{
	if( !DayNight_isOn() )
		return FALSE;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	const UnsignedInt on = ( DUSK_START + DAYNIGHT_LIGHTS_ON_SECONDS ) * s + DayNight_lightDelayFrames( id, kind, FALSE );
	const UnsignedInt off = ( DAWN_START + DAYNIGHT_LIGHTS_OFF_SECONDS ) * s + DayNight_lightDelayFrames( id, kind, TRUE );
	return t >= on && t < off;
}

//-------------------------------------------------------------------------------------------------
/** In the wall second after each sight step, the objects whose ObjectID falls on this frame of the
		spread look again with their new scale.  The list is walked in its own order, which is logic state;
		nothing else is read.  ponytail: an O(objects) walk on 30 frames of each step, 120 frames a cycle;
		a bucket per spread slot if a full map shows it in the frame time. */
//-------------------------------------------------------------------------------------------------
Int DayNight_relookSlot( UnsignedInt frame )
{
	if( !DayNight_isOn() )
		return -1;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	const UnsignedInt spread = spreadFrames();
	for( Int i = 0; i < 4; ++i )
	{
		const UnsignedInt step = STEP_SECONDS[ i ] * s;
		if( t >= step && t - step < spread )
			return (Int)( t - step );	// the steps lie further apart than the spread
	}
	return -1;
}

void DayNight_update( void )
{
	if( TheGameLogic == NULL )
		return;
	const Int slot = DayNight_relookSlot( TheGameLogic->getFrame() );
	if( slot < 0 )
		return;
	const UnsignedInt spread = spreadFrames();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		if( (UnsignedInt)obj->getID() % spread == (UnsignedInt)slot )
			obj->refreshLook();
}
