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

// FILE: GroundMarks.h
// The marks a fight leaves on the terrain under the Battle Marks option, kept free of the engine so
// a test can hold the cap and the fade.  A fixed ring: a long match can never hold more than CAPACITY
// marks, the oldest going first.  The client's alone; nothing here reads or writes logic.  Ages are in
// logic frames, so the one thing taken from the engine is its rate (GameCommon.h, which needs only Lib/).

#pragma once

#include "Common/GameCommon.h"

namespace GroundMarks {

// Where each sits in the scorch texture, EXScorch01: 0 to 3 are burns, then a soft stain, then cracks.
enum { BURN_KINDS = 4, STAIN = 4, CRACKS = 5 };

enum {
	CAPACITY = 256,
	HOLD_FRAMES = 60 * LOGICFRAMES_PER_SECOND,			///< a mark stays whole for a minute
	FADE_FRAMES = 3 * 60 * LOGICFRAMES_PER_SECOND,	///< and is gone three minutes after that
	NEW_MARK_FRAMES = LOGICFRAMES_PER_SECOND / 2,		///< new marks wait this long for the buffer to be rebuilt: a barrage is one rebuild
	FADE_STEP_FRAMES = 2 * LOGICFRAMES_PER_SECOND		///< a fading mark changes this often: 90 steps down
};

struct Mark {
	float x, y, radius;		///< radius 0 is an empty slot
	int type;
	unsigned int born;		///< logic frame
};

/// How much of a mark is left at an age in logic frames, 1 to 0.
inline float strength(unsigned int age)
{
	if (age <= HOLD_FRAMES)
		return 1.0f;
	if (age >= HOLD_FRAMES + FADE_FRAMES)
		return 0.0f;
	return 1.0f - (float)(age - HOLD_FRAMES) / (float)FADE_FRAMES;
}

template <int N>
class RingOf {
public:
	enum { SLOTS = N };

	RingOf() { clear(); }

	void clear()
	{
		for (int i = 0; i < N; ++i)
			m_marks[i].radius = 0.0f;
		m_next = 0;
		m_held = 0;
	}

	bool empty() const { return m_held == 0; }

	/// A mark on top of a like one renews it, or a gun firing at one spot would stack up to a black disc
	/// and eat the ring.  Burns count as one kind.  True when it took a slot of its own.
	bool add(float x, float y, float radius, int type, unsigned int now)
	{
		const float limit = radius / 2.0f;
		for (int i = 0; i < N; ++i) {
			Mark &m = m_marks[i];
			if (m.radius > 0.0f && within(m.x - x, limit) && within(m.y - y, limit) && within(m.radius - radius, limit)
					&& (m.type == type || (m.type < BURN_KINDS && type < BURN_KINDS))) {
				m.born = now;
				return false;
			}
		}
		Mark &m = m_marks[m_next];
		m.x = x;
		m.y = y;
		m.radius = radius;
		m.type = type;
		m.born = now;
		m_next = (m_next + 1) % N;
		if (m_held < N)
			++m_held;
		return true;
	}

	/// How many slots have been written since the last clear, at most N.
	int held() const { return m_held; }

	/// The i'th newest slot, which may be empty or spent.
	const Mark &newest(int i) const { return m_marks[(m_next + N - 1 - i) % N]; }

	bool anyLeft(unsigned int now) const
	{
		for (int i = 0; i < m_held; ++i)
			if (strength(now - newest(i).born) > 0.0f)
				return true;
		return false;
	}

private:
	static bool within(float difference, float limit) { return difference < limit && -difference < limit; }

	Mark m_marks[N];
	int m_next;
	int m_held;
};

typedef RingOf<CAPACITY> Ring;

// The Burnt Ground option: a blast's scorch smoulders.  Black ash under it, its cracks glowing and
// cooling from yellow to a dull red, then the ash goes and EA's scorch is the cold scar left.  A
// fresh blast on a hot patch heats it again (the ring's renew).  The flames are particle systems the
// client makes once, when a patch takes a slot of its own.
enum {
	BURN_CAPACITY = 48,
	BURN_HOT_FRAMES = 3 * LOGICFRAMES_PER_SECOND,				///< the cracks glow full this long
	BURN_COOL_FRAMES = 35 * LOGICFRAMES_PER_SECOND,			///< and are cold this long after that
	BURN_ASH_FADE_FRAMES = 25 * LOGICFRAMES_PER_SECOND,	///< then the ash goes over this long
	BURN_LIFE_FRAMES = BURN_HOT_FRAMES + BURN_COOL_FRAMES + BURN_ASH_FADE_FRAMES
};

typedef RingOf<BURN_CAPACITY> BurnRing;

/// The smallest blast that burns the ground, and the same one that lights flames on it.  A tank shell
/// or a rocket (radius 10 to 12) used to leave a glowing patch too, and a fight round one Overlord
/// paved the ground under its barrels with orange ash; a vehicle's death (15) and a bomb still burn.
static const float BURN_FROM_RADIUS = 15.0f;

inline bool burnsGround(float radius)
{
	return radius >= BURN_FROM_RADIUS;
}

/// How hot a patch's cracks are at an age in logic frames, 1 to 0, cooling fast at first.
inline float burnHeat(unsigned int age)
{
	if (age <= BURN_HOT_FRAMES)
		return 1.0f;
	if (age >= BURN_HOT_FRAMES + BURN_COOL_FRAMES)
		return 0.0f;
	const float left = 1.0f - (float)(age - BURN_HOT_FRAMES) / (float)BURN_COOL_FRAMES;
	return left * left;
}

/// How bright the cracks are, as against how hot (the colour): a cooling patch reddens fast but keeps a
/// faint glow, which dies over the last quarter of the cooling.  Taken from the heat alone, the cracks were
/// dull red flecks nobody saw from 16 s on.
inline float burnGlow(unsigned int age)
{
	if (age <= BURN_HOT_FRAMES)
		return 1.0f;
	if (age >= BURN_HOT_FRAMES + BURN_COOL_FRAMES)
		return 0.0f;
	const float x = (float)(age - BURN_HOT_FRAMES) / (float)BURN_COOL_FRAMES;
	const float left = 1.0f - x;
	const float GLOW_FLOOR = 0.4f, FADE_FROM = 0.75f;
	if (x <= FADE_FROM)
		return GLOW_FLOOR + (1.0f - GLOW_FLOOR) * left * left;
	const float atFade = GLOW_FLOOR + (1.0f - GLOW_FLOOR) * (1.0f - FADE_FROM) * (1.0f - FADE_FROM);
	return atFade * left / (1.0f - FADE_FROM);
}

/// How much of a patch's ash is left, 1 while it smoulders and down to 0 after.
inline float burnAsh(unsigned int age)
{
	if (age <= BURN_HOT_FRAMES + BURN_COOL_FRAMES)
		return 1.0f;
	if (age >= BURN_LIFE_FRAMES)
		return 0.0f;
	return 1.0f - (float)(age - BURN_HOT_FRAMES - BURN_COOL_FRAMES) / (float)BURN_ASH_FADE_FRAMES;
}

/// A number in [0,1) from two integers, for what the client picks per patch or per vertex without
/// drawing on any random stream: the same spot gets the same answer on every run.
inline float burnHash(int a, int b)
{
	unsigned int h = (unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (float)(h & 0xFFFFu) / 65536.0f;
}

}	// namespace GroundMarks
