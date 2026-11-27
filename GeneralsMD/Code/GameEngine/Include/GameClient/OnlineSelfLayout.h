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

// FILE: OnlineSelfLayout.h ///////////////////////////////////////////////////////////////////////
// The ladder's rows and the skins page's cards on the 1920x1080 board (docs/plans/MENU-REDESIGN.md,
// package 11; the design is experiments/proofs/2026-11-24/screens/online-self/README-online-self.md in
// the wrapper).  Pure, so test_gameengine checks them; WOLLadderScreen.cpp places its windows by them.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _ONLINESELFLAYOUT_H_
#define _ONLINESELFLAYOUT_H_

/// the ladder's table: 20 rows of 32 from 256, on the 8 px grid, and the player's own row pinned under
/// them after an 8 high dotted gap, 32 high too; the table's box ends at 946
const Real ONLINE_LADDER_ROWS_TOP = 256.0f;
const Real ONLINE_LADDER_ROW_HEIGHT = 32.0f;
const Real ONLINE_LADDER_BOX_BOTTOM = 946.0f;
const Real ONLINE_LADDER_PINNED_GAP = 8.0f;
const Real ONLINE_LADDER_PINNED_HEIGHT = 32.0f;

/// the skins page's cards: 12 apart in the middle panel's 832 by 668 under the owned bar
const Real ONLINE_SKINS_CARD_GAP = 12.0f;

/** The cards sized to their count: the column count (1 to 4) that gives the largest card near the
	* model's 4:3, the rows split as evenly as they go (the first rows take the spare cards), each row's
	* cards sharing its whole width, so no slot stands empty.  Card slot of count in a width by height
	* area: its offset and size, whole pixels at 1080; all four 0 for a slot past count. */
inline void OnlineSkinsCardPlace( Int slot, Int count, Real width, Real height, Real &x, Real &y, Real &w, Real &h )
{
	x = y = w = h = 0.0f;
	if( count <= 0 || slot < 0 || slot >= count )
		return;
	Int rows = 1;
	Real best = -1.0f;
	for( Int columns = 1; columns <= 4; ++columns )
	{
		const Int need = ( count + columns - 1 ) / columns;
		const Real cw = ( width - ( columns - 1 ) * ONLINE_SKINS_CARD_GAP ) / columns;
		const Real ch = ( height - ( need - 1 ) * ONLINE_SKINS_CARD_GAP ) / need;
		const Real size = cw < ch * 4.0f / 3.0f ? cw : ch * 4.0f / 3.0f;
		if( size > best )
		{
			best = size;
			rows = need;
		}
	}
	h = (Real)(Int)( ( height - ( rows - 1 ) * ONLINE_SKINS_CARD_GAP ) / rows );
	Int first = 0;
	for( Int row = 0; row < rows; ++row )
	{
		const Int here = count / rows + ( row < count % rows ? 1 : 0 );
		if( slot < first + here )
		{
			w = (Real)(Int)( ( width - ( here - 1 ) * ONLINE_SKINS_CARD_GAP ) / here );
			x = ( slot - first ) * ( w + ONLINE_SKINS_CARD_GAP );
			y = row * ( h + ONLINE_SKINS_CARD_GAP );
			return;
		}
		first += here;
	}
}

#endif // _ONLINESELFLAYOUT_H_
