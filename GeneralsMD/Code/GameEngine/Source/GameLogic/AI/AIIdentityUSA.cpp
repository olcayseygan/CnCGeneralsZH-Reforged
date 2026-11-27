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

// AIIdentityUSA.cpp
// The USA generals' own styles: USA, Laser, Superweapon and Air Force.  AIIdentity.h has the hooks.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/BuildAssistant.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BattlePlanUpdate.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"

class AIIdentityUSA : public AIIdentity
{
public:
	enum
	{
		MOVE_BATTLE_PLANS = 1,		///< USA: the Strategy Center's plan follows the war (doBattlePlans)
		MOVE_PLANT_GUARD = 2,			///< Laser: a laser turret beside each power plant, the front one first (doPlantGuard)
		MOVE_FORWARD_LASERS = 4,	///< Laser: a laser turret behind each wave's front as it moves out (doForwardLasers)
		MOVE_ARMY_CANNON = 8,			///< Superweapon: the Particle Cannon at the biggest army it knows (aimAtArmy)
		MOVE_FORTRESS = 16,				///< Superweapon: EMP Patriots past the shared allowance, out of the surplus (doFortress)
		MOVE_BIG_WAVES = 32,			///< Superweapon: waves half a Turtle's size and wait, so fewer and bigger
		MOVE_AIR_TASTE = 64,			///< Air Force: aircraft teams first, less so against known anti-air
		MOVE_CLEAR_AA = 128,			///< Air Force: idle jets strike the known anti-air at the wave's front (doClearAntiAir)
		MOVE_CARPET_ARMY = 256,		///< Air Force: the Carpet Bomb at the biggest army it knows (aimAtArmy)
		MOVE_HELI_MEND = 512,			///< Air Force: a hurt helicopter goes to be mended sooner (pullHealth)
	};

	AIIdentityUSA( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ), m_planWaveFrame( 0 ), m_planWanted( PLANSTATUS_NONE ),
		m_planSwitchFrame( 0 ), m_laserFrontFrame( 0 ), m_airFrontFrame( 0 ) {}

	virtual void onStart( void );
	virtual void onTick( void );
	virtual Real teamScore( const TeamPrototype *team, Real shared ) const;
	virtual Real wavePower( Real shared ) const;
	virtual UnsignedInt waveMaxHoldFrames( UnsignedInt shared ) const;
	virtual void onWaveSent( AIGroup *wave, Real power );
	virtual Bool powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );
	virtual Real pullHealth( const Object *unit, Real shared ) const;
	virtual void xfer( Xfer *xfer );

private:
	void doBattlePlans( void );
	void doPlantGuard( void );
	void doForwardLasers( void );
	void doFortress( void );
	void doClearAntiAir( void );
	Bool aimAtArmy( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );
	const ThingTemplate *buildableDefence( const char *name, Int *cost, Real reserveShare );

	UnsignedInt		m_planWaveFrame;			///< the last wave sent, 0 for none
	Int						m_planWanted;					///< the plan (PLANSTATUS_*) that wave wants
	UnsignedInt		m_planSwitchFrame;		///< the last plan change ordered, 0 for none
	UnsignedInt		m_laserFrontFrame;		///< the wave front (AIPlayer::m_shuttleFrontFrame) a forward turret was last looked for at, 0 for none
	UnsignedInt		m_airFrontFrame;			///< the wave front the jets were last sent ahead of, 0 for none
	std::vector<ObjectID> m_waveJets;		///< the jets the waves sent since that front took, for doClearAntiAir
};

AIIdentity *createUSAIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityUSA( ai, general );
}

/** Each general's moves, and the chance a match plays each.  A chance under 100 is one logic-stream draw. */
void AIIdentityUSA::onStart( void )
{
	switch( m_general )
	{
		case AIGENERAL_USA:
			drawMove( MOVE_BATTLE_PLANS, 100 );
			break;
		case AIGENERAL_USA_LASER:
			drawMove( MOVE_PLANT_GUARD, 100 );
			drawMove( MOVE_FORWARD_LASERS, 75 );
			break;
		case AIGENERAL_USA_SUPERWEAPON:
			drawMove( MOVE_ARMY_CANNON, 100 );
			drawMove( MOVE_FORTRESS, 100 );
			drawMove( MOVE_BIG_WAVES, 75 );
			break;
		case AIGENERAL_USA_AIR_FORCE:
			drawMove( MOVE_AIR_TASTE, 100 );
			drawMove( MOVE_CARPET_ARMY, 100 );
			drawMove( MOVE_HELI_MEND, 100 );
			drawMove( MOVE_CLEAR_AA, 75 );
			break;
		default:
			break;
	}
	DEBUG_LOG(("AI IDENTITY frame %d player %d '%s' moves 0x%x\n", TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(),
		m_ai->m_player->getSide().str(), m_moves));
}

