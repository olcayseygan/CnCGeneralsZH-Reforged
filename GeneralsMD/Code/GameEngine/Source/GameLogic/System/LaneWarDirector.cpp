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

#include "Common/ActionManager.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarBot.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/LaneWarStats.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/SpecialAbilityUpdate.h"
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
Int LaneWar_veterancyPrice( Int cost, Int level )
{
	const Int percent = level == LEVEL_VETERAN ? LANEWAR_VETERAN_PERCENT : level == LEVEL_ELITE ? LANEWAR_ELITE_PERCENT
										: level == LEVEL_HEROIC ? LANEWAR_HEROIC_PERCENT : 0;
	return cost * percent / 100;
}

//-------------------------------------------------------------------------------------------------
/// the footprint's widest extent, rounded up to whole slots; a Ranger takes one, a Crusader two
Int LaneWar_slotsFor( Real radius )
{
	const Int slots = (Int)ceil( 2.0f * radius / (Real)LANEWAR_SLOT_SIZE );
	return slots < 1 ? 1 : slots;
}

//-------------------------------------------------------------------------------------------------
/* The strike rule.  Both waves march at their slowest member's speed, so the meeting is a straight
	 line problem: the gap closes at the two fronts' speeds added, and the jets need their flight from
	 the park to the meeting point.  They take off when the meeting is no further off than that flight,
	 so they come in as the fronts touch.  Fronts already in contact, or standing still, get the strike
	 at once if they are close enough to be fighting, and none otherwise. */
Bool LaneWar_strikeDue( Real gap, Real mySpeed, Real enemySpeed, Real parkBehind, Real jetSpeed, Real *meetAhead )
{
	mySpeed = max( mySpeed, 0.0f );
	enemySpeed = max( enemySpeed, 0.0f );
	if( gap <= (Real)LANEWAR_STRIKE_CONTACT_GAP )
	{
		*meetAhead = 0.5f * max( gap, 0.0f );
		return TRUE;
	}
	const Real closing = mySpeed + enemySpeed;
	if( closing <= 0.0f || jetSpeed <= 0.0f )
		return FALSE;
	*meetAhead = gap * mySpeed / closing;
	return gap / closing <= ( parkBehind + *meetAhead ) / jetSpeed;
}

//-------------------------------------------------------------------------------------------------
/* The roster.  A side's units by tech tier: tier 1 is what its barracks and factory make without a
	 tech building, tier 2 what a Strategy Center, Propaganda Center or Palace (or a general's power)
	 opens and the aircraft, tier 3 the side's heaviest.  Units only: no defence, mine or structure.
	 The hero is tier 0, outside the tiers: bought once a match from his own slot, never sold (the
	 owner's call of 2026-11-20).  A name is the base side's; a general fields "<tag>_<name>" where
	 that template exists, the base one otherwise (the Slth army's suicide bike is the base GLA's
	 template), and an entry only when its own factories build it (generalFactories), so the general's
	 variant comes with its look and its upgrades and what the general lacks is never offered.  An
	 entry whose name is already a general's own template (the Emperor, the Mini-Gunner) is found
	 under that name and offered to that general alone.  The jets are in: they strike from off the
	 map and reload there (LaneWarDirector::updateJets). */
namespace
{
	struct RosterEntry
	{
		const char *name;
		Int tier;
		Bool hero;
	};

	const RosterEntry USA_ROSTER[] =
	{
		{ "AmericaInfantryRanger",							1, FALSE },
		{ "AmericaInfantryMissileDefender",			1, FALSE },
		{ "AmericaInfantryPathfinder",					1, FALSE },
		{ "AmericaVehicleHumvee",								1, FALSE },
		{ "AmericaTankCrusader",								1, FALSE },	// the Lazr army's is the Laser Tank
		{ "AmericaVehicleSentryDrone",					1, FALSE },
		{ "AmericaTankMicrowave",								2, FALSE },
		{ "AmericaVehicleTomahawk",							2, FALSE },
		{ "AmericaTankPaladin",									2, FALSE },
		{ "AmericaVehicleComanche",							2, FALSE },
		{ "AmericaJetRaptor",										2, FALSE },
		{ "AmericaJetStealthFighter",						3, FALSE },
		{ "AmericaJetAurora",										3, FALSE },
		{ "AmericaTankAvenger",									3, FALSE },
		{ "AmericaInfantryColonelBurton",				0, TRUE },
	};

	const RosterEntry CHINA_ROSTER[] =
	{
		{ "ChinaInfantryRedguard",							1, FALSE },
		{ "Infa_ChinaInfantryMiniGunner",				1, FALSE },
		{ "ChinaInfantryTankHunter",						1, FALSE },
		{ "ChinaTankBattleMaster",							1, FALSE },
		{ "ChinaTankGattling",									1, FALSE },
		{ "ChinaTankDragon",										1, FALSE },
		{ "ChinaVehicleTroopCrawler",						1, FALSE },
		{ "ChinaTankECM",												2, FALSE },
		{ "ChinaVehicleInfernoCannon",					2, FALSE },
		{ "ChinaVehicleListeningOutpost",				2, FALSE },
		{ "ChinaVehicleHelix",									3, FALSE },	// tier 2's dearest, so no tier has one unit (2026-11-23)
		{ "ChinaInfantryHacker",								2, FALSE },
		{ "ChinaJetMIG",												2, FALSE },
		{ "ChinaTankOverlord",									3, FALSE },
		{ "Tank_ChinaTankEmperor",							3, FALSE },
		{ "ChinaVehicleNukeLauncher",						3, FALSE },
		{ "ChinaInfantryBlackLotus",						0, TRUE },
	};

	const RosterEntry GLA_ROSTER[] =
	{
		{ "GLAInfantryRebel",										1, FALSE },
		{ "GLAInfantryTerrorist",								1, FALSE },
		{ "GLAInfantryTunnelDefender",					1, FALSE },
		{ "GLAVehicleTechnical",								1, FALSE },
		{ "GLAVehicleCombatBike",								1, FALSE },
		{ "GLAVehicleCombatBikeTerrorist",			1, FALSE },
		{ "GLATankScorpion",										1, FALSE },
		{ "GLAVehicleQuadCannon",								1, FALSE },
		{ "GLAVehicleToxinTruck",								2, FALSE },	// up from 1, and the two dearest of tier 2 to tier 3
		{ "GLAInfantryAngryMobNexus",						2, FALSE },	// beside the Scud: 8, 5 and 3 (2026-11-23)
		{ "GLAInfantryHijacker",								2, FALSE },
		{ "GLATankMarauder",										2, FALSE },
		{ "GLAVehicleRocketBuggy",							2, FALSE },
		{ "GLAVehicleBombTruck",								3, FALSE },
		{ "GLAVehicleBattleBus",								3, FALSE },
		{ "GLAVehicleScudLauncher",							3, FALSE },
		{ "GLAInfantryJarmenKell",							0, TRUE },
	};

