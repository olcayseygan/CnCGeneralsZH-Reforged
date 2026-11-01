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
// marks, the oldest going first.  The client's alone; nothing here reads or writes logic.

#pragma once

namespace GroundMarks {

// Where each sits in the scorch texture, EXScorch01: 0 to 3 are burns, then a soft stain, then cracks.
enum { BURN_KINDS = 4, STAIN = 4, CRACKS = 5 };

enum {
	CAPACITY = 256,
	HOLD_FRAMES = 60 * 60,			///< a mark stays whole for a minute of logic frames
	FADE_FRAMES = 3 * 60 * 60,	///< and is gone three minutes after that
	NEW_MARK_FRAMES = 30,				///< new marks wait this long for the buffer to be rebuilt: a barrage is one rebuild
	FADE_STEP_FRAMES = 120			///< a fading mark changes this often: 90 steps down
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

class Ring {
public:
	Ring() { clear(); }

	void clear()
	{
		for (int i = 0; i < CAPACITY; ++i)
			m_marks[i].radius = 0.0f;
		m_next = 0;
		m_held = 0;
	}

	bool empty() const { return m_held == 0; }

	/// A mark on top of a like one renews it, or a gun firing at one spot would stack up to a black disc
	/// and eat the ring.  Burns count as one kind.
	void add(float x, float y, float radius, int type, unsigned int now)
	{
		const float limit = radius / 2.0f;
		for (int i = 0; i < CAPACITY; ++i) {
			Mark &m = m_marks[i];
			if (m.radius > 0.0f && within(m.x - x, limit) && within(m.y - y, limit) && within(m.radius - radius, limit)
					&& (m.type == type || (m.type < BURN_KINDS && type < BURN_KINDS))) {
				m.born = now;
				return;
			}
		}
		Mark &m = m_marks[m_next];
		m.x = x;
		m.y = y;
		m.radius = radius;
		m.type = type;
		m.born = now;
		m_next = (m_next + 1) % CAPACITY;
		if (m_held < CAPACITY)
			++m_held;
	}

	/// The i'th newest slot, which may be empty or spent.
	const Mark &newest(int i) const { return m_marks[(m_next + CAPACITY - 1 - i) % CAPACITY]; }

	bool anyLeft(unsigned int now) const
	{
		for (int i = 0; i < m_held; ++i)
			if (strength(now - newest(i).born) > 0.0f)
				return true;
		return false;
	}

private:
	static bool within(float difference, float limit) { return difference < limit && -difference < limit; }

	Mark m_marks[CAPACITY];
	int m_next;
	int m_held;
};

}	// namespace GroundMarks
