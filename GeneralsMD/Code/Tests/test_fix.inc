// Fix, the fixed point type GameLogic moves to (Lib/Fix.h), and everything around it: the literal,
// the INI reader, the float boundary, the trig, the matrix, the save and the random draw.  Included
// from test_gameengine.cpp.

#include "Lib/Fix.h"
#include "Lib/FixBoundary.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "WWMath/matrix3d.h"
#include "Common/Geometry.h"
#include "GameLogic/ObjectIter.h"
#include <type_traits>

// Fix * float has to be a compile error, not a silent conversion.
template <typename A, typename B, typename = void>
struct FixCanMultiply : std::false_type {};
template <typename A, typename B>
struct FixCanMultiply<A, B, std::void_t<decltype( std::declval<A>() * std::declval<B>() )>> : std::true_type {};

static_assert( !std::is_constructible<Fix, float>::value, "a float must not become a Fix" );
static_assert( !std::is_constructible<Fix, double>::value, "a double must not become a Fix" );
static_assert( !std::is_convertible<Fix, float>::value, "a Fix must not become a float" );
static_assert( !FixCanMultiply<Fix, float>::value, "Fix * float must not compile" );
static_assert( !FixCanMultiply<double, Fix>::value, "double * Fix must not compile" );
static_assert( FixCanMultiply<Fix, Int>::value, "Fix * Int is fine" );
static_assert( sizeof( Fix ) == 8, "a Fix is its raw int64" );

// The literal is read at compile time.
static_assert( (1.5_fx).raw() == 98304, "" );
static_assert( (0.1_fx).raw() == 6554, "" );						// 6553.6
static_assert( (-3.75_fx).raw() == -245760, "" );
static_assert( (1e3_fx).raw() == 1000 * 65536, "" );
static_assert( (2.5e-2_fx).raw() == 1638, "" );					// 1638.4
static_assert( (0.00000762939453125_fx).raw() == 1, "" );	// exactly half a step rounds away
static_assert( (7_fx).raw() == 7 * 65536, "" );
static_assert( (123456.789_fx).raw() == 8090864124LL, "" );	// 8090864123.9

static Real fixAsReal( Fix f ) { return fixToReal( f ); }

TEST(fix_multiplication_rounds_half_up_across_the_whole_range)
{
	CHECK_EQ( (1.5_fx * 2.25_fx).raw(), (3.375_fx).raw() );
	CHECK_EQ( (Fix::fromRaw( 1 ) * 0.5_fx).raw(), 1 );			// +1/2 step goes up
	CHECK_EQ( (Fix::fromRaw( -1 ) * 0.5_fx).raw(), 0 );			// -1/2 step goes up too
	CHECK_EQ( (Fix::fromRaw( 3 ) * 0.5_fx).raw(), 2 );
	CHECK_EQ( (Fix::fromRaw( -3 ) * 0.5_fx).raw(), -1 );
	CHECK_EQ( (Fix( -7 ) * Fix( 6 )).raw(), (Fix( -42 )).raw() );

	// products that need the full 128 bits on the way
	CHECK_EQ( (Fix( 1 << 20 ) * Fix( 1 << 20 )).raw(), (Int64)1 << 56 );
	CHECK_EQ( (Fix::fromRaw( 0x7FFFFFFFFFFFFFFFLL ) * Fix( 1 )).raw(), 0x7FFFFFFFFFFFFFFFLL );
	CHECK_EQ( (Fix::fromRaw( -0x7FFFFFFFFFFFFFFFLL ) * Fix( -1 )).raw(), 0x7FFFFFFFFFFFFFFFLL );
	CHECK_EQ( (Fix( 3000000 ) * Fix( -3000000 )).raw(), (Int64)-9000000000000LL * 65536 );

	Fix f = 2.5_fx;
	f *= Fix( 4 );
	f += 1.25_fx;
	f -= Fix( 1 );
	CHECK_EQ( f.raw(), (10.25_fx).raw() );
	CHECK( -f == Fix( 0 ) - f );
}

