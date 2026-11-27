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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: WOLBuddyOverlay.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include "Common/AudioEventRTS.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "GameClient/GameText.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/Display.h"
#include "GameClient/MenuBoxParts.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/GameFont.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// window ids ------------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonHideID = NAMEKEY_INVALID;
static NameKeyType buttonAddBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteBuddyID = NAMEKEY_INVALID;
static NameKeyType textEntryID = NAMEKEY_INVALID;
static NameKeyType listboxBuddyID = NAMEKEY_INVALID;
static NameKeyType listboxChatID = NAMEKEY_INVALID;
static NameKeyType buttonAcceptBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDenyBuddyID = NAMEKEY_INVALID;
static NameKeyType radioButtonBuddiesID = NAMEKEY_INVALID;
static NameKeyType radioButtonIgnoreID = NAMEKEY_INVALID;
static NameKeyType parentBuddiesID = NAMEKEY_INVALID;
static NameKeyType parentIgnoreID = NAMEKEY_INVALID;
static NameKeyType listboxIgnoreID = NAMEKEY_INVALID;
static NameKeyType buttonNotificationID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parent = NULL;
static GameWindow *buttonHide = NULL;
static GameWindow *buttonAddBuddy = NULL;
static GameWindow *buttonDeleteBuddy = NULL;
static GameWindow *textEntry = NULL;
static GameWindow *listboxBuddy = NULL;
static GameWindow *listboxChat = NULL;
static GameWindow *buttonAcceptBuddy = NULL;
static GameWindow *buttonDenyBuddy = NULL;
static GameWindow *radioButtonBuddies = NULL;
static GameWindow *radioButtonIgnore = NULL;
static GameWindow *parentBuddies = NULL;
static GameWindow *parentIgnore = NULL;
static GameWindow *listboxIgnore = NULL;

static Bool isOverlayActive = false;
static void insertChat( BuddyMessage msg );
// -online's friends (package F4), further down
static Bool onlineFriends( void );
static const OnlineFriend *friendRow( Int id );
static void addInviteEntry( GameWindow *menu );

/// Team vs AI: a party invite can go to this friend (the service takes them, he is not in our party, it is not full)
static Bool partyHasSeatFor( Int id )
{
	if( !OnlineTeamAIAvailable() )
		return FALSE;
	std::vector<OnlinePartyMember> party;
	OnlineGetParty( party );
	if( (Int)party.size() >= ONLINE_TEAM_SEATS )
		return FALSE;
	for( size_t each = 0; each < party.size(); each++ )
	{
		if( party[ each ].id == id )
			return FALSE;
	}
	return TRUE;
}
// RightClick pointers ---------------------------------------------------------------------
static GameWindow *rcMenu = NULL;
static WindowLayout *noticeLayout = NULL;
static UnsignedInt noticeExpires = 0;
enum { NOTIFICATION_EXPIRES = 3000 };

void setUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id);
void refreshIgnoreList( void );
void showNotificationBox( AsciiString nick, UnicodeString message);
void deleteNotificationBox( void );
static Bool lastNotificationWasStatus = FALSE;
static Int numOnlineInNotification = 0;

class BuddyControls
{
public:
	BuddyControls(void );
	GameWindow *listboxChat;
	NameKeyType listboxChatID;

	GameWindow *listboxBuddies;
	NameKeyType listboxBuddiesID;

	GameWindow *textEntryEdit;
	NameKeyType textEntryEditID;
	Bool isInit;
};

static BuddyControls buddyControls;
BuddyControls::BuddyControls(	void )
{
	listboxChat = NULL;
	listboxChatID = NAMEKEY_INVALID;
	listboxBuddies = NULL;
	listboxBuddiesID = NAMEKEY_INVALID;
	textEntryEdit = NULL;
	textEntryEditID = NAMEKEY_INVALID;
	isInit = FALSE;
}
// At this point I don't give a damn about how good this way is.  I'm doing it anyway.
enum
{
	BUDDY_RESETALL_CRAP = -1,
	BUDDY_WINDOW_BUDDIES = 0,
	BUDDY_WINDOW_DIPLOMACY,
	BUDDY_WINDOW_WELCOME_SCREEN,
};

void InitBuddyControls(Int type)
{
	if(!TheGameSpyInfo)
	{
		buddyControls.textEntryEditID = NAMEKEY_INVALID;
		buddyControls.textEntryEdit = NULL;
		buddyControls.listboxBuddiesID = NAMEKEY_INVALID;
		buddyControls.listboxChatID = NAMEKEY_INVALID;
		buddyControls.listboxBuddies = NULL;
		buddyControls.listboxChat = NULL;
		buddyControls.isInit = FALSE;
		return;
	}
	switch (type) {
	case BUDDY_RESETALL_CRAP:
		buddyControls.textEntryEditID = NAMEKEY_INVALID;
		buddyControls.textEntryEdit = NULL;
		buddyControls.listboxBuddiesID = NAMEKEY_INVALID;
		buddyControls.listboxChatID = NAMEKEY_INVALID;
		buddyControls.listboxBuddies = NULL;
		buddyControls.listboxChat = NULL;
		buddyControls.isInit = FALSE;
	break;
	case BUDDY_WINDOW_BUDDIES:
		buddyControls.textEntryEditID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:TextEntryChat" ) );
		buddyControls.textEntryEdit = TheWindowManager->winGetWindowFromId(NULL,  buddyControls.textEntryEditID);
		buddyControls.listboxBuddiesID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ListboxBuddies" ) );
		buddyControls.listboxChatID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ListboxBuddyChat" ) );
		buddyControls.listboxBuddies = TheWindowManager->winGetWindowFromId( NULL,  buddyControls.listboxBuddiesID );
		buddyControls.listboxChat = TheWindowManager->winGetWindowFromId( NULL,  buddyControls.listboxChatID);
		GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
		buddyControls.isInit = TRUE;
		break;
	case BUDDY_WINDOW_DIPLOMACY:
		buddyControls.textEntryEditID = TheNameKeyGenerator->nameToKey( AsciiString( "Diplomacy.wnd:TextEntryChat" ) );
		buddyControls.textEntryEdit = TheWindowManager->winGetWindowFromId(NULL,  buddyControls.textEntryEditID);
		buddyControls.listboxBuddiesID = TheNameKeyGenerator->nameToKey( AsciiString( "Diplomacy.wnd:ListboxBuddies" ) );
		buddyControls.listboxChatID = TheNameKeyGenerator->nameToKey( AsciiString( "Diplomacy.wnd:ListboxBuddyChat" ) );
		buddyControls.listboxBuddies = TheWindowManager->winGetWindowFromId( NULL,  buddyControls.listboxBuddiesID );
		buddyControls.listboxChat = TheWindowManager->winGetWindowFromId( NULL,  buddyControls.listboxChatID);
		GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
		buddyControls.isInit = TRUE;
		break;
	case BUDDY_WINDOW_WELCOME_SCREEN:
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE, ("Well, you really shouldn't have gotten here, if you really care about GUI Bugs, search for this string, you you don't care, call chris (who probably doesn't care either"));
	}
	
}

WindowMsgHandledType BuddyControlSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2)
{
	if(!TheGameSpyInfo || TheGameSpyInfo->getLocalProfileID() == 0 || !buddyControls.isInit)
	{
		return MSG_IGNORED;
	}

	switch( msg )
	{
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if( controlID == buddyControls.listboxBuddiesID ) 
				{
					RightClickStruct *rc = (RightClickStruct *)mData2;
					WindowLayout *rcLayout;
					if(rc->pos < 0)
						break;

					GPProfile profileID = (GPProfile)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, 0);
					RCItemType itemType = (RCItemType)(Int)(intptr_t)GadgetListBoxGetItemData(control, rc->pos, 1);
					UnicodeString nick = GadgetListBoxGetText(control, rc->pos);

					GadgetListBoxSetSelected(control, rc->pos);
					if (itemType == ITEM_BUDDY)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCBuddiesMenu.wnd"));
					else if (itemType == ITEM_REQUEST)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCBuddyRequestMenu.wnd"));
					else
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCNonBuddiesMenu.wnd"));
					rcMenu = rcLayout->getFirstWindow();
					rcMenu->winGetLayout()->runInit();
					rcMenu->winBringToTop();
					rcMenu->winHide(FALSE);

					// -online: a friend signed in and not in a match is invited from here, into the room we sit in,
					// or outside one into our Team vs AI party while it has a seat for him
					const OnlineFriend *row = onlineFriends() ? friendRow( (Int)profileID ) : NULL;
					if (row && itemType == ITEM_BUDDY && row->status == ONLINE_FRIEND_ACCEPTED
						&& row->presence != ONLINE_PRESENCE_OFFLINE && row->presence != ONLINE_PRESENCE_MATCH
						&& (TheGameSpyInfo->getCurrentStagingRoomID() != 0 || partyHasSeatFor( row->id )))
						addInviteEntry( rcMenu );

					ICoord2D rcSize, rcPos;
					rcMenu->winGetSize(&rcSize.x, &rcSize.y);
					rcPos.x = rc->mouseX;
					rcPos.y = rc->mouseY;
					if(rc->mouseX + rcSize.x > TheDisplay->getWidth())
						rcPos.x = TheDisplay->getWidth() - rcSize.x;
					if(rc->mouseY + rcSize.y > TheDisplay->getHeight())
						rcPos.y = TheDisplay->getHeight() - rcSize.y;
					rcMenu->winSetPosition(rcPos.x, rcPos.y);


					GameSpyRCMenuData *rcData = NEW GameSpyRCMenuData;
					rcData->m_id = profileID;
					if (row)
						rcData->m_nick = row->nick;		// the shown name stands after the dot's spaces
					else
						rcData->m_nick.translate(nick);
					rcData->m_itemType = itemType;
					setUnignoreText(rcLayout, rcData->m_nick, rcData->m_id);
					rcMenu->winSetUserData((void *)rcData);
					TheWindowManager->winSetLoneWindow(rcMenu);
				}
				else
					return MSG_IGNORED;
				break;
			}
			case GEM_EDIT_DONE:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if(controlID != buddyControls.textEntryEditID)
					return MSG_IGNORED;

				// see if someone's selected
				Int selected = -1;
				GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
				if (selected >= 0)
				{
					GPProfile selectedProfile = (GPProfile)(intptr_t)GadgetListBoxGetItemData(buddyControls.listboxBuddies, selected);
					BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator recipIt = m->find(selectedProfile);
					if (recipIt == m->end())
						break;

					DEBUG_LOG(("Trying to send a buddy message to %d.\n", selectedProfile));
					if (TheGameSpyGame && TheGameSpyGame->isInGame() && TheGameSpyGame->isGameInProgress() &&
						!ThePlayerList->getLocalPlayer()->isPlayerActive())
					{
						DEBUG_LOG(("I'm dead - gotta look for cheats.\n"));
						for (Int i=0; i<MAX_SLOTS; ++i)
						{
							DEBUG_LOG(("Slot[%d] profile is %d\n", i, TheGameSpyGame->getGameSpySlot(i)->getProfileID()));
							if (TheGameSpyGame->getGameSpySlot(i)->getProfileID() == selectedProfile)
							{
								// can't send to someone in our game if we're dead/observing.  security breach and all that.  no seances for you.
								if (buddyControls.listboxChat)
								{
									GadgetListBoxAddEntryText( buddyControls.listboxChat, TheGameText->fetch("Buddy:CantTalkToIngameBuddy"),
										GameSpyColor[GSCOLOR_DEFAULT], -1, -1 );
								}
								return MSG_HANDLED;
							}
						}
					}

					// read the user's input and clear the entry box
					UnicodeString txtInput;
					txtInput.set(GadgetTextEntryGetText( buddyControls.textEntryEdit ));
					GadgetTextEntrySetText(buddyControls.textEntryEdit, UnicodeString::TheEmptyString);
					txtInput.trim();
					if (!txtInput.isEmpty())
					{
						// Send the message
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_MESSAGE;
						WideCharNCpy(req.arg.message.text, txtInput.str(), MAX_BUDDY_CHAT_LEN);
						req.arg.message.text[MAX_BUDDY_CHAT_LEN-1] = 0;
						req.arg.message.recipient = selectedProfile;
						TheGameSpyBuddyMessageQueue->addRequest(req);

						// save message for future incarnations of the buddy window
						BuddyMessageList *messages = TheGameSpyInfo->getBuddyMessages();
						BuddyMessage message;
						message.m_timestamp = time(NULL);
						message.m_senderID = TheGameSpyInfo->getLocalProfileID();
						message.m_senderNick = TheGameSpyInfo->getLocalBaseName();
						message.m_recipientID = selectedProfile;
						message.m_recipientNick = recipIt->second.m_name;
						message.m_message = UnicodeString(req.arg.message.text);
						messages->push_back(message);

						// put message on screen
						insertChat(message);
					}
				}
				else
				{
					// nobody selected.  Prompt the user.
					if (buddyControls.listboxChat)
					{
						GadgetListBoxAddEntryText( buddyControls.listboxChat, TheGameText->fetch("Buddy:SelectBuddyToChat"),
							GameSpyColor[GSCOLOR_DEFAULT], -1, -1 );
					}
				}
				break;
			}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}


