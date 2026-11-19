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
// FILE: WOLQuickMatchMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include <random>

#include "Common/GameEngine.h"
#include "Common/QuickmatchPreferences.h"
#include "Common/LadderPreferences.h"
#include "Common/MultiplayerSettings.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomMapGenerator.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Image.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/UIStyle.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"

#include "GameLogic/GameLogic.h"

#include "GameNetwork/GameInfo.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#ifdef DEBUG_LOGGING
#include "Common/MiniLog.h"
//#define PERF_TEST
static LogClass s_perfLog("QMPerf.txt");
static Bool s_inQM = FALSE;
#define PERF_LOG(x) s_perfLog.log x
#else // DEBUG_LOGGING
#define PERF_LOG(x) {}
#endif // DEBUG_LOGGING

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLQuickMatchID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonStopID = NAMEKEY_INVALID;
static NameKeyType buttonWidenID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
static NameKeyType listboxQuickMatchID = NAMEKEY_INVALID;
static NameKeyType listboxMapSelectID = NAMEKEY_INVALID;
static NameKeyType buttonSelectAllMapsID = NAMEKEY_INVALID;
static NameKeyType buttonSelectNoMapsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMaxDisconnectsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMaxPointsID = NAMEKEY_INVALID;
//static NameKeyType textEntryMinPointsID = NAMEKEY_INVALID;
static NameKeyType textEntryWaitTimeID = NAMEKEY_INVALID;
static NameKeyType comboBoxNumPlayersID = NAMEKEY_INVALID;
static NameKeyType comboBoxMaxPingID = NAMEKEY_INVALID;
static NameKeyType comboBoxLadderID = NAMEKEY_INVALID;
static NameKeyType comboBoxMaxDisconnectsID = NAMEKEY_INVALID;
static NameKeyType staticTextNumPlayersID = NAMEKEY_INVALID;
static NameKeyType comboBoxSideID = NAMEKEY_INVALID;
static NameKeyType comboBoxColorID = NAMEKEY_INVALID;


// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLQuickMatch = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonStart = NULL;
static GameWindow *buttonStop = NULL;
static GameWindow *buttonWiden = NULL;
GameWindow *quickmatchTextWindow = NULL;
static GameWindow *listboxMapSelect = NULL;
//static GameWindow *textEntryMaxDisconnects = NULL;
//static GameWindow *textEntryMaxPoints = NULL;
//static GameWindow *textEntryMinPoints = NULL;
static GameWindow *textEntryWaitTime = NULL;
static GameWindow *comboBoxNumPlayers = NULL;
static GameWindow *comboBoxMaxPing = NULL;
static GameWindow *comboBoxLadder = NULL;
static GameWindow *comboBoxDisabledLadder = NULL; // enable and disable this, but never use it.  it is a stand-in for comboBoxLadder for when there are no ladders
static GameWindow *comboBoxMaxDisconnects = NULL;
static GameWindow *staticTextNumPlayers = NULL;
static GameWindow *comboBoxSide = NULL;
static GameWindow *comboBoxColor = NULL;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;		///< pushed over the hub once this page has popped; NULL for the hub itself
static Bool raiseMessageBoxes = false;
static Bool isInInit = FALSE;
static const Image *selectedImage = NULL;
static const Image *unselectedImage = NULL;

static bool isPopulatingLadderBox = false;
static Int maxPingEntries = 0;
static Int maxPoints= 100;
static Int minPoints = 0;

static const LadderInfo * getLadderInfo( void );

//-------------------------------------------------------------------------------------------------
// The search and the reveal, which decide what the page shows (qmPageState)
//-------------------------------------------------------------------------------------------------
static UnsignedInt s_searchSince = 0;		///< Clock_Milliseconds when Start was pressed; 0 when not searching
static Bool s_stopPending = FALSE;			///< Stop or a leave went out and the server has not said stopped or refused yet
static Int s_poolSize = -1;							///< the last qm.status pool, -1 before one
static Bool s_revealShown = FALSE;			///< an opponent was announced and game.start has not come
static UnsignedInt s_revealSerial = 0;
static UnsignedInt s_revealStarted = 0;	///< when the reveal was shown
static UnsignedInt s_revealEnds = 0;		///< when the server's game.start is due
static OnlineMatchReveal s_reveal;			///< the last reveal shown, kept past game.start for the map check
// the -online map check before a Quick Match starts, qmMapCheckTick
static Bool s_mapCheckPending = FALSE;
static UnsignedInt s_mapCheckUntil = 0;
static Int s_mapCheckSeed = 0;
static std::map<AsciiString, std::pair<Int, UnsignedInt> > s_peerMapCRC;	///< by lowercase nick: seed and CRC it sent
static const UnsignedInt QM_MAP_CHECK_MS = 20000;

static void onlineSearchStopped( void )
{
	s_searchSince = 0;
	s_revealShown = FALSE;
}

static void forgetReveal( void )
{
	s_reveal.opponents.clear();
	s_reveal.hasChance = FALSE;
}

/// the server's abandon lock: Search stays disabled and the button row counts it down
static Int qmLockSeconds( void )
{
	return OnlineIsEnabled() ? OnlineGetQuickMatchLockSeconds() : 0;
}

static void onlineQuickMatchTick( void )
{
	// each reveal is shown once: one already shown and then stopped or cancelled is not shown again
	OnlineMatchReveal reveal;
	if (OnlineGetMatchReveal( reveal ) && reveal.serial != s_revealSerial)
	{
		const UnsignedInt now = Clock_Milliseconds();
		s_revealShown = TRUE;
		s_revealSerial = reveal.serial;
		s_revealStarted = now;
		s_revealEnds = now + (UnsignedInt)max( 0, reveal.revealMs );
		s_reveal = reveal;
		DEBUG_LOG(("ONLINE REVEAL shown opponents=%d\n", (Int)reveal.opponents.size()));
	}
}

/// the options are the picks: open between searches, locked from Start until Stop or a failure
static void enableOptionsGadgets(Bool doIt)
{
#ifdef PERF_TEST
	s_inQM = !doIt;
#endif // PERF_TEST
	if (doIt)
	{
		// the server's stopped or refusal, or a match called off; Stop sets s_stopPending again after this
		onlineSearchStopped();
		s_stopPending = FALSE;
	}
	else
	{
		s_searchSince = max( 1u, Clock_Milliseconds() );
		s_poolSize = -1;
		s_stopPending = FALSE;
		forgetReveal();
	}
}

//-------------------------------------------------------------------------------------------------
// The page, drawn in code in the master's boxes (ONLINE-UI-REDESIGN.md, "Quick Match"): the picks,
// the search, the opponent found.  Lengths are given for 1920x1080 and go through UIStyle::px, as the
// master's windows are fitted.
//-------------------------------------------------------------------------------------------------
enum QMPageState { QM_PAGE_PICK, QM_PAGE_SEARCH, QM_PAGE_FOUND, QM_PAGE_NONE };

static QMPageState qmPageState( void )
{
	if (s_revealShown || s_mapCheckPending)
		return QM_PAGE_FOUND;
	return s_searchSince ? QM_PAGE_SEARCH : QM_PAGE_PICK;
}

/// the master's boxes; the side box while picking is PanelMaps, which holds the ladder now
enum QMPanel { QM_PANEL_ARMY, QM_PANEL_COLOR, QM_PANEL_LADDER, QM_PANEL_SEARCH, QM_PANEL_PICKS, QM_PANEL_COUNT };
static const char *const QM_PANEL_NAME[QM_PANEL_COUNT] = { "WOLQuickMatchMenu.wnd:PanelArmy",
	"WOLQuickMatchMenu.wnd:PanelColor", "WOLQuickMatchMenu.wnd:PanelMaps", "WOLQuickMatchMenu.wnd:PanelSearch",
	"WOLQuickMatchMenu.wnd:PanelPicks" };
static const QMPageState QM_PANEL_STATE[QM_PANEL_COUNT] = { QM_PAGE_PICK, QM_PAGE_PICK, QM_PAGE_PICK, QM_PAGE_SEARCH,
	QM_PAGE_SEARCH };
static GameWindow *s_panels[QM_PANEL_COUNT];
static QMPageState s_pageShown = QM_PAGE_NONE;
static const Image *s_randomImage = NULL;		///< the rooms' "?" for a random army or colour

/// the boxes and buttons of the state; the found state draws its cards on the bare room panel
static void showPageState( void )
{
	const QMPageState state = qmPageState();
	if (state == s_pageShown || !parentWOLQuickMatch)
		return;
	s_pageShown = state;
	for (Int each = 0; each < QM_PANEL_COUNT; ++each)
	{
		if (s_panels[each])
			s_panels[each]->winHide( QM_PANEL_STATE[each] != state );
	}
	buttonBack->winHide( state == QM_PAGE_FOUND );
	buttonStart->winHide( state != QM_PAGE_PICK );
	buttonStop->winHide( state != QM_PAGE_SEARCH );
	buttonWiden->winHide( state != QM_PAGE_SEARCH );
}

/** A draw callback's words.  The n-th get of a pass hands out the n-th string and fits it again only
	* when what it is asked to hold changed, so words drawn every frame are fitted once. */
class QMWords
{
public:
	QMWords() : m_next( 0 ) {}
	void begin( void ) { m_next = 0; }
	/// words in font, fitted to widest (0: as they are; name: a nick, cut and never abbreviated) and
	/// wrapped at wrap (0: one line)
	DisplayString *get( GameFont *font, const UnicodeString &words, Int widest = 0, Int wrap = 0, Bool name = FALSE );
	void clear( void );
private:
	struct Entry
	{
		DisplayString *text;
		UnicodeString words;
		GameFont *font;
		Int widest;
		Int wrap;
	};
	std::vector<Entry> m_entries;
	size_t m_next;
};

DisplayString *QMWords::get( GameFont *font, const UnicodeString &words, Int widest, Int wrap, Bool name )
{
	if (m_next == m_entries.size())
	{
		Entry fresh;
		fresh.text = TheDisplayStringManager->newDisplayString();
		fresh.font = NULL;
		fresh.widest = -1;
		fresh.wrap = -1;
		m_entries.push_back( fresh );
	}
	Entry &e = m_entries[m_next++];
	if (e.font != font || e.widest != widest || e.wrap != wrap || e.words.compare( words ) != 0)
	{
		e.font = font;
		e.widest = widest;
		e.wrap = wrap;
		e.words = words;
		e.text->setFont( font );
		e.text->setWordWrap( wrap );
		if (widest <= 0 || font == NULL)
			e.text->setText( words );
		else
			e.text->setText( name ? UIStyle::fitName( words, font, widest ) : UIStyle::fitText( words, font, widest ) );
	}
	return e.text;
}

void QMWords::clear( void )
{
	for (size_t each = 0; each < m_entries.size() && TheDisplayStringManager; ++each)
		TheDisplayStringManager->freeDisplayString( m_entries[each].text );
	m_entries.clear();
	m_next = 0;
}

/// one per box, and the page's own (the last) for the found state
static QMWords s_words[QM_PANEL_COUNT + 1];

enum QMAlign { QM_LEFT, QM_MIDDLE, QM_RIGHT };

/// words with their left edge, middle or right edge at x and their top at y; returns their width
static Int put( DisplayString *words, Int x, Int y, Color color, QMAlign align = QM_LEFT )
{
	Int width, height;
	words->getSize( &width, &height );
	words->draw( x - (align == QM_MIDDLE ? width / 2 : align == QM_RIGHT ? width : 0), y, color, UIStyle::SHADOW );
	return width;
}

static Int widthOf( DisplayString *words )
{
	Int width, height;
	words->getSize( &width, &height );
	return width;
}

static Int heightOf( DisplayString *words )
{
	Int width, height;
	words->getSize( &width, &height );
	return height;
}

static Int qpx( Real at1080 )
{
	return UIStyle::px( at1080 );
}

static const Int QM_CLOCK_POINTS = 46;		///< the Generals face at the spec's 96 px at 1080: the clock and VS
static const Int QM_BUTTON_POINTS = 15;		///< the rooms' buttons
static const WideChar QM_NONE[] = { 0x2013, 0 };	///< an en dash for a number nobody sent

/// the page's faces, found at init: TheFontLibrary walks its whole list on every ask
enum QMFace { QM_FACE_BODY, QM_FACE_BOLD, QM_FACE_SECTION, QM_FACE_HEADING, QM_FACE_NICK, QM_FACE_CHANCE, QM_FACE_DISPLAY,
	QM_FACE_CLOCK, QM_FACE_BUTTON, QM_FACE_COUNT };
static GameFont *s_face[QM_FACE_COUNT];

static void findFaces( void )
{
	s_face[QM_FACE_BODY] = UIStyle::font( UIStyle::TEXT_BODY );
	s_face[QM_FACE_BOLD] = UIStyle::font( UIStyle::TEXT_BODY, TRUE );
	s_face[QM_FACE_SECTION] = UIStyle::sectionFont();
	s_face[QM_FACE_HEADING] = UIStyle::font( UIStyle::TEXT_HEADING );
	s_face[QM_FACE_NICK] = UIStyle::font( UIStyle::TEXT_HEADING, TRUE );
	s_face[QM_FACE_CHANCE] = UIStyle::font( UIStyle::TEXT_STRONG, TRUE );
	s_face[QM_FACE_DISPLAY] = UIStyle::titleFont( UIStyle::TEXT_DISPLAY );
	s_face[QM_FACE_CLOCK] = UIStyle::titleFont( QM_CLOCK_POINTS );
	s_face[QM_FACE_BUTTON] = UIStyle::titleFont( QM_BUTTON_POINTS );
}

static GameFont *bodyFont( Bool bold = FALSE )
{
	return s_face[bold ? QM_FACE_BOLD : QM_FACE_BODY];
}

/** A label's words, fetched once a visit: GameTextManager::fetch copies the label and searches the whole
	* table on every call.  Keyed by the label's address, so only labels that outlive the page come here:
	* literals and the colour definitions' names. */
static std::map<const char *, UnicodeString> s_wordCache;

static const UnicodeString &word( const char *label )
{
	std::map<const char *, UnicodeString>::iterator it = s_wordCache.find( label );
	if (it == s_wordCache.end())
		it = s_wordCache.insert( std::make_pair( label, TheGameText->fetch( label ) ) ).first;
	return it->second;
}

/// the server's name for the search page's queue line, read at init (the welcome has come by then)
static UnicodeString s_serverLabel;

// the server's window of win chances (OnlineQueues.h): 45 to 55 at join, 5 points wider each side
// every 30 s, 30 to 70 at the widest
static const Int QM_WINDOW_STEP = 5;
static const Int QM_WINDOW_STEP_MS = 30000;
static const Int QM_WINDOW_WIDEST_LOW = 30;
static const Int QM_WINDOW_WIDEST_HIGH = 70;