	/// the factories whose build buttons are a side's units, named as the base side's; a general's
	/// own are "<tag>_<name>"
	const char *const FACTORIES[ 3 ][ 3 ] =
	{
		{ "AmericaBarracks", "AmericaWarFactory", "AmericaAirfield" },
		{ "ChinaBarracks", "ChinaWarFactory", "ChinaAirfield" },
		{ "GLABarracks", "GLAArmsDealer", NULL },
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
}

/** 0 USA, 1 China, 2 GLA, -1 a side outside the mode, from the player's PlayerTemplate: its base
	side, and in *tag the general's prefix that names every one of its objects, read off its starting
	building (SupW_AmericaCommandCenter is SupW), or "Base" for the plain side. */
static Int laneSide( const Player *player, AsciiString *tag )
{
	*tag = "Base";
	const PlayerTemplate *army = player != NULL ? player->getPlayerTemplate() : NULL;
	if( army == NULL )
		return -1;
	const AsciiString start = army->getStartingBuilding();
	const char *cut = strchr( start.str(), '_' );
	if( cut != NULL )
		tag->format( "%.*s", (Int)( cut - start.str() ), start.str() );
	const AsciiString base = army->getBaseSide();
	if( base == "USA" )
		return 0;
	if( base == "China" )
		return 1;
	if( base == "GLA" )
		return 2;
	return -1;
}

/// the command sets of the general's own factories, the ones a skirmish base of that army builds
static Int generalFactories( Int side, const AsciiString &tag, const CommandSet **out )
{
	Int count = 0;
	for( Int f = 0; f < 3 && FACTORIES[ side ][ f ] != NULL; ++f )
	{
		AsciiString name( FACTORIES[ side ][ f ] );
		if( tag != "Base" )
			name.format( "%s_%s", tag.str(), FACTORIES[ side ][ f ] );
		const ThingTemplate *factory = TheThingFactory->findTemplate( name, FALSE );
		const CommandSet *set = factory != NULL ? TheCommandStore->findCommandSet( factory->friend_getCommandSetString() ) : NULL;
		if( set != NULL )
			out[ count++ ] = set;
	}
	return count;
}

static Bool factoriesBuild( const CommandSet *const *sets, Int count, const ThingTemplate *thing )
{
	for( Int s = 0; s < count; ++s )
		for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		{
			const CommandButton *button = sets[ s ]->getCommandButton( i );
			if( button != NULL && button->getCommandType() == GUI_COMMAND_UNIT_BUILD && button->getThingTemplate() != NULL
					&& thing->isEquivalentTo( button->getThingTemplate() ) )
				return TRUE;
		}
	return FALSE;
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

Int LaneWar_roster( const Player *player, LaneWarRosterItem *out, Int maxItems )
{
	AsciiString tag;
	const Int side = laneSide( player, &tag );
	const RosterEntry *table = side == 0 ? USA_ROSTER : side == 1 ? CHINA_ROSTER : GLA_ROSTER;
	const Int size = side == 0 ? (Int)( sizeof( USA_ROSTER ) / sizeof( USA_ROSTER[ 0 ] ) )
								 : side == 1 ? (Int)( sizeof( CHINA_ROSTER ) / sizeof( CHINA_ROSTER[ 0 ] ) )
								 : (Int)( sizeof( GLA_ROSTER ) / sizeof( GLA_ROSTER[ 0 ] ) );
	if( side < 0 )
		return 0;
	const CommandSet *factories[ 3 ];
	const Int factoryCount = generalFactories( side, tag, factories );

	Int count = 0;
	for( Int i = 0; i < size && count < maxItems; ++i )
	{
		const RosterEntry &e = table[ i ];
		const ThingTemplate *thing = NULL;
		if( tag != "Base" )
		{
			AsciiString name;
			name.format( "%s_%s", tag.str(), e.name );
			thing = TheThingFactory->findTemplate( name, FALSE );
		}
		if( thing == NULL )
			thing = TheThingFactory->findTemplate( e.name, FALSE );
		if( thing == NULL || !factoriesBuild( factories, factoryCount, thing ) )
			continue;
		out[ count ].thing = thing;
		out[ count ].tier = e.tier;
		out[ count ].hero = e.hero;
		++count;
	}
	return count;
}

static const ThingTemplate *plotTemplateOf( const ThingTemplate *tmpl );

Bool LaneWar_isHero( const Player *player, const ThingTemplate *tmpl )
{
	tmpl = plotTemplateOf( tmpl );	// a wave copy made from the unit's Direct Strike copy is still him
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int count = LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX );
	for( Int i = 0; i < count; ++i )
		if( roster[ i ].thing == tmpl )
			return roster[ i ].hero;
	return FALSE;
}

Int LaneWar_upgrades( const Player *player, const UpgradeTemplate **out, Int maxItems )
{
	AsciiString tag;
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
		if( table[ i ].only != NULL && !tagListed( table[ i ].only, tag.str() ) )
			continue;
		const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( table[ i ].name );
		if( upgrade != NULL && upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER )
			out[ count++ ] = upgrade;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
/// the name is the base side's template or a general's own copy of it ("<tag>_<base>")
static Bool namedAs( const char *name, const char *base )
{
	const size_t length = strlen( name ), baseLength = strlen( base );
	if( length == baseLength )
		return strcmp( name, base ) == 0;
	return length > baseLength && name[ length - baseLength - 1 ] == '_' && strcmp( name + length - baseLength, base ) == 0;
}

namespace
{
	/// LaneWar_damagePercent's table: what the gun does to a unit, a structure and a man, in percent (2026-11-23)
	struct DamageScale
	{
		const char *name;
		Int units, structures, infantry;
	};
	const DamageScale DAMAGE_SCALES[] =
	{
		{ "AmericaVehicleTomahawk",			60, 100, 60 },
		{ "ChinaVehicleNukeLauncher",		60, 100, 60 },
		{ "GLAVehicleScudLauncher",			60, 100, 60 },
		{ "AmericaJetAurora",						100, 75, 100 },
		{ "AmericaInfantryPathfinder",	100, 100, 45 },
	};

	/// the units whose wave copies switch to their WEAPONSET_PLAYER_UPGRADE set, FixesReforged.ini's Lane War part
	const char *const MODE_WEAPON_SETS[] = { "AmericaTankMicrowave" };

	/// a Direct Strike copy of a unit is named "LaneWar_<its name>" (FixesReforged.ini: Black Lotus with a gun)
	const char MODE_COPY_PREFIX[] = "LaneWar_";
}

/// the template a wave copy of a plot unit of this template is made from: its Direct Strike copy where
/// FixesReforged.ini has one, else itself
static const ThingTemplate *waveTemplateOf( const ThingTemplate *tmpl )
{
	AsciiString name;
	name.format( "%s%s", MODE_COPY_PREFIX, tmpl->getName().str() );
	const ThingTemplate *mode = TheThingFactory->findTemplate( name, FALSE );
	return mode != NULL ? mode : tmpl;
}

/// the other way: the plot's template a wave copy's template stands for, so the roster, the hero rule and
/// the players' ability switches know a Direct Strike copy as the unit it copies
static const ThingTemplate *plotTemplateOf( const ThingTemplate *tmpl )
{
	const ThingTemplate *from = tmpl != NULL ? tmpl->friend_getReskinnedFrom() : NULL;
	if( from != NULL && strncmp( tmpl->getName().str(), MODE_COPY_PREFIX, sizeof( MODE_COPY_PREFIX ) - 1 ) == 0 )
		return from;
	return tmpl;
}

Int LaneWar_damagePercent( const char *damager, Bool victimStructure, Bool victimInfantry )
{
	for( Int i = 0; i < (Int)( sizeof( DAMAGE_SCALES ) / sizeof( DAMAGE_SCALES[ 0 ] ) ); ++i )
		if( namedAs( damager, DAMAGE_SCALES[ i ].name ) )
			return victimStructure ? DAMAGE_SCALES[ i ].structures : victimInfantry ? DAMAGE_SCALES[ i ].infantry : DAMAGE_SCALES[ i ].units;
	return 100;
}

Bool LaneWar_hasModeWeaponSet( const char *name )
{
	for( Int i = 0; i < (Int)( sizeof( MODE_WEAPON_SETS ) / sizeof( MODE_WEAPON_SETS[ 0 ] ) ); ++i )
		if( namedAs( name, MODE_WEAPON_SETS[ i ] ) )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWar_abilityDefaultOn( Int commandType, Int powerType )
{
	if( commandType == GUI_COMMAND_HACK_INTERNET || commandType == GUICOMMANDMODE_SABOTAGE_BUILDING
			|| commandType == GUICOMMANDMODE_CONVERT_TO_CARBOMB )
		return FALSE;
	if( commandType == GUI_COMMAND_SPECIAL_POWER )
		return powerType != SPECIAL_INFANTRY_CAPTURE_BUILDING && powerType != SPECIAL_BLACKLOTUS_CAPTURE_BUILDING
				&& powerType != SPECIAL_BLACKLOTUS_STEAL_CASH_HACK;
	return TRUE;
}

static Bool toggleBefore( const LaneWarAbilityToggle &t, Int player, Int templateID, Int slot )
{
	if( t.player != player )
		return t.player < player;
	if( t.templateID != templateID )
		return t.templateID < templateID;
	return t.slot < slot;
}

void LaneWar_setAbilityToggle( std::vector<LaneWarAbilityToggle> &list, Int player, Int templateID, Int slot, Bool on, Bool defaultOn )
{
	size_t at = 0;
	while( at < list.size() && toggleBefore( list[ at ], player, templateID, slot ) )
		++at;
	const Bool found = at < list.size() && list[ at ].player == player && list[ at ].templateID == templateID && list[ at ].slot == slot;
	if( (on != 0) == (defaultOn != 0) )
	{
		if( found )
			list.erase( list.begin() + at );
		return;
	}
	if( found )
	{
		list[ at ].on = on != 0;
		return;
	}
	LaneWarAbilityToggle toggle;
	toggle.player = player;
	toggle.templateID = templateID;
	toggle.slot = slot;
	toggle.on = on != 0;
	list.insert( list.begin() + at, toggle );
}

Bool LaneWar_abilityToggleOn( const std::vector<LaneWarAbilityToggle> &list, Int player, Int templateID, Int slot, Bool defaultOn )
{
	for( size_t i = 0; i < list.size(); ++i )
		if( list[ i ].player == player && list[ i ].templateID == templateID && list[ i ].slot == slot )
			return list[ i ].on;
	return defaultOn;
}

Bool LaneWar_abilityOrderArgsOk( const Int *args, Int argCount )
{
	return argCount >= 3 && args[ 0 ] >= 0 && args[ 0 ] <= 0xFFFF && args[ 1 ] >= 0 && args[ 1 ] < MAX_COMMANDS_PER_SET
			&& ( args[ 2 ] == 0 || args[ 2 ] == 1 );
}

/// a special a wave unit walks up to an enemy to do (LaneWarDirector::walkUp)
static Bool isWalkUpPower( Int power )
{
	return power == SPECIAL_TIMED_CHARGES || power == SPECIAL_REMOTE_CHARGES || power == SPECIAL_TANKHUNTER_TNT_ATTACK
			|| power == SPECIAL_HACKER_DISABLE_BUILDING || power == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING
			|| power == SPECIAL_BLACKLOTUS_STEAL_CASH_HACK || power == SPECIAL_INFANTRY_CAPTURE_BUILDING;
}

static Int specialPowerOf( const CommandButton *button )
{
	const SpecialPowerTemplate *power = button->getCommandType() == GUI_COMMAND_SPECIAL_POWER ? button->getSpecialPowerTemplate() : NULL;
	return power ? (Int)power->getSpecialPowerType() : (Int)SPECIAL_INVALID;
}

/// a walk-up button: a special at an enemy that the unit walks to, or the hijack and sabotage orders
static Bool isWalkUpButton( const CommandButton *button )
{
	const Int type = button->getCommandType();
	if( type == GUICOMMANDMODE_HIJACK_VEHICLE || type == GUICOMMANDMODE_SABOTAGE_BUILDING )
		return TRUE;
	return ( button->getOptions() & NEED_TARGET_ENEMY_OBJECT ) && isWalkUpPower( specialPowerOf( button ) );
}

/** The abilities in one command set: what the AI's rules weigh (AIAbility_buttonsOf) and the walk-ups,
	  read off the INI alone.  No movement order, upgrade or mine clearing, nothing only a script presses,
	  and no special with nothing to aim at in a lane (the booby trap, the disguise, the Helix's bomb). */
static Int abilitiesOfSet( const ThingTemplate *tmpl, const CommandSet *set, LaneWarAbility *out, Int maxItems )
{
	if( tmpl == NULL || set == NULL || tmpl->isKindOf( KINDOF_STRUCTURE ) )
		return 0;
	Int count = 0;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET && count < maxItems; ++i )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || ( button->getOptions() & SCRIPT_ONLY ) )
			continue;
		const Int type = button->getCommandType();
		const Int power = specialPowerOf( button );
		Int kind = -1;
		switch( type )
		{
			case GUI_COMMAND_FIRE_WEAPON:
				if( ( button->getOptions() & USES_MINE_CLEARING_WEAPONSET ) == 0
						&& ( AIAbility_slotIsManual( tmpl, button->getWeaponSlot() ) || AIAbility_slotIsSuicide( tmpl, button->getWeaponSlot() ) ) )
					kind = LANEWAR_ABILITY_WEAPON;
				break;
			case GUI_COMMAND_SPECIAL_POWER:
				if( ( button->getOptions() & NEED_TARGET_ENEMY_OBJECT ) && ( isWalkUpPower( power )
						|| power == SPECIAL_MISSILE_DEFENDER_LASER_GUIDED_MISSILES || power == SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK ) )
					kind = LANEWAR_ABILITY_POWER;
				break;
			case GUI_COMMAND_SWITCH_WEAPON:
				if( button->getWeaponSlot() != PRIMARY_WEAPON )
					kind = LANEWAR_ABILITY_SWITCH;
				break;
			case GUICOMMANDMODE_HIJACK_VEHICLE:
			case GUICOMMANDMODE_SABOTAGE_BUILDING:
				kind = LANEWAR_ABILITY_COMMAND;
				break;
			// left off the list, since nothing in a lane game gives them a use: the car bomb turns one of
			// the player's own wave vehicles into a bomb (the suicide units already blow themselves up on
			// the enemy), and the internet hack makes money the director pays out by its own rule
			case GUICOMMANDMODE_CONVERT_TO_CARBOMB:
			case GUI_COMMAND_HACK_INTERNET:
			default:
				break;
		}
		if( kind < 0 )
			continue;
		out[ count ].button = button;
		out[ count ].slot = i;
		out[ count ].kind = kind;
		out[ count ].defaultOn = LaneWar_abilityDefaultOn( type, power );
		++count;
	}
	return count;
}

Int LaneWar_abilitiesOf( const ThingTemplate *tmpl, LaneWarAbility *out, Int maxItems )
{
	if( tmpl == NULL )
		return 0;
	return abilitiesOfSet( tmpl, TheCommandStore->findCommandSet( tmpl->friend_getCommandSetString() ), out, maxItems );
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
		m_towerPaid[ t ][ 0 ] = m_towerPaid[ t ][ 1 ] = FALSE;
		m_fortressDown[ t ] = FALSE;
	}
	m_middle.zero();
	m_strayWatch = INVALID_ID;
	m_nextTickFrame = 0;
	m_tickCount = 0;
	m_copies.clear();
	m_plotUnits.clear();
	m_middleHolder = -1;
	m_abilityToggles.clear();
	m_jets.clear();
}

//-------------------------------------------------------------------------------------------------
/** A seat's grid from the map's area of that name, its bounds and whole slots from its front left
	  corner: a plot stands behind its end of the lane, so its edge towards the lane, its front, faces
	  the enemy end, and the front runs across y.  FALSE where the map has no such area. */
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
		}
	}

	// the anchor a wave copy is laid out round is the grid's middle
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

	m_nextTickFrame = TheGameLogic->getFrame() + LANEWAR_FIRST_TICK_FRAMES;

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
	DEBUG_LOG(( "LANEWAR: on; seats A [%d %d %d] B [%d %d %d], fortress A %d B %d, spawn A (%.0f,%.0f) B (%.0f,%.0f), middle x %.0f, plot A1 %d across by %d deep from (%.0f,%.0f) to (%.0f,%.0f), first tick at frame %u\n",
							m_seatPlayer[0][0], m_seatPlayer[0][1], m_seatPlayer[0][2], m_seatPlayer[1][0], m_seatPlayer[1][1], m_seatPlayer[1][2],
							(Int)m_fortress[0], (Int)m_fortress[1], m_spawn[0].x, m_spawn[0].y, m_spawn[1].x, m_spawn[1].y, m_middle.x,
							m_gridCols[0][0][LANEWAR_GRID_GROUND], m_gridRows[0][0][LANEWAR_GRID_GROUND], plotLo.x, plotLo.y, plotHi.x, plotHi.y,
							m_nextTickFrame ));

	// what each seat's army can buy, so a log says which general fielded what
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			const Player *player = m_seatPlayer[ t ][ s ] >= 0 ? ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] ) : NULL;
			if( player == NULL )
				continue;
			LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
			const Int count = LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX );
			AsciiString names;
			for( Int i = 0; i < count; ++i )
			{
				names.concat( ' ' );
				names.concat( roster[ i ].thing->getName() );
			}
			DEBUG_LOG(( "LANEWAR: player %d %s roster of %d:%s\n", player->getPlayerIndex(), player->getSide().str(), count, names.str() ));
		}
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

//-------------------------------------------------------------------------------------------------
/// the template's normal locomotors, the first AI module's
static const LocomotorTemplateVector *normalLocomotors( const ThingTemplate *tmpl )
{
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	for( Int m = 0; m < modules.getCount(); ++m )
	{
		const ModuleData *data = modules.getNthData( m );
		if( data != NULL && data->isAiModuleData() )
			return static_cast< const AIUpdateModuleData * >( data )->findLocomotorTemplateVector( LOCOMOTORSET_NORMAL );
	}
	return NULL;
}

Bool LaneWarDirector::isJet( const ThingTemplate *tmpl )
{
	if( tmpl == NULL || !tmpl->isKindOf( KINDOF_AIRCRAFT ) || tmpl->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) )
		return FALSE;
	const LocomotorTemplateVector *set = normalLocomotors( tmpl );
	for( size_t i = 0; set != NULL && i < set->size(); ++i )
		if( ( *set )[ i ]->getAppearance() == LOCO_HOVER )
			return FALSE;		// a helicopter's locomotor (Locomotor::isHelicopter)
	return TRUE;
}

/// a jet of this template's top speed at full health, a frame
static Real jetSpeedOf( const ThingTemplate *tmpl )
{
	Real fastest = 0.0f;
	const LocomotorTemplateVector *set = normalLocomotors( tmpl );
	for( size_t i = 0; set != NULL && i < set->size(); ++i )
		fastest = max( fastest, ( *set )[ i ]->getMaxSpeed() );
	return fastest * tmpl->getSpeedScale();
}

/** The enemy aircraft a jet on its strike goes for first: the nearest one in the air over the map that
	  it can hit, ties to the lower ObjectID.  NULL for a jet with no weapon against air, or nothing up. */
