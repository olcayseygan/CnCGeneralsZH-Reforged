// test_lanehud.cpp: where the Lane War HUD's boxes stand (GameClient/LaneHudLayout.h).

#include "test_harness.h"

#include "GameClient/LaneHudLayout.h"

static bool overlap( const LaneHudBox &a, const LaneHudBox &b )
{
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool onScreen( const LaneHudBox &b, int w, int h )
{
	return b.x >= 0 && b.y >= 0 && b.x + b.w <= w && b.y + b.h <= h && b.w > 0 && b.h > 0;
}

static bool inside( const LaneHudBox &in, const LaneHudBox &out )
{
	return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

// 16:9 and wider only: the owner ruled squarer screens out on 2026-11-20
static const int SCREENS[][ 2 ] = { { 1920, 1080 }, { 1280, 720 }, { 1366, 768 }, { 2560, 1440 }, { 2560, 1080 }, { 3440, 1440 }, { 3840, 2160 } };

TEST(lanehud_at_1080_the_boxes_stand_where_the_plan_drew_them)
{
	LaneHudLayout l;
	LaneHud_layout( 1920, 1080, &l );
	CHECK_EQ( l.top.x, 580 );
	CHECK_EQ( l.top.w, 760 );
	CHECK_EQ( l.top.h, 60 );
	CHECK_EQ( l.buy.x, 0 );
	CHECK_EQ( l.buy.y, 744 );
	CHECK_EQ( l.buy.w, 560 );
	CHECK_EQ( l.economy.x, 1360 );
	CHECK_EQ( l.economy.y, 880 );
	CHECK_EQ( l.economy.w, 560 );
	CHECK_EQ( l.enemy[ 0 ].x, 1700 );
	CHECK_EQ( l.enemy[ 0 ].w, 212 );
	CHECK_EQ( l.enemy[ 0 ].h, 80 );
	CHECK_EQ( l.map.x, 588 );
	CHECK_EQ( l.map.w, 744 );
	CHECK_EQ( l.map.y + l.map.h, 1080 );
	CHECK_EQ( l.abilities.w, 560 );
	CHECK_EQ( l.abilities.h, 420 );
}

TEST(lanehud_the_lane_in_the_strip_map_is_eight_to_one)
{
	for( const auto &size : SCREENS )
	{
		LaneHudLayout l;
		LaneHud_layout( size[ 0 ], size[ 1 ], &l );
		CHECK_EQ( l.lane.w / 8, l.lane.h );
		CHECK( inside( l.lane, l.map ) );
	}
}

TEST(lanehud_no_box_leaves_the_screen_or_meets_another)
{
	for( const auto &size : SCREENS )
	{
		const int w = size[ 0 ], h = size[ 1 ];
		LaneHudLayout l;
		LaneHud_layout( w, h, &l );
		const LaneHudBox boxes[] = { l.top, l.middle, l.enemy[ 0 ], l.enemy[ 1 ], l.enemy[ 2 ], l.map, l.buy, l.economy, l.abilities };
		const int count = (int)( sizeof( boxes ) / sizeof( boxes[ 0 ] ) );
		for( int i = 0; i < count; ++i )
		{
			CHECK( onScreen( boxes[ i ], w, h ) );
			for( int j = i + 1; j < count; ++j )
				CHECK( !overlap( boxes[ i ], boxes[ j ] ) );
		}

		// the panels' insides stay inside them and apart
		CHECK( inside( l.hero, l.buy ) );
		CHECK( !overlap( l.hero, l.info ) );
		for( int t = 0; t < LANEHUD_TABS; ++t )
		{
			CHECK( inside( l.tabs[ t ], l.buy ) );
			CHECK( !overlap( l.hero, l.tabs[ t ] ) );
		}
		for( int c = 0; c < LANEHUD_CELLS; ++c )
		{
			CHECK( inside( l.cells[ c ], l.buy ) );
			CHECK( !overlap( l.cells[ c ], l.info ) );
			CHECK( !overlap( l.cells[ c ], l.hero ) );
			for( int t = 0; t < LANEHUD_TABS; ++t )
				CHECK( !overlap( l.cells[ c ], l.tabs[ t ] ) );
			for( int d = c + 1; d < LANEHUD_CELLS; ++d )
				CHECK( !overlap( l.cells[ c ], l.cells[ d ] ) );
		}
		for( int k = 0; k < LANEHUD_ECONOMY_KEYS; ++k )
		{
			CHECK( inside( l.keys[ k ], l.economy ) );
			CHECK( !overlap( l.keys[ k ], l.money ) );
			for( int m = k + 1; m < LANEHUD_ECONOMY_KEYS; ++m )
				CHECK( !overlap( l.keys[ k ], l.keys[ m ] ) );
		}
	}
}

TEST(lanehud_a_wider_screen_keeps_the_sizes_and_moves_the_edges_out)
{
	LaneHudLayout full, wide;
	LaneHud_layout( 1920, 1080, &full );
	LaneHud_layout( 2560, 1080, &wide );
	CHECK_EQ( wide.buy.x, 0 );
	CHECK_EQ( wide.buy.w, full.buy.w );
	CHECK_EQ( wide.economy.x + wide.economy.w, 2560 );
	CHECK_EQ( wide.map.w, full.map.w );
	CHECK_EQ( wide.map.x, full.map.x + 320 );
}
