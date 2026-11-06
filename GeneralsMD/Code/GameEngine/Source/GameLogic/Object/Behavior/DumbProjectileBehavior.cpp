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

// FILE: DumbProjectileBehavior.cpp
// Author: Steven Johnson, July 2002
// Desc:   

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/BezierSegment.h"
#include "Common/GameCommon.h"
#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/DumbProjectileBehavior.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Weapon.h"
#include "Platform/MsvcFloatCasts.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
const Int DEFAULT_MAX_LIFESPAN = 10 * LOGICFRAMES_PER_SECOND;

// A StraightWhenClear shell whose barrel is raised steeper than this slope is being lobbed, and takes
// the barrel's arc on open ground too. It is the barrel against level ground, or against the line to
// the victim when that climbs: a barrel that cannot dip (the Overlord's) stands well over a target close
// below it, and measured against the line a Paladin's shot 28 away and 13 degrees down went back to
// EA's arc, while a Crusader's barrel that pitches 12 degrees up at a tank on a ridge is aiming, not
// lobbing. A level barrel sits within 3 degrees of level (the logic does not tilt the hull on a slope);
// a Fire Base's stands 38 to 57 degrees up. The tangent of 8 degrees.
const Real STRAIGHT_SHOT_MAX_BARREL_SLOPE = 0.1405f;

//-----------------------------------------------------------------------------
DumbProjectileBehaviorModuleData::DumbProjectileBehaviorModuleData() : 
	m_maxLifespan(DEFAULT_MAX_LIFESPAN),
	m_detonateCallsKill(FALSE),
	m_orientToFlightPath(TRUE),
	m_tumbleRandomly(FALSE),
	m_firstHeight(0.0f),
	m_secondHeight(0.0f),
	m_firstPercentIndent(0.0f),
	m_secondPercentIndent(0.0f),
	m_straightWhenClear(FALSE),
	m_garrisonHitKillCount(0),
	m_garrisonHitKillFX(NULL),
	m_flightPathAdjustDistPerFrame(0.0f)
{
}

//-----------------------------------------------------------------------------
void DumbProjectileBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "MaxLifespan", INI::parseDurationUnsignedInt, NULL, offsetof( DumbProjectileBehaviorModuleData, m_maxLifespan ) },
		{ "TumbleRandomly", INI::parseBool, NULL, offsetof( DumbProjectileBehaviorModuleData, m_tumbleRandomly ) },
		{ "DetonateCallsKill", INI::parseBool, NULL, offsetof( DumbProjectileBehaviorModuleData, m_detonateCallsKill ) },
		{ "OrientToFlightPath", INI::parseBool, NULL, offsetof( DumbProjectileBehaviorModuleData, m_orientToFlightPath ) },

		{ "FirstHeight",					INI::parseReal,						NULL, offsetof( DumbProjectileBehaviorModuleData, m_firstHeight ) },
		{ "SecondHeight",					INI::parseReal,						NULL, offsetof( DumbProjectileBehaviorModuleData, m_secondHeight ) },
		{ "FirstPercentIndent",		INI::parsePercentToReal,	NULL, offsetof( DumbProjectileBehaviorModuleData, m_firstPercentIndent ) },
		{ "SecondPercentIndent",	INI::parsePercentToReal,	NULL, offsetof( DumbProjectileBehaviorModuleData, m_secondPercentIndent ) },
		{ "StraightWhenClear",		INI::parseBool,						NULL, offsetof( DumbProjectileBehaviorModuleData, m_straightWhenClear ) },

		{ "GarrisonHitKillRequiredKindOf", KindOfMaskType::parseFromINI, NULL, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillKindof ) },
		{ "GarrisonHitKillForbiddenKindOf", KindOfMaskType::parseFromINI, NULL, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillKindofNot ) },
		{ "GarrisonHitKillCount", INI::parseUnsignedInt, NULL, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillCount ) },
		{ "GarrisonHitKillFX", INI::parseFXList, NULL, offsetof( DumbProjectileBehaviorModuleData, m_garrisonHitKillFX ) },

		{ "FlightPathAdjustDistPerSecond", INI::parseVelocityReal, NULL, offsetof( DumbProjectileBehaviorModuleData, m_flightPathAdjustDistPerFrame ) },


		{ 0, 0, 0, 0 }
	};

  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
DumbProjectileBehavior::DumbProjectileBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_launcherID = INVALID_ID;
	m_victimID = INVALID_ID;
	m_detonationWeaponTmpl = NULL;
	m_lifespanFrame = 0;
	m_flightPath.clear();
	m_flightPathSegments = 0;
	m_flightPathSpeed = 0;
	m_launchSlope = 0;
	m_flightPathStart.zero();
	m_flightPathEnd.zero();
	m_aimOffset.zero();
	m_currentFlightPathStep = 0;
	m_extraBonusFlags = 0;

  m_hasDetonated = FALSE;
} 

