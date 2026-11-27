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

// FILE: LoadScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Mar 2002
//
//	Filename: 	LoadScreen.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Contains each of the different derived LoadClasses for each of the
//						Different kind of games we can have.
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Platform/SleepMilliseconds.h"
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/NameKeyGenerator.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomMapGenerator.h"
#include "Common/SkirmishPreferences.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameConsole.h"
#include "GameClient/GadgetProgressBar.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/LoadScreenLayout.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/WindowVideoManager.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/UIStyle.h"
#include "GameLogic/AI.h"	// aiPersonalityName, for a computer seat's card
#include "GameLogic/ApocalypseDirector.h"	// the nights' phases, for its board
#include "GameLogic/DirectStrikeDirector.h"	// the wave's tick, income and life, for its board
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/RankPointValue.h"

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
// PRIVATE TYPES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE DATA ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC DATA ////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
enum{
FRAME_TITLES_START = 20,
FRAME_TELETYPE_START = 24,
FRAME_FUDGE_ADD = 30,
FRAME_PORTRAITS_START = 35,
FRAME_OUTER_CIRCLE_LINE_SHOW = 50,
FRAME_INNER_CIRCLE_LINE_SHOW = 52,
FRAME_OUTER_CIRCLE_ALPHA_SHOW = 63,
FRAME_INNER_CIRCLE_ALPHA_SHOW = 74,
FRAME_OUTER_CIRCLE_LINE_HIDE = 75,
FRAME_INNER_BACKDROP_ALPHA_SHOW = 80,
FRAME_INNER_CIRCLE_LINE_HIDE = 81,
FRAME_VS_ANIM_START = 98,
FRAME_RIGHT_VOICE = 140,
};

static const Int TELETYPE_UPDATE_FREQ = 2; // how many frames between teletype updates



//-----------------------------------------------------------------------------
// LoadScreen Class 
//-----------------------------------------------------------------------------

LoadScreen::LoadScreen( void )
{
	m_loadScreen = NULL;		
}

LoadScreen::~LoadScreen( void )
{
	//if(m_loadScreen)
	//	delete (m_loadScreen);
	if(m_loadScreen)
		TheWindowManager->winDestroy( m_loadScreen );
	m_loadScreen = NULL;
}

void LoadScreen::update( Int percent )
{
	// called from inside the map-load tick; the view update below may ask a locked object's height
	Thing::CachePeek loadScreenReadsLeaveNoCache;
	TheGameEngine->serviceWindowsOS();
	if (TheGameEngine->getQuitting())
		return;	//don't bother with any of this if the player is exiting game.

	if( TheGameConsole )
		TheGameConsole->readLoadScreenKeys();
	TheWindowManager->update();
	TheDisplay->update();
	// redraw all views, update the GUI
	TheDisplay->draw();

	setFPMode();
}

// LoadScreenBoard ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
/* A match's load, skirmish, LAN, online or replay, as one board in the menu language
	 (README-loading.md beside loading.py, the wrapper's experiments/proofs/2026-11-24/screens/loading):
	 frame A's panel on glass over the map's preview; the teams as two wings facing each other across the
	 map, a head a team with its ready count and a row a player (a poster for one alone, a card while the
	 wing has room, a line past that); the map with its start spots, the rules and the watchers between
	 them; a tip and the load bar along the foot.  A campaign mission and a saved campaign game load on the
	 same frame over the mission's picture, and a map transfer on the board's rows.

	 Everything is laid out once on the 1920x1080 board (LoadScreenLayout.h) and scaled to the screen by
	 its smaller axis; a pass draws fills, the portraits and display strings made up front. */

static const UnsignedInt BOARD_TIP_MILLISECONDS = 8000;
/// "  ·  ", the middle dot spelled out (the source is read in the machine's code page), between a
/// general's persona and his strategy, and a computer seat's level and personality
static const WideChar BOARD_DOT[] = { ' ', ' ', 0x00B7, ' ', ' ', 0 };
/// the type at 1080 (README-loading.md); nothing is under BOARD_CAPS, 9.3 pixels at 720
enum
{
	BOARD_TITLE = 40,				///< the title, VS, a card head's ready figure
	BOARD_HEAD = 22,				///< TEAM n over cards
	BOARD_NAME = 24,				///< a card's name and figure, the wordmark, the load bar's words
	BOARD_SUBTITLE = 20,
	BOARD_BODY = 18,
	BOARD_SMALL = 15,				///< a line row's general, personality and the night clocks
	BOARD_CAPS = 14,				///< the tracked capitals: READY, RULES, TIP, a phase
	BOARD_UNKNOWN = 72			///< the "?" in a hidden map's well
};
static const Real BOARD_TITLE_TRACKING = 4.8f;
static const Real BOARD_BAR_TRACKING = 2.0f;
static const Color BOARD_SHADOW = GameMakeColor( 0, 0, 0, 160 );

static Int boardStringWidth( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return width;
}

static Int boardStringHeight( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return height;
}

/** The load bar: a dark well with the three-line edge, an EDGE fill and its lit leading edge while it
	* moves, the words tracked in its middle.  The board's foot and the Challenge load share it. */
static void boardDrawLoadBar( const IRegion2D &bar, Int percent, const UnicodeString &words, GameFont *font, Real tracking )
{
	const Int width = bar.hi.x - bar.lo.x;
	const Int height = bar.hi.y - bar.lo.y;
	TheDisplay->drawFillRect( bar.lo.x, bar.lo.y, width, height, GameMakeColor( 0, 0, 0, 150 ) );
	const Int inset = 3;
	const Int track = width - 2 * inset;
	const Int filled = track * min( max( percent, 0 ), 100 ) / 100;
	if( filled > 0 )
		TheDisplay->drawFillRect( bar.lo.x + inset, bar.lo.y + inset, filled, height - 2 * inset, UIStyle::withAlpha( UIStyle::EDGE, 90 ) );
	if( filled > 2 && filled < track )
		UIStyle::litLine( bar.lo.x + inset + filled - 2, bar.lo.y + inset, 2, height - 2 * inset, UIStyle::EDGE_LIT );
	UIStyle::litLine( bar.lo.x, bar.lo.y, width, 1, UIStyle::EDGE );
	UIStyle::litLine( bar.lo.x, bar.hi.y - 1, width, 1, UIStyle::EDGE );
	UIStyle::edge( bar.lo.x, bar.lo.y, width, height );
	if( words.isEmpty() )
		return;
	const Int wordsWidth = UIStyle::trackedWidth( words, font, tracking );
	UIStyle::drawTracked( words, font, ( bar.lo.x + bar.hi.x - wordsWidth ) / 2, ( bar.lo.y + bar.hi.y - font->height ) / 2, UIStyle::INK, tracking );
}

/// a start spot as the room marks it, the player's colour and his seat over it
static void boardDrawDisc( Int middleX, Int middleY, Int radius, Color color, DisplayString *number )
{
	const Int size = 2 * radius;
	LobbyStartSpotFill( middleX - radius, middleY - radius, size, size, color );
	if( number == NULL )
		return;
	Int width, height;
	number->getSize( &width, &height );
	number->draw( middleX - width / 2, middleY - height / 2, LobbyTextColorOn( color ), BOARD_SHADOW );
}

/// m:ss
static UnicodeString boardClock( Int seconds )
{
	UnicodeString words;
	words.format( u"%d:%02d", max( seconds, 0 ) / 60, max( seconds, 0 ) % 60 );
	return words;
}

/// a Direct Strike span on the wall clock at the match's speed, whole seconds (the mode tunes them in
/// wall seconds at the native speed, DirectStrike_gameFrames)
static Int boardDirectStrikeSeconds( UnsignedInt frames )
{
	return (Int)( ( frames + LOGICFRAMES_PER_SECOND / 2 ) / LOGICFRAMES_PER_SECOND );
}

/// a key's words with a trailing colon taken off, for a head ("Observers:" is a label elsewhere)
static UnicodeString boardHeadWords( const char *key )
{
	UnicodeString words = TheGameText->fetch( key );
	words.trim();
	while( words.getLength() > 0 && words.getCharAt( words.getLength() - 1 ) == u':' )
		words.removeLastChar();
	return UIStyle::capitals( words );
}

/// one player's row
struct BoardCard
{
	Int slot;
	Color color;
	Int team;									///< an index into the board's teams
	Int startSpot;						///< the start position the others may see, -1 when hidden
	Int percent;							///< his own load, -1 for a row without a channel
	Bool ai;
	Bool local;
	Bool plainSide;						///< a side with no general, whose picture is small art
	const Image *portrait;		///< NULL for a random or hidden side: a "?"
	const Image *badge;				///< an online player's rank
	UnicodeString nameWords;
	UnicodeString armyWords;
	UnicodeString personaWords;
	UnicodeString detailWords;
	DisplayString *name;
	DisplayString *army;
	DisplayString *persona;		///< "General Townes  ·  Laser Weaponry", NULL on a line row or a plain side
	DisplayString *detail;		///< a computer's level and personality, NULL for a human or a plain computer
	DisplayString *figure;		///< "81%"
	DisplayString *disc;			///< his seat on his start spot, NULL where the map shows none of his
	IRegion2D box;						///< on the 1080 board
	LoadRowKind kind;
	Bool zebra;
};

/// a team's head over its rows in a wing
struct BoardHead
{
	IRegion2D box;						///< on the 1080 board
	LoadHeadKind kind;
	Color color;
	Int toward;								///< chevrons toward the middle: 1 right, -1 left, 0 none
	UnicodeString words;			///< TEAM n, or SURVIVORS
	std::vector< Int > counted;	///< the cards whose readiness it counts; empty for no count
};

/// a row of the rules, or of the nights panel
struct BoardFact
{
	UnicodeString labelWords;	///< tracked capitals when capsLabel
	DisplayString *label;
	DisplayString *value;
	Color labelColor;
	Bool capsLabel;
	Bool blood;								///< the Blood Moon's red rule at its left
};

/// a map transfer's row
struct BoardTransfer
{
	Int slot;
	Color color;
	Bool local;
	Int percent;							///< -1 without a channel: the host, or a player who had the map
	DisplayString *name;
	DisplayString *state;
	DisplayString *figure;
	IRegion2D box;						///< on the 1080 board
	Bool zebra;
};

class LoadScreenBoard
{
public:
	LoadScreenBoard( void );
	~LoadScreenBoard( void );

	/// lays the board out for this game and makes the window that draws it
	GameWindow *create( GameInfo *game, const char *modeLabel, Bool cardBars );
	/** A campaign mission's load, or a saved campaign game's: the header with the mission count, the
		* briefing and the foot over the mission's own picture.  backdrop is that picture when the board
		* stands on no window that shows it (parent NULL, a saved game's); without one it stands on the
		* shell's dark. */
	GameWindow *createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
		const UnicodeString *briefing, Int briefingLines, Int missionNumber, Int missionCount, const Image *backdrop );
	/// a map transfer: every human on the board's rows with the state the network reports
	GameWindow *createTransfer( GameInfo *game, const char *modeLabel );
	void setBadge( Int slot, const Image *badge );
	void setProgress( Int slot, Int percent );
	void setLocalProgress( Int percent );
	void setRejoinProgress( Int percent, Bool waitingForLog );
	void setTransfer( Int slot, Int percent, const UnicodeString &state );
	void setTransferFoot( const UnicodeString *file, const UnicodeString *timeout );

private:
	enum Kind { BOARD_MATCH, BOARD_MISSION, BOARD_TRANSFER };

	static void drawWindow( GameWindow *window, WinInstanceData *instData );
	void prepare( const UnicodeString &titleInCapitals, const UnicodeString &subtitle, Bool titleIsName, const char *tipKey );
	GameWindow *makeWindow( GameWindow *parent );
	void draw( void );
	void drawHeader( void );
	void drawFoot( void );
	void drawMatch( void );
	void drawMission( void );
	void drawTransfer( void );
	void drawHead( const BoardHead &head );
	void drawRow( const BoardCard &card );
	void drawLines( const BoardCard &card, Int left, Int right, Int middle );
	void drawPortrait( const BoardCard &card, const IRegion2D &box, Bool lineRow );
	void drawChannel( Int left, Int right, Int middle, Int height, Int percent, Color color );
	void drawSection( const UnicodeString &words, Int left, Int right, Int top );
	void drawMiddle( void );
	void drawNights( Int top );
	void drawFacts( const std::vector< BoardFact > &facts, Int top, Int pitch );
	void cardFrame( const BoardCard &card, IRegion2D &portrait, Int &textLeft, Int &textRight, Int &textMiddle ) const;
	void fitCard( BoardCard &card );
	Int cardLast( const BoardCard &card ) const;
	void addFact( std::vector< BoardFact > &facts, const UnicodeString &label, const UnicodeString &value, Color labelColor = UIStyle::MUTED, Bool caps = FALSE );
	void refreshLabel( void );
	Int scaled( Real at1080 ) const { return REAL_TO_INT( at1080 * m_fit.scale + 0.5f ); }
	Int X( Real at1080 ) const { return m_fit.x + scaled( at1080 ); }
	Int Y( Real at1080 ) const { return m_fit.y + scaled( at1080 ); }
	IRegion2D onScreen( const IRegion2D &at1080 ) const;
	IRegion2D onScreen( Int left, Int top, Int right, Int bottom ) const;
	GameFont *font( Int px1080, Bool bold = FALSE ) const { return UIStyle::menuFont( px1080, bold ); }
	DisplayString *text( const UnicodeString &words, GameFont *font );
	BoardCard *cardFor( Int slot );
	UnicodeString tipText( Int number, Bool *exists = NULL ) const;

	Kind m_kind;
	UIStyle::BoardFit m_fit;
	Int m_width;							///< the screen
	Int m_height;
	std::vector< BoardCard > m_cards;
	std::vector< BoardHead > m_heads;
	std::vector< BoardFact > m_facts;
	std::vector< BoardFact > m_nights;
	std::vector< BoardTransfer > m_transfers;
	std::vector< DisplayString * > m_strings;
	std::vector< ICoord2D > m_spotAt;		///< a start spot on the 1080 board, the card's index beside it
	std::vector< Int > m_spotCard;
	LoadMiddlePlan m_middle;
	Bool m_middleShown;
	Bool m_wellShown;						///< a hidden or generated map: the well, not the preview
	const Image *m_mapImage;
	const Image *m_backdrop;				///< behind the panel: the map's preview, a saved game's mission picture
	Bool m_ground;								///< nothing of a window's shows behind: the shell's dark under the backdrop
	DisplayString *m_vs;
	DisplayString *m_unknown;
	DisplayString *m_unknownSmall;
	DisplayString *m_wellWords;
	DisplayString *m_observers;
	UnicodeString m_rulesHead;
	UnicodeString m_observersHead;
	UnicodeString m_nightsHead;
	Int m_nightSeconds[ 4 ];
	UnicodeString m_nightWords[ 4 ];
	DisplayString *m_nightClocks[ 4 ];
	UnicodeString m_titleWords;
	DisplayString *m_subtitle;
	DisplayString *m_wordmark[ 2 ];
	Bool m_showWordmark;
	UnicodeString m_readyWords;
	DisplayString *m_lineFigure;		///< "100%" in a line row's figure type, the room a figure keeps
	DisplayString *m_cardFigure;
	// the mission's
	UnicodeString m_missionOf;
	Int m_missionNumber;
	Int m_missionCount;
	UnicodeString m_briefingHead;
	std::vector< DisplayString * > m_briefing;
	Int m_briefingBottom;					///< on the 1080 board
	// the transfer's
	UnicodeString m_playersHead;
	UnicodeString m_statusHead;
	DisplayString *m_transferFile;
	DisplayString *m_transferTimeout;
	// the foot
	const char *m_tipKey;					///< GUI:LoadTip, or a mode's own tips; NULL for none
	UnicodeString m_tipLabel;
	DisplayString *m_tip;
	std::vector< Int > m_tips;		///< the tips that fit on one line
	Int m_tipAt;
	UnsignedInt m_tipSince;
	UnicodeString m_progressFormat;
	UnicodeString m_label;
	Int m_localPercent;
	Int m_rejoinShown;						///< the rejoin bar's last value, -1 for the log wait, -2 before the first
	Bool m_network;
	Bool m_rejoining;
};

LoadScreenBoard::LoadScreenBoard( void )
{
	m_kind = BOARD_MATCH;
	m_width = TheDisplay->getWidth();
	m_height = TheDisplay->getHeight();
	m_fit = UIStyle::boardFit( m_width, m_height );
	m_middleShown = FALSE;
	m_wellShown = FALSE;
	m_mapImage = NULL;
	m_backdrop = NULL;
	m_ground = TRUE;
	m_vs = m_unknown = m_unknownSmall = m_wellWords = m_observers = NULL;
	for( Int each = 0; each < 4; each++ )
	{
		m_nightSeconds[ each ] = 0;
		m_nightClocks[ each ] = NULL;
	}
	m_subtitle = NULL;
	m_wordmark[ 0 ] = m_wordmark[ 1 ] = NULL;
	m_showWordmark = TRUE;
	m_lineFigure = m_cardFigure = NULL;
	m_missionNumber = m_missionCount = 0;
	m_briefingBottom = 0;
	m_transferFile = m_transferTimeout = NULL;
	m_tipKey = "GUI:LoadTip";
	m_tip = NULL;
	m_tipAt = 0;
	m_tipSince = 0;
	m_localPercent = 0;
	m_rejoinShown = -2;
	m_network = FALSE;
	m_rejoining = FALSE;
	m_middle.vsTop = m_middle.nightsTop = m_middle.rulesTop = m_middle.observersTop = -1;
	m_middle.rulePitch = m_middle.bottom = 0;
	m_middle.map.lo.x = m_middle.map.lo.y = m_middle.map.hi.x = m_middle.map.hi.y = 0;
}

LoadScreenBoard::~LoadScreenBoard( void )
{
	for( size_t each = 0; each < m_strings.size(); each++ )
		TheDisplayStringManager->freeDisplayString( m_strings[ each ] );
}

DisplayString *LoadScreenBoard::text( const UnicodeString &words, GameFont *font )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( font );
	string->setText( words );
	m_strings.push_back( string );
	return string;
}

BoardCard *LoadScreenBoard::cardFor( Int slot )
{
	for( size_t each = 0; each < m_cards.size(); each++ )
	{
		if( m_cards[ each ].slot == slot )
			return &m_cards[ each ];
	}
	return NULL;
}

IRegion2D LoadScreenBoard::onScreen( Int left, Int top, Int right, Int bottom ) const
{
	IRegion2D box;
	box.lo.x = X( (Real)left );
	box.lo.y = Y( (Real)top );
	box.hi.x = X( (Real)right );
	box.hi.y = Y( (Real)bottom );
	return box;
}

