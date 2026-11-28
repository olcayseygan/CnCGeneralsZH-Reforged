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

// FILE: ScoreScreenGraphs.cpp ////////////////////////////////////////////////////////////////////
//
// The score screen's whole content after any match, a mission or a replay, in the menu language:
// frame A round the board, the verdict as the title with the match's facts after it, three tabs at
// the header's right, the lit rule and under it the teams' share of the match's score
// (MENU-REDESIGN.md package 13, experiments/proofs/2026-11-24/spirit/c-bridge/README-end3.md).
//
// Summary stands the teams in two wings.  One to four a team are cards (the army's picture, the
// name, the army value over the match, the figures); more are lines; several teams are bands over
// their lines; a free for all is a list of places.  The foot holds the map with every start, what
// stood out of the match (or the chat after a network match, or the online rating card), and the
// key row.  Graphs is one chart of a series picked from a row of chips, drawn as one line a player
// or as each player's share of the whole, with the legend at its right: hover a row to pick its line
// out, click it to hide it; hover the plot for every player's value at that minute.  Build order puts
// each player's pictures on a time axis that folds the stretches where nobody built.
//
// Gold is the local player and nothing else: his name, his row's light, his ring on the map, his line
// on a chart, his frames on the build order.  A winner reads VICTORY in green; a column's best is cyan.
//
// Everything is drawn here from TheMatchHistory, the score keepers and the lobby's slots; the
// decisions that need no window are in ScoreScreenLayout.h and EndScreenLayout.h, where tests walk
// every team split.  The panel is made in code over the retail layout, whose own table ScoreScreen.cpp
// hides.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/MatchHistory.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/EndScreenLayout.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/ScoreScreenLayout.h"
#include "GameClient/UIStyle.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "GameLogic/ApocalypseDirector.h"
#include "GameLogic/AodDirector.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"

#include <algorithm>
#include <vector>

enum GraphTab
{
	TAB_SUMMARY,
	TAB_GRAPHS,
	TAB_BUILD_ORDER,
	TAB_COUNT
};

static const char *const theTabLabels[ TAB_COUNT ] = { "GUI:GraphSummary", "GUI:GraphGraphs", "GUI:GraphBuildOrder" };

/// what the Graphs tab can draw, in the order of its chips
enum ChartSeries
{
	CHART_ARMY_VALUE,
	CHART_INCOME,
	CHART_EARNED,
	CHART_CASH,
	CHART_UNITS,
	CHART_BUILDINGS,
	CHART_POWER,
	CHART_VALUE_DESTROYED,
	CHART_UNITS_LOST,
	CHART_COUNT
};

static const char *const theChartLabels[ CHART_COUNT ] =
{
	"GUI:HudStatArmy", "GUI:HudStatIncome", "GUI:GraphEarned", "GUI:HudStatCash", "GUI:SummaryUnits",
	"GUI:GraphBuildings", "GUI:HudStatPower", "GUI:GraphValueDestroyed", "GUI:GraphUnitsLost"
};

/// the recorded series each chart reads; income is worked out from the money earned
static const MatchSeries theChartSeries[ CHART_COUNT ] =
{
	MATCH_SERIES_ARMY_VALUE, MATCH_SERIES_EARNED, MATCH_SERIES_EARNED, MATCH_SERIES_BANK, MATCH_SERIES_ARMY,
	MATCH_SERIES_BUILDINGS, MATCH_SERIES_POWER, MATCH_SERIES_VALUE_DESTROYED, MATCH_SERIES_LOST
};

/// the standings' columns under four group heads, EndScreen::summaryColumns's order
enum SummaryColumn
{
	COL_SCORE,
	COL_UNITS_BUILT,
	COL_UNITS_LOST,
	COL_UNITS_DESTROYED,
	COL_BUILDINGS_BUILT,
	COL_BUILDINGS_LOST,
	COL_BUILDINGS_DESTROYED,
	COL_EARNED,
	COL_SPENT,
	COL_COUNT
};

static const char *const theColumnLabels[ COL_COUNT ] =
{
	NULL, "GUI:SummaryUnitsBuilt", "GUI:SummaryLost", "GUI:SummaryDestroyed",
	"GUI:SummaryBuildingsBuilt", "GUI:SummaryLost", "GUI:SummaryDestroyed",
	"GUI:SummaryEarned", "GUI:SummarySpent"
};

enum { GROUP_COUNT = 4 };

static const char *const theGroupLabels[ GROUP_COUNT ] = { "GUI:SummaryScore", "GUI:SummaryUnits", "GUI:GraphBuildings", "GUI:SummaryMoney" };

static const Int theColumnGroup[ COL_COUNT ] = { 0, 1, 1, 1, 2, 2, 2, 3, 3 };

/// whether the most (TRUE) or the fewest (FALSE) is a column's best; a lone best is set in cyan
static const Bool theColumnMostIsBest[ COL_COUNT ] = { TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE };

static const char *const theEventLabels[ MATCH_EVENT_TYPE_COUNT ] =
{
	"GUI:GraphSuperweaponBuilt", "GUI:GraphSuperweaponFired", "GUI:GraphGeneralPower", "GUI:GraphPromotion", "GUI:SummaryDefeated"
};

/// which event's picture a group of markers shows, lowest first
static const Int theEventRank[ MATCH_EVENT_TYPE_COUNT ] = { 1, 0, 2, 3, 4 };

/// the lobby's start spot against its map preview (SkirmishGameOptionsMenu.wnd), as the load screen keeps it
static const Int LOBBY_START_SPOT = 28;
static const Int LOBBY_MAP_PREVIEW = 410;
/// numbered rings on the map up to this many players; past it a dot each
static const Int MAP_RINGS_MOST = 8;

/// the 0:00 to 5:00 opening the build order lights, and the lane gap between its pictures
static const UnsignedInt OPENING_FRAMES = 5 * 60 * LOGICFRAMES_PER_SECOND;

/// U+2026, U+00B7 and U+2013 by number: a u"" literal is read in the machine's code page by MSVC
static const WideChar SCORE_ELLIPSIS[] = { 0x2026, 0 };
static const WideChar SCORE_DOT[] = { u' ', 0x00B7, u' ', 0 };

/// the colours the design adds to the language's: the local player's chart line and a dark keyline
static const Color LOCAL_LINE = GameMakeColor( 255, 246, 214, 255 );
static const Color KEYLINE = GameMakeColor( 0, 0, 0, 160 );
static const Color LIGHT_KEY = GameMakeColor( 220, 224, 236, 255 );
static const Color MARK_FILL = GameMakeColor( 6, 8, 12, 230 );

enum FootShows
{
	FOOT_NONE = -1,
	FOOT_HIGHLIGHTS,
	FOOT_CHAT,
	FOOT_CARD
};

struct GraphPlayer
{
	Int index;					///< the player's index, or -1 for a campaign's side, which has numbers and no lines
	Player *player;			///< read while the screen is set up only
	Int slot;						///< the lobby seat, -1 outside a lobby match
	UnicodeString name;	///< numbered where two seats share one: "Hard AI (2)"
	UnicodeString side;
	const Image *icon;
	const Image *portrait;	///< the army's Command Center, the card's picture
	Color color;
	Color nameColor;		///< the colour lifted where it is too dark to read as words
	Bool winner;
	Bool disconnected;
	Bool local;					///< the player at this machine, the one gold marks
	UnsignedInt fell;		///< the frame he was eliminated on, SCORE_STILL_STANDING if never
	Int team;						///< his team in s_order
	Int stats[ COL_COUNT ];
};

/// words on the panel; tracked capitals keep their words and are drawn letter by letter
struct GraphText
{
	DisplayString *text;
	UnicodeString words;
	GameFont *font;
	Real tracking;
	Int x;
	Int y;
	Color color;
};

struct GraphTipLine
{
	UnicodeString text;
	UnicodeString value;		///< a number right-aligned in its own column, or empty
	Color color;
};

struct GraphDrop
{
	Int x;
	Color color;
};

/** A box: a fill, a frame, a picture, a group of event markers on a chart or a build in the build
	* order; the last two carry a tooltip. */
struct GraphMark
{
	IRegion2D box;
	const Image *image;
	Color edge;
	Real edgeWidth;
	Color fill;
	Int rank;							///< theEventRank of the event whose picture it shows
	DisplayString *label;	///< the rank number on a promotion
	DisplayString *count;	///< how many events share the marker, when more than one
	std::vector< GraphTipLine > tip;
	std::vector< GraphDrop > drops;	///< a chart marker's lines down to the plot, drawn on hover
	Int dropBottom;
};

struct GraphLine
{
	Int owner;						///< the player's index
	Color color;
	Bool cross;						///< he fell where the line stops
	Bool local;						///< the local player's: white-gold inside a gold edge
	std::vector< ICoord2D > points;
};

/// a filled rectangle that belongs to a player: a share band's column, or a team divider (owner -1)
struct GraphBand
{
	Int owner;
	Color color;
	IRegion2D box;
};

struct GraphRule
{
	IRegion2D line;
	Color color;
	Real width;
};

/// a line with its soft light: the header rule, a team's bar, the share line
struct GraphLit
{
	IRegion2D box;
	Color color;
};

/// a row and its state: the local player's gold, a white lift and a cyan edge under the pointer
struct GraphRow
{
	IRegion2D box;
	Bool local;
	Bool zebra;
	Bool hovers;
};

/// a start spot on the map
struct GraphSpot
{
	ICoord2D pos;
	Color color;
	DisplayString *number;		///< NULL past MAP_RINGS_MOST players: a dot
	Bool dimmed;							///< he was defeated
	Bool local;
};

enum GraphHitKind
{
	HIT_TAB,
	HIT_CHART,
	HIT_SHARE,
	HIT_EVERY,
	HIT_LEGEND,
	HIT_CHAT
};

/// something the mouse can click: a tab, a chip, a toggle drawn as a chip, a legend row or the chat key
struct GraphHit
{
	IRegion2D box;
	GraphHitKind kind;
	Int value;
	Bool open;
	DisplayString *label;		///< NULL for a legend row, which is drawn with the rows
};

/// the chart's plot, for the crosshair
struct GraphPlot
{
	Bool live;
	IRegion2D box;
	Int chart;
	Bool share;
	std::vector< Int > sampleX;
};

/// the Summary's columns on the board
struct GraphColumns
{
	Int left[ COL_COUNT ];
	Int right[ COL_COUNT ];
	Bool dense;
};

/** The match's history as the screen opened.  TheMatchHistory itself is reset and filled again by
	* whatever map runs next (the shell's) while this screen can still be up, so a tab picked late read
	* that map's first seconds as one flat sample. */
static MatchHistory s_history;
static GameWindow *s_panel = NULL;
static Int s_tab = TAB_SUMMARY;
static Bool s_everyBuild = FALSE;
static Int s_chart = CHART_ARMY_VALUE;
static Bool s_share = FALSE;
static Bool s_hidden[ MAX_PLAYER_COUNT ];
static std::vector< GraphPlayer > s_players;
static std::vector< ScoreTeam > s_order;		///< teams in standings order, members as indexes into s_players
static std::vector< Int > s_teamNumber;			///< the number a team is called by, per s_order entry
static std::vector< Color > s_teamColor;		///< its swatch: the lobby team's colour, or the no-team slate
static Bool s_banded = FALSE;								///< every team carries a band (ScoreScreen_banded)
static Bool s_freeForAll = FALSE;
static Bool s_prepared = FALSE;
static Bool s_anyWinner = FALSE;
static Bool s_campaign = FALSE;
static Bool s_apocalypse = FALSE;		///< an Apocalypse match: no winner, every verdict a time held
static Bool s_watching = FALSE;			///< a replay, an observer or nobody at this machine
static std::vector< GraphText > s_texts;
static std::vector< GraphMark > s_marks;
static std::vector< GraphLine > s_lines;
static std::vector< GraphBand > s_bands;
static std::vector< GraphBand > s_dividers;		///< the ink lines between teams, over the bands
static std::vector< GraphRule > s_rules;
static std::vector< GraphLit > s_lit;
static std::vector< GraphRow > s_rows;
static std::vector< GraphSpot > s_spots;
static std::vector< GraphHit > s_hits;
static GraphPlot s_plot;
static UnicodeString s_mapName;						///< the match's map, or the mission's place
static UnicodeString s_format;						///< "4v4", "Free for all (5)", or empty
static const char *s_subtitleKey = NULL;	///< -online: the words round the format and the length (ScoreScreenGraphsSubtitle)
static UnicodeString s_verdict;						///< VICTORY, DEFEAT, TEAM 2 WINS, NO WINNER, SURVIVED 24:10
static const Image *s_mapImage = NULL;
static Bool s_mapShown = FALSE;
static IRegion2D s_mapBox;
static IRegion2D s_mapDrawn;
static std::vector< DisplayString * > s_tipText;
static std::vector< DisplayString * > s_tipValue;	///< NULL where a line has no number
static Int s_tipKey = -1;
static Int s_keys = 1;										///< the keys ScoreScreen.cpp stood on the key row
static Bool s_chatAvailable = FALSE;			///< a network match's chat, which the foot can show
static Bool s_cardAvailable = FALSE;			///< -online: the rating card, which the foot shows first
static Int s_foot = FOOT_HIGHLIGHTS;
static Real s_footArea[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };

// the faces, every one Arial at a size given at 1080 (UIStyle::menuFont)
static GameFont *s_fTitle = NULL;			///< 40 bold: the verdict, a free for all's place
static GameFont *s_fFigure = NULL;		///< 40: a team's score
static GameFont *s_fFacts = NULL;			///< 20: the facts after the title, a highlight's name
static GameFont *s_fTeam = NULL;			///< 22 bold: TEAM n in its head
static GameFont *s_fName = NULL;			///< 24: a card's name and its lead figure
static GameFont *s_fNameTall = NULL;	///< 28: the same on a card over 200 tall
static GameFont *s_fLead = NULL;			///< 20: a line's lead figure
static GameFont *s_fBody = NULL;			///< 18: names on lines, figures, facts
static GameFont *s_fBodyBold = NULL;	///< 18 bold: place numbers, PLACES a-b
static GameFont *s_fSub = NULL;				///< 16: the general, peak, the speed and money facts
static GameFont *s_fBand = NULL;			///< 17 bold: TEAM n in a band
static GameFont *s_fCaps = NULL;			///< 14 bold: column words, chips' and the verdict's capitals
static GameFont *s_fSmall = NULL;			///< 14: axis labels, the general on a line
static GameFont *s_fHighlight = NULL;	///< 15 bold: a highlight's label
static GameFont *s_fHighlightBig = NULL;	///< 26: a highlight's name in the wide foot

//-------------------------------------------------------------------------------------------------
/** A 1920x1080 board length or point as the panel's own pixels.  The panel stands on frame A's
	* rectangle, 32,28 to 1888,1052. */
static Int lx( Real board ) { return UIStyle::px( board - (Real)EndScreen::PANEL_LEFT ); }
static Int ly( Real board ) { return UIStyle::px( board - (Real)EndScreen::PANEL_TOP ); }
static Int ll( Real length ) { return UIStyle::px( length ); }
/// a width on the screen as a length on the board
static Real bw( Int screen ) { return (Real)screen * 1000.0f / (Real)max( UIStyle::px( 1000.0f ), 1 ); }

static void pickFonts( void )
{
	s_fTitle = UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE, TRUE );
	s_fFigure = UIStyle::menuFont( UIStyle::MENU_TEXT_TEAM_FIGURE );
	s_fFacts = UIStyle::menuFont( 20 );
	s_fTeam = UIStyle::menuFont( 22, TRUE );
	s_fName = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE );
	s_fNameTall = UIStyle::menuFont( 28 );
	s_fLead = UIStyle::menuFont( 20 );
	s_fBody = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	s_fBodyBold = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY, TRUE );
	s_fSub = UIStyle::menuFont( 16 );
	s_fBand = UIStyle::menuFont( 17, TRUE );
	s_fCaps = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	s_fSmall = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL );
	s_fHighlight = UIStyle::menuFont( 15, TRUE );
	s_fHighlightBig = UIStyle::menuFont( 26 );
}

