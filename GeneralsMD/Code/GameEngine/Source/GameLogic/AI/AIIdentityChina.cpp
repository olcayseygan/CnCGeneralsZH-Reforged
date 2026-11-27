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

// AIIdentityChina.cpp
// The China generals' own styles: China, Tank, Infantry and Nuke.  AIIdentity.h has the hooks.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameClient/TerrainVisual.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIIdentity.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ContainModule.h"

class AIIdentityChina : public AIIdentity
{
public:
	enum
	{
		MOVE_HACKER_FARM			= 1 << 0,	///< China: the Internet Center goes up ahead of the rest of the economy
		MOVE_FORWARD_TOWER		= 1 << 1,	///< China: a Propaganda Tower past the front of the base, where the waves form
		MOVE_MEND_TANKS				= 1 << 2,	///< Tank: a dear tank leaves the fight for repair well before it dies
		MOVE_BREACH_DROP			= 1 << 3,	///< Tank: the Tank Drop lands beside its own army inside the enemy base
		MOVE_STEAMROLL				= 1 << 4,	///< Tank: a heavy first wave, then the rest follow it out without waiting
		MOVE_CRAWLER_RAID			= 1 << 5,	///< Infantry: loaded Assault Troop Crawlers raid the enemy's supply early
		MOVE_HORDE						= 1 << 6,	///< Infantry: the waves wait to be a blob, and leave as one
		MOVE_NUKE_PRODUCTION	= 1 << 7,	///< Nuke: the missile goes on the factories and the power plants
		MOVE_NUKE_SIEGE				= 1 << 8,	///< Nuke: Nuke Cannons against a base with guns
		MOVE_CLEAR_FIELDS			= 1 << 9,	///< Nuke: the army never stands about in a radiation field
	};
	enum { RAID_SLOTS = 2 };

	AIIdentityChina( AIPlayer *ai, AIGeneral general ) : AIIdentity( ai, general ), m_towerFrame( 0 ), m_towersAsked( 0 ),
		m_farmUp( FALSE ), m_waveFrame( 0 ), m_dropHeldSince( 0 ), m_raidFrame( 0 )
	{
		for( Int r = 0; r < RAID_SLOTS; ++r )
		{
			m_crawler[ r ] = INVALID_ID;
			m_crawlerSent[ r ] = 0;
			m_crawlerTarget[ r ].zero();
		}
	}

	virtual void onStart( void );
	virtual void onTick( void );
	virtual Int buildingScore( const ThingTemplate *building, Int shared ) const;
	virtual Real teamScore( const TeamPrototype *team, Real shared ) const;
	virtual Real wavePower( Real shared ) const;
	virtual UnsignedInt waveMaxHoldFrames( UnsignedInt shared ) const;
	virtual void onWaveSent( AIGroup *wave, Real power );
	virtual Bool powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );
	virtual Real pullHealth( const Object *unit, Real shared ) const;
	virtual Real rankPullHealth( const Object *unit, Real shared ) const;
	virtual Bool keepsScript( const Team *team ) const;
	virtual void xfer( Xfer *xfer );

private:
	Bool following( void ) const;
	void watchHackerFarm( void );
	void doForwardTower( void );
	void doCrawlerRaids( void );
	void clearFields( void );
	Bool breachDrop( Real radius, Coord3D *target );
	Bool nukeProduction( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target );

	UnsignedInt		m_towerFrame;							///< the last look for a forward tower spot, 0 for none
	Int						m_towersAsked;						///< forward towers asked for this match
	Bool					m_farmUp;									///< the first Internet Center has been reported up
	UnsignedInt		m_waveFrame;							///< the last full wave sent (not a follow-up), 0 for none
	UnsignedInt		m_dropHeldSince;					///< the Tank Drop has waited for a breach since, 0 for not waiting
	UnsignedInt		m_raidFrame;							///< the last crawler sent raiding, 0 for none
	ObjectID			m_crawler[ RAID_SLOTS ];		///< the crawlers out raiding, INVALID_ID for a free slot
	UnsignedInt		m_crawlerSent[ RAID_SLOTS ];
	Coord3D				m_crawlerTarget[ RAID_SLOTS ];
};

AIIdentity *createChinaIdentity( AIPlayer *ai, AIGeneral general )
{
	return new AIIdentityChina( ai, general );
}

// ---- tuning ----

/** An Internet Center's bid over the rest of the build plan: 85 early for any economy building, so it goes up
	* behind a power plant (100) and ahead of the dozer (90) and the factories (70). */
static const Int HACKER_FARM_BID = 12;
/** The forward tower: how far past the base's edge it stands, how many a match, how far apart in time, when it
	* may start, and what it leaves in the bank.  Never under a gun this AI knows of. */
