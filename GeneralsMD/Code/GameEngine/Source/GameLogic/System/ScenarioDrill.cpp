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

// FILE: ScenarioDrill.cpp ///////////////////////////////////////////////////////////////////////
//
// -scenario <name>: play a match out of Run/Scenarios/<name>.txt.  See ScenarioDrill.h for why the
// orders go out through an AIGroup inside the logic frame rather than through the message stream.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/MessageStream.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScenarioDrill.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/ControlBar.h"	// COMMAND_FIRED_BY_SCRIPT, the flag every script-fired power carries
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"	// removeAllBibs, after the build check lights them
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Weapon.h"
#include "GameClient/ReachOverlay.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

// ------------------------------------------------------------------------------------------------
// the shape of a scenario line
// ------------------------------------------------------------------------------------------------

static const Int SCENARIO_MAX_TOKENS = 10;
static const char SCENARIO_COMMENT_CHAR = '#';
static const char *SCENARIO_SELECTOR_ALL = "*";

/// a target seat written "civilian": the map's own buildings, which no -side seat owns. The match
/// nearest the ordered group is taken, since a map has many of each (Fallen Empire's wall towers).
static const char *SCENARIO_CIVILIAN_TOKEN = "civilian";
static const Int SCENARIO_CIVILIAN_SLOT = -1;

/// every object a civilian-seat lookup has seen owned by the map (the neutral or the civilian
/// player): a garrisoned building changes hands to its occupants, so "owned by the map now" would
/// lose a wall tower the moment it fills
static std::set<ObjectID> theCivilianObjects;

/// how far apart spawned units stand, when the line does not say
static const Real SCENARIO_DEFAULT_SPACING = 30.0f;

/// "keep shooting until told otherwise" - the same count the mob's own self-tasking passes
static const Int SCENARIO_ATTACK_SHOTS = 999;

/// how close to its target a unit has to get before arrive counts it, when the line does not say
static const Real SCENARIO_DEFAULT_ARRIVE_RADIUS = 200.0f;

/// the one-token position, and what separates its offset
static const char *SCENARIO_START_PREFIX = "start";
static const char SCENARIO_START_OFFSET_SEPARATOR = ':';

// the fewest tokens each action can be written in, including the frame and the action name, with a
// position written as its one-token start<N> form
static const Int SCENARIO_TOKENS_STOP = 4;
static const Int SCENARIO_TOKENS_MOVE = 5;
static const Int SCENARIO_TOKENS_ATTACK = 6;
static const Int SCENARIO_TOKENS_SPAWN = 6;
static const Int SCENARIO_TOKENS_ARRIVE = 5;
static const Int SCENARIO_TOKENS_SHIFTPOWER = 7;
static const Int SCENARIO_TOKENS_SHIFTUPGRADE = 5;
static const Int SCENARIO_TOKENS_STANCE = 5;

// where the name sits in the lines that carry one
static const Int SCENARIO_SHIFTPOWER_NAME_TOKEN = 6;
static const Int SCENARIO_SHIFTUPGRADE_NAME_TOKEN = 4;
static const Int SCENARIO_STANCE_NAME_TOKEN = 4;

// where the position starts in each line that has one
static const Int SCENARIO_SPAWN_POSITION_TOKEN = 5;
static const Int SCENARIO_ORDER_POSITION_TOKEN = 4;

static const char *SCENARIO_DIRECTORY = "Scenarios\\";
static const char *SCENARIO_EXTENSION = ".txt";

static const Int SCENARIO_REPORT_LENGTH = 192;

// ------------------------------------------------------------------------------------------------
// parsing
// ------------------------------------------------------------------------------------------------

static Bool isScenarioSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/** Split a line into whitespace-separated tokens, stopping at a comment.  Returns how many it
	  found, which may be more than maxTokens - the caller checks the count it wanted. */
static Int tokenizeScenarioLine( const char *line, AsciiString *tokens, Int maxTokens )
{
	Int found = 0;
	const char *at = line;

	while (*at != 0)
	{
		while (*at != 0 && isScenarioSpace( *at ))
			++at;

		if (*at == 0 || *at == SCENARIO_COMMENT_CHAR)
			break;

		const char *start = at;
		while (*at != 0 && !isScenarioSpace( *at ) && *at != SCENARIO_COMMENT_CHAR)
			++at;

		if (found < maxTokens)
		{
			const Int length = (Int)(at - start);
			char word[ 128 ];
			const Int copied = (length < (Int)sizeof( word ) - 1) ? length : (Int)sizeof( word ) - 1;
			memcpy( word, start, copied );
			word[ copied ] = 0;
			tokens[ found ].set( word );
		}
		++found;
	}

	return found;
}

/** atoi cannot tell "0" from "banana", and a scenario that quietly reads a typo as frame 0 fires
	  its whole file on the first frame.  So spell the digits out. */
static Bool parseWholeNumber( const AsciiString &token, Int *value )
{
	const char *at = token.str();
	if (at == NULL || *at == 0)
		return FALSE;

	Int accumulated = 0;
	while (*at != 0)
	{
		if (*at < '0' || *at > '9')
			return FALSE;
		accumulated = accumulated * 10 + (*at - '0');
		++at;
	}

	*value = accumulated;
	return TRUE;
}

static Bool parseActionType( const AsciiString &token, ScenarioActionType *action )
{
	if (token == "spawn")
		*action = SCENARIO_ACTION_SPAWN;
	else if (token == "move")
		*action = SCENARIO_ACTION_MOVE;
	else if (token == "playermove")
		*action = SCENARIO_ACTION_PLAYERMOVE;
	else if (token == "playerattackmove")
		*action = SCENARIO_ACTION_PLAYERATTACKMOVE;
	else if (token == "attackmove")
		*action = SCENARIO_ACTION_ATTACKMOVE;
	else if (token == "attack")
		*action = SCENARIO_ACTION_ATTACK;
	else if (token == "enter")
		*action = SCENARIO_ACTION_ENTER;
	else if (token == "dock")
		*action = SCENARIO_ACTION_DOCK;
	else if (token == "stop")
		*action = SCENARIO_ACTION_STOP;
	else if (token == "clickstop")
		*action = SCENARIO_ACTION_CLICKSTOP;
	else if (token == "arrive")
		*action = SCENARIO_ACTION_ARRIVE;
	else if (token == "particles")
		*action = SCENARIO_ACTION_PARTICLES;
	else if (token == "power")
		*action = SCENARIO_ACTION_POWER;
	else if (token == "produce")
		*action = SCENARIO_ACTION_PRODUCE;
	else if (token == "tally")
		*action = SCENARIO_ACTION_TALLY;
	else if (token == "purchase")
		*action = SCENARIO_ACTION_PURCHASE;
	else if (token == "resume")
		*action = SCENARIO_ACTION_RESUME;
	else if (token == "shiftmove")
		*action = SCENARIO_ACTION_SHIFTMOVE;
	else if (token == "shiftattackmove")
		*action = SCENARIO_ACTION_SHIFTATTACKMOVE;
	else if (token == "shiftattack")
		*action = SCENARIO_ACTION_SHIFTATTACK;
	else if (token == "shiftguard")
		*action = SCENARIO_ACTION_SHIFTGUARD;
	else if (token == "shiftpower")
		*action = SCENARIO_ACTION_SHIFTPOWER;
	else if (token == "shiftupgrade")
		*action = SCENARIO_ACTION_SHIFTUPGRADE;
	else if (token == "construct")
		*action = SCENARIO_ACTION_CONSTRUCT;
	else if (token == "stance")
		*action = SCENARIO_ACTION_STANCE;
	else if (token == "forceattack")
		*action = SCENARIO_ACTION_FORCEATTACK;
	else if (token == "clickenter")
		*action = SCENARIO_ACTION_CLICKENTER;
	else if (token == "weaponat")
		*action = SCENARIO_ACTION_WEAPONAT;
	else if (token == "forceground")
		*action = SCENARIO_ACTION_FORCEGROUND;
	else if (token == "reaim")
		*action = SCENARIO_ACTION_REAIM;
	else if (token == "respond")
		*action = SCENARIO_ACTION_RESPOND;
	else if (token == "guard")
		*action = SCENARIO_ACTION_GUARD;
	else if (token == "repeat")
		*action = SCENARIO_ACTION_REPEAT;
	else if (token == "givemoney")
		*action = SCENARIO_ACTION_GIVEMONEY;
	else if (token == "rank")
		*action = SCENARIO_ACTION_RANK;
	else if (token == "reachproof")
		*action = SCENARIO_ACTION_REACHPROOF;
	else if (token == "topup")
		*action = SCENARIO_ACTION_TOPUP;
	else
		return FALSE;

	return TRUE;
}

/** A position at tokens[*index]: two numbers, or start<N>, or start<N>:<dx>:<dy>.  Moves *index past
	  whatever it used.  The start number is spelt out digit by digit for parseWholeNumber's reason, and
	  an offset that is not two numbers is refused rather than read as zero. */
static ScenarioParseResult parseScenarioPosition( const AsciiString *tokens, Int count, Int *index,
																									ScenarioAction *action )
{
	if (*index >= count)
		return SCENARIO_PARSE_MISSING_ARGS;

	const char *word = tokens[ *index ].str();
	const Int prefixLength = (Int)strlen( SCENARIO_START_PREFIX );
	if (strncmp( word, SCENARIO_START_PREFIX, prefixLength ) != 0)
	{
		if (*index + 1 >= count)
			return SCENARIO_PARSE_MISSING_ARGS;

		action->atStart = SCENARIO_NO_START;
		action->at.x = (Real)atof( word );
		action->at.y = (Real)atof( tokens[ *index + 1 ].str() );
		*index += 2;
		return SCENARIO_PARSE_OK;
	}

	const char *at = word + prefixLength;
	Int start = 0;
	Int digits = 0;
	while (*at >= '0' && *at <= '9')
	{
		start = start * 10 + (*at - '0');
		++at;
		++digits;
	}
	if (digits == 0)
		return SCENARIO_PARSE_BAD_POSITION;

	action->atStart = start;
	action->at.x = 0.0f;
	action->at.y = 0.0f;

	if (*at == SCENARIO_START_OFFSET_SEPARATOR)
	{
		const char *xText = at + 1;
		char *end = NULL;
		action->at.x = (Real)strtod( xText, &end );
		if (end == xText || *end != SCENARIO_START_OFFSET_SEPARATOR)
			return SCENARIO_PARSE_BAD_POSITION;

		const char *yText = end + 1;
		action->at.y = (Real)strtod( yText, &end );
		if (end == yText || *end != 0)
			return SCENARIO_PARSE_BAD_POSITION;
	}
	else if (*at != 0)
	{
		return SCENARIO_PARSE_BAD_POSITION;
	}

	*index += 1;
	return SCENARIO_PARSE_OK;
}

static Int tokensNeededFor( ScenarioActionType action )
{
	switch (action)
	{
		case SCENARIO_ACTION_SPAWN:				return SCENARIO_TOKENS_SPAWN;
		case SCENARIO_ACTION_PARTICLES:		return SCENARIO_TOKENS_SPAWN;
		case SCENARIO_ACTION_MOVE:				return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_PLAYERMOVE:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_POWER:				return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_PLAYERATTACKMOVE:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_ATTACKMOVE:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_SHIFTMOVE:		return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_SHIFTATTACKMOVE:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_SHIFTGUARD:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_CONSTRUCT:		return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_WEAPONAT:		return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_FORCEGROUND:	return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_REAIM:				return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_RESPOND:			return SCENARIO_TOKENS_MOVE;
		case SCENARIO_ACTION_STANCE:			return SCENARIO_TOKENS_STANCE;
		case SCENARIO_ACTION_SHIFTATTACK:	return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_FORCEATTACK:	return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_CLICKENTER:	return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_SHIFTPOWER:	return SCENARIO_TOKENS_SHIFTPOWER;
		case SCENARIO_ACTION_SHIFTUPGRADE:	return SCENARIO_TOKENS_SHIFTUPGRADE;
		case SCENARIO_ACTION_ATTACK:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_ENTER:				return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_RESUME:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_DOCK:				return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_GUARD:				return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_PRODUCE:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_REPEAT:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_GIVEMONEY:		return SCENARIO_TOKENS_STANCE;
		case SCENARIO_ACTION_RANK:				return SCENARIO_TOKENS_STANCE;
		case SCENARIO_ACTION_REACHPROOF:	return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_STOP:				return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_CLICKSTOP:		return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_TALLY:				return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_PURCHASE:		return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_ARRIVE:			return SCENARIO_TOKENS_ARRIVE;
		case SCENARIO_ACTION_TOPUP:				return SCENARIO_TOKENS_MOVE;
	}
	return SCENARIO_TOKENS_STOP;
}

