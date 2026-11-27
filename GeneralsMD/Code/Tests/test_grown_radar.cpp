// test_grown_radar.cpp: how tall Minimap Size draws a grown radar in a side's frame (GameClient/GrownRadarSize.h).

#include "test_harness.h"

#include "GameClient/GrownRadarSize.h"

static const int BOX = 230;

static void checkEveryStepGrows( int glass )
{
	for( int p = 102; p <= 200; p++ )
		CHECK( GrownRadar_insideHeight( BOX, glass, p ) > GrownRadar_insideHeight( BOX, glass, p - 1 ) );
	CHECK_EQ( GrownRadar_insideHeight( BOX, glass, 200 ), 2 * BOX );
}

TEST(grown_radar_tall_map_ramps_from_the_glass)
{
	const int glass = 307;		// 3:4, the glass's width needs 133
	CHECK( GrownRadar_insideHeight( BOX, glass, 101 ) >= glass );
	CHECK( GrownRadar_insideHeight( BOX, glass, 101 ) < GrownRadar_insideHeight( BOX, glass, 120 ) );
	checkEveryStepGrows( glass );
}

TEST(grown_radar_too_tall_map_ramps_from_the_box)
{
	checkEveryStepGrows( 2 * BOX );		// 1:2
	checkEveryStepGrows( 3 * BOX );
	CHECK_EQ( GrownRadar_insideHeight( BOX, 2 * BOX, 150 ), 345 );
}

TEST(grown_radar_wide_map_is_the_percent)
{
	CHECK_EQ( GrownRadar_insideHeight( BOX, 150, 101 ), 232 );
	CHECK_EQ( GrownRadar_insideHeight( BOX, 150, 150 ), 345 );
	checkEveryStepGrows( 150 );
}

TEST(grown_radar_percent_is_held_to_100_200)
{
	CHECK_EQ( GrownRadar_insideHeight( BOX, 150, 50 ), BOX );
	CHECK_EQ( GrownRadar_insideHeight( BOX, 307, 900 ), 2 * BOX );
}
