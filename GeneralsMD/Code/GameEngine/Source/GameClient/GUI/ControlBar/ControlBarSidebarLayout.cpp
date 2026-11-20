// The Red Alert style sidebar's half of ControlBar (docs/plans/HUD-SIDEBAR-LAYOUT.md, phase 1 lane B):
// which seat gets it, and ControlBar.wnd's own windows put on HudLayoutRects' cells after layoutPanels'
// Classic pass.  The tabs, cameos and arrows are ControlBarSidebar.cpp's; nothing here is a pixel of its
// own, every rectangle comes from HudLayoutForScreen (GameClient/Display.h).  Client only.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FunctionLexicon.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/OptionsCatalog.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/ControlBarSidebar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"

extern WindowMsgHandledType ControlBar_portraitBarInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 );	// ControlBarMultiSelect.cpp

/// the PowerWindow's draw before the sidebar stood it on end, W3DPowerDraw from ControlBar.wnd
static GameWinDrawFunc s_classicPowerDraw = NULL;
/// the right HUD's before the sidebar emptied it, W3DRightHUDDraw from ControlBar.wnd
static GameWinDrawFunc s_classicRightHUDDraw = NULL;
/// the portrait parent's before the sidebar gave it the card's readout (the .wnd's default draw)
static GameWinDrawFunc s_classicUnitSelectedDraw = NULL;
static GameWinInputFunc s_classicUnitSelectedInput = NULL;	///< and its input, GameWinBlockInput from ControlBar.wnd
static Bool s_unitSelectedImageCleared = FALSE;
static Bool s_queueImageCleared = FALSE;		///< the production queue's Classic well art, off in the card's row

//-------------------------------------------------------------------------------------------------
/** What the card says beside the portrait, copied from the selection in ControlBar::update (the client
	* pass, outside the render window, where the bar reads the logic for everything else) and drawn from
	* the copy, so the draw never touches an Object. */
//-------------------------------------------------------------------------------------------------
struct SelectionCardReadout
{
	Bool shown;
	UnicodeString name;
	Real health;					///< 0..1, below 0 for none (a multi-selection, a body without health)
	const Image *rank;		///< the veterancy chevron, NULL for a regular
};
static SelectionCardReadout s_card = { FALSE, UnicodeString(), -1.0f, NULL };
static DisplayString *s_cardName = NULL;

/// the head of a factory's queue, 0..1, below 0 while the tray holds no queue (updateBuildQueue)
static Real s_queueProgress = -1.0f;
void SidebarCard_setQueueProgress( Real progress )
{
	s_queueProgress = progress;
}

//-------------------------------------------------------------------------------------------------
/** WinUnitSelected's draw under the sidebar: the name on the label plate, the rank's chevrons under the
	* portrait, the health groove, and the queue head's progress bar at its tile's foot.  The portrait,
	* the upgrade cameos and the queue's buttons are windows and draw themselves; this one draws after
	* them (a newer child of the right HUD than the queue), so the bar stands on its tile. */
