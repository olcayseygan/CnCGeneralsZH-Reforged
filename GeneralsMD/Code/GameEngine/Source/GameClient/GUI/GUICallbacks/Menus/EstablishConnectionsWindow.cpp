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

///// EstablishConnectionsWindow.cpp /////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine


#include "GameClient/GameWindowManager.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/EstablishConnectionsMenu.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameClient/MenuBoxParts.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"

static WindowLayout *establishConnectionsLayout;

static NameKeyType buttonQuitID = NAMEKEY_INVALID;
static NameKeyType staticPlayer1NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer2NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer3NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer4NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer5NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer6NameID = NAMEKEY_INVALID;
static NameKeyType staticPlayer7NameID = NAMEKEY_INVALID;

static NameKeyType staticPlayer1StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer2StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer3StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer4StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer5StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer6StatusID = NAMEKEY_INVALID;
static NameKeyType staticPlayer7StatusID = NAMEKEY_INVALID;

static GameWindow *buttonQuitWindow = NULL;
static GameWindow *staticPlayer1Name = NULL;
static GameWindow *staticPlayer2Name = NULL;
static GameWindow *staticPlayer3Name = NULL;
static GameWindow *staticPlayer4Name = NULL;
static GameWindow *staticPlayer5Name = NULL;
static GameWindow *staticPlayer6Name = NULL;
static GameWindow *staticPlayer7Name = NULL;

static GameWindow *staticPlayer1Status = NULL;
static GameWindow *staticPlayer2Status = NULL;
static GameWindow *staticPlayer3Status = NULL;
static GameWindow *staticPlayer4Status = NULL;
static GameWindow *staticPlayer5Status = NULL;
static GameWindow *staticPlayer6Status = NULL;
static GameWindow *staticPlayer7Status = NULL;

static const char *layoutFilename = "GameSpyGameOptionsMenu.wnd";
static const char *parentName = "GameSpyGameOptionsMenuParent";
static const char *gadgetsToHide[] =
{
	"MapWindow",
	"StaticTextGameName",
	"StaticTextTeam",
	"StaticTextHandicap",
	"StaticTextFaction",
	"StaticTextColor",
	"StaticTextPlayers",
	"TextEntryMapDisplay",
	"ButtonSelectMap",
	"ButtonStart",
	"StaticTextMapPreview",
	NULL // keep this last
};
static const char *perPlayerGadgetsToHide[] =
{
	"ComboBoxTeam",
	"ComboBoxHandicap",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
	"ComboBoxAIPersona",
	"ComboBoxPlayer",
	"ButtonAccept",
	"GenericPing",
	"StaticTextPlayer",
	"SeatCard",
	"SeatBar",
	"SeatSwatch",
	"SeatTeam",
	//"ButtonStartPosition",
	NULL // keep this last
};

static const char *qmlayoutFilename = "WOLQuickMatchMenu.wnd";
static const char *qmparentName = "WOLQuickMatchMenuParent";
static const char *qmgadgetsToHide[] =
{
	"StaticTextTitle",
	"ButtonBack",
	"ButtonOptions",
	"ButtonBuddies",
	"ButtonWiden",
	"ButtonStop",
	"ButtonStart",
	NULL // keep this last
};
static const char *qmperPlayerGadgetsToHide[] =
{
	//"ButtonStartPosition",
	NULL // keep this last
};

static void showGameSpyGameOptionsUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );

}
static void showGameSpyQMUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, qmlayoutFilename, qmparentName, qmgadgetsToHide, qmperPlayerGadgetsToHide );
}

//-------------------------------------------------------------------------------------------------
// Establish Connections in the menu language (docs/plans/MENU-REDESIGN.md, package 5; the boxes
// README's boxes-6): a 720 status box over the room, CONNECTING and how many are ready, a row a
// player (his stripe, his name, white or gold for the local player, a lamp and his state in words),
// QUIT in the foot and not lit, as Enter must not leave a starting game.  The rows are drawn from
// TheEstablishConnectionsMenu, every seat the engine has.
//-------------------------------------------------------------------------------------------------
static const Int CONNECTION_ROW = 46;

static const char *connectionStateKey( NATConnectionState state )
{
	switch( state )
	{
		case NATCONNECTIONSTATE_WAITINGFORMANGLERRESPONSE:	return "GUI:WaitingForManglerResponse";
		case NATCONNECTIONSTATE_WAITINGFORMANGLEDPORT:			return "GUI:WaitingForMangledPort";
		case NATCONNECTIONSTATE_WAITINGFORRESPONSE:					return "GUI:WaitingForResponse";
		case NATCONNECTIONSTATE_DONE:												return "GUI:ConnectionDone";
		case NATCONNECTIONSTATE_FAILED:											return "GUI:ConnectionFailed";
		case NATCONNECTIONSTATE_WAITINGTOBEGIN:							return "GUI:WaitingToBeginConnection";
		default:																						return "GUI:UnknownConnectionState";
	}
}

