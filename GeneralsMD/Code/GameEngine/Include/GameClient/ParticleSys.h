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

// ParticleSys.h //////////////////////////////////////////////////////////////////////////////////
// Particle System type definitions
// Author: Michael S. Booth, November 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _PARTICLE_SYS_H_
#define _PARTICLE_SYS_H_

#include <stdio.h>
#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/GameType.h"
#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/ClientRandomValue.h"

#include "WWMath/matrix3d.h"		///< @todo Replace with our own matrix library
#include "Common/STLTypedefs.h"
#include <functional>
#include <vector>

 
/// @todo Once the client framerate is decoupled, the frame counters within will have to become time-based
 
class Particle;
class ParticleSystem;
class ParticleSystemManager;
class Drawable;
class Object;
struct FieldParse;
class INI;
class DebugWindowDialog;		// really ParticleEditorDialog
class RenderInfoClass;			// ick
class Shadow;

enum ParticleSystemID : Int
{
	INVALID_PARTICLE_SYSTEM_ID = 0
};

// The manager's master list of live systems.  It lives out here so that a ParticleSystem can hold
// its own position in it and unlink itself in constant time instead of making the manager scan.
typedef std::list<ParticleSystem*> ParticleSystemPtrList;
typedef ParticleSystemPtrList::iterator ParticleSystemPtrListIt;

#define MAX_VOLUME_PARTICLE_DEPTH ( 16 )
#define DEFAULT_VOLUME_PARTICLE_DEPTH ( 0 )//The Default is not to do the volume thing!
#define OPTIMUM_VOLUME_PARTICLE_DEPTH ( 6 )


//--------------------------------------------------------------------------------------------------------------

enum { MAX_KEYFRAMES=8 };

struct Keyframe
{
	Real value;
	UnsignedInt frame;
};

struct RGBColorKeyframe
{
	RGBColor color;
	UnsignedInt frame;
};

enum ParticlePriorityType : Int
{
	INVALID_PRIORITY = 0, 
	PARTICLE_PRIORITY_LOWEST = 1,
//	FLUFF = PARTICLE_PRIORITY_LOWEST,		///< total and absolute fluff
//	DEBRIS,		///< debris related particles
//	NATURE,	///< neato effects we just might see in the world
//	WEAPON,		///< Weapons firing and flying in the air
//	DAMAGE,		///< taking damage/dying explosions
//	SPECIAL,	///< super special top priority like a superweapon

	WEAPON_EXPLOSION = PARTICLE_PRIORITY_LOWEST,
	SCORCHMARK,
	DUST_TRAIL,
	BUILDUP,
	DEBRIS_TRAIL,
	UNIT_DAMAGE_FX,
	DEATH_EXPLOSION,
	SEMI_CONSTANT,
	CONSTANT,
	WEAPON_TRAIL,
	AREA_EFFECT,
	CRITICAL,				///< super special top priority like a superweapon
	ALWAYS_RENDER,	///< used for logically important display (not just fluff), so must never be culled, regardless of particle cap, lod, etc
	// !!! *Noting* goes here ... special is the top priority !!!
	PARTICLE_PRIORITY_HIGHEST = ALWAYS_RENDER,
	NUM_PARTICLE_PRIORITIES  ///< Keep this last
};

/**
 * This structure is filled out and passed to the constructor of a Particle to initialize it
 */
class ParticleInfo : public Snapshot
{

public:

	ParticleInfo( void );

	Coord3D m_vel;															///< initial velocity
	Coord3D m_pos;															///< initial position
	Coord3D m_emitterPos;												///< position of the emitter
	Real m_velDamping;													///< velocity damping coefficient

	Real m_angleZ;															///< initial angle around Z axis
	Real m_angularRateZ;												///< initial angle around Z axis
	Real m_angularDamping;											///< angular velocity damping coefficient

	UnsignedInt m_lifetime;											///< lifetime of this particle

	Real m_size;																///< size of the particle
	Real m_sizeRate;														///< rate of change of size
	Real m_sizeRateDamping;											///< damping of size change rate

	Keyframe m_alphaKey[ MAX_KEYFRAMES ];
	RGBColorKeyframe m_colorKey[ MAX_KEYFRAMES ];

	Real m_colorScale;													///< color "scaling" coefficient

	Real m_windRandomness;											///< multiplier for wind randomness per particle

	Bool m_particleUpTowardsEmitter;						///< if this is true, then the 0.0 Z rotation should actually
																							///< correspond to the direction of the emitter.

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

};


struct ParticleUpdateContext;

/**
 * An individual particle created by a ParticleSystem.
 * NOTE: Particles cannot exist without a parent particle system.
 */
class Particle : public MemoryPoolObject, 
								 public ParticleInfo
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( Particle, "ParticlePool" )		

public:

	Particle( ParticleSystem *system, const ParticleInfo *data );

	inline Bool update( const ParticleUpdateContext &context );	///< update this particle's behavior - return false if dead
	void doWindMotion( void );									///< do wind motion (if present) from particle system


	inline const Coord3D *getPosition( void ) { return &m_pos; }
	inline Real getSize( void ) { return m_size; }
	inline Real getAngle( void ) { return m_angleZ; }
	inline Real getAlpha( void ) { return m_alpha; }
	inline UnsignedInt getLifetimeLeft( void ) const { return m_lifetimeLeft; }
	inline const RGBColor *getColor( void ) { return &m_color; }
	inline void setColor( RGBColor *color ) { m_color = *color; }

	inline Bool isInvisible( const ParticleUpdateContext &context );	///< return true if this particle is invisible
	inline Bool diesThisUpdate (void) const { return m_diesThisUpdate; }			///< marked dead by ParticleSystem::updateParticlesMark, not yet removed
	inline void setDiesThisUpdate (Bool dies) { m_diesThisUpdate = dies; }

	void controlParticleSystem( ParticleSystem *sys ) { m_systemUnderControl = sys; }
	void detachControlledParticleSystem( void ) { m_systemUnderControl = NULL; }
	
	// get priority of this particle ... which is the priority of the system it belongs to
	ParticlePriorityType getPriority( void );

	UnsignedInt getPersonality(void) { return m_personality; };
	void setPersonality(UnsignedInt p) { m_personality = p; };

	/// The cell of its template's flipbook this particle shows now (ParticleSystemTemplate's Flipbook
	/// fields), counted row by row from the top left; 0 for a card that is not a flipbook.
	UnsignedInt getFlipbookFrame( void ) const;

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	void computeAlphaRate( void );							///< compute alpha rate to get to next key
	void computeColorRate( void );							///< compute color change to get to next key

