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
// CRCed beyond the box, and a pause or a script freeze stops the cycle with the frame.  Every match
// opens at noon, the middle of the day: frame 0 is DAYNIGHT_START_SECONDS into the cycle, one constant
// for every map, so the first dusk comes 300 game seconds in and every later day is a whole one.
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
// random draw: the NIGHT model condition moves launch bones the logic reads.  The logic sets that
// condition itself, at the bind and in DayNight_update across the seconds the lights turn, so windows,
// lamps and headlights come on after dusk and go off after dawn on the same frame on every machine.
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
	DAYNIGHT_START_SECONDS = DAYNIGHT_DAY_SECONDS / 2,	///< where in the cycle frame 0 falls: noon

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

class Object;

/// the DayNightLightKind of an object, from its template alone: a structure's is a building's, a
/// vehicle's or an aircraft's a vehicle's, anything else (a lamp post or a sign made as an object) a lamp's
extern Int DayNight_lightKindOf( const Object *obj );

/// that object's lights at that frame: DayNight_lightsOn with its ObjectID and its kind
extern Bool DayNight_objectLightsOn( const Object *obj, UnsignedInt frame );

/// some object's lights may turn at that frame: from the first lights of the dusk, and of the dawn,
/// through the longest delay of any kind; FALSE while the cycle is off
extern Bool DayNight_lightsMayTurn( UnsignedInt frame );

/// the ObjectID residue (id % LOGICFRAMES_PER_SECOND) that looks again on that frame, -1 for none:
/// the frame a step reaches an object is the frame DayNight_sightScaleFor changes for it
extern Int DayNight_relookSlot( UnsignedInt frame );

/// once a logic frame, from GameLogic::update: in the second after each sight step, every object
/// whose ObjectID falls on this frame of the spread looks again; while the lights may turn, every
/// object's NIGHT model condition is set to DayNight_objectLightsOn, in list order
extern void DayNight_update( void );

//-------------------------------------------------------------------------------------------------
// The picture: the map's light.  The day's colours are the map's own Afternoon light (the lighting
// chunk carries all four hours, retail defaults fill what a map maker never set), or the map's own
// hour's when its Afternoon is black or absurd.  The sun crosses the sky with the phase: low after
// dawn, DAYNIGHT_SUN_PEAK_DEGREES at noon, low before dusk, over DAYNIGHT_SUN_SWEEP_DEGREES of
// bearing centred on the base light's own, warmer and dimmer as it sinks, held at
// DAYNIGHT_SUN_FLOOR_DEGREES so shadows never cross the map.  At night the moon is the light, on an
// arc of its own, under a blue moonlight in DAYNIGHT_LIGHT_LEVELS levels of DayNight_amount.  The
// direction never jumps where a shadow can show it: the sun's shadow fades out as it sets, the
// direction moves to the moonrise while there is none, and the moon's fades in (DayNight_sky).
//
// Two ways to draw it, by what the ground can do (GameClient::isGroundLitEveryFrame):
//   Direct3D 11 with the sun per pixel takes the direction and diffuse every frame and the ambient as
//   a shader term (the change against the ambient the tiles baked), so the light moves in steps of
//   DAYNIGHT_FINE_STEP_DEGREES and every level with no rebuild of the ground; trees and bibs refill.
//   Classic graphics, -d3d9 and the lower terrain detail bake it all into vertices, so they move in
//   DAYNIGHT_COARSE_STEP_DEGREES and every DAYNIGHT_LIGHT_RETINT_EVERY levels, the ground relit over
//   eight frames, and the stencil shadows fade by their colour's alpha.
//
// The logic hands the frame over after each tick (DayNight_update, LogicThread::callAfterTick), the
// main thread works the light out of it and applies a step only when one moved.  None of it is read
// back by the logic, which keeps reading m_timeOfDay, the map's own hour, untouched.  Headless hands
// nothing over.  Off, nothing here runs, and the picture is the old one on every map.
//
// The Apocalypse runs its own red night through the same keep, tint and apply later (its palette: no
// noon base, no depth rule, a sun that stays), which is why the tint keeps its formula and order.
//-------------------------------------------------------------------------------------------------

enum
{
	DAYNIGHT_LIGHT_LEVELS = 128,					///< levels of the night amount the picture moves in (32 stepped the dawn visibly)
	DAYNIGHT_LIGHT_RETINT_EVERY = 16,			///< where the ground bakes the light, the tables move every this many levels
	DAYNIGHT_NIGHT_LOOK_LEVEL = 64,				///< half the levels: the middle of the dusk and the dawn

