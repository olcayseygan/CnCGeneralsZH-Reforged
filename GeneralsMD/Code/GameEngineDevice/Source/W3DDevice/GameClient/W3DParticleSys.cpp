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

// W3DParticleSys.cpp
// W3D Particle System implementation
// Author: Michael S. Booth, November 2001

#include "Common/GlobalData.h"
#include "Lib/Clock.h"
#include "GameClient/Color.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/FireLights.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "WW3D2/camera.h"
#include "WW3D2/ww3d.h"
#include "W3DDevice/GameClient/W3DSmoothMotion.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx11runtime.h"
#include "Platform/SunShadow.h"
#include "Common/JobSystem.h"
#include "Common/LogicThread.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/LifetimeUpdate.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/ObserverCamera.h"
#include <algorithm>
#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "Platform/WaterFoamParticlePolicy.h"
#include <map>
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "Platform/HazardCloudPolicy.h"
#include "Platform/HazardCloud.h"
#include <array>
#include <memory>
#include <cmath>
#include <tuple>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//-------------------------------------------------------------------------------------------------


#include "Common/QuickTrig.h"
#include "Platform/MsvcFloatCasts.h"
#include "Common/MapObject.h"

// DX11GpuParticles.cpp reads the records at these strides and writes the fade window's numbers into
// its HLSL; a change here is a change there.
static_assert( sizeof( ParticleGpuSpawn ) == 80 && sizeof( ParticleGpuSystemEntry ) == 240,
	"DX11GpuParticles.cpp's SpawnStride and EntryStride" );
static_assert( PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES == 16 && PARTICLE_CLOSED_FORM_FADE_LIFETIME_DIVISOR == 4,
	"DX11GpuParticles.cpp's closed_fade" );
static_assert( GpuParticles::SYSTEM_SLOTS <= 0x10000, "a spawn record holds its slot in 16 bits" );
static_assert( MAX_KEYFRAMES == 8, "DX11GpuParticles.cpp's SystemEntry holds eight segments of each" );

/// The ring's largest size: Evaluate dispatches a group of 64 a record, and a dispatch takes 65535.
static const UnsignedInt GPU_PARTICLE_MOST_RECORDS = 64u * 65535u;

/// The ring the detail preset or -gpuparticles asks for, held to what one dispatch can age.
static UnsignedInt gpuCapacityAsked( void )
{
	const UnsignedInt asked = (UnsignedInt)TheGlobalData->getEffectiveGpuParticleCapacity();
	return asked < GPU_PARTICLE_MOST_RECORDS ? asked : GPU_PARTICLE_MOST_RECORDS;
}

/// A system entry's m_flags as DX11GpuParticles.cpp's compute pass reads them: the system is there,
/// and its particles rest on the terrain (ParticleBounce) instead of falling through it.
static const UnsignedInt PARTICLE_GPU_ALIVE = 1;
static const UnsignedInt PARTICLE_GPU_GROUND_CLAMPED = 2;

#ifdef DEBUG_LOGGING
extern Real TheParticleFillMS;
extern UnsignedInt TheParticlesPastGroupLimit;
#endif

#if ZH_MODERN_WATER
// Modern water replaces only verified vessel-surface particles, after that hull's water draw.  The
// owner is the one the frame view took at the sync point, so the draw reads no live object.
static Bool replaceLegacyVesselFoam(ParticleSystem *sys, ObjectID owner)
{
    if(!sys || !TheWaterRenderObj || !sys->getTemplate() || owner==INVALID_ID) return FALSE;
    const Bool ordinaryGroundParticle=!sys->shouldBillboard() && !sys->isUsingDrawables() &&
        !sys->isUsingStreak() && !sys->isUsingSmudge() && sys->getVolumeParticleDepth()==0;
    const AsciiString name=sys->getTemplate()->getName(),texture=sys->getParticleTypeName();
    if(!ordinaryGroundParticle || !WaterFoamParticlePolicy::KnownSurfaceFoam(name.str(),texture.str())) return FALSE;
    const Bool mirror=ShaderClass::Is_Backface_Culling_Inverted();
    // Keep the original admission functions for each pass. In particular, reflected
    // hulls and spray remain intact; only same-interface surface ripples are isolated.
    const Bool admitted=mirror?TheWaterRenderObj->isModernWaterMirrorForOwner(owner):TheWaterRenderObj->hasModernVesselFoam(owner);
    // Simulation, emission, aging and IDs are untouched. Missing modern admission
    // immediately restores the original particle draw.
    return mirror?
        WaterFoamParticlePolicy::ExcludeSelfReflection(name.str(),texture.str(),TRUE,TRUE,admitted):
        WaterFoamParticlePolicy::Replace(name.str(),texture.str(),TRUE,TRUE,admitted,FALSE);
}
#endif

/** The preset a particle shader type draws with, and whether it names one at all.  An invalid type
	* leaves the point group's previous shader in place on the old path, so it does not take the
	* direct one. */