/// the army cards: Random across the top, then a row per side, the side and its three generals
enum { QM_ARMY_COLUMNS = 4, QM_ARMY_ROWS = 3, QM_ARMY_CARDS = 1 + QM_ARMY_COLUMNS * QM_ARMY_ROWS, QM_NO_ARMY = -2 };
static const struct { const char *side; const char *general; } QM_ARMY[QM_ARMY_COLUMNS * QM_ARMY_ROWS] = {
	{ "America", NULL }, { "AmericaSuperWeaponGeneral", "GUI:OnlineGeneralSuper" },
	{ "AmericaLaserGeneral", "GUI:OnlineGeneralLaser" }, { "AmericaAirForceGeneral", "GUI:OnlineGeneralAir" },
	{ "China", NULL }, { "ChinaTankGeneral", "GUI:OnlineGeneralTank" },
	{ "ChinaInfantryGeneral", "GUI:OnlineGeneralInf" }, { "ChinaNukeGeneral", "GUI:OnlineGeneralNuke" },
	{ "GLA", NULL }, { "GLAToxinGeneral", "GUI:OnlineGeneralToxin" },
	{ "GLADemolitionGeneral", "GUI:OnlineGeneralDemo" }, { "GLAStealthGeneral", "GUI:OnlineGeneralStealth" } };
static const char *const QM_SIDE_NAME[QM_ARMY_ROWS] = { "GUI:USA", "GUI:China", "GUI:GLA" };
static Int s_cardTemplate[QM_ARMY_CARDS];		///< each card's PlayerTemplate: PLAYERTEMPLATE_RANDOM first, QM_NO_ARMY not offered
static const Image *s_cardImage[QM_ARMY_CARDS];	///< each card's findArmyImage, found at init
static Int s_joinedArmy = PLAYERTEMPLATE_RANDOM;	///< the army the last qm.join sent, Random already drawn

/// QM_ARMY's entry for a PlayerTemplate, -1 for Random or an army it does not name
static Int armyEntry( Int templateIndex )
{
	const PlayerTemplate *pt = templateIndex >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( templateIndex ) : NULL;
	for (Int each = 0; pt && each < QM_ARMY_COLUMNS * QM_ARMY_ROWS; ++each)
	{
		if (pt->getSide() == QM_ARMY[each].side)
			return each;
	}
	return -1;
}

/// an army in words: "Air Force · USA" when joined, else "Air Force"; "USA", "Random"
static UnicodeString armyName( Int templateIndex, Bool joined )
{
	if (templateIndex < 0)
		return word( "GUI:Random" );
	const Int entry = armyEntry( templateIndex );
	if (entry < 0)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate( templateIndex );
		return pt ? pt->getDisplayName() : word( "GUI:Random" );
	}
	const UnicodeString &side = word( QM_SIDE_NAME[entry / QM_ARMY_COLUMNS] );
	if (QM_ARMY[entry].general == NULL)
		return side;
	UnicodeString name = word( QM_ARMY[entry].general );
	if (joined)
	{
		name.concat( UIStyle::HEADER_JOINER );
		name.concat( side );
	}
	return name;
}

/// an army's picture: its general's logo, else its side's icon, the "?" for Random (an image search each)
static const Image *findArmyImage( Int templateIndex )
{
	const PlayerTemplate *pt = templateIndex >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( templateIndex ) : NULL;
	if (pt == NULL)
		return s_randomImage;
	return pt->getGeneralImage() ? pt->getGeneralImage() : pt->getSideIconImage();
}

/// findArmyImage from the card pictures found at init, searched again only for an army no card has
static const Image *armyImage( Int templateIndex )
{
	for (Int card = 0; card < QM_ARMY_CARDS; ++card)
	{
		if (s_cardTemplate[card] == templateIndex)
			return s_cardImage[card];
	}
	return findArmyImage( templateIndex );
}

/// the side a rank portrait is drawn for: the army's base side, Random (the Elite portrait) for none
static AsciiString portraitSide( Int templateIndex )
{
	const PlayerTemplate *pt = templateIndex >= 0 ? ThePlayerTemplateStore->getNthPlayerTemplate( templateIndex ) : NULL;
	return pt && pt->getBaseSide().isNotEmpty() ? pt->getBaseSide() : AsciiString( "Random" );
}

/// the army picked, a PlayerTemplate or PLAYERTEMPLATE_RANDOM, from the hidden combo Start reads
static Int pickedTemplate( void )
{
	Int pos = -1;
	if (comboBoxSide)
		GadgetComboBoxGetSelectedPos( comboBoxSide, &pos );
	return pos >= 0 ? (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxSide, pos ) : PLAYERTEMPLATE_RANDOM;
}

static Int pickedColorPos( void )
{
	Int pos = -1;
	if (comboBoxColor)
		GadgetComboBoxGetSelectedPos( comboBoxColor, &pos );
	return pos;
}

/// the colour combo's entry as a MultiplayerColor, -1 for Random
static Int colorAt( Int pos )
{
	return comboBoxColor && pos >= 0 ? (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxColor, pos ) : -1;
}

static UnicodeString colorName( Int color )
{
	MultiplayerColorDefinition *def = color >= 0 ? TheMultiplayerSettings->getColor( color ) : NULL;
	return def ? word( def->getTooltipName().str() ) : word( "GUI:Random" );
}

/// a player's colour down his card's edge, the rooms' grey for one not given yet
static Color seatColor( Int color )
{
	MultiplayerColorDefinition *def = color >= 0 ? TheMultiplayerSettings->getColor( color ) : NULL;
	return def ? def->getColor() : GameMakeColor( 128, 128, 128, 255 );
}

/// the rooms' colour box: black in a FRAME edge, the colour inset in it, the "?" for Random
static void colorBox( Int x, Int y, Int width, Int height, Int color, Int inset )
{
	TheDisplay->drawFillRect( x, y, width, height, GameMakeColor( 0, 0, 0, 255 ) );
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );
	MultiplayerColorDefinition *def = color >= 0 ? TheMultiplayerSettings->getColor( color ) : NULL;
	if (def)
	{
		TheDisplay->drawFillRect( x + inset, y + inset, width - 2 * inset, height - 2 * inset, def->getColor() );
		return;
	}
	if (s_randomImage == NULL)
		return;
	// the art's own 24 by 22 at 1080, smaller in a smaller box
	const Int wide = min( qpx( 24 ), min( width, height ) - 2 * inset );
	const Int tall = wide * 22 / 24;
	const Int left = x + (width - wide) / 2, top = y + (height - tall) / 2;
	TheDisplay->drawImage( s_randomImage, left, top, left + wide, top + tall );
}

/// a rank portrait in its FRAME edge, size square
static void portrait( const Image *image, Int x, Int y, Int size )
{
	if (image == NULL)
		return;
	TheDisplay->drawImage( image, x, y, x + size, y + size );
	TheDisplay->drawOpenRect( x, y, size, size, 1.0f, UIStyle::FRAME );
}

static UnicodeString ratingOf( const OnlinePlayerInfo &info )
{
	return info.hasRating ? UIStyle::ratingText( info.exactRating ) : UnicodeString( QM_NONE );
}

/** A player as the page shows him, worked out again only when the server's numbers move
	* (OnlinePlayerInfoSerial), the reveal changes, or he or his army does: the card lookup, the tier's
	* words and the rank portrait's image search are too dear for every frame. */
struct QMPlayer
{
	Bool filled;
	AsciiString nick;
	Int army;
	UnsignedInt serial;					///< OnlinePlayerInfoSerial when filled
	UnsignedInt reveal;					///< s_revealSerial when filled
	OnlinePlayerInfo info;
	UIStyle::RatingTier tier;
	UnicodeString tierName;
	UnicodeString rating;
	const Image *portrait;
};
enum { QM_SEATS = 2 };
static QMPlayer s_player[QM_SEATS];		///< the local player, the opponent

static void forgetPlayers( void )
{
	for (Int seat = 0; seat < QM_SEATS; ++seat)
		s_player[seat] = QMPlayer();
}

/// seat 0 the local player, 1 the opponent; row is what qm.matched said of him, and fills in a rating
/// and site tier the server's card for him lacks
static const QMPlayer &playerAt( Int seat, const AsciiString &nick, Int army, const OnlineLadderRow *row )
{
	QMPlayer &p = s_player[seat];
	const UnsignedInt serial = OnlinePlayerInfoSerial();
	if (p.filled && p.serial == serial && p.reveal == s_revealSerial && p.army == army && p.nick == nick)
		return p;
	p.filled = TRUE;
	p.nick = nick;
	p.army = army;
	p.serial = serial;
	p.reveal = s_revealSerial;
	if (nick.isEmpty())
		p.info = OnlinePlayerInfo();
	else
		OnlineGetPlayerInfo( nick, p.info );
	if (row && !p.info.hasRating && row->hasRating)
	{
		p.info.hasRating = TRUE;
		p.info.rating = row->rating;
		p.info.exactRating = row->exactRating;
	}
	if (row && p.info.tier.isEmpty())
		p.info.tier = row->tier;
	p.tier = UIStyle::playerTier( p.info );
	p.tierName = OnlineTierName( UIStyle::tierKey( p.tier ) );
	p.rating = ratingOf( p.info );
	p.portrait = UIStyle::tierImage( p.tier, portraitSide( army ) );
	return p;
}

/// a card of the army box (0 Random, then the rows); FALSE for a card whose army is not offered
static Bool armyCardRect( GameWindow *panel, Int card, IRegion2D &r )
{
	if (s_cardTemplate[card] == QM_NO_ARMY)
		return FALSE;
	Int x, y;
	panel->winGetScreenPosition( &x, &y );
	if (card == 0)
	{
		r.lo.x = x + qpx( 20 );
		r.lo.y = y + qpx( 56 );
		r.hi.x = x + qpx( 1088 );
		r.hi.y = y + qpx( 108 );
		return TRUE;
	}
	const Int column = (card - 1) % QM_ARMY_COLUMNS, row = (card - 1) / QM_ARMY_COLUMNS;
	r.lo.x = x + qpx( 20.0f + column * 271.0f );
	r.lo.y = y + qpx( 124.0f + row * 120.0f );
	r.hi.x = r.lo.x + qpx( 255 );
	r.hi.y = r.lo.y + qpx( 108 );
	return TRUE;
}

/// a colour box, in the colour combo's order: Random, then the Quick Match colours
static void colorBoxRect( GameWindow *panel, Int box, IRegion2D &r )
{
	Int x, y;
	panel->winGetScreenPosition( &x, &y );
	r.lo.x = x + qpx( 20.0f + box * 76.0f );
	r.lo.y = y + qpx( 64 );
	r.hi.x = r.lo.x + qpx( 64 );
	r.hi.y = r.lo.y + qpx( 84 );
}

static void saveQuickMatchOptions( void );

/// a click on an army card or a colour box picks it in the hidden combo Start and the hub's Play vs AI read
static WindowMsgHandledType qmPickInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData )
{
	if (msg != GWM_LEFT_DOWN && msg != GWM_LEFT_UP)
		return MSG_IGNORED;
	const Bool army = window == s_panels[QM_PANEL_ARMY];
	GameWindow *combo = army ? comboBoxSide : comboBoxColor;
	if (combo == NULL)
		return MSG_IGNORED;
	const Int mouseX = LOLONGTOSHORT( mData1 ), mouseY = HILONGTOSHORT( mData1 );
	const Int count = army ? (Int)QM_ARMY_CARDS : GadgetComboBoxGetLength( combo );
	Int hit = -1;
	for (Int each = 0; each < count && hit < 0; ++each)
	{
		IRegion2D r;
		if (army)
		{
			if (!armyCardRect( window, each, r ))
				continue;
		}
		else
			colorBoxRect( window, each, r );
		if (mouseX >= r.lo.x && mouseX < r.hi.x && mouseY >= r.lo.y && mouseY < r.hi.y)
			hit = each;
	}
	if (hit < 0)
		return MSG_IGNORED;
	if (msg == GWM_LEFT_UP && !buttonPushed && qmPageState() == QM_PAGE_PICK)
	{
		Int pos = army ? -1 : hit;
		for (Int entry = 0; army && entry < GadgetComboBoxGetLength( combo ); ++entry)
		{
			if ((Int)(intptr_t)GadgetComboBoxGetItemData( combo, entry ) == s_cardTemplate[hit])
				pos = entry;
		}
		if (pos >= 0)
		{
			GadgetComboBoxSetSelectedPos( combo, pos );
			saveQuickMatchOptions();
		}
	}
	return MSG_HANDLED;
}

/// a box of the master as its own draw had it, PANEL in a FRAME edge, and the box's words from the start
static QMWords &panelBox( GameWindow *window, QMPanel panel, Int &x, Int &y, Int &width, Int &height )
{
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	UIStyle::panel( x, y, width, height, UIStyle::PANEL );
	s_words[panel].begin();
	return s_words[panel];
}

/// picking: Random, then each side and its generals, the picked card in the three-line edge
static void drawArmyPanel( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	QMWords &words = panelBox( window, QM_PANEL_ARMY, x, y, width, height );
	UIStyle::heading( words.get( s_face[QM_FACE_SECTION], word( "GUI:Faction" ), width - qpx( 48 ) ), x + qpx( 24 ),
		y + qpx( 20 ) );
	const Int picked = pickedTemplate();
	GameFont *name = bodyFont( TRUE );
	GameFont *body = bodyFont();
	for (Int card = 0; card < QM_ARMY_CARDS; ++card)
	{
		IRegion2D r;
		if (!armyCardRect( window, card, r ))
			continue;
		const Int cardWidth = r.hi.x - r.lo.x, cardHeight = r.hi.y - r.lo.y;
		UIStyle::panel( r.lo.x, r.lo.y, cardWidth, cardHeight, UIStyle::PANEL );
		if (s_cardTemplate[card] == picked)
			UIStyle::edge( r.lo.x, r.lo.y, cardWidth, cardHeight );
		if (card == 0)
		{
			if (s_randomImage)
				TheDisplay->drawImage( s_randomImage, r.lo.x + qpx( 16 ), r.lo.y + qpx( 15 ), r.lo.x + qpx( 40 ), r.lo.y + qpx( 37 ) );
			DisplayString *title = words.get( name, word( "GUI:QMRandomArmy" ), qpx( 260 ) );
			put( title, r.lo.x + qpx( 54 ), r.lo.y + (cardHeight - heightOf( title )) / 2, UIStyle::INK );
			DisplayString *hint = words.get( body, word( "GUI:QMRandomHint" ), cardWidth - qpx( 340 ) );
			put( hint, r.lo.x + qpx( 320 ), r.lo.y + (cardHeight - heightOf( hint )) / 2, UIStyle::MUTED );
			continue;
		}
		const Image *logo = s_cardImage[card];
		if (logo)
			TheDisplay->drawImage( logo, r.lo.x + qpx( 14 ), r.lo.y + qpx( 22 ), r.lo.x + qpx( 78 ), r.lo.y + qpx( 86 ) );
		// the general over his side, or the side over "No general"
		const Int entry = card - 1;
		const UnicodeString &side = word( QM_SIDE_NAME[entry / QM_ARMY_COLUMNS] );
		const char *general = QM_ARMY[entry].general;
		const Int textWidth = cardWidth - qpx( 102 );
		DisplayString *top = words.get( name, general ? word( general ) : side, textWidth );
		DisplayString *under = words.get( body, general ? side : word( "GUI:QMNoGeneral" ), textWidth );
		const Int gap = qpx( 6 );
		const Int textY = r.lo.y + (cardHeight - heightOf( top ) - gap - heightOf( under )) / 2;
		put( top, r.lo.x + qpx( 92 ), textY, UIStyle::INK );
		put( under, r.lo.x + qpx( 92 ), textY + heightOf( top ) + gap, UIStyle::MUTED );
	}
}

