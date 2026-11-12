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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameCommon.h ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  GameCommon.h
//
// Created:    Steven Johnson, October 2001
//
// Desc:			 This is a catchall header for some basic types and definitions
//							needed by various bits of the GameLogic/GameClient, but that
//							we haven't found a good place for yet. Hopefully this file
//							should go away someday, but for now is a convenient spot.
//
//-----------------------------------------------------------------------------

#pragma once

#ifndef _GAMECOMMON_H_
#define _GAMECOMMON_H_



#define DONT_ALLOW_DEBUG_CHEATS_IN_RELEASE ///< Take of the DONT to get cheats back in to release

//#define _CAMPEA_DEMO

// ----------------------------------------------------------------------------------------------
#include "Lib/BaseType.h"
#include <limits.h>

// ----------------------------------------------------------------------------------------------
#if defined(_INTERNAL) || defined(_DEBUG)
	#define DUMP_PERF_STATS
#else
	#define NO_DUMP_PERF_STATS
#endif

// ----------------------------------------------------------------------------------------------
enum
{
	LOGICFRAMES_PER_SECOND = 30,				///< the logic tick; the one place it is set. 30 or 60, everything else derives from it
	RETAIL_LOGICFRAMES_PER_SECOND = 30,	///< the rate every hard-coded frame count and per-frame amount in EA's code and data was written for
	MSEC_PER_SECOND = 1000
};
const Real LOGICFRAMES_PER_MSEC_REAL = (((Real)LOGICFRAMES_PER_SECOND) / ((Real)MSEC_PER_SECOND));
const Real MSEC_PER_LOGICFRAME_REAL = (((Real)MSEC_PER_SECOND) / ((Real)LOGICFRAMES_PER_SECOND));
const Real LOGICFRAMES_PER_SECONDS_REAL = (Real)LOGICFRAMES_PER_SECOND;
const Real SECONDS_PER_LOGICFRAME_REAL = 1.0f / LOGICFRAMES_PER_SECONDS_REAL;

// ----------------------------------------------------------------------------------------------
// Logic runs at the retail rate or twice it. At twice, a frame count written for 30 Hz is doubled,
// an amount added every frame is halved, an acceleration quartered, and a factor multiplied in every
// frame becomes its square root; at the retail rate every conversion is the identity. Each is exact
// in IEEE arithmetic (sqrt included), so all machines agree. Any other ratio would need a power
// for the factor, which is not bit-identical across machines, hence the assert.
enum { LOGICFRAMES_PER_RETAIL_FRAME = LOGICFRAMES_PER_SECOND / RETAIL_LOGICFRAMES_PER_SECOND };
static_assert( LOGICFRAMES_PER_SECOND == LOGICFRAMES_PER_RETAIL_FRAME * RETAIL_LOGICFRAMES_PER_SECOND
	&& ( LOGICFRAMES_PER_RETAIL_FRAME == 1 || LOGICFRAMES_PER_RETAIL_FRAME == 2 ),
	"the Retail conversions are exact only with logic at the retail rate or exactly twice it" );

// ----------------------------------------------------------------------------------------------
// Game speed: the logic still ticks LOGICFRAMES_PER_SECOND times a wall second, but each tick
// advances GAME_SPEED_PERCENT / 100 of a retail tick's game time. At 150 a game second is 20 ticks,
// so everything moves and builds half again as fast on the same wall clock. LOGICFRAMES_PER_SECOND
// keeps meaning wall ticks (pacing, render, audio, network, HUD); a duration in game time is
// seconds times LOGICFRAMES_PER_GAME_SECOND. Like the rate, a source constant: the exe CRC
// fingerprints it, and saves and replays carry it.
//
// The allowed percents are those where every conversion below rounds the same on every machine: a
// whole number of ticks to the game second, a per-frame factor raised to a power in eighths
// (multiplies and sqrtf, IEEE-exact), per-frame rates scaled by quarters (exact floats), integer
// frame counts rounded in integers, and a Real frame count of 2 retail frames staying above one
// tick, so a gain of 1/frames never reaches 1. Below 100 two (rate, speed) pairs would write the
// same ticks per game second into a save (30 Hz at 50 and 60 Hz at 100 both 60, which is also what a
// version 5 save is read as) and load each other's games, so 100 is the floor. That leaves 100, 125
// and 150.
#define GAME_SPEED_PERCENT 150
enum { LOGICFRAMES_PER_GAME_SECOND = LOGICFRAMES_PER_SECOND * 100 / GAME_SPEED_PERCENT };
static_assert( LOGICFRAMES_PER_GAME_SECOND * GAME_SPEED_PERCENT == LOGICFRAMES_PER_SECOND * 100
	&& ( GAME_SPEED_PERCENT * 8 ) % ( 100 * LOGICFRAMES_PER_RETAIL_FRAME ) == 0
	&& GAME_SPEED_PERCENT % 25 == 0
	&& GAME_SPEED_PERCENT >= 100 && GAME_SPEED_PERCENT < 200,
	"GAME_SPEED_PERCENT must be one of 100, 125, 150: only there do the conversions round alike everywhere and every (rate, speed) pair write its own save field" );