IRegion2D LoadScreenBoard::onScreen( const IRegion2D &at1080 ) const
{
	return onScreen( at1080.lo.x, at1080.lo.y, at1080.hi.x, at1080.hi.y );
}

/** Tip n of the board's tips (GUI:LoadTip1, GUI:LoadTip2, ..., or a mode's own); exists says whether
	* there is one.  A mode's tip that names a span takes it on the wall clock at the match's speed, so
	* it says what the player will see at any speed. */
UnicodeString LoadScreenBoard::tipText( Int number, Bool *exists ) const
{
	if( m_tipKey == NULL )
	{
		if( exists )
			*exists = FALSE;
		return UnicodeString::TheEmptyString;
	}
	AsciiString label;
	label.format( "%s%d", m_tipKey, number );
	// probed first: the count runs one past the last tip, and fetching that one logs it as missing text
	const Bool there = UIStyle::hasText( label.str() );
	if( exists )
		*exists = there;
	if( !there )
		return UnicodeString::TheEmptyString;
	UnicodeString words = TheGameText->fetch( label );
	Int seconds = -1;
	if( strcmp( m_tipKey, "GUI:LoadTipDirectStrike" ) == 0 )
	{
		if( number == 1 )
			seconds = boardDirectStrikeSeconds( DIRECTSTRIKE_TICK_FRAMES );
		else if( number == 2 )
			seconds = boardDirectStrikeSeconds( DIRECTSTRIKE_LIFESPAN_FRAMES );
	}
	else if( strcmp( m_tipKey, "GUI:LoadTipApocalypse" ) == 0 )
	{
		if( number == 2 )
			seconds = LoadScreen_wallSeconds( APOCALYPSE_TELEGRAPH_SECONDS, GAME_SPEED_PERCENT );
		else if( number == 3 )
			seconds = LoadScreen_wallSeconds( APOCALYPSE_FIRST_WAVE_SECONDS, GAME_SPEED_PERCENT );
	}
	if( seconds >= 0 )
	{
		UnicodeString formatted;
		formatted.format( words, seconds );
		words = formatted;
	}
	return words;
}

/** The starting money as the header and the rules say it. */
static UnicodeString boardCash( const GameInfo *game )
{
	const Money &cash = game->getStartingCash();
	if( cash.isUnlimited() )
		return TheGameText->fetch( "GUI:StartingMoneyUnlimited" );
	UnicodeString words( u"$" );
	words.concat( UIStyle::grouped( (Int)cash.countMoney() ) );
	return words;
}

/** The game speed as the lobby words it. */
static UnicodeString boardSpeed( void )
{
	Int speed = 0;
	for( Int each = 0; each < PLAYER_GAME_SPEED_COUNT; each++ )
	{
		if( PLAYER_GAME_SPEED_PERCENTS[ each ] == GAME_SPEED_PERCENT )
			speed = each;
	}
	AsciiString label;
	label.format( "GUI:GameSpeedMissions%d", speed );
	return TheGameText->fetch( label );
}

GameWindow *LoadScreenBoard::create( GameInfo *game, const char *modeLabel, Bool cardBars )
{
	m_kind = BOARD_MATCH;
	m_network = cardBars;
	const Bool directStrike = IsDirectStrikeMap( game->getMap() );
	const Bool apocalypse = !directStrike && IsApocalypseMap( game->getMap() );

	// a watcher, a replay's viewer among them, sees every side as it is; a player sees what the
	// lobby let him see of his enemies
	// (a director's recording has no local seat at all)
	const Bool seeAll = TheGameLogic->isInReplayGame() || game->getLocalSlotNum() < 0
		|| game->getConstSlot( game->getLocalSlotNum() )->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;
	// a ranked Quick Match, and any match on a generated map, gives a player neither the map nor any
	// start spot, his own included, so the board never tells where the game put anyone
	const Bool generated = isGeneratedMapPath( game->getMap() );
	const Bool mapHidden = ( game->getStartShrouded() || generated ) && !seeAll;
	// a seat coming back to a match under way sees the sides and colours the match is played with, and no
	// channel on a row: the others are not loading, they are in the match (the bar carries the catch-up)
	m_rejoining = TheNetwork && TheNetwork->isRejoining();
	// a mode hides no army: a Direct Strike plot stands in everyone's sight from the first wave, and
	// Apocalypse seats are all on one side against the dead
	const Bool realSides = seeAll || m_rejoining || directStrike || apocalypse;
	const Int localSlot = TheGameLogic->isInReplayGame() ? -1 : game->getLocalSlotNum();

	// a row a player, in the order the lobby lists them.  Someone watching from this screen without a
	// seat (an -observer match) is a watcher too, under the name the skirmish room would have given
	// him; a replay's viewer is not one of the match's
	std::vector< UnicodeString > observerNames;
	if( game->getLocalSlotNum() < 0 && !TheGameLogic->isInReplayGame() )
	{
		SkirmishPreferences preferences;
		observerNames.push_back( preferences.getUserName() );
	}
	std::vector< Int > lobbyTeam;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( slot == NULL || !slot->isOccupied() )
			continue;
		if( slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
		{
			observerNames.push_back( slot->getName() );
			continue;
		}

		BoardCard card;
		card.slot = i;
		const Int colorIndex = realSides ? slot->getColor() : slot->getApparentColor();
		card.color = TheMultiplayerSettings->getColor( colorIndex )->getColor();
		card.team = 0;
		card.startSpot = mapHidden ? -1 : seeAll ? slot->getStartPos() : slot->getApparentStartPos();
		card.percent = cardBars && slot->isHuman() && !m_rejoining ? 0 : -1;
		card.ai = slot->isAI();
		card.local = i == localSlot;
		card.plainSide = FALSE;
		card.portrait = NULL;
		card.badge = NULL;
		card.name = card.army = card.persona = card.detail = card.figure = card.disc = NULL;
		card.kind = LOAD_ROW_CARD;
		card.zebra = FALSE;
		card.box.lo.x = card.box.lo.y = card.box.hi.x = card.box.hi.y = 0;
		const Int sideIndex = realSides ? slot->getPlayerTemplate() : slot->getApparentPlayerTemplate();
		card.armyWords = TheGameText->fetch( "GUI:Random" );
		if( sideIndex >= 0 )
		{
			const PlayerTemplate *side = ThePlayerTemplateStore->getNthPlayerTemplate( sideIndex );
			card.portrait = UIStyle::armyPortrait( side );
			card.armyWords = side->getDisplayName();
			// the general's Challenge persona, the one the portrait is: his name and his strategy
			const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName( side->getName() ) : NULL;
			card.plainSide = general == NULL;
			if( general && general->getBioName().isNotEmpty() && general->getBioStrategy().isNotEmpty() )
			{
				card.personaWords = TheGameText->fetch( general->getBioName() );
				card.personaWords.concat( BOARD_DOT );
				card.personaWords.concat( TheGameText->fetch( general->getBioStrategy() ) );
			}
		}
		card.nameWords = slot->getName();
		// a Direct Strike team is the start's side of the lane, as the director allies them, whatever
		// team number the seat came with (an -autoskirmish seat has none); the start is dealt by now
		const Int start = slot->getStartPos();
		lobbyTeam.push_back( directStrike && start >= 0 ? DirectStrike_teamOfStart( start ) : slot->getTeamNumber() );

		// a computer's level, where its name is not already that, and a personality other than Normal
		if( slot->isAI() )
		{
			const UnicodeString level = SlotStateName( slot->getState(), slot->getAIRating() );
			if( slot->getName().compare( level ) != 0 )
				card.detailWords = level;
			if( slot->getAIPersonality() != AIPERSONA_NORMAL )
			{
				AsciiString label;
				label.format( "GUI:AIPersona%s", aiPersonalityName( (AIPersonality)slot->getAIPersonality() ) );
				if( !card.detailWords.isEmpty() )
					card.detailWords.concat( BOARD_DOT );
				card.detailWords.concat( TheGameText->fetch( label ) );
			}
		}
		m_cards.push_back( card );
	}

	// the teams the score screen's way: mutual allies from the lobby's team numbers, a player with
	// none a team of his own, in the order they first appear
	const Int count = (Int)m_cards.size();
	std::vector< Bool > allied( count * count, FALSE );
	for( Int a = 0; a < count; a++ )
	{
		for( Int b = 0; b < count; b++ )
			allied[ a * count + b ] = a != b && lobbyTeam[ a ] >= 0 && lobbyTeam[ a ] == lobbyTeam[ b ];
	}
	const std::vector< Int > teamOf = ScoreScreen_teamsFromAllies( count, allied );
	std::vector< ScoreTeam > teams;
	for( Int a = 0; a < count; a++ )
	{
		if( teamOf[ a ] >= (Int)teams.size() )
		{
			teams.resize( teamOf[ a ] + 1 );
			teams[ teamOf[ a ] ].team = teamOf[ a ];
		}
		teams[ teamOf[ a ] ].members.push_back( a );
		m_cards[ a ].team = teamOf[ a ];
	}
	Bool freeForAll = FALSE;
	const std::string format = teams.empty() ? std::string() : ScoreScreen_format( teams, &freeForAll );
	const Bool versus = teams.size() == 2;

	// a team is called by its lobby number and wears its lobby colour; a player alone takes the
	// lowest number no lobby team has, on the room's no-team slate, as the score screen calls him
	std::vector< Int > sharedLobbyTeam( teams.size(), -1 );
	std::vector< Int > firstSlot( teams.size(), 0 );
	for( size_t t = 0; t < teams.size(); t++ )
	{
		// the rows run in seat order, so a team's first member has its lowest seat
		sharedLobbyTeam[ t ] = lobbyTeam[ teams[ t ].members[ 0 ] ];
		firstSlot[ t ] = m_cards[ teams[ t ].members[ 0 ] ].slot;
	}
	const std::vector< Int > teamNumber = ScoreScreen_teamNumbers( sharedLobbyTeam, firstSlot, MAX_SLOTS + 1 );
	std::vector< Color > teamColor( teams.size(), LobbyTeamColor( -1 ) );
	for( size_t t = 0; t < teams.size(); t++ )
	{
		if( sharedLobbyTeam[ t ] >= 0 && teamNumber[ t ] == sharedLobbyTeam[ t ] + 1 )
			teamColor[ t ] = LobbyTeamColor( sharedLobbyTeam[ t ] );
	}

	// the header: the map, then what kind of game, its format as the score screen words it, the cash; a
	// mode's board is titled by the mode, as its room is
	const MapMetaData *map = TheMapCache->findMap( game->getMap() );
	UnicodeString mapName;
	if( map )
		mapName = map->m_displayName;
	else
		mapName.translate( TheGameState->getMapLeafName( game->getMap() ) );
	UnicodeString makeup;
	if( freeForAll )
	{
		UnicodeString players;
		players.format( u" (%d)", count );
		makeup = TheGameText->fetch( "GUI:LoadFreeForAll" );
		makeup.concat( players );
	}
	else if( teams.size() > 1 )
		makeup.translate( AsciiString( format.c_str() ) );
	UnicodeString subtitle = TheGameText->fetch( modeLabel );
	if( directStrike )
	{
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( TheGameText->fetch( "GUI:DirectStrikeMap" ) );
	}
	if( !makeup.isEmpty() )
	{
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( makeup );
	}
	if( !directStrike && !apocalypse )
	{
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( boardCash( game ) );
	}
	if( directStrike )
		prepare( UIStyle::capitals( TheGameText->fetch( "GUI:DirectStrikeLower" ) ), subtitle, FALSE, "GUI:LoadTipDirectStrike" );
	else if( apocalypse )
		prepare( UIStyle::capitals( TheGameText->fetch( "GUI:ApocalypseLower" ) ), subtitle, FALSE, "GUI:LoadTipApocalypse" );
	else
		prepare( UIStyle::nameCapitals( mapName ), subtitle, TRUE, "GUI:LoadTip" );

	// the rules: a mode's own, else the lobby's with the lobby's words
	m_rulesHead = UIStyle::capitals( TheGameText->fetch( "GUI:LoadRules" ) );
	addFact( m_facts, TheGameText->fetch( "GUI:GameSpeed" ), boardSpeed() );
	if( directStrike )
	{
		addFact( m_facts, TheGameText->fetch( "GUI:LoadWaveEvery" ), boardClock( boardDirectStrikeSeconds( DIRECTSTRIKE_TICK_FRAMES ) ) );
		UnicodeString income( u"$" );
		income.concat( UIStyle::grouped( DIRECTSTRIKE_INCOME ) );
		addFact( m_facts, TheGameText->fetch( "GUI:LoadIncomeWave" ), income );
		addFact( m_facts, TheGameText->fetch( "GUI:LoadWaveLife" ), boardClock( boardDirectStrikeSeconds( DIRECTSTRIKE_LIFESPAN_FRAMES ) ) );
	}
	else
	{
		addFact( m_facts, TheGameText->fetch( "GUI:StartingMoney" ), boardCash( game ) );
		addFact( m_facts, TheGameText->fetch( "GUI:Superweapons" ), TheGameText->fetch( SuperweaponCaption( game->getSuperweaponRestriction() ) ) );
		addFact( m_facts, TheGameText->fetch( "GUI:IncomeSharing" ), TheGameText->fetch( IncomeSharingCaption( game->getIncomeSharing() ) ) );
	}
	if( !directStrike && !apocalypse )
	{
		UnicodeString words;
		if( game->getSupplyPileLimit() > 0 )
			words.format( TheGameText->fetch( "GUI:SupplyPileLimitFormat" ), game->getSupplyPileLimit() );
		else
			words = TheGameText->fetch( "GUI:SupplyPileLimitOff" );
		addFact( m_facts, TheGameText->fetch( "GUI:SupplyPileLimit" ), words );
		if( game->getTechRespawn() > 0 )
			words.format( TheGameText->fetch( "GUI:TechRespawnFormat" ), game->getTechRespawn() );
		else
			words = TheGameText->fetch( "GUI:TechRespawnOff" );
		addFact( m_facts, TheGameText->fetch( "GUI:TechRespawn" ), words );
		addFact( m_facts, TheGameText->fetch( "GUI:UnitLimit" ), TheGameText->fetch( game->getUnitLimit() ? "GUI:LoadRuleOn" : "GUI:LoadRuleOff" ) );
		if( game->getDayNight() )	// only when on, so every other match's list is as long as it was
			addFact( m_facts, TheGameText->fetch( "GUI:DayNight" ), TheGameText->fetch( "GUI:LoadRuleOn" ) );
		// Hidden Map is no room's option any more; only a ranked Quick Match plays it, and says so here
		if( game->getStartShrouded() )
			addFact( m_facts, TheGameText->fetch( "GUI:LoadHiddenMap" ), TheGameText->fetch( "GUI:LoadRuleOn" ) );
	}

	// Apocalypse's nights: the cycle's four phases and its warnings, as wall clocks at the match's speed
	if( apocalypse )
	{
		m_nightsHead = UIStyle::capitals( TheGameText->fetch( "GUI:LoadNights" ) );
		const Int seconds[ 4 ] = { APOCALYPSE_DAY_SECONDS, APOCALYPSE_DUSK_SECONDS, APOCALYPSE_NIGHT_SECONDS, APOCALYPSE_DAWN_SECONDS };
		const char *const phaseKeys[ 4 ] = { "GUI:LoadDay", "GUI:LoadDusk", "GUI:LoadNight", "GUI:LoadDawn" };
		for( Int each = 0; each < 4; each++ )
		{
			m_nightSeconds[ each ] = seconds[ each ];
			m_nightWords[ each ] = UIStyle::capitals( TheGameText->fetch( phaseKeys[ each ] ) );
			m_nightClocks[ each ] = text( boardClock( LoadScreen_wallSeconds( seconds[ each ], GAME_SPEED_PERCENT ) ), font( BOARD_SMALL ) );
		}
		addFact( m_nights, UIStyle::capitals( TheGameText->fetch( "GUI:LoadFirstWave" ) ),
			boardClock( LoadScreen_wallSeconds( APOCALYPSE_FIRST_WAVE_SECONDS, GAME_SPEED_PERCENT ) ), UIStyle::INK, TRUE );
		addFact( m_nights, TheGameText->fetch( "GUI:LoadGatesGlow" ),
			boardClock( LoadScreen_wallSeconds( APOCALYPSE_TELEGRAPH_SECONDS, GAME_SPEED_PERCENT ) ) );
		addFact( m_nights, UIStyle::capitals( TheGameText->fetch( "GUI:LoadBloodMoon" ) ),
			TheGameText->fetch( "GUI:LoadEveryThirdNight" ), GameMakeColor( 236, 104, 92, 255 ), TRUE );
		m_nights.back().blood = TRUE;
	}

	// the middle: VS, the map or its well, the nights, the rules, the watchers
	Real aspect = 1.0f;
	if( map && map->m_extent.width() > 0.0f && map->m_extent.height() > 0.0f )
		aspect = map->m_extent.width() / map->m_extent.height();
	m_wellShown = mapHidden || generated;
	m_middle = LoadScreen_planMiddle( versus, aspect, m_wellShown, (Int)m_facts.size(), !observerNames.empty(), apocalypse );
	m_middleShown = TRUE;
	m_vs = versus ? text( UnicodeString( u"VS" ), font( BOARD_TITLE, TRUE ) ) : NULL;
	m_unknown = text( UnicodeString( u"?" ), font( BOARD_UNKNOWN, TRUE ) );
	m_unknownSmall = text( UnicodeString( u"?" ), font( BOARD_SMALL, TRUE ) );
	if( m_wellShown )
	{
		const char *wellKey = directStrike ? "GUI:DirectStrikeIslandNote" : generated ? "GUI:LoadRandomMap" : NULL;
		if( wellKey )
			m_wellWords = text( UIStyle::fitText( TheGameText->fetch( wellKey ), font( BOARD_BODY ), scaled( (Real)( LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT - 32 ) ) ), font( BOARD_BODY ) );
	}
	else
		m_mapImage = getMapPreviewImage( game->getMap() );
	// the map's preview is the backdrop too, under the glass
	m_backdrop = m_mapImage;
	m_observersHead = boardHeadWords( "GUI:LoadObservers" );
	if( !observerNames.empty() )
	{
		// as many names as the line holds, then how many more
		GameFont *bodyFont = font( BOARD_BODY );
		const Int room = scaled( (Real)( LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT - 16 ) );
		m_observers = text( UnicodeString::TheEmptyString, bodyFont );
		const Int total = (Int)observerNames.size();
		for( Int shown = total; shown >= 1; shown-- )
		{
			UnicodeString line;
			for( Int each = 0; each < shown; each++ )
			{
				if( each > 0 )
					line.concat( u", " );
				line.concat( UIStyle::fitName( observerNames[ each ], bodyFont, room / 2 ) );
			}
			if( shown < total )
			{
				UnicodeString more;
				more.format( u"  +%d", total - shown );
				line.concat( more );
			}
			m_observers->setText( line );
			if( boardStringWidth( m_observers ) <= room )
				break;
		}
	}

	// the start spots the others may see on the map's own shape, the player's colour and seat on each
	if( map && !m_wellShown )
	{
		const Int mapWidth = m_middle.map.hi.x - m_middle.map.lo.x;
		const Int mapHeight = m_middle.map.hi.y - m_middle.map.lo.y;
		for( Int start = 0; start < map->m_numPlayers && map->m_isMultiplayer; start++ )
		{
			AsciiString waypoint;
			waypoint.format( "Player_%d_Start", start + 1 );
			WaypointMap::const_iterator it = map->m_waypoints.find( waypoint );
			if( it == map->m_waypoints.end() )
				continue;
			const Real across = ( it->second.x - map->m_extent.lo.x ) / map->m_extent.width();
			const Real down = 1.0f - ( it->second.y - map->m_extent.lo.y ) / map->m_extent.height();
			for( size_t each = 0; each < m_cards.size(); each++ )
			{
				if( m_cards[ each ].startSpot != start )
					continue;
				ICoord2D at;
				at.x = m_middle.map.lo.x + REAL_TO_INT( across * mapWidth );
				at.y = m_middle.map.lo.y + REAL_TO_INT( down * mapHeight );
				m_spotAt.push_back( at );
				m_spotCard.push_back( (Int)each );
			}
		}
	}
	GameFont *numberFont = font( BOARD_CAPS, TRUE );
	for( size_t each = 0; each < m_spotCard.size(); each++ )
	{
		BoardCard &card = m_cards[ m_spotCard[ each ] ];
		if( card.disc == NULL )
		{
			// the seat as a number, so seat 9 and past read as 9 and not as a missing string
			UnicodeString seat;
			seat.format( u"%d", card.slot + 1 );
			card.disc = text( seat, numberFont );
		}
	}

	// the wings: two teams face each other; more go whole to the wing with fewer rows; one team (the
	// Apocalypse's survivors) runs on from the left wing into the right
	struct WingGroup
	{
		Int team;
		std::vector< Int > members;
		Bool counts;
	};
	std::vector< WingGroup > wings[ 2 ];
	if( teams.size() == 1 )
	{
		const std::vector< Int > &members = teams[ 0 ].members;
		const Int half = ( (Int)members.size() + 1 ) / 2;
		WingGroup left, right;
		left.team = right.team = 0;
		left.counts = TRUE;
		right.counts = FALSE;
		left.members.assign( members.begin(), members.begin() + half );
		right.members.assign( members.begin() + half, members.end() );
		wings[ 0 ].push_back( left );
		if( !right.members.empty() )
			wings[ 1 ].push_back( right );
	}
	else
	{
		std::vector< Int > sizes( teams.size() );
		for( size_t t = 0; t < teams.size(); t++ )
			sizes[ t ] = (Int)teams[ t ].members.size();
		const std::vector< Int > wingOf = LoadScreen_wingOfTeams( sizes );
		for( size_t t = 0; t < teams.size(); t++ )
		{
			WingGroup group;
			group.team = (Int)t;
			group.members = teams[ t ].members;
			group.counts = TRUE;
			wings[ wingOf[ t ] ].push_back( group );
		}
	}
	m_readyWords = UIStyle::capitals( TheGameText->fetch( "GUI:LoadReady" ) );
	for( Int side = 0; side < 2; side++ )
	{
		Int rows = 0;
		Bool alone = TRUE;
		for( size_t g = 0; g < wings[ side ].size(); g++ )
		{
			rows += (Int)wings[ side ][ g ].members.size();
			alone = alone && wings[ side ][ g ].members.size() == 1;
		}
		if( rows == 0 )
			continue;
		const LoadWingPlan plan = LoadScreen_planWing( (Int)wings[ side ].size(), rows, alone );
		const Int left = side == 0 ? LOAD_LEFT : LOAD_RIGHT - LOAD_WING_WIDTH;
		const Int right = left + LOAD_WING_WIDTH;
		Int y = LOAD_BODY_TOP;
		for( size_t g = 0; g < wings[ side ].size(); g++ )
		{
			const WingGroup &group = wings[ side ][ g ];
			if( plan.head != LOAD_HEAD_NONE )
			{
				BoardHead head;
				head.kind = plan.head;
				head.box.lo.x = left;
				head.box.lo.y = y;
				head.box.hi.x = right;
				head.box.hi.y = y + plan.headHeight;
				head.color = teamColor[ group.team ];
				head.toward = versus ? ( side == 0 ? 1 : -1 ) : 0;
				if( apocalypse )
					head.words = UIStyle::capitals( TheGameText->fetch( "GUI:LoadSurvivors" ) );
				else
				{
					UnicodeString label;
					label.format( TheGameText->fetch( "GUI:LoadTeam" ), teamNumber[ group.team ] );
					head.words = UIStyle::capitals( label );
				}
				// a rejoining seat sees no counts: nobody is loading but itself
				if( group.counts && !m_rejoining )
					head.counted = teams[ group.team ].members;
				m_heads.push_back( head );
				y += plan.headHeight;
			}
			for( size_t m = 0; m < group.members.size(); m++ )
			{
				BoardCard &card = m_cards[ group.members[ m ] ];
				card.kind = plan.row;
				card.zebra = plan.row == LOAD_ROW_LINE && ( m % 2 ) == 1;
				card.box.lo.x = left;
				card.box.lo.y = y;
				card.box.hi.x = right;
				card.box.hi.y = y + plan.rowHeight;
				y += plan.rowPitch;
			}
			y += plan.gap;
		}
	}

	// each row's words in its kind's type, fitted to it
	m_cardFigure = text( UIStyle::percentText( 100 ), font( BOARD_NAME ) );
	m_lineFigure = text( UIStyle::percentText( 100 ), font( BOARD_BODY ) );
	for( size_t each = 0; each < m_cards.size(); each++ )
	{
		BoardCard &card = m_cards[ each ];
		const Bool line = card.kind == LOAD_ROW_LINE;
		card.name = text( card.nameWords, font( line ? BOARD_BODY : BOARD_NAME ) );
		card.army = text( card.armyWords, font( line ? BOARD_SMALL : BOARD_BODY ) );
		if( !line && !card.personaWords.isEmpty() )
			card.persona = text( card.personaWords, font( BOARD_BODY ) );
		if( card.ai && !card.detailWords.isEmpty() )
			card.detail = text( card.detailWords, font( line ? BOARD_SMALL : BOARD_BODY ) );
		card.figure = text( UIStyle::percentText( max( card.percent, 0 ) ), font( line ? BOARD_BODY : BOARD_NAME ) );
		fitCard( card );
	}

	refreshLabel();
	return makeWindow( NULL );
}