static void insertChat( BuddyMessage msg )
{
	if (buddyControls.listboxChat)
	{
		BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
		BuddyInfoMap::iterator senderIt = m->find(msg.m_senderID);
		BuddyInfoMap::iterator recipientIt = m->find(msg.m_recipientID);
		Bool localSender = (msg.m_senderID == TheGameSpyInfo->getLocalProfileID());
		UnicodeString s;
		//UnicodeString timeStr = UnicodeString(_wctime( (const time_t *)&msg.m_timestamp ));
		UnicodeString timeStr;
		if (localSender /*&& recipientIt != m->end()*/)
		{
			s.format(u"[%hs -> %hs] %s", TheGameSpyInfo->getLocalBaseName().str(), msg.m_recipientNick.str(), msg.m_message.str());
			Int index = GadgetListBoxAddEntryText( buddyControls.listboxChat, s, GameSpyColor[GSCOLOR_PLAYER_SELF], -1, -1 );
			GadgetListBoxAddEntryText( buddyControls.listboxChat, timeStr, GameSpyColor[GSCOLOR_PLAYER_SELF], index, 1);
		}
		else if (!localSender /*&& senderIt != m->end()*/)
		{
			if (!msg.m_senderID)
			{
				s = msg.m_message;
				Int index = GadgetListBoxAddEntryText( buddyControls.listboxChat, s, GameSpyColor[GSCOLOR_DEFAULT], -1, -1 );
				GadgetListBoxAddEntryText( buddyControls.listboxChat, timeStr, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
			}
			else
			{
				s.format(u"[%hs] %s", msg.m_senderNick.str(), msg.m_message.str());
				Int index = GadgetListBoxAddEntryText( buddyControls.listboxChat, s, GameSpyColor[GSCOLOR_PLAYER_BUDDY], -1, -1 );
				GadgetListBoxAddEntryText( buddyControls.listboxChat, timeStr, GameSpyColor[GSCOLOR_PLAYER_BUDDY], index, 1);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
// -online's friends in the overlay (docs/plans/MENU-REDESIGN.md, package F4; the boxes README's
// boxes-4-friends-right-click).  Against a server that says friends:1 the list is the service's rows
// (OnlineGetFriends) and not EA's buddy map: a row is the presence dot, the name and where the friend
// is in MUTED, a request to us its REQUEST chip, one of ours "Sent".  The right-click menus act on the
// service (add, answer, remove, block, and INVITE while the player sits in a room), and a name field
// on the tab row asks for a friend by nick.  Any other server keeps EA's buddy list as it was.
//-------------------------------------------------------------------------------------------------
static std::vector<OnlineFriend> s_friendRows;		///< the rows as the list last showed them
static UnsignedInt s_friendRowsSerial = 0;
static GameWinDrawFunc s_friendsListDraw = NULL;	///< the list box's own draw, under the dots
static GameWindow *s_friendNameEntry = NULL;			///< the tab row's name field
static GameWindow *s_friendAddKey = NULL;					///< ADD FRIEND beside it
static const char *const INVITE_ENTRY = "RCBuddiesMenu.wnd:ButtonInvite";
/// the name column starts this many spaces in, clear of the presence dot the list draws before it
static const WideChar *const DOT_ROOM = u"     ";

static Bool onlineFriends( void )
{
	return OnlineIsEnabled() && OnlineFriendsAvailable();
}

static const OnlineFriend *friendRow( Int id )
{
	for( size_t each = 0; each < s_friendRows.size(); each++ )
	{
		if( s_friendRows[ each ].id == id )
			return &s_friendRows[ each ];
	}
	return NULL;
}

static Bool isBlockedOnline( Int id )
{
	const OnlineFriend *row = onlineFriends() ? friendRow( id ) : NULL;
	return row != NULL && row->status == ONLINE_FRIEND_BLOCKED;
}

/// where a friend is, in MUTED after his name: empty for a request to us (its chip says it)
static UnicodeString presenceWords( const OnlineFriend &row )
{
	if( row.status == ONLINE_FRIEND_PENDING_IN || row.status == ONLINE_FRIEND_BLOCKED )
		return UnicodeString::TheEmptyString;
	if( row.status == ONLINE_FRIEND_PENDING_OUT )
		return TheGameText->fetch( "GUI:FriendRequestSent" );
	UnicodeString words;
	switch( row.presence )
	{
	case ONLINE_PRESENCE_ROOM:
		words.format( TheGameText->fetch( "GUI:InARoom" ), row.roomName.str() );
		return words;
	case ONLINE_PRESENCE_MATCH:
		return TheGameText->fetch( "GUI:InAMatch" );
	case ONLINE_PRESENCE_LOBBY:
		return TheGameText->fetch( "GUI:InTheLobby" );
	case ONLINE_PRESENCE_ONLINE:
		return TheGameText->fetch( "Buddy:Online" );
	default:
		return TheGameText->fetch( "Buddy:Offline" );
	}
}

/// a line of the game's own in the chat with friends, kept for the box's next opening
static void friendsLine( const UnicodeString &words )
{
	if( !TheGameSpyInfo )
		return;
	BuddyMessage message;
	message.m_timestamp = time( NULL );
	message.m_senderID = 0;
	message.m_recipientID = TheGameSpyInfo->getLocalProfileID();
	message.m_recipientNick = TheGameSpyInfo->getLocalBaseName();
	message.m_message = words;
	TheGameSpyInfo->getBuddyMessages()->push_back( message );
	insertChat( message );
}

/// a line naming nick from a string with one %s
static void friendsLineFor( const char *key, const AsciiString &nick )
{
	UnicodeString who, words;
	who.translate( nick );
	words.format( TheGameText->fetch( key ), who.str() );
	friendsLine( words );
}

/// the last refused friend or invite request, said once: in the chat when the box is open, else on
/// EA's notice in the corner
static void showFriendError( void )
{
	AsciiString forWhat, code;
	if( !OnlineTakeFriendError( forWhat, code ) )
		return;
	AsciiString key;
	if( code == "full" && ( forWhat == "invite" || forWhat == "invite.answer" ) )
		key = "GUI:FriendErrorRoomFull";
	else if( code == "throttled" )
		key = "GUI:FriendError_rate";
	else
		key.format( "GUI:FriendError_%s", code.str() );
	const UnicodeString words = UIStyle::hasText( key.str() ) ? TheGameText->fetch( key ) : TheGameText->fetch( "GUI:FriendError" );
	DEBUG_LOG(("ONLINE FRIEND screen refused %s: %s\n", forWhat.str(), code.str()));
	if( buddyControls.isInit && buddyControls.listboxChat )
		friendsLine( words );
	else
	{
		lastNotificationWasStatus = FALSE;
		numOnlineInNotification = 0;
		showNotificationBox( AsciiString::TheEmptyString, words );
	}
}

/// the service's rows into the list: friends, requests to us, ours; the blocked go to the ignored tab
static void fillOnlineFriends( void )
{
	GameWindow *list = buddyControls.listboxBuddies;
	s_friendRowsSerial = OnlineGetFriends( s_friendRows );
	if( list == NULL )
		return;
	Int selected = -1;
	GadgetListBoxGetSelected( list, &selected );
	const Int selectedID = selected >= 0 ? (Int)(intptr_t)GadgetListBoxGetItemData( list, selected, 0 ) : 0;
	const Int top = GadgetListBoxGetTopVisibleEntry( list );
	selected = -1;
	GadgetListBoxReset( list );
	for( size_t each = 0; each < s_friendRows.size(); each++ )
	{
		const OnlineFriend &row = s_friendRows[ each ];
		if( row.status == ONLINE_FRIEND_BLOCKED )
			continue;
		UnicodeString name( DOT_ROOM ), nick;
		nick.translate( row.nick );
		name.concat( nick );
		const Bool on = row.status == ONLINE_FRIEND_ACCEPTED && row.presence != ONLINE_PRESENCE_OFFLINE;
		const Int index = GadgetListBoxAddEntryText( list, name, on || row.status == ONLINE_FRIEND_PENDING_IN ? UIStyle::INK : UIStyle::MUTED, -1, -1 );
		GadgetListBoxAddEntryText( list, presenceWords( row ), UIStyle::MUTED, index, 1 );
		GadgetListBoxSetItemData( list, (void *)(intptr_t)row.id, index, 0 );
		GadgetListBoxSetItemData( list, (void *)(intptr_t)( row.status == ONLINE_FRIEND_PENDING_IN ? ITEM_REQUEST : ITEM_BUDDY ), index, 1 );
		if( row.id == selectedID )
			selected = index;
	}
	if( selected >= 0 )
		GadgetListBoxSetSelected( list, selected );
	GadgetListBoxSetTopVisibleEntry( list, top );
}

/** The list box's own draw, then over it what a row is in the design: the presence dot in the room the
	* names leave (ACTIVE while the friend is signed in, a grey one when not) and a request's REQUEST chip
	* where the others say where they are.  The rows stand where the gadget stands them: from 4 under its
	* top, each its height and a pixel, scrolled by displayPos. */
static void friendsListDraw( GameWindow *window, WinInstanceData *instData )
{
	if( s_friendsListDraw )
		s_friendsListDraw( window, instData );
	if( !onlineFriends() )
		return;
	ListboxData *list = (ListboxData *)window->winGetUserData();
	if( list == NULL || list->listData == NULL || list->columns < 2 || list->columnWidth == NULL )
		return;
	static MenuBox::TextPool pool;
	pool.begin();
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Int dot = max( 4, UIStyle::px( 5.0f ) );
	Int rowTop = y + 4 - list->displayPos;
	for( Int row = 0; row < list->endPos; row++ )
	{
		const Int rowHeight = list->listData[ row ].height + 1;
		const Int top = rowTop;
		rowTop += rowHeight;
		if( top + rowHeight <= y + 4 )
			continue;
		if( top >= y + height )
			break;
		const OnlineFriend *f = friendRow( (Int)(intptr_t)GadgetListBoxGetItemData( window, row, 0 ) );
		if( f == NULL )
			continue;
		const Int middle = top + rowHeight / 2;
		if( middle - dot < y || middle + dot > y + height )
			continue;		// half out of the box: the text is clipped there, the marks would not be
		const Bool on = f->status == ONLINE_FRIEND_ACCEPTED && f->presence != ONLINE_PRESENCE_OFFLINE;
		const Color dotColor = on ? UIStyle::ACTIVE : GameMakeColor( 70, 74, 90, 255 );
		if( f->status == ONLINE_FRIEND_ACCEPTED )
			MenuBox::lamp( x + UIStyle::px( 16.0f ) + dot, middle, dot, dotColor );
		if( f->status == ONLINE_FRIEND_PENDING_IN )
			UIStyle::onlineChip( pool.get( UIStyle::capitals( TheGameText->fetch( "GUI:FriendRequest" ) ) ),
				x + list->columnWidth[ 0 ] + UIStyle::px( 4.0f ), middle, TRUE );
	}
}

void updateBuddyInfo( void )
{
	if (!TheGameSpyBuddyMessageQueue->isConnected())
	{
		GadgetListBoxReset(buddyControls.listboxBuddies);
		return;
	}

	if (!buddyControls.isInit)
		return;

	if (onlineFriends())
	{
		fillOnlineFriends();
		return;
	}

	int selected;
	GPProfile selectedProfile = 0;
	int visiblePos = GadgetListBoxGetTopVisibleEntry(buddyControls.listboxBuddies);

	GadgetListBoxGetSelected(buddyControls.listboxBuddies, &selected);
	if (selected >= 0)
		selectedProfile = (GPProfile)(intptr_t)GadgetListBoxGetItemData(buddyControls.listboxBuddies, selected);

	selected = -1;
	GadgetListBoxReset(buddyControls.listboxBuddies);

	// Add buddies
	BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
	BuddyInfoMap::iterator bIt;
	for (bIt = buddies->begin(); bIt != buddies->end(); ++bIt)
	{
		BuddyInfo info = bIt->second;
		GPProfile profileID = bIt->first;

		// insert name into box
		UnicodeString formatStr;
		formatStr.translate(info.m_name.str());//, info.m_status, info.m_statusString.str(), info.m_locationString.str());
		Color nameColor = (TheGameSpyInfo->isSavedIgnored(profileID)) ?
			GameSpyColor[GSCOLOR_PLAYER_IGNORED] : GameSpyColor[GSCOLOR_PLAYER_BUDDY];
		int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, nameColor, -1, -1);

		// insert status into box
		AsciiString marker;
		marker.format("Buddy:%s", WideCharAsUtf8( info.m_statusString.str() ).str());
		if (!info.m_statusString.compareNoCase(u"Offline") ||
			!info.m_statusString.compareNoCase(u"Online") ||
			!info.m_statusString.compareNoCase(u"Matching"))
		{
			formatStr = TheGameText->fetch(marker);
		}
		else if (!info.m_statusString.compareNoCase(u"Staging") ||
			!info.m_statusString.compareNoCase(u"Loading") ||
			!info.m_statusString.compareNoCase(u"Playing"))
		{
			formatStr.format(TheGameText->fetch(marker), info.m_locationString.str());
		}
		else if (!info.m_statusString.compareNoCase(u"Chatting"))
		{
			UnicodeString roomName;
			GroupRoomMap::iterator gIt = TheGameSpyInfo->getGroupRoomList()->find( atoi( WideCharAsUtf8( info.m_locationString.str() ).str() ) );	// was _wtoi, MSVC-only; the same number for the ASCII digits a room id holds
			if (gIt != TheGameSpyInfo->getGroupRoomList()->end())
			{
				AsciiString s;
				s.format("GUI:%s", gIt->second.m_name.str());
				roomName = TheGameText->fetch(s);
			}
			formatStr.format(TheGameText->fetch(marker), roomName.str());
		}
		else
		{
			formatStr = info.m_statusString;
		}
		GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
		GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void *)(intptr_t)(profileID), index, 0 );
		GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void *)(ITEM_BUDDY), index, 1 );

		if (profileID == selectedProfile)
			selected = index;
	}

	// add requests
	buddies = TheGameSpyInfo->getBuddyRequestMap();
	for (bIt = buddies->begin(); bIt != buddies->end(); ++bIt)
	{
		BuddyInfo info = bIt->second;
		GPProfile profileID = bIt->first;

		// insert name into box
		UnicodeString formatStr;
		formatStr.translate(info.m_name.str());
		int index = GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void *)(intptr_t)(profileID), index, 0 );

		// insert status into box
		formatStr = TheGameText->fetch("GUI:BuddyAddReq");
		GadgetListBoxAddEntryText(buddyControls.listboxBuddies, formatStr, GameSpyColor[GSCOLOR_DEFAULT], index, 1);
		GadgetListBoxSetItemData(buddyControls.listboxBuddies, (void *)(ITEM_REQUEST), index, 1 );

		if (profileID == selectedProfile)
			selected = index;
	}


	// select the same guy
	if (selected >= 0)
	{
		GadgetListBoxSetSelected(buddyControls.listboxBuddies, selected);
	}

	// view the same spot
	GadgetListBoxSetTopVisibleEntry(buddyControls.listboxBuddies, visiblePos);
}

