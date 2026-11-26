// The load board's layout in the menu language (README-loading.md beside loading.py under the
// wrapper's experiments/proofs/2026-11-24/screens/loading): the wings from one player to thirty-two,
// the middle's stack for every mix of versus, map shape, rules and watchers, the nights' band, the
// clocks at the match's speed, the transfer's rows; and the team numbers the load board and the score
// screen share.  Included into test_gameengine.cpp.

#include "GameClient/LoadScreenLayout.h"
#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/UIStyle.h"

TEST(load_screen_and_score_screen_call_teams_by_one_number)
{
	// lobby teams 2 and 1, a player alone, lobby team 2 again (allies broken up), another alone:
	// the first two keep their lobby numbers, the rest take the lowest free ones in seat order
	std::vector< Int > shared, first;
	const Int lobby[ 5 ] = { 1, 0, -1, 1, -1 };
	for( Int t = 0; t < 5; t++ )
	{
		shared.push_back( lobby[ t ] );
		first.push_back( t );
	}
	const std::vector< Int > number = ScoreScreen_teamNumbers( shared, first, 33 );
	CHECK_EQ( (Int)number.size(), 5 );
	CHECK_EQ( number[ 0 ], 2 );
	CHECK_EQ( number[ 1 ], 1 );
	CHECK_EQ( number[ 2 ], 3 );
	CHECK_EQ( number[ 3 ], 4 );
	CHECK_EQ( number[ 4 ], 5 );
	// a lobby number past the highest is no lobby number
	std::vector< Int > high( 1, 40 ), zero( 1, 0 );
	CHECK_EQ( ScoreScreen_teamNumbers( high, zero, 33 )[ 0 ], 1 );

	// a 2v1v1: seats 0 and 1 lobby team 1, seats 2 and 3 alone.  The load board lists the teams in
	// seat order, the score screen in standings order (seat 3 won, then the pair, then seat 2):
	// each team gets one number on both
	std::vector< Int > loadShared, loadFirst;
	loadShared.push_back( 0 );
	loadFirst.push_back( 0 );
	loadShared.push_back( -1 );
	loadFirst.push_back( 2 );
	loadShared.push_back( -1 );
	loadFirst.push_back( 3 );
	const std::vector< Int > onLoad = ScoreScreen_teamNumbers( loadShared, loadFirst, 33 );
	const Int standings[ 3 ] = { 2, 0, 1 };	// the score screen's rows as load board teams
	std::vector< Int > scoreShared, scoreFirst;
	for( Int row = 0; row < 3; row++ )
	{
		scoreShared.push_back( loadShared[ standings[ row ] ] );
		scoreFirst.push_back( loadFirst[ standings[ row ] ] );
	}
	const std::vector< Int > onScore = ScoreScreen_teamNumbers( scoreShared, scoreFirst, 33 );
	for( Int row = 0; row < 3; row++ )
		CHECK_EQ( onScore[ row ], onLoad[ standings[ row ] ] );
	CHECK_EQ( onLoad[ 0 ], 1 );
	CHECK_EQ( onLoad[ 1 ], 2 );
	CHECK_EQ( onLoad[ 2 ], 3 );
}