static UnicodeString clockText( UnsignedInt frame )
{
	const UnsignedInt seconds = frame / LOGICFRAMES_PER_SECOND;
	UnicodeString text;
	if( seconds >= 3600 )
		text.format( u"%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60 );
	else
		text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

/// an axis label: 12k past ten thousand
static UnicodeString valueText( Int value )
{
	UnicodeString text;
	if( value >= 10000 )
		text.format( u"%dk", value / 1000 );
	else
		text.format( u"%d", value );
	return text;
}

static UnicodeString moneyText( Int value )
{
	UnicodeString money( u"$" );
	money.concat( UIStyle::grouped( value ) );
	return money;
}

static UnicodeString fetchText( const char *label )
{
	return TheGameText->fetch( label );
}

static DisplayString *newText( const UnicodeString &text, GameFont *font )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( font );
	string->setText( text );
	return string;
}

static void freeText( DisplayString *&string )
{
	if( string )
		TheDisplayStringManager->freeDisplayString( string );
	string = NULL;
}

static Int textWidth( DisplayString *string )
{
	Int width, height;
	string->getSize( &width, &height );
	return width;
}

static Int measure( const UnicodeString &text, GameFont *font )
{
	DisplayString *string = newText( text, font );
	const Int width = textWidth( string );
	TheDisplayStringManager->freeDisplayString( string );
	return width;
}

/** text as it fits widest pixels in font: whole, or as many of its first characters as fit with an
	* ellipsis after them (ObserverCamera_fitCount), spaces before the ellipsis dropped.  A skirmish
	* name read from the preferences, or the machine's own name standing in for one, has no length cap. */
static UnicodeString fittedText( const UnicodeString &text, GameFont *font, Int widest )
{
	if( text.isEmpty() )
		return text;
	DisplayString *string = newText( text, font );
	std::vector< Int > prefixWidths( text.getLength() + 1, 0 );
	for( Int count = 1; count <= text.getLength(); count++ )
		prefixWidths[ count ] = string->getWidth( count );
	string->setText( UnicodeString( SCORE_ELLIPSIS ) );
	// never past the first letter, whatever the room
	const Int kept = max( ObserverCamera_fitCount( prefixWidths, textWidth( string ), widest ), 1 );
	TheDisplayStringManager->freeDisplayString( string );
	if( kept >= text.getLength() )
		return text;
	UnicodeString cut = text;
	while( cut.getLength() > kept || ( cut.getLength() > 1 && cut.getCharAt( cut.getLength() - 1 ) == u' ' ) )
		cut.removeLastChar();
	cut.concat( SCORE_ELLIPSIS );
	return cut;
}

/** A label (never a player's name) as it fits widest: whole, its short form, abbreviated by the
	* language's rule, or at last cut with an ellipsis (UIStyle::fitText). */
static UnicodeString fittedLabel( const UnicodeString &text, GameFont *font, Int widest )
{
	return UIStyle::fitText( text, font, max( widest, 1 ) );
}

/** Text in the panel's own pixels, its middle at middle.  align: 0 puts x at the text's left, 1 at
	* its middle, 2 at its right.  Returns the text's width. */
static Int addText( const UnicodeString &text, GameFont *font, Int x, Int middle, Color color, Int align = 0 )
{
	if( text.isEmpty() )
		return 0;
	GraphText entry;
	entry.text = newText( text, font );
	entry.font = font;
	entry.tracking = 0.0f;
	Int width, height;
	entry.text->getSize( &width, &height );
	entry.x = x - width * align / 2;
	entry.y = middle - height / 2;
	entry.color = color;
	s_texts.push_back( entry );
	return width;
}

/// addText at a board point
static Int putText( const UnicodeString &text, GameFont *font, Real x, Real middle, Color color, Int align = 0 )
{
	return addText( text, font, lx( x ), ly( middle ), color, align );
}

/// letter-spaced capitals at a board point (UIStyle::drawTracked), tracking at 1080
static Int putTracked( const UnicodeString &text, GameFont *font, Real x, Real middle, Color color, Real tracking, Int align = 0 )
{
	if( text.isEmpty() )
		return 0;
	GraphText entry;
	entry.text = NULL;
	entry.words = text;
	entry.font = font;
	entry.tracking = tracking;
	const Int width = UIStyle::trackedWidth( text, font, tracking );
	entry.x = lx( x ) - width * align / 2;
	entry.y = ly( middle ) - font->height / 2;
	entry.color = color;
	s_texts.push_back( entry );
	return width;
}

static void addRule( Int x0, Int y0, Int x1, Int y1, Color color, Real width = 1.0f )
{
	GraphRule rule;
	rule.line.lo.x = x0;
	rule.line.lo.y = y0;
	rule.line.hi.x = x1;
	rule.line.hi.y = y1;
	rule.color = color;
	rule.width = width;
	s_rules.push_back( rule );
}

static void putRule( Real x0, Real y0, Real x1, Real y1, Color color )
{
	addRule( lx( x0 ), ly( y0 ), lx( x1 ), ly( y1 ), color );
}

static GraphMark newMark( Int x, Int y, Int width, Int height, Color fill, Color edge )
{
	GraphMark mark;
	mark.box.lo.x = x;
	mark.box.lo.y = y;
	mark.box.hi.x = x + width;
	mark.box.hi.y = y + height;
	mark.image = NULL;
	mark.edge = edge;
	mark.edgeWidth = 1.0f;
	mark.fill = fill;
	mark.rank = MATCH_EVENT_TYPE_COUNT;
	mark.label = NULL;
	mark.count = NULL;
	mark.dropBottom = 0;
	return mark;
}

/// a filled rectangle in the panel's pixels
static void addFill( Int x, Int y, Int width, Int height, Color fill )
{
	if( width > 0 && height > 0 )
		s_marks.push_back( newMark( x, y, width, height, fill, 0 ) );
}

/// and on the board, never thinner than a pixel
static void putFill( Real x0, Real y0, Real x1, Real y1, Color fill )
{
	addFill( lx( x0 ), ly( y0 ), max( lx( x1 ) - lx( x0 ), 1 ), max( ly( y1 ) - ly( y0 ), 1 ), fill );
}

static void putLit( Real x0, Real y0, Real x1, Real y1, Color color )
{
	GraphLit lit;
	lit.box.lo.x = lx( x0 );
	lit.box.lo.y = ly( y0 );
	lit.box.hi.x = max( lx( x1 ), lit.box.lo.x + 1 );
	lit.box.hi.y = max( ly( y1 ), lit.box.lo.y + 1 );
	lit.color = color;
	s_lit.push_back( lit );
}

static void putRow( Real x0, Real y0, Real x1, Real y1, Bool local, Bool zebra, Bool hovers = TRUE )
{
	GraphRow row;
	row.box.lo.x = lx( x0 );
	row.box.lo.y = ly( y0 );
	row.box.hi.x = lx( x1 );
	row.box.hi.y = ly( y1 );
	row.local = local;
	row.zebra = zebra;
	row.hovers = hovers;
	s_rows.push_back( row );
}

/// a picture in a board rectangle
static void putImage( const Image *image, Real x0, Real y0, Real x1, Real y1 )
{
	if( image == NULL )
		return;
	GraphMark mark = newMark( lx( x0 ), ly( y0 ), lx( x1 ) - lx( x0 ), ly( y1 ) - ly( y0 ), 0, 0 );
	mark.image = image;
	s_marks.push_back( mark );
}

/// the sorted column's caret: a small down triangle, its right at right, in the panel's pixels
static void addCaret( Int right, Int middle )
{
	const Int size = max( ll( 8.0f ), 4 );
	const Int x = right - size;
	const Int y = middle - size / 4;
	for( Int row = 0; row < size / 2; row++ )
		addFill( x + row, y + row, size - 2 * row, 1, UIStyle::ACTIVE );
}

static void addTip( GraphMark &mark, const UnicodeString &text, Color color )
{
	GraphTipLine line;
	line.text = text;
	line.color = color;
	mark.tip.push_back( line );
}

/** A chip or a tab the mouse can click, its label centred, in the panel's pixels; returns its right
	* edge.  font is the face menuTab draws its label in. */
static Int addHit( GraphHitKind kind, Int value, Bool open, const UnicodeString &label, GameFont *font, Int x, Int y, Int width, Int height )
{
	GraphHit hit;
	hit.kind = kind;
	hit.value = value;
	hit.open = open;
	hit.label = newText( label, font );
	hit.box.lo.x = x;
	hit.box.lo.y = y;
	hit.box.hi.x = x + width;
	hit.box.hi.y = y + height;
	s_hits.push_back( hit );
	return hit.box.hi.x;
}

/// the face UIStyle::menuTab sets on its label
static GameFont *tabFont( void )
{
	return UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
}

static const GraphPlayer *playerFor( Int index )
{
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		if( s_players[ each ].index == index )
			return &s_players[ each ];
	}
	return NULL;
}

/** The players with lines, in standings order; a campaign's side rows have none. */
static std::vector< const GraphPlayer * > chartedPlayers( void )
{
	std::vector< const GraphPlayer * > players;
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		for( size_t m = 0; m < s_order[ t ].members.size(); m++ )
		{
			const GraphPlayer &player = s_players[ s_order[ t ].members[ m ] ];
			if( player.index >= 0 )
				players.push_back( &player );
		}
	}
	return players;
}

/** The cameo a power is fired from; the picture lives on its command button.  InGameUI's
	* powerButton asks the same question for the superweapon timers. */
static const CommandButton *powerButton( const SpecialPowerTemplate *power )
{
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		if( button->getSpecialPowerTemplate() == power && button->getButtonImage() )
			return button;
	}
	return NULL;
}

/** A build's cameo: its own, or else the one on the button that builds it (the Angry Mob has none
	* of its own). */
static const Image *buildImage( const ThingTemplate *thing )
{
	if( thing->getButtonImage() )
		return thing->getButtonImage();
	for( const CommandButton *button = TheControlBar->getCommandButtons(); button; button = button->getNext() )
	{
		if( button->getThingTemplate() == thing && button->getButtonImage() )
			return button->getButtonImage();
	}
	return NULL;
}

static UnicodeString withoutHotkey( UnicodeString text )
{
	UnicodeString plain;
	for( const WideChar *c = text.str(); *c; c++ )
	{
		if( *c != u'&' )
			plain.concat( *c );
	}
	return plain;
}

static Int niceStep( Int raw )
{
	Int magnitude = 1;
	while( magnitude * 10 <= raw )
		magnitude *= 10;
	if( raw <= magnitude )
		return magnitude;
	if( raw <= 2 * magnitude )
		return 2 * magnitude;
	if( raw <= 5 * magnitude )
		return 5 * magnitude;
	return 10 * magnitude;
}

static Int minuteStep( UnsignedInt frames, Int most )
{
	static const Int steps[] = { 1, 2, 5, 10, 15, 20, 30, 60 };
	const Int minutes = frames / ( 60 * LOGICFRAMES_PER_SECOND ) + 1;
	for( Int each = 0; each < ARRAY_SIZE( steps ); each++ )
	{
		if( minutes / steps[ each ] <= most )
			return steps[ each ];
	}
	return 120;
}

/** The frame the match ended on, read once in ScoreScreenGraphsInit (ScoreScreen_endFrame).  It used
	* to be read again at every layout, and a tab picked after the match was torn down under the open
	* screen read the logic's frame as 0. */
static UnsignedInt s_endFrame = 1;

static UnsignedInt matchEndFrame( void )
{
	return s_endFrame;
}

static Int s_aodSurvived = -1;		///< an Art of Defense match: the waves every seat held alike; -1 for any other

/// "Survived 24:10": to the fall, or to the match's end for one still standing; "Waves survived: 23" in Art of Defense
static UnicodeString survivedText( UnsignedInt fell )
{
	UnicodeString text;
	if( s_aodSurvived >= 0 )
		text.format( fetchText( "GUI:AodSurvived" ), s_aodSurvived );
	else
		text.format( fetchText( "GUI:ApocalypseSurvived" ), clockText( fell != SCORE_STILL_STANDING ? fell : matchEndFrame() ).str() );
	return text;
}

/** The charts' time axis ends where the header's length does. */
static UnsignedInt historyEnd( void )
{
	return s_endFrame;
}

/** The samples a chart shows: those up to the match's end, the victory window after it dropped;
	* worked out with the end frame in ScoreScreenGraphsInit. */
static Int s_shownSamples = 0;

static Int shownSamples( void )
{
	return s_shownSamples;
}

/** Reads the match's end and the samples shown once, while the match is still there to read. */
static void readMatchEnd( void )
{
	s_history = TheMatchHistory;
	const MatchHistory &history = s_history;
	std::vector< UnsignedInt > frames( history.getSampleCount(), 0 );
	for( Int sample = 0; sample < history.getSampleCount(); sample++ )
		frames[ sample ] = history.getSampleFrame( sample );
	const UnsignedInt decided = TheVictoryConditions ? TheVictoryConditions->getEndFrame() : 0;
	s_endFrame = ScoreScreen_endFrame( decided, TheGameLogic->getFrame(), frames.empty() ? 0 : frames.back() );
	s_shownSamples = ScoreScreen_shownSamples( frames, s_endFrame );
}

/// whether a colour is too dark to read on the panel, so a stripe of it gets a light keyline
static Bool tooDark( Color color )
{
	return UIStyle::readable( color ) != color;
}

/// a player's name as it stands alone: gold for the local player, white for anyone else
static Color nameInk( const GraphPlayer &player )
{
	if( player.local )
		return UIStyle::GOLD;
	return player.disconnected ? UIStyle::MUTED : UIStyle::INK;
}

/// the colour a team shows as a bar: its lobby colour, a player alone his own
static Color teamInk( Int t )
{
	if( s_order[ t ].members.size() == 1 )
		return s_players[ s_order[ t ].members[ 0 ] ].color;
	return s_teamColor[ t ];
}

//-------------------------------------------------------------------------------------------------
static void clearTip( void )
{
	for( size_t each = 0; each < s_tipText.size(); each++ )
		TheDisplayStringManager->freeDisplayString( s_tipText[ each ] );
	s_tipText.clear();
	for( size_t each = 0; each < s_tipValue.size(); each++ )
		freeText( s_tipValue[ each ] );
	s_tipValue.clear();
	s_tipKey = -1;
}

static void clearContent( void )
{
	for( size_t each = 0; each < s_texts.size(); each++ )
		freeText( s_texts[ each ].text );
	s_texts.clear();
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		freeText( s_marks[ each ].label );
		freeText( s_marks[ each ].count );
	}
	s_marks.clear();
	for( size_t each = 0; each < s_hits.size(); each++ )
		freeText( s_hits[ each ].label );
	s_hits.clear();
	for( size_t each = 0; each < s_spots.size(); each++ )
		freeText( s_spots[ each ].number );
	s_spots.clear();
	s_lines.clear();
	s_bands.clear();
	s_dividers.clear();
	s_rules.clear();
	s_lit.clear();
	s_rows.clear();
	s_plot.live = FALSE;
	s_plot.sampleX.clear();
	s_mapShown = FALSE;
	clearTip();
}

//-------------------------------------------------------------------------------------------------
/** Teams, order, verdicts and the header's format, once the players are all in. */
static void prepareStandings( void )
{
	if( s_prepared )
		return;
	s_prepared = TRUE;

	// teams: two players who both call each other ALLIES, as the observer rack decides it
	const Int count = (Int)s_players.size();
	std::vector< Bool > allied( count * count, FALSE );
	for( Int a = 0; a < count; a++ )
	{
		for( Int b = 0; b < count; b++ )
		{
			const Player *pa = s_players[ a ].player;
			const Player *pb = s_players[ b ].player;
			allied[ a * count + b ] = a != b && pa && pb && pa->getRelationship( pb->getDefaultTeam() ) == ALLIES;
		}
	}
	const std::vector< Int > team = ScoreScreen_teamsFromAllies( count, allied );

	s_anyWinner = FALSE;
	std::vector< ScoreSeat > seats( count );
	for( Int a = 0; a < count; a++ )
	{
		GraphPlayer &player = s_players[ a ];
		player.team = team[ a ];
		s_anyWinner = s_anyWinner || player.winner;
		seats[ a ].team = player.team;
		seats[ a ].score = player.stats[ COL_SCORE ];
		seats[ a ].winner = player.winner;
		seats[ a ].fell = player.fell;
	}
	// a campaign keeps its rows as ScoreScreen gathers them: the player, then his allies, then the enemy,
	// all in one wing
	if( s_campaign )
	{
		s_order.clear();
		ScoreTeam rows;
		rows.team = 0;
		for( Int a = 0; a < count; a++ )
		{
			s_players[ a ].team = 0;
			rows.members.push_back( a );
		}
		if( count > 0 )
			s_order.push_back( rows );
	}
	else
		s_order = ScoreScreen_order( seats );
	s_banded = !s_campaign && ScoreScreen_banded( s_order );

	// two seats of one name (eight "Hard AI") are numbered in standings order: "Hard AI (2)"
	std::vector< UnicodeString > seen;
	std::vector< Int > times;
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		for( size_t m = 0; m < s_order[ t ].members.size(); m++ )
		{
			GraphPlayer &player = s_players[ s_order[ t ].members[ m ] ];
			size_t which = 0;
			while( which < seen.size() && !( seen[ which ] == player.name ) )
				which++;
			if( which == seen.size() )
			{
				seen.push_back( player.name );
				times.push_back( 0 );
			}
			if( ++times[ which ] > 1 )
			{
				UnicodeString number;
				number.format( u" (%d)", times[ which ] );
				player.name.concat( number );
			}
		}
	}

	// a team is called by its lobby number and wears its lobby colour where its members share one;
	// any other (a player alone, allies made in the match) takes the lowest number no lobby team
	// has, on the room's no-team slate; numbered in seat order, as the load board numbers them
	std::vector< Int > sharedLobbyTeam( s_order.size(), -1 );
	std::vector< Int > firstSlot( s_order.size(), 0 );
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		Int number = -1;
		Bool shared = TRUE;
		Int first = MAX_SLOTS + (Int)s_players.size();
		for( size_t m = 0; m < s_order[ t ].members.size(); m++ )
		{
			const GraphPlayer &player = s_players[ s_order[ t ].members[ m ] ];
			const GameSlot *slot = TheGameInfo && player.slot >= 0 ? TheGameInfo->getConstSlot( player.slot ) : NULL;
			const Int lobbyTeam = slot ? slot->getTeamNumber() : -1;
			if( m == 0 )
				number = lobbyTeam;
			else if( lobbyTeam != number )
				shared = FALSE;
			first = min( first, player.slot >= 0 ? player.slot : MAX_SLOTS + s_order[ t ].members[ m ] );
		}
		if( shared )
			sharedLobbyTeam[ t ] = number;
		firstSlot[ t ] = first;
	}
	s_teamNumber = ScoreScreen_teamNumbers( sharedLobbyTeam, firstSlot, MAX_SLOTS + 1 );
	s_teamColor.assign( s_order.size(), LobbyTeamColor( -1 ) );
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		if( sharedLobbyTeam[ t ] >= 0 && s_teamNumber[ t ] == sharedLobbyTeam[ t ] + 1 )
			s_teamColor[ t ] = LobbyTeamColor( sharedLobbyTeam[ t ] );
	}

	// the format the header and the map's foot name
	s_format.clear();
	s_freeForAll = FALSE;
	if( !s_campaign && !s_order.empty() )
	{
		const std::string format = ScoreScreen_format( s_order, &s_freeForAll );
		if( s_freeForAll )
		{
			UnicodeString players;
			players.format( u" (%d)", count );
			s_format = fetchText( "GUI:LoadFreeForAll" );
			s_format.concat( players );
		}
		else
			s_format.translate( AsciiString( format.c_str() ) );
	}

	// the title: the local player's own verdict, or who won for anyone watching
	const Player *local = ThePlayerList->getLocalPlayer();
	s_watching = TheGameLogic->isInReplayGame() || local == NULL || local->isPlayerObserver();
	UnicodeString verdict;
	// a mission, live or replayed, is the player's own win or loss; a match played here and not won,
	// left before anyone won included, is a defeat, as the stats upload counts it
	if( s_campaign )
		verdict = fetchText( TheCampaignManager->isVictorious() ? "GUI:Victory" : "GUI:Defeat" );
	else if( s_apocalypse )
	{
		// nobody wins Apocalypse (the owner, 2026-11-23): the verdict is how long the local seat held, or the
		// last seat for anyone watching
		UnsignedInt fell = SCORE_STILL_STANDING;
		for( Int a = 0; a < count; a++ )
			if( !s_watching && s_players[ a ].player == local )
				fell = s_players[ a ].fell;
		verdict = survivedText( fell );
	}
	else if( !s_watching )
		verdict = fetchText( TheVictoryConditions->isLocalAlliedVictory() ? "GUI:Victory" : "GUI:Defeat" );
	else if( s_anyWinner && !s_order.empty() )
	{
		const ScoreTeam &first = s_order[ 0 ];
		if( first.members.size() >= 2 )
			verdict.format( fetchText( "GUI:ScoreTeamWins" ), s_teamNumber[ 0 ] );
		else
			verdict.format( fetchText( "GUI:ScorePlayerWins" ), s_players[ first.members[ 0 ] ].name.str() );
	}
	if( verdict.isEmpty() )
		verdict = fetchText( "GUI:ScoreNoWinner" );
	s_verdict = UIStyle::capitals( verdict );

	// the players are read no more
	for( Int a = 0; a < count; a++ )
		s_players[ a ].player = NULL;
}

