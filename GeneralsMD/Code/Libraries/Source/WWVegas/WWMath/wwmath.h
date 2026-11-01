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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WWMath                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwmath/wwmath.h                              $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 8/26/01 2:22p                                               $*
 *                                                                                             *
 *                    $Revision:: 64                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef WWMATH_H
#define WWMATH_H

#include "always.h"
#include "dettrig.h"
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <string.h>
// Was <emmintrin.h>, for the two cvtss2si conversions below.  Lib/DetRound.h is where the choice
// of rounding now lives, for both architectures and with a test behind it.
#include "Lib/DetRound.h"
#include <assert.h>
#include <float.h>

/*
** Some global constants.
*/
#define WWMATH_EPSILON		0.0001f
#define WWMATH_EPSILON2		WWMATH_EPSILON * WWMATH_EPSILON
#define WWMATH_PI				3.141592654f
#define WWMATH_FLOAT_MAX	(FLT_MAX)
#define WWMATH_FLOAT_MIN	(FLT_MIN)
#define WWMATH_SQRT2			1.414213562f
#define WWMATH_SQRT3			1.732050808f
#define WWMATH_OOSQRT2		0.707106781f
#define WWMATH_OOSQRT3		0.577350269f

// (DRM 05/07/01) Temporarily eliminated _fastcall
// on non-Microsoft compatible compilers. Jani
// should be replacing this soon.
#ifndef _MSC_VER
#define __fastcall
#endif // _MSC_VER

/* 
**	Macros to convert between degrees and radians
*/
#ifndef RAD_TO_DEG
#define RAD_TO_DEG(x)	(((double)x)*180.0/WWMATH_PI)
#endif

#ifndef DEG_TO_RAD
#define DEG_TO_RAD(x)	(((double)x)*WWMATH_PI/180.0)
#endif

#ifndef RAD_TO_DEGF
#define RAD_TO_DEGF(x)	(((float)x)*180.0f/WWMATH_PI)
#endif

#ifndef DEG_TO_RADF
#define DEG_TO_RADF(x)	(((float)x)*WWMATH_PI/180.0f)
#endif


const int ARC_TABLE_SIZE=1024;
const int SIN_TABLE_SIZE=1024;
extern float _FastAcosTable[ARC_TABLE_SIZE];
extern float _FastAsinTable[ARC_TABLE_SIZE];
extern float _FastSinTable[SIN_TABLE_SIZE];
extern float _FastInvSinTable[SIN_TABLE_SIZE];

/*
** Some simple math functions which work on the built-in types.  
** Include the various other header files in the WWMATH library 
** in order to get matrices, quaternions, etc.
*/
class WWMath
{
public:

// Initialization and Shutdown.  Other math sub-systems which require initialization and
// shutdown processing will be handled in these functions
static void			Init(void);
static void			Shutdown(void);

// These are meant to be a collection of small math utility functions to be optimized at some point.
static WWINLINE float Fabs(float val)
{
	int value=*(int*)&val;
	value&=0x7fffffff;
	return *(float*)&value;
}

static WWINLINE int Float_To_Int_Chop(const float& f);
static WWINLINE int Float_To_Int_Floor(const float& f);

static WWINLINE float Cos(float val);
static WWINLINE float Sin(float val);
static WWINLINE float Sqrt(float val);
static float Inv_Sqrt(float a);
static WWINLINE long Float_To_Long(float f);


static WWINLINE float Fast_Sin(float val);
static WWINLINE float Fast_Inv_Sin(float val);
static WWINLINE float Fast_Cos(float val);
static WWINLINE float Fast_Inv_Cos(float val);

static WWINLINE float Fast_Acos(float val);
static WWINLINE float Acos(float val);
static WWINLINE float Fast_Asin(float val);
static WWINLINE float Asin(float val);


// Sin, Cos, Atan, Atan2, Acos and Asin all go through DetTrig, which is the
// same integer table on every machine.  The C runtime is not: it dispatches on
// the host CPU and ships with Windows rather than with the game, and the x87
// FSIN and FCOS instructions that used to be inlined here are microcoded
// differently by Intel and by AMD.  An object's facing round trips through
// Matrix3D::Rotate_Z and Get_Z_Rotation every frame, so those differences land
// straight in the simulation.  See dettrig.h.
static float		Atan(float x) { return DetTrig::ATan(x); }
static float		Atan2(float y,float x) { return DetTrig::ATan2(y,x); }
static float		Sign(float val);
static float		Ceil(float val) { return ceilf(val); }
static float		Floor(float val) { return floorf(val); }
static bool			Fast_Is_Float_Positive(const float & val);
static bool			Is_Power_Of_2(const unsigned int val);

static float		Random_Float(void);
static float		Random_Float(float min,float max);
static float		Clamp(float val, float min = 0.0f, float max = 1.0f);
static double		Clamp(double val, double min = 0.0f, double max = 1.0f);
static int			Clamp_Int(int val, int min_val, int max_val);
static float		Wrap(float val, float min = 0.0f, float max = 1.0f);
static double		Wrap(double val, double min = 0.0f, double max = 1.0f);
static float		Min(float a, float b);
static float		Max(float a, float b);

static int			Float_As_Int(const float f) { return *((int*)&f); }

static float		Lerp(float a, float b, float lerp );
static double		Lerp(double a, double b, float lerp );

static long			Float_To_Long(double f);

static unsigned char Unit_Float_To_Byte(float f) { return (unsigned char)(f*255.0f); }
static float			Byte_To_Unit_Float(unsigned char byte) { return ((float)byte) / 255.0f; }

static bool			Is_Valid_Float(float x);
static bool			Is_Valid_Double(double x);

};