void HandleBuddyResponses( void )
{
	if (TheGameSpyBuddyMessageQueue)
	{
		BuddyResponse resp;
		if (TheGameSpyBuddyMessageQueue->getResponse( resp ))
		{
			switch (resp.buddyResponseType)
			{
			case BuddyResponse::BUDDYRESPONSE_LOGIN:
				{
					deleteNotificationBox();
				}
				break;
			case BuddyResponse::BUDDYRESPONSE_DISCONNECT:
				{
					lastNotificationWasStatus = FALSE;
					numOnlineInNotification = 0;
					showNotificationBox(AsciiString::TheEmptyString, TheGameText->fetch("Buddy:MessageDisconnected"));
				}
				break;
			case BuddyResponse::BUDDYRESPONSE_MESSAGE:
				{
					if ( !WideCharCmp(resp.arg.message.text, u"I have authorized your request to add me to your list") )
						break;

					if (TheGameSpyInfo->isSavedIgnored(resp.profile))
					{
						//DEBUG_CRASH(("Player is ignored!\n"));
						break; // no buddy messages from ignored people
					}

					// save message for future incarnations of the buddy window
					BuddyMessageList *messages = TheGameSpyInfo->getBuddyMessages();
					BuddyMessage message;
					message.m_timestamp = resp.arg.message.date;
					message.m_senderID = resp.profile;
					message.m_recipientID = TheGameSpyInfo->getLocalProfileID();
					message.m_recipientNick = TheGameSpyInfo->getLocalBaseName();
					message.m_message = resp.arg.message.text;
					// insert status into box
					BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator senderIt = m->find(message.m_senderID);
					AsciiString nick;
					if (senderIt != m->end())
						nick = senderIt->second.m_name.str();
					else
						nick = resp.arg.message.nick;
					message.m_senderNick = nick;
					messages->push_back(message);

					DEBUG_LOG(("Inserting buddy chat from '%s'/'%s'\n", nick.str(), resp.arg.message.nick));

					// put message on screen
					insertChat(message);
					
					// play audio notification
					AudioEventRTS buddyMsgAudio("GUIMessageReceived");
					if( TheAudio )
					{
						TheAudio->addAudioEvent( &buddyMsgAudio );
					}  // end if

					UnicodeString snippet = message.m_message;
					while (snippet.getLength() > 11)
					{
						snippet.removeLastChar();
					}
					UnicodeString s;
					s.format(TheGameText->fetch("Buddy:MessageNotification"), nick.str(), snippet.str());
					lastNotificationWasStatus = FALSE;
					numOnlineInNotification = 0;
					showNotificationBox(AsciiString::TheEmptyString, s);
				}
				break;
			case BuddyResponse::BUDDYRESPONSE_REQUEST:
				{
					// save request for future incarnations of the buddy window
					BuddyInfoMap *m = TheGameSpyInfo->getBuddyRequestMap();
					BuddyInfo info;
					info.m_countryCode = resp.arg.request.countrycode;
					info.m_email = resp.arg.request.email;
					info.m_name = resp.arg.request.nick;
					info.m_id = resp.profile;
					info.m_status = (GPEnum)0;
					info.m_statusString = resp.arg.request.text;
					(*m)[resp.profile] = info;

					// TODO: put request on screen
					updateBuddyInfo();
					// insert status into box
					lastNotificationWasStatus = FALSE;
					numOnlineInNotification = 0;
					showNotificationBox(info.m_name, TheGameText->fetch("Buddy:AddNotification"));
				}
				break;
			case BuddyResponse::BUDDYRESPONSE_STATUS:
				{
					BuddyInfoMap *m = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::const_iterator bit = m->find(resp.profile);
					Bool seenPreviously = FALSE;
					GPEnum oldStatus = GP_OFFLINE;
					GPEnum newStatus = resp.arg.status.status;
					if (bit != m->end())
					{
						seenPreviously = TRUE;
						oldStatus = (*m)[resp.profile].m_status;
					}
					BuddyInfo info;
					info.m_countryCode = resp.arg.status.countrycode;
					info.m_email = resp.arg.status.email;
					info.m_name = resp.arg.status.nick;
					info.m_id = resp.profile;
					info.m_status = newStatus;
					info.m_statusString = UnicodeString(MultiByteToWideCharSingleLine(resp.arg.status.statusString).c_str());
					info.m_locationString = UnicodeString(MultiByteToWideCharSingleLine(resp.arg.status.location).c_str());
					(*m)[resp.profile] = info;

					updateBuddyInfo();
					PopulateLobbyPlayerListbox();
					RefreshGameListBoxes();
					if ( (newStatus == GP_OFFLINE && seenPreviously) ||
						(newStatus == GP_ONLINE && (oldStatus == GP_OFFLINE || !seenPreviously)) )
					//if (!info.m_statusString.compareNoCase(L"Offline") ||
					//!info.m_statusString.compareNoCase(L"Online"))
					{
						// insert status into box
						AsciiString marker;
						marker.format("Buddy:%sNotification", WideCharAsUtf8( info.m_statusString.str() ).str());

						lastNotificationWasStatus = TRUE;
						if (newStatus != GP_OFFLINE)
							++numOnlineInNotification;

						showNotificationBox(info.m_name, TheGameText->fetch(marker));
					}
					else if( newStatus == GP_RECV_GAME_INVITE && !seenPreviously)
					{
						lastNotificationWasStatus = TRUE;
						if (newStatus != GP_OFFLINE)
							++numOnlineInNotification;

						showNotificationBox(info.m_name, TheGameText->fetch("Buddy:OnlineNotification"));
					}
				}
				break;
			}
		}
	}
	else
	{
		DEBUG_CRASH(("No buddy message queue!\n"));
	}
	if(noticeLayout && Clock_Milliseconds() > noticeExpires)
	{
		deleteNotificationBox();
	}
	// -online: the FRIENDS key, the invite toast and a JOIN on its way (package F4)
	FriendsCornerUpdate();
}

void showNotificationBox( AsciiString nick, UnicodeString message)
{
//	if(!GameSpyIsOverlayOpen(GSOVERLAY_BUDDY))
//		return;
	if( !noticeLayout )
		noticeLayout = TheWindowManager->winCreateLayout( "Menus/PopupBuddyListNotification.wnd" );
	noticeLayout->hide( FALSE );
	if (buttonNotificationID == NAMEKEY_INVALID)
	{
		buttonNotificationID = TheNameKeyGenerator->nameToKey("PopupBuddyListNotification.wnd:ButtonNotification");
	}
	GameWindow *win = TheWindowManager->winGetWindowFromId(NULL,buttonNotificationID);
	if(!win)
	{
		deleteNotificationBox();
		return;
	}

	if (lastNotificationWasStatus && numOnlineInNotification > 1)
	{
		message = TheGameText->fetch("Buddy:MultipleOnlineNotification");
	}

	if (nick.isNotEmpty())
		message.format(message, nick.str());
	GadgetButtonSetText(win, message);
	//GadgetStaticTextSetText(win, message);
	noticeExpires = Clock_Milliseconds() + NOTIFICATION_EXPIRES;
	noticeLayout->bringForward();

	AudioEventRTS buttonClick("GUICommunicatorIncoming");

	if( TheAudio )
	{
		TheAudio->addAudioEvent( &buttonClick );
	}  // end if

}