static const Real FORWARD_TOWER_OUT = 150.0f;
static const Int FORWARD_TOWER_MAX = 2;
#define FORWARD_TOWER_EVERY ( (UnsignedInt)( 120 * LOGICFRAMES_PER_GAME_SECOND ) )
#define FORWARD_TOWER_FROM ( (UnsignedInt)( 4 * 60 * LOGICFRAMES_PER_GAME_SECOND ) )
static const Real FORWARD_TOWER_GUN_MARGIN = 60.0f;
static const Int FORWARD_TOWER_BANK = 1000;
/** A tank of the Tank General's that the shared AI pulls out dear goes at this health share, not the rung's, and a
	* veteran one at this share and not the rank rule's 35% (an elite one's is 45% already, a heroic one's 55%). */
static const Real TANK_PULL_HEALTH = 0.45f;
/** The script teams the Infantry General leaves to their scripts, by a part of the team's name: the garrisons, the
	* tech building and derrick captures and the first wave's supply hunters.  Parked for the next wave, the garrison
	* never garrisoned and the captures never captured. */
static const char *const SCRIPT_JOBS[] = { "Garrison", "Capture", "Supply Hunters" };
/** The Tank Drop waits this long for a breach, then the shared aim has it. */
#define DROP_HOLD_FRAMES ( (UnsignedInt)( 120 * LOGICFRAMES_PER_GAME_SECOND ) )
/** A breach: one of ours stands within this of a known enemy building, no known gun reaches it, and its company
	* within BREACH_COMPANY is counted to pick the best foothold. */
static const Real BREACH_REACH = 450.0f;
static const Real BREACH_COMPANY = 300.0f;
/** The steamroller: a full wave is heavier than the shared one, and for this long after it leaves the waves
	* behind it go at half the size and half the wait, so the advance is fed instead of halted.  The window runs
	* from the full wave alone: kept open by each follow-up, 110 of 119 sends in 20 matches went at half. */
#define STEAMROLL_FRAMES ( (UnsignedInt)( 90 * LOGICFRAMES_PER_GAME_SECOND ) )
static const Real STEAMROLL_FIRST = 1.2f;
static const Real STEAMROLL_FOLLOW = 0.5f;
/** The horde: waves wait for more and for longer. */
static const Real HORDE_POWER = 1.25f;
static const Real HORDE_HOLD = 1.2f;
/** The crawler raids: until when, how often a crawler leaves, how long it may drive before it unloads wherever it
	* is, how near its target it unloads, and the bid on the crawler's teams while raids are on. */
#define RAID_UNTIL ( (UnsignedInt)( 10 * 60 * LOGICFRAMES_PER_GAME_SECOND ) )
#define RAID_EVERY ( (UnsignedInt)( 40 * LOGICFRAMES_PER_GAME_SECOND ) )
#define RAID_DRIVE_FRAMES ( (UnsignedInt)( 120 * LOGICFRAMES_PER_GAME_SECOND ) )
static const Real RAID_UNLOAD_REACH = 200.0f;
static const Real RAID_GUN_MARGIN = 50.0f;
static const Real CRAWLER_TEAM_BID = 15.0f;
/** The Nuke Cannon's bid once this AI knows of this many enemy guns. */
static const Int NUKE_SIEGE_GUNS = 4;
static const Real NUKE_SIEGE_BID = 15.0f;
/** How far a step out of a field goes, and how many steps are tried. */
static const Real FIELD_STEP = 120.0f;
static const Int FIELD_STEPS = 5;

/** A look that comes round once in this many frames, staggered by seat. */
static Bool tickDue( const Player *player, UnsignedInt every )
{
	return ( TheGameLogic->getFrame() + 7 * (UnsignedInt)player->getPlayerIndex() ) % every == 0;
}

static void collectAll( Object *obj, void *userData )
{
	((std::vector<Object *> *)userData)->push_back( obj );
}

static Bool templateNamed( const ThingTemplate *tmpl, const char *part )
{
	return tmpl && strstr( tmpl->getName().str(), part ) != NULL;
}

/** Any of the units this team asks for has part in its template name. */
static Bool teamHas( const TeamPrototype *team, const char *part )
{
	const TeamTemplateInfo *info = team->getTemplateInfo();
	for( Int i = 0; i < info->m_numUnitsInfo; ++i )
		if( strstr( info->m_unitsInfo[ i ].unitThingName.str(), part ) != NULL )
			return TRUE;
	return FALSE;
}