static Object *airTargetFor( const Object *jet, const Region3D &map )
{
	Bool hitsAir = FALSE;
	for( Int slot = 0; slot < WEAPONSLOT_COUNT && !hitsAir; ++slot )
	{
		const Weapon *weapon = jet->getWeaponInWeaponSlot( (WeaponSlotType)slot );
		hitsAir = weapon != NULL && ( weapon->getAntiMask() & WEAPON_ANTI_AIRBORNE_VEHICLE ) != 0;
	}
	if( !hitsAir )
		return NULL;

	Object *best = NULL;
	Real bestDistSq = 0.0f;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( !obj->isKindOf( KINDOF_AIRCRAFT ) || !obj->isAirborneTarget() || obj->isEffectivelyDead()
				|| obj->testStatus( OBJECT_STATUS_UNATTACKABLE ) || !map.isInRegionNoZ( obj->getPosition() )
				|| jet->getRelationship( obj ) != ENEMIES
				|| jet->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, obj, CMD_FROM_SCRIPT ) == ATTACKRESULT_NOT_POSSIBLE )
			continue;
		const Real dx = obj->getPosition()->x - jet->getPosition()->x, dy = obj->getPosition()->y - jet->getPosition()->y;
		const Real distSq = dx * dx + dy * dy;
		if( best == NULL || distSq < bestDistSq || ( distSq == bestDistSq && obj->getID() < best->getID() ) )
		{
			best = obj;
			bestDistSq = distSq;
		}
	}
	return best;
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
	const UnsignedInt moneyBefore = player->getMoney()->countMoney();
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
				done = sellUnit( player, args[ 0 ], args[ 1 ], args[ 2 ], argCount >= 4 ? args[ 3 ] : -1 );
			break;
		case LANEWAR_ORDER_MOVE:
			if( argCount >= 5 )
				done = moveUnit( player, args[ 0 ], args[ 1 ], args[ 2 ], args[ 3 ], args[ 4 ] );
			break;
		case LANEWAR_ORDER_UPGRADE:
			if( argCount >= 1 )
				done = buyUpgrade( player, args[ 0 ] );
			break;
		case LANEWAR_ORDER_ABILITY:
			done = setAbility( player, args, argCount );
			break;
		case LANEWAR_ORDER_VETERANCY:
			done = buyVeterancy( player, args, argCount );
			break;
		case LANEWAR_ORDER_GARRISON:
			done = garrison( player, args, argCount );
			break;
		case LANEWAR_ORDER_UNIT_UPGRADE:
			done = buyUnitUpgrade( player, args, argCount );
			break;
		default:
			DEBUG_LOG(( "LANEWAR ORDER: frame %u player %d kind %d with %d arguments is no order\n",
									now, player->getPlayerIndex(), kind, argCount ));
			break;
	}
	(void)done;
	TheLaneWarStats->onOrder( player, kind, moneyBefore, player->getMoney()->countMoney() );
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

	const Int grid = LANEWAR_GRID_GROUND;
	const Int size = sizeFor( tmpl );
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
/// a unit on a plot, or riding with one: held where it was put, unable to fire, unhurt and unpicked
static void holdOnPlot( Object *obj )
{
	obj->setDisabled( DISABLED_HELD );
	obj->setStatus( MAKE_OBJECT_STATUS_MASK2( OBJECT_STATUS_NO_ATTACK, OBJECT_STATUS_UNATTACKABLE ) );
	if( obj->getBodyModule() )
		obj->getBodyModule()->setIndestructible( TRUE );
}

/** What rides with a unit, in object list order: everything inside it, and inside that (an Overlord's
	  bunker and the men in the bunker), and what it made for itself, its drone. */
static void cargoOf( const Object *host, std::vector<Object *> *out )
{
	out->clear();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj == host )
			continue;
		Bool rides = obj->getProducerID() == host->getID();
		for( const Object *in = obj->getContainedBy(); in != NULL && !rides; in = in->getContainedBy() )
			rides = in == host;
		if( rides )
			out->push_back( obj );
	}
}

/// the object upgrade on that command set slot of the unit's live command set, NULL for any other button
static const UpgradeTemplate *unitUpgradeAt( const Object *obj, Int slot )
{
	const CommandSet *set = obj != NULL && slot >= 0 && slot < MAX_COMMANDS_PER_SET ? TheCommandStore->findCommandSet( obj->getCommandSetString() ) : NULL;
	const CommandButton *button = set != NULL ? set->getCommandButton( slot ) : NULL;
	const UpgradeTemplate *upgrade = button != NULL ? button->getUpgradeTemplate() : NULL;
	if( upgrade == NULL || button->getCommandType() != GUI_COMMAND_OBJECT_UPGRADE || upgrade->getUpgradeType() != UPGRADE_TYPE_OBJECT )
		return NULL;
	return upgrade;
}