//-------------------------------------------------------------------------------------------------
DumbProjectileBehavior::~DumbProjectileBehavior()
{
}

//-------------------------------------------------------------------------------------------------
inline Bool within(Real min, Real val, Real max)
{
	return min <= val && val <= max;
}

//-------------------------------------------------------------------------------------------------
inline Bool notWithin(Real min, Real val, Real max)
{
	return val < min || val > max;
}

#ifdef NOT_IN_USE

#define NO_ONLY_RETURN_CLIPPED_PITCHES

//-------------------------------------------------------------------------------------------------
static Bool calcTrajectory(
	const Coord3D& start,		// in: where the projectile starts
	const Coord3D& end,			// in: where the projectile wants to end up
	Real velocity,					// in: the initial speed of the projectile
	Real minPitch,					// in: min pitch (-PI/2)
	Real maxPitch,					// in: max pitch (-PI/2)
	Bool preferShortPitch,	// in: prefer the shorter or longer path?
	Real& angle,						// out: the angle to aim
	Real& pitch							// out: the pitch to aim for 
)
{
	Bool exactTarget = false;

	angle = 0.0f;
	pitch = 0.0f;

	if (velocity <= 0.0f)
	{
		DEBUG_CRASH(("cant get there from here (1)"));
		return false;
	}

	Real dx = end.x - start.x;
	Real dy = end.y - start.y;
	Real dz = end.z - start.z;

	// calculating the angle is trivial.
	angle = ATan2(dy, dx);

	// calculating the pitch requires a bit more effort.
	Real horizDistSqr = sqr(dx) + sqr(dy);
	Real horizDist = sqrt(horizDistSqr);

	// calc the two possible pitches that will cover the given horizontal range.
	// (this is actually only true if dz==0, but is a good first guess)
	Real gravity = fabs(TheGlobalData->m_gravity);
	Real gravityTwoDZ = gravity * 2.0f * dz;

	// let's start by aiming directly for it. we know this isn't right (unless gravity
	// is zero, which it's not) but is a good starting point...
	Real theta = ATan2(dz, horizDist);
	// if the angle isn't pretty shallow, we can get a better initial guess by using
	// the code below...
	const Real SHALLOW_ANGLE = 0.5f * PI / 180.0f;
	if (fabs(theta) > SHALLOW_ANGLE)
	{
		Real t = horizDist / velocity;
		Real vz = (dz/t + 0.5f*gravity*t);
		Real sineOfAngle = clamp(-1.0f, vz / velocity, 1.0f);
		theta = ASin(sineOfAngle)*0.5f;
	}

/*
	this is, in theory, the "right" formula for dz==0, but I
	get better results with the stuff above:

	Real sineOfAngle = (gravity * horizDist) / sqr(velocity);
	if (sineOfAngle > 1.0f)
	{
		return false;	
	}
	Real theta = ASin(sineOfAngle)*0.5f;
*/

	Real pitches[2];
	Real cosPitches[2];
	Real sinPitches[2];
	Real theta_min = max(minPitch, -PI/2);
	Real theta_max = min(maxPitch, PI/2);
	const Real MIN_ANGLE_DIFF = (PI/(180.0f*16.0f));	// 1/16th of a degree. yes, we need that accuracy.
//Int numLoops = 0;
	while (theta_max > theta_min + MIN_ANGLE_DIFF)
	{
//++numLoops;
		pitches[0] = theta;	// shallower angle
		pitches[1] = (theta >= 0.0) ? (PI/2 - theta) : (-PI/2 - theta);	// steeper angle
		
		DEBUG_ASSERTCRASH(pitches[0]<=PI/2&&pitches[0]>=-PI/2,("bad pitches[0] %f\n",rad2deg(pitches[0])));
		DEBUG_ASSERTCRASH(pitches[1]<=PI/2&&pitches[1]>=-PI/2,("bad pitches[1] %f\n",rad2deg(pitches[1])));

		// calc the horiz-speed & time for each.
		// note that time can only be negative for 90<angle<270, and since we
		// ruled those out above, we're gold.
		sinPitches[0] = Sin(pitches[0]);
		sinPitches[1] = Sin(pitches[1]);
		cosPitches[0] = Cos(pitches[0]); 
		cosPitches[1] = Cos(pitches[1]);
		Real t0 = (horizDist / (velocity * cosPitches[0]));
		Real t1 = (horizDist / (velocity * cosPitches[1]));

		t0 = MAX(0,t0);
		t1 = MAX(0,t1);


		DEBUG_ASSERTCRASH(t0>=0&&t1>=0,("neg time"));
		
		Int preferred = ((t0 < t1) == (preferShortPitch)) ? 0 : 1;

		// ok, NOW... since dz is virtually NEVER zero, do a little approximation
		// to get a better guess. (solving the equations directly are hard and
		// it's simpler to approximate)
		Bool tooClose = false;
		Real vx, vz, root;

		vz = velocity*sinPitches[preferred];

		root = sqr(vz) - gravityTwoDZ;
		if (root < 0.0f)
		{
			// oops, no solution for our preferred pitch. try the other one.
			if (preferred == 0)	
				tooClose = true;	// if this fails for the shallow case, it's 'cuz the result is too close
			preferred = 1 - preferred;
			vz = velocity*sinPitches[preferred];
			root = sqr(vz) - gravityTwoDZ;
			if (root < 0.0f)
			{
				DEBUG_CRASH(("cant get there from here (2)"));
				return false;
			}
		}

#ifdef ONLY_RETURN_CLIPPED_PITCHES
		// if we get this far, we have some plausible solution...
		// it actually may be off, but let's go ahead and set the result
		// (along with the "exact" flag) so that the caller will want to
		// try to use the result. (we'll probably iterate and overwrite
		// this result with a more precise one.)
		if (within(minPitch, pitches[preferred], maxPitch))
		{
			pitch = pitches[preferred];
			exactTarget = true;
		}
#else
		// if we get this far, we have some plausible solution...
		// it actually may be off, but let's go ahead and set the result
		// (along with the "exact" flag) so that the caller will want to
		// try to use the result. (we'll probably iterate and overwrite
		// this result with a more precise one.) NOTE however that we
		// don't validate it's within the proper range... caller must do that!
		pitch = pitches[preferred];
		exactTarget = true;
#endif

		vx = velocity*cosPitches[preferred];
		Real actualRange = (vx*(vz + sqrt(root)))/gravity;
		const Real CLOSE_ENOUGH_RANGE = 5.0f;
		if (tooClose || (actualRange < horizDist - CLOSE_ENOUGH_RANGE))
		{
			theta_min = theta;
			theta = (theta + theta_max) * 0.5f;
		}
		else if (actualRange > horizDist + CLOSE_ENOUGH_RANGE)
		{
			theta_max = theta;
			theta = (theta + theta_min) * 0.5f;
		}
		else
		{
			break;
		}
	}

//DEBUG_LOG(("took %d loops to find a match\n",numLoops));
	if (exactTarget)
		return true;

	return false;
}

