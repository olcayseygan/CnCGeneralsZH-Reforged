// The observer camera's director and its pan, included into test_gameengine.cpp.

#include "GameClient/ObserverCamera.h"

static DirectorHeat directorHit( Real x, Real y )
{
	DirectorHeat hit;
	hit.position.x = x;
	hit.position.y = y;
	hit.weight = 1.0f;
	return hit;
}

TEST(the_director_goes_to_the_crowded_fight_not_the_lone_hit)
{
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 5; each++ )
		hits.push_back( directorHit( 1000.0f + each * 20.0f, 1000.0f ) );
	hits.push_back( directorHit( 3000.0f, 3000.0f ) );
	hits.push_back( directorHit( 3010.0f, 3000.0f ) );

	Coord2D place;
	Real heat = 0.0f;
	CHECK( ObserverCamera_hottestPlace( hits, &place, &heat ) );
	CHECK_NEAR( heat, 5.0f, 0.001f );
	CHECK_NEAR( place.x, 1040.0f, 0.001f );
	CHECK_NEAR( place.y, 1000.0f, 0.001f );

	std::vector< DirectorHeat > none;
	CHECK( !ObserverCamera_hottestPlace( none, &place, &heat ) );
}

TEST(the_director_holds_a_live_fight_and_leaves_a_dead_one)
{
	const UnsignedInt longHeld = 15 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt justArrived = LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_shouldMove( 0.0f, 1.0f, justArrived, 0.0f ) );
	CHECK( !ObserverCamera_shouldMove( 0.0f, 0.0f, longHeld, 0.0f ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 20.0f, justArrived, 4.0f ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 7.0f, longHeld, 4.0f ) );
	CHECK( ObserverCamera_shouldMove( 4.0f, 9.0f, longHeld, 4.0f ) );
	// six seconds in is still inside the hold
	CHECK( !ObserverCamera_shouldMove( 4.0f, 9.0f, 6 * LOGICFRAMES_PER_SECOND, 4.0f ) );
}

TEST(the_director_lets_a_fading_fight_go_once_settled)
{
	const UnsignedInt settled = 5 * LOGICFRAMES_PER_SECOND;
	// burnt down from 10 to 4, a fight a third hotter elsewhere is enough inside the hold
	CHECK( ObserverCamera_shouldMove( 4.0f, 5.4f, settled, 10.0f ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 5.0f, settled, 10.0f ) );
	// still at its peak it holds
	CHECK( !ObserverCamera_shouldMove( 4.0f, 5.4f, settled, 4.0f ) );
	// and fading or not, nothing moves it before the settle is up
	CHECK( !ObserverCamera_shouldMove( 4.0f, 5.4f, 3 * LOGICFRAMES_PER_SECOND, 10.0f ) );
}

TEST(the_director_leaves_early_for_a_much_bigger_fight_and_late_for_any_bigger_one)
{
	const UnsignedInt settled = 7 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt tired = 35 * LOGICFRAMES_PER_SECOND;
	CHECK( ObserverCamera_shouldMove( 4.0f, 11.0f, settled, 4.0f ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 9.0f, settled, 4.0f ) );
	// not even a much bigger fight pulls it away in its first seconds
	CHECK( !ObserverCamera_shouldMove( 4.0f, 40.0f, 3 * LOGICFRAMES_PER_SECOND, 4.0f ) );
	CHECK( ObserverCamera_shouldMove( 4.0f, 5.4f, tired, 4.0f ) );
	CHECK( !ObserverCamera_shouldMove( 4.0f, 5.0f, tired, 4.0f ) );
}

TEST(the_director_goes_where_more_is_at_stake_not_where_more_was_hit)
{
	// six rifles plinked at one end, two Overlords killed at the other
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 6; each++ )
		hits.push_back( directorHit( 1000.0f + each * 10.0f, 1000.0f ) );
	for( Int each = 0; each < 2; each++ )
	{
		DirectorHeat hit = directorHit( 3000.0f + each * 10.0f, 3000.0f );
		hit.weight = ObserverCamera_hitWeight( 2000, TRUE, FALSE, FALSE );
		hits.push_back( hit );
	}
	for( Int each = 0; each < 6; each++ )
		hits[ each ].weight = ObserverCamera_hitWeight( 225, FALSE, FALSE, FALSE );

	Coord2D place;
	Real heat = 0.0f;
	CHECK( ObserverCamera_hottestPlace( hits, &place, &heat ) );
	CHECK_NEAR( place.y, 3000.0f, 0.001f );
	CHECK( ObserverCamera_hitWeight( 5000, FALSE, TRUE, FALSE ) > ObserverCamera_hitWeight( 5000, FALSE, FALSE, FALSE ) );
}

TEST(a_skirmish_of_riflemen_does_not_take_the_camera_from_two_tanks)
{
	// eight riflemen shot at, two of them killed, against two tanks hit once each
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 8; each++ )
	{
		DirectorHeat hit = directorHit( 1000.0f + each * 10.0f, 1000.0f );
		hit.weight = ObserverCamera_hitWeight( 225, each < 2, FALSE, TRUE );
		hits.push_back( hit );
	}
	for( Int each = 0; each < 2; each++ )
	{
		DirectorHeat hit = directorHit( 3000.0f + each * 10.0f, 3000.0f );
		hit.weight = ObserverCamera_hitWeight( 900, FALSE, FALSE, FALSE );
		hits.push_back( hit );
	}
	Coord2D place;
	Real heat = 0.0f;
	CHECK( ObserverCamera_hottestPlace( hits, &place, &heat ) );
	CHECK_NEAR( place.y, 3000.0f, 0.001f );
}

TEST(a_special_power_holds_the_camera_thirty_seconds_at_the_most)
{
	DirectorEvent cannon;
	cannon.since = 1000;
	cannon.until = 1000 + 20 * LOGICFRAMES_PER_SECOND;
	// a hit at the target keeps it five seconds past the hit
	CHECK_EQ( ObserverCamera_eventKeptTo( cannon, 1000 + 18 * LOGICFRAMES_PER_SECOND, 5 * LOGICFRAMES_PER_SECOND ),
		1000u + 23 * LOGICFRAMES_PER_SECOND );
	// and never past 30 seconds from the launch: the fight round S1-A1's middle kept one for eight minutes
	CHECK_EQ( ObserverCamera_eventKeptTo( cannon, 1000 + 400 * LOGICFRAMES_PER_SECOND, 5 * LOGICFRAMES_PER_SECOND ),
		1000u + 30 * LOGICFRAMES_PER_SECOND );
}

TEST(the_director_with_no_fight_goes_round_the_sights_and_never_parks)
{
	std::vector< DirectorHeat > sights;
	DirectorHeat army = directorHit( 1000.0f, 1000.0f );
	army.weight = ObserverCamera_sightWeight( 4000, FALSE, TRUE, FALSE );
	DirectorHeat base = directorHit( 3000.0f, 3000.0f );
	base.weight = ObserverCamera_sightWeight( 6000, TRUE, FALSE, FALSE );
	DirectorHeat silo = directorHit( 5000.0f, 1000.0f );
	silo.weight = ObserverCamera_sightWeight( 5000, TRUE, FALSE, TRUE );
	sights.push_back( base );
	sights.push_back( army );
	sights.push_back( silo );

	// the marching army first, then the superweapon, then the base, then nothing new
	std::vector< Coord2D > seen;
	Coord2D place;
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 1000.0f, 0.001f );
	seen.push_back( place );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 5000.0f, 0.001f );
	seen.push_back( place );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 3000.0f, 0.001f );
	seen.push_back( place );
	CHECK( !ObserverCamera_nextSight( sights, seen, &place ) );

	// with the oldest dropped the army comes round again
	seen.erase( seen.begin() );
	CHECK( ObserverCamera_nextSight( sights, seen, &place ) );
	CHECK_NEAR( place.x, 1000.0f, 0.001f );
}

static DirectorEvent directorEvent( UnsignedInt id, Real weight, Bool superweapon )
{
	DirectorEvent event;
	event.id = id;
	event.owner = NULL;
	event.source.x = event.source.y = 500.0f;
	event.target.x = 3000.0f;
	event.target.y = 2000.0f;
	event.since = 1000;
	event.until = 2000;
	event.weight = weight;
	event.superweapon = superweapon;
	event.landed = TRUE;
	event.looksOnly = FALSE;
	event.landsAt = 0;
	event.shown = FALSE;
	event.power = NULL;
	event.sourceThing = NULL;
	return event;
}

TEST(a_power_that_only_looks_never_takes_the_camera)
{
	// the owner's rule: a scan never cuts, nor anything that hurts nobody where it is aimed
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_SPY_SATELLITE ) );
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_RADAR_VAN_SCAN ) );
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_SPY_DRONE ) );
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_CASH_HACK ) );
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_LEAFLET_DROP ) );
	CHECK( ObserverCamera_powerLooksOnly( SPECIAL_PARADROP_AMERICA ) );
	// an EMP brings down the aircraft under it
	CHECK( !ObserverCamera_powerLooksOnly( SPECIAL_EMP_PULSE ) );
	CHECK( !ObserverCamera_powerLooksOnly( SPECIAL_CARPET_BOMB ) );
	CHECK( !ObserverCamera_powerLooksOnly( SPECIAL_ARTILLERY_BARRAGE ) );
	CHECK( !ObserverCamera_powerLooksOnly( SPECIAL_A10_THUNDERBOLT_STRIKE ) );
	CHECK( !ObserverCamera_powerLooksOnly( SPECIAL_PARTICLE_UPLINK_CANNON ) );

	// a spy satellite over the central battle: the fight round its target lands it no more, with a
	// timeline or without (S1-A1 special power 3, frame 3603)
	DirectorEvent scan = directorEvent( 1, 1.0f, FALSE );
	scan.landed = FALSE;
	scan.looksOnly = TRUE;
	CHECK( !ObserverCamera_landsNow( scan, 1100, FALSE, TRUE ) );
	CHECK( !ObserverCamera_landsNow( scan, 1100, TRUE, TRUE ) );
	scan.landsAt = 1100;
	CHECK( !ObserverCamera_landsNow( scan, 1100, TRUE, TRUE ) );
}