/// every part of what was paid for the plot unit, back under the unit's rule
static Int sellValueOf( const LaneWarDirector::PlotUnit &u )
{
	Int value = LaneWar_refund( u.cost, u.sent ) + LaneWar_refund( u.veterancyPaid, u.sent ) + LaneWar_refund( u.upgradesPaid, u.sent );
	for( size_t i = 0; i < u.passengers.size(); ++i )
		value += LaneWar_refund( u.passengers[ i ].cost, u.sent ) + LaneWar_refund( u.passengers[ i ].veterancyPaid, u.sent );
	return value;
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
	const Int grid = LANEWAR_GRID_GROUND;
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
	holdOnPlot( obj );

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
	entry.veterancy = LEVEL_REGULAR;
	entry.veterancyPaid = 0;
	entry.upgradesPaid = 0;
	m_plotUnits.push_back( entry );
	if( LaneWar_isHero( player, tmpl ) )
		m_heroBought[ team ][ seat ] = TRUE;
	TheLaneWarStats->onPlace( player, tmpl, cost );

	DEBUG_LOG(( "LANEWAR PLACE: frame %u player %d '%s' at column %d row %d size %d on plot %s%d%s at (%.0f,%.0f) for %d, %u left\n", now,
							(Int)player->getPlayerIndex(), tmpl->getName().str(), col, row, size, TEAM_NAME[ team ], seat + 1,
							isJet( tmpl ) ? ", a jet" : "", pos.x, pos.y, cost, player->getMoney()->countMoney() ));
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
Bool LaneWarDirector::sellUnit( Player *player, Int grid, Int col, Int row, Int passenger )
{
	const Int index = grid >= 0 && grid < LANEWAR_GRIDS ? plotUnitAt( player, grid, col, row ) : -1;
	const char *refused = index < 0 ? "none of his units there"
											: passenger >= (Int)m_plotUnits[ index ].passengers.size() ? "no such passenger" : NULL;
	if( refused != NULL )
	{
		DEBUG_LOG(( "LANEWAR SELL: frame %u player %d column %d row %d passenger %d refused: %s\n",
								TheGameLogic->getFrame(), player ? (Int)player->getPlayerIndex() : -1, col, row, passenger, refused ));
		return FALSE;
	}

	// one passenger: his price and ranks back under the unit's rule, the unit stays
	if( passenger >= 0 )
	{
		PlotUnit &host = m_plotUnits[ index ];
		const PlotUnit::Passenger p = host.passengers[ passenger ];
		const Int refund = LaneWar_refund( p.cost, host.sent ) + LaneWar_refund( p.veterancyPaid, host.sent );
		host.passengers.erase( host.passengers.begin() + passenger );
		Object *man = TheGameLogic->findObjectByID( p.id );
		if( man != NULL )
		{
			if( man->getContainedBy() != NULL && man->getContainedBy()->getContain() != NULL )
				man->getContainedBy()->getContain()->removeFromContain( man );
			TheGameLogic->destroyObject( man );
		}
		player->getMoney()->deposit( (UnsignedInt)refund, FALSE );
		DEBUG_LOG(( "LANEWAR SELL: frame %u player %d passenger %d '%s' of the unit at column %d row %d, bought for %d and ranked for %d, %s, back %d, %u now\n",
								TheGameLogic->getFrame(), (Int)player->getPlayerIndex(), passenger, p.thing->getName().str(), host.col, host.row,
								p.cost, p.veterancyPaid, host.sent ? "has gone out" : "never out", refund, player->getMoney()->countMoney() ));
		return TRUE;
	}

	const PlotUnit u = m_plotUnits[ index ];
	Object *obj = TheGameLogic->findObjectByID( u.id );
	if( LaneWar_isHero( player, obj->getTemplate() ) )
	{
		DEBUG_LOG(( "LANEWAR SELL: frame %u player %d column %d row %d refused: the hero is not for sale\n",
								TheGameLogic->getFrame(), (Int)player->getPlayerIndex(), col, row ));
		return FALSE;
	}
	// the unit, everything paid on it and everything riding with it
	const Int refund = sellValueOf( u );
	std::vector<Object *> cargo;
	cargoOf( obj, &cargo );
	for( size_t i = 0; i < cargo.size(); ++i )
		TheGameLogic->destroyObject( cargo[ i ] );
	m_plotUnits.erase( m_plotUnits.begin() + index );
	TheGameLogic->destroyObject( obj );
	player->getMoney()->deposit( (UnsignedInt)refund, FALSE );
	DEBUG_LOG(( "LANEWAR SELL: frame %u player %d '%s' at column %d row %d, bought for %d, ranked for %d, upgraded for %d, %d passengers, %d riding along, %s, back %d, %u now\n",
							TheGameLogic->getFrame(), (Int)player->getPlayerIndex(), obj->getTemplate()->getName().str(), u.col, u.row,
							u.cost, u.veterancyPaid, u.upgradesPaid, (Int)u.passengers.size(), (Int)cargo.size(), u.sent ? "has gone out" : "never out",
							refund, player->getMoney()->countMoney() ));
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
	const Coord3D from = *obj->getPosition();
	TheAI->pathfinder()->removeObjectFromPathfindMap( obj );
	obj->setPosition( &pos );
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	// what rides inside goes with it; a held drone beside it is put down as far over
	std::vector<Object *> cargo;
	cargoOf( obj, &cargo );
	for( size_t i = 0; i < cargo.size(); ++i )
	{
		if( cargo[ i ]->getContainedBy() != NULL )
			continue;
		Coord3D at = *cargo[ i ]->getPosition();
		at.x += pos.x - from.x;
		at.y += pos.y - from.y;
		cargo[ i ]->setPosition( &at );
	}
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
/** (grid, column, row[, passenger]): the plot unit covering that slot goes up one rank, Regular to
	  Veteran to Elite to Heroic, for LaneWar_veterancyPrice of what it was bought for.  Only its wave
	  copies wear it; the plot unit itself keeps no rank, so the grid shows none. */
Bool LaneWarDirector::buyVeterancy( Player *player, const Int *args, Int argCount )
{
	const Int grid = argCount >= 3 ? args[ 0 ] : -1;
	const Int index = grid >= 0 && grid < LANEWAR_GRIDS ? plotUnitAt( player, grid, args[ 1 ], args[ 2 ] ) : -1;
	const Int passenger = argCount >= 4 ? args[ 3 ] : -1;
	PlotUnit *host = index >= 0 ? &m_plotUnits[ index ] : NULL;
	PlotUnit::Passenger *man = host != NULL && passenger >= 0 && passenger < (Int)host->passengers.size() ? &host->passengers[ passenger ] : NULL;
	// a passenger's rank goes on his own price, as the unit's on its own
	Int *veterancy = man != NULL ? &man->veterancy : host != NULL ? &host->veterancy : NULL;
	Int *paid = man != NULL ? &man->veterancyPaid : host != NULL ? &host->veterancyPaid : NULL;
	const Int cost = man != NULL ? man->cost : host != NULL ? host->cost : 0;
	const Int price = veterancy != NULL ? LaneWar_veterancyPrice( cost, *veterancy + 1 ) : 0;
	const char *refused = NULL;
	if( host == NULL )
		refused = "none of his units there";
	else if( passenger >= 0 && man == NULL )
		refused = "no such passenger";
	else if( *veterancy >= LEVEL_HEROIC )
		refused = "already Heroic";
	else if( (Int)player->getMoney()->countMoney() < price )
		refused = "too dear";

	if( host == NULL )
	{
		DEBUG_LOG(( "LANEWAR VETERANCY: frame %u player %d at column %d row %d refused: %s\n", TheGameLogic->getFrame(),
								player ? (Int)player->getPlayerIndex() : -1, argCount >= 2 ? args[ 1 ] : -1, argCount >= 3 ? args[ 2 ] : -1, refused ));
		return FALSE;
	}
	const Object *obj = TheGameLogic->findObjectByID( host->id );
	DEBUG_LOG(( "LANEWAR VETERANCY: frame %u player %d '%s'%s%s at column %d row %d to rank %d for %d %s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, obj ? obj->getTemplate()->getName().str() : "?", man ? "'s passenger " : "",
							man ? man->thing->getName().str() : "", argCount >= 2 ? args[ 1 ] : -1, argCount >= 3 ? args[ 2 ] : -1,
							veterancy ? *veterancy + 1 : -1, price, refused ? "refused: " : "bought", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	player->getMoney()->withdraw( (UnsignedInt)price, FALSE );
	++*veterancy;
	*paid += price;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** (grid, column, row, template id): an infantryman of the sender's roster, under the same tier and
	  money rules as a purchase, bought into his plot unit covering that slot.  The unit must take him,
	  with room left as its container counts it, and he must be able to fire out of where he lands (a
	  Battle Bus, a Humvee, an Overlord's or a Helix's bunker; not a Troop Crawler, whose men cannot).
	  He takes no slot and is held inside, as the unit is; a wave copy of the unit goes out with a copy
	  of him in it.  The hero keeps his own slot and is never a passenger. */
Bool LaneWarDirector::garrison( Player *player, const Int *args, Int argCount )
{
	Int team = 0, seat = 0;
	const Int index = argCount >= 4 && args[ 0 ] >= 0 && args[ 0 ] < LANEWAR_GRIDS ? plotUnitAt( player, args[ 0 ], args[ 1 ], args[ 2 ] ) : -1;
	Object *host = index >= 0 ? TheGameLogic->findObjectByID( m_plotUnits[ index ].id ) : NULL;
	const ThingTemplate *tmpl = argCount >= 4 && args[ 3 ] >= 0 && args[ 3 ] <= 0xFFFF ? TheThingFactory->findByTemplateID( (UnsignedShort)args[ 3 ] ) : NULL;
	const LaneWarRosterItem *item = NULL;
	LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
	const Int listed = host != NULL ? LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX ) : 0;
	for( Int i = 0; i < listed && item == NULL; ++i )
		if( roster[ i ].thing == tmpl )
			item = &roster[ i ];
	ContainModuleInterface *contain = host != NULL ? host->getContain() : NULL;
	const Int cost = tmpl != NULL && player != NULL ? tmpl->calcCostToBuild( player ) : 0;

	const char *refused = NULL;
	if( host == NULL || !seatOf( player, &team, &seat ) )
		refused = "none of his units there";
	else if( tmpl == NULL )
		refused = "no such unit";
	else if( item == NULL )
		refused = "not on the side's roster";
	else if( item->tier > m_techLevel[ team ][ seat ] )
		refused = "tier locked";
	else if( item->hero )
		refused = "the hero keeps his own slot";
	else if( !tmpl->isKindOf( KINDOF_INFANTRY ) )
		refused = "only infantry rides";
	else if( contain == NULL )
		refused = "it carries nobody";
	else if( (Int)player->getMoney()->countMoney() < cost )
		refused = "too dear";

	Object *man = NULL;
	if( refused == NULL )
	{
		man = TheThingFactory->newObject( tmpl, player->getDefaultTeam() );
		if( man == NULL )
			refused = "not made";
	}
	if( man != NULL )
	{
		man->setPosition( host->getPosition() );
		for( BehaviorModule **m = man->getBehaviorModules(); *m; ++m )
		{
			CreateModuleInterface *create = (*m)->getCreate();
			if( create )
				create->onBuildComplete();
		}
		if( !contain->isValidContainerFor( man, TRUE ) )
			refused = "no room for him in it";
		else
		{
			contain->addToContain( man );
			Object *in = man->getContainedBy();
			if( in == NULL || in->getContain() == NULL || !in->getContain()->isPassengerAllowedToFire( man->getID() ) )
			{
				refused = "its passengers cannot fire out";
				if( in != NULL && in->getContain() != NULL )
					in->getContain()->removeFromContain( man );
			}
		}
		if( refused != NULL )
		{
			TheGameLogic->destroyObject( man );
			man = NULL;
		}
	}

	DEBUG_LOG(( "LANEWAR GARRISON: frame %u player %d '%s' into '%s' at column %d row %d for %d %s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, tmpl ? tmpl->getName().str() : "?", host ? host->getTemplate()->getName().str() : "?",
							argCount >= 2 ? args[ 1 ] : -1, argCount >= 3 ? args[ 2 ] : -1, cost, refused ? "refused: " : "bought", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	player->getMoney()->withdraw( (UnsignedInt)cost, FALSE );
	holdOnPlot( man );
	PlotUnit::Passenger p;
	p.id = man->getID();
	p.thing = tmpl;
	p.cost = cost;
	p.veterancy = LEVEL_REGULAR;
	p.veterancyPaid = 0;
	m_plotUnits[ index ].passengers.push_back( p );
	DEBUG_LOG(( "LANEWAR GARRISON: frame %u '%s' carries %d passengers, %d of %d places taken\n", TheGameLogic->getFrame(),
							host->getTemplate()->getName().str(), (Int)m_plotUnits[ index ].passengers.size(), (Int)contain->getContainCount(),
							contain->getContainMax() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** (grid, column, row, button slot): the object upgrade on that slot of the sender's plot unit's
	  command set, an Overlord's or a Helix's add-on or an American drone, at the game's own price.
	  Refused when the unit has it, or when an upgrade it has rules it out the way the game's own button
	  greys (UpgradeMux's ConflictsWith: one drone a vehicle, one add-on an Overlord); no science or
	  building is asked for, Lane War having neither.  The plot unit takes it, so a bunker can be
	  garrisoned there, and whatever it makes (the add-on, the drone) is held with it; every wave copy is
	  given the same upgrades in the same order. */
Bool LaneWarDirector::buyUnitUpgrade( Player *player, const Int *args, Int argCount )
{
	const Int index = argCount >= 4 && args[ 0 ] >= 0 && args[ 0 ] < LANEWAR_GRIDS ? plotUnitAt( player, args[ 0 ], args[ 1 ], args[ 2 ] ) : -1;
	Object *obj = index >= 0 ? TheGameLogic->findObjectByID( m_plotUnits[ index ].id ) : NULL;
	const UpgradeTemplate *upgrade = unitUpgradeAt( obj, argCount >= 4 ? args[ 3 ] : -1 );
	const Int price = upgrade != NULL ? upgrade->calcCostToBuild( player ) : 0;
	const char *refused = NULL;
	if( obj == NULL )
		refused = "none of his units there";
	else if( upgrade == NULL )
		refused = "no object upgrade on that button";
	else if( obj->hasUpgrade( upgrade ) )
		refused = "it has it already";
	else if( !obj->affectedByUpgrade( upgrade ) )
		refused = "an upgrade it has rules it out";
	else if( (Int)player->getMoney()->countMoney() < price )
		refused = "too dear";

	DEBUG_LOG(( "LANEWAR UNIT UPGRADE: frame %u player %d '%s' at column %d row %d button %d '%s' for %d %s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, obj ? obj->getTemplate()->getName().str() : "?", argCount >= 2 ? args[ 1 ] : -1,
							argCount >= 3 ? args[ 2 ] : -1, argCount >= 4 ? args[ 3 ] : -1, upgrade ? upgrade->getUpgradeName().str() : "?", price,
							refused ? "refused: " : "bought", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	player->getMoney()->withdraw( (UnsignedInt)price, FALSE );
	obj->giveUpgrade( upgrade );
	PlotUnit &u = m_plotUnits[ index ];
	u.upgrades.push_back( upgrade );
	u.upgradesPaid += price;
	std::vector<Object *> cargo;
	cargoOf( obj, &cargo );
	for( size_t i = 0; i < cargo.size(); ++i )
		holdOnPlot( cargo[ i ] );
	DEBUG_LOG(( "LANEWAR UNIT UPGRADE: frame %u '%s' has %d upgrades, %d riding along held\n", TheGameLogic->getFrame(),
							obj->getTemplate()->getName().str(), (Int)u.upgrades.size(), (Int)cargo.size() ));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Int LaneWarDirector::getNextPassengerVeterancyPrice( Int plotIndex, Int passenger ) const
{
	const PlotUnit::Passenger &p = m_plotUnits[ plotIndex ].passengers[ passenger ];
	return LaneWar_veterancyPrice( p.cost, p.veterancy + 1 );
}

Int LaneWarDirector::getUnitUpgrades( Int plotIndex, UnitUpgrade *out, Int maxItems ) const
{
	const PlotUnit &u = m_plotUnits[ plotIndex ];
	const Object *obj = TheGameLogic->findObjectByID( u.id );
	Player *player = obj != NULL ? obj->getControllingPlayer() : NULL;
	Int count = 0;
	for( Int slot = 0; player != NULL && slot < MAX_COMMANDS_PER_SET && count < maxItems; ++slot )
	{
		const UpgradeTemplate *upgrade = unitUpgradeAt( obj, slot );
		if( upgrade == NULL )
			continue;
		out[ count ].slot = slot;
		out[ count ].upgrade = upgrade;
		out[ count ].price = upgrade->calcCostToBuild( player );
		out[ count ].owned = obj->hasUpgrade( upgrade );
		out[ count ].available = !out[ count ].owned && obj->affectedByUpgrade( upgrade );
		++count;
	}
	return count;
}

Int LaneWarDirector::getSellValue( Int plotIndex ) const
{
	return sellValueOf( m_plotUnits[ plotIndex ] );
}

void LaneWarDirector::getPassengerRoom( Int plotIndex, Int *used, Int *most ) const
{
	Object *obj = TheGameLogic->findObjectByID( m_plotUnits[ plotIndex ].id );
	ContainModuleInterface *contain = obj != NULL ? obj->getContain() : NULL;
	*used = 0;
	*most = 0;
	// no room where the garrison order takes nobody: an Overlord without its bunker, a Troop Crawler
	if( contain == NULL || !contain->isPassengerAllowedToFire() )
		return;
	// the places the game's own pips count: slots, not men (a big passenger takes more than one), and
	// an Overlord's bunker in place of the carrier; the same sum for a container that hides its pips
	if( !contain->getContainerPipsToShow( *most, *used ) )
	{
		*most = contain->getContainMax();
		*used = (Int)contain->getContainCount() + contain->getExtraSlotsInUse();
	}
	*most = max( 0, *most );
}

Int LaneWarDirector::getAirborneJets( Coord3D *where, Int *playerIndex, Int most ) const
{
	Int found = 0;
	for( size_t i = 0; i < m_jets.size() && found < most; ++i )
	{
		const Jet &jet = m_jets[ i ];
		const Object *obj = jet.state != JET_PARKED ? TheGameLogic->findObjectByID( jet.id ) : NULL;
		if( obj == NULL || obj->isEffectivelyDead() )
			continue;
		where[ found ] = *obj->getPosition();
		playerIndex[ found ] = m_seatPlayer[ jet.team ][ jet.seat ];
		++found;
	}
	return found;
}

//-------------------------------------------------------------------------------------------------
/// one of a template's abilities switched on or off for the sender's wave units, kept only where it differs from the default
Bool LaneWarDirector::setAbility( Player *player, const Int *args, Int argCount )
{
	Int team, seat;
	const Bool argsOk = LaneWar_abilityOrderArgsOk( args, argCount );
	const ThingTemplate *tmpl = argsOk ? TheThingFactory->findByTemplateID( (UnsignedShort)args[ 0 ] ) : NULL;
	const LaneWarAbility *ability = NULL;
	LaneWarAbility list[ LANEWAR_ABILITIES_MAX ];
	const Int listed = LaneWar_abilitiesOf( tmpl, list, LANEWAR_ABILITIES_MAX );
	for( Int i = 0; i < listed && ability == NULL; ++i )
		if( list[ i ].slot == args[ 1 ] )
			ability = &list[ i ];
	const char *refused = NULL;
	if( !m_active || !seatOf( player, &team, &seat ) )
		refused = "not seated in a lane game";
	else if( !argsOk )
		refused = "bad arguments";
	else if( tmpl == NULL )
		refused = "no such template";
	else if( ability == NULL )
		refused = "not one of its abilities";

	DEBUG_LOG(( "LANEWAR ABILITY: frame %u player %d '%s' slot %d '%s' %s%s%s\n", TheGameLogic->getFrame(),
							player ? (Int)player->getPlayerIndex() : -1, tmpl ? tmpl->getName().str() : "?", argCount >= 2 ? args[ 1 ] : -1,
							ability ? ability->button->getName().str() : "?", argCount >= 3 && args[ 2 ] == 1 ? "on" : "off",
							refused ? ", refused: " : ability && ability->defaultOn == ( args[ 2 ] == 1 ) ? ", the default" : "", refused ? refused : "" ));
	if( refused != NULL )
		return FALSE;

	LaneWar_setAbilityToggle( m_abilityToggles, player->getPlayerIndex(), args[ 0 ], args[ 1 ], args[ 2 ] == 1, ability->defaultOn );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarDirector::isAbilityOn( Int player, const ThingTemplate *tmpl, Int slot ) const
{
	LaneWarAbility list[ LANEWAR_ABILITIES_MAX ];
	const Int listed = LaneWar_abilitiesOf( tmpl, list, LANEWAR_ABILITIES_MAX );
	for( Int i = 0; i < listed; ++i )
		if( list[ i ].slot == slot )
			return LaneWar_abilityToggleOn( m_abilityToggles, player, tmpl->getTemplateID(), slot, list[ i ].defaultOn );
	return FALSE;
}

/** A live button of a wave unit, which an upgrade may have put in another command set than its template's:
	  the switch of the template's button that is the same one, else that button's default.  The detonator
	  goes with the charges it sets off and the primary switch back with the slot switched to, so both are on. */
Bool LaneWarDirector::buttonOn( Int player, const ThingTemplate *tmpl, const CommandButton *button ) const
{
	const Int type = button->getCommandType();
	if( type == GUI_COMMAND_SWITCH_WEAPON && button->getWeaponSlot() == PRIMARY_WEAPON )
		return TRUE;
	if( type == GUI_COMMAND_SPECIAL_POWER && ( button->getOptions() & NEED_TARGET_ENEMY_OBJECT ) == 0 )
		return TRUE;
	LaneWarAbility list[ LANEWAR_ABILITIES_MAX ];
	const Int listed = LaneWar_abilitiesOf( tmpl, list, LANEWAR_ABILITIES_MAX );
	for( Int i = 0; i < listed; ++i )
		if( list[ i ].button == button )
			return LaneWar_abilityToggleOn( m_abilityToggles, player, tmpl->getTemplateID(), list[ i ].slot, list[ i ].defaultOn );
	return LaneWar_abilityDefaultOn( type, specialPowerOf( button ) );
}

//-------------------------------------------------------------------------------------------------
/** Send the unit on towards the enemy fortress of its owner's team, on its own line, as groupLaneMarch
	  did; nothing for a unit already level with it. */
void LaneWarDirector::march( Object *obj, Int team )
{
	AIUpdateInterface *ai = obj->getAI();
	if( ai == NULL )
		return;
	const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ 1 - team ] );
	Coord3D goal = *obj->getPosition();
	goal.x = fortress != NULL ? fortress->getPosition()->x : m_spawn[ 1 - team ].x;
	if( ( goal.x - obj->getPosition()->x ) * towardEnemy( team ) <= (Real)LANEWAR_SLOT_SIZE )
		return;
	goal.z = TheTerrainLogic->getGroundHeight( goal.x, goal.y );
	if( obj->isAbleToAttack() )
		ai->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
	else
		ai->aiMoveToPosition( &goal, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/// what a walk-up goes for first: the enemy's tower, then his fortress, then any other building, then a vehicle
static Int walkUpRank( const Object *target, const ObjectID *towers, ObjectID fortress )
{
	if( target->getID() == towers[ 0 ] || target->getID() == towers[ 1 ] )
		return 0;
	if( target->getID() == fortress )
		return 1;
	if( target->isKindOf( KINDOF_STRUCTURE ) )
		return 2;
	if( target->isKindOf( KINDOF_VEHICLE ) )
		return 3;
	return -1;
}

/// another wave unit of the same player is on its way into the target
Bool LaneWarDirector::enteredByAnother( const Object *obj, const Object *target ) const
{
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Object *other = TheGameLogic->findObjectByID( m_copies[ i ].id );
		AIUpdateInterface *ai = other != NULL && other != obj ? other->getAI() : NULL;
		if( ai != NULL && other->getControllingPlayer() == obj->getControllingPlayer() && !ai->isIdle() && ai->getGoalObject() == target )
			return TRUE;
	}
	return FALSE;
}

/** The lane's walk-up rule, in place of the skirmish AI's weighing of a base: within the leash (twice the
	  reach the special starts from, capped, and never under what the unit sees), the first enemy it may do it
	  to, buildings before vehicles in walkUpRank's order and by ObjectID inside a rank.  Charges, TNT, the
	  building hack, the captures and sabotage go for buildings; TNT and the timed charge for vehicles too,
	  where the remote one waits on a tank that drives off; the hijack for vehicles alone. */
Bool LaneWarDirector::walkUp( Object *obj, const CommandButton *button, Int enemyTeam )
{
	const Int type = button->getCommandType();
	const Bool hijack = type == GUICOMMANDMODE_HIJACK_VEHICLE;
	const Bool sabotage = type == GUICOMMANDMODE_SABOTAGE_BUILDING;
	const Int power = specialPowerOf( button );
	const SpecialAbilityUpdate *update = power != SPECIAL_INVALID ? obj->findSpecialAbilityUpdate( (SpecialPowerType)power ) : NULL;
	AIUpdateInterface *ai = obj->getAI();
	if( !hijack && !sabotage && ( update == NULL || !AIAbility_specialReady( obj, button ) ) )
		return FALSE;
	// a hijacker or a saboteur on its way in has its target already
	if( ( hijack || sabotage ) && !ai->isIdle() && ai->getGoalObject() != NULL )
		return FALSE;
	if( AIAbility_besideOwnCharge( obj ) )
		return FALSE;

	const Real start = update != NULL ? min( update->getStartAbilityRange(), 2000.0f ) : 0.0f;
	const Real leash = max( 2.0f * start, obj->getVisionRange() );
	const Bool buildings = !hijack;
	const Bool vehicles = hijack || power == SPECIAL_TIMED_CHARGES || power == SPECIAL_TANKHUNTER_TNT_ATTACK;
	std::vector<Object *> targets;
	AIAbility_enemiesNear( obj->getControllingPlayer(), obj->getPosition(), leash, &targets );
	for( Int rank = 0; rank < 4; ++rank )
	{
		if( rank < 3 ? !buildings : !vehicles )
			continue;
		for( size_t t = 0; t < targets.size(); ++t )
		{
			Object *target = targets[ t ];
			if( walkUpRank( target, m_tower[ enemyTeam ], m_fortress[ enemyTeam ] ) != rank || target->testStatus( OBJECT_STATUS_UNATTACKABLE ) )
				continue;
			if( power == SPECIAL_HACKER_DISABLE_BUILDING && target->isDisabled() )
				continue;		// already off
			if( ( hijack || sabotage ) && enteredByAnother( obj, target ) )
				continue;		// one hijacker a tank: the second walks there for nothing
			if( !button->isValidObjectTarget( obj, target ) )
				continue;
			const Bool can = hijack ? TheActionManager->canHijackVehicle( obj, target, CMD_FROM_AI )
										 : sabotage ? TheActionManager->canSabotageBuilding( obj, target, CMD_FROM_AI )
										 : TheActionManager->canDoSpecialPowerAtObject( obj, target, CMD_FROM_AI, button->getSpecialPowerTemplate(), button->getOptions() );
			if( !can )
				continue;
			DEBUG_LOG(( "LANEWAR ABILITY USE: frame %u player %d '%s' %d walk-up '%s' at '%s' %d, leash %.0f\n", TheGameLogic->getFrame(),
									(Int)obj->getControllingPlayer()->getPlayerIndex(), obj->getTemplate()->getName().str(), (Int)obj->getID(),
									button->getName().str(), target->getTemplate()->getName().str(), (Int)target->getID(), leash ));
			if( hijack )
				ai->aiEnter( target, CMD_FROM_AI );
			else
				obj->doCommandButtonAtObject( button, target, CMD_FROM_AI );
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** A button weapon that reaches less than half as far as the unit's own gun, the Dragon tank's wall of
	  fire or Colonel Burton's knife: the skirmish rule waits for an enemy inside that short reach, which a
	  lane fight, won or lost at the gun's reach, almost never brings. So the lane aims it at the nearest
	  enemy on the ground inside the gun's reach that the button takes (the knife only a man), ties to the
	  lower ObjectID, and the unit closes to use it as a player's click would. */
static Bool laneShortVolley( Object *obj, const AIAbilityButton &entry )
{
	const Bool atObject = ( entry.button->getOptions() & NEED_TARGET_ENEMY_OBJECT ) != 0;
	const Weapon *volley = obj->getWeaponInWeaponSlot( entry.slot );
	const Weapon *gun = obj->getWeaponInWeaponSlot( PRIMARY_WEAPON );
	if( volley == NULL || gun == NULL || volley == gun || volley->getStatus() != READY_TO_FIRE )
		return FALSE;
	const Real reach = gun->getAttackRange( obj );
	if( 2.0f * volley->getAttackRange( obj ) >= reach )
		return FALSE;
	std::vector<Object *> enemies;
	AIAbility_enemiesNear( obj->getControllingPlayer(), obj->getPosition(), reach, &enemies );
	Object *nearest = NULL;
	Real nearestSq = 0.0f;
	for( size_t i = 0; i < enemies.size(); ++i )
	{
		Object *enemy = enemies[ i ];
		if( enemy->isEffectivelyDead() || enemy->isKindOf( KINDOF_STRUCTURE ) || enemy->isAirborneTarget() )
			continue;
		if( atObject && ( !entry.button->isValidObjectTarget( obj, enemy )
				|| obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET, enemy, CMD_FROM_AI, entry.slot ) == ATTACKRESULT_NOT_POSSIBLE ) )
			continue;
		const Real dx = enemy->getPosition()->x - obj->getPosition()->x, dy = enemy->getPosition()->y - obj->getPosition()->y;
		const Real distSq = dx * dx + dy * dy;
		if( distSq > reach * reach )
			continue;
		if( nearest == NULL || distSq < nearestSq || ( distSq == nearestSq && enemy->getID() < nearest->getID() ) )
		{
			nearest = enemy;
			nearestSq = distSq;
		}
	}
	if( nearest == NULL )
		return FALSE;
	if( atObject )
		obj->doCommandButtonAtObject( entry.button, nearest, CMD_FROM_AI );
	else
		obj->doCommandButtonAtPosition( entry.button, nearest->getPosition(), CMD_FROM_AI );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The frame a copy that has just pressed a button in weapon slot `slot` may press again (2026-11-23): a
	  hero's after LANEWAR_HERO_COOLDOWN_SECONDS of game time whatever he pressed, a Dragon's wall of fire or
	  a Toxin Truck's slime after LANEWAR_PRESS_COOLDOWN_SECONDS (each unit's one button, so the rest covers
	  all its presses), anything else at once. */
static UnsignedInt pressCooldown( const Player *player, const Object *obj, Int slot, UnsignedInt now )
{
	if( LaneWar_isHero( player, obj->getTemplate() ) )
		return now + (UnsignedInt)( LANEWAR_HERO_COOLDOWN_SECONDS * LOGICFRAMES_PER_GAME_SECOND );
	const Weapon *weapon = slot >= 0 && slot < WEAPONSLOT_COUNT ? obj->getWeaponInWeaponSlot( (WeaponSlotType)slot ) : NULL;
	const AsciiString name = weapon != NULL ? weapon->getName() : AsciiString::TheEmptyString;
	if( strstr( name.str(), "FireWallWeapon" ) != NULL || strstr( name.str(), "ToxinTruckSprayer" ) != NULL )
		return now + (UnsignedInt)( LANEWAR_PRESS_COOLDOWN_SECONDS * LOGICFRAMES_PER_GAME_SECOND );
	return now;
}

//-------------------------------------------------------------------------------------------------
/** The wave units' own buttons, each copy looked at every half game second on its own frame of the
	  stride (ObjectID and frame, no state).  The skirmish AI's on-the-spot rules go first, at the rung that
	  weighs every target in reach, then the walk-ups, the remote charges before the rest; only what the
	  owner's switches leave on.  A copy taken by the enemy (the hijack) marches again for its taker, and
	  one standing idle short of the enemy fortress marches on. */
void LaneWarDirector::useAbilities( UnsignedInt now )
{
	const UnsignedInt stride = (UnsignedInt)max( 1, (Int)( LOGICFRAMES_PER_GAME_SECOND / 2 ) );
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Copy &copy = m_copies[ i ];
		if( ( (UnsignedInt)copy.id + now ) % stride != 0 )
			continue;
		Object *obj = TheGameLogic->findObjectByID( copy.id );
		AIUpdateInterface *ai = obj != NULL ? obj->getAI() : NULL;
		if( ai == NULL || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() )
			continue;
		Player *player = obj->getControllingPlayer();
		Int team, seat;
		if( !seatOf( player, &team, &seat ) )
			continue;
		const Int me = player->getPlayerIndex();
		if( copy.owner != me )
		{
			const Bool taken = copy.owner >= 0;
			copy.owner = me;
			if( taken )
			{
				DEBUG_LOG(( "LANEWAR ABILITY: frame %u '%s' %d is player %d's now and marches for team %s\n", now,
										obj->getTemplate()->getName().str(), (Int)obj->getID(), me, TEAM_NAME[ team ] ));
				march( obj, team );
				continue;
			}
		}
		if( AIAbility_unitBusy( obj ) )
			continue;
		// a copy resting after a capped press only walks on (pressCooldown)
		if( now < copy.pressFrame )
		{
			if( ai->isIdle() )
				march( obj, team );
			continue;
		}

		Object *victim = ai->getCurrentVictim();
		if( victim != NULL && ( victim->isEffectivelyDead() || player->getRelationship( victim->getTeam() ) != ENEMIES ) )
			victim = NULL;
		const ThingTemplate *tmpl = plotTemplateOf( obj->getTemplate() );	// the switches are the plot unit's
		Bool used = FALSE;
		const std::vector<AIAbilityButton> buttons = AIAbility_buttonsOf( obj );
		for( size_t b = 0; b < buttons.size() && !used; ++b )
		{
			const AIAbilityButton &entry = buttons[ b ];
			if( entry.kind == AIABILITY_WALKUP || !buttonOn( me, tmpl, entry.button ) )
				continue;
			switch( entry.kind )
			{
				case AIABILITY_LASER:			used = AIAbility_laser( player, NULL, obj, entry, victim ); break;
				case AIABILITY_SHOT:			used = AIAbility_shot( player, NULL, obj, entry, victim, 2 ) || laneShortVolley( obj, entry ); break;
				case AIABILITY_AREA:			used = AIAbility_area( player, NULL, obj, entry, victim, 2 ) || laneShortVolley( obj, entry ); break;
				case AIABILITY_TOGGLE:		used = AIAbility_toggle( player, NULL, obj, entry, victim ); break;
				case AIABILITY_SUICIDE:		used = AIAbility_suicide( player, NULL, obj, entry, victim ); break;
				case AIABILITY_DETONATE:	used = AIAbility_detonate( player, NULL, obj, entry, victim ); break;
				case AIABILITY_HACK:			used = AIAbility_hack( player, NULL, obj, entry, victim ); break;
				default:									break;
			}
			if( used )
			{
				copy.pressFrame = pressCooldown( player, obj, entry.slot, now );
				DEBUG_LOG(( "LANEWAR ABILITY USE: frame %u player %d '%s' %d on the spot '%s' slot %d, victim '%s' %d, rests until frame %u\n", now, me,
										tmpl->getName().str(), (Int)obj->getID(), entry.button->getName().str(), entry.index,
										victim ? victim->getTemplate()->getName().str() : "none", victim ? (Int)victim->getID() : 0, copy.pressFrame ));
			}
		}

		LaneWarAbility list[ LANEWAR_ABILITIES_MAX ];
		const Int listed = used ? 0 : abilitiesOfSet( tmpl, TheCommandStore->findCommandSet( obj->getCommandSetString() ), list, LANEWAR_ABILITIES_MAX );
		for( Int pass = 0; pass < 2 && !used; ++pass )
			for( Int a = 0; a < listed && !used; ++a )
			{
				const CommandButton *button = list[ a ].button;
				if( ( specialPowerOf( button ) == SPECIAL_REMOTE_CHARGES ) != ( pass == 0 ) || !isWalkUpButton( button ) || !buttonOn( me, tmpl, button ) )
					continue;
				used = walkUp( obj, button, 1 - team );
				if( used )
					copy.pressFrame = pressCooldown( player, obj, PRIMARY_WEAPON, now );
			}

		if( !used && ai->isIdle() )
			march( obj, team );
	}
}

//-------------------------------------------------------------------------------------------------
void LaneWarDirector::update( void )
{
	if( !m_active )
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	// the lifespans first, then what came out of wave units (their dead still listed, so a pilot finds
	// the vehicle he left), then the dead off the list
	expireCopies( now );
	adoptStrays( now );
	pruneCopies();
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
	payTowerBounties( now );
	if( m_fortressDown[ 0 ] || m_fortressDown[ 1 ] )
		return;

	useAbilities( now );
	updateJets( now );
	if( now % ( LANEWAR_CARGO_LOG_SECONDS * LOGICFRAMES_PER_SECOND ) == 0 )
		logCargo( now );
	LaneWarBot_update( this, now );		// the computer seats spend, through onOrder as a player does

	if( now >= m_nextTickFrame )
	{
		tick( now );
		m_nextTickFrame += LANEWAR_TICK_FRAMES;
	}
}

//-------------------------------------------------------------------------------------------------
/** A copy dies LANEWAR_LIFESPAN_FRAMES after it spawned.  It stays listed until pruneCopies, after
	  adoptStrays has seen what its death threw out. */
void LaneWarDirector::expireCopies( UnsignedInt now )
{
	Int expired = 0, out = 0;
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
		++out;
	}

	if( expired > 0 )
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u lifespan ended %d copies, %d still out\n", now, expired, out ));
}

/// the list drops its dead, in order
void LaneWarDirector::pruneCopies( void )
{
	size_t kept = 0;
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj != NULL && !obj->isEffectivelyDead() )
			m_copies[ kept++ ] = m_copies[ i ];
	}
	m_copies.resize( kept );
}

/// the object is a wave copy or a jet out of its squadron, listed still if dead this frame; *dieFrame its lifespan's end
Bool LaneWarDirector::waveDieFrame( ObjectID parent, UnsignedInt *dieFrame ) const
{
	if( parent == INVALID_ID )
		return FALSE;
	for( size_t i = 0; i < m_copies.size(); ++i )
		if( m_copies[ i ].id == parent )
		{
			*dieFrame = m_copies[ i ].dieFrame;
			return TRUE;
		}
	for( size_t i = 0; i < m_jets.size(); ++i )
		if( m_jets[ i ].state != JET_PARKED && m_jets[ i ].id == parent )
		{
			*dieFrame = m_jets[ i ].dieFrame;
			return TRUE;
		}
	return FALSE;
}

/** What comes out of a wave unit belongs to its wave: a Troop Crawler's men, which its TransportContain
	  makes on its first update, a frame after spawnWave has listed it, and the pilot EjectPilotDie's
	  creation list throws out of a ranked American vehicle or jet (its producer is the wreck), or anything
	  else a wave unit makes that a player could order about.  Each becomes a copy: not selectable, dying
	  when its parent's lifespan ends, marching from useAbilities once it stands idle outside.  Only the
	  ObjectIDs made since the last pass are looked at, in ID order, so a parent is always listed before
	  its child is.  A slaved unit (an Angry Mob's men, a drone) follows its master and is left to it; an
	  add-on rides along as spawnCargo leaves it. */
void LaneWarDirector::adoptStrays( UnsignedInt now )
{
	const Int next = (Int)TheGameLogic->getObjectIDCounter();
	for( Int id = max( (Int)m_strayWatch, 1 ); id < next; ++id )
	{
		Object *obj = TheGameLogic->findObjectByID( (ObjectID)id );
		if( obj == NULL || obj->isEffectivelyDead() || obj->getSlavedUpdateInterface() != NULL
				|| !( obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE ) )
				|| obj->isKindOf( KINDOF_DRONE ) || obj->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
			continue;
		Int team, seat;
		if( !seatOf( obj->getControllingPlayer(), &team, &seat ) )
			continue;
		UnsignedInt dieFrame = 0;
		ObjectID parent = obj->getProducerID();
		Bool ofWave = waveDieFrame( parent, &dieFrame );
		for( const Object *in = obj->getContainedBy(); in != NULL && !ofWave; in = in->getContainedBy() )
		{
			parent = in->getID();
			ofWave = waveDieFrame( parent, &dieFrame );
		}
		if( !ofWave )
			continue;
		Bool listed = FALSE;
		for( size_t i = 0; i < m_copies.size() && !listed; ++i )
			listed = m_copies[ i ].id == obj->getID();
		if( listed )
			continue;		// a passenger copy spawnCargo loaded

		obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
		Copy entry;
		entry.id = obj->getID();
		entry.dieFrame = dieFrame;
		entry.owner = obj->getControllingPlayer()->getPlayerIndex();
		m_copies.push_back( entry );
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u '%s' %d of %s%d out of wave unit %d joins its wave, selectable %d, %s, dies at frame %u\n", now,
								obj->getTemplate()->getName().str(), id, TEAM_NAME[ team ], seat + 1, (Int)parent, obj->isSelectable() ? 1 : 0, obj->getContainedBy() != NULL ? "riding inside it" : "on foot", dieFrame ));
	}
	m_strayWatch = (ObjectID)next;
}

//-------------------------------------------------------------------------------------------------
/** Lane War has no airfield or helipad.  A wave helicopter's clip that must go home to reload is
	  refilled here the frame it runs dry, before the object walk, so it never turns for a pad that is
	  not there.  Copies only; the jets reload off the map (updateJets), and a plot's units never fire. */
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
/** What a wave copy takes from its plot unit beyond the template: its object upgrades in the order they
	  were bought, which make its add-on or drone as they did on the plot (owned by the copy's player, not
	  selectable, and no wave copy: a drone counts for nothing at the middle and goes down with its
	  master), then a copy of every passenger at his own rank, loaded in the order he was.  A passenger
	  copy is a wave copy with the same lifespan, firing out of the unit, and marches on his own once
	  he is let out. */
void LaneWarDirector::spawnCargo( Object *copy, const PlotUnit &u, Team *owner, UnsignedInt now )
{
	if( u.upgrades.empty() && u.passengers.empty() )
		return;
	for( size_t i = 0; i < u.upgrades.size(); ++i )
		copy->giveUpgrade( u.upgrades[ i ] );
	std::vector<Object *> cargo;
	cargoOf( copy, &cargo );
	for( size_t i = 0; i < cargo.size(); ++i )
		cargo[ i ]->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );

	ContainModuleInterface *contain = copy->getContain();
	Int loaded = 0;
	for( size_t i = 0; i < u.passengers.size(); ++i )
	{
		const PlotUnit::Passenger &p = u.passengers[ i ];
		Object *man = TheThingFactory->newObject( p.thing, owner );
		if( man == NULL )
			continue;
		man->setPosition( copy->getPosition() );
		for( BehaviorModule **m = man->getBehaviorModules(); *m; ++m )
		{
			CreateModuleInterface *create = (*m)->getCreate();
			if( create )
				create->onBuildComplete();
		}
		man->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
		if( p.veterancy > LEVEL_REGULAR && man->getExperienceTracker() != NULL )
			man->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)p.veterancy, FALSE );
		if( contain == NULL || !contain->isValidContainerFor( man, TRUE ) )
		{
			TheGameLogic->destroyObject( man );
			continue;
		}
		contain->addToContain( man );
		++loaded;
		Copy entry;
		entry.id = man->getID();
		entry.dieFrame = now + LANEWAR_LIFESPAN_FRAMES;
		entry.owner = copy->getControllingPlayer()->getPlayerIndex();
		m_copies.push_back( entry );
	}
	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u '%s' %d goes out with %d upgrades, %d riding along, %d of %d passengers in it\n", now,
							copy->getTemplate()->getName().str(), (Int)copy->getID(), (Int)u.upgrades.size(), (Int)cargo.size(), loaded,
							(Int)u.passengers.size() ));
}

//-------------------------------------------------------------------------------------------------
/** A wave's jet copy joins its seat's squadron parked and rearmed, whatever the rest of it is doing,
	  and goes with the next strike the seat flies.  Its lifespan runs from this tick, as a copy's does.
	  One plot jet has one copy alive at most: while the last is in the squadron, parked or flying, the
	  wave makes none, and the one there takes the object upgrades the plot jet has now at its next
	  take-off and a rank bought since at once. */
void LaneWarDirector::joinSquadron( const PlotUnit &u, UnsignedInt now )
{
	const Object *original = TheGameLogic->findObjectByID( u.id );
	for( size_t i = 0; i < m_jets.size(); ++i )
	{
		Jet &have = m_jets[ i ];
		// its plot unit's, wherever that has been moved to; a jet from a save before version 8 by its slot
		const Bool same = have.plotID != INVALID_ID ? have.plotID == u.id
										: have.team == u.team && have.seat == u.seat && have.col == u.col && have.row == u.row && have.thing == original->getTemplate();
		if( !same )
			continue;
		have.upgrades = u.upgrades;
		if( u.veterancy > have.veterancy )
		{
			have.veterancy = u.veterancy;
			Object *flying = have.state != JET_PARKED ? TheGameLogic->findObjectByID( have.id ) : NULL;
			if( flying != NULL && flying->getExperienceTracker() != NULL && (Int)flying->getVeterancyLevel() < u.veterancy )
				flying->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)u.veterancy, FALSE );
		}
		DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' of %s%d has its copy in the squadron still, %d strikes flown, lifespan to frame %u: no second one, %d jets in all squadrons\n",
								now, have.thing->getName().str(), TEAM_NAME[ u.team ], u.seat + 1, have.strikes, have.dieFrame, (Int)m_jets.size() ));
		return;
	}
	Jet jet;
	jet.id = INVALID_ID;
	jet.plotID = u.id;
	jet.thing = original->getTemplate();
	jet.team = u.team;
	jet.seat = u.seat;
	jet.col = u.col;
	jet.row = u.row;
	jet.state = JET_PARKED;
	jet.dieFrame = now + LANEWAR_LIFESPAN_FRAMES;
	jet.stateFrame = now;
	jet.health = 0.0f;
	jet.veterancy = u.veterancy;
	jet.strikes = 0;
	jet.upgrades = u.upgrades;
	m_jets.push_back( jet );
	DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' of %s%d joins its squadron off the map, %d jets in all squadrons\n", now,
							jet.thing->getName().str(), TEAM_NAME[ u.team ], u.seat + 1, (Int)m_jets.size() ));
}