ScenarioParseResult ScenarioDrill_parseLine( const char *line, ScenarioAction *action )
{
	AsciiString tokens[ SCENARIO_MAX_TOKENS ];
	const Int count = tokenizeScenarioLine( line, tokens, SCENARIO_MAX_TOKENS );
	if (count == 0)
		return SCENARIO_PARSE_BLANK;

	// a bare frame number with nothing after it is a line somebody started and did not finish
	if (count < SCENARIO_TOKENS_STOP)
		return SCENARIO_PARSE_MISSING_ARGS;

	Int frame = 0;
	if (!parseWholeNumber( tokens[ 0 ], &frame ))
		return SCENARIO_PARSE_BAD_FRAME;

	ScenarioActionType actionType;
	if (!parseActionType( tokens[ 1 ], &actionType ))
		return SCENARIO_PARSE_BAD_ACTION;

	Int slot = 0;
	if (!parseWholeNumber( tokens[ 2 ], &slot ))
		return SCENARIO_PARSE_BAD_SLOT;

	if (count < tokensNeededFor( actionType ))
		return SCENARIO_PARSE_MISSING_ARGS;

	action->frame = (UnsignedInt)frame;
	action->action = actionType;
	action->slot = slot;
	action->selector = tokens[ 3 ];
	action->at.x = 0.0f;
	action->at.y = 0.0f;
	action->atStart = SCENARIO_NO_START;
	action->count = 1;
	action->spacing = SCENARIO_DEFAULT_SPACING;
	action->radius = SCENARIO_DEFAULT_ARRIVE_RADIUS;
	action->targetSlot = 0;
	action->targetSelector.clear();
	action->name.clear();

	switch (actionType)
	{
		case SCENARIO_ACTION_TOPUP:
			if (!parseWholeNumber( tokens[ 4 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			break;

		case SCENARIO_ACTION_SPAWN:
		case SCENARIO_ACTION_PARTICLES:
		{
			if (!parseWholeNumber( tokens[ 4 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			Int next = SCENARIO_SPAWN_POSITION_TOKEN;
			const ScenarioParseResult position = parseScenarioPosition( tokens, count, &next, action );
			if (position != SCENARIO_PARSE_OK)
				return position;
			if (count > next)
				action->spacing = (Real)atof( tokens[ next ].str() );
			break;
		}

		case SCENARIO_ACTION_MOVE:
		case SCENARIO_ACTION_PLAYERMOVE:
		case SCENARIO_ACTION_PLAYERATTACKMOVE:
		case SCENARIO_ACTION_ATTACKMOVE:
		case SCENARIO_ACTION_ARRIVE:
		case SCENARIO_ACTION_POWER:
		case SCENARIO_ACTION_SHIFTMOVE:
		case SCENARIO_ACTION_SHIFTATTACKMOVE:
		case SCENARIO_ACTION_SHIFTGUARD:
		case SCENARIO_ACTION_CONSTRUCT:
		case SCENARIO_ACTION_WEAPONAT:
		case SCENARIO_ACTION_FORCEGROUND:
		case SCENARIO_ACTION_REAIM:
		case SCENARIO_ACTION_RESPOND:
		{
			Int next = SCENARIO_ORDER_POSITION_TOKEN;
			const ScenarioParseResult position = parseScenarioPosition( tokens, count, &next, action );
			if (position != SCENARIO_PARSE_OK)
				return position;
			if (actionType == SCENARIO_ACTION_WEAPONAT)
			{
				action->name = (count > next) ? tokens[ next ] : AsciiString( "tertiary" );
				if (action->name != "primary" && action->name != "secondary" && action->name != "tertiary")
					return SCENARIO_PARSE_BAD_ACTION;
			}
			if (actionType == SCENARIO_ACTION_ARRIVE && count > next)
				action->radius = (Real)atof( tokens[ next ].str() );
			if (actionType == SCENARIO_ACTION_SHIFTGUARD)
				action->radius = (count > next) ? (Real)atof( tokens[ next ].str() ) : 0.0f;	// the wheeled radius, none by default
			if (actionType == SCENARIO_ACTION_POWER && count > next)
				action->targetSelector = tokens[ next ];	// fire only the power of this name
			break;
		}

		case SCENARIO_ACTION_ATTACK:
		case SCENARIO_ACTION_ENTER:
		case SCENARIO_ACTION_RESUME:
		case SCENARIO_ACTION_DOCK:
		case SCENARIO_ACTION_GUARD:
		case SCENARIO_ACTION_SHIFTATTACK:
		case SCENARIO_ACTION_FORCEATTACK:
		case SCENARIO_ACTION_CLICKENTER:
		case SCENARIO_ACTION_SHIFTPOWER:
		{
			if (tokens[ 4 ] == SCENARIO_CIVILIAN_TOKEN)
				action->targetSlot = SCENARIO_CIVILIAN_SLOT;
			else if (!parseWholeNumber( tokens[ 4 ], &action->targetSlot ))
				return SCENARIO_PARSE_BAD_SLOT;
			action->targetSelector = tokens[ 5 ];
			if (actionType == SCENARIO_ACTION_SHIFTPOWER)
				action->name = tokens[ SCENARIO_SHIFTPOWER_NAME_TOKEN ];
			break;
		}

		case SCENARIO_ACTION_REACHPROOF:
		{
			if (!parseWholeNumber( tokens[ 4 ], &action->targetSlot ))
				return SCENARIO_PARSE_BAD_SLOT;
			action->targetSelector = tokens[ 5 ];
			action->count = 400;		// sample points of each kind
			if (count > 6 && (!parseWholeNumber( tokens[ 6 ], &action->count ) || action->count < 1))
				return SCENARIO_PARSE_BAD_COUNT;
			break;
		}

		case SCENARIO_ACTION_SHIFTUPGRADE:
			action->name = tokens[ SCENARIO_SHIFTUPGRADE_NAME_TOKEN ];
			break;

		case SCENARIO_ACTION_STANCE:
			action->name = tokens[ SCENARIO_STANCE_NAME_TOKEN ];
			if (action->name != "aggressive" && action->name != "defensive")
				return SCENARIO_PARSE_BAD_ACTION;
			break;

		case SCENARIO_ACTION_RANK:
			action->name = tokens[ SCENARIO_STANCE_NAME_TOKEN ];
			// xp N: N same-class kills of experience; grade N: the ladder's grade N
			if (action->name == "xp" || action->name == "grade")
			{
				if (count <= SCENARIO_STANCE_NAME_TOKEN + 1)
					return SCENARIO_PARSE_MISSING_ARGS;
				if (!parseWholeNumber( tokens[ SCENARIO_STANCE_NAME_TOKEN + 1 ], &action->count ))
					return SCENARIO_PARSE_BAD_COUNT;
				break;
			}
			if (action->name != "veteran" && action->name != "elite" && action->name != "heroic")
				return SCENARIO_PARSE_BAD_ACTION;
			break;

		case SCENARIO_ACTION_PRODUCE:
		{
			action->targetSelector = tokens[ 4 ];	// what to make
			if (!parseWholeNumber( tokens[ 5 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			break;
		}

		case SCENARIO_ACTION_REPEAT:
			action->targetSelector = tokens[ 4 ];	// the unit
			action->name = tokens[ 5 ];
			if (action->name != "on" && action->name != "off")
				return SCENARIO_PARSE_BAD_ACTION;
			break;

		case SCENARIO_ACTION_GIVEMONEY:
			if (!parseWholeNumber( tokens[ 3 ], &action->targetSlot ))
				return SCENARIO_PARSE_BAD_SLOT;
			if (!parseWholeNumber( tokens[ 4 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			break;

		case SCENARIO_ACTION_TALLY:
			action->count = -1;		// nothing wanted, unless the file names a count
			if (count > SCENARIO_TOKENS_STOP && !parseWholeNumber( tokens[ SCENARIO_TOKENS_STOP ], &action->count ))
				return SCENARIO_PARSE_BAD_COUNT;
			break;

		case SCENARIO_ACTION_STOP:
		case SCENARIO_ACTION_CLICKSTOP:
		case SCENARIO_ACTION_PURCHASE:
			break;
	}

	return SCENARIO_PARSE_OK;
}

const char *ScenarioDrill_parseResultName( ScenarioParseResult result )
{
	switch (result)
	{
		case SCENARIO_PARSE_OK:						return "ok";
		case SCENARIO_PARSE_BLANK:				return "blank";
		case SCENARIO_PARSE_BAD_FRAME:		return "frame is not a whole number";
		case SCENARIO_PARSE_BAD_ACTION:		return "no such action";
		case SCENARIO_PARSE_BAD_SLOT:		return "slot is not a whole number";
		case SCENARIO_PARSE_BAD_COUNT:		return "count is not a whole number above zero";
		case SCENARIO_PARSE_MISSING_ARGS:	return "not enough arguments for this action";
		case SCENARIO_PARSE_BAD_POSITION:	return "position is not x y, start<N> or start<N>:<dx>:<dy>";
	}
	return "unknown";
}

// ------------------------------------------------------------------------------------------------
// the loaded file, and what happened to it
// ------------------------------------------------------------------------------------------------

static std::vector<ScenarioAction> theScenarioActions;
static std::vector<ScenarioAction> theScenarioTopups;
static Int theScenarioCursor = 0;
static Bool theScenarioLoaded = FALSE;
static Int theScenarioActionsRun = 0;
static Int theScenarioActionsFailed = 0;
static Int theScenarioUnitsSpawned = 0;
static UnsignedInt theScenarioLastFrame = 0;
static char theScenarioReport[ SCENARIO_REPORT_LENGTH ];

static const UnsignedInt SCENARIO_NOT_ARRIVED = 0xffffffff;

/** One arrive line: the units that matched when it fired, and the frame each first stood within the
	  radius of the target.  The units are fixed when the line fires, so one that is built afterwards
	  cannot finish the run for a unit still stuck in the choke. */
struct ScenarioArrival
{
	Int slot;
	AsciiString selector;
	Coord3D goal;
	Real radius;
	UnsignedInt fromFrame;
	std::vector<ObjectID> ids;
	std::vector<UnsignedInt> arrivedOn;
	std::vector<Coord3D> startedAt;			///< where each stood when the line fired
	std::vector<UnsignedInt> departedOn;	///< the frame each first stood SCENARIO_DEPARTED_DISTANCE from there
	std::vector<Coord3D> lastSeenAt;		///< where each stood on the frame before
	std::vector<Int> stillFor;					///< frames each has stood still since it last moved
	// Stops on the way: a unit that has left and not arrived standing still for SCENARIO_STALL_FRAMES,
	// counted once per stop by what it was doing at that moment.
	Int stallsIdle;						///< gave the order up
	Int stallsHeldBack;				///< its path request held back a second for coming too soon after the last
	Int stallsWaitingForPath;	///< still waiting in the queue for its route
	Int stallsBlocked;				///< blocked by something in front
	Int stallsOther;					///< has a route, is not blocked, and is not moving
	Int longestStall;
	ObjectID longestStallID;
};

/// how long a unit on its way has to stand still before it counts as having stopped
static const Int SCENARIO_STALL_FRAMES = LOGICFRAMES_PER_SECOND;
/// less than this from one frame to the next is standing still
static const Real SCENARIO_STILL_DISTANCE = ConvertRetailPerFrameRate( 0.05f );

/// how far a unit has to have gone from where it stood before it counts as having obeyed the order
static const Real SCENARIO_DEPARTED_DISTANCE = 20.0f;

static std::vector<ScenarioArrival> theScenarioArrivals;

/** One respond line: how quickly each unit it matched took up the order given on that frame.  Aim is the
	  first frame its gun (the turret the current weapon is on, or the hull) points within
	  SCENARIO_RESPOND_AIM of the position; shot the first frame it fires with the gun pointed there; closer
	  the first frame it stands SCENARIO_DEPARTED_DISTANCE nearer the position than it started.  Read-only. */
struct ScenarioResponse
{
	Int slot;
	AsciiString selector;
	Coord3D goal;
	UnsignedInt fromFrame;
	std::vector<ObjectID> ids;
	std::vector<Real> startDist;
	std::vector<UnsignedInt> aimOn;
	std::vector<UnsignedInt> shotOn;
	std::vector<UnsignedInt> closerOn;
};
static const Real SCENARIO_RESPOND_AIM = 0.26f;	// 15 degrees
static std::vector<ScenarioResponse> theScenarioResponses;

static void resetScenario( void )
{
	theScenarioArrivals.clear();
	theScenarioResponses.clear();
	theScenarioActions.clear();
	theScenarioTopups.clear();
	theScenarioCursor = 0;
	theScenarioLoaded = FALSE;
	theScenarioActionsRun = 0;
	theScenarioActionsFailed = 0;
	theScenarioUnitsSpawned = 0;
	theCivilianObjects.clear();
}

/** Sorted by frame, and stable so two actions on the same frame keep the order the file wrote
	  them in.  An unstable sort would make the run depend on the standard library's mood, which is
	  the one thing a repeatable measurement cannot have. */
static Bool scenarioActionIsEarlier( const ScenarioAction &left, const ScenarioAction &right )
{
	return left.frame < right.frame;
}

static void loadScenario( void )
{
	theScenarioLoaded = TRUE;

	AsciiString path;
	path.set( SCENARIO_DIRECTORY );
	path.concat( TheGlobalData->m_scenarioFile );
	path.concat( SCENARIO_EXTENSION );

	/* Read it as bytes, not as text.  readEntireAndClose sizes its buffer from size() and then reads
		 that many bytes; in text mode the CRLF translation hands back fewer than it asked for and the
		 tail of the buffer is whatever was in the heap.  So take the file as it lies and let the
		 tokenizer treat a carriage return as the whitespace it is. */
	File *file = TheFileSystem->openFile( path.str(), File::READ );
	if (file == NULL)
	{
		DEBUG_LOG(("SCENARIO: cannot open '%s'\n", path.str()));
		return;
	}

	const Int fileSize = file->size();
	char *contents = file->readEntireAndClose();		// this also closes and frees the File
	if (contents == NULL)
	{
		DEBUG_LOG(("SCENARIO: '%s' read as nothing\n", path.str()));
		return;
	}

	Int lineNumber = 0;
	Int at = 0;
	while (at < fileSize)
	{
		Int end = at;
		while (end < fileSize && contents[ end ] != '\n')
			++end;

		const Int length = end - at;
		char line[ 512 ];
		const Int copied = (length < (Int)sizeof( line ) - 1) ? length : (Int)sizeof( line ) - 1;
		memcpy( line, contents + at, copied );
		line[ copied ] = 0;
		++lineNumber;

		ScenarioAction action;
		const ScenarioParseResult result = ScenarioDrill_parseLine( line, &action );
		if (result == SCENARIO_PARSE_OK)
		{
			theScenarioActions.push_back( action );
		}
		else if (result != SCENARIO_PARSE_BLANK)
		{
			DEBUG_LOG(("SCENARIO: %s line %d: %s\n",
								 path.str(), lineNumber, ScenarioDrill_parseResultName( result )));
			++theScenarioActionsFailed;
		}

		at = end + 1;
	}

	delete [] contents;

	std::stable_sort( theScenarioActions.begin(), theScenarioActions.end(), scenarioActionIsEarlier );

	DEBUG_LOG(("SCENARIO: loaded '%s', %d actions, %d lines refused\n",
						 path.str(), (Int)theScenarioActions.size(), theScenarioActionsFailed));
}

// ------------------------------------------------------------------------------------------------
// doing what the file asked for
// ------------------------------------------------------------------------------------------------

/** The file names a seat, the same number -side takes.  ThePlayerList is not that list: it opens
	  with a neutral player and a civilian one, so slot 0 was player 2 in the first two-player run and
	  would be something else in another shape of game.  assignSlotIndices recorded the real mapping
	  when the game started, so ask it rather than carrying an offset around. */
static Player *findPlayerForSlot( Int slot )
{
	if (slot == SCENARIO_CIVILIAN_SLOT)
		return ThePlayerList->findPlayerWithNameKey( NAMEKEY( "PlyrCivilian" ) );

	for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
	{
		if (ThePlayerList->getSlotIndex( i ) == slot)
			return ThePlayerList->getNthPlayer( i );
	}
	return NULL;
}

static Bool selectorMatches( const AsciiString &selector, const Object *obj )
{
	if (selector == SCENARIO_SELECTOR_ALL)
		return TRUE;

	const ThingTemplate *tmpl = obj->getTemplate();
	if (tmpl == NULL)
		return FALSE;

	// "GLAVehicleTechnical*": a Technical spawns as one of its chassis templates, never under its own name
	const Int length = selector.getLength();
	if (length > 1 && selector.getCharAt( length - 1 ) == '*')
		return strncmp( tmpl->getName().str(), selector.str(), length - 1 ) == 0;

	return tmpl->getName() == selector;
}

/** Whether this player owns the object, it matches the selector and it can be given an order at all. */
static Bool isOrderableMatch( const Player *player, const AsciiString &selector, Object *obj )
{
	return obj->getControllingPlayer() == player && !obj->isEffectivelyDead()
			&& obj->getAIUpdateInterface() != NULL && selectorMatches( selector, obj );
}

/** Everything this player owns that matches the selector and can be given an order at all. */
static Int gatherIntoGroup( Player *player, const AsciiString &selector, AIGroup *group )
{
	Int taken = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (!isOrderableMatch( player, selector, obj ))
			continue;

		group->add( obj );
		++taken;
	}
	return taken;
}

/** Where the action's position is on this map: its own numbers, or a start position plus the offset
	  it gave.  -scenario pins seat i to start i, and start waypoints are numbered from 1. */
static Bool resolveScenarioPosition( const ScenarioAction &action, Coord3D *pos )
{
	pos->x = action.at.x;
	pos->y = action.at.y;

	if (action.atStart != SCENARIO_NO_START)
	{
		AsciiString waypointName;
		waypointName.format( "Player_%d_Start", action.atStart + 1 );
		const Waypoint *start = TheTerrainLogic->getWaypointByName( waypointName );
		if (start == NULL)
		{
			DEBUG_LOG(("SCENARIO: frame %d: this map has no %s\n", action.frame, waypointName.str()));
			return FALSE;
		}
		pos->x += start->getLocation()->x;
		pos->y += start->getLocation()->y;
	}

	pos->z = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
	return TRUE;
}

static Object *findFirstMatching( Player *player, const AsciiString &selector )
{
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() != player || obj->isEffectivelyDead())
			continue;
		if (selectorMatches( selector, obj ))
			return obj;
	}
	return NULL;
}

/** Of the objects the map owns now or owned at an earlier lookup, the living match nearest the
	  point, whoever holds it now.  A skirmish has no PlyrCivilian seat everywhere, and the map's
	  buildings can sit with the neutral player instead, so both count. */
static Object *findNearestCivilian( const AsciiString &selector, const Coord3D *point )
{
	const Player *neutral = ThePlayerList->getNeutralPlayer();
	const Player *civilian = findPlayerForSlot( SCENARIO_CIVILIAN_SLOT );
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		const Player *owner = obj->getControllingPlayer();
		if (owner != NULL && (owner == neutral || owner == civilian))
			theCivilianObjects.insert( obj->getID() );
	}

	Object *nearest = NULL;
	Real best = 0.0f;
	for( std::set<ObjectID>::const_iterator it = theCivilianObjects.begin(); it != theCivilianObjects.end(); ++it )
	{
		Object *obj = TheGameLogic->findObjectByID( *it );
		if (obj == NULL || obj->isEffectivelyDead() || !selectorMatches( selector, obj ))
			continue;
		const Real distance = ThePartitionManager->getDistanceSquared( obj, point, FROM_CENTER_2D );
		if (nearest == NULL || distance < best)
		{
			nearest = obj;
			best = distance;
		}
	}
	return nearest;
}

/** The recipe the debug spawn path uses (GameLogic.cpp's unitTimings): a template's onCreate ran in
	  the constructor, but a unit that was never built still has to be told it is finished, or half
	  its modules never start.  The team has to be activated too, or it counts as an empty team.
	  A unit that moves goes down only on a cell its own locomotors could enter, the test the
	  pathfinder puts every destination to: a tank set on a cliff or in the sea is taken away again
	  rather than left to drive out.  Aircraft are not asked: anything on a JetAIUpdate, the Comanche
	  included, is made on its ground-only taxiing set, which would refuse it on every cliff. */
static Object *spawnOne( const ScenarioAction &action, const ThingTemplate *tmpl, Team *team, const Coord3D *pos )
{
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if (obj == NULL)
		return NULL;

	obj->setOrientation( 0 );
	obj->setPosition( pos );

	AIUpdateInterface *ai = obj->getAI();
	if (ai != NULL && ai->getLocomotorSet().getValidSurfaces() != 0 && !obj->isKindOf( KINDOF_AIRCRAFT )
			&& !TheAI->pathfinder()->validMovementPosition( obj->getCrusherLevel() > 0, obj->getLayer(), ai->getLocomotorSet(), pos ))
	{
		static const char *const cellNames[] = { "CLEAR", "WATER", "CLIFF", "RUBBLE", "OBSTACLE",
			"BRIDGE_IMPASSABLE", "IMPASSABLE" };
		// a point off the map has no cell at all
		const PathfindCell *cell = TheAI->pathfinder()->getCell( obj->getLayer(), pos );
		const Int type = cell != NULL ? cell->getType() : -1;
		DEBUG_LOG(("SCENARIO: frame %d spawn slot %d '%s' refused at (%.0f,%.0f): cell type %d (%s), no unit of it could stand there\n",
							 action.frame, action.slot, action.selector.str(), pos->x, pos->y, type,
							 cell != NULL ? cellNames[type] : "OFF_MAP"));
		TheGameLogic->destroyObject( obj );
		return NULL;
	}

	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		CreateModuleInterface *create = (*m)->getCreate();
		if (create == NULL)
			continue;
		create->onBuildComplete();
	}

	// a capture refreshes the bar the same way; without it a spawned superweapon waits for a click
	TheControlBar->markUIDirty();

	team->setActive();
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
	return obj;
}

static Coord3D mapCentre( void )
{
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );
	Coord3D centre;
	centre.x = ( extent.lo.x + extent.hi.x ) * 0.5f;
	centre.y = ( extent.lo.y + extent.hi.y ) * 0.5f;
	centre.z = TheTerrainLogic->getGroundHeight( centre.x, centre.y );
	return centre;
}

/** The frame-time gate's crowd.  Money, the team count and a bigger map all stop near 1500 objects,
	  because population caps and the AI's spending stop there, so this is what gets a match to its
	  target: one unit a frame at most, so the top-up is no burst of its own in the tail it measures,
	  the file's lines taking turns on the frame number so every seat gets its share.  The count and
	  the frame are logic state, so the same seed draws the same crowd.  It starts on the road from the
	  seat's start towards the centre, on the first clear ground from 40% of the way out so it clears
	  the base, and the small offset keeps a jam of them from standing on one point. */
static void runTopups( UnsignedInt now )
{
	if (theScenarioTopups.empty())
		return;
	const ScenarioAction &line = theScenarioTopups[ now % theScenarioTopups.size() ];
	if (TheGameLogic->getObjectCount() >= (UnsignedInt)line.count)
		return;

	AsciiString waypointName;
	waypointName.format( "Player_%d_Start", line.slot + 1 );
	const Waypoint *start = TheTerrainLogic->getWaypointByName( waypointName );
	if (start == NULL)
		return;

	const Coord3D centre = mapCentre();
	Coord3D pos;
	for( Real along = 0.4f; along < 1.0f; along += 0.1f )
	{
		pos.x = start->getLocation()->x + ( centre.x - start->getLocation()->x ) * along + (Real)( now % 7 ) * 30.0f - 90.0f;
		pos.y = start->getLocation()->y + ( centre.y - start->getLocation()->y ) * along + (Real)( now % 11 ) * 30.0f - 150.0f;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, &pos );
		if (cell != NULL && cell->getType() == PathfindCell::CELL_CLEAR)
			break;
	}

	Object *obj = spawnOne( line, TheThingFactory->findTemplate( line.selector, FALSE ), findPlayerForSlot( line.slot )->getDefaultTeam(), &pos );
	if (obj == NULL)
		return;
	++theScenarioUnitsSpawned;
	obj->getAI()->aiAttackMoveToPosition( &centre, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
}

static Bool executeSpawn( const ScenarioAction &action, Player *player, const Coord3D &centre )
{
	const ThingTemplate *tmpl = TheThingFactory->findTemplate( action.selector, FALSE );
	if (tmpl == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d spawn: no template named '%s'\n",
							 action.frame, action.selector.str()));
		return FALSE;
	}

	Team *team = player->getDefaultTeam();

	// a square-ish block centred on the point the file named, so the author names where the army is
	// rather than where its top left corner is
	Int columns = 1;
	while (columns * columns < action.count)
		++columns;
	const Int rows = (action.count + columns - 1) / columns;
	const Real halfWidth = (columns - 1) * action.spacing * 0.5f;
	const Real halfHeight = (rows - 1) * action.spacing * 0.5f;

	Int made = 0;
	for( Int i = 0; i < action.count; ++i )
	{
		Coord3D pos;
		pos.x = centre.x + (i % columns) * action.spacing - halfWidth;
		pos.y = centre.y + (i / columns) * action.spacing - halfHeight;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

		// a structure goes only where a player could put it: on flat buildable ground, clear of other
		// buildings.  The check is logic state only; the bibs it lights are client feedback, so off again.
		// A new object reaches the partition cells only on the partition's next update, so that runs
		// first, the way the starting units are placed, or a block of structures stacks on itself
		if (tmpl->isKindOf( KINDOF_STRUCTURE ))
		{
			ThePartitionManager->update();
			const LegalBuildCode code = TheBuildAssistant->isLocationLegalToBuild( &pos, tmpl, 0.0f,
																					BuildAssistant::TERRAIN_RESTRICTIONS | BuildAssistant::NO_OBJECT_OVERLAP,
																					NULL, player );
			TheTerrainVisual->removeAllBibs();
			if (code != LBC_OK)
			{
				static const char *const codeNames[] = { "OK", "RESTRICTED_TERRAIN", "NOT_FLAT_ENOUGH",
					"OBJECTS_IN_THE_WAY", "NO_CLEAR_PATH", "SHROUD", "TOO_CLOSE_TO_SUPPLIES", "GENERIC_FAILURE",
					"TOO_CLOSE_TO_ENEMY", "TOO_MANY_DERRICK_DEFENSES" };
				DEBUG_LOG(("SCENARIO: frame %d spawn slot %d '%s' refused at (%.0f,%.0f): LBC_%s, no structure may be built there\n",
									 action.frame, action.slot, action.selector.str(), pos.x, pos.y, codeNames[code]));
				continue;
			}
		}

		if (spawnOne( action, tmpl, team, &pos ))
			++made;
	}

	theScenarioUnitsSpawned += made;
	DEBUG_LOG(("SCENARIO: frame %d spawn slot %d '%s' %d of %d at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(),
						 made, action.count, centre.x, centre.y));

	return made > 0;
}

/** Particle systems standing on their own, in the same square block spawn lays units out in.  No
	  unit carries an effect that holds more than a few hundred particles, so a scene with a hundred
	  thousand of them cannot be got out of an army; it is asked for here instead.  The systems are
	  client state and draw only on the client's random stream, so they change nothing a CRC sees. */
static Bool executeParticles( const ScenarioAction &action, const Coord3D &centre )
{
	const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate( action.selector );
	if (tmpl == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d particles: no particle system named '%s'\n",
							 action.frame, action.selector.str()));
		return FALSE;
	}

	Int columns = 1;
	while (columns * columns < action.count)
		++columns;
	const Int rows = (action.count + columns - 1) / columns;
	const Real halfWidth = (columns - 1) * action.spacing * 0.5f;
	const Real halfHeight = (rows - 1) * action.spacing * 0.5f;

	Int made = 0;
	for( Int i = 0; i < action.count; ++i )
	{
		ParticleSystem *system = TheParticleSystemManager->createParticleSystem( tmpl );
		if (system == NULL)
			continue;

		Coord3D pos;
		pos.x = centre.x + (i % columns) * action.spacing - halfWidth;
		pos.y = centre.y + (i / columns) * action.spacing - halfHeight;
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
		system->setPosition( &pos );
		++made;
	}

	DEBUG_LOG(("SCENARIO: frame %d particles '%s' %d of %d at (%.0f,%.0f)\n",
						 action.frame, action.selector.str(), made, action.count, centre.x, centre.y));
	return made > 0;
}

/** Watch every unit of this seat that matches the selector as they stand now, and note the frame
	  each one first comes within the radius of the target.  The watch starts on the line's own frame,
	  so a file puts it on the frame of the order it is timing. */
static Bool executeArrive( const ScenarioAction &action, Player *player, const Coord3D &goal )
{
	ScenarioArrival arrival;
	arrival.slot = action.slot;
	arrival.selector = action.selector;
	arrival.goal = goal;
	arrival.radius = action.radius;
	arrival.fromFrame = TheGameLogic->getFrame();

	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (!isOrderableMatch( player, action.selector, obj ))
			continue;

		arrival.ids.push_back( obj->getID() );
		arrival.arrivedOn.push_back( SCENARIO_NOT_ARRIVED );
		arrival.startedAt.push_back( *obj->getPosition() );
		arrival.departedOn.push_back( SCENARIO_NOT_ARRIVED );
		arrival.lastSeenAt.push_back( *obj->getPosition() );
		arrival.stillFor.push_back( 0 );
	}
	arrival.stallsIdle = 0;
	arrival.stallsHeldBack = 0;
	arrival.stallsWaitingForPath = 0;
	arrival.stallsBlocked = 0;
	arrival.stallsOther = 0;
	arrival.longestStall = 0;
	arrival.longestStallID = INVALID_ID;

	DEBUG_LOG(("SCENARIO: frame %d arrive slot %d '%s' x%d within %.0f of (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(), (Int)arrival.ids.size(),
						 action.radius, goal.x, goal.y));

	if (arrival.ids.empty())
		return FALSE;

	theScenarioArrivals.push_back( arrival );
	return TRUE;
}

static void updateArrivals( UnsignedInt now )
{
	for( std::vector<ScenarioArrival>::iterator it = theScenarioArrivals.begin();
			 it != theScenarioArrivals.end(); ++it )
	{
		const Real radiusSquared = it->radius * it->radius;
		for( size_t i = 0; i < it->ids.size(); ++i )
		{
			if (it->arrivedOn[ i ] != SCENARIO_NOT_ARRIVED)
				continue;

			Object *obj = TheGameLogic->findObjectByID( it->ids[ i ] );
			if (obj == NULL || obj->isEffectivelyDead())
				continue;

			if (it->departedOn[ i ] == SCENARIO_NOT_ARRIVED)
			{
				const Real mx = obj->getPosition()->x - it->startedAt[ i ].x;
				const Real my = obj->getPosition()->y - it->startedAt[ i ].y;
				if (mx * mx + my * my >= SCENARIO_DEPARTED_DISTANCE * SCENARIO_DEPARTED_DISTANCE)
					it->departedOn[ i ] = now;
			}
			else
			{
				const Real sx = obj->getPosition()->x - it->lastSeenAt[ i ].x;
				const Real sy = obj->getPosition()->y - it->lastSeenAt[ i ].y;
				if (sx * sx + sy * sy >= SCENARIO_STILL_DISTANCE * SCENARIO_STILL_DISTANCE)
					it->stillFor[ i ] = 0;
				else if (++it->stillFor[ i ] == SCENARIO_STALL_FRAMES)
				{
					const AIUpdateInterface *ai = obj->getAIUpdateInterface();
					if (ai->isIdle())
						++it->stallsIdle;
					else if (ai->isWaitingForPath() && ai->getQueueForPathFrame() != 0)
						++it->stallsHeldBack;
					else if (ai->isWaitingForPath())
						++it->stallsWaitingForPath;
					else if (ai->getNumFramesBlocked() > 0 || ai->isBlockedAndStuck())
						++it->stallsBlocked;
					else
						++it->stallsOther;
				}
				if (it->stillFor[ i ] > it->longestStall)
				{
					it->longestStall = it->stillFor[ i ];
					it->longestStallID = obj->getID();
				}
			}
			it->lastSeenAt[ i ] = *obj->getPosition();

			const Real dx = obj->getPosition()->x - it->goal.x;
			const Real dy = obj->getPosition()->y - it->goal.y;
			if (dx * dx + dy * dy <= radiusSquared)
				it->arrivedOn[ i ] = now;
		}
	}
}

static Real distanceTo2D( const Object *obj, const Coord3D &goal )
{
	const Real dx = obj->getPosition()->x - goal.x;
	const Real dy = obj->getPosition()->y - goal.y;
	return (Real)sqrt( dx * dx + dy * dy );
}

static Bool executeRespond( const ScenarioAction &action, Player *player, const Coord3D &goal )
{
	ScenarioResponse response;
	response.slot = action.slot;
	response.selector = action.selector;
	response.goal = goal;
	response.fromFrame = TheGameLogic->getFrame();
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (!isOrderableMatch( player, action.selector, obj ))
			continue;
		response.ids.push_back( obj->getID() );
		response.startDist.push_back( distanceTo2D( obj, goal ) );
		response.aimOn.push_back( SCENARIO_NOT_ARRIVED );
		response.shotOn.push_back( SCENARIO_NOT_ARRIVED );
		response.closerOn.push_back( SCENARIO_NOT_ARRIVED );
	}
	DEBUG_LOG(("SCENARIO: frame %d respond slot %d '%s' x%d at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(), (Int)response.ids.size(), goal.x, goal.y));
	if (response.ids.empty())
		return FALSE;
	theScenarioResponses.push_back( response );
	return TRUE;
}

static void updateResponses( UnsignedInt now )
{
	for( std::vector<ScenarioResponse>::iterator it = theScenarioResponses.begin(); it != theScenarioResponses.end(); ++it )
	{
		for( size_t i = 0; i < it->ids.size(); ++i )
		{
			Object *obj = TheGameLogic->findObjectByID( it->ids[ i ] );
			if (obj == NULL || obj->isEffectivelyDead() || now <= it->fromFrame)
				continue;

			const AIUpdateInterface *ai = obj->getAIUpdateInterface();
			Real gunAngle = 0.0f;
			Real pitch = 0.0f;
			const WhichTurretType tur = ai ? ai->getWhichTurretForCurWeapon() : TURRET_INVALID;
			if (tur != TURRET_INVALID)
				ai->getTurretRotAndPitch( tur, &gunAngle, &pitch );
			const Bool aimed = fabs( normalizeAngle( ThePartitionManager->getRelativeAngle2D( obj, &it->goal ) - gunAngle ) ) <= SCENARIO_RESPOND_AIM;
			if (aimed && it->aimOn[ i ] == SCENARIO_NOT_ARRIVED)
				it->aimOn[ i ] = now;

			const Weapon *weapon = obj->getCurrentWeapon();
			// the tick runs before the objects do, so last frame's shot is the newest one there is to see
			if (aimed && weapon && weapon->getLastShotFrame() + 1 >= now && weapon->getLastShotFrame() > it->fromFrame
					&& it->shotOn[ i ] == SCENARIO_NOT_ARRIVED)
				it->shotOn[ i ] = weapon->getLastShotFrame();

			if (it->closerOn[ i ] == SCENARIO_NOT_ARRIVED && distanceTo2D( obj, it->goal ) <= it->startDist[ i ] - SCENARIO_DEPARTED_DISTANCE)
				it->closerOn[ i ] = now;
		}
	}
}

/** Mean and worst frames from the line to each of the three marks, over the units that reached it. */
static void logResponses( void )
{
	for( std::vector<ScenarioResponse>::const_iterator it = theScenarioResponses.begin(); it != theScenarioResponses.end(); ++it )
	{
		const std::vector<UnsignedInt> *marks[ 3 ] = { &it->aimOn, &it->shotOn, &it->closerOn };
		Int count[ 3 ] = { 0, 0, 0 };
		Real sum[ 3 ] = { 0.0f, 0.0f, 0.0f };
		Int worst[ 3 ] = { 0, 0, 0 };
		for( Int m = 0; m < 3; ++m )
		{
			for( size_t i = 0; i < it->ids.size(); ++i )
			{
				const UnsignedInt on = (*marks[ m ])[ i ];
				if (on == SCENARIO_NOT_ARRIVED)
					continue;
				const Int frames = (Int)(on - it->fromFrame);
				++count[ m ];
				sum[ m ] += (Real)frames;
				worst[ m ] = (frames > worst[ m ]) ? frames : worst[ m ];
			}
		}
		DEBUG_LOG(("HEADLESS RESPOND: slot %d '%s' from frame %d at (%.0f,%.0f), %d units: aimed %d mean %.1f worst %d, fired %d mean %.1f worst %d, closer %d mean %.1f worst %d\n",
							 it->slot, it->selector.str(), it->fromFrame, it->goal.x, it->goal.y, (Int)it->ids.size(),
							 count[ 0 ], count[ 0 ] ? sum[ 0 ] / count[ 0 ] : 0.0f, worst[ 0 ],
							 count[ 1 ], count[ 1 ] ? sum[ 1 ] / count[ 1 ] : 0.0f, worst[ 1 ],
							 count[ 2 ], count[ 2 ] ? sum[ 2 ] / count[ 2 ] : 0.0f, worst[ 2 ]));
	}
}

/** Statues: a unit with an enemy it could shoot inside its own reach that has neither moved nor fired
	  for STATUE_FRAMES.  Counted once per stop, by what the unit was doing when it reached the mark,
	  and per second for as long as it keeps standing there.  Read-only, and only with a scenario. */
struct StatueTrack
{
	Coord3D lastSeenAt;
	UnsignedInt lastShot;
	Int stillFor;
	Bool counted;
	Bool seen;
};
static const Int STATUE_FRAMES = 3 * LOGICFRAMES_PER_SECOND;
static const Int STATUE_DETAIL_LINES = 40;
static std::map<ObjectID, StatueTrack> theStatueTracks;
static Int theStatueStops = 0;
static Int theStatueInfantryStops = 0;
static Int theStatueSeconds = 0;
static Int theStatueWaiting = 0;
static Int theStatueBlocked = 0;
static Int theStatueNoVictim = 0;
static Int theStatueVictimInRange = 0;
static Int theStatueVictimOutOfRange = 0;
static Int theStatueByState[ NUM_AI_STATES ];

static void updateStatues( UnsignedInt now )
{
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (!(obj->isKindOf( KINDOF_INFANTRY ) || obj->isKindOf( KINDOF_VEHICLE )) || obj->isKindOf( KINDOF_AIRCRAFT ))
			continue;
		if (obj->isEffectivelyDead() || obj->getContainedBy() != NULL || obj->isDisabled())
			continue;
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		const Weapon *weapon = obj->getCurrentWeapon();
		if (ai == NULL || weapon == NULL)
			continue;

		StatueTrack &track = theStatueTracks[ obj->getID() ];
		const Coord3D *pos = obj->getPosition();
		const Real sx = pos->x - track.lastSeenAt.x;
		const Real sy = pos->y - track.lastSeenAt.y;
		const Bool moved = sx * sx + sy * sy >= SCENARIO_STILL_DISTANCE * SCENARIO_STILL_DISTANCE;
		const Bool fired = obj->getLastShotFiredFrame() != track.lastShot;
		track.lastSeenAt = *pos;
		track.lastShot = obj->getLastShotFiredFrame();
		if (!track.seen || moved || fired)
		{
			track.seen = TRUE;
			track.stillFor = 0;
			track.counted = FALSE;
			continue;
		}
		++track.stillFor;
		if (track.stillFor < STATUE_FRAMES || (track.stillFor % LOGICFRAMES_PER_SECOND) != 0)
			continue;

		PartitionFilterRelationship enemies( obj, PartitionFilterRelationship::ALLOW_ENEMIES );
		PartitionFilterAlive alive;
		PartitionFilterPossibleToAttack attackable( ATTACK_NEW_TARGET, obj, CMD_FROM_AI );
		PartitionFilter *filters[] = { &enemies, &alive, &attackable, NULL };
		Real enemyDist = 0.0f;
		Object *enemy = ThePartitionManager->getClosestObject( obj, weapon->getAttackRange( obj ), FROM_BOUNDINGSPHERE_2D, filters, &enemyDist );
		if (enemy == NULL)
			continue;

		++theStatueSeconds;
		if (track.counted)
			continue;
		track.counted = TRUE;

		++theStatueStops;
		if (obj->isKindOf( KINDOF_INFANTRY ))
			++theStatueInfantryStops;
		const StateID state = ai->getCurrentStateID();
		if (state >= 0 && state < NUM_AI_STATES)
			++theStatueByState[ state ];
		Object *victim = ai->getCurrentVictim();
		if (ai->isWaitingForPath())
			++theStatueWaiting;
		else if (ai->getNumFramesBlocked() > 0 || ai->isBlockedAndStuck())
			++theStatueBlocked;
		else if (victim == NULL)
			++theStatueNoVictim;
		else if (weapon->isWithinAttackRange( obj, victim ))
			++theStatueVictimInRange;
		else
			++theStatueVictimOutOfRange;

		if (theStatueStops <= STATUE_DETAIL_LINES)
		{
			DEBUG_LOG(("STATUE: frame %d id %d '%s' state %d idle %d waiting %d blocked %d victim %d '%s' at %.0f health %.0f, enemy %d '%s' at %.0f, range %.0f, weapon '%s' status %d\n",
								 now, obj->getID(), obj->getTemplate()->getName().str(), (Int)state, ai->isIdle(),
								 ai->isWaitingForPath(), ai->getNumFramesBlocked(),
								 victim ? victim->getID() : 0, victim ? victim->getTemplate()->getName().str() : "",
								 victim ? sqrt( ThePartitionManager->getDistanceSquared( obj, victim, FROM_BOUNDINGSPHERE_2D ) ) : 0.0f,
								 victim ? victim->getBodyModule()->getHealth() : 0.0f,
								 enemy->getID(), enemy->getTemplate()->getName().str(), enemyDist,
								 weapon->getAttackRange( obj ), weapon->getName().str(), (Int)weapon->getStatus()));
		}
	}
}

static void logStatues( void )
{
	DEBUG_LOG(("HEADLESS STATUE: %d stops of %.0f s or more with an enemy in reach and no shot (%d infantry), %d unit-seconds - %d waiting for a path, %d blocked, %d no target, %d target in range, %d target out of range\n",
						 theStatueStops, (Real)STATUE_FRAMES / (Real)LOGICFRAMES_PER_SECOND, theStatueInfantryStops, theStatueSeconds,
						 theStatueWaiting, theStatueBlocked, theStatueNoVictim, theStatueVictimInRange, theStatueVictimOutOfRange));
	for( Int s = 0; s < NUM_AI_STATES; ++s )
		if (theStatueByState[ s ] > 0)
			DEBUG_LOG(("HEADLESS STATUE STATE: state %d, %d stops\n", s, theStatueByState[ s ]));
}

void ScenarioDrill_logArrivals( void )
{
	logStatues();
	logResponses();
	for( std::vector<ScenarioArrival>::const_iterator it = theScenarioArrivals.begin();
			 it != theScenarioArrivals.end(); ++it )
	{
		Int arrived = 0;
		Int lost = 0;
		UnsignedInt first = SCENARIO_NOT_ARRIVED;
		UnsignedInt last = 0;
		Real arrivalSum = 0.0f;
		for( size_t i = 0; i < it->ids.size(); ++i )
		{
			const UnsignedInt on = it->arrivedOn[ i ];
			if (on != SCENARIO_NOT_ARRIVED)
			{
				++arrived;
				arrivalSum += (Real)(on - it->fromFrame);
				first = (on < first) ? on : first;
				last = (on > last) ? on : last;
				continue;
			}

			Object *obj = TheGameLogic->findObjectByID( it->ids[ i ] );
			if (obj == NULL || obj->isEffectivelyDead())
			{
				++lost;
				continue;
			}
			const AIUpdateInterface *ai = obj->getAIUpdateInterface();
			DEBUG_LOG(("HEADLESS STRAGGLER: '%s' id %d at (%.0f,%.0f), state %d idle %d moving %d waiting %d path %d\n",
								 it->selector.str(), obj->getID(), obj->getPosition()->x, obj->getPosition()->y,
								 ai->getCurrentStateID(), ai->isIdle(), ai->isMoving(), ai->isWaitingForPath(), ai->getPath() != NULL));
		}

		const Int total = (Int)it->ids.size();
		const Int stillOut = total - arrived - lost;

		// how long the last of them stood before it obeyed; the order is only as quick as that
		Int departed = 0;
		UnsignedInt lastDeparture = 0;
		for( size_t i = 0; i < it->ids.size(); ++i )
		{
			if (it->departedOn[ i ] == SCENARIO_NOT_ARRIVED)
				continue;
			++departed;
			lastDeparture = (it->departedOn[ i ] > lastDeparture) ? it->departedOn[ i ] : lastDeparture;
		}
		DEBUG_LOG(("HEADLESS DEPART: slot %d '%s' from frame %d: %d of %d moved %.0f, the last after %d frames (%.1f s)\n",
							 it->slot, it->selector.str(), it->fromFrame, departed, total, SCENARIO_DEPARTED_DISTANCE,
							 departed ? (Int)(lastDeparture - it->fromFrame) : 0,
							 departed ? (Real)(lastDeparture - it->fromFrame) / (Real)LOGICFRAMES_PER_SECOND : 0.0f));

		DEBUG_LOG(("HEADLESS STALL: slot %d '%s': %d stops of %.0f s or more on the way - %d idle, %d held back, %d waiting for a path, %d blocked, %d with a path and no reason; longest %d frames (%.1f s), unit %d\n",
							 it->slot, it->selector.str(),
							 it->stallsIdle + it->stallsHeldBack + it->stallsWaitingForPath + it->stallsBlocked + it->stallsOther,
							 (Real)SCENARIO_STALL_FRAMES / (Real)LOGICFRAMES_PER_SECOND,
							 it->stallsIdle, it->stallsHeldBack, it->stallsWaitingForPath, it->stallsBlocked, it->stallsOther,
							 it->longestStall, (Real)it->longestStall / (Real)LOGICFRAMES_PER_SECOND,
							 (Int)it->longestStallID));

		if (arrived == 0)
		{
			DEBUG_LOG(("HEADLESS ARRIVE: slot %d '%s' from frame %d: none of %d within %.0f of (%.0f,%.0f), %d lost, %d still out\n",
								 it->slot, it->selector.str(), it->fromFrame, total, it->radius, it->goal.x, it->goal.y,
								 lost, stillOut));
			continue;
		}

		DEBUG_LOG(("HEADLESS ARRIVE: slot %d '%s' from frame %d: %d of %d within %.0f of (%.0f,%.0f), first after %d frames, mean %.0f, last after %d frames (%.1f s), %d lost, %d still out\n",
							 it->slot, it->selector.str(), it->fromFrame, arrived, total, it->radius, it->goal.x, it->goal.y,
							 first - it->fromFrame, arrivalSum / (Real)arrived, last - it->fromFrame,
							 (Real)(last - it->fromFrame) / (Real)LOGICFRAMES_PER_SECOND, lost, stillOut));
	}
}

/** Fire every special power the first matching building of this seat carries, at the position, the
	  way a map script's "fire special power at waypoint" does.  A script fire does not wait for the
	  countdown, so a superweapon spawned a second ago goes off; it still needs power and must not be
	  disabled, which is what the refusal line reports. */
static Bool executePower( const ScenarioAction &action, Player *player, const Coord3D &target )
{
	Object *obj = findFirstMatching( player, action.selector );
	if (obj == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d power: slot %d owns nothing matching '%s'\n",
							 action.frame, action.slot, action.selector.str()));
		return FALSE;
	}

	if (obj->isDisabled())
	{
		// the seat's whole grid, since one building that draws more than its plants make is the usual cause
		DEBUG_LOG(("SCENARIO: frame %d power: '%s' is disabled (underpowered %d: seat makes %d, draws %d)\n",
							 action.frame, action.selector.str(), (Int)obj->isDisabledByType( DISABLED_UNDERPOWERED ),
							 player->getEnergy()->getProduction(), player->getEnergy()->getConsumption()));
		return FALSE;
	}

	Int fired = 0;
	for( BehaviorModule **m = obj->getBehaviorModules(); *m; ++m )
	{
		SpecialPowerModuleInterface *power = (*m)->getSpecialPower();
		if (power == NULL)
			continue;
		if (!action.targetSelector.isEmpty() && power->getPowerName() != action.targetSelector)
			continue;
		power->doSpecialPowerAtLocation( &target, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT );
		++fired;
	}

	DEBUG_LOG(("SCENARIO: frame %d power slot %d '%s' fired %d at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(), fired, target.x, target.y));
	return fired > 0;
}

/** Queue `count` of the template in the first matching building of this seat, the way a click on
	  its build button would, money and prerequisites checked by the building's own queue.  What a
	  queue refuses is counted and reported, not forced. */
static Bool executeProduce( const ScenarioAction &action, Player *player )
{
	Object *building = findFirstMatching( player, action.selector );
	ProductionUpdateInterface *queue = building ? building->getProductionUpdateInterface() : NULL;
	const ThingTemplate *made = TheThingFactory->findTemplate( action.targetSelector );
	if (queue == NULL || made == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d produce: slot %d has no producing '%s', or '%s' is no template\n",
							 action.frame, action.slot, action.selector.str(), action.targetSelector.str()));
		return FALSE;
	}

	Int queued = 0;
	for (Int each = 0; each < action.count; ++each)
	{
		if (queue->canQueueCreateUnit( made ) == CANMAKE_OK && queue->queueCreateUnit( made, queue->requestUniqueUnitID() ))
			++queued;
	}

	DEBUG_LOG(("SCENARIO: frame %d produce slot %d '%s' queued %d of %d '%s'\n",
						 action.frame, action.slot, action.selector.str(), queued, action.count, action.targetSelector.str()));
	return queued > 0;
}

