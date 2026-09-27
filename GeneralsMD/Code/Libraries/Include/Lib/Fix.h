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

/* Fix.h - integer fixed point for the simulation.
 *
 * Q47.16 in a 64 bit integer: 16 bits of fraction, so the step is 1/65536, and a range of about
 * 1.4e14 either side of zero.  Every operation is integer arithmetic, so the answer does not depend
 * on the compiler, the CPU or the C runtime, which is the whole reason GameLogic wants it.
 *
 * There is deliberately no way in from float and no way out to it here.  A float literal does not
 * convert (the constructors are deleted, which also makes Fix * 0.5f a compile error), and there is
 * no operator float.  Constants are written 0.5_fx, which the compiler reads digit by digit without
 * ever forming a float.  The float boundary, for the client side and for data that arrives as float,
 * is Lib/FixBoundary.h, and GameLogic includes it only where it hands something to the client.
 *
 * Rounding: multiplication rounds to nearest, halves up (towards plus infinity).  Division truncates
 * towards zero.  Division by zero asserts and gives zero; a quotient that does not fit saturates.
 * Addition wraps like the integer it is.
 *
 * Angles are radians, the same as the Real code they replace.
 */

#pragma once

#ifndef _LIB_FIX_H_
#define _LIB_FIX_H_

#include "Lib/BaseType.h"
#include <intrin.h>

class Matrix3D;

class Fix
{
public:
	enum { FRAC_BITS = 16 };
	static constexpr Int64 ONE_RAW = (Int64)1 << FRAC_BITS;

	Fix() = default;																		// uninitialised, like a Real, so it can sit in a union
	constexpr Fix( Int i ) : m_raw( (Int64)i * ONE_RAW ) {}
	Fix( float ) = delete;
	Fix( double ) = delete;

	static constexpr Fix fromRaw( Int64 raw ) { Fix f( 0 ); f.m_raw = raw; return f; }
	constexpr Int64 raw() const { return m_raw; }

	// the whole part, rounded down and towards plus infinity; both stay Fix
	constexpr Fix floor() const { return fromRaw( m_raw & ~(ONE_RAW - 1) ); }
	constexpr Fix ceil() const { return fromRaw( (m_raw + (ONE_RAW - 1)) & ~(ONE_RAW - 1) ); }

	constexpr Fix operator-() const { return fromRaw( -m_raw ); }
	constexpr Fix operator+() const { return *this; }

	friend constexpr Fix operator+( Fix a, Fix b ) { return fromRaw( a.m_raw + b.m_raw ); }
	friend constexpr Fix operator-( Fix a, Fix b ) { return fromRaw( a.m_raw - b.m_raw ); }
	friend Fix operator*( Fix a, Fix b );
	friend Fix operator/( Fix a, Fix b );

	Fix &operator+=( Fix b ) { m_raw += b.m_raw; return *this; }
	Fix &operator-=( Fix b ) { m_raw -= b.m_raw; return *this; }
	Fix &operator*=( Fix b ) { return *this = *this * b; }
	Fix &operator/=( Fix b ) { return *this = *this / b; }

	friend constexpr bool operator==( Fix a, Fix b ) { return a.m_raw == b.m_raw; }
	friend constexpr bool operator!=( Fix a, Fix b ) { return a.m_raw != b.m_raw; }
	friend constexpr bool operator< ( Fix a, Fix b ) { return a.m_raw <  b.m_raw; }
	friend constexpr bool operator<=( Fix a, Fix b ) { return a.m_raw <= b.m_raw; }
	friend constexpr bool operator> ( Fix a, Fix b ) { return a.m_raw >  b.m_raw; }
	friend constexpr bool operator>=( Fix a, Fix b ) { return a.m_raw >= b.m_raw; }

private:
	Int64 m_raw;
};

// ------------------------------------------------------------------------------------------------
// Multiplication: the full 128 bit product, plus half a step, shifted back down.  The shift is
// arithmetic on the whole 128 bits, so a negative product rounds the same way a positive one does.
// ------------------------------------------------------------------------------------------------
inline Fix operator*( Fix a, Fix b )
{
	Int64 hi;
	UnsignedInt64 lo = (UnsignedInt64)_mul128( a.m_raw, b.m_raw, &hi );
	const UnsignedInt64 half = (UnsignedInt64)1 << (Fix::FRAC_BITS - 1);
	UnsignedInt64 sum = lo + half;
	hi += (sum < lo) ? 1 : 0;
	return Fix::fromRaw( (Int64)__shiftright128( sum, (UnsignedInt64)hi, Fix::FRAC_BITS ) );
}

// ------------------------------------------------------------------------------------------------
// The _fx literal.  1.5_fx, 0.1_fx, 1e3_fx, 2.5e-2_fx: the compiler hands the literal over as its
// characters and this reads them as a decimal, so the value is the decimal rounded once to the
// nearest 1/65536, halves away from zero.  Fraction digits past the fourteenth significant one are ignored.
// Anything that does not fit, or is not a decimal literal, is a compile error.
// ------------------------------------------------------------------------------------------------
namespace FixLiteral
{
	inline Int64 notALiteral() { throw "not a decimal literal that fits a Fix"; }

