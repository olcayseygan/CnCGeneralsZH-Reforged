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

// FILE: CrcAgreement.h ///////////////////////////////////////////////////////////////////////////
// Desc:   Deciding whether the machines in a room actually disagree about a frame.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _CRC_AGREEMENT_H_
#define _CRC_AGREEMENT_H_

#include "Lib/BaseType.h"

/* Every so often each machine hashes its world and sends the number to everybody else.  If two of
	 those numbers differ, the simulations have diverged and the match is over - so this decision has
	 to be right, and calling a mismatch that did not happen is as bad as missing one.

	 Two ways to get it wrong, both of them EA's, both of them about players who are on their way out
	 of the game:

	   - A player who leaves stays "connected" for a few more frames than their last CRC message
	     survives.  At a short CRC interval that gap is hit routinely, and a missing CRC is not
	     evidence that anybody computed anything differently - it is evidence that a packet is not
	     here yet.

	   - The reverse: a CRC that arrives from somebody who is *already* gone.  EA compared every
	     entry in the map, so the leaver's last hash - computed on a machine that was in the middle
	     of tearing the game down - was held against everybody who is still playing.

	 So: only hashes from players who are still connected count, and all of them have to be here
	 before anything is compared. */

enum CrcAgreement
{
	CRC_AGREEMENT_TOO_FEW,	///< at least one connected player has not reported yet - decide later
	CRC_AGREEMENT_AGREE,		///< everybody still playing hashed the same world
	CRC_AGREEMENT_MISMATCH	///< two players who are both still connected disagree
};

/**
	Judge one CRC frame.

	@param reported			slot i sent a CRC for this frame
	@param crc					what slot i sent (only read where reported[i])
	@param connected		slot i is still in the game
	@param slotCount		length of the three arrays
	@param connectedPlayerCount  how many players the network says are still connected
*/
inline CrcAgreement crcAgreement( const Bool *reported, const UnsignedInt *crc, const Bool *connected,
																	Int slotCount, Int connectedPlayerCount )
{
	Int heard = 0;
	Bool haveReference = FALSE;
	UnsignedInt reference = 0;
	Bool disagreed = FALSE;

	for (Int i = 0; i < slotCount; ++i)
	{
		if (!reported[i] || !connected[i])
			continue;

		++heard;

		if (!haveReference)
		{
			haveReference = TRUE;
			reference = crc[i];
		}
		else if (reference != crc[i])
		{
			disagreed = TRUE;
		}
	}

	if (heard < connectedPlayerCount)
		return CRC_AGREEMENT_TOO_FEW;

	return disagreed ? CRC_AGREEMENT_MISMATCH : CRC_AGREEMENT_AGREE;
}

/**
	Who is off the majority at a mismatch, for the cheat witness.  A strict majority of the connected
	players that reported agree on one CRC, and odd[i] marks every other connected reporter.  Without
	a strict majority (two players, or an even split) nobody is named, since nothing says which side
	is wrong.  Returns how many are marked.  Same arrays as crcAgreement; odd has slotCount entries.
*/
inline Int crcOddOnes( const Bool *reported, const UnsignedInt *crc, const Bool *connected,
											 Int slotCount, Bool *odd )
{
	Int heard = 0;
	for (Int i = 0; i < slotCount; ++i)
	{
		odd[i] = FALSE;
		if (reported[i] && connected[i])
			++heard;
	}

	// sixteen players at most, so counting each value against every other is nothing
	for (Int i = 0; i < slotCount; ++i)
	{
		if (!reported[i] || !connected[i])
			continue;
		Int same = 0;
		for (Int j = 0; j < slotCount; ++j)
		{
			if (reported[j] && connected[j] && crc[j] == crc[i])
				++same;
		}
		if (same * 2 <= heard)
			continue;

		Int marked = 0;
		for (Int j = 0; j < slotCount; ++j)
		{
			if (reported[j] && connected[j] && crc[j] != crc[i])
			{
				odd[j] = TRUE;
				++marked;
			}
		}
		return marked;
	}
	return 0;
}

#endif // _CRC_AGREEMENT_H_