#endif NOT_IN_USE

//-------------------------------------------------------------------------------------------------
// Prepares the missile for launch via proper weapon-system channels.
//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::projectileLaunchAtObjectOrPosition(
	const Object* victim, 
	const Coord3D* victimPos, 
	const Object* launcher, 
	WeaponSlotType wslot, 
	Int specificBarrelToUse, 
	const WeaponTemplate* detWeap,
	const ParticleSystemTemplate* exhaustSysOverride
)
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	DEBUG_ASSERTCRASH(specificBarrelToUse>=0, ("specificBarrelToUse must now be explicit"));

	m_launcherID = launcher ? launcher->getID() : INVALID_ID;
	m_extraBonusFlags = launcher ? launcher->getWeaponBonusCondition() : 0;
	m_victimID = victim ? victim->getID() : INVALID_ID;
	m_detonationWeaponTmpl = detWeap;
	m_lifespanFrame = TheGameLogic->getFrame() + d->m_maxLifespan;

	Object* projectile = getObject();

	Weapon::positionProjectileForLaunch(projectile, launcher, wslot, specificBarrelToUse);

	projectileFireAtObjectOrPosition( victim, victimPos, detWeap, exhaustSysOverride );
}

//-------------------------------------------------------------------------------------------------
// The actual firing of the missile once setup. Uses a Bezier curve with points parameterized in ini
//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::projectileFireAtObjectOrPosition( const Object *victim, const Coord3D *victimPos, const WeaponTemplate *detWeap, const ParticleSystemTemplate* exhaustSysOverride )
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();
	Object* projectile = getObject();
	Real weaponSpeed = detWeap ? detWeap->getWeaponSpeed() : 0.0f;
	Real minWeaponSpeed = detWeap ? detWeap->getMinWeaponSpeed() : 0.0f;

	// At an object, the weapon has picked the shot's point on its body (Weapon::randomPointOnBody) and
	// the shell flies there. A bridge's attack point is not on it, so a shell at a bridge aims at its
	// center, not the ground part.
	Coord3D victimPosToUse = *victimPos;
	m_aimOffset.zero();
	if (victim)
	{
		if (victim->isKindOf(KINDOF_BRIDGE))
			victim->getGeometryInfo().getCenterPosition(*victim->getPosition(), victimPosToUse);
		Weapon::bodyOffsetFromPoint(victim, victimPosToUse, m_aimOffset);
	}

	if( detWeap  &&  detWeap->isScaleWeaponSpeed() )
	{
		// Some weapons want to scale their start speed to the range
		Real minRange = detWeap->getMinimumAttackRange();
		Real maxRange = detWeap->getUnmodifiedAttackRange();
		Real range = sqrt(ThePartitionManager->getDistanceSquared( projectile, &victimPosToUse, FROM_CENTER_2D ) );
		// guard the degenerate min==max range and clamp: a shot inside the minimum range gave a
		// negative ratio and a speed below MinWeaponSpeed (possibly <= 0, which then made
		// ceil(dist/speed) a negative or infinite segment count below).
		Real rangeSpan = maxRange - minRange;
		Real rangeRatio = (rangeSpan > 0.0f) ? ((range - minRange) / rangeSpan) : 1.0f;
		rangeRatio = clamp( 0.0f, rangeRatio, 1.0f );
		m_flightPathSpeed = (rangeRatio * (weaponSpeed - minWeaponSpeed)) + minWeaponSpeed;
	}
	else
	{
		m_flightPathSpeed = weaponSpeed;
	}
	
 	PhysicsBehavior* physics = projectile->getPhysics();
 	if ( d->m_tumbleRandomly && physics)
	{
		const Real maxTumbleRate = ConvertRetailPerFrameRate( 1.0f/PI );
		physics->setPitchRate( GameLogicRandomValueReal( -maxTumbleRate, maxTumbleRate ) );
		physics->setYawRate( GameLogicRandomValueReal( -maxTumbleRate, maxTumbleRate ) );
		physics->setRollRate( GameLogicRandomValueReal( -maxTumbleRate, maxTumbleRate ) );
	}

	m_flightPathStart = *getObject()->getPosition();
	m_flightPathEnd = victimPosToUse;

	// the shell sits on the barrel here, pointing where the barrel points
	Vector3 barrel = getObject()->getTransformMatrix()->Get_X_Vector();
	Real barrelRun = sqrtf(sqr(barrel.X) + sqr(barrel.Y));
	m_launchSlope = (barrelRun > 0.0f) ? barrel.Z / barrelRun : 0.0f;
	if (!calcFlightPath(true))
	{
		//Can only fail if wildly incorrect points
		TheGameLogic->destroyObject( projectile );
		return;
	}
	m_currentFlightPathStep = 0;// We are at the first point, because the launching put us there
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool DumbProjectileBehavior::calcFlightPath(Bool recalcNumSegments)
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	Coord3D controlPoints[4];
	//First point is us, last point is them
	controlPoints[0] = m_flightPathStart;
	controlPoints[3] = m_flightPathEnd;

	Real highestInterveningTerrain;
	Bool onMap = ThePartitionManager->estimateTerrainExtremesAlongLine( controlPoints[0], controlPoints[3], NULL, &highestInterveningTerrain, NULL, NULL );
	if( !onMap )
	{
		return false;
	}

	// An artillery shell with a flight time flies a true parabola and lands that many frames after it
	// left, near or far, the way a gunner sets the charge and the elevation for a time on target. A
	// Bezier whose inner points sit a third of the way along the launch and landing tangents is that
	// parabola exactly, at an even pace, so one sample a frame is where the shell is each frame. The
	// turret raised the barrel to the same launch pitch (WeaponTemplate::getBallisticLaunchPitch), aimed
	// at the middle of the target, and the shot lands on a point of its body a few feet off that: the
	// peak takes up the difference, so the shell still leaves along the barrel. A barrel nowhere near
	// the solution (one held at its lowest pitch) moves the peak a quarter at most.
	UnsignedInt ballisticFrames = m_detonationWeaponTmpl ? m_detonationWeaponTmpl->getBallisticFlightFrames() : 0;
	if( ballisticFrames > 0 )
	{
		const Real LAUNCH_RISE_TO_PEAK_HEIGHT = 0.25f;
		const Real PEAK_HEIGHT_TO_INNER_POINT_RISE = 4.0f / 3.0f;
		const Real PEAK_HEIGHT_SHIFT_LIMIT = 0.25f;
		Coord3D span;
		span.set( &controlPoints[3] );
		span.sub( &controlPoints[0] );
		Real peakHeight = m_detonationWeaponTmpl->getBallisticPeakHeight();
		Real barrelPeakHeight = LAUNCH_RISE_TO_PEAK_HEIGHT * ( m_launchSlope * sqrtf( sqr( span.x ) + sqr( span.y ) ) - span.z );
		peakHeight = clamp( peakHeight * ( 1.0f - PEAK_HEIGHT_SHIFT_LIMIT ), barrelPeakHeight, peakHeight * ( 1.0f + PEAK_HEIGHT_SHIFT_LIMIT ) );
		Real innerRise = PEAK_HEIGHT_TO_INNER_POINT_RISE * peakHeight;
		for( Int inner = 1; inner <= 2; ++inner )
		{
			Real share = inner / 3.0f;
			controlPoints[inner].x = controlPoints[0].x + share * span.x;
			controlPoints[inner].y = controlPoints[0].y + share * span.y;
			controlPoints[inner].z = controlPoints[0].z + share * span.z + innerRise;
		}
		if( recalcNumSegments )
			m_flightPathSegments = ballisticFrames + 1;
		BezierSegment( controlPoints ).getSegmentPoints( m_flightPathSegments, &m_flightPath );
		return true;
	}

	// X and Y for inner points are along the line between us, so normalize and scale a vector between us, but
	// only use the x and y of the result
	Vector3 targetVector;// 0 origin vector between me and him
	targetVector.X = controlPoints[3].x - controlPoints[0].x;
	targetVector.Y = controlPoints[3].y - controlPoints[0].y;
	targetVector.Z = controlPoints[3].z - controlPoints[0].z;

	Real targetDistance = targetVector.Length();
	Real targetRun = sqrtf( sqr( targetVector.X ) + sqr( targetVector.Y ) );
	Real targetSlope = (targetRun > 0.0f) ? targetVector.Z / targetRun : 0.0f;
	targetVector.Normalize();
	Vector3 firstPointAlongLine = targetVector * (targetDistance * d->m_firstPercentIndent );
	Vector3 secondPointAlongLine = targetVector * (targetDistance * d->m_secondPercentIndent );

	controlPoints[1].x = firstPointAlongLine.X + controlPoints[0].x;// add world start to offset along the origin based vector
	controlPoints[1].y = firstPointAlongLine.Y + controlPoints[0].y;
	controlPoints[2].x = secondPointAlongLine.X + controlPoints[0].x;
	controlPoints[2].y = secondPointAlongLine.Y + controlPoints[0].y;

	// Z's are determined using the highest intervening height so they won't hit hills, low end bounded by current Zs
	highestInterveningTerrain = max( highestInterveningTerrain, controlPoints[0].z );
	highestInterveningTerrain = max( highestInterveningTerrain, controlPoints[3].z );
	controlPoints[1].z = highestInterveningTerrain + d->m_firstHeight;
	controlPoints[2].z = highestInterveningTerrain + d->m_secondHeight;

	if( d->m_straightWhenClear && m_launchSlope < max( targetSlope, 0.0f ) + STRAIGHT_SHOT_MAX_BARREL_SLOPE
		&& ThePartitionManager->isClearLineOfSightTerrain( NULL, controlPoints[0], NULL, controlPoints[3] ) )
	{
		// A cannon shell flies the line to its victim. The arc above was ten over the highest ground on the
		// way, so on flat ground a Crusader's shell climbed twelve degrees off the barrel and came down on
		// the target at twenty five; with the glowing trail on it, every shot read as a lob. A hill in the
		// way still lifts it, and so does a raised barrel.
		controlPoints[1].z = controlPoints[0].z + targetVector.Z * targetDistance * d->m_firstPercentIndent;
		controlPoints[2].z = controlPoints[0].z + targetVector.Z * targetDistance * d->m_secondPercentIndent;
	}
	else if( m_launchSlope > 0.0f && d->m_orientToFlightPath && !d->m_tumbleRandomly )
	{
		// A lobbed shell leaves along its raised barrel. The first control point sets the curve's first
		// direction, and over the highest ground it pointed off the barrel: a Nuke Cannon's shell left
		// six and a half degrees under its 39 degree barrel and jinked at the muzzle.
		controlPoints[1].z = controlPoints[0].z + m_launchSlope * sqrtf( sqr(firstPointAlongLine.X) + sqr(firstPointAlongLine.Y) );

		// A tank shell's own arc is ten over the ground, so the lob of a raised tank-shell barrel (the
		// Fire Base's howitzer) went up at the barrel's angle and slid in on the target almost flat. The
		// last control point now sits where the barrel's slope, run backwards from the target, puts it:
		// the shell comes down as steeply as it went up.
		if( d->m_straightWhenClear )
		{
			Real lastRun = sqrtf( sqr(controlPoints[3].x - controlPoints[2].x) + sqr(controlPoints[3].y - controlPoints[2].y) );
			controlPoints[2].z = max( controlPoints[2].z, controlPoints[3].z + m_launchSlope * lastRun );
		}
	}

	// With four control points, we have a curve.  We will decide how many frames we want to take to get to the target,
	// and fill our vector with those curve points.
	//
	// The curve is sampled where a retail (30 Hz) frame put the shell, and the frames in between fly the
	// straight line to the next sample, so a path looks and lasts as it did. Sampled at every frame, a
	// fast shot met points of the curve retail never visited: an instant tracer with FirstHeight 300
	// and WeaponSpeed 9999, which retail sampled at its two ends only, and at 60 Hz the third of four
	// samples flew it 200 into the air.
	BezierSegment flightCurve( controlPoints );
	Int retailSegments;
	if (recalcNumSegments)
	{
		Real flightDistance = flightCurve.getApproximateLength();
		retailSegments = floatToIntAsMsvc( (float)ceil( flightDistance / (m_flightPathSpeed * LOGICFRAMES_PER_RETAIL_FRAME) ) );
		// One sample is no path: the iterator hands back its zeroed point, not the muzzle, and EA's
		// point-blank shell flew to the map's corner and went off there. Two are the muzzle and the target.
		if (retailSegments == 1)
			retailSegments = 2;
	}
	else
	{
		retailSegments = m_flightPathSegments > 1 ? (m_flightPathSegments - 1) / LOGICFRAMES_PER_RETAIL_FRAME + 1 : m_flightPathSegments;
	}
	// A speed of zero or next to it asks for a flight of hundreds of
	// millions of frames, or an infinite one that comes back as INT_MIN. The path that size needs is
	// gigabytes, and a request past 2 GB came back from the allocator short and was written past its
	// end. Ten minutes of flight is more than any shell flies; past that the path is as wrong as one
	// off the map.
	const Int MAX_RETAIL_SEGMENTS = 10 * 60 * RETAIL_LOGICFRAMES_PER_SECOND;
	if (retailSegments < 0 || retailSegments > MAX_RETAIL_SEGMENTS)
	{
		DEBUG_LOG(("DumbProjectileBehavior: '%s' asked for a flight of %d frames at speed %g, dropped\n",
			getObject()->getTemplate()->getName().str(), retailSegments, m_flightPathSpeed));
		return false;
	}
	VecCoord3D retailPath;
	flightCurve.getSegmentPoints( retailSegments, &retailPath );
	m_flightPathSegments = retailSegments > 1 ? (retailSegments - 1) * LOGICFRAMES_PER_RETAIL_FRAME + 1 : retailSegments;
	m_flightPath.resize( m_flightPathSegments );
	for (Int i = 0; i < m_flightPathSegments; ++i)
	{
		Int sample = i / LOGICFRAMES_PER_RETAIL_FRAME;
		Int between = i % LOGICFRAMES_PER_RETAIL_FRAME;
		m_flightPath[i] = retailPath[sample];
		if (between != 0)
		{
			Real t = (Real)between / LOGICFRAMES_PER_RETAIL_FRAME;
			m_flightPath[i].x += t * (retailPath[sample + 1].x - retailPath[sample].x);
			m_flightPath[i].y += t * (retailPath[sample + 1].y - retailPath[sample].y);
			m_flightPath[i].z += t * (retailPath[sample + 1].z - retailPath[sample].z);
		}
	}

