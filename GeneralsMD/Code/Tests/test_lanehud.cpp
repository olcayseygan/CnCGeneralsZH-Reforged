// test_lanehud.cpp: where the Lane War HUD's boxes stand (GameClient/LaneHudLayout.h).

#include "test_harness.h"

#include "GameClient/DirectStrikeLayout.h"
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
	// the match clock: the countdown chip's size, centred under the your-wave chip
	CHECK_EQ( l.matchClock.w, l.countdown.w );
	CHECK_EQ( l.matchClock.h, l.countdown.h );
	CHECK_EQ( l.matchClock.x + l.matchClock.w / 2, l.yourWave.x + l.yourWave.w / 2 );
	CHECK_EQ( l.matchClock.y, 82 );
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
	CHECK_EQ( l.menu.x, 1552 );
	CHECK_EQ( l.diplomacy.x + l.diplomacy.w, 1696 );
	// Tab's frames where README-v4 put them, from the top down the right edge
	CHECK_EQ( l.plotFrame[ 0 ].x, 1700 );
	CHECK_EQ( l.plotFrame[ 0 ].y, 8 );
	CHECK_EQ( l.plotFrame[ 0 ].w, 212 );
	CHECK_EQ( l.plotFrame[ 0 ].h, 80 );
	CHECK_EQ( l.plotFrame[ 2 ].y, 184 );

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
		LaneHudBox boxes[ 8 + LANEHUD_TIERS + 1 + LANEHUD_CARDS + 2 ];
		int count = 0;
		boxes[ count++ ] = l.money;
		boxes[ count++ ] = l.map;
		boxes[ count++ ] = l.yourWave;
		boxes[ count++ ] = l.waveBar;
		boxes[ count++ ] = l.countdown;
		boxes[ count++ ] = l.matchClock;
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
			CHECK( !overlap( panel, l.map ) && !overlap( panel, l.countdown ) && !overlap( panel, l.yourWave ) && !overlap( panel, l.matchClock ) );
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

		// Tab's frames down the right edge, beside the menu keys, clear of a full abilities panel
		for( int i = 0; i < LANEHUD_PLOT_FRAMES; ++i )
		{
			CHECK( onScreen( l.plotFrame[ i ], w, h ) );
			CHECK( !overlap( l.plotFrame[ i ], l.menu ) && !overlap( l.plotFrame[ i ], l.diplomacy ) && !overlap( l.plotFrame[ i ], l.yourWave ) );
			CHECK( !overlap( l.plotFrame[ i ], abilities ) );
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
	CHECK_EQ( wide.diplomacy.x + wide.diplomacy.w, 2560 - 224 );
	CHECK_EQ( wide.plotFrame[ 0 ].x + wide.plotFrame[ 0 ].w, 2560 - 8 );
}

// The Direct Strike lobby, GameClient/DirectStrikeLayout.h: README-lobby.md's rects on the design's 1920x1080

static bool within( const DirectStrikeRect &in, const DirectStrikeRect &out )
{
	return in.x0 >= out.x0 && in.y0 >= out.y0 && in.x1 <= out.x1 && in.y1 <= out.y1;
}

