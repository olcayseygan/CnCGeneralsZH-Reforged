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

// AIIdentityGLA.cpp
// The GLA generals' own styles: GLA, Toxin, Demolition and Stealth.  AIIdentity.h has the hooks.
//
// GLA raids and rebuilds: the army comes home through the tunnels when the base is hit, a fighter stands
// over every hole a dead building left until it is up again, and the Scud Launchers idle at home shell
// the enemy's money from past his guns.  Toxin denies ground: the tractors drive at infantry blobs, the
// Anthrax Bomb lands on the army or the road between a supply pile and the enemy's supply center, and a
// tractor poisons that road.  Demolition trades lives: demo traps at the chokes whatever the personality,
// a Bomb Truck dressed as one of the enemy's own vehicles driven at his dearest building, and a unit with
// the suicide upgrade that is nearly dead blows itself up among the enemy instead of being pulled out.
// Stealth is never seen: the GPS Scrambler goes up over every wave as it sets out, and the units that are
// invisible from the factory raid the enemy's supply centers down a side road no detector watches.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/TunnelTracker.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"	// COMMAND_FIRED_BY_SCRIPT, the options a power fired by the AI carries
#include "GameLogic/AI.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/StealthDetectorUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"

class AIIdentityGLA : public AIIdentity
{
public:
	enum
	{
		MOVE_TUNNEL_JUMP			= 1 << 0,		///< GLA: a base under attack calls the army home through the tunnels (jumpHome)
		MOVE_HOLE_GUARD				= 1 << 1,		///< GLA: a fighter stands over each rebuild hole at home (guardHoles)
		MOVE_SCUD_HARASS			= 1 << 2,		///< GLA: idle Scud Launchers shell the enemy's money from past his guns (scudHarass)
		MOVE_TRACTOR_BLOBS		= 1 << 3,		///< Toxin: tractors drive at infantry blobs (tractorsAtBlobs)
		MOVE_ANTHRAX_ROUTES		= 1 << 4,		///< Toxin: the Anthrax Bomb on the army or the enemy's supply road (powerTarget)
		MOVE_POISON_SUPPLY		= 1 << 5,		///< Toxin: a tractor poisons the road between a supply pile and the enemy's center (poisonSupply)
		MOVE_CHOKE_TRAPS			= 1 << 6,		///< Demolition: demo traps at the chokes whatever the personality (AIPlayer::fortifyChokes)
		MOVE_DISGUISED_TRUCK	= 1 << 7,		///< Demolition: a Bomb Truck in enemy colours at his dearest building (disguisedTruck)
		MOVE_DEATH_BLAST			= 1 << 8,		///< Demolition: nearly dead, it blows itself up among the enemy (deathBlasts, pullHealth)
		MOVE_GPS_COVER				= 1 << 9,		///< Stealth: the GPS Scrambler over a wave as it sets out (onWaveSent)
		MOVE_SUPPLY_RAID			= 1 << 10,	///< Stealth: the invisible raid a supply center down an unwatched side road (supplyRaid)
	};

	AIIdentityGLA( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ),
		m_jumpFrame( 0 ), m_scudFrame( 0 ), m_poisonFrame( 0 ), m_poisonerID( INVALID_ID ),
		m_truckID( INVALID_ID ), m_truckFrame( 0 ), m_raidFrame( 0 ), m_trapFrame( 0 ) {}

	virtual void onStart( void );
	virtual void onTick( void );
	virtual void onWaveSent( AIGroup *wave, Real power );
	virtual Bool powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );
	virtual Real pullHealth( const Object *unit, Real shared ) const;
	virtual void xfer( Xfer *xfer );

private:
	void jumpHome( const std::vector<Object *> &owned );
	void guardHoles( const std::vector<Object *> &owned );
	void scudHarass( const std::vector<Object *> &owned, Player *foe );
	void tractorsAtBlobs( const std::vector<Object *> &owned );
	void poisonSupply( const std::vector<Object *> &owned, Player *foe );
	void chokeTraps( const std::vector<Object *> &owned );
	void disguisedTruck( const std::vector<Object *> &owned, Player *foe );
	void deathBlasts( const std::vector<Object *> &owned );
	void supplyRaid( const std::vector<Object *> &owned, Player *foe );
	Bool supplyRoad( Player *foe, Coord3D *road, Bool clearOfGuns ) const;
	Bool suicideReady( void ) const;

	UnsignedInt	m_jumpFrame;		///< the last call home through the tunnels, 0 for none
	UnsignedInt	m_scudFrame;		///< the last Scud raid sent, 0 for none
	UnsignedInt	m_poisonFrame;	///< the last tractor sent to poison a supply road, 0 for none
	ObjectID		m_poisonerID;		///< that tractor
	ObjectID		m_truckID;			///< the Bomb Truck being dressed and sent, INVALID_ID for none
	UnsignedInt	m_truckFrame;		///< when it was told to dress
	UnsignedInt	m_raidFrame;		///< the last invisible raid sent, 0 for none
	UnsignedInt	m_trapFrame;		///< the last look at the chokes' traps, 0 for none
};

AIIdentity *createGLAIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityGLA( ai, general );
}