void AIIdentityUSA::onTick( void )
{
	doBattlePlans();
	doPlantGuard();
	doForwardLasers();
	doFortress();
	doClearAntiAir();
}

void AIIdentityUSA::xfer( Xfer *xfer )
{
	AIIdentity::xfer( xfer );
	// 2: the wave fronts the forward lasers and the jets last answered
	// 3: the jets the last waves took
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferUnsignedInt( &m_planWaveFrame );
	xfer->xferInt( &m_planWanted );
	xfer->xferUnsignedInt( &m_planSwitchFrame );
	if( version >= 2 )
	{
		xfer->xferUnsignedInt( &m_laserFrontFrame );
		xfer->xferUnsignedInt( &m_airFrontFrame );
	}
	if( version >= 3 )
		xfer->xferSTLObjectIDVector( &m_waveJets );
	else if( xfer->getXferMode() == XFER_LOAD )
		m_waveJets.clear();
}

//----------------------------------------------------------------------------------------------------------
// shared helpers
//----------------------------------------------------------------------------------------------------------

static void collectAll( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

/** AIPlayer.cpp's observerKnowsAbout, short: in sight or remembered, and not hidden and undetected. */
static Bool knownTo( const Object *obj, Int observer )
{
	return !obj->isUnknownTo( observer ) && ( !obj->testStatus( OBJECT_STATUS_STEALTHED ) || obj->testStatus( OBJECT_STATUS_DETECTED ) );
}

/** AIPlayer.cpp's defenseReach: the longest weapon of the template's plain set. */
static Real templateReach( const ThingTemplate *tmpl )
{
	Real reach = 0.0f;
	const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
	for( Int slot = 0; set && slot < WEAPONSLOT_COUNT; ++slot )
	{
		const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
		if( weapon && weapon->getUnmodifiedAttackRange() > reach )
			reach = weapon->getUnmodifiedAttackRange();
	}
	return reach > 0.0f ? reach : 150.0f;
}

/** AIPlayer.cpp's priorityBuildPending: one asked for and not yet started. */
static Bool buildPending( Player *player, const ThingTemplate *tmpl )
{
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
		if( info->isPriorityBuild() && info->isBuildable() && info->getObjectID() == INVALID_ID && info->getTemplateName() == tmpl->getName() )
			return TRUE;
	return FALSE;
}

/** The named defence when a dozer of ours can put it up now and the bank holds its price over this share of what
	* the bank keeps (bankTarget).  The whole target was too much: at Elo 1800 the bank sat under it nearly all
	* match, and of four Laser matches only the opening's 8,000 ever paid for a turret.  Never in the opening
	* either: the first plant guard took 1,000 of the starting 8,000 at frame 597 in every match. */
#define IDENTITY_BUILD_START_FRAMES ( (UnsignedInt)( 150 * LOGICFRAMES_PER_GAME_SECOND ) )

const ThingTemplate *AIIdentityUSA::buildableDefence( const char *name, Int *cost, Real reserveShare )
{
	Player *player = m_ai->m_player;
	if( TheGameLogic->getFrame() < IDENTITY_BUILD_START_FRAMES || !player->getCanBuildBase() || !m_ai->m_baseCenterSet ||
			m_ai->isBaseUnderAttack() )
		return NULL;
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( name, FALSE );
	if( tmpl == NULL || buildPending( player, tmpl ) )
		return NULL;
	*cost = tmpl->calcCostToBuild( player );
	if( (Int)player->getMoney()->countMoney() < REAL_TO_INT( reserveShare * INT_TO_REAL( m_ai->bankTarget() ) ) + *cost )
		return NULL;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
		if( owned[ o ]->isKindOf( KINDOF_DOZER ) && !owned[ o ]->isEffectivelyDead() &&
				TheBuildAssistant->canMakeUnit( owned[ o ], tmpl ) == CANMAKE_OK )
			return tmpl;
	return NULL;
}

//----------------------------------------------------------------------------------------------------------
// Laser General: power first, then the turrets follow the front
//----------------------------------------------------------------------------------------------------------

static const char *LASER_TURRET = "Lazr_AmericaPatriotBattery";
#define LASER_CHECK_FRAMES ( (UnsignedInt)( 10 * LOGICFRAMES_PER_GAME_SECOND ) )
/** A plant counts as guarded with a defence of ours (or one asked for) within the turret's reach of it.  At six
	* tenths, the ring search's own outer ring, a turret it had put 270 from its plant counted as no guard. */
static const Real PLANT_GUARD_REACH = 1.0f;
/** The share of the bank target each building waits above, besides its price (buildableDefence). */
static const Real LASER_RESERVE = 0.5f;
static const Real FORTRESS_RESERVE = 0.5f;
/** A wave front this fresh still gets its turret, and one this far from it already covers it. */
#define FORWARD_LASER_FRAMES ( (UnsignedInt)( 20 * LOGICFRAMES_PER_GAME_SECOND ) )
static const Real FORWARD_LASER_COVER_REACH = 1.5f;
/** The turret stands this share of its reach back from the front, on the home side. */
static const Real FORWARD_LASER_BACK_REACH = 0.5f;
static const Int FORWARD_LASER_MAX = 4;

/** Laser turrets go dark without power, and a plant is what a raid takes first.  Each pass one plant that has no
	* defence near it gets a turret beside it, the one nearest the enemy first, paid from the surplus. */
void AIIdentityUSA::doPlantGuard( void )
{
	Player *player = m_ai->m_player;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !hasMove( MOVE_PLANT_GUARD ) || ( now + player->getPlayerIndex() ) % LASER_CHECK_FRAMES != 0 )
		return;
	Int cost = 0;
	const ThingTemplate *turret = buildableDefence( LASER_TURRET, &cost, LASER_RESERVE );
	if( turret == NULL )
		return;
	const Real guardSqr = sqr( PLANT_GUARD_REACH * templateReach( turret ) );

	std::vector<Coord3D> guards;
	std::vector<Object *> plants;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() )
			continue;
		if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			guards.push_back( *obj->getPosition() );
		else if( obj->isKindOf( KINDOF_FS_POWER ) && !obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			plants.push_back( obj );
	}
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
	{
		const ThingTemplate *tmpl = info->isPriorityBuild() && info->getObjectID() == INVALID_ID ? TheThingFactory->findTemplate( info->getTemplateName(), FALSE ) : NULL;
		if( tmpl && tmpl->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			guards.push_back( *info->getLocation() );
	}

	Coord3D enemyPos = m_ai->m_baseCenter;
	Player *enemy = m_ai->peekAiEnemy();		// getAiEnemy may pick again, and that is the shared AI's to time
	const Bool enemyKnown = enemy && m_ai->enemyStartGuess( enemy->getPlayerIndex(), &enemyPos );
	Object *pick = NULL;
	Real pickSqr = 0.0f;
	for( size_t p = 0; p < plants.size(); ++p )
	{
		const Coord3D *at = plants[ p ]->getPosition();
		Bool guarded = FALSE;
		for( size_t g = 0; g < guards.size() && !guarded; ++g )
			guarded = sqr( guards[ g ].x - at->x ) + sqr( guards[ g ].y - at->y ) <= guardSqr;
		if( guarded )
			continue;
		const Real d = enemyKnown ? sqr( enemyPos.x - at->x ) + sqr( enemyPos.y - at->y ) : 0.0f;
		if( pick == NULL || d < pickSqr )
		{
			pick = plants[ p ];
			pickSqr = d;
		}
	}
	if( pick == NULL )
		return;
	Coord3D at = *pick->getPosition();
	// the fire-field search round the plant, and the nearest legal ground when the plant stands behind the base
	// with no field of fire towards the enemy
	const Bool placed = m_ai->placeDefenseAround( turret, &at ) ||
		m_ai->placeNear( turret, &at, pick->getTemplate()->getTemplateGeometryInfo().getBoundingCircleRadius(), FALSE );
	DEBUG_LOG(("AI LASER frame %d player %d %s a turret by its plant %d at (%.0f,%.0f), %d plants, %d guards, bank %d\n", now,
		player->getPlayerIndex(), placed ? "asks for" : "finds no room for", pick->getID(), at.x, at.y, (Int)plants.size(),
		(Int)guards.size(), (Int)player->getMoney()->countMoney()));
}

