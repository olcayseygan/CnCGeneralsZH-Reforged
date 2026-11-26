// Aircraft effects the data cannot key (AircraftEffects.h).  EA's aircraft hang their particle
// systems on model condition states, which know damage, exhaust and afterburner but not how fast a
// jet flies, how hard it turns, how high it is or what lies under it, and the Object INI that holds
// those states is in the multiplayer checksum.  This walks the aircraft once a logic frame, works
// those out from the object's own position and orientation, and places systems from
// ParticleSystemReforged.ini (outside the checksum) by name: a name the data lacks places nothing.
//
// Thread: LOGIC-THREAD.md.  It runs from GameClient::update after the drawables' walk, at the sync
// point, where that walk reads the objects too; inside the overlap window it does nothing.  It only
// reads, places client particle systems, light pulses and scorches, and draws its chances from the
// client's random stream, so no logic state and no CRC moves.  Classic graphics and -headless skip it.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/AircraftEffects.h"

#include <map>
#include <string.h>

#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/LogicThread.h"
#include "Common/ModelState.h"
#include "Common/TerrainTypes.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

namespace
{
	// speeds in world units a game second, turn and pitch rates in radians a game second
	const Real SUPERSONIC_SPEED = 215.0f;		///< the Aurora's attack run (480); every other jet cruises at 175 or less
	const Real SUBSONIC_SPEED = 200.0f;			///< and back under this before it can cross again
	const Real VORTEX_TURN = 1.0f;					///< a turn this hard draws the wingtip ribbons (TurnRate 120 to 180 degrees is 2.1 to 3.1)
	const Real VORTEX_MIN_SPEED = 90.0f;
	const UnsignedInt VORTEX_HOLD = 6;			///< logic frames the ribbons stay after the turn eases
	// 1.8 and a 0.6 pull-out never fired in the filmed dogfights of 2026-11-27; AIRFX lines log the peaks
	const Real SHEET_TURN = 1.2f;						///< harder still, or a pull-out, puts vapour over the wing
	const Real SHEET_PULL = 0.3f;
	const Real SHEET_MIN_SPEED = 100.0f;
	const UnsignedInt CONE_FRAMES = 20;			///< the vapour cone wraps the airframe this long after the crossing
	const Real BOOM_HEIGHT = 220.0f;				///< a supersonic pass lower than this raises the ground's dust
	const Real SPRAY_HEIGHT = 140.0f;				///< a pass over water lower than this throws spray; jets cruise at 100
	const Real SPRAY_MIN_SPEED = 120.0f;
	const Real GROUND_HEIGHT = 4.0f;				///< on the ground, rolling
	const Real AIRBORNE_HEIGHT = 8.0f;
	const Real ROLL_MIN_SPEED = 45.0f;			///< a takeoff roll fast enough to blow the tarmac's dust
	const Real CONTRAIL_HEIGHT = 40.0f;
	const Real WASH_HEIGHT = 140.0f;				///< a helicopter lower than this over its ground blows it about; they cruise at 100
	const Real BARREL_HOT_SECONDS = 2.0f;		///< a burst this long heats the barrels
	const Real BARREL_COOL_SECONDS = 2.5f;	///< and they shimmer this long after it
	const UnsignedInt BURST_GAP = 12;				///< logic frames between shots that still count as one burst

	struct AirState
	{
		Coord3D pos;
		Real heading, pitch, speed;
		UnsignedInt at;			///< the logic frame pos was read on
		Bool known;
		UnsignedInt seen;
		Bool supersonic;
		UnsignedInt coneUntil;
		UnsignedInt turnedAt;
		UnsignedInt airborneAt;
		Bool touchedDown;
		Bool scorched;
		UnsignedInt burstStart, lastShot, heatUntil;
		ParticleSystemID vortex[ 2 ], contrail[ 2 ], cone, damaged, fall, heat, trail;
		Coord3D tip[ 2 ];
		Bool tipsKnown;
		Real peakTurn, peakPull, peakSpeed;	///< since the last AIRFX line, for tuning the thresholds above
	};