/** A building of theirs this player has seen and remembers, or a unit it sees now and that is not hidden from it. */
static Bool knows( const Object *obj, Int me )
{
	if( obj->isUnknownTo( me ) )
		return FALSE;
	return !obj->testStatus( OBJECT_STATUS_STEALTHED ) || obj->testStatus( OBJECT_STATUS_DETECTED );
}

static Bool gunCovers( const std::vector<AIKnownGun> &guns, Real x, Real y, Real margin )
{
	for( size_t g = 0; g < guns.size(); ++g )
		if( sqr( guns[ g ].m_x - x ) + sqr( guns[ g ].m_y - y ) <= sqr( guns[ g ].m_reach + margin ) )
			return TRUE;
	return FALSE;
}

static Bool idleOrGuarding( const Object *obj )
{
	const StateID state = obj->getAI()->getCurrentStateID();
	return state == AI_IDLE || state == AI_GUARD || state == AI_GUARD_RETALIATE;
}

// ---- the hooks ----

void AIIdentityChina::onStart( void )
{
	switch( m_general )
	{
		case AIGENERAL_CHINA:
			drawMove( MOVE_HACKER_FARM, 100 );
			drawMove( MOVE_FORWARD_TOWER, 75 );
			break;
		case AIGENERAL_CHINA_TANK:
			drawMove( MOVE_MEND_TANKS, 100 );
			drawMove( MOVE_BREACH_DROP, 100 );
			drawMove( MOVE_STEAMROLL, 70 );
			break;
		case AIGENERAL_CHINA_INFANTRY:
			drawMove( MOVE_CRAWLER_RAID, 100 );
			drawMove( MOVE_HORDE, 75 );
			break;
		case AIGENERAL_CHINA_NUKE:
			drawMove( MOVE_NUKE_PRODUCTION, 100 );
			drawMove( MOVE_NUKE_SIEGE, 100 );
			drawMove( MOVE_CLEAR_FIELDS, 100 );
			break;
		default:
			break;
	}
	DEBUG_LOG(("AI IDENTITY frame %d player %d '%s' moves 0x%x\n", TheGameLogic->getFrame(), m_ai->m_player->getPlayerIndex(),
		m_ai->m_player->getSide().str(), m_moves));
}

void AIIdentityChina::onTick( void )
{
	if( !m_ai->m_baseCenterSet )
		return;
	if( hasMove( MOVE_HACKER_FARM ) )
		watchHackerFarm();
	if( hasMove( MOVE_FORWARD_TOWER ) )
		doForwardTower();
	if( hasMove( MOVE_CRAWLER_RAID ) )
		doCrawlerRaids();
	if( hasMove( MOVE_CLEAR_FIELDS ) )
		clearFields();
}

Int AIIdentityChina::buildingScore( const ThingTemplate *building, Int shared ) const
{
	if( hasMove( MOVE_HACKER_FARM ) && shared > 0 && building->isKindOf( KINDOF_FS_INTERNET_CENTER ) )
		return shared + HACKER_FARM_BID;
	return shared;
}

Real AIIdentityChina::teamScore( const TeamPrototype *team, Real shared ) const
{
	if( hasMove( MOVE_CRAWLER_RAID ) && TheGameLogic->getFrame() < RAID_UNTIL && teamHas( team, "TroopCrawler" ) )
		return shared + CRAWLER_TEAM_BID;
	if( hasMove( MOVE_NUKE_SIEGE ) && teamHas( team, "NukeLauncher" ) )
	{
		std::vector<AIKnownGun> guns;
		m_ai->collectKnownGuns( &guns );
		if( (Int)guns.size() >= NUKE_SIEGE_GUNS )
			return shared + NUKE_SIEGE_BID;
	}
	return shared;
}

/** A full wave went within STEAMROLL_FRAMES: the next one follows it. */
Bool AIIdentityChina::following( void ) const
{
	return m_waveFrame != 0 && TheGameLogic->getFrame() - m_waveFrame < STEAMROLL_FRAMES;
}

Real AIIdentityChina::wavePower( Real shared ) const
{
	if( hasMove( MOVE_STEAMROLL ) )
		return shared * ( following() ? STEAMROLL_FOLLOW : STEAMROLL_FIRST );
	if( hasMove( MOVE_HORDE ) )
		return shared * HORDE_POWER;
	return shared;
}

UnsignedInt AIIdentityChina::waveMaxHoldFrames( UnsignedInt shared ) const
{
	if( hasMove( MOVE_STEAMROLL ) && following() )
		return shared / 2;
	if( hasMove( MOVE_HORDE ) )
		return (UnsignedInt)( INT_TO_REAL( shared ) * HORDE_HOLD );
	return shared;
}

