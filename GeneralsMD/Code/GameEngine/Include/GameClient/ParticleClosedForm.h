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

// ParticleClosedForm.h ///////////////////////////////////////////////////////////////////////////
// A particle's state after k updates without running the k updates, and the two records the GPU
// particle path keeps: one per particle, written once at birth, and one per system.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _PARTICLE_CLOSED_FORM_H_
#define _PARTICLE_CLOSED_FORM_H_

#include <math.h>
#include <stddef.h>
#include <string.h>
#include "Common/GameCommon.h"
#include "GameClient/ParticleSys.h"

/**
	* Particle::update's fade window (ParticleSys.cpp, PARTICLE_FADE_*): the last quarter of the
	* lifetime, at most eight retail frames, at least one.  Own names, so ParticleSys.cpp can include
	* this header next to its own copies.
	*/
enum
{
	PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES = 8 * LOGICFRAMES_PER_RETAIL_FRAME,
	PARTICLE_CLOSED_FORM_FADE_LIFETIME_DIVISOR = 4
};

/// a damping below this is taken as this, because HLSL's pow(0, 0) is NaN where C's is 1
static const Real PARTICLE_CLOSED_FORM_MIN_DAMPING = 1.0e-6f;

/**
	* One particle as the GPU sees it, written at birth and never again.  Floats and uints only, every
	* float3 followed by a scalar, so an HLSL StructuredBuffer of the same field order mirrors it.
	*
	* m_systemSlotSeed: the system entry's slot in the low 16 bits, a per-particle seed in the high 16.
	* m_alphaKeys: the eight alpha key values as bytes over 0..255, key i in byte (i & 3) of word i >> 2.
	* The key frames are the system's and live in its entry; only the values are drawn per particle.
	*/
struct ParticleGpuSpawn
{
	Real					m_pos[ 3 ];
	UnsignedInt		m_birthFrame;					///< client frame of the update that made it, k = now - birth + 1
	Real					m_vel[ 3 ];
	Real					m_lifetime;						///< whole frames, 0 = never ages
	Real					m_size;
	Real					m_sizeRate;
	Real					m_sizeRateDamping;
	Real					m_velDamping;
	Real					m_angleZ;
	Real					m_angularRateZ;
	Real					m_angularDamping;
	Real					m_colorScale;
	UnsignedInt		m_systemSlotSeed;
	UnsignedInt		m_alphaKeys[ 2 ];
	UnsignedInt		m_pad;
};

static_assert( sizeof( ParticleGpuSpawn ) == 80, "ParticleGpuSpawn is mirrored by an HLSL struct" );
static_assert( offsetof( ParticleGpuSpawn, m_birthFrame ) == 12, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_vel ) == 16, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_lifetime ) == 28, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_size ) == 32, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_velDamping ) == 44, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_angleZ ) == 48, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_colorScale ) == 60, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_systemSlotSeed ) == 64, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_alphaKeys ) == 68, "ParticleGpuSpawn layout" );
static_assert( offsetof( ParticleGpuSpawn, m_pad ) == 76, "ParticleGpuSpawn layout" );

/**
	* Segment i runs from alpha key i to key i + 1.  m_time is the rate's divisor exactly as
	* computeAlphaRate takes it, the unsigned frame difference as a float, so a key earlier than the
	* one before it gives a near-zero rate rather than a negative one.  m_end is the update count at
	* which key i + 1 is snapped to.
	*/
struct ParticleGpuAlphaSegment
{
	Real					m_time;
	UnsignedInt		m_end;
};

/// segment i runs from colour key i to key i + 1, its rate taken on every update up to m_end
struct ParticleGpuColorSegment
{
	Real					m_rate[ 3 ];
	UnsignedInt		m_end;
};

