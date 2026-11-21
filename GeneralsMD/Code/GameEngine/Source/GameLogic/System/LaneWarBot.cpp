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

// FILE: LaneWarBot.cpp ///////////////////////////////////////////////////////////////////////////
//
// The Lane War computer seat.  See LaneWarBot.h.  A plot unit stays on the plot and every wave of the
// seat copies the whole plot, so money spent is never lost to a fight: saving for the next tier costs
// the seat nothing it already has, and the plan is mostly about what to buy first.  A step, at most
// one purchase, goes in this order: the income level the plan wants by now, the tech tier it wants,
// the hero, a jet when there is money to spare, the unit that trades best for its price against the
// enemy plots' units (AIMatchup_templates, the skirmish AI's own matchups), and once the plot is full
// or the money piles up, ranks and upgrades on the units it has.  Abilities stay at their defaults.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarBot.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

namespace
{
	/** The plan's numbers at the lobby's four levels, Easy, Medium, Hard and Brutal at ratings 0, 15, 30
		  and 45, and how far a rating outside them may carry each.  Between two levels a number runs on a
		  straight line, and past the end ones on the line of the nearest pair, so above Brutal the economy
		  and the tech keep coming earlier and the decisions faster. */
	const Real NO_TICK = (Real)LANEWAR_BOT_NEVER_TICK;
	const Real ANCHOR_RATING[ 4 ] = { AI_RATING_EASY, AI_RATING_MEDIUM, AI_RATING_HARD, AI_RATING_HARD + ( AI_RATING_HARD - AI_RATING_MEDIUM ) };

	struct PlanRow
	{
		Real at[ 4 ];
		Real lo, hi;
	};

	const PlanRow STEP_SECONDS	= { { 6.0f, 3.0f, 2.0f, 1.0f },			0.5f, 12.0f };
	/* An income level pays 67 a tick for its 1000, back in 15 ticks, and the first batch of bot matches
		 ended in 4 to 10 minutes, 8 to 20 ticks: the plan that bought all four early (Brutal then) lost to
		 the one that bought none.  So income comes late and only on the upper levels, and the stronger plans
		 differ in the tech they open, how well they pick and how little money they leave idle. */
	const PlanRow RESERVE				= { { 600.0f, 300.0f, 100.0f, 0.0f },	0.0f, 800.0f };
	const PlanRow INCOME_TICK[ LANEWAR_INCOME_TIERS ] =
	{
		{ { NO_TICK, NO_TICK, 10.0f, 8.0f },		0.0f, NO_TICK },
		{ { NO_TICK, NO_TICK, NO_TICK, 14.0f },	0.0f, NO_TICK },
		{ { NO_TICK, NO_TICK, NO_TICK, NO_TICK },	0.0f, NO_TICK },
		{ { NO_TICK, NO_TICK, NO_TICK, NO_TICK },	0.0f, NO_TICK },
	};
	const PlanRow TECH_TICK[ LANEWAR_TECH_TIERS - 1 ] =
	{
		{ { 12.0f, 4.0f, 3.0f, 2.0f },		0.0f, NO_TICK },
		{ { NO_TICK, 16.0f, 12.0f, 10.0f },	0.0f, NO_TICK },
	};
	const PlanRow HERO_TICK			= { { 6.0f, 3.0f, 2.0f, 1.0f },			0.0f, NO_TICK };
	const PlanRow JET_TICK			= { { NO_TICK, 10.0f, 6.0f, 5.0f },		0.0f, NO_TICK };
	const PlanRow EXTRAS_TICK		= { { NO_TICK, 12.0f, 8.0f, 6.0f },		0.0f, NO_TICK };
	const PlanRow COUNTER				= { { 0.0f, 0.5f, 1.0f, 1.0f },			0.0f, 1.0f };
	const PlanRow JITTER				= { { 1.0f, 0.3f, 0.1f, 0.05f },		0.0f, 1.0f };
	const Real SAVES_FROM_COUNTER = 0.25f;		///< a seat that weighs the enemy this much (Easy to Medium's midpoint) saves