TEST(a_strike_lands_when_the_scout_saw_it_hit_and_without_one_on_a_fight_near_its_target)
{
	DirectorEvent strike = directorEvent( 1, 1.0f, FALSE );
	strike.landed = FALSE;
	const UnsignedInt preroll = 4 * LOGICFRAMES_PER_SECOND;
	// the scouting pass saw its bombs fall 20 seconds after the launch: it lands from the pre-roll before
	strike.landsAt = 1000 + 20 * LOGICFRAMES_PER_SECOND;
	CHECK( !ObserverCamera_landsNow( strike, strike.landsAt - preroll - 1, TRUE, TRUE ) );
	CHECK( ObserverCamera_landsNow( strike, strike.landsAt - preroll, TRUE, FALSE ) );
	// the scouting pass saw it land nowhere: no fight beside its target lands it
	strike.landsAt = 0;
	CHECK( !ObserverCamera_landsNow( strike, 1100, TRUE, TRUE ) );
	// with no timeline a fight near the target is the landing
	CHECK( !ObserverCamera_landsNow( strike, 1100, FALSE, FALSE ) );
	CHECK( ObserverCamera_landsNow( strike, 1100, FALSE, TRUE ) );
}

TEST(a_power_off_the_picture_gets_pane_one_and_a_superweapon_takes_it_from_a_lighter_one)
{
	// a particle cannon shown, and an artillery barrage fired the same frame across the base: inside a
	// split's distance of the cannon it got no pane and was missed, though the cannon's picture did not
	// reach it
	std::vector< DirectorEvent > events;
	events.push_back( directorEvent( 1, 5.0f, TRUE ) );
	events.push_back( directorEvent( 2, 1.0f, FALSE ) );
	events[ 1 ].target.x = 3700.0f;
	const Coord2D place = events[ 0 ].target;
	const Real screen = 1200.0f;
	CHECK( ObserverCamera_offPicture( events[ 1 ].target, place, screen ) );
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 0, place, screen ), 2u );
	// one on the picture already needs no pane, nor one still to land, nor the one shown
	events[ 1 ].target.x = 3300.0f;
	CHECK( !ObserverCamera_offPicture( events[ 1 ].target, place, screen ) );
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 0, place, screen ), 0u );
	events[ 1 ].target.x = 3700.0f;
	events[ 1 ].landed = FALSE;
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 0, place, screen ), 0u );
	events[ 1 ].landed = TRUE;
	CHECK_EQ( ObserverCamera_secondPower( events, 2, 0, events[ 1 ].target, screen ), 1u );
	// before the picture's ground is measured, the gather radius
	CHECK( ObserverCamera_offPicture( events[ 1 ].target, place, 0.0f ) );
	// pane 1 keeps its power against another as big, and gives it up to a superweapon: two strikes
	// holding the panes left a superweapon fired meanwhile unfilmed until one of them was over
	events.push_back( directorEvent( 3, 1.0f, FALSE ) );
	events[ 2 ].target.x = 6000.0f;
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 0, place, screen ), 3u );
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 2, place, screen ), 2u );
	events[ 2 ].weight = 5.0f;
	events[ 2 ].superweapon = TRUE;
	CHECK_EQ( ObserverCamera_secondPower( events, 1, 2, place, screen ), 3u );
}

TEST(the_corner_window_shows_a_second_power_before_a_second_fight)
{
	CHECK_EQ( (Int)ObserverCamera_pickWindow( 0, FALSE ), (Int)DIRECTOR_WINDOW_NONE );
	CHECK_EQ( (Int)ObserverCamera_pickWindow( 0, TRUE ), (Int)DIRECTOR_WINDOW_FIGHT );
	CHECK_EQ( (Int)ObserverCamera_pickWindow( 7, TRUE ), (Int)DIRECTOR_WINDOW_POWER );
	CHECK_EQ( (Int)ObserverCamera_pickWindow( 7, FALSE ), (Int)DIRECTOR_WINDOW_POWER );
}

TEST(the_corner_window_slides_all_the_way_in_before_it_goes_and_out_before_it_comes)
{
	// the slide is a pane's, half a second of the retail clock
	const UnsignedInt slide = ConvertRetailFramesWall( 15U );
	// down and nothing wanted: stays down; wanted: comes once the last slide out is done
	CHECK( !ObserverCamera_holdWindow( FALSE, FALSE, 1000 ) );
	CHECK( ObserverCamera_holdWindow( FALSE, TRUE, slide ) );
	CHECK( !ObserverCamera_holdWindow( FALSE, TRUE, slide - 1 ) );
	// up: stays while wanted, and while still sliding in when nothing is; goes once in
	CHECK( ObserverCamera_holdWindow( TRUE, TRUE, 0 ) );
	CHECK( ObserverCamera_holdWindow( TRUE, FALSE, slide - 1 ) );
	CHECK( !ObserverCamera_holdWindow( TRUE, FALSE, slide ) );
}

TEST(the_corner_window_is_a_quarter_of_the_picture_in_the_lower_right_and_slides_in_from_past_it)
{
	CHECK_EQ( ObserverCamera_windowRect( 1920, 1080, 0.0f ).hi.x, 0 );
	const IRegion2D in = ObserverCamera_windowRect( 1920, 1080, 1.0f );
	CHECK_EQ( in.hi.x - in.lo.x, 480 );
	CHECK_EQ( in.hi.y - in.lo.y, 270 );
	const DirectorWindowFrame frame = ObserverCamera_windowFrame( 1080 );
	// the plate on the screen, a margin in from the right edge and the foot
	CHECK( in.hi.x + frame.border < 1920 );
	CHECK( in.hi.y + frame.border < 1080 );
	CHECK( in.hi.x > 1920 - 60 );
	CHECK( in.hi.y > 1080 - 60 );
	// the lower left stays clear for the event map, and the header with its chip is over the picture
	CHECK( in.lo.x - frame.border > 1920 / 2 );
	CHECK( in.lo.y - frame.header > 1080 / 2 );
	CHECK( frame.header > frame.border );
	CHECK( frame.cut > 0 && frame.rule > 0 && frame.highlight < frame.border );
	// barely in, it and its plate are still past the right edge; half in, part of the way
	CHECK( ObserverCamera_windowRect( 1920, 1080, 0.001f ).lo.x - frame.border >= 1919 );
	CHECK( ObserverCamera_windowRect( 1920, 1080, 0.5f ).lo.x > in.lo.x );
	CHECK_EQ( ObserverCamera_windowRect( 1920, 1080, 0.5f ).lo.y, in.lo.y );
	// the recording shrinks the whole picture into it exactly
	const IRegion2D at720 = ObserverCamera_windowRect( 1280, 720, 1.0f );
	CHECK_EQ( ( at720.hi.x - at720.lo.x ) * OBSERVER_WINDOW_SCALE, 1280 );
	CHECK_EQ( ( at720.hi.y - at720.lo.y ) * OBSERVER_WINDOW_SCALE, 720 );
}

TEST(an_events_file_line_reads_back_as_the_event_it_was_written_from)
{
	DirectorFilmEvent event;
	event.frame = 5400;
	event.type = "lost";
	event.player = 3;
	event.x = 1234.4f;
	event.y = 987.6f;
	event.thing = "AmericaTankCrusader";
	event.name = ObserverCamera_filmName( UnicodeString( u"Crusader Tank" ) );
	const AsciiString line = ObserverCamera_formatFilmEvent( event );
	CHECK_STR( line.str(), "event 5400 lost 3 1234 988 AmericaTankCrusader Crusader Tank" );
	DirectorFilmEvent read;
	CHECK( ObserverCamera_parseFilmEvent( line.str(), &read ) );
	CHECK_EQ( read.frame, 5400u );
	CHECK_STR( read.type.str(), "lost" );
	CHECK_EQ( read.player, 3 );
	CHECK_NEAR( read.x, 1234.0f, 0.001f );
	CHECK_NEAR( read.y, 988.0f, 0.001f );
	CHECK_STR( read.thing.str(), "AmericaTankCrusader" );
	CHECK_STR( read.name.str(), "Crusader Tank" );
	CHECK( ObserverCamera_parseFilmEvent( "event 1 built 2 10 20 ChinaBarracks Barracks\r\n", &read ) );
	CHECK_STR( read.name.str(), "Barracks" );

	// a button's label drops its hotkey marker and its line break; a Turkish name stays UTF-8
	CHECK_STR( ObserverCamera_filmName( UnicodeString( u"&Neutron\nMissile " ) ).str(), "Neutron Missile" );
	// "Karisik" with the dotless i and the s cedilla, by number, since the source is not read as UTF-8
	const WideChar turkish[] = { u'K', u'a', u'r', 0x0131, 0x015F, 0x0131, u'k', 0 };
	CHECK_STR( ObserverCamera_filmName( UnicodeString( turkish ) ).str(), "Kar\xc4\xb1\xc5\x9f\xc4\xb1k" );
	CHECK_STR( ObserverCamera_filmName( UnicodeString::TheEmptyString ).str(), "-" );

	// a player's defeat has no place, no thing and no name
	event.type = "defeated";
	event.x = event.y = -1.0f;
	event.thing.clear();
	event.name.clear();
	CHECK_STR( ObserverCamera_formatFilmEvent( event ).str(), "event 5400 defeated 3 -1 -1 - -" );
	CHECK( !ObserverCamera_parseFilmEvent( "player 0 FactionAmerica FF0000 Bob", &read ) );
	CHECK( !ObserverCamera_parseFilmEvent( "event 5400 defeated 3 -1 -1 -", &read ) );

	// a map's file name may hold spaces, so its extent is read off the end
	const AsciiString map = ObserverCamera_formatFilmMap( "maps\\tournament desert\\tournament desert.map", 0.0f, 0.0f, 4000.0f, 3500.0f );
	CHECK_STR( map.str(), "map maps\\tournament desert\\tournament desert.map 0 0 4000 3500" );
	AsciiString mapFile;
	Real lo[ 2 ], hi[ 2 ];
	CHECK( ObserverCamera_parseFilmMap( ( std::string( map.str() ) + "\n" ).c_str(), &mapFile, &lo[ 0 ], &lo[ 1 ], &hi[ 0 ], &hi[ 1 ] ) );
	CHECK_STR( mapFile.str(), "maps\\tournament desert\\tournament desert.map" );
	CHECK_NEAR( hi[ 0 ], 4000.0f, 0.001f );
	CHECK_NEAR( hi[ 1 ], 3500.0f, 0.001f );
	CHECK( !ObserverCamera_parseFilmMap( "map 0 0 4000 3500", &mapFile, &lo[ 0 ], &lo[ 1 ], &hi[ 0 ], &hi[ 1 ] ) );

	// a player's display name is last and may hold spaces; the colour is six hex digits
	const AsciiString player = ObserverCamera_formatFilmPlayer( 2, "FactionAmericaAirForceGeneral", 0xFF2840E0u, "Hard AI" );
	CHECK_STR( player.str(), "player 2 FactionAmericaAirForceGeneral 2840E0 Hard AI" );
	Int index = -1;
	AsciiString playerTemplate, name;
	UnsignedInt rgb = 0;
	CHECK( ObserverCamera_parseFilmPlayer( ( std::string( player.str() ) + "\r\n" ).c_str(), &index, &playerTemplate, &rgb, &name ) );
	CHECK_EQ( index, 2 );
	CHECK_STR( playerTemplate.str(), "FactionAmericaAirForceGeneral" );
	CHECK_EQ( rgb, 0x2840E0u );
	CHECK_STR( name.str(), "Hard AI" );
}