TEST(fix_division_truncates_towards_zero_and_saturates)
{
	CHECK_EQ( (Fix( 1 ) / Fix( 3 )).raw(), 21845 );					// 21845.33
	CHECK_EQ( (Fix( 2 ) / Fix( 3 )).raw(), 43690 );					// 43690.67, not 43691
	CHECK_EQ( (Fix( -2 ) / Fix( 3 )).raw(), -43690 );
	CHECK_EQ( (Fix( 2 ) / Fix( -3 )).raw(), -43690 );
	CHECK_EQ( (Fix( 7 ) / Fix( 2 )).raw(), (3.5_fx).raw() );
	CHECK_EQ( (Fix::fromRaw( 1 ) / Fix( 2 )).raw(), 0 );

	// the largest quotient that still fits, and one past it
	CHECK_EQ( (Fix::fromRaw( ((Int64)1 << 47) - 1 ) / Fix::fromRaw( 1 )).raw(), (((Int64)1 << 47) - 1) << 16 );
	CHECK_EQ( (Fix::fromRaw( (Int64)1 << 47 ) / Fix::fromRaw( 1 )).raw(), 0x7FFFFFFFFFFFFFFFLL );
	CHECK_EQ( (Fix::fromRaw( -((Int64)1 << 47) ) / Fix::fromRaw( 1 )).raw(), -0x7FFFFFFFFFFFFFFFLL );
	CHECK_EQ( (Fix::fromRaw( 0x7FFFFFFFFFFFFFFFLL ) / Fix::fromRaw( 0x7FFFFFFFFFFFFFFFLL )).raw(), 65536 );

	// division by zero asserts in a debug build and gives zero
	CHECK_EQ( (Fix( 5 ) / Fix( 0 )).raw(), 0 );
}

TEST(fix_floor_and_ceil_go_down_and_up)
{
	CHECK( (1.5_fx).floor() == Fix( 1 ) );
	CHECK( (1.5_fx).ceil() == Fix( 2 ) );
	CHECK( (-1.5_fx).floor() == Fix( -2 ) );
	CHECK( (-1.5_fx).ceil() == Fix( -1 ) );
	CHECK( Fix( 3 ).floor() == Fix( 3 ) );
	CHECK( Fix( -3 ).ceil() == Fix( -3 ) );
}

TEST(fix_ini_scanner_reads_the_decimal_exactly)
{
	CHECK_EQ( INI::scanFix( "0.1" ).raw(), 6554 );
	CHECK_EQ( INI::scanFix( "1e10" ).raw(), 10000000000LL * 65536 );
	CHECK_EQ( INI::scanFix( "-3.75" ).raw(), -245760 );
	CHECK_EQ( INI::scanFix( "  +2.5" ).raw(), 163840 );
	CHECK_EQ( INI::scanFix( "1.5e-3" ).raw(), 98 );							// 98.304
	CHECK_EQ( INI::scanFix( "1.5E+2" ).raw(), 150 * 65536 );
	CHECK_EQ( INI::scanFix( ".5" ).raw(), 32768 );
	CHECK_EQ( INI::scanFix( "5." ).raw(), 5 * 65536 );
	CHECK_EQ( INI::scanFix( "0.000" ).raw(), 0 );
	CHECK_EQ( INI::scanFix( "-0" ).raw(), 0 );
	CHECK_EQ( INI::scanFix( "1e" ).raw(), 65536 );								// like sscanf: the e is not part of it
	CHECK_EQ( INI::scanFix( "12abc" ).raw(), 12 * 65536 );						// and neither is trailing text
	CHECK_EQ( INI::scanFix( "0.00000762939453125" ).raw(), 1 );			// 2^-17, halfway: away from zero
	CHECK_EQ( INI::scanFix( "-0.00000762939453125" ).raw(), -1 );
	CHECK_EQ( INI::scanFix( "123456.789" ).raw(), 8090864124LL );
	CHECK_EQ( INI::scanFix( "0.1000000000000000000000001" ).raw(), 6554 );	// digits past 19 dropped
	CHECK_EQ( INI::scanFix( "140737488355327" ).raw(), 140737488355327LL * 65536 );	// 2^47 - 1

	const char *bad[] = { "", "abc", "-", ".", "e5", "1e30", "140737488355328", "123456789012345678901234" };
	for( size_t i = 0; i < sizeof( bad ) / sizeof( bad[ 0 ] ); ++i )
	{
		Bool threw = FALSE;
		try { INI::scanFix( bad[ i ] ); }
		catch( ... ) { threw = TRUE; }
		if( !threw ) printf( "  scanFix(\"%s\") did not throw\n", bad[ i ] );
		CHECK( threw );
	}
}

