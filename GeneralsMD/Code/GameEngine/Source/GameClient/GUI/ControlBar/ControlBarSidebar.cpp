// The Red Alert style sidebar (ControlBarSidebar.h, docs/plans/HUD-SIDEBAR-LAYOUT.md lane C): four
// tabs, the cameo grid (HUD_SIDEBAR_CAMEO_COLUMNS a row) and two arrows, all in one box window that
// is a child of ControlBarParent and stays the first of its children, so it draws over
// BackgroundMarker's steel and takes the pointer before the sidebar's blocker.
//
// Bina lists every structure the player's builders put up (GroupPanel_gatherConstructible), base
// defences included; the powers tab the powers tray's buttons (his bought general's powers and his
// superweapons, fired through ControlBar::processCommandUI as the tray fires them) and every research
// his buildings offer; Piyade and Araç what his factories make (GroupPanel_gatherBuildable), split on
// KINDOF_INFANTRY, aircraft under Araç.  Only what is unlocked shows (prerequisites, sciences and
// upgrades met; too dear still shows, greyed), grouped by country, his own first, in the order each
// item first showed (Sidebar_showOrder).  A left click on a unit queues it with no squad, through
// the group panel's MSG_QUEUE_UNIT_FOR_GROUP, at the lit factory chips (several share the batch) or
// the logic's pick under Tümü; on a structure it puts the structure on the cursor for a lit builder
// that offers it, or one the bar would use, as the bar's own button does.  A right click takes back
// the units queued deepest in their factories' queues, as many as the modifiers would have queued.
//
// Sat and Onar under the radar put the pointer into a sell or repair mode; GUICommandTranslator hands
// the mode the mouse first (Sidebar_translate), HintSpy lets it have the last word on the cursor.
//
// Thread: LOGIC-THREAD.md.  The logic is read in Sidebar_update and on the input path only, both
// outside the render window; the windows' state is set there and the draw callbacks read nothing
// but the windows.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/ControlBarSidebar.h"

#include "Common/ActionManager.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GroupPanel.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/LookAtXlat.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"

#include <algorithm>
#include <vector>

namespace
{
	const Color CLOCK_COLOR		= GameMakeColor( 0, 0, 0, 190 );			///< darker than the queue's 160: a 140 wide cameo
	const Color ARROW_LIT			= GameMakeColor( 220, 228, 236, 255 );
	const Color ARROW_DARK		= GameMakeColor( 90, 96, 104, 255 );
	const Color TAB_TEXT_EDGE	= GameMakeColor( 0, 0, 0, 255 );
	const Color KEY_LETTER		= GameMakeColor( 255, 210, 60, 255 );	///< the bar's hotkey letters' yellow
	const Real KEY_POINTS			= 10.0f;	///< every key letter on the sidebar and the strip, at 1080: a 15 px line, a 10 px cap
	const Color KEY_PLATE			= GameMakeColor( 2, 4, 5, 230 );		///< the key badge's plate, the Classic badge's
	const Color KEY_PLATE_RIM	= GameMakeColor( 90, 90, 90, 255 );	///< and its one pixel rim
	/// where a key badge stands, at 1080: this far in from the window's top left.  A tab's face starts
	/// 4.7 in (hud_sidebar.py TAB_FACE), so its badge sits a pixel inside the face, clear of the bezel; a
	/// command button is its picture, so its badge is that pixel inside the picture's own border
	const Real KEY_INSET_TAB			= 6.0f;
	const Real KEY_INSET_BUTTON		= 2.0f;
	GameFont *s_keyFont = NULL;		///< KEY_POINTS at the layout's k, set by Sidebar_layout
	Real s_keyScale = 1.0f;				///< and that k
	const Int ICON_INSET			= 3;			///< the icon kept this far inside the bevel

	// the tab body: flat steel with a 1 px bevel, the group panel's stand-in colours; the open tab is
	// lit blue and pressed in (its bevel turned over), a hovered one a step lighter
	const Color TAB_STEEL			= GameMakeColor( 44, 52, 60, 255 );
	const Color TAB_STEEL_HOVER	= GameMakeColor( 58, 68, 79, 255 );
	const Color TAB_STEEL_OPEN	= GameMakeColor( 52, 78, 128, 255 );
	const Color TAB_BEVEL_LIGHT	= GameMakeColor( 140, 152, 166, 255 );
	const Color TAB_BEVEL_DARK	= GameMakeColor( 5, 7, 9, 255 );
	const Color TAB_ICON_DIM		= GameMakeColor( 190, 198, 206, 255 );	///< a closed tab's icon, tinted down

	/// Each tab's icon, a "Pro" piece under the side's prefix (<S><piece>, America's when the side has
	/// none).  Stand-ins borrowed from the bar until the per-side tab icons exist: the buildings tab's
	/// pictogram, the defend signal's shield, the idle infantry and idle vehicle buttons.  Swap the
	/// names here for the new pieces.
	const char *const TAB_ICONS[ SIDEBAR_TAB_COUNT ] =
	{
		"ProTabBuildings", "ProSignalDefend", "ProIdleInfantry", "ProIdleVehicle"
	};

	const char *const TAB_LABELS[ SIDEBAR_TAB_COUNT ] =
	{
		"GUI:SidebarTab0", "GUI:SidebarTab1", "GUI:SidebarTab2", "GUI:SidebarTab3"
	};
	/// the key each tab answers to, for its tooltip: by place, Ctrl with Q W E R (CommandXlat.cpp maps
	/// them back)
	const Int TAB_METAS[ SIDEBAR_TAB_COUNT ] =
	{
		GameMessage::MSG_META_TOGGLE_COMMAND_TAB_BUILDINGS, GameMessage::MSG_META_TOGGLE_COMMAND_TAB_GROUND,
		GameMessage::MSG_META_TOGGLE_COMMAND_TAB_AIRCRAFT, GameMessage::MSG_META_TOGGLE_COMMAND_TAB_DEFENSE
	};

	typedef GroupPanelBuildable Buildable;

	Int s_tab = SIDEBAR_TAB_BUILDINGS;
	Int s_row = 0;

	std::vector< Buildable > s_list;		///< the shown tab's cameos
	std::vector< Buildable > s_all;			///< scratch for the gather
	std::vector< Int > s_count;					///< per s_list entry: units queued over every factory
	std::vector< Int > s_clock;					///< per s_list entry: the furthest head of a queue, -1 for none
	std::vector< ObjectID > s_producer;		///< per s_list entry: a research's building to queue it at, else INVALID_ID
	std::vector< ObjectID > s_researcher;	///< per s_list entry: the building a research is under way at, else INVALID_ID
	GameWinDrawFunc s_cameoDraw = NULL;		///< the image push button's draw, under a research cell's badge
	const Color BADGE_EDGE = GameMakeColor( 0, 0, 0, 220 );

	// the show order (Sidebar_showOrder): each tab's items in the order they first showed, and the
	// countries in the order the player gained them, his own first.  The client's own memory, kept
	// for a match: a new bar (a new match, a load, Options' Accept) or the logic's clock running back
	// starts it again from what is unlocked then, in the command sets' order
	std::vector< const void * > s_seen[ SIDEBAR_TAB_COUNT ];
	std::vector< AsciiString > s_countries;
	UnsignedInt s_seenFrame = 0;					///< the logic frame the other tabs were last looked at, 0 for not yet
	Player *s_orderPlayer = NULL;					///< the player the order is for
	std::vector< Int > s_countryOf;				///< scratch: per gathered item
	std::vector< Int > s_seenAt;
	std::vector< Int > s_showOrder;
	std::vector< Buildable > s_gathered;
	std::vector< ObjectID > s_gatheredProducer;
	Bool s_listed = FALSE;
	Bool s_dirty = TRUE;
	UnsignedInt s_listFrame = 0;
	Player *s_listPlayer = NULL;
	Int s_loggedTab = -1, s_loggedCount = -1;

	Bool s_shown = FALSE;								///< Sidebar_layout showed the box and Sidebar_hide has not hidden it
	GameWindow *s_box = NULL;
	GameWindow *s_tabs[ SIDEBAR_TAB_COUNT ];
	GameWindow *s_cameos[ HUD_SIDEBAR_CAMEOS ];
	GameWindow *s_arrowUp = NULL;
	GameWindow *s_arrowDown = NULL;
	GameWinDrawFunc s_buttonDraw = NULL;	///< the plain push button's draw, under the arrows' chevrons
	const Image *s_tabIcon[ SIDEBAR_TAB_COUNT ];		///< TAB_ICONS for the local player's side, found in Sidebar_update
	Player *s_iconPlayer = NULL;
	DisplayString *s_keyText[ SIDEBAR_TAB_COUNT ];	///< each tab's key letter, set in Sidebar_update
	UnicodeString s_keyShown[ SIDEBAR_TAB_COUNT ];
	IRegion2D s_cells[ HUD_SIDEBAR_CAMEOS ];				///< the cameo cells on screen, the wells drawn under them
	IRegion2D s_cameoRoom[ HUD_SIDEBAR_CAMEOS ];		///< each cell less its inset: the most a cameo window takes

	/// the tabs' pieces, <Prefix>SidebarTab<Kind>{,H,P,I}: bezel and glyph baked in
	const char *const TAB_KINDS[ SIDEBAR_TAB_COUNT ] = { "Buildings", "Defenses", "Infantry", "Vehicles" };
	const Color KEY_LETTER_LIT	= GameMakeColor( 255, 255, 255, 255 );	///< the key letter on a lit tab piece, over its dark edge
	const Real CAMEO_INSET		= 3.0f;		///< the cameo inside its well, at 1080

	/// Sidebar_image's finds for the side they were made for, misses included
	struct SidebarPiece
	{
		AsciiString piece;
		const Image *image;
	};
	AsciiString s_imageSide;
	std::vector< SidebarPiece > s_images;

	// the factory picker under the tabs, on the unit tabs only: "Tümü", then one chip a factory
	const Real CHIP_POINTS			= 9.0f;		///< the chips' text, at 1080
	const Color CHIP_TEXT				= GameMakeColor( 255, 255, 255, 255 );
	std::vector< ObjectID > s_picks[ SIDEBAR_TAB_COUNT ];	///< each tab's lit factories or builders, by ObjectID up; none for Tümü
	std::vector< ObjectID > s_factories;		///< the shown unit tab's factories, by ObjectID up, all of them
	GameWindow *s_chipAll = NULL;
	GameWindow *s_chips[ HUD_SIDEBAR_PICKER_CHIPS ];
	DisplayString *s_chipText[ HUD_SIDEBAR_PICKER_CHIPS + 1 ];	///< [0] "Tümü", then the chips' numbers
	const Image *s_chipIcon[ HUD_SIDEBAR_PICKER_CHIPS ];				///< each chip's building picture, set in Sidebar_update
	Bool s_chipGlyph[ HUD_SIDEBAR_PICKER_CHIPS ];								///< the picture is a ChipIcon glyph, drawn white over a dark edge
	UnicodeString s_chipName[ HUD_SIDEBAR_PICKER_CHIPS ];				///< each chip's building or builder, its tooltip
	std::vector< ObjectID > s_placingBuilders;	///< the lit builders a structure on the cursor was put there for, [0] its source
	std::vector< ObjectID > s_namedBuilders;		///< each builder Sidebar_placementBuilder named in this placement, once a job: busy before the logic hears of it
	std::vector< ObjectID > s_followed;				///< the player's producers and builders in the selection the sidebar last followed
	const Color GLYPH_EDGE = GameMakeColor( 0, 0, 0, 200 );
	const Int CHIP_PICTURE_INSET = 2;															///< a factory's picture inside its chip's edge, at 1080 (a 28 high chip)
	const Color CHIP_PICTURE_DIM = GameMakeColor( 170, 170, 170, 255 );		///< a chip not picked, its picture a shade down
	const Color CHIP_BADGE = GameMakeColor( 0, 0, 0, 190 );								///< behind the chip's number

