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
// FILE: WOLWelcomeMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "gamespy/peer/peer.h"

#include <map>
#include <vector>

#include "Lib/Clock.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/CustomMatchPreferences.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/RandomValue.h"
#include "Common/UserPreferences.h"
#include "Common/PlayerTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NetworkInterface.h"

#include "GameClient/AnimateWindowManager.h"
#include "GameClient/TankSkin.h"
#include "GameClient/Display.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MessageBox.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/MapUtil.h"
#include "GameClient/UIStyle.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/GameSpyOverlay.h"

#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/DisplayStringManager.h"
#include "Common/QuickmatchPreferences.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GeneralsOnline/OnlineBridge.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static Bool isShuttingDown = FALSE;
static Bool buttonPushed = FALSE;
static const char *nextScreen = NULL;		///< a screen pushed once the hub has popped: the login, after Sign out

// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLWelcomeID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonQuickMatchID = NAMEKEY_INVALID;
static NameKeyType buttonLobbyID = NAMEKEY_INVALID;
static NameKeyType buttonMyInfoID = NAMEKEY_INVALID;
static NameKeyType buttonPlayAIID = NAMEKEY_INVALID;
static NameKeyType buttonSignOutID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLWelcome = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonQuickMatch = NULL;
static GameWindow *buttonLobby = NULL;
static GameWindow *buttonPlayAI = NULL;
static GameWindow *buttonTeamSize = NULL;		///< Team vs AI's size, made in code (makeTeamKeys)
static GameWindow *comboBoxAISpeed = NULL;		///< the speed ai.start asks for
static GameWindow *listboxInfo = NULL;

/// the speed Play vs AI asks for, kept as Quick Match keeps its own
static Int aiGameSpeed( void )
{
	QuickMatchPreferences pref;
	return OnlineMatchSpeed( pref.getInt( "AIGameSpeed", RETAIL_GAME_SPEED_PERCENT ) );
}

/// the hub's own window by its name in the master, and its id
static GameWindow *hubWindow( const char *name, NameKeyType *id = NULL )
{
	AsciiString full;
	full.format( "WOLWelcomeMenu.wnd:%s", name );
	const NameKeyType key = TheNameKeyGenerator->nameToKey( full );
	if (id)
		*id = key;
	return TheWindowManager->winGetWindowFromId( NULL, key );
}


static Bool s_controlsLive = FALSE;		///< Find match and Browse games as the room list and joins allow
static Bool s_joiningRoom = FALSE;		///< a chat room join is out (Browse games): the hub waits for its answer

/// a Team vs AI party of two or more: Quick Match is one player's, so its card and tab wait until it ends
static Bool inParty( void )
{
	if (!OnlineTeamAIAvailable())
		return FALSE;
	std::vector<OnlinePartyMember> party;
	OnlineGetParty( party );
	return party.size() >= 2;
}

/// Find match also waits out the server's lock on a player who left a ranked match, and a party
static void applyControls( void )
{
	const Bool party = inParty();
	if (buttonQuickMatch)
		buttonQuickMatch->winEnable( s_controlsLive && OnlineGetQuickMatchLockSeconds() == 0 && !party );
	if (GameWindow *tab = hubWindow( "TabQuickMatch" ))
		tab->winEnable( !party );
	if (buttonLobby)
		buttonLobby->winEnable( s_controlsLive );
}

static void enableControls( Bool state )
{
	s_controlsLive = state;
	applyControls();
}

static void freeHubStrings( void );

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = FALSE;

	// made again by the next init: a push hides this layout and the pop that follows runs init again
	UIStyle::onlinePageShutdown( parentWOLWelcome );
	freeHubStrings();

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		TheShell->push( AsciiString( nextScreen ) );
	}

	nextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** The message of the day, in the hub's MOTD card: its first line INK, the rest MUTED, a line that
	* names its own colour ("\aarrggbbText") in that colour */
//-------------------------------------------------------------------------------------------------
static UnsignedByte grabUByte(const char *s)
{
	char tmp[5] = "0xff";
	tmp[2] = s[0];
	tmp[3] = s[1];
	UnsignedByte b = strtol(tmp, NULL, 16);
	return b;
}

static void addMOTDLine( UnicodeString line, Color color )
{
	line.trim();
	if (line.isEmpty())
		line = UnicodeString(u" ");
	GadgetListBoxAddEntryText(listboxInfo, line, color, -1, -1);
}

static void fillMOTD(void)
{
	if (!listboxInfo || !TheGameSpyInfo)
		return;
	GadgetListBoxReset(listboxInfo);
	AsciiString aMotd = TheGameSpyInfo->getMOTD();
	// -online: the server's MOTD alone, without EA's "Welcome to Generals Zero Hour Online." over it
	UnicodeString headingStr;
	if (!OnlineIsEnabled())
		headingStr = TheGameText->fetch("MOTD:NumPlayersHeading");

	Bool first = TRUE;
	UnicodeString line;
	while (headingStr.nextToken(&line, UnicodeString(u"\n")))
	{
		addMOTDLine( line, first ? UIStyle::INK : UIStyle::MUTED );
		first = FALSE;
	}

	AsciiString aLine;
	while (aMotd.nextToken(&aLine, "\n"))
	{
		aLine.trim();
		Color c = first ? UIStyle::INK : UIStyle::MUTED;
		first = FALSE;
		if (aLine.startsWith("\\\\"))
		{
			aLine = aLine.str()+1;
		}
		else if (aLine.startsWith("\\") && aLine.getLength() > 9)
		{
			// take out the hex value from strings starting as "\ffffffffText"
			UnsignedByte a, r, g, b;
			a = grabUByte(aLine.str()+1);
			r = grabUByte(aLine.str()+3);
			g = grabUByte(aLine.str()+5);
			b = grabUByte(aLine.str()+7);
			c = GameMakeColor(r, g, b, a);
			aLine = aLine.str() + 9;
		}
		addMOTDLine( UnicodeString(MultiByteToWideCharSingleLine(aLine.str()).c_str()), c );
	}

	// the scroll bar only for a message longer than its card
	const ListboxData *list = (const ListboxData *)listboxInfo->winGetUserData();
	const Bool fits = list->totalHeight <= list->displayHeight - TOTAL_OUTLINE_HEIGHT;
	GameWindow *const bar[] = { list->upButton, list->downButton, list->slider };
	for (size_t i = 0; i < sizeof( bar ) / sizeof( bar[ 0 ] ); ++i)
		if (bar[ i ])
			bar[ i ]->winHide( fits );
}

void HandleNumPlayersOnline( Int numPlayersOnline )
{
	fillMOTD();
}

/// GameSpy's daily army win percentages fed a panel the hub no longer has
void HandleOverallStats( const char* szHTTPStats, unsigned len )
{
}

/// GameSpy's own stats filled a player panel on the hub and on Quick Match; neither has one now (the hub's
/// cards and Quick Match's header read OnlineGetPlayerInfo each frame)
void UpdateLocalPlayerStats(void)
{
}

//-------------------------------------------------------------------------------------------------
// -online: Play vs AI.  The button asks the server (WOLPlayVsAI) and the screen's update hands its answer
// on (WOLPlayVsAIUpdate): a 1v1 skirmish against one computer player as strong as the player's AI rating,
// on the map and seed the server picked, or the server's reason not to.  OnlineQueues.h has the lines.
//-------------------------------------------------------------------------------------------------
static Bool s_aiAsked = FALSE;							///< ai.start is out
static Int s_aiArmy = PLAYERTEMPLATE_RANDOM;	///< the army it named, which the player then plays
static UnicodeString s_aiRefusal;					///< why the last Play did not start, shown on the card over Play
static UnicodeString s_aiWaitLine;				///< while ai.start is out: "Starting the match...", later "still waiting"
static UnsignedInt s_aiAskedAt = 0;				///< when it went out
static Bool s_cardsDirty = TRUE;						///< the cards' words want laying out again
static Bool s_playOnEntry = FALSE;					///< WOLWelcomeMenuStartPlayVsAIOnEntry: the next init presses Play

/// the army picked on the Quick Match page, which Play vs AI plays too: a player template, or
/// PLAYERTEMPLATE_RANDOM for -1 and for any army the page does not offer (it offers USA, China and GLA
/// and their generals: no Turkiye, no Boss)
static Int quickMatchArmy( void )
{
	if (TheGameSpyInfo == NULL)
		return PLAYERTEMPLATE_RANDOM;
	QuickMatchPreferences pref;
	const Int pick = pref.getSide();
	const PlayerTemplate *army = pick >= 0 && pick < ThePlayerTemplateStore->getPlayerTemplateCount()
		? ThePlayerTemplateStore->getNthPlayerTemplate( pick ) : NULL;
	if (army == NULL || !army->isPlayableSide() || army->getSide() == "Boss")
		return PLAYERTEMPLATE_RANDOM;
	const AsciiString base = army->getBaseSide();
	return base == "America" || base == "China" || base == "GLA" ? pick : PLAYERTEMPLATE_RANDOM;
}

/// still signed in: a disconnect on a page over the hub resets TheGameSpyInfo (no profile) and pops back here
static Bool signedIn( void )
{
	return TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() != 0 && !TheGameSpyInfo->isDisconnectedAfterGameStart( NULL );
}

/* The match: slot 0 the player in that army, slot 1 the computer at the bot's Elo (OnlineGetAIMatch), on the rung
	 nearest it as -airating seats it, its army, both colours and both start spots drawn from the seed.  The
	 speed ai.ready echoed (the player's pick, retail's pace from an older server), and the native tick rate.  Built here rather than through -autoskirmish's door so no
	 switch of that door reaches a match the server rates.  FALSE, with nothing started, when the map is not
	 in this game. */
static Bool startAIMatch( const OnlineAIMatch &ai )
{
	if (ai.humans > 1)
		return OnlineStartTeamAIMatch( ai );		// Team vs AI: the party's network match on the relay
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap( ai.map ) : NULL;
	if (md == NULL || !md->m_isMultiplayer || md->m_numPlayers < 2)
		return FALSE;

	if (TheGameLogic->isInGame())
		TheGameLogic->clearGameData( FALSE );		// the shell map

	if (TheSkirmishGameInfo == NULL)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->enterGame();

	// the seat carries the signed-in nick: the skirmish name defaults to the PC's, which the score
	// screen would then show
	GameSlot player;
	player.setState( SLOT_PLAYER, UnicodeString( MultiByteToWideCharSingleLine( TheGameSpyInfo->getLocalName().str() ).c_str() ) );
	player.setPlayerTemplate( s_aiArmy );
	player.setColor( -1 );
	player.setStartPos( -1 );
	// what the account wears, as the server last said: drawing only, the logic and its CRC never read it,
	// and the server strips it from the replay it keeps of the match
	player.setSkins( TankSkin_localLoadout() );

	static const SlotState RUNG_STATES[ AISKILL_COUNT ] = { SLOT_EASY_AI, SLOT_MED_AI, SLOT_BRUTAL_AI };
	const AISkillLevel rung = aiSkillOfRating( ai.aiRating );
	Int rating = aiRatingToHundredths( ai.aiRating );
	if (rating == aiRatingToHundredths( aiRatingOfSkill( rung ) ))
		rating = AI_RATING_NONE;		// a rung's own anchor is written as the rung alone
	GameSlot computer;
	computer.setState( RUNG_STATES[ rung ] );
	computer.setAIRating( rating );		// after setState, which drops a rating on a change of state
	computer.setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
	computer.setColor( -1 );
	computer.setStartPos( -1 );

	TheSkirmishGameInfo->setSlot( 0, player );
	TheSkirmishGameInfo->setSlot( 1, computer );
	TheSkirmishGameInfo->setLocalIP( TheSkirmishGameInfo->getSlot( 0 )->getIP() );
	TheSkirmishGameInfo->setMap( md->m_fileName );
	TheSkirmishGameInfo->setGameSpeedPercent( ai.speed );
	TheSkirmishGameInfo->setSeed( ai.seed );
	TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
	TheSkirmishGameInfo->startGame( 0 );

	InitRandom( ai.seed );
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	msg->appendIntegerArgument( LOGICFRAMES_PER_SECOND );			// the native tick, whatever -fps says
	msg->appendIntegerArgument( ai.speed );
	DEBUG_LOG(("ONLINE AI START match=%s map=%s seed=%d aiRating=%.2f rung=%d army=%d speed=%d\n", ai.match.str(),
		md->m_fileName.str(), ai.seed, ai.aiRating, (Int)rung, s_aiArmy, ai.speed));
	return TRUE;
}

void WOLPlayVsAI( void )
{
	if (s_aiAsked)
		return;
	s_aiArmy = quickMatchArmy();
	const PlayerTemplate *army = s_aiArmy >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( s_aiArmy ) : NULL;
	OnlineRequestAIMatch( army ? army->getName() : AsciiString( "Random" ), aiGameSpeed() );
	s_aiAsked = TRUE;
	// the card says the ask is out until the server answers it
	s_aiAskedAt = Clock_Milliseconds();
	s_aiWaitLine = TheGameText->fetch( "GUI:OnlineAIAsking" );
	s_cardsDirty = TRUE;
}

