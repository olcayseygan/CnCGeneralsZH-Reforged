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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: ReplayMenu.cpp /////////////////////////////////////////////////////////////////////
// Author: Chris The masta Huybregts, December 2001
// Description: Replay Menus
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEngine.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Display.h"
#include "GameClient/Image.h"
#include "GameClient/TankSkin.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"

// window ids -------------------------------------------------------------------------------------
static NameKeyType parentWindowID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType windowLadderID = NAMEKEY_INVALID;


// window pointers --------------------------------------------------------------------------------
static GameWindow *parentWindow = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *windowLadder = NULL;

//-------------------------------------------------------------------------------------------------
// -online: EA's ladder was a web page on its dead servers.  Here it is a list the screen builds itself,
// 20 rows a page from the service's ladder answer, the player's own row under them.
//-------------------------------------------------------------------------------------------------
enum { LADDER_PAGE = 20, LADDER_COLUMNS = 6 };
static GameWindow *listboxLadder = NULL;
static GameWindow *buttonPrev = NULL;
static GameWindow *buttonNext = NULL;
static GameWindow *titleText = NULL;
static GameWindow *headerRule = NULL;
static Int s_ladderOffset = 0;
static Int s_ladderShown = -1;			///< the OnlineLadderState last drawn, -1 to draw again

GameWindow *WOLMakeButtonLike( GameWindow *parent, GameWindow *model, Int x, Int y, Int w, Int h, const char *label )
{
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	WinInstanceData *look = model ? model->winGetInstanceData() : NULL;
	if (look)
	{
		for (Int i = 0; i < MAX_DRAW_DATA; ++i)
		{
			inst.m_enabledDrawData[i] = look->m_enabledDrawData[i];
			inst.m_disabledDrawData[i] = look->m_disabledDrawData[i];
			inst.m_hiliteDrawData[i] = look->m_hiliteDrawData[i];
		}
		inst.m_enabledText = look->m_enabledText;
		inst.m_disabledText = look->m_disabledText;
		inst.m_hiliteText = look->m_hiliteText;
	}
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent,
		WIN_STATUS_ENABLED | (look ? WIN_STATUS_IMAGE : 0), x, y, w, h, &inst,
		model ? model->winGetFont() : NULL, look == NULL );
	if (button)
		GadgetButtonSetText( button, TheGameText->fetch( label ) );
	return button;
}

/* One draw-data slot of a window in one state, as a .wnd line spells it. */
enum { LOOK_ENABLED, LOOK_DISABLED, LOOK_HILITE };
static void setLook( GameWindow *w, Int state, Int index, const char *image, Color color, Color border )
{
	if (!w)
		return;
	const Image *img = image ? TheMappedImageCollection->findImageByName( image ) : NULL;
	switch (state)
	{
		case LOOK_ENABLED:	w->winSetEnabledImage( index, img ); w->winSetEnabledColor( index, color ); w->winSetEnabledBorderColor( index, border ); break;
		case LOOK_DISABLED:	w->winSetDisabledImage( index, img ); w->winSetDisabledColor( index, color ); w->winSetDisabledBorderColor( index, border ); break;
		default:						w->winSetHiliteImage( index, img ); w->winSetHiliteColor( index, color ); w->winSetHiliteBorderColor( index, border ); break;
	}
}

/* The online lobby's player list (WOLCustomLobby.wnd ListboxPlayers) as retail draws it, slot for slot,
	 on the code-made list: its highlight art, its scroll arrows and thumb, its white text.  Only the
	 ground differs: the panel's see-through black with the FRAME edge (UI-STYLE.md), where the lobby's
	 list sits on a panel already. */
