/*
 * wwlib coverage.
 *
 * wwlib is the widest of the ported libraries - codecs, hashes, containers,
 * strings, file abstractions and the CPU probe all live here.  The tests
 * concentrate on:
 *   1. Anything with a published answer (SHA-1, MD5, RC4, base64) - those are
 *      checked against the standard vectors, not against what the code does.
 *   2. Round trips, for the codecs that have no published vectors (LCW, RLE,
 *      Blowfish, chunk IO).  A codec that round trips wrong is a corrupted
 *      save game, and nothing else in the build would notice.
 *   3. cpudetect, which already produced one runtime-only port bug
 *      (os_info was left uninitialised on Windows >= Vista).
 */
#include "test_harness.h"

#include <atomic>	/* the B14 thread tests below share flags between threads */
#include <string>	/* next_thread_name */
#include <chrono>	/* mutexclass_timed_acquire_gives_up_and_says_so times its wait finer than Clock_Milliseconds_Coarse */

#include "global.h"       /* UINT4 / PROTO_LIST, which md5.h assumes */
#include "realcrc.h"
#include "crc.h"
#include "base64.h"
#include "blowfish.h"
#include "rc4.h"
#include "sha.h"
#include "sha256.h"
#include "md5.h"
#include "lcw.h"
#include "rle.h"
#include "random.h"
#include "fixed.h"
#include "wwstring.h"
#include "stringex.h"
#include "widestring.h"
#include "trim.h"
#include "nstrdup.h"
#include "strtok_r.h"
#include "gcd_lcm.h"
#include "Vector.H"
#include "simplevec.h"
#include "hash.h"
#include "multilist.h"
#include "cpudetect.h"
#include "ramfile.h"
#include "chunkio.h"
#include "wwfile.h"
#include "rawfile.h"
#include "mixfile.h"
#include "ffactory.h"
#include "thread.h"
#include "mutex.h"
#include "Lib/Clock.h"	/* Clock_Milliseconds_Coarse: GetTickCount on Windows, by Clock.h's contract */

#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/time.h>	/* utimes, for the RawFileClass date test */
#endif

//////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////

/* Render a digest as lowercase hex so failures print something you can paste
   into a reference table instead of a row of byte values. */
static void to_hex(const unsigned char *bytes, int count, char *out)
{
	static const char *digits = "0123456789abcdef";
	for (int i = 0; i < count; ++i)
	{
		out[i * 2 + 0] = digits[(bytes[i] >> 4) & 0xf];
		out[i * 2 + 1] = digits[bytes[i] & 0xf];
	}
	out[count * 2] = 0;
}

/* Deterministic filler with enough structure to be compressible but enough
   noise that a codec cannot pass by accident. */
static void fill_pattern(unsigned char *buf, int len, unsigned seed)
{
	RandomClass rng(seed);
	for (int i = 0; i < len; ++i)
	{
		if ((i / 17) % 3 == 0)
			buf[i] = 0;                                  /* runs, for RLE */
		else if ((i / 29) % 4 == 0)
			buf[i] = (unsigned char)(i & 0xff);          /* ramps, for LCW */
		else
			buf[i] = (unsigned char)rng(0, 255);
	}
}

//////////////////////////////////////////////////////////////////////////////
// CRC
//////////////////////////////////////////////////////////////////////////////

TEST(realcrc_string_and_memory_agree)
{
	const char *text = "Command & Conquer";
	CHECK_EQ(CRC_String(text), CRC_Memory((const unsigned char *)text, (unsigned long)strlen(text)));

	/* Different input must not collide on anything this short. */
	CHECK_NE(CRC_String("abc"), CRC_String("abd"));
	CHECK_NE(CRC_String("abc"), CRC_String("cba"));
	CHECK_EQ(CRC_String(""), 0ul);
}

TEST(realcrc_seed_chains_across_calls)
{
	/* The crc argument is a continuation seed: hashing a buffer in two pieces
	   has to equal hashing it whole, or every streaming caller is wrong. */
	const unsigned char data[] = "the quick brown fox jumps over the lazy dog";
	const unsigned long len = (unsigned long)(sizeof(data) - 1);

	unsigned long whole = CRC_Memory(data, len);
	for (unsigned long split = 0; split <= len; ++split)
	{
		unsigned long part = CRC_Memory(data, split);
		part = CRC_Memory(data + split, len - split, part);
		CHECK_EQ(part, whole);
	}
}

TEST(realcrc_case_insensitive_variant)
{
	CHECK_EQ(CRC_Stringi("MiXeD CaSe"), CRC_Stringi("mixed case"));
	CHECK_EQ(CRC_Stringi("MiXeD CaSe"), CRC_Stringi("MIXED CASE"));
	CHECK_NE(CRC_String("MiXeD CaSe"), CRC_String("mixed case"));
}

TEST(crc_class_table_variant)
{
	const char *text = "Zero Hour";
	unsigned char buf[16];
	memcpy(buf, text, strlen(text));

	CHECK_EQ(CRC::String(text), CRC::Memory(buf, (unsigned long)strlen(text)));
	CHECK_NE(CRC::String("Zero Hour"), CRC::String("Zero Hou"));

	/* Same chaining contract as realcrc. */
	unsigned long a = CRC::Memory(buf, 4);
	CHECK_EQ(CRC::Memory(buf + 4, (unsigned long)strlen(text) - 4, a),
	         CRC::Memory(buf, (unsigned long)strlen(text)));
}

TEST(crcengine_byte_at_a_time_matches_block)
{
	const char *text = "Westwood Studios";
	int len = (int)strlen(text);

	CRCEngine block;
	long whole = block(text, len);

	CRCEngine drip;
	for (int i = 0; i < len; ++i)
		drip(text[i]);

	/* Feeding one byte at a time has to land on the same accumulator - the
	   staging buffer exists precisely to make partial blocks equivalent. */
	CHECK_EQ((long)drip, whole);
}

TEST(crcengine_accumulator_is_32_bits_wide)
{
	/* The test above cannot see the bug this one exists for.  CRCEngine's accumulator was `long`
	   and its staging buffer was char[sizeof(long)], so on Windows it rotated within 32 bits and
	   blocked on four bytes, and on an LP64 platform it would rotate within 64 and block on eight -
	   computing a different CRC from the same input.  Byte-at-a-time and bulk stay equal to each
	   other either way, so a self-consistency check passes on both and says nothing.

	   The value below is the four-byte-blocking answer, which is what every Windows build has
	   produced since 1996.  If a platform disagrees with it the CRC has changed width underneath
	   somebody, and this fails loudly instead of a .big index or an obfuscated string quietly
	   hashing differently. */
	CRCEngine engine;
	const long crc = engine("Westwood Studios", 16);
	CHECK_EQ((unsigned long)(unsigned int)crc, 0x93b0f838UL);
}

//////////////////////////////////////////////////////////////////////////////
// Base64
//////////////////////////////////////////////////////////////////////////////

TEST(base64_known_vectors)
{
	/* RFC 4648 section 10. */
	static const char *plain[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
	static const char *coded[] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };

	for (int i = 0; i < 7; ++i)
	{
		char out[32];
		memset(out, 0, sizeof(out));
		int n = Base64_Encode(plain[i], (int)strlen(plain[i]), out, sizeof(out));
		CHECK_EQ(n, (int)strlen(coded[i]));
		out[n] = 0;
		CHECK_STR(out, coded[i]);
	}
}