	/// the row's number at the rating: piecewise straight through the four levels, carried on past both ends, held to its bounds
	Real planNumber( const PlanRow &row, Real rating )
	{
		Int k = 0;		// the pair of levels whose line it is read on
		while( k < 2 && rating > ANCHOR_RATING[ k + 1 ] )
			++k;
		const Real t = ( rating - ANCHOR_RATING[ k ] ) / ( ANCHOR_RATING[ k + 1 ] - ANCHOR_RATING[ k ] );
		const Real value = row.at[ k ] + t * ( row.at[ k + 1 ] - row.at[ k ] );
		return value < row.lo ? row.lo : value > row.hi ? row.hi : value;
	}

	const Int WAVE_SOON_SECONDS = 6;		///< this close to its own wave a seat stops waiting for a dearer unit
	const Int FEW_UNITS = 2;						///< with fewer units on the plot a seat buys units before anything else
	const Real FRONT_SHARE = 0.5f;			///< of the plot's value the plan wants in front units
	const Real BACK_RANGE = 220.0f;			///< a weapon reaching this far puts a unit in the back rows
	const Int SLOTS_MAX = 64;
	const Int UNIT_UPGRADES_MAX = 32;		///< more than a command set has buttons

	/// the longest reach of the template's default weapons
	Real reachOf( const ThingTemplate *tmpl )
	{
		const WeaponTemplateSet *set = tmpl->getWeaponTemplateSets().empty() ? NULL : tmpl->findWeaponTemplateSet( WeaponSetFlags() );
		Real reach = 0.0f;
		for( Int slot = 0; set != NULL && slot < WEAPONSLOT_COUNT; ++slot )
		{
			const WeaponTemplate *weapon = set->getNth( (WeaponSlotType)slot );
			if( weapon != NULL )
				reach = max( reach, weapon->getUnmodifiedAttackRange() );
		}
		return reach;
	}

	/// artillery and the long guns at the back, the other vehicles in front, infantry and helicopters between
	Int roleOf( const ThingTemplate *tmpl )
	{
		if( reachOf( tmpl ) >= BACK_RANGE )
			return LANEWAR_BOT_BACK;
		if( tmpl->isKindOf( KINDOF_VEHICLE ) && !tmpl->isKindOf( KINDOF_AIRCRAFT ) )
			return LANEWAR_BOT_FRONT;
		return LANEWAR_BOT_MIDDLE;
	}

	/// an enemy plot unit or passenger, what the seat's picks are weighed against
	struct Enemy
	{
		const ThingTemplate *thing;
		Real cost;
	};

	/// what a seat sees when it decides
	struct View
	{
		LaneWarDirector *director;
		Player *player;
		Int team, seat;
		const LaneWarBotPlan *plan;
		UnsignedInt now, tick;
		Int money, spend;
		Bool waveSoon;
		Int units;										///< its live plot units
		Real value, frontValue;				///< what they cost, and the front units' share of it
		Int slotsUsed, slotsTotal;		///< of its ground grid
		std::vector<Enemy> enemies;
		Real enemyValue;
	};

	/// the first free block for the template on the seat's plot, its role's rows first and the middle columns first
	Bool findSlot( const View &v, const ThingTemplate *tmpl, Int role, Int *col, Int *row )
	{
		const Int grid = LaneWarDirector::gridFor( tmpl );
		const Int size = LaneWarDirector::sizeFor( tmpl );
		Int rows[ SLOTS_MAX ], cols[ SLOTS_MAX ];
		const Int rowCount = LaneWarBot_rowOrder( grid == LANEWAR_GRID_AIR ? LANEWAR_BOT_FRONT : role,
																							v.director->getGridRows( v.team, v.seat, grid ), size, rows, SLOTS_MAX );
		const Int colCount = LaneWarBot_columnOrder( v.director->getGridCols( v.team, v.seat, grid ), size, cols, SLOTS_MAX );
		for( Int r = 0; r < rowCount; ++r )
			for( Int c = 0; c < colCount; ++c )
			{
				const char *why = v.director->whyNotPlace( v.player, tmpl, cols[ c ], rows[ r ] );
				// money is the caller's to weigh; only the room counts here
				if( why == NULL || strcmp( why, "too dear" ) == 0 )
				{
					*col = cols[ c ];
					*row = rows[ r ];
					return TRUE;
				}
				if( strcmp( why, "slot taken" ) != 0 && strcmp( why, "off the plot" ) != 0 )
					return FALSE;		// tier, roster, hero: no slot will do
			}
		return FALSE;
	}

