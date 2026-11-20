// Round-trip self-check for CompressionManager over every codec it exposes.
// Run the 'compression_selfcheck' target; "OK" per codec means pass.

#include "Compression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	// Compressible-but-not-trivial payload: repeated text with a counter mixed in.
	enum { SRC_LEN = 64 * 1024 };
	static char src[SRC_LEN];
	for (int i = 0; i < SRC_LEN; i++) {
		src[i] = (char)("Command & Conquer Generals Zero Hour "[i % 37] + (i / 1024) % 7);
	}

	// Explicit list: COMPRESSION_MAX==COMPRESSION_REFPACK in the enum, so a
	// MIN..MAX sweep would only cover NONE+REFPACK.  Names are local because
	// getCompressionNameByType's table also stops at COMPRESSION_MAX.
	static const struct { CompressionType type; const char * name; } codecs[] = {
		{ COMPRESSION_REFPACK, "RefPack" }, { COMPRESSION_NOXLZH, "NoxLZH" },
		{ COMPRESSION_ZLIB1,   "ZLib1"   }, { COMPRESSION_ZLIB9,  "ZLib9"  },
		{ COMPRESSION_BTREE,   "BTree"   }, { COMPRESSION_HUFF,   "Huff"   },
	};

	int failures = 0;
	for (int t = 0; t < (int)(sizeof(codecs) / sizeof(codecs[0])); t++) {
		CompressionType type = codecs[t].type;
		const char * name = codecs[t].name;

		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, type);
		char * packed = (char *)malloc(max_out);
		int packed_len = CompressionManager::compressData(type, src, SRC_LEN, packed, max_out);
		if (packed_len <= 0) {
			printf("FAIL %-12s compressData returned %d\n", name, packed_len);
			failures++; free(packed); continue;
		}

		static char unpacked[SRC_LEN];
		memset(unpacked, 0, SRC_LEN);
		int unpacked_len = CompressionManager::decompressData(packed, packed_len, unpacked, SRC_LEN);
		if (unpacked_len != SRC_LEN || memcmp(src, unpacked, SRC_LEN) != 0) {
			printf("FAIL %-12s roundtrip: %d bytes back, expected %d\n", name, unpacked_len, SRC_LEN);
			failures++; free(packed); continue;
		}

		printf("OK   %-12s %d -> %d bytes\n", name, SRC_LEN, packed_len);
		free(packed);
	}
	// A literal run must fit the caller's capacity, even if the outer header lies.
	unsigned char malformed[] = {'E','A','R',0,1,0,0,0, 0x10,0xfb,0,0,1, 0xe0,'a','b','c','d',0xfc};
	unsigned char guarded[8];
	memset(guarded, 0xa5, sizeof(guarded));
	if (CompressionManager::decompressData(malformed, sizeof(malformed), guarded + 2, 1) != 0
			|| guarded[3] != 0xa5) { printf("FAIL RefPack output bound\n"); ++failures; }
	// Valid four-byte literal stream; every truncated prefix must be rejected.
	malformed[4] = 4; malformed[12] = 4;
	for (int size = 8; size < (int)sizeof(malformed); ++size) {
		if (CompressionManager::decompressData(malformed, size, guarded + 2, 4) != 0) {
			printf("FAIL RefPack truncated input at %d\n", size); ++failures;
		}
	}
	if (CompressionManager::decompressData(malformed, sizeof(malformed), guarded + 2, 4) != 4
			|| memcmp(guarded + 2, "abcd", 4) != 0 || guarded[1] != 0xa5 || guarded[6] != 0xa5) {
		printf("FAIL RefPack bounded literal\n"); ++failures;
	}
	unsigned char badReference[] = {'E','A','R',0,3,0,0,0, 0x10,0xfb,0,0,3, 0,0,0xfc};
	if (CompressionManager::decompressData(badReference, sizeof(badReference), guarded + 2, 4) != 0) {
		printf("FAIL RefPack reference before output\n"); ++failures;
	}

	if (failures) { printf("%d codec(s) FAILED\n", failures); return 1; }
	printf("all codecs OK\n");
	return 0;
}
