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

// FILE: ScoreScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Jun 2002
//
//	Filename: 	ScoreScreen.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Gui callbacks for the Score Screen
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
#include "Lib/Clock.h"
#include "Platform/SleepMilliseconds.h"

#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BattleHonors.h"
#include "Common/CopyProtection.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/ScoreKeeper.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/ThingFactory.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"
#include "GameClient/CDCheck.h"
#include "GameClient/PlayerColorScheme.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/VideoPlayer.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/UIStyle.h"
#include "GameClient/ScoreScreenLayout.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonOkID = NAMEKEY_INVALID;
///static NameKeyType buttonRehostID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType chatBoxBorderID = NAMEKEY_INVALID;
static NameKeyType buttonContinueID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
static NameKeyType buttonSaveReplayID = NAMEKEY_INVALID;
static NameKeyType backdropID = NAMEKEY_INVALID;

static GameWindow *parent = NULL;
static GameWindow *buttonOk = NULL;
//static GameWindow *buttonRehost = NULL;
static GameWindow *buttonContinue = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *buttonEmote = NULL;
static GameWindow *chatBoxBorder = NULL;
static GameWindow *buttonBuddies = NULL;
static GameWindow *staticTextGameSaved = NULL;
static GameWindow *backdrop = NULL;
static GameWindow *challengePortrait = NULL;
static GameWindow *challengeRemarks = NULL;
static GameWindow *challengeWinLossText = NULL;
static GameWindow *gadgetParent = NULL;

static Bool overidePlayerDisplayName = FALSE;

//External declarations
NameKeyType listboxChatWindowScoreScreenID = NAMEKEY_INVALID;
GameWindow *listboxChatWindowScoreScreen = NULL;


std::string LastReplayFileName;
static Bool canSaveReplay = FALSE;
extern void PopupReplayUpdate(WindowLayout *layout, void *userData);
// the summary and graphs panel that replaces the layout's score table, ScoreScreenGraphs.cpp
extern void ScoreScreenGraphsInit( GameWindow *backdrop );
extern void ScoreScreenGraphsAddPlayer( Player *player, const UnicodeString &name, Color color );
extern void ScoreScreenGraphsAddSide( const UnicodeString &name, Color color, Int score, Int unitsBuilt, Int unitsLost, Int unitsDestroyed,
	Int buildingsBuilt, Int buildingsLost, Int buildingsDestroyed, Int earned, Int spent );
extern void ScoreScreenGraphsShow( GameWindow *floor, Bool chat );
extern Real ScoreScreenGraphsChatTop( Real listBottom );
extern void ScoreScreenGraphsSinglePlayer( void );
extern void ScoreScreenGraphsShutdown( void );
extern Bool ScoreScreenGraphsSummaryOpen( void );
extern void ScoreScreenGraphsSubtitle( const char *key );
// WOLWelcomeMenu.cpp: the hub's next init starts a Play vs AI match as if Play were pressed
extern void WOLWelcomeMenuStartPlayVsAIOnEntry( void );

void initSinglePlayer( void );
void finishSinglePlayerInit( void );
static Bool s_needToFinishSinglePlayerInit = FALSE;
static Bool buttonIsFinishCampaign = FALSE;
static WindowLayout *s_blankLayout = NULL;

void initSkirmish( void );
void initLANMultiPlayer(void);
void initInternetMultiPlayer(void);
void initReplayMultiPlayer(void);
void initReplaySinglePlayer(void);
void grabMultiPlayerInfo( void );
void grabSinglePlayerInfo( void );
static void hideScoreTable( void );
static void placeButtonRow( void );
static void placeOnBoard( GameWindow *win, Real x0, Real y0, Real x1, Real y1 );
void ScoreScreenEnableControls(Bool enable);
void displayChallengewinLoss( Image *imageGeneral, AsciiString strGeneral );
enum {
	SCORESCREEN_SINGLEPLAYER = 0,
	SCORESCREEN_SKIRMISH,
	SCORESCREEN_LAN,
	SCORESCREEN_INTERNET,
	SCORESCREEN_REPLAY
	};
static Int screenType;
static Bool s_onlineVerdictShown = FALSE;		///< -online: the rated/unrated line is in the chat box

/// a skirmish the online screens started: they start one only as Play vs AI, and stay up under this screen
static Bool isOnlineAIMatch( void )
{
	return screenType == SCORESCREEN_SKIRMISH && OnlineIsEnabled() && TheGameSpyInfo != NULL;
}

/// the key of why the server left a match unrated, GUI:OnlineUnrated where the strings have no word for it;
/// worked out once a reason, as the card asks every pass and hasText walks the strings
static AsciiString unratedKey( const AsciiString &reason )
{
	static AsciiString s_reason;
	static AsciiString s_label( "GUI:OnlineUnrated" );
	if( reason == s_reason )
		return s_label;
	s_reason = reason;
	s_label.format( "GUI:OnlineUnrated_%s", reason.str() );
	if( reason.isEmpty() || !UIStyle::hasText( s_label.str() ) )
		s_label = "GUI:OnlineUnrated";
	return s_label;
}

//-----------------------------------------------------------------------------
// -online, after a Quick Match or Play vs AI: the rating card under the standings (ONLINE-UI-REDESIGN.md,
// "Score screen after an online match").  The rating that moved, the new tier's rank portrait, the old
// value, an arrow, the new one and the change, the tier and the next one with a track, and one line on
// where the result stands.  After a Quick Match the chat keeps its place under it.  The card stands on
// the Summary alone: Graphs and Build order keep their whole page.
//-----------------------------------------------------------------------------
enum ScoreCard
{
	SCORE_CARD_NONE,
	SCORE_CARD_LADDER,		///< a Quick Match: the ladder rating
	SCORE_CARD_AI,				///< Play vs AI: the AI rating
};
static ScoreCard s_card = SCORE_CARD_NONE;
static Bool s_cardWon = FALSE;
static AsciiString s_cardSide;								///< the local player's base side, for the rank portrait
static AsciiString s_cardAIMatch;							///< the Play vs AI match still live as the screen opened (a quit)
static UnsignedInt s_cardSince = 0;						///< when the screen opened: the line's no-answer clock
static GameWindow *s_cardWindow = NULL;
static GameWindow *s_aiFloor = NULL;					///< after an AI match, the chat's place left empty

/// where the card's buttons send the player once this screen is gone
enum ScoreLeave
{
	SCORE_LEAVE_STAY,					///< what the pop uncovers: Quick Match after a Quick Match, the hub after Play vs AI
	SCORE_LEAVE_HUB,					///< the hub at the online stack's foot
	SCORE_LEAVE_LADDER,				///< Ladder over the hub
	SCORE_LEAVE_QUICK_MATCH,	///< Quick Match over the hub, after Play vs AI
};
static ScoreLeave s_leave = SCORE_LEAVE_STAY;

/// the send as it holds Exit and the wide button, once a screen
static Bool s_sendingSeen = FALSE;
static Bool s_sendingOver = FALSE;
static UnsignedInt s_sendingSince = 0;

/// what the card shows; its words are made again only when this changes
struct RatingFacts
{
	Bool shown;					///< a rating to show at all: the server sent one
	Bool moved;					///< the verdict moved it: the old value, the arrow and the change
	Real before;
	Real after;					///< the value in the display face
	Int games;					///< for the tier, -1 unknown
	AsciiString line;		///< the last line's key
	Bool sending;				///< the line says the result is on its way, over a sliding track
	Bool elo;						///< the figures are a whole Elo: the AI card's always, the ladder's from a server with Elo

	RatingFacts() : shown( FALSE ), moved( FALSE ), before( 0.0f ), after( 0.0f ), games( -1 ), sending( FALSE ), elo( FALSE ) {}
	Bool operator==( const RatingFacts &other ) const
	{
		return shown == other.shown && moved == other.moved && before == other.before && after == other.after
			&& games == other.games && line == other.line && sending == other.sending && elo == other.elo;
	}
};
static RatingFacts s_cardFacts;
static Bool s_cardBuilt = FALSE;

enum
{
	CARD_HEADING,
	CARD_OLD,
	CARD_ARROW,
	CARD_NEW,
	CARD_CHANGE,
	CARD_TIER,
	CARD_NEXT,
	CARD_LINE,
	CARD_TEXT_COUNT
};
static DisplayString *s_cardText[ CARD_TEXT_COUNT ];
static const Image *s_cardPortrait = NULL;
static Color s_cardChangeColor = 0;
static Real s_cardProgress = 0.0f;		///< toward the next tier, 0 to 1
static Int s_cardTrackWidth = 0;
static Bool s_cardSending = FALSE;		///< the line says the result is on its way, over a sliding track

static void setCardText( Int which, const UnicodeString &text, GameFont *font )
{
	if( s_cardText[ which ] == NULL )
		s_cardText[ which ] = TheDisplayStringManager->newDisplayString();
	s_cardText[ which ]->setFont( font );
	s_cardText[ which ]->setText( text );
}

static void freeCardText( Int first, Int end )
{
	for( Int each = first; each < end; each++ )
	{
		if( s_cardText[ each ] && TheDisplayStringManager )
			TheDisplayStringManager->freeDisplayString( s_cardText[ each ] );
		s_cardText[ each ] = NULL;
	}
}

static void cardSize( Int which, Int &width, Int &height )
{
	width = height = 0;
	if( s_cardText[ which ] )
		s_cardText[ which ]->getSize( &width, &height );
}

/* The AI verdict on this match, by its id: the match still live as the screen opened (a quit, whose result
	 goes on the next pass), else the one whose result went on the pass that decided it.  A late verdict on
	 an earlier match names that match and is not shown. */
static Bool thisAIVerdict( OnlineAIVerdict &verdict )
{
	const AsciiString match = s_cardAIMatch.isNotEmpty() ? s_cardAIMatch : OnlineGetLastAIMatchId();
	return match.isNotEmpty() && OnlineGetAIVerdict( verdict ) && verdict.match == match;
}

static RatingFacts readRatingFacts( void )
{
	RatingFacts facts;
	OnlinePlayerInfo me;
	const Bool known = TheGameSpyInfo && OnlineGetPlayerInfo( TheGameSpyInfo->getLocalName(), me );
	AsciiString verdictLine;
	Bool sending = FALSE;
	// a Quick Match's replay always goes up, Play vs AI's only after a win
	AsciiString sendingLine( "GUI:OnlineScoreSending" );
	if( s_card == SCORE_CARD_LADDER )
	{
		facts.shown = known && me.hasRating;
		facts.after = me.shown();
		facts.elo = me.hasElo;
		facts.games = known ? me.games : -1;
		Bool rated = FALSE, ratedElo = FALSE;
		Real delta = 0.0f, rating = 0.0f;
		AsciiString reason;
		if( OnlineGetMatchResult( rated, delta, rating, reason, &ratedElo ) )
		{
			if( rated )
			{
				facts.shown = facts.moved = TRUE;
				facts.elo = ratedElo;
				facts.before = rating - delta;
				facts.after = rating;
				verdictLine = "GUI:OnlineScoreConfirmed";
			}
			else
				verdictLine = unratedKey( reason );		// the rating unchanged, no change shown
		}
		// the line waits while the report or the replay is on its way; the numbers need not
		sending = OnlineIsSendingResults();
	}
	else
	{
		facts.elo = TRUE;
		facts.shown = known && me.hasAIRating;
		facts.after = me.aiRating;
		facts.games = known ? me.aiGames : -1;
		if( !s_cardWon )
			sendingLine = "GUI:OnlineScoreSendingResult";
		OnlineAIVerdict verdict;
		if( thisAIVerdict( verdict ) )
		{
			if( verdict.rated )
			{
				facts.shown = facts.moved = TRUE;
				facts.before = verdict.before;
				facts.after = verdict.after;
				facts.games = verdict.games;
				verdictLine = "GUI:OnlineScoreAISaved";
			}
			// a win held back is with an admin, and the line never names the check that held it
			else
				verdictLine = s_cardWon ? "GUI:OnlineScoreAIHeld" : "GUI:OnlineUnrated";
		}
	}
	switch( ScoreScreen_ratingLine( verdictLine.isNotEmpty(), sending, Clock_Milliseconds() - s_cardSince ) )
	{
		case SCORE_LINE_VERDICT: facts.line = verdictLine; break;
		case SCORE_LINE_SENDING: facts.line = sendingLine; facts.sending = TRUE; break;
		case SCORE_LINE_NO_ANSWER: facts.line = "GUI:OnlineScoreNoAnswer"; break;
	}
	return facts;
}

