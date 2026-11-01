/*
 * wwmath coverage.
 *
 * Priorities, in order:
 *   1. Anything reimplemented during the port (Matrix3D::Get_Inverse, which
 *      replaced D3DXMatrixInverse) - these can silently produce wrong numbers.
 *   2. Anything with 32-bit inline __asm (WWMath::Inv_Sqrt, Float_To_Long,
 *      the whole VectorProcessorClass) - the asm is the reason this port is
 *      Win32-only, and it is exactly what a compiler upgrade can miscompile.
 *   3. The geometry predicates the renderer and collision system stand on.
 *
 * Where an answer is analytically known it is asserted against the closed
 * form, not against whatever the code happens to return today.
 */
#include "test_harness.h"

#include <limits>
#include <cmath>
#include <stdint.h>
#include <string.h>

#include "wwmath.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "matrix3.h"
#include "matrix3d.h"
#include "matrix4.h"
#include "quat.h"
#include "euler.h"
#include "aabox.h"
#include "obbox.h"
#include "sphere.h"
#include "plane.h"
#include "aaplane.h"
#include "tri.h"
#include "lineseg.h"
#include "colmath.h"
/* Several Overlap_Test overloads (box/point, box/box, plane/point) are WWINLINE
   in these two headers rather than compiled into the lib, so anything calling
   AABoxClass::Contains needs them or it fails at link time. */
#include "colmathaabox.h"
#include "colmathplane.h"
#include "colmathinlines.h"
#include "pot.h"
#include "vp.h"
#include "castres.h"
#include "dettrig.h"

static const float EPS = 1e-4f;

/* Matrix3D has no operator*(Vector3) - transforming a point goes through the
   static Transform_Vector.  Wrap it so the tests read like the maths. */
static Vector3 xform(const Matrix3D &m, const Vector3 &v)
{
	Vector3 out;
	Matrix3D::Transform_Vector(m, v, &out);
	return out;
}

/* WWMath::Init builds the Fast_Sin/Fast_Acos lookup tables; without it the
   Fast_* family returns garbage.  Run it once, lazily, from every test that
   needs it rather than relying on static-init order. */
static void ensure_math_init(void)
{
	static bool done = false;
	if (!done)
	{
		WWMath::Init();
		/* Init parks a default table in LookupTableMgrClass's static list;
		   leaving it there faults while the CRT tears the statics down, so
		   pair every Init with a Shutdown before that happens. */
		atexit(WWMath::Shutdown);
		done = true;
	}
}

//////////////////////////////////////////////////////////////////////////////
// Scalar helpers
//////////////////////////////////////////////////////////////////////////////

TEST(wwmath_sqrt_and_inv_sqrt)
{
	/* Inv_Sqrt is __fastcall inline asm on x86 - the single most
	   port-sensitive function in the library. */
	static const float vals[] = { 1.0f, 2.0f, 4.0f, 0.25f, 100.0f, 1e6f, 1e-6f };

	for (int i = 0; i < int(sizeof(vals) / sizeof(vals[0])); ++i)
	{
		float v = vals[i];
		CHECK_NEAR(WWMath::Sqrt(v), sqrt(double(v)), fabs(sqrt(double(v))) * 1e-5 + 1e-6);

		/* Intel's rsqrt approximation - documented as ~30% faster, so it is
		   allowed to be loose, but not more than a few ulp of relative error. */
		double expect = 1.0 / sqrt(double(v));
		CHECK_NEAR(WWMath::Inv_Sqrt(v), expect, expect * 1e-4);
	}
}

TEST(wwmath_float_to_long_rounds_like_asm)
{
	/* The asm version uses fistp, i.e. round-to-nearest-even, not C truncation.
	   Anything that relies on truncation here would be a porting bug. */
	CHECK_EQ(WWMath::Float_To_Long(3.4f), 3L);
	CHECK_EQ(WWMath::Float_To_Long(-3.4f), -3L);
	CHECK_EQ(WWMath::Float_To_Long(0.0f), 0L);
	CHECK_EQ(WWMath::Float_To_Long(2.5f), 2L);   /* ties to even */
	CHECK_EQ(WWMath::Float_To_Long(3.5f), 4L);
	CHECK_EQ(WWMath::Float_To_Long(-2.5f), -2L);
}

TEST(wwmath_float_to_int_chop_and_floor)
{
	CHECK_EQ(WWMath::Float_To_Int_Chop(3.9f), 3);
	CHECK_EQ(WWMath::Float_To_Int_Chop(-3.9f), -3);
	CHECK_EQ(WWMath::Float_To_Int_Floor(3.9f), 3);
	CHECK_EQ(WWMath::Float_To_Int_Floor(-3.9f), -4);
	CHECK_EQ(WWMath::Float_To_Int_Floor(-4.0f), -4);
}

TEST(wwmath_float_to_int_boundaries)
{
	struct Case { float input; int chop, floor; };
	const float tiny = std::numeric_limits<float>::denorm_min();
	const Case cases[] = {
		{ 0.0f, 0, 0 }, { -0.0f, 0, 0 }, { tiny, 0, 0 }, { -tiny, 0, -1 },
		{ std::numeric_limits<float>::min(), 0, 0 }, { -std::numeric_limits<float>::min(), 0, -1 },
		{ 0.5f, 0, 0 }, { -0.5f, 0, -1 },
		{ std::nextafter(1.0f, 0.0f), 0, 0 }, { std::nextafter(-1.0f, 0.0f), 0, -1 },
		{ 1.0f, 1, 1 }, { -1.0f, -1, -1 },
		{ std::nextafter(1.0f, 2.0f), 1, 1 }, { std::nextafter(-1.0f, -2.0f), -1, -2 },
		{ 16777216.0f, 16777216, 16777216 }, { -16777216.0f, -16777216, -16777216 },
		{ 2147483520.0f, 2147483520, 2147483520 },
		{ -2147483520.0f, -2147483520, -2147483520 },
		{ -2147483648.0f, INT32_MIN, INT32_MIN },
	};
	for (const Case &item : cases) {
		CHECK_EQ(WWMath::Float_To_Int_Chop(item.input), item.chop);
		CHECK_EQ(WWMath::Float_To_Int_Floor(item.input), item.floor);
	}
}

TEST(wwmath_float_to_int_invalid_inputs)
{
	// These inputs had undefined shifts/conversions in the old implementation. Their new
	// contract follows DetRound; it is not a claim about legacy replay results for invalid data.
	const float limit = 2147483648.0f;
	const float invalid[] = {
		limit, std::nextafter(limit, std::numeric_limits<float>::infinity()),
		std::nextafter(-limit, -std::numeric_limits<float>::infinity()),
		std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::quiet_NaN(),
	};
	for (float input : invalid) {
		CHECK_EQ(WWMath::Float_To_Int_Chop(input), INT32_MIN);
		CHECK_EQ(WWMath::Float_To_Int_Floor(input), INT32_MIN);
	}
}

TEST(wwmath_float_to_int_quarter_sweep)
{
	// Independent integer arithmetic covers negative fractions, exact integers and the
	// small magnitudes used by lookup-table indices and vertex-lighting channels.
	for (int quarter = -8192; quarter <= 8192; ++quarter) {
		const float input = quarter * 0.25f;
		CHECK_EQ(WWMath::Float_To_Int_Chop(input), quarter / 4);
		CHECK_EQ(WWMath::Float_To_Int_Floor(input), (quarter < 0 ? quarter - 3 : quarter) / 4);
	}
}

TEST(wwmath_clamp_wrap_lerp)
{
	CHECK_NEAR(WWMath::Clamp(0.5f, 0.0f, 1.0f), 0.5f, EPS);
	CHECK_NEAR(WWMath::Clamp(-2.0f, 0.0f, 1.0f), 0.0f, EPS);
	CHECK_NEAR(WWMath::Clamp(7.0f, 0.0f, 1.0f), 1.0f, EPS);
	CHECK_EQ(WWMath::Clamp_Int(15, -3, 9), 9);
	CHECK_EQ(WWMath::Clamp_Int(-15, -3, 9), -3);
	CHECK_EQ(WWMath::Clamp_Int(4, -3, 9), 4);

	/* Wrap is exclusive of max: wrapping exactly max lands on min. */
	CHECK_NEAR(WWMath::Wrap(0.25f, 0.0f, 1.0f), 0.25f, EPS);
	CHECK_NEAR(WWMath::Wrap(1.25f, 0.0f, 1.0f), 0.25f, EPS);
	CHECK_NEAR(WWMath::Wrap(-0.75f, 0.0f, 1.0f), 0.25f, EPS);
	CHECK_NEAR(WWMath::Wrap(5.5f, 2.0f, 4.0f), 3.5f, EPS);

	CHECK_NEAR(WWMath::Lerp(10.0f, 20.0f, 0.0f), 10.0f, EPS);
	CHECK_NEAR(WWMath::Lerp(10.0f, 20.0f, 1.0f), 20.0f, EPS);
	CHECK_NEAR(WWMath::Lerp(10.0f, 20.0f, 0.25f), 12.5f, EPS);
}

