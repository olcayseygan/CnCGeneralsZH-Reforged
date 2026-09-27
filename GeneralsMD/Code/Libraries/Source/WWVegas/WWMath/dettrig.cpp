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

/* dettrig.cpp - see dettrig.h for why this exists at all.
 *
 * The game already shipped with half of this idea in it.  EA wrote an integer
 * fixed point trig table in GameEngine/Source/Common/System/Trig.cpp, said in
 * their own comment that it was there to "use a precalculated lookup table for
 * speed and repeatability across different machines", and then switched it off
 * with "#define DEFAULT_TRIG", routing every call straight back to sinf, cosf,
 * tanf and acosf.  The repeatability is the part that matters, and the shipped
 * game does not have it.
 *
 * This is a rewrite rather than a revival, because their tables were too
 * coarse to simply switch on: 4096 samples of a whole turn with a 12 bit
 * fraction, worst case error 7.7e-4 radians, which is 0.04 degrees of
 * permanent error on every unit's heading.  What is here samples a quarter
 * turn 2048 times with a 24 bit fraction and interpolates, for about 1.6e-7
 * radians at a third of the memory.
 *
 * Every step below is either integer arithmetic or an IEEE operation that is
 * required to be exact or correctly rounded, so all of it is reproducible: the
 * scale factors are powers of two, so converting an angle to fixed point and a
 * result back to float cannot round at all; the two divisions in the arc
 * tangent and the square roots feeding the arc sine and arc cosine are
 * correctly rounded by the hardware; and the argument reduction runs in double
 * so that the reduction itself is not what limits the answer.
 */

#include "always.h"
#include "dettrig.h"

#include <math.h>
#include <float.h>
#include <intrin.h>

// ----------------------------------------------------------------------------
// Fixed point layout.
//
// Angles are measured in turns over the whole width of an unsigned int, which
// makes wrapping free (it is the integer overflow) and makes the quadrant the
// top two bits.  Values are separately scaled by 2^24, the largest power of two
// a float still holds every integer below, so a wider value table could not be
// carried out of here anyway.
// ----------------------------------------------------------------------------
static const unsigned int ANGLE_QUARTER	= 0x40000000u;		// a quarter turn, PI/2
static const unsigned int ANGLE_HALF		= 0x80000000u;		// half a turn, PI

enum
{
	TRIG_VALUE_BITS			= 24,
	TRIG_VALUE_ONE			= 1 << TRIG_VALUE_BITS,			// the value 1.0

	SIN_QUARTER_ENTRIES	= 2048,											// samples of sin over [0, PI/2]
	SIN_INDEX_SHIFT			= 19,												// quarter turn (2^30) / entries
	SIN_FRAC_MASK				= (1 << SIN_INDEX_SHIFT) - 1,

	ATAN_ENTRIES				= 1024,											// samples of atan over [0, 1]
	ATAN_INDEX_SHIFT		= 22,												// unit ratio (2^32) / entries
	ATAN_FRAC_MASK			= (1 << ATAN_INDEX_SHIFT) - 1
};

#include "dettrigtables.h"

/* Both interpolations multiply a table step by a fraction, and both need 64
	 bits to do it: the largest sine step is 12868 against a largest fraction of
	 524287, for 6.7e9, and the largest arc tangent step is 667544 against
	 4194303, for 2.8e12.  gentrigtables.py asserts those two step sizes, so a
	 regenerated table cannot quietly outgrow this. */

typedef __int64 DetInt64;

static const double TURN_SCALE				= 4294967296.0;								// 2^32, one turn
static const double TURNS_PER_RADIAN	= 0.15915494309189533577;			// 1 / (2 PI)
static const double RADIANS_PER_TURN	= 6.28318530717958647692 / TURN_SCALE;
static const float	HALF_PI						= 1.57079632679489661923f;

// ----------------------------------------------------------------------------
/* Reduce an angle in radians to the fixed point turn it lands on.
 *
 * The reduction is done in double rather than in float: a float multiply by
 * 1/(2 PI) carries a relative error of 6e-8, which for an angle of a few turns
 * is already larger than everything else in this file put together.  Doubles
 * are IEEE too, so this costs nothing in reproducibility.
 *
 * floor is exact and the multiply by 2^32 is by a power of two, so the only
 * rounding in here is the one multiply.  The widening to 64 bits before the
 * mask is what handles an angle just below zero, which reduces to a turns
 * value that rounds up to exactly 1.0 and would otherwise convert out of
 * range; wrapping a whole turn back to zero is what it means anyway. */