/// picking: Random and the Quick Match colours as the rooms' colour boxes, the picked one ringed
static void drawColorPanel( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	QMWords &words = panelBox( window, QM_PANEL_COLOR, x, y, width, height );
	const Int picked = pickedColorPos();
	DisplayString *heading = words.get( s_face[QM_FACE_SECTION], word( "GUI:Color" ), qpx( 300 ) );
	UIStyle::heading( heading, x + qpx( 24 ), y + qpx( 20 ) );
	put( words.get( s_face[QM_FACE_SECTION], colorName( colorAt( picked ) ), qpx( 400 ) ), x + qpx( 38 ) + widthOf( heading ),
		y + qpx( 20 ), UIStyle::INK );
	const Int boxes = comboBoxColor ? GadgetComboBoxGetLength( comboBoxColor ) : 0;
	const Int ring = qpx( 6 );
	for (Int box = 0; box < boxes; ++box)
	{
		IRegion2D r;
		colorBoxRect( window, box, r );
		const Int boxWidth = r.hi.x - r.lo.x, boxHeight = r.hi.y - r.lo.y;
		colorBox( r.lo.x, r.lo.y, boxWidth, boxHeight, colorAt( box ), qpx( 6 ) );
		if (box == picked)
			UIStyle::edge( r.lo.x - ring, r.lo.y - ring, boxWidth + 2 * ring, boxHeight + 2 * ring );
	}
}

/// picking, the side box: the ladder standing, the players searching here, and why there are no maps
static void drawLadderPanel( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	QMWords &words = panelBox( window, QM_PANEL_LADDER, x, y, width, height );
	GameFont *body = bodyFont();
	const Int left = x + qpx( 24 ), inner = width - qpx( 48 );
	UIStyle::heading( words.get( s_face[QM_FACE_SECTION], word( "GUI:Ladder" ), inner ), left, y + qpx( 20 ) );

	// the rank portrait for the picked army, the rating as a display figure over its tier
	const QMPlayer &me = playerAt( 0, TheGameSpyInfo->getLocalName(), pickedTemplate(), NULL );
	const OnlinePlayerInfo &info = me.info;
	const Int size = qpx( 144 );
	portrait( me.portrait, left, y + qpx( 64 ), size );
	const Int textX = me.portrait ? left + size + qpx( 24 ) : left;
	put( words.get( s_face[QM_FACE_DISPLAY], me.rating ), textX, y + qpx( 76 ), UIStyle::INK );
	put( words.get( body, me.tierName, left + inner - textX ), textX, y + qpx( 160 ), UIStyle::INK );

	// the ladder place, the rated games, the players searching on this server
	Int players, games, queue;
	const Bool counted = OnlineGetCounts( players, games, queue );
	static const char *const LABEL[] = { "GUI:OnlinePlaceLabel", "GUI:OnlineGamesLabel", "GUI:OnlineHubInQueue" };
	for (Int fact = 0; fact < 3; ++fact)
	{
		UnicodeString value( QM_NONE );
		if (fact == 0 && info.rank > 0)
			value.format( word( "GUI:OnlinePlaceValue" ), info.rank );
		else if (fact == 1 && info.games >= 0)
			value.format( u"%d", info.games );
		else if (fact == 2 && counted)
			value.format( u"%d", queue );
		UIStyle::fact( words.get( body, word( LABEL[fact] ), inner / 2 ), words.get( body, value, inner / 2 ),
			left, y + qpx( 240.0f + fact * 52.0f ), inner, qpx( 52 ) );
	}
	// every ranked match is on a map generated from its seed, so there is no pool to veto
	put( words.get( body, word( "GUI:QMGeneratedLine" ), 0, inner ), left, y + qpx( 420 ), UIStyle::MUTED );
}

/// searching: the clock, the players searching here, and the window of win chances on a 0 to 100 scale
static void drawSearchPanel( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	QMWords &words = panelBox( window, QM_PANEL_SEARCH, x, y, width, height );
	GameFont *body = bodyFont();
	UIStyle::heading( words.get( s_face[QM_FACE_SECTION], word( "GUI:QMSearching" ), width - qpx( 48 ) ), x + qpx( 24 ),
		y + qpx( 20 ) );

	// the others searching: the last qm.status's pool, which counts us, else the server's counts
	const Int middle = x + width / 2;
	const Int elapsed = s_searchSince ? (Int)((Clock_Milliseconds() - s_searchSince) / 1000) : 0;
	UnicodeString text;
	text.format( u"%d:%02d", elapsed / 60, elapsed % 60 );
	put( words.get( s_face[QM_FACE_CLOCK], text ), middle, y + qpx( 130 ), UIStyle::INK, QM_MIDDLE );
	Int players, games, queue;
	const Int searching = s_poolSize >= 0 ? s_poolSize : (OnlineGetCounts( players, games, queue ) ? queue : -1);
	if (searching >= 0)
	{
		// the count, then the server's name, for words that name it ("on this server" leaves it unread)
		text.format( word( "GUI:QMQueueHere" ), max( 0, searching - 1 ), s_serverLabel.str() );
		put( words.get( body, text, width - qpx( 48 ) ), middle, y + qpx( 250 ), UIStyle::MUTED, QM_MIDDLE );
	}

	Real chanceLo, chanceHi;
	Int nextMs;
	if (!OnlineGetQuickMatchChanceWindow( chanceLo, chanceHi, nextMs ))
		return;		// an older server sends no window
	const Int left = x + qpx( 120 ), scale = width - qpx( 240 );
	TheDisplay->drawFillRect( left, y + qpx( 320 ), scale, 1, UIStyle::GRID );
	const Int lo = REAL_TO_INT( chanceLo + 0.5f ), hi = REAL_TO_INT( chanceHi + 0.5f );
	put( words.get( body, word( "GUI:QMWindow" ), scale ), left, y + qpx( 358 ), UIStyle::MUTED );
	text.format( word( "GUI:QMWindowValue" ), lo, hi );
	put( words.get( body, text, scale ), left, y + qpx( 392 ), UIStyle::INK );

	// the band taken now in EDGE over the next step's, FRAME marks where widening stops
	const Int top = y + qpx( 470 ), tall = qpx( 24 );
	const Int nextLo = max( QM_WINDOW_WIDEST_LOW, lo - QM_WINDOW_STEP ), nextHi = min( QM_WINDOW_WIDEST_HIGH, hi + QM_WINDOW_STEP );
	TheDisplay->drawFillRect( left, top, scale, tall, UIStyle::GRID );
	if (nextMs >= 0)
		TheDisplay->drawFillRect( left + scale * nextLo / 100, top, scale * (nextHi - nextLo) / 100, tall,
			UIStyle::withAlpha( UIStyle::EDGE, 110 ) );
	TheDisplay->drawFillRect( left + scale * lo / 100, top, scale * (hi - lo) / 100, tall, UIStyle::EDGE );
	const Int mark = max( 1, qpx( 2 ) );
	TheDisplay->drawFillRect( left + scale * QM_WINDOW_WIDEST_LOW / 100 - mark / 2, y + qpx( 462 ), mark, qpx( 40 ), UIStyle::FRAME );
	TheDisplay->drawFillRect( left + scale * QM_WINDOW_WIDEST_HIGH / 100 - mark / 2, y + qpx( 462 ), mark, qpx( 40 ), UIStyle::FRAME );
	// the scale's numbers: the ends and the widest MUTED, the window's own edges INK in their place
	const Int number[] = { 0, QM_WINDOW_WIDEST_LOW, lo, hi, QM_WINDOW_WIDEST_HIGH, 100 };
	for (Int each = 0; each < 6; ++each)
	{
		const Bool edge = each == 2 || each == 3;
		if (!edge && (number[each] == lo || number[each] == hi))
			continue;
		text.format( u"%d", number[each] );
		put( words.get( body, text ), left + scale * number[each] / 100, y + qpx( 500 ), edge ? UIStyle::INK : UIStyle::MUTED,
			QM_MIDDLE );
	}

	// the next widening and its track, and where widening stops
	const Int half = qpx( 420 );
	if (nextMs >= 0)
	{
		const Int seconds = (nextMs + 999) / 1000;
		text.format( word( "GUI:QMWidens" ), nextLo, nextHi, seconds / 60, seconds % 60 );
		put( words.get( body, text, half ), left, y + qpx( 582 ), UIStyle::MUTED );
		UIStyle::track( left, y + qpx( 620 ), half, qpx( 10 ), 1.0f - nextMs / (Real)QM_WINDOW_STEP_MS );
	}
	text.format( word( "GUI:QMWidenCap" ), QM_WINDOW_WIDEST_LOW, QM_WINDOW_WIDEST_HIGH );
	put( words.get( body, text, half ), left + scale, y + qpx( 582 ), UIStyle::MUTED, QM_RIGHT );
}

/// searching, the side box: the picks, locked, and the ladder rating
static void drawPicksPanel( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	QMWords &words = panelBox( window, QM_PANEL_PICKS, x, y, width, height );
	GameFont *body = bodyFont();
	const Int left = x + qpx( 24 ), inner = width - qpx( 48 ), row = qpx( 52 ), gap = qpx( 12 );
	UIStyle::heading( words.get( s_face[QM_FACE_SECTION], word( "GUI:QMYourPicks" ), inner ), left, y + qpx( 20 ) );

	Int top = y + qpx( 60 );
	const Int army = pickedTemplate();
	DisplayString *value = words.get( body, armyName( army, TRUE ), inner / 2 );
	UIStyle::fact( words.get( body, word( "GUI:Faction" ), inner / 3 ), value, left, top, inner, row );
	// a general's logo at the size it reads at; Random as the colour row's Random box, the same size and art
	const Image *logo = armyImage( army );
	const Int icon = qpx( 36 ), swatch = qpx( 24 );
	if (army < 0)
		colorBox( left + inner - widthOf( value ) - gap - swatch, top + (row - swatch) / 2, swatch, swatch, -1, qpx( 4 ) );
	else if (logo)
	{
		const Int iconX = left + inner - widthOf( value ) - gap - icon;
		TheDisplay->drawImage( logo, iconX, top + (row - icon) / 2, iconX + icon, top + (row - icon) / 2 + icon );
	}

	top += row;
	const Int color = colorAt( pickedColorPos() );
	value = words.get( body, colorName( color ), inner / 2 );
	UIStyle::fact( words.get( body, word( "GUI:Color" ), inner / 3 ), value, left, top, inner, row );
	colorBox( left + inner - widthOf( value ) - gap - swatch, top + (row - swatch) / 2, swatch, swatch, color, qpx( 4 ) );

	top += row;
	const QMPlayer &me = playerAt( 0, TheGameSpyInfo->getLocalName(), army, NULL );
	UnicodeString standing = me.rating;
	standing.concat( u"  " );
	standing.concat( me.tierName );
	UIStyle::fact( words.get( body, word( "GUI:Ladder" ), inner / 3 ), words.get( body, standing, inner / 2 ), left, top,
		inner, row );

	put( words.get( body, word( "GUI:QMPicksHint" ), 0, inner ), left, top + row + qpx( 24 ), UIStyle::MUTED );
}

/// a seat on the found page
struct QMSeat
{
	UnicodeString label;
	UnicodeString nick;
	Int army;									///< a PlayerTemplate, PLAYERTEMPLATE_RANDOM until it resolves
	Int color;								///< a MultiplayerColor, -1 until the server gives one
	const QMPlayer *player;
};

/** The two seats, the player's first.  Until game.start the reveal names the opponent with his picks, and
	* the own seat has the army qm.join sent; once game.start seated everyone (the map check), the slots hold
	* each army as drawn and each colour as given. */
static void gatherSeats( QMSeat seat[QM_SEATS] )
{
	AsciiString nick[QM_SEATS];
	nick[0] = TheGameSpyInfo->getLocalName();
	const OnlineLadderRow *row = s_reveal.opponents.empty() ? NULL : &s_reveal.opponents[0];
	seat[0].army = s_joinedArmy;
	seat[0].color = colorAt( pickedColorPos() );
	// Random and the Elite portrait only for an opponent who left his army or colour to the draw (-1)
	seat[1].army = row ? row->side : PLAYERTEMPLATE_RANDOM;
	seat[1].color = row ? row->color : -1;
	if (row)
		nick[1] = row->nick;
	if (s_mapCheckPending && TheGameSpyGame)
	{
		const Int local = TheGameSpyGame->getLocalSlotNum();
		Bool opponent = FALSE;
		for (Int slotNum = 0; slotNum < MAX_SLOTS; ++slotNum)
		{
			GameSpyGameSlot *slot = TheGameSpyGame->getGameSpySlot( slotNum );
			if (slot == NULL || !slot->isHuman() || (slotNum != local && opponent))
				continue;
			const Int at = slotNum == local ? 0 : 1;
			if (at == 1)
			{
				opponent = TRUE;
				nick[1].translate( slot->getName() );
			}
			seat[at].army = slot->getPlayerTemplate();
			seat[at].color = slot->getColor();
		}
	}
	seat[0].label = word( "GUI:QMYou" );
	seat[1].label = word( "GUI:OnlineOpponentLabel" );
	for (Int each = 0; each < QM_SEATS; ++each)
	{
		if (nick[each].isEmpty())
			seat[each].nick = word( "GUI:QMAnOpponent" );
		else
			seat[each].nick.translate( nick[each] );
		seat[each].player = &playerAt( each, nick[each], seat[each].army, each == 1 ? row : NULL );
	}
}

/// a seat's card: the stripe on its outer edge, the rank portrait, nick and army, the ladder rating as a
/// display figure over its tier, the colour
static void drawSeat( QMWords &words, const QMSeat &seat, Int x, Int y, Int width, Int height, Bool stripeRight )
{
	UIStyle::panel( x, y, width, height, UIStyle::PANEL );
	UIStyle::stripe( stripeRight ? x + width - 1 - UIStyle::stripeWidth() : x + 1, y + 1, height - 2, seatColor( seat.color ) );
	const Int inset = qpx( 30 );
	portrait( seat.player->portrait, x + inset, y + inset, qpx( 144 ) );
	GameFont *body = bodyFont();
	const Int textX = x + qpx( 200 ), textWidth = width - qpx( 230 );
	put( words.get( body, seat.label, textWidth ), textX, y + qpx( 34 ), UIStyle::MUTED );
	put( words.get( s_face[QM_FACE_NICK], seat.nick, textWidth, 0, TRUE ), textX, y + qpx( 64 ), UIStyle::INK );
	put( words.get( body, armyName( seat.army, TRUE ), textWidth ), textX, y + qpx( 108 ), UIStyle::MUTED );
	TheDisplay->drawFillRect( x + inset, y + qpx( 204 ), width - 2 * inset, 1, UIStyle::GRID );

	const Int right = x + width - inset, half = width / 2 - inset;
	put( words.get( body, word( "GUI:Ladder" ), half ), x + inset, y + qpx( 226 ), UIStyle::MUTED );
	put( words.get( s_face[QM_FACE_DISPLAY], seat.player->rating ), x + inset, y + qpx( 256 ), UIStyle::INK );
	put( words.get( body, seat.player->tierName, half ), x + inset, y + qpx( 328 ), UIStyle::INK );
	put( words.get( body, word( "GUI:Color" ), half ), right, y + qpx( 226 ), UIStyle::MUTED, QM_RIGHT );
	const Int swatch = qpx( 24 );
	DisplayString *name = words.get( body, colorName( seat.color ), half - swatch - qpx( 10 ) );
	const Int nameWidth = put( name, right, y + qpx( 262 ), UIStyle::INK, QM_RIGHT );
	colorBox( right - nameWidth - qpx( 10 ) - swatch, y + qpx( 262 ) + (heightOf( name ) - swatch) / 2, swatch, swatch,
		seat.color, qpx( 4 ) );
}

