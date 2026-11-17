/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ControlBarMultiSelect.cpp ////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive GUI for when you select mutiple objects.  What we do is show
//				 the commands that you can use between them all
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MessageStream.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/GameText.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"

extern IRegion2D SidebarWellInset( const IRegion2D &cell, Real k, Int side, Int foot );	// ControlBarSidebarLayout.cpp
#include "GameClient/GameClient.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/SelectionXlat.h"
#include "GameLogic/Object.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif




//-------------------------------------------------------------------------------------------------
/** Populate the visible command bar with commands that are common to all the objects
	* that are selected in the UI */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateMultiSelect( void )
{

	// sanity
	DEBUG_ASSERTCRASH( TheInGameUI->getSelectCount() > 1,
										 ("populateMultiSelect: Can't populate multiselect context cause there are only '%d' things selected\n",
										  TheInGameUI->getSelectCount()) );

	//
	// group the selection by unit type for the right HUD, then let the focused type drive
	// the whole context: its portrait, its command set (build pages included) and the strip
	// of type cameos.
	//
	populateMultiSelectUnitList();

}  // end populateMultiSelect

//-------------------------------------------------------------------------------------------------
/** Show what a multi-selection holds in the right HUD: one cameo per selected unit type on the
	* small strip windows, each with a count badge.  The focused type gets the portrait window
	* and its cameo is the lit one. */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateMultiSelectUnitList( void )
{

	// remember what was focused - a repopulate (a unit died, a build page turned) should not
	// yank the player onto another type
	const ThingTemplate *focusedTemplate = NULL;
	if( m_multiSelectFocus >= 0 && m_multiSelectFocus < m_multiSelectGroupCount )
		focusedTemplate = m_multiSelectGroupTemplate[ m_multiSelectFocus ];

	m_multiSelectGroupCount = 0;

	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selectedDrawables->begin(); it != selectedDrawables->end(); ++it )
	{
		Drawable *draw = *it;
		Object *obj = draw ? draw->getObject() : NULL;

		// same filter the command population uses
		if( obj == NULL || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) ||
				obj->getStatusBits().test( OBJECT_STATUS_SOLD ) )
			continue;

		// find this unit's type group, or open a new one
		const ThingTemplate *thing = draw->getTemplate();
		Int g;
		for( g = 0; g < m_multiSelectGroupCount; g++ )
			if( m_multiSelectGroupTemplate[ g ] == thing )
				break;
		if( g == m_multiSelectGroupCount )
		{
			// ponytail: MAX_MULTI_SELECT_GROUPS unit types; a selection with more than that
			// drops the tail types from the display
			if( m_multiSelectGroupCount >= MAX_MULTI_SELECT_GROUPS )
				continue;
			m_multiSelectGroupTemplate[ g ] = thing;
			m_multiSelectGroupSize[ g ] = 0;
			m_multiSelectGroupFirst[ g ] = draw->getID();
			m_multiSelectGroupCount++;
		}
		else if( !obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) &&
						 TheGameClient->findDrawableByID( m_multiSelectGroupFirst[ g ] )->getObject()->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
		{
			// a finished building speaks for its type, so the bar shows the upgrades it can take
			m_multiSelectGroupFirst[ g ] = draw->getID();
		}
		m_multiSelectGroupSize[ g ]++;

	}  // end for, selected drawables

	// one cell per type, laid out n x n over the right HUD
	layoutMultiSelectTiles( m_multiSelectGroupCount );

	// stay on the type that was focused if it is still selected
	Int focus = 0;
	for( Int g = 0; g < m_multiSelectGroupCount; g++ )
		if( m_multiSelectGroupTemplate[ g ] == focusedTemplate )
			focus = g;

	m_multiSelectFocus = focus;
	setMultiSelectFocus( focus );

}  // end populateMultiSelectUnitList

//-------------------------------------------------------------------------------------------------
/** Focus one type group of the multi-selection: its portrait and count go to the portrait
	* window, and the strip below shows every group's cameo with the focused one lit */