void AIIdentityChina::onWaveSent( AIGroup *wave, Real power )
{
	if( !hasMove( MOVE_STEAMROLL ) && !hasMove( MOVE_HORDE ) )
		return;
	DEBUG_LOG(("AI IDENTITY frame %d player %d sends a %s wave of power %.0f\n", TheGameLogic->getFrame(),
		m_ai->m_player->getPlayerIndex(), hasMove( MOVE_STEAMROLL ) ? ( following() ? "follow-up" : "first" ) : "horde", power));
	// the window runs from the heavy wave, so the follow-ups do not keep it open and shrink every wave after
	if( !following() )
		m_waveFrame = TheGameLogic->getFrame();
}

Bool AIIdentityChina::powerTarget( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	const SpecialPowerType kind = power->getSpecialPowerType();
	if( hasMove( MOVE_BREACH_DROP ) && kind == SPECIAL_TANK_PARADROP )
		return breachDrop( radius, target );
	if( hasMove( MOVE_NUKE_PRODUCTION ) && ( kind == NUKE_SPECIAL_NEUTRON_MISSILE || kind == SPECIAL_NEUTRON_MISSILE ) )
		return nukeProduction( power, enemyIndex, radius, target );
	return FALSE;
}

Real AIIdentityChina::pullHealth( const Object *unit, Real shared ) const
{
	if( hasMove( MOVE_MEND_TANKS ) && shared > 0.0f && unit->isKindOf( KINDOF_VEHICLE ) && !unit->isKindOf( KINDOF_AIRCRAFT ) )
		return max( shared, TANK_PULL_HEALTH );
	return shared;
}

Real AIIdentityChina::rankPullHealth( const Object *unit, Real shared ) const
{
	if( hasMove( MOVE_MEND_TANKS ) && unit->isKindOf( KINDOF_VEHICLE ) && !unit->isKindOf( KINDOF_AIRCRAFT ) )
		return max( shared, TANK_PULL_HEALTH );
	return shared;
}

Bool AIIdentityChina::keepsScript( const Team *team ) const
{
	if( m_general != AIGENERAL_CHINA_INFANTRY || team == NULL )
		return FALSE;
	const char *name = team->getName().str();
	for( Int j = 0; j < (Int)( sizeof( SCRIPT_JOBS ) / sizeof( SCRIPT_JOBS[ 0 ] ) ); ++j )
		if( strstr( name, SCRIPT_JOBS[ j ] ) != NULL )
			return TRUE;
	return FALSE;
}

void AIIdentityChina::xfer( Xfer *xfer )
{
	AIIdentity::xfer( xfer );
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	xfer->xferUnsignedInt( &m_towerFrame );
	xfer->xferInt( &m_towersAsked );
	xfer->xferBool( &m_farmUp );
	xfer->xferUnsignedInt( &m_waveFrame );
	xfer->xferUnsignedInt( &m_dropHeldSince );
	xfer->xferUnsignedInt( &m_raidFrame );
	for( Int r = 0; r < RAID_SLOTS; ++r )
	{
		xfer->xferObjectID( &m_crawler[ r ] );
		xfer->xferUnsignedInt( &m_crawlerSent[ r ] );
		xfer->xferCoord3D( &m_crawlerTarget[ r ] );
	}
}

// ---- China ----

/** Reports the farm once it stands, for the log's count of how early it went up. */
void AIIdentityChina::watchHackerFarm( void )
{
	Player *player = m_ai->m_player;
	if( m_farmUp || !tickDue( player, (UnsignedInt)( 5 * LOGICFRAMES_PER_GAME_SECOND ) ) )
		return;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	Int hackers = 0;
	Bool up = FALSE;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		if( owned[ o ]->isEffectivelyDead() )
			continue;
		if( owned[ o ]->isKindOf( KINDOF_MONEY_HACKER ) )
			++hackers;
		else if( owned[ o ]->isKindOf( KINDOF_FS_INTERNET_CENTER ) && !owned[ o ]->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
			up = TRUE;
	}
	if( !up )
		return;
	m_farmUp = TRUE;
	DEBUG_LOG(("AI IDENTITY frame %d player %d hacker farm up, %d hackers\n", TheGameLogic->getFrame(), player->getPlayerIndex(), hackers));
}

/** A Propaganda Tower past the edge of the base towards the enemy, where the waves gather and the hurt come
	* back to: it mends them and quickens their fire.  Asked for as a priority build list entry, as the base
	* defences are, so the shared AI's dozers and money decide when it goes up. */