/** A row of the rules or the nights: the value kept to three fifths of the row, the label shortened by
	* its language's rule before it would touch the value. */
void LoadScreenBoard::addFact( std::vector< BoardFact > &facts, const UnicodeString &label, const UnicodeString &value, Color labelColor, Bool caps )
{
	GameFont *bodyFont = font( BOARD_BODY );
	const Int room = scaled( (Real)( LOAD_MIDDLE_RIGHT - LOAD_MIDDLE_LEFT - 32 ) );
	BoardFact fact;
	fact.value = text( value, bodyFont );
	if( boardStringWidth( fact.value ) > room * 3 / 5 )
		fact.value->setText( UIStyle::fitText( value, bodyFont, room * 3 / 5 ) );
	const Int labelRoom = room - boardStringWidth( fact.value );
	fact.labelWords = label;
	fact.label = NULL;
	if( !caps )
		fact.label = text( UIStyle::fitText( label, bodyFont, labelRoom ), bodyFont );
	fact.labelColor = labelColor;
	fact.capsLabel = caps;
	fact.blood = FALSE;
	facts.push_back( fact );
}

/// the pitch of a card's last line: a computer's level or personality, a human's channel, or none
Int LoadScreenBoard::cardLast( const BoardCard &card ) const
{
	if( card.detail )
		return LOAD_PITCH_LINE;
	return card.percent >= 0 ? LOAD_PITCH_BAR : 0;
}

/** Where a card's portrait and lines stand, on the 1080 board: a card's portrait as tall as its row
	* at its left with the lines centred beside it; a poster's as large as the wing allows with the lines
	* under it; a line row's small, the words along the line. */
void LoadScreenBoard::cardFrame( const BoardCard &card, IRegion2D &portrait, Int &textLeft, Int &textRight, Int &textMiddle ) const
{
	const IRegion2D &r = card.box;
	const Int width = r.hi.x - r.lo.x;
	const Int height = r.hi.y - r.lo.y;
	const Int lines = LoadScreen_cardText( card.persona != NULL, cardLast( card ) );
	Int side;
	switch( card.kind )
	{
		case LOAD_ROW_POSTER:
		{
			side = LoadScreen_posterPortrait( width, height, lines, card.plainSide );
			const Int top = r.lo.y + max( 8, ( height - side - ( lines + 20 ) ) / 2 );
			portrait.lo.x = r.lo.x + 18 + ( width - 36 - side ) / 2;
			portrait.lo.y = top;
			textLeft = r.lo.x + 18;
			textRight = r.hi.x - 12;
			textMiddle = top + side + 20 + lines / 2;
			break;
		}
		case LOAD_ROW_CARD:
			side = LoadScreen_cardPortrait( height );
			portrait.lo.x = r.lo.x + 18;
			portrait.lo.y = r.lo.y + ( height - side ) / 2;
			textLeft = portrait.lo.x + side + 20;
			textRight = r.hi.x - 12;
			textMiddle = ( r.lo.y + r.hi.y ) / 2;
			break;
		default:
			side = max( 1, height - 8 );
			portrait.lo.x = r.lo.x + 16;
			portrait.lo.y = r.lo.y + 4;
			textLeft = portrait.lo.x + side + 10;
			textRight = r.hi.x - 8;
			textMiddle = ( r.lo.y + r.hi.y ) / 2;
			break;
	}
	portrait.hi.x = portrait.lo.x + side;
	portrait.hi.y = portrait.lo.y + side;
}

/** A row's words fitted to it: a name cut with an ellipsis only, the rest shortened by its language's
	* rule; a line row's name to 168, its general to 150, as the design sets them. */
void LoadScreenBoard::fitCard( BoardCard &card )
{
	IRegion2D portrait;
	Int textLeft, textRight, textMiddle;
	cardFrame( card, portrait, textLeft, textRight, textMiddle );
	GameFont *nameFont = card.name->getFont();
	const Int badgeRoom = card.badge ? nameFont->height * 3 / 4 + scaled( 10 ) : 0;
	if( card.kind == LOAD_ROW_LINE )
	{
		card.name->setText( UIStyle::fitName( card.nameWords, nameFont, scaled( 168 ) - badgeRoom ) );
		card.army->setText( UIStyle::fitText( card.armyWords, card.army->getFont(), scaled( 150 ) ) );
		if( card.detail )
		{
			const Int detailLeft = textLeft + 176 + 160;
			const Int detailRight = textRight - ( card.disc ? 30 : 0 );
			card.detail->setText( UIStyle::fitText( card.detailWords, card.detail->getFont(), scaled( (Real)max( 0, detailRight - detailLeft ) ) ) );
		}
		return;
	}
	const Int room = scaled( (Real)( textRight - textLeft ) );
	const Int nameRoom = room - ( card.disc ? scaled( 40 ) : 0 ) - badgeRoom;
	card.name->setText( UIStyle::fitName( card.nameWords, nameFont, nameRoom ) );
	card.army->setText( UIStyle::fitText( card.armyWords, card.army->getFont(), room ) );
	if( card.persona )
		card.persona->setText( UIStyle::fitText( card.personaWords, card.persona->getFont(), room ) );
	if( card.detail )
		card.detail->setText( UIStyle::fitText( card.detailWords, card.detail->getFont(), room ) );
}

/** The header's and the foot's words, the same on every board: the title cut before it would crowd the
	* subtitle out, the subtitle shortened before it would reach the wordmark; the tips that fit the foot's
	* one line. */
void LoadScreenBoard::prepare( const UnicodeString &titleInCapitals, const UnicodeString &subtitle, Bool titleIsName, const char *tipKey )
{
	GameFont *titleFont = font( BOARD_TITLE, TRUE );
	GameFont *subtitleFont = font( BOARD_SUBTITLE );
	GameFont *wordmarkFont = font( BOARD_NAME, TRUE );
	m_showWordmark = m_kind != BOARD_MISSION;	// a mission's picture is the game's own logo art
	m_wordmark[ 0 ] = text( UnicodeString( u"Zero Hour" ), wordmarkFont );
	m_wordmark[ 1 ] = text( UnicodeString( u" Reforged" ), wordmarkFont );
	m_subtitle = text( subtitle, subtitleFont );

	Int right = X( (Real)LOAD_RIGHT );
	if( m_showWordmark )
		right -= boardStringWidth( m_wordmark[ 0 ] ) + boardStringWidth( m_wordmark[ 1 ] ) + scaled( 48 );
	// the right edge of a mission's title row is its count and pips, which stand in the last 420
	if( m_kind == BOARD_MISSION )
		right -= scaled( 420 );
	const Int room = right - X( (Real)LOAD_LEFT );
	m_titleWords = titleInCapitals;
	const Int titleMost = room * 3 / 5;
	if( UIStyle::trackedWidth( m_titleWords, titleFont, BOARD_TITLE_TRACKING ) > titleMost )
	{
		const Int untracked = titleMost - scaled( BOARD_TITLE_TRACKING ) * titleInCapitals.getLength();
		m_titleWords = titleIsName ? UIStyle::fitName( titleInCapitals, titleFont, untracked ) : UIStyle::fitText( titleInCapitals, titleFont, untracked );
	}
	const Int titleWidth = UIStyle::trackedWidth( m_titleWords, titleFont, BOARD_TITLE_TRACKING );
	m_subtitle->setText( UIStyle::fitText( subtitle, subtitleFont, room - titleWidth - scaled( 24 ) ) );

	// the foot: the TIP chip and a tip on one line beside it; one that wants a second line never shows
	m_tipKey = tipKey;
	m_tipLabel = UIStyle::capitals( TheGameText->fetch( "GUI:LoadTip" ) );
	m_tip = text( UnicodeString::TheEmptyString, font( BOARD_BODY ) );
	m_tips.clear();
	if( m_tipKey )
	{
		const Int chip = UIStyle::trackedWidth( m_tipLabel, font( BOARD_CAPS, TRUE ), UIStyle::MENU_LABEL_TRACKING ) + scaled( 20 );
		const Int tipRoom = X( (Real)LOAD_RIGHT ) - ( X( (Real)LOAD_LEFT ) + chip + scaled( 18 ) );
		for( Int number = 1; ; number++ )
		{
			Bool exists = FALSE;
			const UnicodeString words = tipText( number, &exists );
			if( !exists )
				break;
			m_tip->setText( words );
			if( boardStringWidth( m_tip ) <= tipRoom )
				m_tips.push_back( number );
		}
	}
	// one at random to start, the next every eight seconds; the clock is the wall's, never the
	// logic's dice
	m_tipAt = m_tips.empty() ? 0 : (Int)( Clock_Milliseconds() % m_tips.size() );
	m_tipSince = Clock_Milliseconds();
	m_tip->setText( m_tips.empty() ? UnicodeString::TheEmptyString : tipText( m_tips[ m_tipAt ] ) );

	m_progressFormat = TheGameText->fetch( "GUI:LoadProgress" );
	m_localPercent = 0;
	refreshLabel();
}

/** The load bar's words: "LOADING 81%"; a network seat done with its own load names the slowest of
	* the others, "WAITING FOR KARA MURAT · 23%"; a rejoining seat's and a transfer's are set by them. */
void LoadScreenBoard::refreshLabel( void )
{
	if( m_rejoinShown != -2 || m_kind == BOARD_TRANSFER )
		return;
	UnicodeString words;
	if( m_network && m_localPercent > 100 )
	{
		const BoardCard *slowest = NULL;
		for( size_t each = 0; each < m_cards.size(); each++ )
		{
			const BoardCard &card = m_cards[ each ];
			if( card.percent >= 0 && card.percent < 100 && ( slowest == NULL || card.percent < slowest->percent ) )
				slowest = &card;
		}
		if( slowest )
			words.format( TheGameText->fetch( "GUI:LoadWaitingFor" ), UIStyle::nameCapitals( slowest->nameWords ).str(), slowest->percent );
		else
			words = UIStyle::capitals( TheGameText->fetch( "GUI:LoadWaiting" ) );
	}
	else
	{
		words.format( m_progressFormat, min( max( m_localPercent, 0 ), 100 ) );
		words = UIStyle::capitals( words );
	}
	m_label = words;
}

GameWindow *LoadScreenBoard::makeWindow( GameWindow *parent )
{
	GameWindow *window = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED, 0, 0, m_width, m_height, GameWinDefaultSystem );
	window->winSetUserData( this );
	window->winSetDrawFunc( drawWindow );
	window->winBringToTop();
	return window;
}

GameWindow *LoadScreenBoard::createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
	const UnicodeString *briefing, Int briefingLines, Int missionNumber, Int missionCount, const Image *backdrop )
{
	m_kind = BOARD_MISSION;
	m_backdrop = backdrop;
	m_ground = parent == NULL;
	m_missionNumber = missionNumber;
	m_missionCount = missionNumber > 0 ? missionCount : 0;
	if( m_missionCount > 0 )
		m_missionOf.format( TheGameText->fetch( "GUI:LoadMissionOf" ), missionNumber, missionCount );
	m_missionOf = UIStyle::capitals( m_missionOf );
	prepare( UIStyle::capitals( title ), subtitle, FALSE, "GUI:LoadTip" );

	// the briefing the mission's load always had and never showed, on glass over the left of the
	// picture, a line every 44 and the box grown to hold them
	m_briefingHead = UIStyle::capitals( TheGameText->fetch( "GUI:LoadBriefing" ) );
	GameFont *lineFont = font( BOARD_NAME );
	Int y = LOAD_BRIEFING_TOP + 20 + LOAD_SECTION_HEAD + 4;
	for( Int line = 0; line < briefingLines; line++ )
	{
		if( briefing[ line ].isEmpty() )
			continue;
		DisplayString *words = text( briefing[ line ], lineFont );
		words->setWordWrap( scaled( (Real)( LOAD_BRIEFING_RIGHT - LOAD_LEFT - 48 ) ) );
		m_briefing.push_back( words );
		const Int lines = max( 1, ( boardStringHeight( words ) + lineFont->height / 2 ) / max( 1, lineFont->height ) );
		y += LOAD_BRIEFING_PITCH * lines;
	}
	m_briefingBottom = max( LOAD_BRIEFING_LEAST, y + 8 );
	return makeWindow( parent );
}

