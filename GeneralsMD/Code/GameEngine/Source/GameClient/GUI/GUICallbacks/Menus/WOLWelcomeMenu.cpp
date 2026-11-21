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

#include "Common/GameEngine.h"
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

/// Find match also waits out the server's lock on a player who left a ranked match
static void applyControls( void )
{
	if (buttonQuickMatch)
		buttonQuickMatch->winEnable( s_controlsLive && OnlineGetQuickMatchLockSeconds() == 0 );
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
}

/// the answer to Play: the match starts, or the reason it did not goes on the Play vs AI card
Bool WOLPlayVsAIUpdate( void )
{
	if (!s_aiAsked)
		return FALSE;
	OnlineAIMatch ai;
	const OnlineAIMatchState state = OnlineGetAIMatch( ai );
	if (state == ONLINE_AI_WAITING)
		return TRUE;
	s_aiAsked = FALSE;
	UnicodeString what;
	if (state == ONLINE_AI_READY)
	{
		if (startAIMatch( ai ))
			return FALSE;
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

static void pressPlay( void )
{
	s_aiRefusal.clear();
	s_cardsDirty = TRUE;
	WOLPlayVsAI();
	if (buttonPlayAI)
		buttonPlayAI->winEnable( FALSE );		// the update wakes it when the answer is in
}

//-------------------------------------------------------------------------------------------------
// The hub's cards (ONLINE-UI-REDESIGN.md "Hub", experiments/online-redesign/hub.html), drawn in code
// behind the master's buttons on the master's own boxes.  Every length is the mockup's at 1080 through
// UIStyle::px, the scale the master's boxes were fitted at; the lettering shrinks with them at 4:3
// (hubPoints), as the master's own does, so a card's words fit the card the master fitted.
//-------------------------------------------------------------------------------------------------
enum { CARD_FACTS = 3, RECENT_ROWS = 6 };
static const Real FACT_WIDTH = 190.0f;
static const Real WIDE_FACT_WIDTH = 250.0f;		///< Custom games' one fact, whose label is long in Turkish
static const Real LEAD_TOP = 56.0f;					///< the lead, or the status line in its place, from the card's top
static const Real FACTS_TOP = 106.0f;				///< the facts and the figure's label under it

struct HubCard
{
	GameWindow *window;
	Bool portraitColumn;				///< Play vs AI and Quick Match keep the rank portrait's column
	const Image *portrait;			///< NULL for an unranked player, who shows none
	DisplayString *title, *lead, *label, *figure, *tier, *status;
	DisplayString *factLabel[ CARD_FACTS ], *factValue[ CARD_FACTS ];
	Int facts;
	Real factWidth;							///< the facts' column at 1080
	Int statusRoom;							///< pixels from the lead's top to the facts
};
enum { CARD_AI, CARD_QUICK_MATCH, CARD_CUSTOM, CARD_COUNT };
static HubCard s_cards[ CARD_COUNT ];

/// a recent match's cells, left to right as the mockup's row
enum RecentCell { CELL_CHIP, CELL_MODE, CELL_OPPONENT, CELL_RATING, CELL_MAP, CELL_CHANGE, CELL_AGE, CELL_COUNT };
/// the cells' widths at 1080 (the rating shares the opponent's), 1080 in all, each padded CELL_PAD either side
static const Real CELL_WIDTH[] = { 110.0f, 160.0f, 300.0f, 0.0f, 240.0f, 120.0f, 150.0f };
static const Real CELL_PAD = 10.0f;
static Int s_cellWidth[ CELL_COUNT ];		///< on this screen: the chip's as wide as its widest result, taken from the map's
static DisplayString *s_recent[ RECENT_ROWS ][ CELL_COUNT ];
static Bool s_recentWon[ RECENT_ROWS ];
static Color s_recentChange[ RECENT_ROWS ];
static Int s_recentRows = 0;
static DisplayString *s_recentTitle = NULL;
static DisplayString *s_recentLine = NULL;		///< no matches, or no answer
static DisplayString *s_motdTitle = NULL;
static GameWindow *s_cardRecent = NULL;
static GameWindow *s_cardMOTD = NULL;
static UnsignedInt s_cardsSerial = 0;
static Int s_cardsCounts[ 3 ];
static Int s_cardsLock = 0;					///< the Quick Match lock's seconds the card last showed
static UnsignedInt s_recentSerial = 0;		///< OnlineGetHistorySerial as the rows were last built
static Bool s_recentStale = TRUE;				///< new strings: build the rows whatever the serial says

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
	s_recentTitle = s_recentLine = s_motdTitle = NULL;
	s_recentRows = 0;
	memset( s_rejoinChip, 0, sizeof( s_rejoinChip ) );
	memset( s_rejoinNick, 0, sizeof( s_rejoinNick ) );
	memset( s_rejoinSide, 0, sizeof( s_rejoinSide ) );
	s_rejoinTitle = s_rejoinLine = s_rejoinNote = NULL;
	s_rejoinRows = 0;
}

/// a layout's points on the cards: UIStyle's size at 16:9 and wider, shrunk with the fitted board at 4:3
static Int hubPoints( Int points )
{
	return max( 1, REAL_TO_INT( points * UIStyle::px( 1080.0f ) / (Real)UIStyle::tallPx( 1080.0f ) + 0.5f ) );
}
static GameFont *bodyFont( void )			{ return UIStyle::readableFont( hubPoints( UIStyle::TEXT_BODY ) ); }
/// a card's heading, two points over the body's size whatever the readable floor grew the body to
/// (UIStyle::sectionFont's rule on the hub's own body): on hubPoints( TEXT_STRONG ) alone it came out
/// at 14 pixels over an 18 pixel body at 1280x720
static GameFont *headingFont( void )
{
	const Int bodyHeight = bodyFont()->height;
	Int bodyPoints = hubPoints( UIStyle::TEXT_BODY );
	while (UIStyle::font( bodyPoints )->height < bodyHeight)
		++bodyPoints;
	return UIStyle::font( bodyPoints + UIStyle::TEXT_STRONG - UIStyle::TEXT_BODY );
}
static GameFont *chipFont( void )			{ return UIStyle::readableFont( hubPoints( UIStyle::TEXT_SMALL ), TRUE ); }

static void makeCard( HubCard &card, const char *window, const char *title, const char *lead,
	const char *label, Bool portraitColumn, Real factWidth, Int facts, const char *const factKeys[] )
{
	card.window = hubWindow( window );
	card.portraitColumn = portraitColumn;
	card.portrait = NULL;
	card.factWidth = factWidth;
	Int w = 0, h = 0;
	if (card.window)
		card.window->winGetSize( &w, &h );
	// a status line takes the lead's place and its rule, down to the facts
	card.statusRoom = UIStyle::px( FACTS_TOP - LEAD_TOP - 6.0f );
	GameFont *body = bodyFont();
	const Int inner = w - UIStyle::px( 48.0f );
	card.title = hubString( headingFont() );
	card.title->setText( UIStyle::fitKey( title, headingFont(), inner ) );
	card.lead = hubString( body );
	card.lead->setText( UIStyle::fitKey( lead, body, inner ) );
	// the figure's column runs from the portrait's right (or the card's inset) to the facts
	const Int figureLeft = UIStyle::px( portraitColumn ? 148.0f : 24.0f );
	const Int figureWidth = w - UIStyle::px( 24.0f + factWidth + 12.0f ) - figureLeft;
	card.label = hubString( body );
	card.label->setText( label ? UIStyle::fitKey( label, body, figureWidth ) : UnicodeString::TheEmptyString );
	card.figure = hubString( UIStyle::titleFont( hubPoints( UIStyle::TEXT_DISPLAY ) ) );
	card.tier = hubString( body );
	card.status = hubString( body );
	card.status->setWordWrap( inner );
	card.facts = facts;
	for (Int i = 0; i < facts; ++i)
	{
		card.factLabel[ i ] = hubString( body );
		card.factLabel[ i ]->setText( UIStyle::fitKey( factKeys[ i ], body, UIStyle::px( factWidth - 60.0f ) ) );
		card.factValue[ i ] = hubString( body );
	}
}

static void drawModeCard( const HubCard &card, Int x, Int y, Int w )
{
	const Int left = x + UIStyle::px( 24.0f );
	UIStyle::heading( card.title, left, y + UIStyle::px( 22.0f ) );
	if (card.status->getTextLength() == 0)
	{
		card.lead->draw( left, y + UIStyle::px( LEAD_TOP ), UIStyle::MUTED, UIStyle::SHADOW );
		TheDisplay->drawFillRect( left, y + UIStyle::px( 92.0f ), w - UIStyle::px( 48.0f ), 1, UIStyle::GRID );
	}
	else
		card.status->draw( left, y + UIStyle::px( LEAD_TOP ), UIStyle::INK, UIStyle::SHADOW );

	Int figureX = left;
	if (card.portraitColumn)
	{
		const Int top = y + UIStyle::px( 112.0f );
		const Int size = UIStyle::px( 104.0f );
		if (card.portrait)
			TheDisplay->drawImage( card.portrait, left, top, left + size, top + size );
		figureX = x + UIStyle::px( 148.0f );
	}
	card.label->draw( figureX, y + UIStyle::px( 112.0f ), UIStyle::MUTED, UIStyle::SHADOW );
	card.figure->draw( figureX, y + UIStyle::px( 136.0f ), UIStyle::INK, UIStyle::SHADOW );
	card.tier->draw( figureX, y + UIStyle::px( 204.0f ), UIStyle::INK, UIStyle::SHADOW );

	const Int factsX = x + w - UIStyle::px( 24.0f + card.factWidth );
	for (Int i = 0; i < card.facts; ++i)
		UIStyle::fact( card.factLabel[ i ], card.factValue[ i ], factsX, y + UIStyle::px( FACTS_TOP + 36.0f * i ),
			UIStyle::px( card.factWidth ), UIStyle::px( 36.0f ) );
}

/// a cell's words centred on the row, from x, or ending at x when right
static void drawCell( DisplayString *text, Int x, Int top, Int height, Color color, Bool right = FALSE )
{
	Int w, h;
	text->getSize( &w, &h );
	text->draw( right ? x - w : x, top + ( height - h ) / 2, color, UIStyle::SHADOW );
}

static void drawRecent( Int x, Int y, Int w )
{
	UIStyle::heading( s_recentTitle, x + UIStyle::px( 24.0f ), y + UIStyle::px( 20.0f ) );
	const Int rowTop = y + UIStyle::px( 60.0f );
	const Int rowHeight = UIStyle::px( 44.0f );
	if (s_recentRows == 0)
	{
		drawCell( s_recentLine, x + UIStyle::px( 24.0f ), rowTop, rowHeight, UIStyle::MUTED );
		return;
	}
	const Int rowX = x + UIStyle::px( 14.0f );
	const Int pad = UIStyle::px( CELL_PAD );
	Int start[ CELL_COUNT + 1 ];
	start[ 0 ] = rowX;
	for (Int c = 0; c < CELL_COUNT; ++c)
		start[ c + 1 ] = start[ c ] + s_cellWidth[ c ];
	for (Int r = 0; r < s_recentRows; ++r)
	{
		DisplayString **cell = s_recent[ r ];
		const Int top = rowTop + r * rowHeight;
		Int w0, h0;
		cell[ CELL_CHIP ]->getSize( &w0, &h0 );
		const Int chipHeight = h0 + UIStyle::tallPx( 5.0f ) + UIStyle::tallPx( 4.0f );
		UIStyle::chip( cell[ CELL_CHIP ], start[ CELL_CHIP ] + pad, top + ( rowHeight - chipHeight ) / 2,
			s_recentWon[ r ] ? UIStyle::GOLD : UIStyle::MUTED, s_recentWon[ r ] ? UIStyle::GOLD : UIStyle::FRAME );
		drawCell( cell[ CELL_MODE ], start[ CELL_MODE ] + pad, top, rowHeight, UIStyle::MUTED );
		drawCell( cell[ CELL_OPPONENT ], start[ CELL_OPPONENT ] + pad, top, rowHeight, UIStyle::INK );
		cell[ CELL_OPPONENT ]->getSize( &w0, &h0 );
		drawCell( cell[ CELL_RATING ], start[ CELL_OPPONENT ] + pad + w0 + UIStyle::px( 6.0f ), top, rowHeight, UIStyle::MUTED );
		drawCell( cell[ CELL_MAP ], start[ CELL_MAP ] + pad, top, rowHeight, UIStyle::INK );
		drawCell( cell[ CELL_CHANGE ], start[ CELL_AGE ] - pad, top, rowHeight, s_recentChange[ r ], TRUE );
		drawCell( cell[ CELL_AGE ], start[ CELL_COUNT ] - pad, top, rowHeight, UIStyle::MUTED, TRUE );
		TheDisplay->drawFillRect( rowX, top + rowHeight - 1, start[ CELL_COUNT ] - rowX, 1, UIStyle::GRID );
	}
}

/// the left of the card that Rejoin leaves free: the button's 300 and its 24 inset, and 24 more between
static Int rejoinRoom( Int w )
{
	return w - UIStyle::px( 24.0f + 300.0f + 24.0f + 24.0f );
}

/// the match still running, on the recent matches' card: heading, mode and map with how long it has run,
/// then each seat with a chip saying whether it plays on, in two columns of four, and the note at the foot
static void drawRejoin( Int x, Int y, Int w, Int h )
{
	const Int left = x + UIStyle::px( 24.0f );
	UIStyle::heading( s_rejoinTitle, left, y + UIStyle::px( 20.0f ) );
	s_rejoinLine->draw( left, y + UIStyle::px( 58.0f ), UIStyle::INK, UIStyle::SHADOW );
	TheDisplay->drawFillRect( left, y + UIStyle::px( 92.0f ), w - UIStyle::px( 48.0f ), 1, UIStyle::GRID );
	const Int rowTop = y + UIStyle::px( 102.0f );
	const Int rowHeight = UIStyle::px( 34.0f );
	const Int columnWidth = rejoinRoom( w ) / 2;
	for (Int r = 0; r < s_rejoinRows; ++r)
	{
		const Int cellX = left + ( r / REJOIN_COLUMN_ROWS ) * columnWidth;
		const Int top = rowTop + ( r % REJOIN_COLUMN_ROWS ) * rowHeight;
		Int cw, ch;
		s_rejoinChip[ r ]->getSize( &cw, &ch );
		const Int chipHeight = ch + UIStyle::tallPx( 5.0f ) + UIStyle::tallPx( 4.0f );
		const Int chipWidth = UIStyle::chip( s_rejoinChip[ r ], cellX, top + ( rowHeight - chipHeight ) / 2,
			s_rejoinPresent[ r ] ? UIStyle::ACTIVE : UIStyle::MUTED, UIStyle::FRAME );
		const Int nickX = cellX + chipWidth + UIStyle::px( 10.0f );
		drawCell( s_rejoinNick[ r ], nickX, top, rowHeight, s_rejoinPresent[ r ] ? UIStyle::INK : UIStyle::MUTED );
		Int nw, nh;
		s_rejoinNick[ r ]->getSize( &nw, &nh );
		drawCell( s_rejoinSide[ r ], nickX + nw + UIStyle::px( 8.0f ), top, rowHeight, UIStyle::MUTED );
	}
	Int nw, nh;
	s_rejoinNote->getSize( &nw, &nh );
	s_rejoinNote->draw( left, y + h - UIStyle::px( 24.0f ) - nh, s_rejoinNoteColor, UIStyle::SHADOW );
}

/// every card's draw: the card's box, then what it holds
static void drawHubCard( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	UIStyle::panel( x, y, w, h, UIStyle::PANEL );
	if (s_strings.empty())
		return;
	for (Int c = 0; c < CARD_COUNT; ++c)
	{
		if (s_cards[ c ].window == window)
			drawModeCard( s_cards[ c ], x, y, w );
	}
	if (window == s_cardRecent && s_rejoinShown)
		drawRejoin( x, y, w, h );
	else if (window == s_cardRecent)
		drawRecent( x, y, w );
	if (window == s_cardMOTD)
		UIStyle::heading( s_motdTitle, x + UIStyle::px( 24.0f ), y + UIStyle::px( 20.0f ) );
}

/// the header (wordmark and the player's line) on the see-through ButtonHolder, which draws over the
/// panel; the master's StaticTextTitle and HeaderRule draw the title and its rule
static void drawHubHeader( GameWindow *window, WinInstanceData *instData )
{
	UIStyle::onlineHeader( UnicodeString::TheEmptyString, UnicodeString::TheEmptyString );
}

static void makeCards( void )
{
	freeHubStrings();
	static const char *const aiFacts[] = { "GUI:OnlineGamesLabel", "GUI:OnlineWinRateLabel", "GUI:OnlineSpeedLabel" };
	static const char *const qmFacts[] = { "GUI:OnlinePlaceLabel", "GUI:OnlineHubRecord", "GUI:OnlineHubInQueue" };
	// the client knows players online and open games, not who sits in a room or plays: one fact
	static const char *const customFacts[] = { "GUI:OnlineLoginOnlineNow" };
	makeCard( s_cards[ CARD_AI ], "CardPlayVsAI", "GUI:OnlineHubVsAITitle", "GUI:OnlineHubVsAILead",
		"GUI:OnlineAIRatingLabel", TRUE, FACT_WIDTH, 3, aiFacts );
	makeCard( s_cards[ CARD_QUICK_MATCH ], "CardQuickMatch", "GUI:OnlineQuickMatchLabel",
		"GUI:OnlineHubQuickMatchLead", "GUI:OnlineLadderRatingLabel", TRUE, FACT_WIDTH, 3, qmFacts );
	makeCard( s_cards[ CARD_CUSTOM ], "CardCustom", "GUI:OnlineHubCustomTitle", "GUI:OnlineHubCustomLead",
		"GUI:OnlineOpenGamesLabel", FALSE, WIDE_FACT_WIDTH, 1, customFacts );
	// the Speed fact's value is the master's ComboBoxGameSpeed, over its place

	s_cardRecent = hubWindow( "CardRecent" );
	s_cardMOTD = hubWindow( "CardMOTD" );
	Int w = 0, h = 0;
	if (s_cardRecent)
		s_cardRecent->winGetSize( &w, &h );
	s_recentTitle = hubString( headingFont() );
	s_recentTitle->setText( UIStyle::fitKey( "GUI:OnlineHubRecent", headingFont(), w - UIStyle::px( 48.0f ) ) );
	s_recentLine = hubString( bodyFont() );
	for (Int r = 0; r < RECENT_ROWS; ++r)
		for (Int c = 0; c < CELL_COUNT; ++c)
			s_recent[ r ][ c ] = hubString( c == CELL_CHIP ? chipFont() : bodyFont() );
	s_rejoinTitle = hubString( headingFont() );
	s_rejoinTitle->setText( UIStyle::fitKey( "GUI:OnlineRejoinTitle", headingFont(), w - UIStyle::px( 48.0f ) ) );
	s_rejoinLine = hubString( bodyFont() );
	s_rejoinNote = hubString( bodyFont() );
	for (Int r = 0; r < REJOIN_ROWS; ++r)
	{
		s_rejoinChip[ r ] = hubString( chipFont() );
		s_rejoinNick[ r ] = hubString( bodyFont() );
		s_rejoinSide[ r ] = hubString( bodyFont() );
	}
	s_rejoinStale = TRUE;
	// the cells on this screen, each edge rounded once so the row keeps its width; the result column
	// as wide as its widest chip (Turkish's loss outgrows "Loss"), the extra taken from the map's
	Real at = 0.0f;
	for (Int c = 0; c < CELL_COUNT; ++c)
	{
		s_cellWidth[ c ] = UIStyle::px( at + CELL_WIDTH[ c ] ) - UIStyle::px( at );
		at += CELL_WIDTH[ c ];
	}
	static const char *const results[] = { "GUI:OnlineWinLabel", "GUI:OnlineLossLabel" };
	Int widestChip = 0;
	for (size_t i = 0; i < sizeof( results ) / sizeof( results[ 0 ] ); ++i)
	{
		s_recent[ 0 ][ CELL_CHIP ]->setText( TheGameText->fetch( results[ i ] ) );
		Int chipWidth, chipHeight;
		s_recent[ 0 ][ CELL_CHIP ]->getSize( &chipWidth, &chipHeight );
		widestChip = max( widestChip, chipWidth + 2 * UIStyle::tallPx( 8.0f ) );		// UIStyle::chip's sides
	}
	const Int grown = UIStyle::px( CELL_PAD ) + widestChip - s_cellWidth[ CELL_CHIP ];
	if (grown > 0)
	{
		s_cellWidth[ CELL_CHIP ] += grown;
		s_cellWidth[ CELL_MAP ] -= grown;
	}
	if (s_cardMOTD)
		s_cardMOTD->winGetSize( &w, &h );
	UnicodeString from;
	from.format( TheGameText->fetch( "GUI:OnlineHubFrom" ), LobbyServerLabel().str() );
	s_motdTitle = hubString( headingFont() );
	s_motdTitle->setText( UIStyle::fitText( from, headingFont(), w - UIStyle::px( 48.0f ) ) );

	GameWindow *boxes[] = { s_cards[ CARD_AI ].window, s_cards[ CARD_QUICK_MATCH ].window, s_cards[ CARD_CUSTOM ].window,
		s_cardRecent, s_cardMOTD };
	for (size_t i = 0; i < sizeof( boxes ) / sizeof( boxes[ 0 ] ); ++i)
		if (boxes[ i ])
			boxes[ i ]->winSetDrawFunc( drawHubCard );
	s_cardsDirty = TRUE;
	s_recentStale = TRUE;
}

/** A card's status line (a refusal, the Quick Match lock) in the lead's place, empty for none: in the body
	* face, then in each smaller size down to the layout's own body size, the size the master's fit gives (at
	* 1280x720 and at 4:3 the readable floor grows the body face past what the room holds). */
static void setCardStatus( HubCard &card, const UnicodeString &text )
{
	card.status->setFont( bodyFont() );
	card.status->setText( text );
	if (text.isEmpty())
		return;
	std::vector<GameFont *> faces( 1, bodyFont() );
	for (Int points = hubPoints( UIStyle::TEXT_BODY ); ; ++points)
	{
		GameFont *face = UIStyle::font( points );
		if (face == NULL || face->height >= faces[ 0 ]->height)
			break;
		faces.insert( faces.begin() + 1, face );		// largest of the smaller sizes first
	}
	for (size_t i = 0; i < faces.size(); ++i)
	{
		card.status->setFont( faces[ i ] );
		Int width, height;
		card.status->getSize( &width, &height );
		if (height <= card.statusRoom)
			return;
	}
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
	setCardStatus( ai, s_aiRefusal );

	HubCard &qm = s_cards[ CARD_QUICK_MATCH ];
	const UIStyle::RatingTier tier = UIStyle::playerTier( info );
	qm.portrait = UIStyle::tierImage( tier, side );
	qm.figure->setText( info.hasRating ? UIStyle::ratingText( info.exactRating ) : none );
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
	setCardStatus( qm, text );

	HubCard &custom = s_cards[ CARD_CUSTOM ];
	custom.figure->setText( counts[ 1 ] >= 0 ? UIStyle::grouped( counts[ 1 ] ) : none );
	custom.factValue[ 0 ]->setText( counts[ 0 ] >= 0 ? UIStyle::grouped( counts[ 0 ] ) : none );
	setCardStatus( custom, UnicodeString::TheEmptyString );
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

/// the recent matches, copied and built again only when the history's serial moves
static void refreshRecent( void )
{
	if (s_strings.empty())
		return;
	const UnsignedInt serial = OnlineGetHistorySerial( 0, ONLINE_HISTORY_ALL );
	if (!s_recentStale && serial == s_recentSerial)
		return;
	s_recentStale = FALSE;
	s_recentSerial = serial;
	OnlineHistory history;
	const OnlineLadderState state = OnlineGetHistory( 0, ONLINE_HISTORY_ALL, history );
	s_recentRows = history.arrived ? min( (Int)history.rows.size(), (Int)RECENT_ROWS ) : 0;
	if (state == ONLINE_LADDER_READY && s_recentRows == 0)
		s_recentLine->setText( TheGameText->fetch( "GUI:OnlineHubRecentNone" ) );
	else if (state == ONLINE_LADDER_FAILED && s_recentRows == 0)
		s_recentLine->setText( TheGameText->fetch( "GUI:OnlineHubRecentNoAnswer" ) );
	else
		s_recentLine->setText( UnicodeString::TheEmptyString );

	GameFont *body = bodyFont();
	const Int pad = UIStyle::px( 2.0f * CELL_PAD );
	for (Int r = 0; r < s_recentRows; ++r)
	{
		const OnlineHistoryRow &row = history.rows[ r ];
		DisplayString **cell = s_recent[ r ];
		s_recentWon[ r ] = row.won;
		s_recentChange[ r ] = UIStyle::changeColor( row.change );
		cell[ CELL_CHIP ]->setText( TheGameText->fetch( row.won ? "GUI:OnlineWinLabel" : "GUI:OnlineLossLabel" ) );
		cell[ CELL_MODE ]->setText( UIStyle::fitKey( row.ai ? "GUI:OnlineHubMatchAI" : "GUI:OnlineQuickMatchLabel", body,
			s_cellWidth[ CELL_MODE ] - pad ) );
		cell[ CELL_RATING ]->setText( row.ai ? UIStyle::eloText( row.opponentRating ) : UIStyle::ratingText( row.opponentRating ) );
		Int ratingWidth, h;
		cell[ CELL_RATING ]->getSize( &ratingWidth, &h );
		UnicodeString opponent;
		if (row.ai)
			opponent = TheGameText->fetch( "GUI:OnlineComputerLabel" );
		else
			opponent = UnicodeString( MultiByteToWideCharSingleLine( row.opponent.str() ).c_str() );
		cell[ CELL_OPPONENT ]->setText( UIStyle::fitName( opponent, body,
			s_cellWidth[ CELL_OPPONENT ] - pad - ratingWidth - UIStyle::px( 6.0f ) ) );
		// a ranked map is generated from the match seed and its name only says so ("rmg_v19_..."); an AI
		// match's map is one of the official ones, by its shown name
		UnicodeString map;
		if (row.map.startsWithNoCase( "rmg_" ))
			map = TheGameText->fetch( "GUI:Random" );
		else
			map = UnicodeString( MultiByteToWideCharSingleLine( row.map.str() ).c_str() );
		cell[ CELL_MAP ]->setText( UIStyle::fitText( map, body, s_cellWidth[ CELL_MAP ] - pad, UnicodeString::TheEmptyString, map ) );
		cell[ CELL_CHANGE ]->setText( UIStyle::ratingText( row.change, TRUE ) );
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
			s_rejoinChip[ r ]->setText( TheGameText->fetch( seat.present ? "GUI:OnlineRejoinPlaying" : "GUI:OnlineRejoinAway" ) );
			Int chipWidth, chipHeight;
			s_rejoinChip[ r ]->getSize( &chipWidth, &chipHeight );
			const Int room = column - chipWidth - 2 * UIStyle::tallPx( 8.0f ) - UIStyle::px( 10.0f + 8.0f + 12.0f );
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

	// the master's frame: the tabs with Home open, the header over the panel, the cards behind the buttons
	UIStyle::onlinePageInit( parentWOLWelcome, "WOLWelcomeMenu.wnd", UIStyle::ONLINE_TAB_HOME );
	makeCards();
	if (GameWindow *holder = hubWindow( "ButtonHolder" ))
		holder->winSetDrawFunc( drawHubHeader );
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
	// Play is the card's first action, its label lit as the focused button's; Play vs AI and Sign out
	// belong to the -online service
	if (buttonPlayAI)
	{
		buttonPlayAI->winSetEnabledTextColors( UIStyle::ACTIVE, UIStyle::SHADOW );
		buttonPlayAI->winHide( !OnlineIsEnabled() );
	}
	// Play vs AI's speed in the value's place of its card's Speed fact
	comboBoxAISpeed = hubWindow( "ComboBoxGameSpeed" );
	if (comboBoxAISpeed)
	{
		PopulatePlayerGameSpeedComboBox( comboBoxAISpeed, aiGameSpeed() );
		comboBoxAISpeed->winHide( !OnlineIsEnabled() );
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
			OnlineRequestHistory( 0, ONLINE_HISTORY_ALL, RECENT_ROWS );
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
				buttonPlayAI->winEnable( !answerAwaited && !s_joiningRoom );
			}
			applyControls();
			refreshCards();
			refreshRecent();
			refreshRejoin();
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
