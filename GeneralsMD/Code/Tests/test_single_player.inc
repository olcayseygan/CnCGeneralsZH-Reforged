// The single player's screens (docs/plans/MENU-REDESIGN.md, package 12): Generals' Challenge, the
// replay list, Load Game and their two boxes, against the rects of
// experiments/proofs/2026-11-24/screens/single-player/rects.txt in the wrapper, at 1920x1080.
// Included into test_gameengine.cpp.

#include "GameClient/SinglePlayerLayout.h"

static void singlePlayerCheckRect( const IRegion2D &r, Int left, Int top, Int right, Int bottom )
{
	CHECK_EQ( r.lo.x, left );
	CHECK_EQ( r.lo.y, top );
	CHECK_EQ( r.hi.x, right );
	CHECK_EQ( r.hi.y, bottom );
}

TEST( single_player_challenge_cards_stand_in_three_sides )
{
	using namespace SinglePlayerLayout;
	singlePlayerCheckRect( card( 0, 0 ), 66, 160, 250, 400 );
	// three sides of three, 10 apart inside a side and 36 between sides, all inside the content
	CHECK_EQ( card( 0, 1 ).lo.x - card( 0, 0 ).hi.x, CARD_GAP );
	CHECK_EQ( card( 1, 0 ).lo.x - card( 0, 2 ).hi.x, SIDE_GAP );
	CHECK( card( 2, 2 ).hi.x <= RIGHT );
	CHECK_EQ( sideHead( 1 ).lo.x, card( 1, 0 ).lo.x );
	CHECK_EQ( sideHead( 1 ).hi.x, card( 1, 2 ).hi.x );
	// every persona but Leang has a card, none twice
	Int seen = 0;
	for( Int side = 0; side < SIDES; side++ )
		for( Int slot = 0; slot < CARDS_PER_SIDE; slot++ )
			seen |= 1 << sidePersona( side, slot );
	CHECK_EQ( seen, 0x1ff );
}

TEST( single_player_places_map_is_the_designs_band )
{
	using namespace SinglePlayerLayout;
	singlePlayerCheckRect( PLACES, 644, 428, 1860, 768 );
	CHECK_EQ( placesHeightFor( PLACES.hi.x - PLACES.lo.x ), PLACES.hi.y - PLACES.lo.y );
	singlePlayerCheckRect( unknownBox( 1 ), 660, 686, 920, 752 );
	singlePlayerCheckRect( PLACES_LEGEND, 1460, 716, 1844, 752 );
	// every station the strings name falls inside the map; Thrax and Juhziz have none
	Real lon, lat;
	CHECK( !station( 1, lon, lat ) );
	CHECK( !station( 8, lon, lat ) );
	CHECK( !station( 10, lon, lat ) );
	for( Int persona = 0; persona < CHALLENGE_PERSONAS; persona++ )
	{
		if( !station( persona, lon, lat ) )
			continue;
		Real x, y;
		placesPoint( lon, lat, x, y );
		CHECK( x > PLACES.lo.x && x < PLACES.hi.x );
		CHECK( y > PLACES.lo.y && y < PLACES.hi.y );
	}
}

TEST( single_player_rings_spread_apart_inside_the_map )
{
	using namespace SinglePlayerLayout;
	// Fai (Beijing) and Kwai (Jinan) sit 18 px apart at this scale
	std::vector< Real > x, y;
	Real lon, lat, px, py;
	station( 7, lon, lat );
	placesPoint( lon, lat, px, py );
	x.push_back( px );
	y.push_back( py );
	station( 4, lon, lat );
	placesPoint( lon, lat, px, py );
	x.push_back( px );
	y.push_back( py );
	CHECK( sqrt( ( x[ 1 ] - x[ 0 ] ) * ( x[ 1 ] - x[ 0 ] ) + ( y[ 1 ] - y[ 0 ] ) * ( y[ 1 ] - y[ 0 ] ) ) < RING_GAP );
	spreadRings( x, y, PLACES );
	const Real apart = (Real)sqrt( ( x[ 1 ] - x[ 0 ] ) * ( x[ 1 ] - x[ 0 ] ) + ( y[ 1 ] - y[ 0 ] ) * ( y[ 1 ] - y[ 0 ] ) );
	CHECK( apart >= RING_GAP - 0.01f );
	// a ring pushed against the edge stays 16 in
	std::vector< Real > ex( 1, (Real)PLACES.lo.x ), ey( 1, (Real)PLACES.hi.y );
	spreadRings( ex, ey, PLACES );
	CHECK_NEAR( ex[ 0 ], (Real)( PLACES.lo.x + 16 ), 0.01f );
	CHECK_NEAR( ey[ 0 ], (Real)( PLACES.hi.y - 16 ), 0.01f );
}

