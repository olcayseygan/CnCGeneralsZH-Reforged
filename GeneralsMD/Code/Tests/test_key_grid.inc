// The Key Grid layout's pure parts (GameClient/ControlBarKeyGrid.h): the radar glass in the map's shape,
// the console's rectangles, which place each button takes, the riders' places, the squad tray's cells,
// and the keyboard scheme and Grid Keys option that come with it.

#include "GameClient/ControlBarKeyGrid.h"
#include "GameClient/HotkeyScheme.h"

/* The glass keeps today's area, 322x232 at 1080, in the map's own shape: today's aspect gives today's
   glass, Tournament Desert's tall 176x230 a tall one, and a map past 2:1 stops at 2:1 */
TEST(key_grid_glass_takes_the_maps_shape_at_todays_area)
{
	Int w = 0, h = 0;
	KeyGrid_glassForAspect( 322.0f / 232.0f, &w, &h );
	CHECK_EQ( w, 322 );
	CHECK_EQ( h, 232 );
	KeyGrid_glassForAspect( 0.0f, &w, &h );
	CHECK_EQ( w, 322 );
	CHECK_EQ( h, 232 );

	KeyGrid_glassForAspect( 176.0f / 230.0f, &w, &h );
	CHECK_EQ( w, 239 );
	CHECK( h >= 312 && h <= 313 );
	CHECK( w < h );
	KeyGrid_glassForAspect( 230.0f / 176.0f, &w, &h );
	CHECK( w >= 312 && w <= 313 );
	CHECK( w > h );
	for( Real aspect = 0.25f; aspect <= 4.0f; aspect *= 1.25f )
	{
		KeyGrid_glassForAspect( aspect, &w, &h );
		CHECK( w * h > 322 * 232 * 98 / 100 && w * h < 322 * 232 * 102 / 100 );
	}
	Int w2 = 0, h2 = 0;
	KeyGrid_glassForAspect( 4.0f, &w, &h );
	KeyGrid_glassForAspect( 2.0f, &w2, &h2 );
	CHECK_EQ( w, w2 );
	CHECK_EQ( h, h2 );
}

/* mock_v21.py's v21() at 1920x1080: the glass on the box's foot, the shelf on the box, the spine out of
   the box's right side, the rows hanging off it, keyboard-staggered; a wider map moves the keys right */
TEST(key_grid_console_stands_where_the_mockup_has_it)
{
	const IRegion2D area = { { 0, 0 }, { 1920, 1080 } };
	KeyGridRects kg;
	KeyGridLayoutForScreen( 1920, 1080, area, 1.0f, 322.0f / 232.0f, &kg );
	CHECK_EQ( kg.glass.lo.x, 5 );
	CHECK_EQ( kg.glass.hi.x, 327 );
	CHECK_EQ( kg.glass.lo.y, 832 );
	CHECK_EQ( kg.glass.hi.y, 1064 );
	CHECK_EQ( kg.box.hi.x, 345 );
	CHECK_EQ( kg.box.lo.y, 812 );
	CHECK_EQ( kg.shelf.lo.y, 764 );
	CHECK_EQ( kg.shelf.hi.x, 345 );
	CHECK_EQ( kg.spine.lo.x, 339 );
	CHECK_EQ( kg.spine.hi.x, 379 );
	CHECK_EQ( kg.head.lo.y, 780 );
	CHECK_EQ( kg.money.lo.x, 397 );
	CHECK_EQ( kg.meter.lo.x, 354 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_Q ].lo.x, 387 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_Q ].lo.y, 830 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_Q ].hi.x - kg.cells[ COMMAND_PLACE_Q ].lo.x, 90 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_Q ].hi.y - kg.cells[ COMMAND_PLACE_Q ].lo.y, 72 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_W ].lo.x - kg.cells[ COMMAND_PLACE_Q ].lo.x, 100 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_A ].lo.x - kg.cells[ COMMAND_PLACE_Q ].lo.x, 25 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_Z ].lo.x - kg.cells[ COMMAND_PLACE_Q ].lo.x, 75 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_A ].lo.y, 918 );
	CHECK_EQ( kg.cells[ COMMAND_PLACE_N ].hi.y, 1078 );
	CHECK( kg.cells[ COMMAND_PLACE_N ].hi.x <= kg.deck.hi.x );
	CHECK_EQ( kg.shelfKeys[ 0 ].lo.x, 156 );
	CHECK_EQ( kg.signals.lo.x, 11 );
	CHECK_EQ( kg.signalKeys[ 2 ].lo.x, 99 );
	CHECK_EQ( kg.signalKeys[ 0 ].lo.y, kg.shelf.lo.y + 10 );
	// no room on the spine under today's shelf: the squad switch stands on the head, left of the money
	CHECK( kg.deckSwitch.lo.y >= kg.head.lo.y && kg.deckSwitch.hi.y <= kg.head.hi.y );
	CHECK( kg.deckSwitch.hi.x <= kg.money.lo.x );
	// no two cells meet
	for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
		for( Int other = 0; other < place; other++ )
		{
			const IRegion2D &c = kg.cells[ place ], &o = kg.cells[ other ];
			CHECK( c.hi.x <= o.lo.x || o.hi.x <= c.lo.x || c.hi.y <= o.lo.y || o.hi.y <= c.lo.y );
		}

	// a tall map: a narrower, taller box, the keys left with it, the rows where they were in height
	KeyGridRects tall;
	KeyGridLayoutForScreen( 1920, 1080, area, 1.0f, 176.0f / 230.0f, &tall );
	CHECK_EQ( tall.glass.hi.x - tall.glass.lo.x, 239 );
	CHECK( tall.box.lo.y < kg.box.lo.y );
	CHECK_EQ( tall.cells[ COMMAND_PLACE_Q ].lo.x, kg.cells[ COMMAND_PLACE_Q ].lo.x - 83 );
	CHECK_EQ( tall.cells[ COMMAND_PLACE_Q ].lo.y, 830 );
	// the mockup's switch, on the spine between the shelf and the head
	CHECK_EQ( tall.deckSwitch.lo.x, 258 );
	CHECK_EQ( tall.deckSwitch.lo.y, 742 );
	CHECK_EQ( tall.deckSwitch.hi.y, 776 );

	// 1280x720 on a 21:9 area: two thirds of everything, from the area's left edge
	const IRegion2D narrow = { { 100, 0 }, { 1180, 720 } };
	// ("small" is a Windows macro for char, rpcndr.h)
	KeyGridRects shrunk;
	KeyGridLayoutForScreen( 1280, 720, narrow, 720.0f / 1080.0f, 322.0f / 232.0f, &shrunk );
	CHECK_EQ( shrunk.glass.lo.x, 100 + 3 );
	CHECK_EQ( shrunk.glass.hi.y, 720 - 11 );
	CHECK_EQ( shrunk.cells[ COMMAND_PLACE_Q ].lo.x, 100 + 258 );
}

