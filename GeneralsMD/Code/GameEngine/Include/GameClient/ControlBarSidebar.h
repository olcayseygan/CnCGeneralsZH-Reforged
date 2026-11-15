// The Red Alert style sidebar (HudLayout 1, docs/plans/HUD-SIDEBAR-LAYOUT.md): four production tabs,
// a 2x6 grid of build cameos that scrolls two to a row, and the two arrows under it, made in code as
// children of ControlBarParent.  ControlBar::layoutSidebar places the bar's own windows (radar, money,
// the strip's ButtonCommand01..14) and then hands the rest here; every rectangle comes from
// HudLayoutForScreen (GameClient/Display.h), nothing here is a pixel of its own.  Client only: the logic
// hears of a cameo only through the messages a click sends (MSG_QUEUE_UNIT_FOR_GROUP, structure
// placement, MSG_CANCEL_UNIT_CREATE, and Sat's and Onar's MSG_SELL and MSG_DO_REPAIR).
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#ifndef __CONTROLBARSIDEBAR_H__
#define __CONTROLBARSIDEBAR_H__

#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "GameClient/Display.h"

class Image;

/** The sidebar art for the local player's side: the MappedImage `<Prefix>Sidebar<piece>`, Prefix
	* AmericaPro, ChinaPro (Boss too), GLAPro or TurkiyePro, AmericaPro for a side it does not know; NULL
	* when that side has no such piece.  Looked up once a piece and side, then kept.  `piece` is the name after
	* "Sidebar": "TabBuildingsH", "CameoWell", "Plate". */
const Image *Sidebar_image( const char *piece );

/** The builder a structure on the cursor was put there for by the sidebar's builder chip, while it is
	* still that placement; INVALID_ID otherwise.  PlaceEventTranslator appends it to MSG_DOZER_CONSTRUCT
	* (and _LINE) so that builder takes the job, selected or not. */
ObjectID Sidebar_placementBuilder( void );

/** The tabs, left to right, keyed Q W E R; GUI:SidebarTab0..3 name them. */
enum SidebarTab
{
	SIDEBAR_TAB_BUILDINGS,		///< Bina: what the builders put up, less base defences
	SIDEBAR_TAB_DEFENSE,			///< Savunma: KINDOF_FS_BASE_DEFENSE structures, then every research the player's buildings offer
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

/** Sat and Onar under the radar: each puts the pointer into a mode, RA2 style, in which a left click on
	* one of the player's own structures sells it (MSG_SELL with its ObjectID) or sends a builder the
	* logic picks to repair it (MSG_DO_REPAIR with TRUE after the ObjectID).  The mode stays for the next
	* click, until a right click, Esc, the button again, or a placement or targeting command taking over. */
enum SidebarCursorMode
{
	SIDEBAR_CURSOR_NONE,
	SIDEBAR_CURSOR_SELL,
	SIDEBAR_CURSOR_REPAIR
};

/** What a press on Sat does. */
enum SidebarSellPress
{
	SIDEBAR_SELL_PRESS_MODE_OFF,		///< the sell cursor was on: off
	SIDEBAR_SELL_PRESS_SELECTED,		///< own sellable structures selected: sold now, no cursor
	SIDEBAR_SELL_PRESS_MODE_ON			///< otherwise the sell cursor
};

/** Pure: a press on Sat with the sell cursor `modeOn` and `sellableSelected` of the player's own
	* structures the bar would sell selected. */
Int Sidebar_sellPress( Bool modeOn, Bool sellableSelected );

Int Sidebar_cursorMode( void );		///< SidebarCursorMode

/** Turn the mode off; TRUE when one was on (Esc takes it back before opening the menu). */
Bool Sidebar_cancelCursorMode( void );

/** Sat's and Onar's press from a key (MSG_META_SELL_MODE, MSG_META_REPAIR_MODE), in any layout: the
	* sell cursor, or the selection sold, or the mode off; the repair cursor on or off. */
void Sidebar_pressSell( void );
void Sidebar_pressRepair( void );

class GameMessage;
/** GUICommandTranslator's first look at every message: TRUE when the mode used it (destroy it).  The
	* left button is the mode's while it is on; the right release ends it (Sidebar_takeRightRelease). */
Bool Sidebar_translate( const GameMessage *msg );

/** SelectionXlat's right release: TRUE once when that release ended the mode, so it neither deselects
	* nor cancels anything else. */
Bool Sidebar_takeRightRelease( void );

/** HintSpy, after the hint set the pointer: the mode's cursor over the world, the valid one over a
	* structure the click would act on, the invalid one elsewhere.  Nothing with no mode on. */
void Sidebar_pointCursor( void );

#endif // __CONTROLBARSIDEBAR_H__