TEST(the_director_keeps_its_event_when_none_may_be_shown_any_more)
{
	// narrowed to a player whose things at the target all died, the event it shows is filtered out
	// and nothing is left to compare it with; this read a NULL best and crashed
	const DirectorEvent nuke = directorEvent( 1, 5.0f, TRUE );
	CHECK( ObserverCamera_stayOnEvent( &nuke, NULL ) );
	CHECK( ObserverCamera_stayOnEvent( &nuke, &nuke ) );
	CHECK( !ObserverCamera_stayOnEvent( NULL, &nuke ) );
	CHECK( !ObserverCamera_stayOnEvent( NULL, NULL ) );
}

TEST(a_new_power_landing_elsewhere_takes_the_camera_at_once)
{
	DirectorEvent nuke = directorEvent( 1, 5.0f, TRUE );
	nuke.target.x = 5000.0f;
	DirectorEvent strike = directorEvent( 2, 1.0f, FALSE );
	DirectorEvent scan = directorEvent( 3, 1.0f, FALSE );
	scan.target.x = 3500.0f;
	// a superweapon takes the camera from a strike at once, and a strike never takes it from one
	CHECK( !ObserverCamera_stayOnEvent( &strike, &nuke ) );
	CHECK( ObserverCamera_stayOnEvent( &nuke, &strike ) );
	// a power as big elsewhere goes at once; one landing in the picture already shown does not move it
	CHECK( !ObserverCamera_stayOnEvent( &strike, &scan ) );
	scan.target.x = 3100.0f;
	CHECK( ObserverCamera_stayOnEvent( &strike, &scan ) );
}

TEST(one_use_of_a_power_is_one_event_and_another_power_or_player_is_new)
{
	// any two distinct addresses stand for two powers and two players; only identity is compared
	static const int powers[ 2 ] = { 0, 0 };
	static const int players[ 2 ] = { 0, 0 };
	const SpecialPowerTemplate *strike = (const SpecialPowerTemplate *)&powers[ 0 ];
	const SpecialPowerTemplate *bomb = (const SpecialPowerTemplate *)&powers[ 1 ];
	const Player *one = (const Player *)&players[ 0 ];
	const Player *two = (const Player *)&players[ 1 ];
	DirectorEvent used = directorEvent( 1, 1.0f, FALSE );
	used.owner = one;
	used.power = strike;
	Coord2D beside = { 3100.0f, 2050.0f };
	Coord2D away = { 3600.0f, 2000.0f };
	CHECK( ObserverCamera_sameUse( used, one, strike, beside, 1500 ) );
	CHECK( !ObserverCamera_sameUse( used, one, strike, away, 1500 ) );
	CHECK( !ObserverCamera_sameUse( used, one, bomb, beside, 1500 ) );
	CHECK( !ObserverCamera_sameUse( used, two, strike, beside, 1500 ) );
	// once the first is over the next is a use of its own
	CHECK( !ObserverCamera_sameUse( used, one, strike, beside, 2000 ) );
	// a warhead nobody's power sent is never folded into another
	used.power = NULL;
	CHECK( !ObserverCamera_sameUse( used, one, NULL, beside, 1500 ) );
}

TEST(the_director_rises_over_a_wide_fight_and_not_over_a_tight_one)
{
	std::vector< DirectorHeat > tight;
	std::vector< DirectorHeat > wide;
	for( Int each = 0; each < 4; each++ )
	{
		tight.push_back( directorHit( 1000.0f + ( each - 2 ) * 10.0f, 1000.0f ) );
		wide.push_back( directorHit( 1000.0f + ( each % 2 ? 150.0f : -150.0f ), 1000.0f ) );
	}
	Coord2D middle;
	middle.x = middle.y = 1000.0f;
	CHECK_NEAR( ObserverCamera_spreadAround( wide, middle ), 150.0f, 0.01f );
	CHECK_NEAR( ObserverCamera_fightHeight( ObserverCamera_spreadAround( tight, middle ) ), 0.0f, 0.001f );
	CHECK_NEAR( ObserverCamera_fightHeight( ObserverCamera_spreadAround( wide, middle ) ), 135.0f, 0.01f );
	CHECK( ObserverCamera_fightHeight( 10000.0f ) <= 300.0f );
	std::vector< DirectorHeat > none;
	CHECK_NEAR( ObserverCamera_spreadAround( none, middle ), 0.0f, 0.001f );
}

TEST(the_director_stops_short_of_a_place_that_shows_ground_off_the_map)
{
	// the screen's ground runs 300 to each side, 200 below the point looked at and 500 above it
	Coord2D corners[ 4 ];
	corners[ 0 ].x = -300.0f; corners[ 0 ].y = 500.0f;
	corners[ 1 ].x = 300.0f; corners[ 1 ].y = 500.0f;
	corners[ 2 ].x = -300.0f; corners[ 2 ].y = -200.0f;
	corners[ 3 ].x = 300.0f; corners[ 3 ].y = -200.0f;
	Region2D map;
	map.lo.x = map.lo.y = 0.0f;
	map.hi.x = map.hi.y = 4000.0f;

	Coord2D place;
	place.x = 2000.0f;
	place.y = 2000.0f;
	CHECK_NEAR( ObserverCamera_keepInMap( place, corners, 4, map ).x, 2000.0f, 0.001f );

	// a fight in the map's corner is shown from as near it as the screen allows
	place.x = 50.0f;
	place.y = 3900.0f;
	const Coord2D kept = ObserverCamera_keepInMap( place, corners, 4, map );
	CHECK_NEAR( kept.x, 300.0f, 0.001f );
	CHECK_NEAR( kept.y, 3500.0f, 0.001f );

	// a map narrower than the screen: the place stays between the two edges' limits
	map.hi.x = 400.0f;
	place.x = 50.0f;
	CHECK_NEAR( ObserverCamera_keepInMap( place, corners, 4, map ).x, 100.0f, 0.001f );
}

static ObserverCameraVelocity atRest( void )
{
	ObserverCameraVelocity velocity;
	velocity.x = velocity.y = velocity.z = velocity.angle = velocity.pitch = velocity.zoom = 0.0f;
	return velocity;
}

TEST(the_observer_camera_pans_a_short_way_and_cuts_a_long_one)
{
	ViewLocation from;
	from.init( 1000.0f, 1000.0f, 0.0f, 3.0f, 0.5f, 1.0f );
	// not "near" and "far": windef.h defines both away to nothing
	ViewLocation beside;
	beside.init( 1100.0f, 1000.0f, 0.0f, -3.0f, 0.5f, 1.0f );
	ViewLocation across;
	across.init( 3000.0f, 1000.0f, 0.0f, 3.0f, 0.5f, 1.0f );

	ObserverCameraVelocity velocity = atRest();
	const ViewLocation still = ObserverCamera_approach( from, beside, 0.0f, 0.2f, 900.0f, &velocity );
	CHECK_NEAR( still.getPosition().x, 1000.0f, 0.001f );

	const ViewLocation step = ObserverCamera_approach( from, beside, 0.1f, 0.2f, 900.0f, &velocity );
	CHECK( step.getPosition().x > 1000.0f && step.getPosition().x < 1100.0f );
	// 3 to -3 is the short way through the back, not six radians round the front
	CHECK( fabs( step.getAngle() ) > 3.0f );

	velocity = atRest();
	const ViewLocation cut = ObserverCamera_approach( from, across, 0.01f, 0.2f, 900.0f, &velocity );
	CHECK_NEAR( cut.getPosition().x, 3000.0f, 0.001f );
	CHECK_NEAR( velocity.x, 0.0f, 0.001f );
}