WWINLINE float WWMath::Sign(float val) 
{ 
	if (val > 0.0f) { 
		return +1.0f; 
	} 
	if (val < 0.0f) { 
		return -1.0f; 
	} 
	return 0.0f; 
}

WWINLINE bool WWMath::Fast_Is_Float_Positive(const float & val)
{
	return !((*(int *)(&val)) & 0x80000000);
}

WWINLINE bool WWMath::Is_Power_Of_2(const unsigned int val)
{
	return !((val)&val-1);
}

WWINLINE float WWMath::Random_Float(float min,float max) 
{ 
	return Random_Float() * (max-min) + min; 
}

WWINLINE float WWMath::Clamp(float val, float min /*= 0.0f*/, float max /*= 1.0f*/) 
{
	if(val < min) return min;
	if(val > max) return max;
	return val;
}

WWINLINE double WWMath::Clamp(double val, double min /*= 0.0f*/, double max /*= 1.0f*/) 
{
	if(val < min) return min;
	if(val > max) return max;
	return val;
}

WWINLINE int WWMath::Clamp_Int(int val, int min_val, int max_val) 
{
	if(val < min_val) return min_val;
	if(val > max_val) return max_val;
	return val;
}

WWINLINE float WWMath::Wrap(float val, float min /*= 0.0f*/, float max /*= 1.0f*/) 
{
	// Implemented as an if rather than a while, to long loops
	if ( val >= max )	val -= (max-min);
	if ( val < min )	val += (max-min);

	if ( val < min ) {
		val = min;
	}
	if ( val > max ) {
		val = max;
	}
	return val;
}

WWINLINE double WWMath::Wrap(double val, double min /*= 0.0f*/, double max /*= 1.0f*/) 
{
	// Implemented as an if rather than a while, to long loops
	if ( val >= max )	val -= (max-min);
	if ( val < min )	val += (max-min);
	if ( val < min ) {
		val = min;
	}
	if ( val > max ) {
		val = max;
	}
	return val;
}

WWINLINE float WWMath::Min(float a, float b)
{
	if (a<b) return a;
	return b;
}

WWINLINE float WWMath::Max(float a, float b)
{
	if (a>b) return a;
	return b;
}

WWINLINE float WWMath::Lerp(float a, float b, float lerp )
{
	return (a + (b - a)*lerp);
}

WWINLINE double WWMath::Lerp(double a, double b, float lerp )
{
	return (a + (b - a)*lerp);
}


