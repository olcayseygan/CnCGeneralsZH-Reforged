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

// Copyright (C) Electronic Arts Canada Inc. 1995-2002.  All rights reserved.

#ifndef __HUFREAD
#define __HUFREAD 1

#include <string.h>
#include "codex.h"
#include "huffcodex.h"

#if defined(_MSC_VER)
#pragma warning(push,1)
#endif

/****************************************************************/
/*  Internal Functions                                          */
/****************************************************************/

// A bounded MSB-first reader avoids the legacy decoder's speculative 16-bit
// reads, unchecked code tables and unbounded clue runs.
class HuffBits
{
    const unsigned char *data;
    int length, byte;
    unsigned bit;
public:
    HuffBits(const unsigned char *p, int n) : data(p), length(n), byte(0), bit(0) {}
    bool read(unsigned count, unsigned &value) {
        value = 0;
        if (count > 32) return false;
        while (count--) {
            if (byte >= length) return false;
            value = (value << 1) | ((data[byte] >> (7-bit)) & 1);
            if (++bit == 8) { bit = 0; ++byte; }
        }
        return true;
    }
    bool number(unsigned &value, unsigned limit) {
        unsigned flag;
        if (!read(1, flag)) return false;
        if (flag) return read(2, value) && value <= limit;
        unsigned width = 3;
        do {
            if (!read(1, flag)) return false;
            if (!flag && ++width > 31) return false;
        } while (!flag);
        unsigned tail;
        if (!read(width, tail)) return false;
        unsigned long long result = (1ULL << width) - 4 + tail;
        if (result > limit) return false;
        value = (unsigned)result;
        return true;
    }
};

static int HUFF_decompress(const unsigned char *src, int length, unsigned char *dest, int capacity)
{
    if (!src || !dest || length <= 0 || capacity <= 0) return 0;
    HuffBits input(src, length);
    unsigned type, size, unused, clue;
    if (!input.read(16, type) || (type & 0xff) != 0xfb ||
        ((type & 0x7f00) < 0x3000) || ((type & 0x7f00) > 0x3500)) return 0;
    const unsigned sizeBits = (type & 0x8000) ? 32 : 24;
    if ((type & 0x100) && !input.read(sizeBits, unused)) return 0;
    if (!input.read(sizeBits, size) || !size || size > unsigned(capacity) || !input.read(8, clue)) return 0;
    unsigned counts[17] = {}, first[17] = {}, offsets[17] = {};
    unsigned total = 0, code = 0, most = 0;
    for (unsigned bits = 1; bits <= 16; ++bits) {
        code <<= 1; first[bits] = code; offsets[bits] = total;
        if (!input.number(counts[bits], 256-total) || counts[bits] > (1u << bits) - code) return 0;
        total += counts[bits]; code += counts[bits];
        if (code == (1u << bits)) { most = bits; break; }
    }
    if (!most || !total) return 0;
    unsigned char symbols[256];
    bool used[256] = {};
    unsigned next = 255;
    for (unsigned i = 0; i < total; ++i) {
        unsigned leap;
        if (!input.number(leap, 255-i)) return 0;
        ++leap;
        do { next = (next+1) & 255; if (!used[next]) --leap; } while (leap);
        symbols[i] = (unsigned char)next; used[next] = true;
    }
    if (!used[clue]) return 0; // EOF must be representable.
    unsigned written = 0;
    for (;;) {
        unsigned symbol = 256; code = 0;
        for (unsigned bits = 1; bits <= most; ++bits) {
            unsigned bit;
            if (!input.read(1, bit)) return 0;
            code = (code << 1) | bit;
            if (code >= first[bits] && code-first[bits] < counts[bits]) {
                symbol = symbols[offsets[bits] + code-first[bits]]; break;
            }
        }
        if (symbol == 256) return 0;
        if (symbol == clue) {
            unsigned run;
            if (!input.number(run, size-written)) return 0;
            if (run) {
                if (!written) return 0;
                memset(dest+written, dest[written-1], run); written += run;
                continue;
            }
            unsigned eof;
            if (!input.read(1, eof)) return 0;
            if (eof) {
                if (written != size) return 0;
                // Preserve delta and accelerated-delta members, including sized headers.
                unsigned value = 0, delta = 0;
                for (unsigned i = 0; i < size && (type & 0x600); ++i) {
                    delta = (delta + dest[i]) & 255;
                    if (type & 0x400) { value = (value + delta) & 255; dest[i] = (unsigned char)value; }
                    else dest[i] = (unsigned char)delta;
                }
                return int(size);
            }
            if (!input.read(8, symbol)) return 0;
        }
        if (written == size) return 0;
        dest[written++] = (unsigned char)symbol;
    }
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 30fb..35fb header */

bool GCALL HUFF_is(const void *compresseddata)
{
    bool ok=false;
    int packtype=ggetm(compresseddata,2);

    if (packtype==0x30fb
     || packtype==0x31fb
     || packtype==0x32fb
     || packtype==0x33fb
     || packtype==0x34fb
     || packtype==0x35fb
     || packtype==0xb0fb
     || packtype==0xb1fb
     || packtype==0xb2fb
     || packtype==0xb3fb
     || packtype==0xb4fb
     || packtype==0xb5fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL HUFF_size(const void *compresseddata)
{
    int len=0;
    int packtype=ggetm(compresseddata,2);
    int ssize=(packtype&0x8000)?4:3;

    if (packtype&0x100)     /* 31fb 33fb 35fb */
    {
        len = ggetm((char *)compresseddata+2+ssize,ssize);
    }
    else                    /* 30fb 32fb 34fb */
    {
        len = ggetm((char *)compresseddata+2,ssize);
    }

    return(len);
}

int GCALL HUFF_decode(void *dest, const void *compresseddata, int *compressedsize, int capacity)
{
    return compressedsize ? HUFF_decompress((const unsigned char *)compresseddata, *compressedsize, (unsigned char *)dest, capacity) : 0;
}

#endif

