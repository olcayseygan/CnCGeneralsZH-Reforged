// The main menu's geometry (docs/plans/MENU-REDESIGN.md, package 4): the rail and the panels it
// opens, against the rects of experiments/proofs/2026-11-24/screens/main-menu/README-main-menu.md in
// the wrapper, at 1920x1080, 2560x1080, 1440x1080 and 1280x720.  Included into test_gameengine.cpp.

#include "GameClient/MainMenuLayout.h"

static void mainMenuCheckRect( const MainMenuLayout::Rect &r, Int left, Int top, Int right, Int bottom )
{
	CHECK_EQ( r.left, left );
	CHECK_EQ( r.top, top );
	CHECK_EQ( r.right, right );
	CHECK_EQ( r.bottom, bottom );
}

TEST( main_menu_rails_hold_what_they_list )
{
	using namespace MainMenuLayout;
	// main: Solo Play, Multiplayer, Load, Options, Credits and Exit Game apart; Solo Play: six and
	// Back; Load: two and Back; a game started with -online has one key more on the main rail
	CHECK_EQ( railHeight( FALSE, 5 ), 628 );
	CHECK_EQ( railHeight( TRUE, 6 ), 734 );
	CHECK_EQ( railHeight( TRUE, 2 ), 446 );
	CHECK_EQ( railHeight( FALSE, 6 ), 700 );
	mainMenuCheckRect( railRect( FALSE, 5 ), 1336, 28, 1888, 656 );
	mainMenuCheckRect( railRect( TRUE, 2 ), 1336, 28, 1888, 474 );
	mainMenuCheckRect( railRect( TRUE, 6 ), 1336, 28, 1888, 762 );
}

TEST( main_menu_rail_buttons_step_72_with_the_leaving_one_apart )
{
	using namespace MainMenuLayout;
	// 64 tall, 8 apart, the leaving one 24 under the list: every gap on the 8 px grid
	mainMenuCheckRect( railButton( FALSE, 0, 5 ), 1364, 180, 1860, 244 );
	mainMenuCheckRect( railButton( FALSE, 1, 5 ), 1364, 252, 1860, 316 );
	mainMenuCheckRect( railButton( FALSE, 4, 5 ), 1364, 468, 1860, 532 );
	mainMenuCheckRect( railButton( FALSE, 5, 5 ), 1364, 556, 1860, 620 );
	CHECK_EQ( railButton( FALSE, 1, 5 ).top - railButton( FALSE, 0, 5 ).bottom, 8 );
	CHECK_EQ( railButton( FALSE, 5, 5 ).top - railButton( FALSE, 4, 5 ).bottom, 24 );
	// a sub-list starts under its caption
	mainMenuCheckRect( railButton( TRUE, 0, 6 ), 1364, 214, 1860, 278 );
	// every leaving button sits inside its rail with air under it
	CHECK( railButton( TRUE, 6, 6 ).bottom < railRect( TRUE, 6 ).bottom );
	CHECK( railButton( TRUE, 2, 2 ).bottom < railRect( TRUE, 2 ).bottom );
}

TEST( main_menu_campaign_panel_beside_the_rail )
{
	using namespace MainMenuLayout;
	const CampaignPanel full = campaignPanel( TRUE, TRUE );
	mainMenuCheckRect( full.panel, 816, 28, 1288, 470 );
	CHECK_EQ( full.panel.right + PANEL_GAP, RAIL_LEFT );
	mainMenuCheckRect( campaignTab( 0, full.tabsTop ), 844, 150, 1048, 194 );
	mainMenuCheckRect( campaignTab( 1, full.tabsTop ), 1056, 150, 1260, 194 );
	// tiles 8 apart at any count, the last ending on the content's right
	mainMenuCheckRect( missionTile( 0, 5, full.tilesTop ), 844, 250, 921, 322 );
	for( Int m = 1; m < 5; ++m )
		CHECK_EQ( missionTile( m, 5, full.tilesTop ).left - missionTile( m - 1, 5, full.tilesTop ).right, 8 );
	CHECK_EQ( missionTile( 4, 5, full.tilesTop ).left, 1183 );
	CHECK_EQ( missionTile( 4, 5, full.tilesTop ).right, 1260 );
	CHECK_EQ( missionTile( 6, 7, full.tilesTop ).left, 1207 );
	CHECK_EQ( missionTile( 6, 7, full.tilesTop ).right, 1260 );
	mainMenuCheckRect( difficultyKey( 0, full.keysTop ), 844, 378, 977, 442 );
	mainMenuCheckRect( difficultyKey( 2, full.keysTop ), 1127, 378, 1260, 442 );
	CHECK_EQ( difficultyKey( 1, full.keysTop ).left - difficultyKey( 0, full.keysTop ).right, 8 );
	CHECK_EQ( difficultyKey( 2, full.keysTop ).left - difficultyKey( 1, full.keysTop ).right, 8 );
	// without the original campaign the tabs go and the panel closes up; Generals' Challenge keeps
	// the difficulty keys alone
	CHECK_EQ( campaignPanel( FALSE, TRUE ).tabsTop, -1 );
	CHECK_EQ( campaignPanel( FALSE, TRUE ).panel.bottom, 470 - TAB_HEIGHT - SECTION_GAP );
	CHECK_EQ( campaignPanel( FALSE, FALSE ).tilesTop, -1 );
	CHECK_EQ( campaignPanel( FALSE, FALSE ).keysTop, 182 );
}

TEST( main_menu_mods_panel_stands_on_the_rail_foot )
{
	using namespace MainMenuLayout;
	const Int foot = railRect( TRUE, 6 ).bottom;
	mainMenuCheckRect( modsPanel( foot ), 528, 308, 1288, 762 );
	mainMenuCheckRect( modeCard( 0, foot ), 556, 430, 896, 734 );
	mainMenuCheckRect( modeCard( 1, foot ), 920, 430, 1260, 734 );
}

TEST( main_menu_rail_keeps_its_size_off_the_right_edge )
{
	using namespace MainMenuLayout;
	const Rect rail = railRect( FALSE, 5 );
	mainMenuCheckRect( onScreen( rail, 1920, 1080 ), 1336, 28, 1888, 656 );
	// 21:9 and 4:3 keep the rail 32 off the right edge at 1080's size; 720 is two thirds of 1080
	mainMenuCheckRect( onScreen( rail, 2560, 1080 ), 1976, 28, 2528, 656 );
	mainMenuCheckRect( onScreen( rail, 1440, 1080 ), 856, 28, 1408, 656 );
	mainMenuCheckRect( onScreen( rail, 1280, 720 ), 891, 19, 1259, 437 );
	// the widest thing the rail opens, the Mods panel, still fits a 4:3 screen
	CHECK( onScreen( modsPanel( railRect( TRUE, 6 ).bottom ), 1024, 768 ).left >= 0 );
	CHECK_EQ( length( 14.0f, 720 ), 9 );
	CHECK_EQ( length( 0.4f, 720 ), 1 );
}