// ---- the clocks: one turn a game second, each move on its own multiple of it ----------------------------
static const UnsignedInt GLA_TURN_FRAMES = (UnsignedInt)LOGICFRAMES_PER_GAME_SECOND;
static const UnsignedInt TUNNEL_JUMP_GAP = (UnsignedInt)( 25 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt SCUD_RAID_GAP = (UnsignedInt)( 40 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt POISON_GAP = (UnsignedInt)( 45 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt TRUCK_DRESS_FRAMES = (UnsignedInt)( 15 * LOGICFRAMES_PER_GAME_SECOND );	///< a truck not dressed by then goes as it is
static const UnsignedInt SUPPLY_RAID_GAP = (UnsignedInt)( 50 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt TRAP_GAP = (UnsignedInt)( 12 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt BLOB_FRESH_FRAMES = (UnsignedInt)( 3 * LOGICFRAMES_PER_GAME_SECOND );	///< a ledger row seen this lately is where it was
static const UnsignedInt ARMY_FRESH_FRAMES = (UnsignedInt)( 8 * LOGICFRAMES_PER_GAME_SECOND );
static const UnsignedInt OPENING_FRAMES = (UnsignedInt)( 150 * LOGICFRAMES_PER_GAME_SECOND );		///< no raids or traps before the economy is in

static const Int TUNNEL_JUMP_INTRUDERS = 3;			///< enemies that can shoot at home before the army is called back
static const Int TUNNEL_JUMP_MOST = 30;
static const Real HOLE_GUARD_NEAR = 120.0f;			///< a fighter this near a hole guards it already
static const Int SCUD_RAID_MOST = 3;
static const Real SCUD_STANDOFF = 0.85f;				///< the share of its reach a Scud fires from
static const Real GUN_MARGIN = 40.0f;						///< a gun covers a spot this far past its reach
static const Real BLOB_REACH = 280.0f;					///< how far a tractor looks for a blob
static const Real BLOB_RADIUS = 60.0f;
static const Int BLOB_LEAST = 4;								///< infantry in one blob worth driving at
static const Real ANTHRAX_ARMY_LEAST = 6.0f;		///< an army in the bomb's circle worth it: infantry 2, anything else 1
static const Real SUPPLY_PILE_REACH = 350.0f;		///< the pile a supply center trucks from lies this near it
static const Real BLAST_HEALTH = 0.3f;					///< under this share a suicide unit goes in
static const Real BLAST_REACH = 160.0f;					///< ... at an enemy this near
static const Real BLAST_TOUCH = 35.0f;					///< ... and blows when this near its edge
static const Int RAID_LEAST = 2;
static const Int RAID_MOST = 6;
static const Real DETECTOR_REACH = 450.0f;			///< the widest sight a detector is looked for within
static const Real RAID_SIDE = 0.35f;						///< the side road bends out this share of the way

static Real distSqr2D( const Coord3D *a, const Coord3D *b )
{
	return sqr( a->x - b->x ) + sqr( a->y - b->y );
}

static void collectOwned( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

/** observerKnowsAbout's line (AIPlayer.cpp): in sight or a building seen, and a stealthed thing only once detected. */
static Bool glaKnows( const Object *obj, Int observer )
{
	if( obj == NULL || obj->isEffectivelyDead() || obj->isUnknownTo( observer ) )
		return FALSE;
	return !obj->testStatus( OBJECT_STATUS_STEALTHED ) || obj->testStatus( OBJECT_STATUS_DETECTED );
}

/** A ground unit of ours that fights and is free to: not a worker, not in anything, not a building, and not
	* a Stinger Site's or a tunnel's soldier or a rioter, who go back to whatever spawned them wherever they are
	* sent (the first hole guard sent the same Stinger soldiers 92 times in four matches). */
static Bool freeFighter( const Object *obj )
{
	return !obj->isEffectivelyDead() && obj->getAI() != NULL && !obj->isContained() && !obj->isKindOf( KINDOF_STRUCTURE ) &&
		obj->isKindOf( KINDOF_SELECTABLE ) && obj->getSlavedUpdateInterface() == NULL &&
		!obj->isKindOf( KINDOF_DOZER ) && !obj->isKindOf( KINDOF_HARVESTER ) && !obj->isKindOf( KINDOF_AIRCRAFT ) &&
		!obj->isKindOf( KINDOF_IMMOBILE ) && !obj->isKindOf( KINDOF_DRONE ) && obj->isKindOf( KINDOF_CAN_ATTACK ) &&
		!obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION );
}

static Bool waiting( const Object *obj )
{
	const StateID state = obj->getAI()->getCurrentStateID();
	return state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE;
}

static Bool named( const Object *obj, const char *part )
{
	return strstr( obj->getTemplate()->getName().str(), part ) != NULL;
}

static Real healthShare( const Object *obj )
{
	const BodyModuleInterface *body = obj->getBodyModule();
	return body && body->getMaxHealth() > 0.0f ? body->getHealth() / body->getMaxHealth() : 1.0f;
}

static Bool carriesCharge( const Object *obj )
{
	const Weapon *charge = obj->getWeaponInWeaponSlot( TERTIARY_WEAPON );
	return charge != NULL && charge->getDeathType() == DEATH_SUICIDED;
}

/** templateCanStealth's question (AIPlayer.cpp): invisible from the factory. */
static Bool innateStealth( const ThingTemplate *tmpl )
{
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
		if( modules.getNthName( m ).compareNoCase( "StealthUpdate" ) == 0 )
			return ( (const StealthUpdateModuleData *)modules.getNthData( m ) )->m_innateStealth;
	return FALSE;
}

/** A power of ours of one of these types, ready and with its science bought, in object list order; NULL for none. */
static SpecialPowerModuleInterface *readyPower( Player *player, SpecialPowerType a, SpecialPowerType b )
{
	std::vector<Object *> owned;
	player->iterateObjects( collectOwned, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->isDisabled() || obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			continue;
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerModuleInterface *power = (*m)->getSpecialPower();
			if( power == NULL || !power->isReady() )
				continue;
			const SpecialPowerType type = power->getSpecialPowerTemplate()->getSpecialPowerType();
			if( type != a && type != b )
				continue;
			const ScienceType science = power->getRequiredScience();
			if( science != SCIENCE_INVALID && !player->hasScience( science ) )
				continue;
			return power;
		}
	}
	return NULL;
}

static Bool coveredByGuns( const std::vector<AIKnownGun> &guns, const Coord3D *spot )
{
	for( size_t g = 0; g < guns.size(); ++g )
		if( sqr( guns[ g ].m_x - spot->x ) + sqr( guns[ g ].m_y - spot->y ) <= sqr( guns[ g ].m_reach + GUN_MARGIN ) )
			return TRUE;
	return FALSE;
}

// ---- the hooks -------------------------------------------------------------------------------------------

void AIIdentityGLA::onStart( void )
{
	switch( m_general )
	{
		case AIGENERAL_GLA:
			drawMove( MOVE_TUNNEL_JUMP, 85 );
			drawMove( MOVE_HOLE_GUARD, 85 );
			drawMove( MOVE_SCUD_HARASS, 75 );
			break;
		case AIGENERAL_GLA_TOXIN:
			drawMove( MOVE_TRACTOR_BLOBS, 100 );
			drawMove( MOVE_ANTHRAX_ROUTES, 85 );
			drawMove( MOVE_POISON_SUPPLY, 70 );
			break;
		case AIGENERAL_GLA_DEMOLITION:
			drawMove( MOVE_CHOKE_TRAPS, 80 );
			drawMove( MOVE_DISGUISED_TRUCK, 85 );
			drawMove( MOVE_DEATH_BLAST, 100 );
			break;
		case AIGENERAL_GLA_STEALTH:
			drawMove( MOVE_GPS_COVER, 100 );
			drawMove( MOVE_SUPPLY_RAID, 80 );
			break;
		default:
			break;
	}
	DEBUG_LOG(("AI IDENTITY frame %d player %d '%s' moves 0x%x\n", TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(),
		m_ai->m_player->getSide().str(), m_moves));
}

void AIIdentityGLA::xfer( Xfer *xfer )
{
	AIIdentity::xfer( xfer );
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferUnsignedInt( &m_jumpFrame );
	xfer->xferUnsignedInt( &m_scudFrame );
	xfer->xferUnsignedInt( &m_poisonFrame );
	xfer->xferObjectID( &m_poisonerID );
	xfer->xferObjectID( &m_truckID );
	xfer->xferUnsignedInt( &m_truckFrame );
	xfer->xferUnsignedInt( &m_raidFrame );
	xfer->xferUnsignedInt( &m_trapFrame );
}

void AIIdentityGLA::onTick( void )
{
	if( m_moves == 0 || !m_ai->m_baseCenterSet )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( ( now + (UnsignedInt)m_ai->m_player->getPlayerIndex() * 3 ) % GLA_TURN_FRAMES != 0 )
		return;
	const UnsignedInt turn = now / GLA_TURN_FRAMES;
	std::vector<Object *> owned;
	m_ai->m_player->iterateObjects( collectOwned, &owned );
	// the enemy the shared AI last picked, without picking again
	Player *foe = m_ai->peekAiEnemy();

	if( hasMove( MOVE_TUNNEL_JUMP ) )
		jumpHome( owned );
	if( hasMove( MOVE_HOLE_GUARD ) && turn % 3 == 0 )
		guardHoles( owned );
	if( hasMove( MOVE_SCUD_HARASS ) && foe && turn % 5 == 0 )
		scudHarass( owned, foe );
	if( hasMove( MOVE_TRACTOR_BLOBS ) )
		tractorsAtBlobs( owned );
	if( hasMove( MOVE_POISON_SUPPLY ) && foe && turn % 5 == 0 )
		poisonSupply( owned, foe );
	if( hasMove( MOVE_CHOKE_TRAPS ) )
		chokeTraps( owned );
	if( hasMove( MOVE_DISGUISED_TRUCK ) && foe )
		disguisedTruck( owned, foe );
	if( hasMove( MOVE_DEATH_BLAST ) )
		deathBlasts( owned );
	if( hasMove( MOVE_SUPPLY_RAID ) && foe && turn % 5 == 0 )
		supplyRaid( owned, foe );
	// the skill sets never buy the GPS Scrambler: the first point it can take goes on it
	if( hasMove( MOVE_GPS_COVER ) && turn % 5 == 0 )
	{
		const ScienceType gps = TheScienceStore->getScienceFromInternalName( "Slth_SCIENCE_GPSScrambler" );
		if( gps != SCIENCE_INVALID && m_ai->m_player->isCapableOfPurchasingScience( gps ) && m_ai->m_player->attemptToPurchaseScience( gps ) )
			DEBUG_LOG(("AI STEALTH frame %d player %d buys the GPS Scrambler\n", now, m_ai->m_player->getPlayerIndex()));
	}
}

// ---- GLA -------------------------------------------------------------------------------------------------

/** A base hit by a real force calls its fighters out on the map home through the tunnel network, the way a
	* player jumps his army from one front to the other: each takes the tunnel trip its own move order would
	* (TunnelTracker::findTunnelShortcut) and comes up at home fighting.  One that has no shorter way by tunnel
	* stays on its job. */
void AIIdentityGLA::jumpHome( const std::vector<Object *> &owned )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( m_jumpFrame != 0 && now - m_jumpFrame < TUNNEL_JUMP_GAP )
		return;
	const TunnelTracker *tunnels = m_ai->m_player->getTunnelSystem();
	if( tunnels == NULL || tunnels->friend_getTunnelCount() < 2 || !m_ai->isBaseUnderAttack() )
		return;
	Int intruders = 0;
	if( m_ai->homeIntruder( &intruders, NULL ) == NULL || intruders < TUNNEL_JUMP_INTRUDERS )
		return;

	Int sent = 0;
	for( size_t o = 0; o < owned.size() && sent < TUNNEL_JUMP_MOST; ++o )
	{
		Object *obj = owned[ o ];
		if( !freeFighter( obj ) || m_ai->isAtHome( obj->getPosition() ) )
			continue;
		const Real walk = (Real)sqrt( distSqr2D( obj->getPosition(), &m_ai->m_baseCenter ) );
		Object *entrance = TunnelTracker::findTunnelShortcut( obj, obj->getPosition(), &m_ai->m_baseCenter, walk );
		if( entrance && obj->getAI()->takeTunnelTrip( entrance, &m_ai->m_baseCenter, TUNNEL_TRIP_ATTACK_MOVE, CMD_FROM_AI ) )
			++sent;
	}
	if( sent == 0 )
		return;
	m_jumpFrame = now;
	DEBUG_LOG(("AI GLA frame %d player %d calls %d units home through the tunnels, %d enemies at home\n", now,
		m_ai->m_player->getPlayerIndex(), sent, intruders));
}

/** A dead GLA building leaves a hole that builds it again by itself, unless the enemy finishes the hole
	* first.  A fighter waiting at home goes to stand over each hole there that has none of ours near it. */
void AIIdentityGLA::guardHoles( const std::vector<Object *> &owned )
{
	std::vector<Object *> holes;
	for( size_t o = 0; o < owned.size(); ++o )
		if( owned[ o ]->isKindOf( KINDOF_REBUILD_HOLE ) && !owned[ o ]->isEffectivelyDead() && m_ai->isAtHome( owned[ o ]->getPosition() ) )
			holes.push_back( owned[ o ] );
	if( holes.empty() )
		return;

	std::vector<Object *> taken;
	for( size_t h = 0; h < holes.size(); ++h )
	{
		const Coord3D *at = holes[ h ]->getPosition();
		Object *nearest = NULL;
		Real nearestSqr = 0.0f;
		Bool guarded = FALSE;
		for( size_t o = 0; o < owned.size() && !guarded; ++o )
		{
			Object *obj = owned[ o ];
			if( !freeFighter( obj ) )
				continue;
			const Real d = distSqr2D( obj->getPosition(), at );
			guarded = d <= sqr( HOLE_GUARD_NEAR );
			if( guarded || !waiting( obj ) || !m_ai->isAtHome( obj->getPosition() ) || std::find( taken.begin(), taken.end(), obj ) != taken.end() )
				continue;
			if( nearest == NULL || d < nearestSqr )
			{
				nearest = obj;
				nearestSqr = d;
			}
		}
		if( guarded || nearest == NULL )
			continue;
		taken.push_back( nearest );
		nearest->getAI()->aiGuardPosition( at, GUARDMODE_NORMAL, CMD_FROM_AI );
		DEBUG_LOG(("AI GLA frame %d player %d sends '%s' %d to guard the hole of '%s' at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
			m_ai->m_player->getPlayerIndex(), nearest->getTemplate()->getName().str(), nearest->getID(),
			holes[ h ]->getTemplate()->getName().str(), at->x, at->y));
	}
}

/** The Scud Launchers waiting at home go up to three at a time at the enemy's money: the supply center,
	* drop zone, black market or power plant of his the AI has seen, fired at from SCUD_STANDOFF of their
	* reach on our side of it, where none of the guns it knows reaches.  Dearest first. */
void AIIdentityGLA::scudHarass( const std::vector<Object *> &owned, Player *foe )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < OPENING_FRAMES || ( m_scudFrame != 0 && now - m_scudFrame < SCUD_RAID_GAP ) || m_ai->isBaseUnderAttack() )
		return;
	std::vector<Object *> scuds;
	Real reach = 0.0f;
	for( size_t o = 0; o < owned.size() && (Int)scuds.size() < SCUD_RAID_MOST; ++o )
	{
		Object *obj = owned[ o ];
		if( !freeFighter( obj ) || !named( obj, "ScudLauncher" ) || !waiting( obj ) || !m_ai->isAtHome( obj->getPosition() ) )
			continue;
		const Weapon *weapon = obj->getWeaponInWeaponSlot( PRIMARY_WEAPON );
		if( weapon == NULL )
			continue;
		const Real r = weapon->getAttackRange( obj );
		reach = scuds.empty() ? r : min( reach, r );
		scuds.push_back( obj );
	}
	if( scuds.empty() || reach <= 0.0f )
		return;

	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	std::vector<Object *> theirs;
	foe->iterateObjects( collectOwned, &theirs );
	const Int me = m_ai->m_player->getPlayerIndex();
	Object *best = NULL;
	Real bestWorth = 0.0f;
	for( size_t t = 0; t < theirs.size(); ++t )
	{
		Object *obj = theirs[ t ];
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || !glaKnows( obj, me ) ||
				!( obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || obj->isKindOf( KINDOF_FS_SUPPLY_DROPZONE ) || obj->isKindOf( KINDOF_FS_BLACK_MARKET ) ||
					 obj->isKindOf( KINDOF_FS_POWER ) || obj->isKindOf( KINDOF_CASH_GENERATOR ) ) )
			continue;
		const Real worth = INT_TO_REAL( obj->getTemplate()->calcCostToBuild( foe ) );
		if( best && worth <= bestWorth )
			continue;
		// the spot it is shelled from, on our side of it
		const Coord3D *at = obj->getPosition();
		const Real away = (Real)sqrt( distSqr2D( at, &m_ai->m_baseCenter ) );
		if( away <= 1.0f )
			continue;
		Coord3D spot;
		spot.x = at->x + ( m_ai->m_baseCenter.x - at->x ) * reach * SCUD_STANDOFF / away;
		spot.y = at->y + ( m_ai->m_baseCenter.y - at->y ) * reach * SCUD_STANDOFF / away;
		spot.z = 0.0f;
		if( coveredByGuns( guns, &spot ) )
			continue;
		best = obj;
		bestWorth = worth;
	}
	if( best == NULL )
		return;
	for( size_t s = 0; s < scuds.size(); ++s )
		scuds[ s ]->getAI()->aiAttackObject( best, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	m_scudFrame = now;
	DEBUG_LOG(("AI GLA frame %d player %d sends %d Scud Launchers at '%s' %d at (%.0f,%.0f), from %.0f off, no known gun there\n", now,
		me, (Int)scuds.size(), best->getTemplate()->getName().str(), best->getID(), best->getPosition()->x, best->getPosition()->y,
		reach * SCUD_STANDOFF));
}

// ---- Toxin -----------------------------------------------------------------------------------------------

/** Each Toxin Tractor not already spraying infantry drives at the thickest knot of enemy infantry the AI
	* has in sight within BLOB_REACH of it, at the soldier in its middle: the spray takes the knot. */
void AIIdentityGLA::tractorsAtBlobs( const std::vector<Object *> &owned )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	// the infantry in sight now, from the ledger, in ObjectID order
	std::vector<ObjectID> ids;
	std::vector<Coord3D> at;
	for( std::map<ObjectID, AIPlayer::SeenEnemy>::const_iterator it = m_ai->m_seenEnemies.begin(); it != m_ai->m_seenEnemies.end(); ++it )
	{
		const AIPlayer::SeenEnemy &row = it->second;
		if( row.tmpl == NULL || !row.tmpl->isKindOf( KINDOF_INFANTRY ) || now - row.seen > BLOB_FRESH_FRAMES )
			continue;
		ids.push_back( it->first );
		at.push_back( row.pos );
	}
	if( (Int)ids.size() < BLOB_LEAST )
		return;

	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *tractor = owned[ o ];
		if( !freeFighter( tractor ) || !named( tractor, "ToxinTruck" ) || tractor->getID() == m_poisonerID )
			continue;
		const Object *goal = tractor->getAI()->getGoalObject();
		if( goal && goal->isKindOf( KINDOF_INFANTRY ) )
			continue;
		Int bestCount = 0;
		size_t bestAt = 0;
		for( size_t i = 0; i < at.size(); ++i )
		{
			if( distSqr2D( &at[ i ], tractor->getPosition() ) > sqr( BLOB_REACH ) )
				continue;
			Int count = 0;
			for( size_t j = 0; j < at.size(); ++j )
				if( distSqr2D( &at[ i ], &at[ j ] ) <= sqr( BLOB_RADIUS ) )
					++count;
			if( count > bestCount )
			{
				bestCount = count;
				bestAt = i;
			}
		}
		if( bestCount < BLOB_LEAST )
			continue;
		Object *victim = TheGameLogic->findObjectByID( ids[ bestAt ] );
		if( !glaKnows( victim, m_ai->m_player->getPlayerIndex() ) )
			continue;
		tractor->getAI()->aiAttackObject( victim, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		DEBUG_LOG(("AI TOXIN frame %d player %d drives tractor %d at a blob of %d infantry at (%.0f,%.0f)\n", now,
			m_ai->m_player->getPlayerIndex(), tractor->getID(), bestCount, at[ bestAt ].x, at[ bestAt ].y));
	}
}