TEST(the_observer_camera_eases_in_holds_its_top_speed_and_settles_without_overshoot)
{
	ViewLocation from;
	from.init( 1000.0f, 1000.0f, 0.0f, 0.0f, 0.5f, 1.0f );
	ViewLocation to;
	to.init( 2200.0f, 1000.0f, 0.0f, 0.0f, 0.5f, 1.0f );

	const Real tick = 1.0f / 30.0f;
	const Real topSpeed = 900.0f;
	ObserverCameraVelocity velocity = atRest();
	ViewLocation at = from;
	Real lastStep = 0.0f;
	Real firstStep = 0.0f;
	Bool overshot = FALSE;
	Bool tooFast = FALSE;
	for( Int each = 0; each < 30 * 10; each++ )
	{
		const ViewLocation next = ObserverCamera_approach( at, to, tick, 1.4f, topSpeed, &velocity );
		const Real moved = next.getPosition().x - at.getPosition().x;
		if( each == 0 )
			firstStep = moved;
		tooFast = tooFast || moved > topSpeed * tick;
		overshot = overshot || next.getPosition().x > 2200.0f + 0.01f;
		lastStep = moved;
		at = next;
	}
	// from rest the first step is a small fraction of the speed it reaches
	CHECK( firstStep < 0.25f * topSpeed * tick );
	CHECK( !tooFast );
	CHECK( !overshot );
	CHECK_NEAR( at.getPosition().x, 2200.0f, 1.0f );
	CHECK( lastStep < 1.0f );
}

TEST(the_recording_splits_for_a_second_fight_far_from_the_first_and_never_for_the_same_one)
{
	Coord2D first;
	first.x = first.y = 1000.0f;
	std::vector< DirectorHeat > hits;
	for( Int each = 0; each < 6; each++ )
		hits.push_back( directorHit( 1000.0f + each * 20.0f, 1000.0f ) );
	// a hit 500 away is near enough that the two halves would show the same ground
	hits.push_back( directorHit( 1500.0f, 1000.0f ) );

	Coord2D second;
	Real heat = 0.0f;
	const Real needed = 660.0f;
	CHECK( !ObserverCamera_secondPlace( hits, first, needed, &second, &heat ) );

	for( Int each = 0; each < 3; each++ )
		hits.push_back( directorHit( 3000.0f, 3000.0f + each * 20.0f ) );
	CHECK( ObserverCamera_secondPlace( hits, first, needed, &second, &heat ) );
	CHECK_NEAR( heat, 3.0f, 0.001f );
	CHECK_NEAR( second.x, 3000.0f, 0.001f );
	CHECK_NEAR( second.y, 3020.0f, 0.001f );
	// a picture that spans more ground wants the second fight further off
	CHECK( !ObserverCamera_secondPlace( hits, first, 3000.0f, &second, &heat ) );
}

TEST(a_big_moment_wants_fights_the_scouting_pass_saw_peak_hot)
{
	std::vector< DirectorMoment > timeline;
	DirectorMoment fight;
	fight.start = 1000;
	fight.last = 2000;
	fight.place.x = fight.target.x = 1000.0f;
	fight.place.y = fight.target.y = 1000.0f;
	fight.peak = 8.0f;
	fight.sides = 3;
	fight.power = FALSE;
	fight.superweapon = FALSE;
	timeline.push_back( fight );
	const Coord2D place = { 1000.0f, 1000.0f };
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, 1500, NULL ) );
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, 1500, NULL, 12.0f ) );
	timeline[ 0 ].peak = 12.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, 1500, NULL, 12.0f ) );
}

TEST(the_recording_splits_for_a_big_second_fight_holds_it_and_rests_after)
{
	const UnsignedInt rested = 5 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt longHeld = 8 * LOGICFRAMES_PER_SECOND;
	const Real farApart = 2000.0f;
	const Real needed = 660.0f;
	// on for a second fight hot on its own and at least half the first
	CHECK( ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, needed, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 8.0f, 3.0f, farApart, needed, rested ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 1.0f, 1.5f, farApart, needed, rested ) );
	// not straight back on after it went off
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 3.0f, farApart, needed, 2 * LOGICFRAMES_PER_SECOND ) );
	// held a few seconds while the second fight goes on at all, then only while it keeps a quarter
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.5f, farApart, needed, justJoined ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 8.0f, 2.0f, farApart, needed, longHeld ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 8.0f, 1.9f, farApart, needed, longHeld ) );
}

TEST(the_recording_split_ends_at_once_when_the_two_halves_show_one_fight)
{
	const UnsignedInt justJoined = 2 * LOGICFRAMES_PER_SECOND;
	const Real needed = 660.0f;
	// the director moved onto the second fight: both halves would show the same ground
	CHECK( !ObserverCamera_holdSplit( TRUE, 12.0f, 8.0f, 60.0f, needed, justJoined ) );
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 600.0f, needed, 5 * LOGICFRAMES_PER_SECOND ) );
}

TEST(a_split_once_on_stays_until_its_panes_are_in_and_held_six_seconds)
{
	const Real needed = 660.0f;
	// in through the radar's slide, the panes' and the lines' draw, and held six seconds, whatever the
	// second fight does: one that went off on the next scan slid a pane in and straight back out
	const UnsignedInt least = ConvertRetailFramesWall( 12U + 15U + 21U ) + 6 * LOGICFRAMES_PER_SECOND;	// the three in 30 Hz frames
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, 15 ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, least - 1 ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 6.0f, 0.0f, 2000.0f, needed, least ) );
}

TEST(a_split_wants_its_fights_further_apart_the_more_ground_a_pane_shows)
{
	const UnsignedInt rested = 5 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt longHeld = 8 * LOGICFRAMES_PER_SECOND;
	// panes 1650 across: a split opens past 1650, and a held one stays past 1100
	const Real needed = 1650.0f;
	CHECK( !ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 1600.0f, needed, rested ) );
	CHECK( ObserverCamera_holdSplit( FALSE, 6.0f, 6.0f, 1700.0f, needed, rested ) );
	CHECK( ObserverCamera_holdSplit( TRUE, 6.0f, 6.0f, 1200.0f, needed, longHeld ) );
	CHECK( !ObserverCamera_holdSplit( TRUE, 6.0f, 6.0f, 1050.0f, needed, 15 ) );
}

TEST(the_recording_splits_only_beside_a_real_fight)
{
	// bases going up are no fight: the split's hits are only one side's on another, so the director
	// looking at a sight has no heat of its own and a second fight does not split the picture
	CHECK( !ObserverCamera_holdSplit( FALSE, 0.0f, 10.0f, 2000.0f, 660.0f, 5 * LOGICFRAMES_PER_SECOND ) );
	// and with no fight anywhere there is no second place to show
	std::vector< DirectorHeat > none;
	Coord2D first;
	first.x = first.y = 1000.0f;
	Coord2D second;
	Real heat = 0.0f;
	CHECK( !ObserverCamera_secondPlace( none, first, 660.0f, &second, &heat ) );
}

TEST(a_place_whose_top_leaves_the_map_is_moved_in_by_the_top_alone)
{
	// the ground under a tilted picture's corners: the top reaches three times as far as the bottom, so
	// a look point 400 from the far edge shows 200 past it at the top while its middle is on the map
	const Coord2D corners[ 4 ] = { { -900.0f, 600.0f }, { 900.0f, 600.0f }, { 300.0f, -200.0f }, { -300.0f, -200.0f } };
	Region2D map;
	map.lo.x = map.lo.y = 0.0f;
	map.hi.x = map.hi.y = 3000.0f;
	const Coord2D nearTop = { 1500.0f, 2600.0f };
	const Coord2D kept = ObserverCamera_keepInMap( nearTop, corners, 4, map );
	CHECK_NEAR( kept.x, 1500.0f, 0.001f );
	CHECK_NEAR( kept.y, 2400.0f, 0.001f );
	const Coord2D middle = { 1500.0f, 1500.0f };
	const Coord2D free = ObserverCamera_keepInMap( middle, corners, 4, map );
	CHECK_NEAR( free.y, 1500.0f, 0.001f );
}

TEST(the_score_bar_frame_grows_with_the_picture)
{
	CHECK_EQ( ObserverCamera_paneLineWidth( 720 ), 6 );
	CHECK_EQ( ObserverCamera_paneLineWidth( 1080 ), 9 );
	CHECK_EQ( ObserverCamera_paneLineWidth( 2160 ), 18 );
	// the band is five golds wide
	CHECK_EQ( ObserverCamera_paneBandWidth( 720 ), 30 );
}

static DirectorFightHit fightHit( Real x, Real y, UnsignedInt sides )
{
	DirectorFightHit hit;
	hit.position.x = x;
	hit.position.y = y;
	hit.weight = 1.0f;
	hit.sides = sides;
	return hit;
}

static DirectorMoment scoutedFight( UnsignedInt start, UnsignedInt last, Real x, Real y, Real peak )
{
	DirectorMoment fight;
	fight.start = start;
	fight.last = last;
	fight.place.x = fight.target.x = x;
	fight.place.y = fight.target.y = y;
	fight.peak = peak;
	fight.sides = 3;
	fight.power = FALSE;
	fight.superweapon = FALSE;
	return fight;
}

static DirectorMoment scoutedPower( UnsignedInt frame, Real x, Real y, Bool superweapon )
{
	DirectorMoment power = scoutedFight( frame, frame, x, y, superweapon ? 5.0f : 1.0f );
	power.target.x = x + 1500.0f;
	power.power = TRUE;
	power.superweapon = superweapon;
	return power;
}