GameWindow *LoadScreenBoard::createTransfer( GameInfo *game, const char *modeLabel )
{
	m_kind = BOARD_TRANSFER;
	const MapMetaData *map = TheMapCache->findMap( game->getMap() );
	UnicodeString mapName;
	if( map )
		mapName = map->m_displayName;
	else
		mapName.translate( TheGameState->getMapLeafName( game->getMap() ) );
	UnicodeString subtitle = TheGameText->fetch( modeLabel );
	subtitle.concat( UIStyle::HEADER_JOINER );
	subtitle.concat( TheGameText->fetch( "GUI:LoadMapTransfer" ) );
	prepare( UIStyle::nameCapitals( mapName ), subtitle, TRUE, NULL );
	m_playersHead = UIStyle::capitals( TheGameText->fetch( "GUI:LoadPlayers" ) );
	m_statusHead = UIStyle::capitals( TheGameText->fetch( "GUI:LoadStatus" ) );

	// every human in seat order, half in each column; the host and a player who has the map show their
	// state without a channel, as EA's screen hid their bars
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		const GameSlot *slot = game->getConstSlot( i );
		if( slot == NULL || !slot->isHuman() )
			continue;
		BoardTransfer row;
		row.slot = i;
		row.color = TheMultiplayerSettings->getColor( slot->getApparentColor() )->getColor();
		row.local = i == game->getLocalSlotNum();
		row.percent = i == 0 || slot->hasMap() ? -1 : 0;
		row.name = text( UIStyle::fitName( slot->getName(), font( BOARD_NAME ), scaled( 290 - 22 ) ), font( BOARD_NAME ) );
		row.state = text( UnicodeString::TheEmptyString, font( BOARD_BODY ) );
		row.figure = text( UIStyle::percentText( 0 ), font( BOARD_NAME ) );
		row.zebra = FALSE;
		m_transfers.push_back( row );
	}
	const Int count = (Int)m_transfers.size();
	const Int half = ( count + 1 ) / 2;
	for( Int each = 0; each < count; each++ )
	{
		const Int column = each < half ? 0 : 1;
		const Int index = column == 0 ? each : each - half;
		const Int rows = column == 0 ? half : count - half;
		const Int pitch = LoadScreen_transferRow( rows );
		BoardTransfer &row = m_transfers[ each ];
		row.box.lo.x = column == 0 ? LOAD_LEFT : LOAD_RIGHT - LOAD_TRANSFER_WIDTH;
		row.box.hi.x = row.box.lo.x + LOAD_TRANSFER_WIDTH;
		row.box.lo.y = LOAD_BODY_TOP + 4 + LOAD_SECTION_HEAD + index * pitch;
		row.box.hi.y = row.box.lo.y + pitch - 2;
		row.zebra = ( index % 2 ) == 1;
	}
	m_transferFile = text( UnicodeString::TheEmptyString, font( BOARD_BODY ) );
	m_transferTimeout = text( UnicodeString::TheEmptyString, font( BOARD_NAME ) );
	m_cardFigure = text( UIStyle::percentText( 100 ), font( BOARD_NAME ) );
	m_localPercent = 0;
	return makeWindow( NULL );
}

/** A player's transfer as the network reports it: his state in words and how far his copy has come.
	* The local seat's bar carries his own: the host's sending, a joiner's receiving. */
void LoadScreenBoard::setTransfer( Int slot, Int percent, const UnicodeString &state )
{
	percent = min( max( percent, 0 ), 100 );
	for( size_t each = 0; each < m_transfers.size(); each++ )
	{
		BoardTransfer &row = m_transfers[ each ];
		if( row.slot != slot )
			continue;
		row.state->setText( UIStyle::fitText( state, row.state->getFont(), scaled( 470 - 300 ) ) );
		if( row.percent >= 0 )
		{
			row.percent = percent;
			row.figure->setText( UIStyle::percentText( row.percent ) );
		}
		if( row.local )
		{
			m_localPercent = percent;
			m_label = UIStyle::capitals( state );
			if( percent < 100 )
			{
				m_label.concat( u"  " );
				m_label.concat( UIStyle::percentText( percent ) );
			}
		}
	}
}

void LoadScreenBoard::setTransferFoot( const UnicodeString *file, const UnicodeString *timeout )
{
	if( file && m_transferFile )
		m_transferFile->setText( UIStyle::fitText( *file, m_transferFile->getFont(), scaled( (Real)LOAD_TRANSFER_WIDTH ) ) );
	if( timeout && m_transferTimeout )
		m_transferTimeout->setText( *timeout );
}

/** An online player's rank beside his name, which gives the badge its room. */
void LoadScreenBoard::setBadge( Int slot, const Image *badge )
{
	BoardCard *card = cardFor( slot );
	if( card == NULL || card->name == NULL )
		return;
	card->badge = badge;
	fitCard( *card );
}

void LoadScreenBoard::setProgress( Int slot, Int percent )
{
	BoardCard *card = cardFor( slot );
	if( card == NULL || card->percent < 0 || card->percent == percent )
		return;
	card->percent = percent;
	card->figure->setText( UIStyle::percentText( percent ) );
	refreshLabel();
}

void LoadScreenBoard::setLocalProgress( Int percent )
{
	if( percent == m_localPercent )
		return;
	m_localPercent = percent;
	refreshLabel();
}

/** A seat coming back plays the match up to the room's frame behind the board: the bar fills with the
	* frames played against the frames to play, or reads that the match's log is still on its way. */
void LoadScreenBoard::setRejoinProgress( Int percent, Bool waitingForLog )
{
	percent = waitingForLog ? 0 : min( max( percent, 0 ), 100 );
	const Int shown = waitingForLog ? -1 : percent;		// a value the load's own percentages never take
	if( shown == m_rejoinShown )
		return;
	m_rejoinShown = shown;
	m_localPercent = percent;
	UnicodeString words;
	if( waitingForLog )
		words = TheGameText->fetch( "GUI:RejoinWaitingForLog" );
	else
		words.format( TheGameText->fetch( "GUI:RejoinCatchingUp" ), percent );
	m_label = UIStyle::capitals( words );
}

void LoadScreenBoard::drawWindow( GameWindow *window, WinInstanceData *instData )
{
	( (LoadScreenBoard *)window->winGetUserData() )->draw();
}

void LoadScreenBoard::draw( void )
{
	if( m_tips.size() > 1 && Clock_Milliseconds() - m_tipSince > BOARD_TIP_MILLISECONDS )
	{
		m_tipAt = ( m_tipAt + 1 ) % (Int)m_tips.size();
		m_tipSince = Clock_Milliseconds();
		m_tip->setText( tipText( m_tips[ m_tipAt ] ) );
	}
	// behind: a match's map preview or a saved game's mission picture; the shell's dark where the map is
	// hidden or made, or where no window's picture shows
	if( m_ground )
		TheDisplay->drawFillRect( 0, 0, m_width, m_height, UIStyle::GROUND );
	if( m_backdrop )
		UIStyle::coverImage( m_backdrop, 0, 0, m_width, m_height );
	if( m_kind == BOARD_MISSION )
		drawMission();
	else
	{
		const IRegion2D panel = onScreen( UIStyle::MENU_PANEL_LEFT, UIStyle::MENU_PANEL_TOP, UIStyle::MENU_PANEL_RIGHT, UIStyle::MENU_PANEL_BOTTOM );
		UIStyle::glass( panel, 168 );
		UIStyle::frameA( panel );
		if( m_kind == BOARD_TRANSFER )
			drawTransfer();
		else
			drawMatch();
	}
	drawHeader();
	drawFoot();
}

/** The title tracked in its bold capitals, the subtitle after it, the wordmark at the right, the lit
	* header rule. */
void LoadScreenBoard::drawHeader( void )
{
	GameFont *titleFont = font( BOARD_TITLE, TRUE );
	const Int middle = ( Y( (Real)LOAD_TITLE_TOP ) + Y( (Real)LOAD_TITLE_BOTTOM ) ) / 2;
	const Int titleWidth = UIStyle::drawTracked( m_titleWords, titleFont, X( (Real)LOAD_LEFT ), middle - titleFont->height / 2, UIStyle::INK, BOARD_TITLE_TRACKING );
	if( m_subtitle && m_subtitle->getTextLength() > 0 )
		m_subtitle->draw( X( (Real)LOAD_LEFT ) + titleWidth + scaled( 24 ), middle - boardStringHeight( m_subtitle ) / 2, UIStyle::MUTED, BOARD_SHADOW );
	if( m_showWordmark )
		UIStyle::wordmark( m_wordmark, X( (Real)LOAD_RIGHT ), middle - boardStringHeight( m_wordmark[ 0 ] ) / 2 );
	const IRegion2D rule = onScreen( UIStyle::MENU_PANEL_LEFT + 2, LOAD_HEADER_RULE, UIStyle::MENU_PANEL_RIGHT - 2, LOAD_HEADER_RULE + 2 );
	UIStyle::litLine( rule.lo.x, rule.lo.y, rule.hi.x - rule.lo.x, max( 1, rule.hi.y - rule.lo.y ), UIStyle::FRAME );
}

/** The foot: a faint rule, the TIP chip with a tip on one line (a transfer's file and its time left in
	* that place), and the load bar, the screen's one lit line, filling. */
