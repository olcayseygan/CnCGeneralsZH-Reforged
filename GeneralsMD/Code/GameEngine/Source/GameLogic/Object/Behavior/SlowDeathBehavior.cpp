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

// FILE: SlowDeathBehavior.cpp ///////////////////////////////////////////////////////////////////////
// Author:
// Desc:  
///////////////////////////////////////////////////////////////////////////////////////////////////


// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#define DEFINE_SLOWDEATHPHASE_NAMES
#include "Common/GameLOD.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "Common/GlobalData.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SlowDeathBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "Platform/MsvcFloatCasts.h"
#include "GameLogic/Weapon.h"
#include "GameClient/Drawable.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

const Real BEGIN_MIDPOINT_RATIO = 0.35f;
const Real END_MIDPOINT_RATIO = 0.65f;

//-------------------------------------------------------------------------------------------------
SlowDeathBehaviorModuleData::SlowDeathBehaviorModuleData()
{
	m_sinkRate = 0;
	m_probabilityModifier = 10;
	m_modifierBonusPerOverkillPercent = 0;
	m_sinkDelay = 0;
	m_sinkDelayVariance = 0;
	m_destructionDelay = 0;
	m_destructionDelayVariance = 0;
	m_destructionAltitude = -10;
	m_maskOfLoadedEffects = 0; //assume no ocl, fx, or weapons.
	m_flingForce = 0;
	m_flingForceVariance = 0;
	m_flingPitch = 0;
	m_flingPitchVariance = 0;
	m_deathMomentum = TRUE;
	m_momentumMinSpeed = 0.25f;
	m_momentumTossSpeed = ConvertVelocityInSecsToFrames(32.0f);	// a second in the air at full strength
	m_momentumImpulseReach = 0.35f;
	m_momentumPushSpeed = ConvertVelocityInSecsToFrames(20.0f);
	m_momentumTreadPushSpeed = ConvertVelocityInSecsToFrames(10.0f);
	m_momentumTreadSlideTime = 3 * LOGICFRAMES_PER_SECOND;
	m_momentumTreadSlideLength = 1.25f;
	// redundant.
	//m_fx.clear();
	//m_ocls.clear();
	//m_weapons.clear();
}

//-------------------------------------------------------------------------------------------------
static void parseFX( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != NULL; token = ini->getNextTokenOrNull())
	{
		const FXList *fxl = TheFXListStore->findFXList((token));	// could be null! this is OK!
		self->m_fx[sdphase].push_back(fxl);
		if (fxl)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_FX;
	}
}

//-------------------------------------------------------------------------------------------------
static void parseOCL( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != NULL; token = ini->getNextTokenOrNull())
	{
		const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(token);	// could be null! this is OK!
		self->m_ocls[sdphase].push_back(ocl);
		if (ocl)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_OCL;
	}
}

//-------------------------------------------------------------------------------------------------
static void parseWeapon( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ )
{
	SlowDeathBehaviorModuleData* self = (SlowDeathBehaviorModuleData*)instance;
	SlowDeathPhaseType sdphase = (SlowDeathPhaseType)INI::scanIndexList(ini->getNextToken(), TheSlowDeathPhaseNames);
	for (const char* token = ini->getNextToken(); token != NULL; token = ini->getNextTokenOrNull())
	{
		const WeaponTemplate *wt = TheWeaponStore->findWeaponTemplate(token);	// could be null! this is OK!
		self->m_weapons[sdphase].push_back(wt);
		if (wt)
			self->m_maskOfLoadedEffects |= SlowDeathBehaviorModuleData::HAS_WEAPON;
	}
}

