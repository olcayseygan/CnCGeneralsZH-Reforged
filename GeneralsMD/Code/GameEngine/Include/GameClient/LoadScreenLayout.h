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

// FILE: LoadScreenLayout.h ///////////////////////////////////////////////////////////////////////
//
// The load board's card decisions that need no window and no font, so a test can walk every column
// from one card to sixteen at 1080 and 720: how tall a card is, what it gives up when a column is
// crowded, and where its lines, portrait, start spot disc and load bar sit inside it.
// LoadScreen.cpp measures its fonts, asks LoadScreen_planCards and draws what it says.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _LOADSCREENLAYOUT_H_
#define _LOADSCREENLAYOUT_H_

#include "Lib/BaseType.h"

/// the board's band on the 1920x1080 picture: the cards and the middle start under the header rule,
/// and stop a gap over the tip panel, which stands a gap over the button row
const Real LOAD_BAND_TOP = 176.0f;
const Real LOAD_FOOT_GAP = 16.0f;
const Real LOAD_TIP_PAD = 10.0f;				///< inside the tip panel, round its two lines
const Real LOAD_CARD_TALLEST = 220.0f;	///< a 1v1's card
const Real LOAD_CARD_PAD = 16.0f;
const Real LOAD_PORTRAIT_LARGEST = 168.0f;

/// what a crowded column gives up, in order
enum LoadCardMode
{
	LOAD_CARD_FULL,				///< portrait, name, general, a computer seat's level and personality
	LOAD_CARD_SHORT,			///< the third line gone
	LOAD_CARD_LINE,				///< the side's crest at line height, "name · general" on one line
	LOAD_CARD_SMALL_LINE	///< that line in the general's type, the name's bold kept
};

/// the line heights a column is planned with, in screen pixels
struct LoadCardLines
{
	Real scale;			///< screen pixels to a 1080 board pixel
	Int name;				///< a name, TEXT_STRONG bold
	Int army;				///< the general, TEXT_BODY; a small line's name too
	Int detail;			///< a computer seat's level and personality, TEXT_SMALL
	Int percent;		///< the card's percentage, TEXT_SMALL bold
	Int band;				///< a team band's label, TEXT_SMALL bold
	Int disc;				///< the start spot disc at the size the map draws it
	Bool bars;			///< the cards carry their player's own load bar
};

/// one column's cards; every position inside a card is from the card's top
struct LoadCardPlan
{
	LoadCardMode mode;
	Bool fits;				///< FALSE only when even the tightest card overflows the column
	Int cardHeight;
	Int gap;					///< between a card or a band and the next
	Int bandHeight;		///< a team band's row; 0 when the column has no room for bands at all
	Bool bandLabels;	///< the band carries its swatch and "Team N"; FALSE: a rule in the team's colour
	Int used;					///< the column's whole height, bands and gaps included
	Int emblem;				///< the portrait's side, or the crest's on a one-line card
	Int emblemTop;
	Int nameTop;
	Int armyTop;			///< on the name's line on a one-line card
	Int detailTop;		///< -1 when the line is not drawn
	Int disc;					///< the start spot disc's side
	Int discTop;
	Int barTop;				///< -1 without a bar: no bars, or a column so full it keeps the percentage alone
	Int barHeight;
	Int percentTop;		///< the percentage beside the bar, or on a one-line card's line
};

inline Int LoadScreen_px( Real at1080, Real scale )
{
	return REAL_TO_INT( at1080 * scale + 0.5f );
}

/// the tip panel's height: two of the tip's lines, always, so a short tip never moves the board
inline Int LoadScreen_tipPanelHeight( Int tipLine, Real scale )
{
	return 2 * tipLine + 2 * LoadScreen_px( LOAD_TIP_PAD, scale );
}

/// the line a one-line card's words, disc and percentage stand on
inline Int LoadScreen_lineHeight( LoadCardMode mode, const LoadCardLines &lines )
{
	return max( max( mode == LOAD_CARD_LINE ? lines.name : lines.army, lines.percent ), lines.disc );
}