TEST(base64_round_trip_every_length)
{
	unsigned char src[300];
	char enc[512];
	unsigned char dec[300];
	fill_pattern(src, sizeof(src), 7);

	/* Every length mod 3 matters - that is what decides the padding. */
	for (int len = 0; len <= 64; ++len)
	{
		int e = Base64_Encode(src, len, enc, sizeof(enc));
		CHECK(e >= 0);
		int d = Base64_Decode(enc, e, dec, sizeof(dec));
		CHECK_EQ(d, len);
		if (len > 0)
			CHECK_MEM(src, dec, len);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Hashes
//////////////////////////////////////////////////////////////////////////////

TEST(sha1_known_vectors)
{
	struct { const char *in; const char *out; } cases[] = {
		{ "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
		{ "abc", "a9993e364706816aba3e25717850c26c9cd0d89d" },
		{ "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		  "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
	};

	for (int i = 0; i < 3; ++i)
	{
		SHAEngine sha;
		sha.Hash(cases[i].in, (long)strlen(cases[i].in));

		unsigned char digest[20];
		CHECK_EQ(sha.Result(digest), 20);

		char hex[41];
		to_hex(digest, 20, hex);
		CHECK_STR(hex, cases[i].out);
	}
}

TEST(sha1_incremental_matches_single_shot)
{
	unsigned char data[257];
	fill_pattern(data, sizeof(data), 11);

	SHAEngine whole;
	whole.Hash(data, sizeof(data));
	unsigned char a[20];
	whole.Result(a);

	/* Split across the 64-byte block boundary in every position that matters. */
	static const int splits[] = { 1, 63, 64, 65, 127, 128, 200 };
	for (int i = 0; i < 7; ++i)
	{
		SHAEngine part;
		part.Hash(data, splits[i]);
		part.Hash(data + splits[i], (long)sizeof(data) - splits[i]);
		unsigned char b[20];
		part.Result(b);
		CHECK_MEM(a, b, 20);
	}
}

TEST(sha1_result_is_non_destructive)
{
	/* Result() is documented as "as if the source data were to stop now", so
	   asking twice, or continuing to hash afterwards, has to keep working. */
	SHAEngine sha;
	sha.Hash("abc", 3);

	unsigned char first[20], second[20];
	sha.Result(first);
	sha.Result(second);
	CHECK_MEM(first, second, 20);

	sha.Hash("def", 3);
	unsigned char after[20];
	sha.Result(after);

	SHAEngine reference;
	reference.Hash("abcdef", 6);
	unsigned char expect[20];
	reference.Result(expect);
	CHECK_MEM(after, expect, 20);
}

/* FIPS 180-4's vectors, the million 'a' one fed in uneven pieces so the buffered path is crossed. */
TEST(sha256_known_vectors)
{
	struct { const char *in; const char *out; } cases[] = {
		{ "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
		{ "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
		{ "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
		  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
		{ "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
		  "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1" },
	};
	for (int i = 0; i < 4; ++i)
	{
		Sha256 sha;
		sha.Update(cases[i].in, strlen(cases[i].in));
		unsigned char digest[32];
		sha.Final(digest);
		char hex[65];
		Sha256::Hex(digest, hex);
		CHECK_STR(hex, cases[i].out);
	}

	static char block[1024];		// a piece is at most 1003
	memset(block, 'a', sizeof(block));
	Sha256 million;
	size_t fed = 0;
	for (size_t piece = 1; fed < 1000000; piece = piece % 997 + 7)
	{
		const size_t take = piece < 1000000 - fed ? piece : 1000000 - fed;
		million.Update(block, take);
		fed += take;
	}
	unsigned char digest[32];
	million.Final(digest);
	char hex[65];
	Sha256::Hex(digest, hex);
	CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(md5_known_vectors)
{
	struct { const char *in; const char *out; } cases[] = {
		{ "", "d41d8cd98f00b204e9800998ecf8427e" },
		{ "a", "0cc175b9c0f1b6a831c399e269772661" },
		{ "abc", "900150983cd24fb0d6963f7d28e17f72" },
		{ "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
		{ "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
	};

	for (int i = 0; i < 5; ++i)
	{
		MD5_CTX ctx;
		MD5Init(&ctx);
		MD5Update(&ctx, (unsigned char *)cases[i].in, (unsigned int)strlen(cases[i].in));

		unsigned char digest[16];
		MD5Final(digest, &ctx);

		char hex[33];
		to_hex(digest, 16, hex);
		CHECK_STR(hex, cases[i].out);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Ciphers
//////////////////////////////////////////////////////////////////////////////

TEST(rc4_known_vector)
{
	/* The classic "Key"/"Plaintext" vector. */
	unsigned char buffer[9];
	memcpy(buffer, "Plaintext", 9);

	RC4Class rc4;
	rc4.Prepare_Key((const unsigned char *)"Key", 3);
	rc4.RC4(buffer, 9);

	char hex[19];
	to_hex(buffer, 9, hex);
	CHECK_STR(hex, "bbf316e8d940af0ad3");
}

TEST(rc4_is_its_own_inverse)
{
	unsigned char data[128], copy[128];
	fill_pattern(data, sizeof(data), 3);
	memcpy(copy, data, sizeof(data));

	RC4Class enc;
	enc.Prepare_Key((const unsigned char *)"0123456789abcdef", 16);
	enc.RC4(data, sizeof(data));
	CHECK(memcmp(data, copy, sizeof(data)) != 0);

	RC4Class dec;
	dec.Prepare_Key((const unsigned char *)"0123456789abcdef", 16);
	dec.RC4(data, sizeof(data));
	CHECK_MEM(data, copy, sizeof(data));
}

TEST(rc4_key_length_shortcuts_agree)
{
	/* Prepare_Key has hand-rolled 8- and 16-byte fast paths; they must produce
	   the same key stream as the general loop would. */
	static const unsigned char key8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	unsigned char a[64], b[64];
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));

	RC4Class fast;
	fast.Prepare_Key(key8, 8);
	fast.RC4(a, sizeof(a));

	/* Copying the prepared state must give an identical stream. */
	RC4Class other;
	other.Prepare_Key(key8, 8);
	RC4Class copy;
	copy = other;
	copy.RC4(b, sizeof(b));

	CHECK_MEM(a, b, sizeof(a));
}

TEST(blowfish_round_trip)
{
	unsigned char plain[64], cypher[64], back[64];
	fill_pattern(plain, sizeof(plain), 5);

	BlowfishEngine bf;
	bf.Submit_Key("a moderately long key", 21);

	int n = bf.Encrypt(plain, sizeof(plain), cypher);
	CHECK_EQ(n, (int)sizeof(plain));
	CHECK(memcmp(plain, cypher, sizeof(plain)) != 0);

	int m = bf.Decrypt(cypher, n, back);
	CHECK_EQ(m, (int)sizeof(plain));
	CHECK_MEM(plain, back, sizeof(plain));
}

TEST(blowfish_key_matters)
{
	unsigned char plain[16], c1[16], c2[16];
	memset(plain, 0x5a, sizeof(plain));

	BlowfishEngine a;
	a.Submit_Key("key one", 7);
	a.Encrypt(plain, sizeof(plain), c1);

	BlowfishEngine b;
	b.Submit_Key("key two", 7);
	b.Encrypt(plain, sizeof(plain), c2);

	CHECK(memcmp(c1, c2, sizeof(c1)) != 0);

	/* Decrypting with the wrong key must not accidentally work. */
	unsigned char wrong[16];
	b.Decrypt(c1, sizeof(c1), wrong);
	CHECK(memcmp(wrong, plain, sizeof(plain)) != 0);
}

//////////////////////////////////////////////////////////////////////////////
// LCW / RLE
//////////////////////////////////////////////////////////////////////////////

TEST(lcw_round_trip)
{
	/* Worst case LCW expands slightly, so the destination is oversized. */
	unsigned char src[4096], packed[8192], back[4096];

	static const unsigned seeds[] = { 1, 2, 3 };
	for (int s = 0; s < 3; ++s)
	{
		fill_pattern(src, sizeof(src), seeds[s]);

		int packed_len = LCW_Comp(src, packed, sizeof(src));
		CHECK(packed_len > 0);

		int out_len = LCW_Uncomp(packed, back, sizeof(back));
		CHECK_EQ(out_len, (int)sizeof(src));
		CHECK_MEM(src, back, sizeof(src));
	}
}

// A long run (0xFE) that ends the output exactly.  From an aligned start the run writes 4 bytes to reach
// alignment and then its aligned part a word at a time; 16 bytes leave 12 for the words, which is 4 mod 8,
// where the words used to go in pairs and the last pair wrote 4 bytes past the run - here past the output,
// into the canaries.  No sanitizer needed: the canaries are the check.
TEST(lcw_long_run_stays_inside_the_output)
{
	static const unsigned char stream[] = { 0xFE, 16, 0, 0x5A, 0x80 };
	alignas(16) unsigned char out[32];
	memset(out, 0xC3, sizeof(out));
	int out_len = LCW_Uncomp(stream, out, 16);
	CHECK_EQ(out_len, 16);
	for (int i = 0; i < 16; ++i) CHECK_EQ((int)out[i], 0x5A);
	for (int i = 16; i < 32; ++i) CHECK_EQ((int)out[i], 0xC3);
}

TEST(lcw_compresses_repetitive_data)
{
	unsigned char src[4096], packed[8192], back[4096];
	memset(src, 0xab, sizeof(src));

	int packed_len = LCW_Comp(src, packed, sizeof(src));
	/* A 4K run of one byte has to come out dramatically smaller, otherwise
	   the run encoder is not running at all. */
	CHECK(packed_len < (int)sizeof(src) / 8);

	CHECK_EQ(LCW_Uncomp(packed, back, sizeof(back)), (int)sizeof(src));
	CHECK_MEM(src, back, sizeof(src));
}

TEST(lcw_short_and_empty_input)
{
	unsigned char packed[256], back[256];

	/* len 0 and 1 never reach the assembler - it assumes at least two bytes
	   of input and used to run off the end of a one-byte buffer. */
	for (int len = 0; len <= 32; ++len)
	{
		unsigned char src[32];
		fill_pattern(src, len, 13);

		int packed_len = LCW_Comp(src, packed, len);
		CHECK(packed_len > 0);
		CHECK_EQ(LCW_Uncomp(packed, back, sizeof(back)), len);
		if (len > 0)
			CHECK_MEM(src, back, len);
	}
}

TEST(rle_round_trip)
{
	RLEEngine rle;
	unsigned char src[2048], packed[4096], back[2048];
	fill_pattern(src, sizeof(src), 17);

	int packed_len = rle.Compress(src, packed, sizeof(src));
	CHECK(packed_len > 0);

	int out_len = rle.Decompress(packed, back, packed_len);
	CHECK_EQ(out_len, (int)sizeof(src));
	CHECK_MEM(src, back, sizeof(src));
}

TEST(rle_zero_runs_shrink)
{
	/* RLEEngine only encodes runs of zero - that is its documented scope. */
	RLEEngine rle;
	unsigned char src[2048], packed[4096], back[2048];
	memset(src, 0, sizeof(src));

	int packed_len = rle.Compress(src, packed, sizeof(src));
	CHECK(packed_len < (int)sizeof(src) / 4);
	CHECK_EQ(rle.Decompress(packed, back, packed_len), (int)sizeof(src));
	CHECK_MEM(src, back, sizeof(src));

	/* Data with no zeros at all must survive unharmed even though it cannot
	   be compressed. */
	memset(src, 0xff, sizeof(src));
	packed_len = rle.Compress(src, packed, sizeof(src));
	CHECK_EQ(rle.Decompress(packed, back, packed_len), (int)sizeof(src));
	CHECK_MEM(src, back, sizeof(src));
}

//////////////////////////////////////////////////////////////////////////////
// Random number generators
//////////////////////////////////////////////////////////////////////////////

TEST(random_is_deterministic_per_seed)
{
	RandomClass a(12345), b(12345), c(54321);

	bool differed = false;
	for (int i = 0; i < 64; ++i)
	{
		int va = a();
		CHECK_EQ(va, b());
		if (va != c())
			differed = true;
	}
	/* A different seed must actually produce a different sequence. */
	CHECK(differed);
}

TEST(random_range_is_inclusive_and_covers)
{
	RandomClass rng(999);

	bool seen[7];
	memset(seen, 0, sizeof(seen));

	for (int i = 0; i < 4000; ++i)
	{
		int v = rng(-3, 3);
		CHECK(v >= -3 && v <= 3);
		seen[v + 3] = true;
	}
	for (int i = 0; i < 7; ++i)
		CHECK(seen[i]);

	/* Degenerate range must not spin or overflow. */
	for (int i = 0; i < 16; ++i)
		CHECK_EQ(rng(5, 5), 5);

	/* Reversed bounds are swapped internally rather than rejected. */
	for (int i = 0; i < 64; ++i)
	{
		int v = rng(9, 2);
		CHECK(v >= 2 && v <= 9);
	}
}

TEST(random2_3_4_are_deterministic_and_ranged)
{
	Random2Class r2a(7), r2b(7);
	Random3Class r3a(7, 11), r3b(7, 11);
	Random4Class r4a(7), r4b(7);

	for (int i = 0; i < 64; ++i)
	{
		CHECK_EQ(r2a(), r2b());
		CHECK_EQ(r3a(), r3b());
		CHECK_EQ(r4a(), r4b());

		int v2 = r2a(0, 10);  CHECK(v2 >= 0 && v2 <= 10);
		int v3 = r3a(0, 10);  CHECK(v3 >= 0 && v3 <= 10);
		int v4 = r4a(0, 10);  CHECK(v4 >= 0 && v4 <= 10);
		r2b(0, 10); r3b(0, 10); r4b(0, 10);
	}
}

TEST(pick_random_number_helper)
{
	RandomClass rng(4242);
	for (int i = 0; i < 256; ++i)
	{
		int v = Pick_Random_Number(rng, 100, 200);
		CHECK(v >= 100 && v <= 200);
	}
}

//////////////////////////////////////////////////////////////////////////////
// fixed point
//////////////////////////////////////////////////////////////////////////////

TEST(fixed_construction_and_conversion)
{
	fixed half(1, 2);
	fixed one(1);
	fixed three_quarters(3, 4);

	CHECK(half == fixed::_1_2);
	CHECK(three_quarters == fixed::_3_4);
	CHECK(one > half);
	CHECK(half < one);

	/* operator unsigned rounds to nearest, it does not truncate. */
	CHECK_EQ((unsigned)half, 1u);
	CHECK_EQ((unsigned)fixed(1, 4), 0u);
	CHECK_EQ((unsigned)fixed(3, 4), 1u);
	CHECK_EQ((unsigned)fixed(200), 200u);
}

TEST(fixed_arithmetic)
{
	fixed a(1, 2);
	fixed b(1, 4);

	CHECK(a + b == fixed(3, 4));
	CHECK(a - b == fixed(1, 4));
	CHECK(a * b == fixed(1, 8));
	CHECK(a / b == fixed(2));

	/* Mixed integer operators return integers, rounded. */
	CHECK_EQ(fixed(1, 2) * 10, 5);
	CHECK_EQ(10 * fixed(1, 2), 5);
	CHECK_EQ(fixed(3, 4) * 10, 8);   /* 7.5 rounds up */
}

TEST(fixed_rounding_and_saturation)
{
	fixed v(7, 4);   /* 1.75 */

	CHECK(Round_Down(v) == fixed(1));
	CHECK(Round_Up(v) == fixed(2));
	CHECK(Round(v) == fixed(2));
	CHECK(Round(fixed(5, 4)) == fixed(1));

	CHECK(Saturate(fixed(10), 3u) == fixed(3));
	CHECK(Saturate(fixed(2), 3u) == fixed(2));
	CHECK(Sub_Saturate(fixed(10), 3u) < fixed(3));
}

TEST(fixed_ascii_round_trip)
{
	fixed from_text("0.5");
	CHECK(from_text == fixed::_1_2);

	fixed whole("3");
	CHECK(whole == fixed(3));

	char buffer[32];
	fixed(1, 2).To_ASCII(buffer, sizeof(buffer));
	/* Whatever formatting it picks, it has to parse back to the same value. */
	CHECK(fixed(buffer) == fixed(1, 2));

	CHECK(fixed(fixed(3, 4).As_ASCII()) == fixed(3, 4));
}

//////////////////////////////////////////////////////////////////////////////
// Strings
//////////////////////////////////////////////////////////////////////////////

TEST(stringclass_basics)
{
	StringClass s("hello");
	CHECK_EQ(s.Get_Length(), 5);
	CHECK(s == "hello");
	CHECK(s != "Hello");
	CHECK(!s.Is_Empty());

	StringClass empty;
	CHECK(empty.Is_Empty());
	CHECK_EQ(empty.Get_Length(), 0);

	s += " world";
	CHECK(s == "hello world");
	CHECK_EQ(s.Get_Length(), 11);

	CHECK_EQ(s[0], 'h');
	s[0] = 'H';
	CHECK(s == "Hello world");
}

TEST(stringclass_compare_and_concat)
{
	StringClass a("abc");
	StringClass b("abd");

	CHECK(a.Compare("abc") == 0);
	CHECK(a.Compare_No_Case("ABC") == 0);
	CHECK(a < "abd");
	CHECK(b > "abc");
	CHECK(a <= "abc");
	CHECK(a >= "abc");

	StringClass joined = a + b;
	CHECK(joined == "abcabd");
	CHECK(("x" + a) == "xabc");
	CHECK((a + "y") == "abcy");
}

TEST(stringclass_format_and_erase)
{
	StringClass s;
	s.Format("%s has %d %s", "player", 3, "units");
	CHECK(s == "player has 3 units");

	s.Erase(0, 7);
	CHECK(s == "has 3 units");

	/* Growing well past the small-buffer threshold has to keep working. */
	StringClass big;
	big.Format("%0512d", 1);
	CHECK_EQ(big.Get_Length(), 512);
}

TEST(stringclass_trim)
{
	StringClass s("   padded   ");
	s.Trim();
	CHECK(s == "padded");

	StringClass onlyspace("      ");
	onlyspace.Trim();
	CHECK(onlyspace.Is_Empty());
}

TEST(stringclass_long_strings_survive_reassignment)
{
	/* Free_String decides between the temp-string pool and the heap by
	   comparing addresses; getting that wrong corrupts the pool.  Churn
	   through both size regimes to make sure it holds up. */
	StringClass s;
	for (int i = 0; i < 200; ++i)
	{
		StringClass tmp;
		tmp.Format("%d", i);
		s = tmp;
		CHECK_EQ(atoi(s), i);

		StringClass longer;
		longer.Format("%0300d", i);
		s = longer;
		CHECK_EQ(s.Get_Length(), 300);
	}
}

/* These two test WideStringClass's wide half, which is _WIN32-only by B1's agreed decision: its only
   implementations are Win32 calls, and off Windows it is guarded out of wwstring.h and widestring.h
   rather than given a second UTF-16 conversion beside the engine's own.  They come back when B1's
   char16_t work gives that half a body everywhere.  Guarded, not deleted, so they are still here. */
#if defined(_WIN32)
TEST(widestring_basics)
{
	WideStringClass w(L"wide");
	CHECK_EQ(w.Get_Length(), 4);
	CHECK(w == L"wide");
	CHECK(w != L"WIDE");

	w += L" text";
	CHECK(w == L"wide text");
	CHECK_EQ(w.Get_Length(), 9);

	/* Round trip through the narrow representation. */
	WideStringClass from_narrow("ascii");
	CHECK(from_narrow == L"ascii");

	StringClass narrow;
	narrow = from_narrow;
	CHECK(narrow == "ascii");
}

TEST(widestring_compare_and_format)
{
	WideStringClass a(L"abc");
	CHECK(a.Compare(L"abc") == 0);
	CHECK(a.Compare_No_Case(L"ABC") == 0);
	CHECK(a < L"abd");

	WideStringClass f;
	f.Format(L"%d-%d", 4, 5);
	CHECK(f == L"4-5");
}
#endif	// _WIN32: WideStringClass's wide half, B1

/* _strlwr/_strupr turn asset and definition names into hash keys, so both builds must produce the
   same key.  On Windows this runs MSVC's own; elsewhere, MSVCCompat.h's.  The expected strings are
   written out by hand: ASCII letters convert, digits and punctuation do not, and 0xC9 - a Latin-1
   E-acute, which a locale-aware conversion would change - is left alone, because nothing here ever
   leaves the "C" locale.  Both return their argument, which font3d.cpp relies on. */
TEST(strlwr_and_strupr_are_ascii_only_and_in_place)
{
	char lower[] = "Tank_\xC9" "Crew.W3D";
	CHECK(_strlwr(lower) == lower);
	CHECK_STR(lower, "tank_\xC9" "crew.w3d");

	char upper[] = "Tank_\xE9" "crew.w3d";
	CHECK(_strupr(upper) == upper);
	CHECK_STR(upper, "TANK_\xE9" "CREW.W3D");

	char plain[] = "MiXeD9";
	CHECK_STR(strlwr(plain), "mixed9");
	CHECK_STR(strupr(plain), "MIXED9");
}

TEST(strtrim_in_place)
{
	char buffer[64];

	strcpy(buffer, "  leading and trailing  ");
	CHECK_STR(strtrim(buffer), "leading and trailing");

	strcpy(buffer, "notrim");
	CHECK_STR(strtrim(buffer), "notrim");

	strcpy(buffer, "     ");
	CHECK_STR(strtrim(buffer), "");

	strcpy(buffer, "");
	CHECK_STR(strtrim(buffer), "");

	strcpy(buffer, "\t\ntabs\r\n");
	CHECK_STR(strtrim(buffer), "tabs");
}

TEST(nstrdup_copies)
{
	char *copy = nstrdup("duplicated");
	CHECK_STR(copy, "duplicated");
	delete[] copy;

	CHECK(nstrdup(0) == 0);
}

TEST(strtok_r_is_reentrant)
{
	char outer[] = "a,b,c";
	char inner[] = "1-2-3";
	char *outer_state = 0;
	char *inner_state = 0;

	const char *expect_outer[] = { "a", "b", "c" };
	const char *expect_inner[] = { "1", "2", "3" };

	char *o = strtok_r(outer, ",", &outer_state);
	for (int i = 0; i < 3; ++i)
	{
		CHECK_STR(o, expect_outer[i]);

		/* Interleave a second tokenisation - the whole point of the _r form. */
		char scratch[16];
		strcpy(scratch, inner);
		char *n = strtok_r(scratch, "-", &inner_state);
		for (int j = 0; j < 3; ++j)
		{
			CHECK_STR(n, expect_inner[j]);
			n = strtok_r(0, "-", &inner_state);
		}
		CHECK(n == 0);

		o = strtok_r(0, ",", &outer_state);
	}
	CHECK(o == 0);
}

//////////////////////////////////////////////////////////////////////////////
// Small numeric helpers
//////////////////////////////////////////////////////////////////////////////

TEST(gcd_and_lcm)
{
	CHECK_EQ(Greatest_Common_Divisor(12, 18), 6u);
	CHECK_EQ(Greatest_Common_Divisor(18, 12), 6u);
	CHECK_EQ(Greatest_Common_Divisor(17, 5), 1u);
	CHECK_EQ(Greatest_Common_Divisor(9, 9), 9u);

	CHECK_EQ(Least_Common_Multiple(4, 6), 12u);
	CHECK_EQ(Least_Common_Multiple(21, 6), 42u);
	CHECK_EQ(Least_Common_Multiple(7, 7), 7u);
}

//////////////////////////////////////////////////////////////////////////////
// Containers
//////////////////////////////////////////////////////////////////////////////

TEST(vectorclass_resize_and_id)
{
	VectorClass<int> v(4);
	CHECK_EQ(v.Length(), 4);

	for (int i = 0; i < 4; ++i)
		v[i] = i * 10;

	CHECK_EQ(v.ID(20), 2);
	CHECK_EQ(v.ID(99), -1);
	CHECK_EQ(v.ID(&v[3]), 3);

	/* Resize must preserve the surviving prefix. */
	CHECK(v.Resize(8));
	CHECK_EQ(v.Length(), 8);
	for (int i = 0; i < 4; ++i)
		CHECK_EQ(v[i], i * 10);

	v.Clear();
	CHECK_EQ(v.Length(), 0);
}

TEST(dynamicvector_add_insert_delete)
{
	DynamicVectorClass<int> v;
	CHECK_EQ(v.Count(), 0);

	for (int i = 0; i < 10; ++i)
		CHECK(v.Add(i));
	CHECK_EQ(v.Count(), 10);
	for (int i = 0; i < 10; ++i)
		CHECK_EQ(v[i], i);

	CHECK(v.Add_Head(-1));
	CHECK_EQ(v[0], -1);
	CHECK_EQ(v.Count(), 11);

	CHECK(v.Insert(5, 555));
	CHECK_EQ(v[5], 555);
	CHECK_EQ(v.Count(), 12);

	CHECK(v.Delete_Index(5));
	CHECK_EQ(v.Count(), 11);
	CHECK_EQ(v.ID(555), -1);

	CHECK(v.Delete_Index(0));      /* drops the -1 added at the head */
	CHECK_EQ(v[0], 0);
	CHECK_EQ(v.Count(), 10);

	v.Delete_All();
	CHECK_EQ(v.Count(), 0);
}

TEST(dynamicvector_delete_by_value)
{
	/* Delete(T const &) and Delete(int) are ambiguous for an int vector, so
	   the by-value overload gets exercised on a type that disambiguates. */
	DynamicVectorClass<float> v;
	for (int i = 0; i < 5; ++i)
		v.Add(float(i) * 1.5f);

	CHECK(v.Delete(3.0f));
	CHECK_EQ(v.Count(), 4);
	CHECK_EQ(v.ID(3.0f), -1);

	CHECK(!v.Delete(99.0f));
	CHECK_EQ(v.Count(), 4);
}

TEST(dynamicvector_grows_past_initial_capacity)
{
	DynamicVectorClass<int> v(2);
	v.Set_Growth_Step(3);

	for (int i = 0; i < 100; ++i)
		CHECK(v.Add(i * 3));

	CHECK_EQ(v.Count(), 100);
	for (int i = 0; i < 100; ++i)
		CHECK_EQ(v[i], i * 3);
}

TEST(simplevec_basics)
{
	SimpleVecClass<float> v(4);
	CHECK_EQ(v.Length(), 4);

	for (int i = 0; i < 4; ++i)
		v[i] = float(i);

	v.Resize(8);
	CHECK_EQ(v.Length(), 8);
	for (int i = 0; i < 4; ++i)
		CHECK_NEAR(v[i], float(i), 1e-6f);

	v.Zero_Memory();
	for (int i = 0; i < 8; ++i)
		CHECK_NEAR(v[i], 0.0f, 1e-6f);
}

/* A copy gets its own buffer. The compiler's copy shared the pointer, so the order lines' pool
   (copies of one SegmentedLineClass) freed its source's points twice: v4.0.0's commonest crash. */
TEST(simplevec_copy_owns_its_buffer)
{
	SimpleDynVecClass<int> a;
	for (int i = 0; i < 5; ++i)
		a.Add(i);

	SimpleDynVecClass<int> b(a);
	CHECK_EQ(b.Count(), 5);
	CHECK(&b[0] != &a[0]);
	b.Delete_All();		/* shrinks: frees b's buffer, must leave a's */
	b.Add(7);
	CHECK_EQ(a[4], 4);

	SimpleDynVecClass<int> c;
	c = a;
	CHECK_EQ(c.Count(), 5);
	CHECK(&c[0] != &a[0]);
	CHECK_EQ(c[3], 3);
}

namespace
{
	/* Minimal concrete HashableClass - the table stores the key by pointer,
	   so the string has to outlive the entry. */
	class TestEntry : public HashableClass
	{
	public:
		TestEntry(const char *key) : Key(key) {}
		virtual const char *Get_Key(void) { return Key; }
		const char *Key;
	};

	class ListItem : public MultiListObjectClass
	{
	public:
		ListItem(int v) : Value(v) {}
		int Value;
	};
}

TEST(hashtable_add_find_remove)
{
	HashTableClass table(32);

	TestEntry a("alpha"), b("beta"), c("gamma");
	table.Add(&a);
	table.Add(&b);
	table.Add(&c);

	CHECK(table.Find("alpha") == &a);
	CHECK(table.Find("beta") == &b);
	CHECK(table.Find("gamma") == &c);
	CHECK(table.Find("delta") == 0);

	CHECK(table.Remove(&b));
	CHECK(table.Find("beta") == 0);
	CHECK(table.Find("alpha") == &a);

	table.Reset();
	CHECK(table.Find("alpha") == 0);
}

TEST(hashtable_survives_collisions)
{
	/* A tiny table forces every key into a shared bucket chain. */
	HashTableClass table(2);

	const int COUNT = 64;
	char keys[COUNT][16];
	TestEntry *entries[COUNT];

	for (int i = 0; i < COUNT; ++i)
	{
		sprintf(keys[i], "key%d", i);
		entries[i] = new TestEntry(keys[i]);
		table.Add(entries[i]);
	}

	for (int i = 0; i < COUNT; ++i)
		CHECK(table.Find(keys[i]) == entries[i]);

	/* Removing from the middle of a chain must not lose the rest of it. */
	for (int i = 0; i < COUNT; i += 2)
		CHECK(table.Remove(entries[i]));
	for (int i = 1; i < COUNT; i += 2)
		CHECK(table.Find(keys[i]) == entries[i]);

	table.Reset();
	for (int i = 0; i < COUNT; ++i)
		delete entries[i];
}

TEST(hashtable_iterator_visits_everything)
{
	HashTableClass table(8);

	const int COUNT = 20;
	char keys[COUNT][16];
	TestEntry *entries[COUNT];
	for (int i = 0; i < COUNT; ++i)
	{
		sprintf(keys[i], "k%d", i);
		entries[i] = new TestEntry(keys[i]);
		table.Add(entries[i]);
	}

	bool seen[COUNT];
	memset(seen, 0, sizeof(seen));

	HashTableIteratorClass it(table);
	int visited = 0;
	for (it.First(); !it.Is_Done(); it.Next())
	{
		HashableClass *entry = it.Get_Current();
		for (int i = 0; i < COUNT; ++i)
		{
			if (entry == entries[i])
			{
				CHECK(!seen[i]);
				seen[i] = true;
			}
		}
		++visited;
	}
	CHECK_EQ(visited, COUNT);

	table.Reset();
	for (int i = 0; i < COUNT; ++i)
		delete entries[i];
}

TEST(multilist_add_remove_and_membership)
{
	MultiListClass<ListItem> list;
	CHECK(list.Is_Empty());

	ListItem a(1), b(2), c(3);
	CHECK(list.Add(&a));
	CHECK(list.Add(&b));
	CHECK(list.Add(&c));
	CHECK(!list.Is_Empty());

	CHECK(list.Contains(&a));
	CHECK(list.Contains(&c));

	/* onlyonce defaults to true, so a repeat add is refused. */
	CHECK(!list.Add(&a));

	CHECK(list.Remove(&b));
	CHECK(!list.Contains(&b));
	CHECK(!list.Remove(&b));

	int count = 0;
	MultiListIterator<ListItem> it(&list);
	for (it.First(); !it.Is_Done(); it.Next())
		++count;
	CHECK_EQ(count, 2);

	/* Destroying a member has to unlink it - that is what the object base
	   class exists for.  Empty the list before it goes out of scope, since
	   GenericMultiListClass asserts on a non-empty destructor. */
	list.Remove(&a);
	list.Remove(&c);
	CHECK(list.Is_Empty());
}

TEST(multilist_object_unlinks_itself_on_destruction)
{
	MultiListClass<ListItem> list;
	{
		ListItem transient(9);
		CHECK(list.Add(&transient));
		CHECK(!list.Is_Empty());
	}
	/* transient is gone; MultiListObjectClass::~ must have pulled it out. */
	CHECK(list.Is_Empty());
}

//////////////////////////////////////////////////////////////////////////////
// RAM files and chunk IO
//////////////////////////////////////////////////////////////////////////////

TEST(ramfile_read_write_seek)
{
	char storage[256];
	memset(storage, 0, sizeof(storage));

	RAMFileClass file(storage, sizeof(storage));
	CHECK(file.Open(FileClass::WRITE) != 0);

	const char *payload = "chunk of bytes";
	int len = (int)strlen(payload);
	CHECK_EQ(file.Write(payload, len), len);
	CHECK_EQ(file.Size(), len);
	file.Close();

	CHECK(file.Open(FileClass::READ) != 0);
	char back[64];
	memset(back, 0, sizeof(back));
	CHECK_EQ(file.Read(back, len), len);
	CHECK_STR(back, payload);

	/* Seek back to the start and re-read. */
	CHECK_EQ(file.Seek(0, SEEK_SET), 0);
	memset(back, 0, sizeof(back));
	CHECK_EQ(file.Read(back, 5), 5);
	CHECK(!memcmp(back, "chunk", 5));

	/* Reading past the end returns a short count rather than running off. */
	CHECK_EQ(file.Seek(0, SEEK_END), len);
	CHECK_EQ(file.Read(back, 32), 0);
	file.Close();
}

//////////////////////////////////////////////////////////////////////////////
// RawFileClass, against the disk
//
// These go through the real file system, on purpose: RawFileClass's platform
// arms are the thing under test.  They were written for B5's POSIX port, whose
// predecessor - an _UNIX arm on stdio - truncated a file opened READ|WRITE,
// answered 0 from every seek and returned Unix time where callers keep DOS
// time.  What they do NOT establish: share modes (POSIX has none), behaviour
// past 2GB, or anything about a path with a backslash in it (C1's).
//////////////////////////////////////////////////////////////////////////////

static const char *RAWFILE_TEST_NAME = "test_wwlib_rawfile.tmp";

static void rawfile_write_fresh(const char *text)
{
	RawFileClass file(RAWFILE_TEST_NAME);
	CHECK(file.Open(FileClass::WRITE) != 0);
	CHECK_EQ(file.Write(text, (int)strlen(text)), (int)strlen(text));
	file.Close();
}

/* The file's last-write time, set through the OS rather than through the class
   under test, so the check below has an answer that does not come from
   RawFileClass. */
static bool rawfile_set_mtime_utc(const char *path, long long unix_seconds)
{
#if defined(_WIN32)
	HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	/* 100ns ticks since 1601-01-01, which is 11644473600 seconds before 1970. */
	unsigned long long ticks = ((unsigned long long)unix_seconds + 11644473600ULL) * 10000000ULL;
	FILETIME ft;
	ft.dwLowDateTime = (DWORD)ticks;
	ft.dwHighDateTime = (DWORD)(ticks >> 32);
	BOOL ok = SetFileTime(h, NULL, &ft, &ft);
	CloseHandle(h);
	return ok != 0;
#else
	struct timeval tv[2];
	tv[0].tv_sec = tv[1].tv_sec = (time_t)unix_seconds;
	tv[0].tv_usec = tv[1].tv_usec = 0;
	return utimes(path, tv) == 0;
#endif
}

TEST(rawfile_read_write_open_keeps_what_is_there)
{
	rawfile_write_fresh("hello world");

	/* SKB's OPEN_ALWAYS: opening READ|WRITE must not destroy the contents. */
	RawFileClass file(RAWFILE_TEST_NAME);
	CHECK(file.Open(FileClass::READ | FileClass::WRITE) != 0);
	file.Close();
	CHECK_EQ(file.Size(), 11);

	char back[32];
	memset(back, 0, sizeof(back));
	CHECK(file.Open(FileClass::READ) != 0);
	CHECK_EQ(file.Read(back, sizeof(back)), 11);	/* a short count at end of file */
	CHECK_STR(back, "hello world");
	file.Close();

	/* WRITE is CREATE_ALWAYS, and does truncate.  A fresh object to ask: Size()
	   leaves its answer in BiasLength, so the one above would say 11 forever -
	   on every platform, since that part is shared code. */
	rawfile_write_fresh("bye");
	RawFileClass again(RAWFILE_TEST_NAME);
	CHECK_EQ(again.Size(), 3);
	CHECK(again.Delete() != 0);
	CHECK(!again.Is_Available());
}

TEST(rawfile_seek_answers_the_new_position)
{
	rawfile_write_fresh("0123456789");

	RawFileClass file(RAWFILE_TEST_NAME);
	CHECK(file.Open(FileClass::READ) != 0);
	CHECK_EQ(file.Seek(4, SEEK_SET), 4);
	CHECK_EQ(file.Seek(2, SEEK_CUR), 6);
	char c = 0;
	CHECK_EQ(file.Read(&c, 1), 1);
	CHECK_EQ(c, '6');
	CHECK_EQ(file.Seek(-1, SEEK_END), 9);
	CHECK_EQ(file.Seek(0, SEEK_END), 10);
	file.Close();
	file.Delete();
}

TEST(rawfile_date_time_is_dos_packed_utc)
{
	rawfile_write_fresh("x");

	/* 1000000000 is 2001-09-09 01:46:40 UTC.  DOS date: (2001-1980)<<9 | 9<<5 | 9
	   = 0x2B29; DOS time: 1<<11 | 46<<5 | 40/2 = 0x0DD4.  Worked by hand and by
	   Python's datetime, not by the code under test.  On Windows this is also
	   the check that FileTimeToDosDateTime does not move a UTC FILETIME into
	   local time, which RawFileClass's POSIX arm assumes. */
	CHECK(rawfile_set_mtime_utc(RAWFILE_TEST_NAME, 1000000000LL));

	RawFileClass file(RAWFILE_TEST_NAME);
	CHECK_EQ(file.Get_Date_Time(), 0x2B290DD4UL);	/* closed: opens, reads, closes */
	CHECK(file.Open(FileClass::READ) != 0);
	CHECK_EQ(file.Get_Date_Time(), 0x2B290DD4UL);	/* open */
	file.Close();

	/* And back the other way.  DOS time has two-second resolution, so the value
	   written here is one it can hold exactly. */
	CHECK(file.Open(FileClass::READ | FileClass::WRITE) != 0);
	CHECK(file.Set_Date_Time(0x3A5C8B2EUL));	/* 2009-02-28 17:25:28 */
	file.Close();
	CHECK_EQ(file.Get_Date_Time(), 0x3A5C8B2EUL);

	/* A month of 13 is not a date. */
	CHECK(file.Open(FileClass::READ | FileClass::WRITE) != 0);
	CHECK(!file.Set_Date_Time(((0x3A5CUL & ~(0xFUL << 5)) | (13UL << 5)) << 16));
	file.Close();
	file.Delete();
}

//////////////////////////////////////////////////////////////////////////////
// Mix files
//
// A Renegade-era archive that Zero Hour never opens (ThumbnailManagerClass's
// Pre_Init and Add_Thumbnail_Manager have no callers) but still links.  The
// format's fields are 4 bytes; they were `long`, so on LP64 the reader took
// 8-byte fields and misread every mix file, including its own writer's.  The
// expected bytes below were built by hand in Python - struct.pack("<i") and
// zlib.crc32 over the upper-cased name - not by the code under test, so a
// writer and reader that agree with each other at the wrong width still fail.
// What this does not establish: anything about a mix file from a real game
// install, which Zero Hour does not ship.
//////////////////////////////////////////////////////////////////////////////

static const unsigned char MIX_EXPECTED[90] = {
	0x4D, 0x49, 0x58, 0x31, 0x28, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* MIX1, table 40, names 68, unused */
	0x61, 0x6C, 0x70, 0x68, 0x61, 0x00, 0x00, 0x00,													/* "alpha", padded to 8 */
	0x62, 0x72, 0x61, 0x76, 0x6F, 0x21, 0x21, 0x21, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* "bravo!!!!", padded */
	0x02, 0x00, 0x00, 0x00,																			/* two files, sorted by CRC: */
	0x88, 0xB8, 0xAC, 0x8B, 0x18, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,							/* DIR\B.TXT 0x8BACB888 @24, 9 */
	0x34, 0x9A, 0x8D, 0x96, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,							/* A.TXT     0x968D9A34 @16, 5 */
	0x02, 0x00, 0x00, 0x00,																			/* names, same order: */
	0x0A, 0x64, 0x69, 0x72, 0x5C, 0x42, 0x2E, 0x54, 0x58, 0x54, 0x00,								/* "dir\B.TXT" - backslash kept */
	0x06, 0x41, 0x2E, 0x54, 0x58, 0x54, 0x00,														/* "A.TXT" */
};

static void mix_write_source(const char *name, const char *text)
{
	RawFileClass file(name);
	CHECK(file.Open(FileClass::WRITE) != 0);
	file.Write(text, (int)strlen(text));
	file.Close();
}

static bool mix_read_member(MixFileFactoryClass &mix, const char *name, char *out, int size)
{
	memset(out, 0, size);
	FileClass *file = mix.Get_File(name);
	if (file == NULL) return false;
	file->Open();
	int got = file->Read(out, size - 1);
	mix.Return_File(file);
	return got > 0;
}

TEST(mixfile_writes_the_format_byte_for_byte)
{
	mix_write_source("test_wwlib_mix_a.tmp", "alpha");
	mix_write_source("test_wwlib_mix_b.tmp", "bravo!!!!");
	{
		MixFileCreator creator("test_wwlib.mix");
		creator.Add_File("test_wwlib_mix_a.tmp", "A.TXT");
		creator.Add_File("test_wwlib_mix_b.tmp", "dir\\B.TXT");	/* archive-internal: keeps its backslash */
	}	/* the destructor writes the table and patches the header */

	RawFileClass raw("test_wwlib.mix");
	unsigned char bytes[128];
	memset(bytes, 0xCD, sizeof(bytes));
	CHECK(raw.Open(FileClass::READ) != 0);
	CHECK_EQ(raw.Read(bytes, sizeof(bytes)), (int)sizeof(MIX_EXPECTED));
	raw.Close();
	CHECK_MEM(bytes, MIX_EXPECTED, sizeof(MIX_EXPECTED));

	RawFileClass("test_wwlib_mix_a.tmp").Delete();
	RawFileClass("test_wwlib_mix_b.tmp").Delete();
	raw.Delete();
}

TEST(mixfile_reads_and_flushes_through_the_real_file_system)
{
	/* Written from the expected bytes, not by MixFileCreator, so the reader is
	   checked against the format rather than against the writer. */
	{
		RawFileClass out("test_wwlib.mix");
		CHECK(out.Open(FileClass::WRITE) != 0);
		CHECK_EQ(out.Write(MIX_EXPECTED, sizeof(MIX_EXPECTED)), (int)sizeof(MIX_EXPECTED));
		out.Close();
	}

	SimpleFileFactoryClass ff;
	char text[32];
	{
		MixFileFactoryClass mix("test_wwlib.mix", &ff);
		CHECK(mix.Is_Valid());
		DynamicVectorClass<StringClass> names;
		CHECK(mix.Build_Filename_List(names));
		CHECK_EQ(names.Count(), 2);

		/* Member lookup is by CRC of the upper-cased name, so case does not matter. */
		CHECK(mix_read_member(mix, "a.txt", text, sizeof(text)));
		CHECK_STR(text, "alpha");
		CHECK(mix_read_member(mix, "DIR\\b.txt", text, sizeof(text)));
		CHECK_STR(text, "bravo!!!!");

		/* Flush_Changes: the _splitpath, temp-name and delete/rename path.  Delete_File edits the
		   internal list, which only Build_Internal_Filename_List fills; without it the delete
		   matches nothing and the flush is correctly a no-op. */
		CHECK(mix.Build_Internal_Filename_List());
		mix.Delete_File("A.TXT");
		mix.Flush_Changes();
	}
	CHECK(!RawFileClass("_tmpmix01.dat").Is_Available());	/* renamed over the original */

	MixFileFactoryClass after("test_wwlib.mix", &ff);
	CHECK(after.Is_Valid());
	DynamicVectorClass<StringClass> left;
	CHECK(after.Build_Filename_List(left));
	CHECK_EQ(left.Count(), 1);
	CHECK(!mix_read_member(after, "A.TXT", text, sizeof(text)));
	CHECK(mix_read_member(after, "dir\\B.TXT", text, sizeof(text)));
	CHECK_STR(text, "bravo!!!!");

	RawFileClass("test_wwlib.mix").Delete();
}

TEST(chunkio_nested_chunks_round_trip)
{
	char storage[1024];
	memset(storage, 0, sizeof(storage));

	const uint32 OUTER = 0x100;
	const uint32 INNER = 0x101;
	const uint32 SIBLING = 0x102;
	int written = 0;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);

		CHECK(csave.Begin_Chunk(OUTER));
		CHECK_EQ(csave.Cur_Chunk_Depth(), 1);

		CHECK(csave.Begin_Chunk(INNER));
		CHECK_EQ(csave.Cur_Chunk_Depth(), 2);
		int payload = 0x1234;
		CHECK_EQ(csave.Write(&payload, sizeof(payload)), (uint32)sizeof(payload));
		CHECK(csave.End_Chunk());

		CHECK(csave.Begin_Chunk(SIBLING));
		float f = 2.5f;
		CHECK_EQ(csave.Write(&f, sizeof(f)), (uint32)sizeof(f));
		CHECK(csave.End_Chunk());

		CHECK(csave.End_Chunk());
		CHECK_EQ(csave.Cur_Chunk_Depth(), 0);
		written = file.Size();
		file.Close();
	}

	/* Size the reader to what was actually written - a RAMFileClass spanning
	   the whole scratch buffer would hand ChunkLoadClass the trailing zeros as
	   a perfectly valid empty chunk header. */
	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);

		CHECK(cload.Open_Chunk());
		CHECK_EQ(cload.Cur_Chunk_ID(), OUTER);
		CHECK(cload.Contains_Chunks() != 0);

		CHECK(cload.Open_Chunk());
		CHECK_EQ(cload.Cur_Chunk_ID(), INNER);
		int payload = 0;
		CHECK_EQ(cload.Read(&payload, sizeof(payload)), (uint32)sizeof(payload));
		CHECK_EQ(payload, 0x1234);
		CHECK(cload.Close_Chunk());

		CHECK(cload.Open_Chunk());
		CHECK_EQ(cload.Cur_Chunk_ID(), SIBLING);
		float f = 0.0f;
		CHECK_EQ(cload.Read(&f, sizeof(f)), (uint32)sizeof(f));
		CHECK_NEAR(f, 2.5f, 1e-6f);
		CHECK(cload.Close_Chunk());

		CHECK(!cload.Open_Chunk());     /* no more children */
		CHECK(cload.Close_Chunk());
		CHECK(!cload.Open_Chunk());     /* end of file */
	}
}

TEST(chunkio_micro_chunks_round_trip)
{
	char storage[1024];
	int written = 0;
	memset(storage, 0, sizeof(storage));

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);

		csave.Begin_Chunk(0x200);
		for (int i = 0; i < 8; ++i)
		{
			CHECK(csave.Begin_Micro_Chunk(i));
			int value = i * 100;
			csave.Write(&value, sizeof(value));
			CHECK(csave.End_Micro_Chunk());
		}
		csave.End_Chunk();
		written = file.Size();
		file.Close();
	}

	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);

		CHECK(cload.Open_Chunk());
		CHECK_EQ(cload.Cur_Chunk_ID(), (uint32)0x200);

		int seen = 0;
		while (cload.Open_Micro_Chunk())
		{
			int value = 0;
			CHECK_EQ(cload.Cur_Micro_Chunk_ID(), (uint32)seen);
			CHECK_EQ(cload.Cur_Micro_Chunk_Length(), (uint32)sizeof(value));
			cload.Read(&value, sizeof(value));
			CHECK_EQ(value, seen * 100);
			CHECK(cload.Close_Micro_Chunk());
			++seen;
		}
		CHECK_EQ(seen, 8);
		CHECK(cload.Close_Chunk());
	}
}

TEST(chunkio_seek_skips_payload)
{
	char storage[512];
	int written = 0;
	memset(storage, 0, sizeof(storage));

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		csave.Begin_Chunk(0x300);
		unsigned char blob[64];
		fill_pattern(blob, sizeof(blob), 21);
		csave.Write(blob, sizeof(blob));
		csave.End_Chunk();
		written = file.Size();
		file.Close();
	}

	RAMFileClass file(storage, written);
	file.Open(FileClass::READ);
	ChunkLoadClass cload(&file);

	CHECK(cload.Open_Chunk());
	CHECK_EQ(cload.Cur_Chunk_Length(), (uint32)64);
	CHECK_EQ(cload.Seek(64), (uint32)64);
	CHECK(cload.Close_Chunk());
}

//////////////////////////////////////////////////////////////////////////////
// CPU detection
//////////////////////////////////////////////////////////////////////////////

TEST(cpudetect_reports_something_sane)
{
#ifdef CPUDETECT_X86
	/* Anything this port can run on has CPUID and RDTSC. */
	CHECK(CPUDetectClass::Has_CPUID_Instruction());
	CHECK(CPUDetectClass::Has_RDTSC_Instruction());
	CHECK(CPUDetectClass::Has_MMX_Instruction_Set());
	CHECK(CPUDetectClass::Has_SSE_Instruction_Set());

	CHECK(CPUDetectClass::Get_Processor_Speed() > 0);
	CHECK(CPUDetectClass::Get_Processor_Ticks_Per_Second() > 0);
#else
	/* No x86 feature is present, and saying so is the correct answer rather than a missing one:
	   anything that branches on SSE or MMX must take its plain path here. */
	CHECK(!CPUDetectClass::Has_CPUID_Instruction());
	CHECK(!CPUDetectClass::Has_RDTSC_Instruction());
	CHECK(!CPUDetectClass::Has_MMX_Instruction_Set());
	CHECK(!CPUDetectClass::Has_SSE_Instruction_Set());
	CHECK_EQ((int)CPUDetectClass::Get_Processor_Manufacturer(), (int)CPUDetectClass::MANUFACTURER_UNKNOWN);

	/* The open tier decision in cpudetect.h, by name, so changing it is seen here. */
	CHECK_EQ(CPUDetectClass::Get_Processor_Speed(), CPUDETECT_UNMEASURED_PROCESSOR_MHZ);
	CHECK_EQ(CPUDetectClass::Get_Processor_Ticks_Per_Second(), (sint64)0);

	/* Memory and the OS used to be read only inside the CPUID block, which never runs here. */
	CHECK(CPUDetectClass::Get_Available_Physical_Memory() > 0);
#endif
	CHECK(CPUDetectClass::Get_Total_Physical_Memory() > 0);

	const char *name = CPUDetectClass::Get_Processor_Manufacturer_Name();
	CHECK(name != 0 && name[0] != 0);
	CHECK(CPUDetectClass::Get_Processor_String()[0] != 0);
}

TEST(cpudetect_memory_fits_in_the_signed_int_its_callers_use)
{
	/* Init_Memory used to take GlobalMemoryStatus' 32-bit fields straight.  That call saturates,
	   and /LARGEADDRESSAWARE changes what it saturates to - 0xFFFFFFFF instead of 0x7FFFFFFF - so
	   the number arrived as -1 in W3DShaderManager::testMinimumRequirements' Int *numRAM.
	   GameLODManager::init then read a machine with 32GB as a machine below 256MB, and turned off
	   the shell map, the trees and full-size textures.  This binary is linked
	   /LARGEADDRESSAWARE for the same reason generals.exe is, so it sees what the game sees. */
	unsigned totalPhys = CPUDetectClass::Get_Total_Physical_Memory();
	CHECK(totalPhys > 0);
	CHECK((int)totalPhys > 0);
	CHECK((int)CPUDetectClass::Get_Available_Physical_Memory() >= 0);
	CHECK((int)CPUDetectClass::Get_Total_Page_File_Size() > 0);
	CHECK((int)CPUDetectClass::Get_Available_Page_File_Size() >= 0);

	/* The arithmetic GameLODManager::init does with it: >= PROFILE_ERROR_LIMIT, 0.94, means "has
	   at least 256MB", and its result is m_memPassed. */
	float ratio = (float)((int)totalPhys) / (float)(256 * 1024 * 1024);
	CHECK(ratio >= 0.94f);
}

TEST(cpudetect_logs_are_printable)
{
	/* Regression for the os_info bug: Get_OS_Info used to leave the struct
	   uninitialised on every Windows >= Vista, so the log strings were built
	   from dangling pointers.  A printable, non-empty log is the cheap way to
	   assert the defaults are seeded. */
	const StringClass &log = CPUDetectClass::Get_Processor_Log();
	const StringClass &compact = CPUDetectClass::Get_Compact_Log();

	CHECK(!log.Is_Empty());
	CHECK(!compact.Is_Empty());

	const char *p = compact;
	for (int i = 0; p[i]; ++i)
	{
		unsigned char ch = (unsigned char)p[i];
		CHECK(ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 0x20 && ch < 0x7f));
	}

	/* The compact log is tab separated; the OS code is the first field and
	   must never be blank. */
	CHECK(p[0] != '\t');
}

