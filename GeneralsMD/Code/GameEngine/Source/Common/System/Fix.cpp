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

// Fix.cpp - the out of line half of Lib/Fix.h and Lib/FixBoundary.h.  No float is computed in
// here except in the to*() conversions at the very end, which are the way out to the client.

#include "PreRTS.h"

#include "Lib/Fix.h"
#include "Lib/FixBoundary.h"
#include "Common/Debug.h"

#include "dettrig.h"
#include "WWMath/matrix3d.h"

static const Int64 FIX_MAX_RAW = 0x7FFFFFFFFFFFFFFFLL;

static UnsignedInt64 magnitudeOf( Int64 raw )
{
	return raw < 0 ? 0ull - (UnsignedInt64)raw : (UnsignedInt64)raw;
}

// ------------------------------------------------------------------------------------------------
/* Truncating division on the magnitudes.  |a| * 2^16 / |b| fits 63 bits exactly when
	 |a| / 2^47 < |b|, so that is the overflow test, and it is also what keeps the high half of the
	 dividend below the divisor as _udiv128 requires. */
// ------------------------------------------------------------------------------------------------
Fix operator/( Fix a, Fix b )
{
	if( b.m_raw == 0 )
	{
		DEBUG_CRASH(( "Fix: division by zero\n" ));
		return Fix( 0 );
	}

	UnsignedInt64 ua = magnitudeOf( a.m_raw );
	UnsignedInt64 ub = magnitudeOf( b.m_raw );
	Bool negative = (a.m_raw < 0) != (b.m_raw < 0);

	if( (ua >> 47) >= ub )
	{
		DEBUG_CRASH(( "Fix: quotient out of range\n" ));
		return Fix::fromRaw( negative ? -FIX_MAX_RAW : FIX_MAX_RAW );
	}

	UnsignedInt64 remainder;
	UnsignedInt64 q = _udiv128( ua >> (64 - Fix::FRAC_BITS), ua << Fix::FRAC_BITS, ub, &remainder );
	return Fix::fromRaw( negative ? -(Int64)q : (Int64)q );
}

// ------------------------------------------------------------------------------------------------
/* Newton's iteration on the 128 bit value raw * 2^16, which is the square root's raw value
	 squared.  Starting above the root it falls monotonically to the floor of it; the last step then
	 rounds, since (r + 1/2)^2 = r^2 + r + 1/4 and the value is an integer. */
// ------------------------------------------------------------------------------------------------
Fix fixSqrt( Fix x )
{
	if( x.raw() <= 0 )
	{
		DEBUG_ASSERTCRASH( x.raw() == 0, ( "Fix: square root of a negative number\n" ) );
		return Fix( 0 );
	}

	UnsignedInt64 lo = (UnsignedInt64)x.raw() << Fix::FRAC_BITS;
	UnsignedInt64 hi = (UnsignedInt64)x.raw() >> (64 - Fix::FRAC_BITS);

	unsigned long top;
	Int bits = hi ? (_BitScanReverse64( &top, hi ), (Int)top + 65) : (_BitScanReverse64( &top, lo ), (Int)top + 1);
	UnsignedInt64 r = (UnsignedInt64)1 << ((bits + 1) / 2);

	for( ;; )
	{
		UnsignedInt64 remainder;
		UnsignedInt64 next = (r + _udiv128( hi, lo, r, &remainder )) >> 1;
		if( next >= r )
			break;
		r = next;
	}

	UnsignedInt64 squareHi;
	UnsignedInt64 squareLo = _umul128( r, r, &squareHi );
	UnsignedInt64 restLo = lo - squareLo;
	UnsignedInt64 restHi = hi - squareHi - (lo < squareLo ? 1 : 0);
	if( restHi != 0 || restLo > r )
		++r;

	return Fix::fromRaw( (Int64)r );
}

// ------------------------------------------------------------------------------------------------
/* The whole part is where the highest set bit is.  The fraction comes one bit at a time from
	 squaring the mantissa, held in [1, 2) with 62 bits of fraction: each squaring doubles the
	 logarithm, and whenever the square reaches 2 the next bit is a one.  Seventeen bits, rounded to
	 sixteen. */
