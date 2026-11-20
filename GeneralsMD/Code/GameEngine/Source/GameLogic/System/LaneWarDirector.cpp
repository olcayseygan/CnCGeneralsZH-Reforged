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

// FILE: LaneWarDirector.cpp //////////////////////////////////////////////////////////////////////
//
// The Lane War mode.  See LaneWarDirector.h for what it does, and docs/plans/LANE-MODE.md in the
// wrapper for why.  Everything here is logic state: time is the logic frame, a wave is taken from
// the plot in ObjectID order, purchases arrive only through TheCommandList (or the scenario harness,
// which calls the same entry point inside the logic frame), and nothing reads the client.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

static LaneWarDirector theLaneWarDirector;
LaneWarDirector *TheLaneWarDirector = &theLaneWarDirector;

static const char *const TEAM_NAME[ LANEWAR_TEAMS ] = { "A", "B" };

//-------------------------------------------------------------------------------------------------
Int LaneWar_seatForTick( const Bool occupied[ LANEWAR_SEATS ], UnsignedInt tick )
{
	Int count = 0;
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
		if( occupied[ s ] )
			++count;
	if( count == 0 )
		return -1;

	Int turn = (Int)( tick % (UnsignedInt)count );
	for( Int s = 0; s < LANEWAR_SEATS; ++s )
	{
		if( !occupied[ s ] )
			continue;
		if( turn == 0 )
			return s;
		--turn;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
/* The holding rule (the doc's open item): on each spawn tick, count every live wave copy whose
	 position lies past the centre line on the enemy's side of it, and the team with more holds the
	 middle until the next tick.  A tie holds nothing.  No time in place is asked for: the tick is
	 30 wall seconds apart already. */
Int LaneWar_middleHolder( Int pastLineA, Int pastLineB )
{
	if( pastLineA > pastLineB )
		return 0;
	if( pastLineB > pastLineA )
		return 1;
	return -1;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt LaneWar_income( Bool holdsMiddle )
{
	return LaneWar_incomeAt( 0, holdsMiddle );
}

//-------------------------------------------------------------------------------------------------
/// the base pay and the bought levels; the middle's quarter goes on the whole of it
UnsignedInt LaneWar_incomeAt( Int level, Bool holdsMiddle )
{
	const Int pay = LANEWAR_INCOME + LANEWAR_INCOME_TIER_PAYS * level;
	if( !holdsMiddle )
		return (UnsignedInt)pay;
	return (UnsignedInt)( pay + pay * LANEWAR_MIDDLE_BONUS_PERCENT / 100 );
}

//-------------------------------------------------------------------------------------------------
/// income levels 1 to 4 are 1000 each; tech tier 2 is 1500 and 3 is 3000
Int LaneWar_tierPrice( Int track, Int level )
{
	if( track == LANEWAR_TRACK_INCOME )
		return level >= 1 && level <= LANEWAR_INCOME_TIERS ? LANEWAR_INCOME_TIER_PRICE : 0;
	if( track == LANEWAR_TRACK_TECH )
		return level == 2 ? 1500 : level == 3 ? 3000 : 0;
	return 0;
}

//-------------------------------------------------------------------------------------------------
/// the footprint's widest extent, rounded up to whole slots; a Ranger takes one, a Crusader two
Int LaneWar_slotsFor( Real radius )
{
	const Int slots = (Int)ceil( 2.0f * radius / (Real)LANEWAR_SLOT_SIZE );
	return slots < 1 ? 1 : slots;
}

//-------------------------------------------------------------------------------------------------
/* The roster.  A side's units by tech tier: tier 1 is what its barracks and factory make without a
	 tech building, tier 2 what a Strategy Center, Propaganda Center or Palace (or a general's power)
	 opens and the aircraft, tier 3 the side's heaviest.  Units only: no defence, mine or structure.
	 The hero is tier 0, outside the tiers: bought once a match from his own slot, never sold (the
	 owner's call of 2026-11-20).  A name is the base side's; a
	 general fields "<tag>_<name>" where that template exists, the base one otherwise.  `only` names
	 the generals that have it (Base for the plain side), `except` those that do not, as the generals'
	 own factories offer them; an entry whose name is already a general's own template carries the
	 one general it belongs to in `only`.  The jets are in: in Lane War a wave jet never runs dry
	 (LaneWarDirector::refillJets). */
namespace
{
	struct RosterEntry
	{
		const char *name;
		Int tier;
		Bool hero;
		const char *only;
		const char *except;
	};

	const RosterEntry USA_ROSTER[] =
	{
		{ "AmericaInfantryRanger",							1, FALSE, NULL, NULL },
		{ "AmericaInfantryMissileDefender",			1, FALSE, NULL, NULL },
		{ "AmericaInfantryPathfinder",					1, FALSE, NULL, NULL },
		{ "AmericaVehicleHumvee",								1, FALSE, NULL, NULL },
		{ "AmericaTankCrusader",								1, FALSE, NULL, "AirF SupW" },
		{ "AmericaVehicleSentryDrone",					1, FALSE, NULL, NULL },
		{ "AmericaTankMicrowave",								2, FALSE, NULL, NULL },
		{ "AmericaVehicleTomahawk",							2, FALSE, NULL, "Lazr" },
		{ "AmericaTankPaladin",									2, FALSE, NULL, "AirF" },
		{ "AmericaVehicleComanche",							2, FALSE, NULL, NULL },
		{ "AmericaJetRaptor",										2, FALSE, NULL, NULL },
		{ "AmericaJetStealthFighter",						3, FALSE, NULL, NULL },
		{ "AmericaJetAurora",										3, FALSE, NULL, NULL },
		{ "AmericaTankAvenger",									3, FALSE, NULL, NULL },
		{ "AmericaInfantryColonelBurton",				0, TRUE,	NULL, NULL },
	};

	const RosterEntry CHINA_ROSTER[] =
	{
		{ "ChinaInfantryRedguard",							1, FALSE, NULL, "Infa" },
		{ "Infa_ChinaInfantryMiniGunner",				1, FALSE, "Infa", NULL },
		{ "ChinaInfantryTankHunter",						1, FALSE, NULL, NULL },
		{ "ChinaTankBattleMaster",							1, FALSE, NULL, "Infa" },
		{ "ChinaTankGattling",									1, FALSE, NULL, "Infa" },
		{ "ChinaTankDragon",										1, FALSE, NULL, NULL },
		{ "ChinaVehicleTroopCrawler",						1, FALSE, NULL, NULL },
		{ "ChinaTankECM",												2, FALSE, NULL, NULL },
		{ "ChinaVehicleInfernoCannon",					2, FALSE, NULL, "Tank" },
		{ "ChinaVehicleListeningOutpost",				2, FALSE, NULL, NULL },
		{ "ChinaVehicleHelix",									2, FALSE, NULL, NULL },
		{ "ChinaJetMIG",												2, FALSE, NULL, NULL },
		{ "ChinaTankOverlord",									3, FALSE, NULL, "Tank Infa" },
		{ "Tank_ChinaTankEmperor",							3, FALSE, "Tank", NULL },
		{ "ChinaVehicleNukeLauncher",						3, FALSE, NULL, "Tank" },
		{ "ChinaInfantryBlackLotus",						0, TRUE,	NULL, NULL },
	};

	const RosterEntry GLA_ROSTER[] =
	{
		{ "GLAInfantryRebel",										1, FALSE, NULL, NULL },
		{ "GLAInfantryTerrorist",								1, FALSE, NULL, NULL },
		{ "GLAInfantryTunnelDefender",					1, FALSE, NULL, NULL },
		{ "GLAVehicleTechnical",								1, FALSE, NULL, NULL },
		{ "GLAVehicleCombatBike",								1, FALSE, NULL, NULL },
		{ "GLAVehicleCombatBikeTerrorist",			1, FALSE, "Slth", NULL },
		{ "GLATankScorpion",										1, FALSE, NULL, "Slth" },
		{ "GLAVehicleQuadCannon",								1, FALSE, NULL, NULL },
		{ "GLAVehicleToxinTruck",								1, FALSE, NULL, NULL },
		{ "GLAInfantryAngryMobNexus",						2, FALSE, NULL, NULL },
		{ "GLATankMarauder",										2, FALSE, NULL, "Slth" },
		{ "GLAVehicleRocketBuggy",							2, FALSE, NULL, NULL },
		{ "GLAVehicleBombTruck",								2, FALSE, NULL, NULL },
		{ "GLAVehicleBattleBus",								2, FALSE, NULL, NULL },
		{ "GLAVehicleScudLauncher",							3, FALSE, NULL, "Slth" },
		{ "GLAInfantryJarmenKell",							0, TRUE,	NULL, NULL },
	};

	/// the player upgrades a side sells in Lane War, those that change what a wave unit does
	struct UpgradeEntry
	{
		const char *name;
		const char *only;
	};

	const UpgradeEntry USA_UPGRADES[] =
	{
		{ "Upgrade_AmericaRangerFlashBangGrenade", NULL },
		{ "Upgrade_AmericaTOWMissile", NULL },
		{ "Upgrade_AmericaSentryDroneGun", NULL },
		{ "Upgrade_AmericaDroneArmor", NULL },
		{ "Upgrade_AmericaChemicalSuits", NULL },
		{ "Upgrade_AmericaAdvancedTraining", NULL },
		{ "Upgrade_AmericaCompositeArmor", "Base Lazr" },
		{ "Upgrade_ComancheRocketPods", NULL },
		{ "Upgrade_AmericaLaserMissiles", NULL },
		{ "Upgrade_AmericaCountermeasures", NULL },
		{ "AirF_Upgrade_StealthComanche", "AirF" },
	};

	const UpgradeEntry CHINA_UPGRADES[] =
	{
		{ "Upgrade_ChinaChainGuns", NULL },
		{ "Upgrade_ChinaBlackNapalm", NULL },
		{ "Upgrade_ChinaAircraftArmor", NULL },
		{ "Upgrade_ChinaSubliminalMessaging", NULL },
		{ "Upgrade_Nationalism", "Base Tank Nuke" },
		{ "Upgrade_Fanaticism", "Infa" },
		{ "Upgrade_ChinaUraniumShells", "Base Tank" },
		{ "Upgrade_ChinaNuclearTanks", "Base Tank" },
		{ "Upgrade_ChinaNeutronShells", "Base Infa Nuke" },
		{ "Upgrade_ChinaIsotopeStability", "Nuke" },
		{ "Upgrade_ChinaTacticalNukeMig", "Nuke" },
		{ "Tank_Upgrade_ChinaTankAutoLoader", "Tank" },
	};

	const UpgradeEntry GLA_UPGRADES[] =
	{
		{ "Upgrade_GLAAPBullets", NULL },
		{ "Upgrade_GLAAPRockets", NULL },
		{ "Upgrade_GLAJunkRepair", NULL },
		{ "Upgrade_GLABuggyAmmo", NULL },
		{ "Upgrade_GLAScorpionRocket", "Base Chem Demo" },
		{ "Upgrade_GLAArmTheMob", NULL },
		{ "Upgrade_GLAToxinShells", "Base" },
		{ "Upgrade_GLAAnthraxBeta", "Base Slth" },
		{ "Chem_Upgrade_GLAAnthraxGamma", "Chem" },
		{ "Upgrade_GLACamouflage", "Base" },
		{ "Demo_Upgrade_SuicideBomb", "Demo" },
	};

	struct GeneralTag
	{
		const char *side;
		const char *tag;
	};

	const GeneralTag GENERAL_TAGS[] =
	{
		{ "AmericaAirForceGeneral", "AirF" },
		{ "AmericaLaserGeneral", "Lazr" },
		{ "AmericaSuperWeaponGeneral", "SupW" },
		{ "ChinaTankGeneral", "Tank" },
		{ "ChinaInfantryGeneral", "Infa" },
		{ "ChinaNukeGeneral", "Nuke" },
		{ "GLAToxinGeneral", "Chem" },
		{ "GLADemolitionGeneral", "Demo" },
		{ "GLAStealthGeneral", "Slth" },
	};
}

/// 0 USA, 1 China, 2 GLA, -1 a side outside the mode; *tag the general's prefix or "Base"
static Int laneSide( const Player *player, const char **tag )
{
	*tag = "Base";
	if( player == NULL )
		return -1;
	const AsciiString side = player->getSide();
	for( size_t g = 0; g < sizeof( GENERAL_TAGS ) / sizeof( GENERAL_TAGS[ 0 ] ); ++g )
		if( side == GENERAL_TAGS[ g ].side )
			*tag = GENERAL_TAGS[ g ].tag;
	if( side == "America" || side.startsWith( "AmericaAirForce" ) || side.startsWith( "AmericaLaser" ) || side.startsWith( "AmericaSuperWeapon" ) )
		return 0;
	if( side.startsWith( "China" ) )
		return 1;
	if( side.startsWith( "GLA" ) )
		return 2;
	return -1;
}

/// the tag is one of the words of a space separated list
static Bool tagListed( const char *list, const char *tag )
{
	const size_t length = strlen( tag );
	for( const char *at = list; at != NULL && *at; )
	{
		const char *end = strchr( at, ' ' );
		const size_t word = end ? (size_t)( end - at ) : strlen( at );
		if( word == length && strncmp( at, tag, length ) == 0 )
			return TRUE;
		at = end ? end + 1 : NULL;
	}
	return FALSE;
}

static Bool offeredTo( const char *only, const char *except, const char *tag )
{
	if( only != NULL && !tagListed( only, tag ) )
		return FALSE;
	return except == NULL || !tagListed( except, tag );
}

Int LaneWar_roster( const Player *player, LaneWarRosterItem *out, Int maxItems )
{
	const char *tag;
	const Int side = laneSide( player, &tag );
	const RosterEntry *table = side == 0 ? USA_ROSTER : side == 1 ? CHINA_ROSTER : GLA_ROSTER;
	const Int size = side == 0 ? (Int)( sizeof( USA_ROSTER ) / sizeof( USA_ROSTER[ 0 ] ) )
								 : side == 1 ? (Int)( sizeof( CHINA_ROSTER ) / sizeof( CHINA_ROSTER[ 0 ] ) )
								 : (Int)( sizeof( GLA_ROSTER ) / sizeof( GLA_ROSTER[ 0 ] ) );
	if( side < 0 )
		return 0;

	Int count = 0;
	for( Int i = 0; i < size && count < maxItems; ++i )
	{
		const RosterEntry &e = table[ i ];
		if( !offeredTo( e.only, e.except, tag ) )
			continue;
		const ThingTemplate *thing = NULL;
		if( strcmp( tag, "Base" ) != 0 )
		{
			AsciiString name;
			name.format( "%s_%s", tag, e.name );
			thing = TheThingFactory->findTemplate( name, FALSE );
		}
		if( thing == NULL )
			thing = TheThingFactory->findTemplate( e.name, FALSE );
		if( thing == NULL )
			continue;
		out[ count ].thing = thing;
		out[ count ].tier = e.tier;
		out[ count ].hero = e.hero;
		++count;
	}
	return count;
}

Bool LaneWar_isHero( const Player *player, const ThingTemplate *tmpl )
{
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX );
	for( Int i = 0; i < count; ++i )
		if( roster[ i ].thing == tmpl )
			return roster[ i ].hero;
	return FALSE;
}

Int LaneWar_upgrades( const Player *player, const UpgradeTemplate **out, Int maxItems )
{
	const char *tag;
	const Int side = laneSide( player, &tag );
	const UpgradeEntry *table = side == 0 ? USA_UPGRADES : side == 1 ? CHINA_UPGRADES : GLA_UPGRADES;
	const Int size = side == 0 ? (Int)( sizeof( USA_UPGRADES ) / sizeof( USA_UPGRADES[ 0 ] ) )
								 : side == 1 ? (Int)( sizeof( CHINA_UPGRADES ) / sizeof( CHINA_UPGRADES[ 0 ] ) )
								 : (Int)( sizeof( GLA_UPGRADES ) / sizeof( GLA_UPGRADES[ 0 ] ) );
	if( side < 0 )
		return 0;

	Int count = 0;
	for( Int i = 0; i < size && count < maxItems; ++i )
	{
		if( !offeredTo( table[ i ].only, NULL, tag ) )
			continue;
		const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( table[ i ].name );
		if( upgrade != NULL && upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER )
			out[ count++ ] = upgrade;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
LaneWarDirector::LaneWarDirector( void )
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::reset( void )
{
	m_active = FALSE;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			for( Int g = 0; g < LANEWAR_GRIDS; ++g )
			{
				m_gridLo[ t ][ s ][ g ].x = m_gridLo[ t ][ s ][ g ].y = 0.0f;
				m_gridHi[ t ][ s ][ g ] = m_gridLo[ t ][ s ][ g ];
				m_gridCols[ t ][ s ][ g ] = m_gridRows[ t ][ s ][ g ] = 0;
			}
			m_plotAnchor[ t ][ s ].zero();
			m_seatPlayer[ t ][ s ] = -1;
			m_incomeLevel[ t ][ s ] = 0;
			m_techLevel[ t ][ s ] = 1;
			m_heroBought[ t ][ s ] = FALSE;
		}
		m_spawn[ t ].zero();
		m_fortress[ t ] = INVALID_ID;
		m_tower[ t ][ 0 ] = m_tower[ t ][ 1 ] = INVALID_ID;
		m_fortressDown[ t ] = FALSE;
	}
	m_middle.zero();
	m_nextTickFrame = 0;
	m_tickCount = 0;
	m_copies.clear();
	m_plotUnits.clear();
	m_middleHolder = -1;
}

//-------------------------------------------------------------------------------------------------
/** A seat's grid from the map's area of that name, its bounds and whole slots from its front left
	  corner: a plot stands behind its end of the lane, so its edge towards the lane, its front, faces
	  the enemy end, and the front runs across y.  The air rows behind it are the same shape.  FALSE
	  where the map has no such area. */
Bool LaneWarDirector::readGrid( const char *prefix, Int team, Int seat, Int grid )
{
	AsciiString name;
	name.format( "%s%s%d", prefix, TEAM_NAME[ team ], seat + 1 );
	const PolygonTrigger *area = TheTerrainLogic->getTriggerAreaByName( name );
	if( area == NULL || area->getNumPoints() < 3 )
		return FALSE;
	ICoord3D lo = *area->getPoint( 0 ), hi = lo;
	for( Int p = 1; p < area->getNumPoints(); ++p )
	{
		const ICoord3D *pt = area->getPoint( p );
		lo.x = min( lo.x, pt->x );
		lo.y = min( lo.y, pt->y );
		hi.x = max( hi.x, pt->x );
		hi.y = max( hi.y, pt->y );
	}
	m_gridLo[ team ][ seat ][ grid ].x = (Real)lo.x;
	m_gridLo[ team ][ seat ][ grid ].y = (Real)lo.y;
	m_gridHi[ team ][ seat ][ grid ].x = (Real)hi.x;
	m_gridHi[ team ][ seat ][ grid ].y = (Real)hi.y;
	m_gridCols[ team ][ seat ][ grid ] = ( hi.y - lo.y ) / LANEWAR_SLOT_SIZE;
	m_gridRows[ team ][ seat ][ grid ] = ( hi.x - lo.x ) / LANEWAR_SLOT_SIZE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::newMap( Bool loadingSaveGame )
{
	reset();

	// the map's own names; a map without every one of them is not a lane map
	const PolygonTrigger *middle = TheTerrainLogic->getTriggerAreaByName( "LaneWar_MiddleArea" );
	if( middle == NULL )
		return;

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		AsciiString name;
		name.format( "LaneWar_Spawn%s", TEAM_NAME[ t ] );
		const Waypoint *spawn = TheTerrainLogic->getWaypointByName( name );
		if( spawn == NULL )
		{
			DEBUG_LOG(( "LANEWAR: the map has LaneWar_MiddleArea but no %s, so the mode stays off\n", name.str() ));
			return;
		}
		m_spawn[ t ] = *spawn->getLocation();

		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			if( !readGrid( "LaneWar_Build", t, s, LANEWAR_GRID_GROUND ) )
			{
				DEBUG_LOG(( "LANEWAR: the map has LaneWar_MiddleArea but no LaneWar_Build%s%d, so the mode stays off\n", TEAM_NAME[ t ], s + 1 ));
				reset();
				return;
			}
			// the air rows behind the plot; a map from before them has none, and no aircraft can be bought
			readGrid( "LaneWar_Air", t, s, LANEWAR_GRID_AIR );
		}
	}

	// the anchor a wave copy is laid out round is the ground grid's middle; the air rows keep their
	// place behind it in the wave
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			const Coord2D &lo = m_gridLo[ t ][ s ][ LANEWAR_GRID_GROUND ], &hi = m_gridHi[ t ][ s ][ LANEWAR_GRID_GROUND ];
			const Int cols = m_gridCols[ t ][ s ][ LANEWAR_GRID_GROUND ], rows = m_gridRows[ t ][ s ][ LANEWAR_GRID_GROUND ];
			const Real front = towardEnemy( t );
			const Coord2D first = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, front, 0, 0, 1 );
			const Coord2D last = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, front, cols - 1, rows - 1, 1 );
			m_plotAnchor[ t ][ s ].x = 0.5f * ( first.x + last.x );
			m_plotAnchor[ t ][ s ].y = 0.5f * ( first.y + last.y );
			m_plotAnchor[ t ][ s ].z = TheTerrainLogic->getGroundHeight( m_plotAnchor[ t ][ s ].x, m_plotAnchor[ t ][ s ].y );
		}
	middle->getCenterPoint( &m_middle );
	m_active = TRUE;

	// seats by start position: start 1 is team A's seat 1, start 2 team B's seat 1, start 3 A's seat 2
	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		Player *player = ThePlayerList->getNthPlayer( i );
		const Int start = player != NULL && player->isPlayableSide() ? player->getMpStartIndex() : -1;
		if( start >= 0 && start < LANEWAR_TEAMS * LANEWAR_SEATS )
			m_seatPlayer[ LaneWar_teamOfStart( start ) ][ LaneWar_seatOfStart( start ) ] = i;
	}

	m_nextTickFrame = TheGameLogic->getFrame() + LANEWAR_TICK_FRAMES;

	if( !loadingSaveGame )
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			{
				AsciiString name;
				name.format( "LaneWar_Fortress%s", TEAM_NAME[ t ] );
				if( obj->getName() == name )
					m_fortress[ t ] = obj->getID();
				for( Int k = 0; k < 2; ++k )
				{
					name.format( "LaneWar_Tower%s%d", TEAM_NAME[ t ], k + 1 );
					if( obj->getName() == name )
						m_tower[ t ][ k ] = obj->getID();
				}
			}

		// the map, not the lobby, makes the teams: both ends ally inside and fight across
		for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			for( Int s = 0; s < LANEWAR_SEATS; ++s )
			{
				if( m_seatPlayer[ t ][ s ] < 0 )
					continue;
				Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] );
				player->getMoney()->init();
				player->getMoney()->deposit( LANEWAR_START_MONEY, FALSE );

				for( Int t2 = 0; t2 < LANEWAR_TEAMS; ++t2 )
					for( Int s2 = 0; s2 < LANEWAR_SEATS; ++s2 )
					{
						if( m_seatPlayer[ t2 ][ s2 ] < 0 || ( t2 == t && s2 == s ) )
							continue;
						player->setPlayerRelationship( ThePlayerList->getNthPlayer( m_seatPlayer[ t2 ][ s2 ] ),
																					 t2 == t ? ALLIES : ENEMIES );
					}
			}
	}

	const Coord2D &plotLo = m_gridLo[ 0 ][ 0 ][ LANEWAR_GRID_GROUND ], &plotHi = m_gridHi[ 0 ][ 0 ][ LANEWAR_GRID_GROUND ];
	DEBUG_LOG(( "LANEWAR: on; seats A [%d %d %d] B [%d %d %d], fortress A %d B %d, spawn A (%.0f,%.0f) B (%.0f,%.0f), middle x %.0f, plot A1 %d across by %d deep from (%.0f,%.0f) to (%.0f,%.0f), air rows %d by %d, first tick at frame %u\n",
							m_seatPlayer[0][0], m_seatPlayer[0][1], m_seatPlayer[0][2], m_seatPlayer[1][0], m_seatPlayer[1][1], m_seatPlayer[1][2],
							(Int)m_fortress[0], (Int)m_fortress[1], m_spawn[0].x, m_spawn[0].y, m_spawn[1].x, m_spawn[1].y, m_middle.x,
							m_gridCols[0][0][LANEWAR_GRID_GROUND], m_gridRows[0][0][LANEWAR_GRID_GROUND], plotLo.x, plotLo.y, plotHi.x, plotHi.y,
							m_gridCols[0][0][LANEWAR_GRID_AIR], m_gridRows[0][0][LANEWAR_GRID_AIR], m_nextTickFrame ));
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::seatOf( const Player *player, Int *team, Int *seat ) const
{
	if( player == NULL )
		return FALSE;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			if( m_seatPlayer[ t ][ s ] == player->getPlayerIndex() )
			{
				*team = t;
				*seat = s;
				return TRUE;
			}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/// +1 when the enemy end lies towards +x, -1 when it lies towards -x; the lane runs along x
Real LaneWarDirector::towardEnemy( Int team ) const
{
	return m_spawn[ 1 - team ].x >= m_spawn[ team ].x ? 1.0f : -1.0f;
}

//-------------------------------------------------------------------------------------------------
/// the middle of a size by size block of slots whose front left slot is (col, row)
Coord3D LaneWarDirector::slotCentre( Int team, Int seat, Int grid, Int col, Int row, Int size ) const
{
	const Coord2D &lo = m_gridLo[ team ][ seat ][ grid ], &hi = m_gridHi[ team ][ seat ][ grid ];
	const Coord2D at = LaneWar_slotPlace( lo.x, lo.y, hi.x, hi.y, towardEnemy( team ), col, row, size );
	Coord3D pos;
	pos.x = at.x;
	pos.y = at.y;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	return pos;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::getGridBounds( Int team, Int seat, Int grid, Coord2D *lo, Coord2D *hi ) const
{
	if( !m_active || team < 0 || team >= LANEWAR_TEAMS || seat < 0 || seat >= LANEWAR_SEATS
			|| grid < 0 || grid >= LANEWAR_GRIDS || m_gridCols[ team ][ seat ][ grid ] <= 0 )
		return FALSE;
	*lo = m_gridLo[ team ][ seat ][ grid ];
	*hi = m_gridHi[ team ][ seat ][ grid ];
	return TRUE;
}

Bool LaneWarDirector::getPlotBounds( Int team, Int seat, Coord2D *lo, Coord2D *hi ) const
{
	return getGridBounds( team, seat, LANEWAR_GRID_GROUND, lo, hi );
}

Bool LaneWarDirector::getAirBounds( Int team, Int seat, Coord2D *lo, Coord2D *hi ) const
{
	return getGridBounds( team, seat, LANEWAR_GRID_AIR, lo, hi );
}

//-------------------------------------------------------------------------------------------------
Int LaneWarDirector::gridFor( const ThingTemplate *tmpl )
{
	return tmpl != NULL && tmpl->isKindOf( KINDOF_AIRCRAFT ) ? LANEWAR_GRID_AIR : LANEWAR_GRID_GROUND;
}

/// the footprint's widest extent in whole slots
Int LaneWarDirector::sizeFor( const ThingTemplate *tmpl )
{
	const GeometryInfo &geom = tmpl->getTemplateGeometryInfo();
	return LaneWar_slotsFor( max( geom.getMajorRadius(), geom.getMinorRadius() ) );
}

//-------------------------------------------------------------------------------------------------
Int LaneWarDirector::plotUnitAt( const Player *player, Int grid, Int col, Int row ) const
{
	Int team, seat;
	if( !m_active || !seatOf( player, &team, &seat ) )
		return -1;
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		const PlotUnit &u = m_plotUnits[ i ];
		if( u.team == team && u.seat == seat && u.grid == grid && LaneWar_blocksOverlap( col, row, 1, u.col, u.row, u.size )
				&& TheGameLogic->findObjectByID( u.id ) != NULL )
			return (Int)i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::hasLostFortress( const Player *player ) const
{
	Int team, seat;
	if( !m_active || !seatOf( player, &team, &seat ) )
		return FALSE;
	const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ team ] );
	return fortress == NULL || fortress->isEffectivelyDead();
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::onOrder( Player *player, Int kind, const Int *args, Int argCount )
{
	if( !m_active || player == NULL )
		return;

	const UnsignedInt now = TheGameLogic->getFrame();
	Bool done = FALSE;
	switch( kind )
	{
		case LANEWAR_ORDER_PLACE:
			if( argCount >= 3 && args[ 0 ] >= 0 && args[ 0 ] <= 0xFFFF )
				done = placeUnit( player, TheThingFactory->findByTemplateID( (UnsignedShort)args[ 0 ] ), args[ 1 ], args[ 2 ] );
			break;
		case LANEWAR_ORDER_TIER:
			if( argCount >= 2 )
				done = buyTier( player, args[ 0 ], args[ 1 ] );
			break;
		case LANEWAR_ORDER_SELL:
			if( argCount >= 3 )
				done = sellUnit( player, args[ 0 ], args[ 1 ], args[ 2 ] );
			break;
		case LANEWAR_ORDER_MOVE:
			if( argCount >= 5 )
				done = moveUnit( player, args[ 0 ], args[ 1 ], args[ 2 ], args[ 3 ], args[ 4 ] );
			break;
		case LANEWAR_ORDER_UPGRADE:
			if( argCount >= 1 )
				done = buyUpgrade( player, args[ 0 ] );
			break;
		default:
			DEBUG_LOG(( "LANEWAR ORDER: frame %u player %d kind %d with %d arguments is no order\n",
									now, player->getPlayerIndex(), kind, argCount ));
			break;
	}
	(void)done;
}

//-------------------------------------------------------------------------------------------------
const char *LaneWarDirector::whyNotPlace( const Player *player, const ThingTemplate *tmpl, Int col, Int row, ObjectID skip ) const
{
	Int team, seat;
	if( !m_active || !seatOf( player, &team, &seat ) )
		return "not seated in a lane game";
	if( tmpl == NULL || tmpl->isKindOf( KINDOF_STRUCTURE ) || tmpl->isKindOf( KINDOF_PROJECTILE ) )
		return "not a unit";

	// on the side's roster, its tier open, and no second hero
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int listed = LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX );
	const LaneWarRosterItem *item = NULL;
	for( Int i = 0; i < listed && item == NULL; ++i )
		if( roster[ i ].thing == tmpl )
			item = &roster[ i ];
	if( item == NULL )
		return "not on the side's roster";
	if( item->tier > m_techLevel[ team ][ seat ] )
		return "tier locked";
	if( item->hero && skip == INVALID_ID && m_heroBought[ team ][ seat ] )
		return "the hero is bought once";

	const Int grid = gridFor( tmpl );
	const Int size = sizeFor( tmpl );
	if( m_gridCols[ team ][ seat ][ grid ] <= 0 )
		return "no air rows on this map";
	if( !LaneWar_blockFits( col, row, size, m_gridCols[ team ][ seat ][ grid ], m_gridRows[ team ][ seat ][ grid ] ) )
		return "off the plot";
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		const PlotUnit &u = m_plotUnits[ i ];
		if( u.team != team || u.seat != seat || u.id == skip )
			continue;
		const Object *obj = TheGameLogic->findObjectByID( u.id );
		if( obj == NULL )
			continue;
		if( u.grid == grid && LaneWar_blocksOverlap( col, row, size, u.col, u.row, u.size ) )
			return "slot taken";
	}
	if( skip == INVALID_ID && (Int)const_cast<Player *>( player )->getMoney()->countMoney() < tmpl->calcCostToBuild( const_cast<Player *>( player ) ) )
		return "too dear";
	return NULL;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::placeUnit( Player *player, const ThingTemplate *tmpl, Int col, Int row )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const char *refused = whyNotPlace( player, tmpl, col, row );
	if( refused != NULL )
	{
		DEBUG_LOG(( "LANEWAR PLACE: frame %u player %d '%s' at column %d row %d refused: %s\n", now,
								player ? (Int)player->getPlayerIndex() : -1, tmpl ? tmpl->getName().str() : "?", col, row, refused ));
		return FALSE;
	}

	Int team, seat;
	seatOf( player, &team, &seat );
	const Int grid = gridFor( tmpl );
	const Int size = sizeFor( tmpl );
	const Int cost = tmpl->calcCostToBuild( player );
	Team *owner = player->getDefaultTeam();
	Object *obj = TheThingFactory->newObject( tmpl, owner );
	if( obj == NULL )
		return FALSE;
	player->getMoney()->withdraw( (UnsignedInt)cost, FALSE );

	const Coord3D pos = slotCentre( team, seat, grid, col, row, size );
	obj->setOrientation( towardEnemy( team ) > 0.0f ? 0.0f : PI );
	obj->setPosition( &pos );
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if( create )
			create->onBuildComplete();
	}
	owner->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );

	// held where it was put, unable to fire, unhurt and unpicked, whatever the ground round it is
	obj->setDisabled( DISABLED_HELD );
	obj->setStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_NO_ATTACK, OBJECT_STATUS_UNATTACKABLE ) );
	if( obj->getBodyModule() )
		obj->getBodyModule()->setIndestructible( TRUE );

	PlotUnit entry;
	entry.id = obj->getID();
	entry.team = team;
	entry.seat = seat;
	entry.grid = grid;
	entry.col = col;
	entry.row = row;
	entry.size = size;
	entry.cost = cost;
	entry.sent = FALSE;
	m_plotUnits.push_back( entry );
	if( LaneWar_isHero( player, tmpl ) )
		m_heroBought[ team ][ seat ] = TRUE;

	DEBUG_LOG(( "LANEWAR PLACE: frame %u player %d '%s' at column %d row %d size %d on plot %s%d %s at (%.0f,%.0f) for %d, %u left\n", now,
							(Int)player->getPlayerIndex(), tmpl->getName().str(), col, row, size, TEAM_NAME[ team ], seat + 1,
							grid == LANEWAR_GRID_AIR ? "air rows" : "ground", pos.x, pos.y, cost, player->getMoney()->countMoney() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/// the next level of a track, paid in full; the level asked for must be the one after the player's
Bool LaneWarDirector::buyTier( Player *player, Int track, Int level )
{
	Int team, seat;
	const char *refused = NULL;
	Int price = LaneWar_tierPrice( track, level );
	if( !m_active || !seatOf( player, &team, &seat ) )
		refused = "not seated in a lane game";
	else if( price <= 0 )
		refused = "no such tier";
	else if( level != ( track == LANEWAR_TRACK_INCOME ? m_incomeLevel[ team ][ seat ] : m_techLevel[ team ][ seat ] ) + 1 )
		refused = "not the next level";
	else if( (Int)player->getMoney()->countMoney() < price )
		refused = "too dear";

	DEBUG_LOG(( "LANEWAR TIER: frame %u player %d %s level %d for %d %s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, track == LANEWAR_TRACK_INCOME ? "income" : "tech", level, price,
							refused ? "refused: " : "bought", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	player->getMoney()->withdraw( (UnsignedInt)price, FALSE );
	if( track == LANEWAR_TRACK_INCOME )
		m_incomeLevel[ team ][ seat ] = level;
	else
		m_techLevel[ team ][ seat ] = level;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/// the plot unit covering that slot goes, and its price comes back: all of it before its first wave
Bool LaneWarDirector::sellUnit( Player *player, Int grid, Int col, Int row )
{
	const Int index = grid >= 0 && grid < LANEWAR_GRIDS ? plotUnitAt( player, grid, col, row ) : -1;
	if( index < 0 )
	{
		DEBUG_LOG(( "LANEWAR SELL: frame %u player %d column %d row %d refused: none of his units there\n",
								TheGameLogic->getFrame(), player ? (Int)player->getPlayerIndex() : -1, col, row ));
		return FALSE;
	}

	const PlotUnit u = m_plotUnits[ index ];
	Object *obj = TheGameLogic->findObjectByID( u.id );
	if( LaneWar_isHero( player, obj->getTemplate() ) )
	{
		DEBUG_LOG(( "LANEWAR SELL: frame %u player %d column %d row %d refused: the hero is not for sale\n",
								TheGameLogic->getFrame(), (Int)player->getPlayerIndex(), col, row ));
		return FALSE;
	}
	const Int refund = LaneWar_refund( u.cost, u.sent );
	m_plotUnits.erase( m_plotUnits.begin() + index );
	TheGameLogic->destroyObject( obj );
	player->getMoney()->deposit( (UnsignedInt)refund, FALSE );
	DEBUG_LOG(( "LANEWAR SELL: frame %u player %d '%s' at column %d row %d, bought for %d, %s, back %d, %u now\n",
							TheGameLogic->getFrame(), (Int)player->getPlayerIndex(), obj->getTemplate()->getName().str(), u.col, u.row,
							u.cost, u.sent ? "has gone out" : "never out", refund, player->getMoney()->countMoney() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/// the plot unit covering the first slot stands down with its front left slot at the second, same grid
Bool LaneWarDirector::moveUnit( Player *player, Int grid, Int fromCol, Int fromRow, Int toCol, Int toRow )
{
	const Int index = grid >= 0 && grid < LANEWAR_GRIDS ? plotUnitAt( player, grid, fromCol, fromRow ) : -1;
	Object *obj = index >= 0 ? TheGameLogic->findObjectByID( m_plotUnits[ index ].id ) : NULL;
	const char *refused = obj == NULL ? "none of his units there" : whyNotPlace( player, obj->getTemplate(), toCol, toRow, obj->getID() );
	if( refused != NULL )
	{
		DEBUG_LOG(( "LANEWAR MOVE: frame %u player %d from column %d row %d to %d %d refused: %s\n", TheGameLogic->getFrame(),
								player ? (Int)player->getPlayerIndex() : -1, fromCol, fromRow, toCol, toRow, refused ));
		return FALSE;
	}

	PlotUnit &u = m_plotUnits[ index ];
	const Coord3D pos = slotCentre( u.team, u.seat, u.grid, toCol, toRow, u.size );
	TheAI->pathfinder()->removeObjectFromPathfindMap( obj );
	obj->setPosition( &pos );
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	DEBUG_LOG(( "LANEWAR MOVE: frame %u player %d '%s' from column %d row %d to %d %d\n", TheGameLogic->getFrame(),
							(Int)player->getPlayerIndex(), obj->getTemplate()->getName().str(), u.col, u.row, toCol, toRow ));
	u.col = toCol;
	u.row = toRow;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/// an entry of the side's upgrade list, done at once: the player's units, the wave copies among them, take it
Bool LaneWarDirector::buyUpgrade( Player *player, Int index )
{
	Int team, seat;
	const UpgradeTemplate *list[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_upgrades( player, list, LANEWAR_ROSTER_MAX );
	const UpgradeTemplate *upgrade = index >= 0 && index < count ? list[ index ] : NULL;
	const Int price = upgrade ? upgrade->calcCostToBuild( player ) : 0;
	const char *refused = NULL;
	if( !m_active || !seatOf( player, &team, &seat ) )
		refused = "not seated in a lane game";
	else if( upgrade == NULL )
		refused = "not on the side's list";
	else if( player->hasUpgradeComplete( upgrade ) )
		refused = "already done";
	else if( (Int)player->getMoney()->countMoney() < price )
		refused = "too dear";

	DEBUG_LOG(( "LANEWAR UPGRADE: frame %u player %d '%s' for %d %s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, upgrade ? upgrade->getUpgradeName().str() : "?", price,
							refused ? "refused: " : "bought", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	player->getMoney()->withdraw( (UnsignedInt)price, FALSE );
	player->addUpgrade( upgrade, UPGRADE_STATUS_COMPLETE );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::update( void )
{
	if( !m_active )
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	expireCopies( now );
	refillJets();

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ t ] );
		if( !m_fortressDown[ t ] && ( fortress == NULL || fortress->isEffectivelyDead() ) )
		{
			m_fortressDown[ t ] = TRUE;
			DEBUG_LOG(( "HEADLESS LANEWAR: frame %u fortress %s down, team %s loses\n", now, TEAM_NAME[ t ], TEAM_NAME[ t ] ));
		}
	}
	if( m_fortressDown[ 0 ] || m_fortressDown[ 1 ] )
		return;

	if( now >= m_nextTickFrame )
	{
		tick( now );
		m_nextTickFrame += LANEWAR_TICK_FRAMES;
	}
}

//-------------------------------------------------------------------------------------------------
/// a copy dies LANEWAR_LIFESPAN_FRAMES after it spawned; the list drops the dead as it goes
void LaneWarDirector::expireCopies( UnsignedInt now )
{
	Int expired = 0;
	size_t kept = 0;
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		if( now >= m_copies[ i ].dieFrame )
		{
			obj->kill();
			++expired;
			continue;
		}
		m_copies[ kept++ ] = m_copies[ i ];
	}
	m_copies.resize( kept );

	if( expired > 0 )
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u lifespan ended %d copies, %d still out\n", now, expired, (Int)kept ));
}

//-------------------------------------------------------------------------------------------------
/** Lane War has no airfield.  A jet's clip that must go home to reload (Raptor, MiG, Aurora, the
	  Stealth Fighter) is refilled here the frame it runs dry, before the object walk, so its JetAIUpdate
	  never sees it empty and never turns for a runway that is not there: a wave jet strafes down the
	  lane for its whole life.  Copies only; a plot's jets are held and never fire. */
void LaneWarDirector::refillJets( void )
{
	Weapon::StatusPeek peek;	// asking must not move a weapon's fire cycle (Weapon::getStatus)
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj == NULL || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_AIRCRAFT ) )
			continue;
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( weapon != NULL && weapon->getReloadType() == RETURN_TO_BASE_TO_RELOAD && weapon->getStatus() == OUT_OF_AMMO )
			{
				obj->reloadAllAmmo( TRUE );
				break;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
/// the player has a wave copy of a hero alive on the field and still his: no second one goes out
Bool LaneWarDirector::heroCopyAlive( const Player *player ) const
{
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj != NULL && !obj->isEffectivelyDead() && obj->getControllingPlayer() == player
				&& LaneWar_isHero( player, obj->getTemplate() ) )
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::tick( UnsignedInt now )
{
	// the middle first, over the copies already out, then the pay, then the new waves
	Int pastLine[ LANEWAR_TEAMS ] = { 0, 0 };
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		Int team, seat;
		if( obj == NULL || obj->isEffectivelyDead() || !seatOf( obj->getControllingPlayer(), &team, &seat ) )
			continue;
		if( ( obj->getPosition()->x - m_middle.x ) * towardEnemy( team ) > 0.0f )
			++pastLine[ team ];
	}
	const Int holder = LaneWar_middleHolder( pastLine[ 0 ], pastLine[ 1 ] );

	UnsignedInt paid[ LANEWAR_TEAMS ] = { 0, 0 };
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			if( m_seatPlayer[ t ][ s ] < 0 )
				continue;
			const UnsignedInt pay = LaneWar_incomeAt( m_incomeLevel[ t ][ s ], holder == t );
			paid[ t ] = max( paid[ t ], pay );
			ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] )->getMoney()->deposit( pay, FALSE );
		}
	m_middleHolder = holder;

	// a plot's units never leave their slots: the furthest any stands from its own, for the log alone
	Real drift = 0.0f;
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		const PlotUnit &u = m_plotUnits[ i ];
		const Object *obj = TheGameLogic->findObjectByID( u.id );
		if( obj == NULL )
			continue;
		const Coord3D at = slotCentre( u.team, u.seat, u.grid, u.col, u.row, u.size );
		const Real dx = obj->getPosition()->x - at.x, dy = obj->getPosition()->y - at.y;
		drift = max( drift, (Real)sqrt( dx * dx + dy * dy ) );
	}

	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tick %u: past the line A %d B %d, middle %s, paid A %u B %u at most a player, %d plot units, furthest %.1f off its slot\n",
							now, m_tickCount, pastLine[ 0 ], pastLine[ 1 ], holder < 0 ? "nobody" : TEAM_NAME[ holder ], paid[ 0 ], paid[ 1 ],
							(Int)m_plotUnits.size(), drift ));

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		Bool occupied[ LANEWAR_SEATS ];
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			occupied[ s ] = m_seatPlayer[ t ][ s ] >= 0;
		const Int seat = LaneWar_seatForTick( occupied, m_tickCount );
		if( seat >= 0 )
			spawnWave( t, seat, now );
	}
	++m_tickCount;
}

//-------------------------------------------------------------------------------------------------
/** The seat's plot, copied onto the team's pad: each plot unit's template at its slot's place
	  relative to the grid's centre, put down relative to the spawn waypoint.  A plot's front, the edge
	  towards the lane, faces the enemy end as the wave does, so the offset carries over unturned: the
	  front row leads the wave and column 0 is its left flank.  Each copy faces the enemy end.  A copy
	  that lands on ground its locomotor cannot use is moved to the nearest that it can, as a starting
	  unit is. */
void LaneWarDirector::spawnWave( Int team, Int seat, UnsignedInt now )
{
	Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ team ][ seat ] );
	const Real facing = towardEnemy( team ) > 0.0f ? 0.0f : PI;
	Team *owner = player->getDefaultTeam();
	AIGroup *group = TheAI->createGroup();

	// the owner's rule of 2026-11-20: a seat's hero goes out only while no copy of him is alive and the
	// player's on the field, asked once before this wave adds any
	const Bool heroOut = heroCopyAlive( player );
	Bool heroHeld = FALSE;

	// m_plotUnits is in purchase order, which is ObjectID order
	for( size_t i = 0; i < m_plotUnits.size(); ++i )
	{
		PlotUnit &u = m_plotUnits[ i ];
		const Object *original = u.team == team && u.seat == seat ? TheGameLogic->findObjectByID( u.id ) : NULL;
		if( original == NULL || original->getControllingPlayer() != player )
			continue;
		if( heroOut && LaneWar_isHero( player, original->getTemplate() ) )
		{
			heroHeld = TRUE;
			continue;
		}
		u.sent = TRUE;

		const Coord3D at = slotCentre( team, seat, u.grid, u.col, u.row, u.size );
		Coord3D pos = m_spawn[ team ];
		pos.x += at.x - m_plotAnchor[ team ][ seat ].x;
		pos.y += at.y - m_plotAnchor[ team ][ seat ].y;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		Object *copy = TheThingFactory->newObject( original->getTemplate(), owner );
		if( copy == NULL )
			continue;
		copy->setOrientation( facing );
		copy->setPosition( &pos );
		for( BehaviorModule **m = copy->getBehaviorModules(); *m; ++m )
		{
			CreateModuleInterface *create = (*m)->getCreate();
			if( create )
				create->onBuildComplete();
		}
		copy->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
		owner->setActive();
		TheAI->pathfinder()->addObjectToPathfindMap( copy );

		AIUpdateInterface *ai = copy->getAI();
		if( ai && !copy->isKindOf( KINDOF_AIRCRAFT )
				&& TheAI->pathfinder()->adjustDestination( copy, ai->getLocomotorSet(), &pos ) )
		{
			TheAI->pathfinder()->updateGoal( copy, &pos, LAYER_GROUND );
			copy->setPosition( &pos );
		}

		// a jet is born taxiing, waiting for a runway; a wave jet is born flying at its own height
		JetAIUpdate *jet = ai ? ai->getJetAIUpdate() : NULL;
		if( jet != NULL )
		{
			jet->friend_setAllowAirLoco( TRUE );
			ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
			if( ai->getCurLocomotor() != NULL )
				pos.z += ai->getCurLocomotor()->getPreferredHeight();
			copy->setPosition( &pos );
		}

		group->add( copy );
		Copy entry;
		entry.id = copy->getID();
		entry.dieFrame = now + LANEWAR_LIFESPAN_FRAMES;
		m_copies.push_back( entry );
	}

	/* As far as the enemy fortress: the wave's middle walks onto its line, so the rows in front of the
		 fortress stop at it and fight it and the flanks come up level with it.  The pads stand in front of
		 the towers, so the distance between them would stop a wave short of the enemy's tower. */
	Real dx = m_spawn[ 1 - team ].x - m_spawn[ team ].x;
	const Object *enemyFortress = TheGameLogic->findObjectByID( m_fortress[ 1 - team ] );
	if( enemyFortress != NULL )
		dx = enemyFortress->getPosition()->x - m_spawn[ team ].x;
	const VecObjectID &ids = group->getAllIDs();
	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tick %u team %s seat %d (player %d) sends %d%s\n",
							now, m_tickCount, TEAM_NAME[ team ], seat + 1, (Int)player->getPlayerIndex(), (Int)ids.size(),
							heroHeld ? ", the hero held back: his last copy is still out" : "" ));

	if( group->isEmpty() )
	{
		TheAI->destroyGroup( group );
		return;
	}
	group->groupLaneMarch( dx, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );

	AsciiString label;
	label.format( "LaneWar %s%d", TEAM_NAME[ team ], seat + 1 );
	ScenarioDrill_watchLaneMarch( (Int)player->getPlayerIndex(), label, group, dx );
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::crc( Xfer *xfer )
{
	LaneWarDirector::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Version 1: the seats, the fortresses and towers, the tick clock, the copies with their death frames and the
	  plot units with their slots.  The grids, pads and middle come from the map on every load.
	  Version 2: each plot unit's grid, price and whether it has gone out; each seat's income and tech
	  level and whether its hero is bought; the team holding the middle. */
void LaneWarDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			xfer->xferInt( &m_seatPlayer[ t ][ s ] );
		xfer->xferObjectID( &m_fortress[ t ] );
		xfer->xferObjectID( &m_tower[ t ][ 0 ] );
		xfer->xferObjectID( &m_tower[ t ][ 1 ] );
		xfer->xferBool( &m_fortressDown[ t ] );
	}
	xfer->xferUnsignedInt( &m_nextTickFrame );
	xfer->xferUnsignedInt( &m_tickCount );

	UnsignedInt count = (UnsignedInt)m_copies.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_copies.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		xfer->xferObjectID( &m_copies[ i ].id );
		xfer->xferUnsignedInt( &m_copies[ i ].dieFrame );
	}

	count = (UnsignedInt)m_plotUnits.size();
	xfer->xferUnsignedInt( &count );
	if( xfer->getXferMode() == XFER_LOAD )
		m_plotUnits.resize( count );
	for( UnsignedInt i = 0; i < count; ++i )
	{
		PlotUnit &u = m_plotUnits[ i ];
		xfer->xferObjectID( &u.id );
		xfer->xferInt( &u.team );
		xfer->xferInt( &u.seat );
		xfer->xferInt( &u.col );
		xfer->xferInt( &u.row );
		xfer->xferInt( &u.size );
		if( version >= 2 )
		{
			xfer->xferInt( &u.grid );
			xfer->xferInt( &u.cost );
			xfer->xferBool( &u.sent );
		}
		else
		{
			u.grid = LANEWAR_GRID_GROUND;
			u.cost = 0;
			u.sent = TRUE;
		}
	}

	if( version >= 2 )
	{
		for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			for( Int s = 0; s < LANEWAR_SEATS; ++s )
			{
				xfer->xferInt( &m_incomeLevel[ t ][ s ] );
				xfer->xferInt( &m_techLevel[ t ][ s ] );
				xfer->xferBool( &m_heroBought[ t ][ s ] );
			}
		xfer->xferInt( &m_middleHolder );
	}
}