	/// how the template trades for its price against the enemy plots, 0..1, a coin's 0.5 with no enemy units out
	Real counterOf( const View &v, const ThingTemplate *tmpl, Real cost )
	{
		if( v.enemyValue <= 0.0f )
			return 0.5f;
		Real sum = 0.0f;
		for( size_t i = 0; i < v.enemies.size(); ++i )
			sum += v.enemies[ i ].cost * AIMatchup_templates( tmpl, v.enemies[ i ].thing, cost, v.enemies[ i ].cost );
		return sum / v.enemyValue;
	}

	void order( View &v, Int kind, const Int *args, Int count, const char *what, const char *name, Int price )
	{
		DEBUG_LOG(( "LANEWAR BOT: frame %u tick %u player %d rating %.2f %s '%s' for %d with %d, %d units on the plot worth %.0f, %.0f of it in front\n",
								v.now, v.tick, (Int)v.player->getPlayerIndex(), v.plan->rating, what, name, price, v.money, v.units,
								v.value, v.frontValue ));
		v.director->onOrder( v.player, kind, args, count );
	}

	/** A unit of the roster: the best pick that fits on the plot, and whether it was bought.  `jets` picks
		  among the jets alone, otherwise among the ground units and helicopters.  *wanted is the price of the
		  pick it waits for, 0 when there is none. */
	Bool buyUnit( View &v, Bool jets, Int *wanted )
	{
		const LaneWarBotPlan &plan = *v.plan;
		*wanted = 0;
		LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
		const Int listed = LaneWar_roster( v.player, roster, LANEWAR_ROSTER_MAX );

		struct Pick
		{
			const ThingTemplate *thing;
			Int cost, role, col, row, size;
			Bool suicide;		///< it ends itself to hit: the matchup prices one blast as a kill, and in the lane most die before they reach
		};
		Pick picks[ LANEWAR_ROSTER_MAX ];
		Int count = 0;
		Real densest = 0.0f;
		for( Int i = 0; i < listed; ++i )
		{
			const ThingTemplate *tmpl = roster[ i ].thing;
			if( roster[ i ].hero || roster[ i ].tier > v.director->getTechLevel( v.team, v.seat ) || LaneWarDirector::isJet( tmpl ) != jets )
				continue;
			Pick &p = picks[ count ];
			p.thing = tmpl;
			p.cost = tmpl->calcCostToBuild( v.player );
			p.role = roleOf( tmpl );
			p.size = LaneWarDirector::sizeFor( tmpl );
			p.suicide = FALSE;
			for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
				p.suicide = p.suicide || AIAbility_slotIsSuicide( tmpl, (WeaponSlotType)slot );
			if( p.cost <= 0|| !findSlot( v, tmpl, p.role, &p.col, &p.row ) )
				continue;
			densest = max( densest, (Real)p.cost / (Real)( p.size * p.size ) );
			++count;
		}

		// the score: the matchup, the front the plan wants, value per slot as the plot fills, and the dice
		const Real frontShare = v.value > 0.0f ? v.frontValue / v.value : 0.0f;
		const Real fill = v.slotsTotal > 0 ? (Real)v.slotsUsed / (Real)v.slotsTotal : 0.0f;
		Int best = -1, bestAffordable = -1;
		Real bestScore = 0.0f, bestAffordableScore = 0.0f;
		for( Int i = 0; i < count; ++i )
		{
			const Pick &p = picks[ i ];
			// the judgement grows with the rating: none of it at Easy, where the dice alone pick
			Real score = GameLogicRandomValueReal( 0.0f, plan.jitter );
			if( plan.counterWeight > 0.0f )
				score += plan.counterWeight * ( p.suicide ? min( counterOf( v, p.thing, (Real)p.cost ), 0.5f ) : counterOf( v, p.thing, (Real)p.cost ) );
			if( ( p.role == LANEWAR_BOT_FRONT ) == ( frontShare < FRONT_SHARE ) )
				score += 0.2f * plan.counterWeight;
			if( densest > 0.0f )
				score += 0.3f * plan.counterWeight * fill * (Real)p.cost / (Real)( p.size * p.size ) / densest;
			if( best < 0 || score > bestScore )
			{
				best = i;
				bestScore = score;
			}
			if( p.cost <= v.spend && ( bestAffordable < 0 || score > bestAffordableScore ) )
			{
				bestAffordable = i;
				bestAffordableScore = score;
			}
		}

		// a plan that saves waits for its pick, until its own wave is about to go
		const Int chosen = plan.saves && !v.waveSoon && v.units >= FEW_UNITS ? best : bestAffordable;
		if( chosen < 0 )
			return FALSE;
		const Pick &p = picks[ chosen ];
		if( p.cost > v.spend )
		{
			*wanted = p.cost;
			return FALSE;
		}
		const Int args[ 3 ] = { (Int)p.thing->getTemplateID(), p.col, p.row };
		order( v, LANEWAR_ORDER_PLACE, args, 3, jets ? "buys the jet" : "buys", p.thing->getName().str(), p.cost );
		return TRUE;
	}