/** Each wave's front (computeShuttleFront: along its road, short of every gun this AI knows) gets a laser turret
	* half its reach behind it, unless one of ours already covers the spot: the line moves out with the army, and
	* a wave pushed back falls back onto its own guns.  At most FORWARD_LASER_MAX stand away from home. */
void AIIdentityUSA::doForwardLasers( void )
{
	const UnsignedInt front = m_ai->m_shuttleFrontFrame;
	if( !hasMove( MOVE_FORWARD_LASERS ) || front == 0 || front == m_laserFrontFrame )
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	Player *player = m_ai->m_player;
	if( now - front > FORWARD_LASER_FRAMES )
	{
		m_laserFrontFrame = front;
		return;
	}
	if( ( now + player->getPlayerIndex() ) % ( LASER_CHECK_FRAMES / 4 ) != 0 )
		return;
	Int cost = 0;
	const ThingTemplate *turret = buildableDefence( LASER_TURRET, &cost, LASER_RESERVE );
	if( turret == NULL )
		return;		// tried again on the next pass while the front is fresh
	m_laserFrontFrame = front;
	const Coord3D *base = &m_ai->m_baseCenter;
	Coord3D at = m_ai->m_shuttleFront;
	if( m_ai->isAtHome( &at ) )
		return;
	const Real reach = templateReach( turret );
	const Real out = (Real)sqrt( sqr( at.x - base->x ) + sqr( at.y - base->y ) );
	at.x -= ( at.x - base->x ) / out * FORWARD_LASER_BACK_REACH * reach;
	at.y -= ( at.y - base->y ) / out * FORWARD_LASER_BACK_REACH * reach;
	at.z = TheTerrainLogic->getGroundHeight( at.x, at.y );

	Int away = 0;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		const Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			continue;
		const Coord3D *pos = obj->getPosition();
		if( sqr( pos->x - at.x ) + sqr( pos->y - at.y ) <= sqr( FORWARD_LASER_COVER_REACH * reach ) )
			return;		// covered already
		if( !m_ai->isAtHome( pos ) )
			++away;
	}
	if( away >= FORWARD_LASER_MAX )
		return;
	// the front stops short of the guns this AI knows; the turret's own reach may still touch one
	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	for( size_t g = 0; g < guns.size(); ++g )
		if( sqr( guns[ g ].m_x - at.x ) + sqr( guns[ g ].m_y - at.y ) <= sqr( guns[ g ].m_reach + 50.0f ) )
			return;
	const Bool placed = m_ai->placeDefenseAround( turret, &at ) || m_ai->placeNear( turret, &at, 0.0f, FALSE );
	DEBUG_LOG(("AI LASER frame %d player %d %s a forward turret at (%.0f,%.0f), %.0f out, %d away from home, bank %d\n", now,
		player->getPlayerIndex(), placed ? "asks for" : "finds no room for", at.x, at.y, out, away, (Int)player->getMoney()->countMoney()));
}

