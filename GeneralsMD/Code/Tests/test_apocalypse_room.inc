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
		// one-line rows back to back under the head: 192 + 56 i at 1080
		CHECK_EQ( row.top, (Int)LOBBY_ROW_HEAD + i * (Int)LOBBY_ROW_SEAT );
	}
	// the four rows end at 416, where GUIUtil.cpp's APOC_NIGHTS_TOP stands 14 under them (THE NIGHTS head at 430)
	const LobbyRoomItem &last = plan.items[ plan.itemCount - 1 ];
	CHECK_EQ( last.top + last.height, (Int)LOBBY_ROW_HEAD + (Int)APOCALYPSE_SEATS * (Int)LOBBY_ROW_SEAT );
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
	// the skirmish room is the player alone: one row under the head
	ApocalypsePlanRoom( APOCALYPSE_SKIRMISH_SEATS, 15, plan );
	CHECK_EQ( plan.itemCount, 2 );
	CHECK_EQ( plan.items[ 1 ].slot, 0 );
}

TEST( apocalypse_room_has_no_computer_seats )
{
	// the owner, 2026-11-27: no computer in Apocalypse; co-op up to four players, the skirmish room alone
	const Int computers[] = { SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI, SLOT_TAKEOVER };
	for( Int c = 0; c < (Int)ARRAY_SIZE( computers ); ++c )
	{
		CHECK_EQ( ApocalypseSeatState( 1, APOCALYPSE_SEATS, computers[ c ] ), (Int)SLOT_OPEN );
		CHECK_EQ( ApocalypseSeatState( 3, APOCALYPSE_SEATS, computers[ c ] ), (Int)SLOT_OPEN );
		CHECK_EQ( ApocalypseSeatState( 4, APOCALYPSE_SEATS, computers[ c ] ), (Int)SLOT_CLOSED );
		CHECK_EQ( ApocalypseSeatState( 1, APOCALYPSE_SKIRMISH_SEATS, computers[ c ] ), (Int)SLOT_CLOSED );
	}
	// open and closed seats stay within the room's seats, past them every one closes
	CHECK_EQ( ApocalypseSeatState( 2, APOCALYPSE_SEATS, SLOT_OPEN ), (Int)SLOT_OPEN );
	CHECK_EQ( ApocalypseSeatState( 2, APOCALYPSE_SEATS, SLOT_CLOSED ), (Int)SLOT_CLOSED );
	CHECK_EQ( ApocalypseSeatState( 7, APOCALYPSE_SEATS, SLOT_OPEN ), (Int)SLOT_CLOSED );
	CHECK_EQ( ApocalypseSeatState( 1, APOCALYPSE_SKIRMISH_SEATS, SLOT_OPEN ), (Int)SLOT_CLOSED );
	// a human is never thrown out, wherever he sits
	CHECK_EQ( ApocalypseSeatState( 0, APOCALYPSE_SKIRMISH_SEATS, SLOT_PLAYER ), (Int)SLOT_PLAYER );
	CHECK_EQ( ApocalypseSeatState( 9, APOCALYPSE_SEATS, SLOT_PLAYER ), (Int)SLOT_PLAYER );
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