	typedef std::map< DrawableID, AirState > AirMap;
	AirMap s_air;
	UnsignedInt s_lastFrame = 0;

	ParticleSystem *create( const char *name )
	{
		const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate( name );
		return tmp ? TheParticleSystemManager->createParticleSystem( tmp ) : NULL;
	}

	/// a one-off system placed in the world
	void spawn( const char *name, const Coord3D &pos )
	{
		ParticleSystem *sys = create( name );
		if( sys )
		{
			sys->setPosition( &pos );
			sys->setSaveable( FALSE );
		}
	}

	/// a system riding the drawable at a point of its model, kept by id until released
	void hold( ParticleSystemID &id, const char *name, Drawable *draw, const Coord3D &local )
	{
		if( id != INVALID_PARTICLE_SYSTEM_ID && TheParticleSystemManager->findParticleSystem( id ) )
			return;
		id = INVALID_PARTICLE_SYSTEM_ID;
		ParticleSystem *sys = create( name );
		if( sys == NULL )
			return;
		sys->setPosition( &local );
		sys->attachToDrawable( draw );
		sys->setSaveable( FALSE );
		id = sys->getSystemID();
	}

	/// stop it emitting; what it already laid lives out its life
	void release( ParticleSystemID &id )
	{
		if( id == INVALID_PARTICLE_SYSTEM_ID )
			return;
		if( ParticleSystem *sys = TheParticleSystemManager->findParticleSystem( id ) )
			sys->destroy();
		id = INVALID_PARTICLE_SYSTEM_ID;
	}

	void releaseAll( AirState &s )
	{
		release( s.vortex[ 0 ] );
		release( s.vortex[ 1 ] );
		release( s.contrail[ 0 ] );
		release( s.contrail[ 1 ] );
		release( s.cone );
		release( s.damaged );
		release( s.fall );
		release( s.heat );
		release( s.trail );
	}

	/// the ground's surface under a point, and whether it is water
	Real surfaceUnder( Real x, Real y, Bool &water )
	{
		const Real ground = TheTerrainLogic->getGroundHeight( x, y );
		Real waterZ = ground;
		water = TheTerrainLogic->isUnderwater( x, y, &waterZ ) && waterZ > ground;
		return water ? waterZ : ground;
	}

	/// the name with the ground's kind on it: <base>Water, Snow, Sand, Grass or Dirt
	const char *byGround( char *buffer, size_t size, const char *base, Real x, Real y, Bool water )
	{
		const char *kind = "Dirt";
		if( water )
			kind = "Water";
		else if( TerrainType *tile = TheTerrainVisual ? TheTerrainVisual->getTerrainTile( x, y ) : NULL )
		{
			switch( tile->getClass() )
			{
				case TERRAIN_SNOW_1: case TERRAIN_SNOW_2: case TERRAIN_SNOW_3: case TERRAIN_RUGGED_SNOW: case TERRAIN_FLAT_SNOW:
					kind = "Snow"; break;
				case TERRAIN_DESERT_1: case TERRAIN_DESERT_2: case TERRAIN_DESERT_3: case TERRAIN_SAND: case TERRAIN_LIVE_DESERT:
				case TERRAIN_DRY_DESERT: case TERRAIN_ACCENT_SAND: case TERRAIN_TROPICAL_BEACH: case TERRAIN_BEACH_PARK:
					kind = "Sand"; break;
				case TERRAIN_GRASS: case TERRAIN_SWISS_1: case TERRAIN_SWISS_2: case TERRAIN_SWISS_3: case TERRAIN_EASTERN_EUROPE_1:
				case TERRAIN_EASTERN_EUROPE_2: case TERRAIN_EASTERN_EUROPE_3: case TERRAIN_COBBLESTONE_GRASS: case TERRAIN_ACCENT_GRASS:
				case TERRAIN_FIELD: case TERRAIN_WOOD:
					kind = "Grass"; break;
				default:
					break;
			}
		}
		snprintf( buffer, size, "%s%s", base, kind );
		return buffer;
	}