/// the card's two lines the strings may not have yet: theirs, else the English
static UnicodeString newCardWords( const AsciiString &key )
{
	if( UIStyle::hasText( key.str() ) )
		return TheGameText->fetch( key );
	if( key == "GUI:OnlineScoreNoAnswer" )
		return UnicodeString( u"The server has not answered yet. The result will show on zerohour.gg." );
	return UnicodeString( u"Sending the result" );
}

/// the card's figures on its own scale: a whole Elo, or an older server's ladder two decimals
static UnicodeString cardRatingText( const RatingFacts &facts, Real value, Bool sign = FALSE )
{
	return UIStyle::ladderText( value, facts.elo, sign );
}

static Real cardTierStart( const RatingFacts &facts, UIStyle::RatingTier tier )
{
	return s_card == SCORE_CARD_AI ? UIStyle::aiTierStart( tier ) : UIStyle::tierStart( tier, facts.elo );
}

static void buildRatingCard( const RatingFacts &facts )
{
	freeCardText( CARD_HEADING, CARD_TEXT_COUNT );
	GameFont *body = UIStyle::font( UIStyle::TEXT_BODY );
	setCardText( CARD_HEADING, TheGameText->fetch( s_card == SCORE_CARD_AI ? "GUI:OnlineAIRatingLabel" : "GUI:OnlineLadderRatingLabel" ),
		UIStyle::sectionFont() );
	Int width = 0, height = 0;
	s_cardWindow->winGetSize( &width, &height );
	const Int pad = UIStyle::px( 24.0f );
	s_cardPortrait = NULL;
	if( facts.shown )
	{
		const UIStyle::RatingTier tier = s_card == SCORE_CARD_AI ? UIStyle::aiRatingTier( facts.after, facts.games ) :
			UIStyle::ratingTier( facts.after, facts.games, facts.elo );
		s_cardPortrait = UIStyle::tierImage( tier, s_cardSide );
		if( facts.moved )
		{
			setCardText( CARD_OLD, cardRatingText( facts, facts.before ), UIStyle::titleFont( UIStyle::TEXT_TITLE ) );
			static const WideChar ARROW[] = { 0x2192, 0 };		// as a code unit: the sources build without /utf-8
			setCardText( CARD_ARROW, UnicodeString( ARROW ), UIStyle::font( UIStyle::TEXT_HEADING ) );
			setCardText( CARD_CHANGE, cardRatingText( facts, facts.after - facts.before, TRUE ), UIStyle::font( UIStyle::TEXT_STRONG, TRUE ) );
			// a defeat's change is MUTED, as its verdict is
			s_cardChangeColor = s_cardWon ? UIStyle::changeColor( facts.after - facts.before ) : UIStyle::MUTED;
		}
		setCardText( CARD_NEW, cardRatingText( facts, facts.after ), UIStyle::titleFont( UIStyle::TEXT_DISPLAY ) );
		setCardText( CARD_TIER, OnlineTierName( AsciiString( UIStyle::tierKey( tier ) ) ), body );

		// the next tier at the card's right, as far left as the numbers leave room; none past the last
		// tier, and none for the unranked, whose next step is games played and not a rating
		Int numbersRight = pad + ( s_cardPortrait ? UIStyle::px( 126.0f ) : 0 );
		for( Int each = CARD_OLD; each <= CARD_CHANGE; each++ )
		{
			Int w, h;
			cardSize( each, w, h );
			if( s_cardText[ each ] )
				numbersRight += w + UIStyle::px( 16.0f );
		}
		const Int nextRoom = min( UIStyle::tallPx( 436.0f ), width - pad - numbersRight );
		if( tier != UIStyle::TIER_UNRANKED && tier + 1 < UIStyle::TIER_COUNT && nextRoom > 0 )
		{
			const UIStyle::RatingTier next = (UIStyle::RatingTier)( tier + 1 );
			const Real start = cardTierStart( facts, tier );
			const Real goal = cardTierStart( facts, next );
			UnicodeString words;
			words.format( TheGameText->fetch( "GUI:OnlineNextTier" ), OnlineTierName( AsciiString( UIStyle::tierKey( next ) ) ).str(),
				cardRatingText( facts, goal ).str(), cardRatingText( facts, goal - facts.after ).str() );
			setCardText( CARD_NEXT, UIStyle::fitText( words, body, nextRoom ), body );
			// private's Elo track starts where sergeant's does, as the site's meter: empty
			s_cardProgress = goal > start ? ( facts.after - start ) / ( goal - start ) : 0.0f;
			s_cardTrackWidth = min( UIStyle::px( 436.0f ), nextRoom );
		}
	}

	UnicodeString line;
	s_cardSending = facts.sending;
	if( facts.line == "GUI:OnlineScoreAISaved" )
		line.format( TheGameText->fetch( facts.line ), cardRatingText( facts, facts.after ).str() );
	else if( facts.line == "GUI:OnlineScoreNoAnswer" || facts.line == "GUI:OnlineScoreSendingResult" )
		line = newCardWords( facts.line );
	else
		line = TheGameText->fetch( facts.line );
	setCardText( CARD_LINE, UIStyle::fitText( line, body, width - pad * 2 ), body );
}

/* The card on the mockup's grid at 1080 (px), each line pushed down where the one above it runs taller,
	 as the type keeps its 1080 size on a 4:3 screen while px shrinks. */
static void drawRatingCard( GameWindow *window, WinInstanceData *instData )
{
	if( s_cardText[ CARD_HEADING ] == NULL || s_cardText[ CARD_LINE ] == NULL )
		return;
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	UIStyle::panel( x, y, width, height, UIStyle::PANEL );
	const Int pad = UIStyle::px( 24.0f );
	const Int right = x + width - pad;
	UIStyle::heading( s_cardText[ CARD_HEADING ], x + pad, y + UIStyle::px( 18.0f ) );

	Int left = x + pad;
	if( s_cardPortrait )
	{
		const Int portrait = UIStyle::px( 104.0f );
		const Int top = y + UIStyle::px( 58.0f );
		TheDisplay->drawImage( s_cardPortrait, left, top, left + portrait, top + portrait );
		left += UIStyle::px( 126.0f );
	}

	Int lineTop = y + UIStyle::px( 178.0f );
	if( s_cardText[ CARD_NEW ] )
	{
		// the numbers on one baseline, which sits about four fifths down a face's line
		const Int rowTop = y + UIStyle::px( 62.0f );
		Int w, h;
		cardSize( CARD_NEW, w, h );
		const Int newHeight = h;
		const Int baseline = rowTop + newHeight * 4 / 5;
		const Color colors[] = { UIStyle::MUTED, UIStyle::MUTED, UIStyle::INK, s_cardChangeColor };
		Int at = left;
		for( Int each = CARD_OLD; each <= CARD_CHANGE; each++ )
		{
			if( s_cardText[ each ] == NULL )
				continue;
			cardSize( each, w, h );
			s_cardText[ each ]->draw( at, baseline - h * 4 / 5, colors[ each - CARD_OLD ], UIStyle::SHADOW );
			at += w + UIStyle::px( 16.0f );
		}
		const Int tierTop = max( y + UIStyle::px( 138.0f ), rowTop + newHeight + UIStyle::px( 2.0f ) );
		s_cardText[ CARD_TIER ]->draw( left, tierTop, UIStyle::INK, UIStyle::SHADOW );
		cardSize( CARD_TIER, w, h );
		lineTop = max( lineTop, tierTop + h + UIStyle::px( 6.0f ) );
		if( s_cardText[ CARD_NEXT ] )
		{
			cardSize( CARD_NEXT, w, h );
			const Int nextTop = y + UIStyle::px( 62.0f );
			s_cardText[ CARD_NEXT ]->draw( right - w, nextTop, UIStyle::MUTED, UIStyle::SHADOW );
			const Int trackTop = max( y + UIStyle::px( 98.0f ), nextTop + h + UIStyle::px( 4.0f ) );
			UIStyle::track( right - s_cardTrackWidth, trackTop, s_cardTrackWidth, max( UIStyle::px( 10.0f ), 2 ), s_cardProgress );
		}
	}

	s_cardText[ CARD_LINE ]->draw( x + pad, lineTop, UIStyle::MUTED, UIStyle::SHADOW );
	if( s_cardSending )
	{
		// a quarter of a track sliding along it after the words: how long the send takes is not known
		Int w, h;
		cardSize( CARD_LINE, w, h );
		const Int trackLeft = x + pad + w + pad;
		const Int trackWidth = min( UIStyle::px( 300.0f ), right - trackLeft );
		const Int trackHeight = max( UIStyle::px( 10.0f ), 2 );
		if( trackWidth > trackHeight * 4 )
		{
			const Int trackTop = lineTop + ( h - trackHeight ) / 2;
			UIStyle::track( trackLeft, trackTop, trackWidth, trackHeight, 0.0f );
			const Int segment = trackWidth / 4;
			const Int period = 1500;
			const Int at = (Int)( Clock_Milliseconds() % period ) * ( trackWidth + segment ) / period - segment;
			const Int from = max( at, 0 );
			const Int to = min( at + segment, trackWidth );
			if( to > from )
				TheDisplay->drawFillRect( trackLeft + from, trackTop, to - from, trackHeight, UIStyle::EDGE );
		}
	}
}

static GameWindow *makeCardWindow( Real top, Real bottom, GameWinDrawFunc draw )
{
	// over the summary panel, which is the backdrop's last child; it shows what is under it, and takes no input
	GameWindow *window = TheWindowManager->winCreate( backdrop, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT, 0, 0, 1, 1, GameWinDefaultSystem );
	placeOnBoard( window, SCORE_LEFT, top, SCORE_LEFT_RIGHT, bottom );
	window->winSetDrawFunc( draw );
	return window;
}

/// the chat's place after an AI match, which has no chat: nothing drawn, only the floor the seats stop over
static void drawNothing( GameWindow *, WinInstanceData * )
{
}

/* The send holds Exit, the wide button and the card's Ladder or Quick Match button, every way off the
	 screen, as the "Sending Results" box held the screen: from the first time the screen sees it, while
	 it lasts, a second at least and thirty at most, and once a screen, so chat sent after it does not
	 bring it back.  Any online network game; the card's line says why. */
static void updateSendingHold( void )
{
	if( screenType != SCORESCREEN_INTERNET || !OnlineIsEnabled() || s_sendingOver )
		return;
	const Bool sending = OnlineIsSendingResults();
	if( !s_sendingSeen )
	{
		if( !sending )
			return;
		s_sendingSeen = TRUE;
		s_sendingSince = Clock_Milliseconds();
	}
	const Bool hold = ScoreScreen_sendingHolds( sending, Clock_Milliseconds() - s_sendingSince );
	s_sendingOver = !hold;
	// the release leaves them to the Save replay popup while it is up: it enables them as it closes
	WindowLayout *replayPopup = TheShell->getPopupReplayLayout();
	if( !hold && replayPopup && !replayPopup->isHidden() )
		return;
	GameWindow *held[] = { buttonOk, buttonContinue, s_card != SCORE_CARD_NONE ? buttonBuddies : NULL };
	for( Int each = 0; each < ARRAY_SIZE( held ); each++ )
	{
		if( held[ each ] )
			held[ each ]->winEnable( !hold );
	}
}

static void updateRatingCard( void )
{
	if( s_card == SCORE_CARD_NONE || s_cardWindow == NULL )
		return;
	OnlineUpdate();
	s_cardWindow->winHide( !ScoreScreenGraphsSummaryOpen() );
	const RatingFacts facts = readRatingFacts();
	if( s_cardBuilt && facts == s_cardFacts )
		return;
	s_cardFacts = facts;
	s_cardBuilt = TRUE;
	buildRatingCard( facts );
}

/** After the init of the screen's kind: the card for a Quick Match or a Play vs AI match, the row's
	* buttons as the card's. */