	// Sat and Onar: their own box over the two seats under the radar, beside ButtonGeneral
	GameWindow *s_keyBox = NULL;
	GameWindow *s_sellKey = NULL;
	GameWindow *s_repairKey = NULL;
	DisplayString *s_keyLabel[ 2 ];			///< [0] Sat, [1] Onar, on the steel stand-in only
	Int s_cursorMode = SIDEBAR_CURSOR_NONE;
	Bool s_eatRightClick = FALSE;				///< the right release ended the mode; its click gives no order
	Bool s_rightReleaseTaken = FALSE;		///< and SelectionXlat leaves that release alone
}

//-------------------------------------------------------------------------------------------------
const Image *Sidebar_image( const char *piece )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( piece == NULL || player == NULL || TheMappedImageCollection == NULL )
		return NULL;
	const AsciiString side = player->getSide();
	if( side != s_imageSide )
	{
		s_imageSide = side;
		s_images.clear();
	}
	for( size_t i = 0; i < s_images.size(); i++ )
		if( s_images[ i ].piece.compare( piece ) == 0 )
			return s_images[ i ].image;

	const Bool gla = side.startsWith( "GLA" );
	const char *prefix = side.startsWith( "China" ) || side.startsWith( "Boss" ) ? "ChinaPro"
		: gla ? "GLAPro" : side.startsWith( "Turkiye" ) ? "TurkiyePro" : "AmericaPro";
	AsciiString name;
	name.format( "%sSidebar%s", prefix, piece );
	const Image *image = TheMappedImageCollection->findImageByName( name );
	// the bar's other GLA pieces are spelled "Gla" (GlaProTray)
	if( image == NULL && gla )
	{
		name.format( "GlaProSidebar%s", piece );
		image = TheMappedImageCollection->findImageByName( name );
	}
	SidebarPiece found;
	found.piece = piece;
	found.image = image;
	s_images.push_back( found );
	return image;
}

/// `base` in the state a button is drawn in (I disabled, P pushed or lit, H hovered), the plain piece
/// when the side has no picture for that state, NULL when it has neither
static const Image *statePiece( const char *base, Bool enabled, Bool pushed, Bool hover )
{
	AsciiString piece;
	piece.format( "%s%s", base, !enabled ? "I" : pushed ? "P" : hover ? "H" : "" );
	const Image *image = Sidebar_image( piece.str() );
	return image != NULL ? image : Sidebar_image( base );
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

//-------------------------------------------------------------------------------------------------
Int PowerRail_state( Int production, Int consumption, Int yellowRange )
{
	if( consumption <= 0 )
		return POWER_RAIL_GREEN;
	if( consumption > production )
		return POWER_RAIL_RED;
	return consumption > production - yellowRange ? POWER_RAIL_YELLOW : POWER_RAIL_GREEN;
}

//-------------------------------------------------------------------------------------------------
Int PowerRail_blocks( Int amount, Int production, Int consumption, Int blocks )
{
	const Int larger = MAX( production, consumption );
	if( amount <= 0 || larger <= 0 || blocks <= 0 )
		return 0;
	// amount / ( larger * 5 / 4 ) of the rail, to the nearest block
	const Int lit = (Int)( ( (Int64)amount * blocks * 8 + larger * 5 ) / ( (Int64)larger * 10 ) );
	return MIN( MAX( lit, 1 ), blocks );
}

//-------------------------------------------------------------------------------------------------
// The windows: made on the first layout and again whenever the window manager has thrown them away
// with ControlBarParent.
//-------------------------------------------------------------------------------------------------
static Int boxId( void )
{
	static const Int id = (Int)TheNameKeyGenerator->nameToKey( "ControlBarSidebar:Box" );
	return id;
}

static Bool boxAlive( void )
{
	return s_box != NULL && TheWindowManager->winGetWindowFromId( NULL, boxId() ) == s_box;
}

static GameWindow *barParent( void )
{
	static NameKeyType parentKey = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
	return TheWindowManager->winGetWindowFromId( NULL, parentKey );
}

static void buildTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	TheControlBar->showBuildTooltipLayout( window );
}

/// the side's arrow piece; without one the plain button with a chevron on it, lit while there is
/// somewhere to go
static void drawArrow( GameWindow *window, WinInstanceData *instData )
{
	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	const Bool enabled = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED );
	const Image *art = statePiece( window == s_arrowUp ? "ArrowUp" : "ArrowDown", enabled,
		BitTest( instData->getState(), WIN_STATE_SELECTED ), BitTest( instData->getState(), WIN_STATE_HILITED ) );
	if( art != NULL )
	{
		TheDisplay->drawImage( art, at.x, at.y, at.x + size.x, at.y + size.y );
		return;
	}

	if( s_buttonDraw != NULL )
		s_buttonDraw( window, instData );
	const Int half = max( size.y / 5, 2 );
	const Int cx = at.x + size.x / 2, cy = at.y + size.y / 2;
	const Bool up = window == s_arrowUp;
	const Int tip = up ? cy - half : cy + half, back = up ? cy + half : cy - half;
	const Color c = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) ? ARROW_LIT : ARROW_DARK;
	TheDisplay->drawLine( cx - 2 * half, back, cx, tip, 2.0f, c );
	TheDisplay->drawLine( cx, tip, cx + 2 * half, back, 2.0f, c );
}

/// the stand-in body of a tab or a chip: steel with a 1 px bevel, raised, or pressed in and lit
static void drawSteel( const ICoord2D &at, const ICoord2D &size, Bool lit, Bool hover )
{
	const Color face = lit ? TAB_STEEL_OPEN : hover ? TAB_STEEL_HOVER : TAB_STEEL;
	const Color light = lit ? TAB_BEVEL_DARK : TAB_BEVEL_LIGHT;
	const Color shade = lit ? TAB_BEVEL_LIGHT : TAB_BEVEL_DARK;
	TheDisplay->drawFillRect( at.x, at.y, size.x, size.y, face );
	TheDisplay->drawFillRect( at.x, at.y, size.x, 1, light );
	TheDisplay->drawFillRect( at.x, at.y + 1, 1, size.y - 1, light );
	TheDisplay->drawFillRect( at.x + 1, at.y + size.y - 1, size.x - 1, 1, shade );
	TheDisplay->drawFillRect( at.x + size.x - 1, at.y + 1, 1, size.y - 2, shade );
}

/// the badge's plate for `letters` characters measuring `textW` x `textH`: a square of the line's
/// height, so every single letter, I or W, stands on the same plate (a bold W is narrower than the
/// line, though getSize's overlap can say otherwise); a longer text (a chord's name) widens it.  Never
/// wider than `roomW` nor taller than `roomH`
static void keyPlateSize( Int letters, Int textW, Int textH, Int roomW, Int roomH, Int *plateW, Int *plateH )
{
	*plateH = MIN( textH, roomH );
	*plateW = MIN( letters > 1 ? MAX( *plateH, textW + 4 ) : *plateH, roomW );
}

//-------------------------------------------------------------------------------------------------
GameFont *Sidebar_keyFont( void )
{
	return s_shown ? s_keyFont : NULL;
}

//-------------------------------------------------------------------------------------------------
void Sidebar_drawKeyBadge( DisplayString *letter, const IRegion2D &window, Real designInset, Color color )
{
	if( letter == NULL )
		return;
	Int textW = 0, textH = 0;
	letter->getSize( &textW, &textH );
	const Int inset = MAX( REAL_TO_INT( designInset * s_keyScale + 0.5f ), 1 );
	const Int roomW = window.width() - 2 * inset, roomH = window.height() - 2 * inset;
	if( textW <= 0 || textH <= 0 || roomW <= 2 || roomH <= 2 )
		return;
	Int plateW = 0, plateH = 0;
	keyPlateSize( letter->getTextLength(), textW, textH, roomW, roomH, &plateW, &plateH );
	const Int left = window.lo.x + inset, top = window.lo.y + inset;
	TheDisplay->drawFillRect( left, top, plateW, plateH, KEY_PLATE );
	TheDisplay->drawOpenRect( left, top, plateW, plateH, 1, KEY_PLATE_RIM );
	// the line box centred on the plate: Arial's caps sit within a pixel of the line's middle
	IRegion2D plate = { { left, top }, { left + plateW, top + plateH } };
	letter->setClipRegion( &plate );
	letter->draw( left + ( plateW - textW ) / 2, top + ( plateH - textH ) / 2, color, TAB_TEXT_EDGE );
}

/// tab `t`'s key: the sidebar's one badge, yellow, white on the open tab
static void drawKeyBadge( Int t, const ICoord2D &at, const ICoord2D &size, Bool open )
{
	IRegion2D window = { { at.x, at.y }, { at.x + size.x, at.y + size.y } };
	Sidebar_drawKeyBadge( s_keyText[ t ], window, KEY_INSET_TAB, open ? KEY_LETTER_LIT : KEY_LETTER );
}

static void drawTab( GameWindow *window, WinInstanceData *instData )
{
	const Int t = (Int)(intptr_t)GadgetButtonGetData( window ) - 1;
	if( t < 0 || t >= SIDEBAR_TAB_COUNT )
		return;
	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	if( size.x < 3 || size.y < 3 )
		return;

	// the open tab, and one held down under the button, take the lit, pressed-in look
	const Bool open = t == s_tab || BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
	const Bool keyed = s_keyText[ t ] != NULL && !s_keyShown[ t ].isEmpty();

	// the side's baked tab: bezel and glyph in the piece, P the open tab's lit look; the key on top,
	// dark on the lit piece
	AsciiString base;
	base.format( "Tab%s", TAB_KINDS[ t ] );
	const Image *art = statePiece( base.str(), BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ), open, hover );
	if( art != NULL )
	{
		TheDisplay->drawImage( art, at.x, at.y, at.x + size.x, at.y + size.y );
		if( keyed )
			drawKeyBadge( t, at, size, open );
		return;
	}

	// the stand-in until the side has its pieces
	drawSteel( at, size, open, hover );

	// pressed in, the face's contents sit a pixel down and right
	const Int push = open ? 1 : 0;
	// the icon centred in the face, as large as fits with its own shape kept
	AsciiString glyph;
	glyph.format( "TabIcon%s", TAB_KINDS[ t ] );
	const Image *icon = Sidebar_image( glyph.str() );
	if( icon == NULL )
		icon = s_tabIcon[ t ];
	const Int roomW = size.x - 2 * ( 1 + ICON_INSET ), roomH = size.y - 2 * ( 1 + ICON_INSET );
	if( icon != NULL && icon->getImageWidth() > 0 && icon->getImageHeight() > 0 && roomW > 0 && roomH > 0 )
	{
		const Real fit = min( (Real)roomW / icon->getImageWidth(), (Real)roomH / icon->getImageHeight() );
		const Int w = max( REAL_TO_INT( icon->getImageWidth() * fit ), 1 );
		const Int h = max( REAL_TO_INT( icon->getImageHeight() * fit ), 1 );
		// centred, but moved right off the key badge in the top left as far as the face allows
		Int textW = 0, textH = 0, plateW = 0, plateH = 0;
		const Int inset = MAX( REAL_TO_INT( KEY_INSET_TAB * s_keyScale + 0.5f ), 1 );
		if( keyed )
		{
			s_keyText[ t ]->getSize( &textW, &textH );
			keyPlateSize( s_keyText[ t ]->getTextLength(), textW, textH, size.x - 2 * inset, size.y - 2 * inset, &plateW, &plateH );
		}
		const Int badgeRight = keyed ? inset + plateW + 1 : 0;
		Int x = max( ( size.x - w ) / 2, badgeRight );
		x = at.x + min( x, size.x - 2 - w ) + push;
		const Int y = at.y + ( size.y - h ) / 2 + push;
		TheDisplay->drawImage( icon, x, y, x + w, y + h, open ? 0xFFFFFFFF : TAB_ICON_DIM );
	}
	if( keyed )
	{
		const ICoord2D pushed = { at.x + push, at.y + push };
		drawKeyBadge( t, pushed, size, open );
	}
}