/**
	* One particle system as the GPU sees it.  Rebuilt when the system's keys change.  The texture
	* slice and the flags belong to the renderer; particleGpuSystemEntryBuild leaves them zero.
	* m_shaderType holds ParticleSystemInfo::ParticleShaderType (ADDITIVE 1, ALPHA 2, ALPHA_TEST 3,
	* MULTIPLY 4).
	*/
struct ParticleGpuSystemEntry
{
	Real										m_drift[ 3 ];
	Real										m_gravity;
	Real										m_color[ 3 ];									///< colour key 0
	UnsignedInt							m_textureSlice;
	UnsignedInt							m_shaderType;
	UnsignedInt							m_flags;
	UnsignedInt							m_alphaSegmentCount;					///< 0..7
	UnsignedInt							m_colorSegmentCount;					///< 0..7
	ParticleGpuAlphaSegment	m_alphaSegment[ MAX_KEYFRAMES ];
	ParticleGpuColorSegment	m_colorSegment[ MAX_KEYFRAMES ];
};

static_assert( sizeof( ParticleGpuAlphaSegment ) == 8, "ParticleGpuAlphaSegment is mirrored by an HLSL struct" );
static_assert( sizeof( ParticleGpuColorSegment ) == 16, "ParticleGpuColorSegment is mirrored by an HLSL struct" );
static_assert( sizeof( ParticleGpuSystemEntry ) == 240, "ParticleGpuSystemEntry is mirrored by an HLSL struct" );
static_assert( offsetof( ParticleGpuSystemEntry, m_gravity ) == 12, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_color ) == 16, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_textureSlice ) == 28, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_shaderType ) == 32, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_colorSegmentCount ) == 44, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_alphaSegment ) == 48, "ParticleGpuSystemEntry layout" );
static_assert( offsetof( ParticleGpuSystemEntry, m_colorSegment ) == 112, "ParticleGpuSystemEntry layout" );

/// What Particle::update would leave in a particle after k updates.
struct ParticleClosedFormState
{
	Coord3D		m_pos;
	Coord3D		m_vel;
	Real			m_size;
	Real			m_angleZ;
	Real			m_alpha;
	RGBColor	m_color;
	Bool			m_dead;		///< update would have returned false by update k, of age or of invisibility
};

// ------------------------------------------------------------------------------------------------
/// Particle::update's clamp, which leaves a NaN alone just as its two comparisons do
// ------------------------------------------------------------------------------------------------
inline Real particleClosedFormClamp01( Real value )
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

// ------------------------------------------------------------------------------------------------
inline Real particleGpuSpawnAlphaKey( const ParticleGpuSpawn &spawn, UnsignedInt key )
{
	return (Real)( ( spawn.m_alphaKeys[ key >> 2 ] >> ( ( key & 3 ) * 8 ) ) & 0xFF ) / 255.0f;
}

// ------------------------------------------------------------------------------------------------
/** The birth record of a particle made from info.  An alpha key outside 0..1 is clamped into a
	* byte, which only moves the rate toward it; the value itself is clamped on every update anyway. */
// ------------------------------------------------------------------------------------------------
inline void particleGpuSpawnPack( const ParticleInfo &info, UnsignedInt birthFrame, UnsignedInt systemSlot,
																	UnsignedInt seed, ParticleGpuSpawn *spawn )
{
	spawn->m_pos[ 0 ] = info.m_pos.x;
	spawn->m_pos[ 1 ] = info.m_pos.y;
	spawn->m_pos[ 2 ] = info.m_pos.z;
	spawn->m_birthFrame = birthFrame;
	spawn->m_vel[ 0 ] = info.m_vel.x;
	spawn->m_vel[ 1 ] = info.m_vel.y;
	spawn->m_vel[ 2 ] = info.m_vel.z;
	spawn->m_lifetime = (Real)info.m_lifetime;
	spawn->m_size = info.m_size;
	spawn->m_sizeRate = info.m_sizeRate;
	spawn->m_sizeRateDamping = info.m_sizeRateDamping;
	spawn->m_velDamping = info.m_velDamping;
	spawn->m_angleZ = info.m_angleZ;
	spawn->m_angularRateZ = info.m_angularRateZ;
	spawn->m_angularDamping = info.m_angularDamping;
	spawn->m_colorScale = info.m_colorScale;
	spawn->m_systemSlotSeed = ( systemSlot & 0xFFFF ) | ( ( seed & 0xFFFF ) << 16 );
	spawn->m_alphaKeys[ 0 ] = 0;
	spawn->m_alphaKeys[ 1 ] = 0;
	for( UnsignedInt i = 0; i < MAX_KEYFRAMES; ++i )
	{
		const UnsignedInt byte = (UnsignedInt)( particleClosedFormClamp01( info.m_alphaKey[ i ].value ) * 255.0f + 0.5f );
		spawn->m_alphaKeys[ i >> 2 ] |= byte << ( ( i & 3 ) * 8 );
	}
	spawn->m_pad = 0;
}