static void initRatingCard( void )
{
	s_card = SCORE_CARD_NONE;
	s_leave = SCORE_LEAVE_STAY;
	s_sendingSeen = s_sendingOver = FALSE;
	s_cardBuilt = FALSE;
	s_cardAIMatch.clear();
	s_cardSince = Clock_Milliseconds();
	if( !OnlineIsEnabled() )
		return;
	if( screenType == SCORESCREEN_INTERNET && TheGameSpyGame && TheGameSpyGame->isQMGame() )
		s_card = SCORE_CARD_LADDER;
	else if( isOnlineAIMatch() )
		s_card = SCORE_CARD_AI;
	else
		return;
	s_cardWon = TheVictoryConditions && TheVictoryConditions->isLocalAlliedVictory();
	Player *local = ThePlayerList->getLocalPlayer();
	s_cardSide = local ? local->getBaseSide() : AsciiString::TheEmptyString;
	if( s_card == SCORE_CARD_AI && OnlineIsAIMatchLive() )
		s_cardAIMatch = OnlineClaimAIMatch();
	ScoreScreenGraphsSubtitle( s_card == SCORE_CARD_AI ? "GUI:OnlineScoreSubAI" : "GUI:OnlineScoreSubRanked" );

	// the row: Save replay, Ladder (Quick Match after an AI match), Exit, and the wide button
	if( buttonBuddies )
	{
		buttonBuddies->winHide( FALSE );
		GadgetButtonSetText( buttonBuddies, TheGameText->fetch( s_card == SCORE_CARD_AI ? "GUI:OnlineTabQuickMatch" : "GUI:OnlineLadderButton" ) );
	}
	if( buttonContinue )
	{
		buttonContinue->winHide( FALSE );
		GadgetButtonSetText( buttonContinue, TheGameText->fetch( s_card == SCORE_CARD_AI ? "GUI:OnlineScorePlayAgain" : "GUI:OnlineScoreAgain" ) );
	}

	s_cardWindow = makeCardWindow( SCORE_RATING_TOP, SCORE_RATING_BOTTOM, drawRatingCard );
	if( s_card == SCORE_CARD_AI )
		s_aiFloor = makeCardWindow( SCORE_CHAT_TOP, SCORE_CONTENT_BOTTOM, drawNothing );
	updateRatingCard();
}

static void freeRatingCard( void )
{
	freeCardText( 0, CARD_TEXT_COUNT );
	GameWindow *made[] = { s_cardWindow, s_aiFloor };
	for( Int each = 0; each < ARRAY_SIZE( made ); each++ )
	{
		if( made[ each ] && TheWindowManager )
			TheWindowManager->winDestroy( made[ each ] );
	}
	s_cardWindow = s_aiFloor = NULL;
	s_cardPortrait = NULL;
	s_card = SCORE_CARD_NONE;
}

/* The card's buttons on the online stack, the hub at its foot.  This screen's pop left the page it
	 uncovered asleep (Quick Match after a Quick Match, the hub after Play vs AI): every page's init starts
	 the shell map, and two inits in one pass would start it twice.  So Quick Match goes without its init,
	 the match it played closed here as its init would close it, and the hub wakes or a page opens over it
	 as a tab would open it. */
static void leaveToOnlinePage( ScoreLeave leave )
{
	WindowLayout *top = TheShell->top();
	const AsciiString quickMatch( UIStyle::onlineTabScreen( UIStyle::ONLINE_TAB_QUICK_MATCH ) );
	if( top && quickMatch.compareNoCase( top->getFilename() ) == 0 )
	{
		// as WOLQuickMatchMenuInit closes it: the match over, its negotiation gone, the room's slots cleared,
		// so no card takes the finished game for the player's room
		if( TheGameSpyGame && TheGameSpyGame->isGameInProgress() )
			TheGameSpyGame->setGameInProgress( FALSE );
		if( TheNAT != NULL )
		{
			delete TheNAT;
			TheNAT = NULL;
		}
		if( TheGameSpyGame )
			TheGameSpyGame->reset();
		if( leave == SCORE_LEAVE_HUB )
		{
			TheShell->popImmediate();		// the hub's init runs
			return;
		}
		TheShell->popImmediateIntoGame();
	}
	else if( leave == SCORE_LEAVE_HUB )
	{
		TheShell->showShell();		// whatever stood under this screen, woken as a plain pop would wake it
		return;
	}
	TheShell->push( UIStyle::onlineTabScreen( leave == SCORE_LEAVE_LADDER ? UIStyle::ONLINE_TAB_LADDER : UIStyle::ONLINE_TAB_QUICK_MATCH ) );
}

/** A click on Exit, the Ladder or Quick Match button, or the wide one while the card is up. */
static void ratingCardLeave( Int controlID )
{
	// Quick Match again lands on Quick Match with its picks kept, Exit after Play vs AI on the hub: both
	// are what the pop uncovers.  Play again: that hub starts the next match as it opens.
	s_leave = SCORE_LEAVE_STAY;
	if( controlID == buttonBuddiesID )
		s_leave = s_card == SCORE_CARD_AI ? SCORE_LEAVE_QUICK_MATCH : SCORE_LEAVE_LADDER;
	else if( controlID == buttonOkID && s_card == SCORE_CARD_LADDER )
		s_leave = SCORE_LEAVE_HUB;
	else if( controlID == buttonContinueID && s_card == SCORE_CARD_AI )
		WOLWelcomeMenuStartPlayVsAIOnEntry();
	// a connection lost after the start: Quick Match's own init says so and goes back to the hub
	if( s_card == SCORE_CARD_LADDER && TheGameSpyInfo && TheGameSpyInfo->isDisconnectedAfterGameStart( NULL ) )
		s_leave = SCORE_LEAVE_STAY;
	TheShell->pop();
}

/* -online, after any other network game (a custom one): the server's verdict as one line in the chat box,
	 "ELO +12 -> 1213" (an older server's "Rating +1.3 -> 22") or why the match was unrated.  It comes a moment after both reports are in, while
	 this screen is up. */
static void showOnlineVerdict( void )
{
	if (s_onlineVerdictShown || s_card != SCORE_CARD_NONE || screenType != SCORESCREEN_INTERNET || !OnlineIsEnabled()
		|| !listboxChatWindowScoreScreen)
		return;
	OnlineUpdate();
	Bool rated = FALSE, elo = FALSE;
	Real delta = 0.0f, rating = 0.0f;
	AsciiString reason;
	if (!OnlineGetMatchResult( rated, delta, rating, reason, &elo ))
		return;
	s_onlineVerdictShown = TRUE;
	UnicodeString line;
	if (rated && elo)
		line.format( TheGameText->fetch( "GUI:OnlineEloResult" ), REAL_TO_INT( floorf( delta + 0.5f ) ), REAL_TO_INT( floorf( rating + 0.5f ) ) );
	else if (rated)
		line.format( TheGameText->fetch( "GUI:OnlineRatedResult" ), delta, REAL_TO_INT( floorf( rating + 0.5f ) ) );
	else
		line = TheGameText->fetch( unratedKey( reason ) );
	GadgetListBoxAddEntryText( listboxChatWindowScoreScreen, line, GameSpyColor[GSCOLOR_MOTD_HEADING], -1 );
}

struct ScoreGather
{
	Int m_totalMoneyEarned;						///< The total money that was harvested, refined, received in crates
	Int m_totalMoneySpent;						///< The total money spent on units, buildings, repairs
	Int m_totalUnitsDestroyed;				///< The total number of enimies that we've killed
	Int m_totalUnitsBuilt;						///< The total number of units we've created (created meaning that we built from a building)
	Int m_totalUnitsLost;							///< The total number of our units lost
	Int m_totalBuildingsDestroyed;		///< The total number of Buildings we've destroyed
	Int m_totalBuildingsBuilt;				///< The total number of buildings we've constructed
	Int m_totalBuildingsLost;					///< The total number of our buildings lost
};
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