TEST(load_screen_wing_rows_match_the_design_table)
{
	// README-loading.md's "Row sizes by player count", row by row
	LoadWingPlan plan = LoadScreen_planWing( 1, 1, TRUE );		// 1 v 1
	CHECK_EQ( (Int)plan.row, (Int)LOAD_ROW_POSTER );
	CHECK_EQ( plan.rowPitch, 692 );
	plan = LoadScreen_planWing( 1, 2, FALSE );
	CHECK_EQ( (Int)plan.row, (Int)LOAD_ROW_CARD );
	CHECK_EQ( plan.rowPitch, 346 );
	plan = LoadScreen_planWing( 2, 2, TRUE );								// a 2 v 1 v 1's right wing
	CHECK_EQ( plan.rowPitch, 312 );
	plan = LoadScreen_planWing( 1, 3, FALSE );
	CHECK_EQ( plan.rowPitch, 230 );
	plan = LoadScreen_planWing( 1, 4, FALSE );							// a 4 v 4
	CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_CARD );
	CHECK_EQ( (Int)plan.row, (Int)LOAD_ROW_CARD );
	CHECK_EQ( plan.rowPitch, 173 );
	plan = LoadScreen_planWing( 2, 4, FALSE );
	CHECK_EQ( plan.rowPitch, 156 );
	plan = LoadScreen_planWing( 1, 5, FALSE );							// five a team: lines
	CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_LINE );
	CHECK_EQ( (Int)plan.row, (Int)LOAD_ROW_LINE );
	CHECK_EQ( plan.rowPitch, 88 );
	plan = LoadScreen_planWing( 1, 8, FALSE );
	CHECK_EQ( plan.rowPitch, 87 );
	plan = LoadScreen_planWing( 2, 8, FALSE );
	CHECK_EQ( plan.rowPitch, 80 );
	plan = LoadScreen_planWing( 2, 16, FALSE );							// 8 v 8 v 8 v 8
	CHECK_EQ( plan.rowPitch, 40 );
	plan = LoadScreen_planWing( 8, 16, FALSE );							// sixteen teams of two
	CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_RULE );
	CHECK_EQ( plan.rowPitch, 40 );
	plan = LoadScreen_planWing( 16, 16, TRUE );							// a free-for-all of 32
	CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_NONE );
	CHECK_EQ( plan.rowPitch, 46 );
}

TEST(load_screen_wings_fit_the_body_from_one_to_thirty_two)
{
	const Int body = LOAD_BODY_BOTTOM - LOAD_BODY_TOP;
	for( Int rows = 1; rows <= 32; rows++ )
	{
		for( Int teams = 1; teams <= rows; teams++ )
		{
			// every team one player is what the board calls alone, and nothing else is
			for( Int alone = 0; alone < 2; alone++ )
			{
				if( ( alone == 1 ) != ( teams == rows ) )
					continue;
				const LoadWingPlan plan = LoadScreen_planWing( teams, rows, alone == 1 );
				// nothing leaves the body, and the plan's own sum is what it says
				CHECK( plan.used <= body );
				CHECK_EQ( plan.used, plan.headHeight * teams + plan.rowPitch * rows + plan.gap * ( teams - 1 ) );
				CHECK( plan.rowHeight > 0 && plan.rowHeight < plan.rowPitch );
				// a card holds a human's four lines; a poster its portrait over them
				if( plan.row != LOAD_ROW_LINE )
				{
					CHECK( plan.rowPitch >= LOAD_CARD_LEAST );
					CHECK( plan.rowHeight >= LoadScreen_cardText( TRUE, LOAD_PITCH_BAR ) );
				}
				else
					CHECK( plan.rowPitch <= LOAD_LINE_MOST );
				if( plan.row == LOAD_ROW_POSTER )
				{
					const Int text = LoadScreen_cardText( TRUE, LOAD_PITCH_BAR );
					const Int side = LoadScreen_posterPortrait( LOAD_WING_WIDTH, plan.rowHeight, text, FALSE );
					CHECK( side > 0 && 8 + side + 20 + text <= plan.rowHeight );
					CHECK( side <= LOAD_WING_WIDTH - 36 );
				}
				// sixteen rows to a wing still read, however their teams split: an 18 px name on a row
				// of 32 or more (40 in the design's two cases, 8 v 8 v 8 v 8 and sixteen pairs)
				if( rows <= 16 )
					CHECK( plan.rowPitch >= 32 );
				// the heads: none for a free-for-all, a rule past four teams, else a card's or a line's
				if( alone && teams > 2 )
					CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_NONE );
				else if( teams > 4 )
					CHECK_EQ( (Int)plan.head, (Int)LOAD_HEAD_RULE );
				else
					CHECK_EQ( (Int)plan.head, plan.row == LOAD_ROW_LINE ? (Int)LOAD_HEAD_LINE : (Int)LOAD_HEAD_CARD );
			}
		}
	}
	// a card's portrait stands inside its row and never past 300
	for( Int height = 1; height < 800; height += 7 )
	{
		const Int side = LoadScreen_cardPortrait( height );
		CHECK( side >= 1 && side <= LOAD_PORTRAIT_MOST );
		if( height > 17 )
			CHECK( side <= height - 16 );
	}
	// a plain side's picture is small art and stops at 360 on a poster
	CHECK_EQ( LoadScreen_posterPortrait( LOAD_WING_WIDTH, 692 - 8, LoadScreen_cardText( FALSE, 0 ), TRUE ), LOAD_PLAIN_PORTRAIT_MOST );
}