//-------------------------------------------------------------------------------------------------
static void drawSelectionCard( GameWindow *window, WinInstanceData *instData )
{
	if( !s_card.shown || TheControlBar == NULL || TheDisplay == NULL )
		return;
	HudLayoutRects hud;
	TheControlBar->getHudLayoutRects( &hud );
	const Real k = hud.scale;

	// the name: one bold line, left-aligned, clipped to its rect
	const IRegion2D &text = hud.nameText;
	if( s_cardName == NULL )
		s_cardName = TheDisplayStringManager->newDisplayString();
	if( s_cardName != NULL && text.width() > 4 )
	{
		GameFont *font = TheFontLibrary->getFont( "Arial", MAX( 7, REAL_TO_INT( 11 * k + 0.5f ) ), TRUE );
		if( s_cardName->getFont() != font )
			s_cardName->setFont( font );
		s_cardName->setWordWrap( 0 );
		if( s_cardName->getText().compare( s_card.name ) != 0 )
			s_cardName->setText( s_card.name );
		IRegion2D clip = text;
		s_cardName->setClipRegion( &clip );
		Int w, h;
		s_cardName->getSize( &w, &h );
		s_cardName->draw( text.lo.x, text.lo.y + ( text.height() - h ) / 2, GameMakeColor( 245, 245, 245, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
	}

	// the rank's chevrons, their own shape, centred in their rect
	const IRegion2D &rank = hud.rankRect;
	if( s_card.rank != NULL && s_card.rank->getImageHeight() > 0 && rank.height() > 0 )
	{
		Int h = rank.height();
		Int w = h * s_card.rank->getImageWidth() / s_card.rank->getImageHeight();
		if( w > rank.width() )
		{
			w = rank.width();
			h = w * s_card.rank->getImageHeight() / s_card.rank->getImageWidth();
		}
		const Int x = rank.lo.x + ( rank.width() - w ) / 2, y = rank.lo.y + ( rank.height() - h ) / 2;
		TheDisplay->drawImage( s_card.rank, x, y, x + w, y + h );
	}

	// the health groove, its fill a pixel inside, green to yellow to red as the stock health bars go
	const IRegion2D &bar = hud.healthBar;
	if( s_card.health >= 0.0f && bar.width() > 2 && bar.height() > 2 )
	{
		const Real health = MIN( 1.0f, s_card.health );
		const Color fill = health > 0.5f ? GameMakeColor( 40, 210, 70, 255 )
			: ( health > 0.25f ? GameMakeColor( 230, 220, 0, 255 ) : GameMakeColor( 230, 0, 0, 255 ) );
		TheDisplay->drawFillRect( bar.lo.x, bar.lo.y, bar.width(), bar.height(), GameMakeColor( 0, 0, 0, 200 ) );
		const Int filled = REAL_TO_INT( ( bar.width() - 2 ) * health + 0.5f );
		if( filled > 0 )
			TheDisplay->drawFillRect( bar.lo.x + 1, bar.lo.y + 1, filled, bar.height() - 2, fill );
	}

	// the queue's head: a 3 high bar along its tile's foot, how far it has come
	if( s_queueProgress >= 0.0f )
	{
		const IRegion2D &cell = hud.trayCells[ 0 ];
		const Int barH = MAX( 2, REAL_TO_INT( 3 * k + 0.5f ) );
		TheDisplay->drawFillRect( cell.lo.x, cell.hi.y - barH, cell.width(), barH, GameMakeColor( 0, 0, 0, 220 ) );
		const Int filled = REAL_TO_INT( cell.width() * MIN( 1.0f, s_queueProgress ) + 0.5f );
		if( filled > 0 )
			TheDisplay->drawFillRect( cell.lo.x, cell.hi.y - barH, filled, barH, GameMakeColor( 255, 200, 40, 255 ) );
	}
}

/// WinUnitSelected spans the card under the sidebar: a click on a type tile it does not cover with a
/// child goes on to the right HUD (portraitBarInput), not into the .wnd's GameWinBlockInput
static WindowMsgHandledType passToParent( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::updateSelectionCardReadout( void )
{
	s_card.shown = FALSE;
	if( !m_sidebarLayout )
		return;

	// the queue's bar only while the queue stands in the tray (updateBuildQueue sets it while it does)
	if( m_contextParent[ CP_BUILD_QUEUE ] == NULL || m_contextParent[ CP_BUILD_QUEUE ]->winIsHidden() )
		s_queueProgress = -1.0f;

	// a multi-selection: the focused type's name over its portrait, no rank or health of one unit
	if( m_currContext == CB_CONTEXT_MULTI_SELECT )
	{
		if( m_multiSelectFocus < 0 || m_multiSelectFocus >= m_multiSelectGroupCount )
			return;
		// the focused type and how many of it the selection holds, "Crusader x4"
		s_card.name.format( u"%ls  x%d", m_multiSelectGroupTemplate[ m_multiSelectFocus ]->getDisplayName().str(),
			m_multiSelectGroupSize[ m_multiSelectFocus ] );
		// the whole selection's health: its units' health summed over their maximums summed
		Real health = 0.0f, most = 0.0f;
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			const Object *member = *it ? ( *it )->getObject() : NULL;
			const BodyModuleInterface *body = member ? member->getBodyModule() : NULL;
			if( body == NULL || body->getMaxHealth() <= 0.0f )
				continue;
			health += body->getHealth();
			most += body->getMaxHealth();
		}
		s_card.health = most > 0.0f ? health / most : -1.0f;
		s_card.rank = NULL;
		s_card.shown = TRUE;
		return;
	}

	Drawable *draw = m_currentSelectedDrawable;
	const Object *obj = draw ? draw->getObject() : NULL;
	if( obj == NULL || TheInGameUI->getSelectCount() != 1 )
		return;

	// the drawable's template: a disguised enemy reads as what it wears, as its portrait does
	s_card.name = draw->getTemplate()->getDisplayName();
	const BodyModuleInterface *body = obj->getBodyModule();
	s_card.health = body && body->getMaxHealth() > 0.0f ? body->getHealth() / body->getMaxHealth() : -1.0f;
	s_card.rank = calculateVeterancyOverlayForObject( obj );
	s_card.shown = TRUE;
}

//-------------------------------------------------------------------------------------------------
static GameWindow *barWindow( const char *name )
{
	AsciiString full;
	full.format( "ControlBar.wnd:%s", name );
	return TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( full ) );
}

//-------------------------------------------------------------------------------------------------
/** A cell's button inside the card art's well: `side` design pixels in on the left, top and right, and
	* `foot` on the bottom (0 leaves it standing on the cell's foot). */
//-------------------------------------------------------------------------------------------------
IRegion2D SidebarWellInset( const IRegion2D &cell, Real k, Int side, Int foot )
{
	const Int s = MAX( 1, REAL_TO_INT( side * k + 0.5f ) );
	const Int f = foot > 0 ? MAX( 1, REAL_TO_INT( foot * k + 0.5f ) ) : 0;
	IRegion2D r = cell;
	r.lo.x += s;
	r.lo.y += s;
	r.hi.x -= s;
	r.hi.y -= f;
	return r;
}

/// a tray tile's cameo: 2 in from its well's edges at 1080 and the art's 5:4 centred in what is left
static IRegion2D trayCameo( const IRegion2D &cell, Real k )
{
	return HudFitAspect( SidebarWellInset( cell, k, 2, 2 ), HUD_CAMEO_ART_W, HUD_CAMEO_ART_H );
}

//-------------------------------------------------------------------------------------------------
static IRegion2D screenRect( GameWindow *window )
{
	IRegion2D r;
	window->winGetScreenPosition( &r.lo.x, &r.lo.y );
	window->winGetSize( &r.hi.x, &r.hi.y );
	r.hi.x += r.lo.x;
	r.hi.y += r.lo.y;
	return r;
}

//-------------------------------------------------------------------------------------------------
/** Everything under `window` moved from `from` (the window's rectangle before) to `origin` at scale
	* `s`, through placeWindowAt so the placement record follows.  placeWindowAt leaves the children
	* standing on screen, so each child's rectangle read here is still the one it had before. */
//-------------------------------------------------------------------------------------------------
static void fitChildren( ControlBar *bar, GameWindow *window, const IRegion2D &from, const ICoord2D &origin, Real s )
{
	for( GameWindow *child = window->winGetChild(); child; child = child->winGetNext() )
	{
		const IRegion2D was = screenRect( child );
		IRegion2D now;
		now.lo.x = origin.x + REAL_TO_INT_FLOOR( ( was.lo.x - from.lo.x ) * s );
		now.lo.y = origin.y + REAL_TO_INT_FLOOR( ( was.lo.y - from.lo.y ) * s );
		now.hi.x = origin.x + REAL_TO_INT_CEIL( ( was.hi.x - from.lo.x ) * s );
		now.hi.y = origin.y + REAL_TO_INT_CEIL( ( was.hi.y - from.lo.y ) * s );
		bar->placeWindowAt( child, now );
		fitChildren( bar, child, from, origin, s );
	}
}

//-------------------------------------------------------------------------------------------------
/** `window` onto `to`, and what it holds scaled with it at one scale, the smaller of the two, from
	* `to`'s top left: a Classic pane (the construction timer, the beacon, the portrait) on a sidebar
	* cell of another shape keeps its own shape. */
//-------------------------------------------------------------------------------------------------
static void fitWindowTree( ControlBar *bar, GameWindow *window, const IRegion2D &to )
{
	if( window == NULL )
		return;
	const IRegion2D from = screenRect( window );
	bar->placeWindowAt( window, to );
	if( from.width() <= 0 || from.height() <= 0 )
		return;
	const Real s = MIN( (Real)to.width() / from.width(), (Real)to.height() / from.height() );
	fitChildren( bar, window, from, to.lo, s );
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::wantsSidebarLayout( void ) const
{
	if( TheGlobalData == NULL || TheGlobalData->m_hudLayout != HUD_LAYOUT_SIDEBAR )
		return FALSE;
	if( TheGameLogic == NULL || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()
			|| TheGameLogic->isInReplayGame() )
		return FALSE;

	// initWindows lays the bar out before it has looked the windows up (and the old tree may still
	// stand, queued for destruction): the Classic pass only, and update() lays the sidebar out after
	if( m_rightHUDWindow == NULL || m_rightHUDUnitSelectParent == NULL || m_contextParent[ CP_COMMAND ] == NULL )
		return FALSE;

	// observers and a defeated player keep the Classic bar until phase 4's observer sidebar
	if( m_isObserverCommandBar )
		return FALSE;
	if( m_controlBarSchemeManager != NULL && m_controlBarSchemeManager->getCurrentSide().compareNoCase( "Observer" ) == 0 )
		return FALSE;
	const Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	return local != NULL && local->isPlayerActive() && !local->isPlayerObserver();
}

//-------------------------------------------------------------------------------------------------
void ControlBar::getHudLayoutRects( HudLayoutRects *out ) const
{
	if( out == NULL )
		return;
	HudLayoutForScreen( TheDisplay ? TheDisplay->getWidth() : 0, TheDisplay ? TheDisplay->getHeight() : 0,
		m_sidebarLayout ? HUD_LAYOUT_SIDEBAR : HUD_LAYOUT_CLASSIC,
		TheGlobalData ? TheGlobalData->m_hudWidth : (Int)HUD_WIDTH_FULL, out );
}

//-------------------------------------------------------------------------------------------------
void ControlBar::hideForSidebar( GameWindow *window )
{
	if( window == NULL || window->winIsHidden() )
		return;
	window->winHide( TRUE );
	m_sidebarHidden.push_back( window );
}

//-------------------------------------------------------------------------------------------------
/** Where a command stands on the strip: the generic group (Attack Move, Stop, Guard and its kinds, a
	* building's Rally Point and Sell) at
	* the right end in that order, a container's slots (exit, evacuate) just before them, and everything
	* else, the unit's own, from the left. */
//-------------------------------------------------------------------------------------------------
enum StripRole { STRIP_OWN, STRIP_CONTAINER, STRIP_ATTACK_MOVE, STRIP_STOP, STRIP_GUARD, STRIP_RALLY, STRIP_SELL };

static StripRole stripRole( const CommandButton *command )
{
	if( command == NULL )
		return STRIP_OWN;
	switch( command->getCommandType() )
	{
		case GUI_COMMAND_ATTACK_MOVE:									return STRIP_ATTACK_MOVE;
		case GUI_COMMAND_STOP:												return STRIP_STOP;
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:			return STRIP_GUARD;
		case GUI_COMMAND_SET_RALLY_POINT:							return STRIP_RALLY;
		case GUI_COMMAND_SELL:												return STRIP_SELL;
		case GUI_COMMAND_EXIT_CONTAINER:
		case GUI_COMMAND_EVACUATE:										return STRIP_CONTAINER;
		default:																			return STRIP_OWN;
	}
}

//-------------------------------------------------------------------------------------------------
/** The strip's fourteen cells by role instead of by ControlBar.wnd's slot: the Classic grid's slots
	* left gaps all along it.  The unit's own commands from the left in their command set order, the
	* generic group packed against the right end, the container's slots right-aligned just left of it,
	* the hidden windows in the cells left over.  Only where each window stands changes: the slot, and
	* with it the key a button answers to and the letter it shows, is the window's.  Run after every
	* layout and context switch, and every frame (a transport's or a garrison's slots come and go
	* without one); placeWindowAt does nothing for a window already in its cell. */
//-------------------------------------------------------------------------------------------------
void ControlBar::arrangeStripByRole( void )
{
	if( !m_sidebarLayout )
		return;
	HudLayoutRects hud;
	getHudLayoutRects( &hud );

	enum { CELLS = HUD_STRIP_BUTTONS };
	Int own[ CELLS ], container[ CELLS ], generic[ CELLS ], hidden[ CELLS ];
	Int owns = 0, containers = 0, generics = 0, hiddens = 0;
	for( Int i = 0; i < CELLS && i < MAX_COMMANDS_PER_SET; i++ )
	{
		GameWindow *win = m_commandWindows[ i ];
		if( win == NULL )
			continue;
		if( win->winIsHidden() )
		{
			hidden[ hiddens++ ] = i;
			continue;
		}
		const StripRole role = stripRole( (const CommandButton *)GadgetButtonGetData( win ) );
		if( role == STRIP_OWN )
			own[ owns++ ] = i;
		else if( role == STRIP_CONTAINER )
			container[ containers++ ] = i;
		else
		{
			// Attack Move, Stop, Guard, Rally Point, Sell: kept in that order whatever the command set's
			Int at = generics++;
			while( at > 0 && stripRole( (const CommandButton *)GadgetButtonGetData( m_commandWindows[ generic[ at - 1 ] ] ) ) > role )
			{
				generic[ at ] = generic[ at - 1 ];
				at--;
			}
			generic[ at ] = i;
		}
	}

	Int cellOf[ CELLS ];
	Bool taken[ CELLS ];
	for( Int c = 0; c < CELLS; c++ )
	{
		cellOf[ c ] = -1;
		taken[ c ] = FALSE;
	}
	for( Int n = 0; n < owns; n++ )
		cellOf[ own[ n ] ] = n;
	for( Int n = 0; n < generics; n++ )
		cellOf[ generic[ n ] ] = CELLS - generics + n;
	for( Int n = 0; n < containers; n++ )
		cellOf[ container[ n ] ] = CELLS - generics - containers + n;
	for( Int c = 0; c < CELLS; c++ )
		if( cellOf[ c ] >= 0 )
			taken[ cellOf[ c ] ] = TRUE;
	Int spare = 0;
	for( Int n = 0; n < hiddens; n++ )
	{
		while( spare < CELLS && taken[ spare ] )
			spare++;
		if( spare < CELLS )
		{
			cellOf[ hidden[ n ] ] = spare;
			taken[ spare ] = TRUE;
		}
	}

	for( Int i = 0; i < CELLS && i < MAX_COMMANDS_PER_SET; i++ )
		if( m_commandWindows[ i ] != NULL && cellOf[ i ] >= 0 )
			placeWindowAt( m_commandWindows[ i ], hud.stripButtons[ cellOf[ i ] ] );
}

//-------------------------------------------------------------------------------------------------
// The sidebar's own keys: the side's <Prefix>Sidebar<Key> art (the HUD artist's sheet, Sidebar_image)
// on the bar's Options, Diplomacy and General buttons, each in its four states, and the scheme's Classic
// images put back for the Classic bar.  A scheme set since (a defeat into the observer's) has given the
// button its own images already, so a key is only put back while it still wears the sidebar's.
//-------------------------------------------------------------------------------------------------
enum { SIDEBAR_KEY_MENU, SIDEBAR_KEY_DIPLOMACY, SIDEBAR_KEY_GENERAL, SIDEBAR_KEY_COUNT };
static const char *const s_keyPiece[ SIDEBAR_KEY_COUNT ] = { "KeyMenu", "KeyDiplomacy", "KeyGeneral" };
static const char *const s_keyWindow[ SIDEBAR_KEY_COUNT ] = { "ButtonOptions", "PopupCommunicator", "ButtonGeneral" };
enum { KEY_ENABLED, KEY_HILITE, KEY_PUSHED, KEY_DISABLED, KEY_STATES };
static const Image *s_keyClassic[ SIDEBAR_KEY_COUNT ][ KEY_STATES ];
static const Image *s_keyWorn[ SIDEBAR_KEY_COUNT ];		///< the sidebar art a key was given, NULL while it wears its own
static const Image *s_keyWornLit[ SIDEBAR_KEY_COUNT ];	///< and its lit state, which the General key flashes to (getStarImage)

/// one key's four states, "" "H" "P" "I"; FALSE when the sheet has no such key
static Bool sidebarKeyImages( const char *piece, const Image **out )
{
	out[ KEY_ENABLED ] = Sidebar_image( piece );
	if( out[ KEY_ENABLED ] == NULL )
		return FALSE;
	AsciiString name;
	name.format( "%sH", piece );
	out[ KEY_HILITE ] = Sidebar_image( name.str() );
	if( out[ KEY_HILITE ] == NULL )
		out[ KEY_HILITE ] = out[ KEY_ENABLED ];
	name.format( "%sP", piece );
	out[ KEY_PUSHED ] = Sidebar_image( name.str() );
	if( out[ KEY_PUSHED ] == NULL )
		out[ KEY_PUSHED ] = out[ KEY_HILITE ];
	name.format( "%sI", piece );
	out[ KEY_DISABLED ] = Sidebar_image( name.str() );
	if( out[ KEY_DISABLED ] == NULL )
		out[ KEY_DISABLED ] = out[ KEY_ENABLED ];
	return TRUE;
}

/// TRUE when the key now wears the sidebar's art
static Bool wearSidebarKey( Int key )
{
	GameWindow *win = barWindow( s_keyWindow[ key ] );
	const Image *art[ KEY_STATES ];
	if( win == NULL || !sidebarKeyImages( s_keyPiece[ key ], art ) )
		return FALSE;
	const Image *now = GadgetButtonGetEnabledImage( win );
	if( now != art[ KEY_ENABLED ] && now != art[ KEY_HILITE ] )
	{
		s_keyClassic[ key ][ KEY_ENABLED ] = GadgetButtonGetEnabledImage( win );
		s_keyClassic[ key ][ KEY_HILITE ] = GadgetButtonGetHiliteImage( win );
		s_keyClassic[ key ][ KEY_PUSHED ] = GadgetButtonGetHiliteSelectedImage( win );
		s_keyClassic[ key ][ KEY_DISABLED ] = GadgetButtonGetDisabledImage( win );
	}
	GadgetButtonSetEnabledImage( win, art[ KEY_ENABLED ] );
	GadgetButtonSetHiliteImage( win, art[ KEY_HILITE ] );
	GadgetButtonSetHiliteSelectedImage( win, art[ KEY_PUSHED ] );
	GadgetButtonSetDisabledImage( win, art[ KEY_DISABLED ] );
	s_keyWorn[ key ] = art[ KEY_ENABLED ];
	s_keyWornLit[ key ] = art[ KEY_HILITE ];
	return TRUE;
}

static void restoreClassicKeys( void )
{
	for( Int key = 0; key < SIDEBAR_KEY_COUNT; key++ )
	{
		GameWindow *win = s_keyWorn[ key ] ? barWindow( s_keyWindow[ key ] ) : NULL;
		const Image *now = win != NULL ? GadgetButtonGetEnabledImage( win ) : NULL;
		if( win != NULL && ( now == s_keyWorn[ key ] || now == s_keyWornLit[ key ] ) )
		{
			GadgetButtonSetEnabledImage( win, s_keyClassic[ key ][ KEY_ENABLED ] );
			GadgetButtonSetHiliteImage( win, s_keyClassic[ key ][ KEY_HILITE ] );
			GadgetButtonSetHiliteSelectedImage( win, s_keyClassic[ key ][ KEY_PUSHED ] );
			GadgetButtonSetDisabledImage( win, s_keyClassic[ key ][ KEY_DISABLED ] );
		}
		s_keyWorn[ key ] = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
// The money readout left-aligned after the "$" the sidebar's plate paints in the money glass (x 1698 at
// 1920, 12 into the glass; the digits from 1710, 24 into it).  The scheme's alignment comes back for the Classic bar unless a scheme
// set since gave its own: the saved one is kept with the side it was read under.
//-------------------------------------------------------------------------------------------------
static Bool s_moneySaved = FALSE;
static Bool s_moneyCentered = FALSE;
static Int s_moneyLeftMargin = 0;
static AsciiString s_moneySide;

static TextData *moneyText( GameWindow *money )
{
	if( money == NULL || !BitTest( money->winGetStyle(), GWS_STATIC_TEXT ) )
		return NULL;
	return (TextData *)money->winGetUserData();
}

//-------------------------------------------------------------------------------------------------
void ControlBar::restoreClassicWindows( void )
{
	for( size_t i = 0; i < m_sidebarHidden.size(); i++ )
		m_sidebarHidden[ i ]->winHide( FALSE );
	m_sidebarHidden.clear();

	restoreClassicKeys();
	if( s_moneySaved )
	{
		TextData *text = moneyText( barWindow( "MoneyDisplay" ) );
		if( text != NULL && m_controlBarSchemeManager != NULL
				&& m_controlBarSchemeManager->getCurrentSide().compare( s_moneySide.str() ) == 0 )
		{
			text->centered = s_moneyCentered;
			text->leftMargin = s_moneyLeftMargin;
		}
		s_moneySaved = FALSE;
	}

	if( m_sidebarBlockSaved )
	{
		GameWindow *block = barWindow( "ControlBarProBlockInputProduction" );
		if( block != NULL )
			block->winEnable( m_sidebarBlockEnabled );
		m_sidebarBlockSaved = FALSE;
	}

	GameWindow *power = barWindow( "PowerWindow" );
	GameWinDrawFunc vertical = TheFunctionLexicon ? TheFunctionLexicon->gameWinDrawFunc( NAMEKEY( "W3DPowerDrawV" ) ) : NULL;
	if( power != NULL && vertical != NULL && s_classicPowerDraw != NULL && power->winGetDrawFunc() == vertical )
		power->winSetDrawFunc( s_classicPowerDraw );

	GameWinDrawFunc noDraw = TheFunctionLexicon ? TheFunctionLexicon->gameWinDrawFunc( NAMEKEY( "W3DNoDraw" ) ) : NULL;
	if( m_rightHUDWindow != NULL && noDraw != NULL && s_classicRightHUDDraw != NULL && m_rightHUDWindow->winGetDrawFunc() == noDraw )
		m_rightHUDWindow->winSetDrawFunc( s_classicRightHUDDraw );

	if( m_rightHUDUnitSelectParent != NULL )
	{
		if( s_classicUnitSelectedDraw != NULL && m_rightHUDUnitSelectParent->winGetDrawFunc() == drawSelectionCard )
			m_rightHUDUnitSelectParent->winSetDrawFunc( s_classicUnitSelectedDraw );
		if( s_classicUnitSelectedInput != NULL && m_rightHUDUnitSelectParent->winGetInputFunc() == passToParent )
			m_rightHUDUnitSelectParent->winSetInputFunc( s_classicUnitSelectedInput );
		if( s_unitSelectedImageCleared )
			m_rightHUDUnitSelectParent->winSetStatus( WIN_STATUS_IMAGE );
	}
	s_unitSelectedImageCleared = FALSE;
	if( s_queueImageCleared && m_contextParent[ CP_BUILD_QUEUE ] != NULL )
		m_contextParent[ CP_BUILD_QUEUE ]->winSetStatus( WIN_STATUS_IMAGE );
	s_queueImageCleared = FALSE;
	s_card.shown = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** layoutPanels' second half under the sidebar.  The Classic pass has just put every window where the
	* Classic bar has it and written that down; this moves ControlBar.wnd's windows onto the sidebar's,
	* the strip's and the card's cells through placeWindowAt only, so the record stays true and a later
	* Classic pass finds every window where it was left. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutSidebar( void )
{
	GameWindow *parent = m_contextParent[ CP_MASTER ];
	if( parent == NULL || TheDisplay == NULL )
		return;
	HudLayoutRects hud;
	getHudLayoutRects( &hud );

	//
	// The frame covers the screen: the window manager hands the pointer to a top-level window by its
	// rectangle first, and the sidebar runs to the top.  It is SEE_THRU and passes what it is given
	// (ControlBarInput), so only the blockers below stop a click.  Its children are moved the other way
	// so they stand where they stood on screen.
	//
	ICoord2D at;
	parent->winGetScreenPosition( &at.x, &at.y );
	for( GameWindow *child = parent->winGetChild(); child; child = child->winGetNext() )
	{
		Int x, y;
		child->winGetPosition( &x, &y );
		child->winSetPosition( x + at.x - hud.screen.lo.x, y + at.y - hud.screen.lo.y );
	}
	parent->winSetPosition( hud.screen.lo.x, hud.screen.lo.y );
	parent->winSetSize( hud.screen.width(), hud.screen.height() );
	m_panelOrigin = hud.screen.lo;
	// the sidebar does not minimise (switchControlBarStage); a bar a script hid stays hidden
	if( m_currentControlBarStage != CONTROL_BAR_STAGE_HIDDEN )
		m_currentControlBarStage = CONTROL_BAR_STAGE_DEFAULT;

	// what the world must not take a click through, for the group panel, the airlift panel, InGameUI
	IRegion2D left = hud.strip;
	left.lo.x = MIN( hud.card.lo.x, hud.strip.lo.x );
	left.lo.y = MIN( hud.card.lo.y, hud.strip.lo.y );
	m_panelRect[ CB_PANEL_LEFT ] = left;
	m_panelRect[ CB_PANEL_CENTER ] = hud.strip;
	m_panelRect[ CB_PANEL_RIGHT ] = hud.strip;

	// the sidebar's top: its own keys where the sheet has them
	placeWindowAt( barWindow( "ButtonOptions" ), hud.menuButton );
	placeWindowAt( barWindow( "PopupCommunicator" ), hud.diplomacyButton );
	wearSidebarKey( SIDEBAR_KEY_MENU );
	wearSidebarKey( SIDEBAR_KEY_DIPLOMACY );

	// the digits left-aligned 24 into the glass (x 1710 at 1920), after the plate's painted "$" at 12
	GameWindow *money = barWindow( "MoneyDisplay" );
	IRegion2D moneyAt = hud.money;
	moneyAt.lo.x = MIN( hud.money.lo.x + REAL_TO_INT( 24 * hud.scale + 0.5f ), hud.money.hi.x - 1 );
	placeWindowAt( money, moneyAt );
	TextData *moneyData = moneyText( money );
	if( moneyData != NULL )
	{
		if( !s_moneySaved )
		{
			s_moneyCentered = moneyData->centered;
			s_moneyLeftMargin = moneyData->leftMargin;
			s_moneySide = m_controlBarSchemeManager ? m_controlBarSchemeManager->getCurrentSide() : AsciiString::TheEmptyString;
			s_moneySaved = TRUE;
		}
		moneyData->centered = FALSE;
		moneyData->leftMargin = 0;
	}
	placeWindowAt( barWindow( "LeftHUD" ), hud.radar );
	if( m_radarAttackGlowWindow != NULL )
	{
		// EA's lamp on the radar box's top right corner, its own size
		ICoord2D size;
		m_radarAttackGlowWindow->winGetSize( &size.x, &size.y );
		IRegion2D lamp;
		lamp.lo.x = hud.radar.hi.x - size.x;
		lamp.lo.y = hud.radar.lo.y;
		lamp.hi.x = hud.radar.hi.x;
		lamp.hi.y = hud.radar.lo.y + size.y;
		placeWindowAt( m_radarAttackGlowWindow, lamp );
	}
	// The General button only while there is something to spend (the owner's, 2026-11-20): the powers
	// fire from the wing and the W tab, so the key stands in its seat beside Sat and Onar, its star
	// flashing (getStarImage), only while the player has points or a talent pick waiting, and a click
	// opens the General's Powers window as Ctrl+G does.  updateSidebarGeneralKey shows and hides it;
	// setControlBarSchemeByPlayer hides it as this does
	wearSidebarKey( SIDEBAR_KEY_GENERAL );
	placeWindowAt( barWindow( "ButtonGeneral" ), hud.generalButton );
	hideForSidebar( barWindow( "ButtonGeneral" ) );

	// the power bar stood on end down the sidebar's left edge (W3DPowerDrawV, lane D)
	GameWindow *power = barWindow( "PowerWindow" );
	if( power != NULL )
	{
		placeWindowAt( power, hud.powerRail );
		GameWinDrawFunc vertical = TheFunctionLexicon ? TheFunctionLexicon->gameWinDrawFunc( NAMEKEY( "W3DPowerDrawV" ) ) : NULL;
		if( vertical != NULL && power->winGetDrawFunc() != vertical )
		{
			s_classicPowerDraw = power->winGetDrawFunc();
			power->winSetDrawFunc( vertical );
		}
	}

	// the blockers: the radar's covers the sidebar, the production plate's the strip
	placeWindowAt( barWindow( "ControlBarProBlockInputRadar" ), hud.sidebar );
	GameWindow *stripBlock = barWindow( "ControlBarProBlockInputProduction" );
	if( stripBlock != NULL )
	{
		// ControlBar.wnd has it disabled (the command grid blocks there), but the strip must block with
		// nothing selected too
		if( !m_sidebarBlockSaved )
		{
			m_sidebarBlockEnabled = BitTest( stripBlock->winGetStatus(), WIN_STATUS_ENABLED ) ? TRUE : FALSE;
			m_sidebarBlockSaved = TRUE;
		}
		stripBlock->winEnable( TRUE );
		placeWindowAt( stripBlock, hud.strip );
	}

	// the selection's commands along the strip, ButtonCommand01..14 in order
	placeWindowAt( m_contextParent[ CP_COMMAND ], hud.strip );
	for( Int i = 0; i < HUD_STRIP_BUTTONS && i < MAX_COMMANDS_PER_SET; i++ )
		placeWindowAt( m_commandWindows[ i ], hud.stripButtons[ i ] );
	arrangeStripByRole();

	// the panes that stand in for the commands go over the strip's buttons
	IRegion2D buttons = hud.strip;
	buttons.lo.x = hud.stripButtons[ 0 ].lo.x;
	buttons.hi.x = hud.stripButtons[ HUD_STRIP_BUTTONS - 1 ].hi.x;
	fitWindowTree( this, m_contextParent[ CP_UNDER_CONSTRUCTION ], buttons );
	fitWindowTree( this, m_contextParent[ CP_OCL_TIMER ], buttons );
	fitWindowTree( this, m_contextParent[ CP_BEACON ], buttons );

	//
	// The selection card, welded to the strip: the right HUD covers the card, so a click on any of it
	// reaches its tiles (portraitBarInput), and draws nothing there itself (the card's plate is the
	// sidebar background's).  The portrait, name, rank and health stand in `portrait`; under them the
	// flat row on the strip holds a multi-selection's type tiles (layoutMultiSelectTiles) or a factory's
	// queue (updateBuildQueue), never both; a single unit's upgrade cameos climb from the card's top
	//
	placeWindowAt( m_rightHUDWindow, hud.card );
	// its input passes clicks off the medallion and the plate to the world (Classic keeps it once a
	// multi-selection has set it, as before)
	m_rightHUDWindow->winSetInputFunc( ControlBar_portraitBarInput );
	GameWinDrawFunc noDraw =TheFunctionLexicon ? TheFunctionLexicon->gameWinDrawFunc( NAMEKEY( "W3DNoDraw" ) ) : NULL;
	if( noDraw != NULL && m_rightHUDWindow->winGetDrawFunc() != noDraw )
	{
		s_classicRightHUDDraw = m_rightHUDWindow->winGetDrawFunc();
		m_rightHUDWindow->winSetDrawFunc( noDraw );
	}
	// The portrait's parent spans the card: the picture in the medallion and the upgrade cameos in the
	// tray are its children, so both stay inside it (tooltips).  A click it does not give a child goes on
	// to the right HUD (passToParent: a type tile's, portraitBarInput).  Its painted well (the Classic
	// 3x3) goes, and it draws the name, rank, health and the queue's progress instead
	placeWindowAt( m_rightHUDUnitSelectParent, hud.card );
	if( BitTest( m_rightHUDUnitSelectParent->winGetStatus(), WIN_STATUS_IMAGE ) )
	{
		m_rightHUDUnitSelectParent->winClearStatus( WIN_STATUS_IMAGE );
		s_unitSelectedImageCleared = TRUE;
	}
	if( m_rightHUDUnitSelectParent->winGetDrawFunc() != drawSelectionCard )
	{
		s_classicUnitSelectedDraw = m_rightHUDUnitSelectParent->winGetDrawFunc();
		m_rightHUDUnitSelectParent->winSetDrawFunc( drawSelectionCard );
	}
	if( m_rightHUDUnitSelectParent->winGetInputFunc() != passToParent )
	{
		s_classicUnitSelectedInput = m_rightHUDUnitSelectParent->winGetInputFunc();
		m_rightHUDUnitSelectParent->winSetInputFunc( passToParent );
	}
	// the portrait at the art's own shape (120x96) in the medallion's well, which is a little wider
	placeWindowAt( m_rightHUDCameoWindow, HudFitAspect( hud.portrait, HUD_CAMEO_ART_W, HUD_CAMEO_ART_H ) );

	// a single unit's upgrades in the tray's first five tiles, each 2 in from its tile's edges
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS && i < HUD_TRAY_CELLS; i++ )
		placeWindowAt( m_rightHUDUpgradeCameos[ i ], trayCameo( hud.trayCells[ i ], hud.scale ) );

	// a factory's queue in the tray, ButtonQueue01..06 left to right, its Classic well art off.
	// ponytail: the tray holds six, so the queue's seventh to ninth entries do not show (and cannot be
	// cancelled from here); a "+3" if the owner wants them
	GameWindow *queue = m_contextParent[ CP_BUILD_QUEUE ];
	if( queue != NULL )
	{
		IRegion2D trayBox = hud.trayCells[ 0 ];
		trayBox.hi = hud.trayCells[ HUD_TRAY_CELLS - 1 ].hi;
		placeWindowAt( queue, trayBox );
		if( BitTest( queue->winGetStatus(), WIN_STATUS_IMAGE ) )
		{
			queue->winClearStatus( WIN_STATUS_IMAGE );
			s_queueImageCleared = TRUE;
		}
		for( Int i = 0; i < MAX_BUILD_QUEUE_BUTTONS; i++ )
		{
			AsciiString name;
			name.format( "ButtonQueue%02d", i + 1 );
			GameWindow *button = barWindow( name.str() );
			if( i < HUD_TRAY_CELLS )
				placeWindowAt( button, trayCameo( hud.trayCells[ i ], hud.scale ) );
			else
				hideForSidebar( button );
		}
	}
	if( !m_multiSelectTiles.empty() )
		layoutMultiSelectTiles( (Int)m_multiSelectTiles.size() );

	// No idle keys and no beacon key on the sidebar (the owner's, 2026-11-20): the worker, infantry,
	// vehicle and aircraft keys, the army key and the beacon key are hidden, their hotkeys the command
	// map's.  setControlBarSchemeByPlayer's showIdleButtons hides the idle ones the same way
	static const char *const keyNames[] =
	{
		"ButtonIdleWorker", "ButtonIdleInfantry", "ButtonIdleVehicle", "ButtonIdleAircraft", "ButtonSelectArmy",
		"ButtonPlaceBeacon"
	};
	for( Int i = 0; i < (Int)( sizeof( keyNames ) / sizeof( keyNames[ 0 ] ) ); i++ )
		hideForSidebar( barWindow( keyNames[ i ] ) );

	// what the sidebar has no place for
	static const char *const hiddenNames[] =
	{
		"ButtonLarge", "ButtonSmall", "ButtonMedium", "GeneralsExp", "ExpBarForeground",
		"ControlBarProBlockInputMoney", "ControlBarProBlockInputUpgrades", "ControlBarProBlockInputGenPowerButton",
		"ObserverPlayerInfoWindow", "ObserverPlayerListWindow",
	};
	for( Int i = 0; i < (Int)( sizeof( hiddenNames ) / sizeof( hiddenNames[ 0 ] ) ); i++ )
		hideForSidebar( barWindow( hiddenNames[ i ] ) );

	// the tabs, the cameos and the arrows (lane C), then the radar into its box
	Sidebar_layout( hud );
	fitRadarToMap( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** The sidebar's General key in its seat while the local player has science points or a talent pick
	* waiting, gone otherwise (layoutSidebar placed it and put it on the hidden list, so the Classic bar
	* gets it back whatever this left it as).  Called from Sidebar_update, the client pass. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateSidebarGeneralKey( void )
{
	if( !m_sidebarLayout )
		return;
	GameWindow *win = barWindow( "ButtonGeneral" );
	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( win == NULL || player == NULL )
		return;
	const Bool spend = player->getSciencePurchasePoints() > 0 || player->hasTalentWaiting();
	if( spend == win->winIsHidden() )
		win->winHide( !spend );
}

//-------------------------------------------------------------------------------------------------
GameWindow *ControlBar::getShownPowerShortcut( Int i ) const
{
	if( m_specialPowerShortcutButtons == NULL || i < 0 || i >= m_currentlyUsedSpecialPowersButtons || i >= MAX_SPECIAL_POWER_SHORTCUTS )
		return NULL;
	GameWindow *win = m_specialPowerShortcutButtons[ i ];
	return win != NULL && !win->winIsHidden() && GadgetButtonGetData( win ) != NULL ? win : NULL;
}

//-------------------------------------------------------------------------------------------------
void ControlBar::firePowerCell( GameWindow *cell )
{
	processCommandUI( cell, GBM_SELECTED );
}