/// the least height a card of mode needs
inline Int LoadScreen_cardNeeds( LoadCardMode mode, const LoadCardLines &lines )
{
	const Real s = lines.scale;
	const Int inset = LoadScreen_px( 8.0f, s );
	const Int lineGap = LoadScreen_px( 4.0f, s );
	const Int barHeight = max( 3, LoadScreen_px( 5.0f, s ) );
	switch( mode )
	{
		case LOAD_CARD_FULL:
			return 2 * inset + lines.name + lineGap + lines.army + lineGap + lines.detail + ( lines.bars ? LoadScreen_px( 24.0f, s ) : 0 );
		case LOAD_CARD_SHORT:
			return 2 * inset + lines.name + lineGap + lines.army + ( lines.bars ? LoadScreen_px( 24.0f, s ) : 0 );
		default:
		{
			const Int edge = max( 1, LoadScreen_px( 2.0f, s ) );
			return 2 * edge + LoadScreen_lineHeight( mode, lines ) + ( lines.bars ? edge + barHeight : 0 );
		}
	}
}

/** One column's cards: cards of them under bands team bands, in columnHeight pixels.  The first of
	* full, short, line and small line that fits, from first on, with labelled bands; then the same with
	* the bands pressed to a rule in the team's colour; then with no bands at all.  A card never grows
	* past a 1v1's, and a one-line card never past its line and a little air.  Nothing is ever drawn
	* smaller than the type it was given. */
