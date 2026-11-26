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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////
// FILE: NetworkDirectConnect.cpp
// Author: Bryan Cleveland, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"
#include "gamespy/peer/peer.h"

#include "Common/QuotedPrintable.h"
#include "Common/UserPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/UIStyle.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// window ids ------------------------------------------------------------------------------

// Window Pointers ------------------------------------------------------------------------

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

extern Bool LANbuttonPushed;
extern Bool LANisShuttingDown;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;

static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonHostID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;
static NameKeyType editPlayerNameID = NAMEKEY_INVALID;
static NameKeyType comboboxRemoteIPID = NAMEKEY_INVALID;
static NameKeyType staticLocalIPID = NAMEKEY_INVALID;

static GameWindow *buttonBack = NULL;
static GameWindow *buttonHost = NULL;
static GameWindow *buttonJoin = NULL;
static GameWindow *editPlayerName = NULL;
static GameWindow *comboboxRemoteIP = NULL;
static GameWindow *staticLocalIP = NULL;

void PopulateRemoteIPComboBox()
{
	LANPreferences userprefs;
	GadgetComboBoxReset(comboboxRemoteIP);

	Int numRemoteIPs = userprefs.getNumRemoteIPs();
	Color white = GameMakeColor(255,255,255,255);

	for (Int i = 0; i < numRemoteIPs; ++i)
	{
		UnicodeString entry;
		entry = userprefs.getRemoteIPEntry(i);
		GadgetComboBoxAddEntry(comboboxRemoteIP, entry, white);
	}

	if (numRemoteIPs > 0)
	{
		GadgetComboBoxSetSelectedPos(comboboxRemoteIP, 0, TRUE);
	}
	userprefs.write();
}

void UpdateRemoteIPList()
{
	Int n1[4], n2[4];
	LANPreferences prefs;
	Int numEntries = GadgetComboBoxGetLength(comboboxRemoteIP);
	Int currentSelection = -1;
	GadgetComboBoxGetSelectedPos(comboboxRemoteIP, &currentSelection);
	UnicodeString unisel = GadgetComboBoxGetText(comboboxRemoteIP);
	AsciiString sel;
	sel.translate(unisel);

//	UnicodeString newEntry = prefs.getRemoteIPEntry(0);
	UnicodeString newEntry = unisel;
	UnicodeString newIP;
	newEntry.nextToken(&newIP, UnicodeString(u":"));
	// WideCharScan, not swscanf: newIP.str() is a WideChar* and swscanf reads a wchar_t*, which is
	// a different width off Windows.  See Lib/WideCharFns.h.
	Int numFields = WideCharScan(newIP.str(), u"%d.%d.%d.%d", &(n1[0]), &(n1[1]), &(n1[2]), &(n1[3]));

	if (numFields != 4) {
		// this is not a properly formatted IP, don't change a thing.
		return;
	}

	prefs["RemoteIP0"] = sel;

	Int currentINIEntry = 1;

	for (Int i = 0; i < numEntries; ++i)
	{
		if (i != currentSelection)
		{
			GadgetComboBoxSetSelectedPos(comboboxRemoteIP, i, FALSE);
			UnicodeString uni;
			uni = GadgetComboBoxGetText(comboboxRemoteIP);
			AsciiString ascii;
			ascii.translate(uni);

			// prevent more than one copy of an IP address from being put in the list.
			if (currentSelection == -1)
			{
				UnicodeString oldEntry = uni;
				UnicodeString oldIP;
				oldEntry.nextToken(&oldIP, UnicodeString(u":"));

				WideCharScan(oldIP.str(), u"%d.%d.%d.%d", &(n2[0]), &(n2[1]), &(n2[2]), &(n2[3]));

				Bool isEqual = TRUE;
				for (Int i = 0; (i < 4) && (isEqual == TRUE); ++i) {
					if (n1[i] != n2[i]) {
						isEqual = FALSE;
					}
				}
				// check to see if this is a duplicate or if this is not a properly formatted IP address.
				if (isEqual == TRUE)
				{
					--numEntries;
					continue;
				}
			}
			AsciiString temp;
			temp.format("RemoteIP%d", currentINIEntry);
			++currentINIEntry;
			prefs[temp.str()] = ascii;
		}
	}

	if (currentSelection == -1)
	{
		++numEntries;
	}

	AsciiString numRemoteIPs;
	numRemoteIPs.format("%d", numEntries);

	prefs["NumRemoteIPs"] = numRemoteIPs;

	prefs.write();
}