/** The road the enemy's supply trucks drive: halfway between a supply center of his the AI has seen and the
	* supply pile nearest it, the first such center in his object list.  clearOfGuns skips a road a known gun
	* covers. */
Bool AIIdentityGLA::supplyRoad( Player *foe, Coord3D *road, Bool clearOfGuns ) const
{
	std::vector<AIKnownGun> guns;
	if( clearOfGuns )
		m_ai->collectKnownGuns( &guns );
	std::vector<Object *> theirs;
	foe->iterateObjects( collectOwned, &theirs );
	const Int me = m_ai->m_player->getPlayerIndex();
	for( size_t t = 0; t < theirs.size(); ++t )
	{
		const Object *center = theirs[ t ];
		if( !center->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || center->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || !glaKnows( center, me ) )
			continue;
		PartitionFilterAcceptByKindOf piles( MAKE_KINDOF_MASK( KINDOF_SUPPLY_SOURCE ), KINDOFMASK_NONE );
		PartitionFilterRejectByKindOf notCenter( MAKE_KINDOF_MASK( KINDOF_FS_SUPPLY_CENTER ), KINDOFMASK_NONE );
		PartitionFilterAlive alive;
		PartitionFilterOnMap onMap;
		PartitionFilter *filters[] = { &piles, &notCenter, &alive, &onMap, NULL };
		Object *pile = ThePartitionManager->getClosestObject( center->getPosition(), SUPPLY_PILE_REACH, FROM_CENTER_2D, filters );
		if( pile == NULL )
			continue;
		Coord3D spot;
		spot.x = 0.5f * ( pile->getPosition()->x + center->getPosition()->x );
		spot.y = 0.5f * ( pile->getPosition()->y + center->getPosition()->y );
		spot.z = 0.5f * ( pile->getPosition()->z + center->getPosition()->z );
		if( coveredByGuns( guns, &spot ) )
			continue;
		*road = spot;
		return TRUE;
	}
	return FALSE;
}

