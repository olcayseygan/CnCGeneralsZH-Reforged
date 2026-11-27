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

// AIIdentityTurkiye.cpp
// Turkiye's own style: long reach and short sight.  AIIdentity.h has the hooks.
//
// The Firtina shells to 600 and sees 100, the Sakarya to 500, the Alpagu operator to 350 and sees 200; the
// Spotter sees 500.  So the side wins by looking first and shooting from where nothing shoots back: spotters
// go ahead and stand just outside the known guns' reach, the guns shell what they find (the shared
// shellDefence, which only ever shoots what this player knows of), a gun left with no line in front of it
// goes home instead of driving blind into the enemy, the recon flight looks at the enemy's front rather than
// his richest corner, and the Kargu drones go where his defences stand thickest.  Under it a blend of the
// Turtle and the Tech personality: fewer and bigger waves, a defence and its powers a little earlier.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"

class AIIdentityTurkiye : public AIIdentity
{
public:
	enum
	{
		MOVE_SPOTTERS_AHEAD	= 1,	///< spotters stand at the edge of the known defences, or lead their wave (doSpotters)
		MOVE_STAND_OFF			= 2,	///< a long gun whose wave lost its line goes home (doStandOff)
		MOVE_KARGU_MARKS		= 4,	///< the Kargu swarm and the SIHA munitions go at the thickest known defences (aimKargu)
		MOVE_RECON_FRONT		= 8,	///< the recon flight looks at the enemy's front, then his middle (aimRecon)
	};

	AIIdentityTurkiye( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ), m_reconShots( 0 ) {}

	virtual void onStart( void );
	virtual void onTick( void );
	virtual Int buildingScore( const ThingTemplate *building, Int shared ) const;
	virtual Real teamScore( const TeamPrototype *team, Real shared ) const;
	virtual Real wavePower( Real shared ) const;
	virtual UnsignedInt waveMaxHoldFrames( UnsignedInt shared ) const;
	virtual Bool powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );
	virtual void xfer( Xfer *xfer );

private:
	void doSpotters( const std::vector<KnownDefence> &defences );
	void doStandOff( void );
	Bool aimRecon( Int enemyIndex, Coord3D *target );
	Bool aimKargu( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target ) const;
	Bool enemyCentre( Int enemyIndex, Coord3D *centre, Real *extent ) const;
	/// no gun this player knows of, standing or moving, reaches a soldier at (x, y): its influence map is empty there
	Bool outOfReach( Real x, Real y ) const { return m_ai->m_influence.enemyAt( x, y, AITHREAT_INFANTRY_MIX ) <= 0.0f; }

	UnsignedInt		m_reconShots;		///< recon flights aimed so far: even ones at his front, odd ones at his middle
};

AIIdentity *createTurkiyeIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityTurkiye( ai, general );
}

// ---- the blend: what part of the Turtle and the Tech personality the side plays by, the rest Normal ----
static const Int BLEND_TURTLE_PERCENT = 20;
static const Int BLEND_TECH_PERCENT = 20;

static Real blendOf( Real AIPersonaWeights::*field )
{
	const Real turtle = INT_TO_REAL( BLEND_TURTLE_PERCENT ) / 100.0f;
	const Real tech = INT_TO_REAL( BLEND_TECH_PERCENT ) / 100.0f;
	return ( 1.0f - turtle - tech ) + turtle * ( aiPersonaWeights( AIPERSONA_TURTLE ).*field ) + tech * ( aiPersonaWeights( AIPERSONA_TECH ).*field );
}

/** A team rich in long guns and their eyes scores up by this at all of it, by cost (selectTeamToBuild's
	* counter weighs up to 20, the role 10). */
static const Real LONG_GUN_TEAM_SPAN = 10.0f;

// ---- the standing orders ----
static const Int TURKIYE_TICK_FRAMES = (Int)( 3 * LOGICFRAMES_PER_GAME_SECOND );
/** A spotter stands this far outside a known defence's reach: past where holdAtDefences parks a unit at the
	* edge (reach + 30 + 90), so the two never send it back and forth, and still near enough that its 500 sight
	* reaches well past the gun into the base behind it. */