const Real LOGICFRAMES_PER_GAME_SECOND_REAL = (Real)LOGICFRAMES_PER_GAME_SECOND;
const Real GAME_SECONDS_PER_LOGICFRAME_REAL = 1.0f / LOGICFRAMES_PER_GAME_SECOND_REAL;
const Real GAME_MSEC_PER_LOGICFRAME_REAL = (((Real)MSEC_PER_SECOND) / ((Real)LOGICFRAMES_PER_GAME_SECOND));

// The Wall forms convert for the logic rate only: a frame count or rate tied to the wall clock
// (frame pacing, the network's run-ahead, -fps). Everything in game time takes the plain forms.
// A frame count past half the type's range is a sentinel (a "never" or "forever"), and stays one
constexpr Int ConvertRetailFramesWall( Int frames )
{
	if( frames > INT_MAX / LOGICFRAMES_PER_RETAIL_FRAME ) return INT_MAX;
	if( frames < INT_MIN / LOGICFRAMES_PER_RETAIL_FRAME ) return INT_MIN;
	return frames * LOGICFRAMES_PER_RETAIL_FRAME;
}
constexpr UnsignedInt ConvertRetailFramesWall( UnsignedInt frames )
{
	if( frames > UINT_MAX / LOGICFRAMES_PER_RETAIL_FRAME ) return UINT_MAX;
	return frames * LOGICFRAMES_PER_RETAIL_FRAME;
}
constexpr Real ConvertRetailFramesWall( Real frames ) { return frames * (Real)LOGICFRAMES_PER_RETAIL_FRAME; }

// Game-time frame counts: the wall conversion, then divided by the speed, rounded to the nearest
// tick in integers (half away from zero). A nonzero count never becomes 0, and a sentinel past half
// the range is left as the rate made it. At 100 each is exactly its Wall form.
constexpr Int ConvertRetailFrames( Int frames )
{
	const Int wall = ConvertRetailFramesWall( frames );
	if( wall > INT_MAX / 2 || wall < INT_MIN / 2 ) return wall;
	const Int64 n = (Int64)wall * 100;
	const Int scaled = (Int)( ( n >= 0 ? n + GAME_SPEED_PERCENT / 2 : n - GAME_SPEED_PERCENT / 2 ) / GAME_SPEED_PERCENT );
	return scaled != 0 ? scaled : ( wall > 0 ? 1 : wall < 0 ? -1 : 0 );
}
constexpr UnsignedInt ConvertRetailFrames( UnsignedInt frames )
{
	const UnsignedInt wall = ConvertRetailFramesWall( frames );
	if( wall > UINT_MAX / 2 ) return wall;
	const UnsignedInt scaled = (UnsignedInt)( ( (UnsignedInt64)wall * 100 + GAME_SPEED_PERCENT / 2 ) / GAME_SPEED_PERCENT );
	return scaled != 0 || wall == 0 ? scaled : 1;
}
// The ratio 100 * R / P is a constant folded once, so every target rounds it and the multiply the
// same way; it is exact only at 100 (R itself), while at 125 0.8f and 1.6f are the nearest floats,
// not the true ratios. A count of at least one tick stays at least one tick.
constexpr Real ConvertRetailFrames( Real frames )
{
	const Real wall = ConvertRetailFramesWall( frames );
	const Real scaled = frames * ( (Real)( 100 * LOGICFRAMES_PER_RETAIL_FRAME ) / (Real)GAME_SPEED_PERCENT );
	const Real least = wall < 1.0f ? wall : 1.0f;
	return scaled < least ? least : scaled;
}
// A multiplier in frames rather than a count (frames of prone time per point of damage, frames of
// velocity to lead a target by): scaled like any duration, with no one-tick floor, which would make
// a ratio between 1 and the speed's share longer in game time than retail's.  At 100 it is the Wall form.
constexpr Real ConvertRetailFrameRatio( Real framesPerUnit )
{
	return framesPerUnit * ( (Real)( 100 * LOGICFRAMES_PER_RETAIL_FRAME ) / (Real)GAME_SPEED_PERCENT );
}

