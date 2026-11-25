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
	CHECK_EQ( railHeight( FALSE, 5 ), 626 );
	CHECK_EQ( railHeight( TRUE, 6 ), 732 );
	CHECK_EQ( railHeight( TRUE, 2 ), 444 );
	CHECK_EQ( railHeight( FALSE, 6 ), 698 );
	mainMenuCheckRect( railRect( FALSE, 5 ), 1336, 28, 1888, 654 );
	mainMenuCheckRect( railRect( TRUE, 2 ), 1336, 28, 1888, 472 );
	mainMenuCheckRect( railRect( TRUE, 6 ), 1336, 28, 1888, 760 );
}

TEST( main_menu_rail_buttons_step_72_with_the_leaving_one_apart )
{
	using namespace MainMenuLayout;
	mainMenuCheckRect( railButton( FALSE, 0, 5 ), 1364, 180, 1860, 242 );
	mainMenuCheckRect( railButton( FALSE, 1, 5 ), 1364, 252, 1860, 314 );
	mainMenuCheckRect( railButton( FALSE, 4, 5 ), 1364, 468, 1860, 530 );
	mainMenuCheckRect( railButton( FALSE, 5, 5 ), 1364, 554, 1860, 616 );
	// a sub-list starts under its caption
	mainMenuCheckRect( railButton( TRUE, 0, 6 ), 1364, 214, 1860, 276 );
	// every leaving button sits inside its rail with air under it
	CHECK( railButton( TRUE, 6, 6 ).bottom < railRect( TRUE, 6 ).bottom );
	CHECK( railButton( TRUE, 2, 2 ).bottom < railRect( TRUE, 2 ).bottom );
}

TEST( main_menu_campaign_panel_beside_the_rail )
{
	using namespace MainMenuLayout;
	const CampaignPanel full = campaignPanel( TRUE, TRUE );
	mainMenuCheckRect( full.panel, 816, 28, 1288, 464 );
	CHECK_EQ( full.panel.right + PANEL_GAP, RAIL_LEFT );
	mainMenuCheckRect( campaignTab( 0, full.tabsTop ), 844, 150, 1047, 194 );
	mainMenuCheckRect( campaignTab( 1, full.tabsTop ), 1057, 150, 1260, 194 );
	// five tiles 12 apart, seven 8 apart, the last ending on the content's right
	mainMenuCheckRect( missionTile( 0, 5, full.tilesTop ), 844, 248, 918, 320 );
	CHECK_EQ( missionTile( 4, 5, full.tilesTop ).left, 1186 );
	CHECK_EQ( missionTile( 4, 5, full.tilesTop ).right, 1260 );
	CHECK_EQ( missionTile( 6, 7, full.tilesTop ).left, 1207 );
	CHECK_EQ( missionTile( 6, 7, full.tilesTop ).right, 1260 );
	mainMenuCheckRect( difficultyKey( 0, full.keysTop ), 844, 374, 976, 436 );
	mainMenuCheckRect( difficultyKey( 2, full.keysTop ), 1128, 374, 1260, 436 );
	// without the original campaign the tabs go and the panel closes up; Generals' Challenge keeps
	// the difficulty keys alone
	CHECK_EQ( campaignPanel( FALSE, TRUE ).tabsTop, -1 );
	CHECK_EQ( campaignPanel( FALSE, TRUE ).panel.bottom, 464 - TAB_HEIGHT - SECTION_GAP );
	CHECK_EQ( campaignPanel( FALSE, FALSE ).tilesTop, -1 );
	CHECK_EQ( campaignPanel( FALSE, FALSE ).keysTop, 178 );
}

TEST( main_menu_mods_panel_stands_on_the_rail_foot )
{
	using namespace MainMenuLayout;
	const Int foot = railRect( TRUE, 6 ).bottom;
	mainMenuCheckRect( modsPanel( foot ), 528, 306, 1288, 760 );
	mainMenuCheckRect( modeCard( 0, foot ), 556, 428, 896, 732 );
	mainMenuCheckRect( modeCard( 1, foot ), 920, 428, 1260, 732 );
}

TEST( main_menu_rail_keeps_its_size_off_the_right_edge )
{
	using namespace MainMenuLayout;
	const Rect rail = railRect( FALSE, 5 );
	mainMenuCheckRect( onScreen( rail, 1920, 1080 ), 1336, 28, 1888, 654 );
	// 21:9 and 4:3 keep the rail 32 off the right edge at 1080's size; 720 is two thirds of 1080
	mainMenuCheckRect( onScreen( rail, 2560, 1080 ), 1976, 28, 2528, 654 );
	mainMenuCheckRect( onScreen( rail, 1440, 1080 ), 856, 28, 1408, 654 );
	mainMenuCheckRect( onScreen( rail, 1280, 720 ), 891, 19, 1259, 436 );
	// the widest thing the rail opens, the Mods panel, still fits a 4:3 screen
	CHECK( onScreen( modsPanel( railRect( TRUE, 6 ).bottom ), 1024, 768 ).left >= 0 );
	CHECK_EQ( length( 14.0f, 720 ), 9 );
	CHECK_EQ( length( 0.4f, 720 ), 1 );
}