/* order.py's rule on the American Dozer: Power Q, Supply W, Barracks E, War Factory R, Airfield T;
   Patriot A, Fire Base S; Strategy Center Z (the tech building), Drop Zone X, Particle Cannon C;
   Command Center B, the last free key of the row the line ended in; Disarm Mines N */
TEST(key_grid_places_a_builders_set_by_unlock_order)
{
	enum { SLOTS = MAX_COMMANDS_PER_SET };
	KeyGridSlot dozer[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
	{
		const KeyGridSlot empty = { GUI_COMMAND_NONE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
		dozer[ i ] = empty;
	}
	// EA's slots: command center 1, power 2, supply 3, barracks 4, patriot 5, fire base 6, strategy
	// center 7, war factory 8, drop zone 9, airfield 10, particle cannon 11, disarm mines 14
	const KeyGridSlot cc = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_COMMAND_CENTER, 0, 30, -1 };
	const KeyGridSlot power = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_UTILITY, -1, KEY_GRID_ROLE_ECONOMY, 1, 12, -1 };
	const KeyGridSlot supply = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_UTILITY, -1, KEY_GRID_ROLE_ECONOMY, 1, 8, -1 };
	const KeyGridSlot barracks = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_BUILD, 2, 9, -1 };
	const KeyGridSlot patriot = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_DEFENSE, -1, KEY_GRID_ROLE_DEFENSE, 2, 0, -1 };
	const KeyGridSlot firebase = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_DEFENSE, -1, KEY_GRID_ROLE_DEFENSE, 3, 0, -1 };
	const KeyGridSlot strategy = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_UTILITY, -1, KEY_GRID_ROLE_EXTRA, 3, 25, -1 };
	const KeyGridSlot factory = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_BUILD, 2, 14, -1 };
	const KeyGridSlot dropzone = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_UTILITY, -1, KEY_GRID_ROLE_EXTRA, 2, 0, -1 };
	const KeyGridSlot airfield = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_BUILD, 3, 4, -1 };
	const KeyGridSlot cannon = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_UTILITY, -1, KEY_GRID_ROLE_EXTRA, 4, 0, -1 };
	const KeyGridSlot mines = { GUI_COMMAND_SPECIAL_POWER, COMMAND_GROUP_ABILITY, COMMAND_PLACE_N, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	dozer[ 0 ] = cc;
	dozer[ 1 ] = power;
	dozer[ 2 ] = supply;
	dozer[ 3 ] = barracks;
	dozer[ 4 ] = patriot;
	dozer[ 5 ] = firebase;
	dozer[ 6 ] = strategy;
	dozer[ 7 ] = factory;
	dozer[ 8 ] = dropzone;
	dozer[ 9 ] = airfield;
	dozer[ 10 ] = cannon;
	dozer[ 13 ] = mines;
	Int places[ SLOTS ];
	CHECK( !ControlBar_keyGridPlaces( dozer, SLOTS, places ) );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 7 ], (Int)COMMAND_PLACE_R );
	CHECK_EQ( places[ 9 ], (Int)COMMAND_PLACE_T );
	CHECK_EQ( places[ 4 ], (Int)COMMAND_PLACE_A );
	CHECK_EQ( places[ 5 ], (Int)COMMAND_PLACE_S );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_Z );
	CHECK_EQ( places[ 8 ], (Int)COMMAND_PLACE_X );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_C );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_B );
	CHECK_EQ( places[ 13 ], (Int)COMMAND_PLACE_N );
	CHECK_EQ( places[ 11 ], -1 );

	// the GLA worker's fake set: each fake on its real building's place, whatever its own order says
	KeyGridSlot fakes[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		fakes[ i ] = dozer[ 11 ];
	const KeyGridSlot fakeFactory = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_BUILD, 1, 0, COMMAND_PLACE_R };
	const KeyGridSlot fakeBarracks = { GUI_COMMAND_DOZER_CONSTRUCT, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_BUILD, 1, 0, COMMAND_PLACE_E };
	const KeyGridSlot back = { GUI_COMMAND_OBJECT_UPGRADE, COMMAND_GROUP_ABILITY, COMMAND_PLACE_H, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	fakes[ 0 ] = fakeFactory;
	fakes[ 1 ] = fakeBarracks;
	fakes[ 12 ] = back;
	ControlBar_keyGridPlaces( fakes, SLOTS, places );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_R );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_H );

	// no tech building (no extra anyone names): the extras still open the Z row, a superweapon among them
	dozer[ 6 ].refs = 0;
	ControlBar_keyGridPlaces( dozer, SLOTS, places );
	CHECK_EQ( places[ 8 ], (Int)COMMAND_PLACE_Z );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_X );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_C );
}