// An amount added every frame scales by speed / rate, an acceleration by its square. With the
// percent a multiple of 25 both factors are exact floats (1, 1/2, 1/4, 5/4, 25/16 ...), and x * 1.0f
// is x, so at 100 these give what a literal 0.5f and 0.25f (or no conversion) would.
constexpr Real ConvertRetailPerFrameRate( Real perFrame ) { return perFrame * ( (Real)GAME_SPEED_PERCENT / (Real)( 100 * LOGICFRAMES_PER_RETAIL_FRAME ) ); }
constexpr Real ConvertRetailPerFrameSquared( Real perFrameSqr ) { return perFrameSqr * ( (Real)( GAME_SPEED_PERCENT * GAME_SPEED_PERCENT ) / (Real)( 10000 * LOGICFRAMES_PER_RETAIL_FRAME * LOGICFRAMES_PER_RETAIL_FRAME ) ); }

// A factor multiplied in every frame is raised to speed / rate, a power in eighths: the whole part
// by multiplies, the rest as f^(1/2), f^(1/4), f^(1/8) by repeated sqrtf, always in this order.
// At 100 and the retail rate it is the factor untouched, at twice the rate sqrtf of it as before.
enum { GAME_FACTOR_EXPONENT_EIGHTHS = GAME_SPEED_PERCENT * 8 / ( 100 * LOGICFRAMES_PER_RETAIL_FRAME ) };
inline Real ConvertRetailPerFrameFactor( Real factor )
{
	if( GAME_FACTOR_EXPONENT_EIGHTHS == 8 ) return factor;
	const Real f = factor > 0.0f ? factor : 0.0f;
	Real result = 1.0f;
	for( Int i = 0; i < GAME_FACTOR_EXPONENT_EIGHTHS / 8; ++i )
		result *= f;
	Real root = f;
	for( Int bit = 4; bit >= 1; bit /= 2 )
	{
		root = sqrtf( root );
		if( GAME_FACTOR_EXPONENT_EIGHTHS & bit )
			result *= root;
	}
	return result;
}

// ----------------------------------------------------------------------------------------------
// The speeds a skirmish or a mission can be played at, in percent of retail's pace.  A choice is wall
// pacing only: GameEngine_framesPerSecondForSpeed turns it into logic ticks a wall second (20, 25, 30
// and 40 at 30 Hz and GAME_SPEED_PERCENT 150), and every tick simulates the same game time at all of
// them, so the CRC stream does not change with it.  The native tick, GAME_SPEED_PERCENT, is the
// default and the only speed a network game runs.  -fps and a replay's speed keys do not pass here.
enum { PLAYER_GAME_SPEED_PERCENT_DEFAULT = GAME_SPEED_PERCENT };
static const Int PLAYER_GAME_SPEED_PERCENTS[] = { 100, 125, 150, 200 };
enum { PLAYER_GAME_SPEED_COUNT = sizeof( PLAYER_GAME_SPEED_PERCENTS ) / sizeof( PLAYER_GAME_SPEED_PERCENTS[ 0 ] ) };
// the allowed percent nearest the one asked for, the lower on a tie
inline Int ClampPlayerGameSpeedPercent( Int percent )
{
	Int best = PLAYER_GAME_SPEED_PERCENTS[ 0 ];
	for( Int i = 1; i < PLAYER_GAME_SPEED_COUNT; ++i )
	{
		const Int candidate = PLAYER_GAME_SPEED_PERCENTS[ i ];
		const Int bestGap = best > percent ? best - percent : percent - best;
		const Int gap = candidate > percent ? candidate - percent : percent - candidate;
		if( gap < bestGap )
			best = candidate;
	}
	return best;
}