static const Real LOOKOUT_SLACK = 130.0f;
/** ... a spot no other known defence reaches within this of. */
static const Real LOOKOUT_COVER_SLACK = 30.0f;
/** At most this many spotters stand at the known defences; the rest lead their waves. */
static const Int LOOKOUT_MAX = 3;
/** None goes to a lookout further than this from where it stands. */
static const Real LOOKOUT_MAX_WALK = 2500.0f;
/** A spotter with a wave and no known defence ahead walks this far in front of the wave's front unit. */
static const Real LOOKOUT_LEAD = 150.0f;
/** One this near its spot, or already walking to within this of it, is left alone. */
static const Real LOOKOUT_NEAR = 60.0f;
/** A long gun outreaches the line by this much (isSiegeGun's 300 in the shared AI). */
static const Real LONG_GUN_RANGE = 300.0f;

// ---- the powers ----
/** The Kargu swarm and the SIHA munitions go at his defences while the guns hold the attacks back, or at a
	* cluster of at least this many known ones at any time. */
static const Int KARGU_MIN_DEFENCES = 4;

/** How far an object shoots the ground, its longest gun (the shared AI's groundAttackRange). */
static Real groundReach( const Object *obj )
{
	Real best = 0.0f;
	for( Int slot = PRIMARY_WEAPON; slot < WEAPONSLOT_COUNT; ++slot )
	{
		const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
		if( weapon == NULL || ( weapon->getTemplate()->getAntiMask() & WEAPON_ANTI_GROUND ) == 0 )
			continue;
		const Real range = weapon->getAttackRange( obj );
		if( range > best )
			best = range;
	}
	return best;
}

/** A spotter: infantry with spotter sight that is not a hero (the Maroon Beret has the sight and fights). */
static Bool isSpotter( const Object *obj )
{
	return obj->getTemplate()->hasSpotterSight() && obj->isKindOf( KINDOF_INFANTRY ) && !obj->isKindOf( KINDOF_HERO );
}

/** A gun that shoots past its own sight and past the line: the Firtina, the Sakarya, the Alpagu operator. */
static Bool isLongGun( const Object *obj )
{
	return obj->getTemplate()->acquiresBeyondVision() && !obj->isKindOf( KINDOF_AIRCRAFT ) && groundReach( obj ) >= LONG_GUN_RANGE;
}

/** Every enemy building with a ground gun this player knows of (the shared collectKnownDefences), in object
	* list order, of one enemy's only when onlyOwner is not -1; m_power is its price and its garrison's. */
static void collectKnownDefences( const Player *player, Int onlyOwner, std::vector<KnownDefence> *defences )
{
	const Int me = player->getPlayerIndex();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || !( obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) ) ||
				obj->isKindOf( KINDOF_FS_SUPERWEAPON ) || player->getRelationship( obj->getTeam() ) != ENEMIES )
			continue;
		if( onlyOwner >= 0 && ( obj->getControllingPlayer() == NULL || obj->getControllingPlayer()->getPlayerIndex() != onlyOwner ) )
			continue;
		if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) || obj->isDisabled() )
			continue;
		Real reach = groundReach( obj );
		Real power = reach > 0.0f ? INT_TO_REAL( obj->getTemplate()->calcCostToBuild( obj->getControllingPlayer() ) ) : 0.0f;
		const ContainModuleInterface *contain = obj->getContain();
		if( contain && contain->isPassengerAllowedToFire() )
		{
			const ContainedItemsList *items = contain->getContainedItemsList();
			for( ContainedItemsList::const_iterator it = items->begin(); it != items->end(); ++it )
			{
				const Real range = groundReach( *it );
				if( range <= 0.0f )
					continue;
				power += INT_TO_REAL( (*it)->getTemplate()->calcCostToBuild( (*it)->getControllingPlayer() ) );
				if( range > reach )
					reach = range;
			}
		}
		if( power <= 0.0f || obj->isUnknownTo( me ) )
			continue;
		if( obj->testStatus( OBJECT_STATUS_STEALTHED ) && !obj->testStatus( OBJECT_STATUS_DETECTED ) )
			continue;
		KnownDefence defence;
		defence.m_object = obj;
		defence.m_reach = reach;
		defence.m_power = power;
		defences->push_back( defence );
	}
}