	Real wrapAngle( Real a )
	{
		while( a > PI ) a -= 2.0f * PI;
		while( a < -PI ) a += 2.0f * PI;
		return a;
	}

	/// a point in the model's own space, along its nose (x), left wing (y) and up (z)
	Coord3D local( Real x, Real y, Real z )
	{
		Coord3D c;
		c.set( x, y, z );
		return c;
	}

	/// the same point in the world
	Coord3D toWorld( const Object *obj, const Coord3D &l )
	{
		const Matrix3D *m = obj->getTransformMatrix();
		Vector3 v;
		Matrix3D::Transform_Vector( *m, Vector3( l.x, l.y, l.z ), &v );
		Coord3D c;
		c.set( v.X, v.Y, v.Z );
		return c;
	}

	void findTips( Drawable *draw, const Object *obj, AirState &s )
	{
		if( s.tipsKnown )
			return;
		s.tipsKnown = TRUE;
		Coord3D bones[ 2 ];
		if( draw->getPristineBonePositions( "WINGTIP", 1, bones, NULL, 2 ) == 2 )
		{
			s.tip[ 0 ] = bones[ 0 ];
			s.tip[ 1 ] = bones[ 1 ];
			return;
		}
		const Real r = obj->getGeometryInfo().getMajorRadius();
		s.tip[ 0 ] = local( -0.3f * r, 0.7f * r, 0.0f );
		s.tip[ 1 ] = local( -0.3f * r, -0.7f * r, 0.0f );
	}

	Bool nameHas( const Object *obj, const char *part )
	{
		return strstr( obj->getTemplate()->getName().str(), part ) != NULL;
	}