void AIIdentityChina::doForwardTower( void )
{
	Player *player = m_ai->m_player;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !tickDue( player, (UnsignedInt)( 10 * LOGICFRAMES_PER_GAME_SECOND ) ) || now < FORWARD_TOWER_FROM || m_towersAsked >= FORWARD_TOWER_MAX )
		return;
	if( m_towerFrame != 0 && now - m_towerFrame < FORWARD_TOWER_EVERY )
		return;
	if( m_ai->m_pressure == AIPRESSURE_DEFEND || m_ai->isBaseUnderAttack() )
		return;

	// the tower a dozer of ours can put up: the one whose template mends round itself
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	const ThingTemplate *tower = NULL;
	for( size_t o = 0; o < owned.size() && tower == NULL; ++o )
	{
		const Object *dozer = owned[ o ];
		if( dozer->isEffectivelyDead() || !dozer->isKindOf( KINDOF_DOZER ) )
			continue;
		const CommandSet *set = TheCommandStore->findCommandSet( dozer->getCommandSetString() );
		for( Int i = 0; set && i < MAX_COMMANDS_PER_SET && tower == NULL; ++i )
		{
			const CommandButton *button = set->getCommandButton( i );
			if( button == NULL || button->getCommandType() != GUI_COMMAND_DOZER_CONSTRUCT || button->getThingTemplate() == NULL )
				continue;
			const ThingTemplate *tmpl = button->getThingTemplate();
			const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
			for( Int m = 0; m < modules.getCount(); ++m )
				if( modules.getNthName( m ).compare( "PropagandaTowerBehavior" ) == 0 )
					tower = tmpl;
		}
	}
	if( tower == NULL || !player->canBuild( tower ) ||
			(Int)player->getMoney()->countMoney() < tower->calcCostToBuild( player ) + FORWARD_TOWER_BANK )
		return;
	// one asked for and not up yet goes first, the shared AI's own towers by the defences included
	for( BuildListInfo *info = player->getBuildList(); info; info = info->getNext() )
		if( info->getTemplateName() == tower->getName() && info->getObjectID() == INVALID_ID )
			return;

	Coord3D dir;
	if( !m_ai->enemyDirection( &dir ) )
		return;
	m_towerFrame = now;		// found a spot or not, the next look is FORWARD_TOWER_EVERY on
	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	const Real out = m_ai->m_baseRadius + FORWARD_TOWER_OUT;
	// straight at the enemy, then a little to either side
	static const Real SIDE[] = { 0.0f, 0.35f, -0.35f, 0.7f, -0.7f };
	for( Int s = 0; s < (Int)( sizeof( SIDE ) / sizeof( SIDE[ 0 ] ) ); ++s )
	{
		Coord3D pos = m_ai->m_baseCenter;
		pos.x += out * ( dir.x - SIDE[ s ] * dir.y );
		pos.y += out * ( dir.y + SIDE[ s ] * dir.x );
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		// and only where the shared build will keep it: on an unsafe spot (isLocationSafe) it moves the entry away
		if( gunCovers( guns, pos.x, pos.y, FORWARD_TOWER_GUN_MARGIN ) || !m_ai->reachableFromBase( &pos ) ||
				!m_ai->isLocationSafe( &pos, tower ) )
			continue;
		const Real angle = tower->getPlacementViewAngle();
		const Bool legal = LBC_OK == TheBuildAssistant->isLocationLegalToBuild( &pos, tower, angle,
			BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP, NULL, player );
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, as in buildAIBaseDefenseStructure
		if( !legal )
			continue;
		player->addToPriorityBuildList( tower->getName(), &pos, angle );
		++m_towersAsked;
		DEBUG_LOG(("AI IDENTITY frame %d player %d asks a forward '%s' at (%.0f,%.0f), %.0f out of the base\n", now,
			player->getPlayerIndex(), tower->getName().str(), pos.x, pos.y, out));
		return;
	}
}

// ---- Tank ----

/** Where one of ours already stands among his buildings out of every gun this AI knows of, the one with the most
	* company: the tanks land in the hole the army made.  With none the drop waits up to DROP_HOLD_FRAMES (TRUE with
	* a zero target holds the power in both callers), then the shared aim has it. */
