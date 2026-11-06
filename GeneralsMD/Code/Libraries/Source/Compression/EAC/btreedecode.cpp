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

#ifndef __BTRREAD
#define __BTRREAD 1

#include <string.h>
#include "codex.h"
#include "btreecodex.h"

/****************************************************************/
/*  Internal Functions                                          */
/****************************************************************/

// Source and destination limits are mandatory: archive lengths are untrusted.
static int BTREE_decompress(const unsigned char *src, int length, unsigned char *dest, int capacity)
{
    if (!src || !dest || length < 7 || capacity <= 0) return 0;
    unsigned type = (unsigned(src[0]) << 8) | src[1];
    if (type != 0x46fb && type != 0x47fb) return 0;
    int pos = type == 0x47fb ? 5 : 2;
    if (length - pos < 5) return 0;
    unsigned size = (unsigned(src[pos]) << 16) | (unsigned(src[pos+1]) << 8) | src[pos+2];
    pos += 3;
    if (!size || size > unsigned(capacity)) return 0;
    const unsigned clue = src[pos++], nodes = src[pos++];
    if (nodes > unsigned((length - pos) / 3)) return 0;
    bool branch[256] = {};
    unsigned char left[256] = {}, right[256] = {};
    for (unsigned i = 0; i < nodes; ++i) {
        unsigned node = src[pos++];
        // The encoder reserves the clue and assigns each branch once.
        if (node == clue || branch[node]) return 0;
        branch[node] = true; left[node] = src[pos++]; right[node] = src[pos++];
    }
    // Validate the entire dictionary, including unused nodes. Iterative DFS also
    // computes expansion lengths, bounding work and rejecting cycles before output.
    unsigned state[256] = {}, expanded[256] = {};
    unsigned stack[512];
    for (unsigned root = 0; root < 256; ++root) {
        if (!branch[root] || state[root] == 2) continue;
        unsigned top = 0; stack[top++] = root;
        while (top) {
            unsigned node = stack[top-1];
            if (!branch[node]) { expanded[node] = 1; state[node] = 2; --top; continue; }
            if (state[node] == 0) state[node] = 1;
            unsigned child = state[left[node]] != 2 ? left[node] : right[node];
            if (state[left[node]] == 2 && state[right[node]] == 2) {
                // Unused dictionary entries may expand beyond this member. Saturate
                // their lengths; reject only if an oversized branch is actually used.
                expanded[node] = expanded[left[node]] > size || expanded[right[node]] > size - expanded[left[node]]
                    ? size+1 : expanded[left[node]] + expanded[right[node]];
                state[node] = 2; --top; continue;
            }
            if (child == clue || state[child] == 1 || top == 512) return 0;
            stack[top++] = child;
        }
    }
    unsigned written = 0;
    while (pos < length) {
        unsigned node = src[pos++];
        if (node == clue) {
            if (pos == length) return 0;
            node = src[pos++];
            if (!node) return written == size ? int(written) : 0;
            if (written == size) return 0;
            dest[written++] = (unsigned char)node;
        } else {
            unsigned top = 0; stack[top++] = node;
            while (top) {
                node = stack[--top];
                if (branch[node]) {
                    if (expanded[node] > size - written || top > 509) return 0;
                    stack[top++] = right[node]; stack[top++] = left[node];
                } else {
                    if (written == size) return 0;
                    dest[written++] = (unsigned char)node;
                }
            }
        }
    }
    return 0; // A complete end marker is required, even when output is full.
}

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 46fb header */

bool GCALL BTREE_is(const void *compresseddata)
{
    bool ok=false;

    if (ggetm(compresseddata,2)==0x46fb
     || ggetm(compresseddata,2)==0x47fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL BTREE_size(const void *compresseddata)
{
    int len=0;

    if (ggetm(compresseddata,2)==0x46fb)
    {
        len = ggetm((char *)compresseddata+2,3);
    }
    else
    {
        len = ggetm((char *)compresseddata+2+3,3);
    }

    return(len);
}

int GCALL BTREE_decode(void *dest, const void *compresseddata, int *compressedsize, int capacity)
{
    return compressedsize ? BTREE_decompress((const unsigned char *)compresseddata, *compressedsize, (unsigned char *)dest, capacity) : 0;
}

#endif