/// whether the shown tab's picker has `id` lit: a factory or builder in the lit set, or Tümü
/// (INVALID_ID) when the set is empty
static Bool chipLit( ObjectID id )
{
	const std::vector< ObjectID > &picks = s_picks[ s_tab ];
	return id == INVALID_ID ? picks.empty() : std::binary_search( picks.begin(), picks.end(), id );
}

/// a picker chip: the side's Chip, P on every lit chip and while held down, else the steel stand-in;
/// its text in the middle, dark on the lit piece
static void drawChip( GameWindow *window, WinInstanceData *instData )
{
	Int index = -1;
	if( window != s_chipAll )
	{
		for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
			if( s_chips[ i ] == window )
				index = i;
		if( index < 0 )
			return;
	}
	const ObjectID id = index < 0 || index >= (Int)s_factories.size() ? INVALID_ID : s_factories[ index ];
	const Bool lit = chipLit( id );
	const Bool pushed = lit || BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );

	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	if( size.x < 3 || size.y < 3 )
		return;
	// every chip, Tümü too, on the same piece at the same size
	const Image *art = statePiece( "Chip", TRUE, pushed, hover );
	if( art != NULL )
		TheDisplay->drawImage( art, at.x, at.y, at.x + size.x, at.y + size.y );
	else
		drawSteel( at, size, pushed, hover );
	if( lit )
	{
		// the pick's frame, inside the chip's own rect as fills (a 2 px line would straddle the edge);
		// a picture sits 2 px in and leaves it showing
		TheDisplay->drawFillRect( at.x, at.y, size.x, 2, KEY_LETTER );
		TheDisplay->drawFillRect( at.x, at.y + size.y - 2, size.x, 2, KEY_LETTER );
		TheDisplay->drawFillRect( at.x, at.y + 2, 2, size.y - 4, KEY_LETTER );
		TheDisplay->drawFillRect( at.x + size.x - 2, at.y + 2, 2, size.y - 4, KEY_LETTER );
	}

	DisplayString *text = s_chipText[ index + 1 ];
	const Image *icon = index >= 0 ? s_chipIcon[ index ] : NULL;
	const Int push = pushed && art == NULL ? 1 : 0;
	Int textW = 0, textH = 0;
	if( text != NULL )
		text->getSize( &textW, &textH );

	// a factory chip with its building's own picture: the picture fills the chip 2 px in, shape kept
	// and cut at the chip's edges rather than shrunk; the number a badge in the bottom right corner on
	// a dark backing; the pick's chip in a lit frame, the others' pictures a shade darker
	if( index >= 0 && icon != NULL && !s_chipGlyph[ index ] && icon->getImageWidth() > 0 && icon->getImageHeight() > 0 )
	{
		// over the bezel up to the lit frame's 2 px, so the picture takes all of a chip that small
		const Int inset = max( REAL_TO_INT( CHIP_PICTURE_INSET * size.y / 28.0f + 0.5f ), 2 );
		IRegion2D room = { { at.x + inset, at.y + inset }, { at.x + size.x - inset, at.y + size.y - inset } };
		if( room.width() > 0 && room.height() > 0 )
		{
			const Real cover = max( (Real)room.width() / icon->getImageWidth(), (Real)room.height() / icon->getImageHeight() );
			const Int w = REAL_TO_INT( icon->getImageWidth() * cover + 0.5f );
			const Int h = REAL_TO_INT( icon->getImageHeight() * cover + 0.5f );
			const Int x = room.lo.x + ( room.width() - w ) / 2, y = room.lo.y + ( room.height() - h ) / 2;
			TheDisplay->setClipRegion( &room );
			TheDisplay->drawImage( icon, x, y, x + w, y + h, lit ? 0xFFFFFFFF : CHIP_PICTURE_DIM );
			TheDisplay->enableClipping( FALSE );
		}
		if( text != NULL )
		{
			const Int bx = at.x + size.x - inset - textW - 2, by = at.y + size.y - inset - textH;
			TheDisplay->drawFillRect( bx - 2, by, textW + 4, textH, CHIP_BADGE );
			text->draw( bx, by, CHIP_TEXT, TAB_TEXT_EDGE );
		}
		return;
	}

	// Tümü, or a factory with no picture of its own: the artist's kind glyph (or nothing) left, the
	// number right of it, the pair centred.  White over a dark edge on every face, lit or not
	Int iconW = 0, iconH = 0;
	const Int margin = max( size.y / 9, 2 );
	if( icon != NULL && icon->getImageWidth() > 0 && icon->getImageHeight() > 0 )
	{
		iconH = size.y - 2 * margin;
		iconW = REAL_TO_INT( (Real)icon->getImageWidth() * iconH / icon->getImageHeight() );
		// a wide picture is cut down to what leaves the number its room
		const Int roomW = size.x - 2 * margin - textW - margin;
		if( iconW > roomW && roomW > 0 )
		{
			iconH = REAL_TO_INT( (Real)iconH * roomW / iconW );
			iconW = roomW;
		}
	}
	const Int gap = iconW > 0 && textW > 0 ? margin : 0;
	const Int left = at.x + ( size.x - iconW - gap - textW ) / 2 + push;
	if( iconW > 0 && iconH > 0 )
	{
		const Int top = at.y + ( size.y - iconH ) / 2 + push;
		if( s_chipGlyph[ index ] )
		{
			// a glyph: its shape in black a pixel down and right, then in white over it
			TheDisplay->drawImage( icon, left + 1, top + 1, left + 1 + iconW, top + 1 + iconH, GLYPH_EDGE );
			TheDisplay->drawImage( icon, left, top, left + iconW, top + iconH, CHIP_TEXT );
		}
		else
			TheDisplay->drawImage( icon, left, top, left + iconW, top + iconH );
	}
	if( text != NULL )
		text->draw( left + iconW + gap, at.y + ( size.y - textH ) / 2 + push, CHIP_TEXT, TAB_TEXT_EDGE );
}

/// the box: the side's cameo well under every cell, the cameos inside them
static void drawBox( GameWindow *window, WinInstanceData *instData )
{
	const Image *well = Sidebar_image( "CameoWell" );
	if( well != NULL )
		for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
			TheDisplay->drawImage( well, s_cells[ i ].lo.x, s_cells[ i ].lo.y, s_cells[ i ].hi.x, s_cells[ i ].hi.y );
}

/// a cameo as the push button draws it, and on a research cell an up chevron in the top left corner,
/// white over a dark edge, so it reads as research and not a building to place
static void drawCameo( GameWindow *window, WinInstanceData *instData )
{
	if( s_cameoDraw != NULL )
		s_cameoDraw( window, instData );
	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( window );
	if( command == NULL || ( command->getCommandType() != GUI_COMMAND_PLAYER_UPGRADE && command->getCommandType() != GUI_COMMAND_OBJECT_UPGRADE ) )
		return;

	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	const Int half = max( size.y / 9, 3 );						// the chevron's half width; its height is the same
	const Int cx = at.x + 4 + half, top = at.y + 4;
	for( Int pass = 0; pass < 2; pass++ )
	{
		// the dark edge a pixel down and right and wider, then the white over it
		const Int d = pass == 0 ? 1 : 0;
		const Color c = pass == 0 ? BADGE_EDGE : CHIP_TEXT;
		const Real w = pass == 0 ? 4.0f : 2.0f;
		for( Int row = 0; row < 2; row++ )
		{
			const Int y = top + d + row * ( half / 2 + 2 );
			TheDisplay->drawLine( cx - half + d, y + half, cx + d, y, w, c );
			TheDisplay->drawLine( cx + d, y, cx + half + d, y + half, w, c );
		}
	}
}

/// Sat or Onar: the side's KeySell or KeyRepair, P while its mode is on, else the steel stand-in with
/// the name on it
static void drawKey( GameWindow *window, WinInstanceData *instData )
{
	const Bool sell = window == s_sellKey;
	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	if( size.x < 3 || size.y < 3 )
		return;
	const Bool lit = s_cursorMode == ( sell ? SIDEBAR_CURSOR_SELL : SIDEBAR_CURSOR_REPAIR )
		|| BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
	const Image *art = statePiece( sell ? "KeySell" : "KeyRepair", BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ), lit, hover );
	if( art != NULL )
	{
		TheDisplay->drawImage( art, at.x, at.y, at.x + size.x, at.y + size.y );
		return;
	}
	drawSteel( at, size, lit, hover );
	DisplayString *label = s_keyLabel[ sell ? 0 : 1 ];
	if( label == NULL )
		return;
	Int w = 0, h = 0;
	label->getSize( &w, &h );
	label->draw( at.x + ( size.x - w ) / 2, at.y + ( size.y - h ) / 2, CHIP_TEXT, TAB_TEXT_EDGE );
}

static void drawNothing( GameWindow *window, WinInstanceData *instData )
{
}

//-------------------------------------------------------------------------------------------------
// The sell and repair cursors.  Client only: what a click sends is checked again by the logic.
//-------------------------------------------------------------------------------------------------
Int Sidebar_sellPress( Bool modeOn, Bool sellableSelected )
{
	if( modeOn )
		return SIDEBAR_SELL_PRESS_MODE_OFF;
	return sellableSelected ? SIDEBAR_SELL_PRESS_SELECTED : SIDEBAR_SELL_PRESS_MODE_ON;
}

Int Sidebar_cursorMode( void )
{
	return s_cursorMode;
}

Bool Sidebar_cancelCursorMode( void )
{
	const Bool was = s_cursorMode != SIDEBAR_CURSOR_NONE;
	s_cursorMode = SIDEBAR_CURSOR_NONE;
	return was;
}

/// a structure the local player controls under `mouse`, NULL for anything else
static Object *ownStructureAt( const ICoord2D &mouse )
{
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	Drawable *draw = TheTacticalView && player ? TheTacticalView->pickDrawable( &mouse, FALSE, PICK_TYPE_SELECTABLE ) : NULL;
	Object *obj = draw ? draw->getObject() : NULL;
	if( obj == NULL || obj->getControllingPlayer() != player || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) )
		return NULL;
	return obj;
}

/// what the logic's MSG_SELL asks before it sells
static Bool sellable( const Object *obj )
{
	return obj != NULL && !obj->testStatus( OBJECT_STATUS_SOLD ) && AIGroup::mayPlayerSell( obj );
}

struct RepairerSearch
{
	const Object *target;
	Bool found;
};

static void findRepairer( Object *obj, void *userData )
{
	RepairerSearch *s = (RepairerSearch *)userData;
	if( s->found || obj->isEffectivelyDead() )
		return;
	AIUpdateInterface *ai = obj->getAI();
	if( ai != NULL && ai->getDozerAIInterface() != NULL && TheActionManager->canRepairObject( obj, s->target, CMD_FROM_PLAYER ) )
		s->found = TRUE;
}

/// one of the local player's builders could go and repair `obj`: what the logic's pick asks
static Bool repairable( const Object *obj )
{
	if( obj == NULL )
		return FALSE;
	RepairerSearch s = { obj, FALSE };
	ThePlayerList->getLocalPlayer()->iterateObjects( findRepairer, &s );
	return s.found;
}

static void enterCursorMode( Int mode )
{
	// one thing on the pointer at a time
	TheInGameUI->setGUICommand( NULL );
	TheInGameUI->placeBuildAvailable( NULL, NULL );
	TheInGameUI->disarmSignal();
	s_cursorMode = mode;
}

static Bool sellableSelected( void )
{
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = *it ? (*it)->getObject() : NULL;
		if( obj != NULL && obj->isLocallyControlled() && sellable( obj ) )
			return TRUE;
	}
	return FALSE;
}

static void pressSell( void )
{
	switch( Sidebar_sellPress( s_cursorMode == SIDEBAR_CURSOR_SELL, sellableSelected() ) )
	{
		case SIDEBAR_SELL_PRESS_MODE_OFF:	s_cursorMode = SIDEBAR_CURSOR_NONE; break;
		// the bar's own Sell: the logic sells what of the selection it may
		case SIDEBAR_SELL_PRESS_SELECTED:	TheMessageStream->appendMessage( GameMessage::MSG_SELL ); break;
		default:													enterCursorMode( SIDEBAR_CURSOR_SELL ); break;
	}
}