//-------------------------------------------------------------------------------------------------
/// the x past the map's border behind the team's own end, where its jets wait
Real LaneWarDirector::parkX( Int team ) const
{
	Region3D ext;
	TheTerrainLogic->getExtentIncludingBorder( &ext );
	return towardEnemy( team ) > 0.0f ? ext.lo.x - (Real)LANEWAR_JET_PARK_MARGIN : ext.hi.x + (Real)LANEWAR_JET_PARK_MARGIN;
}

//-------------------------------------------------------------------------------------------------
/** The team's front: the x of its wave copy furthest down the lane towards the enemy, the first in
	  spawn order on a tie, and how fast that one closes on him a frame.  FALSE with none out. */
Bool LaneWarDirector::frontOf( Int team, Real *x, Real *speed ) const
{
	const Real toward = towardEnemy( team );
	Bool found = FALSE;
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		const Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		Int t, s;
		if( obj == NULL || obj->isEffectivelyDead() || obj->isContained() || !seatOf( obj->getControllingPlayer(), &t, &s ) || t != team )
			continue;
		if( found && obj->getPosition()->x * toward <= *x * toward )
			continue;
		found = TRUE;
		*x = obj->getPosition()->x;
		const PhysicsBehavior *physics = obj->getPhysics();
		*speed = physics != NULL ? physics->getVelocity()->x * toward : 0.0f;
	}
	return found;
}