	template <char... C>
	constexpr Int64 parse()
	{
		const char s[] = { C..., '\0' };
		char digits[ sizeof...(C) + 1 ] = {};
		Int count = 0, point = -1, exponent = 0, i = 0;
		for( ; s[ i ] && s[ i ] != 'e' && s[ i ] != 'E'; ++i )
		{
			if( s[ i ] == '.' ) point = count;
			else if( s[ i ] >= '0' && s[ i ] <= '9' ) digits[ count++ ] = s[ i ];
			else if( s[ i ] != '\'' ) return notALiteral();
		}
		if( point < 0 ) point = count;
		if( s[ i ] )
		{
			Bool negative = false;
			++i;
			if( s[ i ] == '-' || s[ i ] == '+' ) negative = (s[ i++ ] == '-');
			for( ; s[ i ]; ++i )
			{
				if( s[ i ] < '0' || s[ i ] > '9' ) return notALiteral();
				exponent = exponent * 10 + (s[ i ] - '0');
				if( exponent > 1000 ) return notALiteral();
			}
			if( negative ) exponent = -exponent;
		}

		// the digit at position k is worth 10^(integerDigits - 1 - k)
		Int integerDigits = point + exponent;
		Int64 whole = 0;
		for( Int k = 0; k < integerDigits; ++k )
		{
			whole = whole * 10 + (k < count ? digits[ k ] - '0' : 0);
			if( whole >= ((Int64)1 << 47) ) return notALiteral();
		}
		Int64 frac = 0, scale = 1;
		// fourteen significant fraction digits at most, so frac * 2^16 fits; leading zeros are free
		for( Int k = integerDigits; k < count && frac < 10000000000000LL && scale < 1000000000000000000LL; ++k )
		{
			frac = frac * 10 + (k >= 0 ? digits[ k ] - '0' : 0);
			scale *= 10;
		}
		Int64 fracRaw = frac * Fix::ONE_RAW / scale;
		if( (frac * Fix::ONE_RAW % scale) * 2 >= scale ) ++fracRaw;
		return whole * Fix::ONE_RAW + fracRaw;
	}
}

template <char... C>
constexpr Fix operator"" _fx()
{
	return Fix::fromRaw( FixLiteral::parse<C...>() );
}

// ------------------------------------------------------------------------------------------------
// Functions.  All integer; sin, cos and atan2 go through DetTrig's tables.
// ------------------------------------------------------------------------------------------------
Fix fixSqrt( Fix x );							///< rounded to the nearest step; a negative argument asserts and gives 0
Fix fixLog2( Fix x );							///< a zero or negative argument asserts and gives 0
Fix fixSin( Fix radians );
Fix fixCos( Fix radians );
Fix fixAtan2( Fix y, Fix x );			///< the angle of (x, y), in (-PI, PI]; atan2(0, 0) is 0
UnsignedInt fixRadiansToTurn( Fix radians );	///< a whole turn is 2^32, so the result wraps for free

inline Fix fixAbs( Fix x ) { return x < Fix( 0 ) ? -x : x; }
inline Fix fixMin( Fix a, Fix b ) { return a < b ? a : b; }
inline Fix fixMax( Fix a, Fix b ) { return a > b ? a : b; }

// ------------------------------------------------------------------------------------------------
// The fixed point twins of Coord2D, Coord3D and Region3D.  toCoord*() is the way out to the client;
// nothing comes back in except through FixBoundary.h.
// ------------------------------------------------------------------------------------------------
struct FCoord2D
{
	Fix x, y;

	void zero() { x = y = Fix( 0 ); }
	void set( Fix ax, Fix ay ) { x = ax; y = ay; }
	Fix lengthSqr() const { return x * x + y * y; }
	Fix length() const { return fixSqrt( lengthSqr() ); }
	Coord2D toCoord2D() const;
};

struct FCoord3D
{
	Fix x, y, z;

	void zero() { x = y = z = Fix( 0 ); }
	void set( Fix ax, Fix ay, Fix az ) { x = ax; y = ay; z = az; }
	void add( const FCoord3D &a ) { x += a.x; y += a.y; z += a.z; }
	void sub( const FCoord3D &a ) { x -= a.x; y -= a.y; z -= a.z; }
	void scale( Fix s ) { x *= s; y *= s; z *= s; }
	Fix lengthSqr() const { return x * x + y * y + z * z; }
	Fix length() const { return fixSqrt( lengthSqr() ); }
	Coord3D toCoord3D() const;
};

struct FRegion3D
{
	FCoord3D lo, hi;

	void zero() { lo.zero(); hi.zero(); }
	Fix width() const { return hi.x - lo.x; }
	Fix height() const { return hi.y - lo.y; }
	Fix depth() const { return hi.z - lo.z; }
	Bool isInRegionNoZ( const FCoord3D &q ) const
	{
		return lo.x < q.x && q.x < hi.x && lo.y < q.y && q.y < hi.y;
	}
	Region3D toRegion3D() const;
};

// ------------------------------------------------------------------------------------------------
/* A rotation and a translation, laid out the way Matrix3D is: three rows of four, the fourth column
	 being the translation.  The rotate calls post-multiply, like Matrix3D::Rotate_X and friends, so a
	 sequence of them reads the same way it does on the float matrix. */
// ------------------------------------------------------------------------------------------------
struct FixMatrix3D
{
	Fix m[ 3 ][ 4 ];

	void makeIdentity();
	void setTranslation( const FCoord3D &t ) { m[ 0 ][ 3 ] = t.x; m[ 1 ][ 3 ] = t.y; m[ 2 ][ 3 ] = t.z; }
	FCoord3D getTranslation() const { FCoord3D t; t.set( m[ 0 ][ 3 ], m[ 1 ][ 3 ], m[ 2 ][ 3 ] ); return t; }
	void rotateX( Fix radians );
	void rotateY( Fix radians );
	void rotateZ( Fix radians );
	FCoord3D transformPoint( const FCoord3D &p ) const;
	FCoord3D rotateVector( const FCoord3D &v ) const;
	void getOrthogonalInverse( FixMatrix3D &inv ) const;		///< the transpose of the rotation, and -R'T
	void toMatrix3D( Matrix3D *out ) const;
};

#endif // _LIB_FIX_H_
