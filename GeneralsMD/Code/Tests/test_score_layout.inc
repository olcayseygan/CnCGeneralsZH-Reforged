// The end-of-match screens' rects in the menu language (EndScreenLayout.h): the Summary's cut for 2 to
// 32 players against README-end3.md's table, every team split fitting over the foot, the standings'
// columns, the key row and the foot's middle, the build order's folded axis and its lanes, and the
// Direct Strike rows.  Included into test_gameengine.cpp after test_score_screen.inc, whose
// scoreSplits it walks.

#include "GameClient/EndScreenLayout.h"

/// the Summary of these team sizes, a free for all when every team is one player and there are three or more
static EndScreen::SummaryPlan endPlan( const std::vector< Int > &sizes )
{
	Int players = 0, biggest = 0;
	for( size_t t = 0; t < sizes.size(); t++ )
	{
		players += sizes[ t ];
		biggest = max( biggest, sizes[ t ] );
	}
	return EndScreen::summaryPlan( sizes, players >= 3 && biggest == 1 );
}

/// how much of the content area one wing of the plan takes
static Int endWingHeight( const EndScreen::SummaryPlan &plan, const std::vector< Int > &sizes, Int wing )
{
	Int used = plan.head;
	for( size_t t = 0; t < sizes.size(); t++ )
	{
		if( plan.wing[ t ] != wing )
			continue;
		used += sizes[ t ] * plan.rowHeight;
		if( plan.kind == EndScreen::ROWS_BANDED && EndScreen::bandedHasBand( sizes[ t ] ) )
			used += plan.bandHeight;
		if( plan.totals )
			used += EndScreen::TOTALS;
	}
	return used;
}

TEST(end_screen_summary_rows_match_the_design_table)
{
	// README-end3.md, "Summary": rows and feet for one to sixteen a team, 8 x 4, 16 x 2 and 32 alone
	struct Case { Int teams, size; EndScreen::RowKind kind; Int foot, rowHeight, bandHeight; };
	const Case cases[] =
	{
		{ 2, 1, EndScreen::ROWS_CARD, EndScreen::FOOT_WIDE, 540, 0 },
		{ 2, 2, EndScreen::ROWS_CARD, EndScreen::FOOT_WIDE, 252, 0 },
		{ 2, 3, EndScreen::ROWS_CARD, EndScreen::FOOT_LINE, 210, 0 },
		{ 2, 4, EndScreen::ROWS_CARD, EndScreen::FOOT_LINE, 158, 0 },
		{ 2, 16, EndScreen::ROWS_LINE, EndScreen::FOOT_LINE, 39, 0 },
		{ 8, 4, EndScreen::ROWS_BANDED, EndScreen::FOOT_LINE, 36, EndScreen::BAND },
		{ 16, 2, EndScreen::ROWS_BANDED, EndScreen::FOOT_LINE, 31, EndScreen::BAND_TIGHT },
		{ 32, 1, EndScreen::ROWS_PLACES, EndScreen::FOOT_LINE, 45, 0 },
	};
	for( Int each = 0; each < ARRAY_SIZE( cases ); each++ )
	{
		const std::vector< Int > sizes( cases[ each ].teams, cases[ each ].size );
		const EndScreen::SummaryPlan plan = endPlan( sizes );
		CHECK_EQ( (Int)plan.kind, (Int)cases[ each ].kind );
		CHECK_EQ( plan.foot, cases[ each ].foot );
		CHECK_EQ( plan.rowHeight, cases[ each ].rowHeight );
		if( plan.kind == EndScreen::ROWS_BANDED )
			CHECK_EQ( plan.bandHeight, cases[ each ].bandHeight );
		// two teams stand one a wing; several and a free for all split their rows evenly
		CHECK_EQ( plan.rows[ 0 ], plan.rows[ 1 ] );
	}
	// a free for all's first half of the places on the left
	const EndScreen::SummaryPlan ffa = endPlan( std::vector< Int >( 5, 1 ) );
	CHECK_EQ( ffa.rows[ 0 ], 3 );
	CHECK_EQ( ffa.rows[ 1 ], 2 );
	CHECK_EQ( ffa.wing[ 2 ], 0 );
	CHECK_EQ( ffa.wing[ 3 ], 1 );
	// one team alone (an Apocalypse run) takes the whole width as cards
	const EndScreen::SummaryPlan alone = endPlan( std::vector< Int >( 1, 4 ) );
	CHECK( alone.single );
	CHECK_EQ( (Int)alone.kind, (Int)EndScreen::ROWS_CARD );
	CHECK_EQ( alone.rows[ 1 ], 0 );
	// a player alone in a match of teams is his own team's line, with no band over him
	std::vector< Int > lone;
	lone.push_back( 2 );
	for( Int each = 0; each < 30; each++ )
		lone.push_back( 1 );
	const EndScreen::SummaryPlan loners = endPlan( lone );
	CHECK_EQ( (Int)loners.kind, (Int)EndScreen::ROWS_BANDED );
	CHECK_EQ( loners.bands[ 0 ] + loners.bands[ 1 ], 1 );
	// a small first team does not push everyone else into the right wing
	std::vector< Int > lopsided;
	lopsided.push_back( 1 );
	lopsided.push_back( 16 );
	lopsided.push_back( 15 );
	const EndScreen::SummaryPlan cut = endPlan( lopsided );
	CHECK_EQ( cut.wing[ 1 ], 0 );
	CHECK_EQ( cut.wing[ 2 ], 1 );
}