static void collectOwned( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

/** The computer's move order to a unit that may be busy, as a one-point path (the shared aiOrderMove). */
static void orderMove( AIUpdateInterface *ai, const Coord3D *pos )
{
	std::vector<Coord3D> leg( 1, *pos );
	ai->aiFollowPath( &leg, NULL, CMD_FROM_AI );
}

/** Whether a known defence other than skip reaches within slack of a spot. */
static Bool coveredByOther( const std::vector<KnownDefence> &defences, size_t skip, Real x, Real y, Real slack )
{
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		if( d != skip && sqr( at->x - x ) + sqr( at->y - y ) <= sqr( defences[ d ].m_reach + slack ) )
			return TRUE;
	}
	return FALSE;
}

/** A known defence's row in doSpotters' order: nearest our base first, the object list's order on a tie. */
struct DefenceByHome
{
	Real distSqr;
	size_t index;
	bool operator<( const DefenceByHome &o ) const { return distSqr < o.distSqr || ( distSqr == o.distSqr && index < o.index ); }
};

//-------------------------------------------------------------------------------------------------
void AIIdentityTurkiye::onStart( void )
{
	drawMove( MOVE_SPOTTERS_AHEAD, 85 );
	drawMove( MOVE_STAND_OFF, 70 );
	drawMove( MOVE_KARGU_MARKS, 75 );
	drawMove( MOVE_RECON_FRONT, 100 );
	DEBUG_LOG(("AI IDENTITY frame %d player %d '%s' moves 0x%x\n", TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(),
		m_ai->m_player->getSide().str(), m_moves));
}

void AIIdentityTurkiye::onTick( void )
{
	const Int phase = ( m_ai->m_player->getPlayerIndex() * 7 ) % TURKIYE_TICK_FRAMES;
	if( ( (Int)TheGameLogic->getFrame() + phase ) % TURKIYE_TICK_FRAMES != 0 || !m_ai->m_baseCenterSet )
		return;
	if( hasMove( MOVE_SPOTTERS_AHEAD ) )
	{
		std::vector<KnownDefence> defences;
		collectKnownDefences( m_ai->m_player, -1, &defences );
		doSpotters( defences );
	}
	if( hasMove( MOVE_STAND_OFF ) )
		doStandOff();
}

void AIIdentityTurkiye::xfer( Xfer *xfer )
{
	AIIdentity::xfer( xfer );
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferUnsignedInt( &m_reconShots );
}

//-------------------------------------------------------------------------------------------------
/** Send the spotters ahead.  The guns see 100 and shoot 600, and the AI knew one or two of a walled base's
	* defences through the fog, so the shells had nothing to go at.  A spotter out with a wave, or any spotter
	* while the known guns hold the attacks back, takes the known defence nearest our base that no other
	* spotter has, and stands on our side of it, LOOKOUT_SLACK outside its reach, at a spot (turned up to 60
	* degrees either way) no other known gun reaches, a Sakarya's 557 included: on the first build the tactics
	* stepped a spotter back from one every few seconds and this sent it in again.  What it sees, the guns shell.  One with no
	* lookout left walks LOOKOUT_LEAD in front of its wave's front unit, so the wave sees before it is seen. */