//-------------------------------------------------------------------------------------------------
// Worker names ------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//
// Except.cpp's thread list finds a thread by its name to unregister it, and asserts that the entry
// it finds has that thread's ID; a Debug build aborts there (W3).  Two live threads with one name
// break that, and so does a thread Stop() had to terminate: it never unregisters, so the next thread
// with its name finds the stale entry first.  So every worker below is named once per instance.
static std::string next_thread_name(const char *base)
{
	static std::atomic<int> serial(0);
	return std::string(base) + std::to_string(++serial);
}

//-------------------------------------------------------------------------------------------------
// ThreadClass::Stop() deadlock pattern -----------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//
// Regression for the shutdown stall fixed in WW3D2's TextureLoader::Deinit (textureloader.cpp):
// that function used to take a FastCriticalSectionClass lock and only THEN call Stop() on the
// worker thread, which takes that same lock every time round its loop before checking whether it
// should keep running. If the worker reached its own lock acquire after the main thread grabbed
// it, both sides blocked forever - the worker on the lock, the main thread inside Stop()'s
// wait-for-exit spin - so every quit that caught the worker mid-loop paid the full 3-second
// TerminateThread ceiling. The fix is ordering: call Stop() before taking the lock, so the worker
// is free to finish its own acquire, see running go false, and exit on its own.
//
// TextureLoader itself needs a live D3D device, so this reproduces the same two primitives -
// ThreadClass and FastCriticalSectionClass - standalone. LockLoopWorker plays the loader thread's
// role exactly: grab the lock, hold it briefly, release, repeat.
//
class LockLoopWorker : public ThreadClass
{
public:
	LockLoopWorker(FastCriticalSectionClass &lock) : ThreadClass(next_thread_name("LockLoopWorker").c_str()), m_lock(lock), m_looping(false) {}

