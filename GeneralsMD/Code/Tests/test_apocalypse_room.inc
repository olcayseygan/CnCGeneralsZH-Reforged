// The Apocalypse room (docs/plans/MENU-REDESIGN.md, package 3b; the design is
// experiments/proofs/2026-11-24/lobbies/apocalypse/README-apocalypse-room.md in the wrapper): four seats in
// seat order under the survivors' head, and the nights band's figures read from the director, never typed.
// GUIUtil.cpp's ApocalypsePlanRoom, ApocalypseNightFallsAt, ApocalypseWallSeconds and
// ApocalypseNewKindsOfNight.  Included into test_gameengine.cpp.

#include "GameLogic/ApocalypseDirector.h"

TEST( apocalypse_room_four_rows_in_seat_order_under_one_head )
{
	LobbyRoomPlan plan;
	ApocalypsePlanRoom( 8, 15, plan );
	CHECK_EQ( plan.itemCount, 1 + (Int)APOCALYPSE_SEATS );
	CHECK( plan.items[ 0 ].head );
	CHECK_EQ( plan.items[ 0 ].top, 0 );
	CHECK_EQ( plan.items[ 0 ].height, (Int)LOBBY_ROW_HEAD );
	for( Int i = 0; i < (Int)APOCALYPSE_SEATS; ++i )
	{
		const LobbyRoomItem &row = plan.items[ 1 + i ];
		CHECK( !row.head );
		CHECK_EQ( row.slot, i );
		CHECK_EQ( row.height, (Int)LOBBY_ROW_SEAT );
		// README: seat rows at 194 + 90 i, the head from 160
		CHECK_EQ( LOBBY_SEATS_TOP + row.top, 194 + 90 * i );
	}
	// the last row ends over THE NIGHTS head at 566
	CHECK( LOBBY_SEATS_TOP + plan.items[ plan.itemCount - 1 ].top + LOBBY_ROW_SEAT <= 566 );
	CHECK_EQ( plan.maxScroll, 0 );
	CHECK_EQ( plan.above + plan.below, 0 );
}

TEST( apocalypse_room_fewer_seats_fewer_rows )
{
	LobbyRoomPlan plan;
	ApocalypsePlanRoom( 2, 15, plan );
	CHECK_EQ( plan.itemCount, 3 );
	CHECK_EQ( plan.orderCount, 2 );
	ApocalypsePlanRoom( 4, 3, plan );		// the skirmish room's rows bound it too
	CHECK_EQ( plan.orderCount, 3 );
}

TEST( apocalypse_room_nights_fall_on_the_directors_clock )
{
	// day and dusk before the first night, a whole cycle a night after it
	CHECK_EQ( ApocalypseNightFallsAt( 1 ), APOCALYPSE_DAY_SECONDS + APOCALYPSE_DUSK_SECONDS );
	const Int cycle = APOCALYPSE_DAY_SECONDS + APOCALYPSE_DUSK_SECONDS + APOCALYPSE_NIGHT_SECONDS + APOCALYPSE_DAWN_SECONDS;
	CHECK_EQ( ApocalypseNightFallsAt( 2 ) - ApocalypseNightFallsAt( 1 ), cycle );
	// README: at 1.5x the day is 2:40 and night 1 falls at 3:10
	CHECK_EQ( ApocalypseWallSeconds( APOCALYPSE_DAY_SECONDS, 150 ), 160 );
	CHECK_EQ( ApocalypseWallSeconds( ApocalypseNightFallsAt( 1 ), 150 ), 190 );
	CHECK_EQ( ApocalypseWallSeconds( ApocalypseNightFallsAt( 1 ), 100 ), ApocalypseNightFallsAt( 1 ) );
}

TEST( apocalypse_room_new_kinds_by_night )
{
	// the README's count of rolled kinds by first night: the walker on day one, then 6, 6, 4, 6, 5, 1;
	// broods and bosses are never rolled
	const Int want[] = { 1, 6, 6, 4, 6, 5, 1, 0 };
	Int total = 0;
	for( Int night = 0; night < (Int)ARRAY_SIZE( want ); ++night )
	{
		Int kinds[ 8 ];
		const Int count = ApocalypseNewKindsOfNight( night, kinds, 8 );
		CHECK_EQ( count, want[ night ] );
		for( Int k = 0; k < min( count, 8 ); ++k )
		{
			CHECK_EQ( Apocalypse_kind( kinds[ k ] ).firstNight, night );
			CHECK( ( Apocalypse_kind( kinds[ k ] ).flags & APOCALYPSE_KIND_BOSS ) == 0 );
		}
		total += count;
	}
	CHECK_EQ( ApocalypseNewKindsOfNight( 0, NULL, 0 ), 1 );		// counts without writing
	CHECK( total < Apocalypse_kindCount() );
}
