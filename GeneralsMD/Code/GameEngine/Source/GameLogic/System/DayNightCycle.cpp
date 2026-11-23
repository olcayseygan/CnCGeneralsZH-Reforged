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

#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/DayNightCycle.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "Lib/Clock.h"
#include "Lib/Trig.h"
#include <math.h>

static Int theOverrideForTests = -1;

static void handLight( UnsignedInt now );

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

/// where that frame falls in its cycle, in ticks from the start of the day; frame 0 is noon
static UnsignedInt intoCycle( UnsignedInt frame )
{
	const UnsignedInt cycle = (UnsignedInt)DAYNIGHT_CYCLE_SECONDS * gameSecond();
	// the offset is under one cycle, so the sum wraps the same way on every machine (UnsignedInt)
	return ( frame % cycle + (UnsignedInt)DAYNIGHT_START_SECONDS * gameSecond() ) % cycle;
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
Int DayNight_lightKindOf( const Object *obj )
{
	if( obj->isKindOf( KINDOF_STRUCTURE ) )
		return DAYNIGHT_LIGHT_BUILDING;
	if( obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_AIRCRAFT ) )
		return DAYNIGHT_LIGHT_VEHICLE;
	return DAYNIGHT_LIGHT_LAMP;
}

//-------------------------------------------------------------------------------------------------
Bool DayNight_objectLightsOn( const Object *obj, UnsignedInt frame )
{
	return DayNight_lightsOn( obj->getID(), DayNight_lightKindOf( obj ), frame );
}

//-------------------------------------------------------------------------------------------------
static_assert( DAYNIGHT_BUILDING_DELAY_SECONDS >= DAYNIGHT_LAMP_DELAY_SECONDS
	&& DAYNIGHT_BUILDING_DELAY_SECONDS >= DAYNIGHT_VEHICLE_DELAY_SECONDS, "the lights' window takes the building's delay as the longest" );

Bool DayNight_lightsMayTurn( UnsignedInt frame )
{
	if( !DayNight_isOn() )
		return FALSE;
	const UnsignedInt t = intoCycle( frame );
	const UnsignedInt s = gameSecond();
	const UnsignedInt longest = (UnsignedInt)DAYNIGHT_BUILDING_DELAY_SECONDS * s;	// a delay runs 0 to this, both ends in
	const UnsignedInt on = ( DUSK_START + DAYNIGHT_LIGHTS_ON_SECONDS ) * s;
	const UnsignedInt off = ( DAWN_START + DAYNIGHT_LIGHTS_OFF_SECONDS ) * s;
	return ( t >= on && t - on <= longest ) || ( t >= off && t - off <= longest );
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

/** The lights: across the two windows a light may turn in, every object with a drawable takes its
		NIGHT condition from DayNight_objectLightsOn, a function of its ObjectID, its template and the frame,
		in list order inside the tick, so a launch bone asked later in this tick or the next answers the
		same on every machine.  The write is a no-op when the flag already stands (clearAndSetModelConditionFlags);
		nothing of the drawable is read.  An object made outside the windows took the right answer at its
		bind (Object::friend_bindToDrawable), and a save brings the flags with the drawable.  ponytail: an
		O(objects) walk on every tick of the two 20 game second windows, about 800 ticks a cycle at 150;
		a list per delay frame if a full map shows it in the frame time. */
static void turnLights( UnsignedInt frame )
{
	if( !DayNight_lightsMayTurn( frame ) || TheGlobalData == NULL || !TheGlobalData->m_forceModelsToFollowTimeOfDay )
		return;
	ModelConditionFlags night;
	night.set( MODELCONDITION_NIGHT );
	ModelConditionFlags none;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		Drawable *draw = obj->getDrawable();
		if( draw == NULL )
			continue;
		if( DayNight_objectLightsOn( obj, frame ) )
			draw->clearAndSetModelConditionFlags( none, night );
		else
			draw->clearAndSetModelConditionFlags( night, none );
	}
}

void DayNight_update( void )
{
	if( TheGameLogic == NULL )
		return;
	handLight( TheGameLogic->getFrame() );
	turnLights( TheGameLogic->getFrame() );
	const Int slot = DayNight_relookSlot( TheGameLogic->getFrame() );
	if( slot < 0 )
		return;
	const UnsignedInt spread = spreadFrames();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		if( (UnsignedInt)obj->getID() % spread == (UnsignedInt)slot )
			obj->refreshLook();
}

//-------------------------------------------------------------------------------------------------
// The picture: the map's light (DayNightCycle.h).  The pure parts first, then the main thread's.
//-------------------------------------------------------------------------------------------------

static Real luminance( const RGBColor &c )
{
	return 0.299f * c.red + 0.587f * c.green + 0.114f * c.blue;
}

