// The Red Alert style sidebar (ControlBarSidebar.h, docs/plans/HUD-SIDEBAR-LAYOUT.md lane C): four
// tabs, twelve cameos two to a row and two arrows, all in one box window that is a child of
// ControlBarParent and stays the first of its children, so it draws over BackgroundMarker's steel and
// takes the pointer before the sidebar's blocker.
//
// Bina and Savunma list what the player's builders put up (GroupPanel_gatherConstructible), split on
// KINDOF_FS_BASE_DEFENSE; Piyade and Araç what his factories make (GroupPanel_gatherBuildable), split
// on KINDOF_INFANTRY, aircraft under Araç.  A left click on a unit queues it with no squad, through
// the group panel's MSG_QUEUE_UNIT_FOR_GROUP; on a structure it puts the structure on the cursor for a
// builder that offers it, as the bar's own button does.  A right click takes back the units queued
// deepest in their factories' queues, as many as the modifiers would have queued.
//
// Thread: LOGIC-THREAD.md.  The logic is read in Sidebar_update and on the input path only, both
// outside the render window; the windows' state is set there and the draw callbacks read nothing
// but the windows.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/ControlBarSidebar.h"

#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
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
#include "GameClient/MetaEvent.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <algorithm>
#include <vector>

namespace
{
	const Color CLOCK_COLOR		= GameMakeColor( 0, 0, 0, 190 );			///< darker than the queue's 160: a 140 wide cameo
	const Color ARROW_LIT			= GameMakeColor( 220, 228, 236, 255 );
	const Color ARROW_DARK		= GameMakeColor( 90, 96, 104, 255 );
	const Color TAB_TEXT_EDGE	= GameMakeColor( 0, 0, 0, 255 );
	const Color KEY_LETTER		= GameMakeColor( 255, 210, 60, 255 );	///< the bar's hotkey letters' yellow
	const Real KEY_POINTS			= 8.0f;		///< the key letter in a tab's top left corner, at 1080
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

static void drawNothing( GameWindow *window, WinInstanceData *instData )
{
}

static void buildTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	TheControlBar->showBuildTooltipLayout( window );
}

/// the plain button with a chevron on it, lit while there is somewhere to go
static void drawArrow( GameWindow *window, WinInstanceData *instData )
{
	if( s_buttonDraw != NULL )
		s_buttonDraw( window, instData );

	ICoord2D at, size;
	window->winGetScreenPosition( &at.x, &at.y );
	window->winGetSize( &size.x, &size.y );
	const Int half = max( size.y / 5, 2 );
	const Int cx = at.x + size.x / 2, cy = at.y + size.y / 2;
	const Bool up = window == s_arrowUp;
	const Int tip = up ? cy - half : cy + half, back = up ? cy + half : cy - half;
	const Color c = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) ? ARROW_LIT : ARROW_DARK;
	TheDisplay->drawLine( cx - 2 * half, back, cx, tip, 2.0f, c );
	TheDisplay->drawLine( cx, tip, cx + 2 * half, back, 2.0f, c );
}