static Color connectionLamp( NATConnectionState state )
{
	switch( state )
	{
		case NATCONNECTIONSTATE_DONE:						return UIStyle::ACTIVE;
		case NATCONNECTIONSTATE_FAILED:					return GameMakeColor( 255, 104, 72, 255 );
		case NATCONNECTIONSTATE_WAITINGTOBEGIN:	return UIStyle::MUTED;
		default:																return UIStyle::INK;
	}
}

/// the colours of the room's human seats but the local one, in the order NAT names them
static void seatColours( Color colours[ MAX_SLOTS ], Color &local )
{
	for( Int i = 0; i < MAX_SLOTS; ++i )
		colours[ i ] = UIStyle::MUTED;
	local = UIStyle::MUTED;
	if( TheGameSpyGame == NULL || TheMultiplayerSettings == NULL )
		return;
	Int n = 0;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = TheGameSpyGame->getConstSlot( i );
		if( slot == NULL || !slot->isHuman() )
			continue;
		const MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor( slot->getApparentColor() );
		const Color colour = def ? def->getColor() : UIStyle::MUTED;
		if( i == TheGameSpyGame->getLocalSlotNum() )
			local = colour;
		else
			colours[ n++ ] = colour;
	}
}

static void connectionsBoxDraw( GameWindow *window, WinInstanceData * )
{
	static MenuBox::TextPool pool;
	static Int placedHeight = -1, placedWidth = -1, placedScreen = -1;
	static GameWindow *placedFor = NULL;
	if( TheEstablishConnectionsMenu == NULL )
		return;
	pool.begin();

	// the rows: the local player first, then every seat NAT has named
	UnicodeString names[ MAX_SLOTS + 1 ];
	NATConnectionState states[ MAX_SLOTS + 1 ];
	Color colours[ MAX_SLOTS + 1 ];
	Color others[ MAX_SLOTS ], localColour;
	seatColours( others, localColour );
	Int rows = 0;
	const GameSlot *localSlot = TheGameSpyGame ? TheGameSpyGame->getConstSlot( TheGameSpyGame->getLocalSlotNum() ) : NULL;
	if( localSlot )
	{
		names[ rows ] = localSlot->getName();
		states[ rows ] = NATCONNECTIONSTATE_DONE;
		colours[ rows++ ] = localColour;
	}
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		if( TheEstablishConnectionsMenu->getPlayerName( i ).isEmpty() )
			continue;
		names[ rows ] = TheEstablishConnectionsMenu->getPlayerName( i );
		states[ rows ] = TheEstablishConnectionsMenu->getPlayerStatus( i );
		colours[ rows++ ] = others[ i ];
	}
	Int ready = 0;
	for( Int i = 0; i < rows; ++i )
		ready += states[ i ] == NATCONNECTIONSTATE_DONE ? 1 : 0;

	const Int height = UIStyle::BOX_HEAD + 16 + rows * CONNECTION_ROW + 16 + UIStyle::BOX_FOOT;
	const IRegion2D at1080 = UIStyle::boxRect1080( UIStyle::BOX_QUESTION, height, FALSE );
	if( window != placedFor || height != placedHeight || TheDisplay->getWidth() != placedWidth || TheDisplay->getHeight() != placedScreen )
	{
		placedFor = window;
		placedHeight = height;
		placedWidth = TheDisplay->getWidth();
		placedScreen = TheDisplay->getHeight();
		MenuBox::place( window, at1080 );
		MenuBox::place( buttonQuitWindow, UIStyle::boxKeyRect1080( at1080, 0 ) );
	}
	const IRegion2D r = MenuBox::onBoard( at1080 );
	DisplayString *title = pool.get( UIStyle::capitals( TheGameText->fetch( "GUI:Connecting" ) ) );
	UIStyle::boxShell( r, title, FALSE );
	UnicodeString count;
	count.format( TheGameText->fetch( "GUI:ReadyCount" ), ready, rows );
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	GameFont *name = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY + 2 );
	MenuBox::words( pool.get( count ), body, r.lo.x + UIStyle::px( 32.0f ) + title->getWidth() + UIStyle::px( 20.0f ),
		r.lo.y + UIStyle::px( 44.0f ), UIStyle::MUTED );

	const Int x0 = at1080.lo.x + UIStyle::BOX_PAD, x1 = at1080.hi.x - UIStyle::BOX_PAD;
	Int y = at1080.lo.y + UIStyle::BOX_HEAD + 16;
	for( Int i = 0; i < rows; ++i )
	{
		if( i % 2 )
			MenuBox::fill( UIStyle::boardRect( x0, y, x1, y + CONNECTION_ROW ), GameMakeColor( 255, 255, 255, 7 ) );
		const Int cy = UIStyle::boardY( (Real)y + CONNECTION_ROW / 2 );
		const Int half = UIStyle::px( 9.0f );
		MenuBox::stripe( UIStyle::boardX( (Real)x0 + 4 ), cy - half, cy + half, max( 2, UIStyle::px( 4.0f ) ), colours[ i ] );
		const Bool you = localSlot != NULL && i == 0;
		MenuBox::words( pool.get( UIStyle::fitName( names[ i ], name, UIStyle::px( 220.0f ) ) ), name, UIStyle::boardX( (Real)x0 + 16 ), cy,
			you ? UIStyle::GOLD : UIStyle::INK );
		MenuBox::lamp( UIStyle::boardX( (Real)x0 + 255 ), cy, max( 2, UIStyle::px( 5.0f ) ), connectionLamp( states[ i ] ) );
		MenuBox::words( pool.get( UIStyle::fitKey( connectionStateKey( states[ i ] ), body, UIStyle::px( (Real)( x1 - x0 - 274 ) ) ) ), body,
			UIStyle::boardX( (Real)x0 + 274 ), cy, states[ i ] == NATCONNECTIONSTATE_WAITINGTOBEGIN ? UIStyle::MUTED : UIStyle::INK );
		y += CONNECTION_ROW;
	}
	UIStyle::boxFootRule( r );
}

