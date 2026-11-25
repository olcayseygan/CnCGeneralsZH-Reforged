// The box family (docs/plans/MENU-REDESIGN.md, package 5): the in-game menu's two heights and rects,
// and the disconnect box growing with its waiting seats, going to half rows past eight, giving up
// chat lines before it would reach the command bar.  The numbers are the design's
// (experiments/proofs/2026-11-24/screens/boxes/boxes.py in the wrapper).  Included into
// test_gameengine.cpp, after test_menu_style.inc.

#include "GameClient/DisconnectMenu.h"

extern Int QuitMenuBoxHeight( Int keys );

TEST( menu_boxes_ingame_menu_stands_where_the_design_does )
{
	using namespace UIStyle;
	// single player and skirmish: Return, Save/Load, Options, Restart, Exit
	CHECK_EQ( QuitMenuBoxHeight( 5 ), 540 );
	menuCheckRect( boxRect1080( BOX_MENU, QuitMenuBoxHeight( 5 ), TRUE ), 680, 132, 1240, 672 );
	// a network game or a replay: no Save/Load
	CHECK_EQ( QuitMenuBoxHeight( 4 ), 468 );
	menuCheckRect( boxRect1080( BOX_MENU, QuitMenuBoxHeight( 4 ), TRUE ), 680, 168, 1240, 636 );
	// clear of the command bar, frame A's corner return and air included
	CHECK( boxRect1080( BOX_MENU, QuitMenuBoxHeight( 5 ), TRUE ).hi.y <= BOX_BAR_TOP - 44 );
}

TEST( menu_boxes_disconnect_box_grows_with_its_seats )
{
	using namespace UIStyle;
	// the design's eight seats: two waiting, five talking on one line, four chat lines
	const DisconnectBoxSize eight = DisconnectBoxSizeFor( 2, 1, FALSE );
	CHECK( !eight.halves );
	CHECK_EQ( eight.rows, 2 );
	CHECK_EQ( eight.shown, 2 );
	CHECK_EQ( eight.chatLines, 4 );
	CHECK_EQ( eight.height, 498 );
	menuCheckRect( boxRect1080( BOX_LIST, eight.height, TRUE ), 440, 153, 1480, 651 );

	// the 32-seat still: ten waiting go two to a line, twenty-one names on three lines
	const DisconnectBoxSize full = DisconnectBoxSizeFor( 10, 3, FALSE );
	CHECK( full.halves );
	CHECK_EQ( full.rows, 5 );
	CHECK_EQ( full.shown, 10 );
	CHECK_EQ( full.chatLines, 4 );
	CHECK_EQ( full.height, 716 );
	menuCheckRect( boxRect1080( BOX_LIST, full.height, TRUE ), 440, 56, 1480, 772 );

	// eight waiting still have a row each; nine go to half rows
	CHECK( !DisconnectBoxSizeFor( 8, 0, FALSE ).halves );
	CHECK( DisconnectBoxSizeFor( 9, 0, FALSE ).halves );
	CHECK_EQ( DisconnectBoxSizeFor( 9, 0, FALSE ).rows, 5 );

	// the host's stall is a row of its own
	CHECK_EQ( DisconnectBoxSizeFor( 2, 1, TRUE ).height - DisconnectBoxSizeFor( 2, 1, FALSE ).height,
		DISCONNECT_ROW - ( DisconnectBoxSizeFor( 2, 1, FALSE ).chatLines - DisconnectBoxSizeFor( 2, 1, TRUE ).chatLines ) * DISCONNECT_CHAT_LINE );
}

TEST( menu_boxes_disconnect_box_never_reaches_the_bar )
{
	using namespace UIStyle;
	for( Int waiting = 0; waiting <= 31; ++waiting )
	{
		for( Int lines = 0; lines <= 4; ++lines )
		{
			const DisconnectBoxSize size = DisconnectBoxSizeFor( waiting, lines, waiting % 3 == 0 );
			CHECK( size.height <= DISCONNECT_BOX_MAX );
			CHECK( size.chatLines >= 2 && size.chatLines <= 4 );
			CHECK( size.shown <= waiting );
			CHECK( boxRect1080( BOX_LIST, size.height, TRUE ).hi.y <= BOX_BAR_TOP - 44 );
		}
	}
	// all 31 others waiting: the rows that fit, the chat down to two lines
	const DisconnectBoxSize worst = DisconnectBoxSizeFor( 31, 0, FALSE );
	CHECK_EQ( worst.chatLines, 2 );
	CHECK( worst.shown < 31 );
}