/** What a team head, a band or a lone row says beside its name, and in which colour; FALSE for
	* nothing (a match left before anyone won). */
static Bool verdictFor( Bool winner, Bool disconnected, UnsignedInt fell, UnicodeString &label, Color &color )
{
	color = UIStyle::MUTED;
	// Apocalypse: no winner and no defeat, every seat or team carries how long it held
	if( s_apocalypse && !disconnected )
	{
		label = survivedText( fell );
		return TRUE;
	}
	if( !s_anyWinner && !s_campaign )
		return FALSE;
	if( winner )
	{
		label = fetchText( "GUI:Victory" );
		color = UIStyle::ACTIVE;
		return TRUE;
	}
	if( disconnected )
	{
		label = fetchText( "GUI:SummaryDisconnected" );
		return TRUE;
	}
	label = fetchText( "GUI:Defeat" );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The last sample a player's line reaches: the one at or before his fall. */
static Int lastSampleOf( const GraphPlayer &player )
{
	const MatchHistory &history = s_history;
	Int last = shownSamples() - 1;
	while( last > 0 && player.fell != SCORE_STILL_STANDING && history.getSampleFrame( last ) > player.fell )
		last--;
	return last;
}

/** A chart's values for one player at every sample, 0 after the sample he fell on: the one rule
	* the lines, the share bands and the crosshair's tooltip all read. */
static std::vector< Int > chartValues( Int chart, const GraphPlayer &player )
{
	const MatchHistory &history = s_history;
	const Int count = history.getSampleCount();
	std::vector< Int > values( count, 0 );
	for( Int sample = 0; sample < count; sample++ )
		values[ sample ] = history.getValue( sample, player.index, theChartSeries[ chart ] );
	if( chart == CHART_INCOME )
	{
		std::vector< UnsignedInt > frames( count, 0 );
		for( Int sample = 0; sample < count; sample++ )
			frames[ sample ] = history.getSampleFrame( sample );
		values = ScoreScreen_incomePerMinute( frames, values, 30 * LOGICFRAMES_PER_SECOND, 60 * LOGICFRAMES_PER_SECOND );
	}
	for( Int sample = lastSampleOf( player ) + 1; sample < count; sample++ )
		values[ sample ] = 0;
	return values;
}

/// a player's army value at its peak over the match
static Int armyPeak( const GraphPlayer &player )
{
	if( player.index < 0 || !s_history.wasSampled( player.index ) )
		return 0;
	Int peak = 0;
	const Int last = shownSamples() - 1;
	for( Int sample = 0; sample <= last; sample++ )
		peak = max( peak, s_history.getValue( sample, player.index, MATCH_SERIES_ARMY_VALUE ) );
	return peak;
}

/** Gives a marker the picture of event, which outranks whatever it showed before. */
static void dressMark( GraphMark &mark, const MatchEvent &event, const GraphPlayer &player, const CommandButton *button )
{
	mark.rank = theEventRank[ event.type ];
	mark.edge = player.local ? UIStyle::GOLD : player.nameColor;
	mark.fill = MARK_FILL;
	freeText( mark.label );
	switch( event.type )
	{
		case MATCH_EVENT_SUPERWEAPON_BUILT:
			mark.image = event.thing->getButtonImage();
			break;
		case MATCH_EVENT_SUPERWEAPON_FIRED:
			mark.image = button ? button->getButtonImage() : event.thing->getButtonImage();
			break;
		case MATCH_EVENT_POWER:
			mark.image = button ? button->getButtonImage() : NULL;
			break;
		default:
		{
			mark.image = NULL;
			UnicodeString rank;
			rank.format( u"%d", event.rank );
			mark.label = newText( rank, s_fCaps );
			break;
		}
	}
}

/// a chart's pieces over and under its plot: a marker lane's height with its gap, and the time axis
static Int markerLaneHeight( void )
{
	return s_fSmall->height + 6 + max( ll( 6.0f ), 4 );
}

static Int chartFootHeight( void )
{
	return s_fSmall->height + max( s_fBody->height / 2, 4 ) / 2;
}

/** The chart in x0,y0 to x1,y1 (the panel's pixels): lanes of event markers over a plot of one line a
	* player, or of every player's share of the whole as bands ordered by team, up to the frame the match
	* was decided on. */
static void layoutChart( Int chart, Bool share, Int x0, Int y0, Int x1, Int y1, Int lanes = 2 )
{
	const MatchHistory &history = s_history;
	const Int count = shownSamples();
	const Int pad = max( s_fBody->height / 2, 4 );
	const Int icon = s_fSmall->height + 6;
	const Int iconWidth = icon * 4 / 3;
	const Int laneGap = max( ll( 6.0f ), 4 );
	const Int plotTop = y0 + lanes * markerLaneHeight() + pad / 2;
	const Int plotBottom = y1 - chartFootHeight();

	if( count == 0 )
	{
		addText( fetchText( "GUI:GraphNoData" ), s_fBody, x0, plotTop + s_fBody->height, UIStyle::MUTED );
		return;
	}

	const std::vector< const GraphPlayer * > players = chartedPlayers();
	std::vector< std::vector< Int > > values( players.size() );
	Int most = 0;
	for( size_t each = 0; each < players.size(); each++ )
	{
		values[ each ] = chartValues( chart, *players[ each ] );
		if( s_hidden[ players[ each ]->index ] || !history.wasSampled( players[ each ]->index ) )
			continue;
		for( Int sample = 0; sample < count; sample++ )
			most = max( most, values[ each ][ sample ] );
	}

	// five value gridlines; the labels' width is the wider of both ways of drawing this series, so
	// switching between lines and share keeps the plot's left edge
	const Int lineStep = niceStep( max( most, 5 ) / 5 );
	const Int lineCeiling = lineStep * ( ( max( most, 1 ) + lineStep - 1 ) / lineStep );
	const Int step = share ? 20 : lineStep;
	const Int ceiling = share ? 100 : lineCeiling;
	const Int labelWidth = max( measure( UnicodeString( u"100%" ), s_fSmall ), measure( valueText( lineCeiling ), s_fSmall ) );
	const Int plotLeft = x0 + labelWidth + pad;
	const Int plotRight = x1 - iconWidth / 2;
	const Int plotWidth = max( plotRight - plotLeft, 1 );
	const Int plotHeight = max( plotBottom - plotTop, 1 );
	const UnsignedInt end = historyEnd();

	for( Int value = 0; value <= ceiling; value += step )
	{
		const Int y = plotBottom - (Int)( (Real)value / ceiling * plotHeight );
		addRule( plotLeft, y, plotRight, y, value == 0 ? UIStyle::FRAME : UIStyle::GRID );
		UnicodeString label = share ? UIStyle::percentText( value ) : valueText( value );
		addText( label, s_fSmall, plotLeft - pad / 2, y, UIStyle::MUTED, 2 );
	}

	const Int minutes = minuteStep( end, 10 );
	for( UnsignedInt frame = 0; frame <= end; frame += minutes * 60 * LOGICFRAMES_PER_SECOND )
	{
		const Int x = plotLeft + (Int)( (Real)frame / end * plotWidth );
		addRule( x, plotBottom, x, plotBottom + pad / 2, UIStyle::GRID );
		addText( clockText( frame ), s_fSmall, x, plotBottom + pad / 2 + s_fSmall->height / 2, UIStyle::MUTED, frame == 0 ? 0 : 1 );
	}

	s_plot.live = TRUE;
	s_plot.box.lo.x = plotLeft;
	s_plot.box.lo.y = plotTop;
	s_plot.box.hi.x = plotRight + 1;
	s_plot.box.hi.y = plotBottom + 1;
	s_plot.chart = chart;
	s_plot.share = share;
	for( Int sample = 0; sample < count; sample++ )
		s_plot.sampleX.push_back( plotLeft + (Int)( (Real)history.getSampleFrame( sample ) / end * plotWidth ) );

	if( !share )
	{
		for( size_t each = 0; each < players.size(); each++ )
		{
			const GraphPlayer &player = *players[ each ];
			if( s_hidden[ player.index ] || !history.wasSampled( player.index ) )
				continue;
			GraphLine graph;
			graph.owner = player.index;
			graph.color = player.nameColor;		// readable on the panel
			graph.cross = player.fell != SCORE_STILL_STANDING;
			graph.local = player.local;
			const Int last = lastSampleOf( player );
			for( Int sample = 0; sample <= last; sample++ )
			{
				ICoord2D point;
				point.x = s_plot.sampleX[ sample ];
				point.y = plotBottom - (Int)( (Real)values[ each ][ sample ] / ceiling * plotHeight );
				graph.points.push_back( point );
			}
			s_lines.push_back( graph );
		}
	}
	else
	{
		// ponytail: a column of rectangles every few pixels, since the display draws no polygon; about
		// 4,000 small fills for eight players at 1080, fine for a menu
		const Int column = max( ll( 3.0f ), 2 );
		for( Int x = plotLeft; x < plotRight; x += column )
		{
			const Real frame = (Real)( x - plotLeft ) / plotWidth * end;
			Int sample = 0;
			while( sample + 1 < count && history.getSampleFrame( sample + 1 ) <= frame )
				sample++;
			const Int next = min( sample + 1, count - 1 );
			const Real span = (Real)history.getSampleFrame( next ) - (Real)history.getSampleFrame( sample );
			const Real along = span > 0.0f ? ( frame - history.getSampleFrame( sample ) ) / span : 0.0f;
			std::vector< Real > at( players.size(), 0.0f );
			Real total = 0.0f;
			for( size_t each = 0; each < players.size(); each++ )
			{
				if( s_hidden[ players[ each ]->index ] || !history.wasSampled( players[ each ]->index ) )
					continue;
				const Real a = (Real)values[ each ][ sample ];
				const Real b = (Real)values[ each ][ next ];
				at[ each ] = max( 0.0f, a + ( b - a ) * min( max( along, 0.0f ), 1.0f ) );
				total += at[ each ];
			}
			if( total <= 0.0f )
				continue;
			// see-through bands in the readable colours, a 2 px white line where one team gives way to the
			// next, and a 3 px gold top edge on the local player's band
			const Int width = min( column, plotRight - x );
			Real below = 0.0f;
			for( size_t each = 0; each < players.size(); each++ )
			{
				const Int top = plotTop + (Int)( below / total * plotHeight );
				below += at[ each ];
				const Int bottom = plotTop + (Int)( below / total * plotHeight );
				if( bottom > top )
				{
					GraphBand band;
					band.owner = players[ each ]->index;
					band.color = UIStyle::withAlpha( players[ each ]->nameColor, 120 );
					band.box.lo.x = x;
					band.box.lo.y = top;
					band.box.hi.x = x + width;
					band.box.hi.y = bottom;
					s_bands.push_back( band );
					if( players[ each ]->local )
					{
						GraphBand edge;
						edge.owner = -1;
						edge.color = UIStyle::GOLD;
						edge.box.lo.x = x;
						edge.box.lo.y = top;
						edge.box.hi.x = x + width;
						edge.box.hi.y = min( top + max( ll( 3.0f ), 2 ), bottom );
						s_dividers.push_back( edge );
					}
				}
				size_t later = each + 1;
				while( later < players.size() && at[ later ] <= 0.0f )
					later++;
				if( later < players.size() && players[ later ]->team != players[ each ]->team && bottom > plotTop && bottom < plotBottom )
				{
					GraphBand divider;
					divider.owner = -1;
					divider.color = UIStyle::INK;
					divider.box.lo.x = x;
					divider.box.lo.y = bottom - 1;
					divider.box.hi.x = x + width;
					divider.box.hi.y = bottom + 1;
					s_dividers.push_back( divider );
				}
			}
		}
	}

	// the event markers in two lanes: an event that lands on one of its own seat's markers joins it,
	// which then shows the weightiest of its events and counts them; any other goes into the first lane
	// it clears, or else into the lane that ends soonest, pushed right until it clears.  Two seats never
	// share a marker, and no two markers in a lane overlap; the drop line keeps the true moment.
	std::vector< Int > laneRight( lanes, -1000000 );
	std::vector< Int > laneMark( lanes, -1 );
	std::vector< Int > laneOwner( lanes, -1 );
	const std::vector< MatchEvent > &events = history.getEvents();
	for( size_t each = 0; lanes > 0 && each < events.size(); each++ )
	{
		const MatchEvent &event = events[ each ];
		if( event.type == MATCH_EVENT_DEFEATED )
			continue;		// the line's cross says it
		if( event.frame > end )
			continue;		// after the match was decided
		const GraphPlayer *player = playerFor( event.player );
		if( player == NULL || s_hidden[ player->index ] )
			continue;
		const Int x = plotLeft + (Int)( (Real)event.frame / end * plotWidth );
		const Int boxLeft = max( min( x - iconWidth / 2, plotRight - iconWidth ), plotLeft );
		Int markIndex = -1;
		for( Int l = 0; l < lanes && markIndex < 0; l++ )
		{
			if( laneMark[ l ] >= 0 && laneOwner[ l ] == player->index && boxLeft < laneRight[ l ] )
				markIndex = laneMark[ l ];
		}
		if( markIndex < 0 )
		{
			// a crowd pushed to the plot's very end joins the last marker there rather than overlap it
			// or run off the plot; its hover names every event
			Bool clear = FALSE;
			Int soonest = 0;
			for( Int l = 0; l < lanes; l++ )
			{
				clear = clear || boxLeft >= laneRight[ l ];
				if( laneRight[ l ] < laneRight[ soonest ] )
					soonest = l;
			}
			if( !clear && laneRight[ soonest ] > plotRight - iconWidth )
				markIndex = laneMark[ soonest ];
		}
		if( markIndex < 0 )
		{
			Int lane = 0;
			Int left = boxLeft;
			for( Int l = 0; l < lanes; l++ )
			{
				if( boxLeft >= laneRight[ l ] )
				{
					lane = l;
					break;
				}
				if( l == 0 || laneRight[ l ] < laneRight[ lane ] )
					lane = l;
			}
			if( boxLeft < laneRight[ lane ] )
				left = laneRight[ lane ];
			GraphMark mark = newMark( left, y0 + lane * ( icon + laneGap ), iconWidth, icon, MARK_FILL, player->nameColor );
			mark.dropBottom = plotBottom;
			s_marks.push_back( mark );
			markIndex = (Int)s_marks.size() - 1;
			laneRight[ lane ] = left + iconWidth + pad / 2;
			laneMark[ lane ] = markIndex;
			laneOwner[ lane ] = player->index;
		}
		GraphMark &mark = s_marks[ markIndex ];
		const CommandButton *button = event.power ? powerButton( event.power ) : NULL;
		if( theEventRank[ event.type ] < mark.rank )
			dressMark( mark, event, *player, button );

		UnicodeString what;
		switch( event.type )
		{
			case MATCH_EVENT_SUPERWEAPON_BUILT:
				what = event.thing->getDisplayName();
				break;
			case MATCH_EVENT_SUPERWEAPON_FIRED:
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : event.thing->getDisplayName();
				break;
			case MATCH_EVENT_POWER:
				what = button ? withoutHotkey( TheGameText->fetch( button->getTextLabel() ) ) : UnicodeString( u"?" );
				break;
			default:
				what.format( fetchText( "GUI:GraphRank" ).str(), event.rank );
				break;
		}
		UnicodeString tip;
		tip.format( u"%s  %s  %s: %s", clockText( event.frame ).str(), player->name.str(),
			fetchText( theEventLabels[ event.type ] ).str(), what.str() );
		addTip( mark, tip, nameInk( *player ) );
		GraphDrop drop;
		drop.x = x;
		drop.color = UIStyle::withAlpha( player->nameColor, 180 );
		mark.drops.push_back( drop );
	}
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( s_marks[ each ].tip.size() > 1 && s_marks[ each ].count == NULL && !s_marks[ each ].drops.empty() )
		{
			// "×3", so a promotion's rank and the count beside it never read as one number
			UnicodeString count;
			count.format( u"%d", (Int)s_marks[ each ].tip.size() );
			UnicodeString number;
			number.concat( (WideChar)0x00D7 );
			number.concat( count );
			s_marks[ each ].count = newText( number, s_fCaps );
		}
	}
}