/** One Toxin Tractor waiting at home goes to an enemy supply road no known gun covers and sprays its
	* contaminating secondary there, the ground his trucks cross on every trip. */
void AIIdentityGLA::poisonSupply( const std::vector<Object *> &owned, Player *foe )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < OPENING_FRAMES || ( m_poisonFrame != 0 && now - m_poisonFrame < POISON_GAP ) || m_ai->isBaseUnderAttack() )
		return;
	const Object *busy = TheGameLogic->findObjectByID( m_poisonerID );
	if( busy && !busy->isEffectivelyDead() && busy->getAI() && !waiting( busy ) )
		return;
	m_poisonerID = INVALID_ID;
	Object *tractor = NULL;
	for( size_t o = 0; o < owned.size() && tractor == NULL; ++o )
		if( freeFighter( owned[ o ] ) && named( owned[ o ], "ToxinTruck" ) && waiting( owned[ o ] ) && m_ai->isAtHome( owned[ o ]->getPosition() ) &&
				owned[ o ]->getWeaponInWeaponSlot( SECONDARY_WEAPON ) != NULL )
			tractor = owned[ o ];
	Coord3D road;
	if( tractor == NULL || !supplyRoad( foe, &road, TRUE ) || !tractor->setWeaponLock( SECONDARY_WEAPON, LOCKED_TEMPORARILY ) )
		return;
	tractor->getAI()->aiAttackPosition( &road, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	m_poisonerID = tractor->getID();
	m_poisonFrame = now;
	DEBUG_LOG(("AI TOXIN frame %d player %d sends tractor %d to poison the supply road at (%.0f,%.0f)\n", now,
		m_ai->m_player->getPlayerIndex(), tractor->getID(), road.x, road.y));
}