	DAYNIGHT_LOOK_EASE_SECONDS = 4,				///< game seconds the cloud shadow and the water take to turn to the night's, round the turn of the light

	DAYNIGHT_SUN_FLOOR_DEGREES = 15,			///< the sun and the moon never stand lower for the shadows
	DAYNIGHT_SUN_PEAK_DEGREES = 35,				///< the sun at noon
	DAYNIGHT_SUN_SWEEP_DEGREES = 150,			///< bearing the sun crosses in a day, centred on the base light's
	DAYNIGHT_MOON_PEAK_DEGREES = 30,
	DAYNIGHT_MOON_SWEEP_DEGREES = 120,

	DAYNIGHT_SUN_FADE_OUT_SECONDS = 40,		///< game seconds into the dusk the sun's shadow is gone,
	DAYNIGHT_MOON_RISES_SECONDS = 45,			///< and the light turns to the moon,
	DAYNIGHT_MOON_FADE_IN_SECONDS = 50,		///< whose shadow comes up from here to the end of the dusk
	DAYNIGHT_MOON_FADE_OUT_SECONDS = 25,	///< game seconds into the dawn the moon's shadow is gone,
	DAYNIGHT_SUN_RISES_SECONDS = 30,			///< and the light turns to the sun,
	DAYNIGHT_SUN_FADE_IN_SECONDS = 35,		///< whose shadow comes up from here to the end of the dawn
};

/// the direction steps, in degrees of bearing and of elevation, for a ground lit every frame and
/// for one that bakes the light into its vertices
#define DAYNIGHT_FINE_STEP_DEGREES		0.5f
#define DAYNIGHT_COARSE_STEP_DEGREES	5.0f

/// where the light comes from at a frame, and how strong its shadows are
struct DayNightSky
{
	Real bearing;			///< degrees, counter-clockwise from +x, of the direction toward the light
	Real elevation;		///< degrees over the horizon, never under DAYNIGHT_SUN_FLOOR_DEGREES
	Real shadow;			///< 0 to 1, the share of the shadows' darkness and of the direct light the handovers leave
	Bool moon;				///< the moon is the light
	Real look;				///< 0 to 1, how far the cloud shadow and the water are into the night's look
};

/// the sky at that frame over a base light whose own bearing toward the light is `noonBearing`;
/// a sky that does not move while the cycle is off
extern void DayNight_sky( UnsignedInt frame, Real noonBearing, DayNightSky &out );

/// how warm and dim the sun is for its elevation: 1 at the floor, 0 at the peak, 0 for the moon
extern Real DayNight_lowSun( const DayNightSky &sky );

/// a step of the light: a level (of DAYNIGHT_LIGHT_LEVELS) and a sky, quantised for the ground
struct DayNightStep
{
	Int level;
	Int bearing;
	Int elevation;
	Int shadow;
	Bool moon;
};

/// the step a level and a sky make on a ground lit every frame (`fine`) or one that bakes it
extern void DayNight_step( Int level, const DayNightSky &sky, Bool fine, DayNightStep &out );

/// the level the tables carry for that level: itself where the ground takes the light every frame,
/// else the step of DAYNIGHT_LIGHT_RETINT_EVERY at or below it, which lands on 0 at the end of every
/// dawn and on DAYNIGHT_LIGHT_LEVELS at the end of every dusk
extern Int DayNight_tintLevel( Int level, Bool fine );

/// how the ground takes a retint the cycle is applying (BaseHeightMapRenderObjClass::setTimeOfDay)
enum DayNightGroundRetint
{
	DAYNIGHT_GROUND_FULL = 0,					///< anything else, and the cycle's first step after a keep or a change of ground: the whole rebuild
	DAYNIGHT_GROUND_LIGHT_ONLY,				///< the ground takes it every frame: the scorches and bridges refill
	DAYNIGHT_GROUND_LIGHT_AND_TREES,	///< the same, on a coarse step: the trees and bibs refill as well
	DAYNIGHT_GROUND_SPREAD,						///< the ground bakes it: relit a slice a frame
};
extern Int DayNight_groundRetint( void );

/// main thread, for the Direct3D 11 ground: the ambient on screen against the ambient the tiles
/// baked, light 0's, added in TERRAIN_RELIGHT_SAMPLING; all nought while the cycle's light is not kept
extern void DayNight_terrainAmbientDelta( Real out[ 3 ] );

