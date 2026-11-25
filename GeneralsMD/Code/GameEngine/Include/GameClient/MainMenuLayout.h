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

// FILE: MainMenuLayout.h /////////////////////////////////////////////////////////////////////////
//
// The main menu's geometry, with no window and no font, so a test can check it at any screen size:
// the rail on the right and the panels it opens to its left (docs/plans/MENU-REDESIGN.md, package 4;
// the rects are experiments/proofs/2026-11-24/screens/main-menu/README-main-menu.md in the wrapper).
// Every number is at 1080 on the 1920 board.  The menu stands on one scale, the screen's height over
// 1080, anchored to the screen's right edge, so the rail keeps its size and its 32 px off the edge at
// 21:9 and 4:3 alike and the shell map fills whatever is left.  MainMenu.cpp places and draws by it.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _MAINMENULAYOUT_H_
#define _MAINMENULAYOUT_H_

#include "Lib/BaseType.h"

namespace MainMenuLayout
{
	const Int BOARD_WIDTH = 1920;
	const Int BOARD_HEIGHT = 1080;

	// the rail: 552 wide, 32 off the right edge, 28 off the top, its height by what it holds
	const Int RAIL_LEFT = 1336;
	const Int RAIL_RIGHT = 1888;
	const Int RAIL_TOP = 28;
	const Int RAIL_PAD = 28;											///< content inset: 1364 to 1860
	const Int CONTENT_LEFT = RAIL_LEFT + RAIL_PAD;
	const Int CONTENT_RIGHT = RAIL_RIGHT - RAIL_PAD;
	const Int FAMILY_TOP = 56;										///< the family line and the version, 18 tall
	const Int WORDMARK_TOP = 84;									///< 60 tall
	const Int HEADER_RULE = 156;									///< 2 tall
	const Int LIST_TOP = 180;											///< a sub-list's caption, or the first button
	const Int CAPTION_STEP = 34;
	const Int BUTTON_HEIGHT = 62;
	const Int BUTTON_STEP = 72;
	const Int SPLIT = 24;													///< the air before the leaving button
	const Int RAIL_FOOT = 28;											///< under the leaving button

	// a panel the rail opens stands 48 to its left
	const Int PANEL_GAP = 48;
	const Int PANEL_RIGHT = RAIL_LEFT - PANEL_GAP;
	const Int PANEL_PAD = 28;
	const Int PANEL_TITLE_STEP = 68;							///< the title, then the lit rule
	const Int PANEL_HEAD = PANEL_PAD + PANEL_TITLE_STEP + 2 + 24;

	const Int CAMPAIGN_WIDTH = 472;
	const Int CAMPAIGN_LEFT = PANEL_RIGHT - CAMPAIGN_WIDTH;
	const Int TAB_HEIGHT = 44;
	const Int TAB_GAP = 10;
	const Int LABEL_STEP = 28;										///< MISSION, DIFFICULTY: the label and its air
	const Int TILE_HEIGHT = 72;
	const Int SECTION_GAP = 26;
	const Int KEY_GAP = 10;

	const Int CARD_WIDTH = 340;
	const Int CARD_HEIGHT = 304;
	const Int CARD_GAP = 24;
	const Int MODS_WIDTH = 2 * PANEL_PAD + 2 * CARD_WIDTH + CARD_GAP;
	const Int MODS_LEFT = PANEL_RIGHT - MODS_WIDTH;
	const Int MODS_HEIGHT = PANEL_HEAD + CARD_HEIGHT + PANEL_PAD;

	const Int TOOLTIP_HEIGHT = 42;
	const Int TOOLTIP_OFF = 38;										///< the card's right edge this far left of what it names
	const Int TOOLTIP_UNDER = 14;									///< or this far under a panel

	struct Rect { Int left, top, right, bottom; };

	inline Rect rect( Int left, Int top, Int right, Int bottom )
	{
		Rect r = { left, top, right, bottom };
		return r;
	}

	/// the rail's height holding items buttons and the leaving one, with a caption over them or not
	inline Int railHeight( Bool caption, Int items )
	{
		const Int rows = items + 1;
		return ( LIST_TOP - RAIL_TOP ) + ( caption ? CAPTION_STEP : 0 ) + rows * BUTTON_HEIGHT
			+ ( rows - 1 ) * ( BUTTON_STEP - BUTTON_HEIGHT ) + SPLIT + RAIL_FOOT;
	}

	inline Rect railRect( Bool caption, Int items )
	{
		return rect( RAIL_LEFT, RAIL_TOP, RAIL_RIGHT, RAIL_TOP + railHeight( caption, items ) );
	}

	/// button index of a rail of items; index == items is the leaving button, set apart
	inline Rect railButton( Bool caption, Int index, Int items )
	{
		Int top = LIST_TOP + ( caption ? CAPTION_STEP : 0 ) + index * BUTTON_STEP;
		if( index >= items )
			top += SPLIT - ( BUTTON_STEP - BUTTON_HEIGHT );
		return rect( CONTENT_LEFT, top, CONTENT_RIGHT, top + BUTTON_HEIGHT );
	}