Bool AIIdentityChina::breachDrop( Real radius, Coord3D *target )
{
	Player *player = m_ai->m_player;
	const Int me = player->getPlayerIndex();
	const UnsignedInt now = TheGameLogic->getFrame();

	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	std::vector<const Object *> army;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		const Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_AIRCRAFT ) ||
				!( obj->isKindOf( KINDOF_VEHICLE ) || obj->isKindOf( KINDOF_INFANTRY ) ) || obj->isKindOf( KINDOF_DOZER ) ||
				obj->isKindOf( KINDOF_HARVESTER ) || m_ai->isAtHome( obj->getPosition() ) )
			continue;
		army.push_back( obj );
	}

	// every enemy's buildings this AI knows of, in player and object order
	std::vector<const Object *> buildings;
	for( Int i = 0; i < ThePlayerList->getPlayerCount() && !army.empty(); ++i )
	{
		Player *them = ThePlayerList->getNthPlayer( i );
		if( them == player || player->getRelationship( them->getDefaultTeam() ) != ENEMIES )
			continue;
		std::vector<Object *> theirs;
		them->iterateObjects( collectAll, &theirs );
		for( size_t o = 0; o < theirs.size(); ++o )
			if( theirs[ o ]->isKindOf( KINDOF_STRUCTURE ) && !theirs[ o ]->isEffectivelyDead() && knows( theirs[ o ], me ) )
				buildings.push_back( theirs[ o ] );
	}

	std::vector<AIKnownGun> guns;
	if( !buildings.empty() )
		m_ai->collectKnownGuns( &guns );
	const Object *best = NULL;
	Int bestCompany = 0;
	for( size_t a = 0; a < army.size() && !buildings.empty(); ++a )
	{
		const Coord3D *at = army[ a ]->getPosition();
		if( gunCovers( guns, at->x, at->y, radius ) )
			continue;
		Bool inside = FALSE;
		for( size_t b = 0; b < buildings.size() && !inside; ++b )
			inside = sqr( buildings[ b ]->getPosition()->x - at->x ) + sqr( buildings[ b ]->getPosition()->y - at->y ) <= sqr( BREACH_REACH );
		if( !inside )
			continue;
		Int company = 0;
		for( size_t c = 0; c < army.size(); ++c )
			if( sqr( army[ c ]->getPosition()->x - at->x ) + sqr( army[ c ]->getPosition()->y - at->y ) <= sqr( BREACH_COMPANY ) )
				++company;
		if( best == NULL || company > bestCompany )
		{
			best = army[ a ];
			bestCompany = company;
		}
	}
	if( best )
	{
		*target = *best->getPosition();
		target->z = TheTerrainLogic->getGroundHeight( target->x, target->y );
		DEBUG_LOG(("AI IDENTITY frame %d player %d drops tanks into the breach at (%.0f,%.0f), %d of ours there, held %d frames\n",
			now, me, target->x, target->y, bestCompany, m_dropHeldSince != 0 ? (Int)( now - m_dropHeldSince ) : 0));
		m_dropHeldSince = 0;
		m_ai->noteSuperweaponAim( target );
		return TRUE;
	}
	if( m_dropHeldSince == 0 )
	{
		m_dropHeldSince = now;
		DEBUG_LOG(("AI IDENTITY frame %d player %d holds the Tank Drop for a breach\n", now, me));
	}
	if( now - m_dropHeldSince < DROP_HOLD_FRAMES )
	{
		target->zero();
		return TRUE;
	}
	DEBUG_LOG(("AI IDENTITY frame %d player %d found no breach in %d frames, the shared aim drops the tanks\n", now, me,
		(Int)( now - m_dropHeldSince )));
	m_dropHeldSince = 0;
	return FALSE;
}

// ---- Infantry ----

/** A loaded crawler idle on our side of the map drives at the enemy's supply that no known gun covers, nearest
	* our base first, and lets its men out there; once out they stand idle away from home and the shared AI sends
	* them hunting under the company rule it gives every hunter.  RAID_SLOTS out at once, one leaving every
	* RAID_EVERY. */