	void updateJet( Drawable *draw, Object *obj, AirState &s, UnsignedInt now, Real turnRate, Real pitchRate,
		Real speed, Real accel, Real height, Bool water, Real surface )
	{
		const Coord3D *pos = obj->getPosition();
		const Real r = obj->getGeometryInfo().getMajorRadius();
		const Bool dead = obj->isEffectivelyDead();
		char name[ 64 ];
		findTips( draw, obj, s );

		if( height > AIRBORNE_HEIGHT )
		{
			s.airborneAt = now;
			s.touchedDown = FALSE;
		}

		// wingtip vortices, only in a hard turn (EA's always-on JetContrail is switched off in the data)
		if( !dead && height > 20.0f && speed > VORTEX_MIN_SPEED && fabs( turnRate ) > VORTEX_TURN )
			s.turnedAt = now;
		if( s.turnedAt != 0 && now - s.turnedAt <= VORTEX_HOLD )
		{
			hold( s.vortex[ 0 ], "ReforgedWingtipVortex", draw, s.tip[ 0 ] );
			hold( s.vortex[ 1 ], "ReforgedWingtipVortex", draw, s.tip[ 1 ] );
		}
		else
		{
			release( s.vortex[ 0 ] );
			release( s.vortex[ 1 ] );
		}

		// vapour over the wing in a harder turn or a pull-out, every frame
		if( !dead && height > 20.0f && speed > SHEET_MIN_SPEED
				&& ( fabs( turnRate ) > SHEET_TURN || pitchRate > SHEET_PULL ) )
		{
			for( Int side = 0; side < 2; ++side )
			{
				const Coord3D mid = local( s.tip[ side ].x * 0.5f, s.tip[ side ].y * 0.5f, s.tip[ side ].z + 1.0f );
				spawn( "ReforgedWingVapour", toWorld( obj, mid ) );
			}
		}

		// the supersonic crossing: the vapour cone, then dust or spray under a low pass
		if( !s.supersonic && speed >= SUPERSONIC_SPEED && !dead )
		{
			s.supersonic = TRUE;
			s.coneUntil = now + CONE_FRAMES;
		}
		else if( s.supersonic && speed < SUBSONIC_SPEED )
			s.supersonic = FALSE;
		if( now < s.coneUntil )
			hold( s.cone, "ReforgedVapourCone", draw, local( 0.15f * r, 0.0f, 0.0f ) );
		else
			release( s.cone );
		if( s.supersonic && height < BOOM_HEIGHT )
		{
			Coord3D under = *pos;
			under.z = surface + 1.0f;
			spawn( byGround( name, sizeof( name ), "ReforgedSonicBoom", pos->x, pos->y, water ), under );
		}

		// a low pass over water throws spray behind it
		if( water && !dead && height < SPRAY_HEIGHT && speed > SPRAY_MIN_SPEED )
		{
			Coord3D behind = toWorld( obj, local( -r, 0.0f, 0.0f ) );
			behind.z = surface + 0.5f;
			spawn( "ReforgedJetSprayWater", behind );
		}

		// the runway: tyre smoke on touchdown, the tarmac's dust blown back on the takeoff roll
		if( !dead && height < GROUND_HEIGHT )
		{
			if( !s.touchedDown && s.airborneAt != 0 && now - s.airborneAt < LOGICFRAMES_PER_SECOND && speed > 30.0f )
			{
				s.touchedDown = TRUE;
				spawn( "ReforgedTyreSmoke", toWorld( obj, local( -0.2f * r, 0.15f * r, -1.0f ) ) );
				spawn( "ReforgedTyreSmoke", toWorld( obj, local( -0.2f * r, -0.15f * r, -1.0f ) ) );
			}
			if( speed > ROLL_MIN_SPEED && accel > 0.0f && ( now & 1 ) == 0 )
			{
				Coord3D behind = toWorld( obj, local( -1.2f * r, 0.0f, 0.0f ) );
				behind.z = surface + 0.5f;
				spawn( byGround( name, sizeof( name ), "ReforgedRunwayBlast", behind.x, behind.y, FALSE ), behind );
			}
		}

		// the grey smoke of a hurt jet, the step below EA's REALLYDAMAGED black
		const ModelConditionFlags &flags = draw->getModelConditionFlags();
		if( !dead && height > AIRBORNE_HEIGHT && flags.test( MODELCONDITION_DAMAGED ) && !flags.test( MODELCONDITION_REALLY_DAMAGED ) )
			hold( s.damaged, "ReforgedJetDamagedSmokeGrey", draw, local( -0.8f * r, 0.0f, 0.0f ) );
		else
			release( s.damaged );

		// the fall: a smoke line off one wingtip, which the jet's own roll winds into a spiral, parts
		// breaking off, and a furrow of scorches where it hits
		if( dead )
		{
			if( height > GROUND_HEIGHT )
			{
				hold( s.fall, "ReforgedJetFallSmoke", draw, s.tip[ 0 ] );
				if( now % 10 == 0 )
					spawn( "ReforgedJetFallDebris", *pos );
			}
			else if( !s.scorched )
			{
				s.scorched = TRUE;
				release( s.fall );
				const Matrix3D *m = obj->getTransformMatrix();
				Vector3 ahead = m->Get_X_Vector();
				ahead.Z = 0.0f;
				if( ahead.Length2() > 0.0f )
					ahead.Normalize();
				for( Int i = 0; i < 3; ++i )
				{
					Coord3D at = *pos;
					at.x -= ahead.X * r * ( 0.9f * i );
					at.y -= ahead.Y * r * ( 0.9f * i );
					TheGameClient->addScorch( &at, r * ( 0.55f - 0.1f * i ), SCORCH_1 );
				}
			}
		}

		// the Aurora's own contrail, high and straight; the built jets keep none in level flight
		if( !dead && nameHas( obj, "Aurora" ) && height > CONTRAIL_HEIGHT && speed > 60.0f )
		{
			hold( s.contrail[ 0 ], "ReforgedAuroraContrail", draw, s.tip[ 0 ] );
			hold( s.contrail[ 1 ], "ReforgedAuroraContrail", draw, s.tip[ 1 ] );
		}
		else
		{
			release( s.contrail[ 0 ] );
			release( s.contrail[ 1 ] );
		}
	}