/// a tab, all drawn here: a steel body with a 1 px bevel, raised when closed and pressed in and lit
/// when open, its icon in the middle and the key it answers to in the top left corner; the name is in
/// the tooltip
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

	const Bool open = t == s_tab;
	const Bool hover = BitTest( instData->getState(), WIN_STATE_HILITED );
	const Color face = open ? TAB_STEEL_OPEN : hover ? TAB_STEEL_HOVER : TAB_STEEL;
	const Color lit = open ? TAB_BEVEL_DARK : TAB_BEVEL_LIGHT;
	const Color shade = open ? TAB_BEVEL_LIGHT : TAB_BEVEL_DARK;
	TheDisplay->drawFillRect( at.x, at.y, size.x, size.y, face );
	TheDisplay->drawFillRect( at.x, at.y, size.x, 1, lit );
	TheDisplay->drawFillRect( at.x, at.y + 1, 1, size.y - 1, lit );
	TheDisplay->drawFillRect( at.x + 1, at.y + size.y - 1, size.x - 1, 1, shade );
	TheDisplay->drawFillRect( at.x + size.x - 1, at.y + 1, 1, size.y - 2, shade );

	// pressed in, the face's contents sit a pixel down and right
	const Int push = open ? 1 : 0;
	// the icon centred in the face, as large as fits with its own shape kept
	const Image *icon = s_tabIcon[ t ];
	const Int roomW = size.x - 2 * ( 1 + ICON_INSET ), roomH = size.y - 2 * ( 1 + ICON_INSET );
	if( icon != NULL && icon->getImageWidth() > 0 && icon->getImageHeight() > 0 && roomW > 0 && roomH > 0 )
	{
		const Real fit = min( (Real)roomW / icon->getImageWidth(), (Real)roomH / icon->getImageHeight() );
		const Int w = max( REAL_TO_INT( icon->getImageWidth() * fit ), 1 );
		const Int h = max( REAL_TO_INT( icon->getImageHeight() * fit ), 1 );
		const Int x = at.x + ( size.x - w ) / 2 + push, y = at.y + ( size.y - h ) / 2 + push;
		TheDisplay->drawImage( icon, x, y, x + w, y + h, open ? 0xFFFFFFFF : TAB_ICON_DIM );
	}
	if( s_keyText[ t ] != NULL && !s_keyShown[ t ].isEmpty() )
		s_keyText[ t ]->draw( at.x + 3 + push, at.y + 1 + push, KEY_LETTER, TAB_TEXT_EDGE );
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
static void clickCameo( const CommandButton *command, Bool right )
{
	const ThingTemplate *thing = command->getThingTemplate();
	if( thing == NULL || ThePlayerList->getLocalPlayer() == NULL )
		return;

	if( command->getCommandType() == GUI_COMMAND_UNIT_BUILD )
	{
		if( right )
			cancelNewest( thing );
		else
			GroupPanel_queueUnit( thing, -1 );
	}
	else if( command->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT && !right )
	{
		Object *builder = builderFor( thing );
		if( builder != NULL )
			ControlBar::startStructurePlacement( command, builder );
	}
}