void startNextCampaignGame(void)
{
	TheShell->popImmediateIntoGame();
	TheShell->hideShell();
	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	if (TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
	{
		DEBUG_ASSERTCRASH( TheChallengeGameInfo, ("TheChallengeGameInfo doesn't exist.\n") );
		TheChallengeGameInfo->init();  
		TheChallengeGameInfo->clearSlotList();
		TheChallengeGameInfo->reset();
		TheChallengeGameInfo->enterGame();
		TheChallengeGameInfo->setMap(TheCampaignManager->getCurrentMap());

		Int templateNum = TheChallengeGenerals->getCurrentPlayerTemplateNum();
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		GameSlot slot;
		slot.setState(SLOT_PLAYER, playerTemplate->getDisplayName());
		slot.setPlayerTemplate(templateNum);
		TheChallengeGameInfo->setSlot(0, slot);
		
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData();
	}

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	GameEngine_appendNewGameRates( msg, TheGlobalData->m_missionGameSpeedPercent );
	
	InitRandom(0);
}


void ScoreScreenEnableControls(Bool enable)
{
	// if we are using the button, do the enable thing.
	if ((buttonOk != NULL) && (buttonOk->winIsHidden() == FALSE)) {
		buttonOk->winEnable(enable);
	}

	if ((buttonContinue != NULL) && (buttonContinue->winIsHidden() == FALSE)) {
		buttonContinue->winEnable(enable);
	}

	if ((buttonBuddies != NULL) && (buttonBuddies->winIsHidden() == FALSE)) {
		buttonBuddies->winEnable(enable);
	}

	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if ((buttonSaveReplay != NULL) && (buttonSaveReplay->winIsHidden() == FALSE)) {
		if (!canSaveReplay)
			enable = FALSE;
		buttonSaveReplay->winEnable(enable);
	}
}

extern Bool DontShowMainMenu; //KRIS
Bool g_playMusic = FALSE;
Bool ReplayWasPressed = FALSE;
/** Initialize the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenInit( WindowLayout *layout, void *userData )
{
	//Play music after subsystems get reset including the audio...
	g_playMusic = TRUE;
	
	if (TheGameSpyInfo)
	{
		DEBUG_LOG(("ScoreScreenInit(): TheGameSpyInfo->stuff(%s/%s/%s)\n", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));
	}

	DontShowMainMenu = TRUE; //KRIS
	buttonIsFinishCampaign = FALSE;
	s_onlineVerdictShown = FALSE;

	//Store the keys so we have them later
	parentID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ParentScoreScreen" ) );
	buttonOkID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonOk" ) );
	textEntryChatID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:TextEntryChat" ) );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonEmote" ) );
	listboxChatWindowScoreScreenID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ListboxChatWindowScoreScreen" ) );
//	buttonRehostID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonRehost" ) );
	chatBoxBorderID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ChatBoxBorder" ) );
	buttonBuddiesID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonBuddy" ) );
	buttonContinueID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonContinue" ) );
	buttonSaveReplayID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:ButtonSaveReplay" ) );
	backdropID = TheNameKeyGenerator->nameToKey( AsciiString( "ScoreScreen.wnd:MainBackdrop" ) );

	parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	buttonOk = TheWindowManager->winGetWindowFromId( parent, buttonOkID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parent, textEntryChatID );
	buttonEmote = TheWindowManager->winGetWindowFromId( parent,buttonEmoteID  );
	listboxChatWindowScoreScreen = TheWindowManager->winGetWindowFromId( parent, listboxChatWindowScoreScreenID );

//	buttonRehost = TheWindowManager->winGetWindowFromId( parent, buttonRehostID );
	chatBoxBorder = TheWindowManager->winGetWindowFromId( parent, chatBoxBorderID );
	buttonContinue = TheWindowManager->winGetWindowFromId( parent, buttonContinueID );
	buttonBuddies = TheWindowManager->winGetWindowFromId( parent, buttonBuddiesID );
	staticTextGameSaved= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:StaticTextGameSaveComplete") );
	backdrop = TheWindowManager->winGetWindowFromId( parent, backdropID );
	challengePortrait = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:BigPortrait") );
	challengeWinLossText = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:ChallengeWinLossText") );
	challengeRemarks = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GeneralRemarks") );
	gadgetParent = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GadgetParent") );
	// get the replay filename for later (not full path)
	LastReplayFileName = TheRecorder->getLastReplayFileName().str();
	staticTextGameSaved->winHide(TRUE);
	overidePlayerDisplayName = FALSE;
	WindowLayout *replayLayout = TheShell->getPopupReplayLayout();
	if (replayLayout != NULL) {
		replayLayout->hide(TRUE);
	}
	canSaveReplay = FALSE;
	if(TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		canSaveReplay = TRUE;
	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if (TheRecorder->getMode() == RECORDERMODETYPE_NONE && buttonSaveReplay)
		buttonSaveReplay->winEnable(FALSE);

	s_needToFinishSinglePlayerInit = FALSE;
	ScoreScreenGraphsInit( backdrop );
	if (TheGameLogic->isInReplayGame())
	{
		if (buttonSaveReplay)
			buttonSaveReplay->winHide(TRUE);

		if (TheRecorder->isMultiplayer())
		{
			initReplayMultiPlayer();
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initReplaySinglePlayer();
		}
	}
	else
	{
		if(TheGameLogic->isInInternetGame())
		{
			initInternetMultiPlayer();
		}
		else if( TheGameLogic->isInLanGame())
		{
			initLANMultiPlayer();
		}
		else if( TheGameLogic->isInSkirmishGame())
		{
			initSkirmish();
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initSinglePlayer();
			if (buttonSaveReplay)
				buttonSaveReplay->winHide(TRUE);
		}
	}

	// challenge maps have no replay (design and technical issues)
	Bool isChallengeCampaign = TheCampaignManager->getCurrentCampaign() ? TheCampaignManager->getCurrentCampaign()->isChallengeCampaign() : FALSE;
	if (isChallengeCampaign && buttonSaveReplay)
	{
		buttonSaveReplay->winEnable(FALSE);
		buttonSaveReplay->winHide(TRUE);
	}

	// -online: the rating card after a Quick Match or Play vs AI, and the row's buttons for it
	initRatingCard();

	// The panel is the whole score screen; the layout's own table and its Tactical Analysis box stay
	// hidden.  A General's Challenge mission keeps its portrait of the general instead, which
	// finishSinglePlayerInit puts where the table was.  The panel stops above the chat after a
	// network match and above the "game saved" line after a mission.
	hideScoreTable();
	if (!isChallengeCampaign)
		placeButtonRow();
	if (screenType == SCORESCREEN_SINGLEPLAYER)
	{
		if (!isChallengeCampaign)
			ScoreScreenGraphsShow(staticTextGameSaved, FALSE);
	}
	else if (s_aiFloor)
		ScoreScreenGraphsShow(s_aiFloor, TRUE);		// the seats stop over the chat's place as after a network match
	else if (listboxChatWindowScoreScreen->winIsHidden())
		ScoreScreenGraphsShow(NULL, FALSE);
	else
		ScoreScreenGraphsShow(listboxChatWindowScoreScreen, TRUE);

	// Make Sure the layout is visible
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );
	ReplayWasPressed = FALSE;
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}
	
}

void FixupScoreScreenMovieWindow( void )
{
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}
}

/** Shutdown the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenShutdown( WindowLayout *layout, void *userData )
{
	DontShowMainMenu = FALSE; //KRIS
	ScoreScreenGraphsShutdown();
	freeRatingCard();
	const ScoreLeave leave = TheGameEngine != NULL && !TheGameEngine->getQuitting() ? s_leave : SCORE_LEAVE_STAY;
	s_leave = SCORE_LEAVE_STAY;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete; where the card's buttons go elsewhere, the page uncovered stays asleep
	TheShell->shutdownComplete( layout, leave != SCORE_LEAVE_STAY );

	// Stopped, not faded.  A fade steps once per audio update, and what comes next is a map load
	// (the shell map, or the next mission) that runs no audio update until it is done.  The score
	// music held its volume through the whole load and did all two seconds of its fade on top of
	// the track the shell map's first script frame starts, so both played at once.
	TheAudio->removeAudioEvent( AHSV_StopTheMusic );
	// So the stop lands at the click, before the load rather than after it.  Not on the way out
	// of the program: quitting with this screen up runs this from the shell's destructor, after
	// the terrain and the view the listener update reads are already freed.
	if( TheGameEngine != NULL && !TheGameEngine->getQuitting() )
		TheAudio->update();

	if( leave != SCORE_LEAVE_STAY )
		leaveToOnlinePage( leave );
}

/** Update the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenUpdate( WindowLayout * layout, void *userData)
{
	WindowLayout *popupReplayLayout = TheShell->getPopupReplayLayout();
	if (popupReplayLayout != NULL) {
		if (popupReplayLayout->isHidden() == FALSE) {
			PopupReplayUpdate(popupReplayLayout, NULL);
		}
	}

	if (s_needToFinishSinglePlayerInit)
	{
		finishSinglePlayerInit();
		s_needToFinishSinglePlayerInit = FALSE;
	}

	showOnlineVerdict();
	updateSendingHold();
	updateRatingCard();

	//TheGameLogic->clearGameData() gets called after ScoreScreenInit and before the
	//first ScoreScreenUpdate(), so it was creatively moved here so we can actually
	//hear the music play.
	if( TheGameInfo && g_playMusic )
	{
		g_playMusic = FALSE;
		
		GameSlot *lSlot = TheGameInfo->getSlot(TheGameInfo->getLocalSlotNum());
		const PlayerTemplate* pt;

		if (lSlot && lSlot->getPlayerTemplate() >= 0)
			pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
		else
			pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
		AsciiString musicName = pt->getScoreScreenMusic();
		if ( !musicName.isEmpty() )
		{
			TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
			AudioEventRTS event( musicName );
			event.setShouldFade( TRUE );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();//Since GameEngine::update() is suspended until after I am gone... 
		}


	}
}

/** Input function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenInput( GameWindow *window, UnsignedInt msg,
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
					// not while the send holds the button
					if( BitTest( state, KEY_STATE_UP ) && ( buttonOk == NULL || buttonOk->winGetEnabled() ) )
					{

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)buttonOk, buttonOkID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end MainMenuInput

/** System Function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg ) 
	{
		// --------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;
		}  // end input

		// --------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			ReplayWasPressed = FALSE;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			if( s_card != SCORE_CARD_NONE && ( controlID == buttonOkID || controlID == buttonContinueID || controlID == buttonBuddiesID ) )
				ratingCardLeave( controlID );
			else if( controlID == buttonOkID )
			{
				TheShell->pop();
				TheCampaignManager->setCampaign(AsciiString::TheEmptyString);
			}
			else if ( controlID == buttonContinueID )	
			{
				if(!buttonIsFinishCampaign)
					ReplayWasPressed = TRUE;
				if( screenType == SCORESCREEN_SINGLEPLAYER)	
				{
					AsciiString mapName = TheCampaignManager->getCurrentMap();

					if( mapName.isEmpty() )
					{
						ReplayWasPressed = FALSE;
						TheShell->pop();
					}
					else
					{
						CheckForCDAtGameStart( startNextCampaignGame );
					}
				}
			}
			else if ( controlID == buttonBuddiesID )	
			{
				GameSpyToggleOverlay( GSOVERLAY_BUDDY );
			}
			else if ( controlID == buttonSaveReplayID )
			{
				ScoreScreenEnableControls(FALSE);
        WindowLayout *saveReplayLayout = TheShell->getPopupReplayLayout();
				DEBUG_ASSERTCRASH( saveReplayLayout, ("Unable to get save replay menu layout.\n") );
				saveReplayLayout->runInit();
				saveReplayLayout->hide( FALSE );
				saveReplayLayout->bringForward();
			}

			else if ( controlID == buttonEmoteID )
			{
				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				if (!txtInput.isEmpty())
					if(TheLAN)
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
					//add the gamespy chat request here
			} //if ( controlID == buttonEmote )
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				AsciiString name;
				name.format("ScoreScreen.wnd:ButtonAdd%d", i);
				if( controlID == TheNameKeyGenerator->nameToKey(name))
				{
					Bool notBuddy = TRUE;
					Int playerID = (Int)(intptr_t)GadgetButtonGetData(TheWindowManager->winGetWindowFromId(NULL,controlID));
											// request to add a buddy
					BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator bIt;
					if( playerID > 0)
					{
						bIt = buddies->find(playerID);
						if (bIt != buddies->end())
						{
							notBuddy = FALSE;
						}
					}
					if(notBuddy)
					{
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_ADDBUDDY;
						req.arg.addbuddy.id = playerID;
						UnicodeString buddyAddstr;
						buddyAddstr = TheGameText->fetch("GUI:BuddyAddReq");
						WideCharNCpy(req.arg.addbuddy.text, buddyAddstr.str(), MAX_BUDDY_CHAT_LEN);
						req.arg.addbuddy.text[MAX_BUDDY_CHAT_LEN-1] = 0;
						TheGameSpyBuddyMessageQueue->addRequest(req);
					}
					break;
				}
			}
		}

		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			// Take the user's input and echo it into the chat window as well as
			// send it to the other clients on the lan
			if ( controlID == textEntryChatID )
			{
				
				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				if (!txtInput.isEmpty())
					if(TheLAN)
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);
					//add the gamespy chat request here

			}// if ( controlID == textEntryChatID )
		}
	}		
	return MSG_HANDLED;
}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

/** Special Init path for making this a single player Score Screen */
//-------------------------------------------------------------------------------------------------
void initSkirmish( void )
{
	screenType = SCORESCREEN_SKIRMISH;
	grabMultiPlayerInfo();
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

void PlayMovieAndBlock(AsciiString movieTitle)
{
	VideoStreamInterface *videoStream = TheVideoPlayer->open( movieTitle );
	if ( videoStream == NULL )
	{
		return;
	}

	// Create the new buffer
	VideoBuffer *videoBuffer = TheDisplay->createVideoBuffer();
	if (	videoBuffer == NULL || 
				!videoBuffer->allocate(	videoStream->width(), 
													videoStream->height())
		)
	{
		delete videoBuffer;
		videoBuffer = NULL;

		if ( videoStream )
			videoStream->close();
		videoStream = NULL;

		return;
	}

	GameWindow *movieWindow = s_blankLayout->getFirstWindow();
	TheWritableGlobalData->m_loadScreenRender = TRUE;
	while (videoStream->frameIndex() < videoStream->frameCount() - 1)
	{
		TheGameEngine->serviceWindowsOS();

		if(!videoStream->isFrameReady())
		{
			sleepMilliseconds( 1 );
			continue;
		}

		if (!TheGameEngine->isActive())
		{	//we are alt-tabbed out, so just increment the frame
			videoStream->frameNext();
			videoStream->frameDecompress();
			continue;
		}
		
		videoStream->frameDecompress();
		videoStream->frameRender(videoBuffer);
		videoStream->frameNext();

		if(videoBuffer)
			movieWindow->winGetInstanceData()->setVideoBuffer(videoBuffer);

		//TheWindowManager->update();

		TheDisplay->draw();
	}
	TheWritableGlobalData->m_loadScreenRender = FALSE;
	movieWindow->winGetInstanceData()->setVideoBuffer(NULL);
	if (videoBuffer)
	{
		delete videoBuffer;
		videoBuffer = NULL;
	}
	if (videoStream)
	{
		videoStream->close();
		videoStream = NULL;
	}

	setFPMode();
}

void initSinglePlayer( void )
{
	screenType = SCORESCREEN_SINGLEPLAYER;
	TheCampaignManager->setRankPoints(ThePlayerList->getLocalPlayer()->getSkillPoints());
	TheCampaignManager->setGameDifficulty(TheScriptEngine->getGlobalDifficulty());
	grabSinglePlayerInfo();
	s_needToFinishSinglePlayerInit = TRUE;
	s_blankLayout = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(s_blankLayout,("We Couldn't Load Menus/BlankWindow.wnd"));
	s_blankLayout->hide(FALSE);
	s_blankLayout->bringForward();
	s_blankLayout->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
}

void displayChallengeWinLoss( const Image *imageGeneral, const UnicodeString strHeader, const UnicodeString strRemarks )
{
	// format the display for challenge mode win/loss
	backdrop->winHide(TRUE);
	gadgetParent->winHide(TRUE);	
	challengeWinLossText->winHide(FALSE);
	challengeRemarks->winHide(FALSE);
	challengePortrait->winHide(FALSE);
	parent->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("GeneralsChallengeWinLoss"));

	// display the defeated enemy general
	challengePortrait->winSetEnabledImage(0, imageGeneral);
	GadgetStaticTextSetText(challengeWinLossText, strHeader);
	GadgetStaticTextSetText(challengeRemarks, strRemarks);
}

void finishSinglePlayerInit( void )
{
	Bool copyProtectOK = TRUE;
#ifdef DO_COPY_PROTECTION
	copyProtectOK = CopyProtect::validate();
#endif
	if(copyProtectOK && TheCampaignManager->isVictorious())
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralDefeated = general->getImageDefeated();
			const UnicodeString strGeneralDefeated = TheGameText->fetch(general->getStringDefeated());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeWinText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralDefeated, strHeader, strGeneralDefeated);

			AudioEventRTS event( general->getWinSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		TheCampaignManager->gotoNextMission();

		if(TheCampaignManager->getCurrentMap().isEmpty())
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:EndCampaign"));
			buttonIsFinishCampaign = TRUE;
			// mark us as having completed the campaign
			Campaign* campaign = TheCampaignManager->getCurrentCampaign();
			if (campaign)
			{
				GameDifficulty difficulty = TheCampaignManager->getGameDifficulty();
				SkirmishBattleHonors stats;
				// an original Generals campaign earns its side's honour too
				const AsciiString side = campaign->getSide();
				if (side.compareNoCase("USA") == 0)
				{
					stats.setUSACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_USA);
				}

				if (side.compareNoCase("China") == 0)
				{
					stats.setCHINACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_CHINA);
				}

				if (side.compareNoCase("GLA") == 0)
				{
					stats.setGLACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_GLA);
				}

				for (int i = 0; i < MAX_GLOBAL_GENERAL_TYPES; ++i)
				{
					char campaignName[128];
					sprintf(campaignName, "CHALLENGE_%d", i);
					if (campaign->m_name.compareNoCase(campaignName) == 0)
					{
						stats.setChallengeCampaignComplete(i, difficulty);
						stats.setHonors(BATTLE_HONOR_CHALLENGE_MODE);
					}
				}

				stats.write();

				if (buttonOk)
					buttonOk->winHide(TRUE);
				if (buttonContinue)
					buttonContinue->winHide(TRUE);
				if (textEntryChat)
					textEntryChat->winHide(TRUE);
				if (buttonEmote)
					buttonEmote->winHide(TRUE);
				if (listboxChatWindowScoreScreen)
					listboxChatWindowScoreScreen->winHide(TRUE);
				if (chatBoxBorder)
					chatBoxBorder->winHide(TRUE);
				if (buttonBuddies)
					buttonBuddies->winHide(TRUE);
				if (campaign->getFinalVictoryMovie().isNotEmpty())
				{
					AsciiString vidName;
					vidName = campaign->getFinalVictoryMovie();
					Bool useLowRes = FALSE;
					if (TheGameLODManager) {
						if (!TheGameLODManager->didMemPass()) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->findStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->getStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
					}
					if(!useLowRes)
						PlayMovieAndBlock(vidName);
				}
			}
		}
		else 
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:SaveAndContinue"));
			
			// auto save game
			TheGameState->missionSave();
			if(staticTextGameSaved)
				staticTextGameSaved->winHide(FALSE);
		}
	}
	else
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralVictorious = general->getImageVictorious();
			const UnicodeString strGeneralVictorious = TheGameText->fetch(general->getStringVictorious());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeLossText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralVictorious, strHeader, strGeneralVictorious);

			AudioEventRTS event( general->getLossSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:Retry"));

	}

	//Added By Sadullah Nader
	//Fix for the black screen text that appears after loading sequence

	TheInGameUI->freeMessageResources();

	//
	s_blankLayout->destroyWindows();
	s_blankLayout->deleteInstance();
	s_blankLayout = NULL;

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );

	if (buttonOk)
		buttonOk->winHide(FALSE);
	if (buttonContinue)
		buttonContinue->winHide(FALSE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);

	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);

	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);

	// Continue is back, so the row is laid out again; a Challenge keeps its own page
	if (!TheCampaignManager->getCurrentCampaign() || !TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		placeButtonRow();
}

