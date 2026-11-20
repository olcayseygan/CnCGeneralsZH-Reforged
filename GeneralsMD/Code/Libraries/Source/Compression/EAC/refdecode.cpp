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

#ifndef __REFREAD
#define __REFREAD 1

#include <string.h>
#include "codex.h"
#include "refcodex.h"

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 10fb header */

bool GCALL REF_is(const void *compresseddata)
{
    bool ok=false;
    int packtype=ggetm(compresseddata,2);

    if (packtype==0x10fb
     || packtype==0x11fb
     || packtype==0x90fb
     || packtype==0x91fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL REF_size(const void *compresseddata)
{
    int len=0;
    int packtype=ggetm(compresseddata,2);
    int ssize=(packtype&0x8000)?4:3;

    if (packtype&0x100)     /* 11fb */
    {
        len = ggetm((char *)compresseddata+2+ssize,ssize);
    }
    else                    /* 10fb */
    {
        len = ggetm((char *)compresseddata+2,ssize);
    }

    return(len);
}


// Both capacities come from the caller, not from the compressed stream.
int GCALL REF_decode(void *dest, const void *compresseddata, int *compressedsize, int capacity)
{
    if (!dest || !compresseddata || !compressedsize || *compressedsize < 5 || capacity <= 0)
        return 0;
    const unsigned char *s = (const unsigned char *)compresseddata;
    unsigned char *d = (unsigned char *)dest;
    const unsigned int input = (unsigned int)*compressedsize;
    unsigned int at = 2, written = 0;
    const unsigned int type = ((unsigned int)s[0] << 8) | s[1];
    if (type != 0x10fb && type != 0x11fb && type != 0x90fb && type != 0x91fb)
        return 0;
    const unsigned int width = (type & 0x8000) ? 4 : 3;
    if (type & 0x100) at += width;
    if (at + width > input) return 0;
    unsigned int expected = 0;
    for (unsigned int i = 0; i < width; ++i) expected = (expected << 8) | s[at++];
    if (expected > (unsigned int)capacity) return 0;

    while (at < input)
    {
        const unsigned int first = s[at++];
        unsigned int literals = 0, length = 0, distance = 0;
        bool last = false;
        if (first < 0x80)
        {
            if (input - at < 1) return 0;
            const unsigned int second = s[at++];
            literals = first & 3;
            distance = ((first & 0x60) << 3) + second + 1;
            length = ((first & 0x1c) >> 2) + 3;
        }
        else if (first < 0xc0)
        {
            if (input - at < 2) return 0;
            const unsigned int second = s[at++], third = s[at++];
            literals = second >> 6;
            distance = ((second & 0x3f) << 8) + third + 1;
            length = (first & 0x3f) + 4;
        }
        else if (first < 0xe0)
        {
            if (input - at < 3) return 0;
            const unsigned int second = s[at++], third = s[at++], fourth = s[at++];
            literals = first & 3;
            distance = ((first & 0x10) << 12) + (second << 8) + third + 1;
            length = ((first & 0x0c) << 6) + fourth + 5;
        }
        else
        {
            literals = ((first & 0x1f) << 2) + 4;
            if (literals > 112) { literals = first & 3; last = true; }
        }
        if (literals > input - at || literals > expected - written) return 0;
        memcpy(d + written, s + at, literals);
        at += literals;
        written += literals;
        if (length)
        {
            if (distance > written || length > expected - written) return 0;
            // Overlapping references deliberately copy forward, as RefPack requires.
            for (unsigned int i = 0; i < length; ++i) {
                d[written] = d[written - distance];
                ++written;
            }
        }
        if (last)
        {
            if (written != expected) return 0;
            *compressedsize = (int)at;
            return (int)written;
        }
    }
    return 0; // no terminating command
}

#endif