//-------------------------------------------------------------------------------------------------
void ControlBar::setMultiSelectFocus( Int index )
{

	if( m_multiSelectGroupCount == 0 )
		return;

	// wrap in both directions
	m_multiSelectFocus = ( index % m_multiSelectGroupCount + m_multiSelectGroupCount )
											 % m_multiSelectGroupCount;

	Drawable *draw = TheGameClient->findDrawableByID( m_multiSelectGroupFirst[ m_multiSelectFocus ] );
	Object *obj = draw ? draw->getObject() : NULL;
	if( obj == NULL )
		return;		// the next selection change repopulates the groups

	//
	// the focused type's representative drives the whole context: the command area shows its
	// command set (a builder's structure pages included), and updateContextMultiSelect keeps
	// judging those buttons through the single-selection update
	//
	m_currentSelectedDrawable = draw;
	populateCommand( obj );

	// the previous representative's production queue does not belong to this one; the update
	// re-shows the panel if the new representative is producing
	m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );

	// ... and the 3x3 type grid in the right HUD
	updateMultiSelectStrip();

}  // end setMultiSelectFocus

//-------------------------------------------------------------------------------------------------
/** Keep the 3x3 unit-type grid of a multi-selection alive.  Called every frame: the
	* single-selection update trades the portrait back and forth with the build queue panel
	* and setPortraitByObject hides the grid cells, so the multi-select look has to be
	* re-applied after them. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateMultiSelectStrip( void )
{

	if( m_currContext != CB_CONTEXT_MULTI_SELECT || m_multiSelectGroupCount == 0 )
		return;

	//
	// the portrait bar's rule, the owner's: each selected type once, its own tile with its count, and
	// no upgrades, which belong to one unit.  The well is emptied, upgrades and all: the portrait
	// window is larger than a grid cell and would show through every cell no type fills.
	// setPortraitByObject hides the cells, so it goes first
	//
	setPortraitByObject( NULL );

	//
	// The sidebar's card keeps its portrait in the medallion: the focused type's, with how many of it
	// are selected, and still no upgrades.  Its tray holds six types; a selection of more shows the
	// first six and the focus keys still reach the rest
	//
	const Bool sidebar = isSidebarLayout();
	if( sidebar )
	{
		const ThingTemplate *focused = m_multiSelectGroupTemplate[ m_multiSelectFocus ];
		setPortraitByImage( focused->getSelectedPortraitImage() ? focused->getSelectedPortraitImage() : focused->getButtonImage() );
		GadgetButtonSetCount( m_rightHUDCameoWindow, m_multiSelectGroupSize[ m_multiSelectFocus ] );
	}

	// one cell per selected type with its count; the focused type is the lit one, the rest
	// wear the darkened overlay state.  A cell is drawn from the portrait art, as large as the
	// well itself for a one-type selection; the button cameo stretched to that size is a blur
	for( size_t i = 0; i < m_multiSelectTiles.size(); i++ )
	{
		GameWindow *win = m_multiSelectTiles[ i ];
		if( win == NULL )
			continue;

		if( (Int)i < m_multiSelectGroupCount && ( !sidebar || (Int)i < HUD_TRAY_CELLS ) )
		{
			const ThingTemplate *type = m_multiSelectGroupTemplate[ i ];
			win->winHide( FALSE );
			win->winSetEnabledImage( 0, type->getSelectedPortraitImage() ? type->getSelectedPortraitImage() : type->getButtonImage() );
			win->winEnable( (Int)i == m_multiSelectFocus );
			GadgetButtonSetCount( win, m_multiSelectGroupSize[ i ] );
		}
		else
			win->winHide( TRUE );
	}

}  // end updateMultiSelectStrip

//-------------------------------------------------------------------------------------------------
/** The tooltip a type tile shows, through the build tooltip the command buttons use. */
//-------------------------------------------------------------------------------------------------
static void portraitBarTooltip( GameWindow *window, WinInstanceData *instData, UnsignedInt mouse )
{
	TheControlBar->showBuildTooltipLayout( window );
}