/** How much of this seat's matching stock is still standing: the count, the health left against the
	  health they started with, and what the living ones cost.  Structures count too, so a fight between
	  an army and a defence reads as money lost on each side. */
static Bool executeTally( const ScenarioAction &action, Player *player )
{
	Int alive = 0;
	Int inside = 0;
	Real health = 0.0f;
	Real maxHealth = 0.0f;
	Int worth = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() != player || obj->isEffectivelyDead() || !selectorMatches( action.selector, obj ))
			continue;

		++alive;
		if (obj->getContainedBy() != NULL)
			++inside;
		health += obj->getBodyModule()->getHealth();
		maxHealth += obj->getBodyModule()->getMaxHealth();
		worth += obj->getTemplate()->friend_getBuildCost();
	}

	DEBUG_LOG(("HEADLESS TALLY: frame %d slot %d '%s': %d alive, health %.0f of %.0f, worth %d, %d inside, money %d\n",
						 action.frame, action.slot, action.selector.str(), alive, health, maxHealth, worth, inside,
						 player->getMoney()->countMoney()));
	if (action.count >= 0)
		DEBUG_LOG(("HEADLESS EXPECT: %s: frame %d slot %d '%s': %d alive, wanted %d\n",
							 alive == action.count ? "PASS" : "FAIL",
							 action.frame, action.slot, action.selector.str(), alive, action.count));
	return TRUE;
}

