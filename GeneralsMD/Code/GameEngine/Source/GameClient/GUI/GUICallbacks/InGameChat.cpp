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

// FILE: InGameChat.cpp ///////////////////////////////////////////////////////////////////////
// Author: Matthew D. Campbell - June 2002
// Desc: GUI callbacks for the in-game chat entry
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"
#include "Common/MessageStream.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/DisconnectMenu.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LanguageFilter.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

static GameWindow *chatWindow = NULL;
static GameWindow *chatTextEntry = NULL;
static GameWindow *chatTypeStaticText = NULL;
static UnicodeString s_savedChat;
static InGameChatType inGameChatType;
static const UnsignedInt SAME_PRESS_MS = 150;	///< ToggleInGameChat: two calls this close are one Enter

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShowInGameChat( Bool immediate )
{
	if (TheGameLogic->isInReplayGame())
		return;

	if (TheInGameUI->isQuitMenuVisible())
		return;

	if (TheDisconnectMenu && TheDisconnectMenu->isScreenVisible())
		return;

	if (chatWindow)
	{
		chatWindow->winHide(FALSE);
		chatWindow->winEnable(TRUE);
		chatTextEntry->winHide(FALSE);
		chatTextEntry->winEnable(TRUE);
		GadgetTextEntrySetText( chatTextEntry, s_savedChat );
		s_savedChat.clear();
	}
	else
	{
		chatWindow = TheWindowManager->winCreateFromScript( AsciiString("InGameChat.wnd") );

		static NameKeyType textEntryChatID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:TextEntryChat" );
		chatTextEntry = TheWindowManager->winGetWindowFromId( NULL, textEntryChatID );
		GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );

		static NameKeyType chatTypeStaticTextID = TheNameKeyGenerator->nameToKey( "InGameChat.wnd:StaticTextChatType" );
		chatTypeStaticText = TheWindowManager->winGetWindowFromId( NULL, chatTypeStaticTextID );
	}
	TheWindowManager->winSetFocus( chatTextEntry );
	SetInGameChatType( INGAME_CHAT_EVERYONE );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ResetInGameChat( void )
{
	if(chatWindow)
		TheWindowManager->winDestroy( chatWindow );
	chatWindow = NULL;
	chatTextEntry = NULL;
	chatTypeStaticText = NULL;
	s_savedChat.clear();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void HideInGameChat( Bool immediate )
{
	if (chatWindow)
	{
		s_savedChat = GadgetTextEntryGetText( chatTextEntry );
		chatWindow->winHide(TRUE);
		chatWindow->winEnable(FALSE);
		chatTextEntry->winHide(TRUE);
		chatTextEntry->winEnable(FALSE);
		TheWindowManager->winSetFocus( NULL );
	}
	TheWindowManager->winSetFocus( NULL );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void SetInGameChatType( InGameChatType chatType )
{
	inGameChatType = chatType;
	if (chatTypeStaticText)
	{
		switch (inGameChatType)
		{
		case INGAME_CHAT_EVERYONE:
			if (ThePlayerList->getLocalPlayer()->isPlayerActive())
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Everyone") );
			else
				GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Observers") );
			break;
		case INGAME_CHAT_ALLIES:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Allies") );
			break;
		case INGAME_CHAT_PLAYERS:
			GadgetStaticTextSetText( chatTypeStaticText, TheGameText->fetch("Chat:Players") );
			break;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** While the chat is open: the line being typed and whom it goes to, and the chat's windows moved
	* over the page's typing bar, `x` `y` `width` `height` on screen, so the invisible layout takes no
	* clicks anywhere else.  FALSE while it is shut. */
// ------------------------------------------------------------------------------------------------
Bool GetInGameChatEntry( UnicodeString &typed, UnicodeString &audience, Int x, Int y, Int width, Int height )
{
	if( !IsInGameChatActive() )
		return FALSE;

	typed = GadgetTextEntryGetText( chatTextEntry );
	audience = GadgetStaticTextGetText( chatTypeStaticText );
	chatWindow->winSetPosition( x, y );
	chatWindow->winSetSize( width, height );
	for( GameWindow *child = chatWindow->winGetChild(); child; child = child->winGetNext() )
	{
		child->winSetPosition( 0, 0 );
		child->winSetSize( child == chatTextEntry ? width : 0, child == chatTextEntry ? height : 0 );
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool IsInGameChatActive() {
	if (chatWindow != NULL) {
		if (chatWindow->winIsHidden() == FALSE) {
			return TRUE;
		}
	}
	return FALSE;
}

// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus(void);
}
extern int isThreadHosting;

Bool handleInGameSlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		UnicodeString s;
		s.format(u"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting);
		TheInGameUI->message(s);
		return TRUE; // was a slash command
	}

	// The vote kick: "/kick name" opens one (and is a yes), "/yes" and "/no" answer the open one.
	// This only asks; GameLogic::processKickVote decides on every machine alike whether it counts.
	if (token == "kick" || token == "yes" || token == "no")
	{
		Int target = TheGameLogic->getKickVoteTarget();
		if (token == "kick")
		{
			UnicodeString name(uText.str() + 5);	// what follows "/kick"
			name.trim();
			target = -1;
			for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
			{
				Player *player = ThePlayerList->getNthPlayer(i);
				if (player != ThePlayerList->getLocalPlayer() && player->getPlayerType() == PLAYER_HUMAN
						&& player->isPlayerActive() && player->getPlayerDisplayName().compareNoCase(name) == 0)
					target = i;
			}
			if (target < 0)
			{
				TheInGameUI->message("GUI:KickVoteNoPlayer", name.str());
				return TRUE;
			}
		}
		else if (target < 0)
		{
			TheInGameUI->message("GUI:KickVoteNone");
			return TRUE;
		}

		GameMessage *vote = TheMessageStream->appendMessage(GameMessage::MSG_VOTE_KICK);
		vote->appendIntegerArgument(target);
		vote->appendBooleanArgument(token != "no");
		return TRUE;
	}

	return FALSE; // not a slash command
}

// ------------------------------------------------------------------------------------------------
/** help / attack / money, or yardim / saldir / para, as the whole line: a request to the computer
	* allies (MSG_TEAM_REQUEST) at where the camera looks.  The line still goes out as chat. */
// ------------------------------------------------------------------------------------------------
static void sendTeamRequest( const UnicodeString &line )
{
	static const struct { const WideChar *word; Int kind; } COMMANDS[] = {
		{ u"help", AIREQ_HELP }, { u"yardim", AIREQ_HELP }, { u"yard\u0131m", AIREQ_HELP },
		{ u"attack", AIREQ_ATTACK }, { u"saldir", AIREQ_ATTACK }, { u"sald\u0131r", AIREQ_ATTACK },
		{ u"money", AIREQ_MONEY }, { u"para", AIREQ_MONEY } };
	for( Int c = 0; c < (Int)( sizeof( COMMANDS ) / sizeof( COMMANDS[ 0 ] ) ); ++c )
	{
		if( line.compareNoCase( COMMANDS[ c ].word ) != 0 )
			continue;
		Coord3D pos;
		TheTacticalView->getPosition( &pos );
		GameMessage *request = TheMessageStream->appendMessage( GameMessage::MSG_TEAM_REQUEST );
		request->appendIntegerArgument( COMMANDS[ c ].kind );
		request->appendLocationArgument( pos );
		return;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ToggleInGameChat( Bool immediate )
{
	// One Enter that sends reaches here twice, once as the text entry's end of edit and once as the
	// chat key, in either order; the second must not open the chat straight back up.  A flag that
	// swallowed the next call ate the following Enter instead whenever the text entry took the key,
	// so opening the chat again after a line took two presses.  The second of one press comes within
	// the same few frames; a press of its own does not.
	static UnsignedInt hiddenAtMs = 0;
	if (Clock_Milliseconds() - hiddenAtMs < SAME_PRESS_MS)
		return;

	if (TheGameLogic->isInReplayGame())
		return;

	if (!TheGameInfo->isMultiPlayer() && TheGlobalData->m_netMinPlayers)
		return;

	if (chatWindow)
	{
		Bool show = chatWindow->winIsHidden();
		if (show)
			ShowInGameChat( immediate );
		else
		{
			if (chatTextEntry)
			{
				// Send what is there, clear it out, and hide the window
				UnicodeString msg = GadgetTextEntryGetText( chatTextEntry );
				msg.trim();
				if (!msg.isEmpty())
					sendTeamRequest(msg);
				if (!msg.isEmpty() && !handleInGameSlashCommands(msg))
				{
					const Player *localPlayer = ThePlayerList->getLocalPlayer();
					AsciiString playerName;
					Int playerMask = 0;

					for (Int i=0; i<MAX_SLOTS; ++i)
					{
						playerName.format("player%d", i);
						const Player *player = ThePlayerList->findPlayerWithNameKey( TheNameKeyGenerator->nameToKey( playerName ) );
						if (player && localPlayer)
						{
							switch (inGameChatType)
							{
							case INGAME_CHAT_EVERYONE:
								if (!TheGameInfo->getConstSlot(i)->isMuted())
									playerMask |= (1<<i);
								break;
							case INGAME_CHAT_ALLIES:
								if ( (player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES &&
									localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) || player==localPlayer )
									playerMask |= (1<<i);
								break;
							case INGAME_CHAT_PLAYERS:
								if ( player == localPlayer )
									playerMask |= (1<<i);
								break;
							}
						}
					}
					TheLanguageFilter->filterLine(msg);
					TheNetwork->sendChat(msg, playerMask);
				}
				GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				HideInGameChat( immediate );
				hiddenAtMs = Clock_Milliseconds();
			}
		}
	}
	else
	{
		ShowInGameChat( immediate );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatInput( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
//			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					HideInGameChat();
					return MSG_HANDLED;
					//return MSG_IGNORED;
				}  // end escape

			}  // end switch( key )

			return MSG_HANDLED;

		}  // end char

	}

	return MSG_IGNORED;

}  // end InGameChatInput

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType InGameChatSystem( GameWindow *window, UnsignedInt msg, 
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{
		//---------------------------------------------------------------------------------------------
		case GGM_FOCUS_CHANGE:
		{
//			Bool focus = (Bool) mData1;
			//if (focus)
				//TheWindowManager->winSetGrabWindow( chatTextEntry );
			break;
		} // end focus change

		//---------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{	
			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}//case GWM_INPUT_FOCUS:

		//---------------------------------------------------------------------------------------------
		case GEM_EDIT_DONE:
		{
			ToggleInGameChat();
			//HideInGameChat();

			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			static NameKeyType buttonClearID = TheNameKeyGenerator->nameToKey( AsciiString( "InGameChat.wnd:ButtonClear" ) );
			if (control && control->winGetWindowId() == buttonClearID)
			{
				if (chatTextEntry)
					GadgetTextEntrySetText( chatTextEntry, UnicodeString::TheEmptyString );
				s_savedChat.clear();
			}
			break;

		}  // end button selected

		//---------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;

	}  // end switch( msg )

	return MSG_HANDLED;

}  // end InGameChatSystem