// ------------------------------------------------------------------------------------------------
/** The update on which each key after key 0 takes over.  Particle::update moves to key j on the
	* first update k after the last switch with k - 1 >= frame j, at most one key an update, and stops
	* at the first key whose frame is zero.  Keys out of order therefore last one update each.
	* Returns the number of segments. */
// ------------------------------------------------------------------------------------------------
inline UnsignedInt particleClosedFormSwitchUpdates( const UnsignedInt frame[ MAX_KEYFRAMES ], UnsignedInt end[ MAX_KEYFRAMES ] )
{
	UnsignedInt count = 0;
	UnsignedInt last = 0;
	for( UnsignedInt key = 1; key < MAX_KEYFRAMES && frame[ key ] != 0; ++key )
	{
		last = ( frame[ key ] + 1 > last + 1 ) ? frame[ key ] + 1 : last + 1;
		end[ count++ ] = last;
	}
	return count;
}

// ------------------------------------------------------------------------------------------------
/** The system's half of every particle it makes: drift, gravity, colour key 0 and the alpha and
	* colour segment tables, with the rates divided exactly as computeAlphaRate and computeColorRate
	* divide them.  Past the eighth key the real code reads one key beyond the array for one update;
	* here that rate is zero. */
// ------------------------------------------------------------------------------------------------
inline void particleGpuSystemEntryBuild( const ParticleSystemInfo &system, ParticleGpuSystemEntry *entry )
{
	memset( entry, 0, sizeof( *entry ) );

	const Coord3D drift = system.m_driftVelocity;
	entry->m_drift[ 0 ] = drift.x;
	entry->m_drift[ 1 ] = drift.y;
	entry->m_drift[ 2 ] = drift.z;
	entry->m_gravity = system.m_gravity;
	entry->m_color[ 0 ] = system.m_colorKey[ 0 ].color.red;
	entry->m_color[ 1 ] = system.m_colorKey[ 0 ].color.green;
	entry->m_color[ 2 ] = system.m_colorKey[ 0 ].color.blue;
	entry->m_shaderType = (UnsignedInt)system.m_shaderType;

	UnsignedInt frame[ MAX_KEYFRAMES ];
	UnsignedInt end[ MAX_KEYFRAMES ];

	for( UnsignedInt i = 0; i < MAX_KEYFRAMES; ++i )
		frame[ i ] = system.m_alphaKey[ i ].frame;
	entry->m_alphaSegmentCount = particleClosedFormSwitchUpdates( frame, end );
	for( UnsignedInt i = 0; i < entry->m_alphaSegmentCount; ++i )
	{
		const UnsignedInt time = frame[ i + 1 ] - frame[ i ];
		entry->m_alphaSegment[ i ].m_time = (Real)time;
		entry->m_alphaSegment[ i ].m_end = end[ i ];
	}

	for( UnsignedInt i = 0; i < MAX_KEYFRAMES; ++i )
		frame[ i ] = system.m_colorKey[ i ].frame;
	entry->m_colorSegmentCount = particleClosedFormSwitchUpdates( frame, end );
	for( UnsignedInt i = 0; i < entry->m_colorSegmentCount; ++i )
	{
		const UnsignedInt time = frame[ i + 1 ] - frame[ i ];
		const RGBColor &from = system.m_colorKey[ i ].color;
		const RGBColor &to = system.m_colorKey[ i + 1 ].color;
		entry->m_colorSegment[ i ].m_rate[ 0 ] = ( to.red - from.red ) / time;
		entry->m_colorSegment[ i ].m_rate[ 1 ] = ( to.green - from.green ) / time;
		entry->m_colorSegment[ i ].m_rate[ 2 ] = ( to.blue - from.blue ) / time;
		entry->m_colorSegment[ i ].m_end = end[ i ];
	}
}