void AIIdentityChina::doCrawlerRaids( void )
{
	Player *player = m_ai->m_player;
	const UnsignedInt now = TheGameLogic->getFrame();
	if( !tickDue( player, (UnsignedInt)( 3 * LOGICFRAMES_PER_GAME_SECOND ) ) )
		return;
	const Int me = player->getPlayerIndex();

	// the ones out: unload at the target, or wherever they are once they stopped or drove too long
	for( Int r = 0; r < RAID_SLOTS; ++r )
	{
		if( m_crawler[ r ] == INVALID_ID )
			continue;
		Object *crawler = TheGameLogic->findObjectByID( m_crawler[ r ] );
		ContainModuleInterface *contain = crawler ? crawler->getContain() : NULL;
		if( crawler == NULL || crawler->isEffectivelyDead() || crawler->getAI() == NULL || crawler->getControllingPlayer() != player ||
				contain == NULL || contain->getContainCount() == 0 )
		{
			m_crawler[ r ] = INVALID_ID;
			continue;
		}
		const Coord3D *at = crawler->getPosition();
		const Bool there = sqr( at->x - m_crawlerTarget[ r ].x ) + sqr( at->y - m_crawlerTarget[ r ].y ) <= sqr( RAID_UNLOAD_REACH );
		const Bool stopped = crawler->getAI()->getCurrentStateID() == AI_IDLE && !m_ai->isAtHome( at );
		if( there || stopped || now - m_crawlerSent[ r ] >= RAID_DRIVE_FRAMES )
		{
			DEBUG_LOG(("AI IDENTITY frame %d player %d crawler %d unloads %d men at (%.0f,%.0f), %s\n", now, me, crawler->getID(),
				(Int)contain->getContainCount(), at->x, at->y, there ? "at its target" : ( stopped ? "stopped short" : "drove too long" )));
			crawler->getAI()->aiEvacuate( FALSE, CMD_FROM_AI );
			m_crawler[ r ] = INVALID_ID;
		}
	}

	if( now >= RAID_UNTIL || ( m_raidFrame != 0 && now - m_raidFrame < RAID_EVERY ) || m_ai->isBaseUnderAttack() ||
			m_ai->m_pressure == AIPRESSURE_DEFEND )
		return;
	Int slot = -1;
	for( Int r = 0; r < RAID_SLOTS && slot < 0; ++r )
		if( m_crawler[ r ] == INVALID_ID )
			slot = r;
	if( slot < 0 )
		return;

	// a loaded crawler standing about on our side of the map.  A parked one too: the shared AI parks every team
	// for the next wave, its scripts' supply hunters included, and with parked ones left out 40 matches had two
	// raids.  The rest of its wave goes without it
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	Object *crawler = NULL;
	Int crawlers = 0;
	Int loaded = 0;
	for( size_t o = 0; o < owned.size() && crawler == NULL; ++o )
	{
		Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->getAI() == NULL || !templateNamed( obj->getTemplate(), "TroopCrawler" ) )
			continue;
		++crawlers;
		if( obj->getContain() == NULL || obj->getContain()->getContainCount() == 0 )
			continue;
		++loaded;
		if( idleOrGuarding( obj ) && m_ai->isOurSideOfMap( obj->getPosition() ) )
			crawler = obj;
	}
	if( crawler == NULL )
	{
		if( crawlers > 0 && tickDue( player, RAID_EVERY ) )
			DEBUG_LOG(("AI IDENTITY frame %d player %d has no crawler to raid with: %d crawlers, %d loaded, none idle on our side\n",
				now, me, crawlers, loaded));
		return;
	}

	// his gatherers and supply centers this AI knows of, out of every known gun's reach, nearest our base first
	std::vector<AIKnownGun> guns;
	m_ai->collectKnownGuns( &guns );
	const Object *target = NULL;
	Real bestSqr = 0.0f;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->isEffectivelyDead() || !( obj->isKindOf( KINDOF_HARVESTER ) || obj->isKindOf( KINDOF_FS_SUPPLY_CENTER ) ) ||
				obj->isKindOf( KINDOF_AIRCRAFT ) || player->getRelationship( obj->getTeam() ) != ENEMIES || !knows( obj, me ) )
			continue;
		const Coord3D *at = obj->getPosition();
		if( gunCovers( guns, at->x, at->y, RAID_GUN_MARGIN ) )
			continue;
		const Real distSqr = sqr( at->x - m_ai->m_baseCenter.x ) + sqr( at->y - m_ai->m_baseCenter.y );
		if( target == NULL || distSqr < bestSqr )
		{
			target = obj;
			bestSqr = distSqr;
		}
	}
	if( target == NULL )
	{
		if( tickDue( player, RAID_EVERY ) )
			DEBUG_LOG(("AI IDENTITY frame %d player %d crawler %d has no raid target: no supply of his known out of his known guns (%d)\n",
				now, me, crawler->getID(), (Int)guns.size()));
		return;
	}
	m_crawler[ slot ] = crawler->getID();
	m_crawlerSent[ slot ] = now;
	m_crawlerTarget[ slot ] = *target->getPosition();
	m_raidFrame = now;
	crawler->getAI()->aiMoveToPosition( &m_crawlerTarget[ slot ], CMD_FROM_AI );
	DEBUG_LOG(("AI IDENTITY frame %d player %d crawler %d raids '%s' at (%.0f,%.0f) with %d men\n", now, me, crawler->getID(),
		target->getTemplate()->getName().str(), m_crawlerTarget[ slot ].x, m_crawlerTarget[ slot ].y, (Int)crawler->getContain()->getContainCount()));
}

// ---- Nuke ----

/** The missile on the spot where the most of his factories and power plants this AI knows of fall inside the
	* blast, by what they cost him: his army stops coming and his powered defences go quiet.  None known, and the
	* shared aim has it. */
