// The Red Alert style sidebar (ControlBarSidebar.h).  Phase 0: the seam only.  The tab and the scroll
// row are kept, Sidebar_scroll is the real thing, and the windows, the lists and the clicks come with
// phase 1 (docs/plans/HUD-SIDEBAR-LAYOUT.md, lane C).
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/ControlBarSidebar.h"

static Int s_tab = SIDEBAR_TAB_BUILDINGS;
static Int s_row = 0;

//-------------------------------------------------------------------------------------------------
void Sidebar_layout( const HudLayoutRects &rects )
{
	(void)rects;
}

//-------------------------------------------------------------------------------------------------
void Sidebar_update( void )
{
}

//-------------------------------------------------------------------------------------------------
void Sidebar_hide( void )
{
}

//-------------------------------------------------------------------------------------------------
void Sidebar_selectTab( Int tab )
{
	if( tab < 0 || tab >= SIDEBAR_TAB_COUNT )
		return;
	s_tab = tab;
	s_row = 0;
}

//-------------------------------------------------------------------------------------------------
Int Sidebar_selectedTab( void )
{
	return s_tab;
}

//-------------------------------------------------------------------------------------------------
void Sidebar_scrollBy( Int rows )
{
	// ponytail: no cameo count until lane C's lists exist, so only the top is a limit here
	s_row = Sidebar_scroll( 0x7fff, HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, s_row + rows );
}

//-------------------------------------------------------------------------------------------------
Int Sidebar_scroll( Int count, Int cols, Int rows, Int row )
{
	if( count <= 0 || cols <= 0 || rows <= 0 )
		return 0;
	const Int totalRows = ( count + cols - 1 ) / cols;
	const Int lastTop = totalRows > rows ? totalRows - rows : 0;
	if( row > lastTop )
		row = lastTop;
	return row < 0 ? 0 : row;
}