void AIIdentityTurkiye::doSpotters( const std::vector<KnownDefence> &defences )
{
	AIPlayer *ai = m_ai;
	const Coord3D base = ai->m_baseCenter;
	const Bool gunHold = ai->m_gunHoldSince != 0;

	// the defences in front, nearest our base first; one inside our base is the guard's to fight
	std::vector<DefenceByHome> front;
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		DefenceByHome row;
		row.distSqr = sqr( at->x - base.x ) + sqr( at->y - base.y );
		row.index = d;
		if( row.distSqr > sqr( ai->m_baseRadius ) )
			front.push_back( row );
	}
	std::sort( front.begin(), front.end() );
	std::vector<Bool> taken( defences.size(), FALSE );

	Coord3D enemyAt;
	enemyAt.zero();
	Real extent = 0.0f;
	Player *enemy = ai->peekAiEnemy();
	const Bool haveEnemy = enemy != NULL && enemyCentre( enemy->getPlayerIndex(), &enemyAt, &extent );

	std::vector<Object *> owned;
	ai->m_player->iterateObjects( collectOwned, &owned );
	Int lookouts = 0;
	Int sentLook = 0;
	Int sentLead = 0;
	// the turn the lookout is looked for at: straight on, then 30 and 60 degrees either way
	static const Real TURN_COS[] = { 1.0f, 0.8660254f, 0.8660254f, 0.5f, 0.5f };
	static const Real TURN_SIN[] = { 0.0f, 0.5f, -0.5f, 0.8660254f, -0.8660254f };
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		AIUpdateInterface *unitAI = obj->getAI();
		if( obj->isEffectivelyDead() || unitAI == NULL || obj->isContained() || !isSpotter( obj ) || obj->isDisabled() )
			continue;
		const Coord3D *pos = obj->getPosition();
		AIGroup *group = obj->getGroup();
		if( !gunHold && ( group == NULL || ai->isAtHome( pos ) ) )
			continue;		// a spotter in a team still gathering at home goes with it
		const AIPlayer::TacticalStep *step = ai->findTacticalStep( obj->getID() );
		if( step && ( step->resumeFrame != 0 || step->fallingBack ) )
			continue;		// the tactics are stepping it out of a reach, or doFallback pulled it out

		Coord3D spot;
		spot.zero();
		Bool have = FALSE;
		Bool lookout = FALSE;
		for( size_t f = 0; f < front.size() && !have && lookouts < LOOKOUT_MAX; ++f )
		{
			const size_t d = front[ f ].index;
			if( taken[ d ] )
				continue;
			const Coord3D *at = defences[ d ].m_object->getPosition();
			Real dx = base.x - at->x;
			Real dy = base.y - at->y;
			const Real len = (Real)sqrt( dx * dx + dy * dy );
			if( len < 1.0f )
				continue;
			dx /= len;
			dy /= len;
			const Real out = defences[ d ].m_reach + LOOKOUT_SLACK;
			for( Int t = 0; t < 5 && !have; ++t )
			{
				const Real x = at->x + out * ( dx * TURN_COS[ t ] - dy * TURN_SIN[ t ] );
				const Real y = at->y + out * ( dy * TURN_COS[ t ] + dx * TURN_SIN[ t ] );
				if( coveredByOther( defences, d, x, y, LOOKOUT_COVER_SLACK ) || sqr( x - pos->x ) + sqr( y - pos->y ) > sqr( LOOKOUT_MAX_WALK ) ||
						!outOfReach( x, y ) )
					continue;
				spot.x = x;
				spot.y = y;
				have = TRUE;
			}
			if( have )
			{
				taken[ d ] = TRUE;
				lookout = TRUE;
				++lookouts;
			}
		}
		if( !have && group != NULL && haveEnemy )
		{
			// the front of its wave: the line unit farthest from home
			const Object *lead = NULL;
			Real leadSqr = 0.0f;
			const VecObjectID &ids = group->getAllIDs();
			for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
			{
				const Object *mate = TheGameLogic->findObjectByID( *it );
				if( mate == NULL || mate == obj || mate->isEffectivelyDead() || mate->isContained() || mate->isKindOf( KINDOF_AIRCRAFT ) ||
						isSpotter( mate ) || isLongGun( mate ) || groundReach( mate ) <= 0.0f )
					continue;
				const Real d = sqr( mate->getPosition()->x - base.x ) + sqr( mate->getPosition()->y - base.y );
				if( lead == NULL || d > leadSqr )
				{
					lead = mate;
					leadSqr = d;
				}
			}
			if( lead != NULL && !ai->isAtHome( lead->getPosition() ) )
			{
				const Real dx = enemyAt.x - lead->getPosition()->x;
				const Real dy = enemyAt.y - lead->getPosition()->y;
				const Real len = (Real)sqrt( dx * dx + dy * dy );
				if( len > LOOKOUT_LEAD )
				{
					spot.x = lead->getPosition()->x + dx / len * LOOKOUT_LEAD;
					spot.y = lead->getPosition()->y + dy / len * LOOKOUT_LEAD;
					have = !coveredByOther( defences, defences.size(), spot.x, spot.y, LOOKOUT_SLACK ) && outOfReach( spot.x, spot.y );
				}
			}
		}
		if( !have || sqr( spot.x - pos->x ) + sqr( spot.y - pos->y ) <= sqr( LOOKOUT_NEAR ) )
			continue;
		const Coord3D *goal = unitAI->getGoalPosition();
		if( unitAI->isMoving() && goal && sqr( goal->x - spot.x ) + sqr( goal->y - spot.y ) <= sqr( LOOKOUT_NEAR ) )
			continue;		// on its way: sent again it would stop for a new path every pass
		spot.z = TheTerrainLogic->getGroundHeight( spot.x, spot.y );
		orderMove( unitAI, &spot );
		if( lookout )
			++sentLook;
		else
			++sentLead;
	}
	if( sentLook + sentLead > 0 )
		DEBUG_LOG(("AI TURKIYE SPOT frame %d player %d sends %d spotters to the edge of %d known defences, %d ahead of their waves%s\n",
			TheGameLogic->getFrame(), ai->m_player->getPlayerIndex(), sentLook, (Int)front.size(), sentLead, gunHold ? ", guns holding" : ""));
}