//-------------------------------------------------------------------------------------------------
/** The seat's rearmed parked jets take off together when LaneWar_strikeDue says so.  Each is made anew
	  off the map on its plot slot's line across the lane, each row a slot further back as on the
	  plot, a second copy of the same slot one plot's depth behind the first, and the lot
	  attack-moves straight along the lane to the meeting point at the slowest one's speed
	  (AIGroup::groupLaneMarch), striking what it meets there. */
void LaneWarDirector::launchJets( Int team, Int seat, UnsignedInt now )
{
	Real jetSpeed = 0.0f;
	Int ready = 0;
	for( size_t i = 0; i < m_jets.size(); ++i )
	{
		const Jet &jet = m_jets[ i ];
		if( jet.team != team || jet.seat != seat || jet.state != JET_PARKED || now < jet.stateFrame )
			continue;
		const Real speed = jetSpeedOf( jet.thing );
		jetSpeed = ready == 0 ? speed : min( jetSpeed, speed );
		++ready;
	}
	if( ready == 0 )
		return;

	Real myX = 0.0f, mySpeed = 0.0f, enemyX = 0.0f, enemySpeed = 0.0f;
	if( !frontOf( team, &myX, &mySpeed ) || !frontOf( 1 - team, &enemyX, &enemySpeed ) )
		return;
	const Real toward = towardEnemy( team );
	const Real park = parkX( team );
	const Real gap = ( enemyX - myX ) * toward;
	Real meetAhead = 0.0f;
	if( !LaneWar_strikeDue( gap, mySpeed, enemySpeed, ( myX - park ) * toward, jetSpeed, &meetAhead ) )
		return;
	const Real strikeX = myX + toward * meetAhead;

	Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ team ][ seat ] );
	Team *owner = player->getDefaultTeam();
	const Coord2D &plotLo = m_gridLo[ team ][ seat ][ LANEWAR_GRID_GROUND ], &plotHi = m_gridHi[ team ][ seat ][ LANEWAR_GRID_GROUND ];
	const Real plotFront = toward > 0.0f ? plotHi.x : plotLo.x;
	const Real plotDepth = plotHi.x - plotLo.x;
	AIGroup *group = TheAI->createGroup();
	for( size_t i = 0; i < m_jets.size(); ++i )
	{
		Jet &jet = m_jets[ i ];
		if( jet.team != team || jet.seat != seat || jet.state != JET_PARKED || now < jet.stateFrame )
			continue;
		Int stack = 0;
		for( size_t j = 0; j < i; ++j )
			stack += m_jets[ j ].team == team && m_jets[ j ].seat == seat && m_jets[ j ].col == jet.col && m_jets[ j ].row == jet.row
							 && m_jets[ j ].state == JET_OUT && m_jets[ j ].stateFrame == now;

		const Coord3D at = slotCentre( team, seat, LANEWAR_GRID_GROUND, jet.col, jet.row, sizeFor( jet.thing ) );
		Coord3D pos;
		pos.x = park + ( at.x - plotFront ) - toward * (Real)stack * plotDepth;
		pos.y = m_spawn[ team ].y + at.y - m_plotAnchor[ team ][ seat ].y;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		Object *obj = TheThingFactory->newObject( jet.thing, owner );
		if( obj == NULL )
			continue;
		obj->setOrientation( toward > 0.0f ? 0.0f : PI );
		obj->setPosition( &pos );
		for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
		{
			CreateModuleInterface *create = (*m)->getCreate();
			if( create )
				create->onBuildComplete();
		}
		obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_UNSELECTABLE ) );
		owner->setActive();
		for( size_t k = 0; k < jet.upgrades.size(); ++k )
			obj->giveUpgrade( jet.upgrades[ k ] );

		// born flying at its own height and at full speed, as a payload plane comes in
		AIUpdateInterface *ai = obj->getAI();
		JetAIUpdate *jetAI = ai ? ai->getJetAIUpdate() : NULL;
		if( jetAI != NULL )
		{
			jetAI->friend_setAllowAirLoco( TRUE );
			ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
		}
		Locomotor *loco = ai ? ai->getCurLocomotor() : NULL;
		PhysicsBehavior *physics = obj->getPhysics();
		if( loco != NULL )
		{
			pos.z += loco->getPreferredHeight();
			obj->setPosition( &pos );
			if( physics != NULL )
			{
				Coord3D force = *obj->getUnitDirectionVector2D();
				const Real factor = loco->getMaxSpeedForCondition( obj->getBodyModule()->getDamageState() ) * physics->getMass();
				force.x *= factor;
				force.y *= factor;
				force.z = 0.0f;
				physics->applyMotiveForce( &force );
			}
		}
		BodyModuleInterface *body = obj->getBodyModule();
		if( body != NULL && jet.health > 0.0f && jet.health < body->getHealth() )
			body->internalChangeHealth( jet.health - body->getHealth() );
		if( jet.veterancy > 0 && obj->getExperienceTracker() != NULL )
			obj->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)jet.veterancy, FALSE );

		group->add( obj );
		jet.id = obj->getID();
		jet.state = JET_OUT;
		jet.stateFrame = now;
		++jet.strikes;
		DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' %d of %s%d takes off on its strike %d from (%.0f,%.0f), health %.0f of %.0f\n", now,
								jet.thing->getName().str(), (Int)jet.id, TEAM_NAME[ team ], seat + 1, jet.strikes, pos.x, pos.y,
								body ? body->getHealth() : 0.0f, body ? body->getMaxHealth() : 0.0f ));
	}

	DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u %s%d strikes with %d jets at x %.0f: fronts %s x %.0f and %s x %.0f, gap %.0f, closing %.2f and %.2f a frame, jets %.2f a frame\n",
							now, TEAM_NAME[ team ], seat + 1, (Int)group->getAllIDs().size(), strikeX, TEAM_NAME[ team ], myX, TEAM_NAME[ 1 - team ], enemyX,
							gap, mySpeed, enemySpeed, jetSpeed ));
	if( group->isEmpty() )
	{
		TheAI->destroyGroup( group );
		return;
	}
	// timed for the meeting, but flown as any unit's attack-move down the lane to the enemy's end;
	// updateJets turns a jet that can hit air onto the enemy's aircraft first
	group->groupLaneMarch( enemyEndX( team ) - park, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
}

