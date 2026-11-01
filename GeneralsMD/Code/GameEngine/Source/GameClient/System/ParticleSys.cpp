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

// ParticleSys.cpp ////////////////////////////////////////////////////////////////////////////////
// Particle System implementation
// Author: Michael S. Booth, November 2001
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicThread.h"

#include <algorithm>
#include <mutex>

#define DEFINE_PARTICLE_SYSTEM_NAMES

#include "Common/FileSystem.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/PerfTimer.h"
#include "Common/JobSystem.h"
#include "Common/ThingFactory.h"
#include "Common/GameLOD.h"
#include "Common/Xfer.h"

#include "GameClient/Drawable.h"
#include "GameClient/DebugDisplay.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Shadow.h"
#include "GameClient/Snow.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//------------------------------------------------------------------------------ Performance Timers 
//#include "Common/PerfMetrics.h"
//#include "Common/PerfTimer.h"

//static PerfTimer s_particleSys("ParticleSys::update", false, PERFMETRICS_LOGIC_STARTFRAME, PERFMETRICS_LOGIC_STOPFRAME);
//-------------------------------------------------------------------------------------------------
/* An INI timer, in frames.  A negative one (no shipped particle system has one) never runs out, as the
	 Windows build is understood to behave; C leaves converting it to an unsigned count undefined, and
	 ARM64 made it 0: a system that started at once, and a negative BurstDelay that burst every frame.
	 "Never" is 2^24 frames, six days, small enough that the burst delay's LOD coefficient cannot push
	 it out of range.  Anything from -1 up converts as it always did. */
static UnsignedInt particleTimerFrames( Real value )
{
	const UnsignedInt never = 1u << 24;
	if( !(value > -1.0f) || !(value < (Real)never) )
		return never;
	return (UnsignedInt)value;
}

//-------------------------------------------------------------------------------------------------
/* The particles step once a logic frame, and every number in a template was written for retail's 30
	 of them a second.  So each is converted as it is read: a frame count doubled, an amount added every
	 frame halved, gravity quartered, a factor multiplied in every frame square-rooted.  A burst comes
	 BurstDelay + 1 frames after the last one, so the delay doubles plus one: BurstDelay 0, a burst
	 every retail frame, becomes one every second frame and the particles a second stay what they were. */
static void scaleRandomVariable( void *store, Real (*convert)( Real ) )
{
	GameClientRandomVariable *var = (GameClientRandomVariable *)store;
	var->setRange( convert( var->getMinimumValue() ), convert( var->getMaximumValue() ), var->getDistributionType() );
}

static Real retailBurstDelay( Real delay )
{
	return ConvertRetailFrames( delay ) + (Real)(LOGICFRAMES_PER_RETAIL_FRAME - 1);
}

static void parseRetailFramesRandom( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseGameClientRandomVariable( ini, instance, store, userData );
	scaleRandomVariable( store, ConvertRetailFrames );
}

static void parseRetailBurstDelayRandom( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseGameClientRandomVariable( ini, instance, store, userData );
	scaleRandomVariable( store, retailBurstDelay );
}

static void parseRetailPerFrameRandom( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseGameClientRandomVariable( ini, instance, store, userData );
	scaleRandomVariable( store, ConvertRetailPerFrameRate );
}

static void parseRetailPerFrameFactorRandom( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseGameClientRandomVariable( ini, instance, store, userData );
	scaleRandomVariable( store, ConvertRetailPerFrameFactor );
}

/// "FlipbookGrid": frames a side, kept as its log2 because the point group's frame tables are indexed
/// that way.  1 is a plain card; anything but a power of two up to 16 stops the load.
static void parseFlipbookGrid( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	const UnsignedInt side = INI::scanUnsignedInt( ini->getNextToken() );
	UnsignedByte log2 = 0;
	while (log2 < 4 && (1u << log2) < side)
		++log2;
	if ((1u << log2) != side)
		throw INI_INVALID_DATA;
	*(UnsignedByte *)store = log2;
}

static void parseRetailPerFrameSquaredReal( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseReal( ini, instance, store, userData );
	*(Real *)store = ConvertRetailPerFrameSquared( *(Real *)store );
}

static void parseRetailPerFrameCoord3D( INI *ini, void *instance, void *store, const void *userData )
{
	INI::parseCoord3D( ini, instance, store, userData );
	Coord3D *v = (Coord3D *)store;
	v->x = ConvertRetailPerFrameRate( v->x );
	v->y = ConvertRetailPerFrameRate( v->y );
	v->z = ConvertRetailPerFrameRate( v->z );
}

//-------------------------------------------------------------------------------------------------
/* A particle whose keys never reach zero alpha (or black, or white for MULTIPLY) would vanish with
	 whatever it showed on its last frame.  Over the last frames of its life its strength is capped by
	 an envelope falling linearly to zero, so it fades instead.  The window is a quarter of the
	 lifetime, at most eight retail frames.  A cap rather than a multiplier: a particle already fading
	 below the envelope is left exactly as authored. */
static const UnsignedInt PARTICLE_FADE_MAX_FRAMES = 8 * LOGICFRAMES_PER_RETAIL_FRAME;
static const UnsignedInt PARTICLE_FADE_LIFETIME_DIVISOR = 4;

//-------------------------------------------------------------------------------------------------

// the singleton
ParticleSystemManager *TheParticleSystemManager = NULL;

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleInfo::ParticleInfo( void )
{
	//Added By Sadullah Nader
	//Initializations inserted
	m_angleZ = 0.0f;
	m_angularDamping = 0.0f;
	m_angularRateZ = 0.0f;
	m_colorScale =0.0f;
  m_size = 0.0f;
	m_sizeRate = 0.0f;
	m_sizeRateDamping = 0.0f;
	m_velDamping = 0.0f;
	m_windRandomness = 0.0f;

	m_emitterPos.zero();
	m_pos.zero();
	m_vel.zero();

	m_lifetime = 0;
	m_particleUpTowardsEmitter = FALSE;
	
	//
}  // end ParticleInfo

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::xfer( Xfer *xfer )
{
	Int i;

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// velocity
	xfer->xferCoord3D( &m_vel );

	// position
	xfer->xferCoord3D( &m_pos );

	// emitter position
	xfer->xferCoord3D( &m_emitterPos );

	// velocity damping
	xfer->xferReal( &m_velDamping );

	// angle
	Real tempAngle=0;	//temporary value to save out for backwards compatibility when we supported x,y
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &m_angleZ );

	// angular rate
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &tempAngle );
	xfer->xferReal( &m_angularRateZ );

	// lifetime
	xfer->xferUnsignedInt( &m_lifetime );

	// size
	xfer->xferReal( &m_size );

	// size rate
	xfer->xferReal( &m_sizeRate );

	// size rate damping
	xfer->xferReal( &m_sizeRateDamping );

	// alpha keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferReal( &m_alphaKey[ i ].value );
		xfer->xferUnsignedInt( &m_alphaKey[ i ].frame );

	}  // end for, i

	// color keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferRGBColor( &m_colorKey[ i ].color );
		xfer->xferUnsignedInt( &m_colorKey[ i ].frame );

	}  // end for, i

	// color scale
	xfer->xferReal( &m_colorScale );

	// particle up towards emitter
	xfer->xferBool( &m_particleUpTowardsEmitter );

	// wind randomness
	xfer->xferReal( &m_windRandomness );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleInfo::loadPostProcess( void )
{

}  // end loadPostProcess

/** Load post process */
// ------------------------------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
enum
{
	MAX_SIZE_BONUS = 50
};


//todo move this somewhere more useful.
static Real angleBetween(const Coord2D *vecA, const Coord2D *vecB);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Particle ///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** Compute alpha rate to get to next key on given frame */
// ------------------------------------------------------------------------------------------------
void Particle::computeAlphaRate( void )
{
	if (m_alphaKey[ m_alphaTargetKey ].frame == 0)
	{
		m_alphaRate = 0.0f;
		return;
	}

	Real delta = m_alphaKey[ m_alphaTargetKey ].value - m_alphaKey[ m_alphaTargetKey-1 ].value;
	UnsignedInt time = m_alphaKey[ m_alphaTargetKey ].frame - m_alphaKey[ m_alphaTargetKey-1 ].frame;

	m_alphaRate = delta/time;
}

// ------------------------------------------------------------------------------------------------
/** Compute color rate to get to next key on given frame */
// ------------------------------------------------------------------------------------------------
void Particle::computeColorRate( void )
{
	if (m_colorKey[ m_colorTargetKey ].frame == 0)
	{
		m_colorRate.red = 0.0f;
		m_colorRate.green = 0.0f;
		m_colorRate.blue = 0.0f;
		return;
	}

	UnsignedInt time = m_colorKey[ m_colorTargetKey ].frame - m_colorKey[ m_colorTargetKey-1 ].frame;
	Real delta = m_colorKey[ m_colorTargetKey ].color.red - m_colorKey[ m_colorTargetKey-1 ].color.red;
	m_colorRate.red = delta/time;

	delta = m_colorKey[ m_colorTargetKey ].color.green - m_colorKey[ m_colorTargetKey-1 ].color.green;
	m_colorRate.green = delta/time;

	delta = m_colorKey[ m_colorTargetKey ].color.blue - m_colorKey[ m_colorTargetKey-1 ].color.blue;
	m_colorRate.blue = delta/time;
}

// ------------------------------------------------------------------------------------------------
/** Construct a particle from a particle template */
// ------------------------------------------------------------------------------------------------
Particle::Particle( ParticleSystem *system, const ParticleInfo *info )
{
	m_system = system;

	m_diesThisUpdate = FALSE;
	m_accel.x = 0.0f;
	m_accel.y = 0.0f;
	m_accel.z = 0.0f;

	m_vel = info->m_vel;
	m_pos = info->m_pos;

	m_angleZ = info->m_angleZ;

	//Added By Sadullah Nader
	//Initializations inserted
	m_lastPos.zero();
	//
	m_windRandomness = info->m_windRandomness;	
	m_particleUpTowardsEmitter = info->m_particleUpTowardsEmitter;
	m_emitterPos = info->m_emitterPos;

	
	m_angularRateZ = info->m_angularRateZ;
	m_angularDamping = info->m_angularDamping;

	m_velDamping = info->m_velDamping;

	m_lifetime = info->m_lifetime;
	m_lifetimeLeft = info->m_lifetime;
	m_createTimestamp = TheGameClient->getFrame();
	m_personality = 0;

	// Drawn only when the system's template is a flipbook that asks for it, so every other system
	// leaves the client's random stream as it was.
	const ParticleSystemTemplate *flipbook = system->getTemplate();
	m_flipbookStart = 0;
	if (flipbook->getFlipbookGridLog2() != 0 && flipbook->isFlipbookRandomStart())
		m_flipbookStart = (UnsignedByte)GameClientRandomValue( 0, ( 1 << ( 2 * flipbook->getFlipbookGridLog2() ) ) - 1 );

	m_size = info->m_size;
	m_sizeRate = info->m_sizeRate;
	m_sizeRateDamping = info->m_sizeRateDamping;

	// set up alpha
	// i is used after the loop; VC6 for-scope let it escape.
	int i;
	for( i =0; i<MAX_KEYFRAMES; i++ )
		m_alphaKey[i] = info->m_alphaKey[i];

	m_alpha = m_alphaKey[0].value;
	m_alphaTargetKey = 1;
	computeAlphaRate();

	// set up colors
	for( i=0; i<MAX_KEYFRAMES; i++ )
		m_colorKey[i] = info->m_colorKey[i];

	m_color = m_colorKey[0].color;
	m_colorTargetKey = 1;
	computeColorRate();

	m_colorScale = info->m_colorScale;

	m_inSystemList = m_inOverallList = FALSE;
	m_systemPrev = m_systemNext = m_overallPrev = m_overallNext = NULL;

	// add this particle to the global list, retaining particle creation order
	TheParticleSystemManager->addParticle(this, system->getPriority() );

	// add this particle to the Particle System list, retaining local creation order
	m_system->addParticle(this);

	//DEBUG_ASSERTLOG(!(totalParticleCount % 100 == 0), ( "TotalParticleCount = %d\n", m_totalParticleCount ));
}

// ------------------------------------------------------------------------------------------------
/** Destructor */
// ------------------------------------------------------------------------------------------------
Particle::~Particle()
{
	// tell the particle system that this particle is gone
	m_system->removeParticle( this );

	// if this particle was controlling another particle system, destroy that system
	if (m_systemUnderControl)
	{
		m_systemUnderControl->detachControlParticle( this );
		m_systemUnderControl->destroy();
	}
	m_systemUnderControl = NULL;

	// remove from the global list
	TheParticleSystemManager->removeParticle(this);

	//DEBUG_ASSERTLOG(!(totalParticleCount % 100 == 0), ( "TotalParticleCount = %d\n", m_totalParticleCount ));
}

// ------------------------------------------------------------------------------------------------
/** The age is the logic frames the particle has lived, so the frames run on the game's clock and stop
	* with it.  A particle with no lifetime never ages and keeps its first frame.  Reads only the
	* particle and its template, so the render's fill jobs may call it. */
// ------------------------------------------------------------------------------------------------
UnsignedInt Particle::getFlipbookFrame( void ) const
{
	const ParticleSystemTemplate *flipbook = m_system->getTemplate();
	const UnsignedInt frames = 1u << ( 2 * flipbook->getFlipbookGridLog2() );
	if (frames == 1 || m_lifetime == 0)
		return m_flipbookStart;

	const UnsignedInt age = m_lifetime - m_lifetimeLeft;
	if (flipbook->getFlipbookRate() > 0.0f)
		return ( m_flipbookStart + (UnsignedInt)( age * flipbook->getFlipbookRate() * SECONDS_PER_LOGICFRAME_REAL ) ) % frames;

	const UnsignedInt frame = m_flipbookStart + (UnsignedInt)( (Real)age / (Real)m_lifetime * (Real)( frames - m_flipbookStart ) );
	return frame < frames ? frame : frames - 1;
}

// ------------------------------------------------------------------------------------------------
/** See ParticleSys.h.  The density scales the optical depth the alpha implies and is untuned; a
 * particle fainter than the thinnest casts nothing. */
// ------------------------------------------------------------------------------------------------
Real particleSunMapOpticalDepth( Real alpha, UnsignedInt layers )
{
	const Real SUN_MAP_DENSITY = 1.0f;
	const Real SUN_MAP_THINNEST = 0.02f;
	if (alpha < SUN_MAP_THINNEST)
		return 0.0f;
	if (alpha > 0.95f)
		alpha = 0.95f;
	return -(Real)log( 1.0f - alpha ) * (Real)( layers > 1 ? layers : 1 ) * SUN_MAP_DENSITY;
}

// ------------------------------------------------------------------------------------------------
/** The soft blob a whole particle system drops on the terrain.  See ParticleSys.h - free of
 * ParticleSystem so the whole decision is testable without a renderer. */
// ------------------------------------------------------------------------------------------------
void particleShadowBlobReset( ParticleShadowBlob *blob )
{
	blob->m_count = 0;
	blob->m_minX = blob->m_maxX = 0.0f;
	blob->m_minY = blob->m_maxY = 0.0f;
	blob->m_maxSize = 0.0f;
	blob->m_sumAlpha = 0.0f;
}

// ------------------------------------------------------------------------------------------------
void particleShadowBlobAdd( ParticleShadowBlob *blob, Real x, Real y, Real size, Real alpha )
{
	if (blob->m_count == 0)
	{
		blob->m_minX = blob->m_maxX = x;
		blob->m_minY = blob->m_maxY = y;
	}
	else
	{
		if (x < blob->m_minX) blob->m_minX = x;
		if (x > blob->m_maxX) blob->m_maxX = x;
		if (y < blob->m_minY) blob->m_minY = y;
		if (y > blob->m_maxY) blob->m_maxY = y;
	}

	if (size > blob->m_maxSize)
		blob->m_maxSize = size;

	if (alpha > 0.0f)
		blob->m_sumAlpha += alpha;

	blob->m_count++;
}