// PrimaryDamage = 0.00001 is in four Zero Hour weapons; rounded to the nearest step it would do no
// damage at all.  A value the text says is not zero loads as at least one step.
TEST(fix_ini_scanner_rounds_a_tiny_value_up_to_one_step)
{
	CHECK_EQ( INI::scanFix( "0.00001" ).raw(), 1 );							// 0.655 of a step
	CHECK_EQ( INI::scanFix( "0.0000076293945312" ).raw(), 1 );				// just under halfway
	CHECK_EQ( INI::scanFix( "0.000001" ).raw(), 1 );
	CHECK_EQ( INI::scanFix( "1e-70" ).raw(), 1 );								// past the exponent cut
	CHECK_EQ( INI::scanFix( "-0.000001" ).raw(), -1 );
	CHECK_EQ( INI::scanFix( "0.0000152587890625" ).raw(), 1 );				// exactly one step stays one
	CHECK_EQ( INI::scanFix( "0.00003" ).raw(), 2 );							// 1.97: ordinary rounding
	CHECK_EQ( INI::scanFix( "0" ).raw(), 0 );									// zero is still zero
	CHECK_EQ( INI::scanFix( "0.0e-80" ).raw(), 0 );
}

// the parsers read the next token off strtok's buffer, so prime it with a field name first
static Fix parseFixField( void (*parse)( INI *, void *, void *, const void * ), const char *value )
{
	static char line[ 128 ];
	sprintf( line, "Field = %s", value );
	strtok( line, " \n\r\t=" );
	INI ini;
	Fix out = Fix( -12345 );
	parse( &ini, NULL, &out, NULL );
	return out;
}

TEST(fix_ini_parsers_scale_before_they_round)
{
	CHECK_EQ( parseFixField( INI::parseFix, "2.5" ).raw(), 163840 );
	CHECK_EQ( parseFixField( INI::parsePercentToFix, "50%" ).raw(), 32768 );
	CHECK_EQ( parseFixField( INI::parsePercentToFix, "95.4%" ).raw(), 62521 );		// 62521.34
	CHECK_EQ( parseFixField( INI::parseDurationFix, "1000" ).raw(), 30 * 65536 );
	CHECK_EQ( parseFixField( INI::parseDurationFix, "100" ).raw(), 3 * 65536 );		// exact, where 100 * 0.03f is not
	CHECK_EQ( parseFixField( INI::parseVelocityFix, "30" ).raw(), 65536 );
	CHECK_EQ( parseFixField( INI::parseVelocityFix, "10" ).raw(), 21845 );			// 21845.33
	CHECK_EQ( parseFixField( INI::parseVelocityFix, "0.0003" ).raw(), 1 );			// 0.66 of a step after the divide
	CHECK_EQ( parseFixField( INI::parseAccelerationFix, "900" ).raw(), 65536 );
	CHECK_EQ( parseFixField( INI::parseAngleFix, "180" ).raw(), 205887 );				// PI: 205887.42
	CHECK_EQ( parseFixField( INI::parseAngleFix, "90" ).raw(), 102944 );				// 102943.71
	CHECK_EQ( parseFixField( INI::parseAngleFix, "-45" ).raw(), -51472 );				// -51471.85
	CHECK_EQ( parseFixField( INI::parseAngularVelocityFix, "180" ).raw(), 6863 );		// PI/30: 6862.91
}

static Fix fixFromBits( UnsignedInt bits ) { return fixFromFloatBits( bits ); }