TEST(end_screen_every_split_fits_over_the_foot)
{
	// every split of 2 to 8 players and of 16, 24 and 32 into teams of at most sixteen: each team in a
	// wing, both wings inside the content area over the foot, line rows never under 24 px, cards never
	// under 150
	const Int counts[] = { 2, 3, 4, 5, 6, 7, 8, 16, 24, 32 };
	Int splitsSeen = 0;
	for( Int c = 0; c < ARRAY_SIZE( counts ); c++ )
	{
		std::vector< std::vector< Int > > splits;
		std::vector< Int > sizes;
		scoreSplits( counts[ c ], min( counts[ c ], 16 ), sizes, splits );
		for( size_t s = 0; s < splits.size(); s++ )
		{
			splitsSeen++;
			const EndScreen::SummaryPlan plan = endPlan( splits[ s ] );
			const Int room = EndScreen::footTop( plan.foot ) - EndScreen::CONTENT_TOP;
			CHECK_EQ( (Int)plan.wing.size(), (Int)splits[ s ].size() );
			Int listed = 0;
			for( Int w = 0; w < 2; w++ )
			{
				CHECK( endWingHeight( plan, splits[ s ], w ) <= room );
				listed += plan.rows[ w ];
			}
			CHECK_EQ( listed, counts[ c ] );
			if( plan.kind == EndScreen::ROWS_CARD )
				CHECK( plan.rowHeight >= 150 );
			else
				CHECK( plan.rowHeight >= 24 && plan.rowHeight <= EndScreen::LINE_ROW_MAX );
			// the wings stay in standings order: once a team goes right, no later one comes back left
			for( size_t t = 1; t < plan.wing.size(); t++ )
				CHECK( plan.wing[ t ] >= plan.wing[ t - 1 ] );
		}
	}
	CHECK( splitsSeen > 5000 );
}

TEST(end_screen_columns_stand_inside_their_wing)
{
	for( Int dense = 0; dense < 2; dense++ )
	{
		Int left[ EndScreen::COLUMNS ], right[ EndScreen::COLUMNS ];
		EndScreen::summaryColumns( EndScreen::WING_LEFT_X1, dense != 0, left, right );
		CHECK_EQ( right[ EndScreen::COLUMNS - 1 ], EndScreen::WING_LEFT_X1 - 8 );
		for( Int col = 0; col < EndScreen::COLUMNS; col++ )
		{
			CHECK( left[ col ] < right[ col ] );
			if( col > 0 )
				CHECK( right[ col - 1 ] < left[ col ] );
		}
		// the names keep room left of the score: 240 px past the wing's edge, 280 when dense
		CHECK( left[ 0 ] - EndScreen::WING_LEFT_X0 >= ( dense ? 280 : 240 ) );
	}
	// a card's picture never pushes the score column off its figures: the tallest card's picture and
	// its gap end left of the units
	Int left[ EndScreen::COLUMNS ], right[ EndScreen::COLUMNS ];
	EndScreen::summaryColumns( EndScreen::WING_LEFT_X1, FALSE, left, right );
	CHECK_EQ( EndScreen::cardPicture( 540 ), 180 );
	CHECK_EQ( EndScreen::cardPicture( 158 ), 138 );
	CHECK( EndScreen::WING_LEFT_X0 + 12 + EndScreen::cardPicture( 540 ) + 18 < left[ 1 ] - 60 );
}