// ------------------------------------------------------------------------------------------------
/** For a damping d and a real k >= 1: power = d^k, sum = d^0 + ... + d^(k-1), and nested = the sum
	* of sum(i) over i = 1..k.  A rate damped by d moves its value by rate * sum in k updates.
	*
	* The quotients (1 - d^k) / (1 - d) cancel to noise as d approaches 1, so while (1 - d) k < 0.1
	* the binomial series takes over: sum = C(k,1) - e C(k,2) + e^2 C(k,3) - e^3 C(k,4) with e = 1 - d,
	* nested the same over C(k+1, j+2).  It is exact for d = 1 and for k <= 4, and the first term
	* dropped is under 1e-6 of the value. */
// ------------------------------------------------------------------------------------------------
inline void particleClosedFormSums( Real damping, Real k, Real *power, Real *sum, Real *nested )
{
	const Real d = ( damping < PARTICLE_CLOSED_FORM_MIN_DAMPING ) ? PARTICLE_CLOSED_FORM_MIN_DAMPING : damping;
	const Real e = 1.0f - d;
	*power = powf( d, k );

	if (fabsf( e ) * k < 0.1f)
	{
		*sum = k * ( 1.0f - e * ( k - 1.0f ) / 2.0f * ( 1.0f - e * ( k - 2.0f ) / 3.0f * ( 1.0f - e * ( k - 3.0f ) / 4.0f ) ) );
		*nested = k * ( k + 1.0f ) / 2.0f
			* ( 1.0f - e * ( k - 1.0f ) / 3.0f * ( 1.0f - e * ( k - 2.0f ) / 4.0f * ( 1.0f - e * ( k - 3.0f ) / 5.0f ) ) );
		return;
	}

	*sum = ( 1.0f - *power ) / e;
	*nested = ( k - d * *sum ) / e;
}

// ------------------------------------------------------------------------------------------------
/** Alpha after update k.  Within a segment the rate is constant and the value is clamped every
	* update, so n updates in is clamp(start + n rate) exactly; a segment starts at its key's value,
	* snapped on the update that reaches it.  Past the last key alpha stays where the last snap put it.
	* ADDITIVE never touches alpha. */
// ------------------------------------------------------------------------------------------------
inline Real particleClosedFormAlpha( const ParticleGpuSpawn &spawn, const ParticleGpuSystemEntry &entry, Real k )
{
	if (entry.m_shaderType == (UnsignedInt)ParticleSystemInfo::ADDITIVE)
		return particleGpuSpawnAlphaKey( spawn, 0 );

	UnsignedInt start = 0;
	UnsignedInt key = 0;
	for( ; key < entry.m_alphaSegmentCount; ++key )
	{
		const ParticleGpuAlphaSegment &segment = entry.m_alphaSegment[ key ];
		if (k < (Real)segment.m_end)
		{
			const Real from = particleGpuSpawnAlphaKey( spawn, key );
			const Real rate = ( particleGpuSpawnAlphaKey( spawn, key + 1 ) - from ) / segment.m_time;
			return particleClosedFormClamp01( from + ( k - (Real)start ) * rate );
		}
		start = segment.m_end;
	}
	return particleClosedFormClamp01( particleGpuSpawnAlphaKey( spawn, key ) );
}