void HostDirectConnectGame()
{
	// Init LAN API Singleton
	DEBUG_ASSERTCRASH(TheLAN != NULL, ("TheLAN is NULL!"));
	if (!TheLAN)
	{
		TheLAN = NEW LANAPI();
	}

	UnsignedInt localIP = TheLAN->GetLocalIP();
	UnicodeString localIPString;
	localIPString.format(u"%d.%d.%d.%d", localIP >> 24, (localIP & 0xff0000) >> 16, (localIP & 0xff00) >> 8, localIP & 0xff);

	UnicodeString name;
	name = GadgetTextEntryGetText(editPlayerName);

	LANPreferences prefs;
	prefs["UserName"] = UnicodeStringToQuotedPrintable(name);
	prefs.write();

	while (name.getLength() > g_lanPlayerNameLength)
		name.removeLastChar();
	TheLAN->RequestSetName(name);
	TheHostGameMode = HOST_MODE_CLASSIC;	// Direct Connect asks no mode; nothing an earlier host picked carries over
	TheLAN->RequestGameCreate(localIPString, TRUE);
}

void JoinDirectConnectGame()
{
	// Init LAN API Singleton

	if (!TheLAN)
	{
		TheLAN = NEW LANAPI();
	}

	UnsignedInt ipaddress = 0;
	UnicodeString ipunistring = GadgetComboBoxGetText(comboboxRemoteIP);
	AsciiString asciientry;
	asciientry.translate(ipunistring);

	AsciiString ipstring;
	asciientry.nextToken(&ipstring, "(");

	char ipstr[16];
	strcpy(ipstr, ipstring.str());

	Int ip1, ip2, ip3, ip4;
	sscanf(ipstr, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);

	DEBUG_LOG(("JoinDirectConnectGame - joining at %d.%d.%d.%d\n", ip1, ip2, ip3, ip4));

	ipaddress = (ip1 << 24) + (ip2 << 16) + (ip3 << 8) + ip4;
//	ipaddress = htonl(ipaddress);

	UnicodeString name;
	name = GadgetTextEntryGetText(editPlayerName);

	LANPreferences prefs;
	prefs["UserName"] = UnicodeStringToQuotedPrintable(name);
	prefs.write();

	UpdateRemoteIPList();
	PopulateRemoteIPComboBox();

	while (name.getLength() > g_lanPlayerNameLength)
		name.removeLastChar();
	TheLAN->RequestSetName(name);

	TheLAN->RequestGameJoinDirectConnect(ipaddress);
}

/* Direct Connect as a box of the message box's pattern over the backdrop (README-game-lists.md
	 "lists-direct-connect"), on EA's screen: the box on the board at 580,295 to 1340,785, YOUR NAME, YOUR ADDRESS
	 with its hint and THEIR ADDRESS down its left, the fields at 852 to 1308, BACK, HOST and JOIN at its foot,
	 JOIN lit.  EA's screen stays a screen; its parents are fitted to the box so every gadget inside takes clicks. */
enum { DC_LEFT = 580, DC_TOP = 295, DC_RIGHT = 1340, DC_BOTTOM = 785, DC_FIELD = 852, DC_KEY_TOP = 717 };

static void drawDirectConnect( GameWindow *window, WinInstanceData * )
{
	(void)window;
	const IRegion2D box = UIStyle::boardRect( DC_LEFT, DC_TOP, DC_RIGHT, DC_BOTTOM );
	LobbyDrawBoxHead( box, TheGameText->fetch( "GUI:DirectConnect" ), TheGameText->fetch( "GUI:DirectConnectQuestion" ),
		UnicodeString::TheEmptyString );
	const Int x = UIStyle::boardX( 612.0f );
	LobbyDrawBoxLabel( TheGameText->fetch( "GUI:YourName" ), x, UIStyle::boardY( 425.0f ) );
	LobbyDrawBoxLabel( TheGameText->fetch( "GUI:YourAddress" ), x, UIStyle::boardY( 497.0f ) );
	LobbyDrawBoxLabel( TheGameText->fetch( "GUI:TheirAddress" ), x, UIStyle::boardY( 569.0f ) );
	if (staticLocalIP)
		LobbyDrawWords( LOBBY_TEXT_BOX + 3, GadgetStaticTextGetText( staticLocalIP ), UIStyle::MENU_TEXT_ROW_FIGURE, FALSE,
			UIStyle::boardX( 864.0f ), UIStyle::boardY( 497.0f ), UIStyle::INK );
	LobbyDrawWords( LOBBY_TEXT_BOX + 4, TheGameText->fetch( "GUI:AddressHint" ), UIStyle::MENU_TEXT_HINT, FALSE,
		UIStyle::boardX( 1308.0f ), UIStyle::boardY( 497.0f ), UIStyle::MUTED, TRUE );
}