public:
	Particle *				m_systemNext;
	Particle *				m_systemPrev;
	Particle *				m_overallNext;
	Particle *				m_overallPrev;

protected:
	ParticleSystem *	m_system;										///< the particle system this particle belongs to
	UnsignedInt				m_personality;							    ///< each new particle assigned a number one higher than the previous

	// most of the particle data is derived from ParticleInfo

	Coord3D						m_accel;														///< current acceleration
	Coord3D						m_lastPos;													///< previous position
	UnsignedInt				m_lifetimeLeft;									///< lifetime remaining, if zero -> destroy
	UnsignedInt				m_createTimestamp;							///< frame this particle was created

	Real							m_alpha;																///< current alpha of this particle
	Real							m_alphaRate;														///< current rate of alpha change
	Int								m_alphaTargetKey;												///< next index into key array

	RGBColor					m_color;														///< current color of this particle
	RGBColor					m_colorRate;												///< current rate of color change
	Int								m_colorTargetKey;												///< next index into key array


	Bool							m_diesThisUpdate;											///< set on the job pool, read when the dead are removed; never saved
	UnsignedByte			m_flipbookStart;											///< the flipbook cell it starts on; drawing only, never saved
public:
	Bool							m_inSystemList;
	Bool							m_inOverallList;

	union
	{
		ParticleSystem *	m_systemUnderControl;			///< the particle system attached to this particle (not the system that created us)
		ParticleSystemID	m_systemUnderControlID;	///< id of system attached to this particle (not the system that created us);
	};

};


//--------------------------------------------------------------------------------------------------------------

#ifdef DEFINE_PARTICLE_SYSTEM_NAMES

/**** NOTE: These MUST be kept in sync with the enumerations below *****/

static char *ParticleShaderTypeNames[] = 
{
	"NONE", "ADDITIVE", "ALPHA", "ALPHA_TEST", "MULTIPLY", NULL
};

static char *ParticleTypeNames[] = 
{
	"NONE", "PARTICLE", "DRAWABLE", "STREAK", "VOLUME_PARTICLE","SMUDGE", NULL
};

static char *EmissionVelocityTypeNames[] =
{
	"NONE", "ORTHO", "SPHERICAL", "HEMISPHERICAL", "CYLINDRICAL", "OUTWARD", NULL
};

static char *EmissionVolumeTypeNames[] =
{
	"NONE", "POINT", "LINE", "BOX", "SPHERE", "CYLINDER", NULL
};

//"NONE", "FLUFF", "DEBRIS", "NATURE", "WEAPON", "DAMAGE", "SPECIAL"
static char *ParticlePriorityNames[] = 
{
	"NONE", "WEAPON_EXPLOSION","SCORCHMARK","DUST_TRAIL","BUILDUP","DEBRIS_TRAIL","UNIT_DAMAGE_FX","DEATH_EXPLOSION","SEMI_CONSTANT","CONSTANT","WEAPON_TRAIL","AREA_EFFECT","CRITICAL", "ALWAYS_RENDER", NULL
};

static char *WindMotionNames[] = 
{
	"NONE", "Unused", "PingPong", "Circular", NULL
};

#endif

/**
 * All of the properties of a particle system, used by both ParticleSystemTemplates
 * and ParticleSystem classes.
 */
/**
 * Collide one particle with the ground.  Free of ParticleSystem on purpose: it needs no
 * subsystems, so it can be exercised directly (test_gameengine).  pos/vel are world space and
 * are updated in place; groundZ and normal are what TheTerrainLogic reports under pos.
 * Returns TRUE if the particle was at or below the ground.
 */
extern Bool particleGroundBounce( Coord3D *pos, Coord3D *vel, Real groundZ,
															 const Coord3D *normal, Real bounce, Real friction );

/**
 * The soft blob a whole particle system drops on the terrain.  A cloud of billboards has no
 * silhouette to project, so one decal stands in for the lot: accumulate every live particle's
 * footprint here, then ask Resolve where it goes, how big it is and how dark.
 *
 * Free of ParticleSystem for the same reason particleGroundBounce is - it needs no subsystems,
 * so the whole decision can be driven directly (test_gameengine).
 */
struct ParticleShadowBlob
{
	Int		m_count;						///< particles accumulated
	Real	m_minX, m_maxX;			///< world-space extent of the cloud
	Real	m_minY, m_maxY;
	Real	m_maxSize;					///< the widest particle in it
	Real	m_sumAlpha;					///< summed opacity, the stand-in for optical depth
};

extern void particleShadowBlobReset( ParticleShadowBlob *blob );
extern void particleShadowBlobAdd( ParticleShadowBlob *blob, Real x, Real y, Real size, Real alpha );

/**
 * Turn the accumulated particles into a decal.  Returns FALSE when this system has not earned
 * one - too few particles, particles too small (trails, sparks, muzzle flashes) or no alpha left -
 * which is also the signal to release a decal it already has.
 */
extern Bool particleShadowBlobResolve( const ParticleShadowBlob *blob, Real *centerX, Real *centerY,
																			 Real *sizeX, Real *sizeY, Int *opacity );

/**
 * The optical depth through the middle of one particle in the sun's smoke map: the one its own
 * alpha implies, the one the eye sees through it, times how many layers it is drawn.  A plain
 * billboard is one layer whatever getVolumeParticleDepth says (it answers 0 for anything that is
 * not a volume particle).  Zero for a particle too faint to cast.
 */
extern Real particleSunMapOpticalDepth( Real alpha, UnsignedInt layers );

/**
 * What "-smoke <thickness>" does to one smoke system.  A single number on the command line is
 * spent across three separate properties, because spending all of it on any one of them looks
 * wrong: all on lifetime gives a thin haze that never leaves, all on alpha gives a solid grey
 * brick that vanishes on schedule, all on size gives smoke wider than the building it came from.
 *
 * Free of ParticleSystem for the same reason the blob is - it needs no subsystems.
 */
struct SmokeBoost
{
	Real	m_lifetimeScale;		///< particle lifetime, system lifetime and every keyframe time
	Real	m_alphaScale;				///< opacity of each particle, clamped to fully opaque
	Real	m_sizeScale;				///< how wide each particle starts
};

/**
 * Resolve the command line's thickness into the three scales.  Returns FALSE for a thickness
 * that asks for nothing (anything at or below 1), which is also the "switch not given" case.
 */
extern Bool particleSmokeBoostResolve( Real thickness, SmokeBoost *boost );

/**
 * Does this particle system's name say it is smoke?  The match is on the template name because
 * the shipped ParticleSystem.ini lives inside INIZH.big and there is nothing else to go on.
 */
extern Bool particleSmokeNameMatches( const char *name );