TEST(fix_from_float_bits_rounds_to_nearest_even_and_saturates)
{
	CHECK_EQ( fixFromReal( 1.5f ).raw(), 98304 );
	CHECK_EQ( fixFromReal( -1.5f ).raw(), -98304 );
	CHECK_EQ( fixFromReal( 0.1f ).raw(), 6554 );
	CHECK_EQ( fixFromReal( 0.0f ).raw(), 0 );
	CHECK_EQ( fixFromReal( -0.0f ).raw(), 0 );
	CHECK_EQ( fixFromReal( 12345.25f ).raw(), 809058304LL );

	// ties to even: 1/2 step -> 0, 3/2 -> 2, 5/2 -> 2
	CHECK_EQ( fixFromReal( 1.0f / 131072.0f ).raw(), 0 );
	CHECK_EQ( fixFromReal( 3.0f / 131072.0f ).raw(), 2 );
	CHECK_EQ( fixFromReal( 5.0f / 131072.0f ).raw(), 2 );
	CHECK_EQ( fixFromReal( -3.0f / 131072.0f ).raw(), -2 );
	// just above half a step
	CHECK_EQ( fixFromReal( 1.0f / 131072.0f * 1.0000001f ).raw(), 1 );
	CHECK_EQ( fixFromReal( 1.0f / 262144.0f ).raw(), 0 );

	CHECK_EQ( fixFromBits( 0x00000001 ).raw(), 0 );						// smallest subnormal
	CHECK_EQ( fixFromBits( 0x7FC00000 ).raw(), 0 );						// NaN
	CHECK_EQ( fixFromBits( 0x7F800000 ).raw(), 0x7FFFFFFFFFFFFFFFLL );	// +inf
	CHECK_EQ( fixFromBits( 0xFF800000 ).raw(), -0x7FFFFFFFFFFFFFFFLL );	// -inf
	CHECK_EQ( fixFromReal( 1e30f ).raw(), 0x7FFFFFFFFFFFFFFFLL );
	CHECK_EQ( fixFromReal( 70368744177664.0f ).raw(), (Int64)1 << 62 );	// 2^46 fits
	CHECK_EQ( fixFromReal( 140737488355328.0f ).raw(), 0x7FFFFFFFFFFFFFFFLL );	// 2^47 does not

	// and back out again
	CHECK( fixAsReal( 1.5_fx ) == 1.5f );
	CHECK( fixAsReal( -3.75_fx ) == -3.75f );
	for( Int i = -1000; i <= 1000; ++i )
	{
		Real r = (Real)i * 0.37f;
		CHECK_NEAR( fixAsReal( fixFromReal( r ) ), r, 0.5 / 65536.0 + 1e-9 );
	}
}

TEST(fix_trig_matches_the_float_reference)
{
	Int worstSin = 0, worstAtan = 0;
	for( Int i = -2000; i <= 2000; ++i )
	{
		Fix a = Fix::fromRaw( (Int64)i * 1117 );		// about -34 to +34 radians
		double x = (double)a.raw() / 65536.0;
		Int es = (Int)fabs( (double)fixSin( a ).raw() - sin( x ) * 65536.0 );
		Int ec = (Int)fabs( (double)fixCos( a ).raw() - cos( x ) * 65536.0 );
		if( es > worstSin ) worstSin = es;
		if( ec > worstSin ) worstSin = ec;
	}
	for( Int yi = -20; yi <= 20; ++yi )
		for( Int xi = -20; xi <= 20; ++xi )
		{
			Fix y = Fix::fromRaw( (Int64)yi * 40503 ), x = Fix::fromRaw( (Int64)xi * 70001 );
			double ref = ( yi == 0 && xi == 0 ) ? 0.0 : atan2( (double)y.raw(), (double)x.raw() );
			Int e = (Int)fabs( (double)fixAtan2( y, x ).raw() - ref * 65536.0 );
			if( e > worstAtan ) worstAtan = e;
		}
	printf( "  worst sin/cos %d steps, atan2 %d steps\n", worstSin, worstAtan );
	CHECK( worstSin <= 2 );
	CHECK( worstAtan <= 2 );

	CHECK( fixSin( Fix( 0 ) ) == Fix( 0 ) );
	CHECK( fixCos( Fix( 0 ) ) == Fix( 1 ) );
	CHECK( fixAtan2( Fix( 0 ), Fix( -1 ) ).raw() == 205887 );		// +PI, not -PI
	CHECK( fixAtan2( Fix( 1 ), Fix( 0 ) ).raw() == 102944 );		// PI/2
	CHECK( fixAtan2( Fix( 0 ), Fix( 0 ) ) == Fix( 0 ) );
	CHECK_EQ( fixRadiansToTurn( Fix( 0 ) ), 0u );
	CHECK_EQ( fixRadiansToTurn( Fix::fromRaw( 102944 ) ), 0x40000000u + 3045u );	// 0.29 of a step past PI/2
}