	/// the hero, once a match, among the middle rows
	Bool buyHero( View &v, Bool *waiting )
	{
		*waiting = FALSE;
		LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
		const Int listed = LaneWar_roster( v.player, roster, LANEWAR_ROSTER_MAX );
		for( Int i = 0; i < listed; ++i )
		{
			if( !roster[ i ].hero )
				continue;
			const ThingTemplate *tmpl = roster[ i ].thing;
			Int col, row;
			if( !findSlot( v, tmpl, LANEWAR_BOT_MIDDLE, &col, &row ) )
				return FALSE;
			const Int cost = tmpl->calcCostToBuild( v.player );
			if( cost > v.money )
			{
				*waiting = TRUE;
				return FALSE;
			}
			const Int args[ 3 ] = { (Int)tmpl->getTemplateID(), col, row };
			order( v, LANEWAR_ORDER_PLACE, args, 3, "buys the hero", tmpl->getName().str(), cost );
			return TRUE;
		}
		return FALSE;
	}

	/** Ranks and upgrades on what the seat has, each only when the money is twice its price or the plot has
		  no room left: a player upgrade worth at least twice its price in the units it touches, then the
		  dearest unit's own upgrade, then a rank on the dearest unit. */
	Bool buyExtra( View &v, Bool plotFull )
	{
		const std::vector<LaneWarDirector::PlotUnit> &units = v.director->getPlotUnits();
		const Int least = plotFull ? 1 : 2;

		const UpgradeTemplate *list[ LANEWAR_ROSTER_MAX ];
		const Int upgrades = LaneWar_upgrades( v.player, list, LANEWAR_ROSTER_MAX );
		Int bestUpgrade = -1;
		Real bestWorth = 0.0f;
		for( Int k = 0; k < upgrades; ++k )
		{
			const Int price = list[ k ]->calcCostToBuild( v.player );
			if( v.player->hasUpgradeComplete( list[ k ] ) || price <= 0 || price * least > v.spend )
				continue;
			Real touched = 0.0f;
			for( size_t i = 0; i < units.size(); ++i )
			{
				const LaneWarDirector::PlotUnit &u = units[ i ];
				const Object *obj = u.team == v.team && u.seat == v.seat ? TheGameLogic->findObjectByID( u.id ) : NULL;
				if( obj != NULL && obj->affectedByUpgrade( list[ k ] ) )
					touched += (Real)u.cost;
				for( size_t m = 0; obj != NULL && m < u.passengers.size(); ++m )
				{
					const Object *man = TheGameLogic->findObjectByID( u.passengers[ m ].id );
					if( man != NULL && man->affectedByUpgrade( list[ k ] ) )
						touched += (Real)u.passengers[ m ].cost;
				}
			}
			const Real worth = touched / (Real)price;
			if( worth >= 2.0f && worth > bestWorth )
			{
				bestUpgrade = k;
				bestWorth = worth;
			}
		}
		if( bestUpgrade >= 0 )
		{
			const Int args[ 1 ] = { bestUpgrade };
			order( v, LANEWAR_ORDER_UPGRADE, args, 1, "researches", list[ bestUpgrade ]->getUpgradeName().str(),
						 list[ bestUpgrade ]->calcCostToBuild( v.player ) );
			return TRUE;
		}

		// the dearest unit first, the earliest bought on a tie
		Int dearest = -1;
		for( size_t i = 0; i < units.size(); ++i )
		{
			const LaneWarDirector::PlotUnit &u = units[ i ];
			if( u.team != v.team || u.seat != v.seat || TheGameLogic->findObjectByID( u.id ) == NULL )
				continue;
			if( dearest < 0 || u.cost > units[ dearest ].cost )
				dearest = (Int)i;
		}
		if( dearest < 0 )
			return FALSE;
		const LaneWarDirector::PlotUnit &u = units[ dearest ];
		const Object *obj = TheGameLogic->findObjectByID( u.id );

		LaneWarDirector::UnitUpgrade own[ UNIT_UPGRADES_MAX ];
		const Int owned = v.director->getUnitUpgrades( dearest, own, UNIT_UPGRADES_MAX );
		for( Int k = 0; k < owned; ++k )
			if( own[ k ].available && own[ k ].price > 0 && own[ k ].price * least <= v.spend )
			{
				const Int args[ 4 ] = { u.grid, u.col, u.row, own[ k ].slot };
				order( v, LANEWAR_ORDER_UNIT_UPGRADE, args, 4, "upgrades", obj->getTemplate()->getName().str(), own[ k ].price );
				return TRUE;
			}

		const Int rank = v.director->getNextVeterancyPrice( dearest );
		if( u.veterancy < LEVEL_HEROIC && rank > 0 && rank * least <= v.spend )
		{
			const Int args[ 3 ] = { u.grid, u.col, u.row };
			order( v, LANEWAR_ORDER_VETERANCY, args, 3, "ranks up", obj->getTemplate()->getName().str(), rank );
			return TRUE;
		}
		return FALSE;
	}