//----------------------------------------------------------------------------------------------------------
// Superweapon General: a fortress that strikes the army, and seldom walks out
//----------------------------------------------------------------------------------------------------------

static const char *EMP_PATRIOT = "SupW_AmericaPatriotBattery";
/** The guns the fortress stands to, in shares of the shared allowance (aiDefenseAllowance and the persona's
	* row), and a few over it from the start. */
static const Real FORTRESS_SHARE = 1.5f;
static const Int FORTRESS_EXTRA = 2;
/** Half the way to a Turtle's waves (1.8 the power, 1.6 the wait). */
static const Real BIG_WAVE_POWER = 1.4f;
static const Real BIG_WAVE_HOLD = 1.3f;

void AIIdentityUSA::doFortress( void )
{
	Player *player = m_ai->m_player;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !hasMove( MOVE_FORTRESS ) || ( now + player->getPlayerIndex() ) % LASER_CHECK_FRAMES != 0 )
		return;
	Int cost = 0;
	const ThingTemplate *patriot = buildableDefence( EMP_PATRIOT, &cost, FORTRESS_RESERVE );
	if( patriot == NULL )
		return;
	Int army = 0;
	Int defences = 0;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		const Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() )
			continue;
		if( obj->isKindOf( KINDOF_FS_BASE_DEFENSE ) )
			++defences;
		else if( !obj->isKindOf( KINDOF_STRUCTURE ) && !obj->isKindOf( KINDOF_DOZER ) && !obj->isKindOf( KINDOF_HARVESTER ) &&
						 !obj->isKindOf( KINDOF_MONEY_HACKER ) && obj->isAbleToAttack() )
			++army;
	}
	Real allowance = INT_TO_REAL( aiDefenseAllowance( army, now ) );
	if( m_ai->m_persona != AIPERSONA_NORMAL )
		allowance *= m_ai->personaWeights().m_defenses;
	const Int fortress = REAL_TO_INT( allowance * FORTRESS_SHARE ) + FORTRESS_EXTRA;
	if( defences >= fortress )
		return;
	const Bool placed = m_ai->placeDefense( patriot );
	DEBUG_LOG(("AI FORTRESS frame %d player %d %s '%s', %d of %d standing, army %d, bank %d\n", now, player->getPlayerIndex(),
		placed ? "puts up" : "has no room for", patriot->getName().str(), defences, fortress, army, (Int)player->getMoney()->countMoney()));
}