/** The Anthrax Bomb: on the thickest army of his the AI has seen in the last few seconds, infantry twice;
	* with no army worth it, on the road his supply trucks drive; with neither, the shared aim. */
Bool AIIdentityGLA::powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	if( !hasMove( MOVE_ANTHRAX_ROUTES ) || power->getSpecialPowerType() != SPECIAL_ANTHRAX_BOMB )
		return FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	std::vector<Coord3D> at;
	std::vector<Real> weight;
	for( std::map<ObjectID, AIPlayer::SeenEnemy>::const_iterator it = m_ai->m_seenEnemies.begin(); it != m_ai->m_seenEnemies.end(); ++it )
	{
		const AIPlayer::SeenEnemy &row = it->second;
		if( row.tmpl == NULL || row.owner != enemyIndex || now - row.seen > ARMY_FRESH_FRAMES || row.tmpl->isKindOf( KINDOF_AIRCRAFT ) )
			continue;
		at.push_back( row.pos );
		weight.push_back( row.tmpl->isKindOf( KINDOF_INFANTRY ) ? 2.0f : 1.0f );
	}
	Real best = 0.0f;
	size_t bestAt = 0;
	for( size_t i = 0; i < at.size(); ++i )
	{
		Real sum = 0.0f;
		for( size_t j = 0; j < at.size(); ++j )
			if( distSqr2D( &at[ i ], &at[ j ] ) <= sqr( radius ) )
				sum += weight[ j ];
		if( sum > best )
		{
			best = sum;
			bestAt = i;
		}
	}
	if( best >= ANTHRAX_ARMY_LEAST )
	{
		*target = at[ bestAt ];
		DEBUG_LOG(("AI TOXIN frame %d player %d aims '%s' at an army worth %.0f at (%.0f,%.0f)\n", now, m_ai->m_player->getPlayerIndex(),
			power->getName().str(), best, target->x, target->y));
		return TRUE;
	}
	Player *foe = ThePlayerList->getNthPlayer( enemyIndex );
	if( foe && supplyRoad( foe, target, FALSE ) )
	{
		DEBUG_LOG(("AI TOXIN frame %d player %d aims '%s' at the supply road at (%.0f,%.0f)\n", now, m_ai->m_player->getPlayerIndex(),
			power->getName().str(), target->x, target->y));
		return TRUE;
	}
	return FALSE;
}