// ----------------------------------------------------------------------------------------------
// note that this returns a REAL value, not an int... most callers will want to
// call ceil() on the result, so that partial frames get converted to full frames!
// These four take game time (data durations and speeds); at GAME_SPEED_PERCENT 100 each is the
// wall-rate expression it always was, bit for bit.
inline Real ConvertDurationFromMsecsToFrames(Real msec)
{
	return (msec * (((Real)LOGICFRAMES_PER_GAME_SECOND) / ((Real)MSEC_PER_SECOND)));
}

// ----------------------------------------------------------------------------------------------
inline Real ConvertVelocityInSecsToFrames(Real distPerMsec)
{
	// this looks wrong, but is the correct conversion factor.
	return (distPerMsec * GAME_SECONDS_PER_LOGICFRAME_REAL);
}

// ----------------------------------------------------------------------------------------------
inline Real ConvertAccelerationInSecsToFrames(Real distPerSec2)
{
	// this looks wrong, but is the correct conversion factor.
	const Real SEC_PER_LOGICFRAME_SQR = (GAME_SECONDS_PER_LOGICFRAME_REAL * GAME_SECONDS_PER_LOGICFRAME_REAL);
	return (distPerSec2 * SEC_PER_LOGICFRAME_SQR);
}

// ----------------------------------------------------------------------------------------------
inline Real ConvertAngularVelocityInDegreesPerSecToRadsPerFrame(Real degPerSec)
{
	const Real RADS_PER_DEGREE = PI / 180.0f;
	return (degPerSec * (GAME_SECONDS_PER_LOGICFRAME_REAL * RADS_PER_DEGREE));
}

// ----------------------------------------------------------------------------------------------
// Every Player takes a seat: the neutral player at 0, the map's own sides (civilian and the like),
// one per occupied game slot (MAX_SLOTS, 32) and the replay observer.  48 leaves the map fourteen sides
// beside a full lobby.  Every per-player array in logic is this long, a PartitionCell's among them,
// so it is not free; PlayerMaskType below caps it at 64.
enum
{
	MAX_PLAYER_COUNT = 48											///< max number of Players.
};

//
// A player index that arrives from outside this machine - off the wire, or out of a replay file -
// is only a number, and nothing on the way in bounds it.  Everything that turns one into a Player
// asks this first; a number that fails it names nobody and the command carrying it is dropped.
//
inline Bool isValidPlayerIndex( Int playerIndex )
{
	return playerIndex >= 0 && playerIndex < MAX_PLAYER_COUNT;
}

// ----------------------------------------------------------------------------------------------
/**
	a bitmask that can uniquely represent each player.
*/
typedef UnsignedInt64 PlayerMaskType;
const PlayerMaskType PLAYERMASK_ALL = ~(PlayerMaskType)0;
const PlayerMaskType PLAYERMASK_NONE = 0x0;
static_assert( MAX_PLAYER_COUNT <= sizeof( PlayerMaskType ) * 8, "PlayerMaskType needs a bit for every player" );

/// The one bit of a player index.  Shift a PlayerMaskType, never an Int: 1 << 32 is undefined.
inline PlayerMaskType playerIndexToMask( Int playerIndex )
{
	return (PlayerMaskType)1 << playerIndex;
}

// ----------------------------------------------------------------------------------------------
enum 
{ 
	MAX_GLOBAL_GENERAL_TYPES = 9,		///< number of playable General Types, not including the boss)
	
	/// The start of the playable global generals playertemplates
	GLOBAL_GENERAL_BEGIN = 5,
	
	/// The end of the playable global generals
	GLOBAL_GENERAL_END = (GLOBAL_GENERAL_BEGIN + MAX_GLOBAL_GENERAL_TYPES - 1)
};

//-------------------------------------------------------------------------------------------------
enum GameDifficulty
{
	DIFFICULTY_EASY,
	DIFFICULTY_NORMAL,
	DIFFICULTY_HARD,

	DIFFICULTY_COUNT
};

//-------------------------------------------------------------------------------------------------
enum PlayerType
{
	PLAYER_HUMAN,				///< player is human-controlled
	PLAYER_COMPUTER,		///< player is computer-controlled

	PLAYERTYPE_COUNT
};

//-------------------------------------------------------------------------------------------------
/// A PartitionCell can be one of three states for Shroud
enum CellShroudStatus
{
	CELLSHROUD_CLEAR,
	CELLSHROUD_FOGGED,
	CELLSHROUD_SHROUDED,