Real AIIdentityUSA::wavePower( Real shared ) const
{
	return hasMove( MOVE_BIG_WAVES ) ? shared * BIG_WAVE_POWER : shared;
}

UnsignedInt AIIdentityUSA::waveMaxHoldFrames( UnsignedInt shared ) const
{
	return hasMove( MOVE_BIG_WAVES ) ? (UnsignedInt)REAL_TO_INT( INT_TO_REAL( shared ) * BIG_WAVE_HOLD ) : shared;
}

/** The shared aim scores buildings and, by the rung's threat weight, armies; Hard puts none on armies, so the
	* Particle Cannon went to the dearest corner of his base.  This one looks only at his army: the known ground
	* units that can fight, each a candidate center, worth the price of every one of them within the beam's reach
	* (half again near our own base), less twice the price of ours there.  Under ARMY_AIM_MIN_WORTH or
	* ARMY_AIM_MIN_UNITS it leaves the shot to the shared aim. */
static const Real ARMY_AIM_RADIUS = 100.0f;
static const Real ARMY_AIM_MIN_WORTH = 2500.0f;
static const Int ARMY_AIM_MIN_UNITS = 3;
static const Real ARMY_AIM_HOME_BONUS = 1.5f;

Bool AIIdentityUSA::aimAtArmy( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	Player *enemy = ThePlayerList->getNthPlayer( enemyIndex );
	if( enemy == NULL )
		return FALSE;
	Player *player = m_ai->m_player;
	const Int me = player->getPlayerIndex();
	const Real reachSqr = sqr( max( radius, ARMY_AIM_RADIUS ) );

	std::vector<Coord3D> his;
	std::vector<Real> hisCost;
	std::vector<Object *> objs;
	enemy->iterateObjects( collectAll, &objs );
	for( size_t o = 0; o < objs.size(); ++o )
	{
		const Object *obj = objs[ o ];
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || !obj->isKindOf( KINDOF_CAN_ATTACK ) ||
				obj->isSignificantlyAboveTerrain() || !knownTo( obj, me ) )
			continue;
		his.push_back( *obj->getPosition() );
		hisCost.push_back( INT_TO_REAL( obj->getTemplate()->calcCostToBuild( enemy ) ) );
	}
	if( (Int)his.size() < ARMY_AIM_MIN_UNITS )
		return FALSE;
	std::vector<Coord3D> ours;
	std::vector<Real> ourCost;
	objs.clear();
	player->iterateObjects( collectAll, &objs );
	for( size_t o = 0; o < objs.size(); ++o )
	{
		const Object *obj = objs[ o ];
		if( obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isSignificantlyAboveTerrain() )
			continue;
		ours.push_back( *obj->getPosition() );
		ourCost.push_back( INT_TO_REAL( obj->getTemplate()->calcCostToBuild( player ) ) );
	}

	// ponytail: every known unit against every other, paid only on the frame a strike is ready
	Int best = -1;
	Real bestWorth = 0.0f;
	Int bestCount = 0;
	for( size_t c = 0; c < his.size(); ++c )
	{
		Real worth = 0.0f;
		Int count = 0;
		for( size_t u = 0; u < his.size(); ++u )
			if( sqr( his[ u ].x - his[ c ].x ) + sqr( his[ u ].y - his[ c ].y ) <= reachSqr )
			{
				worth += hisCost[ u ];
				++count;
			}
		for( size_t u = 0; u < ours.size(); ++u )
			if( sqr( ours[ u ].x - his[ c ].x ) + sqr( ours[ u ].y - his[ c ].y ) <= reachSqr )
				worth -= 2.0f * ourCost[ u ];
		if( m_ai->isAtHome( &his[ c ] ) )
			worth *= ARMY_AIM_HOME_BONUS;
		if( count >= ARMY_AIM_MIN_UNITS && worth > bestWorth )
		{
			best = (Int)c;
			bestWorth = worth;
			bestCount = count;
		}
	}
	if( best < 0 || bestWorth < ARMY_AIM_MIN_WORTH )
		return FALSE;
	// the middle of the knot rather than the unit it was counted from
	Coord3D aim;
	aim.zero();
	for( size_t u = 0; u < his.size(); ++u )
		if( sqr( his[ u ].x - his[ best ].x ) + sqr( his[ u ].y - his[ best ].y ) <= reachSqr )
		{
			aim.x += his[ u ].x;
			aim.y += his[ u ].y;
		}
	aim.x /= INT_TO_REAL( bestCount );
	aim.y /= INT_TO_REAL( bestCount );
	aim.z = TheTerrainLogic->getGroundHeight( aim.x, aim.y );
	*target = aim;
	m_ai->noteSuperweaponAim( target );
	DEBUG_LOG(("AI SUPERWEAPON frame %d player %d aims '%s' at an army of %d at (%.0f,%.0f), worth %.0f%s\n", TheGameLogic->getFrame(),
		me, power->getName().str(), bestCount, aim.x, aim.y, bestWorth, m_ai->isAtHome( &aim ) ? ", at home" : ""));
	return TRUE;
}