TEST( single_player_ladder_tiles_share_the_maps_width )
{
	using namespace SinglePlayerLayout;
	singlePlayerCheckRect( ladderTile( 7, 0 ), 644, 784, 809, 948 );
	CHECK_EQ( ladderTile( 7, 6 ).hi.x, PLACES.hi.x );
	CHECK_EQ( ladderTile( 8, 7 ).hi.x, PLACES.hi.x );
	CHECK_EQ( ladderTile( 8, 0 ).hi.x - ladderTile( 8, 0 ).lo.x, 143 );
	// seven battles keep full names, eight go to surnames
	CHECK( ladderTileWidth( 7 ) >= LADDER_SURNAMES_UNDER );
	CHECK( ladderTileWidth( 8 ) < LADDER_SURNAMES_UNDER );
	singlePlayerCheckRect( DOSSIER_PORTRAIT, 60, 516, 348, 948 );
	CHECK( DOSSIER_PORTRAIT.hi.y < FOOT_RULE && LADDER_BOTTOM < FOOT_RULE );
}

TEST( single_player_lists_hold_whole_rows_and_their_columns )
{
	using namespace SinglePlayerLayout;
	singlePlayerCheckRect( REPLAY_HEADS, 60, 128, 1236, 162 );
	CHECK_EQ( REPLAY_LIST.hi.y - REPLAY_LIST.lo.y, REPLAY_ROWS * REPLAY_ROW_HEIGHT );
	CHECK( REPLAY_LIST.hi.y <= REPLAY_LEGEND.lo.y );
	CHECK_EQ( SAVE_LIST.hi.y - SAVE_LIST.lo.y, SAVE_ROWS * SAVE_ROW_HEIGHT );
	CHECK( SAVE_LIST.hi.y <= SAVE_LEGEND_TOP );
	// the columns stand in order, never over one another, inside the rows
	for( Int c = 1; c < REPLAY_COLUMNS; c++ )
		CHECK( replayColumn( c ).left > replayColumn( c - 1 ).right );
	CHECK_EQ( replayColumn( REPLAY_NAME ).left, 76 );
	CHECK( replayColumn( REPLAY_VERSION ).right <= REPLAY_ROW_RIGHT );
	for( Int c = 1; c < SAVE_COLUMNS; c++ )
	{
		CHECK( saveColumn( c ).left > saveColumn( c - 1 ).right );
		CHECK( saveColumn( c, SAVE_BOX_LIST.lo.x, SAVE_BOX_LIST.hi.x ).left > saveColumn( c - 1, SAVE_BOX_LIST.lo.x, SAVE_BOX_LIST.hi.x ).right );
	}
	CHECK( saveColumn( SAVE_SAVED ).right <= SAVE_ROW_RIGHT );
	CHECK( saveColumn( SAVE_NAME, SAVE_BOX_LIST.lo.x, SAVE_BOX_LIST.hi.x ).left > SAVE_BOX_LIST.lo.x );
	CHECK( saveColumn( SAVE_SAVED, SAVE_BOX_LIST.lo.x, SAVE_BOX_LIST.hi.x ).right < SAVE_BOX_LIST.hi.x );
	CHECK( replayBoxColumn( RBOX_LENGTH ).right < REPLAY_BOX_LIST.hi.x );
	// the details: preview under the name, facts under the preview, players under the facts
	CHECK( REPLAY_DETAILS_NAME.hi.y < REPLAY_PREVIEW.lo.y );
	CHECK( REPLAY_PREVIEW.hi.y < REPLAY_FACTS_TOP );
	CHECK( replayFact( 5 ).hi.y < REPLAY_PLAYERS_TOP );
	CHECK( replayFact( 2 ).hi.x <= RIGHT );
	// 32 seats in two columns still end over the foot rule, rows never under the body's 18
	CHECK_EQ( replayPlayerColumns( 32 ), 2 );
	CHECK( REPLAY_PLAYERS_TOP + 34 + 16 * replayPlayerRow( 32 ) <= FOOT_RULE - 10 );
	CHECK( replayPlayerRow( 32 ) >= 18 );
	CHECK_EQ( replayPlayerRow( 8 ), REPLAY_PLAYER_ROW );
	CHECK_EQ( replayPlayerColumns( 8 ), 1 );
	CHECK( missionTile( 7, 6 ).hi.x <= RIGHT );
	CHECK( MISSION_TILES_TOP + MISSION_TILE_HEIGHT < SAVE_PREVIEW.lo.y );
}