inline LoadCardPlan LoadScreen_planCards( Int cards, Int bands, Int columnHeight, const LoadCardLines &lines,
	LoadCardMode first = LOAD_CARD_FULL )
{
	const Real s = lines.scale;
	const Int pad = LoadScreen_px( LOAD_CARD_PAD, s );
	const Int lineGap = LoadScreen_px( 4.0f, s );
	const Int barHeight = max( 3, LoadScreen_px( 5.0f, s ) );
	const Int edge = max( 1, LoadScreen_px( 2.0f, s ) );
	cards = max( cards, 1 );
	bands = max( bands, 0 );

	LoadCardPlan plan;
	plan.fits = FALSE;
	const Int bandSteps[ 3 ] = { lines.band + LoadScreen_px( 10.0f, s ), max( 2, LoadScreen_px( 6.0f, s ) ), 0 };
	Bool chosen = FALSE;
	for( Int step = 0; step < 3 && !chosen; step++ )
	{
		const Int bandHeight = bands > 0 ? bandSteps[ step ] : 0;
		const Int rows = cards + ( bandHeight > 0 ? bands : 0 );
		for( Int m = first; m <= LOAD_CARD_SMALL_LINE && !chosen; m++ )
		{
			const LoadCardMode mode = (LoadCardMode)m;
			const Bool tall = mode <= LOAD_CARD_SHORT;
			const Int gap = tall ? LoadScreen_px( cards > 4 ? 8.0f : 16.0f, s ) : edge;
			const Int room = ( columnHeight - bandHeight * ( rows - cards ) - gap * ( rows - 1 ) ) / cards;
			const Int needs = LoadScreen_cardNeeds( mode, lines );
			if( room < needs )
				continue;
			plan.mode = mode;
			plan.fits = TRUE;
			plan.gap = gap;
			plan.bandHeight = bandHeight;
			plan.bandLabels = bandHeight > 0 && step == 0;
			plan.cardHeight = tall ? min( room, LoadScreen_px( LOAD_CARD_TALLEST, s ) ) : min( room, needs + LoadScreen_px( 12.0f, s ) );
			plan.used = plan.cardHeight * cards + bandHeight * ( rows - cards ) + gap * ( rows - 1 );
			chosen = TRUE;
		}
		if( bands == 0 )
			break;
	}
	if( !chosen && lines.bars )
	{
		// the bars go before the words do: a one-line card keeps its percentage on its line
		LoadCardLines barless = lines;
		barless.bars = FALSE;
		LoadCardPlan percentOnly = LoadScreen_planCards( cards, bands, columnHeight, barless, max( first, LOAD_CARD_LINE ) );
		if( percentOnly.fits )
			return percentOnly;
	}
	if( !chosen )
	{
		// a column nothing fits: the tightest card, flush, and the words may run over its edge
		plan.mode = LOAD_CARD_SMALL_LINE;
		plan.gap = 0;
		plan.bandHeight = 0;
		plan.bandLabels = FALSE;
		plan.cardHeight = max( 1, columnHeight / cards );
		plan.used = plan.cardHeight * cards;
	}

	const Int h = plan.cardHeight;
	plan.barHeight = barHeight;
	plan.barTop = -1;
	plan.detailTop = -1;
	if( plan.mode <= LOAD_CARD_SHORT )
	{
		// the portrait centred against the card's outer edge; the words centred beside it, lifted
		// over a bar on the card's foot, or with the bar right under them where they would reach it
		plan.emblem = min( h - 2 * pad, LoadScreen_px( LOAD_PORTRAIT_LARGEST, s ) );
		plan.emblemTop = ( h - plan.emblem ) / 2;
		const Bool third = plan.mode == LOAD_CARD_FULL;
		const Int block = lines.name + lineGap + lines.army + ( third ? lineGap + lines.detail : 0 );
		Int y = ( h - block ) / 2;
		if( lines.bars )
		{
			y -= LoadScreen_px( 8.0f, s );
			plan.barTop = h - pad - barHeight;
			plan.percentTop = plan.barTop + barHeight / 2 - lines.percent / 2;
			if( y + block + LoadScreen_px( 6.0f, s ) > plan.percentTop )
			{
				const Int barGap = LoadScreen_px( 8.0f, s );
				y = ( h - block - barGap - barHeight ) / 2;
				plan.barTop = y + block + barGap;
				plan.percentTop = plan.barTop + barHeight / 2 - lines.percent / 2;
			}
		}
		else
			plan.percentTop = 0;
		plan.nameTop = y;
		plan.armyTop = y + lines.name + lineGap;
		if( third )
			plan.detailTop = plan.armyTop + lines.army + lineGap;
		// the disc in the inner top corner, the percentage under it at the bar
		plan.disc = lines.disc;
		plan.discTop = LoadScreen_px( 10.0f, s );
	}
	else
	{
		// one line, the crest at its height at the outer edge, the disc and the percentage at its
		// inner end, the bar under it
		const Int line = LoadScreen_lineHeight( plan.mode, lines );
		const Int content = line + ( lines.bars ? edge + barHeight : 0 );
		const Int top = ( h - content ) / 2;
		const Int nameLine = plan.mode == LOAD_CARD_LINE ? lines.name : lines.army;
		plan.emblem = line;
		plan.emblemTop = top;
		plan.nameTop = top + ( line - nameLine ) / 2;
		plan.armyTop = top + ( line - lines.army ) / 2;
		plan.disc = min( lines.disc, line );
		plan.discTop = top + ( line - plan.disc ) / 2;
		plan.percentTop = top + ( line - lines.percent ) / 2;
		if( lines.bars )
			plan.barTop = top + line + edge;
	}
	return plan;
}

/** How many sub-columns a column's cards stand in: one, or two side by side of one-line cards when
	* the column has more than LOAD_COLUMN_MOST or one sub-column cannot hold them.  bands are the
	* column's team bands as one sub-column would carry them. */
const Int LOAD_COLUMN_MOST = 16;
inline Int LoadScreen_subColumns( Int cards, Int bands, Int columnHeight, const LoadCardLines &lines )
{
	if( cards <= LOAD_COLUMN_MOST && LoadScreen_planCards( cards, bands, columnHeight, lines ).fits )
		return 1;
	return 2;
}

#endif // _LOADSCREENLAYOUT_H_
