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

TEST(lanehud_at_1080_the_boxes_stand_where_the_art_drew_them)
{
	LaneHudLayout l;
	LaneHud_layout( 1920, 1080, &l );
	CHECK_EQ( l.money.x, 505 );
	CHECK_EQ( l.money.w, 200 );
	CHECK_EQ( l.map.x, 715 );
	CHECK_EQ( l.map.y, 8 );
	CHECK_EQ( l.map.h, 66 );
	CHECK_EQ( l.lane.x, 720 );
	CHECK_EQ( l.lane.w, 480 );
	CHECK_EQ( l.lane.h, 56 );
	CHECK_EQ( l.yourWave.x, 1215 );
	CHECK_EQ( l.waveBar.x, 715 );
	CHECK_EQ( l.waveBar.w, 490 );
	CHECK_EQ( l.waveBar.y, 82 );
	CHECK_EQ( l.waveBar.h, 14 );
	CHECK_EQ( l.countdown.x + l.countdown.w / 2, 960 );
	CHECK_EQ( l.countdown.y, 104 );
	CHECK_EQ( l.tiers[ 0 ].x, 353 );
	CHECK_EQ( l.tiers[ 2 ].y, 1034 );
	CHECK_EQ( l.hero.x, 433 );
	CHECK_EQ( l.hero.w, 100 );
	CHECK_EQ( l.cards[ 0 ].x, 549 );
	CHECK_EQ( l.cards[ 7 ].x + l.cards[ 7 ].w, 1391 );
	CHECK_EQ( l.cards[ 0 ].y, 958 );
	CHECK_EQ( l.cards[ 0 ].h, 116 );
	CHECK_EQ( l.deck.x, 1407 );
	CHECK_EQ( l.abilitiesKey.x + l.abilitiesKey.w, 1567 );
	CHECK_EQ( l.upgradeRow.x + l.upgradeRow.w, 1513 );
	CHECK_EQ( LaneHud_upgradeCell( l, 3, 2 ).x + LaneHud_upgradeCell( l, 3, 2 ).w, 1513 );
	CHECK_EQ( LaneHud_upgradeCell( l, 3, 0 ).x, 1513 - 3 * 106 + 6 );
	CHECK_EQ( l.menu.x, 1770 );
	CHECK_EQ( l.diplomacy.x + l.diplomacy.w, 1914 );

	// six rows fill the art's panel exactly: 1408,646 to 1600,950, a row's parts where the art put them
	const LaneHudBox abilities = LaneHud_abilitiesPanel( l, 6 );
	CHECK_EQ( abilities.x, 1408 );
	CHECK_EQ( abilities.y, 646 );
	CHECK_EQ( abilities.w, 192 );
	CHECK_EQ( abilities.h, 304 );
	const LaneHudBox row = LaneHud_abilityRow( l, 6, 1 );
	CHECK_EQ( row.y, 654 + 48 );
	CHECK_EQ( LaneHud_at( row, l.abilityCameo ).x, 1422 );
	CHECK_EQ( LaneHud_at( row, l.abilityIcon ).x, 1478 );
	CHECK_EQ( LaneHud_at( row, l.abilityToggle ).x, 1540 );
	CHECK_EQ( LaneHud_at( row, l.abilityToggle ).y, 654 + 48 + 13 );
}

TEST(lanehud_the_strip_map_fills_the_lane_chips_glass)
{
	for( const auto &size : SCREENS )
	{
		LaneHudLayout l;
		LaneHud_layout( size[ 0 ], size[ 1 ], &l );
		CHECK( inside( l.lane, l.map ) );
		CHECK( l.lane.w > 8 * l.lane.h && l.lane.w < 9 * l.lane.h );
	}
}

