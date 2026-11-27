// The ladder's rows and the skins page's cards (docs/plans/MENU-REDESIGN.md, package 11; the design is
// experiments/proofs/2026-11-24/screens/online-self/README-online-self.md in the wrapper).
// OnlineSelfLayout.h, which WOLLadderScreen.cpp places its windows by.  Included into test_gameengine.cpp.

#include "GameClient/OnlineSelfLayout.h"

TEST( online_self_ladder_rows_fit_the_table_pinned_or_not )
{
	// 20 rows of 32 from 256, then an 8 gap and his 32 row, all inside the box's 946, every edge on the 8 grid
	// (the walker's lint: the README's 33 and 30 high rows and 20 gap read as off-grid gaps)
	CHECK_EQ( (Int)( ONLINE_LADDER_ROWS_TOP + 20 * ONLINE_LADDER_ROW_HEIGHT ), 896 );
	const Real pinnedEnd = ONLINE_LADDER_ROWS_TOP + 20 * ONLINE_LADDER_ROW_HEIGHT + ONLINE_LADDER_PINNED_GAP
		+ ONLINE_LADDER_PINNED_HEIGHT;
	CHECK_EQ( (Int)pinnedEnd, 936 );
	CHECK( pinnedEnd <= ONLINE_LADDER_BOX_BOTTOM - 8.0f );
	CHECK_EQ( (Int)ONLINE_LADDER_ROW_HEIGHT % 8, 0 );
	CHECK_EQ( (Int)ONLINE_LADDER_PINNED_GAP % 8, 0 );
	CHECK_EQ( (Int)ONLINE_LADDER_ROWS_TOP % 8, 0 );
}

static const Real SKINS_W = ONLINE_SKINS_CARDS_RIGHT - ONLINE_SKINS_CARDS_LEFT;		// 840
static const Real SKINS_H = ONLINE_SKINS_CARDS_BOTTOM - ONLINE_SKINS_CARDS_TOP;		// 672

TEST( online_self_skins_page_stands_on_the_8_grid )
{
	// the list: eight units under three heads, 70 high, 8 apart, 40 between sides, from 226 to 906
	const Real units = 8.0f, sides = 3.0f;
	const Real end = ONLINE_SKINS_UNITS_TOP + units * ONLINE_SKINS_UNIT_HEIGHT + ( units - sides ) * ONLINE_SKINS_UNIT_GAP
		+ ( sides - 1.0f ) * ONLINE_SKINS_SIDE_GAP;
	CHECK_EQ( (Int)end, 906 );
	CHECK( end <= ONLINE_SKINS_UNITS_BOTTOM );
	CHECK_EQ( ( (Int)ONLINE_SKINS_UNITS_TOP - 162 ) % 8, 0 );		// under the tabs' foot
	CHECK( 976 - (Int)ONLINE_SKINS_UNITS_BOTTOM > 64 );			// a gap the walker's lint leaves alone
	CHECK_EQ( (Int)ONLINE_SKINS_UNIT_GAP % 8, 0 );
	CHECK_EQ( (Int)ONLINE_SKINS_SIDE_GAP % 8, 0 );
	// the cards: 48 right of the list's rows (412), 32 under the filters (226), 16 apart
	CHECK_EQ( (Int)ONLINE_SKINS_CARDS_LEFT - 412, 48 );
	CHECK_EQ( (Int)ONLINE_SKINS_CARDS_TOP - 226, 32 );
	CHECK_EQ( (Int)ONLINE_SKINS_CARD_GAP, 16 );
}

/// the cards of count laid in the skins page's 840 by 672: per row, and every card checked
static Int skinsRows( Int count, Int perRow[ 8 ] )
{
	Int rows = 0;
	Real lastY = -1.0f;
	for( Int slot = 0; slot < count; ++slot )
	{
		Real x, y, w, h;
		OnlineSkinsCardPlace( slot, count, SKINS_W, SKINS_H, x, y, w, h );
		if( y != lastY )
		{
			perRow[ rows++ ] = 0;
			lastY = y;
		}
		++perRow[ rows - 1 ];
	}
	return rows;
}

TEST( online_self_skins_cards_split_their_rows_evenly )
{
	Int perRow[ 8 ];
	// the Chinook's seven looks: 3, 2 and 2
	CHECK_EQ( skinsRows( 7, perRow ), 3 );
	CHECK_EQ( perRow[ 0 ], 3 );
	CHECK_EQ( perRow[ 1 ], 2 );
	CHECK_EQ( perRow[ 2 ], 2 );
	// the Worker's two: one above the other, each the panel's whole width
	CHECK_EQ( skinsRows( 2, perRow ), 2 );
	CHECK_EQ( perRow[ 0 ], 1 );
	Real x, y, w, h;
	OnlineSkinsCardPlace( 1, 2, SKINS_W, SKINS_H, x, y, w, h );
	CHECK_EQ( (Int)w, 840 );
	CHECK_EQ( (Int)( y + h ), 672 );
	// a slot past the count has no place
	OnlineSkinsCardPlace( 2, 2, SKINS_W, SKINS_H, x, y, w, h );
	CHECK_EQ( (Int)w, 0 );
}

TEST( online_self_skins_cards_fill_each_row_and_never_overlap )
{
	for( Int count = 1; count <= 12; ++count )
	{
		Real lastRight = 0.0f, lastY = -1.0f, lastBottom = 0.0f;
		for( Int slot = 0; slot < count; ++slot )
		{
			Real x, y, w, h;
			OnlineSkinsCardPlace( slot, count, SKINS_W, SKINS_H, x, y, w, h );
			CHECK( w > 0.0f && h > 0.0f );
			CHECK( x + w <= SKINS_W && y + h <= SKINS_H );
			if( y == lastY )
				CHECK_EQ( (Int)x, (Int)( lastRight + ONLINE_SKINS_CARD_GAP ) );		// 16 apart along a row
			else
			{
				CHECK_EQ( (Int)x, 0 );
				CHECK( slot == 0 || (Int)y == (Int)( lastBottom + ONLINE_SKINS_CARD_GAP ) );
				// the row before ended on the right edge exactly: no near-miss edges between rows
				CHECK( slot == 0 || (Int)lastRight == (Int)SKINS_W );
			}
			lastRight = x + w;
			lastY = y;
			lastBottom = y + h;
		}
		CHECK_EQ( (Int)lastRight, (Int)SKINS_W );
	}
}