static void retailListLook( GameWindow *list )
{
	if (!list)
		return;
	const Color none = GameMakeColor( 255, 255, 255, 0 );
	const Color white = GameMakeColor( 254, 254, 254, 255 );
	const Color green = GameMakeColor( 0, 128, 0, 255 );
	setLook( list, LOOK_ENABLED, 0, NULL, GameMakeColor( 255, 4, 0, 0 ), GameMakeColor( 49, 55, 168, 255 ) );
	setLook( list, LOOK_ENABLED, 1, "ListBoxHiliteItemLeftEnd", GameMakeColor( 255, 255, 0, 255 ), white );
	setLook( list, LOOK_ENABLED, 2, "ListBoxHiliteItemRightEnd", none, none );
	setLook( list, LOOK_ENABLED, 3, "ListBoxHiliteItemRepeatingCenter", none, none );
	setLook( list, LOOK_ENABLED, 4, "ListBoxHiliteItemSmallRepeatingCenter", none, none );
	setLook( list, LOOK_DISABLED, 0, NULL, GameMakeColor( 255, 4, 0, 0 ), GameMakeColor( 49, 55, 168, 255 ) );
	setLook( list, LOOK_DISABLED, 1, NULL, GameMakeColor( 192, 192, 192, 255 ), white );
	setLook( list, LOOK_HILITE, 0, NULL, GameMakeColor( 255, 4, 0, 0 ), GameMakeColor( 49, 55, 168, 255 ) );
	setLook( list, LOOK_HILITE, 1, "ListBoxHiliteSelectedItemLeftEnd", white, green );
	setLook( list, LOOK_HILITE, 2, "ListBoxHiliteSelectedItemRightEnd", none, none );
	setLook( list, LOOK_HILITE, 3, "ListBoxHiliteSelectedItemRepeatingCenter", none, none );
	setLook( list, LOOK_HILITE, 4, "ListBoxHiliteSelectedItemSmallRepeatingCenter", none, none );
	list->winSetEnabledTextColors( white, GameMakeColor( 0, 0, 0, 255 ) );
	list->winSetDisabledTextColors( GameMakeColor( 192, 192, 192, 255 ), GameMakeColor( 64, 64, 64, 255 ) );
	list->winSetHiliteTextColors( GameMakeColor( 128, 128, 255, 255 ), GameMakeColor( 0, 0, 128, 255 ) );

	static const char *arrows[2][4] = {
		{ "VSliderUpButtonEnabled", "VSliderUpButtonHiliteSelected", "VSliderUpButtonDisabled", "VSliderUpButtonHilite" },
		{ "VSliderDownButtonEnabled", "VSliderDownButtonHiliteSelected", "VSliderDownButtonDisabled", "VSliderDownButtonHilite" },
	};
	GameWindow *buttons[2] = { GadgetListBoxGetUpButton( list ), GadgetListBoxGetDownButton( list ) };
	for (Int i = 0; i < 2; ++i)
	{
		setLook( buttons[i], LOOK_ENABLED, 0, arrows[i][0], GameMakeColor( 255, 0, 0, 255 ), GameMakeColor( 255, 128, 128, 255 ) );
		setLook( buttons[i], LOOK_ENABLED, 1, arrows[i][1], GameMakeColor( 255, 255, 0, 255 ), white );
		setLook( buttons[i], LOOK_DISABLED, 0, arrows[i][2], GameMakeColor( 128, 128, 128, 255 ), GameMakeColor( 192, 192, 192, 255 ) );
		setLook( buttons[i], LOOK_DISABLED, 1, NULL, GameMakeColor( 192, 192, 192, 255 ), white );
		setLook( buttons[i], LOOK_HILITE, 0, arrows[i][3], GameMakeColor( 0, 255, 0, 255 ), green );
		setLook( buttons[i], LOOK_HILITE, 1, arrows[i][1], white, green );
	}

	GameWindow *slider = GadgetListBoxGetSlider( list );
	setLook( slider, LOOK_ENABLED, 0, NULL, GameMakeColor( 255, 190, 0, 0 ), GameMakeColor( 47, 55, 168, 255 ) );
	setLook( slider, LOOK_DISABLED, 0, NULL, GameMakeColor( 128, 128, 128, 0 ), GameMakeColor( 148, 112, 0, 255 ) );
	setLook( slider, LOOK_HILITE, 0, NULL, GameMakeColor( 0, 255, 0, 0 ), GameMakeColor( 49, 55, 168, 255 ) );
	GameWindow *thumb = slider ? slider->winGetChild() : NULL;
	setLook( thumb, LOOK_ENABLED, 0, "ScrollBarThumbEnabled", GameMakeColor( 255, 4, 0, 0 ), GameMakeColor( 255, 243, 28, 255 ) );
	setLook( thumb, LOOK_ENABLED, 1, "ScrollBarThumbHiliteSelected", GameMakeColor( 255, 255, 0, 255 ), white );
	setLook( thumb, LOOK_DISABLED, 0, "ScrollBarThumbDisabled", GameMakeColor( 128, 128, 128, 255 ), GameMakeColor( 192, 192, 192, 255 ) );
	setLook( thumb, LOOK_DISABLED, 1, NULL, GameMakeColor( 192, 192, 192, 255 ), white );
	setLook( thumb, LOOK_HILITE, 0, "ScrollBarThumbHilite", GameMakeColor( 0, 255, 0, 255 ), green );
	setLook( thumb, LOOK_HILITE, 1, "ScrollBarThumbHiliteSelected", white, green );
}