static Bool particlePresetShader( ParticleSystemInfo::ParticleShaderType type, ShaderClass *shader )
{
	switch( type )
	{
		case ParticleSystemInfo::ADDITIVE:	*shader = ShaderClass::_PresetAdditiveSpriteShader;				return TRUE;
		case ParticleSystemInfo::ALPHA:			*shader = ShaderClass::_PresetAlphaSpriteShader;					return TRUE;
		case ParticleSystemInfo::ALPHA_TEST:	*shader = ShaderClass::_PresetATestSpriteShader;					return TRUE;
		case ParticleSystemInfo::MULTIPLY:	*shader = ShaderClass::_PresetMultiplicativeSpriteShader;	return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// Fire lights the smoke around it.  Each frame the additive systems (flames, explosions, muzzle
// flashes) are reduced to one light apiece at their brightness-weighted centre, the scene's enabled
// light pulses join them, and every alpha-blended particle near one of those is lit by it.  A fire's
// light is the colour of fire, not of its particles: an additive flame's vertex colour is near white
// and the orange is in its texture, so the hue taken from the vertices made the light grey.  It is
// strongest just over the flames and falls away with height above them and sideways from them, so a
// plume is lit at its base and goes grey as it rises; it flickers with the flames because the
// flames' own particles are what measure it.  The light a particle takes is reflected by its own
// colour and, past white, keeps its hue rather than clipping, so white smoke turns orange and soot
// dark orange.  All of it is client-side colour on the CPU, so both devices draw it.  It goes in
// before the shade, being vertex colour, so a plume's own shade dims it too (the shaders have no
// channel to carry it past the shade); a thin particle takes less of it, which keeps the plume's thin
// edge, unshaded, from glowing brighter than its shaded core.
//-------------------------------------------------------------------------------------------------

static const Int	SMOKE_LIGHTS_MAX							= 32;			///< lights gathered a frame, the strongest kept
static const Int	SMOKE_LIGHTS_PER_SYSTEM				= 4;			///< lights one smoke system evaluates per particle
static const Real	FIRE_LIGHT_FULL_WEIGHT				= 120.0f;	///< sum of brightness x size at which a fire is full strength
static const Real	FIRE_LIGHT_MIN_WEIGHT					= 4.0f;		///< below this an additive system lights nothing
static const Real	FIRE_LIGHT_RADIUS							= 35.0f;	///< sideways reach of a fire's light beyond its own spread
static const Real	FIRE_LIGHT_SPREAD_SCALE				= 2.0f;		///< times the flames' RMS spread, added to the reach
static const Real	FIRE_LIGHT_MAX_RADIUS					= 140.0f;
static const Real	FIRE_LIGHT_HEIGHT							= 90.0f;	///< height over the flames at which their light is gone
static const Real	FIRE_LIGHT_BELOW_SHARE				= 0.25f;	///< share of the sideways reach the light goes below them
static const Real	FIRE_LIGHT_GAIN								= 0.7f;		///< light a full-strength fire puts on the smoke over it
static const Real	PULSE_LIGHT_GAIN							= 0.4f;		///< same for an FX light pulse, whose colour is already full
static const Real	SMOKE_LIGHT_MAX_GLOW					= 0.9f;		///< most light any particle takes, brightest channel
static const Real	SMOKE_LIGHT_SOOT_SHARE				= 0.35f;	///< share of the light even black smoke gives back
static const Real	SMOKE_LIGHT_THIN_ALPHA				= 0.25f;	///< a particle thinner than this takes its share of the light
static const Real	FIRE_LIGHT_RED = 1.0f, FIRE_LIGHT_GREEN = 0.55f, FIRE_LIGHT_BLUE = 0.24f;	///< 255,140,60


/** A light as the smoke sees it: its colour is already scaled by its strength and gain.  A fire's
	* reaches its radius sideways, its height upwards and a share of the radius downwards; a pulse's
	* (invHeight zero) is a ball.  reachSq is the ball that holds either, for picking. */
struct SmokeLight
{
	Real x, y, z;
	Real radiusSq, invRadiusSq;
	Real invHeight, invBelow;
	Real reachSq;
	Real r, g, b;
	Real strength;	///< orders the list, strongest first
};

/** Keeps the list at SMOKE_LIGHTS_MAX by dropping the weakest. */
static void addSmokeLight( SmokeLight *lights, Int &count, const SmokeLight &light )
{
	Int slot = count;
	if (count == SMOKE_LIGHTS_MAX)
	{
		slot = 0;
		for (Int i = 1; i < count; ++i)
			if (lights[ i ].strength < lights[ slot ].strength)
				slot = i;
		if (lights[ slot ].strength >= light.strength)
			return;
	}
	else
		++count;
	lights[ slot ] = light;
}

/** An additive system's light before the view box has had its say.  It depends on the particles
	* alone, and they only move when ParticleSystemManager::update runs, once a logic frame, so the
	* walk over them is kept for every pass drawn until the next one. */
struct FireLight
{
	SmokeLight	light;
	Real				reach;	///< the light's reach in any one axis, for the view box test
};

static std::vector<FireLight>	s_fireLights;					///< in system list order, as the walk found them
static Bool										s_fireLightsValid = FALSE;
static UnsignedInt						s_fireLightsFrame;			///< the particle update they were taken after
static UnsignedInt						s_fireLightsParticles;
static UnsignedInt						s_fireLightsSystems;

/** One fire's light from its particles' weighted sums: w the weight, x, y and z the weighted
	* position, sq the weighted square of it.  FALSE when it is not bright enough. */
static Bool makeFireLight( Real w, Real x, Real y, Real z, Real sq, Real fade, FireLight &fire )
{
	if (w < FIRE_LIGHT_MIN_WEIGHT)
		return FALSE;

	SmokeLight &light = fire.light;
	const Real inv = 1.0f / w;
	light.x = x * inv;
	light.y = y * inv;
	light.z = z * inv;
	const Real variance = sq * inv - (light.x * light.x + light.y * light.y + light.z * light.z);
	Real radius = FIRE_LIGHT_RADIUS + FIRE_LIGHT_SPREAD_SCALE * (variance > 0.0f ? sqrtf( variance ) : 0.0f);
	if (radius > FIRE_LIGHT_MAX_RADIUS)
		radius = FIRE_LIGHT_MAX_RADIUS;

	const Real height = FIRE_LIGHT_HEIGHT;
	const Real reach = radius > height ? radius : height;
	fire.reach = reach;

	// fire's own colour, and the strength from how much of the flames there is
	light.strength = (w < FIRE_LIGHT_FULL_WEIGHT ? w / FIRE_LIGHT_FULL_WEIGHT : 1.0f) * fade;
	const Real scale = light.strength * FIRE_LIGHT_GAIN;
	light.r = FIRE_LIGHT_RED * scale;
	light.g = FIRE_LIGHT_GREEN * scale;
	light.b = FIRE_LIGHT_BLUE * scale;
	light.radiusSq = radius * radius;
	light.invRadiusSq = 1.0f / light.radiusSq;
	light.invHeight = 1.0f / height;
	light.invBelow = 1.0f / (radius * FIRE_LIGHT_BELOW_SHARE);
	light.reachSq = reach * reach;
	return TRUE;
}

static void addFireLight( Real w, Real x, Real y, Real z, Real sq, Real fade )
{
	FireLight fire;
	if (makeFireLight( w, x, y, z, sq, fade, fire ))
		s_fireLights.push_back( fire );
}

/** A system's flames as weighted sums.  An Emissive body's colour stays white while its flipbook cools
	* to soot, so its weight falls with the life left. */
static void sumFlames( ParticleSystem *sys, FireLights::Flames &flames )
{
	const Bool emissive = sys->getTemplate()->isEmissive();
	for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
	{
		const RGBColor *c = p->getColor();
		Real pw = (c->red + c->green + c->blue) * (1.0f / 3.0f) * p->getSize();
		if (emissive && p->m_lifetime)
			pw *= (Real)p->getLifetimeLeft() / (Real)p->m_lifetime;
		if (pw <= 0.0f)
			continue;
		const Coord3D *pos = p->getPosition();
		flames.add( pw, pos->x, pos->y, pos->z );
	}
}

/** One additive or Emissive system as a point light, if it is bright enough. */
static void gatherFireLight( ParticleSystem *sys, Real fade )
{
	FireLights::Flames flames;
	sumFlames( sys, flames );
	addFireLight( flames.w, flames.x, flames.y, flames.z, flames.sq, fade );
}

// The Firelight option's lights, picked by the last doParticles for the next frame's terrain and
// models: the scene takes them first and these cost nothing but the walk over the flames in view.
static FireLights::Gather	s_firelightGroups;
static FireLights::Light	s_firelights[ FireLights::MAX_LIGHTS ];
static Int								s_firelightCount = 0;

const FireLights::Light *getFirelights( Int *count )
{
	*count = s_firelightCount;
	return s_firelights;
}

/** The flames in view, grouped, then the strongest as lights.  A FireLook template that draws by
	* adding light is a flame or a blast; its smoke, FireLook or not, blends.  A FireLook system is never
	* on the GPU path (isGpuEligible), so nothing here pays for a closed form, and the systems were filed
	* once a particle update (m_frameFires). */
static void gatherFirelights( const AABoxClass &view, UnsignedInt frame, const std::vector<ParticleSystem *> &systems, const std::vector<Real> &fades )
{
	s_firelightGroups.clear();
	for (size_t i = 0; i < systems.size(); ++i)
	{
		// the view is filed once a particle update and a system can run out of particles before the next
		Particle *first = systems[ i ]->getFirstParticle();
		if (first == NULL
				|| WWMath::Fabs( first->getPosition()->x - view.Center.X ) > view.Extent.X + FireLights::MAX_REACH
				|| WWMath::Fabs( first->getPosition()->y - view.Center.Y ) > view.Extent.Y + FireLights::MAX_REACH)
			continue;
		FireLights::Flames flames;
		sumFlames( systems[ i ], flames );
		flames.scale( fades[ i ] );
		s_firelightGroups.add( flames );
	}
	const float center[3] = { view.Center.X, view.Center.Y, view.Center.Z };
	const float extent[3] = { view.Extent.X, view.Extent.Y, view.Extent.Z };
	s_firelightCount = s_firelightGroups.pick( center, extent, frame, s_firelights );

#ifdef DEBUG_LOGGING
	static UnsignedInt s_nextLog = 0;
	if (frame >= s_nextLog && s_firelightCount > 0)
	{
		s_nextLog = frame + 10 * LOGICFRAMES_PER_SECOND;
		DEBUG_LOG(( "FIRELIGHT frame %u: %d flame systems, %d groups in view, %d lit, strongest %.2f\n",
			frame, (Int)systems.size(), s_firelightGroups.count(), s_firelightCount, s_firelights[ 0 ].strength ));
	}
#endif
}

/** The same for a GPU system, from the spawns it keeps (ParticleSystem::getGpuSample) aged in closed
	* form to the last particle update, as a CPU particle stands then.
	* ponytail: GPU_SPAWN_SAMPLES spawns stand in for the whole fire, so a big one weighs as a small
	* one would; scale by a live count per slot if fires under smoke look too dim.
	* A million particles can be thousands of such systems at sixteen closed forms each, eight
	* milliseconds on one thread, so the systems go to the job pool, one item each, and the lit ones
	* join s_fireLights afterwards in the order the walk found them. */
struct GpuFireLight
{
	ParticleSystem *								system;
	Real														fade;
	UnsignedInt											slot;
	const ParticleGpuSystemEntry *	entry;
	FireLight												light;
	Bool														lit;
};

static std::vector<GpuFireLight> s_gpuFireLights;	///< kept across updates so the gather does not allocate

static const Int GPU_FIRE_LIGHTS_PER_CLAIM = 16;

static void gatherGpuFireLight( Int index, void *context )
{
	GpuFireLight &fire = ( (GpuFireLight *)context )[ index ];
	ParticleSystem *sys = fire.system;
	const UnsignedInt slot = fire.slot;
	const ParticleGpuSystemEntry &entry = *fire.entry;
	const Bool emissive = sys->getTemplate()->isEmissive();
	const UnsignedInt now = ParticleSystemManager::getParticleFrame();
	Real w = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f, sq = 0.0f;
	for (UnsignedInt i = 0; i < sys->getGpuSampleCount(); ++i)
	{
		const ParticleSystem::GpuSpawnSample &sample = sys->getGpuSample( i );
		ParticleGpuSpawn spawn;
		particleGpuSpawnPack( sample.m_info, sample.m_birthFrame, slot, 0, &spawn );
		const Real k = (Real)( now - sample.m_birthFrame + 1 );
		ParticleClosedFormState state;
		particleClosedFormEvaluate( spawn, entry, k, &state );
		if (state.m_dead)
			continue;
		Real pw = (state.m_color.red + state.m_color.green + state.m_color.blue) * (1.0f / 3.0f) * state.m_size;
		if (emissive)
			pw *= ( spawn.m_lifetime - k ) / spawn.m_lifetime;
		if (pw <= 0.0f)
			continue;
		const Coord3D &pos = state.m_pos;
		w += pw;
		x += pw * pos.x;
		y += pw * pos.y;
		z += pw * pos.z;
		sq += pw * (pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
	}
	fire.lit = makeFireLight( w, x, y, z, sq, fire.fade, fire.light );
}

/** The scene's enabled light pulses, which FX lists put on explosions. */
static void gatherPulseLights( SmokeLight *lights, Int &count, const AABoxClass &view )
{
	if (W3DDisplay::m_3DScene == NULL)
		return;

	RefRenderObjListIterator it( W3DDisplay::m_3DScene->getDynamicLights() );
	for (it.First(); !it.Is_Done(); it.Next())
	{
		W3DDynamicLight *pulse = (W3DDynamicLight *)it.Peek_Obj();
		if (!pulse->isEnabled() || pulse->Get_Type() != LightClass::POINT)
			continue;

		Real nearRange, farRange;
		pulse->Get_Far_Attenuation_Range( nearRange, farRange );
		if (farRange < 1.0f)
			continue;

		const Vector3 pos = pulse->Get_Position();
		if (WWMath::Fabs( pos.X - view.Center.X ) > view.Extent.X + farRange
				|| WWMath::Fabs( pos.Y - view.Center.Y ) > view.Extent.Y + farRange
				|| WWMath::Fabs( pos.Z - view.Center.Z ) > view.Extent.Z + farRange)
			continue;

		Vector3 diffuse;
		pulse->Get_Diffuse( &diffuse );
		SmokeLight light;
		light.x = pos.X;
		light.y = pos.Y;
		light.z = pos.Z;
		light.radiusSq = farRange * farRange;
		light.invRadiusSq = 1.0f / light.radiusSq;
		light.invHeight = 0.0f;		// a ball
		light.invBelow = 0.0f;
		light.reachSq = light.radiusSq;
		light.r = diffuse.X * PULSE_LIGHT_GAIN;
		light.g = diffuse.Y * PULSE_LIGHT_GAIN;
		light.b = diffuse.Z * PULSE_LIGHT_GAIN;
		light.strength = WWMath::Max( diffuse.X, WWMath::Max( diffuse.Y, diffuse.Z ) );
		if (light.strength > 0.0f)
			addSmokeLight( lights, count, light );
	}
}

/** Fills lights[] with this frame's lights, strongest first, and returns how many.  The fires are
	* offered in the order the systems walk found them, so the weakest-out list keeps the same ones. */
static Int gatherSmokeLights( SmokeLight *lights, const AABoxClass &view )
{
	Int count = 0;
	for (size_t i = 0; i < s_fireLights.size(); ++i)
	{
		const FireLight &fire = s_fireLights[ i ];
		const SmokeLight &light = fire.light;
		if (WWMath::Fabs( light.x - view.Center.X ) > view.Extent.X + fire.reach
				|| WWMath::Fabs( light.y - view.Center.Y ) > view.Extent.Y + fire.reach
				|| WWMath::Fabs( light.z - view.Center.Z ) > view.Extent.Z + fire.reach)
			continue;
		addSmokeLight( lights, count, light );
	}
	gatherPulseLights( lights, count, view );

	for (Int i = 1; i < count; ++i)
	{
		const SmokeLight light = lights[ i ];
		Int j = i;
		for (; j > 0 && lights[ j - 1 ].strength < light.strength; --j)
			lights[ j ] = lights[ j - 1 ];
		lights[ j ] = light;
	}
	return count;
}

/** The strongest lights that reach an alpha system's particles, at most SMOKE_LIGHTS_PER_SYSTEM.
	* Any other system gets none. */
static Int pickSmokeLights( ParticleSystem *sys, const SmokeLight *lights, Int count, SmokeLight *picked )
{
	if (count == 0 || sys->getShaderType() != ParticleSystemInfo::ALPHA || sys->getTemplate()->isEmissive())
		return 0;

	Particle *p = sys->getFirstParticle();
	if (p == NULL)
		return 0;
	Real minX = p->getPosition()->x, maxX = minX;
	Real minY = p->getPosition()->y, maxY = minY;
	Real minZ = p->getPosition()->z, maxZ = minZ;
	for (p = p->m_systemNext; p; p = p->m_systemNext)
	{
		const Coord3D *pos = p->getPosition();
		minX = WWMath::Min( minX, pos->x );	maxX = WWMath::Max( maxX, pos->x );
		minY = WWMath::Min( minY, pos->y );	maxY = WWMath::Max( maxY, pos->y );
		minZ = WWMath::Min( minZ, pos->z );	maxZ = WWMath::Max( maxZ, pos->z );
	}

	Int n = 0;
	for (Int i = 0; i < count && n < SMOKE_LIGHTS_PER_SYSTEM; ++i)
	{
		const SmokeLight &l = lights[ i ];
		const Real dx = l.x < minX ? minX - l.x : (l.x > maxX ? l.x - maxX : 0.0f);
		const Real dy = l.y < minY ? minY - l.y : (l.y > maxY ? l.y - maxY : 0.0f);
		const Real dz = l.z < minZ ? minZ - l.z : (l.z > maxZ ? l.z - maxZ : 0.0f);
		if (dx * dx + dy * dy + dz * dz < l.reachSq)
			picked[ n++ ] = l;
	}
	return n;
}

/** A smoke particle's colour lit by the picked lights.  A fire's light falls as (1 - h^2/R^2)^2
	* sideways and (1 - dz/H)^2 above the flames, much faster below them; a pulse's as (1 - d^2/R^2)^2
	* all round.  The sum is held to SMOKE_LIGHT_MAX_GLOW in its brightest channel, scaled down by how
	* thin the particle is, and reflected by the particle's own colour (lifted by the soot share, so
	* black smoke still shows the fire); a result past white is scaled back whole, which keeps its
	* hue. */
static inline void lightSmoke( const Coord3D *pos, Real alpha, const SmokeLight *lights, Int count,
	Real &red, Real &green, Real &blue )
{
	Real r = 0.0f, g = 0.0f, b = 0.0f;
	for (Int i = 0; i < count; ++i)
	{
		const SmokeLight &l = lights[ i ];
		const Real dx = pos->x - l.x, dy = pos->y - l.y, dz = pos->z - l.z;
		Real f;
		if (l.invHeight > 0.0f)
		{
			const Real side = 1.0f - (dx * dx + dy * dy) * l.invRadiusSq;
			const Real up = (dz >= 0.0f) ? 1.0f - dz * l.invHeight : 1.0f + dz * l.invBelow;
			if (side <= 0.0f || up <= 0.0f)
				continue;
			f = side * side * up * up;
		}
		else
		{
			const Real t = 1.0f - (dx * dx + dy * dy + dz * dz) * l.invRadiusSq;
			if (t <= 0.0f)
				continue;
			f = t * t;
		}
		r += f * l.r;
		g += f * l.g;
		b += f * l.b;
	}

	Real peak = WWMath::Max( r, WWMath::Max( g, b ) );
	if (peak <= 0.0f)
		return;
	Real scale = (peak > SMOKE_LIGHT_MAX_GLOW) ? SMOKE_LIGHT_MAX_GLOW / peak : 1.0f;
	if (alpha < SMOKE_LIGHT_THIN_ALPHA)
		scale *= alpha / SMOKE_LIGHT_THIN_ALPHA;

	const Real soot = SMOKE_LIGHT_SOOT_SHARE;
	red += r * scale * (soot + (1.0f - soot) * red);
	green += g * scale * (soot + (1.0f - soot) * green);
	blue += b * scale * (soot + (1.0f - soot) * blue);
	peak = WWMath::Max( red, WWMath::Max( green, blue ) );
	if (peak > 1.0f)
	{
		red /= peak;
		green /= peak;
		blue /= peak;
	}
}

/** A particle of a stealthed thing's system, faded by ParticleSystem::getStealthFade, which only
	* hands a partial fade to additive and alpha blended systems: an additive particle adds less
	* light, an alpha blended one takes less alpha. */
static inline void stealthFadeParticle( ParticleSystemInfo::ParticleShaderType type, Real fade,
	Real &red, Real &green, Real &blue, Real &alpha )
{
	if (type == ParticleSystemInfo::ADDITIVE)
	{
		red *= fade;
		green *= fade;
		blue *= fade;
	}
	else
		alpha *= fade;
}

/** What every job of one frame's billboard fill reads. */
struct BillboardFillJob
{
	W3DParticleSystemManager::BillboardFill *	fills;
	Matrix4x4																	view;
	Real																			centerX, centerY, centerZ;
	Real																			extentX, extentY, extentZ;
	SmokeLight																lights[ SMOKE_LIGHTS_MAX ];	///< this frame's, strongest first
	Int																				lightCount;
	/// Direct3D 11 is drawing the picture: the glow goes into the normals, and its pixel program adds
	/// it after the sun's shadow and the smoke's own shade, which dimmed a fire seen through the far
	/// side of a plume when it was in the colour.  Direct3D 9 shades nothing and keeps it baked.
	Bool																			glowAfterShade;
};

/// systems a pool thread claims at once; a system is a few hundred particles, so one claim a system
/// would spend more on the interlocked claim than the work is worth
static const Int BILLBOARD_FILLS_PER_CLAIM = 4;

/** One sorted billboard system's quads, written into the range reserved for it.  Reads the system's
	* particles and writes only that range and that fill's counts: no allocation, no device calls,
	* nothing another job touches.  The cull and the 512-a-system cut are the ones the serial loop
	* below applies to every other system. */
static void fillBillboards( Int index, void *context )
{
	BillboardFillJob *job = (BillboardFillJob *)context;
	W3DParticleSystemManager::BillboardFill &fill = job->fills[ index ];
	VertexFormatXYZNDUV2 *quad = fill.range.Vertices;
	Int drawn = 0;
	SmokeLight lights[ SMOKE_LIGHTS_PER_SYSTEM ];
	const Int lightCount = pickSmokeLights( fill.system, job->lights, job->lightCount, lights );
	const ParticleSystemInfo::ParticleShaderType shaderType = fill.system->getShaderType();
	const UnsignedInt flipbookLog2 = fill.system->getTemplate()->getFlipbookGridLog2();

	for (Particle *p = fill.system->getFirstParticle(); p; p = p->m_systemNext)
	{
		const Coord3D *pos = p->getPosition();
		const Real psize = p->getSize();

		//Cull particle to edges of screen and terrain.
		if (WWMath::Fabs(pos->x - job->centerX) > (job->extentX + psize))
			continue;

		if (WWMath::Fabs(pos->y - job->centerY) > (job->extentY + psize))
			continue;

		if (WWMath::Fabs(pos->z - job->centerZ) > (job->extentZ + psize))
			continue;

		const RGBColor *color = p->getColor();
		Real red = color->red, green = color->green, blue = color->blue;
		Vector3 glow( 0.0f, 0.0f, 0.0f );
		if (lightCount > 0)
		{
			lightSmoke( pos, p->getAlpha(), lights, lightCount, red, green, blue );
			if (job->glowAfterShade)
			{
				glow.Set( red - color->red, green - color->green, blue - color->blue );
				red = color->red;
				green = color->green;
				blue = color->blue;
			}
		}
		Real alpha = p->getAlpha();
		if (fill.fade < 1.0f)
			stealthFadeParticle( shaderType, fill.fade, red, green, blue, alpha );
		const unsigned packed = DX8Wrapper::Convert_Color_Clamp( Vector4( red, green, blue, alpha ) );
		// The orientation table's index wraps, as it did on Windows (Platform/MsvcFloatCasts.h).
		const uint8 orientation = floatToByteAsMsvc( p->getAngle() * 255.0f / (2.0f * PI) );
		PointGroupClass::Write_Billboard( quad, job->view, Vector3( pos->x, pos->y, pos->z ), psize,
			orientation, packed, glow, flipbookLog2, flipbookLog2 ? p->getFlipbookFrame() : 0 );
		if (fill.fire)
		{
			// FIRE_LOOK_SAMPLING's packing: a whole number below 256, the seed times eight plus the
			// flipbook's log2 frames a side, so the noise bends a flipbook frame inside its own cell;
			// then the age held off the whole numbers on both sides, so the interpolated value never
			// floors to the next one.  Lifetime 0 is a particle that never dies (four retail
			// systems), which stays young.
			const Real age = p->m_lifetime ? 1.0f - (Real)p->getLifetimeLeft() / (Real)p->m_lifetime : 0.0f;
			const Real fireAgeSeed = (Real)( ( ( ( p->getPersonality() * 97u ) & 31u ) << 3 ) | flipbookLog2 ) + 0.01f + 0.98f * age;
			for (Int k = 0; k < 4; ++k)
				quad[ k ].v2 = fireAgeSeed;
		}
		quad += 4;

		if (++drawn == fill.capacity)
		{
			fill.pastLimit = fill.system->getParticleCount() - drawn;
			break;
		}
	}

	fill.drawn = drawn;
	fill.glow = job->glowAfterShade && lightCount > 0;
}

//-------------------------------------------------------------------------------------------------
// A contamination field - toxin, anthrax, radiation - is ground-aligned AREA_EFFECT particles:
// squares up to 170 units across, every one at the emitter's height, which UseCallersRadius spreads
// over the weapon's damage radius (140 for a large toxin field).  Drawn flat, on anything but level
// ground the uphill side of a square sank into the terrain and the downhill side hung in the air.
// Each puddle square is a grid here instead, every vertex set on the terrain under it, so the field
// lies on the hill.  A field whose emitter stands clear of the terrain, on a bridge deck, keeps its
// height.  Which squares a field draws is collectGroundFields' business, below.
//
// Before the glow, the same grid takes light off the ground under the bright part of the texture,
// mostly from the channels the field's hue lacks, so the ground under the field turns the field's
// colour and the pool reads as liquid and not as light laid on the ground.  The hue is the
// template's own, so anthrax keeps its colour.  Every field's stain goes down before any field's
// glow: drawn system by system, a later system's stain darkened the glow already laid by the ones
// before it, and the overlaps came out as black-green blotches through the pool.  Both passes are
// plain DX8Wrapper draws in world space and both devices draw them.
//-------------------------------------------------------------------------------------------------

static const Real	FIELD_GRID_SPACING		= 5.0f;		///< world units between grid vertices; a terrain cell is ten
static const Int	FIELD_GRID_MAX_STEPS	= 16;			///< per side; the largest squares get a coarser grid
static const Real	FIELD_LIFT						= 1.0f;		///< over the terrain, for the chords between vertices
static const Real	FIELD_ON_GROUND				= 10.0f;	///< an emitter higher than this over the terrain is on a deck
static const Real	FIELD_STAIN_DARKEN		= 0.05f;	///< share of the ground's light the stain takes in every channel
static const Real	FIELD_STAIN_TINT			= 0.25f;	///< further share taken from the channels the hue lacks
static const Int	FIELD_CHUNK_VERTICES	= 8192;		///< one draw's worth; a 16 step square is 289

/** Is this system a field to be laid on the terrain?  The same pair the field particle count uses.
	* A system attached to a drawable or an object is not: its position is an offset in that model's
	* space (the damaged oil tank's ToxinTankPuddle hangs off a bone), and it moves with it, so it keeps
	* the ordinary particle path. */
static Bool isGroundField( ParticleSystem *sys )
{
	return sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned
		&& !sys->isUsingStreak() && sys->getVolumeParticleDepth() <= 1
		&& sys->getAttachedDrawable() == INVALID_DRAWABLE_ID && sys->getAttachedObject() == INVALID_ID;
}

struct FieldChunk
{
	Int firstVertex, vertexCount;
	Int firstIndex, indexCount;
};

static std::vector<Vector3>					s_fieldPos;
static std::vector<Vector2>					s_fieldUV;
static std::vector<unsigned>				s_fieldGlow;
static std::vector<unsigned>				s_fieldStain;
static std::vector<unsigned short>	s_fieldIndex;
static std::vector<FieldChunk>			s_fieldChunks;

/** One field system's chunks and how it draws. */
struct FieldBatch
{
	TextureClass *texture;		///< a reference held until the frame's fields are drawn
	ShaderClass glow, stain;
	Bool stained;
	Int firstChunk, chunkCount;
};

static std::vector<FieldBatch>			s_fieldBatches;

/** Writes one pass of a chunk and draws it; the index buffer is already set. */
static void drawFieldPass( const FieldChunk &chunk, const std::vector<unsigned> &colors, const ShaderClass &shader )
{
	DynamicVBAccessClass vbAccess( BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, (unsigned short)chunk.vertexCount );
	{
		DynamicVBAccessClass::WriteLockClass lock( &vbAccess );
		VertexFormatXYZNDUV2 *vb = lock.Get_Formatted_Vertex_Array();
		for (Int i = 0; i < chunk.vertexCount; ++i)
		{
			const Int v = chunk.firstVertex + i;
			vb[i].x = s_fieldPos[ v ].X;
			vb[i].y = s_fieldPos[ v ].Y;
			vb[i].z = s_fieldPos[ v ].Z;
			vb[i].nx = 0.0f;
			vb[i].ny = 0.0f;
			vb[i].nz = 1.0f;
			vb[i].diffuse = colors[ v ];
			vb[i].u1 = s_fieldUV[ v ].X;
			vb[i].v1 = s_fieldUV[ v ].Y;
			vb[i].u2 = 0.0f;
			vb[i].v2 = 0.0f;
		}
	}
	DX8Wrapper::Set_Shader( shader );
	DX8Wrapper::Set_Vertex_Buffer( vbAccess );
	DX8Wrapper::Draw_Triangles( 0, (unsigned short)(chunk.indexCount / 3), 0, (unsigned short)chunk.vertexCount );
}

/** One puddle square laid on the terrain as a grid, into the chunk being filled. */
static void addFieldPuddle( FieldChunk &chunk, Real cx, Real cy, Real flatZ, Bool onGround, Real size, Real angle,
	unsigned glowColor, unsigned stainColor )
{
	Int steps = (Int)ceilf( 2.0f * size / FIELD_GRID_SPACING );
	steps = steps < 1 ? 1 : (steps > FIELD_GRID_MAX_STEPS ? FIELD_GRID_MAX_STEPS : steps);
	const Int side = steps + 1;
	const Int vertices = side * side;

	if (chunk.vertexCount + vertices > FIELD_CHUNK_VERTICES)
	{
		s_fieldChunks.push_back( chunk );
		chunk.firstVertex += chunk.vertexCount;
		chunk.firstIndex += chunk.indexCount;
		chunk.vertexCount = 0;
		chunk.indexCount = 0;
	}

	// the square's half axes: PointGroupClass laid a ground quad out to size on either side
	const Real c = WWMath::Cos( angle ) * size;
	const Real s = WWMath::Sin( angle ) * size;
	const Real step = 2.0f / steps;
	// the buffers grow once a puddle and are written in place: a large field is thousands of vertices
	// and tens of thousands of indices a frame, and a push_back each was most of what it cost
	const size_t firstVertex = s_fieldPos.size();
	s_fieldPos.resize( firstVertex + vertices );
	s_fieldUV.resize( firstVertex + vertices );
	s_fieldGlow.resize( firstVertex + vertices, glowColor );
	s_fieldStain.resize( firstVertex + vertices, stainColor );
	Vector3 *outPos = &s_fieldPos[ firstVertex ];
	Vector2 *outUV = &s_fieldUV[ firstVertex ];
	for (Int j = 0; j < side; ++j)
	{
		const Real b = -1.0f + j * step;
		for (Int i = 0; i < side; ++i)
		{
			const Real a = -1.0f + i * step;
			const Real x = cx + a * c - b * s;
			const Real y = cy + a * s + b * c;
			const Real z = onGround ? TheTerrainRenderObject->getHeightMapHeight( x, y, NULL ) + FIELD_LIFT : flatZ;
			(outPos++)->Set( x, y, z );
			(outUV++)->Set( (1.0f - a) * 0.5f, (1.0f - b) * 0.5f );
		}
	}

	const Int base = chunk.vertexCount;
	const size_t firstIndex = s_fieldIndex.size();
	s_fieldIndex.resize( firstIndex + steps * steps * 6 );
	unsigned short *outIndex = &s_fieldIndex[ firstIndex ];
	for (Int j = 0; j < steps; ++j)
	{
		for (Int i = 0; i < steps; ++i)
		{
			const unsigned short v = (unsigned short)(base + j * side + i);
			*outIndex++ = v;
			*outIndex++ = (unsigned short)(v + 1);
			*outIndex++ = (unsigned short)(v + side);
			*outIndex++ = (unsigned short)(v + 1);
			*outIndex++ = (unsigned short)(v + side + 1);
			*outIndex++ = (unsigned short)(v + side);
		}
	}
	chunk.vertexCount += vertices;
	chunk.indexCount += steps * steps * 6;
}

//-------------------------------------------------------------------------------------------------
// A field does not draw its particles.  The object behind it fires its field weapon every half
// second, and each shot's FX starts a new system whose particles swell from black to the field's
// colour and back to black in two seconds, at fresh random places: drawn as they are, the pool
// throbbed, its patches came and went, and past MaxFieldParticleCount new ones were refused and it
// thinned out.  The systems only say where a field is.  Every system of one template at one place
// is one field, and it is drawn as a fixed scatter of puddles over the emission radius, placed by a
// hash of the place so the scatter is the same every frame, at the template's brightest colour.  It
// fades in over FIELD_FADE_IN and out over the last FIELD_FADE_OUT frames of the field object's
// life, read off its LifetimeUpdate; a field with no such object (a one-off contamination) fades
// with its last particle instead.  This reads the logic's objects and writes nothing back.
//
// Held dead still the pool looked painted on, so each puddle stays where it is and breathes: it
// fades down by up to FIELD_PULSE and back, and swells and shrinks by FIELD_BREATHE, every puddle
// on its own period and phase, on the logic clock blended between ticks as the models are.  The
// puddles fall out of step with each other, so the pool shimmers and never throbs as a whole.
//-------------------------------------------------------------------------------------------------

static const Real	FIELD_PULSE_PERIOD		= 2.5f;		///< seconds, from 0.6 to 1.4 of it per puddle
static const Real	FIELD_PULSE						= 0.55f;	///< share of a puddle's brightness it fades by
static const Real	FIELD_BREATHE					= 0.04f;	///< share of a puddle's size it swings by
static const Real	FIELD_BUBBLE_SPACING	= 30.0f;	///< radius per bubble slot on a radiation pool
static const Int	FIELD_MAX_BUBBLES			= 8;
static const Real	FIELD_BUBBLE_PERIOD		= 1.5f;		///< seconds a bubble lives, from 0.7 to 1.3 of it per slot
static const Real	FIELD_BUBBLE_SIZE			= 5.0f;		///< half width at the pop
static const Real	FIELD_BUBBLE_GAIN			= 0.6f;
static const Real	FIELD_COVER						= 2.0f;		///< puddles per (radius / puddle size) squared
static const Int	FIELD_MAX_PUDDLES			= 48;
static const Real	FIELD_GAIN						= 0.7f;		///< of the template's brightest colour, a puddle
static const Real	FIELD_GAIN_TOXIN			= 0.35f;	///< the same for a field that is not lit like radiation: at 0.7 a pool's middle added past white and hid the ground in it
static const Int	FIELD_FADE_IN					= LOGICFRAMES_PER_SECOND / 2;			///< logic frames
static const Int	FIELD_FADE_OUT				= 3 * LOGICFRAMES_PER_SECOND;			///< logic frames before the field object dies
static const Int	FIELD_FADE_GONE				= FIELD_FADE_OUT;	///< logic frames, a field fogged over or whose object went early
// Whether the fields are tracked the way the volumetric clouds need them: a
// backend that draws them, and the Volumetric style chosen.  Classic keeps the
// retail keys, owner choice, grace and initial delay on every platform.  Changed
// only at the sync point (fieldCloudsSupported), never in the render, because
// FieldKey ordering reads it and changing it while keys exist invalidates the map.
static bool s_fieldCloudsSupported = false;
static bool s_fieldCloudCapability = false;		///< the device's answer, asked once per device
static bool s_fieldCloudCapabilityKnown = false;
static bool fieldCloudsSupported();

static const Real	FIELD_OBJECT_REACH		= 5.0f;		///< how near the field's centre its object stands

struct FieldKey
{
	const ParticleSystemTemplate *tmpl;
	Int x, y;
	Int z;
	Int palette;
	ObjectID owner;
	bool operator<( const FieldKey &o ) const
	{
		if (s_fieldCloudsSupported) {
			// The initial nuke emitter and its periodic radiation pulses describe
			// one hazard. Keep its age/cache when the emitter template changes.
			const bool bound = palette >= 0 && owner != INVALID_ID;
			const bool otherBound = o.palette >= 0 && o.owner != INVALID_ID;
			if (bound != otherBound) return bound < otherBound;
			if (bound) {
				if (owner != o.owner) return owner < o.owner;
				return palette < o.palette;
			}
		}
		if (tmpl != o.tmpl) return tmpl < o.tmpl;
		if (x != o.x) return x < o.x;
		if (y != o.y) return y < o.y;
		return s_fieldCloudsSupported && z < o.z;
	}
};

/** A field as it is drawn, kept between frames: a shot whose particles the LOD refused leaves a
	* half second with no system at the place, and the pool stays through it. */
struct FieldState
{
	Int born;						///< logic frame it was first seen
	Int visualBorn;             ///< formation age survives fog fade changes
	Real cloudShown, cloudFormation, cloudGoneDensity, cloudGoneFormation, cloudRevealFrom;
	Int cloudGoneAt, cloudRevealAt;
	Int cloudDieFrame;          ///< owner's LifetimeUpdate die frame at the last sync point, or -1
	Int seen;						///< logic frame a system was last seen at it
	ObjectID object;		///< the object whose life it fades with, or INVALID_ID
	Int gone;						///< logic frame it began going out (fog, or its object gone), or -1
	Real goneFade;			///< the fade it had then
	Real shown;					///< the fade it was last drawn at
	Int particleEnd;		///< logic frame its last particle ends, for a field with no object
	Coord3D centre;
	Real radius;				///< the emission radius, which UseCallersRadius set from the weapon
	Real size;					///< a puddle's half width
	RGBColor color;			///< the template's brightest keyframe
	ParticleSystemInfo::ParticleShaderType shader;
	AsciiString texture;
	Real stealth;				///< ParticleSystem::getStealthFade of the last system seen at it
	Real lifeFade;			///< fieldFade's answer at the last sync point
};

static const Int	FIELD_GRACE						= LOGICFRAMES_PER_SECOND;			///< logic frames a field with no object outlives its systems
static const Real	FIELD_STACK						= 2.5f;		///< puddles' worth of cover a spot may add up to
static const Real	FIELD_SATURATE				= 0.6f;		///< share of the weakest channel taken out of the colour

static std::map<FieldKey, FieldState>	s_fieldStates;
struct FieldSystemOwner { ObjectID object; unsigned pass; };
static std::map<ParticleSystemID, FieldSystemOwner> s_fieldSystemOwners;
static unsigned s_fieldCollectionPass = 0;

static bool fieldCloudsSupported()
{
	const bool newDevice = !s_fieldCloudCapabilityKnown;
	if (newDevice) {
		s_fieldCloudCapability = HazardCloud::Supported();
		s_fieldCloudCapabilityKnown = true;
	}
	// The Classic graphics setting is the game's own picture, and its clouds.
	const bool volumetric = s_fieldCloudCapability && TheGlobalData
		&& TheGlobalData->m_cloudStyle == 1 && !TheGlobalData->m_classicGraphics;
	// The programs every layer shares are built here, on the shell map or a match's first frame,
	// or the frame the style is switched: built by the first hazard instead, a program the shader
	// cache lacked froze that frame for three and a half seconds of FXC.
	if (volumetric && (newDevice || !s_fieldCloudsSupported))
		if (std::unique_ptr<HazardCloud::Layer> layer = HazardCloud::CreateLayer())
			layer->initialize();
	// A new device, or the style switched: destroy keys before changing their
	// comparator's mode.
	if (volumetric != s_fieldCloudsSupported) {
		s_fieldStates.clear();
		s_fieldSystemOwners.clear();
		s_fieldCloudsSupported = volumetric;
	}
	return s_fieldCloudsSupported;
}
static Bool s_fieldSkipNextFadeIn = FALSE;

/** One puddle of this frame's fields, before it is laid. */
struct FieldPuddle
{
	Real x, y, size, angle;
	Real bright;				///< its own share of the field's colour
	Real fade;					///< its field's fade
	Real overlap;				///< how many puddles, itself included, cover its middle
};

/** One field of this frame, before it is laid. */
struct FieldDraw
{
	FieldBatch batch;
	Coord3D centre;
	Bool onGround;
	Real fade;
	Bool visible;				///< inside the view; one outside is collected only for its overlap
	RGBColor color;
	AsciiString texture;
	Int firstPuddle, puddleCount;
	Bool glowing;				///< a warm hue, radiation: lit rather than stained, and it bubbles
	Real radius;
	UnsignedInt seed;		///< the place's hash, for its bubbles
};

/** Where one field's puddles lie this frame, for the overlap. */
struct FieldBounds
{
	Real minX, maxX, minY, maxY;	///< of the puddles' centres
	Real maxSize;									///< the largest puddle's half width
};

static std::vector<FieldPuddle>	s_fieldPuddles;
static std::vector<FieldDraw>		s_fieldDraws;
static std::vector<FieldBounds>	s_fieldBounds;	///< one a field draw, in their order

/** The CLEANUP_HAZARD object with a LifetimeUpdate standing on a field's centre, if there is one. */
static ObjectID findFieldObject( const Coord3D &centre, Int palette = -1 )
{
	static NameKeyType key_LifetimeUpdate = NAMEKEY( "LifetimeUpdate" );
	ObjectID found = INVALID_ID;
	Real best = FIELD_OBJECT_REACH * FIELD_OBJECT_REACH;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!obj->isKindOf( KINDOF_CLEANUP_HAZARD ))
			continue;
		if (s_fieldCloudsSupported) {
			if (obj->isDestroyed() || obj->isEffectivelyDead())
				continue;
			if (palette >= 0 && HazardCloudPolicy::paletteForOwner(obj->getTemplate()->getName().str()) != palette)
				continue;
		}
		const Coord3D *pos = obj->getPosition();
		if (s_fieldCloudsSupported && WWMath::Fabs(pos->z - centre.z) > FIELD_OBJECT_REACH)
			continue;
		const Real dx = pos->x - centre.x, dy = pos->y - centre.y;
		const Real d = dx * dx + dy * dy;
		const bool nearer = s_fieldCloudsSupported ? (d < best || (found == INVALID_ID && d <= best)) : d <= best;
		if (nearer && obj->findUpdateModule( key_LifetimeUpdate ) != NULL)
		{
			best = d;
			found = obj->getID();
		}
	}
	return found;
}

/** The owner of the hazard cloud already standing at this place, living or not.  A field's object
	* fires its weapon on the frame it dies, and that last system finds no living owner: left alone it
	* started a fresh cloud at full strength over the ended one, ten seconds of smoke after the hazard
	* was gone.  It belongs to the field it was fired into. */
static ObjectID standingFieldOwner( Int x, Int y, Int z, Int palette )
{
	for (std::map<FieldKey, FieldState>::const_iterator st = s_fieldStates.begin(); st != s_fieldStates.end(); ++st)
	{
		const FieldKey &key = st->first;
		if (key.owner != INVALID_ID && key.palette == palette && key.x == x && key.y == y && key.z == z)
			return key.owner;
	}
	return INVALID_ID;
}

/** The middle of a random range. */
static inline Real midValue( const GameClientRandomVariable &v )
{
	return 0.5f * (v.getMinimumValue() + v.getMaximumValue());
}

/** A field's fade this frame, from its object's remaining life or its particles'.
	*
	* Under the fog the pool goes out, as the retail one did: FXList::doFXPos plays nothing where the
	* watcher's cell is not clear, so no new puddles came and the old ones died off in two seconds.
	* A Scud Storm's reveal running out over its own fields did exactly that 32 seconds into their
	* 45.  It is asked here of the cell directly rather than read off the systems stopping, which
	* left the pool at full strength for the 30 frames the systems were given to come back. */
static Real fieldFade( FieldState &state, Int now )
{
	static NameKeyType key_LifetimeUpdate = NAMEKEY( "LifetimeUpdate" );
	// A capable backend follows the owner's life between sparse emitters.
	// Otherwise retain the upstream emitter grace, including for bound owners.
	Bool lost = ((!s_fieldCloudsSupported || state.object == INVALID_ID) && now - state.seen > FIELD_GRACE)
		|| ThePartitionManager->getShroudStatusForPlayer( TheObserverCamera.getShroudPlayerIndex(), &state.centre ) != CELLSHROUD_CLEAR;
	Int remaining = state.particleEnd - now;
	if (!lost && state.object != INVALID_ID)
	{
		Object *obj = TheGameLogic->findObjectByID( state.object );
		LifetimeUpdate *life = obj ? (LifetimeUpdate *)obj->findUpdateModule( key_LifetimeUpdate ) : NULL;
		if (life)
			remaining = (Int)life->getDieFrame() - now;
		else
			lost = TRUE;	// cleaned up early, or died
	}
	if (lost)
	{
		// the pool goes out over the same three seconds as at the end of its life, from where it was
		if (state.gone < 0)
		{
			state.gone = now;
			state.goneFade = state.shown;
		}
		const Real left = 1.0f - (Real)(now - state.gone) / FIELD_FADE_GONE;
		state.shown = WWMath::Max( left, 0.0f ) * state.goneFade;
		return state.shown;
	}
	if (state.gone >= 0)
	{
		// out of the fog again: up from where it had got to
		state.born = now - REAL_TO_INT_FLOOR( state.shown * FIELD_FADE_IN );
		state.gone = -1;
	}
	const Real fadeIn = WWMath::Clamp( (Real)(now - state.born) / FIELD_FADE_IN, 0.0f, 1.0f );
	const Real fadeOut = WWMath::Clamp( (Real)remaining / FIELD_FADE_OUT, 0.0f, 1.0f );
	state.shown = fadeIn * fadeOut;
	return state.shown;
}

namespace {
// A busy battlefield has a fixed visual memory/draw budget. Fields which cannot
// be admitted keep the ordinary ground effect, including unknown mod templates.
constexpr unsigned MaxCloudLayers = 8;
constexpr std::uint64_t CloudCacheBudget = 192ull * 1024 * 1024;
struct CloudLayer {
	FieldKey key = {};
	std::unique_ptr<HazardCloud::Layer> renderer;
	unsigned used = 0;
	float terrainChecked = -100;
	float terrainReach = -1;
	bool terrainValid = false;
};
std::array<CloudLayer, MaxCloudLayers> s_cloudLayers;
unsigned s_cloudPass = 0, s_cloudDraws = 0;
struct CloudWalker { Coord3D position; HazardCloud::Body body; unsigned frame; };
std::map<ObjectID, CloudWalker> s_cloudWalkers;
std::vector<HazardCloud::Body> s_cloudBodies;
unsigned s_cloudBodyFrame = ~0u;

bool isCloudMainPass()
{
	return !ShaderClass::Is_Backface_Culling_Inverted()
		&& W3DDisplay::m_3DScene
		&& W3DDisplay::m_3DScene->getCustomPassMode() == SCENE_PASS_DEFAULT;
}

bool modernCloudsEnabled()
{
	return s_fieldCloudsSupported && isCloudMainPass();
}

void releaseCloudLayers()
{
	for (auto &slot : s_cloudLayers) slot = CloudLayer{};
	s_cloudWalkers.clear();
	s_cloudBodies.clear();
	s_cloudBodyFrame = ~0u;
}

void beginCloudPass(unsigned now)
{
	if (!s_fieldCloudsSupported) {
		releaseCloudLayers();
		return;
	}
	// A water reflection/custom pass must not evict main-view caches or wakes.
	if (!modernCloudsEnabled()) return;
	++s_cloudPass;
	s_cloudDraws = 0;
}

void updateCloudBodies(unsigned now)
{
	if (s_cloudBodyFrame == now) return;
	s_cloudBodies.clear();
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject()) {
		if (!obj->isKindOf(KINDOF_INFANTRY) || obj->isDestroyed() || obj->isEffectivelyDead()) continue;
		Drawable *draw = obj->getDrawable();
		const Coord3D &pos = *obj->getPosition();
		// Invisible infantry must not disclose themselves through a smoke wake.
		if (!draw || draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud()
			|| draw->getStealthLook() != STEALTHLOOK_NONE || draw->getEffectiveOpacity() < .99f
			|| ThePartitionManager->getShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(), &pos) != CELLSHROUD_CLEAR)
			continue;
		HazardCloud::Body body = {};
		body.x = pos.x; body.y = pos.y; body.z = pos.z;
		body.radius = std::clamp(obj->getGeometryInfo().getBoundingCircleRadius(), 1.f, 12.f);
		body.height = std::clamp(obj->getGeometryInfo().getMaxHeightAbovePosition(), 4.f, 30.f);
		auto old = s_cloudWalkers.find(obj->getID());
		if (old != s_cloudWalkers.end() && now > old->second.frame && now - old->second.frame <= LOGICFRAMES_PER_SECOND / 2) {
			const float dt = float(now - old->second.frame) / LOGICFRAMES_PER_SECOND;
			body.vx = (pos.x - old->second.position.x) / dt;
			body.vy = (pos.y - old->second.position.y) / dt;
			if (body.vx * body.vx + body.vy * body.vy > 100.f * 100.f) body.vx = body.vy = 0;
		}
		s_cloudWalkers[obj->getID()] = {pos, body, now};
		s_cloudBodies.push_back(body);
	}
	for (auto it = s_cloudWalkers.begin(); it != s_cloudWalkers.end();) {
		if (it->second.frame != now) it = s_cloudWalkers.erase(it); else ++it;
	}
	s_cloudBodyFrame = now;
}