/** Moonlight over noon.  The ambient carries what stays readable: a face turned from the moon keeps
		about three fifths of its noon, a hard ambient cut left the Apocalypse's units black silhouettes.
		The moon itself is dim and blue, a luminance of 0.27 of the noon sun's, which also leaves a bright
		map's sun shafts next to nothing (their strength falls with the sun's luminance under 0.25). */
static const DayNightPalette theCyclePalette =
{
	{ 1.0f, 1.0f, 1.0f },
	{ 1.0f, 1.0f, 1.0f },
	{ 0.55f, 0.62f, 0.90f },
	{ 0.22f, 0.27f, 0.42f },
	0.5f,
	TRUE,
	TRUE,
	TRUE,
};

/// a low sun's factors over the day's: a little dimmer and warm, the sun's light most (DayNight_lowSun)
static const Real LOW_SUN_AMBIENT[ 3 ] = { 0.95f, 0.90f, 0.85f };
static const Real LOW_SUN_DIFFUSE[ 3 ] = { 1.00f, 0.82f, 0.62f };

/// the stronger of two: the shadow strength a fade may take, since 0 stands for the default darkness
static const Real FADED_SHADOW_STRENGTH = 0.001f;

const DayNightPalette &DayNight_palette( void )
{
	return theCyclePalette;
}

Int DayNight_lightLevel( Real amount )
{
	const Int level = (Int)( amount * (Real)DAYNIGHT_LIGHT_LEVELS + 0.5f );
	return level < 0 ? 0 : level > DAYNIGHT_LIGHT_LEVELS ? DAYNIGHT_LIGHT_LEVELS : level;
}

Int DayNight_tintLevel( Int level, Bool fine )
{
	return fine ? level : level / DAYNIGHT_LIGHT_RETINT_EVERY * DAYNIGHT_LIGHT_RETINT_EVERY;
}

static Real clamp01( Real x )
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static const Real PI_REAL = 3.14159265f;

/// a body crossing the sky, `u` 0 at its rise to 1 at its set.  The picture's alone, but through
/// Lib/Trig.h like every angle in a GameLogic file, so the sky is the same bits on every machine
static void bodyAt( Real u, Real noonBearing, Real sweep, Real peak, DayNightSky &out )
{
	out.bearing = noonBearing + ( 0.5f - u ) * sweep;
	out.elevation = (Real)DAYNIGHT_SUN_FLOOR_DEGREES + ( peak - (Real)DAYNIGHT_SUN_FLOOR_DEGREES ) * Sin( PI_REAL * u );
}

void DayNight_sky( UnsignedInt frame, Real noonBearing, DayNightSky &out )
{
	out.bearing = noonBearing;
	out.elevation = (Real)DAYNIGHT_SUN_PEAK_DEGREES;
	out.shadow = 1.0f;
	out.moon = FALSE;
	out.look = 0.0f;
	if( !DayNight_isOn() )
		return;
	const Real t = (Real)intoCycle( frame ) / (Real)gameSecond();	// game seconds into the cycle
	// the night look eases in round the turn of the light, centred on it
	const Real ease = (Real)DAYNIGHT_LOOK_EASE_SECONDS;
	if( t >= (Real)NIGHT_START && t < (Real)DAWN_START )
		out.look = 1.0f;
	else if( t >= (Real)DUSK_START && t < (Real)NIGHT_START )
		out.look = clamp01( ( t - (Real)DUSK_START - (Real)DAYNIGHT_MOON_RISES_SECONDS + 0.5f * ease ) / ease );
	else if( t >= (Real)DAWN_START )
		out.look = 1.0f - clamp01( ( t - (Real)DAWN_START - (Real)DAYNIGHT_SUN_RISES_SECONDS + 0.5f * ease ) / ease );
	const Real sunSweep = (Real)DAYNIGHT_SUN_SWEEP_DEGREES, sunPeak = (Real)DAYNIGHT_SUN_PEAK_DEGREES;
	const Real moonSweep = (Real)DAYNIGHT_MOON_SWEEP_DEGREES, moonPeak = (Real)DAYNIGHT_MOON_PEAK_DEGREES;
	if( t < (Real)DUSK_START )
	{
		bodyAt( t / (Real)DAYNIGHT_DAY_SECONDS, noonBearing, sunSweep, sunPeak, out );
		return;
	}
	if( t < (Real)NIGHT_START )
	{
		// the sun's shadow fades as it sets, the light turns while there is none, the moon's comes up
		const Real d = t - (Real)DUSK_START;
		if( d < (Real)DAYNIGHT_MOON_RISES_SECONDS )
		{
			bodyAt( 1.0f, noonBearing, sunSweep, sunPeak, out );
			out.shadow = 1.0f - clamp01( d / (Real)DAYNIGHT_SUN_FADE_OUT_SECONDS );
		}
		else
		{
			bodyAt( 0.0f, noonBearing, moonSweep, moonPeak, out );
			out.moon = TRUE;
			out.shadow = clamp01( ( d - (Real)DAYNIGHT_MOON_FADE_IN_SECONDS )
				/ (Real)( DAYNIGHT_DUSK_SECONDS - DAYNIGHT_MOON_FADE_IN_SECONDS ) );
		}
		return;
	}
	if( t < (Real)DAWN_START )
	{
		bodyAt( ( t - (Real)NIGHT_START ) / (Real)DAYNIGHT_NIGHT_SECONDS, noonBearing, moonSweep, moonPeak, out );
		out.moon = TRUE;
		return;
	}
	const Real d = t - (Real)DAWN_START;
	if( d < (Real)DAYNIGHT_SUN_RISES_SECONDS )
	{
		bodyAt( 1.0f, noonBearing, moonSweep, moonPeak, out );
		out.moon = TRUE;
		out.shadow = 1.0f - clamp01( d / (Real)DAYNIGHT_MOON_FADE_OUT_SECONDS );
	}
	else
	{
		bodyAt( 0.0f, noonBearing, sunSweep, sunPeak, out );
		out.shadow = clamp01( ( d - (Real)DAYNIGHT_SUN_FADE_IN_SECONDS ) / (Real)( DAYNIGHT_DAWN_SECONDS - DAYNIGHT_SUN_FADE_IN_SECONDS ) );
	}
}