WWINLINE bool WWMath::Is_Valid_Float(float x)
{
	// The float's own 32 bits.  This read them through an unsigned long, which is 8 bytes on LP64 (macOS,
	// Linux): 4 bytes past x on every call (ASan under ZH_SANITIZE), though the mask kept the answer right.
	uint32_t bits;
	memcpy(&bits, &x, sizeof(bits));
	uint32_t exponent = (bits & 0x7F800000) >> (32-9);

	// if exponent is 0xFF, this is a NAN 
	if (exponent == 0xFF) {
		return false;
	}
	return true;
}

WWINLINE bool WWMath::Is_Valid_Double(double x)
{
	// The high word of the double's 64 bits.  This took it as the second unsigned long, which on LP64 is
	// the 8 bytes after x: the answer did not depend on x at all there.
	uint64_t bits;
	memcpy(&bits, &x, sizeof(bits));
	uint32_t exponent = ((uint32_t)(bits >> 32) & 0x7FF00000) >> (32-12);

	// if exponent is 0x7FF, this is a NAN 
	if (exponent == 0x7FF) {
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------------
// Float to long
// ----------------------------------------------------------------------------

WWINLINE long WWMath::Float_To_Long(float f)
{
	// Not a cast: EA's fistp rounds to nearest even and a C cast truncates, and callers in
	// GameLogic depend on the rounding.  Lib/DetRound.h picks the instruction per architecture -
	// cvtss2si on x64, FCVTNS on arm64 - and Tests/test_detround.cpp holds them to the same table.
	return DetRound::To_Long(f);
}

WWINLINE long WWMath::Float_To_Long(double f)
{
	return DetRound::To_Long(f);
}

// ----------------------------------------------------------------------------
// Cos
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Cos(float val)
{
	return DetTrig::Cos(val);
}

// ----------------------------------------------------------------------------
// Sin
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Sin(float val)
{
	return DetTrig::Sin(val);
}

// ----------------------------------------------------------------------------
// Fast, table based sin
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Sin(float val)
{
	val*=float(SIN_TABLE_SIZE) / (2.0f * WWMATH_PI);

	int idx0=Float_To_Int_Floor(val);
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0 = ((unsigned)idx0) & (SIN_TABLE_SIZE-1);
	idx1 = ((unsigned)idx1) & (SIN_TABLE_SIZE-1);
	
	return (1.0f - frac) * _FastSinTable[idx0] + frac * _FastSinTable[idx1];
}

// ----------------------------------------------------------------------------
// Fast, table based 1.0f/sin
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Inv_Sin(float val)
{
#if 0 // TODO: more testing, not reliable! 
	float index = val * float(SIN_TABLE_SIZE) / (2.0f * WWMATH_PI);

	int idx0=Float_To_Int_Floor(index);
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0 = ((unsigned)idx0) & (SIN_TABLE_SIZE-1);
	idx1 = ((unsigned)idx1) & (SIN_TABLE_SIZE-1);
	
	// The table becomes inaccurate near 0 and 2pi so fall back to doing a divide.
	const int BUFFER = 16;
	if ((idx0 <= BUFFER) || (idx0 >= SIN_TABLE_SIZE-BUFFER-1)) {
		return 1.0f / WWMath::Fast_Sin(val);
	} else {
		return (1.0f - frac) * _FastInvSinTable[idx0] + frac * _FastInvSinTable[idx1];
	}
#else
	return 1.0f / WWMath::Fast_Sin(val);
#endif
}


// ----------------------------------------------------------------------------
// Fast, table based cos
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Cos(float val)
{
	val+=(WWMATH_PI * 0.5f);
	val*=float(SIN_TABLE_SIZE) / (2.0f * WWMATH_PI);

	int idx0=Float_To_Int_Floor(val);
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0 = ((unsigned)idx0) & (SIN_TABLE_SIZE-1);
	idx1 = ((unsigned)idx1) & (SIN_TABLE_SIZE-1);
	
	return (1.0f - frac) * _FastSinTable[idx0] + frac * _FastSinTable[idx1];
}

// ----------------------------------------------------------------------------
// Fast, table based 1.0f/cos
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Inv_Cos(float val)
{
#if 0 // TODO: more testing, not reliable!
	float index = val + (WWMATH_PI * 0.5f);
	index *= float(SIN_TABLE_SIZE) / (2.0f * WWMATH_PI);

	int idx0=Float_To_Int_Chop(index);
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0 = ((unsigned)idx0) & (SIN_TABLE_SIZE-1);
	idx1 = ((unsigned)idx1) & (SIN_TABLE_SIZE-1);
	
	// The table becomes inaccurate near 0 and 2pi so fall back to doing a divide.
	if ((idx0 <= 2) || (idx0 >= SIN_TABLE_SIZE-3)) {
		return 1.0f / WWMath::Fast_Cos(val);
	} else {
		return (1.0f - frac) * _FastInvSinTable[idx0] + frac * _FastInvSinTable[idx1];
	}
#else
	return 1.0f / WWMath::Fast_Cos(val);
#endif
}

// ----------------------------------------------------------------------------
// Fast, table based arc cos
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Acos(float val)
{
	// Near -1 and +1, the table becomes too inaccurate
	if (WWMath::Fabs(val) > 0.975f) {
		return WWMath::Acos(val);
	}

	val*=float(ARC_TABLE_SIZE/2);

	int idx0=Float_To_Int_Floor(val);
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0+=ARC_TABLE_SIZE/2;
	idx1+=ARC_TABLE_SIZE/2;

	// we dont even get close to the edge of the table...
	assert((idx0 >= 0) && (idx0 < ARC_TABLE_SIZE));
	assert((idx1 >= 0) && (idx1 < ARC_TABLE_SIZE));

	// compute and return the interpolated value
	return (1.0f - frac) * _FastAcosTable[idx0] + frac * _FastAcosTable[idx1];
}

// ----------------------------------------------------------------------------
// Arc cos
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Acos(float val)
{
	return DetTrig::ACos(val);
}

// ----------------------------------------------------------------------------
// Fast, table based arc sin
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Fast_Asin(float val)
{
	// Near -1 and +1, the table becomes too inaccurate
	if (WWMath::Fabs(val) > 0.975f) {
		return WWMath::Asin(val);
	}
	
	val*=float(ARC_TABLE_SIZE/2);

	int idx0=Float_To_Int_Floor(val); 
	int idx1=idx0+1;
	float frac=val-(float)idx0;

	idx0+=ARC_TABLE_SIZE/2;
	idx1+=ARC_TABLE_SIZE/2;

	// we dont even get close to the edge of the table...
	assert((idx0 >= 0) && (idx0 < ARC_TABLE_SIZE));
	assert((idx1 >= 0) && (idx1 < ARC_TABLE_SIZE));

	// compute and return the interpolated value
	return (1.0f - frac) * _FastAsinTable[idx0] + frac * _FastAsinTable[idx1];
}

// ----------------------------------------------------------------------------
// Arc sin
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Asin(float val)
{
	return DetTrig::ASin(val);
}

// ----------------------------------------------------------------------------
// Sqrt
// ----------------------------------------------------------------------------

WWINLINE float WWMath::Sqrt(float val)
{
	return (float)sqrt(val);
}

WWINLINE int WWMath::Float_To_Int_Chop(const float& f)
{
	// A cast truncates toward zero on every platform, independently of the FPU rounding mode.
	// The old bit shifts were undefined even at -0.5f. Guard invalid inputs before the cast:
	// INT_MIN matches DetRound's invalid-input sentinel, including on Apple silicon.
	if (f >= -2147483648.0f && f < 2147483648.0f) {
		return static_cast<int>(f);
	}
	return INT_MIN;
}

WWINLINE int WWMath::Float_To_Int_Floor (const float& f)
{
	const int truncated = Float_To_Int_Chop(f);
	// INT_MIN is either the exact lower bound or the invalid-input sentinel. Neither may
	// be decremented; all other negative fractions round down by one from truncation.
	return truncated != INT_MIN && f < static_cast<float>(truncated) ? truncated - 1 : truncated;
}

// ----------------------------------------------------------------------------
// Inverse square root
// ----------------------------------------------------------------------------

// EA had a naked x87 routine here, a Newton-Raphson refinement of the classic bit-trick estimate,
// "some 30% faster than regular C++ compiled".  It was 32-bit assembly and this is what the build
// has always compiled everywhere else.
WWINLINE float WWMath::Inv_Sqrt(float val)
{
	return 1.0f / (float)sqrt(val);
}


#endif