bool updateCloudTerrain(CloudLayer &slot, const HazardCloud::FieldState &field, bool onGround)
{
	if (!onGround) { slot.renderer->setTerrain(0, nullptr); return true; }
	constexpr unsigned side = HazardCloud::TerrainGridSide;
	std::array<float, side * side> heights;
	const float reach = field.radius + field.fringe + 18.f;
	float low = field.ground.z, high = field.ground.z;
	for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
		const float dx = -reach + 2 * reach * x / (side - 1), dy = -reach + 2 * reach * y / (side - 1);
		const float z = TheTerrainRenderObject->getHeightMapHeight(field.ground.x + dx, field.ground.y + dy, NULL);
		if (!std::isfinite(z)) return false;
		heights[y * side + x] = z;
		low = (std::min)(low, z); high = (std::max)(high, z);
	}
	slot.renderer->setTerrain(0, high - low > .001f ? heights.data() : nullptr);
	return true;
}

/** The camera the scene is drawn from, in the device's layout, from the camera itself: the
	* device's transforms can be whatever the last draw left until the next state flush. */
HazardCloud::Camera cloudCamera(CameraClass &camera)
{
	HazardCloud::Camera result;
	Matrix4x4 projection;
	camera.Get_D3D_Projection_Matrix(&projection);
	Matrix3D view;
	camera.Get_View_Matrix(&view);
	const Matrix4x4 d3dProjection = projection.Transpose();
	const Matrix4x4 d3dView = Matrix4x4(view).Transpose();
	memcpy(result.view, &d3dView, sizeof(result.view));
	memcpy(result.projection, &d3dProjection, sizeof(result.projection));
	// as CameraClass::Apply sets the device's viewport
	int width, height, bits;
	bool windowed;
	WW3D::Get_Render_Target_Resolution(width, height, bits, windowed);
	const ViewportClass &viewport = camera.Get_Viewport();
	result.x = (unsigned)(viewport.Min.X * (float)width);
	result.y = (unsigned)(viewport.Min.Y * (float)height);
	result.width = (unsigned)((viewport.Max.X - viewport.Min.X) * (float)width);
	result.height = (unsigned)((viewport.Max.Y - viewport.Min.Y) * (float)height);
	camera.Get_Depth_Range(&result.minZ, &result.maxZ);
	return result;
}