TEST(lanehud_no_box_leaves_the_screen_or_meets_another)
{
	for( const auto &size : SCREENS )
	{
		const int w = size[ 0 ], h = size[ 1 ];
		LaneHudLayout l;
		LaneHud_layout( w, h, &l );

		// the boxes that stand at once: the top chips, the menu keys and the bottom row
		LaneHudBox boxes[ 7 + LANEHUD_TIERS + 1 + LANEHUD_CARDS + 2 ];
		int count = 0;
		boxes[ count++ ] = l.money;
		boxes[ count++ ] = l.map;
		boxes[ count++ ] = l.yourWave;
		boxes[ count++ ] = l.waveBar;
		boxes[ count++ ] = l.countdown;
		boxes[ count++ ] = l.menu;
		boxes[ count++ ] = l.diplomacy;
		for( int t = 0; t < LANEHUD_TIERS; ++t )
			boxes[ count++ ] = l.tiers[ t ];
		boxes[ count++ ] = l.hero;
		for( int c = 0; c < LANEHUD_CARDS; ++c )
			boxes[ count++ ] = l.cards[ c ];
		boxes[ count++ ] = l.deck;
		boxes[ count++ ] = l.abilitiesKey;
		for( int i = 0; i < count; ++i )
		{
			CHECK( onScreen( boxes[ i ], w, h ) );
			for( int j = i + 1; j < count; ++j )
				CHECK( !overlap( boxes[ i ], boxes[ j ] ) );
		}

		// the hand holds the hero and the cards, risen too, and nothing else of the row
		CHECK( inside( l.hero, l.hand ) );
		for( int c = 0; c < LANEHUD_CARDS; ++c )
		{
			CHECK( inside( l.cards[ c ], l.hand ) );
			CHECK( l.cards[ c ].y - LaneHud_px( LANEHUD_CARD_RISE, l.scale ) >= l.hand.y );
		}
		CHECK( !overlap( l.hand, l.deck ) );
		for( int t = 0; t < LANEHUD_TIERS; ++t )
			CHECK( !overlap( l.hand, l.tiers[ t ] ) );

		// what opens over the row stays under the top plate and clear of the risen cards
		const LaneHudBox abilities = LaneHud_abilitiesPanel( l, LANEHUD_ABILITY_ROWS );
		for( int p = 0; p < 2; ++p )
		{
			const LaneHudBox &panel = p == 0 ? l.upgradeRow : abilities;
			CHECK( onScreen( panel, w, h ) );
			CHECK( !overlap( panel, l.map ) && !overlap( panel, l.countdown ) && !overlap( panel, l.yourWave ) );
			const int rise = LaneHud_px( LANEHUD_CARD_RISE, l.scale );
			for( int c = 0; c < LANEHUD_CARDS; ++c )
				CHECK( !overlap( panel, LaneHud_box( l.cards[ c ].x, l.cards[ c ].y - rise, l.cards[ c ].w, l.cards[ c ].h + rise ) ) );
			CHECK( !overlap( panel, l.deck ) && !overlap( panel, l.abilitiesKey ) );
		}
		for( int c = 0; c < LANEHUD_UPGRADE_CELLS; ++c )
		{
			const LaneHudBox cell = LaneHud_upgradeCell( l, LANEHUD_UPGRADE_CELLS, c );
			CHECK( inside( cell, l.upgradeRow ) );
			if( c > 0 )
				CHECK( !overlap( cell, LaneHud_upgradeCell( l, LANEHUD_UPGRADE_CELLS, c - 1 ) ) );
		}
		for( int r = 0; r < LANEHUD_ABILITY_ROWS; ++r )
		{
			const LaneHudBox row = LaneHud_abilityRow( l, LANEHUD_ABILITY_ROWS, r );
			const LaneHudBox parts[ 3 ] = { LaneHud_at( row, l.abilityCameo ), LaneHud_at( row, l.abilityIcon ), LaneHud_at( row, l.abilityToggle ) };
			for( int k = 0; k < 3; ++k )
			{
				CHECK( inside( parts[ k ], row ) );
				for( int m = k + 1; m < 3; ++m )
					CHECK( !overlap( parts[ k ], parts[ m ] ) );
			}
			CHECK( inside( row, abilities ) );
		}

		// Tab's frames down the right edge, under the menu keys
		for( int i = 0; i < LANEHUD_PLOT_FRAMES; ++i )
		{
			CHECK( onScreen( l.plotFrame[ i ], w, h ) );
			CHECK( !overlap( l.plotFrame[ i ], l.menu ) && !overlap( l.plotFrame[ i ], l.diplomacy ) && !overlap( l.plotFrame[ i ], l.yourWave ) );
			if( i > 0 )
				CHECK( !overlap( l.plotFrame[ i ], l.plotFrame[ i - 1 ] ) );
		}
	}
}

TEST(lanehud_a_wider_screen_keeps_the_sizes_and_keeps_the_hud_centred)
{
	LaneHudLayout full, wide;
	LaneHud_layout( 1920, 1080, &full );
	LaneHud_layout( 2560, 1080, &wide );
	CHECK_EQ( wide.map.w, full.map.w );
	CHECK_EQ( wide.map.x, full.map.x + 320 );
	CHECK_EQ( wide.countdown.x, full.countdown.x + 320 );
	CHECK_EQ( wide.cards[ 0 ].x, full.cards[ 0 ].x + 320 );
	CHECK_EQ( wide.map.w, full.map.w );
	CHECK_EQ( wide.diplomacy.x + wide.diplomacy.w, 2560 - 6 );
}