void LoadScreenBoard::drawFoot( void )
{
	const Int left = X( (Real)LOAD_LEFT );
	const Int right = X( (Real)LOAD_RIGHT );
	TheDisplay->drawFillRect( left, Y( (Real)LOAD_FOOT_RULE ), right - left, 1, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	const Int tipTop = Y( (Real)LOAD_TIP_TOP );
	const Int tipBottom = Y( (Real)LOAD_TIP_BOTTOM );
	const Int tipMiddle = ( tipTop + tipBottom ) / 2;
	if( m_kind == BOARD_TRANSFER )
	{
		if( m_transferFile )
			m_transferFile->draw( left, tipMiddle - boardStringHeight( m_transferFile ) / 2, UIStyle::MUTED, BOARD_SHADOW );
		if( m_transferTimeout )
			m_transferTimeout->draw( right - boardStringWidth( m_transferTimeout ), tipMiddle - boardStringHeight( m_transferTimeout ) / 2, UIStyle::INK, BOARD_SHADOW );
	}
	else if( m_tip && m_tip->getTextLength() > 0 )
	{
		GameFont *capsFont = font( BOARD_CAPS, TRUE );
		const Int chip = UIStyle::trackedWidth( m_tipLabel, capsFont, UIStyle::MENU_LABEL_TRACKING ) + scaled( 20 );
		TheDisplay->drawOpenRect( left, tipTop + scaled( 8 ), chip, tipBottom - tipTop - 2 * scaled( 8 ), 1.0f, UIStyle::FRAME );
		UIStyle::drawTracked( m_tipLabel, capsFont, left + scaled( 10 ), tipMiddle - capsFont->height / 2, UIStyle::ACTIVE, UIStyle::MENU_LABEL_TRACKING );
		m_tip->draw( left + chip + scaled( 18 ), tipMiddle - boardStringHeight( m_tip ) / 2, UIStyle::INK, BOARD_SHADOW );
	}
	boardDrawLoadBar( onScreen( LOAD_LEFT, LOAD_BAR_TOP, LOAD_RIGHT, LOAD_BAR_BOTTOM ), m_localPercent, m_label,
		font( BOARD_NAME, TRUE ), BOARD_BAR_TRACKING );
}

/** A section's head (RULES, OBSERVERS, THE NIGHTS): green tracked capitals and a lit FRAME rule. */
void LoadScreenBoard::drawSection( const UnicodeString &words, Int left, Int right, Int top )
{
	GameFont *capsFont = font( BOARD_CAPS, TRUE );
	UIStyle::drawTracked( words, capsFont, left, top + ( scaled( 22 ) - capsFont->height ) / 2, UIStyle::ACTIVE, UIStyle::MENU_LABEL_TRACKING );
	UIStyle::litLine( left, top + scaled( 26 ), right - left, 1, UIStyle::FRAME );
}

/** Two chevrons pointing toward (1 right, -1 left), size tall, from x. */
static void boardDrawChevrons( Int x, Int middle, Int size, Int toward, Color color )
{
	const Int half = max( 2, size / 2 );
	const Int thick = max( 2, size / 5 );
	for( Int which = 0; which < 2; which++ )
	{
		const Int x0 = x + which * ( half + thick + 1 );
		for( Int row = -half; row <= half; row++ )
		{
			const Int depth = half - ( row < 0 ? -row : row );
			const Int at = toward > 0 ? x0 + depth : x0 + half - depth;
			TheDisplay->drawFillRect( at, middle + row, thick, 1, color );
		}
	}
}

void LoadScreenBoard::drawHead( const BoardHead &head )
{
	const IRegion2D box = onScreen( head.box );
	const Color lit = UIStyle::readable( head.color );
	if( head.kind == LOAD_HEAD_RULE )
	{
		TheDisplay->drawFillRect( box.lo.x, box.lo.y + scaled( 4 ), box.hi.x - box.lo.x, max( 2, scaled( 3 ) ), lit );
		return;
	}
	const Bool big = head.kind == LOAD_HEAD_CARD;
	const Int height = scaled( big ? 44 : 34 );
	const Int middle = box.lo.y + height / 2;
	UIStyle::litLine( box.lo.x, box.lo.y + scaled( 4 ), max( 2, scaled( 4 ) ), height - 2 * scaled( 4 ), lit );
	GameFont *labelFont = font( big ? BOARD_HEAD : BOARD_BODY, TRUE );
	const Int labelWidth = UIStyle::drawTracked( head.words, labelFont, box.lo.x + scaled( 16 ), middle - labelFont->height / 2,
		UIStyle::INK, big ? 2.4f : 2.0f );
	if( head.toward != 0 )
		boardDrawChevrons( box.lo.x + scaled( 16 ) + labelWidth + scaled( 14 ), middle, scaled( big ? 12 : 10 ), head.toward, lit );
	if( !head.counted.empty() )
	{
		Int ready = 0;
		for( size_t each = 0; each < head.counted.size(); each++ )
		{
			const BoardCard &card = m_cards[ head.counted[ each ] ];
			if( card.ai || card.percent < 0 || card.percent >= 100 )
				ready++;
		}
		UnicodeString figureWords;
		figureWords.format( u"%d/%d", ready, (Int)head.counted.size() );
		GameFont *figureFont = font( big ? BOARD_TITLE : BOARD_NAME );
		const Int figureWidth = UIStyle::trackedWidth( figureWords, figureFont, 0.0f );
		UIStyle::drawTracked( figureWords, figureFont, box.hi.x - figureWidth, middle - figureFont->height / 2 - scaled( big ? 3 : 1 ), UIStyle::INK, 0.0f );
		GameFont *capsFont = font( BOARD_CAPS, TRUE );
		const Int readyWidth = UIStyle::trackedWidth( m_readyWords, capsFont, 1.4f );
		UIStyle::drawTracked( m_readyWords, capsFont, box.hi.x - figureWidth - scaled( 12 ) - readyWidth, middle - capsFont->height / 2 + scaled( 2 ),
			ready == (Int)head.counted.size() ? UIStyle::ACTIVE : UIStyle::MUTED, 1.4f );
	}
	UIStyle::litLine( box.lo.x, box.lo.y + height + scaled( 6 ), box.hi.x - box.lo.x, 1, UIStyle::FRAME );
}

/** A portrait in its box, cut to cover it, a "?" well for a random side, the player's colour along its
	* foot. */
void LoadScreenBoard::drawPortrait( const BoardCard &card, const IRegion2D &box, Bool lineRow )
{
	const Int width = box.hi.x - box.lo.x;
	const Int height = box.hi.y - box.lo.y;
	if( card.portrait )
		UIStyle::coverImage( card.portrait, box.lo.x, box.lo.y, width, height );
	else
	{
		TheDisplay->drawFillRect( box.lo.x, box.lo.y, width, height, GameMakeColor( 10, 12, 20, 230 ) );
		TheDisplay->drawOpenRect( box.lo.x, box.lo.y, width, height, 1.0f, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
		DisplayString *mark = lineRow ? m_unknownSmall : m_unknown;
		if( mark )
			mark->draw( box.lo.x + ( width - boardStringWidth( mark ) ) / 2, box.lo.y + ( height - boardStringHeight( mark ) ) / 2, UIStyle::MUTED, BOARD_SHADOW );
	}
	const Int foot = max( 2, scaled( lineRow ? 3.0f : 4.0f ) );
	TheDisplay->drawFillRect( box.lo.x, box.hi.y - foot, width, foot, card.color );
	const Color lit = UIStyle::readable( card.color );
	if( lit != card.color )		// a colour too dark to read keeps a light keyline over it
		TheDisplay->drawFillRect( box.lo.x, box.hi.y - foot - 1, width, 1, lit );
}

/** A player's load: a dark channel filled in his colour, lifted where it is dark, the leading edge lit
	* while it moves; a finished one dims. */
void LoadScreenBoard::drawChannel( Int left, Int right, Int middle, Int height, Int percent, Color color )
{
	const Int top = middle - height / 2;
	if( right <= left )
		return;
	TheDisplay->drawFillRect( left, top, right - left, height, UIStyle::withAlpha( UIStyle::FRAME, 70 ) );
	const Int filled = ( right - left ) * min( max( percent, 0 ), 100 ) / 100;
	if( filled <= 0 )
		return;
	const Color lit = UIStyle::readable( color );
	if( percent >= 100 )
	{
		TheDisplay->drawFillRect( left, top, filled, height, UIStyle::withAlpha( lit, 150 ) );
		return;
	}
	UIStyle::litLine( left, top, filled, height, lit );
	TheDisplay->drawFillRect( left + filled - 2, top - 2, 2, height + 4, UIStyle::withAlpha( UIStyle::INK, 230 ) );
}

/** A card's and a poster's lines, one block centred on middle: the name (gold for the local player)
	* with his start spot at the right, the army, the general's persona, then a computer's personality or
	* a human's load channel and figure. */
void LoadScreenBoard::drawLines( const BoardCard &card, Int left, Int right, Int middle )
{
	const Int total = scaled( (Real)LoadScreen_cardText( card.persona != NULL, cardLast( card ) ) );
	Int y = middle - total / 2;
	Int pitch = scaled( (Real)LOAD_PITCH_NAME );
	if( card.disc )
		boardDrawDisc( right - scaled( 14 ), y + pitch / 2, scaled( 14 ), card.color, card.disc );
	card.name->draw( left, y + ( pitch - boardStringHeight( card.name ) ) / 2, card.local ? UIStyle::GOLD : UIStyle::INK, BOARD_SHADOW );
	if( card.badge )
	{
		const Int badge = card.name->getFont()->height * 3 / 4;
		const Int bx = left + boardStringWidth( card.name ) + scaled( 10 );
		const Int by = y + ( pitch - badge ) / 2;
		TheDisplay->drawImage( card.badge, bx, by, bx + badge, by + badge );
	}
	y += pitch;
	pitch = scaled( (Real)LOAD_PITCH_LINE );
	card.army->draw( left, y + ( pitch - boardStringHeight( card.army ) ) / 2, UIStyle::INK, BOARD_SHADOW );
	y += pitch;
	if( card.persona )
	{
		card.persona->draw( left, y + ( pitch - boardStringHeight( card.persona ) ) / 2, UIStyle::MUTED, BOARD_SHADOW );
		y += pitch;
	}
	if( card.detail )
		card.detail->draw( left, y + ( pitch - boardStringHeight( card.detail ) ) / 2, UIStyle::MUTED, BOARD_SHADOW );
	else if( card.percent >= 0 )
	{
		pitch = scaled( (Real)LOAD_PITCH_BAR );
		const Int figureRoom = boardStringWidth( m_cardFigure ) + scaled( 4 );
		drawChannel( left, right - figureRoom - scaled( 16 ), y + pitch / 2 + scaled( 2 ), max( 2, scaled( 10 ) ), card.percent, card.color );
		card.figure->draw( right - boardStringWidth( card.figure ), y + ( pitch - boardStringHeight( card.figure ) ) / 2 + scaled( 2 ),
			card.percent >= 100 ? UIStyle::MUTED : UIStyle::INK, BOARD_SHADOW );
	}
}

void LoadScreenBoard::drawRow( const BoardCard &card )
{
	const IRegion2D box = onScreen( card.box );
	UIStyle::menuRow( box, card.local ? UIStyle::ROW_LOCAL : UIStyle::ROW_PLAIN, card.zebra );
	IRegion2D portrait;
	Int textLeft, textRight, textMiddle;
	cardFrame( card, portrait, textLeft, textRight, textMiddle );
	const Bool line = card.kind == LOAD_ROW_LINE;
	// the player's colour down the row's outer edge
	const Int stripeX = card.box.lo.x + ( line ? 6 : 7 );
	const Int stripeInset = line ? 7 : 8;
	TheDisplay->drawFillRect( X( (Real)stripeX ), Y( (Real)( card.box.lo.y + stripeInset ) ), max( 2, scaled( line ? 3.0f : 4.0f ) ),
		max( 1, scaled( (Real)( card.box.hi.y - card.box.lo.y - 2 * stripeInset ) ) ), card.color );
	drawPortrait( card, onScreen( portrait ), line );
	if( !line )
	{
		drawLines( card, X( (Real)textLeft ), X( (Real)textRight ), Y( (Real)textMiddle ) );
		return;
	}

	// a line: name, general, then a computer's personality or the load, the start spot at the end
	const Int middle = ( box.lo.y + box.hi.y ) / 2;
	const Int nameX = X( (Real)textLeft );
	card.name->draw( nameX, middle - boardStringHeight( card.name ) / 2, card.local ? UIStyle::GOLD : UIStyle::INK, BOARD_SHADOW );
	if( card.badge )
	{
		const Int badge = card.name->getFont()->height * 3 / 4;
		const Int bx = nameX + boardStringWidth( card.name ) + scaled( 6 );
		TheDisplay->drawImage( card.badge, bx, middle - badge / 2, bx + badge, middle - badge / 2 + badge );
	}
	card.army->draw( X( (Real)( textLeft + 176 ) ), middle - boardStringHeight( card.army ) / 2, UIStyle::MUTED, BOARD_SHADOW );
	Int right = X( (Real)textRight );
	if( card.disc )
	{
		boardDrawDisc( right - scaled( 11 ), middle, scaled( 11 ), card.color, card.disc );
		right -= scaled( 30 );
	}
	const Int after = X( (Real)( textLeft + 176 + 160 ) );
	if( card.detail )
		card.detail->draw( after, middle - boardStringHeight( card.detail ) / 2, UIStyle::MUTED, BOARD_SHADOW );
	else if( card.percent >= 0 )
	{
		const Int figureRoom = boardStringWidth( m_lineFigure ) + scaled( 2 );
		drawChannel( after, right - figureRoom - scaled( 12 ), middle, max( 2, scaled( 6 ) ), card.percent, card.color );
		card.figure->draw( right - boardStringWidth( card.figure ), middle - boardStringHeight( card.figure ) / 2,
			card.percent >= 100 ? UIStyle::MUTED : UIStyle::INK, BOARD_SHADOW );
	}
}

/** Rows of label and value, the odd ones a faint wash: the rules, the nights' warnings. */
void LoadScreenBoard::drawFacts( const std::vector< BoardFact > &facts, Int top, Int pitch )
{
	const Int left = X( (Real)LOAD_MIDDLE_LEFT );
	const Int right = X( (Real)LOAD_MIDDLE_RIGHT );
	GameFont *capsFont = font( BOARD_CAPS, TRUE );
	for( size_t each = 0; each < facts.size(); each++ )
	{
		const BoardFact &fact = facts[ each ];
		const Int rowTop = Y( (Real)( top + (Int)each * pitch ) );
		const Int rowBottom = Y( (Real)( top + (Int)( each + 1 ) * pitch ) );
		const Int middle = ( rowTop + rowBottom ) / 2;
		if( each % 2 )
			TheDisplay->drawFillRect( left, rowTop, right - left, rowBottom - rowTop, GameMakeColor( 255, 255, 255, 6 ) );
		fact.value->draw( right - scaled( 8 ) - boardStringWidth( fact.value ), middle - boardStringHeight( fact.value ) / 2, UIStyle::INK, BOARD_SHADOW );
		if( fact.capsLabel )
			UIStyle::drawTracked( fact.labelWords, capsFont, left + scaled( 8 ), middle - capsFont->height / 2, fact.labelColor, UIStyle::MENU_LABEL_TRACKING );
		else if( fact.label )
			fact.label->draw( left + scaled( 8 ), middle - boardStringHeight( fact.label ) / 2, fact.labelColor, BOARD_SHADOW );
		if( fact.blood )
			UIStyle::litLine( left, rowTop + scaled( 6 ), max( 2, scaled( 2 ) ), rowBottom - rowTop - 2 * scaled( 6 ), GameMakeColor( 184, 38, 31, 255 ) );
	}
}

/** Apocalypse's own panel: THE NIGHTS, the cycle as one band in the room's four colours with each
	* phase's capitals and wall clock, then the first wave, the gates' warning and the Blood Moon. */
void LoadScreenBoard::drawNights( Int top )
{
	const Int left = LOAD_MIDDLE_LEFT;
	const Int right = LOAD_MIDDLE_RIGHT;
	drawSection( m_nightsHead, X( (Real)left ), X( (Real)right ), Y( (Real)top ) );
	static const UnsignedByte PHASE[ 4 ][ 3 ] = { { 150, 128, 70 }, { 150, 70, 40 }, { 20, 26, 70 }, { 70, 80, 110 } };
	Int widths[ 4 ];
	LoadScreen_nightWidths( right - left, m_nightSeconds, LOAD_NIGHT_LEAST, widths );
	const Int band = top + LOAD_SECTION_HEAD + 6;
	GameFont *capsFont = font( BOARD_CAPS, TRUE );
	Int x = left;
	for( Int each = 0; each < 4; each++ )
	{
		const Color color = GameMakeColor( PHASE[ each ][ 0 ], PHASE[ each ][ 1 ], PHASE[ each ][ 2 ], 255 );
		const IRegion2D segment = onScreen( x, band, x + widths[ each ] - 2, band + 46 );
		TheDisplay->drawFillRect( segment.lo.x, segment.lo.y, segment.hi.x - segment.lo.x, segment.hi.y - segment.lo.y, UIStyle::withAlpha( color, 120 ) );
		TheDisplay->drawFillRect( segment.lo.x, segment.lo.y, segment.hi.x - segment.lo.x, max( 1, scaled( 2 ) ), color );
		UIStyle::drawTracked( m_nightWords[ each ], capsFont, X( (Real)( x + 8 ) ), Y( (Real)( band + 13 ) ) - capsFont->height / 2, UIStyle::INK, UIStyle::MENU_LABEL_TRACKING );
		if( m_nightClocks[ each ] )
			m_nightClocks[ each ]->draw( X( (Real)( x + 8 ) ), Y( (Real)( band + 33 ) ) - boardStringHeight( m_nightClocks[ each ] ) / 2, UIStyle::INK, BOARD_SHADOW );
		x += widths[ each ];
	}
	drawFacts( m_nights, band + 62, 38 );
}

/** The middle: VS, the map with the start spots or its well, the nights, the rules, the watchers. */
void LoadScreenBoard::drawMiddle( void )
{
	const Int left = X( (Real)LOAD_MIDDLE_LEFT );
	const Int right = X( (Real)LOAD_MIDDLE_RIGHT );
	if( m_vs && m_middle.vsTop >= 0 )
	{
		const Int top = Y( (Real)m_middle.vsTop );
		const Int bottom = Y( (Real)( m_middle.vsTop + 50 ) );
		m_vs->draw( ( left + right - boardStringWidth( m_vs ) ) / 2, ( top + bottom - boardStringHeight( m_vs ) ) / 2, UIStyle::INK, BOARD_SHADOW );
	}
	const IRegion2D map = onScreen( m_middle.map );
	const Int mapWidth = map.hi.x - map.lo.x;
	const Int mapHeight = map.hi.y - map.lo.y;
	if( m_wellShown || m_mapImage == NULL )
	{
		TheDisplay->drawFillRect( map.lo.x, map.lo.y, mapWidth, mapHeight, GameMakeColor( 6, 8, 18, 200 ) );
		TheDisplay->drawOpenRect( map.lo.x, map.lo.y, mapWidth, mapHeight, 1.0f, UIStyle::FRAME );
		const Int middle = ( map.lo.y + map.hi.y ) / 2;
		if( m_unknown )
			m_unknown->draw( ( map.lo.x + map.hi.x - boardStringWidth( m_unknown ) ) / 2, middle - scaled( 25 ) - boardStringHeight( m_unknown ) / 2, UIStyle::MUTED, BOARD_SHADOW );
		if( m_wellWords )
			m_wellWords->draw( ( map.lo.x + map.hi.x - boardStringWidth( m_wellWords ) ) / 2, middle + scaled( 39 ) - boardStringHeight( m_wellWords ) / 2, UIStyle::INK, BOARD_SHADOW );
	}
	else
	{
		TheDisplay->drawImage( m_mapImage, map.lo.x, map.lo.y, map.hi.x, map.hi.y );
		TheDisplay->drawOpenRect( map.lo.x - 1, map.lo.y - 1, mapWidth + 2, mapHeight + 2, 1.0f, UIStyle::FRAME );
		for( size_t each = 0; each < m_spotAt.size(); each++ )
		{
			const BoardCard &card = m_cards[ m_spotCard[ each ] ];
			boardDrawDisc( X( (Real)m_spotAt[ each ].x ), Y( (Real)m_spotAt[ each ].y ), scaled( 13 ), card.color, card.disc );
		}
	}
	if( m_middle.nightsTop >= 0 )
		drawNights( m_middle.nightsTop );
	if( m_middle.rulesTop >= 0 )
	{
		drawSection( m_rulesHead, left, right, Y( (Real)m_middle.rulesTop ) );
		drawFacts( m_facts, m_middle.rulesTop + LOAD_SECTION_HEAD, m_middle.rulePitch );
	}
	if( m_middle.observersTop >= 0 && m_observers )
	{
		drawSection( m_observersHead, left, right, Y( (Real)m_middle.observersTop ) );
		const Int top = Y( (Real)( m_middle.observersTop + LOAD_SECTION_HEAD ) );
		m_observers->draw( left + scaled( 8 ), top + ( scaled( 30 ) - boardStringHeight( m_observers ) ) / 2, UIStyle::INK, BOARD_SHADOW );
	}
}

void LoadScreenBoard::drawMatch( void )
{
	for( size_t each = 0; each < m_heads.size(); each++ )
		drawHead( m_heads[ each ] );
	for( size_t each = 0; each < m_cards.size(); each++ )
		drawRow( m_cards[ each ] );
	if( m_middleShown )
		drawMiddle();
}

/** The mission's own picture stays the screen: frame A closes it, glass only under the header, the
	* briefing and the foot; the mission count and its pips at the title row's right. */
void LoadScreenBoard::drawMission( void )
{
	UIStyle::glass( onScreen( UIStyle::MENU_PANEL_LEFT, UIStyle::MENU_PANEL_TOP, UIStyle::MENU_PANEL_RIGHT, LOAD_MISSION_HEADER_BOTTOM ), 168 );
	UIStyle::glass( onScreen( UIStyle::MENU_PANEL_LEFT, LOAD_MISSION_FOOT_TOP, UIStyle::MENU_PANEL_RIGHT, UIStyle::MENU_PANEL_BOTTOM ), 168 );
	UIStyle::frameA( onScreen( UIStyle::MENU_PANEL_LEFT, UIStyle::MENU_PANEL_TOP, UIStyle::MENU_PANEL_RIGHT, UIStyle::MENU_PANEL_BOTTOM ) );

	if( m_missionCount > 0 )
	{
		const Int pip = LOAD_PIP_WIDTH, gap = LOAD_PIP_GAP;
		const Int pipsLeft = LOAD_RIGHT - m_missionCount * pip - ( m_missionCount - 1 ) * gap;
		for( Int each = 0; each < m_missionCount; each++ )
		{
			const IRegion2D box = onScreen( pipsLeft + each * ( pip + gap ), 64, pipsLeft + each * ( pip + gap ) + pip, 76 );
			const Int width = box.hi.x - box.lo.x;
			const Int height = box.hi.y - box.lo.y;
			if( each < m_missionNumber - 1 )
				TheDisplay->drawFillRect( box.lo.x, box.lo.y, width, height, UIStyle::FRAME );
			else if( each == m_missionNumber - 1 )
				UIStyle::litLine( box.lo.x, box.lo.y, width, height, UIStyle::EDGE_LIT );
			else
				TheDisplay->drawOpenRect( box.lo.x, box.lo.y, width, height, 1.0f, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
		}
		if( !m_missionOf.isEmpty() )
		{
			GameFont *capsFont = font( BOARD_CAPS, TRUE );
			const Int width = UIStyle::trackedWidth( m_missionOf, capsFont, UIStyle::MENU_LABEL_TRACKING );
			UIStyle::drawTracked( m_missionOf, capsFont, X( (Real)( pipsLeft - 18 ) ) - width, Y( 70.0f ) - capsFont->height / 2, UIStyle::MUTED, UIStyle::MENU_LABEL_TRACKING );
		}
	}

	if( m_briefing.empty() )
		return;
	const IRegion2D box = onScreen( LOAD_LEFT, LOAD_BRIEFING_TOP, LOAD_BRIEFING_RIGHT, m_briefingBottom );
	UIStyle::glass( box, 168 );
	TheDisplay->drawOpenRect( box.lo.x, box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, 1.0f, UIStyle::FRAME );
	drawSection( m_briefingHead, X( (Real)( LOAD_LEFT + 24 ) ), X( (Real)( LOAD_BRIEFING_RIGHT - 24 ) ), Y( (Real)( LOAD_BRIEFING_TOP + 20 ) ) );
	Int y = Y( (Real)( LOAD_BRIEFING_TOP + 20 + LOAD_SECTION_HEAD + 4 ) );
	for( size_t each = 0; each < m_briefing.size(); each++ )
	{
		DisplayString *words = m_briefing[ each ];
		words->draw( X( (Real)( LOAD_LEFT + 24 ) ), y + ( scaled( 36 ) - min( boardStringHeight( words ), scaled( 36 ) ) ) / 2, UIStyle::INK, BOARD_SHADOW );
		const Int lineHeight = max( 1, words->getFont()->height );
		const Int lines = max( 1, ( boardStringHeight( words ) + lineHeight / 2 ) / lineHeight );
		y += scaled( (Real)LOAD_BRIEFING_PITCH ) * lines;
	}
}

/** A map transfer on the board's rows: PLAYERS and STATUS over two columns, a row a human with his
	* colour as a stripe, the state the network reports and his channel. */
void LoadScreenBoard::drawTransfer( void )
{
	GameFont *capsFont = font( BOARD_CAPS, TRUE );
	const Int count = (Int)m_transfers.size();
	const Int half = ( count + 1 ) / 2;
	for( Int column = 0; column < 2 && ( column == 0 || count > half ); column++ )
	{
		const Int left = column == 0 ? LOAD_LEFT : LOAD_RIGHT - LOAD_TRANSFER_WIDTH;
		drawSection( m_playersHead, X( (Real)left ), X( (Real)( left + LOAD_TRANSFER_WIDTH ) ), Y( (Real)( LOAD_BODY_TOP + 4 ) ) );
		UIStyle::drawTracked( m_statusHead, capsFont, X( (Real)( left + 300 ) ), Y( (Real)( LOAD_BODY_TOP + 4 ) ) + ( scaled( 22 ) - capsFont->height ) / 2,
			UIStyle::ACTIVE, UIStyle::MENU_LABEL_TRACKING );
	}
	for( size_t each = 0; each < m_transfers.size(); each++ )
	{
		const BoardTransfer &row = m_transfers[ each ];
		const IRegion2D box = onScreen( row.box );
		const Int middle = ( box.lo.y + box.hi.y ) / 2;
		UIStyle::menuRow( box, row.local ? UIStyle::ROW_LOCAL : UIStyle::ROW_PLAIN, row.zebra );
		TheDisplay->drawFillRect( X( (Real)( row.box.lo.x + 6 ) ), box.lo.y + scaled( 10 ), max( 2, scaled( 4 ) ), max( 1, box.hi.y - box.lo.y - 2 * scaled( 10 ) ), row.color );
		row.name->draw( X( (Real)( row.box.lo.x + 22 ) ), middle - boardStringHeight( row.name ) / 2, row.local ? UIStyle::GOLD : UIStyle::INK, BOARD_SHADOW );
		const Bool done = row.percent >= 100 || ( row.percent < 0 && row.slot != 0 );
		row.state->draw( X( (Real)( row.box.lo.x + 300 ) ), middle - boardStringHeight( row.state ) / 2, done ? UIStyle::MUTED : UIStyle::INK, BOARD_SHADOW );
		const Int right = X( (Real)( row.box.hi.x - 8 ) );
		const Int figureRoom = boardStringWidth( m_cardFigure ) + scaled( 4 );
		const Int channelLeft = X( (Real)( row.box.lo.x + 480 ) );
		if( row.percent >= 0 )
		{
			drawChannel( channelLeft, right - figureRoom - scaled( 12 ), middle, max( 2, scaled( 10 ) ), row.percent, row.color );
			row.figure->draw( right - boardStringWidth( row.figure ), middle - boardStringHeight( row.figure ) / 2, UIStyle::INK, BOARD_SHADOW );
		}
		else if( done )	// had the map: the channel full and dim
			drawChannel( channelLeft, right - figureRoom - scaled( 12 ), middle, max( 2, scaled( 10 ) ), 100, row.color );
	}
}

/** The Challenge load keeps EA's screen; only its loading bar is the board's, drawn over the bar's
	* holder with the gauge's own progress (GadgetProgressBarSetProgress keeps it as user data). */
static GameWindow *s_challengeGauge = NULL;

static void boardDrawChallengeBar( GameWindow *window, WinInstanceData *instData )
{
	const Int percent = s_challengeGauge ? (Int)(intptr_t)s_challengeGauge->winGetUserData() : 0;
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	IRegion2D bar;
	bar.lo.x = x;
	bar.lo.y = y;
	bar.hi.x = x + width;
	bar.hi.y = y + height;
	UnicodeString words;
	words.format( TheGameText->fetch( "GUI:LoadProgress" ), min( max( percent, 0 ), 100 ) );
	boardDrawLoadBar( bar, percent, UIStyle::capitals( words ), UIStyle::menuFont( BOARD_BODY, TRUE ), BOARD_BAR_TRACKING );
}

/** A campaign's chain from FirstMission through each NextMission, and the current mission's place on
	* it; both 0 off a campaign or off the chain. */
static void boardMissionPlace( Int *number, Int *count )
{
	*number = *count = 0;
	Campaign *campaign = TheCampaignManager ? TheCampaignManager->getCurrentCampaign() : NULL;
	const Mission *current = TheCampaignManager ? TheCampaignManager->getCurrentMission() : NULL;
	if( campaign == NULL || current == NULL )
		return;
	std::vector< const char * > chain;
	for( Mission *mission = campaign->getNextMission( NULL ); mission && chain.size() < 64; mission = campaign->getNextMission( mission ) )
		chain.push_back( mission->m_name.str() );
	if( chain.empty() )
		return;
	*number = LoadScreen_missionPlace( &chain[ 0 ], (Int)chain.size(), current->m_name.str() );
	*count = *number > 0 ? (Int)chain.size() : 0;
}

/** The side's mission picture, as SinglePlayerLoadScreen sets it on its window. */
static const Image *boardMissionPicture( void )
{
	const Campaign *campaign = TheCampaignManager ? TheCampaignManager->getCurrentCampaign() : NULL;
	if( campaign == NULL )
		return NULL;
	const AsciiString side = campaign->getSide();
	if( side.compareNoCase( "USA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "MissionLoad_USA" );
	if( side.compareNoCase( "GLA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "MissionLoad_GLA" );
	if( side.compareNoCase( "China" ) == 0 )
		return TheMappedImageCollection->findImageByName( "MissionLoad_China" );
	return NULL;
}

/** A mission's header words: its place as the title (its number where the mission names none), the
	* campaign and the difficulty after it; extra joins the subtitle (a saved game's words and date). */
static void boardMissionWords( UnicodeString &title, UnicodeString &subtitle, const UnicodeString &extra )
{
	static const char *const difficultyLabels[ DIFFICULTY_COUNT ] = { "GUI:Easy", "GUI:Medium", "GUI:Hard" };
	const Mission *mission = TheCampaignManager->getCurrentMission();
	UnicodeString number;
	number.format( TheGameText->fetch( "GUI:LoadMission" ), TheCampaignManager->getCurrentMissionNumber() + 1 );
	title = number;
	subtitle = TheGameText->fetch( TheCampaignManager->getCurrentCampaign()->m_campaignNameLabel );
	if( mission && !mission->m_locationNameLabel.isEmpty() )
		title = TheGameText->fetch( mission->m_locationNameLabel );
	if( !extra.isEmpty() )
	{
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( extra );
	}
	subtitle.concat( UIStyle::HEADER_JOINER );
	subtitle.concat( TheGameText->fetch( difficultyLabels[ TheCampaignManager->getGameDifficulty() ] ) );
}


// SinglePlayerLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
void CampaignBriefingVoice::update(AudioManager &audio, const AudioEventRTS &voice,
	Int movieFrame, Bool skipMovie)
{
	// Frame 255 is EA's authored external-voice cue. Zero Hour missions name no voice.
	if (m_attempted || voice.getEventName().isEmpty()
		|| voice.getEventName().compareNoCase("NoSound") == 0 || (!skipMovie && movieFrame < 255))
		return;
	m_attempted = TRUE;
	if (audio.isOn(AudioAffect_Speech) && audio.getVolume(AudioAffect_Speech) > 0.0f)
		audio.friend_forcePlayAudioEventRTS(&voice);
}

void SinglePlayerLoadScreen::startBriefingAudio(const Mission &mission, Int movieFrame, Bool skipMovie)
{
	if (TheGameEngine->getQuitting() || !TheGameEngine->isActive())
		m_briefingVoice.cancel();
	else
		m_briefingVoice.update(*TheAudio, mission.m_briefingVoice, movieFrame, skipMovie);
}

SinglePlayerLoadScreen::SinglePlayerLoadScreen( void )
{
	//Added By Sadullah Nader
	//Initialization(s) inserted
	m_currentObjectiveLine = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = NULL;
	m_currentObjectiveWidthOffset = 0;
	//
	m_progressBar = NULL;
	m_percent = NULL;
	m_videoStream = NULL;
	m_videoBuffer = NULL;
	m_objectiveWin = NULL;
	for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		m_objectiveLines[i] = NULL;
	m_board = NULL;

}

SinglePlayerLoadScreen::~SinglePlayerLoadScreen( void )
{
	// the board's window is a child of the load screen's, gone with it in ~LoadScreen
	delete m_board;
	m_progressBar = NULL;
	m_percent = NULL;
	m_objectiveWin = NULL;
	for(Int i = 0; i < MAX_OBJECTIVE_LINES; ++i)
		m_objectiveLines[i] = NULL;
	if(m_videoBuffer)
		delete m_videoBuffer;
	m_videoBuffer = NULL;

	if ( m_videoStream )
		m_videoStream->close();
	m_videoStream = NULL;
	
	TheAudio->removeAudioEvent( m_ambientLoopHandle );
	m_ambientLoopHandle = NULL;
	
}

void SinglePlayerLoadScreen::moveWindows( Int frame )
{
	enum{
		STATE_BEGIN = 250,
		STATE_SHOW_LOCATION = 251,
		STATE_BEGIN_BREIFING = 255,
//		STATE_BEGIN_ANIMATING_TEXT = 250,
		STATE_SHOW_CAMEO_1 = 434,
		STATE_BEGIN_ANIMATING_TEXT = 356,
		STATE_HIDE_CAMEO_1 = 459,
		STATE_SHOW_CAMEO_2 = 464,
		STATE_HIDE_CAMEO_2 = 492,
		STATE_SHOW_CAMEO_3 = 497,
		STATE_HIDE_CAMEO_3 = 524,
//		STATE_END_ANIM_HEAD = 450,
		STATE_END_ANIMATING_TEXT = 730,
		STATE_END = 730
	};
	if(frame < STATE_BEGIN || frame > STATE_END)
		return;
	
	if( frame == STATE_BEGIN_BREIFING)
	{
		// add sound support here
		TheAudio->friend_forcePlayAudioEventRTS(&TheCampaignManager->getCurrentMission()->m_briefingVoice);
	}

	if( frame == STATE_BEGIN_ANIMATING_TEXT)
	{
		m_objectiveWin->winHide(FALSE);
		// animate the text and stuff
	}
	
	if( frame > STATE_BEGIN_ANIMATING_TEXT && frame <= STATE_END_ANIMATING_TEXT && !m_finishedObjectiveText)
	{
		if(m_currentObjectiveLineCharacter >= m_unicodeObjectiveLines[m_currentObjectiveLine].getLength() )
		{
			m_currentObjectiveLine++;
			m_currentObjectiveLineCharacter =0;
		}
		if(m_currentObjectiveLine >= MAX_OBJECTIVE_LINES || m_unicodeObjectiveLines[m_currentObjectiveLine].isEmpty())
		{
			m_finishedObjectiveText = TRUE;
		}
		else
		{
			WideChar wChar = m_unicodeObjectiveLines[m_currentObjectiveLine].getCharAt(m_currentObjectiveLineCharacter);
			UnicodeString text = GadgetStaticTextGetText(m_objectiveLines[m_currentObjectiveLine]);
			text.concat(wChar);
			GadgetStaticTextSetText(m_objectiveLines[m_currentObjectiveLine], text);

		}
		m_currentObjectiveLineCharacter++;
	}
	switch (frame) {
	
	case STATE_SHOW_LOCATION:
		m_location->winHide(FALSE);
		break;
	case STATE_SHOW_CAMEO_1:
		m_unitDesc[0]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_1:
		m_unitDesc[0]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_2:
		m_unitDesc[1]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_2:
		m_unitDesc[1]->winHide(TRUE);
		break;
	case STATE_SHOW_CAMEO_3:
		m_unitDesc[2]->winHide(FALSE);
		break;
	case STATE_HIDE_CAMEO_3:
		m_unitDesc[2]->winHide(TRUE);
		break;
	}

}
/*
	static Bool on = FALSE;
	static ICoord2D startPos, endPos;
	enum{
		STATE_BEGIN = 275,
		STATE_BEGIN_ANIM = 290,
		STATE_ANIM_CAMEO1 = 300,
		STATE_ANIM_CAMEO1_TRASITION_CAMEO2 = 350,
		STATE_ANIM_CAMEO2 = 400,
		STATE_ANIM_CAMEO2_TRASITION_CAMEO3 = 450,
		STATE_ANIM_CAMEO3 = 500,
		STATED_END_ANIM = 550,
		STATE_END = 800
	};
	if(frame < STATE_BEGIN)
		return;
	else if(frame == STATE_BEGIN )
	{
		m_cameoWindow1->winHide(FALSE);
		m_cameoWindow2->winHide(FALSE);
		m_cameoWindow3->winHide(FALSE);
		m_cameoFrame->winHide(FALSE);
	}
	else if( frame == STATE_ANIM_CAMEO1)
	{
		m_cameoWindow1->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[0]));
		//save of positions
	}
	else if( frame == STATE_ANIM_CAMEO1_TRASITION_CAMEO2)
	{
		m_cameoWindow1->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow1->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow2->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;
	
	}
	else if( frame > STATE_ANIM_CAMEO1_TRASITION_CAMEO2 && frame < STATE_ANIM_CAMEO2)
	{
		
		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO1_TRASITION_CAMEO2)) / (STATE_ANIM_CAMEO2 - STATE_ANIM_CAMEO1_TRASITION_CAMEO2);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO2 )
	{
		m_cameoWindow2->winEnable(TRUE);
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[1]));
	}
	else if( frame == STATE_ANIM_CAMEO2_TRASITION_CAMEO3)
	{
		m_cameoWindow2->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		ICoord2D tempPos;
		Int xOffset;
		m_cameoFrame->winGetPosition(&startPos.x, &startPos.y);
		m_cameoWindow2->winGetPosition(&tempPos.x, &tempPos.y);
		xOffset = tempPos.x - startPos.x;
		m_cameoWindow3->winGetPosition(&endPos.x, &endPos.y);
		endPos.x = endPos.x - xOffset;
		endPos.y = startPos.y;
	
	}
	else if( frame > STATE_ANIM_CAMEO2_TRASITION_CAMEO3 && frame < STATE_ANIM_CAMEO3)
	{
		
		//extrapolate between start and end pos
		Real percent = INT_TO_REAL((frame - STATE_ANIM_CAMEO2_TRASITION_CAMEO3)) / (STATE_ANIM_CAMEO3 - STATE_ANIM_CAMEO2_TRASITION_CAMEO3);
		m_cameoFrame->winSetPosition(startPos.x + (endPos.x - startPos.x) * percent, endPos.y);
	}
	else if( frame == STATE_ANIM_CAMEO3 )
	{
		m_cameoFrame->winSetPosition(endPos.x, endPos.y);
		m_cameoWindow3->winEnable(TRUE);
		GadgetStaticTextSetText(m_cameoText, TheGameText->fetch(TheCampaignManager->getCurrentMission()->m_cameoImageName[2]));
	}
	else if( frame ==STATED_END_ANIM)
	{
		m_cameoWindow3->winEnable(FALSE);
		GadgetStaticTextSetText(m_cameoText, UnicodeString::TheEmptyString);
		m_cameoFrame->winHide(TRUE);
		
	}
}*/
 
void SinglePlayerLoadScreen::init( GameInfo *game )
{
	m_briefingVoice.reset();
	//No music in SinglePlayerLoadScreen

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/SinglePlayerLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();
//	Mission *mission = TheCampaignManager->getCurrentMission();
	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );	

	m_percent = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:Percent" ) ));
	DEBUG_ASSERTCRASH(m_percent, ("Can't initialize the m_percent for the single player loadscreen"));
	GadgetStaticTextSetText(m_percent,UnicodeString(u"0%"));
	m_percent->winHide(TRUE);

	m_objectiveWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ObjectivesWin" ) ));
	DEBUG_ASSERTCRASH(m_objectiveWin, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_objectiveWin->winHide(TRUE);

	
	Mission *mission = TheCampaignManager->getCurrentMission();
	AsciiString lineName;
	// i is used after the loop; VC6 for-scope let it escape.
	Int i;
	for(i = 0; i < MAX_OBJECTIVE_LINES; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextLine%d",i);
		m_objectiveLines[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_objectiveLines[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_objectiveLines[i],UnicodeString::TheEmptyString);

		// translate the objective lines
		if(mission->m_missionObjectivesLabel[i].isNotEmpty())
			m_unicodeObjectiveLines[i] = TheGameText->fetch(mission->m_missionObjectivesLabel[i]);
	}

	for(i = 0; i < MAX_DISPLAYED_UNITS; ++i)
	{
		lineName.format("SinglePlayerLoadScreen.wnd:StaticTextCameoText%d",i);
		m_unitDesc[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( lineName ));
		DEBUG_ASSERTCRASH(m_unitDesc[i], ("Can't initialize the m_objectiveLines[%d] for the single player loadscreen", i));
		GadgetStaticTextSetText(m_unitDesc[i],TheGameText->fetch(mission->m_unitNames[i]));
		m_unitDesc[i]->winHide(TRUE);
	}
	m_location = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:StaticTextCameoText3" ) ));
	DEBUG_ASSERTCRASH(m_location, ("Can't initialize the m_objectiveWin for the single player loadscreen"));
	m_location->winHide(TRUE);
	GadgetStaticTextSetText(m_location, TheGameText->fetch(mission->m_locationNameLabel));
	

	
	m_currentObjectiveLine = 0;
	m_currentObjectiveWidthOffset = 0;
	m_currentObjectiveLineCharacter = 0;
	m_finishedObjectiveText = FALSE;
/*
	m_cameoWindow1 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo1" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow1, ("Can't initialize the m_cameoWindow1 for the single player loadscreen"));
	m_cameoWindow1->winHide(TRUE);
	m_cameoWindow1->winEnable(FALSE);
	m_cameoWindow1->winSetEnabledImage(0, mission->m_cameoImage[0]);
	m_cameoWindow1->winSetDisabledImage(0, mission->m_cameoDisabledImage[0]);

	m_cameoWindow2 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo2" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow2, ("Can't initialize the m_cameoWindow2 for the single player loadscreen"));
	m_cameoWindow2->winHide(TRUE);
	m_cameoWindow2->winEnable(FALSE);
	m_cameoWindow2->winSetEnabledImage(0, mission->m_cameoImage[1]);
	m_cameoWindow2->winSetDisabledImage(0, mission->m_cameoDisabledImage[1]);

	m_cameoWindow3 = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowCameo3" ) ));
	DEBUG_ASSERTCRASH(m_cameoWindow3, ("Can't initialize the m_cameoWindow3 for the single player loadscreen"));
	m_cameoWindow3->winHide(TRUE);
	m_cameoWindow3->winEnable(FALSE);
	m_cameoWindow3->winSetEnabledImage(0, mission->m_cameoImage[2]);
	m_cameoWindow3->winSetDisabledImage(0, mission->m_cameoDisabledImage[2]);

	m_headMovie = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowHead" ) ));
	DEBUG_ASSERTCRASH(m_headMovie, ("Can't initialize the m_headMovie for the single player loadscreen"));
	m_headMovie->winHide(TRUE);
	m_cameoFrame = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:WindowHiliteCameo" ) ));
	DEBUG_ASSERTCRASH(m_cameoFrame, ("Can't initialize the m_cameoFrame for the single player loadscreen"));
	m_cameoFrame->winHide(TRUE);
	m_cameoText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:StaticTextCameoText" ) ));
	DEBUG_ASSERTCRASH(m_cameoText, ("Can't initialize the m_cameoText for the single player loadscreen"));

*/
	m_ambientLoop.setEventName("LoadScreenAmbient");

	// format the progress bar: USA to blue, GLA to green, China to red
	// and set the background image.  Before the movie, which may not open: the original Generals
	// campaigns' movies come from a Generals install, and without one the side's picture was lost too.
	AsciiString campaignName = TheCampaignManager->getCurrentCampaign()->getSide();
	GameWindow *backgroundWin = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "SinglePlayerLoadScreen.wnd:ParentSinglePlayerLoadScreen" ) ));
	if (campaignName.compareNoCase("USA") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_USA") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter2") );
	}
	else if (campaignName.compareNoCase("GLA") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_GLA") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter3") );
	}
	else if (campaignName.compareNoCase("China") == 0)
	{
		backgroundWin->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName("MissionLoad_China") );
		m_progressBar->winSetEnabledImage( 6, TheMappedImageCollection->findImageByName("LoadingBar_ProgressCenter1") );
	}
	// else leave the default background screen

	// create the new stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );
	if ( m_videoStream == NULL )
	{
		startBriefingAudio(*mission, 0, TRUE);
		m_percent->winHide(TRUE);
		return;
	}

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (	m_videoBuffer == NULL || 
				!m_videoBuffer->allocate(	m_videoStream->width(), 
													m_videoStream->height())
		)
	{
		delete m_videoBuffer;
		m_videoBuffer = NULL;

		if ( m_videoStream )
			m_videoStream->close();
		m_videoStream = NULL;
		startBriefingAudio(*mission, 0, TRUE);

		return;
	}


	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
		{
			TheGameEngine->serviceWindowsOS();

			if(!m_videoStream->isFrameReady())
			{
				sleepMilliseconds( 1 );	
				continue;
			}

			if (!TheGameEngine->isActive())
			{/*	//we are alt-tabbed out, so just increment the frame
				m_videoStream->frameNext();
				m_videoStream->frameDecompress();*/

				//Changing for MissionDisk, just skip to end.
				break;
			}
			
			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);

// PULLED FROM THE MISSION DISK
//			moveWindows( m_videoStream->frameIndex());
			startBriefingAudio(*mission, m_videoStream->frameIndex());

			m_videoStream->frameNext();

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);
			if(m_videoStream->frameIndex() % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(u"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
				GadgetStaticTextSetText(m_percent, per);
			
			}
			TheWindowManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();
		}

		// A short/failed movie may finish before its cue. The once guard prevents a replay.
		startBriefingAudio(*mission, 0, TRUE);
		// let the background image show through
		m_videoStream->close();
		m_videoStream = NULL;
		m_loadScreen->winGetInstanceData()->setVideoBuffer( NULL );
		TheDisplay->draw();
	}
	else
	{
		// if we're min spec'ed don't play a movie
		startBriefingAudio(*mission, 0, TRUE);
		
		Int delay = mission->m_voiceLength * 1000;
		Int begin = Clock_Milliseconds();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{		
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

			TheWindowManager->update();
			TheDisplay->draw();
			sleepMilliseconds( 100 );
			currTime = Clock_Milliseconds();
		}
		

		TheWindowManager->update();
		TheDisplay->draw();

	}
	setFPMode();
	m_percent->winHide(TRUE);
	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);
	
}

