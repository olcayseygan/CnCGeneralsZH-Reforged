// The match clock (MatchClock.cpp): a steel tray at the top of the screen with the time the match has
// run and, where the match keeps a day and night, a bar of the cycle's phases with the sun or the moon
// riding it, the time left in the phase and the nights gone by.  Client only: it reads a state handed to
// it and writes nothing back.  MatchClock_gather fills that state from whichever clock the match keeps.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#pragma once

#ifndef MATCHCLOCK_H
#define MATCHCLOCK_H

#include "Lib/BaseType.h"

enum { MATCHCLOCK_PHASES = 4 };		///< day, dusk, night and dawn, in that order, as both clocks number them

struct MatchClockState
{
	UnsignedInt matchFrames;										///< logic frames since the match began
	UnsignedInt framesPerSecond;								///< what a second of the shown times is in frames
	Bool hasCycle;															///< FALSE: the clock alone, no bar
	Int phase;																	///< 0 day to 3 dawn
	Real night;																	///< 0 full day to 1 full night
	UnsignedInt intoPhase;											///< frames into the phase
	UnsignedInt phaseFrames[ MATCHCLOCK_PHASES ];		///< each phase's length, the bar's segments
	Int nightCount;															///< nights begun, the one under way counted; -1 shows none
	Bool bloodMoon;															///< this night's moon is a Blood Moon
};

/// whether the clock is drawn in this match at all
extern Bool MatchClock_wanted( void );

/// fill the state for that logic frame: the day and night box's cycle, else the Apocalypse director's, else the clock alone
extern void MatchClock_gather( UnsignedInt frame, MatchClockState &state );

/// draw it, its tray's top at that screen row
extern void MatchClock_draw( const MatchClockState &state, Int top );

#endif
