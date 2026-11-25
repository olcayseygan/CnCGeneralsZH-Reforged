// test_observerhud.cpp: where the watcher's HUD stands (GameClient/ObserverHudLayout.h), 2 to 32 seats.

#include "test_harness.h"

#include "GameClient/ObserverHudLayout.h"

static bool overlap( const ObsRect &a, const ObsRect &b )
{
	return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static bool inside( const ObsRect &in, const ObsRect &out )
{
	return in.x0 >= out.x0 && in.y0 >= out.y0 && in.x1 <= out.x1 && in.y1 <= out.y1;
}

TEST(observer_hud_radar_box_holds_its_glass_and_keys)
{
	const ObsRect box = ObsHud_radarBox(), glass = ObsHud_radarGlass();
	CHECK( inside( glass, box ) );
	CHECK_EQ( glass.w(), glass.h() );		// the map's square
	for( int i = 0; i < OBSHUD_KEYS; i++ )
	{
		const ObsRect k = ObsHud_key( i );
		CHECK( inside( k, box ) );
		CHECK( !overlap( k, glass ) );
		CHECK_EQ( k.w(), 58 );
		CHECK_EQ( k.h(), 36 );
		if( i > 0 )
			CHECK( !overlap( k, ObsHud_key( i - 1 ) ) );
	}
	CHECK( ObsHud_key( OBSHUD_KEYS - 1 ).y1 <= 1080 );
}

TEST(observer_hud_panel_wells_stand_inside_the_panel_and_apart)
{
	const ObsRect block = ObsHud_panelBlock();
	CHECK( inside( block, ObsHud_panel() ) );
	CHECK( inside( ObsHud_panelMoney(), block ) );
	CHECK( inside( ObsHud_panelPortrait(), block ) );
	CHECK( !overlap( ObsHud_panelPortrait(), ObsHud_panelCells() ) );
	for( int i = 0; i < 4; i++ )
	{
		CHECK( !overlap( ObsHud_panelFact( i ), ObsHud_panelPortrait() ) );
		CHECK( !overlap( ObsHud_panelFact( i ), ObsHud_panelCells() ) );
	}
	for( int row = 0; row < 2; row++ )
		for( int i = 0; i < 7; i++ )
			CHECK( inside( ObsHud_panelCell( row, i ), ObsHud_panelCells() ) );
	for( int i = 0; i < 3; i++ )
		CHECK( !overlap( ObsHud_panelCell( 2, i ), ObsHud_panelPowersWord() ) );
	for( int i = 0; i < 2; i++ )
	{
		CHECK( inside( ObsHud_panelPower( i ), ObsHud_panelCells() ) );
		CHECK( !overlap( ObsHud_panelPower( i ), ObsHud_panelPowersWord() ) );
	}
	// the radar box and the panel never meet
	CHECK( !overlap( ObsHud_panelBlock(), ObsHud_radarBox() ) );
}

TEST(observer_hud_roster_box_grows_with_its_list_and_stays_clear)
{
	// README-spec's tops (the mount's): 2 seats of one team each at 816, 8 in two teams at 578, 16 at 438, 4 teams
	// of 8 in two columns at 470
	ObsRosterTier t = ObsHud_rosterTier( 2 );
	CHECK_EQ( ObsHud_rightBox( OBSHUD_ROSTER_X1, ObsHud_rosterBody( 2, 2, t.row, 0 ) ).mount.y0, 816 );
	t = ObsHud_rosterTier( 8 );
	CHECK_EQ( ObsHud_rightBox( OBSHUD_ROSTER_X1, ObsHud_rosterBody( 2, 8, t.row, t.head ) ).mount.y0, 578 );
	t = ObsHud_rosterTier( 16 );
	CHECK_EQ( t.columns, 1 );
	CHECK_EQ( ObsHud_rightBox( OBSHUD_ROSTER_X1, ObsHud_rosterBody( 2, 16, t.row, t.head ) ).mount.y0, 438 );
	t = ObsHud_rosterTier( 32 );
	CHECK_EQ( t.columns, 2 );
	CHECK_EQ( ObsHud_rightBox( OBSHUD_ROSTER_X2, ObsHud_rosterBody( 2, 16, t.row, t.head ) ).mount.y0, 470 );
	// teams of one carry no heads
	CHECK_EQ( ObsHud_rosterBody( 8, 8, 30, 0 ), 26 + 8 * 30 + 30 );

	for( int seats = 2; seats <= 32; seats++ )
	{
		t = ObsHud_rosterTier( seats );
		const int rows = t.columns == 2 ? ( seats + 1 ) / 2 : seats;
		const ObsRightBox b = ObsHud_rightBox( t.columns == 2 ? OBSHUD_ROSTER_X2 : OBSHUD_ROSTER_X1, ObsHud_rosterBody( 2, rows, t.row, t.head ) );
		CHECK( b.box.w() <= OBSHUD_BOX_REF_W );
		CHECK( b.box.h() <= OBSHUD_BOX_REF_H );
		CHECK( b.mount.y0 >= 0 );
		CHECK( inside( b.well, b.box ) );
		CHECK( inside( b.drop[ 0 ], b.box ) );
		CHECK( inside( b.drop[ 1 ], b.box ) );
		CHECK( !overlap( b.drop[ 0 ], b.drop[ 1 ] ) );
		CHECK( b.drop[ 1 ].y1 <= b.well.y0 );
		CHECK( inside( b.clock, b.mount ) );
		// the followed player's panel steps left to clear the box and still clears the radar box
		ObsRect panel = ObsHud_panelBlock();
		panel.x0 += ObsHud_panelShift( b.box.x0 );
		panel.x1 += ObsHud_panelShift( b.box.x0 );
		CHECK( !overlap( b.block, panel ) );
		CHECK( !overlap( panel, ObsHud_radarBox() ) );
		CHECK( !overlap( b.block, ObsHud_radarBox() ) );
	}
	CHECK_EQ( ObsHud_panelShift( OBSHUD_ROSTER_X1 ), 0 );
	CHECK_EQ( ObsHud_panelShift( OBSHUD_ROSTER_X2 ), -94 );
}

TEST(observer_hud_standings_fit_the_box_at_every_seat_count)
{
	CHECK_EQ( ObsHud_standBody( 32 ), 30 + 32 * 22 + 12 + 30 );
	for( int seats = 2; seats <= 32; seats++ )
		for( int nameW = 40; nameW <= 400; nameW += 90 )
		{
			const int x0 = ObsHud_standX0( seats, nameW );
			const ObsRightBox b = ObsHud_rightBox( x0, ObsHud_standBody( seats ) );
			CHECK( b.box.w() >= 384 );
			CHECK( b.box.w() <= OBSHUD_BOX_REF_W );
			CHECK( b.box.h() <= OBSHUD_BOX_REF_H );
			CHECK( b.mount.y0 >= 0 );
			// the panel shows only with the roster; the standings stay clear of the radar box
			CHECK( !overlap( b.block, ObsHud_radarBox() ) );
		}
}

TEST(observer_hud_tab_table_holds_four_columns_of_eight)
{
	const ObsRect table = ObsHud_tabTable();
	CHECK( !overlap( table, ObsHud_tabTitles() ) );
	CHECK( !overlap( table, ObsHud_tabFacts() ) );
	for( int c = 0; c < OBSHUD_TAB_COLUMNS; c++ )
	{
		const ObsRect col = ObsHud_tabColumn( c, OBSHUD_TAB_COLUMNS, table );
		CHECK( inside( col, table ) );
		if( c > 0 )
			CHECK( !overlap( col, ObsHud_tabColumn( c - 1, OBSHUD_TAB_COLUMNS, table ) ) );
		for( int j = 0; j < OBSHUD_TAB_ROWS; j++ )
		{
			const ObsRect row = ObsHud_tabRow( col, j, OBSHUD_TAB_ROWS );
			CHECK( inside( row, table ) );
			CHECK_EQ( row.h(), OBSHUD_TAB_ROW_H );
			CHECK( inside( ObsHud_tabMute( row ), row ) );
		}
	}
	// four rows or fewer take twice a row's height for bigger pictures, never half the table
	CHECK_EQ( ObsHud_tabRow( ObsHud_tabColumn( 0, 2, table ), 0, 1 ).h(), 2 * OBSHUD_TAB_ROW_H );
	CHECK_EQ( ObsHud_tabRow( ObsHud_tabColumn( 0, 2, table ), 3, 4 ).h(), 2 * OBSHUD_TAB_ROW_H );
	CHECK( inside( ObsHud_tabRow( ObsHud_tabColumn( 0, 2, table ), 3, 4 ), table ) );
	CHECK_EQ( ObsHud_tabRow( ObsHud_tabColumn( 0, 2, table ), 0, 5 ).h(), OBSHUD_TAB_ROW_H );
}
