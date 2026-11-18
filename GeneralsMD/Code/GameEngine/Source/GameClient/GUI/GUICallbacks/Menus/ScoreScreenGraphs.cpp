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
// The score screen's whole content after any match, a mission or a replay, drawn as the staging
// room's twin: its panel, its header, its seats column on the left and its map column on the right.
// Three tabs sit on the room's column-head row.
//
// Summary is the room with the results in it.  The seats column holds the standings grouped by team
// (mutual allies, as the observer rack works them out): a team of two or more under a band with its
// swatch, its verdict and its totals, its members as seat cards; a player alone is a card with its own
// verdict.  The winners first, then the teams by when their last member fell, then by score.  Under
// the standings sits the chat after a network match, or a strip of army value shares over time.  The
// map column holds the verdict in the Generals face, the map with every player's start spot and the
// match's facts in the room's rules box.
//
// Graphs is one chart of a series picked from a row of chips, drawn as one line a player or as each
// player's share of the whole, with the legend in the map column as the same grouped cards: hover one
// to pick its line out, click it to hide it; hover the plot for every player's value at that minute.
// Build order puts each player's cameos on a time axis whose first five minutes are stretched.
//
// Everything is drawn here from TheMatchHistory, the score keepers and the lobby's slots; the
// decisions that need no window are in ScoreScreenLayout.h, where a test walks every team split.
// The panel is made in code over the retail layout, whose own table ScoreScreen.cpp hides.
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

/// the standings' columns under four group heads
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

/// whether the most (TRUE) or the fewest (FALSE) is a column's best; a lone best is set in gold
static const Bool theColumnMostIsBest[ COL_COUNT ] = { TRUE, TRUE, FALSE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE };

/// each column's width on the 1080 board: counts stay under four figures, money and score do not
static const Real theColumnWidth[ COL_COUNT ] = { 92.0f, 82.0f, 82.0f, 82.0f, 82.0f, 82.0f, 82.0f, 100.0f, 100.0f };
static const Real GROUP_GAP = 16.0f;

static const char *const theEventLabels[ MATCH_EVENT_TYPE_COUNT ] =
{
	"GUI:GraphSuperweaponBuilt", "GUI:GraphSuperweaponFired", "GUI:GraphGeneralPower", "GUI:GraphPromotion", "GUI:SummaryDefeated"
};

/// which event's picture a group of markers shows, lowest first
static const Int theEventRank[ MATCH_EVENT_TYPE_COUNT ] = { 1, 0, 2, 3, 4 };

/// the first five minutes take this share of the build order's width once the match ran past them
static const Real EARLY_SHARE = 0.55f;
static const UnsignedInt EARLY_FRAMES = 5 * 60 * LOGICFRAMES_PER_SECOND;

/// the lobby's start spot against its map preview (SkirmishGameOptionsMenu.wnd), as the load screen keeps it
static const Int LOBBY_START_SPOT = 28;
static const Int LOBBY_MAP_PREVIEW = 410;

/// U+2026 and U+00B7 by number: a u"" literal is read in the machine's code page by MSVC
static const WideChar SCORE_ELLIPSIS[] = { 0x2026, 0 };
static const WideChar SCORE_DOT[] = { u' ', 0x00B7, u' ', 0 };

struct GraphPlayer
{
	Int index;					///< the player's index, or -1 for a campaign's side, which has numbers and no lines
	Player *player;			///< read while the screen is set up only
	Int slot;						///< the lobby seat, -1 outside a lobby match
	UnicodeString name;
	UnicodeString side;
	const Image *icon;
	Color color;
	Color nameColor;		///< the colour lifted where it is too dark to read as words
	Bool winner;
	Bool disconnected;
	UnsignedInt fell;		///< the frame he was eliminated on, SCORE_STILL_STANDING if never
	Int team;						///< his team in s_order
	Int stats[ COL_COUNT ];
};

