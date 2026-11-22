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
/* A match's load, skirmish, LAN, online or replay, as one board drawn in code: a card per player with
	 the general's portrait or the side's crest, the name in the player's colour, the army and, for a
	 computer seat, its level and personality; the teams in two columns facing each other across the
	 map, or the players shared between the columns in a free-for-all; the map's name, its preview
	 with the start spots, a tip and the load's progress along the bottom.  In a network game every
	 human's card carries his own bar.

	 Everything is laid out once, when the board is made, against a 1920x1080 picture scaled to the
	 screen; a pass draws flat fills, the images the lobby and the challenge menu already load (the
	 portraits share one 512 page, the crests another) and display strings made up front.  The
	 colours, the type and the boxes are the front end's (UIStyle.h), so the board reads as the
	 staging room it came from. */

/// the picture the boxes below are given for; another screen scales them by its smaller axis
static const Real BOARD_WIDTH = 1920.0f;
static const Real BOARD_HEIGHT = 1080.0f;
static const UnsignedInt BOARD_TIP_MILLISECONDS = 8000;
/// a middle dot between spaces, spelled out (the source is read in the machine's code page), between a
/// computer seat's level and personality; the header joins its parts with UIStyle::HEADER_JOINER
static const WideChar BOARD_SEPARATOR[] = { ' ', ' ', ' ', 0x00B7, ' ', ' ', ' ', 0 };
/// the same dot between a one-line card's name and general
static const WideChar BOARD_LINE_SEPARATOR[] = { ' ', 0x00B7, ' ', 0 };
/// a column of cards, and the gap between a column and the middle's map and facts
static const Real BOARD_COLUMN_WIDTH = 600.0f;
static const Real BOARD_MIDDLE_GAP = 32.0f;
/// a card's room between its portrait and its words
static const Real BOARD_CARD_TEXT_GAP = 24.0f;
/// the rooms' start spot button against their map preview (SkirmishGameOptionsMenu.wnd), so the spots
/// keep the room's size on a preview of another size
static const Int LOBBY_START_SPOT = 28;
static const Int LOBBY_MAP_PREVIEW = 410;

/// one player's card
struct BoardCard
{
	Int slot;
	Color color;
	Color nameColor;					///< the colour, lifted toward white where it is too dark to read on the panel
	Int team;									///< the board's team, ScoreScreen_teamsFromAllies over the lobby's teams
	Int column;								///< 0 left, 1 right
	Int startSpot;						///< the start position the others may see, -1 when hidden
	Int percent;							///< this player's own load, -1 on a card without a bar
	Bool mirrored;						///< the right column's cards face left
	const Image *emblem;			///< the general's portrait or the side's crest, NULL for a hidden or random side
	const Image *crest;				///< the side's crest, a one-line card's emblem
	const Image *badge;				///< an online player's rank
	UnicodeString nameWords;	///< the words whole; the strings below hold them fitted to the card
	UnicodeString armyWords;
	UnicodeString detailWords;
	DisplayString *name;
	DisplayString *army;
	DisplayString *detail;		///< a computer seat's level and personality
	DisplayString *percentText;
	DisplayString *disc;			///< the seat number on the start spot disc, NULL where the map shows no spot of his
	IRegion2D box;
};

/// a team's band over its cards in a column, or the rule in its colour a crowded column keeps
struct BoardBand
{
	IRegion2D box;
	Color color;
	Bool mirrored;
	DisplayString *label;			///< "Team N", NULL for a rule
};

/// a row of the facts box: the lobby's label, what it was set to
struct BoardFact
{
	DisplayString *label;
	DisplayString *value;
};

/// where a card's parts stand across it, from its plan and its words
struct BoardCardSpan
{
	Int emblemX;
	Int textLeft;
	Int textRight;
	Int innerLeft;						///< the inner column: the disc, the percentage
	Int innerRight;
};

/// one of the map's start spots, drawn as the room draws it
struct BoardSpot
{
	ICoord2D pos;
	Color color;							///< the player's on it, 0 for a spot open or hidden
	DisplayString *number;		///< the player's seat, NULL with the colour
};

class LoadScreenBoard
{
public:
	LoadScreenBoard( void );
	~LoadScreenBoard( void );

	/// lays the board out for this game and makes the window that draws it
	GameWindow *create( GameInfo *game, const char *modeLabel, Bool cardBars );
	/// a campaign mission's: the header, the briefing and the foot over the mission's own picture
	GameWindow *createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
		const UnicodeString *briefing, Int briefingLines );
	void setBadge( Int slot, const Image *badge );
	void setProgress( Int slot, Int percent );
	void setLocalProgress( Int percent );
	void setRejoinProgress( Int percent, Bool waitingForLog );

private:
	static void drawWindow( GameWindow *window, WinInstanceData *instData );
	void prepare( const UnicodeString &titleInCapitals, const UnicodeString &subtitle, Bool titleIsName );
	GameWindow *makeWindow( GameWindow *parent );
	void draw( void );
	void drawMatch( void );
	void drawBriefing( void );
	void drawCard( const BoardCard &card );
	void drawTip( void );
	Int scaled( Real pixels ) const { return REAL_TO_INT( pixels * m_scale + 0.5f ); }
	DisplayString *text( const UnicodeString &words, GameFont *font );
	BoardCard *cardFor( Int slot );
	void cardSpan( const BoardCard &card, BoardCardSpan &span );
	void fitCard( BoardCard &card );
	void addFact( const char *label, const UnicodeString &value, Int width );
	void setPercentLabel( Int percent );

	Real m_scale;
	Int m_left;								///< the 16:9 picture's corner, inset on a squarer screen
	Int m_top;
	Int m_width;							///< the screen
	Int m_height;
	std::vector< BoardCard > m_cards;
	std::vector< BoardBand > m_bands;
	std::vector< BoardSpot > m_spots;
	std::vector< BoardFact > m_facts;
	std::vector< DisplayString * > m_strings;
	LoadCardPlan m_plan[ 2 ];	///< each column's cards
	GameFont *m_nameFont;
	GameFont *m_lineNameFont;	///< a name on a small one-line card
	GameFont *m_armyFont;
	GameFont *m_detailFont;
	GameFont *m_smallFont;		///< the percentage, the facts, a band's label
	Bool m_mission;
	DisplayString *m_briefingHead;
	std::vector< DisplayString * > m_briefing;
	IRegion2D m_briefingBox;
	DisplayString *m_title;
	DisplayString *m_subtitle;
	DisplayString *m_wordmark[ 2 ];
	DisplayString *m_centerLabel;
	ICoord2D m_centerLabelPos;
	DisplayString *m_observers;
	ICoord2D m_observersPos;
	DisplayString *m_unknown;
	DisplayString *m_unknownSmall;
	DisplayString *m_lineSeparator;
	DisplayString *m_fullPercent;		///< "100%", the room a card keeps for its percentage
	const Image *m_mapImage;
	IRegion2D m_mapBox;
	IRegion2D m_mapDrawn;
	IRegion2D m_factsBox;
	Int m_factPitch;
	Int m_spotSize;						///< a start spot on the map and on a card
	DisplayString *m_tipLabel;
	DisplayString *m_tip;
	IRegion2D m_tipBox;
	Int m_tipTextLeft;
	std::vector< Int > m_tips;	///< the GUI:LoadTip numbers that fit in two lines here
	Int m_tipAt;
	UnsignedInt m_tipSince;
	UnicodeString m_progressFormat;
	DisplayString *m_percent;
	DisplayString *m_waiting;
	Int m_localPercent;
	Int m_rejoinShown;				///< the rejoin bar's last value, -1 for the log wait, -2 before the first
	Bool m_network;
};