Real DayNight_lowSun( const DayNightSky &sky )
{
	// the moon takes the same warmth by its elevation: it rises and sets at the floor where the sun
	// hands over to it, so the turn keeps the warmth; at full night the day's factors weigh nothing
	return clamp01( ( (Real)DAYNIGHT_SUN_PEAK_DEGREES - sky.elevation )
		/ (Real)( DAYNIGHT_SUN_PEAK_DEGREES - DAYNIGHT_SUN_FLOOR_DEGREES ) );
}

/// steps of the handovers' fade on a ground that bakes the light, each a spread relight
static const Int COARSE_FADE_STEPS = 32;

static Int roundToInt( Real x )
{
	return (Int)floor( x + 0.5f );
}

void DayNight_step( Int level, const DayNightSky &sky, Bool fine, DayNightStep &out )
{
	const Real degrees = fine ? DAYNIGHT_FINE_STEP_DEGREES : DAYNIGHT_COARSE_STEP_DEGREES;
	out.level = DayNight_tintLevel( level, fine );
	out.bearing = roundToInt( sky.bearing / degrees );
	out.elevation = roundToInt( sky.elevation / degrees );
	// the shadows and the direct light fade together: every level where the ground takes it every
	// frame, in 32 steps where each is a spread relight (eighths showed the moon coming up)
	out.shadow = roundToInt( sky.shadow * ( fine ? (Real)DAYNIGHT_LIGHT_LEVELS : (Real)COARSE_FADE_STEPS ) );
	out.moon = sky.moon;
}

static Bool sameStep( const DayNightStep &a, const DayNightStep &b )
{
	return a.level == b.level && a.bearing == b.bearing && a.elevation == b.elevation && a.shadow == b.shadow
		&& ( a.moon != 0 ) == ( b.moon != 0 );
}

Bool DayNight_nightLookAt( Int level )
{
	return level >= DAYNIGHT_NIGHT_LOOK_LEVEL;
}

