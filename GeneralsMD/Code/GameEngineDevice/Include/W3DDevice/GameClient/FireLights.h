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

// FILE: FireLights.h
// The Firelight option: flames as point lights on the ground, walls and units, kept free of the engine
// so a test can hold the grouping and the cap.  A burning building is a dozen systems, so flames near
// each other become one light, and the strongest MAX_LIGHTS in view go to the per pixel lights
// (BLAST_LIGHT_SAMPLING).  Client only: nothing here reads or writes logic.

#pragma once

#include <math.h>

namespace FireLights {

enum {
	MAX_LIGHTS = 12,		///< a frame's fire lights, out of the 32 per pixel slots the pulses share
	MAX_GROUPS = 128		///< flames in view; past this a new group is dropped
};

static const float MERGE_DISTANCE = 60.0f;	///< flames this close to a group's middle join it
static const float MIN_WEIGHT = 25.0f;			///< brightness x size under which flames light nothing: a muzzle flare
static const float FULL_WEIGHT = 300.0f;		///< and at which they are at full strength
static const float REACH = 90.0f;						///< a full fire's reach beyond its own spread
static const float SPREAD_SCALE = 2.0f;			///< times the flames' RMS spread, added to the reach
static const float MAX_REACH = 260.0f;
static const float INNER_REACH = 10.0f;			///< full strength inside this
static const float LIFT = 25.0f;						///< over the flames' middle, so the ground around them faces the light
static const float GAIN = 0.7f;							///< a full fire's light at the surface it faces
static const float AMBIENT_SHARE = 0.3f;		///< of that, on every surface it reaches whichever way it faces
static const float RED = 1.0f, GREEN = 0.5f, BLUE = 0.2f;

/// Weighted sums over a fire's particles: w the weight (brightness x size), then the weighted
/// position and the weighted square of it.
struct Flames {
	float w, x, y, z, sq;

	Flames() : w(0.0f), x(0.0f), y(0.0f), z(0.0f), sq(0.0f) {}

	void add(float weight, float px, float py, float pz)
	{
		w += weight;
		x += weight * px;
		y += weight * py;
		z += weight * pz;
		sq += weight * (px * px + py * py + pz * pz);
	}
	void add(const Flames &other)
	{
		w += other.w;
		x += other.x;
		y += other.y;
		z += other.z;
		sq += other.sq;
	}
	void scale(float by)
	{
		w *= by;
		x *= by;
		y *= by;
		z *= by;
		sq *= by;
	}
};

struct Light {
	float x, y, z, reach, inner;
	float red, green, blue;		///< the facing surface's light; the ambient is AMBIENT_SHARE of it
	float strength;
};

/// 0 at MIN_WEIGHT, so a fire comes on with no pop, and 1 from FULL_WEIGHT.
inline float strength(float weight)
{
	if (weight <= MIN_WEIGHT)
		return 0.0f;
	return weight >= FULL_WEIGHT ? 1.0f : (weight - MIN_WEIGHT) / (FULL_WEIGHT - MIN_WEIGHT);
}

/// A fire breathing on the particle clock, 0.78 to 1, each spot of the map out of step with the next.
/// The flames' own weight moves it too until it saturates, and a big fire's weight always has.
inline float flicker(unsigned int frame, float x, float y)
{
	const float f = (float)frame;
	const float phase = floorf(x / 64.0f) * 3.7f + floorf(y / 64.0f) * 5.3f;
	const float noise = 0.5f * sinf(0.21f * f + phase) + 0.3f * sinf(0.53f * f + 2.1f * phase) + 0.2f * sinf(1.07f * f + 3.7f * phase);
	return 0.89f + 0.11f * noise;
}

class Gather {
public:
	Gather() : m_count(0) {}

	void clear() { m_count = 0; }
	int count() const { return m_count; }

	/// Flames join the first group whose middle is near them, whose spread they then widen.
	void add(const Flames &flames)
	{
		if (flames.w <= 0.0f)
			return;
		const float inv = 1.0f / flames.w;
		const float px = flames.x * inv, py = flames.y * inv, pz = flames.z * inv;
		for (int i = 0; i < m_count; ++i) {
			const Flames &group = m_groups[i];
			const float groupInv = 1.0f / group.w;
			const float dx = group.x * groupInv - px, dy = group.y * groupInv - py, dz = group.z * groupInv - pz;
			if (dx * dx + dy * dy + dz * dz < MERGE_DISTANCE * MERGE_DISTANCE) {
				m_groups[i].add(flames);
				return;
			}
		}
		// ponytail: past MAX_GROUPS in view a late fire goes dark; pick by weight if a nuke's field ever gets here
		if (m_count < MAX_GROUPS)
			m_groups[m_count++] = flames;
	}

	/// The strongest MAX_LIGHTS whose reach touches the box, into out[MAX_LIGHTS].  Returns how many.
	int pick(const float center[3], const float extent[3], unsigned int frame, Light *out) const
	{
		int lit = 0;
		for (int i = 0; i < m_count; ++i) {
			Light light;
			if (!makeLight(m_groups[i], frame, light))
				continue;
			if (fabsf(light.x - center[0]) > extent[0] + light.reach
					|| fabsf(light.y - center[1]) > extent[1] + light.reach
					|| fabsf(light.z - center[2]) > extent[2] + light.reach)
				continue;
			int slot = lit;
			if (lit == MAX_LIGHTS) {
				slot = 0;
				for (int j = 1; j < lit; ++j)
					if (out[j].strength < out[slot].strength)
						slot = j;
				if (out[slot].strength >= light.strength)
					continue;
			} else
				++lit;
			out[slot] = light;
		}
		return lit;
	}

private:
	static bool makeLight(const Flames &flames, unsigned int frame, Light &light)
	{
		light.strength = strength(flames.w);
		if (light.strength <= 0.0f)
			return false;
		const float inv = 1.0f / flames.w;
		light.x = flames.x * inv;
		light.y = flames.y * inv;
		light.z = flames.z * inv + LIFT;
		const float mz = flames.z * inv;
		const float variance = flames.sq * inv - (light.x * light.x + light.y * light.y + mz * mz);
		const float reach = REACH * (0.5f + 0.5f * light.strength) + SPREAD_SCALE * (variance > 0.0f ? sqrtf(variance) : 0.0f);
		light.reach = reach < MAX_REACH ? reach : MAX_REACH;
		light.inner = INNER_REACH;
		const float gain = light.strength * GAIN * flicker(frame, light.x, light.y);
		light.red = RED * gain;
		light.green = GREEN * gain;
		light.blue = BLUE * gain;
		return true;
	}

	Flames m_groups[MAX_GROUPS];
	int m_count;
};

}	// namespace FireLights
