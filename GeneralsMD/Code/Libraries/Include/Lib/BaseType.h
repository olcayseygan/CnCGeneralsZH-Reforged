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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: BaseType.h ///////////////////////////////////////////////////////////
//
// Project:  RTS3
//
// Basic types and constants
// Author: Michael S. Booth, January 1995, September 2000
//
///////////////////////////////////////////////////////////////////////////////

// tell the compiler to only load this file once
#pragma once


#ifndef _BASE_TYPE_H_
#define _BASE_TYPE_H_

#include <math.h>
#include <string.h>

#include "Platform/MsvcFloatCasts.h"

/*
**	Turn off some unneeded warnings.
**	Within the windows headers themselves, Microsoft has disabled the warnings 4290, 4514, 
**	4069, 4200, 4237, 4103, 4001, 4035, 4164. Makes you wonder, eh?
*/

// "unreferenced inline function has been removed" Yea, so what?
#pragma warning(disable : 4514)

// Unreferenced local function removed.
#pragma warning(disable : 4505)

// 'unreferenced formal parameter'
#pragma warning(disable : 4100)

// 'identifier was truncated to '255' characters in the browser information':
// Tempates create LLLOOONNNGGG identifiers!
#pragma warning(disable : 4786)

// 'function selected for automatic inline expansion'.  Cool, but since we're treating
// warnings as errors, don't warn me about this!
#pragma warning(disable : 4711)

#if 0
// 'assignment within condition expression'. actually a pretty useful warning, 
// but way too much existing code violates it.
//#pragma warning(disable : 4706)
#else
// actually, it turned out not to be too bad, so this is now ENABLED. (srj)
#pragma warning(error : 4706)
#endif

// 'conditional expression is constant'. used lots in debug builds.
#pragma warning(disable : 4127)

// 'nonstandard extension used : nameless struct/union'. MS headers violate this...
#pragma warning(disable : 4201)

// 'unreachable code'. STL violates this...
#pragma warning(disable : 4702)

// 'local variable is initialized but not referenced'. good thing to know about...
#pragma warning(error : 4189)

// 'unreferenced local variable'. good thing to know about...
#pragma warning(error : 4101)

#ifndef PI
#define PI     3.14159265359f
#define TWO_PI 6.28318530718f
#endif

#ifndef NULL
//#define NULL ((void *)0)
#define NULL 0						// C++ doesn't like casting void *'s into other pointers
#endif

// MSVC math.h defines overloaded functions with this name...
//#ifndef abs
//#define abs(x) (((x) < 0) ? -(x) : (x))
//#endif

// The min and max macros below are function-like, so every later declaration or call spelled
// `max(` expands through them - including the C++ library's own std::min/std::max and the headers
// that call them.  MSVC's library and libc++ guard against that; libstdc++ does not, and the first
// engine source built against it (on Linux) stopped in <bits/algorithmfwd.h> with "macro max passed
// 3 arguments".  So off MSVC, the standard headers this tree's headers include are parsed here,
// before the macros exist, and their include guards keep them from being parsed again under them.
// A header this list misses fails loudly the same way, never silently.
// Keyed on the COMPILER'S LIBRARY, hence !_MSC_VER and not !_WIN32: the question is whose <algorithm>
// this is, not which operating system runs it.  mingw is Windows with libstdc++ and needs the list.
#if !defined(_MSC_VER) && defined(__cplusplus)
#include <algorithm>
#include <atomic>
#include <bitset>
#include <chrono>
#include <deque>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#endif

#ifndef min
#define min(x,y) (((x)<(y)) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) (((x)>(y)) ? (x) : (y))
#endif

#ifndef TRUE
#define TRUE true
#endif

#ifndef FALSE
#define FALSE false
#endif

// Elements in an array
#ifndef ELEMENTS_OF
#define ELEMENTS_OF( x ) ( sizeof( x ) / sizeof( x[0] ) )
#endif

