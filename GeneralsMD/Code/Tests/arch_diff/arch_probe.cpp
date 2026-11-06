/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * The probe half of the x86_64/arm64 differential harness.  See run_arch_diff.sh for what this
 * proves and, more importantly, what it does not.
 *
 * It is compiled twice from this one file - once for arm64, once for x86_64 - and run twice.  It
 * computes, it does not judge: every value it can reach goes to stdout as
 *
 *     section <TAB> key <TAB> value
 *
 * and the comparison happens outside, between the two runs.  There is deliberately no expected
 * value anywhere in this file.  An expected value would be a third opinion, and the whole point of
 * the exercise is that the two architectures are each other's reference.
 *
 * Floats and doubles are printed as their bit patterns, never as decimal.  A decimal rendering
 * loses exactly the last-bit differences this harness exists to find, and printf's own rounding
 * would become part of the measurement.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "wwmath.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define PROBE_ARM64 1
#define PROBE_ARCH "arm64"
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define PROBE_SSE2 1
#define PROBE_ARCH "x86_64"
#else
#error "arch_probe has no implementation for this architecture."
#endif

/* Lib/DetRound.h arrives with B3.  Until then the convert section below still covers the same
** instructions directly, so the harness has teeth on either side of that merge. */
#if defined(__has_include)
#  if __has_include("Lib/DetRound.h")
#    include "Lib/DetRound.h"
#    define PROBE_HAS_DETROUND 1
#  endif
#endif

#if defined(PROBE_WITH_DETTRIG)
#include "dettrig.h"
#endif

/* B17's portable D3DXVec4Transform, the arithmetic a shell's flight path is built from. */
#if defined(__has_include)
#  if __has_include("d3dxportable.h") && __has_include("d3dx_sweep.h")
#    include "d3dxportable.h"
#    include "d3dx_sweep.h"
#    define PROBE_HAS_D3DX 1
#  endif
#endif

static void row_l(const char *section, const char *key, long v)
{
	printf("%s\t%s\t%ld\n", section, key, v);
}

static void row_f(const char *section, const char *key, float v)
{
	unsigned int bits;
	memcpy(&bits, &v, sizeof(bits));
	printf("%s\t%s\t0x%08x\n", section, key, bits);
}

static void row_d(const char *section, const char *key, double v)
{
	unsigned long long bits;
	memcpy(&bits, &v, sizeof(bits));
	printf("%s\t%s\t0x%016llx\n", section, key, bits);
}

/* ------------------------------------------------------------------ the conversions
 *
 * Each architecture's round-half-to-even float->long, which is what DetRound.h selects between.
 * This is where the known divergence lives: the two agree on every in-range value and every tie,
 * and disagree on NaN, the infinities and anything outside int32.
 */
static long native_to_long_f(float f)
{
#if defined(PROBE_ARM64)
	return (long)vcvtns_s32_f32(f);
#else
	return (long)_mm_cvtss_si32(_mm_set_ss(f));
#endif
}

static long native_to_long_d(double d)
{
#if defined(PROBE_ARM64)
	return (long)vcvtnd_s64_f64(d);
#else
	return (long)_mm_cvtsd_si32(_mm_set_sd(d));
#endif
}

static const struct { const char *key; float v; } CONVERT_F[] = {
	{ "0",              0.0f },        { "-0",            -0.0f },
	{ "1",              1.0f },        { "-1",            -1.0f },
	{ "0.5",            0.5f },        { "1.5",            1.5f },
	{ "2.5",            2.5f },        { "3.5",            3.5f },
	{ "4.5",            4.5f },        { "-0.5",          -0.5f },
	{ "-1.5",          -1.5f },        { "-2.5",          -2.5f },
	{ "-3.5",          -3.5f },        { "-4.5",          -4.5f },
	{ "below-tie",      0.49999997f }, { "above-tie",      0.50000006f },
	{ "2.4999998",      2.4999998f },  { "2.5000005",      2.5000005f },
	{ "3.4",            3.4f },        { "-3.4",          -3.4f },
	{ "3.6",            3.6f },        { "-3.6",          -3.6f },
	{ "0.4",            0.4f },        { "-0.4",          -0.4f },
	{ "0.6",            0.6f },        { "-0.6",          -0.6f },
	{ "last-fraction",  8388607.5f },  { "first-odd-2^23", 8388609.0f },
	{ "max-in-int32",   2147483520.0f },
	{ "min-in-int32",  -2147483520.0f },
	{ "pos-2^31",       2147483648.0f },
	{ "neg-2^31",      -2147483648.0f },
	{ "1e10",           1e10f },       { "-1e10",         -1e10f },
	{ "flt-min",        1.1754944e-38f },
	{ "-flt-min",      -1.1754944e-38f },
};