	/// Until the worker has taken the lock once, or about a second: true when it has
	bool Wait_Until_Looping()
	{
		for (int i = 0; i < 1000 && !m_looping.load(); ++i)
			ThreadClass::Sleep_Ms(1);
		return m_looping.load();
	}

protected:
	/* One difference from the loader: running is read only while the lock is held.  Read before taking
	   it, as the loader does, the worker can release the lock, be preempted, and read running false
	   after the test thread took the lock and called Stop() - so it exits without ever blocking, and
	   Stop() returns early.  That window failed the deadlock test under ctest -j4 on a four-core Linux
	   host (twice, the second in a CI run).  Read under the lock, a worker
	   can only see running after acquiring the lock, which the test holds through Stop(), so the stall
	   the test pins is certain; unlocked, the worker takes the lock, sees running false and exits. */
	virtual void Thread_Function()
	{
		for (;;)
		{
			FastCriticalSectionClass::LockClass lock(m_lock);
			if (!running)
				break;
			m_looping.store(true);
			for (volatile int i = 0; i < 2000; ++i) {}
		}
	}

private:
	FastCriticalSectionClass &m_lock;
	std::atomic<bool> m_looping;
};

TEST(threadclass_stop_deadlocks_if_the_caller_holds_the_workers_lock)
{
	/* Pins the buggy ordering: take the lock the worker also wants, then call Stop() while still
	   holding it. A short timeout keeps the (expected) deadlock from costing real seconds - what
	   matters is that it reliably runs out the whole timeout instead of returning early, which is
	   exactly what turned into the 3-second quit stall in the real code. */
	FastCriticalSectionClass lock;
	LockLoopWorker worker(lock);
	worker.Execute();
	/* In its loop before the lock is taken here, not after a fixed 20 ms sleep.  This check failed once
	   (elapsed < 250) under ctest -j4 on a four-core machine (L1b, Linux x86_64) and not in 60 runs since;
	   the likely reading is a worker not yet started, which sees running false and exits at once, so
	   Stop() returns early.  Waiting for the loop removes that reading whichever it was. */
	CHECK(worker.Wait_Until_Looping());

	unsigned start = Clock_Milliseconds_Coarse();
	{
		FastCriticalSectionClass::LockClass held(lock);
		worker.Stop(300);
	}
	unsigned elapsed = Clock_Milliseconds_Coarse() - start;

	CHECK(elapsed >= 250);
}