// the hub's 800x600 authoring grid, in pixels
#define LADDER_X( v ) ((Int)((v) * (TheDisplay->getWidth() / 800.0f)))
#define LADDER_Y( v ) ((Int)((v) * (TheDisplay->getHeight() / 600.0f)))

/* The hub's frame (WOLWelcomeMenu.wnd, authored at 800x600): MainMenuRuler over the whole screen
	 where this layout fills it with flat navy, the GadgetParent panel at 41,41 to 759,560, a "Generals"
	 20 title in its top band with a rule under it at y 80, and its buttons in "Generals" 15 at the
	 panel's foot (Back at 560,520 to 732,556).  This layout's own Back is in Times New Roman.  Both pages
	 of this layout, the ladder and the skins, stand in it. */
static void createFrame( const char *title )
{
	parentWindow->winSetStatus( WIN_STATUS_IMAGE );
	parentWindow->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName( "MainMenuRuler" ) );
	parentWindow->winSetEnabledColor( 0, GameMakeColor( 2, 2, 2, 255 ) );

	windowLadder->winSetPosition( LADDER_X( 41 ), LADDER_Y( 41 ) );
	windowLadder->winSetSize( LADDER_X( 759 ) - LADDER_X( 41 ), LADDER_Y( 560 ) - LADDER_Y( 41 ) );
	windowLadder->winSetEnabledColor( 0, GameMakeColor( 2, 2, 2, 190 ) );
	windowLadder->winSetEnabledBorderColor( 0, GameMakeColor( 47, 55, 168, 255 ) );

	WinInstanceData ruleInst;
	ruleInst.init();
	BitSet( ruleInst.m_style, GWS_USER_WINDOW );
	headerRule = TheWindowManager->winCreate( parentWindow, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT,
		LADDER_X( 41 ), LADDER_Y( 80 ), LADDER_X( 759 ) - LADDER_X( 41 ), max( 1, LADDER_Y( 81 ) - LADDER_Y( 80 ) ), NULL, &ruleInst );
	if (headerRule)
	{
		headerRule->winSetEnabledColor( 0, GameMakeColor( 0, 0, 0, 255 ) );
		headerRule->winSetEnabledBorderColor( 0, GameMakeColor( 47, 55, 168, 255 ) );
	}

	WinInstanceData titleInst;
	titleInst.init();
	BitSet( titleInst.m_style, GWS_STATIC_TEXT );
	TextData titleData;
	memset( &titleData, 0, sizeof(titleData) );
	titleData.centeredVertically = TRUE;
	titleData.leftMargin = LADDER_X( 8 );
	titleText = TheWindowManager->gogoGadgetStaticText( parentWindow, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT,
		LADDER_X( 44 ), LADDER_Y( 41 ), LADDER_X( 752 ) - LADDER_X( 44 ), LADDER_Y( 80 ) - LADDER_Y( 41 ),
		&titleInst, &titleData, UIStyle::titleFont( 20 ), FALSE );
	if (titleText)
	{
		titleText->winSetEnabledTextColors( GameMakeColor( 255, 255, 255, 255 ), GameMakeColor( 0, 0, 0, 255 ) );
		titleText->winSetEnabledColor( 0, GameMakeColor( 2, 2, 2, 0 ) );
		titleText->winSetEnabledBorderColor( 0, GameMakeColor( 0, 0, 0, 0 ) );
		GadgetStaticTextSetText( titleText, TheGameText->fetch( title ) );
	}

	if (buttonBack)
	{
		buttonBack->winSetPosition( LADDER_X( 560 ), LADDER_Y( 520 ) );
		buttonBack->winSetSize( LADDER_X( 732 ) - LADDER_X( 560 ), LADDER_Y( 556 ) - LADDER_Y( 520 ) );
		buttonBack->winSetFont( UIStyle::titleFont( 15 ) );
		buttonBack->winBringToTop();
	}
}