// ------------------------------------------------------------------------------------------------
Fix fixLog2( Fix x )
{
	if( x.raw() <= 0 )
	{
		DEBUG_CRASH(( "Fix: log2 of a number that is not positive\n" ));
		return Fix( 0 );
	}

	unsigned long top;
	_BitScanReverse64( &top, (UnsignedInt64)x.raw() );

	UnsignedInt64 m = top <= 62 ? (UnsignedInt64)x.raw() << (62 - top) : (UnsignedInt64)x.raw() >> (top - 62);
	Int64 fraction = 0;
	for( Int i = 0; i < Fix::FRAC_BITS + 1; ++i )
	{
		UnsignedInt64 hi;
		UnsignedInt64 lo = _umul128( m, m, &hi );
		m = __shiftright128( lo, hi, 62 );
		fraction <<= 1;
		if( m >= ((UnsignedInt64)1 << 63) )
		{
			m >>= 1;
			fraction |= 1;
		}
	}

	return Fix::fromRaw( ((Int64)top - Fix::FRAC_BITS) * Fix::ONE_RAW + ((fraction + 1) >> 1) );
}

// ------------------------------------------------------------------------------------------------
/* raw * 2^16 / (2 PI) turns, which is raw * (2^64 / PI) / 2^49, rounded.  The constant is
	 2^64 / PI to the nearest integer. */
// ------------------------------------------------------------------------------------------------
UnsignedInt fixRadiansToTurn( Fix radians )
{
	const Int64 TWO_64_OVER_PI = 0x517CC1B727220A95LL;
	Int64 hi;
	UnsignedInt64 lo = (UnsignedInt64)_mul128( radians.raw(), TWO_64_OVER_PI, &hi );
	UnsignedInt64 sum = lo + ((UnsignedInt64)1 << 48);
	hi += (sum < lo) ? 1 : 0;
	return (UnsignedInt)__shiftright128( sum, (UnsignedInt64)hi, 49 );
}

// the tables answer in 2^24ths; round to 2^16ths
static Fix fromTrigValue( int value )
{
	return Fix::fromRaw( ((Int64)value + 128) >> 8 );
}

Fix fixSin( Fix radians ) { return fromTrigValue( DetTrig::SinTurn( fixRadiansToTurn( radians ) ) ); }
Fix fixCos( Fix radians ) { return fromTrigValue( DetTrig::CosTurn( fixRadiansToTurn( radians ) ) ); }

// ------------------------------------------------------------------------------------------------
// turns * 2 PI / 2^32 radians, in 2^16ths: turns * PI / 2^15 = turns * (PI * 2^61) / 2^76, rounded.
// ------------------------------------------------------------------------------------------------
Fix fixAtan2( Fix y, Fix x )
{
	const Int64 PI_Q61 = 0x6487ED5110B4611ALL;
	Int64 hi;
	_mul128( DetTrig::ATan2Turn( y.raw(), x.raw() ), PI_Q61, &hi );
	// both the half step (bit 75) and the shift (76) are above the low word, which cannot carry
	// into either, so the high word alone decides the result
	return Fix::fromRaw( (hi + ((Int64)1 << (75 - 64))) >> (76 - 64) );
}

// ------------------------------------------------------------------------------------------------
// fixFromFloatBits: see FixBoundary.h.  value = mantissa * 2^(exponent - 150), so the raw value is
// mantissa * 2^(exponent - 134).
// ------------------------------------------------------------------------------------------------
Fix fixFromFloatBits( UnsignedInt bits )
{
	Bool negative = (bits >> 31) != 0;
	Int exponent = (Int)((bits >> 23) & 0xFF);
	UnsignedInt64 mantissa = bits & 0x7FFFFF;

	if( exponent == 0xFF && mantissa != 0 )
	{
		DEBUG_CRASH(( "Fix: NaN crossed into the simulation\n" ));
		return Fix( 0 );
	}

	if( exponent == 0 )
		exponent = 1;					// subnormal: no hidden bit, same scale as the smallest normal
	else
		mantissa |= 0x800000;

	Int shift = exponent - 134;
	UnsignedInt64 magnitude;
	if( shift >= 0 )
	{
		// the mantissa is below 2^24, so 39 is the last shift that stays below 2^63; infinity lands
		// here too
		if( shift > 39 )
			return Fix::fromRaw( negative ? -FIX_MAX_RAW : FIX_MAX_RAW );
		magnitude = mantissa << shift;
	}
	else if( -shift >= 25 )
	{
		magnitude = 0;					// under half a step, since the mantissa is below 2^24
	}
	else
	{
		Int right = -shift;
		UnsignedInt64 half = (UnsignedInt64)1 << (right - 1);
		UnsignedInt64 rest = mantissa & ((half << 1) - 1);
		magnitude = mantissa >> right;
		if( rest > half || (rest == half && (magnitude & 1)) )
			++magnitude;
	}

	return Fix::fromRaw( negative ? -(Int64)magnitude : (Int64)magnitude );
}