// ----------------------------------------------------------------------------
static unsigned int fixedAngle( float radians )
{
	double turns = (double)radians * TURNS_PER_RADIAN;
	turns -= floor( turns );
	return (unsigned int)((DetInt64)(turns * TURN_SCALE) & 0xFFFFFFFF);
}

// ----------------------------------------------------------------------------
// Turn a signed count of turns back into radians.  The count is exact, so this
// is one rounding.
// ----------------------------------------------------------------------------
static float turnsToRadians( DetInt64 turns )
{
	return (float)((double)turns * RADIANS_PER_TURN);
}

// ----------------------------------------------------------------------------
// sin of a quarter turn's worth of angle, u in [0, 2^30], scaled by
// TRIG_VALUE_ONE.
// ----------------------------------------------------------------------------
static int quarterSin( unsigned int u )
{
	unsigned int index = u >> SIN_INDEX_SHIFT;

	// only the exact quarter turn lands here, and it is the last entry rather
	// than a special case
	if( index >= (unsigned int)SIN_QUARTER_ENTRIES )
		return theSinQuarterTable[ SIN_QUARTER_ENTRIES ];

	int base = theSinQuarterTable[ index ];
	int step = theSinQuarterTable[ index + 1 ] - base;

	return base + (int)(((DetInt64)step * (int)(u & SIN_FRAC_MASK)) >> SIN_INDEX_SHIFT);
}

// ----------------------------------------------------------------------------
// sin of a whole turn's worth of angle, scaled by TRIG_VALUE_ONE.  The quadrant
// is the top two bits of the angle, which is the entire reason for measuring
// angles in turns.
// ----------------------------------------------------------------------------
static int fixedSin( unsigned int angle )
{
	unsigned int offset = angle & (ANGLE_QUARTER - 1);

	switch( angle >> 30 )
	{
		case 0:		return  quarterSin( offset );
		case 1:		return  quarterSin( ANGLE_QUARTER - offset );
		case 2:		return -quarterSin( offset );
		default:	return -quarterSin( ANGLE_QUARTER - offset );
	}
}

// ----------------------------------------------------------------------------
// atan of a ratio in [0, 1], the answer in turns, so at most an eighth of one.
// ----------------------------------------------------------------------------
static int arcTanFixed( DetInt64 fixed )
{
	DetInt64 index = fixed >> ATAN_INDEX_SHIFT;

	if( index >= ATAN_ENTRIES )
		return theArcTanTable[ ATAN_ENTRIES ];

	int base = theArcTanTable[ (int)index ];
	int step = theArcTanTable[ (int)index + 1 ] - base;

	return base + (int)(((DetInt64)step * (fixed & ATAN_FRAC_MASK)) >> ATAN_INDEX_SHIFT);
}

// ----------------------------------------------------------------------------
// the ratio scaled by 2^32; a ratio of exactly one scales to 2^32, so this is
// measured in 64 bits all the way down
// ----------------------------------------------------------------------------
static int arcTanUnit( float ratio )
{
	return arcTanFixed( (DetInt64)((double)ratio * TURN_SCALE) );
}

// ----------------------------------------------------------------------------
/* The angle of (x, y) in turns, in [-2^31, 2^31].
 *
 * The table is only ever asked about the angle to the nearer axis, which keeps
 * its argument in the half of the range where atan is well behaved, and the
 * quadrant is then folded back in with integer adds.  Those adds are done in 64
 * bits on purpose: half a turn is 2^31, which does not fit a signed 32 bit int,
 * and the case that lands exactly on it is atan2 of zero and a negative x,
 * where the answer has to come back as +PI rather than -PI.
 *
 * IEEE division is correctly rounded, so the ratio is identical on every
 * machine; the smaller magnitude goes on top, so it is always in [0, 1] and the
 * denominator is never zero. */
// ----------------------------------------------------------------------------
static DetInt64 fixedATan2( float y, float x )
{
	float ax = (float)fabs( x );
	float ay = (float)fabs( y );

	if( ax == 0.0f && ay == 0.0f )
		return 0;

	DetInt64 fromAxis = (ay <= ax)
			? (DetInt64)arcTanUnit( ay / ax )
			: (DetInt64)ANGLE_QUARTER - arcTanUnit( ax / ay );

	if( x >= 0.0f )
		return (y >= 0.0f) ? fromAxis : -fromAxis;

	return (y >= 0.0f) ? ((DetInt64)ANGLE_HALF - fromAxis) : (fromAxis - (DetInt64)ANGLE_HALF);
}