//--------------------------------------------------------------------
// Fundamental type definitions
//--------------------------------------------------------------------
#include <stdint.h>   // int64_t and uint64_t, for Int64/UnsignedInt64 below
// __forceinline on the four inline helpers further down, and the CRT spellings for everything
// that reaches this header - which is most of GameEngine, and all of compression.  always.h
// brings the same header to WWVegas; this is the other door into it.
#include "Platform/MSVCCompat.h"
typedef float							Real;							// 4 bytes 
typedef int								Int;							// 4 bytes 
typedef unsigned int			UnsignedInt;	  	// 4 bytes
// 4 bytes on Win32, 8 on x64: for the places that carry a pointer through an integer, which the
// GUI message payloads and a few handle typedefs do.
typedef size_t						UnsignedIntPtr;		// pointer sized
typedef unsigned short		UnsignedShort;		// 2 bytes 
typedef short							Short;					  // 2 bytes 
typedef unsigned char			UnsignedByte;			// 1 byte		USED TO BE "Byte"
typedef char							Byte;							// 1 byte		USED TO BE "SignedByte"
typedef char							Char;							// 1 byte of text
typedef bool							Bool;							// 
// The note that used to be here said MSVC did not support "long long" yet.  It has since 2003, and
// <cstdint> is the spelling that is the same width on every compiler this builds on - which matters
// because these two are in save games and in the replay checksum.
typedef int64_t						Int64;							// 8 bytes 
typedef uint64_t					UnsignedInt64;	  	// 8 bytes 

#include "Lib/Trig.h"

//-----------------------------------------------------------------------------
#include "Lib/WideChar.h"	// WideChar: its own header, so WW3D2 can have it without this one

//-----------------------------------------------------------------------------
template <typename NUM>
inline NUM sqr(NUM x)
{
	return x*x;
}

template <typename NUM>
inline NUM clamp(NUM lo, NUM val, NUM hi)
{
	if (val < lo) return lo;
	else if (val > hi) return hi;
	else return val;
}

template <typename NUM>
inline int sign(NUM x)
{
	if (x > 0) return 1;
	else if (x < 0) return -1;
	else return 0;
}

//-----------------------------------------------------------------------------
inline Real rad2deg(Real rad) { return rad * (180/PI); }
inline Real deg2rad(Real rad) { return rad * (PI/180); }

//-----------------------------------------------------------------------------
// For twiddling bits
//-----------------------------------------------------------------------------
// winnt.h claims BitTest for the _bittest intrinsic.  Pull it in here (windef.h
// is the smallest header that reaches it) so its include guard is already
// closed, then take the name back: otherwise any header that gets to winnt.h
// after this one silently rewrites every use below into an intrinsic that takes
// a LONG* and the game's bit fields stop compiling.  Not <windows.h>: that also
// drags in winsock.h, and the device code includes winsock2.h.
//
// None of that fight exists off Windows: there is no winnt.h to claim the name, so there is
// nothing to pull in early and nothing to take back.  The #undef below is kept unguarded
// because undefining a macro that was never defined is well-formed and it keeps the four
// definitions that follow reading the same on both platforms.  B5.
#if defined(_WIN32)
#if defined(_M_ARM64)
#ifndef _ARM64_
#define _ARM64_		// the same for Windows on Arm: _AMD64_ there takes winnt.h down the x64 path
#endif
#else
#ifndef _AMD64_
#define _AMD64_		// windows.h does this before it reaches windef.h, and winnt.h
#endif				// #errors with "No Target Architecture" without it
#endif
#include <windef.h>
#endif
#include <string.h>
// Was <emmintrin.h>, for fast_float2long_round below.  See Lib/DetRound.h for which
// instruction each architecture uses and why the rounding is not negotiable.
#include "Lib/DetRound.h"
#undef BitTest
#define BitTest( x, i ) ( ( (x) & (i) ) != 0 )
#define BitSet( x, i ) ( (x) |= (i) )
#define BitClear( x, i ) ( (x ) &= ~(i) )
#define BitToggle( x, i ) ( (x) ^= (i) )

//-------------------------------------------------------------------------------------------------

