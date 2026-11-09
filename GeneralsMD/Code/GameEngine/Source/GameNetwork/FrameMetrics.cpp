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

/** FrameMetrics.cpp */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "GameNetwork/FrameMetrics.h"
#include "GameNetwork/NetworkUtil.h"
#include "GameNetwork/CushionMetrics.h"

/** -----------------------------------------------------------------------------------------------
 * The fps number the run-ahead equation wants: logic frames per second, from the logic frame the
 * sampling window opened on, the frame it closed on, and how long the window really lasted.
 * Free function (not a member, not static) so test_gameengine can link straight to it.
 */
Real FrameMetrics_logicFpsSample( UnsignedInt frame, UnsignedInt windowStartFrame, time_t windowMS )
{
	if (windowMS <= 0)
		return 0.0f;
	return (Real)(frame - windowStartFrame) * (Real)1000.0 / (Real)windowMS;
}

FrameMetrics::FrameMetrics() 
{
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_matchFps = LOGICFRAMES_PER_SECOND;
	m_averageFps = 0.0f;
	m_cushionIndex = 0;
	m_fpsListIndex = 0;
	m_lastFpsTimeThing = 0;
	m_fpsStartingFrame = 0;
	m_fpsStallMS = 0;
	m_minimumCushion = 0;

	m_fpsList = NEW Real[TheGlobalData->m_networkFPSHistoryLength];
}

FrameMetrics::~FrameMetrics() {
	// NEW Real[...]; EA freed it with scalar delete, which is a different operator
	if (m_fpsList != NULL) {
		delete[] m_fpsList;
		m_fpsList = NULL;
	}
}

void FrameMetrics::init() {
	// Full of the match's own speed: thirty seconds of history seeded at normal speed held a turbo
	// room near normal speed for most of a minute.
	m_averageFps = (Real)m_matchFps;
	m_minimumCushion = -1;

	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for (i = 0; i < TheGlobalData->m_networkFPSHistoryLength; ++i) {
		m_fpsList[i] = (Real)m_matchFps;
	}
	m_fpsListIndex = 0;
	// The fps window has to open on a real time and a real frame.  Left at zero, the first sample
	// is taken against a zero start time, reads as ~0 fps, and drags the whole room's run-ahead
	// down for the first NetworkFPSHistoryLength seconds of the game.
	m_lastFpsTimeThing = Clock_Milliseconds();
	m_fpsStartingFrame = 0;
	m_fpsStallMS = 0;
	m_cushionIndex = 0;
}

void FrameMetrics::setMatchFps(Int fps) {
	m_matchFps = fps;
	init();
}

void FrameMetrics::doPerFrameMetrics(UnsignedInt frame) {
	// Do the measurement of the fps.
	//
	// This number is what the run-ahead equation calls "how fast can this machine advance the
	// simulation" - it becomes minFps in ConnectionManager::updateRunAhead, and through that the
	// input delay every player in the room lives with.  EA sampled TheDisplay->getAverageFPS()
	// here because their renderer was locked to the logic rate, so the two were the same number.
	// They are not any more: this fork's renderer runs free while logic ticks on its own clock, so
	// a player with a weak GPU and a healthy CPU reported 12 while still stepping logic at 30, and
	// the run-ahead came out at a third of what the game needed - that player then stalled
	// constantly and the room stalled with them.
	//
	// doPerFrameMetrics() is called once per *logic* frame (ConnectionManager::processFrameTick),
	// so the honest measurement is in the argument: count the logic frames the window covered.
	// This is EA's own commented-out alternative, with the logic frame instead of the client one
	// and divided by the real window length rather than assuming it was exactly 1000ms.
	time_t curTime = Clock_Milliseconds();
	time_t windowMS = curTime - m_lastFpsTimeThing;
	if (windowMS >= 1000) {
		/* Time spent waiting for another machine's commands is not this machine being slow, and
			 counting it latched the room: a stall on a real internet link read as a low logic rate,
			 the room was commanded down to it, the slower room stalled just as often (the run-ahead is
			 sized in frames of that same rate, so it covers the same wall time) and reported lower
			 again.  A two player match on 2026-09-29 played 7835 frames in 371 seconds, 21 a
			 second, from start to end.  The machine that is really slow never waits on anybody, so its
			 own rate still reaches minFps untouched.  A quarter of the window is kept so a window
			 that was almost all stall cannot divide by nearly nothing, and the catch-up frames run
			 after a stall are capped at the game speed rather than reported as a faster machine. */
		time_t busyMS = windowMS - m_fpsStallMS;
		if (busyMS < windowMS / 4)
			busyMS = windowMS / 4;
		Real logicFps = FrameMetrics_logicFpsSample(frame, m_fpsStartingFrame, busyMS);
		if (logicFps > (Real)m_matchFps)
			logicFps = (Real)m_matchFps;
		m_fpsStallMS = 0;
		m_averageFps -= ((m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // subtract out the old value from the average.
		m_fpsList[m_fpsListIndex] = logicFps;
		m_averageFps += ((Real)(m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // add the new value to the average.
//		DEBUG_LOG(("average after: %f\n", m_averageFps));
		++m_fpsListIndex;
		m_fpsListIndex %= TheGlobalData->m_networkFPSHistoryLength;
		m_lastFpsTimeThing = curTime;
		m_fpsStartingFrame = frame;
	}
}

void FrameMetrics::addCushion(Int cushion) {
	++m_cushionIndex;
	m_cushionIndex %= TheGlobalData->m_networkCushionHistoryLength;
	if (m_cushionIndex == 0) {
		m_minimumCushion = -1;
	}
	if ((cushion < m_minimumCushion) || (m_minimumCushion == -1)) {
		m_minimumCushion = cushion;
	}
}

Int FrameMetrics::getAverageFPS() {
	return (Int)m_averageFps;
}

void FrameMetrics::addNetworkStall(time_t ms) {
	if (ms > 0)
		m_fpsStallMS += ms;
}

Int FrameMetrics::getMinimumCushion() {
	return m_minimumCushion;
}