TEST(end_screen_keys_sit_right_to_left_without_touching)
{
	Int lastLeft = EndScreen::INSET_RIGHT + EndScreen::KEY_GAP;
	// four keys (Continue, Exit, Save Replay, Chat) stay clear of the left wing's edge
	for( Int index = 0; index < 4; index++ )
	{
		Int x0, x1;
		EndScreen::keyRect( index, x0, x1 );
		CHECK_EQ( x1 - x0, index == 0 ? EndScreen::KEY_PRIMARY : EndScreen::KEY_SECONDARY );
		CHECK_EQ( x1, lastLeft - EndScreen::KEY_GAP );
		lastLeft = x0;
	}
	CHECK( lastLeft > EndScreen::WING_RIGHT_X0 );
	CHECK_EQ( EndScreen::legendLeft( 16 ), 1440 );
	CHECK_EQ( EndScreen::legendLeft( 17 ), 1240 );
	// the key row stands inside both feet, and the foot's middle keeps room between the map and five keys
	CHECK( EndScreen::footTop( EndScreen::FOOT_LINE ) + EndScreen::FOOT_PAD <= EndScreen::KEYS_TOP );
	CHECK( EndScreen::KEYS_BOTTOM <= EndScreen::FOOT_BOTTOM );
	CHECK_EQ( EndScreen::footMap( EndScreen::FOOT_LINE ), 112 );
	CHECK_EQ( EndScreen::footMap( EndScreen::FOOT_WIDE ), 240 );
	CHECK_EQ( EndScreen::footMiddleLeft( EndScreen::FOOT_LINE ), 372 );
	CHECK_EQ( EndScreen::footMiddleLeft( EndScreen::FOOT_WIDE ), 550 );
	for( Int keys = 1; keys <= 5; keys++ )
		CHECK( EndScreen::footMiddleRight( keys ) - EndScreen::footMiddleLeft( EndScreen::FOOT_WIDE ) >= 300 );
	// the Graphs page's plot and Build order's tracks stand over the page's foot rule
	CHECK( EndScreen::PLOT_BOTTOM + 22 < EndScreen::PAGE_FOOT_RULE );
	CHECK( EndScreen::TRACKS_BOTTOM < EndScreen::PAGE_FOOT_RULE );
}

TEST(end_screen_build_axis_folds_where_nobody_built)
{
	const UnsignedInt fps = 30;
	// the axis ends a minute boundary past the last build with 30 s of room
	CHECK_EQ( EndScreen::buildAxisEnd( 18 * 60 * fps, fps, 30 ), 19u * 60 * fps );
	CHECK_EQ( EndScreen::buildAxisEnd( 18 * 60 * fps + 40 * fps, fps, 30 ), 20u * 60 * fps );
	CHECK_EQ( EndScreen::buildAxisEnd( 0, fps, 30 ), 60u * fps );

	// builds every 30 s through six minutes, nothing until ten, then every 30 s to eighteen
	std::vector< UnsignedInt > builds;
	for( UnsignedInt s = 10; s <= 360; s += 30 )
		builds.push_back( s * fps );
	for( UnsignedInt s = 600; s <= 1080; s += 30 )
		builds.push_back( s * fps );
	const UnsignedInt end = EndScreen::buildAxisEnd( builds.back(), fps, 30 );
	const Int left = 300, width = 1500, fold = 34;
	const std::vector< EndScreen::AxisStretch > axis = EndScreen::foldAxis( builds, end, 20 * fps, 40 * fps, fold, left, width );
	CHECK_EQ( (Int)axis.size(), 2 );
	CHECK_EQ( axis[ 0 ].from, 0u );
	CHECK_EQ( axis[ 1 ].to, end );
	// the quiet stretch 6:00 to 9:40 is gone (the last early build at 5:40), folded to 34 px
	CHECK_EQ( axis[ 0 ].to, ( 340 + 20 ) * fps );
	CHECK_EQ( axis[ 1 ].from, ( 600 - 20 ) * fps );
	CHECK_NEAR( axis[ 1 ].x0 - axis[ 0 ].x1, (Real)fold, 0.01f );
	// the width is filled exactly, and every kept stretch runs at one scale
	CHECK_NEAR( axis.back().x1, (Real)( left + width ), 0.5f );
	const Real scale0 = ( axis[ 0 ].x1 - axis[ 0 ].x0 ) / (Real)( axis[ 0 ].to - axis[ 0 ].from );
	const Real scale1 = ( axis[ 1 ].x1 - axis[ 1 ].x0 ) / (Real)( axis[ 1 ].to - axis[ 1 ].from );
	CHECK_NEAR( scale0, scale1, 0.0001f );
	// frames map left to right; one inside the fold lands in the fold's middle and is not kept
	Real last = -1.0f;
	for( UnsignedInt frame = 0; frame <= end; frame += 15 * fps )
	{
		const Real x = EndScreen::axisX( axis, frame );
		CHECK( x >= last );
		CHECK( x >= (Real)left - 0.01f && x <= (Real)( left + width ) + 0.5f );
		last = x;
	}
	CHECK_NEAR( EndScreen::axisX( axis, 480 * fps ), ( axis[ 0 ].x1 + axis[ 1 ].x0 ) * 0.5f, 0.01f );
	CHECK( !EndScreen::axisKeeps( axis, 480 * fps ) );
	CHECK( EndScreen::axisKeeps( axis, 300 * fps ) );

	// builds closer than the quiet span are joined: one stretch, no fold, the plain linear axis
	std::vector< UnsignedInt > steady;
	for( UnsignedInt s = 0; s <= 600; s += 50 )
		steady.push_back( s * fps );
	const std::vector< EndScreen::AxisStretch > linear = EndScreen::foldAxis( steady, 660 * fps, 20 * fps, 40 * fps, fold, 0, 660 );
	CHECK_EQ( (Int)linear.size(), 1 );
	CHECK_NEAR( EndScreen::axisX( linear, 330 * fps ), 330.0f, 0.5f );
	// no builds at all: the start and the end alone, a minute apart, joined
	CHECK_EQ( (Int)EndScreen::foldAxis( std::vector< UnsignedInt >(), 60 * fps, 20 * fps, 40 * fps, fold, 0, 600 ).size(), 1 );
}

