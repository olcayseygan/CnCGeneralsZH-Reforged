// test_directstrikehud.cpp: where the Direct Strike HUD's boxes stand (GameClient/DirectStrikeHudLayout.h).

#include "test_harness.h"

#include "GameClient/DirectStrikeLayout.h"
#include "GameClient/DirectStrikeHudLayout.h"

static bool overlap( const DirectStrikeHudBox &a, const DirectStrikeHudBox &b )
{
	return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static bool onScreen( const DirectStrikeHudBox &b, int w, int h )
{
	return b.x >= 0 && b.y >= 0 && b.x + b.w <= w && b.y + b.h <= h && b.w > 0 && b.h > 0;
}

static bool inside( const DirectStrikeHudBox &in, const DirectStrikeHudBox &out )
{
	return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

// 16:9 and wider only: the owner ruled squarer screens out on 2026-11-20
static const int SCREENS[][ 2 ] = { { 1920, 1080 }, { 1280, 720 }, { 1366, 768 }, { 2560, 1440 }, { 2560, 1080 }, { 3440, 1440 }, { 3840, 2160 } };

TEST(direct_strike_hud_at_1080_the_boxes_stand_where_the_art_drew_them)
{
	DirectStrikeHudLayout l;
	DirectStrikeHud_layout( 1920, 1080, &l );
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
	CHECK_EQ( DirectStrikeHud_upgradeCell( l, 3, 2 ).x + DirectStrikeHud_upgradeCell( l, 3, 2 ).w, 1513 );
	CHECK_EQ( DirectStrikeHud_upgradeCell( l, 3, 0 ).x, 1513 - 3 * 106 + 6 );
	CHECK_EQ( l.menu.x, 1552 );
	CHECK_EQ( l.diplomacy.x + l.diplomacy.w, 1696 );
	// Tab's frames where README-v4 put them, from the top down the right edge
	CHECK_EQ( l.plotFrame[ 0 ].x, 1700 );
	CHECK_EQ( l.plotFrame[ 0 ].y, 8 );
	CHECK_EQ( l.plotFrame[ 0 ].w, 212 );
	CHECK_EQ( l.plotFrame[ 0 ].h, 80 );
	CHECK_EQ( l.plotFrame[ 2 ].y, 184 );

	// six rows fill the art's panel exactly: 1408,646 to 1600,950, a row's parts where the art put them
	const DirectStrikeHudBox abilities = DirectStrikeHud_abilitiesPanel( l, 6 );
	CHECK_EQ( abilities.x, 1408 );
	CHECK_EQ( abilities.y, 646 );
	CHECK_EQ( abilities.w, 192 );
	CHECK_EQ( abilities.h, 304 );
	const DirectStrikeHudBox row = DirectStrikeHud_abilityRow( l, 6, 1 );
	CHECK_EQ( row.y, 654 + 48 );
	CHECK_EQ( DirectStrikeHud_at( row, l.abilityCameo ).x, 1422 );
	CHECK_EQ( DirectStrikeHud_at( row, l.abilityIcon ).x, 1478 );
	CHECK_EQ( DirectStrikeHud_at( row, l.abilityToggle ).x, 1540 );
	CHECK_EQ( DirectStrikeHud_at( row, l.abilityToggle ).y, 654 + 48 + 13 );
}

TEST(direct_strike_hud_the_strip_map_fills_the_lane_chips_glass)
{
	for( const auto &size : SCREENS )
	{
		DirectStrikeHudLayout l;
		DirectStrikeHud_layout( size[ 0 ], size[ 1 ], &l );
		CHECK( inside( l.lane, l.map ) );
		CHECK( l.lane.w > 8 * l.lane.h && l.lane.w < 9 * l.lane.h );
	}
}

TEST(direct_strike_hud_no_box_leaves_the_screen_or_meets_another)
{
	for( const auto &size : SCREENS )
	{
		const int w = size[ 0 ], h = size[ 1 ];
		DirectStrikeHudLayout l;
		DirectStrikeHud_layout( w, h, &l );

		// the boxes that stand at once: the top chips, the menu keys and the bottom row
		DirectStrikeHudBox boxes[ 8 + DSHUD_TIERS + 1 + DSHUD_CARDS + 3 ];
		int count = 0;
		boxes[ count++ ] = l.money;
		boxes[ count++ ] = l.map;
		boxes[ count++ ] = l.yourWave;
		boxes[ count++ ] = l.waveBar;
		boxes[ count++ ] = l.countdown;
		boxes[ count++ ] = l.matchClock;
		boxes[ count++ ] = l.menu;
		boxes[ count++ ] = l.diplomacy;
		for( int t = 0; t < DSHUD_TIERS; ++t )
			boxes[ count++ ] = l.tiers[ t ];
		boxes[ count++ ] = l.hero;
		for( int c = 0; c < DSHUD_CARDS; ++c )
			boxes[ count++ ] = l.cards[ c ];
		boxes[ count++ ] = l.deck;
		boxes[ count++ ] = l.abilitiesKey;
		boxes[ count++ ] = l.revealKey;
		for( int i = 0; i < count; ++i )
		{
			CHECK( onScreen( boxes[ i ], w, h ) );
			for( int j = i + 1; j < count; ++j )
				CHECK( !overlap( boxes[ i ], boxes[ j ] ) );
		}

		// the hand holds the hero and the cards, risen too, and nothing else of the row
		CHECK( inside( l.hero, l.hand ) );
		for( int c = 0; c < DSHUD_CARDS; ++c )
		{
			CHECK( inside( l.cards[ c ], l.hand ) );
			CHECK( l.cards[ c ].y - DirectStrikeHud_px( DSHUD_CARD_RISE, l.scale ) >= l.hand.y );
		}
		CHECK( !overlap( l.hand, l.deck ) );
		for( int t = 0; t < DSHUD_TIERS; ++t )
			CHECK( !overlap( l.hand, l.tiers[ t ] ) );

		// what opens over the row stays under the top plate and clear of the risen cards
		const DirectStrikeHudBox abilities = DirectStrikeHud_abilitiesPanel( l, DSHUD_ABILITY_ROWS );
		for( int p = 0; p < 2; ++p )
		{
			const DirectStrikeHudBox &panel = p == 0 ? l.upgradeRow : abilities;
			CHECK( onScreen( panel, w, h ) );
			CHECK( !overlap( panel, l.map ) && !overlap( panel, l.countdown ) && !overlap( panel, l.yourWave ) && !overlap( panel, l.matchClock ) );
			const int rise = DirectStrikeHud_px( DSHUD_CARD_RISE, l.scale );
			for( int c = 0; c < DSHUD_CARDS; ++c )
				CHECK( !overlap( panel, DirectStrikeHud_box( l.cards[ c ].x, l.cards[ c ].y - rise, l.cards[ c ].w, l.cards[ c ].h + rise ) ) );
			CHECK( !overlap( panel, l.deck ) && !overlap( panel, l.abilitiesKey ) && !overlap( panel, l.revealKey ) );
		}
		for( int c = 0; c < DSHUD_UPGRADE_CELLS; ++c )
		{
			const DirectStrikeHudBox cell = DirectStrikeHud_upgradeCell( l, DSHUD_UPGRADE_CELLS, c );
			CHECK( inside( cell, l.upgradeRow ) );
			if( c > 0 )
				CHECK( !overlap( cell, DirectStrikeHud_upgradeCell( l, DSHUD_UPGRADE_CELLS, c - 1 ) ) );
		}
		for( int r = 0; r < DSHUD_ABILITY_ROWS; ++r )
		{
			const DirectStrikeHudBox row = DirectStrikeHud_abilityRow( l, DSHUD_ABILITY_ROWS, r );
			const DirectStrikeHudBox parts[ 3 ] = { DirectStrikeHud_at( row, l.abilityCameo ), DirectStrikeHud_at( row, l.abilityIcon ), DirectStrikeHud_at( row, l.abilityToggle ) };
			for( int k = 0; k < 3; ++k )
			{
				CHECK( inside( parts[ k ], row ) );
				for( int m = k + 1; m < 3; ++m )
					CHECK( !overlap( parts[ k ], parts[ m ] ) );
			}
			CHECK( inside( row, abilities ) );
		}

		// Tab's frames down the right edge, beside the menu keys, clear of a full abilities panel
		for( int i = 0; i < DSHUD_PLOT_FRAMES; ++i )
		{
			CHECK( onScreen( l.plotFrame[ i ], w, h ) );
			CHECK( !overlap( l.plotFrame[ i ], l.menu ) && !overlap( l.plotFrame[ i ], l.diplomacy ) && !overlap( l.plotFrame[ i ], l.yourWave ) );
			CHECK( !overlap( l.plotFrame[ i ], abilities ) );
			if( i > 0 )
				CHECK( !overlap( l.plotFrame[ i ], l.plotFrame[ i - 1 ] ) );
		}
	}
}

TEST(direct_strike_hud_a_wider_screen_keeps_the_sizes_and_keeps_the_hud_centred)
{
	DirectStrikeHudLayout full, wide;
	DirectStrikeHud_layout( 1920, 1080, &full );
	DirectStrikeHud_layout( 2560, 1080, &wide );
	CHECK_EQ( wide.map.w, full.map.w );
	CHECK_EQ( wide.map.x, full.map.x + 320 );
	CHECK_EQ( wide.countdown.x, full.countdown.x + 320 );
	CHECK_EQ( wide.cards[ 0 ].x, full.cards[ 0 ].x + 320 );
	CHECK_EQ( wide.map.w, full.map.w );
	CHECK_EQ( wide.diplomacy.x + wide.diplomacy.w, 2560 - 224 );
	CHECK_EQ( wide.plotFrame[ 0 ].x + wide.plotFrame[ 0 ].w, 2560 - 8 );
}

// The Direct Strike lobby, GameClient/DirectStrikeLayout.h: README-ds-lobby.md's rects on the design's 1920x1080

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
	const DirectStrikeHudBox card = DirectStrike_box( wide, DirectStrikeLobby_card( 0 ) );
	CHECK_EQ( card.x, 320 + DSLOBBY_TEAM_X0 );
	CHECK_EQ( card.w, DSLOBBY_CARD_W );
	float x, y;
	DirectStrike_toDesign( small, 640, 360, &x, &y );
	CHECK( x > 959.9f && x < 960.1f && y > 539.9f && y < 540.1f );
}

TEST(dslobby_the_rects_are_the_designs)
{
	// README-ds-lobby.md's table at 1920x1080
	const DirectStrikeRect panel = DirectStrikeLobby_panel();
	CHECK( panel.x0 == 32 && panel.y0 == 28 && panel.x1 == 1888 && panel.y1 == 1052 );
	CHECK( DirectStrikeLobby_back().x0 == 1704 && DirectStrikeLobby_back().y0 == 48 && DirectStrikeLobby_back().y1 == 92 );
	CHECK( DirectStrikeLobby_teamHead( 1 ).x0 == 1310 && DirectStrikeLobby_teamHead( 1 ).y0 == 128 && DirectStrikeLobby_teamHead( 1 ).y1 == 172 );
	CHECK( DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, 1 ) ).y0 == 450 && DirectStrikeLobby_card( DirectStrikeLobby_seat( 1, 2 ) ).y0 == 712 );
	const int seat = DirectStrikeLobby_seat( 1, 0 );
	const DirectStrikeRect c = DirectStrikeLobby_card( seat );
	CHECK( DirectStrikeLobby_part( seat, DSSEAT_PORTRAIT ).x0 == c.x0 + 12 && DirectStrikeLobby_part( seat, DSSEAT_PORTRAIT ).x1 == c.x0 + 238 );
	CHECK( DirectStrikeLobby_part( seat, DSSEAT_LINE ).x0 == c.x0 + 256 && DirectStrikeLobby_part( seat, DSSEAT_LINE ).x1 == c.x0 + 538 );
	CHECK( DirectStrikeLobby_part( seat, DSSEAT_ARMY ).y0 == c.y0 + 97 && DirectStrikeLobby_part( seat, DSSEAT_COLOR ).y1 == c.y0 + 238 );
	CHECK( DirectStrikeLobby_island( false ).y1 == 708 && DirectStrikeLobby_islandKey( false ).y0 == 674 );
	CHECK( DirectStrikeLobby_island( true ).y1 == 344 && DirectStrikeLobby_islandKey( true ).y0 == 310 );
	CHECK( DirectStrikeLobby_size( false ).x1 == 952 && DirectStrikeLobby_speed( false ).x0 == 968 && DirectStrikeLobby_size( false ).y0 == 772 );
	CHECK( DirectStrikeLobby_start( false ).y0 == 840 && DirectStrikeLobby_start( true ).y0 == 852 );
	CHECK( DirectStrikeLobby_chatInput().x1 == 1150 && DirectStrikeLobby_send().x0 == 1160 );
	// the seats' foot is START's in both rooms
	CHECK_EQ( DirectStrikeLobby_card( DirectStrikeLobby_seat( 0, DSLOBBY_ROWS - 1 ) ).y1, DirectStrikeLobby_start( false ).y1 );
	CHECK_EQ( DirectStrikeLobby_card( DirectStrikeLobby_seat( 1, DSLOBBY_ROWS - 1 ) ).y1, DirectStrikeLobby_start( true ).y1 );
	// the fortress stands 0.7 of the half-lane from the middle, the starts 26 cells past the lane's end
	CHECK( DirectStrikeLobby_fortressShare() > 0.611f && DirectStrikeLobby_fortressShare() < 0.612f );
}