void deleteNotificationBox( void )
{
	lastNotificationWasStatus = FALSE;
	numOnlineInNotification = 0;
	if(noticeLayout)
	{
		noticeLayout->destroyWindows();
		noticeLayout->deleteInstance();
		noticeLayout = NULL;
	}
}

void PopulateOldBuddyMessages(void)
{
	// show previous messages
	BuddyMessageList *messages = TheGameSpyInfo->getBuddyMessages();
	for (BuddyMessageList::iterator mIt = messages->begin(); mIt != messages->end(); ++mIt)
	{
		BuddyMessage message = *mIt;
		insertChat(message);
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Buddy Overlay */
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// The friends box in the menu language (docs/plans/MENU-REDESIGN.md, package 5; the boxes README's
// boxes-4-friends-right-click): a 1040 list box over the menu, FRIENDS, two tabs, the list at the
// left, the chat with the picked friend at the right, the chat line and CLOSE in the foot, no key
// lit.  Tools/boxes_layout.py places the windows; this draws the box, the tabs and the key.  The
// rows are still the list box EA fills from the buddy list: what a row shows (presence, where a
// friend is, a request chip) is the friends package's to draw, from its own data.
//-------------------------------------------------------------------------------------------------
static DisplayString *friendsTitle = NULL;
static DisplayString *friendsOnline = NULL;		///< "3 online" after the title, -online with friends

static void friendsBoxDraw( GameWindow *window, WinInstanceData * )
{
	const IRegion2D r = MenuBox::windowRect( window );
	UIStyle::boxShell( r, friendsTitle, FALSE );
	UIStyle::boxFootRule( r );
	if( onlineFriends() && friendsTitle && friendsOnline )
	{
		UnicodeString words;
		words.format( TheGameText->fetch( "GUI:FriendsOnline" ), OnlineCountFriendsOnline() );
		if( words.compare( friendsOnline->getText() ) != 0 )
			friendsOnline->setText( words );
		Int titleWidth, titleHeight;
		friendsTitle->getSize( &titleWidth, &titleHeight );
		// the title stands BOX_PAD in, its row the head's upper part over the lit rule
		MenuBox::words( friendsOnline, UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ),
			r.lo.x + UIStyle::px( (Real)UIStyle::BOX_PAD + 20.0f ) + titleWidth, r.lo.y + UIStyle::px( 40.0f ), UIStyle::MUTED );
	}
}

static void friendsTabDraw( GameWindow *window, WinInstanceData *instData )
{
	// -online's FRIENDS tab counts its rows, the ignored tab its names
	Int count = 0;
	if( onlineFriends() )
	{
		if( window == radioButtonBuddies && buddyControls.listboxBuddies )
			count = GadgetListBoxGetNumEntries( buddyControls.listboxBuddies );
		else if( window == radioButtonIgnore && listboxIgnore )
			count = GadgetListBoxGetNumEntries( listboxIgnore );
	}
	UIStyle::menuTab( MenuBox::windowRect( window ), instData->getTextDisplayString(),
		BitTest( instData->getState(), WIN_STATE_SELECTED ), BitTest( instData->getState(), WIN_STATE_HILITED ), count );
}

/// a window stood on the screen at r, whatever its parent
static void placeOnScreen( GameWindow *window, const IRegion2D &r )
{
	if( window == NULL )
		return;
	Int px = 0, py = 0;
	if( window->winGetParent() )
		window->winGetParent()->winGetScreenPosition( &px, &py );
	window->winSetPosition( r.lo.x - px, r.lo.y - py );
	window->winSetSize( max( 1, r.hi.x - r.lo.x ), max( 1, r.hi.y - r.lo.y ) );
}

/** A push button made in code with model's look (its draw data, font and text colours), named so a
	* script finds it in the window tree (ui_walk.py, the -control socket's uitree); r is on the screen. */
static GameWindow *makeFriendsButton( GameWindow *parent, GameWindow *model, const IRegion2D &r, const char *name, const char *label )
{
	if( parent == NULL || model == NULL )
		return NULL;
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	WinInstanceData *look = model->winGetInstanceData();
	for( Int i = 0; i < MAX_DRAW_DATA; ++i )
	{
		inst.m_enabledDrawData[ i ] = look->m_enabledDrawData[ i ];
		inst.m_disabledDrawData[ i ] = look->m_disabledDrawData[ i ];
		inst.m_hiliteDrawData[ i ] = look->m_hiliteDrawData[ i ];
	}
	inst.m_enabledText = look->m_enabledText;
	inst.m_disabledText = look->m_disabledText;
	inst.m_hiliteText = look->m_hiliteText;
	inst.m_decoratedNameString = name;
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( parent, WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
		0, 0, 1, 1, &inst, model->winGetFont(), FALSE );
	if( button == NULL )
		return NULL;
	placeOnScreen( button, r );
	GadgetButtonSetText( button, TheGameText->fetch( label ) );
	return button;
}

/// the name field on the tab row, with the chat line's look, a nick's length, letters the nicks take
static GameWindow *makeFriendNameEntry( GameWindow *parent, GameWindow *model, const IRegion2D &r )
{
	if( parent == NULL || model == NULL )
		return NULL;
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_ENTRY_FIELD | GWS_MOUSE_TRACK );
	WinInstanceData *look = model->winGetInstanceData();
	for( Int i = 0; i < MAX_DRAW_DATA; ++i )
	{
		inst.m_enabledDrawData[ i ] = look->m_enabledDrawData[ i ];
		inst.m_disabledDrawData[ i ] = look->m_disabledDrawData[ i ];
		inst.m_hiliteDrawData[ i ] = look->m_hiliteDrawData[ i ];
	}
	inst.m_enabledText = look->m_enabledText;
	inst.m_disabledText = look->m_disabledText;
	inst.m_hiliteText = look->m_hiliteText;
	inst.m_decoratedNameString = "WOLBuddyOverlay.wnd:TextEntryFriendName";
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	EntryData entryData;
	memset( &entryData, 0, sizeof( entryData ) );
	entryData.maxTextLen = 24;
	entryData.aSCIIOnly = TRUE;
	GameWindow *entry = TheWindowManager->gogoGadgetTextEntry( parent,
		WIN_STATUS_ENABLED | ( BitTest( model->winGetStatus(), WIN_STATUS_IMAGE ) ? WIN_STATUS_IMAGE : 0 ),
		0, 0, 1, 1, &inst, &entryData, model->winGetFont(), FALSE );
	if( entry == NULL )
		return NULL;
	placeOnScreen( entry, r );
	return entry;
}

/// the name field's nick asked for as a friend; the field empties
static void addFriendFromField( void )
{
	if( s_friendNameEntry == NULL || !onlineFriends() )
		return;
	UnicodeString typed;
	typed.set( GadgetTextEntryGetText( s_friendNameEntry ) );
	typed.trim();
	GadgetTextEntrySetText( s_friendNameEntry, UnicodeString::TheEmptyString );
	if( typed.isEmpty() )
		return;
	AsciiString nick;
	nick.translate( typed );
	if( TheGameSpyInfo && nick.compareNoCase( TheGameSpyInfo->getLocalBaseName() ) == 0 )
		return;
	OnlineAddFriend( nick );
	friendsLineFor( "GUI:FriendRequestSentTo", nick );
	DEBUG_LOG(("ONLINE FRIEND screen add %s\n", nick.str()));
}

static void dressFriendsBox( GameWindow *box )
{
	if( box == NULL )
		return;
	if( friendsTitle == NULL )
		friendsTitle = TheDisplayStringManager->newDisplayString();
	friendsTitle->setText( UIStyle::capitals( TheGameText->fetch( "GUI:Friends" ) ) );
	box->winSetDrawFunc( friendsBoxDraw );
	if( radioButtonBuddies )
		radioButtonBuddies->winSetDrawFunc( friendsTabDraw );
	if( radioButtonIgnore )
		radioButtonIgnore->winSetDrawFunc( friendsTabDraw );
	// CLOSE takes the player nowhere new: no key of this box is lit
	UIStyle::setMenuKey( buttonHide, FALSE );

	// -online with friends: the dots and chips over the list, "3 online" after the title, and on the
	// tab row's right a name field and ADD FRIEND (the chat's head the design puts there has no window)
	s_friendNameEntry = NULL;
	s_friendAddKey = NULL;
	GameWindow *list = buddyControls.listboxBuddies;
	if( list && list->winGetDrawFunc() != friendsListDraw )
	{
		s_friendsListDraw = list->winGetDrawFunc();
		list->winSetDrawFunc( friendsListDraw );
	}
	if( !onlineFriends() || radioButtonBuddies == NULL || buttonHide == NULL )
		return;
	if( friendsOnline == NULL )
		friendsOnline = TheDisplayStringManager->newDisplayString();
	// the tab row's height, ending where the chat's column ends; the key CLOSE's width, the field 246
	// at 1080 left of it, 16 apart
	const IRegion2D tabs = MenuBox::windowRect( radioButtonBuddies );
	const IRegion2D close = MenuBox::windowRect( buttonHide );
	const Int right = buddyControls.listboxChat ? MenuBox::windowRect( buddyControls.listboxChat ).hi.x : close.hi.x;
	const Int keyLeft = right - ( close.hi.x - close.lo.x );
	const Int gap = UIStyle::px( 16.0f );
	s_friendNameEntry = makeFriendNameEntry( box, buddyControls.textEntryEdit,
		MenuBox::region( keyLeft - gap - UIStyle::px( 246.0f ), tabs.lo.y, keyLeft - gap, tabs.hi.y ) );
	s_friendAddKey = makeFriendsButton( box, buttonHide, MenuBox::region( keyLeft, tabs.lo.y, right, tabs.hi.y ),
		"WOLBuddyOverlay.wnd:ButtonAddFriend", "GUI:AddFriend" );
	if( s_friendAddKey )
	{
		GadgetButtonSetText( s_friendAddKey, UIStyle::capitals( TheGameText->fetch( "GUI:AddFriend" ) ) );
		UIStyle::setMenuKey( s_friendAddKey, FALSE );
		s_friendAddKey->winEnable( FALSE );		// the field is empty; the box's update wakes it
	}
}

//-------------------------------------------------------------------------------------------------
/** The six right-click menus (RC*Menu.wnd), named in their layouts' DRAWCALLBACK: the room's open
	* list at the pointer, nearly opaque black with the lit edge and a shadow, the player's name over
	* the entries in small capitals (the game's menu, opened on a game, has no name line to fill), an
	* entry a row, the one under the pointer lifted with the rail's white bar. */
//-------------------------------------------------------------------------------------------------
void RightClickMenuDraw( GameWindow *window, WinInstanceData * )
{
	static const NameKeyType gameMenu = NAMEKEY( "RCGameDetailsMenu.wnd:RCParent" );
	static MenuBox::TextPool pool;
	const IRegion2D r = MenuBox::windowRect( window );
	const Int shadow = UIStyle::px( 6.0f );
	MenuBox::fill( MenuBox::region( r.lo.x + shadow, r.lo.y + shadow, r.hi.x + shadow, r.hi.y + shadow ), GameMakeColor( 0, 0, 0, 120 ) );
	MenuBox::fill( r, GameMakeColor( 4, 6, 12, 242 ) );
	MenuBox::outline( r, UIStyle::EDGE_LIT );
	pool.begin();
	if( (NameKeyType)window->winGetWindowId() != gameMenu && window->winGetUserData() )
	{
		const GameSpyRCMenuData *data = (const GameSpyRCMenuData *)window->winGetUserData();
		UnicodeString name;
		name.translate( data->m_nick );
		MenuBox::words( pool.get( UIStyle::nameCapitals( name ) ), UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE ),
			r.lo.x + UIStyle::px( 16.0f ), r.lo.y + UIStyle::px( 18.0f ), UIStyle::MUTED );
	}
	MenuBox::rule( r.lo.x + 1, r.hi.x - 1, r.lo.y + UIStyle::px( 33.0f ), 160 );
}