//-------------------------------------------------------------------------------------------------
/// the x a team's units march to: the enemy fortress, or the enemy spawn once it has fallen
Real LaneWarDirector::enemyEndX( Int team ) const
{
	const Object *fortress = TheGameLogic->findObjectByID( m_fortress[ 1 - team ] );
	return fortress != NULL ? fortress->getPosition()->x : m_spawn[ 1 - team ].x;
}

//-------------------------------------------------------------------------------------------------
/** The squadrons, every frame.  A jet whose lifespan is up dies wherever it is, and one shot down
	  leaves its squadron.  One out on a strike turns for home once its clip is spent, its pass is flown,
	  its own AI turns it home, or LANEWAR_STRIKE_FRAMES are up; one flying home parks the moment it is
	  past the map's border, keeping its health and rank, and is rearmed LANEWAR_JET_REARM_FRAMES later.
	  Every half game second each seat's rearmed jets are asked whether a strike is due. */
void LaneWarDirector::updateJets( UnsignedInt now )
{
	if( m_jets.empty() )
		return;

	Region3D ext;
	TheTerrainLogic->getExtentIncludingBorder( &ext );
	const UnsignedInt stride = (UnsignedInt)max( 1, (Int)( LOGICFRAMES_PER_GAME_SECOND / 2 ) );
	size_t kept = 0;
	for( size_t i = 0; i < m_jets.size(); ++i )
	{
		Jet jet = m_jets[ i ];
		Object *obj = jet.state == JET_PARKED ? NULL : TheGameLogic->findObjectByID( jet.id );
		if( jet.state != JET_PARKED && ( obj == NULL || obj->isEffectivelyDead() ) )
		{
			DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' %d of %s%d shot down on its strike %d\n", now, jet.thing->getName().str(),
									(Int)jet.id, TEAM_NAME[ jet.team ], jet.seat + 1, jet.strikes ));
			continue;
		}
		if( now >= jet.dieFrame )
		{
			if( obj != NULL )
			{
				// listed dead until the next pass, so the pilot its death throws out finds his wave
				Copy entry;
				entry.id = obj->getID();
				entry.dieFrame = jet.dieFrame;
				entry.owner = m_seatPlayer[ jet.team ][ jet.seat ];
				m_copies.push_back( entry );
				obj->kill();
			}
			DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' of %s%d lifespan ended %s after %d strikes\n", now, jet.thing->getName().str(),
									TEAM_NAME[ jet.team ], jet.seat + 1, obj != NULL ? "in the air" : "parked", jet.strikes ));
			continue;
		}

		AIUpdateInterface *ai = obj != NULL ? obj->getAI() : NULL;
		JetAIUpdate *jetAI = ai != NULL ? ai->getJetAIUpdate() : NULL;
		if( jet.state == JET_OUT && ai != NULL )
		{
			const Real endX = enemyEndX( jet.team );
			const Bool passFlown = ( endX - obj->getPosition()->x ) * towardEnemy( jet.team ) <= (Real)LANEWAR_SLOT_SIZE;
			const char *why = jetAI != NULL && jetAI->isOutOfSpecialReloadAmmo() ? "its clip spent"
											: jetAI != NULL && jetAI->isGoingHomeOnItsOwn() ? "its own AI turned it home"
											: ai->isIdle() && passFlown ? "its pass flown"
											: now >= jet.stateFrame + LANEWAR_STRIKE_FRAMES ? "the strike's time up" : NULL;
			if( why == NULL && ( ai->isIdle() || now % stride == 0 ) )
			{
				// enemy aircraft first for a jet that can hit them, looked for every half game second;
				// otherwise, or once none is up, the lane
				Object *air = airTargetFor( obj, ext );
				if( air != NULL && ai->getGoalObject() != air && now % stride == 0 )
				{
					ai->aiAttackObject( air, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
					DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' %d of %s%d goes for '%s' %d in the air at (%.0f,%.0f)\n", now,
											jet.thing->getName().str(), (Int)jet.id, TEAM_NAME[ jet.team ], jet.seat + 1, air->getTemplate()->getName().str(),
											(Int)air->getID(), air->getPosition()->x, air->getPosition()->y ));
				}
				else if( air == NULL && ai->isIdle() )
				{
					Coord3D goal = *obj->getPosition();
					goal.x = endX;
					goal.z = TheTerrainLogic->getGroundHeight( goal.x, goal.y );
					ai->aiAttackMoveToPosition( &goal, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT );
				}
			}
			if( why != NULL )
			{
				Coord3D home = *obj->getPosition();
				home.x = parkX( jet.team ) - towardEnemy( jet.team ) * (Real)LANEWAR_JET_PARK_MARGIN;
				home.z = TheTerrainLogic->getGroundHeight( home.x, home.y );
				// off the map, as a payload plane leaves it (HeadOffMapState)
				if( ai->getCurLocomotor() != NULL )
				{
					ai->getCurLocomotor()->setAllowInvalidPosition( TRUE );
					ai->getCurLocomotor()->setUltraAccurate( TRUE );
				}
				ai->aiMoveToPosition( &home, CMD_FROM_SCRIPT );
				jet.state = JET_HOME;
				jet.stateFrame = now;
				DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' %d of %s%d heads home from (%.0f,%.0f) after strike %d, %s\n", now,
										jet.thing->getName().str(), (Int)jet.id, TEAM_NAME[ jet.team ], jet.seat + 1, obj->getPosition()->x,
										obj->getPosition()->y, jet.strikes, why ));
			}
		}
		else if( jet.state == JET_HOME && !ext.isInRegionNoZ( obj->getPosition() ) )
		{
			jet.health = obj->getBodyModule() != NULL ? obj->getBodyModule()->getHealth() : 0.0f;
			jet.veterancy = obj->getExperienceTracker() != NULL ? (Int)obj->getExperienceTracker()->getVeterancyLevel() : 0;
			DEBUG_LOG(( "HEADLESS LANEWAR JET: frame %u '%s' %d of %s%d parks off the map at x %.0f after strike %d, health %.0f, rearmed at frame %u\n", now,
									jet.thing->getName().str(), (Int)jet.id, TEAM_NAME[ jet.team ], jet.seat + 1, obj->getPosition()->x, jet.strikes,
									jet.health, now + LANEWAR_JET_REARM_FRAMES ));
			TheGameLogic->destroyObject( obj );
			jet.id = INVALID_ID;
			jet.state = JET_PARKED;
			jet.stateFrame = now + LANEWAR_JET_REARM_FRAMES;
		}
		m_jets[ kept++ ] = jet;
	}
	m_jets.resize( kept );

	if( now % stride != 0 )
		return;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			if( m_seatPlayer[ t ][ s ] >= 0 )
				launchJets( t, s, now );
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
			// the level's pay with the middle's bonus, and for the middle's holder what the match's length adds
			const UnsignedInt pay = LaneWar_incomeAt( m_incomeLevel[ t ][ s ], holder == t ) + ( holder == t ? LaneWar_lateIncome( m_tickCount ) : 0 );
			paid[ t ] = max( paid[ t ], pay );
			ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] )->getMoney()->deposit( pay, FALSE );
			TheLaneWarStats->onIncome( m_seatPlayer[ t ][ s ], pay,
				LaneWar_incomeAt( m_incomeLevel[ t ][ s ], holder == t ) - LaneWar_incomeAt( m_incomeLevel[ t ][ s ], FALSE ) );
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
/** A Watch Tower that has fallen pays LANEWAR_TOWER_BOUNTY once to every seated player of the other team,
	  the frame it is found gone or dead, whoever's shot ended it (2026-11-23). */
void LaneWarDirector::payTowerBounties( UnsignedInt now )
{
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int k = 0; k < 2; ++k )
		{
			if( m_towerPaid[ t ][ k ] || m_tower[ t ][ k ] == INVALID_ID )
				continue;
			const Object *tower = TheGameLogic->findObjectByID( m_tower[ t ][ k ] );
			if( tower != NULL && !tower->isEffectivelyDead() )
				continue;
			m_towerPaid[ t ][ k ] = TRUE;
			const Int paid = 1 - t;
			AsciiString to, kept;
			for( Int s = 0; s < LANEWAR_SEATS; ++s )
			{
				AsciiString one;
				if( m_seatPlayer[ paid ][ s ] >= 0 )
				{
					Player *player = ThePlayerList->getNthPlayer( m_seatPlayer[ paid ][ s ] );
					player->getMoney()->deposit( LANEWAR_TOWER_BOUNTY, FALSE );
					TheLaneWarStats->onIncome( m_seatPlayer[ paid ][ s ], LANEWAR_TOWER_BOUNTY, 0 );
					one.format( " %d:%u", m_seatPlayer[ paid ][ s ], player->getMoney()->countMoney() );
					to.concat( one );
				}
				if( m_seatPlayer[ t ][ s ] >= 0 )
				{
					one.format( " %d:%u", m_seatPlayer[ t ][ s ], ThePlayerList->getNthPlayer( m_seatPlayer[ t ][ s ] )->getMoney()->countMoney() );
					kept.concat( one );
				}
			}
			DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tower %s%d down: %d to each seat of team %s, now [%s ]; team %s keeps [%s ]\n", now,
									TEAM_NAME[ t ], k + 1, (Int)LANEWAR_TOWER_BOUNTY, TEAM_NAME[ paid ], to.str(), TEAM_NAME[ t ], kept.str() ));
		}
}

//-------------------------------------------------------------------------------------------------
/** For the log only: the passengers out riding in their wave units and the drones out with their
	  masters, and how many of each have fired.  Counted mid-fight, since a wave rarely lives to the next tick. */