TEST(wwmath_sign_min_max_fabs)
{
	CHECK_NEAR(WWMath::Sign(-9.0f), -1.0f, EPS);
	CHECK_NEAR(WWMath::Sign(9.0f), 1.0f, EPS);
	CHECK_NEAR(WWMath::Fabs(-3.5f), 3.5f, EPS);
	CHECK_NEAR(WWMath::Min(3.0f, -1.0f), -1.0f, EPS);
	CHECK_NEAR(WWMath::Max(3.0f, -1.0f), 3.0f, EPS);
	CHECK(WWMath::Fast_Is_Float_Positive(1.0f));
	CHECK(!WWMath::Fast_Is_Float_Positive(-1.0f));
}

TEST(wwmath_is_power_of_2)
{
	CHECK(WWMath::Is_Power_Of_2(1));
	CHECK(WWMath::Is_Power_Of_2(2));
	CHECK(WWMath::Is_Power_Of_2(256));
	CHECK(WWMath::Is_Power_Of_2(1u << 31));
	CHECK(!WWMath::Is_Power_Of_2(3));
	CHECK(!WWMath::Is_Power_Of_2(255));
}

TEST(wwmath_unit_float_byte_roundtrip)
{
	for (int b = 0; b < 256; ++b)
	{
		float f = WWMath::Byte_To_Unit_Float((unsigned char)b);
		CHECK_EQ(int(WWMath::Unit_Float_To_Byte(f)), b);
	}
}

TEST(wwmath_fast_trig_matches_libm)
{
	ensure_math_init();

	/* Table-driven approximations: tolerance is set by the table size
	   (SIN_TABLE_SIZE / ARC_TABLE_SIZE), not by float precision. */
	for (int i = 0; i <= 64; ++i)
	{
		float t = float(i) / 64.0f;
		float ang = -WWMATH_PI + t * 2.0f * WWMATH_PI;
		CHECK_NEAR(WWMath::Fast_Sin(ang), sin(double(ang)), 0.005);
		CHECK_NEAR(WWMath::Fast_Cos(ang), cos(double(ang)), 0.005);

		float x = -1.0f + t * 2.0f;
		CHECK_NEAR(WWMath::Fast_Acos(x), acos(double(x)), 0.02);
		CHECK_NEAR(WWMath::Fast_Asin(x), asin(double(x)), 0.02);
	}
}

TEST(wwmath_acos_asin_clamp_out_of_range)
{
	ensure_math_init();

	/* Acos and Asin used to be bare wrappers over libm and did NOT clamp, so a
	   dot product that landed a hair outside [-1,1] - which normalised vectors
	   do all the time - came back NaN, and Fast_* inherited it because outside
	   +/-0.975 it defers to the exact form.  They now go through DetTrig, which
	   clamps its argument, so those calls return the boundary value instead.
	   That is a behaviour change and this is where it is pinned. */
	CHECK_EQ(WWMath::Acos(1.0000001f), WWMath::Acos(1.0f));
	CHECK_EQ(WWMath::Asin(-1.0000001f), WWMath::Asin(-1.0f));
	CHECK_EQ(WWMath::Fast_Acos(1.5f), WWMath::Fast_Acos(1.0f));
	CHECK_EQ(WWMath::Fast_Asin(-1.5f), WWMath::Fast_Asin(-1.0f));

	/* Exactly on the boundary is still finite. */
	CHECK_NEAR(WWMath::Acos(1.0f), 0.0f, EPS);
	CHECK_NEAR(WWMath::Acos(-1.0f), WWMATH_PI, EPS);
	CHECK_NEAR(WWMath::Asin(1.0f), WWMATH_PI * 0.5f, EPS);
	CHECK_NEAR(WWMath::Fast_Acos(1.0f), 0.0f, EPS);
	CHECK_NEAR(WWMath::Fast_Asin(-1.0f), -WWMATH_PI * 0.5f, EPS);
}

TEST(pot_find_power_of_two)
{
	CHECK_EQ(Find_POT(1), 1);
	CHECK_EQ(Find_POT(2), 2);
	CHECK_EQ(Find_POT(3), 4);
	CHECK_EQ(Find_POT(5), 8);
	CHECK_EQ(Find_POT(256), 256);
	CHECK_EQ(Find_POT(257), 512);

	CHECK_EQ(int(Find_POT_Log2(1)), 0);
	CHECK_EQ(int(Find_POT_Log2(2)), 1);
	CHECK_EQ(int(Find_POT_Log2(4)), 2);
	CHECK_EQ(int(Find_POT_Log2(256)), 8);
	/* Every POT that a texture dimension can take must round-trip. */
	for (int shift = 0; shift <= 12; ++shift)
		CHECK_EQ(Find_POT(1 << shift), 1 << shift);
}

//////////////////////////////////////////////////////////////////////////////
// Vector2 / Vector3 / Vector4
//////////////////////////////////////////////////////////////////////////////

TEST(vector3_arithmetic)
{
	Vector3 a(1.0f, 2.0f, 3.0f);
	Vector3 b(4.0f, 5.0f, 6.0f);

	Vector3 sum = a + b;
	CHECK_NEAR(sum.X, 5.0f, EPS);
	CHECK_NEAR(sum.Y, 7.0f, EPS);
	CHECK_NEAR(sum.Z, 9.0f, EPS);

	Vector3 diff = b - a;
	CHECK_NEAR(diff.X, 3.0f, EPS);

	Vector3 scaled = a * 2.0f;
	CHECK_NEAR(scaled.Z, 6.0f, EPS);

	Vector3 neg = -a;
	CHECK_NEAR(neg.X, -1.0f, EPS);

	a += b;
	CHECK_NEAR(a.X, 5.0f, EPS);
	a -= b;
	CHECK_NEAR(a.X, 1.0f, EPS);

	CHECK(Vector3(1.0f, 2.0f, 3.0f) == Vector3(1.0f, 2.0f, 3.0f));
	CHECK(Vector3(1.0f, 2.0f, 3.0f) != Vector3(1.0f, 2.0f, 3.5f));
}

TEST(vector3_length_and_normalize)
{
	Vector3 v(3.0f, 4.0f, 12.0f);
	CHECK_NEAR(v.Length2(), 169.0f, EPS);
	CHECK_NEAR(v.Length(), 13.0f, EPS);
	/* Quick_Length is the Graphics Gems approximation, worst case ~8.7% high
	   (which (3,4,12) hits almost exactly).  That only holds once the three
	   components are actually sorted - the third swap used to assign min=mid
	   and lose the old mid, pushing this case to 14.375, i.e. 10.6% high. */
	CHECK_NEAR(v.Quick_Length(), 13.0f, 13.0f * 0.09f);
	CHECK_NEAR(Vector3(12.0f, 4.0f, 3.0f).Quick_Length(), 13.0f, 13.0f * 0.09f);
	CHECK_NEAR(Vector3(-3.0f, -12.0f, -4.0f).Quick_Length(), 13.0f, 13.0f * 0.09f);
	CHECK_NEAR(Vector3(0.0f, 0.0f, 0.0f).Quick_Length(), 0.0f, EPS);

	v.Normalize();
	CHECK_NEAR(v.Length(), 1.0f, EPS);
	CHECK_NEAR(v.X, 3.0f / 13.0f, EPS);

	Vector3 n(0.0f, 5.0f, 0.0f);
	n.Normalize();
	CHECK_NEAR(n.Y, 1.0f, EPS);
}

TEST(vector3_dot_and_cross)
{
	Vector3 a(1.0f, 2.0f, 3.0f);
	Vector3 b(4.0f, -5.0f, 6.0f);

	CHECK_NEAR(Vector3::Dot_Product(a, b), 4.0f - 10.0f + 18.0f, EPS);

	Vector3 c;
	Vector3::Cross_Product(a, b, &c);
	/* a x b = (2*6-3*-5, 3*4-1*6, 1*-5-2*4) = (27, 6, -13) */
	CHECK_NEAR(c.X, 27.0f, EPS);
	CHECK_NEAR(c.Y, 6.0f, EPS);
	CHECK_NEAR(c.Z, -13.0f, EPS);

	/* The cross product is orthogonal to both inputs. */
	CHECK_NEAR(Vector3::Dot_Product(c, a), 0.0f, EPS);
	CHECK_NEAR(Vector3::Dot_Product(c, b), 0.0f, EPS);

	/* ...and anti-commutative. */
	Vector3 d;
	Vector3::Cross_Product(b, a, &d);
	CHECK_NEAR(d.X, -c.X, EPS);
}

TEST(vector3_lerp_minmax_cap)
{
	Vector3 a(0.0f, 0.0f, 0.0f);
	Vector3 b(10.0f, 20.0f, -40.0f);

	Vector3 mid;
	Vector3::Lerp(a, b, 0.25f, &mid);
	CHECK_NEAR(mid.X, 2.5f, EPS);
	CHECK_NEAR(mid.Z, -10.0f, EPS);

	Vector3 lo(1.0f, 9.0f, 5.0f);
	lo.Update_Min(Vector3(4.0f, 2.0f, 5.0f));
	CHECK_NEAR(lo.X, 1.0f, EPS);
	CHECK_NEAR(lo.Y, 2.0f, EPS);

	Vector3 hi(1.0f, 9.0f, 5.0f);
	hi.Update_Max(Vector3(4.0f, 2.0f, 5.0f));
	CHECK_NEAR(hi.X, 4.0f, EPS);
	CHECK_NEAR(hi.Y, 9.0f, EPS);

	Vector3 cap(-8.0f, 3.0f, 0.5f);
	cap.Cap_Absolute_To(Vector3(5.0f, 5.0f, 5.0f));
	CHECK_NEAR(cap.X, -5.0f, EPS);
	CHECK_NEAR(cap.Y, 3.0f, EPS);
}