static void pressRepair( void )
{
	if( s_cursorMode == SIDEBAR_CURSOR_REPAIR )
		s_cursorMode = SIDEBAR_CURSOR_NONE;
	else
		enterCursorMode( SIDEBAR_CURSOR_REPAIR );
}

void Sidebar_pressSell( void ) { pressSell(); }
void Sidebar_pressRepair( void ) { pressRepair(); }

Bool Sidebar_translate( const GameMessage *msg )
{
	const GameMessage::Type type = msg->getType();
	if( s_eatRightClick && ( type == GameMessage::MSG_MOUSE_RIGHT_CLICK || type == GameMessage::MSG_MOUSE_RIGHT_DOUBLE_CLICK ) )
	{
		s_eatRightClick = FALSE;
		return TRUE;
	}
	if( s_cursorMode == SIDEBAR_CURSOR_NONE )
		return FALSE;

	switch( type )
	{
		// the left button is the mode's: no selection box, no deselect on the release
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
		case GameMessage::MSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
		case GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP:
			return TRUE;

		case GameMessage::MSG_MOUSE_LEFT_CLICK:
		case GameMessage::MSG_MOUSE_LEFT_DOUBLE_CLICK:
		{
			Object *obj = ownStructureAt( msg->getArgument( 0 )->pixelRegion.hi );
			if( s_cursorMode == SIDEBAR_CURSOR_SELL && sellable( obj ) )
			{
				GameMessage *sell = TheMessageStream->appendMessage( GameMessage::MSG_SELL );
				sell->appendObjectIDArgument( obj->getID() );
			}
			else if( s_cursorMode == SIDEBAR_CURSOR_REPAIR && repairable( obj ) )
			{
				GameMessage *repair = TheMessageStream->appendMessage( GameMessage::MSG_DO_REPAIR );
				repair->appendObjectIDArgument( obj->getID() );
				repair->appendBooleanArgument( TRUE );
			}
			return TRUE;
		}

		// a right click ends it, a right drag that panned does not; LookAtXlat still needs the release
		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
			if( TheLookAtTranslator == NULL || !TheLookAtTranslator->isRightDragPanning() )
			{
				s_cursorMode = SIDEBAR_CURSOR_NONE;
				s_eatRightClick = TRUE;
				s_rightReleaseTaken = TRUE;
			}
			return FALSE;
	}
	return FALSE;
}

Bool Sidebar_takeRightRelease( void )
{
	const Bool taken = s_rightReleaseTaken;
	s_rightReleaseTaken = FALSE;
	return taken;
}

void Sidebar_pointCursor( void )
{
	if( s_cursorMode == SIDEBAR_CURSOR_NONE || TheMouse == NULL )
		return;
	const MouseIO *io = TheMouse->getMouseStatus();
	if( io == NULL )
		return;
	Object *obj = ownStructureAt( io->pos );
	if( s_cursorMode == SIDEBAR_CURSOR_SELL )
		TheMouse->setCursor( sellable( obj ) ? Mouse::SELL : Mouse::SELL_INVALID );
	else
		TheMouse->setCursor( repairable( obj ) ? Mouse::DO_REPAIR : Mouse::GENERIC_INVALID );
}

/// the letter of the key `meta` is on, without its modifiers ("Q" for Ctrl+Q); empty when it has none
static UnicodeString keyLetter( Int meta )
{
	if( TheMetaMap == NULL )
		return UnicodeString::TheEmptyString;
	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
		if( rec->m_meta == (GameMessage::Type)meta )
			return MetaMap_formatChord( rec->m_key, NONE, rec->m_usableIn );
	return UnicodeString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
/** Whether `obj` is a live builder whose bar puts `thing` up now. */
//-------------------------------------------------------------------------------------------------
static Bool offersStructure( Object *obj, const ThingTemplate *thing )
{
	if( obj == NULL || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER ) )
		return FALSE;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button != NULL && button->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && button->getThingTemplate() == thing )
			return TheControlBar->getButtonAvailability( button, obj ) == COMMAND_AVAILABLE;
	}
	return FALSE;
}

struct BuilderSearch
{
	const ThingTemplate *thing;
	Object *found;
};

static void findBuilder( Object *obj, void *userData )
{
	BuilderSearch *s = (BuilderSearch *)userData;
	if( s->found == NULL && offersStructure( obj, s->thing ) )
		s->found = obj;
}

/// the builder to place `thing` for: the bar's stand-in when it offers it, free one first, else the
/// first of the player's builders that does (a captured one for a structure only it offers)
static Object *builderFor( const ThingTemplate *thing )
{
	for( Int freeOnly = 1; freeOnly >= 0; freeOnly-- )
	{
		Drawable *standIn = TheControlBar->findStandInBuilder( freeOnly != 0 );
		if( standIn != NULL && offersStructure( standIn->getObject(), thing ) )
			return standIn->getObject();
	}
	BuilderSearch s = { thing, NULL };
	ThePlayerList->getLocalPlayer()->iterateObjects( findBuilder, &s );
	return s.found;
}

//-------------------------------------------------------------------------------------------------
/** A right click: the units of `thing` queued deepest in the player's factories come back out, as
	* many as shift and ctrl would have queued.  The deepest are the ones furthest from coming out; a
	* factory's head, the one under way, goes last. */
//-------------------------------------------------------------------------------------------------
struct QueuedUnit
{
	ObjectID producer;
	ProductionID id;
	Int depth;	///< place in its factory's queue, 0 the head
};

struct QueuedSearch
{
	const ThingTemplate *thing;
	std::vector< QueuedUnit > *found;
};

static void findQueued( Object *obj, void *userData )
{
	QueuedSearch *s = (QueuedSearch *)userData;
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL || obj->isEffectivelyDead() || !obj->isLocallyControlled() )
		return;
	Int depth = 0;
	for( const ProductionEntry *p = pu->firstProduction(); p; p = pu->nextProduction( p ), depth++ )
		if( p->getProductionType() == PRODUCTION_UNIT && p->getProductionObject() != NULL
				&& ( p->getProductionObject() == s->thing || p->getProductionObject()->isEquivalentTo( s->thing ) ) )
		{
			const QueuedUnit unit = { obj->getID(), p->getProductionID(), depth };
			s->found->push_back( unit );
		}
}

static Bool deeper( const QueuedUnit &a, const QueuedUnit &b )
{
	return a.depth > b.depth;
}

static void cancelNewest( const ThingTemplate *thing )
{
	std::vector< QueuedUnit > queued;
	QueuedSearch s = { thing, &queued };
	ThePlayerList->getLocalPlayer()->iterateObjects( findQueued, &s );
	std::stable_sort( queued.begin(), queued.end(), deeper );

	const Int wanted = min( getBuildBatchCount(), (Int)queued.size() );
	for( Int each = 0; each < wanted; each++ )
	{
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CANCEL_UNIT_CREATE );
		msg->appendIntegerArgument( queued[ each ].id );
		msg->appendObjectIDArgument( queued[ each ].producer );
	}
}

//-------------------------------------------------------------------------------------------------
void Sidebar_pressChip( std::vector< ObjectID > &picks, ObjectID chip, Bool toggle )
{
	if( chip == INVALID_ID || !toggle )
	{
		picks.clear();
		if( chip != INVALID_ID )
			picks.push_back( chip );
		return;
	}
	std::vector< ObjectID >::iterator at = std::lower_bound( picks.begin(), picks.end(), chip );
	if( at != picks.end() && *at == chip )
		picks.erase( at );
	else
		picks.insert( at, chip );
}

Int Sidebar_nextFactory( std::vector< UnsignedInt > &queued, const std::vector< UnsignedInt > &caps )
{
	Int best = -1;
	for( Int i = 0; i < (Int)queued.size() && i < (Int)caps.size(); i++ )
		if( queued[ i ] < caps[ i ] && ( best < 0 || queued[ i ] < queued[ best ] ) )
			best = i;
	if( best >= 0 )
		queued[ best ]++;
	return best;
}

static Bool isResearch( const Buildable &entry );
static Bool isPower( const Buildable &entry );
static CommandAvailability pickedAvailability( Object *factory, const ThingTemplate *thing );

/// a unit cameo's left click.  Tümü: the logic picks the factory (MSG_QUEUE_UNIT_FOR_GROUP with no
/// squad); one chip lit: that factory; several: the batch spread over those whose own bar has the
/// unit ready, each unit to the least loaded as the bar spreads a build over several selected
/// factories (ControlBarCommandProcessing.cpp, GUI_COMMAND_UNIT_BUILD), the lower ObjectID on a tie,
/// each order naming its factory.  Cut to the bank as GroupPanel_queueUnit cuts it; the logic checks
/// every factory again (Player::factoryTakesGroupUnit) and drops an order that no longer fits.
static void queueAtPicks( const ThingTemplate *thing )
{
	const std::vector< ObjectID > &picks = s_picks[ s_tab ];
	if( picks.size() <= 1 )
	{
		GroupPanel_queueUnit( thing, -1, picks.empty() ? INVALID_ID : picks[ 0 ] );
		return;
	}

	std::vector< ObjectID > factories;
	std::vector< UnsignedInt > queued, caps;
	for( size_t p = 0; p < picks.size(); p++ )
	{
		Object *factory = TheGameLogic->findObjectByID( picks[ p ] );
		const ProductionUpdateInterface *pu = factory != NULL ? factory->getProductionUpdateInterface() : NULL;
		if( pu == NULL || pickedAvailability( factory, thing ) != COMMAND_AVAILABLE )
			continue;
		factories.push_back( picks[ p ] );
		queued.push_back( pu->getProductionCount() );
		caps.push_back( getQueueCap( pu ) );
	}

	Player *player = ThePlayerList->getLocalPlayer();
	Int wanted = getBuildBatchCount();
	const Int cost = thing->calcCostToBuild( player );
	if( cost > 0 )
		wanted = min( wanted, (Int)( player->getMoney()->countMoney() / (UnsignedInt)cost ) );
	for( Int each = 0; each < wanted; each++ )
	{
		const Int at = Sidebar_nextFactory( queued, caps );
		if( at < 0 )
			break;
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UNIT_FOR_GROUP );
		msg->appendIntegerArgument( thing->getTemplateID() );
		msg->appendIntegerArgument( -1 );
		msg->appendObjectIDArgument( factories[ at ] );
	}
}

static void clickCameo( GameWindow *control, const CommandButton *command, Bool right )
{
	if( ThePlayerList->getLocalPlayer() == NULL )
		return;

	// a power cell fires as the tray's button does: the bar finds the holder itself, no selection
	// needed, and the logic hears of it through the same messages
	for( size_t at = 0; at < s_list.size(); at++ )
		if( s_list[ at ].button == command && isPower( s_list[ at ] ) )
		{
			if( !right )
				TheControlBar->firePowerCell( control );
			return;
		}

	// a research cell: queued at the building that offers it, cancelled at the one researching it,
	// both named on the message so no selection is needed (the logic checks the building is the
	// player's, can make and can afford it)
	const UpgradeTemplate *upgrade = command->getUpgradeTemplate();
	if( upgrade != NULL && ( command->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE || command->getCommandType() == GUI_COMMAND_OBJECT_UPGRADE ) )
	{
		for( size_t at = 0; at < s_list.size(); at++ )
		{
			if( s_list[ at ].button != command || !isResearch( s_list[ at ] ) )
				continue;
			const ObjectID building = right ? s_researcher[ at ] : s_producer[ at ];
			if( building == INVALID_ID )
				return;
			GameMessage *msg;
			if( right )
			{
				msg = TheMessageStream->appendMessage( GameMessage::MSG_CANCEL_UPGRADE );
				msg->appendIntegerArgument( upgrade->getUpgradeNameKey() );
				msg->appendObjectIDArgument( building );
			}
			else
			{
				msg = TheMessageStream->appendMessage( GameMessage::MSG_QUEUE_UPGRADE );
				msg->appendObjectIDArgument( building );
				msg->appendIntegerArgument( upgrade->getUpgradeNameKey() );
			}
			return;
		}
		return;
	}

	const ThingTemplate *thing = command->getThingTemplate();
	if( thing == NULL )
		return;

	if( command->getCommandType() == GUI_COMMAND_UNIT_BUILD )
	{
		if( right )
			cancelNewest( thing );
		else
			queueAtPicks( thing );
	}
	else if( command->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && !right )
	{
		// builder chips lit: those of them that offer it put it up, one of them named on the order
		// (Sidebar_placementBuilder); none of them can, nothing (its cameo is grey).  Tümü: the
		// stand-in, and the logic's nearest idle builder as before
		const std::vector< ObjectID > &picks = s_picks[ s_tab ];
		s_placingBuilders.clear();
		s_namedBuilders.clear();
		for( size_t p = 0; p < picks.size(); p++ )
			if( offersStructure( TheGameLogic->findObjectByID( picks[ p ] ), thing ) )
				s_placingBuilders.push_back( picks[ p ] );
		if( !picks.empty() && s_placingBuilders.empty() )
			return;
		Object *builder = s_placingBuilders.empty() ? builderFor( thing ) : TheGameLogic->findObjectByID( s_placingBuilders[ 0 ] );
		if( builder != NULL )
			ControlBar::startStructurePlacement( command, builder );
	}
}

