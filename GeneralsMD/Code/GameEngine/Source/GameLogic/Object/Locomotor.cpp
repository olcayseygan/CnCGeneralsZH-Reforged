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

// FILE: Locomotor.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, Feb 2002
// Desc:   Locomotor descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#define DEFINE_SURFACECATEGORY_NAMES
#define DEFINE_LOCO_Z_NAMES
#define DEFINE_LOCO_APPEARANCE_NAMES

#include "Common/INI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/AIUpdate.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static const Real DONUT_TIME_DELAY_SECONDS=2.5f;
static const Real DONUT_DISTANCE=4.0*PATHFIND_CELL_SIZE_F;

// Ground vehicles (Locomotor::isGroundVehicle). Speeds are per frame, as everywhere in this file.
static const Real VEHICLE_ACCEL_EASE_FRAMES = 6.0f;		// frames for the pull along the nose to build up to full Acceleration
static const Real VEHICLE_BRAKE_EASE_FRAMES = 3.0f;		// and to full braking, which a driver stamps on harder
static const Real VEHICLE_VELOCITY_FRAMES = 4.0f;			// a speed error is asked back over about this many frames
static const Real VEHICLE_STOP_EASE_FRAMES = 4.0f;		// a planned stop fades out over about this many frames
// a planned stop or bend is counted short by this many frames of travel, for the braking to build up in
static const Real VEHICLE_STOP_LEAD_FRAMES = VEHICLE_BRAKE_EASE_FRAMES + VEHICLE_VELOCITY_FRAMES;
static const Real VEHICLE_PLAN_DECEL_SHARE = 0.85f;		// a planned stop or bend brakes this much less hard than the vehicle can
static const Real VEHICLE_TRAFFIC_BRAKE = 2.0f;				// times its braking a vehicle may stamp on for traffic: slowed below its speed by the crowd, a bump or the group
static const Real VEHICLE_STOP_SECONDS = 0.8f;				// no vehicle takes longer than this to stop from top speed, whatever its Braking
static const Real VEHICLE_YAW_EASE_FRAMES = 6.0f;			// frames for a turn to wind up to TurnRate, or back down from it
static const Real VEHICLE_STILL_SHARE = 0.1f;					// below this share of top speed a vehicle may choose to go forward or back
static const Real VEHICLE_REVERSE_SHARE = 0.45f;			// share of top speed a vehicle backs up at
static const Real VEHICLE_REVERSE_BODY_LENGTHS = 3.0f;	// a goal behind and closer than this along the route is backed up to
static const Real VEHICLE_REVERSE_COS = -0.34f;				// cos 110 degrees: behind, for that
static const Real VEHICLE_BEND_COS = 0.985f;					// cos 10 degrees: a gentler bend is driven at full speed
static const Real VEHICLE_MIN_SLIDE = 0.08f;					// the slide onto the goal in the last cell moves at least this far a frame
static const Real TREAD_TURN_SPEED_FLOOR = 0.25f;			// share of top speed a tank keeps through a right-angle turn
static const Real TREAD_PIVOT_SHARE = 0.5f;						// share of TurnRate a tank pivots on the spot at
static const Real TREAD_PIVOT_COS = -0.17f;						// cos 100 degrees: further round than this a tank stops and pivots
static const Real VEHICLE_CORNER_GRIP = 0.5f;					// share of its braking a car can pull sideways on its tightest arc
static const Real WHEEL_MIN_RADIUS_BODY_LENGTHS = 1.5f;	// a car turns no tighter than this, unless that arc cannot reach the goal
static const Real WHEEL_TIGHT_RADIUS_BODY_LENGTHS = 0.3f;	// and then crawls round this, at its turn speed

// Infantry on two legs (moveTowardsPositionLegs, maintainCurrentPositionLegs).
static const Real INFANTRY_YAW_EASE_FRAMES = 2.0f;		// frames for a turn on the move to wind up to TurnRate, or back down from it
static const Real INFANTRY_STOP_FRAMES = 3.0f;				// a soldier stops from top speed in this many frames, whatever its Braking


#define MAX_BRAKING_FACTOR 5.0f
///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
LocomotorStore *TheLocomotorStore = NULL;					///< the Locomotor store definition

const Real BIGNUM = 99999.0f;
const Real SINE_DESCENT_LOOKAHEAD_FRAMES = 4.0f;
const Real INTERCEPT_EPSILON = 0.0001f;

static const char *TheLocomotorPriorityNames[] = 
{
	"MOVES_BACK",
	"MOVES_MIDDLE",
	"MOVES_FRONT",

	NULL
};


///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
static Real calcSlowDownDist(Real curSpeed, Real desiredSpeed, Real maxBraking)
{
	Real delta = curSpeed - desiredSpeed;
	if (delta <= 0)
		return 0.0f;

	Real dist = (sqr(delta) / fabs(maxBraking)) * 0.5f;

	// use a little fudge so that things can stop "on a dime" more easily...
	const Real FUDGE = 1.05f;
	return dist * FUDGE;
}

//-----------------------------------------------------------------------------
inline Bool isNearlyZero(Real a)
{
	const Real TINY_EPSILON = 0.001f;
	return fabs(a) < TINY_EPSILON;
}

//-----------------------------------------------------------------------------
inline Bool isNearly(Real a, Real val)
{
	const Real TINY_EPSILON = 0.001f;
	return fabs(a - val) < TINY_EPSILON;
}

//-----------------------------------------------------------------------------
// return the angle delta (in 3-space) we turned.
static Real tryToRotateVector3D(
	Real maxAngle,						// if negative, it's a percent (0...1) of the dist to rotate 'em
	const Vector3& inCurDir,
	const Vector3& inGoalDir,
	Vector3& actualDir
)
{
	if (isNearlyZero(maxAngle))
	{
		actualDir = inCurDir;
		return 0.0f;
	}

	Vector3 curDir = inCurDir;
	curDir.Normalize();

	Vector3 goalDir = inGoalDir;
	goalDir.Normalize();

	// dot of two unit vectors is cos of angle between them.
	Real cosine = Vector3::Dot_Product(curDir, goalDir);
	// bound it in case of numerical error
	Real angleBetween = (Real)ACos(clamp(-1.0f, cosine, 1.0f));

	if (maxAngle < 0)
	{
		maxAngle = -maxAngle * angleBetween;
		if (isNearlyZero(maxAngle))
		{
			actualDir = inCurDir;
			return 0.0f;
		}
	}

	if (fabs(angleBetween) <= maxAngle)
	{
		// close enough
		actualDir = goalDir;
	}
	else
	{
		// nah, try as much as we can in the right dir.
		// we need to rotate around the axis perpendicular to these two vecs.
		// but: cross of two vectors is the perpendicular axis!
#ifdef ALLOW_TEMPORARIES
		Vector3 objCrossGoal = Vector3::Cross_Product(curDir, goalDir);
		objCrossGoal.Normalize();
#else
		Vector3 objCrossGoal;
		Vector3::Normalized_Cross_Product(curDir, goalDir, &objCrossGoal);
#endif

		angleBetween = maxAngle;
		Matrix3D rotMtx(objCrossGoal, angleBetween);
		actualDir = rotMtx.Rotate_Vector(curDir);
	}
	
	return angleBetween;
}

//-------------------------------------------------------------------------------------------------
static Real tryToOrientInThisDirection3D(Object* obj, Real maxTurnRate, const Vector3& desiredDir)
{
	Vector3 actualDir;
	Real relAngle = tryToRotateVector3D(maxTurnRate, obj->getTransformMatrix()->Get_X_Vector(), desiredDir, actualDir);
	if (relAngle != 0.0f)
	{
		Vector3 objPos(obj->getPosition()->x, obj->getPosition()->y, obj->getPosition()->z);

		Matrix3D newXform;
		newXform.buildTransformMatrix( objPos, actualDir );

		obj->setTransformMatrix( &newXform );
	}
	return relAngle;
}

//-------------------------------------------------------------------------------------------------
// The turn this frame that keeps a projectile on the circle tangent to its nose through the goal. That
// circle turns through twice the angle between nose and goal over its length, so a step of `step`
// units turns 2*step*sin(angle)/dist, the same every frame for a goal that stands still. A goal behind
// the nose is chased straight (Locomotor_projectileChasesStraight), so past a right angle is only a
// moving victim's meeting point, and a loop of diameter dist brings the nose round to it.
// The step that arrives points straight at the goal.
static Real calcArcTurnToGoal(const Vector3& nose, const Vector3& toGoal, Real step)
{
	Real dist = toGoal.Length();
	if (step >= dist)
		return BIGNUM;

	Real cosine = Vector3::Dot_Product(nose, toGoal) / (nose.Length() * dist);
	Real sine = (cosine > 0.0f) ? sqrt(1.0f - sqr(cosine)) : 1.0f;
	return 2.0f * step * sine / dist;
}

//-------------------------------------------------------------------------------------------------
// Where a projectile flying straight at `speed` a frame meets a victim that keeps its velocity, as an
// offset from the projectile: the smallest t > 0 with |toVictim + victimVelocity*t| = speed*t. A victim
// standing still is its own meeting point, and one this speed never catches is aimed at directly.
Coord3D Locomotor_interceptOffset(const Coord3D& toVictim, const Coord3D& victimVelocity, Real speed)
{
	Real a = sqr(victimVelocity.x) + sqr(victimVelocity.y) + sqr(victimVelocity.z) - sqr(speed);
	Real b = 2.0f * (toVictim.x * victimVelocity.x + toVictim.y * victimVelocity.y + toVictim.z * victimVelocity.z);
	Real c = sqr(toVictim.x) + sqr(toVictim.y) + sqr(toVictim.z);

	Real meetingFrames = -1.0f;
	if (fabs(a) < INTERCEPT_EPSILON)
	{
		if (b < 0.0f)
			meetingFrames = -c / b;
	}
	else if (b * b - 4.0f * a * c >= 0.0f)
	{
		Real root = sqrt(b * b - 4.0f * a * c);
		Real firstFrames = (-b - root) / (2.0f * a);
		Real secondFrames = (-b + root) / (2.0f * a);
		if (firstFrames > secondFrames)
			std::swap(firstFrames, secondFrames);
		meetingFrames = (firstFrames > 0.0f) ? firstFrames : secondFrames;
	}

	if (meetingFrames <= 0.0f)
		return toVictim;

	Coord3D offset;
	offset.set(toVictim.x + victimVelocity.x * meetingFrames,
						 toVictim.y + victimVelocity.y * meetingFrames,
						 toVictim.z + victimVelocity.z * meetingFrames);
	return offset;
}

//-------------------------------------------------------------------------------------------------
// How far a victim on the ground moves before a projectile flying straight at `speed` a frame meets it,
// for the aim point that far ahead of the victim. Only the victim's travel over the ground counts, as in
// the arc after the lock: its velocity's climb over a slope is left out. Zero for a victim standing still.
Coord3D Locomotor_groundLead(const Coord3D& toVictim, const Coord3D& victimVelocity, Real speed)
{
	Coord3D flatVelocity = victimVelocity;
	flatVelocity.z = 0.0f;
	Coord3D meeting = Locomotor_interceptOffset(toVictim, flatVelocity, speed);
	Coord3D lead;
	lead.set(meeting.x - toVictim.x, meeting.y - toVictim.y, 0.0f);
	return lead;
}

//-------------------------------------------------------------------------------------------------
// A locked projectile chasing something in the air keeps EA's cheat and moves straight at it every
// frame. An aircraft is nearly as fast as the rocket, and neither arc could close on one: a rocket got
// within 11 of a helicopter, flew past it and climbed away with its nose 110 degrees off. Rockets at
// helicopters and jets over 1800 frames stayed locked p95 20 frames on the straight chase and never drew
// away; the arc took 26 and drew away 5 times, the arc onto the meeting point 35 and 17.
// A goal behind the nose takes the straight chase too. The arc to it bulges past the goal on the far
// side, and when the goal is on the ground below that is underground: a Comanche hovering over a
// supply truck put 4 of 4 missiles into the dirt 23 to 47 away, where from 250 out all 4 hit.
static Bool Locomotor_projectileChasesStraight(Object* obj, const Vector3& toGoal)
{
	Object *victim = obj->getAI()->getGoalObject();
	if (victim && victim->isAboveTerrain())
		return TRUE;

	return Vector3::Dot_Product(obj->getTransformMatrix()->Get_X_Vector(), toGoal) <= 0.0f;
}

//-------------------------------------------------------------------------------------------------
inline Real tryToOrientInThisDirection3D(Object* obj, Real maxTurnRate, const Coord3D* dir)
{
	return tryToOrientInThisDirection3D(obj, maxTurnRate, Vector3(dir->x, dir->y, dir->z));
}

