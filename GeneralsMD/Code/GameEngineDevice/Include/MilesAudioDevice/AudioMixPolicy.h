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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: AudioMixPolicy.h
// The arithmetic behind MilesAudioManager's battle mix, kept free of the engine so a test can hold
// it: which playing sound gives way to a new one, how much louder a shot gets for the identical
// shots folded into it, how deep a hill or a building stands in the line between the microphone and
// a sound, and how fast the muffle that follows from it moves.  Nothing here reads or writes logic.

#pragma once

#include <math.h>

namespace AudioMixPolicy {

/// How much a sound matters when the voices run out: its priority first (EA's AP_LOWEST .. AP_CRITICAL,
/// 0 to 4), then how loud it is at the microphone (0 to 1), so among equals the quietest, which is
/// mostly the farthest, gives way first.
inline float importance(int priority, float loudness)
{
	if (loudness < 0.0f)
		loudness = 0.0f;
	if (loudness > 1.0f)
		loudness = 1.0f;
	return (float)priority + loudness;
}

/// The gain a shot plays at for the identical shots folded into it: a step for each, never past the
/// ceiling.  Many guns firing together sound like more than one gun, but not like all of them.
inline float coalescedGain(int merged, float step, float ceiling)
{
	const float gain = 1.0f + step * (float)merged;
	return gain < ceiling ? gain : ceiling;
}

}	// namespace AudioMixPolicy