//-------------------------------------------------------------------------------------------------
/** Never close the distance.  keepSiegeBehind trails a long gun behind its wave's front unit, and leaves it
	* alone once the wave has no line unit left: a Firtina that sees 100 then drove on to the wave's goal
	* alone and met the enemy's tanks at their range rather than its own.  One whose wave has no line left,
	* away from home and not firing, goes home to join the next. */
void AIIdentityTurkiye::doStandOff( void )
{
	AIPlayer *ai = m_ai;
	std::vector<Object *> owned;
	ai->m_player->iterateObjects( collectOwned, &owned );
	Int sent = 0;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *gun = owned[ o ];
		AIUpdateInterface *gunAI = gun->getAI();
		if( gun->isEffectivelyDead() || gunAI == NULL || gun->isContained() || gun->getGroup() == NULL || !isLongGun( gun ) ||
				gunAI->isAttacking() || ai->isAtHome( gun->getPosition() ) || ai->isFallingBack( gun->getID() ) )
			continue;
		Bool line = FALSE;
		const VecObjectID &ids = gun->getGroup()->getAllIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end() && !line; ++it )
		{
			const Object *mate = TheGameLogic->findObjectByID( *it );
			line = mate != NULL && mate != gun && !mate->isEffectivelyDead() && !mate->isContained() && !mate->isKindOf( KINDOF_AIRCRAFT ) &&
				!isSpotter( mate ) && !isLongGun( mate ) && groundReach( mate ) > 0.0f;
		}
		if( line )
			continue;
		const Coord3D *goal = gunAI->getGoalPosition();
		if( gunAI->isMoving() && goal && ai->isAtHome( goal ) )
			continue;		// already going
		Coord3D home = ai->m_baseCenter;
		home.z = TheTerrainLogic->getGroundHeight( home.x, home.y );
		orderMove( gunAI, &home );
		++sent;
	}
	if( sent > 0 )
		DEBUG_LOG(("AI TURKIYE STANDOFF frame %d player %d sends %d long guns home, their waves have no line left\n",
			TheGameLogic->getFrame(), ai->m_player->getPlayerIndex(), sent));
}

//-------------------------------------------------------------------------------------------------
/** The middle of what this player knows of an enemy's buildings, and half the smaller side of their box. */
Bool AIIdentityTurkiye::enemyCentre( Int enemyIndex, Coord3D *centre, Real *extent ) const
{
	Region2D bounds;
	m_ai->getPlayerStructureBounds( &bounds, enemyIndex, FALSE, m_ai->m_player->getPlayerIndex() );
	if( bounds.hi.x == 0.0f && bounds.lo.x == 0.0f && bounds.hi.y == 0.0f && bounds.lo.y == 0.0f )
		return FALSE;
	centre->x = ( bounds.lo.x + bounds.hi.x ) * 0.5f;
	centre->y = ( bounds.lo.y + bounds.hi.y ) * 0.5f;
	centre->z = 0.0f;
	*extent = 0.5f * min( bounds.width(), bounds.height() );
	return TRUE;
}