// ------------------------------------------------------------------------------------------------
Bool particleShadowBlobResolve( const ParticleShadowBlob *blob, Real *centerX, Real *centerY,
																Real *sizeX, Real *sizeY, Int *opacity )
{
	// A handful of tiny motes is a bullet trail, a muzzle flash or a spark shower.  A cloud that
	// has faded out is not drawn any more and must not leave a stain.  None of them earn a decal,
	// and saying so here is also how a system that had one gives it back.
	const Int   MIN_PARTICLES = 3;
	const Real  MIN_PARTICLE_SIZE = 15.0f;		///< world units across
	const Real  MAX_BLOB_SIZE = 300.0f;				///< a wind-blown system must not smear over the map
	const Real  ALPHA_SATURATION = 6.0f;			///< summed alpha at which the blob is as dark as it gets
	const Real  MAX_OPACITY = 80.0f;					///< smoke shades the ground, it never blacks it out

	if (blob->m_count < MIN_PARTICLES)
		return FALSE;

	if (blob->m_maxSize < MIN_PARTICLE_SIZE)
		return FALSE;

	if (blob->m_sumAlpha <= 0.0f)
		return FALSE;

	*centerX = (blob->m_minX + blob->m_maxX) * 0.5f;
	*centerY = (blob->m_minY + blob->m_maxY) * 0.5f;

	// the cloud's own spread, widened by one particle so the edge particles are covered too
	Real spreadX = (blob->m_maxX - blob->m_minX) + blob->m_maxSize;
	Real spreadY = (blob->m_maxY - blob->m_minY) + blob->m_maxSize;
	*sizeX = spreadX > MAX_BLOB_SIZE ? MAX_BLOB_SIZE : spreadX;
	*sizeY = spreadY > MAX_BLOB_SIZE ? MAX_BLOB_SIZE : spreadY;

	Real depth = blob->m_sumAlpha / ALPHA_SATURATION;
	if (depth > 1.0f)
		depth = 1.0f;
	*opacity = REAL_TO_INT( depth * MAX_OPACITY );

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** Spend "-smoke <thickness>" across the three properties that make smoke read as smoke.  See
 * ParticleSys.h for why it is not all spent on one of them. */
// ------------------------------------------------------------------------------------------------
Bool particleSmokeBoostResolve( Real thickness, SmokeBoost *boost )
{
	// Past this the cloud stops being smoke and starts being a grey wall that never lifts, and
	// the particle ceiling below cannot keep up with it either.
	const Real MAX_THICKNESS = 8.0f;

	// Of everything asked for beyond the shipped value, this share goes to opacity and this share
	// to particle width; the rest goes to time.  Both are well under half deliberately: doubling
	// the life of a cloud is barely noticeable, doubling its opacity is not subtle at all.
	const Real ALPHA_SHARE = 0.35f;
	const Real SIZE_SHARE = 0.25f;

	if (thickness <= 1.0f)
		return FALSE;

	if (thickness > MAX_THICKNESS)
		thickness = MAX_THICKNESS;

	Real excess = thickness - 1.0f;
	boost->m_lifetimeScale = thickness;
	boost->m_alphaScale = 1.0f + excess * ALPHA_SHARE;
	boost->m_sizeScale = 1.0f + excess * SIZE_SHARE;

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
static char particleNameLowerCased( char letter )
{
	return (letter >= 'A' && letter <= 'Z') ? (char)(letter - 'A' + 'a') : letter;
}

// ------------------------------------------------------------------------------------------------
/** Case-insensitive search for "smoke" anywhere in a template name.  Every shipped system is
 * named by hand and 175 of the 1088 in INIZH.big carry the word, from SmokeTrail through
 * BuildingSmokeLarge; matching the word is the only handle there is without editing an INI that
 * lives inside a .big. */
// ------------------------------------------------------------------------------------------------
Bool particleSmokeNameMatches( const char *name )
{
	static const char WORD[] = "smoke";
	const Int WORD_LENGTH = sizeof(WORD) - 1;

	if (name == NULL)
		return FALSE;

	for( const char *at = name; *at != '\0'; ++at )
	{
		Int matched = 0;
		while( matched < WORD_LENGTH && particleNameLowerCased( at[ matched ] ) == WORD[ matched ] )
			++matched;

		if (matched == WORD_LENGTH)
			return TRUE;
	}

	return FALSE;
}

// ------------------------------------------------------------------------------------------------
/** The ceiling "-smoke" needs.  A cloud that lives N times as long has N times as much of itself
 * in the air at once, and the eviction that enforces the shipped ceiling takes the oldest particle
 * first - which is the smoke, every time. */
// ------------------------------------------------------------------------------------------------
Int particleSmokeParticleCap( Int shippedCap, Real thickness )
{
	// A number this side of the vertex buffers the particle renderer fills each frame.  Reaching
	// it at all means something on the field is emitting far more than the smoke this switch
	// touches, and the shipped eviction is the right answer again at that point.
	const Int CAP_CEILING = 20000;

	// zero is the player asking for no particles at all, and no switch overrides that
	if (shippedCap <= 0 || thickness <= 1.0f)
		return shippedCap;

	Int raised = REAL_TO_INT( shippedCap * thickness );
	return (raised > CAP_CEILING) ? CAP_CEILING : raised;
}

// ------------------------------------------------------------------------------------------------
/** Collide a particle with the ground.  The velocity is split into the part heading into the
 * surface and the part sliding along it: the first is reflected and scaled by 'bounce', the
 * second scaled by 'friction'.  Working off the terrain normal rather than a flat Z flip means
 * debris rolls down a slope instead of hopping straight up off it.
 *
 * A particle already moving away from the surface is only lifted clear, never flipped back in -
 * that is what keeps a resting particle resting instead of jittering. */
// ------------------------------------------------------------------------------------------------
Bool particleGroundBounce( Coord3D *pos, Coord3D *vel, Real groundZ,
													 const Coord3D *normal, Real bounce, Real friction )
{
	if( pos->z >= groundZ )
		return FALSE;

	pos->z = groundZ;

	const Real intoSurface = vel->x * normal->x + vel->y * normal->y + vel->z * normal->z;
	if( intoSurface >= 0.0f )
		return TRUE;

	Coord3D alongNormal;
	alongNormal.set( normal->x * intoSurface, normal->y * intoSurface, normal->z * intoSurface );

	vel->x = (vel->x - alongNormal.x) * friction - alongNormal.x * bounce;
	vel->y = (vel->y - alongNormal.y) * friction - alongNormal.y * bounce;
	vel->z = (vel->z - alongNormal.z) * friction - alongNormal.z * bounce;

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** Update the behavior of an individual particle */
// ------------------------------------------------------------------------------------------------
Bool Particle::update( const ParticleUpdateContext &context )
{
	// gravity goes into the acceleration, which is zero here, so the sum is exactly the gravity
	if (context.gravity != 0.0f)
		m_accel.z += context.gravity;

	// integrate acceleration into velocity
	m_vel.x += m_accel.x;
	m_vel.y += m_accel.y;
	m_vel.z += m_accel.z;

	m_vel.x *= m_velDamping;
	m_vel.y *= m_velDamping;
	m_vel.z *= m_velDamping;

	// integrate velocity into position
	const Coord3D *driftVel = &context.driftVelocity;
	m_pos.x += m_vel.x + driftVel->x;
	m_pos.y += m_vel.y + driftVel->y;
	m_pos.z += m_vel.z + driftVel->z;

	// integrate the wind (if specified) into position
	ParticleSystemInfo::WindMotion windMotion = context.windMotion;

	// see if we should even do anything
	if( windMotion != ParticleSystemInfo::WIND_MOTION_NOT_USED )
		doWindMotion();

	// hit the ground, if this system asked to.  Only while descending, so the terrain is not
	// sampled for every rising smoke puff in the scene.
	// ponytail: terrain heightmap only - buildings, bridges and the water surface are not consulted
	if( context.groundCollision
			&& (m_vel.z + driftVel->z) < 0.0f )
	{
		Coord3D groundNormal;
		const Real groundZ = TheTerrainLogic->getGroundHeight( m_pos.x, m_pos.y, &groundNormal );
		particleGroundBounce( &m_pos, &m_vel, groundZ, &groundNormal,
												context.groundBounce, context.groundFriction );
	}

	// update orientation
	m_angleZ += m_angularRateZ;
	m_angularRateZ *= m_angularDamping;

	if (m_particleUpTowardsEmitter) {
		// adjust the up position back towards the particle
		static const Coord2D upVec = { 0.0f, 1.0f };
		Coord2D emitterDir;
		emitterDir.x = m_pos.x - m_emitterPos.x;
		emitterDir.y = m_pos.y - m_emitterPos.y;
		m_angleZ = (angleBetween(&upVec, &emitterDir) + PI);


	}

	// update size
	m_size += m_sizeRate;
	m_sizeRate *= m_sizeRateDamping;

	//
	// Update alpha (if used)
	//

	if (context.shaderType != ParticleSystemInfo::ADDITIVE)
	{
		m_alpha += m_alphaRate;

		if (m_alphaTargetKey < MAX_KEYFRAMES && m_alphaKey[ m_alphaTargetKey ].frame)
		{
			if (context.clientFrame - m_createTimestamp >= m_alphaKey[ m_alphaTargetKey ].frame)
			{
				m_alpha = m_alphaKey[ m_alphaTargetKey ].value;
				m_alphaTargetKey++;
				computeAlphaRate();
			}
		}
		else
			m_alphaRate = 0.0f;

		if (m_alpha < 0.0f)
			m_alpha = 0.0f;
		else if (m_alpha > 1.0f)
			m_alpha = 1.0f;
	}


	//
	// Update color
	//
	m_color.red += m_colorRate.red;
	m_color.green += m_colorRate.green;
	m_color.blue += m_colorRate.blue;

	if (m_colorTargetKey < MAX_KEYFRAMES && m_colorKey[ m_colorTargetKey ].frame)
	{
		if (context.clientFrame - m_createTimestamp >= m_colorKey[ m_colorTargetKey ].frame)
		{
			// can't set, because of colorscale
			// m_color = m_colorKey[ m_colorTargetKey ].color;
			m_colorTargetKey++;
			computeColorRate();
		}
	}
	else
	{
		m_colorRate.red = 0.0f;
		m_colorRate.green = 0.0f;
		m_colorRate.blue = 0.0f;
	}

	/// @todo Rethink this - at least its name
	m_color.red += m_colorScale;
	m_color.green += m_colorScale;
	m_color.blue += m_colorScale;

	if (m_color.red < 0.0f)
		m_color.red = 0.0f;
	else if (m_color.red > 1.0f)
		m_color.red = 1.0f;

	if (m_color.green < 0.0f)
		m_color.green = 0.0f;
	else if (m_color.green > 1.0f)
		m_color.green = 1.0f;

	if (m_color.blue < 0.0f)
		m_color.blue = 0.0f;
	else if (m_color.blue > 1.0f)
		m_color.blue = 1.0f;


	// reset the acceleration for accumulation next frame
	m_accel.z=m_accel.y=m_accel.x= 0.0f;

	// monitor lifetime, and fade out over the last frames of it
	if (m_lifetimeLeft)
	{
		if (--m_lifetimeLeft == 0)
			return false;

		UnsignedInt fadeFrames = m_lifetime / PARTICLE_FADE_LIFETIME_DIVISOR;
		if (fadeFrames > PARTICLE_FADE_MAX_FRAMES)
			fadeFrames = PARTICLE_FADE_MAX_FRAMES;
		else if (fadeFrames == 0)
			fadeFrames = 1;

		if (m_lifetimeLeft <= fadeFrames && !TheGlobalData->m_classicGraphics)
		{
			// zero on the last frame, so isInvisible below retires it on the frame it would die anyway
			const Real envelope = (Real)(m_lifetimeLeft - 1) / (Real)fadeFrames;
			switch (context.shaderType)
			{
				case ParticleSystemInfo::ALPHA:
				case ParticleSystemInfo::ALPHA_TEST:
					if (m_alpha > envelope)
						m_alpha = envelope;
					break;

				case ParticleSystemInfo::ADDITIVE:
				{
					// scale the brightest channel down to the envelope, keeping the hue
					const Real brightest = max( m_color.red, max( m_color.green, m_color.blue ) );
					if (brightest > envelope)
					{
						const Real k = envelope / brightest;
						m_color.red *= k;
						m_color.green *= k;
						m_color.blue *= k;
					}
					break;
				}

				case ParticleSystemInfo::MULTIPLY:
				{
					// the same toward white, which multiplies to nothing
					const Real darkest = 1.0f - min( m_color.red, min( m_color.green, m_color.blue ) );
					if (darkest > envelope)
					{
						const Real k = envelope / darkest;
						m_color.red = 1.0f - (1.0f - m_color.red) * k;
						m_color.green = 1.0f - (1.0f - m_color.green) * k;
						m_color.blue = 1.0f - (1.0f - m_color.blue) * k;
					}
					break;
				}
			}
		}
	}

	DEBUG_ASSERTCRASH( m_lifetimeLeft, ( "A particle has an infinite lifetime..." ));

	// if we've gone totally invisible, destroy ourselves
	if (isInvisible( context ))
		return false;
	return true;
}

// ------------------------------------------------------------------------------------------------
/** Do wind motion as specified by the particle system template, if present */
// ------------------------------------------------------------------------------------------------
void Particle::doWindMotion( void )
{

	// get the angle of the wind
	Real windAngle = m_system->getWindAngle();

	// get the system position
	Coord3D systemPos;
	m_system->getPosition( &systemPos );

	// when we're attached objects and drawables we offset by that position as well
	if( ObjectID attachedObj = m_system->getAttachedObject() )
	{
		Object *obj = TheGameLogic->findObjectByID( attachedObj );

		if( obj )
		{
			const Coord3D *objPos = obj->getPosition();

			systemPos.x += objPos->x;
			systemPos.y += objPos->y;
			systemPos.z += objPos->z;

		}  // end if

	}  // end if
	else if( DrawableID attachedDraw = m_system->getAttachedDrawable() )
	{
		Drawable *draw = TheGameClient->findDrawableByID( attachedDraw );

		if( draw )
		{
			const Coord3D *drawPos = draw->getPosition();

			systemPos.x += drawPos->x;
			systemPos.y += drawPos->y;
			systemPos.z += drawPos->z;

		}  // end if

	}  // end else if

	//
	// compute a vector from the system position in the world to the particle ... we will use
	// this to compute how much force we apply
	//
	Coord3D v;
	v.x = m_pos.x - systemPos.x;
	v.y = m_pos.y - systemPos.y;
	v.z = m_pos.z - systemPos.z;

	// distance amounts for full force from wind and no force at all
	Real fullForceDistance = 75.0f;
	Real noForceDistance = 200.0f;

	//
	// given the distance from the wind position to the particle ... figure out how much
	// force we're going to apply to it.  When it's further away (outside of the full force
	// distance) we will apply only a fraction of the force
	//

	Real distFromWind = v.length();
	if( distFromWind < noForceDistance )
	{
		Real windForceStrength = ConvertRetailPerFrameRate( 2.0f ) * m_windRandomness;

		// only apply force if still within the circle of influence
		if( distFromWind > fullForceDistance )
			windForceStrength *= (1.0f - ((distFromWind - fullForceDistance) / 
																		(noForceDistance - fullForceDistance)));

		// integate the wind motion into the position
		m_pos.x += (Cos( windAngle ) * windForceStrength);
		m_pos.y += (Sin( windAngle ) * windForceStrength);

	}  // end if

}  // doWindMotion

// ------------------------------------------------------------------------------------------------
/** Get priority of a particle ... which is the priority of it's attached system */
// ------------------------------------------------------------------------------------------------
ParticlePriorityType Particle::getPriority( void )
{ 
	return m_system->getPriority();
}

// ------------------------------------------------------------------------------------------------
/** Return true if this particle is invisible */
// ------------------------------------------------------------------------------------------------
Bool Particle::isInvisible( const ParticleUpdateContext &context )
{
	switch (context.shaderType)
	{
		case ParticleSystemInfo::ADDITIVE:
			// if color is black, this particle is invisible
			
			// check that we're not in the process of going to another color; past the last key
			// frame the index is one beyond the array
			if (m_colorTargetKey >= MAX_KEYFRAMES || m_colorKey[ m_colorTargetKey ].frame == 0)
			{
				if ((m_color.red + m_color.green + m_color.blue) <= 0.06f)
					return true;
			}
			return false;

		case ParticleSystemInfo::ALPHA:
			// if alpha is zero, this particle is invisible - but not while it is on its way to another
			// key, or a particle that fades in from zero slower than 0.02 a frame dies on its first update
			if (m_alphaTargetKey >= MAX_KEYFRAMES || m_alphaKey[ m_alphaTargetKey ].frame == 0)
			{
				if (m_alpha < 0.02f)
					return true;
			}
			return false;

		case ParticleSystemInfo::ALPHA_TEST:
			// hmm... assume these particles are never invisible
			return false;

		case ParticleSystemInfo::MULTIPLY:
			// if color is white, this particle is invisible

			// check that we're not in the process of going to another color
			if (m_colorTargetKey >= MAX_KEYFRAMES || m_colorKey[ m_colorTargetKey ].frame == 0)
			{
				if ((m_color.red * m_color.green * m_color.blue) > 0.95f)
					return true;
			}
			return false;
	}

	// should never get here - if we do, data is incorrect
	return true;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Particle::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void Particle::xfer( Xfer *xfer )
{

	// version 
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// base class particle info
	ParticleInfo::xfer( xfer );

	// personality
	xfer->xferUnsignedInt( &m_personality );

	// acceleration
	xfer->xferCoord3D( &m_accel );

	// last position
	xfer->xferCoord3D( &m_lastPos );

	// lifetime left
	xfer->xferUnsignedInt( &m_lifetimeLeft );

	// creation timestamp
	xfer->xferUnsignedInt( &m_createTimestamp );

	// alpha
	xfer->xferReal( &m_alpha );

	// alpha rate
	xfer->xferReal( &m_alphaRate );

	// alpha target key
	xfer->xferInt( &m_alphaTargetKey );

	// color
	xfer->xferRGBColor( &m_color );

	// color rate
	xfer->xferRGBColor( &m_colorRate );

	// color target key
	xfer->xferInt( &m_colorTargetKey );

	// drawable
	DrawableID drawableID = INVALID_DRAWABLE_ID;
	xfer->xferDrawableID( &drawableID );	//saving for backwards compatibility when we supported drawables.

	// system under control as an id
	ParticleSystemID systemUnderControlID = m_systemUnderControl ? m_systemUnderControl->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;
	xfer->xferUser( &systemUnderControlID, sizeof( ParticleSystemID ) );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Particle::loadPostProcess( void )
{

	// call base class post process
	ParticleInfo::loadPostProcess();

	// tidy up the m_systemUnderControl pointer
	if( m_systemUnderControlID != INVALID_PARTICLE_SYSTEM_ID )
	{
		ParticleSystem *system;
		
		// find system
		system = TheParticleSystemManager->findParticleSystem( m_systemUnderControlID );

		// set us as the control particle for this system
		system->setControlParticle( this );
		controlParticleSystem( system );

		// sanity
		if( m_systemUnderControlID == NULL )
		{

			DEBUG_CRASH(( "Particle::loadPostProcess - Unable to find system under control pointer\n" ));
			throw SC_INVALID_DATA;

		}  // end if

	}  // end if

}  // end loadPostProcess

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemInfo::ParticleSystemInfo()
{
	m_priority = PARTICLE_PRIORITY_LOWEST;
	m_isGroundAligned = false;
	m_isEmitAboveGroundOnly = false;
	m_isParticleUpTowardsEmitter = false;
	
	//Added By Sadullah Nader
	//Initializations inserted
	m_driftVelocity.zero();
	m_gravity = 0.0f;
	m_groundCollision = FALSE;	// retail behaviour: particles pass through the terrain
	m_groundBounce = 0.35f;			// only consulted once an INI sets GroundCollision = Yes
	m_groundFriction = ConvertRetailPerFrameFactor( 0.6f );	// as GroundFriction is parsed
	m_isEmissionVolumeHollow = FALSE;
	m_isOneShot = FALSE;
	m_slavePosOffset.zero();
	m_systemLifetime = 0;
	
	//
	// some default values for the wind motion values
	m_windMotion = WIND_MOTION_NOT_USED;
	m_windAngle = 0.0f;
	m_windAngleChange = ConvertRetailPerFrameRate( 0.15f );  // higher is ping pong faster
	m_windAngleChangeMin = ConvertRetailPerFrameRate( 0.15f );
	m_windAngleChangeMax = ConvertRetailPerFrameRate( 0.45f );
	m_windMotionStartAngleMin = 0.0f;
	m_windMotionStartAngleMax = PI / 4.0f;
	m_windMotionStartAngle = m_windMotionStartAngleMin;
	m_windMotionEndAngleMin = TWO_PI - (PI / 4.0f);
	m_windMotionEndAngleMax = TWO_PI;
	m_windMotionEndAngle = m_windMotionEndAngleMin;
	m_windMotionMovingToEndAngle = TRUE;
	m_volumeParticleDepth = DEFAULT_VOLUME_PARTICLE_DEPTH;

}


void ParticleSystemInfo::tintAllColors( Color tintColor )
{
	RGBColor rgb;
	rgb.setFromInt(tintColor);

	//This tints all but the first colorKey!!!
	// setFromInt has already brought each channel to 0..1; dividing by 255 again here turned every
	// tint into black, which nobody saw because only the beacon's never-taken fallback called it
	for (int key = 1; key < MAX_KEYFRAMES; ++key )
	{
		m_colorKey[ key ].color.red   *= rgb.red;
		m_colorKey[ key ].color.green *= rgb.green;
		m_colorKey[ key ].color.blue  *= rgb.blue;
	}

}  // end loadPostProcess


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::xfer( Xfer *xfer )
{
	Int i;

	// version 
	// 2: ground collision (m_groundCollision / m_groundBounce / m_groundFriction)
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// is one shot
	xfer->xferBool( &m_isOneShot );

	// shader type
	xfer->xferUser( &m_shaderType, sizeof( ParticleShaderType ) );

	// particle type
	xfer->xferUser( &m_particleType, sizeof( ParticleType ) );

	// particle type name
	xfer->xferAsciiString( &m_particleTypeName );

	// angles
	GameClientRandomVariable	tempRandom;	//for backwards compatibility when we supported x,y
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &m_angleZ, sizeof( GameClientRandomVariable ) );

	// angular rate
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &tempRandom, sizeof( GameClientRandomVariable ) );
	xfer->xferUser( &m_angularRateZ, sizeof( GameClientRandomVariable ) );

	// angular damping
	xfer->xferUser( &m_angularDamping, sizeof( GameClientRandomVariable ) );

	// velocity damping
	xfer->xferUser( &m_velDamping, sizeof( GameClientRandomVariable ) );

	// lifetime
	xfer->xferUser( &m_lifetime, sizeof( GameClientRandomVariable ) );

	// system lifetime
	xfer->xferUnsignedInt( &m_systemLifetime );

	// start size
	xfer->xferUser( &m_startSize, sizeof( GameClientRandomVariable ) );

	// start size rate
	xfer->xferUser( &m_startSizeRate, sizeof( GameClientRandomVariable ) );

	// size rate
	xfer->xferUser( &m_sizeRate, sizeof( GameClientRandomVariable ) );

	// size rate damping
	xfer->xferUser( &m_sizeRateDamping, sizeof( GameClientRandomVariable ) );

	// alpha keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferUser( &m_alphaKey[ i ].var, sizeof( GameClientRandomVariable ) );
		xfer->xferUnsignedInt( &m_alphaKey[ i ].frame );

	}  // end for, i

	// color keys
	for( i = 0; i < MAX_KEYFRAMES; ++i )
	{

		xfer->xferRGBColor( &m_colorKey[ i ].color );
		xfer->xferUnsignedInt( &m_colorKey[ i ].frame );

	}  // end for, i

	// color scale
	xfer->xferUser( &m_colorScale, sizeof( GameClientRandomVariable ) );
	
	// burst delay
	xfer->xferUser( &m_burstDelay, sizeof( GameClientRandomVariable ) );
	
	// burst count
	xfer->xferUser( &m_burstCount, sizeof( GameClientRandomVariable ) );
	
	// initial delay
	xfer->xferUser( &m_initialDelay, sizeof( GameClientRandomVariable ) );
	
	// drift velocity
	xfer->xferCoord3D( &m_driftVelocity );
	
	// gravity
	xfer->xferReal( &m_gravity );
		
	// slave system name
	xfer->xferAsciiString( &m_slaveSystemName );

	// slave position offset
	xfer->xferCoord3D( &m_slavePosOffset );

	// attached system name
	xfer->xferAsciiString( &m_attachedSystemName );

	// emission velocity type, this must come before m_emissionVelocity
	xfer->xferUser( &m_emissionVelocityType, sizeof( EmissionVelocityType ) );

	// particle priority
	xfer->xferUser( &m_priority, sizeof( ParticlePriorityType ) );

	// emission velocity
	switch( m_emissionVelocityType )
	{

		// --------------------------------------------------------------------------------------------
		case ORTHO:
			xfer->xferUser( &m_emissionVelocity.ortho.x, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.ortho.y, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.ortho.z, sizeof( GameClientRandomVariable ) );
			break;

		// --------------------------------------------------------------------------------------------
		case SPHERICAL:		
			xfer->xferUser( &m_emissionVelocity.spherical.speed, sizeof( GameClientRandomVariable ) );			
			break;

		// --------------------------------------------------------------------------------------------
		case HEMISPHERICAL:
			xfer->xferUser( &m_emissionVelocity.hemispherical.speed, sizeof( GameClientRandomVariable ) );			
			break;

		// --------------------------------------------------------------------------------------------
		case CYLINDRICAL:
			xfer->xferUser( &m_emissionVelocity.cylindrical.radial, sizeof( GameClientRandomVariable ) );						
			xfer->xferUser( &m_emissionVelocity.cylindrical.normal, sizeof( GameClientRandomVariable ) );
			break;

		// --------------------------------------------------------------------------------------------
		case OUTWARD:
			xfer->xferUser( &m_emissionVelocity.outward.speed, sizeof( GameClientRandomVariable ) );
			xfer->xferUser( &m_emissionVelocity.outward.otherSpeed, sizeof( GameClientRandomVariable ) );
			break;

	}  // end switch, m_emissionVelocityType

	// emission volume type
	xfer->xferUser( &m_emissionVolumeType, sizeof( EmissionVolumeType ) );

	// emission volume
	switch( m_emissionVolumeType )
	{

		// --------------------------------------------------------------------------------------------
		case POINT:
			// point has no data, it uses the systems position			
			break;

		// --------------------------------------------------------------------------------------------
		case LINE:
			xfer->xferCoord3D( &m_emissionVolume.line.start );
			xfer->xferCoord3D( &m_emissionVolume.line.end );
			break;

		// --------------------------------------------------------------------------------------------
		case BOX:
			xfer->xferCoord3D( &m_emissionVolume.box.halfSize );
			break;

		// --------------------------------------------------------------------------------------------
		case SPHERE:
			xfer->xferReal( &m_emissionVolume.sphere.radius );
			break;

		// --------------------------------------------------------------------------------------------
		case CYLINDER:
			xfer->xferReal( &m_emissionVolume.cylinder.radius );
			xfer->xferReal( &m_emissionVolume.cylinder.length );
			break;

	}  // end switch, m_emissionVolumeType

	// is emission volume hollow
	xfer->xferBool( &m_isEmissionVolumeHollow );

	// is ground aligned
	xfer->xferBool( &m_isGroundAligned );

	// emit above ground only
	xfer->xferBool( &m_isEmitAboveGroundOnly );

	// is particle up towards emitter
	xfer->xferBool( &m_isParticleUpTowardsEmitter );

	// wind motion
	xfer->xferUser( &m_windMotion, sizeof( WindMotion ) );

	// wind angle
	xfer->xferReal( &m_windAngle );

	// wind angle change
	xfer->xferReal( &m_windAngleChange );

	// wind angle change min
	xfer->xferReal( &m_windAngleChangeMin );

	// wind angle change max
	xfer->xferReal( &m_windAngleChangeMax );

	// wind motion start angle
	xfer->xferReal( &m_windMotionStartAngle );

	// wind motion start angle min
	xfer->xferReal( &m_windMotionStartAngleMin );

	// wind motion start angle max
	xfer->xferReal( &m_windMotionStartAngleMax );

	// wind motion end angle
	xfer->xferReal( &m_windMotionEndAngle );

	// wind motion end angle min
	xfer->xferReal( &m_windMotionEndAngleMin );

	// wind motion end angle max
	xfer->xferReal( &m_windMotionEndAngleMax );

	// wind motion moving to end angle
	xfer->xferByte( &m_windMotionMovingToEndAngle );

	// ground collision - added in version 2, absent from saves written before it
	if( version >= 2 )
	{
		xfer->xferBool( &m_groundCollision );
		xfer->xferReal( &m_groundBounce );
		xfer->xferReal( &m_groundFriction );
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystemInfo::loadPostProcess( void )
{

}  // end loadPostProcess

///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystem /////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

UnsignedInt ParticleSystem::s_sunMapStamp = 1;

// ------------------------------------------------------------------------------------------------
/** Read particle system properties from given file */
// ------------------------------------------------------------------------------------------------
ParticleSystem::ParticleSystem( const ParticleSystemTemplate *sysTemplate,
																ParticleSystemID id, 
																Bool createSlaves )
{
	m_systemParticlesHead = m_systemParticlesTail = NULL;

	m_isFirstPos = true;
	m_template = sysTemplate;
	m_systemID = id;

	//Added By Sadullah Nader
	//Initializations inserted
	m_lastPos.zero();
	m_pos.zero();
	m_velCoeff.zero();
	//

	m_attachedToDrawableID = INVALID_DRAWABLE_ID;
	m_attachedToObjectID = INVALID_ID;
	m_attachedObjectDrawableForDraw = INVALID_DRAWABLE_ID;

	m_isLocalIdentity = true;
	m_localTransform.Make_Identity();

	m_isIdentity = true;
	m_transform.Make_Identity();
  m_skipParentXfrm = false;

	m_isStopped = false;
	m_isDestroyed = false;
	m_isSaveable = true;

	m_slavePosOffset = sysTemplate->m_slavePosOffset;


	// EA's own todo here was "further formalize this parameter with an UnsignedInt field in the
	// editor": the member existed, was reset to zero on every system built from a template, and
	// the getter ignored it and returned a hard-coded 6 for anything typed VOLUME_PARTICLE.
	// It is a template field now, and zero still means "whatever the engine thinks best".
	m_volumeParticleDepth = sysTemplate->m_volumeParticleDepth;


	m_driftVelocity = sysTemplate->m_driftVelocity;

	m_velCoeff.x = 1.0f;
	m_velCoeff.y = 1.0f;
	m_velCoeff.z = 1.0f;
	m_countCoeff = 1.0f;
	m_delayCoeff = 1.0f;
	m_sizeCoeff = 1.0f;

	m_gravity = sysTemplate->m_gravity;

	m_lifetime = sysTemplate->m_lifetime;
	m_startSize = sysTemplate->m_startSize;
	m_startSizeRate = sysTemplate->m_startSizeRate;
	m_sizeRate = sysTemplate->m_sizeRate;
	m_sizeRateDamping = sysTemplate->m_sizeRateDamping;

	// i is used after the loop; VC6 for-scope let it escape.
	int i;
	for( i =0; i<MAX_KEYFRAMES; i++ )
		m_alphaKey[i] = sysTemplate->m_alphaKey[i];

	for( i=0; i<MAX_KEYFRAMES; i++ )
		m_colorKey[i] = sysTemplate->m_colorKey[i];

	/// @todo It is confusing to do this conversion here...
	Real low = sysTemplate->m_colorScale.getMinimumValue();
	Real hi = sysTemplate->m_colorScale.getMaximumValue();
	m_colorScale.setRange( low / 255.0f, hi / 255.0f );

	m_burstDelay = sysTemplate->m_burstDelay;
	m_burstDelayLeft = 0;

	m_burstCount = sysTemplate->m_burstCount;

	m_isOneShot = sysTemplate->m_isOneShot;

	m_delayLeft = 0;					// buildClientPart
	m_initialDelaySet = FALSE;
	m_isShell = FALSE;

	m_startTimestamp = 0;			// buildClientPart
	m_systemLifetimeLeft = sysTemplate->m_systemLifetime;
	if (sysTemplate->m_systemLifetime)
		m_isForever = false;
	else
		m_isForever = true;

	m_accumulatedSizeBonus = 0;

	m_velDamping = sysTemplate->m_velDamping;

	m_angleZ = sysTemplate->m_angleZ;
	m_angularRateZ = sysTemplate->m_angularRateZ;
	m_angularDamping = sysTemplate->m_angularDamping;

	m_priority = sysTemplate->m_priority;

	m_emissionVelocityType = sysTemplate->m_emissionVelocityType;
	m_emissionVelocity = sysTemplate->m_emissionVelocity;

	m_emissionVolumeType = sysTemplate->m_emissionVolumeType;
	m_emissionVolume = sysTemplate->m_emissionVolume;

	m_isEmissionVolumeHollow = sysTemplate->m_isEmissionVolumeHollow;
	m_isGroundAligned = sysTemplate->m_isGroundAligned;
	m_isEmitAboveGroundOnly = sysTemplate->m_isEmitAboveGroundOnly;
	m_isParticleUpTowardsEmitter = sysTemplate->m_isParticleUpTowardsEmitter;

	m_windMotion = sysTemplate->m_windMotion;
	m_windAngleChange = sysTemplate->m_windAngleChange;
	m_windAngleChangeMin = sysTemplate->m_windAngleChangeMin;
	m_windAngleChangeMax = sysTemplate->m_windAngleChangeMax;
	m_windMotionStartAngleMin = sysTemplate->m_windMotionStartAngleMin;
	m_windMotionStartAngleMax = sysTemplate->m_windMotionStartAngleMax;
	m_windMotionEndAngleMin = sysTemplate->m_windMotionEndAngleMin;
	m_windMotionEndAngleMax = sysTemplate->m_windMotionEndAngleMax;
	m_windMotionMovingToEndAngle = sysTemplate->m_windMotionMovingToEndAngle;
	m_windMotionStartAngle = 0.0f;		// buildClientPart
	m_windMotionEndAngle = 0.0f;
	m_windAngle = 0.0f;

	m_shaderType = sysTemplate->m_shaderType;

	m_particleType = sysTemplate->m_particleType;
	m_particleTypeName = sysTemplate->m_particleTypeName;

	m_isStopped = false;

	// set up slave particle system, if any
	m_masterSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_slaveSystemID = INVALID_PARTICLE_SYSTEM_ID;
	m_masterSystem = NULL;
	m_slaveSystem = NULL;

	m_attachedSystemName = sysTemplate->m_attachedSystemName;
	m_particleCount = 0;
	m_personalityStore = 0;
	m_controlParticle = NULL;
	m_groundShadow = NULL;
	m_inSunMapStamp = 0;

	m_gpuEligible = TheParticleSystemManager->acceptsGpuParticles() && sysTemplate->isGpuEligible();
	m_gpuSpawnTotal = 0;
	m_gpuLastDeathFrame = 0;

	// A system logic makes inside a tick is built after it (LOGIC-THREAD.md M5).  The load path's
	// systems have no id yet and are never made in a tick.
	if (m_systemID != INVALID_PARTICLE_SYSTEM_ID && LogicThread::defersEffects())
	{
		m_isShell = TRUE;
		TheParticleSystemManager->friend_addShell(this, createSlaves);
	}
	else
		buildClientPart(createSlaves);

	//DEBUG_ASSERTLOG(!(m_totalParticleSystemCount % 10 == 0), ( "TotalParticleSystemCount = %d\n", m_totalParticleSystemCount ));
}

// ------------------------------------------------------------------------------------------------
/** The constructor's client half, in the order the constructor always ran it: the client stream's
	* draws, the slave, the manager's list, the DebrisTrails option. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::buildClientPart( Bool createSlaves )
{
	LOGIC_CENSUS( CENSUS_PARTICLES );
	m_isShell = FALSE;
	const ParticleSystemTemplate *sysTemplate = m_template;

	// drawn whether or not the tick set its own delay, as the constructor drew it before the call
	const UnsignedInt delay = particleTimerFrames( sysTemplate->m_initialDelay.getValue() );
	if (!m_initialDelaySet)
		m_delayLeft = delay;

	m_startTimestamp = TheGameClient->getFrame();

	m_windMotionStartAngle = GameClientRandomValueReal( m_windMotionStartAngleMin, m_windMotionStartAngleMax );
	m_windMotionEndAngle = GameClientRandomValueReal( m_windMotionEndAngleMin, m_windMotionEndAngleMax );
	m_windAngle = GameClientRandomValueReal( m_windMotionStartAngle, m_windMotionEndAngle );

	if( createSlaves )
	{
		ParticleSystem *slaveSystem = sysTemplate->createSlaveSystem();

		if( slaveSystem )
		{

			setSlave( slaveSystem );
			m_slaveSystem->setMaster( this );

			// what the tick did to the shell that a built system passes to its slave
			if( !m_isSaveable )
				m_slaveSystem->setSaveable( FALSE );
			if( m_isDestroyed )
				m_slaveSystem->destroy();

		}  // end if

	}  // end if

	// A system built without an id yet - the load path does that, so it can restore the saved id
	// first - registers itself once it has one, rather than here where the manager would file it
	// under nothing and every later lookup by id would miss it.
	if (m_systemID != INVALID_PARTICLE_SYSTEM_ID)
		TheParticleSystemManager->friend_addParticleSystem(this);

	// DebrisTrails off: the fork's blast fragments never emit, so nothing of them, their trails or
	// their slaves is drawn.  Destroyed rather than never built, so the FXList, the object creation
	// list and every caller holding the pointer go on exactly as they do with the option on.
	if (sysTemplate->m_explosionDebrisTrail && !TheGlobalData->m_explosionDebrisTrails)
		destroy();
}

// ------------------------------------------------------------------------------------------------
/** Destroy particle system and all of its particles */
// ------------------------------------------------------------------------------------------------
ParticleSystem::~ParticleSystem()
{

	// tell any of our slave systems that we are going away
	if( m_slaveSystem )
	{

		DEBUG_ASSERTCRASH( m_slaveSystem->getMaster() == this, ("~ParticleSystem: Our slave doesn't have us as a master!\n") );
		m_slaveSystem->setMaster( NULL );
		setSlave( NULL );

	}  // end if

	// tell any master system that *we* are going away
	if( m_masterSystem )
	{

		DEBUG_ASSERTCRASH( m_masterSystem->getSlave() == this, ("~ParticleSystem: Our master doesn't have us as a slave!\n") );
		m_masterSystem->setSlave( NULL );
		setMaster( NULL );

	}  // end if
	

	// give the ground blob back before the particles it was measured from go away
	releaseGroundShadow();

	// and the device's slot, with every GPU particle still in the air
	if (m_gpuEligible)
		TheParticleSystemManager->releaseGpuSystem( this );

	// destroy all particles "in the air"
	while (m_systemParticlesHead)
		m_systemParticlesHead->deleteInstance();

	m_attachedToDrawableID = INVALID_DRAWABLE_ID;
	m_attachedToObjectID = INVALID_ID;

	// if this system was controlled by a particle, detach
	if (m_controlParticle)
		m_controlParticle->detachControlledParticleSystem();

	m_controlParticle = NULL;

	// a shell a cleared game left was never filed
	if (m_systemID != INVALID_PARTICLE_SYSTEM_ID && !m_isShell)
		TheParticleSystemManager->friend_removeParticleSystem(this);
	//DEBUG_ASSERTLOG(!(m_totalParticleSystemCount % 10 == 0), ( "TotalParticleSystemCount = %d\n", m_totalParticleSystemCount ));
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setMaster( ParticleSystem *master )
{

	m_masterSystem = master;
	m_masterSystemID = master ? master->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;

}  // end set Master

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setSlave( ParticleSystem *slave )
{

	m_slaveSystem = slave;
	m_slaveSystemID = slave ? slave->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;

}  // end setSlave

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setSaveable(Bool b)
{
	m_isSaveable = b;
	if (m_slaveSystem)
		m_slaveSystem->setSaveable(b);
}

// ------------------------------------------------------------------------------------------------
/** (Re)start a stopped particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::start( void )
{
	m_isStopped = false;
}

// ------------------------------------------------------------------------------------------------
/** Stop a particle system from emitting */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::stop( void )
{
	m_isStopped = true;
}

// ------------------------------------------------------------------------------------------------
/** Stop emitting, wait for all of our particles to die, then destroy self. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::destroy( void )
{
	m_isDestroyed = true;
	if( m_slaveSystem )
	{
		m_slaveSystem->destroy();  // If we don't it will leak forever.  We are solely responsible for it.
	}
}

// ------------------------------------------------------------------------------------------------
/** Get the position of the particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::getPosition( Coord3D *pos )
{
	Vector3 vec;
	m_localTransform.Get_Translation(&vec);
	if (pos)
	{	pos->x=vec.X;
		pos->y=vec.Y;
		pos->z=vec.Z;
	}
}

// ------------------------------------------------------------------------------------------------
/** Set the position of the particle system */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setPosition( const Coord3D *pos )
{
	m_localTransform.Set_X_Translation( pos->x );
	m_localTransform.Set_Y_Translation( pos->y );
	m_localTransform.Set_Z_Translation( pos->z );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Set the system's local transform */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setLocalTransform( const Matrix3D *matrix )
{
	m_localTransform = *matrix;
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformX( Real x )
{
	m_localTransform.Rotate_X( x );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformY( Real y )
{
	m_localTransform.Rotate_Y( y );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Rotate local transform matrix */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::rotateLocalTransformZ( Real z )
{
	m_localTransform.Rotate_Z( z );
	m_isLocalIdentity = false;
}

// ------------------------------------------------------------------------------------------------
/** Attach this particle system to a Drawable */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::attachToDrawable( const Drawable *draw )
{
	if (draw)
		m_attachedToDrawableID = draw->getID();
	else
		m_attachedToDrawableID = INVALID_DRAWABLE_ID;
}

// ------------------------------------------------------------------------------------------------
/** Attach this particle system to a Drawable */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::attachToObject( const Object *obj )
{
	if (obj)
		m_attachedToObjectID = obj->getID();
	else
		m_attachedToObjectID = INVALID_ID;
}

// ------------------------------------------------------------------------------------------------
/** Compute a random point on a unit sphere
 * @todo The density of random points generated is not uniform within the sphere */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computePointOnUnitSphere( void )
{
	static Coord3D point;

	do
	{
		point.x = GameClientRandomValueReal( -1.0f, 1.0f );
		point.y = GameClientRandomValueReal( -1.0f, 1.0f );
		point.z = GameClientRandomValueReal( -1.0f, 1.0f );
	}
	while (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f);

	point.normalize();

	return &point;
}

// ------------------------------------------------------------------------------------------------
/** Compute a velocity vector based on emission properties */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computeParticleVelocity( const Coord3D *pos )
{
	static Coord3D newVel;

	switch( m_emissionVelocityType )
	{
		case ORTHO:
			newVel.x = m_emissionVelocity.ortho.x.getValue();
			newVel.y = m_emissionVelocity.ortho.y.getValue();
			newVel.z = m_emissionVelocity.ortho.z.getValue();
			break;
		
		case CYLINDRICAL:
		{
			Real radialSpeed, angle;
			radialSpeed = m_emissionVelocity.cylindrical.radial.getValue();

			angle = GameClientRandomValueReal( 0, 2.0f*PI );
			newVel.x = radialSpeed * cos( angle );
			newVel.y = radialSpeed * sin( angle );

			newVel.z = m_emissionVelocity.cylindrical.normal.getValue();
			break;
		}

		// "outward" velocity is directed along the surface normal of the emission volume
		case OUTWARD:
		{
			Real speed = m_emissionVelocity.outward.speed.getValue();
			Real otherSpeed = m_emissionVelocity.outward.otherSpeed.getValue();
			Coord3D sysPos;

			/*
			sysPos.x = m_localTransform.Get_X_Translation();
			sysPos.y = m_localTransform.Get_Y_Translation();
			sysPos.z = m_localTransform.Get_Z_Translation();
			*/
			sysPos.x = 0.0f;
			sysPos.y = 0.0f;
			sysPos.z = 0.0f;

			switch( m_emissionVolumeType )
			{
				case CYLINDER:
					Coord2D disk;

					disk.x = pos->x - sysPos.x;
					disk.y = pos->y - sysPos.y;
					disk.normalize();

					newVel.x = speed * disk.x;
					newVel.y = speed * disk.y;
					newVel.z = otherSpeed;
					break;

				case BOX:				///< @todo Implement BOX OUTWARD velocity
				case SPHERE:
				{
					newVel.x = pos->x - sysPos.x;
					newVel.y = pos->y - sysPos.y;
					newVel.z = pos->z - sysPos.z;
					newVel.normalize();

					newVel.x *= speed;
					newVel.y *= speed;
					newVel.z *= speed;
					break;
				}

				case LINE:
				{
					Coord3D along;			// unit vector along line direction

					along.x = m_emissionVolume.line.end.x - m_emissionVolume.line.start.x;
					along.y = m_emissionVolume.line.end.y - m_emissionVolume.line.start.y;
					along.z = m_emissionVolume.line.end.z - m_emissionVolume.line.start.z;
					along.normalize();

					Coord3D perp;				// unit vector perpendicular to the along/up plane
					Coord3D up;					// unit vector in the up direction (Z)
					up.x = 0.0;
					up.y = 0.0;
					up.z = 1.0;
					perp.crossProduct( &up, &along, &perp );
					up.crossProduct( &along, &perp, &up );

					// "speed" is in 'horizontal' plane, and "otherSpeed" is 'vertical'
					newVel.x = speed * perp.x + otherSpeed * up.x;
					newVel.y = speed * perp.y + otherSpeed * up.y;
					newVel.z = speed * perp.z + otherSpeed * up.z;								
					break;
				}

				case POINT:
				{
					Coord3D vel = *computePointOnUnitSphere();

					newVel.x = speed * vel.x;
					newVel.y = speed * vel.y;
					newVel.z = speed * vel.z;
					break;
				}
			}

			break;
		}

		case SPHERICAL:
		{
			Real speed = m_emissionVelocity.spherical.speed.getValue();
			Coord3D vel = *computePointOnUnitSphere();

			newVel.x = speed * vel.x;
			newVel.y = speed * vel.y;
			newVel.z = speed * vel.z;
			break;
		}

		case HEMISPHERICAL:
		{
			Coord3D vel;
			Real speed = m_emissionVelocity.spherical.speed.getValue();

			do
			{
				vel.x = GameClientRandomValueReal( -1.0f, 1.0f );
				vel.y = GameClientRandomValueReal( -1.0f, 1.0f );
				vel.z = GameClientRandomValueReal( 0.0f, 1.0f );
			}
			while (vel.x == 0.0f && vel.y == 0.0f && vel.z == 0.0f);

			vel.normalize();

			newVel.x = speed * vel.x;
			newVel.y = speed * vel.y;
			newVel.z = speed * vel.z;
			break;
		}

		default:
			newVel.x = 0.0f;
			newVel.y = 0.0f;
			newVel.z = 0.0f;
			break;
	}

	// scale the velocity by the velocity multiplier
	newVel.x *= m_velCoeff.x*(0.5f+TheGlobalData->m_particleScale/2.0f);
	newVel.y *= m_velCoeff.y*(0.5f+TheGlobalData->m_particleScale/2.0f);
	newVel.z *= m_velCoeff.z*(0.5f+TheGlobalData->m_particleScale/2.0f);

	return &newVel;
}

// ------------------------------------------------------------------------------------------------
/** Compute a position based on emission properties */
// ------------------------------------------------------------------------------------------------
const Coord3D *ParticleSystem::computeParticlePosition( void )
{
	static Coord3D newPos;

	switch( m_emissionVolumeType )
	{
		case CYLINDER:
		{
			Real angle = GameClientRandomValueReal( 0, 2.0f*PI );
			Real radius;
			
			if (m_isEmissionVolumeHollow)
				radius = m_emissionVolume.cylinder.radius;
			else
				radius = GameClientRandomValueReal( 0.0f, m_emissionVolume.cylinder.radius );

			newPos.x = radius * cos( angle );
			newPos.y = radius * sin( angle );

			Real halfLength = m_emissionVolume.cylinder.length/2.0f;
			newPos.z = GameClientRandomValueReal( -halfLength, halfLength );

			break;
		}

		case SPHERE:
		{
			Real radius;

			if (m_isEmissionVolumeHollow)
				radius = m_emissionVolume.sphere.radius;
			else
				radius = GameClientRandomValueReal( 0.0f, m_emissionVolume.sphere.radius );

			newPos = *computePointOnUnitSphere();

			newPos.x *= radius;
			newPos.y *= radius;
			newPos.z *= radius;

			break;
		}

		case BOX:
		{			
			if (m_isEmissionVolumeHollow) {
				// determine which side to generate on.
				// 0 is bottom, 3 is top, 
				// 1 is left , 4 is right
				// 2 is front, 5 is right back

				int side = GameClientRandomValue(0, 6);
				if (side % 3 == 0) {
					// generate X, Y
					newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
					newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
					if (side == 0) {
						newPos.z = -m_emissionVolume.box.halfSize.z;
					} else {
						newPos.z = m_emissionVolume.box.halfSize.z;
					}
															
				} else if (side % 3 == 1) {
					// generate Y, Z
					newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
					newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
					if (side == 1) {
						newPos.x = -m_emissionVolume.box.halfSize.x;
					} else {
						newPos.x = m_emissionVolume.box.halfSize.y;
					}

				} else if (side % 3 == 2) {
					// generate X, Z
					newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
					newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
					if (side == 2) {
						newPos.y = -m_emissionVolume.box.halfSize.y;
					} else {
						newPos.y = m_emissionVolume.box.halfSize.y;
					}
				}
			} else {
				newPos.x = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.x, m_emissionVolume.box.halfSize.x );
				newPos.y = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.y, m_emissionVolume.box.halfSize.y );
				newPos.z = GameClientRandomValueReal( -m_emissionVolume.box.halfSize.z, m_emissionVolume.box.halfSize.z );
			break;
			}
		}

		case LINE:
		{
			Coord3D delta, start, end;

			start = m_emissionVolume.line.start;
			end = m_emissionVolume.line.end;

			delta.x = end.x - start.x;
			delta.y = end.y - start.y;
			delta.z = end.z - start.z;

			Real t = GameClientRandomValueReal( 0.0f, 1.0f );

			newPos.x = start.x + t * delta.x;
			newPos.y = start.y + t * delta.y;
			newPos.z = start.z + t * delta.z;
			break;
		}

		case POINT:
		default:
			newPos.x = 0.0f;
			newPos.y = 0.0f;
			newPos.z = 0.0f;
			break;
	}
	newPos.x *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	newPos.y *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	newPos.z *= (0.5f+TheGlobalData->m_particleScale/2.0f);
	return &newPos;
}

// ------------------------------------------------------------------------------------------------
/** Factory method for particles. */
// ------------------------------------------------------------------------------------------------
Particle *ParticleSystem::createParticle( const ParticleInfo *info, 
																					ParticlePriorityType priority,
																					Bool forceCreate )
{

	//
	// if we aren't absolutely forcing this particle to be created (which is needed when
	// loading and creating particle systems from the save games) we need to check a few
	// restrictions before this particle can really be created
	//
	if( forceCreate == FALSE )
	{

		if (TheGlobalData->m_useFX == FALSE)
			return NULL;

		//
		// Enforce particle limit.
		// If we are at the limit, destroy the oldest particle in order
		// to make room for this one.
		//

		//
		// Check if particle is below priorities we allow for this FPS or if it being skipped because 
		// all particesl are being skipped (excluding special fps independent particles at 
		// getMinDynamicParticleSkipPriority()) 
		//
		if( priority < TheGameLODManager->getMinDynamicParticlePriority() ||
				(priority < TheGameLODManager->getMinDynamicParticleSkipPriority() && 
				 TheGameLODManager->isParticleSkipped()) )
			return NULL;

		if ( getParticleCount() > 0 && priority == AREA_EFFECT && m_isGroundAligned && TheParticleSystemManager->getFieldParticleCount() > (UnsignedInt)TheGlobalData->m_maxFieldParticleCount )
			return NULL;

		// ALWAYS_RENDER particles are exempt from all count limits, and are always created, regardless of LOD issues.
		if (priority != ALWAYS_RENDER)
		{
			// "-particlecap" stands in for the options slider, which the LOD manager applies long
			// after the command line is parsed
			Int shippedCap = TheGlobalData->getEffectiveParticleCap();
			Int particleCap = particleSmokeParticleCap( shippedCap, TheGlobalData->m_smokeThickness );
			int numInExcess = TheParticleSystemManager->getParticleCount() - (UnsignedInt)particleCap;
			if ( numInExcess > 0)
			{
				// At the cap an eligible spawn goes to the GPU pool rather than evicting the oldest, so
				// below it the picture is the CPU's exactly.  No Particle comes back; the caller tells
				// this NULL from a refusal by getGpuSpawnTotal.  A player who set no particles at all
				// gets none there either.
				if (m_gpuEligible && TheGlobalData->m_maxParticleCount != 0 && spawnOnGpu( info ))
					return NULL;

				if( TheParticleSystemManager->removeOldestParticles((UnsignedInt) numInExcess, priority) != numInExcess )
					return NULL;  // could not remove enough particles, don't create new stuff
			}

			if (TheGlobalData->m_maxParticleCount == 0)
				return NULL;
		}

	}  // end if

	Particle *p = newInstance(Particle)( this, info );
	return p;

}

// ------------------------------------------------------------------------------------------------
/** One spawn to the manager's GPU path, kept in the sample ring if it takes it. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::spawnOnGpu( const ParticleInfo *info )
{
	if (info->m_lifetime == 0 || !TheParticleSystemManager->spawnGpuParticle( this, info ))
		return FALSE;

	const UnsignedInt birth = ParticleSystemManager::getParticleFrame();
	if (m_gpuSamples.empty())
		m_gpuSamples.resize( GPU_SPAWN_SAMPLES );
	GpuSpawnSample &sample = m_gpuSamples[ m_gpuSpawnTotal % GPU_SPAWN_SAMPLES ];
	sample.m_info = *info;
	sample.m_birthFrame = birth;
	++m_gpuSpawnTotal;

	// a CPU twin is updated in the pass it is born in too, and dies in the update that takes its
	// lifetime to zero
	const UnsignedInt death = birth + info->m_lifetime - 1;
	if (death > m_gpuLastDeathFrame)
		m_gpuLastDeathFrame = death;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/** Generate a new, random set of ParticleInfo
 * particleNum and particleCount are used to get 'tween frame particles emitted in the correct
 * place. (jkmcd) */
// ------------------------------------------------------------------------------------------------
const ParticleInfo *ParticleSystem::generateParticleInfo( Int particleNum, Int particleCount )
{
	static ParticleInfo info;
	if (particleCount == 0) {
		DEBUG_CRASH(("particleCount must NOT be 0. Set to 1 or greater."));
		return &info;
	}

	// NOTE: position MUST be computed before velocity, in case OUTWARD velocity is
	// specified, which must know where the particle is in space.
	info.m_pos = *computeParticlePosition();
	info.m_vel = *computeParticleVelocity( &info.m_pos );

	// transform the position and velocity, if necessary
	/// @todo Avoid conversion from Coord3D to Vector3 somehow
	if (m_isIdentity == false)
	{
		// transform particle position to world coordinates
		Vector3 p, pr;
		
		Coord3D emissionAdjustment;	// this is the adjustment for inter-frame emission
		// @todo : This should work, if m_lastPos = m_pos is removed from here but it doesn't. 
		// @todo : Investigate why. jkmcd
		if (m_isFirstPos) {
			m_lastPos = m_pos;
			m_isFirstPos = false;
		}
		
		emissionAdjustment.x = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.x - m_lastPos.x);
		emissionAdjustment.y = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.y - m_lastPos.y);
		emissionAdjustment.z = (1 - (INT_TO_REAL(particleNum) / particleCount)) * (m_pos.z - m_lastPos.z);

		p.X = info.m_pos.x;
		p.Y = info.m_pos.y;
		p.Z = info.m_pos.z;

#ifdef ALLOW_TEMPORARIES
		pr = m_transform * p;
#else
		m_transform.mulVector3(p, pr);
#endif

		info.m_pos.x = pr.X - emissionAdjustment.x;
		info.m_pos.y = pr.Y - emissionAdjustment.y;
		info.m_pos.z = pr.Z - emissionAdjustment.z;

		// transform particle velocity to world coordinates
		Vector3 v, vr;

		v.X = info.m_vel.x;
		v.Y = info.m_vel.y;
		v.Z = info.m_vel.z;

		Matrix3D::Rotate_Vector( m_transform, v, &vr );

		info.m_vel.x = vr.X;
		info.m_vel.y = vr.Y;
		info.m_vel.z = vr.Z;
	}

	info.m_velDamping = m_velDamping.getValue();
	info.m_angularDamping = m_angularDamping.getValue();

	info.m_angleZ = m_angleZ.getValue();
	info.m_angularRateZ = m_angularRateZ.getValue();

	info.m_lifetime = particleTimerFrames( m_lifetime.getValue() );

	info.m_size = m_startSize.getValue()*m_sizeCoeff*TheGlobalData->m_particleScale;
	info.m_sizeRate = m_sizeRate.getValue()*m_sizeCoeff*TheGlobalData->m_particleScale;
	info.m_sizeRateDamping = m_sizeRateDamping.getValue();

	// Keeping a running tally makes each successive particle spawned start a bit bigger (or smaller).
	info.m_size += m_accumulatedSizeBonus;
	m_accumulatedSizeBonus += m_startSizeRate.getValue();
	if( m_accumulatedSizeBonus )
		m_accumulatedSizeBonus = min( m_accumulatedSizeBonus, (float)MAX_SIZE_BONUS );

	for( int i=0; i<MAX_KEYFRAMES; i++ )
	{
		info.m_alphaKey[i].value = m_alphaKey[i].var.getValue();
		info.m_alphaKey[i].frame = m_alphaKey[i].frame;
		info.m_colorKey[i] = m_colorKey[i];
	}

/*
	info.m_color.red = m_color.red.getValue();
	info.m_color.green = m_color.green.getValue();
	info.m_color.blue = m_color.blue.getValue();
*/

	info.m_colorScale = m_colorScale.getValue();
#ifdef ALLOW_TEMPORARIES
	Vector3 pos = m_transform * Vector3(0, 0, 0);
#else
	Vector3 pos;
	m_transform.mulVector3(Vector3(0, 0, 0), pos);
#endif
	info.m_emitterPos.x = pos.X;
	info.m_emitterPos.y = pos.Y;
	info.m_emitterPos.z = pos.Z;
	info.m_particleUpTowardsEmitter = m_isParticleUpTowardsEmitter;

	info.m_windRandomness = GameClientRandomValueReal( 0.7f, 1.3f );

	return &info;
}

// ------------------------------------------------------------------------------------------------
/** Update this particle system, potentially generating new particles */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::updateEmission( Int localPlayerIndex, Bool *keepSystem )
{
	if (TheGlobalData->m_useFX == FALSE)
	{
		*keepSystem = false;
		return false;
	}

	// do initial delay ... note, this currently delays the lifetime
	if (m_delayLeft)
	{
		--m_delayLeft;

		// system actually "starts" once initial delay is over
		/// @todo reset start time when system is stopped/started
		if (m_delayLeft == 0)
			m_startTimestamp = TheGameClient->getFrame();

		*keepSystem = true;
		return false;
	}

	// update the wind motion
	if (m_windMotion != ParticleSystemInfo::WIND_MOTION_NOT_USED )
		updateWindMotion();

	// if this system is attached to a Drawable/Object, update the current transform
	// matrix so generated particles' are relative to the parent Drawable's
	// position and orientation
	Bool transformSet = false;
	const Matrix3D *parentXfrm = NULL;
	Bool isShrouded = false;
	// the hull of a tank or car is drawn trailing its logic heading, and exhaust and damage smoke
	// hang off the hull as drawn (Drawable::getDrawnHeadingLag)
	Matrix3D drawnXfrm;

	if (m_attachedToDrawableID)
	{
		Drawable *attachedTo = TheGameClient->findDrawableByID( m_attachedToDrawableID );

		if (attachedTo)
		{
			if (attachedTo->getFullyObscuredByShroud())
				isShrouded = true;

			drawnXfrm = *attachedTo->getTransformMatrix();
			drawnXfrm.Rotate_Z( attachedTo->getDrawnHeadingLag() );
			parentXfrm = &drawnXfrm;

			m_lastPos = m_pos;
			m_pos = *attachedTo->getPosition();
		}
		else
		{ 
			// Drawable has been destroyed - lose our attachment to it
			m_attachedToDrawableID = INVALID_DRAWABLE_ID;

			// destroy ourselves
			destroy();
		}
	}
	else if (m_attachedToObjectID)
	{
		Object *objectAttachedTo = TheGameLogic->findObjectByID( m_attachedToObjectID );

		if (objectAttachedTo)
		{
			if (!isShrouded)
				isShrouded = (objectAttachedTo->getShroudedStatus(localPlayerIndex) >= OBJECTSHROUD_FOGGED);
 
      const Drawable * draw = objectAttachedTo->getDrawable();
			m_attachedObjectDrawableForDraw = draw ? draw->getID() : INVALID_DRAWABLE_ID;
      if ( draw )
      {
  			drawnXfrm = *draw->getTransformMatrix();
  			drawnXfrm.Rotate_Z( draw->getDrawnHeadingLag() );
  			parentXfrm = &drawnXfrm;
      }
      else
  			parentXfrm = objectAttachedTo->getTransformMatrix();
      



			m_lastPos = m_pos;
			m_pos = *objectAttachedTo->getPosition();
		}
		else
		{ 
			// Drawable has been destroyed - lose our attachment to it
			m_attachedToObjectID = INVALID_ID;

			// destroy ourselves
			destroy();
		}
	}



	if (parentXfrm)
	{
    if (m_skipParentXfrm)
    {
      //this particle system is already in world space so no need to apply parent xform.
      m_transform = m_localTransform;
    }
    else
    {
		  // if system has its own local transform, concatenate them
		  if (m_isLocalIdentity == false)
  #ifdef ALLOW_TEMPORARIES
			  m_transform = (*parentXfrm) * m_localTransform;
  #else
			  m_transform.mul(*parentXfrm, m_localTransform);
  #endif
		  else
			  m_transform = *parentXfrm;
    }

		  m_isIdentity = false;
		  transformSet = true;
	}


	if (transformSet == false)
	{
		if (m_isLocalIdentity == false)
		{
			m_transform = m_localTransform;
			m_isIdentity = false;
		}
		else
		{
			m_isIdentity = true;
		}
	}

	// if we are controlled by a particle, its position is local origin
	if (m_controlParticle)
	{
		const Coord3D *controlPos = m_controlParticle->getPosition();
		/// @todo Concatenate this, instead of overriding (MSB)
		m_transform.Set_X_Translation( controlPos->x );
		m_transform.Set_Y_Translation( controlPos->y );
		m_transform.Set_Z_Translation( controlPos->z );
		m_isIdentity = false;
		m_lastPos = m_pos;
		m_pos = *controlPos;
	}


	//
	// Generate new particles if the system hasn't been 'stopped' or 'destroyed'
	// If we are a slave system, do not generate particles ourselves - our master will force us to
	//
	if (m_isDestroyed == false)
	{
		if (m_isForever || (m_isForever == false && m_systemLifetimeLeft > 0))
		{
			if (!isShrouded && m_isStopped == false && m_masterSystem == NULL)
			{
				if (m_burstDelayLeft == 0)
				{
					ParticlePriorityType priority = getPriority();

					// emit a burst of particles
					Int count = REAL_TO_INT(m_burstCount.getValue());

					// Windows' conversion: a huge count scaled past the int range is INT_MIN there, no burst,
					// where ARM64 saturated to INT_MAX and emitted until the loop ran out
					count = floatToIntAsMsvc(count * m_countCoeff);

					for( Int i=0; i<count; i++ )
					{
						// generate this particle's unique attributes
						const ParticleInfo *info = generateParticleInfo(i, count);
						if (!m_isEmitAboveGroundOnly || (info->m_pos.z >= TheTerrainLogic->getGroundHeight(info->m_pos.x, info->m_pos.y)))
						{
							// actually create a particle.  One the GPU took has no Particle but still feeds
							// the slave and the chain below; its system has no attached system to make
							// (m_gpuEligible), so p is never NULL there.
							const UnsignedInt gpuSpawnsBefore = m_gpuSpawnTotal;
							Particle *p = createParticle( info, priority );
							if (p == NULL && m_gpuSpawnTotal == gpuSpawnsBefore)
								continue;

							if (m_attachedSystemName.isEmpty() == false)
							{
								const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate( m_attachedSystemName );
								if (tmp)
								{
									ParticleSystem *sys = TheParticleSystemManager->createParticleSystem( tmp, TRUE );
									sys->setControlParticle( p );
									p->controlParticleSystem( sys );
								}
							}

							// create a slave particle, if necessary
							if (m_slaveSystem)
							{
								// the merge below draws a fresh master particle, so the place has to be read first
								const Coord3D rootPos = info->m_pos;

								ParticleInfo mergeInfo = ParticleSystem::mergeRelatedParticleSystems(this, m_slaveSystem, false);

								// create slaved particle
								m_slaveSystem->createParticle( &mergeInfo, priority );

								// A slave never emits and has no transform of its own, so EA's data never hung a
								// slave off a slave.  The fork's missile smoke hangs off the lens flare: every
								// system further down the chain is fed from here, in its own look and size rather
								// than scaled by the master's.  Its BurstCount is spread evenly over the path from
								// the last feed to this particle, so a rocket that covers 40 between two of its
								// master's bursts leaves a line rather than beads.  Nothing moves a slave's own
								// m_pos, so it holds the last feed point.
								for (ParticleSystem *chained = m_slaveSystem->m_slaveSystem; chained; chained = chained->m_slaveSystem)
								{
									const Coord3D from = chained->m_isFirstPos ? rootPos : chained->m_pos;
									chained->m_isFirstPos = false;
									chained->m_pos = rootPos;

									const Int chainedCount = floatToIntAsMsvc( REAL_TO_INT( chained->m_burstCount.getValue() ) * chained->m_countCoeff );
									const Coord3D *offset = chained->getSlavePositionOffset();
									for (Int k = 1; k <= chainedCount; ++k)
									{
										const Real along = INT_TO_REAL( k ) / chainedCount;
										ParticleInfo chainedInfo = *chained->generateParticleInfo( 1, 1 );
										chainedInfo.m_pos.x = from.x + (rootPos.x - from.x) * along + offset->x;
										chainedInfo.m_pos.y = from.y + (rootPos.y - from.y) * along + offset->y;
										chainedInfo.m_pos.z = from.z + (rootPos.z - from.z) * along + offset->z;
										// its own priority, not the master's: the LOD and cap checks then drop the
										// chained smoke before the streak it rides on
										chained->createParticle( &chainedInfo, chained->getPriority() );
									}
								}
							}
						}
					}
						
					// compute next burst delay
					m_burstDelayLeft = particleTimerFrames( m_burstDelay.getValue() );
					m_burstDelayLeft *= m_delayCoeff;
				}
				else
				{
					m_burstDelayLeft--;
				}

			} // end if stopped check
		} // end if system lifetime check
	} // end if is destroyed

	// decided here, after the transform and the burst, where update() always decided it
	m_pendingCastsGroundShadow = shouldCastGroundShadow();
	return true;
}

// ------------------------------------------------------------------------------------------------
/** What every particle of this system reads during this update. */
// ------------------------------------------------------------------------------------------------
Coord3D ParticleSystem::getDriftWithWeather( void ) const
{
	Coord3D drift = m_driftVelocity;
	// The Weather option's wind leans what rises and hangs in the air.  A flame's additive and a mark
	// laid on the ground stay put, and so does an area effect, which shows where a hazard really is.
	if (TheSnowManager != NULL && TheSnowManager->isWeatherOn() && m_shaderType == ParticleSystemInfo::ALPHA
			&& !m_isGroundAligned && m_priority != AREA_EFFECT)
	{
		const Coord3D &wind = TheSnowManager->getWeatherWind();
		drift.x += wind.x;
		drift.y += wind.y;
	}
	return drift;
}

void ParticleSystem::makeUpdateContext( ParticleUpdateContext *context ) const
{
	context->driftVelocity = getDriftWithWeather();
	context->gravity = m_gravity;
	context->clientFrame = TheGameClient->getFrame();
	context->shaderType = m_shaderType;
	context->windMotion = m_windMotion;
	context->groundCollision = m_groundCollision && TheTerrainLogic != NULL;
	context->groundBounce = m_groundBounce;
	context->groundFriction = m_groundFriction;
}

// ------------------------------------------------------------------------------------------------
/** Update this particle system in one pass, potentially generating new particles.  The manager
 * takes this path for a system linked to other systems, because what its particles do - die,
 * move, emit into a slave - reaches those systems within the same frame. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::update( Int localPlayerIndex )
{
	Bool keepSystem = true;
	if (!updateEmission( localPlayerIndex, &keepSystem ))
		return keepSystem;

	ParticleShadowBlob shadowBlob;
	particleShadowBlobReset( &shadowBlob );

	ParticleUpdateContext context;
	makeUpdateContext( &context );

	Particle *p = m_systemParticlesHead;
	Particle *oldParticle;
	while (p)
	{
		if (p->update( context ) == false)
		{
			oldParticle = p;
			p = p->m_systemNext;
			oldParticle->deleteInstance();
		} else {
			// one decal stands in for the whole cloud, so measure the survivors as we pass them
			if (m_pendingCastsGroundShadow)
			{
				const Coord3D *ppos = p->getPosition();
				particleShadowBlobAdd( &shadowBlob, ppos->x, ppos->y, p->getSize(), p->getAlpha() );
			}
			p = p->m_systemNext;
		}
	}

	return finishUpdate( &shadowBlob );
}

// ------------------------------------------------------------------------------------------------
/** Nothing else reads this system's particles and it writes into no other system: it is not
 * steered by a particle, its particles steer nothing (an attached system is only ever made from
 * the template name), and it has no master and no slave.  Such a system's particles can be
 * integrated at the same time as any other's.  A system gains a link only when it is created, so
 * the answer cannot change between the emission and the particles. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::isUpdateIndependent( void ) const
{
	return m_controlParticle == NULL && m_attachedSystemName.isEmpty()
			&& m_slaveSystem == NULL && m_masterSystem == NULL;
}

// ------------------------------------------------------------------------------------------------
/** The particles of update(), without removing the dead: each one is marked instead, so this reads
 * and writes nothing but this system's particles and its pending blob.  No allocation, no random
 * draws - the job pool's rules. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::updateParticlesMark( void )
{
	ParticleUpdateContext context;
	makeUpdateContext( &context );
	particleShadowBlobReset( &m_pendingShadowBlob );
	m_pendingDeaths = 0;

	for (Particle *p = m_systemParticlesHead; p; p = p->m_systemNext)
	{
		const Bool alive = p->update( context );
		p->setDiesThisUpdate( !alive );
		if (!alive)
			++m_pendingDeaths;
		// one decal stands in for the whole cloud, so measure the survivors as we pass them
		if (alive && m_pendingCastsGroundShadow)
		{
			const Coord3D *ppos = p->getPosition();
			particleShadowBlobAdd( &m_pendingShadowBlob, ppos->x, ppos->y, p->getSize(), p->getAlpha() );
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** Remove what updateParticlesMark marked, in list order, and finish the update. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::reapAndFinishUpdate( void )
{
	// Particles are appended as they are born and most die of age, so the dead are nearly always at
	// the head of the list.  Walking the whole list a second time after the marking pass cost more
	// than the marking pass saved on a single core; stopping once every marked particle is gone does
	// not.  Everything past that point was marked alive in the same pass.
	UnsignedInt remaining = m_pendingDeaths;
	Particle *p = m_systemParticlesHead;
	while (p && remaining)
	{
		Particle *next = p->m_systemNext;
		if (p->diesThisUpdate())
		{
			p->deleteInstance();
			--remaining;
		}
		p = next;
	}

	return finishUpdate( &m_pendingShadowBlob );
}

// ------------------------------------------------------------------------------------------------
/** The ground blob and the system's lifetime, once its particles are done. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::finishUpdate( const ParticleShadowBlob *blob )
{
	if (m_pendingCastsGroundShadow)
	{
		ParticleShadowBlob footprint = *blob;
		addGpuSamplesToShadowBlob( &footprint );
		updateGroundShadow( &footprint );
	}
	else
		releaseGroundShadow();

	// GPU particles still in the air hold the system exactly as CPU ones do
	const Bool gpuAlive = getGpuLifetimeLeft() != 0;

	//
	// If we have been "destroyed", wait for all of our particles to die off,
	// then destroy ourselves (return false).
	//
	// A finite slave outlives its master by its own lifetime.  Going away first nulls the slave's
	// master, and a slave with no master emits at the world origin and is cut short, so the master
	// stays in the list, destroyed and drawing nothing, until the slave is done.
	if (m_isDestroyed && !m_systemParticlesHead && !gpuAlive)
		return m_slaveSystem != NULL && !m_slaveSystem->isSystemForever();

	// monitor particle system lifetime
	if (m_isForever == false)
	{
		// decrement lifetime if not zero
		if (m_systemLifetimeLeft)
			m_systemLifetimeLeft--;

		// if there are still particles "in the air", don't destroy yet
		if (getParticleCount() || gpuAlive)
			return true;

		// check if time is up
		if (m_systemLifetimeLeft == 0)
		{
			if (m_slaveSystem == NULL)
				return false;
			// A slave that never ends was left behind here with no master, emitting on its own at the
			// world origin and never removed: every blast whose flare carries a ReforgedHotCore left
			// one, 900 of them after 15000 frames of a four-AI match.  It goes with its master now.
			if (m_slaveSystem->isSystemForever())
			{
				m_slaveSystem->destroy();
				return false;
			}
			m_isDestroyed = true;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt ParticleSystemManager::getParticleFrame( void )
{
	return TheParticleSystemManager->m_particleFrame;
}

// ------------------------------------------------------------------------------------------------
/** Counted as a CPU particle's getLifetimeLeft is after this frame's update: zero in the update it
	* dies in. */
// ------------------------------------------------------------------------------------------------
UnsignedInt ParticleSystem::getGpuLifetimeLeft( void ) const
{
	const UnsignedInt now = ParticleSystemManager::getParticleFrame();
	return m_gpuLastDeathFrame > now ? m_gpuLastDeathFrame - now : 0;
}

// ------------------------------------------------------------------------------------------------
/** 1 + r + ... + r^(k-1), the sum a damped rate adds up to over k updates. */
// ------------------------------------------------------------------------------------------------
static Real particleDampedSum( Real r, UnsignedInt k )
{
	if (fabsf( 1.0f - r ) < 1e-4f)
		return (Real)k;
	return ( 1.0f - powf( r, (Real)k ) ) / ( 1.0f - r );
}

// ------------------------------------------------------------------------------------------------
/** The GPU particles' share of the ground blob.  Each kept spawn is aged over what the blob reads
	* of Particle::update: the ground plane only, where gravity does not reach, so position is the
	* damped velocity plus the drift; size the damped rate; alpha the straight line between its keys.
	* ponytail: GPU_SPAWN_SAMPLES spawns stand in for the whole cloud and alpha skips the key snap
	* and the fade envelope; ParticleClosedForm.h's evaluator replaces this once it lands. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::addGpuSamplesToShadowBlob( ParticleShadowBlob *blob ) const
{
	const UnsignedInt now = ParticleSystemManager::getParticleFrame();
	const UnsignedInt samples = getGpuSampleCount();
	const Coord3D drift = getDriftWithWeather();
	for (UnsignedInt i = 0; i < samples; ++i)
	{
		const GpuSpawnSample &sample = getGpuSample( i );
		const ParticleInfo &info = sample.m_info;
		const UnsignedInt k = now - sample.m_birthFrame + 1;		// updates since birth
		if (k >= info.m_lifetime)
			continue;

		const Real travel = info.m_velDamping * particleDampedSum( info.m_velDamping, k );
		const Real x = info.m_pos.x + k * drift.x + info.m_vel.x * travel;
		const Real y = info.m_pos.y + k * drift.y + info.m_vel.y * travel;
		const Real size = info.m_size + info.m_sizeRate * particleDampedSum( info.m_sizeRateDamping, k );

		Real alpha = info.m_alphaKey[ 0 ].value;
		for (Int j = 1; j < MAX_KEYFRAMES && info.m_alphaKey[ j ].frame; ++j)
		{
			const Keyframe &from = info.m_alphaKey[ j - 1 ];
			const Keyframe &to = info.m_alphaKey[ j ];
			if (k >= to.frame)
			{
				alpha = to.value;
				continue;
			}
			alpha = from.value + ( to.value - from.value ) * (Real)( k - from.frame ) / (Real)( to.frame - from.frame );
			break;
		}

		particleShadowBlobAdd( blob, x, y, size, alpha );
	}
}

// ------------------------------------------------------------------------------------------------
/** Give the ground blob back.  The decal's manager lives inside TheGameClient, and shutdownAll
 * runs in reverse init order, so TheGameClient - and with it every decal it was holding - is gone
 * before TheParticleSystemManager destroys the systems.  Releasing then would follow a dangling
 * pointer, so the NULL manager is the signal that there is nothing left to give back. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::releaseGroundShadow( void )
{
	if (m_groundShadow)
	{
		if (TheProjectedShadowManager)
			m_groundShadow->release();
		m_groundShadow = NULL;
	}
}

// ------------------------------------------------------------------------------------------------
/** Is this system a big soft cloud - smoke, dust, a plume - rather than fire, a glow or a decal
 * that is already on the ground?  Additive systems are light rather than shade, and a
 * ground-aligned one is drawn flat on the terrain already. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystem::shouldCastGroundShadow( void ) const
{
	// not defensive: test_gameengine_stubs.cpp defines the manager pointer as NULL
	if (TheProjectedShadowManager == NULL || TheGlobalData == NULL)
		return FALSE;

	if (!TheGlobalData->m_shadowsForParticles)
		return FALSE;

	if (m_particleType != PARTICLE && m_particleType != VOLUME_PARTICLE)
		return FALSE;

	if (m_shaderType != ALPHA && m_shaderType != ALPHA_TEST)
		return FALSE;

	// a fireball's body gives light and throws no shadow
	if (m_template->isEmissive())
		return FALSE;

	// the sun's map holds this cloud already and shades the ground under it through its own density
	if (isInSunMap())
		return FALSE;

	if (m_isGroundAligned)
		return FALSE;

	return getStealthFade() > 0.0f;
}

// ------------------------------------------------------------------------------------------------
/** The smoke and fire on a stealthed unit or building go with it.  An enemy's that the local player
 * cannot see draw nothing: retail stopped only the model's own bone systems, and a hidden base still
 * burned and smoked in plain view.  The player's own stealthed one breathes with its stealth
 * opacity, a third of it for smoke and a little over half for fire, beside the glass the cloak draws.
 * Detected by the enemy, or not stealthed at all, it draws whole.  A slave follows its master, which
 * carries the attachment.  Client drawing only; nothing goes back into the logic. */
// ------------------------------------------------------------------------------------------------
Real ParticleSystem::getStealthFade( void ) const
{
	const ParticleSystem *sys = this;
	while (sys->m_masterSystem)
		sys = sys->m_masterSystem;

	const Drawable *draw = NULL;
	if (sys->m_attachedToDrawableID != INVALID_DRAWABLE_ID)
		draw = TheGameClient->findDrawableByID( sys->m_attachedToDrawableID );
	else if (LogicThread::isInsideWindow())
		draw = TheGameClient->findDrawableByID( sys->m_attachedObjectDrawableForDraw );
	else if (sys->m_attachedToObjectID != INVALID_ID)
	{
		// gone since the last particle update, which then destroys the system
		const Object *obj = TheGameLogic->findObjectByID( sys->m_attachedToObjectID );
		if (obj)
			draw = obj->getDrawable();
	}
	if (draw == NULL)
		return 1.0f;

	switch (draw->getStealthLook())
	{
		case STEALTHLOOK_INVISIBLE:
			return 0.0f;
		case STEALTHLOOK_VISIBLE_FRIENDLY:
		case STEALTHLOOK_VISIBLE_FRIENDLY_DETECTED:
			// Alpha tested particles take no fade (less alpha only cuts holes in them), and a
			// multiplying one half faded is a grey smear, so those draw whole and not at all.
			switch (m_shaderType)
			{
				case ADDITIVE:		return draw->getEffectiveOpacity() * 0.55f;
				case ALPHA_TEST:	return 1.0f;
				case MULTIPLY:		return 0.0f;
				default:					return draw->getEffectiveOpacity() * 0.35f;
			}
		default:
			return 1.0f;
	}
}

// ------------------------------------------------------------------------------------------------
/** Refresh - or release - the one decal that shades the ground under this system's particles. */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::updateGroundShadow( const ParticleShadowBlob *blob )
{
	Real centerX, centerY, sizeX, sizeY;
	Int opacity;

	if (!particleShadowBlobResolve( blob, &centerX, &centerY, &sizeX, &sizeY, &opacity ))
	{
		// the cloud shrank, thinned out or blew away: take the stain with it
		releaseGroundShadow();
		return;
	}

	if (m_groundShadow == NULL)
	{
		Shadow::ShadowTypeInfo decalInfo;
		decalInfo.allowUpdates = FALSE;			// the texture never changes
		decalInfo.allowWorldAlign = TRUE;		// let it wrap over the terrain it lands on
		// SHADOW_ALPHA_DECAL, not SHADOW_DECAL: only the alpha/additive decals are honoured by
		// setOpacity/setColor and taken off m_decalList by removeShadow, and this one has to fade.
		decalInfo.m_type = SHADOW_ALPHA_DECAL;
		strcpy( decalInfo.m_ShadowName, "shadow" );
		// nonzero on purpose - this addDecal overload divides by the size directly
		decalInfo.m_sizeX = sizeX;
		decalInfo.m_sizeY = sizeY;
		decalInfo.m_offsetX = 0.0f;
		decalInfo.m_offsetY = 0.0f;

		m_groundShadow = TheProjectedShadowManager->addDecal( &decalInfo );
		if (m_groundShadow == NULL)
			return;

		m_groundShadow->setAngle( 0.0f );
		m_groundShadow->setColor( 0x00000000 );		// black: this is shade, not a tint
	}

	m_groundShadow->setSize( sizeX, sizeY );
	m_groundShadow->setPosition( centerX, centerY,
															 TheTerrainLogic ? TheTerrainLogic->getGroundHeight( centerX, centerY ) : 0.0f );
	m_groundShadow->setOpacity( REAL_TO_INT( opacity * getStealthFade() ) );
}

// ------------------------------------------------------------------------------------------------
/** Update the wind motion */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::updateWindMotion( void )
{

	switch( m_windMotion )
	{

		// --------------------------------------------------------------------------------------------
		case ParticleSystemInfo::WIND_MOTION_PING_PONG:
		{
			Real startAngle = m_windMotionStartAngle;
			Real endAngle = m_windMotionEndAngle;

			// this only works when start angle is less than end angle
			DEBUG_ASSERTCRASH( startAngle < endAngle, ("updateWindMotion: startAngle must be < endAngle\n") );

			// how big is the total angle span
			Real totalSpan = endAngle - startAngle;
			Real halfSpan = totalSpan / 2.0f;

			// given our current angle ... how far away from the "center" of the span are we
			Real diffFromCenter = fabs( halfSpan - m_windAngle + startAngle );
	
			//
			// given our distance from the center ... we need to compute how much we will change
			// the angle.  When we are closer to the center we change it faster (more), and when
			// we are near the edges we change is slower (less)
			//
			Real change = (1.0f - (diffFromCenter / halfSpan)) * m_windAngleChange;

			// we will always change a little bit
			#define MINIMUM_CHANGE ConvertRetailPerFrameRate( 0.005f )  // lower #'s have softer swings at the edge angles
			if( change < MINIMUM_CHANGE )
				change = MINIMUM_CHANGE;
			
			//
			// if we are moving toward the end angle we add the change, if we're moving away
			// from it we subtract it
			//
			if( m_windMotionMovingToEndAngle )
			{

				// add angle
				m_windAngle += change;

				// see if we're at the end and should switch directions
				if( m_windAngle >= endAngle )
				{

					// change directions
					m_windMotionMovingToEndAngle = FALSE;

					// pick a new change delta
					m_windAngleChange = 
							GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

					// pick new start and end angles
					m_windMotionStartAngle = 
							GameClientRandomValueReal( m_windMotionStartAngleMin, 
																				 m_windMotionStartAngleMax );
					m_windMotionEndAngle = 
							GameClientRandomValueReal( m_windMotionEndAngleMin, 
																				 m_windMotionEndAngleMax );

				}  // end if

			}  // end if
			else
			{

				// subtract angle
				m_windAngle -= change;

				// see if we're at the end and should switch directions
				if( m_windAngle <= startAngle )
				{

					// change directions
					m_windMotionMovingToEndAngle = TRUE;

					// pick a new change delta
					m_windAngleChange = 
							GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

					// pick new start and end angles
					m_windMotionStartAngle = 
							GameClientRandomValueReal( m_windMotionStartAngleMin, 
																				 m_windMotionStartAngleMax );
					m_windMotionEndAngle = 
							GameClientRandomValueReal( m_windMotionEndAngleMin, 
																				 m_windMotionEndAngleMax );

				}  // end if

			}  // end else

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case ParticleSystemInfo::WIND_MOTION_CIRCULAR:
		{

			// give us a wind angle change if one hasn't been specifed (this plays nice with the particle editor)
			if( m_windAngleChange == 0.0f )
				m_windAngleChange = GameClientRandomValueReal( m_windAngleChangeMin, m_windAngleChangeMax );

			// add to our wind angle
			m_windAngle += m_windAngleChange;
			
			// keep in 0 to 2PI range just to keep the numbers safe and sane
			if( m_windAngle > TWO_PI )
				m_windAngle -= TWO_PI;
			else if( m_windAngle < 0.0f )
				m_windAngle += TWO_PI;

			break;

		}  // end case

		// ---------------------------------------------------------------------------------------------
		default:
		{

			break;

		}  // end default

	}  // end if

}  // end updateWindMotion

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::addParticle( Particle *particleToAdd )
{
	if (particleToAdd->m_inSystemList)
		return;

	if (!m_systemParticlesHead)
	{
		m_systemParticlesHead = particleToAdd;
	}

	if (m_systemParticlesTail)
	{
		m_systemParticlesTail->m_systemNext = particleToAdd;
		particleToAdd->m_systemPrev = m_systemParticlesTail;
	}
	else
	{
		particleToAdd->m_systemPrev = NULL;
	}

	m_systemParticlesTail = particleToAdd;
	particleToAdd->m_systemNext = NULL;
	particleToAdd->m_inSystemList = TRUE;

	++m_particleCount;

	particleToAdd->setPersonality( m_personalityStore++ ); 

}

// ------------------------------------------------------------------------------------------------
/** Remove given particle from the list - ONLY FOR USE BY PARTICLE */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::removeParticle( Particle *particleToRemove )
{
	if (!particleToRemove->m_inSystemList)
		return;

	// remove links from prev & next objs
	if (particleToRemove->m_systemNext)
		particleToRemove->m_systemNext->m_systemPrev = particleToRemove->m_systemPrev;
	if (particleToRemove->m_systemPrev)
		particleToRemove->m_systemPrev->m_systemNext = particleToRemove->m_systemNext;

	// update head & tail if neccessary
	if (particleToRemove == m_systemParticlesHead)
		m_systemParticlesHead = particleToRemove->m_systemNext;
	if (particleToRemove == m_systemParticlesTail)
		m_systemParticlesTail = particleToRemove->m_systemPrev;

	particleToRemove->m_systemNext = particleToRemove->m_systemPrev = NULL;
	particleToRemove->m_inSystemList = FALSE;
	--m_particleCount;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleInfo ParticleSystem::mergeRelatedParticleSystems( ParticleSystem *masterParticleSystem, ParticleSystem *slaveParticleSystem, Bool slaveNeedsFullPromotion)
{
	if (!masterParticleSystem || !slaveParticleSystem) {
		DEBUG_CRASH(("masterParticleSystem or slaveParticleSystem was NULL. Should not happen. JKMCD"));
		ParticleInfo bogus;
		memset( &bogus, 0, sizeof( bogus ) );
		return bogus;
	}

	// copy info
	ParticleInfo mergeInfo = *masterParticleSystem->generateParticleInfo(1, 1);

	// generate one from the slave system
	const ParticleInfo *info = slaveParticleSystem->generateParticleInfo(1, 1);

	// override unique attributes of slave particle
	mergeInfo.m_lifetime = info->m_lifetime;

	// size becomes a scale factor of master's particles
	mergeInfo.m_size *= info->m_size;
	mergeInfo.m_sizeRate *= info->m_sizeRate;
	mergeInfo.m_sizeRateDamping *= info->m_sizeRateDamping;

	mergeInfo.m_angleZ = info->m_angleZ;
	mergeInfo.m_angularRateZ = info->m_angularRateZ;
	mergeInfo.m_angularDamping = info->m_angularDamping;

	// i is used after the loop; VC6 for-scope let it escape.
	int i;
	for( i =0; i<MAX_KEYFRAMES; i++ )
		mergeInfo.m_alphaKey[i] = info->m_alphaKey[i];

	for( i=0; i<MAX_KEYFRAMES; i++ )
		mergeInfo.m_colorKey[i] = info->m_colorKey[i];

	mergeInfo.m_colorScale = info->m_colorScale;

	// offset slave's position relative to master's
	const Coord3D *offset = slaveParticleSystem->getSlavePositionOffset();
	mergeInfo.m_pos.x += offset->x;
	mergeInfo.m_pos.y += offset->y;
	mergeInfo.m_pos.z += offset->z;

	if (slaveNeedsFullPromotion) {
		slaveParticleSystem->m_burstCount = masterParticleSystem->m_burstCount;
		slaveParticleSystem->m_burstDelay = masterParticleSystem->m_burstDelay;

		slaveParticleSystem->m_priority = masterParticleSystem->m_priority;
		slaveParticleSystem->m_emissionVelocity = masterParticleSystem->m_emissionVelocity;
		slaveParticleSystem->m_emissionVelocityType = masterParticleSystem->m_emissionVelocityType;
		slaveParticleSystem->m_emissionVolume = masterParticleSystem->m_emissionVolume;
		slaveParticleSystem->m_emissionVolumeType = masterParticleSystem->m_emissionVolumeType;
		slaveParticleSystem->m_isEmissionVolumeHollow = masterParticleSystem->m_isEmissionVolumeHollow;

		
		slaveParticleSystem->m_startSize.setRange(masterParticleSystem->m_startSize.getMinimumValue() * slaveParticleSystem->m_startSize.getMinimumValue(),
																							masterParticleSystem->m_startSize.getMaximumValue() * slaveParticleSystem->m_startSize.getMaximumValue(),
																							masterParticleSystem->m_startSize.getDistributionType());
	
		slaveParticleSystem->m_sizeRate.setRange(masterParticleSystem->m_sizeRate.getMinimumValue() * slaveParticleSystem->m_sizeRate.getMinimumValue(),
																							masterParticleSystem->m_sizeRate.getMaximumValue() * slaveParticleSystem->m_sizeRate.getMaximumValue(),
																							masterParticleSystem->m_sizeRate.getDistributionType());

		slaveParticleSystem->m_sizeRateDamping.setRange(masterParticleSystem->m_sizeRateDamping.getMinimumValue() * slaveParticleSystem->m_sizeRateDamping.getMinimumValue(),
																							masterParticleSystem->m_sizeRateDamping.getMaximumValue() * slaveParticleSystem->m_sizeRateDamping.getMaximumValue(),
																							masterParticleSystem->m_sizeRateDamping.getDistributionType());
		
//		slaveParticleSystem->m_burstCount.setRange(masterParticleSystem->m_burstCount.getMinimumValue() / 2, 
//																							 masterParticleSystem->m_burstCount.getMaximumValue() / 2,
//																							 masterParticleSystem->m_burstCount.getDistributionType());
		
	}

	return mergeInfo;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setLifetimeRange( Real min, Real max )
{
	m_lifetime.setRange( min, max );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ParticleSystem::setControlParticle( Particle *p )
{
	m_controlParticle = p;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// base class info
	ParticleSystemInfo::xfer( xfer );

	// particle system ID
	const ParticleSystemID idBeforeXfer = m_systemID;
	xfer->xferUser( &m_systemID, sizeof( ParticleSystemID ) );
	if( m_systemID != idBeforeXfer )
		TheParticleSystemManager->friend_renumberParticleSystem( this, idBeforeXfer );

	// attached to drawable id
	xfer->xferDrawableID( &m_attachedToDrawableID );

	// attached to object id
	xfer->xferObjectID( &m_attachedToObjectID );

	// is local identity
	xfer->xferBool( &m_isLocalIdentity );

	// local transform
	xfer->xferUser( &m_localTransform, sizeof( Matrix3D ) );

	// is identity
	xfer->xferBool( &m_isIdentity );

	// transform
	xfer->xferUser( &m_transform, sizeof( Matrix3D ) );

	// burst delay left
	xfer->xferUnsignedInt( &m_burstDelayLeft );

	// delay left
	xfer->xferUnsignedInt( &m_delayLeft );

	// start timestamp
	xfer->xferUnsignedInt( &m_startTimestamp );

	// system lifetime left
	xfer->xferUnsignedInt( &m_systemLifetimeLeft );

	// personality store
	xfer->xferUnsignedInt( &m_personalityStore );

	// is forever
	xfer->xferBool( &m_isForever );

	// accumulated size bonus
	xfer->xferReal( &m_accumulatedSizeBonus );

	// is stopped
	xfer->xferBool( &m_isStopped );

	// we never save destroyed particle systems so there is no need to consider m_isDestroyed
	// m_isDestroyed <-- do nothing with me

	// ditto for m_isSaveable
	// m_isSaveable <-- do nothing with me

	// velCoeff
	xfer->xferCoord3D( &m_velCoeff );

	// count coeff
	xfer->xferReal( &m_countCoeff );

	// delay coeff
	xfer->xferReal( &m_delayCoeff );

	// size coeff
	xfer->xferReal( &m_sizeCoeff );

	// position
	xfer->xferCoord3D( &m_pos );

	// last position
	xfer->xferCoord3D( &m_lastPos );

	// is first pos
	xfer->xferBool( &m_isFirstPos );

	// slave system id
	xfer->xferUser( &m_slaveSystemID, sizeof( ParticleSystemID ) );

	// master system
	xfer->xferUser( &m_masterSystemID, sizeof( ParticleSystemID ) );

	// particle count
	UnsignedInt particleCount = m_particleCount;
	xfer->xferUnsignedInt( &particleCount );

	// particles
	if( xfer->getXferMode() == XFER_SAVE )
	{
		Particle *particle;

		// go through all particles in this system
		for( particle = m_systemParticlesHead; particle; particle = particle->m_systemNext )
		{

			// write particle information
			xfer->xferSnapshot( particle );

		}  // end for, particle

	}  // end if, save
	else
	{
		ParticlePriorityType priority = getPriority();
		const ParticleInfo *info = generateParticleInfo( 0, 1 );
		Particle *particle;

		// read each particle data block
		for( UnsignedInt i = 0; i < particleCount; ++i )
		{

			// create a new particle
			particle = createParticle( info, priority, TRUE );

			// sanity
			DEBUG_ASSERTCRASH( particle, ("ParticleSyste::xfer - Unable to create particle for loading\n") );

			// read in the particle data
			xfer->xferSnapshot( particle );

		}  // end for i

	}  // end else, load

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystem::loadPostProcess( void )
{

	// call base class post process
	ParticleSystemInfo::loadPostProcess();

	// reconnect slave pointers if needed
	if( m_slaveSystemID != INVALID_PARTICLE_SYSTEM_ID )
	{

		// sanity
		if( m_slaveSystem != NULL )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_slaveSystem is not NULL but should be\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// assign system
		m_slaveSystem = TheParticleSystemManager->findParticleSystem( m_slaveSystemID );

		// sanity
		if( m_slaveSystem == NULL || m_slaveSystem->isDestroyed() == TRUE )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_slaveSystem is NULL or destroyed\n" ));
			throw SC_INVALID_DATA;

		}  // end if

	}  // end if

	// reconnect master pointers if needed
	if( m_masterSystemID != INVALID_PARTICLE_SYSTEM_ID )
	{

		// sanity
		if( m_masterSystem != NULL )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_masterSystem is not NULL but should be\n" ));
			throw SC_INVALID_DATA;

		}  // end if

		// assign system
		m_masterSystem = TheParticleSystemManager->findParticleSystem( m_masterSystemID );

		// sanity
		if( m_masterSystem == NULL || m_masterSystem->isDestroyed() == TRUE )
		{

			DEBUG_CRASH(( "ParticleSystem::loadPostProcess - m_masterSystem is NULL or destroyed\n" ));
			throw SC_INVALID_DATA;

		}  // end if

	}  // end if

}  // end loadPostProcess

///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystemTemplate /////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** INI parse data */
// ------------------------------------------------------------------------------------------------
const FieldParse ParticleSystemTemplate::m_fieldParseTable[] = 
{
	{ "Priority",								INI::parseIndexList, ParticlePriorityNames, offsetof( ParticleSystemTemplate, m_priority ) },
	{ "IsOneShot",							INI::parseBool,						NULL,		offsetof( ParticleSystemTemplate, m_isOneShot ) },
	{ "Shader",									INI::parseIndexList,			ParticleShaderTypeNames,		offsetof( ParticleSystemTemplate, m_shaderType ) },
	{ "Type",										INI::parseIndexList,			ParticleTypeNames,		offsetof( ParticleSystemTemplate, m_particleType ) },
	{ "VolumeParticleDepth",		INI::parseUnsignedInt,		NULL,		offsetof( ParticleSystemTemplate, m_volumeParticleDepth ) },
	{ "ParticleName",						INI::parseAsciiString,		NULL,		offsetof( ParticleSystemTemplate, m_particleTypeName ) },
	{ "AngleZ",									INI::parseGameClientRandomVariable,	NULL,		offsetof( ParticleSystemTemplate, m_angleZ ) },
	{ "AngularRateZ",						parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_angularRateZ ) },
	{ "AngularDamping",					parseRetailPerFrameFactorRandom,	NULL,		offsetof( ParticleSystemTemplate, m_angularDamping ) },

	{ "VelocityDamping",				parseRetailPerFrameFactorRandom,	NULL,		offsetof( ParticleSystemTemplate, m_velDamping ) },
	{ "Gravity",								parseRetailPerFrameSquaredReal,								NULL,		offsetof( ParticleSystemTemplate, m_gravity ) },
	{ "GroundCollision",						INI::parseBool,																NULL,		offsetof( ParticleSystemTemplate, m_groundCollision ) },
	{ "GroundBounce",							INI::parseReal,																NULL,		offsetof( ParticleSystemTemplate, m_groundBounce ) },
	// applied on every frame a particle comes down on the ground, which for one at rest is every frame
	{ "GroundFriction",						INI::parseRetailPerFrameFactorReal,						NULL,		offsetof( ParticleSystemTemplate, m_groundFriction ) },
	{ "SlaveSystem",						INI::parseAsciiString,												NULL,		offsetof( ParticleSystemTemplate, m_slaveSystemName ) },
	{ "SlavePosOffset",					INI::parseCoord3D,														NULL,		offsetof( ParticleSystemTemplate, m_slavePosOffset ) },
	{ "PerParticleAttachedSystem",		INI::parseAsciiString,								NULL,		offsetof( ParticleSystemTemplate, m_attachedSystemName ) },
	{ "ExplosionDebrisTrail",				INI::parseBool,												NULL,		offsetof( ParticleSystemTemplate, m_explosionDebrisTrail ) },
	{ "FireLook",										INI::parseBool,												NULL,		offsetof( ParticleSystemTemplate, m_fireLook ) },
	{ "Emissive",										INI::parseBool,												NULL,		offsetof( ParticleSystemTemplate, m_emissive ) },
	{ "FlipbookGrid",								parseFlipbookGrid,										NULL,		offsetof( ParticleSystemTemplate, m_flipbookGridLog2 ) },
	{ "FlipbookRate",								INI::parseReal,												NULL,		offsetof( ParticleSystemTemplate, m_flipbookRate ) },
	{ "FlipbookRandomStart",				INI::parseBool,												NULL,		offsetof( ParticleSystemTemplate, m_flipbookRandomStart ) },

	{ "Lifetime",								parseRetailFramesRandom,	NULL,		offsetof( ParticleSystemTemplate, m_lifetime ) },
	{ "SystemLifetime",					INI::parseRetailFramesUnsignedInt,						NULL,		offsetof( ParticleSystemTemplate, m_systemLifetime ) },

	{ "Size",										INI::parseGameClientRandomVariable,	NULL,		offsetof( ParticleSystemTemplate, m_startSize ) },
	// per particle emitted, not per frame
	{ "StartSizeRate",					INI::parseGameClientRandomVariable,	NULL,		offsetof( ParticleSystemTemplate, m_startSizeRate ) },
	{ "SizeRate",								parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_sizeRate ) },
	{ "SizeRateDamping",				parseRetailPerFrameFactorRandom,	NULL,		offsetof( ParticleSystemTemplate, m_sizeRateDamping ) },

	{ "Alpha1",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[0] ) },
	{ "Alpha2",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[1] ) },
	{ "Alpha3",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[2] ) },
	{ "Alpha4",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[3] ) },
	{ "Alpha5",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[4] ) },
	{ "Alpha6",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[5] ) },
	{ "Alpha7",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[6] ) },
	{ "Alpha8",									ParticleSystemTemplate::parseRandomKeyframe,	NULL,		offsetof( ParticleSystemTemplate, m_alphaKey[7] ) },

	{ "Color1",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[0] ) },
	{ "Color2",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[1] ) },
	{ "Color3",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[2] ) },
	{ "Color4",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[3] ) },
	{ "Color5",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[4] ) },
	{ "Color6",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[5] ) },
	{ "Color7",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[6] ) },
	{ "Color8",									ParticleSystemTemplate::parseRGBColorKeyframe,NULL,		offsetof( ParticleSystemTemplate, m_colorKey[7] ) },