/** A shift click: the order goes onto the seat's order queue the way it would arrive over the network
	  with MSG_QUEUE_NEXT_ORDER in front of it, carrying the arguments the command translator gives it.
	  The group is destroyed here on every path, by the queue when it takes the order. */
static Bool executeShiftOrder( const ScenarioAction &action, Player *player, const Coord3D &dest,
															 AIGroup *group, Int taken )
{
	GameMessage::Type type = GameMessage::MSG_DO_GUARD_POSITION;
	const char *verb = "shiftguard";
	if (action.action == SCENARIO_ACTION_SHIFTMOVE)
	{
		type = GameMessage::MSG_DO_MOVETO;
		verb = "shiftmove";
	}
	else if (action.action == SCENARIO_ACTION_SHIFTATTACKMOVE)
	{
		type = GameMessage::MSG_DO_ATTACKMOVETO;
		verb = "shiftattackmove";
	}
	else if (action.action == SCENARIO_ACTION_SHIFTATTACK)
	{
		type = GameMessage::MSG_DO_ATTACK_OBJECT;
		verb = "shiftattack";
	}
	else if (action.action == SCENARIO_ACTION_SHIFTPOWER)
	{
		type = GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT;
		verb = "shiftpower";
	}

	GameMessage *msg = newInstance( GameMessage )( type );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );

	if (action.action == SCENARIO_ACTION_SHIFTATTACK || action.action == SCENARIO_ACTION_SHIFTPOWER)
	{
		Player *targetPlayer = findPlayerForSlot( action.targetSlot );
		Object *target = (targetPlayer != NULL) ? findFirstMatching( targetPlayer, action.targetSelector ) : NULL;
		const SpecialPowerTemplate *power = (action.action == SCENARIO_ACTION_SHIFTPOWER)
																				? TheSpecialPowerStore->findSpecialPowerTemplate( action.name ) : NULL;
		if (target == NULL || (action.action == SCENARIO_ACTION_SHIFTPOWER && power == NULL))
		{
			DEBUG_LOG(("SCENARIO: frame %d %s: slot %d owns nothing matching '%s', or there is no power '%s'\n",
								 action.frame, verb, action.targetSlot, action.targetSelector.str(), action.name.str()));
			msg->deleteInstance();
			TheAI->destroyGroup( group );
			return FALSE;
		}

		if (power != NULL)
			msg->appendIntegerArgument( power->getID() );
		msg->appendObjectIDArgument( target->getID() );
		if (power != NULL)
		{
			msg->appendIntegerArgument( 0 );						// the button's options, which a capture never reads
			msg->appendObjectIDArgument( INVALID_ID );	// fired by the selection, as off the command bar
		}
	}
	else
	{
		msg->appendLocationArgument( dest );
		if (type == GameMessage::MSG_DO_ATTACKMOVETO)
			msg->appendBooleanArgument( FALSE );
		if (type == GameMessage::MSG_DO_GUARD_POSITION)
		{
			msg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );
			if (action.radius > 0.0f)
				msg->appendRealArgument( action.radius );
		}
	}

	player->getOrderQueue()->setNextOrderMode( ORDER_QUEUE_APPEND );
	player->getOrderQueue()->takeMessage( msg, group, player );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d %s slot %d '%s' x%d\n",
						 action.frame, verb, action.slot, action.selector.str(), taken));
	return TRUE;
}