TEST(threadclass_stop_returns_promptly_when_called_unlocked)
{
	/* The fix, in the shape TextureLoader::Deinit now uses: call Stop() before taking the lock the
	   worker wants. The worker finishes whatever it is doing, sees running go false at the top of
	   its loop, and exits - no contention with this thread at all. */
	FastCriticalSectionClass lock;
	LockLoopWorker worker(lock);
	worker.Execute();
	ThreadClass::Sleep_Ms(20);

	unsigned start = Clock_Milliseconds_Coarse();
	worker.Stop(300);
	unsigned elapsed = Clock_Milliseconds_Coarse() - start;

	CHECK(elapsed < 250);
}

//-------------------------------------------------------------------------------------------------
// The guarantees B14's rewrite has to keep ---------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//
// mutex.h's three lock classes and ThreadClass stopped being Win32 primitives and became standard
// library ones.  Each of these pins a property that the Win32 version provided and that a
// plausible "simplification" of the replacement would quietly take away - a non-recursive mutex
// where a recursive one was needed, a poll that blocks, a thread id that is the same for every
// thread.  None of them would fail as a wrong answer; they fail as a hang or as an assert that
// passes from the wrong thread.
//
// Note the std::atomic on every flag these worker classes share with the thread that starts them.
// The first draft of these tests used `volatile bool`, which is exactly the defect B14 had just
// removed from ThreadClass::running - and ThreadSanitizer reported it, in the tests, on the run
// meant to prove the fix.  `volatile` is the reflex; it orders nothing and it is not an atomic.
// Anything shared with a worker here wants std::atomic, including in a test.

