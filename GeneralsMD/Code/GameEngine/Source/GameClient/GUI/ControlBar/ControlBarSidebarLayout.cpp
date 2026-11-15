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
#include "GameClient/GameFont.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"

/// the PowerWindow's draw before the sidebar stood it on end, W3DPowerDraw from ControlBar.wnd
static GameWinDrawFunc s_classicPowerDraw = NULL;
/// the right HUD's before the sidebar emptied it, W3DRightHUDDraw from ControlBar.wnd
static GameWinDrawFunc s_classicRightHUDDraw = NULL;
/// the portrait parent's before the sidebar gave it the card's readout (the .wnd's default draw)
static GameWinDrawFunc s_classicUnitSelectedDraw = NULL;
static Bool s_unitSelectedImageCleared = FALSE;

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

//-------------------------------------------------------------------------------------------------
/** The card's portrait half (HudLayoutRects::portrait, 160x104 at 1080): the portrait picture at its
	* top left in the cameos' 5:4, a bit over half the width, the name and the rank beside it, the health
	* bar along the foot. */
//-------------------------------------------------------------------------------------------------
static void selectionCardCells( const HudLayoutRects &hud, IRegion2D *cameo, IRegion2D *text, IRegion2D *bar )
{
	const IRegion2D &p = hud.portrait;
	const Int gap = MAX( 2, REAL_TO_INT( 4 * hud.scale + 0.5f ) );
	const Int barH = MAX( 4, REAL_TO_INT( 8 * hud.scale + 0.5f ) );
	Int w = p.width() * 55 / 100;
	Int h = w * 4 / 5;
	const Int roomH = p.height() - barH - gap;
	if( h > roomH )
	{
		h = MAX( 1, roomH );
		w = h * 5 / 4;
	}
	cameo->lo = p.lo;
	cameo->hi.x = p.lo.x + w;
	cameo->hi.y = p.lo.y + h;
	text->lo.x = cameo->hi.x + gap;
	text->lo.y = p.lo.y;
	text->hi.x = p.hi.x;
	text->hi.y = cameo->hi.y;
	bar->lo.x = p.lo.x;
	bar->hi.x = p.hi.x;
	bar->hi.y = p.hi.y;
	bar->lo.y = p.hi.y - barH;
}

//-------------------------------------------------------------------------------------------------
/** WinUnitSelected's draw under the sidebar: the selection's name, rank and health beside its portrait.
	* The portrait and the upgrade cameos are its children and draw themselves. */