TEST( single_player_boxes_stand_where_the_design_draws_them )
{
	using namespace SinglePlayerLayout;
	singlePlayerCheckRect( REPLAY_BOX, 440, 252, 1480, 828 );
	singlePlayerCheckRect( REPLAY_BOX_ENTRY, 572, 358, 1448, 406 );
	CHECK( REPLAY_BOX_LIST.hi.y < REPLAY_BOX.hi.y - 84 );
	// the text box and the list on the 8 px grid, the box keys 16 apart
	CHECK_EQ( ( REPLAY_BOX_LIST.lo.y - REPLAY_BOX_ENTRY.hi.y ) % 8, 0 );
	CHECK_EQ( boxKey( REPLAY_BOX, 0, 170 ).lo.x - boxKey( REPLAY_BOX, 1, 170 ).hi.x, 16 );
	singlePlayerCheckRect( SAVE_BOX, 440, 124, 1480, 680 );
	// over a match the box ends 44 over the command bar's highest plate
	CHECK( SAVE_BOX.hi.y <= 832 - 44 );
	CHECK( SAVE_BOX_LIST.hi.y < SAVE_BOX.hi.y - 84 );
	singlePlayerCheckRect( boxKey( SAVE_BOX, 0, SAVE_BOX_KEY_WIDTH ), 1258, 612, 1448, 660 );
	singlePlayerCheckRect( boxKey( SAVE_BOX, 2, SAVE_BOX_KEY_WIDTH ), 846, 612, 1036, 660 );
	// the screens' keys on the foot row: BACK, then 12 apart, the primary at the right edge
	CHECK_EQ( REPLAY_COPY.lo.x - REPLAY_BACK.hi.x, KEY_GAP );
	CHECK_EQ( REPLAY_DELETE.lo.x - REPLAY_COPY.hi.x, KEY_GAP );
	CHECK_EQ( SAVE_DELETE.lo.x - SAVE_BACK.hi.x, KEY_GAP );
	CHECK_EQ( REPLAY_WATCH.hi.x, RIGHT );
	CHECK_EQ( SAVE_LOAD.hi.x, RIGHT );
	CHECK_EQ( CHALLENGE_PLAY.hi.x, RIGHT );
}

TEST( single_player_readouts_write_lengths_and_teams )
{
	using namespace SinglePlayerLayout;
	char text[ 32 ];
	clockText( 845, text, sizeof( text ) );
	CHECK_STR( text, "14:05" );
	clockText( 4360, text, sizeof( text ) );
	CHECK_STR( text, "1:12:40" );
	clockText( -5, text, sizeof( text ) );
	CHECK_STR( text, "00:00" );
	std::vector< Int > teams;
	for( Int each = 0; each < 8; each++ )
		teams.push_back( each < 4 ? 0 : 1 );
	teamsText( teams, text, sizeof( text ) );
	CHECK_STR( text, "4 v 4" );
	teams.assign( 3, -1 );
	teamsText( teams, text, sizeof( text ) );
	CHECK_STR( text, "1 v 1 v 1" );
	CHECK_EQ( saveExtrasBudgetMs( 10 ), 200 );
	CHECK_EQ( saveExtrasBudgetMs( 100 ), 400 );
	// a square map in the wide preview: as tall as the box, centred
	singlePlayerCheckRect( fitShape( REPLAY_PREVIEW, 1.0f, 1.0f ), 1412, 192, 1716, 496 );
	singlePlayerCheckRect( fitShape( REPLAY_PREVIEW, 0.0f, 0.0f ), 1268, 192, 1860, 496 );
}
