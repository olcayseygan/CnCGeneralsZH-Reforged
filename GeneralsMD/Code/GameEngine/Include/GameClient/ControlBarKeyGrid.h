// The Key Grid layout (HUD_LAYOUT_GRID, the owner's of 2026-11-27): the Reforged bar's commands as three
// staggered keyboard rows of six on a deck beside a radar box cut to the map's own shape, every place
// pressed by its letter.  This header is the pure half: where everything stands for a screen and a map
// (KeyGridLayoutForScreen), which button takes which place (ControlBar_keyGridPlaces) and the squad
// tray's cells.  ControlBarKeyGrid.cpp holds both halves; ControlBar::layoutKeyGrid moves ControlBar.wnd's
// windows onto these rectangles.  Client only.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Lib/BaseType.h"
#include "GameClient/ControlBar.h"

//-------------------------------------------------------------------------------------------------
/** The radar's glass at 1080 for a map of `aspect` (width over height): today's glass area, 322x232,
	* in the map's own shape, so the picture fills it with no letterbox.  The aspect is held to 1:2..2:1,
	* past which the glass would leave the console (ponytail: a stranger map letterboxes). */
//-------------------------------------------------------------------------------------------------
void KeyGrid_glassForAspect( Real aspect, Int *width, Int *height );

enum { KEY_GRID_SHELF_KEYS = 4 };			///< Options, Beacon, Worker, Chat, left to right on the shelf
enum { KEY_GRID_SIGNAL_KEYS = 3 };		///< the smoke signal keys at the shelf's left

/// the reference screen the mockups are drawn at; every rectangle below is these pixels times `scale`
#define KEY_GRID_DESIGN_W 1920.0f
#define KEY_GRID_DESIGN_H 1080.0f

//-------------------------------------------------------------------------------------------------
/** Where Key Grid puts everything on one screen, in screen pixels.  The left console stands on the
	* HudWidth area's left edge and the screen's foot: the radar box round the glass, the shelf on the box
	* with the signal keys and the four tool keys, the spine standing out of the box's right side with the
	* power meter sunk in it and the money head on it, and the deck of three rows hanging off the spine.
	* The right side (portrait, rank, minimise and General keys) is the Reforged bar's and not here.
	* The owner's V21 mockups at 1920x1080; Tools/hud_key_grid.py's rects() and compose() hold the same numbers. */
//-------------------------------------------------------------------------------------------------
struct KeyGridRects
{
	Real scale;															///< 1080 pixels to screen pixels: ControlBarUniformScale() / 1.8
	IRegion2D glass;												///< the radar's picture, the map's shape: 5,1064-h .. 5+w,1064
	IRegion2D box;													///< the radar box round it, from the area's left edge to the foot
	IRegion2D shelf;												///< the plate on the box: 0,top .. max(box, 345),box top + 2
	IRegion2D signals;											///< the three signal keys' strip at the shelf's left
	IRegion2D signalKeys[ KEY_GRID_SIGNAL_KEYS ];	///< 40x30 from x 11, 44 apart, the shelf's top + 10: attack, defend, attention
	IRegion2D shelfKeys[ KEY_GRID_SHELF_KEYS ];	///< 44x34 from x 156, 46 apart: Options, Beacon, Worker, Chat
	IRegion2D spine;												///< 40 wide out of the box's right side, the shelf's top + 24 to the foot
	IRegion2D deckSwitch;										///< the squad tray switch, 36x34: on the spine at y 742 where the box is low
																					///< enough (its top at 740 or above), else on the head's end left of the money
	IRegion2D head;													///< the money head on the spine: y 780..822
	IRegion2D money;												///< the money well, 152x28; the readout stands 25 in, after the painted "$"
	IRegion2D meter;												///< the power meter sunk in the spine, 10 wide, from under the head to 1066
	IRegion2D deck;													///< the deck's art: the spine's left at the head's top to the Z row's right at the foot
	IRegion2D rows;													///< the three rows' backs, the click blocker over the keys
	IRegion2D cells[ COMMAND_PLACE_COUNT ];	///< 90x72, 100 apart, rows 88 apart from y 830, A in by 25 and Z by 75
};