// ------------------------------------------------------------------------------------------------
/** Colour after update k, before the fade.  Every update adds the segment's rate and the
	* particle's colorScale and clamps; the rate is taken on the update that switches too, so each
	* segment overshoots its key by one rate.  Past the last key only colorScale moves it. */
// ------------------------------------------------------------------------------------------------
inline void particleClosedFormColor( const ParticleGpuSpawn &spawn, const ParticleGpuSystemEntry &entry, Real k, Real color[ 3 ] )
{
	const Real scale = spawn.m_colorScale;
	color[ 0 ] = entry.m_color[ 0 ];
	color[ 1 ] = entry.m_color[ 1 ];
	color[ 2 ] = entry.m_color[ 2 ];

	Real done = 0.0f;
	for( UnsignedInt i = 0; i < entry.m_colorSegmentCount && done < k; ++i )
	{
		const ParticleGpuColorSegment &segment = entry.m_colorSegment[ i ];
		const Real end = ( k < (Real)segment.m_end ) ? k : (Real)segment.m_end;
		const Real updates = end - done;
		for( Int c = 0; c < 3; ++c )
			color[ c ] = particleClosedFormClamp01( color[ c ] + updates * ( segment.m_rate[ c ] + scale ) );
		done = end;
	}

	if (done < k)
	{
		for( Int c = 0; c < 3; ++c )
			color[ c ] = particleClosedFormClamp01( color[ c ] + ( k - done ) * scale );
	}
}

// ------------------------------------------------------------------------------------------------
/** The fade over the last frames of a life, capping alpha (ALPHA, ALPHA_TEST) or the brightest
	* channel (ADDITIVE) at an envelope falling to zero.  Particle::update writes the cap into the
	* particle and keeps integrating from there, so this is exact only until the cap first bites; the
	* difference lives inside the window. */