#if defined(_DEBUG) || defined(_INTERNAL)
	if( TheGlobalData->m_debugProjectilePath )
		displayFlightPath();
#endif

	return true;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool DumbProjectileBehavior::projectileHandleCollision( Object *other )
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	if (other != NULL)
	{
		Object *projectileLauncher = TheGameLogic->findObjectByID( projectileGetLauncherID() );

			// if it's not the specific thing we were targeting, see if we should incidentally collide...
		if (!m_detonationWeaponTmpl->shouldProjectileCollideWith(projectileLauncher, getObject(), other, m_victimID))
		{
			//DEBUG_LOG(("ignoring projectile collision with %s at frame %d\n",other->getTemplate()->getName().str(),TheGameLogic->getFrame()));
			return true;
		}

		if (d->m_garrisonHitKillCount > 0)
		{
			ContainModuleInterface* contain = other->getContain();
			if( contain && contain->getContainCount() > 0 && contain->isGarrisonable() && !contain->isImmuneToClearBuildingAttacks() )
			{
				Int numKilled = 0;

				// garrisonable buildings subvert the normal process here.
				const ContainedItemsList* items = contain->getContainedItemsList();
				if (items)
				{
					for (ContainedItemsList::const_iterator it = items->begin(); it != items->end() && numKilled < d->m_garrisonHitKillCount; )
					{
						Object* thingToKill = *it++;
						if (!thingToKill->isEffectivelyDead() && thingToKill->isKindOfMulti(d->m_garrisonHitKillKindof, d->m_garrisonHitKillKindofNot))
						{
							//DEBUG_LOG(("Killed a garrisoned unit (%08lx %s) via Flash-Bang!\n",thingToKill,thingToKill->getTemplate()->getName().str()));
							if (projectileLauncher)
								projectileLauncher->scoreTheKill( thingToKill );
							thingToKill->kill();
							++numKilled;
						}
					} // next contained item
				} // if items
				
				if (numKilled > 0)
				{
					// note, fx is played at center of building, not at grenade's location
					FXList::doFXObj(d->m_garrisonHitKillFX, other, NULL);

					getObject()->getControllingPlayer()->getAcademyStats()->recordClearedGarrisonedBuilding();

					// don't do the normal explosion; just destroy ourselves & return
					TheGameLogic->destroyObject(getObject());

					return true;
				}
			}	// if a garrisonable thing
		}

	}

	// collided with something... blow'd up!
	detonate();

	// mark ourself as "no collisions" (since we might still exist in slow death mode)
	getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_COLLISIONS ) );
	return true;
}