TEST(fix_sqrt_and_log2_match_the_float_reference)
{
	CHECK( fixSqrt( Fix( 4 ) ) == Fix( 2 ) );
	CHECK_EQ( fixSqrt( Fix( 2 ) ).raw(), 92682 );				// 92681.9
	CHECK( fixSqrt( Fix( 0 ) ) == Fix( 0 ) );
	CHECK( fixSqrt( Fix( -4 ) ) == Fix( 0 ) );
	CHECK_EQ( fixSqrt( Fix::fromRaw( 1 ) ).raw(), 256 );
	CHECK_EQ( fixSqrt( Fix::fromRaw( 0x7FFFFFFFFFFFFFFFLL ) ).raw(), 777472127994LL );	// the 128 bit path

	CHECK( fixLog2( Fix( 8 ) ) == Fix( 3 ) );
	CHECK( fixLog2( 0.5_fx ) == Fix( -1 ) );
	CHECK( fixLog2( Fix( 1 ) ) == Fix( 0 ) );
	CHECK( fixLog2( Fix( 0 ) ) == Fix( 0 ) );

	Int worstSqrt = 0, worstLog = 0;
	for( Int i = 1; i < 5000; ++i )
	{
		Int64 raw = (Int64)i * i * i * 977 + i;
		Fix v = Fix::fromRaw( raw );
		double x = (double)raw / 65536.0;
		Int es = (Int)fabs( (double)fixSqrt( v ).raw() - sqrt( x ) * 65536.0 + 0.0 );
		Int el = (Int)fabs( (double)fixLog2( v ).raw() - log( x ) / log( 2.0 ) * 65536.0 );
		if( es > worstSqrt ) worstSqrt = es;
		if( el > worstLog ) worstLog = el;
	}
	printf( "  worst sqrt %d steps, log2 %d steps\n", worstSqrt, worstLog );
	CHECK( worstSqrt <= 1 );
	CHECK( worstLog <= 2 );
}

TEST(fix_matrix_rotates_like_matrix3d)
{
	const Real angles[ 3 ] = { 0.7f, -1.9f, 2.6f };
	FixMatrix3D f;
	f.makeIdentity();
	FCoord3D t;
	t.set( Fix( 10 ), -2.5_fx, 0.125_fx );
	f.setTranslation( t );
	f.rotateX( fixFromReal( angles[ 0 ] ) );
	f.rotateY( fixFromReal( angles[ 1 ] ) );
	f.rotateZ( fixFromReal( angles[ 2 ] ) );

	Matrix3D m( true );
	m.Set_Translation( Vector3( 10.0f, -2.5f, 0.125f ) );
	m.Rotate_X( angles[ 0 ] );
	m.Rotate_Y( angles[ 1 ] );
	m.Rotate_Z( angles[ 2 ] );

	Matrix3D converted;
	f.toMatrix3D( &converted );
	for( Int i = 0; i < 3; ++i )
		for( Int j = 0; j < 4; ++j )
			CHECK_NEAR( converted[ i ][ j ], m[ i ][ j ], 1e-4 );

	FCoord3D p;
	p.set( Fix( 3 ), Fix( -4 ), 5.5_fx );
	Vector3 ref;
	Matrix3D::Transform_Vector( m, Vector3( 3.0f, -4.0f, 5.5f ), &ref );
	Coord3D got = f.transformPoint( p ).toCoord3D();
	CHECK_NEAR( got.x, ref.X, 1e-3 );
	CHECK_NEAR( got.y, ref.Y, 1e-3 );
	CHECK_NEAR( got.z, ref.Z, 1e-3 );

	// the orthogonal inverse takes the point back to within a few steps
	FixMatrix3D inv;
	f.getOrthogonalInverse( inv );
	FCoord3D back = inv.transformPoint( f.transformPoint( p ) );
	CHECK( fixAbs( back.x - p.x ).raw() <= 8 );
	CHECK( fixAbs( back.y - p.y ).raw() <= 8 );
	CHECK( fixAbs( back.z - p.z ).raw() <= 8 );

	FRegion3D r;
	r.lo.set( Fix( 0 ), Fix( 0 ), Fix( 0 ) );
	r.hi.set( Fix( 4 ), Fix( 2 ), Fix( 1 ) );
	FCoord3D inside;
	inside.set( 1.5_fx, 1.5_fx, Fix( 99 ) );
	CHECK( r.isInRegionNoZ( inside ) );
	CHECK( r.width() == Fix( 4 ) );
	FCoord3D v;
	v.set( Fix( 3 ), Fix( 4 ), Fix( 0 ) );
	CHECK( v.length() == Fix( 5 ) );
}