TEST(end_screen_build_pictures_keep_their_true_time)
{
	// three lanes of 40 px pictures: the first three at one moment take a lane each, the fourth overlaps
	// the lane that ends soonest at its own x, never pushed right
	std::vector< Int > lanes( 3, -1000 );
	CHECK_EQ( EndScreen::buildLane( lanes, 100, 40, 3 ), 0 );
	CHECK_EQ( EndScreen::buildLane( lanes, 100, 40, 3 ), 1 );
	CHECK_EQ( EndScreen::buildLane( lanes, 110, 40, 3 ), 2 );
	CHECK_EQ( EndScreen::buildLane( lanes, 120, 40, 3 ), 0 );
	CHECK_EQ( lanes[ 0 ], 163 );
	// once clear, the first lane again
	CHECK_EQ( EndScreen::buildLane( lanes, 400, 40, 3 ), 0 );
}

TEST(end_screen_direct_strike_rows_fit_over_the_timeline)
{
	CHECK_EQ( EndScreen::directStrikeRowHeight( 1 ), 128 );
	CHECK_EQ( EndScreen::directStrikeRowHeight( 3 ), 104 );
	// three seats and the head leave the timeline at least 300 px over the key row, one seat 540
	CHECK( EndScreen::DS_TIMELINE_BOTTOM - EndScreen::directStrikeTimelineTop( 3 ) >= 300 );
	CHECK( EndScreen::DS_TIMELINE_BOTTOM - EndScreen::directStrikeTimelineTop( 1 ) >= 400 );
	CHECK( EndScreen::DS_TIMELINE_BOTTOM < EndScreen::PAGE_FOOT_RULE );
	// the seven figures stand inside each wing, right of the tallest row's picture
	const Int wings[ 2 ][ 2 ] = { { EndScreen::WING_LEFT_X0, EndScreen::WING_LEFT_X1 }, { EndScreen::WING_RIGHT_X0, EndScreen::WING_RIGHT_X1 } };
	for( Int w = 0; w < 2; w++ )
	{
		Int left[ EndScreen::DS_COLUMNS ], right[ EndScreen::DS_COLUMNS ];
		EndScreen::directStrikeColumns( wings[ w ][ 1 ], left, right );
		CHECK_EQ( right[ EndScreen::DS_COLUMNS - 1 ], wings[ w ][ 1 ] - 8 );
		for( Int col = 1; col < EndScreen::DS_COLUMNS; col++ )
			CHECK( right[ col - 1 ] < left[ col ] );
		CHECK( left[ 0 ] > wings[ w ][ 0 ] + 8 + EndScreen::directStrikeRowHeight( 1 ) - 16 + 16 );
	}
}
