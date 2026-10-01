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

// FILE: ScenarioDrill.h /////////////////////////////////////////////////////////////////////////
//
// -scenario <name>: play a match out of a text file instead of out of a person or an AI.
//
// A measurement needs the same units doing the same thing twice, and -autoskirmish cannot give
// that: it hands every slot PLAYERTEMPLATE_RANDOM and the faction falls out of the seed, so
// "enough angry mobs to see them cost anything" is a thing you wait for rather than a thing you
// ask for.  A scenario file names the unit, the place, the count and the frame, so the same
// command line puts the same army on the same ground in two different builds.
//
// Orders go out as an AIGroup built inside the logic frame, ordered with
// CMD_FROM_SCRIPT - and not through TheMessageStream.  Two reasons.  A GameMessage is stamped with
// the local player and the fake index a harness would write does not survive a network game
// (NetCommandMsg re-stamps it with the sender's).  Worse for a measurement, messages propagate on
// the render pass, so which logic frame an order lands on depends on how fast the machine drew -
// and two builds that draw at different speeds would then be playing different scenarios.  A tick
// keyed to the logic frame has neither problem.
//
// The bill for that: these orders exist nowhere in the command
// stream, so a run driven by a scenario cannot be replayed.  Repeatability comes from the file
// plus -seed instead, which is what an A/B needs anyway.
//
// The shift verbs are the exception to the script source.  They hand the order to the seat's order
// queue exactly as a message with MSG_QUEUE_NEXT_ORDER in front of it would arrive, so the queue
// dispatches them as the player's own, which is the only way to exercise it without a mouse.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SCENARIODRILL_H_
#define __SCENARIODRILL_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

class Object;
class Player;

// ------------------------------------------------------------------------------------------------
/** What one scenario line asks for. */
// ------------------------------------------------------------------------------------------------
//
// A <position> is two numbers, x and y, or one token naming a start position: start<N> is where
// seat N began (-scenario pins seat i to start i), and start<N>:<dx>:<dy> stands that far off it.
// The token form is what lets one file play on a generated map, whose starts move with the seed.
//
enum ScenarioActionType
{
	SCENARIO_ACTION_SPAWN = 0,		///< spawn <slot> <template> <count> <position> [spacing]
	SCENARIO_ACTION_MOVE,					///< move <slot> <selector> <position>
	SCENARIO_ACTION_ATTACKMOVE,		///< attackmove <slot> <selector> <position>
	SCENARIO_ACTION_ATTACK,				///< attack <slot> <selector> <targetSlot> <targetSelector>
	SCENARIO_ACTION_STOP,					///< stop <slot> <selector>
	SCENARIO_ACTION_ARRIVE,				///< arrive <slot> <selector> <position> [radius]
	SCENARIO_ACTION_PARTICLES,		///< particles <slot> <systemTemplate> <count> <position> [spacing]; slot is read and ignored
	SCENARIO_ACTION_ENTER,				///< enter <slot> <selector> <targetSlot> <targetSelector>
	SCENARIO_ACTION_PLAYERMOVE,		///< playermove <slot> <selector> <position>; a right click, not a script order
	SCENARIO_ACTION_POWER,					///< power <slot> <building> <position> [powerName]; fire its special powers (or the one named) there now, charged or not
	SCENARIO_ACTION_PLAYERATTACKMOVE,	///< playerattackmove <slot> <selector> <position>; the player's attack move, not a script's
	SCENARIO_ACTION_PRODUCE,			///< produce <slot> <building> <template> <count>; queue that many in its first matching building
	SCENARIO_ACTION_TALLY,				///< tally <slot> <selector>; log how many are alive, their health and what they cost
	SCENARIO_ACTION_SHIFTMOVE,				///< shiftmove <slot> <selector> <position>; a shift right click, onto the units' order queue
	SCENARIO_ACTION_SHIFTATTACKMOVE,	///< shiftattackmove <slot> <selector> <position>; the same with attack move
	SCENARIO_ACTION_SHIFTATTACK,			///< shiftattack <slot> <selector> <targetSlot> <targetSelector>; the same with an attack on one unit
	SCENARIO_ACTION_SHIFTGUARD,				///< shiftguard <slot> <selector> <position>; the same with the guard key
	SCENARIO_ACTION_SHIFTPOWER,				///< shiftpower <slot> <selector> <targetSlot> <targetSelector> <power>; the same with a special power armed, on one object
	SCENARIO_ACTION_SHIFTUPGRADE,			///< shiftupgrade <slot> <selector> <upgrade>; shift on an object upgrade button, bought by every unit that matches
	SCENARIO_ACTION_DOCK,							///< dock <slot> <selector> <targetSlot> <targetSelector>; a right click on a supply point or a dock
	SCENARIO_ACTION_CONSTRUCT,				///< construct <slot> <template> <position>; the local player's placement click, whatever is selected, as a message that is recorded and crosses the network
	SCENARIO_ACTION_TELEPORT					///< teleport <slot> <selector> <position>; stop them and stand them there, an episode reset rather than an order
};