/// EA's screen fitted into the box, its labels and rule hidden for the code's, its gadgets in the language
static void initDirectConnectLook( GameWindow *root )
{
	if (root)
		root->winSetDrawFunc( drawDirectConnect );
	GameWindow *gadgets = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "NetworkDirectConnect.wnd:GadgetParent" ) );
	GameWindow *inner = buttonHost ? buttonHost->winGetParent() : NULL;
	LobbyPlaceOnBoard( gadgets, DC_LEFT, DC_TOP, DC_RIGHT, DC_BOTTOM );
	if (inner && inner != gadgets)
		LobbyPlaceOnBoard( inner, DC_LEFT, DC_TOP, DC_RIGHT, DC_BOTTOM );
	LobbyPlaceOnBoard( editPlayerName, DC_FIELD, 401, 1308, 449 );
	LobbyPlaceOnBoard( comboboxRemoteIP, DC_FIELD, 545, 1308, 593 );
	LobbyPlaceOnBoard( buttonBack, 624, DC_KEY_TOP, 844, DC_KEY_TOP + 48 );
	LobbyPlaceOnBoard( buttonHost, 856, DC_KEY_TOP, 1076, DC_KEY_TOP + 48 );
	LobbyPlaceOnBoard( buttonJoin, 1088, DC_KEY_TOP, 1308, DC_KEY_TOP + 48 );
	static const char *const drawn[] = { "StaticRemoteIPDescription", "StaticLocalIPDescription", "StaticPlayerNameDescription",
		"Line", "StaticWindowLabel", "StaticLocalIP" };
	for (Int i = 0; i < (Int)ARRAY_SIZE( drawn ); ++i)
	{
		AsciiString name;
		name.format( "NetworkDirectConnect.wnd:%s", drawn[ i ] );
		if (GameWindow *window = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( name ) ))
			window->winHide( TRUE );
	}
	LobbyMenuEntry( editPlayerName, UIStyle::MENU_TEXT_ROW_FIGURE, UIStyle::GOLD );
	if (comboboxRemoteIP)
	{
		UIStyle::setMenuComboBox( comboboxRemoteIP );
		LobbyMenuEntry( GadgetComboBoxGetEditBox( comboboxRemoteIP ), UIStyle::MENU_TEXT_BODY, UIStyle::INK );
		if (GameWindow *list = GadgetComboBoxGetListBox( comboboxRemoteIP ))
		{
			UIStyle::setMenuListBox( list );
			GadgetListBoxSetFont( list, UIStyle::menuFont( UIStyle::MENU_TEXT_HINT + 1 ) );
		}
	}
	LobbyMenuKey( buttonBack, FALSE );
	LobbyMenuKey( buttonHost, FALSE );
	LobbyMenuKey( buttonJoin, TRUE );
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Welcome Menu */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectInit( WindowLayout *layout, void *userData )
{
	LANbuttonPushed = false;
	LANisShuttingDown = false;

	if (TheLAN == NULL)
	{
		TheLAN = NEW LANAPI();
		TheLAN->init();
	}
	TheLAN->reset();

	buttonPushed = false;
	isShuttingDown = false;
	TheShell->showShellMap(TRUE);
	buttonBackID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonBack" ) );
	buttonHostID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonHost" ) );
	buttonJoinID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ButtonJoin" ) );
	editPlayerNameID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:EditPlayerName" ) );
	comboboxRemoteIPID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:ComboboxRemoteIP" ) );
	staticLocalIPID = TheNameKeyGenerator->nameToKey( AsciiString( "NetworkDirectConnect.wnd:StaticLocalIP" ) );

	buttonBack = TheWindowManager->winGetWindowFromId( NULL,  buttonBackID);
	buttonHost = TheWindowManager->winGetWindowFromId( NULL,	buttonHostID);
	buttonJoin = TheWindowManager->winGetWindowFromId( NULL,	buttonJoinID);
	editPlayerName = TheWindowManager->winGetWindowFromId( NULL,	editPlayerNameID);
	comboboxRemoteIP = TheWindowManager->winGetWindowFromId( NULL,	comboboxRemoteIPID);
	staticLocalIP = TheWindowManager->winGetWindowFromId( NULL, staticLocalIPID);
	initDirectConnectLook( TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "NetworkDirectConnect.wnd:NetworkDirectConnectParent" ) ) );