/**
 * The particle ceiling "-smoke" needs.  Smoke that lives longer is smoke that is alive at the
 * same time as more of itself, and removeOldestParticles takes the oldest first - so left at the
 * shipped ceiling, longer-lived smoke is exactly what the cap deletes to make room for sparks.
 */
extern Int particleSmokeParticleCap( Int shippedCap, Real thickness );

class ParticleSystemInfo : public Snapshot
{

public:

	ParticleSystemInfo();												///< to set defaults.

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	Bool m_isOneShot;														///< if true, destroy system after one burst has occurred

	enum ParticleShaderType
	{
		INVALID_SHADER=0, ADDITIVE, ALPHA, ALPHA_TEST, MULTIPLY
	} 
	m_shaderType;																///< how this particle is rendered

	enum ParticleType
	{
		INVALID_TYPE=0, PARTICLE, DRAWABLE, STREAK, VOLUME_PARTICLE, SMUDGE	 ///< is a particle a 2D-screen-facing particle, or a Drawable, or a Segment in a streak?
	}
	m_particleType;

	AsciiString m_particleTypeName;							///< if PARTICLE, texture filename, if DRAWABLE, Drawable name

	GameClientRandomVariable m_angleZ;										///< initial angle around Z axis
	GameClientRandomVariable m_angularRateZ;							///< initial angle around Z axis
	GameClientRandomVariable m_angularDamping;						///< angular velocity damping coefficient

	GameClientRandomVariable m_velDamping;								///< velocity damping factor

	GameClientRandomVariable m_lifetime;									///< lifetime of emitted particles
	UnsignedInt m_systemLifetime;								///< lifetime of the particle system

	GameClientRandomVariable m_startSize;									///< initial size of emitted particles
	GameClientRandomVariable m_startSizeRate;							///< change in start size of emitted particles
	GameClientRandomVariable m_sizeRate;									///< rate of change of size
	GameClientRandomVariable m_sizeRateDamping;						///< damping of size change


	UnsignedInt m_volumeParticleDepth;								///< how many layers deep to draw the particle, if >1

	struct RandomKeyframe
	{
		GameClientRandomVariable var;												///< the range of values at this keyframe
		UnsignedInt frame;												///< the frame number
	};


	RandomKeyframe m_alphaKey[ MAX_KEYFRAMES ];
	RGBColorKeyframe m_colorKey[ MAX_KEYFRAMES ];	///< color of particle

	typedef Int Color;

	void tintAllColors( Color tintColor );
	
	GameClientRandomVariable m_colorScale;								///< color coefficient

	GameClientRandomVariable m_burstDelay;								///< time between particle emissions
	GameClientRandomVariable m_burstCount;								///< number of particles emitted per burst

	GameClientRandomVariable m_initialDelay;							///< delay before particles begin emitting

	Coord3D m_driftVelocity;										///< additional velocity added to all particles
	Real m_gravity;															///< gravity acceleration (global Z) for particles in this system

	// Terrain collision.  Retail particles fall straight through the ground; sparks, debris and
	// smoke all vanish into the hillside instead of hitting it.  Off by default, so every
	// existing particle system keeps its shipped behaviour until its INI opts in.
	Bool m_groundCollision;												///< if true, particles bounce off the terrain instead of passing through
	Real m_groundBounce;													///< fraction of the velocity into the ground that comes back out (0 = dead stop, 1 = elastic)
	Real m_groundFriction;												///< fraction of the velocity along the ground kept per contact (1 = frictionless slide)

	AsciiString m_slaveSystemName;							///< if non-empty, create a system whose particles track this system's
	Coord3D m_slavePosOffset;										///< positional offset of slave particles relative to master's

	AsciiString m_attachedSystemName;						///< if non-empty, create a system attached to each particle of this system

	//-------------------------------------------------------
	// The direction and speed at which particles are emitted
	enum EmissionVelocityType
	{
		INVALID_VELOCITY=0, ORTHO, SPHERICAL, HEMISPHERICAL, CYLINDRICAL, OUTWARD
	}
	m_emissionVelocityType;

	ParticlePriorityType m_priority;

	union
	{
		struct
		{
			GameClientRandomVariable x;												///< initial speed of particle along X axis
			GameClientRandomVariable y;												///< initial speed of particle along Y axis
			GameClientRandomVariable z;												///< initial speed of particle along Z axis
		}
		ortho;

		struct
		{
			GameClientRandomVariable speed;										///< initial speed of particle along random radial direction
		}
		spherical, hemispherical;

		struct
		{
			GameClientRandomVariable radial;									///< initial speed of particle in the disk
			GameClientRandomVariable normal;									///< initial speed of particle perpendicular to disk
		}
		cylindrical;

		struct
		{
			GameClientRandomVariable speed;										///< speed outward from emission volume
			GameClientRandomVariable otherSpeed;							///< speed along "other" axis, such as cylinder length
		}
		outward;
	}
	m_emissionVelocity;

	//----------------------------------------------------------
	// The volume of space where particles are initially created
	// Note that the volume is relative to the system's position and orientation
	enum EmissionVolumeType
	{
		INVALID_VOLUME=0, POINT, LINE, BOX, SPHERE, CYLINDER
	}
	m_emissionVolumeType;												///< the type of volume where particles are created
	
	union emissionVolumeUnion
	{
		// point just uses system's position

		// line
		struct
		{
			Coord3D start, end;
		}
		line;

		// box
		struct
		{
			Coord3D halfSize;
		}
		box;

		// sphere
		struct
		{
			Real radius;
		}
		sphere;

		// cylinder
		struct
		{
			Real radius;
			Real length;
		}
		cylinder;
	}
	m_emissionVolume;														///< the dimensions of the emission volume

	Bool m_isEmissionVolumeHollow;							///< if true, only create particles at boundary of volume
	Bool m_isGroundAligned;											///< if true, align with the ground. if false, then do the normal billboarding. 
	Bool m_isEmitAboveGroundOnly;								///< if true, only emit particles when the system is above ground.
	Bool m_isParticleUpTowardsEmitter;					///< if true, align the up direction to be towards the emitter.

