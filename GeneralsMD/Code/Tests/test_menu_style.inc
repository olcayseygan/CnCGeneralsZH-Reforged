// The menu language's foundation (docs/plans/MENU-REDESIGN.md, package 2): the board on four screens,
// frame A's ticks, the 9 px floor under menu text, the box's widths, rows and keys, and the body's
// wrapping.  The numbers are the design's (experiments/proofs/2026-11-24/spirit/c-bridge/c3.py and
// screens/boxes/boxes.py in the wrapper).  Included into test_gameengine.cpp.

#include "GameClient/UIStyle.h"

static void menuCheckRect( const IRegion2D &r, Int left, Int top, Int right, Int bottom )
{
	CHECK_EQ( r.lo.x, left );
	CHECK_EQ( r.lo.y, top );
	CHECK_EQ( r.hi.x, right );
	CHECK_EQ( r.hi.y, bottom );
}

TEST( menu_style_board_panel_on_four_screens )
{
	using namespace UIStyle;
	// 16:9 fills; 720 is two thirds of it; 21:9 keeps the board centred with backdrop at the sides;
	// 4:3 is width bound with backdrop above and below
	menuCheckRect( boardRectOn( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM, 1920, 1080 ), 32, 28, 1888, 1052 );
	menuCheckRect( boardRectOn( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM, 1280, 720 ), 21, 19, 1259, 701 );
	menuCheckRect( boardRectOn( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM, 2560, 1080 ), 352, 28, 2208, 1052 );
	menuCheckRect( boardRectOn( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM, 1024, 768 ), 17, 111, 1007, 657 );

	const BoardFit wide = boardFit( 2560, 1080 );
	CHECK_EQ( wide.x, 320 );
	CHECK_EQ( wide.y, 0 );
	const BoardFit square = boardFit( 1024, 768 );
	CHECK_EQ( square.x, 0 );
	CHECK_EQ( square.y, 96 );

	// the grid's rows stand in order, the keys inside the panel
	CHECK( MENU_TITLE_TOP < MENU_TITLE_BOTTOM && MENU_TITLE_BOTTOM < MENU_HEADER_RULE );
	CHECK( MENU_HEADER_RULE < MENU_TABS_TOP && MENU_TABS_BOTTOM < MENU_CONTENT_TOP );
	CHECK( MENU_CONTENT_TOP < MENU_FOOT_RULE && MENU_FOOT_RULE < MENU_KEYS_TOP );
	CHECK( MENU_KEYS_BOTTOM < MENU_PANEL_BOTTOM );
	CHECK_EQ( pxOn( 1.0f, 640, 480 ), 1 );	// a line never thins to nothing
	CHECK_EQ( pxOn( 0.0f, 640, 480 ), 0 );
}

TEST( menu_style_frame_ticks )
{
	using namespace UIStyle;
	// 4 px every 8, 8 every 40, 14 every 200, counted from the corner
	CHECK_EQ( frameTickLength( 0 ), 14 );
	CHECK_EQ( frameTickLength( 1 ), 4 );
	CHECK_EQ( frameTickLength( 4 ), 4 );
	CHECK_EQ( frameTickLength( 5 ), 8 );
	CHECK_EQ( frameTickLength( 10 ), 8 );
	CHECK_EQ( frameTickLength( 24 ), 4 );
	CHECK_EQ( frameTickLength( 25 ), 14 );
	CHECK_EQ( frameTickLength( 50 ), 14 );
	Int longTicks = 0, middle = 0;
	const Int across = ( MENU_PANEL_RIGHT - MENU_PANEL_LEFT ) / FRAME_TICK_STEP;	// 232 steps over the panel
	for( Int i = 0; i <= across; i++ )
	{
		longTicks += frameTickLength( i ) == 14;
		middle += frameTickLength( i ) == 8;
	}
	CHECK_EQ( longTicks, 10 );		// 0, 200, ... 1800 along 1856
	CHECK_EQ( middle, 37 );
	// at 720 a step of 8 is 5.33 px, so the 200 px mark lands on 133
	CHECK_EQ( pxOn( (Real)( FRAME_TICK_STEP * 25 ), 1280, 720 ), 133 );
}

TEST( menu_style_text_floor )
{
	using namespace UIStyle;
	CHECK_EQ( menuTextPixels( MENU_TEXT_BODY, 1920, 1080 ), 18 );
	CHECK_EQ( menuTextPixels( MENU_TEXT_TITLE, 1920, 1080 ), 40 );
	CHECK_EQ( menuTextPixels( MENU_TEXT_LABEL, 1280, 720 ), 9 );		// 9.3: the standing order's floor
	CHECK_EQ( menuTextPixels( MENU_TEXT_BODY, 1280, 720 ), 12 );
	CHECK_EQ( menuTextPixels( MENU_TEXT_TITLE, 2560, 1440 ), 53 );
	const Int screens[][ 2 ] = { { 1920, 1080 }, { 1280, 720 }, { 1024, 768 }, { 800, 600 }, { 640, 480 }, { 2560, 1080 }, { 3840, 2160 } };
	const Int sizes[] = { MENU_TEXT_TITLE, MENU_TEXT_TEAM_FIGURE, MENU_TEXT_ROW_FIGURE, MENU_TEXT_BODY, MENU_TEXT_LABEL,
		MENU_TEXT_BOX_TITLE, MENU_TEXT_KEY, MENU_TEXT_HINT };
	for( Int s = 0; s < 7; s++ )
	{
		for( Int z = 0; z < 8; z++ )
		{
			const Int em = menuTextPixels( sizes[ z ], screens[ s ][ 0 ], screens[ s ][ 1 ] );
			CHECK( em >= MENU_TEXT_FLOOR );
			// the point size's em (points * 4 / 3) stays within a pixel of it and never under the floor
			const Int points = menuFontPoints( em );
			CHECK( points * 4 >= ( em - 1 ) * 3 && points * 4 <= ( em + 1 ) * 3 );
			CHECK( points * 4 >= MENU_TEXT_FLOOR * 3 );
		}
	}
	CHECK( MENU_TEXT_LABEL >= 14 && MENU_TEXT_HINT >= 14 );		// nothing under 14 at 1080
}

