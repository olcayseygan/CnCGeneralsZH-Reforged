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
	/** The plan's numbers at the lobby's four levels, Easy, Medium, Hard and Brutal at Elo 800, 1300,
		  1800 and 2200, and how far a rating outside them may carry each.  Between two levels a number runs
		  on a straight line, and past the end ones on the line of the nearest pair, so above Brutal the
		  economy and the tech keep coming earlier and the decisions faster.  Brutal's anchor is its own
		  rating, so Vicious, Savage and Insane (2600, 3400, 4200) sit 2, 4 and 6 of its steps past Hard,
		  where they sat on the scale before Elo, and play the plan they played. */
	const Real NO_TICK = (Real)LANEWAR_BOT_NEVER_TICK;
	const Real ANCHOR_RATING[ 4 ] = { AI_RATING_EASY, AI_RATING_MEDIUM, AI_RATING_HARD, AI_RATING_BRUTAL };

	struct PlanRow
	{
		Real at[ 4 ];
		Real lo, hi;
	};

	const PlanRow STEP_SECONDS	= { { 6.0f, 3.0f, 2.0f, 1.0f },			0.5f, 12.0f };
	/* An income level pays 100 a tick for its 1000, back in 10 ticks, and every seat starts on 2000 and
		 is paid 400 a tick; bot matches on 30 second ticks lasted about 18 ticks, and 45 second ticks are
		 not measured yet.
		 A unit bought goes out in every wave after it, so money held is the worst buy there is: the
		 stronger plans differ in how little they leave idle, how well they pick, and how early they tidy
		 and sell.  Income does not pay back in time. */
	const PlanRow RESERVE				= { { 600.0f, 300.0f, 100.0f, 0.0f },	0.0f, 800.0f };
	const PlanRow INCOME_TICK[ LANEWAR_INCOME_TIERS ] =
	{
		{ { NO_TICK, NO_TICK, NO_TICK, NO_TICK },	0.0f, NO_TICK },
		{ { NO_TICK, NO_TICK, NO_TICK, NO_TICK },	0.0f, NO_TICK },
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
	const PlanRow MOVE_TICK			= { { NO_TICK, 6.0f, 3.0f, 1.0f },		0.0f, NO_TICK };
	const PlanRow SELL_TICK			= { { NO_TICK, 16.0f, 10.0f, 6.0f },	0.0f, NO_TICK };
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
	const Real PLOT_BEFORE_TIER = 2.0f;	///< a tier or the hero waits until the plot is worth this many times its price
	const Real SELL_BELOW = 0.35f;			///< a unit trading worse than this against the enemy plots is dead weight
	const Real SELL_MARGIN = 0.2f;			///< ... sold only for a roster unit trading this much better
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

	/// artillery, the long guns and the jets (which never march) at the back, the other vehicles in front, infantry and helicopters between
	Int roleOf( const ThingTemplate *tmpl )
	{
		if( LaneWarDirector::isJet( tmpl ) || reachOf( tmpl ) >= BACK_RANGE )
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

	/** The first free block for the template on the seat's plot.  Its role's band comes first, filled a
		  column at a time from the middle outwards so the band fills in depth before it widens, then the
		  other rows a row at a time.  `onlyBand` looks in the band alone; `skip` is a unit being moved,
		  whose own slots count as free and whose price is already paid. */
	Bool findSlot( const View &v, const ThingTemplate *tmpl, Int role, Int *col, Int *row, Bool onlyBand = FALSE, ObjectID skip = INVALID_ID )
	{
		const Int grid = LANEWAR_GRID_GROUND;
		const Int size = LaneWarDirector::sizeFor( tmpl );
		Int rows[ SLOTS_MAX ], cols[ SLOTS_MAX ], inBand;
		const Int rowCount = LaneWarBot_rowOrder( role, v.director->getGridRows( v.team, v.seat, grid ), size, rows, SLOTS_MAX, &inBand );
		const Int colCount = LaneWarBot_columnOrder( v.director->getGridCols( v.team, v.seat, grid ), size, cols, SLOTS_MAX );
		const Int tries = ( onlyBand ? inBand : rowCount ) * colCount;
		for( Int k = 0; k < tries; ++k )
		{
			const Bool banded = k < inBand * colCount;
			const Int r = banded ? k % inBand : inBand + ( k - inBand * colCount ) / colCount;
			const Int c = banded ? k / inBand : ( k - inBand * colCount ) % colCount;
			const char *why = v.director->whyNotPlace( v.player, tmpl, cols[ c ], rows[ r ], skip );
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

	/// whether a unit of the role and size standing with its front row at `row` stands in its role's band
	Bool inRoleBand( const View &v, Int grid, Int role, Int size, Int row )
	{
		Int rows[ SLOTS_MAX ], inBand;
		LaneWarBot_rowOrder( role, v.director->getGridRows( v.team, v.seat, grid ),
												 size, rows, SLOTS_MAX, &inBand );
		for( Int k = 0; k < inBand; ++k )
			if( rows[ k ] == row )
				return TRUE;
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

	/// whether one of the template's weapon slots ends the unit when it hits
	Bool isSuicide( const ThingTemplate *tmpl )
	{
		for( Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot )
			if( AIAbility_slotIsSuicide( tmpl, (WeaponSlotType)slot ) )
				return TRUE;
		return FALSE;
	}

	/** counterOf, held to an even trade for a unit that ends itself to hit: the matchup prices one blast
		  as a kill, and in the lane most of them die before they reach anything */
	Real tradeOf( const View &v, const ThingTemplate *tmpl, Real cost, Bool suicide )
	{
		const Real counter = counterOf( v, tmpl, cost );
		return suicide ? min( counter, 0.5f ) : counter;
	}

	void order( View &v, Int kind, const Int *args, Int count, const char *what, const char *name, Int price )
	{
		// where it goes: a placement's column and row, a move's new ones, a sale's old ones
		const Int at = kind == LANEWAR_ORDER_PLACE ? 1 : kind == LANEWAR_ORDER_MOVE ? 3 : kind == LANEWAR_ORDER_SELL ? 1 : -1;
		DEBUG_LOG(( "LANEWAR BOT: frame %u tick %u player %d rating %.2f %s '%s' for %d at %d %d with %d, %d units on the plot worth %.0f, %.0f of it in front\n",
								v.now, v.tick, (Int)v.player->getPlayerIndex(), v.plan->rating, what, name, price, at >= 0 ? args[ at ] : -1,
								at >= 0 ? args[ at + 1 ] : -1, v.money, v.units, v.value, v.frontValue ));
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
			Bool suicide;
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
			p.suicide = isSuicide( tmpl );
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
				score += plan.counterWeight * tradeOf( v, p.thing, (Real)p.cost, p.suicide );
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

		/* A plan that saves waits for a pick within the next tick's pay, until its own wave is about to go.
			 A fixed 300 was a tick's pay at 30 second ticks; at 45 a seat that spent down every tick held
			 under 700 and never again reached a 900 tank, so plots turned to infantry for good: seed 5's
			 Hard 3v3 bought 40 to 48 Rangers a seat after the opening's Crusader and touched no fortress
			 in 90 minutes. */
		const Int reach = (Int)v.director->getIncome( v.team, v.seat );
		const Bool waits = plan.saves && !v.waveSoon && v.units >= FEW_UNITS && best >= 0 && picks[ best ].cost <= v.spend + reach;
		const Int chosen = waits ? best : bestAffordable;
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
			if( v.value < PLOT_BEFORE_TIER * (Real)cost )
				return FALSE;
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

	/** One ground unit standing outside its role's band, the first in purchase order, moved into the band
		  where a slot has come free there (a sale, or the band was full when it was bought).  Moves cost
		  nothing. */
	Bool moveIntoBand( View &v )
	{
		const std::vector<LaneWarDirector::PlotUnit> &units = v.director->getPlotUnits();
		for( size_t i = 0; i < units.size(); ++i )
		{
			const LaneWarDirector::PlotUnit &u = units[ i ];
			const Object *obj = u.team == v.team && u.seat == v.seat && u.grid == LANEWAR_GRID_GROUND ? TheGameLogic->findObjectByID( u.id ) : NULL;
			if( obj == NULL )
				continue;
			const Int role = roleOf( obj->getTemplate() );
			Int col, row;
			if( inRoleBand( v, u.grid, role, u.size, u.row ) || !findSlot( v, obj->getTemplate(), role, &col, &row, TRUE, u.id ) )
				continue;
			const Int args[ 5 ] = { u.grid, u.col, u.row, col, row };
			order( v, LANEWAR_ORDER_MOVE, args, 5, "moves", obj->getTemplate()->getName().str(), 0 );
			return TRUE;
		}
		return FALSE;
	}

	/** The ground unit that trades worst against the enemy plots, sold when the roster has one that trades
		  clearly better and the seat can pay for it with the refund.  The hero is never for sale. */
	Bool sellDeadWeight( View &v )
	{
		if( v.enemyValue <= 0.0f )
			return FALSE;
		const std::vector<LaneWarDirector::PlotUnit> &units = v.director->getPlotUnits();
		Int worst = -1;
		Real worstTrade = 0.0f;
		for( size_t i = 0; i < units.size(); ++i )
		{
			const LaneWarDirector::PlotUnit &u = units[ i ];
			const Object *obj = u.team == v.team && u.seat == v.seat && u.grid == LANEWAR_GRID_GROUND ? TheGameLogic->findObjectByID( u.id ) : NULL;
			if( obj == NULL || LaneWar_isHero( v.player, obj->getTemplate() ) || LaneWarDirector::isJet( obj->getTemplate() ) )
				continue;
			const Real trade = tradeOf( v, obj->getTemplate(), (Real)max( u.cost, 1 ), isSuicide( obj->getTemplate() ) );
			if( worst < 0 || trade < worstTrade )
			{
				worst = (Int)i;
				worstTrade = trade;
			}
		}
		if( worst < 0 || worstTrade >= SELL_BELOW )
			return FALSE;
		const LaneWarDirector::PlotUnit &u = units[ worst ];
		const Int refund = LaneWar_refund( u.cost, u.sent );

		LaneWarRosterItem roster[ LANEWAR_ROSTER_MAX ];
		const Int listed = LaneWar_roster( v.player, roster, LANEWAR_ROSTER_MAX );
		Real bestTrade = 0.0f;
		for( Int i = 0; i < listed; ++i )
		{
			const ThingTemplate *tmpl = roster[ i ].thing;
			const Int cost = tmpl->calcCostToBuild( v.player );
			if( roster[ i ].hero || LaneWarDirector::isJet( tmpl ) || roster[ i ].tier > v.director->getTechLevel( v.team, v.seat )
					|| cost <= 0 || cost > v.spend + refund )
				continue;
			bestTrade = max( bestTrade, tradeOf( v, tmpl, (Real)cost, isSuicide( tmpl ) ) );
		}
		if( bestTrade < worstTrade + SELL_MARGIN )
			return FALSE;
		const Object *obj = TheGameLogic->findObjectByID( u.id );
		const Int args[ 3 ] = { u.grid, u.col, u.row };
		order( v, LANEWAR_ORDER_SELL, args, 3, "sells", obj->getTemplate()->getName().str(), refund );
		return TRUE;
	}

	/// the plot's next step the plan wants on a track, bought when the seat can pay; *saving when it cannot yet
	Bool buyTier( View &v, Int track, Bool *saving )
	{
		const Int level = 1 + ( track == LANEWAR_TRACK_INCOME ? v.director->getIncomeLevel( v.team, v.seat ) : v.director->getTechLevel( v.team, v.seat ) );
		if( !LaneWarBot_wantsTier( *v.plan, track, level, v.tick ) )
			return FALSE;
		const Int price = LaneWar_tierPrice( track, level );
		if( v.value < PLOT_BEFORE_TIER * (Real)price )
			return FALSE;
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

	/// an enemy plot unit or passenger as every seat saw it before this frame's decisions
	struct Sighting
	{
		Int team;
		Enemy enemy;
	};

	/// what is on the plots now, units and passengers, in purchase order
	void sightPlots( const LaneWarDirector *director, std::vector<Sighting> *out )
	{
		out->clear();
		const std::vector<LaneWarDirector::PlotUnit> &units = director->getPlotUnits();
		for( size_t i = 0; i < units.size(); ++i )
		{
			const LaneWarDirector::PlotUnit &u = units[ i ];
			const Object *obj = TheGameLogic->findObjectByID( u.id );
			if( obj == NULL )
				continue;
			Sighting s;
			s.team = u.team;
			s.enemy.thing = obj->getTemplate();
			s.enemy.cost = (Real)max( u.cost, 1 );
			out->push_back( s );
			for( size_t m = 0; m < u.passengers.size(); ++m )
			{
				s.enemy.thing = u.passengers[ m ].thing;
				s.enemy.cost = (Real)max( u.passengers[ m ].cost, 1 );
				out->push_back( s );
			}
		}
	}

	/// one decision of one seat: at most one order.  It sees the enemy plots as they stood before this
	/// frame's decisions, `sighted`, so no seat reads a purchase or a sale made beside its own.
	void step( LaneWarDirector *director, Player *player, Int team, Int seat, const LaneWarBotPlan &plan, UnsignedInt now,
						 const std::vector<Sighting> &sighted )
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
								 && director->getNextTickFrame() <= now + LaneWar_gameFrames( WAVE_SOON_SECONDS );
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
		}
		for( size_t i = 0; i < sighted.size(); ++i )
			if( sighted[ i ].team != team )
			{
				v.enemies.push_back( sighted[ i ].enemy );
				v.enemyValue += sighted[ i ].enemy.cost;
			}

		// a unit the plot's own rows have room for again goes back to them first, for nothing
		if( plan.moveTick < NO_TICK && (Real)v.tick >= plan.moveTick && moveIntoBand( v ) )
			return;

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

		// late on, a unit that trades badly against what the enemy fields makes room for one that does not
		if( plan.sellTick < NO_TICK && (Real)v.tick >= plan.sellTick && sellDeadWeight( v ) )
			return;

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
	// wall seconds at the native 1.5x as game time, as the director's clock runs (LaneWar_gameFrames); the
	// ratio is exactly 1 at the native speed, so the product there is what it always was
	const Real native = (Real)NATIVE_GAME_SPEED_PERCENT / (Real)GAME_SPEED_PERCENT;
	plan->stepFrames = (UnsignedInt)floor( seconds * LOGICFRAMES_PER_SECOND * native + 0.5f );
	plan->reserve = (Int)floor( planNumber( RESERVE, rating ) + 0.5f );
	for( Int k = 0; k < LANEWAR_INCOME_TIERS; ++k )
		plan->incomeTick[ k ] = planNumber( INCOME_TICK[ k ], rating );
	for( Int k = 0; k < LANEWAR_TECH_TIERS - 1; ++k )
		plan->techTick[ k ] = planNumber( TECH_TICK[ k ], rating );
	plan->heroTick = planNumber( HERO_TICK, rating );
	plan->jetTick = planNumber( JET_TICK, rating );
	plan->extrasTick = planNumber( EXTRAS_TICK, rating );
	plan->moveTick = planNumber( MOVE_TICK, rating );
	plan->sellTick = planNumber( SELL_TICK, rating );
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
Int LaneWarBot_rowOrder( Int role, Int rows, Int size, Int *out, Int maxItems, Int *inBand )
{
	*inBand = 0;
	const Int last = rows - size;		// the furthest back a block's front row can stand
	if( last < 0 )
		return 0;
	const Int third = ( rows + 1 ) / 3;
	const Int bandFirst = role == LANEWAR_BOT_FRONT ? 0 : role == LANEWAR_BOT_BACK ? rows - third : third;
	const Int bandEnd = role == LANEWAR_BOT_FRONT ? third : role == LANEWAR_BOT_BACK ? rows : rows - third;
	// a block too deep for its band still starts in it, as far back as it fits
	const Int lo = min( bandFirst, last ), hi = max( lo + 1, min( bandEnd, last + 1 ) );

	Int count = 0;
	for( Int k = 0; k < hi - lo && count < maxItems; ++k )
		out[ count++ ] = role == LANEWAR_BOT_BACK ? hi - 1 - k : lo + k;
	*inBand = count;
	// then the rest, nearest the band first, the back side first on a tie
	for( Int d = 1; count < maxItems && d <= last; ++d )
	{
		if( hi - 1 + d <= last )
			out[ count++ ] = hi - 1 + d;
		if( lo - d >= 0 && count < maxItems )
			out[ count++ ] = lo - d;
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
	std::vector<Sighting> sighted;
	Bool sightedYet = FALSE;
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
			if( now % max( plan.stepFrames, 1u ) != 0 )
				continue;
			if( !sightedYet )
				sightPlots( director, &sighted );
			sightedYet = TRUE;
			step( director, player, t, s, plan, now, sighted );
		}
}