struct GraphText
{
	DisplayString *text;
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

/** A box: a card, a stripe, a chip, an icon, a group of event markers on a chart or a build in the
	* build order; the last two carry a tooltip. */
struct GraphMark
{
	IRegion2D box;
	const Image *image;
	Color edge;
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

struct GraphSwatch
{
	IRegion2D box;
	Color color;
};

/// a start spot on the map, drawn as the room draws it
struct GraphSpot
{
	ICoord2D pos;
	Color color;							///< 0 for a spot nobody took
	DisplayString *number;
	Bool dimmed;							///< he was defeated
	Bool gold;								///< he won
};

enum GraphHitKind
{
	HIT_TAB,
	HIT_CHART,
	HIT_SHARE,
	HIT_EVERY,
	HIT_LEGEND
};

/// something the mouse can click: a tab, a chip, a toggle drawn as a tab, or a legend card
struct GraphHit
{
	IRegion2D box;
	GraphHitKind kind;
	Int value;
	Bool open;
	DisplayString *label;		///< NULL for a legend card, which is drawn with the cards
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

/** The match's history as the screen opened.  TheMatchHistory itself is reset and filled again by
	* whatever map runs next (the shell's) while this screen can still be up, so a tab picked late read
	* that map's first seconds as one flat sample. */
static MatchHistory s_history;
static GameWindow *s_panel = NULL;
static GameFont *s_font = NULL;				///< names
static GameFont *s_body = NULL;				///< numbers, tabs, facts
static GameFont *s_small = NULL;			///< sub-heads, chips, axis labels
static GameFont *s_smallBold = NULL;	///< team heads, section heads, counts on markers
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
static Bool s_prepared = FALSE;
static Bool s_anyWinner = FALSE;
static Bool s_campaign = FALSE;
static std::vector< GraphText > s_texts;
static std::vector< GraphMark > s_marks;
static std::vector< GraphLine > s_lines;
static std::vector< GraphBand > s_bands;
static std::vector< GraphBand > s_dividers;		///< the ink lines between teams, over the bands
static std::vector< GraphRule > s_rules;
static std::vector< GraphSwatch > s_swatches;
static std::vector< GraphSpot > s_spots;
static std::vector< GraphHit > s_hits;
static GraphPlot s_plot;
static DisplayString *s_title = NULL;							///< the map's name
static DisplayString *s_subtitle = NULL;					///< the format and the length, "4v4" a middle dot "23:41"
static const char *s_subtitleKey = NULL;					///< -online: the words round the format and the length (ScoreScreenGraphsSubtitle)
static DisplayString *s_wordmark[ 2 ] = { NULL, NULL };
static DisplayString *s_verdict = NULL;						///< VICTORY, DEFEAT, TEAM 2 WINS, NO WINNER
static Color s_verdictColor = 0;
static const Image *s_mapImage = NULL;
static Bool s_mapShown = FALSE;
static IRegion2D s_mapBox;
static IRegion2D s_mapDrawn;
static Int s_ruleY = 0;					///< the header's rule, from the panel's top
static Int s_bottom = 0;				///< where the map column stops, from the panel's top
static Int s_leftBottom = 0;		///< where the seats column stops: over the chat after a network match
static std::vector< DisplayString * > s_tipText;
static std::vector< DisplayString * > s_tipValue;	///< NULL where a line has no number
static Int s_tipKey = -1;

//-------------------------------------------------------------------------------------------------
/** A 1920x1080 board length or point as the panel's own pixels. */
static Int lx( Real board ) { return UIStyle::px( board - UIStyle::ROOM_PANEL_LEFT ); }
static Int ly( Real board ) { return UIStyle::px( board - UIStyle::ROOM_PANEL_TOP ); }

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

/** align: 0 puts x at the text's left, 1 at its middle, 2 at its right.  Returns the text's width. */
static Int addText( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Int align = 0 )
{
	GraphText entry;
	entry.text = newText( text, font );
	const Int width = textWidth( entry.text );
	entry.x = x - width * align / 2;
	entry.y = y;
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

static GraphMark newMark( Int x, Int y, Int width, Int height, Color fill, Color edge )
{
	GraphMark mark;
	mark.box.lo.x = x;
	mark.box.lo.y = y;
	mark.box.hi.x = x + width;
	mark.box.hi.y = y + height;
	mark.image = NULL;
	mark.edge = edge;
	mark.fill = fill;
	mark.rank = MATCH_EVENT_TYPE_COUNT;
	mark.label = NULL;
	mark.count = NULL;
	mark.dropBottom = 0;
	return mark;
}

static void addTip( GraphMark &mark, const UnicodeString &text, Color color )
{
	GraphTipLine line;
	line.text = text;
	line.color = color;
	mark.tip.push_back( line );
}

static void addSwatch( Int x, Int y, Int size, Color color )
{
	GraphSwatch swatch;
	swatch.box.lo.x = x;
	swatch.box.lo.y = y;
	swatch.box.hi.x = x + size;
	swatch.box.hi.y = y + size;
	swatch.color = color;
	s_swatches.push_back( swatch );
}

/** A tab-looking box the mouse can click, its label centred; returns its right edge. */
static Int addHit( GraphHitKind kind, Int value, Bool open, const UnicodeString &label, GameFont *font, Int x, Int y, Int height, Int pad )
{
	GraphHit hit;
	hit.kind = kind;
	hit.value = value;
	hit.open = open;
	hit.label = newText( label, font );
	hit.box.lo.x = x;
	hit.box.lo.y = y;
	hit.box.hi.x = x + textWidth( hit.label ) + pad * 3;
	hit.box.hi.y = y + height;
	s_hits.push_back( hit );
	return hit.box.hi.x;
}

static Int hitWidth( const UnicodeString &label, GameFont *font, Int pad )
{
	return measure( label, font ) + pad * 3;
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
	* screen read the logic's frame as 0: the chart kept one sample while the summary's strip, laid out
	* at once, had them all. */
static UnsignedInt s_endFrame = 1;

static UnsignedInt matchEndFrame( void )
{
	return s_endFrame;
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
		TheDisplayStringManager->freeDisplayString( s_texts[ each ].text );
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
	s_swatches.clear();
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
	// a campaign keeps its rows as ScoreScreen gathers them: the player, then his allies, then the enemy
	if( s_campaign )
	{
		s_order.clear();
		for( Int a = 0; a < count; a++ )
		{
			ScoreTeam alone;
			alone.team = team[ a ];
			alone.members.push_back( a );
			s_order.push_back( alone );
		}
	}
	else
		s_order = ScoreScreen_order( seats );
	s_banded = !s_campaign && ScoreScreen_banded( s_order );

	// a team is called by its lobby number and wears its lobby colour where its members share one;
	// any other (a player alone, allies made in the match) takes the lowest number no lobby team
	// has, on the room's no-team slate
	// numbered in seat order, as the load board numbers them, though the rows run in standings order
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
			// a player with no seat (a campaign's) after every seated one, in the standings' own order
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

	// the header's subtitle: the format and the length
	UnicodeString subtitle;
	if( !s_campaign && !s_order.empty() )
	{
		Bool freeForAll = FALSE;
		const std::string format = ScoreScreen_format( s_order, &freeForAll );
		if( freeForAll )
		{
			UnicodeString players;
			players.format( u" (%d)", count );
			subtitle = fetchText( "GUI:LoadFreeForAll" );
			subtitle.concat( players );
		}
		else
			subtitle.translate( AsciiString( format.c_str() ) );
		subtitle.concat( UIStyle::HEADER_JOINER );
	}
	subtitle.concat( clockText( matchEndFrame() ) );
	if( s_subtitleKey && !s_campaign && !s_order.empty() )
	{
		Bool freeForAll = FALSE;
		UnicodeString format;
		format.translate( AsciiString( ScoreScreen_format( s_order, &freeForAll ).c_str() ) );
		subtitle.format( fetchText( s_subtitleKey ), format.str(), clockText( matchEndFrame() ).str() );
	}
	freeText( s_subtitle );
	s_subtitle = newText( subtitle, UIStyle::subtitleFont() );

	// the verdict over the map: the local player's own, or who won for anyone watching
	const Player *local = ThePlayerList->getLocalPlayer();
	const Bool watching = TheGameLogic->isInReplayGame() || local == NULL || local->isPlayerObserver();
	UnicodeString verdict;
	s_verdictColor = UIStyle::MUTED;
	// a mission, live or replayed, is the player's own win or loss; a match played here and not won,
	// left before anyone won included, is a defeat, as the stats upload counts it
	if( s_campaign )
	{
		const Bool won = TheCampaignManager->isVictorious();
		verdict = UIStyle::capitals( fetchText( won ? "GUI:Victory" : "GUI:Defeat" ) );
		s_verdictColor = won ? UIStyle::GOLD : UIStyle::MUTED;
	}
	else if( !watching )
	{
		const Bool won = TheVictoryConditions->isLocalAlliedVictory();
		verdict = UIStyle::capitals( fetchText( won ? "GUI:Victory" : "GUI:Defeat" ) );
		s_verdictColor = won ? UIStyle::GOLD : UIStyle::MUTED;
	}
	else if( s_anyWinner && !s_order.empty() )
	{
		const ScoreTeam &first = s_order[ 0 ];
		if( first.members.size() >= 2 )
			verdict.format( fetchText( "GUI:ScoreTeamWins" ), s_teamNumber[ 0 ] );
		else
			verdict.format( fetchText( "GUI:ScorePlayerWins" ), s_players[ first.members[ 0 ] ].name.str() );
		verdict = UIStyle::capitals( verdict );
		s_verdictColor = UIStyle::GOLD;
	}
	if( verdict.isEmpty() )
		verdict = UIStyle::capitals( fetchText( "GUI:ScoreNoWinner" ) );
	freeText( s_verdict );
	s_verdict = newText( verdict, UIStyle::titleFont( UIStyle::TEXT_DISPLAY ) );
	// a size down where the column is too narrow for it, then abbreviated, never shrunk further
	const Int verdictRoom = lx( SCORE_RIGHT ) - lx( SCORE_RIGHT_LEFT );
	if( textWidth( s_verdict ) > verdictRoom )
	{
		GameFont *smaller = UIStyle::titleFont( UIStyle::TEXT_TITLE );
		s_verdict->setFont( smaller );
		if( textWidth( s_verdict ) > verdictRoom )
			s_verdict->setText( UIStyle::fitText( verdict, smaller, verdictRoom ) );
	}

	// the players are read no more
	for( Int a = 0; a < count; a++ )
		s_players[ a ].player = NULL;
}

/** The verdict a card or a team band carries, or FALSE for none: a match left early has none. */
static Bool chipFor( Bool winner, Bool disconnected, UnsignedInt fell, UnicodeString &label, Color &color, Color &edge )
{
	if( !s_anyWinner && !s_campaign )
		return FALSE;
	edge = UIStyle::FRAME;
	color = UIStyle::MUTED;
	if( winner )
	{
		label = fetchText( "GUI:SummaryWinner" );
		color = edge = UIStyle::GOLD;
		return TRUE;
	}
	if( disconnected )
	{
		label = fetchText( "GUI:SummaryDisconnected" );
		return TRUE;
	}
	label = fetchText( "GUI:SummaryDefeated" );
	if( fell != SCORE_STILL_STANDING )
	{
		label.concat( u' ' );
		label.concat( clockText( fell ) );
	}
	return TRUE;
}

/** A chip right-aligned at right and centred on middle: a one-pixel box round a small label.
	* Returns its left edge. */
static Int addChip( const UnicodeString &label, Color color, Color edge, Int right, Int middle )
{
	const Int pad = UIStyle::px( 6.0f );
	const Int height = s_small->height;
	const Int width = addText( fittedLabel( label, s_small, UIStyle::px( 170.0f ) ), s_small, right - pad, middle - height / 2, color, 2 );
	s_marks.push_back( newMark( right - width - pad * 2, middle - height / 2 - 2, width + pad * 2, height + 4, 0, edge ) );
	return right - width - pad * 2;
}

/** How tight the standings pack into available pixels. */
static ScoreRowFit summaryFit( Int available )
{
	const Int heads = s_banded ? (Int)s_order.size() : 0;
	return ScoreScreen_fitRows( (Int)s_players.size(), heads, available, UIStyle::px( SCORE_CARD_PITCH ), UIStyle::px( SCORE_HEAD_PITCH ),
		max( UIStyle::px( SCORE_MIN_CARD_PITCH ), s_font->height + 4 ), max( UIStyle::px( SCORE_MIN_HEAD_PITCH ), s_small->height + 4 ) );
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

/** Gives a marker the picture of event, which outranks whatever it showed before. */
static void dressMark( GraphMark &mark, const MatchEvent &event, const GraphPlayer &player, const CommandButton *button )
{
	mark.rank = theEventRank[ event.type ];
	mark.edge = player.nameColor;
	mark.fill = UIStyle::CARD;
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
			mark.label = newText( rank, s_smallBold );
			break;
		}
	}
}

/** The chart in x0,y0 to x1,y1: lanes of event markers over a plot of one line a player, or of every
	* player's share of the whole as bands ordered by team, up to the frame the match was decided on.
	* compact leaves out the value labels, for the strip under the standings. */
/// a chart's pieces over and under its plot: a marker lane's height with its gap, and the time axis
static Int markerLaneHeight( void )
{
	return s_small->height + 6 + max( UIStyle::px( 6.0f ), 4 );
}

static Int chartFootHeight( void )
{
	return s_small->height + max( s_body->height / 2, 4 ) / 2;
}

static void layoutChart( Int chart, Bool share, Int x0, Int y0, Int x1, Int y1, Bool compact, Int lanes = 2 )
{
	const MatchHistory &history = s_history;
	const Int count = shownSamples();
	const Int pad = max( s_body->height / 2, 4 );
	const Int smallLine = s_small->height;
	const Int icon = smallLine + 6;
	const Int iconWidth = icon * 4 / 3;
	const Int laneGap = max( UIStyle::px( 6.0f ), 4 );
	const Int plotTop = y0 + lanes * markerLaneHeight() + pad / 2;
	const Int plotBottom = y1 - chartFootHeight();

	if( count == 0 )
	{
		addText( fetchText( "GUI:GraphNoData" ), s_body, x0, plotTop, UIStyle::MUTED );
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

	// the value labels decide where the plot starts: the wider of both ways of drawing this series, so
	// switching between lines and share keeps the plot's left edge
	const Int lineStep = niceStep( max( most, 4 ) / 4 );
	const Int lineCeiling = lineStep * ( ( max( most, 1 ) + lineStep - 1 ) / lineStep );
	const Int step = share ? 25 : lineStep;
	const Int ceiling = share ? 100 : lineCeiling;
	const Int labelWidth = max( measure( UnicodeString( u"100%" ), s_small ), measure( valueText( lineCeiling ), s_small ) );
	const Int plotLeft = compact ? x0 : x0 + labelWidth + pad;
	const Int plotRight = x1 - iconWidth / 2;
	const Int plotWidth = max( plotRight - plotLeft, 1 );
	const Int plotHeight = max( plotBottom - plotTop, 1 );
	const UnsignedInt end = historyEnd();

	for( Int value = 0; value <= ceiling; value += step )
	{
		const Int y = plotBottom - (Int)( (Real)value / ceiling * plotHeight );
		addRule( plotLeft, y, plotRight, y, value == 0 ? UIStyle::FRAME : UIStyle::GRID );
		if( compact )
			continue;
		UnicodeString label = valueText( value );
		if( share )
			label.concat( u'%' );
		addText( label, s_small, plotLeft - pad / 2, y - smallLine / 2, UIStyle::MUTED, 2 );
	}

	const Int minutes = minuteStep( end, compact ? 6 : 10 );
	for( UnsignedInt frame = 0; frame <= end; frame += minutes * 60 * LOGICFRAMES_PER_SECOND )
	{
		const Int x = plotLeft + (Int)( (Real)frame / end * plotWidth );
		addText( clockText( frame ), s_small, x, plotBottom + pad / 4, UIStyle::MUTED, frame == 0 ? 0 : 1 );
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
			graph.color = player.nameColor;		// readable on the panel, as his name is
			graph.cross = player.fell != SCORE_STILL_STANDING;
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
		const Int column = max( UIStyle::px( 3.0f ), 2 );
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
			// see-through bands in the readable colours, a one-pixel dark gap between two, and a
			// three-pixel ink line where one team gives way to the next, drawn over every band
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
					band.color = UIStyle::withAlpha( players[ each ]->nameColor, 170 );
					band.box.lo.x = x;
					band.box.lo.y = top;
					band.box.hi.x = x + width;
					band.box.hi.y = bottom < plotBottom && bottom - top > 1 ? bottom - 1 : bottom;
					s_bands.push_back( band );
				}
				size_t next = each + 1;
				while( next < players.size() && at[ next ] <= 0.0f )
					next++;
				if( next < players.size() && players[ next ]->team != players[ each ]->team && bottom > plotTop && bottom < plotBottom )
				{
					GraphBand divider;
					divider.owner = -1;
					divider.color = UIStyle::INK;
					divider.box.lo.x = x;
					divider.box.lo.y = bottom - 2;
					divider.box.hi.x = x + width;
					divider.box.hi.y = bottom + 1;
					s_dividers.push_back( divider );
				}
			}
		}
	}

