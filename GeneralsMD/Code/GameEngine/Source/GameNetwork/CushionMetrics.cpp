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

// FILE: CushionMetrics.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   How many frames of margin we have left, and when to spend some slowing down.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "GameNetwork/CushionMetrics.h"

#include <math.h>

Int frameCushion( UnsignedInt executionFrame, UnsignedInt currentFrame )
{
	/* EA wrote this as `UnsignedInt cushion = executionFrame - TheGameLogic->getFrame()`, which is
		 fine right up until a command arrives for a frame that has already run - a late relay, or a
		 command caught up with after the run-ahead shrank.  The subtraction then wraps to around
		 four billion, and the caller hands that to FrameMetrics::addCushion, whose parameter is Int:
		 4294967295 arrives as -1, which is exactly that class's "no sample yet" sentinel.  One late
		 command therefore erased the minimum cushion for the window. */
	Int diff = (Int)(executionFrame - currentFrame);		// signed difference, wraparound-safe
	if( diff < 0 )
		diff = 0;

	return diff;
}

Bool frameIsTooOldToResend( UnsignedInt currentFrame, UnsignedInt requestedFrame, Int framesToKeep )
{
	/* EA wrote this as `if ((TheGameLogic->getFrame() - FRAMES_TO_KEEP) > frame)`, an UnsignedInt
		 subtraction with nothing under it: for the first FRAMES_TO_KEEP frames of a match it wraps to
		 around four billion, which is greater than any frame anyone can ask for, so every resend
		 request in the opening seconds was answered with "this is too far in the past" and dropped.
		 A stall at the start of a game therefore could not be repaired by the mechanism that exists
		 to repair it, and had to wait for the retry and then the disconnect screen instead. */
	Int age = (Int)(currentFrame - requestedFrame);		// signed difference, wraparound-safe
	if( age < 0 )
		return FALSE;

	return age > framesToKeep;
}

Bool shouldSelfSlug( Int minimumCushion, Int runAhead, UnsignedInt slackPercent )
{
	/* Negative is FrameMetrics' "nothing measured yet", set by init() and never produced by a real
		 sample once frameCushion clamps.  It used to reach here as an UnsignedInt through
		 ConnectionManager::getMinimumCushion's return type, so it read as four billion frames of
		 margin - the largest cushion representable, from the state that knows the least. */
	if( minimumCushion < 0 )
		return FALSE;

	if( runAhead <= 0 )
		return FALSE;

	return minimumCushion < selfSlugThreshold( runAhead, slackPercent );
}

Int selfSlugThreshold( Int runAhead, UnsignedInt slackPercent )
{
	/* The threshold is the slack the run-ahead was given: once the margin has eaten into that, the
		 next hiccup is a stall rather than a wobble.  With a floor, because the slack is a
		 percentage of a run-ahead that is a handful of frames on the links most rooms are played on
		 - see SELFSLUG_MIN_THRESHOLD_FRAMES. */
	Int threshold = (Int)((runAhead * (Int)slackPercent) / 100);
	if( threshold < SELFSLUG_MIN_THRESHOLD_FRAMES )
		threshold = SELFSLUG_MIN_THRESHOLD_FRAMES;

	return threshold;
}

Real roomWorstLatency( const Real latencies[], const Bool connected[], Int numSlots )
{
	Real worst = 0.0f;
	for( Int slot = 0; slot < numSlots; ++slot )
	{
		if( connected[slot] && latencies[slot] > worst )
			worst = latencies[slot];
	}
	return worst;
}

Int computeRunAhead( Real roundTripSeconds, Int fps, UnsignedInt slackPercent,
										 Int minRunAhead, Int maxRunAhead )
{
	if( roundTripSeconds < 0.0f )
		roundTripSeconds = 0.0f;
	if( fps < 1 )
		fps = 1;			// the room rate is floored long before it gets here, but the division below is real

	/* roomWorstLatency() is the worst round trip in the room, so half of it is the one-way trip
		 a command has to survive.  Rounded up, where EA truncated: a 150 ms round trip truncates to
		 two frames - 66 ms - to cover 75 ms of wire, and a run-ahead shorter than the trip itself is
		 not a tight window, it is a stall on every command that uses it.  Nobody ever saw that
		 because the old floor of ten frames was larger than the formula's answer on every link
		 anyone plays on; lowering the floor is what makes the arithmetic underneath it matter. */
	Int frames = (Int)ceilf( (roundTripSeconds / 2.0f) * (Real)fps );

	// EA's proportional slack, and then the fixed allowance an average and a percentage cannot give
	frames += (Int)((frames * (Int)slackPercent) / 100);
	frames += RUNAHEAD_JITTER_FRAMES;

	if( frames < minRunAhead )
		frames = minRunAhead;

	if( frames > maxRunAhead )
		frames = maxRunAhead;			// don't let the run ahead get out of hand

	return frames;
}

Real sanitizeLatencySample( Real seconds, Real ceilingSeconds )
{
	if( ceilingSeconds < 0.0f )
		ceilingSeconds = 0.0f;

	if( !(seconds >= 0.0f) )
		return 0.0f;						// the clock went backwards, or a NaN off the wire; no information, not a fast link

	if( seconds > ceilingSeconds )
		return ceilingSeconds;

	return seconds;
}

Int settleRoomFrameRate( Int minFps, Int fpsLimit )
{
	if( minFps < ROOM_FRAME_RATE_FLOOR )
		minFps = ROOM_FRAME_RATE_FLOOR;

	if( minFps > fpsLimit )
		minFps = fpsLimit;

	return minFps;
}

Int probeRoomFrameRate( Int settledFps, Int fpsLimit )
{
	Int probed = (settledFps * (100 + ROOM_FRAME_RATE_PROBE_PERCENT)) / 100;

	/* Integer division eats the step below ten frames a second, and a step of zero is the latch
		 this whole function exists to break. */
	if( probed <= settledFps )
		probed = settledFps + 1;

	if( probed > fpsLimit )
		probed = fpsLimit;

	return probed;
}

UnsignedInt nextPacketRouterSlot( const UnsignedInt *fallback, Int maxSlots, UnsignedInt currentSlot )
{
	/* EA wrote this as `while ((index < (MAX_SLOTS-1)) && (fallback[index] != playerID)) ++index;`
		 followed by `++index; return fallback[index];`.  The loop stops at MAX_SLOTS-1 whether it found
		 anything or not, so the increment can leave index at MAX_SLOTS and the return reads one entry
		 past the end of the array.  In ConnectionManager that entry is m_localAddr, the local IP: the
		 "new packet router" comes out as a number around three billion, and the next metrics send
		 indexes m_connections[] with it.  Two ways in - the current router sitting in the last entry
		 of a full eight player plan, or not being in the plan at all - and the same walk is written
		 out twice, in getNextPacketRouterSlot and in disconnectPlayer. */
	Int index = 0;
	while( (index < maxSlots) && (fallback[index] != currentSlot) )
		++index;

	if( index >= maxSlots )
		return (UnsignedInt)maxSlots;		// not in the plan, so nothing follows it

	++index;
	if( index >= maxSlots )
		return (UnsignedInt)maxSlots;		// last in the plan, nobody left to take over

	UnsignedInt next = fallback[index];
	if( next >= (UnsignedInt)maxSlots )
		return (UnsignedInt)maxSlots;		// the -1 padding an emptied entry carries

	return next;
}