//	{ "COLOR",									ParticleSystemTemplate::parseRandomRGBColor,	NULL,		offsetof( ParticleSystemTemplate, m_color ) },
	{ "ColorScale",							parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_colorScale ) },

	{ "BurstDelay",							parseRetailBurstDelayRandom,	NULL,		offsetof( ParticleSystemTemplate, m_burstDelay ) },
	{ "BurstCount",							INI::parseGameClientRandomVariable,	NULL,		offsetof( ParticleSystemTemplate, m_burstCount ) },

	{ "InitialDelay",						parseRetailFramesRandom,	NULL,		offsetof( ParticleSystemTemplate, m_initialDelay ) },

	{ "DriftVelocity",					parseRetailPerFrameCoord3D,										NULL,		offsetof( ParticleSystemTemplate, m_driftVelocity ) },
	{ "VelocityType",						INI::parseIndexList,													EmissionVelocityTypeNames,		offsetof( ParticleSystemTemplate, m_emissionVelocityType ) },
	{ "VelOrthoX",							parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.x ) },
	{ "VelOrthoY",							parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.y ) },
	{ "VelOrthoZ",							parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.ortho.z ) },

	{ "VelSpherical",						parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.spherical.speed ) },
	{ "VelHemispherical",				parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.hemispherical.speed ) },

	{ "VelCylindricalRadial",		parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.cylindrical.radial ) },
	{ "VelCylindricalNormal",		parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.cylindrical.normal ) },

	{ "VelOutward",							parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.outward.speed ) },
	{ "VelOutwardOther",				parseRetailPerFrameRandom,	NULL,		offsetof( ParticleSystemTemplate, m_emissionVelocity.outward.otherSpeed ) },

	{ "VolumeType",							INI::parseIndexList,													EmissionVolumeTypeNames,		offsetof( ParticleSystemTemplate, m_emissionVolumeType ) },
	{ "VolLineStart",						INI::parseCoord3D,														NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.line.start ) },
	{ "VolLineEnd",							INI::parseCoord3D,														NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.line.end ) },

	{ "VolBoxHalfSize",					INI::parseCoord3D,														NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.box.halfSize ) },

	{ "VolSphereRadius",				INI::parseReal,																NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.sphere.radius ) },

	{ "VolCylinderRadius",			INI::parseReal,																NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.cylinder.radius ) },
	{ "VolCylinderLength",			INI::parseReal,																NULL,		offsetof( ParticleSystemTemplate, m_emissionVolume.cylinder.length ) },

	{ "IsHollow",								INI::parseBool,																NULL,		offsetof( ParticleSystemTemplate, m_isEmissionVolumeHollow ) },
	{ "IsGroundAligned",				INI::parseBool,																NULL,		offsetof( ParticleSystemTemplate, m_isGroundAligned ) },
	{ "IsEmitAboveGroundOnly",	INI::parseBool,																NULL,		offsetof( ParticleSystemTemplate, m_isEmitAboveGroundOnly) },
	{ "IsParticleUpTowardsEmitter",	INI::parseBool,																NULL,		offsetof( ParticleSystemTemplate, m_isParticleUpTowardsEmitter) },

	{ "WindMotion",					INI::parseIndexList, WindMotionNames, offsetof( ParticleSystemTemplate, m_windMotion ) },

	{ "WindAngleChangeMin", INI::parseRetailPerFrameReal, NULL, offsetof( ParticleSystemTemplate, m_windAngleChangeMin ) },
	{ "WindAngleChangeMax", INI::parseRetailPerFrameReal, NULL, offsetof( ParticleSystemTemplate, m_windAngleChangeMax ) },

	{ "WindPingPongStartAngleMin",			INI::parseReal, NULL, offsetof( ParticleSystemTemplate, m_windMotionStartAngleMin ) },
	{ "WindPingPongStartAngleMax",			INI::parseReal, NULL, offsetof( ParticleSystemTemplate, m_windMotionStartAngleMax ) },

	{ "WindPingPongEndAngleMin",				INI::parseReal, NULL, offsetof( ParticleSystemTemplate, m_windMotionEndAngleMin ) },
	{ "WindPingPongEndAngleMax",				INI::parseReal, NULL, offsetof( ParticleSystemTemplate, m_windMotionEndAngleMax ) },


	{ NULL,											NULL,																					NULL,		0 },
};