//-------------------------------------------------------------------------------------------------
/** The right HUD's input.  A tile that is not the focused type is disabled to wear its darkened
	* look, so its clicks land here on the parent and never on the tile; the focused tile ignores
	* them and they come here too.  A press that went down in the world is the selection translator's,
	* which holds it until some release lets it go, so while it holds one the release here ends a box
	* dragged onto the HUD and is no click on a tile.  Nothing is kept here between a press and its
	* release, so nothing can go stale.  Everything else goes on to the wall the .wnd gave the HUD,
	* which lets go of that world press. */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ControlBar_portraitBarInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	// the sidebar's card is its bounds: the world shows above the label plate, beside the medallion's
	// top, and a click there is the world's
	if( TheControlBar->isSidebarLayout() && msg >= GWM_LEFT_DOWN && msg <= GWM_RIGHT_DRAG )
	{
		HudLayoutRects hud;
		TheControlBar->getHudLayoutRects( &hud );
		const Int x = LOLONGTOSHORT( mData1 ), y = HILONGTOSHORT( mData1 );
		const Bool onMedallion = x >= hud.medallion.lo.x && x < hud.medallion.hi.x && y >= hud.medallion.lo.y && y < hud.medallion.hi.y;
		const Bool onPlate = x >= hud.labelPlate.lo.x && x < hud.labelPlate.hi.x && y >= hud.labelPlate.lo.y && y < hud.labelPlate.hi.y;
		if( !onMedallion && !onPlate )
			return MSG_IGNORED;
	}

	// the click's own position, which the window manager packed into mData1: the cursor's
	// (TheMouse's status) is not the click's when one is posted without moving it (the control
	// socket's click, a replayed input), and a tile under the cursor rather than the click was
	// never the one meant
	if( msg == GWM_LEFT_UP && !TheSelectionTranslator->isLeftMouseButtonDown() )
		TheControlBar->clickMultiSelectTile( LOLONGTOSHORT( mData1 ), HILONGTOSHORT( mData1 ) );
	return GameWinBlockInput( window, msg, mData1, mData2 );
}

//-------------------------------------------------------------------------------------------------
/** Takes units out of the selection the way a shift-click on a selected unit in the world does:
	* MSG_REMOVE_FROM_SELECTED_GROUP for the logic's group and deselectDrawable for the client's.
	* A plain click removes every other type, shift and a click removes the clicked one. */