/** Special Init path for making this a single player replay Score Screen */
//-------------------------------------------------------------------------------------------------
void initReplaySinglePlayer( void )
{
	screenType = SCORESCREEN_REPLAY;
	grabSinglePlayerInfo();
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);

//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

/** Special Init path for making this a Multiplayer Score Screen(LAN) */
//-------------------------------------------------------------------------------------------------
void initLANMultiPlayer(void)
{
	screenType = SCORESCREEN_LAN;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(FALSE);
	if (buttonEmote)
		buttonEmote->winHide(FALSE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(FALSE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(FALSE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
}

/** Special Init path for making this a Multiplayer Score Screen(Internet) */
//-------------------------------------------------------------------------------------------------
void initInternetMultiPlayer(void)
{
	screenType = SCORESCREEN_INTERNET;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(FALSE);

	if (chatBoxBorder)
		chatBoxBorder->winHide(FALSE);

	if(TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() ==0)
		buttonBuddies->winHide(TRUE);
	else
		buttonBuddies->winHide(FALSE);

	if (!TheGameSpyBuddyMessageQueue)
		return;
	BuddyRequest req;
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_SETSTATUS;
	req.arg.status.status = GP_ONLINE;
	strcpy(req.arg.status.statusString, "Online");
	strcpy(req.arg.status.locationString, "");
	TheGameSpyBuddyMessageQueue->addRequest(req);
}

/** Special Init path for making this a Multiplayer Score Screen(Replay) */
//-------------------------------------------------------------------------------------------------
void initReplayMultiPlayer(void)
{
	screenType = SCORESCREEN_REPLAY;
	grabMultiPlayerInfo();
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}


static Bool isSlotLocalAlly(GameInfo *game, const GameSlot *slot)
{
	const GameSlot *localSlot = game->getConstSlot(game->getLocalSlotNum());
	if (!localSlot)
		return TRUE;

	if (slot == localSlot)
		return TRUE;

	if (slot->getTeamNumber() < 0)
		return FALSE;

	return slot->getTeamNumber() == localSlot->getTeamNumber();
}

static void updateSkirmishBattleHonors(SkirmishBattleHonors& stats)
{
	DEBUG_LOG(("Updating Skirmish battle honors\n"));
	Player *localPlayer = ThePlayerList->getLocalPlayer();
	ScoreKeeper *s = localPlayer->getScoreKeeper();

	if (stats.getWinStreak() >= 5)
		stats.setHonors(BATTLE_HONOR_STREAK);

	// For Apocalypse Honor, see if the player has used each category of super weapon.
	const ThingTemplate *pTemplate = TheThingFactory->findTemplate("GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Boss_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Chem_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Slth_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Demo_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();

	pTemplate = TheThingFactory->findTemplate("AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("AirF_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("Lazr_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("SupW_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("Boss_ParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();

	pTemplate = TheThingFactory->findTemplate("ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Boss_NuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Infa_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Nuke_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Tank_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	if (stats.builtNuke() && stats.builtParticleCannon() && stats.builtSCUD())
		stats.setHonors(BATTLE_HONOR_APOCALYPSE);

	KindOfMaskType validMask, invalidMask;
	validMask.set(KINDOF_VEHICLE);
	invalidMask.set(KINDOF_AIRCRAFT);
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 50)
		stats.setHonors(BATTLE_HONOR_BATTLE_TANK);

	validMask.clear();
	validMask.set(KINDOF_AIRCRAFT);
	invalidMask.clear();
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 20)
		stats.setHonors(BATTLE_HONOR_AIR_WING);

	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 5)
	{
		stats.setHonors(BATTLE_HONOR_BLITZ5);
	}

	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 10)
	{
		stats.setHonors(BATTLE_HONOR_BLITZ10);
	}

	if (stats.getNumGamesLoyal() >= 20 && localPlayer->getSide() == "America")
		stats.setHonors(BATTLE_HONOR_LOYALTY_USA);

	if (stats.getNumGamesLoyal() >= 20 && 	localPlayer->getSide() == "China")
		stats.setHonors(BATTLE_HONOR_LOYALTY_CHINA);

	if (stats.getNumGamesLoyal() >= 20 && 	localPlayer->getSide() == "GLA")
		stats.setHonors(BATTLE_HONOR_LOYALTY_GLA);

	// endurance medal(s)
	Int numEasy = 0;
	Int numMedium = 0;
	Int numBrutal = 0;
	Bool anyAlliedAI = FALSE;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		if (slot->isAI() && !isSlotLocalAlly(TheGameInfo, slot))
		{
			SlotState st = TheGameInfo->getConstSlot(i)->getState();
			if (st == SLOT_EASY_AI)
				++numEasy;
			if (st == SLOT_MED_AI)
				++numMedium;
			if (st == SLOT_BRUTAL_AI)
				++numBrutal;
		}
		else if (slot->isAI())
		{
			// can't get challenge medals with AI helpers
			anyAlliedAI = TRUE;
		}
	}
	if (/*!anyAlliedAI &&*/ (numEasy || numMedium || numBrutal))
	{
		Int oldEasy = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_EASY_AI);
		Int oldMedium = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_MED_AI);
		Int oldBrutal = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_BRUTAL_AI);
		if (numEasy)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_EASY_AI, max(oldEasy, numEasy + numMedium + numBrutal));
		}
		if (numMedium)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_MED_AI, max(oldMedium, numMedium + numBrutal));
		}
		if (numBrutal)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_BRUTAL_AI, max(oldBrutal, numBrutal));
		}
	}
}

static void updateMPBattleHonors(Int& honors, PSPlayerStats& stats)
{
	DEBUG_LOG(("Updating MP battle honors\n"));
	Player *localPlayer = ThePlayerList->getLocalPlayer();
	ScoreKeeper *s = localPlayer->getScoreKeeper();

	//	BATTLE_HONOR_STREAK 				= 0x0000002,
	if (stats.winsInARow >= 5)
		honors |= BATTLE_HONOR_STREAK;

	//	BATTLE_HONOR_LOYALTY_USA		= 0x0000020,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "America")
		honors |= BATTLE_HONOR_LOYALTY_USA;

	//	BATTLE_HONOR_LOYALTY_CHINA	= 0x0000040,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "China")
		honors |= BATTLE_HONOR_LOYALTY_CHINA;

	//	BATTLE_HONOR_LOYALTY_GLA		= 0x0000060,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "GLA")
		honors |= BATTLE_HONOR_LOYALTY_GLA;

	//	BATTLE_HONOR_BATTLE_TANK		= 0x0000080,
	KindOfMaskType validMask, invalidMask;
	validMask.set(KINDOF_VEHICLE);
	invalidMask.set(KINDOF_AIRCRAFT);
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 50)
		honors |= BATTLE_HONOR_BATTLE_TANK;

	//	BATTLE_HONOR_AIR_WING				= 0x0000100,
	validMask.clear();
	validMask.set(KINDOF_AIRCRAFT);
	invalidMask.clear();
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 20)
		honors |= BATTLE_HONOR_AIR_WING;

	if (stats.builtNuke && stats.builtParticleCannon && stats.builtSCUD)
		honors |= BATTLE_HONOR_APOCALYPSE;

	//	BATTLE_HONOR_BLITZ5					= 0x0008000,
	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 5)
	{
		honors |= BATTLE_HONOR_BLITZ5;
	}

	//	BATTLE_HONOR_BLITZ10				= 0x0008000,
	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 10)
	{
		honors |= BATTLE_HONOR_BLITZ10;
	}

	//	BATTLE_HONOR_GLOBAL_GENERAL
	Int numGlobalChallengeWins = 0;
	for (int i = GLOBAL_GENERAL_BEGIN; i <= GLOBAL_GENERAL_END; ++i)
	{
		PerGeneralMap::const_iterator pit = stats.wins.find(i);
		if (pit != stats.wins.end())
		{
			if (pit->second > 0)
			{
				++numGlobalChallengeWins;
			}
		}
	}

	if (numGlobalChallengeWins >= MAX_GLOBAL_GENERAL_TYPES)
	{
		honors |= BATTLE_HONOR_GLOBAL_GENERAL;
	}

	/*  NOT IMPLEMENTED YET */
	//	BATTLE_HONOR_LADDER_CHAMP		= 0x0000001, (IGNORED HERE)
	//	BATTLE_HONOR_SPECIAL_FORCES	= 0x0000200, (Not Valid)
	//	BATTLE_HONOR_ENDURANCE			= 0x0000400,
	//	BATTLE_HONOR_CAMPAIGN_USA		= 0x0000800,
	//	BATTLE_HONOR_CAMPAIGN_CHINA	= 0x0001000,
	//	BATTLE_HONOR_CAMPAIGN_GLA  	= 0x0002000,
}