static const struct { const char *key; double v; } CONVERT_D[] = {
	{ "0",              0.0 },   { "-0",  -0.0 },
	{ "1",              1.0 },   { "-1",  -1.0 },
	{ "0.5",            0.5 },   { "1.5",  1.5 },
	{ "2.5",            2.5 },   { "3.5",  3.5 },
	{ "4.5",            4.5 },   { "-0.5", -0.5 },
	{ "-1.5",          -1.5 },   { "-2.5", -2.5 },
	{ "-3.5",          -3.5 },   { "-4.5", -4.5 },
	{ "below-tie",      0.49999999999999994 },
	{ "above-tie",      0.5000000000000001 },
	{ "2.4999999999999996", 2.4999999999999996 },
	{ "2.5000000000000004", 2.5000000000000004 },
	{ "3.4",            3.4 },    { "-3.4", -3.4 },
	{ "3.6",            3.6 },    { "-3.6", -3.6 },
	{ "max-int32",      2147483647.0 },
	{ "min-int32",     -2147483647.0 },
	{ "pos-2^31",       2147483648.0 },
	{ "neg-2^31",      -2147483648.0 },
	{ "big-exact",      4503599627370495.5 },
	{ "big-odd",        4503599627370497.0 },
	{ "1e10",           1e10 },   { "-1e10", -1e10 },
};

static void probe_convert(void)
{
	char key[128];
	for (unsigned i = 0; i < sizeof(CONVERT_F) / sizeof(CONVERT_F[0]); ++i) {
		snprintf(key, sizeof(key), "f32/%s", CONVERT_F[i].key);
		row_l("convert", key, native_to_long_f(CONVERT_F[i].v));
	}
	for (unsigned i = 0; i < sizeof(CONVERT_D) / sizeof(CONVERT_D[0]); ++i) {
		snprintf(key, sizeof(key), "f64/%s", CONVERT_D[i].key);
		row_l("convert", key, native_to_long_d(CONVERT_D[i].v));
	}
	/* Kept out of the tables because a NaN is awkward as a constant initialiser. */
	row_l("convert", "f32/nan",   native_to_long_f((float)NAN));
	row_l("convert", "f32/+inf",  native_to_long_f((float)INFINITY));
	row_l("convert", "f32/-inf",  native_to_long_f((float)-INFINITY));
	row_l("convert", "f64/nan",   native_to_long_d((double)NAN));
	row_l("convert", "f64/+inf",  native_to_long_d((double)INFINITY));
	row_l("convert", "f64/-inf",  native_to_long_d((double)-INFINITY));

	/* `long` itself, because the harness is here to catch exactly this kind of assumption. */
	row_l("convert", "sizeof-long", (long)sizeof(long));
}