/** Fill `out` for a screen, the HudWidth `area` on it, a scale and the map's aspect (0 or less is
	* today's glass, 322x232).  Pure. */
void KeyGridLayoutForScreen( Int screenWidth, Int screenHeight, const IRegion2D &area, Real scale, Real mapAspect,
	KeyGridRects *out );

/** A place's letter, Q W E R T Y A S D F G H Z X C V B N in CommandPlace order; 0 off the grid. */
inline WideChar ControlBar_commandPlaceKey( Int place )
{
	return place >= 0 && place < COMMAND_PLACE_COUNT ? (WideChar)"QWERTYASDFGHZXCVBN"[ place ] : 0;
}

//-------------------------------------------------------------------------------------------------
/** What a builder's structure is for, on Key Grid's rows (order.py's role()).  A fake reads as the
	* building it imitates. */
//-------------------------------------------------------------------------------------------------
enum KeyGridRole
{
	KEY_GRID_ROLE_OTHER = 0,	///< no structure: an order, an upgrade, a power, a switch
	KEY_GRID_ROLE_ECONOMY,		///< power or supply (FS_POWER, FS_SUPPLY_CENTER)
	KEY_GRID_ROLE_BUILD,			///< a factory, barracks, war factory or airfield
	KEY_GRID_ROLE_EXTRA,			///< any other structure: tech, drop zone, superweapon, the rest
	KEY_GRID_ROLE_DEFENSE,		///< a base defense or a demo trap
	KEY_GRID_ROLE_COMMAND_CENTER
};

/** One command slot as ControlBar_keyGridPlaces reads it. */
struct KeyGridSlot
{
	Int type;			///< GUI_COMMAND_*, GUI_COMMAND_NONE for an empty slot
	Int group;		///< CommandGroup, for a set that builds nothing (today's rule)
	Int pinned;		///< ControlBar_namedCommandPlace, or -1
	Int role;			///< KeyGridRole
	Int depth;		///< the structure's prerequisite depth (0 for none)
	Int refs;			///< how many objects name the structure in their Prerequisites; 0 for a superweapon
	Int mirror;		///< the place the real building of a GLA fake has on the real set, or -1
};

/** The passengers' places on a set that builds nothing: along the Z row from N leftward, then along
	* the A row from H leftward, round whatever holds a key there already. */
extern const Int KEY_GRID_RIDER_PLACES[ 11 ];

//-------------------------------------------------------------------------------------------------
/** Where each of `count` slots stands under Key Grid, `places` a CommandPlace or -1 for each; TRUE
	* when the set attack-moves, so A, C and V stay free for force attack, hold position and move.
	*
	* A set that builds structures (order.py, the owner's of 2026-11-27): the pinned and fixed buttons
	* first (Disarm Mines N, the GLA switch H, the Demo charge B, stop S...), a GLA fake on its real
	* building's place, then the Q row the build line: economy, then production, each by prerequisite
	* depth and then slot; the Z row the tech building (the structure most named in Prerequisites, a
	* superweapon never) and the other structures after it; the A row the defenses; the command center
	* the last free key of the row the build line ended in; anything else the last free place.  A row that
	* fills goes on in the next row the order names.
	*
	* A set that builds nothing: ControlBar_commandPlaces for everything but the passengers, then the
	* passengers on KEY_GRID_RIDER_PLACES. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar_keyGridPlaces( const KeyGridSlot *slots, Int count, Int *places );

//-------------------------------------------------------------------------------------------------
/** The squad tray on the deck (GroupPanel): `count` entries on the grid's places in reading order,
	* Q W E R T Y, A..H, Z..N, -1 past the eighteenth.  The place of entry `index`. */
//-------------------------------------------------------------------------------------------------
inline Int KeyGrid_trayPlace( Int index )
{
	return index >= 0 && index < COMMAND_PLACE_COUNT ? index : -1;
}