TEST(vector3_is_valid_rejects_nan_and_inf)
{
	CHECK(Vector3(1.0f, 2.0f, 3.0f).Is_Valid());

	/* Built through a union so the compiler cannot fold the comparison away. */
	union { unsigned u; float f; } nan_bits, inf_bits;
	nan_bits.u = 0x7fc00000u;
	inf_bits.u = 0x7f800000u;
	CHECK(!Vector3(nan_bits.f, 0.0f, 0.0f).Is_Valid());
	CHECK(!Vector3(0.0f, inf_bits.f, 0.0f).Is_Valid());
}

// Is_Valid_Float and Is_Valid_Double against std::isfinite over every class - normal, denormal, both
// zeroes, both infinities, quiet and signalling NaNs, the largest finite values - and a stride through every
// float's bits and every double's high word.  Both read their argument through unsigned long once, which is
// 8 bytes on LP64: ASan under ZH_SANITIZE catches that here, and Is_Valid_Double answered for the bytes
// after its argument.
TEST(is_valid_float_and_double_every_class)
{
	static const uint32_t FLOATS[] = { 0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x00800000u,
		0x3F800000u, 0xBF800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u,
		0x7F800001u, 0x7FBFFFFFu };
	for (size_t i = 0; i < sizeof(FLOATS) / sizeof(FLOATS[0]); ++i) {
		float f;
		memcpy(&f, &FLOATS[i], sizeof(f));
		CHECK(WWMath::Is_Valid_Float(f) == (std::isfinite(f) != 0));
	}
	int float_mismatches = 0;
	for (uint64_t u = 0; u <= 0xFFFFFFFFull; u += 0x10001ull) {
		const uint32_t bits = (uint32_t)u;
		float f;
		memcpy(&f, &bits, sizeof(f));
		float_mismatches += WWMath::Is_Valid_Float(f) != (std::isfinite(f) != 0);
	}
	CHECK_EQ(float_mismatches, 0);

	static const uint64_t DOUBLES[] = { 0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull,
		0x000FFFFFFFFFFFFFull, 0x0010000000000000ull, 0x3FF0000000000000ull, 0xBFF0000000000000ull,
		0x7FEFFFFFFFFFFFFFull, 0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000000ull,
		0x7FF0000000000001ull, 0xFFF8000000000000ull };
	for (size_t i = 0; i < sizeof(DOUBLES) / sizeof(DOUBLES[0]); ++i) {
		double d;
		memcpy(&d, &DOUBLES[i], sizeof(d));
		CHECK(WWMath::Is_Valid_Double(d) == (std::isfinite(d) != 0));
	}
	int double_mismatches = 0;
	for (uint64_t high = 0; high <= 0xFFFFFFFFull; high += 0x10001ull) {
		const uint64_t bits = (high << 32) | 0x12345678ull;
		double d;
		memcpy(&d, &bits, sizeof(d));
		double_mismatches += WWMath::Is_Valid_Double(d) != (std::isfinite(d) != 0);
	}
	CHECK_EQ(double_mismatches, 0);
}

TEST(vector3_equal_within_epsilon)
{
	Vector3 a(1.0f, 1.0f, 1.0f);
	Vector3 b(1.0f + 1e-6f, 1.0f, 1.0f);
	CHECK(Equal_Within_Epsilon(a, b, 1e-4f));
	CHECK(!Equal_Within_Epsilon(a, Vector3(1.1f, 1.0f, 1.0f), 1e-4f));
}

TEST(vector2_basics)
{
	Vector2 a(3.0f, 4.0f);
	CHECK_NEAR(a.Length(), 5.0f, EPS);
	CHECK_NEAR(a.Length2(), 25.0f, EPS);
	CHECK_NEAR(Vector2::Dot_Product(a, Vector2(1.0f, 0.0f)), 3.0f, EPS);

	a.Normalize();
	CHECK_NEAR(a.Length(), 1.0f, EPS);

	Vector2 lerp;
	Vector2::Lerp(Vector2(0.0f, 0.0f), Vector2(4.0f, 8.0f), 0.5f, &lerp);
	CHECK_NEAR(lerp.X, 2.0f, EPS);
	CHECK_NEAR(lerp.Y, 4.0f, EPS);
}

TEST(vector4_basics)
{
	Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
	Vector4 b(4.0f, 3.0f, 2.0f, 1.0f);
	CHECK_NEAR(Vector4::Dot_Product(a, b), 4.0f + 6.0f + 6.0f + 4.0f, EPS);
	CHECK_NEAR(a.Length2(), 30.0f, EPS);

	Vector4 sum = a + b;
	CHECK_NEAR(sum.X, 5.0f, EPS);
	CHECK_NEAR(sum.W, 5.0f, EPS);

	Vector4 lerp;
	Vector4::Lerp(a, b, 0.5f, &lerp);
	CHECK_NEAR(lerp.X, 2.5f, EPS);
}

//////////////////////////////////////////////////////////////////////////////
// Matrices
//////////////////////////////////////////////////////////////////////////////

static void check_is_identity(const Matrix3D &m)
{
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
			CHECK_NEAR(m[r][c], (r == c) ? 1.0f : 0.0f, 1e-3f);
}

TEST(matrix3d_identity_and_translation)
{
	Matrix3D m(1);
	check_is_identity(m);

	m.Set_Translation(Vector3(4.0f, -2.0f, 8.0f));
	CHECK_NEAR(m.Get_X_Translation(), 4.0f, EPS);
	CHECK_NEAR(m.Get_Y_Translation(), -2.0f, EPS);
	CHECK_NEAR(m.Get_Z_Translation(), 8.0f, EPS);

	Vector3 t = m.Get_Translation();
	CHECK_NEAR(t.Z, 8.0f, EPS);

	/* Translation moves a point, and the rotation part is untouched. */
	Vector3 p = xform(m, Vector3(1.0f, 1.0f, 1.0f));
	CHECK_NEAR(p.X, 5.0f, EPS);
	CHECK_NEAR(p.Y, -1.0f, EPS);
	CHECK_NEAR(p.Z, 9.0f, EPS);
}

TEST(matrix3d_axis_rotation)
{
	/* 90 degrees about Z sends +X to +Y. */
	Matrix3D rz(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.5f);
	Vector3 v = xform(rz, Vector3(1.0f, 0.0f, 0.0f));
	CHECK_NEAR(v.X, 0.0f, 1e-3f);
	CHECK_NEAR(v.Y, 1.0f, 1e-3f);
	CHECK_NEAR(v.Z, 0.0f, 1e-3f);

	/* 90 degrees about X sends +Y to +Z. */
	Matrix3D rx(Vector3(1.0f, 0.0f, 0.0f), WWMATH_PI * 0.5f);
	v = xform(rx, Vector3(0.0f, 1.0f, 0.0f));
	CHECK_NEAR(v.Z, 1.0f, 1e-3f);

	/* Four quarter turns is the identity. */
	Matrix3D acc(1), tmp;
	for (int i = 0; i < 4; ++i)
	{
		Matrix3D::Multiply(acc, rz, &tmp);
		acc = tmp;
	}
	check_is_identity(acc);
}

TEST(matrix3d_get_inverse_general_affine)
{
	/* This is the function that was rewritten to drop D3DXMatrixInverse, so it
	   gets the hardest cases: rotation+translation, and non-uniform scale where
	   the transpose shortcut is wrong. */
	/* Matrix3D's axis-angle form asserts a unit axis; a Debug build aborts on anything else. */
	Vector3 axis(0.3f, -0.7f, 0.65f);
	axis.Normalize();
	Matrix3D rot(axis, 1.1f);
	rot.Set_Translation(Vector3(12.0f, -4.5f, 3.25f));

	Matrix3D inv, prod;
	rot.Get_Inverse(inv);
	Matrix3D::Multiply(rot, inv, &prod);
	check_is_identity(prod);
	Matrix3D::Multiply(inv, rot, &prod);
	check_is_identity(prod);

	Matrix3D scale(Vector3(2.0f, 0.0f, 0.0f),
	               Vector3(0.0f, 0.5f, 0.0f),
	               Vector3(0.0f, 0.0f, 4.0f),
	               Vector3(1.0f, 2.0f, 3.0f));
	scale.Get_Inverse(inv);
	Matrix3D::Multiply(scale, inv, &prod);
	check_is_identity(prod);
	Matrix3D::Multiply(inv, scale, &prod);
	check_is_identity(prod);

	/* Shear, which has no orthogonal structure at all to fall back on. */
	Matrix3D shear(Vector3(1.0f, 0.4f, -0.2f),
	               Vector3(0.0f, 1.0f, 0.7f),
	               Vector3(0.3f, 0.0f, 1.0f),
	               Vector3(-5.0f, 6.0f, 7.0f));
	shear.Get_Inverse(inv);
	Matrix3D::Multiply(shear, inv, &prod);
	check_is_identity(prod);
}