static bool meet( const DirectStrikeRect &a, const DirectStrikeRect &b )
{
	return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static const DirectStrikeRect DESIGN = { 0, 0, 1920, 1080 };

TEST(dslobby_the_frame_scales_by_the_short_side_and_centres)
{
	const DirectStrikeFrame small = DirectStrike_frame( 1280, 720 );
	CHECK( small.k > 0.666f && small.k < 0.667f );
	CHECK_EQ( small.ox, 0 );
	CHECK_EQ( small.oy, 0 );
	const DirectStrikeFrame wide = DirectStrike_frame( 2560, 1080 );
	CHECK_EQ( wide.ox, 320 );
	CHECK_EQ( wide.oy, 0 );
	const LaneHudBox card = DirectStrike_box( wide, DirectStrikeLobby_card( 0 ) );
	CHECK_EQ( card.x, 320 + DSLOBBY_TEAM_X0 );
	CHECK_EQ( card.w, 600 );
	float x, y;
	DirectStrike_toDesign( small, 640, 360, &x, &y );
	CHECK( x > 959.9f && x < 960.1f && y > 539.9f && y < 540.1f );
}

TEST(dslobby_team_two_is_team_ones_mirror_and_its_lamps_face_the_middle)
{
	for( int row = 0; row < DSLOBBY_ROWS; ++row )
	{
		const int left = DirectStrikeLobby_seat( 0, row ), right = DirectStrikeLobby_seat( 1, row );
		CHECK_EQ( DirectStrikeLobby_team( left ), 0 );
		CHECK_EQ( DirectStrikeLobby_team( right ), 1 );
		CHECK_EQ( DirectStrikeLobby_row( right ), row );
		const DirectStrikeRect a = DirectStrikeLobby_card( left ), b = DirectStrikeLobby_card( right );
		for( int lights = 0; lights < 2; ++lights )
			for( int part = 0; part < DSSEAT_PARTS; ++part )
			{
				const bool drawn = lights || ( part != DSSEAT_LAMP && part != DSSEAT_PING );
				const DirectStrikeRect p = DirectStrikeLobby_part( left, (DirectStrikeSeatPart)part, lights != 0 );
				const DirectStrikeRect q = DirectStrikeLobby_part( right, (DirectStrikeSeatPart)part, lights != 0 );
				CHECK( within( p, a ) && within( q, b ) );
				CHECK_EQ( p.x0 - a.x0, b.x1 - q.x1 );
				CHECK_EQ( p.y0, q.y0 );
				for( int other = 0; other < part && drawn; ++other )
					if( lights || ( other != DSSEAT_LAMP && other != DSSEAT_PING ) )
						CHECK( !meet( p, DirectStrikeLobby_part( left, (DirectStrikeSeatPart)other, lights != 0 ) ) );
			}
		CHECK( DirectStrikeLobby_part( left, DSSEAT_LAMP ).x0 > DirectStrikeLobby_part( left, DSSEAT_PORTRAIT ).x1 );
		CHECK( DirectStrikeLobby_part( right, DSSEAT_LAMP ).x1 < DirectStrikeLobby_part( right, DSSEAT_PORTRAIT ).x0 );
		// the lights strip holds the lamp and the ping and stays clear of the line and the chips
		CHECK( within( DirectStrikeLobby_part( left, DSSEAT_LAMP ), DirectStrikeLobby_strip( left ) ) );
		CHECK( within( DirectStrikeLobby_part( right, DSSEAT_PING ), DirectStrikeLobby_strip( right ) ) );
		CHECK( !meet( DirectStrikeLobby_part( left, DSSEAT_LINE ), DirectStrikeLobby_strip( left ) ) );
		CHECK( !meet( DirectStrikeLobby_part( right, DSSEAT_COLOR ), DirectStrikeLobby_strip( right ) ) );
		for( int k = 0; k < DSLOBBY_LIP_SEGS; ++k )
		{
			CHECK( within( DirectStrikeLobby_lipSeg( left, k ), a ) && within( DirectStrikeLobby_lipSeg( right, k ), b ) );
			CHECK( !meet( DirectStrikeLobby_lipSeg( left, k ), DirectStrikeLobby_part( left, DSSEAT_LINE ) ) );
		}
		// the pointed plates reach toward the centre column and stop short of it
		const DirectStrikeRect pa = DirectStrikeLobby_pointed( a, 0, DSLOBBY_SPEAR ), pb = DirectStrikeLobby_pointed( b, 1, DSLOBBY_SPEAR );
		CHECK_EQ( pa.x1, a.x1 + DSLOBBY_SPEAR );
		CHECK_EQ( pb.x0, b.x0 - DSLOBBY_SPEAR );
		CHECK( !meet( pa, DirectStrikeLobby_islandPanel() ) && !meet( pb, DirectStrikeLobby_islandPanel() ) );
		CHECK( !meet( pa, DirectStrikeLobby_chat() ) && !meet( pb, DirectStrikeLobby_chat() ) );
		CHECK( !meet( pa, DirectStrikeLobby_back() ) && !meet( pb, DirectStrikeLobby_start() ) );
	}
	for( int team = 0; team < DSLOBBY_TEAMS; ++team )
	{
		const DirectStrikeRect head = DirectStrikeLobby_teamHead( team );
		CHECK( !meet( DirectStrikeLobby_pointed( head, team, DSLOBBY_HEAD_SPEAR ), DirectStrikeLobby_islandChip() ) );
		for( int i = 0; i < DSLOBBY_HEAD_LEDS; ++i )
			CHECK( within( DirectStrikeLobby_headLed( team, i ), head ) );
		for( int i = 0; i < DSLOBBY_HEAD_CHEVRONS; ++i )
			CHECK( within( DirectStrikeLobby_headChevron( team, i ), head ) );
		CHECK( within( DirectStrikeLobby_titleChevron( team, 0 ), DirectStrikeLobby_title() ) );
		CHECK( within( DirectStrikeLobby_titleChevron( team, 1 ), DirectStrikeLobby_title() ) );
	}
}

TEST(dslobby_every_seat_is_a_full_card_and_the_tug_bar_meets_in_the_middle)
{
	for( int seat = 0; seat < DSLOBBY_SEATS; ++seat )
		CHECK_EQ( DirectStrikeLobby_card( seat ).y1 - DirectStrikeLobby_card( seat ).y0, DSLOBBY_CARD_H );
	CHECK_EQ( DirectStrikeLobby_card( 4 ).y0, DirectStrikeLobby_card( 2 ).y1 + DSLOBBY_CARD_GAP );
	const DirectStrikeRect console = DirectStrikeLobby_console(), tug = DirectStrikeLobby_tug();
	CHECK( within( tug, console ) && within( console, DESIGN ) );
	CHECK_EQ( console.y1, tug.y1 + DSLOBBY_CONSOLE_FOOT );
	CHECK_EQ( tug.y0, DirectStrikeLobby_start().y1 + DSLOBBY_TUG_GAP );
	for( int i = 0; i < DSLOBBY_TUG_SEGS; ++i )
	{
		const DirectStrikeRect a = DirectStrikeLobby_tugSeg( 0, i ), b = DirectStrikeLobby_tugSeg( 1, i );
		CHECK( within( a, tug ) && within( b, tug ) );
		CHECK( a.x1 < 958.0f && b.x0 > 962.0f );	// clear of the gold line in the middle
		CHECK_EQ( a.x0 - tug.x0, tug.x1 - b.x1 );
	}
}

TEST(dslobby_a_seat_is_its_start_and_empty_slots_fill_the_rest)
{
	int seatOf[ DSLOBBY_SEATS ], slotAt[ DSLOBBY_SEATS ];
	// a fresh room: nobody has a start, every slot on its own seat
	const int fresh[ DSLOBBY_SEATS ] = { -1, -1, -1, -1, -1, -1 };
	DirectStrikeLobby_seats( fresh, seatOf, slotAt );
	for( int i = 0; i < DSLOBBY_SEATS; ++i )
		CHECK( seatOf[ i ] == i && slotAt[ i ] == i );
	// the host (slot 0) moved to seat 3: slot 3, empty, shows on the seat he left
	const int moved[ DSLOBBY_SEATS ] = { 3, 1, -1, -1, -1, -1 };
	DirectStrikeLobby_seats( moved, seatOf, slotAt );
	CHECK_EQ( seatOf[ 0 ], 3 );
	CHECK_EQ( seatOf[ 3 ], 0 );
	CHECK( seatOf[ 1 ] == 1 && seatOf[ 2 ] == 2 && seatOf[ 4 ] == 4 && seatOf[ 5 ] == 5 );
	// slot 3 fills there (a joiner, its start not given yet: -1 like an empty slot) and stays where it was shown;
	// once FitDirectStrikeRoom gives it that seat as its start, nothing moves
	const int seated[ DSLOBBY_SEATS ] = { 3, 1, -1, 0, -1, -1 };
	DirectStrikeLobby_seats( seated, seatOf, slotAt );
	CHECK( seatOf[ 0 ] == 3 && seatOf[ 3 ] == 0 && seatOf[ 2 ] == 2 && seatOf[ 4 ] == 4 );
	// two claim one start: the first slot keeps it, the second takes a free seat; every seat shows one slot
	const int clash[ DSLOBBY_SEATS ] = { 2, -1, 2, -1, 9, -1 };
	DirectStrikeLobby_seats( clash, seatOf, slotAt );
	CHECK_EQ( seatOf[ 0 ], 2 );
	CHECK_NE( seatOf[ 2 ], 2 );
	for( int seat = 0; seat < DSLOBBY_SEATS; ++seat )
		CHECK( slotAt[ seat ] >= 0 && seatOf[ slotAt[ seat ] ] == seat );
}

TEST(dslobby_the_block_is_centred_and_its_columns_share_top_and_foot)
{
	CHECK_EQ( DirectStrikeLobby_console().y0, DirectStrikeLobby_title().y0 + DSLOBBY_TITLE_H / 2 );
	CHECK_EQ( DirectStrikeLobby_title().y0, 1080 - DirectStrikeLobby_console().y1 );	// the title's top to the console's foot, centred
	CHECK_EQ( DirectStrikeLobby_teamHead( 0 ).y0, DirectStrikeLobby_islandPanel().y0 );
	CHECK_EQ( DirectStrikeLobby_teamHead( 1 ).y0, DirectStrikeLobby_islandChip().y0 );
	CHECK_EQ( DirectStrikeLobby_teamHead( 1 ).y1, DirectStrikeLobby_islandChip().y1 );
	CHECK_EQ( DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, DSLOBBY_ROWS - 1 ) ).y1, DirectStrikeLobby_start().y1 );
	CHECK_EQ( DirectStrikeLobby_card( DirectStrikeLobby_seat( 1, DSLOBBY_ROWS - 1 ) ).y1, DirectStrikeLobby_back().y1 );
	// the chat over the first two seat rows, the input on the third's top, the offline panel down to the input's foot
	CHECK_EQ( DirectStrikeLobby_chat().y0, DirectStrikeLobby_card( 0 ).y0 );
	CHECK_EQ( DirectStrikeLobby_chat().y1, DirectStrikeLobby_card( 2 ).y1 );
	CHECK_EQ( DirectStrikeLobby_chatInput().y0, DirectStrikeLobby_card( 4 ).y0 );
	CHECK_EQ( DirectStrikeLobby_islandPanel().y1, DirectStrikeLobby_chatInput().y1 );
	CHECK( within( DirectStrikeLobby_island(), DirectStrikeLobby_islandPanel() ) );
	CHECK( within( DirectStrikeLobby_chatGlass(), DirectStrikeLobby_chat() ) );
	CHECK( within( DirectStrikeLobby_islandSmall(), DirectStrikeLobby_islandWell() ) );
	CHECK( within( DirectStrikeLobby_islandWell(), DirectStrikeLobby_islandChip() ) );
	CHECK( DirectStrikeLobby_chatInput().y1 < DirectStrikeLobby_start().y0 );
	CHECK_EQ( DirectStrikeLobby_size().y1, DirectStrikeLobby_start().y1 );
	CHECK_EQ( DirectStrikeLobby_speed().y1, DirectStrikeLobby_start().y1 );
	CHECK_EQ( DirectStrikeLobby_speed().x0, DirectStrikeLobby_size().x1 + DSLOBBY_ROW_GAP );
	CHECK_EQ( DirectStrikeLobby_start().x0, DirectStrikeLobby_speed().x1 + DSLOBBY_ROW_GAP );
	CHECK_EQ( DirectStrikeLobby_start().x1, DirectStrikeLobby_islandPanel().x1 );
	// START in the figure type, about 130 wide, and the longest translation, about 160, still fit
	CHECK( DirectStrikeLobby_start().x1 - DirectStrikeLobby_start().x0 >= 180.0f );
}