/* A transport (the Humvee): its upgrades along Q as today, the orders on their places, A C V kept for
   force attack, hold position and move, and the passengers along the Z row from N leftward, then the A
   row from H leftward, round what holds a key there */
TEST(key_grid_places_riders_from_the_corner)
{
	enum { SLOTS = MAX_COMMANDS_PER_SET };
	KeyGridSlot humvee[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
	{
		const KeyGridSlot empty = { GUI_COMMAND_NONE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
		humvee[ i ] = empty;
	}
	const KeyGridSlot upgrade = { GUI_COMMAND_OBJECT_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot rider = { GUI_COMMAND_EXIT_CONTAINER, COMMAND_GROUP_PASSENGER, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot evacuate = { GUI_COMMAND_EVACUATE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot attackMove = { GUI_COMMAND_ATTACK_MOVE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot guard = { GUI_COMMAND_GUARD, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot stop = { GUI_COMMAND_STOP, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	humvee[ 0 ] = upgrade;
	humvee[ 1 ] = upgrade;
	humvee[ 2 ] = upgrade;
	humvee[ 3 ] = rider;
	humvee[ 4 ] = rider;
	humvee[ 5 ] = rider;
	humvee[ 6 ] = evacuate;
	humvee[ 10 ] = attackMove;
	humvee[ 11 ] = guard;
	humvee[ 12 ] = stop;
	Int places[ SLOTS ];
	CHECK( ControlBar_keyGridPlaces( humvee, SLOTS, places ) );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_Z );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_D );
	CHECK_EQ( places[ 11 ], (Int)COMMAND_PLACE_X );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_S );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_N );
	CHECK_EQ( places[ 4 ], (Int)COMMAND_PLACE_B );
	CHECK_EQ( places[ 5 ], (Int)COMMAND_PLACE_H );

	// a garrison of ten: N B V C X, then H G F D S A, everything that is free; no rider on a held key
	KeyGridSlot bunker[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		bunker[ i ] = i < 10 ? rider : humvee[ 7 ];
	bunker[ 10 ] = evacuate;
	CHECK( !ControlBar_keyGridPlaces( bunker, SLOTS, places ) );
	static const Int expected[ 10 ] = { COMMAND_PLACE_N, COMMAND_PLACE_B, COMMAND_PLACE_V, COMMAND_PLACE_C, COMMAND_PLACE_X,
		COMMAND_PLACE_H, COMMAND_PLACE_G, COMMAND_PLACE_F, COMMAND_PLACE_D, COMMAND_PLACE_S };
	for( Int i = 0; i < 10; i++ )
		CHECK_EQ( places[ i ], expected[ i ] );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_Z );
}

/* A factory (the owner's of 2026-11-28): its upgrades on a row of their own along Z, up a row when it
   fills; the units a column at a time from the left over the rows with no upgrade, a block under the
   left hand rather than one row; rally point B and sell N where they always are; no key twice */
TEST(key_grid_packs_a_factorys_units_left_and_its_upgrades_on_a_row)
{
	enum { SLOTS = MAX_COMMANDS_PER_SET };
	const KeyGridSlot empty = { GUI_COMMAND_NONE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot unit = { GUI_COMMAND_UNIT_BUILD, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot upgrade = { GUI_COMMAND_PLAYER_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot own = { GUI_COMMAND_OBJECT_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot power = { GUI_COMMAND_SPECIAL_POWER, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot rally = { GUI_COMMAND_SET_RALLY_POINT, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot sell = { GUI_COMMAND_SELL, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };

	// nine units and three upgrades, interleaved in EA's slots, rally point and sell last
	KeyGridSlot factory[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		factory[ i ] = empty;
	static const Int UNITS[ 9 ] = { 0, 1, 2, 4, 5, 7, 8, 10, 11 };
	for( Int i = 0; i < 9; i++ )
		factory[ UNITS[ i ] ] = unit;
	factory[ 3 ] = upgrade;
	factory[ 6 ] = own;
	factory[ 9 ] = upgrade;
	factory[ 12 ] = rally;
	factory[ 13 ] = sell;
	Int places[ SLOTS ];
	CHECK( !ControlBar_keyGridPlaces( factory, SLOTS, places ) );
	static const Int COLUMNS[ 9 ] = { COMMAND_PLACE_Q, COMMAND_PLACE_A, COMMAND_PLACE_W, COMMAND_PLACE_S, COMMAND_PLACE_E,
		COMMAND_PLACE_D, COMMAND_PLACE_R, COMMAND_PLACE_F, COMMAND_PLACE_T };
	for( Int i = 0; i < 9; i++ )
		CHECK_EQ( places[ UNITS[ i ] ], COLUMNS[ i ] );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_Z );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_X );
	CHECK_EQ( places[ 9 ], (Int)COMMAND_PLACE_C );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_RALLY );
	CHECK_EQ( places[ 13 ], (Int)COMMAND_PLACE_SELL );
	CHECK_EQ( places[ 14 ], -1 );

	// a tech building: six upgrades fill Z X C V B and climb to A; its three powers keep off both rows
	KeyGridSlot tech[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		tech[ i ] = empty;
	for( Int i = 0; i < 6; i++ )
		tech[ i ] = upgrade;
	tech[ 6 ] = power;
	tech[ 7 ] = power;
	tech[ 8 ] = power;
	tech[ 9 ] = sell;
	ControlBar_keyGridPlaces( tech, SLOTS, places );
	static const Int CLIMB[ 6 ] = { COMMAND_PLACE_Z, COMMAND_PLACE_X, COMMAND_PLACE_C, COMMAND_PLACE_V, COMMAND_PLACE_B, COMMAND_PLACE_A };
	for( Int i = 0; i < 6; i++ )
		CHECK_EQ( places[ i ], CLIMB[ i ] );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 7 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 8 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 9 ], (Int)COMMAND_PLACE_SELL );

	// a full set of seventeen: every one on a place, no place twice
	KeyGridSlot full[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		full[ i ] = i < 8 ? upgrade : unit;
	full[ 17 ] = sell;
	ControlBar_keyGridPlaces( full, SLOTS, places );
	Bool seen[ COMMAND_PLACE_COUNT ] = { FALSE };
	for( Int i = 0; i < SLOTS; i++ )
	{
		CHECK( places[ i ] >= 0 && places[ i ] < COMMAND_PLACE_COUNT );
		if( places[ i ] >= 0 && places[ i ] < COMMAND_PLACE_COUNT )
		{
			CHECK( !seen[ places[ i ] ] );
			seen[ places[ i ] ] = TRUE;
		}
	}
}

/* Skills (the owner's of 2026-11-28): special powers, weapons to fire and the like on a row of their own,
   Q from the left and down a row when it fills; upgrades keep Z climbing; the units pack left in the rows
   neither takes.  A fighting unit's abilities go the same way round its fixed keys and A, C, V */
TEST(key_grid_puts_skills_on_a_row_from_the_top)
{
	enum { SLOTS = MAX_COMMANDS_PER_SET };
	const KeyGridSlot empty = { GUI_COMMAND_NONE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot unit = { GUI_COMMAND_UNIT_BUILD, COMMAND_GROUP_PRODUCTION, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot upgrade = { GUI_COMMAND_PLAYER_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot power = { GUI_COMMAND_SPECIAL_POWER, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot fire = { GUI_COMMAND_FIRE_WEAPON, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot rally = { GUI_COMMAND_SET_RALLY_POINT, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot sell = { GUI_COMMAND_SELL, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot attackMove = { GUI_COMMAND_ATTACK_MOVE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot guard = { GUI_COMMAND_GUARD, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot stop = { GUI_COMMAND_STOP, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	Int places[ SLOTS ];

	// a factory with powers: powers Q W, upgrades Z X, its four units along the free A row
	KeyGridSlot factory[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		factory[ i ] = empty;
	factory[ 0 ] = unit;
	factory[ 1 ] = power;
	factory[ 2 ] = unit;
	factory[ 3 ] = upgrade;
	factory[ 4 ] = unit;
	factory[ 5 ] = power;
	factory[ 6 ] = unit;
	factory[ 7 ] = upgrade;
	factory[ 12 ] = rally;
	factory[ 13 ] = sell;
	CHECK( !ControlBar_keyGridPlaces( factory, SLOTS, places ) );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 5 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_Z );
	CHECK_EQ( places[ 7 ], (Int)COMMAND_PLACE_X );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_A );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_S );
	CHECK_EQ( places[ 4 ], (Int)COMMAND_PLACE_D );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_F );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_RALLY );
	CHECK_EQ( places[ 13 ], (Int)COMMAND_PLACE_SELL );

	// a hero with four abilities: Q W E R, his orders on their keys, A C V left for the held orders
	KeyGridSlot hero[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		hero[ i ] = empty;
	hero[ 0 ] = fire;
	hero[ 1 ] = fire;
	hero[ 2 ] = power;
	hero[ 3 ] = fire;
	hero[ 10 ] = attackMove;
	hero[ 11 ] = guard;
	hero[ 12 ] = stop;
	CHECK( ControlBar_keyGridPlaces( hero, SLOTS, places ) );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_R );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_ATTACK_MOVE );
	CHECK_EQ( places[ 11 ], (Int)COMMAND_PLACE_GUARD );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_STOP );

	// eight abilities: the Q row full, then down round A (held), S (stop) and D (attack move) to F G
	for( Int i = 4; i < 8; i++ )
		hero[ i ] = power;
	CHECK( ControlBar_keyGridPlaces( hero, SLOTS, places ) );
	for( Int i = 0; i < 6; i++ )
		CHECK_EQ( places[ i ], i );
	CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_F );
	CHECK_EQ( places[ 7 ], (Int)COMMAND_PLACE_G );
	for( Int i = 0; i < SLOTS; i++ )
		CHECK( places[ i ] != (Int)COMMAND_PLACE_ATTACK && places[ i ] != (Int)COMMAND_PLACE_HOLD && places[ i ] != (Int)COMMAND_PLACE_MOVE );

	// a full set: five skills Q..T, five upgrades Z..B, seven units along A and then the one free key, Y
	KeyGridSlot full[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		full[ i ] = i < 5 ? power : i < 10 ? upgrade : unit;
	full[ 17 ] = sell;
	ControlBar_keyGridPlaces( full, SLOTS, places );
	for( Int i = 0; i < 5; i++ )
		CHECK_EQ( places[ i ], (Int)COMMAND_PLACE_Q + i );
	for( Int i = 0; i < 5; i++ )
		CHECK_EQ( places[ 5 + i ], (Int)COMMAND_PLACE_Z + i );
	for( Int i = 0; i < 6; i++ )
		CHECK_EQ( places[ 10 + i ], (Int)COMMAND_PLACE_A + i );
	CHECK_EQ( places[ 16 ], (Int)COMMAND_PLACE_Y );
	CHECK_EQ( places[ 17 ], (Int)COMMAND_PLACE_SELL );
	Bool seen[ COMMAND_PLACE_COUNT ] = { FALSE };
	for( Int i = 0; i < SLOTS; i++ )
		if( places[ i ] >= 0 && places[ i ] < COMMAND_PLACE_COUNT )
		{
			CHECK( !seen[ places[ i ] ] );
			seen[ places[ i ] ] = TRUE;
		}
}

/* Skill families (the owner's of 2026-11-28, on the Strategy Center): the battle plans on one row, the
   other powers (the scan) on the next, never mixed; the upgrades still climb from Z and finish the
   powers' row after them on a tall set.  A unit's weapon modes and its one-shot powers split the same way */
TEST(key_grid_splits_skills_by_family)
{
	enum { SLOTS = MAX_COMMANDS_PER_SET };
	const KeyGridSlot empty = { GUI_COMMAND_NONE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot plan = { GUI_COMMAND_SPECIAL_POWER, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1, KEY_GRID_FAMILY_PLAN };
	const KeyGridSlot scan = { GUI_COMMAND_SPECIAL_POWER, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1, KEY_GRID_FAMILY_POWER };
	const KeyGridSlot upgrade = { GUI_COMMAND_PLAYER_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot stop = { GUI_COMMAND_STOP, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot sell = { GUI_COMMAND_SELL, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot mode = { GUI_COMMAND_SWITCH_WEAPON, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1, KEY_GRID_FAMILY_MODE };
	const KeyGridSlot fire = { GUI_COMMAND_FIRE_WEAPON, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot attackMove = { GUI_COMMAND_ATTACK_MOVE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	const KeyGridSlot guard = { GUI_COMMAND_GUARD, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1 };
	Int places[ SLOTS ];

	// the Strategy Center: Bombardment, the scan, Hold the Line, Search and Destroy, six upgrades, stop, sell
	KeyGridSlot center[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		center[ i ] = empty;
	center[ 0 ] = plan;
	center[ 1 ] = scan;
	center[ 2 ] = plan;
	center[ 3 ] = plan;
	for( Int i = 4; i < 10; i++ )
		center[ i ] = upgrade;
	center[ 10 ] = stop;
	center[ 11 ] = sell;
	CHECK( !ControlBar_keyGridPlaces( center, SLOTS, places ) );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_E );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_A );
	static const Int CLIMB[ 6 ] = { COMMAND_PLACE_Z, COMMAND_PLACE_X, COMMAND_PLACE_C, COMMAND_PLACE_V, COMMAND_PLACE_B, COMMAND_PLACE_D };
	for( Int i = 0; i < 6; i++ )
		CHECK_EQ( places[ 4 + i ], CLIMB[ i ] );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_STOP );
	CHECK_EQ( places[ 11 ], (Int)COMMAND_PLACE_SELL );

	// four plans: the plans' row is Q alone, the scan still starts the next row
	center[ 12 ] = plan;
	ControlBar_keyGridPlaces( center, SLOTS, places );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_R );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_A );

	// EA's own Strategy Center set, the MOAB (an upgrade that names the Daisy Cutter power) among them:
	// the MOAB stands with the scan on the powers' row (D, past stop on S), the other five upgrades on Z
	{
		const KeyGridSlot moab = { GUI_COMMAND_PLAYER_UPGRADE, COMMAND_GROUP_ABILITY, -1, KEY_GRID_ROLE_OTHER, 0, 0, -1, KEY_GRID_FAMILY_POWER, 1 };
		KeyGridSlot ea[ SLOTS ];
		for( Int i = 0; i < SLOTS; i++ )
			ea[ i ] = empty;
		ea[ 0 ] = plan;			// Bombardment
		ea[ 1 ] = scan;			// CIA Intelligence
		ea[ 2 ] = plan;			// Hold the Line
		ea[ 4 ] = plan;			// Search and Destroy
		ea[ 5 ] = upgrade;	// Chemical Suits
		ea[ 6 ] = moab;
		ea[ 7 ] = upgrade;	// Composite Armor
		ea[ 8 ] = upgrade;	// Advanced Training
		ea[ 9 ] = upgrade;	// Drone Armor
		ea[ 10 ] = stop;
		ea[ 12 ] = upgrade;	// Supply Lines
		ea[ 13 ] = sell;
		CHECK( !ControlBar_keyGridPlaces( ea, SLOTS, places ) );
		CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_Q );
		CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_W );
		CHECK_EQ( places[ 4 ], (Int)COMMAND_PLACE_E );
		CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_A );
		CHECK_EQ( places[ 6 ], (Int)COMMAND_PLACE_D );
		CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_STOP );
		CHECK_EQ( places[ 5 ], (Int)COMMAND_PLACE_Z );
		CHECK_EQ( places[ 7 ], (Int)COMMAND_PLACE_X );
		CHECK_EQ( places[ 8 ], (Int)COMMAND_PLACE_C );
		CHECK_EQ( places[ 9 ], (Int)COMMAND_PLACE_V );
		CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_B );
		CHECK_EQ( places[ 13 ], (Int)COMMAND_PLACE_SELL );
	}

	// a unit with two weapon modes and two one-shot weapons: modes Q W, the weapons down on the A row
	// round A (held), S (stop) and D (attack move)
	KeyGridSlot soldier[ SLOTS ];
	for( Int i = 0; i < SLOTS; i++ )
		soldier[ i ] = empty;
	soldier[ 0 ] = fire;
	soldier[ 1 ] = mode;
	soldier[ 2 ] = fire;
	soldier[ 3 ] = mode;
	soldier[ 10 ] = attackMove;
	soldier[ 11 ] = guard;
	soldier[ 12 ] = stop;
	CHECK( ControlBar_keyGridPlaces( soldier, SLOTS, places ) );
	CHECK_EQ( places[ 1 ], (Int)COMMAND_PLACE_Q );
	CHECK_EQ( places[ 3 ], (Int)COMMAND_PLACE_W );
	CHECK_EQ( places[ 0 ], (Int)COMMAND_PLACE_F );
	CHECK_EQ( places[ 2 ], (Int)COMMAND_PLACE_G );
	CHECK_EQ( places[ 12 ], (Int)COMMAND_PLACE_STOP );
	CHECK_EQ( places[ 10 ], (Int)COMMAND_PLACE_ATTACK_MOVE );
	CHECK_EQ( places[ 11 ], (Int)COMMAND_PLACE_GUARD );
}

/* The squad tray on the deck: entries on the places in reading order, none past N */
TEST(key_grid_tray_fills_the_places_in_reading_order)
{
	CHECK_EQ( KeyGrid_trayPlace( 0 ), (Int)COMMAND_PLACE_Q );
	CHECK_EQ( KeyGrid_trayPlace( 6 ), (Int)COMMAND_PLACE_A );
	CHECK_EQ( KeyGrid_trayPlace( 17 ), (Int)COMMAND_PLACE_N );
	CHECK_EQ( KeyGrid_trayPlace( 18 ), -1 );
	CHECK_EQ( KeyGrid_trayPlace( -1 ), -1 );
	for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
		CHECK_EQ( ControlBar_commandPlaceKey( place ), (WideChar)"QWERTYASDFGHZXCVBN"[ place ] );
	CHECK_EQ( ControlBar_commandPlaceKey( COMMAND_PLACE_COUNT ), 0 );
}

/* Key Grid's keys: every place on its letter bare in HotkeysGrid.ini's scheme, nothing else bare on one
   of the eighteen, no fixed letters, and on the bar no button letter of its own (its place presses it) */
TEST(key_grid_scheme_keys_the_places)
{
	CHECK_EQ( hotkeySchemeFor( HUD_LAYOUT_GRID, TRUE ), (Int)HOTKEY_SCHEME_GRID );
	CHECK_EQ( hotkeySchemeFor( HUD_LAYOUT_GRID, FALSE ), (Int)HOTKEY_SCHEME_CLASSIC );
	CHECK_EQ( hotkeySchemeFor( HUD_LAYOUT_CLASSIC, TRUE ), (Int)HOTKEY_SCHEME_CLASSIC );
	CHECK_EQ( hotkeySchemeFor( HUD_LAYOUT_SIDEBAR, TRUE ), (Int)HOTKEY_SCHEME_RED_ALERT );
	CHECK_STR( HotkeyScheme_fileName( HOTKEY_SCHEME_GRID ), "HotkeysGrid.ini" );
	CHECK_STR( HotkeyScheme_fixedLetters( HOTKEY_SCHEME_GRID ), "" );
	Int count = 0;
	const HotkeySchemeKey *keys = HotkeyScheme_keys( HOTKEY_SCHEME_GRID, &count );
	CHECK( keys != NULL );
	Int slots = 0;
	for( Int each = 0; keys && each < count; each++ )
	{
		CHECK( Hotkeys_isKeyName( keys[ each ].key ) );
		const Bool bare = strcmp( keys[ each ].modifiers, "NONE" ) == 0;
		const Bool letter = strlen( keys[ each ].key ) == 5 && strncmp( keys[ each ].key, "KEY_", 4 ) == 0;
		const WideChar k = letter ? (WideChar)keys[ each ].key[ 4 ] : 0;
		Bool placeKey = FALSE;
		for( Int place = 0; place < COMMAND_PLACE_COUNT; place++ )
			placeKey = placeKey || ControlBar_commandPlaceKey( place ) == k;
		if( strncmp( keys[ each ].command, "COMMAND_SLOT", 12 ) == 0 )
		{
			const Int place = atoi( keys[ each ].command + 12 ) - 1;
			CHECK( bare && letter );
			CHECK_EQ( k, ControlBar_commandPlaceKey( place ) );
			slots++;
		}
		else if( bare && letter )
			CHECK( !placeKey );
		for( Int other = each + 1; other < count; other++ )
			CHECK( strcmp( keys[ each ].key, keys[ other ].key ) != 0 || strcmp( keys[ each ].modifiers, keys[ other ].modifiers ) != 0 );
	}
	CHECK_EQ( slots, (Int)COMMAND_PLACE_COUNT );

	std::map< std::string, WideChar > chosen;
	chosen[ "command_guard@3" ] = 'K';
	chosen[ "command_guard" ] = 'K';
	chosen[ "command_scatter" ] = 'Q';
	const AsciiString guard( "Command_Guard" ), scatter( "Command_Scatter" ), power( "Command_SpecialPower" );
	// on the bar no letter at all, the player's included
	CHECK_EQ( HotKeyManager::buttonKeyIn( guard, 'G', GUI_COMMAND_GUARD, chosen, 2, -1, HOTKEY_SCHEME_GRID ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( power, 'P', GUI_COMMAND_SPECIAL_POWER, chosen, 0, -1, HOTKEY_SCHEME_GRID ), 0 );
	// off the bar the player's letter or the label's, unless it is one of the eighteen
	CHECK_EQ( HotKeyManager::buttonKeyIn( guard, 'G', GUI_COMMAND_GUARD, chosen, -1, -1, HOTKEY_SCHEME_GRID ), (WideChar)'K' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( scatter, 0, GUI_COMMAND_NONE, chosen, -1, -1, HOTKEY_SCHEME_GRID ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( power, 'P', GUI_COMMAND_SPECIAL_POWER, chosen, -1, -1, HOTKEY_SCHEME_GRID ), (WideChar)'P' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( power, 'U', GUI_COMMAND_SPECIAL_POWER, chosen, -1, -1, HOTKEY_SCHEME_GRID ), 0 );
	CHECK_EQ( HotKeyManager::fixedKey( guard, GUI_COMMAND_GUARD, HOTKEY_SCHEME_GRID ), 0 );
	CHECK_EQ( HotKeyManager::fixedKey( guard, GUI_COMMAND_GUARD, HOTKEY_SCHEME_CLASSIC ), (WideChar)'G' );
}

/* The Grid Keys box: off by default, saved as its own line, and with the layout it picks the scheme:
   Key Grid's own keys only under Key Grid with the box ticked; Red Alert Style keeps its keys either way */
TEST(key_grid_grid_keys_option_picks_the_scheme)
{
	const OptionDef *grid = findOptionDef( "GridKeys" );
	const OptionDef *layout = findOptionDef( "HudLayout" );
	CHECK( grid != NULL && layout != NULL );
	if( grid == NULL || layout == NULL )
		return;
	CHECK_EQ( (Int)grid->kind, (Int)OPTION_BOOL );
	CHECK_STR( grid->widgetName, "OptionsMenu.wnd:CheckGridKeys" );

	GlobalData *saved = TheWritableGlobalData;
	TheWritableGlobalData = NEW GlobalData;
	CHECK( !TheGlobalData->m_gridKeys );
	CHECK_EQ( optionDefaultValue( *grid ), 0 );

	UserPreferences pref;
	pref[ AsciiString( "HudLayout" ) ] = AsciiString( "3" );
	pref[ AsciiString( "GridKeys" ) ] = AsciiString( "yes" );
	loadOptionsFromPreferences( pref );
	CHECK_EQ( TheGlobalData->m_hudLayout, (Int)HUD_LAYOUT_GRID );
	CHECK( TheGlobalData->m_gridKeys );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_GRID );

	// either setter, in either order, keeps the scheme on the pair
	grid->set( 0 );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_CLASSIC );
	layout->set( HUD_LAYOUT_SIDEBAR );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_RED_ALERT );
	grid->set( 1 );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_RED_ALERT );
	layout->set( HUD_LAYOUT_GRID );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_GRID );
	layout->set( HUD_LAYOUT_CLASSIC );
	CHECK_EQ( TheGlobalData->m_hotkeyScheme, (Int)HOTKEY_SCHEME_CLASSIC );
	CHECK( !hudLayoutHasCommandTabs( HUD_LAYOUT_GRID ) );

	// the box's grid by window slot (Q W E R T Y U over A S D F G H J) on the bottom bar, never on the
	// Red Alert sidebar's strip, whose bare Q W E R and D are its tabs and deploy
	const Bool wasGrid = HotKeyManager::commandBarGrid(), wasFixed = HotKeyManager::fixedOrderKeys();
	HotKeyManager::setCommandBarGrid( FALSE );
	HotKeyManager::setFixedOrderKeys( TRUE );
	std::map< std::string, WideChar > none;
	const AsciiString stop( "Command_Stop" );
	CHECK_EQ( HotKeyManager::buttonKeyIn( stop, 'S', GUI_COMMAND_STOP, none, 0, -1, HOTKEY_SCHEME_CLASSIC ), (WideChar)'Q' );
	layout->set( HUD_LAYOUT_SIDEBAR );
	CHECK_EQ( HotKeyManager::buttonKeyIn( stop, 'S', GUI_COMMAND_STOP, none, 0, -1, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'S' );
	grid->set( 0 );
	layout->set( HUD_LAYOUT_CLASSIC );
	CHECK_EQ( HotKeyManager::buttonKeyIn( stop, 'S', GUI_COMMAND_STOP, none, 0, -1, HOTKEY_SCHEME_CLASSIC ), (WideChar)'S' );
	HotKeyManager::setCommandBarGrid( wasGrid );
	HotKeyManager::setFixedOrderKeys( wasFixed );

	delete TheWritableGlobalData;
	TheWritableGlobalData = saved;
}

/* What the English CommandMap.ini puts bare on the eighteen letters, and where Key Grid's keys move it:
   E Q H W X and the replay's F to U J M Ctrl+U Ctrl+J Ctrl+M, Deploy to K, Follow to L, the path build
   hold to O.  Stop alone gives up its S, the stop place's own key (ControlBar's orderAtPlace) */
TEST(key_grid_scheme_moves_what_classic_had_on_the_places)
{
	static const char MAP[] =
		"CommandMap SELECT_MATCHING_UNITS\r\n  Key = KEY_E\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap VIEW_COMMAND_CENTER\r\n  Key = KEY_H\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap SELECT_ALL\r\n  Key = KEY_Q\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap SELECT_ALL_AIRCRAFT\r\n  Key = KEY_W\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap SCATTER\r\n  Key = KEY_X\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = CONTROL\r\nEnd\r\n"
		"CommandMap STOP\r\n  Key = KEY_S\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = CONTROL\r\nEnd\r\n"
		"CommandMap DEPLOY\r\n  Key = KEY_D\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = CONTROL\r\nEnd\r\n"
		"CommandMap FOLLOW\r\n  Key = KEY_F\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = CONTROL\r\nEnd\r\n"
		"CommandMap BEGIN_PATH_BUILD\r\n  Key = KEY_Z\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = CONTROL\r\nEnd\r\n"
		"CommandMap TOGGLE_FAST_FORWARD_REPLAY\r\n  Key = KEY_F\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = INTERFACE\r\nEnd\r\n";
	std::vector< HotkeyCommandRow > rows = Hotkeys_commandRows( MAP );
	HotkeyScheme_applyToRows( rows, HOTKEY_SCHEME_GRID );
	struct Move { const char *id; const char *key; const char *mods; };
	static const Move moves[] =
	{
		{ "SELECT_MATCHING_UNITS", "KEY_U", "NONE" }, { "SELECT_ALL", "KEY_J", "NONE" }, { "VIEW_COMMAND_CENTER", "KEY_M", "NONE" },
		{ "SELECT_ALL_AIRCRAFT", "KEY_U", "CTRL" }, { "SCATTER", "KEY_J", "CTRL" }, { "TOGGLE_FAST_FORWARD_REPLAY", "KEY_M", "CTRL" },
		{ "DEPLOY", "KEY_K", "NONE" }, { "FOLLOW", "KEY_L", "NONE" }, { "BEGIN_PATH_BUILD", "KEY_O", "NONE" },
	};
	for( Int each = 0; each < (Int)( sizeof( moves ) / sizeof( moves[ 0 ] ) ); each++ )
	{
		Bool found = FALSE;
		for( size_t row = 0; row < rows.size(); row++ )
			if( rows[ row ].id == moves[ each ].id )
			{
				found = TRUE;
				CHECK( rows[ row ].binding == HotkeyBinding( moves[ each ].key, moves[ each ].mods ) );
			}
		CHECK( found );
	}
	for( size_t row = 0; row < rows.size(); row++ )
		if( rows[ row ].id == "STOP" )
			CHECK( rows[ row ].binding.key.empty() );
}