// ----------------------------------------------------------------------------
/* acos in turns, in [0, 2^31].
 *
 * The half angle form, 2 atan(sqrt((1-x)/(1+x))), rather than
 * atan2(sqrt(1 - x*x), x).  Near either end the obvious form has already
 * cancelled its significant bits away in 1 - x*x, while 1 - x and 1 + x are
 * exact for every x this function accepts.  Doubling here rather than after the
 * conversion to radians keeps the doubling exact and leaves one rounding
 * instead of two. */
// ----------------------------------------------------------------------------
static DetInt64 fixedACos( float x )
{
	if( x <= -1.0f )
		return (DetInt64)ANGLE_HALF;
	if( x >= 1.0f )
		return 0;

	return 2 * fixedATan2( (float)sqrt( 1.0f - x ), (float)sqrt( 1.0f + x ) );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
float DetTrig::Sin( float radians )
{
	return (float)fixedSin( fixedAngle( radians ) ) * (1.0f / (float)TRIG_VALUE_ONE);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
float DetTrig::Cos( float radians )
{
	return (float)fixedSin( fixedAngle( radians ) + ANGLE_QUARTER ) * (1.0f / (float)TRIG_VALUE_ONE);
}

// ----------------------------------------------------------------------------
// Both halves come from the same table, so the ratio is as good as the sine is,
// and the divide is correctly rounded: numerator and denominator are integers
// well inside the 24 bits a float holds exactly.
// ----------------------------------------------------------------------------
float DetTrig::Tan( float radians )
{
	unsigned int angle = fixedAngle( radians );
	int sine = fixedSin( angle );
	int cosine = fixedSin( angle + ANGLE_QUARTER );

	// the fixed point angle can land exactly on a quarter turn, where a float one
	// never quite does
	if( cosine == 0 )
		return sine >= 0 ? FLT_MAX : -FLT_MAX;

	return (float)sine / (float)cosine;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
float DetTrig::ATan2( float y, float x )
{
	return turnsToRadians( fixedATan2( y, x ) );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
float DetTrig::ATan( float x )
{
	return turnsToRadians( fixedATan2( x, 1.0f ) );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
float DetTrig::ACos( float x )
{
	return turnsToRadians( fixedACos( x ) );
}

// ----------------------------------------------------------------------------
// Small arguments go through atan2(x, sqrt(1 - x*x)), which is well conditioned
// there.  Large ones borrow the arc cosine, because that is where 1 - x*x stops
// being trustworthy and where asin itself turns steep; the quarter turn they
// are subtracted from is exact, so borrowing it costs nothing.
// ----------------------------------------------------------------------------
float DetTrig::ASin( float x )
{
	if( x <= -1.0f )
		return -HALF_PI;
	if( x >= 1.0f )
		return HALF_PI;

	if( x > 0.7f )
		return turnsToRadians( (DetInt64)ANGLE_QUARTER - fixedACos( x ) );
	if( x < -0.7f )
		return turnsToRadians( fixedACos( -x ) - (DetInt64)ANGLE_QUARTER );

	return turnsToRadians( fixedATan2( x, (float)sqrt( 1.0f - x * x ) ) );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
int DetTrig::SinTurn( unsigned int turn )
{
	return fixedSin( turn );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
int DetTrig::CosTurn( unsigned int turn )
{
	return fixedSin( turn + ANGLE_QUARTER );
}

// ----------------------------------------------------------------------------
/* fixedATan2 with the ratio taken exactly instead of through a float divide:
	 the smaller magnitude over the larger, scaled by 2^32 and truncated, in one
	 128 bit division.  The quotient is at most 2^32 because the smaller is on
	 top, and the high half of the dividend is below the divisor for the same
	 reason, which is what _udiv128 needs. */
// ----------------------------------------------------------------------------
long long DetTrig::ATan2Turn( long long y, long long x )
{
	unsigned long long ax = x < 0 ? 0ull - (unsigned long long)x : (unsigned long long)x;
	unsigned long long ay = y < 0 ? 0ull - (unsigned long long)y : (unsigned long long)y;

	if( ax == 0 && ay == 0 )
		return 0;

	unsigned long long lesser = ay <= ax ? ay : ax;
	unsigned long long greater = ay <= ax ? ax : ay;
	unsigned long long remainder;
	DetInt64 ratio = (DetInt64)_udiv128( lesser >> 32, lesser << 32, greater, &remainder );

	DetInt64 fromAxis = (ay <= ax)
			? (DetInt64)arcTanFixed( ratio )
			: (DetInt64)ANGLE_QUARTER - arcTanFixed( ratio );

	if( x >= 0 )
		return (y >= 0) ? fromAxis : -fromAxis;

	return (y >= 0) ? ((DetInt64)ANGLE_HALF - fromAxis) : (fromAxis - (DetInt64)ANGLE_HALF);
}