#if defined(PROBE_HAS_DETROUND)
static void probe_detround(void)
{
	char key[128];
	for (unsigned i = 0; i < sizeof(CONVERT_F) / sizeof(CONVERT_F[0]); ++i) {
		snprintf(key, sizeof(key), "f32/%s", CONVERT_F[i].key);
		row_l("detround", key, DetRound::To_Long(CONVERT_F[i].v));
	}
	for (unsigned i = 0; i < sizeof(CONVERT_D) / sizeof(CONVERT_D[0]); ++i) {
		snprintf(key, sizeof(key), "f64/%s", CONVERT_D[i].key);
		row_l("detround", key, DetRound::To_Long(CONVERT_D[i].v));
	}
	row_l("detround", "f32/nan",  DetRound::To_Long((float)NAN));
	row_l("detround", "f32/+inf", DetRound::To_Long((float)INFINITY));
	row_l("detround", "f64/nan",  DetRound::To_Long((double)NAN));
	row_l("detround", "f64/+inf", DetRound::To_Long((double)INFINITY));
}
#endif

static void probe_wwmath_value(const char *key, float value)
{
	// Force actual target instructions rather than compile-time evaluation on the build host.
	volatile float input = value;
	const float f = input;
	char named[128];
	snprintf(named, sizeof(named), "chop/%s", key);
	row_l("wwmath-convert", named, WWMath::Float_To_Int_Chop(f));
	snprintf(named, sizeof(named), "floor/%s", key);
	row_l("wwmath-convert", named, WWMath::Float_To_Int_Floor(f));
}

static void probe_wwmath_convert(void)
{
	for (unsigned i = 0; i < sizeof(CONVERT_F) / sizeof(CONVERT_F[0]); ++i) {
		probe_wwmath_value(CONVERT_F[i].key, CONVERT_F[i].v);
	}
	// Boundary manifest: signed subnormals, neighbours of one and the int32 limits,
	// largest finite values, infinities and both NaN signs. These helpers must agree even
	// where their former shift-based implementation had no defined C++ result.
	const struct { const char *key; uint32_t bits; } boundaries[] = {
		{ "+denorm-min", 0x00000001u }, { "-denorm-min", 0x80000001u },
		{ "+denorm-max", 0x007FFFFFu }, { "-denorm-max", 0x807FFFFFu },
		{ "below-one", 0x3F7FFFFFu }, { "above-neg-one", 0xBF7FFFFFu },
		{ "above-one", 0x3F800001u }, { "below-neg-one", 0xBF800001u },
		{ "above-pos-2^31", 0x4F000001u }, { "below-neg-2^31", 0xCF000001u },
		{ "+flt-max", 0x7F7FFFFFu }, { "-flt-max", 0xFF7FFFFFu },
		{ "+inf", 0x7F800000u }, { "-inf", 0xFF800000u },
		{ "+nan", 0x7FC00000u }, { "-nan", 0xFFC00000u },
	};
	for (unsigned i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
		float value;
		memcpy(&value, &boundaries[i].bits, sizeof(value));
		probe_wwmath_value(boundaries[i].key, value);
	}
	uint32_t fingerprint = 2166136261u;
	for (int quarter = -8192; quarter <= 8192; ++quarter) {
		volatile float input = quarter * 0.25f;
		const float value = input;
		fingerprint = (fingerprint ^ (uint32_t)WWMath::Float_To_Int_Chop(value)) * 16777619u;
		fingerprint = (fingerprint ^ (uint32_t)WWMath::Float_To_Int_Floor(value)) * 16777619u;
	}
	printf("wwmath-convert\tquarter-sweep\t0x%08x\n", fingerprint);
}

/* ------------------------------------------------------------------ float expressions
 *
 * What E1 is actually about.  -ffp-contract=off is the flag that stops clang folding a*b+c into one
 * fused multiply-add on arm64 while MSVC does not fuse it; this section computes shapes where the
 * fusion would show, so that a build which loses the flag is caught by a difference here rather than by a desync
 * six months later.
 */
static float g_a = 0.1f, g_b = 0.2f, g_c = 0.3f;   /* not const: keep it out of the constant folder */
static double g_x = 0.1, g_y = 0.2, g_z = 0.3;