Bool AIIdentityUSA::powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	const SpecialPowerType kind = power->getSpecialPowerType();
	if( hasMove( MOVE_ARMY_CANNON ) && ( kind == SPECIAL_PARTICLE_UPLINK_CANNON || kind == SUPW_SPECIAL_PARTICLE_UPLINK_CANNON ||
			kind == LAZR_SPECIAL_PARTICLE_UPLINK_CANNON ) )
		return aimAtArmy( power, enemyIndex, radius, target );
	if( hasMove( MOVE_CARPET_ARMY ) && ( kind == SPECIAL_CARPET_BOMB || kind == AIRF_SPECIAL_CARPET_BOMB ) )
		return aimAtArmy( power, enemyIndex, radius, target );
	return FALSE;
}

//----------------------------------------------------------------------------------------------------------
// Air Force General: more aircraft, and the jets go first
//----------------------------------------------------------------------------------------------------------

/** An all-aircraft team's lift in production priority, the Air personality's 25 at four fifths, cut by the share
	* of his known guns that shoot up as the Air personality's is (aiPersonaAirBias), never under half. */
static const Real AIR_TEAM_TASTE = 20.0f;
static const Real AIR_TEAM_TASTE_FLOOR = 0.5f;
/** A hurt helicopter is pulled to be mended under this share of its health, where the rung's own is lower. */
static const Real HELI_MEND_HEALTH = 0.5f;

Real AIIdentityUSA::teamScore( const TeamPrototype *team, Real shared ) const
{
	if( !hasMove( MOVE_AIR_TASTE ) )
		return shared;
	const TeamTemplateInfo *info = team->getTemplateInfo();
	if( info->m_isBaseDefense || info->m_isPerimeterDefense )
		return shared;
	Real air = 0.0f;
	Real total = 0.0f;
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
	{
		const ThingTemplate *tmpl = TheThingFactory->findTemplate( info->m_unitsInfo[ i ].unitThingName, FALSE );
		if( tmpl == NULL )
			continue;
		const Int minUnits = info->m_unitsInfo[ i ].minUnits;
		const Real worth = INT_TO_REAL( minUnits > 1 ? minUnits : 1 ) * INT_TO_REAL( tmpl->calcCostToBuild( m_ai->m_player ) );
		total += worth;
		if( tmpl->isKindOf( KINDOF_AIRCRAFT ) )
			air += worth;
	}
	if( air <= 0.0f || total <= 0.0f )
		return shared;
	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	Int antiAir = 0;
	for( size_t g = 0; g < guns.size(); ++g )
		if( guns[ g ].m_airReach > 0.0f )
			++antiAir;
	const Real taste = max( aiPersonaAirBias( AIR_TEAM_TASTE, guns.empty() ? 0.0f : INT_TO_REAL( antiAir ) / INT_TO_REAL( (Int)guns.size() ) ),
													AIR_TEAM_TASTE_FLOOR * AIR_TEAM_TASTE );
	return shared + taste * air / total;
}

Real AIIdentityUSA::pullHealth( const Object *unit, Real shared ) const
{
	if( !hasMove( MOVE_HELI_MEND ) || shared <= 0.0f || !unit->isKindOf( KINDOF_AIRCRAFT ) )
		return shared;
	const AIUpdateInterface *ai = unit->getAI();
	const JetAIUpdate *jet = ai ? ai->getJetAIUpdate() : NULL;
	if( jet && jet->friend_needsRunway() )
		return shared;		// a jet lands and is mended on its own
	return max( shared, HELI_MEND_HEALTH );
}

#define CLEAR_AA_CHECK_FRAMES ( (UnsignedInt)( 2 * LOGICFRAMES_PER_GAME_SECOND ) )
#define CLEAR_AA_FRESH_FRAMES ( (UnsignedInt)( 15 * LOGICFRAMES_PER_GAME_SECOND ) )
/** The known anti-air gun nearest the front, when one stands within this of it, and the jets go only in a pair or more. */
static const Real CLEAR_AA_RANGE = 600.0f;
static const Int CLEAR_AA_MIN_JETS = 2;

