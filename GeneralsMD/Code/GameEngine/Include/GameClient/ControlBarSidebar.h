// The Red Alert style sidebar (HudLayout 1, docs/plans/HUD-SIDEBAR-LAYOUT.md): four production tabs,
// a 2x6 grid of build cameos that scrolls two to a row, and the two arrows under it, made in code as
// children of ControlBarParent.  ControlBar::layoutSidebar places the bar's own windows (radar, money,
// the strip's ButtonCommand01..14) and then hands the rest here; every rectangle comes from
// HudLayoutForScreen (GameClient/Display.h), nothing here is a pixel of its own.  Client only: the logic
// hears of a cameo only through the messages a click sends (MSG_QUEUE_UNIT_FOR_GROUP, structure
// placement, MSG_CANCEL_UNIT_CREATE).
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __CONTROLBARSIDEBAR_H__
#define __CONTROLBARSIDEBAR_H__

#include "Lib/BaseType.h"
#include "GameClient/Display.h"

/** The tabs, left to right, keyed Q W E R; GUI:SidebarTab0..3 name them. */
enum SidebarTab
{
	SIDEBAR_TAB_BUILDINGS,		///< Bina: what the builders put up, less base defences
	SIDEBAR_TAB_DEFENSE,			///< Savunma: KINDOF_FS_BASE_DEFENSE structures
	SIDEBAR_TAB_INFANTRY,			///< Piyade: KINDOF_INFANTRY units
	SIDEBAR_TAB_VEHICLES,			///< Araç: every other unit, aircraft included

	SIDEBAR_TAB_COUNT
};
static_assert( SIDEBAR_TAB_COUNT == HUD_SIDEBAR_TABS, "one tab cell per sidebar tab" );

/** Make the sidebar's windows if they are not there, or ControlBarParent was made again since, and put
	* each on its cell: tabs[], cameos[], arrowUp, arrowDown.  Called from ControlBar::layoutSidebar on
	* every layout (start, resize, scheme change, rebuild after Options' Accept), so it is safe to call
	* any number of times.  Shows them. */
void Sidebar_layout( const HudLayoutRects &rects );

/** Once a client frame from ControlBar::update while the sidebar layout is on: refills the selected
	* tab's cells (what the player can build, greyed when he cannot), their build clocks and counts.
	* Outside the render window; reads the logic the way the bar's other updates do. */
void Sidebar_update( void );

/** Hide every sidebar window: the Classic layout, an observer, a replay, a defeated player. */
void Sidebar_hide( void );

/** Show tab `tab` (SidebarTab) from its first row.  The tab buttons and the Q W E R keys
	* (MSG_META_TOGGLE_COMMAND_TAB_* in sidebar mode) land here.  An unknown tab does nothing. */
void Sidebar_selectTab( Int tab );

/** The tab shown, SIDEBAR_TAB_BUILDINGS until one is picked. */
Int Sidebar_selectedTab( void );

/** Move the selected tab's cameos by `rows` rows, negative up: the arrows and the wheel over the grid.
	* Kept in range by Sidebar_scroll. */
void Sidebar_scrollBy( Int rows );

/** Pure: the first row shown when `count` cameos fill `cols` a row in a window of `rows` rows and the
	* player asked for `row`, kept between 0 and the last row that still fills the window. */
Int Sidebar_scroll( Int count, Int cols, Int rows, Int row );

#endif // __CONTROLBARSIDEBAR_H__