//-----------------------------------------------------------------------------
static void calcDirectionToApplyThrust(
	const Object* obj, 
	const PhysicsBehavior* physics, 
	const Coord3D& ingoalPos,
	Real maxAccel,
	Vector3& goalDir
)
{
	/*
		our meta-goal here is to calculate the direction we should apply our motive force
		in order to minimize the angle between (our velocity) and (direction towards goalpos).

		this is complicated by the fact that we generally have an intrinsic velocity already,
		that must be accounted for, and by the fact that we can only apply force in our
		forward-x-direction (with a thrust-angle-range), and (due to limited range) might not 
		be able to apply the force in the optimal direction!
	*/

	// convert to Vector3, to use all its handy stuff
	Vector3 objPos(obj->getPosition()->x, obj->getPosition()->y, obj->getPosition()->z);
	Vector3 goalPos(ingoalPos.x, ingoalPos.y, ingoalPos.z);

	Vector3 vecToGoal = goalPos - objPos;
	if (isNearlyZero(vecToGoal.Length2()))
	{
		// goal pos is essentially same as current pos, so just stay the same & return
		goalDir = obj->getTransformMatrix()->Get_X_Vector();
		return;
	}

	/*
		get our cur vel into a useful Vector3 form
	*/
	Vector3 curVel(physics->getVelocity()->x, physics->getVelocity()->y, physics->getVelocity()->z);

	// add gravity to our vel so that we account for it in our calcs
	curVel.Z += TheGlobalData->m_gravity;

	Bool foundSolution = false;
	Real distToGoalSqr = vecToGoal.Length2();
	Real distToGoal = sqrt(distToGoalSqr);
	Real curVelMagSqr = curVel.Length2();
	Real curVelMag = sqrt(curVelMagSqr);
	Real maxAccelSqr = sqr(maxAccel);

	Real denom = curVelMagSqr - maxAccelSqr;
	if (!isNearlyZero(denom))
	{
		// solve the (greatly simplified) quadratic...
		Real t = (distToGoal * (curVelMag + maxAccel)) / denom;
		Real t2 = (distToGoal * (curVelMag - maxAccel)) / denom;
		if (t >= 0 || t2 >= 0)
		{
			// choose the smallest positive t.
			if (t < 0 || (t2 >= 0 && t2 < t))
				t = t2;
			
			// plug it in.
			if (!isNearlyZero(t))
			{
				goalDir.X = (vecToGoal.X / t) - curVel.X;
				goalDir.Y = (vecToGoal.Y / t) - curVel.Y;
				goalDir.Z = (vecToGoal.Z / t) - curVel.Z;
				goalDir.Normalize();
				foundSolution = true;
			}
		}
	}
	if (!foundSolution)
	{
		// Doh... no (useful) solution. revert to dumb.
		goalDir = vecToGoal;
		goalDir.Normalize();
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
LocomotorTemplate::LocomotorTemplate()
{
	// these values mean "make the same as undamaged if not explicitly specified"
	m_maxSpeedDamaged = -1.0f;
	m_maxTurnRateDamaged = -1.0f;
	m_accelerationDamaged = -1.0f;
	m_liftDamaged = -1.0f;

	m_surfaces = 0;
	m_maxSpeed = 0.0f;
	m_maxTurnRate = 0.0f;
	m_acceleration = 0.0f;
	m_lift = 0.0f;
	m_braking = BIGNUM;
	m_minSpeed = 0.0f;
	m_minTurnSpeed = BIGNUM;
	m_behaviorZ = Z_NO_Z_MOTIVE_FORCE;
	m_appearance = LOCO_OTHER;
	m_movePriority = LOCO_MOVES_MIDDLE;
	m_preferredHeight = 0;
	m_preferredHeightDamping = 1.0f;
	m_circlingRadius = 0;

	m_maxThrustAngle = 0;
	m_speedLimitZ = 999999.0f;
	m_extra2DFriction = 0.0f;

	m_accelPitchLimit = 0;
	m_decelPitchLimit = 0;
	m_bounceKick = 0;

//	m_pitchStiffness = 0;
//	m_rollStiffness = 0;
//	m_pitchDamping = 0;
//	m_rollDamping = 0;
// it's highly unlikely you want zero for the defaults for stiffness and damping... (srj)
// for stiffness: stiffness of the "springs" in the suspension 0 = no stiffness, 1 = totally stiff (huh huh, he said "stiff")
// for damping: 0=perfect spring, bounces forever.  1=glued to terrain.
	m_pitchStiffness = 0.1f;
	m_rollStiffness = 0.1f;
	m_pitchDamping = 0.9f;
	m_rollDamping = 0.9f;
	m_forwardVelCoef = 0;
	m_pitchByZVelCoef = 0;
	m_thrustRoll = 0.0f;
	m_wobbleRate = 0.0f;
	m_minWobble = 0.0f;
	m_maxWobble = 0.0f;
	m_lateralVelCoef = 0;
	m_forwardAccelCoef = 0;
	m_lateralAccelCoef = 0;
	m_uniformAxialDamping = 1.0f;
	m_turnPivotOffset = 0;
	m_apply2DFrictionWhenAirborne = false;
	m_downhillOnly = false;
	m_allowMotiveForceWhileAirborne = false;
	m_locomotorWorksWhenDead = false;
	m_airborneTargetingHeight = INT_MAX;
	m_stickToGround = false;
	m_canMoveBackward = false;
	m_hasSuspension = false;
	m_wheelTurnAngle = 0;
	m_maximumWheelExtension = 0;
	m_maximumWheelCompression = 0;
	m_closeEnoughDist = 1.0f;
	m_isCloseEnoughDist3D = FALSE;
	m_ultraAccurateSlideIntoPlaceFactor = 0.0f;

	m_wanderWidthFactor = 0.0f;
	m_wanderLengthFactor = 1.0f;
	m_wanderAboutPointRadius = 0.0f;
  
	m_rudderCorrectionDegree    = 0.0f;
	m_rudderCorrectionRate      = 0.0f;	
	m_elevatorCorrectionDegree  = 0.0f;
	m_elevatorCorrectionRate    = 0.0f;

}

//-------------------------------------------------------------------------------------------------
LocomotorTemplate::~LocomotorTemplate()
{

}

//-------------------------------------------------------------------------------------------------
void LocomotorTemplate::validate()
{
	// this is ok; parachutes need it!
	//DEBUG_ASSERTCRASH(m_lift == 0.0f || m_lift > fabs(TheGlobalData->m_gravity), ("Lift is too low to counteract gravity!"));
	//DEBUG_ASSERTCRASH(m_liftDamaged == 0.0f || m_liftDamaged > fabs(TheGlobalData->m_gravity), ("LiftDamaged is too low to counteract gravity!"));
	//DEBUG_ASSERTCRASH(m_preferredHeight == 0.0f || (m_behaviorZ == Z_SURFACE_RELATIVE_HEIGHT || m_behaviorZ == Z_ABSOLUTE_HEIGHT || m_appearance == LOCO_THRUST), 
	//	("You must use Z_SURFACE_RELATIVE_HEIGHT or Z_ABSOLUTE_HEIGHT (or THRUST) to use preferredHeight"));

	// for 'damaged' stuff that was omitted, set 'em to be the same as 'undamaged'...
	if (m_maxSpeedDamaged < 0.0f)
		m_maxSpeedDamaged = m_maxSpeed;
	
	if (m_maxTurnRateDamaged < 0.0f)
		m_maxTurnRateDamaged = m_maxTurnRate;

	if (m_accelerationDamaged < 0.0f)
		m_accelerationDamaged = m_acceleration;

	if (m_liftDamaged < 0.0f)
		m_liftDamaged = m_lift;

	if (m_appearance == LOCO_WINGS)
	{
		if (m_minSpeed <= 0.0f)
		{
			DEBUG_CRASH(("WINGS should always have positive minSpeeds (otherwise, they hover)"));
			m_minSpeed = 0.01f;
		}
		if (m_minTurnSpeed <= 0.0f)
		{
			DEBUG_CRASH(("WINGS should always have positive minTurnSpeed"));
			m_minTurnSpeed = 0.01f;
		}
	}

	if (m_appearance == LOCO_THRUST)
	{
		if (m_behaviorZ != Z_NO_Z_MOTIVE_FORCE ||
				m_lift != 0.0f ||
				m_liftDamaged != 0.0f)
		{
			DEBUG_CRASH(("THRUST locos may not use ZAxisBehavior or lift!\n"));
			throw INI_INVALID_DATA;
		}
		if (m_maxSpeed <= 0.0f)
		{
			// if one of these was omitted, it defaults to zero... just quietly heal it here, rather than crashing
			DEBUG_LOG(("THRUST locos may not have zero m_maxSpeed; healing...\n"));
			m_maxSpeed = 0.01f;
		}
		if (m_maxSpeedDamaged <= 0.0f)
		{
			// if one of these was omitted, it defaults to zero... just quietly heal it here, rather than crashing
			DEBUG_LOG(("THRUST locos may not have zero m_maxSpeedDamaged; healing...\n"));
			m_maxSpeedDamaged = 0.01f;
		}
		if (m_minSpeed <= 0.0f)
		{
			// if one of these was omitted, it defaults to zero... just quietly heal it here, rather than crashing
			DEBUG_LOG(("THRUST locos may not have zero m_minSpeed; healing...\n"));
			m_minSpeed = 0.01f;
		}
	}
}

//-------------------------------------------------------------------------------------------------
static void parseFrictionPerSec( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	Real fricPerSec = INI::scanReal(ini->getNextToken());
	Real fricPerFrame = fricPerSec * SECONDS_PER_LOGICFRAME_REAL;
	*(Real *)store = fricPerFrame;
} 

//-------------------------------------------------------------------------------------------------
const FieldParse* LocomotorTemplate::getFieldParse() const  
{
	static const FieldParse TheFieldParse[] =
	{
		{ "Surfaces", INI::parseBitString32, TheLocomotorSurfaceTypeNames, offsetof(LocomotorTemplate, m_surfaces) },		
		{ "Speed", INI::parseVelocityReal, NULL, offsetof(LocomotorTemplate, m_maxSpeed) },		
		{ "SpeedDamaged", INI::parseVelocityReal, NULL, offsetof( LocomotorTemplate, m_maxSpeedDamaged ) },
		{ "TurnRate", INI::parseAngularVelocityReal, NULL, offsetof(LocomotorTemplate, m_maxTurnRate) },		
		{ "TurnRateDamaged", INI::parseAngularVelocityReal, NULL, offsetof( LocomotorTemplate, m_maxTurnRateDamaged ) },
		{ "Acceleration", INI::parseAccelerationReal, NULL, offsetof(LocomotorTemplate, m_acceleration) },		
		{ "AccelerationDamaged", INI::parseAccelerationReal, NULL, offsetof( LocomotorTemplate, m_accelerationDamaged ) },
		{ "Lift", INI::parseAccelerationReal, NULL, offsetof(LocomotorTemplate, m_lift) },		
		{ "LiftDamaged", INI::parseAccelerationReal, NULL, offsetof( LocomotorTemplate, m_liftDamaged ) },
		{ "Braking", INI::parseAccelerationReal, NULL, offsetof(LocomotorTemplate, m_braking) },		
		{ "MinSpeed", INI::parseVelocityReal, NULL, offsetof(LocomotorTemplate, m_minSpeed) },		
		{ "MinTurnSpeed", INI::parseVelocityReal, NULL, offsetof(LocomotorTemplate, m_minTurnSpeed) },		
		{ "PreferredHeight", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_preferredHeight) },
		{ "PreferredHeightDamping", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_preferredHeightDamping) },
		{ "CirclingRadius", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_circlingRadius) },
		{ "Extra2DFriction", parseFrictionPerSec, NULL, offsetof(LocomotorTemplate, m_extra2DFriction) },
		{ "SpeedLimitZ", INI::parseVelocityReal, NULL, offsetof(LocomotorTemplate, m_speedLimitZ) },
		{ "MaxThrustAngle", INI::parseAngleReal, NULL, offsetof(LocomotorTemplate, m_maxThrustAngle) },		// yes, angle, not angular-vel
		{ "ZAxisBehavior", INI::parseIndexList, TheLocomotorBehaviorZNames, offsetof(LocomotorTemplate, m_behaviorZ) },		
		{ "Appearance", INI::parseIndexList, TheLocomotorAppearanceNames, offsetof(LocomotorTemplate, m_appearance) },		\
		{ "GroupMovementPriority", INI::parseIndexList, TheLocomotorPriorityNames, offsetof(LocomotorTemplate, m_movePriority) },		\

		{ "AccelerationPitchLimit", INI::parseAngleReal, NULL, offsetof(LocomotorTemplate, m_accelPitchLimit) },
		{ "DecelerationPitchLimit", INI::parseAngleReal, NULL, offsetof(LocomotorTemplate, m_decelPitchLimit) },
		{ "BounceAmount", INI::parseAngularVelocityReal, NULL, offsetof(LocomotorTemplate, m_bounceKick) },		
		{ "PitchStiffness", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_pitchStiffness) },		
		{ "RollStiffness", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_rollStiffness) },		
		{ "PitchDamping", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_pitchDamping) },		
		{ "RollDamping", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_rollDamping) },		
		{ "ThrustRoll", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_thrustRoll) },
		{ "ThrustWobbleRate",	INI::parseReal, NULL, offsetof(LocomotorTemplate, m_wobbleRate) },
		{ "ThrustMinWobble",	INI::parseReal, NULL, offsetof(LocomotorTemplate, m_minWobble) },
		{ "ThrustMaxWobble",	INI::parseReal, NULL, offsetof(LocomotorTemplate, m_maxWobble) },
		{ "PitchInDirectionOfZVelFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_pitchByZVelCoef) },		
		{ "ForwardVelocityPitchFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_forwardVelCoef) },		
		{ "LateralVelocityRollFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_lateralVelCoef) },		
		{ "ForwardAccelerationPitchFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_forwardAccelCoef) },		
		{ "LateralAccelerationRollFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_lateralAccelCoef) },		
		{ "UniformAxialDamping", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_uniformAxialDamping) },		
		{ "TurnPivotOffset", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_turnPivotOffset) },		
		{ "Apply2DFrictionWhenAirborne", INI::parseBool, NULL, offsetof(LocomotorTemplate, m_apply2DFrictionWhenAirborne) },
		{ "DownhillOnly", INI::parseBool, NULL, offsetof(LocomotorTemplate, m_downhillOnly) },		
		{ "AllowAirborneMotiveForce", INI::parseBool, NULL, offsetof(LocomotorTemplate, m_allowMotiveForceWhileAirborne) },
		{ "LocomotorWorksWhenDead", INI::parseBool, NULL, offsetof(LocomotorTemplate, m_locomotorWorksWhenDead) },
		{ "AirborneTargetingHeight", INI::parseInt, NULL, offsetof( LocomotorTemplate, m_airborneTargetingHeight ) },
		{ "StickToGround",				INI::parseBool,			NULL,	offsetof(LocomotorTemplate, m_stickToGround) },		
		{ "CanMoveBackwards",				INI::parseBool,			NULL,	offsetof(LocomotorTemplate, m_canMoveBackward) },		
		{ "HasSuspension",				INI::parseBool,			NULL,	offsetof(LocomotorTemplate, m_hasSuspension) },		
		{ "FrontWheelTurnAngle", INI::parseAngleReal, NULL, offsetof(LocomotorTemplate, m_wheelTurnAngle) },		
		{ "MaximumWheelExtension", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_maximumWheelExtension) },		
		{ "MaximumWheelCompression", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_maximumWheelCompression) },		
		{ "CloseEnoughDist",				 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_closeEnoughDist) },
		{ "CloseEnoughDist3D",			 INI::parseBool, NULL, offsetof(LocomotorTemplate, m_isCloseEnoughDist3D) },
		{ "SlideIntoPlaceTime",		INI::parseDurationReal, NULL, offsetof(LocomotorTemplate, m_ultraAccurateSlideIntoPlaceFactor) },

		{ "WanderWidthFactor", INI::parseReal, NULL, offsetof(LocomotorTemplate, m_wanderWidthFactor) },		
		{ "WanderLengthFactor",				 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_wanderLengthFactor) },
		{ "WanderAboutPointRadius",				 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_wanderAboutPointRadius) },

		{ "RudderCorrectionDegree",		 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_rudderCorrectionDegree) },
		{ "RudderCorrectionRate",			 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_rudderCorrectionRate) },
		{ "ElevatorCorrectionDegree",	 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_elevatorCorrectionDegree) },
		{ "ElevatorCorrectionRate",		 INI::parseReal, NULL, offsetof(LocomotorTemplate, m_elevatorCorrectionRate) },
		{ NULL, NULL, NULL, 0 }  // keep this last	
	
	};
	return TheFieldParse;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
LocomotorStore::LocomotorStore()
{
} 

//-------------------------------------------------------------------------------------------------
LocomotorStore::~LocomotorStore()
{
	// delete all the templates, then clear out the table.
	LocomotorTemplateMap::iterator it;
	for (it = m_locomotorTemplates.begin(); it != m_locomotorTemplates.end(); ++it) {
		it->second->deleteInstance();
	}

	m_locomotorTemplates.clear();
}

//-------------------------------------------------------------------------------------------------
LocomotorTemplate* LocomotorStore::findLocomotorTemplate(NameKeyType namekey)
{
	if (namekey == NAMEKEY_INVALID)
		return NULL;

  LocomotorTemplateMap::iterator it = m_locomotorTemplates.find(namekey);
  if (it == m_locomotorTemplates.end()) 
		return NULL;
	else
		return (*it).second;
}

//-------------------------------------------------------------------------------------------------
const LocomotorTemplate* LocomotorStore::findLocomotorTemplate(NameKeyType namekey) const
{
	if (namekey == NAMEKEY_INVALID)
		return NULL;

  LocomotorTemplateMap::const_iterator it = m_locomotorTemplates.find(namekey);
  if (it == m_locomotorTemplates.end()) 
	{
		return NULL;
	}
	else
	{
		return (*it).second;
	}
}

//-------------------------------------------------------------------------------------------------
void LocomotorStore::update()
{
}