	void updateHelicopter( Object *obj, AirState &s, UnsignedInt now, Real height, Bool water, Real surface )
	{
		(void)s;
		if( obj->isEffectivelyDead() || height >= WASH_HEIGHT || height < -2.0f )
			return;
		// thicker the lower it hangs; a sitting one blows hardest
		if( GameClientRandomValueReal( 0.0f, WASH_HEIGHT ) < height )
			return;
		const Coord3D *pos = obj->getPosition();
		Coord3D under = *pos;
		under.z = surface + 1.0f;
		char name[ 64 ];
		spawn( byGround( name, sizeof( name ), "ReforgedRotorWash", pos->x, pos->y, water ), under );
		// over water, a fine ring of condensation off the rotor tips now and then
		if( water && now % 6 == 0 )
		{
			Coord3D rotor = *pos;
			rotor.z += 4.0f;
			spawn( "ReforgedRotorTipRing", rotor );
		}
	}

	/// a Comanche's or a Spectre's gun after a long burst: an invisible heat source at the muzzle
	void updateBarrels( Drawable *draw, Object *obj, AirState &s, UnsignedInt now )
	{
		if( !nameHas( obj, "Comanche" ) && !nameHas( obj, "SpectreGunship" ) )
			return;
		const ModelConditionFlags &flags = draw->getModelConditionFlags();
		if( flags.test( MODELCONDITION_FIRING_A ) || flags.test( MODELCONDITION_FIRING_B ) )
		{
			if( s.lastShot == 0 || now - s.lastShot > BURST_GAP )
				s.burstStart = now;
			s.lastShot = now;
			if( now - s.burstStart >= (UnsignedInt)( BARREL_HOT_SECONDS * LOGICFRAMES_PER_GAME_SECOND ) )
				s.heatUntil = now + (UnsignedInt)( BARREL_COOL_SECONDS * LOGICFRAMES_PER_GAME_SECOND );
		}
		if( now < s.heatUntil )
		{
			Coord3D muzzle;
			if( draw->getPristineBonePositions( "MUZZLE", 1, &muzzle, NULL, 1 ) != 1 )
				muzzle = local( 0.7f * obj->getGeometryInfo().getMajorRadius(), 0.0f, -2.0f );
			hold( s.heat, "ReforgedBarrelHeat", draw, muzzle );
		}
		else
			release( s.heat );
	}
}

void AircraftEffects_reset( void )
{
	s_air.clear();
	s_lastFrame = 0;
}