TEST(the_scout_follows_a_fight_from_its_first_scan_to_its_last_and_keeps_two_apart)
{
	std::vector< DirectorMoment > fights;
	std::vector< DirectorFightHit > hits;
	for( Int each = 0; each < 3; each++ )
		hits.push_back( fightHit( 1000.0f + each * 10.0f, 1000.0f, 1 | 2 ) );
	hits.push_back( fightHit( 4000.0f, 4000.0f, 4 | 8 ) );
	hits.push_back( fightHit( 4010.0f, 4000.0f, 4 | 8 ) );
	ObserverCamera_trackFights( fights, hits, 15 );
	CHECK_EQ( (Int)fights.size(), 2 );
	CHECK_EQ( fights[ 0 ].start, 15u );
	CHECK_NEAR( fights[ 0 ].peak, 3.0f, 0.001f );
	CHECK_EQ( fights[ 0 ].sides, 3u );
	CHECK_EQ( fights[ 1 ].sides, 12u );

	// the first fight moves 200 on and a third player joins it: the same fight, seen later
	hits.clear();
	for( Int each = 0; each < 4; each++ )
		hits.push_back( fightHit( 1200.0f + each * 10.0f, 1000.0f, 1 | 4 ) );
	ObserverCamera_trackFights( fights, hits, 30 );
	CHECK_EQ( (Int)fights.size(), 2 );
	CHECK_EQ( fights[ 0 ].start, 15u );
	CHECK_EQ( fights[ 0 ].last, 30u );
	CHECK_NEAR( fights[ 0 ].place.x, 1010.0f, 0.001f );
	CHECK_NEAR( fights[ 0 ].target.x, 1215.0f, 0.001f );
	CHECK_NEAR( fights[ 0 ].peak, 4.0f, 0.001f );
	CHECK_EQ( fights[ 0 ].sides, 7u );
	CHECK_EQ( fights[ 1 ].last, 15u );

	// quiet for longer than the gap, the place starts a new fight
	ObserverCamera_trackFights( fights, hits, 30 + 4 * LOGICFRAMES_PER_SECOND );
	CHECK_EQ( (Int)fights.size(), 3 );
	CHECK_EQ( fights[ 2 ].start, 30u + 4 * LOGICFRAMES_PER_SECOND );
}

TEST(the_scout_counts_a_fight_worth_filming_only_when_it_got_hot_and_lasted)
{
	CHECK( ObserverCamera_worthFilming( scoutedFight( 100, 100 + 8 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 4.0f ) ) );
	CHECK( !ObserverCamera_worthFilming( scoutedFight( 100, 100 + 8 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 3.9f ) ) );
	CHECK( !ObserverCamera_worthFilming( scoutedFight( 100, 100 + 3 * LOGICFRAMES_PER_SECOND, 0.0f, 0.0f, 20.0f ) ) );
	CHECK( ObserverCamera_worthFilming( scoutedPower( 100, 0.0f, 0.0f, FALSE ) ) );
}

TEST(a_special_power_is_worth_filming_only_once_it_hurts_somebody_where_it_was_aimed)
{
	// a spy satellite and a carpet bomb fired the same frame; only the bomb's target is hit
	std::vector< DirectorMoment > moments;
	moments.push_back( scoutedPower( 1800, 500.0f, 500.0f, FALSE ) );
	moments.push_back( scoutedPower( 1800, 500.0f, 3000.0f, FALSE ) );
	moments.push_back( scoutedPower( 1800, 500.0f, 5000.0f, TRUE ) );
	moments[ 0 ].peak = moments[ 1 ].peak = 0.0f;
	// the superweapon is filmed from its launch, hit or not
	CHECK( ObserverCamera_worthFilming( moments[ 2 ] ) );
	CHECK( !ObserverCamera_worthFilming( moments[ 1 ] ) );
	std::vector< DirectorFightHit > hits;
	hits.push_back( fightHit( 2100.0f, 3000.0f, 3 ) );
	ObserverCamera_markLandings( moments, hits, 1950 );
	CHECK( !ObserverCamera_worthFilming( moments[ 0 ] ) );
	CHECK( ObserverCamera_worthFilming( moments[ 1 ] ) );
	CHECK_EQ( moments[ 1 ].last, 1950u );
	// a hit more than thirty seconds after the launch is somebody else's
	hits.push_back( fightHit( 2000.0f, 500.0f, 3 ) );
	ObserverCamera_markLandings( moments, hits, 1800 + 31 * LOGICFRAMES_PER_SECOND );
	CHECK( !ObserverCamera_worthFilming( moments[ 0 ] ) );
	// but a strike flown in from the map's edge lands twenty seconds on, on a building or anything else;
	// waiting twelve seconds missed it
	ObserverCamera_markLandings( moments, hits, 1800 + 20 * LOGICFRAMES_PER_SECOND );
	CHECK( ObserverCamera_worthFilming( moments[ 0 ] ) );
	CHECK_EQ( moments[ 0 ].last, 1800u + 20 * LOGICFRAMES_PER_SECOND );
	// and its first hit is its landing, which a later one does not move
	ObserverCamera_markLandings( moments, hits, 1800 + 25 * LOGICFRAMES_PER_SECOND );
	CHECK_EQ( moments[ 0 ].last, 1800u + 20 * LOGICFRAMES_PER_SECOND );

	// the filming pass finds the power by its frame and its target, written without decimals, and
	// learns when it lands; a superweapon from its launch
	Coord2D at;
	at.x = 2000.4f;
	at.y = 3000.3f;
	CHECK_EQ( ObserverCamera_powerLandsAt( moments, 1800, at ), 1950u );
	at.y = 500.0f;
	CHECK_EQ( ObserverCamera_powerLandsAt( moments, 1800, at ), 1800u + 20 * LOGICFRAMES_PER_SECOND );
	at.y = 3000.0f;
	CHECK_EQ( ObserverCamera_powerLandsAt( moments, 1801, at ), 0u );
	at.y = 5000.0f;
	CHECK_EQ( ObserverCamera_powerLandsAt( moments, 1800, at ), 1800u );
	moments[ 0 ].peak = 0.0f;
	at.y = 500.0f;
	CHECK_EQ( ObserverCamera_powerLandsAt( moments, 1800, at ), 0u );
}

TEST(a_hot_fight_the_end_of_the_scouting_run_cut_short_is_still_worth_filming)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 5865, 6000, 1218.0f, 1689.0f, 15.2f ) );
	timeline.push_back( scoutedFight( 5000, 5100, 3000.0f, 3000.0f, 9.0f ) );
	timeline.push_back( scoutedFight( 5950, 5940 + 2 * LOGICFRAMES_PER_SECOND, 4000.0f, 4000.0f, 2.0f ) );
	timeline.push_back( scoutedPower( 5990, 500.0f, 500.0f, FALSE ) );
	CHECK( !ObserverCamera_worthFilming( timeline[ 0 ] ) );
	ObserverCamera_closeTimeline( timeline, 6000 );
	CHECK( ObserverCamera_worthFilming( timeline[ 0 ] ) );
	// one that was over before the end keeps its own end
	CHECK_EQ( timeline[ 1 ].last, 5100u );
	CHECK( !ObserverCamera_worthFilming( timeline[ 1 ] ) );
	// still small at the end is still a fizzle
	CHECK( !ObserverCamera_worthFilming( timeline[ 2 ] ) );
	CHECK_EQ( timeline[ 3 ].last, 5990u );
}

TEST(the_director_goes_to_wait_for_a_fight_four_seconds_before_it_starts_superweapons_first)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1600, 1000.0f, 1000.0f, 6.0f ) );
	timeline.push_back( scoutedFight( 1050, 1600, 3000.0f, 3000.0f, 9.0f ) );
	timeline.push_back( scoutedFight( 1010, 1030, 5000.0f, 5000.0f, 2.0f ) );
	timeline.push_back( scoutedPower( 1020, 6000.0f, 1000.0f, FALSE ) );
	timeline.push_back( scoutedPower( 1100, 6000.0f, 3000.0f, TRUE ) );
	// a power the scout never saw land, a scan, is not waited for
	timeline[ 3 ].peak = 0.0f;
	const UnsignedInt preroll = 4 * LOGICFRAMES_PER_SECOND;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1000 - preroll - 1, NULL, 660.0f ), -1 );
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1000 - preroll, NULL, 660.0f ), 0 );
	// two coming up: the hotter
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, NULL, 660.0f ), 1 );
	// a superweapon over any fight
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1100 - preroll, NULL, 660.0f ), 4 );
	// one that has started is no longer waited for
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1100, NULL, 660.0f ), -1 );
	// a power that landed is waited for ahead of any fight, a superweapon still ahead of it
	timeline[ 3 ].peak = 1.0f;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, NULL, 660.0f ), 3 );
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1100 - preroll, NULL, 660.0f ), 4 );
	// and is passed over where another pane already shows its target, not where it was fired from
	Coord2D taken = { 7500.0f, 1000.0f };
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, &taken, 660.0f ), 1 );
	// a strike that lands twenty seconds after its launch is waited for before it lands, not before
	// it is fired
	timeline[ 3 ].last = 1020 + 20 * LOGICFRAMES_PER_SECOND;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1020 - preroll, NULL, 660.0f ), 0 );
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, timeline[ 3 ].last - preroll, NULL, 660.0f ), 3 );
}

TEST(the_director_does_not_wait_for_the_fight_the_other_pane_already_shows)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1600, 1000.0f, 1000.0f, 6.0f ) );
	timeline.push_back( scoutedFight( 1050, 1600, 3000.0f, 3000.0f, 9.0f ) );
	const UnsignedInt preroll = 4 * LOGICFRAMES_PER_SECOND;
	// pane 1 waits at the hotter one: pane 0 takes the other
	Coord2D taken;
	taken.x = 3100.0f;
	taken.y = 3000.0f;
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1050 - preroll, &taken, 660.0f ), 0 );
	// and with nothing else coming up, none
	CHECK_EQ( ObserverCamera_prerollMoment( timeline, 1001, &taken, 660.0f ), -1 );
}