	CELLSHROUD_COUNT
};

//-------------------------------------------------------------------------------------------------
/// Since an object can take up more than a single PartitionCell, this is a status that applies to the whole Object
enum ObjectShroudStatus
{
	OBJECTSHROUD_INVALID,				///< indeterminate state, will recompute
	OBJECTSHROUD_CLEAR,					///< object is not shrouded at all (ie, completely visible)
	OBJECTSHROUD_PARTIAL_CLEAR,	///< object is partly clear (rest is shroud or fog)
	OBJECTSHROUD_FOGGED,				///< object is completely fogged
	OBJECTSHROUD_SHROUDED,			///< object is completely shrouded
	OBJECTSHROUD_INVALID_BUT_PREVIOUS_VALID,			///< indeterminate state, will recompute, BUT previous status is valid, don't reset (used for save/load)

	OBJECTSHROUD_COUNT
};

//-------------------------------------------------------------------------------------------------
enum GuardMode
{
	GUARDMODE_NORMAL,
	GUARDMODE_GUARD_WITHOUT_PURSUIT,	// no pursuit out of guard area
	GUARDMODE_GUARD_FLYING_UNITS_ONLY	// ignore nonflyers
};

// ---------------------------------------------------
enum 
{ 
	NEVER				= 0,
	FOREVER			= 0x3fffffff			// (we use 0x3fffffff so that we can add offsets and not overflow...
																//		at 30fps we're still pretty safe!)
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

/// Veterancy level define needed by several files that don't need the full Experience code.
// NOTE NOTE NOTE: Keep TheVeterencyNames in sync with these.
enum VeterancyLevel
{
	LEVEL_REGULAR = 0,
	LEVEL_VETERAN,
	LEVEL_ELITE,
	LEVEL_HEROIC,

	LEVEL_COUNT,
	LEVEL_INVALID,

	LEVEL_FIRST = LEVEL_REGULAR,
	LEVEL_LAST = LEVEL_HEROIC
};

// TheVeterancyNames is defined in GameCommon.cpp
extern const char *TheVeterancyNames[]; 

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
enum CommandSourceType : Int 
{ 

	CMD_FROM_PLAYER = 0, 
	CMD_FROM_SCRIPT, 
	CMD_FROM_AI,
	CMD_FROM_DOZER,							// Special rare command when the dozer originates a command to attack a mine. Mines are not ai-attackable, and it seems deceitful for the dozer to generate a player or script command. jba.
	CMD_DEFAULT_SWITCH_WEAPON,	// Special case: A weapon that can be chosen -- this is the default case (machine gun vs flashbang).

};		///< the source of a command

//-------------------------------------------------------------------------------------------------
enum AbleToAttackType
{
	_ATTACK_FORCED			= 0x01,
	_ATTACK_CONTINUED		= 0x02,
	_ATTACK_TUNNELNETWORK_GUARD = 0x04,

	/**
		can we attack if this is a new target?
	*/
	ATTACK_NEW_TARGET = (0),

	/**
		can we attack if this is a new target, via force-fire?
		(The only current difference between this and ATTACK_NEW_TARGET is that disguised units
		are force-attackable even when stealthed.)
	*/
	ATTACK_NEW_TARGET_FORCED= (_ATTACK_FORCED),	

	/**
		can we attack if this is continuation of an existing attack?
		(The only current difference between this and ATTACK_NEW_TARGET is you are allowed to follow
		immobile shrouded units into the fog)
	*/
	ATTACK_CONTINUED_TARGET = (_ATTACK_CONTINUED),

	/**
		can we attack if this is continuation of an existing attack?
		(The only current difference between this and ATTACK_NEW_TARGET is you are allowed to follow
		immobile shrouded units into the fog)
	*/
	ATTACK_CONTINUED_TARGET_FORCED = (_ATTACK_FORCED | _ATTACK_CONTINUED),