	enum WindMotion
	{
		WIND_MOTION_INVALID = 0,
		WIND_MOTION_NOT_USED,
		WIND_MOTION_PING_PONG,
		WIND_MOTION_CIRCULAR
	};
	WindMotion m_windMotion;				///< motion of the wind angle updating
	Real m_windAngle;								///< angle of the "wind" associated with this system
	Real m_windAngleChange;					///< current how fast the angle changes (higher=faster change)
	Real m_windAngleChangeMin;			///< min for angle change
	Real m_windAngleChangeMax;			///< max for angle change
	Real m_windMotionStartAngle;						///< (for ping pong) angle 1 of the ping pong
	Real m_windMotionStartAngleMin;					///< (for ping pong) min angle for angle 1
	Real m_windMotionStartAngleMax;					///< (for ping pong) max angle for angle 1
	Real m_windMotionEndAngle;							///< (for ping pong) angle 2 of the ping pong
	Real m_windMotionEndAngleMin;						///< (for ping pong) min angle for angle 2
	Real m_windMotionEndAngleMax;						///< (for ping pong) max angel for angle 2
	Byte m_windMotionMovingToEndAngle;			///< (for ping pong) TRUE if we're moving "towards" the end angle

};

/**
 * What every particle of one system reads during one update, looked up once for the system.  The
 * particles used to ask their system for each of these, and the client for the frame twice, once
 * a particle: with a hundred thousand of them Particle::update was 13% of the frame's samples.
 */
struct ParticleUpdateContext
{
	Coord3D																	driftVelocity;
	Real																		gravity;
	UnsignedInt															clientFrame;
	ParticleSystemInfo::ParticleShaderType	shaderType;
	ParticleSystemInfo::WindMotion					windMotion;
	Bool																		groundCollision;	///< the system asked for it and there is terrain to hit
	Real																		groundBounce;
	Real																		groundFriction;
};


/**
 * A ParticleSystemTemplate, used by the ParticleSystemManager to instantiate ParticleSystems.
 */
class ParticleSystemTemplate : public MemoryPoolObject, protected ParticleSystemInfo
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ParticleSystemTemplate, "ParticleSystemTemplatePool" )		

public:

	/** Force terrain collision on for a system whose INI never asked for it ("-particlebounce").
		* ParticleSystemInfo is a protected base, so the manager needs a way in. */
	void forceGroundCollision( Real bounce, Real friction )
	{ m_groundCollision = TRUE; m_groundBounce = bounce; m_groundFriction = friction; }

	/** Make this system's smoke last longer and read thicker ("-smoke").  Same reason as above:
		* ParticleSystemInfo is a protected base, so the manager needs a way in. */
	void forceSmokeBoost( const SmokeBoost &boost );

	/** Classic Graphics: a system only the fork has, reached from data every machine loads, draws nothing. */
	void emitNothing( void )
	{
		m_burstCount.setRange( 0.0f, 0.0f );
		m_systemLifetime = 1;
		m_slaveSystemName.clear();
		m_attachedSystemName.clear();
	}
	void copyLookOf( const ParticleSystemTemplate *other ) { static_cast<ParticleSystemInfo&>( *this ) = *other; }

	ParticleSystemTemplate( const AsciiString &name );

	AsciiString getName( void ) const { return m_name; }

	/** "ExplosionDebrisTrail = Yes": one of the fork's smoke-trailing blast fragments, which the
		* player's DebrisTrails option turns off.  A system built from it while the option is off is
		* destroyed before it emits, so every FXList and object creation list stays the same for everyone. */
	Bool isExplosionDebrisTrail( void ) const { return m_explosionDebrisTrail; }
	void setExplosionDebrisTrail( Bool on ) { m_explosionDebrisTrail = on; }

	/** "FireLook = Yes": a flame or explosion system, whose billboards the Direct3D 11 frame draws
		* with a temperature ramp over their life, edges that burn away and a flicker
		* (W3DParticleSys.cpp, FIRE_LOOK_SAMPLING in ffshader.h).  Drawing only. */
	Bool isFireLook( void ) const { return m_fireLook; }

	/** "FlipbookGrid = 8": the card is a square atlas of 8 by 8 frames (2, 4, 8 or 16 a side), row by
		* row from the top left, and each particle shows one at a time.  "FlipbookRate" is frames a
		* second, looping; 0 (the default) plays the frames once over the particle's life.
		* "FlipbookRandomStart = Yes" starts each particle on a frame from the client's random stream,
		* and a particle playing once then runs from there to the last.  Drawing only, and only for
		* billboards and the point group: a streak or a ground field still draws the whole atlas. */
	/** "Emissive = Yes": an alpha blended system that gives light rather than takes it, a fireball's
		* body: no glow from nearby fires and lights, and nothing cast into the sun's smoke map, so it
		* neither shades itself nor darkens the ground under it.  Drawing only. */
	Bool isEmissive( void ) const { return m_emissive; }

	UnsignedInt getFlipbookGridLog2( void ) const { return m_flipbookGridLog2; }
	Real getFlipbookRate( void ) const { return m_flipbookRate; }

	/// Whether the GPU path's closed form can draw this template's particles (ParticleSys.cpp says why
	/// each test is there); the manager still has to accept GPU particles at all.
	Bool isGpuEligible( void ) const;
	const AsciiString &getParticleTypeName( void ) const { return m_particleTypeName; }
	Bool isFlipbookRandomStart( void ) const { return m_flipbookRandomStart; }

	// This function was made const because of update modules' module data being all const.
	ParticleSystem *createSlaveSystem( Bool createSlaves = TRUE ) const ;					///< if returns non-NULL, it is a slave system for use

	const FieldParse *getFieldParse( void ) const { return m_fieldParseTable; }	///< Parsing from INI access
	static void parseRGBColorKeyframe( INI* ini, void *instance, void *store, const void* /*userData*/ );
	static void parseRandomKeyframe( INI* ini, void *instance, void *store, const void* /*userData*/ );
	static void parseRandomRGBColor( INI* ini, void *instance, void *store, const void* /*userData*/ );
	static void parseRandomRGBColorRate( INI* ini, void *instance, void *store, const void* /*userData*/ );

protected:
	friend class ParticleSystemManager;					///< @todo remove this friendship
	friend class ParticleSystem;								///< @todo remove this friendship
	
	// These friendships are naughty but necessary for particle editing.
	friend class DebugWindowDialog;							///< @todo remove this friendship when no longer editing particles
	friend void _updateAsciiStringParmsToSystem(ParticleSystemTemplate *particleTemplate);
	friend void _updateAsciiStringParmsFromSystem(ParticleSystemTemplate *particleTemplate);
	friend void _writeSingleParticleSystem( File *out, ParticleSystemTemplate *templ );

	static const FieldParse m_fieldParseTable[];			///< the parse table for INI definition