TEST(matrix3d_inverse_transform_point_roundtrip)
{
	/* Matrix3D's axis-angle form asserts a unit axis; a Debug build aborts on anything else. */
	Vector3 axis(0.1f, 0.9f, -0.4f);
	axis.Normalize();
	Matrix3D m(axis, 0.77f);
	m.Set_Translation(Vector3(-3.0f, 11.0f, 0.5f));

	Matrix3D inv;
	m.Get_Inverse(inv);

	Vector3 p(2.5f, -7.0f, 4.25f);
	Vector3 back = xform(inv, xform(m, p));
	CHECK_NEAR(back.X, p.X, 1e-3f);
	CHECK_NEAR(back.Y, p.Y, 1e-3f);
	CHECK_NEAR(back.Z, p.Z, 1e-3f);
}

TEST(matrix3d_rotate_helpers_match_axis_form)
{
	Matrix3D a(1), b(1);
	a.Rotate_X(0.6f);
	b.Set(Vector3(1.0f, 0.0f, 0.0f), 0.6f);
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			CHECK_NEAR(a[r][c], b[r][c], 1e-4f);

	Matrix3D cz(1), dz(1);
	cz.Rotate_Z(-1.3f);
	dz.Set(Vector3(0.0f, 0.0f, 1.0f), -1.3f);
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			CHECK_NEAR(cz[r][c], dz[r][c], 1e-4f);
}

TEST(matrix3d_get_axis_rotation)
{
	Matrix3D m(1);
	m.Rotate_Z(0.42f);
	CHECK_NEAR(m.Get_Z_Rotation(), 0.42f, 1e-3f);

	Matrix3D n(1);
	n.Rotate_X(-0.9f);
	CHECK_NEAR(n.Get_X_Rotation(), -0.9f, 1e-3f);
}

TEST(matrix3x3_multiply_and_inverse)
{
	/* The axis-angle constructor assumes a unit axis - feed it anything else
	   and you get a matrix that is not a rotation at all. */
	Vector3 axis(0.2f, 0.5f, -0.84f);
	axis.Normalize();
	Matrix3x3 r(axis, 0.9f);
	Matrix3x3 inv = r.Inverse();
	Matrix3x3 prod = r * inv;

	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			CHECK_NEAR(prod[i][j], (i == j) ? 1.0f : 0.0f, 1e-3f);

	/* A pure rotation has determinant 1 and its inverse is its transpose. */
	CHECK_NEAR(r.Determinant(), 1.0f, 1e-3f);
	Matrix3x3 t = r.Transpose();
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			CHECK_NEAR(t[i][j], inv[i][j], 1e-3f);
}

TEST(matrix4x4_identity_and_multiply)
{
	Matrix4x4 id(true);
	Matrix4x4 m(true);
	m[0][3] = 5.0f;
	m[1][3] = -2.0f;

	Matrix4x4 prod = id * m;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			CHECK_NEAR(prod[r][c], m[r][c], EPS);

	/* Homogeneous transform of a point picks up the translation column. */
	Vector4 v = m * Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	CHECK_NEAR(v.X, 6.0f, EPS);
	CHECK_NEAR(v.Y, -1.0f, EPS);
	CHECK_NEAR(v.W, 1.0f, EPS);
}

TEST(matrix4x4_from_matrix3d_agrees_on_points)
{
	/* Matrix3D's axis-angle form asserts a unit axis; a Debug build aborts on anything else. */
	Vector3 axis(0.3f, 0.2f, 0.93f);
	axis.Normalize();
	Matrix3D m3(axis, 0.5f);
	m3.Set_Translation(Vector3(2.0f, 3.0f, 4.0f));

	Matrix4x4 m4(m3);

	Vector3 p(1.5f, -2.5f, 0.75f);
	Vector3 a = xform(m3, p);
	Vector4 b = m4 * Vector4(p.X, p.Y, p.Z, 1.0f);

	CHECK_NEAR(a.X, b.X, 1e-3f);
	CHECK_NEAR(a.Y, b.Y, 1e-3f);
	CHECK_NEAR(a.Z, b.Z, 1e-3f);
	CHECK_NEAR(b.W, 1.0f, 1e-3f);
}

//////////////////////////////////////////////////////////////////////////////
// Quaternions
//////////////////////////////////////////////////////////////////////////////

TEST(quaternion_axis_angle_roundtrip)
{
	Vector3 axis(0.0f, 0.0f, 1.0f);
	Quaternion q = Axis_To_Quat(axis, WWMATH_PI * 0.5f);
	CHECK_NEAR(q.Length(), 1.0f, EPS);

	/* Same rotation as the matrix form. */
	Matrix3D fromq(1);
	fromq.Set(q, Vector3(0.0f, 0.0f, 0.0f));
	Matrix3D fromaxis(axis, WWMATH_PI * 0.5f);

	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			CHECK_NEAR(fromq[r][c], fromaxis[r][c], 1e-3f);
}

TEST(quaternion_build_from_matrix_roundtrip)
{
	Matrix3D m(Vector3(0.26726f, 0.53452f, 0.80178f), 1.234f);
	Quaternion q = Build_Quaternion(m);
	CHECK_NEAR(q.Length(), 1.0f, 1e-3f);

	Matrix3D back(1);
	back.Set(q, Vector3(0.0f, 0.0f, 0.0f));
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			CHECK_NEAR(back[r][c], m[r][c], 1e-3f);
}

TEST(quaternion_multiply_and_inverse)
{
	Quaternion a = Axis_To_Quat(Vector3(1.0f, 0.0f, 0.0f), 0.7f);
	Quaternion b = Axis_To_Quat(Vector3(0.0f, 1.0f, 0.0f), -1.1f);

	Quaternion ident = a * Inverse(a);
	CHECK_NEAR(ident.W, 1.0f, 1e-3f);
	CHECK_NEAR(ident.X, 0.0f, 1e-3f);

	/* (ab)^-1 == b^-1 a^-1 */
	Quaternion lhs = Inverse(a * b);
	Quaternion rhs = Inverse(b) * Inverse(a);
	CHECK(Equal_Within_Epsilon(lhs, rhs, 1e-3f));
}

TEST(quaternion_slerp_endpoints_and_midpoint)
{
	Quaternion a = Axis_To_Quat(Vector3(0.0f, 0.0f, 1.0f), 0.0f);
	Quaternion b = Axis_To_Quat(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.5f);

	Quaternion r;
	Slerp(r, a, b, 0.0f);
	CHECK(Equal_Within_Epsilon(r, a, 1e-3f));

	Slerp(r, a, b, 1.0f);
	CHECK(Equal_Within_Epsilon(r, b, 1e-3f));

	/* Halfway along a great circle is the 45-degree rotation. */
	Slerp(r, a, b, 0.5f);
	Quaternion mid = Axis_To_Quat(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.25f);
	CHECK(Equal_Within_Epsilon(r, mid, 1e-3f));
	CHECK_NEAR(r.Length(), 1.0f, 1e-3f);

	/* Fast_Slerp is the approximate variant; still has to stay on the sphere. */
	Quaternion fr;
	Fast_Slerp(fr, a, b, 0.5f);
	CHECK_NEAR(fr.Length(), 1.0f, 1e-2f);
	CHECK(Equal_Within_Epsilon(fr, mid, 1e-2f));
}

TEST(quaternion_normalize_and_conjugate)
{
	Quaternion q(0.0f, 3.0f, 0.0f, 4.0f);
	CHECK_NEAR(q.Length(), 5.0f, EPS);

	Quaternion n = Normalize(q);
	CHECK_NEAR(n.Length(), 1.0f, EPS);

	Quaternion c = Conjugate(n);
	CHECK_NEAR(c.W, n.W, EPS);
	CHECK_NEAR(c.Y, -n.Y, EPS);
}

//////////////////////////////////////////////////////////////////////////////
// Bounding volumes
//////////////////////////////////////////////////////////////////////////////

TEST(aabox_from_points_and_contains)
{
	Vector3 pts[4] = {
		Vector3(-1.0f, -2.0f, -3.0f),
		Vector3(5.0f, 0.0f, 1.0f),
		Vector3(0.0f, 4.0f, 0.0f),
		Vector3(1.0f, 1.0f, 7.0f)
	};

	AABoxClass box(pts, 4);
	/* min = (-1,-2,-3), max = (5,4,7) -> center (2,1,2), extent (3,3,5) */
	CHECK_NEAR(box.Center.X, 2.0f, EPS);
	CHECK_NEAR(box.Center.Y, 1.0f, EPS);
	CHECK_NEAR(box.Center.Z, 2.0f, EPS);
	CHECK_NEAR(box.Extent.X, 3.0f, EPS);
	CHECK_NEAR(box.Extent.Z, 5.0f, EPS);

	for (int i = 0; i < 4; ++i)
		CHECK(box.Contains(pts[i]));

	CHECK(!box.Contains(Vector3(100.0f, 0.0f, 0.0f)));
	CHECK(box.Contains(AABoxClass(Vector3(2.0f, 1.0f, 2.0f), Vector3(0.5f, 0.5f, 0.5f))));
	CHECK(!box.Contains(AABoxClass(Vector3(2.0f, 1.0f, 2.0f), Vector3(50.0f, 0.5f, 0.5f))));
}