	// the event markers in two lanes, the same rule on the Graphs page and in the summary's strip: an
	// event that lands on one of its own seat's markers joins it, which then shows the weightiest of
	// its events and counts them; any other goes into the first lane it clears, or else into the lane
	// that ends soonest, pushed right until it clears.  Two seats never share a marker, and no two
	// markers in a lane overlap; the drop line keeps the true moment.
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
		// a box stays over the plot, its own edges inside the plot's
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
			GraphMark mark = newMark( left, y0 + lane * ( icon + laneGap ), iconWidth, icon, UIStyle::CARD, player->nameColor );
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
		addTip( mark, tip, player->nameColor );
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
			s_marks[ each ].count = newText( number, s_smallBold );
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** "Highlights" in the map column between top and bottom: up to four lines, "label: name", the most
	* enemy value destroyed, the top earner, the biggest army at its peak and the first superweapon
	* built, each left out where nobody has one, and the last ones dropped when the room runs out. */
static void layoutHighlights( Int r0, Int r1, Int top, Int bottom )
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
		Int peak = 0;
		for( Int sample = 0; sample <= last; sample++ )
			peak = max( peak, s_history.getValue( sample, player.index, MATCH_SERIES_ARMY_VALUE ) );
		const Int values[ 3 ] = { s_history.getValue( last, player.index, MATCH_SERIES_VALUE_DESTROYED ), player.stats[ COL_EARNED ], peak };
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
			best[ 3 ] = playerFor( events[ each ].player );
	}
	static const char *const labels[ 4 ] =
	{
		"GUI:ScoreHighlightDestroyed", "GUI:ScoreHighlightEarned", "GUI:ScoreHighlightArmy", "GUI:ScoreHighlightSuperweapon"
	};

	const Int pitch = s_body->height + UIStyle::px( 6.0f );
	Int y = top + s_smallBold->height + UIStyle::px( 6.0f );
	if( y + pitch > bottom )
		return;
	addText( fittedLabel( fetchText( "GUI:ScoreHighlights" ), s_smallBold, r1 - r0 ), s_smallBold, r0, top, UIStyle::ACTIVE );
	for( Int kind = 0; kind < 4 && y + pitch <= bottom; kind++ )
	{
		if( best[ kind ] == NULL )
			continue;
		// the label gets up to half the column, the name the rest
		UnicodeString label = UIStyle::fitKey( labels[ kind ], s_body, ( r1 - r0 ) / 2 );
		label.concat( u':' );
		const Int labelWidth = addText( label, s_body, r0, y, UIStyle::MUTED );
		const Int gap = UIStyle::px( 8.0f );
		// where names repeat (eight "Hard AI" seats) the side tells them apart: "Hard AI · China"
		UnicodeString who = best[ kind ]->name;
		for( size_t other = 0; other < s_players.size(); other++ )
		{
			if( &s_players[ other ] != best[ kind ] && s_players[ other ].name == best[ kind ]->name && !best[ kind ]->side.isEmpty() )
			{
				who.concat( SCORE_DOT );
				who.concat( best[ kind ]->side );
				break;
			}
		}
		addText( fittedText( who, s_body, r1 - r0 - labelWidth - gap ), s_body, r0 + labelWidth + gap, y, best[ kind ]->nameColor );
		y += pitch;
	}
}

/** The Summary's standings in the seats column and the verdict, map and facts in the map column. */
static void layoutSummary( Int pad )
{
	const Int x0 = lx( SCORE_LEFT );
	const Int x1 = lx( SCORE_LEFT_RIGHT );
	const Int stripe = UIStyle::stripeWidth();

	// the columns from the right edge in, a gap between groups
	Int colRight[ COL_COUNT ];
	Int colLeft[ COL_COUNT ];
	Int x = x1 - pad;
	for( Int col = COL_COUNT - 1; col >= 0; col-- )
	{
		if( col < COL_COUNT - 1 && theColumnGroup[ col ] != theColumnGroup[ col + 1 ] )
			x -= UIStyle::px( GROUP_GAP );
		colRight[ col ] = x;
		x -= UIStyle::px( theColumnWidth[ col ] );
		colLeft[ col ] = x;
	}
	const Int namesRight = colLeft[ 0 ] - pad;

	// the heads: a group's word over its columns, the column's under it, a rule under both
	const Int headTop = ly( SCORE_CONTENT_TOP );
	const Int subTop = headTop + s_body->height;
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
		const UnicodeString label = UIStyle::fitKey( theGroupLabels[ group ], s_body, colRight[ last ] - colLeft[ first ] - UIStyle::px( 8.0f ) );
		if( first == last )
			addText( label, s_body, colRight[ first ], headTop, UIStyle::INK, 2 );
		else
		{
			addText( label, s_body, ( colLeft[ first ] + colRight[ last ] ) / 2, headTop, UIStyle::INK, 1 );
			addRule( colLeft[ first ] + pad, subTop - 1, colRight[ last ], subTop - 1, UIStyle::GRID );
		}
	}
	// a sub-head keeps a clear gap to the column on its left in every language, cut short if it must
	for( Int col = 0; col < COL_COUNT; col++ )
	{
		if( theColumnLabels[ col ] )
			addText( UIStyle::fitKey( theColumnLabels[ col ], s_small, colRight[ col ] - colLeft[ col ] - UIStyle::px( 8.0f ) ),
				s_small, colRight[ col ], subTop + 2, UIStyle::MUTED, 2 );
	}
	const Int cardsTop = ly( SCORE_CARDS_TOP );
	addRule( x0, cardsTop - pad / 2, x1, cardsTop - pad / 2, UIStyle::FRAME );