protected:
	AsciiString								m_name;													///< the name of this template
	Bool											m_explosionDebrisTrail;						///< switched off with the DebrisTrails option (client only)
	Bool											m_fireLook;												///< drawn with the Direct3D 11 fire look (client only)
	Bool											m_emissive;												///< alpha blended but takes no light and casts no smoke shadow (client only)
	UnsignedByte							m_flipbookGridLog2;								///< log2 of the flipbook's frames a side, 0 for a plain card (client only)
	Real											m_flipbookRate;										///< flipbook frames a second, looping; 0 plays once over the life (client only)
	Bool											m_flipbookRandomStart;						///< each particle starts on a random flipbook frame (client only)

	// This has to be mutable because of the delayed initialization thing in createSlaveSystem
	mutable const ParticleSystemTemplate *m_slaveTemplate;		///< if non-NULL, use this to create a slave system

	// template attribute data inherited from ParticleSystemInfo class
};

/**
 * A particle system, responsible for creating Particles.
 * If a particle system has finished, but still has particles "in the air", it must wait
 * before destroying itself in order to ensure everything can be cleaned up if the system
 * is reset.
 */
class ParticleSystem : public MemoryPoolObject, 
											 public ParticleSystemInfo
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ParticleSystem, "ParticleSystemPool" )		