void SinglePlayerLoadScreen::reset( void )
{
 m_briefingVoice.cancel();
 setLoadScreen(NULL);
 m_progressBar = NULL;
}

void SinglePlayerLoadScreen::update( Int percent )
{
	// the briefing movie has the whole screen; the board comes with the load after it
	if( m_board == NULL )
	{
		// Zero Hour's missions name no place, so the number is the title there; the mission's place in
		// its campaign is the count and pips at the title row's end
		UnicodeString title, subtitle;
		boardMissionWords( title, subtitle, UnicodeString::TheEmptyString );
		Int number, count;
		boardMissionPlace( &number, &count );
		m_board = NEW LoadScreenBoard;
		m_board->createMission( m_loadScreen, title, subtitle, m_unicodeObjectiveLines, MAX_OBJECTIVE_LINES, number, count, NULL );
		m_progressBar->winHide( TRUE );
		m_percent->winHide( TRUE );
	}
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	m_board->setLocalProgress( percent );

	// Do this last!
	LoadScreen::update( percent );
}

void SinglePlayerLoadScreen::setProgressRange( Int min, Int max )
{

}

// ChallengeLoadScreen Class ///////////////////////////////////////////////
//-----------------------------------------------------------------------------
ChallengeLoadScreen::ChallengeLoadScreen( void )
{
	m_progressBar = NULL;
	m_videoStream = NULL;
	m_videoBuffer = NULL;

	m_bioNameLeft = NULL;
	m_bioAgeLeft = NULL;
	m_bioBirthplaceLeft = NULL;
	m_bioStrategyLeft = NULL;
	m_bioBigNameEntryLeft = NULL;
	m_bioNameEntryLeft = NULL;
	m_bioAgeEntryLeft = NULL;
	m_bioBirthplaceEntryLeft = NULL;
	m_bioStrategyEntryLeft = NULL;
	m_bioBigNameEntryRight = NULL;
	m_bioNameRight = NULL;
	m_bioAgeRight = NULL;
	m_bioBirthplaceRight = NULL;
	m_bioStrategyRight = NULL;
	m_bioNameEntryRight = NULL;
	m_bioAgeEntryRight = NULL;
	m_bioBirthplaceEntryRight = NULL;
	m_bioStrategyEntryRight = NULL;

	m_portraitLeft = NULL;
	m_portraitRight = NULL;
	m_portraitMovieLeft = NULL;
	m_portraitMovieRight = NULL;

//	m_overlayReticleCrosshairs = NULL;
//	m_overlayReticleCircleLineOuter = NULL;
//	m_overlayReticleCircleLineInner = NULL;
	m_overlayReticleCircleAlphaOuter = NULL;
	m_overlayReticleCircleAlphaInner = NULL;
	m_overlayVsBackdrop = NULL;
	m_overlayVs = NULL;
	m_wndVideoManager = NULL;
}
	