/// the opponent found: two cards facing with VS and the win chance between them, the map under them,
/// and the button row as the countdown to game.start, then as the map check
static void drawFound( QMWords &words )
{
	QMSeat seat[QM_SEATS];
	gatherSeats( seat );
	const Int cardTop = UIStyle::boardY( 214 ), cardWidth = qpx( 700 ), cardHeight = qpx( 384 );
	drawSeat( words, seat[0], UIStyle::boardX( 124 ), cardTop, cardWidth, cardHeight, FALSE );
	drawSeat( words, seat[1], UIStyle::boardX( 1096 ), cardTop, cardWidth, cardHeight, TRUE );

	// the player's own chance first, his card being the left one
	const Int middle = UIStyle::boardX( 960 ), between = qpx( 272 );
	put( words.get( s_face[QM_FACE_SECTION], word( "GUI:QMFound" ), between ), middle, UIStyle::boardY( 298 ),
		UIStyle::ACTIVE, QM_MIDDLE );
	put( words.get( s_face[QM_FACE_CLOCK], word( "GUI:QMVs" ) ), middle, UIStyle::boardY( 334 ), UIStyle::INK, QM_MIDDLE );
	if (s_reveal.hasChance)
	{
		const Int own = max( 0, min( 100, REAL_TO_INT( s_reveal.chance * 100.0f + 0.5f ) ) );
		UnicodeString chance = UIStyle::percentText( own );
		chance.concat( UIStyle::HEADER_JOINER );
		chance.concat( UIStyle::percentText( 100 - own ) );
		put( words.get( bodyFont(), word( "GUI:QMChance" ), between ), middle, UIStyle::boardY( 452 ), UIStyle::MUTED,
			QM_MIDDLE );
		put( words.get( s_face[QM_FACE_CHANCE], chance, between ), middle, UIStyle::boardY( 488 ), UIStyle::INK, QM_MIDDLE );
	}

	/* The map, generated from the seed at game.start, stays unseen until the match: the load screen
		 shows neither it nor a start spot (setStartShrouded), so this box gives a "?" and no name either. */
	const Int mapX = UIStyle::boardX( 600 ), mapY = UIStyle::boardY( 622 ), mapWidth = qpx( 720 ), mapHeight = qpx( 294 );
	UIStyle::panel( mapX, mapY, mapWidth, mapHeight, UIStyle::PANEL );
	const Int picture = qpx( 246 ), pictureX = mapX + qpx( 24 ), pictureY = mapY + qpx( 24 );
	colorBox( pictureX, pictureY, picture, picture, -1, 0 );
	UIStyle::edge( pictureX - 3, pictureY - 3, picture + 6, picture + 6 );
	const Int textX = mapX + qpx( 300 ), textWidth = mapWidth - qpx( 324 );
	UIStyle::heading( words.get( s_face[QM_FACE_SECTION], word( "GUI:OnlineMapLabel" ), textWidth ), textX, mapY + qpx( 34 ) );
	put( words.get( s_face[QM_FACE_HEADING], word( "GUI:QMNewMap" ), textWidth ), textX, mapY + qpx( 74 ), UIStyle::INK );
	put( words.get( bodyFont(), word( "GUI:QMMapGenerated" ), 0, textWidth ), textX, mapY + qpx( 122 ), UIStyle::MUTED );

	// one wide button filling with EDGE as the load screen's bar fills
	const Int barX = UIStyle::boardX( UIStyle::ROOM_INSET ), barY = UIStyle::boardY( UIStyle::ROOM_BUTTON_TOP );
	const Int barWidth = UIStyle::boardX( UIStyle::ROOM_INSET_RIGHT ) - barX;
	const Int barHeight = UIStyle::boardY( UIStyle::ROOM_BUTTON_BOTTOM ) - barY;
	const UnsignedInt now = Clock_Milliseconds();
	Real done;
	UnicodeString label;
	if (s_mapCheckPending)
	{
		done = 1.0f - (Int)(s_mapCheckUntil - now) / (Real)QM_MAP_CHECK_MS;
		label = UIStyle::capitals( word( "GUI:QMCheckingMap" ) );
	}
	else
	{
		// at zero it waits on the server's game.start
		const Int left = (Int)(s_revealEnds - now), length = (Int)(s_revealEnds - s_revealStarted);
		label.format( word( "GUI:QMStartsBanner" ), left > 0 ? (left + 999) / 1000 : 0 );
		done = length > 0 ? 1.0f - left / (Real)length : 1.0f;
	}
	UIStyle::button( barX, barY, barWidth, barHeight );
	const Int fill = REAL_TO_INT( (barWidth - 6) * max( 0.0f, min( 1.0f, done ) ) + 0.5f );
	if (fill > 0)
		TheDisplay->drawFillRect( barX + 3, barY + 3, fill, barHeight - 6, UIStyle::withAlpha( UIStyle::EDGE, 110 ) );
	DisplayString *banner = words.get( s_face[QM_FACE_BUTTON], label, barWidth - qpx( 48 ) );
	put( banner, barX + barWidth / 2, barY + (barHeight - heightOf( banner )) / 2, UIStyle::INK, QM_MIDDLE );
}

/// the abandon lock counting down on the button row, between Back and the Search it keeps disabled
static void drawLockLine( QMWords &words )
{
	const Int seconds = qmLockSeconds();
	if (seconds <= 0 || buttonStart == NULL || buttonBack == NULL)
		return;
	Int startX, startY, startWidth, startHeight, backX, backY, backWidth, backHeight;
	buttonStart->winGetScreenPosition( &startX, &startY );
	buttonStart->winGetSize( &startWidth, &startHeight );
	buttonBack->winGetScreenPosition( &backX, &backY );
	buttonBack->winGetSize( &backWidth, &backHeight );
	const Int gap = qpx( 24 );
	UnicodeString text;
	text.format( word( "GUI:QMLocked" ), seconds / 60, seconds % 60 );
	DisplayString *line = words.get( bodyFont( TRUE ), text, startX - backX - backWidth - 2 * gap );
	put( line, startX - gap, startY + (startHeight - heightOf( line )) / 2, UIStyle::INK, QM_RIGHT );
}

/// the room panel, the header's wordmark and player line (the master has the title and its rule), the
/// found page, which has no box of its own, and the abandon lock while picking
static void drawQMPage( GameWindow *window, WinInstanceData * )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	UIStyle::panel( x, y, width, height, UIStyle::PANEL );
	UIStyle::onlineHeader( UnicodeString::TheEmptyString, UnicodeString::TheEmptyString );
	s_words[QM_PANEL_COUNT].begin();
	if (s_pageShown == QM_PAGE_FOUND)
		drawFound( s_words[QM_PANEL_COUNT] );
	else if (s_pageShown == QM_PAGE_PICK)
		drawLockLine( s_words[QM_PANEL_COUNT] );
}

enum 
{
	MAX_DISCONNECTS_ANY = 0,
	MAX_DISCONNECTS_5 = 5,
	MAX_DISCONNECTS_10 = 10,
	MAX_DISCONNECTS_25 = 25,
	MAX_DISCONNECTS_50 = 50,
};
enum{ MAX_DISCONNECTS_COUNT = 5 };

static Int MAX_DISCONNECTS[MAX_DISCONNECTS_COUNT] = {MAX_DISCONNECTS_ANY, MAX_DISCONNECTS_5, 
																											MAX_DISCONNECTS_10, MAX_DISCONNECTS_25,
																											MAX_DISCONNECTS_50};
																										

void UpdateStartButton(void)
{
	// -online hides the map list, its match being on a generated map whatever was ticked, and waits out
	// the abandon lock; ladder or none (comboBoxLadder is NULL without ladders)
	if (buttonStart && OnlineIsEnabled())
	{
		buttonStart->winEnable( qmLockSeconds() == 0 );
		return;
	}
	if (!comboBoxLadder || !buttonStart || !listboxMapSelect)
		return;

	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	if (li)
	{
		buttonStart->winEnable(TRUE);
		return;
	}

	Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
	for ( Int i=0; i<numMaps; ++i )
	{
		if ((Bool)GadgetListBoxGetItemData(listboxMapSelect, i, 0))
		{
			buttonStart->winEnable(TRUE);
			return;
		}
	}
	buttonStart->winEnable(FALSE);
}

// -----------------------------------------------------------------------------

/// qm.join's colour is a MultiplayerColor under this, EA's eight and the five Reforged added; the server
/// turns any other into Random (online/server.js QM_COLORS), and the colour box holds one row of them
static const Int QM_COLORS = 13;

static void populateQMColorComboBox(QuickMatchPreferences& pref)
{
	Int numColors = min(QM_COLORS, TheMultiplayerSettings->getNumColors());
	UnicodeString colorName;

	GadgetComboBoxReset(comboBoxColor);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboBoxColor, TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboBoxColor, newIndex, (void *)-1);

	for (Int c=0; c<numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def)
			continue;

		colorName = TheGameText->fetch(def->getTooltipName().str());
		newIndex = GadgetComboBoxAddEntry(comboBoxColor, colorName, def->getColor());
		GadgetComboBoxSetItemData(comboBoxColor, newIndex, (void *)(intptr_t)c);
	}
	// the saved position, Random when it named a colour this box no longer holds
	const Int saved = pref.getColor();
	GadgetComboBoxSetSelectedPos(comboBoxColor, saved >= 0 && saved < GadgetComboBoxGetLength(comboBoxColor) ? saved : 0);
}

// -----------------------------------------------------------------------------

static void populateQMSideComboBox(Int favSide, const LadderInfo *li = NULL)
{
	// Quick Match offers the armies a plain Random draws from: playable, not a locked general, not
	// RandomExcluded (Turkiye), which the server also turns back into Random.  A saved pick that is
	// not listed selects Random.
	std::vector<Int> templates;
	GetRandomDraw(PLAYERTEMPLATE_RANDOM, FALSE, templates);

	GadgetComboBoxReset(comboBoxSide);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboBoxSide, TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboBoxSide, newIndex, (void *)PLAYERTEMPLATE_RANDOM);

	std::set<AsciiString> seenSides;
	
	Int entryToSelect = 0; // select Random by default

	for (size_t t=0; t<templates.size(); ++t)
	{
		const Int c = templates[t];
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);

		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		if (li)
		{
			if (std::find(li->validFactions.begin(), li->validFactions.end(), fac->getSide()) == li->validFactions.end())
				continue; // ladder doesn't allow it.
		}

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboBoxSide, TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboBoxSide, newIndex, (void *)(intptr_t)c);

		if (c == favSide)
			entryToSelect = newIndex;
	}
	seenSides.clear();

	GadgetComboBoxSetSelectedPos(comboBoxSide, entryToSelect);
	if (li && li->randomFactions)
		comboBoxSide->winEnable(FALSE);
	else
		comboBoxSide->winEnable(TRUE);
}

void HandleQMLadderSelection(Int ladderID)
{
	if (!parentWOLQuickMatch)
		return;

	QuickMatchPreferences pref;

	if (ladderID < 1)
	{
		pref.setLastLadder(AsciiString::TheEmptyString, 0);
		pref.write();
		return;
	}

	const LadderInfo *info = TheLadderList->findLadderByIndex(ladderID);
	if (!info)
	{
		pref.setLastLadder(AsciiString::TheEmptyString, 0);
	}
	else
	{
		pref.setLastLadder(info->address, info->port);
	}

	pref.write();
}

static inline Bool isValidLadder( const LadderInfo *lad )
{
	if (lad && lad->index > 0 && lad->validQM)
	{
		PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(TheGameSpyInfo->getLocalProfileID());
		Int numWins = 0;
		PerGeneralMap::iterator it;
		for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
		{
			numWins += it->second;
		}
		if (!lad->maxWins || lad->maxWins >=numWins)
		{
			if (!lad->minWins || lad->minWins<=numWins)
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

void PopulateQMLadderListBox( GameWindow *win )
{
	if (!parentWOLQuickMatch || !comboBoxLadder)
		return;

	isPopulatingLadderBox = true;

	QuickMatchPreferences pref;
	AsciiString userPrefFilename;
	Int localProfile = TheGameSpyInfo->getLocalProfileID();

	Color specialColor = GameSpyColor[GSCOLOR_MAP_SELECTED];
	Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Color favoriteColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Int index;
	GadgetListBoxReset( win );

	std::set<const LadderInfo *> usedLadders;

	// start with "No Ladder"
	index = GadgetListBoxAddEntryText( win, TheGameText->fetch("GUI:NoLadder"), normalColor, -1 );
	GadgetListBoxSetItemData( win, 0, index );

	// add the last ladder
	Int selectedPos = 0;
	AsciiString lastLadderAddr = pref.getLastLadderAddr();
	UnsignedShort lastLadderPort = pref.getLastLadderPort();
	const LadderInfo *info = TheLadderList->findLadder( lastLadderAddr, lastLadderPort );
	if (isValidLadder(info))
	{
		usedLadders.insert(info);
		index = GadgetListBoxAddEntryText( win, info->name, favoriteColor, -1 );
		GadgetListBoxSetItemData( win, (void *)(intptr_t)(info->index), index );
		selectedPos = index;
	}

	// our recent ladders
	LadderPreferences ladPref;
	ladPref.loadProfile( localProfile );
	const LadderPrefMap recentLadders = ladPref.getRecentLadders();
	for (LadderPrefMap::const_iterator cit = recentLadders.begin(); cit != recentLadders.end(); ++cit)
	{
		AsciiString addr = cit->second.address;
		UnsignedShort port = cit->second.port;
		if (addr == lastLadderAddr && port == lastLadderPort)
			continue;
		const LadderInfo *info = TheLadderList->findLadder( addr, port );
		if (isValidLadder(info) && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetListBoxAddEntryText( win, info->name, favoriteColor, -1 );
			GadgetListBoxSetItemData( win, (void *)(intptr_t)(info->index), index );
		}
	}

	// special ladders
	const LadderInfoList *lil = TheLadderList->getSpecialLadders();
	LadderInfoList::const_iterator lit;
	for (lit = lil->begin(); lit != lil->end(); ++lit)
	{
		const LadderInfo *info = *lit;
		if (isValidLadder(info) && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetListBoxAddEntryText( win, info->name, specialColor, -1 );
			GadgetListBoxSetItemData( win, (void *)(intptr_t)(info->index), index );
		}
	}

	// standard ladders
	lil = TheLadderList->getStandardLadders();
	for (lit = lil->begin(); lit != lil->end(); ++lit)
	{
		const LadderInfo *info = *lit;
		if (isValidLadder(info) && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetListBoxAddEntryText( win, info->name, normalColor, -1 );
			GadgetListBoxSetItemData( win, (void *)(intptr_t)(info->index), index );
		}
	}

	GadgetListBoxSetSelected( win, selectedPos );
	isPopulatingLadderBox = false;
}

static const LadderInfo * getLadderInfo( void )
{
	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	return li;
}

void PopulateQMLadderComboBox( void )
{
	if (!parentWOLQuickMatch || !comboBoxLadder)
		return;

	isPopulatingLadderBox = true;

	QuickMatchPreferences pref;
	Int localProfile = TheGameSpyInfo->getLocalProfileID();

	Color specialColor = GameSpyColor[GSCOLOR_MAP_SELECTED];
	Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
	Int index;
	GadgetComboBoxReset( comboBoxLadder );
	index = GadgetComboBoxAddEntry( comboBoxLadder, TheGameText->fetch("GUI:NoLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadder, index, 0 );

	std::set<const LadderInfo *> usedLadders;

	Int selectedPos = 0;
	AsciiString lastLadderAddr = pref.getLastLadderAddr();
	UnsignedShort lastLadderPort = pref.getLastLadderPort();
	const LadderInfo *info = TheLadderList->findLadder( lastLadderAddr, lastLadderPort );
	if (isValidLadder(info))
	{
		usedLadders.insert(info);
		index = GadgetComboBoxAddEntry( comboBoxLadder, info->name, specialColor );
		GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)(intptr_t)(info->index) );
		selectedPos = index;

		// we selected a ladder?  No game size choice for us...
		GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, info->playersPerTeam-1);
		comboBoxNumPlayers->winEnable( FALSE );
	}
	else
	{
		comboBoxNumPlayers->winEnable( TRUE );
	}

	LadderPreferences ladPref;
	ladPref.loadProfile( localProfile );
	const LadderPrefMap recentLadders = ladPref.getRecentLadders();
	for (LadderPrefMap::const_iterator cit = recentLadders.begin(); cit != recentLadders.end(); ++cit)
	{
		AsciiString addr = cit->second.address;
		UnsignedShort port = cit->second.port;
		if (addr == lastLadderAddr && port == lastLadderPort)
			continue;
		const LadderInfo *info = TheLadderList->findLadder( addr, port );
		if (isValidLadder(info) && usedLadders.find(info) == usedLadders.end())
		{
			usedLadders.insert(info);
			index = GadgetComboBoxAddEntry( comboBoxLadder, info->name, normalColor );
			GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)(intptr_t)(info->index) );
		}
	}

	index = GadgetComboBoxAddEntry( comboBoxLadder, TheGameText->fetch("GUI:ChooseLadder"), normalColor );
	GadgetComboBoxSetItemData( comboBoxLadder, index, (void *)-1 );

	GadgetComboBoxSetSelectedPos( comboBoxLadder, selectedPos );
	isPopulatingLadderBox = false;

	populateQMSideComboBox(pref.getSide(), getLadderInfo()); // this will set side to random and disable if necessary
}