// challenge medals are beating 1-7 Brutal AIs
static void updateChallengeMedals(Int& medals)
{
	if (!TheGameInfo->isSkirmish())
		return;

	Int numAIs = 0;
	Int numBrutals = 0;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		if (slot->isAI() && !isSlotLocalAlly(TheGameInfo, slot))
		{
			++numAIs;
			if (TheGameInfo->getConstSlot(i)->getState() == SLOT_BRUTAL_AI)
				++numBrutals;
		}
		else if (slot->isAI())
		{
			// can't get challenge medals with AI helpers
			return;
		}
	}

	if (numAIs)
	{
		switch(numBrutals)
		{
		case 1:
			medals |= BH_CHALLENGE_MASK_1;
			break;
		case 2:
			medals |= BH_CHALLENGE_MASK_2;
			break;
		case 3:
			medals |= BH_CHALLENGE_MASK_3;
			break;
		case 4:
			medals |= BH_CHALLENGE_MASK_4;
			break;
		case 5:
			medals |= BH_CHALLENGE_MASK_5;
			break;
		case 6:
			medals |= BH_CHALLENGE_MASK_6;
			break;
		case 7:
			medals |= BH_CHALLENGE_MASK_7;
			break;
		}
	}
}

static inline int CheckForApocalypse( ScoreKeeper *s, const char* szWeapon )
{
	const ThingTemplate* pTemplate = TheThingFactory->findTemplate(szWeapon);
	if( s->getTotalObjectsBuilt(pTemplate) > 0 )
		return 1;
	return 0;
}

/** Populate the various windows with the information about the game based on each player's score
		keeper. */