/** Comanches go in with the wave and die to the anti-air it walks into.  When a wave sets out, the jets standing
	* idle on the airfield and the ones the wave took strike the known anti-air building nearest its front first.
	* It fired no strike in 40 Air Force matches (2026-11-27): a jet had to be able to shoot the gun from where it
	* stood (ATTACKRESULT_POSSIBLE), which a jet on its airfield never is, and the wave and the spare-guard sweep take
	* every jet idle at home, so by the next check none was left.  The shared air guard (steerAircraft) still calls
	* a strike off whose flight crosses other known guns. */
void AIIdentityUSA::doClearAntiAir( void )
{
	const UnsignedInt front = m_ai->m_shuttleFrontFrame;
	if( !hasMove( MOVE_CLEAR_AA ) )
		return;
	if( front == 0 || front == m_airFrontFrame )
	{
		m_waveJets.clear();		// a wave sent with no road sets no front: its jets are not this move's
		return;
	}
	Player *player = m_ai->m_player;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( ( now + player->getPlayerIndex() ) % CLEAR_AA_CHECK_FRAMES != 0 )
		return;
	m_airFrontFrame = front;
	std::vector<ObjectID> waveJets;
	waveJets.swap( m_waveJets );
	if( now - front > CLEAR_AA_FRESH_FRAMES )
		return;
	const Coord3D *at = &m_ai->m_shuttleFront;
	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	Object *gun = NULL;
	Real gunSqr = sqr( CLEAR_AA_RANGE );
	for( size_t g = 0; g < guns.size(); ++g )
	{
		if( guns[ g ].m_airReach <= 0.0f )
			continue;
		Object *obj = TheGameLogic->findObjectByID( guns[ g ].m_id );
		if( obj == NULL || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) ||
				player->getRelationship( obj->getTeam() ) != ENEMIES )
			continue;
		const Real d = sqr( guns[ g ].m_x - at->x ) + sqr( guns[ g ].m_y - at->y );
		if( d < gunSqr )
		{
			gun = obj;
			gunSqr = d;
		}
	}
	if( gun == NULL )
		return;
	std::vector<Object *> jets;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		AIUpdateInterface *ai = obj->getAI();
		const JetAIUpdate *jet = ai ? ai->getJetAIUpdate() : NULL;
		if( jet == NULL || !jet->friend_needsRunway() || obj->isEffectivelyDead() || jet->isOutOfSpecialReloadAmmo() ||
				!obj->isAbleToAttack() )
			continue;
		// parked and idle, or one of the wave's on its way out; an order given in the takeoff waits for the wheels
		const Bool parked = ai->isIdle() && !obj->isAirborneTarget() && !jet->friend_isTakeoffOrLandingInProgress();
		const Bool sent = std::find( waveJets.begin(), waveJets.end(), obj->getID() ) != waveJets.end() && !jet->isReturningForLanding();
		if( ( !parked && !sent ) ||
				obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, gun, CMD_FROM_AI ) < ATTACKRESULT_POSSIBLE_AFTER_MOVING )
			continue;
		jets.push_back( obj );
	}
	if( (Int)jets.size() < CLEAR_AA_MIN_JETS )
		return;
	for( size_t j = 0; j < jets.size(); ++j )
		jets[ j ]->getAI()->aiAttackObject( gun, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
	DEBUG_LOG(("AI AIRFORCE frame %d player %d sends %d jets at '%s' %d (%.0f,%.0f), %.0f from the wave's front\n", now,
		player->getPlayerIndex(), (Int)jets.size(), gun->getTemplate()->getName().str(), gun->getID(), gun->getPosition()->x,
		gun->getPosition()->y, (Real)sqrt( gunSqr )));
}

/** The scripts set Hold the Line once and leave it.  A plan change stuns every unit it covers for five seconds
	* (BattlePlanChangeParalyzeTime), so the plan moves only on the turns that are quiet anyway: when a wave sets out
	* (Bombardment's 20% damage when it walks into known guns, Search and Destroy's 20% range and sight otherwise)
	* and when the pressure turns to defence (Hold the Line's tenth off the damage taken).  Under attack it changes
	* nothing, and it waits BATTLE_PLAN_SWITCH_FRAMES between two changes. */