TEST(aabox_add_point_grows_only_as_needed)
{
	AABoxClass box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	float vol_before = box.Volume();

	box.Add_Point(Vector3(0.5f, 0.5f, 0.5f));      /* already inside */
	CHECK_NEAR(box.Volume(), vol_before, EPS);

	box.Add_Point(Vector3(3.0f, 0.0f, 0.0f));
	CHECK(box.Contains(Vector3(3.0f, 0.0f, 0.0f)));
	CHECK(box.Volume() > vol_before);
	/* min stays -1, max becomes 3 -> center 1, extent 2 on X only. */
	CHECK_NEAR(box.Center.X, 1.0f, EPS);
	CHECK_NEAR(box.Extent.X, 2.0f, EPS);
	CHECK_NEAR(box.Extent.Y, 1.0f, EPS);
}

TEST(aabox_project_to_axis_and_volume)
{
	AABoxClass box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 2.0f, 3.0f));
	CHECK_NEAR(box.Volume(), 2.0f * 4.0f * 6.0f, 1e-3f);

	/* Projection onto a cardinal axis is that axis' extent. */
	CHECK_NEAR(box.Project_To_Axis(Vector3(1.0f, 0.0f, 0.0f)), 1.0f, EPS);
	CHECK_NEAR(box.Project_To_Axis(Vector3(0.0f, 0.0f, 1.0f)), 3.0f, EPS);
	/* Onto a diagonal it is the sum of |axis_i| * extent_i. */
	CHECK_NEAR(box.Project_To_Axis(Vector3(1.0f, 1.0f, 0.0f)), 3.0f, EPS);
}

TEST(aabox_transform_by_rotation)
{
	AABoxClass box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	Matrix3D rot(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.25f);
	box.Transform(rot);

	/* A unit cube spun 45 degrees about Z needs a sqrt(2) box in X and Y,
	   and still contains its own centre. */
	CHECK_NEAR(box.Extent.X, WWMATH_SQRT2, 1e-3f);
	CHECK_NEAR(box.Extent.Y, WWMATH_SQRT2, 1e-3f);
	CHECK_NEAR(box.Extent.Z, 1.0f, 1e-3f);
	CHECK(box.Contains(box.Center));
}

TEST(aabox_translate)
{
	AABoxClass box(Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 1.0f, 1.0f));
	box.Translate(Vector3(10.0f, 0.0f, -3.0f));
	CHECK_NEAR(box.Center.X, 11.0f, EPS);
	CHECK_NEAR(box.Center.Z, 0.0f, EPS);
	/* Translation never changes size. */
	CHECK_NEAR(box.Extent.X, 1.0f, EPS);
}

TEST(minmax_aabox_init_and_add)
{
	MinMaxAABoxClass mm(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	mm.Add_Point(Vector3(-4.0f, 0.5f, 9.0f));
	CHECK_NEAR(mm.MinCorner.X, -4.0f, EPS);
	CHECK_NEAR(mm.MaxCorner.Z, 9.0f, EPS);

	/* Converting to the centre/extent form must describe the same volume. */
	AABoxClass box(mm);
	CHECK_NEAR(box.Center.X, -1.5f, EPS);
	CHECK_NEAR(box.Extent.X, 2.5f, EPS);
	CHECK(box.Contains(Vector3(-4.0f, 0.5f, 9.0f)));
}

TEST(sphere_add_and_transform)
{
	SphereClass s(Vector3(0.0f, 0.0f, 0.0f), 1.0f);
	CHECK_NEAR(s.Radius, 1.0f, EPS);

	/* Absorbing a far sphere must cover both. */
	s.Add_Sphere(SphereClass(Vector3(10.0f, 0.0f, 0.0f), 1.0f));
	CHECK(s.Radius >= 6.0f - EPS);
	CHECK_NEAR(s.Center.X, 5.0f, 1e-3f);

	/* Rigid transforms move the centre and leave the radius alone. */
	SphereClass t(Vector3(1.0f, 0.0f, 0.0f), 2.0f);
	Matrix3D m(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.5f);
	m.Set_Translation(Vector3(0.0f, 0.0f, 5.0f));
	t.Transform(m);
	CHECK_NEAR(t.Radius, 2.0f, 1e-3f);
	CHECK_NEAR(t.Center.Y, 1.0f, 1e-3f);
	CHECK_NEAR(t.Center.Z, 5.0f, 1e-3f);
}

TEST(obbox_axis_aligned_matches_aabox)
{
	/* An OBB with an identity basis must project exactly like the AABox. */
	OBBoxClass obb(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 2.0f, 3.0f));
	CHECK_NEAR(obb.Project_To_Axis(Vector3(1.0f, 0.0f, 0.0f)), 1.0f, 1e-3f);
	CHECK_NEAR(obb.Project_To_Axis(Vector3(0.0f, 1.0f, 0.0f)), 2.0f, 1e-3f);
	CHECK_NEAR(obb.Volume(), 2.0f * 4.0f * 6.0f, 1e-2f);

	/* OBBoxClass has no Contains of its own; point containment goes through
	   the collision layer. */
	CHECK_EQ(CollisionMath::Overlap_Test(obb, Vector3(0.5f, 0.5f, 0.5f)),
	         CollisionMath::INSIDE);
	CHECK_EQ(CollisionMath::Overlap_Test(obb, Vector3(0.5f, 0.5f, 9.0f)),
	         CollisionMath::OUTSIDE);

	/* An identity-basis OBB's axis-aligned extent is just its extent. */
	Vector3 aae;
	obb.Compute_Axis_Aligned_Extent(&aae);
	CHECK_NEAR(aae.X, 1.0f, 1e-3f);
	CHECK_NEAR(aae.Z, 3.0f, 1e-3f);

	/* Spun 90 degrees about Z, the X and Y extents swap. */
	OBBoxClass spun(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 2.0f, 3.0f),
	                Matrix3x3(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.5f));
	spun.Compute_Axis_Aligned_Extent(&aae);
	CHECK_NEAR(aae.X, 2.0f, 1e-3f);
	CHECK_NEAR(aae.Y, 1.0f, 1e-3f);
	CHECK_NEAR(aae.Z, 3.0f, 1e-3f);
}

TEST(obbox_transform_moves_center_and_basis)
{
	OBBoxClass in(Vector3(1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));

	Matrix3D tm(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.5f);
	tm.Set_Translation(Vector3(0.0f, 0.0f, 4.0f));

	OBBoxClass out;
	OBBoxClass::Transform(tm, in, &out);

	/* Centre follows the transform; a rigid motion leaves the volume alone. */
	CHECK_NEAR(out.Center.X, 0.0f, 1e-3f);
	CHECK_NEAR(out.Center.Y, 1.0f, 1e-3f);
	CHECK_NEAR(out.Center.Z, 4.0f, 1e-3f);
	CHECK_NEAR(out.Volume(), in.Volume(), 1e-2f);
}

//////////////////////////////////////////////////////////////////////////////
// Planes, triangles, line segments
//////////////////////////////////////////////////////////////////////////////

TEST(plane_from_three_points)
{
	PlaneClass p(Vector3(0.0f, 0.0f, 0.0f),
	             Vector3(1.0f, 0.0f, 0.0f),
	             Vector3(0.0f, 1.0f, 0.0f));

	/* Normal is +/-Z and unit length, and the plane passes through origin. */
	CHECK_NEAR(fabs(p.N.Z), 1.0f, 1e-3f);
	CHECK_NEAR(p.N.Length(), 1.0f, 1e-3f);
	CHECK_NEAR(p.D, 0.0f, 1e-3f);
}

TEST(plane_line_intersection)
{
	/* z = 5 plane, segment straight down the Z axis from 0 to 10. */
	PlaneClass p(Vector3(0.0f, 0.0f, 1.0f), 5.0f);

	float t = -1.0f;
	bool hit = p.Compute_Intersection(Vector3(0.0f, 0.0f, 0.0f),
	                                  Vector3(0.0f, 0.0f, 10.0f), &t);
	CHECK(hit);
	CHECK_NEAR(t, 0.5f, 1e-3f);

	/* Parallel segment never hits. */
	CHECK(!p.Compute_Intersection(Vector3(0.0f, 0.0f, 0.0f),
	                              Vector3(10.0f, 0.0f, 0.0f), &t));
}

TEST(plane_intersect_planes_gives_shared_line)
{
	PlaneClass a(Vector3(1.0f, 0.0f, 0.0f), 0.0f);   /* x = 0 */
	PlaneClass b(Vector3(0.0f, 1.0f, 0.0f), 0.0f);   /* y = 0 */

	Vector3 dir, point;
	PlaneClass::Intersect_Planes(a, b, &dir, &point);

	/* Their intersection is the Z axis. */
	CHECK_NEAR(fabs(dir.Z), 1.0f, 1e-3f);
	CHECK_NEAR(dir.X, 0.0f, 1e-3f);
	CHECK_NEAR(dir.Y, 0.0f, 1e-3f);
	/* The returned point lies on both planes. */
	CHECK_NEAR(point.X, 0.0f, 1e-3f);
	CHECK_NEAR(point.Y, 0.0f, 1e-3f);
}