static void probe_fpexpr(void)
{
	row_f("fpexpr", "f32/mul-add",    g_a * g_b + g_c);
	row_f("fpexpr", "f32/mul-sub",    g_a * g_b - g_c);
	row_f("fpexpr", "f32/nested-fma", (g_a * g_b + g_c) * g_a + g_b);
	row_d("fpexpr", "f64/mul-add",    g_x * g_y + g_z);
	row_d("fpexpr", "f64/nested-fma", (g_x * g_y + g_z) * g_x + g_y);

	row_f("fpexpr", "f32/sqrt",       sqrtf(g_a));
	row_d("fpexpr", "f64/sqrt",       sqrt(g_x));
	row_f("fpexpr", "f32/div",        g_a / g_c);
	row_d("fpexpr", "f64/div",        g_x / g_z);

	/* An accumulation long enough that any reassociation shows up in the low bits. */
	float acc = 0.0f;
	for (int i = 1; i <= 10000; ++i) acc += 1.0f / (float)i;
	row_f("fpexpr", "f32/harmonic-10000", acc);

	double dacc = 0.0;
	for (int i = 1; i <= 10000; ++i) dacc += 1.0 / (double)i;
	row_d("fpexpr", "f64/harmonic-10000", dacc);

	/* float <-> double crossings, where an x87-era build would have kept extra precision. */
	row_f("fpexpr", "f32/from-f64", (float)(g_x * g_y));
	row_d("fpexpr", "f64/from-f32", (double)(g_a * g_b));
}

#if defined(PROBE_WITH_DETTRIG)
/* ------------------------------------------------------------------ DetTrig
 *
 * dettrig.h replaced the CRT transcendentals with integer arithmetic over a committed table so that
 * two machines cannot disagree by a bit.  It should therefore be identical across architectures,
 * and that is worth asserting rather than assuming: this is the part of the simulation's arithmetic
 * with the most room to differ, and it is the reason dettrig exists at all.
 *
 * Every output goes over as a bit pattern, plus one FNV-1a fingerprint over a dense sweep so that a
 * difference anywhere in the range is caught without shipping 20,000 rows.
 */
static void probe_dettrig(void)
{
	static const float ANGLES[] = {
		0.0f, 1e-8f, 0.5f, 1.0f, 1.5707963f, 3.1415927f, -3.1415927f,
		4.712389f, 6.2831853f, 100.0f, -100.0f, 1e6f, -1e6f,
	};
	char key[128];
	for (unsigned i = 0; i < sizeof(ANGLES) / sizeof(ANGLES[0]); ++i) {
		snprintf(key, sizeof(key), "sin/%u", i);  row_f("dettrig", key, DetTrig::Sin(ANGLES[i]));
		snprintf(key, sizeof(key), "cos/%u", i);  row_f("dettrig", key, DetTrig::Cos(ANGLES[i]));
		snprintf(key, sizeof(key), "tan/%u", i);  row_f("dettrig", key, DetTrig::Tan(ANGLES[i]));
		snprintf(key, sizeof(key), "atan/%u", i); row_f("dettrig", key, DetTrig::ATan(ANGLES[i]));
	}
	static const float UNITS[] = { -1.0f, -0.75f, -0.5f, 0.0f, 0.5f, 0.75f, 1.0f, 2.0f, -2.0f };
	for (unsigned i = 0; i < sizeof(UNITS) / sizeof(UNITS[0]); ++i) {
		snprintf(key, sizeof(key), "acos/%u", i); row_f("dettrig", key, DetTrig::ACos(UNITS[i]));
		snprintf(key, sizeof(key), "asin/%u", i); row_f("dettrig", key, DetTrig::ASin(UNITS[i]));
	}
	for (int y = -2; y <= 2; ++y) {
		for (int x = -2; x <= 2; ++x) {
			snprintf(key, sizeof(key), "atan2/%d,%d", y, x);
			row_f("dettrig", key, DetTrig::ATan2((float)y, (float)x));
		}
	}

	unsigned int sum = 2166136261u;
	for (int i = 0; i < 20000; ++i) {
		const float t = -20.0f + (float)i * 0.002f;
		const float vals[4] = { DetTrig::Sin(t), DetTrig::Cos(t), DetTrig::ATan(t),
		                        DetTrig::ATan2(t, 1.0f) };
		for (int k = 0; k < 4; ++k) {
			unsigned int bits;
			memcpy(&bits, &vals[k], sizeof(bits));
			sum = (sum ^ bits) * 16777619u;
		}
	}
	printf("dettrig\tsweep-fingerprint\t0x%08x\n", sum);
}
#endif