static void createLadderGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;
	createFrame( "GUI:OnlineLadderButton" );
	const Int bw = LADDER_X( 732 ) - LADDER_X( 560 ), bh = LADDER_Y( 556 ) - LADDER_Y( 520 );
	const Int bx = LADDER_X( 560 ), by = LADDER_Y( 520 );

	// the list fills the panel between the rule and the button row
	const Int gap = LADDER_Y( 8 );
	const Int left = LADDER_X( 56 );
	const Int width = LADDER_X( 744 ) - left;
	const Int top = LADDER_Y( 81 ) + gap;
	const Int height = by - gap - top;

	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_SCROLL_LISTBOX | GWS_MOUSE_TRACK );
	ListboxData data;
	memset( &data, 0, sizeof(data) );
	data.listLength = LADDER_PAGE + 4;		// heading, the page, a gap and our own row
	data.columns = LADDER_COLUMNS;
	data.columnWidthPercentage = NEW Int[LADDER_COLUMNS];		// the listbox deletes it with itself
	static const Int widths[LADDER_COLUMNS] = { 10, 34, 14, 18, 12, 12 };
	for (Int i = 0; i < LADDER_COLUMNS; ++i)
		data.columnWidthPercentage[i] = widths[i];
	data.scrollBar = TRUE;
	data.forceSelect = FALSE;
	listboxLadder = TheWindowManager->gogoGadgetListBox( parentWindow, WIN_STATUS_ENABLED,
		left, top, width, height, &inst, &data, UIStyle::font( UIStyle::TEXT_BODY ), FALSE );
	retailListLook( listboxLadder );

	// Previous and Next on the Back button's row, to its left
	const Int buttonGap = UIStyle::px( 16.0f );
	buttonNext = WOLMakeButtonLike( parentWindow, buttonBack, bx - bw - buttonGap, by, bw, bh, "GUI:OnlineLadderNext" );
	buttonPrev = WOLMakeButtonLike( parentWindow, buttonBack, bx - 2 * (bw + buttonGap), by, bw, bh, "GUI:OnlineLadderPrev" );
}

static void destroyLadderGadgets( void )
{
	GameWindow *made[] = { listboxLadder, buttonPrev, buttonNext, titleText, headerRule };
	for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	listboxLadder = buttonPrev = buttonNext = titleText = headerRule = NULL;
}

static void addLadderRow( const OnlineLadderRow &r, Color color )
{
	UnicodeString cell;
	Int row;
	if (r.rank > 0)
		cell.format( u"%d", r.rank );
	else
		cell = UnicodeString( u"-" );
	row = GadgetListBoxAddEntryText( listboxLadder, cell, color, -1, 0 );
	cell.translate( r.nick );
	GadgetListBoxAddEntryText( listboxLadder, cell, color, row, 1 );
	if (r.hasRating)
		cell.format( u"%d", r.rating );
	else
		cell = UnicodeString( u"-" );
	GadgetListBoxAddEntryText( listboxLadder, cell, color, row, 2 );
	GadgetListBoxAddEntryText( listboxLadder, OnlineTierName( r.tier ), color, row, 3 );
	cell.format( u"%d", r.wins );
	GadgetListBoxAddEntryText( listboxLadder, cell, color, row, 4 );
	cell.format( u"%d", r.losses );
	GadgetListBoxAddEntryText( listboxLadder, cell, color, row, 5 );
}

static void addLadderLine( const UnicodeString &text )
{
	Int row = GadgetListBoxAddEntryText( listboxLadder, UnicodeString( u" " ), GameSpyColor[GSCOLOR_DEFAULT], -1, 0 );
	GadgetListBoxAddEntryText( listboxLadder, text, GameSpyColor[GSCOLOR_DEFAULT], row, 1 );
}