ChallengeLoadScreen::~ChallengeLoadScreen( void )
{
	m_progressBar = NULL;
	s_challengeGauge = NULL;
	if(m_videoBuffer)
		delete m_videoBuffer;
	m_videoBuffer = NULL;
	if ( m_videoStream )
		m_videoStream->close();
	m_videoStream = NULL;
	
	m_bioNameLeft = NULL;
	m_bioAgeLeft = NULL;
	m_bioBirthplaceLeft = NULL;
	m_bioStrategyLeft = NULL;
	m_bioBigNameEntryLeft = NULL;
	m_bioNameEntryLeft = NULL;
	m_bioAgeEntryLeft = NULL;
	m_bioBirthplaceEntryLeft = NULL;
	m_bioStrategyEntryLeft = NULL;
	m_bioBigNameEntryRight = NULL;
	m_bioNameRight = NULL;
	m_bioAgeRight = NULL;
	m_bioBirthplaceRight = NULL;
	m_bioStrategyRight = NULL;
	m_bioNameEntryRight = NULL;
	m_bioAgeEntryRight = NULL;
	m_bioBirthplaceEntryRight = NULL;
	m_bioStrategyEntryRight = NULL;

	m_portraitLeft = NULL;
	m_portraitRight = NULL;
	m_portraitMovieLeft = NULL;
	m_portraitMovieRight = NULL;

//	m_overlayReticleCrosshairs = NULL;
//	m_overlayReticleCircleLineOuter = NULL;
//	m_overlayReticleCircleLineInner = NULL;
	m_overlayReticleCircleAlphaOuter = NULL;
	m_overlayReticleCircleAlphaInner = NULL;
	m_overlayVsBackdrop = NULL;
	m_overlayVs = NULL;

	if(m_wndVideoManager)
		delete m_wndVideoManager;
	m_wndVideoManager = NULL;

	TheAudio->removeAudioEvent( m_ambientLoopHandle );
	m_ambientLoopHandle = NULL;
}

// accepts the number of chars to advance, the window we're concerned with, the total text for final display, and the current position of the readout
// returns the updated position of the readout
Int updateTeletypeText( Int num_chars, GameWindow* window, UnicodeString full_text, Int current_text_pos )
{
	DEBUG_ASSERTCRASH(window, ("No window for teletype text update"));
	UnicodeString currentText = GadgetStaticTextGetText(window);
	WideChar wChar;
	for (Int i = 0; i < num_chars; i++)
	{
		if (current_text_pos < full_text.getLength())
		{
			wChar = full_text.getCharAt(current_text_pos);
			currentText.concat(wChar);
			current_text_pos++;
		}
	}	
	GadgetStaticTextSetText(window, currentText);
	return current_text_pos;
}

void ChallengeLoadScreen::activatePieces( Int frame, const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent )
{
	static Int textPosBigNameRight = 0;
	static Int textPosNameRight = 0;
	static Int textPosAgeRight = 0;
	static Int textPosBirthplaceRight = 0;
	static Int textPosStrategyRight = 0;
	static Int textPosBigNameLeft = 0;
	static Int textPosNameLeft = 0;
	static Int textPosAgeLeft = 0;
	static Int textPosBirthplaceLeft = 0;
	static Int textPosStrategyLeft = 0;

	AudioEventRTS eventLeftGeneral( generalPlayer->getNameSound() );
	AudioEventRTS eventVS("Taunts_GCAnnouncer12");
	AudioEventRTS eventRightGeneral( generalOpponent ? generalOpponent->getNameSound() : AsciiString::TheEmptyString );

	switch (frame)
	{
		case FRAME_TITLES_START:
			m_bioNameLeft->winHide(FALSE);
//			m_bioAgeLeft->winHide(FALSE);
			m_bioBirthplaceLeft->winHide(FALSE);
			m_bioStrategyLeft->winHide(FALSE);
			m_bioNameRight->winHide(FALSE);
//			m_bioAgeRight->winHide(FALSE);
			m_bioBirthplaceRight->winHide(FALSE);
			m_bioStrategyRight->winHide(FALSE);

			break;
		case FRAME_TELETYPE_START:
			// reinit the statics for each new load screen
			textPosBigNameRight = 0;
			textPosNameRight = 0;
			textPosAgeRight = 0;
			textPosBirthplaceRight = 0;
			textPosStrategyRight = 0;
			textPosBigNameLeft = 0;
			textPosNameLeft = 0;
			textPosAgeLeft = 0;
			textPosBirthplaceLeft = 0;
			textPosStrategyLeft = 0;

			m_bioBigNameEntryLeft->winHide(FALSE);
			m_bioNameEntryLeft->winHide(FALSE);
//			m_bioAgeEntryLeft->winHide(FALSE);
			m_bioBirthplaceEntryLeft->winHide(FALSE);
			m_bioStrategyEntryLeft->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryLeft, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryLeft, UnicodeString::TheEmptyString );

			m_bioBigNameEntryRight->winHide(FALSE);
			m_bioNameEntryRight->winHide(FALSE);
//			m_bioAgeEntryRight->winHide(FALSE);
			m_bioBirthplaceEntryRight->winHide(FALSE);
			m_bioStrategyEntryRight->winHide(FALSE);
			GadgetStaticTextSetText( m_bioBigNameEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioNameEntryRight, UnicodeString::TheEmptyString );
//			GadgetStaticTextSetText( m_bioAgeEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioBirthplaceEntryRight, UnicodeString::TheEmptyString );
			GadgetStaticTextSetText( m_bioStrategyEntryRight, UnicodeString::TheEmptyString );
			break;
		case FRAME_PORTRAITS_START:
			
			m_wndVideoManager->playMovie( m_portraitMovieLeft, generalPlayer->getPortraitMovieLeftName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
			m_portraitMovieLeft->winHide(FALSE);
			if (generalOpponent)
			{
				m_wndVideoManager->playMovie( m_portraitMovieRight, generalOpponent->getPortraitMovieRightName(), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
				m_portraitMovieRight->winHide(FALSE);
			}

			TheAudio->addAudioEvent( &eventLeftGeneral );
			
			break;
		case FRAME_OUTER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_SHOW:
//			m_overlayReticleCircleLineInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaOuter->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_ALPHA_SHOW:
			m_overlayReticleCircleAlphaInner->winHide(FALSE);
			break;
		case FRAME_OUTER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineOuter->winHide(TRUE);
			break;
		case FRAME_INNER_BACKDROP_ALPHA_SHOW:
			m_overlayVsBackdrop->winHide(FALSE);
			break;
		case FRAME_INNER_CIRCLE_LINE_HIDE:
//			m_overlayReticleCircleLineInner->winHide(TRUE);
			break;
		case FRAME_VS_ANIM_START:
			// it's time to start the overlay movie
//					m_overlayVsBackdrop->winSetEnabledImage( 0, TheMappedImageCollection->findImageByFilename("))
			m_overlayVsBackdrop->winHide(FALSE);
			m_overlayVs->winHide(FALSE);
			m_wndVideoManager->playMovie( m_overlayVs, AsciiString("VSSmall"), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);

			// "Verses"
			TheAudio->addAudioEvent( &eventVS );

			break;
		case FRAME_RIGHT_VOICE:
			TheAudio->addAudioEvent( &eventRightGeneral );

			break;
	}

	// update the teletype readout
	if (frame > FRAME_TELETYPE_START && (frame % TELETYPE_UPDATE_FREQ) == 0)
	{
		textPosNameLeft = updateTeletypeText( 1, m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosNameLeft);
		textPosBigNameLeft = updateTeletypeText( 1, m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()), textPosBigNameLeft);
//		textPosAgeLeft = updateTeletypeText( 1, m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()), textPosAgeLeft);
		textPosBirthplaceLeft = updateTeletypeText( 1, m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()), textPosBirthplaceLeft);
		textPosStrategyLeft = updateTeletypeText( 1, m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()), textPosStrategyLeft);
		
		if (generalOpponent)
		{
			textPosNameRight = updateTeletypeText( 1, m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosNameRight);
			textPosBigNameRight = updateTeletypeText( 1, m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()), textPosBigNameRight);
//			textPosAgeRight = updateTeletypeText( 1, m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()), textPosAgeRight);
			textPosBirthplaceRight = updateTeletypeText( 1, m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()), textPosBirthplaceRight);
			textPosStrategyRight = updateTeletypeText( 1, m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()), textPosStrategyRight);
		}
	}
}
 
void ChallengeLoadScreen::activatePiecesMinSpec(const GeneralPersona *generalPlayer, const GeneralPersona *generalOpponent)
{
	m_bioNameLeft->winHide(FALSE);
//	m_bioAgeLeft->winHide(FALSE);
	m_bioBirthplaceLeft->winHide(FALSE);
	m_bioStrategyLeft->winHide(FALSE);
	m_bioNameRight->winHide(FALSE);
//	m_bioAgeRight->winHide(FALSE);
	m_bioBirthplaceRight->winHide(FALSE);
	m_bioStrategyRight->winHide(FALSE);
	m_bioBigNameEntryLeft->winHide(FALSE);
	m_bioNameEntryLeft->winHide(FALSE);
//	m_bioAgeEntryLeft->winHide(FALSE);
	m_bioBirthplaceEntryLeft->winHide(FALSE);
	m_bioStrategyEntryLeft->winHide(FALSE);
	GadgetStaticTextSetText( m_bioBigNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
	GadgetStaticTextSetText( m_bioNameEntryLeft, TheGameText->fetch(generalPlayer->getBioName()) );
//	GadgetStaticTextSetText( m_bioAgeEntryLeft, TheGameText->fetch(generalPlayer->getBioDOB()) );
	GadgetStaticTextSetText( m_bioBirthplaceEntryLeft, TheGameText->fetch(generalPlayer->getBioRank()) );
	GadgetStaticTextSetText( m_bioStrategyEntryLeft, TheGameText->fetch(generalPlayer->getBioStrategy()) );
	m_bioBigNameEntryRight->winHide(FALSE);
	m_bioNameEntryRight->winHide(FALSE);
//	m_bioAgeEntryRight->winHide(FALSE);
	m_bioBirthplaceEntryRight->winHide(FALSE);
	m_bioStrategyEntryRight->winHide(FALSE);
	m_portraitLeft->winSetEnabledImage(0, generalPlayer->getBioPortraitLarge() );
	m_portraitLeft->winHide(FALSE);
	if (generalOpponent)
	{
		GadgetStaticTextSetText( m_bioBigNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
		GadgetStaticTextSetText( m_bioNameEntryRight, TheGameText->fetch(generalOpponent->getBioName()) );
//		GadgetStaticTextSetText( m_bioAgeEntryRight, TheGameText->fetch(generalOpponent->getBioDOB()) );
		GadgetStaticTextSetText( m_bioBirthplaceEntryRight, TheGameText->fetch(generalOpponent->getBioRank()) );
		GadgetStaticTextSetText( m_bioStrategyEntryRight, TheGameText->fetch(generalOpponent->getBioStrategy()) );
		m_portraitRight->winSetEnabledImage(0, generalOpponent->getBioPortraitLarge() );
		m_portraitRight->winHide(FALSE);
	}
	m_overlayReticleCircleAlphaOuter->winHide(FALSE);
	m_overlayReticleCircleAlphaInner->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVsBackdrop->winHide(FALSE);
	m_overlayVs->winHide(FALSE);
	m_wndVideoManager->playMovie( m_overlayVs, AsciiString("VSSmall"), WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME);
}


void ChallengeLoadScreen::init( GameInfo *game )
{
	const Campaign *campaign = TheCampaignManager->getCurrentCampaign();
	const Mission *mission = TheCampaignManager->getCurrentMission();

	// the player general is tied to the campaign
	const GeneralPersona* generalPlayer = TheChallengeGenerals->getPlayerGeneralByCampaignName( campaign->m_name );

	// the opponent general is tied to the mission
	DEBUG_ASSERTCRASH(mission->m_generalName.isNotEmpty(), ("No GeneralName associated with this mission, check Campaign.ini"));
	const GeneralPersona* generalOpponent = TheChallengeGenerals->getGeneralByGeneralName( mission->m_generalName );
	// A mission can name a general the challenge data does not have.  The challenge menu leaves such a
	// general out of its list, but a saved game does not pass through that menu.  The screen then keeps its
	// right side empty: no portrait, no bio, no voice.
	if (generalOpponent == NULL)
		DEBUG_LOG(("ChallengeLoadScreen: the data has no general '%s' for this mission\n", mission->m_generalName.str()));

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/ChallengeLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the single player loadscreen"));
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ChallengeLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );

	// EA's screen stays; its loading bar is the board's, drawn over the bar's holder (ParentLoadingBar)
	// with everything EA put in it hidden, the gauge still taking the progress
	if( m_progressBar )
	{
		GameWindow *holder = TheWindowManager->winGetWindowFromId( m_loadScreen, TheNameKeyGenerator->nameToKey( AsciiString( "ChallengeLoadScreen.wnd:ParentLoadingBar" ) ) );
		if( holder == NULL )
			holder = m_progressBar;
		for( GameWindow *child = holder->winGetChild(); child; child = child->winGetNext() )
			child->winHide( TRUE );
		s_challengeGauge = m_progressBar;
		holder->winSetDrawFunc( boardDrawChallengeBar );
	}

	m_ambientLoop.setEventName("LoadScreenAmbient");

	// create the new background video stream
	m_videoStream = TheVideoPlayer->open( TheCampaignManager->getCurrentMission()->m_movieLabel );
	// as SinglePlayerLoadScreen::init does: a movie that does not open (none headless, or a missing file) has
	// no stream, and the buffer below read its size through NULL
	if ( m_videoStream == NULL )
		return;

	// Create the new buffer
	m_videoBuffer = TheDisplay->createVideoBuffer();
	if (m_videoBuffer == NULL || !m_videoBuffer->allocate(	m_videoStream->width(), m_videoStream->height() ))
	{
		delete m_videoBuffer;
		m_videoBuffer = NULL;

		if ( m_videoStream )
			m_videoStream->close();
		m_videoStream = NULL;

		return;
	}

	// init overlays
	NameKeyType namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitLeft"));
	m_portraitLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitRight"));
	m_portraitRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitMovieLeft"));
	m_portraitMovieLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:PortraitMovieRight"));
	m_portraitMovieRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:ReticleCrosshairs"));
//	m_overlayReticleCrosshairs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
/*
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:OuterCircleLine"));
	m_overlayReticleCircleLineOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:InnerCircleLine"));
	m_overlayReticleCircleLineInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
*/
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:CircleAlphaOuter"));
	m_overlayReticleCircleAlphaOuter = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:CircleAlphaInner"));
	m_overlayReticleCircleAlphaInner = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:VersusBackdrop"));
	m_overlayVsBackdrop = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:OverlayVs"));
	m_overlayVs = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );

	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameLeft"));
	m_bioNameLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBLeft"));
//	m_bioAgeLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceLeft"));
	m_bioBirthplaceLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyLeft"));
	m_bioStrategyLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BigNameEntryLeft"));
	m_bioBigNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameEntryLeft"));
	m_bioNameEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBEntryLeft"));
//	m_bioAgeEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceEntryLeft"));
	m_bioBirthplaceEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyEntryLeft"));
	m_bioStrategyEntryLeft = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameRight"));
	m_bioNameRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBRight"));
//	m_bioAgeRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceRight"));
	m_bioBirthplaceRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyRight"));
	m_bioStrategyRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BigNameEntryRight"));
	m_bioBigNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioNameEntryRight"));
	m_bioNameEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
//	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioDOBEntryRight"));
//	m_bioAgeEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioBirthplaceEntryRight"));
	m_bioBirthplaceEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );
	namekey = TheNameKeyGenerator->nameToKey( AsciiString("ChallengeLoadScreen.wnd:BioStrategyEntryRight"));
	m_bioStrategyEntryRight = TheWindowManager->winGetWindowFromId( m_loadScreen, namekey );


	// make sure reticle stuff starts out hidden