#if defined(PROBE_HAS_D3DX)
/* ------------------------------------------------------------------ D3DX
 *
 * d3dxportable.h stands in for d3dx9_43.dll on the replay CRC path.  Tests/d3dx_oracle compares
 * its x86_64 build with Microsoft's own machine code over D3DX_SWEEP_COUNT inputs.  This section
 * is the other link in that chain: the same header on arm64 against x86_64, over the same inputs,
 * as one fingerprint.  The oracle prints its fingerprint too, and while the two agree, the arm64
 * build computes what the DLL's scalar and non-Intel bodies compute on every one of those inputs.
 * The golden rows are printed individually so that a difference can be named.
 *
 * The one row that is expected to differ is inf-w, where inf*0 generates a NaN.  x86 generates
 * 0xFFC00000 and arm64 0x7FC00000.  C cannot pin a generated NaN, and a NaN here means the
 * simulation has already diverged; the row is here so that the caveat is measured rather than
 * asserted.
 */
/* Volatile: a static that is never written is a constant to clang, which then folds the NaN at
 * compile time as 0x7FC00000 on both targets, and the row measures the compiler instead of the
 * hardware.  Measured: without volatile both columns read 0x7FC00000. */
static volatile float g_inf_w[4] = { 1.0f, 1.0f, 1.0f, INFINITY };

static void probe_d3dx(void)
{
	char key[64];
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		float in[4];
		float out[4];
		for (int lane = 0; lane < 4; ++lane) {
			memcpy(&in[lane], &D3DX_GOLDEN_ROWS[row].in[lane], sizeof(float));
		}
		D3DXPortable::Vec4Transform(out, in, D3DX_GOLDEN_BASIS);
		for (int lane = 0; lane < 4; ++lane) {
			snprintf(key, sizeof(key), "golden/%u/%c", row, "xyzw"[lane]);
			row_f("d3dx", key, out[lane]);
		}
	}

	static const float CANCEL_L[4] = { 1e8f, 1.0f, -1e8f, 1.0f };
	static const float ONES[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	row_f("d3dx", "dot/cancellation", D3DXPortable::Vec4Dot(CANCEL_L, ONES));

	const float inf_w[4] = { g_inf_w[0], g_inf_w[1], g_inf_w[2], g_inf_w[3] };
	float inf_out[4];
	D3DXPortable::Vec4Transform(inf_out, inf_w, D3DX_GOLDEN_BASIS);
	for (int lane = 0; lane < 4; ++lane) {
		snprintf(key, sizeof(key), "inf-w/%c", "xyzw"[lane]);
		row_f("d3dx", key, inf_out[lane]);
	}

	D3DXSweepState state = { D3DX_SWEEP_SEED };
	unsigned int hash = D3DX_SWEEP_HASH_BASIS;
	for (unsigned int index = 0; index < D3DX_SWEEP_COUNT; ++index) {
		float vector[4];
		float matrix[16];
		float out[4];
		d3dx_sweep_input(&state, index, vector, matrix);
		D3DXPortable::Vec4Transform(out, vector, matrix);
		hash = d3dx_sweep_mix(hash, out);
	}
	printf("d3dx\tsweep-fingerprint\t0x%08x\n", hash);
}
#endif

int main(void)
{
	printf("meta\tarch\t%s\n", PROBE_ARCH);
	probe_convert();
	probe_wwmath_convert();
#if defined(PROBE_HAS_DETROUND)
	probe_detround();
#endif
	probe_fpexpr();
#if defined(PROBE_WITH_DETTRIG)
	probe_dettrig();
#endif
#if defined(PROBE_HAS_D3DX)
	probe_d3dx();
#endif
	return 0;
}