static void populateQuickMatchMapSelectListbox( QuickMatchPreferences& pref )
{
	std::list<AsciiString> maps = TheGameSpyConfig->getQMMaps();

	// enable/disable box based on ladder status
	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	//listboxMapSelect->winEnable( li == NULL || li->randomMaps == FALSE );

	Int numPlayers = 0;
	if (li)
	{
		numPlayers = li->playersPerTeam*2;

		maps = li->validMaps;
	}
	else
	{
		GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &selected);
		if (selected < 0)
			selected = 0;
		numPlayers = (selected+1)*2;
	}


	GadgetListBoxReset(listboxMapSelect);
	for (std::list<AsciiString>::const_iterator it = maps.begin(); it != maps.end(); ++it)
	{
		AsciiString theMap = *it;
		const MapMetaData *md = TheMapCache->findMap(theMap);
		if (md && md->m_numPlayers >= numPlayers)
		{
			UnicodeString displayName;
			displayName = md->m_displayName;
			Bool isSelected = pref.isMapSelected(theMap);
			if (li && li->randomMaps)
				isSelected = TRUE;
			Int width = 10;
			Int height = 10;
			const Image *img = (isSelected)?selectedImage:unselectedImage;
			if ( img )
			{
				width = min(GadgetListBoxGetColumnWidth(listboxMapSelect, 0), img->getImageWidth());
				height = width;
			}
			Int index = GadgetListBoxAddEntryImage(listboxMapSelect, img, -1, 0, height, width);
			GadgetListBoxAddEntryText(listboxMapSelect, displayName, GameSpyColor[(isSelected)?GSCOLOR_MAP_SELECTED:GSCOLOR_MAP_UNSELECTED], index, 1);
			GadgetListBoxSetItemData(listboxMapSelect, (void *)isSelected, index);
			GadgetListBoxSetItemData(listboxMapSelect, (void *)md, index, 1);
		}
	}
}

static void saveQuickMatchOptions( void )
{
	if(isInInit)
		return;
	QuickMatchPreferences pref;

	std::list<AsciiString> maps = TheGameSpyConfig->getQMMaps();

	Int index;
	Int selected;
	GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
	index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
	const LadderInfo *li = TheLadderList->findLadderByIndex( index );
	Int numPlayers = 0;

	if (li)
	{
		pref.setLastLadder( li->address, li->port );
		numPlayers = li->playersPerTeam*2;

		pref.write();
		//return; // don't save our defaults based on the tournament's defaults
	}
	else
	{
		pref.setLastLadder( AsciiString::TheEmptyString, 0 );
		GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &selected);
		if (selected < 0)
			selected = 0;
		numPlayers = (selected+1)*2;
	}

	if (!li || !li->randomMaps)  // don't save the map as selected if we couldn't choose
	{
		Int row = 0;
		Int entries = GadgetListBoxGetNumEntries(listboxMapSelect);
		while ( row < entries)
		{
			const MapMetaData *md = (const MapMetaData *)GadgetListBoxGetItemData(listboxMapSelect, row, 1);
			if(md)
				pref.setMapSelected(md->m_fileName, (Bool)GadgetListBoxGetItemData(listboxMapSelect, row));
			row++;
		}
	}

	UnicodeString u;
	AsciiString a;
//	u = GadgetTextEntryGetText(textEntryMaxDisconnects);
//	a.translate(u);
//	pref.setMaxDisconnects(atoi(a.str()));
//	u = GadgetTextEntryGetText(textEntryMaxPoints);
//	a.translate(u);
//	pref.setMaxPoints(max(100, atoi(a.str())));
//	u = GadgetTextEntryGetText(textEntryMinPoints);
//	a.translate(u);
//	pref.setMinPoints(min(100, atoi(a.str())));
	//u = GadgetTextEntryGetText(textEntryWaitTime);
	//a.translate(u);
	//pref.setWaitTime(atoi(a.str()));

	GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &selected);
	pref.setNumPlayers(selected);
	GadgetComboBoxGetSelectedPos(comboBoxMaxPing, &selected);
	pref.setMaxPing(selected);

	// the army as the hub's Play vs AI reads it too: PLAYERTEMPLATE_RANDOM for Random
	pref.setSide(pickedTemplate());
	GadgetComboBoxGetSelectedPos(comboBoxColor, &selected);
	pref.setColor(max(0, selected));

	GadgetComboBoxGetSelectedPos(comboBoxMaxDisconnects, &selected);
	pref.setMaxDisconnects(selected);


	pref.write();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Quick Match Menu */
//-------------------------------------------------------------------------------------------------
void WOLQuickMatchMenuInit( WindowLayout *layout, void *userData )
{
	isInInit = TRUE;
	if (TheGameSpyGame && TheGameSpyGame->isGameInProgress())
	{
		TheGameSpyGame->setGameInProgress(FALSE);

		// check if we were disconnected
		Int disconReason;
		if (TheGameSpyInfo->isDisconnectedAfterGameStart(&disconReason))
		{
			AsciiString disconMunkee;
			disconMunkee.format("GUI:GSDisconReason%d", disconReason);
			UnicodeString title, body;
			title = TheGameText->fetch( "GUI:GSErrorTitle" );
			body = TheGameText->fetch( disconMunkee );
			GameSpyCloseAllOverlays();
			GSMessageBoxOk( title, body );
			TheGameSpyInfo->reset();
			DEBUG_LOG(("WOLQuickMatchMenuInit() - game was in progress, and we were disconnected, so pop immediate back to main menu\n"));
			TheShell->popImmediate();
			return;
		}
	}

	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	raiseMessageBoxes = true;

	if (TheNAT != NULL) {
		delete TheNAT;
		TheNAT = NULL;
	}

	parentWOLQuickMatchID = NAMEKEY( "WOLQuickMatchMenu.wnd:WOLQuickMatchMenuParent" );
	buttonBackID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonBack" );
	buttonBuddiesID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonBuddies" );
	buttonStartID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonStart" );
	buttonStopID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonStop" );
	buttonWidenID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonWiden" );
	listboxQuickMatchID = NAMEKEY( "WOLQuickMatchMenu.wnd:ListboxQuickMatch" );
	listboxMapSelectID = NAMEKEY( "WOLQuickMatchMenu.wnd:ListBoxMapSelect" );
	buttonSelectAllMapsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonSelectAllMaps" );
	buttonSelectNoMapsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ButtonSelectNoMaps" );
	//textEntryMaxDisconnectsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMaxDisconnects" );
	//textEntryMaxPointsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMaxPointPercent" );
	//textEntryMinPointsID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryMinPointPercent" );
	textEntryWaitTimeID = NAMEKEY( "WOLQuickMatchMenu.wnd:TextEntryWaitTime" );
	comboBoxMaxPingID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxMaxPing" );
	comboBoxNumPlayersID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxNumPlayers" );
	comboBoxLadderID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxLadder" );
	comboBoxMaxDisconnectsID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxMaxDisconnects" );
	staticTextNumPlayersID = NAMEKEY( "WOLQuickMatchMenu.wnd:StaticTextNumPlayers" );
	comboBoxSideID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxSide" );
	comboBoxColorID = NAMEKEY( "WOLQuickMatchMenu.wnd:ComboBoxColor" );

	parentWOLQuickMatch = TheWindowManager->winGetWindowFromId( NULL, parentWOLQuickMatchID );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonBackID);
	buttonStart = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonStartID);
	buttonStop = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonStopID);
	buttonWiden = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  buttonWidenID);
	quickmatchTextWindow = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  listboxQuickMatchID);
	listboxMapSelect = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,  listboxMapSelectID);
	//textEntryMaxDisconnects = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMaxDisconnectsID );
	//textEntryMaxPoints = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMaxPointsID );
	//textEntryMinPoints = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryMinPointsID );
	textEntryWaitTime = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, textEntryWaitTimeID );
	comboBoxMaxPing = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxMaxPingID );
	comboBoxNumPlayers = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxNumPlayersID );
	comboBoxLadder = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxLadderID );
	comboBoxMaxDisconnects = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxMaxDisconnectsID );
	TheGameSpyInfo->registerTextWindow(quickmatchTextWindow);
	staticTextNumPlayers = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, staticTextNumPlayersID );
	comboBoxSide = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxSideID );
	comboBoxColor = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, comboBoxColorID );

	if (TheLadderList->getStandardLadders()->size() == 0
		&& TheLadderList->getSpecialLadders()->size() == 0
		&& TheLadderList->getLocalLadders()->size() == 0)
	{
		// no ladders, so just disable them
		comboBoxDisabledLadder = comboBoxLadder;
		comboBoxLadder = NULL;

		isPopulatingLadderBox = TRUE;

		Color normalColor = GameSpyColor[GSCOLOR_MAP_UNSELECTED];
		Int index;
		GadgetComboBoxReset( comboBoxDisabledLadder );
		index = GadgetComboBoxAddEntry( comboBoxDisabledLadder, TheGameText->fetch("GUI:NoLadder"), normalColor );
		GadgetComboBoxSetItemData( comboBoxDisabledLadder, index, 0 );
		GadgetComboBoxSetSelectedPos( comboBoxDisabledLadder, index );

		isPopulatingLadderBox = FALSE;

		/** This code would actually *hide* the combo box, but it doesn't look as good.  Left here since someone will want to
		 ** see it at some point.  :P
		if (comboBoxLadder)
		{
			comboBoxLadder->winHide(TRUE);
			comboBoxLadder->winEnable(FALSE);
		}
		comboBoxLadder = NULL;
		GameWindow *staticTextLadder = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch,
			NAMEKEY("WOLQuickMatchMenu.wnd:StaticTextLadder") );
		if (staticTextLadder)
			staticTextLadder->winHide(TRUE);
		*/
	}

	GameWindow *buttonBuddies = TheWindowManager->winGetWindowFromId(NULL, buttonBuddiesID);
	if (buttonBuddies)
		buttonBuddies->winEnable(TRUE);

	/* The buttons in the rooms' "Generals" 15 capitals (the master sets them in Arial 10), with the
		 master's labels. */
	const struct { const char *name; const char *label; } buttons[] = {
		{ "WOLQuickMatchMenu.wnd:ButtonBack", "GUI:Back" }, { "WOLQuickMatchMenu.wnd:ButtonWiden", "GUI:WidenSearch" },
		{ "WOLQuickMatchMenu.wnd:ButtonStop", "GUI:QMStop" }, { "WOLQuickMatchMenu.wnd:ButtonStart", "GUI:QMSearch" } };
	findFaces();
	GameFont *buttonFont = s_face[QM_FACE_BUTTON];
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
	{
		GameWindow *button = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, NAMEKEY( buttons[i].name ) );
		if (!button)
			continue;
		Int width, height;
		button->winGetSize( &width, &height );
		button->winSetFont( buttonFont );
		GadgetButtonSetText( button, UIStyle::fitText( UIStyle::capitals( TheGameText->fetch( buttons[i].label ) ), buttonFont,
			width - qpx( 24 ) ) );
	}

	// the page drawn in code: the header's row and tabs, the boxes, the cards and colour boxes that take clicks
	UIStyle::onlinePageInit( parentWOLQuickMatch, "WOLQuickMatchMenu.wnd", UIStyle::ONLINE_TAB_QUICK_MATCH );
	GameWindow *room = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, NAMEKEY( "WOLQuickMatchMenu.wnd:SubParent" ) );
	if (room)
		room->winSetDrawFunc( drawQMPage );
	static const GameWinDrawFunc PANEL_DRAW[QM_PANEL_COUNT] = { drawArmyPanel, drawColorPanel, drawLadderPanel,
		drawSearchPanel, drawPicksPanel };
	for (Int each = 0; each < QM_PANEL_COUNT; ++each)
	{
		s_panels[each] = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, NAMEKEY( QM_PANEL_NAME[each] ) );
		if (s_panels[each])
			s_panels[each]->winSetDrawFunc( PANEL_DRAW[each] );
	}
	for (Int each = QM_PANEL_ARMY; each <= QM_PANEL_COLOR; ++each)
	{
		if (s_panels[each])
		{
			s_panels[each]->winClearStatus( WIN_STATUS_NO_INPUT );
			s_panels[each]->winSetInputFunc( qmPickInput );
		}
	}
	// a ranked match is on a map generated from its seed, so the master's map list and its All and None go
	static const char *const noMaps[] = { "WOLQuickMatchMenu.wnd:ListBoxMapSelect", "WOLQuickMatchMenu.wnd:ButtonSelectAllMaps",
		"WOLQuickMatchMenu.wnd:ButtonSelectNoMaps" };
	for (size_t i = 0; i < sizeof(noMaps) / sizeof(noMaps[0]); ++i)
	{
		GameWindow *win = TheWindowManager->winGetWindowFromId( parentWOLQuickMatch, NAMEKEY( noMaps[i] ) );
		if (win)
			win->winHide( TRUE );
	}
	s_randomImage = TheMappedImageCollection->findImageByName( "GameinfoRANDOM" );

	// QM is not going yet, so disable the Widen Search button
	buttonWiden->winEnable( FALSE );
	GadgetListBoxReset(quickmatchTextWindow);
	enableOptionsGadgets(TRUE);

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentWOLQuickMatch );

	// fill in preferences
	selectedImage = TheMappedImageCollection->findImageByName("CustomMatch_selected");
	unselectedImage = TheMappedImageCollection->findImageByName("CustomMatch_deselected");
	QuickMatchPreferences pref;


	UnicodeString s;