//	m_overlayReticleCircleLineOuter->winHide(TRUE);
//	m_overlayReticleCircleLineInner->winHide(TRUE);
	m_overlayReticleCircleAlphaOuter->winHide(TRUE);
	m_overlayReticleCircleAlphaInner->winHide(TRUE);
	m_overlayVsBackdrop->winHide(TRUE);
	m_overlayVs->winHide(TRUE);

	m_wndVideoManager = NEW WindowVideoManager;
	m_wndVideoManager->init();

	if(TheGameLODManager && TheGameLODManager->didMemPass())
	{
		Int progressUpdateCount = m_videoStream->frameCount() / FRAME_FUDGE_ADD;
		Int shiftedPercent = -FRAME_FUDGE_ADD + 1;
		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
		{
			TheGameEngine->serviceWindowsOS();

			if(!m_videoStream->isFrameReady())
			{
				sleepMilliseconds( 1 );	
				continue;
			}

			if (!TheGameEngine->isActive())
			{	//we are alt-tabbed out, so just increment the frame
				m_videoStream->frameNext();
				m_videoStream->frameDecompress();
				continue;
			}
			
			m_videoStream->frameDecompress();
			m_videoStream->frameRender(m_videoBuffer);
			m_videoStream->frameNext();

			if(m_videoBuffer)
				m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

			Int frame = m_videoStream->frameIndex();
			if(frame % progressUpdateCount == 0)
			{
				shiftedPercent++;
				if(shiftedPercent >0)
					shiftedPercent = 0;
				Int percent = (shiftedPercent + FRAME_FUDGE_ADD)/1.3;
				UnicodeString per;
				per.format(u"%d%%",percent);
				TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
				GadgetProgressBarSetProgress(m_progressBar, percent);
			}
			TheWindowManager->update();

			activatePieces(frame, generalPlayer, generalOpponent);
			m_wndVideoManager->update();

			// redraw all views, update the GUI
			TheDisplay->draw();

			TheAudio->update();
		}
	}
	else
	{
		// if we're min speced
		m_videoStream->frameGoto(m_videoStream->frameCount()); // zero based
		while(!m_videoStream->isFrameReady())
			sleepMilliseconds( 1 );
		m_videoStream->frameDecompress();
		m_videoStream->frameRender(m_videoBuffer);
		if(m_videoBuffer)
			m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);

		activatePiecesMinSpec(generalPlayer, generalOpponent);

		Int delay = mission->m_voiceLength * 1000;
		Int begin = Clock_Milliseconds();
		Int currTime = begin;
		Int fudgeFactor = 0;
		while(begin + delay > currTime )
		{		
			fudgeFactor = 30 * ((currTime - begin)/ INT_TO_REAL(delay ));
			GadgetProgressBarSetProgress(m_progressBar, fudgeFactor);

			TheWindowManager->update();
			TheDisplay->draw();
			sleepMilliseconds( 100 );
			currTime = Clock_Milliseconds();
		}
		
		m_wndVideoManager->update();
		TheWindowManager->update();
		TheDisplay->draw();
	}
	setFPMode();

	
	AudioEventRTS event( generalOpponent ? generalOpponent->getRandomTauntSound() : AsciiString::TheEmptyString );
	TheAudio->addAudioEvent( &event );

	m_ambientLoopHandle = TheAudio->addAudioEvent(&m_ambientLoop);
	TheAudio->update();
}

void ChallengeLoadScreen::reset( void )
{
 setLoadScreen(NULL);
 m_progressBar = NULL;
 s_challengeGauge = NULL;
}

void ChallengeLoadScreen::update( Int percent )
{
	percent = (percent + FRAME_FUDGE_ADD)/1.3;
	UnicodeString per;
	per.format(u"%d%%",percent);
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	GadgetProgressBarSetProgress(m_progressBar, percent);

	// Do this last!
	LoadScreen::update( percent );
}

void ChallengeLoadScreen::setProgressRange( Int min, Int max )
{

}

// ShellGameLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
ShellGameLoadScreen::ShellGameLoadScreen( void )
{
	m_progressBar = NULL;
	m_board = NULL;
}

ShellGameLoadScreen::~ShellGameLoadScreen( void )
{

	m_progressBar = NULL;
	delete m_board;
}

void ShellGameLoadScreen::init( GameInfo *game )
{
	static Bool firstLoad = TRUE;

	// a replay loads on the board a played match loads on, its players as they really were
	if( game && TheGameLogic->getGameMode() == GAME_REPLAY )
	{
		m_board = NEW LoadScreenBoard;
		m_loadScreen = m_board->create( game, "GUI:LoadModeReplay", FALSE );
		return;
	}

	// a saved campaign game loads on its mission's frame: the mission's picture, the save's date and
	// the mission's briefing (a skirmish save names no players, and keeps EA's screen)
	const Mission *mission = TheCampaignManager ? TheCampaignManager->getCurrentMission() : NULL;
	if( TheGameState->isInLoadGame() && TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER
		&& mission && TheCampaignManager->getCurrentCampaign() )
	{
		const SaveDate &date = TheGameState->getSaveGameInfo()->date;
		WallClockTime when;
		when.wYear = date.year;
		when.wMonth = date.month;
		when.wDayOfWeek = date.dayOfWeek;
		when.wDay = date.day;
		when.wHour = date.hour;
		when.wMinute = date.minute;
		when.wSecond = date.second;
		when.wMilliseconds = date.milliseconds;
		UnicodeString saved = TheGameText->fetch( "GUI:LoadSavedGame" );
		if( date.year > 0 )
		{
			saved.concat( UIStyle::HEADER_JOINER );
			saved.concat( getUnicodeDateBuffer( when ) );
			saved.concat( u" " );
			saved.concat( getUnicodeTimeBuffer( when ) );
		}
		UnicodeString title, subtitle;
		boardMissionWords( title, subtitle, saved );
		UnicodeString briefing[ MAX_OBJECTIVE_LINES ];
		for( Int line = 0; line < MAX_OBJECTIVE_LINES; ++line )
		{
			if( mission->m_missionObjectivesLabel[ line ].isNotEmpty() )
				briefing[ line ] = TheGameText->fetch( mission->m_missionObjectivesLabel[ line ] );
		}
		Int number, count;
		boardMissionPlace( &number, &count );
		m_board = NEW LoadScreenBoard;
		m_loadScreen = m_board->createMission( NULL, title, subtitle, briefing, MAX_OBJECTIVE_LINES, number, count, boardMissionPicture() );
		return;
	}

	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/ShellGameLoadScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the ShellGame loadscreen"));
	if( m_loadScreen == NULL )
	{
		// The game's own window files could not be read: a damaged or partial install, never a bug to
		// chase in the code. The crash report says so instead of a NULL window.
		RELEASE_CRASH(("The game's files are incomplete: Window\\Menus\\ShellGameLoadScreen.wnd could not be loaded. "
			"Verify the game files or install the game again."));
		return;
	}
	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	// Store the pointer to the progress bar on the loadscreen
	m_progressBar = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:ProgressLoad" ) ));
	DEBUG_ASSERTCRASH(m_progressBar, ("Can't initialize the progressbar for the single player loadscreen"));
	GadgetProgressBarSetProgress(m_progressBar, 0 );	
	m_progressBar->winHide(TRUE);
	
	if(m_loadScreen && firstLoad && TheGameLODManager && TheGameLODManager->didMemPass())
	{
		m_loadScreen->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("TitleScreen"));
		TheWritableGlobalData->m_breakTheMovie = FALSE;

//		m_videoStream = TheVideoPlayer->open( "Sizzle" );
//		if ( m_videoStream == NULL )
//		{
//			m_progressBar->winHide(FALSE);
//			return;
//		}
//
//		// Create the new buffer
//		m_videoBuffer = TheDisplay->createVideoBuffer();
//		if (	m_videoBuffer == NULL || 
//					!m_videoBuffer->allocate(	m_videoStream->width(), 
//														m_videoStream->height())
//			)
//		{
//			delete m_videoBuffer;
//			m_videoBuffer = NULL;
//
//			if ( m_videoStream )
//				m_videoStream->close();
//			m_videoStream = NULL;
//
//			return;
//		}
//		TheGlobalData->m_isBreakableMovie = TRUE;
//		TheGlobalData->m_breakTheMovie = FALSE;
//		while (m_videoStream->frameIndex() < m_videoStream->frameCount() - 1 )
//		{
//			if(TheGlobalData->m_breakTheMovie)
//			{
//				TheGlobalData->m_breakTheMovie = FALSE;
//				m_videoStream->frameGoto(m_videoStream->frameCount() - 1);
//			}
//			if(m_videoStream->frameIndex() < m_videoStream->frameCount() - 1)
//			{
//				if(!m_videoStream->isFrameReady())
//					continue;
//
//				m_videoStream->frameDecompress();
//				m_videoStream->frameRender(m_videoBuffer);
//				m_videoStream->frameNext();
//				if(m_videoBuffer)
//					m_loadScreen->winGetInstanceData()->setVideoBuffer(m_videoBuffer);
//			}
//
//			TheWindowManager->update();
//		//	TheShell->update();
//			//TheDisplay->update();
//			// redraw all views, update the GUI
//			TheDisplay->draw();
//		}
//		TheGlobalData->m_isBreakableMovie = FALSE;
//		TheGlobalData->m_breakTheMovie = FALSE;
//		GameWindow *win = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:EAGamesLogo" ) ));
//		if(win)
//			win->winHide(FALSE);

		GameWindow *win = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( AsciiString( "ShellGameLoadScreen.wnd:StaticTextLegal" ) ));
		if(win)
			win->winHide(FALSE);
		firstLoad = FALSE;

		UnsignedInt showTime = Clock_Milliseconds();
		while(showTime + 3000 > Clock_Milliseconds())
		{	
			LoadScreen::update(0);
			sleepMilliseconds( 100 );
		}

	}
	m_progressBar->winHide(FALSE);	
}

void ShellGameLoadScreen::reset( void )
{
 setLoadScreen(NULL);
 m_progressBar = NULL;
}

void ShellGameLoadScreen::update( Int percent )
{
	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);
	if( m_board )
		m_board->setLocalProgress( percent );
	else
		GadgetProgressBarSetProgress(m_progressBar, percent);

	// Do this last!
	LoadScreen::update( percent );
}

// MultiPlayerLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MultiPlayerLoadScreen::MultiPlayerLoadScreen( void )
{
	m_board = NULL;
}

MultiPlayerLoadScreen::~MultiPlayerLoadScreen( void )
{
	delete m_board;

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
//	TheAudio->stopAudio( AudioAffect_Music );
}

void MultiPlayerLoadScreen::init( GameInfo *game )
{
	// a skirmish has one machine loading and one bar; a LAN game gives every human his own
	const Bool network = TheGameLogic->getGameMode() == GAME_LAN;
	m_board = NEW LoadScreenBoard;
	m_loadScreen = m_board->create( game, network ? "GUI:LoadModeLAN" : "GUI:LoadModeSkirmish", network );

	// getLocalSlotNum() is -1 when no slot in the list is the local player - a game watched from
	// the observer camera rather than played - and getSlot(-1) hands back NULL.  Same guard the
	// score screen already has.
	GameSlot *lSlot = game->getSlot(game->getLocalSlotNum());
	const PlayerTemplate* pt;
	if (lSlot && lSlot->getPlayerTemplate() >= 0)
		pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
	else
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );

	AsciiString musicName = pt->getLoadScreenMusic();
	if ( ! musicName.isEmpty() )
	{
		TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
		AudioEventRTS event( musicName );
		event.setShouldFade( TRUE );

		TheAudio->addAudioEvent( &event );
		TheAudio->update();//Since GameEngine::update() is suspended until after I am gone... 

	}

	TheGameLogic->initTimeOutValues();
}

void MultiPlayerLoadScreen::reset( void )
{
	setLoadScreen(NULL);
}

void MultiPlayerLoadScreen::update( Int percent )
{
	m_board->setLocalProgress( percent );
	if (TheNetwork)
	{
		if(percent <= 100)
			TheNetwork->updateLoadProgress( percent );
		TheNetwork->liteupdate();
	}
	else
	{
		if (percent <= 100)
			TheGameLogic->processProgress( TheGameInfo->getLocalSlotNum(), percent );
	}

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );	

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MultiPlayerLoadScreen::processProgress(Int playerId, Int percentage)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	m_board->setProgress( playerId, percentage );
}

/* The match has loaded under the board and the in-game windows came up behind it; the board stays on top
	 until the seat is back in the room. */
void MultiPlayerLoadScreen::showRejoin( Int percent, Bool waitingForLog )
{
	m_board->setRejoinProgress( percent, waitingForLog );
	if( m_loadScreen )
		m_loadScreen->winBringToTop();
}

// GameSpyLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
GameSpyLoadScreen::GameSpyLoadScreen( void )
{
	m_board = NULL;
}

GameSpyLoadScreen::~GameSpyLoadScreen( void )
{
	delete m_board;
}

void GameSpyLoadScreen::init( GameInfo *game )
{
	m_board = NEW LoadScreenBoard;
	m_loadScreen = m_board->create( game, "GUI:LoadModeOnline", TRUE );

	// a player's rank beside his name, from the stats the lobby fetched
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		GameSpyGameSlot *slot = (GameSpyGameSlot *)game->getSlot(i);
		if( !slot || !slot->isHuman() || slot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
			continue;
		PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(slot->getProfileID());
		m_board->setBadge( i, LookupSmallRankImage( GetFavoriteSide(stats), CalculateRank(stats) ) );
	}

	TheGameLogic->initTimeOutValues();
}

void GameSpyLoadScreen::reset( void )
{
	setLoadScreen(NULL);
}

void GameSpyLoadScreen::update( Int percent )
{
	m_board->setLocalProgress( percent );
	if(percent <= 100)
		TheNetwork->updateLoadProgress( percent );
	TheNetwork->liteupdate();

	//GadgetProgressBarSetProgress(m_progressBars[TheNetwork->getLocalPlayerID()], percent );	

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void GameSpyLoadScreen::processProgress(Int playerId, Int percentage)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	m_board->setProgress( playerId, percentage );
}

void GameSpyLoadScreen::showRejoin( Int percent, Bool waitingForLog )
{
	m_board->setRejoinProgress( percent, waitingForLog );
	if( m_loadScreen )
		m_loadScreen->winBringToTop();
}

// MapTransferLoadScreen Class //////////////////////////////////////////////////
//-----------------------------------------------------------------------------
MapTransferLoadScreen::MapTransferLoadScreen( void )
{
	m_board = NULL;
	m_oldTimeout = 0;
	for(Int i = 0; i < MAX_SLOTS; ++i)
		m_oldProgress[i] = -1;
}

MapTransferLoadScreen::~MapTransferLoadScreen( void )
{
	// the board's window goes with the load screen's in ~LoadScreen
	delete m_board;
	m_board = NULL;
}

/* Every human on the board's rows, any count, with the state the network reports; EA's
	 MapTransferScreen.wnd had eight rows and is not used. */
void MapTransferLoadScreen::init( GameInfo *game )
{
	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network?!!!!"));
	const Bool online = TheGameSpyGame && game == static_cast< GameInfo * >( TheGameSpyGame );
	m_board = NEW LoadScreenBoard;
	m_loadScreen = m_board->createTransfer( game, online ? "GUI:LoadModeOnline" : "GUI:LoadModeLAN" );
}

void MapTransferLoadScreen::reset( void )
{
	setLoadScreen(NULL);
	for(Int i = 0; i < MAX_SLOTS; ++i)
		m_oldProgress[i] = -1;
}

void MapTransferLoadScreen::update( Int percent )
{
	if (TheNetwork)
	{
		TheNetwork->liteupdate();
	}

	TheMouse->setCursorTooltip(UnicodeString::TheEmptyString);

	// Do this last!
	LoadScreen::update( percent );
}

void MapTransferLoadScreen::processProgress(Int playerId, Int percentage, AsciiString stateStr)
{
	
	// The test named every way playerId can be wrong and then indexed m_playerLookup with
	// it anyway; in Release the assert is not compiled in at all, so a -1 read past the array.
	if( percentage < 0 || percentage > 100 || playerId >= MAX_SLOTS || playerId < 0 )
	{
		DEBUG_ASSERTCRASH(FALSE, ("Percentage %d was passed in for Player %d\n", percentage, playerId));
		return;
	}
	if( m_board == NULL )
		return;

	if (m_oldProgress[playerId] == percentage)
		return;
	m_oldProgress[playerId] = percentage;

	m_board->setTransfer( playerId, percentage, TheGameText->fetch(stateStr) );
}

void MapTransferLoadScreen::processTimeout(Int secondsLeft)
{
	if (m_oldTimeout == secondsLeft)
		return;
	m_oldTimeout = secondsLeft;

	if (m_board)
	{
		UnicodeString txt;
		txt.format(TheGameText->fetch("MapTransfer:Timeout"), (secondsLeft/60), (secondsLeft%60));
		m_board->setTransferFoot( NULL, &txt );
	}
}

void MapTransferLoadScreen::setCurrentFilename(AsciiString filename)
{
	if (m_board)
	{
		UnicodeString name;
		name.translate(TheGameState->getMapLeafName(filename));
		UnicodeString txt;
		txt.format(TheGameText->fetch("MapTransfer:CurrentFile"), name.str());
		m_board->setTransferFoot( &txt, NULL );
	}
}