void RightClickEntryDraw( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D r = MenuBox::windowRect( window );
	const UnicodeString text = instData->getText();
	const UnicodeString capitals = UIStyle::capitals( text );
	if( text.compare( capitals ) != 0 )
		GadgetButtonSetText( window, capitals );
	if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
	{
		MenuBox::fill( r, GameMakeColor( 255, 255, 255, 22 ) );
		UIStyle::litLine( r.lo.x, r.lo.y, max( 2, UIStyle::px( 3.0f ) ), r.hi.y - r.lo.y, UIStyle::INK );
	}
	MenuBox::words( instData->getTextDisplayString(), UIStyle::menuFont( UIStyle::MENU_TEXT_HINT, TRUE ),
		r.lo.x + UIStyle::px( 15.0f ), ( r.lo.y + r.hi.y ) / 2, UIStyle::INK );
}

void WOLBuddyOverlayInit( WindowLayout *layout, void *userData )
{
	parentID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:BuddyMenuParent" ) );
	buttonHideID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ButtonHide" ) );
	buttonAddBuddyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ButtonAdd" ) );
	buttonDeleteBuddyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ButtonDelete" ) );
	//textEntryID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:TextEntryChat" ) );
	//listboxBuddyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ListboxBuddies" ) );
	//listboxChatID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ListboxBuddyChat" ) );
	buttonAcceptBuddyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ButtonYes" ) );
	buttonDenyBuddyID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ButtonNo" ) );
	radioButtonBuddiesID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:RadioButtonBuddies" ) );
	radioButtonIgnoreID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:RadioButtonIgnore" ) );
	parentBuddiesID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:BuddiesParent" ) );
	parentIgnoreID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:IgnoreParent" ) );
	listboxIgnoreID = TheNameKeyGenerator->nameToKey( AsciiString( "WOLBuddyOverlay.wnd:ListboxIgnore" ) );


	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonHide = TheWindowManager->winGetWindowFromId( parent,  buttonHideID);
	buttonAddBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonAddBuddyID);
	buttonDeleteBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonDeleteBuddyID);
	//	textEntry = TheWindowManager->winGetWindowFromId( parent,  textEntryID);
	//listboxBuddy = TheWindowManager->winGetWindowFromId( parent,  listboxBuddyID);
	//listboxChat = TheWindowManager->winGetWindowFromId( parent,  listboxChatID);
	buttonAcceptBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonAcceptBuddyID);
	buttonDenyBuddy = TheWindowManager->winGetWindowFromId( parent,  buttonDenyBuddyID);
	radioButtonBuddies = TheWindowManager->winGetWindowFromId( parent,  radioButtonBuddiesID);
	radioButtonIgnore = TheWindowManager->winGetWindowFromId( parent,  radioButtonIgnoreID);
	parentBuddies = TheWindowManager->winGetWindowFromId( parent,  parentBuddiesID);
	parentIgnore = TheWindowManager->winGetWindowFromId( parent,  parentIgnoreID);
	listboxIgnore = TheWindowManager->winGetWindowFromId( parent,  listboxIgnoreID);

	InitBuddyControls(BUDDY_WINDOW_BUDDIES);

	GadgetRadioSetSelection(radioButtonBuddies,FALSE);
	parentBuddies->winHide(FALSE);
	parentIgnore->winHide(TRUE);

	//GadgetTextEntrySetText(textEntry, UnicodeString::TheEmptyString);

	PopulateOldBuddyMessages();

	dressFriendsBox( parent );
	// BuddiesParent and IgnoreParent reach down to the foot (the chat line is a child of the first) and
	// load after CLOSE, so the shown one covered it: the IGNORED page could not be closed by its key
	if (buttonHide)
		buttonHide->winBringToTop();

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parent );

	isOverlayActive = true;
	updateBuddyInfo();
	
} // WOLBuddyOverlayInit

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLBuddyOverlayShutdown( WindowLayout *layout, void *userData )
{
	listboxIgnore = NULL;
	s_friendNameEntry = NULL;		// the box's own children, destroyed with it
	s_friendAddKey = NULL;

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	//TheShell->shutdownComplete( layout );

	isOverlayActive = false;

	InitBuddyControls(BUDDY_RESETALL_CRAP);

}  // WOLBuddyOverlayShutdown


//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay update method */
//-------------------------------------------------------------------------------------------------
void WOLBuddyOverlayUpdate( WindowLayout * layout, void *userData)
{
	if (!TheGameSpyBuddyMessageQueue || !TheGameSpyBuddyMessageQueue->isConnected())
	{
		GameSpyCloseOverlay(GSOVERLAY_BUDDY);
		return;
	}
	// -online: a refusal said in the chat, the rows again when the service's list moved
	if (onlineFriends())
	{
		showFriendError();
		// ADD FRIEND takes clicks only with a name typed beside it
		if (s_friendAddKey && s_friendNameEntry)
		{
			UnicodeString typed;
			typed.set( GadgetTextEntryGetText( s_friendNameEntry ) );
			typed.trim();
			s_friendAddKey->winEnable( !typed.isEmpty() );
		}
		std::vector<OnlineFriend> rows;
		if (OnlineGetFriends( rows ) != s_friendRowsSerial)
		{
			updateBuddyInfo();
			if (parentIgnore && !parentIgnore->winIsHidden())
				refreshIgnoreList();
		}
	}
}// WOLBuddyOverlayUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLBuddyOverlayInput( GameWindow *window, UnsignedInt msg,
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
																							(WindowMsgData)buttonHide, buttonHideID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}// WOLBuddyOverlayInput

//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLBuddyOverlaySystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	if(BuddyControlSystem(window, msg, mData1, mData2) == MSG_HANDLED)
	{
		return MSG_HANDLED;
	}
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
		case GLM_RIGHT_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if( controlID == listboxIgnoreID ) 
				{
					RightClickStruct *rc = (RightClickStruct *)mData2;
					WindowLayout *rcLayout;
					if(rc->pos < 0)
						break;

					Bool isBuddy = false, isRequest = false;
					GPProfile profileID = (GPProfile)(intptr_t)GadgetListBoxGetItemData(control, rc->pos);
					UnicodeString nick = GadgetListBoxGetText(control, rc->pos);
					BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator bIt;
					bIt = buddies->find(profileID);
					if (bIt != buddies->end())
					{
						isBuddy = true;
					}
					else
					{
						buddies = TheGameSpyInfo->getBuddyRequestMap();
						bIt = buddies->find(profileID);
						if (bIt != buddies->end())
						{
							isRequest = true;
						}
						else
						{
							// neither buddy nor request
							//break;
						}
					}

					GadgetListBoxSetSelected(control, rc->pos);
					if (isBuddy)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCBuddiesMenu.wnd"));
					else if (isRequest)
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCBuddyRequestMenu.wnd"));
					else
						rcLayout = TheWindowManager->winCreateLayout(AsciiString("Menus/RCNonBuddiesMenu.wnd"));
					rcMenu = rcLayout->getFirstWindow();
					rcMenu->winGetLayout()->runInit();
					rcMenu->winBringToTop();
					rcMenu->winHide(FALSE);
					
					

					rcMenu->winSetPosition(rc->mouseX, rc->mouseY);
					GameSpyRCMenuData *rcData = NEW GameSpyRCMenuData;
					rcData->m_id = profileID;
					rcData->m_nick.translate(nick);
					rcData->m_itemType = (isBuddy)?ITEM_BUDDY:((isRequest)?ITEM_REQUEST:ITEM_NONBUDDY);
					setUnignoreText(rcLayout, rcData->m_nick, rcData->m_id);
					rcMenu->winSetUserData((void *)rcData);
					TheWindowManager->winSetLoneWindow(rcMenu);
				}
				break;
			}
		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonHideID)
				{
					GameSpyCloseOverlay( GSOVERLAY_BUDDY );
				}
				else if (control == s_friendAddKey && s_friendAddKey != NULL)
				{
					addFriendFromField();
				}
				else if (controlID == radioButtonBuddiesID)
				{
					parentBuddies->winHide(FALSE);
					parentIgnore->winHide(TRUE);
				}
				else if (controlID == radioButtonIgnoreID)
				{
					parentBuddies->winHide(TRUE);
					parentIgnore->winHide(FALSE);
					refreshIgnoreList();
				}
				else if (controlID == buttonAddBuddyID)
				{
					/*
					UnicodeString uName = GadgetTextEntryGetText(textEntry);
					AsciiString aName;
					aName.translate(uName);
					if (!aName.isEmpty())
					{
						TheWOLBuddyList->requestBuddyAdd(aName);
					}
					GadgetTextEntrySetText(textEntry, UnicodeString::TheEmptyString);
					*/
				}
				else if (controlID == buttonDeleteBuddyID)
				{
					/*
					int selected;
					AsciiString selectedName = AsciiString::TheEmptyString;

					GadgetListBoxGetSelected(listbox, &selected);
					if (selected >= 0)
						selectedName = TheNameKeyGenerator->keyToName((NameKeyType)(int)GadgetListBoxGetItemData(listbox, selected));

					if (!selectedName.isEmpty())
					{
						TheWOLBuddyList->requestBuddyDelete(selectedName);
					}
					*/
				}
				break;
			}// case GBM_SELECTED:
		case GEM_EDIT_DONE:
			{
				// Enter in the tab row's name field asks for that friend (the chat line went to BuddyControlSystem)
				GameWindow *control = (GameWindow *)mData1;
				if (control != NULL && control == s_friendNameEntry)
					addFriendFromField();
				break;
			}
		case GLM_DOUBLE_CLICKED:
			{
				/*
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxBuddyID ) 
				{
					int rowSelected = mData2;
				
					if (rowSelected >= 0)
					{
						UnicodeString buddyName;
						GameWindow *listboxWindow = TheWindowManager->winGetWindowFromId( parent, listboxBuddyID );

							// get text of buddy name
						buddyName = GadgetListBoxGetText( listboxWindow, rowSelected,0 );
						GPProfile buddyID = (GPProfile)GadgetListBoxGetItemData( listboxWindow, rowSelected, 0 );

						Int index = -1;
						gpGetBuddyIndex(TheGPConnection, buddyID, &index);
						if (index >= 0)
						{
							GPBuddyStatus status;
							gpGetBuddyStatus(TheGPConnection, rowSelected, &status);

							UnicodeString string;
							string.format(L"To join %s in %hs:", buddyName.str(), status.locationString);
							GameSpyAddText(string, GSCOLOR_DEFAULT);

							if (status.status == GP_CHATTING)
							{
								AsciiString location = status.locationString;
								AsciiString val;
								location.nextToken(&val, "/");
								location.nextToken(&val, "/");
								location.nextToken(&val, "/");

								string.format(L"  ???");
								if (!val.isEmpty())
								{
									Int groupRoom = atoi(val.str());
									if (TheGameSpyChat->getCurrentGroupRoomID() == groupRoom)
									{
										// already there
										string.format(L"  nothing");
										GameSpyAddText(string, GSCOLOR_DEFAULT);
									}
									else
									{
										GroupRoomMap *rooms = TheGameSpyChat->getGroupRooms();
										if (rooms)
										{
											Bool needToJoin = true;
											GroupRoomMap::iterator it = rooms->find(groupRoom);
											if (it != rooms->end())
											{
												// he's in a different room
												if (TheGameSpyChat->getCurrentGroupRoomID())
												{
													string.format(L"  leave group room");
													GameSpyAddText(string, GSCOLOR_DEFAULT);

													TheGameSpyChat->leaveRoom(GroupRoom);
												}
												else if (TheGameSpyGame->isInGame())
												{
													if (TheGameSpyGame->isGameInProgress())
													{
														string.format(L"  can't leave game in progress");
														GameSpyAddText(string, GSCOLOR_DEFAULT);
														needToJoin = false;
													}
													else
													{
														string.format(L"  leave game setup");
														GameSpyAddText(string, GSCOLOR_DEFAULT);

														TheGameSpyChat->leaveRoom(StagingRoom);
														TheGameSpyGame->leaveGame();
													}
												}
												if (needToJoin)
												{
													string.format(L"  join lobby %d", groupRoom);
													TheGameSpyChat->joinGroupRoom(groupRoom);
													GameSpyAddText(string, GSCOLOR_DEFAULT);
												}
											}
										}
									}
								}
							}
						}
						else
						{
							DEBUG_CRASH(("No buddy associated with that ProfileID"));
							GameSpyUpdateBuddyOverlay();
						}
					}
				}
				*/
				break;
			}
		
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// WOLBuddyOverlaySystem