//	// animate controls
//	TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_LEFT, TRUE, 800);
//	TheShell->registerWithAnimateManager(buttonHost, WIN_ANIMATION_SLIDE_LEFT, TRUE, 600);
//	TheShell->registerWithAnimateManager(buttonJoin, WIN_ANIMATION_SLIDE_LEFT, TRUE, 200);
//
	LANPreferences userprefs;
	UnicodeString name;
	name = userprefs.getUserName();

	if (name.getLength() == 0)
	{
		name = TheGameText->fetch("GUI:Player");
	}

	GadgetTextEntrySetText(editPlayerName, name);

	PopulateRemoteIPComboBox();

	UnicodeString ipstr;

	delete TheLAN;
	TheLAN = NULL;

	if (TheLAN == NULL) {
//		DEBUG_ASSERTCRASH(TheLAN != NULL, ("TheLAN is null initializing the direct connect screen."));
		TheLAN = NEW LANAPI();

		OptionPreferences prefs;
		UnsignedInt IP = prefs.getOnlineIPAddress();
		
		IPEnumeration IPs;

//		if (!IP)
//		{
			EnumeratedIP *IPlist = IPs.getAddresses();
			DEBUG_ASSERTCRASH(IPlist, ("No IP addresses found!"));
			if (!IPlist)
			{
				/// @todo: display error and exit lan lobby if no IPs are found
			}

			Bool foundIP = FALSE;
			EnumeratedIP *tempIP = IPlist;
			while ((tempIP != NULL) && (foundIP == FALSE)) {
				if (IP == tempIP->getIP()) {
					foundIP = TRUE;
				}
				tempIP = tempIP->getNext();
			}

			if (foundIP == FALSE) {
				// The IP that we had no longer exists, we need to pick a new one.
				IP = IPlist->getIP();
			}

//			IP = IPlist->getIP();
//		}
		TheLAN->init();
		TheLAN->SetLocalIP(IP);
	}

	UnsignedInt ip = TheLAN->GetLocalIP();
	ipstr.format(u"%d.%d.%d.%d", ip >> 24, (ip & 0xff0000) >> 16, (ip & 0xff00) >> 8, ip & 0xff);
	GadgetStaticTextSetText(staticLocalIP, ipstr);

	TheLAN->RequestLobbyLeave(true);
	layout->hide(FALSE);
	layout->bringForward();
	TheTransitionHandler->setGroup("NetworkDirectConnectFade");


} // NetworkDirectConnectInit

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}  // end if

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectShutdown( WindowLayout *layout, void *userData )
{
	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();

	TheTransitionHandler->reverse("NetworkDirectConnectFade");
}  // NetworkDirectConnectShutdown


//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu update method */
//-------------------------------------------------------------------------------------------------
void NetworkDirectConnectUpdate( WindowLayout * layout, void *userData)
{
	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
}// NetworkDirectConnectUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType NetworkDirectConnectInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

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
}// NetworkDirectConnectInput

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType NetworkDirectConnectSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:

		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					UnicodeString name;
					name = GadgetTextEntryGetText(editPlayerName);

					LANPreferences prefs;
					prefs["UserName"] = UnicodeStringToQuotedPrintable(name);
					prefs.write();

					while (name.getLength() > g_lanPlayerNameLength)
						name.removeLastChar();
					TheLAN->RequestSetName(name);

					buttonPushed = true;
					LANbuttonPushed = true;
					TheShell->pop();
				} //if ( controlID == buttonBack )
				else if (controlID == buttonHostID)
				{
					HostDirectConnectGame();
				}
				else if (controlID == buttonJoinID)
				{
					JoinDirectConnectGame();
				}
				break;
			}// case GBM_SELECTED:
	
		case GEM_EDIT_DONE:
			{
				break;
			}
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// NetworkDirectConnectSystem
