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
#include "GameClient/Image.h"
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
static Int s_ladderOffset = 0;
static Int s_ladderShown = -1;			///< the OnlineLadderState last drawn, -1 to draw again

static GameWindow *makeButtonLike( GameWindow *model, Int x, Int y, Int w, Int h, const char *label )
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
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parentWindow,
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
	setLook( list, LOOK_ENABLED, 0, NULL, UIStyle::PANEL, UIStyle::FRAME );
	setLook( list, LOOK_ENABLED, 1, "ListBoxHiliteItemLeftEnd", GameMakeColor( 255, 255, 0, 255 ), white );
	setLook( list, LOOK_ENABLED, 2, "ListBoxHiliteItemRightEnd", none, none );
	setLook( list, LOOK_ENABLED, 3, "ListBoxHiliteItemRepeatingCenter", none, none );
	setLook( list, LOOK_ENABLED, 4, "ListBoxHiliteItemSmallRepeatingCenter", none, none );
	setLook( list, LOOK_DISABLED, 0, NULL, UIStyle::PANEL, UIStyle::FRAME );
	setLook( list, LOOK_DISABLED, 1, NULL, GameMakeColor( 192, 192, 192, 255 ), white );
	setLook( list, LOOK_HILITE, 0, NULL, UIStyle::PANEL, UIStyle::FRAME );
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

static void createLadderGadgets( void )
{
	if (!parentWindow || !windowLadder)
		return;
	Int px, py, lx, ly, lw, lh;
	parentWindow->winGetScreenPosition( &px, &py );
	windowLadder->winGetScreenPosition( &lx, &ly );
	windowLadder->winGetSize( &lw, &lh );
	lx -= px;
	ly -= py;

	Int bx = lx + lw - 160, by = ly + lh + 8, bw = 160, bh = 44;
	if (buttonBack)
	{
		buttonBack->winGetScreenPosition( &bx, &by );
		buttonBack->winGetSize( &bw, &bh );
		bx -= px;
		by -= py;
	}

	// a framed panel in the middle of the board, above the button row: about the lobby's game list
	const Int gap = UIStyle::px( 16.0f );
	const Int width = min( lw, UIStyle::px( 1200.0f ) );
	const Int left = UIStyle::boardX( 960.0f ) - px - width / 2;
	const Int top = max( ly, UIStyle::boardY( 140.0f ) - py );
	const Int height = max( UIStyle::px( 200.0f ), by - gap - top );

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
	buttonNext = makeButtonLike( buttonBack, bx - bw - gap, by, bw, bh, "GUI:OnlineLadderNext" );
	buttonPrev = makeButtonLike( buttonBack, bx - 2 * (bw + gap), by, bw, bh, "GUI:OnlineLadderPrev" );
}

static void destroyLadderGadgets( void )
{
	GameWindow *made[] = { listboxLadder, buttonPrev, buttonNext };
	for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); ++i)
		if (made[i])
			TheWindowManager->winDestroy( made[i] );
	listboxLadder = buttonPrev = buttonNext = NULL;
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
	if (OnlineIsEnabled())
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

			break;

		}  // end selected

		default:
			return MSG_IGNORED;
	}  // end switch

	return MSG_HANDLED;
}  // end ReplayMenuSystem

