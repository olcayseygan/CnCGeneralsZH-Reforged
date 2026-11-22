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

// FILE: DayNightCycle.h //////////////////////////////////////////////////////////////////////////
//
// The day and night cycle of a match, the host's box in a skirmish, LAN or custom online room (DN=1
// in the game options string; GameLogic::isDayNight).  Day, dusk, night, dawn and day again, in game
// seconds, as a pure function of the logic frame: nothing here keeps state, so nothing is saved or
// CRCed beyond the box, and a pause or a script freeze stops the cycle with the frame.
//
// What the logic does with it: at night every object clears the fog of war 20% less far, in two
// steps of 10% at fixed points of the dusk and back in two at fixed points of the dawn
// (Object::look).  Weapon range, vision range, detection and the AI's sight are left alone.  A look
// is cached, so on each step every object looks again, spread by ObjectID over one wall second
// (DayNight_update), and the scale an object looks with is delayed by the same spread
// (DayNight_sightScaleFor), so a look in between gives the same answer on every machine.
//
// The rest is for the picture: how far into the night the frame is, and when each object's lights
// come on and go off.  An object's light delay is a hash of its ObjectID and nothing else, never a
// random draw: the NIGHT model condition moves launch bones the logic reads.
//
// The cycle stands down under the Apocalypse and Lane War, which write the same light tables.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DAYNIGHTCYCLE_H_
#define __DAYNIGHTCYCLE_H_

#include "Common/GameCommon.h"
#include "Common/GameType.h"

enum DayNightPhase
{
	DAYNIGHT_DAY = 0,
	DAYNIGHT_DUSK,
	DAYNIGHT_NIGHT,
	DAYNIGHT_DAWN,
	DAYNIGHT_PHASES
};

/// what kind of light an object carries, which sets how long after nightfall it may wait to come on
enum DayNightLightKind
{
	DAYNIGHT_LIGHT_BUILDING = 0,
	DAYNIGHT_LIGHT_LAMP,									///< a street or prop lamp
	DAYNIGHT_LIGHT_VEHICLE,
	DAYNIGHT_LIGHT_KINDS
};

enum
{
	DAYNIGHT_DAY_SECONDS = 600,						///< game seconds of each phase
	DAYNIGHT_DUSK_SECONDS = 90,
	DAYNIGHT_NIGHT_SECONDS = 240,
	DAYNIGHT_DAWN_SECONDS = 60,
	DAYNIGHT_CYCLE_SECONDS = DAYNIGHT_DAY_SECONDS + DAYNIGHT_DUSK_SECONDS + DAYNIGHT_NIGHT_SECONDS + DAYNIGHT_DAWN_SECONDS,

	DAYNIGHT_DUSK_STEP_1_SECONDS = 30,		///< game seconds into the dusk the sight drops to 0.9,
	DAYNIGHT_DUSK_STEP_2_SECONDS = 60,		///< and to 0.8
	DAYNIGHT_DAWN_STEP_1_SECONDS = 20,		///< game seconds into the dawn it comes back to 0.9,
	DAYNIGHT_DAWN_STEP_2_SECONDS = 40,		///< and to 1.0
	DAYNIGHT_SIGHT_STEPS = 3,							///< 1.0, 0.9, 0.8

	DAYNIGHT_LIGHTS_ON_SECONDS = 45,			///< game seconds into the dusk the first lights come on, the rest within their delay
	DAYNIGHT_LIGHTS_OFF_SECONDS = 30,			///< game seconds into the dawn the first go off
	DAYNIGHT_BUILDING_DELAY_SECONDS = 20,	///< the longest a light of each kind waits, game seconds
	DAYNIGHT_LAMP_DELAY_SECONDS = 6,
	DAYNIGHT_VEHICLE_DELAY_SECONDS = 4,
};

/// the cycle runs in this match: the box is ticked and neither the Apocalypse nor Lane War is on, one
/// Bool GameLogic settles at the start and saves ahead of the objects (GameLogic::isDayNight)
extern Bool DayNight_isOn( void );

/// tests only: 1 makes DayNight_isOn answer yes, 0 no, -1 (the default) asks the match again
extern void DayNight_setOverrideForTests( Int on );

/// logic frames of one whole cycle at this match's speed
extern UnsignedInt DayNight_cycleFrames( void );

/// the phase of that frame, a DayNightPhase; DAYNIGHT_DAY while the cycle is off
extern Int DayNight_phase( UnsignedInt frame );

/// how far into the night that frame is: 0 by day, eased up across the dusk, 1 at night, eased down
/// across the dawn; 0 while the cycle is off
extern Real DayNight_amount( UnsignedInt frame );

/// the sight step of that frame, 0 (1.0) to DAYNIGHT_SIGHT_STEPS - 1 (0.8); 0 while the cycle is off
extern Int DayNight_sightStep( UnsignedInt frame );

/// the share of its fog of war reveal an object has at that frame: 1.0, 0.9 or 0.8
extern Real DayNight_sightScale( UnsignedInt frame );

/// the same for one object, delayed by its ObjectID's place in the one second spread of re-looks,
/// so the object takes each step on the frame DayNight_update has it look again
extern Real DayNight_sightScaleFor( ObjectID id, UnsignedInt frame );

/// frames that object's light waits past the first lights of the dusk (or, dawn TRUE, past the first
/// to go off at dawn): a hash of the ObjectID, 0 to its kind's longest delay in game seconds
extern UnsignedInt DayNight_lightDelayFrames( ObjectID id, Int kind, Bool dawn = FALSE );

/// that object's lights are on at that frame; an object made at night answers yes straight away.
/// FALSE while the cycle is off.  A NIGHT model condition driven from this has to be set from the logic,
/// on the logic frame, never from a client pass: the logic reads launch bones from the condition state
/// (ApocalypseDirector::handLight is the pattern).
extern Bool DayNight_lightsOn( ObjectID id, Int kind, UnsignedInt frame );

/// the ObjectID residue (id % LOGICFRAMES_PER_SECOND) that looks again on that frame, -1 for none:
/// the frame a step reaches an object is the frame DayNight_sightScaleFor changes for it
extern Int DayNight_relookSlot( UnsignedInt frame );

/// once a logic frame, from GameLogic::update: in the second after each sight step, every object
/// whose ObjectID falls on this frame of the spread looks again
extern void DayNight_update( void );

#endif // __DAYNIGHTCYCLE_H_