static WindowMsgHandledType boxSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg != GBM_SELECTED && msg != GBM_SELECTED_RIGHT )
		return GameWinDefaultSystem( window, msg, mData1, mData2 );

	GameWindow *control = (GameWindow *)mData1;
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		if( control == s_tabs[ t ] )
		{
			if( msg == GBM_SELECTED )
				Sidebar_selectTab( t );
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
		clickCameo( command, msg == GBM_SELECTED_RIGHT );
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

static GameWindow *makeButton( UnsignedInt status )
{
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	// GadgetPushButtonInput reads the right click bit off the instance data, not the window's status
	if( BitTest( status, WIN_STATUS_RIGHT_CLICK ) )
		BitSet( inst.m_status, WIN_STATUS_RIGHT_CLICK );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( s_box, status | WIN_STATUS_ENABLED, 0, 0, 1, 1, &inst, NULL, TRUE );
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
	s_box->winSetDrawFunc( drawNothing );
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
	}

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

//-------------------------------------------------------------------------------------------------
void Sidebar_layout( const HudLayoutRects &rects )
{
	GameWindow *parent = barParent();
	if( parent == NULL || !makeWindows( parent ) )
		return;

	// first of ControlBarParent's children: drawn last, over BackgroundMarker's sidebar steel, and
	// asked first for the pointer
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

	ICoord2D origin;
	parent->winGetScreenPosition( &origin.x, &origin.y );
	place( s_box, box, origin );
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
		place( s_tabs[ t ], rects.tabs[ t ], box.lo );
	for( Int i = 0; i < HUD_SIDEBAR_CAMEOS; i++ )
		place( s_cameos[ i ], rects.cameos[ i ], box.lo );
	place( s_arrowUp, rects.arrowUp, box.lo );
	place( s_arrowDown, rects.arrowDown, box.lo );

	GameFont *keyFont = TheFontLibrary->getFont( "Arial", max( REAL_TO_INT( KEY_POINTS * rects.scale ), 6 ), TRUE );
	for( Int t = 0; t < SIDEBAR_TAB_COUNT; t++ )
	{
		if( s_keyText[ t ] != NULL && keyFont != NULL )
			s_keyText[ t ]->setFont( keyFont );
	}

	s_shown = TRUE;
	s_dirty = TRUE;
	s_iconPlayer = NULL;	// a new match keeps the slot's Player but may change its side
	s_box->winHide( FALSE );
}

//-------------------------------------------------------------------------------------------------
void Sidebar_hide( void )
{
	s_shown = FALSE;
	if( boxAlive() )
		s_box->winHide( TRUE );
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
	return tab == SIDEBAR_TAB_BUILDINGS || tab == SIDEBAR_TAB_DEFENSE;
}

static void fillList( Player *player )
{
	const Bool structures = structureTab( s_tab );
	if( structures )
		GroupPanel_gatherConstructible( player, s_all );
	else
		GroupPanel_gatherBuildable( player, s_all );

	s_list.clear();
	for( size_t i = 0; i < s_all.size(); i++ )
	{
		const ThingTemplate *thing = s_all[ i ].thing;
		const Bool pick = structures ? thing->isKindOf( KINDOF_FS_BASE_DEFENSE ) == ( s_tab == SIDEBAR_TAB_DEFENSE )
			: thing->isKindOf( KINDOF_INFANTRY ) == ( s_tab == SIDEBAR_TAB_INFANTRY );
		if( pick )
			s_list.push_back( s_all[ i ] );
	}
	s_count.assign( s_list.size(), 0 );
	s_clock.assign( s_list.size(), -1 );

	if( s_tab != s_loggedTab || (Int)s_list.size() != s_loggedCount )
	{
		s_loggedTab = s_tab;
		s_loggedCount = (Int)s_list.size();
		DEBUG_LOG(( "SIDEBAR tab %d cells %d\n", s_tab, s_loggedCount ));
	}
}

/// one factory's queue into s_count and s_clock: every queued unit of a listed type counts, the head
/// alone carries progress
static void gatherProgress( Object *obj, void *userData )
{
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if( pu == NULL || obj->isEffectivelyDead() )
		return;
	const ProductionEntry *head = pu->firstProduction();
	for( const ProductionEntry *p = head; p; p = pu->nextProduction( p ) )
	{
		if( p->getProductionType() != PRODUCTION_UNIT || p->getProductionObject() == NULL )
			continue;
		for( size_t at = 0; at < s_list.size(); at++ )
		{
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
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( !s_shown || !boxAlive() || player == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame() )
		return;

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

	// once a logic frame, or at once when the tab changed: nothing read here changes in between
	const UnsignedInt frame = TheGameLogic->getFrame();
	if( s_dirty || !s_listed || s_listFrame != frame || s_listPlayer != player )
	{
		fillList( player );
		if( !structureTab( s_tab ) )
			player->iterateObjects( gatherProgress, NULL );
		s_listed = TRUE;
		s_dirty = FALSE;
		s_listFrame = frame;
		s_listPlayer = player;
	}

	const Int count = (Int)s_list.size();
	s_row = Sidebar_scroll( count, HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, s_row );
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
		}
		GroupPanel_showAvailability( win, entry.best );

		// a greyed cameo with units queued still takes the right click that cancels them
		win->winClearStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );
		if( s_count[ at ] > 0 )
			win->winSetStatus( WIN_STATUS_CANCEL_WHEN_DISABLED );
		if( s_clock[ at ] >= 0 )
			GadgetButtonDrawInverseClock( win, s_clock[ at ], CLOCK_COLOR );
		else
			GadgetButtonClearClock( win );
		GadgetButtonSetCount( win, s_count[ at ] );
		win->winHide( FALSE );
	}

	if( s_arrowUp != NULL )
		s_arrowUp->winEnable( s_row > 0 );
	if( s_arrowDown != NULL )
		s_arrowDown->winEnable( s_row < Sidebar_scroll( count, HUD_SIDEBAR_CAMEO_COLUMNS, HUD_SIDEBAR_CAMEO_ROWS, count ) );
}