WindowMsgHandledType PopupBuddyNotificationSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
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

		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if (controlID == buttonNotificationID)
				{
					GameSpyOpenOverlay( GSOVERLAY_BUDDY );
				}
				break;
			}

		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// PopupBuddyNotificationSystem

/*
static NameKeyType buttonAcceptBuddyID = NAMEKEY_INVALID;
static NameKeyType buttonDenyBuddyID = NAMEKEY_INVALID;
*/
static NameKeyType buttonAddID = NAMEKEY_INVALID;
static NameKeyType buttonDeleteID = NAMEKEY_INVALID;
static NameKeyType buttonPlayID = NAMEKEY_INVALID;
static NameKeyType buttonIgnoreID = NAMEKEY_INVALID;
static NameKeyType buttonStatsID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
//static GameWindow *rCparent = NULL;


//-------------------------------------------------------------------------------------------------
/** WOL Buddy Overlay Right Click menu callbacks */
//-------------------------------------------------------------------------------------------------
void WOLBuddyOverlayRCMenuInit( WindowLayout *layout, void *userData )
{
	AsciiString controlName;
	controlName.format("%s:ButtonAdd",layout->getFilename().str()+6);
	buttonAddID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonDelete",layout->getFilename().str()+6);
	buttonDeleteID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonPlay",layout->getFilename().str()+6);
	buttonPlayID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonIgnore",layout->getFilename().str()+6);
	buttonIgnoreID =  TheNameKeyGenerator->nameToKey( controlName );
	controlName.format("%s:ButtonStats",layout->getFilename().str()+6);
	buttonStatsID =  TheNameKeyGenerator->nameToKey( controlName );
}
static void closeRightClickMenu(GameWindow *win)
{

	if(win)
	{
		WindowLayout *winLay = win->winGetLayout();
		if(!winLay)
			return;
		winLay->destroyWindows();					
		winLay->deleteInstance();
		winLay = NULL;

	}
}

void RequestBuddyAdd(Int profileID, AsciiString nick)
{
	// request to add a buddy
	BuddyRequest req;
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_ADDBUDDY;
	req.arg.addbuddy.id = profileID;
	UnicodeString buddyAddstr;
	buddyAddstr = TheGameText->fetch("GUI:BuddyAddReq");
	WideCharNCpy(req.arg.addbuddy.text, buddyAddstr.str(), MAX_BUDDY_CHAT_LEN);
	req.arg.addbuddy.text[MAX_BUDDY_CHAT_LEN-1] = 0;
	TheGameSpyBuddyMessageQueue->addRequest(req);

	UnicodeString s;
	Bool exists = TRUE;
	s.format(TheGameText->fetch("Buddy:InviteSent", &exists));
	if (!exists)
	{
		// no string yet.  don't display.
		return;
	}

	// save message for future incarnations of the buddy window
	BuddyMessageList *messages = TheGameSpyInfo->getBuddyMessages();
	BuddyMessage message;
	message.m_timestamp = time(NULL);
	message.m_senderID = 0;
	message.m_senderNick = "";
	message.m_recipientID = TheGameSpyInfo->getLocalProfileID();
	message.m_recipientNick = TheGameSpyInfo->getLocalBaseName();
	message.m_message.format(TheGameText->fetch("Buddy:InviteSentToPlayer"), nick.str());

	// insert status into box
	messages->push_back(message);

	DEBUG_LOG(("Inserting buddy add request\n"));

	// put message on screen
	insertChat(message);
	
	// play audio notification
	AudioEventRTS buddyMsgAudio("GUIMessageReceived");
	if( TheAudio )
	{
		TheAudio->addAudioEvent( &buddyMsgAudio );
	}  // end if

	lastNotificationWasStatus = FALSE;
	numOnlineInNotification = 0;
	showNotificationBox(AsciiString::TheEmptyString, s);
}

/** INVITE on a friend's menu (RCBuddiesMenu.wnd has no such window: it is made here, an entry on top
	* with the card's look, the others one down and the menu an entry taller). */
static void addInviteEntry( GameWindow *menu )
{
	if( menu == NULL )
		return;
	GameWindow *model = TheWindowManager->winGetWindowFromId( menu, TheNameKeyGenerator->nameToKey( "RCBuddiesMenu.wnd:ButtonStats" ) );
	if( model == NULL )
		return;
	Int modelX, modelY, modelWidth, modelHeight;
	model->winGetPosition( &modelX, &modelY );
	model->winGetSize( &modelWidth, &modelHeight );
	for( GameWindow *child = menu->winGetChild(); child; child = child->winGetNext() )
	{
		Int childX, childY;
		child->winGetPosition( &childX, &childY );
		if( childY >= modelY )
			child->winSetPosition( childX, childY + modelHeight );
	}
	Int menuWidth, menuHeight;
	menu->winGetSize( &menuWidth, &menuHeight );
	menu->winSetSize( menuWidth, menuHeight + modelHeight );

	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	inst.m_decoratedNameString = INVITE_ENTRY;
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	GameWindow *entry = TheWindowManager->gogoGadgetPushButton( menu, WIN_STATUS_ENABLED,
		modelX, modelY, modelWidth, modelHeight, &inst, model->winGetFont(), FALSE );
	if( entry == NULL )
		return;
	GadgetButtonSetText( entry, TheGameText->fetch( "GUI:FriendInvite" ) );
	entry->winSetDrawFunc( RightClickEntryDraw );
}

/** A right-click entry answered by -online's friends: TRUE when it was one of theirs (done now), FALSE
	* for the card and anything else, which keep EA's way. */
static Bool rcFriendAction( Int controlID, Int id, const AsciiString &nick, Bool isBuddy, Bool isRequest )
{
	static const NameKeyType inviteID = TheNameKeyGenerator->nameToKey( INVITE_ENTRY );
	if( controlID == (Int)inviteID )
	{
		OnlineInviteFriend( id );
		friendsLineFor( "GUI:FriendInviteSentTo", nick );
		DEBUG_LOG(("ONLINE INVITE screen to %d\n", id));
		return TRUE;
	}
	if( controlID == (Int)buttonAddID )
	{
		if( isRequest )
		{
			OnlineAnswerFriend( id, TRUE );
			TheGameSpyInfo->getBuddyRequestMap()->erase( id );
		}
		else if( !isBuddy )
		{
			OnlineAddFriend( nick );
			friendsLineFor( "GUI:FriendRequestSentTo", nick );
		}
		DEBUG_LOG(("ONLINE FRIEND screen add id=%d request=%d\n", id, (Int)isRequest));
	}
	else if( controlID == (Int)buttonDeleteID )
	{
		if( isRequest )
		{
			OnlineAnswerFriend( id, FALSE );
			TheGameSpyInfo->getBuddyRequestMap()->erase( id );
		}
		else
		{
			OnlineRemoveFriend( id );
			TheGameSpyInfo->getBuddyMap()->erase( id );
		}
		DEBUG_LOG(("ONLINE FRIEND screen remove id=%d request=%d\n", id, (Int)isRequest));
	}
	else if( controlID == (Int)buttonIgnoreID )
	{
		// IGNORE is EA's list on this machine and a block on the service; UNIGNORE lifts both
		const Bool blocked = isBlockedOnline( id );
		if( TheGameSpyInfo->isSavedIgnored( id ) || blocked )
		{
			if( TheGameSpyInfo->isSavedIgnored( id ) )
				TheGameSpyInfo->removeFromSavedIgnoreList( id );
			if( blocked )
				OnlineRemoveFriend( id );
		}
		else
		{
			TheGameSpyInfo->addToSavedIgnoreList( id, nick );
			OnlineBlockPlayer( id );
		}
		DEBUG_LOG(("ONLINE FRIEND screen block id=%d lift=%d\n", id, (Int)blocked));
		refreshIgnoreList();
	}
	else
		return FALSE;
	updateBuddyInfo();
	PopulateLobbyPlayerListbox();
	return TRUE;
}

WindowMsgHandledType WOLBuddyOverlayRCMenuSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{
		
		case GWM_CREATE:
			{
				
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				rcMenu = NULL;
				break;
			} // case GWM_DESTROY:

		case GGM_CLOSE:
			{
				closeRightClickMenu(window);
				//rcMenu = NULL;
				break;
			}
		

		case GBM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				GameSpyRCMenuData *rcData = (GameSpyRCMenuData*)window->winGetUserData();
				if(!rcData)
					break;
				DEBUG_ASSERTCRASH(rcData, ("WOLBuddyOverlayRCMenuSystem GBM_SELECTED:: we're attempting to read the GameSpyRCMenuData from the window, but the data's not there"));
				GPProfile profileID = rcData->m_id;
				AsciiString nick = rcData->m_nick;

				Bool isBuddy = false, isRequest = false;
				Bool isGameSpyUser = profileID > 0;
				if (rcData->m_itemType == ITEM_BUDDY)
					isBuddy = TRUE;
				else if (rcData->m_itemType == ITEM_REQUEST)
					isRequest = TRUE;

				if(rcData)
				{
					delete rcData;
					rcData = NULL;
				}
				window->winSetUserData(NULL);
				//DEBUG_ASSERTCRASH(profileID > 0, ("Bad profile ID in user data!"));

				// -online with friends: every entry asks the service; its answer moves the rows
				if( onlineFriends() && isGameSpyUser && rcFriendAction( controlID, (Int)profileID, nick, isBuddy, isRequest ) )
				{
					closeRightClickMenu(window);
					break;
				}

				if( controlID == buttonAddID )
				{
					if(!isGameSpyUser)
						break;
					DEBUG_LOG(("ButtonAdd was pushed\n"));
					if (isRequest)
					{
						// ok the request
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_OKADD;
						req.arg.profile.id = profileID;
						TheGameSpyBuddyMessageQueue->addRequest(req);

						BuddyInfoMap *m = TheGameSpyInfo->getBuddyRequestMap();
						m->erase( profileID );
						// if the profile ID is not from a buddy and we're okaying his request, then
						// request to add him to our list automatically CLH 2-18-03
						if(!TheGameSpyInfo->isBuddy(profileID))
						{
							RequestBuddyAdd(profileID, nick);		
						}
						updateBuddyInfo();
					}
					else if (!isBuddy)
					{
						RequestBuddyAdd(profileID, nick);
					}
				}
				else if( controlID == buttonDeleteID )
				{
					if(!isGameSpyUser)
						break;
					if (isBuddy)
					{
						// delete the buddy
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_DELBUDDY;
						req.arg.profile.id = profileID;
						TheGameSpyBuddyMessageQueue->addRequest(req);
					}
					else 
					{
						// delete the request
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_DENYADD;
						req.arg.profile.id = profileID;
						TheGameSpyBuddyMessageQueue->addRequest(req);
						BuddyInfoMap *m = TheGameSpyInfo->getBuddyRequestMap();
						m->erase( profileID );
					}
					BuddyInfoMap *buddies = (isBuddy)?TheGameSpyInfo->getBuddyMap():TheGameSpyInfo->getBuddyRequestMap();
					buddies->erase(profileID);
					updateBuddyInfo();
					DEBUG_LOG(("ButtonDelete was pushed\n"));
					PopulateLobbyPlayerListbox();
				}
				else if( controlID == buttonPlayID )
				{
					DEBUG_LOG(("buttonPlayID was pushed\n"));
				}
				else if( controlID == buttonIgnoreID )
				{
					DEBUG_LOG(("%s is isGameSpyUser %d", nick.str(), isGameSpyUser));
					if( isGameSpyUser )
					{
						if(TheGameSpyInfo->isSavedIgnored(profileID))
						{
							TheGameSpyInfo->removeFromSavedIgnoreList(profileID);
						}
						else
						{
							TheGameSpyInfo->addToSavedIgnoreList(profileID, nick);
						}
					}
					else
					{	
						if(TheGameSpyInfo->isIgnored(nick))
						{
							TheGameSpyInfo->removeFromIgnoreList(nick);
						}
						else
						{
							TheGameSpyInfo->addToIgnoreList(nick);
						}
					}
					updateBuddyInfo();
					refreshIgnoreList();
					// repopulate our player listboxes now
					PopulateLobbyPlayerListbox();
				}
				else if( controlID == buttonStatsID )
				{
					DEBUG_LOG(("buttonStatsID was pushed\n"));
					GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);
					SetLookAtPlayer(profileID,nick );
					GameSpyOpenOverlay(GSOVERLAY_PLAYERINFO);
					PSRequest req;
					req.requestType = PSRequest::PSREQUEST_READPLAYERSTATS;
					req.player.id = profileID;
					TheGameSpyPSMessageQueue->addRequest(req);
				}
				closeRightClickMenu(window);
				break;
			}
		default:
			return MSG_IGNORED;
	
	}//Switch		
	return MSG_HANDLED;
}