#define BATTLE_PLAN_CHECK_FRAMES ( (UnsignedInt)( 2 * LOGICFRAMES_PER_GAME_SECOND ) )
#define BATTLE_PLAN_SWITCH_FRAMES ( (UnsignedInt)( 45 * LOGICFRAMES_PER_GAME_SECOND ) )
#define BATTLE_PLAN_WAVE_FRAMES ( (UnsignedInt)( 20 * LOGICFRAMES_PER_GAME_SECOND ) )	///< how long after a send its plan may still be put up
static const Real BATTLE_PLAN_SIEGE_SHARE = 0.25f;	///< known guns on its road worth this share of the wave make it a siege

void AIIdentityUSA::onWaveSent( AIGroup *wave, Real power )
{
	// the jets this wave takes off the airfield, for doClearAntiAir to send ahead of it
	if( hasMove( MOVE_CLEAR_AA ) )
	{
		const VecObjectID &ids = wave->getAllIDs();
		for( VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it )
		{
			const Object *obj = TheGameLogic->findObjectByID( *it );
			const AIUpdateInterface *ai = obj ? obj->getAI() : NULL;
			const JetAIUpdate *jet = ai ? ai->getJetAIUpdate() : NULL;
			if( jet && jet->friend_needsRunway() && std::find( m_waveJets.begin(), m_waveJets.end(), *it ) == m_waveJets.end() )
				m_waveJets.push_back( *it );
		}
	}
	if( !hasMove( MOVE_BATTLE_PLANS ) )
		return;
	const Bool siege = m_ai->m_gunHoldSince != 0 || m_ai->guardedRoad() >= BATTLE_PLAN_SIEGE_SHARE * power;
	m_planWaveFrame = TheGameLogic->getFrame();
	m_planWanted = siege ? PLANSTATUS_BOMBARDMENT : PLANSTATUS_SEARCHANDDESTROY;
}

static void collectStrategyCenters( Object *obj, void *userData )
{
	if( obj->isKindOf( KINDOF_FS_STRATEGY_CENTER ) )
		((std::vector<Object *> *)userData)->push_back( obj );
}

void AIIdentityUSA::doBattlePlans( void )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if( now % BATTLE_PLAN_CHECK_FRAMES != 0 || !hasMove( MOVE_BATTLE_PLANS ) )
		return;
	if( m_planSwitchFrame != 0 && now - m_planSwitchFrame < BATTLE_PLAN_SWITCH_FRAMES )
		return;
	Int want = PLANSTATUS_NONE;
	if( m_planWaveFrame != 0 && now - m_planWaveFrame < BATTLE_PLAN_WAVE_FRAMES )
		want = m_planWanted;
	else if( m_ai->m_pressure == AIPRESSURE_DEFEND && !m_ai->isBaseUnderAttack() )
		want = PLANSTATUS_HOLDTHELINE;
	if( want == PLANSTATUS_NONE )
		return;
	const UnsignedInt option = want == PLANSTATUS_BOMBARDMENT ? OPTION_ONE : ( want == PLANSTATUS_HOLDTHELINE ? OPTION_TWO : OPTION_THREE );

	Player *player = m_ai->m_player;
	std::vector<Object *> centers;
	player->iterateObjects( collectStrategyCenters, &centers );
	for( size_t o = 0; o < centers.size(); ++o )
	{
		Object *center = centers[ o ];
		if( center->isEffectivelyDead() || center->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || center->testStatus( OBJECT_STATUS_SOLD ) )
			continue;
		// the plan it was last told, whoever told it
		UnsignedInt current = 0;
		for( BehaviorModule **m = center->getBehaviorModules(); *m; ++m )
		{
			SpecialPowerUpdateInterface *sp = (*m)->getSpecialPowerUpdateInterface();
			if( sp && sp->getCommandOption() != 0 )
				current = (UnsignedInt)sp->getCommandOption();
		}
		if( current == option )
			continue;
		const CommandSet *commandSet = TheCommandStore->findCommandSet( center->getCommandSetString() );
		if( commandSet == NULL )
			continue;
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = commandSet->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER || button->getSpecialPowerTemplate() == NULL ||
					button->getSpecialPowerTemplate()->getSpecialPowerType() != SPECIAL_CHANGE_BATTLE_PLANS || ( button->getOptions() & option ) == 0 )
				continue;
			DEBUG_LOG(("AI PLAN frame %d player %d sets plan %d (was option %d)%s\n", now, player->getPlayerIndex(), want, current,
				want == PLANSTATUS_HOLDTHELINE ? ", outmatched" : ( want == PLANSTATUS_BOMBARDMENT ? ", wave into guns" : ", wave out" )));
			center->doCommandButton( button, CMD_FROM_AI );
			m_planSwitchFrame = now;
			break;
		}
	}
}