public:

	ParticleSystem( const ParticleSystemTemplate *sysTemplate, 
									ParticleSystemID id,
									Bool createSlaves );			///< create a particle system from a template and assign it this ID

	inline ParticleSystemID getSystemID( void ) const { return m_systemID; }	///< get unique system ID

	void setPosition( const Coord3D *pos );			///< set the position of the particle system
	void getPosition( Coord3D *pos );				///< get the position of the particle system
	void setLocalTransform( const Matrix3D *matrix );	///< set the system's local transform
	void rotateLocalTransformX( Real x );				///< rotate local transform matrix
	void rotateLocalTransformY( Real y );				///< rotate local transform matrix
	void rotateLocalTransformZ( Real z );				///< rotate local transform matrix
  void setSkipParentXfrm(Bool enable) { m_skipParentXfrm = enable; } ///<disable transforming particle system with parent matrix.
		
	const Coord3D *getDriftVelocity( void ) { return &m_driftVelocity; }	///< get the drift velocity of the system
	Coord3D getDriftWithWeather( void ) const;	///< that, plus the Weather option's wind on what it leans
	Bool isGroundCollision( void ) const { return m_groundCollision; }		///< do this system's particles hit the terrain?
	Real getGroundBounce( void ) const { return m_groundBounce; }				///< restitution against the terrain
	Real getGroundFriction( void ) const { return m_groundFriction; }		///< drag along the terrain

	void attachToDrawable( const Drawable *draw );							///< attach this particle system to a Drawable
	void attachToObject( const Object *obj );									///< attach this particle system to an Object
	void attachToObjectID( ObjectID id ) { m_attachedToObjectID = id; }	///< the same, for an object that may be gone already

	/** LOGIC-THREAD.md phase 2, M5.  A system made inside a tick is a shell until the end of the tick:
		* its ID and everything copied from the template, so logic can place it, attach it, stop it and
		* keep its ID.  The client stream's draws, the slave, the manager's list and the DebrisTrails
		* option wait for this, which the constructor runs at once outside a tick. */
	void buildClientPart( Bool createSlaves );
	Bool isShell( void ) const { return m_isShell; }

	virtual Bool update( Int localPlayerIndex );								///< update this particle system, return false if dead

	/// update() in three parts, for a system no other system reads or writes, so the manager can run
	/// the middle one on the job pool: the emission serially in list order, the particles anywhere,
	/// then the dead particles, the ground blob and the lifetime serially in list order again.
	Bool isUpdateIndependent( void ) const;
	Bool updateEmission( Int localPlayerIndex, Bool *keepSystem );	///< FALSE when the update ended before the particles; *keepSystem is then the answer
	void updateParticlesMark( void );			///< job-safe: integrate every particle and mark the ones that die
	Bool reapAndFinishUpdate( void );			///< remove the marked particles and finish; return false if the system is done
	void updateWindMotion( void );							///< update wind motion

	void setControlParticle( Particle *p );			///< set control particle

	void start( void );													///< (re)start a stopped particle system
	void stop( void );													///< stop a particle system from emitting
	void destroy( void );												///< stop emitting, and wait for particles to die, then destroy self

	void setVelocityMultiplier( const Coord3D *value ) { m_velCoeff = *value; }	///< set the velocity multiplier coefficient
	const Coord3D *getVelocityMultiplier( void ) { return &m_velCoeff; }					///< get the velocity multiplier coefficient

	void setBurstCountMultiplier( Real value ) { m_countCoeff = value; }
	Real getBurstCountMultiplier( void ) { return m_countCoeff; }

	void setBurstDelayMultiplier( Real value ) { m_delayCoeff = value; }
	Real getBurstDelayMultiplier( void ) { return m_delayCoeff; }

	void setSizeMultiplier( Real value ) { m_sizeCoeff = value; }
	Real getSizeMultiplier( void ) { return m_sizeCoeff; }

	/// Force a burst of particles to be emitted immediately.
	void trigger (void) { m_burstDelayLeft = 0; m_delayLeft = 0;}

	void setInitialDelay( UnsignedInt delay ) { m_delayLeft = delay; m_initialDelaySet = TRUE; }
	UnsignedInt getInitialDelayLeft() const { return m_delayLeft; }

	/// the name of the particles; a reference, as the renderer asks it of every system several times
	/// a frame and a copy is an atomic increment and decrement each
	const AsciiString &getParticleTypeName( void ) const { return m_particleTypeName; }
	Bool isUsingDrawables( void ) { return (m_particleType == DRAWABLE) ? true : false; }
	Bool isUsingStreak( void ) { return (m_particleType == STREAK) ? true : false; }
	Bool isUsingSmudge( void ) { return (m_particleType == SMUDGE) ? true : false; }
	/// How many layers deep a volume particle is drawn. `VolumeParticleDepth` in the template sets
	/// it; left at zero the engine picks its own figure, which is what every system did before the
	/// field existed. Only VOLUME_PARTICLE draws layers at all.
	UnsignedInt getVolumeParticleDepth( void )
	{
		if( m_particleType != VOLUME_PARTICLE )
			return 0;
		if( m_volumeParticleDepth == 0 )
			return OPTIMUM_VOLUME_PARTICLE_DEPTH;
		return ( m_volumeParticleDepth > MAX_VOLUME_PARTICLE_DEPTH )
						 ? MAX_VOLUME_PARTICLE_DEPTH : m_volumeParticleDepth;
	}

	/// The renderer put at least one of this system's particles into the sun's smoke map on its
	/// last frame, so the cloud shades the ground already and its blob would shade it twice.
	/// Written by the shadow pass every frame it runs, read by shouldCastGroundShadow.
	/// A mark is the stamp of the fill that set it, so the next fill drops every mark at once with
	/// forgetAllInSunMap rather than finding each system the last one marked.
	void setInSunMap( Bool inSunMap ) { m_inSunMapStamp = inSunMap ? s_sunMapStamp : 0; }
	Bool isInSunMap( void ) const { return m_inSunMapStamp == s_sunMapStamp; }
	static void forgetAllInSunMap( void ) { if( ++s_sunMapStamp == 0 ) s_sunMapStamp = 1; }

	Bool shouldBillboard( void ) { return !m_isGroundAligned; }

	ParticleShaderType getShaderType( void ) { return m_shaderType; }

	void setSlave( ParticleSystem *slave );			///< set a slave system for us
	ParticleSystem *getSlave( void ) { return m_slaveSystem; }
	void setMaster( ParticleSystem *master );  ///< make this a slave with a master
	ParticleSystem *getMaster( void ) { return m_masterSystem; }
	const Coord3D *getSlavePositionOffset( void ) { return &m_slavePosOffset; }
	
	void setSystemLifetime( UnsignedInt frames ) { m_systemLifetimeLeft = frames; }; ///< not the particle life, the system!... Lorenzen
	void setFiniteSystemLifetime( UnsignedInt frames ) { m_isForever = false; m_systemLifetimeLeft = frames; }	///< the same, and it ends even when the template said forever (fork)
	void setLifetimeRange( Real min, Real max );
	Bool isSystemForever() const {return m_isForever;}

	UnsignedInt getStartFrame( void ) { return m_startTimestamp; }	///< return frame when this system was created
	Bool isDestroyed( void ) const { return m_isDestroyed; }

	void setSaveable( Bool b );
	Bool isSaveable( void ) const { return m_isSaveable; }

	/// called when the particle this system is controlled by dies
	void detachControlParticle( Particle *p ) { m_controlParticle = NULL; }

	/// called to merge two systems info. If slaveNeedsFullPromotion is true, then the slave needs to be aware of how many particles
	/// to generate as well.
	static ParticleInfo mergeRelatedParticleSystems( ParticleSystem *masterParticleSystem, ParticleSystem *slaveParticleSystem, Bool slaveNeedsFullPromotion);

	/// @todo Const this (MSB)
	const ParticleSystemTemplate *getTemplate( void ) { return m_template; }	///< get the template used to create this system

	// @todo Const this jkmcd
	Particle *getFirstParticle( void ) { return m_systemParticlesHead; }
	const Particle *getLastParticle( void ) const { return m_systemParticlesTail; }	///< the youngest, the last one emitted
	UnsignedInt getSystemLifetimeLeft( void ) const { return m_systemLifetimeLeft; }

	void addParticle( Particle *particleToAdd );
	/// when a particle dies, it calls this method - ONLY FOR USE BY PARTICLE
	void removeParticle( Particle *p );
	UnsignedInt getParticleCount( void ) const { return m_particleCount; }
	Bool hasGroundShadow( void ) const { return m_groundShadow != NULL; }

	inline ObjectID getAttachedObject( void ) { return m_attachedToObjectID; }
	inline DrawableID getAttachedDrawable( void ) { return m_attachedToDrawableID; }

	// Access to dynamically changing part of a particle system.
	void setEmissionVolumeSphereRadius( Real newRadius ) { if (m_emissionVolumeType == SPHERE) m_emissionVolume.sphere.radius = newRadius; }
	void setEmissionVolumeCylinderRadius( Real newRadius ) { if (m_emissionVolumeType == CYLINDER) m_emissionVolume.cylinder.radius = newRadius; }
	EmissionVolumeType getEmisionVolumeType() const { return m_emissionVolumeType; }
	ParticlePriorityType getPriority() const { return m_priority; }

	// Access to wind motoin
	Real getWindAngle( void ) { return m_windAngle; }
	WindMotion getWindMotion( void ) { return m_windMotion; }

	/// TRUE if this system is the kind of big soft cloud that should shade the ground under it
	Bool shouldCastGroundShadow( void ) const;

	/// How much of this system the local player sees, for the stealth of what it is attached to.
	Real getStealthFade( void ) const;

	/** The GPU particle path (docs/plans/GPU-PARTICLES.md).  An eligible system hands each spawn to
		* the manager's spawnGpuParticle, which keeps it as a spawn record the vertex shader ages in
		* closed form; no Particle is made and nothing on the CPU touches it again.  The system keeps
		* the last few spawns so the client can still estimate what the cloud does (ground blob, fire
		* light) without reading the device's ring back. */
	struct GpuSpawnSample
	{
		ParticleInfo	m_info;					///< the spawn exactly as spawnGpuParticle was handed it
		UnsignedInt		m_birthFrame;		///< ParticleSystemManager::getParticleFrame() at the spawn
	};
	enum { GPU_SPAWN_SAMPLES = 16 };

	Bool isGpuEligible( void ) const { return m_gpuEligible; }								///< decided once, in the constructor
	UnsignedInt getGpuSpawnTotal( void ) const { return m_gpuSpawnTotal; }				///< spawns the GPU took, ever; 0 for a CPU-only system
	UnsignedInt getGpuLastDeathFrame( void ) const { return m_gpuLastDeathFrame; }	///< the update its last GPU particle to go dies in, as a CPU twin would
	UnsignedInt getGpuLifetimeLeft( void ) const;									///< frames until the last GPU particle dies, as Particle::getLifetimeLeft counts; 0 when none is alive
	UnsignedInt getGpuSampleCount( void ) const { return m_gpuSpawnTotal < (UnsignedInt)GPU_SPAWN_SAMPLES ? m_gpuSpawnTotal : (UnsignedInt)GPU_SPAWN_SAMPLES; }
	/// 0 is the newest spawn, getGpuSampleCount() - 1 the oldest kept
	const GpuSpawnSample &getGpuSample( UnsignedInt newestFirst ) const { return m_gpuSamples[ ( m_gpuSpawnTotal - 1 - newestFirst ) % GPU_SPAWN_SAMPLES ]; }
	/// The template asked for terrain collision (its INI, or -particlebounce and the ParticleBounce
	/// option forcing it): the device rests GPU particles on the terrain, without the bounce.
	Bool isGpuGroundClamped( void ) const { return m_template->m_groundCollision; }

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	virtual Particle *createParticle( const ParticleInfo *data,
																		ParticlePriorityType priority,
																		Bool forceCreate = FALSE );	///< factory method for particles


	const ParticleInfo *generateParticleInfo( Int particleNum, Int particleCount );	///< generate a new, random set of ParticleInfo
	const Coord3D *computeParticlePosition( void );		///< compute a position based on emission properties
	const Coord3D *computeParticleVelocity( const Coord3D *pos );	///< compute a velocity vector based on emission properties
	const Coord3D *computePointOnUnitSphere( void );	///< compute a random point on a unit sphere