TEST(tri_normal_and_contains_point)
{
	Vector3 v0(0.0f, 0.0f, 0.0f);
	Vector3 v1(4.0f, 0.0f, 0.0f);
	Vector3 v2(0.0f, 4.0f, 0.0f);
	Vector3 n;

	TriClass tri;
	tri.V[0] = &v0;
	tri.V[1] = &v1;
	tri.V[2] = &v2;
	tri.N = &n;
	tri.Compute_Normal();

	CHECK_NEAR(n.Length(), 1.0f, 1e-3f);
	CHECK_NEAR(fabs(n.Z), 1.0f, 1e-3f);

	CHECK(tri.Contains_Point(Vector3(1.0f, 1.0f, 0.0f)));
	CHECK(!tri.Contains_Point(Vector3(3.0f, 3.0f, 0.0f)));  /* past the hypotenuse */
	CHECK(!tri.Contains_Point(Vector3(-1.0f, 1.0f, 0.0f)));

	/* The dominant plane of a Z-facing triangle is the XY plane. */
	int a1 = -1, a2 = -1;
	tri.Find_Dominant_Plane(&a1, &a2);
	CHECK_EQ(a1, 0);
	CHECK_EQ(a2, 1);
}

TEST(lineseg_geometry)
{
	LineSegClass seg(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f));
	CHECK_NEAR(seg.Get_Length(), 10.0f, EPS);
	CHECK_NEAR(seg.Get_Dir().X, 1.0f, EPS);
	CHECK_NEAR(seg.Get_DP().X, 10.0f, EPS);

	Vector3 p;
	seg.Compute_Point(0.25f, &p);
	CHECK_NEAR(p.X, 2.5f, EPS);

	/* Closest point clamps to the segment, it does not run off the line. */
	CHECK_NEAR(seg.Find_Point_Closest_To(Vector3(3.0f, 9.0f, 0.0f)).X, 3.0f, 1e-3f);
	CHECK_NEAR(seg.Find_Point_Closest_To(Vector3(-8.0f, 1.0f, 0.0f)).X, 0.0f, 1e-3f);
	CHECK_NEAR(seg.Find_Point_Closest_To(Vector3(50.0f, 1.0f, 0.0f)).X, 10.0f, 1e-3f);
}

TEST(lineseg_transform_preserves_length)
{
	LineSegClass seg(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 6.0f, 3.0f));
	float len = seg.Get_Length();

	Matrix3D m(Vector3(0.0f, 1.0f, 0.0f), 0.8f);
	m.Set_Translation(Vector3(-20.0f, 5.0f, 1.0f));

	LineSegClass moved(seg, m);
	CHECK_NEAR(moved.Get_Length(), len, 1e-3f);
	CHECK_NEAR(moved.Get_P0().X, xform(m, seg.Get_P0()).X, 1e-3f);
}

//////////////////////////////////////////////////////////////////////////////
// Collision predicates
//////////////////////////////////////////////////////////////////////////////

TEST(colmath_overlap_plane_point)
{
	PlaneClass p(Vector3(0.0f, 0.0f, 1.0f), 0.0f);   /* z = 0, normal +Z */

	CHECK_EQ(CollisionMath::Overlap_Test(p, Vector3(0.0f, 0.0f, 5.0f)),
	         CollisionMath::POS);
	CHECK_EQ(CollisionMath::Overlap_Test(p, Vector3(0.0f, 0.0f, -5.0f)),
	         CollisionMath::NEG);
}

TEST(colmath_overlap_aaplane_sphere)
{
	AAPlaneClass p(AAPlaneClass::ZNORMAL, 0.0f);

	CHECK_EQ(CollisionMath::Overlap_Test(p, SphereClass(Vector3(0.0f, 0.0f, 10.0f), 1.0f)),
	         CollisionMath::POS);
	CHECK_EQ(CollisionMath::Overlap_Test(p, SphereClass(Vector3(0.0f, 0.0f, -10.0f), 1.0f)),
	         CollisionMath::NEG);
	/* Straddling the plane.  The NEG test compared against +Radius instead of
	   -Radius, which made BOTH all but unreachable and reported every
	   straddling sphere as entirely behind the plane. */
	CHECK_EQ(CollisionMath::Overlap_Test(p, SphereClass(Vector3(0.0f, 0.0f, 0.0f), 1.0f)),
	         CollisionMath::BOTH);
	CHECK_EQ(CollisionMath::Overlap_Test(p, SphereClass(Vector3(0.0f, 0.0f, 0.5f), 1.0f)),
	         CollisionMath::BOTH);
	CHECK_EQ(CollisionMath::Overlap_Test(p, SphereClass(Vector3(0.0f, 0.0f, -0.5f), 1.0f)),
	         CollisionMath::BOTH);
}

TEST(colmath_sphere_sphere_and_sphere_point)
{
	SphereClass s(Vector3(0.0f, 0.0f, 0.0f), 2.0f);

	CHECK_EQ(CollisionMath::Overlap_Test(s, Vector3(1.0f, 0.0f, 0.0f)),
	         CollisionMath::INSIDE);
	CHECK_EQ(CollisionMath::Overlap_Test(s, Vector3(9.0f, 0.0f, 0.0f)),
	         CollisionMath::OUTSIDE);

	/* Sphere/sphere is coarser than the enum suggests: anything within the
	   summed radii reports INSIDE, and OVERLAPPED is reachable only for two
	   concentric spheres of equal radius.  Pinned as-is - callers of this
	   overload cannot tell containment from a graze. */
	CHECK_EQ(CollisionMath::Overlap_Test(s, SphereClass(Vector3(0.0f, 0.0f, 0.0f), 0.5f)),
	         CollisionMath::INSIDE);
	CHECK_EQ(CollisionMath::Overlap_Test(s, SphereClass(Vector3(20.0f, 0.0f, 0.0f), 1.0f)),
	         CollisionMath::OUTSIDE);
	CHECK_EQ(CollisionMath::Overlap_Test(s, SphereClass(Vector3(2.5f, 0.0f, 0.0f), 1.0f)),
	         CollisionMath::INSIDE);
	CHECK_EQ(CollisionMath::Overlap_Test(s, SphereClass(Vector3(0.0f, 0.0f, 0.0f), 2.0f)),
	         CollisionMath::OVERLAPPED);
}