/// the answer to Play: the match starts, or the reason it did not goes on the Play vs AI card
Bool WOLPlayVsAIUpdate( void )
{
	// Team vs AI: a party member pressed Play, and this hub answers with the army it would play; the match's
	// ai.ready then comes here unasked
	AsciiString teamAsk, teamFrom;
	if (OnlineTakeTeamAsk( teamAsk, teamFrom ))
	{
		const Int pick = quickMatchArmy();
		const PlayerTemplate *mine = pick >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( pick ) : NULL;
		OnlineAnswerTeamAsk( teamAsk, mine ? mine->getName() : AsciiString( "Random" ) );
	}
	if (!s_aiAsked && !OnlineIsTeamAIMatchReady())
		return FALSE;
	OnlineAIMatch ai;
	const OnlineAIMatchState state = OnlineGetAIMatch( ai );
	if (state == ONLINE_AI_WAITING)
	{
		// a team start waits up to 15 s on the party's hubs; past AI_ASK_SLOW_MS the line says the server is slow.
		// ponytail: no give-up, a late ai.ready still starts the match (giving up would leave it open, a loss)
		static const UnsignedInt AI_ASK_SLOW_MS = 30000;
		if (s_aiAsked && Clock_Milliseconds() - s_aiAskedAt > AI_ASK_SLOW_MS && s_aiWaitLine.compare( TheGameText->fetch( "GUI:OnlineAIStillWaiting" ) ) != 0)
		{
			s_aiWaitLine = TheGameText->fetch( "GUI:OnlineAIStillWaiting" );
			s_cardsDirty = TRUE;
			DEBUG_LOG(("ONLINE AI WAITING over %u ms\n", AI_ASK_SLOW_MS));
		}
		return TRUE;
	}
	s_aiAsked = FALSE;
	s_aiWaitLine.clear();
	s_cardsDirty = TRUE;
	UnicodeString what;
	if (state == ONLINE_AI_READY)
	{
		if (startAIMatch( ai ))
		{
			// the match is loading: no hub key (FIND TEAM, Back, Esc) answers until the next init clears
			// buttonPushed; PLAY and Team greyed meanwhile, the next update after it wakes them
			buttonPushed = TRUE;
			if (buttonPlayAI)
				buttonPlayAI->winEnable( FALSE );
			if (buttonTeamSize)
				buttonTeamSize->winEnable( FALSE );
			return FALSE;
		}
		OnlineDropAIMatch();
		DEBUG_LOG(("ONLINE AI START map=%s missing\n", ai.map.str()));
		UnicodeString map;
		map.translate( ai.map );
		what.format( TheGameText->fetch( "GUI:OnlineAIMapMissing" ), map.str() );
	}
	else if (state == ONLINE_AI_REFUSED)
	{
		// a reason the strings name has its own sentence; any other is quoted in the general one
		AsciiString label, until;
		label.format( "GUI:OnlineAIRefused_%s", ai.reason.str() );
		if (ai.reason == "maintenance")
		{
			OnlineTakeMaintenanceRefusal( until );		// the error names no time, the notice in force does
			what = OnlineMaintenanceText( until );
		}
		else if (UIStyle::hasText( label.str() ))
			what = TheGameText->fetch( label );
		else
		{
			UnicodeString reason;
			reason.translate( ai.reason );
			what.format( TheGameText->fetch( "GUI:OnlineAIRefused" ), reason.str() );
		}
	}
	if (!what.isEmpty())
	{
		DEBUG_LOG(("ONLINE AI REFUSED %s\n", WideCharAsUtf8( what.str() ).str()));
		s_aiRefusal = what;
		s_cardsDirty = TRUE;
	}
	return FALSE;
}

/// the score screen's Play again: the hub's next init presses Play as the player would
void WOLWelcomeMenuStartPlayVsAIOnEntry( void )
{
	s_playOnEntry = TRUE;
}

//-------------------------------------------------------------------------------------------------
// Team vs AI on the Play vs AI card (docs/plans/TEAM-VS-AI.md), on a server that takes it: a Team fact
// with a key that steps 1V1, 2V2, 3V3, 4V4 (never under the party's size), the party on the status row
// with LEAVE at its right, and PLAY that plays the party, queues for a team the party cannot fill
// (FIND TEAM) or leaves the queue (CANCEL).  Friends join the party from the friends box's INVITE.
//-------------------------------------------------------------------------------------------------
static Int s_teamSize = 1;								///< humans a side; 1 is the 1v1
static GameWindow *buttonLeaveParty = NULL;
static UnicodeString s_teamLine;					///< the search or the party, on the status row when no refusal is up
static UnicodeString s_playWords;					///< PLAY as the master words it
static UnsignedInt s_teamQueuedAt = 0;		///< when the queue took us, for the wait's clock
static UnsignedInt s_teamSerial = 0;			///< OnlineGetParty's serial as the card last showed it
static Int s_teamQueued = -1;
static Int s_teamSecond = -1;
static Int s_teamShownSize = -1;
static GameFont *labelFont( void );		// the cards' faces and lengths, further down
static Int hpx( Real at1080 );

static Int partySize( void )
{
	std::vector<OnlinePartyMember> party;
	OnlineGetParty( party );
	return (Int)party.size();
}

/// a key the master has no window for, made once on the hub's panel, drawn by the language
static GameWindow *makeTeamKey( const char *name, const UnicodeString &label )
{
	if (parentWOLWelcome == NULL)
		return NULL;
	const NameKeyType id = TheNameKeyGenerator->nameToKey( name );
	GameWindow *key = TheWindowManager->winGetWindowFromId( parentWOLWelcome, id );		// the hub's next init
	if (key == NULL)
	{
		WinInstanceData inst;
		inst.init();
		BitSet( inst.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK );
		inst.m_decoratedNameString = name;
		inst.m_id = (Int)id;
		key = TheWindowManager->gogoGadgetPushButton( parentWOLWelcome, WIN_STATUS_ENABLED, 0, 0, 1, 1, &inst, labelFont(), FALSE );
		if (key == NULL)
			return NULL;
		key->winBringToTop();
	}
	GadgetButtonSetText( key, label );
	UIStyle::setMenuKey( key, FALSE );
	key->winHide( TRUE );
	return key;
}

/// a key stood on the screen at x, y, w, h, whatever its parent
static void placeTeamKey( GameWindow *key, Int x, Int y, Int w, Int h )
{
	if (key == NULL)
		return;
	Int px = 0, py = 0;
	if (key->winGetParent())
		key->winGetParent()->winGetScreenPosition( &px, &py );
	key->winSetPosition( x - px, y - py );
	key->winSetSize( w, h );
}

static void makeTeamKeys( void )
{
	buttonTeamSize = makeTeamKey( "WOLWelcomeMenu.wnd:ButtonTeamSize", UnicodeString( u"1V1" ) );
	buttonLeaveParty = makeTeamKey( "WOLWelcomeMenu.wnd:ButtonLeaveParty", UIStyle::capitals( TheGameText->fetch( "GUI:OnlineHubLeaveParty" ) ) );
	s_playWords = UIStyle::capitals( TheGameText->fetch( "GUI:OnlineHubPlay" ) );	// the master's word: the key may still read FIND TEAM
	s_teamSerial = 0;
	s_teamQueued = s_teamSecond = s_teamShownSize = -1;
}

/** The card's team state each update: the size held at the party's, the keys shown and placed on card (the
	* Play vs AI card's window), PLAY's word, the status line. */
static void updateTeam( GameWindow *card )
{
	const Bool on = OnlineTeamAIAvailable();
	std::vector<OnlinePartyMember> party;
	const UnsignedInt serial = OnlineGetParty( party );
	const Int members = on ? (Int)party.size() : 0;
	if (!on)
		s_teamSize = 1;
	else if (members > s_teamSize)
		s_teamSize = min( members, (Int)ONLINE_TEAM_SEATS );
	const Int queued = on ? OnlineGetTeamAIQueue() : 0;
	if (queued > 0 && s_teamQueuedAt == 0)
		s_teamQueuedAt = Clock_Milliseconds();
	else if (queued == 0)
		s_teamQueuedAt = 0;
	const Int second = queued > 0 ? (Int)( ( Clock_Milliseconds() - s_teamQueuedAt ) / 1000 ) : -1;

	if (card && buttonTeamSize && buttonLeaveParty)
	{
		Int x, y, w, h;
		card->winGetScreenPosition( &x, &y );
		card->winGetSize( &w, &h );
		const Int right = x + w - hpx( 24.0f ), keyWidth = hpx( 110.0f );
		placeTeamKey( buttonTeamSize, right - keyWidth, y + hpx( 192.0f ), keyWidth, hpx( 32.0f ) );
		placeTeamKey( buttonLeaveParty, right - keyWidth, y + h - hpx( 98.0f ) - hpx( 14.0f ), keyWidth, hpx( 28.0f ) );
		buttonTeamSize->winHide( !on );
		buttonTeamSize->winEnable( queued == 0 );
		buttonLeaveParty->winHide( members < 2 );
	}
	if (serial == s_teamSerial && queued == s_teamQueued && second == s_teamSecond && s_teamSize == s_teamShownSize)
		return;
	s_teamSerial = serial;
	s_teamQueued = queued;
	s_teamSecond = second;
	s_teamShownSize = s_teamSize;
	s_cardsDirty = TRUE;

	UnicodeString size;
	size.format( u"%dV%d", s_teamSize, s_teamSize );
	if (buttonTeamSize)
		GadgetButtonSetText( buttonTeamSize, size );
	if (buttonPlayAI)
	{
		UnicodeString play = s_playWords;
		if (queued > 0)
			play = UIStyle::capitals( TheGameText->fetch( "GUI:OnlineHubCancelSearch" ) );
		else if (s_teamSize > max( 1, members ))
			play = UIStyle::capitals( TheGameText->fetch( "GUI:OnlineHubFindTeam" ) );
		GadgetButtonSetText( buttonPlayAI, play );
	}
	s_teamLine.clear();
	if (queued > 0)
	{
		UnicodeString wanted;
		wanted.format( u"%dV%d", queued, queued );
		s_teamLine.format( TheGameText->fetch( "GUI:OnlineHubFindingTeam" ), wanted.str(), second / 60, second % 60 );
	}
	else if (members >= 2)
	{
		UnicodeString names;
		for (size_t each = 0; each < party.size(); ++each)
		{
			UnicodeString nick;
			nick.translate( party[ each ].nick );
			if (each)
				names.concat( u", " );
			names.concat( nick );
		}
		s_teamLine.format( u"%s  %s", UIStyle::capitals( TheGameText->fetch( "GUI:OnlineHubParty" ) ).str(), names.str() );
	}
}

/// the Team key: the next size the party can play, back to its own (or the 1v1) after 4V4
static void stepTeamSize( void )
{
	const Int members = partySize();
	const Int smallest = members >= 2 ? members : 1;
	s_teamSize = s_teamSize >= (Int)ONLINE_TEAM_SEATS ? smallest : max( smallest, s_teamSize + 1 );
}

static void pressPlay( void )
{
	s_aiRefusal.clear();
	s_cardsDirty = TRUE;
	// Team vs AI: CANCEL leaves the queue; a team bigger than the party (or than a lone player) is found by it
	if (OnlineTeamAIAvailable())
	{
		if (OnlineGetTeamAIQueue() > 0)
		{
			OnlineLeaveTeamAIQueue();
			return;
		}
		if (s_teamSize > max( 1, partySize() ))
		{
			s_aiArmy = quickMatchArmy();
			const PlayerTemplate *army = s_aiArmy >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( s_aiArmy ) : NULL;
			OnlineQueueTeamAI( s_teamSize, army ? army->getName() : AsciiString( "Random" ), aiGameSpeed() );
			return;
		}
	}
	WOLPlayVsAI();
	if (buttonPlayAI)
		buttonPlayAI->winEnable( FALSE );		// the update wakes it when the answer is in
}

//-------------------------------------------------------------------------------------------------
// The hub's cards in the menu language (menu package 9, screens/online-entry/README-online-entry.md
// "Hub"), drawn in code behind the master's keys on the master's clear boxes.  Every length is the
// design's at 1080 through UIStyle::px, the scale the master was fitted at; type through
// UIStyle::menuFont.  The head, frame A and the foot's rule are UIStyle::onlineHeader's.
//-------------------------------------------------------------------------------------------------
enum { CARD_FACTS = 4, RECENT_ROWS = 5, FORM_RESULTS = 5, OPEN_GAME_ROWS = 3, LADDER_ROWS = 5 };
static const Real FACT_WIDTH = 236.0f;		///< the facts' column, ending 24 in from the card's right