//-------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::detonate()
{
  if ( m_hasDetonated )
    return;

	Object* obj = getObject();
	if (m_detonationWeaponTmpl)
	{
		TheWeaponStore->handleProjectileDetonation(m_detonationWeaponTmpl, obj, obj->getPosition(), m_extraBonusFlags);

		if ( getDumbProjectileBehaviorModuleData()->m_detonateCallsKill )
		{
			// don't call kill(); do it manually, so we can specify DEATH_DETONATED
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
			damageInfo.in.m_deathType = DEATH_DETONATED;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
			obj->attemptDamage( &damageInfo );
		}
		else
		{
			TheGameLogic->destroyObject( obj );
		}

	}
	else
	{
		// don't call kill(); do it manually, so we can specify DEATH_DETONATED
		DamageInfo damageInfo;
		damageInfo.in.m_damageType = DAMAGE_UNRESISTABLE;
		damageInfo.in.m_deathType = DEATH_DETONATED;
		damageInfo.in.m_sourceID = INVALID_ID;
		damageInfo.in.m_amount = obj->getBodyModule()->getMaxHealth();
		obj->attemptDamage( &damageInfo );
	}

	if (obj->getDrawable())
		obj->getDrawable()->setDrawableHidden(true);
  
  m_hasDetonated = TRUE; 

}