//-------------------------------------------------------------------------------------------------
static void drawSelectionCard( GameWindow *window, WinInstanceData *instData )
{
	if( !s_card.shown || TheControlBar == NULL || TheDisplay == NULL )
		return;
	HudLayoutRects hud;
	TheControlBar->getHudLayoutRects( &hud );
	IRegion2D cameo, text, bar;
	selectionCardCells( hud, &cameo, &text, &bar );

	// the name, wrapped to the column, at most its height
	Int textBottom = text.lo.y;
	if( s_cardName == NULL )
		s_cardName = TheDisplayStringManager->newDisplayString();
	if( s_cardName != NULL && text.width() > 4 )
	{
		GameFont *font = TheFontLibrary->getFont( "Arial", MAX( 7, REAL_TO_INT( 11 * hud.scale + 0.5f ) ), TRUE );
		if( s_cardName->getFont() != font )
			s_cardName->setFont( font );
		s_cardName->setWordWrap( text.width() );
		if( s_cardName->getText().compare( s_card.name ) != 0 )
			s_cardName->setText( s_card.name );
		s_cardName->setClipRegion( &text );
		s_cardName->draw( text.lo.x, text.lo.y, GameMakeColor( 245, 245, 245, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		Int w, h;
		s_cardName->getSize( &w, &h );
		textBottom = MIN( text.lo.y + h, text.hi.y );
	}

	// the rank's chevron under it, its own shape, as tall as a line of the name at most
	if( s_card.rank != NULL && s_card.rank->getImageHeight() > 0 )
	{
		const Int room = text.hi.y - textBottom - 2;
		const Int h = MIN( room, MAX( 8, REAL_TO_INT( 18 * hud.scale + 0.5f ) ) );
		const Int w = h * s_card.rank->getImageWidth() / s_card.rank->getImageHeight();
		if( h > 0 )
			TheDisplay->drawImage( s_card.rank, text.lo.x, textBottom + 2, text.lo.x + w, textBottom + 2 + h );
	}

	// the health bar, green to yellow to red as the stock health bars go
	if( s_card.health >= 0.0f && bar.width() > 2 && bar.height() > 2 )
	{
		const Real health = MIN( 1.0f, s_card.health );
		const Color fill = health > 0.5f ? GameMakeColor( 0, 220, 0, 255 )
			: ( health > 0.25f ? GameMakeColor( 230, 220, 0, 255 ) : GameMakeColor( 230, 0, 0, 255 ) );
		TheDisplay->drawFillRect( bar.lo.x, bar.lo.y, bar.width(), bar.height(), GameMakeColor( 0, 0, 0, 200 ) );
		const Int filled = REAL_TO_INT( ( bar.width() - 2 ) * health + 0.5f );
		if( filled > 0 )
			TheDisplay->drawFillRect( bar.lo.x + 1, bar.lo.y + 1, filled, bar.height() - 2, fill );
		TheDisplay->drawOpenRect( bar.lo.x, bar.lo.y, bar.width(), bar.height(), 1.0f, GameMakeColor( 90, 90, 90, 255 ) );
	}
}

//-------------------------------------------------------------------------------------------------
void ControlBar::updateSelectionCardReadout( void )
{
	s_card.shown = FALSE;
	if( !m_sidebarLayout )
		return;

	// a multi-selection: the focused type's name over its portrait, no rank or health of one unit
	if( m_currContext == CB_CONTEXT_MULTI_SELECT )
	{
		if( m_multiSelectFocus < 0 || m_multiSelectFocus >= m_multiSelectGroupCount )
			return;
		s_card.name = m_multiSelectGroupTemplate[ m_multiSelectFocus ]->getDisplayName();
		s_card.health = -1.0f;
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
void ControlBar::restoreClassicWindows( void )
{
	for( size_t i = 0; i < m_sidebarHidden.size(); i++ )
		m_sidebarHidden[ i ]->winHide( FALSE );
	m_sidebarHidden.clear();

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
		if( s_unitSelectedImageCleared )
			m_rightHUDUnitSelectParent->winSetStatus( WIN_STATUS_IMAGE );
	}
	s_unitSelectedImageCleared = FALSE;
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

	// the sidebar's top
	placeWindowAt( barWindow( "ButtonOptions" ), hud.menuButton );
	placeWindowAt( barWindow( "MoneyDisplay" ), hud.money );
	placeWindowAt( barWindow( "PopupCommunicator" ), hud.diplomacyButton );
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
	// Sat and Onar come with phase 2; until then the General button takes their row as well
	IRegion2D generalRow = hud.generalButton;
	generalRow.lo.x = MIN( hud.sellButton.lo.x, hud.generalButton.lo.x );
	placeWindowAt( barWindow( "ButtonGeneral" ), generalRow );

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

	// the panes that stand in for the commands go over the strip's buttons
	IRegion2D buttons = hud.strip;
	buttons.lo.x = hud.stripButtons[ 0 ].lo.x;
	buttons.hi.x = hud.stripButtons[ HUD_STRIP_BUTTONS - 1 ].hi.x;
	fitWindowTree( this, m_contextParent[ CP_UNDER_CONSTRUCTION ], buttons );
	fitWindowTree( this, m_contextParent[ CP_OCL_TIMER ], buttons );
	fitWindowTree( this, m_contextParent[ CP_BEACON ], buttons );

	//
	// The selection card: the right HUD covers the card, so a click on any of it reaches its tiles
	// (portraitBarInput), and draws nothing there itself (the card's plate is the sidebar background's);
	// the portrait with its upgrade cameos stands in `portrait`, and a multi-selection's type tiles in
	// the grid's cells (layoutMultiSelectTiles).  A single unit is the portrait alone, the grid empty
	//
	placeWindowAt( m_rightHUDWindow, hud.card );
	GameWinDrawFunc noDraw = TheFunctionLexicon ? TheFunctionLexicon->gameWinDrawFunc( NAMEKEY( "W3DNoDraw" ) ) : NULL;
	if( noDraw != NULL && m_rightHUDWindow->winGetDrawFunc() != noDraw )
	{
		s_classicRightHUDDraw = m_rightHUDWindow->winGetDrawFunc();
		m_rightHUDWindow->winSetDrawFunc( noDraw );
	}
	// the portrait's parent spans the card, so the upgrade cameos on the grid stay inside it (tooltips);
	// its painted well (the Classic 3x3) goes, and it draws the name, rank and health instead
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
	IRegion2D cameo, text, bar;
	selectionCardCells( hud, &cameo, &text, &bar );
	placeWindowAt( m_rightHUDCameoWindow, cameo );
	// a single unit's upgrade cameos in a row on the grid, which only a multi-selection's tiles use
	for( Int i = 0; i < MAX_RIGHT_HUD_UPGRADE_CAMEOS && i < HUD_CARD_GRID_CELLS; i++ )
		placeWindowAt( m_rightHUDUpgradeCameos[ i ], hud.gridCells[ i ] );
	fitWindowTree( this, m_contextParent[ CP_BUILD_QUEUE ], hud.portrait );
	if( !m_multiSelectTiles.empty() )
		layoutMultiSelectTiles( (Int)m_multiSelectTiles.size() );

	//
	// The idle keys, the army key and the beacon key in a row on the strip, from x 1100 at 1920.  A 4:3
	// strip has no room left beside the commands, so there the row stands above the strip's right end.
	//
	// ponytail: above the strip it may meet the superweapon timers' lowest row; phase 4 tunes 4:3 and 5:4
	static const char *const rowNames[] =
	{
		"ButtonIdleWorker", "ButtonIdleInfantry", "ButtonIdleVehicle", "ButtonIdleAircraft", "ButtonSelectArmy", "ButtonPlaceBeacon"
	};
	enum { ROW_COUNT = sizeof( rowNames ) / sizeof( rowNames[ 0 ] ) };
	GameWindow *row[ ROW_COUNT ];
	Int count = 0;
	for( Int i = 0; i < ROW_COUNT; i++ )
	{
		GameWindow *win = barWindow( rowNames[ i ] );
		ICoord2D extent = { 0, 0 };
		if( win != NULL )
			win->winGetSize( &extent.x, &extent.y );
		// a scheme without a seat for one of them sizes it to nothing (ControlBarScheme::init)
		if( win != NULL && extent.x > 0 && extent.y > 0 )
			row[ count++ ] = win;
	}
	const Int size = hud.stripButtons[ 0 ].height();
	const Int pitch = hud.stripButtons[ 1 ].lo.x - hud.stripButtons[ 0 ].lo.x;
	const Int gap = pitch - size;
	const Int need = count > 0 ? count * pitch - gap : 0;
	ICoord2D rowAt;
	if( hud.stripSpare.width() >= need )
	{
		rowAt.x = MIN( MAX( hud.area.lo.x + REAL_TO_INT( 1100 * hud.scale ), hud.stripSpare.lo.x ), hud.stripSpare.hi.x - need );
		rowAt.y = hud.stripButtons[ 0 ].lo.y;
	}
	else
	{
		rowAt.x = hud.play.hi.x - gap - need;
		rowAt.y = hud.strip.lo.y - gap - size;
	}
	for( Int i = 0; i < count; i++ )
	{
		IRegion2D cell;
		cell.lo.x = rowAt.x + i * pitch;
		cell.lo.y = rowAt.y;
		cell.hi.x = cell.lo.x + size;
		cell.hi.y = cell.lo.y + size;
		placeWindowAt( row[ i ], cell );
	}

	// what the sidebar has no place for
	static const char *const hiddenNames[] =
	{
		"ButtonLarge", "ButtonSmall", "ButtonMedium", "GeneralsExp", "ExpBarForeground",
		"ControlBarProBlockInputMoney", "ControlBarProBlockInputUpgrades", "ControlBarProBlockInputGenPowerButton",
		"ObserverPlayerInfoWindow", "ObserverPlayerListWindow",
	};
	for( Int i = 0; i < (Int)( sizeof( hiddenNames ) / sizeof( hiddenNames[ 0 ] ) ); i++ )
		hideForSidebar( barWindow( hiddenNames[ i ] ) );
	// the queue is updateBuildQueue's to show, and it does not under the sidebar
	if( m_contextParent[ CP_BUILD_QUEUE ] != NULL )
		m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );

	// the tabs, the cameos and the arrows (lane C), then the radar into its box
	Sidebar_layout( hud );
	fitRadarToMap( TRUE );
}