/** Shift on an object upgrade button, once for every unit that matches: the two messages the command
	  bar sends, handed to the dispatcher the way they arrive.  An upgrade names the unit that buys it, so
	  unlike the other shift verbs this one needs nothing selected - and the buyer can be a building,
	  which takes no orders: a barracks researching Capture Building is a shift-click on its button too. */
static Bool executeShiftUpgrade( const ScenarioAction &action, Player *player )
{
	std::vector<ObjectID> buyers;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() == player && !obj->isEffectivelyDead() && selectorMatches( action.selector, obj ))
			buyers.push_back( obj->getID() );
	}
	const Int taken = (Int)buyers.size();
	if (taken == 0)
	{
		DEBUG_LOG(("SCENARIO: frame %d: slot %d owns nothing matching '%s'\n",
							 action.frame, action.slot, action.selector.str()));
		return FALSE;
	}

	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade( action.name );
	if (upgrade == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d shiftupgrade: there is no upgrade '%s'\n", action.frame, action.name.str()));
		return FALSE;
	}

	for (std::vector<ObjectID>::const_iterator it = buyers.begin(); it != buyers.end(); ++it)
	{
		GameMessage *prefix = newInstance( GameMessage )( GameMessage::MSG_QUEUE_NEXT_ORDER );
		prefix->friend_setPlayerIndex( player->getPlayerIndex() );
		prefix->appendIntegerArgument( ORDER_QUEUE_APPEND );
		TheGameLogic->logicMessageDispatcher( prefix, NULL );
		prefix->deleteInstance();

		GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_QUEUE_UPGRADE );
		msg->friend_setPlayerIndex( player->getPlayerIndex() );
		msg->appendObjectIDArgument( *it );
		msg->appendIntegerArgument( upgrade->getUpgradeNameKey() );
		TheGameLogic->logicMessageDispatcher( msg, NULL );
		msg->deleteInstance();
	}

	DEBUG_LOG(("SCENARIO: frame %d shiftupgrade slot %d '%s' x%d '%s'\n",
						 action.frame, action.slot, action.selector.str(), taken, action.name.str()));
	return TRUE;
}