TEST(criticalsectionclass_is_recursive)
{
	/* A Win32 CRITICAL_SECTION can be re-acquired by the thread that holds it, and this class
	   was one.  Built against a std::mutex instead, this deadlocks on the second line - not
	   eventually and not on some machines, but here, every time.  That is why the replacement is
	   a std::recursive_mutex, and this test is what says so to whoever wonders why. */
	CriticalSectionClass cs;
	CriticalSectionClass::LockClass outer(cs);
	CriticalSectionClass::LockClass inner(cs);
	CHECK(true);	// reaching this line is the assertion
}

TEST(mutexclass_is_recursive)
{
	/* Likewise: a Win32 mutex is owned by a thread and re-acquiring it from that thread succeeds
	   rather than blocking. */
	MutexClass m;
	MutexClass::LockClass a(m);
	CHECK(!a.Failed());
	MutexClass::LockClass b(m);
	CHECK(!b.Failed());
}

class LockHolderWorker : public ThreadClass
{
public:
	LockHolderWorker(MutexClass &m) : ThreadClass(next_thread_name("LockHolderWorker").c_str()), Held(false), m_mutex(m) {}
	std::atomic<bool> Held;
protected:
	virtual void Thread_Function()
	{
		MutexClass::LockClass held(m_mutex);
		Held = true;
		while (running) { ThreadClass::Sleep_Ms(1); }
	}
private:
	MutexClass &m_mutex;
};

