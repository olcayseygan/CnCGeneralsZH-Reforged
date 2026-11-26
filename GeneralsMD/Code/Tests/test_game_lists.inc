// The game lists' columns and row states (docs/plans/MENU-REDESIGN.md, package 10; the design is
// experiments/proofs/2026-11-24/screens/game-lists/README-game-lists.md in the wrapper).  The online
// Custom games page and the LAN lobby draw every row at these board columns, and a click on a column
// word or a tooltip finds its column by them.  LobbyUtils.cpp.  Included into test_gameengine.cpp.

#include "GameNetwork/GameSpy/LobbyUtils.h"

TEST( game_lists_columns_are_the_designs )
{
	// README "Columns", 16:9: lock 60 to 86, GAME 94 to 440, MODE 452 to 620, MAP 632 to 850, PLAYERS 862 to
	// 1012, PING 1024 to 1136, STATE 1160 to 1286
	static const Int want[ LOBBY_COL_COUNT ][ 2 ] =
		{ { 60, 86 }, { 94, 440 }, { 452, 620 }, { 632, 850 }, { 862, 1012 }, { 1024, 1136 }, { 1160, 1286 } };
	for( Int column = 0; column < LOBBY_COL_COUNT; ++column )
	{
		Int left, right;
		CHECK( LobbyGameColumnSpan( column, FALSE, left, right ) );
		CHECK_EQ( left, want[ column ][ 0 ] );
		CHECK_EQ( right, want[ column ][ 1 ] );
	}
}

TEST( game_lists_columns_never_overlap_and_stay_left_of_the_scroll_bar )
{
	// in order, each starting at least 8 past the one before, the last ending before the track at 1296
	Int lastRight = 0;
	for( Int column = 0; column < LOBBY_COL_COUNT; ++column )
	{
		Int left, right;
		LobbyGameColumnSpan( column, FALSE, left, right );
		CHECK( left < right );
		CHECK( left >= lastRight + 8 );
		lastRight = right;
	}
	CHECK( lastRight <= 1296 );
}

TEST( game_lists_lan_has_no_lock_and_no_ping )
{
	Int left, right;
	CHECK( !LobbyGameColumnSpan( LOBBY_COL_LOCK, TRUE, left, right ) );
	CHECK( !LobbyGameColumnSpan( LOBBY_COL_PING, TRUE, left, right ) );
	CHECK( LobbyGameColumnSpan( LOBBY_COL_STATE, TRUE, left, right ) );
	CHECK_EQ( LobbyGameColumnAt( 70, TRUE ), -1 );
	CHECK_EQ( LobbyGameColumnAt( 1100, TRUE ), -1 );
	CHECK_EQ( LobbyGameColumnAt( 1100, FALSE ), (Int)LOBBY_COL_PING );
}

TEST( game_lists_column_at_finds_the_word_under_the_pointer )
{
	CHECK_EQ( LobbyGameColumnAt( 94, FALSE ), (Int)LOBBY_COL_GAME );
	CHECK_EQ( LobbyGameColumnAt( 439, FALSE ), (Int)LOBBY_COL_GAME );
	CHECK_EQ( LobbyGameColumnAt( 445, FALSE ), -1 );		// the gap between two columns
	CHECK_EQ( LobbyGameColumnAt( 900, FALSE ), (Int)LOBBY_COL_PLAYERS );
	CHECK_EQ( LobbyGameColumnAt( 1200, FALSE ), (Int)LOBBY_COL_STATE );
	CHECK_EQ( LobbyGameColumnAt( 1300, FALSE ), -1 );
	CHECK_EQ( LobbyGameColumnAt( 10, FALSE ), -1 );
}

TEST( game_lists_row_state_reads_join_then_playing_then_build_then_full )
{
	CHECK_EQ( (Int)LobbyGameStateOf( FALSE, FALSE, FALSE, FALSE ), (Int)LOBBY_STATE_OPEN );
	CHECK_EQ( (Int)LobbyGameStateOf( FALSE, FALSE, FALSE, TRUE ), (Int)LOBBY_STATE_FULL );
	CHECK_EQ( (Int)LobbyGameStateOf( FALSE, FALSE, TRUE, TRUE ), (Int)LOBBY_STATE_OTHER_BUILD );
	CHECK_EQ( (Int)LobbyGameStateOf( FALSE, TRUE, TRUE, TRUE ), (Int)LOBBY_STATE_PLAYING );
	CHECK_EQ( (Int)LobbyGameStateOf( TRUE, TRUE, TRUE, TRUE ), (Int)LOBBY_STATE_JOINING );
}