/** The stance key: MSG_SET_STANCE handed to the dispatcher with the matching units as the selection,
	  which is how it arrives from a player.  The dispatcher destroys the group. */
static Bool executeStance( const ScenarioAction &action, Player *player, AIGroup *group, Int taken )
{
	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_SET_STANCE );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );
	msg->appendIntegerArgument( action.name == "aggressive" ? 1 : 0 );
	TheGameLogic->logicMessageDispatcher( msg, group );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d stance slot %d '%s' x%d %s\n",
						 action.frame, action.slot, action.selector.str(), taken, action.name.str()));
	return TRUE;
}

/** The attack key armed and a left click on one enemy: MSG_DO_FORCE_ATTACK_OBJECT handed to the
	  dispatcher with the matching units as the selection.  The client only sends it when one of them can
	  shoot the target, so a seat that could not is refused here the same way rather than ordered. */
static Bool executeForceAttack( const ScenarioAction &action, Player *player, AIGroup *group, Int taken )
{
	Player *targetPlayer = findPlayerForSlot( action.targetSlot );
	Object *target = (targetPlayer != NULL) ? findFirstMatching( targetPlayer, action.targetSelector ) : NULL;
	Bool canShoot = FALSE;
	const VecObjectID ids = group->getAllIDs();
	for( VecObjectID::const_iterator it = ids.begin(); target != NULL && it != ids.end(); ++it )
	{
		const Object *obj = TheGameLogic->findObjectByID( *it );
		const CanAttackResult result = obj->isAbleToAttack()
																	 ? obj->getAbleToAttackSpecificObject( ATTACK_NEW_TARGET_FORCED, target, CMD_FROM_PLAYER )
																	 : ATTACKRESULT_NOT_POSSIBLE;
		canShoot = canShoot || result == ATTACKRESULT_POSSIBLE || result == ATTACKRESULT_POSSIBLE_AFTER_MOVING;
	}
	if (!canShoot)
	{
		DEBUG_LOG(("SCENARIO: frame %d forceattack: none of slot %d '%s' x%d can shoot slot %d '%s'; the click sends nothing\n",
							 action.frame, action.slot, action.selector.str(), taken, action.targetSlot, action.targetSelector.str()));
		TheAI->destroyGroup( group );
		return FALSE;
	}

	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_DO_FORCE_ATTACK_OBJECT );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );
	msg->appendObjectIDArgument( target->getID() );
	TheGameLogic->logicMessageDispatcher( msg, group );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d forceattack slot %d '%s' x%d -> slot %d '%s'\n",
						 action.frame, action.slot, action.selector.str(), taken, action.targetSlot, action.targetSelector.str()));
	return TRUE;
}