	// the row a column's best stands in, or -1 where two rows share it; with two players a best only
	// repeats who won, and money spent is no achievement, so neither is marked
	Int bestRow[ COL_COUNT ];
	for( Int col = 0; col < COL_COUNT; col++ )
	{
		bestRow[ col ] = -1;
		if( s_players.size() < 3 || col == COL_SPENT )
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

	const ScoreRowFit fit = summaryFit( s_leftBottom - cardsTop );
	const Int cardHeight = fit.cardPitch - max( UIStyle::px( 6.0f ), 2 );
	// the name over the side where the card has room for both: at the name's own size, or a size
	// down where only that fits, else the name alone
	GameFont *nameFont = s_font;
	Bool twoLines = cardHeight >= s_font->height + s_small->height;
	if( !twoLines && cardHeight >= s_body->height + s_small->height )
	{
		nameFont = s_body;
		twoLines = TRUE;
	}
	Int y = cardsTop;
	for( size_t t = 0; t < s_order.size(); t++ )
	{
		const ScoreTeam &team = s_order[ t ];
		if( s_banded )
		{
			// the band: the lobby's team swatch and the team's name; a team of two or more adds its
			// verdict and its totals, a player alone keeps his on his card
			const Int middle = y + fit.headPitch / 2;
			const Int swatch = s_small->height + 2;
			addSwatch( x0, middle - swatch / 2, swatch, s_teamColor[ t ] );
			UnicodeString name;
			name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
			// half the names column, the other half kept for the team's chip
			const Int nameLeft = x0 + swatch + pad;
			addText( fittedLabel( name, s_smallBold, ( namesRight - nameLeft ) / 2 ), s_smallBold, nameLeft, middle - s_smallBold->height / 2, UIStyle::ACTIVE );
			y += fit.headPitch;
		}
		if( team.members.size() >= 2 )
		{
			const Int middle = y - fit.headPitch / 2;
			Bool winner = FALSE, disconnected = TRUE;
			UnsignedInt fell = 0;
			Int total[ COL_COUNT ] = { 0 };
			for( size_t m = 0; m < team.members.size(); m++ )
			{
				const GraphPlayer &player = s_players[ team.members[ m ] ];
				winner = winner || player.winner;
				disconnected = disconnected && player.disconnected;
				fell = max( fell, player.fell );
				for( Int col = 0; col < COL_COUNT; col++ )
					total[ col ] += player.stats[ col ];
			}
			UnicodeString chip;
			Color chipColor, chipEdge;
			if( chipFor( winner, disconnected, fell, chip, chipColor, chipEdge ) )
				addChip( chip, chipColor, chipEdge, namesRight, middle );
			for( Int col = 0; col < COL_COUNT; col++ )
				addText( UIStyle::grouped( total[ col ] ), s_small, colRight[ col ], middle - s_small->height / 2, UIStyle::MUTED, 2 );
		}

		for( size_t m = 0; m < team.members.size(); m++ )
		{
			const Int row = team.members[ m ];
			const GraphPlayer &player = s_players[ row ];
			const Int middle = y + cardHeight / 2;
			// the lobby's seat card: its panel and frame, the winner's edge gold as the host's is there
			s_marks.push_back( newMark( x0, y, x1 - x0, cardHeight, UIStyle::PANEL, player.winner ? UIStyle::GOLD : UIStyle::FRAME ) );
			s_marks.push_back( newMark( x0 + 1, y + 1, stripe, cardHeight - 2, player.color, player.color ) );
			Int textLeft = x0 + stripe + pad;
			if( player.icon )
			{
				const Int iconSize = cardHeight - UIStyle::px( 10.0f );
				GraphMark icon = newMark( textLeft, middle - iconSize / 2, iconSize, iconSize, 0, 0 );
				icon.image = player.icon;
				s_marks.push_back( icon );
				textLeft += iconSize + pad;
			}
			Int nameRight = namesRight;
			UnicodeString chip;
			Color chipColor, chipEdge;
			if( team.members.size() == 1 && player.index >= 0 && chipFor( player.winner, player.disconnected, player.fell, chip, chipColor, chipEdge ) )
				nameRight = addChip( chip, chipColor, chipEdge, namesRight, middle ) - pad;
			const Int room = nameRight - textLeft;
			if( twoLines && !player.side.isEmpty() )
			{
				const Int top = middle - ( nameFont->height + s_small->height ) / 2;
				addText( fittedText( player.name, nameFont, room ), nameFont, textLeft, top, player.nameColor );
				addText( fittedLabel( player.side, s_small, room ), s_small, textLeft, top + nameFont->height, UIStyle::MUTED );
			}
			else
			{
				// one line: the name, then the side after it, muted and smaller, abbreviated by rule where
				// short of room and left out where not even that fits; the name is never shortened
				const Int nameWidth = addText( fittedText( player.name, s_font, room ), s_font, textLeft, middle - s_font->height / 2, player.nameColor );
				const Int sideRoom = room - nameWidth - pad;
				if( !player.side.isEmpty() && sideRoom >= s_small->height * 2 )
					addText( fittedLabel( player.side, s_small, sideRoom ), s_small, textLeft + nameWidth + pad,
						middle - s_font->height / 2 + s_font->height - s_small->height - 1, UIStyle::MUTED );
			}
			for( Int col = 0; col < COL_COUNT; col++ )
				addText( UIStyle::grouped( player.stats[ col ] ), s_body, colRight[ col ], middle - s_body->height / 2,
					bestRow[ col ] == row ? UIStyle::GOLD : UIStyle::INK, 2 );
			y += fit.cardPitch;
		}
	}

	// the match strip, where the chat is not: who held what share of the armies' worth, and when
	// Under its label, no taller than SCORE_STRIP_MAX however much room a short list leaves.  The
	// bands keep SCORE_STRIP_BANDS at least: short of room the strip gives up its marker lanes first
	// (Graphs keeps them), then the whole strip, and never squashes the bands.
	const Int stripTop = y + UIStyle::px( SCORE_CHAT_GAP );
	const Int chartTop = stripTop + s_smallBold->height + pad / 2;
	const Int chartBottom = min( s_bottom, chartTop + UIStyle::px( SCORE_STRIP_MAX ) );
	const Int bandRoom = chartBottom - chartTop - pad / 2 - chartFootHeight();
	Int stripLanes = 2;
	while( stripLanes >= 0 && bandRoom - stripLanes * markerLaneHeight() < UIStyle::px( SCORE_STRIP_BANDS ) )
		stripLanes--;
	if( s_leftBottom == s_bottom && stripLanes >= 0 && s_history.getSampleCount() > 0 && !s_campaign )
	{
		addText( fittedLabel( fetchText( "GUI:HudStatArmy" ), s_smallBold, x1 - x0 ), s_smallBold, x0, stripTop, UIStyle::ACTIVE );
		layoutChart( CHART_ARMY_VALUE, TRUE, x0, chartTop, x1, chartBottom, TRUE, stripLanes );
	}

	// the map column: the verdict, the map, the facts in the room's rules box
	const Int r0 = lx( SCORE_RIGHT_LEFT );
	const Int r1 = lx( SCORE_RIGHT );
	Int verdictWidth, verdictHeight;
	s_verdict->getSize( &verdictWidth, &verdictHeight );
	const Int verdictTop = ly( SCORE_CONTENT_TOP );

	std::vector< UnicodeString > factLabels, factValues;
	factLabels.push_back( fetchText( "GUI:ScoreLength" ) );
	factValues.push_back( clockText( matchEndFrame() ) );
	if( !s_campaign )
	{
		Int speed = 0;
		for( Int each = 0; each < PLAYER_GAME_SPEED_COUNT; each++ )
		{
			if( PLAYER_GAME_SPEED_PERCENTS[ each ] == GAME_SPEED_PERCENT )
				speed = each;
		}
		AsciiString speedLabel;
		speedLabel.format( "GUI:GameSpeedMissions%d", speed );
		factLabels.push_back( fetchText( "GUI:GameSpeed" ) );
		factValues.push_back( fetchText( speedLabel.str() ) );
		if( TheGameInfo )
		{
			const Money &cash = TheGameInfo->getStartingCash();
			factLabels.push_back( fetchText( "GUI:StartingMoney" ) );
			if( cash.isUnlimited() )
				factValues.push_back( fetchText( "GUI:StartingMoneyUnlimited" ) );
			else
			{
				UnicodeString money( u"$" );
				money.concat( UIStyle::grouped( (Int)cash.countMoney() ) );
				factValues.push_back( money );
			}
		}
	}
	// as the lobby's map column: the verdict, the map under it, the facts box standing on the foot over
	// the button row as the rules box stands over Play Game, the highlights between map and facts
	const Int factPitch = UIStyle::px( 38.0f );
	const Int factPad = UIStyle::px( 12.0f );
	const Int factsHeight = (Int)factLabels.size() * factPitch + factPad * 2 - 4;
	const Int mapTop = verdictTop + verdictHeight + factPad;
	const Int mapSize = min( r1 - r0 - 6, s_bottom - factsHeight - factPad - 6 - mapTop );
	const MapMetaData *map = TheMapCache->findMap( TheGlobalData->m_mapName );
	const Int factsTop = s_bottom - factsHeight;
	if( mapSize >= UIStyle::px( 120.0f ) )
	{
		// the frame hugs the map's own shape inside that square, so a wide or a tall map wears no
		// black bars; a page with no map keeps the square
		s_mapShown = TRUE;
		Int mapWidth = mapSize, mapHeight = mapSize;
		if( map && s_mapImage && map->m_extent.width() > 0.0f && map->m_extent.height() > 0.0f )
		{
			const Real aspect = map->m_extent.width() / map->m_extent.height();
			if( aspect >= 1.0f )
				mapHeight = max( REAL_TO_INT( mapSize / aspect ), 1 );
			else
				mapWidth = max( REAL_TO_INT( mapSize * aspect ), 1 );
		}
		s_mapBox.lo.x = r0 + ( r1 - r0 - mapWidth ) / 2;
		s_mapBox.lo.y = mapTop + 3;
		s_mapBox.hi.x = s_mapBox.lo.x + mapWidth;
		s_mapBox.hi.y = s_mapBox.lo.y + mapHeight;
		s_mapDrawn = s_mapBox;
	}
	// between the map and the facts, what stood out of the match, as many lines as the room holds
	layoutHighlights( r0 + factPad, r1 - factPad, s_mapShown ? s_mapBox.hi.y + 3 + factPad : mapTop, factsTop - factPad );
	s_marks.push_back( newMark( r0, factsTop, r1 - r0, factsHeight, UIStyle::PANEL, UIStyle::FRAME ) );
	for( size_t each = 0; each < factLabels.size(); each++ )
	{
		const Int rowMiddle = factsTop + factPad + (Int)each * factPitch + factPitch / 2 - 2;
		const Int valueWidth = addText( factValues[ each ], s_body, r1 - factPad, rowMiddle - s_body->height / 2, UIStyle::INK, 2 );
		addText( fittedLabel( factLabels[ each ], s_body, r1 - r0 - factPad * 3 - valueWidth ), s_body, r0 + factPad, rowMiddle - s_body->height / 2, UIStyle::MUTED );
	}
	if( !s_mapShown || map == NULL || s_mapImage == NULL )
		return;
	if( !map->m_isMultiplayer || TheGameInfo == NULL )
		return;
	for( Int start = 0; start < map->m_numPlayers; start++ )
	{
		AsciiString waypoint;
		waypoint.format( "Player_%d_Start", start + 1 );
		WaypointMap::const_iterator it = map->m_waypoints.find( waypoint );
		if( it == map->m_waypoints.end() )
			continue;
		const Real across = ( it->second.x - map->m_extent.lo.x ) / map->m_extent.width();
		const Real down = 1.0f - ( it->second.y - map->m_extent.lo.y ) / map->m_extent.height();
		GraphSpot spot;
		spot.pos.x = s_mapDrawn.lo.x + REAL_TO_INT( across * ( s_mapDrawn.hi.x - s_mapDrawn.lo.x ) );
		spot.pos.y = s_mapDrawn.lo.y + REAL_TO_INT( down * ( s_mapDrawn.hi.y - s_mapDrawn.lo.y ) );
		spot.color = 0;
		spot.number = NULL;
		spot.dimmed = FALSE;
		spot.gold = FALSE;
		for( size_t each = 0; each < s_players.size(); each++ )
		{
			const GraphPlayer &player = s_players[ each ];
			const GameSlot *slot = player.slot >= 0 ? TheGameInfo->getConstSlot( player.slot ) : NULL;
			if( slot == NULL || slot->getStartPos() != start )
				continue;
			AsciiString seat;
			seat.format( "NUMBER:%d", player.slot + 1 );
			spot.color = player.color;
			spot.number = newText( TheGameText->fetch( seat ), s_smallBold );
			spot.dimmed = s_anyWinner && !player.winner;
			spot.gold = player.winner;
		}
		// a spot nobody took is not drawn: an empty ring reads as one more player
		if( spot.number != NULL )
			s_spots.push_back( spot );
	}
}

//-------------------------------------------------------------------------------------------------
/** The legend in the map column: team bands and a card a player, his value at the end and his
	* peak; hovering one picks his line out, clicking one hides it. */
static void layoutLegend( Int chart, Int pad )
{
	const Int r0 = lx( SCORE_RIGHT_LEFT );
	const Int r1 = lx( SCORE_RIGHT );
	const Int top = ly( SCORE_CONTENT_TOP );
	const MatchHistory &history = s_history;
	if( history.getSampleCount() == 0 )
		return;
	std::vector< const GraphPlayer * > players = chartedPlayers();
	const Int heads = s_banded ? (Int)s_order.size() : 0;
	const ScoreRowFit fit = ScoreScreen_fitRows( (Int)players.size(), heads, s_bottom - top, UIStyle::px( SCORE_CARD_PITCH ), UIStyle::px( SCORE_HEAD_PITCH ),
		max( UIStyle::px( SCORE_MIN_CARD_PITCH ), s_font->height + 4 ), max( UIStyle::px( SCORE_MIN_HEAD_PITCH ), s_small->height + 4 ) );
	const Int cardHeight = fit.cardPitch - max( UIStyle::px( 6.0f ), 2 );
	const Bool twoLines = cardHeight >= s_body->height + s_small->height + 2;
	const Int stripe = UIStyle::stripeWidth();
	Int y = top;
	Int lastTeam = -1;
	for( size_t each = 0; each < players.size(); each++ )
	{
		const GraphPlayer &player = *players[ each ];
		size_t t = 0;
		while( t < s_order.size() && s_order[ t ].team != player.team )
			t++;
		if( player.team != lastTeam && t < s_order.size() && s_banded )
		{
			const Int middle = y + fit.headPitch / 2;
			const Int swatch = s_small->height + 2;
			addSwatch( r0, middle - swatch / 2, swatch, s_teamColor[ t ] );
			UnicodeString name;
			name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
			addText( fittedLabel( name, s_smallBold, r1 - r0 - swatch - pad * 2 ), s_smallBold, r0 + swatch + pad, middle - s_smallBold->height / 2, UIStyle::ACTIVE );
			y += fit.headPitch;
		}
		lastTeam = player.team;

		const std::vector< Int > values = chartValues( chart, player );
		const Int last = lastSampleOf( player );
		Int peak = 0;
		for( Int sample = 0; sample <= last; sample++ )
			peak = max( peak, values[ sample ] );
		const Bool hidden = s_hidden[ player.index ];
		const Int middle = y + cardHeight / 2;
		s_marks.push_back( newMark( r0, y, r1 - r0, cardHeight, UIStyle::PANEL, UIStyle::FRAME ) );
		s_marks.push_back( newMark( r0 + 1, y + 1, stripe, cardHeight - 2, hidden ? UIStyle::withAlpha( player.color, 70 ) : player.color, 0 ) );
		const Int valueWidth = addText( UIStyle::grouped( values[ last ] ), s_body, r1 - pad, twoLines ? middle - ( s_body->height + s_small->height ) / 2 : middle - s_body->height / 2,
			hidden ? UIStyle::MUTED : UIStyle::INK, 2 );
		UnicodeString peakText;
		peakText.format( fetchText( "GUI:GraphPeak" ), UIStyle::grouped( peak ).str() );
		Int peakWidth = 0;
		if( twoLines )
			peakWidth = addText( fittedLabel( peakText, s_small, ( r1 - r0 ) / 2 ), s_small, r1 - pad, middle - ( s_body->height + s_small->height ) / 2 + s_body->height, UIStyle::MUTED, 2 );
		const Int textLeft = r0 + stripe + pad;
		const Int room = r1 - pad * 2 - max( valueWidth, peakWidth ) - textLeft;
		addText( fittedText( player.name, s_body, room ), s_body, textLeft, middle - s_body->height / 2, hidden ? UIStyle::MUTED : player.nameColor );
		GraphHit hit;
		hit.kind = HIT_LEGEND;
		hit.value = player.index;
		hit.open = !hidden;
		hit.label = NULL;
		hit.box.lo.x = r0;
		hit.box.lo.y = y;
		hit.box.hi.x = r1;
		hit.box.hi.y = y + cardHeight;
		s_hits.push_back( hit );
		y += fit.cardPitch;
	}
}

//-------------------------------------------------------------------------------------------------
/** Frame to x on the build order's timeline: linear when the match was short, otherwise the first
	* five minutes get EARLY_SHARE of the width and the rest of the match the remainder. */
static Int buildX( UnsignedInt frame, UnsignedInt end, Int left, Int width )
{
	if( end <= EARLY_FRAMES + EARLY_FRAMES / 5 )
		return left + (Int)( (Real)frame / end * width );
	const Int early = (Int)( width * EARLY_SHARE );
	if( frame <= EARLY_FRAMES )
		return left + (Int)( (Real)frame / EARLY_FRAMES * early );
	return left + early + (Int)( (Real)( frame - EARLY_FRAMES ) / ( end - EARLY_FRAMES ) * ( width - early ) );
}

static void layoutBuildOrder( Int bottom, Int pad )
{
	const MatchHistory &history = s_history;
	const Int smallLine = s_small->height;
	const std::vector< const GraphPlayer * > players = chartedPlayers();
	if( players.empty() )
		return;
	const Int x0 = lx( SCORE_LEFT );
	const Int x1 = lx( SCORE_RIGHT );
	const Int top = ly( SCORE_CONTENT_TOP );
	const Int rowsTop = top + smallLine + pad / 2;

	const Int heads = s_banded ? (Int)s_order.size() : 0;
	// a lone campaign player gets the height of two rows, not the whole area
	const Int roomy = ( bottom - rowsTop ) / max( (Int)players.size(), 2 );
	const ScoreRowFit fit = ScoreScreen_fitRows( (Int)players.size(), heads, bottom - rowsTop, min( roomy, UIStyle::px( 96.0f ) ),
		UIStyle::px( 28.0f ), smallLine + 6, smallLine + 4 );
	const Int rowHeight = fit.cardPitch;
	const Bool twoLines = rowHeight - 6 >= 2 * smallLine;

	UnsignedInt end = historyEnd();
	Int nameWidth = UIStyle::px( 160.0f );
	for( size_t each = 0; each < players.size(); each++ )
	{
		const std::vector< MatchBuild > &builds = history.getBuilds( players[ each ]->index );
		if( !builds.empty() )
			end = max( end, builds.back().frame );
		nameWidth = max( nameWidth, measure( players[ each ]->name, s_small ) );
		if( twoLines )
			nameWidth = max( nameWidth, measure( players[ each ]->side, s_small ) );
	}
	nameWidth = min( nameWidth, UIStyle::px( 260.0f ) );
	end = max( end, (UnsignedInt)LOGICFRAMES_PER_SECOND );
	const Int stripe = UIStyle::stripeWidth();
	const Int cardRight = x0 + stripe + pad * 2 + nameWidth;

	// the cameos at their own 4:3, two lanes of the first of each thing, three of every build, and
	// fewer lanes where that would shrink a cameo under two thirds of a line of text
	Int lanes = s_everyBuild ? 3 : 2;
	while( lanes > 1 && ( rowHeight - pad ) / lanes - 3 < smallLine * 2 / 3 )
		lanes--;
	const Int icon = min( max( ( rowHeight - pad ) / lanes - 3, 10 ), s_body->height * 3 / 2 );
	const Int iconWidth = icon * 4 / 3;
	const Int left = cardRight + pad + iconWidth / 2;
	const Int right = x1 - iconWidth / 2;
	const Int span = max( right - left, 1 );
	const Int rowsBottom = rowsTop + fit.used;

	// the time axis: minutes through the first five, then a coarser step for the rest, a gridline
	// each across every row
	const Bool split = end > EARLY_FRAMES + EARLY_FRAMES / 5;
	const UnsignedInt minute = 60 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt lateStep = split ? minuteStep( end - EARLY_FRAMES, 6 ) * minute : minuteStep( end, 10 ) * minute;
	for( UnsignedInt frame = 0; frame <= end; frame += ( split && frame < EARLY_FRAMES ) ? minute : lateStep )
	{
		const Int x = buildX( frame, end, left, span );
		addRule( x, rowsTop, x, rowsBottom, frame == EARLY_FRAMES && split ? UIStyle::FRAME : UIStyle::GRID );
		addText( clockText( frame ), s_small, x, top, UIStyle::MUTED, 1 );
		if( split && frame < EARLY_FRAMES && frame + minute > EARLY_FRAMES )
			frame = EARLY_FRAMES - minute;
	}

	Int y = rowsTop;
	Int lastTeam = -1;
	for( size_t each = 0; each < players.size(); each++ )
	{
		const GraphPlayer &player = *players[ each ];
		size_t t = 0;
		while( t < s_order.size() && s_order[ t ].team != player.team )
			t++;
		if( player.team != lastTeam && t < s_order.size() && s_banded )
		{
			const Int middle = y + fit.headPitch / 2;
			const Int swatch = smallLine + 2;
			addSwatch( x0, middle - swatch / 2, swatch, s_teamColor[ t ] );
			UnicodeString name;
			name.format( fetchText( "GUI:LoadTeam" ), s_teamNumber[ t ] );
			addText( fittedLabel( name, s_smallBold, cardRight - x0 - swatch - pad * 2 ), s_smallBold, x0 + swatch + pad, middle - s_smallBold->height / 2, UIStyle::ACTIVE );
			y += fit.headPitch;
		}
		lastTeam = player.team;

		const Int cardHeight = rowHeight - 4;
		const Int middle = y + cardHeight / 2;
		s_marks.push_back( newMark( x0, y, cardRight - x0, cardHeight, UIStyle::PANEL, UIStyle::FRAME ) );
		s_marks.push_back( newMark( x0 + 1, y + 1, stripe, cardHeight - 2, player.color, 0 ) );
		const Int textLeft = x0 + stripe + pad;
		if( twoLines && !player.side.isEmpty() )
		{
			addText( fittedText( player.name, s_small, nameWidth ), s_small, textLeft, middle - smallLine, player.nameColor );
			addText( fittedLabel( player.side, s_small, nameWidth ), s_small, textLeft, middle, UIStyle::MUTED );
		}
		else
			addText( fittedText( player.name, s_small, nameWidth ), s_small, textLeft, middle - smallLine / 2, player.nameColor );
		addRule( cardRight, y + cardHeight, right + iconWidth / 2, y + cardHeight, UIStyle::GRID );

		std::vector< Int > laneRight( lanes, -iconWidth * 4 );
		std::vector< const ThingTemplate * > seen;
		const Int lanesTop = y + ( cardHeight - lanes * ( icon + 3 ) ) / 2;
		const std::vector< MatchBuild > &builds = history.getBuilds( player.index );
		for( size_t built = 0; built < builds.size(); built++ )
		{
			const MatchBuild &build = builds[ built ];
			// a picture says what was built; something with none says nothing, and gets left out
			const Image *image = buildImage( build.thing );
			if( image == NULL )
				continue;
			if( !s_everyBuild )
			{
				if( std::find( seen.begin(), seen.end(), build.thing ) != seen.end() )
					continue;
				seen.push_back( build.thing );
			}
			// each into the first lane it clears, or else nudged right to the first free place in the
			// lane that ends soonest; the tooltip keeps the real time
			Int x = buildX( build.frame, end, left, span ) - iconWidth / 2;
			Int lane = 0;
			Bool clear = FALSE;
			for( Int l = 0; l < lanes && !clear; l++ )
			{
				if( x >= laneRight[ l ] )
				{
					lane = l;
					clear = TRUE;
				}
				else if( laneRight[ l ] < laneRight[ lane ] )
					lane = l;
			}
			if( !clear )
				x = laneRight[ lane ];
			// ponytail: past the timeline's end a crowd stacks on the last place; the hover still names each
			x = min( x, right - iconWidth / 2 );
			laneRight[ lane ] = x + iconWidth + 3;
			GraphMark mark = newMark( x, lanesTop + lane * ( icon + 3 ), iconWidth, icon, UIStyle::CARD,
				build.thing->isKindOf( KINDOF_STRUCTURE ) ? UIStyle::MUTED : player.nameColor );
			mark.image = image;
			UnicodeString tip;
			tip.format( u"%s  %s", clockText( build.frame ).str(), build.thing->getDisplayName().str() );
			addTip( mark, tip, UIStyle::INK );
			s_marks.push_back( mark );
		}
		y += rowHeight;
	}
}

//-------------------------------------------------------------------------------------------------
static void layoutPanel( void )
{
	clearContent();
	const Int pad = max( s_body->height / 2, 4 );

	// the header, as a staging room's: the map's name, the wordmark across from it, a rule under both
	s_ruleY = ly( UIStyle::ROOM_HEADER_LINE );

	// the tabs on the room's column-head row
	const Int tabsTop = ly( SCORE_TABS_TOP );
	const Int tabsHeight = ly( SCORE_TABS_BOTTOM ) - tabsTop;
	Int x = lx( SCORE_LEFT );
	for( Int tab = 0; tab < TAB_COUNT; tab++ )
		x = addHit( HIT_TAB, tab, tab == s_tab, UIStyle::capitals( fetchText( theTabLabels[ tab ] ) ), s_body, x, tabsTop, tabsHeight, pad ) + pad / 2;

	if( s_tab == TAB_SUMMARY )
		layoutSummary( pad );
	else if( s_tab == TAB_BUILD_ORDER )
	{
		const UnicodeString every = fetchText( s_everyBuild ? "GUI:BuildOrderFirsts" : "GUI:BuildOrderEvery" );
		addHit( HIT_EVERY, 0, FALSE, every, s_body, lx( SCORE_RIGHT ) - hitWidth( every, s_body, pad ), tabsTop, tabsHeight, pad );
		layoutBuildOrder( s_leftBottom, pad );
	}
	else
	{
		// lines or share, at the row's right end
		const UnicodeString lines = UIStyle::capitals( fetchText( "GUI:GraphLines" ) );
		const UnicodeString share = UIStyle::capitals( fetchText( "GUI:GraphShare" ) );
		const Int shareLeft = lx( SCORE_RIGHT ) - hitWidth( share, s_body, pad );
		const Int linesLeft = shareLeft - pad / 2 - hitWidth( lines, s_body, pad );
		addHit( HIT_SHARE, 0, !s_share, lines, s_body, linesLeft, tabsTop, tabsHeight, pad );
		addHit( HIT_SHARE, 1, s_share, share, s_body, shareLeft, tabsTop, tabsHeight, pad );

		// the series as chips along the seats column, onto a second row where they run out
		const Int x0 = lx( SCORE_LEFT );
		const Int x1 = lx( SCORE_LEFT_RIGHT );
		const Int chipHeight = s_small->height + pad;
		Int chipX = x0;
		Int chipY = ly( SCORE_CONTENT_TOP );
		for( Int chart = 0; chart < CHART_COUNT; chart++ )
		{
			const UnicodeString label = UIStyle::fitKey( theChartLabels[ chart ], s_small, x1 - x0 - pad * 2 );
			const Int width = hitWidth( label, s_small, pad / 2 );
			if( chipX > x0 && chipX + width > x1 )
			{
				chipX = x0;
				chipY += chipHeight + pad / 2;
			}
			chipX = addHit( HIT_CHART, chart, chart == s_chart, label, s_small, chipX, chipY, chipHeight, pad / 2 ) + pad / 2;
		}
		layoutChart( s_chart, s_share, x0, chipY + chipHeight + pad, x1, s_leftBottom, FALSE );
		layoutLegend( s_chart, pad );
	}
}

//-------------------------------------------------------------------------------------------------
static void drawBox( const IRegion2D &box, Int ox, Int oy, Color fill, Color edge )
{
	const Int w = box.hi.x - box.lo.x;
	const Int h = box.hi.y - box.lo.y;
	if( fill )
		TheDisplay->drawFillRect( ox + box.lo.x, oy + box.lo.y, w, h, fill );
	if( edge )
		TheDisplay->drawOpenRect( ox + box.lo.x, oy + box.lo.y, w, h, 1.0f, edge );
}

static Bool isInside( const IRegion2D &box, Int ox, Int oy, Int x, Int y )
{
	return x >= ox + box.lo.x && x < ox + box.hi.x && y >= oy + box.lo.y && y < oy + box.hi.y;
}

/** A tooltip of lines under (or, near the panel's foot, over) the anchor; key names what it shows,
	* so its strings are made once while it stays. */
static void drawTip( const std::vector< GraphTipLine > &lines, Int key, Int x, Int top, Int bottom, Int ox, Int oy, Int width, Int height )
{
	if( key != s_tipKey )
	{
		clearTip();
		for( size_t line = 0; line < lines.size(); line++ )
		{
			s_tipText.push_back( newText( lines[ line ].text, s_small ) );
			s_tipValue.push_back( lines[ line ].value.isEmpty() ? NULL : newText( lines[ line ].value, s_small ) );
		}
		s_tipKey = key;
	}
	// the words on the left, the numbers right-aligned on one edge, the same padding all round
	const Int pad = max( UIStyle::px( 8.0f ), 4 );
	Int wordsWidth = 0, valueWidth = 0;
	const Int lineHeight = s_small->height + 2;
	for( size_t line = 0; line < s_tipText.size(); line++ )
	{
		wordsWidth = max( wordsWidth, textWidth( s_tipText[ line ] ) );
		if( s_tipValue[ line ] )
			valueWidth = max( valueWidth, textWidth( s_tipValue[ line ] ) );
	}
	const Int w = wordsWidth + ( valueWidth > 0 ? pad * 2 + valueWidth : 0 ) + pad * 2;
	const Int h = (Int)s_tipText.size() * lineHeight + pad * 2;
	const Int tx = max( min( x, ox + width - w - 4 ), ox + 4 );
	const Int ty = bottom + 4 + h < oy + height ? bottom + 4 : max( top - h - 4, oy + 4 );
	UIStyle::panel( tx, ty, w, h, UIStyle::CARD );
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
	line.color = UIStyle::MUTED;
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
			UnicodeString text;
			text.format( u"%d%%", total > 0 ? max( value, 0 ) * 100 / total : 0 );
			return text;
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
		line.color = player.nameColor;
		lines.push_back( line );
	}
	return lines;
}