// ------------------------------------------------------------------------------------------------
// FixMatrix3D.  Each product is rounded on its own, in a fixed order.
// ------------------------------------------------------------------------------------------------
void FixMatrix3D::makeIdentity()
{
	for( Int i = 0; i < 3; ++i )
		for( Int j = 0; j < 4; ++j )
			m[ i ][ j ] = Fix( i == j ? 1 : 0 );
}

void FixMatrix3D::rotateX( Fix radians )
{
	Fix s = fixSin( radians ), c = fixCos( radians );
	for( Int i = 0; i < 3; ++i )
	{
		Fix a = m[ i ][ 1 ], b = m[ i ][ 2 ];
		m[ i ][ 1 ] = c * a + s * b;
		m[ i ][ 2 ] = c * b - s * a;
	}
}

void FixMatrix3D::rotateY( Fix radians )
{
	Fix s = fixSin( radians ), c = fixCos( radians );
	for( Int i = 0; i < 3; ++i )
	{
		Fix a = m[ i ][ 0 ], b = m[ i ][ 2 ];
		m[ i ][ 0 ] = c * a - s * b;
		m[ i ][ 2 ] = s * a + c * b;
	}
}

void FixMatrix3D::rotateZ( Fix radians )
{
	Fix s = fixSin( radians ), c = fixCos( radians );
	for( Int i = 0; i < 3; ++i )
	{
		Fix a = m[ i ][ 0 ], b = m[ i ][ 1 ];
		m[ i ][ 0 ] = c * a + s * b;
		m[ i ][ 1 ] = c * b - s * a;
	}
}

FCoord3D FixMatrix3D::rotateVector( const FCoord3D &v ) const
{
	FCoord3D out;
	out.x = m[ 0 ][ 0 ] * v.x + m[ 0 ][ 1 ] * v.y + m[ 0 ][ 2 ] * v.z;
	out.y = m[ 1 ][ 0 ] * v.x + m[ 1 ][ 1 ] * v.y + m[ 1 ][ 2 ] * v.z;
	out.z = m[ 2 ][ 0 ] * v.x + m[ 2 ][ 1 ] * v.y + m[ 2 ][ 2 ] * v.z;
	return out;
}

FCoord3D FixMatrix3D::transformPoint( const FCoord3D &p ) const
{
	FCoord3D out = rotateVector( p );
	out.add( getTranslation() );
	return out;
}

void FixMatrix3D::getOrthogonalInverse( FixMatrix3D &inv ) const
{
	for( Int i = 0; i < 3; ++i )
		for( Int j = 0; j < 3; ++j )
			inv.m[ i ][ j ] = m[ j ][ i ];

	FCoord3D t = inv.rotateVector( getTranslation() );
	t.scale( Fix( -1 ) );
	inv.setTranslation( t );
}

// ------------------------------------------------------------------------------------------------
// The way out to the client.
// ------------------------------------------------------------------------------------------------
void FixMatrix3D::toMatrix3D( Matrix3D *out ) const
{
	out->Set( fixToReal( m[ 0 ][ 0 ] ), fixToReal( m[ 0 ][ 1 ] ), fixToReal( m[ 0 ][ 2 ] ), fixToReal( m[ 0 ][ 3 ] ),
						fixToReal( m[ 1 ][ 0 ] ), fixToReal( m[ 1 ][ 1 ] ), fixToReal( m[ 1 ][ 2 ] ), fixToReal( m[ 1 ][ 3 ] ),
						fixToReal( m[ 2 ][ 0 ] ), fixToReal( m[ 2 ][ 1 ] ), fixToReal( m[ 2 ][ 2 ] ), fixToReal( m[ 2 ][ 3 ] ) );
}

FixMatrix3D fixMatrixFromMatrix3D( const Matrix3D &in )
{
	FixMatrix3D out;
	for( Int i = 0; i < 3; ++i )
		for( Int j = 0; j < 4; ++j )
			out.m[ i ][ j ] = fixFromReal( in[ i ][ j ] );
	return out;
}

Coord2D FCoord2D::toCoord2D() const
{
	Coord2D c;
	c.x = fixToReal( x );
	c.y = fixToReal( y );
	return c;
}

Coord3D FCoord3D::toCoord3D() const
{
	Coord3D c;
	c.x = fixToReal( x );
	c.y = fixToReal( y );
	c.z = fixToReal( z );
	return c;
}

Region3D FRegion3D::toRegion3D() const
{
	Region3D r;
	r.lo = lo.toCoord3D();
	r.hi = hi.toCoord3D();
	return r;
}
