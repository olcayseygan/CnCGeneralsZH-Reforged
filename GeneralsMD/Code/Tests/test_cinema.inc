// -cinema's shot list. A shot that is half-read still flies a camera, just not the one written down,
// and footage is judged by eye, so every refused line has to say why and every accepted one has to
// carry exactly the numbers on it.

#include "GameClient/CinemaDirector.h"

TEST(cinema_parses_a_move_and_a_cut)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	CHECK( CinemaDirector_parseLine( "2.5 move 1200 900 4", &shot, &isShot, &reason ) );
	CHECK( isShot );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_MOVE );
	CHECK_NEAR( shot.at, 2.5f, 0.001f );
	CHECK_NEAR( shot.routeX[ 0 ], 1200.0f, 0.001f );
	CHECK_NEAR( shot.routeY[ 0 ], 900.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 4.0f, 0.001f );

	// a cut takes x y and then as much of zoom, angle and pitch as it cares to give
	CHECK( CinemaDirector_parseLine( "0 cut 500 600 0.8 45", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_CUT );
	CHECK_EQ( shot.values, 2 );
	CHECK_NEAR( shot.value[ 0 ], 0.8f, 0.001f );
	CHECK_NEAR( shot.value[ 1 ], 45.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 0.0f, 0.001f );
}

TEST(cinema_parses_a_route_follow_and_switches)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	CHECK( CinemaDirector_parseLine( "10 route 6 100 100 200 150 300 100 # along the river", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_ROUTE );
	CHECK_EQ( shot.routePoints, 3 );
	CHECK_NEAR( shot.routeX[ 2 ], 300.0f, 0.001f );
	CHECK_NEAR( shot.seconds, 6.0f, 0.001f );

	CHECK( CinemaDirector_parseLine( "12 follow AmericaTankCrusader 5", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_FOLLOW );
	CHECK_STR( shot.name.str(), "AmericaTankCrusader" );
	CHECK_NEAR( shot.seconds, 5.0f, 0.001f );

	CHECK( CinemaDirector_parseLine( "0 hud on", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_HUD );
	CHECK( shot.on );

	CHECK( CinemaDirector_parseLine( "4 filter blur", &shot, &isShot, &reason ) );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_FILTER );
	CHECK_STR( shot.name.str(), "blur" );

	CHECK( CinemaDirector_parseLine( "   # only a comment", &shot, &isShot, &reason ) );
	CHECK( !isShot );
}

TEST(cinema_refuses_what_it_cannot_read)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	// strtod would read "3s" as 3; a typo has to be refused, not fired at the wrong time
	CHECK( !CinemaDirector_parseLine( "3s move 1 2 3", &shot, &isShot, &reason ) );
	CHECK( !reason.isEmpty() );
	CHECK( !CinemaDirector_parseLine( "3 move 1 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 zoom 0 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 pitch 50 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 route 5 100", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 hud maybe", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 filter sepia", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "3 dolly 1 2", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "-1 shot", &shot, &isShot, &reason ) );
}

TEST(cinema_eases_and_curves_through_its_points)
{
	CHECK_NEAR( CinemaDirector_ease( -1.0f ), 0.0f, 0.0001f );
	CHECK_NEAR( CinemaDirector_ease( 0.5f ), 0.5f, 0.0001f );
	CHECK_NEAR( CinemaDirector_ease( 2.0f ), 1.0f, 0.0001f );
	// level at the ends: the first tenth covers far less than a tenth of the way
	CHECK( CinemaDirector_ease( 0.1f ) < 0.05f );

	const Real xs[] = { 0.0f, 100.0f, 200.0f };
	const Real ys[] = { 0.0f, 100.0f, 0.0f };
	Real x = 0.0f;
	Real y = 0.0f;
	CinemaDirector_routePoint( xs, ys, 3, 0.0f, &x, &y );
	CHECK_NEAR( x, 0.0f, 0.001f );
	CinemaDirector_routePoint( xs, ys, 3, 0.5f, &x, &y );
	CHECK_NEAR( x, 100.0f, 0.001f );
	CHECK_NEAR( y, 100.0f, 0.001f );
	CinemaDirector_routePoint( xs, ys, 3, 1.0f, &x, &y );
	CHECK_NEAR( x, 200.0f, 0.001f );
	CHECK_NEAR( y, 0.0f, 0.001f );
	// between the points it bends through them rather than cutting the corner
	CinemaDirector_routePoint( xs, ys, 3, 0.25f, &x, &y );
	CHECK( y > 50.0f );
}