Bool AIIdentityTurkiye::powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	const SpecialPowerType type = power->getSpecialPowerType();
	if( type == SPECIAL_SPY_SATELLITE && hasMove( MOVE_RECON_FRONT ) )
		return aimRecon( enemyIndex, target );
	if( ( type == SPECIAL_SCUD_STORM || type == SPECIAL_ARTILLERY_BARRAGE ) && hasMove( MOVE_KARGU_MARKS ) )
		return aimKargu( power, enemyIndex, radius, target );
	return FALSE;
}

/** The recon flight (the spy satellite's enum) was aimed by the shared AI at his richest spot, which is the
	* spot it already knew best.  The guns need his front: the edge of his buildings on our side, where a
	* turtle's ring stands, and every second flight his middle. */
Bool AIIdentityTurkiye::aimRecon( Int enemyIndex, Coord3D *target )
{
	Coord3D centre;
	Real extent = 0.0f;
	if( !m_ai->m_baseCenterSet || !enemyCentre( enemyIndex, &centre, &extent ) )
		return FALSE;
	Coord3D aim = centre;
	const Bool atFront = ( m_reconShots % 2 ) == 0;
	if( atFront )
	{
		const Real dx = m_ai->m_baseCenter.x - centre.x;
		const Real dy = m_ai->m_baseCenter.y - centre.y;
		const Real len = (Real)sqrt( dx * dx + dy * dy );
		if( len > 1.0f )
		{
			const Real out = min( extent * 0.8f, len * 0.5f );
			aim.x += dx / len * out;
			aim.y += dy / len * out;
		}
	}
	aim.z = TheTerrainLogic->getGroundHeight( aim.x, aim.y );
	*target = aim;
	++m_reconShots;
	DEBUG_LOG(("AI TURKIYE RECON frame %d player %d looks at player %d's %s, (%.0f,%.0f)\n", TheGameLogic->getFrame(),
		m_ai->m_player->getPlayerIndex(), enemyIndex, atFront ? "front" : "middle", aim.x, aim.y));
	return TRUE;
}

/** The Kargu swarm and the SIHA munitions at his defences: the known one with the most known defence round
	* it within the power's radius (by price, a tie to the one nearer our base), at their centre by price.  Only
	* while the guns hold the attacks back or the cluster is at least KARGU_MIN_DEFENCES strong; otherwise the
	* shared aim at his army and his money.  The swarm leaves 350 of sight behind it for 30 seconds, and the
	* long guns shell what it shows. */