static void drawLadder( void )
{
	Int offset = 0;
	std::vector<OnlineLadderRow> rows;
	OnlineLadderRow me;
	Bool hasMe = FALSE;
	const OnlineLadderState state = OnlineGetLadder( offset, rows, me, hasMe );
	if ((Int)state == s_ladderShown || !listboxLadder)
		return;
	s_ladderShown = (Int)state;

	GadgetListBoxReset( listboxLadder );
	const Color heading = UIStyle::ACTIVE;		// the layouts' green column and section heads
	const char *titles[LADDER_COLUMNS] = { "GUI:OnlineLadderRank", "GUI:OnlineLadderPlayer", "GUI:OnlineLadderRating",
		"GUI:OnlineLadderTier", "GUI:OnlineLadderWins", "GUI:OnlineLadderLosses" };
	Int row = -1;
	for (Int i = 0; i < LADDER_COLUMNS; ++i)
		row = GadgetListBoxAddEntryText( listboxLadder, TheGameText->fetch( titles[i] ), heading, i == 0 ? -1 : row, i );

	if (state == ONLINE_LADDER_WAITING)
		addLadderLine( TheGameText->fetch( "GUI:OnlineLadderLoading" ) );
	else if (state == ONLINE_LADDER_FAILED)
		addLadderLine( TheGameText->fetch( "GUI:OnlineLadderUnavailable" ) );
	else
	{
		const AsciiString self = TheGameSpyInfo ? TheGameSpyInfo->getLocalName() : AsciiString::TheEmptyString;
		for (size_t i = 0; i < rows.size(); ++i)
			addLadderRow( rows[i], rows[i].nick.compareNoCase( self ) == 0 ? GameSpyColor[GSCOLOR_PLAYER_SELF] : GameSpyColor[GSCOLOR_PLAYER_NORMAL] );
		if (rows.empty())
			addLadderLine( TheGameText->fetch( "GUI:OnlineLadderEmpty" ) );
		if (hasMe)
		{
			addLadderLine( UnicodeString( u" " ) );
			addLadderRow( me, GameSpyColor[GSCOLOR_PLAYER_SELF] );
		}
	}
	if (buttonPrev)
		buttonPrev->winEnable( state != ONLINE_LADDER_WAITING && s_ladderOffset > 0 );
	if (buttonNext)
		buttonNext->winEnable( state == ONLINE_LADDER_READY && (Int)rows.size() >= LADDER_PAGE );
}

static void requestLadderPage( Int offset )
{
	s_ladderOffset = max( 0, offset );
	s_ladderShown = -1;
	OnlineRequestLadder( s_ladderOffset );
	drawLadder();
}

//-------------------------------------------------------------------------------------------------
// The skins page, the hub's SKINS button (WOLSkinsScreenOpen): one row per unit SkinsReforged.ini
// dresses, its name then Default and every skin for it, the account's pick pushed in.  A skin the
// account does not own stays on its row, disabled.  Save sends the whole loadout; once the site took
// it the next room and Quick Match seat it (OnlineSaveSkins).  The same layout as the ladder, so the
// same frame: no layout of its own to ship.
//-------------------------------------------------------------------------------------------------
struct SkinChoice
{
	GameWindow *button;
	Int id;									///< 0 for Default
};
struct SkinRow
{
	AsciiString unit;
	GameWindow *label;
	std::vector<SkinChoice> choices;
	Int pick;
};
static Bool s_skinsPage = FALSE;
static std::vector<SkinRow> s_skinRows;
static GameWindow *skinsStatus = NULL;
static GameWindow *buttonSave = NULL;
static Int s_skinsState = -1;					///< OnlineSkinsState last drawn
static UnsignedInt64 s_skinsOwnedShown = 0;
static Bool s_skinsDirty = TRUE;				///< draw again even with the state unchanged
static Bool s_skinsSaved = FALSE;			///< the last READY answers this page's Save

void WOLSkinsScreenOpen( void )
{
	s_skinsPage = TRUE;
	TheShell->push( AsciiString( "Menus/WOLLadderScreen.wnd" ) );
}