TEST(fix_xfer_saves_the_raw_value)
{
	bootOnce();
	const char *path = "test_fix_xfer_tmp.bin";

	Fix a = Fix::fromRaw( -0x123456789ABCLL );
	FCoord3D c;
	c.set( 1.5_fx, -2.25_fx, Fix::fromRaw( 1 ) );
	FixMatrix3D m;
	m.makeIdentity();
	m.rotateZ( 0.5_fx );

	XferSave write;
	write.open( AsciiString( path ) );
	write.xferFix( &a );
	write.xferFCoord3D( &c );
	write.xferFixMatrix3D( &m );
	write.close();

	FILE *fp = fopen( path, "rb" );
	CHECK( fp != NULL );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		CHECK_EQ( ftell( fp ), 8 + 24 + 1 + 96 );	// the matrix carries a version byte
		fclose( fp );
	}

	Fix a2 = Fix( 0 );
	FCoord3D c2;
	c2.zero();
	FixMatrix3D m2;
	memset( &m2, 0, sizeof( m2 ) );
	XferLoad read;
	read.open( AsciiString( path ) );
	read.xferFix( &a2 );
	read.xferFCoord3D( &c2 );
	read.xferFixMatrix3D( &m2 );
	read.close();
	remove( path );

	CHECK( a2 == a );
	CHECK( c2.x == c.x && c2.y == c.y && c2.z == c.z );
	CHECK( memcmp( &m2, &m, sizeof( m ) ) == 0 );
}

TEST(fix_logic_random_value_stays_in_range_and_repeats)
{
	InitRandom( 4242 );
	Fix first[ 8 ];
	for( Int i = 0; i < 8; ++i )
		first[ i ] = GameLogicRandomValueFix( -1.5_fx, 2.25_fx );

	InitRandom( 4242 );
	Bool same = TRUE, distinct = FALSE;
	for( Int i = 0; i < 8; ++i )
	{
		Fix v = GameLogicRandomValueFix( -1.5_fx, 2.25_fx );
		same = same && v == first[ i ];
		distinct = distinct || v != first[ 0 ];
	}
	CHECK( same );
	CHECK( distinct );

	Bool inRange = TRUE, sawLo = FALSE, sawHi = FALSE;
	for( Int i = 0; i < 20000; ++i )
	{
		Fix v = GameLogicRandomValueFix( Fix::fromRaw( 10 ), Fix::fromRaw( 13 ) );
		inRange = inRange && v >= Fix::fromRaw( 10 ) && v <= Fix::fromRaw( 13 );
		sawLo = sawLo || v == Fix::fromRaw( 10 );
		sawHi = sawHi || v == Fix::fromRaw( 13 );
	}
	CHECK( inRange );
	CHECK( sawLo && sawHi );
	CHECK( GameLogicRandomValueFix( Fix( 3 ), Fix( 3 ) ) == Fix( 3 ) );
	CHECK( GameLogicRandomValueFix( Fix( 3 ), Fix( 1 ) ) == Fix( 1 ) );
}