TEST(dslobby_both_teams_are_laid_out_alike_and_their_parts_never_meet)
{
	const float levels[ DSLOBBY_LEVELS ] = { 34, 60, 34, 46 };		// "Easy" .. "Brutal" at 16 px
	const float longLevels[ DSLOBBY_LEVELS ] = { 40, 32, 24, 64 };	// "Kolay" .. "Acımasız"
	for( int row = 0; row < DSLOBBY_ROWS; ++row )
	{
		const int left = DirectStrikeLobby_seat( 0, row ), right = DirectStrikeLobby_seat( 1, row );
		CHECK_EQ( DirectStrikeLobby_team( left ), 0 );
		CHECK_EQ( DirectStrikeLobby_team( right ), 1 );
		CHECK_EQ( DirectStrikeLobby_row( right ), row );
		const DirectStrikeRect a = DirectStrikeLobby_card( left ), b = DirectStrikeLobby_card( right );
		CHECK_EQ( b.x0 - a.x0, 1250.0f );
		for( int part = 0; part < DSSEAT_PARTS; ++part )
		{
			const DirectStrikeRect p = DirectStrikeLobby_part( left, (DirectStrikeSeatPart)part );
			const DirectStrikeRect q = DirectStrikeLobby_part( right, (DirectStrikeSeatPart)part );
			CHECK( within( p, a ) && within( q, b ) );
			CHECK_EQ( q.x0 - p.x0, 1250.0f );
			CHECK_EQ( p.y0, q.y0 );
			for( int other = 0; other < part; ++other )
				if( part != DSSEAT_TAG )
					CHECK( !meet( p, DirectStrikeLobby_part( left, (DirectStrikeSeatPart)other ) ) );
		}
		// the readiness tag sits on the picture's top, clear of the lines beside it
		CHECK( within( DirectStrikeLobby_part( left, DSSEAT_TAG ), DirectStrikeLobby_part( left, DSSEAT_PORTRAIT ) ) );
		CHECK( !meet( DirectStrikeLobby_part( left, DSSEAT_TAG ), DirectStrikeLobby_part( left, DSSEAT_LINE ) ) );
		// a client's TAKE SEAT and the host's level keys take the army and colour lines' place, in order, apart
		CHECK( within( DirectStrikeLobby_takeSeat( left ), a ) && !meet( DirectStrikeLobby_takeSeat( left ), DirectStrikeLobby_part( left, DSSEAT_LINE ) ) );
		for( int words = 0; words < 2; ++words )
			for( int i = 0; i < DSLOBBY_LEVELS; ++i )
			{
				const float *w = words == 0 ? levels : longLevels;
				const DirectStrikeRect key = DirectStrikeLobby_levelKey( right, i, w );
				CHECK( within( key, DirectStrikeLobby_part( right, DSSEAT_COLOR ) ) );
				CHECK( key.x1 - key.x0 >= w[ i ] + 8.0f );
				if( i > 0 )
					CHECK_EQ( key.x0, DirectStrikeLobby_levelKey( right, i - 1, w ).x1 + DSLOBBY_LEVEL_GAP );
			}
		CHECK_EQ( DirectStrikeLobby_levelKey( left, DSLOBBY_LEVELS - 1, levels ).x1, DirectStrikeLobby_part( left, DSSEAT_COLOR ).x1 );
		// the wings stay clear of the centre column
		CHECK( a.x1 < DSLOBBY_CENTRE_X0 && b.x0 > DSLOBBY_CENTRE_X1 );
	}
	for( int team = 0; team < DSLOBBY_TEAMS; ++team )
	{
		const DirectStrikeRect head = DirectStrikeLobby_teamHead( team );
		CHECK( head.y1 + 6 < DirectStrikeLobby_card( DirectStrikeLobby_seat( team, 0 ) ).y0 );
		CHECK( head.y0 > DSLOBBY_RULE_Y + 5 );
		CHECK( !meet( head, DirectStrikeLobby_island( false ) ) );
	}
}