// ---- Demolition ------------------------------------------------------------------------------------------

/** The Turtle's demo traps in front of the chokes (AIPlayer::fortifyChokes), for any personality, once the
	* opening is over and while the base is not being hit.  Its count is the rung's. */
void AIIdentityGLA::chokeTraps( const std::vector<Object *> &owned )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < OPENING_FRAMES || ( m_trapFrame != 0 && now - m_trapFrame < TRAP_GAP ) )
		return;
	m_trapFrame = now;
	// the Turtle lays them already
	if( m_ai->m_persona == AIPERSONA_TURTLE || !m_ai->m_player->getCanBuildBase() || m_ai->isBaseUnderAttack() )
		return;
	for( size_t o = 0; o < owned.size(); ++o )
		if( owned[ o ]->isKindOf( KINDOF_DOZER ) && !owned[ o ]->isEffectivelyDead() && owned[ o ]->getAI() )
		{
			m_ai->fortifyChokes( owned[ o ] );
			return;
		}
}

/** The suicide upgrade is done: a death by its own hand fires the big charge. */
Bool AIIdentityGLA::suicideReady( void ) const
{
	const UpgradeTemplate *upgrade = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade( "Demo_Upgrade_SuicideBomb" ) : NULL;
	return upgrade && m_ai->m_player->hasUpgradeComplete( upgrade );
}

/** One Bomb Truck at a time: dressed as a vehicle of the enemy's the AI has in sight, then driven at the
	* dearest building of his it has seen, his superweapon or command center before anything else.  A truck
	* that has not dressed in TRUCK_DRESS_FRAMES goes as it is. */