// ------------------------------------------------------------------------------------------------
/** Parse a "random keyframe".
 * The format is "FIELD = low high frame". */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRandomKeyframe( INI* ini, void *instance,
																											 void *store, const void* /*userData*/ )
{
	RandomKeyframe *key = static_cast<RandomKeyframe *>(store);

	Real low = ini->scanReal(ini->getNextToken());
	Real high = ini->scanReal(ini->getNextToken());
	key->frame = ConvertRetailFrames(ini->scanUnsignedInt(ini->getNextToken()));

	// set the range of the random variable
	key->var.setRange( low, high );
}

// ------------------------------------------------------------------------------------------------
/** Parse a "color keyframe".
 * The format is "FIELD = R:r G:g B:b frame". */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRGBColorKeyframe( INI* ini, void *instance,
																													void *store, const void* /*userData*/ )
{
	RGBColorKeyframe *key = static_cast<RGBColorKeyframe *>(store);

	INI::parseRGBColor( ini, instance, &key->color, NULL );
	INI::parseRetailFramesUnsignedInt( ini, instance, &key->frame, NULL );
}

// ------------------------------------------------------------------------------------------------
/** Parse a RandomVariable RGB color.
 * Note that the components may be negative, as this is used for rates, as well. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::parseRandomRGBColor( INI* ini, void *instance, 
																											 void *store, const void* /*userData*/ )
{
#if 0
	char seps[] = " \n\r\t=:RGB,";
	const char *token;
	Int colors[2][3];
	Int result;

	enum { LO = 0, HI = 1 };
	enum { RED = 0, GREEN = 1, BLUE = 2 };

	// initialize to invalid values
	colors[ LO ][ RED ] = -1;
	colors[ LO ][ GREEN ] = -1;
	colors[ LO ][ BLUE ] = -1;
	colors[ HI ][ RED ] = -1;
	colors[ HI ][ GREEN ] = -1;
	colors[ HI ][ BLUE ] = -1;

	// do each color part
	for( Int i = 0; i < 3; i++ )
	{
		for( Int j = 0; j < 2; j++ )
		{
			// get the color number
			token = ini->getNextToken(seps);

			// convert to number
			colors[j][i] = ini->scanInt(token);

			// check to see if it's within range
			if( colors[j][i] < -255 || colors[j][i] > 255 )
				throw INI_INVALID_DATA;

		}  // end for i
	}

	// assign the color components to the "RGBColor" pointer at 'store'
	ParticleSystemInfo::RandomRGBColor *theColor = (ParticleSystemInfo::RandomRGBColor *)store;

	theColor->red.setRange( (Real)colors[ LO ][ RED ] / 255.0f, (Real)colors[ HI ][ RED ] / 255.0f ); 
	theColor->green.setRange( (Real)colors[ LO ][ GREEN ] / 255.0f, (Real)colors[ HI ][ GREEN ] / 255.0f ); 
	theColor->blue.setRange( (Real)colors[ LO ][ BLUE ] / 255.0f, (Real)colors[ HI ][ BLUE ] / 255.0f ); 
#endif
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate::ParticleSystemTemplate( const AsciiString &name ) :
	m_name(name)
{ 
	//Added By Sadullah Nader
	//Initializations inserted
	m_slaveTemplate = NULL;
	//
	m_explosionDebrisTrail = FALSE;
	m_fireLook = FALSE;
	m_emissive = FALSE;
	m_flipbookGridLog2 = 0;
	m_flipbookRate = 0.0f;
	m_flipbookRandomStart = FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate::~ParticleSystemTemplate()
{
 
}

// ------------------------------------------------------------------------------------------------
/** Make this system's smoke last longer and read thicker ("-smoke"). */
// ------------------------------------------------------------------------------------------------
void ParticleSystemTemplate::forceSmokeBoost( const SmokeBoost &boost )
{
	// Smoke that outlives the explosion has to outrank it: removeOldestParticles only takes
	// particles below the priority of whatever is asking for room, so smoke left down among the
	// weapon effects is deleted by the next shell that lands.  AREA_EFFECT and above are left
	// alone - AREA_EFFECT carries its own field-particle ceiling and CRITICAL is the superweapons.
	const ParticlePriorityType PRIORITY_FLOOR = SEMI_CONSTANT;

	m_lifetime.setRange( m_lifetime.getMinimumValue() * boost.m_lifetimeScale,
											 m_lifetime.getMaximumValue() * boost.m_lifetimeScale,
											 m_lifetime.getDistributionType() );

	// zero means the emitter never stops on its own, and scaling that is still never
	m_systemLifetime = (UnsignedInt)(m_systemLifetime * boost.m_lifetimeScale);

	m_startSize.setRange( m_startSize.getMinimumValue() * boost.m_sizeScale,
												m_startSize.getMaximumValue() * boost.m_sizeScale,
												m_startSize.getDistributionType() );

	// Growth is per frame.  Left alone against a longer life the cloud would swell far past the
	// width the art asked for; slowed by the same factor it reaches the same width, later.
	m_sizeRate.setRange( m_sizeRate.getMinimumValue() / boost.m_lifetimeScale,
											 m_sizeRate.getMaximumValue() / boost.m_lifetimeScale,
											 m_sizeRate.getDistributionType() );

	for( Int keyframe = 0; keyframe < MAX_KEYFRAMES; ++keyframe )
	{
		// Keyframe times are frame numbers counted from the particle's birth, so a particle that
		// lives twice as long against the shipped times spends the extra life at its last key -
		// which for smoke is fully transparent.  The extra life would be invisible.
		m_alphaKey[ keyframe ].frame =
			(UnsignedInt)(m_alphaKey[ keyframe ].frame * boost.m_lifetimeScale);
		m_colorKey[ keyframe ].frame =
			(UnsignedInt)(m_colorKey[ keyframe ].frame * boost.m_lifetimeScale);

		Real low = m_alphaKey[ keyframe ].var.getMinimumValue() * boost.m_alphaScale;
		Real high = m_alphaKey[ keyframe ].var.getMaximumValue() * boost.m_alphaScale;
		if (low > 1.0f) low = 1.0f;
		if (high > 1.0f) high = 1.0f;
		m_alphaKey[ keyframe ].var.setRange( low, high,
																				 m_alphaKey[ keyframe ].var.getDistributionType() );
	}

	if (m_priority < PRIORITY_FLOOR)
		m_priority = PRIORITY_FLOOR;
}

// ------------------------------------------------------------------------------------------------
/** The GPU path takes only what its closed form evaluates (GPU-PARTICLES.md, split point).  Wind,
	* the up-towards-emitter turn and a system on every particle need CPU state each frame; a
	* drawable, a streak, a volume particle, a smudge and the SMUD heat haze are drawn by other code;
	* the fire look and the flipbook read a particle's age in the CPU renderer; ALPHA_TEST and
	* MULTIPLY stay in the CPU's sorted pool.  Ground collision does not refuse a system, as the
	* ParticleBounce option forces it on all of them: the device rests those on the terrain instead
	* (isGpuGroundClamped).  A lifetime of 0 (forever) is refused per spawn.  Slaves and controlled
	* children qualify, being on their own after birth.  A system copies every field read here from
	* its template unchanged, so the template answers for all its systems. */
// ------------------------------------------------------------------------------------------------
Bool ParticleSystemTemplate::isGpuEligible( void ) const
{
	return m_particleType == PARTICLE
		&& !m_particleTypeName.startsWith( "SMUD" )
		&& !m_isGroundAligned
		&& m_windMotion == WIND_MOTION_NOT_USED
		&& m_attachedSystemName.isEmpty()
		&& ( m_shaderType == ALPHA || m_shaderType == ADDITIVE )
		&& !m_fireLook
		&& m_flipbookGridLog2 == 0
		&& !m_isParticleUpTowardsEmitter;
}

// ------------------------------------------------------------------------------------------------
/** If returns non-NULL, it is a slave system for use ... the create slaves parameter
 * tells *this* slave system whether or not it should create any slaves itself
 * automatically during its own constructor */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemTemplate::createSlaveSystem( Bool createSlaves ) const
{
	if (m_slaveTemplate == NULL && m_slaveSystemName.isEmpty() == false)
		m_slaveTemplate = TheParticleSystemManager->findTemplate( m_slaveSystemName );

	ParticleSystem *slave = NULL;
	if (m_slaveTemplate)
		slave = TheParticleSystemManager->createParticleSystem( m_slaveTemplate, createSlaves );

	return slave;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ParticleSystemManager //////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemManager::ParticleSystemManager( void )
{

	m_uniqueSystemID = INVALID_PARTICLE_SYSTEM_ID;

	m_onScreenParticleCount = 0;
	m_localPlayerIndex = 0;
	
	//Added By Sadullah Nader
	//Initializations inserted
	m_lastLogicFrameUpdate = 0;
	m_particleFrame = 0;
	m_particleCount = 0;
	m_fieldParticleCount = 0;
	m_groundShadowCount = 0;
	m_particleSystemCount = 0;
	//

	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{
		
		m_allParticlesHead[ i ] = NULL;
		m_allParticlesTail[ i ] = NULL;

	}  // end for, i

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ParticleSystemManager::~ParticleSystemManager()
{
	reset();

	TemplateMap::iterator begin(m_templateMap.begin());
	TemplateMap::iterator end(m_templateMap.end());
	for (; begin != end; ++begin) {
		(*begin).second->deleteInstance();
	}
}

// ------------------------------------------------------------------------------------------------
/** Initialize the manager */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::init( void )
{
	/// Read INI data and build templates
	INI ini;
	ini.load( AsciiString( "Data\\INI\\ParticleSystem.ini" ), INI_LOAD_OVERWRITE, NULL );
	// -modfactions: each mod's systems after retail's, under its names
	INI::loadModPasses( "Data\\INI\\ParticleSystem.ini", NULL );
	// The fork's own systems, beside EA's 1088 rather than in a loose copy of all of them
	m_classicReadingForkSystems = TheGlobalData && TheGlobalData->m_classicGraphics;
	ini.load( AsciiString( "Data\\INI\\ParticleSystemReforged.ini" ), INI_LOAD_OVERWRITE, NULL );
	m_classicReadingForkSystems = FALSE;

	// "-particlebounce" on the command line: every system hits the terrain, using the built-in
	// defaults.  Without it nothing changes unless a system's INI sets GroundCollision itself.
	if( TheGlobalData && TheGlobalData->m_particleGroundBounce )
	{
		for( TemplateMap::iterator it = m_templateMap.begin(); it != m_templateMap.end(); ++it )
			it->second->forceGroundCollision( 0.35f, ConvertRetailPerFrameFactor( 0.6f ) );
		DEBUG_LOG(("-particlebounce: terrain collision forced on for %d particle systems\n",
							(Int)m_templateMap.size()));
	}

	// "-smoke <thickness>" on the command line: every system with "smoke" in its name burns longer
	// and reads thicker.  Same reason this is here rather than in an INI - the shipped
	// ParticleSystem.ini is inside INIZH.big and a loose copy would have to replace all 1088 of them.
	SmokeBoost smokeBoost;
	if( TheGlobalData && particleSmokeBoostResolve( TheGlobalData->m_smokeThickness, &smokeBoost ) )
	{
		Int boosted = 0;
		for( TemplateMap::iterator it = m_templateMap.begin(); it != m_templateMap.end(); ++it )
		{
			if( particleSmokeNameMatches( it->second->getName().str() ) )
			{
				it->second->forceSmokeBoost( smokeBoost );
				++boosted;
			}
		}

		// The particle ceiling is not reported here on purpose: at this point in startup it is
		// still the built-in default and the player's own slider has not been applied yet.  It is
		// read, and scaled, per particle in createParticle.
		DEBUG_LOG(("-smoke %.2f: %d of %d particle systems boosted, lifetime x%.2f alpha x%.2f "
							 "size x%.2f\n", TheGlobalData->m_smokeThickness, boosted,
							 (Int)m_templateMap.size(), smokeBoost.m_lifetimeScale, smokeBoost.m_alphaScale,
							 smokeBoost.m_sizeScale));
	}

	// sanity, our lists must be empty!!
	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{

		// sanity		
		DEBUG_ASSERTCRASH( m_allParticlesHead[ i ] == NULL, ("INIT: ParticleSystem all particles head[%d] is not NULL!\n", i) );
		DEBUG_ASSERTCRASH( m_allParticlesTail[ i ] == NULL, ("INIT: ParticleSystem all particles tail[%d] is not NULL!\n", i) );

		// just to be clean set them to NULL
		m_allParticlesHead[ i ] = NULL;
		m_allParticlesTail[ i ] = NULL;

	}  // end for, i

}

// ------------------------------------------------------------------------------------------------
/** Reset the manager and all particle systems */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::reset( void )
{
	// systems a tick made in a game it then cleared, never built
	for (size_t i = 0; i < m_shells.size(); ++i)
		m_shells[ i ]->deleteInstance();
	m_shells.clear();

	while (getParticleSystemCount()) {
		if (m_allParticleSystemList.front()) {
			m_allParticleSystemList.front()->deleteInstance();
		}
	}

	// sanity, our lists must be empty!!
	for( Int i = 0; i < NUM_PARTICLE_PRIORITIES; ++i )
	{

		// sanity		
		DEBUG_ASSERTCRASH( m_allParticlesHead[ i ] == NULL, ("RESET: ParticleSystem all particles head[%d] is not NULL!\n", i) );
		DEBUG_ASSERTCRASH( m_allParticlesTail[ i ] == NULL, ("RESET: ParticleSystem all particles tail[%d] is not NULL!\n", i) );

		// just to be clean set them to NULL
		m_allParticlesHead[ i ] = NULL;
		m_allParticlesTail[ i ] = NULL;

	}  // end for, i

	m_particleCount = 0;
	m_fieldParticleCount = 0;
	m_groundShadowCount = 0;
	m_particleSystemCount = 0;
	m_allParticleSystemMap.clear();

	m_uniqueSystemID = INVALID_PARTICLE_SYSTEM_ID;
	
	m_lastLogicFrameUpdate = -1;

	// Templates stay, but not as a map.ini left them: each one it wrote over gets its state from
	// before back, backwards so map.ini and solo.ini on the same system end at the first.  A system the
	// map made from nothing goes back to a fresh template's defaults rather than away, since an FXList
	// that named it may hold it.  The slave is looked up again from the restored name.
	for( Int i = (Int)m_beforeMapOverrides.size() - 1; i >= 0; --i )
	{
		ParticleSystemTemplate *sysTemplate = m_beforeMapOverrides[ i ].first;
		(ParticleSystemInfo &)*sysTemplate = m_beforeMapOverrides[ i ].second;
		sysTemplate->m_slaveTemplate = NULL;
	}
	m_beforeMapOverrides.clear();
}

// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::rememberTemplateBeforeMapINI( ParticleSystemTemplate *sysTemplate )
{
	m_beforeMapOverrides.push_back( std::make_pair( sysTemplate, (const ParticleSystemInfo &)*sysTemplate ) );
}

// ------------------------------------------------------------------------------------------------
/** Update all particle systems */
// ------------------------------------------------------------------------------------------------
/// systems a pool thread claims at once; a system is a few hundred particles at most
static const Int PARTICLE_SYSTEMS_PER_CLAIM = 4;

/** One independent system's particles, on the job pool. */
static void markSystemParticles( Int index, void *context )
{
	ParticleSystem **systems = (ParticleSystem **)context;
	systems[ index ]->updateParticlesMark();
}

DECLARE_PERF_TIMER(ParticleSystemManager)
void ParticleSystemManager::update( void )
{
	if (m_lastLogicFrameUpdate == TheGameLogic->getFrame()) {
		return;
	}

	// update the last logic frame.
	m_lastLogicFrameUpdate = TheGameLogic->getFrame();
	// before the systems, so a spawn of this pass is born on it and aged once by it, like a Particle
	++m_particleFrame;

	USE_PERF_TIMER(ParticleSystemManager)
	ParticleSystem *sys;

	m_groundShadowCount = 0;

	// Three passes.  In list order: a system linked to others updates whole, as it always did, and
	// every other system emits.  On the job pool: those other systems' particles.  In list order
	// again: their dead particles, ground blobs and lifetimes.  The emissions are the only part that
	// draws on the client's random stream and they still draw in list order, so every new particle
	// comes out as it did.  What moved is when a dying particle leaves the manager's count: after all
	// the emissions rather than before the next one, which only matters at the particle cap.  With a
	// hundred thousand particles the particles were most of the update.
	m_independentUpdates.clear();
	for(ParticleSystemListIt it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end();)
	{
		sys = (*it);
		++it;
		if (!sys) {
			continue;
		}

		Bool keepSystem = true;
		if (sys->isUpdateIndependent())
		{
			if (sys->updateEmission(m_localPlayerIndex, &keepSystem))
			{
				m_independentUpdates.push_back( sys );
				continue;
			}
		}
		else
		{
			keepSystem = sys->update(m_localPlayerIndex);
		}

		if (keepSystem == false)
			sys->deleteInstance();
		else if (sys->hasGroundShadow())
			++m_groundShadowCount;
	}

	if (!m_independentUpdates.empty())
	{
		JobSystem::parallel_for( (Int)m_independentUpdates.size(), PARTICLE_SYSTEMS_PER_CLAIM,
			markSystemParticles, &m_independentUpdates[ 0 ] );
	}

	for (size_t i = 0; i < m_independentUpdates.size(); ++i)
	{
		sys = m_independentUpdates[ i ];
		if (sys->reapAndFinishUpdate() == false)
			sys->deleteInstance();
		else if (sys->hasGroundShadow())
			++m_groundShadowCount;
	}
	m_independentUpdates.clear();

	// The one number that says what a particle switch actually did, and the only way to compare two
	// runs at all: two launches of the same seed and the same scenario do not produce the same
	// frame, so a pixel difference between their screenshots measures the divergence rather than
	// the change.  Grep the log for PARTICLES.
	const UnsignedInt REPORT_INTERVAL = 100;
	if (m_lastLogicFrameUpdate % REPORT_INTERVAL == 0)
	{
		DEBUG_LOG(("PARTICLES frame %d: %d particles in %d systems, %d ground shadows\n",
							 m_lastLogicFrameUpdate, m_particleCount, m_particleSystemCount,
							 m_groundShadowCount));
	}
}

// ------------------------------------------------------------------------------------------------
/** sets the count of the particles on screen after each frame */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::setOnScreenParticleCount(int count)
{
	m_onScreenParticleCount = count;
}

// ------------------------------------------------------------------------------------------------
/** Given a file containing particle system properties, create a new instance of it */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemManager::createParticleSystem( const ParticleSystemTemplate *sysTemplate, Bool createSlaves )
{
	// sanity
	if (sysTemplate == NULL)
		return NULL;

	// The tick's shells and the render's own systems (a model state's bone systems, a truck's dust)
	// take ids at the same time while the two overlap.  Particle ids are client handles; no logic CRC
	// reads them.
	// ponytail: one global lock on the counter, uncontended outside the overlap; an atomic if it ever shows in a profile
	static std::mutex s_idLock;
	ParticleSystemID id;
	{
		std::lock_guard<std::mutex> lock( s_idLock );
		m_uniqueSystemID = (ParticleSystemID)((UnsignedInt)m_uniqueSystemID + 1);
		id = m_uniqueSystemID;
	}
	ParticleSystem *sys = newInstance(ParticleSystem)( sysTemplate, id, createSlaves );
	return sys;
}

// ------------------------------------------------------------------------------------------------
/// given a template, instantiate a particle system attached to the given object, and return its ID
// ------------------------------------------------------------------------------------------------
ParticleSystemID ParticleSystemManager::createAttachedParticleSystemID( 
																			const ParticleSystemTemplate *sysTemplate,
																			Object* attachTo,
																			Bool createSlaves )
{
	ParticleSystem* pSystem = TheParticleSystemManager->createParticleSystem(sysTemplate, createSlaves);
	if (pSystem && attachTo)
		pSystem->attachToObject(attachTo);
	return pSystem ? pSystem->getSystemID() : INVALID_PARTICLE_SYSTEM_ID;
}

// ------------------------------------------------------------------------------------------------
/** Find a particle system with the matching system id  */
// ------------------------------------------------------------------------------------------------
ParticleSystem *ParticleSystemManager::findParticleSystem( ParticleSystemID id )
{
	if (id == INVALID_PARTICLE_SYSTEM_ID)
		return NULL;	// my, that was easy

	// A system this tick made is the tick's own until it is built, and the logic thread fills the list
	// beside the render: the main thread inside the window looks only at what it made there itself.
	if (LogicThread::isInsideWindow())
	{
		for (size_t i = 0; i < m_windowSystems.size(); ++i)
			if (m_windowSystems[ i ]->getSystemID() == id)
				return m_windowSystems[ i ];
	}
	else
	{
		for (size_t i = 0; i < m_shells.size(); ++i)
			if (m_shells[ i ]->getSystemID() == id)
				return m_shells[ i ];
	}

	LOGIC_CENSUS( CENSUS_PARTICLES );
	ParticleSystemIDMap::const_iterator it = m_allParticleSystemMap.find( id );
	return (it != m_allParticleSystemMap.end()) ? it->second : NULL;

}  // end findParticleSystem

// ------------------------------------------------------------------------------------------------
/** destroy the particle system with the given id (if it still exists) */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::destroyParticleSystemByID(ParticleSystemID id)
{
	withSystem( id, []( ParticleSystem *pSystem ) { pSystem->destroy(); } );
}

// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::withSystem( ParticleSystemID id, const std::function<void( ParticleSystem * )> &work )
{
	if( id == INVALID_PARTICLE_SYSTEM_ID )
		return;

	// after the tick, in order with the systems it built (LOGIC-THREAD.md M5)
	LogicThread::callAfterTick( [this, id, work]() {
		ParticleSystem* pSystem = findParticleSystem(id);
		if( pSystem )
			work( pSystem );
	} );
}

// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_addShell( ParticleSystem *shell, Bool createSlaves )
{
	m_shells.push_back( shell );
	const ParticleSystemID id = shell->getSystemID();
	LogicThread::callAfterTick( [this, shell, id, createSlaves]() {
		// gone if a reset inside the tick deleted it; by the ID, as a new shell can take its address
		std::vector<ParticleSystem *>::iterator it = std::find( m_shells.begin(), m_shells.end(), shell );
		if( it == m_shells.end() || shell->getSystemID() != id )
			return;
		m_shells.erase( it );
		shell->buildClientPart( createSlaves );
	} );
}

// ------------------------------------------------------------------------------------------------
/** Locate an existing ParticleSystemTemplate */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::findTemplate( const AsciiString &name ) const
{
	ParticleSystemTemplate *sysTemplate = NULL;

	TemplateMap::const_iterator find(m_templateMap.find(name));
	if (find != m_templateMap.end()) {
		sysTemplate = (*find).second;
	}
	else {
		AsciiString retailName;
		if (TheModNamespace.findRetailName(name, MODNAME_PARTICLESYSTEM, &retailName))
			sysTemplate = findTemplate(retailName);
	}

	return sysTemplate;
}

// ------------------------------------------------------------------------------------------------
/** Create a new ParticleSystemTemplate */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::newTemplate( const AsciiString &name )
{
	ParticleSystemTemplate *sysTemplate = findTemplate(name);
	if (sysTemplate == NULL) {
		sysTemplate = newInstance(ParticleSystemTemplate)( name );

		if (! m_templateMap.insert(std::make_pair(name, sysTemplate)).second) {
			sysTemplate->deleteInstance();
			sysTemplate = NULL;
		}
	}

	return sysTemplate;
}

// ------------------------------------------------------------------------------------------------
/** Find a particle system's parent. Should really only be called by TheScriptEngine */
// ------------------------------------------------------------------------------------------------
ParticleSystemTemplate *ParticleSystemManager::findParentTemplate( const AsciiString &name, Int parentNum ) const
{
	if (name.isEmpty()) {
		return NULL;
	}

	TemplateMap::const_iterator begin(m_templateMap.begin());
	TemplateMap::const_iterator end(m_templateMap.end());
	for(; begin != end; ++begin) {
		ParticleSystemTemplate *sysTemplate = (*begin).second;
		if (name.compare(sysTemplate->m_slaveSystemName) == 0) {
			if (! parentNum--) {
				return sysTemplate;
			}
		}
	}

	return NULL;	
}

// ------------------------------------------------------------------------------------------------
/** Destroy any particle systems that are attached to this object */	
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::destroyAttachedSystems( Object *obj )
{

	// sanity
	if( obj == NULL )
		return;

	// after the tick, so the systems it attached to the object are built and among them (M5)
	const ObjectID objID = obj->getID();
	LogicThread::callAfterTick( [this, objID]() {
		LOGIC_CENSUS( CENSUS_PARTICLES );

		// iterate through all systems
		ParticleSystem *system = NULL;

		for( ParticleSystemListIt it = m_allParticleSystemList.begin();
				 it != m_allParticleSystemList.end();
				 ++it )
		{

			system = *it;
			if( system == NULL )
				continue;

			if( system->getAttachedObject() == objID )
				system->destroy();

		}
	} );

}

// ------------------------------------------------------------------------------------------------
/** Add a particle to the global particle list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::addParticle( Particle *particleToAdd, ParticlePriorityType priority )
{
	if (particleToAdd->m_inOverallList)
		return;

	if (!m_allParticlesHead[ priority ])
	{
		m_allParticlesHead[ priority ] = particleToAdd;
	}

	if (m_allParticlesTail[ priority ])
	{
		m_allParticlesTail[ priority ]->m_overallNext = particleToAdd;
		particleToAdd->m_overallPrev = m_allParticlesTail[ priority ];
	}
	else
	{
		particleToAdd->m_overallPrev = NULL;
	}

	m_allParticlesTail[ priority ] = particleToAdd;
	particleToAdd->m_overallNext = NULL;
	particleToAdd->m_inOverallList = TRUE;

	++m_particleCount;
	

}

// ------------------------------------------------------------------------------------------------
/** Remove a particle from the global particle list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::removeParticle( Particle *particleToRemove)
{
	if (!particleToRemove->m_inOverallList)
		return;

	// get priority of the particle we're removing
	ParticlePriorityType priority = particleToRemove->getPriority();

	// remove links from prev & next objs
	if (particleToRemove->m_overallNext)
		particleToRemove->m_overallNext->m_overallPrev = particleToRemove->m_overallPrev;
	if (particleToRemove->m_overallPrev)
		particleToRemove->m_overallPrev->m_overallNext = particleToRemove->m_overallNext;

	// update head & tail if neccessary
	if (particleToRemove == m_allParticlesHead[ priority ])
		m_allParticlesHead[ priority ] = particleToRemove->m_overallNext;
	if (particleToRemove == m_allParticlesTail[ priority ])
		m_allParticlesTail[ priority ] = particleToRemove->m_overallPrev;

	particleToRemove->m_overallNext = particleToRemove->m_overallPrev = NULL;
	particleToRemove->m_inOverallList = FALSE;
	--m_particleCount;


}

// ------------------------------------------------------------------------------------------------
/** Add a particle system to the master particle system list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_addParticleSystem( ParticleSystem *particleSystemToAdd )
{
	// the tick beside the render reads the list and the lookup; filed once the window closes
	if (LogicThread::isInsideWindow())
	{
		m_windowSystems.push_back(particleSystemToAdd);
		return;
	}
	m_allParticleSystemList.push_back(particleSystemToAdd);
	particleSystemToAdd->m_managerListIt = --m_allParticleSystemList.end();
	m_allParticleSystemMap[ particleSystemToAdd->getSystemID() ] = particleSystemToAdd;
	++m_particleSystemCount;
}

// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_fileWindowSystems( void )
{
	// outside the window now, so each one files itself as it would have at its build, in the order made
	for (size_t i = 0; i < m_windowSystems.size(); ++i)
		friend_addParticleSystem( m_windowSystems[ i ] );
	m_windowSystems.clear();
}

// ------------------------------------------------------------------------------------------------
/** Remove a particle system from the master particle system list. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_removeParticleSystem( ParticleSystem *particleSystemToRemove )
{
	// The constructor filed it and only the destructor calls this, so the iterator is ours and
	// still good; EA scanned the whole list for it on every single system that died.
	std::vector<ParticleSystem *>::iterator unfiled = std::find( m_windowSystems.begin(), m_windowSystems.end(), particleSystemToRemove );
	if (unfiled != m_windowSystems.end())
	{
		m_windowSystems.erase( unfiled );
		return;
	}
	m_allParticleSystemList.erase( particleSystemToRemove->m_managerListIt );
	m_allParticleSystemMap.erase( particleSystemToRemove->getSystemID() );
	--m_particleSystemCount;
}

// ------------------------------------------------------------------------------------------------
/** A system loaded from a save file is constructed with a fresh id and then reads its saved one,
 * so the id it was filed under is no longer the id it answers to.  Re-file it. */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::friend_renumberParticleSystem( ParticleSystem *particleSystem, ParticleSystemID oldID )
{
	ParticleSystemIDMap::iterator it = m_allParticleSystemMap.find( oldID );
	if (it != m_allParticleSystemMap.end() && it->second == particleSystem)
		m_allParticleSystemMap.erase( it );

	m_allParticleSystemMap[ particleSystem->getSystemID() ] = particleSystem;
}

// ------------------------------------------------------------------------------------------------
/** Remove the oldest N number of particles from the lowest priority lists first.  We will
 * not remove particles from any priorities higher or equal to the priorityCap parameter. */
// ------------------------------------------------------------------------------------------------
Int ParticleSystemManager::removeOldestParticles( UnsignedInt count, 
																									ParticlePriorityType priorityCap )
{
	Int countToRemove = count;

	while (count-- && getParticleCount()) 
	{
		for( Int i = PARTICLE_PRIORITY_LOWEST;
				 i < priorityCap;
				 ++i )
		{
			if( m_allParticlesHead[ i ] ) 
			{
				m_allParticlesHead[ i ]->deleteInstance();
				break;  // exit for
			}
		}
	}

	// return the number of particles actually removed
	return countToRemove - count;

}

// ------------------------------------------------------------------------------------------------
/** Preload particle system textures */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::preloadAssets( TimeOfDay timeOfDay )
{
	TemplateMap::iterator begin(m_templateMap.begin());
	TemplateMap::iterator end(m_templateMap.end());

	for (; begin != end; ++begin) {
		const ParticleSystemTemplate *tmplate = (*begin).second;
		if (tmplate->m_particleType == ParticleSystemInfo::PARTICLE &&
			 	(! tmplate->m_particleTypeName.isEmpty()))
		{
			TheDisplay->preloadTextureAssets(tmplate->m_particleTypeName);
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::crc( Xfer *xfer )
{

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// unique system ID counter
	xfer->xferUser( &m_uniqueSystemID, sizeof( ParticleSystemID ) );

	// count of particle systems in the world
	UnsignedInt systemCount = m_particleSystemCount;
	xfer->xferUnsignedInt( &systemCount );

	// particle systems data
	AsciiString systemName;
	ParticleSystem *system;
	if( xfer->getXferMode() == XFER_SAVE )
	{

		// iterate each particle system
		ParticleSystemListIt it;
		for( it = m_allParticleSystemList.begin(); it != m_allParticleSystemList.end(); ++it )
		{
			systemCount--;
			// get system
			system = *it;

			// ignore destroyed systems and non-saveable systems
			if( system->isDestroyed() == TRUE || system->isSaveable() == FALSE )	{
				AsciiString mtString = "";
				xfer->xferAsciiString(&mtString); // write null string as key for destroyed system.
				continue;
			}

			// write template name
			systemName = system->getTemplate()->getName();
			xfer->xferAsciiString( &systemName );

			// write system data
			xfer->xferSnapshot( system );

		}  // end for, it
		DEBUG_ASSERTCRASH(systemCount==0, ("Mismatch in write count."));

	}  // end if, save
	else
	{
		const ParticleSystemTemplate *systemTemplate;

		// read each particle system
		for( UnsignedInt i = 0; i < systemCount; ++i )
		{

			// read system name and find template
			xfer->xferAsciiString( &systemName );
			if (systemName.isEmpty()) {
				continue; // destroyed particle system.
			}
			systemTemplate = findTemplate( systemName );

			// sanity
			if( systemTemplate == NULL )
			{

				DEBUG_CRASH(( "ParticleSystemManager::xfer - Unknown particle system template '%s'\n",
											systemName.str() ));
				throw SC_INVALID_DATA;

			}  // end if

			// Create the system without an id and let the saved one come back first.  It used to be
			// created through createParticleSystem, which hands out the next id in sequence and
			// files the system under it - and then the xfer below overwrote the id with the saved
			// one, leaving the manager's index pointing at the wrong system.  Every lookup by id
			// after a load missed: a master could not find its slaves.
			system = newInstance(ParticleSystem)( systemTemplate, INVALID_PARTICLE_SYSTEM_ID, FALSE );

			if( system == NULL )
			{

				DEBUG_CRASH(( "ParticleSystemManager::xfer - Unable to allocate particle system '%s'\n",
											systemName.str() ));
				throw SC_INVALID_DATA;

			}  // end if

			// read system data - this is what restores the id
			xfer->xferSnapshot( system );

			if( system->getSystemID() == INVALID_PARTICLE_SYSTEM_ID )
			{

				DEBUG_CRASH(( "ParticleSystemManager::xfer - No system id restored for particle system '%s'\n",
											systemName.str() ));
				system->deleteInstance();
				throw SC_INVALID_DATA;

			}  // end if

			friend_addParticleSystem( system );

		}  // end for, i

	}  // end else, load

}  // end particleSystemManager

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ParticleSystemManager::loadPostProcess( void )
{

}  // end loadPostProcess

// ------------------------------------------------------------------------------------------------
/** Output particle system statistics to the screen
 * @todo Implement a real console (MSB) */
// ------------------------------------------------------------------------------------------------
void ParticleSystemDebugDisplay( DebugDisplayInterface *dd, void *, FILE *fp )
{
	if (!dd)
		return;

	dd->setCursorPos( 0, 0 );
	dd->setRightMargin( 2 );
	
	dd->printf( "Total Particles: %d\n", TheParticleSystemManager->getParticleCount() );
	dd->printf( "Total Particles (On Screen): %d\n", TheParticleSystemManager->getOnScreenParticleCount());
	dd->printf( "Total Particle Systems: %d\n", TheParticleSystemManager->getParticleSystemCount() );

	ParticleSystemManager::ParticleSystemList list = TheParticleSystemManager->getAllParticleSystems();
	ParticleSystemManager::ParticleSystemList::iterator it;

	std::map<AsciiString, Int> templateMap;
	std::map<AsciiString, Int> templateMapParticleCount;

	std::map<AsciiString, Int>::iterator templateMapIt;
	std::map<AsciiString, Int>::iterator templateMapParticleCountIt;

	for ( it = list.begin(); it != list.end(); ++it )
	{
		AsciiString templateName = (*it)->getTemplate()->getName();
		templateMapIt = templateMap.find(templateName);
		if (templateMapIt == templateMap.end())
		{
			templateMap.insert(std::make_pair(templateName, 1));
			templateMapParticleCount.insert(std::make_pair(templateName, (*it)->getParticleCount()));
		}
		else
		{
			++templateMapIt->second;

			templateMapParticleCountIt = templateMapParticleCount.find(templateName);
			if (templateMapParticleCountIt != templateMapParticleCount.end())
				templateMapParticleCountIt->second += (*it)->getParticleCount();
		}
	}

	for (templateMapIt = templateMap.begin(); templateMapIt != templateMap.end(); ++templateMapIt)
	{
		templateMapParticleCountIt = templateMapParticleCount.find(templateMapIt->first);

		dd->printf("  %s: %d instances", templateMapIt->first.str(), templateMapIt->second);
		if (templateMapParticleCountIt != templateMapParticleCount.end())
			dd->printf("  (Avg per system %.2f)", INT_TO_REAL(templateMapParticleCountIt->second) / templateMapIt->second);
		dd->printf("\n");
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static Real angleBetween(const Coord2D *vecA, const Coord2D *vecB)
{
	if (!vecA || !vecB) {
		return 0.0f;
	}

	// Each length was a square root, and the guard above computed both of them and then threw
	// them away so the two lines below could compute them again.
	const Real lengthA = vecA->length();
	const Real lengthB = vecB->length();

	if (lengthA == 0.0f || lengthB == 0.0f) {
		return 0.0f;
	}

	Real dotProduct = (vecA->x * vecB->x + vecA->y * vecB->y);
	Real cosTheta = dotProduct / (lengthA * lengthB);

	// If the dot product is 0.0, then they are orthogonal
	if (dotProduct == 0.0f) {
		if (vecB->x > 0) {
			return PI;
		}

		return 0.0f;
	}

	Real theta = ACos( cosTheta );

	if (vecB->x > 0) {
		return theta;
	}
	
	return -theta;
}