bool drawModernCloud(const FieldKey &key, FieldState &state, float fade, float seconds, int now,
	const HazardCloud::Camera &camera)
{
	if (!modernCloudsEnabled() || key.palette < 0) return false;
	if (s_cloudDraws >= MaxCloudLayers) return false;
	const float age = (std::max)(0.f, float(now - state.visualBorn) / LOGICFRAMES_PER_SECOND);
	// the owner's life was read at the sync point (updateGroundFields), not beside the tick
	const int remaining = state.cloudDieFrame >= 0 ? state.cloudDieFrame - now : state.particleEnd - now;
	const float left = (std::max)(0.f, float(remaining) / LOGICFRAMES_PER_SECOND);
	const float lifetime = age + left;
	const auto buildup = HazardCloudPolicy::visualBuildup(key.palette, state.radius >= 100.f, age, lifetime);
	HazardCloud::FieldState field = {};
	field.ground = state.centre;
	const float ground = TheTerrainRenderObject->getHeightMapHeight(state.centre.x, state.centre.y, NULL);
	const bool onGround = state.centre.z - ground < FIELD_ON_GROUND;
	if (onGround) field.ground.z = ground;
	field.radius = state.radius;
	field.height = 18.61f; // Approved infantry-height cloud from the shared preview.
	field.palette = float(key.palette);
	field.fringe = HazardCloudPolicy::visualFringe(state.size);
	field.formation = buildup.formation;
	field.release = buildup.release;
	const float endFade = std::clamp(left / (std::max)(.1f, (std::min)(3.f, lifetime * .25f)), 0.f, 1.f);
	field.density = buildup.density * endFade * state.stealth;
	if (state.gone >= 0) {
		if (state.cloudGoneAt != state.gone) {
			state.cloudGoneAt = state.gone;
			state.cloudGoneDensity = state.cloudShown;
			state.cloudGoneFormation = state.cloudFormation;
		}
		// Do not let a young cloud keep thickening after it becomes hidden or
		// its owner is removed. Fade from the last actually shown appearance.
		field.density = state.cloudGoneDensity * std::clamp(1.f - float(now - state.gone) / FIELD_FADE_GONE, 0.f, 1.f);
		field.formation = state.cloudGoneFormation;
		field.release = 0;
	} else {
		if (state.cloudGoneAt >= 0) {
			state.cloudRevealAt = now;
			state.cloudRevealFrom = state.cloudShown;
			state.cloudGoneAt = -1;
		}
		const float reveal = std::clamp(float(now - state.cloudRevealAt) / FIELD_FADE_IN, 0.f, 1.f);
		field.density = state.cloudRevealFrom + (field.density - state.cloudRevealFrom) * reveal;
	}
	state.cloudShown = field.density;
	state.cloudFormation = field.formation;
	if (!HazardCloudPolicy::validField(field.ground.x, field.ground.y, field.ground.z, field.radius,
		field.height, field.density, field.formation, field.fringe, key.palette)) return false;
	// A fully faded modern field is intentionally invisible. Drawing Classic
	// here would flash its puddle back during buildup/cleanup boundary frames.
	if (field.density <= .00001f) return true;
	CloudLayer *slot = nullptr;
	for (auto &candidate : s_cloudLayers)
		if (candidate.renderer && !(key < candidate.key) && !(candidate.key < key)) { slot = &candidate; break; }
	if (!slot) {
		for (auto &candidate : s_cloudLayers) if (!candidate.renderer) { slot = &candidate; break; }
		if (!slot) for (auto &candidate : s_cloudLayers)
			if (candidate.used != s_cloudPass && (!slot || candidate.used < slot->used)) slot = &candidate;
		if (!slot) return false;
		*slot = CloudLayer{};
		slot->key = key;
	}
	const auto wanted = HazardCloud::Layer::estimateCacheBytes(&field, 1);
	std::uint64_t used = 0;
	for (const auto &other : s_cloudLayers) if (&other != slot && other.renderer) used += other.renderer->estimatedCacheBytes();
	if (wanted > CloudCacheBudget) return false;
	while (used > CloudCacheBudget - wanted) {
		CloudLayer *oldest = nullptr;
		for (auto &other : s_cloudLayers)
			if (&other != slot && other.renderer && other.used != s_cloudPass
				&& (!oldest || other.used < oldest->used)) oldest = &other;
		if (!oldest) return false;
		used -= oldest->renderer->estimatedCacheBytes();
		*oldest = CloudLayer{};
	}
	if (!slot->renderer) {
		slot->renderer = HazardCloud::CreateLayer();
		if (!slot->renderer || !slot->renderer->initialize()) { slot->renderer.reset(); return false; }
		const float reach = field.radius + field.fringe + 24.f;
		slot->renderer->setFlowDomain(field.ground.x - reach, field.ground.y - reach, field.ground.x + reach, field.ground.y + reach);
	}
	slot->renderer->setFields(&field, 1);
	if (seconds < slot->terrainChecked || seconds - slot->terrainChecked >= 1.f
		|| slot->terrainReach != field.radius + field.fringe) {
		slot->terrainValid = updateCloudTerrain(*slot, field, onGround);
		slot->terrainChecked = seconds;
		slot->terrainReach = field.radius + field.fringe;
	}
	if (!slot->terrainValid) return false;
	std::array<HazardCloud::Body, 32> bodies;
	unsigned count = 0;
	for (const auto &body : s_cloudBodies) {
		const float dx = body.x - field.ground.x, dy = body.y - field.ground.y;
		const float reach = field.radius + field.fringe + body.radius;
		const float localGround = onGround ? TheTerrainRenderObject->getHeightMapHeight(body.x, body.y, NULL) : field.ground.z;
		if (dx * dx + dy * dy <= reach * reach && std::fabs(body.z - localGround) < field.height)
			if (count < bodies.size()) bodies[count++] = body;
	}
	slot->renderer->setBodies(bodies.data(), count);
	slot->renderer->setHeatEnabled(TheGlobalData->m_useHeatEffects != FALSE);
	slot->renderer->update(seconds);
	slot->used = s_cloudPass;
	if (!slot->renderer->render(camera)) return false;
	++s_cloudDraws;
	return true;
}
} // namespace

/** This frame's fields from their systems, and each one's fade, at the sync point before the render:
	* finding a field's object, its life and the fog over it read the logic (LOGIC-THREAD.md phase 3).
	* `systems` is every isGroundField system in list order, empty ones too. */
static void updateGroundFields( const std::vector<ParticleSystem *> &systems )
{
	const Int now = (Int)TheGameLogic->getFrame();
	// An explicit cache refresh may need an immediate comparison while paused.
	// Consume this once, even if this sync point has no fields, so later gameplay
	// emitters always retain the normal fade-in unless explicitly refreshed again.
	const bool cloudsSupported = fieldCloudsSupported();
	const Bool skipFadeIn = cloudsSupported && s_fieldSkipNextFadeIn;
	s_fieldSkipNextFadeIn = FALSE;
	if (cloudsSupported) ++s_fieldCollectionPass;
	std::map<std::tuple<Int, Int, Int, Int>, ObjectID> fieldOwners;

	// where the fields are this frame, and what each one looks like
	for (size_t f = 0; f < systems.size(); ++f)
	{
		ParticleSystem *sys = systems[ f ];
		if (cloudsSupported && sys->getInitialDelayLeft() > 0) continue;

		Coord3D pos;
		sys->getPosition( &pos );
		Int palette = -1;
		ObjectID owner = INVALID_ID;
		if (cloudsSupported) {
			palette = HazardCloudPolicy::paletteForTemplate(sys->getTemplate()->getName().str());
			const auto ownerKey = std::make_tuple(REAL_TO_INT_FLOOR(pos.x + .5f), REAL_TO_INT_FLOOR(pos.y + .5f),
				REAL_TO_INT_FLOOR(pos.z + .5f), palette);
			auto binding = s_fieldSystemOwners.find(sys->getSystemID());
			if (palette >= 0 && binding != s_fieldSystemOwners.end()) owner = binding->second.object;
			if (owner == INVALID_ID) {
				auto ownerEntry = fieldOwners.find(ownerKey);
				if (ownerEntry == fieldOwners.end()) {
					ObjectID found = findFieldObject(pos, palette);
					if (found == INVALID_ID && palette >= 0)
						found = standingFieldOwner(std::get<0>(ownerKey), std::get<1>(ownerKey), std::get<2>(ownerKey), palette);
					ownerEntry = fieldOwners.emplace(ownerKey, found).first;
				}
				owner = ownerEntry->second;
			}
			if (palette >= 0) {
				// A surviving emitter retains its hazard identity after cleanup.
				// Otherwise its last particles would create a fresh, growing cloud.
				s_fieldSystemOwners[sys->getSystemID()] = {owner, s_fieldCollectionPass};
			}
		}
		FieldKey key = { sys->getTemplate(), REAL_TO_INT_FLOOR( pos.x + 0.5f ), REAL_TO_INT_FLOOR( pos.y + 0.5f ),
			cloudsSupported ? REAL_TO_INT_FLOOR(pos.z + .5f) : 0, palette, owner };
		const ParticleSystemInfo::EmissionVolumeType volume = sys->getEmisionVolumeType();
		const Real radius = volume == ParticleSystemInfo::SPHERE ? sys->m_emissionVolume.sphere.radius
			: (volume == ParticleSystemInfo::CYLINDER ? sys->m_emissionVolume.cylinder.radius : 0.0f);
		Int life = sys->isSystemForever() ? FIELD_FADE_OUT : (Int)sys->getSystemLifetimeLeft();
		life += REAL_TO_INT_CEIL( sys->m_lifetime.getMaximumValue() );
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
			life = WWMath::Max( life, (Int)p->getLifetimeLeft() );

		std::map<FieldKey, FieldState>::iterator st = s_fieldStates.find( key );
		// A blast FX can precede its cleanup object within the same tick. Adopt
		// that unbound visual when the owner arrives rather than drawing it twice.
		if (cloudsSupported && st == s_fieldStates.end() && palette >= 0 && owner != INVALID_ID) {
			for (auto orphan = s_fieldStates.begin(); orphan != s_fieldStates.end(); ++orphan) {
				const FieldKey &old = orphan->first;
				if (old.owner == INVALID_ID && old.palette == palette && old.x == key.x && old.y == key.y && old.z == key.z) {
					FieldState adopted = orphan->second;
					adopted.object = owner;
					s_fieldStates.erase(orphan);
					st = s_fieldStates.emplace(key, adopted).first;
					break;
				}
			}
		}
		if (st == s_fieldStates.end() || st->second.seen != now)
		{
			if (st == s_fieldStates.end())
			{
				FieldState fresh = {};
				fresh.born = now - (skipFadeIn ? FIELD_FADE_IN : 0);
				fresh.visualBorn = fresh.born;
				fresh.cloudGoneAt = -1;
				fresh.cloudRevealAt = now - FIELD_FADE_IN;
				fresh.cloudDieFrame = -1;
				fresh.seen = now;
				fresh.object = cloudsSupported ? owner : findFieldObject( pos );
				fresh.gone = -1;
				fresh.goneFade = 0.0f;
				fresh.shown = 0.0f;
				st = s_fieldStates.insert( std::make_pair( key, fresh ) ).first;
			}
			FieldState &state = st->second;
			state.seen = now;
			state.particleEnd = now + life;
			state.centre = pos;
			state.radius = radius;

			// a puddle the size a particle reaches halfway through its life
			const Real half = 0.5f * midValue( sys->m_lifetime );
			const Real rate = midValue( sys->m_sizeRate );
			const Real damping = midValue( sys->m_sizeRateDamping );
			const Real growth = damping < 1.0f ? rate * (1.0f - powf( damping, half )) / (1.0f - damping) : rate * half;
			state.size = WWMath::Max( midValue( sys->m_startSize ) + growth, 1.0f );

			// the template's brightest keyframe is the field's colour
			state.color = sys->m_colorKey[ 0 ].color;
			for (Int k = 1; k < MAX_KEYFRAMES; ++k)
			{
				const RGBColor &c = sys->m_colorKey[ k ].color;
				if (c.red + c.green + c.blue > state.color.red + state.color.green + state.color.blue)
					state.color = c;
			}
			state.shader = sys->getShaderType();
			state.texture = sys->getParticleTypeName();
		}
		else
		{
			FieldState &state = st->second;
			state.radius = WWMath::Max( state.radius, radius );
			state.particleEnd = WWMath::Max( state.particleEnd, now + life );
		}
		st->second.stealth = sys->getStealthFade();
	}
	for (auto binding = s_fieldSystemOwners.begin(); binding != s_fieldSystemOwners.end();) {
		if (binding->second.pass != s_fieldCollectionPass) binding = s_fieldSystemOwners.erase(binding);
		else ++binding;
	}

	for (std::map<FieldKey, FieldState>::iterator st = s_fieldStates.begin(); st != s_fieldStates.end(); )
	{
		FieldState &state = st->second;

		// Kept while its systems are, even faded out: the systems outlive the object by a few
		// seconds, and a field forgotten under them would be found again as a new one.
		state.lifeFade = fieldFade( state, now );
		const Bool stale = now - state.seen > FIELD_GRACE;
		if ((stale && state.lifeFade <= 0.0f) || now < state.seen)
		{
			// out, or a frame count from before a load
			s_fieldStates.erase( st++ );
			continue;
		}
		++st;
	}
	if (cloudsSupported) {
		// The hazard clouds' owners' lifetimes and the infantry wading through them read the logic, so
		// they are taken here with the rest, not in the render beside the tick.
		static NameKeyType key_cloudLife = NAMEKEY("LifetimeUpdate");
		for (std::map<FieldKey, FieldState>::iterator st = s_fieldStates.begin(); st != s_fieldStates.end(); ++st) {
			FieldState &state = st->second;
			state.cloudDieFrame = -1;
			if (state.object == INVALID_ID || st->first.palette < 0) continue;
			Object *obj = TheGameLogic->findObjectByID(state.object);
			if (obj) if (auto *life = static_cast<LifetimeUpdate *>(obj->findUpdateModule(key_cloudLife)))
				state.cloudDieFrame = Int(life->getDieFrame());
		}
		updateCloudBodies((unsigned)now);
	}
}