//-------------------------------------------------------------------------------------------------
ObjectID Sidebar_placementBuilder( void )
{
	// only while the structure on the cursor is still the one the lit builders' cameo put there
	if( s_placingBuilders.empty() || TheInGameUI == NULL || TheInGameUI->getPendingPlaceType() == NULL
			|| TheInGameUI->getPendingPlaceSourceObjectID() != s_placingBuilders[ 0 ] )
		return INVALID_ID;
	ICoord2D anchor;
	Coord3D site;
	if( s_placingBuilders.size() == 1 || !TheInGameUI->isPlacementAnchored() || TheTacticalView == NULL )
		return s_placingBuilders[ 0 ];
	TheInGameUI->getPlacementPoints( &anchor, NULL );
	if( !TheTacticalView->screenToTerrain( &anchor, &site ) )
		return s_placingBuilders[ 0 ];

	// several lit: the pick MSG_DOZER_CONSTRUCT makes among selected builders (GameLogicDispatch.cpp,
	// considerBuilder), made among them: the free one nearest the site, else the nearest of those with
	// the fewest jobs, the lower ObjectID on an equal distance.  A builder already named in this
	// placement is busy: a Shift run's orders are still on their way to the logic, which has not given
	// him the job yet
	const ThingTemplate *thing = TheInGameUI->getPendingPlaceType();
	ObjectID best[ 2 ] = { INVALID_ID, INVALID_ID };		///< [0] the free one, [1] any
	Real bestDistSqr[ 2 ] = { 0.0f, 0.0f };
	Int bestJobs = 0;																	///< [1]'s jobs named in this placement
	for( size_t b = 0; b < s_placingBuilders.size(); b++ )
	{
		Object *obj = TheGameLogic->findObjectByID( s_placingBuilders[ b ] );
		if( !offersStructure( obj, thing ) )
			continue;
		const Real dx = obj->getPosition()->x - site.x, dy = obj->getPosition()->y - site.y;
		const Real distSqr = dx * dx + dy * dy;
		const Int jobs = (Int)std::count( s_namedBuilders.begin(), s_namedBuilders.end(), obj->getID() );
		AIUpdateInterface *ai = obj->getAI();
		DozerAIInterface *dozer = ai != NULL ? ai->getDozerAIInterface() : NULL;
		const SupplyTruckAIInterface *supply = ai != NULL ? ai->getSupplyTruckAIInterface() : NULL;
		const Bool idle = jobs == 0 && dozer != NULL && !dozer->isAnyTaskPending() && !( supply != NULL && supply->isCurrentlyFerryingSupplies() );
		if( idle && ( best[ 0 ] == INVALID_ID || distSqr < bestDistSqr[ 0 ] ) )
		{
			best[ 0 ] = obj->getID();
			bestDistSqr[ 0 ] = distSqr;
		}
		if( best[ 1 ] == INVALID_ID || jobs < bestJobs || ( jobs == bestJobs && distSqr < bestDistSqr[ 1 ] ) )
		{
			best[ 1 ] = obj->getID();
			bestDistSqr[ 1 ] = distSqr;
			bestJobs = jobs;
		}
	}
	const ObjectID named = best[ 0 ] != INVALID_ID ? best[ 0 ] : best[ 1 ] != INVALID_ID ? best[ 1 ] : s_placingBuilders[ 0 ];
	s_namedBuilders.push_back( named );
	return named;
}

/// a factory or builder chip: its display name alone, nothing about its owner
static void chipTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		if( s_chips[ i ] == window && !s_chipName[ i ].isEmpty() )
			TheMouse->setCursorTooltip( s_chipName[ i ] );
}

static WindowMsgHandledType boxSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	// the pointer off a chip takes its tooltip with it, so none is left over the cameos
	if( msg == GBM_MOUSE_LEAVING )
	{
		for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
			if( (GameWindow *)mData1 == s_chips[ i ] )
				TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
	}
	if( msg != GBM_SELECTED && msg != GBM_SELECTED_RIGHT )
		return GameWinDefaultSystem( window, msg, mData1, mData2 );

	GameWindow *control = (GameWindow *)mData1;
	if( control != NULL && ( control == s_sellKey || control == s_repairKey ) )
	{
		if( msg == GBM_SELECTED )
		{
			if( control == s_sellKey )
				pressSell();
			else
				pressRepair();
		}
		return MSG_HANDLED;
	}
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		if( control == s_tabs[ t ] )
		{
			if( msg == GBM_SELECTED )
				Sidebar_selectTab( t );
			return MSG_HANDLED;
		}
	// Tümü lights itself alone; a chip alone, or with Shift goes into the lit set or out of it, as
	// Shift adds to and takes from a selection
	if( control == s_chipAll )
	{
		if( msg == GBM_SELECTED )
			Sidebar_pressChip( s_picks[ s_tab ], INVALID_ID, FALSE );
		return MSG_HANDLED;
	}
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		if( control == s_chips[ i ] )
		{
			if( msg == GBM_SELECTED && i < (Int)s_factories.size() )
				Sidebar_pressChip( s_picks[ s_tab ], s_factories[ i ], TheKeyboard != NULL && TheKeyboard->isShift() );
			return MSG_HANDLED;
		}
	if( control == s_arrowUp || control == s_arrowDown )
	{
		if( msg == GBM_SELECTED )
			Sidebar_scrollBy( control == s_arrowUp ? -1 : 1 );
		return MSG_HANDLED;
	}

	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( control );
	if( command != NULL )
		clickCameo( control, command, msg == GBM_SELECTED_RIGHT );
	return MSG_HANDLED;
}

/// the wheel anywhere over the box scrolls the cameos; nothing under the box sees the pointer
static WindowMsgHandledType boxInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_WHEEL_UP || msg == GWM_WHEEL_DOWN )
	{
		Sidebar_scrollBy( msg == GWM_WHEEL_UP ? -1 : 1 );
		return MSG_HANDLED;
	}
	return GameWinBlockInput( window, msg, mData1, mData2 );
}

static GameWindow *makeButton( UnsignedInt status, GameWindow *parent = NULL )
{
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	// GadgetPushButtonInput reads the right click bit off the instance data, not the window's status
	if( BitTest( status, WIN_STATUS_RIGHT_CLICK ) )
		BitSet( inst.m_status, WIN_STATUS_RIGHT_CLICK );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent != NULL ? parent : s_box, status | WIN_STATUS_ENABLED, 0, 0, 1, 1, &inst, NULL, TRUE );
	if( button == NULL )
		return NULL;
	GadgetButtonSetData( button, NULL );
	GadgetButtonSetAltSound( button, "GUICommandBarClick" );
	return button;
}

static Bool makeWindows( GameWindow *parent )
{
	if( boxAlive() && s_box->winGetParent() == parent )
		return TRUE;

	s_box = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, 0, 0, 1, 1, boxSystem );
	if( s_box == NULL )
		return FALSE;
	s_box->winSetWindowId( boxId() );
	s_box->winSetDrawFunc( drawBox );
	s_box->winSetInputFunc( boxInput );

	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		GameWindow *button = makeButton( 0 );
		s_tabs[ t ] = button;
		if( button == NULL )
			continue;
		GadgetButtonSetData( button, (void *)(intptr_t)( t + 1 ) );
		button->winSetDrawFunc( drawTab );
		if( s_keyText[ t ] == NULL )
			s_keyText[ t ] = TheDisplayStringManager->newDisplayString();
		s_keyShown[ t ].clear();
	}

	for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
	{
		GameWindow *button = makeButton( WIN_STATUS_IMAGE | WIN_STATUS_USE_OVERLAY_STATES | WIN_STATUS_RIGHT_CLICK | WIN_STATUS_HIDDEN );
		s_cameos[ i ] = button;
		if( button == NULL )
			continue;
		button->winSetTooltipFunc( buildTooltip );
		GadgetButtonSetBorder( button, TheControlBar->getBuildBorderColor() );
		if( button->winGetDrawFunc() != drawCameo )
			s_cameoDraw = button->winGetDrawFunc();
		button->winSetDrawFunc( drawCameo );
	}

	GameWindow **chips[ HUD_SIDEBAR_PICKER_CHIPS + 1 ] = { &s_chipAll };
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		chips[ i + 1 ] = &s_chips[ i ];
	for( Int c = 0; c <= HUD_SIDEBAR_PICKER_CHIPS; c++ )
	{
		*chips[ c ] = makeButton( WIN_STATUS_HIDDEN );
		if( *chips[ c ] != NULL )
		{
			(*chips[ c ])->winSetDrawFunc( drawChip );
			if( c > 0 )
				(*chips[ c ])->winSetTooltipFunc( chipTooltip );
		}
		if( s_chipText[ c ] == NULL )
			s_chipText[ c ] = TheDisplayStringManager->newDisplayString();
	}
	// a bar made again is most often a new match, whose ObjectIDs mean other buildings; and with the
	// lit sets gone the selection is followed again, so Options' Accept mid-match keeps its chips lit
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		s_picks[ t ].clear();
	s_followed.clear();
	s_countries.clear();

	s_arrowUp = makeButton( 0 );
	s_arrowDown = makeButton( 0 );
	GameWindow *arrows[] = { s_arrowUp, s_arrowDown };
	for( Int a = 0; a < 2; a++ )
	{
		if( arrows[ a ] == NULL )
			continue;
		if( arrows[ a ]->winGetDrawFunc() != drawArrow )
			s_buttonDraw = arrows[ a ]->winGetDrawFunc();
		arrows[ a ]->winSetDrawFunc( drawArrow );
	}

	// Sat and Onar in a box of their own: the cameo box stops short of the radar, and one box across
	// the whole row would take the General button's clicks
	s_keyBox = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, 0, 0, 1, 1, boxSystem );
	s_sellKey = s_repairKey = NULL;
	if( s_keyBox != NULL )
	{
		s_keyBox->winSetDrawFunc( drawNothing );
		s_keyBox->winSetInputFunc( GameWinBlockInput );
		s_sellKey = makeButton( 0, s_keyBox );
		s_repairKey = makeButton( 0, s_keyBox );
		GameWindow *keys[] = { s_sellKey, s_repairKey };
		for( Int k = 0; k < 2; k++ )
		{
			if( keys[ k ] != NULL )
				keys[ k ]->winSetDrawFunc( drawKey );
			if( s_keyLabel[ k ] == NULL )
				s_keyLabel[ k ] = TheDisplayStringManager->newDisplayString();
		}
	}
	s_cursorMode = SIDEBAR_CURSOR_NONE;

	s_dirty = TRUE;
	return TRUE;
}