// the drone: the line, then the flight it plans, which is what the footage is

TEST(cinema_parses_a_drone_and_its_settings)
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;

	CHECK( CinemaDirector_parseLine( "30 drone 12 1000 1000 40 1500 1200 30 2000 1000 60 look 1800 1500 tilt -5 clear 30 bank 0.5 sway 0", &shot, &isShot, &reason ) );
	CHECK( isShot );
	CHECK_EQ( (Int)shot.verb, (Int)CINEMA_VERB_DRONE );
	CHECK_NEAR( shot.seconds, 12.0f, 0.001f );
	CHECK_EQ( shot.routePoints, 3 );
	CHECK_NEAR( shot.routeX[ 1 ], 1500.0f, 0.001f );
	CHECK_NEAR( shot.routeY[ 1 ], 1200.0f, 0.001f );
	CHECK_NEAR( shot.routeZ[ 2 ], 60.0f, 0.001f );
	CHECK_EQ( shot.drone.look, (Int)CINEMA_DRONE_LOOK_POINT );
	CHECK_NEAR( shot.drone.lookX, 1800.0f, 0.001f );
	CHECK_NEAR( shot.drone.lookY, 1500.0f, 0.001f );
	CHECK_NEAR( shot.drone.tilt, -5.0f, 0.001f );
	CHECK_NEAR( shot.drone.clearance, 30.0f, 0.001f );
	CHECK_NEAR( shot.drone.bank, 0.5f, 0.001f );
	CHECK_NEAR( shot.drone.sway, 0.0f, 0.001f );

	// no settings: look ahead, a little down, banking and swaying
	CHECK( CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30", &shot, &isShot, &reason ) );
	CHECK_EQ( shot.drone.look, (Int)CINEMA_DRONE_LOOK_AHEAD );
	CHECK( shot.drone.tilt < 0.0f );
	CHECK( shot.drone.clearance > 0.0f );
	CHECK_NEAR( shot.drone.bank, 1.0f, 0.001f );

	CHECK( CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30 look AmericaTankCrusader", &shot, &isShot, &reason ) );
	CHECK_EQ( shot.drone.look, (Int)CINEMA_DRONE_LOOK_OBJECT );
	CHECK_STR( shot.name.str(), "AmericaTankCrusader" );

	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 0 0 0 30 400 0 30", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30 wobble 3", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30 clear -1", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30 tilt", &shot, &isShot, &reason ) );
	CHECK( !CinemaDirector_parseLine( "0 drone 5 0 0 30 400 0 30 look 100", &shot, &isShot, &reason ) );
	CHECK( !reason.isEmpty() );
}

/// flat ground with one steep hill 200 high on (500, 0)
static Real cinemaTestHill( void *, Real x, Real y )
{
	const Real dx = x - 500.0f;
	return 200.0f * (Real)exp( -(dx * dx + y * y) / (2.0f * 60.0f * 60.0f) );
}

static Real cinemaTestFlat( void *, Real, Real )
{
	return 0.0f;
}

static void planCinemaTestDrone( CinemaDroneFlight *flight, const char *line, CinemaGroundFn ground )
{
	CinemaShot shot;
	Bool isShot = FALSE;
	AsciiString reason;
	CHECK( CinemaDirector_parseLine( line, &shot, &isShot, &reason ) );
	CinemaDrone_plan( flight, shot, ground, NULL );
}

