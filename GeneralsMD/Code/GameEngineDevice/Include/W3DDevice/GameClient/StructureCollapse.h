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

// FILE: StructureCollapse.h
// The Collapse option: a copy of a building's last model goes into the ground.  A render object no
// drawable owns: the object is already gone or already rubble, and nothing in the game reads any of
// this.  The timing is here, free of the engine, so a test can hold it.

#pragma once

class RenderObjClass;

namespace StructureCollapse {

enum { MAX_PIECES = 48 };		///< a nuke on a base loses the rest of its copies, not a frame

/// Seconds a building of this height takes to go into the ground.
inline float sinkSeconds(float height)
{
	const float seconds = 0.9f + height / 60.0f;
	return seconds < 2.5f ? seconds : 2.5f;
}

/// How much of its height a building has lost at this many seconds, 0 to 1: a beat, then faster and faster.
inline float sunk(float seconds, float total)
{
	const float BEAT = 0.15f;
	if (seconds <= BEAT)
		return 0.0f;
	if (seconds >= total)
		return 1.0f;
	const float fraction = (seconds - BEAT) / (total - BEAT);
	return fraction * fraction;
}

/// Hand over a copy of a building's model as it stands: this takes the reference and it goes into the ground.
void add(RenderObjClass *copy, float height);

/// Moves what is falling.  Outside the frame's draw: this adds to the scene.
void update();

/// Nothing falling, nothing in the scene: a map is ending.
void clear();

}  // namespace StructureCollapse