void AIIdentityGLA::disguisedTruck( const std::vector<Object *> &owned, Player *foe )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < OPENING_FRAMES )
		return;
	const Int me = m_ai->m_player->getPlayerIndex();
	Object *truck = TheGameLogic->findObjectByID( m_truckID );
	if( truck == NULL || truck->isEffectivelyDead() || truck->getAI() == NULL )
	{
		m_truckID = INVALID_ID;
		if( m_ai->isBaseUnderAttack() )
			return;
		truck = NULL;
		for( size_t o = 0; o < owned.size() && truck == NULL; ++o )
			if( freeFighter( owned[ o ] ) && owned[ o ]->isKindOf( KINDOF_DISGUISER ) && waiting( owned[ o ] ) &&
					m_ai->isAtHome( owned[ o ]->getPosition() ) && owned[ o ]->findSpecialPowerModuleInterface( SPECIAL_DISGUISE_AS_VEHICLE ) )
				truck = owned[ o ];
		if( truck == NULL )
			return;
		// a ground vehicle of his in sight to dress as, as the button allows
		Object *model = NULL;
		for( std::map<ObjectID, AIPlayer::SeenEnemy>::const_iterator it = m_ai->m_seenEnemies.begin(); it != m_ai->m_seenEnemies.end() && model == NULL; ++it )
		{
			if( it->second.owner != foe->getPlayerIndex() || now - it->second.seen > BLOB_FRESH_FRAMES )
				continue;
			Object *obj = TheGameLogic->findObjectByID( it->first );
			if( glaKnows( obj, me ) && obj->isKindOf( KINDOF_VEHICLE ) && !obj->isKindOf( KINDOF_AIRCRAFT ) && !obj->isKindOf( KINDOF_BOAT ) &&
					!obj->isKindOf( KINDOF_CLIFF_JUMPER ) && !obj->isKindOf( KINDOF_DISGUISER ) )
				model = obj;
		}
		if( model == NULL )
			return;
		truck->findSpecialPowerModuleInterface( SPECIAL_DISGUISE_AS_VEHICLE )->doSpecialPowerAtObject( model, COMMAND_FIRED_BY_SCRIPT );
		m_truckID = truck->getID();
		m_truckFrame = now;
		DEBUG_LOG(("AI DEMO frame %d player %d dresses Bomb Truck %d as '%s' %d\n", now, me, truck->getID(),
			model->getTemplate()->getName().str(), model->getID()));
		return;
	}

	// dressed, or out of time: at his dearest building, unless already on its way to one
	const StealthUpdate *stealth = truck->getStealth();
	const Bool dressed = stealth && stealth->isDisguised();
	if( !dressed && now - m_truckFrame < TRUCK_DRESS_FRAMES )
		return;
	const Object *goal = truck->getAI()->getGoalObject();
	if( goal && !goal->isEffectivelyDead() && goal->isKindOf( KINDOF_STRUCTURE ) )
		return;
	std::vector<Object *> theirs;
	foe->iterateObjects( collectOwned, &theirs );
	Object *best = NULL;
	Real bestWorth = 0.0f;
	for( size_t t = 0; t < theirs.size(); ++t )
	{
		Object *obj = theirs[ t ];
		if( !obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_REBUILD_HOLE ) || !glaKnows( obj, me ) )
			continue;
		Real worth = INT_TO_REAL( obj->getTemplate()->calcCostToBuild( foe ) );
		if( obj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
			worth *= 5.0f;
		else if( obj->isKindOf( KINDOF_COMMANDCENTER ) )
			worth *= 3.0f;
		if( best == NULL || worth > bestWorth )
		{
			best = obj;
			bestWorth = worth;
		}
	}
	if( best == NULL )
		return;
	truck->getAI()->aiAttackObject( best, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	DEBUG_LOG(("AI DEMO frame %d player %d drives Bomb Truck %d%s at '%s' %d at (%.0f,%.0f)\n", now, me, truck->getID(),
		dressed ? " in his colours" : "", best->getTemplate()->getName().str(), best->getID(), best->getPosition()->x, best->getPosition()->y));
}

/** With the suicide upgrade a unit that dies by its own hand fires a far bigger charge than one that is shot.
	* So a fighter nearly dead goes at the nearest enemy within BLAST_REACH and blows itself up beside it. */
void AIIdentityGLA::deathBlasts( const std::vector<Object *> &owned )
{
	if( !suicideReady() )
		return;
	const Int me = m_ai->m_player->getPlayerIndex();
	Int charged = 0;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		if( !freeFighter( obj ) || obj->isKindOf( KINDOF_DISGUISER ) || !carriesCharge( obj ) || healthShare( obj ) >= BLAST_HEALTH )
			continue;
		PartitionFilterRelationship enemies( obj, PartitionFilterRelationship::ALLOW_ENEMIES );
		PartitionFilterAlive alive;
		PartitionFilterOnMap onMap;
		PartitionFilterRejectByKindOf notAir( MAKE_KINDOF_MASK( KINDOF_AIRCRAFT ), KINDOFMASK_NONE );
		PartitionFilterRejectByKindOf notShell( MAKE_KINDOF_MASK( KINDOF_PROJECTILE ), KINDOFMASK_NONE );
		PartitionFilterRejectByKindOf notMine( MAKE_KINDOF_MASK( KINDOF_MINE ), KINDOFMASK_NONE );
		PartitionFilter *filters[] = { &enemies, &alive, &onMap, &notAir, &notShell, &notMine, NULL };
		Object *victim = ThePartitionManager->getClosestObject( obj, BLAST_REACH, FROM_BOUNDINGSPHERE_2D, filters );
		if( !glaKnows( victim, me ) )
			continue;
		const Real touch = BLAST_TOUCH + victim->getGeometryInfo().getBoundingCircleRadius();
		if( distSqr2D( obj->getPosition(), victim->getPosition() ) <= sqr( touch ) )
		{
			if( !obj->setWeaponLock( TERTIARY_WEAPON, LOCKED_TEMPORARILY ) )
				continue;
			DEBUG_LOG(("AI DEMO frame %d player %d blows '%s' %d up at %.0f%% beside '%s' %d\n", TheGameLogic->getFrame(), me,
				obj->getTemplate()->getName().str(), obj->getID(), healthShare( obj ) * 100.0f, victim->getTemplate()->getName().str(), victim->getID()));
			obj->getAI()->aiAttackPosition( obj->getPosition(), 1, CMD_FROM_AI );
		}
		else
		{
			Coord3D to = *victim->getPosition();
			obj->getAI()->aiMoveToPosition( &to, CMD_FROM_AI );
			++charged;
		}
	}
	if( charged > 0 )
		DEBUG_LOG(("AI DEMO frame %d player %d sends %d nearly dead units at the enemy to blow\n", TheGameLogic->getFrame(), me, charged));
}

Real AIIdentityGLA::pullHealth( const Object *unit, Real shared ) const
{
	// a unit that carries the big charge is worth more blown up among the enemy than mended
	if( hasMove( MOVE_DEATH_BLAST ) && carriesCharge( unit ) && suicideReady() )
		return 0.0f;
	return shared;
}