TEST( menu_style_box_widths_and_growth )
{
	using namespace UIStyle;
	CHECK_EQ( (Int)BOX_MENU, 560 );
	CHECK_EQ( (Int)BOX_QUESTION, 720 );
	CHECK_EQ( (Int)BOX_LIST, 1040 );
	CHECK_EQ( BOX_MENU % FRAME_TICK_STEP, 0 );		// frame A's ticks fall the same on every box
	CHECK_EQ( BOX_QUESTION % FRAME_TICK_STEP, 0 );
	CHECK_EQ( BOX_LIST % FRAME_TICK_STEP, 0 );

	// the question box: 600,415 to 1320,665 at one line, 28 more a line, centred over a menu
	CHECK_EQ( questionBoxHeight( 1 ), 250 );
	CHECK_EQ( questionBoxHeight( 3 ) - questionBoxHeight( 2 ), BOX_LINE );
	menuCheckRect( boxRect1080( BOX_QUESTION, questionBoxHeight( 1 ), FALSE ), 600, 415, 1320, 665 );
	menuCheckRect( boxRect1080( BOX_QUESTION, questionBoxHeight( 2 ), FALSE ), 600, 401, 1320, 679 );
	// over a match, in the world above the bar: the 8 seat disconnect box and the tallest one
	menuCheckRect( boxRect1080( BOX_LIST, 498, TRUE ), 440, 153, 1480, 651 );
	menuCheckRect( boxRect1080( BOX_LIST, 732, TRUE ), 440, 56, 1480, 788 );
	CHECK( boxRect1080( BOX_LIST, 900, TRUE ).lo.y >= BOX_TOP_MIN );

	// the keys: Enter's at the right, 170 wide, 12 apart, 48 high, 20 over the foot
	const IRegion2D box = boxRect1080( BOX_QUESTION, questionBoxHeight( 1 ), FALSE );
	menuCheckRect( boxKeyRect1080( box, 0 ), 1118, 597, 1288, 645 );
	menuCheckRect( boxKeyRect1080( box, 1 ), 936, 597, 1106, 645 );
	menuCheckRect( boxKeyRect1080( box, 2 ), 754, 597, 924, 645 );

	// the tallest question box still stands on the board
	const IRegion2D tall = boxRect1080( BOX_QUESTION, questionBoxHeight( questionBoxMaxLines() ), FALSE );
	CHECK( tall.lo.y >= BOX_TOP_MIN && tall.hi.y <= BOARD_HEIGHT - BOX_TOP_MIN );
	CHECK( questionBoxMaxLines() >= 20 );
}

/// a test font: every letter 10 px, a space 5
static Int menuTestMeasure( const UnicodeString &text, void * )
{
	Int width = 0;
	for( Int each = 0; each < text.getLength(); each++ )
		width += text.getCharAt( each ) == u' ' ? 5 : 10;
	return width;
}

TEST( menu_style_wrap_lines )
{
	using namespace UIStyle;
	std::vector< UnicodeString > lines;
	// "aaaa bbbb cccc" is 130 wide; at 100 the third word goes down
	wrapLines( UnicodeString( u"aaaa bbbb cccc" ), 100, menuTestMeasure, NULL, lines );
	CHECK_EQ( (Int)lines.size(), 2 );
	CHECK( lines[ 0 ] == UnicodeString( u"aaaa bbbb" ) );
	CHECK( lines[ 1 ] == UnicodeString( u"cccc" ) );

	// a word wider than the line stands alone; a break in the text is kept, an empty line too
	lines.clear();
	wrapLines( UnicodeString( u"aa aaaaaaaaaaaa b\n\nc" ), 50, menuTestMeasure, NULL, lines );
	CHECK_EQ( (Int)lines.size(), 5 );
	CHECK( lines[ 0 ] == UnicodeString( u"aa" ) );
	CHECK( lines[ 1 ] == UnicodeString( u"aaaaaaaaaaaa" ) );
	CHECK( lines[ 2 ] == UnicodeString( u"b" ) );
	CHECK( lines[ 3 ].isEmpty() );
	CHECK( lines[ 4 ] == UnicodeString( u"c" ) );

	// one line fits whole, and the box grows by the count
	lines.clear();
	wrapLines( UnicodeString( u"Close Zero Hour Reforged" ), 1000, menuTestMeasure, NULL, lines );
	CHECK_EQ( (Int)lines.size(), 1 );
	CHECK_EQ( questionBoxHeight( (Int)lines.size() ), 250 );
}
