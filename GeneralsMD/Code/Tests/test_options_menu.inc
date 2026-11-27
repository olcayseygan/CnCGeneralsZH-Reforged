// The options screen (docs/plans/MENU-REDESIGN.md, package 8): the two merged lists read off and
// written back to the rows Options.ini keeps, the cost mark, and a page laid down its column with
// its fold open, closed and scrolled, at the design's numbers
// (experiments/proofs/2026-11-24/screens/settings/README-settings.md in the wrapper).  Included into
// test_gameengine.cpp, after test_room_layout.inc.  The rects themselves are held by
// Tools/optionsmenu_layout.py's selfcheck.

TEST( options_menu_texture_filtering_list_is_the_two_rows )
{
	CHECK_EQ( textureFilteringEntry( 0, 8 ), 0 );
	CHECK_EQ( textureFilteringEntry( 1, 8 ), 1 );
	CHECK_EQ( textureFilteringEntry( 2, 4 ), 2 );
	CHECK_EQ( textureFilteringEntry( 2, 8 ), 3 );
	CHECK_EQ( textureFilteringEntry( 2, 16 ), 4 );
	// the card's own choice reads as 16x
	CHECK_EQ( textureFilteringEntry( 2, 0 ), 4 );

	Int filter = -1, anisotropy = 6;
	textureFilteringRows( 1, &filter, &anisotropy );
	CHECK_EQ( filter, 1 );
	CHECK_EQ( anisotropy, 6 );	// a look at Trilinear keeps the picked level
	textureFilteringRows( 3, &filter, &anisotropy );
	CHECK_EQ( filter, TEXTURE_FILTER_ANISOTROPIC );
	CHECK_EQ( anisotropy, 8 );
	textureFilteringRows( 99, &filter, &anisotropy );
	CHECK_EQ( anisotropy, 16 );
	// every entry survives the round trip
	for( Int entry = 0; entry < TEXTURE_FILTERING_ENTRY_COUNT; ++entry )
	{
		textureFilteringRows( entry, &filter, &anisotropy );
		CHECK_EQ( textureFilteringEntry( filter, anisotropy ), entry );
	}
}

TEST( options_menu_reflections_list_is_the_two_rows )
{
	CHECK_EQ( waterReflectionsEntry( 0, 2 ), 0 );
	CHECK_EQ( waterReflectionsEntry( 1, 0 ), 1 );
	CHECK_EQ( waterReflectionsEntry( 1, 2 ), 3 );
	Int enabled = -1, mode = 2;
	waterReflectionsRows( 0, &enabled, &mode );
	CHECK_EQ( enabled, 0 );
	CHECK_EQ( mode, 2 );	// Off leaves the mode
	waterReflectionsRows( 2, &enabled, &mode );
	CHECK_EQ( enabled, 1 );
	CHECK_EQ( mode, 1 );
}

static OptionsFlowMetrics optionsTestMetrics( void )
{
	OptionsFlowMetrics m;
	m.top = 132;
	m.bottom = 954;
	m.groupGap = 4;
	m.faderMin = 330;
	return m;
}

static void optionsTestItem( OptionsFlowItem &item, Int kind, Bool shown )
{
	item.kind = kind;
	item.height = 34;
	item.shown = shown;
	item.top = -2;
	item.drawnHeight = 0;
}