void setUnignoreText( WindowLayout *layout, AsciiString nick, GPProfile id)
{
	AsciiString controlName;
	controlName.format("%s:ButtonIgnore",layout->getFilename().str()+6);
	NameKeyType ID =  TheNameKeyGenerator->nameToKey( controlName );
	GameWindow *win = TheWindowManager->winGetWindowFromId(layout->getFirstWindow(), ID);
	if(win)
	{
		if(TheGameSpyInfo->isSavedIgnored(id) || TheGameSpyInfo->isIgnored(nick) || isBlockedOnline(id))
			GadgetButtonSetText(win, TheGameText->fetch("GUI:Unignore"));
	}
}

void refreshIgnoreList( void )
{

	if (listboxIgnore == NULL)
		return;		// a right-click menu over a lobby, the box shut

	SavedIgnoreMap tempMap;
	tempMap = TheGameSpyInfo->returnSavedIgnoreList();
	SavedIgnoreMap::iterator it = tempMap.begin();
	GadgetListBoxReset(listboxIgnore);
	while(it != tempMap.end())
	{
		UnicodeString name;
		name.translate(it->second);
		Int pos = GadgetListBoxAddEntryText(listboxIgnore, name, GameMakeColor(255,100,100,255),-1);
		GadgetListBoxSetItemData(listboxIgnore, (void *)(intptr_t)it->first,pos );
		++it;
	}
	IgnoreList tempList;
	tempList = TheGameSpyInfo->returnIgnoreList();
	IgnoreList::iterator iListIt = tempList.begin();
	while( iListIt != tempList.end())
	{
		AsciiString aName = *iListIt;
		UnicodeString name;
		name.translate(aName);
		Int pos = GadgetListBoxAddEntryText(listboxIgnore, name, GameMakeColor(255,100,100,255),-1);
		GadgetListBoxSetItemData(listboxIgnore, 0,pos );
		++iListIt;
	}

	// -online: the players blocked on the service from another machine, not in this one's list
	if (onlineFriends())
	{
		for (size_t each = 0; each < s_friendRows.size(); ++each)
		{
			const OnlineFriend &row = s_friendRows[each];
			if (row.status != ONLINE_FRIEND_BLOCKED || tempMap.find(row.id) != tempMap.end())
				continue;
			UnicodeString name;
			name.translate(row.nick);
			Int pos = GadgetListBoxAddEntryText(listboxIgnore, name, GameMakeColor(255,100,100,255),-1);
			GadgetListBoxSetItemData(listboxIgnore, (void *)(intptr_t)row.id, pos );
		}
	}

//
//	GPProfile profileID = 0;
//	PlayerInfoMap::iterator it = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
//	if (it != TheGameSpyInfo->getPlayerInfoMap()->end())
//		profileID = it->second.m_profileID;

}

//=================================================================================================
// The friends corner (package F4): the FRIENDS key on an online page's tab row, the invite toast
// under the head's right end, and a JOIN carried from wherever it was pressed to the custom lobby,
// where the room is joined the way the lobby's own Join joins it.  Every window here is a child of
// the shell's top screen, so it hides and goes with that screen; the holders' system function hears
// the destroy and forgets them.
//=================================================================================================
static const Int FRIENDS_KEY_WIDTH = 200;				///< at 1080; the header keeps it clear of the server line
static const Int INVITE_LIFE_MS = 120000;				///< the service's ONLINE_INVITE_MS, for the toast's bar
static const UnsignedInt JOIN_WAIT_MS = 20000;	///< how long a JOIN may take to reach the lobby
// the toast on the board, under the head's right end: two lines, the keys, the bar along its foot
static const Int TOAST_LEFT = 1380;
static const Int TOAST_TOP = 182;
static const Int TOAST_RIGHT = 1860;
static const Int TOAST_BOTTOM = 354;
static const Int TOAST_KEY_TOP = 270;
static const Int TOAST_KEY_WIDTH = 150;

static const char *const SCREEN_LOBBY = "Menus/WOLCustomLobby.wnd";
static const char *const SCREEN_ROOM = "Menus/GameSpyGameOptionsMenu.wnd";
static const char *const SCREEN_SCORE = "Menus/ScoreScreen.wnd";
static const char *const SCREEN_HUB = "Menus/WOLWelcomeMenu.wnd";
static const char *const SCREEN_QUICK_MATCH = "Menus/WOLQuickMatchMenu.wnd";
static const char *const SCREEN_LADDER = "Menus/WOLLadderScreen.wnd";

static GameWindow *s_cornerParent = NULL;		///< the screen the windows below are children of
static GameWindow *s_keyHolder = NULL;
static GameWindow *s_friendsKey = NULL;
static Int s_friendsKeyCount = -1;
static GameWindow *s_toast = NULL;
static GameWindow *s_toastJoin = NULL;
static GameWindow *s_toastDecline = NULL;
static OnlineInvite s_toastInvite;					///< what the toast shows; game 0 for nothing
static UnicodeString s_toastName;
static UnicodeString s_toastRoom;

/// a JOIN on its way to the lobby: game 0 for none
struct FriendsJoin
{
	Int from;
	Int game;
	UnsignedInt until;
	Bool asking;							///< the room's "Leave this room?" is up
	WindowLayout *pressedOn;	///< the screen whose way out was pressed already
};
static FriendsJoin s_join = { 0, 0, 0, FALSE, NULL };

static WindowLayout *topScreen( void )
{
	return TheShell ? TheShell->top() : NULL;
}

static Bool topIs( const char *file )
{
	WindowLayout *top = topScreen();
	return top != NULL && AsciiString( file ).compareNoCase( top->getFilename() ) == 0;
}

static void destroyCorner( void )
{
	if( s_keyHolder )
		TheWindowManager->winDestroy( s_keyHolder );
	if( s_toast )
		TheWindowManager->winDestroy( s_toast );
	s_keyHolder = s_friendsKey = NULL;
	s_toast = s_toastJoin = s_toastDecline = NULL;
	s_toastInvite.from = 0;
	s_friendsKeyCount = -1;
}

static void joinInvite( void );
static void declineInvite( void );

static WindowMsgHandledType cornerSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_DESTROY:
			if( window == s_keyHolder )
				s_keyHolder = s_friendsKey = NULL;
			if( window == s_toast )
				s_toast = s_toastJoin = s_toastDecline = NULL;
			return MSG_HANDLED;
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control == NULL )
				return MSG_HANDLED;
			if( control == s_friendsKey )
				GameSpyToggleOverlay( GSOVERLAY_BUDDY );
			else if( control == s_toastJoin )
				joinInvite();
			else if( control == s_toastDecline )
				declineInvite();
			return MSG_HANDLED;
		}
		default:
			break;
	}
	return MSG_IGNORED;
}

static void drawNothing( GameWindow *, WinInstanceData * )
{
}

/// the toast: who, the room and its mode, JOIN lit and DECLINE, the invite's 120 s as a bar along the foot
static void toastDraw( GameWindow *window, WinInstanceData * )
{
	if( s_toastInvite.from == 0 )
		return;
	const IRegion2D r = MenuBox::windowRect( window );
	MenuBox::fill( r, GameMakeColor( 4, 6, 12, 236 ) );
	MenuBox::outline( r, UIStyle::EDGE );
	UIStyle::litLine( r.lo.x + 1, r.lo.y, UIStyle::px( 64.0f ), max( 1, UIStyle::px( 2.0f ) ), UIStyle::EDGE_LIT );
	static MenuBox::TextPool pool;
	pool.begin();
	const Int pad = UIStyle::px( 20.0f );
	const Int widest = r.hi.x - r.lo.x - 2 * pad;
	GameFont *strong = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY, TRUE );
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	MenuBox::words( pool.get( UIStyle::fitText( s_toastName, strong, widest ) ), strong, r.lo.x + pad, r.lo.y + UIStyle::px( 32.0f ), UIStyle::INK );
	MenuBox::words( pool.get( UIStyle::fitText( s_toastRoom, body, widest ) ), body, r.lo.x + pad, r.lo.y + UIStyle::px( 62.0f ), UIStyle::MUTED );
	const Real share = max( 0.0f, min( 1.0f, s_toastInvite.msLeft / (Real)INVITE_LIFE_MS ) );
	const Int barTop = r.hi.y - UIStyle::px( 18.0f );
	const Int barBottom = barTop + max( 2, UIStyle::px( 3.0f ) );
	MenuBox::fill( MenuBox::region( r.lo.x + pad, barTop, r.hi.x - pad, barBottom ), UIStyle::GRID );
	MenuBox::fill( MenuBox::region( r.lo.x + pad, barTop, r.lo.x + pad + REAL_TO_INT( widest * share ), barBottom ), UIStyle::EDGE_LIT );
}

static GameWindow *makeHolder( GameWindow *parent, const char *name, GameWinDrawFunc draw )
{
	WinInstanceData inst;
	inst.init();
	inst.m_decoratedNameString = name;
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	GameWindow *holder = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED, 0, 0, 1, 1, cornerSystem, &inst );
	if( holder )
		holder->winSetDrawFunc( draw );
	return holder;
}

/// a key of the corner's in the menu language, its look the code's own (setMenuKey)
static GameWindow *makeCornerKey( GameWindow *holder, const char *name, const UnicodeString &label, Bool primary )
{
	if( holder == NULL )
		return NULL;
	WinInstanceData inst;
	inst.init();
	BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
	inst.m_decoratedNameString = name;
	inst.m_id = (Int)TheNameKeyGenerator->nameToKey( inst.m_decoratedNameString );
	GameWindow *key = TheWindowManager->gogoGadgetPushButton( holder, WIN_STATUS_ENABLED, 0, 0, 1, 1, &inst,
		UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE ), FALSE );
	if( key == NULL )
		return NULL;
	GadgetButtonSetText( key, label );
	UIStyle::setMenuKey( key, primary );
	return key;
}