static GameWindow *makeLine( Int x, Int y, Int w, Int h, GameFont *font, Color color )
{
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_STATIC_TEXT );
	TextData data;
	memset( &data, 0, sizeof(data) );
	data.centeredVertically = TRUE;
	GameWindow *line = TheWindowManager->gogoGadgetStaticText( parentWindow, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT,
		x, y, w, h, &inst, &data, font, FALSE );
	if (line)
	{
		line->winSetEnabledTextColors( color, GameMakeColor( 0, 0, 0, 255 ) );
		line->winSetEnabledColor( 0, GameMakeColor( 2, 2, 2, 0 ) );
		line->winSetEnabledBorderColor( 0, GameMakeColor( 0, 0, 0, 0 ) );
	}
	return line;
}

/* The row's pick pushed in and green (ACTIVE, the open tab's colour), the rest up and white. */
static void markPicks( void )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			GameWindow *b = s_skinRows[r].choices[c].button;
			if (!b)
				continue;
			const Bool on = s_skinRows[r].choices[c].id == s_skinRows[r].pick;
			GadgetCheckLikeButtonSetVisualCheck( b, on );
			b->winSetEnabledTextColors( on ? UIStyle::ACTIVE : UIStyle::INK, GameMakeColor( 0, 0, 0, 255 ) );
		}
}

/* Each row's pick from what the account wears. */
static void picksFromLoadout( void )
{
	const SkinLoadout wear = TankSkin_localLoadout();
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		s_skinRows[r].pick = 0;
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].id && (wear & ((SkinLoadout)1 << s_skinRows[r].choices[c].id)))
				s_skinRows[r].pick = s_skinRows[r].choices[c].id;
	}
	markPicks();
}

static void createSkinsGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;
	createFrame( "GUI:OnlineSkinsButton" );
	windowLadder->winSetTooltip( UnicodeString::TheEmptyString );

	// rows in id order, a unit's skins on the row of its first
	for (Int id = 1; id <= TANK_SKIN_MAX_ID; ++id)
	{
		AsciiString unit, unitLabel, label;
		if (!TankSkin_describe( id, unit, unitLabel, label ))
			continue;
		size_t r = 0;
		while (r < s_skinRows.size() && s_skinRows[r].unit.compareNoCase( unit ) != 0)
			++r;
		if (r == s_skinRows.size())
		{
			SkinRow row;
			row.unit = unit;
			row.label = NULL;
			row.pick = 0;
			SkinChoice none = { NULL, 0 };
			row.choices.push_back( none );
			s_skinRows.push_back( row );
		}
		SkinChoice choice = { NULL, id };
		s_skinRows[r].choices.push_back( choice );
		if (s_skinRows[r].label == NULL)
		{
			// ponytail: one row of 44 a unit; past about nine units the rows reach the button row and want a list
			const Int y = LADDER_Y( 100 + 44 * (Int)r );
			s_skinRows[r].label = makeLine( LADDER_X( 64 ), y, LADDER_X( 240 ) - LADDER_X( 64 ), LADDER_Y( 36 ),
				UIStyle::font( UIStyle::TEXT_STRONG ), UIStyle::INK );
			UnicodeString name;
			if (unitLabel.isEmpty())
				name.translate( unit );
			else
				name = TheGameText->fetch( unitLabel );
			if (s_skinRows[r].label)
				GadgetStaticTextSetText( s_skinRows[r].label, name );
		}
	}

	// the choices, Default first, each the hub's button (176 wide, 8 apart); ponytail: three fit a row
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		const Int y = LADDER_Y( 100 + 44 * (Int)r );
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			const Int x0 = 248 + 184 * (Int)c;
			AsciiString unit, unitLabel, label = "GUI:OnlineSkinsDefault";
			if (s_skinRows[r].choices[c].id)
				TankSkin_describe( s_skinRows[r].choices[c].id, unit, unitLabel, label );
			GameWindow *b = WOLMakeButtonLike( parentWindow, buttonBack, LADDER_X( x0 ), y,
				LADDER_X( x0 + 176 ) - LADDER_X( x0 ), LADDER_Y( 36 ), label.isEmpty() ? "GUI:OnlineSkinsDefault" : label.str() );
			if (b)
			{
				b->winSetFont( UIStyle::titleFont( 15 ) );
				GadgetButtonEnableCheckLike( b, TRUE, FALSE );
			}
			s_skinRows[r].choices[c].button = b;
		}
	}

	skinsStatus = makeLine( LADDER_X( 64 ), LADDER_Y( 100 + 44 * (Int)s_skinRows.size() + 8 ),
		LADDER_X( 744 ) - LADDER_X( 64 ), LADDER_Y( 28 ), UIStyle::font( UIStyle::TEXT_BODY ), UIStyle::MUTED );

	const Int bw = LADDER_X( 732 ) - LADDER_X( 560 ), bh = LADDER_Y( 556 ) - LADDER_Y( 520 );
	buttonSave = WOLMakeButtonLike( parentWindow, buttonBack, LADDER_X( 560 ) - bw - UIStyle::px( 16.0f ), LADDER_Y( 520 ),
		bw, bh, "GUI:OnlineSkinsSave" );
	if (buttonSave)
		buttonSave->winSetFont( UIStyle::titleFont( 15 ) );

	s_skinsState = -1;
	s_skinsDirty = TRUE;
	s_skinsSaved = FALSE;
	picksFromLoadout();
	OnlineRequestSkins();		// a code redeemed on the site since the sign-in
}