protected:
	/// refresh (or release) the one decal that shades the ground under this system's particles
	void updateGroundShadow( const ParticleShadowBlob *blob );
	void releaseGroundShadow( void );							///< give the ground blob back, if we have one

	void makeUpdateContext( ParticleUpdateContext *context ) const;
	Bool finishUpdate( const ParticleShadowBlob *blob );	///< the ground blob and the lifetime, after the particles
	void addGpuSamplesToShadowBlob( ParticleShadowBlob *blob ) const;	///< the GPU particles' share of the ground blob, from the samples
	Bool spawnOnGpu( const ParticleInfo *info );	///< hand one spawn to the manager's GPU path; TRUE if it took it

	ParticleShadowBlob	m_pendingShadowBlob;				///< the survivors' footprint, gathered by updateParticlesMark
	Bool								m_pendingCastsGroundShadow;	///< decided by updateEmission, where update() always decided it
	UnsignedInt					m_pendingDeaths;						///< particles updateParticlesMark marked; the reap stops once it has removed them
	UnsignedInt					m_inSunMapStamp;						///< setInSunMap: client-only, never xfered; 0 is never a fill's stamp
	static UnsignedInt	s_sunMapStamp;							///< the current fill's, from 1

protected:
	Particle *				m_systemParticlesHead;
	Particle *				m_systemParticlesTail;

	Shadow *					m_groundShadow;									///< client-only soft blob on the terrain, never xfered
	friend class ParticleSystemManager;					///< it hands us our slot in its list and takes it back
	ParticleSystemPtrListIt	m_managerListIt;					///< where we sit in the manager's master list, so it need not look

	UnsignedInt				m_particleCount;								///< current count of particles for this system
	ParticleSystemID	m_systemID;											///< unique id given to this system from the particle system manager

	DrawableID				m_attachedToDrawableID;					///< if non-zero, system is parented to this Drawable
	ObjectID					m_attachedToObjectID;						///< if non-zero, system is parented to this Object
	DrawableID				m_attachedObjectDrawableForDraw;	///< that Object's drawable at the last update, for getStealthFade inside the overlap window

	Matrix3D					m_localTransform;								///< local orientation & position of system
	Matrix3D					m_transform;										///< composite transform of parent Drawable and local

	UnsignedInt				m_burstDelayLeft;								///< when zero, emit a particle burst
	UnsignedInt				m_delayLeft;										///< decremented until zero

	UnsignedInt				m_startTimestamp;								///< timestamp when this particle system was (re)started
	UnsignedInt				m_systemLifetimeLeft;						///< lifetime remaining for entire particle system
	UnsignedInt				m_personalityStore;							///< increments each time it is aggigned to each new particle
																										///< so that each particle gets an ever greater number

	Real							m_accumulatedSizeBonus;					///< For a system that wants to make particles start bigger and bigger.  StartSizeRate

	Coord3D						m_velCoeff;											///< scalar value multiplied by all velocity parameters
	Real							m_countCoeff;										///< scalar value multiplied by burst count
	Real							m_delayCoeff;										///< scalar value multiplied by burst delay
	Real							m_sizeCoeff;										///< scalar value multiplied by initial size

	Coord3D						m_pos;													///< this is the position to emit at.
	Coord3D						m_lastPos;											///< this is the previous position we emitted at.

	ParticleSystem *	m_slaveSystem;									///< if non-NULL, another system this one has control of
	ParticleSystemID	m_slaveSystemID;								///< id of slave system (if present)

	ParticleSystem *	m_masterSystem;									///< if non-NULL, the system that controls this one
	ParticleSystemID	m_masterSystemID;								///< master system id (if present);

	const ParticleSystemTemplate *	m_template;						///< the template this system was constructed from
	Particle *											m_controlParticle;		///< if non-NULL, this system is controlled by this particle

	Bool							m_isLocalIdentity;										///< if true, the matrix can be ignored
	Bool							m_isIdentity;													///< if true, the matrix can be ignored
	Bool							m_isForever;													///< System has infinite lifetime
	Bool							m_isStopped;													///< if stopped, do not emit particles
	Bool							m_isDestroyed;												///< are we destroyed and waiting for particles to die
	Bool							m_isFirstPos;													///< true if this system hasn't been drawn before.
	Bool							m_isSaveable;													///< true if this system should be saved/loaded
  Bool              m_skipParentXfrm;                     ///< true if this system is already in world space.
	Bool							m_isShell;														///< made inside a tick and not yet built (buildClientPart)
	Bool							m_initialDelaySet;										///< setInitialDelay was called, so the build keeps it over the template's

	Bool							m_gpuEligible;												///< spawns go to the manager's GPU path; client-only, never xfered
	UnsignedInt				m_gpuSpawnTotal;											///< spawns the GPU took; indexes m_gpuSamples
	UnsignedInt				m_gpuLastDeathFrame;									///< the latest frame a GPU spawn of this system dies in
	std::vector<GpuSpawnSample>	m_gpuSamples;								///< ring of the last GPU_SPAWN_SAMPLES spawns, sized on the first


	// the actual particle system data is inherited from ParticleSystemInfo

};


//--------------------------------------------------------------------------------------------------------------
/**
 * The particle system manager, responsible for maintaining all ParticleSystems
 */