//-------------------------------------------------------------------------------------------------
/*static*/ void SlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p) 
{
  UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] = 
	{
		{ "SinkRate",													INI::parseVelocityReal,						NULL, offsetof( SlowDeathBehaviorModuleData, m_sinkRate ) },
		{ "ProbabilityModifier",							INI::parseInt,										NULL, offsetof( SlowDeathBehaviorModuleData, m_probabilityModifier ) },
		{ "ModifierBonusPerOverkillPercent",	INI::parsePercentToReal,					NULL, offsetof( SlowDeathBehaviorModuleData, m_modifierBonusPerOverkillPercent ) },
		{ "SinkDelay",												INI::parseDurationUnsignedInt,		NULL, offsetof( SlowDeathBehaviorModuleData, m_sinkDelay ) },
		{ "SinkDelayVariance",								INI::parseDurationUnsignedInt,		NULL, offsetof( SlowDeathBehaviorModuleData, m_sinkDelayVariance ) },
		{ "DestructionDelay",									INI::parseDurationUnsignedInt,		NULL, offsetof( SlowDeathBehaviorModuleData, m_destructionDelay ) },
		{ "DestructionDelayVariance",					INI::parseDurationUnsignedInt,		NULL, offsetof( SlowDeathBehaviorModuleData, m_destructionDelayVariance ) },
		{ "DestructionAltitude",							INI::parseReal,										NULL, offsetof( SlowDeathBehaviorModuleData, m_destructionAltitude ) },
		{ "FX",																parseFX,													NULL, 0 },
		{ "OCL",															parseOCL,													NULL, 0 },
		{ "Weapon",														parseWeapon,											NULL, 0 },
		{ "FlingForce",												INI::parseRetailPerFrameReal,			NULL, offsetof( SlowDeathBehaviorModuleData, m_flingForce) },
		{ "FlingForceVariance",								INI::parseRetailPerFrameReal,									NULL, offsetof( SlowDeathBehaviorModuleData, m_flingForceVariance) },
		{ "FlingPitch",												INI::parseAngleReal,							NULL, offsetof( SlowDeathBehaviorModuleData, m_flingPitch) },
		{ "FlingPitchVariance",								INI::parseAngleReal,							NULL, offsetof( SlowDeathBehaviorModuleData, m_flingPitchVariance) },
		{ "DeathMomentum",										INI::parseBool,										NULL, offsetof( SlowDeathBehaviorModuleData, m_deathMomentum) },
		{ "DeathMomentumMinSpeed",						INI::parsePercentToReal,					NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumMinSpeed) },
		{ "DeathMomentumTossSpeed",						INI::parseVelocityReal,						NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumTossSpeed) },
		{ "DeathMomentumImpulseReach",				INI::parsePercentToReal,					NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumImpulseReach) },
		{ "DeathMomentumPushSpeed",						INI::parseVelocityReal,						NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumPushSpeed) },
		{ "DeathMomentumTreadPushSpeed",			INI::parseVelocityReal,						NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumTreadPushSpeed) },
		{ "DeathMomentumTreadSlideTime",			INI::parseDurationUnsignedInt,		NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumTreadSlideTime) },
		{ "DeathMomentumTreadSlideLength",		INI::parsePercentToReal,					NULL, offsetof( SlowDeathBehaviorModuleData, m_momentumTreadSlideLength) },
		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
	p.add(DieMuxData::getFieldParse(), offsetof( SlowDeathBehaviorModuleData, m_dieMuxData ));
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SlowDeathBehavior::SlowDeathBehavior( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_flags = 0;
	m_sinkFrame = 0;
	m_midpointFrame = 0;
	m_destructionFrame = 0;
	m_acceleratedTimeScale = 1.0f;
	m_momentumVelX = 0.0f;
	m_momentumVelY = 0.0f;
	m_momentumHopSpeed = 0.0f;
	m_momentumStartFrame = 0;
	m_momentumEndFrame = 0;
	m_momentumDeadlineFrame = 0;
	m_deferredOCL = -1;

	if (getSlowDeathBehaviorModuleData()->m_probabilityModifier < 1)
	{
		DEBUG_CRASH(("ProbabilityModifer must be >= 1.\n"));
		throw INI_INVALID_DATA;
	}

	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SlowDeathBehavior::~SlowDeathBehavior( void )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Int SlowDeathBehavior::getProbabilityModifier( const DamageInfo *damageInfo ) const
{
	// Calculating how far past dead we were allows us to pick more spectacular deaths when
	// severly killed, and more sedate ones when only slightly killed.
	// eg ( 200 hp max, had 10 left, took 50 damage, 40 overkill, (40/200) * 100 = 20 overkill %)
	Int overkillDamage = damageInfo->out.m_actualDamageDealt - damageInfo->out.m_actualDamageClipped;
	Real overkillPercent = (float)overkillDamage / (float)getObject()->getBodyModule()->getMaxHealth();
	// An object with no health to lose (an InactiveBody: debris, hulks) makes this 0/0.  Windows converts
	// the NaN to INT_MIN, so the modifier comes out 1; the helper gives that answer everywhere.
	Int overkillModifier = floatToIntAsMsvc( overkillPercent * getSlowDeathBehaviorModuleData()->m_modifierBonusPerOverkillPercent );

	return max( getSlowDeathBehaviorModuleData()->m_probabilityModifier + overkillModifier, 1 );
}

//-------------------------------------------------------------------------------------------------
static void calcRandomForce(Real minMag, Real maxMag, Real minPitch, Real maxPitch, Coord3D& force)
{
	Real angle = GameLogicRandomValueReal(-PI, PI);
	Real pitch = GameLogicRandomValueReal(minPitch, maxPitch);
	Real mag = GameLogicRandomValueReal(minMag, maxMag);

	Matrix3D mtx(1);
	mtx.Scale(mag);
	mtx.Rotate_Z(angle);
	mtx.Rotate_Y(-pitch);

	Vector3 v = mtx.Get_X_Vector();

	force.x = v.X;
	force.y = v.Y;
	force.z = v.Z;
}

//-------------------------------------------------------------------------------------------------
/** Height over whatever it stands on, a bridge included: physics lands it on that layer, and the
	* terrain under a bridge would keep a wreck on it "in the air" for good. */
static Real heightAboveFloor(const Object* obj)
{
	const Coord3D* pos = obj->getPosition();
	return pos->z - TheTerrainLogic->getLayerHeight(pos->x, pos->y, obj->getLayer());
}

//-------------------------------------------------------------------------------------------------
/** How far the lowest corner of the body reaches below its position, turned as it is now.  The
	* position is the base, so a car tumbling nose down has its bonnet in the ground well before
	* the base comes down to it. */
static Real bodyReachBelow(const Object* obj)
{
	const Matrix3D& m = *obj->getTransformMatrix();
	const GeometryInfo& geom = obj->getGeometryInfo();
	const Real halfLength = geom.getMajorRadius();
	const Real halfWidth = (geom.getGeomType() == GEOMETRY_BOX) ? geom.getMinorRadius() : halfLength;
	const Real height = geom.getMaxHeightAbovePosition();
	// the world z of a body point (x, y, z) is m[2][0]x + m[2][1]y + m[2][2]z
	const Real lowest = -fabs(m[2][0]) * halfLength - fabs(m[2][1]) * halfWidth + min(0.0f, m[2][2] * height);
	return -lowest;
}

const Real MOMENTUM_AIRBORNE_HEIGHT = 0.5f;																	///< above this a wreck is in the air: its slide waits, physics flies it
const Real MOMENTUM_LIFTOFF_HEIGHT = 0.05f;																	///< above this a slow car's low hop has left the ground
const UnsignedInt MOMENTUM_LANDING_GRACE = 3 * LOGICFRAMES_PER_SECOND;			///< on top of the slide and the hop, the most it may hold the destruction back
const Real MOMENTUM_BOUNCINESS = -0.4f;																			///< off the ground's stiffness: it lands heavily, two short bounces
const Real MOMENTUM_FRICTION_KEEP = 0.92f;																	///< share of the speed set on it that ground friction leaves to move on each frame

//-------------------------------------------------------------------------------------------------
/** True if some OCL of the module, in any phase, throws the dying body itself (the Technical's
	* ApplyRandomForce): the data already gave that death its flight, and it ends in the air.  For a
	* wheeled toss, also one that launches a whole-vehicle copy hard (the Rocket Buggy's and the
	* Combat Bike's FINAL): tossed by us first, it would be thrown a second time when it lands. A
	* tank's slide ending in a turret or barrel popping off is fine. */
static Bool dataFlingsBody(const SlowDeathBehaviorModuleData* d, Bool wheels)
{
	for (Int phase = 0; phase < SD_PHASE_COUNT; ++phase)
	{
		const OCLVec& v = d->m_ocls[phase];
		for (OCLVec::const_iterator i = v.begin(); i != v.end(); ++i)
		{
			if (*i != NULL && ((*i)->movesPrimary() || (wheels && (*i)->launchesBody())))
				return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** The level direction the killing shot pushes the body, away from where it came from: the
	* weapon's shockwave vector if it has one, else from the source object to us.  Only for damage
	* that hits like a blow - shells, bullets, missiles, bombs, mines, beams; poison, radiation, fire,
	* microwaves and the rest push nothing, and neither does a source that is gone. */
/*static*/ Bool SlowDeathBehavior::calcHitDirection(const Object* obj, const DamageInfo* damageInfo, Real& dirX, Real& dirY)
{
	if (damageInfo == NULL)
		return FALSE;
	switch (damageInfo->in.m_damageType)
	{
		case DAMAGE_EXPLOSION:
		case DAMAGE_ARMOR_PIERCING:
		case DAMAGE_SMALL_ARMS:
		case DAMAGE_GATTLING:
		case DAMAGE_SNIPER:
		case DAMAGE_MELEE:
		case DAMAGE_PARTICLE_BEAM:
		case DAMAGE_INFANTRY_MISSILE:
		case DAMAGE_AURORA_BOMB:
		case DAMAGE_LAND_MINE:
		case DAMAGE_JET_MISSILES:
		case DAMAGE_STEALTHJET_MISSILES:
		case DAMAGE_COMANCHE_VULCAN:
			break;
		default:
			return FALSE;
	}

	Real dx;
	Real dy;
	const Coord3D& shock = damageInfo->in.m_shockWaveVector;
	if (shock.x != 0.0f || shock.y != 0.0f)
	{
		dx = shock.x;
		dy = shock.y;
	}
	else
	{
		const Object* source = TheGameLogic->findObjectByID(damageInfo->in.m_sourceID);
		if (source == NULL || source == obj)
			return FALSE;
		dx = obj->getPosition()->x - source->getPosition()->x;
		dy = obj->getPosition()->y - source->getPosition()->y;
	}
	const Real len = sqrtf(dx*dx + dy*dy);
	if (len < 0.01f)
		return FALSE;
	dirX = dx / len;
	dirY = dy / len;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** A wreck coming down raised the same small puff whether it dropped off a hover or went in at full
	* speed. Three lists in FXListReforged.ini, picked by the speed it hits the ground at: a hovering
	* helicopter falling off its lift lands light (about 40 a second), a car tossed at speed or a slow
	* jet medium, a helicopter or a jet shot down at speed heavy. Effects only; the client plays them. */
/*static*/ void SlowDeathBehavior::doImpactFX(const Object* obj, Real impactSpeed)
{
	const Real MEDIUM_IMPACT = ConvertVelocityInSecsToFrames(50.0f);
	const Real HEAVY_IMPACT = ConvertVelocityInSecsToFrames(110.0f);
	const char* name = "FX_Reforged_ImpactLight";
	if (impactSpeed >= HEAVY_IMPACT)
		name = "FX_Reforged_ImpactHeavy";
	else if (impactSpeed >= MEDIUM_IMPACT)
		name = "FX_Reforged_ImpactMedium";
	FXList::doFXObj(TheFXListStore->findFXList(name), obj);
}

//-------------------------------------------------------------------------------------------------
/** A ground vehicle killed on the move used to stop where it was hit: dead, nothing drives it, and
	* ground friction takes a car from full speed to nothing in a third of a second.  It carries on
	* now, and the shot that killed it pushes it on top of that, away from where it came from.  A
	* tracked wreck slides on that way, braking evenly to a halt; a wheeled one is tossed up the
	* way the Technical's own death throws it, tumbling over the way it is going - hit from behind
	* it flips nose over tail, hit from the side it rolls away from the shot - and blows the moment
	* it comes down.  The destruction waits for it, and so does the INITIAL OCL when that is only
	* the burnt hull or debris, which would otherwise be left lying where it was hit.  A parked one
	* only gets the push: a tank shoved a little, a car hopped and tipped away from the blast.  With
	* no push to go on either - a parked one killed by poison, by fire, by a script - it dies as it
	* always did. Aircraft, soldiers and deaths the data already flings keep theirs. */
Bool SlowDeathBehavior::beginMomentum(const DamageInfo* damageInfo)
{
	const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
	Object* obj = getObject();
	PhysicsBehavior* physics = obj->getPhysics();
	AIUpdateInterface* ai = obj->getAI();
	Locomotor* locomotor = ai ? ai->getCurLocomotor() : NULL;
	if (physics == NULL || locomotor == NULL)
		return FALSE;
	if (!obj->isKindOf(KINDOF_VEHICLE) || obj->isKindOf(KINDOF_AIRCRAFT) ||
			obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_STRUCTURE))
		return FALSE;
	if (obj->getContainedBy() != NULL || obj->isDisabledByType(DISABLED_HELD) || obj->isSignificantlyAboveTerrain())
		return FALSE;

	LocomotorAppearance appearance = locomotor->getAppearance();
	Bool wheels = (appearance == LOCO_WHEELS_FOUR || appearance == LOCO_MOTORCYCLE);
	if ((!wheels && appearance != LOCO_TREADS) || dataFlingsBody(d, wheels))
		return FALSE;
	const Coord3D* vel = physics->getVelocity();
	Real speed = sqrtf(vel->x*vel->x + vel->y*vel->y);
	Real topSpeed = locomotor->getMaxSpeedForCondition(BODY_PRISTINE);
	if (topSpeed <= 0.0f)
		return FALSE;
	// EA's treads and wheels cross the map diagonally at up to 1.41 times their Speed
	Real share = min(speed / topSpeed, 1.0f);
	Real hitX = 0.0f;
	Real hitY = 0.0f;
	const Bool hit = calcHitDirection(obj, damageInfo, hitX, hitY);
	if (share < d->m_momentumMinSpeed)
	{
		// parked: only the shot moves it
		if (!hit)
			return FALSE;
		share = 0.0f;
	}

	/* A tank's slide is set by the distance it covers: braking evenly from top speed it runs on a
		 share of its own hull length, whatever that speed is, so a quick Scorpion and a slow Crusader
		 both read as a heavy hull skidding.  A slow giant would take an age over it, so its time has a
		 ceiling, and it stops short instead. */
	const Real gravity = -TheGlobalData->m_gravity;
	UnsignedInt now = TheGameLogic->getFrame();
	m_momentumHopSpeed = 0.0f;
	m_momentumStartFrame = now;

	// a car with no shot to throw it - poison, fire, a script - rolls on and slides to a stop as a tank does
	if (!wheels || !hit)
	{
		// its own speed and a modest shove away from the shot; the slide is set by the two together
		const Real push = hit ? d->m_momentumTreadPushSpeed : 0.0f;
		const Real vx = vel->x + hitX * push;
		const Real vy = vel->y + hitY * push;
		const Real slideShare = min(sqrtf(vx*vx + vy*vy) / topSpeed, 1.0f);
		if (!(slideShare > 0.0f))
			return FALSE;
		const Real hullLength = 2.0f * obj->getGeometryInfo().getMajorRadius();
		// clamped as a float: casting a negative or enormous one is undefined, and x64 and ARM64 differ
		Real fullFrames = 2.0f * d->m_momentumTreadSlideLength * hullLength / (topSpeed * MOMENTUM_FRICTION_KEEP);
		if (!(fullFrames >= 1.0f))
			return FALSE;
		if (fullFrames > (Real)d->m_momentumTreadSlideTime)
			fullFrames = (Real)d->m_momentumTreadSlideTime;
		const UnsignedInt slideTime = (UnsignedInt)fullFrames;
		if (slideTime == 0)
			return FALSE;
		m_momentumEndFrame = now + max((UnsignedInt)1, (UnsignedInt)(slideTime * slideShare));
		m_momentumDeadlineFrame = m_momentumEndFrame + MOMENTUM_LANDING_GRACE;
		m_momentumVelX = vx;
		m_momentumVelY = vy;
		m_flags |= (1<<MOMENTUM_ACTIVE) | (1<<MOMENTUM_SLIDING);
		// physics does not move a unit across the ground while it brakes for its stop, and nothing will
		// drive the wreck again to clear it
		obj->clearStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_BRAKING));
		// a slide off a ledge onto a roof still ends in its hull and blast, not the crash weapon
		physics->setNoStructureCrash(TRUE);
		Coord3D shove;
		shove.x = vx - vel->x;
		shove.y = vy - vel->y;
		shove.z = 0.0f;
		physics->addVelocityTo(&shove);
		return TRUE;
	}

	/* A wheeled one hit by a shot is thrown, and flown by physics from there: its own speed plus what
		 the shot gives it, gravity, and nothing steering it. It blows when it comes down. The
		 Technical's own death throw (OCL_TechnicalAirDeathStart) kicks it up 45 to 75 a second and
		 spins it at random, nearly two seconds in the air; a toss built the same way read as staged
		 and floaty, so this one is worked out from the hit instead. */
	if (gravity <= 0.0f || d->m_momentumTossSpeed <= 0.0f)
		return FALSE;

	/* The shot is an impulse J on a rigid box. Its strength goes with the share of top speed the car
		 died at (a parked one hit by a shot gets a third of it), the spread on it is a tenth either
		 way, and it is set for a body of Mass 50, so a lighter one goes further and a heavier one less
		 (held between half and one and a half). At full strength a Mass 50 car gets 32 a second up,
		 about a second in the air, and the push away from the shot level. */
	const Real PARKED_STRENGTH = 0.35f;
	const Real REFERENCE_MASS = 50.0f;
	const Real mass = physics->getMass();
	const Real massScale = (mass > 0.0f) ? min(max(REFERENCE_MASS / mass, 0.5f), 1.5f) : 1.0f;
	const Real strength = max(share, PARKED_STRENGTH) * massScale * GameLogicRandomValueReal(0.9f, 1.1f);
	const Real push = d->m_momentumPushSpeed * strength;					// level part of J/m, along the hit
	const Real tossSpeed = d->m_momentumTossSpeed * strength;			// vertical part of J/m
	const Real vx = vel->x + hitX * push;
	const Real vy = vel->y + hitY * push;
	// clamped as a float before the cast, as the slide above is; a minute in the air is past any data
	const Real MAX_AIR_FRAMES = 60.0f * LOGICFRAMES_PER_SECOND;
	const Real airFrames = min(max(2.0f * tossSpeed / gravity, 1.0f), MAX_AIR_FRAMES);
	m_momentumVelX = vx;
	m_momentumVelY = vy;
	m_momentumHopSpeed = tossSpeed;
	m_momentumEndFrame = now + (UnsignedInt)airFrames;
	m_momentumDeadlineFrame = m_momentumEndFrame + MOMENTUM_LANDING_GRACE;
	m_flags |= (1<<MOMENTUM_ACTIVE) | (1<<MOMENTUM_TUMBLING);
	obj->clearStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_BRAKING));

	Coord3D toss;
	toss.x = vx - vel->x;
	toss.y = vy - vel->y;
	toss.z = tossSpeed;
	physics->setAllowToFall(TRUE);
	physics->setAllowBouncing(TRUE);
	physics->setExtraBounciness(MOMENTUM_BOUNCINESS);
	physics->setNoStructureCrash(TRUE);
	physics->addVelocityTo(&toss);

	/* The spin is what that impulse does to a solid box, nothing more: dw = I^-1 (r x J), with the box
		 GeometryMajorRadius a long, MinorRadius b wide and Height h tall, in the body's own axes (x the
		 nose, y the left side, z up; the car is upright when it dies).  I is a solid box's about its
		 centre: Ixx = m((2b)^2 + h^2)/12, Iyy = m((2a)^2 + h^2)/12, Izz = m((2a)^2 + (2b)^2)/12.  J acts
		 on the face towards the shot at a quarter of the height, low on the hull, a tenth of the
		 height either way. A blast spreads over that face, so the point its push comes through sits
		 inward of the edge, ImpulseReach (35%) of the way from the centre.  Driving speed adds no turn:
		 no torque.  Hit from behind at the lower rear, the lift under the tail beats the push below the
		 centre and the nose dips; from the side the near side rises and it leans away; head on it rears
		 back.  Physics doubles a rate before turning the body (PitchRollYawFactor), so it is halved
		 here. */
	const GeometryInfo& geom = obj->getGeometryInfo();
	const Real halfLength = geom.getMajorRadius();
	const Real halfWidth = (geom.getGeomType() == GEOMETRY_BOX) ? geom.getMinorRadius() : halfLength;
	const Real height = geom.getMaxHeightAbovePosition();
	const Coord3D* nose = obj->getUnitDirectionVector2D();
	const Real hitFwd = hitX * nose->x + hitY * nose->y;			// push direction in body axes
	const Real hitLeft = hitX * -nose->y + hitY * nose->x;
	// the point on the face towards the shot, which is behind the push direction
	Real reach = 1.0e9f;
	if (fabs(hitFwd) > 0.001f)
		reach = halfLength / fabs(hitFwd);
	if (fabs(hitLeft) > 0.001f)
		reach = min(reach, halfWidth / fabs(hitLeft));
	reach *= d->m_momentumImpulseReach;
	const Real rx = -hitFwd * reach;
	const Real ry = -hitLeft * reach;
	const Real rz = -height * 0.25f + height * GameLogicRandomValueReal(-0.1f, 0.1f);	// from the centre, h/2 up
	const Real jx = hitFwd * push;		// J/m in body axes; the mass cancels against I
	const Real jy = hitLeft * push;
	const Real jz = tossSpeed;
	const Real ixx = ((2.0f * halfWidth) * (2.0f * halfWidth) + height * height) / 12.0f;
	const Real iyy = ((2.0f * halfLength) * (2.0f * halfLength) + height * height) / 12.0f;
	const Real izz = ((2.0f * halfLength) * (2.0f * halfLength) + (2.0f * halfWidth) * (2.0f * halfWidth)) / 12.0f;
	const Real factor = physics->getPitchRollYawFactor();
	if (factor > 0.0f && ixx > 0.0f && iyy > 0.0f && izz > 0.0f)
	{
		// r x J, then each part over its moment of inertia
		const Real wx = (ry * jz - rz * jy) / ixx;
		const Real wy = (rz * jx - rx * jz) / iyy;
		const Real wz = (rx * jy - ry * jx) / izz;
		physics->setRollRate(wx / factor);
		physics->setPitchRate(wy / factor);
		physics->setYawRate(wz / factor);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** It has come to rest, or something else has taken it over: the destruction it held back can go
	* ahead, and the INITIAL OCL it held back lands where the wreck is now. */
void SlowDeathBehavior::endMomentum(Bool stopSpin)
{
	// landed, held or out of time, the spin it was given stops with it; thrown by something else (a
	// shockwave, which set a spin of its own), it keeps what it has
	PhysicsBehavior* physics = getObject()->getPhysics();
	if (stopSpin && (m_flags & (1<<MOMENTUM_TUMBLING)) != 0 && physics != NULL)
	{
		physics->setYawRate(0.0f);
		physics->setRollRate(0.0f);
		physics->setPitchRate(0.0f);
	}
	m_flags &= ~((1<<MOMENTUM_ACTIVE) | (1<<MOMENTUM_SLIDING) | (1<<MOMENTUM_TUMBLING));

	const OCLVec& v = getSlowDeathBehaviorModuleData()->m_ocls[SDPHASE_INITIAL];
	if (m_deferredOCL >= 0 && m_deferredOCL < (Int)v.size())
		ObjectCreationList::create(v[m_deferredOCL], getObject(), NULL);
	m_deferredOCL = -1;
}

//-------------------------------------------------------------------------------------------------
/** Runs the wreck's ground speed down along its profile.  Physics would stop it in a third of a
	* second on its own, so the velocity is set back on the profile after each physics step.  In the
	* air the profile waits and physics flies the car on whatever it has; setting the ground speed
	* there stopped a car dead in mid-air at the end of its slide, which is what looked like hanging.
	* Anything that stops it harder than friction does - running into a building nukes the velocity -
	* ends the slide there, and anything that threw it on the frame it died (a shockwave) takes the
	* death over entirely. */
Bool SlowDeathBehavior::updateMomentum()
{
	if ((m_flags & (1<<MOMENTUM_ACTIVE)) == 0)
		return FALSE;

	Object* obj = getObject();
	PhysicsBehavior* physics = obj->getPhysics();
	UnsignedInt now = TheGameLogic->getFrame();
	if (physics == NULL || obj->isDisabledByType(DISABLED_HELD) || now >= m_momentumDeadlineFrame)
	{
		endMomentum();
		return FALSE;
	}

	const Coord3D* vel = physics->getVelocity();

	/* The first frame physics has run since the death should show the speed it died at and the hop it
		 was given. More than that, by a retail frame's worth of a unit a frame, is a throw from
		 elsewhere - a shockwave on the same shot - and that flight is the death: no slide, no hold. */
	if ((m_flags & (1<<MOMENTUM_CHECKED)) == 0 && now > m_momentumStartFrame)
	{
		m_flags |= (1<<MOMENTUM_CHECKED);
		const Real dx = vel->x - m_momentumVelX;
		const Real dy = vel->y - m_momentumVelY;
		// only an upward excess: a car dying downhill already carries a downward speed of its own
		const Real dz = max(vel->z - m_momentumHopSpeed, 0.0f);
		const Real THROWN = ConvertRetailPerFrameRate(1.0f);
		if (dx*dx + dy*dy + dz*dz > THROWN*THROWN)
		{
			endMomentum(FALSE);
			return FALSE;
		}
	}

	const Bool airborne = heightAboveFloor(obj) > MOMENTUM_AIRBORNE_HEIGHT;

	/* The toss ends the frame the body first touches the ground coming down - any corner of it,
		 since a car tumbling nose down has its bonnet in the dirt while its base is still up in the
		 air - and the death ends there with it: blast, hull and all on that one frame, before it can
		 dig in and roll about on the ground.  A slow car's toss can be lower than the airborne height
		 above, so leaving the ground is watched against a floor of its own; one that never got off at
		 all ends when it should have landed. */
	if ((m_flags & (1<<MOMENTUM_TUMBLING)) != 0 && now > m_momentumStartFrame)
	{
		const Real height = heightAboveFloor(obj);
		const Bool leftGround = (m_flags & (1<<MOMENTUM_LEFT_GROUND)) != 0;
		const Bool bodyDown = leftGround && vel->z <= 0.0f && height - bodyReachBelow(obj) <= MOMENTUM_LIFTOFF_HEIGHT;
		if (bodyDown || (!leftGround && height <= MOMENTUM_LIFTOFF_HEIGHT && now >= m_momentumEndFrame))
		{
			// the base can still be up where the bonnet hit: the hull and the blast go on the ground under it
			// and righted on its heading, as a bounce would: one that blows later sits on its wheels
			if (height > 0.0f)
			{
				Coord3D pos = *obj->getPosition();
				pos.z -= height;
				obj->setPosition(&pos);
			}
			// the nose gives the heading, unless it points nearly straight up or down mid-flip: then the
			// way it was flying does
			const Matrix3D& m = *obj->getTransformMatrix();
			Real heading = m.Get_Z_Rotation();
			if (fabs(m[2][0]) > 0.95f && (m_momentumVelX != 0.0f || m_momentumVelY != 0.0f))
				heading = ATan2(m_momentumVelY, m_momentumVelX);
			physics->setAngles(heading, 0.0f, 0.0f);
			doImpactFX(obj, sqrtf(vel->x*vel->x + vel->y*vel->y + vel->z*vel->z));
			endMomentum();
			return FALSE;
		}
		if (height > MOMENTUM_LIFTOFF_HEIGHT)
			m_flags |= (1<<MOMENTUM_LEFT_GROUND);
	}

	// in the air the slide waits for it to come down
	if ((m_flags & (1<<MOMENTUM_SLIDING)) != 0 && airborne)
	{
		++m_momentumStartFrame;
		++m_momentumEndFrame;
	}
	else if ((m_flags & (1<<MOMENTUM_SLIDING)) != 0)
	{
		const Real span = (Real)(m_momentumEndFrame - m_momentumStartFrame);
		const Real startSpeed = sqrtf(m_momentumVelX*m_momentumVelX + m_momentumVelY*m_momentumVelY);
		const Real speed = sqrtf(vel->x*vel->x + vel->y*vel->y);

		// what it was set to last frame, which friction alone takes no more than a tenth off
		Real prevU = (now > m_momentumStartFrame) ? (Real)(now - 1 - m_momentumStartFrame) / span : 0.0f;
		prevU = min(prevU, 1.0f);
		const Real expected = startSpeed * (1.0f - prevU);
		const Real BLOCKED_SHARE = 0.5f;
		const Real MIN_EXPECTED = ConvertRetailPerFrameRate(0.1f);

		if (now >= m_momentumEndFrame)
		{
			physics->scrubVelocity2D(0.0f);
			m_flags &= ~(1<<MOMENTUM_SLIDING);
		}
		else if (expected > MIN_EXPECTED && speed < expected * BLOCKED_SHARE)
		{
			m_flags &= ~(1<<MOMENTUM_SLIDING);
		}
		else
		{
			// braking evenly: a steady loss of speed reads as a skid, and leaves no crawl at the end
			const Real share = 1.0f - (Real)(now - m_momentumStartFrame) / span;
			Coord3D delta;
			delta.x = m_momentumVelX * share - vel->x;
			delta.y = m_momentumVelY * share - vel->y;
			delta.z = 0.0f;
			physics->addVelocityTo(&delta);
		}
	}

	if ((m_flags & ((1<<MOMENTUM_SLIDING) | (1<<MOMENTUM_TUMBLING))) != 0 || airborne)
		return TRUE;

	// come to rest
	endMomentum();
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::beginSlowDeath(const DamageInfo *damageInfo)
{
	if (!isSlowDeathActivated())
	{
		const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
		Object* obj = getObject();

		if (d->m_sinkRate && obj->isKindOf(KINDOF_INFANTRY))
		{

			Drawable *draw = getObject()->getDrawable();
			if ( draw )
			{
				// this object sinks slowly after it dies so don't draw a
				// floating shadow decal on the ground above it.
				obj->getDrawable()->setShadowsEnabled(false);
				draw->setTerrainDecalFadeTarget( 0.0f, ConvertRetailPerFrameRate(-0.2f) );
			}



		}

		
		// Ask game detail manager if we need to speedup all deaths to improve performance
		Real timeScale = TheGameLODManager->getSlowDeathScale();
		m_acceleratedTimeScale = 1.0f;	// assume normal death speed.

		if (timeScale == 0.0f && !d->hasNonLodEffects())
		{	
			// Deaths happen instantly so just delete the object and return
			TheGameLogic->destroyObject(obj);
			return;
		}
		else
		{	
			// timescale is some non-zero value so we may need to speed up death
			if( getObject()->isKindOf( KINDOF_HULK ) && TheGameLogic->getHulkMaxLifetimeOverride() != -1 )
			{
				//Scripts don't want hulks around, so start sinking immediately!
				m_sinkFrame = 1;
				m_midpointFrame = (LOGICFRAMES_PER_SECOND/2) + 1;
				m_destructionFrame = LOGICFRAMES_PER_SECOND + 1;
				m_acceleratedTimeScale = 1.0f;
			}
			else
			{
				m_sinkFrame = timeScale * (d->m_sinkDelay + GameLogicRandomValue(0, d->m_sinkDelayVariance));
				m_destructionFrame = timeScale * (d->m_destructionDelay + GameLogicRandomValue(0, d->m_destructionDelayVariance));
				m_midpointFrame = GameLogicRandomValue( BEGIN_MIDPOINT_RATIO * m_destructionFrame, END_MIDPOINT_RATIO * m_destructionFrame );
				m_acceleratedTimeScale = timeScale;
			}
		}

		UnsignedInt now = TheGameLogic->getFrame();

		if (d->m_flingForce > 0)
		{

			
			//Just in case this is a stingersoldier or other HELD object, lets set them free so they will fly
			// with their own physics during slow death
			if( obj->isDisabledByType( DISABLED_HELD ) )
			{
				static NameKeyType key_SlavedUpdate = NAMEKEY( "SlavedUpdate" );
				SlavedUpdate* slave = (SlavedUpdate*)obj->findUpdateModule( key_SlavedUpdate );
				if( slave )
				{
					slave->onSlaverDie( NULL );
				}
			}				

			PhysicsBehavior* physics = obj->getPhysics();
			if (physics)
			{
				// make sure we are at least a bit above the ground
				const Real MIN_ALTITUDE = 1.0f;
				Real altitude = obj->getHeightAboveTerrain();
				if (altitude < MIN_ALTITUDE)
				{
					Coord3D pos = *obj->getPosition();
					pos.z += MIN_ALTITUDE;
					obj->setPosition(&pos);
				}

				Coord3D force;
				calcRandomForce(d->m_flingForce, d->m_flingForce + d->m_flingForceVariance, 
												d->m_flingPitch, d->m_flingPitch + d->m_flingPitchVariance, force);
				physics->setAllowToFall(true);
				physics->applyForce(&force);
				physics->setExtraBounciness(-1.0);					// we don't want this guy to bounce at all
				physics->setExtraFriction(-3.0f / RETAIL_LOGICFRAMES_PER_SECOND);							// reduce his ground friction a bit
				physics->setAllowBouncing(true);
				Real orientation = ATan2(force.y, force.x);
				physics->setAngles(orientation, 0, 0);
				obj->getDrawable()->setModelConditionState(MODELCONDITION_EXPLODED_FLAILING);
				m_flags |= (1<<FLUNG_INTO_AIR);
			}
			setWakeFrame(obj, UPDATE_SLEEP_NONE);
		}
		else if (d->m_deathMomentum && beginMomentum(damageInfo))
		{
			// it carries on from the speed it died at, which wants looking after every frame
			setWakeFrame(obj, UPDATE_SLEEP_NONE);
		}
		else
		{
			// we don't need to wake up immediately, but only when the first of these
			// counters wants to trigger....
			Int whenToWakeTime = m_sinkFrame;
			if (whenToWakeTime > m_destructionFrame) 
				whenToWakeTime = m_destructionFrame;
			if (whenToWakeTime > m_midpointFrame) 
				whenToWakeTime = m_midpointFrame;
			setWakeFrame(obj, UPDATE_SLEEP(whenToWakeTime));
		}
		m_sinkFrame += now;
		m_destructionFrame += now;
		m_midpointFrame += now;

		m_flags |= (1<<SLOW_DEATH_ACTIVATED);

		if ((m_flags & (1<<MOMENTUM_ACTIVE)) != 0)
		{
			/* The fire and the blast go off where it was hit. So does the OCL, unless all it leaves is
				 wreckage - the burnt hull, debris - which waits for where the wreck stops; a soldier thrown
				 clear, a weapon or anything carrying the car's speed still goes off here and now. Drawn in
				 doPhaseStuff's order, fx, OCL, weapon, so the random stream runs as it always did. */
			doPhaseStuff(SDPHASE_INITIAL, SlowDeathBehaviorModuleData::HAS_FX);
			const OCLVec& ocls = d->m_ocls[SDPHASE_INITIAL];
			if (d->m_maskOfLoadedEffects && !ocls.empty())
			{
				const Int idx = GameLogicRandomValue(0, ocls.size() - 1);
				if (ocls[idx] != NULL && ocls[idx]->isWreckageOnly())
					m_deferredOCL = idx;
				else
					ObjectCreationList::create(ocls[idx], obj, NULL);
			}
			doPhaseStuff(SDPHASE_INITIAL, SlowDeathBehaviorModuleData::HAS_WEAPON);
		}
		else
		{
			doPhaseStuff(SDPHASE_INITIAL);
		}

	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::doPhaseStuff(SlowDeathPhaseType sdphase, Int what)
{
	const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
	Int idx, listSize;

	if (!d->m_maskOfLoadedEffects)
		return;	//has no ocl, fx, or weapons.

	listSize = (what & SlowDeathBehaviorModuleData::HAS_FX) ? d->m_fx[sdphase].size() : 0;
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const FXListVec& v = d->m_fx[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const FXList* fxl = v[idx];
		FXList::doFXObj(fxl, getObject(), NULL);
	}

	listSize = (what & SlowDeathBehaviorModuleData::HAS_OCL) ? d->m_ocls[sdphase].size() : 0;
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const OCLVec& v = d->m_ocls[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const ObjectCreationList* ocl = v[idx];
		ObjectCreationList::create(ocl, getObject(), NULL);
	}

	listSize = (what & SlowDeathBehaviorModuleData::HAS_WEAPON) ? d->m_weapons[sdphase].size() : 0;
	if (listSize > 0)
	{
		idx = GameLogicRandomValue(0, listSize-1);
		const WeaponTemplateVec& v = d->m_weapons[sdphase];
		DEBUG_ASSERTCRASH(idx>=0&&idx<v.size(),("bad idx"));
		const WeaponTemplate* wt = v[idx];
		if (wt)
		{
			TheWeaponStore->createAndFireTempWeapon(wt, getObject(), getObject()->getPosition());
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SlowDeathBehavior::update()
{
	//DEBUG_LOG(("updating SlowDeathBehavior %08lx\n",this));
	DEBUG_ASSERTCRASH(isSlowDeathActivated(), ("hmm, this should not be possible"));

	const SlowDeathBehaviorModuleData* d = getSlowDeathBehaviorModuleData();
	Object* obj = getObject();

	Real timeScale = TheGameLODManager->getSlowDeathScale();

	// Check if we have normal time scale but LODManager is requeseting acceleration
	if (timeScale != 1.0f && m_acceleratedTimeScale == 1.0f && !d->hasNonLodEffects())
	{	
		// speed of deaths has been increased since beginning of death
		// so adjust it to current levels.
		if (timeScale == 0)
		{	
			// instant death
			TheGameLogic->destroyObject(obj);
			return UPDATE_SLEEP_NONE;
		}

		// These are absolute frames by now (onDie added the start frame), so scale only what is
		// left of each; scaling the whole number put every death thousands of frames in the past.
		UnsignedInt scaleFrom = TheGameLogic->getFrame();
		if (m_sinkFrame > scaleFrom)
			m_sinkFrame = scaleFrom + (UnsignedInt)((Real)(m_sinkFrame - scaleFrom) * timeScale);
		if (m_midpointFrame > scaleFrom)
			m_midpointFrame = scaleFrom + (UnsignedInt)((Real)(m_midpointFrame - scaleFrom) * timeScale);
		if (m_destructionFrame > scaleFrom)
			m_destructionFrame = scaleFrom + (UnsignedInt)((Real)(m_destructionFrame - scaleFrom) * timeScale);
		m_acceleratedTimeScale = timeScale;
	};

	UnsignedInt now = TheGameLogic->getFrame();

	// a wreck still carrying on from the speed it died at blows up when it has come to rest
	const Bool momentumHolds = updateMomentum();
	if (momentumHolds && m_destructionFrame <= now)
		m_destructionFrame = now + 1;

	if ((m_flags & (1<<FLUNG_INTO_AIR)) != 0)
	{
		if ((m_flags & (1<<BOUNCED)) == 0)
		{
			++m_sinkFrame;
			++m_midpointFrame;
			++m_destructionFrame;
			if (!obj->isAboveTerrain())
			{
				obj->clearAndSetModelConditionFlags(MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_FLAILING), 
																						MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_BOUNCING));
				m_flags |= (1<<BOUNCED);
			}
			
			// Here we want to make sure we die if we collide with a tree on the way down
			PhysicsBehavior *phys = obj->getPhysics();
			if ( phys )
			{
				ObjectID treeID = phys->getLastCollidee();
				Object *tree = TheGameLogic->findObjectByID( treeID );
				if ( tree )
				{
					if (tree->isKindOf( KINDOF_SHRUBBERY ) )
					{
						obj->setDisabled( DISABLED_HELD );
						obj->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_FLAILING) ); 
						obj->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_EXPLODED_BOUNCING) ); 
						obj->setModelConditionFlags(   MAKE_MODELCONDITION_MASK(MODELCONDITION_PARACHUTING) ); //looks like he is snagged in a tree
						obj->setPositionZ( obj->getPosition()->z - (d->m_sinkRate * 50.0f) );// make him sink faster
						if ( !obj->isAboveTerrain() )
							TheGameLogic->destroyObject(obj);

					}
				}
			}



		}
	}

	// sinking holds the physics still, so it waits for the slide too
	if ( (now >= m_sinkFrame && d->m_sinkRate > 0.0f && !momentumHolds) )
	{
		// disable Physics (if any) so that we can control the sink...
		obj->setDisabled( DISABLED_HELD );
		Coord3D pos = *obj->getPosition();
		pos.z -= d->m_sinkRate / m_acceleratedTimeScale;
		obj->setPosition( &pos );
	}

	// the midpoint's debris, like the INITIAL hull, belongs where the wreck stops
	if( now >= m_midpointFrame && (m_flags & (1<<MIDPOINT_EXECUTED)) == 0 && !momentumHolds )
	{
		doPhaseStuff(SDPHASE_MIDPOINT);
		m_flags |= (1<<MIDPOINT_EXECUTED);
	}

	if (now >= m_destructionFrame)
	{
		doPhaseStuff(SDPHASE_FINAL);
		TheGameLogic->destroyObject(obj);
	}

	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void SlowDeathBehavior::onDie( const DamageInfo *damageInfo )
{
	Object *obj = getObject();

	if (!isDieApplicable(damageInfo))
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai)
	{
		// has another AI already handled us. (hopefully another SlowDeathBehavior)
		if (ai->isAiInDeadState())
			return;
		ai->markAsDead();
	}

	// deselect this unit for all players.
	TheGameLogic->deselectObject(obj, PLAYERMASK_ALL, TRUE);

	Int total = 0;
	for (BehaviorModule** update = obj->getBehaviorModules(); *update; ++update)
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != NULL && sdu->isDieApplicable(damageInfo))
		{
			total += sdu->getProbabilityModifier( damageInfo );
		}
	}
	DEBUG_ASSERTCRASH(total > 0, ("Hmm, this is wrong"));


	// this returns a value from 1...total, inclusive
	Int roll = GameLogicRandomValue(1, total);

	for (BehaviorModule** update = obj->getBehaviorModules(); *update; ++update)
	{
		SlowDeathBehaviorInterface* sdu = (*update)->getSlowDeathBehaviorInterface();
		if (sdu != NULL && sdu->isDieApplicable(damageInfo))
		{
			roll -= sdu->getProbabilityModifier( damageInfo );
			if (roll <= 0)
			{
				sdu->beginSlowDeath(damageInfo);
				return;
			}
		}
	}

	DEBUG_CRASH(("We should never get here"));
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: the slide a vehicle killed on the move carries on with */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// sink frame
	xfer->xferUnsignedInt( &m_sinkFrame );

	// midpoint frame
	xfer->xferUnsignedInt( &m_midpointFrame );

	// destruction frame
	xfer->xferUnsignedInt( &m_destructionFrame );

	// accelerated time scale
	xfer->xferReal( &m_acceleratedTimeScale );

	// flags
	xfer->xferUnsignedInt( &m_flags );

	// the slide; a version 1 save has none, and its flags never carry the momentum bits
	if( version >= 2 )
	{
		xfer->xferReal( &m_momentumVelX );
		xfer->xferReal( &m_momentumVelY );
		xfer->xferReal( &m_momentumHopSpeed );
		xfer->xferUnsignedInt( &m_momentumStartFrame );
		xfer->xferUnsignedInt( &m_momentumEndFrame );
		xfer->xferUnsignedInt( &m_momentumDeadlineFrame );
		xfer->xferInt( &m_deferredOCL );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SlowDeathBehavior::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