static void destroySkinsGadgets( void )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
	{
		if (s_skinRows[r].label)
			TheWindowManager->winDestroy( s_skinRows[r].label );
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].button)
				TheWindowManager->winDestroy( s_skinRows[r].choices[c].button );
	}
	s_skinRows.clear();
	GameWindow *made[] = { skinsStatus, buttonSave, titleText, headerRule };
	for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	skinsStatus = buttonSave = titleText = headerRule = NULL;
}

static void drawSkins( void )
{
	UnsignedInt64 owned = 0;
	AsciiString error;
	const OnlineSkinsState state = OnlineGetSkins( owned, error );
	if (!s_skinsDirty && (Int)state == s_skinsState && owned == s_skinsOwnedShown)
		return;
	if (state == ONLINE_SKINS_READY && s_skinsState == (Int)ONLINE_SKINS_WAITING)
		picksFromLoadout();		// an answer arrived: what the site holds now, after a save or a fresh read
	s_skinsState = (Int)state;
	s_skinsOwnedShown = owned;
	s_skinsDirty = FALSE;
	DEBUG_LOG(( "ONLINE SKINS state=%d error=%s owned=%s worn=%s rows=%d\n", (Int)state, error.str(),
		TankSkin_slotField( owned ).str(), TankSkin_slotField( TankSkin_localLoadout() ).str(), (Int)s_skinRows.size() ));

	for (size_t r = 0; r < s_skinRows.size(); ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
		{
			const Int id = s_skinRows[r].choices[c].id;
			if (s_skinRows[r].choices[c].button)
				s_skinRows[r].choices[c].button->winEnable( id == 0 || (owned & ((SkinLoadout)1 << id)) != 0 );
		}
	if (buttonSave)
		buttonSave->winEnable( state != ONLINE_SKINS_WAITING && owned != 0 );

	const char *line;
	if (state == ONLINE_SKINS_WAITING)
		line = "GUI:OnlineSkinsWaiting";
	else if (state == ONLINE_SKINS_READY)
		line = s_skinsSaved ? "GUI:OnlineSkinsSaved" : owned == 0 ? "GUI:OnlineSkinsNone" : "GUI:OnlineSkinsHint";
	else if (error == "signedout")
		line = "GUI:OnlineSkinsSignedOut";
	else if (error == "throttled")
		line = "GUI:OnlineSkinsThrottled";
	else if (error == "unavailable")
		line = "GUI:OnlineSkinsUnavailable";
	else
		line = "GUI:OnlineSkinsRefused";		// not_owned, two_for_unit, unknown: the site's view changed under us
	if (skinsStatus)
	{
		const Bool bad = state == ONLINE_SKINS_FAILED || (state == ONLINE_SKINS_READY && owned == 0);
		skinsStatus->winSetEnabledTextColors( bad ? UIStyle::INK : s_skinsSaved && state == ONLINE_SKINS_READY ? UIStyle::ACTIVE : UIStyle::MUTED,
			GameMakeColor( 0, 0, 0, 255 ) );
		GadgetStaticTextSetText( skinsStatus, TheGameText->fetch( line ) );
	}
}

/* A choice clicked: that row's pick.  FALSE when the control is none of them. */
static Bool pickSkin( GameWindow *control )
{
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		for (size_t c = 0; c < s_skinRows[r].choices.size(); ++c)
			if (s_skinRows[r].choices[c].button == control)
			{
				s_skinRows[r].pick = s_skinRows[r].choices[c].id;
				markPicks();		// the click flipped the button itself; this puts every row right again
				s_skinsSaved = FALSE;
				s_skinsDirty = TRUE;
				drawSkins();
				return TRUE;
			}
	return FALSE;
}

static void saveSkins( void )
{
	SkinLoadout wear = 0;
	for (size_t r = 0; r < s_skinRows.size(); ++r)
		if (s_skinRows[r].pick)
			wear |= (SkinLoadout)1 << s_skinRows[r].pick;
	s_skinsSaved = TRUE;
	OnlineSaveSkins( wear );
	drawSkins();
}


//-------------------------------------------------------------------------------------------------
/** Initialize the single player menu */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenInit( WindowLayout *layout, void *userData )
{
	TheShell->showShellMap(TRUE);

	// get ids for our children controls
	parentWindowID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:LadderParent") );
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:ButtonBack") );
	windowLadderID = TheNameKeyGenerator->nameToKey( AsciiString("WOLLadderScreen.wnd:WindowLadder") );

	parentWindow = TheWindowManager->winGetWindowFromId( NULL, parentWindowID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWindow, buttonBackID );
	windowLadder = TheWindowManager->winGetWindowFromId( parentWindow, windowLadderID );

	//Load the listbox shiznit
//	PopulateReplayFileListbox(listboxReplayFiles);

	//TheWebBrowser->createBrowserWindow("Westwood", windowLadder);
	if (OnlineIsEnabled() && s_skinsPage)
	{
		createSkinsGadgets();
		drawSkins();
	}
	else if (OnlineIsEnabled())
	{
		createLadderGadgets();
		requestLadderPage( 0 );
	}
	else if (TheWebBrowser != NULL)
	{
		TheWebBrowser->createBrowserWindow("MessageBoard", windowLadder);
	}

	// show menu
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parentWindow );

}  // end ReplayMenuInit