LoadScreenBoard::LoadScreenBoard( void )
{
	m_width = TheDisplay->getWidth();
	m_height = TheDisplay->getHeight();
	m_scale = min( m_width / BOARD_WIDTH, m_height / BOARD_HEIGHT );
	m_left = ( m_width - scaled( BOARD_WIDTH ) ) / 2;
	m_top = ( m_height - scaled( BOARD_HEIGHT ) ) / 2;
	m_mission = FALSE;
	m_briefingHead = NULL;
	m_title = m_subtitle = m_centerLabel = m_observers = m_unknown = m_unknownSmall = NULL;
	m_lineSeparator = m_fullPercent = NULL;
	m_wordmark[ 0 ] = m_wordmark[ 1 ] = NULL;
	m_nameFont = UIStyle::font( UIStyle::TEXT_STRONG, TRUE );
	m_lineNameFont = UIStyle::readableFont( UIStyle::TEXT_BODY, TRUE );
	m_armyFont = UIStyle::readableFont( UIStyle::TEXT_BODY );
	// the small type grows on a small screen until its capitals reach the floor
	m_detailFont = UIStyle::readableFont( UIStyle::TEXT_SMALL );
	m_smallFont = UIStyle::readableFont( UIStyle::TEXT_SMALL, TRUE );
	m_spotSize = 1;
	m_mapImage = NULL;
	m_factPitch = 0;
	m_tipLabel = m_tip = m_percent = m_waiting = NULL;
	m_tipTextLeft = 0;
	m_tipAt = 0;
	m_tipSince = 0;
	m_localPercent = 0;
	m_rejoinShown = -2;
	m_network = FALSE;
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

static Int stringWidth( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return width;
}

static Int stringHeight( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return height;
}

/** A start spot as the room draws it: EA's ring (missing art draws no ring), and on a spot the
	* others may see, the player's colour in its middle and his seat number over it.  The map's spots
	* and a card's disc are the same mark. */
static void drawStartSpot( Int x, Int y, Int size, Color color, DisplayString *number )
{
	const Image *ring = TheMappedImageCollection->findImageByName( "PlayerStart" );
	if( ring )
		TheDisplay->drawImage( ring, x, y, x + size, y + size );
	if( number == NULL )
		return;
	LobbyStartSpotFill( x, y, size, size, color );
	number->draw( x + ( size - stringWidth( number ) ) / 2, y + ( size - stringHeight( number ) ) / 2,
		LobbyTextColorOn( color ), 0 );
}

/** A player's own load on his card, "81%", in the language's own order ("%81"). */
static UnicodeString playerPercent( Int percent )
{
	UnicodeString words;
	words.format( TheGameText->fetch( "GUI:LoadPlayerPercent" ), percent );
	return words;
}

/** The starting money as the header and the facts say it. */
static UnicodeString cashText( const GameInfo *game )
{
	const Money &cash = game->getStartingCash();
	if( cash.isUnlimited() )
		return TheGameText->fetch( "GUI:StartingMoneyUnlimited" );
	UnicodeString words( u"$" );
	words.concat( UIStyle::grouped( (Int)cash.countMoney() ) );
	return words;
}

/** The side's crest from the lobby: the three 144 pixel logos on one page.  A side on none of the
	* three brings its own lobby glyph or nothing. */
static const Image *sideCrest( const PlayerTemplate *side )
{
	const AsciiString base = side->getBaseSide();
	if( base.compareNoCase( "USA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SAFactionLogo144_US" );
	if( base.compareNoCase( "China" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SNFactionLogo144_China" );
	if( base.compareNoCase( "GLA" ) == 0 )
		return TheMappedImageCollection->findImageByName( "SUFactionLogo144_GLA" );
	return side->getGeneralImage();
}

/** A general's face from the challenge menu, all ten on one page; a plain side shows its crest. */
static const Image *sideEmblem( const PlayerTemplate *side )
{
	const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName( side->getName() );
	if( general && general->getBioPortraitSmall() )
		return general->getBioPortraitSmall();
	return sideCrest( side );
}

/** Tip number n of the string files' GUI:LoadTip1, GUI:LoadTip2, ...; exists says whether there is one. */
static UnicodeString tipText( Int number, Bool *exists = NULL )
{
	AsciiString label;
	label.format( "GUI:LoadTip%d", number );
	// probed first: the count runs one past the last tip, and fetching that one logs it as missing text
	const Bool there = UIStyle::hasText( label.str() );
	if( exists )
		*exists = there;
	return there ? TheGameText->fetch( label ) : UnicodeString::TheEmptyString;
}

GameWindow *LoadScreenBoard::create( GameInfo *game, const char *modeLabel, Bool cardBars )
{
	m_network = cardBars;
	GameFont *centerFont = UIStyle::titleFont( UIStyle::TEXT_DISPLAY );
	GameFont *observerFont = UIStyle::readableFont( UIStyle::TEXT_SMALL );

	// a watcher, a replay's viewer among them, sees every side as it is; a player sees what the
	// lobby let him see of his enemies
	// (a director's recording has no local seat at all)
	const Bool seeAll = TheGameLogic->isInReplayGame() || game->getLocalSlotNum() < 0
		|| game->getConstSlot( game->getLocalSlotNum() )->getOriginalPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;
	// a ranked Quick Match, and any match on a generated map, gives a player neither the map nor any
	// start spot, his own included, so the board never tells where the game put anyone
	const Bool mapHidden = ( game->getStartShrouded() || isGeneratedMapPath( game->getMap() ) ) && !seeAll;
	// a seat coming back to a match under way sees the sides and colours the match is played with, and no
	// bar on a card: the others are not loading, they are in the match (the wide button carries the catch-up)
	const Bool rejoining = TheNetwork && TheNetwork->isRejoining();
	const Bool realSides = seeAll || rejoining;

	// a card a player, in the order the lobby lists them.  Someone watching from this screen without a
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
		card.nameColor = UIStyle::readable( card.color );
		card.team = 0;
		card.column = 0;
		card.startSpot = mapHidden ? -1 : seeAll ? slot->getStartPos() : slot->getApparentStartPos();
		card.percent = cardBars && slot->isHuman() && !rejoining ? 0 : -1;
		card.mirrored = FALSE;
		card.badge = NULL;
		card.emblem = NULL;
		card.crest = NULL;
		card.disc = NULL;
		const Int sideIndex = realSides ? slot->getPlayerTemplate() : slot->getApparentPlayerTemplate();
		UnicodeString army = TheGameText->fetch( "GUI:Random" );
		if( sideIndex >= 0 )
		{
			const PlayerTemplate *side = ThePlayerTemplateStore->getNthPlayerTemplate( sideIndex );
			card.emblem = sideEmblem( side );
			card.crest = sideCrest( side );
			army = side->getDisplayName();
		}
		card.nameWords = slot->getName();
		card.armyWords = army;
		card.name = text( card.nameWords, m_nameFont );
		card.army = text( card.armyWords, m_armyFont );
		lobbyTeam.push_back( slot->getTeamNumber() );

		UnicodeString detail;
		if( slot->isAI() )
		{
			const UnicodeString level = SlotStateName( slot->getState(), slot->getAIRating() );
			if( slot->getName().compare( level ) != 0 )
				detail = level;
			if( slot->getAIPersonality() != AIPERSONA_NORMAL )
			{
				AsciiString label;
				label.format( "GUI:AIPersona%s", aiPersonalityName( (AIPersonality)slot->getAIPersonality() ) );
				if( !detail.isEmpty() )
					detail.concat( BOARD_SEPARATOR );
				detail.concat( TheGameText->fetch( label ) );
			}
		}
		card.detailWords = detail;
		card.detail = text( detail, m_detailFont );
		card.percentText = text( playerPercent( 0 ), m_smallFont );
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
	const Bool banded = ScoreScreen_banded( teams );
	const Bool versus = teams.size() == 2;

	// a team is called by its lobby number and wears its lobby colour; a player alone takes the
	// lowest number no lobby team has, on the room's no-team slate, as the score screen calls him
	// (a team here is one lobby team, or one player with none)
	std::vector< Int > sharedLobbyTeam( teams.size(), -1 );
	std::vector< Int > firstSlot( teams.size(), 0 );
	for( size_t t = 0; t < teams.size(); t++ )
	{
		// the cards run in seat order, so a team's first member has its lowest seat
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

	// two teams face each other across the map; more share the two columns, whole teams, the largest
	// first, each to the column with fewer rows, so a team is never split across the map
	std::vector< Int > bySize( teams.size() );
	for( size_t t = 0; t < teams.size(); t++ )
		bySize[ t ] = (Int)t;
	if( !versus )
	{
		std::stable_sort( bySize.begin(), bySize.end(),
			[ &teams ]( Int a, Int b ) { return teams[ a ].members.size() > teams[ b ].members.size(); } );
	}
	std::vector< Int > columnTeams[ 2 ];
	Int columnRows[ 2 ] = { 0, 0 };
	for( size_t each = 0; each < bySize.size(); each++ )
	{
		const Int t = bySize[ each ];
		const Int side = versus ? (Int)each : ( columnRows[ 1 ] < columnRows[ 0 ] ? 1 : 0 );
		columnTeams[ side ].push_back( t );
		columnRows[ side ] += (Int)teams[ t ].members.size() + ( banded ? 1 : 0 );
	}
	// a column keeps its teams in seat order
	for( Int side = 0; side < 2; side++ )
		std::sort( columnTeams[ side ].begin(), columnTeams[ side ].end() );

	// the header: the map, then what kind of game, its format as the score screen words it, the cash
	const MapMetaData *map = TheMapCache->findMap( game->getMap() );
	UnicodeString mapName;
	if( map )
		mapName = map->m_displayName;
	else
		mapName.translate( TheGameState->getMapLeafName( game->getMap() ) );
	UnicodeString subtitle = TheGameText->fetch( modeLabel );
	UnicodeString makeup;
	if( freeForAll )
	{
		UnicodeString players;
		players.format( u" (%d)", count );
		makeup = TheGameText->fetch( "GUI:LoadFreeForAll" );
		makeup.concat( players );
	}
	else
		makeup.translate( AsciiString( format.c_str() ) );
	if( !makeup.isEmpty() )
	{
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( makeup );
	}
	subtitle.concat( UIStyle::HEADER_JOINER );
	subtitle.concat( cashText( game ) );
	prepare( UIStyle::nameCapitals( mapName ), subtitle, TRUE );

	m_unknown = text( UnicodeString( u"?" ), centerFont );
	m_unknownSmall = text( UnicodeString( u"?" ), m_smallFont );
	m_lineSeparator = text( UnicodeString( BOARD_LINE_SEPARATOR ), m_armyFont );
	m_fullPercent = text( playerPercent( 100 ), m_smallFont );

	const Int columnWidth = scaled( BOARD_COLUMN_WIDTH );
	const Int bandTop = m_top + scaled( LOAD_BAND_TOP );
	const Int bandBottom = m_tipBox.lo.y - scaled( LOAD_FOOT_GAP );
	const Int leftX = m_left + scaled( UIStyle::ROOM_INSET );
	const Int rightX = m_left + scaled( UIStyle::ROOM_PANEL_RIGHT + UIStyle::ROOM_PANEL_LEFT - UIStyle::ROOM_INSET ) - columnWidth;

	// the middle, between the columns with a gap to the cards: VS over the map in a versus, the map
	// as large as the room leaves, the facts box under it and the watchers under that
	const Int middleLeft = leftX + columnWidth + scaled( BOARD_MIDDLE_GAP );
	const Int middleWidth = rightX - scaled( BOARD_MIDDLE_GAP ) - middleLeft;
	const Int gap = scaled( 16 );
	m_centerLabel = versus ? text( UnicodeString( u"VS" ), centerFont ) : NULL;

	// the lobby's rules with the lobby's words for them
	{
		Int speed = 0;
		for( Int each = 0; each < PLAYER_GAME_SPEED_COUNT; each++ )
		{
			if( PLAYER_GAME_SPEED_PERCENTS[ each ] == GAME_SPEED_PERCENT )
				speed = each;
		}
		AsciiString speedLabel;
		speedLabel.format( "GUI:GameSpeedMissions%d", speed );
		addFact( "GUI:GameSpeed", TheGameText->fetch( speedLabel ), middleWidth );
		addFact( "GUI:StartingMoney", cashText( game ), middleWidth );
		addFact( "GUI:Superweapons", TheGameText->fetch( SuperweaponCaption( game->getSuperweaponRestriction() ) ), middleWidth );
		addFact( "GUI:IncomeSharing", TheGameText->fetch( IncomeSharingCaption( game->getIncomeSharing() ) ), middleWidth );
		UnicodeString words;
		if( game->getSupplyPileLimit() > 0 )
			words.format( TheGameText->fetch( "GUI:SupplyPileLimitFormat" ), game->getSupplyPileLimit() );
		else
			words = TheGameText->fetch( "GUI:SupplyPileLimitOff" );
		addFact( "GUI:SupplyPileLimit", words, middleWidth );
		if( game->getTechRespawn() > 0 )
			words.format( TheGameText->fetch( "GUI:TechRespawnFormat" ), game->getTechRespawn() );
		else
			words = TheGameText->fetch( "GUI:TechRespawnOff" );
		addFact( "GUI:TechRespawn", words, middleWidth );
		addFact( "GUI:UnitLimit", TheGameText->fetch( game->getUnitLimit() ? "GUI:LoadRuleOn" : "GUI:LoadRuleOff" ), middleWidth );
		if( game->getDayNight() )	// only when on, so every other match's list is as long as it was
			addFact( "GUI:DayNight", TheGameText->fetch( "GUI:LoadRuleOn" ), middleWidth );
	}
	const Int factPad = scaled( 10 );
	const Int factLeast = m_factPitch;

	// the watchers, the one at this screen among them: as many names as the lines hold, then how
	// many more; two lines, more where the map leaves the room
	m_observers = NULL;
	Int observerLines = 2;
	if( !observerNames.empty() )
		m_observers = text( TheGameText->fetch( "GUI:LoadObservers" ), observerFont );
	Int observerLine = m_observers ? stringHeight( m_observers ) : 0;
	if( m_observers )
	{
		m_observers->setWordWrap( middleWidth );
		m_observers->setWordWrapCentered( TRUE );
	}

	// the map's own shape inside the room left, the frame hugging it as the score screen's does
	Real aspect = 1.0f;
	if( map && map->m_extent.width() > 0.0f && map->m_extent.height() > 0.0f )
		aspect = map->m_extent.width() / map->m_extent.height();
	Int mapWidth = 0, mapHeight = 0, factsHeight = 0;
	for( Int pass = 0; pass < 2; pass++ )
	{
		if( m_observers )
		{
			const Int total = (Int)observerNames.size();
			const Int most = observerLines * observerLine + observerLine / 2;
			for( Int shown = total; shown >= 0; shown-- )
			{
				UnicodeString line = TheGameText->fetch( "GUI:LoadObservers" );
				line.concat( u"  " );
				for( Int each = 0; each < shown; each++ )
				{
					if( each > 0 )
						line.concat( u", " );
					line.concat( UIStyle::fitName( observerNames[ each ], observerFont, middleWidth / 2 ) );
				}
				if( shown < total )
				{
					UnicodeString more;
					more.format( u"  +%d", total - shown );
					line.concat( more );
				}
				m_observers->setText( line );
				if( stringHeight( m_observers ) < most )
					break;
			}
		}
		factsHeight = (Int)m_facts.size() * m_factPitch + 2 * factPad;
		Int stack = gap + factsHeight;
		if( m_centerLabel )
			stack += stringHeight( m_centerLabel ) + gap;
		if( m_observers )
			stack += gap + stringHeight( m_observers );
		const Int roomHigh = max( scaled( 120 ), bandBottom - bandTop - stack );
		mapWidth = middleWidth;
		mapHeight = REAL_TO_INT( mapWidth / aspect );
		if( mapHeight > roomHigh )
		{
			mapHeight = roomHigh;
			mapWidth = REAL_TO_INT( mapHeight * aspect );
		}
		mapWidth = max( mapWidth, 1 );
		mapHeight = max( mapHeight, 1 );
		if( pass == 1 )
			break;
		// what a wide map leaves goes to the facts' rows, up to the lobby's pitch, then to the watchers
		Int spare = roomHigh - mapHeight;
		if( !m_facts.empty() && spare > 0 )
		{
			const Int grow = min( spare / (Int)m_facts.size(), max( 0, scaled( 38 ) - factLeast ) );
			m_factPitch = factLeast + grow;
			spare -= grow * (Int)m_facts.size();
		}
		if( m_observers && observerLine > 0 && spare > observerLine )
			observerLines = min( 2 + spare / observerLine, 6 );
	}
	Int stack = gap + factsHeight + mapHeight;
	if( m_centerLabel )
		stack += stringHeight( m_centerLabel ) + gap;
	if( m_observers )
		stack += gap + stringHeight( m_observers );
	const Int middle = middleLeft + middleWidth / 2;
	Int y = bandTop + max( 0, bandBottom - bandTop - stack ) / 2;
	if( m_centerLabel )
	{
		m_centerLabelPos.x = middle - stringWidth( m_centerLabel ) / 2;
		m_centerLabelPos.y = y;
		y += stringHeight( m_centerLabel ) + gap;
	}
	m_mapBox.lo.x = middle - mapWidth / 2;
	m_mapBox.lo.y = y;
	m_mapBox.hi.x = m_mapBox.lo.x + mapWidth;
	m_mapBox.hi.y = y + mapHeight;
	m_mapDrawn = m_mapBox;
	y += mapHeight + gap;
	m_factsBox.lo.x = middleLeft;
	m_factsBox.lo.y = y;
	m_factsBox.hi.x = middleLeft + middleWidth;
	m_factsBox.hi.y = y + factsHeight;
	y += factsHeight + gap;
	if( m_observers )
	{
		m_observersPos.x = middleLeft;
		m_observersPos.y = y;
	}

	// the preview the lobby showed and the start spots the others may see, the player's colour and
	// seat on each, as the room shows them; an open or hidden spot is not drawn, as on the score
	// screen.  A card whose spot shows wears the same disc at the same size, never under 18 pixels
	// at 720 and never too small for its number
	m_mapImage = mapHidden ? TheMappedImageCollection->findImageByName( "UnknownMap" ) : getMapPreviewImage( game->getMap() );
	GameFont *numberFont = m_smallFont;
	m_spotSize = max( max( mapWidth, mapHeight ) * LOBBY_START_SPOT / LOBBY_MAP_PREVIEW, max( scaled( 27 ), numberFont->height + 2 ) );
	if( map )
	{
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
				// the seat as a number, so seat 9 and past read as 9 and not as a missing string
				UnicodeString seat;
				seat.format( u"%d", m_cards[ each ].slot + 1 );
				BoardSpot spot;
				spot.pos.x = m_mapDrawn.lo.x + REAL_TO_INT( across * ( m_mapDrawn.hi.x - m_mapDrawn.lo.x ) );
				spot.pos.y = m_mapDrawn.lo.y + REAL_TO_INT( down * ( m_mapDrawn.hi.y - m_mapDrawn.lo.y ) );
				spot.color = m_cards[ each ].color;
				spot.number = text( seat, numberFont );
				m_cards[ each ].disc = text( seat, numberFont );
				m_spots.push_back( spot );
			}
		}
	}

	// each column's cards, under their teams' bands, as tall as the column lets them be;
	// LoadScreen_planCards says what a crowded column gives up
	LoadCardLines lines;
	lines.scale = m_scale;
	lines.name = m_nameFont->height;
	lines.army = max( m_armyFont->height, m_lineNameFont->height );
	lines.percent = m_smallFont->height;
	lines.band = m_smallFont->height;
	lines.disc = m_spotSize;
	lines.bars = cardBars;
	const Int columnHeight = bandBottom - bandTop;
	for( Int side = 0; side < 2; side++ )
	{
		// the column's cards in its teams' order, each with its team
		std::vector< Int > order, orderTeam;
		lines.detail = 0;
		for( size_t t = 0; t < columnTeams[ side ].size(); t++ )
		{
			const ScoreTeam &team = teams[ columnTeams[ side ][ t ] ];
			for( size_t m = 0; m < team.members.size(); m++ )
			{
				order.push_back( team.members[ m ] );
				orderTeam.push_back( columnTeams[ side ][ t ] );
				if( !m_cards[ team.members[ m ] ].detailWords.isEmpty() )
					lines.detail = m_detailFont->height;
			}
		}
		const Int cards = (Int)order.size();
		const Int columnBands = banded ? (Int)columnTeams[ side ].size() : 0;
		const Int subColumns = LoadScreen_subColumns( cards, columnBands, columnHeight, lines );

		// more than one sub-column splits the cards by count, a team running on into the second keeps
		// a band over it there too
		const Int firstCount = ( cards + subColumns - 1 ) / subColumns;
		Int subCards[ 2 ] = { 0, 0 };
		Int subBands[ 2 ] = { 0, 0 };
		for( Int each = 0; each < cards; each++ )
		{
			const Int sub = each < firstCount ? 0 : 1;
			const Bool teamStarts = each == 0 || each == firstCount || orderTeam[ each ] != orderTeam[ each - 1 ];
			subCards[ sub ]++;
			if( banded && teamStarts )
				subBands[ sub ]++;
		}
		m_plan[ side ] = subColumns == 1 ? LoadScreen_planCards( cards, columnBands, columnHeight, lines )
			: LoadScreen_planCards( max( subCards[ 0 ], subCards[ 1 ] ), max( subBands[ 0 ], subBands[ 1 ] ), columnHeight, lines, LOAD_CARD_LINE );
		const LoadCardPlan &plan = m_plan[ side ];
		const Int subGap = scaled( 8 );
		const Int subWidth = subColumns == 1 ? columnWidth : ( columnWidth - subGap ) / 2;
		Int y = 0;
		for( Int each = 0; each < cards; each++ )
		{
			const Int sub = each < firstCount ? 0 : 1;
			const Int x = ( side == 0 ? leftX : rightX ) + sub * ( subWidth + subGap );
			if( each == 0 || each == firstCount )
			{
				const Int used = subCards[ sub ] * plan.cardHeight + ( plan.bandHeight > 0 ? subBands[ sub ] : 0 ) * plan.bandHeight
					+ ( subCards[ sub ] + ( plan.bandHeight > 0 ? subBands[ sub ] : 0 ) - 1 ) * plan.gap;
				y = bandTop + max( 0, columnHeight - used ) / 2;
			}
			const Int which = orderTeam[ each ];
			const Bool teamStarts = each == 0 || each == firstCount || which != orderTeam[ each - 1 ];
			if( banded && teamStarts && plan.bandHeight > 0 )
			{
				BoardBand band;
				band.box.lo.x = x;
				band.box.lo.y = y;
				band.box.hi.x = x + subWidth;
				band.box.hi.y = y + plan.bandHeight;
				band.color = teamColor[ which ];
				band.mirrored = side == 1;
				band.label = NULL;
				if( plan.bandLabels )
				{
					UnicodeString label;
					label.format( TheGameText->fetch( "GUI:LoadTeam" ), teamNumber[ which ] );
					band.label = text( UIStyle::fitText( label, m_smallFont, subWidth / 2 ), m_smallFont );
				}
				m_bands.push_back( band );
				y += plan.bandHeight + plan.gap;
			}
			BoardCard &card = m_cards[ order[ each ] ];
			card.column = side;
			card.mirrored = side == 1;
			card.box.lo.x = x;
			card.box.lo.y = y;
			card.box.hi.x = x + subWidth;
			card.box.hi.y = y + plan.cardHeight;
			y += plan.cardHeight + plan.gap;
		}
	}
	for( size_t each = 0; each < m_cards.size(); each++ )
		fitCard( m_cards[ each ] );

	return makeWindow( NULL );
}

/** A row for the facts box, the label shortened by its language's rule before it would touch the
	* value, the value kept to three fifths of the row. */
void LoadScreenBoard::addFact( const char *label, const UnicodeString &value, Int width )
{
	GameFont *font = UIStyle::readableFont( UIStyle::TEXT_SMALL );
	const Int pad = scaled( 10 );
	const Int room = width - 3 * pad;
	BoardFact fact;
	fact.value = text( value, font );
	if( stringWidth( fact.value ) > room * 3 / 5 )
		fact.value->setText( UIStyle::fitText( value, font, room * 3 / 5 ) );
	fact.label = text( UIStyle::fitKey( label, font, room - stringWidth( fact.value ) ), font );
	m_factPitch = font->height + scaled( 6 );
	m_facts.push_back( fact );
}

/** Where a card's parts stand across it: the portrait or crest at the outer edge by the stripe, the
	* words beside it, and at the inner edge a column for the start spot disc and the percentage,
	* stacked on a tall card and side by side on a one-line card. */
void LoadScreenBoard::cardSpan( const BoardCard &card, BoardCardSpan &span )
{
	const LoadCardPlan &plan = m_plan[ card.column ];
	const Int x0 = card.box.lo.x;
	const Int width = card.box.hi.x - x0;
	const Int stripe = UIStyle::stripeWidth();
	const Int pad = scaled( LOAD_CARD_PAD );
	const Bool tall = plan.mode <= LOAD_CARD_SHORT;
	const Int textGap = scaled( tall ? BOARD_CARD_TEXT_GAP : 12.0f );
	const Int percentRoom = card.percent >= 0 ? stringWidth( m_fullPercent ) : 0;
	const Int discRoom = card.disc ? plan.disc : 0;
	Int inner = tall ? max( percentRoom, discRoom ) : percentRoom + discRoom + ( percentRoom > 0 && discRoom > 0 ? scaled( 8 ) : 0 );
	const Int innerGap = inner > 0 ? scaled( 12 ) : 0;
	if( !card.mirrored )
	{
		span.emblemX = x0 + stripe + pad;
		span.textLeft = span.emblemX + plan.emblem + textGap;
		span.innerRight = x0 + width - pad;
		span.innerLeft = span.innerRight - inner;
		span.textRight = span.innerLeft - innerGap;
	}
	else
	{
		span.emblemX = x0 + width - stripe - pad - plan.emblem;
		span.textRight = span.emblemX - textGap;
		span.innerLeft = x0 + pad;
		span.innerRight = span.innerLeft + inner;
		span.textLeft = span.innerRight + innerGap;
	}
}

/** The card's words fitted to its plan: a tall card's name cut with an ellipsis only, its general
	* and its third line shortened by their language's rule; a one-line card's general gives way
	* first, down to two fifths of the line, and the name then takes what is left. */
void LoadScreenBoard::fitCard( BoardCard &card )
{
	const LoadCardPlan &plan = m_plan[ card.column ];
	BoardCardSpan span;
	cardSpan( card, span );
	const Int room = max( 0, span.textRight - span.textLeft );
	GameFont *nameFont = plan.mode == LOAD_CARD_SMALL_LINE ? m_lineNameFont : m_nameFont;
	card.name->setFont( nameFont );
	card.name->setText( card.nameWords );
	const Int badgeRoom = card.badge ? stringHeight( card.name ) * 3 / 4 + scaled( 10 ) : 0;
	if( plan.mode <= LOAD_CARD_SHORT )
	{
		card.name->setText( UIStyle::fitName( card.nameWords, nameFont, room - badgeRoom ) );
		card.army->setText( UIStyle::fitText( card.armyWords, m_armyFont, room ) );
		card.detail->setText( UIStyle::fitText( card.detailWords, m_detailFont, room ) );
		return;
	}
	card.army->setText( card.armyWords );
	const Int avail = room - badgeRoom - stringWidth( m_lineSeparator );
	if( stringWidth( card.name ) + stringWidth( card.army ) <= avail )
		return;
	card.army->setText( UIStyle::fitText( card.armyWords, m_armyFont, max( avail - stringWidth( card.name ), avail * 2 / 5 ) ) );
	card.name->setText( UIStyle::fitName( card.nameWords, nameFont, avail - stringWidth( card.army ) ) );
}

/** The header's and the foot's words, the same on every board: the title cut before it would reach
	* the subtitle, the subtitle shortened before it would reach the wordmark; a tip panel over the
	* button row with the tips that fit it in two lines. */
void LoadScreenBoard::prepare( const UnicodeString &titleInCapitals, const UnicodeString &subtitle, Bool titleIsName )
{
	GameFont *wordmarkFont = UIStyle::font( UIStyle::TEXT_STRONG, TRUE );
	GameFont *tipFont = UIStyle::readableFont( UIStyle::TEXT_BODY );
	GameFont *titleFont = UIStyle::titleFont( UIStyle::TEXT_TITLE );
	GameFont *subtitleFont = UIStyle::subtitleFont();
	m_title = text( titleInCapitals, titleFont );
	m_subtitle = text( subtitle, subtitleFont );
	m_wordmark[ 0 ] = text( UnicodeString( u"Zero Hour" ), wordmarkFont );
	m_wordmark[ 1 ] = text( UnicodeString( u" Reforged" ), wordmarkFont );

	const Int left = m_left + scaled( UIStyle::ROOM_INSET );
	const Int right = m_left + scaled( UIStyle::ROOM_PANEL_RIGHT + UIStyle::ROOM_PANEL_LEFT - UIStyle::ROOM_INSET );
	const Int between = UIStyle::headerGap();
	Int room = right - ( m_left + scaled( UIStyle::ROOM_TITLE_LEFT ) );
	if( !m_mission )
		room -= stringWidth( m_wordmark[ 0 ] ) + stringWidth( m_wordmark[ 1 ] ) + scaled( 32 );
	if( stringWidth( m_title ) + between + stringWidth( m_subtitle ) > room )
	{
		const Int titleRoom = max( room - between - min( stringWidth( m_subtitle ), room * 2 / 5 ), room / 2 );
		if( stringWidth( m_title ) > titleRoom )
			m_title->setText( titleIsName ? UIStyle::fitName( titleInCapitals, titleFont, titleRoom )
				: UIStyle::fitText( titleInCapitals, titleFont, titleRoom ) );
		m_subtitle->setText( UIStyle::fitText( subtitle, subtitleFont, room - between - stringWidth( m_title ) ) );
	}

	// the foot: a panel of two lines over the button row, the TIP chip in it and a tip beside the
	// chip; a tip needs no shortening, so one that wants a third line here is never shown
	m_tipLabel = text( TheGameText->fetch( "GUI:LoadTip" ), m_smallFont );
	m_tip = text( UnicodeString( u"X" ), tipFont );
	const Int tipLine = stringHeight( m_tip );
	const Int panelHeight = LoadScreen_tipPanelHeight( tipLine, m_scale );
	m_tipBox.lo.x = left;
	m_tipBox.hi.x = right;
	m_tipBox.hi.y = m_top + scaled( UIStyle::ROOM_BUTTON_TOP ) - scaled( LOAD_FOOT_GAP );
	m_tipBox.lo.y = m_tipBox.hi.y - panelHeight;
	const Int pad = scaled( LOAD_TIP_PAD );
	m_tipTextLeft = left + pad + stringWidth( m_tipLabel ) + 2 * scaled( 8 ) + scaled( 16 );
	m_tip->setWordWrap( right - pad - m_tipTextLeft );
	m_tips.clear();
	for( Int number = 1; ; number++ )
	{
		Bool exists = FALSE;
		const UnicodeString words = tipText( number, &exists );
		if( !exists )
			break;
		m_tip->setText( words );
		if( stringHeight( m_tip ) < 2 * tipLine + tipLine / 2 )
			m_tips.push_back( number );
	}
	// one at random to start, the next every eight seconds; the clock is the wall's, never the
	// logic's dice
	m_tipAt = m_tips.empty() ? 0 : (Int)( Clock_Milliseconds() % m_tips.size() );
	m_tipSince = Clock_Milliseconds();
	m_tip->setText( m_tips.empty() ? UnicodeString::TheEmptyString : tipText( m_tips[ m_tipAt ] ) );

	// lettered as a lobby button's label
	m_progressFormat = TheGameText->fetch( "GUI:LoadProgress" );
	m_percent = text( UnicodeString::TheEmptyString, UIStyle::titleFont( UIStyle::TEXT_HEADING ) );
	m_waiting = text( UIStyle::capitals( TheGameText->fetch( "GUI:LoadWaiting" ) ), UIStyle::titleFont( UIStyle::TEXT_HEADING ) );
	m_localPercent = 0;
	setPercentLabel( 0 );
}

/** "LOADING 81%", in the language's own order: the string file's format says where the number goes. */
void LoadScreenBoard::setPercentLabel( Int percent )
{
	UnicodeString words;
	words.format( m_progressFormat, min( max( percent, 0 ), 100 ) );
	m_percent->setText( UIStyle::capitals( words ) );
}

GameWindow *LoadScreenBoard::makeWindow( GameWindow *parent )
{
	GameWindow *window = TheWindowManager->winCreate( parent, WIN_STATUS_ENABLED, 0, 0, m_width, m_height, GameWinDefaultSystem );
	window->winSetUserData( this );
	window->winSetDrawFunc( drawWindow );
	window->winBringToTop();
	return window;
}

/** A mission keeps its own picture behind; the header names it, a panel on the left carries the
	* briefing the mission's load always had and never showed, and the foot is a match's. */
GameWindow *LoadScreenBoard::createMission( GameWindow *parent, const UnicodeString &title, const UnicodeString &subtitle,
	const UnicodeString *briefing, Int briefingLines )
{
	m_mission = TRUE;
	prepare( UIStyle::capitals( title ), subtitle, FALSE );

	const Int pad = scaled( 28 );
	const Int width = scaled( BOARD_COLUMN_WIDTH );
	m_briefingBox.lo.x = m_left + scaled( UIStyle::ROOM_INSET );
	m_briefingBox.lo.y = m_top + scaled( LOAD_BAND_TOP );
	m_briefingBox.hi.x = m_briefingBox.lo.x + width;
	m_briefingHead = text( UIStyle::fitKey( "GUI:LoadBriefing", m_smallFont, width - 2 * pad ), m_smallFont );
	Int bottom = m_briefingBox.lo.y + pad + stringHeight( m_briefingHead ) + scaled( 8 );
	GameFont *lineFont = UIStyle::font( UIStyle::TEXT_STRONG );
	for( Int line = 0; line < briefingLines; line++ )
	{
		if( briefing[ line ].isEmpty() )
			continue;
		DisplayString *words = text( briefing[ line ], lineFont );
		words->setWordWrap( width - 2 * pad );
		m_briefing.push_back( words );
		bottom += scaled( 10 ) + stringHeight( words );
	}
	m_briefingBox.hi.y = bottom + pad;
	return makeWindow( parent );
}

/** An online player's rank beside his name, which gives the badge its room. */
void LoadScreenBoard::setBadge( Int slot, const Image *badge )
{
	BoardCard *card = cardFor( slot );
	if( card == NULL )
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
	card->percentText->setText( playerPercent( percent ) );
}

void LoadScreenBoard::setLocalProgress( Int percent )
{
	if( percent == m_localPercent )
		return;
	m_localPercent = percent;
	setPercentLabel( percent );
}

/** A seat coming back plays the match up to the room's frame behind the board: the wide button fills with
	* the frames played against the frames to play, or reads that the match's log is still on its way. */
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
	m_percent->setText( UIStyle::capitals( words ) );
}

void LoadScreenBoard::drawWindow( GameWindow *window, WinInstanceData *instData )
{
	( (LoadScreenBoard *)window->winGetUserData() )->draw();
}

void LoadScreenBoard::drawCard( const BoardCard &card )
{
	const Int x0 = card.box.lo.x;
	const Int y0 = card.box.lo.y;
	const Int width = card.box.hi.x - x0;
	const Int height = card.box.hi.y - y0;
	// a lobby seat's row: the panel's black, its blue line, the player's colour down the outer edge
	UIStyle::panel( x0, y0, width, height, UIStyle::PANEL );
	const Int stripe = UIStyle::stripeWidth();
	UIStyle::stripe( card.mirrored ? x0 + width - stripe : x0, y0, height, card.color );

	const LoadCardPlan &plan = m_plan[ card.column ];
	const Bool tall = plan.mode <= LOAD_CARD_SHORT;
	BoardCardSpan span;
	cardSpan( card, span );

	// the portrait in a frame of the player's colour; a one-line card has the side's crest at the
	// line's height instead
	const Int emblem = plan.emblem;
	const Int emblemX = span.emblemX;
	const Int emblemY = y0 + plan.emblemTop;
	if( tall )
	{
		TheDisplay->drawFillRect( emblemX, emblemY, emblem, emblem, UIStyle::GROUND );
		if( card.emblem )
			TheDisplay->drawImage( card.emblem, emblemX + 2, emblemY + 2, emblemX + emblem - 2, emblemY + emblem - 2 );
		else
			m_unknown->draw( emblemX + ( emblem - stringWidth( m_unknown ) ) / 2, emblemY + ( emblem - stringHeight( m_unknown ) ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
		TheDisplay->drawOpenRect( emblemX, emblemY, emblem, emblem, 1.0f, UIStyle::withAlpha( card.color, 200 ) );
	}
	else if( card.crest )
		TheDisplay->drawImage( card.crest, emblemX, emblemY, emblemX + emblem, emblemY + emblem );
	else
		m_unknownSmall->draw( emblemX + ( emblem - stringWidth( m_unknownSmall ) ) / 2, emblemY + ( emblem - stringHeight( m_unknownSmall ) ) / 2, UIStyle::MUTED, UIStyle::SHADOW );

	// the words, on the portrait's far side, aligned toward it; the rank badge after the name
	const Int nameWidth = stringWidth( card.name );
	const Int badgeSize = stringHeight( card.name ) * 3 / 4;
	const Int badgeRoom = card.badge ? scaled( 10 ) + badgeSize : 0;
	if( tall )
	{
		const Int nameX = card.mirrored ? span.textRight - nameWidth : span.textLeft;
		card.name->draw( nameX, y0 + plan.nameTop, card.nameColor, UIStyle::SHADOW );
		if( card.badge )
		{
			const Int bx = card.mirrored ? nameX - scaled( 10 ) - badgeSize : nameX + nameWidth + scaled( 10 );
			const Int by = y0 + plan.nameTop + ( stringHeight( card.name ) - badgeSize ) / 2;
			TheDisplay->drawImage( card.badge, bx, by, bx + badgeSize, by + badgeSize );
		}
		const Int armyWidth = stringWidth( card.army );
		card.army->draw( card.mirrored ? span.textRight - armyWidth : span.textLeft, y0 + plan.armyTop, UIStyle::INK, UIStyle::SHADOW );
		if( plan.detailTop >= 0 && card.detail->getTextLength() > 0 )
		{
			const Int detailWidth = stringWidth( card.detail );
			card.detail->draw( card.mirrored ? span.textRight - detailWidth : span.textLeft, y0 + plan.detailTop, UIStyle::MUTED, UIStyle::SHADOW );
		}
	}
	else
	{
		// "name · general", read left to right in either column, against the crest
		const Int total = nameWidth + badgeRoom + stringWidth( m_lineSeparator ) + stringWidth( card.army );
		Int x = card.mirrored ? span.textRight - total : span.textLeft;
		card.name->draw( x, y0 + plan.nameTop, card.nameColor, UIStyle::SHADOW );
		x += nameWidth;
		if( card.badge )
		{
			const Int by = y0 + plan.nameTop + ( stringHeight( card.name ) - badgeSize ) / 2;
			TheDisplay->drawImage( card.badge, x + scaled( 10 ), by, x + scaled( 10 ) + badgeSize, by + badgeSize );
			x += badgeRoom;
		}
		m_lineSeparator->draw( x, y0 + plan.armyTop, UIStyle::MUTED, UIStyle::SHADOW );
		x += stringWidth( m_lineSeparator );
		card.army->draw( x, y0 + plan.armyTop, UIStyle::INK, UIStyle::SHADOW );
	}

	// the start spot the map shows for him, in the inner top corner, or at the line's inner end
	const Int discRoom = card.disc ? plan.disc + scaled( 8 ) : 0;
	if( card.disc )
		drawStartSpot( card.mirrored ? span.innerLeft : span.innerRight - plan.disc, y0 + plan.discTop, plan.disc, card.color, card.disc );

	// a network player's own load: a bar under the words, the percentage in the inner column beside
	// it, or on a one-line card beside the disc; a column too full for bars keeps the percentage
	if( card.percent >= 0 )
	{
		if( plan.barTop >= 0 )
		{
			const Int barLeft = span.textLeft;
			const Int barRight = span.textRight;
			const Int barY = y0 + plan.barTop;
			TheDisplay->drawFillRect( barLeft, barY, barRight - barLeft, plan.barHeight, UIStyle::GRID );
			const Int filled = ( barRight - barLeft ) * min( card.percent, 100 ) / 100;
			TheDisplay->drawFillRect( card.mirrored ? barRight - filled : barLeft, barY, filled, plan.barHeight, card.color );
		}
		const Int percentWidth = stringWidth( card.percentText );
		Int percentX;
		if( tall )
			percentX = card.mirrored ? span.innerLeft : span.innerRight - percentWidth;
		else
			percentX = card.mirrored ? span.innerLeft + discRoom : span.innerRight - discRoom - percentWidth;
		card.percentText->draw( percentX, y0 + plan.percentTop, card.percent >= 100 ? UIStyle::INK : UIStyle::MUTED, UIStyle::SHADOW );
	}
}

/** The foot's panel: the TIP chip, the tip beside it on up to two lines. */
void LoadScreenBoard::drawTip( void )
{
	const Int width = m_tipBox.hi.x - m_tipBox.lo.x;
	const Int height = m_tipBox.hi.y - m_tipBox.lo.y;
	UIStyle::panel( m_tipBox.lo.x, m_tipBox.lo.y, width, height, UIStyle::PANEL );
	const Int chipPad = scaled( 8 );
	const Int chipHeight = stringHeight( m_tipLabel ) + scaled( 4 );
	const Int chipX = m_tipBox.lo.x + scaled( LOAD_TIP_PAD );
	const Int chipY = m_tipBox.lo.y + ( height - chipHeight ) / 2;
	TheDisplay->drawOpenRect( chipX, chipY, stringWidth( m_tipLabel ) + 2 * chipPad, chipHeight, 1.0f, UIStyle::FRAME );
	m_tipLabel->draw( chipX + chipPad, chipY + scaled( 2 ), UIStyle::ACTIVE, UIStyle::SHADOW );
	if( m_tip->getTextLength() > 0 )
		m_tip->draw( m_tipTextLeft, m_tipBox.lo.y + ( height - stringHeight( m_tip ) ) / 2, UIStyle::INK, UIStyle::SHADOW );
}

void LoadScreenBoard::draw( void )
{
	if( m_tips.size() > 1 && Clock_Milliseconds() - m_tipSince > BOARD_TIP_MILLISECONDS )
	{
		m_tipAt = ( m_tipAt + 1 ) % (Int)m_tips.size();
		m_tipSince = Clock_Milliseconds();
		m_tip->setText( tipText( m_tips[ m_tipAt ] ) );
	}

	const Int panelLeft = m_left + scaled( UIStyle::ROOM_PANEL_LEFT );
	const Int panelTop = m_top + scaled( UIStyle::ROOM_PANEL_TOP );
	const Int panelWidth = scaled( UIStyle::ROOM_PANEL_RIGHT - UIStyle::ROOM_PANEL_LEFT );
	const Int panelBottom = m_top + scaled( UIStyle::ROOM_PANEL_BOTTOM );
	const Int headerLine = m_top + scaled( UIStyle::ROOM_HEADER_LINE );
	const Int left = m_left + scaled( UIStyle::ROOM_INSET );
	const Int right = m_left + scaled( UIStyle::ROOM_PANEL_RIGHT + UIStyle::ROOM_PANEL_LEFT - UIStyle::ROOM_INSET );
	const Int buttonTop = m_top + scaled( UIStyle::ROOM_BUTTON_TOP );
	const Int buttonHeight = scaled( UIStyle::ROOM_BUTTON_BOTTOM - UIStyle::ROOM_BUTTON_TOP );
	if( m_mission )
	{
		// the mission's own picture stays; the lobby's panel frames only its header and its foot
		const Int footTop = m_tipBox.lo.y - scaled( LOAD_FOOT_GAP );
		UIStyle::panel( panelLeft, panelTop, panelWidth, headerLine - panelTop, UIStyle::PANEL );
		UIStyle::panel( panelLeft, footTop, panelWidth, panelBottom - footTop, UIStyle::PANEL );
		drawBriefing();
	}
	else
	{
		// the shell's still, dimmed as the lobby dims its map, under the lobby's panel
		UIStyle::shell();
		UIStyle::panel( panelLeft, panelTop, panelWidth, panelBottom - panelTop, UIStyle::PANEL );
		drawMatch();
	}

	// the header: the title where the lobby's stands, what kind of game after it, the wordmark across
	// (the score screen's header, drawn by the same call)
	const Int titleMiddle = m_top + scaled( UIStyle::ROOM_TITLE_MIDDLE );
	UIStyle::header( m_title, m_subtitle, m_left + scaled( UIStyle::ROOM_TITLE_LEFT ), titleMiddle );
	// a mission's picture is the game's own logo art, and the wordmark never stands beside EA's logo
	if( !m_mission )
		UIStyle::wordmark( m_wordmark, right, titleMiddle - stringHeight( m_wordmark[ 0 ] ) / 2 );
	UIStyle::rule( panelLeft, headerLine, panelWidth );

	// the foot: the tip's panel over the lobby's button row, whose one wide button fills as the game
	// loads
	drawTip();
	UIStyle::button( left, buttonTop, right - left, buttonHeight );
	const Int edge = 3;
	const Int filled = ( right - left - 2 * edge ) * min( m_localPercent, 100 ) / 100;
	TheDisplay->drawFillRect( left + edge, buttonTop + edge, filled, buttonHeight - 2 * edge, UIStyle::withAlpha( UIStyle::EDGE, 110 ) );
	DisplayString *label = m_network && m_localPercent > 100 ? m_waiting : m_percent;
	label->draw( ( left + right - stringWidth( label ) ) / 2, buttonTop + ( buttonHeight - stringHeight( label ) ) / 2,
		UIStyle::INK, UIStyle::SHADOW );
}

void LoadScreenBoard::drawBriefing( void )
{
	if( m_briefing.empty() )
		return;
	const IRegion2D &box = m_briefingBox;
	const Int width = box.hi.x - box.lo.x;
	const Int height = box.hi.y - box.lo.y;
	const Int pad = scaled( 28 );
	UIStyle::panel( box.lo.x, box.lo.y, width, height, UIStyle::PANEL );
	Int y = box.lo.y + pad;
	m_briefingHead->draw( box.lo.x + pad, y, UIStyle::ACTIVE, UIStyle::SHADOW );
	y += stringHeight( m_briefingHead ) + scaled( 8 );
	for( size_t each = 0; each < m_briefing.size(); each++ )
	{
		y += scaled( 10 );
		m_briefing[ each ]->draw( box.lo.x + pad, y, UIStyle::INK, UIStyle::SHADOW );
		y += stringHeight( m_briefing[ each ] );
	}
}

void LoadScreenBoard::drawMatch( void )
{
	// a team's band over its cards: the room's team swatch at the outer edge and its name, or where the
	// column is crowded a rule in the team's colour
	for( size_t each = 0; each < m_bands.size(); each++ )
	{
		const BoardBand &band = m_bands[ each ];
		const Int width = band.box.hi.x - band.box.lo.x;
		const Int height = band.box.hi.y - band.box.lo.y;
		if( band.label == NULL )
		{
			const Int rule = max( 2, scaled( 3 ) );
			TheDisplay->drawFillRect( band.box.lo.x, band.box.lo.y + ( height - rule ) / 2, width, rule, band.color );
			continue;
		}
		const Int swatch = stringHeight( band.label ) + 2;
		const Int middle = band.box.lo.y + height / 2;
		const Int swatchX = band.mirrored ? band.box.hi.x - swatch : band.box.lo.x;
		LobbySwatchFill( swatchX, middle - swatch / 2, swatch, swatch, band.color );
		const Int labelX = band.mirrored ? swatchX - scaled( 10 ) - stringWidth( band.label ) : swatchX + swatch + scaled( 10 );
		band.label->draw( labelX, middle - stringHeight( band.label ) / 2, UIStyle::ACTIVE, UIStyle::SHADOW );
	}

	for( size_t each = 0; each < m_cards.size(); each++ )
		drawCard( m_cards[ each ] );

	// the middle
	if( m_centerLabel )
		m_centerLabel->draw( m_centerLabelPos.x, m_centerLabelPos.y, UIStyle::INK, UIStyle::SHADOW );
	const Int mapWidth = m_mapBox.hi.x - m_mapBox.lo.x;
	const Int mapHeight = m_mapBox.hi.y - m_mapBox.lo.y;
	TheDisplay->drawFillRect( m_mapBox.lo.x, m_mapBox.lo.y, mapWidth, mapHeight, UIStyle::GROUND );
	if( m_mapImage )
		TheDisplay->drawImage( m_mapImage, m_mapDrawn.lo.x, m_mapDrawn.lo.y, m_mapDrawn.hi.x, m_mapDrawn.hi.y );
	// the lobby's map preview wears a button's edge, here round the map's own shape
	UIStyle::edge( m_mapBox.lo.x - 3, m_mapBox.lo.y - 3, mapWidth + 6, mapHeight + 6 );
	const Int spot = m_spotSize;
	for( size_t each = 0; each < m_spots.size(); each++ )
	{
		const BoardSpot &mark = m_spots[ each ];
		drawStartSpot( mark.pos.x - spot / 2, mark.pos.y - spot / 2, spot, mark.color, mark.number );
	}

	// the facts in the lobby's rules box: the label on the left, what it was set to on the right
	const Int factsWidth = m_factsBox.hi.x - m_factsBox.lo.x;
	UIStyle::panel( m_factsBox.lo.x, m_factsBox.lo.y, factsWidth, m_factsBox.hi.y - m_factsBox.lo.y, UIStyle::PANEL );
	const Int factPad = scaled( 10 );
	for( size_t each = 0; each < m_facts.size(); each++ )
	{
		const BoardFact &fact = m_facts[ each ];
		const Int rowMiddle = m_factsBox.lo.y + factPad + (Int)each * m_factPitch + m_factPitch / 2;
		fact.label->draw( m_factsBox.lo.x + factPad, rowMiddle - stringHeight( fact.label ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
		fact.value->draw( m_factsBox.hi.x - factPad - stringWidth( fact.value ), rowMiddle - stringHeight( fact.value ) / 2, UIStyle::INK, UIStyle::SHADOW );
	}
	if( m_observers )
		m_observers->draw( m_observersPos.x, m_observersPos.y, UIStyle::MUTED, UIStyle::SHADOW );
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
		// Zero Hour's missions name no place, so the number is the title there
		static const char *const difficultyLabels[ DIFFICULTY_COUNT ] = { "GUI:Easy", "GUI:Medium", "GUI:Hard" };
		const Mission *mission = TheCampaignManager->getCurrentMission();
		UnicodeString number;
		number.format( TheGameText->fetch( "GUI:LoadMission" ), TheCampaignManager->getCurrentMissionNumber() + 1 );
		UnicodeString title = number;
		UnicodeString subtitle = TheGameText->fetch( TheCampaignManager->getCurrentCampaign()->m_campaignNameLabel );
		if( !mission->m_locationNameLabel.isEmpty() )
		{
			title = TheGameText->fetch( mission->m_locationNameLabel );
			subtitle.concat( UIStyle::HEADER_JOINER );
			subtitle.concat( number );
		}
		subtitle.concat( UIStyle::HEADER_JOINER );
		subtitle.concat( TheGameText->fetch( difficultyLabels[ TheCampaignManager->getGameDifficulty() ] ) );
		m_board = NEW LoadScreenBoard;
		m_board->createMission( m_loadScreen, title, subtitle, m_unicodeObjectiveLines, MAX_OBJECTIVE_LINES );
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
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_oldTimeout = 0;
	//
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
}
	
MapTransferLoadScreen::~MapTransferLoadScreen( void )
{
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
}

void MapTransferLoadScreen::init( GameInfo *game )
{
	// create the layout of the load screen
	m_loadScreen = TheWindowManager->winCreateFromScript( AsciiString( "Menus/MapTransferScreen.wnd" ) );
	DEBUG_ASSERTCRASH(m_loadScreen, ("Can't initialize the map transfer loadscreen"));
	if (!m_loadScreen)
		return;

	m_loadScreen->winHide(FALSE);
	m_loadScreen->winBringToTop();

	DEBUG_ASSERTCRASH(TheNetwork, ("Where the Heck is the Network?!!!!"));
	DEBUG_LOG(("NumPlayers %d\n", TheNetwork->getNumPlayers()));

	AsciiString winName;
	Int i;

	// Load the Filename Text
	winName.format( "MapTransferScreen.wnd:StaticTextCurrentFile");
	m_fileNameText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
	DEBUG_ASSERTCRASH(m_fileNameText, ("Can't initialize the filename for the map transfer loadscreen"));

	// Load the Timeout Text
	winName.format( "MapTransferScreen.wnd:StaticTextTimeout");
	m_timeoutText = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
	DEBUG_ASSERTCRASH(m_timeoutText, ("Can't initialize the timeout for the map transfer loadscreen"));

	Int netSlot = 0;
	// Loop through and make the loadscreen look all good.
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		// Load the Progress Bar
		winName.format( "MapTransferScreen.wnd:ProgressLoad%d",i);
		m_progressBars[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressBars[i], ("Can't initialize the progressbars for the map transfer loadscreen"));
		// set the progressbar to zero
		GadgetProgressBarSetProgress(m_progressBars[i], 0 );

		// Load the Player's name
		winName.format( "MapTransferScreen.wnd:StaticTextPlayer%d",i);
		m_playerNames[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_playerNames[i], ("Can't initialize the Names for the map transfer loadscreen"));

		// Load the Progress Text
		winName.format( "MapTransferScreen.wnd:StaticTextProgress%d",i);
		m_progressText[i] = TheWindowManager->winGetWindowFromId( m_loadScreen,TheNameKeyGenerator->nameToKey( winName ));
		DEBUG_ASSERTCRASH(m_progressText[i], ("Can't initialize the progress text for the map transfer loadscreen"));

		// get the slot man!
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isHuman())
			continue;
		if (!m_progressBars[netSlot] || !m_playerNames[netSlot] || !m_progressText[netSlot])
			continue;	// the layout's rows are used up; this player's transfer is not shown
		Color houseColor = TheMultiplayerSettings->getColor(slot->getApparentColor())->getColor();
		GadgetProgressBarSetEnabledBarColor(m_progressBars[netSlot], houseColor );

		UnicodeString name = slot->getName();
		GadgetStaticTextSetText(m_playerNames[netSlot], name );
		m_playerNames[netSlot]->winSetEnabledTextColors(houseColor, m_playerNames[netSlot]->winGetEnabledTextBorderColor());

		GadgetStaticTextSetText(m_progressText[netSlot], UnicodeString::TheEmptyString );
		m_progressText[netSlot]->winSetEnabledTextColors(houseColor, m_progressText[netSlot]->winGetEnabledTextBorderColor());

		if ((i == 0 || (TheGameInfo->getConstSlot(i)->isHuman() && TheGameInfo->getConstSlot(i)->hasMap())) && m_progressBars[netSlot])
			m_progressBars[netSlot]->winHide(TRUE);

		m_playerLookup[i] = netSlot; // save our mapping so we can update progress correctly

		netSlot++;
	}
	
	for(i = netSlot; i < MAX_SLOTS; ++i)
	{
		if (!m_progressBars[i] || !m_playerNames[i] || !m_progressText[i])
			continue;
		m_progressBars[i]->winHide(TRUE);
		m_playerNames[i]->winHide(TRUE);
		m_progressText[i]->winHide(TRUE);
	}
}

void MapTransferLoadScreen::reset( void )
{
	setLoadScreen(NULL);
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressBars[i] = NULL;
		m_playerNames[i] = NULL;
		m_progressText[i]= NULL;
		m_playerLookup[i] = -1;
		m_oldProgress[i] = -1;
	}
	m_fileNameText = NULL;
	m_timeoutText = NULL;
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
	if( m_playerLookup[playerId] == -1)
		return;

	if (m_oldProgress[playerId] == percentage)
		return;
	m_oldProgress[playerId] = percentage;

	Int translatedSlot = m_playerLookup[playerId];
	if(m_progressBars[translatedSlot])
		GadgetProgressBarSetProgress(m_progressBars[translatedSlot], percentage );	
	if (m_progressText[translatedSlot])
		GadgetStaticTextSetText(m_progressText[translatedSlot], TheGameText->fetch(stateStr));
}

void MapTransferLoadScreen::processTimeout(Int secondsLeft)
{
	if (m_oldTimeout == secondsLeft)
		return;
	m_oldTimeout = secondsLeft;

	if (m_timeoutText)
	{
		UnicodeString txt;
		txt.format(TheGameText->fetch("MapTransfer:Timeout"), (secondsLeft/60), (secondsLeft%60));
		GadgetStaticTextSetText(m_timeoutText, txt);
	}
}

void MapTransferLoadScreen::setCurrentFilename(AsciiString filename)
{
	if (m_fileNameText)
	{
		UnicodeString txt;
		txt.translate(TheGameState->getMapLeafName(filename));
		txt.format(TheGameText->fetch("MapTransfer:CurrentFile"), txt.str());
		GadgetStaticTextSetText(m_fileNameText, txt);
	}
}