TEST( options_menu_page_stands_down_its_column )
{
	// two groups: a head and two rows, a head and one row
	OptionsFlowItem items[ 5 ];
	optionsTestItem( items[ 0 ], OPTIONS_FLOW_HEAD, FALSE );
	optionsTestItem( items[ 1 ], OPTIONS_FLOW_ROW, TRUE );
	optionsTestItem( items[ 2 ], OPTIONS_FLOW_ROW, TRUE );
	optionsTestItem( items[ 3 ], OPTIONS_FLOW_HEAD, FALSE );
	optionsTestItem( items[ 4 ], OPTIONS_FLOW_ROW, TRUE );
	OptionsFlowResult all = optionsFlow( items, 5, optionsTestMetrics(), 0 );
	CHECK_EQ( items[ 0 ].top, 132 );
	CHECK_EQ( items[ 1 ].top, 166 );
	CHECK_EQ( items[ 2 ].top, 200 );
	CHECK_EQ( items[ 3 ].top, 238 );		// the group gap
	CHECK_EQ( items[ 4 ].top, 272 );
	CHECK_EQ( all.end, 306 );
	CHECK_EQ( all.maxScroll, 0 );

	// rows this machine lacks: the second group's only row goes and its head with it
	items[ 2 ].shown = FALSE;
	items[ 4 ].shown = FALSE;
	OptionsFlowResult fewer = optionsFlow( items, 5, optionsTestMetrics(), 0 );
	CHECK_EQ( items[ 1 ].top, 166 );
	CHECK_EQ( items[ 2 ].top, -1 );
	CHECK_EQ( items[ 3 ].top, -1 );
	CHECK_EQ( fewer.end, 200 );
}

TEST( options_menu_long_page_scrolls_to_the_body_foot )
{
	// Controls: 21 rows and 4 heads, 862 high, run past the body's 822
	OptionsFlowItem items[ 26 ];
	Int n = 0;
	for( Int group = 0; group < 4; ++group )
	{
		optionsTestItem( items[ n++ ], OPTIONS_FLOW_HEAD, FALSE );
		const Int rows = group == 0 ? 11 : group == 1 ? 4 : group == 2 ? 5 : 1;
		for( Int r = 0; r < rows; ++r )
			optionsTestItem( items[ n++ ], OPTIONS_FLOW_ROW, TRUE );
	}
	OptionsFlowResult top = optionsFlow( items, n, optionsTestMetrics(), 0 );
	CHECK_EQ( top.maxScroll, 862 - 822 );
	CHECK_EQ( top.end, 954 );
	for( Int i = 0; i < n; ++i )
		CHECK( items[ i ].top == -1 || items[ i ].top + items[ i ].drawnHeight <= 954 );
	CHECK_EQ( items[ 0 ].top, 132 );
	CHECK_EQ( items[ n - 1 ].top, -1 );

	// scrolled to its foot, the first head is gone and the last row stands on the body's foot
	optionsFlow( items, n, optionsTestMetrics(), 100000 );
	CHECK_EQ( items[ 0 ].top, -1 );
	CHECK_EQ( items[ n - 1 ].top, 954 - 34 );
}

TEST( options_menu_faders_take_what_the_rows_leave )
{
	// Audio: Volume's five faders, Output's three rows, Mix's two rows
	OptionsFlowItem items[ 13 ];
	Int n = 0;
	optionsTestItem( items[ n++ ], OPTIONS_FLOW_HEAD, FALSE );
	for( Int f = 0; f < 5; ++f )
		optionsTestItem( items[ n++ ], OPTIONS_FLOW_FADER, TRUE );
	optionsTestItem( items[ n++ ], OPTIONS_FLOW_HEAD, FALSE );
	for( Int r = 0; r < 3; ++r )
		optionsTestItem( items[ n++ ], OPTIONS_FLOW_ROW, TRUE );
	optionsTestItem( items[ n++ ], OPTIONS_FLOW_HEAD, FALSE );
	for( Int r = 0; r < 2; ++r )
		optionsTestItem( items[ n++ ], OPTIONS_FLOW_ROW, TRUE );
	OptionsFlowResult page = optionsFlow( items, n, optionsTestMetrics(), 0 );
	// the last row lands on the foot: 822 less 3 heads, 5 rows and 2 gaps
	CHECK_EQ( items[ 1 ].drawnHeight, 822 - 8 * 34 - 2 * 4 );
	CHECK_EQ( items[ 1 ].top, items[ 5 ].top );
	CHECK_EQ( page.end, 954 );
	CHECK_EQ( page.maxScroll, 0 );
}
