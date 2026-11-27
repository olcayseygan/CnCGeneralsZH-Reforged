// The Classic room's seat list (docs/plans/MENU-REDESIGN.md, package 3a; the design is
// experiments/proofs/2026-11-24/lobbies/classic/README-classic-room.md in the wrapper): seats grouped
// by team from the fewest to the most, a head a group, every row one size at 32 seats as at 8, a
// window that scrolls by whole seats with a strip for what is past either edge.  The plan is
// GUIUtil.cpp's LobbyOrderSeats and LobbyPlanRoom.  Included into test_gameengine.cpp.

static const Int ROOM_WINDOW = LOBBY_SEATS_BOTTOM - LOBBY_SEATS_TOP;

static void roomSeats( LobbySeatState seats[], Int count, const Int teams[], const Bool seated[] )
{
	for( Int i = 0; i < count; ++i )
	{
		seats[ i ].team = teams[ i ];
		seats[ i ].seated = seated[ i ];
	}
}

TEST( room_layout_groups_from_fewest_seats_to_most )
{
	// team 1 (0) has three seats, team 2 (1) one, team 3 (2) two; a computer with no team, an open seat
	const Int teams[] = { 0, 1, 0, 2, 0, 2, -1, -1 };
	const Bool seated[] = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE };
	LobbySeatState seats[ 8 ];
	roomSeats( seats, 8, teams, seated );
	Int order[ 8 ], group[ 8 ];
	LobbyOrderSeats( seats, 8, order, group );
	// no team first, then team 2 (one), team 3 (two), team 1 (three), the open seat last
	const Int wantOrder[] = { 6, 1, 3, 5, 0, 2, 4, 7 };
	const Int wantGroup[] = { LOBBY_GROUP_ALONE, 1, 2, 2, 0, 0, 0, LOBBY_GROUP_UNSEATED };
	for( Int i = 0; i < 8; ++i )
	{
		CHECK_EQ( order[ i ], wantOrder[ i ] );
		CHECK_EQ( group[ i ], wantGroup[ i ] );
	}
}

TEST( room_layout_ties_keep_team_order )
{
	const Int teams[] = { 1, 0, 1, 0 };
	const Bool seated[] = { TRUE, TRUE, TRUE, TRUE };
	LobbySeatState seats[ 4 ];
	roomSeats( seats, 4, teams, seated );
	Int order[ 4 ], group[ 4 ];
	LobbyOrderSeats( seats, 4, order, group );
	CHECK_EQ( order[ 0 ], 1 );
	CHECK_EQ( order[ 1 ], 3 );
	CHECK_EQ( order[ 2 ], 0 );
	CHECK_EQ( order[ 3 ], 2 );
}

TEST( room_layout_eight_seats_in_two_teams_fit_without_a_scroll )
{
	// eight seats in two teams take 512 of the 792 pixels: two heads of 32 and eight one-line rows of 56
	const Int teams[] = { 0, 0, 0, 0, 1, 1, 1, 1 };
	const Bool seated[] = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE };
	LobbySeatState seats[ 8 ];
	roomSeats( seats, 8, teams, seated );
	LobbyRoomPlan plan;
	LobbyPlanRoom( seats, 8, 0, ROOM_WINDOW, 15, plan );
	CHECK_EQ( plan.maxScroll, 0 );
	CHECK_EQ( plan.above, 0 );
	CHECK_EQ( plan.below, 0 );
	CHECK_EQ( plan.itemCount, 10 );
	CHECK( plan.items[ 0 ].head );
	CHECK_EQ( plan.items[ 0 ].count, 4 );
	CHECK( plan.items[ 5 ].head );
	const LobbyRoomItem &last = plan.items[ plan.itemCount - 1 ];
	CHECK_EQ( last.top + last.height, 2 * LOBBY_ROW_HEAD + 8 * LOBBY_ROW_SEAT );
	CHECK_EQ( last.top + last.height, 512 );
}