//	s.format(L"%d", pref.getMaxDisconnects());
//	GadgetTextEntrySetText(textEntryMaxDisconnects, s);
//	s.format(L"%d", pref.getMaxPoints());
//	GadgetTextEntrySetText(textEntryMaxPoints, s);
//	s.format(L"%d", pref.getMinPoints());
//	GadgetTextEntrySetText(textEntryMinPoints, s);
	//s.format(L"%d", pref.getWaitTime());
	//GadgetTextEntrySetText(textEntryWaitTime, s);
	maxPoints= pref.getMaxPoints();
	minPoints = pref.getMinPoints();

	Color c = GameSpyColor[GSCOLOR_DEFAULT];
	GadgetComboBoxReset( comboBoxNumPlayers );
	Int i;
	for (i=1; i<5; ++i)
	{
		s.format(TheGameText->fetch("GUI:PlayersVersusPlayers"), i, i);
		GadgetComboBoxAddEntry( comboBoxNumPlayers, s, c );
	}
	GadgetComboBoxSetSelectedPos( comboBoxNumPlayers, max(0, pref.getNumPlayers()) );

	GadgetComboBoxReset(comboBoxMaxDisconnects);
	GadgetComboBoxAddEntry( comboBoxMaxDisconnects, TheGameText->fetch("GUI:Any"), c);
	for( i = 1; i < MAX_DISCONNECTS_COUNT; ++i )
	{
		s.format(u"%d", MAX_DISCONNECTS[i]);
		GadgetComboBoxAddEntry( comboBoxMaxDisconnects, s, c );
	}
	Int maxDisconIndex = max(0, pref.getMaxDisconnects());
	GadgetComboBoxSetSelectedPos(comboBoxMaxDisconnects, maxDisconIndex);

	GadgetComboBoxReset( comboBoxMaxPing );
	maxPingEntries = (TheGameSpyConfig->getPingTimeoutInMs() - 1) / 100;
	maxPingEntries++; // need to add the entry for the actual timeout
	for (i=1; i <maxPingEntries; ++i)
	{
		s.format(TheGameText->fetch("GUI:TimeInMilliseconds"), i*100);
		GadgetComboBoxAddEntry( comboBoxMaxPing, s, c );
	}
	GadgetComboBoxAddEntry( comboBoxMaxPing, TheGameText->fetch("GUI:ANY"), c );
	i = pref.getMaxPing();
	if( i < 0 )
		i = 0;
	if( i >= maxPingEntries )
		i = maxPingEntries - 1;
	GadgetComboBoxSetSelectedPos( comboBoxMaxPing, i );

	populateQMColorComboBox(pref);
	populateQMSideComboBox(pref.getSide(), getLadderInfo());

	PopulateQMLadderComboBox();
	TheShell->showShellMap(TRUE);
	TheGameSpyGame->reset();

	GadgetListBoxReset(listboxMapSelect);
	populateQuickMatchMapSelectListbox(pref);

	// the army cards, for the armies the side combo offers, each in its QM_ARMY place
	s_cardTemplate[0] = PLAYERTEMPLATE_RANDOM;
	for (Int card = 1; card < QM_ARMY_CARDS; ++card)
		s_cardTemplate[card] = QM_NO_ARMY;
	for (Int entry = 0; comboBoxSide && entry < GadgetComboBoxGetLength( comboBoxSide ); ++entry)
	{
		const Int army = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxSide, entry );
		const Int place = armyEntry( army );
		if (place >= 0)
			s_cardTemplate[1 + place] = army;
		else if (army >= 0)
			DEBUG_LOG(("QUICK MATCH army %d has no card\n", army));
	}
	for (Int card = 0; card < QM_ARMY_CARDS; ++card)
		s_cardImage[card] = findArmyImage( s_cardTemplate[card] );
	s_joinedArmy = PLAYERTEMPLATE_RANDOM;
	forgetPlayers();
	s_serverLabel = LobbyServerLabel();

	UpdateStartButton();
	onlineSearchStopped();
	forgetReveal();
	s_mapCheckPending = FALSE;
	s_peerMapCRC.clear();
	s_pageShown = QM_PAGE_NONE;
	showPageState();
	TheTransitionHandler->setGroup("WOLQuickMatchMenuFade");
	isInInit= FALSE;
} // WOLQuickMatchMenuInit

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// the page's words live as long as it draws, through the fade out
	for (Int each = 0; each <= QM_PANEL_COUNT; ++each)
		s_words[each].clear();
	for (Int each = 0; each < QM_PANEL_COUNT; ++each)
		s_panels[each] = NULL;
	s_pageShown = QM_PAGE_NONE;
	s_wordCache.clear();
	forgetPlayers();
	s_serverLabel.clear();

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		TheShell->push(nextScreen);
	}

	nextScreen = NULL;

}  // end if

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLQuickMatchMenuShutdown( WindowLayout *layout, void *userData )
{
	TheGameSpyInfo->unregisterTextWindow(quickmatchTextWindow);

	if (TheGameEngine != NULL && !TheGameEngine->getQuitting())	// NULL when the shell's destructor runs this on exit
		saveQuickMatchOptions();

	UIStyle::onlinePageShutdown( parentWOLQuickMatch );
	parentWOLQuickMatch = NULL;
	buttonBack = NULL;
	quickmatchTextWindow = NULL;
	selectedImage = unselectedImage = NULL;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("WOLQuickMatchMenuFade");

	RaiseGSMessageBox();
}  // WOLQuickMatchMenuShutdown


#ifdef PERF_TEST
static const char* getMessageString(Int t)
{
	switch(t)
	{
		case PeerResponse::PEERRESPONSE_LOGIN:
			return "login";
		case PeerResponse::PEERRESPONSE_DISCONNECT:
			return "disconnect";
		case PeerResponse::PEERRESPONSE_MESSAGE:
			return "message";
		case PeerResponse::PEERRESPONSE_GROUPROOM:
			return "group room";
		case PeerResponse::PEERRESPONSE_STAGINGROOM:
			return "staging room";
		case PeerResponse::PEERRESPONSE_STAGINGROOMPLAYERINFO:
			return "staging room player info";
		case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
			return "group room join";
		case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
			return "staging room create";
		case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
			return "staging room join";
		case PeerResponse::PEERRESPONSE_PLAYERJOIN:
			return "player join";
		case PeerResponse::PEERRESPONSE_PLAYERLEFT:
			return "player part";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDNICK:
			return "player nick";
		case PeerResponse::PEERRESPONSE_PLAYERINFO:
			return "player info";
		case PeerResponse::PEERRESPONSE_PLAYERCHANGEDFLAGS:
			return "player flags";
		case PeerResponse::PEERRESPONSE_ROOMUTM:
			return "room UTM";
		case PeerResponse::PEERRESPONSE_PLAYERUTM:
			return "player UTM";
		case PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS:
			return "QM status";
		case PeerResponse::PEERRESPONSE_GAMESTART:
			return "game start";
		case PeerResponse::PEERRESPONSE_FAILEDTOHOST:
			return "host failure";
	}
	return "unknown";
}
#endif // PERF_TEST

/* -online: every peer builds the match's map from the seed, and nothing in the match compares the result,
	so a machine whose copy came out different would split the game at its first CRC check. Before the game
	starts each peer sends the others "MC/<seed> <map CRC>", a player UTM the server passes on unread, and
	starts only once every seat has answered with its own CRC. A seat that never answers (a build without
	this check) calls the match off when the wait runs out instead of leaving us loading against it.
	The state is with the search's above, and the found page shows the wait on its button row. */

static AsciiString qmSlotNick( Int slotNum )
{
	AsciiString nick;
	nick.translate( TheGameSpyGame->getGameSpySlot( slotNum )->getName() );
	return nick;
}

/// kept whenever it comes: a peer can be matched, and send, a moment before we are
static void qmPeerMapArrived( const std::string &nick, const std::string &options )
{
	Int seed = 0;
	UnsignedInt crc = 0;
	if (sscanf( options.c_str(), "%d %X", &seed, &crc ) != 2)
		return;
	AsciiString key = nick.c_str();
	key.toLower();
	s_peerMapCRC[key] = std::make_pair( seed, crc );
}

static void qmLaunchMatched( void )
{
	SendStatsToOtherPlayers(TheGameSpyGame);
	TheGameSpyGame->startGame(0);
	GameWindow *buttonBuddies = TheWindowManager->winGetWindowFromId(NULL, buttonBuddiesID);
	if (buttonBuddies)
		buttonBuddies->winEnable(FALSE);
	GameSpyCloseOverlay(GSOVERLAY_BUDDY);
}

static void qmBeginMapCheck( Int seed )
{
	s_mapCheckPending = TRUE;
	s_mapCheckSeed = seed;
	s_mapCheckUntil = Clock_Milliseconds() + QM_MAP_CHECK_MS;

	AsciiString options;
	options.format( "%d %X", seed, TheGameSpyGame->getMapCRC() );
	PeerRequest req;
	req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
	req.UTM.isStagingRoom = TRUE;
	req.id = "MC/";
	req.options = options.str();
	const Int localSlot = TheGameSpyGame->getLocalSlotNum();
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (i == localSlot || !TheGameSpyGame->getGameSpySlot( i )->isHuman())
			continue;
		req.nick = qmSlotNick( i ).str();
		TheGameSpyPeerMessageQueue->addRequest( req );
	}
	DEBUG_LOG(("ONLINE MAP CHECK sent seed=%d crc=%X\n", seed, TheGameSpyGame->getMapCRC()));
}

/** Calls the match off and frees the screen for another search. The server hears it as a leave inside its
	grace, a no contest: unheard, the player stayed in the match until the next search, past the grace, and
	the silent seat's lone win made them the abandoning loser. label is shown with slotNum's name. With label
	NULL the server's own qm.status is why, and its case in the update loop says so and sets the buttons. */
static void qmAbortMatch( const char *label, Int slotNum, const char *reason )
{
	s_mapCheckPending = FALSE;
	s_peerMapCRC.clear();
	OnlineCallOffQuickMatch( reason );
	if (label)
	{
		UnicodeString text;
		text.format( TheGameText->fetch( label ), TheGameSpyGame->getGameSpySlot( slotNum )->getName().str() );
		GSMessageBoxOk( TheGameText->fetch( "GUI:OnlineTabQuickMatch" ), text );
	}
	TheGameSpyGame->reset();
	if (!label)
		return;
	buttonWiden->winEnable( FALSE );
	enableOptionsGadgets( TRUE );
}

/// TRUE once the game started, which may take the screen away
static Bool qmMapCheckTick( void )
{
	if (!s_mapCheckPending)
		return FALSE;
	const UnsignedInt ours = TheGameSpyGame->getMapCRC();
	const Int localSlot = TheGameSpyGame->getLocalSlotNum();
	Int silent = -1;
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		if (i == localSlot || !TheGameSpyGame->getGameSpySlot( i )->isHuman())
			continue;
		AsciiString key = qmSlotNick( i );
		key.toLower();
		std::map<AsciiString, std::pair<Int, UnsignedInt> >::const_iterator it = s_peerMapCRC.find( key );
		if (it == s_peerMapCRC.end() || it->second.first != s_mapCheckSeed)
		{
			if (silent < 0)
				silent = i;
			continue;
		}
		if (it->second.second != ours)
		{
			DEBUG_LOG(("ONLINE MAP CHECK mismatch slot=%d ours=%X theirs=%X, match called off\n", i, ours, it->second.second));
			qmAbortMatch( "GUI:QMMapDiffers", i, "mapdiffers" );
			return FALSE;
		}
	}
	if (silent >= 0)
	{
		if ((Int)(Clock_Milliseconds() - s_mapCheckUntil) < 0)
			return FALSE;
		DEBUG_LOG(("ONLINE MAP CHECK slot=%d never answered, match called off\n", silent));
		qmAbortMatch( "GUI:QMMapNoAnswer", silent, "mapnoanswer" );
		return FALSE;
	}
	s_mapCheckPending = FALSE;
	s_peerMapCRC.clear();
	DEBUG_LOG(("ONLINE MAP CHECK every seat has crc=%X\n", ours));
	qmLaunchMatched();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu update method */
//-------------------------------------------------------------------------------------------------
void WOLQuickMatchMenuUpdate( WindowLayout * layout, void *userData)
{
	if (TheGameLogic->isInShellGame() && TheGameLogic->getFrame() == 1)
	{
		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_ENTERED_FROM_GAME);
	}

	// We'll only be successful if we've requested to 
	if(isShuttingDown && TheShell->isAnimFinished()&& TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}
	
	/// @todo: MDC handle disconnects in-game the same way as Custom Match!

	if (TheShell->isAnimFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		HandleBuddyResponses();
		HandlePersistentStorageResponses();

		if (TheGameSpyGame && TheGameSpyGame->isGameInProgress())
		{
			if (TheGameSpyInfo->isDisconnectedAfterGameStart(NULL))
			{
				return; // already been disconnected, so don't worry.
			}

			Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
			Bool sawImportantMessage = FALSE;
			PeerResponse resp;
			while (allowedMessages-- && !sawImportantMessage && TheGameSpyPeerMessageQueue->getResponse( resp ))
			{
				switch (resp.peerResponseType)
				{
				case PeerResponse::PEERRESPONSE_DISCONNECT:
					{
						sawImportantMessage = TRUE;
						AsciiString disconMunkee;
						disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);

						// check for scorescreen
						NameKeyType listboxChatWindowScoreScreenID = NAMEKEY("ScoreScreen.wnd:ListboxChatWindowScoreScreen");
						GameWindow *listboxChatWindowScoreScreen = TheWindowManager->winGetWindowFromId( NULL, listboxChatWindowScoreScreenID );
						if (listboxChatWindowScoreScreen)
						{
							GadgetListBoxAddEntryText(listboxChatWindowScoreScreen, TheGameText->fetch(disconMunkee),
								GameSpyColor[GSCOLOR_DEFAULT], -1);
						}
						else
						{
							// still ingame
							TheInGameUI->message(disconMunkee);
						}
						TheGameSpyInfo->markAsDisconnectedAfterGameStart(resp.discon.reason);
					}
				}
			}

			return; // if we're in game, all we care about is if we've been disconnected from the chat server
		}

		if (TheNAT != NULL) {
			NATStateType NATState = TheNAT->update();
			if (NATState == NATSTATE_DONE)
			{
				TheGameSpyGame->launchGame();
				if (TheGameSpyInfo) // this can be blown away by a disconnect on the map transfer screen
					TheGameSpyInfo->leaveStagingRoom();
				return; // don't do any more processing this frame, in case the screen goes away
			}
			else if (NATState == NATSTATE_FAILED)
			{
				// delete TheNAT, its no good for us anymore.
				delete TheNAT;
				TheNAT = NULL;

				// Just back out.  This cleans up some slot list problems
				buttonPushed = true;
				GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:NATNegotiationFailed"));
				nextScreen = NULL;	// back to the hub under this page
				TheShell->pop();
				return; // don't do any more processing this frame, in case the screen goes away
			}
		}

		// -quickmatch: press Start once per process, the queue's own button and settings
		static Bool s_onlineQueued = FALSE;
		if (OnlineIsEnabled() && TheGlobalData->m_quickMatchOnStart && !s_onlineQueued && !s_inQM
			&& buttonStart && !buttonStart->winIsHidden() && buttonStart->winGetEnabled())
		{
			s_onlineQueued = TRUE;
			DEBUG_LOG(("ONLINE QUEUE start\n"));
			TheWindowManager->winSendSystemMsg( parentWOLQuickMatch, GBM_SELECTED, (WindowMsgData)buttonStart, buttonStartID );
		}

		if (OnlineIsEnabled())
			onlineQuickMatchTick();
		if (qmMapCheckTick())
			return; // the game started, and the screen may be going away