/** A player's click on a building or transport with the matching units selected: MSG_ENTER handed to
	  the dispatcher, so the order carries CMD_FROM_PLAYER and meets the checks a human's does, where
	  enter is a script's order. */
static Bool executeClickEnter( const ScenarioAction &action, Player *player, AIGroup *group, Int taken )
{
	Object *target = NULL;
	if (action.targetSlot == SCENARIO_CIVILIAN_SLOT)
	{
		Coord3D center;
		group->getCenter( &center );
		target = findNearestCivilian( action.targetSelector, &center );
	}
	else
	{
		Player *targetPlayer = findPlayerForSlot( action.targetSlot );
		target = (targetPlayer != NULL) ? findFirstMatching( targetPlayer, action.targetSelector ) : NULL;
	}
	if (target == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d clickenter: slot %d owns nothing matching '%s'\n",
							 action.frame, action.targetSlot, action.targetSelector.str()));
		TheAI->destroyGroup( group );
		return FALSE;
	}

	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_ENTER );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );
	msg->appendObjectIDArgument( INVALID_ID );		// the selection, as the click sends it
	msg->appendObjectIDArgument( target->getID() );
	TheGameLogic->logicMessageDispatcher( msg, group );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d clickenter slot %d '%s' x%d -> slot %d '%s' %d\n",
						 action.frame, action.slot, action.selector.str(), taken, action.targetSlot,
						 action.targetSelector.str(), target->getID()));
	return TRUE;
}

/** A FIRE_WEAPON button with NEED_TARGET_POS, clicked on the ground (the Comanche's rocket pods): the
	  arguments GUICommandTranslator gives MSG_DO_WEAPON_AT_LOCATION, the button's default shot count and
	  nothing under the cursor, handed to the dispatcher with the matching units as the selection.  The
	  dispatcher destroys the group. */
static Bool executeWeaponAt( const ScenarioAction &action, Player *player, const Coord3D &dest, AIGroup *group, Int taken )
{
	const WeaponSlotType weaponSlot = (action.name == "primary") ? PRIMARY_WEAPON
																	: (action.name == "secondary") ? SECONDARY_WEAPON : TERTIARY_WEAPON;
	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_DO_WEAPON_AT_LOCATION );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );
	msg->appendIntegerArgument( weaponSlot );
	msg->appendLocationArgument( dest );
	msg->appendIntegerArgument( 0x7fffffff );		// CommandButton's MaxShotsToFire when the INI says none
	msg->appendObjectIDArgument( INVALID_ID );
	TheGameLogic->logicMessageDispatcher( msg, group );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d weaponat slot %d '%s' x%d %s at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(), taken, action.name.str(), dest.x, dest.y));
	return TRUE;
}

/** The rank tree's button: the player is raised to the rank the science asks for and given the points
	  it costs, and then MSG_PURCHASE_SCIENCE goes to the dispatcher as the control bar sends it, so the
	  purchase itself is the one a click makes. */
static const Int SCENARIO_HIGHEST_RANK = 5;

static Bool executePurchase( const ScenarioAction &action, Player *player )
{
	const ScienceType science = TheScienceStore->getScienceFromInternalName( action.selector );
	if (science == SCIENCE_INVALID)
	{
		DEBUG_LOG(("SCENARIO: frame %d purchase: there is no science '%s'\n", action.frame, action.selector.str()));
		return FALSE;
	}

	while (!TheScienceStore->playerHasPrereqsForScience( player, science ) && player->getRankLevel() < SCENARIO_HIGHEST_RANK)
		player->setRankLevel( player->getRankLevel() + 1 );
	const Int cost = TheScienceStore->getSciencePurchaseCost( science );
	if (player->getSciencePurchasePoints() < cost)
		player->addSciencePurchasePoints( cost - player->getSciencePurchasePoints() );

	GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_PURCHASE_SCIENCE );
	msg->friend_setPlayerIndex( player->getPlayerIndex() );
	msg->appendIntegerArgument( science );
	TheGameLogic->logicMessageDispatcher( msg, NULL );
	msg->deleteInstance();

	DEBUG_LOG(("SCENARIO: frame %d purchase slot %d '%s' rank %d: %s\n", action.frame, action.slot,
						 action.selector.str(), player->getRankLevel(), player->hasScience( science ) ? "bought" : "refused"));
	return player->hasScience( science );
}

static Bool executeOrder( const ScenarioAction &action, Player *player, const Coord3D &dest )
{
	if (action.action == SCENARIO_ACTION_SHIFTUPGRADE)
		return executeShiftUpgrade( action, player );

	AIGroup *group = TheAI->createGroup();
	const Int taken = gatherIntoGroup( player, action.selector, group );
	if (taken == 0)
	{
		TheAI->destroyGroup( group );
		DEBUG_LOG(("SCENARIO: frame %d: slot %d owns nothing matching '%s'\n",
							 action.frame, action.slot, action.selector.str()));
		return FALSE;
	}

	Bool ordered = TRUE;
	switch (action.action)
	{
		case SCENARIO_ACTION_MOVE:
		case SCENARIO_ACTION_PLAYERMOVE:
		case SCENARIO_ACTION_ATTACKMOVE:
		case SCENARIO_ACTION_PLAYERATTACKMOVE:
		{
			// A player's move takes a different branch of groupMoveToPosition (every member gathers on
			// the clicked point), so a script order cannot stand in for a right click.  The same goes
			// for an attack move, which only a player's or a computer's takes through the tunnels.
			const Bool attacks = action.action == SCENARIO_ACTION_ATTACKMOVE || action.action == SCENARIO_ACTION_PLAYERATTACKMOVE;
			if (action.action == SCENARIO_ACTION_MOVE)
				group->groupMoveToPosition( &dest, FALSE, CMD_FROM_SCRIPT );
			else if (action.action == SCENARIO_ACTION_PLAYERMOVE)
				group->groupMoveToPosition( &dest, FALSE, CMD_FROM_PLAYER );
			else if (action.action == SCENARIO_ACTION_PLAYERATTACKMOVE)
				group->groupAttackMoveToPosition( &dest, SCENARIO_ATTACK_SHOTS, CMD_FROM_PLAYER );
			else
				group->groupAttackMoveToPosition( &dest, SCENARIO_ATTACK_SHOTS, CMD_FROM_SCRIPT );

			DEBUG_LOG(("SCENARIO: frame %d %s slot %d '%s' x%d to (%.0f,%.0f)\n",
								 action.frame,
								 attacks ? "attackmove" : "move",
								 action.slot, action.selector.str(), taken, dest.x, dest.y));
			break;
		}

		case SCENARIO_ACTION_ATTACK:
		case SCENARIO_ACTION_ENTER:
		case SCENARIO_ACTION_RESUME:
		case SCENARIO_ACTION_DOCK:
		case SCENARIO_ACTION_GUARD:
		{
			const char *verb = (action.action == SCENARIO_ACTION_ATTACK) ? "attack"
											 : (action.action == SCENARIO_ACTION_DOCK) ? "dock"
											 : (action.action == SCENARIO_ACTION_GUARD) ? "guard"
											 : (action.action == SCENARIO_ACTION_RESUME) ? "resume" : "enter";
			Object *target = NULL;
			if (action.targetSlot == SCENARIO_CIVILIAN_SLOT)
			{
				Coord3D center;
				group->getCenter( &center );
				target = findNearestCivilian( action.targetSelector, &center );
			}
			else
			{
				Player *targetPlayer = findPlayerForSlot( action.targetSlot );
				target = (targetPlayer != NULL) ? findFirstMatching( targetPlayer, action.targetSelector ) : NULL;
			}
			if (target == NULL)
			{
				DEBUG_LOG(("SCENARIO: frame %d %s: slot %d owns nothing matching '%s'\n",
									 action.frame, verb, action.targetSlot, action.targetSelector.str()));
				ordered = FALSE;
				break;
			}

			if (action.action == SCENARIO_ACTION_ATTACK)
				group->groupAttackObject( target, SCENARIO_ATTACK_SHOTS, CMD_FROM_SCRIPT );
			else if (action.action == SCENARIO_ACTION_DOCK)
				group->groupDock( target, CMD_FROM_SCRIPT );
			else if (action.action == SCENARIO_ACTION_GUARD)
				group->groupGuardObject( target, GUARDMODE_NORMAL, CMD_FROM_SCRIPT );
			else if (action.action == SCENARIO_ACTION_RESUME)
				group->groupResumeConstruction( target, CMD_FROM_PLAYER );
			else
				group->groupEnter( target, CMD_FROM_SCRIPT );

			DEBUG_LOG(("SCENARIO: frame %d %s slot %d '%s' x%d -> slot %d '%s'\n",
								 action.frame, verb, action.slot, action.selector.str(), taken,
								 action.targetSlot, action.targetSelector.str()));
			break;
		}

		case SCENARIO_ACTION_SHIFTMOVE:
		case SCENARIO_ACTION_SHIFTATTACKMOVE:
		case SCENARIO_ACTION_SHIFTATTACK:
		case SCENARIO_ACTION_SHIFTGUARD:
		case SCENARIO_ACTION_SHIFTPOWER:
			return executeShiftOrder( action, player, dest, group, taken );		// the group is gone either way

		case SCENARIO_ACTION_STANCE:
			return executeStance( action, player, group, taken );		// the dispatcher destroys the group

		case SCENARIO_ACTION_FORCEATTACK:
			return executeForceAttack( action, player, group, taken );		// so does this, or it does itself

		case SCENARIO_ACTION_CLICKENTER:
			return executeClickEnter( action, player, group, taken );		// the dispatcher again

		case SCENARIO_ACTION_WEAPONAT:
			return executeWeaponAt( action, player, dest, group, taken );		// the dispatcher again

		case SCENARIO_ACTION_FORCEGROUND:
		{
			// the attack key's click on open ground, through the dispatcher, which destroys the group
			GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_DO_FORCE_ATTACK_GROUND );
			msg->friend_setPlayerIndex( player->getPlayerIndex() );
			msg->appendLocationArgument( dest );
			TheGameLogic->logicMessageDispatcher( msg, group );
			msg->deleteInstance();
			DEBUG_LOG(("SCENARIO: frame %d forceground slot %d '%s' x%d at (%.0f,%.0f)\n",
								 action.frame, action.slot, action.selector.str(), taken, dest.x, dest.y));
			return TRUE;
		}

		case SCENARIO_ACTION_REAIM:
		{
			// the right click CommandXlat sends while a power's aim can be moved; the dispatcher destroys the group
			GameMessage *msg = newInstance( GameMessage )( GameMessage::MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION );
			msg->friend_setPlayerIndex( player->getPlayerIndex() );
			msg->appendLocationArgument( dest );
			msg->appendIntegerArgument( SPECIAL_INVALID );
			msg->appendObjectIDArgument( INVALID_ID );
			TheGameLogic->logicMessageDispatcher( msg, group );
			msg->deleteInstance();
			DEBUG_LOG(("SCENARIO: frame %d reaim slot %d '%s' x%d at (%.0f,%.0f)\n",
								 action.frame, action.slot, action.selector.str(), taken, dest.x, dest.y));
			return TRUE;
		}

		case SCENARIO_ACTION_RANK:
		{
			const VeterancyLevel level = action.name == "veteran" ? LEVEL_VETERAN
																 : action.name == "elite" ? LEVEL_ELITE : LEVEL_HEROIC;
			const VecObjectID &ids = group->getAllIDs();
			for (VecObjectID::const_iterator it = ids.begin(); it != ids.end(); ++it)
			{
				Object *obj = TheGameLogic->findObjectByID( *it );
				ExperienceTracker *tracker = obj->getExperienceTracker();
				const RankLadder *ladder = TheGlobalData->getRankLadder( obj->getTemplate() );
				const Int unitValue = RankLadder::unitValue( obj->getTemplate() );
				if (action.name == "xp")
					tracker->setExperienceAndLevel( action.count * unitValue );
				else if (action.name == "grade" && ladder)
					tracker->setExperienceAndLevel( ladder->threshold( unitValue, action.count ) );
				else if (action.name != "grade")
					tracker->setVeterancyLevel( level );
				DEBUG_LOG(("SCENARIO: frame %d rank slot %d '%s' %s xp=%d grade=%d level=%d\n", action.frame, action.slot,
									 obj->getTemplate()->getName().str(), action.name.str(), tracker->getCurrentExperience(),
									 tracker->getGrade(), (Int)tracker->getVeterancyLevel()));
			}
			DEBUG_LOG(("SCENARIO: frame %d rank slot %d '%s' x%d %s %d\n",
								 action.frame, action.slot, action.selector.str(), taken, action.name.str(), action.count));
			break;
		}

		case SCENARIO_ACTION_STOP:
		{
			group->groupIdle( CMD_FROM_SCRIPT );
			DEBUG_LOG(("SCENARIO: frame %d stop slot %d '%s' x%d\n",
								 action.frame, action.slot, action.selector.str(), taken));
			break;
		}

		case SCENARIO_ACTION_CLICKSTOP:
		{
			group->groupIdle( CMD_FROM_PLAYER );		// what GameLogicDispatch does with MSG_DO_STOP
			DEBUG_LOG(("SCENARIO: frame %d clickstop slot %d '%s' x%d\n",
								 action.frame, action.slot, action.selector.str(), taken));
			break;
		}

		case SCENARIO_ACTION_SPAWN:
		case SCENARIO_ACTION_ARRIVE:
		case SCENARIO_ACTION_PARTICLES:
		case SCENARIO_ACTION_POWER:
		case SCENARIO_ACTION_PRODUCE:
		case SCENARIO_ACTION_REPEAT:
		case SCENARIO_ACTION_GIVEMONEY:
		case SCENARIO_ACTION_TALLY:
		case SCENARIO_ACTION_RESPOND:
		case SCENARIO_ACTION_REACHPROOF:
		case SCENARIO_ACTION_TOPUP:
			ordered = FALSE;		// handled before the group is built
			break;
	}

	TheAI->destroyGroup( group );
	return ordered;
}