TEST( room_layout_rows_keep_their_size_at_32_seats )
{
	// 32 seats in teams of 1, 4, 7, 9 and 11, as the design's 32-seat still
	Int teams[ 32 ];
	Bool seated[ 32 ];
	const Int sizes[] = { 1, 4, 7, 9, 11 };
	Int at = 0;
	for( Int t = 0; t < 5; ++t )
		for( Int s = 0; s < sizes[ t ]; ++s, ++at )
		{
			teams[ at ] = t;
			seated[ at ] = TRUE;
		}
	LobbySeatState seats[ 32 ];
	roomSeats( seats, 32, teams, seated );
	LobbyRoomPlan plan;
	LobbyPlanRoom( seats, 32, 0, ROOM_WINDOW, 15, plan );
	CHECK( plan.maxScroll > 0 );
	CHECK_EQ( plan.above, 0 );
	CHECK( plan.below > 0 );
	for( Int i = 0; i < plan.itemCount; ++i )
	{
		const LobbyRoomItem &item = plan.items[ i ];
		CHECK_EQ( item.height, item.head ? (Int)LOBBY_ROW_HEAD : (Int)LOBBY_ROW_SEAT );
		// every row whole and over the foot's strip
		CHECK( item.top + item.height <= ROOM_WINDOW - LOBBY_ROW_STRIP );
	}
	// the window shows every seat it does not count above or below
	Int rows = 0;
	for( Int i = 0; i < plan.itemCount; ++i )
		rows += plan.items[ i ].head ? 0 : 1;
	CHECK_EQ( plan.above + rows + plan.below, 32 );

	// twelve seats down: the strip above, the first seat's group head repeated at the window's top
	LobbyPlanRoom( seats, 32, 12, ROOM_WINDOW, 15, plan );
	CHECK_EQ( plan.scroll, 12 );
	CHECK_EQ( plan.above, 12 );
	CHECK( plan.items[ 0 ].head );
	CHECK_EQ( plan.items[ 0 ].top, (Int)LOBBY_ROW_STRIP );
	CHECK_EQ( plan.items[ 0 ].group, plan.items[ 1 ].group );
	CHECK_EQ( plan.items[ 1 ].slot, plan.order[ 12 ] );

	// the scroll stops where the last seat stands at the foot, nothing below it
	LobbyPlanRoom( seats, 32, 99, ROOM_WINDOW, 15, plan );
	CHECK_EQ( plan.scroll, plan.maxScroll );
	CHECK_EQ( plan.below, 0 );
	CHECK_EQ( plan.items[ plan.itemCount - 1 ].slot, plan.order[ 31 ] );
	LobbyPlanRoom( seats, 32, plan.maxScroll - 1, ROOM_WINDOW, 15, plan );
	CHECK( plan.below > 0 );
	LobbyPlanRoom( seats, 32, -5, ROOM_WINDOW, 15, plan );
	CHECK_EQ( plan.scroll, 0 );
}

TEST( room_layout_open_seats_are_rows_of_the_same_height_and_a_row_budget_holds )
{
	// one player and 31 open seats: every row 56, open or seated, so the rows never jump; at most 15
	// rows (the skirmish room's row windows) and what the window holds
	Int teams[ 32 ];
	Bool seated[ 32 ];
	for( Int i = 0; i < 32; ++i )
	{
		teams[ i ] = -1;
		seated[ i ] = i == 0;
	}
	LobbySeatState seats[ 32 ];
	roomSeats( seats, 32, teams, seated );
	LobbyRoomPlan plan;
	LobbyPlanRoom( seats, 32, 0, ROOM_WINDOW, 15, plan );
	CHECK( plan.items[ 0 ].head );
	CHECK_EQ( plan.items[ 0 ].group, (Int)LOBBY_GROUP_ALONE );
	CHECK_EQ( plan.items[ 1 ].slot, 0 );
	CHECK( plan.items[ 2 ].head );
	CHECK_EQ( plan.items[ 2 ].group, (Int)LOBBY_GROUP_UNSEATED );
	CHECK_EQ( plan.items[ 2 ].count, 31 );
	CHECK_EQ( plan.items[ 1 ].height, (Int)LOBBY_ROW_SEAT );
	CHECK_EQ( plan.items[ 3 ].height, (Int)LOBBY_ROW_SEAT );
	CHECK( plan.items[ plan.itemCount - 1 ].top + LOBBY_ROW_SEAT <= ROOM_WINDOW - LOBBY_ROW_STRIP );
	Int rows = 0;
	for( Int i = 0; i < plan.itemCount; ++i )
		rows += plan.items[ i ].head ? 0 : 1;
	CHECK( rows <= 15 );
	CHECK_EQ( plan.below, 32 - rows );
}

TEST( room_layout_a_team_change_moves_the_seat_to_its_group )
{
	// seat 5 goes from team 2 to team 1: the order puts it after team 1's seats once its sort team moves
	const Int before[] = { 0, 0, 0, 1, 1, 1 };
	const Int after[] = { 0, 0, 0, 1, 1, 0 };
	const Bool seated[] = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE };
	LobbySeatState seats[ 6 ];
	Int order[ 6 ], group[ 6 ];
	roomSeats( seats, 6, before, seated );
	LobbyOrderSeats( seats, 6, order, group );
	CHECK_EQ( order[ 5 ], 5 );
	CHECK_EQ( group[ 5 ], 1 );
	roomSeats( seats, 6, after, seated );
	LobbyOrderSeats( seats, 6, order, group );
	// team 2 has two now and comes first; seat 5 ends team 1's group
	CHECK_EQ( order[ 0 ], 3 );
	CHECK_EQ( order[ 1 ], 4 );
	CHECK_EQ( order[ 5 ], 5 );
	CHECK_EQ( group[ 5 ], 0 );
}