TEST(the_director_skips_only_a_fight_the_scout_saw_fizzle)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 500, 560, 2000.0f, 2000.0f, 2.0f ) );
	timeline.push_back( scoutedFight( 500, 500 + 8 * LOGICFRAMES_PER_SECOND, 5000.0f, 5000.0f, 8.0f ) );
	Coord2D place;
	place.x = 2050.0f;
	place.y = 2000.0f;
	CHECK( ObserverCamera_fizzles( timeline, place, 520 ) );
	// over and past its gap, whatever is there now the scout did not see
	CHECK( !ObserverCamera_fizzles( timeline, place, 560 + 4 * LOGICFRAMES_PER_SECOND ) );
	place.x = place.y = 5000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 600 ) );
	place.x = place.y = 8000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 520 ) );
	// a fight worth filming in the same place beats the fizzle beside it
	timeline.push_back( scoutedFight( 500, 500 + 8 * LOGICFRAMES_PER_SECOND, 2100.0f, 2000.0f, 8.0f ) );
	place.x = 2050.0f;
	place.y = 2000.0f;
	CHECK( !ObserverCamera_fizzles( timeline, place, 520 ) );
}

TEST(the_recording_splits_before_a_known_second_fight_far_from_the_first)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 1000, 1500, 4000.0f, 4000.0f, 8.0f ) );
	timeline.push_back( scoutedFight( 900, 1500, 1200.0f, 1000.0f, 20.0f ) );
	timeline.push_back( scoutedFight( 900, 1500, 6000.0f, 6000.0f, 2.0f ) );
	Coord2D first;
	first.x = first.y = 1000.0f;
	// three seconds ahead: at four pane 1 sat on an empty bridge
	const UnsignedInt lead = 3 * LOGICFRAMES_PER_SECOND;
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead - 1, 660.0f, FALSE ), -1 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead, 660.0f, FALSE ), 0 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1500, 660.0f, TRUE ), 0 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1501, 660.0f, TRUE ), -1 );
	// the fight 4243 away is too near once the picture is so wide a split wants 4300
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 1000 - lead, 4300.0f, FALSE ), -1 );
}

TEST(a_split_is_planned_only_beside_a_fight_that_outlasts_its_least_hold)
{
	std::vector< DirectorMoment > timeline;
	// ten seconds, past the eight a fight has to last to be worth filming
	const UnsignedInt end = 1000 + 10 * LOGICFRAMES_PER_SECOND;
	timeline.push_back( scoutedFight( 1000, end, 1000.0f, 1000.0f, 8.0f ) );
	timeline[ 0 ].target.x = 1400.0f;
	timeline.push_back( scoutedFight( 1000, end, 3000.0f, 3000.0f, 2.0f ) );
	Coord2D place;
	place.x = 1050.0f;
	place.y = 1000.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, end, NULL ) );
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, end + 1, NULL ) );
	// near where it ended up as well as where it began
	place.x = 1600.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, end, NULL ) );
	// but not for a second pane while the first shows where it began: that is the first pane's fight
	Coord2D first;
	first.x = first.y = 1000.0f;
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, end, &first ) );
	first.x = first.y = 3000.0f;
	CHECK( ObserverCamera_fightLasts( timeline, place, 1100, end, &first ) );
	// one about to begin counts; a fizzle never does
	CHECK( ObserverCamera_fightLasts( timeline, place, 1000 - 4 * LOGICFRAMES_PER_SECOND, end, NULL ) );
	place.x = place.y = 3000.0f;
	CHECK( !ObserverCamera_fightLasts( timeline, place, 1100, 1200, NULL ) );
}

TEST(the_recording_splits_ahead_for_a_fight_still_to_begin_and_not_anew_for_one_under_way)
{
	std::vector< DirectorMoment > timeline;
	timeline.push_back( scoutedFight( 5865, 6870, 1218.0f, 1689.0f, 15.2f ) );
	Coord2D first;
	first.x = 2346.0f;
	first.y = 1668.0f;
	// a split already up keeps the fight while it lasts
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 6451, 660.0f, TRUE ), 0 );
	// none up: where it began twenty seconds ago is no place to send a pane
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 6451, 660.0f, FALSE ), -1 );
	CHECK_EQ( ObserverCamera_plannedSecond( timeline, first, 5864, 660.0f, FALSE ), 0 );
}

TEST(a_timeline_line_reads_back_as_the_moment_it_was_written_from)
{
	DirectorMoment fight = scoutedFight( 450, 1230, 1234.0f, 567.0f, 7.25f );
	fight.target.x = 1400.0f;
	fight.sides = 0x30;
	DirectorMoment read;
	CHECK( ObserverCamera_parseMoment( ObserverCamera_formatMoment( fight ).str(), &read ) );
	CHECK_EQ( read.start, 450u );
	CHECK_EQ( read.last, 1230u );
	CHECK_NEAR( read.place.x, 1234.0f, 0.001f );
	CHECK_NEAR( read.place.y, 567.0f, 0.001f );
	CHECK_NEAR( read.target.x, 1400.0f, 0.001f );
	CHECK_NEAR( read.peak, 7.25f, 0.001f );
	CHECK_EQ( read.sides, 0x30u );
	CHECK( !read.power );
	CHECK( ObserverCamera_parseMoment( ObserverCamera_formatMoment( scoutedPower( 90, 10.0f, 20.0f, TRUE ) ).str(), &read ) );
	CHECK( read.power );
	CHECK( read.superweapon );
	CHECK( !ObserverCamera_parseMoment( "crc 900 305419896", &read ) );
	CHECK( !ObserverCamera_parseMoment( "", &read ) );
}

TEST(pane_animations_ease_over_logic_frames)
{
	CHECK_NEAR( ObserverCamera_easeFrames( 100, 100, 15 ), 0.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 90, 100, 15 ), 0.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 110, 100, 20 ), 0.5f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 115, 100, 15 ), 1.0f, 0.0001f );
	CHECK_NEAR( ObserverCamera_easeFrames( 400, 100, 15 ), 1.0f, 0.0001f );
	Real last = 0.0f;
	for( UnsignedInt frame = 100; frame <= 115; frame++ )
	{
		const Real now = ObserverCamera_easeFrames( frame, 100, 15 );
		CHECK( now >= last );
		last = now;
	}
}

TEST(the_directors_height_eases_to_a_new_place_and_cuts_with_a_cut)
{
	// a place 300 higher: no frame at 30 a second moves more than a twentieth of it, it gets there
	// within seven seconds, and never passes it
	Real height = 0.0f;
	Real velocity = 0.0f;
	Real biggestStep = 0.0f;
	for( Int frame = 0; frame < 210; frame++ )
	{
		const Real next = ObserverCamera_easeHeight( height, 300.0f, &velocity, 1.0f / 30.0f, FALSE );
		biggestStep = max( biggestStep, next - height );
		CHECK( next <= 300.0f );
		height = next;
	}
	CHECK( biggestStep < 15.0f );
	CHECK( height > 290.0f );
	// the first frame of a change barely moves: the view's own settle took a tenth of the gap at once
	velocity = 0.0f;
	CHECK( ObserverCamera_easeHeight( 0.0f, 300.0f, &velocity, 1.0f / 30.0f, FALSE ) < 3.0f );
	// a cut is there at once and starts the next glide from rest
	velocity = 50.0f;
	CHECK_NEAR( ObserverCamera_easeHeight( 0.0f, 300.0f, &velocity, 1.0f / 30.0f, TRUE ), 300.0f, 0.0001f );
	CHECK_NEAR( velocity, 0.0f, 0.0001f );
}

TEST(the_score_bar_takes_two_rows_from_five_players_with_blocks_kept_whole)
{
	std::vector< Int > sizes( 4, 1 );
	std::vector< Int > rows = ObserverCamera_cardRows( sizes );
	for( size_t block = 0; block < rows.size(); block++ )
		CHECK_EQ( rows[ block ], 0 );
	// eight alone: four and four
	sizes.assign( 8, 1 );
	rows = ObserverCamera_cardRows( sizes );
	for( size_t block = 0; block < rows.size(); block++ )
		CHECK_EQ( rows[ block ], block < 4 ? 0 : 1 );
	// 4v4 and 4v1: a team to a row
	sizes.assign( 2, 4 );
	rows = ObserverCamera_cardRows( sizes );
	CHECK_EQ( rows[ 0 ], 0 );
	CHECK_EQ( rows[ 1 ], 1 );
	sizes[ 1 ] = 1;
	rows = ObserverCamera_cardRows( sizes );
	CHECK_EQ( rows[ 1 ], 1 );
	// 1v1v1v1v2: three cards up, the last single and the pair down
	sizes.assign( 4, 1 );
	sizes.push_back( 2 );
	rows = ObserverCamera_cardRows( sizes );
	CHECK_EQ( rows[ 2 ], 0 );
	CHECK_EQ( rows[ 3 ], 1 );
	CHECK_EQ( rows[ 4 ], 1 );
	// 2v2v2: four up, two down
	sizes.assign( 3, 2 );
	rows = ObserverCamera_cardRows( sizes );
	CHECK_EQ( rows[ 1 ], 0 );
	CHECK_EQ( rows[ 2 ], 1 );
}