//-------------------------------------------------------------------------------------------------
// The header, on every page
//-------------------------------------------------------------------------------------------------
static void layoutHeader( void )
{
	using namespace EndScreen;
	const Real titleMiddle = ( TITLE_TOP + TITLE_BOTTOM ) * 0.5f;
	const Int titleWidth = putTracked( s_verdict, s_fTitle, INSET_LEFT, titleMiddle, UIStyle::INK, UIStyle::MENU_TEXT_TITLE * 0.12f );

	// the tabs, right-aligned to the inset at the title row
	const Real tabsLeft = INSET_RIGHT - TAB_COUNT * TAB_WIDTH - ( TAB_COUNT - 1 ) * TAB_GAP;
	for( Int tab = 0; tab < TAB_COUNT; tab++ )
	{
		const Real x0 = tabsLeft + tab * ( TAB_WIDTH + TAB_GAP );
		const UnicodeString label = fittedLabel( UIStyle::capitals( fetchText( theTabLabels[ tab ] ) ), tabFont(), ll( TAB_WIDTH - 16 ) );
		addHit( HIT_TAB, tab, tab == s_tab, label, tabFont(), lx( x0 ), ly( TABS_TOP ), ll( TAB_WIDTH ), ll( TAB_HEIGHT ) );
	}

	// the facts after the title: the map, the format and the length, or the online words round them
	Real x = INSET_LEFT + bw( titleWidth ) + 24.0f;
	UnicodeString facts = s_mapName;
	if( s_subtitleKey && !s_campaign && !s_order.empty() )
	{
		UnicodeString online;
		online.format( fetchText( s_subtitleKey ), s_format.str(), clockText( matchEndFrame() ).str() );
		facts.concat( SCORE_DOT );
		facts.concat( online );
	}
	else
	{
		if( !s_format.isEmpty() )
		{
			facts.concat( SCORE_DOT );
			facts.concat( s_format );
		}
		facts.concat( SCORE_DOT );
		facts.concat( clockText( matchEndFrame() ) );
	}
	// a free for all the local player played: his place, large, in gold, after the facts
	Int place = 0;
	if( s_freeForAll && !s_watching )
	{
		for( size_t t = 0; t < s_order.size() && place == 0; t++ )
			if( s_players[ s_order[ t ].members[ 0 ] ].local )
				place = (Int)t + 1;
	}
	const Real placeRoom = place > 0 ? 200.0f : 0.0f;
	const Int factsWidth = putText( fittedLabel( facts, s_fFacts, ll( tabsLeft - 24.0f - x - placeRoom ) ), s_fFacts, x, titleMiddle + 2.0f, UIStyle::MUTED );
	if( place > 0 )
	{
		x += bw( factsWidth ) + 36.0f;
		x += bw( putTracked( UIStyle::capitals( fetchText( "GUI:ScorePlace" ) ), s_fCaps, x, titleMiddle + 2.0f, UIStyle::MUTED, 1.6f ) ) + 12.0f;
		UnicodeString number;
		number.format( u"%d", place );
		x += bw( putText( number, s_fTitle, x, titleMiddle, UIStyle::GOLD ) ) + 8.0f;
		UnicodeString of;
		of.format( u"/ %d", (Int)s_order.size() );
		putText( of, s_fFacts, x, titleMiddle + 2.0f, UIStyle::MUTED );
	}

	// the match's settings, right-aligned under the tabs: speed, starting money, watchers
	if( !s_campaign )
	{
		std::vector< UnicodeString > labels, values;
		Int speed = 0;
		for( Int each = 0; each < PLAYER_GAME_SPEED_COUNT; each++ )
		{
			if( PLAYER_GAME_SPEED_PERCENTS[ each ] == GAME_SPEED_PERCENT )
				speed = each;
		}
		AsciiString speedLabel;
		speedLabel.format( "GUI:GameSpeedMissions%d", speed );
		labels.push_back( fetchText( "GUI:GameSpeed" ) );
		values.push_back( fetchText( speedLabel.str() ) );
		if( TheGameInfo )
		{
			const Money &cash = TheGameInfo->getStartingCash();
			labels.push_back( fetchText( "GUI:StartingMoney" ) );
			values.push_back( cash.isUnlimited() ? fetchText( "GUI:StartingMoneyUnlimited" ) : moneyText( (Int)cash.countMoney() ) );
			Int watchers = 0;
			for( Int slot = 0; slot < MAX_SLOTS; slot++ )
			{
				const GameSlot *seat = TheGameInfo->getConstSlot( slot );
				if( seat && seat->isOccupied() && seat->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER )
					watchers++;
			}
			if( watchers > 0 )
			{
				UnicodeString count;
				count.format( u"%d", watchers );
				labels.push_back( fetchText( "GUI:ScoreObservers" ) );
				values.push_back( count );
			}
		}
		Real right = INSET_RIGHT;
		const Real middle = ( FACTS_TOP + FACTS_BOTTOM ) * 0.5f;
		for( Int each = (Int)labels.size() - 1; each >= 0; each-- )
		{
			right -= bw( putText( values[ each ], s_fSub, right, middle, UIStyle::INK, 2 ) ) + 7.0f;
			right -= bw( putText( labels[ each ], s_fSub, right, middle, UIStyle::MUTED, 2 ) ) + 26.0f;
		}
	}

	// the lit rule, and the teams' share of the match's score as a line under it
	putLit( PANEL_LEFT + 2, RULE_Y, PANEL_RIGHT - 2, RULE_Y + 2, UIStyle::FRAME );
	if( s_campaign || s_order.empty() )
		return;
	std::vector< Int > score( s_order.size(), 0 );
	Int total = 0;
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		for( size_t m = 0; m < s_order[ t ].members.size(); m++ )
			score[ t ] += max( s_players[ s_order[ t ].members[ m ] ].stats[ COL_SCORE ], 0 );
		total += score[ t ];
	}
	const Real x0 = PANEL_LEFT + 2, x1 = PANEL_RIGHT - 2;
	if( s_order.size() == 2 )
	{
		// two teams pull from their ends, a white tick at the middle
		const Real split = total > 0 ? x0 + ( x1 - x0 ) * score[ 0 ] / total : ( x0 + x1 ) * 0.5f;
		putLit( x0, SHARE_TOP, split, SHARE_BOTTOM, teamInk( 0 ) );
		putLit( split, SHARE_TOP, x1, SHARE_BOTTOM, teamInk( 1 ) );
		putFill( ( x0 + x1 ) * 0.5f, SHARE_TOP - 4, ( x0 + x1 ) * 0.5f + 1, SHARE_BOTTOM + 4, UIStyle::INK );
		return;
	}
	Real at = x0;
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		const Real width = total > 0 ? ( x1 - x0 ) * score[ t ] / total : ( x1 - x0 ) / s_order.size();
		if( width >= 1.0f )
			putLit( at, SHARE_TOP, at + width - 1.0f, SHARE_BOTTOM, UIStyle::readable( teamInk( (Int)t ) ) );
		at += width;
	}
}

//-------------------------------------------------------------------------------------------------
// The Summary
//-------------------------------------------------------------------------------------------------
static GraphColumns columnsFor( Real right, Bool dense, Real scoreLeft = 0.0f )
{
	GraphColumns cols;
	cols.dense = dense;
	EndScreen::summaryColumns( (Int)right, dense, cols.left, cols.right );
	if( scoreLeft > 0.0f )
		cols.left[ COL_SCORE ] = (Int)scoreLeft;
	return cols;
}

/** The column heads in a band from y: a group's word over a hairline over its columns, each column's
	* word right over its figures, SCORE alone green with its caret, the sorted column. */
static void columnHeads( const GraphColumns &cols, Real y )
{
	for( Int group = 0; group < GROUP_COUNT; group++ )
	{
		Int first = COL_COUNT, last = 0;
		for( Int col = 0; col < COL_COUNT; col++ )
		{
			if( theColumnGroup[ col ] == group )
			{
				first = min( first, col );
				last = max( last, col );
			}
		}
		const Int room = ll( (Real)( cols.right[ last ] - cols.left[ first ] ) );
		if( first == last )
		{
			const UnicodeString label = UIStyle::capitals( UIStyle::fitKey( theGroupLabels[ group ], s_fCaps, room ) );
			const Int width = putTracked( label, s_fCaps, cols.right[ first ], y + 28.0f, UIStyle::ACTIVE, 1.0f, 2 );
			addCaret( lx( cols.right[ first ] ) - width - ll( 6.0f ), ly( y + 28.0f ) );
			continue;
		}
		const UnicodeString label = UIStyle::capitals( UIStyle::fitKey( theGroupLabels[ group ], s_fCaps, room ) );
		putTracked( label, s_fCaps, ( cols.left[ first ] + cols.right[ last ] ) * 0.5f, y + 8.0f, UIStyle::INK, 1.0f, 1 );
		putRule( cols.left[ first ], y + 18.0f, cols.right[ last ], y + 18.0f, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
		for( Int col = first; col <= last; col++ )
		{
			if( theColumnLabels[ col ] == NULL )
				continue;
			const Int widest = ll( (Real)( cols.right[ col ] - cols.left[ col ] + 9 ) );
			putText( UIStyle::capitals( UIStyle::fitKey( theColumnLabels[ col ], s_fCaps, widest ) ), s_fCaps, cols.right[ col ] + 1.0f, y + 29.0f,
				UIStyle::MUTED, 2 );
		}
	}
}

/// the row a column's best stands in, or -1 where two rows share it; with two players a best only
/// repeats who won, and money spent is no achievement, so neither is marked
static void columnBests( Int bestRow[ COL_COUNT ] )
{
	for( Int col = 0; col < COL_COUNT; col++ )
	{
		bestRow[ col ] = -1;
		if( s_players.size() < 3 || col == COL_SPENT || s_campaign )
			continue;
		Int best = 0;
		for( size_t each = 0; each < s_players.size(); each++ )
		{
			const Int value = s_players[ each ].stats[ col ];
			const Bool better = theColumnMostIsBest[ col ] ? value > best : value < best;
			if( each == 0 || better )
			{
				best = value;
				bestRow[ col ] = (Int)each;
			}
			else if( value == best )
				bestRow[ col ] = -1;
		}
	}
}

/** A row's figures, their middle at middle: the score in lead with a bar under it showing its share of
	* the match's best (bar < 0 for none), the rest in body.  best[ col ] lights a column's best in cyan
	* with a cyan bar under it. */
static void figures( const GraphColumns &cols, const Int values[ COL_COUNT ], const Bool best[ COL_COUNT ], Real middle,
	GameFont *lead, GameFont *body, Color ink, Real bar, Color barColor )
{
	for( Int col = 0; col < COL_COUNT; col++ )
	{
		GameFont *font = col == COL_SCORE ? lead : body;
		const Color color = best[ col ] ? UIStyle::EDGE_LIT : ink;
		const UnicodeString text = col == COL_EARNED || col == COL_SPENT ? moneyText( values[ col ] ) : UIStyle::grouped( values[ col ] );
		const Int width = putText( text, font, cols.right[ col ], middle, color, 2 );
		const Int under = ly( middle ) + font->height / 2 + 1;
		if( best[ col ] )
			addFill( lx( cols.right[ col ] ) - width, under, width, max( ll( 2.0f ), 1 ), UIStyle::EDGE_LIT );
		if( col == COL_SCORE && bar >= 0.0f )
		{
			const Int left = lx( cols.left[ col ] );
			const Int full = lx( cols.right[ col ] ) - left;
			addFill( left, under + 1, max( (Int)( full * min( bar, 1.0f ) ), 1 ), max( ll( 3.0f ), 2 ), best[ col ] ? UIStyle::EDGE_LIT : barColor );
		}
	}
}

/// a player's stripe, a light keyline beside it when his colour is dark
static void playerStripe( Real x0, Real y0, Real x1, Real y1, Color color )
{
	putFill( x0, y0, x1, y1, color );
	if( tooDark( color ) )
		putFill( x0 - 1.0f, y0, x0, y1, LIGHT_KEY );
}

static Int bestScore( void )
{
	Int top = 1;
	for( size_t each = 0; each < s_players.size(); each++ )
		top = max( top, s_players[ each ].stats[ COL_SCORE ] );
	return top;
}

/** A two-team (or one-team) head from y: the team's lit bar, TEAM n and its verdict, its score at 40,
	* the column heads and the lit rule under them. */
static void teamHead( Int t, Real x0, Real x1, Real y, const GraphColumns &cols )
{
	const ScoreTeam &team = s_order[ t ];
	Bool winner = FALSE, disconnected = TRUE;
	UnsignedInt fell = 0;
	Int score = 0;
	for( size_t m = 0; m < team.members.size(); m++ )
	{
		const GraphPlayer &player = s_players[ team.members[ m ] ];
		winner = winner || player.winner;
		disconnected = disconnected && player.disconnected;
		fell = max( fell, player.fell );
		score += player.stats[ COL_SCORE ];
	}
	if( !s_campaign )
	{
		putLit( x0, y + 8.0f, x0 + 4.0f, y + 44.0f, teamInk( t ) );
		UnicodeString name;
		name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
		const Int nameWidth = putTracked( UIStyle::capitals( name ), s_fTeam, x0 + 16.0f, y + 26.0f, UIStyle::INK, 2.4f );
		UnicodeString verdict;
		Color color;
		if( verdictFor( winner, disconnected, fell, verdict, color ) )
			putTracked( UIStyle::capitals( verdict ), s_fCaps, x0 + 32.0f + bw( nameWidth ), y + 27.0f, color, 1.4f );
		putText( UIStyle::grouped( score ), s_fFigure, x1 - 8.0f, y + 26.0f, UIStyle::INK, 2 );
	}
	columnHeads( cols, y + 52.0f );
	putLit( x0, y + 92.0f, x1, y + 93.0f, UIStyle::FRAME );
}

/// a wing of several teams or places: its places beside the column heads, the lit rule under them
static void wingHead( Int first, Int last, Real x0, Real x1, Real y, const GraphColumns &cols )
{
	UnicodeString label;
	if( first == last )
		label.format( u"%s %d", UIStyle::capitals( fetchText( "GUI:ScorePlace" ) ).str(), first );
	else
		label.format( fetchText( "GUI:ScorePlaces" ), first, last );
	putTracked( UIStyle::capitals( label ), s_fBodyBold, x0 + 16.0f, y + 17.0f, UIStyle::INK, 2.0f );
	columnHeads( cols, y );
	putLit( x0, y + 40.0f, x1, y + 41.0f, UIStyle::FRAME );
}

/** A card's army chart: the player's army value over the match as a line on one scale with every other
	* card, with its minute labels when tall enough. */
static void cardChart( const GraphPlayer &player, Real x0, Real y0, Real x1, Real y1, Int top, Color color )
{
	const Int count = shownSamples();
	if( count < 2 || player.index < 0 || !s_history.wasSampled( player.index ) )
		return;
	const Bool axis = y1 - y0 >= 120.0f;
	const Real plotBottom = axis ? y1 - 18.0f : y1;
	const UnsignedInt end = historyEnd();
	const std::vector< Int > values = chartValues( CHART_ARMY_VALUE, player );
	putRule( x0, plotBottom, x1, plotBottom, UIStyle::GRID );
	GraphLine graph;
	graph.owner = -1;
	graph.color = color;
	graph.cross = player.fell != SCORE_STILL_STANDING;
	graph.local = FALSE;
	const Int last = lastSampleOf( player );
	for( Int sample = 0; sample <= last; sample++ )
	{
		ICoord2D point;
		point.x = lx( x0 + ( x1 - x0 ) * (Real)s_history.getSampleFrame( sample ) / end );
		point.y = ly( plotBottom - ( plotBottom - y0 ) * (Real)values[ sample ] / max( top, 1 ) );
		graph.points.push_back( point );
	}
	s_lines.push_back( graph );
	if( !axis )
		return;
	const Int minutes = end / ( 60 * LOGICFRAMES_PER_SECOND ) <= 30 ? 5 : 10;
	for( UnsignedInt frame = 0; frame <= end; frame += minutes * 60 * LOGICFRAMES_PER_SECOND )
	{
		const Real x = x0 + ( x1 - x0 ) * (Real)frame / end;
		putRule( x, plotBottom, x, plotBottom + 4.0f, UIStyle::withAlpha( UIStyle::INK, 60 ) );
		putText( clockText( frame ), s_fSmall, x, y1 - 7.0f, UIStyle::MUTED, frame == 0 ? 0 : ( x > x1 - 30.0f ? 2 : 1 ) );
	}
}

/// one to four a team: the army's picture, the name and the general, the army chart, the figures
static void cardRow( const GraphPlayer &player, Int row, Real x0, Real y0, Real x1, Real y1, const Int bestRow[ COL_COUNT ],
	Int topScore, Int topArmy, Color teamColor )
{
	putRow( x0, y0, x1, y1, player.local, FALSE );
	const Real h = y1 - y0;
	const Real pic = (Real)EndScreen::cardPicture( (Int)h );
	const Real pt = h - pic < 60.0f ? y0 + ( h - pic ) * 0.5f : y0 + 10.0f;
	const Real px0 = x0 + 12.0f;
	putFill( px0, pt, px0 + pic, pt + pic, UIStyle::CARD );
	putImage( player.portrait ? player.portrait : player.icon, px0, pt, px0 + pic, pt + pic );
	putFill( px0, pt + pic - 4.0f, px0 + pic, pt + pic, player.color );
	if( tooDark( player.color ) )
		putFill( px0, pt + pic - 5.0f, px0 + pic, pt + pic - 4.0f, LIGHT_KEY );

	const Real tx = px0 + pic + 18.0f;
	const Real ny = y0 + 8.0f;
	GameFont *nameFont = h > 200.0f ? s_fNameTall : s_fName;
	const GraphColumns cols = columnsFor( x1, FALSE, tx );
	Real right = x1 - 8.0f;
	if( player.index >= 0 )
	{
		UnicodeString peak;
		peak.format( fetchText( "GUI:GraphPeak" ), moneyText( armyPeak( player ) ).str() );
		right -= bw( putText( peak, s_fSub, right, ny + 19.0f, UIStyle::MUTED, 2 ) ) + 12.0f;
		right -= bw( putTracked( UIStyle::capitals( fetchText( "GUI:HudStatArmy" ) ), s_fCaps, right, ny + 20.0f, UIStyle::MUTED, 1.2f, 2 ) ) + 16.0f;
	}
	const Int nameWidth = putText( fittedText( player.name, nameFont, ll( min( 320.0f, right - tx ) ) ), nameFont, tx, ny + 17.0f, nameInk( player ) );
	const Real sideLeft = tx + bw( nameWidth ) + 12.0f;
	if( right - sideLeft > 40.0f )
		putText( fittedLabel( player.disconnected ? fetchText( "GUI:SummaryDisconnected" ) : player.side, s_fSub, ll( right - sideLeft ) ),
			s_fSub, sideLeft, ny + 19.0f, UIStyle::MUTED );

	const Real chartTop = ny + 42.0f, chartBottom = y1 - 54.0f;
	if( chartBottom - chartTop >= 24.0f )
		cardChart( player, tx, chartTop, x1 - 8.0f, chartBottom, topArmy, player.local ? UIStyle::GOLD : UIStyle::readable( teamColor ) );

	Bool best[ COL_COUNT ];
	for( Int col = 0; col < COL_COUNT; col++ )
		best[ col ] = bestRow[ col ] == row;
	figures( cols, player.stats, best, y1 - 31.0f, h > 200.0f ? s_fNameTall : s_fName, s_fBody, UIStyle::INK,
		(Real)player.stats[ COL_SCORE ] / topScore, UIStyle::readable( teamColor ) );
}

/** Five a team and more, and every place: [place], stripe, name, general, the figures.  A winner's
	* place is green with a green edge light; the local player's gold, larger. */
static void lineRow( const GraphPlayer &player, Int row, Real x0, Real y0, Real x1, Real y1, const GraphColumns &cols,
	const Int bestRow[ COL_COUNT ], Int topScore, Int place, Bool winner, Bool zebra, Color teamColor )
{
	putRow( x0, y0, x1, y1, player.local, zebra );
	const Real middle = ( y0 + y1 ) * 0.5f;
	Real lead = 0.0f;
	if( place > 0 )
	{
		lead = 40.0f;
		if( winner && !player.local )
			putLit( x0, y0, x0 + 3.0f, y1, UIStyle::ACTIVE );
		UnicodeString number;
		number.format( u"%d", place );
		putText( number, player.local ? UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE, TRUE ) : s_fBodyBold, x0 + lead - 2.0f, middle,
			player.local ? UIStyle::GOLD : winner ? UIStyle::ACTIVE : UIStyle::MUTED, 2 );
	}
	playerStripe( x0 + lead + 8.0f, y0 + 7.0f, x0 + lead + 12.0f, y1 - 7.0f, player.color );
	const Real ax = x0 + 150.0f + lead * 0.5f;
	const Real nameLeft = x0 + lead + 22.0f;
	putText( fittedText( player.name, s_fBody, ll( ax - 8.0f - nameLeft ) ), s_fBody, nameLeft, middle, nameInk( player ) );
	const Real sideRoom = cols.left[ COL_SCORE ] - 6.0f - ax;
	if( sideRoom > 20.0f )
	{
		if( player.disconnected )
			putText( fittedLabel( UIStyle::capitals( fetchText( "GUI:SummaryDisconnected" ) ), s_fCaps, ll( sideRoom ) ), s_fCaps, ax, middle, UIStyle::MUTED );
		else
			putText( fittedLabel( player.side, s_fSmall, ll( sideRoom ) ), s_fSmall, ax, middle, UIStyle::MUTED );
	}
	Bool best[ COL_COUNT ];
	for( Int col = 0; col < COL_COUNT; col++ )
		best[ col ] = bestRow[ col ] == row;
	figures( cols, player.stats, best, middle, s_fLead, cols.dense ? s_fSub : s_fBody, UIStyle::INK, (Real)player.stats[ COL_SCORE ] / topScore,
		UIStyle::readable( teamColor ) );
}