void AircraftEffects_update( void )
{
	if( TheGlobalData == NULL || TheGlobalData->m_classicGraphics || TheGlobalData->m_headless
			|| TheGameLogic == NULL || TheGameClient == NULL || TheParticleSystemManager == NULL || TheTerrainLogic == NULL
			|| LogicThread::isInsideWindow() )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < s_lastFrame )
		s_air.clear();	// a new match: the ids start over
	if( now == s_lastFrame )
		return;
	s_lastFrame = now;
	const Real perSecond = (Real)LOGICFRAMES_PER_GAME_SECOND;

	for( Drawable *draw = TheGameClient->getDrawableList(); draw; draw = draw->getNextDrawable() )
	{
		Object *obj = draw->getObject();
		if( obj == NULL || obj->getDrawable() != draw )
			continue;
		const Bool aircraft = obj->isKindOf( KINDOF_AIRCRAFT );
		const Bool flare = !aircraft && obj->getTemplate()->getName() == "CountermeasureFlare";
		const Bool bomb = !aircraft && ( nameHas( obj, "AuroraBomb" ) || nameHas( obj, "CarpetBomb" ) );
		if( !aircraft && !flare && !bomb )
			continue;

		AirState &s = s_air[ draw->getID() ];
		if( !s.known )
		{
			memset( &s, 0, sizeof( s ) );
			for( Int i = 0; i < 2; ++i )
				s.vortex[ i ] = s.contrail[ i ] = INVALID_PARTICLE_SYSTEM_ID;
			s.cone = s.damaged = s.fall = s.heat = s.trail = INVALID_PARTICLE_SYSTEM_ID;
		}
		s.seen = now;

		if( draw->getFullyObscuredByShroud() || draw->isDrawableEffectivelyHidden() )
		{
			releaseAll( s );
			s.known = FALSE;
			continue;
		}

		const Coord3D *pos = obj->getPosition();
		Bool water = FALSE;
		const Real surface = surfaceUnder( pos->x, pos->y, water );
		const Real height = pos->z - surface;

		if( flare )
		{
			// a decoy flare lights the ground under it, warm white, while it falls
			if( height > 1.0f && ( now & 1 ) == 0 )
			{
				RGBColor warm;
				warm.red = 1.0f; warm.green = 0.86f; warm.blue = 0.62f;
				TheDisplay->createLightPulse( pos, &warm, height + 15.0f, 40.0f, 0, 3 );
			}
			s.known = TRUE;
			continue;
		}
		if( bomb )
		{
			if( height > GROUND_HEIGHT )
				hold( s.trail, "ReforgedBombTrail", draw, local( 0.0f, 0.0f, 0.0f ) );
			else
				release( s.trail );
			s.known = TRUE;
			continue;
		}

		const Matrix3D *m = obj->getTransformMatrix();
		const Vector3 nose = m->Get_X_Vector();
		const Real heading = WWMath::Atan2( nose.Y, nose.X );
		const Real pitch = WWMath::Asin( WWMath::Clamp( nose.Z, -1.0f, 1.0f ) );
		Real speed = 0.0f, turnRate = 0.0f, pitchRate = 0.0f, accel = 0.0f;
		if( s.known )
		{
			// a pass that caught up several ticks at once divides by all of them
			const Real ticks = (Real)( now > s.at ? now - s.at : 1 );
			Coord3D step = *pos;
			step.sub( &s.pos );
			speed = step.length() * perSecond / ticks;
			turnRate = wrapAngle( heading - s.heading ) * perSecond / ticks;
			pitchRate = ( pitch - s.pitch ) * perSecond / ticks;
			accel = speed - s.speed;
		}

		if( obj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
			updateHelicopter( obj, s, now, height, water, surface );
		else if( s.known )
			updateJet( draw, obj, s, now, turnRate, pitchRate, speed, accel, height, water, surface );
		updateBarrels( draw, obj, s, now );

		// the peaks every five seconds, so the thresholds above are tuned to what play reaches
		if( s.known )
		{
			if( fabs( turnRate ) > s.peakTurn ) s.peakTurn = (Real)fabs( turnRate );
			if( pitchRate > s.peakPull ) s.peakPull = pitchRate;
			if( speed > s.peakSpeed ) s.peakSpeed = speed;
			if( now % ( 5 * LOGICFRAMES_PER_GAME_SECOND ) == 0 && s.peakSpeed > 0.0f )
			{
				DEBUG_LOG(( "AIRFX frame %d %s turn %.2f pull %.2f speed %.0f height %.0f\n", now,
					obj->getTemplate()->getName().str(), s.peakTurn, s.peakPull, s.peakSpeed, height ));
				s.peakTurn = s.peakPull = s.peakSpeed = 0.0f;
			}
		}

		s.pos = *pos;
		s.heading = heading;
		s.pitch = pitch;
		s.speed = speed;
		s.at = now;
		s.known = TRUE;
	}

	// what left the map, or this player's sight, lets its systems go
	for( AirMap::iterator it = s_air.begin(); it != s_air.end(); )
	{
		if( it->second.seen != now )
		{
			releaseAll( it->second );
			s_air.erase( it++ );
		}
		else
			++it;
	}
}