struct HubCard
{
	GameWindow *window;
	Bool portraitColumn;				///< Play vs AI and Quick Match keep the rank portrait's column
	const Image *portrait;			///< NULL for an unranked player, who shows a "?" well
	DisplayString *title, *lead, *label, *figure, *tier, *status;
	DisplayString *factLabel[ CARD_FACTS ], *factValue[ CARD_FACTS ];
	Int facts;
	/// LAST 5: the label, the two letters, the results newest first and how many there are
	DisplayString *formLabel, *formWin, *formLoss;
	Bool formWon[ FORM_RESULTS ];
	Int formCount;
};
enum { CARD_AI, CARD_QUICK_MATCH, CARD_CUSTOM, CARD_COUNT };
static HubCard s_cards[ CARD_COUNT ];

/// the Custom games card's second figure and its first open games, from games.watch
static DisplayString *s_customLabel2 = NULL, *s_customFigure2 = NULL;
static DisplayString *s_openGame[ OPEN_GAME_ROWS ][ 3 ];		///< name, map, seats
static Int s_openGameRows = 0;
struct HubGame
{
	UnicodeString name;
	AsciiString map;
	Int players, maxPlayers;
};
static std::map<Int, HubGame> s_openGames;		///< by the server's game id, the open ones only
static Bool s_openGamesStale = TRUE;

/// a recent match's cells, left to right as the design's row
enum RecentCell { CELL_CHIP, CELL_MODE, CELL_OPPONENT, CELL_RATING, CELL_MAP, CELL_CHANGE, CELL_AGE, CELL_COUNT };
/// each head's left edge at 1080 from the box's inset; change and when are set from the right (CELL_RIGHT)
static const Real CELL_LEFT[ CELL_COUNT ] = { 0.0f, 146.0f, 312.0f, 0.0f, 596.0f, 822.0f, 0.0f };
static const Real CHANGE_RIGHT = 150.0f;	///< the change's right edge, this far in from the box's inner right
static DisplayString *s_recent[ RECENT_ROWS ][ CELL_COUNT ];
static Bool s_recentWon[ RECENT_ROWS ];
static Real s_recentChange[ RECENT_ROWS ];
static Real s_recentBiggest = 0.0f;		///< the largest change shown, the bars' full length
static Int s_recentRows = 0;
static DisplayString *s_recentTitle = NULL;
static DisplayString *s_recentLine = NULL;		///< no matches, or no answer
static DisplayString *s_recentHead[ CELL_COUNT ];
static DisplayString *s_motdTitle = NULL;
static DisplayString *s_serverHead = NULL;
static DisplayString *s_serverFact[ 2 ][ 2 ];	///< Online now and In Quick Match: label, value
static GameWindow *s_cardRecent = NULL;
static GameWindow *s_cardMOTD = NULL;
static UnsignedInt s_cardsSerial = 0;
static Int s_cardsCounts[ 3 ];
static Int s_cardsLock = 0;					///< the Quick Match lock's seconds the card last showed
static UnsignedInt s_recentSerial[ 2 ];	///< OnlineGetHistorySerial for Quick Match and AI as the rows were last built
static Bool s_recentStale = TRUE;				///< new strings: build the rows whatever the serial says

/* A new player has no history: the recent box shows the ladder's top five instead (the Ladder tab's
	 ladder answer, asked here when both histories came back empty). */
static Bool s_ladderShown = FALSE;
static Bool s_ladderAsked = FALSE;
static DisplayString *s_ladderHead = NULL;
static DisplayString *s_ladderColumn[ 4 ];			///< #, PLAYER, ELO, W–L
static DisplayString *s_ladder[ LADDER_ROWS ][ 4 ];
static const Image *s_ladderPortrait[ LADDER_ROWS ];
static Real s_ladderElo[ LADDER_ROWS ];
static Int s_ladderRows = 0;
static DisplayString *s_wellMark = NULL;			///< the "?" of every well

/* A match the player dropped out of that the others still play (the sign-in's match.rejoinable): the
	 recent matches' card shows it instead, its players in two columns, with Rejoin at its foot. */
enum { REJOIN_ROWS = 8, REJOIN_COLUMN_ROWS = 4 };
static GameWindow *buttonRejoin = NULL;
static NameKeyType buttonRejoinID = NAMEKEY_INVALID;
static Bool s_rejoinShown = FALSE;				///< the card is the match still running
static Bool s_rejoinAsked = FALSE;				///< match.rejoin is out
static Bool s_rejoinStale = TRUE;				///< new strings: build the rows whatever the serial says
static UnsignedInt s_rejoinSerial = 0;
static Int s_rejoinSecond = -1;					///< the elapsed seconds the line last showed
static UnicodeString s_rejoinHead;				///< mode and map, before the elapsed time
static DisplayString *s_rejoinTitle = NULL;
static DisplayString *s_rejoinLine = NULL;		///< mode, map, how long it has run
static DisplayString *s_rejoinNote = NULL;		///< a wait or a refusal, empty otherwise
static Color s_rejoinNoteColor = 0;
static DisplayString *s_rejoinChip[ REJOIN_ROWS ];
static DisplayString *s_rejoinNick[ REJOIN_ROWS ];
static DisplayString *s_rejoinSide[ REJOIN_ROWS ];
static Bool s_rejoinPresent[ REJOIN_ROWS ];
static Bool s_rejoinLocal[ REJOIN_ROWS ];		///< the seat is the local player's, his nick gold
static Bool s_playLit = TRUE;								///< PLAY is the lit key; REJOIN is while a match is offered
static Int s_rejoinRows = 0;

static std::vector<DisplayString *> s_strings;		///< every string the cards made, freed together

static DisplayString *hubString( GameFont *font )
{
	DisplayString *text = TheDisplayStringManager->newDisplayString();
	text->setFont( font );
	s_strings.push_back( text );
	return text;
}

static void freeHubStrings( void )
{
	for (size_t i = 0; i < s_strings.size(); ++i)
		TheDisplayStringManager->freeDisplayString( s_strings[ i ] );
	s_strings.clear();
	memset( s_cards, 0, sizeof( s_cards ) );
	memset( s_recent, 0, sizeof( s_recent ) );
	memset( s_recentHead, 0, sizeof( s_recentHead ) );
	memset( s_openGame, 0, sizeof( s_openGame ) );
	memset( s_serverFact, 0, sizeof( s_serverFact ) );
	memset( s_ladder, 0, sizeof( s_ladder ) );
	memset( s_ladderColumn, 0, sizeof( s_ladderColumn ) );
	s_recentTitle = s_recentLine = s_motdTitle = s_serverHead = s_ladderHead = s_wellMark = NULL;
	s_customLabel2 = s_customFigure2 = NULL;
	s_recentRows = s_openGameRows = s_ladderRows = 0;
	memset( s_rejoinChip, 0, sizeof( s_rejoinChip ) );
	memset( s_rejoinNick, 0, sizeof( s_rejoinNick ) );
	memset( s_rejoinSide, 0, sizeof( s_rejoinSide ) );
	s_rejoinTitle = s_rejoinLine = s_rejoinNote = NULL;
	s_rejoinRows = 0;
}

/// the five sizes at 1080 (UIStyle::MENU_TEXT_*): body 18, a box's name and a row's figure 24, a key
/// figure 40, the 14 px bold capital labels
static GameFont *bodyFont( void )			{ return UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ); }
static GameFont *headingFont( void )	{ return UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE ); }
static GameFont *figureFont( void )		{ return UIStyle::menuFont( UIStyle::MENU_TEXT_TEAM_FIGURE ); }
static GameFont *labelFont( void )		{ return UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE ); }
static Int hpx( Real at1080 )					{ return UIStyle::px( at1080 ); }

/// a label in capitals, fitted
static UnicodeString capsKey( const char *key, Int widest )
{
	return UIStyle::fitText( UIStyle::capitals( TheGameText->fetch( key ) ), labelFont(), widest );
}

/// words with their left edge (or right edge, right) at x, centred on middle; their width
static Int drawAt( DisplayString *text, Int x, Int middle, Color color, Bool right = FALSE )
{
	Int w, h;
	text->getSize( &w, &h );
	text->draw( right ? x - w : x, middle - h / 2, color, UIStyle::SHADOW );
	return w;
}

/// the box's name, 24, with the capitals after it 16 apart (UNRANKED, RANKED · 1V1); its bottom
static void drawBoxName( DisplayString *name, DisplayString *lead, Int x, Int y )
{
	const Int middle = y + hpx( 33.0f );
	const Int w = drawAt( name, x, middle, UIStyle::INK );
	if (lead && lead->getTextLength())
		drawAt( lead, x + w + hpx( 16.0f ), middle, UIStyle::MUTED );
}