// note, this function depends on the cpu rounding mode, which we set to CHOP every frame, 
// but apparently tends to be left in unpredictable modes by various system bits of
// code, so use this function with caution -- it might not round in the way you want.
//
// EA's fld/fistp pair, written as cvtss2si: both round in the current mode, which setFPMode pins
// to nearest.
// <emmintrin.h> used to be included up in the BitTest block, which had nothing to do with it.
// It is here, next to its only user, and guarded: the header #errors outright on a non-x86
// target ("This header is only meant to be used on x86 and x64 architecture").  The function
// body below is still SSE2 and still does not compile on arm64 - that is B12's, not B5's.
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#endif
__forceinline long fast_float2long_round(float f)
{
	return DetRound::To_Long(f);
}

// super fast float trunc routine, works always (independent of any FPU modes)
// code courtesy of Martin Hoffesommer (grin)
//
// The same bits EA's asm produced, in C: a mask of 0xff800000 shifted arithmetically right by
// (exponent - 127) clears the fraction, and an exponent below 127 clears everything.  The shift
// count keeps the asm's wrap at 32 (sar only reads the low five bits of cl) for the huge values
// where that matters, so no input changes answer.
__forceinline float fast_float_trunc(float f)
{
  unsigned int bits;
  memcpy(&bits, &f, sizeof(bits));
  const unsigned int exponent = (bits >> 23) & 0xff;
  const int mask = exponent < 127 ? 0 : (int)0xff800000 >> ((exponent - 127) & 31);
  bits &= (unsigned int)mask;
  memcpy(&f, &bits, sizeof(f));
  return f;
}

/* floor and ceil, off the truncation above.
 *
 * EA's versions nudged the value by 0.99999994 (the largest float below one) in the direction they
 * wanted and then truncated.  That is exact only while the nudge survives the addition, and it does
 * not: for any f of 2 or more the sum f + 0.99999994 has no representable neighbour that close, so
 * it rounds to f + 1 and the "ceil" of an exact integer came back one too high.  ceil(10.0f) was 11.
 *
 * Nobody noticed in the simulation, where the arguments are cell arithmetic on positions that are
 * almost never whole - but every countdown on screen is a whole number of seconds by construction,
 * so the build time over a war factory and the seconds over a structure going up were both a second
 * long.
 *
 * Truncation already rounds toward zero, so the correction is one comparison and no magic constant.
 *
 * Pinned by ceil_and_floor_are_exact_on_whole_numbers in test_gameengine.cpp. */

// same here, fast floor function
__forceinline float fast_float_floor(float f)
{
  float t = fast_float_trunc(f);
  return (t > f) ? t - 1.0f : t;
}

// same here, fast ceil function
__forceinline float fast_float_ceil(float f)
{
  float t = fast_float_trunc(f);
  return (t < f) ? t + 1.0f : t;
}

//-------------------------------------------------------------------------------------------------

/* Float to integer, and it is a plain cast.
 *
 * EA wrote these as fast_float2long_round(fast_float_trunc(x)): mask the fractional mantissa bits
 * off with a block of inline assembly, then push the result through the x87 stack with fld/fistp.
 * In 1999 that beat the compiler, which had to spill the FPU control word twice to honour C's
 * round-toward-zero rule.  On a machine with SSE2 - which is every machine that can run this, and
 * which is what MSVC targets by default for x86 - a cast to Int is a single cvttss2si that already
 * truncates toward zero, and the two-step version is a memory round trip, an x87 stack push and an
 * x87 stack pop to reach the same answer.
 *
 * The cost is not just the instructions.  These macros sit in a header the whole game includes, and
 * an __asm block in an inlined function stops MSVC optimizing across it: every expression in this
 * file's neighbourhood - and REAL_TO_INT alone has 235 call sites, most of them in the simulation -
 * was compiled with one hand tied.  It is also where the fast_float_trunc register clobber lived,
 * the one that ended every run at the main menu until the "xor ebx,ebx" was found.
 *
 * The result is identical for every value that fits in the destination, and the intermediate cast
 * to Int is kept where the destination is narrower so the two-step narrowing is exactly the one the
 * long-returning original did.  Better than identical in one respect: fast_float2long_round rounds
 * by the FPU's current mode, which the comment on it warns "tends to be left in unpredictable modes
 * by various system bits of code" - it was only safe here because fast_float_trunc had already made
 * the value integral.  cvttss2si has no mode to be left in.  The trunc/floor/ceil helpers below are
 * untouched; they still have callers that want a Real back.
 *
 * Pinned by real_to_int_agrees_with_the_assembly_it_replaced in test_gameengine.cpp.
 *
 * The cast is MSVC's cvttss2si written out (Platform/MsvcFloatCasts.h), not the C cast itself.  In range
 * the two are the same instruction's answer.  Out of range or NaN, C leaves the cast undefined; MSVC and
 * x86 give INT_MIN, while ARM64 saturates and gives 0 for NaN.  The helper gives INT_MIN everywhere, so
 * every one of these macros is Windows' answer by construction (found by a sweep of the float conversions). */

