// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.

#include "GameClient/DisplayCutout.h"
#include <limits.h>

namespace {
Int cutoutX = 772, cutoutY = 0, cutoutWidth = 184, cutoutHeight = 32;
Bool cutoutPresent = TRUE;
Bool queryTestCutout( Int &x, Int &y, Int &width, Int &height )
{
	x = cutoutX; y = cutoutY; width = cutoutWidth; height = cutoutHeight;
	return cutoutPresent;
}
struct CutoutFixture
{
	DisplayCutoutQuery saved = TheDisplayCutoutQuery;
	CutoutFixture()
	{
		cutoutX = 772; cutoutY = 0; cutoutWidth = 184; cutoutHeight = 32; cutoutPresent = TRUE;
		TheDisplayCutoutQuery = queryTestCutout;
	}
	~CutoutFixture() { TheDisplayCutoutQuery = saved; }
};
}

TEST(display_cutout_moves_only_intersecting_widgets)
{
	CutoutFixture fixture;
	CHECK_EQ( displayCutoutTopOffset( 0, 0, 772, 32 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 956, 0, 772, 32 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 32 );
	CHECK_EQ( displayCutoutTopOffset( 700, 8, 100, 20 ), 24 );
	CHECK_EQ( displayCutoutTopOffset( 800, 32, 100, 20 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 800, 40, 100, 20 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 800, -30, 100, 30 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 800, -20, 100, 30 ), 52 );
}

TEST(display_cutout_default_and_invalid_bounds_leave_layout_unchanged)
{
	CutoutFixture fixture;
	TheDisplayCutoutQuery = NULL;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
	TheDisplayCutoutQuery = queryTestCutout;
	cutoutPresent = FALSE;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
	cutoutPresent = TRUE;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 0, 20 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, -1 ), 0 );
	cutoutWidth = -1;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
	cutoutWidth = 184; cutoutHeight = 0;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
	cutoutHeight = 32; cutoutX = -1; cutoutWidth = 1000;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
	cutoutX = 772; cutoutY = -1;
	CHECK_EQ( displayCutoutTopOffset( 800, 0, 100, 20 ), 0 );
}

TEST(display_cutout_handles_clipped_and_large_widget_coordinates)
{
	CutoutFixture fixture;
	CHECK_EQ( displayCutoutTopOffset( -20, 0, 10, 20 ), 0 );
	CHECK_EQ( displayCutoutTopOffset( -20, 0, 1000, 20 ), 32 );
	cutoutX = INT_MAX - 20; cutoutY = 10; cutoutWidth = 10; cutoutHeight = 20;
	CHECK_EQ( displayCutoutTopOffset( INT_MAX - 15, 0, 20, 15 ), 30 );
	cutoutY = INT_MAX - 10; cutoutHeight = 20;
	CHECK_EQ( displayCutoutTopOffset( INT_MAX - 15, INT_MAX - 5, 20, 10 ), 0 );
}