static void place( GameWindow *win, const IRegion2D &cell, const ICoord2D &origin )
{
	if( win == NULL )
		return;
	win->winSetPosition( cell.lo.x - origin.x, cell.lo.y - origin.y );
	win->winSetSize( cell.width(), cell.height() );
}

/// cameo `i` on its art's own shape: as large as its room in the well holds, centred.  The push
/// button draws its picture, clock, count and hover over the whole window, so the window takes the
/// picture's shape rather than the picture the cell's; with no picture it takes the whole room
static void fitCameo( Int i, const Image *art )
{
	if( s_cameos[ i ] == NULL || s_box == NULL )
		return;
	IRegion2D r = s_cameoRoom[ i ];
	if( art != NULL && art->getImageWidth() > 0 && art->getImageHeight() > 0 && r.width() > 0 && r.height() > 0 )
	{
		const Real fit = min( (Real)r.width() / art->getImageWidth(), (Real)r.height() / art->getImageHeight() );
		const Int w = max( REAL_TO_INT( art->getImageWidth() * fit + 0.5f ), 1 );
		const Int h = max( REAL_TO_INT( art->getImageHeight() * fit + 0.5f ), 1 );
		r.lo.x += ( r.width() - w ) / 2;
		r.lo.y += ( r.height() - h ) / 2;
		r.hi.x = r.lo.x + w;
		r.hi.y = r.lo.y + h;
	}
	ICoord2D origin;
	s_box->winGetScreenPosition( &origin.x, &origin.y );
	place( s_cameos[ i ], r, origin );
}

//-------------------------------------------------------------------------------------------------
void Sidebar_layout( const HudLayoutRects &rects )
{
	GameWindow *parent = barParent();
	if( parent == NULL || !makeWindows( parent ) )
		return;

	// first of ControlBarParent's children: drawn last, over BackgroundMarker's sidebar steel, and
	// asked first for the pointer; Sat and Onar's box right after it, over the radar's blocker too
	if( s_keyBox != NULL && s_box->winGetNext() != s_keyBox )
	{
		s_keyBox->winBringToTop();
		s_box->winBringToTop();
	}
	if( parent->winGetChild() != s_box )
		s_box->winBringToTop();

	// the box holds everything here, tabs to arrows; it does not reach up to the radar
	IRegion2D box = rects.tabs[ 0 ];
	auto grow = [ &box ]( const IRegion2D &r )
	{
		box.lo.x = min( box.lo.x, r.lo.x );
		box.lo.y = min( box.lo.y, r.lo.y );
		box.hi.x = max( box.hi.x, r.hi.x );
		box.hi.y = max( box.hi.y, r.hi.y );
	};
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		grow( rects.tabs[ t ] );
	for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
		grow( rects.cameos[ i ] );
	grow( rects.arrowUp );
	grow( rects.arrowDown );
	grow( rects.pickerAll );
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		grow( rects.pickerChips[ i ] );

	ICoord2D origin;
	parent->winGetScreenPosition( &origin.x, &origin.y );
	place( s_box, box, origin );
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		place( s_tabs[ t ], rects.tabs[ t ], box.lo );
	for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
	{
		// the cell is the well; the cameo sits 3 px inside it at 1080
		const Int inset = max( REAL_TO_INT( CAMEO_INSET * rects.scale + 0.5f ), 1 );
		s_cells[ i ] = rects.cameos[ i ];
		IRegion2D &room = s_cameoRoom[ i ];
		room = rects.cameos[ i ];
		room.lo.x += inset;
		room.lo.y += inset;
		room.hi.x -= inset;
		room.hi.y -= inset;
		fitCameo( i, s_cameos[ i ] != NULL ? GadgetButtonGetEnabledImage( s_cameos[ i ] ) : NULL );
	}
	place( s_arrowUp, rects.arrowUp, box.lo );
	place( s_arrowDown, rects.arrowDown, box.lo );
	place( s_chipAll, rects.pickerAll, box.lo );
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		place( s_chips[ i ], rects.pickerChips[ i ], box.lo );

	IRegion2D keys = rects.sellButton;
	keys.lo.x = min( keys.lo.x, rects.repairButton.lo.x );
	keys.lo.y = min( keys.lo.y, rects.repairButton.lo.y );
	keys.hi.x = max( keys.hi.x, rects.repairButton.hi.x );
	keys.hi.y = max( keys.hi.y, rects.repairButton.hi.y );
	place( s_keyBox, keys, origin );
	place( s_sellKey, rects.sellButton, keys.lo );
	place( s_repairKey, rects.repairButton, keys.lo );
	const char *const keyNames[ 2 ] = { "GUI:SidebarSell", "GUI:SidebarRepair" };
	GameWindow *keyWindows[ 2 ] = { s_sellKey, s_repairKey };
	for( Int k = 0; k < 2; k++ )
	{
		const UnicodeString name = TheGameText->fetch( keyNames[ k ] );
		if( keyWindows[ k ] != NULL )
			keyWindows[ k ]->winSetTooltip( name );
		if( s_keyLabel[ k ] != NULL )
			s_keyLabel[ k ]->setText( name );
	}

	GameFont *chipFont = TheFontLibrary->getFont( "Arial", max( REAL_TO_INT( CHIP_POINTS * rects.scale ), 6 ), TRUE );
	for( Int k = 0; k < 2; k++ )
		if( s_keyLabel[ k ] != NULL && chipFont != NULL )
			s_keyLabel[ k ]->setFont( chipFont );
	for( Int c = 0; c <= HUD_SIDEBAR_PICKER_CHIPS; c++ )
	{
		if( s_chipText[ c ] == NULL || chipFont == NULL )
			continue;
		s_chipText[ c ]->setFont( chipFont );
		UnicodeString text;
		if( c == 0 )
			text = TheGameText->fetch( "GUI:SidebarAll" );
		else
			text.format( u"%d", c );
		s_chipText[ c ]->setText( text );
	}

	// Tümü smaller until it fits its chip, 3 px clear of each side
	if( s_chipText[ 0 ] != NULL )
	{
		for( Int points = max( REAL_TO_INT( CHIP_POINTS * rects.scale ), 6 ); points > 5; points-- )
		{
			s_chipText[ 0 ]->setFont( TheFontLibrary->getFont( "Arial", points, TRUE ) );
			Int w = 0, h = 0;
			s_chipText[ 0 ]->getSize( &w, &h );
			if( w <= rects.pickerAll.width() - 6 )
				break;
		}
	}

	// one key font for the tabs and the strip's command buttons (W3DPushButton asks Sidebar_keyFont),
	// rounded rather than cut so 720's 6.7 points is 7, not 6
	GameFont *keyFont = TheFontLibrary->getFont( "Arial", max( REAL_TO_INT( KEY_POINTS * rects.scale + 0.5f ), 6 ), TRUE );
	s_keyFont = keyFont;
	s_keyScale = rects.scale;
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		if( s_keyText[ t ] != NULL && keyFont != NULL )
			s_keyText[ t ]->setFont( keyFont );
	}

	s_shown = TRUE;
	s_dirty = TRUE;
	s_iconPlayer = NULL;	// a new match keeps the slot's Player but may change its side
	s_box->winHide( FALSE );
	if( s_keyBox != NULL )
		s_keyBox->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
void Sidebar_hide( void )
{
	s_shown = FALSE;
	s_cursorMode = SIDEBAR_CURSOR_NONE;
	if( boxAlive() )
	{
		s_box->winHide( TRUE );
		if( s_keyBox != NULL )
			s_keyBox->winHide( TRUE );
	}
}

//-------------------------------------------------------------------------------------------------
void Sidebar_selectTab( Int tab )
{
	if( tab < 0 || tab >= SIDEBAR_TAB_COUNT )
		return;
	if( tab != s_tab )
		s_dirty = TRUE;
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
	s_row = Sidebar_scroll( (Int)s_list.size(), HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, s_row + rows );
}

//-------------------------------------------------------------------------------------------------
// The lists and the progress on them.
//-------------------------------------------------------------------------------------------------
static Bool structureTab( Int tab )
{
	return tab == SIDEBAR_TAB_BUILDINGS;
}

//-------------------------------------------------------------------------------------------------
void Sidebar_showOrder( const std::vector< Int > &country, const std::vector< Int > &seen, std::vector< Int > &order )
{
	order.clear();
	for( Int i = 0; i < (Int)country.size() && i < (Int)seen.size(); i++ )
		order.push_back( i );
	std::stable_sort( order.begin(), order.end(), [ & ]( Int a, Int b )
	{
		return country[ a ] != country[ b ] ? country[ a ] < country[ b ] : seen[ a ] < seen[ b ];
	} );
}

/// the country a side belongs to: a general's own faction is his country's ("AmericaAirForceGeneral"
/// is America's), anything else its own
static AsciiString countryOf( const AsciiString &side )
{
	static const char *const COUNTRIES[] = { "America", "China", "GLA", "Turkiye", "Boss" };
	for( Int i = 0; i < (Int)( sizeof( COUNTRIES ) / sizeof( COUNTRIES[ 0 ] ) ); i++ )
		if( side.startsWith( COUNTRIES[ i ] ) )
			return AsciiString( COUNTRIES[ i ] );
	return side;
}

/// `country`'s place among the player's: his own was put first when the order began, a foreign one
/// takes the next place the first time anything of it shows
static Int countryRank( const AsciiString &country )
{
	for( Int i = 0; i < (Int)s_countries.size(); i++ )
		if( s_countries[ i ] == country )
			return i;
	s_countries.push_back( country );
	return (Int)s_countries.size() - 1;
}

/// `key`'s place in `tab`'s first-seen order, taking the next one the first time it shows
static Int seenRank( Int tab, const void *key )
{
	std::vector< const void * > &seen = s_seen[ tab ];
	for( Int i = 0; i < (Int)seen.size(); i++ )
		if( seen[ i ] == key )
			return i;
	seen.push_back( key );
	return (Int)seen.size() - 1;
}

/// the show order starts again (a new bar: a new match, a load, Options' Accept), the player's own
/// country first.  The lit chips and the builders a placement named go with it: after a load or a
/// clock run back their ObjectIDs may name buildings the player never picked
static void resetShowOrder( Player *player )
{
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		s_seen[ t ].clear();
		s_picks[ t ].clear();
	}
	s_followed.clear();
	s_placingBuilders.clear();
	s_namedBuilders.clear();
	s_countries.clear();
	if( player != NULL )
		s_countries.push_back( countryOf( player->getSide() ) );
	s_seenFrame = 0;
}

/// whether `thing`'s own prerequisites are met: the buildings and sciences it asks for, and not barred
/// by a script.  What Player::canBuild reads for the bar, less the count limits, so a unit at its cap
/// is still listed (greyed) and one whose building is missing is not
static Bool prerequisitesMet( const Player *player, const ThingTemplate *thing )
{
	if( thing == NULL || !player->allowedToBuild( thing ) )
		return FALSE;
	if( thing->getBuildable() == BSTATUS_IGNORE_PREREQUISITES )
		return TRUE;
	for( Int i = 0; i < thing->getPrereqCount(); i++ )
		if( !thing->getNthPrereq( i )->isSatisfied( player ) )
			return FALSE;
	return TRUE;
}

/// whether a gathered cell is unlocked: its prerequisites, the sciences its button asks for and a
/// player upgrade it needs are all there.  Too dear, busy or at its cap is still unlocked (greyed, as
/// the bar has it); locked is left off the tab altogether
static Bool unlocked( const Player *player, const Buildable &entry )
{
	const CommandButton *button = entry.button;
	if( button == NULL )
		return FALSE;
	for( size_t i = 0; i < button->getScienceVec().size(); i++ )
		if( !player->hasScience( button->getScienceVec()[ i ] ) )
			return FALSE;
	const UpgradeTemplate *upgrade = button->getUpgradeTemplate();
	if( BitTest( button->getOptions(), NEED_UPGRADE ) && upgrade != NULL && upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER
			&& !player->hasUpgradeComplete( upgrade ) )
		return FALSE;
	return entry.thing == NULL || prerequisitesMet( player, entry.thing );
}