	/// the campaign panel's rows: tabs when the side's original campaign is installed, mission tiles
	/// for a campaign (Generals' Challenge has none), the difficulty keys always
	struct CampaignPanel
	{
		Rect panel;
		Int tabsTop;				///< -1 without tabs
		Int missionLabel;		///< -1 without tiles
		Int tilesTop;
		Int difficultyLabel;
		Int keysTop;
	};

	inline CampaignPanel campaignPanel( Bool tabs, Bool tiles )
	{
		CampaignPanel p;
		Int y = RAIL_TOP + PANEL_HEAD;
		p.tabsTop = -1;
		p.missionLabel = -1;
		p.tilesTop = -1;
		if( tabs )
		{
			p.tabsTop = y;
			y += TAB_HEIGHT + SECTION_GAP;
		}
		if( tiles )
		{
			p.missionLabel = y;
			p.tilesTop = y + LABEL_STEP;
			y = p.tilesTop + TILE_HEIGHT + SECTION_GAP;
		}
		p.difficultyLabel = y;
		p.keysTop = y + LABEL_STEP;
		p.panel = rect( CAMPAIGN_LEFT, RAIL_TOP, PANEL_RIGHT, p.keysTop + BUTTON_HEIGHT + PANEL_PAD );
		return p;
	}

	/// the two tabs, Generals then Zero Hour, side by side over the content's width
	inline Rect campaignTab( Int index, Int top )
	{
		const Int left = CAMPAIGN_LEFT + PANEL_PAD, right = PANEL_RIGHT - PANEL_PAD;
		const Int half = ( right - left - TAB_GAP ) / 2;
		return index == 0 ? rect( left, top, left + half, top + TAB_HEIGHT ) : rect( right - half, top, right, top + TAB_HEIGHT );
	}

	/// mission tile index of count in one row: 12 apart up to five, 8 apart past that
	inline Rect missionTile( Int index, Int count, Int top )
	{
		const Int left = CAMPAIGN_LEFT + PANEL_PAD, right = PANEL_RIGHT - PANEL_PAD;
		if( count < 1 )
			count = 1;
		const Int gap = count <= 5 ? 12 : 8;
		const Real width = ( right - left - gap * ( count - 1 ) ) / (Real)count;
		const Int x0 = left + (Int)( index * ( width + gap ) + 0.5f );
		const Int x1 = left + (Int)( index * ( width + gap ) + width + 0.5f );
		return rect( x0, top, x1, top + TILE_HEIGHT );
	}

	/// Easy, Medium, Hard in one row
	inline Rect difficultyKey( Int index, Int top )
	{
		const Int left = CAMPAIGN_LEFT + PANEL_PAD, right = PANEL_RIGHT - PANEL_PAD;
		const Real width = ( right - left - 2 * KEY_GAP ) / 3.0f;
		const Int x0 = left + (Int)( index * ( width + KEY_GAP ) + 0.5f );
		const Int x1 = left + (Int)( index * ( width + KEY_GAP ) + width + 0.5f );
		return rect( x0, top, x1, top + BUTTON_HEIGHT );
	}

	/// the Mods panel stands on the rail's foot, so Mods' green line runs straight in
	inline Rect modsPanel( Int railBottom )
	{
		return rect( MODS_LEFT, railBottom - MODS_HEIGHT, PANEL_RIGHT, railBottom );
	}

	inline Rect modeCard( Int index, Int railBottom )
	{
		const Rect p = modsPanel( railBottom );
		const Int left = p.left + PANEL_PAD + index * ( CARD_WIDTH + CARD_GAP );
		const Int top = p.top + PANEL_HEAD;
		return rect( left, top, left + CARD_WIDTH, top + CARD_HEIGHT );
	}

	/// a length at 1080 on a screen this many pixels tall
	inline Int length( Real at1080, Int screenHeight )
	{
		const Int n = (Int)( at1080 * screenHeight / (Real)BOARD_HEIGHT + 0.5f );
		return n == 0 && at1080 > 0.0f ? 1 : n;
	}

	/// a board x at 1080 on a screen: measured from the right edge at the height's scale
	inline Int screenX( Int x1080, Int screenWidth, Int screenHeight )
	{
		return screenWidth - (Int)( ( BOARD_WIDTH - x1080 ) * screenHeight / (Real)BOARD_HEIGHT + 0.5f );
	}

	inline Int screenY( Int y1080, Int screenHeight )
	{
		return (Int)( y1080 * screenHeight / (Real)BOARD_HEIGHT + 0.5f );
	}

	inline Rect onScreen( const Rect &r, Int screenWidth, Int screenHeight )
	{
		return rect( screenX( r.left, screenWidth, screenHeight ), screenY( r.top, screenHeight ),
			screenX( r.right, screenWidth, screenHeight ), screenY( r.bottom, screenHeight ) );
	}
}

#endif // _MAINMENULAYOUT_H_