#ifdef PERF_TEST
		UnsignedInt start = Clock_Milliseconds();
		UnsignedInt end = Clock_Milliseconds();
		std::list<Int> responses;
		Int numMessages = 0;
#endif // PERF_TEST

		Int allowedMessages = TheGameSpyInfo->getMaxMessagesPerUpdate();
		Bool sawImportantMessage = FALSE;
		PeerResponse resp;
		while (allowedMessages-- && !sawImportantMessage && TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
#ifdef PERF_TEST
			++numMessages;
			responses.push_back(resp.peerResponseType);
#endif // PERF_TEST
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_PLAYERUTM:
				{
					if (!strcasecmp(resp.command.c_str(), "MC"))
						qmPeerMapArrived(resp.nick, resp.commandOptions);
					if (!strcasecmp(resp.command.c_str(), "STATS"))
					{
						DEBUG_LOG(("Saw STATS from %s, data was '%s'\n", resp.nick.c_str(), resp.commandOptions.c_str()));
						AsciiString data = resp.commandOptions.c_str();
						AsciiString idStr;
						data.nextToken(&idStr, " ");
						Int id = atoi(idStr.str());
						DEBUG_LOG(("data: %d(%s) - '%s'\n", id, idStr.str(), data.str()));

						PSPlayerStats stats = TheGameSpyPSMessageQueue->parsePlayerKVPairs(data.str());
						PSPlayerStats oldStats = TheGameSpyPSMessageQueue->findPlayerStatsByID(id);
						stats.id = id;
						DEBUG_LOG(("Parsed ID is %d, old ID is %d\n", stats.id, oldStats.id));
						if (stats.id && (oldStats.id == 0))
							TheGameSpyPSMessageQueue->trackPlayerStats(stats);

						// now fill in the profileID in the game slot
						AsciiString nick = resp.nick.c_str();
						for (Int i=0; i<MAX_SLOTS; ++i)
						{
							GameSpyGameSlot *slot = TheGameSpyGame->getGameSpySlot(i);
							if (slot && slot->isHuman() && (slot->getLoginName().compareNoCase(nick) == 0))
							{
								slot->setProfileID(id);
								break;
							}
						}
					}
					Int slotNum = TheGameSpyGame->getSlotNum(resp.nick.c_str());
					if ((slotNum >= 0) && (slotNum < MAX_SLOTS) && (!strcasecmp(resp.command.c_str(), "NAT"))) {
						// this is a command for NAT negotiations, pass if off to TheNAT
						sawImportantMessage = TRUE;
						if (TheNAT != NULL) {
							TheNAT->processGlobalMessage(slotNum, resp.commandOptions.c_str());
						}
					}
					/*
					else if (key == "NAT")
					{
						if ((val >= FirewallHelperClass::FIREWALL_TYPE_SIMPLE) &&
								(val <= FirewallHelperClass::FIREWALL_TYPE_DESTINATION_PORT_DELTA))
						{
							slot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)val);
							DEBUG_LOG(("Setting NAT behavior to %d for player %d\n", val, slotNum));
							change = true;
						}
						else
						{
							DEBUG_LOG(("Rejecting invalid NAT behavior %d from player %d\n", val, slotNum));
						}
					}
					*/
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
					TheGameSpyInfo->reset();
					// the page is leaving: no key or button may act on it while it fades
					buttonPushed = true;
					TheShell->pop();
				}
            break; // LORENZEN ADDED. SORRY IF THIS "BREAKS" IT...


			case PeerResponse::PEERRESPONSE_JOINGROUPROOM:
				/*
				if (resp.joinGroupRoom.ok)
				{
					TheGameSpyInfo->addText(UnicodeString(L"Joined group room"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				else
				{
					TheGameSpyInfo->addText(UnicodeString(L"Didn't join group room"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				*/
				break;
			case PeerResponse::PEERRESPONSE_PLAYERJOIN:
				{
					//UnicodeString str;
					//str.format(L"Player %hs joined the room", resp.nick.c_str());
					//TheGameSpyInfo->addText(str, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				break;
			case PeerResponse::PEERRESPONSE_PLAYERLEFT:
				{
					//UnicodeString str;
					//str.format(L"Player %hs left the room", resp.nick.c_str());
					//TheGameSpyInfo->addText(str, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				break;
			case PeerResponse::PEERRESPONSE_MESSAGE:
				{
					//UnicodeString m;
					//m.format(L"[%hs]: %ls", resp.nick.c_str(), resp.text.c_str());
					//TheGameSpyInfo->addText(m, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				break;


// LORENZEN EXPRESSES DOUBT ABOUT THIS ONE, AS IT MAY HAVE SUFFERED MERGE MANGLING... SORRY
            // I THINK THIS IS THE OBSOLETE VERSION... SEE THE NEWER LOOKING ONE ABOVE
/*
  			case PeerResponse::PEERRESPONSE_DISCONNECT:
  				{
  					UnicodeString title, body;
  					AsciiString disconMunkee;
  					disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
   				title = TheGameText->fetch( "GUI:GSErrorTitle" );
  					body = TheGameText->fetch( disconMunkee );
  					GameSpyCloseAllOverlays();
  					GSMessageBoxOk( title, body );
  					TheGameSpyInfo->reset();
  					TheShell->pop();
  				}
*/



			case PeerResponse::PEERRESPONSE_CREATESTAGINGROOM:
				{
					if (resp.createStagingRoom.result == PEERJoinSuccess)
					{
						// Woohoo!  On to our next screen!
						UnicodeString str;
						str.format(u"Created staging room");
						TheGameSpyInfo->addText(str, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
					}
					else
					{
						UnicodeString s;
						s.format(u"createStagingRoom result: %d", resp.createStagingRoom.result);
						TheGameSpyInfo->addText( s, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow );
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_JOINSTAGINGROOM:
				{
					if (resp.joinStagingRoom.ok == PEERTrue)
					{
						// Woohoo!  On to our next screen!
						UnicodeString s;
						s.format(u"joinStagingRoom result: %d", resp.joinStagingRoom.ok);
						TheGameSpyInfo->addText( s, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow );
					}
					else
					{
						UnicodeString s;
						s.format(u"joinStagingRoom result: %d", resp.joinStagingRoom.ok);
						TheGameSpyInfo->addText( s, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow );
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_STAGINGROOM:
				{
					UnicodeString str;
					str.format(u"Staging room list callback", resp.nick.c_str());
					TheGameSpyInfo->addText(str, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				break;
			case PeerResponse::PEERRESPONSE_QUICKMATCHSTATUS:
				{
					sawImportantMessage = TRUE;
					// an opponent announced, or a match in its map check, that this status may take back
					const Bool matchHeld = s_revealShown || s_mapCheckPending;
					// Any other word from the queue while the map check waits (searching again, left, locked,
					// refused) means the server no longer holds us in that match: drop the check before the
					// tick launches it against a seat the server let go, and let the case below say why.
					if (s_mapCheckPending && resp.qmStatus.status != QM_MATCHED)
					{
						DEBUG_LOG(("ONLINE MAP CHECK dropped by qm status %d\n", (Int)resp.qmStatus.status));
						qmAbortMatch( NULL, 0, "qmstatus" );
					}
					switch( resp.qmStatus.status )
					{
					case QM_IDLE:
						//TheGameSpyInfo->addText(UnicodeString(L"Status: QM_IDLE"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_JOININGQMCHANNEL:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:JOININGQMCHANNEL"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_LOOKINGFORBOT:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:LOOKINGFORBOT"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_SENTINFO:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:SENTINFO"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_WORKING:
						{
							if (matchHeld)
							{
								// searching again after a reveal or in the map check: the opponent left before the
								// match started
								s_revealShown = FALSE;
								forgetReveal();
								GSMessageBoxOk(TheGameText->fetch("GUI:OnlineTabQuickMatch"), TheGameText->fetch("GUI:QMOpponentLeft"));
							}
							// the server holds us in the queue again after a match the map check called off; a
							// searching status still in flight when Stop went out does not bring the search back
							if (!s_searchSince && !s_stopPending)
								s_searchSince = max( 1u, Clock_Milliseconds() );
							s_poolSize = resp.qmStatus.poolSize;
							// -online shows the pool on the search page, beside the clock
							if (!OnlineIsEnabled())
							{
								UnicodeString s;
								s.format(TheGameText->fetch("QM:WORKING"), resp.qmStatus.poolSize);
								TheGameSpyInfo->addText(s, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
							}
						}
						buttonWiden->winEnable( TRUE );
						break;
					case QM_POOLSIZE:
						{
							s_poolSize = resp.qmStatus.poolSize;
							if (!OnlineIsEnabled())
							{
								UnicodeString s;
								s.format(TheGameText->fetch("QM:POOLSIZE"), resp.qmStatus.poolSize);
								TheGameSpyInfo->addText(s, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
							}
						}
						break;
					case QM_WIDENINGSEARCH:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:WIDENINGSEARCH"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						buttonWiden->winEnable( FALSE );
						break;
					case QM_MATCHED:
						{
							onlineSearchStopped();
							TheGameSpyInfo->addText(TheGameText->fetch("QM:MATCHED"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
							buttonWiden->winEnable( FALSE );

							TheGameSpyGame->enterGame();
							TheGameSpyGame->setSeed(resp.qmStatus.seed);

							TheGameSpyGame->markGameAsQM();

							const LadderInfo *info = getLadderInfo();
							if (!info)
							{
								TheGameSpyGame->setLadderIP("localhost");
								TheGameSpyGame->setLadderPort(0);
							}
							else
							{
								TheGameSpyGame->setLadderIP(info->address);
								TheGameSpyGame->setLadderPort(info->port);
							}

							Int i;
							Int numPlayers = 0;
							for (i=0; i<MAX_SLOTS; ++i)
							{
								if (!resp.stagingRoomPlayerNames[i].empty())
									++numPlayers;
							}

							std::list<AsciiString> maps = TheGameSpyConfig->getQMMaps();
							if (OnlineIsEnabled())
							{
								/* -online is ranked: every peer generates the same map from the match seed and the seat
									 count, and the server's mapIdx goes unused.  The map starts black and the load screen
									 shows neither it nor a start spot, so nobody knows where anybody is. */
								TheGameSpyGame->setStartShrouded(TRUE);
								AsciiString generated;
								stageRandomMap(RandomMapGenerator::quickMatchSettings(resp.qmStatus.seed, numPlayers), generated);
								generated.toLower();
								TheMapCache->updateCache();
								/* No fallback map: QM peers never compare MC=, so a machine that played another map
									 would split the match with nothing to refuse it.  A map that did not build stays
									 set by name, and launchGame finds no such map and leaves the match with its
									 "could not transfer map" box. */
								TheGameSpyGame->setMap(generated);
								if (TheMapCache->findMap(generated) == NULL)
									DEBUG_LOG(("ONLINE MATCHED no generated map: seed=%d players=%d map=%s, leaving the match\n",
										resp.qmStatus.seed, numPlayers, generated.str()));
								maps.clear();
							}
							for (std::list<AsciiString>::const_iterator it = maps.begin(); it != maps.end(); ++it)
							{
								AsciiString theMap = *it;
								theMap.toLower();
								const MapMetaData *md = TheMapCache->findMap(theMap);
								if (md && md->m_numPlayers >= numPlayers)
								{
									TheGameSpyGame->setMap(*it);
									if (resp.qmStatus.mapIdx-- == 0)
										break;
								}
							}

							Int numPlayersPerTeam = numPlayers/2;
							DEBUG_ASSERTCRASH(numPlayersPerTeam, ("0 players per team???"));
							if (!numPlayersPerTeam)
								numPlayersPerTeam = 1;

							for (i=0; i<MAX_SLOTS; ++i)
							{
								GameSpyGameSlot *slot = TheGameSpyGame->getGameSpySlot(i);
								if (resp.stagingRoomPlayerNames[i].empty())
								{
									slot->setState(SLOT_CLOSED);
								}
								else
								{
									AsciiString aName = resp.stagingRoomPlayerNames[i].c_str();
									UnicodeString uName;
									uName.translate(aName);
									slot->setState(SLOT_PLAYER, uName, resp.qmStatus.IP[i]);
									slot->setColor(resp.qmStatus.color[i]);
									slot->setPlayerTemplate(resp.qmStatus.side[i]);
									//slot->setProfileID(0);
									slot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)resp.qmStatus.nat[i]);
									slot->setSkins(resp.qmStatus.skins[i]);	// the server's word for every seat, its own included
									slot->setLocale("");
									slot->setTeamNumber( i/numPlayersPerTeam );
									if (i==0)
										TheGameSpyGame->setGameName(uName);
								}
							}

							if (OnlineIsEnabled())
							{
								// the options string's MC= and MS=, off the map every peer staged
								AsciiString lowerMap = TheGameSpyGame->getMap();
								lowerMap.toLower();
								const MapMetaData *md = TheMapCache->findMap(lowerMap);
								if (md)
								{
									TheGameSpyGame->setMapCRC(md->m_CRC);
									TheGameSpyGame->setMapSize(md->m_filesize);
								}
							}

							DEBUG_LOG(("Starting a QM game: options=[%s]\n", GameInfoToAsciiString(TheGameSpyGame).str()));
							if (OnlineIsEnabled())
								DEBUG_LOG(("ONLINE MATCHED seed=%d players=%d map=%s\n", resp.qmStatus.seed, numPlayers, TheGameSpyGame->getMap().str()));
							// -online starts once every seat has said it built the same map; qmMapCheckTick launches
							if (OnlineIsEnabled())
								qmBeginMapCheck(resp.qmStatus.seed);
							else
								qmLaunchMatched();
						}
						break;
					case QM_INCHANNEL:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:INCHANNEL"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_NEGOTIATINGFIREWALLS:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:NEGOTIATINGFIREWALLS"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_STARTINGGAME:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:STARTINGGAME"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						break;
					case QM_COULDNOTFINDBOT:
						// -online: the server would not queue us.  The abandon lock counts down on the button row,
						// Search disabled under it; a match already holding us or a server error gets the box
						// maintenance has its own words: the service is closing, not this player's fault
						if (OnlineIsEnabled() && !OnlineShowMaintenanceRefusal() && qmLockSeconds() == 0)
							GSMessageBoxOk(TheGameText->fetch("GUI:OnlineTabQuickMatch"), TheGameText->fetch("GUI:QMRefused"));
						TheGameSpyInfo->addText(TheGameText->fetch("QM:COULDNOTFINDBOT"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						buttonWiden->winEnable( FALSE );
						enableOptionsGadgets(TRUE);
						break;
					case QM_COULDNOTFINDCHANNEL:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:COULDNOTFINDCHANNEL"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						buttonWiden->winEnable( FALSE );
						enableOptionsGadgets(TRUE);
						break;
					case QM_COULDNOTNEGOTIATEFIREWALLS:
						TheGameSpyInfo->addText(TheGameText->fetch("QM:COULDNOTNEGOTIATEFIREWALLS"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						buttonWiden->winEnable( FALSE );
						enableOptionsGadgets(TRUE);
						break;
					case QM_STOPPED:
						OnlineShowMaintenanceRefusal();		// maintenance took us out of the queue, unasked
						TheGameSpyInfo->addText(TheGameText->fetch("QM:STOPPED"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
						buttonWiden->winEnable( FALSE );
						enableOptionsGadgets(TRUE);
						break;
					}
				}
				break;
			}
		}
#ifdef PERF_TEST
		// check performance
		end = Clock_Milliseconds();
		UnsignedInt frameTime = end-start;
		if (frameTime > 100 || responses.size() > 20)
		{
			UnicodeString munkee;
			munkee.format(u"inQM:%d %d ms, %d messages", s_inQM, frameTime, responses.size());
			TheGameSpyInfo->addText(munkee, GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
			PERF_LOG(("%s\n", WideCharAsUtf8( munkee.str() ).str()));

			std::list<Int>::const_iterator it;
			for (it = responses.begin(); it != responses.end(); ++it)
			{
				PERF_LOG(("  %s\n", getMessageString(*it)));
			}
		}
#endif // PERF_TEST
		// Search follows the abandon lock in and out, which counts down here and not in a status
		if (OnlineIsEnabled() && buttonStart->winGetEnabled() == (qmLockSeconds() > 0))
			UpdateStartButton();
		showPageState();
	}
}// WOLQuickMatchMenuUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLQuickMatchMenuInput( GameWindow *window, UnsignedInt msg,
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
						if(buttonBack && !buttonBack->winIsHidden())
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
}// WOLQuickMatchMenuInput

/** Leaving the page, by Back (Home) or a tab.  -online: the search stops first, or the server pairs us
	* and the opponent loads against nobody.  The hub stays at the stack's foot: this page pops, and its
	* shutdown pushes a tab's page over the hub. */
static void qmLeave( UIStyle::OnlineTab to )
{
	if (OnlineIsEnabled() && (s_searchSince || s_revealShown))
	{
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_STOPQUICKMATCH;
		TheGameSpyPeerMessageQueue->addRequest(req);
		onlineSearchStopped();
		s_stopPending = TRUE;
	}
	buttonPushed = true;
	TheGameSpyInfo->leaveGroupRoom();
	nextScreen = UIStyle::onlineTabScreen( to );
	TheShell->pop();
}

//-------------------------------------------------------------------------------------------------
/** WOL Quick Match Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLQuickMatchMenuSystem( GameWindow *window, UnsignedInt msg, 
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
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				Int pos = -1;
				GadgetComboBoxGetSelectedPos(control, &pos);

				saveQuickMatchOptions();
				if (controlID == comboBoxLadderID && !isPopulatingLadderBox)
				{
					if (pos >= 0)
					{
						QuickMatchPreferences pref;
						Int ladderID = (Int)(intptr_t)GadgetComboBoxGetItemData(control, pos);
						if (ladderID == 0)
						{
							// no ladder selected - enable buttons
							GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, max(0, pref.getNumPlayers()/2-1));
							comboBoxNumPlayers->winEnable( TRUE );
							populateQMSideComboBox(pref.getSide()); // this will set side to random and disable if necessary
						}
						else if (ladderID > 0)
						{
							// ladder selected - disable buttons
							const LadderInfo *li = TheLadderList->findLadderByIndex(ladderID);
							if (li)
								GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, li->playersPerTeam-1);
							else
								GadgetComboBoxSetSelectedPos(comboBoxNumPlayers, 0);
							comboBoxNumPlayers->winEnable( FALSE );

							populateQMSideComboBox(pref.getSide(), li); // this will set side to random and disable if necessary
						}
						else
						{
							// "Choose a ladder" selected - open overlay
							PopulateQMLadderComboBox(); // this restores the non-"Choose a ladder" selection
							GameSpyOpenOverlay( GSOVERLAY_LADDERSELECT );
						}
					}
				}
				if (!isInInit)
				{
					QuickMatchPreferences pref;
					populateQuickMatchMapSelectListbox(pref);
					UpdateStartButton();
				}
				break;
			} // case GCM_SELECTED

		case GBM_SELECTED:
			{
				// the found page holds a match the server paired: through the reveal and the map check
				// (QM_MAP_CHECK_MS at most) a tab, like the hidden Back, would walk out on the other seats
				if (buttonPushed || qmPageState() == QM_PAGE_FOUND)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// a tab leaves as Back does, the search stopped first
				if ( UIStyle::onlineTabClicked( control, qmLeave ) )
					break;
				// Start and Stop hide only at the next update; a second click before it must not send twice
				if ( controlID == buttonStopID && qmPageState() == QM_PAGE_SEARCH )
				{
					PeerRequest req;
					req.peerRequestType = PeerRequest::PEERREQUEST_STOPQUICKMATCH;
					TheGameSpyPeerMessageQueue->addRequest(req);
					buttonWiden->winEnable( FALSE );
					enableOptionsGadgets(TRUE);
					s_stopPending = TRUE;
					TheGameSpyInfo->addText(TheGameText->fetch("GUI:QMAborted"), GameSpyColor[GSCOLOR_DEFAULT], quickmatchTextWindow);
				}
				else if ( controlID == buttonWidenID )
				{
					PeerRequest req;
					req.peerRequestType = PeerRequest::PEERREQUEST_WIDENQUICKMATCHSEARCH;
					TheGameSpyPeerMessageQueue->addRequest(req);
					buttonWiden->winEnable( FALSE );
				}
				else if ( controlID == buttonStartID && qmPageState() == QM_PAGE_PICK && qmLockSeconds() == 0 )
				{
					PeerRequest req;
					req.peerRequestType = PeerRequest::PEERREQUEST_STARTQUICKMATCH;
					req.qmMaps.clear();
					Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
					if (OnlineIsEnabled())
					{
						/* the service's qm.join maps index its whole pool, the welcome's <QMMaps>.  The match is
							 played on a map generated from its seed and the pool map goes unused, so nothing is
							 vetoed: a veto would only keep the server from pairing two players over a map neither
							 of them plays. */
						std::list<AsciiString> pool = TheGameSpyConfig->getQMMaps();
						for (std::list<AsciiString>::const_iterator it = pool.begin(); it != pool.end(); ++it)
							req.qmMaps.push_back(TRUE);
					}
					else
					{
						for ( Int i=0; i<numMaps; ++i )
						{
							req.qmMaps.push_back(GadgetListBoxGetItemData(listboxMapSelect, i, 0));
						}
					}
					UnicodeString u;
					AsciiString a;
//					u = GadgetTextEntryGetText(textEntryMaxDisconnects);
//					a.translate(u);
//					req.QM.maxDiscons = atoi(a.str());
//					u = GadgetTextEntryGetText(textEntryMaxPoints);
//					a.translate(u);
					req.QM.maxPointPercentage = max(100, maxPoints);
//					u = GadgetTextEntryGetText(textEntryMinPoints);
//					a.translate(u);
					req.QM.minPointPercentage = min(100, minPoints);
					//u = GadgetTextEntryGetText(textEntryWaitTime);
					//a.translate(u);
					//req.QM.widenTime = atoi(a.str());
					req.QM.widenTime = 0;

					Int val;
					GadgetComboBoxGetSelectedPos(comboBoxMaxDisconnects, &val);
					if( val < 0)
						 val = 0;
					req.QM.maxDiscons = MAX_DISCONNECTS[val];

					GadgetComboBoxGetSelectedPos(comboBoxMaxPing, &val);
					if (val < 0)
						val = 0;
					if( val >= maxPingEntries - 1)
					{
						req.QM.maxPing = TheGameSpyConfig->getPingTimeoutInMs();	
					}
					else
						req.QM.maxPing = (val+1)*100;

					PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(TheGameSpyInfo->getLocalProfileID());
					req.QM.points = CalculateRank(stats);

					Int ladderIndex, index, selected;
					GadgetComboBoxGetSelectedPos( comboBoxLadder, &selected );
					ladderIndex = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxLadder, selected );
					const LadderInfo *ladderInfo = NULL;
					if (ladderIndex < 0)
					{
						ladderIndex = 0;
					}
					if (ladderIndex)
					{
						ladderInfo = TheLadderList->findLadderByIndex( ladderIndex );
						if (!ladderInfo)
						{
							ladderIndex = 0; // sanity
						}
					}
					req.QM.ladderID = ladderIndex;

					req.QM.ladderPassCRC = 0;

					index = -1;
					GadgetComboBoxGetSelectedPos( comboBoxSide, &selected );
					if (selected >= 0)
						index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxSide, selected );
					req.QM.side = index;
					if( index == PLAYERTEMPLATE_RANDOM || (ladderInfo && ladderInfo->randomFactions) )
					{
						// Resolve Random now, or else we will get the same pick every darn time.  A forced-random
						// ladder draws too; either way from the listed armies, which are the ladder's valid
						// factions that Quick Match offers, so neither can land on Turkiye.
						std::vector<Int> sides;
						for( Int entry = 0; entry < GadgetComboBoxGetLength(comboBoxSide); ++entry )
						{
							const Int side = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxSide, entry );
							if( side >= 0 )
								sides.push_back( side );
						}
						/* From this machine's own entropy, not the client stream: every shell map start seeds
							 that with InitRandom(0) on every machine, so two copies that had drawn as often since
							 drew the same army. */
						if( !sides.empty() )
						{
							std::random_device entropy;
							req.QM.side = sides[ std::uniform_int_distribution<Int>( 0, (Int)sides.size() - 1 )( entropy ) ];
						}
					}
					// the found page's own card shows the army drawn, as the opponent's shows him
					s_joinedArmy = req.QM.side;

					index = -1;
					GadgetComboBoxGetSelectedPos( comboBoxColor, &selected );
					if (selected >= 0)
						index = (Int)(intptr_t)GadgetComboBoxGetItemData( comboBoxColor, selected );
					req.QM.color = index;

					OptionPreferences natPref;
					req.QM.NAT = natPref.getFirewallBehavior();

					if (ladderIndex)
					{
						req.QM.numPlayers = (ladderInfo)?ladderInfo->playersPerTeam*2 : 2;
					}
					else
					{
						GadgetComboBoxGetSelectedPos(comboBoxNumPlayers, &val);
						if (val < 0)
							val = 0;
						req.QM.numPlayers = (val+1)*2;
					}

					Int numDiscons = 0;
					PerGeneralMap::iterator it;
					for(it =stats.discons.begin(); it != stats.discons.end(); ++it)
					{
						numDiscons += it->second;
					}
					for(it =stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
					{
						numDiscons += it->second;
					}
					req.QM.discons = numDiscons;


					strncpy(req.QM.pings, TheGameSpyInfo->getPingString().str(), 17);
					req.QM.pings[16] = 0;
					
					req.QM.botID = TheGameSpyConfig->getQMBotID();
					req.QM.roomID = TheGameSpyConfig->getQMChannel();

					req.QM.exeCRC = TheGlobalData->m_exeCRC;
					req.QM.iniCRC = TheGlobalData->m_iniCRC;

					TheGameSpyPeerMessageQueue->addRequest(req);
					buttonWiden->winEnable( FALSE );
					enableOptionsGadgets(FALSE);

					if (ladderIndex > 0)
					{
						// save the ladder as being played upon even if we cancel out of matching early...
						LadderPreferences ladPref;
						ladPref.loadProfile( TheGameSpyInfo->getLocalProfileID() );
						LadderPref p;
						p.lastPlayDate = time(NULL);
						p.address = ladderInfo->address;
						p.port = ladderInfo->port;
						p.name = ladderInfo->name;
						ladPref.addRecentLadder( p );
						ladPref.write();
					}
				}
				else if ( controlID == buttonBuddiesID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );
				}
				else if ( controlID == buttonBackID )
				{
					qmLeave( UIStyle::ONLINE_TAB_HOME );
				} //if ( controlID == buttonBack )
				else if ( controlID == buttonSelectAllMapsID )
				{
					Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
					for ( Int i=0; i<numMaps; ++i )
					{
						GadgetListBoxAddEntryImage(listboxMapSelect, selectedImage, i, 0);
						GadgetListBoxSetItemData(listboxMapSelect, (void *)1, i);
						GadgetListBoxAddEntryText(listboxMapSelect, GadgetListBoxGetText(listboxMapSelect, i, 1), GameSpyColor[GSCOLOR_MAP_SELECTED], i, 1);
					}
				} //if ( controlID == buttonSelectAllMapsID )
				else if ( controlID == buttonSelectNoMapsID )
				{
					Int numMaps = GadgetListBoxGetNumEntries(listboxMapSelect);
					for ( Int i=0; i<numMaps; ++i )
					{
						GadgetListBoxAddEntryImage(listboxMapSelect, unselectedImage, i, 0);
						GadgetListBoxSetItemData(listboxMapSelect, (void *)0, i);
						GadgetListBoxAddEntryText(listboxMapSelect, GadgetListBoxGetText(listboxMapSelect, i, 1), GameSpyColor[GSCOLOR_MAP_UNSELECTED], i, 1);
					}
				} //if ( controlID == buttonSelectNoMapsID )
				break;
			}// case GBM_SELECTED:
	
		case GLM_SELECTED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				Int selected = (Int)mData2;

				if ( controlID == listboxMapSelectID )
				{
					const LadderInfo *li = getLadderInfo();
					if (selected >= 0 && (!li || !li->randomMaps))
					{
						Bool wasSelected = (Bool)GadgetListBoxGetItemData(control, selected, 0);
						GadgetListBoxSetItemData(control, (void *)(!wasSelected), selected, 0);
						Int width = 10;
						Int height = 10;
						const Image *img = (!wasSelected)?selectedImage:unselectedImage;
						if ( img )
						{
							width = min(GadgetListBoxGetColumnWidth(control, 0), img->getImageWidth());
							height = width;
						}
						GadgetListBoxAddEntryImage(control, img, selected, 0, height, width);
						GadgetListBoxAddEntryText(control, GadgetListBoxGetText(control, selected, 1), GameSpyColor[(wasSelected)?GSCOLOR_MAP_UNSELECTED:GSCOLOR_MAP_SELECTED], selected, 1);
					}
					if (selected >= 0)
						GadgetListBoxSetSelected(control, -1);
				}
				UpdateStartButton();
				break;
			}// case GLM_SELECTED

		case GEM_EDIT_DONE:
			{
				break;
			}
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// WOLQuickMatchMenuSystem