TEST(mutexclass_poll_fails_rather_than_blocking)
{
	/* Lock(0) is WaitForSingleObject with a zero timeout: a poll.  Five GameSpy call sites take
	   their mutex that way and carry on when it fails, so a replacement that blocked instead
	   would turn a skipped update into a stalled network thread. */
	MutexClass m;
	LockHolderWorker worker(m);
	worker.Execute();
	while (!worker.Held) { ThreadClass::Sleep_Ms(1); }

	MutexClass::LockClass poll(m, 0);
	CHECK(poll.Failed());

	worker.Stop(1000);
}

TEST(mutexclass_timed_acquire_gives_up_and_says_so)
{
	MutexClass m;
	LockHolderWorker worker(m);
	worker.Execute();
	while (!worker.Held) { ThreadClass::Sleep_Ms(1); }

	/* Timed with steady_clock (QueryPerformanceCounter on MSVC), not Clock_Milliseconds_Coarse: on Windows that
	   is GetTickCount, in 15.625 ms ticks at the default timer resolution, and a 60 ms wait reads as 46.9 ms
	   whenever only three tick boundaries fall inside it.  It failed the integration gate once (2026-09-27);
	   on the VM, 200 such waits read as low as 47 ms by GetTickCount and never under 61 ms by steady_clock.
	   The other coarse-clock bounds in this file allow 50 ms or more. */
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	{
		MutexClass::LockClass timed(m, 60);
		CHECK(timed.Failed());
	}
	CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >= 50);

	worker.Stop(1000);
}

class IdWorker : public ThreadClass
{
public:
	IdWorker() : ThreadClass(next_thread_name("IdWorker").c_str()), Id(0), Stable(false) {}
	std::atomic<unsigned> Id;
	std::atomic<bool> Stable;
protected:
	virtual void Thread_Function()
	{
		Id = ThreadClass::_Get_Current_Thread_ID();
		Stable = (Id == ThreadClass::_Get_Current_Thread_ID());
		while (running) { ThreadClass::Sleep_Ms(1); }
	}
};

TEST(thread_ids_are_distinct_stable_and_never_zero)
{
	/* Every use of _Get_Current_Thread_ID in the tree is an equality test against one stored
	   earlier: DX8_THREAD_ASSERT, TextureLoader's three main-thread asserts, wwmemlog's
	   per-thread category stack.  An id that repeats makes those pass from the wrong thread, and
	   zero - which the old _UNIX branch returned for every thread - makes all of them pass from
	   every thread. */
	const unsigned mine = ThreadClass::_Get_Current_Thread_ID();
	CHECK(mine != 0);
	CHECK(mine == ThreadClass::_Get_Current_Thread_ID());

	IdWorker a, b;
	a.Execute();
	b.Execute();
	while (a.Id == 0 || b.Id == 0) { ThreadClass::Sleep_Ms(1); }

	CHECK(a.Stable);
	CHECK(b.Stable);
	CHECK(a.Id != 0);
	CHECK(b.Id != 0);
	CHECK(a.Id != b.Id);
	CHECK(a.Id != mine);
	CHECK(b.Id != mine);

	a.Stop(1000);
	b.Stop(1000);
}

class CountingWorker : public ThreadClass
{
public:
	CountingWorker() : ThreadClass(next_thread_name("CountingWorker").c_str()), Ticks(0) {}
	std::atomic<long> Ticks;
protected:
	virtual void Thread_Function() { while (running) { ++Ticks; ThreadClass::Sleep_Ms(0); } }
};