//-------------------------------------------------------------------------------------------------
/**
 * Simulate one frame of a missile's behavior
 */
UpdateSleepTime DumbProjectileBehavior::update()
{
	const DumbProjectileBehaviorModuleData* d = getDumbProjectileBehaviorModuleData();

	if (m_lifespanFrame != 0 && TheGameLogic->getFrame() >= m_lifespanFrame)
	{
		// lifetime demands detonation
		detonate();
		return UPDATE_SLEEP_NONE;
	}

	if( m_currentFlightPathStep >= m_flightPath.size() )
	{
		// No more steps to use. Would go out of bounds on vector, so have to do something.
		// We could allow physics to take over and make us fall, but the point of this whole task
		// is to guarentee where the shell explodes.  This way, it _will_ explode at the target point.
		detonate();
		return UPDATE_SLEEP_NONE;
	}

	if (m_victimID != INVALID_ID && d->m_flightPathAdjustDistPerFrame > 0.0f)
	{
		Object* victim = TheGameLogic->findObjectByID(m_victimID);
		if (victim)
		{
			Coord3D newVictimPos;
			Weapon::pointFromBodyOffset(victim, m_aimOffset, newVictimPos);
			Coord3D delta;
			delta.x = newVictimPos.x - m_flightPathEnd.x;
			delta.y = newVictimPos.y - m_flightPathEnd.y;
			delta.z = newVictimPos.z - m_flightPathEnd.z;
			Real distVictimMovedSqr = sqr(delta.x) + sqr(delta.y) + sqr(delta.z);
			if (distVictimMovedSqr > 0.1f)
			{
				Real distVictimMoved = sqrtf(distVictimMovedSqr);
				if (distVictimMoved > d->m_flightPathAdjustDistPerFrame)
					distVictimMoved = d->m_flightPathAdjustDistPerFrame;
				delta.normalize();
				m_flightPathEnd.x += distVictimMoved * delta.x;
				m_flightPathEnd.y += distVictimMoved * delta.y;
				m_flightPathEnd.z += distVictimMoved * delta.z;
				if (!calcFlightPath(false))
				{
					DEBUG_CRASH(("Hmm, recalc of flight path returned false... should this happen?"));
					detonate();
					return UPDATE_SLEEP_NONE;
				}
			}
		}
	}

	//Otherwise, continue to force the flight path
	Coord3D flightStep = m_flightPath[m_currentFlightPathStep];

	if (d->m_orientToFlightPath && (!d->m_tumbleRandomly) )
  {
    if ( m_currentFlightPathStep > 0)
	  {
	  // this seems reasonable; however, if this object has a PhysicsBehavior on it, this calc will be wrong, 
	  // since Physics is applying gravity, which we duly ignore, but the prevPos won't be what we expect.
	  // get it from the flight path instead. (srj)
	  //Coord3D prevPos = *getObject()->getPosition();

		  Coord3D prevPos = m_flightPath[m_currentFlightPathStep - 1];

		  Vector3 curDir(flightStep.x - prevPos.x, flightStep.y - prevPos.y, flightStep.z - prevPos.z);
		  curDir.Normalize();	// buildTransformMatrix wants it this way
      Matrix3D orientMtx;
		  orientMtx.buildTransformMatrix(Vector3(flightStep.x, flightStep.y, flightStep.z), curDir);
		  getObject()->setTransformMatrix(&orientMtx);
    }
    else if ( m_flightPath.size() >= 2 ) // oops! how do we orient the projectile on the zeroeth frame? This didn't matter until we started using the
      //long, blurry projectile graphics which look badly oriented on step 0 of the flight path
      // so lets orient it the same as if it were on frame 1!
      // (a point blank shot has a single path point, and this used to read m_flightPath[1] anyway)
    {
		  Coord3D prevPos = m_flightPath[0];
		  Coord3D curPos = m_flightPath[1];

		  Vector3 curDir(curPos.x - prevPos.x, curPos.y - prevPos.y, curPos.z - prevPos.z);
		  curDir.Normalize();	// buildTransformMatrix wants it this way
      Matrix3D orientMtx;
		  orientMtx.buildTransformMatrix(Vector3(flightStep.x, flightStep.y, flightStep.z), curDir);
		  getObject()->setTransformMatrix(&orientMtx);
    }

	}
	else
	{
		getObject()->setPosition(&flightStep);
	}

	// note that we want to use getHighestLayerForDestination() here, so that anything even slightly
	// below the bridge translates into GROUND. (getLayerForDestination just does a "closest" check)
	PathfindLayerEnum oldLayer = getObject()->getLayer();
	PathfindLayerEnum newLayer = TheTerrainLogic->getHighestLayerForDestination(getObject()->getPosition());
	getObject()->setLayer(newLayer);

	if (oldLayer != LAYER_GROUND && newLayer == LAYER_GROUND)
	{
		// see if we' still in the bridge's xy area
		Coord3D tmp = *getObject()->getPosition();
		tmp.z = 9999.0f;
		PathfindLayerEnum testLayer = TheTerrainLogic->getHighestLayerForDestination(&tmp);
		if (testLayer == oldLayer)
		{
			// ensure we are slightly above the bridge, to account for fudge & sloppy art
			const Real FUDGE = 2.0f;
			tmp.z = TheTerrainLogic->getLayerHeight(tmp.x, tmp.y, testLayer) + FUDGE;
			getObject()->setPosition(&tmp);
			// blow'd up!
			detonate();
			return UPDATE_SLEEP_NONE;
		}
	}

	++m_currentFlightPathStep;

	return UPDATE_SLEEP_NONE;//This no longer flys with physics, so it needs to not sleep
}

