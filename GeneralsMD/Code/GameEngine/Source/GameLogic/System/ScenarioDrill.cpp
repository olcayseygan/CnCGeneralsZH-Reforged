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
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
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

#include <algorithm>
#include <vector>

// ------------------------------------------------------------------------------------------------
// the shape of a scenario line
// ------------------------------------------------------------------------------------------------

static const Int SCENARIO_MAX_TOKENS = 10;
static const char SCENARIO_COMMENT_CHAR = '#';
static const char *SCENARIO_SELECTOR_ALL = "*";

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

// where the name sits in the lines that carry one
static const Int SCENARIO_SHIFTPOWER_NAME_TOKEN = 6;
static const Int SCENARIO_SHIFTUPGRADE_NAME_TOKEN = 4;

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
		case SCENARIO_ACTION_SHIFTATTACK:	return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_SHIFTPOWER:	return SCENARIO_TOKENS_SHIFTPOWER;
		case SCENARIO_ACTION_SHIFTUPGRADE:	return SCENARIO_TOKENS_SHIFTUPGRADE;
		case SCENARIO_ACTION_ATTACK:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_ENTER:				return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_DOCK:				return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_PRODUCE:			return SCENARIO_TOKENS_ATTACK;
		case SCENARIO_ACTION_STOP:				return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_TALLY:				return SCENARIO_TOKENS_STOP;
		case SCENARIO_ACTION_ARRIVE:			return SCENARIO_TOKENS_ARRIVE;
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
		{
			Int next = SCENARIO_ORDER_POSITION_TOKEN;
			const ScenarioParseResult position = parseScenarioPosition( tokens, count, &next, action );
			if (position != SCENARIO_PARSE_OK)
				return position;
			if (actionType == SCENARIO_ACTION_ARRIVE && count > next)
				action->radius = (Real)atof( tokens[ next ].str() );
			if (actionType == SCENARIO_ACTION_POWER && count > next)
				action->targetSelector = tokens[ next ];	// fire only the power of this name
			break;
		}

		case SCENARIO_ACTION_ATTACK:
		case SCENARIO_ACTION_ENTER:
		case SCENARIO_ACTION_DOCK:
		case SCENARIO_ACTION_SHIFTATTACK:
		case SCENARIO_ACTION_SHIFTPOWER:
		{
			if (!parseWholeNumber( tokens[ 4 ], &action->targetSlot ))
				return SCENARIO_PARSE_BAD_SLOT;
			action->targetSelector = tokens[ 5 ];
			if (actionType == SCENARIO_ACTION_SHIFTPOWER)
				action->name = tokens[ SCENARIO_SHIFTPOWER_NAME_TOKEN ];
			break;
		}

		case SCENARIO_ACTION_SHIFTUPGRADE:
			action->name = tokens[ SCENARIO_SHIFTUPGRADE_NAME_TOKEN ];
			break;

		case SCENARIO_ACTION_PRODUCE:
		{
			action->targetSelector = tokens[ 4 ];	// what to make
			if (!parseWholeNumber( tokens[ 5 ], &action->count ) || action->count < 1)
				return SCENARIO_PARSE_BAD_COUNT;
			break;
		}

		case SCENARIO_ACTION_STOP:
		case SCENARIO_ACTION_TALLY:
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
static const Real SCENARIO_STILL_DISTANCE = 0.05f;

/// how far a unit has to have gone from where it stood before it counts as having obeyed the order
static const Real SCENARIO_DEPARTED_DISTANCE = 20.0f;

static std::vector<ScenarioArrival> theScenarioArrivals;

static void resetScenario( void )
{
	theScenarioArrivals.clear();
	theScenarioActions.clear();
	theScenarioCursor = 0;
	theScenarioLoaded = FALSE;
	theScenarioActionsRun = 0;
	theScenarioActionsFailed = 0;
	theScenarioUnitsSpawned = 0;
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

/** The recipe the debug spawn path uses (GameLogic.cpp's unitTimings): a template's onCreate ran in
	  the constructor, but a unit that was never built still has to be told it is finished, or half
	  its modules never start.  The team has to be activated too, or it counts as an empty team. */
static Bool spawnOne( const ThingTemplate *tmpl, Team *team, const Coord3D *pos )
{
	Object *obj = TheThingFactory->newObject( tmpl, team );
	if (obj == NULL)
		return FALSE;

	obj->setOrientation( 0 );
	obj->setPosition( pos );

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
	return TRUE;
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

		if (spawnOne( tmpl, team, &pos ))
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

void ScenarioDrill_logArrivals( void )
{
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
		DEBUG_LOG(("SCENARIO: frame %d power: '%s' is disabled (no power?)\n",
							 action.frame, action.selector.str()));
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
			msg->appendIntegerArgument( GUARDMODE_GUARD_WITHOUT_PURSUIT );
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
	  unlike the other shift verbs this one needs nothing selected. */
static Bool executeShiftUpgrade( const ScenarioAction &action, Player *player, AIGroup *group, Int taken )
{
	const std::vector<ObjectID> buyers = group->getAllIDs();
	TheAI->destroyGroup( group );

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

static Bool executeOrder( const ScenarioAction &action, Player *player, const Coord3D &dest )
{
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
		case SCENARIO_ACTION_DOCK:
		{
			const char *verb = (action.action == SCENARIO_ACTION_ATTACK) ? "attack"
											 : (action.action == SCENARIO_ACTION_DOCK) ? "dock" : "enter";
			Player *targetPlayer = findPlayerForSlot( action.targetSlot );
			Object *target = (targetPlayer != NULL)
											 ? findFirstMatching( targetPlayer, action.targetSelector )
											 : NULL;
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

		case SCENARIO_ACTION_SHIFTUPGRADE:
			return executeShiftUpgrade( action, player, group, taken );

		case SCENARIO_ACTION_STOP:
		{
			group->groupIdle( CMD_FROM_SCRIPT );
			DEBUG_LOG(("SCENARIO: frame %d stop slot %d '%s' x%d\n",
								 action.frame, action.slot, action.selector.str(), taken));
			break;
		}

		case SCENARIO_ACTION_SPAWN:
		case SCENARIO_ACTION_ARRIVE:
		case SCENARIO_ACTION_PARTICLES:
		case SCENARIO_ACTION_POWER:
		case SCENARIO_ACTION_PRODUCE:
		case SCENARIO_ACTION_TALLY:
			ordered = FALSE;		// handled before the group is built
			break;
	}

	TheAI->destroyGroup( group );
	return ordered;
}

Bool ScenarioDrill_execute( const ScenarioAction &action )
{
	Player *player = findPlayerForSlot( action.slot );
	if (player == NULL)
	{
		DEBUG_LOG(("SCENARIO: frame %d: nobody is sitting in slot %d\n", action.frame, action.slot));
		return FALSE;
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

	if (action.action == SCENARIO_ACTION_POWER)
		return executePower( action, player, position );

	if (action.action == SCENARIO_ACTION_PRODUCE)
		return executeProduce( action, player );

	if (action.action == SCENARIO_ACTION_TALLY)
		return executeTally( action, player );

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

	if (TheGlobalData->m_scenarioFile.isEmpty())
		return;

	if (!theScenarioLoaded)
		loadScenario();

	while (theScenarioCursor < (Int)theScenarioActions.size()
				 && theScenarioActions[ theScenarioCursor ].frame <= now)
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