void LaneWarDirector::logCargo( UnsignedInt now ) const
{
	Int riding = 0, fired = 0, subdued = 0;
	for( size_t i = 0; i < m_copies.size(); ++i )
	{
		Object *obj = TheGameLogic->findObjectByID( m_copies[ i ].id );
		if( obj != NULL && !obj->isEffectivelyDead() && obj->isDisabledByType( DISABLED_SUBDUED ) )
			++subdued;
		if( obj == NULL || obj->isEffectivelyDead() || !obj->isContained() )
			continue;
		++riding;
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( weapon != NULL && weapon->getLastShotFrame() > 0 )
			{
				++fired;
				break;
			}
		}
	}
	// and the drones out with their masters: a live object a live wave copy made that rides in nothing
	Int drones = 0, dronesFired = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if( obj->getProducerID() == INVALID_ID || !obj->isKindOf( KINDOF_DRONE ) || obj->isEffectivelyDead() || obj->isContained() )
			continue;
		Bool ofCopy = FALSE;
		for( size_t i = 0; i < m_copies.size() && !ofCopy; ++i )
			ofCopy = m_copies[ i ].id == obj->getProducerID();
		if( !ofCopy )
			continue;
		++drones;
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
		{
			const Weapon *weapon = obj->getWeaponInWeaponSlot( (WeaponSlotType)slot );
			if( weapon != NULL && weapon->getLastShotFrame() > 0 )
			{
				++dronesFired;
				break;
			}
		}
	}
	if( riding > 0 || drones > 0 )
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u: %d passengers out riding, %d of them have fired; %d drones out with their masters, %d of them have fired\n",
								now, riding, fired, drones, dronesFired ));
	// and the wave units a Microwave Tank or an ECM Tank holds stunned
	if( subdued > 0 )
		DEBUG_LOG(( "HEADLESS LANEWAR: frame %u: %d wave units subdued\n", now, subdued ));

	// and a tower or fortress a Hacker has switched off
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const ObjectID ids[ 3 ] = { m_tower[ t ][ 0 ], m_tower[ t ][ 1 ], m_fortress[ t ] };
		for( Int k = 0; k < 3; ++k )
		{
			const Object *obj = TheGameLogic->findObjectByID( ids[ k ] );
			if( obj != NULL && obj->isDisabledByType( DISABLED_HACKED ) )
				DEBUG_LOG(( "HEADLESS LANEWAR: frame %u '%s' %d of team %s hacked off until frame %u\n", now, obj->getTemplate()->getName().str(),
										(Int)obj->getID(), TEAM_NAME[ t ], obj->getDisabledUntil( DISABLED_HACKED ) ));
			if( obj != NULL && obj->isDisabledByType( DISABLED_SUBDUED ) )
				DEBUG_LOG(( "HEADLESS LANEWAR: frame %u '%s' %d of team %s subdued\n", now, obj->getTemplate()->getName().str(), (Int)obj->getID(), TEAM_NAME[ t ] ));
		}
	}
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
		if( isJet( original->getTemplate() ) )
		{
			joinSquadron( u, now );
			continue;
		}

		const Coord3D at = slotCentre( team, seat, u.grid, u.col, u.row, u.size );
		Coord3D pos = m_spawn[ team ];
		pos.x += at.x - m_plotAnchor[ team ][ seat ].x;
		pos.y += at.y - m_plotAnchor[ team ][ seat ].y;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		Object *copy = TheThingFactory->newObject( waveTemplateOf( original->getTemplate() ), owner );
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
		if( u.veterancy > LEVEL_REGULAR && copy->getExperienceTracker() != NULL )
		{
			copy->getExperienceTracker()->setVeterancyLevel( (VeterancyLevel)u.veterancy, FALSE );
			DEBUG_LOG(( "HEADLESS LANEWAR: frame %u '%s' %d goes out at rank %d\n", now, copy->getTemplate()->getName().str(),
									(Int)copy->getID(), (Int)copy->getVeterancyLevel() ));
		}
		// the Microwave Tank's vehicle disabler: a set only the mode switches on (FixesReforged.ini); Black Lotus's
		// gun came with her template above (waveTemplateOf)
		if( LaneWar_hasModeWeaponSet( copy->getTemplate()->getName().str() ) )
		{
			copy->setWeaponSetFlag( WEAPONSET_PLAYER_UPGRADE );
			DEBUG_LOG(( "LANEWAR JOB: frame %u '%s' %d goes out with its Direct Strike weapons\n", now, copy->getTemplate()->getName().str(),
									(Int)copy->getID() ));
		}
		owner->setActive();
		TheAI->pathfinder()->addObjectToPathfindMap( copy );

		AIUpdateInterface *ai = copy->getAI();
		if( ai && !copy->isKindOf( KINDOF_AIRCRAFT )
				&& TheAI->pathfinder()->adjustDestination( copy, ai->getLocomotorSet(), &pos ) )
		{
			TheAI->pathfinder()->updateGoal( copy, &pos, LAYER_GROUND );
			copy->setPosition( &pos );
		}

		// a JetAIUpdate (the Comanche's) is born taxiing, waiting for a runway; a wave copy is born flying
		JetAIUpdate *jet = ai ? ai->getJetAIUpdate() : NULL;
		if( jet != NULL )
		{
			jet->friend_setAllowAirLoco( TRUE );
			ai->chooseLocomotorSet( LOCOMOTORSET_NORMAL );
			if( ai->getCurLocomotor() != NULL )
				pos.z += ai->getCurLocomotor()->getPreferredHeight();
			copy->setPosition( &pos );
		}
		spawnCargo( copy, u, owner, now );

		group->add( copy );
		Copy entry;
		entry.id = copy->getID();
		entry.dieFrame = now + LANEWAR_LIFESPAN_FRAMES;
		entry.owner = player->getPlayerIndex();
		m_copies.push_back( entry );
	}

	/* As far as the enemy fortress: the wave's middle walks onto its line, so the rows in front of the
		 fortress stop at it and fight it and the flanks come up level with it.  The pads stand behind
		 the fortresses, so the distance between them would walk a wave past the enemy's fortress; the pad
		 behind its own is passed on the way out, the pathfinder taking the middle rows round it.  The
		 upright plot of 2026-11-22 makes the copy 32 cells wide, so about half its columns stand
		 behind the 14-cell fortress and squeeze past it into the 15.5 cells a side the outer columns
		 already walk. */
	Real dx = m_spawn[ 1 - team ].x - m_spawn[ team ].x;
	const Object *enemyFortress = TheGameLogic->findObjectByID( m_fortress[ 1 - team ] );
	if( enemyFortress != NULL )
		dx = enemyFortress->getPosition()->x - m_spawn[ team ].x;
	const VecObjectID &ids = group->getAllIDs();
	DEBUG_LOG(( "HEADLESS LANEWAR: frame %u tick %u team %s seat %d (player %d) sends %d%s\n",
							now, m_tickCount, TEAM_NAME[ team ], seat + 1, (Int)player->getPlayerIndex(), (Int)ids.size(),
							heroHeld ? ", the hero held back: his last copy is still out" : "" ));
	TheLaneWarStats->onWave( (Int)player->getPlayerIndex(), (Int)ids.size() );

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
	  level and whether its hero is bought; the team holding the middle.
	  Version 3: each copy's owner, and the players' ability switches.
	  Version 4: the squadrons' jets.
	  Version 5: each plot unit's rank and what it cost.
	  Version 6: each plot unit's object upgrades and passengers.
	  Version 7: each jet's object upgrades.
	  Version 8: each jet's plot unit, and how far adoptStrays has looked.
	  Version 9: which towers' bounties are paid.
	  Version 10: each copy's next press frame. */
void LaneWarDirector::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 10;
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
		if( version >= 3 )
			xfer->xferInt( &m_copies[ i ].owner );
		else
			m_copies[ i ].owner = -1;		// taken as whoever holds it the first time it is looked at
		if( version >= 10 )
			xfer->xferUnsignedInt( &m_copies[ i ].pressFrame );
		else
			m_copies[ i ].pressFrame = 0;
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
			// a save from before the air rows went names grid 1 for a jet; there is one grid now
			if( u.grid < 0 || u.grid >= LANEWAR_GRIDS )
				u.grid = LANEWAR_GRID_GROUND;
		}
		else
		{
			u.grid = LANEWAR_GRID_GROUND;
			u.cost = 0;
			u.sent = TRUE;
		}
		if( version >= 5 )
		{
			xfer->xferInt( &u.veterancy );
			xfer->xferInt( &u.veterancyPaid );
		}
		else
		{
			u.veterancy = LEVEL_REGULAR;
			u.veterancyPaid = 0;
		}
		if( version >= 6 )
		{
			UnsignedInt upgrades = (UnsignedInt)u.upgrades.size();
			xfer->xferUnsignedInt( &upgrades );
			if( xfer->getXferMode() == XFER_LOAD )
				u.upgrades.clear();
			for( UnsignedInt k = 0; k < upgrades; ++k )
			{
				AsciiString name = xfer->getXferMode() == XFER_LOAD ? AsciiString::TheEmptyString : u.upgrades[ k ]->getUpgradeName();
				xfer->xferAsciiString( &name );
				if( xfer->getXferMode() == XFER_LOAD )
				{
					const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( name );
					if( upgrade != NULL )
						u.upgrades.push_back( upgrade );
				}
			}
			xfer->xferInt( &u.upgradesPaid );

			UnsignedInt passengers = (UnsignedInt)u.passengers.size();
			xfer->xferUnsignedInt( &passengers );
			if( xfer->getXferMode() == XFER_LOAD )
				u.passengers.resize( passengers );
			for( UnsignedInt k = 0; k < passengers; ++k )
			{
				PlotUnit::Passenger &p = u.passengers[ k ];
				xfer->xferObjectID( &p.id );
				AsciiString name = xfer->getXferMode() == XFER_LOAD ? AsciiString::TheEmptyString : p.thing->getName();
				xfer->xferAsciiString( &name );
				if( xfer->getXferMode() == XFER_LOAD )
					p.thing = TheThingFactory->findTemplate( name, FALSE );
				xfer->xferInt( &p.cost );
				xfer->xferInt( &p.veterancy );
				xfer->xferInt( &p.veterancyPaid );
			}
			if( xfer->getXferMode() == XFER_LOAD )
			{
				size_t kept = 0;
				for( size_t k = 0; k < u.passengers.size(); ++k )
					if( u.passengers[ k ].thing != NULL )
						u.passengers[ kept++ ] = u.passengers[ k ];
				u.passengers.resize( kept );
			}
		}
		else
		{
			u.upgrades.clear();
			u.upgradesPaid = 0;
			u.passengers.clear();
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

	if( version >= 3 )
	{
		count = (UnsignedInt)m_abilityToggles.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_abilityToggles.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			LaneWarAbilityToggle &t = m_abilityToggles[ i ];
			xfer->xferInt( &t.player );
			xfer->xferInt( &t.templateID );
			xfer->xferInt( &t.slot );
			xfer->xferBool( &t.on );
		}
	}

	if( version >= 4 )
	{
		count = (UnsignedInt)m_jets.size();
		xfer->xferUnsignedInt( &count );
		if( xfer->getXferMode() == XFER_LOAD )
			m_jets.resize( count );
		for( UnsignedInt i = 0; i < count; ++i )
		{
			Jet &jet = m_jets[ i ];
			xfer->xferObjectID( &jet.id );
			AsciiString name = jet.thing != NULL ? jet.thing->getName() : AsciiString::TheEmptyString;
			xfer->xferAsciiString( &name );
			if( xfer->getXferMode() == XFER_LOAD )
				jet.thing = TheThingFactory->findTemplate( name, FALSE );
			xfer->xferInt( &jet.team );
			xfer->xferInt( &jet.seat );
			xfer->xferInt( &jet.col );
			xfer->xferInt( &jet.row );
			xfer->xferInt( &jet.state );
			xfer->xferUnsignedInt( &jet.dieFrame );
			xfer->xferUnsignedInt( &jet.stateFrame );
			xfer->xferReal( &jet.health );
			xfer->xferInt( &jet.veterancy );
			xfer->xferInt( &jet.strikes );
			UnsignedInt upgrades = (UnsignedInt)jet.upgrades.size();
			if( version >= 7 )
				xfer->xferUnsignedInt( &upgrades );
			else
				upgrades = 0;
			if( xfer->getXferMode() == XFER_LOAD )
				jet.upgrades.clear();
			for( UnsignedInt k = 0; k < upgrades; ++k )
			{
				AsciiString upgradeName = xfer->getXferMode() == XFER_LOAD ? AsciiString::TheEmptyString : jet.upgrades[ k ]->getUpgradeName();
				xfer->xferAsciiString( &upgradeName );
				const UpgradeTemplate *upgrade = xfer->getXferMode() == XFER_LOAD ? TheUpgradeCenter->findUpgrade( upgradeName ) : NULL;
				if( upgrade != NULL )
					jet.upgrades.push_back( upgrade );
			}
			if( version >= 8 )
				xfer->xferObjectID( &jet.plotID );
			else
				jet.plotID = INVALID_ID;		// matched by its slot instead (joinSquadron)
		}
		if( xfer->getXferMode() == XFER_LOAD )
		{
			size_t kept = 0;
			for( size_t i = 0; i < m_jets.size(); ++i )
				if( m_jets[ i ].thing != NULL )
					m_jets[ kept++ ] = m_jets[ i ];
			m_jets.resize( kept );
		}
	}
	else
		m_jets.clear();

	if( version >= 8 )
		xfer->xferObjectID( &m_strayWatch );
	else
		m_strayWatch = INVALID_ID;

	// a match saved before the bounty pays none, for towers fallen or still to fall
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int k = 0; k < 2; ++k )
		{
			if( version >= 9 )
				xfer->xferBool( &m_towerPaid[ t ][ k ] );
			else
				m_towerPaid[ t ][ k ] = TRUE;
		}
}
