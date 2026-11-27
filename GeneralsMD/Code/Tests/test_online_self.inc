// The ladder's rows and the skins page's cards (docs/plans/MENU-REDESIGN.md, package 11; the design is
// experiments/proofs/2026-11-24/screens/online-self/README-online-self.md in the wrapper).
// OnlineSelfLayout.h, which WOLLadderScreen.cpp places its windows by.  Included into test_gameengine.cpp.

#include "GameClient/OnlineSelfLayout.h"

TEST( online_self_ladder_rows_fit_the_table_pinned_or_not )
{
	// README: 20 rows of 33 from 262; pinned, 20 of 30, a 20 gap and his 48 row, all inside the box's 946
	CHECK_EQ( (Int)( ONLINE_LADDER_ROWS_TOP + 20 * OnlineLadderRowHeight( FALSE ) ), 922 );
	const Real pinnedEnd = ONLINE_LADDER_ROWS_TOP + 20 * OnlineLadderRowHeight( TRUE ) + ONLINE_LADDER_PINNED_GAP
		+ ONLINE_LADDER_PINNED_HEIGHT;
	CHECK_EQ( (Int)pinnedEnd, 930 );
	CHECK( pinnedEnd <= ONLINE_LADDER_BOX_BOTTOM - 8.0f );
}

/// the cards of count laid in the skins page's 832 by 668: per row, and every card checked
static Int skinsRows( Int count, Int perRow[ 8 ] )
{
	Int rows = 0;
	Real lastY = -1.0f;
	for( Int slot = 0; slot < count; ++slot )
	{
		Real x, y, w, h;
		OnlineSkinsCardPlace( slot, count, 832.0f, 668.0f, x, y, w, h );
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
	OnlineSkinsCardPlace( 1, 2, 832.0f, 668.0f, x, y, w, h );
	CHECK_EQ( (Int)w, 832 );
	CHECK_EQ( (Int)( y + h ), 668 );
	// a slot past the count has no place
	OnlineSkinsCardPlace( 2, 2, 832.0f, 668.0f, x, y, w, h );
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
			OnlineSkinsCardPlace( slot, count, 832.0f, 668.0f, x, y, w, h );
			CHECK( w > 0.0f && h > 0.0f );
			CHECK( x + w <= 832.0f && y + h <= 668.0f );
			if( y == lastY )
				CHECK_EQ( (Int)x, (Int)( lastRight + ONLINE_SKINS_CARD_GAP ) );		// 12 apart along a row
			else
			{
				CHECK_EQ( (Int)x, 0 );
				CHECK( slot == 0 || y >= lastBottom + ONLINE_SKINS_CARD_GAP );
				// the row before ended within its cards' rounding of the right edge
				CHECK( slot == 0 || lastRight >= 832.0f - 4.0f );
			}
			lastRight = x + w;
			lastY = y;
			lastBottom = y + h;
		}
		CHECK( lastRight >= 832.0f - 4.0f );
	}
}