//-------------------------------------------------------------------------------------------------
/** single player menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenShutdown( WindowLayout *layout, void *userData )
{

	destroyLadderGadgets();
	destroySkinsGadgets();
	s_skinsPage = FALSE;		// the hub's LADDER opens the ladder again
	if (TheWebBrowser != NULL && !OnlineIsEnabled())
	{
		TheWebBrowser->closeBrowserWindow(windowLadder);
	}

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end ReplayMenuShutdown

//-------------------------------------------------------------------------------------------------
/** single player menu update method */
//-------------------------------------------------------------------------------------------------
void WOLLadderScreenUpdate( WindowLayout *layout, void *userData )
{
	if (OnlineIsEnabled())
	{
		OnlineUpdate();		// the lines this screen waits on, whatever lies under it on the shell stack
		if (s_skinsPage)
			drawSkins();
		else
			drawLadder();
	}
}  // end ReplayMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Replay menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLadderScreenInput( GameWindow *window, UnsignedInt msg,
																						WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end ReplayMenuInput

//-------------------------------------------------------------------------------------------------
/** single player menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLadderScreenSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			
			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input
		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == buttonBackID )
			{

				// thou art directed to return to thy known solar system immediately!
				TheShell->pop();

			}  // end else if
			else if( control == buttonPrev )
			{
				requestLadderPage( s_ladderOffset - LADDER_PAGE );
			}
			else if( control == buttonNext )
			{
				requestLadderPage( s_ladderOffset + LADDER_PAGE );
			}
			else if( control == buttonSave )
			{
				saveSkins();
			}
			else
			{
				pickSkin( control );
			}

			break;

		}  // end selected

		default:
			return MSG_IGNORED;
	}  // end switch

	return MSG_HANDLED;
}  // end ReplayMenuSystem