/** Lays the fields updateGroundFields found on the terrain into the field buffers; drawGroundFields
	* draws them.  Returns the puddles laid. */
static Int collectGroundFields( const AABoxClass &view, CameraClass &sceneCamera )
{
	const Int now = (Int)TheGameLogic->getFrame();
	// between the last two logic ticks, as the models are, so a pulse does not step at the logic rate
	const Real seconds = ((Real)now + (TheSmoothMotionActive ? TheSmoothMotionAlpha : 0.0f)) / LOGICFRAMES_PER_SECOND;
	s_fieldPuddles.clear();
	s_fieldDraws.clear();
	beginCloudPass(now);
	const HazardCloud::Camera camera = cloudCamera( sceneCamera );

	Int laid = 0;
	for (std::map<FieldKey, FieldState>::iterator st = s_fieldStates.begin(); st != s_fieldStates.end(); ++st)
	{
		const FieldKey &key = st->first;
		FieldState &state = st->second;

		// a field on a stealthed thing goes with it, as its other particles do
		const Real fade = state.lifeFade * state.stealth;
		if (fade <= 0.0f)
			continue;

		// a field off screen is still collected, as its puddles dim the ones it overlaps on screen
		const Real size = state.size;
		Real reach = state.radius + size;
		Bool groundVolume = FALSE;
		if (modernCloudsEnabled() && key.palette >= 0) {
			reach = state.radius + HazardCloudPolicy::visualFringe(size) + 18.f;
			groundVolume = TRUE;
		}
		const Bool visible = WWMath::Fabs( state.centre.x - view.Center.X ) <= view.Extent.X + reach
			&& WWMath::Fabs( state.centre.y - view.Center.Y ) <= view.Extent.Y + reach
			&& (groundVolume || WWMath::Fabs( state.centre.z - view.Center.Z ) <= view.Extent.Z + reach);
		if (visible) {
			if (drawModernCloud(key, state, fade, seconds, now, camera)) {
				// Preserve the client's existing area-effect particle ceiling even
				// though the replacement does not build the old puddle geometry.
				const Real spread = state.radius / size;
				laid += WWMath::Clamp_Int((Int)ceilf(FIELD_COVER * spread * spread), 1, FIELD_MAX_PUDDLES);
				if (state.color.red >= state.color.green && state.color.red >= state.color.blue)
					laid += WWMath::Clamp_Int((Int)(state.radius / FIELD_BUBBLE_SPACING), 1, FIELD_MAX_BUBBLES);
				continue;
			}
		}

		FieldBatch batch;
		if (!particlePresetShader( state.shader, &batch.glow ))
			continue;
		batch.glow.Set_Cull_Mode( ShaderClass::CULL_MODE_DISABLE );
		batch.glow.Set_Primary_Gradient( ShaderClass::GRADIENT_MODULATE );
		batch.glow.Set_Texturing( ShaderClass::TEXTURING_ENABLE );
		// dst * (1 - texel * colour): black texels leave the ground alone, as they add nothing to it
		batch.stain = batch.glow;
		batch.stain.Set_Src_Blend_Func( ShaderClass::SRCBLEND_ZERO );
		batch.stain.Set_Dst_Blend_Func( ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR );
		batch.stained = state.shader == ParticleSystemInfo::ADDITIVE;
		batch.texture = NULL;

		FieldDraw draw;
		draw.batch = batch;
		draw.centre = state.centre;
		draw.onGround = state.centre.z
			- TheTerrainRenderObject->getHeightMapHeight( state.centre.x, state.centre.y, NULL ) < FIELD_ON_GROUND;
		draw.fade = fade;
		draw.visible = visible;
		draw.texture = state.texture;

		// More saturated than the keyframe: pulling the weakest channel down keeps the hue and keeps
		// the pool from going mint where the puddles overlap.
		const RGBColor &key_color = state.color;
		const Real low = WWMath::Min( key_color.red, WWMath::Min( key_color.green, key_color.blue ) ) * FIELD_SATURATE;
		const Real peak = WWMath::Max( key_color.red, WWMath::Max( key_color.green, key_color.blue ) );
		const Real lift = peak > low ? peak / (peak - low) : 1.0f;
		draw.color.red = (key_color.red - low) * lift;
		draw.color.green = (key_color.green - low) * lift;
		draw.color.blue = (key_color.blue - low) * lift;

		// Radiation is the one warm field.  Its orange stained the ground brown and glowed at 0.7 of a
		// dim keyframe, which read as a scorch; it is lit to full strength instead and leaves the
		// ground under it alone.
		draw.glowing = draw.color.red >= draw.color.green && draw.color.red >= draw.color.blue;
		if (draw.glowing)
		{
			const Real full = WWMath::Max( draw.color.red, 1.0f / 255.0f );
			draw.color.red = 1.0f;
			draw.color.green /= full;
			draw.color.blue /= full;
			draw.batch.stained = FALSE;
		}
		draw.radius = state.radius;

		const Real spread = state.radius / size;
		const Int count = WWMath::Clamp_Int( (Int)ceilf( FIELD_COVER * spread * spread ), 1, FIELD_MAX_PUDDLES );

		// the same scatter every frame: a little generator seeded by the place
		UnsignedInt seed = (UnsignedInt)key.x * 73856093u ^ (UnsignedInt)key.y * 19349663u ^ 0x9E3779B9u;
		draw.seed = seed;
		draw.firstPuddle = (Int)s_fieldPuddles.size();
		draw.puddleCount = count;
		for (Int i = 0; i < count; ++i)
		{
			Real u[ 4 ];
			for (Int k = 0; k < 4; ++k)
			{
				seed = seed * 1664525u + 1013904223u;
				u[ k ] = (Real)(seed >> 8) * (1.0f / 16777216.0f);
			}
			const Real r = count == 1 ? 0.0f : state.radius * sqrtf( u[ 0 ] );
			const Real a = u[ 1 ] * 2.0f * PI;
			FieldPuddle puddle;
			// its own beat, from a second generator so the scatter stays where it was
			UnsignedInt h = seed * 2654435761u;
			Real w[ 3 ];
			for (Int k = 0; k < 3; ++k)
			{
				h = h * 1664525u + 1013904223u;
				w[ k ] = (Real)(h >> 8) * (1.0f / 16777216.0f);
			}
			const Real phase = w[ 0 ] * 2.0f * PI;
			const Real cycle = 2.0f * PI * seconds / (FIELD_PULSE_PERIOD * (0.6f + 0.8f * w[ 1 ]));
			const Real pulse = 0.5f + 0.5f * WWMath::Sin( cycle + phase );
			puddle.x = state.centre.x + r * WWMath::Cos( a );
			puddle.y = state.centre.y + r * WWMath::Sin( a );
			puddle.size = size * (1.0f + FIELD_BREATHE * WWMath::Sin( 0.7f * cycle + w[ 2 ] * 2.0f * PI ));
			puddle.angle = u[ 2 ] * 2.0f * PI;
			puddle.bright = (0.8f + 0.2f * u[ 3 ]) * (1.0f - FIELD_PULSE * pulse);
			puddle.fade = fade;
			puddle.overlap = 0.0f;
			s_fieldPuddles.push_back( puddle );
		}
		s_fieldDraws.push_back( draw );
	}

	// How many puddles cover each one's middle, over every field: four Scud fields on one spot and the
	// thick middle of each, added up, went past white.  Each puddle is dimmed by how far its cover
	// goes over FIELD_STACK, so the whole pool tops out at about that many puddles' worth.  A
	// neighbour counts at its own fade, so a field coming up or going out dims the others with it
	// rather than in one frame.
	//
	// A field's puddles lie inside the box of their own centres and reach no further past it than its
	// largest puddle, so two fields whose boxes stand further apart on an axis than their two largest
	// puddles together add nothing to each other and their pairs are not tried.  The rest still sum in
	// puddle order, so every overlap comes out as it did; the margin is wide enough that no pair left
	// out could have rounded to a t above zero.
	// ponytail: every pair of near fields, a few hundred puddles on the map at most; a grid when a
	// map holds more
	const Int fieldCount = (Int)s_fieldDraws.size();
	s_fieldBounds.resize( fieldCount );
	for (Int d = 0; d < fieldCount; ++d)
	{
		FieldBounds &b = s_fieldBounds[ d ];
		const FieldPuddle &first = s_fieldPuddles[ s_fieldDraws[ d ].firstPuddle ];
		b.minX = b.maxX = first.x;
		b.minY = b.maxY = first.y;
		b.maxSize = first.size;
		for (Int i = s_fieldDraws[ d ].firstPuddle + 1; i < s_fieldDraws[ d ].firstPuddle + s_fieldDraws[ d ].puddleCount; ++i)
		{
			const FieldPuddle &p = s_fieldPuddles[ i ];
			b.minX = WWMath::Min( b.minX, p.x );	b.maxX = WWMath::Max( b.maxX, p.x );
			b.minY = WWMath::Min( b.minY, p.y );	b.maxY = WWMath::Max( b.maxY, p.y );
			b.maxSize = WWMath::Max( b.maxSize, p.size );
		}
	}
	for (Int a = 0; a < fieldCount; ++a)
	{
		const FieldBounds &ba = s_fieldBounds[ a ];
		const Int aEnd = s_fieldDraws[ a ].firstPuddle + s_fieldDraws[ a ].puddleCount;
		for (Int i = s_fieldDraws[ a ].firstPuddle; i < aEnd; ++i)
		{
			FieldPuddle &p = s_fieldPuddles[ i ];
			p.overlap = 1.0f;
			for (Int f = 0; f < fieldCount; ++f)
			{
				const FieldBounds &bf = s_fieldBounds[ f ];
				const Real apart = (ba.maxSize + bf.maxSize) * 1.01f + 0.01f;
				if (bf.minX - ba.maxX > apart || ba.minX - bf.maxX > apart
						|| bf.minY - ba.maxY > apart || ba.minY - bf.maxY > apart)
					continue;
				const Int fEnd = s_fieldDraws[ f ].firstPuddle + s_fieldDraws[ f ].puddleCount;
				for (Int j = s_fieldDraws[ f ].firstPuddle; j < fEnd; ++j)
				{
					const FieldPuddle &q = s_fieldPuddles[ j ];
					const Real reach = p.size + q.size;
					const Real dx = p.x - q.x, dy = p.y - q.y;
					const Real t = 1.0f - (dx * dx + dy * dy) / (reach * reach);
					if (j != i && t > 0.0f)
						p.overlap += t * t * q.fade;
				}
			}
		}
	}

	for (size_t d = 0; d < s_fieldDraws.size(); ++d)
	{
		FieldDraw &draw = s_fieldDraws[ d ];
		if (!draw.visible)
			continue;
		FieldBatch &batch = draw.batch;
		batch.firstChunk = (Int)s_fieldChunks.size();
		FieldChunk chunk = { (Int)s_fieldPos.size(), 0, (Int)s_fieldIndex.size(), 0 };
		for (Int i = draw.firstPuddle; i < draw.firstPuddle + draw.puddleCount; ++i)
		{
			const FieldPuddle &puddle = s_fieldPuddles[ i ];
			const Real stack = puddle.overlap > FIELD_STACK ? FIELD_STACK / puddle.overlap : 1.0f;
			const Real bright = (draw.glowing ? FIELD_GAIN : FIELD_GAIN_TOXIN) * draw.fade * puddle.bright * stack;
			const Real red = draw.color.red * bright, green = draw.color.green * bright, blue = draw.color.blue * bright;
			const unsigned glowColor = DX8Wrapper::Convert_Color_Clamp( Vector4( red, green, blue, 1.0f ) );
			unsigned stainColor = 0;
			const Real peak = WWMath::Max( red, WWMath::Max( green, blue ) );
			if (batch.stained && peak > 0.0f)
			{
				// once more by the fade, so the stain is gone with the glow and not after it
				const Real inv = 1.0f / peak;
				const Real k = peak * draw.fade;
				stainColor = DX8Wrapper::Convert_Color_Clamp( Vector4(
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - red * inv)),
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - green * inv)),
					k * (FIELD_STAIN_DARKEN + FIELD_STAIN_TINT * (1.0f - blue * inv)),
					1.0f ) );
			}
			addFieldPuddle( chunk, puddle.x, puddle.y, draw.centre.z, draw.onGround, puddle.size, puddle.angle,
				glowColor, stainColor );
			++laid;
		}

		// Radiation bubbles: a few small light blobs that swell out of the pool and pop, each slot on
		// its own beat and somewhere new every time.  They take no part in the overlap above.
		if (draw.glowing)
		{
			const Int bubbles = WWMath::Clamp_Int( (Int)(draw.radius / FIELD_BUBBLE_SPACING), 1, FIELD_MAX_BUBBLES );
			for (Int b = 0; b < bubbles; ++b)
			{
				UnsignedInt h = draw.seed ^ ((UnsignedInt)b * 2246822519u);
				h = h * 1664525u + 1013904223u;
				const Real period = FIELD_BUBBLE_PERIOD * (0.7f + 0.6f * (Real)(h >> 8) * (1.0f / 16777216.0f));
				h = h * 1664525u + 1013904223u;
				const Real t = seconds / period + (Real)(h >> 8) * (1.0f / 16777216.0f);
				const Real round = floorf( t );
				const Real life = t - round;

				// where this round's bubble comes up
				UnsignedInt p = h ^ ((UnsignedInt)(Int)round * 3266489917u);
				p = p * 1664525u + 1013904223u;
				const Real r = draw.radius * 0.9f * sqrtf( (Real)(p >> 8) * (1.0f / 16777216.0f) );
				p = p * 1664525u + 1013904223u;
				const Real a = (Real)(p >> 8) * (1.0f / 16777216.0f) * 2.0f * PI;

				// up over most of its life, gone in the last fifth; lighter and yellower than the pool
				const Real swell = life < 0.8f ? life / 0.8f : (1.0f - life) / 0.2f;
				const Real bright = FIELD_BUBBLE_GAIN * draw.fade * swell;
				const unsigned bubbleColor = DX8Wrapper::Convert_Color_Clamp( Vector4( bright,
					bright * (0.5f + 0.5f * draw.color.green), bright * (0.3f + 0.7f * draw.color.blue), 1.0f ) );
				addFieldPuddle( chunk, draw.centre.x + r * WWMath::Cos( a ), draw.centre.y + r * WWMath::Sin( a ),
					draw.centre.z, draw.onGround, FIELD_BUBBLE_SIZE * (0.4f + 0.6f * life), a, bubbleColor, 0 );
				++laid;
			}
		}
		s_fieldChunks.push_back( chunk );

		batch.chunkCount = (Int)s_fieldChunks.size() - batch.firstChunk;
		batch.texture = W3DDisplay::m_assetManager->Get_Texture( draw.texture.str() );
		s_fieldBatches.push_back( batch );
	}
	return laid;
}

/** Draws every field the frame collected: all their stains, then all their glows. */
static void drawGroundFields( void )
{
	if (s_fieldBatches.empty())
		return;

	VertexMaterialClass *material = VertexMaterialClass::Get_Preset( VertexMaterialClass::PRELIT_DIFFUSE );
	Matrix4x4 identity( true );
	DX8Wrapper::Set_Transform( D3DTS_WORLD, identity );
	DX8Wrapper::Set_Material( material );
	REF_PTR_RELEASE( material );

	for (Int pass = 0; pass < 2; ++pass)
	{
		const Bool stainPass = pass == 0;
		for (size_t b = 0; b < s_fieldBatches.size(); ++b)
		{
			const FieldBatch &batch = s_fieldBatches[ b ];
			if (stainPass && !batch.stained)
				continue;
			DX8Wrapper::Set_Texture( 0, batch.texture );
			for (Int k = batch.firstChunk; k < batch.firstChunk + batch.chunkCount; ++k)
			{
				const FieldChunk &fc = s_fieldChunks[ k ];
				DynamicIBAccessClass ibAccess( BUFFER_TYPE_DYNAMIC_DX8, (unsigned short)fc.indexCount );
				{
					DynamicIBAccessClass::WriteLockClass lock( &ibAccess );
					memcpy( lock.Get_Index_Array(), &s_fieldIndex[ fc.firstIndex ], fc.indexCount * sizeof( unsigned short ) );
				}
				DX8Wrapper::Set_Index_Buffer( ibAccess, 0 );
				if (stainPass)
					drawFieldPass( fc, s_fieldStain, batch.stain );
				else
					drawFieldPass( fc, s_fieldGlow, batch.glow );
			}
		}
	}

	for (size_t b = 0; b < s_fieldBatches.size(); ++b)
		s_fieldBatches[ b ].texture->Release_Ref();	// the draw state holds its own reference to the last one
	s_fieldBatches.clear();
}