TEST(dslobby_every_open_list_stays_on_the_screen_and_holds_its_rows)
{
	const DirectStrikeRect offline[] =
	{
		DirectStrikeLobby_title(), DirectStrikeLobby_islandPanel(), DirectStrikeLobby_back(), DirectStrikeLobby_size(),
		DirectStrikeLobby_speed(), DirectStrikeLobby_start(),
	};
	const DirectStrikeRect network[] =
	{
		DirectStrikeLobby_title(), DirectStrikeLobby_islandChip(), DirectStrikeLobby_chat(), DirectStrikeLobby_chatInput(),
		DirectStrikeLobby_send(), DirectStrikeLobby_back(), DirectStrikeLobby_size(), DirectStrikeLobby_speed(), DirectStrikeLobby_start(),
	};
	CHECK( within( DirectStrikeLobby_speedList(), DESIGN ) );
	CHECK( !meet( DirectStrikeLobby_speedList(), DirectStrikeLobby_speed() ) );
	for( int row = 0; row < DSLOBBY_SPEEDS; ++row )
		CHECK( within( DirectStrikeLobby_speedRow( row ), DirectStrikeLobby_speedList() ) );
	CHECK( within( DirectStrikeLobby_sizeList(), DESIGN ) );
	CHECK( !meet( DirectStrikeLobby_sizeList(), DirectStrikeLobby_size() ) );
	for( int row = 0; row < DSLOBBY_SIZES; ++row )
		CHECK( within( DirectStrikeLobby_sizeRow( row ), DirectStrikeLobby_sizeList() ) );
	for( int i = 0; i < (int)( sizeof( offline ) / sizeof( offline[ 0 ] ) ); ++i )
		for( int j = 0; j < i; ++j )
			CHECK( !meet( offline[ i ], offline[ j ] ) );
	for( int i = 0; i < (int)( sizeof( network ) / sizeof( network[ 0 ] ) ); ++i )
		for( int j = 0; j < i; ++j )
			CHECK( !meet( network[ i ], network[ j ] ) );
	for( int slot = 0; slot < DSLOBBY_SEATS; ++slot )
	{
		const DirectStrikeRect seats = DirectStrikeLobby_seatList( slot );
		CHECK( within( seats, DESIGN ) );
		for( int row = 0; row < DSLOBBY_SEAT_ROWS; ++row )
			CHECK( within( DirectStrikeLobby_seatRow( slot, row ), seats ) );

		const DirectStrikeRect picker = DirectStrikeLobby_picker( slot );
		const DirectStrikeRect chip = DirectStrikeLobby_part( slot, DSSEAT_ARMY );
		CHECK( within( picker, DESIGN ) );
		CHECK( !meet( picker, chip ) );
		if( picker.x0 != DirectStrikeLobby_islandPanel().x0 )
			CHECK_EQ( DirectStrikeLobby_team( slot ) == 0 ? picker.x1 : picker.x0, DirectStrikeLobby_team( slot ) == 0 ? chip.x1 : chip.x0 );
		CHECK( within( DirectStrikeLobby_pickerRandom( slot ), picker ) );
		for( int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
			for( int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
			{
				const DirectStrikeRect t = DirectStrikeLobby_pickerTile( slot, column, tile );
				CHECK( within( t, picker ) );
				CHECK( !meet( t, DirectStrikeLobby_pickerRandom( slot ) ) );
			}

		const DirectStrikeRect colors = DirectStrikeLobby_colorList( slot, 8 );
		CHECK( within( colors, DESIGN ) );
		for( int c = 0; c < 8; ++c )
			CHECK( within( DirectStrikeLobby_colorCell( slot, 8, c ), colors ) );
	}
}
