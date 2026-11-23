// The partition manager's shape tests, through geomCollidesWithGeom: the same procs decide every
// collision pair, building placement and the would-collide filter, so they are GameLogic and have
// to give the same answer on every machine and in either argument order.

static Bool PartitionCollideTest_hit( const Coord3D &posA, const GeometryInfo &geomA, Real angleA,
	const Coord3D &posB, const GeometryInfo &geomB, Real angleB )
{
	return ThePartitionManager->geomCollidesWithGeom( &posA, geomA, angleA, &posB, geomB, angleB );
}

/** A thin box laid across a wider one with no corner of either inside the other: a wall crossing a
	 hull.  EA's corner test missed it; the separating-axis test does not, and still lets a box that
	 is clear of the wall pass. */
TEST(partition_a_thin_box_crossing_a_box_with_no_corner_inside_collides)
{
	ThePartitionManager = NEW PartitionManager;	// only for its geometry test

	const GeometryInfo wall( GEOMETRY_BOX, FALSE, 10.0f, 50.0f, 1.0f );	// x -50..50, y -1..1
	const GeometryInfo strip( GEOMETRY_BOX, FALSE, 10.0f, 1.0f, 30.0f );	// 2 wide, 60 long
	Coord3D origin; origin.zero();
	Coord3D across; across.set( 10.0f, 0.0f, 0.0f );
	Coord3D clear; clear.set( 60.0f, 0.0f, 0.0f );

	CHECK( PartitionCollideTest_hit( origin, wall, 0.0f, across, strip, 0.0f ) );
	CHECK( PartitionCollideTest_hit( across, strip, 0.0f, origin, wall, 0.0f ) );
	CHECK( !PartitionCollideTest_hit( origin, wall, 0.0f, clear, strip, 0.0f ) );

	// the same crossing with the strip described as a long box turned a quarter
	const GeometryInfo turnedStrip( GEOMETRY_BOX, FALSE, 10.0f, 30.0f, 1.0f );
	CHECK( PartitionCollideTest_hit( origin, wall, 0.0f, across, turnedStrip, PI / 2.0f ) );

	delete ThePartitionManager;
	ThePartitionManager = NULL;
}

/** A circle is a circle: a cylinder of radius 10 whose centre is 11.3 from a box's corner misses it
	 whichever way the cylinder faces.  EA made a square of the circle turned to its facing, so this
	 one touched at facing 0 and the reach along the box's side grew by 41% at 45 degrees. */
TEST(partition_a_cylinder_beside_a_box_corner_is_tested_as_a_circle)
{
	ThePartitionManager = NEW PartitionManager;

	const GeometryInfo box( GEOMETRY_BOX, FALSE, 10.0f, 10.0f, 10.0f );	// x, y -10..10
	const GeometryInfo round( GEOMETRY_CYLINDER, FALSE, 10.0f, 10.0f, 10.0f );
	Coord3D origin; origin.zero();
	Coord3D offCorner; offCorner.set( 18.0f, 18.0f, 0.0f );
	Coord3D nearCorner; nearCorner.set( 17.0f, 17.0f, 0.0f );	// 9.9 from the corner
	Coord3D offSide; offSide.set( 0.0f, 21.0f, 0.0f );

	CHECK( !PartitionCollideTest_hit( origin, box, 0.0f, offCorner, round, 0.0f ) );
	CHECK( !PartitionCollideTest_hit( offCorner, round, 0.0f, origin, box, 0.0f ) );
	CHECK( PartitionCollideTest_hit( origin, box, 0.0f, nearCorner, round, 0.0f ) );
	CHECK( !PartitionCollideTest_hit( origin, box, 0.0f, offSide, round, PI / 4.0f ) );
	CHECK( !PartitionCollideTest_hit( origin, box, 0.0f, offSide, round, 0.0f ) );

	delete ThePartitionManager;
	ThePartitionManager = NULL;
}

/** A sphere's position is its centre.  One centred 4 under a box's base, radius 5, reaches into it,
	 and the answer does not depend on which of the two is asked first; EA's z test left the sphere's
	 lower half out and refused it with the box first. */
TEST(partition_a_sphere_under_a_box_base_collides_in_either_order)
{
	ThePartitionManager = NEW PartitionManager;

	const GeometryInfo ball( GEOMETRY_SPHERE, FALSE, 5.0f, 5.0f, 5.0f );
	const GeometryInfo box( GEOMETRY_BOX, FALSE, 10.0f, 10.0f, 10.0f );
	Coord3D ballPos; ballPos.set( 0.0f, 0.0f, 0.0f );
	Coord3D boxPos; boxPos.set( 0.0f, 0.0f, 4.0f );
	Coord3D boxHigh; boxHigh.set( 0.0f, 0.0f, 6.0f );

	CHECK( PartitionCollideTest_hit( ballPos, ball, 0.0f, boxPos, box, 0.0f ) );
	CHECK( PartitionCollideTest_hit( boxPos, box, 0.0f, ballPos, ball, 0.0f ) );
	CHECK( !PartitionCollideTest_hit( ballPos, ball, 0.0f, boxHigh, box, 0.0f ) );
	CHECK( !PartitionCollideTest_hit( boxHigh, box, 0.0f, ballPos, ball, 0.0f ) );

	delete ThePartitionManager;
	ThePartitionManager = NULL;
}
