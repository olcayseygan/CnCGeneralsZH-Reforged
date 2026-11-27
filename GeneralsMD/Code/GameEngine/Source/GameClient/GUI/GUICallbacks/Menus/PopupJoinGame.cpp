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

// FILE: PopupJoinGame.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Jul 2002
//
//	Filename: 	PopupJoinGame.cpp
//
//	author:		Matthew D. Campbell
//	
//	purpose:	Contains the Callbacks for the Join Game Popup
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameClient/UIStyle.h"


//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

static NameKeyType parentPopupID = NAMEKEY_INVALID;
static NameKeyType textEntryGamePasswordID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;
static NameKeyType buttonJoinID = NAMEKEY_INVALID;

static GameWindow *parentPopup = NULL;
static GameWindow *textEntryGamePassword = NULL;
static UnicodeString s_gameName;

static void joinGame( AsciiString password );

/// the field's words as a password: joins when there are any
static void joinWithEntry( void )
{
	UnicodeString txtInput;
	txtInput.set( GadgetTextEntryGetText( textEntryGamePassword ) );
	GadgetTextEntrySetText( textEntryGamePassword, UnicodeString::TheEmptyString );
	txtInput.trim();
	if (!txtInput.isEmpty())
	{
		AsciiString munkee;
		munkee.translate( txtInput );
		joinGame( munkee );
	}
}

/* Join Game in the menu language (README-game-lists.md, "lists-join-password"): the box's shell with the game's
	 name after the title, the field's label and "Esc cancels" at the foot; offsets at 1080 from its top left. */
static void drawJoinBox( GameWindow *window, WinInstanceData * )
{
	IRegion2D box;
	Int width, height;
	window->winGetScreenPosition( &box.lo.x, &box.lo.y );
	window->winGetSize( &width, &height );
	box.hi.x = box.lo.x + width;
	box.hi.y = box.lo.y + height;
	LobbyDrawBoxHead( box, TheGameText->fetch( "GUI:LobbyJoinGame" ), s_gameName, TheGameText->fetch( "GUI:HintEscCancel" ) );
	LobbyDrawBoxLabel( TheGameText->fetch( "GUI:JoinPasswordLine" ), box.lo.x + UIStyle::px( 32.0f ), box.lo.y + UIStyle::px( 120.0f ) );
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** Initialize the PopupHostGameInit menu */
//-------------------------------------------------------------------------------------------------
void PopupJoinGameInit( WindowLayout *layout, void *userData )
{
	parentPopupID = TheNameKeyGenerator->nameToKey(AsciiString("PopupJoinGame.wnd:ParentJoinPopUp"));
	parentPopup = TheWindowManager->winGetWindowFromId(NULL, parentPopupID);

	textEntryGamePasswordID = TheNameKeyGenerator->nameToKey(AsciiString("PopupJoinGame.wnd:TextEntryGamePassword"));
	textEntryGamePassword = TheWindowManager->winGetWindowFromId(parentPopup, textEntryGamePasswordID);
	GadgetTextEntrySetText(textEntryGamePassword, UnicodeString::TheEmptyString);

	NameKeyType staticTextGameNameID = TheNameKeyGenerator->nameToKey(AsciiString("PopupJoinGame.wnd:StaticTextGameName"));
	GameWindow *staticTextGameName = TheWindowManager->winGetWindowFromId(parentPopup, staticTextGameNameID);
	GadgetStaticTextSetText(staticTextGameName, UnicodeString::TheEmptyString);

	buttonCancelID = NAMEKEY("PopupJoinGame.wnd:ButtonCancel");
	buttonJoinID = NAMEKEY("PopupJoinGame.wnd:ButtonJoin");

	s_gameName.clear();
	GameSpyStagingRoom *ourRoom = TheGameSpyInfo->findStagingRoomByID(TheGameSpyInfo->getCurrentStagingRoomID());
	if (ourRoom)
	{
		GadgetStaticTextSetText(staticTextGameName, ourRoom->getGameName());
		s_gameName = ourRoom->getGameName();
	}

	// the menu language: the box drawn by code, the field and the keys on the master's windows, JOIN Enter's key
	if (parentPopup)
		parentPopup->winSetDrawFunc( drawJoinBox );
	LobbyMenuEntry( textEntryGamePassword, UIStyle::MENU_TEXT_BODY, UIStyle::INK );
	LobbyMenuKey( TheWindowManager->winGetWindowFromId( parentPopup, buttonCancelID ), FALSE );
	LobbyMenuKey( TheWindowManager->winGetWindowFromId( parentPopup, buttonJoinID ), TRUE );

	TheWindowManager->winSetFocus( textEntryGamePassword ? textEntryGamePassword : parentPopup );
	TheWindowManager->winSetModal( parentPopup );

}

//-------------------------------------------------------------------------------------------------
/** PopupHostGameInput callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupJoinGameInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
//			if (buttonPushed)
//				break;

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
						GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
						SetLobbyAttemptHostJoin( FALSE );
						parentPopup = NULL;
					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}

//-------------------------------------------------------------------------------------------------
/** PopupHostGameSystem callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType PopupJoinGameSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
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

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			if (controlID == buttonCancelID)
			{
				GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
				SetLobbyAttemptHostJoin( FALSE );
				parentPopup = NULL;
			}
			else if (controlID == buttonJoinID && parentPopup)
				joinWithEntry();
			break;
		}

    //----------------------------------------------------------------------------------------------
    case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;

		}  // end input
    //---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
     
      if( controlID == textEntryGamePasswordID && parentPopup )
				joinWithEntry();
			break;
		}
		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

static void joinGame( AsciiString password )
{
	GameSpyStagingRoom *ourRoom = TheGameSpyInfo->findStagingRoomByID(TheGameSpyInfo->getCurrentStagingRoomID());
	if (!ourRoom)
	{
		GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
		SetLobbyAttemptHostJoin( FALSE );
		parentPopup = NULL;
		return;
	}
	PeerRequest req;
	req.peerRequestType = PeerRequest::PEERREQUEST_JOINSTAGINGROOM;
	req.text = ourRoom->getGameName().str();
	req.stagingRoom.id = ourRoom->getID();
	req.password = password.str();
	TheGameSpyPeerMessageQueue->addRequest(req);
	DEBUG_LOG(("Attempting to join game %d(%s) with password [%s]\n", ourRoom->getID(), WideCharAsUtf8( ourRoom->getGameName().str() ).str(), password.str()));
	GameSpyCloseOverlay(GSOVERLAY_GAMEPASSWORD);
	parentPopup = NULL;
}