W3DParticleSystemManager::W3DParticleSystemManager()
{
	m_pointGroup = NULL;
	m_streakLine = NULL;
	m_posBuffer = NULL;
	m_RGBABuffer = NULL;
	m_sizeBuffer = NULL;
	m_angleBuffer = NULL;
	m_frameBuffer = NULL;
	m_readyToRender = false;

	m_onScreenParticleCount = 0;

	m_frameViewValid = FALSE;
	m_sunMapCastersValid = FALSE;
	m_frameViewLogicFrame = 0;
	m_frameViewFrame = 0;
	m_frameViewParticles = 0;
	m_frameViewSystems = 0;
	m_frameViewNextID = INVALID_PARTICLE_SYSTEM_ID;

	// asked again on the main thread at each map's load and each draw; this may run on the logic's
	m_gpuSupported = FALSE;
	m_gpuCapacity = 0;
	m_gpuRefused = FALSE;
	m_gpuWritten = 0;
	m_gpuTail = 0;
	m_gpuPendingFirst = 0;
	m_gpuLive = 0;
	m_gpuDrainedFrame = 0;
	m_gpuFadesDirty = FALSE;
	m_gpuDirtyFirst = 0;
	m_gpuDirtyEnd = 0;
	m_gpuLastSystem = NULL;
	m_gpuLastSlot = GPU_NO_SLOT;
	m_gpuSliceCursor = 0;
	m_gpuTexturesFullTold = FALSE;
	m_gpuSpawned = 0;
	m_gpuEvicted = 0;
	m_gpuLogFrame = 0;

	m_pointGroup = NEW PointGroupClass();
	//m_streakLine = NULL;
	m_streakLine = NEW StreakLineClass();
	
	m_posBuffer = NEW_REF( ShareBufferClass<Vector3>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_posBuffer") );
	m_RGBABuffer = NEW_REF( ShareBufferClass<Vector4>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_RGBABuffer") );
	m_sizeBuffer = NEW_REF( ShareBufferClass<float>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_sizeBuffer") );
	m_angleBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_angleBuffer") );
	m_frameBuffer = NEW_REF( ShareBufferClass<uint8>, (MAX_POINTS_PER_GROUP, "W3DParticleSystemManager::m_frameBuffer") );
}

W3DParticleSystemManager::~W3DParticleSystemManager()
{
	releaseCloudResources();
	delete m_pointGroup;

//	W3DDisplay::m_3DScene->Remove_Render_Object( m_streakLine );

	if (m_streakLine)
	{
		REF_PTR_RELEASE(m_streakLine);
	}

	REF_PTR_RELEASE(m_posBuffer);
	REF_PTR_RELEASE(m_RGBABuffer);
	REF_PTR_RELEASE(m_sizeBuffer);
	REF_PTR_RELEASE(m_angleBuffer);
	REF_PTR_RELEASE(m_frameBuffer);

	resetGpuParticles();		// the texture references it holds
}

/** A reset can be followed by an update on the same logic frame, so the fires' frame alone would
	* not tell the lights taken before it from the ones after. */
void W3DParticleSystemManager::reset()
{
	ParticleSystemManager::reset();
	s_fireLightsValid = FALSE;
	s_firelightCount = 0;
	m_frameViewValid = FALSE;	// its systems are gone
	m_frameView.clear();
	m_frameFires.clear();
	m_frameFireFades.clear();
	m_fieldSystems.clear();
	m_sunMapCasters.clear();
	invalidateGroundFields();	// the object IDs it holds belong to the game that ended
	resetGpuParticles();			// the client frame starts again, and every system is gone
}

void W3DParticleSystemManager::invalidateGroundFields(bool fadeIn)
{
	// The layers' fields and wakes belong to the game that ended; the programs the
	// layers share stay, so the next map's first hazard compiles nothing.
	releaseCloudLayers();
	// Ground draw buffers are rebuilt by the next collect/draw; their texture
	// references are already released at the end of each draw. Only this map
	// persists the footprint after an emitter is removed or changes appearance.
	s_fieldStates.clear();
	s_fieldSystemOwners.clear();
	s_fieldSkipNextFadeIn = !fadeIn;
}

void W3DParticleSystemManager::releaseCloudResources()
{
	releaseCloudLayers();
	HazardCloud::ReleaseDevice();
	// Query the replacement device at the next sync point. The helper clears
	// keys before changing their ordering if support changed; unchanged support
	// retains field ages/owner bindings across a resize or device reset.
	s_fieldCloudCapabilityKnown = false;
}

/** Only for the device going: a mode change keeps the device, and with it the ring's particles,
	* where releaseCloudResources drops the clouds' screen sized targets. */
void W3DParticleSystemManager::releaseGpuResources()
{
	GpuParticles::ReleaseDevice();
}

/** LOGIC-THREAD.md phase 3.  A system's stealth fade asks its object's drawable, and a field asks for
	* its object, its life and the fog, so the frame view and the fields are taken here, before the
	* render; the render's own refreshFrameView finds the view still valid. */
void W3DParticleSystemManager::takeReadsForRender( void )
{
	refreshFrameView();
	updateGroundFields( m_fieldSystems );
}

/** The systems a pass draws from, walked again only when a particle update or anything else that
	* adds or takes away a system or a particle has run since the last walk.  A system with no
	* particles draws nothing and one the local player cannot see is skipped, so neither is kept.
	* Inside the window a new id means no walk: a system made there is filed when the window closes,
	* so the walk would find what the sync point found, by way of the objects the tick is deleting. */
void W3DParticleSystemManager::refreshFrameView( void )
{
	const UnsignedInt logicFrame = TheGameLogic->getFrame();
	if (m_frameViewValid && m_frameViewLogicFrame == logicFrame && m_frameViewFrame == m_lastLogicFrameUpdate
			&& m_frameViewParticles == m_particleCount && m_frameViewSystems == m_particleSystemCount
			&& (m_frameViewNextID == m_uniqueSystemID || LogicThread::isInsideWindow()))
		return;

	m_frameView.clear();
	m_frameFires.clear();
	m_frameFireFades.clear();
	m_fieldSystems.clear();
	for (ParticleSystemListIt it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end(); ++it)
	{
		ParticleSystem *sys = *it;
		if (sys == NULL || sys->isUsingDrawables())
			continue;
		// a field is kept with no particles too: its system still says where the pool is
		if (isGroundField( sys ))
			m_fieldSystems.push_back( sys );
		if (sys->getParticleCount() == 0)
			continue;
		const Real fade = sys->getStealthFade();
		if (fade <= 0.0f)
			continue;
		SystemView view;
		view.system = sys;
		view.fade = fade;
#if ZH_MODERN_WATER
		view.owner = INVALID_ID;
		if (sys->getAttachedDrawable() != INVALID_DRAWABLE_ID) {
			const Drawable *drawable = TheGameClient->findDrawableByID(sys->getAttachedDrawable());
			const Object *object = drawable ? drawable->getObject() : NULL;
			if (object) view.owner = object->getID();
		} else view.owner = sys->getAttachedObject();
#endif
		m_frameView.push_back( view );
		if (sys->getTemplate()->isFireLook()
				&& (sys->getShaderType() == ParticleSystemInfo::ADDITIVE || sys->getTemplate()->isEmissive()))
		{
			m_frameFires.push_back( sys );
			m_frameFireFades.push_back( fade );
		}
	}
	refreshGpuSystems();

	m_frameViewValid = TRUE;
	m_sunMapCastersValid = FALSE;
	m_frameViewLogicFrame = logicFrame;
	m_frameViewFrame = m_lastLogicFrameUpdate;
	m_frameViewParticles = m_particleCount;
	m_frameViewSystems = m_particleSystemCount;
	m_frameViewNextID = m_uniqueSystemID;
}

/** One smoke system's share of the walk getSunMapCasters hands the job pool: the room reserved for
	* its particles in the caster list, and how many of them its job kept. */
struct SunMapWalk
{
	ParticleSystem *	system;
	Real							fade;				///< its stealth fade: a stealthed thing's smoke shades only as much as it is drawn
	UnsignedInt				layers;			///< ParticleSystem::getVolumeParticleDepth
	Int								first;			///< its first slot in the caster list
	Int								capacity;		///< its particles
	Int								kept;				///< written by the job
};

struct SunMapWalkJob
{
	SunMapWalk *															walks;
	W3DParticleSystemManager::SunMapCaster *	casters;
};

/// systems a pool thread claims at once; most smoke systems are a handful of particles
static const Int SUN_MAP_WALKS_PER_CLAIM = 8;

static std::vector<SunMapWalk> s_sunMapWalks;	///< kept so the walk does not allocate

/** One system's particles into its own slots of the caster list.  Reads its particles and writes only
	* those slots and its own walk: no allocation, nothing another job touches. */
static void walkSunMapCasters( Int index, void *context )
{
	SunMapWalkJob *job = (SunMapWalkJob *)context;
	SunMapWalk &walk = job->walks[ index ];
	W3DParticleSystemManager::SunMapCaster *out = job->casters + walk.first;
	Int kept = 0;
	for (Particle *p = walk.system->getFirstParticle(); p && kept < walk.capacity; p = p->m_systemNext)
	{
		const Real opticalDepth = particleSunMapOpticalDepth( p->getAlpha() * walk.fade, walk.layers );
		if (opticalDepth <= 0.0f)
			continue;
		const Real radius = p->getSize() * 0.5f;		// the billboard's size is its full width
		if (radius <= 0.0f)
			continue;
		const Coord3D *pos = p->getPosition();
		out[ kept ].x = pos->x;
		out[ kept ].y = pos->y;
		out[ kept ].z = pos->z;
		out[ kept ].radius = radius;
		out[ kept ].opticalDepth = opticalDepth;
		out[ kept ].system = walk.system;
		++kept;
	}
	walk.kept = kept;
}

/** See W3DParticleSys.h.  A ground-aligned system is a decal already, fire is additive and casts
	* nothing, and the heat haze's texture names start with SMUD, as doParticles tests.  The particles
	* are walked on the job pool, each system into room reserved for it in list order, and the gaps
	* the faint ones leave are closed in the same order, so the list is the one a serial walk makes. */
const std::vector<W3DParticleSystemManager::SunMapCaster> &W3DParticleSystemManager::getSunMapCasters( void )
{
	refreshFrameView();
	if (m_sunMapCastersValid)
		return m_sunMapCasters;

	s_sunMapWalks.clear();
	Int reserved = 0;
	for (size_t v = 0; v < m_frameView.size(); ++v)
	{
		ParticleSystem *sys = m_frameView[ v ].system;
		if (sys->getShaderType() != ParticleSystemInfo::ALPHA || !sys->shouldBillboard()
				|| sys->isUsingStreak() || sys->isUsingSmudge() || sys->getTemplate()->isEmissive())
			continue;
		if (*((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
			continue;

		SunMapWalk walk;
		walk.system = sys;
		walk.fade = m_frameView[ v ].fade;
		walk.layers = sys->getVolumeParticleDepth();
		walk.first = reserved;
		walk.capacity = (Int)sys->getParticleCount();
		walk.kept = 0;
		reserved += walk.capacity;
		s_sunMapWalks.push_back( walk );
	}

	m_sunMapCasters.resize( (size_t)reserved );
	SunMapWalkJob job;
	job.walks = s_sunMapWalks.empty() ? NULL : &s_sunMapWalks[ 0 ];
	job.casters = m_sunMapCasters.empty() ? NULL : &m_sunMapCasters[ 0 ];
	JobSystem::parallel_for( (Int)s_sunMapWalks.size(), SUN_MAP_WALKS_PER_CLAIM, walkSunMapCasters, &job );

	size_t kept = 0;
	for (size_t w = 0; w < s_sunMapWalks.size(); ++w)
	{
		const SunMapWalk &walk = s_sunMapWalks[ w ];
		if (kept != (size_t)walk.first)
			std::copy( m_sunMapCasters.begin() + walk.first, m_sunMapCasters.begin() + walk.first + walk.kept,
				m_sunMapCasters.begin() + kept );
		kept += walk.kept;
	}
	m_sunMapCasters.resize( kept );

	m_sunMapCastersValid = TRUE;
	return m_sunMapCasters;
}

/**
 * Hack because DoParticles is called from Flush(), which is called
 * multiple times per frame.  We only want to render once.
 * @todo Clean up the flag/Flush hack.
 */
void W3DParticleSystemManager::queueParticleRender()
{
	m_readyToRender = true;
}

/**
 * Nasty hack to render particles last. Called directly by WW3D::Flush()
 */
void DoParticles( RenderInfoClass &rinfo )
{
	if (TheParticleSystemManager)
		TheParticleSystemManager->doParticles(rinfo);
}

void W3DParticleSystemManager::doParticles(RenderInfoClass &rinfo)
{

	if (m_readyToRender == false)
		return;

	// external mechanism must tell us when it's OK to render again...
	m_readyToRender = false;

	//reset each frame
	/// @todo lorenzen sez: this should be debug only:
	m_onScreenParticleCount = 0;

	Int visibleSmudgeCount = 0;
	if (TheSmudgeManager)
		TheSmudgeManager->setSmudgeCountLastFrame(0);	//keep track of visible smudges

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//@todo lorenzen sez: put these in registers for sure
	Real bcX = bbox.Center.X;
	Real bcY = bbox.Center.Y;
	Real bcZ = bbox.Center.Z;
	Real beX = bbox.Extent.X;
	Real beY = bbox.Extent.Y;
	Real beZ = bbox.Extent.Z;

	unsigned int personalities[MAX_POINTS_PER_GROUP];


	m_fieldParticleCount = 0;

	SmudgeSet *set=NULL;
	if (TheSmudgeManager)
		set=TheSmudgeManager->addSmudgeSet();	//global smudge set through which all smudges are rendered.

#ifdef DEBUG_LOGGING
	Int64 tFillStart, tFillEnd;
	tFillStart = Clock_Ticks();
#endif

	// Sorted billboards - most smoke, fire and dust - are written on the job pool.  With a hundred
	// thousand particles, walking them one system after another was a quarter of the frame.  Their
	// room in the sorting array is reserved here in list order, and the loop below inserts each one
	// into the sorting pool at its place in the same order, so the pool sees them as it always did.
	// Smudges, which draw on the client's random stream, streaks, volume particles, ground-aligned and
	// alpha-tested systems stay in that loop as they were.  Both loops walk the frame view, which
	// leaves out what neither would draw: drawable systems, empty ones and ones the player cannot see.
	refreshFrameView();
	m_billboardFills.clear();
	for (size_t v = 0; v < m_frameView.size(); ++v)
	{
		ParticleSystem *sys = m_frameView[ v ].system;
#if ZH_MODERN_WATER
		if (replaceLegacyVesselFoam(sys, m_frameView[ v ].owner))
			continue;
#endif
		if (*((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
			continue;

		BillboardFill fill;
		if (sys->isUsingStreak() || !sys->shouldBillboard() || sys->getVolumeParticleDepth() > 1
				|| !particlePresetShader( sys->getShaderType(), &fill.shader )
				|| !PointGroupClass::Would_Sort_Billboards( fill.shader ))
			continue;

		const Int particles = (Int)sys->getParticleCount();
		fill.fade = m_frameView[ v ].fade;
		fill.system = sys;
		fill.capacity = (particles < (Int)MAX_POINTS_PER_GROUP) ? particles : (Int)MAX_POINTS_PER_GROUP;
		fill.fieldIncrement = ( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;
		fill.drawn = 0;
		fill.pastLimit = 0;
		fill.glow = FALSE;
		fill.fire = sys->getTemplate()->isFireLook();
		fill.texture =W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );
		PointGroupClass::Reserve_Sorted_Billboards( fill.capacity, &fill.range );
		m_billboardFills.push_back( fill );
	}

	BillboardFillJob fillJob;
	fillJob.fills = m_billboardFills.empty() ? NULL : &m_billboardFills[ 0 ];
	DX8Wrapper::Get_Transform( D3DTS_VIEW, fillJob.view );
	fillJob.centerX = bcX;
	fillJob.centerY = bcY;
	fillJob.centerZ = bcZ;
	fillJob.extentX = beX;
	fillJob.extentY = beY;
	fillJob.extentZ = beZ;
	fillJob.lightCount = 0;
	// A reflecting water's mirror comes here first with its own, smaller box; the picks are the frame's.
	if (!ShaderClass::Is_Backface_Culling_Inverted())
	{
		s_firelightCount = 0;
		if (TheGlobalData->m_firelight && Direct3D11_Lights_Per_Pixel())
			gatherFirelights( bbox, m_lastLogicFrameUpdate, m_frameFires, m_frameFireFades );
	}
	// Only a CPU drawn smoke system takes a light (pickSmokeLights); with none, the walk over every
	// fire, a GPU one's samples aged in closed form, is a third of a frame thrown away.
	Bool smokeDrawn = FALSE;
	for (size_t v = 0; v < m_frameView.size() && !smokeDrawn; ++v)
		smokeDrawn = m_frameView[ v ].system->getShaderType() == ParticleSystemInfo::ALPHA
			&& !m_frameView[ v ].system->getTemplate()->isEmissive();
	if (TheGlobalData->m_smokeFireLighting && smokeDrawn)
	{
		// Between two particle updates nothing moves a particle; a system made in between has none
		// yet, and the counts catch a load or anything else that adds or takes some away.
		if (!s_fireLightsValid || s_fireLightsFrame != m_lastLogicFrameUpdate
				|| s_fireLightsParticles != m_particleCount || s_fireLightsSystems != m_particleSystemCount)
		{
			s_fireLights.clear();
			// a hidden fire lights nothing, an own stealthed one only as much as it shows
			for (size_t v = 0; v < m_frameView.size(); ++v)
				if (m_frameView[ v ].system->getShaderType() == ParticleSystemInfo::ADDITIVE
						|| m_frameView[ v ].system->getTemplate()->isEmissive())
					gatherFireLight( m_frameView[ v ].system, m_frameView[ v ].fade );
			s_gpuFireLights.resize( m_gpuFireSystems.size() );
			for (size_t g = 0; g < m_gpuFireSystems.size(); ++g)
			{
				const GpuFireSystem &fire = m_gpuFireSystems[ g ];
				GpuFireLight &item = s_gpuFireLights[ g ];
				item.system = fire.system;
				item.fade = fire.fade;
				item.slot = fire.slot;
				item.entry = &m_gpuEntries[ fire.slot ];
			}
			if (!s_gpuFireLights.empty())
				JobSystem::parallel_for( (Int)s_gpuFireLights.size(), GPU_FIRE_LIGHTS_PER_CLAIM, gatherGpuFireLight, &s_gpuFireLights[ 0 ] );
			for (size_t g = 0; g < s_gpuFireLights.size(); ++g)
				if (s_gpuFireLights[ g ].lit)
					s_fireLights.push_back( s_gpuFireLights[ g ].light );
			s_fireLightsValid = TRUE;
			s_fireLightsFrame = m_lastLogicFrameUpdate;
			s_fireLightsParticles = m_particleCount;
			s_fireLightsSystems = m_particleSystemCount;
		}
		fillJob.lightCount = gatherSmokeLights( fillJob.lights, bbox );
	}
	fillJob.glowAfterShade = Direct3D11_Present_Is_Enabled();
#if defined(__linux__)
	fillJob.glowAfterShade = fillJob.glowAfterShade
		|| SunShadow::OwnsSceneShadows(DX8Wrapper::_Get_D3D_Device());
#endif
	JobSystem::parallel_for( (Int)m_billboardFills.size(), BILLBOARD_FILLS_PER_CLAIM, fillBillboards, &fillJob );

	s_fieldPos.clear();
	s_fieldUV.clear();
	s_fieldGlow.clear();
	s_fieldStain.clear();
	s_fieldIndex.clear();
	s_fieldChunks.clear();
	{
		const Int puddles = collectGroundFields( bbox, rinfo.Camera );
		// counted as field particles on purpose: past MaxFieldParticleCount the field systems keep
		// one particle each, and that one is all the steady pool needs from them
		m_fieldParticleCount += puddles;
		m_onScreenParticleCount += puddles;
	}
	drawGroundFields();

	size_t nextFill = 0;
	for (size_t v = 0; v < m_frameView.size(); ++v)
	{
		ParticleSystem *sys = m_frameView[ v ].system;
		const Real fade = m_frameView[ v ].fade;

		// first, as a reserved system is never a smudge
		if (nextFill < m_billboardFills.size() && m_billboardFills[ nextFill ].system == sys)
		{
			BillboardFill &fill = m_billboardFills[ nextFill++ ];
			PointGroupClass::Insert_Sorted_Billboards( &fill.range, fill.drawn, fill.texture, fill.shader, fill.glow != FALSE, fill.fire != FALSE,
				sys->getTemplate()->isEmissive() != FALSE );
			fill.texture->Release_Ref();	// the draw state took its own reference
			m_fieldParticleCount += fill.fieldIncrement * fill.drawn;
			m_onScreenParticleCount += fill.drawn;
#ifdef DEBUG_LOGGING
			TheParticlesPastGroupLimit += fill.pastLimit;
#endif
			continue;
		}
#if ZH_MODERN_WATER
		if (replaceLegacyVesselFoam(sys, m_frameView[ v ].owner))
			continue;
#endif

		//temporary hack that checks if texture name starts with "SMUD" - if so, we can assume it's a smudge type
		if (/*sys->isUsingSmudge()*/ *((UnsignedInt *)sys->getParticleTypeName().str()) == 0x44554D53)
		{
			if (TheSmudgeManager && ((W3DSmudgeManager*)TheSmudgeManager)->getHardwareSupport() && TheGlobalData->m_useHeatEffects)
			{
				//set-up all the per-particle
				for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
				{
					const Coord3D *pos = p->getPosition();
					Real psize = p->getSize();

					//Cull particle to edges of screen and terrain.
					if (WWMath::Fabs( pos->x - bcX ) > ( beX + psize ) )
						continue;

					if (WWMath::Fabs( pos->y - bcY ) > ( beY + psize ) )
						continue;

					if (WWMath::Fabs( pos->z - bcZ ) > ( beZ + psize ) )
						continue;

					Smudge *smudge = set->addSmudgeToSet();

					smudge->m_pos.Set( pos->x, pos->y, pos->z );
					// Same range on both axes. The vertical one was half the horizontal, which
					// nobody could see while the centre vertex was reading the horizontal offset
					// for both - now that it reads .Y, a heat smudge pulled twice as far sideways.
					smudge->m_offset.Set( GameClientRandomValueReal(-0.06f,0.06f), GameClientRandomValueReal(-0.06f,0.06f) );
					smudge->m_size = psize;
					smudge->m_opacity = p->getAlpha() * fade;
					visibleSmudgeCount++;
				}
			}
			continue;
		}

		if (isGroundField( sys ))
			continue;	// drawn above, every field at once

		/// @todo lorenzen sez: declare these outside the sys loop, and put some in registers
		// initialize them here still, of course
		// build W3D particle buffer
		Int count = 0;
		Vector3 *posArray = m_posBuffer->Get_Array();
		Real *sizeArray = m_sizeBuffer->Get_Array();
		Vector4 *RGBAArray = m_RGBABuffer->Get_Array();
		uint8 *angleArray = m_angleBuffer->Get_Array();
		uint8 *frameArray = m_frameBuffer->Get_Array();
		const UnsignedInt flipbookLog2 = sys->getTemplate()->getFlipbookGridLog2();
		const Coord3D *pos;
		const RGBColor *color;
		Real psize;
		// the same answer for every particle of the system, so asked once rather than once a particle
		const UnsignedInt fieldParticleIncrement =
			( sys->getPriority() == AREA_EFFECT && sys->m_isGroundAligned != FALSE ) ? 1 : 0;
		SmokeLight lights[ SMOKE_LIGHTS_PER_SYSTEM ];
		const Int lightCount = pickSmokeLights( sys, fillJob.lights, fillJob.lightCount, lights );




		//set-up all the per-particle
		for (Particle *p = sys->getFirstParticle(); p; p = p->m_systemNext)
		{
			pos = p->getPosition();
			psize = p->getSize();

			//Cull particle to edges of screen and terrain.
			if (WWMath::Fabs(pos->x - bcX) > (beX + psize))
				continue;

			if (WWMath::Fabs(pos->y - bcY) > (beY + psize))
				continue;

			if (WWMath::Fabs(pos->z - bcZ) > (beZ + psize))
				continue;

			m_fieldParticleCount += fieldParticleIncrement;
			
			//@todo lorenzen sez: use pointer arithmetic for these arrays
			personalities[count] = p->getPersonality();
			
			posArray[count].X = pos->x;
			posArray[count].Y = pos->y;
			posArray[count].Z = pos->z;

			sizeArray[count] = psize;
			if (flipbookLog2)
				frameArray[count] = (uint8)p->getFlipbookFrame();

			color = p->getColor();
			RGBAArray[count].X = color->red;
			RGBAArray[count].Y = color->green;
			RGBAArray[count].Z = color->blue;
			RGBAArray[count].W = p->getAlpha();
			if (lightCount > 0)
				lightSmoke( pos, RGBAArray[count].W, lights, lightCount, RGBAArray[count].X, RGBAArray[count].Y, RGBAArray[count].Z );
			if (fade < 1.0f)
				stealthFadeParticle( sys->getShaderType(), fade, RGBAArray[count].X, RGBAArray[count].Y, RGBAArray[count].Z, RGBAArray[count].W );

			angleArray[count] = floatToByteAsMsvc( p->getAngle() * 255.0f / (2.0f * PI) );
			
			if (++count == MAX_POINTS_PER_GROUP)
			{
#ifdef DEBUG_LOGGING
				TheParticlesPastGroupLimit += sys->getParticleCount() - count;
#endif
				break;
			}
		}

		if ( count == 0 )
			continue;	//this system has no particles to render

		TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( sys->getParticleTypeName().str() );

		
		if ( m_streakLine && sys->isUsingStreak() && (count >= 2) ) 
		{
			m_streakLine->Reset_Line();

			m_streakLine->Set_Texture( texture );
			texture->Release_Ref();//release reference since it's held by streakline
			switch( sys->getShaderType() )
			{
				case ParticleSystemInfo::ADDITIVE:
					m_streakLine->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA:
					m_streakLine->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
					break;
				case ParticleSystemInfo::ALPHA_TEST:
					m_streakLine->Set_Shader( ShaderClass::_PresetATestSpriteShader );
					break;
				case ParticleSystemInfo::MULTIPLY:
					m_streakLine->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
					break;
			}
			
			//UPDATE THE STREAK'S ARRAYS
			m_streakLine->Set_LocsWidthsColors( 
				count,
				m_posBuffer->Get_Array(),
				m_sizeBuffer->Get_Array(),
				m_RGBABuffer->Get_Array(),
				&personalities[0]
				);

			//WWASSERT( m_streakLine->Get_Num_Points() == count );

			// This is the happy place for this!
			RGBAArray[0].X = 0;//eliminates the scissor edge on the trailing edge of the streak
			RGBAArray[0].Y = 0;
			RGBAArray[0].Z = 0;
			RGBAArray[0].W = 0;


			//RENDER STREAK!
			m_streakLine->Render( rinfo );
			
		}
		else 
		{

			WWASSERT( m_pointGroup );

			if ( m_pointGroup ) // this catches the particle and volumeparticle cases
			{
				// render all the systems' particles
				m_pointGroup->Set_Texture( texture );
				texture->Release_Ref();//release reference since it's held by pointGroup
				m_pointGroup->Set_Flag( PointGroupClass::TRANSFORM, true );	// transform to screen space

				switch( sys->getShaderType() )
				{
					case ParticleSystemInfo::ADDITIVE:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAdditiveSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA:
						m_pointGroup->Set_Shader( ShaderClass::_PresetAlphaSpriteShader );
						break;
					case ParticleSystemInfo::ALPHA_TEST:
						m_pointGroup->Set_Shader( ShaderClass::_PresetATestSpriteShader );
						break;
					case ParticleSystemInfo::MULTIPLY:
						m_pointGroup->Set_Shader( ShaderClass::_PresetMultiplicativeSpriteShader );
						break;
				}

				/// @todo Use both QUADS and TRIS for particles
				m_pointGroup->Set_Point_Mode( PointGroupClass::QUADS );
				// A flipbook's frames go in the frame array; every other system draws the whole card.
				m_pointGroup->Set_Arrays( m_posBuffer, m_RGBABuffer, NULL, m_sizeBuffer, m_angleBuffer,
					flipbookLog2 ? m_frameBuffer : NULL, count );
				m_pointGroup->Set_Frame_Row_Column_Count_Log2( (unsigned char)flipbookLog2 );
				m_pointGroup->Set_Billboard(sys->shouldBillboard());
				m_pointGroup->Set_Point_Frame( 0 );

				//RENDER IT!
				if( sys->getVolumeParticleDepth() > 1 )
				{
					m_pointGroup->RenderVolumeParticle( rinfo, sys->getVolumeParticleDepth() );
				}
				else
					m_pointGroup->Render( rinfo );
		
			}
		}


		/// @todo lorenzen sez: this should be debug only:
		//add particle count to total
		m_onScreenParticleCount += count;

	/*
		// draw the wind vector for this particle system on the screen
		UnsignedInt width = TheDisplay->getWidth();
		UnsignedInt height = TheDisplay->getHeight();
		Coord3D worldStart, worldEnd;
		ICoord2D pixelStart, pixelEnd;
		sys->getPosition( &worldStart );
		worldEnd.x = Cos( sys->getWindAngle() ) * 50.0f + worldStart.x;
		worldEnd.y = Sin( sys->getWindAngle() ) * 50.0f + worldStart.y;
		worldEnd.z = worldStart.z;
		TheTacticalView->worldToScreen( &worldStart, &pixelStart );
		TheTacticalView->worldToScreen( &worldEnd, &pixelEnd );
		Color colorStart = GameMakeColor( 255, 255, 255, 255 );
		Color colorEnd = GameMakeColor( 255, 128, 128, 255 );
		TheDisplay->drawLine( pixelStart.x, pixelStart.y, pixelEnd.x, pixelEnd.y, 1.0f, colorStart, colorEnd );
	*/


	}// next system

		/// @todo lorenzen sez: this should be debug only:
	TheParticleSystemManager->setOnScreenParticleCount(m_onScreenParticleCount);

#ifdef DEBUG_LOGGING
	tFillEnd = Clock_Ticks();
	{
		Int64 freq;
		freq = Clock_Ticks_Per_Second();
		if( freq > 0 )
			TheParticleFillMS += (Real)((double)(tFillEnd - tFillStart) * 1000.0 / (double)freq);
	}
#endif

	// The GPU particles' two passes, under the sorted billboards this loop put in the sorting pool,
	// which land over them when the pool flushes.
	drawGpuParticles( rinfo );

	//Draw any particles belonging to weather effects
	if (TheSnowManager)
		((W3DSnowManager *)TheSnowManager)->render(rinfo);

	//Now process screen smudges which are particles that distort the background behind them.
	if(TheSmudgeManager)
	{
		((W3DSmudgeManager *)TheSmudgeManager)->render(rinfo);
		TheSmudgeManager->reset();	//clear all the smudges after rendering since we fill again each frame.
		TheSmudgeManager->setSmudgeCountLastFrame(visibleSmudgeCount);
	}
}

//-------------------------------------------------------------------------------------------------
// The GPU particle path's bookkeeping (GPU-PARTICLES.md in the wrapper; GpuParticles.h is the device
// half).  A spawn is packed into a record at the ring's head and kept on the CPU only as the frame
// it is surely dead on, which is what the live range and the eviction count read, and counted by
// that frame for the log line's live count.  A system gets an entry slot at its first spawn and
// gives it back when it goes; the slot is not handed out again until the last record naming it has
// died.  Everything goes up to the device in drawGpuParticles: the frame's records in at most two
// pieces, the entries and fades that changed, and a few texture slices a frame.
//-------------------------------------------------------------------------------------------------

/// A particle texture's first picture, drawn into its slice: at most this many a frame.
static const UnsignedInt GPU_SLICES_DRAWN_A_FRAME = 4;
/// Slices looked at again a frame for a view that changed (a texture's background load finishing).
static const UnsignedInt GPU_SLICES_CHECKED_A_FRAME = 8;

/** Asked by every system's constructor, which may run on the logic thread, so it reads only what the
	* main thread cached and calls nothing on the device. */
Bool W3DParticleSystemManager::acceptsGpuParticles( void ) const
{
	return m_gpuSupported && !m_gpuRefused && TheGlobalData->getEffectiveGpuParticleCapacity() > 0
		&& !TheGlobalData->m_headless && !TheGlobalData->m_classicGraphics;
}

void W3DParticleSystemManager::markGpuSlotDirty( UnsignedInt slot )
{
	if (m_gpuDirtyFirst == m_gpuDirtyEnd)
	{
		m_gpuDirtyFirst = slot;
		m_gpuDirtyEnd = slot + 1;
		return;
	}
	if (slot < m_gpuDirtyFirst)
		m_gpuDirtyFirst = slot;
	if (slot + 1 > m_gpuDirtyEnd)
		m_gpuDirtyEnd = slot + 1;
}

/** The array slice of a particle texture, given one on its first use and queued for its picture.  A
	* reference is held on each texture until the match ends, so its slice stays its own.  GPU_NO_SLOT
	* once the array is full. */
UnsignedInt W3DParticleSystemManager::gpuTextureSlice( const AsciiString &name )
{
	TextureClass *texture = W3DDisplay::m_assetManager->Get_Texture( name.str() );
	std::map<TextureClass *, UnsignedInt>::const_iterator known = m_gpuTextureSlices.find( texture );
	if (known != m_gpuTextureSlices.end())
	{
		texture->Release_Ref();
		return known->second;
	}
	if (m_gpuTextures.size() == (size_t)GpuParticles::TEXTURE_SLICES)
	{
		if (!m_gpuTexturesFullTold)
			DEBUG_LOG(( "GPUPARTICLES: the texture array is full at %d slices; %s and every texture after it stays on the CPU\n",
				(Int)GpuParticles::TEXTURE_SLICES, name.str() ));
		m_gpuTexturesFullTold = TRUE;
		texture->Release_Ref();
		return GPU_NO_SLOT;
	}
	const UnsignedInt slice = (UnsignedInt)m_gpuTextures.size();
	m_gpuTextures.push_back( texture );
	m_gpuTextureSlices[ texture ] = slice;
	m_gpuSliceQueue.push_back( slice );
	return slice;
}

/** The entry slot of a system, given one at its first spawn: the longest released slot if its records
	* have all died, else a new one.  GPU_NO_SLOT when all SYSTEM_SLOTS are taken or the texture array
	* is full, and the CPU takes the spawn; the texture refusal is kept, the slot one asked again. */
UnsignedInt W3DParticleSystemManager::gpuSlotFor( ParticleSystem *system )
{
	if (system == m_gpuLastSystem)
		return m_gpuLastSlot;

	UnsignedInt slot = GPU_NO_SLOT;
	std::unordered_map<ParticleSystem *, UnsignedInt>::const_iterator known = m_gpuSlotOf.find( system );
	if (known != m_gpuSlotOf.end())
		slot = known->second;
	else
	{
		const UnsignedInt slice = gpuTextureSlice( system->getParticleTypeName() );
		if (slice == GPU_NO_SLOT)
		{
			m_gpuSlotOf[ system ] = GPU_NO_SLOT;
			return GPU_NO_SLOT;
		}
		// ponytail: only the front is looked at; a slot released later that comes free sooner waits its turn
		if (!m_gpuFreeSlots.empty() && m_gpuSlots[ m_gpuFreeSlots.front() ].reuseFrame <= getParticleFrame())
		{
			slot = m_gpuFreeSlots.front();
			m_gpuFreeSlots.pop_front();
		}
		else
		{
			if (m_gpuSlots.size() == (size_t)GpuParticles::SYSTEM_SLOTS)
				return GPU_NO_SLOT;
			slot = (UnsignedInt)m_gpuSlots.size();
			m_gpuSlots.resize( slot + 1 );
			m_gpuEntries.resize( slot + 1 );
			m_gpuFades.resize( slot + 1 );
		}

		GpuSlot &taken = m_gpuSlots[ slot ];
		taken.system = system;
		taken.slice = slice;
		taken.reuseFrame = 0;
		ParticleGpuSystemEntry &entry = m_gpuEntries[ slot ];
		particleGpuSystemEntryBuild( *system, &entry );
		// taken once a slot, so the Weather option switched in a match reaches the systems made after it
		const Coord3D drift = system->getDriftWithWeather();
		entry.m_drift[ 0 ] = drift.x;
		entry.m_drift[ 1 ] = drift.y;
		entry.m_textureSlice = slice;
		entry.m_flags = PARTICLE_GPU_ALIVE | ( system->isGpuGroundClamped() ? PARTICLE_GPU_GROUND_CLAMPED : 0u );
		// the fade is taken at the sync point (refreshGpuSystems), which runs before the next draw
		m_gpuFades[ slot ] = 1.0f;
		m_gpuFadesDirty = TRUE;
		markGpuSlotDirty( slot );
		m_gpuSlotOf[ system ] = slot;
	}

	m_gpuLastSystem = system;
	m_gpuLastSlot = slot;
	return slot;
}

/** The records that died since the last call off the live count.  The particle frame only goes up
	* (a reset clears the counts); a gap of a whole ring of frames with no call means no spawn in it
	* either, so everything counted has died. */
void W3DParticleSystemManager::drainGpuDeaths( UnsignedInt now )
{
	if (now <= m_gpuDrainedFrame)
		return;
	if (now - m_gpuDrainedFrame >= (UnsignedInt)GPU_DEATH_FRAMES)
	{
		std::fill( m_gpuDeathCounts.begin(), m_gpuDeathCounts.end(), 0u );
		m_gpuLive = 0;
		m_gpuDrainedFrame = now;
		return;
	}
	while (m_gpuDrainedFrame < now)
	{
		UnsignedInt &dying = m_gpuDeathCounts[ ++m_gpuDrainedFrame % GPU_DEATH_FRAMES ];
		m_gpuLive -= dying;
		dying = 0;
	}
}

Bool W3DParticleSystemManager::spawnGpuParticle( ParticleSystem *system, const ParticleInfo *info )
{
	// Classic can come on mid-match, after the system was made eligible; its spawns go to the CPU
	if (m_gpuRefused || TheGlobalData->m_classicGraphics)
		return FALSE;
	const UnsignedInt slot = gpuSlotFor( system );
	if (slot == GPU_NO_SLOT)
		return FALSE;

	// k = now - birth + smooth reaches the lifetime one update after a CPU twin's last
	const UnsignedInt now = getParticleFrame();
	drainGpuDeaths( now );
	UnsignedInt &death = m_gpuDeath[ (size_t)( m_gpuWritten % m_gpuCapacity ) ];
	if (death > now)
	{
		++m_gpuEvicted;
		--m_gpuDeathCounts[ death % GPU_DEATH_FRAMES ];
		--m_gpuLive;
	}
	// ponytail: the death count's ring is 65535 frames (18 minutes), so a longer life is cut to that
	// for the tail and the count; the compute pass still ages it by its own lifetime while it is drawn
	const UnsignedInt life = info->m_lifetime < (UnsignedInt)GPU_DEATH_FRAMES ? info->m_lifetime : (UnsignedInt)GPU_DEATH_FRAMES - 1;
	death = now + life;
	++m_gpuDeathCounts[ death % GPU_DEATH_FRAMES ];
	++m_gpuLive;

	// A window drawing nothing (minimised) still spawns; past two rings of records only the newest
	// ring could ever be drawn.
	if (m_gpuPending.size() == 2 * (size_t)m_gpuCapacity)
	{
		m_gpuPending.erase( m_gpuPending.begin(), m_gpuPending.begin() + m_gpuCapacity );
		m_gpuPendingFirst += m_gpuCapacity;
	}
	m_gpuPending.resize( m_gpuPending.size() + 1 );
	particleGpuSpawnPack( *info, now, slot, (UnsignedInt)( m_gpuWritten * 2654435761u ) >> 16, &m_gpuPending.back() );
	++m_gpuWritten;
	++m_gpuSpawned;
	return TRUE;
}

void W3DParticleSystemManager::releaseGpuSystem( ParticleSystem *system )
{
	std::unordered_map<ParticleSystem *, UnsignedInt>::iterator known = m_gpuSlotOf.find( system );
	if (known == m_gpuSlotOf.end())
		return;		// an eligible system that never spawned on the GPU
	const UnsignedInt slot = known->second;
	m_gpuSlotOf.erase( known );
	if (m_gpuLastSystem == system)
		m_gpuLastSystem = NULL;
	if (slot == GPU_NO_SLOT)
		return;		// refused for its texture, it never held one

	// its records stay in the ring until they die; until then the entry culls them, and the slot waits
	GpuSlot &freed = m_gpuSlots[ slot ];
	freed.system = NULL;
	freed.reuseFrame = system->getGpuLastDeathFrame() + 1;
	m_gpuEntries[ slot ].m_flags = 0;
	m_gpuFades[ slot ] = 0.0f;
	m_gpuFadesDirty = TRUE;
	markGpuSlotDirty( slot );
	m_gpuFreeSlots.push_back( slot );
}

/** With the frame view, at the sync point: each system's stealth fade, and the fires among them for
	* the smoke's light.  The entry is not built again here: gpuSlotFor builds it at the system's first
	* GPU spawn, and the only writers of the keys it reads (tintAllColors from the beacon and the signal
	* smoke, which also sets two keys of its own) run right after createParticleSystem, before any spawn;
	* a slave's merge works on a copy.  Rebuilding and comparing every entry every update cost 0.65 ms
	* at 3450 systems.
	* ponytail: a key changed on a system already spawning on the GPU is not seen; give
	* ParticleSystemInfo a key version and rebuild on a new one if anything ever does that. */
void W3DParticleSystemManager::refreshGpuSystems( void )
{
	m_gpuFireSystems.clear();
	for (UnsignedInt slot = 0; slot < (UnsignedInt)m_gpuSlots.size(); ++slot)
	{
		ParticleSystem *sys = m_gpuSlots[ slot ].system;
		if (sys == NULL)
			continue;
		const Real fade = sys->getStealthFade();
		const Real shown = fade > 0.0f ? fade : 0.0f;
		if (m_gpuFades[ slot ] != shown)
		{
			m_gpuFades[ slot ] = shown;
			m_gpuFadesDirty = TRUE;
		}
		if (fade > 0.0f && sys->getGpuSampleCount() != 0
				&& ( sys->getShaderType() == ParticleSystemInfo::ADDITIVE || sys->getTemplate()->isEmissive() ))
		{
			const GpuFireSystem fire = { sys, fade, slot };
			m_gpuFireSystems.push_back( fire );
		}
	}
}

/** At a map's load, after the base has loaded the particle textures: everything the device makes
	* for the GPU path is made here, so the match's first frames pay none of it. */
void W3DParticleSystemManager::preloadAssets( TimeOfDay timeOfDay )
{
	ParticleSystemManager::preloadAssets( timeOfDay );
	// a detail preset changed since the last match asks for another ring: made again here, between
	// matches, never in one, and a size the device refused before may be one it takes now
	if (m_gpuCapacity != 0 && TheGlobalData->getEffectiveGpuParticleCapacity() > 0 && gpuCapacityAsked() != m_gpuCapacity)
	{
		m_gpuCapacity = 0;
		m_gpuRefused = FALSE;
	}
	if (!prepareGpu())
		return;
	sendGpuGround();		// a new map: the old ground is not this one's

	// Every texture an eligible template names gets its slice and picture now, drawn in one go.
	// ponytail: the slices go in template order, so past TEXTURE_SLICES the textures left out are
	// whichever came last, not the least used; count spawns per texture if that ever bites.
	for (TemplateMap::const_iterator it = beginParticleSystemTemplate(); it != endParticleSystemTemplate(); ++it)
	{
		const ParticleSystemTemplate *tmplate = it->second;
		if (tmplate->isGpuEligible() && !tmplate->getParticleTypeName().isEmpty()
				&& gpuTextureSlice( tmplate->getParticleTypeName() ) == GPU_NO_SLOT)
			break;
	}
	while (!m_gpuSliceQueue.empty())
	{
		const UnsignedInt slice = m_gpuSliceQueue.front();
		m_gpuSliceQueue.pop_front();
		m_gpuTextures[ slice ]->Init();
		GpuParticles::Set_Slice_Texture( slice, m_gpuTextures[ slice ]->Peek_D3D_Base_Texture() );
	}
	GpuParticles::Evaluate( 0, 0, getParticleFrame(), 0.0f );		// draws the slices asked; ages nothing
	DEBUG_LOG(( "GPUPARTICLES: %u texture slices drawn at load\n", (UnsignedInt)m_gpuTextures.size() ));
}

/** On the main thread, at a map's load and at each draw.  A device made again (a reset, a new
	* capacity) holds nothing of what went up before: those records are lost, and every entry, fade,
	* slice and the ground go up again. */
Bool W3DParticleSystemManager::prepareGpu( void )
{
	m_gpuSupported = GpuParticles::Supported();
	if (!m_gpuSupported || m_gpuRefused || TheGlobalData->getEffectiveGpuParticleCapacity() <= 0)
		return FALSE;
	if (m_gpuCapacity == 0)
	{
		m_gpuCapacity = gpuCapacityAsked();
		m_gpuDeath.assign( m_gpuCapacity, 0 );
		m_gpuDeathCounts.assign( GPU_DEATH_FRAMES, 0 );
	}

	bool fresh = false;
	if (!GpuParticles::Prepare( m_gpuCapacity, &fresh ))
	{
		m_gpuRefused = TRUE;
		m_gpuPending.clear();
		m_gpuPendingFirst = m_gpuWritten;
		m_gpuTail = m_gpuWritten;
		return FALSE;
	}
	if (fresh)
	{
		if (m_gpuTail < m_gpuPendingFirst)
			m_gpuTail = m_gpuPendingFirst;
		m_gpuDirtyFirst = 0;
		m_gpuDirtyEnd = (UnsignedInt)m_gpuSlots.size();
		m_gpuFadesDirty = TRUE;
		m_gpuSliceQueue.clear();
		for (UnsignedInt slice = 0; slice < (UnsignedInt)m_gpuTextures.size(); ++slice)
			m_gpuSliceQueue.push_back( slice );
		sendGpuGround();
	}
	return TRUE;
}

/** The drawn terrain's heights (BaseHeightMapRenderObjClass::getDrawHeight's map) for the systems
	* that rest on it, one texel a height map vertex so the device's bilinear read lands on the vertices
	* as the CPU's does.
	* ponytail: taken once a map and at a device's reset, so a crater dug in the match is not in it;
	* send it again from the terrain's own change if a resting particle is seen floating over one. */
void W3DParticleSystemManager::sendGpuGround( void )
{
	WorldHeightMap *map = TheTerrainRenderObject->getMap();
	if (map == NULL)
		return;		// no map loaded: a menu drawn with no shell map
	const UnsignedInt width = (UnsignedInt)map->getXExtent();
	const UnsignedInt height = (UnsignedInt)map->getYExtent();
	const UnsignedByte *data = map->getDataPtr();
	std::vector<float> heights( (size_t)width * height );
	for (size_t i = 0; i < heights.size(); ++i)
		heights[ i ] = data[ i ] * MAP_HEIGHT_SCALE;
	// vertex i is at world (i - border) * MAP_XY_FACTOR, its texel's centre at (i + 0.5) / extent
	const Real border = (Real)map->getBorderSizeInline();
	const float mapping[ 4 ] = { 1.0f / ( MAP_XY_FACTOR * width ), 1.0f / ( MAP_XY_FACTOR * height ),
		( border + 0.5f ) / width, ( border + 0.5f ) / height };
	GpuParticles::Set_Ground( &heights[ 0 ], width, height, mapping );
}

/** A few queued slices' first pictures a frame, and a few drawn ones looked at again in turn, so a
	* burst of new textures is spread over frames instead of landing in one. */
void W3DParticleSystemManager::drawGpuSlices( void )
{
	const auto send = [this]( UnsignedInt slice )
	{
		TextureClass *texture = m_gpuTextures[ slice ];
		texture->Init();		// as Apply would, for a texture no CPU draw has bound
		GpuParticles::Set_Slice_Texture( slice, texture->Peek_D3D_Base_Texture() );
	};
	for (UnsignedInt drawn = 0; drawn < GPU_SLICES_DRAWN_A_FRAME && !m_gpuSliceQueue.empty(); ++drawn)
	{
		send( m_gpuSliceQueue.front() );
		m_gpuSliceQueue.pop_front();
	}
	const UnsignedInt count = (UnsignedInt)m_gpuTextures.size();
	for (UnsignedInt checked = 0; checked < GPU_SLICES_CHECKED_A_FRAME && checked < count; ++checked)
	{
		m_gpuSliceCursor = ( m_gpuSliceCursor + 1 ) % count;
		send( m_gpuSliceCursor );
	}
}

/** Both passes, from doParticles before the sorted billboards: ADDITIVE first so the smoke drawn in
	* the ALPHA pass darkens the fire under it, as the sorting pool's back to front order mostly does. */
void W3DParticleSystemManager::drawGpuParticles( RenderInfoClass &rinfo )
{
	// Classic may have come on mid-match: what is in the ring waits, undrawn, until it goes off
	if (TheGlobalData->m_classicGraphics || !prepareGpu())
		return;

	const UnsignedInt now = getParticleFrame();
	drainGpuDeaths( now );

	// the records spawned since the last draw, the newest ring of them at most, in at most two pieces
	const size_t pending = m_gpuPending.size();
	if (pending != 0)
	{
		const UnsignedInt keep = pending < (size_t)m_gpuCapacity ? (UnsignedInt)pending : m_gpuCapacity;
		const ParticleGpuSpawn *from = &m_gpuPending[ pending - keep ];
		const UnsignedInt ring = (UnsignedInt)( ( m_gpuWritten - keep ) % m_gpuCapacity );
		const UnsignedInt first = keep < m_gpuCapacity - ring ? keep : m_gpuCapacity - ring;
		GpuParticles::Write_Spawns( ring, from, first );
		if (first < keep)
			GpuParticles::Write_Spawns( 0, from + first, keep - first );
		m_gpuPending.clear();
	}
	m_gpuPendingFirst = m_gpuWritten;

	if (m_gpuDirtyFirst != m_gpuDirtyEnd)
	{
		GpuParticles::Write_Systems( m_gpuDirtyFirst, &m_gpuEntries[ m_gpuDirtyFirst ], m_gpuDirtyEnd - m_gpuDirtyFirst );
		m_gpuDirtyFirst = m_gpuDirtyEnd = 0;
	}
	if (m_gpuFadesDirty && !m_gpuFades.empty())
		GpuParticles::Write_Fades( &m_gpuFades[ 0 ], (UnsignedInt)m_gpuFades.size() );
	m_gpuFadesDirty = FALSE;
	drawGpuSlices();

	// the oldest record that may still be alive: past every overwritten one, then past the dead
	if (m_gpuWritten - m_gpuTail > m_gpuCapacity)
		m_gpuTail = m_gpuWritten - m_gpuCapacity;
	while (m_gpuTail < m_gpuWritten && m_gpuDeath[ (size_t)( m_gpuTail % m_gpuCapacity ) ] <= now)
		++m_gpuTail;
	const UnsignedInt range = (UnsignedInt)( m_gpuWritten - m_gpuTail );

	GpuParticles::Evaluate( (UnsignedInt)( m_gpuTail % m_gpuCapacity ), range, now, TheSmoothMotionAlpha );

	const HazardCloud::Camera cloud = cloudCamera( rinfo.Camera );
	GpuParticles::Camera camera;
	memcpy( camera.view, cloud.view, sizeof( camera.view ) );
	memcpy( camera.projection, cloud.projection, sizeof( camera.projection ) );
	GpuParticles::Draw( ParticleSystemInfo::ADDITIVE, camera );
	GpuParticles::Draw( ParticleSystemInfo::ALPHA, camera );

#ifdef DEBUG_LOGGING
	// GPU-PARTICLES.md's measurement line, beside PARTICLES every 100 particle updates: the records
	// alive, those spawned and those overwritten alive since the last line, the range the passes
	// draw, and the systems holding a slot.
	if (m_lastLogicFrameUpdate % 100 == 0 && m_lastLogicFrameUpdate != m_gpuLogFrame)
	{
		m_gpuLogFrame = m_lastLogicFrameUpdate;
		DEBUG_LOG(( "GPUPARTICLES frame %d: live %u spawned %u ring %u evicted %u systems %u\n",
			m_lastLogicFrameUpdate, m_gpuLive, m_gpuSpawned, range, m_gpuEvicted, (UnsignedInt)m_gpuSlotOf.size() ));
		m_gpuSpawned = 0;
		m_gpuEvicted = 0;
	}
#endif
}

/** After a reset every system is gone, so nothing in the ring is drawn again, no slot waits, and the
	* match's textures are let go for the next one's. */
void W3DParticleSystemManager::resetGpuParticles( void )
{
	m_gpuTail = m_gpuWritten;
	m_gpuPendingFirst = m_gpuWritten;
	m_gpuPending.clear();
	std::fill( m_gpuDeath.begin(), m_gpuDeath.end(), 0u );
	std::fill( m_gpuDeathCounts.begin(), m_gpuDeathCounts.end(), 0u );
	m_gpuLive = 0;
	m_gpuSlots.clear();
	m_gpuEntries.clear();
	m_gpuFades.clear();
	m_gpuFadesDirty = FALSE;
	m_gpuFreeSlots.clear();
	m_gpuSlotOf.clear();
	m_gpuLastSystem = NULL;
	m_gpuDirtyFirst = m_gpuDirtyEnd = 0;
	m_gpuFireSystems.clear();
	for (size_t i = 0; i < m_gpuTextures.size(); ++i)
		m_gpuTextures[ i ]->Release_Ref();
	m_gpuTextures.clear();
	m_gpuTextureSlices.clear();
	m_gpuSliceQueue.clear();
	m_gpuSliceCursor = 0;
	m_gpuTexturesFullTold = FALSE;
}