//-------------------------------------------------------------------------------------------------
/** The powers tab's research: every upgrade button on the command set of a finished structure of the
	* player's, one cell an upgrade.  A player upgrade the player has finished is left out, and so is
	* an object upgrade no building still lacks.  The cell keeps the building with the best availability
	* (the bar's own reading), and a click researches it there. */
//-------------------------------------------------------------------------------------------------
struct UpgradeSearch
{
	Player *player;
	std::vector< Buildable > *list;
	std::vector< ObjectID > *producers;
};

static void gatherUpgrades( Object *obj, void *userData )
{
	UpgradeSearch *s = (UpgradeSearch *)userData;
	if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ) || obj->getProductionUpdateInterface() == NULL
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || BitTest( button->getOptions(), SCRIPT_ONLY ) || !TheControlBar->hasCommandWindow( i ) )
			continue;
		const Bool playerUpgrade = button->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE;
		if( !playerUpgrade && button->getCommandType() != GUI_COMMAND_OBJECT_UPGRADE )
			continue;
		const UpgradeTemplate *upgrade = button->getUpgradeTemplate();
		if( upgrade == NULL )
			continue;
		if( playerUpgrade ? s->player->hasUpgradeComplete( upgrade ) : ( obj->hasUpgrade( upgrade ) || !obj->affectedByUpgrade( upgrade ) ) )
			continue;
		const CommandAvailability now = TheControlBar->getButtonAvailability( button, obj );
		if( now == COMMAND_HIDDEN )
			continue;

		size_t at = 0;
		while( at < s->list->size() && ( !isResearch( (*s->list)[ at ] ) || (*s->list)[ at ].button->getUpgradeTemplate() != upgrade ) )
			at++;
		if( at < s->list->size() )
		{
			if( ControlBar::commandAvailabilityRank( now ) > ControlBar::commandAvailabilityRank( (*s->list)[ at ].best ) )
			{
				(*s->list)[ at ].best = now;
				(*s->producers)[ at ] = obj->getID();
			}
			continue;
		}
		const Buildable added = { NULL, button, now };
		if( !unlocked( s->player, added ) )
			continue;
		s->list->push_back( added );
		s->producers->push_back( obj->getID() );
	}
}

static Bool isResearch( const Buildable &entry )
{
	if( entry.thing != NULL || entry.button == NULL )
		return FALSE;
	const GUICommandType type = entry.button->getCommandType();
	return type == GUI_COMMAND_PLAYER_UPGRADE || type == GUI_COMMAND_OBJECT_UPGRADE;
}

/// a powers tray button on the powers tab: a general's power, a superweapon, a select-all
static Bool isPower( const Buildable &entry )
{
	return entry.thing == NULL && entry.button != NULL && !isResearch( entry );
}

/// what a power cell fires from (the most ready holder of its power, as the tray asks), or the first
/// of the units a select-all names; NULL when the player has none
static Object *powerHolder( Player *player, const CommandButton *button )
{
	if( button->getSpecialPowerTemplate() != NULL )
		return player->findMostReadyShortcutSpecialPowerOfType( button->getSpecialPowerTemplate()->getSpecialPowerType() );
	return button->getThingTemplate() != NULL ? player->findAnyExistingObjectWithThingTemplate( button->getThingTemplate() ) : NULL;
}

/// `tab`'s unlocked items as the gathers find them, in the command sets' order, and per item the
/// building a research is queued at.  The powers tab: the tray's shown buttons that still have
/// something behind them, then the research
static void gatherTab( Player *player, Int tab, std::vector< Buildable > &items, std::vector< ObjectID > &producers )
{
	items.clear();
	producers.clear();
	if( tab == SIDEBAR_TAB_POWERS )
	{
		for( Int i = 0; i < MAX_SPECIAL_POWER_SHORTCUTS; i++ )
		{
			GameWindow *win = TheControlBar->getShownPowerShortcut( i );
			if( win == NULL )
				continue;
			const CommandButton *button = (const CommandButton *)GadgetButtonGetData( win );
			const CommandAvailability now = TheControlBar->getButtonAvailability( button, powerHolder( player, button ) );
			if( now == COMMAND_HIDDEN )
				continue;
			const Buildable power = { NULL, button, now };
			items.push_back( power );
			producers.push_back( INVALID_ID );
		}
		UpgradeSearch search = { player, &items, &producers };
		player->iterateObjects( gatherUpgrades, &search );
		return;
	}

	const Bool structures = structureTab( tab );
	if( structures )
		GroupPanel_gatherConstructible( player, s_all );
	else
		GroupPanel_gatherBuildable( player, s_all );
	for( size_t i = 0; i < s_all.size(); i++ )
	{
		const Bool pick = structures || s_all[ i ].thing->isKindOf( KINDOF_INFANTRY ) == ( tab == SIDEBAR_TAB_INFANTRY );
		if( pick && unlocked( player, s_all[ i ] ) )
			items.push_back( s_all[ i ] );
	}
	producers.assign( items.size(), INVALID_ID );
}

/// what an item is for the first-seen order: its structure's or unit's template, its research's
/// upgrade, its power's button
static const void *showKey( const Buildable &entry )
{
	if( entry.thing != NULL )
		return entry.thing;
	return isResearch( entry ) ? (const void *)entry.button->getUpgradeTemplate() : (const void *)entry.button;
}

/// an item's country's place: a structure's or unit's own side, a research's building's, a power's
/// holder's; the player's own (0) for one with no side or no holder
static Int countryPlace( Player *player, const Buildable &entry, ObjectID producer )
{
	const ThingTemplate *from = entry.thing;
	if( from == NULL )
	{
		const Object *obj = isResearch( entry ) ? TheGameLogic->findObjectByID( producer ) : powerHolder( player, entry.button );
		from = obj != NULL ? obj->getTemplate() : NULL;
	}
	if( from == NULL || from->getDefaultOwningSide().isEmpty() )
		return 0;
	return countryRank( countryOf( from->getDefaultOwningSide() ) );
}

/// `items`' show order into s_showOrder; an item never seen on `tab` before takes the next place in
/// its first-seen order, and a country never seen the next place among the player's
static void orderTab( Player *player, Int tab, const std::vector< Buildable > &items, const std::vector< ObjectID > &producers )
{
	s_countryOf.clear();
	s_seenAt.clear();
	for( size_t i = 0; i < items.size(); i++ )
	{
		s_countryOf.push_back( countryPlace( player, items[ i ], producers[ i ] ) );
		s_seenAt.push_back( seenRank( tab, showKey( items[ i ] ) ) );
	}
	Sidebar_showOrder( s_countryOf, s_seenAt, s_showOrder );
}

/// the tabs not shown are looked at a few times a second, so what unlocks on one of them while
/// another is shown takes its place in the order it unlocked, not the order the player looked
static void watchOtherTabs( Player *player, UnsignedInt frame )
{
	const UnsignedInt WATCH_FRAMES = LOGICFRAMES_PER_SECOND / 3;
	if( s_seenFrame != 0 && frame < s_seenFrame + WATCH_FRAMES )
		return;
	s_seenFrame = frame;
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		if( t == s_tab )
			continue;
		gatherTab( player, t, s_gathered, s_gatheredProducer );
		orderTab( player, t, s_gathered, s_gatheredProducer );
	}
}

static void fillList( Player *player )
{
	gatherTab( player, s_tab, s_gathered, s_gatheredProducer );
	orderTab( player, s_tab, s_gathered, s_gatheredProducer );
	s_list.clear();
	s_producer.clear();
	for( size_t i = 0; i < s_showOrder.size(); i++ )
	{
		s_list.push_back( s_gathered[ s_showOrder[ i ] ] );
		s_producer.push_back( s_gatheredProducer[ s_showOrder[ i ] ] );
	}
	s_count.assign( s_list.size(), 0 );
	s_clock.assign( s_list.size(), -1 );
	s_researcher.assign( s_list.size(), INVALID_ID );

	if( s_tab != s_loggedTab || (Int)s_list.size() != s_loggedCount )
	{
		s_loggedTab = s_tab;
		s_loggedCount = (Int)s_list.size();
		DEBUG_LOG(( "SIDEBAR tab %d cells %d\n", s_tab, s_loggedCount ));
	}
}

/// one factory's queue into s_count and s_clock: every queued unit of a listed type counts, the head
/// alone carries progress
/// how the picked factory's own bar has `thing`: its button's availability, restricted when it has none
static CommandAvailability pickedAvailability( Object *factory, const ThingTemplate *thing )
{
	// a factory's unit buttons on the unit tabs, a builder's structure buttons on Bina
	const Int type = structureTab( s_tab ) ? GUI_COMMAND_DOZER_CONSTRUCT : GUI_COMMAND_UNIT_BUILD;
	const CommandSet *set = TheControlBar->findCommandSet( factory->getCommandSetString() );
	for( Int i = 0; set != NULL && i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button != NULL && button->getCommandType() == type && button->getThingTemplate() == thing )
			return TheControlBar->getButtonAvailability( button, factory );
	}
	return COMMAND_RESTRICTED;
}

/// a live, finished builder of the player's: the Bina tab's chips
static void findBuilderChip( Object *obj, void *userData )
{
	std::vector< ObjectID > *found = (std::vector< ObjectID > *)userData;
	if( obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_DOZER )
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	found->push_back( obj->getID() );
}

struct FactorySearch
{
	Bool infantry;										///< the Piyade tab's factories, else Araç's
	std::vector< ObjectID > *found;
};
static void findFactory( Object *obj, void *userData );

//-------------------------------------------------------------------------------------------------
/** The player picked producers or builders of his in the world: each tab one of them belongs to (a
	* builder Bina, a barracks Piyade, a war factory or airfield Araç) gets exactly those chips lit, and
	* the sidebar shows the shown tab if it is one of them, else the first of them.  Only when the
	* producers in the selection change, so a tab or a chip the player chose afterwards is left alone. */
//-------------------------------------------------------------------------------------------------
static void followSelection( Player *player )
{
	static std::vector< ObjectID > now;
	static std::vector< Object * > objects;
	now.clear();
	objects.clear();
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if( selected != NULL )
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			Object *obj = *it != NULL ? ( *it )->getObject() : NULL;
			if( obj != NULL && obj->getControllingPlayer() == player && ( obj->isKindOf( KINDOF_DOZER ) || obj->getProductionUpdateInterface() != NULL ) )
			{
				now.push_back( obj->getID() );
				objects.push_back( obj );
			}
		}
	if( now == s_followed )
		return;
	s_followed = now;

	std::vector< ObjectID > lit[ SIDEBAR_TAB_COUNT ];
	for( size_t o = 0; o < objects.size(); o++ )
	{
		Object *obj = objects[ o ];
		std::vector< ObjectID > found;
		Int tab = -1;
		if( obj->isKindOf( KINDOF_DOZER ) )
		{
			findBuilderChip( obj, &found );
			tab = found.empty() ? -1 : SIDEBAR_TAB_BUILDINGS;
		}
		else
		{
			FactorySearch infantry = { TRUE, &found };
			findFactory( obj, &infantry );
			if( !found.empty() )
				tab = SIDEBAR_TAB_INFANTRY;
			else
			{
				FactorySearch vehicles = { FALSE, &found };
				findFactory( obj, &vehicles );
				tab = found.empty() ? -1 : SIDEBAR_TAB_VEHICLES;
			}
		}
		if( tab >= 0 )
			lit[ tab ].push_back( obj->getID() );
	}

	Int show = lit[ s_tab ].empty() ? -1 : s_tab;
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		if( lit[ t ].empty() )
			continue;
		if( show < 0 )
			show = t;
		// those past the chips drop out on the refill, and with none left the tab is back on Tümü
		std::sort( lit[ t ].begin(), lit[ t ].end() );
		s_picks[ t ].swap( lit[ t ] );
	}
	if( show >= 0 )
		Sidebar_selectTab( show );
}