/// main thread: the ambient a tile bakes.  `live` (m_terrainAmbient[0], what every other reader
/// takes) unless the cycle's light is kept on a ground lit every frame and the tile leaves the sun to
/// the pixel (`sunApart`), when it is the base the cycle holds still and the change rides as a term
extern RGBColor DayNight_bakedTerrainAmbient( const RGBColor &live, Bool sunApart );

/// the logic hands the light over after its ticks: the cycle is on and something draws
extern Bool DayNight_handsLightOver( void );

/// main thread: the infantry light scale to draw with, from the map's per-hour scales: the base
/// hour's by day toward the night's by the light on screen while the cycle's light is kept, else the
/// map's own hour's
extern Real DayNight_infantryLightScale( const Real scales[ TIME_OF_DAY_COUNT ] );

/// the NIGHT model condition the map's own hour gives an object when it is made: the map's hour's with
/// the cycle off, as it always was; with the cycle on, the day's, since every match opens at noon (a map
/// authored for night would otherwise show its windows lit at midday).  Under the cycle an object takes
/// DayNight_objectLightsOn instead.  Logic reads it through launch and exit bones, so it is a function
/// of the match's settings only.
extern Bool DayNight_mapNightCondition( void );

/// how the light goes from day to night
struct DayNightPalette
{
	Real dayAmbient[ 3 ];					///< factors over the base light's ambient by day,
	Real dayDiffuse[ 3 ];					///< and over its sun and fill lights,
	Real nightAmbient[ 3 ];				///< and the same at night
	Real nightDiffuse[ 3 ];
	Real shadowCut;								///< the share of the sun shadow's darkness the night takes off
	Bool noonBase;								///< the base is the map's Afternoon light where it is sane, else the map's own hour's
	Bool depthByMapSun;						///< a map whose own sun is dim takes less of the night (DayNight_depthFor)
	Bool sunMoves;								///< the sun and the moon cross the sky (DayNight_sky)
};

/// the cycle's own palette: day the base itself, night a blue moonlight
extern const DayNightPalette &DayNight_palette( void );

/// one light's colours and direction at a step (`sun`: light 0, the one that moves and fades over the
/// handovers), from the base light's; `depth` is DayNight_depthFor's.  What applyMapLight writes.
extern void DayNight_stepLight( const DayNightPalette &palette, const DayNightStep &step, Bool fine, Real depth, Bool sun,
	const RGBColor &baseAmbient, const RGBColor &baseDiffuse, const Coord3D &basePos,
	RGBColor &ambient, RGBColor &diffuse, Coord3D &pos );

/// the level of that night amount, 0 to DAYNIGHT_LIGHT_LEVELS
extern Int DayNight_lightLevel( Real amount );

/// the night look (no cloud shadow, the water's night) at that level
extern Bool DayNight_nightLookAt( Int level );

/// how deep the night goes on a map whose base sun has that colour, 0 to 1: a full night under a sun
/// of luminance 0.9 and over, none under 0.3 and below (a map authored dark stays readable)
extern Real DayNight_depthFor( const RGBColor &sunDiffuse );

/// a base light's Afternoon table holds a usable noon: its ambient luminance 0.02 to 1.2, its sun's 0.25 to 2
extern Bool DayNight_saneNoon( const RGBColor &ambient, const RGBColor &diffuse );

/// out = base * ( day + ( night - day ) * amount ), channel by channel: the Apocalypse's tint, to the bit
extern void DayNight_tint( RGBColor &out, const RGBColor &base, const Real *day, const Real *night, Real amount );

/// main thread only (through LogicThread::callAfterTick): keep the base light if this map has not
/// yet, then put the light where the palette has it at that frame, when a step of it moved.  Writes
/// the map's own hour's tables and their copies the ground reads, light 0's direction, the shadows'
/// darkness and the night look, and has the client take them (GameClient::setMapLight).
extern void DayNight_applyMapLight( const DayNightPalette &palette, UnsignedInt frame );

/// from GameLogic::reset: the shadow's darkness, a setting of the whole game, comes back, and the
/// next map keeps its own base light (its tables come with it)
extern void DayNight_resetPicture( void );

/// main thread: how far into the night the light on screen is, 0 to 1, in its own steps; 0 when
/// nothing has been applied
extern Real DayNight_lightAmount( void );

/// main thread: the night look to draw.  `mapNight` is the old test (the map's hour is night);
/// with the cycle's light kept the phase decides, with the cycle on and nothing kept yet the match's
/// opening noon does, and with the cycle off the map does, as it always did.
extern Bool DayNight_nightLookFor( Bool mapNight );

#endif // __DAYNIGHTCYCLE_H_