//-------------------------------------------------------------------------------------------------
Bool ControlBar::clickMultiSelectTile( Int x, Int y )
{
	if( m_currContext != CB_CONTEXT_MULTI_SELECT )
		return FALSE;

	for( Int group = 0; group < m_multiSelectGroupCount && group < (Int)m_multiSelectTiles.size(); group++ )
	{
		GameWindow *tile = m_multiSelectTiles[ group ];
		if( tile == NULL || tile->winIsHidden() || !tile->winPointInWindow( x, y ) )
			continue;

		const ThingTemplate *clicked = m_multiSelectGroupTemplate[ group ];
		const Bool dropClicked = TheKeyboard->isShift();
		// a copy: deselectDrawable takes each one out of the list being walked
		const DrawableList selected = *TheInGameUI->getAllSelectedDrawables();
		GameMessage *remove = NULL;
		for( DrawableListCIt it = selected.begin(); it != selected.end(); ++it )
		{
			Drawable *draw = *it;
			if( ( draw->getTemplate() == clicked ) != dropClicked )
				continue;
			if( remove == NULL )
				remove = TheMessageStream->appendMessage( GameMessage::MSG_REMOVE_FROM_SELECTED_GROUP );
			remove->appendObjectIDArgument( draw->getObject()->getID() );
			TheInGameUI->deselectDrawable( draw );
		}
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool ControlBar::describePortraitBarWindow( GameWindow *window, UnicodeString &name, UnicodeString &description ) const
{
	for( Int upgrade = 0; upgrade < MAX_RIGHT_HUD_UPGRADE_CAMEOS; upgrade++ )
	{
		const UpgradeTemplate *shown = m_rightHUDUpgrades[ upgrade ];
		if( window != m_rightHUDUpgradeCameos[ upgrade ] || shown == NULL )
			continue;

		name = TheGameText->fetch( shown->getDisplayNameLabel().str() );
		// the description is the one on the button that buys it, when there is such a button;
		// retail's unused Command_UpgradeCamoNetting has a typo'd label, so skip labels the string table lacks
		description.clear();
		for( const CommandButton *button = TheCommandStore->getCommandButtons(); button; button = button->getNext() )
		{
			if( button->getUpgradeTemplate() == shown && button->getDescriptionLabel().isNotEmpty() )
			{
				Bool exists = FALSE;
				UnicodeString text = TheGameText->fetch( button->getDescriptionLabel(), &exists );
				if( exists )
				{
					description = text;
					break;
				}
			}
		}
		return TRUE;
	}

	for( size_t tile = 0; tile < m_multiSelectTiles.size() && (Int)tile < m_multiSelectGroupCount; tile++ )
	{
		if( window != m_multiSelectTiles[ tile ] )
			continue;

		name.format( u"%ls (%d)", m_multiSelectGroupTemplate[ tile ]->getDisplayName().str(), m_multiSelectGroupSize[ tile ] );
		description = TheGameText->fetch( "GUI:SelectionTypeDescription" );
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Make sure 'count' grid cells exist and lay them out n x n over the right HUD, n the
	* smallest square that holds them: one type fills the HUD, 2-4 get 2x2, 5-9 get 3x3, and
	* so on.  Cells are plain windows created in code (ControlBar.wnd has none to spare) that
	* borrow the push button image draw, so cameos, count badges and the darkened disabled
	* state work. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutMultiSelectTiles( Int count )
{
	if( m_rightHUDWindow == NULL || count <= 0 )
		return;

	Int n = 1;
	while( n * n < count )
		n++;

	ICoord2D hudSize;
	m_rightHUDWindow->winGetSize( &hudSize.x, &hudSize.y );
	Int cellW = hudSize.x / n;
	Int cellH = hudSize.y / n;

	// The sidebar's card has a tray on its label plate (HudLayoutRects::trayCells): tile i is cell i,
	// left to right, the types past its six are not shown (updateMultiSelectStrip)
	HudLayoutRects hud;
	ICoord2D hudAt = { 0, 0 };
	const Bool sidebar = isSidebarLayout();
	if( sidebar )
	{
		getHudLayoutRects( &hud );
		m_rightHUDWindow->winGetScreenPosition( &hudAt.x, &hudAt.y );
		cellW = hud.trayCells[ 0 ].width();
		cellH = hud.trayCells[ 0 ].height();
	}

	// the tiles' clicks reach the HUD itself, see portraitBarInput
	m_rightHUDWindow->winSetInputFunc( ControlBar_portraitBarInput );

	for( Int i = 0; i < count; i++ )
	{
		if( (Int)m_multiSelectTiles.size() <= i )
		{
			GameWindow *tile = TheWindowManager->winCreate( m_rightHUDWindow,
													WIN_STATUS_ENABLED | WIN_STATUS_USE_OVERLAY_STATES | WIN_STATUS_HIDDEN,
													0, 0, cellW, cellH, GameWinDefaultSystem );
			if( tile )
			{
				tile->winSetDrawFunc( TheWindowManager->getPushButtonImageDrawFunc() );
				tile->winSetTooltipFunc( portraitBarTooltip );
			}
			m_multiSelectTiles.push_back( tile );
		}

		GameWindow *tile = m_multiSelectTiles[ i ];
		if( tile == NULL )
			continue;
		if( sidebar )
		{
			// in the art's well, 2 in from every edge, at the cameo's 5:4
			const IRegion2D cell = HudFitAspect( SidebarWellInset( hud.trayCells[ MIN( i, (Int)HUD_TRAY_CELLS - 1 ) ], hud.scale, 2, 2 ),
				HUD_CAMEO_ART_W, HUD_CAMEO_ART_H );
			tile->winSetPosition( cell.lo.x - hudAt.x, cell.lo.y - hudAt.y );
			tile->winSetSize( cell.width(), cell.height() );
			continue;
		}
		Int col = i % n;
		Int row = i / n;
		tile->winSetPosition( col * cellW + 1, row * cellH + 1 );
		tile->winSetSize( cellW - 2, cellH - 2 );
	}

}  // end layoutMultiSelectTiles

//-------------------------------------------------------------------------------------------------
/** Update logic for the multi select context sensitive GUI.  The command area mirrors the
	* focused type's representative, so the single-selection update does all the work:
	* availability, build queue panel, reload clocks, queue count badges. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextMultiSelect( void )
{

	// the representative can die between the deselect event and the UI re-evaluation
	if( m_currentSelectedDrawable == NULL || m_currentSelectedDrawable->getObject() == NULL )
		return;

	updateContextCommand();

	// the update may have traded the portrait for a build queue - keep the type strip alive
	updateMultiSelectStrip();

}  // end updateContextMultiSelect