class ParticleSystemManager : public SubsystemInterface,
															public Snapshot
{

public:

	typedef ParticleSystemPtrList ParticleSystemList;
	typedef ParticleSystemPtrListIt ParticleSystemListIt;
	typedef std::hash_map< ParticleSystemID, ParticleSystem *, rts::hash<ParticleSystemID>, rts::equal_to<ParticleSystemID> > ParticleSystemIDMap;
	typedef std::hash_map<AsciiString, ParticleSystemTemplate *, rts::hash<AsciiString>, rts::equal_to<AsciiString> > TemplateMap;

	ParticleSystemManager( void );
	virtual ~ParticleSystemManager();

	virtual void init( void );									///< initialize the manager
	virtual void reset( void );									///< reset the manager and all particle systems
	virtual void update( void );								///< update all particle systems

	virtual Int getOnScreenParticleCount( void ) = 0;   ///< returns the number of particles on screen
  virtual void setOnScreenParticleCount(int count);

	ParticleSystemTemplate *findTemplate( const AsciiString &name ) const;
	ParticleSystemTemplate *findParentTemplate( const AsciiString &name, int parentNum ) const;
	ParticleSystemTemplate *newTemplate( const AsciiString &name );
	Bool isClassicReadingForkSystems( void ) const { return m_classicReadingForkSystems; }	///< ParticleSystemReforged.ini going in under Classic Graphics
	/// a map.ini is about to write over this template; reset() puts it back
	void rememberTemplateBeforeMapINI( ParticleSystemTemplate *sysTemplate );

	/// given a template, instantiate a particle system
	ParticleSystem *createParticleSystem( const ParticleSystemTemplate *sysTemplate,
																				Bool createSlaves = TRUE );	

	/** given a template, instantiate a particle system. 
		if attachTo is not null, attach the particle system to the given object.
		return the particle system's ID, NOT its pointer.
	*/
	ParticleSystemID createAttachedParticleSystemID( const ParticleSystemTemplate *sysTemplate,
																				Object* attachTo,
																				Bool createSlaves = TRUE );	

	/// find a particle system given a unique system identifier
	ParticleSystem *findParticleSystem( ParticleSystemID id );

	/// destroy the particle system with the given id (if it still exists)
	void destroyParticleSystemByID(ParticleSystemID id);

	/** Run work on the system with this id if it exists: at once outside a tick, after it for a call
		* from one (LOGIC-THREAD.md M5), where logic may not reach a live system. */
	void withSystem( ParticleSystemID id, const std::function<void( ParticleSystem * )> &work );

	/// return iterators to the particle system template
	TemplateMap::iterator beginParticleSystemTemplate() { return m_templateMap.begin(); }
	TemplateMap::iterator endParticleSystemTemplate() { return m_templateMap.end(); }
	TemplateMap::const_iterator beginParticleSystemTemplate() const { return m_templateMap.begin(); }
	TemplateMap::const_iterator endParticleSystemTemplate() const { return m_templateMap.end(); }
	
	/// destroy attached systems to object
	void destroyAttachedSystems( Object *obj );

	void setLocalPlayerIndex(Int index)	{m_localPlayerIndex=index;}
	void addParticle( Particle *particleToAdd, ParticlePriorityType priority );
	void removeParticle( Particle *particleToRemove );
	Int removeOldestParticles( UnsignedInt count, ParticlePriorityType priorityCap );
	UnsignedInt getParticleCount( void ) const { return m_particleCount; }

	UnsignedInt getFieldParticleCount( void )     const { return m_fieldParticleCount; }
	UnsignedInt getGroundShadowCount( void )      const { return m_groundShadowCount; }

	UnsignedInt getParticleSystemCount( void ) const { return m_particleSystemCount; }

	// @todo const this jkmcd
	ParticleSystemList &getAllParticleSystems( void ) { return m_allParticleSystemList; }
	
	virtual void doParticles(RenderInfoClass &rinfo) = 0;
	virtual void queueParticleRender() = 0;

	/** The GPU particle path (docs/plans/GPU-PARTICLES.md); a device that has one overrides all three.
		* acceptsGpuParticles is asked once per system, in its constructor.  spawnGpuParticle takes one
		* spawn of an eligible system whole or answers FALSE, and the system then makes a CPU Particle.
		* releaseGpuSystem is called from every eligible system's destructor; one destroyed by the
		* manager's own destructor reaches this base no-op, the device's part being gone already. */
	virtual Bool acceptsGpuParticles( void ) const { return FALSE; }
	virtual Bool spawnGpuParticle( ParticleSystem *system, const ParticleInfo *info ) { return FALSE; }
	virtual void releaseGpuSystem( ParticleSystem *system ) {}

	/// The GPU particles' clock: one tick for each update() that runs the systems, which is exactly
	/// how often a CPU Particle ages, where the logic frame runs two or three ahead in a catch-up
	/// pass.  Spawn records stamp their birthFrame from it and age against it.
	static UnsignedInt getParticleFrame( void );

	virtual void preloadAssets( TimeOfDay timeOfDay );

	// these are only for use by partcle systems to link and unlink themselves
	void friend_addParticleSystem( ParticleSystem *particleSystemToAdd );
	void friend_removeParticleSystem( ParticleSystem *particleSystemToRemove );
	// A restored system reads its saved id after the constructor has already filed it under the
	// fresh one, so it tells the manager to re-file it.
	void friend_renumberParticleSystem( ParticleSystem *particleSystem, ParticleSystemID oldID );
	/// a system a tick made waits here for its build after the tick (ParticleSystem::buildClientPart)
	void friend_addShell( ParticleSystem *shell, Bool createSlaves );
	/// LogicThread::OverlapWindow::close, once the tick beside the render is done: what the render
	/// made inside the window joins the list and the lookup
	void friend_fileWindowSystems( void );

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer );
	virtual void xfer( Xfer *xfer );
	virtual void loadPostProcess( void );

	Particle *m_allParticlesHead[ NUM_PARTICLE_PRIORITIES ];
	Particle *m_allParticlesTail[ NUM_PARTICLE_PRIORITIES ];

	ParticleSystemID m_uniqueSystemID;					///< unique system ID to assign to each system created

	ParticleSystemList m_allParticleSystemList;
	ParticleSystemIDMap m_allParticleSystemMap;	///< the same systems, by id, so findParticleSystem() is not a scan

	UnsignedInt m_particleCount;
	UnsignedInt m_fieldParticleCount; ///< this does not need to be xfered, since it is evaluated every frame
	UnsignedInt m_groundShadowCount;	///< systems holding a decal this frame; counted for the PARTICLES log line
	UnsignedInt m_particleSystemCount;
	Int m_onScreenParticleCount;                ///< number of particles displayed on screen per frame
	UnsignedInt m_lastLogicFrameUpdate;
	UnsignedInt m_particleFrame;		///< getParticleFrame; client-only, never xfered
	Int m_localPlayerIndex;	///<used to tell particle systems which particles can be skipped due to player shroud status
	std::vector<ParticleSystem *> m_independentUpdates;	///< this update's systems whose particles go to the job pool; kept so it does not reallocate
	std::vector<ParticleSystem *> m_shells;							///< made inside this tick, built after it; reset() deletes any a cleared game left
	std::vector<ParticleSystem *> m_windowSystems;			///< built by the main thread inside the overlap window, filed when it closes; the tick reads the list and the lookup beside the render

private:
	TemplateMap m_templateMap;		///< a hash map of all particle system templates
	Bool m_classicReadingForkSystems = FALSE;
	std::vector< std::pair<ParticleSystemTemplate *, ParticleSystemInfo> > m_beforeMapOverrides;	///< see reset()
};

/// The particle system manager singleton
extern ParticleSystemManager *TheParticleSystemManager;

class DebugDisplayInterface;
extern void ParticleSystemDebugDisplay( DebugDisplayInterface *dd, void *, FILE *fp = NULL );


#endif // _PARTICLE_SYS_H_