TEST(colmath_box_intersection_tests)
{
	AABoxClass a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	AABoxClass overlapping(Vector3(1.5f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	AABoxClass disjoint(Vector3(50.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));

	CHECK(CollisionMath::Intersection_Test(a, overlapping));
	CHECK(!CollisionMath::Intersection_Test(a, disjoint));

	/* Touching exactly at a face counts as intersecting. */
	AABoxClass touching(Vector3(2.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
	CHECK(CollisionMath::Intersection_Test(a, touching));

	/* Same answers through the sphere/box pair.  This one had its three axis
	   comparisons inverted, so it answered true only when the sphere was
	   clear of the box on every axis - the exact opposite of the question. */
	CHECK(CollisionMath::Intersection_Test(SphereClass(Vector3(1.5f, 0.0f, 0.0f), 1.0f), a));
	CHECK(CollisionMath::Intersection_Test(SphereClass(Vector3(0.0f, 0.0f, 0.0f), 0.1f), a));
	CHECK(!CollisionMath::Intersection_Test(SphereClass(Vector3(50.0f, 0.0f, 0.0f), 1.0f), a));
	CHECK(!CollisionMath::Intersection_Test(SphereClass(Vector3(0.0f, 0.0f, 9.0f), 1.0f), a));
}

TEST(colmath_obb_separating_axis)
{
	/* Two unit cubes, one spun 45 degrees about Z.  Near enough to overlap,
	   far enough that only a proper separating-axis test gets it right. */
	OBBoxClass a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));

	Matrix3x3 spin(Vector3(0.0f, 0.0f, 1.0f), WWMATH_PI * 0.25f);
	OBBoxClass b(Vector3(2.2f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), spin);
	CHECK(CollisionMath::Intersection_Test(a, b));

	OBBoxClass c(Vector3(3.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), spin);
	CHECK(!CollisionMath::Intersection_Test(a, c));
}

TEST(colmath_box_tri_intersection)
{
	Vector3 v0(0.0f, 0.0f, 0.0f);
	Vector3 v1(4.0f, 0.0f, 0.0f);
	Vector3 v2(0.0f, 4.0f, 0.0f);
	Vector3 n;

	TriClass tri;
	tri.V[0] = &v0; tri.V[1] = &v1; tri.V[2] = &v2; tri.N = &n;
	tri.Compute_Normal();

	CHECK(CollisionMath::Intersection_Test(
	          AABoxClass(Vector3(1.0f, 1.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f)), tri));
	CHECK(!CollisionMath::Intersection_Test(
	          AABoxClass(Vector3(1.0f, 1.0f, 50.0f), Vector3(1.0f, 1.0f, 1.0f)), tri));
	/* Beyond the hypotenuse but still in the triangle's plane. */
	CHECK(!CollisionMath::Intersection_Test(
	          AABoxClass(Vector3(9.0f, 9.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f)), tri));
}

//////////////////////////////////////////////////////////////////////////////
// VectorProcessorClass - the SIMD/asm batch paths
//////////////////////////////////////////////////////////////////////////////

TEST(vp_transform_matches_scalar)
{
	/* Matrix3D's axis-angle form asserts a unit axis; a Debug build aborts on anything else. */
	Vector3 axis(0.3f, 0.6f, 0.74f);
	axis.Normalize();
	Matrix3D m(axis, 0.9f);
	m.Set_Translation(Vector3(5.0f, -2.0f, 1.0f));

	/* Deliberately not a multiple of 4: the asm paths process blocks and hand
	   the remainder to a scalar tail, which is where they get truncated. */
	const int N = 13;
	Vector3 src[N], dst[N];
	for (int i = 0; i < N; ++i)
		src[i] = Vector3(float(i), float(i) * -0.5f, float(N - i));

	VectorProcessorClass::Transform(dst, src, m, N);

	for (int i = 0; i < N; ++i)
	{
		Vector3 expect = xform(m, src[i]);
		CHECK_NEAR(dst[i].X, expect.X, 1e-3f);
		CHECK_NEAR(dst[i].Y, expect.Y, 1e-3f);
		CHECK_NEAR(dst[i].Z, expect.Z, 1e-3f);
	}
}

TEST(vp_transform_vector4_matches_scalar)
{
	Matrix4x4 m(true);
	m[0][0] = 2.0f; m[1][1] = 3.0f; m[2][2] = -1.0f;
	m[0][3] = 7.0f;

	const int N = 7;
	Vector3 src[N];
	Vector4 dst[N];
	for (int i = 0; i < N; ++i)
		src[i] = Vector3(float(i), 1.0f, 2.0f);

	VectorProcessorClass::Transform(dst, src, m, N);

	for (int i = 0; i < N; ++i)
	{
		Vector4 expect = m * Vector4(src[i].X, src[i].Y, src[i].Z, 1.0f);
		CHECK_NEAR(dst[i].X, expect.X, 1e-3f);
		CHECK_NEAR(dst[i].W, expect.W, 1e-3f);
	}
}

TEST(vp_copy_and_copy_indexed)
{
	const int N = 9;
	Vector3 src[N], dst[N];
	for (int i = 0; i < N; ++i)
		src[i] = Vector3(float(i), float(i * 2), float(i * 3));

	VectorProcessorClass::Copy(dst, src, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(dst[i].Y, float(i * 2), EPS);

	/* Reverse the order through the index path. */
	unsigned int index[N];
	for (int i = 0; i < N; ++i)
		index[i] = unsigned(N - 1 - i);

	VectorProcessorClass::CopyIndexed(dst, src, index, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(dst[i].X, float(N - 1 - i), EPS);

	/* The Vector4-from-Vector3-plus-alpha overload. */
	Vector4 v4[N];
	VectorProcessorClass::Copy(v4, src, 0.5f, N);
	for (int i = 0; i < N; ++i)
	{
		CHECK_NEAR(v4[i].X, float(i), EPS);
		CHECK_NEAR(v4[i].W, 0.5f, EPS);
	}
}

TEST(vp_normalize_clear_minmax)
{
	const int N = 6;
	Vector3 v[N];
	for (int i = 0; i < N; ++i)
		v[i] = Vector3(float(i + 1), float(i + 2), float(i + 3));

	Vector3 min, max;
	VectorProcessorClass::MinMax(v, min, max, N);
	CHECK_NEAR(min.X, 1.0f, EPS);
	CHECK_NEAR(max.X, float(N), EPS);
	CHECK_NEAR(max.Z, float(N + 2), EPS);

	VectorProcessorClass::Normalize(v, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(v[i].Length(), 1.0f, 1e-3f);

	VectorProcessorClass::Clear(v, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(v[i].Length2(), 0.0f, EPS);
}

TEST(vp_scalar_batch_ops)
{
	const int N = 11;
	float a[N], b[N];
	for (int i = 0; i < N; ++i)
		a[i] = float(i) - 5.0f;

	VectorProcessorClass::ClampMin(b, a, 0.0f, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(b[i], a[i] < 0.0f ? 0.0f : a[i], EPS);

	for (int i = 0; i < N; ++i)
		a[i] = float(i);
	VectorProcessorClass::MulAdd(a, 2.0f, 1.0f, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(a[i], float(i) * 2.0f + 1.0f, EPS);

	for (int i = 0; i < N; ++i)
		a[i] = float(i) + 1.0f;
	VectorProcessorClass::Power(b, a, 2.0f, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(b[i], (float(i) + 1.0f) * (float(i) + 1.0f), 1e-2f);

	Vector3 axis(1.0f, 0.0f, 0.0f);
	Vector3 vecs[N];
	float dots[N];
	for (int i = 0; i < N; ++i)
		vecs[i] = Vector3(float(i), 99.0f, -99.0f);
	VectorProcessorClass::DotProduct(dots, axis, vecs, N);
	for (int i = 0; i < N; ++i)
		CHECK_NEAR(dots[i], float(i), 1e-3f);
}

TEST(vp_clamp_vector4)
{
	const int N = 5;
	Vector4 src[N], dst[N];
	for (int i = 0; i < N; ++i)
		src[i] = Vector4(float(i) - 2.0f, 10.0f, -10.0f, 0.5f);

	VectorProcessorClass::Clamp(dst, src, 0.0f, 1.0f, N);
	for (int i = 0; i < N; ++i)
	{
		/* -2,-1,0,1,2 clamped into [0,1].  The low half is the interesting
		   part: the second assignment used to re-read src, throwing away the
		   min clamp and letting negatives through. */
		float expect = WWMath::Clamp(float(i) - 2.0f, 0.0f, 1.0f);
		CHECK_NEAR(dst[i].X, expect, EPS);
		CHECK_NEAR(dst[i].Y, 1.0f, EPS);
		CHECK_NEAR(dst[i].Z, 0.0f, EPS);
		CHECK_NEAR(dst[i].W, 0.5f, EPS);
	}
}


/* ---------------------------------------------------------------------------
   DetTrig - the trigonometry the simulation runs on.

   The point of these functions is not accuracy, it is that every machine gets
   the same bits.  The C runtime's sin and cos are not specified bit for bit,
   they dispatch on the host CPU, and ucrtbase ships with Windows rather than
   with the game; the x87 FSIN and FCOS that WWMath used to inline here are
   microcoded differently by Intel and by AMD.  A lockstep match whose two ends
   disagree about one unit's facing by one bit is two different games a second
   later.

   So there are two kinds of test below.  The accuracy ones compare against the
   runtime and are only asking "is the table good enough to replace it"; the
   bit-stability one is the actual guarantee, and it is the one that fails if a
   table is regenerated or a scale is changed.
   --------------------------------------------------------------------------- */

static float dettrig_worst_error(float (*ours)(float), double (*ref)(double),
	double lo, double hi, int samples)
{
	float worst = 0.0f;
	for (int i = 0; i <= samples; ++i)
	{
		float x = float(lo + (hi - lo) * i / double(samples));
		float err = float(fabs(double(ours(x)) - ref(double(x))));
		if (err > worst)
			worst = err;
	}
	return worst;
}

TEST(dettrig_sin_and_cos_track_the_reference_over_several_turns)
{
	/* One float epsilon at 1.0 is 1.2e-7, and the table's interpolation budget
	   is 7.4e-8 on top of the 24-bit value quantization.  The 4096-entry 12-bit
	   table EA left behind switched off was 7.7e-4 here - 0.04 degrees of
	   permanent heading error - which is why this is a rewrite, not a revival. */
	CHECK(dettrig_worst_error(DetTrig::Sin, sin, -20.0, 20.0, 200000) < 3.0e-7f);
	CHECK(dettrig_worst_error(DetTrig::Cos, cos, -20.0, 20.0, 200000) < 3.0e-7f);
}

TEST(dettrig_reduces_arguments_far_outside_one_turn)
{
	// the reduction runs in double for exactly this reason
	CHECK(dettrig_worst_error(DetTrig::Sin, sin, -1000.0, 1000.0, 200000) < 3.0e-7f);
}

TEST(dettrig_sin_and_cos_are_exact_where_they_can_be)
{
	CHECK_EQ(DetTrig::Sin(0.0f), 0.0f);
	CHECK_EQ(DetTrig::Cos(0.0f), 1.0f);
	CHECK_EQ(DetTrig::Sin(-0.0f), 0.0f);
}

TEST(dettrig_atan2_covers_every_quadrant)
{
	static const float pts[] = { -7.0f, -1.3f, -0.02f, 0.0f, 0.02f, 1.3f, 7.0f };
	float worst = 0.0f;
	for (int i = 0; i < 7; ++i)
	{
		for (int j = 0; j < 7; ++j)
		{
			/* The negative x axis is checked on its own below: atan2 answers
			   -PI for a negative zero y and this answers +PI for both zeroes. */
			if (pts[i] == 0.0f && pts[j] < 0.0f)
				continue;
			float err = float(fabs(double(DetTrig::ATan2(pts[i], pts[j]))
				- atan2(double(pts[i]), double(pts[j]))));
			if (err > worst)
				worst = err;
		}
	}
	CHECK(worst < 3.0e-7f);
}

TEST(dettrig_atan2_answers_the_negative_x_axis_as_positive_pi)
{
	/* Half a turn is 2^31, which does not fit a signed 32-bit int: fold the
	   quadrant in 32 bits and this case wraps round to -PI.  It is folded in 64. */
	CHECK_NEAR(DetTrig::ATan2(0.0f, -1.0f), 3.14159265f, 1.0e-6f);
	CHECK(DetTrig::ATan2(0.0f, -1.0f) > 0.0f);
	CHECK_EQ(DetTrig::ATan2(0.0f, 0.0f), 0.0f);
	CHECK_EQ(DetTrig::ATan2(0.0f, 1.0f), 0.0f);
}

TEST(dettrig_DEFECT_atan2_ignores_the_sign_of_a_zero_y)
{
	/* atan2f(-0.0f, -1.0f) is -PI; this returns +PI, because the quadrant is
	   picked with (y >= 0.0f) and negative zero compares equal to zero.  Nothing
	   the simulation computes an angle from produces a negative zero on purpose,
	   so this is pinned rather than special-cased - a future change to it should
	   be a decision. */
	CHECK_EQ(DetTrig::ATan2(-0.0f, -1.0f), DetTrig::ATan2(0.0f, -1.0f));
}

// A NaN or an infinity has no angle.  fixedAngle takes it as 0, and arcTanUnit takes a NaN ratio as 0,
// explicitly now, where both used to go through an undefined conversion.  These are the answers that
// conversion gave on ARM64 and on x86 alike (measured: this test passes without the guards on both), so
// the test cannot catch the old code; it pins the answers against a future change.  Locomotor feeds
// Sin/Cos a NaN whenever a missile's nose is already on its goal.
TEST(dettrig_nan_and_infinity_have_the_answers_arm64_always_gave)
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	CHECK_EQ(DetTrig::Sin(nan), DetTrig::Sin(0.0f));
	CHECK_EQ(DetTrig::Cos(nan), DetTrig::Cos(0.0f));
	CHECK_EQ(DetTrig::Sin(inf), DetTrig::Sin(0.0f));
	CHECK_EQ(DetTrig::Cos(-inf), DetTrig::Cos(0.0f));
	CHECK_EQ(DetTrig::Tan(nan), DetTrig::Tan(0.0f));
	CHECK_EQ(DetTrig::ATan2(inf, inf), 0.0f);							// inf / inf is a NaN ratio
	CHECK_EQ(DetTrig::ATan2(nan, 1.0f), DetTrig::ATan2(-1.0f, 0.0f));	// the quadrant tests all fail
	CHECK_EQ(DetTrig::ATan2(1.0f, nan), DetTrig::ATan2(1.0f, 0.0f));	// x >= 0 fails, y >= 0 holds
	CHECK_EQ(DetTrig::ACos(nan), -DetTrig::ACos(-1.0f));
	CHECK_EQ(DetTrig::ASin(nan), DetTrig::ATan2(-1.0f, 0.0f));
	CHECK_EQ(DetTrig::ATan(nan), DetTrig::ATan2(-1.0f, 0.0f));
}

TEST(dettrig_acos_and_asin_track_the_reference)
{
	CHECK(dettrig_worst_error(DetTrig::ACos, acos, -1.0, 1.0, 200000) < 6.0e-7f);
	CHECK(dettrig_worst_error(DetTrig::ASin, asin, -1.0, 1.0, 200000) < 5.0e-7f);

	CHECK_NEAR(DetTrig::ACos(-1.0f), 3.14159265f, 1.0e-6f);
	CHECK_EQ(DetTrig::ACos(1.0f), 0.0f);
	CHECK_NEAR(DetTrig::ASin(1.0f), 1.57079633f, 1.0e-6f);
	CHECK_NEAR(DetTrig::ASin(-1.0f), -1.57079633f, 1.0e-6f);

	/* Out of domain clamps instead of returning a NaN.  A dot product of two
	   normalised vectors lands a hair outside [-1,1] all the time, and the
	   callers have never checked. */
	CHECK_EQ(DetTrig::ACos(-2.0f), DetTrig::ACos(-1.0f));
	CHECK_EQ(DetTrig::ASin(2.0f), DetTrig::ASin(1.0f));
}

TEST(dettrig_acos_keeps_its_accuracy_next_to_one)
{
	/* The half-angle form, 2*atan2(sqrt(1-x), sqrt(1+x)), is there for this:
	   the obvious atan2(sqrt(1 - x*x), x) has cancelled its significant bits
	   away by the time x reaches 0.9999. */
	static const float near_one[] = { 0.99f, 0.9999f, 0.999999f, -0.99f, -0.9999f };
	for (int i = 0; i < 5; ++i)
		CHECK_NEAR(DetTrig::ACos(near_one[i]), acos(double(near_one[i])), 3.0e-7);
}

TEST(dettrig_tan_matches_the_reference_away_from_the_poles)
{
	for (int i = -1000; i <= 1000; ++i)
	{
		float x = float(i) * 0.0014f;		// stays inside +-1.4, clear of PI/2
		CHECK_NEAR(DetTrig::Tan(x), tan(double(x)), 4.0e-6);
	}
}

TEST(dettrig_identities_hold)
{
	for (int i = 0; i < 3000; ++i)
	{
		float a = float(i) * 0.00419f - 6.0f;
		float s = DetTrig::Sin(a);
		float c = DetTrig::Cos(a);
		CHECK_NEAR(s * s + c * c, 1.0f, 5.0e-7f);

		if (a > -3.14f && a < 3.14f)
			CHECK_NEAR(DetTrig::ATan2(s, c), a, 5.0e-7f);
	}
}

TEST(dettrig_is_repeatable)
{
	// no state, no table built at startup, no dependence on the rounding mode
	for (int i = 0; i < 500; ++i)
	{
		float a = float(i) * 0.017f;
		CHECK_EQ(DetTrig::Sin(a), DetTrig::Sin(a));
		CHECK_EQ(DetTrig::ATan2(a, 1.0f - a), DetTrig::ATan2(a, 1.0f - a));
		CHECK_EQ(DetTrig::ACos(a * 0.002f), DetTrig::ACos(a * 0.002f));
	}
}

TEST(dettrig_output_is_bit_stable)
{
	/* The whole point of the exercise: this number is a property of the source
	   and not of the machine.  Two builds of the same tree must produce it on
	   any CPU and any Windows.  If it changes after an edit to dettrig.cpp or
	   to Tools/gentrigtables.py, that edit moved every angle in the simulation
	   and invalidated every replay along with it. */
	unsigned int sum = 2166136261u;
	for (int i = 0; i < 4096; ++i)
	{
		float a = float(i) * 0.0031415f - 6.2831f;
		float vals[6];
		vals[0] = DetTrig::Sin(a);
		vals[1] = DetTrig::Cos(a);
		vals[2] = DetTrig::Tan(a * 0.2f);
		vals[3] = DetTrig::ATan2(a, 1.7f - a);
		vals[4] = DetTrig::ACos(a * 0.15f);
		vals[5] = DetTrig::ASin(a * 0.15f);
		for (int k = 0; k < 6; ++k)
		{
			unsigned int bits;
			memcpy(&bits, &vals[k], sizeof(bits));
			sum = (sum ^ bits) * 16777619u;
		}
	}

	const unsigned int expected = 0xD70EF14Bu;
	if (sum != expected)
		printf("    dettrig fingerprint is 0x%08X, expected 0x%08X\n", sum, expected);
	CHECK_EQ(sum, expected);
}

TEST(wwmath_trig_is_dettrig)
{
	// WWMath::Sin and Cos were inline x87 FSIN/FCOS - the literal hazard here.
	for (int i = 0; i < 200; ++i)
	{
		float a = float(i) * 0.031f - 3.1f;
		CHECK_EQ(WWMath::Sin(a), DetTrig::Sin(a));
		CHECK_EQ(WWMath::Cos(a), DetTrig::Cos(a));
		CHECK_EQ(WWMath::Atan2(a, 1.3f), DetTrig::ATan2(a, 1.3f));
		CHECK_EQ(WWMath::Acos(a * 0.3f), DetTrig::ACos(a * 0.3f));
		CHECK_EQ(WWMath::Asin(a * 0.3f), DetTrig::ASin(a * 0.3f));
	}
}

TEST(rotation_helpers_go_through_dettrig)
{
	/* This is the one that matters to the game: an object's facing round trips
	   through Matrix3D::Rotate_Z on the way in and Get_Z_Rotation on the way
	   out, every logic frame.  Both are WWMath's, which is why the table lives
	   here and not in GameEngine. */
	const float theta = 0.7f;

	Matrix3D m;
	m.Make_Identity();
	m.Rotate_Z(theta);
	CHECK_EQ(m[0][0], DetTrig::Cos(theta));
	CHECK_EQ(m[1][0], DetTrig::Sin(theta));
	CHECK_EQ(m.Get_Z_Rotation(),
		DetTrig::ATan2(DetTrig::Sin(theta), DetTrig::Cos(theta)));

	Matrix3x3 m3;
	m3.Make_Identity();
	m3.Rotate_X(theta);
	CHECK_EQ(m3[1][1], DetTrig::Cos(theta));

	Vector3 v(1.0f, 0.0f, 0.0f);
	v.Rotate_Z(theta);
	CHECK_EQ(v.X, DetTrig::Cos(theta));
	CHECK_EQ(v.Y, DetTrig::Sin(theta));
}