	/**
		Special case that bypasses some of the checks for units guarding from within tunnel networks!
		For example, a unit inside couldn't normally see outside and would fail.
	*/
	ATTACK_TUNNEL_NETWORK_GUARD = (_ATTACK_TUNNELNETWORK_GUARD)
	
};

//-------------------------------------------------------------------------------------------------
inline Bool isForcedAttack(AbleToAttackType t)
{
	return (((Int)t) & _ATTACK_FORCED) != 0;
}

//-------------------------------------------------------------------------------------------------
inline Bool isContinuedAttack(AbleToAttackType t)
{
	return (((Int)t) & _ATTACK_CONTINUED) != 0;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

typedef UnsignedInt VeterancyLevelFlags;

const VeterancyLevelFlags VETERANCY_LEVEL_FLAGS_ALL = 0xffffffff;
const VeterancyLevelFlags VETERANCY_LEVEL_FLAGS_NONE = 0x00000000;

/* A level's bit in VeterancyLevelFlags: bit (level - 1), as EA wrote it, the shift count taken modulo 32.
	 LEVEL_REGULAR is 0, so EA's `1UL << (dt - 1)` shifted by -1.  Built as it shipped, with a 32-bit
	 unsigned long and x86's shl reading the count's low five bits, that is bit 31, which ALL includes and
	 "+REGULAR" sets.  With a 64-bit unsigned long (macOS, Linux) it was bit 63, outside the flags, so no
	 die module with default flags ever ran for a regular unit.  The mask gives Windows' bit everywhere. */
inline VeterancyLevelFlags veterancyLevelFlagBit(VeterancyLevel dt)
{
	return (VeterancyLevelFlags)1 << (((Int)dt - 1) & 31);
}

inline Bool getVeterancyLevelFlag(VeterancyLevelFlags flags, VeterancyLevel dt)
{
	return (flags & veterancyLevelFlagBit(dt)) != 0;
}

inline VeterancyLevelFlags setVeterancyLevelFlag(VeterancyLevelFlags flags, VeterancyLevel dt)
{
	return (flags | veterancyLevelFlagBit(dt));
}

inline VeterancyLevelFlags clearVeterancyLevelFlag(VeterancyLevelFlags flags, VeterancyLevel dt)
{
	return (flags & ~veterancyLevelFlagBit(dt));
}

// ----------------------------------------------------------------------------------------------
#define BOGUSPTR(p) ((((unsigned int)(p)) & 1) != 0)

// ----------------------------------------------------------------------------------------------
#define MAKE_DLINK_HEAD(OBJCLASS, LISTNAME)																						\
public:																																								\
	inline DLINK_ITERATOR<OBJCLASS> iterate_##LISTNAME() const													\
	{																																										\
		DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));	\
		return DLINK_ITERATOR<OBJCLASS>(m_dlinkhead_##LISTNAME.m_head, &OBJCLASS::dlink_next_##LISTNAME);	\
	}																																										\
	inline OBJCLASS *getFirstItemIn_##LISTNAME() const																	\
	{																																										\
		DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));	\
		return m_dlinkhead_##LISTNAME.m_head;																							\
	}																																										\
	inline Bool isInList_##LISTNAME(OBJCLASS* o) const																	\
	{																																										\
		DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));	\
		return o->dlink_isInList_##LISTNAME(&m_dlinkhead_##LISTNAME.m_head);							\
	}																																										\
	inline void prependTo_##LISTNAME(OBJCLASS* o)																				\
	{																																										\
		DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));	\
		if (!isInList_##LISTNAME(o))																											\
			o->dlink_prependTo_##LISTNAME(&m_dlinkhead_##LISTNAME.m_head);									\
	}																																										\
	inline void removeFrom_##LISTNAME(OBJCLASS* o)																			\
	{																																										\
		DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));	\
		if (isInList_##LISTNAME(o))																												\
			o->dlink_removeFrom_##LISTNAME(&m_dlinkhead_##LISTNAME.m_head);									\
	}																																										\
	typedef void (*RemoveAllProc_##LISTNAME)(OBJCLASS* o);															\
	inline void removeAll_##LISTNAME(RemoveAllProc_##LISTNAME p = NULL)									\
	{																																										\
		while (m_dlinkhead_##LISTNAME.m_head)																							\
		{																																									\
			DEBUG_ASSERTCRASH(!BOGUSPTR(m_dlinkhead_##LISTNAME.m_head), ("bogus head ptr"));\
			OBJCLASS *tmp = m_dlinkhead_##LISTNAME.m_head;																	\
			removeFrom_##LISTNAME(tmp);																											\
			if (p) (*p)(tmp);																																\
		}																																									\
	}																																										\
	inline void reverse_##LISTNAME()																										\
	{																																										\
		OBJCLASS* cur = m_dlinkhead_##LISTNAME.m_head;																		\
		OBJCLASS* prev = NULL;																														\
		while (cur)																																				\
		{																																									\
			OBJCLASS* originalNext = cur->dlink_next_##LISTNAME();													\
			cur->dlink_swapLinks_##LISTNAME();																							\
			prev = cur;																																			\
			cur = originalNext;																															\
		}																																									\
		m_dlinkhead_##LISTNAME.m_head = prev;																							\
	}																																										\
private:																																							\
	/* a trick: init head to zero */																										\
	struct DLINKHEAD_##LISTNAME																													\
	{																																										\
	public:																																							\
		OBJCLASS* m_head;																																	\
		inline DLINKHEAD_##LISTNAME() :																										\
			m_head(0) { }																																		\
		inline ~DLINKHEAD_##LISTNAME()																										\
			{ DEBUG_ASSERTCRASH(!m_head,("destroying dlinkhead still in a list " #LISTNAME)); }				\
	};																																									\
	DLINKHEAD_##LISTNAME m_dlinkhead_##LISTNAME;	

// ----------------------------------------------------------------------------------------------
#define MAKE_DLINK(OBJCLASS, LISTNAME)	\
public:																	\
	OBJCLASS* dlink_prev_##LISTNAME() const { return m_dlink_##LISTNAME.m_prev; }										\
	OBJCLASS* dlink_next_##LISTNAME() const	{ return m_dlink_##LISTNAME.m_next; }										\
	void dlink_swapLinks_##LISTNAME()																									\
	{																																													\
		OBJCLASS* originalNext = m_dlink_##LISTNAME.m_next;																			\
		m_dlink_##LISTNAME.m_next = m_dlink_##LISTNAME.m_prev;																	\
		m_dlink_##LISTNAME.m_prev = originalNext;																								\
	}																																													\
	Bool dlink_isInList_##LISTNAME(OBJCLASS* const* pListHead) const										\
	{																																													\
		DEBUG_ASSERTCRASH(!BOGUSPTR(*pListHead) && !BOGUSPTR(m_dlink_##LISTNAME.m_next) && !BOGUSPTR(m_dlink_##LISTNAME.m_prev), ("bogus ptrs")); \
		return *pListHead == this || m_dlink_##LISTNAME.m_prev || m_dlink_##LISTNAME.m_next;		\
	}																																													\
	void dlink_prependTo_##LISTNAME(OBJCLASS** pListHead)															\
	{																																													\
		DEBUG_ASSERTCRASH(!dlink_isInList_##LISTNAME(pListHead), ("already in list " #LISTNAME));					\
		DEBUG_ASSERTCRASH(!BOGUSPTR(*pListHead) && !BOGUSPTR(m_dlink_##LISTNAME.m_next) && !BOGUSPTR(m_dlink_##LISTNAME.m_prev), ("bogus ptrs")); \
		m_dlink_##LISTNAME.m_next = *pListHead;																									\
		if (*pListHead)																																					\
			(*pListHead)->m_dlink_##LISTNAME.m_prev = this;																				\
		*pListHead = this;																																			\
		DEBUG_ASSERTCRASH(!BOGUSPTR(*pListHead) && !BOGUSPTR(m_dlink_##LISTNAME.m_next) && !BOGUSPTR(m_dlink_##LISTNAME.m_prev), ("bogus ptrs")); \
	}																																													\
	void dlink_removeFrom_##LISTNAME(OBJCLASS** pListHead)															\
	{																																													\
		DEBUG_ASSERTCRASH(dlink_isInList_##LISTNAME(pListHead), ("not in list" #LISTNAME));			\
		DEBUG_ASSERTCRASH(!BOGUSPTR(*pListHead) && !BOGUSPTR(m_dlink_##LISTNAME.m_next) && !BOGUSPTR(m_dlink_##LISTNAME.m_prev), ("bogus ptrs")); \
		if (m_dlink_##LISTNAME.m_next)																													\
			m_dlink_##LISTNAME.m_next->m_dlink_##LISTNAME.m_prev = m_dlink_##LISTNAME.m_prev;			\
		if (m_dlink_##LISTNAME.m_prev)																													\
			m_dlink_##LISTNAME.m_prev->m_dlink_##LISTNAME.m_next = m_dlink_##LISTNAME.m_next;			\
		else																																										\
			*pListHead = m_dlink_##LISTNAME.m_next;																								\
		m_dlink_##LISTNAME.m_prev = 0;																													\
		m_dlink_##LISTNAME.m_next = 0;																													\
		DEBUG_ASSERTCRASH(!BOGUSPTR(*pListHead) && !BOGUSPTR(m_dlink_##LISTNAME.m_next) && !BOGUSPTR(m_dlink_##LISTNAME.m_prev), ("bogus ptrs")); \
	}																																													\
private:																\
	/* a trick: init links to zero */			\
	struct DLINK_##LISTNAME								\
	{																			\
	public:																\
		OBJCLASS* m_prev;										\
		OBJCLASS* m_next;										\
		inline DLINK_##LISTNAME() :					\
			m_prev(0), m_next(0) { }					\
		inline ~DLINK_##LISTNAME()					\
			{ DEBUG_ASSERTCRASH(!m_prev && !m_next,("destroying dlink still in a list "  #LISTNAME)); } \
	};																		\
	DLINK_##LISTNAME m_dlink_##LISTNAME;	

// ------------------------------------------------------------------------
// this is the weird C++ syntax for "call pointer-to-member-function"... see C++ FAQ LITE for details.
#define callMemberFunction(object,ptrToMember)  ((object).*(ptrToMember)) 

// ------------------------------------------------------------------------
template<class OBJCLASS>
class DLINK_ITERATOR
{
public:
	// this is the weird C++ syntax for "pointer-to-member-function"
	typedef OBJCLASS* (OBJCLASS::*GetNextFunc)() const;	
private:
	OBJCLASS* m_cur;
	GetNextFunc m_getNextFunc;	// this is the weird C++ syntax for "pointer-to-member-function"
public:
	DLINK_ITERATOR(OBJCLASS* cur, GetNextFunc getNextFunc) : m_cur(cur), m_getNextFunc(getNextFunc) 
	{ 
	}
	
	void advance()
	{ 
		if (m_cur)
			m_cur = callMemberFunction(*m_cur, m_getNextFunc)();
	}
	
	Bool done() const
	{ 
		return m_cur == NULL; 
	}

	OBJCLASS* cur() const 
	{ 
		return m_cur; 
	}

};

// ------------------------------------------------------------------------

enum WhichTurretType
{
	TURRET_INVALID = -1,

	TURRET_MAIN = 0,
	TURRET_ALT,

	MAX_TURRETS
};

// ------------------------------------------------------------------------
// this normalizes an angle to the range -PI...PI.
extern Real normalizeAngle(Real angle);

// ------------------------------------------------------------------------
// this returns the difference between a1 and a2, normalized.
inline Real stdAngleDiff(Real a1, Real a2)
{
	return normalizeAngle(a1 - a2);
}

// ------------------------------------------------------------------------
// NOTE NOTE NOTE: Keep TheRelationShipNames in sync with this enum
enum Relationship
{
	ENEMIES = 0,
	NEUTRAL,
	ALLIES
};


// TheRelationShipNames is defined in Common/GameCommon.cpp
extern const char *TheRelationshipNames[];

// ------------------------------------------------------------------------
/** strtoul( text, NULL, 10 ) as Windows answers it, where unsigned long is 32 bits: a number past
	* 0xFFFFFFFF is 0xFFFFFFFF, and a negative one is its magnitude negated modulo 2^32.  A 64-bit
	* unsigned long keeps the low 32 bits of a big number instead, so text from the network, a replay
	* or a preferences file ("SC=4294967296") would give another amount off Windows. */
#if defined(_WIN32)
inline UnsignedInt strtoulAsWindows( const char *text )
{
	return strtoul( text, NULL, 10 );
}
#else
inline UnsignedInt strtoulAsWindows( const char *text )
{
	while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
		++text;
	Bool negative = FALSE;
	if (*text == '+' || *text == '-')
		negative = (*text++ == '-');
	unsigned long long magnitude = 0;
	for (; *text >= '0' && *text <= '9'; ++text)
	{
		magnitude = magnitude * 10 + (unsigned long long)(*text - '0');
		if (magnitude > 0xFFFFFFFFull)
			return 0xFFFFFFFFu;			// out of range, whatever the sign: ULONG_MAX
	}
	return negative ? 0u - (UnsignedInt)magnitude : (UnsignedInt)magnitude;
}
#endif

#endif // _GAMECOMMON_H_