	/// the plot's next step the plan wants on a track, bought when the seat can pay; *saving when it cannot yet
	Bool buyTier( View &v, Int track, Bool *saving )
	{
		const Int level = 1 + ( track == LANEWAR_TRACK_INCOME ? v.director->getIncomeLevel( v.team, v.seat ) : v.director->getTechLevel( v.team, v.seat ) );
		if( !LaneWarBot_wantsTier( *v.plan, track, level, v.tick ) )
			return FALSE;
		const Int price = LaneWar_tierPrice( track, level );
		if( price > v.money )
		{
			*saving = TRUE;
			return FALSE;
		}
		const Int args[ 2 ] = { track, level };
		order( v, LANEWAR_ORDER_TIER, args, 2, track == LANEWAR_TRACK_INCOME ? "buys income level" : "buys tech tier",
					 level == 1 ? "1" : level == 2 ? "2" : level == 3 ? "3" : "4", price );
		return TRUE;
	}

	/// one decision of one seat: at most one purchase.  It sees the enemy plots as they stood before this
	/// frame's decisions, the first `seen` plot units, so no seat reads a purchase made beside its own.
	void step( LaneWarDirector *director, Player *player, Int team, Int seat, const LaneWarBotPlan &plan, UnsignedInt now, size_t seen )
	{
		View v;
		v.director = director;
		v.player = player;
		v.team = team;
		v.seat = seat;
		v.plan = &plan;
		v.now = now;
		v.tick = director->getTickCount();
		v.money = (Int)player->getMoney()->countMoney();

		Bool occupied[ LANEWAR_SEATS ];
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
			occupied[ s ] = director->getSeatPlayer( team, s ) >= 0;
		v.waveSoon = LaneWar_seatForTick( occupied, v.tick ) == seat
								 && director->getNextTickFrame() <= now + (UnsignedInt)( WAVE_SOON_SECONDS * LOGICFRAMES_PER_SECOND );
		// the reserve is money a weaker seat leaves idle, its own wave or not
		v.spend = v.money - plan.reserve;

		// its own plot and the enemy's, in purchase order
		v.units = 0;
		v.value = v.frontValue = 0.0f;
		v.slotsUsed = 0;
		v.slotsTotal = director->getGridCols( team, seat ) * director->getGridRows( team, seat );
		v.enemyValue = 0.0f;
		const std::vector<LaneWarDirector::PlotUnit> &units = director->getPlotUnits();
		for( size_t i = 0; i < units.size(); ++i )
		{
			const LaneWarDirector::PlotUnit &u = units[ i ];
			const Object *obj = TheGameLogic->findObjectByID( u.id );
			if( obj == NULL )
				continue;
			if( u.team == team && u.seat == seat )
			{
				++v.units;
				v.value += (Real)u.cost;
				if( roleOf( obj->getTemplate() ) == LANEWAR_BOT_FRONT )
					v.frontValue += (Real)u.cost;
				if( u.grid == LANEWAR_GRID_GROUND )
					v.slotsUsed += u.size * u.size;
			}
			else if( u.team != team && i < seen )
			{
				Enemy e;
				e.thing = obj->getTemplate();
				e.cost = (Real)max( u.cost, 1 );
				v.enemies.push_back( e );
				v.enemyValue += e.cost;
				for( size_t m = 0; m < u.passengers.size(); ++m )
				{
					e.thing = u.passengers[ m ].thing;
					e.cost = (Real)max( u.passengers[ m ].cost, 1 );
					v.enemies.push_back( e );
					v.enemyValue += e.cost;
				}
			}
		}

		// the economy and the tech the plan wants by now come first, once the plot has something on it
		Bool saving = FALSE;
		if( v.units >= FEW_UNITS )
		{
			if( buyTier( v, LANEWAR_TRACK_INCOME, &saving ) )
				return;
			if( !saving && buyTier( v, LANEWAR_TRACK_TECH, &saving ) )
				return;
			if( !saving && (Real)v.tick >= plan.heroTick && plan.heroTick < NO_TICK && !director->isHeroBought( team, seat ) )
			{
				Bool waiting;
				if( buyHero( v, &waiting ) )
					return;
				saving = waiting;
			}
			// held through its own waves too: the plot goes out whole whether or not this money is spent
			if( saving && plan.saves )
				return;
		}

		// a jet when the money would buy two, then the ground
		Int wanted = 0;
		if( plan.jetTick < NO_TICK && (Real)v.tick >= plan.jetTick && v.units >= FEW_UNITS )
		{
			LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
			const Int listed = LaneWar_roster( player, roster, LANEWAR_ROSTER_MAX );
			Int cheapest = 0;
			for( Int i = 0; i < listed; ++i )
				if( !roster[ i ].hero && LaneWarDirector::isJet( roster[ i ].thing ) && roster[ i ].tier <= director->getTechLevel( team, seat ) )
				{
					const Int cost = roster[ i ].thing->calcCostToBuild( player );
					cheapest = cheapest == 0 ? cost : min( cheapest, cost );
				}
			if( cheapest > 0 && v.spend >= 2 * cheapest && buyUnit( v, TRUE, &wanted ) )
				return;
		}
		if( buyUnit( v, FALSE, &wanted ) )
			return;

		// nothing left to place, or the money piling up past what it waits for
		const Bool plotFull = wanted == 0 && v.units >= FEW_UNITS;
		if( plan.extrasTick < NO_TICK && (Real)v.tick >= plan.extrasTick && ( plotFull || v.spend >= 2 * wanted ) )
			buyExtra( v, plotFull );
	}
}