//-------------------------------------------------------------------------------------------------
void LocomotorStore::reset()
{
	// cleanup overrides.
	LocomotorTemplateMap::iterator it;
	for (it = m_locomotorTemplates.begin(); it != m_locomotorTemplates.end(); ) {
		Overridable *locoTemp = it->second->deleteOverrides();
		if (!locoTemp)
		{
			// Port defect 32 (fixed the same way upstream): erase(it) left it dangling, and the loop went on from
			// the freed node. A locomotor a map.ini made from nothing (the shipped test map Hovercraft; any
			// custom map, whose map.ini travels with a map transfer) lands here: the game hung (Mac), crashed
			// (Windows, Linux) at the end of the match, and upstream saw the heap damage crash a save load.
			it = m_locomotorTemplates.erase(it);
		}
		else
		{
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
LocomotorTemplate *LocomotorStore::newOverride( LocomotorTemplate *locoTemplate )
{
	if (locoTemplate == NULL)
		return NULL;

	// allocate new template
	LocomotorTemplate *newTemplate = newInstance(LocomotorTemplate);

	// copy data from final override to 'newTemplate' as a set of initial default values
	*newTemplate = *locoTemplate;
	locoTemplate->setNextOverride(newTemplate);

	newTemplate->markAsOverride();

	// return the newly created override for us to set values with etc
	return newTemplate;

}  // end newOverride

//-------------------------------------------------------------------------------------------------
/*static*/ void LocomotorStore::parseLocomotorTemplateDefinition(INI* ini)
{
	if (!TheLocomotorStore)
		throw INI_INVALID_DATA;

	Bool isOverride = false;
	// read the Locomotor name
	const char* token = ini->getNextToken();
	NameKeyType namekey = NAMEKEY(token);
	
	LocomotorTemplate *loco = TheLocomotorStore->findLocomotorTemplate(namekey);
	if (loco) {
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
			loco = TheLocomotorStore->newOverride((LocomotorTemplate*) loco->friend_getFinalOverride());
		} 
		isOverride = true;
	} else {
		loco = newInstance(LocomotorTemplate);
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
			loco->markAsOverride();
		}
	}

	loco->friend_setName(token);
	ini->initFromINI(loco, loco->getFieldParse());
	loco->validate();
	
	// if this is an override, then we want the pointer on the existing named locomotor to point us 
	// to the override, so don't add it to the map.
	if (!isOverride)
		TheLocomotorStore->m_locomotorTemplates[namekey] = loco;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseLocomotorTemplateDefinition( INI* ini )
{
	LocomotorStore::parseLocomotorTemplateDefinition(ini);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
Locomotor::Locomotor(const LocomotorTemplate* tmpl)
{
	m_template = tmpl;
	m_brakingFactor = 1.0f;
	m_maxLift = BIGNUM;
	m_maxSpeed = BIGNUM;
	m_speedScale = 1.0f;
	m_maxAccel = BIGNUM;
	m_maxBraking = BIGNUM;
	m_maxTurnRate = BIGNUM;
	m_flags = 0;
	m_closeEnoughDist = m_template->m_closeEnoughDist;
	setFlag(IS_CLOSE_ENOUGH_DIST_3D, m_template->m_isCloseEnoughDist3D);
#ifdef CIRCLE_FOR_LANDING
	m_circleThresh = 0.0f;
#endif
	m_preferredHeight = m_template->m_preferredHeight;
	m_preferredHeightDamping = m_template->m_preferredHeightDamping;
	m_sineDescentDistance = 0.0f;

	m_angleOffset = GameLogicRandomValueReal(-PI/6, PI/6);
	m_offsetIncrement = (PI/40) * (GameLogicRandomValueReal(0.8f, 1.2f)/m_template->m_wanderLengthFactor);
	setFlag(OFFSET_INCREASING, GameLogicRandomValue(0,1));
	m_donutTimer = TheGameLogic->getFrame()+DONUT_TIME_DELAY_SECONDS*LOGICFRAMES_PER_SECOND;
	m_yawRate = 0.0f;
	m_driveAccelX = 0.0f;
	m_driveAccelY = 0.0f;
	m_driveSpeed = 0.0f;
	m_driveFrame = 0;
}

//-------------------------------------------------------------------------------------------------
Locomotor::Locomotor(const Locomotor& that)
{
	//Added By Sadullah Nader
	//Initializations 
	m_angleOffset = 0.0f;
	m_maintainPos.zero();

	//
	
	m_template = that.m_template;
	m_brakingFactor = that.m_brakingFactor;
	m_maxLift = that.m_maxLift;
	m_maxSpeed = that.m_maxSpeed;
	m_speedScale = that.m_speedScale;
	m_maxAccel = that.m_maxAccel;
	m_maxBraking = that.m_maxBraking;
	m_maxTurnRate = that.m_maxTurnRate;
	m_flags = that.m_flags;
	m_closeEnoughDist = that.m_closeEnoughDist;
#ifdef CIRCLE_FOR_LANDING
	m_circleThresh = that.m_circleThresh;
#endif
	m_preferredHeight = that.m_preferredHeight;
	m_preferredHeightDamping = that.m_preferredHeightDamping;
	m_sineDescentDistance = that.m_sineDescentDistance;
	m_angleOffset = that.m_angleOffset;
	m_offsetIncrement = that.m_offsetIncrement;
	m_yawRate = that.m_yawRate;
	m_driveAccelX = that.m_driveAccelX;
	m_driveAccelY = that.m_driveAccelY;
	m_driveSpeed = that.m_driveSpeed;
	m_driveFrame = that.m_driveFrame;
}

//-------------------------------------------------------------------------------------------------
Locomotor& Locomotor::operator=(const Locomotor& that)
{
	if (this != &that)
	{
		m_template = that.m_template;
		m_brakingFactor = that.m_brakingFactor;
		m_maxLift = that.m_maxLift;
		m_maxSpeed = that.m_maxSpeed;
		m_speedScale = that.m_speedScale;
		m_maxAccel = that.m_maxAccel;
		m_maxBraking = that.m_maxBraking;
		m_maxTurnRate = that.m_maxTurnRate;
		m_flags = that.m_flags;
		m_closeEnoughDist = that.m_closeEnoughDist;
#ifdef CIRCLE_FOR_LANDING
		m_circleThresh = that.m_circleThresh;
#endif
		m_preferredHeight = that.m_preferredHeight;
		m_preferredHeightDamping = that.m_preferredHeightDamping;
		m_sineDescentDistance = that.m_sineDescentDistance;
		m_yawRate = that.m_yawRate;
		m_driveAccelX = that.m_driveAccelX;
		m_driveAccelY = that.m_driveAccelY;
		m_driveSpeed = that.m_driveSpeed;
		m_driveFrame = that.m_driveFrame;
	}
	return *this;
}

//-------------------------------------------------------------------------------------------------
Locomotor::~Locomotor() 
{
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Locomotor::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 3: m_sineDescentDistance
	* 4: m_yawRate, m_driveAccelX, m_driveAccelY
	* 5: m_driveSpeed, m_driveFrame
	* 6: m_speedScale */
// ------------------------------------------------------------------------------------------------
void Locomotor::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 6;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if (version>=2) {
		xfer->xferUnsignedInt(&m_donutTimer);
	}

	xfer->xferCoord3D(&m_maintainPos);
	xfer->xferReal(&m_brakingFactor);
	xfer->xferReal(&m_maxLift);
	xfer->xferReal(&m_maxSpeed);
	xfer->xferReal(&m_maxAccel);
	xfer->xferReal(&m_maxBraking);
	xfer->xferReal(&m_maxTurnRate);
	xfer->xferReal(&m_closeEnoughDist);
#ifdef CIRCLE_FOR_LANDING
	DEBUG_CRASH(("not supported, must fix me"));
#endif
	xfer->xferUnsignedInt(&m_flags);
	xfer->xferReal(&m_preferredHeight);
	xfer->xferReal(&m_preferredHeightDamping);
	xfer->xferReal(&m_angleOffset);
	xfer->xferReal(&m_offsetIncrement);

	if (version >= 3)
		xfer->xferReal(&m_sineDescentDistance);

	if (version >= 4)
	{
		xfer->xferReal(&m_yawRate);
		xfer->xferReal(&m_driveAccelX);
		xfer->xferReal(&m_driveAccelY);
	}

	if (version >= 5)
	{
		xfer->xferReal(&m_driveSpeed);
		xfer->xferUnsignedInt(&m_driveFrame);
	}

	if (version >= 6)
		xfer->xferReal(&m_speedScale);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Locomotor::loadPostProcess( void )
{

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
void Locomotor::startMove(void) 
{
	// Reset the donut timer.
	m_donutTimer = TheGameLogic->getFrame()+DONUT_TIME_DELAY_SECONDS*LOGICFRAMES_PER_SECOND;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getMaxSpeedForCondition(BodyDamageType condition) const
{
	Real speed;

	if( IS_CONDITION_BETTER( condition, TheGlobalData->m_movementPenaltyDamageState ) )
		speed = m_template->m_maxSpeed;
	else
		speed = m_template->m_maxSpeedDamaged;
	speed *= m_speedScale;

	if (speed > m_maxSpeed)
		speed = m_maxSpeed;

	return speed;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getMaxTurnRate(BodyDamageType condition) const
{
	Real turn;

	if( IS_CONDITION_BETTER( condition, TheGlobalData->m_movementPenaltyDamageState ) )
		turn = m_template->m_maxTurnRate;
	else
		turn = m_template->m_maxTurnRateDamaged;

	if (turn > m_maxTurnRate)
		turn = m_maxTurnRate;

	const Real TURN_FACTOR = 2;
	if (getFlag(ULTRA_ACCURATE))
		turn *= TURN_FACTOR;	// monster turning ability

	return turn;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getMaxAcceleration(BodyDamageType condition) const
{
	Real accel;

	if( IS_CONDITION_BETTER( condition, TheGlobalData->m_movementPenaltyDamageState ) )
		accel = m_template->m_acceleration;
	else
		accel = m_template->m_accelerationDamaged;

	if (accel > m_maxAccel)
		accel = m_maxAccel;

	return accel;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getBraking() const
{
	Real braking = m_template->m_braking;

	if (braking > m_maxBraking)
		braking = m_maxBraking;

	return braking;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getMaxLift(BodyDamageType condition) const
{
	Real lift;

	if( IS_CONDITION_BETTER( condition, TheGlobalData->m_movementPenaltyDamageState ) )
		lift = m_template->m_lift;
	else
		lift = m_template->m_liftDamaged;

	if (lift > m_maxLift)
		lift = m_maxLift;

	return lift;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::locoUpdate_moveTowardsAngle(Object* obj, Real goalAngle)
{
	setFlag(MAINTAIN_POS_IS_VALID, false);

	if (obj == NULL || m_template == NULL)
		return;

	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == NULL)
	{
		DEBUG_CRASH(("you can only apply Locomotors to objects with Physics"));
		return;
	}

	// Skip moveTowardsAngle if physics say you're stunned
	if(physics->getIsStunned())
	{
		return;
	}

#ifdef DEBUG_OBJECT_ID_EXISTS
//	DEBUG_ASSERTLOG(obj->getID() != TheObjectIDToDebug, ("locoUpdate_moveTowardsAngle %f (%f deg), spd %f (%f)\n",goalAngle,goalAngle*180/PI,physics->getSpeed(),physics->getForwardSpeed2D()));
#endif

	Real minSpeed = getMinSpeed();
	if (minSpeed > 0)
	{
		// can't stay in one place; move in the desired direction at min speed.
		Coord3D desiredPos = *obj->getPosition();
		desiredPos.x += Cos(goalAngle) * minSpeed * 2;
		desiredPos.y += Sin(goalAngle) * minSpeed * 2;
		// pass a huge num for "dist to goal", so that we don't think we're nearing
		// our destination and thus slow down...
		const Real onPathDistToGoal = 99999.0f;
		Bool blocked = false;
		locoUpdate_moveTowardsPosition(obj, desiredPos, onPathDistToGoal, minSpeed, &blocked);
		
		// don't need to call handleBehaviorZ() here, since locoUpdate_moveTowardsPosition() will do so
		return;
	}
	else
	{
		DEBUG_ASSERTCRASH(m_template->m_appearance != LOCO_THRUST, ("THRUST should always have minspeeds!\n"));
		Coord3D desiredPos = *obj->getPosition();
		desiredPos.x += Cos(goalAngle) * 1000.0f;
		desiredPos.y += Sin(goalAngle) * 1000.0f;
		PhysicsTurningType rotating = rotateTowardsPosition(obj, desiredPos);
		physics->setTurning(rotating);
		// a helicopter turning in place has no sideways friction to stop a slide it came in with
		if (isHelicopter(obj))
		{
			brakeHelicopter(obj, physics);
			m_yawRate = 0.0f;	// this turn is rotateTowardsPosition's, which parking needs to stop dead on the heading
		}
		// a soldier turning to aim may still be walking off the end of his approach
		if (m_template->m_appearance == LOCO_LEGS_TWO)
			brakeLegs(obj, physics);
		handleBehaviorZ(obj, physics, *obj->getPosition());
	}

}

//-------------------------------------------------------------------------------------------------
PhysicsTurningType Locomotor::rotateTowardsPosition(Object* obj, const Coord3D& goalPos, Real *relAngle)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real turnRate = getMaxTurnRate(bdt);

	PhysicsTurningType rotating = rotateObjAroundLocoPivot(obj, goalPos, turnRate, relAngle);
	return rotating;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::setPhysicsOptions(Object* obj)
{
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == NULL)
	{
		DEBUG_CRASH(("you can only apply Locomotors to objects with Physics"));
		return;
	}

	// crank up the friction in ultra-accurate mode to increase movement precision.
	const Real EXTRA_FRIC = 0.5f;
	Real extraExtraFriction = getFlag(ULTRA_ACCURATE) ? EXTRA_FRIC : 0.0f;
	physics->setExtraFriction(m_template->m_extra2DFriction + extraExtraFriction);
	physics->setAllowAirborneFriction(getApply2DFrictionWhenAirborne());	// you'd think we wouldn't want friction in the air, but it's needed for realistic behavior.
	physics->setStickToGround(getStickToGround()); // walking guys aren't allowed to catch huge (or even small) air.
	Bool helicopter = isHelicopter(obj);
	physics->setMotiveSteersSideways(helicopter);
	if (!helicopter && !isGroundVehicle(obj) && m_template->m_appearance != LOCO_LEGS_TWO)
	{
		// landed, taxiing or dying: whatever it was pulling or swinging when it last flew or drove is over
		m_yawRate = 0.0f;
		m_driveAccelX = 0.0f;
		m_driveAccelY = 0.0f;
	}
}

//-------------------------------------------------------------------------------------------------
Bool Locomotor::isHelicopter(const Object* obj) const
{
	return m_template->m_appearance == LOCO_HOVER
		&& BitTest(m_template->m_surfaces, LOCOMOTORSURFACE_AIR)
		&& obj->isKindOf(KINDOF_AIRCRAFT)
		&& !obj->isEffectivelyDead();	// a dying one spirals on HelicopterSlowDeathBehavior's forces and the old friction
}

//-------------------------------------------------------------------------------------------------
Bool Locomotor::isGroundVehicle(const Object* obj) const
{
	LocomotorAppearance appearance = m_template->m_appearance;
	return (appearance == LOCO_TREADS || appearance == LOCO_WHEELS_FOUR || appearance == LOCO_MOTORCYCLE)
		&& !obj->isKindOf(KINDOF_AIRCRAFT)	// a jet or a landed helicopter taxiing keeps EA's drive
		&& !getFlag(ULTRA_ACCURATE);				// and so does a dozer or a POW truck lining up on its spot
}

//-------------------------------------------------------------------------------------------------
void Locomotor::locoUpdate_moveTowardsPosition(Object* obj, const Coord3D& goalPos,
																							 Real onPathDistToGoal, Real desiredSpeed, Bool *blocked,
																							 const Coord3D *faceTarget, Bool fighting, Real bendDist, Real bendCos)
{
	setFlag(MAINTAIN_POS_IS_VALID, false);

	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	
	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	Real distToStopAtMaxSpeed = (maxSpeed/getBraking()) * (maxSpeed)/2.0f;
	if (onPathDistToGoal>PATHFIND_CELL_SIZE_F && onPathDistToGoal > distToStopAtMaxSpeed) 
	{
		setFlag(IS_BRAKING, false);
		m_brakingFactor = 1.0f;
	}
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == NULL)
	{
		DEBUG_CRASH(("you can only apply Locomotors to objects with Physics"));
		return;
	}

	// Skip moveTowardsPosition if physics say you're stunned
	if(physics->getIsStunned())
	{
		return;
	}

#ifdef DEBUG_OBJECT_ID_EXISTS
//	DEBUG_ASSERTLOG(obj->getID() != TheObjectIDToDebug, ("locoUpdate_moveTowardsPosition %f %f %f (dtg %f, spd %f), speed %f (%f)\n",goalPos.x,goalPos.y,goalPos.z,onPathDistToGoal,desiredSpeed,physics->getSpeed(),physics->getForwardSpeed2D()));
#endif

	//
	// do not allow for invalid positions that the pathfinder cannot handle ... for airborne
	// objects we don't need the pathfinder so we'll ignore this
	//
	if( BitTest( m_template->m_surfaces, LOCOMOTORSURFACE_AIR ) == false &&
			!TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, obj->getPosition()) && 
			!getFlag(ALLOW_INVALID_POSITION)) 
	{
		// Somehow, we have gotten to an invalid location.
		if (fixInvalidPosition(obj, physics)) 
		{
			// the we adjusted us toward a legal position, so just return.
			return;
		}
	}

	// If the actual distance is farther, then use the actual distance so we get there.
	Real dx = goalPos.x - obj->getPosition()->x;
	Real dy = goalPos.y - obj->getPosition()->y;
	Real dz = goalPos.z - obj->getPosition()->z;
	Real dist = sqrt(dx*dx+dy*dy);
	if (dist>onPathDistToGoal) 
	{
		if (!obj->isKindOf(KINDOF_PROJECTILE) && dist>2*onPathDistToGoal) 
		{
			setFlag(IS_BRAKING, true);
		}
		onPathDistToGoal = dist;
	}

	Coord3D nullAccel;							
	
	Bool treatAsAirborne = false;
	Coord3D pos = *obj->getPosition();
	Real heightAboveSurface = pos.z - TheTerrainLogic->getLayerHeight(pos.x, pos.y, obj->getLayer());
	
	if( obj->getStatusBits().test( OBJECT_STATUS_DECK_HEIGHT_OFFSET ) )
	{
		heightAboveSurface -= obj->getCarrierDeckHeight();
	}

	if (heightAboveSurface > -(3*3)*TheGlobalData->m_gravity) 
	{
		// If we get high enough to stay up for 3 frames, then we left the ground.
		treatAsAirborne = true;
	}
	// We apply a zero acceleration to all units, as the call to 
	// applyMotiveForce flags an object as being "driven" by a locomotor, rather
	// than being pushed around by objects bumping it.
	nullAccel.x = nullAccel.y = nullAccel.z = 0;
	physics->applyMotiveForce(&nullAccel);

	if (*blocked) 
	{
		if (desiredSpeed > physics->getVelocityMagnitude()) 
		{
			*blocked = false;
		}
		if (treatAsAirborne && BitTest( m_template->m_surfaces, LOCOMOTORSURFACE_AIR ) ) 
		{
			// Airborne flying objects don't collide for now.  jba.
			*blocked = false;
		}
	}

	if (*blocked) 
	{
		physics->scrubVelocity2D(desiredSpeed); // stop if we are about to run into the blocking object.
		Real turnRate = getMaxTurnRate(obj->getBodyModule()->getDamageState());
		if (m_template->m_wanderWidthFactor == 0.0f) 
		{
			*blocked = (TURN_NONE != rotateObjAroundLocoPivot(obj, goalPos, turnRate));
		}
		
		// it is very important to be sure to call this in all situations, even if not moving in 2d space.
		handleBehaviorZ(obj, physics, goalPos);
		return;
	}

	if ( 
// srj sez: I don't know why we didn't want HOVERs to allow to "brake".
// we actually really want them to, because it allows much more precise destination positioning.
//			m_template->m_appearance == LOCO_HOVER ||
				m_template->m_appearance == LOCO_WINGS) 
	{
		setFlag(IS_BRAKING, false);
	}

	Bool wasBraking = obj->getStatusBits().test( OBJECT_STATUS_BRAKING );
	Bool groundVehicle = isGroundVehicle(obj);

	physics->setTurning(TURN_NONE);
	if (getAllowMotiveForceWhileAirborne() || !treatAsAirborne)
	{
		switch (m_template->m_appearance)
		{
			case LOCO_LEGS_TWO:
					moveTowardsPositionLegs(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
			case LOCO_CLIMBER:
					moveTowardsPositionClimb(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
			case LOCO_WHEELS_FOUR:
			case LOCO_MOTORCYCLE:
					if (groundVehicle)
						moveTowardsPositionVehicle(obj, physics, goalPos, onPathDistToGoal, desiredSpeed, bendDist, bendCos);
					else
						moveTowardsPositionWheels( obj, physics, goalPos, onPathDistToGoal, desiredSpeed );
					break;
			case LOCO_TREADS:
					if (groundVehicle)
						moveTowardsPositionVehicle(obj, physics, goalPos, onPathDistToGoal, desiredSpeed, bendDist, bendCos);
					else
						moveTowardsPositionTreads(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
			case LOCO_HOVER:
					moveTowardsPositionHover(obj, physics, goalPos, onPathDistToGoal, desiredSpeed, faceTarget, fighting);
					break;
			case LOCO_WINGS:
					moveTowardsPositionWings(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
			case LOCO_THRUST:
					moveTowardsPositionThrust(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
			case LOCO_OTHER:
			default:
					moveTowardsPositionOther(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
					break;
		}
	}

	handleBehaviorZ(obj, physics, goalPos);
	// Objects that are braking don't follow the normal physics, so they end up at their destination exactly.
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ), getFlag(IS_BRAKING) );
	/* A vehicle slides onto its goal on the frames physics leaves its position alone, which is this
		 frame's flag. Last frame's put the slide one frame late: the frame braking began moved nothing,
		 and the first frame of the next order both slid and drove, up to twice top speed. */
	if (groundVehicle)
		wasBraking = getFlag(IS_BRAKING);

	if (wasBraking)
	{
	#define MIN_VEL (PATHFIND_CELL_SIZE_F/(LOGICFRAMES_PER_SECOND))

		Coord3D pos = *obj->getPosition();
		if (obj->isKindOf(KINDOF_PROJECTILE)) 
		{
			// Projectiles never stop braking once they start.  jba.
			obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
			// Projectiles cheat in 3 dimensions. Chasing an aircraft, or a goal behind the nose, that is straight at it; otherwise along
			// the nose, so a missile flies the arc moveTowardsPositionThrust turns it onto instead of
			// kinking at the lock.
			dist = sqrt(dx*dx+dy*dy+dz*dz);
			Real vel = physics->getVelocityMagnitude();
			if (vel < MIN_VEL)
				vel = MIN_VEL;
			if (vel > dist)
				vel = dist;	// do not overcompensate!
			if (dist > 0.001f && Locomotor_projectileChasesStraight(obj, Vector3(dx, dy, dz)))
			{
				pos.x += dx / dist * vel;
				pos.y += dy / dist * vel;
				pos.z += dz / dist * vel;
			}
			else if (dist > 0.001f)
			{
				Vector3 nose = obj->getTransformMatrix()->Get_X_Vector();
				nose.Normalize();
				pos.x += nose.X * vel;
				pos.y += nose.Y * vel;
				pos.z += nose.Z * vel;
			}
		}	
		else 
		{
			// not projectiles only cheat in x & y.
			// Normalize.
			if (dist > 0.001f)
			{
				// a helicopter's speed need not lie along its nose
				const Coord3D *v = physics->getVelocity();
				Real vel = isHelicopter(obj) ? sqrt(sqr(v->x) + sqr(v->y)) : fabs(physics->getForwardSpeed2D());
				// a vehicle's planned stop has already slowed it; a third of a cell a frame would put the speed back
				Real minVel = groundVehicle ? VEHICLE_MIN_SLIDE : MIN_VEL;
				if (vel < minVel)
					vel = minVel;
				if (vel > dist)
					vel = dist;	// do not overcompensate!
				dist = 1.0f / dist;
				dx *= dist;
				dy *= dist;
				pos.x += dx * vel;
				pos.y += dy * vel;
			}
		}
		obj->setPosition(&pos);
	}

}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionTreads(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{

	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	Real maxAcceleration = getMaxAcceleration(bdt);

	// Locomotion for treaded vehicles, ie tanks.

	//
	// Orient toward goal position
	//
//	Real angle = obj->getOrientation();
//	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, &goalPos );
//	Real desiredAngle = angle + relAngle;
	Real relAngle ;
	PhysicsTurningType rotating = rotateTowardsPosition(obj, goalPos, &relAngle);
	physics->setTurning(rotating);

	//
	// Modulate speed according to turning. The more we have to turn, the slower we go
	//
	const Real QUAETERPI = PI / 4.0f;
	Real angleCoeff = (Real)fabs( relAngle ) / QUAETERPI;
	if (angleCoeff > 1.0f)
		angleCoeff = 1.0;


	Real dx = obj->getPosition()->x - goalPos.x;
	Real dy = obj->getPosition()->y - goalPos.y;
	

	Real goalSpeed = (1.0f - angleCoeff) * desiredSpeed;


//	if (speed < m_minTurnSpeed)
//		speed = m_minTurnSpeed;

	Real actualSpeed = physics->getForwardSpeed2D();
	Real slowDownTime = actualSpeed / getBraking();
	Real slowDownDist = (actualSpeed/1.50f) * slowDownTime;	

	if (sqr(dx)+sqr(dy)<sqr(2*PATHFIND_CELL_SIZE_F) && angleCoeff > 0.05) {
		goalSpeed = actualSpeed*0.6f;
	}

 	if (onPathDistToGoal < slowDownDist && !getFlag(IS_BRAKING) && !getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		setFlag(IS_BRAKING, true);
		m_brakingFactor = 1.1f;
	}

	if (onPathDistToGoal>PATHFIND_CELL_SIZE_F && onPathDistToGoal > 2.0*slowDownDist) 
	{
		setFlag(IS_BRAKING, false);
	}

	if (getFlag(IS_BRAKING)) 
	{
		// Standing still on top of the goal is 0/0, which came out NaN and stayed in the member:
		// the clamp below lets a NaN through, and the next frame that wants to slow down multiplies
		// the braking force by it and hands the physics a NaN force. Sitting on the goal is as hard
		// as braking gets, so the ratio's limit is what belongs there.
		if (onPathDistToGoal > 0.0f) {
			m_brakingFactor = slowDownDist/onPathDistToGoal;
			m_brakingFactor *= m_brakingFactor;
		} else {
			m_brakingFactor = MAX_BRAKING_FACTOR;
		}
		if (m_brakingFactor>MAX_BRAKING_FACTOR) {
			m_brakingFactor = MAX_BRAKING_FACTOR;
		}
		if (slowDownDist>onPathDistToGoal) {
			goalSpeed = actualSpeed-getBraking();
			if (goalSpeed<0.0f) goalSpeed= 0.0f;
		} else if (slowDownDist>onPathDistToGoal*0.75f) {
			goalSpeed = actualSpeed-getBraking()/2.0f;
			if (goalSpeed<0.0f) goalSpeed = 0.0f;
		} else {
			goalSpeed = actualSpeed;
		}
	}


	//DEBUG_LOG(("Actual speed %f, Braking factor %f, slowDownDist %f, Pathdist %f, goalSpeed %f\n", 
	//	actualSpeed, m_brakingFactor, slowDownDist, onPathDistToGoal, goalSpeed));

	//
	// Maintain goal speed
	//
	Real speedDelta = goalSpeed - actualSpeed;
	if (speedDelta != 0.0f)
	{
		Real mass = physics->getMass();
		Real acceleration = (speedDelta > 0.0f) ? maxAcceleration : -m_brakingFactor*getBraking();
		Real accelForce = mass * acceleration;

		/*
			don't accelerate/brake more than necessary. do a quick calc to 
			see how much force we really need to achieve our goal speed...
		*/
		Real maxForceNeeded = mass * speedDelta;
		if (fabs(accelForce) > fabs(maxForceNeeded))
			accelForce = maxForceNeeded;

		const Coord3D *dir = obj->getUnitDirectionVector2D();

		Coord3D force;
		force.x = accelForce * dir->x;
		force.y = accelForce * dir->y;
		force.z = 0.0f;

		// apply forces to object
		physics->applyMotiveForce( &force );
	}
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionWheels(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	Real maxTurnRate = getMaxTurnRate(bdt);
	Real maxAcceleration = getMaxAcceleration(bdt);

	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	// Locomotion for wheeled vehicles, ie trucks.
	//
	// See if we are turning.  If so, use the min turn speed.
	//
	Real turnSpeed = m_template->m_minTurnSpeed;
	Real angle = obj->getOrientation();
//	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, &goalPos );
//	Real desiredAngle = angle + relAngle;
	Real desiredAngle = ATan2(goalPos.y - obj->getPosition()->y, goalPos.x - obj->getPosition()->x);
	Real relAngle = stdAngleDiff(desiredAngle, angle);

	Bool moveBackwards = false;

	// Wheeled vehicles can only turn while moving, so make sure the turn speed is reasonable.
	if (turnSpeed < maxSpeed/4.0f) 
	{
		turnSpeed = maxSpeed/4.0f;
	}


	Real actualSpeed = physics->getForwardSpeed2D();
	Bool do3pointTurn = false;
#if 1
	if (actualSpeed==0.0f) {
		setFlag(MOVING_BACKWARDS, false);
		if (m_template->m_canMoveBackward && fabs(relAngle) > PI/2) {
			setFlag(MOVING_BACKWARDS, true );
			setFlag(DOING_THREE_POINT_TURN, onPathDistToGoal>5*obj->getGeometryInfo().getMajorRadius());
		}																						

	}
	if (getFlag(MOVING_BACKWARDS)) {
		if (fabs(relAngle) < PI/2) {
			moveBackwards = false;
			setFlag(MOVING_BACKWARDS, false);
		} else {
			moveBackwards = true;
			setFlag(DOING_THREE_POINT_TURN, onPathDistToGoal>5*obj->getGeometryInfo().getMajorRadius());
			do3pointTurn = getFlag(DOING_THREE_POINT_TURN);
			if (!do3pointTurn) {
				desiredAngle = stdAngleDiff(desiredAngle, PI);
				relAngle = stdAngleDiff(desiredAngle, angle);
			}
		} 
	}
#endif

	const Real SMALL_TURN = PI / 20.0f;
	if ((Real)fabs( relAngle ) > SMALL_TURN) 
	{
		if (desiredSpeed>turnSpeed) 
		{
			desiredSpeed = turnSpeed;
		}
	}

	Real goalSpeed = desiredSpeed;
	if (moveBackwards) {
		actualSpeed = -actualSpeed;
	}



	Real slowDownTime = actualSpeed / getBraking() + 1.0f;
	Real slowDownDist = (actualSpeed/1.5f) * slowDownTime + actualSpeed;	
	Real effectiveSlowDownDist = slowDownDist;
	if (effectiveSlowDownDist < 1*PATHFIND_CELL_SIZE) {
		effectiveSlowDownDist = 1*PATHFIND_CELL_SIZE;
	}


	const Real FIFTEEN_DEGREES = PI / 12.0f;
	const Real PROJECT_FRAMES = LOGICFRAMES_PER_SECOND/2; // Project out 1/2 second.
	if (fabs( relAngle ) > FIFTEEN_DEGREES) 
	{
		// If we're turning more than 10 degrees, check & see if we're moving into "impassable territory"
		Real distance = PROJECT_FRAMES * (goalSpeed+actualSpeed)/2.0f;
		Real targetAngle = obj->getOrientation();
		Real turnFactor = ((goalSpeed+actualSpeed)/2.0f)/turnSpeed;
		if (turnFactor > 1.0f) 
			turnFactor = 1.0f;
		Real turnAmount = PROJECT_FRAMES*turnFactor*maxTurnRate/4.0f;
		if (relAngle < 0) 
		{
			targetAngle -= turnAmount;
		}	
		else 
		{
			targetAngle += turnAmount;
		}
		Coord3D offset;
		offset.x = Cos(targetAngle)*distance;
		offset.y = Sin(targetAngle)*distance;
		offset.z = 0;

		const Coord3D* pos = obj->getPosition();

		Coord3D nextPos;
		nextPos.x = pos->x+offset.x;
		nextPos.y = pos->y+offset.y;
		nextPos.z = pos->z;

		pos = obj->getPosition();

		Coord3D halfPos;
		halfPos.x = pos->x+offset.x/2;
		halfPos.y = pos->y+offset.y/2;
		halfPos.z = pos->z;

		if (!TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, &halfPos) ||
			!TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, &nextPos)) 
		{
			PhysicsTurningType rotating = rotateTowardsPosition(obj, goalPos);
			physics->setTurning(rotating);

			// apply a zero force to object so that it acts "driven"
			Coord3D force;
			force.zero();
			physics->applyMotiveForce( &force );
			return;
		}

	}

	if (onPathDistToGoal < effectiveSlowDownDist && !getFlag(IS_BRAKING) && !getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		setFlag(IS_BRAKING, true);
		m_brakingFactor = 1.1f;
	}


	if (onPathDistToGoal>PATHFIND_CELL_SIZE_F && onPathDistToGoal > 2.0*slowDownDist) 
	{
		setFlag(IS_BRAKING, false);
	}

 	if (onPathDistToGoal > DONUT_DISTANCE) {
		m_donutTimer = TheGameLogic->getFrame()+DONUT_TIME_DELAY_SECONDS*LOGICFRAMES_PER_SECOND;
	} else {
		if (m_donutTimer < TheGameLogic->getFrame()) {
			setFlag(IS_BRAKING, true);
		}
	}

	if (getFlag(IS_BRAKING)) 
	{
		// wheels deliberately brake at a flat factor, unlike treads: the ramped
		// slowDownDist/onPathDistToGoal calculation that used to sit here was overwritten by this
		// line on the very next statement, so it was dead. Behaviour unchanged; only the dead
		// computation is gone.
		m_brakingFactor = 1.0f;
		if (slowDownDist>onPathDistToGoal) {
			goalSpeed = actualSpeed-getBraking();
			if (goalSpeed<0.0f) goalSpeed= 0.0f;
		} else if (slowDownDist>onPathDistToGoal*0.75f) {
			goalSpeed = actualSpeed-getBraking()/2.0f;
			if (goalSpeed<0.0f) goalSpeed = 0.0f;
		} else {
			goalSpeed = actualSpeed;
		}
	}


	//DEBUG_LOG(("Actual speed %f, Braking factor %f, slowDownDist %f, Pathdist %f, goalSpeed %f\n", 
	//	actualSpeed, m_brakingFactor, slowDownDist, onPathDistToGoal, goalSpeed));


	// Wheeled can only turn while moving.
	Real turnFactor = actualSpeed/turnSpeed;
	if (turnFactor<0) {
		turnFactor = -turnFactor; // in case we're sliding backwards in a 3 pt turn.
	}
	if (turnFactor > 1.0f) 
		turnFactor = 1.0f;
	Real turnAmount = turnFactor*maxTurnRate;	

	PhysicsTurningType rotating;
	if (moveBackwards && !do3pointTurn) {
		Coord3D backwardPos = *obj->getPosition();
		backwardPos.x += -(goalPos.x - obj->getPosition()->x);
		backwardPos.y += -(goalPos.y - obj->getPosition()->y);
		rotating = rotateObjAroundLocoPivot(obj, backwardPos, turnAmount);	
	} else {
		rotating = rotateObjAroundLocoPivot(obj, goalPos, turnAmount);	
	}

	physics->setTurning(rotating);

	//
	// Maintain goal speed
	//
	Real speedDelta = goalSpeed - actualSpeed;
	if (moveBackwards) {
		speedDelta = -goalSpeed+actualSpeed;
	}
	if (speedDelta != 0.0f)
	{
		Real mass = physics->getMass();
		Real acceleration;
		if (moveBackwards) {
			acceleration = (speedDelta < 0.0f) ? -maxAcceleration : m_brakingFactor*getBraking();
		}	else {
			acceleration = (speedDelta > 0.0f) ? maxAcceleration : -m_brakingFactor*getBraking();
		}
		Real accelForce = mass * acceleration;

		/*
			don't accelerate/brake more than necessary. do a quick calc to 
			see how much force we really need to achieve our goal speed...
		*/
		Real maxForceNeeded = mass * speedDelta;
		if (fabs(accelForce) > fabs(maxForceNeeded))
			accelForce = maxForceNeeded;

		//DEBUG_LOG(("Braking %d, actualSpeed %f, goalSpeed %f, delta %f, accel %f\n", getFlag(IS_BRAKING),
			//actualSpeed, goalSpeed, speedDelta, accelForce));

		const Coord3D *dir = obj->getUnitDirectionVector2D();

		Coord3D force;
		force.x = accelForce * dir->x;
		force.y = accelForce * dir->y;
		force.z = 0.0f;

		// apply forces to object
		physics->applyMotiveForce( &force );
	}

}
//-------------------------------------------------------------------------------------------------
/// the hardest a vehicle brakes: its Braking, or whatever stops it from top speed in VEHICLE_STOP_SECONDS
Real Locomotor::getVehicleStopDecel(BodyDamageType condition) const
{
	Real decel = getMaxSpeedForCondition(condition) / (VEHICLE_STOP_SECONDS * LOGICFRAMES_PER_SECOND);
	if (decel < getBraking())
		decel = getBraking();
	return decel;
}

//-------------------------------------------------------------------------------------------------
/**
	Ease a vehicle's speed along its nose towards wantSpeed, negative for backing up. It asks for the
	error back over VEHICLE_VELOCITY_FRAMES, no harder than accel, or decel while that sheds speed;
	the pull it applies moves towards that by a share of the limit a frame, so a start, a stop or a
	change of mind builds up instead of jumping; and it never rolls through a stop into the other
	direction unless that is the way it wants to go.
*/
void Locomotor::driveVehicle(Object* obj, PhysicsBehavior *physics, Real wantSpeed, Real accel, Real decel)
{
	Real speed = physics->getForwardSpeed2D();
	/* A collision shoves a unit between two frames, and EA's locomotor took the shove straight back out
		 on the next one by setting the speed it wanted outright. Eased back instead, it threw a Crusader
		 rubbing past another one back at half its top speed, over and over, while the column behind it
		 stood. Whatever the speed along the nose gained since the last frame's drive that the drive did
		 not put there is taken back out at once. */
	UnsignedInt now = TheGameLogic->getFrame();
	Real shove = 0.0f;
	if (m_driveFrame + 1 == now)
	{
		shove = speed - m_driveSpeed;
		speed = m_driveSpeed;
	}
	Real ask = (wantSpeed - speed) / VEHICLE_VELOCITY_FRAMES;
	Bool slowing = (speed > 0.0f && ask < 0.0f) || (speed < 0.0f && ask > 0.0f);
	Real limit = slowing ? decel : accel;
	if (ask > limit)
		ask = limit;
	else if (ask < -limit)
		ask = -limit;

	Real jerk = slowing ? decel / VEHICLE_BRAKE_EASE_FRAMES : accel / VEHICLE_ACCEL_EASE_FRAMES;
	Real change = ask - m_driveAccelX;
	if (change > jerk)
		change = jerk;
	else if (change < -jerk)
		change = -jerk;
	m_driveAccelX += change;

	Real next = speed + m_driveAccelX;
	if ((speed >= 0.0f && wantSpeed >= 0.0f && next < 0.0f) || (speed <= 0.0f && wantSpeed <= 0.0f && next > 0.0f))
		m_driveAccelX = -speed;
	m_driveSpeed = speed + m_driveAccelX;
	m_driveFrame = now;

	if (m_driveAccelX == shove)
		return;
	const Coord3D *dir = obj->getUnitDirectionVector2D();
	Real pull = physics->getMass() * (m_driveAccelX - shove);
	Coord3D force;
	force.x = pull * dir->x;
	force.y = pull * dir->y;
	force.z = 0.0f;
	physics->applyMotiveForce(&force);
}

//-------------------------------------------------------------------------------------------------
/// turn a vehicle by yaw about its pivot, and carry its speed round with the nose
void Locomotor::turnVehicle(Object* obj, PhysicsBehavior *physics, Real yaw)
{
	if (yaw == 0.0f)
	{
		physics->setTurning(TURN_NONE);
		return;
	}
	/* Treads and tyres grip: the speed along the nose turns with it. Left alone, physics kept it on
		 the old line and only the sideways friction wore it off, so a vehicle turning at speed skidded
		 wide of where it was steering. A sideways push from a collision is not the vehicle's own speed
		 and is left to the friction. */
	Real forward = physics->getForwardSpeed2D();
	Coord3D before = *obj->getUnitDirectionVector2D();
	turnObjAroundLocoPivot(obj, yaw);
	const Coord3D *after = obj->getUnitDirectionVector2D();
	Real carry = physics->getMass() * forward;
	Coord3D force;
	force.x = carry * (after->x - before.x);
	force.y = carry * (after->y - before.y);
	force.z = 0.0f;
	physics->applyMotiveForce(&force);
	physics->setTurning(yaw > 0.0f ? TURN_POSITIVE : TURN_NEGATIVE);
}

//-------------------------------------------------------------------------------------------------
/*
	A tank, car or bike under its own power. EA's treads and wheels set the speed they wanted on the
	next frame (Acceleration and Braking were 1000, a thirtieth of a second from rest to top speed),
	cut it to 60% on every frame near a waypoint that was not dead ahead, pivoted a tank on the spot
	at full TurnRate for anything past 45 degrees, and turned a car round in less than its own length.

	Here the speed eases towards a target (driveVehicle) that is the lowest of:
	- the speed asked for, and a share of it for backing up;
	- a share for the turn the vehicle is in, full straight ahead and a floor at a right angle; a
		tank further round than TREAD_PIVOT_COS stops and pivots;
	- the speed at which the turn rate still bends the path onto the steering point;
	- the speed it can still brake down from to the speed of the next bend in the route, and to a
		stop at the end of it, both planned at a gentler rate than the vehicle can brake and counted
		short by the time the braking takes to build up.
	Speed it is asked to shed for traffic is braked harder than a planned stop (VEHICLE_TRAFFIC_BRAKE).
	The nose turns at an eased rate: a tank at full TurnRate on the move and TREAD_PIVOT_SHARE of it
	standing, a car at no more than its speed over WHEEL_MIN_RADIUS_BODY_LENGTHS, and when that arc
	cannot reach the steering point (or would run onto ground it cannot drive, or the goal is close
	behind), in a crawl at EA's turn speed round WHEEL_TIGHT_RADIUS_BODY_LENGTHS, so a car still gets
	everywhere it used to. A goal behind and close is backed up to by anything with treads or
	CanMoveBackwards; a car that can back up and has a goal behind and far does a three-point turn.
	The last cell is still the slide onto the goal it always was.
*/
void Locomotor::moveTowardsPositionVehicle(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos,
	Real onPathDistToGoal, Real desiredSpeed, Real bendDist, Real bendCos)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	if (desiredSpeed > maxSpeed)
		desiredSpeed = maxSpeed;
	Real turnRate = getMaxTurnRate(bdt);
	Real accel = getMaxAcceleration(bdt);
	Real decel = getVehicleStopDecel(bdt);
	Real planDecel = decel * VEHICLE_PLAN_DECEL_SHARE;
	Bool treads = (m_template->m_appearance == LOCO_TREADS);
	Real bodyLength = 2.0f * obj->getGeometryInfo().getMajorRadius();
	Real speed = physics->getForwardSpeed2D();
	Real absSpeed = fabs(speed);

	// EA's turn speed: a car's tight turn is driven at this
	Real turnSpeed = m_template->m_minTurnSpeed;
	if (turnSpeed < maxSpeed / 4.0f)
		turnSpeed = maxSpeed / 4.0f;
	if (turnSpeed > maxSpeed)
		turnSpeed = maxSpeed;

	// where the steering point is against the nose: the cosine ahead, the sine to the left
	const Coord3D *pos = obj->getPosition();
	const Coord3D *dir = obj->getUnitDirectionVector2D();
	Real dx = goalPos.x - pos->x;
	Real dy = goalPos.y - pos->y;
	Real dist = sqrt(dx*dx + dy*dy);
	Real goalCos = 1.0f;
	Real goalSin = 0.0f;
	if (dist > 0.1f)	// right on it the bearing is rounding noise
	{
		goalCos = (dx * dir->x + dy * dir->y) / dist;
		goalSin = (dir->x * dy - dir->y * dx) / dist;
	}

	// forward or back is chosen standing, and given up once the goal is no longer behind
	if (getFlag(MOVING_BACKWARDS) && goalCos > 0.0f)
	{
		setFlag(MOVING_BACKWARDS, false);
		setFlag(DOING_THREE_POINT_TURN, false);
	}
	if (absSpeed < maxSpeed * VEHICLE_STILL_SHARE)
	{
		Bool behind = (treads || m_template->m_canMoveBackward) && goalCos < VEHICLE_REVERSE_COS;
		Bool close = onPathDistToGoal < VEHICLE_REVERSE_BODY_LENGTHS * bodyLength;
		setFlag(MOVING_BACKWARDS, behind && (close || !treads));
		setFlag(DOING_THREE_POINT_TURN, behind && !close && !treads);
	}
	Bool backing = getFlag(MOVING_BACKWARDS);
	Bool threePoint = backing && getFlag(DOING_THREE_POINT_TURN);
	Bool tailLeads = backing && !threePoint;

	// the same against whichever end leads, and the arc from here along that way through the goal
	Real leadCos = tailLeads ? -goalCos : goalCos;
	Real leadSin = tailLeads ? -goalSin : goalSin;
	Real arcRadius = BIGNUM;
	if (fabs(leadSin) > 0.01f)
		arcRadius = dist / (2.0f * fabs(leadSin));
	Real minRadius = WHEEL_MIN_RADIUS_BODY_LENGTHS * bodyLength;

	Bool tight = FALSE;
	if (!treads && !backing)
	{
		/* Turning as hard as it can, a car drives round a circle of minRadius beside it. A goal inside
			 that circle cannot be reached that way at all, and one close behind only by a loop several
			 times longer than the trip: both are left to EA's tight turn. */
		Real side = (goalSin < 0.0f) ? -minRadius : minRadius;
		Real cx = pos->x - dir->y * side;
		Real cy = pos->y + dir->x * side;
		tight = sqr(goalPos.x - cx) + sqr(goalPos.y - cy) < sqr(minRadius)
			|| (goalCos < 0.0f && onPathDistToGoal < 2.0f * minRadius);
		const Real FIFTEEN_DEGREES_COS = 0.966f;
		const Real PROJECT_FRAMES = LOGICFRAMES_PER_SECOND / 2;
		if (!tight && leadCos < FIFTEEN_DEGREES_COS && absSpeed > 0.0f)
		{
			// EA's check, on the wider arc: half a second of it must not run onto ground it cannot drive
			Real reach = PROJECT_FRAMES * absSpeed;
			Real chordAngle = obj->getOrientation() + ((leadSin < 0.0f) ? -0.5f : 0.5f) * reach / minRadius;
			Coord3D nextPos = *pos;
			nextPos.x += Cos(chordAngle) * reach;
			nextPos.y += Sin(chordAngle) * reach;
			Coord3D halfPos = *pos;
			halfPos.x += Cos(chordAngle) * reach * 0.5f;
			halfPos.y += Sin(chordAngle) * reach * 0.5f;
			tight = !TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, &halfPos)
				|| !TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, &nextPos);
		}
	}

	// turn
	Real maxYaw;
	if (treads)
	{
		Real rolling = absSpeed / (0.5f * maxSpeed);
		if (rolling > 1.0f)
			rolling = 1.0f;
		maxYaw = turnRate * (TREAD_PIVOT_SHARE + (1.0f - TREAD_PIVOT_SHARE) * rolling);
	}
	else if (tight)
	{
		/* EA turned a car at full TurnRate once it rolled at its turn speed, about its rear axle, which
			 swung the middle of a Quad Cannon sideways at twice the speed it drove. This is a crawl round
			 a few metres, about as tight as a pivot, and no faster at the middle than at the axle. */
		maxYaw = absSpeed / (WHEEL_TIGHT_RADIUS_BODY_LENGTHS * bodyLength);
		if (maxYaw > turnRate)
			maxYaw = turnRate;
	}
	else
	{
		maxYaw = absSpeed / minRadius;
		if (maxYaw > turnRate)
			maxYaw = turnRate;
	}
	Real error = 0.0f;
	if (dist > 0.1f)
		error = threePoint ? ATan2(goalSin, goalCos) : ATan2(leadSin, leadCos);
	turnVehicle(obj, physics, easeYaw(error, maxYaw, turnRate / VEHICLE_YAW_EASE_FRAMES));

	// speed
	/* A tank slows to a crawl for a right angle. A car takes its tightest arc as fast as its tyres
		 hold it sideways, which this calls a share of its braking grip; slower, a Quad Cannon took
		 ten seconds over a U-turn. */
	Real floor = TREAD_TURN_SPEED_FLOOR;
	if (!treads)
	{
		Real cornerSpeed = sqrt(VEHICLE_CORNER_GRIP * decel * minRadius);
		floor = (cornerSpeed < maxSpeed) ? cornerSpeed / maxSpeed : 1.0f;
	}
	Real cap = desiredSpeed;
	if (backing && cap > maxSpeed * VEHICLE_REVERSE_SHARE)
		cap = maxSpeed * VEHICLE_REVERSE_SHARE;
	if (tight)
	{
		if (cap > turnSpeed)
			cap = turnSpeed;
	}
	else if (!threePoint)
	{
		cap *= floor + (1.0f - floor) * ((leadCos > 0.0f) ? leadCos : 0.0f);
		if (treads && !backing && goalCos < TREAD_PIVOT_COS)
			cap = 0.0f;
		if (cap > turnRate * arcRadius)
			cap = turnRate * arcRadius;
	}
	if (!backing && bendDist > 0.0f && bendCos < VEHICLE_BEND_COS)
	{
		Real bendSpeed = maxSpeed * (floor + (1.0f - floor) * ((bendCos > 0.0f) ? bendCos : 0.0f));
		if (treads && bendCos < TREAD_PIVOT_COS)
			bendSpeed = 0.0f;
		Real left = bendDist - absSpeed * VEHICLE_STOP_LEAD_FRAMES;
		if (left < 0.0f)
			left = 0.0f;
		Real bendCap = sqrt(bendSpeed * bendSpeed + 2.0f * planDecel * left);
		if (cap > bendCap)
			cap = bendCap;
	}
	if (!getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		// the fastest it can still stop from, with the braking fading out over the last few frames
		Real left = onPathDistToGoal - absSpeed * VEHICLE_STOP_LEAD_FRAMES;
		if (left < 0.0f)
			left = 0.0f;
		Real ease = planDecel * VEHICLE_STOP_EASE_FRAMES;
		Real stopSpeed = sqrt(2.0f * planDecel * left + ease * ease) - ease;
		if (cap > stopSpeed)
			cap = stopSpeed;
	}
	/* A stop or a bend is seen coming and braked for gently. Traffic is not: the crowd's brake behind a
		 slower unit, a bump limit or a slower group all arrive as a desired speed below the one it is
		 doing, and are obeyed as hard as a driver would stamp on the pedal. */
	Real brake = decel;
	if (desiredSpeed < absSpeed)
		brake *= VEHICLE_TRAFFIC_BRAKE;
	driveVehicle(obj, physics, backing ? -cap : cap, accel, brake);

	// the last cell is a slide onto the goal, and so is a car still circling it after a while
	Bool slide = onPathDistToGoal < PATHFIND_CELL_SIZE_F;
	if (!treads)
	{
		if (onPathDistToGoal > DONUT_DISTANCE)
			m_donutTimer = TheGameLogic->getFrame() + DONUT_TIME_DELAY_SECONDS * LOGICFRAMES_PER_SECOND;
		else if (m_donutTimer < TheGameLogic->getFrame())
			slide = TRUE;
	}
	setFlag(IS_BRAKING, slide);
}

//-------------------------------------------------------------------------------------------------
/// a vehicle with nowhere to go brakes to a stop and lets its turn wind down; TRUE while it still moves
Bool Locomotor::maintainCurrentPositionVehicle(Object* obj, PhysicsBehavior *physics)
{
	setFlag(MOVING_BACKWARDS, false);
	setFlag(DOING_THREE_POINT_TURN, false);
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real turnRate = getMaxTurnRate(bdt);
	turnVehicle(obj, physics, easeYaw(0.0f, 0.0f, turnRate / VEHICLE_YAW_EASE_FRAMES));
	if (physics->getForwardSpeed2D() == 0.0f)
	{
		m_driveAccelX = 0.0f;
		return m_yawRate != 0.0f;
	}
	driveVehicle(obj, physics, 0.0f, getMaxAcceleration(bdt), getVehicleStopDecel(bdt));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool Locomotor::fixInvalidPosition(Object* obj, PhysicsBehavior *physics)
{
	if (obj->isKindOf(KINDOF_DOZER)) {
		// don't fix him.
		return false;
	}
#define no_IGNORE_INVALID
#ifdef IGNORE_INVALID
	// Right now we ignore invalid positions, so when units clip the edge of a building or cliff
	// they don't get stuck.  jba. 12SEPT02
	return false;
#else
	Int dx = 0;
	Int dy = 0;
	Int i, j;
	for (j=-1; j<2; j++) {
		for (i=-1; i<2; i++) {
			Coord3D thePos = *obj->getPosition();
			thePos.x += i*PATHFIND_CELL_SIZE_F;
			thePos.y += j*PATHFIND_CELL_SIZE_F;
			if (!TheAI->pathfinder()->validMovementTerrain(obj->getLayer(), this, &thePos)) {
				if (i<0) dx += 1;
				if (i>0) dx -= 1;
				if (j<0) dy += 1;
				if (j>0) dy -= 1;
			}
		}
	}
	if (dx || dy) {

		Coord3D correction;
		correction.x = dx*physics->getMass()/5;
		correction.y = dy*physics->getMass()/5;
		correction.z = 0;

		Coord3D correctionNormalized = correction;
		correctionNormalized.normalize();

		Coord3D velocity;
		// Kill current velocity	in the direction of the correction.
		velocity = *physics->getVelocity();
		Real dot = (velocity.x*correctionNormalized.x) + (velocity.y*correctionNormalized.y);
		if (dot>.25f) {
			// It was already leaving.
			return false;
		}
		
		
		// Kill current accel
		//physics->clearAcceleration();

		if (dot<0) {
			dot = sqrt(-dot);
			correctionNormalized.x *= dot*physics->getMass();
			correctionNormalized.y *= dot*physics->getMass();
			physics->applyMotiveForce(&correctionNormalized);
		}

		// apply correction.
		physics->applyMotiveForce(&correction);
		return true;
	}
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::calcMinTurnRadius(BodyDamageType condition, Real* timeToTravelThatDist) const
{
	Real minSpeed = getMinSpeed();								// in dist/frame
	Real maxTurnRate = getMaxTurnRate(condition);	// in rads/frame

	/*
		our minimum circumference will be like so:
		
		Real minTurnCircum = maxSpeed * (2*PI / maxTurnRate);

		so therefore our minimum turn radius is:
		
		Real minTurnRadius = minTurnCircum / 2*PI;

		so we just eliminate the middleman:
	*/
	// if we can't turn, return a huge-but-finite radius rather than NAN...
	Real minTurnRadius = (maxTurnRate > 0.0f) ? minSpeed / maxTurnRate : BIGNUM;

	if (timeToTravelThatDist)
		*timeToTravelThatDist = (minSpeed > 0.0f) ? (minTurnRadius / minSpeed) : 0.0f;

	return minTurnRadius;
}


//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionLegs(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{
	if (getIsDownhillOnly() && obj->getPosition()->z < goalPos.z)
	{
		return;
	}
	
	Real maxAcceleration = getMaxAcceleration( obj->getBodyModule()->getDamageState() );

	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	Real maxSpeed = getMaxSpeedForCondition( obj->getBodyModule()->getDamageState() );
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	// Locomotion for infantry.
	//
	// Orient toward goal position
	//
	Real actualSpeed = physics->getForwardSpeed2D();
	Real angle = obj->getOrientation();
//	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, &goalPos );
//	Real desiredAngle = angle + relAngle;
	Real desiredAngle = ATan2(goalPos.y - obj->getPosition()->y, goalPos.x - obj->getPosition()->x);

	if (m_template->m_wanderWidthFactor != 0.0f) {
		Real angleLimit = PI/8 * m_template->m_wanderWidthFactor;
		// This is the wander offline code - it forces the desired angle away from the goal, so we wander back & forth.  jba.
		if (getFlag(OFFSET_INCREASING)) {
			m_angleOffset += m_offsetIncrement*actualSpeed;
			if (m_angleOffset > angleLimit) {
				setFlag(OFFSET_INCREASING, false);
			}
		} else {
			m_angleOffset -= m_offsetIncrement*actualSpeed;
			if (m_angleOffset<-angleLimit) {
				setFlag(OFFSET_INCREASING, true);
			}
		}
		desiredAngle = normalizeAngle(desiredAngle+m_angleOffset);
	}
	
	Real relAngle = stdAngleDiff(desiredAngle, angle);
	if (fabs(goalPos.x - obj->getPosition()->x) < 0.1f && fabs(goalPos.y - obj->getPosition()->y) < 0.1f)
		relAngle = 0.0f;	// right on it the bearing is rounding noise

	/* EA swung the nose at a flat TurnRate and left the walk on its old line, so a soldier sent back
		 the way he came skidded backwards past his own nose for five frames while it came round, and any
		 bend past 45 degrees took his speed to nothing. The turn now winds up and down over
		 INFANTRY_YAW_EASE_FRAMES, his feet carry the walk round with the nose, and his speed follows the
		 cosine of what is left to turn: nearly all of it on a gentle bend, none past a right angle, where
		 he stops in INFANTRY_STOP_FRAMES and turns on the spot. */
	Real turnRate = getMaxTurnRate(obj->getBodyModule()->getDamageState());
	turnVehicle(obj, physics, easeYaw(relAngle, turnRate, turnRate / INFANTRY_YAW_EASE_FRAMES));

	Real goalSpeed = desiredSpeed * Cos(relAngle);
	if (goalSpeed < 0.0f)
		goalSpeed = 0.0f;

	// the fastest he can still stop from before the end of the route
	Real stopDecel = getLegsStopDecel(obj);
	if (!getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		Real stopSpeed = sqrt(2.0f * stopDecel * onPathDistToGoal);
		if (goalSpeed > stopSpeed)
			goalSpeed = stopSpeed;
	}
	if (goalSpeed < m_template->m_minSpeed)
		goalSpeed = m_template->m_minSpeed;

	//
	// Maintain goal speed
	//
	Real speedDelta = goalSpeed - actualSpeed;
	if (speedDelta != 0.0f)
	{
		Real mass = physics->getMass();
		Real acceleration = (speedDelta > 0.0f) ? maxAcceleration : -stopDecel;
		Real accelForce = mass * acceleration;

		/*
			don't accelerate/brake more than necessary. do a quick calc to 
			see how much force we really need to achieve our goal speed...
		*/
		Real maxForceNeeded = mass * speedDelta;
		if (fabs(accelForce) > fabs(maxForceNeeded))
			accelForce = maxForceNeeded;

		const Coord3D *dir = obj->getUnitDirectionVector2D();

		Coord3D force;
		force.x = accelForce * dir->x;
		force.y = accelForce * dir->y;
		force.z = 0.0f;

		

		// apply forces to object
		physics->applyMotiveForce( &force );
	}
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionClimb(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{
	Real maxAcceleration = getMaxAcceleration( obj->getBodyModule()->getDamageState() );

	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	Real maxSpeed = getMaxSpeedForCondition( obj->getBodyModule()->getDamageState() );
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	// Locomotion for climbing infantry.


	Bool moveBackwards = false;

	Real dx, dy, dz;

	Coord3D pos = *obj->getPosition();
	
	dx = pos.x - goalPos.x;
	dy = pos.y - goalPos.y;
	dz = pos.z - goalPos.z;
	if (dz*dz > sqr(PATHFIND_CELL_SIZE_F)) {
		setFlag(CLIMBING, true);
	} 
	if (fabs(dz)<1) {
		setFlag(CLIMBING, false);
	}


	//setFlag(CLIMBING, true);

	if (getFlag(CLIMBING)) {
		Coord3D delta = goalPos;
		delta.x -= pos.x;
		delta.y -= pos.y;
		delta.z = 0;
		delta.normalize();
		delta.x += pos.x;
		delta.y += pos.y;
		delta.z = TheTerrainLogic->getGroundHeight(delta.x, delta.y);
		if (delta.z < pos.z-0.1) {
			moveBackwards = true;
		}

		Real groundSlope = fabs(delta.z - pos.z);
		if (groundSlope<1.0f) groundSlope = 1.0f;
		
		if (groundSlope>1.0f) {
			desiredSpeed /= groundSlope*4;
		}
	}
	setFlag(MOVING_BACKWARDS, moveBackwards);

	//
	// Orient toward goal position
	//
	Real angle = obj->getOrientation();
//	Real relAngle = ThePartitionManager->getRelativeAngle2D( obj, &goalPos );
//	Real desiredAngle = angle + relAngle;
	Real desiredAngle = ATan2(goalPos.y - obj->getPosition()->y, goalPos.x - obj->getPosition()->x);
	Real relAngle = stdAngleDiff(desiredAngle, angle);

	if (moveBackwards) {
		desiredAngle = stdAngleDiff(desiredAngle, PI);
		relAngle = stdAngleDiff(desiredAngle, angle);
	}

	locoUpdate_moveTowardsAngle(obj, desiredAngle);

	//
	// Modulate speed according to turning. The more we have to turn, the slower we go
	//
	const Real QUARTERPI = PI / 4.0f;
	Real angleCoeff = (Real)fabs( relAngle ) / (QUARTERPI);
	if (angleCoeff > 1.0f)
		angleCoeff = 1.0;

	Real goalSpeed = (1.0f - angleCoeff) * desiredSpeed;

	Real actualSpeed = physics->getForwardSpeed2D();

	if (moveBackwards) {
		actualSpeed = -actualSpeed;
	}

	//Real slowDownDist = (actualSpeed - m_template->m_minSpeed) / getBraking();
	Real slowDownDist = calcSlowDownDist(actualSpeed, m_template->m_minSpeed, getBraking());
	if (onPathDistToGoal < slowDownDist && !getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		goalSpeed = m_template->m_minSpeed;
	}

	//
	// Maintain goal speed
	//
	Real speedDelta = goalSpeed - actualSpeed;
	if (moveBackwards) {
		speedDelta = -goalSpeed+actualSpeed;
	}
	if (speedDelta != 0.0f)
	{
		Real mass = physics->getMass();
		Real acceleration;
		if (moveBackwards) {
			acceleration = (speedDelta < 0.0f) ? -maxAcceleration : getBraking();
		}	else {
			acceleration = (speedDelta > 0.0f) ? maxAcceleration : -getBraking();
		}
		Real accelForce = mass * acceleration;

		/*
			don't accelerate/brake more than necessary. do a quick calc to 
			see how much force we really need to achieve our goal speed...
		*/
		Real maxForceNeeded = mass * speedDelta;
		if (fabs(accelForce) > fabs(maxForceNeeded))
			accelForce = maxForceNeeded;

		const Coord3D *dir = obj->getUnitDirectionVector2D();

		Coord3D force;
		force.x = accelForce * dir->x;
		force.y = accelForce * dir->y;
		force.z = 0.0f;

		// apply forces to object
		physics->applyMotiveForce( &force );
	}
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionWings(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{
#ifdef CIRCLE_FOR_LANDING
	if (m_circleThresh > 0.0f)
	{
		// if we are going a mostly-vertical maneuver, circle in order to
		// gain/lose altitude, then resume course...
		const Coord3D* pos = obj->getPosition();
		Real dx = goalPos.x - pos->x;
		Real dy = goalPos.y - pos->y;
		Real dz = goalPos.z - pos->z;
		if (fabs(dz) > m_circleThresh)
		{
			// aim for the spot on the opposite side of the circle.

			// find the direction towards our goal pos
			Real angleTowardPos = 
					(isNearlyZero(dx) && isNearlyZero(dy)) ?
					obj->getOrientation() :
					ATan2(dy, dx);

			Real aimDir = (PI - PI/8);
			angleTowardPos += aimDir;

			BodyDamageType bdt = obj->getBodyModule()->getDamageState();
			Real turnRadius = calcMinTurnRadius(bdt, NULL) * 4;

			// project a spot "radius" dist away from it, in that dir
			Coord3D desiredPos = goalPos;
			desiredPos.x += Cos(angleTowardPos) * turnRadius;
			desiredPos.y += Sin(angleTowardPos) * turnRadius;
			moveTowardsPositionOther(obj, physics, desiredPos, 0, desiredSpeed);
			return;
		}
	}
#endif

	// handle the 2D component.
	moveTowardsPositionOther(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionHover(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed, const Coord3D *faceTarget, Bool fighting)
{
	// handle the 2D component.
	if (isHelicopter(obj))
		moveTowardsPositionHelicopter(obj, physics, goalPos, onPathDistToGoal, desiredSpeed, faceTarget, fighting);
	else
		moveTowardsPositionOther(obj, physics, goalPos, onPathDistToGoal, desiredSpeed);

	// Only hover locomotors care about their OverWater special effects.  (OverWater also affects speed, so this is not a client thing)
	Coord3D newPosition = *obj->getPosition();
	if( TheTerrainLogic->isUnderwater( newPosition.x, newPosition.y ) )
	{
		if( ! getFlag( OVER_WATER ) )
		{
			// Change my model condition because I used to not be over water, but now I am
			setFlag( OVER_WATER, TRUE );
			obj->setModelConditionState( MODELCONDITION_OVER_WATER );
		}
	}
	else
	{
		if( getFlag( OVER_WATER ) )
		{
			// Here, I was, but now I'm not
			setFlag( OVER_WATER, FALSE );
			obj->clearModelConditionState( MODELCONDITION_OVER_WATER );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/*
	A helicopter flies the velocity it wants rather than the way it faces. It wants the goal's
	direction at the speed it can still stop from, and steerHelicopter eases its 2D velocity there.
	The planned stop is gentler than Braking (getHelicopterStopDecel), starts early enough for the
	eased braking to build up, and fades out over the last HELICOPTER_STOP_EASE_FRAMES, so the
	helicopter flares to a halt on the goal instead of slamming into one. The nose is a separate
	choice: on the target when there is one, and otherwise turned into the flight as it goes. Only a
	hop of TURN_INTO_FLIGHT_MIN_DIST or less is flown backwards or sideways with the nose left where
	it is; a turn that has started inside a longer move carries on until the helicopter is over its
	goal. A helicopter fighting with a gun that reaches its target without the nose turns into the
	flight only while the goal is more than FIGHTING_TURN_SECONDS out at full speed and
	FIGHTING_TURN_MIN_DIST away, so a short pull-back or slide keeps its nose and its missiles on
	the target.
*/
static const Real HELICOPTER_STOP_EASE_FRAMES = 6.0f;	// the planned braking fades out over about this many frames
static const Real HELICOPTER_YAW_EASE_FRAMES = 10.0f;	// frames for the nose to wind up to TurnRate, or back down from it
static const Real HELICOPTER_ACCEL_EASE_FRAMES = 5.0f;	// frames for the pull to build from nothing to full Acceleration
static const Real HELICOPTER_VELOCITY_FRAMES = 4.0f;	// a velocity error is asked back over about this many frames
// the stop is planned this many frames of travel early, for the braking to build up in
static const Real HELICOPTER_STOP_LEAD_FRAMES = HELICOPTER_ACCEL_EASE_FRAMES * 0.5f + HELICOPTER_VELOCITY_FRAMES;

Real Locomotor::getHelicopterStopDecel(BodyDamageType condition) const
{
	Real decel = 2.0f * getMaxAcceleration(condition);
	if (decel > getBraking())
		decel = getBraking();
	return decel;
}

void Locomotor::moveTowardsPositionHelicopter(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed, const Coord3D *faceTarget, Bool fighting)
{
	const Real TURN_INTO_FLIGHT_MIN_DIST = 40.0f;	// a hop this short slides; about a helicopter's length
	const Real FIGHTING_TURN_SECONDS = 3.0f;
	const Real FIGHTING_TURN_MIN_DIST = 400.0f;
	const Real TOO_CLOSE_TO_AIM = 1.0f;	// right over the target the bearing to it is noise
	const Real TURN_INTO_FLIGHT_FROM_HOVER = 0.5f;	// share of TurnRate the turn into the flight starts at from a hover

	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	if (desiredSpeed > maxSpeed)
		desiredSpeed = maxSpeed;

	const Coord3D *pos = obj->getPosition();
	const Coord3D *vel = physics->getVelocity();
	Real speed = sqrt(sqr(vel->x) + sqr(vel->y));
	const Coord3D *noseToward = NULL;
	Real rateShare = 1.0f;
	if (faceTarget)
	{
		if (fabs(faceTarget->x - pos->x) > TOO_CLOSE_TO_AIM || fabs(faceTarget->y - pos->y) > TOO_CLOSE_TO_AIM)
			noseToward = faceTarget;
	}
	else if (fighting ? onPathDistToGoal > maxSpeed * LOGICFRAMES_PER_SECOND * FIGHTING_TURN_SECONDS && onPathDistToGoal > FIGHTING_TURN_MIN_DIST
					 : onPathDistToGoal > TURN_INTO_FLIGHT_MIN_DIST
						 || (m_yawRate != 0.0f && (fabs(goalPos.x - pos->x) > TOO_CLOSE_TO_AIM || fabs(goalPos.y - pos->y) > TOO_CLOSE_TO_AIM)))
	{
		/* The nose comes round as the speed builds, at TURN_INTO_FLIGHT_FROM_HOVER of TurnRate from
			 a hover and all of it at full speed. At full TurnRate a Comanche swings 180 degrees before
			 it has gone 30, and the turn reads as done on the spot. A nose still turning when the goal
			 comes within TURN_INTO_FLIGHT_MIN_DIST keeps turning, or a hop a little longer than that
			 would stop its turn halfway and come in sideways. */
		noseToward = &goalPos;
		if (speed < maxSpeed)
			rateShare = TURN_INTO_FLIGHT_FROM_HOVER + (1.0f - TURN_INTO_FLIGHT_FROM_HOVER) * speed / maxSpeed;
	}
	physics->setTurning(turnHelicopter(obj, noseToward, rateShare));

	Real goalSpeed = desiredSpeed;
	if (!getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		/* The fastest speed it can still stop from in the distance left, slowing at decel. The
			 plain sqrt(2*decel*dist) holds full braking to the last frame and then stops dead; with
			 the ease speed taken off, the braking fades out at the end. The distance is counted short
			 by the travel of the lead frames, the time the eased braking takes to build up, or the
			 helicopter overshoots a short hop. */
		Real distLeft = onPathDistToGoal - speed * HELICOPTER_STOP_LEAD_FRAMES;
		if (distLeft < 0.0f)
			distLeft = 0.0f;
		Real decel = getHelicopterStopDecel(bdt);
		Real ease = decel * HELICOPTER_STOP_EASE_FRAMES;
		Real stopSpeed = sqrt(2.0f * decel * distLeft + ease * ease) - ease;
		if (goalSpeed > stopSpeed)
			goalSpeed = stopSpeed;
	}

	Real wantX = 0.0f;
	Real wantY = 0.0f;
	Real dx = goalPos.x - pos->x;
	Real dy = goalPos.y - pos->y;
	Real dist = sqrt(dx*dx + dy*dy);
	if (dist > 0.001f)
	{
		wantX = dx * goalSpeed / dist;
		wantY = dy * goalSpeed / dist;
	}

	steerHelicopter(obj, physics, wantX, wantY);
}

//-------------------------------------------------------------------------------------------------
/**
	Ease a helicopter's 2D velocity towards (wantX, wantY). It asks for the velocity error back over
	HELICOPTER_VELOCITY_FRAMES, no harder than its Acceleration, or its Braking when that slows the way
	it is going; and the pull it actually applies moves towards that by at most an
	HELICOPTER_ACCEL_EASE_FRAMES share of its Acceleration a frame, so a start, a stop or a change
	of direction builds up and dies away instead of jumping. The ask is also never more than the pull
	can wind down from in the velocity error left, the eased stop of the pull itself: without that a
	hard stop from full speed (a Comanche whose rocket pods come into range) carried its braking past
	a standstill and backed 33 the way it came.
*/
void Locomotor::steerHelicopter(Object* obj, PhysicsBehavior *physics, Real wantX, Real wantY)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxAccel = getMaxAcceleration(bdt);
	Real jerk = maxAccel / HELICOPTER_ACCEL_EASE_FRAMES;
	const Coord3D *vel = physics->getVelocity();

	Real errX = wantX - vel->x;
	Real errY = wantY - vel->y;
	Real ax = errX / HELICOPTER_VELOCITY_FRAMES;
	Real ay = errY / HELICOPTER_VELOCITY_FRAMES;
	Real ask = sqrt(ax*ax + ay*ay);
	Real limit = (ax * vel->x + ay * vel->y < 0.0f) ? getBraking() : maxAccel;
	Real unwind = sqrt(2.0f * jerk * sqrt(errX*errX + errY*errY));
	if (limit > unwind)
		limit = unwind;
	if (ask > limit)
	{
		ax *= limit / ask;
		ay *= limit / ask;
	}

	Real changeX = ax - m_driveAccelX;
	Real changeY = ay - m_driveAccelY;
	Real change = sqrt(changeX*changeX + changeY*changeY);
	if (change > jerk)
	{
		changeX *= jerk / change;
		changeY *= jerk / change;
	}
	m_driveAccelX += changeX;
	m_driveAccelY += changeY;

	if (m_driveAccelX == 0.0f && m_driveAccelY == 0.0f)
		return;

	Real mass = physics->getMass();
	Coord3D force;
	force.x = mass * m_driveAccelX;
	force.y = mass * m_driveAccelY;
	force.z = 0.0f;
	physics->applyMotiveForce(&force);
}

//-------------------------------------------------------------------------------------------------
/// bring a helicopter's 2D velocity to rest whichever way it points, eased like any other change
void Locomotor::brakeHelicopter(Object* obj, PhysicsBehavior *physics)
{
	steerHelicopter(obj, physics, 0.0f, 0.0f);
}

//-------------------------------------------------------------------------------------------------
/**
	Swing a helicopter's nose towards a point, or let the swing die away when toward is null. The
	turn winds up to TurnRate over HELICOPTER_YAW_EASE_FRAMES and winds down again so it stops on the
	heading, where rotateTowardsPosition jumps straight to full rate and stops dead. rateShare takes
	the top rate below TurnRate.
*/
PhysicsTurningType Locomotor::turnHelicopter(Object* obj, const Coord3D *toward, Real rateShare)
{
	Real turnRate = getMaxTurnRate(obj->getBodyModule()->getDamageState());
	Real yawAccel = turnRate / HELICOPTER_YAW_EASE_FRAMES;
	Real maxRate = turnRate * rateShare;
	Real angle = obj->getOrientation();

	Real error = 0.0f;
	if (toward)
	{
		const Coord3D *pos = obj->getPosition();
		error = stdAngleDiff(ATan2(toward->y - pos->y, toward->x - pos->x), angle);
	}

	if (easeYaw(error, maxRate, yawAccel) == 0.0f)
		return TURN_NONE;
	obj->setOrientation(normalizeAngle(angle + m_yawRate));
	return (m_yawRate > 0.0f) ? TURN_POSITIVE : TURN_NEGATIVE;
}

//-------------------------------------------------------------------------------------------------
/**
	Move m_yawRate towards the rate that closes a heading error of error radians, by at most yawAccel
	a frame: as fast as maxRate allows and still wind down onto the heading, never past it in one
	frame. Returns the new rate, which is the turn to make this frame.
*/
Real Locomotor::easeYaw(Real error, Real maxRate, Real yawAccel)
{
	Real rate = sqrt(2.0f * yawAccel * fabs(error));
	if (rate > maxRate)
		rate = maxRate;
	if (rate > fabs(error))
		rate = fabs(error);
	Real wantRate = (error < 0.0f) ? -rate : rate;

	Real change = wantRate - m_yawRate;
	if (change > yawAccel)
		change = yawAccel;
	else if (change < -yawAccel)
		change = -yawAccel;
	m_yawRate += change;
	return m_yawRate;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionThrust(Object* obj, PhysicsBehavior *physics, const Coord3D& chasedGoalPos, Real onPathDistToGoal, Real desiredSpeed)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();

	Real maxForwardSpeed = getMaxSpeedForCondition(bdt);

	// A missile that has not locked on yet flies at where a victim on the ground will be when it gets
	// there, not at where the victim is now, so it comes up to the lock already in front of a crossing
	// vehicle. The lock's cheat put these hits in anyway: Missile Defenders at Technicals driving past
	// killed as many with it as without. Aircraft keep the straight chase: see
	// Locomotor_projectileChasesStraight.
	Coord3D goalPos = chasedGoalPos;
	Object *victim = obj->isKindOf(KINDOF_PROJECTILE) ? obj->getAI()->getGoalObject() : NULL;
	if (victim && victim->getPhysics() && !victim->isAboveTerrain() && !obj->getStatusBits().test(OBJECT_STATUS_BRAKING))
	{
		Coord3D toVictim;
		toVictim.set(goalPos.x - obj->getPosition()->x, goalPos.y - obj->getPosition()->y, goalPos.z - obj->getPosition()->z);
		Coord3D lead = Locomotor_groundLead(toVictim, *victim->getPhysics()->getVelocity(), maxForwardSpeed);
		goalPos.x += lead.x;
		goalPos.y += lead.y;
	}

	desiredSpeed = clamp(m_template->m_minSpeed, desiredSpeed, maxForwardSpeed);
	Real actualForwardSpeed = physics->getForwardSpeed3D();

	if (getBraking() > 0)
	{
		//Real slowDownDist = (actualForwardSpeed - m_template->m_minSpeed) / getBraking();
		Real slowDownDist = calcSlowDownDist(actualForwardSpeed, m_template->m_minSpeed, getBraking());
		if (onPathDistToGoal < slowDownDist && !getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
			desiredSpeed = m_template->m_minSpeed;
	}

	Coord3D localGoalPos = goalPos;
#ifdef USE_ZDIR_DAMPING
	Real zDirDamping = 0.0f;
#endif

	//out of the handleBehaviorZ() function
	Coord3D pos = *obj->getPosition();
	Real distToGoal2D = sqrt(sqr(goalPos.x - pos.x) + sqr(goalPos.y - pos.y));
	if( m_preferredHeight != 0.0f && !getFlag(PRECISE_Z_POS) && distToGoal2D < m_sineDescentDistance )
	{
		// Come down along height(d) = h * sin(PI/2 * d / descentDist), aiming a few frames ahead on the one
		// such curve that passes through the missile now. A Scud is still near the top of its climb here,
		// about 230 over the goal against a cruise height of 120, so the curve starts from where it is.
		// Aiming at the goal itself instead was a straight dive with a kink where it began.
		Real lookAhead = SINE_DESCENT_LOOKAHEAD_FRAMES * physics->getVelocityMagnitude();
		if (distToGoal2D > lookAhead)
		{
			Real aimDist = distToGoal2D - lookAhead;
			Real aimFraction = aimDist / distToGoal2D;
			Real heightNow = pos.z - goalPos.z;
			localGoalPos.x = goalPos.x + (pos.x - goalPos.x) * aimFraction;
			localGoalPos.y = goalPos.y + (pos.y - goalPos.y) * aimFraction;
			localGoalPos.z = goalPos.z + heightNow * Sin(PI / 2 * aimDist / m_sineDescentDistance) / Sin(PI / 2 * distToGoal2D / m_sineDescentDistance);
		}
	}
	else if( m_preferredHeight != 0.0f && !getFlag(PRECISE_Z_POS) )
	{
		// If we have a preferred flight height, and we haven't been told explicitly to ignore it...
		Real surfaceHt =  getSurfaceHtAtPt(pos.x, pos.y);
		localGoalPos.z = m_preferredHeight + surfaceHt;
//		localGoalPos.z = goalPos.z;
		Real delta = localGoalPos.z - pos.z;
		delta *= getPreferredHeightDamping();	  
		localGoalPos.z = pos.z + delta;

#ifdef USE_ZDIR_DAMPING
		// closer we get to the preferred height, less we adjust z-thrust,
		// so we tend to "level out" at that height. we don't use this till
		// below, but go ahead and calc it now...
		Real MAX_VERTICAL_DAMP_RANGE = m_preferredHeight * 0.5;
		delta = fabs(delta);
		if (delta > MAX_VERTICAL_DAMP_RANGE) 
			delta = MAX_VERTICAL_DAMP_RANGE;
		zDirDamping = 1.0f - (delta / MAX_VERTICAL_DAMP_RANGE);
#endif
	}

	Vector3 forwardDir = obj->getTransformMatrix()->Get_X_Vector();

	// Maintain goal speed
	Real forwardSpeedDelta = desiredSpeed - actualForwardSpeed;
	Real maxAccel = (forwardSpeedDelta > 0.0f || getBraking() == 0) ? getMaxAcceleration(bdt) : -getBraking();
	Real maxTurnRate = getMaxTurnRate(bdt);

	// what direction do we need to thrust in, in order to reach the goalpos?
	Vector3 desiredThrustDir;
	calcDirectionToApplyThrust(obj, physics, localGoalPos, maxAccel, desiredThrustDir);

	// we might not be able to thrust in that dir, so thrust as closely as we can
	Real maxThrustAngle =	(maxTurnRate > 0) ? (m_template->m_maxThrustAngle) : 0;
	Vector3 thrustDir;
	Real thrustAngle = tryToRotateVector3D(maxThrustAngle, forwardDir, desiredThrustDir, thrustDir);

	// note that we are trying to orient in the direction of our vel, not the dir of our thrust.
	if (!isNearlyZero(physics->getVelocityMagnitude()))
	{
		const Coord3D* veltmp = physics->getVelocity();
		Vector3 vel(veltmp->x, veltmp->y, veltmp->z);
		Bool adjust = true;
		if( obj->getStatusBits().test( OBJECT_STATUS_BRAKING ) ) 
		{
			// align to target, cause that's where we're going anyway.

			vel.Set(goalPos.x - pos.x, goalPos.y-pos.y, goalPos.z-pos.z);
			if (isNearlyZero(sqr(vel.X)+sqr(vel.Y)+sqr(vel.Z))) {
				// we are at target.
				adjust = false;
			}
			else if (obj->isKindOf(KINDOF_PROJECTILE) && !Locomotor_projectileChasesStraight(obj, vel))
			{
				// the projectile cheat moves along the nose, so this turn is the path it flies. It bends
				// towards where a moving victim will be when the missile gets there rather than where the
				// victim is now: rockets at driving tanks turned p95 4.1 and at most 7.5 degrees a frame.
				Real step = physics->getVelocityMagnitude();
				if (step < MIN_VEL)
					step = MIN_VEL;
				Object *victim = obj->getAI()->getGoalObject();
				if (victim && victim->getPhysics() && step < vel.Length())
				{
					Coord3D toVictim;
					toVictim.set(vel.X, vel.Y, vel.Z);
					// flat: an aircraft held at its cruise height still carries the climb in its velocity, and
					// leading on that sent rockets straight up past the helicopter they were chasing
					Coord3D victimVelocity = *victim->getPhysics()->getVelocity();
					victimVelocity.z = 0.0f;
					Coord3D lead = Locomotor_interceptOffset(toVictim, victimVelocity, step);
					vel.Set(lead.x, lead.y, lead.z);
				}
				maxTurnRate = calcArcTurnToGoal(forwardDir, vel, step);
			}
			else
			{
				maxTurnRate = 3*maxTurnRate;
			}
		}
#ifdef USE_ZDIR_DAMPING
		if (zDirDamping != 0.0f)
		{
			Vector3 vel2D(veltmp->x, veltmp->y, 0);
			// no need to normalize -- this call does that internally
			tryToRotateVector3D(-zDirDamping, vel, vel2D, vel);
		}
#endif
		if (adjust) {
			/*Real orient =*/ tryToOrientInThisDirection3D(obj, maxTurnRate, vel);
		}
	}

	if (forwardSpeedDelta != 0.0f || thrustAngle != 0.0f)
	{
		if (maxForwardSpeed <= 0.0f) 
		{
			maxForwardSpeed = 0.01f; // In some cases, this is 0, hack for now.  jba.
		}
		Real damping = clamp(0.0f, maxAccel / maxForwardSpeed, 1.0f);
		Vector3 curVel(physics->getVelocity()->x, physics->getVelocity()->y, physics->getVelocity()->z);

		Vector3 accelVec = thrustDir * maxAccel - curVel * damping;
		//DEBUG_LOG(("accel %f (max %f) vel %f (max %f) damping %f\n",accelVec.Length(),maxAccel,curVel.Length(),maxForwardSpeed,damping));

		Real mass = physics->getMass();

		Coord3D force;
		force.x = mass * accelVec.X;
		force.y = mass * accelVec.Y;
		force.z = mass * accelVec.Z;

		// apply forces to object
		physics->applyMotiveForce( &force );
	}
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::getSurfaceHtAtPt(Real x, Real y)
{
	Real ht = 0;

	Real z,waterZ;
	if (TheTerrainLogic->isUnderwater(x, y, &waterZ, &z)) {
		ht += waterZ;
	} else {
		ht += z;
	}
	
	return ht;
}

//-------------------------------------------------------------------------------------------------
Real Locomotor::calcLiftToUseAtPt(Object* obj, PhysicsBehavior *physics, Real curZ, Real surfaceAtPt, Real preferredHeight)
{
	/*
		take the classic equation:

			x = x0 + v*t + 0.5*a*t^2
		
		and solve for acceleration.
	*/
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxGrossLift = getMaxLift(bdt);
	Real maxNetLift = maxGrossLift + TheGlobalData->m_gravity;	// note that gravity is always negative.
	if (maxNetLift < 0)
		maxNetLift = 0;
	Real curVelZ = physics->getVelocity()->z;
	// going down, braking is limited by net lift; going up, braking is limited by gravity
	Real maxAccel;
	if (getFlag(ULTRA_ACCURATE))
		maxAccel = (curVelZ < 0) ? 2*maxNetLift : -2*maxNetLift;
	else
		maxAccel = (curVelZ < 0) ? maxNetLift : TheGlobalData->m_gravity;
	// see how far we need to slow to dead stop, given max braking
	Real desiredAccel;
	const Real TINY_ACCEL = 0.001f;
	if (fabs(maxAccel) > TINY_ACCEL)
	{
		Real deltaZ = preferredHeight - curZ;
		// calc how far it will take for us to go from cur speed to zero speed, at max accel.
		// Real brakeDist = calcSlowDownDist(curVelZ, 0, maxAccel);
		// in theory, the above is the correct calculation, but in practice,
		// doesn't work in some situations (eg, opening of USA01 map). Why, I dunno.
		// But for now I have gone back to the old, looks-incorrect-to-me-but-works calc. (srj)
		Real brakeDist = (sqr(curVelZ) / fabs(maxAccel));
		if (fabs(brakeDist) > fabs(deltaZ))
		{
			// if the dist-to-accel (or dist-to-brake) is further than the dist-to-go,
			// use the max accel.
			desiredAccel = maxAccel;
		}
		else if (fabs(curVelZ) > m_template->m_speedLimitZ)
		{
			// or, if we're going too fast, limit it here.
			desiredAccel = m_template->m_speedLimitZ - curVelZ;
		}
		else
		{
			// ok, figure out the correct accel to use to get us there at zero.
			//
			// dz = v t + 0.5 a t^2
			//	thus
			// a = 2(dz - v t)/t^2
			//	and
			// t = (-v +- sqrt(v*v + 2*a*dz))/a
			//
			// but if we assume t=1, then 
			//	a=2(dz-v)
			// then, plug it back in and see if t is really 1...
			desiredAccel = 2.0f * (deltaZ - curVelZ);
		}
	}
	else
	{
		desiredAccel = 0.0f;
	}
	Real liftToUse = desiredAccel - TheGlobalData->m_gravity;
	if (getFlag(ULTRA_ACCURATE))
	{
		// in ultra-accurate mode, we allow cheating.
		const Real UP_FACTOR = 3.0f;
		if (liftToUse > UP_FACTOR*maxGrossLift)
			liftToUse = UP_FACTOR*maxGrossLift;
		// srj sez: we used to clip lift to zero here (not allowing neg lift).
		// however, I now think that allowing neg lift in ultra-accurate mode is
		// a good and desirable thing; in particular, it enables jets to complete
		// "short" landings more accurately (previously they sometimes would "float"
		// down, which sucked.) if you need to bump this back to zero, check it carefully...
		else if (liftToUse < -maxGrossLift)
			liftToUse = -maxGrossLift;
	}
	else
	{
		if (liftToUse > maxGrossLift)
			liftToUse = maxGrossLift;
		else if (liftToUse < 0.0f)
			liftToUse = 0.0f;
	}

	return liftToUse;
}

//-------------------------------------------------------------------------------------------------
PhysicsTurningType Locomotor::rotateObjAroundLocoPivot(Object* obj, const Coord3D& goalPos, 
																											 Real maxTurnRate, Real *relAngle)
{
	Real angle = obj->getOrientation();
	Real offset = getTurnPivotOffset();	

	PhysicsTurningType turn = TURN_NONE;

	if (getFlag(IS_BRAKING)) offset = 0.0f; // When braking we do exact movement towards goal, instead of physics.
	//Rotating about pivot moves the object, and can make us miss our goal, so it is disabled.  jba.
	if (offset != 0.0f)
	{
		Real radius = obj->getGeometryInfo().getBoundingCircleRadius();
		Real turnPointOffset = offset * radius;

		Coord3D turnPos = *obj->getPosition();
		const Coord3D* dir = obj->getUnitDirectionVector2D();
		turnPos.x += dir->x * turnPointOffset;
		turnPos.y += dir->y * turnPointOffset;
		Real dx =goalPos.x - turnPos.x;
		Real dy = goalPos.y - turnPos.y;
		// If we are very close to the goal, we twitch due to rounding error.  So just return. jba.
		if (fabs(dx)<0.1f && fabs(dy)<0.1f) return TURN_NONE; 
		Real desiredAngle = ATan2(dy, dx);
		Real amount = stdAngleDiff(desiredAngle, angle);
		if (relAngle) *relAngle = amount;
		if (amount>maxTurnRate) {
			amount = maxTurnRate;
			turn = TURN_POSITIVE;
		} else if (amount < -maxTurnRate) {
			amount = -maxTurnRate;
			turn = TURN_NEGATIVE;
		} else {
			turn = TURN_NONE;
		}

#if 0
		Coord3D desiredPos = *obj->getPosition();	// well, desired Dir, anyway
		desiredPos.x += Cos(angle + amount) * radius;
		desiredPos.y += Sin(angle + amount) * radius;


		// so, the thing is, we want to rotate ourselves so that our *center* is rotated
		// by the given amount, but the rotation must be around turnPos. so do a little
		// back-calculation.
		Real angleDesiredForTurnPos = ATan2(desiredPos.y - turnPos.y, desiredPos.x - turnPos.x);
		amount = angleDesiredForTurnPos - angle;
#endif
		/// @todo srj -- there's probably a more efficient & more direct way to do this. find it.
		Matrix3D mtx;
		Matrix3D tmp(1);
		tmp.Translate(turnPos.x, turnPos.y, 0);
		tmp.In_Place_Pre_Rotate_Z(amount);
		tmp.Translate(-turnPos.x, -turnPos.y, 0);

		mtx.mul(tmp, *obj->getTransformMatrix());

		obj->setTransformMatrix(&mtx);
	}
	else
	{
		Real desiredAngle = ATan2(goalPos.y - obj->getPosition()->y, goalPos.x - obj->getPosition()->x);
		Real amount = stdAngleDiff(desiredAngle, angle);
		if (relAngle) *relAngle = amount;
		if (amount>maxTurnRate) {
			amount = maxTurnRate;
			turn = TURN_POSITIVE;
		} else if (amount < -maxTurnRate) {
			amount = -maxTurnRate;
			turn = TURN_NEGATIVE;
		} else {
			turn = TURN_NONE;
		}
		obj->setOrientation( normalizeAngle(angle + amount) );
	}
	return turn;
}

//-------------------------------------------------------------------------------------------------
/// turn by amount about the locomotor's pivot point, the way rotateObjAroundLocoPivot turns
void Locomotor::turnObjAroundLocoPivot(Object* obj, Real amount)
{
	Real offset = getTurnPivotOffset();
	if (getFlag(IS_BRAKING))
		offset = 0.0f;	// sliding onto the goal: a turn about the rear axle would carry it off again
	if (offset == 0.0f)
	{
		obj->setOrientation(normalizeAngle(obj->getOrientation() + amount));
		return;
	}

	Real turnPointOffset = offset * obj->getGeometryInfo().getBoundingCircleRadius();
	Coord3D turnPos = *obj->getPosition();
	const Coord3D* dir = obj->getUnitDirectionVector2D();
	turnPos.x += dir->x * turnPointOffset;
	turnPos.y += dir->y * turnPointOffset;

	Matrix3D mtx;
	Matrix3D tmp(1);
	tmp.Translate(turnPos.x, turnPos.y, 0);
	tmp.In_Place_Pre_Rotate_Z(amount);
	tmp.Translate(-turnPos.x, -turnPos.y, 0);
	mtx.mul(tmp, *obj->getTransformMatrix());
	obj->setTransformMatrix(&mtx);
}

//-------------------------------------------------------------------------------------------------
/*
	return true if we can maintain the position without being called every frame (eg, we are
	resting on the ground), false if not (eg, we are hovering or circling)
*/
Bool Locomotor::handleBehaviorZ(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos)
{
	Bool requiresConstantCalling = TRUE;

	// keep the agent aligned on the terrain
	switch(m_template->m_behaviorZ)
	{
		case Z_NO_Z_MOTIVE_FORCE:
			// nothing to do. 
			requiresConstantCalling = FALSE;
			break;

		case Z_SEA_LEVEL:
			requiresConstantCalling = TRUE;
			if( !obj->isDisabledByType( DISABLED_HELD ) )
			{
				Coord3D pos = *obj->getPosition();
				Real waterZ;
				if (TheTerrainLogic->isUnderwater(pos.x, pos.y, &waterZ)) {
					pos.z = waterZ;
				} else {
					pos.z = TheTerrainLogic->getLayerHeight(pos.x, pos.y, obj->getLayer());
				}
				obj->setPosition(&pos);
			}
			break;

		case Z_FIXED_SURFACE_RELATIVE_HEIGHT:
		case Z_FIXED_ABSOLUTE_HEIGHT:
			requiresConstantCalling = TRUE;
			{
				Coord3D pos = *obj->getPosition();
				Bool surfaceRel = (m_template->m_behaviorZ == Z_FIXED_SURFACE_RELATIVE_HEIGHT);
				Real surfaceHt = surfaceRel ? getSurfaceHtAtPt(pos.x, pos.y) : 0.0f;
				pos.z = m_preferredHeight + (surfaceRel ? surfaceHt : 0);
				obj->setPosition(&pos);
			}
			break;

		case Z_RELATIVE_TO_GROUND_AND_BUILDINGS:
			requiresConstantCalling = TRUE;
			{
				// srj sez: use getGroundOrStructureHeight(), because someday it will cache building heights...
				Coord3D pos = *obj->getPosition();
				Real surfaceHt = ThePartitionManager->getGroundOrStructureHeight(pos.x, pos.y);

					pos.z = m_preferredHeight + surfaceHt;

					obj->setPosition(&pos);

			}
			break;
		case Z_SMOOTH_RELATIVE_TO_HIGHEST_LAYER:
			requiresConstantCalling = TRUE;
			{
				if (m_preferredHeight != 0.0f || getFlag(PRECISE_Z_POS))
				{
					Coord3D pos = *obj->getPosition();
					
					// srj sez: if we aren't on the ground, never find the ground layer
					PathfindLayerEnum layerAtDest = obj->getLayer();
					if (layerAtDest == LAYER_GROUND)
						layerAtDest = TheTerrainLogic->getHighestLayerForDestination( &pos );

					Real surfaceHt;
					Coord3D normal;
					const Bool clip = false;	// return the height, even if off the edge of the bridge proper.
					surfaceHt = TheTerrainLogic->getLayerHeight( pos.x, pos.y, layerAtDest, &normal, clip );

					Real preferredHeight = m_preferredHeight + surfaceHt;
					if (getFlag(PRECISE_Z_POS))
						preferredHeight = goalPos.z;

					Real delta = preferredHeight - pos.z;
					delta *= getPreferredHeightDamping();
					preferredHeight = pos.z + delta;

					Real liftToUse = calcLiftToUseAtPt(obj, physics, pos.z, surfaceHt, preferredHeight);

					//DEBUG_LOG(("HandleBZ %d LiftToUse %f\n",TheGameLogic->getFrame(),liftToUse));
					if (liftToUse != 0.0f)
					{
						Coord3D force;
						force.x = 0.0f;
						force.y = 0.0f;
						force.z = liftToUse * physics->getMass();
						physics->applyMotiveForce(&force);
					}
				}
			}
			break;
	
		case Z_SURFACE_RELATIVE_HEIGHT:
		case Z_ABSOLUTE_HEIGHT:
			requiresConstantCalling = TRUE;
			{
				if (m_preferredHeight != 0.0f || getFlag(PRECISE_Z_POS))
				{
					Coord3D pos = *obj->getPosition();
					
					Bool surfaceRel = (m_template->m_behaviorZ == Z_SURFACE_RELATIVE_HEIGHT);
					Real surfaceHt = surfaceRel ? getSurfaceHtAtPt(pos.x, pos.y) : 0.0f;
					Real preferredHeight = m_preferredHeight + (surfaceRel ? surfaceHt : 0);
					if (getFlag(PRECISE_Z_POS))
						preferredHeight = goalPos.z;

					Real delta = preferredHeight - pos.z;
					delta *= getPreferredHeightDamping();
					preferredHeight = pos.z + delta;

					Real liftToUse = calcLiftToUseAtPt(obj, physics, pos.z, surfaceHt, preferredHeight);

					//DEBUG_LOG(("HandleBZ %d LiftToUse %f\n",TheGameLogic->getFrame(),liftToUse));
					if (liftToUse != 0.0f)
					{
						Coord3D force;
						force.x = 0.0f;
						force.y = 0.0f;
						force.z = liftToUse * physics->getMass();
						physics->applyMotiveForce(&force);
					}
				}
			}
			break;
	}

	return requiresConstantCalling;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::moveTowardsPositionOther(Object* obj, PhysicsBehavior *physics, const Coord3D& goalPos, Real onPathDistToGoal, Real desiredSpeed)
{
	BodyDamageType bdt = obj->getBodyModule()->getDamageState();
	Real maxAcceleration = getMaxAcceleration(bdt);

	// sanity, we cannot use desired speed that is greater than our max speed we are capable of moving at
	Real maxSpeed = getMaxSpeedForCondition(bdt);
	if( desiredSpeed > maxSpeed )
		desiredSpeed = maxSpeed;

	Real goalSpeed = desiredSpeed;
	Real actualSpeed = physics->getForwardSpeed2D();

	// Locomotion for other things, ie don't know what it is jba :)
	//
	// Orient toward goal position
	// exception: if very close (ie, we could get there in 2 frames or less),\
	// and ULTRA_ACCURATE, just slide into place
	//
	const Coord3D* pos =  obj->getPosition();
	Coord3D dirToApplyForce = *obj->getUnitDirectionVector2D();

//DEBUG_ASSERTLOG(!getFlag(ULTRA_ACCURATE),("thresh %f %f (%f %f)\n",
//fabs(goalPos.y - pos->y),fabs(goalPos.x - pos->x),
//fabs(goalPos.y - pos->y)/goalSpeed,fabs(goalPos.x - pos->x)/goalSpeed));
	if (getFlag(ULTRA_ACCURATE) && 
				fabs(goalPos.y - pos->y) <= goalSpeed * m_template->m_ultraAccurateSlideIntoPlaceFactor && 
				fabs(goalPos.x - pos->x) <= goalSpeed * m_template->m_ultraAccurateSlideIntoPlaceFactor)
	{
		// don't turn, just slide in the right direction
		physics->setTurning(TURN_NONE);
		dirToApplyForce.x = goalPos.x - pos->x;
		dirToApplyForce.y = goalPos.y - pos->y;
		dirToApplyForce.z = 0.0f;
		dirToApplyForce.normalize();
	}
	else
	{
		PhysicsTurningType rotating = rotateTowardsPosition(obj, goalPos);
		physics->setTurning(rotating);
	}

	if (!getFlag(NO_SLOW_DOWN_AS_APPROACHING_DEST))
	{
		Real slowDownDist = calcSlowDownDist(actualSpeed, m_template->m_minSpeed, getBraking());
		if (onPathDistToGoal < slowDownDist)
		{
			goalSpeed = m_template->m_minSpeed;
		}
	}

	//
	// Maintain goal speed
	//
	Real speedDelta = goalSpeed - actualSpeed;
	if (speedDelta != 0.0f)
	{
		Real mass = physics->getMass();
		Real acceleration = (speedDelta > 0.0f) ? maxAcceleration : -getBraking();
		Real accelForce = mass * acceleration;

		/*
			don't accelerate/brake more than necessary. do a quick calc to 
			see how much force we really need to achieve our goal speed...
		*/
		Real maxForceNeeded = mass * speedDelta;
		if (fabs(accelForce) > fabs(maxForceNeeded))
			accelForce = maxForceNeeded;

		Coord3D force;
		force.x = accelForce * dirToApplyForce.x;
		force.y = accelForce * dirToApplyForce.y;
		force.z = 0.0f;

		// apply forces to object
		physics->applyMotiveForce( &force );
	}

}


//-------------------------------------------------------------------------------------------------
/*
	return true if we can maintain the position without being called every frame (eg, we are
	resting on the ground), false if not (eg, we are hovering or circling)
*/
Bool Locomotor::locoUpdate_maintainCurrentPosition(Object* obj)
{
	if (!getFlag(MAINTAIN_POS_IS_VALID))
	{
		m_maintainPos = *obj->getPosition();
		setFlag(MAINTAIN_POS_IS_VALID, true);
	}

	m_donutTimer = TheGameLogic->getFrame()+DONUT_TIME_DELAY_SECONDS*LOGICFRAMES_PER_SECOND;
	setFlag(IS_BRAKING, false);
	/* The status goes with the flag. Left set, physics stopped moving a parked unit by its own speed,
		 and its next order began with a slide towards the new goal on top of the drive. A missile sets
		 it itself for its last dive and keeps it. */
	if (!obj->isKindOf(KINDOF_PROJECTILE))
		obj->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );
	PhysicsBehavior *physics = obj->getPhysics();
	if (physics == NULL)
	{
		DEBUG_CRASH(("you can only apply Locomotors to objects with Physics"));
		return TRUE;
	}

#ifdef DEBUG_OBJECT_ID_EXISTS
//	DEBUG_ASSERTLOG(obj->getID() != TheObjectIDToDebug, ("locoUpdate_maintainCurrentPosition %f %f %f, speed %f (%f)\n",m_maintainPos.x,m_maintainPos.y,m_maintainPos.z,physics->getSpeed(),physics->getForwardSpeed2D()));
#endif

	Bool requiresConstantCalling = TRUE;	// assume the worst.
	switch (m_template->m_appearance) 
	{
		case LOCO_THRUST:
			maintainCurrentPositionThrust(obj, physics);
			requiresConstantCalling = TRUE;
			break;
		case LOCO_LEGS_TWO:
		case LOCO_CLIMBER:
			requiresConstantCalling = maintainCurrentPositionLegs(obj, physics);
			break;
		case LOCO_WHEELS_FOUR:
		case LOCO_MOTORCYCLE:
			if (isGroundVehicle(obj))
			{
				requiresConstantCalling = maintainCurrentPositionVehicle(obj, physics);
				break;
			}
			maintainCurrentPositionWheels(obj, physics);
			requiresConstantCalling = FALSE;
			break;
		case LOCO_TREADS:
			if (isGroundVehicle(obj))
			{
				requiresConstantCalling = maintainCurrentPositionVehicle(obj, physics);
				break;
			}
			maintainCurrentPositionTreads(obj, physics);
			requiresConstantCalling = FALSE;
			break;
		case LOCO_HOVER:
			maintainCurrentPositionHover(obj, physics);
			requiresConstantCalling = TRUE;
			break;
		case LOCO_WINGS:
			maintainCurrentPositionWings(obj, physics);
			requiresConstantCalling = TRUE;
			break;
		case LOCO_OTHER:
		default:
			maintainCurrentPositionOther(obj, physics);
			requiresConstantCalling = TRUE;
			break;
	}

	// but we do need to do this even if not moving, for hovering/Thrusting things.
	if (handleBehaviorZ(obj, physics, m_maintainPos))
		requiresConstantCalling = TRUE;

	return requiresConstantCalling;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::maintainCurrentPositionThrust(Object* obj, PhysicsBehavior *physics)
{
	DEBUG_ASSERTCRASH(getFlag(MAINTAIN_POS_IS_VALID), ("invalid maintain pos"));
	/// @todo srj -- should these also use the "circling radius" stuff, like wings?
	moveTowardsPositionThrust(obj, physics, m_maintainPos, 0, getMinSpeed());
}

//-------------------------------------------------------------------------------------------------
void Locomotor::maintainCurrentPositionWings(Object* obj, PhysicsBehavior *physics)
{
	DEBUG_ASSERTCRASH(getFlag(MAINTAIN_POS_IS_VALID), ("invalid maintain pos"));
	physics->setTurning(TURN_NONE);
	if (physics->isMotive() && obj->isAboveTerrain())	// no need to stop something that isn't moving (or is just sitting on the ground)
	{

			// aim for the spot on the opposite side of the circle.
		BodyDamageType bdt = obj->getBodyModule()->getDamageState();
		Real turnRadius = m_template->m_circlingRadius;
		if (turnRadius == 0.0f)
			turnRadius = calcMinTurnRadius(bdt, NULL);

		// find the direction towards our "maintain pos"
		const Coord3D* pos = obj->getPosition();
		Real dx = m_maintainPos.x - pos->x;
		Real dy = m_maintainPos.y - pos->y;
		Real angleTowardMaintainPos = 
				(isNearlyZero(dx) && isNearlyZero(dy)) ?
				obj->getOrientation() :
				ATan2(dy, dx);

		Real aimDir = (PI - PI/8);
		if (turnRadius < 0)
		{
			turnRadius = -turnRadius;
			aimDir = -aimDir;
		}
		angleTowardMaintainPos += aimDir;

		// project a spot "radius" dist away from it, in that dir
		Coord3D desiredPos = m_maintainPos;
		desiredPos.x += Cos(angleTowardMaintainPos) * turnRadius;
		desiredPos.y += Sin(angleTowardMaintainPos) * turnRadius;
		moveTowardsPositionWings(obj, physics, desiredPos, 0, m_template->m_minSpeed);
	}
}

//-------------------------------------------------------------------------------------------------
void Locomotor::maintainCurrentPositionHover(Object* obj, PhysicsBehavior *physics)
{
	physics->setTurning(TURN_NONE);
	if (isHelicopter(obj))
	{
		physics->setTurning(turnHelicopter(obj, NULL));	// a swing still going winds down rather than stopping dead
		if (physics->isMotive())
			brakeHelicopter(obj, physics);
		return;
	}
	if (physics->isMotive())	// no need to stop something that isn't moving.
	{
		DEBUG_ASSERTCRASH(m_template->m_minSpeed == 0.0f, ("HOVER should always have zero minSpeeds (otherwise, they WING)"));

		BodyDamageType bdt = obj->getBodyModule()->getDamageState();
		Real maxAcceleration = getMaxAcceleration(bdt);
		Real actualSpeed = physics->getForwardSpeed2D();
		//
		// Stop
		//
		Real minSpeed = max( 1.0E-10f, m_template->m_minSpeed ); 
		Real speedDelta = minSpeed - actualSpeed;
		if (fabs(speedDelta) > minSpeed)
		{
			Real mass = physics->getMass();
			Real acceleration = (speedDelta > 0.0f) ? maxAcceleration : -getBraking();
			Real accelForce = mass * acceleration;

			/*
				don't accelerate/brake more than necessary. do a quick calc to 
				see how much force we really need to achieve our goal speed...
			*/
			Real maxForceNeeded = mass * speedDelta;
			if (fabs(accelForce) > fabs(maxForceNeeded))
				accelForce = maxForceNeeded;

			const Coord3D *dir = obj->getUnitDirectionVector2D();

			Coord3D force;
			force.x = accelForce * dir->x;
			force.y = accelForce * dir->y;
			force.z = 0.0f;


      // Apply a random kick (if applicable) to dirty-up visually.
      // The idea is that chopper pilots have to do course corrections all the time
      // Because of changes in wind, pressure, etc.
      // Those changes are added here, then the 



			// apply forces to object
			physics->applyMotiveForce( &force );
		}
	}

}

//-------------------------------------------------------------------------------------------------
/// a soldier with nowhere to go comes to a stop; TRUE while he still moves
Bool Locomotor::maintainCurrentPositionLegs(Object* obj, PhysicsBehavior *physics)
{
	physics->setTurning(TURN_NONE);
	return brakeLegs(obj, physics);
}

//-------------------------------------------------------------------------------------------------
/**
	Take a soldier's walk off at getLegsStopDecel a frame, along whatever line it is on; TRUE while
	some is left. EA zeroed it on the frame of a stop order or an arrival, and the man stood dead
	still in mid-stride.
*/
Bool Locomotor::brakeLegs(Object* obj, PhysicsBehavior *physics)
{
	m_yawRate = 0.0f;	// a turn from here is rotateTowardsPosition's
	if (!physics->isMotive())	// no need to stop something that isn't moving
		return FALSE;
	const Coord3D *vel = physics->getVelocity();
	Real speed = sqrt(vel->x*vel->x + vel->y*vel->y) - getLegsStopDecel(obj);
	physics->scrubVelocity2D(speed > 0.0f ? speed : 0.0f);
	return speed > 0.0f;
}

//-------------------------------------------------------------------------------------------------
/// the hardest a soldier brakes: his Braking, or whatever stops him from top speed in INFANTRY_STOP_FRAMES
Real Locomotor::getLegsStopDecel(const Object* obj) const
{
	Real decel = getMaxSpeedForCondition(obj->getBodyModule()->getDamageState()) / INFANTRY_STOP_FRAMES;
	if (decel < getBraking())
		decel = getBraking();
	return decel;
}

//-------------------------------------------------------------------------------------------------
void Locomotor::maintainCurrentPositionOther(Object* obj, PhysicsBehavior *physics)
{

	physics->setTurning(TURN_NONE);
	if (physics->isMotive())	// no need to stop something that isn't moving.
	{
		physics->scrubVelocity2D(0); // stop.
	}

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
LocomotorSet::LocomotorSet()
{
	m_locomotors.clear();
	m_validLocomotorSurfaces = 0;
	m_downhillOnly = FALSE;

}

//-------------------------------------------------------------------------------------------------
LocomotorSet::LocomotorSet(const LocomotorSet& that)
{
	DEBUG_CRASH(("unimplemented"));
}

//-------------------------------------------------------------------------------------------------
LocomotorSet& LocomotorSet::operator=(const LocomotorSet& that)
{
	if (this != &that)
	{
		DEBUG_CRASH(("unimplemented"));
	}
	return *this;
}

//-------------------------------------------------------------------------------------------------
LocomotorSet::~LocomotorSet()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void LocomotorSet::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void LocomotorSet::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// count of vector
	UnsignedShort count = m_locomotors.size();
	xfer->xferUnsignedShort( &count );

	// data
	if (xfer->getXferMode() == XFER_SAVE)
	{
		for (LocomotorVector::iterator it = m_locomotors.begin(); it != m_locomotors.end(); ++it)
		{
			Locomotor* loco = *it;
			AsciiString name = loco->getTemplateName();
			xfer->xferAsciiString(&name);
			xfer->xferSnapshot(loco);
		}
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		// vector should be empty at this point
		if (m_locomotors.empty() == FALSE)
		{
			DEBUG_CRASH(( "LocomotorSet::xfer - vector is not empty, but should be\n" ));
			throw XFER_LIST_NOT_EMPTY;
		}

		for (UnsignedShort i = 0; i < count; ++i)
		{
			AsciiString name;
			xfer->xferAsciiString(&name);

			const LocomotorTemplate* lt = TheLocomotorStore->findLocomotorTemplate(NAMEKEY(name));
			if (lt == NULL)
			{
				DEBUG_CRASH(( "LocomotorSet::xfer - template %s not found\n", name.str() ));
				throw XFER_UNKNOWN_STRING;
			}

			Locomotor* loco = TheLocomotorStore->newLocomotor(lt);
			xfer->xferSnapshot(loco);
			m_locomotors.push_back(loco);		
		}
	}

	xfer->xferInt(&m_validLocomotorSurfaces);
	xfer->xferBool(&m_downhillOnly);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void LocomotorSet::loadPostProcess( void )
{

}  // end loadPostProcess

//-------------------------------------------------------------------------------------------------
void LocomotorSet::xferSelfAndCurLocoPtr(Xfer *xfer, Locomotor** loco)
{
	xfer->xferSnapshot(this);

	if (xfer->getXferMode() == XFER_SAVE)
	{
		AsciiString name;
		if (*loco)
			name = (*loco)->getTemplateName();
		xfer->xferAsciiString(&name);
	}
	else if (xfer->getXferMode() == XFER_LOAD)
	{
		AsciiString name;
		xfer->xferAsciiString(&name);

		if (name.isEmpty())
		{
			*loco = NULL;
		}
		else
		{
			for (int i = 0; i < m_locomotors.size(); ++i)
			{
				if (m_locomotors[i]->getTemplateName() == name)
				{
					*loco = m_locomotors[i];
					return;
				}
			}

			DEBUG_CRASH(( "LocomotorSet::xfer - template %s not found\n", name.str() ));
			throw XFER_UNKNOWN_STRING;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void LocomotorSet::clear()
{
	for (int i = 0; i < m_locomotors.size(); ++i)
	{
		if (m_locomotors[i])
			m_locomotors[i]->deleteInstance();
	}
	m_locomotors.clear();
	m_validLocomotorSurfaces = 0;
	m_downhillOnly = FALSE;
}

//-------------------------------------------------------------------------------------------------
void LocomotorSet::setSpeedScale(Real scale)
{
	for (int i = 0; i < m_locomotors.size(); ++i)
		m_locomotors[i]->setSpeedScale(scale);
}

//-------------------------------------------------------------------------------------------------
void LocomotorSet::addLocomotor(const LocomotorTemplate* lt)
{
	Locomotor* loco = TheLocomotorStore->newLocomotor(lt);
	if (loco)
	{
		m_locomotors.push_back(loco);
		m_validLocomotorSurfaces |= loco->getLegalSurfaces();
		if (loco->getIsDownhillOnly())
		{
			m_downhillOnly = TRUE;
		}
		else // Previous locos were gravity only, but this one isn't!
		{
			DEBUG_ASSERTCRASH(!m_downhillOnly,("LocomotorSet, YOU CAN NOT MIX DOWNHILL-ONLY LOCOMOTORS WITH NON-DOWNHILL-ONLY ONES."));
		}

	}
}

//-------------------------------------------------------------------------------------------------
Locomotor* LocomotorSet::findLocomotor(LocomotorSurfaceTypeMask t)
{
	for (LocomotorVector::iterator it = m_locomotors.begin(); it != m_locomotors.end(); ++it)
	{
		Locomotor* curLocomotor = *it;
		if (curLocomotor && (curLocomotor->getLegalSurfaces() & t))
			return curLocomotor;
	}
	return NULL;
}


