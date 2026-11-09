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

// FILE: CushionMetrics.h /////////////////////////////////////////////////////////////////////////
// Desc:   How many frames of margin we have left, and when to spend some slowing down.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CUSHION_METRICS_H_
#define _CUSHION_METRICS_H_

#include "Lib/BaseType.h"

/* The "cushion" is how many frames ahead of the simulation a command arrived: the margin between
	 the network and the frame that needs it.  When it runs low the machine deliberately slows its
	 own logic rate - the self-slug - so the rest of the room can catch up before anybody stalls
	 outright.  That decision is worth getting right in both directions: slug when there is no
	 margin left and the room stutters instead of stalling, slug when there is and every player
	 pays for nothing. */

/** The margin, in frames, between the frame a command is for and the frame we are on.  Never
	  negative: a command whose frame has already gone by is no margin at all, not a negative one. */
Int frameCushion( UnsignedInt executionFrame, UnsignedInt currentFrame );

/* Frame numbers are UnsignedInt, so every "how far apart are these two frames" question in the
	 network layer is one subtraction away from four billion.  The helpers here take the difference
	 as a signed Int, which is the same bit pattern read the way the question meant it. */

/** Whether a frame is too far in the past to still be resendable.  A frame we have not reached
	  yet is not old - we simply have nothing for it.  "Past" means within half the frame counter's
	  range, which at 30 Hz is over four hundred days of play. */
Bool frameIsTooOldToResend( UnsignedInt currentFrame, UnsignedInt requestedFrame, Int framesToKeep );

/** Whether to slow our own logic rate.  A negative minimumCushion means no sample has been taken
	  yet, which is not the same as no margin - see the sentinel note in CushionMetrics.cpp. */
Bool shouldSelfSlug( Int minimumCushion, Int runAhead, UnsignedInt slackPercent );

/** The cushion, in frames, below which the self-slug engages. */
Int selfSlugThreshold( Int runAhead, UnsignedInt slackPercent );

enum
{
	/* The brake needs room to work.  NetworkRunAheadSlack is 10 %, and the run-ahead it is a
		 percentage of is small: MIN_RUNAHEAD is four frames, and computeRunAhead only clears that on
		 links slower than about 240 ms round trip.  Ten percent of four is nothing, and a brake that
		 engages with less than a frame of margin left is not a brake, it is the stall it was meant
		 to prevent.  Two frames was 66 ms at 30 Hz; logic runs at 60 now, so the same 66 ms is four.
		 Every frame count in this enum is doubled from its 30 Hz value for that reason. */
	SELFSLUG_MIN_THRESHOLD_FRAMES = 4,

	/* The fixed part of the run-ahead's safety margin.  The transit term computeRunAhead sizes the
		 window from is built out of *average* round trips, and an average is exceeded half the time;
		 the proportional slack on top of it is worth 0.4 frames on a LAN and one frame on a 300 ms
		 link, which is not a margin, it is a rounding error.  Two frames - 66 ms at 30 Hz - is the
		 allowance for the jitter the average hides, and it is what makes a low MIN_RUNAHEAD safe:
		 the links that need the window get more of it than the old flat floor of ten ever gave them,
		 and the links that do not stop paying for it.

		 Three since the router stopped being counted as a leg of the trip (now roomWorstLatency).  The
		 window that came out of that was the honest one, and at 200 to 300 ms of ping with 30 to 60 ms
		 of jitter two frames of allowance let the tail through: two players at 200 ms fell from 29
		 logic frames a second to 26 on a 5-frame window.  A LAN still sits on MIN_RUNAHEAD.  Three
		 30 Hz frames, 100 ms, is six at 60. */
	RUNAHEAD_JITTER_FRAMES = 6,

	/* The room never runs slower than this, whatever the metrics say: a sixth of normal speed. */
	ROOM_FRAME_RATE_FLOOR = 10,

	/* How far above the slowest reported rate the room is actually told to run.  This is EA's own
		 step - they applied it to the slowest player alone, "just in case they are able to" - given
		 to everybody, which is what lets the rate climb at all.  Ten percent, the same slack the
		 run-ahead is sized with. */
	ROOM_FRAME_RATE_PROBE_PERCENT = 10,
};

/* How far into the future the room schedules its commands.  Every machine runs the same logic
	 frame at the same time, so a command has to be on every machine before that frame arrives; the
	 run-ahead is how much head start it is given.  Too small and the room stalls waiting for a
	 packet that is still in flight, too large and every click is answered late.  The packet router
	 computes this for the whole room and broadcasts it, so it is one number everybody obeys. */