static void drawPanel( GameWindow *window, WinInstanceData *instData )
{
	Int ox, oy, width, height;
	window->winGetScreenPosition( &ox, &oy );
	window->winGetSize( &width, &height );
	// on the layout's ruler frame, the staging room's panel, its title's place and its header line
	UIStyle::panel( ox, oy, width, height, UIStyle::PANEL );

	Int markWidth, markHeight;
	s_wordmark[ 0 ]->getSize( &markWidth, &markHeight );
	const Int titleMiddle = oy + ly( UIStyle::ROOM_TITLE_MIDDLE );
	UIStyle::header( s_title, s_subtitle, ox + lx( UIStyle::ROOM_TITLE_LEFT ), titleMiddle );
	UIStyle::wordmark( s_wordmark, ox + width - lx( UIStyle::ROOM_INSET ), titleMiddle - markHeight / 2 );
	UIStyle::rule( ox, oy + s_ruleY, width );

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	Int focus = -1;				// the player whose legend card the mouse is on
	Int focusCard = -1;		// and that card
	for( size_t each = 0; each < s_hits.size(); each++ )
	{
		const GraphHit &hit = s_hits[ each ];
		const Bool hovered = isInside( hit.box, ox, oy, mouse.x, mouse.y );
		if( hit.label == NULL )
		{
			if( hovered )
			{
				focusCard = (Int)each;
				if( hit.open )
					focus = hit.value;
			}
			continue;
		}
		const Color label = UIStyle::tab( ox + hit.box.lo.x, oy + hit.box.lo.y, hit.box.hi.x - hit.box.lo.x, hit.box.hi.y - hit.box.lo.y,
			TRUE, hit.open, hovered && !( hit.open && hit.kind != HIT_EVERY ) );
		Int w, h;
		hit.label->getSize( &w, &h );
		hit.label->draw( ox + ( hit.box.lo.x + hit.box.hi.x - w ) / 2, oy + ( hit.box.lo.y + hit.box.hi.y - h ) / 2, label, UIStyle::SHADOW );
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
		const Color color = focus >= 0 && band.owner != focus ? UIStyle::withAlpha( band.color, 50 ) : band.color;
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
		drawBox( mark.box, ox, oy, mark.fill, mark.edge );
		const Int inset = 2;
		if( mark.image )
			TheDisplay->drawImage( mark.image, ox + mark.box.lo.x + inset, oy + mark.box.lo.y + inset, ox + mark.box.hi.x - inset, oy + mark.box.hi.y - inset );
		if( mark.label )
		{
			Int w, h;
			mark.label->getSize( &w, &h );
			mark.label->draw( ox + ( mark.box.lo.x + mark.box.hi.x - w ) / 2, oy + ( mark.box.lo.y + mark.box.hi.y - h ) / 2, mark.edge, UIStyle::SHADOW );
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
	// the legend card under the mouse lights its edge, as the lobby's buttons do
	if( focusCard >= 0 )
	{
		const IRegion2D &box = s_hits[ focusCard ].box;
		UIStyle::edge( ox + box.lo.x, oy + box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y );
	}
	for( size_t each = 0; each < s_swatches.size(); each++ )
	{
		const IRegion2D &box = s_swatches[ each ].box;
		LobbySwatchFill( ox + box.lo.x, oy + box.lo.y, box.hi.x - box.lo.x, box.hi.y - box.lo.y, s_swatches[ each ].color );
	}

	if( s_mapShown )
	{
		const Int mapWidth = s_mapBox.hi.x - s_mapBox.lo.x;
		const Int mapHeight = s_mapBox.hi.y - s_mapBox.lo.y;
		TheDisplay->drawFillRect( ox + s_mapBox.lo.x, oy + s_mapBox.lo.y, mapWidth, mapHeight, UIStyle::GROUND );
		if( s_mapImage )
			TheDisplay->drawImage( s_mapImage, ox + s_mapDrawn.lo.x, oy + s_mapDrawn.lo.y, ox + s_mapDrawn.hi.x, oy + s_mapDrawn.hi.y );
		// the lobby's map preview wears a button's edge
		UIStyle::edge( ox + s_mapBox.lo.x - 3, oy + s_mapBox.lo.y - 3, mapWidth + 6, mapHeight + 6 );
		const Int spot = max( mapWidth, mapHeight ) * LOBBY_START_SPOT / LOBBY_MAP_PREVIEW;
		const Image *ring = TheMappedImageCollection->findImageByName( "PlayerStart" );
		const Image *goldRing = TheMappedImageCollection->findImageByName( "ReforgedLobbySpotRing" );
		for( size_t each = 0; each < s_spots.size(); each++ )
		{
			const GraphSpot &mark = s_spots[ each ];
			const Int x = ox + mark.pos.x - spot / 2;
			const Int y = oy + mark.pos.y - spot / 2;
			// a winner's spot inside a ring of gold a little wider than its own
			if( mark.gold && goldRing )
			{
				const Int grow = max( UIStyle::px( 4.0f ), 2 );
				TheDisplay->drawImage( goldRing, x - grow, y - grow, x + spot + grow, y + spot + grow, UIStyle::GOLD );
			}
			if( ring )
				TheDisplay->drawImage( ring, x, y, x + spot, y + spot );
			if( mark.number == NULL )
				continue;
			const Color fill = mark.dimmed ? UIStyle::withAlpha( mark.color, 90 ) : mark.color;
			LobbyStartSpotFill( x, y, spot, spot, fill );
			Int w, h;
			mark.number->getSize( &w, &h );
			mark.number->draw( x + ( spot - w ) / 2, y + ( spot - h ) / 2, mark.dimmed ? UIStyle::MUTED : LobbyTextColorOn( mark.color ), 0 );
		}
	}

	for( size_t each = 0; each < s_lines.size(); each++ )
	{
		const GraphLine &graph = s_lines[ each ];
		const Bool dim = focus >= 0 && graph.owner != focus;
		const Color color = dim ? UIStyle::withAlpha( graph.color, 70 ) : graph.color;
		const Real lineWidth = !dim && focus >= 0 ? 3.0f : 2.0f;
		const std::vector< ICoord2D > &points = graph.points;
		for( size_t point = 1; point < points.size(); point++ )
			TheDisplay->drawLine( ox + points[ point - 1 ].x, oy + points[ point - 1 ].y, ox + points[ point ].x, oy + points[ point ].y, lineWidth, color );
		if( graph.cross && !points.empty() )
		{
			const Int arm = max( UIStyle::px( 5.0f ), 3 );
			const Int cx = ox + points.back().x;
			const Int cy = oy + points.back().y;
			TheDisplay->drawLine( cx - arm, cy - arm, cx + arm, cy + arm, 2.0f, color );
			TheDisplay->drawLine( cx - arm, cy + arm, cx + arm, cy - arm, 2.0f, color );
		}
	}

	for( size_t each = 0; each < s_texts.size(); each++ )
		s_texts[ each ].text->draw( ox + s_texts[ each ].x, oy + s_texts[ each ].y, s_texts[ each ].color, UIStyle::SHADOW );

	if( s_tab == TAB_SUMMARY && s_verdict )
	{
		Int w, h;
		s_verdict->getSize( &w, &h );
		const Int r0 = lx( SCORE_RIGHT_LEFT );
		const Int r1 = lx( SCORE_RIGHT );
		s_verdict->draw( ox + ( r0 + r1 - w ) / 2, oy + ly( SCORE_CONTENT_TOP ), s_verdictColor, UIStyle::SHADOW );
	}

	if( hovered >= 0 )
	{
		const GraphMark &mark = s_marks[ hovered ];
		drawBox( mark.box, ox, oy, 0, UIStyle::EDGE_LIT );
		drawTip( mark.tip, hovered, ox + mark.box.lo.x, oy + mark.box.lo.y, oy + mark.box.hi.y, ox, oy, width, height );
	}
	else if( s_plot.live && !s_plot.sampleX.empty() && isInside( s_plot.box, ox, oy, mouse.x, mouse.y ) )
	{
		// the crosshair on the nearest sample, and every player's value there
		Int sample = 0;
		for( size_t each = 1; each < s_plot.sampleX.size(); each++ )
		{
			if( abs( ox + s_plot.sampleX[ each ] - mouse.x ) < abs( ox + s_plot.sampleX[ sample ] - mouse.x ) )
				sample = (Int)each;
		}
		const Int x = ox + s_plot.sampleX[ sample ];
		TheDisplay->drawLine( x, oy + s_plot.box.lo.y, x, oy + s_plot.box.hi.y, 1.0f, UIStyle::EDGE_LIT );
		drawTip( plotTip( sample ), 1000000 + sample, x + UIStyle::px( 12.0f ), mouse.y, mouse.y + UIStyle::px( 12.0f ), ox, oy, width, height );
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
	s_prepared = FALSE;
	s_tab = TAB_SUMMARY;
	s_everyBuild = FALSE;
	s_chart = CHART_ARMY_VALUE;
	s_share = FALSE;
	for( Int each = 0; each < MAX_PLAYER_COUNT; each++ )
		s_hidden[ each ] = FALSE;
	s_plot.live = FALSE;
	s_font = UIStyle::font( UIStyle::TEXT_STRONG );
	s_body = UIStyle::font( UIStyle::TEXT_BODY );
	s_small = UIStyle::font( UIStyle::TEXT_SMALL );
	s_smallBold = UIStyle::font( UIStyle::TEXT_SMALL, TRUE );
	s_campaign = FALSE;		// until ScoreScreenGraphsSinglePlayer says the rows are a mission's
	s_subtitleKey = NULL;
	readMatchEnd();

	// the match's map names the page, as it named the load screen; a campaign mission's map has no
	// name of its own, so the mission's place or number does, as on its load screen
	const MapMetaData *map = TheMapCache->findMap( TheGlobalData->m_mapName );
	const Mission *mission = TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER ? TheCampaignManager->getCurrentMission() : NULL;
	UnicodeString mapName;
	if( mission && !mission->m_locationNameLabel.isEmpty() )
		mapName = TheGameText->fetch( mission->m_locationNameLabel );
	else if( mission )
		mapName.format( TheGameText->fetch( "GUI:LoadMission" ), TheCampaignManager->getCurrentMissionNumber() + 1 );
	else if( map )
		mapName = map->m_displayName;
	else
		mapName.translate( TheGameState->getMapLeafName( TheGlobalData->m_mapName ) );
	s_title = newText( UIStyle::nameCapitals( mapName ), UIStyle::titleFont( UIStyle::TEXT_TITLE ) );
	GameFont *wordmarkFont = UIStyle::font( UIStyle::TEXT_STRONG, TRUE );
	s_wordmark[ 0 ] = newText( UnicodeString( u"Zero Hour" ), wordmarkFont );
	s_wordmark[ 1 ] = newText( UnicodeString( u" Reforged" ), wordmarkFont );
	s_mapImage = getMapPreviewImage( TheGlobalData->m_mapName );

	// the skirmish room's panel, its button row inside; the last of the backdrop's children, so the
	// chat after a network match draws over it and the hidden score table's windows give it the clicks
	Int backdropX, backdropY;
	backdrop->winGetScreenPosition( &backdropX, &backdropY );
	const Int panelX = UIStyle::boardX( UIStyle::ROOM_PANEL_LEFT );
	const Int panelY = UIStyle::boardY( UIStyle::ROOM_PANEL_TOP );
	s_panel = TheWindowManager->winCreate( backdrop, WIN_STATUS_ENABLED | WIN_STATUS_HIDDEN, panelX - backdropX, panelY - backdropY,
		UIStyle::px( UIStyle::ROOM_PANEL_RIGHT - UIStyle::ROOM_PANEL_LEFT ), UIStyle::px( UIStyle::ROOM_PANEL_BOTTOM - UIStyle::ROOM_PANEL_TOP ),
		GameWinDefaultSystem );
	TheWindowManager->unlinkChildWindow( s_panel );
	TheWindowManager->addWindowToParentAtEnd( s_panel, backdrop );
	s_bottom = s_leftBottom = ly( SCORE_CONTENT_BOTTOM );
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
	GraphPlayer entry;
	entry.index = player->getPlayerIndex();
	entry.player = player;
	entry.slot = slotOf( player );
	entry.name = name;
	entry.side = side ? side->getDisplayName() : UnicodeString::TheEmptyString;
	entry.icon = side ? side->getSideIconImage() : NULL;
	entry.color = color;
	entry.nameColor = UIStyle::readable( color );
	// the single-player grab adds one player, the mission's own, whose win the campaign keeps
	entry.winner = s_campaign ? TheCampaignManager->isVictorious() : TheVictoryConditions->hasAchievedVictory( player );
	const GameSlot *slot = entry.slot >= 0 ? TheGameInfo->getConstSlot( entry.slot ) : NULL;
	entry.disconnected = slot && slot->disconnected();
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
	entry.color = color;
	entry.nameColor = UIStyle::readable( color );
	entry.winner = FALSE;
	entry.disconnected = FALSE;
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

/** Where the network chat's list starts on the 1080 board when its foot is at listBottom: the
	* room's own place, or lower where the standings need more room (ScoreScreen_chatListTop). */
Real ScoreScreenGraphsChatTop( Real listBottom )
{
	prepareStandings();
	const ScoreRowFit fit = summaryFit( ly( SCORE_CHAT_TOP - SCORE_CHAT_GAP ) - ly( SCORE_CARDS_TOP ) );
	return ScoreScreen_chatListTop( fit, UIStyle::px( 1000.0f ) / 1000.0f, listBottom );
}

/** Lays the panel out and shows it.  floor, when given, is a window the panel has to stop above:
	* with chat the network chat's list, which only the seats column stops over; without, the "game
	* saved" line after a mission, which the whole page stops over. */
void ScoreScreenGraphsShow( GameWindow *floor, Bool chat )
{
	prepareStandings();
	s_bottom = s_leftBottom = ly( SCORE_CONTENT_BOTTOM );
	if( floor )
	{
		Int panelX, panelY, floorX, floorY;
		s_panel->winGetScreenPosition( &panelX, &panelY );
		floor->winGetScreenPosition( &floorX, &floorY );
		const Int above = floorY - panelY - UIStyle::px( SCORE_CHAT_GAP );
		if( chat )
			s_leftBottom = min( s_leftBottom, above );
		else
			s_bottom = s_leftBottom = min( s_bottom, above );
	}
	layoutPanel();
	s_panel->winHide( FALSE );
}

/// TRUE while the Summary is the open page: the rating card after an online match stands on it alone
Bool ScoreScreenGraphsSummaryOpen( void )
{
	return s_tab == TAB_SUMMARY;
}

/// -online: the header's subtitle as key words it, its two %ls the format and the length ("Ranked 1v1 ·
/// 14:32"); before the panel is shown.  The key is a literal and outlives the screen.
void ScoreScreenGraphsSubtitle( const char *key )
{
	s_subtitleKey = key;
	s_prepared = FALSE;
}

void ScoreScreenGraphsShutdown( void )
{
	clearContent();
	freeText( s_title );
	freeText( s_subtitle );
	freeText( s_verdict );
	freeText( s_wordmark[ 0 ] );
	freeText( s_wordmark[ 1 ] );
	TheWindowManager->winDestroy( s_panel );
	s_panel = NULL;
	s_players.clear();
	s_order.clear();
	s_prepared = FALSE;
	s_mapImage = NULL;
}