/// a team of two or more inside a wing of several: its lit colour, its place, TEAM n, VICTORY, its sums
static void bandRow( Int t, Real x0, Real y0, Real x1, Real y1, const GraphColumns &cols )
{
	const ScoreTeam &team = s_order[ t ];
	const Real middle = ( y0 + y1 ) * 0.5f;
	Bool winner = FALSE, disconnected = TRUE;
	UnsignedInt fell = 0;
	Int sums[ COL_COUNT ] = { 0 };
	for( size_t m = 0; m < team.members.size(); m++ )
	{
		const GraphPlayer &player = s_players[ team.members[ m ] ];
		winner = winner || player.winner;
		disconnected = disconnected && player.disconnected;
		fell = max( fell, player.fell );
		for( Int col = 0; col < COL_COUNT; col++ )
			sums[ col ] += player.stats[ col ];
	}
	putFill( x0, y0, x1, y1, UIStyle::withAlpha( UIStyle::INK, 10 ) );
	putLit( x0, y0 + 3.0f, x0 + 4.0f, y1 - 3.0f, teamInk( t ) );
	UnicodeString place;
	place.format( u"%d", t + 1 );
	putText( place, s_fBodyBold, x0 + 36.0f, middle, winner ? UIStyle::ACTIVE : UIStyle::MUTED, 2 );
	UnicodeString name;
	name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
	const Int nameWidth = putTracked( UIStyle::capitals( name ), s_fBand, x0 + 46.0f, middle, UIStyle::INK, 1.8f );
	UnicodeString verdict;
	Color color;
	if( verdictFor( winner, disconnected, fell, verdict, color ) && ( winner || s_apocalypse ) )
		putTracked( UIStyle::capitals( verdict ), s_fCaps, x0 + 56.0f + bw( nameWidth ), middle, color, 1.4f );
	const Bool none[ COL_COUNT ] = { FALSE };
	figures( cols, sums, none, middle, s_fBody, s_fSub, UIStyle::MUTED, -1.0f, 0 );
}

/// two teams of more than one: the TOTAL line under a team
static void totalsRow( Int t, Real x0, Real x1, Real y, const GraphColumns &cols )
{
	const ScoreTeam &team = s_order[ t ];
	Int sums[ COL_COUNT ] = { 0 };
	for( size_t m = 0; m < team.members.size(); m++ )
		for( Int col = 0; col < COL_COUNT; col++ )
			sums[ col ] += s_players[ team.members[ m ] ].stats[ col ];
	putRule( x0, y, x1, y, UIStyle::withAlpha( UIStyle::FRAME, 140 ) );
	putTracked( UIStyle::capitals( fetchText( "GUI:ScoreTotal" ) ), s_fCaps, x0 + 16.0f, y + 18.0f, UIStyle::MUTED, 1.2f );
	const Bool none[ COL_COUNT ] = { FALSE };
	figures( cols, sums, none, y + 18.0f, s_fBody, s_fBody, UIStyle::MUTED, -1.0f, 0 );
}

/** The highlights: the most enemy value destroyed, the top earner, the biggest army at its peak and
	* the first superweapon built, each left out where nobody has one.  The line foot lays them in a
	* row, the wide foot two by two with what they measured larger. */
static void layoutHighlights( Bool wide, Real x0, Real x1, Real y0, Real y1 )
{
	if( s_campaign || s_history.getSampleCount() == 0 )
		return;
	const Int last = shownSamples() - 1;
	const GraphPlayer *best[ 4 ] = { NULL, NULL, NULL, NULL };
	Int bestValue[ 4 ] = { 0, 0, 0, 0 };
	for( size_t each = 0; each < s_players.size(); each++ )
	{
		const GraphPlayer &player = s_players[ each ];
		if( player.index < 0 || !s_history.wasSampled( player.index ) )
			continue;
		const Int values[ 3 ] = { s_history.getValue( last, player.index, MATCH_SERIES_VALUE_DESTROYED ), player.stats[ COL_EARNED ], armyPeak( player ) };
		for( Int kind = 0; kind < 3; kind++ )
		{
			if( values[ kind ] > bestValue[ kind ] )
			{
				bestValue[ kind ] = values[ kind ];
				best[ kind ] = &player;
			}
		}
	}
	const std::vector< MatchEvent > &events = s_history.getEvents();
	for( size_t each = 0; each < events.size() && best[ 3 ] == NULL; each++ )
	{
		if( events[ each ].type == MATCH_EVENT_SUPERWEAPON_BUILT && events[ each ].frame <= historyEnd() )
		{
			best[ 3 ] = playerFor( events[ each ].player );
			bestValue[ 3 ] = (Int)events[ each ].frame;
		}
	}
	static const char *const labels[ 4 ] =
	{
		"GUI:ScoreHighlightDestroyed", "GUI:ScoreHighlightEarned", "GUI:ScoreHighlightArmy", "GUI:ScoreHighlightSuperweapon"
	};
	static const char *const measured[ 4 ] =
	{
		"GUI:ScoreHighlightDestroyedValue", "GUI:ScoreHighlightEarnedValue", "GUI:ScoreHighlightArmyValue", NULL
	};

	Int shown = 0;
	for( Int kind = 0; kind < 4; kind++ )
	{
		if( best[ kind ] == NULL )
			continue;
		UnicodeString value;
		if( measured[ kind ] )
			value.format( fetchText( measured[ kind ] ), moneyText( bestValue[ kind ] ).str() );
		else
			value = clockText( (UnsignedInt)bestValue[ kind ] );
		if( wide )
		{
			const Real cw = ( x1 - x0 ) * 0.5f, ch = ( y1 - y0 ) * 0.5f;
			const Real cx = x0 + ( shown % 2 ) * cw, cy = y0 + ( shown / 2 ) * ch;
			if( shown % 2 )
				putRule( cx - 14.0f, cy + 10.0f, cx - 14.0f, cy + ch - 10.0f, UIStyle::withAlpha( UIStyle::FRAME, 90 ) );
			if( shown >= 2 )
				putRule( x0, cy, x1, cy, UIStyle::withAlpha( UIStyle::FRAME, 90 ) );
			const Int room = ll( cw - 34.0f );
			putText( fittedLabel( fetchText( labels[ kind ] ), s_fHighlight, room ), s_fHighlight, cx + 10.0f, cy + 24.0f, UIStyle::MUTED );
			putText( fittedText( best[ kind ]->name, s_fHighlightBig, room ), s_fHighlightBig, cx + 10.0f, cy + 54.0f, nameInk( *best[ kind ] ) );
			putText( fittedLabel( value, s_fBody, room ), s_fBody, cx + 10.0f, cy + 86.0f, UIStyle::INK );
		}
		else
		{
			const Real cw = ( x1 - x0 ) / 4.0f;
			const Real cx = x0 + shown * cw;
			const Int room = ll( cw - 16.0f );
			putText( fittedLabel( fetchText( labels[ kind ] ), s_fHighlight, room ), s_fHighlight, cx, y0 + 22.0f, UIStyle::MUTED );
			putText( fittedText( best[ kind ]->name, s_fFacts, room ), s_fFacts, cx, y0 + 52.0f, nameInk( *best[ kind ] ) );
			putText( fittedLabel( value, s_fSub, room ), s_fSub, cx, y0 + 82.0f, UIStyle::MUTED );
		}
		shown++;
	}
}

/** The map in a square at x, y: its own shape inside the square, every start the match used on it.
	* Up to MAP_RINGS_MOST players a numbered ring each, the local player's in gold; past it a dot in the
	* team's colour, the local player's ringed in gold. */
static void layoutMap( Real x, Real y, Real size )
{
	const MapMetaData *map = TheMapCache->findMap( TheGlobalData->m_mapName );
	s_mapShown = TRUE;
	Int mapWidth = ll( size ), mapHeight = ll( size );
	if( map && s_mapImage && map->m_extent.width() > 0.0f && map->m_extent.height() > 0.0f )
	{
		const Real aspect = map->m_extent.width() / map->m_extent.height();
		if( aspect >= 1.0f )
			mapHeight = max( REAL_TO_INT( mapWidth / aspect ), 1 );
		else
			mapWidth = max( REAL_TO_INT( mapHeight * aspect ), 1 );
	}
	s_mapBox.lo.x = lx( x ) + ( ll( size ) - mapWidth ) / 2;
	s_mapBox.lo.y = ly( y ) + ( ll( size ) - mapHeight ) / 2;
	s_mapBox.hi.x = s_mapBox.lo.x + mapWidth;
	s_mapBox.hi.y = s_mapBox.lo.y + mapHeight;
	s_mapDrawn = s_mapBox;
	if( map == NULL || s_mapImage == NULL || !map->m_isMultiplayer || TheGameInfo == NULL )
		return;
	const Bool rings = (Int)s_players.size() <= MAP_RINGS_MOST;
	for( Int start = 0; start < map->m_numPlayers; start++ )
	{
		AsciiString waypoint;
		waypoint.format( "Player_%d_Start", start + 1 );
		WaypointMap::const_iterator it = map->m_waypoints.find( waypoint );
		if( it == map->m_waypoints.end() )
			continue;
		const Real across = ( it->second.x - map->m_extent.lo.x ) / map->m_extent.width();
		const Real down = 1.0f - ( it->second.y - map->m_extent.lo.y ) / map->m_extent.height();
		for( size_t each = 0; each < s_players.size(); each++ )
		{
			const GraphPlayer &player = s_players[ each ];
			const GameSlot *slot = player.slot >= 0 ? TheGameInfo->getConstSlot( player.slot ) : NULL;
			if( slot == NULL || slot->getStartPos() != start )
				continue;
			GraphSpot spot;
			spot.pos.x = s_mapDrawn.lo.x + REAL_TO_INT( across * ( s_mapDrawn.hi.x - s_mapDrawn.lo.x ) );
			spot.pos.y = s_mapDrawn.lo.y + REAL_TO_INT( down * ( s_mapDrawn.hi.y - s_mapDrawn.lo.y ) );
			// a ring wears the player's colour; past the rings a dot wears his team's
			spot.color = player.color;
			if( !rings && !s_freeForAll )
			{
				for( size_t t = 0; t < s_order.size(); t++ )
					if( s_order[ t ].team == player.team )
						spot.color = teamInk( (Int)t );
			}
			spot.number = NULL;
			if( rings )
			{
				AsciiString seat;
				seat.format( "NUMBER:%d", player.slot + 1 );
				spot.number = newText( TheGameText->fetch( seat ), s_fCaps );
			}
			spot.dimmed = s_anyWinner && !player.winner;
			spot.local = player.local;
			s_spots.push_back( spot );
		}
	}
}

/** The Summary's foot from top: the map with its name and format and the best-in-column key, the
	* middle (highlights, the chat or the rating card), and the chat key on the key row. */
static void layoutFoot( Int foot )
{
	using namespace EndScreen;
	const Real top = footTop( foot );
	putRule( INSET_LEFT, top, INSET_RIGHT, top, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	const Real y = top + FOOT_PAD, y1 = FOOT_BOTTOM;
	const Real map = (Real)footMap( foot );
	layoutMap( INSET_LEFT, y, map );
	const Real mx = INSET_LEFT + map + 16.0f;
	const Bool wide = foot == FOOT_WIDE;
	const Real middleLeft = (Real)footMiddleLeft( foot );
	const Int room = ll( middleLeft - 16.0f - mx );
	putText( fittedLabel( s_mapName, wide ? s_fFacts : s_fBody, room ), wide ? s_fFacts : s_fBody, mx, y + ( wide ? 16.0f : 12.0f ), UIStyle::INK );
	putText( fittedLabel( s_format, s_fSmall, room ), s_fSmall, mx, y + ( wide ? 44.0f : 38.0f ), UIStyle::MUTED );
	if( !s_campaign && s_players.size() >= 3 )
	{
		putFill( mx, y1 - 15.0f, mx + 20.0f, y1 - 12.0f, UIStyle::EDGE_LIT );
		putText( fittedLabel( fetchText( "GUI:ScoreBestInColumn" ), s_fSmall, room - ll( 28.0f ) ), s_fSmall, mx + 28.0f, y1 - 13.0f, UIStyle::MUTED );
	}

	const Real middleRight = (Real)footMiddleRight( s_keys + ( s_chatAvailable ? 1 : 0 ) );
	s_footArea[ 0 ] = middleLeft;
	s_footArea[ 1 ] = y;
	s_footArea[ 2 ] = middleRight;
	s_footArea[ 3 ] = y1;
	if( s_foot == FOOT_HIGHLIGHTS )
		layoutHighlights( wide, middleLeft, middleRight, y, y1 );
}

/** The chat key at the key row's next place left of ScoreScreen.cpp's keys; on every page, since the
	* chat lives on the Summary's foot. */
static void layoutChatKey( void )
{
	if( !s_chatAvailable )
		return;
	Int x0, x1;
	EndScreen::keyRect( s_keys, x0, x1 );
	addHit( HIT_CHAT, 0, s_tab == TAB_SUMMARY && s_foot == FOOT_CHAT, UIStyle::capitals( fetchText( "GUI:ScoreChat" ) ),
		UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE ), lx( x0 ), ly( EndScreen::KEYS_TOP ), ll( x1 - x0 ), ll( EndScreen::KEYS_BOTTOM - EndScreen::KEYS_TOP ) );
}

static void layoutSummary( void )
{
	using namespace EndScreen;
	if( s_order.empty() )
		return;
	std::vector< Int > sizes;
	for( size_t t = 0; t < s_order.size(); t++ )
		sizes.push_back( (Int)s_order[ t ].members.size() );
	SummaryPlan plan = summaryPlan( sizes, s_freeForAll );
	if( s_campaign && plan.totals )
	{
		// a mission's rows are the player, his allies and the enemy: no total sums them
		plan.totals = FALSE;
		plan.rowHeight = min( plan.rowHeight + TOTALS / max( sizes[ 0 ], 1 ), plan.kind == ROWS_LINE ? LINE_ROW_MAX : 10000 );
	}

	Int bestRow[ COL_COUNT ];
	columnBests( bestRow );
	const Int topScore = bestScore();
	Int topArmy = 1;
	for( size_t each = 0; each < s_players.size(); each++ )
		topArmy = max( topArmy, armyPeak( s_players[ each ] ) );
	const Real wingX0[ 2 ] = { plan.single ? (Real)INSET_LEFT : (Real)WING_LEFT_X0, (Real)WING_RIGHT_X0 };
	const Real wingX1[ 2 ] = { plan.single ? (Real)INSET_RIGHT : (Real)WING_LEFT_X1, (Real)WING_RIGHT_X1 };

	if( plan.kind == ROWS_CARD || plan.kind == ROWS_LINE )
	{
		for( size_t t = 0; t < s_order.size(); t++ )
		{
			const ScoreTeam &team = s_order[ t ];
			const Real x0 = wingX0[ plan.wing[ t ] ], x1 = wingX1[ plan.wing[ t ] ];
			const Bool dense = (Int)team.members.size() >= DENSE_FROM;
			const Real pic = (Real)cardPicture( plan.rowHeight );
			const GraphColumns cols = plan.kind == ROWS_CARD ? columnsFor( x1, FALSE, x0 + 12.0f + pic + 18.0f ) : columnsFor( x1, dense );
			teamHead( (Int)t, x0, x1, CONTENT_TOP, cols );
			Real y = CONTENT_TOP + TEAM_HEAD;
			for( size_t m = 0; m < team.members.size(); m++ )
			{
				const Int row = team.members[ m ];
				if( plan.kind == ROWS_CARD )
					cardRow( s_players[ row ], row, x0, y, x1, y + plan.rowHeight, bestRow, topScore, topArmy, teamInk( (Int)t ) );
				else
					lineRow( s_players[ row ], row, x0, y, x1, y + plan.rowHeight, cols, bestRow, topScore, 0, FALSE, ( m % 2 ) == 1, teamInk( (Int)t ) );
				y += plan.rowHeight;
			}
			if( plan.totals )
				totalsRow( (Int)t, x0, x1, y + 2.0f, cols );
		}
	}
	else
	{
		// several teams or a free for all: the wings read down the left, then the right
		Int place = 0;
		for( Int w = 0; w < 2; w++ )
		{
			const Real x0 = wingX0[ w ], x1 = wingX1[ w ];
			const GraphColumns cols = columnsFor( x1, TRUE );
			Int first = 0, last = 0;
			for( size_t t = 0; t < s_order.size(); t++ )
			{
				if( plan.wing[ t ] != w )
					continue;
				const Int from = plan.kind == ROWS_PLACES ? place + 1 : (Int)t + 1;
				if( first == 0 )
					first = from;
				last = plan.kind == ROWS_PLACES ? place + (Int)s_order[ t ].members.size() : (Int)t + 1;
				if( plan.kind == ROWS_PLACES )
					place += (Int)s_order[ t ].members.size();
			}
			if( first == 0 )
				continue;
			wingHead( first, last, x0, x1, CONTENT_TOP, cols );
			Real y = CONTENT_TOP + WING_HEAD;
			Int shown = first;
			for( size_t t = 0; t < s_order.size(); t++ )
			{
				if( plan.wing[ t ] != w )
					continue;
				const ScoreTeam &team = s_order[ t ];
				const Bool band = plan.kind == ROWS_BANDED && bandedHasBand( (Int)team.members.size() );
				if( band )
				{
					bandRow( (Int)t, x0, y, x1, y + plan.bandHeight - 2.0f, cols );
					y += plan.bandHeight;
				}
				for( size_t m = 0; m < team.members.size(); m++ )
				{
					const Int row = team.members[ m ];
					const GraphPlayer &player = s_players[ row ];
					// a free for all's place and a lone team's own: his team's place in the standings
					const Int rowPlace = band ? 0 : plan.kind == ROWS_PLACES ? shown : (Int)t + 1;
					lineRow( player, row, x0, y, x1, y + plan.rowHeight, cols, bestRow, topScore, rowPlace, player.winner,
						plan.kind == ROWS_PLACES && ( shown % 2 ) == 0, teamInk( (Int)t ) );
					y += plan.rowHeight;
					shown++;
				}
			}
		}
	}
	layoutFoot( plan.foot );
}