#define REAL_TO_INT(x)						((Int)floatToIntAsMsvc(x))
#define REAL_TO_UNSIGNEDINT(x)		((UnsignedInt)floatToIntAsMsvc(x))
#define REAL_TO_SHORT(x)					((Short)floatToIntAsMsvc(x))
#define REAL_TO_UNSIGNEDSHORT(x)	((UnsignedShort)floatToIntAsMsvc(x))
#define REAL_TO_BYTE(x)						((Byte)floatToIntAsMsvc(x))
#define REAL_TO_UNSIGNEDBYTE(x)		((UnsignedByte)floatToIntAsMsvc(x))
#define REAL_TO_CHAR(x)						((Char)floatToIntAsMsvc(x))
#define DOUBLE_TO_REAL(x)					((Real) (x))
#define DOUBLE_TO_INT(x)					((Int)(x))
#define INT_TO_REAL(x)						((Real) (x))

// once we've ceiled/floored, trunc and round are identical, and currently, round is faster... (srj)
#define REAL_TO_INT_CEIL(x)				((Int)fast_float2long_round(fast_float_ceil(x)))
#define REAL_TO_INT_FLOOR(x)			((Int)fast_float2long_round(fast_float_floor(x)))

#define FAST_REAL_TRUNC(x)        fast_float_trunc(x)
#define FAST_REAL_CEIL(x)         fast_float_ceil(x)
#define FAST_REAL_FLOOR(x)        fast_float_floor(x)

//
// Element count of a real array.  Four files each defined their own copy of this, and the bounded
// string calls need it at every site, so it lives here now.  It is deliberately the array form
// only: hand it a pointer and the division is silently wrong, so never use it on a parameter that
// has decayed.
//
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)             (sizeof(a) / sizeof((a)[0]))
#endif

//--------------------------------------------------------------------
// Derived type definitions
//--------------------------------------------------------------------

// NOTE: Keep these derived types simple, and avoid constructors and destructors
// so they can be used within unions.

// real-valued range defined by low and high values
struct RealRange 
{
	Real lo, hi;							// low and high values of the range

	// combine the given range with us such that we now encompass
	// both ranges
	void combine( RealRange &other )
	{
		lo = min( lo, other.lo );
		hi = max( hi, other.hi );
	}
};

struct Coord2D 
{
	Real x, y;

	Real length( void ) const { return (Real)sqrt( x*x + y*y ); }

	void normalize( void )
	{
		Real len = length();
		if( len != 0 )
		{
			x /= len;
			y /= len;
		}
	}
	
	Real toAngle( void ) const;  ///< turn 2D vector into angle (where angle 0 is down the +x axis)

};

inline Real Coord2D::toAngle( void ) const
{
	const Real len = length();
	if (len == 0.0f)
		return 0.0f;

	Real c = x/len;
	// bound it in case of numerical error
	if (c < -1.0f)
		c = -1.0f;
	else if (c > 1.0f)
		c = 1.0f;

	return y < 0.0f ? -ACos(c) : ACos(c);
}  // end toAngle

struct ICoord2D 
{
	Int x, y;

	Int length( void ) const { return (Int)sqrt( (double)(x*x + y*y) ); }
};

struct Region2D
{
	Coord2D lo, hi;						// bounds of 2D rectangular region