TEST(load_screen_teams_go_whole_to_the_lighter_wing)
{
	// two teams face each other, whatever their sizes
	std::vector< Int > sizes;
	sizes.push_back( 1 );
	sizes.push_back( 7 );
	std::vector< Int > wing = LoadScreen_wingOfTeams( sizes );
	CHECK_EQ( wing[ 0 ], 0 );
	CHECK_EQ( wing[ 1 ], 1 );
	// 2 v 1 v 1: the pair left, the two alone right
	sizes.clear();
	sizes.push_back( 2 );
	sizes.push_back( 1 );
	sizes.push_back( 1 );
	wing = LoadScreen_wingOfTeams( sizes );
	CHECK_EQ( wing[ 0 ], 0 );
	CHECK_EQ( wing[ 1 ], 1 );
	CHECK_EQ( wing[ 2 ], 1 );
	// 8 v 8 v 8 v 8: two teams a wing, sixteen rows each
	sizes.assign( 4, 8 );
	wing = LoadScreen_wingOfTeams( sizes );
	Int rows[ 2 ] = { 0, 0 };
	for( Int t = 0; t < 4; t++ )
		rows[ wing[ t ] ] += sizes[ t ];
	CHECK_EQ( rows[ 0 ], 16 );
	CHECK_EQ( rows[ 1 ], 16 );
	// a free-for-all of 32 splits sixteen and sixteen
	sizes.assign( 32, 1 );
	wing = LoadScreen_wingOfTeams( sizes );
	rows[ 0 ] = rows[ 1 ] = 0;
	for( Int t = 0; t < 32; t++ )
		rows[ wing[ t ] ]++;
	CHECK_EQ( rows[ 0 ], 16 );
	CHECK_EQ( rows[ 1 ], 16 );
}