Bool AIIdentityChina::nukeProduction( const SpecialPowerTemplate *power, Int enemyIndex, Real radius, Coord3D *target )
{
	Player *player = m_ai->m_player;
	Player *enemy = ThePlayerList->getNthPlayer( enemyIndex );
	if( enemy == NULL )
		return FALSE;
	const Int me = player->getPlayerIndex();
	std::vector<Object *> theirs;
	enemy->iterateObjects( collectAll, &theirs );
	std::vector<const Object *> picks;
	for( size_t o = 0; o < theirs.size(); ++o )
	{
		const Object *obj = theirs[ o ];
		if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) || !knows( obj, me ) )
			continue;
		if( obj->isKindOf( KINDOF_FS_FACTORY ) || obj->isKindOf( KINDOF_FS_WARFACTORY ) || obj->isKindOf( KINDOF_FS_BARRACKS ) ||
				obj->isKindOf( KINDOF_FS_AIRFIELD ) || obj->isKindOf( KINDOF_FS_POWER ) )
			picks.push_back( obj );
	}
	const Object *best = NULL;
	Int bestWorth = 0;
	Int bestCount = 0;
	for( size_t c = 0; c < picks.size(); ++c )
	{
		const Coord3D *at = picks[ c ]->getPosition();
		Int worth = 0;
		Int count = 0;
		for( size_t p = 0; p < picks.size(); ++p )
			if( sqr( picks[ p ]->getPosition()->x - at->x ) + sqr( picks[ p ]->getPosition()->y - at->y ) <= sqr( radius ) )
			{
				worth += picks[ p ]->getTemplate()->calcCostToBuild( enemy );
				++count;
			}
		if( best == NULL || worth > bestWorth )
		{
			best = picks[ c ];
			bestWorth = worth;
			bestCount = count;
		}
	}
	if( best == NULL )
		return FALSE;
	*target = *best->getPosition();
	target->z = TheTerrainLogic->getGroundHeight( target->x, target->y );
	DEBUG_LOG(("AI IDENTITY frame %d player %d aims '%s' at production and power (%.0f,%.0f): %d buildings worth %d round '%s'\n",
		TheGameLogic->getFrame(), me, power->getName().str(), target->x, target->y, bestCount, bestWorth, best->getTemplate()->getName().str()));
	m_ai->noteSuperweaponAim( target );
	return TRUE;
}

/** One of ours standing still in a field on the map's hazard layer (radiation, poison, fire) walks out of it
	* towards home: the lane choice already routes waves round the fields, and this keeps the army from parking
	* in the ones its own Nuke Cannons and missile leave. */
void AIIdentityChina::clearFields( void )
{
	Player *player = m_ai->m_player;
	if( !tickDue( player, (UnsignedInt)( 2 * LOGICFRAMES_PER_GAME_SECOND ) ) || !m_ai->layersHere() || !m_ai->hasTacticalMap() )
		return;
	const AIInfluenceMap &map = m_ai->m_influence;
	std::vector<Object *> owned;
	player->iterateObjects( collectAll, &owned );
	Int moved = 0;
	for( size_t o = 0; o < owned.size(); ++o )
	{
		Object *obj = owned[ o ];
		if( obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == NULL || obj->isKindOf( KINDOF_STRUCTURE ) ||
				obj->isKindOf( KINDOF_IMMOBILE ) || obj->isKindOf( KINDOF_AIRCRAFT ) || !idleOrGuarding( obj ) )
			continue;
		const Coord3D *at = obj->getPosition();
		if( map.hazardAt( at->x, at->y ) <= 0.0f )
			continue;
		const Real dx = m_ai->m_baseCenter.x - at->x;
		const Real dy = m_ai->m_baseCenter.y - at->y;
		const Real length = (Real)sqrt( sqr( dx ) + sqr( dy ) );
		if( length < 1.0f )
			continue;
		Coord3D to = *at;
		for( Int step = 0; step < FIELD_STEPS && map.hazardAt( to.x, to.y ) > 0.0f; ++step )
		{
			to.x += FIELD_STEP * dx / length;
			to.y += FIELD_STEP * dy / length;
		}
		if( map.hazardAt( to.x, to.y ) > 0.0f )
			continue;
		to.z = TheTerrainLogic->getGroundHeight( to.x, to.y );
		obj->getAI()->aiMoveToPosition( &to, CMD_FROM_AI );
		++moved;
	}
	if( moved > 0 )
		DEBUG_LOG(("AI IDENTITY frame %d player %d walks %d units out of a field\n", TheGameLogic->getFrame(), player->getPlayerIndex(), moved));
}