// ------------------------------------------------------------------------------------------------
/** displayFlightPath for debugging */
// ------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
void DumbProjectileBehavior::displayFlightPath()
{
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	for( Int pointIndex = 0; pointIndex < m_flightPath.size(); ++pointIndex )
	{
		addIcon(&m_flightPath[pointIndex], TheGlobalData->m_debugProjectileTileWidth, 
										TheGlobalData->m_debugProjectileTileDuration, 
										TheGlobalData->m_debugProjectileTileColor);
	}
}
#endif

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::xfer( Xfer *xfer )
{

	// version 2 saves how far along its arc the shell is, and rebuilds the arc on load.  Version 1
	// saved neither: a shell in flight came back at step zero of a path it no longer had, so it
	// went off in the launcher's face instead of finishing its flight.
	// version 3 adds the launcher's weapon bonus flags, lost on load so a shell in flight detonated
	// without veterancy or horde bonus, and the detonated flag, so a DetonateCallsKill shell cannot
	// go off a second time
	// version 4 adds the barrel's slope at launch, which a lobbed shell's path starts along
	// version 5 adds the shot's point on its victim, which a shell following the victim keeps to
	XferVersion currentVersion = 5;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// launcher
	xfer->xferObjectID( &m_launcherID );

	// victim ID
	xfer->xferObjectID( &m_victimID );

	xfer->xferInt( &m_flightPathSegments );
	xfer->xferReal( &m_flightPathSpeed );
	xfer->xferCoord3D( &m_flightPathStart );
	xfer->xferCoord3D( &m_flightPathEnd );

	// weapon template
	AsciiString weaponTemplateName = AsciiString::TheEmptyString;
	if( m_detonationWeaponTmpl )
		weaponTemplateName = m_detonationWeaponTmpl->getName();
	xfer->xferAsciiString( &weaponTemplateName );
	if( xfer->getXferMode() == XFER_LOAD )
	{

		if( weaponTemplateName == AsciiString::TheEmptyString )
			m_detonationWeaponTmpl = NULL;
		else
		{

			// find template
			m_detonationWeaponTmpl = TheWeaponStore->findWeaponTemplate( weaponTemplateName );

			// sanity
			if( m_detonationWeaponTmpl == NULL )
			{

				DEBUG_CRASH(( "DumbProjectileBehavior::xfer - Unknown weapon template '%s'\n",
											weaponTemplateName.str() ));
				throw SC_INVALID_DATA;

			}  // end if

		}  // end else

	}  // end if

	// lifespan frame
	xfer->xferUnsignedInt( &m_lifespanFrame );

	if( version >= 2 )
		xfer->xferInt( &m_currentFlightPathStep );

	if( version >= 3 )
	{
		xfer->xferUnsignedInt( &m_extraBonusFlags );
		xfer->xferBool( &m_hasDetonated );
	}

	if( version >= 4 )
		xfer->xferReal( &m_launchSlope );

	if( version >= 5 )
		xfer->xferCoord3D( &m_aimOffset );

	// the path itself is a vector of points that is not saved; rebuild it from the start, end and
	// segment count that are, so the step we just read means something
	if( xfer->getXferMode() == XFER_LOAD && m_flightPathSegments > 0 )
		calcFlightPath( false );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DumbProjectileBehavior::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