//-------------------------------------------------------------------------------------------------
void LaneWarBot_planAt( Real rating, LaneWarBotPlan *plan )
{
	plan->rating = rating;
	const Real seconds = planNumber( STEP_SECONDS, rating );
	plan->stepFrames = (UnsignedInt)floor( seconds * LOGICFRAMES_PER_SECOND + 0.5f );
	plan->reserve = (Int)floor( planNumber( RESERVE, rating ) + 0.5f );
	for( Int k = 0; k < LANEWAR_INCOME_TIERS; ++k )
		plan->incomeTick[ k ] = planNumber( INCOME_TICK[ k ], rating );
	for( Int k = 0; k < LANEWAR_TECH_TIERS - 1; ++k )
		plan->techTick[ k ] = planNumber( TECH_TICK[ k ], rating );
	plan->heroTick = planNumber( HERO_TICK, rating );
	plan->jetTick = planNumber( JET_TICK, rating );
	plan->extrasTick = planNumber( EXTRAS_TICK, rating );
	plan->counterWeight = planNumber( COUNTER, rating );
	plan->jitter = planNumber( JITTER, rating );
	plan->saves = plan->counterWeight >= SAVES_FROM_COUNTER;
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarBot_wantsTier( const LaneWarBotPlan &plan, Int track, Int level, UnsignedInt tick )
{
	Real from = NO_TICK;
	if( track == LANEWAR_TRACK_INCOME && level >= 1 && level <= LANEWAR_INCOME_TIERS )
		from = plan.incomeTick[ level - 1 ];
	else if( track == LANEWAR_TRACK_TECH && level >= 2 && level <= LANEWAR_TECH_TIERS )
		from = plan.techTick[ level - 2 ];
	return from < NO_TICK && (Real)tick >= from;
}

//-------------------------------------------------------------------------------------------------
Int LaneWarBot_rowOrder( Int role, Int rows, Int size, Int *out, Int maxItems )
{
	const Int last = rows - size;		// the furthest back a block's front row can stand
	if( last < 0 )
		return 0;
	const Int start = role == LANEWAR_BOT_FRONT ? 0 : role == LANEWAR_BOT_BACK ? last : min( last, rows / 3 );
	Int count = 0;
	// from the role's row outwards, towards the back first for the middle
	for( Int d = 0; count < maxItems && d <= last; ++d )
	{
		const Int back = start + d, front = start - d;
		if( role == LANEWAR_BOT_BACK )
		{
			if( front >= 0 )
				out[ count++ ] = front;
			continue;
		}
		if( back <= last )
			out[ count++ ] = back;
		if( role == LANEWAR_BOT_MIDDLE && d > 0 && front >= 0 && count < maxItems )
			out[ count++ ] = front;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
Int LaneWarBot_columnOrder( Int cols, Int size, Int *out, Int maxItems )
{
	const Int last = cols - size;
	if( last < 0 )
		return 0;
	const Int centre = last / 2;
	Int count = 0;
	for( Int d = 0; count < maxItems && d <= last; ++d )
	{
		if( centre - d >= 0 )
			out[ count++ ] = centre - d;
		if( d > 0 && centre + d <= last && count < maxItems )
			out[ count++ ] = centre + d;
	}
	return count;
}

//-------------------------------------------------------------------------------------------------
void LaneWarBot_update( LaneWarDirector *director, UnsignedInt now )
{
	/* Every seat of one plan decides on the same frames, all of them against the plots as this frame
		 found them.  The seats used to be staggered by seven frames a player index, so in a 1v1 player 2
		 always decided seven frames after player 3 and had the last look at his plot before each wave.
		 With the same army on both sides at the same rating, the seat that looked last won 36 of 48 as
		 China and 12 of 48 as GLA; deciding together, 9 of 24 and 12 of 24. */
	const size_t seen = director->getPlotUnits().size();
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			const Int index = director->getSeatPlayer( t, s );
			Player *player = index >= 0 ? ThePlayerList->getNthPlayer( index ) : NULL;
			const AIPlayer *ai = player != NULL && player->getPlayerType() == PLAYER_COMPUTER ? player->getAIPlayer() : NULL;
			if( ai == NULL )
				continue;
			LaneWarBotPlan plan;
			LaneWarBot_planAt( ai->getAIRating(), &plan );
			if( now % max( plan.stepFrames, 1u ) == 0 )
				step( director, player, t, s, plan, now, seen );
		}
}