TEST(threadclass_running_flag_reaches_the_worker)
{
	/* `running` was a volatile bool written by Stop() on one thread and read by Thread_Function
	   on another.  MSVC's /volatile:ms gave that acquire/release semantics; clang gives it none,
	   and ThreadSanitizer called it a data race on the first armed run.  It is a std::atomic<bool>
	   now.  What this test pins is the consequence rather than the mechanism: a worker that never
	   observes the write runs until Stop's timeout instead of stopping, so a prompt Stop is the
	   evidence that the write got there. */
	CountingWorker w;
	CHECK(!w.Is_Running());
	w.Execute();
	CHECK(w.Is_Running());
	ThreadClass::Sleep_Ms(30);

	unsigned start = Clock_Milliseconds_Coarse();
	w.Stop(3000);
	CHECK((Clock_Milliseconds_Coarse() - start) < 1000);
	CHECK(!w.Is_Running());
	CHECK(w.Ticks > 0);
}

TEST(fastcriticalsection_serialises_a_plain_counter)
{
	/* The one lock in this header that is not recursive and never was - its own comment says so,
	   and the old spin enforced it by spinning forever on a flag only the holder could clear.
	   What it does guarantee is exclusion, over a counter that is deliberately not atomic so
	   that a lock which stopped locking would show up as a wrong total rather than as nothing. */
	FastCriticalSectionClass lock;
	static long counter;	// static so the lambda-free worker below can reach it
	counter = 0;

	struct Bumper : public ThreadClass
	{
		/* Each its own name: WWLib's Unregister_Thread_ID finds a thread by name and asserts the id, so four
		   live threads called "Bumper" failed that assert in a Debug build (W3). */
		Bumper(FastCriticalSectionClass &l, long *c, const char *name) : ThreadClass(name), m_lock(l), m_counter(c) {}
		virtual void Thread_Function()
		{
			for (int i = 0; i < 20000; ++i) {
				FastCriticalSectionClass::LockClass held(m_lock);
				++(*m_counter);
			}
			running = false;
		}
		FastCriticalSectionClass &m_lock;
		long *m_counter;
	};

	Bumper a(lock, &counter, "BumperA"), b(lock, &counter, "BumperB"), c(lock, &counter, "BumperC"), d(lock, &counter, "BumperD");
	a.Execute(); b.Execute(); c.Execute(); d.Execute();
	a.Stop(30000); b.Stop(30000); c.Stop(30000); d.Stop(30000);

	CHECK_EQ(counter, 4L * 20000L);
}

/* ---------------------------------------------------------------------------------------------
 * stringex - bounded string copies.
 *
 * strncpy is the trap these replace: given a source at least as long as the destination it copies
 * dstsize characters and writes no terminator, so every later read runs off the end of the buffer.
 * strlcpy always terminates and reports the length it wanted, which is the only way a caller can
 * tell that truncation happened.
 * --------------------------------------------------------------------------------------------- */
TEST(strlcpy_terminates_and_reports_the_length_it_wanted)
{
	char dst[8];

	/* fits: full copy, terminated, return is the source length */
	memset(dst, 'X', sizeof(dst));
	CHECK_EQ(6u, (unsigned)strlcpy(dst, "abcdef", sizeof(dst)));
	CHECK_STR("abcdef", dst);

	/* exactly fills: still terminated inside the buffer */
	memset(dst, 'X', sizeof(dst));
	CHECK_EQ(7u, (unsigned)strlcpy(dst, "abcdefg", sizeof(dst)));
	CHECK_STR("abcdefg", dst);
	CHECK_EQ(0, (int)dst[7]);

	/* too long: truncated, still terminated, and the return says so */
	memset(dst, 'X', sizeof(dst));
	CHECK_EQ(11u, (unsigned)strlcpy(dst, "abcdefghijk", sizeof(dst)));
	CHECK_STR("abcdefg", dst);
	CHECK(11u >= sizeof(dst));					/* return >= dstsize is the truncation signal */

	/* a zero-sized destination is not written at all */
	char guard[2] = { 'A', 'B' };
	CHECK_EQ(3u, (unsigned)strlcpy(guard, "abc", 0));
	CHECK_EQ('A', guard[0]);
	CHECK_EQ('B', guard[1]);

	/* empty source */
	CHECK_EQ(0u, (unsigned)strlcpy(dst, "", sizeof(dst)));
	CHECK_STR("", dst);
}

TEST(strlcpy_does_not_write_past_the_destination)
{
	/* The overflow this exists to stop: a long source into a short buffer must leave the byte
	   after the buffer untouched. */
	struct { char buf[4]; char canary; } s;
	s.canary = (char)0x7E;
	CHECK_EQ(9u, (unsigned)strlcpy(s.buf, "123456789", sizeof(s.buf)));
	CHECK_STR("123", s.buf);
	CHECK_EQ((char)0x7E, s.canary);
}

TEST(strlcat_appends_within_the_buffer_and_terminates)
{
	char dst[8];

	strcpy(dst, "abc");
	CHECK_EQ(6u, (unsigned)strlcat(dst, "def", sizeof(dst)));
	CHECK_STR("abcdef", dst);

	/* appending past the end truncates and reports what it wanted */
	strcpy(dst, "abcde");
	CHECK_EQ(10u, (unsigned)strlcat(dst, "fghij", sizeof(dst)));
	CHECK_STR("abcdefg", dst);

	/* a full destination is left alone */
	strcpy(dst, "abcdefg");
	CHECK_EQ(10u, (unsigned)strlcat(dst, "hij", sizeof(dst)));
	CHECK_STR("abcdefg", dst);

	/* appending nothing changes nothing */
	strcpy(dst, "abc");
	CHECK_EQ(3u, (unsigned)strlcat(dst, "", sizeof(dst)));
	CHECK_STR("abc", dst);
}

TEST(wcslcpy_and_wcslcat_count_characters_not_bytes)
{
	/* The template is instantiated per character type, so the wide forms have to bound by
	   character count - bounding by bytes would halve the usable buffer. */
	wchar_t dst[8];

	CHECK_EQ(6u, (unsigned)wcslcpy(dst, L"abcdef", 8));
	CHECK(wcscmp(dst, L"abcdef") == 0);

	CHECK_EQ(11u, (unsigned)wcslcpy(dst, L"abcdefghijk", 8));
	CHECK(wcscmp(dst, L"abcdefg") == 0);

	wcscpy(dst, L"abc");
	CHECK_EQ(6u, (unsigned)wcslcat(dst, L"def", 8));
	CHECK(wcscmp(dst, L"abcdef") == 0);
}

//-------------------------------------------------------------------------------------------------
//
// threadwake.h: WW3D's texture loader thread sleeps on a ThreadWakeClass between tasks, where it used to
// look at its queue every millisecond (textureloader.cpp).  WakeWorker plays that thread exactly: its loop
// waits for work and takes one item a turn, and its own destructor Stop()s the wake before ~ThreadClass
// joins it, as LoaderThreadClass's does.  A wake that could be lost leaves an item unserved; a Stop() that
// could not reach a sleeping worker hangs the join.
//
#include "threadwake.h"

class WakeWorker : public ThreadClass
{
public:
	WakeWorker(ThreadWakeClass &wake, std::atomic<int> &pending, std::atomic<int> &served)
		: ThreadClass("WakeWorker"), m_wake(wake), m_pending(pending), m_served(served) {}
	~WakeWorker() { m_wake.Stop(); }

	void Thread_Function()
	{
		while (running) {
			if (!m_wake.Wait([this] { return m_pending.load() > 0; }))
				break;
			--m_pending;
			++m_served;
		}
	}

private:
	ThreadWakeClass &m_wake;
	std::atomic<int> &m_pending;
	std::atomic<int> &m_served;
};

/// Until served reaches want, or about five seconds: true when it did.
static bool wait_for_served(const std::atomic<int> &served, int want)
{
	for (int i = 0; i < 5000 && served.load() < want; ++i)
		ThreadClass::Sleep_Ms(1);
	return served.load() >= want;
}

static long long ms_since(std::chrono::steady_clock::time_point start)
{
	return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

TEST(threadwake_serves_work_queued_while_the_worker_sleeps)
{
	ThreadWakeClass wake;
	std::atomic<int> pending(0), served(0);
	WakeWorker *worker = new WakeWorker(wake, pending, served);
	worker->Execute();
	ThreadClass::Sleep_Ms(100);		// asleep in Wait by now
	CHECK_EQ(served.load(), 0);
	for (int item = 1; item <= 3; ++item) {
		++pending;
		wake.Wake();
		CHECK(wait_for_served(served, item));
	}
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	delete worker;						// ~WakeWorker Stop()s the wake, then ~ThreadClass joins
	CHECK(ms_since(start) < 1000);
	CHECK_EQ(served.load(), 3);
}

TEST(threadwake_work_queued_before_the_first_wait_is_not_lost)
{
	ThreadWakeClass wake;
	std::atomic<int> pending(1), served(0);
	WakeWorker *worker = new WakeWorker(wake, pending, served);
	worker->Execute();					// no Wake(): the work was there before the worker looked
	CHECK(wait_for_served(served, 1));
	delete worker;
}

TEST(threadwake_stop_ends_a_sleeping_worker_at_once)
{
	ThreadWakeClass wake;
	std::atomic<int> pending(0), served(0);
	WakeWorker *worker = new WakeWorker(wake, pending, served);
	worker->Execute();
	ThreadClass::Sleep_Ms(100);
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	wake.Stop();
	delete worker;
	CHECK(ms_since(start) < 1000);
	CHECK(!wake.Wait([] { return true; }));	// stopped: even work does not keep a worker
	CHECK_EQ(served.load(), 0);
}

TEST(threadwake_restart_lets_a_new_worker_run)
{
	ThreadWakeClass wake;
	std::atomic<int> pending(0), served(0);
	wake.Stop();
	wake.Restart();						// as TextureLoader::Init does before Execute()
	WakeWorker *worker = new WakeWorker(wake, pending, served);
	worker->Execute();
	++pending;
	wake.Wake();
	CHECK(wait_for_served(served, 1));
	delete worker;
}
