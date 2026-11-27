// The map pickers' grid (docs/plans/MENU-REDESIGN.md, package 7; the design is
// experiments/proofs/2026-11-24/screens/pickers/README-pickers.md in the wrapper): a grid that fills
// its height exactly, square pictures with two name lines, and a list that opens on the picked map's
// row.  The rule is MapPicker.cpp's MapPickerGridShape and MapPickerFirstRow.  Included into
// test_gameengine.cpp.

#include "GameClient/MapPicker.h"

TEST( map_picker_grid_fills_its_height )
{
	// Official at 1080: the seat column 60 to 1276 from 182 to 952, eight columns to start
	const MapPickerShape official = MapPickerGridShape( 60, 1276, 182, 952, 8 );
	CHECK_EQ( official.columns, 8 );
	CHECK_EQ( official.tileWidth, 145 );
	CHECK_EQ( official.rows, 4 );
	CHECK_EQ( official.tileHeight, 186 );
	// the rows and their gaps end on the area's foot, give or take the division's rest
	const Int used = official.rows * official.tileHeight + ( official.rows - 1 ) * MAP_PICKER_GAP;
	CHECK( used <= 952 - 182 && 952 - 182 - used < official.rows );
	// every tile holds its square picture and two name lines
	CHECK( official.tileHeight >= official.tileWidth - 2 * MAP_PICKER_INSET + MAP_PICKER_NAME_ROOM );

	// the Random tab's map types under MAP SIZE: from nine columns, ten win
	const MapPickerShape types = MapPickerGridShape( 60, 1276, 306, 952, 9 );
	CHECK_EQ( types.columns, 10 );
	CHECK_EQ( types.rows, 4 );
	CHECK( types.tileHeight >= types.tileWidth - 2 * MAP_PICKER_INSET + MAP_PICKER_NAME_ROOM );

	// a crowded room's line under the tabs: the grid 44 lower still fills what is left
	const MapPickerShape crowded = MapPickerGridShape( 60, 1276, 226, 952, 8 );
	CHECK( crowded.columns >= 8 && crowded.columns < 12 );
	CHECK( crowded.rows * crowded.tileHeight + ( crowded.rows - 1 ) * MAP_PICKER_GAP <= 952 - 226 );

	// an area lower than any tile still gives one row
	const MapPickerShape low = MapPickerGridShape( 60, 1276, 900, 952, 8 );
	CHECK_EQ( low.rows, 1 );
	CHECK_EQ( low.tileHeight, 52 );
}

TEST( map_picker_places_a_screen_of_tiles_twice )
{
	/* Select Map crashed in the grid's first push_back past its vector's capacity: FactionPicker.cpp had a
		 PickerCell of UnicodeStrings at namespace scope, MapPicker.cpp one of ints, and the linker kept one
		 growth path for both.  This runs the grid's own cell vector through every growth of a full screen,
		 twice over, and reads every tile back. */
	std::vector< MapPickerTile > tiles;
	for( Int pass = 0; pass < 2; pass++ )
	{
		const Int first = pass == 0 ? 0 : 3;
		MapPickerPlaceTiles( 200, first, 182, 8, tiles );
		const MapPickerShape shape = MapPickerGridShape( 60, 1276, 182, 952, 8 );
		CHECK_EQ( (Int)tiles.size(), shape.columns * shape.rows );
		for( Int k = 0; k < (Int)tiles.size(); k++ )
		{
			CHECK_EQ( tiles[ k ].row, first * shape.columns + k );
			CHECK_EQ( tiles[ k ].left, 60 + ( k % shape.columns ) * ( shape.tileWidth + MAP_PICKER_GAP ) );
			CHECK_EQ( tiles[ k ].top, 182 + ( k / shape.columns ) * ( shape.tileHeight + MAP_PICKER_GAP ) );
			CHECK_EQ( tiles[ k ].right - tiles[ k ].left, shape.tileWidth );
			CHECK_EQ( tiles[ k ].bottom - tiles[ k ].top, shape.tileHeight );
		}
	}
	// the end of a list: the last screen holds the rows left, a first past it is pulled back
	MapPickerPlaceTiles( 62, 99, 182, 8, tiles );
	CHECK_EQ( (Int)tiles.size(), 62 - 4 * 8 );
	CHECK_EQ( tiles.back().row, 61 );
	MapPickerPlaceTiles( 0, 0, 182, 8, tiles );
	CHECK_EQ( (Int)tiles.size(), 0 );
}

TEST( map_picker_opens_on_the_picked_row )
{
	// 62 maps, 8 columns, 4 rows on screen: eight rows in all, the last first row is 4
	CHECK_EQ( MapPickerFirstRow( 0, 8, 4, 62 ), 0 );
	CHECK_EQ( MapPickerFirstRow( 31, 8, 4, 62 ), 0 );		// row 3: still on the first screen
	CHECK_EQ( MapPickerFirstRow( 32, 8, 4, 62 ), 1 );		// row 4 the last one shown
	CHECK_EQ( MapPickerFirstRow( 61, 8, 4, 62 ), 4 );		// the end: no empty screen under it
	CHECK_EQ( MapPickerFirstRow( -1, 8, 4, 62 ), 0 );		// nothing picked: the top
	CHECK_EQ( MapPickerFirstRow( 5, 8, 4, 6 ), 0 );			// fewer than a screen
	CHECK_EQ( MapPickerFirstRow( 5, 0, 4, 6 ), 0 );
}