// ---- Stealth ---------------------------------------------------------------------------------------------

/** The GPS Scrambler hides every unit of ours in its circle: up over the wave the moment it sets out. */
void AIIdentityGLA::onWaveSent( AIGroup *wave, Real power )
{
	if( !hasMove( MOVE_GPS_COVER ) || wave == NULL )
		return;
	SpecialPowerModuleInterface *scrambler = readyPower( m_ai->m_player, SPECIAL_GPS_SCRAMBLER, SLTH_SPECIAL_GPS_SCRAMBLER );
	Coord3D center;
	if( scrambler == NULL || !wave->getCenter( &center ) )
		return;
	DEBUG_LOG(("AI STEALTH frame %d player %d hides a wave of %d units, %.0f power, under '%s' at (%.0f,%.0f)\n", TheGameLogic->getFrame(),
		m_ai->m_player->getPlayerIndex(), wave->getCount(), power, scrambler->getSpecialPowerTemplate()->getName().str(), center.x, center.y));
	scrambler->doSpecialPowerAtLocation( &center, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
}

/** The units invisible from the factory that wait at home raid an enemy supply center no detector of his
	* the AI has seen watches.  They go round by a side road, the straight line's middle bent out towards
	* whichever side the AI's map and its losses say is quieter, and fight the trucks they find there. */
void AIIdentityGLA::supplyRaid( const std::vector<Object *> &owned, Player *foe )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now < OPENING_FRAMES || ( m_raidFrame != 0 && now - m_raidFrame < SUPPLY_RAID_GAP ) || m_ai->isBaseUnderAttack() )
		return;
	std::vector<Object *> raiders;
	for( size_t o = 0; o < owned.size() && (Int)raiders.size() < RAID_MOST; ++o )
	{
		Object *obj = owned[ o ];
		if( freeFighter( obj ) && !obj->isKindOf( KINDOF_DISGUISER ) && innateStealth( obj->getTemplate() ) && waiting( obj ) &&
				m_ai->isAtHome( obj->getPosition() ) && !obj->testStatus( OBJECT_STATUS_DETECTED ) )
			raiders.push_back( obj );
	}
	if( (Int)raiders.size() < RAID_LEAST )
		return;

	// his detectors the AI knows of, and the first supply center of his none of them watches
	const Int me = m_ai->m_player->getPlayerIndex();
	static const NameKeyType detectorKey = NAMEKEY( "StealthDetectorUpdate" );
	std::vector<Object *> theirs;
	foe->iterateObjects( collectOwned, &theirs );
	std::vector<Coord3D> detectors;
	for( size_t t = 0; t < theirs.size(); ++t )
	{
		const Object *obj = theirs[ t ];
		const StealthDetectorUpdate *detector = (const StealthDetectorUpdate *)obj->findUpdateModule( detectorKey );
		if( detector && detector->isSDEnabled() && glaKnows( obj, me ) )
			detectors.push_back( *obj->getPosition() );
	}
	Coord3D road;
	road.zero();
	Bool found = FALSE;
	for( size_t t = 0; t < theirs.size() && !found; ++t )
	{
		const Object *center = theirs[ t ];
		if( !center->isKindOf( KINDOF_FS_SUPPLY_CENTER ) || !glaKnows( center, me ) )
			continue;
		Bool watched = FALSE;
		for( size_t d = 0; d < detectors.size() && !watched; ++d )
			watched = distSqr2D( &detectors[ d ], center->getPosition() ) <= sqr( DETECTOR_REACH );
		if( watched )
			continue;
		road = *center->getPosition();
		found = TRUE;
	}
	if( !found )
		return;

	// the side road: the straight line's middle bent out to its quieter side
	Coord3D from;
	from.zero();
	for( size_t r = 0; r < raiders.size(); ++r )
	{
		from.x += raiders[ r ]->getPosition()->x;
		from.y += raiders[ r ]->getPosition()->y;
	}
	from.x /= (Real)raiders.size();
	from.y /= (Real)raiders.size();
	const Real dx = road.x - from.x;
	const Real dy = road.y - from.y;
	Coord3D side[ 2 ];
	Real noise[ 2 ];
	const AIThreatMix mix = m_ai->readMix( AITHREAT_GROUND_MIX );
	for( Int s = 0; s < 2; ++s )
	{
		const Real sign = s == 0 ? 1.0f : -1.0f;
		side[ s ].x = from.x + 0.5f * dx - sign * RAID_SIDE * dy;
		side[ s ].y = from.y + 0.5f * dy + sign * RAID_SIDE * dx;
		side[ s ].z = 0.0f;
		noise[ s ] = m_ai->killZoneAt( &side[ s ] ) + ( m_ai->hasTacticalMap() ? m_ai->m_influence.enemyAt( side[ s ].x, side[ s ].y, mix ) : 0.0f );
		for( size_t d = 0; d < detectors.size(); ++d )
			if( distSqr2D( &detectors[ d ], &side[ s ] ) <= sqr( DETECTOR_REACH ) )
				noise[ s ] += 1000.0f;
	}
	const Int quiet = noise[ 1 ] < noise[ 0 ] ? 1 : 0;
	for( size_t r = 0; r < raiders.size(); ++r )
	{
		std::vector<Coord3D> legs;
		legs.push_back( side[ quiet ] );
		legs.push_back( road );
		raiders[ r ]->getAI()->aiFollowPath( &legs, NULL, CMD_FROM_AI );
	}
	m_raidFrame = now;
	DEBUG_LOG(("AI STEALTH frame %d player %d raids the supply center at (%.0f,%.0f) with %d unseen units by (%.0f,%.0f), %d detectors known\n",
		now, me, road.x, road.y, (Int)raiders.size(), side[ quiet ].x, side[ quiet ].y, (Int)detectors.size()));
}