Real DayNight_depthFor( const RGBColor &sunDiffuse )
{
	const Real depth = ( luminance( sunDiffuse ) - 0.3f ) / 0.6f;
	return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

Bool DayNight_saneNoon( const RGBColor &ambient, const RGBColor &diffuse )
{
	const Real a = luminance( ambient ), d = luminance( diffuse );
	// a NaN fails every comparison, so it is not sane either
	return a >= 0.02f && a <= 1.2f && d >= 0.25f && d <= 2.0f;
}

void DayNight_tint( RGBColor &out, const RGBColor &base, const Real *day, const Real *night, Real amount )
{
	out.red = base.red * ( day[ 0 ] + ( night[ 0 ] - day[ 0 ] ) * amount );
	out.green = base.green * ( day[ 1 ] + ( night[ 1 ] - day[ 1 ] ) * amount );
	out.blue = base.blue * ( day[ 2 ] + ( night[ 2 ] - day[ 2 ] ) * amount );
}

//-------------------------------------------------------------------------------------------------
/** Logic side, once a tick from DayNight_update: hand the main thread the frame this tick ran.  The
		light is a pure function of that frame and the map's base light; nothing the logic reads comes
		back from it.  Headless draws nothing, so it hands nothing. */
//-------------------------------------------------------------------------------------------------
Bool DayNight_handsLightOver( void )
{
	return DayNight_isOn() && TheGlobalData != NULL && !TheGlobalData->m_headless;
}

static void handLight( UnsignedInt now )
{
	if( !DayNight_handsLightOver() )
		return;
	LogicThread::callAfterTick( [now]() { DayNight_applyMapLight( DayNight_palette(), now ); } );
}

//-------------------------------------------------------------------------------------------------
// Main thread from here on.
//-------------------------------------------------------------------------------------------------

static GlobalData::TerrainLighting theBaseTerrainLight[ MAX_GLOBAL_LIGHTS ];
static GlobalData::TerrainLighting theBaseObjectLight[ MAX_GLOBAL_LIGHTS ];
static Real theBaseShadowStrength = 0.0f;
static Real theDepth = 1.0f;
static Real theNoonBearing = 0.0f;			///< degrees, the base light 0's bearing toward the sun
static Int theKeptHour = -1;					///< the map hour whose tables the base was kept for; -1 none
static DayNightStep theAppliedStep;		///< the step the tables carry,
static Bool theAppliedFine = FALSE;		///< for this kind of ground,
static Bool theStepApplied = FALSE;		///< once one has been applied since the keep
static Bool theNightLook = FALSE;
static Real theAmbientDelta[ 3 ] = { 0.0f, 0.0f, 0.0f };
static Int theGroundRetint = DAYNIGHT_GROUND_FULL;
static DayNightStep theCoarseStep;		///< the step a coarse ground would carry, for the trees on a fine one
static Int theLookStep = 0;						///< LOOK_STEPS of the night look the cloud shadow and water carry
static Int theStencilStep = 0;				///< STENCIL_STEPS of the stencil shadows' darkness
static Int theSmokeStep = -1;					///< SMOKE_STEPS of the night the smoke's tint carries; -1 white, untouched
static Int theBaseHour = TIME_OF_DAY_AFTERNOON;	///< the hour whose colours the day is
static UnsignedInt theTreeFrame = 0;	///< the frame the trees and bibs last refilled for the light
static const UnsignedInt TREE_REFILL_FRAMES = 600;

static const Int LOOK_STEPS = 16;			///< the cloud fade refilters a 1024 texture per step
static const Int STENCIL_STEPS = 32;		///< alpha only
static const Int SMOKE_STEPS = 32;			///< each change ages the GPU particles' ring again
/// the tint over smoke and dust at full night: about the darkening of a lit face, a little blue
static const Real NIGHT_SMOKE[ 3 ] = { 0.42f, 0.48f, 0.66f };
/// of the direct light a handover takes away, the share the ambient gives back (a lit face's average)
static const Real DIRECT_TO_AMBIENT = 0.35f;


/// the map's own tables for every hour as the map load left them, taken before the cycle writes any
/// (keepMapLight); a later keep reads these, never the live tables the cycle has tinted
static GlobalData::TerrainLighting theMapTerrainLight[ TIME_OF_DAY_COUNT ][ MAX_GLOBAL_LIGHTS ];
static GlobalData::TerrainLighting theMapObjectLight[ TIME_OF_DAY_COUNT ][ MAX_GLOBAL_LIGHTS ];
static Bool theMapLightTaken = FALSE;

/** The base: the colours of the map's Afternoon where they are sane (palette.noonBase), else of its
		own hour, with the same table's light directions, and the shadow's darkness.  All of it from the
		map's own tables as they loaded; a keep after a script's hour change gives the hour it leaves its
		own light back first. */
static void keepMapLight( const DayNightPalette &palette )
{
	if( !theMapLightTaken )
	{
		memcpy( theMapTerrainLight, TheGlobalData->m_terrainLighting, sizeof( theMapTerrainLight ) );
		memcpy( theMapObjectLight, TheGlobalData->m_terrainObjectsLighting, sizeof( theMapObjectLight ) );
		theBaseShadowStrength = TheGlobalData->m_shadowMapStrength;
		theMapLightTaken = TRUE;
	}
	else if( theKeptHour >= 0 && theKeptHour < TIME_OF_DAY_COUNT )
	{
		memcpy( TheWritableGlobalData->m_terrainLighting[ theKeptHour ], theMapTerrainLight[ theKeptHour ], sizeof( theMapTerrainLight[ 0 ] ) );
		memcpy( TheWritableGlobalData->m_terrainObjectsLighting[ theKeptHour ], theMapObjectLight[ theKeptHour ], sizeof( theMapObjectLight[ 0 ] ) );
	}
	const Int hour = TheGlobalData->m_timeOfDay;
	Int from = hour;
	if( palette.noonBase && hour != TIME_OF_DAY_AFTERNOON )
	{
		const GlobalData::TerrainLighting &terrain = theMapTerrainLight[ TIME_OF_DAY_AFTERNOON ][ 0 ];
		const GlobalData::TerrainLighting &objects = theMapObjectLight[ TIME_OF_DAY_AFTERNOON ][ 0 ];
		if( DayNight_saneNoon( terrain.ambient, terrain.diffuse ) && DayNight_saneNoon( objects.ambient, objects.diffuse ) )
			from = TIME_OF_DAY_AFTERNOON;
		else
			DEBUG_LOG(( "DAYNIGHT: the map's Afternoon light is not a noon, its own hour %d is the day\n", hour ));
	}
	for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
	{
		theBaseTerrainLight[ k ] = theMapTerrainLight[ from ][ k ];
		theBaseObjectLight[ k ] = theMapObjectLight[ from ][ k ];
		if( !palette.sunMoves )
		{
			// a sun that stays keeps the map's own hour's direction, the authored shadows
			theBaseTerrainLight[ k ].lightPos = theMapTerrainLight[ hour ][ k ].lightPos;
			theBaseObjectLight[ k ].lightPos = theMapObjectLight[ hour ][ k ].lightPos;
		}
	}
	// lightPos is the way the light travels, so the sun lies the other way
	const Coord3D &sun = theBaseTerrainLight[ 0 ].lightPos;
	theNoonBearing = ( sun.x != 0.0f || sun.y != 0.0f ) ? ATan2( -sun.y, -sun.x ) * ( 180.0f / PI_REAL ) : 0.0f;
	theDepth = palette.depthByMapSun ? DayNight_depthFor( theBaseTerrainLight[ 0 ].diffuse ) : 1.0f;
	theKeptHour = hour;
	theBaseHour = from;
	theStepApplied = FALSE;
	theNightLook = hour == TIME_OF_DAY_NIGHT;	// what the water shows now
	theLookStep = theNightLook ? LOOK_STEPS : 0;
	theStencilStep = STENCIL_STEPS;
	theAmbientDelta[ 0 ] = theAmbientDelta[ 1 ] = theAmbientDelta[ 2 ] = 0.0f;
}

/// light 0 from that bearing and elevation, keeping the length its base had (the ground's light
/// takes the ray unnormalised, BaseHeightMapRenderObjClass::doTheLight)
static void aimLight( Coord3D &out, const Coord3D &base, Real bearing, Real elevation )
{
	const Real length = (Real)sqrt( base.x * base.x + base.y * base.y + base.z * base.z );
	const Real b = bearing * ( PI_REAL / 180.0f ), e = elevation * ( PI_REAL / 180.0f );
	const Real across = Cos( e );
	out.x = -length * across * Cos( b );
	out.y = -length * across * Sin( b );
	out.z = -length * Sin( e );
}

/// the sky a step stands for: its quantised bearing, elevation and fade
static void steppedSky( const DayNightStep &step, Bool fine, DayNightSky &out )
{
	const Real degrees = fine ? DAYNIGHT_FINE_STEP_DEGREES : DAYNIGHT_COARSE_STEP_DEGREES;
	out.bearing = (Real)step.bearing * degrees;
	out.elevation = (Real)step.elevation * degrees;
	out.shadow = (Real)step.shadow / ( fine ? (Real)DAYNIGHT_LIGHT_LEVELS : (Real)COARSE_FADE_STEPS );
	out.moon = step.moon;
	out.look = 0.0f;
}

//-------------------------------------------------------------------------------------------------
void DayNight_stepLight( const DayNightPalette &palette, const DayNightStep &step, Bool fine, Real depth, Bool sun,
	const RGBColor &baseAmbient, const RGBColor &baseDiffuse, const Coord3D &basePos,
	RGBColor &ambient, RGBColor &diffuse, Coord3D &pos )
{
	DayNightSky stepped;
	steppedSky( step, fine, stepped );
	const Real amount = (Real)step.level / (Real)DAYNIGHT_LIGHT_LEVELS;
	const Real low = palette.sunMoves ? DayNight_lowSun( stepped ) : 0.0f;
	Real dayAmbient[ 3 ], dayDiffuse[ 3 ], nightAmbient[ 3 ], nightDiffuse[ 3 ];
	for( Int i = 0; i < 3; ++i )
	{
		// a low sun is warmer and dimmer; at the peak, and for a sun that stays, the palette's day as it is
		dayAmbient[ i ] = palette.dayAmbient[ i ] * ( 1.0f + ( LOW_SUN_AMBIENT[ i ] - 1.0f ) * low );
		dayDiffuse[ i ] = palette.dayDiffuse[ i ] * ( 1.0f + ( LOW_SUN_DIFFUSE[ i ] - 1.0f ) * low );
		// a full depth takes the palette's night as written, so the Apocalypse's moves over to the bit
		nightAmbient[ i ] = palette.depthByMapSun
			? palette.dayAmbient[ i ] + ( palette.nightAmbient[ i ] - palette.dayAmbient[ i ] ) * depth : palette.nightAmbient[ i ];
		nightDiffuse[ i ] = palette.depthByMapSun
			? palette.dayDiffuse[ i ] + ( palette.nightDiffuse[ i ] - palette.dayDiffuse[ i ] ) * depth : palette.nightDiffuse[ i ];
	}
	DayNight_tint( ambient, baseAmbient, dayAmbient, nightAmbient, amount );
	DayNight_tint( diffuse, baseDiffuse, dayDiffuse, nightDiffuse, amount );
	pos = basePos;
	if( !sun || !palette.sunMoves )
		return;
	aimLight( pos, basePos, stepped.bearing, stepped.elevation );
	if( stepped.shadow < 1.0f )
	{
		/* The handover: the sun's own light goes with its shadow and comes back as the moon's, and what
			 a lit face loses the ambient takes about a third of back, so the turn of the light lands on a
			 frame with no direct light for it to move: nothing a pixel can show. */
		const Real lift = ( 1.0f - stepped.shadow ) * DIRECT_TO_AMBIENT;
		ambient.red += diffuse.red * lift;
		ambient.green += diffuse.green * lift;
		ambient.blue += diffuse.blue * lift;
		diffuse.red *= stepped.shadow;
		diffuse.green *= stepped.shadow;
		diffuse.blue *= stepped.shadow;
	}
}

//-------------------------------------------------------------------------------------------------
void DayNight_applyMapLight( const DayNightPalette &palette, UnsignedInt frame )
{
	if( TheWritableGlobalData == NULL )
		return;
	if( theKeptHour != TheGlobalData->m_timeOfDay )		// a new map, or a script changed the hour
		keepMapLight( palette );

	// the step this frame makes on this ground; nothing to do until one of its parts moves
	const Bool fine = !TheGlobalData->m_headless && TheGameClient != NULL && TheGameClient->isGroundLitEveryFrame();
	DayNightSky sky;
	DayNight_sky( frame, theNoonBearing, sky );
	if( !palette.sunMoves )
	{
		sky.bearing = theNoonBearing;
		sky.shadow = 1.0f;
		sky.moon = FALSE;
	}
	DayNightStep step;
	DayNight_step( DayNight_lightLevel( DayNight_amount( frame ) ), sky, fine, step );
	/* The night look (cloud shadow and water) and the stencil shadows' fade move on steps of their own,
		 none of which relights anything, so they ease where the light's step would show. */
	const Int lookStep = roundToInt( sky.look * (Real)LOOK_STEPS );
	const Int stencilStep = roundToInt( sky.shadow * (Real)STENCIL_STEPS );
	const Bool lightMoved = !( theStepApplied && theAppliedFine == fine && sameStep( step, theAppliedStep ) );
	if( !lightMoved && lookStep == theLookStep && stencilStep == theStencilStep )
		return;
	if( !TheGlobalData->m_headless && TheGameClient != NULL )
	{
		if( lookStep != theLookStep )
			TheGameClient->setNightLook( (Real)lookStep / (Real)LOOK_STEPS );
		if( stencilStep != theStencilStep )
			TheGameClient->setShadowFade( (Real)stencilStep / (Real)STENCIL_STEPS );		// the stencil shadows, -d3d9 and Classic
	}
	theLookStep = lookStep;
	theStencilStep = stencilStep;
	theNightLook = lookStep >= LOOK_STEPS;
	if( !lightMoved )
		return;
	/* The first step after a keep, and any step where the ground changed between lit every frame and
		 baked, rebuilds it whole: the tiles were baked with another ambient (the map hour's at the load,
		 the coarse path's tint) and a light-only step would leave it under them for the rest of the match. */
	const Bool wholeGround = !theStepApplied || theAppliedFine != fine;
	// on a fine ground the trees and bibs, which bake all of the light, follow the coarse steps only
	DayNightStep coarse;
	DayNight_step( DayNight_lightLevel( DayNight_amount( frame ) ), sky, FALSE, coarse );
	// and, so they never keep a stale bake for long (the noon held a 32.5 degree one), once the light
	// has moved and TREE_REFILL_FRAMES have passed since their last refill
	const Bool treesToo = wholeGround || !sameStep( coarse, theCoarseStep )
		|| frame < theTreeFrame || frame - theTreeFrame >= TREE_REFILL_FRAMES;
	if( treesToo )
		theTreeFrame = frame;
	theCoarseStep = coarse;
	theAppliedStep = step;
	theAppliedFine = fine;
	theStepApplied = TRUE;

	// everything below is the step's, never the frame's, so a step draws the same whenever it lands
	const UnsignedInt start = Clock_Milliseconds();
	const Real amount = (Real)step.level / (Real)DAYNIGHT_LIGHT_LEVELS;
	DayNightSky stepped;
	steppedSky( step, fine, stepped );

	/* The ground reads m_terrainAmbient, m_terrainDiffuse and m_terrainLightPos, GlobalData::
		 setTimeOfDay's copies of the hour's terrain light, and never the table itself, so the copies are
		 written too; the hour stays (the logic reads it).  A ground lit every frame keeps the ambient its
		 tiles baked, the base's, and takes the change as a shader term (DayNight_terrainAmbientDelta), so
		 a rebuild for any other reason bakes the same ambient. */
	const Int hour = TheGlobalData->m_timeOfDay;
	for( Int k = 0; k < MAX_GLOBAL_LIGHTS; ++k )
	{
		GlobalData::TerrainLighting terrain = theBaseTerrainLight[ k ];
		GlobalData::TerrainLighting objects = theBaseObjectLight[ k ];
		DayNight_stepLight( palette, step, fine, theDepth, k == 0, theBaseTerrainLight[ k ].ambient, theBaseTerrainLight[ k ].diffuse,
			theBaseTerrainLight[ k ].lightPos, terrain.ambient, terrain.diffuse, terrain.lightPos );
		DayNight_stepLight( palette, step, fine, theDepth, k == 0, theBaseObjectLight[ k ].ambient, theBaseObjectLight[ k ].diffuse,
			theBaseObjectLight[ k ].lightPos, objects.ambient, objects.diffuse, objects.lightPos );
		TheWritableGlobalData->m_terrainLighting[ hour ][ k ] = terrain;
		TheWritableGlobalData->m_terrainObjectsLighting[ hour ][ k ] = objects;
		// every reader takes the live ambient; only a tile that leaves the sun to the pixel bakes the
		// base (DayNight_bakedTerrainAmbient), and the shader adds the difference
		TheWritableGlobalData->m_terrainAmbient[ k ] = terrain.ambient;
		TheWritableGlobalData->m_terrainDiffuse[ k ] = terrain.diffuse;
		TheWritableGlobalData->m_terrainLightPos[ k ] = terrain.lightPos;
		if( k == 0 )
		{
			theAmbientDelta[ 0 ] = fine ? terrain.ambient.red - theBaseTerrainLight[ 0 ].ambient.red : 0.0f;
			theAmbientDelta[ 1 ] = fine ? terrain.ambient.green - theBaseTerrainLight[ 0 ].ambient.green : 0.0f;
			theAmbientDelta[ 2 ] = fine ? terrain.ambient.blue - theBaseTerrainLight[ 0 ].ambient.blue : 0.0f;
		}
	}

	// ponytail: 0.45 and 0.52 mirror SHADOW_MAP_STRENGTH and SHADOW_MAP_REFORGED_STRENGTH
	// (W3DVolumetricShadow.cpp), the strengths a 0 here stands for; move them to GlobalData if they change
	const Real dayStrength = theBaseShadowStrength > 0.0f ? theBaseShadowStrength
		: TheGlobalData->m_lightingStyle == LIGHTING_STYLE_REFORGED ? 0.52f : 0.45f;
	Real strength = step.level > 0 ? dayStrength * ( 1.0f - palette.shadowCut * amount ) : theBaseShadowStrength;
	if( stepped.shadow < 1.0f )		// a handover: the sun's map fades, as far as a strength above 0 goes
		strength = max( ( strength > 0.0f ? strength : dayStrength ) * stepped.shadow, FADED_SHADOW_STRENGTH );
	TheWritableGlobalData->m_shadowMapStrength = strength;

	// smoke and dust are unlit, so they take the night's darkening as a tint; fire and anything that
	// glows keeps its colour (W3DParticleSys.cpp smokeTintFor)
	const Int smokeStep = palette.sunMoves ? roundToInt( amount * (Real)SMOKE_STEPS ) : -1;
	if( smokeStep >= 0 && smokeStep != theSmokeStep && TheParticleSystemManager != NULL )
	{
		const Real a = (Real)smokeStep / (Real)SMOKE_STEPS;
		RGBColor tint;
		tint.red = 1.0f + ( NIGHT_SMOKE[ 0 ] - 1.0f ) * a;
		tint.green = 1.0f + ( NIGHT_SMOKE[ 1 ] - 1.0f ) * a;
		tint.blue = 1.0f + ( NIGHT_SMOKE[ 2 ] - 1.0f ) * a;
		TheParticleSystemManager->setSmokeTint( tint );
		theSmokeStep = smokeStep;
	}

	if( TheGlobalData->m_headless || TheGameClient == NULL )
		return;
	theGroundRetint = wholeGround ? DAYNIGHT_GROUND_FULL
		: !fine ? DAYNIGHT_GROUND_SPREAD
		: treesToo ? DAYNIGHT_GROUND_LIGHT_AND_TREES : DAYNIGHT_GROUND_LIGHT_ONLY;
	static const char *const GROUND[] = { "whole rebuild", "light only", "light and trees", "spread" };
	const char *ground = GROUND[ theGroundRetint ];
	const UnsignedInt tablesDone = Clock_Milliseconds();
	TheGameClient->refreshMapLight();
	theGroundRetint = DAYNIGHT_GROUND_FULL;
	// the tables, then the client's take (shadows, scene lights, the ground's marks); the refills and
	// a rebuild run on the next draw, under their own TERRAIN lines and in the pass's time
	const GlobalData::TerrainLighting &shown = TheGlobalData->m_terrainLighting[ hour ][ 0 ];
	DEBUG_LOG(( "DAYNIGHT: frame %u level %d of %d, %s at %d/%d degrees, shadow %d, stencil %d of %d, %s, tables %u ms, client %u ms, "
							"light 0 ambient %.4f %.4f %.4f diffuse %.4f %.4f %.4f\n",
							frame, step.level, (Int)DAYNIGHT_LIGHT_LEVELS, sky.moon ? "moon" : "sun",
							roundToInt( stepped.bearing ), roundToInt( stepped.elevation ), roundToInt( stepped.shadow * 100.0f ),
							theStencilStep, STENCIL_STEPS, ground, (UnsignedInt)( tablesDone - start ), (UnsignedInt)( Clock_Milliseconds() - tablesDone ),
							shown.ambient.red, shown.ambient.green, shown.ambient.blue, shown.diffuse.red, shown.diffuse.green, shown.diffuse.blue ));
}

//-------------------------------------------------------------------------------------------------
void DayNight_resetPicture( void )
{
	LogicThread::callAfterTick( []()
	{
		if( theKeptHour >= 0 )
		{
			if( TheWritableGlobalData != NULL )
				TheWritableGlobalData->m_shadowMapStrength = theBaseShadowStrength;
			if( TheGameClient != NULL )
			{
				TheGameClient->setShadowFade( 1.0f );
				if( theLookStep != 0 )
					TheGameClient->setNightLook( 0.0f );		// the cloud texture outlives the map
			}
		}
		if( theSmokeStep >= 0 && TheParticleSystemManager != NULL )
		{
			RGBColor white;
			white.red = white.green = white.blue = 1.0f;
			TheParticleSystemManager->setSmokeTint( white );
		}
		theSmokeStep = -1;
		theLookStep = 0;
		theTreeFrame = 0;
		// the water and the tables come back with the next map's own (W3DTerrainLogic::loadMap)
		theKeptHour = -1;
		theMapLightTaken = FALSE;
		theStepApplied = FALSE;
		theNightLook = FALSE;
		theAmbientDelta[ 0 ] = theAmbientDelta[ 1 ] = theAmbientDelta[ 2 ] = 0.0f;
	} );
}

//-------------------------------------------------------------------------------------------------
Real DayNight_lightAmount( void )
{
	return theKeptHour >= 0 && theStepApplied && theAppliedStep.level > 0
		? (Real)theAppliedStep.level / (Real)DAYNIGHT_LIGHT_LEVELS : 0.0f;
}

//-------------------------------------------------------------------------------------------------
Real DayNight_infantryLightScale( const Real scales[ TIME_OF_DAY_COUNT ] )
{
	const Int hour = TheGlobalData != NULL ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_AFTERNOON;
	if( theKeptHour < 0 || !theStepApplied )
		return scales[ hour ];
	// the base hour's scale by day, the map's night scale by the light on screen
	const Real day = scales[ theBaseHour ], night = scales[ TIME_OF_DAY_NIGHT ];
	return day + ( night - day ) * DayNight_lightAmount();
}

//-------------------------------------------------------------------------------------------------
Bool DayNight_mapNightCondition( void )
{
	if( DayNight_isOn() )
		return FALSE;
	return TheGlobalData != NULL && TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT;
}

//-------------------------------------------------------------------------------------------------
Int DayNight_groundRetint( void )
{
	return theGroundRetint;
}

//-------------------------------------------------------------------------------------------------
RGBColor DayNight_bakedTerrainAmbient( const RGBColor &live, Bool sunApart )
{
	if( sunApart && theKeptHour >= 0 && theStepApplied && theAppliedFine )
		return theBaseTerrainLight[ 0 ].ambient;
	return live;
}

//-------------------------------------------------------------------------------------------------
void DayNight_terrainAmbientDelta( Real out[ 3 ] )
{
	const Bool kept = theKeptHour >= 0;
	out[ 0 ] = kept ? theAmbientDelta[ 0 ] : 0.0f;
	out[ 1 ] = kept ? theAmbientDelta[ 1 ] : 0.0f;
	out[ 2 ] = kept ? theAmbientDelta[ 2 ] : 0.0f;
}

//-------------------------------------------------------------------------------------------------
Bool DayNight_nightLookFor( Bool mapNight )
{
	/* Under the cycle the cloud pass stays on all night with its texture faded to white
		 (CloudMapTerrainTextureClass::setStrength): dropping the pass put the ground and the roads on
		 other pipelines, built on the spot (181 ms), and changed the picture in one frame.  The map's
		 own rule, no clouds on a night map, holds with the cycle off. */
	if( theKeptHour >= 0 || DayNight_isOn() )
		return FALSE;
	return mapNight;
}