	Real width( void ) const { return hi.x - lo.x; }
	Real height( void ) const { return hi.y - lo.y; }
};

struct IRegion2D
{
	ICoord2D lo, hi;					// bounds of 2D rectangular region

	Int width( void ) const { return hi.x - lo.x; }
	Int height( void ) const { return hi.y - lo.y; }
};


struct Coord3D 
{
	Real x, y, z;

	Real length( void ) const { return (Real)sqrt( x*x + y*y + z*z ); }
	Real lengthSqr( void ) const { return ( x*x + y*y + z*z ); }

	void normalize( void )
	{
		Real len = length();

		if( len != 0 )
		{
			x /= len;
			y /= len;
			z /= len;
		}
	}
	
	static void crossProduct( const Coord3D *a, const Coord3D *b, Coord3D *r )
	{
		r->x = (a->y * b->z - a->z * b->y);
		r->y = (a->z * b->x - a->x * b->z);
		r->z = (a->x * b->y - a->y * b->x);
	}
	
	void zero( void )
	{
		x = 0.0f;
		y = 0.0f;
		z = 0.0f;
	}

	void add( const Coord3D *a )
	{
		x += a->x;
		y += a->y;
		z += a->z;
	}
	
	void sub( const Coord3D *a )
	{
		x -= a->x;
		y -= a->y;
		z -= a->z;
	}
	
	void set( const Coord3D *a )
	{
		x = a->x;
		y = a->y;
		z = a->z;
	}
	
	void set( Real ax, Real ay, Real az )
	{
		x = ax;
		y = ay;
		z = az;
	}

	void scale( Real scale )
	{
		x *= scale;
		y *= scale;
		z *= scale;
	}

	Bool equals( const Coord3D &r )
	{
		return (x == r.x && 
						y == r.y &&
						z == r.z);
	}

	Bool operator==( const Coord3D &r )
	{
		return (x == r.x &&
						y == r.y &&
						z == r.z);
	}
};

struct ICoord3D 
{
	Int x, y, z;

	Int length( void ) const { return (Int)sqrt( (double)(x*x + y*y + z*z) ); }
	void zero( void )
	{

		x = 0;
		y = 0;
		z = 0;
	}
};

struct Region3D
{
	Coord3D lo, hi;						// axis-aligned bounding box

	Real width( void ) const { return hi.x - lo.x; }
	Real height( void ) const { return hi.y - lo.y; }
	Real depth( void ) const { return hi.z - lo.z; }

	void zero() { lo.zero(); hi.zero(); }
	Bool isInRegionNoZ( const Coord3D *query ) const
	{
		return (lo.x < query->x) && (query->x < hi.x) 
						&& (lo.y < query->y) && (query->y < hi.y);
	}
	Bool isInRegionWithZ( const Coord3D *query ) const
	{
		return (lo.x < query->x) && (query->x < hi.x) 
						&& (lo.y < query->y) && (query->y < hi.y)
						&& (lo.z < query->z) && (query->z < hi.z);
	}
};

struct IRegion3D
{
	ICoord3D lo, hi;					// axis-aligned bounding box

	Int width( void ) const { return hi.x - lo.x; }
	Int height( void ) const { return hi.y - lo.y; }
	Int depth( void ) const { return hi.z - lo.z; }
};


struct RGBColor
{
	Real red, green, blue;		// range between 0 and 1

	inline Int getAsInt() const
	{
		return
			((Int)(red * 255.0) << 16) |
			((Int)(green * 255.0) << 8) |
			((Int)(blue * 255.0) << 0);
	}

	inline void setFromInt(Int c)
	{
		red = ((c >> 16) & 0xff) / 255.0f;
		green = ((c >>  8) & 0xff) / 255.0f;
		blue = ((c >>  0) & 0xff) / 255.0f;
	}

};

struct RGBAColorReal
{

	Real red, green, blue, alpha;  // range between 0.0 and 1.0

};

struct RGBAColorInt
{

	UnsignedInt red, green, blue, alpha;  // range between 0 and 255

};

#endif // _BASE_TYPE_H_