// P2: the angle folds into (-PI, PI] the way normalizeAngle does, and PI itself stays put.
TEST(fix_normalize_angle_folds_into_one_turn)
{
	CHECK( fixNormalizeAngle( FIX_PI ) == FIX_PI );
	CHECK( fixNormalizeAngle( -FIX_PI ) == -FIX_PI + FIX_TWO_PI );		// -PI is not in the range; it comes round
	CHECK( fixNormalizeAngle( 7.0_fx ) == 7.0_fx - FIX_TWO_PI );
	CHECK( fixNormalizeAngle( -7.0_fx ) == -7.0_fx + FIX_TWO_PI );
	CHECK( fixNormalizeAngle( 0.5_fx ) == 0.5_fx );
}

// P2: GeometryInfo holds its extents in Fix, works its bounding radii out in Fix, and saves them as
// Fix.  A 3 by 4 box has a bounding circle of exactly 5.
TEST(fix_geometry_extents_are_fixed_and_save_as_fix)
{
	bootOnce();
	GeometryInfo box( GEOMETRY_BOX, FALSE, 10, 3, 4 );
	CHECK( box.getBoundingCircleRadiusFix() == Fix( 5 ) );
	CHECK( box.getBoundingSphereRadiusFix() == fixSqrt( Fix( 50 ) ) );
	CHECK( box.getZDeltaToCenterPositionFix() == Fix( 5 ) );
	CHECK( box.getMajorRadius() == 3.0f );		// the P2 shim reads the same value back

	GeometryInfo cyl( GEOMETRY_CYLINDER, FALSE, 30, 5, 0 );
	CHECK( cyl.getBoundingSphereRadiusFix() == Fix( 15 ) );	// half the height beats the radius
}

TEST(fix_geometry_saves_as_fix)
{
	bootOnce();
	GeometryInfo box( GEOMETRY_BOX, FALSE, 10, 3, 4 );
	const char *path = "test_fix_geometry_tmp.bin";
	box.setMajorRadiusFix( 1.25_fx );
	XferSave write;
	write.open( AsciiString( path ) );
	write.xferSnapshot( &box );
	write.close();

	GeometryInfo back( GEOMETRY_SPHERE, FALSE, 1, 1, 1 );
	XferLoad read;
	read.setOptions( XO_NO_POST_PROCESSING );		// there is no game state to hand it to
	read.open( AsciiString( path ) );
	read.xferSnapshot( &back );
	read.close();
	remove( path );

	CHECK( back.getGeomType() == GEOMETRY_BOX );
	CHECK( back.getMajorRadiusFix() == 1.25_fx );
	CHECK( back.getBoundingCircleRadiusFix() == box.getBoundingCircleRadiusFix() );
}

// P2: the range query's iterator sorts on a Fix; equal distances keep the order they came in.
TEST(fix_object_iterator_sorts_on_fix_and_keeps_ties_in_order)
{
	bootOnce();
	SimpleObjectIterator *iter = newInstance(SimpleObjectIterator);
	Object *a = (Object *)0x10, *b = (Object *)0x20, *c = (Object *)0x30, *d = (Object *)0x40;
	iter->insertFix( a, Fix( 3 ) );		// insert puts each at the head: the list reads d c b a
	iter->insertFix( b, Fix( 1 ) );
	iter->insertFix( c, Fix( 2 ) );
	iter->insertFix( d, Fix( 1 ) );
	iter->sort( ITER_SORTED_NEAR_TO_FAR );
	Fix n;
	CHECK( iter->firstWithNumericFix( &n ) == d );
	CHECK( n == Fix( 1 ) );
	CHECK( iter->nextWithNumericFix( &n ) == b );
	CHECK( iter->nextWithNumericFix( &n ) == c );
	CHECK( iter->nextWithNumericFix( &n ) == a );
	CHECK( n == Fix( 3 ) );
	iter->sort( ITER_SORTED_FAR_TO_NEAR );
	CHECK( iter->first() == a );
	iter->deleteInstance();
}