TEST(the_score_bar_sets_every_side_in_one_row_and_fits_eight_players)
{
	// 4v1: four cards with three gaps, a "vs", one card
	std::vector< Int > sizes;
	sizes.push_back( 4 );
	sizes.push_back( 1 );
	std::vector< Int > cardLefts, blockLefts;
	CHECK_EQ( ObserverCamera_cardRow( sizes, 100, 4, 30, &cardLefts, &blockLefts ), 5 * 100 + 3 * 4 + 30 );
	CHECK_EQ( (Int)cardLefts.size(), 5 );
	CHECK_EQ( cardLefts[ 3 ], 3 * 104 );
	CHECK_EQ( blockLefts[ 1 ], 4 * 100 + 3 * 4 + 30 );
	CHECK_EQ( cardLefts[ 4 ], blockLefts[ 1 ] );
	// two alone, three alone and a pair: the 1v1v1v1v2 staging
	sizes.assign( 4, 1 );
	sizes.push_back( 2 );
	CHECK_EQ( ObserverCamera_cardRow( sizes, 100, 4, 30, &cardLefts, &blockLefts ), 6 * 100 + 4 + 4 * 30 );
	CHECK_EQ( blockLefts[ 4 ], 4 * 130 );

	// the sizes a 720 picture measured, largest first: every count from two to eight alone and 4v4
	// fit 1280 less the bar's padding at some step, and no step is taken that is larger than needed
	const Int cardWidths[] = { 170, 150, 132, 116 };
	const Int cardGaps[] = { 6, 5, 4, 3 };
	const Int versusWidths[] = { 30, 26, 22, 20 };
	const Int room = 1280 - 2 * 8;
	for( Int players = 2; players <= 8; players++ )
	{
		sizes.assign( players, 1 );
		const Int step = ObserverCamera_cardStep( sizes, cardWidths, cardGaps, versusWidths, 4, room );
		CHECK( ObserverCamera_cardRow( sizes, cardWidths[ step ], cardGaps[ step ], versusWidths[ step ], &cardLefts, &blockLefts ) <= room );
		if( step > 0 )
			CHECK( ObserverCamera_cardRow( sizes, cardWidths[ step - 1 ], cardGaps[ step - 1 ], versusWidths[ step - 1 ], &cardLefts, &blockLefts ) > room );
	}
	sizes.assign( 2, 4 );
	const Int step = ObserverCamera_cardStep( sizes, cardWidths, cardGaps, versusWidths, 4, room );
	// 4v4 has one "vs" where eight alone have seven, so it keeps a larger size: 1200 + 30 + 26
	CHECK_EQ( step, 1 );
	// a row too wide at every step gets the widest card that fits
	const Int narrow = ObserverCamera_cardWidthIn( sizes, cardGaps[ 3 ], versusWidths[ 3 ], 900 );
	CHECK( ObserverCamera_cardRow( sizes, narrow, cardGaps[ 3 ], versusWidths[ 3 ], &cardLefts, &blockLefts ) <= 900 );
	CHECK( ObserverCamera_cardRow( sizes, narrow + 1, cardGaps[ 3 ], versusWidths[ 3 ], &cardLefts, &blockLefts ) > 900 );

	// a name of eight characters ten pixels each, the ellipsis six: whole while it fits, else the most
	// characters that leave room for the ellipsis
	std::vector< Int > prefixWidths;
	for( Int count = 0; count <= 8; count++ )
		prefixWidths.push_back( 10 * count );
	CHECK_EQ( ObserverCamera_fitCount( prefixWidths, 6, 80 ), 8 );
	CHECK_EQ( ObserverCamera_fitCount( prefixWidths, 6, 79 ), 7 );
	CHECK_EQ( ObserverCamera_fitCount( prefixWidths, 6, 75 ), 6 );
	CHECK_EQ( ObserverCamera_fitCount( prefixWidths, 6, 16 ), 1 );
	CHECK_EQ( ObserverCamera_fitCount( prefixWidths, 6, 15 ), 0 );
}

TEST(the_army_bar_cuts_its_whole_width_among_the_sides)
{
	std::vector< Int > armies;
	armies.push_back( 1 );
	armies.push_back( 1 );
	armies.push_back( 1 );
	const std::vector< Int > pixels = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( pixels[ 0 ] + pixels[ 1 ] + pixels[ 2 ], 100 );
	CHECK( pixels[ 0 ] >= 33 && pixels[ 0 ] <= 34 );
	armies[ 0 ] = 3000;
	armies[ 1 ] = 1000;
	armies[ 2 ] = 0;
	const std::vector< Int > uneven = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( uneven[ 0 ], 75 );
	CHECK_EQ( uneven[ 1 ], 25 );
	CHECK_EQ( uneven[ 2 ], 0 );
	armies.assign( 3, 0 );
	const std::vector< Int > none = ObserverCamera_barShares( armies, 100 );
	CHECK_EQ( none[ 0 ] + none[ 1 ] + none[ 2 ], 0 );
}

TEST(a_dark_player_colour_is_lifted_to_read_and_a_gold_one_is_told_from_the_lines)
{
	// purple, #9600c8, is lifted; white stays white
	const Color purple = GameMakeColor( 0x96, 0x00, 0xc8, 255 );
	const Color lifted = ObserverCamera_readableColor( purple );
	CHECK_NE( lifted, purple );
	UnsignedByte red, green, blue, alpha;
	GameGetColorComponents( lifted, &red, &green, &blue, &alpha );
	CHECK( red > 0x96 && green > 0x00 && blue > 0xc8 );
	const Color white = GameMakeColor( 255, 255, 255, 255 );
	CHECK_NE( lifted, white );
	CHECK_EQ( ObserverCamera_readableColor( white ), white );
	// gold, #dde20d, is near the lines' gold; blue and red are not
	CHECK( ObserverCamera_nearBrandGold( GameMakeColor( 0xdd, 0xe2, 0x0d, 255 ) ) );
	CHECK( !ObserverCamera_nearBrandGold( GameMakeColor( 0x00, 0x00, 0xff, 255 ) ) );
	CHECK( !ObserverCamera_nearBrandGold( GameMakeColor( 0xff, 0x00, 0x00, 255 ) ) );
}

TEST(power_flags_hang_one_at_a_time_and_hold_less_with_another_waiting)
{
	const DirectorShowing waiting = { 3, NULL, NULL, FALSE, 0, 0 };
	std::vector< DirectorShowing > flags( 2, waiting );
	// the first drops on the frame it is queued; with one behind it, it holds half of 120 after its 15
	CHECK( ObserverCamera_advanceShowing( flags, 100, 15, 120, FALSE ) );
	CHECK_EQ( flags[ 0 ].start, 100u );
	CHECK_EQ( flags[ 1 ].start, 0u );
	CHECK_NEAR( ObserverCamera_showingShown( flags[ 0 ], 100, 15 ), 0.0f, 0.0001f );
	for( UnsignedInt frame = 101; frame < 175; frame++ )
		CHECK( !ObserverCamera_advanceShowing( flags, frame, 15, 120, FALSE ) );
	CHECK_NEAR( ObserverCamera_showingShown( flags[ 0 ], 174, 15 ), 1.0f, 0.0001f );
	CHECK( !ObserverCamera_advanceShowing( flags, 175, 15, 120, FALSE ) );
	CHECK_EQ( flags[ 0 ].leaving, 175u );
	CHECK( ObserverCamera_showingShown( flags[ 0 ], 182, 15 ) > 0.0f );
	CHECK( ObserverCamera_showingShown( flags[ 0 ], 182, 15 ) < 1.0f );
	// gone 15 frames later, and the next drops on that frame and, alone, holds the whole 120
	for( UnsignedInt frame = 176; frame < 190; frame++ )
		CHECK( !ObserverCamera_advanceShowing( flags, frame, 15, 120, FALSE ) );
	CHECK( ObserverCamera_advanceShowing( flags, 190, 15, 120, FALSE ) );
	CHECK_EQ( (Int)flags.size(), 1 );
	CHECK_EQ( flags[ 0 ].start, 190u );
	for( UnsignedInt frame = 191; frame <= 325; frame++ )
		ObserverCamera_advanceShowing( flags, frame, 15, 120, FALSE );
	CHECK_EQ( flags[ 0 ].leaving, 325u );
	for( UnsignedInt frame = 326; frame <= 340; frame++ )
		ObserverCamera_advanceShowing( flags, frame, 15, 120, FALSE );
	CHECK( flags.empty() );
	CHECK( !ObserverCamera_advanceShowing( flags, 341, 15, 120, FALSE ) );
}

TEST(the_last_defeat_banner_of_a_decided_match_stays_up)
{
	const DirectorShowing waiting = { 1, NULL, NULL, FALSE, 0, 0 };
	std::vector< DirectorShowing > banners( 2, waiting );
	for( UnsignedInt frame = 100; frame < 1000; frame++ )
		ObserverCamera_advanceShowing( banners, frame, 24, 90, TRUE );
	// the first opened over 24, held 45 with the last behind it and closed over 24; the last never leaves
	CHECK_EQ( (Int)banners.size(), 1 );
	CHECK_EQ( banners[ 0 ].start, 193u );
	CHECK_EQ( banners[ 0 ].leaving, 0u );
	CHECK_NEAR( ObserverCamera_showingShown( banners[ 0 ], 999, 24 ), 1.0f, 0.0001f );
}