// ------------------------------------------------------------------------------------------------
inline void particleClosedFormFade( const ParticleGpuSpawn &spawn, const ParticleGpuSystemEntry &entry, Real k,
																		Real *alpha, Real color[ 3 ] )
{
	const UnsignedInt lifetime = (UnsignedInt)spawn.m_lifetime;
	if (lifetime == 0)
		return;

	UnsignedInt fadeFrames = lifetime / PARTICLE_CLOSED_FORM_FADE_LIFETIME_DIVISOR;
	if (fadeFrames > PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES)
		fadeFrames = PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES;
	else if (fadeFrames == 0)
		fadeFrames = 1;

	const Real left = spawn.m_lifetime - k;
	if (left > (Real)fadeFrames)
		return;

	const Real envelope = ( left - 1.0f ) / (Real)fadeFrames;
	switch (entry.m_shaderType)
	{
		case ParticleSystemInfo::ALPHA:
		case ParticleSystemInfo::ALPHA_TEST:
			if (*alpha > envelope)
				*alpha = envelope;
			break;

		case ParticleSystemInfo::ADDITIVE:
		{
			Real brightest = color[ 0 ];
			if (color[ 1 ] > brightest)
				brightest = color[ 1 ];
			if (color[ 2 ] > brightest)
				brightest = color[ 2 ];
			if (brightest > envelope)
			{
				const Real scale = envelope / brightest;
				color[ 0 ] *= scale;
				color[ 1 ] *= scale;
				color[ 2 ] *= scale;
			}
			break;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** A particle of a system without wind, ground collision or upTowardsEmitter, k updates after
	* birth (k >= 1, fractional for a frame drawn between two updates).  MULTIPLY is not modelled:
	* no fade and never invisible.
	*
	* With v_k = d (v_{k-1} + g) and p_k = p_{k-1} + v_k + drift: v_k = d^k v0 + g d sum(k) and
	* p_k = p0 + k drift + v0 d sum(k) + g d nested(k).  Angle and size are a0 + r0 sum(k) in their
	* own dampings. */
// ------------------------------------------------------------------------------------------------
inline void particleClosedFormEvaluate( const ParticleGpuSpawn &spawn, const ParticleGpuSystemEntry &entry, Real k,
																				ParticleClosedFormState *state )
{
	Real power, sum, nested;

	particleClosedFormSums( spawn.m_velDamping, k, &power, &sum, &nested );
	const Real d = ( spawn.m_velDamping < PARTICLE_CLOSED_FORM_MIN_DAMPING ) ? PARTICLE_CLOSED_FORM_MIN_DAMPING : spawn.m_velDamping;
	const Real velocitySum = d * sum;
	state->m_vel.x = power * spawn.m_vel[ 0 ];
	state->m_vel.y = power * spawn.m_vel[ 1 ];
	state->m_vel.z = power * spawn.m_vel[ 2 ] + entry.m_gravity * velocitySum;
	state->m_pos.x = spawn.m_pos[ 0 ] + k * entry.m_drift[ 0 ] + spawn.m_vel[ 0 ] * velocitySum;
	state->m_pos.y = spawn.m_pos[ 1 ] + k * entry.m_drift[ 1 ] + spawn.m_vel[ 1 ] * velocitySum;
	state->m_pos.z = spawn.m_pos[ 2 ] + k * entry.m_drift[ 2 ] + spawn.m_vel[ 2 ] * velocitySum
		+ entry.m_gravity * d * nested;

	particleClosedFormSums( spawn.m_angularDamping, k, &power, &sum, &nested );
	state->m_angleZ = spawn.m_angleZ + spawn.m_angularRateZ * sum;

	particleClosedFormSums( spawn.m_sizeRateDamping, k, &power, &sum, &nested );
	state->m_size = spawn.m_size + spawn.m_sizeRate * sum;

	Real color[ 3 ];
	Real alpha = particleClosedFormAlpha( spawn, entry, k );
	particleClosedFormColor( spawn, entry, k, color );
	particleClosedFormFade( spawn, entry, k, &alpha, color );
	state->m_alpha = alpha;
	state->m_color.red = color[ 0 ];
	state->m_color.green = color[ 1 ];
	state->m_color.blue = color[ 2 ];

	// isInvisible: only once past the last key, and it kills, so a particle that was invisible on
	// any update since is gone.  Past the last key alpha only falls; colour moves by colorScale on
	// every channel alike, so the dimmest update is the first one past the key or this one.
	Bool invisible = FALSE;
	switch (entry.m_shaderType)
	{
		case ParticleSystemInfo::ALPHA:
		{
			const UnsignedInt last = entry.m_alphaSegmentCount ? entry.m_alphaSegment[ entry.m_alphaSegmentCount - 1 ].m_end : 0;
			invisible = k >= (Real)last && alpha < 0.02f;
			break;
		}

		case ParticleSystemInfo::ADDITIVE:
		{
			const UnsignedInt last = entry.m_colorSegmentCount ? entry.m_colorSegment[ entry.m_colorSegmentCount - 1 ].m_end : 0;
			if (k < (Real)last)
				break;
			invisible = ( color[ 0 ] + color[ 1 ] + color[ 2 ] ) <= 0.06f;
			const Real first = last ? (Real)last : 1.0f;
			if (!invisible && first < k)
			{
				Real firstAlpha = alpha;
				Real firstColor[ 3 ];
				particleClosedFormColor( spawn, entry, first, firstColor );
				particleClosedFormFade( spawn, entry, first, &firstAlpha, firstColor );
				invisible = ( firstColor[ 0 ] + firstColor[ 1 ] + firstColor[ 2 ] ) <= 0.06f;
			}
			break;
		}
	}

	state->m_dead = invisible || ( spawn.m_lifetime != 0.0f && k >= spawn.m_lifetime );
}

#endif // _PARTICLE_CLOSED_FORM_H_
