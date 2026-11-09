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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

/** FrameMetrics.h */

#pragma once

#ifndef __FRAMEMETRICS_H
#define __FRAMEMETRICS_H

#include "Lib/BaseType.h"
#include "GameNetwork/NetworkDefs.h"

class FrameMetrics {
public:
	FrameMetrics();
	virtual ~FrameMetrics();

	void init();
	void reset();
	/// The match's logic frames a second, turbo mode included: the history starts full of it and no
	/// sample reads above it.  Starts again as init() does.
	void setMatchFps(Int fps);
	Int getMatchFps() const { return m_matchFps; }

	void doPerFrameMetrics(UnsignedInt frame);
	void addCushion(Int cushion);
	void addNetworkStall(time_t ms);	///< wall time spent waiting on another machine's commands, not computing

	Int getAverageFPS();
	Int getMinimumCushion();

protected:
	Int m_matchFps;																						///< see setMatchFps

	// These are used for keeping track of parameters to the run ahead equation.
	// frames per second history variables.
	Real *m_fpsList;								///< A record of how many game logic frames per second there were for the last 60 seconds.
	time_t m_lastFpsTimeThing;																///< The time when the last fps entry started being recorded.
	time_t m_fpsStallMS;																			///< Network stall inside the current fps window; see doPerFrameMetrics.
	UnsignedInt m_fpsStartingFrame;														///< The logic frame the window above opened on, so the sample is a logic rate.
	Int m_fpsListIndex;																				///< Index into the array of the current fps list entry being measured.
	Real m_averageFps;																				///< The current average logic fps: the oldest sample is subtracted out as each new one is added in.

	// The round trip the run-ahead is sized on is no longer here: it is each Connection's own srtt
	// (ConnectionManager::updateRunAhead), since commands stopped going through the packet router.

	// packet arrival cushion variables.
	// Keeps track of the cushion for the incoming commands.
	UnsignedInt m_cushionIndex;															///< The next index to use for the cushion list.
	Int m_minimumCushion;																			///< The average cushion for the history.
};

#endif // __FRAMEMETRICS_H