TEST(a_defeated_card_flashes_is_struck_and_closes_one_card_at_a_time)
{
	// the card's times are 30 Hz frame counts: flash 12, strike from 6 over 12, struck 36, collapse 18
	const UnsignedInt K = LOGICFRAMES_PER_RETAIL_FRAME;
	Real flash = 0.0f, struck = 0.0f, collapse = 0.0f;
	const UnsignedInt first = ObserverCamera_collapseFrom( 100, 0 );
	CHECK_EQ( first, 100 + 36 * K );
	ObserverCamera_cardExit( 100, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( flash, 0.0f, 0.0001f );
	ObserverCamera_cardExit( 100 + 4 * K, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( flash, 1.0f, 0.0001f );
	CHECK_NEAR( struck, 0.0f, 0.0001f );
	ObserverCamera_cardExit( 100 + 12 * K, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( flash, 0.0f, 0.0001f );
	CHECK( struck > 0.0f && struck < 1.0f );
	ObserverCamera_cardExit( 100 + 36 * K, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( struck, 1.0f, 0.0001f );
	CHECK_NEAR( collapse, 0.0f, 0.0001f );
	ObserverCamera_cardExit( 100 + 45 * K, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( collapse, 0.5f, 0.0001f );
	ObserverCamera_cardExit( 100 + 54 * K, 100, first, &flash, &struck, &collapse );
	CHECK_NEAR( collapse, 1.0f, 0.0001f );
	// a second player beaten on the same frame closes once the first card has gone, a later one on its own
	const UnsignedInt second = ObserverCamera_collapseFrom( 100, first );
	CHECK_EQ( second, 100 + 54 * K );
	CHECK_EQ( ObserverCamera_collapseFrom( 400, second ), 400 + 36 * K );
}

TEST(a_look_through_a_player_gives_back_the_camera_and_fog_the_watcher_had)
{
	ObserverCamera *camera = new ObserverCamera();
	// the rack's look: the fog stays as set, a second player keeps the first mode, letting go gives it back
	camera->setMode( OBSERVER_CAMERA_DIRECTOR );
	camera->setFog( TRUE );
	camera->lookThrough( 2, FALSE );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_PLAYER );
	CHECK_EQ( camera->getFollowedPlayerIndex(), 2 );
	camera->lookThrough( 3, FALSE );
	CHECK( camera->isLooking() );
	CHECK( camera->stopLooking() );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_DIRECTOR );
	CHECK_EQ( camera->getFollowedPlayerIndex(), (Int)ObserverCamera::NO_PLAYER );
	CHECK( camera->isFogOn() );
	CHECK( !camera->stopLooking() );
	// the observer bar's look turns the fog on, and letting go turns it off again
	camera->setFog( FALSE );
	camera->lookThrough( 1 );
	CHECK( camera->isFogOn() );
	CHECK( camera->stopLooking() );
	CHECK( !camera->isFogOn() );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_DIRECTOR );
	// so does the lever moved off Player, the look's other way out
	camera->lookThrough( 1 );
	camera->setMode( OBSERVER_CAMERA_FREE );
	CHECK( !camera->isLooking() );
	CHECK( !camera->isFogOn() );
	// a rewind's reset and give-back keeps the fog the look took
	camera->lookThrough( 1 );
	const Bool tookFog = camera->lookTookFog();
	camera->reset();
	camera->setMode( OBSERVER_CAMERA_PLAYER );
	camera->followPlayer( 1 );
	camera->setFog( TRUE );
	camera->keepLooking( TRUE, tookFog );
	CHECK( camera->stopLooking() );
	CHECK( !camera->isFogOn() );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_FREE );
	camera->setMode( OBSERVER_CAMERA_DIRECTOR );
	// the lever moved off Player ends the look: the next one remembers the new stop
	camera->lookThrough( 1, FALSE );
	camera->setMode( OBSERVER_CAMERA_FREE );
	CHECK( !camera->isLooking() );
	camera->lookThrough( 1, FALSE );
	CHECK( camera->stopLooking() );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_FREE );
	// a follow with no look lets go of the player and keeps the director
	camera->setMode( OBSERVER_CAMERA_DIRECTOR );
	camera->followPlayer( 4 );
	CHECK( camera->stopLooking() );
	CHECK_EQ( (Int)camera->getMode(), (Int)OBSERVER_CAMERA_DIRECTOR );
	delete camera;
}

static DirectorReplay directorReplay( UnsignedInt landed, Real x, Int player, const char *power )
{
	DirectorReplay replay;
	replay.landed = landed;
	ObserverCamera_replayRange( landed, &replay.first, &replay.last );
	replay.target.x = x;
	replay.target.y = 500.0f;
	replay.player = player;
	replay.power = power;
	replay.splice = 0;
	return replay;
}

TEST(an_instant_replay_is_a_missed_superweapon_or_generals_power_that_landed)
{
	CHECK( ObserverCamera_missedBigMoment( TRUE, FALSE, TRUE, FALSE ) );
	CHECK( ObserverCamera_missedBigMoment( FALSE, TRUE, TRUE, FALSE ) );
	// shown on the picture or in the window, or never landing, or a unit's own ability: nothing to replay
	CHECK( !ObserverCamera_missedBigMoment( TRUE, FALSE, TRUE, TRUE ) );
	CHECK( !ObserverCamera_missedBigMoment( FALSE, TRUE, FALSE, FALSE ) );
	CHECK( !ObserverCamera_missedBigMoment( FALSE, FALSE, TRUE, FALSE ) );
}

TEST(an_instant_replay_runs_round_its_landing_and_goes_in_on_a_calm_frame_after_it)
{
	const UnsignedInt K = LOGICFRAMES_PER_SECOND;
	UnsignedInt first = 0, last = 0;
	ObserverCamera_replayRange( 100 * K, &first, &last );
	CHECK_EQ( first, 98 * K );
	CHECK_EQ( last, 106 * K );
	// a power in the match's first second starts its replay on the first frame
	ObserverCamera_replayRange( K, &first, &last );
	CHECK_EQ( first, 1u );
	CHECK_EQ( last, 7 * K );

	// not inside its own frames, calm or not; calm after them; past the wait whatever goes on
	CHECK( !ObserverCamera_spliceNow( 106 * K, 106 * K, TRUE ) );
	CHECK( ObserverCamera_spliceNow( 106 * K + 1, 106 * K, TRUE ) );
	CHECK( !ObserverCamera_spliceNow( 110 * K, 106 * K, FALSE ) );
	CHECK( ObserverCamera_spliceNow( 116 * K, 106 * K, FALSE ) );

	// the film's pictures from its first recorded frame, -recordfps 60 at 30 being two a frame
	CHECK_EQ( ObserverCamera_filmPicture( 1, 1, 2 ), 0u );
	CHECK_EQ( ObserverCamera_filmPicture( 3001, 1, 2 ), 6000u );
	CHECK_EQ( ObserverCamera_filmPicture( 3001, 1, 1 ), 3000u );
}

TEST(instant_replays_are_played_in_order_and_never_twice_over_one_frame)
{
	const UnsignedInt K = LOGICFRAMES_PER_SECOND;
	std::vector< DirectorReplay > replays;
	replays.push_back( directorReplay( 300 * K, 3000.0f, 2, "SuperweaponNeutronMissile" ) );
	replays.push_back( directorReplay( 100 * K, 1000.0f, 1, "SpecialPowerCarpetBomb" ) );
	// landing 7 s after the carpet bomb: starts where its replay ends and keeps its own landing
	replays.push_back( directorReplay( 107 * K, 2000.0f, 3, "SpecialPowerA10ThunderboltMissileStrike" ) );
	// landing a second after it, the carpet bomb's replay runs past this one's landing: nothing of it is left
	replays.push_back( directorReplay( 101 * K, 2500.0f, 4, "SpecialPowerArtilleryBarrage" ) );
	ObserverCamera_planReplays( replays );
	CHECK_EQ( (Int)replays.size(), 3 );
	CHECK_EQ( replays[ 0 ].landed, 100 * K );
	CHECK_EQ( replays[ 1 ].first, 106 * K + 1 );
	CHECK_EQ( replays[ 1 ].landed, 107 * K );
	CHECK_EQ( replays[ 1 ].last, 113 * K );
	CHECK_EQ( replays[ 2 ].landed, 300 * K );

	// the lead drawn before a replay counts as its own, but never takes frames from the one before
	CHECK_EQ( ObserverCamera_replayAt( replays, 98 * K, 0 ), 0 );
	CHECK_EQ( ObserverCamera_replayAt( replays, 97 * K, 0 ), -1 );
	CHECK_EQ( ObserverCamera_replayAt( replays, 97 * K, 2 * K ), 0 );
	CHECK_EQ( ObserverCamera_replayAt( replays, 106 * K, 2 * K ), 0 );
	CHECK_EQ( ObserverCamera_replayAt( replays, 106 * K + 1, 2 * K ), 1 );
	CHECK_EQ( ObserverCamera_replayAt( replays, 200 * K, 2 * K ), -1 );
}

TEST(a_replays_file_line_reads_back_and_the_clip_list_line_is_pictures_and_frames)
{
	DirectorReplay replay = directorReplay( 3000, 1234.4f, 2, "SuperweaponNeutronMissile" );
	replay.splice = 3500;
	const AsciiString line = ObserverCamera_formatReplay( replay );
	const UnsignedInt first = replay.first;
	const UnsignedInt last = replay.last;
	AsciiString expected;
	expected.format( "replay %u %u 3000 1234 500 2 3500 SuperweaponNeutronMissile", first, last );
	CHECK_STR( line.str(), expected.str() );
	DirectorReplay read;
	CHECK( ObserverCamera_parseReplay( ( std::string( line.str() ) + "\n" ).c_str(), &read ) );
	CHECK_EQ( read.first, first );
	CHECK_EQ( read.last, last );
	CHECK_EQ( read.landed, 3000u );
	CHECK_NEAR( read.target.x, 1234.0f, 0.001f );
	CHECK_NEAR( read.target.y, 500.0f, 0.001f );
	CHECK_EQ( read.player, 2 );
	CHECK_EQ( read.splice, 3500u );
	CHECK_STR( read.power.str(), "SuperweaponNeutronMissile" );
	// a warhead no power sent, a CRC line, and a range running backwards are no replay
	replay.power.clear();
	CHECK( ObserverCamera_parseReplay( ObserverCamera_formatReplay( replay ).str(), &read ) );
	CHECK_STR( read.power.str(), "-" );
	CHECK( !ObserverCamera_parseReplay( "crc 900 12345", &read ) );
	CHECK( !ObserverCamera_parseReplay( "replay 500 400 450 0 0 1 600 X", &read ) );

	replay.power = "SpecialPowerCarpetBomb";
	CHECK_STR( ObserverCamera_formatReplayClip( 1, 480, 480, 6998, replay ).str(),
		( std::string( "clip 1 480 480 6998 " ) + std::to_string( first ) + " " + std::to_string( last ) + " 2 SpecialPowerCarpetBomb" ).c_str() );
}