TEST(dslobby_the_lane_fills_from_each_fortress_and_its_status_hides_no_segment)
{
	const DirectStrikeRect lane = DirectStrikeLobby_lane(), track = DirectStrikeLobby_track(), status = DirectStrikeLobby_status();
	CHECK( within( track, lane ) && within( lane, DirectStrikeLobby_panel() ) );
	CHECK( lane.y0 > DirectStrikeLobby_start( false ).y1 );
	// the network room's status over the track's middle, clear of every segment (the design's box hid one)
	CHECK( within( status, lane ) );
	CHECK( status.y1 <= track.y0 );
	for( int size = 1; size <= DSLOBBY_ROWS; ++size )
		for( int i = 0; i < size; ++i )
		{
			const DirectStrikeRect a = DirectStrikeLobby_laneSeg( 0, i, size ), b = DirectStrikeLobby_laneSeg( 1, i, size );
			CHECK( within( a, track ) && within( b, track ) );
			CHECK( a.x1 < 958.0f && b.x0 > 962.0f );	// clear of the white line in the middle
			CHECK( a.x0 - track.x0 < track.x1 - b.x1 + 0.01f && a.x0 - track.x0 > track.x1 - b.x1 - 0.01f );
			CHECK( !meet( a, status ) && !meet( b, status ) );
			if( i > 0 )
				CHECK( !meet( a, DirectStrikeLobby_laneSeg( 0, i - 1, size ) ) );
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

TEST(dslobby_the_centre_column_parts_never_meet_and_stand_in_the_column)
{
	for( int net = 0; net < 2; ++net )
	{
		const bool network = net != 0;
		const DirectStrikeRect column = DirectStrike_rect( DSLOBBY_CENTRE_X0, DSLOBBY_HEAD_Y, DSLOBBY_CENTRE_X1, DSLOBBY_FOOT );
		const DirectStrikeRect parts[] =
		{
			DirectStrikeLobby_island( network ), DirectStrikeLobby_caption( network ), DirectStrikeLobby_size( network ),
			DirectStrikeLobby_speed( network ), DirectStrikeLobby_start( network ),
			network ? DirectStrikeLobby_chat() : DirectStrikeLobby_island( network ),
			network ? DirectStrikeLobby_chatInput() : DirectStrikeLobby_island( network ),
			network ? DirectStrikeLobby_send() : DirectStrikeLobby_island( network ),
		};
		const int count = network ? 8 : 5;
		for( int i = 0; i < count; ++i )
		{
			CHECK( within( parts[ i ], column ) );
			for( int j = 0; j < i; ++j )
				CHECK( !meet( parts[ i ], parts[ j ] ) );
		}
		CHECK( within( DirectStrikeLobby_islandKey( network ), DirectStrikeLobby_island( network ) ) );
		CHECK( within( DirectStrikeLobby_mixed( network ), DirectStrikeLobby_caption( network ) ) );
		CHECK_EQ( DirectStrikeLobby_speed( network ).x0, DirectStrikeLobby_size( network ).x1 + DSLOBBY_SETUP_GAP );
		CHECK_EQ( DirectStrikeLobby_speed( network ).x1, DirectStrikeLobby_start( network ).x1 );
		// START's spaced word, about 300 wide, fits with room for the longest translation
		CHECK( DirectStrikeLobby_start( network ).x1 - DirectStrikeLobby_start( network ).x0 >= 500.0f );
	}
	// the header's three parts apart
	CHECK( !meet( DirectStrikeLobby_title(), DirectStrikeLobby_back() ) );
	CHECK( DirectStrikeLobby_back().y1 < DSLOBBY_RULE_Y );
}

TEST(dslobby_every_open_list_stays_on_the_panel_and_never_touches_start)
{
	for( int net = 0; net < 2; ++net )
	{
		const bool network = net != 0;
		const DirectStrikeRect panel = DirectStrikeLobby_panel(), start = DirectStrikeLobby_start( network );
		const DirectStrikeRect size = DirectStrikeLobby_sizeList( network ), speed = DirectStrikeLobby_speedList( network );
		CHECK( within( size, panel ) && within( speed, panel ) );
		CHECK( size.y1 <= DirectStrikeLobby_size( network ).y0 && speed.y1 <= DirectStrikeLobby_speed( network ).y0 );	// both open up
		CHECK( !meet( size, start ) && !meet( speed, start ) );
		for( int row = 0; row < DSLOBBY_SIZES; ++row )
			CHECK( within( DirectStrikeLobby_listRow( size, row ), size ) );
		for( int row = 0; row < DSLOBBY_SPEEDS; ++row )
			CHECK( within( DirectStrikeLobby_listRow( speed, row ), speed ) );
		for( int seat = 0; seat < DSLOBBY_SEATS; ++seat )
		{
			const int team = DirectStrikeLobby_team( seat );
			const DirectStrikeRect wing = DirectStrike_rect( DirectStrikeLobby_teamX( team ), DSLOBBY_HEAD_Y,
				DirectStrikeLobby_teamX( team ) + DSLOBBY_CARD_W, DSLOBBY_PANEL_Y1 );
			for( int rows = DSLOBBY_HUMAN_SEAT_ROWS; rows <= DSLOBBY_SEAT_ROWS; rows += DSLOBBY_SEAT_ROWS - DSLOBBY_HUMAN_SEAT_ROWS )
			{
				const DirectStrikeRect seats = DirectStrikeLobby_seatList( seat, rows );
				CHECK( within( seats, panel ) && within( seats, wing ) && !meet( seats, start ) );
				CHECK( !meet( seats, DirectStrikeLobby_part( seat, DSSEAT_LINE ) ) );
				for( int row = 0; row < rows; ++row )
					CHECK( within( DirectStrikeLobby_listRow( seats, row ), seats ) );
			}

			// the picker inside its team's wing, so it never reaches the island or START
			const DirectStrikeRect picker = DirectStrikeLobby_picker( seat );
			CHECK( within( picker, panel ) && within( picker, wing ) && !meet( picker, start ) );
			CHECK( within( DirectStrikeLobby_pickerRandom( seat ), picker ) );
			for( int column = 0; column < DSLOBBY_ARMY_COLUMNS; ++column )
				for( int tile = 0; tile < DSLOBBY_ARMY_TILES; ++tile )
				{
					const DirectStrikeRect t = DirectStrikeLobby_pickerTile( seat, column, tile );
					CHECK( within( t, picker ) );
					CHECK( !meet( t, DirectStrikeLobby_pickerRandom( seat ) ) );
					if( tile > 0 )
						CHECK( !meet( t, DirectStrikeLobby_pickerTile( seat, column, tile - 1 ) ) );
					if( column > 0 )
						CHECK( !meet( t, DirectStrikeLobby_pickerTile( seat, column - 1, tile ) ) );
				}
			// the first row's picker opens down, under its chip
			if( DirectStrikeLobby_row( seat ) == 0 )
				CHECK( picker.y0 >= DirectStrikeLobby_part( seat, DSSEAT_ARMY ).y1 );

			for( int colors = 1; colors <= 9; colors += 4 )
			{
				const DirectStrikeRect list = DirectStrikeLobby_colorList( seat, colors );
				// from the colour line's left as drawn, so it reaches 10 past the wing into the gap, never the island
				CHECK( within( list, panel ) && !meet( list, start ) && !meet( list, DirectStrikeLobby_island( network ) ) );
				CHECK( list.x1 <= wing.x1 + 10.0f );
				for( int c = 0; c < colors; ++c )
				{
					CHECK( within( DirectStrikeLobby_colorCell( seat, colors, c ), list ) );
					CHECK( DirectStrikeLobby_colorCell( seat, colors, c ).y1 <= list.y1 - DSLOBBY_COLOR_NAME_H );
				}
			}
		}
	}
}