//-------------------------------------------------------------------------------------------------
// Graphs
//-------------------------------------------------------------------------------------------------
/** The legend right of the plot: a band a team, a row a player with his value at the end and his
	* peak; hover a row to pick his line out, click it to hide it.  Two columns past sixteen players. */
static void layoutLegend( Int chart )
{
	using namespace EndScreen;
	const MatchHistory &history = s_history;
	if( history.getSampleCount() == 0 )
		return;
	const std::vector< const GraphPlayer * > players = chartedPlayers();
	const Int columns = (Int)players.size() > 16 ? 2 : 1;
	const Real left = (Real)legendLeft( (Int)players.size() );
	const Real columnWidth = ( INSET_RIGHT - left - ( columns - 1 ) * 20.0f ) / columns;
	const Int perColumn = ( (Int)players.size() + columns - 1 ) / columns;
	const Int pad = max( ll( 8.0f ), 4 );
	for( Int c = 0; c < columns; c++ )
	{
		const Int from = c * perColumn, to = min( (Int)players.size(), from + perColumn );
		Int heads = 0, lastTeam = -1;
		for( Int each = from; each < to; each++ )
		{
			if( s_banded && players[ each ]->team != lastTeam )
				heads++;
			lastTeam = players[ each ]->team;
		}
		const Int top = ly( PLOT_TOP ), bottom = ly( PLOT_BOTTOM );
		const ScoreRowFit fit = ScoreScreen_fitRows( to - from, heads, bottom - top, ll( 52.0f ), ll( 34.0f ), max( ll( 30.0f ), s_fBody->height + 4 ),
			max( ll( 22.0f ), s_fCaps->height + 4 ) );
		const Bool twoLines = fit.cardPitch >= ll( 52.0f );
		const Int r0 = lx( left + c * ( columnWidth + 20.0f ) ), r1 = r0 + ll( columnWidth );
		Int y = top;
		lastTeam = -1;
		for( Int each = from; each < to; each++ )
		{
			const GraphPlayer &player = *players[ each ];
			size_t t = 0;
			while( t < s_order.size() && s_order[ t ].team != player.team )
				t++;
			if( s_banded && player.team != lastTeam && t < s_order.size() )
			{
				const Int middle = y + fit.headPitch / 2;
				GraphLit bar;
				bar.box.lo.x = r0;
				bar.box.lo.y = y + fit.headPitch / 4;
				bar.box.hi.x = r0 + max( ll( 4.0f ), 2 );
				bar.box.hi.y = y + fit.headPitch * 3 / 4;
				bar.color = teamInk( (Int)t );
				s_lit.push_back( bar );
				UnicodeString name;
				name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
				addText( fittedLabel( UIStyle::capitals( name ), s_fCaps, ( r1 - r0 ) / 2 ), s_fCaps, r0 + pad * 2, middle, UIStyle::INK );
				// the team's sum of its members' end values
				Int sum = 0;
				for( size_t m = 0; m < s_order[ t ].members.size(); m++ )
				{
					const GraphPlayer &member = s_players[ s_order[ t ].members[ m ] ];
					if( member.index >= 0 && !s_hidden[ member.index ] )
						sum += chartValues( chart, member )[ lastSampleOf( member ) ];
				}
				addText( UIStyle::grouped( sum ), s_fSub, r1 - pad, middle, UIStyle::MUTED, 2 );
				y += fit.headPitch;
			}
			lastTeam = player.team;

			const std::vector< Int > values = chartValues( chart, player );
			const Int last = lastSampleOf( player );
			Int peak = 0;
			for( Int sample = 0; sample <= last; sample++ )
				peak = max( peak, values[ sample ] );
			const Bool hidden = s_hidden[ player.index ];
			const Int rowHeight = fit.cardPitch - 2;
			GraphRow row;
			row.box.lo.x = r0;
			row.box.lo.y = y;
			row.box.hi.x = r1;
			row.box.hi.y = y + rowHeight;
			row.local = player.local;
			row.zebra = FALSE;
			row.hovers = FALSE;		// the legend's hover is its picked look, drawn with the hits
			s_rows.push_back( row );
			const Int middle = y + rowHeight / 2;
			addFill( r0 + pad, y + ll( 7.0f ), max( ll( 4.0f ), 2 ), rowHeight - 2 * ll( 7.0f ), hidden ? UIStyle::withAlpha( player.color, 70 ) : player.color );
			const Int topLine = twoLines ? middle - s_fBody->height / 2 - 2 : middle;
			const Int valueWidth = addText( UIStyle::grouped( values[ last ] ), s_fBody, r1 - pad, topLine, hidden ? UIStyle::MUTED : UIStyle::INK, 2 );
			const Int textLeft = r0 + pad * 2 + ll( 4.0f );
			addText( fittedText( player.name, s_fBody, r1 - pad * 3 - valueWidth - textLeft ), s_fBody, textLeft, topLine,
				hidden ? UIStyle::MUTED : nameInk( player ) );
			if( twoLines )
			{
				UnicodeString peakText;
				peakText.format( fetchText( "GUI:GraphPeak" ), UIStyle::grouped( peak ).str() );
				const Int under = middle + s_fSmall->height / 2 + 2;
				const Int peakWidth = addText( fittedLabel( peakText, s_fSmall, ( r1 - r0 ) / 2 ), s_fSmall, r1 - pad, under, UIStyle::MUTED, 2 );
				addText( fittedLabel( player.side, s_fSmall, r1 - pad * 3 - peakWidth - textLeft ), s_fSmall, textLeft, under, UIStyle::MUTED );
			}
			GraphHit hit;
			hit.kind = HIT_LEGEND;
			hit.value = player.index;
			hit.open = !hidden;
			hit.label = NULL;
			hit.box = row.box;
			s_hits.push_back( hit );
			y += fit.cardPitch;
		}
	}
}