/// ScenarioAction::atStart when the position is plain numbers
enum { SCENARIO_NO_START = -1 };

// ------------------------------------------------------------------------------------------------
/** Why a line was not turned into an action.  A scenario that silently drops half its orders is
	  worse than one that refuses to load, because the run still produces numbers. */
// ------------------------------------------------------------------------------------------------
enum ScenarioParseResult
{
	SCENARIO_PARSE_OK = 0,
	SCENARIO_PARSE_BLANK,					///< a comment or an empty line: nothing to do, not an error
	SCENARIO_PARSE_BAD_FRAME,
	SCENARIO_PARSE_BAD_ACTION,
	SCENARIO_PARSE_BAD_SLOT,
	SCENARIO_PARSE_BAD_COUNT,
	SCENARIO_PARSE_MISSING_ARGS,
	SCENARIO_PARSE_BAD_POSITION
};

// ------------------------------------------------------------------------------------------------
/** One parsed line.  Which fields carry anything depends on the action - see the enum above. */
// ------------------------------------------------------------------------------------------------
struct ScenarioAction
{
	UnsignedInt frame;						///< the logic frame this fires on
	ScenarioActionType action;
	Int slot;											///< the seat, the same number -side takes; not an index into ThePlayerList
	AsciiString selector;					///< a template name, or "*" for everything that seat owns
	Coord2D at;										///< spawn, move, attackmove and arrive target, or the offset from atStart
	Int atStart;									///< the start position at is measured from, SCENARIO_NO_START for none
	Int count;										///< how many to spawn
	Real spacing;									///< how far apart to spawn them, in world units
	Real radius;									///< how close to the target counts as arrived
	Int targetSlot;								///< whose units to attack
	AsciiString targetSelector;		///< which of them
	AsciiString name;							///< shiftpower's special power, shiftupgrade's upgrade
};

/** Turn one line of a scenario file into an action.  Pure: no engine state is read, which is what
	  lets the parser be tested without booting a game. */
extern ScenarioParseResult ScenarioDrill_parseLine( const char *line, ScenarioAction *action );

/** The name of a parse result, for the log. */
extern const char *ScenarioDrill_parseResultName( ScenarioParseResult result );

/** Run whatever the file asks for on this logic frame.  Called from GameLogic::update. */
extern void ScenarioDrill_tick( void );

/** Carry out one action now.  The scenario file is one caller; the control server is the other, so
	  that a command typed down a socket means exactly what the same line means in a file.  Only safe
	  from inside a logic frame, and only while a match is running. */
extern Bool ScenarioDrill_execute( const ScenarioAction &action );

/** The player sitting in a seat, the same number -side takes, or NULL.  Read-only, so the control
	  server's units query can ask it from a render pass. */
extern Player *ScenarioDrill_findPlayerForSlot( Int slot );

/** Whether a selector (a template name, "*" or a prefix ending in "*") names this object. */
extern Bool ScenarioDrill_selectorMatches( const AsciiString &selector, const Object *obj );

/** One line for the end-of-run summary: how much of the file actually happened. */
extern const char *ScenarioDrill_report( void );

/** One HEADLESS ARRIVE line per arrive action: how many of the units it watched got within its
	  radius, and how many frames after the line fired the first and the last of them did.  The last
	  one's time is the choke probe's number (ROADMAP M0.3). */
extern void ScenarioDrill_logArrivals( void );

#endif // __SCENARIODRILL_H_