/// a "?" well where a rank portrait would stand: an unranked player
static void drawWell( Int x, Int y, Int size )
{
	TheDisplay->drawFillRect( x, y, size, size, GameMakeColor( 10, 12, 20, 230 ) );
	TheDisplay->drawOpenRect( x, y, size, size, 1.0f, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
	if (s_wellMark == NULL)
		return;
	GameFont *font = size >= hpx( 80.0f ) ? figureFont() : headingFont();
	if (s_wellMark->getFont() != font)
		s_wellMark->setFont( font );
	Int w, h;
	s_wellMark->getSize( &w, &h );
	s_wellMark->draw( x + ( size - w ) / 2, y + ( size - h ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
}

/// a rating's change as the row's figure, white up and MUTED down, a quiet bar of its size before it
static void drawChange( DisplayString *figure, Real change, Real biggest, Int right, Int middle )
{
	const Int w = drawAt( figure, right, middle, change > 0.0f ? UIStyle::INK : UIStyle::MUTED, TRUE );
	const Int bar = hpx( 110.0f ), barRight = right - w - hpx( 14.0f );
	TheDisplay->drawFillRect( barRight - bar, middle - 1, bar, max( 1, hpx( 2.0f ) ), UIStyle::withAlpha( UIStyle::FRAME, 70 ) );
	const Int length = biggest > 0.0f ? REAL_TO_INT( bar * fabs( change ) / biggest + 0.5f ) : 0;
	if (length > 0)
		TheDisplay->drawFillRect( barRight - length, middle - hpx( 2.0f ), length, max( 2, hpx( 4.0f ) ),
			change > 0.0f ? UIStyle::EDGE : UIStyle::FRAME );
}

/** A card's words.  title is its name (24), lead the capitals after it, label the figure's (14 px capitals
	* over the 40 px figure); facts are rows of 40 at the right; a mode card has the LAST 5 row. */
static void makeCard( HubCard &card, const char *window, const char *title, const char *lead,
	const char *label, Bool portraitColumn, Int facts, const char *const factKeys[] )
{
	card.window = hubWindow( window );
	card.portraitColumn = portraitColumn;
	card.portrait = NULL;
	Int w = 0, h = 0;
	if (card.window)
		card.window->winGetSize( &w, &h );
	GameFont *body = bodyFont();
	const Int inner = w - hpx( 48.0f );
	card.title = hubString( headingFont() );
	card.title->setText( UIStyle::fitKey( title, headingFont(), inner / 2 ) );
	Int titleWidth, titleHeight;
	card.title->getSize( &titleWidth, &titleHeight );
	card.lead = hubString( labelFont() );
	card.lead->setText( capsKey( lead, inner - titleWidth - hpx( 16.0f ) ) );
	card.label = hubString( labelFont() );
	card.label->setText( label ? capsKey( label, hpx( 200.0f ) ) : UnicodeString::TheEmptyString );
	card.figure = hubString( figureFont() );
	card.tier = hubString( body );
	card.status = hubString( body );
	card.facts = facts;
	for (Int i = 0; i < facts; ++i)
	{
		card.factLabel[ i ] = hubString( body );
		card.factLabel[ i ]->setText( UIStyle::fitKey( factKeys[ i ], body, hpx( FACT_WIDTH / 2.0f ) ) );
		card.factValue[ i ] = hubString( body );
	}
	card.formLabel = hubString( labelFont() );
	card.formLabel->setText( capsKey( "GUI:OnlineHubLast5", hpx( 90.0f ) ) );
	card.formWin = hubString( labelFont() );
	card.formWin->setText( UIStyle::capitals( TheGameText->fetch( "GUI:OnlineWinLetter" ) ) );
	card.formLoss = hubString( labelFont() );
	card.formLoss->setText( UIStyle::capitals( TheGameText->fetch( "GUI:OnlineLossLetter" ) ) );
	card.formCount = 0;
}

/** A mode: its name and what it is, the key figure with the tier's portrait before it (a "?" well while
	* unranked), the facts at the right, the last five results under the figure, the status (a refusal, the
	* lock) over the key the master stands across its foot. */
static void drawModeCard( const HubCard &card, Int x, Int y, Int w, Int h )
{
	const Int left = x + hpx( 24.0f ), right = x + w - hpx( 24.0f );
	drawBoxName( card.title, card.lead, left, y );

	Int figureX = left;
	if (card.portraitColumn)
	{
		const Int top = y + hpx( 70.0f ), size = hpx( 112.0f );
		if (card.portrait)
			TheDisplay->drawImage( card.portrait, left, top, left + size, top + size );
		else
			drawWell( left, top, size );
		figureX = left + hpx( 132.0f );
	}
	card.label->draw( figureX, y + hpx( 70.0f ), UIStyle::MUTED, UIStyle::SHADOW );
	card.figure->draw( figureX, y + hpx( 92.0f ), UIStyle::INK, UIStyle::SHADOW );
	drawAt( card.tier, figureX, y + hpx( 158.0f ), UIStyle::INK );

	const Int factsX = right - hpx( FACT_WIDTH );
	for (Int i = 0; i < card.facts; ++i)
	{
		const Int middle = y + hpx( 88.0f + 40.0f * i );
		drawAt( card.factLabel[ i ], factsX, middle, UIStyle::MUTED );
		if (card.factValue[ i ]->getTextLength())
			drawAt( card.factValue[ i ], right, middle, UIStyle::INK, TRUE );
	}

	if (card.formCount > 0)
	{
		const Int middle = y + hpx( 217.0f );
		Int cx = left + drawAt( card.formLabel, left, middle, UIStyle::MUTED ) + hpx( 16.0f );
		for (Int i = 0; i < card.formCount; ++i)
			cx += UIStyle::onlineChip( card.formWon[ i ] ? card.formWin : card.formLoss, cx, middle, card.formWon[ i ] ) + hpx( 6.0f );
	}
	if (card.status->getTextLength())
		drawAt( card.status, left, y + h - hpx( 98.0f ), UIStyle::INK );
}

/// Custom games: the two figures side by side, then the first open games (name, map, seats)
static void drawCustomCard( const HubCard &card, Int x, Int y, Int w )
{
	const Int left = x + hpx( 24.0f ), right = x + w - hpx( 24.0f );
	drawBoxName( card.title, card.lead, left, y );
	card.label->draw( left, y + hpx( 70.0f ), UIStyle::MUTED, UIStyle::SHADOW );
	card.figure->draw( left, y + hpx( 92.0f ), UIStyle::INK, UIStyle::SHADOW );
	s_customLabel2->draw( left + hpx( 192.0f ), y + hpx( 70.0f ), UIStyle::MUTED, UIStyle::SHADOW );
	s_customFigure2->draw( left + hpx( 192.0f ), y + hpx( 92.0f ), UIStyle::INK, UIStyle::SHADOW );
	const Int pitch = hpx( 34.0f );
	for (Int r = 0; r < s_openGameRows; ++r)
	{
		const Int top = y + hpx( 160.0f ) + r * pitch, middle = top + pitch / 2;
		if (r % 2 == 0)
			TheDisplay->drawFillRect( left - hpx( 8.0f ), top, right - left + hpx( 16.0f ), pitch, GameMakeColor( 255, 255, 255, 7 ) );
		drawAt( s_openGame[ r ][ 0 ], left, middle, UIStyle::INK );
		drawAt( s_openGame[ r ][ 1 ], left + hpx( 186.0f ), middle, UIStyle::MUTED );
		drawAt( s_openGame[ r ][ 2 ], right, middle, UIStyle::INK, TRUE );
	}
}

/// a head of the recent table or the ladder: 14 px capitals, MUTED, from its left or to its right edge
static void drawHead( DisplayString *head, Int x, Int middle, Bool right = FALSE )
{
	drawAt( head, x, middle, UIStyle::MUTED, right );
}

/** The recent matches: heads over one lit rule, five rows 54 high with a faint zebra, the result's chip,
	* the mode, the opponent and his rating, the map, the change as the row's figure with its bar, when. */
static void drawRecent( Int x, Int y, Int w )
{
	const Int left = x + hpx( 24.0f ), right = x + w - hpx( 24.0f );
	drawBoxName( s_recentTitle, NULL, left, y );
	const Int headMiddle = y + hpx( 73.0f );
	if (s_recentRows == 0)
	{
		drawAt( s_recentLine, left, headMiddle, UIStyle::MUTED );
		return;
	}
	const Int changeRight = right - hpx( CHANGE_RIGHT );
	for (Int c = 0; c < CELL_COUNT; ++c)
	{
		if (c == CELL_RATING)
			continue;
		if (c == CELL_CHANGE)
			drawHead( s_recentHead[ c ], changeRight, headMiddle, TRUE );
		else if (c == CELL_AGE)
			drawHead( s_recentHead[ c ], right, headMiddle, TRUE );
		else
			drawHead( s_recentHead[ c ], left + hpx( CELL_LEFT[ c ] ), headMiddle );
	}
	UIStyle::litLine( left, y + hpx( 88.0f ), right - left, 1, UIStyle::FRAME );
	const Int pitch = hpx( 54.0f );
	for (Int r = 0; r < s_recentRows; ++r)
	{
		DisplayString **cell = s_recent[ r ];
		const Int top = y + hpx( 94.0f ) + r * pitch, middle = top + pitch / 2;
		if (r % 2)
			TheDisplay->drawFillRect( left - hpx( 8.0f ), top, right - left + hpx( 16.0f ), pitch, GameMakeColor( 255, 255, 255, 7 ) );
		UIStyle::onlineChip( cell[ CELL_CHIP ], left, middle, s_recentWon[ r ] );
		drawAt( cell[ CELL_MODE ], left + hpx( CELL_LEFT[ CELL_MODE ] ), middle, UIStyle::MUTED );
		const Int opponent = left + hpx( CELL_LEFT[ CELL_OPPONENT ] );
		const Int ow = drawAt( cell[ CELL_OPPONENT ], opponent, middle, UIStyle::INK );
		drawAt( cell[ CELL_RATING ], opponent + ow + hpx( 10.0f ), middle, UIStyle::MUTED );
		drawAt( cell[ CELL_MAP ], left + hpx( CELL_LEFT[ CELL_MAP ] ), middle, UIStyle::INK );
		drawChange( cell[ CELL_CHANGE ], s_recentChange[ r ], s_recentBiggest, changeRight, middle );
		drawAt( cell[ CELL_AGE ], right, middle, UIStyle::MUTED, TRUE );
	}
}

/** A new player: one line says there is no history yet, and the ladder's top five take the box under
	* LADDER · TOP 5: place, rank portrait, name, ELO with its bar, W–L. */
static void drawLadderTop( Int x, Int y, Int w )
{
	const Int left = x + hpx( 24.0f ), right = x + w - hpx( 24.0f );
	Int titleWidth, titleHeight;
	s_recentTitle->getSize( &titleWidth, &titleHeight );
	drawBoxName( s_recentTitle, NULL, left, y );
	drawAt( s_recentLine, left + titleWidth + hpx( 20.0f ), y + hpx( 35.0f ), UIStyle::MUTED );
	const Int top = UIStyle::onlineSection( s_ladderHead, left, right, y + hpx( 74.0f ) );
	const Int headMiddle = top + hpx( 17.0f );
	drawHead( s_ladderColumn[ 0 ], left, headMiddle );
	drawHead( s_ladderColumn[ 1 ], left + hpx( 104.0f ), headMiddle );
	drawHead( s_ladderColumn[ 2 ], left + hpx( 900.0f ), headMiddle, TRUE );
	drawHead( s_ladderColumn[ 3 ], right, headMiddle, TRUE );
	const Int pitch = hpx( 52.0f );
	const Real best = s_ladderRows > 0 ? s_ladderElo[ 0 ] : 0.0f;
	for (Int r = 0; r < s_ladderRows; ++r)
	{
		const Int rowTop = top + hpx( 34.0f ) + r * pitch, middle = rowTop + pitch / 2;
		if (r % 2)
			TheDisplay->drawFillRect( left - hpx( 8.0f ), rowTop, right - left + hpx( 16.0f ), pitch, GameMakeColor( 255, 255, 255, 7 ) );
		drawAt( s_ladder[ r ][ 0 ], left, middle, UIStyle::MUTED );
		const Int size = hpx( 40.0f );
		if (s_ladderPortrait[ r ])
			TheDisplay->drawImage( s_ladderPortrait[ r ], left + hpx( 52.0f ), middle - size / 2, left + hpx( 52.0f ) + size,
				middle - size / 2 + size );
		drawAt( s_ladder[ r ][ 1 ], left + hpx( 104.0f ), middle, UIStyle::INK );
		const Int ew = drawAt( s_ladder[ r ][ 2 ], left + hpx( 900.0f ), middle, UIStyle::INK, TRUE );
		// the bar: the rating's share of the leader's over the site's starting 1000
		const Int bar = hpx( 200.0f ), barRight = left + hpx( 900.0f ) - ew - hpx( 14.0f );
		TheDisplay->drawFillRect( barRight - bar, middle - 1, bar, max( 1, hpx( 2.0f ) ), UIStyle::withAlpha( UIStyle::FRAME, 70 ) );
		const Real share = best > 1000.0f ? ( s_ladderElo[ r ] - 1000.0f ) / ( best - 1000.0f ) : 0.0f;
		const Int length = REAL_TO_INT( bar * max( 0.0f, min( 1.0f, share ) ) + 0.5f );
		if (length > 0)
			TheDisplay->drawFillRect( barRight - length, middle - hpx( 2.0f ), length, max( 2, hpx( 4.0f ) ), UIStyle::EDGE );
		drawAt( s_ladder[ r ][ 3 ], right, middle, UIStyle::INK, TRUE );
	}
}

/// the left of the card that Rejoin leaves free: the key's 300 and its 24 inset, and 24 more between
static Int rejoinRoom( Int w )
{
	return w - hpx( 24.0f + 300.0f + 24.0f + 24.0f );
}

/** The match still running, in the recent matches' place: its name, mode and map with how long it has
	* run, a lit rule, the seats in two columns of four with a chip saying who plays on (the local player
	* gold), the note over REJOIN at the foot. */
static void drawRejoin( Int x, Int y, Int w, Int h )
{
	const Int left = x + hpx( 24.0f );
	drawBoxName( s_rejoinTitle, NULL, left, y );
	drawAt( s_rejoinLine, left, y + hpx( 70.0f ), UIStyle::MUTED );
	UIStyle::litLine( left, y + hpx( 96.0f ), w - hpx( 48.0f ), 1, UIStyle::FRAME );
	const Int pitch = hpx( 60.0f );
	const Int columnWidth = rejoinRoom( w ) / 2;
	const AsciiString me = TheGameSpyInfo ? TheGameSpyInfo->getLocalName() : AsciiString::TheEmptyString;
	for (Int r = 0; r < s_rejoinRows; ++r)
	{
		const Int cellX = left + ( r / REJOIN_COLUMN_ROWS ) * columnWidth;
		const Int middle = y + hpx( 136.0f ) + ( r % REJOIN_COLUMN_ROWS ) * pitch;
		const Int chipWidth = UIStyle::onlineChip( s_rejoinChip[ r ], cellX, middle, s_rejoinPresent[ r ] );
		const Int nickX = cellX + chipWidth + hpx( 14.0f );
		const Color nick = s_rejoinLocal[ r ] ? UIStyle::GOLD : s_rejoinPresent[ r ] ? UIStyle::INK : UIStyle::MUTED;
		const Int nw = drawAt( s_rejoinNick[ r ], nickX, middle, nick );
		drawAt( s_rejoinSide[ r ], nickX + nw + hpx( 10.0f ), middle, UIStyle::MUTED );
	}
	drawAt( s_rejoinNote, left, y + h - hpx( 50.0f ), s_rejoinNoteColor );
}

/// the server box: its name, the message of the day (the master's InfoListbox), the server's facts
static void drawServerBox( Int x, Int y, Int w, Int h )
{
	const Int left = x + hpx( 24.0f ), right = x + w - hpx( 24.0f );
	drawBoxName( s_motdTitle, NULL, left, y );
	s_serverHead->draw( left, y + h - hpx( 150.0f ), UIStyle::MUTED, UIStyle::SHADOW );
	for (Int i = 0; i < 2; ++i)
	{
		const Int middle = y + h - hpx( 108.0f ) + i * hpx( 36.0f );
		drawAt( s_serverFact[ i ][ 0 ], left, middle, UIStyle::MUTED );
		drawAt( s_serverFact[ i ][ 1 ], right, middle, UIStyle::INK, TRUE );
	}
}

/// every box's draw: the box, then what it holds
static void drawHubCard( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	IRegion2D box;
	box.lo.x = x;
	box.lo.y = y;
	box.hi.x = x + w;
	box.hi.y = y + h;
	UIStyle::onlineBox( box );
	if (s_strings.empty())
		return;
	if (window == s_cards[ CARD_CUSTOM ].window)
		drawCustomCard( s_cards[ CARD_CUSTOM ], x, y, w );
	else if (window == s_cards[ CARD_AI ].window || window == s_cards[ CARD_QUICK_MATCH ].window)
		drawModeCard( s_cards[ window == s_cards[ CARD_AI ].window ? CARD_AI : CARD_QUICK_MATCH ], x, y, w, h );
	else if (window == s_cardRecent && s_rejoinShown)
		drawRejoin( x, y, w, h );
	else if (window == s_cardRecent && s_ladderShown)
		drawLadderTop( x, y, w );
	else if (window == s_cardRecent)
		drawRecent( x, y, w );
	else if (window == s_cardMOTD)
		drawServerBox( x, y, w, h );
}

/// the page's panel, before everything on it: the online head, frame A and the foot's rule
static void drawHubHeader( GameWindow *window, WinInstanceData *instData )
{
	UIStyle::onlineHeader( UnicodeString::TheEmptyString );
}

static void makeCards( void )
{
	freeHubStrings();
	// the fourth, Team, shows only on a server with Team vs AI (refreshCards sets the count)
	static const char *const aiFacts[] = { "GUI:OnlineGamesLabel", "GUI:OnlineWinRateLabel", "GUI:OnlineSpeedLabel", "GUI:OnlineHubTeamLabel" };
	static const char *const qmFacts[] = { "GUI:OnlinePlaceLabel", "GUI:OnlineHubRecord", "GUI:OnlineHubInQueue" };
	makeCard( s_cards[ CARD_AI ], "CardPlayVsAI", "GUI:OnlineHubVsAITitle", "GUI:OnlineHubVsAILead",
		"GUI:OnlineAIRatingLabel", TRUE, 4, aiFacts );
	makeCard( s_cards[ CARD_QUICK_MATCH ], "CardQuickMatch", "GUI:OnlineQuickMatchLabel",
		"GUI:OnlineHubQuickMatchLead", "GUI:OnlineLadderRatingLabel", TRUE, 3, qmFacts );
	// the Speed fact's value is the master's ComboBoxGameSpeed, over its place
	// Custom games: open games and players online as two figures, then the first open games
	makeCard( s_cards[ CARD_CUSTOM ], "CardCustom", "GUI:OnlineHubCustomTitle", "GUI:OnlineHubCustomLead",
		"GUI:OnlineOpenGamesLabel", FALSE, 0, NULL );
	s_customLabel2 = hubString( labelFont() );
	s_customLabel2->setText( capsKey( "GUI:OnlineLoginOnlineNow", hpx( 180.0f ) ) );
	s_customFigure2 = hubString( figureFont() );
	for (Int r = 0; r < OPEN_GAME_ROWS; ++r)
		for (Int c = 0; c < 3; ++c)
			s_openGame[ r ][ c ] = hubString( bodyFont() );
	s_openGamesStale = TRUE;
	s_wellMark = hubString( figureFont() );
	s_wellMark->setText( UnicodeString( u"?" ) );

	s_cardRecent = hubWindow( "CardRecent" );
	s_cardMOTD = hubWindow( "CardMOTD" );
	Int w = 0, h = 0;
	if (s_cardRecent)
		s_cardRecent->winGetSize( &w, &h );
	s_recentTitle = hubString( headingFont() );
	s_recentTitle->setText( UIStyle::fitKey( "GUI:OnlineHubRecent", headingFont(), w / 3 ) );
	s_recentLine = hubString( bodyFont() );
	static const char *const heads[ CELL_COUNT ] = { "GUI:OnlineHeadResult", "GUI:OnlineHeadMode", "GUI:OnlineHeadOpponent", NULL,
		"GUI:OnlineHeadMap", "GUI:OnlineHeadChange", "GUI:OnlineHeadWhen" };
	for (Int c = 0; c < CELL_COUNT; ++c)
	{
		s_recentHead[ c ] = hubString( labelFont() );
		if (heads[ c ])
			s_recentHead[ c ]->setText( capsKey( heads[ c ], hpx( 150.0f ) ) );
	}
	for (Int r = 0; r < RECENT_ROWS; ++r)
		for (Int c = 0; c < CELL_COUNT; ++c)
			s_recent[ r ][ c ] = hubString( c == CELL_CHIP ? labelFont() : c == CELL_CHANGE ? headingFont() : bodyFont() );
	s_ladderHead = hubString( labelFont() );
	s_ladderHead->setText( capsKey( "GUI:OnlineHubLadderTop", hpx( 400.0f ) ) );
	static const char *const ladderHeads[ 4 ] = { NULL, "GUI:OnlineHeadPlayer", "GUI:OnlineLadderShort", "GUI:OnlineHubRecord" };
	for (Int c = 0; c < 4; ++c)
	{
		s_ladderColumn[ c ] = hubString( labelFont() );
		s_ladderColumn[ c ]->setText( ladderHeads[ c ] ? capsKey( ladderHeads[ c ], hpx( 200.0f ) ) : UnicodeString( u"#" ) );
	}
	for (Int r = 0; r < LADDER_ROWS; ++r)
		for (Int c = 0; c < 4; ++c)
			s_ladder[ r ][ c ] = hubString( c == 2 ? headingFont() : bodyFont() );
	s_ladderShown = s_ladderAsked = FALSE;
	s_rejoinTitle = hubString( headingFont() );
	s_rejoinTitle->setText( UIStyle::fitKey( "GUI:OnlineRejoinTitle", headingFont(), w - hpx( 48.0f ) ) );
	s_rejoinLine = hubString( bodyFont() );
	s_rejoinNote = hubString( bodyFont() );
	for (Int r = 0; r < REJOIN_ROWS; ++r)
	{
		s_rejoinChip[ r ] = hubString( labelFont() );
		s_rejoinNick[ r ] = hubString( bodyFont() );
		s_rejoinSide[ r ] = hubString( bodyFont() );
	}
	s_rejoinStale = TRUE;
	if (s_cardMOTD)
		s_cardMOTD->winGetSize( &w, &h );
	UnicodeString from;
	from.format( TheGameText->fetch( "GUI:OnlineHubFrom" ), LobbyServerLabel().str() );
	s_motdTitle = hubString( headingFont() );
	s_motdTitle->setText( UIStyle::fitText( from, headingFont(), w - hpx( 48.0f ) ) );
	s_serverHead = hubString( labelFont() );
	s_serverHead->setText( capsKey( "GUI:OnlineServerHead", w - hpx( 48.0f ) ) );
	static const char *const serverFacts[ 2 ] = { "GUI:OnlineLoginOnlineNow", "GUI:OnlineLoginInQuickMatch" };
	for (Int i = 0; i < 2; ++i)
	{
		s_serverFact[ i ][ 0 ] = hubString( bodyFont() );
		s_serverFact[ i ][ 0 ]->setText( UIStyle::fitKey( serverFacts[ i ], bodyFont(), ( w - hpx( 48.0f ) ) * 7 / 10 ) );
		s_serverFact[ i ][ 1 ] = hubString( bodyFont() );
	}

	GameWindow *boxes[] = { s_cards[ CARD_AI ].window, s_cards[ CARD_QUICK_MATCH ].window, s_cards[ CARD_CUSTOM ].window,
		s_cardRecent, s_cardMOTD };
	for (size_t i = 0; i < sizeof( boxes ) / sizeof( boxes[ 0 ] ); ++i)
		if (boxes[ i ])
			boxes[ i ]->winSetDrawFunc( drawHubCard );
	s_cardsDirty = TRUE;
	s_recentStale = TRUE;
}

/// a card's status line (a refusal, the Quick Match lock) over its key, empty for none; a sentence, so it
/// wraps to the card and is never abbreviated (two lines fit between LAST 5 and the key)
static void setCardStatus( HubCard &card, const UnicodeString &text, Int reserve1080 = 0 )
{
	Int w = 0, h = 0;
	if (card.window)
		card.window->winGetSize( &w, &h );
	card.status->setWordWrap( w - hpx( 48.0f + reserve1080 ) );
	card.status->setText( text );
}

/// the three cards' numbers, laid out again when the player's numbers, the counts or the army change
static void refreshCards( void )
{
	if (s_strings.empty() || TheGameSpyInfo == NULL)
		return;
	OnlinePlayerInfo info;
	OnlineGetPlayerInfo( TheGameSpyInfo->getLocalName(), info );
	Int counts[ 3 ];
	if (!OnlineGetCounts( counts[ 0 ], counts[ 1 ], counts[ 2 ] ))
		counts[ 0 ] = counts[ 1 ] = counts[ 2 ] = -1;
	const Int lock = OnlineGetQuickMatchLockSeconds();
	if (!s_cardsDirty && info.serial == s_cardsSerial && memcmp( counts, s_cardsCounts, sizeof( counts ) ) == 0
		&& lock == s_cardsLock)
		return;
	s_cardsDirty = FALSE;
	s_cardsSerial = info.serial;
	memcpy( s_cardsCounts, counts, sizeof( counts ) );
	s_cardsLock = lock;

	const UnicodeString none( u"\x2013" );		// an en dash for a number nobody sent
	const Int army = quickMatchArmy();
	const AsciiString side = army >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( army )->getBaseSide() : AsciiString( "Random" );
	UnicodeString text;

	HubCard &ai = s_cards[ CARD_AI ];
	const UIStyle::RatingTier aiTier = UIStyle::aiRatingTier( info.aiRating, info.hasAIRating ? info.aiGames : -1 );
	ai.portrait = UIStyle::tierImage( aiTier, side );
	ai.figure->setText( info.hasAIRating ? UIStyle::eloText( info.aiRating ) : none );
	ai.tier->setText( OnlineTierName( UIStyle::tierKey( aiTier ) ) );
	ai.factValue[ 0 ]->setText( info.hasAIRating ? UIStyle::grouped( info.aiGames ) : none );
	ai.factValue[ 1 ]->setText( info.hasAIRating && info.aiGames > 0
		? UIStyle::percentText( UIStyle::percent( info.aiWins, info.aiGames ) ) : none );
	// Team vs AI: the Team fact, and the search or the party on the status row while no refusal is up,
	// clear of LEAVE at its right
	ai.facts = OnlineTeamAIAvailable() ? 4 : 3;
	const Bool leaveShown = buttonLeaveParty && !buttonLeaveParty->winIsHidden();
	const UnicodeString &aiLine = !s_aiRefusal.isEmpty() ? s_aiRefusal : !s_aiWaitLine.isEmpty() ? s_aiWaitLine : s_teamLine;
	setCardStatus( ai, aiLine, leaveShown ? 126 : 0 );

	HubCard &qm = s_cards[ CARD_QUICK_MATCH ];
	const UIStyle::RatingTier tier = UIStyle::playerTier( info );
	qm.portrait = UIStyle::tierImage( tier, side );
	qm.figure->setText( info.hasRating ? UIStyle::ladderText( info.shown(), info.hasElo ) : none );
	qm.tier->setText( OnlineTierName( UIStyle::tierKey( tier ) ) );
	// under five rated games the ladder places nobody: how far along instead (-1 is a count nobody sent)
	if (info.hasRating && info.games >= 0 && info.games < UIStyle::RANKED_GAMES)
		text.format( TheGameText->fetch( "GUI:OnlineHubRankedGames" ), info.games, (Int)UIStyle::RANKED_GAMES );
	else if (info.hasStats && info.rank > 0)
		text.format( TheGameText->fetch( "GUI:OnlinePlaceValue" ), info.rank );
	else
		text = none;
	qm.factValue[ 0 ]->setText( UIStyle::fitText( text, bodyFont(), UIStyle::px( 120.0f ) ) );		// beside a short "Place"
	if (info.hasStats)
		text.format( u"%d\x2013%d", info.wins, info.losses );
	else
		text = none;
	qm.factValue[ 1 ]->setText( text );
	qm.factValue[ 2 ]->setText( counts[ 2 ] >= 0 ? UIStyle::grouped( counts[ 2 ] ) : none );
	// a player who left a ranked match waits out the server's lock: how long, in the lead's place, Find
	// match greyed meanwhile
	static const Int SECONDS_PER_MINUTE = 60;
	text.clear();
	if (lock > 0)
		text.format( TheGameText->fetch( "GUI:OnlineHubQuickMatchLocked" ), lock / SECONDS_PER_MINUTE, lock % SECONDS_PER_MINUTE );
	else if (inParty())
		text = TheGameText->fetch( "GUI:OnlineInParty" );		// Find match greyed: Quick Match is one player's
	setCardStatus( qm, text );

	HubCard &custom = s_cards[ CARD_CUSTOM ];
	custom.figure->setText( counts[ 1 ] >= 0 ? UIStyle::grouped( counts[ 1 ] ) : none );
	s_customFigure2->setText( counts[ 0 ] >= 0 ? UIStyle::grouped( counts[ 0 ] ) : none );
	setCardStatus( custom, UnicodeString::TheEmptyString );
	for (Int i = 0; i < 2; ++i)
		s_serverFact[ i ][ 1 ]->setText( counts[ i == 0 ? 0 : 2 ] >= 0 ? UIStyle::grouped( counts[ i == 0 ? 0 : 2 ] ) : none );
}

/** The open games games.watch sends (PEERRESPONSE_STAGINGROOM, as the lobby reads them): the hub keeps the
	* open ones by id, a started or full one going as it leaves the list.  FALSE for any other response. */
static Bool takeOpenGame( const PeerResponse &resp )
{
	if (resp.peerResponseType == PeerResponse::PEERRESPONSE_STAGINGROOMLISTCOMPLETE)
		return TRUE;
	if (resp.peerResponseType != PeerResponse::PEERRESPONSE_STAGINGROOM)
		return FALSE;
	s_openGamesStale = TRUE;
	const Int id = resp.stagingRoom.id;
	if (resp.stagingRoom.action == PEER_CLEAR)
		s_openGames.clear();
	else if (resp.stagingRoom.action == PEER_REMOVE || resp.stagingRoomMapName.empty() || !resp.stagingRoom.isStaging
		|| resp.stagingRoom.numPlayers >= resp.stagingRoom.maxPlayers)
		s_openGames.erase( id );
	else if (resp.stagingRoom.action == PEER_ADD || resp.stagingRoom.action == PEER_UPDATE)
	{
		HubGame &game = s_openGames[ id ];
		game.name = UnicodeString( resp.stagingServerName.c_str() );
		AsciiString map;
		for (const char *c = resp.stagingRoomMapName.c_str(); *c; ++c)
			map.concat( *c == '/' ? '\\' : *c );
		game.map = TheGameState ? TheGameState->portableMapPathToRealMapPath( map ) : map;
		game.players = resp.stagingRoom.numPlayers;
		game.maxPlayers = resp.stagingRoom.maxPlayers;
	}
	return TRUE;
}

/// a map's shown name: the map cache's, "Random" for a generated one, else its file's stem
static UnicodeString mapShownName( const AsciiString &path )
{
	AsciiString stem = path;
	const char *slash = strrchr( stem.str(), '\\' );
	stem = slash ? AsciiString( slash + 1 ) : stem;
	if (stem.startsWithNoCase( "rmg_" ))
		return TheGameText->fetch( "GUI:Random" );
	if (const MapMetaData *md = TheMapCache ? TheMapCache->findMap( path ) : NULL)
		return md->m_displayName;
	if (stem.endsWithNoCase( ".map" ))
		for (Int i = 0; i < 4; ++i)
			stem.removeLastChar();
	UnicodeString name;
	name.translate( stem );
	return name;
}

/// the Custom games card's rows: the first three open games by the server's order of ids
static void refreshOpenGames( void )
{
	if (s_strings.empty() || !s_openGamesStale)
		return;
	s_openGamesStale = FALSE;
	Int w = 0, h = 0;
	if (s_cards[ CARD_CUSTOM ].window)
		s_cards[ CARD_CUSTOM ].window->winGetSize( &w, &h );
	const Int inner = w - hpx( 48.0f );
	s_openGameRows = 0;
	for (std::map<Int, HubGame>::const_iterator it = s_openGames.begin(); it != s_openGames.end() && s_openGameRows < OPEN_GAME_ROWS; ++it)
	{
		DisplayString **row = s_openGame[ s_openGameRows++ ];
		UnicodeString seats;
		seats.format( u"%d/%d", it->second.players, it->second.maxPlayers );
		row[ 2 ]->setText( seats );
		row[ 0 ]->setText( UIStyle::fitName( it->second.name, bodyFont(), hpx( 176.0f ) ) );
		row[ 1 ]->setText( UIStyle::fitText( mapShownName( it->second.map ), bodyFont(), inner - hpx( 186.0f + 52.0f ) ) );
	}
}

/// LAST 5 on a mode card, newest first, from that mode's history
static void refreshForm( HubCard &card, OnlineHistoryMode mode )
{
	OnlineHistory history;
	OnlineGetHistory( 0, mode, history );
	card.formCount = history.arrived ? min( (Int)history.rows.size(), (Int)FORM_RESULTS ) : 0;
	for (Int i = 0; i < card.formCount; ++i)
		card.formWon[ i ] = history.rows[ i ].won;
}

/// how long ago, in the coarsest unit that says it
static UnicodeString ageText( Int seconds )
{
	UnicodeString text;
	if (seconds < 3600)
		text.format( TheGameText->fetch( "GUI:OnlineHubAgoMinutes" ), max( 1, seconds / 60 ) );
	else if (seconds < 86400)
		text.format( TheGameText->fetch( "GUI:OnlineHubAgoHours" ), seconds / 3600 );
	else if (seconds < 2 * 86400)
		text = TheGameText->fetch( "GUI:OnlineHubYday" );
	else
		text.format( TheGameText->fetch( "GUI:OnlineHubAgoDays" ), seconds / 86400 );
	return text;
}

/// the ladder's top five for a player with no history yet, asked once a visit; FALSE until it is in
static Bool refreshLadderTop( void )
{
	if (!s_ladderAsked)
	{
		s_ladderAsked = TRUE;
		OnlineRequestLadder( 0 );
	}
	Int offset = 0;
	std::vector<OnlineLadderRow> rows;
	OnlineLadderRow me;
	Bool hasMe = FALSE;
	if (OnlineGetLadder( offset, rows, me, hasMe ) != ONLINE_LADDER_READY || offset != 0)
		return FALSE;
	s_ladderRows = min( (Int)rows.size(), (Int)LADDER_ROWS );
	for (Int r = 0; r < s_ladderRows; ++r)
	{
		const OnlineLadderRow &row = rows[ r ];
		UnicodeString text;
		text.format( u"%d", row.rank > 0 ? row.rank : r + 1 );
		s_ladder[ r ][ 0 ]->setText( text );
		text.translate( row.nick );
		s_ladder[ r ][ 1 ]->setText( UIStyle::fitName( text, bodyFont(), hpx( 400.0f ) ) );
		s_ladder[ r ][ 2 ]->setText( row.hasRating ? UIStyle::ladderText( row.shown(), row.hasElo ) : UnicodeString( u"\x2013" ) );
		text.format( u"%d\x2013%d", row.wins, row.losses );
		s_ladder[ r ][ 3 ]->setText( text );
		s_ladderElo[ r ] = row.hasElo ? row.elo : 0.0f;
		OnlinePlayerInfo info;
		info.hasRating = row.hasRating;
		info.hasElo = row.hasElo;
		info.elo = row.elo;
		info.exactRating = row.exactRating;
		info.tier = row.tier;
		info.games = row.wins + row.losses;
		s_ladderPortrait[ r ] = UIStyle::tierImage( UIStyle::playerTier( info ), AsciiString( "Random" ) );
	}
	return TRUE;
}

/** The recent matches: each mode's last five (history asked per mode), merged newest first, five rows.
	* Built again only when either history's serial moves.  With both answered and empty the box shows the
	* ladder's top five instead. */
static void refreshRecent( void )
{
	if (s_strings.empty())
		return;
	const UnsignedInt serial[ 2 ] = { OnlineGetHistorySerial( 0, ONLINE_HISTORY_QM ), OnlineGetHistorySerial( 0, ONLINE_HISTORY_AI ) };
	if (s_ladderShown && s_ladderRows == 0)
		refreshLadderTop();
	if (!s_recentStale && serial[ 0 ] == s_recentSerial[ 0 ] && serial[ 1 ] == s_recentSerial[ 1 ])
		return;
	s_recentStale = FALSE;
	s_recentSerial[ 0 ] = serial[ 0 ];
	s_recentSerial[ 1 ] = serial[ 1 ];
	OnlineHistory qm, ai;
	const OnlineLadderState qmState = OnlineGetHistory( 0, ONLINE_HISTORY_QM, qm );
	const OnlineLadderState aiState = OnlineGetHistory( 0, ONLINE_HISTORY_AI, ai );
	refreshForm( s_cards[ CARD_QUICK_MATCH ], ONLINE_HISTORY_QM );
	refreshForm( s_cards[ CARD_AI ], ONLINE_HISTORY_AI );

	// newest first across both: the smaller age first, each list already newest first
	std::vector<const OnlineHistoryRow *> rows;
	size_t a = 0, b = 0;
	const size_t qmRows = qm.arrived ? qm.rows.size() : 0, aiRows = ai.arrived ? ai.rows.size() : 0;
	while (rows.size() < (size_t)RECENT_ROWS && (a < qmRows || b < aiRows))
	{
		if (b >= aiRows || (a < qmRows && qm.rows[ a ].age <= ai.rows[ b ].age))
			rows.push_back( &qm.rows[ a++ ] );
		else
			rows.push_back( &ai.rows[ b++ ] );
	}
	s_recentRows = (Int)rows.size();
	const Bool answered = qmState == ONLINE_LADDER_READY && aiState == ONLINE_LADDER_READY;
	s_ladderShown = answered && s_recentRows == 0;
	if (s_ladderShown)
	{
		s_recentLine->setText( TheGameText->fetch( "GUI:OnlineHubRecentNone" ) );
		refreshLadderTop();
	}
	else if ((qmState == ONLINE_LADDER_FAILED || aiState == ONLINE_LADDER_FAILED) && s_recentRows == 0)
		s_recentLine->setText( TheGameText->fetch( "GUI:OnlineHubRecentNoAnswer" ) );
	else
		s_recentLine->setText( UnicodeString::TheEmptyString );

	GameFont *body = bodyFont();
	s_recentBiggest = 0.0f;
	for (Int r = 0; r < s_recentRows; ++r)
	{
		const OnlineHistoryRow &row = *rows[ r ];
		DisplayString **cell = s_recent[ r ];
		s_recentWon[ r ] = row.won;
		s_recentChange[ r ] = row.change;
		s_recentBiggest = max( s_recentBiggest, (Real)fabs( row.change ) );
		cell[ CELL_CHIP ]->setText( UIStyle::capitals( TheGameText->fetch( row.won ? "GUI:OnlineWinLabel" : "GUI:OnlineLossLabel" ) ) );
		cell[ CELL_MODE ]->setText( UIStyle::fitKey( row.ai ? "GUI:OnlineHubMatchAI" : "GUI:OnlineQuickMatchLabel", body,
			hpx( CELL_LEFT[ CELL_OPPONENT ] - CELL_LEFT[ CELL_MODE ] - 16.0f ) ) );
		cell[ CELL_RATING ]->setText( UIStyle::ladderText( row.opponentRating, row.ai || row.hasElo ) );
		Int ratingWidth, h;
		cell[ CELL_RATING ]->getSize( &ratingWidth, &h );
		UnicodeString opponent;
		if (row.ai)
			opponent = TheGameText->fetch( "GUI:OnlineComputerLabel" );
		else
			opponent = UnicodeString( MultiByteToWideCharSingleLine( row.opponent.str() ).c_str() );
		cell[ CELL_OPPONENT ]->setText( UIStyle::fitName( opponent, body,
			hpx( CELL_LEFT[ CELL_MAP ] - CELL_LEFT[ CELL_OPPONENT ] - 26.0f ) - ratingWidth ) );
		// a ranked map is generated from the match seed and its name only says so ("rmg_v19_..."); an AI
		// match's map is one of the official ones, by its shown name
		UnicodeString map;
		if (row.map.startsWithNoCase( "rmg_" ))
			map = TheGameText->fetch( "GUI:Random" );
		else
			map = UnicodeString( MultiByteToWideCharSingleLine( row.map.str() ).c_str() );
		cell[ CELL_MAP ]->setText( UIStyle::fitText( map, body, hpx( 210.0f ), UnicodeString::TheEmptyString, map ) );
		cell[ CELL_CHANGE ]->setText( UIStyle::ladderText( row.change, row.hasElo, TRUE ) );
		cell[ CELL_AGE ]->setText( ageText( row.age ) );
	}
}

static void setRejoinNote( const UnicodeString &text, Color color )
{
	s_rejoinNoteColor = color;
	Int w = 0, h = 0;
	if (s_cardRecent)
		s_cardRecent->winGetSize( &w, &h );
	s_rejoinNote->setText( UIStyle::fitText( text, bodyFont(), rejoinRoom( w ) ) );
}

/// a match.refused code in words
static UnicodeString rejoinRefusalText( const AsciiString &code )
{
	if (code == "crc")
		return TheGameText->fetch( "GUI:OnlineRejoinRefusedCrc" );
	if (code == "norelay")
		return TheGameText->fetch( "GUI:OnlineRejoinRefusedRelay" );
	return TheGameText->fetch( "GUI:OnlineRejoinRefused" );
}

/// the match still running: the card's rows when the offer changes, its clock every second, Rejoin's
/// answer as it comes (the match goes to OnlineStartRejoin, a refusal onto the card or into a box when the
/// match went with it)
static void pressRejoin( void );

static void refreshRejoin( void )
{
	if (s_strings.empty())
		return;
	OnlineRejoinable offer;
	const Bool offered = OnlineGetRejoinable( offer );
	// -onlinerejoin: Rejoin pressed for a script, once, the moment the card offers it
	if (offered && TheGlobalData->m_onlineAutoRejoin && !s_rejoinAsked)
	{
		TheWritableGlobalData->m_onlineAutoRejoin = FALSE;
		DEBUG_LOG(("ONLINE REJOIN PRESS match=%s\n", offer.match.str()));
		pressRejoin();
	}
	if (s_rejoinStale || offer.serial != s_rejoinSerial)
	{
		s_rejoinStale = FALSE;
		s_rejoinSerial = offer.serial;
		s_rejoinSecond = -1;
		Int w = 0, h = 0;
		if (s_cardRecent)
			s_cardRecent->winGetSize( &w, &h );
		GameFont *body = bodyFont();
		UnicodeString map;
		if (offer.ranked || offer.map.isEmpty())
			map = TheGameText->fetch( "GUI:Random" );		// a Quick Match's map is made from its seed
		else if (const MapMetaData *md = TheMapCache ? TheMapCache->findMap( offer.map ) : NULL)
			map = md->m_displayName;
		else
		{
			AsciiString stem = offer.map;
			const char *slash = strrchr( stem.str(), '\\' );
			stem = slash ? AsciiString( slash + 1 ) : stem;
			if (stem.endsWithNoCase( ".map" ))
				for (Int i = 0; i < 4; ++i)
					stem.removeLastChar();
			map.translate( stem );
		}
		s_rejoinHead.format( u"%ls \x00B7 %ls \x00B7 ",
			TheGameText->fetch( offer.ranked ? "GUI:OnlineQuickMatchLabel" : "GUI:OnlineRejoinCustom" ).str(), map.str() );
		const Int column = rejoinRoom( w ) / 2;
		s_rejoinRows = min( (Int)offer.players.size(), (Int)REJOIN_ROWS );
		for (Int r = 0; r < s_rejoinRows; ++r)
		{
			const OnlineRejoinSeat &seat = offer.players[ r ];
			s_rejoinPresent[ r ] = seat.present;
			s_rejoinLocal[ r ] = TheGameSpyInfo && seat.nick.compareNoCase( TheGameSpyInfo->getLocalName() ) == 0;
			s_rejoinChip[ r ]->setText( UIStyle::capitals( TheGameText->fetch( seat.present ? "GUI:OnlineRejoinPlaying" : "GUI:OnlineRejoinAway" ) ) );
			Int chipWidth, chipHeight;
			s_rejoinChip[ r ]->getSize( &chipWidth, &chipHeight );
			const Int room = column - chipWidth - hpx( 18.0f + 14.0f + 10.0f + 16.0f );
			const PlayerTemplate *pt = seat.side >= 0 && ThePlayerTemplateStore ? ThePlayerTemplateStore->getNthPlayerTemplate( seat.side ) : NULL;
			const UnicodeString side = pt ? pt->getDisplayName() : TheGameText->fetch( "GUI:Random" );
			// the nick has the room the side's words leave, at most two thirds of it
			s_rejoinSide[ r ]->setText( UIStyle::fitText( side, body, room / 3 ) );
			Int sideWidth, sideHeight;
			s_rejoinSide[ r ]->getSize( &sideWidth, &sideHeight );
			s_rejoinNick[ r ]->setText( UIStyle::fitName( UnicodeString( MultiByteToWideCharSingleLine( seat.nick.str() ).c_str() ),
				body, room - sideWidth ) );
		}
		if (!s_rejoinAsked)
			setRejoinNote( UnicodeString::TheEmptyString, UIStyle::MUTED );
	}
	if (offered && offer.elapsedSeconds != s_rejoinSecond)
	{
		s_rejoinSecond = offer.elapsedSeconds;
		UnicodeString elapsed;
		elapsed.format( TheGameText->fetch( "GUI:OnlineRejoinElapsed" ), offer.elapsedSeconds / 60, offer.elapsedSeconds % 60 );
		Int w = 0, h = 0;
		if (s_cardRecent)
			s_cardRecent->winGetSize( &w, &h );
		UnicodeString line = s_rejoinHead;
		line.concat( elapsed );
		s_rejoinLine->setText( UIStyle::fitText( line, bodyFont(), w - UIStyle::px( 48.0f ) ) );
	}
	s_rejoinShown = offered;
	if (buttonRejoin)
	{
		buttonRejoin->winHide( !offered );
		buttonRejoin->winEnable( offered && !s_rejoinAsked && !s_joiningRoom && !s_aiAsked );
	}

	RejoinInfo info;
	AsciiString refusal;
	const OnlineRejoinState state = OnlineGetRejoin( info, refusal );
	if (state == ONLINE_REJOIN_READY)
	{
		s_rejoinAsked = FALSE;
		DEBUG_LOG(("ONLINE WELCOME rejoin=%s\n", info.match.str()));
		OnlineStartRejoin( info );
		// the match starts from its log, which a seat in the room sends once the room stands for this one
		if (TheNetwork && TheNetwork->isRejoining())
		{
			s_rejoinAsked = TRUE;
			setRejoinNote( TheGameText->fetch( "GUI:RejoinWaitingForLog" ), UIStyle::MUTED );
		}
	}
	else if (state == ONLINE_REJOIN_REFUSED)
	{
		s_rejoinAsked = FALSE;
		if (OnlineGetRejoinable( offer ))
			setRejoinNote( rejoinRefusalText( refusal ), UIStyle::INK );
		else
			GSMessageBoxOk( TheGameText->fetch( "GUI:GSErrorTitle" ), rejoinRefusalText( refusal ) );
	}
}

static void pressRejoin( void )
{
	if (s_rejoinAsked)
		return;
	s_rejoinAsked = TRUE;
	setRejoinNote( TheGameText->fetch( "GUI:OnlineRejoinWaiting" ), UIStyle::MUTED );
	if (buttonRejoin)
		buttonRejoin->winEnable( FALSE );
	OnlineRequestRejoin();
}

/// whether joinBestGroupRoom sends a join: no room held, and a room besides Quick Match's channel to
/// pick.  Otherwise it bails or raises its own failure box, and no answer ever comes.
// ponytail: mirrors the pick in PeerDefs.cpp joinBestGroupRoom; a Bool return from it would retire this copy
static Bool joinSendsRequest( void )
{
	if (TheGameSpyInfo->getCurrentGroupRoom() != 0)
		return FALSE;
	static const Int NO_ROOM_YET = 1000;		// EA's starting count, under which any room's waiting players fall
	static const Int SETTLED = 25;					// EA stops looking once a room this quiet is picked
	Int pick = -1;
	Int fewest = NO_ROOM_YET;
	const GroupRoomMap *rooms = TheGameSpyInfo->getGroupRoomList();
	for (GroupRoomMap::const_iterator it = rooms->begin(); it != rooms->end(); ++it)
	{
		const GameSpyGroupRoom &room = it->second;
		if (TheGameSpyConfig->getQMChannel() != room.m_groupID && fewest > SETTLED && room.m_numWaiting < fewest)
		{
			pick = room.m_groupID;
			fewest = room.m_numWaiting;
		}
	}
	return pick > 0;
}

/// Custom games joins a chat room first, as Browse games always did; the lobby goes up when the join
/// answers (WOLWelcomeMenuUpdate), and until then only Back and Sign out answer a click.  The Custom
/// tab waits for the room list as Browse games does, greyed.
static void joinRoom( void )
{
	if (!s_controlsLive)
		return;
	const Bool sends = joinSendsRequest();
	TheGameSpyInfo->joinBestGroupRoom();
	if (!sends)
		return;
	s_joiningRoom = TRUE;
	enableControls( FALSE );
}

/** A tab's way off the hub, and Find match's and Browse games': the hub stays at the stack's foot and
	* the page goes on top of it. */
static void hubLeave( UIStyle::OnlineTab to )
{
	if (to == UIStyle::ONLINE_TAB_CUSTOM)
	{
		joinRoom();
		return;
	}
	const char *screen = UIStyle::onlineTabScreen( to );
	if (screen == NULL)
		return;
	buttonPushed = TRUE;
	TheShell->push( AsciiString( screen ) );
}

/// Main Menu's and Sign out's goodbye to the service
static void logOut( void )
{
	if (TheGameSpyPeerMessageQueue)
	{
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_LOGOUT;
		TheGameSpyPeerMessageQueue->addRequest( req );
	}
	if (TheGameSpyBuddyMessageQueue)
	{
		BuddyRequest breq;
		breq.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGOUT;
		TheGameSpyBuddyMessageQueue->addRequest( breq );
	}

	DEBUG_LOG(("Tearing down GameSpy from WOLWelcomeMenuSystem(GBM_SELECTED)\n"));
	TearDownGameSpy();
}

/** Off the hub to the login screen: Sign out (forget drops the kept sign-in, so the login screen asks
	* for the password instead of signing straight back in), and a login that went while the hub stood
	* under another page or on its own (the kept sign-in stays).  The login screen needs the queues it
	* found when the main menu opened it. */
static void leaveForLogin( Bool forget )
{
	DEBUG_LOG(("ONLINE HUB to=login forget=%d\n", (Int)forget));
	if (forget && TheGameSpyInfo)
		OnlineSaveToken( TheGameSpyInfo->getLocalEmail(), AsciiString::TheEmptyString );
	logOut();
	SetUpGameSpy( OnlineGetMOTD().str(), OnlineGetConfig().str() );
	buttonPushed = TRUE;
	nextScreen = "Menus/GameSpyLoginProfile.wnd";
	TheShell->pop();
}

static Bool raiseMessageBoxes = FALSE;
//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Welcome Menu */
//-------------------------------------------------------------------------------------------------
void WOLWelcomeMenuInit( WindowLayout *layout, void *userData )
{
	nextScreen = NULL;
	buttonPushed = FALSE;
	isShuttingDown = FALSE;
	s_joiningRoom = FALSE;

	parentWOLWelcome = hubWindow( "WOLWelcomeMenuParent", &parentWOLWelcomeID );
	buttonBack = hubWindow( "ButtonBack", &buttonBackID );
	buttonQuickMatch = hubWindow( "ButtonQuickMatch", &buttonQuickMatchID );
	buttonLobby = hubWindow( "ButtonCustomMatch", &buttonLobbyID );
	hubWindow( "ButtonMyInfo", &buttonMyInfoID );
	buttonPlayAI = hubWindow( "ButtonPlayVsAI", &buttonPlayAIID );
	GameWindow *buttonSignOut = hubWindow( "ButtonSignOut", &buttonSignOutID );
	listboxInfo = hubWindow( "InfoListbox" );
	// shown while the server offers a match still running (refreshRejoin)
	buttonRejoin = hubWindow( "ButtonRejoin", &buttonRejoinID );
	if (buttonRejoin)
	{
		buttonRejoin->winSetEnabledTextColors( UIStyle::ACTIVE, UIStyle::SHADOW );
		buttonRejoin->winHide( TRUE );
	}
	s_rejoinShown = FALSE;
	s_rejoinAsked = FALSE;

	// the master's frame: the tabs with Home open, the head on the panel before the cards, the cards behind
	// the keys, every key and the speed combo in the language
	UIStyle::onlinePageInit( parentWOLWelcome, "WOLWelcomeMenu.wnd", UIStyle::ONLINE_TAB_HOME );
	makeCards();
	if (GameWindow *panel = hubWindow( "GadgetParent" ))
		panel->winSetDrawFunc( drawHubHeader );
	static const char *const keys[] = { "ButtonBack", "ButtonMyInfo", "ButtonSignOut", "ButtonQuickMatch", "ButtonCustomMatch" };
	for (size_t i = 0; i < sizeof( keys ) / sizeof( keys[ 0 ] ); ++i)
	{
		if (GameWindow *key = hubWindow( keys[ i ] ))
		{
			UIStyle::setMenuKey( key, FALSE );
			GadgetButtonSetText( key, UIStyle::capitals( key->winGetText() ) );
		}
	}
	s_playLit = TRUE;
	UIStyle::setMenuKey( buttonPlayAI, TRUE );
	UIStyle::setMenuKey( buttonRejoin, FALSE );
	if (buttonPlayAI)
		GadgetButtonSetText( buttonPlayAI, UIStyle::capitals( buttonPlayAI->winGetText() ) );
	if (buttonRejoin)
		GadgetButtonSetText( buttonRejoin, UIStyle::capitals( buttonRejoin->winGetText() ) );
	// Team vs AI's Team key and LEAVE, hidden until a server with it and a party show them (updateTeam)
	makeTeamKeys();
	// the recent matches are drawn on their card: a chip and a coloured change are more than a
	// one-column listbox's text
	if (GameWindow *recent = hubWindow( "ListboxRecent" ))
		recent->winHide( TRUE );
	// the message of the day stands on its card, not in a box of its own, in the cards' body face (the
	// master's fitted size falls to 10 px at 1280x720 and 4:3)
	if (listboxInfo)
	{
		listboxInfo->winSetFont( bodyFont() );
		const Color none = WIN_COLOR_UNDEFINED;
		listboxInfo->winSetEnabledColor( 0, none );
		listboxInfo->winSetEnabledBorderColor( 0, none );
		listboxInfo->winSetHiliteColor( 0, none );
		listboxInfo->winSetHiliteBorderColor( 0, none );
	}
	// Play vs AI and Sign out belong to the -online service
	if (buttonPlayAI)
		buttonPlayAI->winHide( !OnlineIsEnabled() );
	// Play vs AI's speed in the value's place of its card's Speed fact
	comboBoxAISpeed = hubWindow( "ComboBoxGameSpeed" );
	if (comboBoxAISpeed)
	{
		PopulatePlayerGameSpeedComboBox( comboBoxAISpeed, aiGameSpeed() );
		comboBoxAISpeed->winHide( !OnlineIsEnabled() );
		UIStyle::setMenuComboBox( comboBoxAISpeed );
	}
	if (buttonSignOut)
		buttonSignOut->winHide( !OnlineIsEnabled() );
	s_aiRefusal.clear();

	Bool serviceOnline = FALSE;
#ifdef ZH_ONLINE
	serviceOnline = OnlineBridge_IsConfigured();
#endif
	if (serviceOnline)
	{
		// lobbies and custom games only, and the service's connections need no firewall detection
		static const char *const theirs[] = { "ButtonQuickMatch", "TabQuickMatch", "TabLadder", "TabSkins" };
		for (size_t i = 0; i < sizeof(theirs) / sizeof(theirs[0]); ++i)
			if (GameWindow *win = hubWindow( theirs[i] ))
				win->winHide(TRUE);
	}
	// -online: every packet goes through our relay, so EA's firewall probe (against its dead mangler
	// servers) has nothing to find, signed in or not.  The cards' numbers and the recent matches arrive
	// after the screen drew; signed out, the first update leaves for the login.
	else if (OnlineIsEnabled())
	{
		if (signedIn())
		{
			OnlineRequestStats(TheGameSpyInfo->getLocalName());
			// each mode's last five: the cards' LAST 5 and, merged, the recent matches
			OnlineRequestHistory( 0, ONLINE_HISTORY_QM, FORM_RESULTS );
			OnlineRequestHistory( 0, ONLINE_HISTORY_AI, FORM_RESULTS );
			// the open games for the Custom games card: games.watch, answered as the lobby's list (PEER_CLEAR
			// first, then a game a line, then each change); the lobby asks again over it
			s_openGames.clear();
			s_openGamesStale = TRUE;
			if (TheGameSpyPeerMessageQueue)
			{
				PeerRequest req;
				req.peerRequestType = PeerRequest::PEERREQUEST_STARTGAMELIST;
				req.gameList.restrictGameList = FALSE;
				TheGameSpyPeerMessageQueue->addRequest( req );
			}
		}
	}
	else
	{
		if (TheFirewallHelper == NULL) {
			TheFirewallHelper = createFirewallHelper();
		}
		if (TheFirewallHelper->detectFirewall() == TRUE) {
			// don't need to detect firewall, already been done.
			delete TheFirewallHelper;
			TheFirewallHelper = NULL;
		}
	}

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentWOLWelcome );

	enableControls( TheGameSpyInfo->gotGroupRoomList() );
	TheShell->showShellMap(TRUE);

	fillMOTD();

	// -online keeps no locale and shows none, so it does not ask for one
	GameSpyMiscPreferences cPref;
	if ((cPref.getLocale() < LOC_MIN || cPref.getLocale() > LOC_MAX) && !OnlineIsEnabled())
	{
		GameSpyOpenOverlay(GSOVERLAY_LOCALESELECT);
	}

	raiseMessageBoxes = TRUE;
	TheTransitionHandler->setGroup("WOLWelcomeMenuFade");

	// the score screen's Play again lands here
	if (s_playOnEntry)
	{
		s_playOnEntry = FALSE;
		if (OnlineIsEnabled() && buttonPlayAI && signedIn())
			pressPlay();
	}

} // WOLWelcomeMenuInit

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLWelcomeMenuShutdown( WindowLayout *layout, void *userData )
{
	listboxInfo = NULL;
	buttonRejoin = NULL;
	buttonTeamSize = NULL;		// children of the panel; the next init finds them again
	buttonLeaveParty = NULL;

	if (TheFirewallHelper != NULL) {
		delete TheFirewallHelper;
		TheFirewallHelper = NULL;
	}

	isShuttingDown = TRUE;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("WOLWelcomeMenuFade");


	RaiseGSMessageBox();
}  // WOLWelcomeMenuShutdown


//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu update method */
//-------------------------------------------------------------------------------------------------
void WOLWelcomeMenuUpdate( WindowLayout * layout, void *userData)
{
	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = FALSE;
	}

	if (TheFirewallHelper != NULL)
	{
		if (TheFirewallHelper->behaviorDetectionUpdate())
		{
			TheWritableGlobalData->m_firewallBehavior = TheFirewallHelper->getFirewallBehavior();

			TheFirewallHelper->writeFirewallBehavior();

			TheFirewallHelper->flagNeedToRefresh(FALSE); // 2/19/03 BGC, we're done, so we don't need to refresh the NAT anymore.
			
			// we are now done with the firewall helper
			delete TheFirewallHelper;
			TheFirewallHelper = NULL;
		}
	}

	// The shell updates every layout on its stack, hidden ones too.  Only the hub on top and showing reads
	// the queues: covered (a page pushed over it, Quick Match pushed by a score screen over a hub that
	// hideShell hid for a match and whose init never ran since) it would eat that page's messages.
	const Bool showing = TheShell->top() == layout && !layout->isHidden();
	if (showing && TheShell->isAnimFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		// -online: the login went under a page over the hub (that page reset the info and popped back
		// here), or after a game: the login screen, not a hub of dead numbers.  Here and not from init,
		// whose pop the shell's own pop in progress would swallow.
		if (OnlineIsEnabled() && !signedIn())
		{
			leaveForLogin( FALSE );
			return;
		}

		HandleBuddyResponses();
		HandlePersistentStorageResponses();

		Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
		Bool sawImportantMessage = FALSE;
		PeerResponse resp;
		while (allowedMessages-- && !sawImportantMessage && TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_GROUPROOM:
				{
					GameSpyGroupRoom room;
					room.m_groupID = resp.groupRoom.id;
					room.m_maxWaiting = resp.groupRoom.maxWaiting;
					room.m_name = resp.groupRoomName.c_str();
					room.m_translatedName = UnicodeString(u"TEST");
					room.m_numGames = resp.groupRoom.numGames;
					room.m_numPlaying = resp.groupRoom.numPlaying;
					room.m_numWaiting = resp.groupRoom.numWaiting;
					TheGameSpyInfo->addGroupRoom( room );
					if (room.m_groupID == 0 && !s_joiningRoom)
					{
						enableControls( TRUE );
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
				{
					sawImportantMessage = TRUE;
					s_joiningRoom = FALSE;
					enableControls( TRUE );
					if (resp.joinGroupRoom.ok)
					{
						//buttonPushed = TRUE;
						TheGameSpyInfo->setCurrentGroupRoom(resp.joinGroupRoom.id);
						if (OnlineIsEnabled())
							DEBUG_LOG(("ONLINE LOBBY joined room=%d\n", resp.joinGroupRoom.id));
						//GSMessageBoxOk( TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSGroupRoomJoinOK") );

						// the lobby goes on top: the hub stays at the stack's foot
						buttonPushed = TRUE;
						TheShell->push( AsciiString( UIStyle::onlineTabScreen( UIStyle::ONLINE_TAB_CUSTOM ) ) );
					}
					else
					{
						GSMessageBoxOk( TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSGroupRoomJoinFail") );
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_DISCONNECT:
				{
					sawImportantMessage = TRUE;
					UnicodeString title, body;
					AsciiString disconMunkee;
					disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
					title = TheGameText->fetch( "GUI:GSErrorTitle" );
					body = TheGameText->fetch( disconMunkee );
					GameSpyCloseAllOverlays();
					GSMessageBoxOk( title, body );
					if (OnlineIsEnabled())
					{
						leaveForLogin( FALSE );
						return;
					}
					TheShell->pop();
				}
				break;
			default:
				takeOpenGame( resp );		// games.watch's list, for the Custom games card
				break;
			}
		}

		// -online: Play vs AI's answer, the button greyed while it or a room join is awaited; Find match
		// through the leaver lock's last second; the cards' numbers and the recent matches as they arrive
		// (auth.ok, stats, counts, history)
		if (OnlineIsEnabled() && !buttonPushed)
		{
			if (buttonPlayAI)
			{
				const Bool answerAwaited = WOLPlayVsAIUpdate();
				buttonPlayAI->winEnable( !answerAwaited && !s_joiningRoom && !buttonPushed );		// pushed: a match loads
			}
			if (!buttonPushed)
				updateTeam( s_cards[ CARD_AI ].window );
			applyControls();
			refreshCards();
			refreshRecent();
			refreshRejoin();
			refreshOpenGames();
			// one lit key: PLAY, or REJOIN while a match still running is offered
			if (s_playLit == s_rejoinShown)
			{
				s_playLit = !s_rejoinShown;
				UIStyle::setMenuKey( buttonPlayAI, s_playLit );
				UIStyle::setMenuKey( buttonRejoin, !s_playLit );
			}
		}

		// -online scripting: once per process, the click a player would make next.  -quickmatch opens Quick
		// Match; -onlinehost, -onlinejoin and -onlinesay go to the lobby, which carries them on.
		static Bool s_onlineAutoDone = FALSE;
		if (OnlineIsEnabled() && !s_onlineAutoDone && !buttonPushed && TheGameSpyInfo->gotGroupRoomList())
		{
			s_onlineAutoDone = TRUE;
			if (TheGlobalData->m_quickMatchOnStart)
			{
				DEBUG_LOG(("ONLINE WELCOME to=quickmatch\n"));
				hubLeave( UIStyle::ONLINE_TAB_QUICK_MATCH );
			}
			else if (TheGlobalData->m_onlineHostGame.isNotEmpty() || TheGlobalData->m_onlineJoinGame.isNotEmpty()
				|| TheGlobalData->m_onlineSay.isNotEmpty())
			{
				DEBUG_LOG(("ONLINE WELCOME to=lobby\n"));
				joinRoom();
			}
		}
	}

}// WOLWelcomeMenuUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLWelcomeMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed || s_aiAsked)		// leaving while ai.start is out would open a match nobody plays
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
}// WOLWelcomeMenuInput

//-------------------------------------------------------------------------------------------------
/** WOL Welcome Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLWelcomeMenuSystem( GameWindow *window, UnsignedInt msg, 
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

		case GCM_SELECTED:
			{
				// Play vs AI's speed, kept for the next Play and the score screen's Play again
				if ((GameWindow *)mData1 == comboBoxAISpeed)
				{
					QuickMatchPreferences pref;
					pref.setInt( "AIGameSpeed", PlayerGameSpeedFromComboBox( comboBoxAISpeed, RETAIL_GAME_SPEED_PERCENT ) );
					pref.write();
				}
				break;
			}

		case GBM_SELECTED:
			{
				if (buttonPushed || s_aiAsked)		// the server's answer to Play vs AI comes first
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				// a room join is out: the lobby is about to go up, and only the ways off the service still answer
				if (s_joiningRoom && controlID != buttonBackID && controlID != buttonSignOutID)
					break;
				if (UIStyle::onlineTabClicked( control, hubLeave ))
					break;

				if ( controlID == buttonBackID )
				{
					logOut();
					buttonPushed = TRUE;
					TheShell->pop();
				}
				else if (controlID == buttonSignOutID)
				{
					leaveForLogin( TRUE );
				}
				else if (controlID == buttonQuickMatchID)
				{
					hubLeave( UIStyle::ONLINE_TAB_QUICK_MATCH );
				}
				else if (controlID == buttonMyInfoID )
				{
					SetLookAtPlayer(TheGameSpyInfo->getLocalProfileID(), TheGameSpyInfo->getLocalName());
					GameSpyToggleOverlay(GSOVERLAY_PLAYERINFO);
				}
				else if (controlID == buttonLobbyID)
				{
					hubLeave( UIStyle::ONLINE_TAB_CUSTOM );
				}
				else if (controlID == buttonPlayAIID)
				{
					pressPlay();
				}
				else if (buttonTeamSize != NULL && control == buttonTeamSize)
				{
					stepTeamSize();
				}
				else if (buttonLeaveParty != NULL && control == buttonLeaveParty)
				{
					DEBUG_LOG(("ONLINE PARTY leave pressed\n"));
					OnlineLeaveParty();
				}
				else if (controlID == buttonRejoinID)
				{
					pressRejoin();
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
}// WOLWelcomeMenuSystem