static void layoutGraphs( void )
{
	using namespace EndScreen;
	// lines or share at the row's right end, the series as chips from its left
	const Int chipPad = ll( 16.0f );
	GameFont *font = tabFont();
	const UnicodeString lines = UIStyle::capitals( fetchText( "GUI:GraphLines" ) );
	const UnicodeString share = UIStyle::capitals( fetchText( "GUI:GraphShare" ) );
	const Int shareWidth = measure( share, font ) + chipPad * 2;
	const Int linesWidth = measure( lines, font ) + chipPad * 2;
	const Int chipTop = ly( CONTENT_TOP ), chipHeight = ll( CHIP_HEIGHT );
	const Int shareLeft = lx( INSET_RIGHT ) - shareWidth;
	const Int linesLeft = shareLeft - ll( 8.0f ) - linesWidth;
	addHit( HIT_SHARE, 0, !s_share, lines, font, linesLeft, chipTop, linesWidth, chipHeight );
	addHit( HIT_SHARE, 1, s_share, share, font, shareLeft, chipTop, shareWidth, chipHeight );

	const Int room = linesLeft - ll( 24.0f ) - lx( INSET_LEFT );
	Int whole = 0;
	for( Int chart = 0; chart < CHART_COUNT; chart++ )
		whole += measure( UIStyle::capitals( fetchText( theChartLabels[ chart ] ) ), font ) + chipPad * 2 + ll( 8.0f );
	// short of room every chip takes an even share, its words shortened by the language's rule
	const Int widest = whole > room ? room / CHART_COUNT - chipPad * 2 - ll( 8.0f ) : 100000;
	Int x = lx( INSET_LEFT );
	for( Int chart = 0; chart < CHART_COUNT; chart++ )
	{
		const UnicodeString label = UIStyle::capitals( UIStyle::fitKey( theChartLabels[ chart ], font, widest ) );
		x = addHit( HIT_CHART, chart, chart == s_chart, label, font, x, chipTop, measure( label, font ) + chipPad * 2, chipHeight ) + ll( 8.0f );
	}

	const Int legend = legendLeft( (Int)chartedPlayers().size() );
	layoutChart( s_chart, s_share, lx( INSET_LEFT ), ly( PLOT_TOP ), lx( (Real)( legend - PLOT_LEGEND_GAP ) ), ly( PLOT_BOTTOM ) + chartFootHeight() );
	layoutLegend( s_chart );
	putRule( INSET_LEFT, PAGE_FOOT_RULE, INSET_RIGHT, PAGE_FOOT_RULE, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
}

//-------------------------------------------------------------------------------------------------
// Build order
//-------------------------------------------------------------------------------------------------
struct ShownBuild
{
	UnsignedInt frame;
	const ThingTemplate *thing;
	const Image *image;
};

/// what a player's track shows: every build with a picture, or the first of each
static std::vector< ShownBuild > shownBuilds( const GraphPlayer &player )
{
	std::vector< ShownBuild > shown;
	std::vector< const ThingTemplate * > seen;
	const std::vector< MatchBuild > &builds = s_history.getBuilds( player.index );
	for( size_t each = 0; each < builds.size(); each++ )
	{
		// a picture says what was built; something with none says nothing, and gets left out
		const Image *image = buildImage( builds[ each ].thing );
		if( image == NULL )
			continue;
		if( !s_everyBuild )
		{
			if( std::find( seen.begin(), seen.end(), builds[ each ].thing ) != seen.end() )
				continue;
			seen.push_back( builds[ each ].thing );
		}
		ShownBuild build;
		build.frame = builds[ each ].frame;
		build.thing = builds[ each ].thing;
		build.image = image;
		shown.push_back( build );
	}
	return shown;
}

/// a dashed upright line in the panel's pixels
static void addDashes( Int x, Int y0, Int y1, Color color )
{
	const Int dash = max( ll( 4.0f ), 2 );
	for( Int y = y0; y < y1; y += dash * 2 )
		addFill( x, y, 1, min( dash, y1 - y ), color );
}

/// two cyan slashes across the axis at x, the fold's and the opening's mark
static void addSlashes( Int x, Int middle )
{
	const Int rise = max( ll( 6.0f ), 3 ), gap = max( ll( 3.0f ), 2 );
	addRule( x - gap - rise / 2, middle + rise, x - gap + rise / 2, middle - rise, UIStyle::EDGE_LIT, 1.5f );
	addRule( x + gap - rise / 2, middle + rise, x + gap + rise / 2, middle - rise, UIStyle::EDGE_LIT, 1.5f );
}

/** One wing of the build order: a name block and a track a player, under a time axis of its own that
	* folds where nobody in the wing built. */
static void layoutBuildWing( const std::vector< const GraphPlayer * > &players, Real x0, Real x1, Bool tight )
{
	using namespace EndScreen;
	if( players.empty() )
		return;
	const Int count = (Int)players.size();
	const Real block = tight ? (Real)NAME_BLOCK_TIGHT : (Real)NAME_BLOCK;
	const Int left = lx( x0 + block + 12.0f ), right = lx( x1 );
	const Int tracksTop = ly( TRACKS_TOP );
	Int heads = 0, lastTeam = -1;
	for( Int each = 0; each < count; each++ )
	{
		if( s_banded && players[ each ]->team != lastTeam )
			heads++;
		lastTeam = players[ each ]->team;
	}
	const Int available = ly( TRACKS_BOTTOM ) - tracksTop;
	const Int roomy = available / max( count, 2 );
	const ScoreRowFit fit = ScoreScreen_fitRows( count, heads, available, min( roomy, ll( 160.0f ) ), ll( 28.0f ),
		max( ll( 30.0f ), s_fBody->height + 6 ), max( ll( 22.0f ), s_fCaps->height + 4 ) );
	const Int rowHeight = fit.cardPitch;
	const Int tracksBottom = tracksTop + fit.used;

	// the axis: every build shown keeps its room, the quiet stretches between fold
	std::vector< std::vector< ShownBuild > > builds( count );
	std::vector< UnsignedInt > frames;
	UnsignedInt lastBuild = 0;
	for( Int each = 0; each < count; each++ )
	{
		builds[ each ] = shownBuilds( *players[ each ] );
		for( size_t b = 0; b < builds[ each ].size(); b++ )
		{
			frames.push_back( builds[ each ][ b ].frame );
			lastBuild = max( lastBuild, builds[ each ][ b ].frame );
		}
	}
	const UnsignedInt fps = LOGICFRAMES_PER_SECOND;
	const UnsignedInt end = buildAxisEnd( lastBuild, fps, 30 );
	const Bool crowded = count >= 16;
	const std::vector< AxisStretch > axis = foldAxis( frames, end, ( crowded ? 12 : 20 ) * fps, ( crowded ? 24 : 40 ) * fps, ll( (Real)FOLD_PX ), left,
		right - left );
	const Int labelMiddle = tracksTop - s_fSmall->height / 2 - ll( 4.0f );

	// the opening lit faintly to 5:00, its end marked by two slashes and a dashed cyan line
	if( end > OPENING_FRAMES && axisKeeps( axis, OPENING_FRAMES ) )
	{
		const Int x5 = REAL_TO_INT( axisX( axis, OPENING_FRAMES ) );
		addFill( left, tracksTop, x5 - left, tracksBottom - tracksTop, UIStyle::withAlpha( UIStyle::INK, 6 ) );
		addSlashes( x5, labelMiddle + s_fSmall->height );
		addDashes( x5, tracksTop, tracksBottom, UIStyle::withAlpha( UIStyle::EDGE_LIT, 140 ) );
	}
	// ticks on every minute inside a stretch, labelled where 60 px apart; a stretch after a fold starts on
	// a white label, so the jump is read; the last tick says where the axis ends
	const UnsignedInt minute = 60 * fps;
	Int lastLabel = -1000000;
	const Int labelGap = ll( 60.0f );
	const Int endX = REAL_TO_INT( axisX( axis, end ) );
	for( size_t s = 0; s < axis.size(); s++ )
	{
		if( s > 0 )
		{
			// the fold: a dark band down the tracks between dashed cyan edges, two slashes on the axis
			const Int f0 = REAL_TO_INT( axis[ s - 1 ].x1 ), f1 = REAL_TO_INT( axis[ s ].x0 );
			addFill( f0, tracksTop, f1 - f0, tracksBottom - tracksTop, GameMakeColor( 0, 0, 0, 90 ) );
			addDashes( f0, tracksTop, tracksBottom, UIStyle::EDGE_LIT );
			addDashes( f1, tracksTop, tracksBottom, UIStyle::EDGE_LIT );
			addSlashes( ( f0 + f1 ) / 2, labelMiddle + s_fSmall->height );
			if( f1 - lastLabel >= labelGap && endX - f1 >= labelGap )
			{
				addText( clockText( axis[ s ].from ), s_fCaps, f1, labelMiddle, UIStyle::INK, 0 );
				lastLabel = f1;
			}
		}
		for( UnsignedInt frame = ( axis[ s ].from + minute - 1 ) / minute * minute; frame <= axis[ s ].to; frame += minute )
		{
			const Int x = REAL_TO_INT( axisX( axis, frame ) );
			addRule( x, tracksTop, x, tracksBottom, UIStyle::GRID );
			if( frame == end || x - lastLabel < labelGap || endX - x < labelGap )
				continue;
			addText( clockText( frame ), s_fSmall, x, labelMiddle, UIStyle::MUTED, frame == 0 ? 0 : 1 );
			lastLabel = x;
		}
	}
	addText( clockText( end ), s_fCaps, endX, labelMiddle, UIStyle::INK, 2 );

	// the pictures at 1.36:1, up to 64 tall in up to five lanes, fewer where they would shrink too far
	const Int inner = rowHeight - ll( 8.0f );
	const Int lanes = max( 1, min( (Int)BUILD_LANES_MOST, inner / ( max( ll( 24.0f ), 12 ) + 3 ) ) );
	const Int icon = max( 8, min( ll( (Real)BUILD_PICTURE_MOST ), inner / lanes - 3 ) );
	const Int iconWidth = icon * 136 / 100;
	const Int stripe = max( ll( 4.0f ), 2 );
	const Int pad = max( ll( 8.0f ), 4 );
	const Int nameLeft = lx( x0 ) + pad + stripe + pad;
	const Int nameRoom = lx( x0 + block ) - nameLeft;
	Int y = tracksTop;
	lastTeam = -1;
	for( Int each = 0; each < count; each++ )
	{
		const GraphPlayer &player = *players[ each ];
		size_t t = 0;
		while( t < s_order.size() && s_order[ t ].team != player.team )
			t++;
		if( s_banded && player.team != lastTeam && t < s_order.size() )
		{
			const Int middle = y + fit.headPitch / 2;
			GraphLit bar;
			bar.box.lo.x = lx( x0 );
			bar.box.lo.y = y + fit.headPitch / 4;
			bar.box.hi.x = lx( x0 ) + stripe;
			bar.box.hi.y = y + fit.headPitch * 3 / 4;
			bar.color = teamInk( (Int)t );
			s_lit.push_back( bar );
			UnicodeString name;
			name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
			addText( fittedLabel( UIStyle::capitals( name ), s_fCaps, nameRoom ), s_fCaps, nameLeft, middle, UIStyle::INK );
			y += fit.headPitch;
		}
		lastTeam = player.team;

		const Int cardHeight = rowHeight - 4;
		const Int middle = y + cardHeight / 2;
		GraphRow row;
		row.box.lo.x = lx( x0 );
		row.box.lo.y = y;
		row.box.hi.x = right;
		row.box.hi.y = y + cardHeight;
		row.local = player.local;
		row.zebra = FALSE;
		row.hovers = TRUE;
		s_rows.push_back( row );
		addFill( lx( x0 ) + pad, y + ll( 6.0f ), stripe, cardHeight - 2 * ll( 6.0f ), player.color );
		const Bool tall = cardHeight >= ll( 140.0f );
		const Bool twoLines = cardHeight >= s_fBody->height + s_fSmall->height + ll( 6.0f );
		const Int nameMiddle = tall ? y + ll( 22.0f ) : twoLines ? middle - s_fSmall->height / 2 - 1 : middle;
		addText( fittedText( player.name, s_fBody, nameRoom ), s_fBody, nameLeft, nameMiddle, nameInk( player ) );
		if( twoLines )
			addText( fittedLabel( player.side, s_fSmall, nameRoom ), s_fSmall, nameLeft, nameMiddle + s_fBody->height / 2 + s_fSmall->height / 2 + 2,
				UIStyle::MUTED );
		if( tall )
		{
			// what he built: units and structures, the figures large
			Int units = 0, structures = 0;
			const std::vector< MatchBuild > &all = s_history.getBuilds( player.index );
			for( size_t b = 0; b < all.size(); b++ )
			{
				if( all[ b ].thing->isKindOf( KINDOF_STRUCTURE ) )
					structures++;
				else
					units++;
			}
			const Int figureMiddle = y + cardHeight - ll( 40.0f );
			const Int unitsWidth = addText( UIStyle::grouped( units ), s_fName, nameLeft, figureMiddle, UIStyle::INK );
			addText( UIStyle::capitals( fetchText( "GUI:BuildOrderUnit" ) ), s_fCaps, nameLeft, figureMiddle + ll( 22.0f ), UIStyle::MUTED );
			const Int second = nameLeft + max( unitsWidth, measure( UIStyle::capitals( fetchText( "GUI:BuildOrderUnit" ) ), s_fCaps ) ) + ll( 24.0f );
			addText( UIStyle::grouped( structures ), s_fName, second, figureMiddle, UIStyle::INK );
			addText( UIStyle::capitals( fetchText( "GUI:BuildOrderStructure" ) ), s_fCaps, second, figureMiddle + ll( 22.0f ), UIStyle::MUTED );
		}
		addRule( left, y + cardHeight, right, y + cardHeight, UIStyle::GRID );

		// each at its true time: the first lane clear there, else over the lane that ends soonest
		std::vector< Int > laneRight( lanes, -iconWidth * 4 );
		const Int lanesTop = y + ( cardHeight - lanes * ( icon + 3 ) ) / 2;
		for( size_t b = 0; b < builds[ each ].size(); b++ )
		{
			const ShownBuild &build = builds[ each ][ b ];
			const Int centre = REAL_TO_INT( axisX( axis, build.frame ) );
			const Int x = max( left, min( centre - iconWidth / 2, right - iconWidth ) );
			const Int lane = buildLane( laneRight, x, iconWidth, 3 );
			const Bool structure = build.thing->isKindOf( KINDOF_STRUCTURE );
			GraphMark mark = newMark( x, lanesTop + lane * ( icon + 3 ), iconWidth, icon, UIStyle::CARD,
				structure ? UIStyle::MUTED : player.local ? UIStyle::GOLD : player.nameColor );
			mark.edgeWidth = structure ? 1.0f : 2.0f;
			mark.image = build.image;
			UnicodeString tip;
			tip.format( u"%s  %s", clockText( build.frame ).str(), build.thing->getDisplayName().str() );
			addTip( mark, tip, UIStyle::INK );
			s_marks.push_back( mark );
		}
		y += rowHeight;
	}
}

static void layoutBuildOrder( void )
{
	using namespace EndScreen;
	const std::vector< const GraphPlayer * > players = chartedPlayers();
	GameFont *font = tabFont();
	const UnicodeString every = UIStyle::capitals( fetchText( "GUI:BuildOrderEvery" ) );
	const Int everyWidth = measure( every, font ) + ll( 32.0f );
	addHit( HIT_EVERY, 0, s_everyBuild, every, font, lx( INSET_RIGHT ) - everyWidth, ly( CONTENT_TOP ), everyWidth, ll( CHIP_HEIGHT ) );
	putRule( INSET_LEFT, PAGE_FOOT_RULE, INSET_RIGHT, PAGE_FOOT_RULE, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	if( players.empty() )
		return;

	// the note: where the axis ends against how long the match ran
	UnsignedInt lastBuild = 0;
	for( size_t each = 0; each < players.size(); each++ )
	{
		const std::vector< ShownBuild > shown = shownBuilds( *players[ each ] );
		if( !shown.empty() )
			lastBuild = max( lastBuild, shown.back().frame );
	}
	UnicodeString note;
	note.format( fetchText( "GUI:BuildOrderAxis" ), clockText( buildAxisEnd( lastBuild, LOGICFRAMES_PER_SECOND, 30 ) ).str(),
		clockText( matchEndFrame() ).str() );
	putText( fittedLabel( note, s_fSub, lx( INSET_RIGHT ) - everyWidth - ll( 24.0f ) - lx( INSET_LEFT ) ), s_fSub, INSET_LEFT,
		CONTENT_TOP + CHIP_HEIGHT * 0.5f, UIStyle::MUTED );

	if( players.size() > 16 )
	{
		const size_t half = ( players.size() + 1 ) / 2;
		layoutBuildWing( std::vector< const GraphPlayer * >( players.begin(), players.begin() + half ), WING_LEFT_X0, WING_LEFT_X1, TRUE );
		layoutBuildWing( std::vector< const GraphPlayer * >( players.begin() + half, players.end() ), WING_RIGHT_X0, WING_RIGHT_X1, TRUE );
	}
	else
		layoutBuildWing( players, INSET_LEFT, INSET_RIGHT, FALSE );

	// the foot names the frames and the fold
	const Real middle = ( KEYS_TOP + KEYS_BOTTOM ) * 0.5f;
	Real x = INSET_LEFT;
	putFill( x, middle - 8.0f, x + 22.0f, middle + 8.0f, UIStyle::CARD );
	s_marks.back().edge = UIStyle::INK;
	s_marks.back().edgeWidth = 2.0f;
	x += 30.0f;
	x += bw( putText( fetchText( "GUI:BuildOrderUnit" ), s_fSmall, x, middle, UIStyle::MUTED ) ) + 24.0f;
	putFill( x, middle - 8.0f, x + 22.0f, middle + 8.0f, UIStyle::CARD );
	s_marks.back().edge = UIStyle::MUTED;
	x += 30.0f;
	x += bw( putText( fetchText( "GUI:BuildOrderStructure" ), s_fSmall, x, middle, UIStyle::MUTED ) ) + 24.0f;
	putFill( x, middle - 10.0f, x + 22.0f, middle + 10.0f, GameMakeColor( 0, 0, 0, 90 ) );
	addDashes( lx( x ), ly( middle - 10.0f ), ly( middle + 10.0f ), UIStyle::EDGE_LIT );
	addDashes( lx( x + 22.0f ), ly( middle - 10.0f ), ly( middle + 10.0f ), UIStyle::EDGE_LIT );
	x += 30.0f;
	putText( fittedLabel( fetchText( "GUI:BuildOrderFolded" ), s_fSmall, ll( (Real)EndScreen::footMiddleRight( s_keys + ( s_chatAvailable ? 1 : 0 ) ) - x ) ),
		s_fSmall, x, middle, UIStyle::MUTED );
}

//-------------------------------------------------------------------------------------------------
static void layoutPanel( void )
{
	clearContent();
	pickFonts();
	prepareStandings();
	layoutHeader();
	if( s_tab == TAB_SUMMARY )
		layoutSummary();
	else if( s_tab == TAB_BUILD_ORDER )
		layoutBuildOrder();
	else
		layoutGraphs();
	layoutChatKey();
}

//-------------------------------------------------------------------------------------------------
static void drawBox( const GraphMark &mark, Int ox, Int oy )
{
	const IRegion2D &box = mark.box;
	const Int w = box.hi.x - box.lo.x;
	const Int h = box.hi.y - box.lo.y;
	if( mark.fill )
		TheDisplay->drawFillRect( ox + box.lo.x, oy + box.lo.y, w, h, mark.fill );
	if( mark.edge )
		TheDisplay->drawOpenRect( ox + box.lo.x, oy + box.lo.y, w, h, mark.edgeWidth, mark.edge );
}

static Bool isInside( const IRegion2D &box, Int ox, Int oy, Int x, Int y )
{
	return x >= ox + box.lo.x && x < ox + box.hi.x && y >= oy + box.lo.y && y < oy + box.hi.y;
}

static IRegion2D onScreen( const IRegion2D &box, Int ox, Int oy )
{
	IRegion2D r;
	r.lo.x = ox + box.lo.x;
	r.lo.y = oy + box.lo.y;
	r.hi.x = ox + box.hi.x;
	r.hi.y = oy + box.hi.y;
	return r;
}

/** A tooltip of lines under (or, near the panel's foot, over) the anchor; key names what it shows,
	* so its strings are made once while it stays.  Dark glass edged in EDGE, as the language's cards. */
static void drawTip( const std::vector< GraphTipLine > &lines, Int key, Int x, Int top, Int bottom, Int ox, Int oy, Int width, Int height )
{
	if( key != s_tipKey )
	{
		clearTip();
		for( size_t line = 0; line < lines.size(); line++ )
		{
			s_tipText.push_back( newText( lines[ line ].text, s_fSub ) );
			s_tipValue.push_back( lines[ line ].value.isEmpty() ? NULL : newText( lines[ line ].value, s_fSub ) );
		}
		s_tipKey = key;
	}
	// the words on the left, the numbers right-aligned on one edge, the same padding all round
	const Int pad = max( ll( 10.0f ), 4 );
	Int wordsWidth = 0, valueWidth = 0;
	const Int lineHeight = s_fSub->height + 4;
	for( size_t line = 0; line < s_tipText.size(); line++ )
	{
		wordsWidth = max( wordsWidth, textWidth( s_tipText[ line ] ) );
		if( s_tipValue[ line ] )
			valueWidth = max( valueWidth, textWidth( s_tipValue[ line ] ) );
	}
	const Int w = max( wordsWidth + ( valueWidth > 0 ? pad * 2 + valueWidth : 0 ) + pad * 2, valueWidth > 0 ? ll( 280.0f ) : 0 );
	const Int h = (Int)s_tipText.size() * lineHeight + pad * 2;
	const Int tx = max( min( x, ox + width - w - 4 ), ox + 4 );
	const Int ty = bottom + 4 + h < oy + height ? bottom + 4 : max( top - h - 4, oy + 4 );
	TheDisplay->drawFillRect( tx, ty, w, h, GameMakeColor( 0, 0, 0, 230 ) );
	TheDisplay->drawOpenRect( tx, ty, w, h, 1.0f, UIStyle::EDGE );
	for( size_t line = 0; line < s_tipText.size(); line++ )
	{
		const Int y = ty + pad + (Int)line * lineHeight;
		s_tipText[ line ]->draw( tx + pad, y, lines[ line ].color, UIStyle::SHADOW );
		if( s_tipValue[ line ] )
			s_tipValue[ line ]->draw( tx + w - pad - textWidth( s_tipValue[ line ] ), y, lines[ line ].color, UIStyle::SHADOW );
	}
}

/** Every player's value at one sample for the crosshair's tooltip, grouped by team. */
static std::vector< GraphTipLine > plotTip( Int sample )
{
	std::vector< GraphTipLine > lines;
	GraphTipLine line;
	line.text = clockText( s_history.getSampleFrame( sample ) );
	line.color = UIStyle::EDGE_LIT;
	lines.push_back( line );
	const std::vector< const GraphPlayer * > players = chartedPlayers();
	Int total = 0;
	std::vector< Int > values( players.size(), 0 );
	for( size_t each = 0; each < players.size(); each++ )
	{
		if( s_hidden[ players[ each ]->index ] )
			continue;
		values[ each ] = chartValues( s_plot.chart, *players[ each ] )[ sample ];
		total += max( values[ each ], 0 );
	}
	// a number as the plot reads it: the share of the whole, or the value itself
	struct Shown
	{
		static UnicodeString of( Int value, Int total, Bool share )
		{
			if( !share )
				return UIStyle::grouped( value );
			return UIStyle::percentText( total > 0 ? max( value, 0 ) * 100 / total : 0 );
		}
	};
	Int lastTeam = -1;
	for( size_t each = 0; each < players.size(); each++ )
	{
		const GraphPlayer &player = *players[ each ];
		if( s_hidden[ player.index ] )
			continue;
		size_t t = 0;
		while( t < s_order.size() && s_order[ t ].team != player.team )
			t++;
		if( player.team != lastTeam && t < s_order.size() && s_banded )
		{
			// the team's row carries its total, summed over its members shown
			Int teamValue = 0;
			for( size_t other = 0; other < players.size(); other++ )
			{
				if( players[ other ]->team == player.team && !s_hidden[ players[ other ]->index ] )
					teamValue += values[ other ];
			}
			line.text.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
			line.value = Shown::of( teamValue, total, s_plot.share );
			line.color = UIStyle::ACTIVE;
			lines.push_back( line );
		}
		lastTeam = player.team;
		line.text = player.name;
		line.value = Shown::of( values[ each ], total, s_plot.share );
		line.color = nameInk( player );
		lines.push_back( line );
	}
	return lines;
}

static void drawLine( const GraphLine &graph, Int ox, Int oy, Int focus )
{
	const std::vector< ICoord2D > &points = graph.points;
	const Bool dim = focus >= 0 && graph.owner != focus;
	const Bool picked = focus >= 0 && graph.owner == focus;
	for( Int pass = 0; pass < 2; pass++ )
	{
		Real width;
		Color color;
		if( dim )
		{
			if( pass == 0 )
				continue;
			width = 1.0f;
			color = UIStyle::withAlpha( graph.local ? UIStyle::GOLD : graph.color, 60 );
		}
		else if( graph.local )
		{
			// white-gold inside a gold edge, so a yellow player's line never reads as his
			width = pass == 0 ? 7.0f : 3.0f;
			color = pass == 0 ? UIStyle::GOLD : LOCAL_LINE;
		}
		else
		{
			width = pass == 0 ? ( picked ? 5.0f : 4.0f ) : ( picked ? 3.0f : 2.0f );
			color = pass == 0 ? KEYLINE : graph.color;
		}
		for( size_t point = 1; point < points.size(); point++ )
			TheDisplay->drawLine( ox + points[ point - 1 ].x, oy + points[ point - 1 ].y, ox + points[ point ].x, oy + points[ point ].y, width, color );
		if( pass == 1 && graph.cross && !points.empty() )
		{
			const Int arm = max( ll( 5.0f ), 3 );
			const Int cx = ox + points.back().x;
			const Int cy = oy + points.back().y;
			TheDisplay->drawLine( cx - arm, cy - arm, cx + arm, cy + arm, 2.0f, color );
			TheDisplay->drawLine( cx - arm, cy + arm, cx + arm, cy - arm, 2.0f, color );
		}
	}
}

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	Int ox, oy, width, height;
	window->winGetScreenPosition( &ox, &oy );
	window->winGetSize( &width, &height );
	IRegion2D panel;
	panel.lo.x = ox;
	panel.lo.y = oy;
	panel.hi.x = ox + width;
	panel.hi.y = oy + height;
	UIStyle::glass( panel, 168 );
	UIStyle::frameA( panel );

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	for( size_t each = 0; each < s_rows.size(); each++ )
	{
		const GraphRow &row = s_rows[ each ];
		const Bool hovered = row.hovers && isInside( row.box, ox, oy, mouse.x, mouse.y );
		UIStyle::menuRow( onScreen( row.box, ox, oy ), row.local ? UIStyle::ROW_LOCAL : hovered ? UIStyle::ROW_HOVER : UIStyle::ROW_PLAIN, row.zebra );
	}
	for( size_t each = 0; each < s_lit.size(); each++ )
	{
		const IRegion2D &box = s_lit[ each ].box;
		UIStyle::litLine( ox + box.lo.x, oy + box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, s_lit[ each ].color );
	}

	Int focus = -1;				// the player whose legend row the mouse is on
	Int focusRow = -1;		// and that row
	for( size_t each = 0; each < s_hits.size(); each++ )
	{
		const GraphHit &hit = s_hits[ each ];
		const Bool hovered = isInside( hit.box, ox, oy, mouse.x, mouse.y );
		if( hit.kind == HIT_LEGEND )
		{
			if( hovered )
			{
				focusRow = (Int)each;
				if( hit.open )
					focus = hit.value;
			}
			continue;
		}
		const IRegion2D box = onScreen( hit.box, ox, oy );
		if( hit.kind == HIT_CHAT )
		{
			const Bool down = hovered && TheMouse->getMouseStatus()->leftState != MBS_Up;
			UIStyle::menuKey( box, hit.label, hit.open || down ? UIStyle::KEY_PRESSED : hovered ? UIStyle::KEY_HOVER : UIStyle::KEY_NORMAL, FALSE );
		}
		else
			UIStyle::menuTab( box, hit.label, hit.open, hovered && !hit.open );
	}
	// the legend row under the pointer is the picked one: an EDGE wash in a cyan box
	if( focusRow >= 0 )
	{
		const IRegion2D box = onScreen( s_hits[ focusRow ].box, ox, oy );
		TheDisplay->drawFillRect( box.lo.x, box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, UIStyle::withAlpha( UIStyle::EDGE, 90 ) );
		TheDisplay->drawOpenRect( box.lo.x, box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, 1.0f, UIStyle::EDGE_LIT );
	}

	// the grid first, the share bands over it, the team lines over the bands
	for( size_t each = 0; each < s_rules.size(); each++ )
	{
		const GraphRule &rule = s_rules[ each ];
		TheDisplay->drawLine( ox + rule.line.lo.x, oy + rule.line.lo.y, ox + rule.line.hi.x, oy + rule.line.hi.y, rule.width, rule.color );
	}
	for( size_t each = 0; each < s_bands.size(); each++ )
	{
		const GraphBand &band = s_bands[ each ];
		const Color color = focus >= 0 && band.owner != focus ? UIStyle::withAlpha( band.color, 40 ) : band.color;
		TheDisplay->drawFillRect( ox + band.box.lo.x, oy + band.box.lo.y, band.box.hi.x - band.box.lo.x, band.box.hi.y - band.box.lo.y, color );
	}
	for( size_t each = 0; each < s_dividers.size(); each++ )
	{
		const IRegion2D &box = s_dividers[ each ].box;
		TheDisplay->drawFillRect( ox + box.lo.x, oy + box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, s_dividers[ each ].color );
	}

	Int hovered = -1;
	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		if( !s_marks[ each ].tip.empty() && isInside( s_marks[ each ].box, ox, oy, mouse.x, mouse.y ) )
			hovered = (Int)each;
	}
	if( hovered >= 0 )
	{
		const GraphMark &mark = s_marks[ hovered ];
		for( size_t each = 0; each < mark.drops.size(); each++ )
			TheDisplay->drawLine( ox + mark.drops[ each ].x, oy + mark.box.hi.y, ox + mark.drops[ each ].x, oy + mark.dropBottom, 1.0f, mark.drops[ each ].color );
	}

	for( size_t each = 0; each < s_marks.size(); each++ )
	{
		const GraphMark &mark = s_marks[ each ];
		drawBox( mark, ox, oy );
		const Int inset = mark.edge ? 2 : 0;
		if( mark.image )
			TheDisplay->drawImage( mark.image, ox + mark.box.lo.x + inset, oy + mark.box.lo.y + inset, ox + mark.box.hi.x - inset, oy + mark.box.hi.y - inset );
		if( mark.label )
		{
			Int w, h;
			mark.label->getSize( &w, &h );
			mark.label->draw( ox + ( mark.box.lo.x + mark.box.hi.x - w ) / 2, oy + ( mark.box.lo.y + mark.box.hi.y - h ) / 2, UIStyle::INK, UIStyle::SHADOW );
		}
		if( mark.count )
		{
			Int w, h;
			mark.count->getSize( &w, &h );
			const Int x = ox + mark.box.hi.x - w - 2;
			const Int y = oy + mark.box.hi.y - h;
			TheDisplay->drawFillRect( x - 2, y, w + 3, h - 1, GameMakeColor( 0, 0, 0, 200 ) );
			mark.count->draw( x, y - 1, UIStyle::INK, UIStyle::SHADOW );
		}
	}

	if( s_mapShown )
	{
		const Int mapWidth = s_mapBox.hi.x - s_mapBox.lo.x;
		const Int mapHeight = s_mapBox.hi.y - s_mapBox.lo.y;
		TheDisplay->drawFillRect( ox + s_mapBox.lo.x, oy + s_mapBox.lo.y, mapWidth, mapHeight, UIStyle::GROUND );
		if( s_mapImage )
			TheDisplay->drawImage( s_mapImage, ox + s_mapDrawn.lo.x, oy + s_mapDrawn.lo.y, ox + s_mapDrawn.hi.x, oy + s_mapDrawn.hi.y );
		TheDisplay->drawOpenRect( ox + s_mapBox.lo.x - 1, oy + s_mapBox.lo.y - 1, mapWidth + 2, mapHeight + 2, 1.0f, UIStyle::EDGE );
		const Int spot = max( max( mapWidth, mapHeight ) * LOBBY_START_SPOT / LOBBY_MAP_PREVIEW, ll( 16.0f ) );
		const Image *ring = TheMappedImageCollection->findImageByName( "PlayerStart" );
		const Image *goldRing = TheMappedImageCollection->findImageByName( "ReforgedLobbySpotRing" );
		const Int dot = max( ll( 4.0f ), 3 );
		for( size_t each = 0; each < s_spots.size(); each++ )
		{
			const GraphSpot &mark = s_spots[ each ];
			const Int cx = ox + mark.pos.x, cy = oy + mark.pos.y;
			const Color fill = mark.dimmed ? UIStyle::withAlpha( mark.color, 90 ) : mark.color;
			if( mark.number == NULL )
			{
				// past eight a dot in the team's colour; the local player's ringed in gold
				if( mark.local )
					TheDisplay->drawOpenRect( cx - dot, cy - dot, dot * 2, dot * 2, 1.0f, UIStyle::GOLD );
				TheDisplay->drawFillRect( cx - dot / 2, cy - dot / 2, dot, dot, fill );
				continue;
			}
			const Int x = cx - spot / 2;
			const Int y = cy - spot / 2;
			if( mark.local && goldRing )
			{
				const Int grow = max( ll( 4.0f ), 2 );
				TheDisplay->drawImage( goldRing, x - grow, y - grow, x + spot + grow, y + spot + grow, UIStyle::GOLD );
			}
			if( ring )
				TheDisplay->drawImage( ring, x, y, x + spot, y + spot );
			LobbyStartSpotFill( x, y, spot, spot, fill );
			Int w, h;
			mark.number->getSize( &w, &h );
			mark.number->draw( x + ( spot - w ) / 2, y + ( spot - h ) / 2, mark.dimmed ? UIStyle::MUTED : LobbyTextColorOn( mark.color ), 0 );
		}
	}

	for( size_t each = 0; each < s_lines.size(); each++ )
		drawLine( s_lines[ each ], ox, oy, focus );

	for( size_t each = 0; each < s_texts.size(); each++ )
	{
		const GraphText &text = s_texts[ each ];
		if( text.text )
			text.text->draw( ox + text.x, oy + text.y, text.color, UIStyle::SHADOW );
		else
			UIStyle::drawTracked( text.words, text.font, ox + text.x, oy + text.y, text.color, text.tracking );
	}

	if( hovered >= 0 )
	{
		const GraphMark &mark = s_marks[ hovered ];
		TheDisplay->drawOpenRect( ox + mark.box.lo.x, oy + mark.box.lo.y, mark.box.hi.x - mark.box.lo.x, mark.box.hi.y - mark.box.lo.y, 1.0f, UIStyle::EDGE_LIT );
		drawTip( mark.tip, hovered, ox + mark.box.lo.x, oy + mark.box.lo.y, oy + mark.box.hi.y, ox, oy, width, height );
	}
	else if( s_plot.live && !s_plot.sampleX.empty() && isInside( s_plot.box, ox, oy, mouse.x, mouse.y ) )
	{
		// the pointer on the nearest sample, and every player's value there
		Int sample = 0;
		for( size_t each = 1; each < s_plot.sampleX.size(); each++ )
		{
			if( abs( ox + s_plot.sampleX[ each ] - mouse.x ) < abs( ox + s_plot.sampleX[ sample ] - mouse.x ) )
				sample = (Int)each;
		}
		const Int x = ox + s_plot.sampleX[ sample ];
		TheDisplay->drawLine( x, oy + s_plot.box.lo.y, x, oy + s_plot.box.hi.y, 1.0f, UIStyle::EDGE_LIT );
		drawTip( plotTip( sample ), 1000000 + sample, x + ll( 12.0f ), mouse.y, mouse.y + ll( 12.0f ), ox, oy, width, height );
	}
	else if( s_tipKey >= 0 )
		clearTip();
}