static void InitEstablishConnectionsDialog( void ) {
	buttonQuitID = TheNameKeyGenerator->nameToKey( AsciiString("EstablishConnectionsScreen.wnd:ButtonQuit") );
	buttonQuitWindow = TheWindowManager->winGetWindowFromId(NULL, buttonQuitID);
	GameWindow *box = TheWindowManager->winGetWindowFromId(NULL, TheNameKeyGenerator->nameToKey( AsciiString("EstablishConnectionsScreen.wnd:EstablishConnectionsWindow") ));
	if (box != NULL) {
		box->winSetDrawFunc(connectionsBoxDraw);
	}
	UIStyle::setMenuKey(buttonQuitWindow, FALSE);
	if (buttonQuitWindow != NULL) {
		GadgetButtonSetText(buttonQuitWindow, UIStyle::capitals(buttonQuitWindow->winGetInstanceData()->getText()));
	}
}

void ShowEstablishConnectionsWindow( void ) {
	if (establishConnectionsLayout == NULL) {
		establishConnectionsLayout = TheWindowManager->winCreateLayout( "Menus/EstablishConnectionsScreen.wnd" );
		InitEstablishConnectionsDialog();
	}
	establishConnectionsLayout->hide(FALSE);
	TheWindowManager->winSetFocus(establishConnectionsLayout->getFirstWindow());
	if (!TheGameSpyGame->isQMGame())
	{
		showGameSpyGameOptionsUnderlyingGUIElements(FALSE);
	}
	else
	{
		showGameSpyQMUnderlyingGUIElements(FALSE);
	}
}

void HideEstablishConnectionsWindow( void ) {
	if (establishConnectionsLayout == NULL) {
//		establishConnectionsLayout = TheWindowManager->winCreateLayout( "Menus/EstablishConnectionsScreen.wnd" );
//		InitEstablishConnectionsDialog();
		return;
	}
//	establishConnectionsLayout->hide(TRUE);
//	establishConnectionsLayout->hide(TRUE);
//	TheWindowManager->winDestroy(establishConnectionsLayout);
	establishConnectionsLayout->destroyWindows();
	establishConnectionsLayout->deleteInstance();
	establishConnectionsLayout = NULL;
	if (!TheGameSpyGame->isQMGame())
	{
		showGameSpyGameOptionsUnderlyingGUIElements(TRUE);
	}
	else
	{
		showGameSpyQMUnderlyingGUIElements(TRUE);
	}
}

WindowMsgHandledType EstablishConnectionsControlInput(GameWindow *window, UnsignedInt msg,
																											WindowMsgData mData1, WindowMsgData mData2) {

	return MSG_IGNORED;
}

WindowMsgHandledType EstablishConnectionsControlSystem(GameWindow *window, UnsignedInt msg,
																											 WindowMsgData mData1, WindowMsgData mData2) {

	switch (msg) {
		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonQuitID) {
					TheEstablishConnectionsMenu->abortGame();
				}
				break;
			}
	} // end switch
	return MSG_HANDLED;
}