//-------------------------------------------------------------------------------------------------
void populatePlayerInfo( Player *player, Int pos)
{
	if(!player || pos > MAX_SLOTS)
		return;
	Color color = clientPlayerColor( player );
	ScoreKeeper *scoreKpr = player->getScoreKeeper();
	if(!scoreKpr)
	{
		DEBUG_ASSERTCRASH(FALSE,("Player %s does not have a scoreKeeper", player->getPlayerDisplayName().str()));
		return;
	}
	UnicodeString name = overidePlayerDisplayName ? TheGameText->fetch("GUI:Player") : player->getPlayerDisplayName();
	if( isOnlineAIMatch() && player->getPlayerType() == PLAYER_COMPUTER )
		name = TheGameText->fetch( "GUI:OnlineComputerLabel" );		// Play vs AI's opponent, whose skill is its rating
	ScoreScreenGraphsAddPlayer( player, name, color );

	if ( screenType == SCORESCREEN_SKIRMISH && player->isLocalPlayer() )
	{
		if (TheGameInfo->isSandbox() || !(TheVictoryConditions->isLocalAlliedDefeat() || TheVictoryConditions->isLocalAlliedVictory()))
		{
			// If we died, and are watching sparring AIs, we still get the loss.
			if (player->isPlayerActive())
			{
				DEBUG_LOG(("Skipping skirmish stats update: sandbox:%d defeat:%d victory:%d\n",
					TheGameInfo->isSandbox(), TheVictoryConditions->isLocalAlliedDefeat(), TheVictoryConditions->isLocalAlliedVictory()));
				return;
			}
		}

		SkirmishBattleHonors stats;

		if (TheVictoryConditions->isLocalAlliedVictory())
		{
			stats.setWins(stats.getWins()+1);
			stats.setWinStreak(stats.getWinStreak()+1);
			stats.setBestWinStreak(max(stats.getBestWinStreak(), stats.getWinStreak()));
			updateSkirmishBattleHonors(stats);
			Int challengeMedals = stats.getChallengeMedals();
			updateChallengeMedals(challengeMedals);
			stats.setChallengeMedals(challengeMedals);
		}
		else
		{
			stats.setLosses(stats.getLosses()+1);
			stats.setWinStreak(0);
		}

		AsciiString lastGeneral = stats.getLastGeneral();
		stats.setLastGeneral(player->getPlayerTemplate()->getSide());
		if (lastGeneral != stats.getLastGeneral())
		{
			stats.setNumGamesLoyal(0);
		}
		else
		{
			stats.setNumGamesLoyal(stats.getNumGamesLoyal()+1);
		}

		stats.write();
	}

	if ( screenType == SCORESCREEN_INTERNET )
	{
		DEBUG_LOG(("populatePlayerInfo() - SCORESCREEN_INTERNET\n"));
		if (TheGameSpyGame && !TheGameSpyGame->getUseStats()
		 && !TheGameSpyGame->isQMGame() )  //QuickMatch games always record stats
			return;	//the host has requested not to record stats for this game.

		Int localID = TheGameSpyInfo->getLocalProfileID();
		if (localID)
		{
			Int localSlotNum = TheGameSpyGame->getLocalSlotNum();
			if (player->isLocalPlayer())
			{
				GameSpyGameSlot *localSlot = TheGameSpyGame->getGameSpySlot(localSlotNum);
				if (localSlot)
				{
					if (TheVictoryConditions->amIObserver())
					{
						// nothing to track
						DEBUG_LOG(("populatePlayerInfo() - not tracking stats for observer\n"));
						return;
					}

					PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(localID);

					UnsignedInt latestHumanInGame = 0;
					UnsignedInt lastFrameOfGame = 0;
					Bool gameEndedInDisconnect = TRUE;
					Bool sawAnyDisconnects = FALSE;
					Bool anyNonAI = FALSE;
					Bool anyAI = FALSE;
					for (Int i=0; i<MAX_SLOTS; ++i)
					{
						const GameSlot *slot = TheGameInfo->getConstSlot(i);
						if (slot->isOccupied() && i != localSlotNum )
						{
							if( slot->isAI() )
								anyAI = TRUE;
							else
								anyNonAI = TRUE;
						}
						if (slot->isOccupied())
						{
							lastFrameOfGame = max(lastFrameOfGame, slot->lastFrameInGame());
						}
						if (slot->isHuman())
						{
							if (i != localSlotNum)
							{
								latestHumanInGame = max(latestHumanInGame, slot->lastFrameInGame());
							}
						}
					}
					DEBUG_LOG(("Game ended on frame %d - TheGameLogic->getFrame()=%d\n", lastFrameOfGame-1, TheGameLogic->getFrame()-1));
					// i is used after the loop; VC6 for-scope let it escape.
					Int i;
					for (i =0; i<MAX_SLOTS; ++i)
					{
						const GameSlot *slot = TheGameInfo->getConstSlot(i);
						DEBUG_LOG(("latestHumanInGame=%d, slot->isOccupied()=%d, slot->disconnected()=%d, slot->isAI()=%d, slot->lastFrameInGame()=%d\n",
							latestHumanInGame, slot->isOccupied(), slot->disconnected(), slot->isAI(), slot->lastFrameInGame()));
						if (slot->isOccupied() && slot->disconnected())
						{
							DEBUG_LOG(("Marking game as a possible disconnect game\n"));
							sawAnyDisconnects = TRUE;
						}
						if (slot->isOccupied() && !slot->disconnected() && i != localSlotNum &&
							(slot->isAI() || (slot->lastFrameInGame() >= lastFrameOfGame/*TheGameLogic->getFrame()*/-1)))
						{
							DEBUG_LOG(("Marking game as not ending in disconnect\n"));
							gameEndedInDisconnect = FALSE;
						}
					}

					if (!sawAnyDisconnects)
					{
						DEBUG_LOG(("Didn't see any disconnects - making gameEndedInDisconnect == FALSE\n"));
						gameEndedInDisconnect = FALSE;
					}

					if (gameEndedInDisconnect)
					{
						if (latestHumanInGame == TheNetwork->getPingFrame())
						{
							// we pinged on the last frame someone was there - i.e. game ended in a disconnect.
							// check if we were to blame.
							if (TheNetwork->getPingsRecieved() < max(1, TheNetwork->getPingsSent()/2)) /// @todo: what's a good percent of pings to have gotten?
							{
								DEBUG_LOG(("We were to blame.  Leaving gameEndedInDisconnect = true\n"));
							}
							else
							{
								DEBUG_LOG(("We were not to blame.  Changing gameEndedInDisconnect = false\n"));
								gameEndedInDisconnect = FALSE;
							}
						}
						else
						{
							DEBUG_LOG(("gameEndedInDisconnect, and we didn't ping on last frame.  What's up with that?\n"));
						}
					}
					
					if (!anyNonAI)
					{
						// play against all ai players -- no stats to gather.
						return;
					}

					if( anyAI )
					{
						// Holy mis-implemented, Batman, You get no stats for _any_ AI, not _all_ AI.  
						// No wonder we fired the whole department.
						return;
					}

 					//Remove the extra disconnection we add to all games when they start.
					DEBUG_LOG(("populatePlayerInfo() - removing extra disconnect\n"));
 					if (TheGameSpyInfo)
						TheGameSpyInfo->updateAdditionalGameSpyDisconnections(-1);

					Bool sawEndOfGame = FALSE;
					if (TheVictoryConditions->isLocalAlliedDefeat() || TheVictoryConditions->isLocalAlliedVictory())
					{
						sawEndOfGame = TRUE;
					}
					if (TheVictoryConditions->isLocalDefeat())
					{
						sawEndOfGame = TRUE;
					}
					if (TheNetwork->sawCRCMismatch() || gameEndedInDisconnect)
					{
						sawEndOfGame = TRUE;
					}
					if (!sawEndOfGame)
					{
						DEBUG_LOG(("Not sending results - we didn't finish a game. %d\n", TheVictoryConditions->getEndFrame() ));
						return;
					}

					// send ladder results (even if we end the game in the first N seconds).  -online has no
					// ladder address: the service takes every match's report and decides what is rated.
					if (OnlineIsEnabled())
					{
						TheGameSpyGame->sendOnlineReport( "score" );
					}
					else if (TheGameSpyGame->getLadderPort() && TheGameSpyGame->getLadderIP().isNotEmpty())
					{
						GameResultsRequest gameResReq;
						gameResReq.hostname = TheGameSpyGame->getLadderIP().str();
						gameResReq.port = TheGameSpyGame->getLadderPort();
						gameResReq.results = TheGameSpyGame->generateLadderGameResultsPacket().str();
						DEBUG_ASSERTCRASH(TheGameResultsQueue, ("No Game Results queue!\n"));
						if (TheGameResultsQueue)
						{
							TheGameResultsQueue->addRequest(gameResReq);
						}
					}
					if (TheVictoryConditions->getEndFrame() < LOGICFRAMES_PER_SECOND * 25 && TheVictoryConditions->getEndFrame())
					{
 						return;
					}
					// generate and send a gameres packet
					AsciiString resultsPacket = TheGameSpyGame->generateGameSpyGameResultsPacket();
					DEBUG_LOG(("About to send results packet: %s\n", resultsPacket.str()));
					PSRequest grReq;
					grReq.requestType = PSRequest::PSREQUEST_SENDGAMERESTOGAMESPY;
					grReq.results = resultsPacket.str();
					TheGameSpyPSMessageQueue->addRequest(grReq);

					Int ptIdx;
					const PlayerTemplate *myTemplate = player->getPlayerTemplate();
					DEBUG_LOG(("myTemplate = %X(%s)\n", myTemplate, myTemplate->getName().str()));
					for (ptIdx = 0; ptIdx < ThePlayerTemplateStore->getPlayerTemplateCount(); ++ptIdx)
					{
						const PlayerTemplate *nthTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(ptIdx);
						DEBUG_LOG(("nthTemplate = %X(%s)\n", nthTemplate, nthTemplate->getName().str()));
						if (nthTemplate == myTemplate)
						{
							break;
						}
					}

					if (stats.id == 0)
					{
						// we haven't gotten stats for ourselves yet.  Bummer.
						// what we'll do is just update our disconnects in the registry if we disconnected.
						// other than that, there's not much we can do.  :P
						if (gameEndedInDisconnect || TheNetwork->sawCRCMismatch())
						{
							/* @todo: this the right way
							UnsignedInt discons = 0;
							UnsignedInt syncs = 0;
							GetUnsignedIntFromRegistry("", "dc", discons);
							GetUnsignedIntFromRegistry("", "se", sync);
							++discons;
							++syncs;
							SetUnsignedIntInRegistry("", "dc", discons);
							SetUnsignedIntInRegistry("", "se", syncs);
							*/
							DEBUG_LOG(("populatePlayerInfo() - need to save off info for disconnect games!\n"));

							PSRequest req;
							req.requestType = PSRequest::PSREQUEST_UPDATEPLAYERSTATS;
							req.email = TheGameSpyInfo->getLocalEmail().str();
							req.nick = TheGameSpyInfo->getLocalBaseName().str();
							req.password = "";
							req.player = stats;
							req.addDesync = TheNetwork->sawCRCMismatch();
 							req.addDiscon = gameEndedInDisconnect;
							req.lastHouse = ptIdx;
							TheGameSpyPSMessageQueue->addRequest(req);
						}
						DEBUG_CRASH(("populatePlayerInfo() - not tracking stats - we haven't gotten the original stuff yet\n"));
						return;
					}

					if (TheNetwork->sawCRCMismatch())
					{
						++stats.desyncs[ptIdx];
					}
					else if (gameEndedInDisconnect)
					{
						++stats.discons[ptIdx];
					}
					else if (TheVictoryConditions->isLocalAlliedDefeat() || !TheVictoryConditions->getEndFrame())
					{
						++stats.losses[ptIdx];
					}
					else
					{
						++stats.wins[ptIdx];
					}

					ScoreKeeper *s = player->getScoreKeeper();
					stats.buildingsBuilt[ptIdx] += s->getTotalBuildingsBuilt();
					stats.buildingsKilled[ptIdx] += s->getTotalBuildingsDestroyed();
					stats.buildingsLost[ptIdx] += s->getTotalBuildingsLost();

					if (TheGameSpyGame->isQMGame())
					{
						stats.QMGames[ptIdx]++;
					}
					else
					{
						stats.customGames[ptIdx]++;
					}

					if (TheNetwork->sawCRCMismatch())
					{
						stats.lossesInARow = 0;
						stats.desyncsInARow++;
						stats.disconsInARow = 0;
						stats.winsInARow = 0;
						stats.maxDesyncsInARow = max(stats.desyncsInARow, stats.maxDesyncsInARow);
					}
					else if (gameEndedInDisconnect)
					{
						stats.lossesInARow = 0;
						stats.desyncsInARow = 0;
						stats.disconsInARow++;
						stats.winsInARow = 0;
						stats.maxDisconsInARow = max(stats.disconsInARow, stats.maxDisconsInARow);
					}
					else if (TheVictoryConditions->isLocalAlliedVictory())
					{
						stats.lossesInARow = 0;
						stats.desyncsInARow = 0;
						stats.disconsInARow = 0;
						stats.winsInARow++;
						stats.maxWinsInARow = max(stats.winsInARow, stats.maxWinsInARow);
					}
					else
					{
						stats.lossesInARow++;
						stats.desyncsInARow = 0;
						stats.disconsInARow = 0;
						stats.winsInARow = 0;
						stats.maxLossesInARow = max(stats.lossesInARow, stats.maxLossesInARow);
					}

					stats.earnings[ptIdx] += s->getTotalMoneyEarned();
					stats.duration[ptIdx] += TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60; // in minutes
					stats.games[ptIdx]++;
 					//since we raise this number when game starts, we need to lower it back on completion
 					Int disCons;
 					disCons = stats.discons[ptIdx];
 					disCons -= 1;
 					if (disCons >= 0)
 						stats.discons[ptIdx] = disCons;

					stats.gamesAsRandom += IsRandomPlayerTemplate(localSlot->getOriginalPlayerTemplate());

					if (stats.lastGeneral != ptIdx)
						stats.gamesInRowWithLastGeneral = 0;
					stats.gamesInRowWithLastGeneral++;
					stats.lastGeneral = ptIdx;

					Int gameSize = 0;
					for (Int i=0; i<MAX_SLOTS; ++i)
					{
						if (TheGameSpyGame->getConstSlot(i)->isOccupied() && TheGameSpyGame->getConstSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
							++gameSize;
					}
					switch (gameSize)
					{
					case 2:
						stats.gamesOf2p[ptIdx]++;
						break;
					case 3:
						stats.gamesOf3p[ptIdx]++;
						break;
					case 4:
						stats.gamesOf4p[ptIdx]++;
						break;
					case 5:
						stats.gamesOf5p[ptIdx]++;
						break;
					case 6:
						stats.gamesOf6p[ptIdx]++;
						break;
					case 7:
						stats.gamesOf7p[ptIdx]++;
						break;
					case 8:
						stats.gamesOf8p[ptIdx]++;
						break;
					default:
						return; // nothing to track.
					}

					stats.lastFPS = TheDisplay->getAverageFPS(); ///@todo: need something more than this, really. :(

					stats.surrenders[ptIdx] += TheGameInfo->haveWeSurrendered()  || !TheVictoryConditions->getEndFrame();

					AsciiString systemSpec;
					systemSpec.format("LOD%d", TheGameLODManager->findStaticLODLevel());
					stats.systemSpec = systemSpec.str();

					stats.techCaptured[ptIdx] += s->getTotalTechBuildingsCaptured();

					stats.unitsBuilt[ptIdx] += s->getTotalUnitsBuilt();
					stats.unitsKilled[ptIdx] += s->getTotalUnitsDestroyed();
					stats.unitsLost[ptIdx] += s->getTotalUnitsLost();

					DEBUG_LOG(("Before game built scud:%d, cannon:%d, nuke:%d\n", stats.builtSCUD, stats.builtParticleCannon, stats.builtNuke ));
					stats.builtSCUD += CheckForApocalypse( s, "GLAScudStorm" );
					stats.builtSCUD += CheckForApocalypse( s, "Chem_GLAScudStorm" );
					stats.builtSCUD += CheckForApocalypse( s, "Demo_GLAScudStorm" );
					stats.builtSCUD += CheckForApocalypse( s, "Slth_GLAScudStorm" );
					stats.builtParticleCannon += CheckForApocalypse( s, "AmericaParticleCannonUplink" );
					stats.builtParticleCannon += CheckForApocalypse( s, "AirF_AmericaParticleCannonUplink" );
					stats.builtParticleCannon += CheckForApocalypse( s, "Lazr_AmericaParticleCannonUplink" );
					stats.builtParticleCannon += CheckForApocalypse( s, "SupW_AmericaParticleCannonUplink" );
					stats.builtNuke += CheckForApocalypse( s, "ChinaNuclearMissileLauncher" );
					stats.builtNuke += CheckForApocalypse( s, "Nuke_ChinaNuclearMissileLauncher" );
					stats.builtNuke += CheckForApocalypse( s, "Infa_ChinaNuclearMissileLauncher" );
					stats.builtNuke += CheckForApocalypse( s, "Tank_ChinaNuclearMissileLauncher" );
					DEBUG_LOG(("After game built scud:%d, cannon:%d, nuke:%d\n", stats.builtSCUD, stats.builtParticleCannon, stats.builtNuke ));

					if (TheGameSpyGame->getLadderPort() && TheGameSpyGame->getLadderIP().isNotEmpty())
					{
						stats.lastLadderPort = TheGameSpyGame->getLadderPort();
						stats.lastLadderHost = TheGameSpyGame->getLadderIP().str();
					}

					if (!TheNetwork->sawCRCMismatch() && !gameEndedInDisconnect && !TheVictoryConditions->isLocalAlliedDefeat() && TheVictoryConditions->getEndFrame())
					{
						updateMPBattleHonors(stats.battleHonors, stats);
						updateChallengeMedals(stats.challengeMedals);
					}

					DEBUG_LOG(("populatePlayerInfo() - tracking stats for %s/%s/%s\n", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));

					PSRequest req;
					req.requestType = PSRequest::PSREQUEST_UPDATEPLAYERSTATS;
					req.email = TheGameSpyInfo->getLocalEmail().str();
					req.nick = TheGameSpyInfo->getLocalBaseName().str();
					req.password = TheGameSpyInfo->getLocalPassword().str();
					req.player = stats;
					req.addDesync = TheNetwork->sawCRCMismatch();
 					req.addDiscon = gameEndedInDisconnect;
					req.lastHouse = ptIdx;
					TheGameSpyPSMessageQueue->addRequest(req);
					TheGameSpyPSMessageQueue->trackPlayerStats(stats);
					
					// force an update of our shtuff
					PSResponse newResp;
					newResp.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
					newResp.player = stats;
					TheGameSpyPSMessageQueue->addResponse(newResp);

					// cache our stuff for easy reading next time
					GameSpyMiscPreferences mPref;
					mPref.setCachedStats(GameSpyPSMessageQueueInterface::formatPlayerKVPairs(stats).c_str());
					mPref.write();
				}
			}
		}
	}
}

/** We Grab information about the players differently in Multiplayer.  We only want the players
		listed in the slots */
//-------------------------------------------------------------------------------------------------
void grabMultiPlayerInfo( void )
{
	typedef std::map<Int, Player *> ScoreMap;
	typedef ScoreMap::iterator ScoreMapIt;
	typedef ScoreMap::reverse_iterator RevScoreMapIt;

	Int playerCount = 0;
	AsciiString playerName;
	Player *player;
	ScoreMap scores;
	ScoreMapIt it;
	scores.clear();
	Int adder = 1; // Varible used to add on an offset to the score to make sure we don't add people to the same map

	player = ThePlayerList->getLocalPlayer();
	if (player)
	{
		const Image *image = TheMappedImageCollection->findImageByName("MutiPlayer_ScoreScreen");
		if(image)
		{
			parent->winSetEnabledImage(0, image);
			parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
		}
	}

	// Add each player and score to the map. THis allows us to sort the players based on score.
	for( Int i = 0; i < MAX_SLOTS; ++i)
	{
		playerName.format("player%d",i);
		player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
		if(player)
		{
			Int score = player->getScoreKeeper()->calculateScore();
			it = scores.find( score );
			if (it != scores.end())
			score += adder++;			
			scores[score] = player;
			++playerCount;
		}
	}
	Int count =0;
	RevScoreMapIt revIt;
	// display the players based on Score; an observer has nothing to show
	for ( revIt = scores.rbegin(); revIt != scores.rend(); ++revIt)
	{
		Player *p = revIt->second;
		if(!p->isPlayerObserver())
			populatePlayerInfo( p, count);
		count ++;
	}
	
	DEBUG_ASSERTCRASH(count == playerCount, ("For some reason we added %d players to the scores map, but only read %d", playerCount, count));
	
}

enum
{
	USA_FRIEND = 0,
	CHINA_FRIEND,
	GLA_FRIEND,
	USA_ENEMY,		// Keep friends with friends, enemys with enemys
	CHINA_ENEMY,
	GLA_ENEMY,
	MAX_RELATIONS // keep me last
};
/**	Grab the single player info */
//-------------------------------------------------------------------------------------------------
void grabSinglePlayerInfo( void )
{
	Int playerCount = 0;
	Player *player, *localPlayer;
	localPlayer = ThePlayerList->getLocalPlayer();
	// the summary lays these rows out as a mission's, a replayed one included
	ScoreScreenGraphsSinglePlayer();

	if(localPlayer)
	{
		if(!localPlayer->isPlayerObserver())
		{
			populatePlayerInfo(localPlayer, playerCount);
			++playerCount;
		}
		else
		{
			for(Int k = 0; k < MAX_PLAYER_COUNT; ++k)
			{
				localPlayer = ThePlayerList->getNthPlayer(k);
				if(localPlayer && localPlayer->getPlayerType() == PLAYER_HUMAN)
				{
					populatePlayerInfo(localPlayer, playerCount);
					++playerCount;
					break;
				}
				localPlayer = NULL;
			}
		}
		PlayerTemplate const *fact = ThePlayerList->getLocalPlayer()->getPlayerTemplate();
		if(fact != NULL)
		{
			const Image *image = TheMappedImageCollection->findImageByName(ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getScoreScreen());
			if(image)
			{
				parent->winSetEnabledImage(0, image);
				parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
			}
		}
	}
	if(!localPlayer)
		return;
	AsciiString side;
	// okay, there's all kinds of hard coding going on here.  THe reason why! well,
	// We have no way of telling what sides we have in the game.  Hence, the hardcoding.
	for(Int j = 0; j < MAX_RELATIONS; ++j )
	{
		Bool isFriend = TRUE;
		
		// set the string we'll be compairing to
		switch (j) {
		case USA_ENEMY:
			isFriend = FALSE;
		case USA_FRIEND:
			side.set("USA");
			break;
		case CHINA_ENEMY:
			isFriend = FALSE;
		case CHINA_FRIEND:
			side.set("China");
			break;
		case GLA_ENEMY:
			isFriend = FALSE;	
		case GLA_FRIEND:
			side.set("GLA");
			break;
		}
		ScoreGather sg;
		sg.m_totalBuildingsBuilt = 0;
		sg.m_totalBuildingsDestroyed = 0;
		sg.m_totalBuildingsLost = 0;
		sg.m_totalMoneyEarned = 0;
		sg.m_totalMoneySpent = 0;
		sg.m_totalUnitsBuilt = 0;
		sg.m_totalUnitsDestroyed = 0;
		sg.m_totalUnitsLost = 0;
		Int score = 0;
		Bool populate = FALSE;
		Color color;
		for(Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			player = ThePlayerList->getNthPlayer(i);
			if(player && player != localPlayer &&	
				 side.compare(player->getBaseSide()) == 0)
			{
				if ((TheGameLogic->isInSinglePlayerGame() == FALSE) || (player->getListInScoreScreen() == TRUE))
				{
					if((isFriend == TRUE && localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) ||
							(isFriend == FALSE && localPlayer->getRelationship(player->getDefaultTeam()) == ENEMIES))
					{
						ScoreKeeper *sk = player->getScoreKeeper();
						sg.m_totalBuildingsBuilt += sk->getTotalBuildingsBuilt();
						sg.m_totalBuildingsDestroyed += sk->getTotalBuildingsDestroyed();
						sg.m_totalBuildingsLost += sk->getTotalBuildingsLost();
						sg.m_totalMoneyEarned += sk->getTotalMoneyEarned();
						sg.m_totalMoneySpent += sk->getTotalMoneySpent();
						sg.m_totalUnitsBuilt += sk->getTotalUnitsBuilt();
						sg.m_totalUnitsDestroyed += sk->getTotalUnitsDestroyed();
						sg.m_totalUnitsLost += sk->getTotalUnitsLost();
						score += sk->calculateScore();
							color = clientPlayerColor( player );
						populate = TRUE;	
					}
				}
			}
		}
		if(populate)
		{
			AsciiString label;
			label.set("GUI:");
			label.concat(side);
			if(isFriend)
				label.concat("Allies");
			else
				label.concat("Enemies");
			ScoreScreenGraphsAddSide(TheGameText->fetch(label), color, score, sg.m_totalUnitsBuilt, sg.m_totalUnitsLost, sg.m_totalUnitsDestroyed,
				sg.m_totalBuildingsBuilt, sg.m_totalBuildingsLost, sg.m_totalBuildingsDestroyed, sg.m_totalMoneyEarned, sg.m_totalMoneySpent);
			++playerCount;
		}
	}
}

/** The layout's own score table, its Tactical Analysis box and the chat's academy twin: the
	* summary panel shows all of it now. */
//-------------------------------------------------------------------------------------------------
static void hideScoreTable( void )
{
	static const char *const tableWindows[] =
	{
		"ScoreScreen.wnd:StaticTextPlayerName", "ScoreScreen.wnd:StaticTextUnitsBuilt", "ScoreScreen.wnd:StaticTextUnitsLost",
		"ScoreScreen.wnd:StaticTextUnitsDestroyed", "ScoreScreen.wnd:StaticTextBuildingsConstructed", "ScoreScreen.wnd:StaticTextBuildingsLost",
		"ScoreScreen.wnd:StaticTextBuildingsDestroyed", "ScoreScreen.wnd:StaticTextSuppliesCollected",
		"ScoreScreen.wnd:CameoUnitsCreated", "ScoreScreen.wnd:CameoUnitsLost", "ScoreScreen.wnd:CameoUnitsDestroyed",
		"ScoreScreen.wnd:CameoBuildingCreated", "ScoreScreen.wnd:CameoBuildingLost", "ScoreScreen.wnd:CameoBuildingDestroyed",
		"ScoreScreen.wnd:CameoSuppliesCollected", "ScoreScreen.wnd:Line",
		"ScoreScreen.wnd:ListboxWarschoolAdvice", "ScoreScreen.wnd:StaticTextWarSchool"
	};
	// Every lookup may come back NULL: a ScoreScreen.wnd from another install (a mod, an old copy in
	// Window) lacks windows ours has, and the table is only hidden here, so a missing one costs nothing.
	for( Int each = 0; each < ARRAY_SIZE( tableWindows ); ++each )
	{
		GameWindow *win = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( tableWindows[ each ] ) );
		if( win )
			win->winHide( TRUE );
	}
	// the rows' frame and the table's header box, which EA left unnamed; and the backdrop's own
	// darkening, which the panel replaces
	if( gadgetParent )
		gadgetParent->winHide( TRUE );
	if( backdrop )
	{
		GameWindow *darkening = TheWindowManager->winGetWindowFromId( backdrop, TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:" ) );
		if( darkening )
			darkening->winHide( TRUE );
		backdrop->winSetEnabledColor( 0, GameMakeColor( 0, 0, 0, 0 ) );
	}

	// and its rows, one a slot
	static const char *const rowWindows[] =
	{
		"ScoreScreen.wnd:StaticTextPlayer%d", "ScoreScreen.wnd:StaticTextObserver%d", "ScoreScreen.wnd:StaticTextUnitsBuilt%d",
		"ScoreScreen.wnd:StaticTextUnitsLost%d", "ScoreScreen.wnd:StaticTextUnitsDestroyed%d", "ScoreScreen.wnd:StaticTextBuildingsBuilt%d",
		"ScoreScreen.wnd:StaticTextBuildingsLost%d", "ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", "ScoreScreen.wnd:StaticTextResources%d",
		"ScoreScreen.wnd:GameWindowWinner%d"
	};
	AsciiString winName;
	for( Int i = 0; i < MAX_SLOTS; ++i)
	{
		for( Int each = 0; each < ARRAY_SIZE( rowWindows ); ++each )
		{
			winName.format( rowWindows[ each ], i );
			GameWindow *row = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
			if( row )	// the layout has eight rows, MAX_SLOTS may be more
				row->winHide( TRUE );
		}
	}
}

/** The staging rooms' button row, inside the panel on the ruler frame: Save Replay and Buddies from
	* the left where a room has Back and Record, Continue (or the way out alone) wide at the right where
	* it has Start. The chat line and the "game saved" line sat where the row stands now and move up
	* over it, the chat above them along. Laid out again once Continue is shown, so it runs twice. */
/** A window on the 1920x1080 board from x0,y0 to x1,y1, wherever its parent stands. */
static void placeOnBoard( GameWindow *win, Real x0, Real y0, Real x1, Real y1 )
{
	if( win == NULL )
		return;
	Int parentX = 0, parentY = 0;
	if( win->winGetParent() )
		win->winGetParent()->winGetScreenPosition( &parentX, &parentY );
	win->winSetPosition( UIStyle::boardX( x0 ) - parentX, UIStyle::boardY( y0 ) - parentY );
	win->winSetSize( UIStyle::px( x1 - x0 ), UIStyle::px( y1 - y0 ) );
}

static void placeButtonRow( void )
{
	const Int top = UIStyle::boardY( UIStyle::ROOM_BUTTON_TOP );
	const Int height = UIStyle::px( UIStyle::ROOM_BUTTON_BOTTOM - UIStyle::ROOM_BUTTON_TOP );
	const Int gap = UIStyle::px( UIStyle::ROOM_BUTTON_GAP );
	const Int width = UIStyle::px( UIStyle::ROOM_BUTTON_WIDTH );

	GameWindow *fromLeft[] = { TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID ), buttonBuddies };
	Int x = UIStyle::boardX( UIStyle::ROOM_INSET );
	for( Int each = 0; each < ARRAY_SIZE( fromLeft ); ++each )
	{
		if( fromLeft[ each ]->winIsHidden() )
			continue;
		fromLeft[ each ]->winSetPosition( x, top );
		fromLeft[ each ]->winSetSize( width, height );
		x += width + gap;
	}

	GameWindow *fromRight[] = { buttonContinue, buttonOk };
	x = UIStyle::boardX( UIStyle::ROOM_PANEL_RIGHT + UIStyle::ROOM_PANEL_LEFT - UIStyle::ROOM_INSET );
	Bool first = TRUE;
	for( Int each = 0; each < ARRAY_SIZE( fromRight ); ++each )
	{
		if( fromRight[ each ]->winIsHidden() )
			continue;
		const Int buttonWidth = first ? UIStyle::px( UIStyle::ROOM_WIDE_BUTTON_WIDTH ) : width;
		x -= buttonWidth;
		fromRight[ each ]->winSetPosition( x, top );
		fromRight[ each ]->winSetSize( buttonWidth, height );
		x -= gap;
		first = FALSE;
	}

	Int entryX, entryY, entryWidth, entryHeight;
	textEntryChat->winGetScreenPosition( &entryX, &entryY );
	textEntryChat->winGetSize( &entryWidth, &entryHeight );
	const Int lift = entryY + entryHeight - ( top - gap );
	if( lift > 0 )
	{
		GameWindow *foot[] = { textEntryChat, buttonEmote, staticTextGameSaved, listboxChatWindowScoreScreen };
		for( Int each = 0; each < ARRAY_SIZE( foot ); ++each )
		{
			Int footX, footY;
			foot[ each ]->winGetPosition( &footX, &footY );
			foot[ each ]->winSetPosition( footX, footY - lift );
		}
	}

	// after a network match the chat stands where the network rooms keep theirs: under the seats, in
	// the seats' column, the chat line and the emote button on the column's foot; the list starts
	// lower where eight players in teams need the room.  The border first, since the others may be its
	// children and are placed by their screen position.
	if( listboxChatWindowScoreScreen && !listboxChatWindowScoreScreen->winIsHidden() )
	{
		const Real emoteLeft = SCORE_LEFT_RIGHT - 64.0f;
		// online hides the chat line, and the list then runs to the frame's foot
		const Bool chatLine = textEntryChat && !textEntryChat->winIsHidden();
		const Real listBottom = chatLine ? SCORE_CHAT_LIST_BOTTOM : SCORE_CONTENT_BOTTOM;
		const Real chatTop = ScoreScreenGraphsChatTop( listBottom );
		placeOnBoard( chatBoxBorder, SCORE_LEFT, chatTop, SCORE_LEFT_RIGHT, SCORE_CONTENT_BOTTOM );
		placeOnBoard( listboxChatWindowScoreScreen, SCORE_LEFT, chatTop, SCORE_LEFT_RIGHT, listBottom );
		placeOnBoard( textEntryChat, SCORE_LEFT, SCORE_CHAT_ENTRY_TOP, emoteLeft - 8.0f, SCORE_CONTENT_BOTTOM );
		placeOnBoard( buttonEmote, emoteLeft, SCORE_CHAT_ENTRY_TOP, SCORE_LEFT_RIGHT, SCORE_CONTENT_BOTTOM );
	}
}