TEST(load_screen_middle_stack_stays_in_the_body)
{
	const Real aspects[ 6 ] = { 0.4f, 0.75f, 1.0f, 370.0f / 190.0f, 2.5f, 4.0f };
	for( Int versus = 0; versus < 2; versus++ )
	{
		for( Int a = 0; a < 6; a++ )
		{
			for( Int well = 0; well < 2; well++ )
			{
				for( Int rules = 0; rules <= 9; rules++ )
				{
					for( Int observers = 0; observers < 2; observers++ )
					{
						for( Int nights = 0; nights < 2; nights++ )
						{
							const LoadMiddlePlan plan = LoadScreen_planMiddle( versus == 1, aspects[ a ], well == 1, rules, observers == 1, nights == 1 );
							CHECK( plan.bottom <= LOAD_BODY_BOTTOM );
							CHECK( plan.map.lo.x >= LOAD_MIDDLE_LEFT && plan.map.hi.x <= LOAD_MIDDLE_RIGHT );
							CHECK( plan.map.hi.x - plan.map.lo.x <= LOAD_MAP_LARGEST );
							CHECK( plan.map.hi.y - plan.map.lo.y <= LOAD_MAP_LARGEST );
							CHECK( plan.map.hi.y > plan.map.lo.y );
							// VS stands over the map, never on it
							if( versus )
								CHECK( plan.vsTop >= 0 && plan.vsTop + 50 <= plan.map.lo.y );
							else
								CHECK_EQ( plan.vsTop, -1 );
							// a well is the column's width; a preview keeps its shape within a pixel
							if( well )
								CHECK_EQ( plan.map.hi.x - plan.map.lo.x, LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT );
							else
							{
								const Real drawn = (Real)( plan.map.hi.x - plan.map.lo.x ) / (Real)( plan.map.hi.y - plan.map.lo.y );
								CHECK( drawn / aspects[ a ] > 0.97f && drawn / aspects[ a ] < 1.03f );
							}
							Int y = plan.map.hi.y;
							if( nights )
							{
								CHECK( plan.nightsTop >= y );
								y = plan.nightsTop + LOAD_NIGHTS_HEIGHT;
							}
							if( rules > 0 )
							{
								// nine rules and the watchers under a tall map keep a row of 30; under the
								// nights, Apocalypse's four keep one of 30 too
								CHECK( plan.rulesTop >= y );
								CHECK( plan.rulePitch <= LOAD_RULE_PITCH );
								if( !nights || rules <= 4 )
									CHECK( plan.rulePitch >= 30 );
								y = plan.rulesTop + LOAD_SECTION_HEAD + rules * plan.rulePitch;
							}
							else
								CHECK_EQ( plan.rulesTop, -1 );
							if( observers )
							{
								CHECK( plan.observersTop >= y );
								y = plan.observersTop + LOAD_SECTION_HEAD + 30;
							}
							CHECK_EQ( plan.bottom, y );
						}
					}
				}
			}
		}
	}
	// Iron Dragon's 4 v 4 with eight rules and two watchers: the map the column's width
	const LoadMiddlePlan iron = LoadScreen_planMiddle( TRUE, 370.0f / 190.0f, FALSE, 8, TRUE, FALSE );
	CHECK_EQ( iron.map.hi.x - iron.map.lo.x, LOAD_MAP_LARGEST );
	CHECK_EQ( iron.map.lo.y, 192 );
	CHECK_EQ( iron.rulePitch, LOAD_RULE_PITCH );
}

TEST(load_screen_apocalypse_nights_and_clocks)
{
	// the band: every phase at least its floor, the four filling the column exactly
	const Int seconds[ 4 ] = { 240, 45, 120, 30 };
	Int widths[ 4 ];
	LoadScreen_nightWidths( LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT, seconds, LOAD_NIGHT_LEAST, widths );
	Int sum = 0;
	for( Int each = 0; each < 4; each++ )
	{
		CHECK( widths[ each ] >= LOAD_NIGHT_LEAST );
		sum += widths[ each ];
	}
	CHECK_EQ( sum, LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT );
	CHECK( widths[ 2 ] > widths[ 1 ] && widths[ 0 ] > widths[ 2 ] );

	// game seconds on the wall clock at the match's speed: the design's 1.5x figures, and retail's own
	CHECK_EQ( LoadScreen_wallSeconds( 240, 150 ), 160 );
	CHECK_EQ( LoadScreen_wallSeconds( 45, 150 ), 30 );
	CHECK_EQ( LoadScreen_wallSeconds( 120, 150 ), 80 );
	CHECK_EQ( LoadScreen_wallSeconds( 30, 150 ), 20 );
	CHECK_EQ( LoadScreen_wallSeconds( 60, 150 ), 40 );
	CHECK_EQ( LoadScreen_wallSeconds( 20, 150 ), 13 );
	CHECK_EQ( LoadScreen_wallSeconds( 240, 100 ), 240 );
	CHECK_EQ( LoadScreen_wallSeconds( 240, 200 ), 120 );
	CHECK_EQ( LoadScreen_wallSeconds( 45, 0 ), 45 );
}

TEST(load_screen_mission_count_follows_the_chain)
{
	// GEN_USA: Mission01 to Mission07 in order; GEN_GLA skips Mission03, seven missions numbered to 08
	const char *const usa[ 7 ] = { "mission01", "mission02", "mission03", "mission04", "mission05", "mission06", "mission07" };
	const char *const gla[ 7 ] = { "mission01", "mission02", "mission04", "mission05", "mission06", "mission07", "mission08" };
	CHECK_EQ( LoadScreen_missionPlace( usa, 7, "mission01" ), 1 );
	CHECK_EQ( LoadScreen_missionPlace( usa, 7, "mission07" ), 7 );
	CHECK_EQ( LoadScreen_missionPlace( gla, 7, "mission04" ), 3 );
	CHECK_EQ( LoadScreen_missionPlace( gla, 7, "mission08" ), 7 );
	// a mission off the chain has no place, so the count and its pips are not drawn
	CHECK_EQ( LoadScreen_missionPlace( gla, 7, "mission03" ), 0 );
	CHECK_EQ( LoadScreen_missionPlace( usa, 0, "mission01" ), 0 );
}