/** Every matching defence of the seat put through ReachOverlay_prove against the first matching unit
	  of the target seat, which is moved about for it and put back. */
static Bool executeReachProof( const ScenarioAction &action, Player *player )
{
	Player *targetPlayer = findPlayerForSlot( action.targetSlot );
	Object *probe = NULL;
	for( Object *obj = TheGameLogic->getFirstObject(); obj && targetPlayer; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() == targetPlayer && !obj->isEffectivelyDead() && obj->getContainedBy() == NULL
				&& selectorMatches( action.targetSelector, obj ))
		{
			probe = obj;
			break;
		}
	}
	if (probe == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d reachproof slot %d: no '%s' of slot %d to stand in\n",
							 action.frame, action.slot, action.targetSelector.str(), action.targetSlot));
		return FALSE;
	}

	Int proven = 0;
	Int passed = 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		if (obj->getControllingPlayer() != player || obj->isEffectivelyDead() || !selectorMatches( action.selector, obj ))
			continue;
		++proven;
		if (ReachOverlay_prove( obj, probe, action.count ))
			++passed;
	}
	DEBUG_LOG(("SCENARIO: frame %d reachproof slot %d '%s' against '%s': %d of %d passed\n",
						 action.frame, action.slot, action.selector.str(), probe->getTemplate()->getName().str(), passed, proven));
	return proven > 0 && passed == proven;
}

/** A placement click, as PlaceEventTranslator sends it: one MSG_DOZER_CONSTRUCT onto the message
	  stream, so it is recorded in the replay and reaches every machine of a network game, and the
	  logic picks the builder from whatever is selected then - with nothing selected, the player's idle
	  builder nearest the site.  Only the local player can click, so any other slot is refused. */
static Bool executeConstruct( const ScenarioAction &action, Player *player, const Coord3D &dest )
{
	const ThingTemplate *build = TheThingFactory->findTemplate( action.selector );
	if (player != ThePlayerList->getLocalPlayer() || build == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d construct: slot %d is not the local player, or '%s' is no template\n",
							 action.frame, action.slot, action.selector.str()));
		return FALSE;
	}

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_DOZER_CONSTRUCT );
	msg->appendIntegerArgument( build->getTemplateID() );
	msg->appendLocationArgument( dest );
	msg->appendRealArgument( 0.0f );

	DEBUG_LOG(("SCENARIO: frame %d construct slot %d '%s' at (%.0f,%.0f)\n",
						 action.frame, action.slot, action.selector.str(), dest.x, dest.y));
	return TRUE;
}

/** The Ctrl+Alt build key on a factory: one MSG_SET_REPEAT_UNIT onto the message stream, recorded and
	  sent to every machine like construct, so only the local player can press it. */
static Bool executeRepeat( const ScenarioAction &action, Player *player )
{
	Object *building = findFirstMatching( player, action.selector );
	const ThingTemplate *made = TheThingFactory->findTemplate( action.targetSelector );
	if (player != ThePlayerList->getLocalPlayer() || building == NULL || made == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d repeat: slot %d is not the local player, has no '%s', or '%s' is no template\n",
							 action.frame, action.slot, action.selector.str(), action.targetSelector.str()));
		return FALSE;
	}

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_SET_REPEAT_UNIT );
	msg->appendIntegerArgument( made->getTemplateID() );
	msg->appendObjectIDArgument( building->getID() );
	msg->appendBooleanArgument( action.name == "on" );

	DEBUG_LOG(("SCENARIO: frame %d repeat slot %d '%s' %d '%s' %s\n", action.frame, action.slot,
						 action.selector.str(), building->getID(), action.targetSelector.str(), action.name.str()));
	return TRUE;
}

/** The Tab screen's Send: one MSG_TRANSFER_MONEY onto the message stream, recorded and checked in
	  GameLogic like a click on the button, so only the local player can send it.  The target is a seat;
	  the screen never offers an enemy, and this is how a request to pay one gets made anyway. */
static Bool executeGiveMoney( const ScenarioAction &action, Player *player )
{
	Player *target = findPlayerForSlot( action.targetSlot );
	if (player != ThePlayerList->getLocalPlayer() || target == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d givemoney: slot %d is not the local player, or nobody sits in slot %d\n",
							 action.frame, action.slot, action.targetSlot));
		return FALSE;
	}

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_TRANSFER_MONEY );
	msg->appendIntegerArgument( target->getPlayerIndex() );
	msg->appendIntegerArgument( action.count );

	DEBUG_LOG(("SCENARIO: frame %d givemoney slot %d to slot %d (player %d) %d\n", action.frame, action.slot,
						 action.targetSlot, target->getPlayerIndex(), action.count));
	return TRUE;
}

Bool ScenarioDrill_execute( const ScenarioAction &action )
{
	Player *player = findPlayerForSlot( action.slot );
	if (player == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d: nobody is sitting in slot %d\n", action.frame, action.slot));
		return FALSE;
	}

	if (action.action == SCENARIO_ACTION_TOPUP)
	{
		if (TheThingFactory->findTemplate( action.selector, FALSE ) == NULL)
		{
			DEBUG_LOG(("SCENARIO: frame %d topup: no template named '%s'\n", action.frame, action.selector.str()));
			return FALSE;
		}
		theScenarioTopups.push_back( action );
		const Coord3D centre = mapCentre();
		DEBUG_LOG(("SCENARIO: frame %d topup slot %d '%s' to %d objects, road to (%.0f,%.0f)\n", action.frame, action.slot,
							 action.selector.str(), action.count, centre.x, centre.y));
		return TRUE;
	}

	Coord3D position;
	if (!resolveScenarioPosition( action, &position ))
		return FALSE;

	if (action.action == SCENARIO_ACTION_SPAWN)
		return executeSpawn( action, player, position );

	if (action.action == SCENARIO_ACTION_PARTICLES)
		return executeParticles( action, position );

	if (action.action == SCENARIO_ACTION_ARRIVE)
		return executeArrive( action, player, position );

	if (action.action == SCENARIO_ACTION_RESPOND)
		return executeRespond( action, player, position );

	if (action.action == SCENARIO_ACTION_POWER)
		return executePower( action, player, position );

	if (action.action == SCENARIO_ACTION_PRODUCE)
		return executeProduce( action, player );

	if (action.action == SCENARIO_ACTION_REPEAT)
		return executeRepeat( action, player );

	if (action.action == SCENARIO_ACTION_GIVEMONEY)
		return executeGiveMoney( action, player );

	if (action.action == SCENARIO_ACTION_TALLY)
		return executeTally( action, player );

	if (action.action == SCENARIO_ACTION_REACHPROOF)
		return executeReachProof( action, player );

	if (action.action == SCENARIO_ACTION_PURCHASE)
		return executePurchase( action, player );

	if (action.action == SCENARIO_ACTION_CONSTRUCT)
		return executeConstruct( action, player, position );

	return executeOrder( action, player, position );
}

// ------------------------------------------------------------------------------------------------
// the tick
// ------------------------------------------------------------------------------------------------

void ScenarioDrill_tick( void )
{
	// the shell map is a running game too, and an army spawned onto the main menu is not the match
	// anybody asked to measure
	if (TheGameLogic->isInShellGame())
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	// a restart runs the clock back, and the file should play again rather than sit finished
	if (now < theScenarioLastFrame)
		resetScenario();
	theScenarioLastFrame = now;

	// before the file test, because an arrive typed down the control socket has no file behind it
	updateArrivals( now );
	updateResponses( now );

	if (TheGlobalData->m_scenarioFile.isEmpty())
		return;

	if (!theScenarioLoaded)
		loadScenario();

	// the gate's crowd is there to measure the frame, and the statue count's walk of every unit would be in it
	if (theScenarioTopups.empty())
		updateStatues( now );
	runTopups( now );

	// a scenario file counts 30 Hz frames, so the files written before 60 Hz logic play at the same seconds
	while (theScenarioCursor < (Int)theScenarioActions.size()
				 && ConvertRetailFrames( theScenarioActions[ theScenarioCursor ].frame ) <= now)
	{
		if (ScenarioDrill_execute( theScenarioActions[ theScenarioCursor ] ))
			++theScenarioActionsRun;
		else
			++theScenarioActionsFailed;
		++theScenarioCursor;
	}
}

const char *ScenarioDrill_report( void )
{
	sprintf( theScenarioReport, "%d actions, %d ran, %d refused, %d units spawned",
					 (Int)theScenarioActions.size(), theScenarioActionsRun,
					 theScenarioActionsFailed, theScenarioUnitsSpawned );
	return theScenarioReport;
}