/* Every command goes straight from its author to each other player, so the longest trip in the
	 room is one hop over the worst link in it: half of the worst round trip.  Each player reports
	 its worst round trip to anyone, and the room's figure is the largest report, the router's own
	 included.  When every command went through the packet router the trip was two hops, worst
	 player to router and on to the second worst, and EA summed two round trips for it.

	 latencies and connected are indexed by slot. */
Real roomWorstLatency( const Real latencies[], const Bool connected[], Int numSlots );

/** The run-ahead, in frames.  roundTripSeconds is roomWorstLatency's, fps the rate the room has
	  settled on. */
Int computeRunAhead( Real roundTripSeconds, Int fps, UnsignedInt slackPercent,
										 Int minRunAhead, Int maxRunAhead );

/* A latency sample is the wall time between sending a frame's commands and the ack coming back, so
	 it measures the link only while both machines are running.  Let the logic stop - a lost packet,
	 a player alt-tabbing, a map that took a moment - and the same stopwatch keeps running, and the
	 stall is filed as round trip time.  It is not: it is the answer to a question the run-ahead is
	 not asking.  A real match recorded 1.79 s and then 7.64 s on a link whose measured srtt was
	 51 ms; the run-ahead sized on them went from its usual 5 frames to 29 and then 64 - 2.1 seconds
	 of input delay - and stayed there while the sample sat in the average.

	 So a sample past any plausible round trip is discarded down to the ceiling rather than trusted.
	 A link genuinely that slow is not playable anyway, and clamping costs nothing there; the point
	 is that a stall can no longer buy a permanent delay for everyone.

	 Half a second of round trip is past where this game is playable, and it is what the clamp
	 has to be worth: two players pinned at the ceiling still only buy 18 frames of
	 run-ahead - 0.6 s - against the 64 frames the raw samples bought.  A link genuinely slower
	 than this loses at most 33 ms of one-way window, which RUNAHEAD_JITTER_FRAMES already covers.
	 ponytail: one ceiling for every link.  The self-calibrating version is the connection's own
	 srtt + 4*rttvar (Connection.h has both), if a real link is ever found that this undersizes.

	 It was half a second until the owner said the rooms this fork is played in sit at 200 to 300 ms
	 of ping.  The sample is that ping plus the game's own share of the trip - a frame of send
	 grouping each way and the time until the next pass reads the socket - and a 300 ms ping with
	 60 ms of jitter measured 0.38 s on average with single samples past 0.5, so the ceiling was
	 already cutting into links people play on and would have undersized the window on every one of
	 them a little slower. */
const Real MAX_PLAUSIBLE_LATENCY_SECONDS = 0.8f;

/** Fold a raw round-trip measurement into the range a round trip can actually occupy. */
Real sanitizeLatencySample( Real seconds, Real ceilingSeconds );

/* The room's logic rate is set by the packet router to the slowest rate any player reports, and
	 every machine then paces its own logic on it (Network::timeForNewFrame).  The trap is that the
	 rate a player reports is the rate they *achieved*, and a player pinned at the room's rate
	 achieves exactly it - so the reported minimum can never rise above the rate that produced it,
	 and one player's two-second hitch used to hold the whole room at that speed for the rest of the
	 match.  The way out is to command a rate slightly above the measured minimum: whoever cannot
	 follow reports short and pins the room honestly, everybody else follows and the minimum climbs
	 with the room until it hits the limit. */

/** The slowest reported rate, brought inside the range the room is allowed to run at. */
Int settleRoomFrameRate( Int minFps, Int fpsLimit );

/** The rate to actually command: one probe step above the settled rate, never above the limit. */
Int probeRoomFrameRate( Int settledFps, Int fpsLimit );

/* Every machine keeps the same packet router fallback plan - the list of who relays for everybody,
	 in the order they take over - built from the shared slot list, so the succession is identical on
	 every machine without anyone having to agree about it at run time.  Walking that list is the one
	 place a disagreement would be fatal, and it is also where the walk ran off the end. */

/** The slot that takes over relaying after currentSlot, or maxSlots when there is nobody left.
	  fallback holds maxSlots entries, valid slots are below maxSlots, and anything else (the -1
	  padding an emptied entry is left with) ends the list. */
UnsignedInt nextPacketRouterSlot( const UnsignedInt *fallback, Int maxSlots, UnsignedInt currentSlot );

#endif // _CUSHION_METRICS_H_