TEST(load_screen_transfer_rows_fit_the_body)
{
	// two columns of up to sixteen humans each, at most 76 a row, never past the body
	const Int top = LOAD_BODY_TOP + 4 + LOAD_SECTION_HEAD;
	for( Int rows = 1; rows <= 16; rows++ )
	{
		const Int pitch = LoadScreen_transferRow( rows );
		CHECK( pitch <= LOAD_TRANSFER_ROW_MOST );
		CHECK( top + rows * pitch <= LOAD_BODY_BOTTOM );
		CHECK( pitch >= 40 );
	}
	CHECK_EQ( LoadScreen_transferRow( 9 ), LOAD_TRANSFER_ROW_MOST );
}

TEST(load_screen_board_sits_on_frame_a)
{
	// the board's content inside frame A's panel at every shape, the wings and the middle apart
	CHECK( LOAD_LEFT > UIStyle::MENU_PANEL_LEFT && LOAD_RIGHT < UIStyle::MENU_PANEL_RIGHT );
	CHECK( LOAD_HEADER_RULE > UIStyle::MENU_PANEL_TOP && LOAD_BAR_BOTTOM < UIStyle::MENU_PANEL_BOTTOM );
	CHECK( LOAD_LEFT + LOAD_WING_WIDTH < LOAD_MIDDLE_LEFT );
	CHECK( LOAD_MIDDLE_RIGHT < LOAD_RIGHT - LOAD_WING_WIDTH );
	CHECK( LOAD_BODY_BOTTOM < LOAD_FOOT_RULE && LOAD_TIP_BOTTOM < LOAD_BAR_TOP );
	// the header and the content's edges are every converted screen's, so nothing jumps between them
	CHECK_EQ( LOAD_LEFT, UIStyle::MENU_INSET );
	CHECK_EQ( LOAD_RIGHT, UIStyle::MENU_PANEL_RIGHT - 32 );
	CHECK_EQ( LOAD_TITLE_TOP, UIStyle::MENU_TITLE_TOP );
	CHECK_EQ( LOAD_TITLE_BOTTOM, UIStyle::MENU_TITLE_BOTTOM );
	CHECK_EQ( LOAD_HEADER_RULE, UIStyle::MENU_HEADER_RULE );
	// the middle sits the same distance from either wing
	CHECK_EQ( LOAD_MIDDLE_LEFT - ( LOAD_LEFT + LOAD_WING_WIDTH ), ( LOAD_RIGHT - LOAD_WING_WIDTH ) - LOAD_MIDDLE_RIGHT );
	// the transfer's two columns stand flush with the content's edges
	CHECK( LOAD_LEFT + LOAD_TRANSFER_WIDTH < LOAD_RIGHT - LOAD_TRANSFER_WIDTH );
	const Int shapes[ 4 ][ 2 ] = { { 1920, 1080 }, { 1280, 720 }, { 2560, 1080 }, { 1024, 768 } };
	for( Int each = 0; each < 4; each++ )
	{
		const Int w = shapes[ each ][ 0 ], h = shapes[ each ][ 1 ];
		const IRegion2D bar = UIStyle::boardRectOn( LOAD_LEFT, LOAD_BAR_TOP, LOAD_RIGHT, LOAD_BAR_BOTTOM, w, h );
		CHECK( bar.lo.x >= 0 && bar.hi.x <= w && bar.lo.y >= 0 && bar.hi.y <= h );
		// the smallest words, 14 at 1080, never under the floor
		CHECK( UIStyle::menuTextPixels( 14, w, h ) >= UIStyle::MENU_TEXT_FLOOR );
	}
}