/// a finished factory of the player's whose bar offers a unit of the tab's kind
static void findFactory( Object *obj, void *userData )
{
	FactorySearch *s = (FactorySearch *)userData;
	// the command center's dozers stay with Tümü: the chips are the barracks, the war factories and
	// the airfields
	if( obj->isEffectivelyDead() || obj->getProductionUpdateInterface() == NULL || obj->isKindOf( KINDOF_COMMANDCENTER )
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ) )
		return;
	const CommandSet *set = TheControlBar->findCommandSet( obj->getCommandSetString() );
	if( set == NULL )
		return;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; i++ )
	{
		const CommandButton *button = set->getCommandButton( i );
		if( button == NULL || button->getThingTemplate() == NULL
				|| !GroupPanel_isPlayerBuildButton( button->getCommandType(), button->getOptions(), TheControlBar->hasCommandWindow( i ) )
				|| button->getThingTemplate()->isKindOf( KINDOF_INFANTRY ) != s->infantry
				|| TheControlBar->getButtonAvailability( button, obj ) == COMMAND_HIDDEN )
			continue;
		s->found->push_back( obj->getID() );
		return;
	}
}

static void gatherProgress( Object *obj, void *userData )
{
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL || obj->isEffectivelyDead() )
		return;
	const ProductionEntry *head = pu->firstProduction();
	for( const ProductionEntry *p = head; p; p = pu->nextProduction( p ) )
	{
		// a research under way: its cell's clock (a full scrim while it waits in the queue) and the
		// building to cancel it at
		if( p->getProductionType() == PRODUCTION_UPGRADE && p->getProductionUpgrade() != NULL )
		{
			for( size_t at = 0; at < s_list.size(); at++ )
			{
				if( !isResearch( s_list[ at ] ) || s_list[ at ].button->getUpgradeTemplate() != p->getProductionUpgrade() )
					continue;
				s_count[ at ]++;
				s_clock[ at ] = max( s_clock[ at ], p == head ? REAL_TO_INT( p->getPercentComplete() ) : 0 );
				s_researcher[ at ] = obj->getID();
				break;
			}
			continue;
		}
		if( p->getProductionType() != PRODUCTION_UNIT || p->getProductionObject() == NULL )
			continue;
		for( size_t at = 0; at < s_list.size(); at++ )
		{
			if( s_list[ at ].thing == NULL )
				continue;
			if( s_list[ at ].thing != p->getProductionObject() && !s_list[ at ].thing->isEquivalentTo( p->getProductionObject() ) )
				continue;
			s_count[ at ]++;
			if( p == head )
				s_clock[ at ] = max( s_clock[ at ], REAL_TO_INT( p->getPercentComplete() ) );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void Sidebar_update( void )
{
	if( TheControlBar == NULL || !TheControlBar->isSidebarLayout() )
	{
		if( s_shown )
			Sidebar_hide();
		return;
	}
	// the General key, only while there is something to spend
	TheControlBar->updateSidebarGeneralKey();
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( !s_shown || !boxAlive() || player == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
	{
		s_cursorMode = SIDEBAR_CURSOR_NONE;
		return;
	}

	// a structure to place, a targeting command or a signal taken up since puts the mode down
	if( s_cursorMode != SIDEBAR_CURSOR_NONE && ( TheInGameUI->getPendingPlaceType() != NULL
			|| TheInGameUI->getGUICommand() != NULL || TheInGameUI->isSignalArmed() ) )
		s_cursorMode = SIDEBAR_CURSOR_NONE;

	// the tab icons for this player's side, once a player
	if( s_iconPlayer != player )
	{
		s_iconPlayer = player;
		const AsciiString side = player->getSide();
		const char *prefix = side.startsWith( "China" ) || side.startsWith( "Boss" ) ? "China"
			: side.startsWith( "GLA" ) ? "Gla" : side.startsWith( "Turkiye" ) ? "Turkiye" : "America";
		for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		{
			AsciiString name;
			name.format( "%s%s", prefix, TAB_ICONS[ t ] );
			s_tabIcon[ t ] = TheMappedImageCollection->findImageByName( name );
			if( s_tabIcon[ t ] == NULL )
			{
				name.format( "America%s", TAB_ICONS[ t ] );
				s_tabIcon[ t ] = TheMappedImageCollection->findImageByName( name );
			}
		}
	}

	// each tab's name and the key the command map gives it now, so a key moved on Options > Hotkeys in
	// a match shows from the next frame; rewritten only when it changed
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		if( s_tabs[ t ] == NULL )
			continue;
		const UnicodeString tip = ControlBar_withMetaKey( TheGameText->fetch( TAB_LABELS[ t ] ), TAB_METAS[ t ] );
		if( tip.compare( s_tabs[ t ]->winGetInstanceData()->getTooltipText() ) != 0 )
			s_tabs[ t ]->winSetTooltip( tip );
		const UnicodeString letter = keyLetter( TAB_METAS[ t ] );
		if( s_keyText[ t ] != NULL && letter.compare( s_keyShown[ t ] ) != 0 )
		{
			s_keyShown[ t ] = letter;
			s_keyText[ t ]->setText( letter );
		}
	}

	// producers or builders of the player's newly selected in the world: their tabs and their chips
	followSelection( player );

	// the structure on the cursor no longer the lit builders' once the placement is over
	if( TheInGameUI->getPendingPlaceType() == NULL )
	{
		s_placingBuilders.clear();
		s_namedBuilders.clear();
	}

	// once a logic frame, or at once when the tab changed: nothing read here changes in between
	const UnsignedInt frame = TheGameLogic->getFrame();
	// the show order starts again for a new player or a clock that ran back (a load), or once the bar
	// was made again (makeWindows empties the countries)
	if( s_countries.empty() || s_orderPlayer != player || frame < s_seenFrame )
	{
		resetShowOrder( player );
		s_orderPlayer = player;
		s_dirty = TRUE;
	}
	if( s_dirty || !s_listed || s_listFrame != frame || s_listPlayer != player )
	{
		fillList( player );
		watchOtherTabs( player, frame );
		s_factories.clear();
		if( s_tab == SIDEBAR_TAB_BUILDINGS )
			player->iterateObjects( findBuilderChip, &s_factories );		// the builders
		else
		{
			player->iterateObjects( gatherProgress, NULL );						// units queued, research under way
			if( s_tab != SIDEBAR_TAB_POWERS )
			{
				// the picker's factories
				FactorySearch search = { s_tab == SIDEBAR_TAB_INFANTRY, &s_factories };
				player->iterateObjects( findFactory, &search );
			}
		}

		// sorted by ObjectID; a lit producer drops out once it is gone or off the chips, and with none
		// left the tab is back on Tümü
		std::sort( s_factories.begin(), s_factories.end() );
		const size_t chips = min( s_factories.size(), (size_t)HUD_SIDEBAR_PICKER_CHIPS );
		std::vector< ObjectID > &picks = s_picks[ s_tab ];
		for( size_t p = picks.size(); p-- > 0; )
			if( std::find( s_factories.begin(), s_factories.begin() + chips, picks[ p ] ) == s_factories.begin() + chips )
				picks.erase( picks.begin() + p );

		{
			// each chip's building or builder: its picture for the draw, its name for the tooltip
			for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
			{
				Object *factory = i < (Int)chips ? TheGameLogic->findObjectByID( s_factories[ i ] ) : NULL;
				// the building's own picture (the owner's pick); the artist's kind glyph only for one without
				s_chipIcon[ i ] = factory != NULL ? factory->getTemplate()->getButtonImage() : NULL;
				s_chipGlyph[ i ] = FALSE;
				if( factory != NULL && s_chipIcon[ i ] == NULL )
				{
					const char *kind = factory->isKindOf( KINDOF_FS_AIRFIELD ) ? "ChipIconAirfield"
						: factory->isKindOf( KINDOF_FS_BARRACKS ) ? "ChipIconBarracks"
						: factory->isKindOf( KINDOF_FS_WARFACTORY ) ? "ChipIconFactory" : "ChipIconGeneric";
					s_chipIcon[ i ] = Sidebar_image( kind );
					s_chipGlyph[ i ] = s_chipIcon[ i ] != NULL;
				}
				s_chipName[ i ] = factory != NULL ? factory->getTemplate()->getDisplayName() : UnicodeString::TheEmptyString;
			}
		}
		s_listed = TRUE;
		s_dirty = FALSE;
		s_listFrame = frame;
		s_listPlayer = player;
	}

	const Int count = (Int)s_list.size();
	s_row = Sidebar_scroll( count, HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, s_row );
	// with factories or builders lit the cameos read their bars, so what none of them can build is
	// greyed: each cameo as the best of them has it
	static std::vector< Object * > lit;
	lit.clear();
	for( size_t p = 0; p < s_picks[ s_tab ].size(); p++ )
	{
		Object *obj = TheGameLogic->findObjectByID( s_picks[ s_tab ][ p ] );
		if( obj != NULL )
			lit.push_back( obj );
	}
	for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
	{
		GameWindow *win = s_cameos[ i ];
		if( win == NULL )
			continue;
		const Int at = s_row * HUD_SIDEBAR_CAMEO_COLUMNS + i;
		if( at >= count )
		{
			win->winHide( TRUE );
			GadgetButtonSetData( win, NULL );
			continue;
		}

		const Buildable &entry = s_list[ at ];
		if( GadgetButtonGetData( win ) != (void *)entry.button )
		{
			GadgetButtonSetEnabledImage( win, entry.button->getButtonImage() );
			GadgetButtonSetData( win, (void *)entry.button );
			fitCameo( i, entry.button->getButtonImage() );
			// a power's gold ready number goes with the power
			GadgetButtonSetText( win, UnicodeString() );
		}
		win->winClearStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );
		if( isPower( entry ) )
		{
			// a power cell reads as the tray's button does, its recharge clock drawn on it by the bar,
			// which finds a shortcut power's holder itself; only a select-all is handed its unit
			GadgetButtonClearClock( win );
			const Bool selectAll = entry.button->getCommandType() == GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE;
			Object *holder = selectAll ? powerHolder( player, entry.button ) : NULL;
			GroupPanel_showAvailability( win, TheControlBar->getPowerCellAvailability( entry.button, holder, win ) );
			// how many of the power's superweapons are ready, gold, as the tray's button shows it
			GadgetButtonSetCount( win, 0 );
			TheControlBar->setShortcutReadyCount( win, entry.button );
			win->winHide( FALSE );
			continue;
		}
		// a research cell keeps its own building's reading: the lit builders have no research
		CommandAvailability availability = entry.best;
		if( !isResearch( entry ) )
			for( size_t l = 0; l < lit.size(); l++ )
			{
				const CommandAvailability now = pickedAvailability( lit[ l ], entry.thing );
				if( l == 0 || ControlBar::commandAvailabilityRank( now ) > ControlBar::commandAvailabilityRank( availability ) )
					availability = now;
			}
		GroupPanel_showAvailability( win, availability );

		// a greyed cameo with units queued still takes the right click that cancels them
		if( s_count[ at ] > 0 )
			win->winSetStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );
		if( s_clock[ at ] >= 0 )
			GadgetButtonDrawInverseClock( win, s_clock[ at ], CLOCK_COLOR );
		else
			GadgetButtonClearClock( win );
		GadgetButtonSetCount( win, isResearch( entry ) ? 0 : s_count[ at ] );
		win->winHide( FALSE );
	}

	if( s_arrowUp != NULL )
		s_arrowUp->winEnable( s_row > 0 );
	if( s_arrowDown != NULL )
		s_arrowDown->winEnable( s_row < Sidebar_scroll( count, HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, count ) );

	// the picker on every tab but the powers': Tümü, and a chip a factory (a builder on Bina), the
	// first HUD_SIDEBAR_PICKER_CHIPS by ObjectID
	if( s_chipAll != NULL )
		s_chipAll->winHide( s_tab == SIDEBAR_TAB_POWERS );
	for( Int i = 0; i < HUD_SIDEBAR_PICKER_CHIPS; i++ )
		if( s_chips[ i ] != NULL )
			s_chips[ i ]->winHide( i >= (Int)s_factories.size() );
}