/// FRIENDS and the friends signed in, where the online page's head stood room for it
static void updateFriendsKey( GameWindow *parent )
{
	const IRegion2D r = UIStyle::onlineFriendsKeyRect();
	if( !UIStyle::onlinePageShowing() || r.hi.x <= r.lo.x )
	{
		if( s_keyHolder )
			s_keyHolder->winHide( TRUE );
		return;
	}
	if( s_keyHolder == NULL )
	{
		s_keyHolder = makeHolder( parent, "FriendsCorner.wnd:KeyHolder", drawNothing );
		s_friendsKey = makeCornerKey( s_keyHolder, "FriendsCorner.wnd:ButtonFriends", UIStyle::capitals( TheGameText->fetch( "GUI:Friends" ) ), FALSE );
		s_friendsKeyCount = -1;
		if( s_keyHolder == NULL )
			return;
	}
	s_keyHolder->winHide( FALSE );
	placeOnScreen( s_keyHolder, r );
	placeOnScreen( s_friendsKey, r );
	const Int count = OnlineCountFriendsOnline();
	if( s_friendsKey && count != s_friendsKeyCount )
	{
		s_friendsKeyCount = count;
		UnicodeString label = UIStyle::capitals( TheGameText->fetch( "GUI:Friends" ) );
		if( count > 0 )
		{
			UnicodeString counted;
			counted.format( u"%s  %d", label.str(), count );
			label = counted;
		}
		GadgetButtonSetText( s_friendsKey, label );
	}
}

/// the mode's word for the toast's second line, as the mode box names it
static UnicodeString modeWords( const AsciiString &mode )
{
	if( mode.compareNoCase( "directstrike" ) == 0 )
		return TheGameText->fetch( "GUI:GameModeDirectStrike" );
	if( mode.compareNoCase( "apocalypse" ) == 0 )
		return TheGameText->fetch( "GUI:GameModeApocalypse" );
	if( mode.compareNoCase( "teamai" ) == 0 )
		return TheGameText->fetch( "GUI:GameModeTeamAI" );		// a party invite: no room, game 0
	return TheGameText->fetch( "GUI:GameModeClassic" );
}

/// the oldest invite still up, unless its JOIN is on the way; the toast made, kept or taken away
static void updateToast( GameWindow *parent )
{
	std::vector<OnlineInvite> invites;
	OnlineGetInvites( invites );
	const OnlineInvite *show = NULL;
	for( size_t each = 0; each < invites.size() && show == NULL; each++ )
	{
		if( s_join.game != 0 && invites[ each ].from == s_join.from && invites[ each ].game == s_join.game )
			continue;
		show = &invites[ each ];
	}
	if( show == NULL )
	{
		if( s_toast )
			TheWindowManager->winDestroy( s_toast );
		s_toast = s_toastJoin = s_toastDecline = NULL;
		s_toastInvite.from = 0;
		return;
	}
	if( s_toast == NULL )
	{
		s_toast = makeHolder( parent, "FriendsCorner.wnd:InviteToast", toastDraw );
		s_toastJoin = makeCornerKey( s_toast, "FriendsCorner.wnd:ButtonJoin", UIStyle::capitals( TheGameText->fetch( "GUI:FriendJoin" ) ), TRUE );
		s_toastDecline = makeCornerKey( s_toast, "FriendsCorner.wnd:ButtonDecline", UIStyle::capitals( TheGameText->fetch( "GUI:FriendDecline" ) ), FALSE );
		s_toastInvite.from = 0;
		if( s_toast == NULL )
			return;
		s_toast->winBringToTop();
	}
	if( show->from != s_toastInvite.from || show->game != s_toastInvite.game )
	{
		UnicodeString who;
		who.translate( show->nick );
		s_toastName.format( TheGameText->fetch( "GUI:FriendInvites" ), who.str() );
		const UnicodeString mode = modeWords( show->mode );
		if( show->roomName.isEmpty() )
			s_toastRoom = mode;
		else
			s_toastRoom.format( u"%s \x00B7 %s", show->roomName.str(), mode.str() );
		DEBUG_LOG(("ONLINE INVITE screen shows from=%d game=%d\n", show->from, show->game));
		AudioEventRTS incoming( "GUICommunicatorIncoming" );
		if( TheAudio )
			TheAudio->addAudioEvent( &incoming );
	}
	s_toastInvite = *show;
	MenuBox::place( s_toast, MenuBox::region( TOAST_LEFT, TOAST_TOP, TOAST_RIGHT, TOAST_BOTTOM ) );
	const Int keyRight = TOAST_RIGHT - 20;
	MenuBox::place( s_toastJoin, MenuBox::region( keyRight - TOAST_KEY_WIDTH, TOAST_KEY_TOP, keyRight, TOAST_KEY_TOP + UIStyle::BOX_KEY_HEIGHT ) );
	MenuBox::place( s_toastDecline, MenuBox::region( keyRight - 2 * TOAST_KEY_WIDTH - UIStyle::BOX_KEY_GAP, TOAST_KEY_TOP,
		keyRight - TOAST_KEY_WIDTH - UIStyle::BOX_KEY_GAP, TOAST_KEY_TOP + UIStyle::BOX_KEY_HEIGHT ) );
}

static void leaveRoomYes( void )
{
	s_join.asking = FALSE;
	s_join.until = Clock_Milliseconds() + JOIN_WAIT_MS;
}

static void leaveRoomNo( void )
{
	s_join.asking = FALSE;
	s_join.game = 0;		// the toast comes back while the invite lasts
}

static void joinInvite( void )
{
	if( s_toastInvite.from == 0 )
		return;
	if( s_toastInvite.game == 0 )
	{
		// a Team vs AI party's invite: taken where it stands, the hub shows the party
		DEBUG_LOG(("ONLINE INVITE screen joins the party of %d\n", s_toastInvite.from));
		OnlineAnswerInvite( s_toastInvite.from, 0, TRUE );
		s_toastInvite.from = 0;
		return;
	}
	s_join.from = s_toastInvite.from;
	s_join.game = s_toastInvite.game;
	s_join.until = Clock_Milliseconds() + JOIN_WAIT_MS;
	s_join.pressedOn = NULL;
	s_join.asking = FALSE;
	DEBUG_LOG(("ONLINE INVITE screen join from=%d game=%d\n", s_join.from, s_join.game));
	if( topIs( SCREEN_ROOM ) && TheGameSpyInfo && TheGameSpyInfo->getCurrentStagingRoomID() != 0 )
	{
		s_join.asking = TRUE;
		GSMessageBoxYesNo( TheGameText->fetch( "GUI:FriendInviteTitle" ), TheGameText->fetch( "GUI:FriendLeaveRoom" ), leaveRoomYes, leaveRoomNo );
	}
}

static void declineInvite( void )
{
	if( s_toastInvite.from == 0 )
		return;
	DEBUG_LOG(("ONLINE INVITE screen decline from=%d game=%d\n", s_toastInvite.from, s_toastInvite.game));
	OnlineAnswerInvite( s_toastInvite.from, s_toastInvite.game, FALSE );
	s_toastInvite.from = 0;
}

/// a screen's own key pressed as a click on it presses it, if it shows and takes clicks
static Bool pressKey( GameWindow *screen, const char *name )
{
	GameWindow *key = TheWindowManager->winGetWindowFromId( screen, TheNameKeyGenerator->nameToKey( name ) );
	if( key == NULL || key->winIsHidden() || !key->winGetEnabled() )
		return FALSE;
	GameWindow *owner = key->winGetOwner() ? key->winGetOwner() : screen;
	TheWindowManager->winSendSystemMsg( owner, GBM_SELECTED, (WindowMsgData)key, key->winGetWindowId() );
	return TRUE;
}

/// the lobby joins the invite's room as its Join does, the service waiving the password
static void joinFromLobby( void )
{
	GameSpyStagingRoom *room = TheGameSpyInfo ? TheGameSpyInfo->findStagingRoomByID( s_join.game ) : NULL;
	if( room == NULL || TheGameSpyGame == NULL )
		return;		// the lobby's list fills a moment after it opens; JOIN_WAIT_MS bounds the wait
	const Int from = s_join.from;
	const Int game = s_join.game;
	s_join.game = 0;
	if( room->getExeCRC() != TheGlobalData->m_exeCRC || room->getIniCRC() != TheGlobalData->m_iniCRC )
	{
		DEBUG_LOG(("ONLINE INVITE screen join refused: another build in game %d\n", game));
		OnlineAnswerInvite( from, game, FALSE );
		GSMessageBoxOk( TheGameText->fetch( "GUI:JoinFailedDefault" ), TheGameText->fetch( "GUI:FriendErrorBuild" ) );
		return;
	}
	TheGameSpyInfo->markAsStagingRoomJoiner( game );
	TheGameSpyGame->setGameName( room->getGameName() );
	TheGameSpyGame->setLadderIP( room->getLadderIP() );
	TheGameSpyGame->setLadderPort( room->getLadderPort() );
	SetLobbyAttemptHostJoin( TRUE );
	LobbySetJoiningGame( game );
	OnlineAnswerInvite( from, game, TRUE );
	DEBUG_LOG(("ONLINE INVITE screen join sent from=%d game=%d\n", from, game));
}

/// a JOIN carried a screen nearer the lobby: the room left, the end screen closed, the Custom tab taken
static void stepJoin( void )
{
	if( s_join.game == 0 || s_join.asking )
		return;
	if( Clock_Milliseconds() > s_join.until )
	{
		DEBUG_LOG(("ONLINE INVITE screen join gave up on game %d\n", s_join.game));
		s_join.game = 0;
		return;
	}
	std::vector<OnlineInvite> invites;
	OnlineGetInvites( invites );
	Bool alive = FALSE;
	for( size_t each = 0; each < invites.size() && !alive; each++ )
		alive = invites[ each ].from == s_join.from && invites[ each ].game == s_join.game;
	if( !alive )
	{
		s_join.game = 0;		// gone, filled or started meanwhile
		return;
	}
	WindowLayout *top = topScreen();
	if( top == NULL || !TheShell->isAnimFinished() )
		return;
	if( topIs( SCREEN_LOBBY ) )
	{
		joinFromLobby();
		return;
	}
	if( top == s_join.pressedOn )
		return;		// its way out is pressed; the next screen is on its way
	const char *key = NULL;
	if( topIs( SCREEN_ROOM ) )
		key = "GameSpyGameOptionsMenu.wnd:ButtonBack";
	else if( topIs( SCREEN_SCORE ) )
		key = "ScoreScreen.wnd:ButtonOk";
	else if( topIs( SCREEN_HUB ) )
		key = "WOLWelcomeMenu.wnd:TabCustom";
	else if( topIs( SCREEN_QUICK_MATCH ) )
		key = "WOLQuickMatchMenu.wnd:TabCustom";
	else if( topIs( SCREEN_LADDER ) )
		key = "WOLLadderScreen.wnd:TabCustom";
	if( key != NULL && pressKey( top->getFirstWindow(), key ) )
		s_join.pressedOn = top;
}

void FriendsCornerUpdate( void )
{
	if( !onlineFriends() )
	{
		UIStyle::onlineReserveFriendsKey( 0 );
		destroyCorner();
		s_join.game = 0;
		return;
	}
	UIStyle::onlineReserveFriendsKey( FRIENDS_KEY_WIDTH );
	if( !buddyControls.isInit )
		showFriendError();		// the box, while open, says it in its chat
	WindowLayout *top = topScreen();
	GameWindow *parent = top ? top->getFirstWindow() : NULL;
	if( parent != s_cornerParent )
	{
		destroyCorner();
		s_cornerParent = parent;
	}
	if( parent == NULL )
		return;
	updateFriendsKey( parent );
	updateToast( parent );
	stepJoin();
}
