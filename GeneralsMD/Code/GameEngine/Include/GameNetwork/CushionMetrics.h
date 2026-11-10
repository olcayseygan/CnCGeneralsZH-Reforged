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

/* Each frame's arrival cushion is measured once, by the arrival that completes it.  EA measured every
	 arrival for a frame that was already complete, and every synchronized command goes out twice more,
	 30 ms apart, until it is acked (CONNECTION_REDUNDANT_COPIES), so on a 200 ms link the last copy set
	 the minimum four frames under the real margin and the room ran self-slugged on 80 % of its frames.
	 A high-water mark is not enough either: a frame whose first FRAMEINFO was lost completes after the
	 one behind it, and it is exactly the late one.  So each frame in flight keeps its own mark, a ring
	 slot holding the frame number it last measured. */
enum { CUSHION_RING_FRAMES = 512 };

/** Whether an arrival for frame may measure its cushion: not yet measured, not in the past, and no
	  further ahead than maxAhead (a peer's far-future frame would alias another frame's slot).  The
	  caller still asks whether the frame is complete, then marks it with markCushionMeasured. */
Bool arrivalMeasuresCushion( const UnsignedInt measured[CUSHION_RING_FRAMES], UnsignedInt frame,
														 UnsignedInt currentFrame, Int maxAhead );
void markCushionMeasured( UnsignedInt measured[CUSHION_RING_FRAMES], UnsignedInt frame );

/** Whether to slow our own logic rate.  A negative minimumCushion means no sample has been taken
	  yet, which is not the same as no margin - see the sentinel note in CushionMetrics.cpp. */
Bool shouldSelfSlug( Int minimumCushion, Int runAhead, UnsignedInt slackPercent );

/** The cushion, in frames, below which the self-slug engages. */
Int selfSlugThreshold( Int runAhead, UnsignedInt slackPercent );

enum
{
	/* The brake's floor.  NetworkRunAheadSlack is 10 %, and the run-ahead it is a percentage of is a
		 handful of frames, so ten percent of it rounds to nothing, and a threshold of nothing never
		 fires.  One frame: the cushion is the frame a packet completed minus the frame about to run, so
		 it counts the whole frames of real time left, and 0 is still up to a frame early, the last
		 reading before the stall.  It was two, and four at 30 Hz before that.  A threshold is not a
		 margin: the brake only pulls the machine that runs ahead back until its cushion reaches the
		 threshold, so in a room that started out of step the leader sits right on it, and every frame
		 above the threshold that is not phase margin goes to the follower as cushion nobody needs. */
	SELFSLUG_MIN_THRESHOLD_FRAMES = 1,

	/* A frame of headroom above the brake that no round trip measures.  Two machines' logic clocks
		 are not in step, a frame runs and is announced on a render pass, and a starved or hitching
		 machine is late on every link at once, so a packet on time by the wire can still land a frame
		 late against the other machine's tick.  Two copies on loopback without it, at a run-ahead of 2,
		 ran clean on an idle CPU and slowed themselves on 17 to 18 % of frames, at 58 fps, on a
		 saturated one. */
	RUNAHEAD_PHASE_MARGIN_FRAMES = 1,

	/* The room never runs slower than this, whatever the metrics say: a sixth of normal speed. */
	ROOM_FRAME_RATE_FLOOR = 10,

	/* How far above the slowest reported rate the room is actually told to run.  This is EA's own
		 step - they applied it to the slowest player alone, "just in case they are able to" - given
		 to everybody, which is what lets the rate climb at all.  Ten percent, the same slack the
		 run-ahead is sized with. */
	ROOM_FRAME_RATE_PROBE_PERCENT = 10,
};

/* The jitter part of the run-ahead's margin, in round-trip deviations.  The transit term is an
	 average, and an average is exceeded half the time; by how much is the link's spread, which every
	 connection already measures as its Jacobson/Karels rttvar, the mean deviation of the round trip.
	 The margin used to be a flat six frames, 100 ms, on every link.

	 Why one.  The brake looks at the smallest cushion of the last ten frames, and each of those is the
	 later of the other players' packets, so covering every arrival would take the largest one-way
	 excess of about twenty: about 1.4 rttvar for uniform noise split evenly between the directions,
	 1.7 for normal noise, 2.3 for noise all on one direction.  It is not worth covering.  Past the
	 margin a late packet only engages the brake, whose frames are still in the window, and the
	 brake costs a few tenths of a frame a second while a frame of window costs every click 17 ms.
	 Measured on three copies at 200 ms of ping with 30 ms of jitter (rttvar 22 to 29 ms), counting the
	 FRAMEINFO lag: a 5-frame margin engaged the brake on 6 to 9 % of frames at 58.4 to 58.9 fps, 6
	 frames on 4 to 6 % at 59.0, 7 to 8 frames on 0 to 3 % at 59.1 to 59.2.  One deviation is the 6. */
const Real RUNAHEAD_JITTER_DEVIATIONS = 1.0f;

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

/** The run-ahead, in frames.  roundTripSeconds is roomWorstLatency's, jitterSeconds the worst rttvar
	  any player reported (roomWorstLatency over those), fps the rate the room has settled on.

	  Frame f + run-ahead's FRAMEINFO leaves the moment frame f has run, so a packet trip frames on the
	  wire lands with run-ahead - trip frames of real time left, and the cushion the brake reads is the
	  whole frames of that.  So the window is the one-way trip plus RUNAHEAD_JITTER_DEVIATIONS of
	  jitter, rounded past to the next whole frame, then EA's proportional slack, the phase margin and
	  the brake's threshold.  It used to round the trip up and add a frame for the FRAMEINFO on top, and
	  the jitter rounded up beside it, which counted the same frame two or three times whenever the
	  trip was not a whole number of frames: loopback ran at 5 where 3 holds.  The brake's frame is in
	  the window because the brake fires whenever the cushion drops below it.  The slack stays for the
	  reason it was there, the frame rate probe - the window is counted at the settled rate and played
	  at up to 10 % above it - and it is nothing below about a 300 ms round trip anyway. */
Int computeRunAhead( Real roundTripSeconds, Real jitterSeconds, Int fps, UnsignedInt slackPercent,
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
	 than this loses at most 33 ms of one-way window, which the jitter margin already covers.
	 ponytail: one ceiling for every link.  The self-calibrating version is the connection's own
	 srtt + 4*rttvar (Connection.h has both), if a real link is ever found that this undersizes.

	 It was half a second until the owner said the rooms this fork is played in sit at 200 to 300 ms
	 of ping.  The sample is that ping plus the game's own share of the trip - a frame of send
	 grouping each way and the time until the next pass reads the socket - and a 300 ms ping with
	 60 ms of jitter measured 0.38 s on average with single samples past 0.5, so the ceiling was
	 already cutting into links people play on and would have undersized the window on every one of
	 them a little slower. */
const Real MAX_PLAUSIBLE_LATENCY_SECONDS = 0.8f;

/* The same guard for the jitter figure.  A stall filed as a round trip moves rttvar by a quarter of
	 the sample, so the 1.79 s above would read as 450 ms of jitter for the half second it takes to
	 decay, and buy the room 27 frames of margin.  A mean deviation of 100 ms is round trips swinging a
	 quarter of a second either side, past where a match is playable, and still only 6 frames. */
const Real MAX_PLAUSIBLE_JITTER_SECONDS = 0.1f;

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