TEST(cinema_drone_runs_up_cruises_and_stops)
{
	static CinemaDroneFlight flight;
	planCinemaTestDrone( &flight, "0 drone 10 0 0 20 1000 0 20 sway 0", cinemaTestFlat );
	CHECK_NEAR( flight.length, 1000.0f, 1.0f );

	Real speed = 0.0f;
	CHECK_NEAR( CinemaDrone_distance( flight, 0.0f, &speed ), 0.0f, 0.001f );
	CHECK_NEAR( speed, 0.0f, 0.001f );
	CHECK_NEAR( CinemaDrone_distance( flight, 10.0f, &speed ), flight.length, 0.01f );
	CHECK_NEAR( CinemaDrone_distance( flight, 5.0f, &speed ), flight.length * 0.5f, 0.5f );
	CHECK_NEAR( speed, flight.cruise, 0.001f );

	// never backwards, never a jump, and no faster than the cruise
	Real last = 0.0f;
	for (Int i = 1; i <= 1000; ++i)
	{
		const Real d = CinemaDrone_distance( flight, (Real)i * 0.01f, &speed );
		CHECK( d >= last );
		CHECK( d - last <= flight.cruise * 0.01f + 0.01f );
		CHECK( speed <= flight.cruise + 0.001f );
		last = d;
	}
	// a hundredth of a second in it has barely moved
	CinemaDrone_distance( flight, 0.1f, &speed );
	CHECK( speed < flight.cruise * 0.05f );
}

TEST(cinema_drone_keeps_clear_of_the_ground)
{
	static CinemaDroneFlight flight;
	planCinemaTestDrone( &flight, "0 drone 10 0 0 40 1000 0 40 clear 25 sway 0", cinemaTestHill );

	// level and at the height asked for, far from the hill
	CinemaDronePose pose;
	CinemaDrone_pose( flight, 0.0f, NULL, &pose );
	CHECK_NEAR( pose.eye.z, 40.0f, 0.5f );
	CHECK_NEAR( pose.heading, 0.0f, 0.001f );
	CHECK_NEAR( pose.roll, 0.0f, 0.001f );

	Real early = 0.0f;
	for (Int i = 0; i <= 2000; ++i)
	{
		CinemaDrone_pose( flight, (Real)i * 0.005f, NULL, &pose );
		const Real floor = cinemaTestHill( NULL, pose.eye.x, pose.eye.y ) + 25.0f;
		CHECK( pose.eye.z >= floor - 0.5f );
		if (pose.eye.x < 350.0f && pose.eye.z - floor > early)
			early = pose.eye.z - floor;
	}
	// it climbs before the hill rather than on its face: 150 short of the top it is already well
	// above what the ground there would ask
	CHECK( early > 50.0f );
}

TEST(cinema_drone_banks_into_a_turn)
{
	// a world camera with a positive roll drops the right side of the picture, which is what
	// View::setFreeCameraPose promises and what the drone's sign is written against
	Matrix3D camera;
	camera.Look_At( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), 0.3f );
	CHECK( camera.Get_X_Vector().Z < -0.1f );

	static CinemaDroneFlight flight;
	CinemaDronePose pose;
	// a left turn, the course winding anticlockwise from +x to +y
	planCinemaTestDrone( &flight, "0 drone 8 0 0 30 600 0 30 900 300 30 900 900 30 sway 0", cinemaTestFlat );
	CinemaDrone_pose( flight, 4.0f, NULL, &pose );
	CHECK( pose.roll < -0.05f );
	CHECK( pose.roll >= -40.0f * PI / 180.0f - 0.001f );

	planCinemaTestDrone( &flight, "0 drone 8 0 0 30 600 0 30 900 300 30 900 900 30 bank 0 sway 0", cinemaTestFlat );
	CinemaDrone_pose( flight, 4.0f, NULL, &pose );
	CHECK_NEAR( pose.roll, 0.0f, 0.0001f );

	// straight: level
	planCinemaTestDrone( &flight, "0 drone 8 0 0 30 900 0 30 sway 0", cinemaTestFlat );
	CinemaDrone_pose( flight, 4.0f, NULL, &pose );
	CHECK_NEAR( pose.roll, 0.0f, 0.001f );
	// looking ahead a little down, as the default tilt says
	CHECK_NEAR( pose.tilt, -10.0f * PI / 180.0f, 0.01f );

	// a target is looked at wherever the drone is
	Coord3D target;
	target.set( 450.0f, 500.0f, 30.0f );
	CinemaDrone_pose( flight, 4.0f, &target, &pose );
	CHECK_NEAR( pose.heading, (Real)atan2( 500.0f - pose.eye.y, 450.0f - pose.eye.x ), 0.001f );
}