Bool AIIdentityTurkiye::aimKargu( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target ) const
{
	if( !m_ai->m_baseCenterSet )
		return FALSE;
	std::vector<KnownDefence> defences;
	collectKnownDefences( m_ai->m_player, enemyIndex, &defences );
	const Coord3D &base = m_ai->m_baseCenter;
	Int best = -1;
	Real bestPower = 0.0f;
	Real bestHomeSqr = 0.0f;
	Int bestCount = 0;
	for( size_t d = 0; d < defences.size(); ++d )
	{
		const Coord3D *at = defences[ d ].m_object->getPosition();
		Real sum = 0.0f;
		Int count = 0;
		for( size_t e = 0; e < defences.size(); ++e )
		{
			const Coord3D *other = defences[ e ].m_object->getPosition();
			if( sqr( other->x - at->x ) + sqr( other->y - at->y ) <= sqr( radius ) )
			{
				sum += defences[ e ].m_power;
				++count;
			}
		}
		const Real homeSqr = sqr( at->x - base.x ) + sqr( at->y - base.y );
		if( best < 0 || sum > bestPower || ( sum == bestPower && homeSqr < bestHomeSqr ) )
		{
			best = (Int)d;
			bestPower = sum;
			bestHomeSqr = homeSqr;
			bestCount = count;
		}
	}
	const Bool gunHold = m_ai->m_gunHoldSince != 0;
	if( best < 0 || bestPower <= 0.0f || ( !gunHold && bestCount < KARGU_MIN_DEFENCES ) )
		return FALSE;
	const Coord3D *at = defences[ best ].m_object->getPosition();
	Coord3D aim;
	aim.zero();
	for( size_t e = 0; e < defences.size(); ++e )
	{
		const Coord3D *other = defences[ e ].m_object->getPosition();
		if( sqr( other->x - at->x ) + sqr( other->y - at->y ) <= sqr( radius ) )
		{
			aim.x += other->x * defences[ e ].m_power;
			aim.y += other->y * defences[ e ].m_power;
		}
	}
	aim.x /= bestPower;
	aim.y /= bestPower;
	aim.z = TheTerrainLogic->getGroundHeight( aim.x, aim.y );
	*target = aim;
	DEBUG_LOG(("AI TURKIYE KARGU frame %d player %d aims '%s' at %d known defences worth %.0f round (%.0f,%.0f)%s\n",
		TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(), power->getName().str(), bestCount, bestPower, aim.x, aim.y,
		gunHold ? ", guns holding" : ""));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The blend's purchase leanings on the shared score: the Turtle's defences, the Tech's superweapon and
	* upgrades, both a little less army.  A shared score of 0 is the plan picked in order rather than by
	* score, and stays 0. */
Int AIIdentityTurkiye::buildingScore( const ThingTemplate *building, Int shared ) const
{
	if( shared == 0 )
		return shared;
	const Int kind = (Int)m_ai->purchaseKindOf( building );
	if( kind < 0 || kind >= AIBUY_KIND_COUNT )
		return shared;
	return shared + ( BLEND_TURTLE_PERCENT * aiPersonaWeights( AIPERSONA_TURTLE ).m_buy[ kind ] +
		BLEND_TECH_PERCENT * aiPersonaWeights( AIPERSONA_TECH ).m_buy[ kind ] ) / 100;
}

/** The blend's taste for defence teams, and a team's share of long guns and spotters by cost at
	* LONG_GUN_TEAM_SPAN.  America's scripts name the Tomahawk; the factory makes the Firtina. */
Real AIIdentityTurkiye::teamScore( const TeamPrototype *team, Real shared ) const
{
	const TeamTemplateInfo *info = team->getTemplateInfo();
	Real score = shared;
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		score += ( INT_TO_REAL( BLEND_TURTLE_PERCENT ) * aiPersonaWeights( AIPERSONA_TURTLE ).m_defenceTeams +
			INT_TO_REAL( BLEND_TECH_PERCENT ) * aiPersonaWeights( AIPERSONA_TECH ).m_defenceTeams ) / 100.0f;
	Real total = 0.0f;
	Real longGuns = 0.0f;
	const Player *owner = m_ai->m_player;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( tmpl == NULL )
			continue;			// a map's team naming a unit this game does not have
		const ThingTemplate *own = TheThingFactory->findSideReskin( tmpl, owner->getSide() );
		if( own != NULL )
			tmpl = own;
		const Int minUnits = info->m_unitsInfo[ i ].minUnits;
		const Real worth = INT_TO_REAL( minUnits > 1 ? minUnits : 1 ) * INT_TO_REAL( tmpl->calcCostToBuild( owner ) );
		total += worth;
		if( tmpl->acquiresBeyondVision() || ( tmpl->hasSpotterSight() && tmpl->isKindOf( KINDOF_INFANTRY ) && !tmpl->isKindOf( KINDOF_HERO ) ) )
			longGuns += worth;
	}
	if( total > 0.0f )
		score += LONG_GUN_TEAM_SPAN * longGuns / total;
	return score;
}

/** The blend's waves: the Turtle waits for 1.8 times the power and the Tech 1.3, so a fifth of each is 1.22
	* times the shared wave, and the longest hold 1.16 times. */
Real AIIdentityTurkiye::wavePower( Real shared ) const
{
	return shared * blendOf( &AIPersonaWeights::m_wavePower );
}

UnsignedInt AIIdentityTurkiye::waveMaxHoldFrames( UnsignedInt shared ) const
{
	return (UnsignedInt)( INT_TO_REAL( shared ) * blendOf( &AIPersonaWeights::m_waveHold ) );
}