static WindowMsgHandledType panelInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_CHAR )
		return MSG_IGNORED;
	if( msg == GWM_LEFT_UP )
	{
		Int ox, oy;
		window->winGetScreenPosition( &ox, &oy );
		const Int x = (Int)( mData1 & 0xFFFF );
		const Int y = (Int)( ( mData1 >> 16 ) & 0xFFFF );
		for( size_t each = 0; each < s_hits.size(); each++ )
		{
			const GraphHit &hit = s_hits[ each ];
			if( !isInside( hit.box, ox, oy, x, y ) )
				continue;
			switch( hit.kind )
			{
				case HIT_TAB: s_tab = hit.value; break;
				case HIT_CHART: s_chart = hit.value; break;
				case HIT_SHARE: s_share = hit.value != 0; break;
				case HIT_EVERY: s_everyBuild = !s_everyBuild; break;
				case HIT_LEGEND: s_hidden[ hit.value ] = !s_hidden[ hit.value ]; break;
				case HIT_CHAT:
					// the chat in the Summary's foot, or back to what it covered
					if( s_tab != TAB_SUMMARY )
						s_foot = FOOT_CHAT;
					else
						s_foot = s_foot == FOOT_CHAT ? ( s_cardAvailable ? FOOT_CARD : FOOT_HIGHLIGHTS ) : FOOT_CHAT;
					s_tab = TAB_SUMMARY;
					break;
			}
			layoutPanel();
			return MSG_HANDLED;
		}
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Makes the panel, hidden until ScoreScreenGraphsShow, on the layout's backdrop. */
void ScoreScreenGraphsInit( GameWindow *backdrop )
{
	s_players.clear();
	s_order.clear();
	s_teamNumber.clear();
	s_teamColor.clear();
	s_banded = FALSE;
	s_freeForAll = FALSE;
	s_prepared = FALSE;
	s_tab = TAB_SUMMARY;
	s_everyBuild = FALSE;
	s_chart = CHART_ARMY_VALUE;
	s_share = FALSE;
	s_keys = 1;
	s_chatAvailable = s_cardAvailable = FALSE;
	s_foot = FOOT_HIGHLIGHTS;
	for( Int each = 0; each < MAX_PLAYER_COUNT; each++ )
		s_hidden[ each ] = FALSE;
	s_plot.live = FALSE;
	pickFonts();
	s_campaign = FALSE;		// until ScoreScreenGraphsSinglePlayer says the rows are a mission's
	s_apocalypse = TheApocalypseDirector != NULL && TheApocalypseDirector->isActive();
	// Art of Defense has no winner either (the owner, 2026-11-28): every verdict is the waves the seats held
	s_aodSurvived = TheAodDirector != NULL && TheAodDirector->isActive() ? TheAodDirector->getWavesSurvived() : -1;
	s_apocalypse = s_apocalypse || s_aodSurvived >= 0;
	s_subtitleKey = NULL;
	readMatchEnd();

	// the match's map names the page, as it named the load screen; a campaign mission's map has no
	// name of its own, so the mission's place or number does, as on its load screen
	const MapMetaData *map = TheMapCache->findMap( TheGlobalData->m_mapName );
	const Mission *mission = TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER ? TheCampaignManager->getCurrentMission() : NULL;
	if( mission && !mission->m_locationNameLabel.isEmpty() )
		s_mapName = TheGameText->fetch( mission->m_locationNameLabel );
	else if( mission )
		s_mapName.format( TheGameText->fetch( "GUI:LoadMission" ), TheCampaignManager->getCurrentMissionNumber() + 1 );
	else if( map )
		s_mapName = map->m_displayName;
	else
		s_mapName.translate( TheGameState->getMapLeafName( TheGlobalData->m_mapName ) );
	s_mapImage = getMapPreviewImage( TheGlobalData->m_mapName );

	// frame A's panel; the last of the backdrop's children, so the chat after a network match draws over
	// it and the hidden score table's windows give it the clicks
	Int backdropX, backdropY;
	backdrop->winGetScreenPosition( &backdropX, &backdropY );
	const IRegion2D panel = UIStyle::boardRect( EndScreen::PANEL_LEFT, EndScreen::PANEL_TOP, EndScreen::PANEL_RIGHT, EndScreen::PANEL_BOTTOM );
	s_panel = TheWindowManager->winCreate( backdrop, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, panel.lo.x - backdropX, panel.lo.y - backdropY,
		panel.hi.x - panel.lo.x, panel.hi.y - panel.lo.y, GameWinDefaultSystem );
	TheWindowManager->unlinkChildWindow( s_panel );
	TheWindowManager->addWindowToParentAtEnd( s_panel, backdrop );
	s_panel->winSetDrawFunc( drawPanel );
	s_panel->winSetInputFunc( panelInput );
}

/** The lobby seat a player sat in: the slot whose player name he carries, -1 outside a lobby. */
static Int slotOf( const Player *player )
{
	if( TheGameInfo == NULL )
		return -1;
	for( Int slot = 0; slot < MAX_SLOTS; slot++ )
	{
		AsciiString name;
		name.format( "player%d", slot );
		if( player->getPlayerNameKey() == TheNameKeyGenerator->nameToKey( name ) )
			return TheGameInfo->getConstSlot( slot ) ? slot : -1;
	}
	return -1;
}

void ScoreScreenGraphsAddPlayer( Player *player, const UnicodeString &name, Color color )
{
	ScoreKeeper *score = player->getScoreKeeper();
	const PlayerTemplate *side = player->getPlayerTemplate();
	const Player *local = ThePlayerList->getLocalPlayer();
	GraphPlayer entry;
	entry.index = player->getPlayerIndex();
	entry.player = player;
	entry.slot = slotOf( player );
	entry.name = name;
	entry.side = side ? side->getDisplayName() : UnicodeString::TheEmptyString;
	entry.icon = side ? side->getSideIconImage() : NULL;
	entry.portrait = side ? UIStyle::armyPortrait( side ) : NULL;
	entry.color = color;
	entry.nameColor = UIStyle::readable( color );
	// the single-player grab adds one player, the mission's own, whose win the campaign keeps
	entry.winner = s_campaign ? TheCampaignManager->isVictorious() : TheVictoryConditions->hasAchievedVictory( player );
	const GameSlot *slot = entry.slot >= 0 ? TheGameInfo->getConstSlot( entry.slot ) : NULL;
	entry.disconnected = slot && slot->disconnected();
	entry.local = player == local && !TheGameLogic->isInReplayGame() && !player->isPlayerObserver();
	const UnsignedInt fell = s_history.getDefeatFrame( entry.index );
	entry.fell = fell > 0 ? fell : SCORE_STILL_STANDING;
	entry.team = -1;
	entry.stats[ COL_SCORE ] = score->calculateScore();
	entry.stats[ COL_UNITS_BUILT ] = score->getTotalUnitsBuilt();
	entry.stats[ COL_UNITS_LOST ] = score->getTotalUnitsLost();
	entry.stats[ COL_UNITS_DESTROYED ] = score->getTotalUnitsDestroyed();
	entry.stats[ COL_BUILDINGS_BUILT ] = score->getTotalBuildingsBuilt();
	entry.stats[ COL_BUILDINGS_LOST ] = score->getTotalBuildingsLost();
	entry.stats[ COL_BUILDINGS_DESTROYED ] = score->getTotalBuildingsDestroyed();
	entry.stats[ COL_EARNED ] = score->getTotalMoneyEarned();
	entry.stats[ COL_SPENT ] = score->getTotalMoneySpent();
	s_players.push_back( entry );
	s_prepared = FALSE;
}

/** The rows that follow come from the single-player grab, a mission live or replayed: the player,
	* then his allies and enemies a side a row, laid out as a campaign page. */
void ScoreScreenGraphsSinglePlayer( void )
{
	s_campaign = TRUE;
	s_prepared = FALSE;
}

/** A campaign's allies or enemies of one side, summed into one row; it has numbers and no lines. */
void ScoreScreenGraphsAddSide( const UnicodeString &name, Color color, Int score, Int unitsBuilt, Int unitsLost, Int unitsDestroyed,
	Int buildingsBuilt, Int buildingsLost, Int buildingsDestroyed, Int earned, Int spent )
{
	GraphPlayer entry;
	entry.index = -1;
	entry.player = NULL;
	entry.slot = -1;
	entry.name = name;
	entry.icon = NULL;
	entry.portrait = NULL;
	entry.color = color;
	entry.nameColor = UIStyle::readable( color );
	entry.winner = FALSE;
	entry.disconnected = FALSE;
	entry.local = FALSE;
	entry.fell = SCORE_STILL_STANDING;
	entry.team = -1;
	entry.stats[ COL_SCORE ] = score;
	entry.stats[ COL_UNITS_BUILT ] = unitsBuilt;
	entry.stats[ COL_UNITS_LOST ] = unitsLost;
	entry.stats[ COL_UNITS_DESTROYED ] = unitsDestroyed;
	entry.stats[ COL_BUILDINGS_BUILT ] = buildingsBuilt;
	entry.stats[ COL_BUILDINGS_LOST ] = buildingsLost;
	entry.stats[ COL_BUILDINGS_DESTROYED ] = buildingsDestroyed;
	entry.stats[ COL_EARNED ] = earned;
	entry.stats[ COL_SPENT ] = spent;
	s_players.push_back( entry );
	s_prepared = FALSE;
}

/** Lays the panel out and shows it.  keys is how many keys ScoreScreen.cpp stood on the key row (the
	* chat key goes next to them); chat says a network match's chat is there for the foot, card that the
	* online rating card is, which the foot shows first.  After a network match the chat is open. */
void ScoreScreenGraphsShow( Int keys, Bool chat, Bool card )
{
	s_keys = max( keys, 1 );
	s_chatAvailable = chat;
	s_cardAvailable = card;
	s_foot = card ? FOOT_CARD : chat ? FOOT_CHAT : FOOT_HIGHLIGHTS;
	layoutPanel();
	s_panel->winHide( FALSE );
}

/// what the Summary's foot shows in its middle (FootShows), FOOT_NONE on the other pages
Int ScoreScreenGraphsFootShows( void )
{
	return s_tab == TAB_SUMMARY ? s_foot : FOOT_NONE;
}

/// the foot's middle on the board: where the chat, the rating card or the "game saved" line stand
void ScoreScreenGraphsFootArea( Real &x0, Real &y0, Real &x1, Real &y1 )
{
	x0 = s_footArea[ 0 ];
	y0 = s_footArea[ 1 ];
	x1 = s_footArea[ 2 ];
	y1 = s_footArea[ 3 ];
}

/// TRUE while the Summary is the open page
Bool ScoreScreenGraphsSummaryOpen( void )
{
	return s_tab == TAB_SUMMARY;
}

/// -online: the header's facts as key words them, its two %ls the format and the length ("Ranked 1v1 ·
/// 14:32"); before the panel is shown.  The key is a literal and outlives the screen.
void ScoreScreenGraphsSubtitle( const char *key )
{
	s_subtitleKey = key;
	s_prepared = FALSE;
}

void ScoreScreenGraphsShutdown( void )
{
	clearContent();
	TheWindowManager->winDestroy( s_panel );
	s_panel = NULL;
	s_players.clear();
	s_order.clear();
	s_prepared = FALSE;
	s_mapImage = NULL;
}
