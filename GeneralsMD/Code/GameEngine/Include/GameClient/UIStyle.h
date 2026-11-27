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

// FILE: UIStyle.h ////////////////////////////////////////////////////////////////////////////////
//
// The front end's one look, for the screens drawn in code (the load screen's board, the score
// screen's pages) so they match the ones built from .wnd layouts (main menu, options, the staging
// rooms, the in-game menu).  The colours were read off those layouts at 1920x1080; the type is the
// layouts' own, Arial and the "Generals" face at a layout's point size, sized the way the layouts'
// text is.  The rules for using them are in UI-STYLE.md in the wrapper's docs/reference.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _UISTYLE_H_
#define _UISTYLE_H_

#include "Lib/BaseType.h"
#include "GameClient/Color.h"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"

class GameFont;
class DisplayString;
class GameWindow;
class Image;
class PlayerTemplate;
struct OnlinePlayerInfo;

namespace UIStyle
{
	/// type sizes in a layout's points (authored at 800x600), the sizes the .wnd files use
	enum TextSize
	{
		TEXT_SMALL = 8,			///< chips, axis labels, a card's third line
		TEXT_BODY = 10,			///< lists, subtitles, a card's second line, a tab
		TEXT_STRONG = 12,		///< player names, table numbers, the wordmark
		TEXT_HEADING = 14,	///< column and group headings, a room's labels
		TEXT_TITLE = 18,		///< the screen's title, in the "Generals" face
		TEXT_DISPLAY = 30		///< one big word: VS
	};

	extern const Color GROUND;					///< behind a screen with no picture behind it
	extern const Color PANEL;						///< a panel over a picture: the layouts' see-through black
	extern const Color CARD;						///< a card or a row on a panel
	extern const Color FRAME;						///< a panel's and a card's edge, the header rule
	extern const Color GRID;						///< faint rules: a chart's grid, between rows, a bar's track
	extern const Color EDGE;						///< a button's edge, a progress bar, a selection
	extern const Color EDGE_LIT;				///< the bright middle line of a button's edge, a hover
	extern const Color TAB_EDGE;				///< a tab's box
	extern const Color INK;							///< words and numbers
	extern const Color MUTED;						///< what is secondary
	extern const Color ACTIVE;					///< the layouts' green: section headings, the open tab, chip labels
	extern const Color GOLD;						///< results only: the winner, a column's best
	extern const Color WORDMARK_BLUE;		///< "Reforged" in the wordmark, nowhere else
	extern const Color SHADOW;
	extern const Color SHELL_DIM;				///< the staging rooms' dimming of what stands behind their panel

	/// the staging rooms' geometry on the 1920x1080 board (Tools/lobbyroom_layout.py): the panel on
	/// the score screen's ruler frame's bright line, the title, the header line, the seat inset and
	/// the button row; the load and score screens stand on it too
	const Real ROOM_PANEL_LEFT = 96.0f;
	const Real ROOM_PANEL_TOP = 72.0f;
	const Real ROOM_PANEL_RIGHT = 1824.0f;
	const Real ROOM_PANEL_BOTTOM = 1008.0f;
	const Real ROOM_TITLE_LEFT = 120.0f;
	const Real ROOM_TITLE_MIDDLE = 112.0f;
	const Real ROOM_HEADER_LINE = 147.0f;
	const Real ROOM_INSET = 124.0f;
	const Real ROOM_BUTTON_TOP = 932.0f;
	const Real ROOM_BUTTON_BOTTOM = 988.0f;
	const Real ROOM_BUTTON_GAP = 16.0f;
	const Real ROOM_BUTTON_WIDTH = 300.0f;				///< Main Menu's
	const Real ROOM_WIDE_BUTTON_WIDTH = 488.0f;		///< Play Game's

	/// a length given for 1920x1080, on this screen (the smaller axis, as the layouts stretch)
	Int px( Real at1080 );
	/// a 1920x1080 board point on this screen, the board centred on a squarer one
	Int boardX( Real at1080 );
	Int boardY( Real at1080 );
	/// the whole screen: the shell's still, as a staging room stands on it without its map, dimmed,
	/// and the score screen's ruler frame over the board
	void shell( void );
	/// Arial at a layout's point size, grown for the screen the way a layout's text is
	GameFont *font( Int points, Bool bold = FALSE );
	/// the "Generals" face the layouts' titles and buttons are set in
	GameFont *titleFont( Int points );
	/// the floor under every word on a code screen: capitals this many pixels tall, at 1280x720 too
	const Int READABLE_CAPITALS = 12;
	/// font( points, bold ), or the next size up whose capitals reach READABLE_CAPITALS on this screen
	GameFont *readableFont( Int points, Bool bold = FALSE );
	/// a number with its thousands grouped: 12,345
	UnicodeString grouped( Int value );

	/// a filled box with a FRAME edge
	void panel( Int x, Int y, Int width, Int height, Color fill );
	/// the two-pixel rule under a header and over a foot
	void rule( Int x, Int y, Int width );
	/// a layout button's box: the panel's black under a blue, cyan, blue edge
	void button( Int x, Int y, Int width, Int height );
	enum ButtonLook { BUTTON_IDLE, BUTTON_LIT, BUTTON_PUSHED, BUTTON_DISABLED };
	/// a layout push button's own art, EA's three pieces, in one of its states; its label is ACTIVE
	/// when lit or pushed, as the layouts' HILITE text colour; button() where the art is missing
	void pushButton( Int x, Int y, Int width, Int height, ButtonLook look );
	/// that blue, cyan, blue edge alone, round a map preview or a progress bar
	void edge( Int x, Int y, Int width, Int height );
	/// a tab's box; the label in it is ACTIVE when the tab is open, INK when not
	void tab( Int x, Int y, Int width, Int height );
	/// a tab that answers the mouse: under it, a lit push button (pushed when the tab is open);
	/// returns its label's colour
	Color tab( Int x, Int y, Int width, Int height, Bool enabled, Bool open, Bool hovered );
	/// the player's colour down the edge of his row or card, stripeWidth() wide
	void stripe( Int x, Int y, Int height, Color player );
	Int stripeWidth( void );
	/// "Zero Hour Reforged" with its right edge at right; words are "Zero Hour" and " Reforged"
	void wordmark( DisplayString *words[ 2 ], Int right, Int y );
	/// a code screen's header line, the load board's and the score screen's alike: the title (the
	/// "Generals" face, capitals) from x, the subtitle in subtitleFont and MUTED headerGap after it,
	/// both centred on middle, the subtitle's parts joined by HEADER_JOINER
	void header( DisplayString *title, DisplayString *subtitle, Int x, Int middle );
	GameFont *subtitleFont( void );
	Int headerGap( void );
	extern const WideChar HEADER_JOINER[];	///< " · "
	/// a screen's title in capitals, as the layouts letter theirs
	UnicodeString capitals( const UnicodeString &words );
	/// a name in capitals whatever the language, so a map's English name keeps its plain I in Turkish
	UnicodeString nameCapitals( const UnicodeString &name );

	/// how wide text is, for abbreviate: a font's width on screen, or a test's own rule
	typedef Int (*TextMeasure)( const UnicodeString &text, void *context );
	/** The longest form of a label that fits widest, so no text ever overlaps another: the text
		* whole; else shortForm (a hand-written short form, may be empty); else the language's own
		* abbreviation (a word cut after a consonant and before a vowel with a period, the first
		* syllable kept, never after a soft sign, a short i or a soft g; English drops inner vowels
		* first; of several words the longest goes first and the last word last); else the text cut
		* with an ellipsis, never past its first letter.  words FALSE skips the shortening (a player's
		* name is only ever cut with the ellipsis); a word that is also a word of keep (a name inside a
		* sentence) is never shortened.  language is a TextLanguage. */
	UnicodeString abbreviate( const UnicodeString &text, const UnicodeString &shortForm, Int widest, Int language,
		TextMeasure measure, void *context, Bool words = TRUE, const UnicodeString &keep = UnicodeString::TheEmptyString );
	/// abbreviate in font for the text language the game shows
	UnicodeString fitText( const UnicodeString &text, GameFont *font, Int widest,
		const UnicodeString &shortForm = UnicodeString::TheEmptyString, const UnicodeString &keep = UnicodeString::TheEmptyString );
	/// whether the string files hold key, asked without fetching it: a fetch of a missing label logs
	/// "TEXT MISSING", which online_check.py fails a run on, so an optional key (a _Short form, the
	/// tip after the last) is probed here first
	Bool hasText( const char *key );
	/// the string of key fitted in font, its "<key>_Short" form tried before the automatic one
	UnicodeString fitKey( const char *key, GameFont *font, Int widest );
	/// a player's name fitted in font: whole, or cut with an ellipsis, never abbreviated
	UnicodeString fitName( const UnicodeString &name, GameFont *font, Int widest );

	/// a house colour lifted toward white where it is too dark to read as words on a panel
	Color readable( Color player );
	Color withAlpha( Color color, Int alpha );

	/// the face of a section heading inside a panel: the rooms' column heads' size, as the options
	/// screen's "Volume" measures
	GameFont *sectionFont( void );
	/// a section heading, ACTIVE, in sectionFont
	void heading( DisplayString *words, Int x, Int y );
	/// a fact row (Games, Win rate, Speed on a hub card): the label MUTED at the left, the value in
	/// valueColor ending at the row's right edge, both centred on the row, a GRID rule under it
	void fact( DisplayString *label, DisplayString *value, Int x, Int y, Int width, Int height, Color valueColor = INK );
	/// a track: GRID, filled from the left to fraction (0 to 1) in fill
	void track( Int x, Int y, Int width, Int height, Real fraction, Color fill = EDGE );
	/// a chip's face, TEXT_SMALL bold
	GameFont *chipFont( void );
	/// a chip (Open, Worn, a result): a one-pixel edge round its label, top left at x, y; returns its
	/// width.  A win is GOLD on GOLD, a loss MUTED on FRAME, the rest ACTIVE on FRAME.
	Int chip( DisplayString *label, Int x, Int y, Color color = ACTIVE, Color edge = FRAME );

	//-----------------------------------------------------------------------------------------------
	// Ratings as the online pages write them (ONLINE-UI-REDESIGN.md).  The pure ones take the
	// language, a TextLanguageType, so a test can ask for any; the others read the game's.
	//-----------------------------------------------------------------------------------------------
	/// rated games a player needs before the ladder ranks him, the site's ladder_min_games
	const Int RANKED_GAMES = 5;
	/// the site's tiers (website/app/Models/Player.php TIERS), lowest first; each shows EA's rank
	/// portrait of its name, and Corporal, Brigadier and Commander go unused
	enum RatingTier
	{
		TIER_UNRANKED,
		TIER_PRIVATE,
		TIER_SERGEANT,
		TIER_LIEUTENANT,
		TIER_CAPTAIN,
		TIER_MAJOR,
		TIER_COLONEL,
		TIER_GENERAL,
		TIER_COUNT
	};
	/// the tier a rating falls in: general from 30, colonel 25, major 20, captain 15, lieutenant 10,
	/// sergeant 5, private under that; unranked under RANKED_GAMES games or with the count unknown
	/// (-1).  A ladder rating goes through playerTier.  elo reads rating as the ladder's Elo, on the site's
	/// TIERS: general from 2000, colonel 1800, major 1600, captain 1400, lieutenant 1200, sergeant 1000.
	RatingTier ratingTier( Real rating, Int games, Bool elo = FALSE );
	/// the same tiers for an AI rating, which is an Elo and has no tier from the site: Easy's 800 is
	/// private, Medium's 1300 captain, Hard's 1800 general, each step between at its share of the ladder's
	RatingTier aiRatingTier( Real elo, Int games );
	/// the Elo an AI tier starts at, for the next tier's track (tierStart on the AI's scale)
	Real aiTierStart( RatingTier tier );
	/// a player's ladder tier: the site's (info.tier), which knows its own minimum of games and who is
	/// banned and rates the exact value, as the ladder shows it; ratingTier only when the site sent
	/// none or a key this build does not know
	RatingTier playerTier( const OnlinePlayerInfo &info );
	/// the rating a tier starts at, for the next tier's track; private and unranked start at 0, and on the
	/// Elo (elo) at the site's starting 1000, so private's track is empty as the site's is
	Real tierStart( RatingTier tier, Bool elo = FALSE );
	/// the site's key for a tier ("major"): OnlineTierName and GUI:OnlineTier_<key> say it in words
	const char *tierKey( RatingTier tier );
	/// EA's rank (RANK_PRIVATE onward, as rankImage reads it) whose portrait a tier shows, -1 unranked
	Int tierRank( RatingTier tier );
	/** A rank portrait's mapped image name: Rank_<rank>_USA, _China or _GLA for side, a PlayerTemplate
		* base side ("America" reads as USA), Rank_<rank>Elite for "Random" or any other army, EA's own
		* "Rank" for the elite private, "NewPlayer" for no side, empty for a rank past EA's ten. */
	AsciiString rankImageName( const AsciiString &side, Int rank );
	/// that image, NULL where the name is empty or the art is missing
	const Image *rankImage( const AsciiString &side, Int rank );
	/// a tier's portrait for side; NULL for unranked, who shows none
	const Image *tierImage( RatingTier tier, const AsciiString &side );
	/** An army's picture: a general's Generals' Challenge portrait (BioPortraitLarge, PAAirGen), else
		* the selected portrait of the army's StartingBuilding, the Command Center (SAComCentr_L), else its
		* button, else the army's logo; NULL for none. */
	const Image *armyPortrait( const PlayerTemplate *army );
	/// image scaled to cover the box, keeping its shape, the overflow cut off: sideways evenly, from the
	/// top a quarter of it, so a portrait keeps its head.  within, when given, is a clip the caller holds:
	/// the picture stays inside it too, and it is the clip again afterwards (else clipping goes off)
	void coverImage( const Image *image, Int x, Int y, Int width, Int height, const IRegion2D *within = NULL );
	/// a rating to two decimals, the language's way: 22.70, and 22,70 in Turkish, German, French,
	/// Russian and Ukrainian; sign puts a + before a rise, for a change.  Never "-0.00".
	UnicodeString ratingTextIn( Real rating, Bool sign, Int language );
	UnicodeString ratingText( Real rating, Bool sign = FALSE );
	/// an Elo, the AI rating, as a whole number rounded half up: 1213, +12 for a change.  Never "-0".
	UnicodeString eloText( Real elo, Bool sign = FALSE );
	/// a ladder figure: eloText when it is the Elo (elo), else the old scale's ratingText
	UnicodeString ladderTextIn( Real value, Bool elo, Bool sign, Int language );
	UnicodeString ladderText( Real value, Bool elo, Bool sign = FALSE );
	/// part of whole in whole percent, rounded half up; 0 with no whole
	Int percent( Int part, Int whole );
	/// a percentage the language's way: 55%, %55 in Turkish (as the load screen's YÜKLENİYOR %81),
	/// 55 % in German, French, Russian and Ukrainian
	UnicodeString percentTextIn( Int value, Int language );
	UnicodeString percentText( Int value );
	/// how a rating's move reads: a rise GOLD, a fall MUTED (the score screen's winner and defeat),
	/// no move INK
	Color changeColor( Real change );

	//-----------------------------------------------------------------------------------------------
	// The online pages in the menu language (MENU-REDESIGN.md packages 9 to 11, the design in
	// screens/online-entry/README-online-entry.md and header.py): the hub, Quick Match, Custom games,
	// Ladder and Skins stand in frame A on the board with five tabs on its tab row; the login is a
	// dialog with the same head.  Their masters (Tools/online_layout.py) are authored at 1920x1080 and
	// fitted at one scale, centred, the board's own fit.
	//-----------------------------------------------------------------------------------------------
	const Int ONLINE_LEFT = 60;										///< the content's edges, 28 in from the panel
	const Int ONLINE_RIGHT = 1860;
	const Real ONLINE_CONTENT_TOP = 182.0f;				///< MENU_CONTENT_TOP
	const Real ONLINE_CONTENT_BOTTOM = 946.0f;		///< 16 over the foot rule
	const Real ONLINE_SIDE_WIDTH = 600.0f;				///< the side column; the main column takes the rest
	const Real ONLINE_COLUMN_GAP = 24.0f;
	/// the login's dialog: the head keeps its offsets from the corner, content from 102 under its top
	const Int ONLINE_LOGIN_LEFT = 340;
	const Int ONLINE_LOGIN_TOP = 241;
	const Int ONLINE_LOGIN_RIGHT = 1580;
	const Int ONLINE_LOGIN_BOTTOM = 839;
	const Real ROOM_INSET_RIGHT = 1796.0f;				///< TEMP package 9: goes once its callers convert
	const Real ONLINE_TABS_TOP = 158.0f;
	const Real ONLINE_TABS_BOTTOM = 190.0f;

	/// a length that holds text on a code-drawn online page, by the height's scale.  px takes the
	/// smaller axis, 0.75 at 1440x1080, while adjustFontSize keeps the type at 1080's size there, so
	/// a box sized by px would be a third too small for its words; by the height it is not, and the
	/// main column narrows instead.  The same as px on any screen 16:9 or wider.
	Int tallPx( Real at1080 );
	/// the online pages' content band, ONLINE_CONTENT_TOP to ONLINE_CONTENT_BOTTOM on the board: a
	/// side column ONLINE_SIDE_WIDTH wide (tallPx) ending at ONLINE_RIGHT, and the main column from
	/// ONLINE_LEFT to ONLINE_COLUMN_GAP before it.  1176 and 600 at 16:9; at 4:3 the side keeps its
	/// width and the main column narrows.
	void onlineColumns( IRegion2D &main, IRegion2D &side );
	/// the page's panel on the screen: frame A's, or the login's dialog on a page with no tabs
	IRegion2D onlinePanel( void );
	/// a box inside a page: black at 70 in a 1 px FRAME edge
	void onlineBox( const IRegion2D &box );
	/** A box's section head: words in 14 px bold capitals, ACTIVE, with their top at top, and a lit
		* 1 px FRAME rule from left to right 24 under it.  Returns the y 26 under top, where the section's
		* content starts.  This sets the words' font. */
	Int onlineSection( DisplayString *words, Int left, Int right, Int top );
	/// a chip: 14 px bold capitals in a 1 px box 22 high, centred on middle from x; lit is an EDGE
	/// wash and edge with INK words (a win, Playing, the notice), else FRAME and MUTED.  Its width.
	Int onlineChip( DisplayString *label, Int x, Int middle, Bool lit );
	/// a ping light for a round trip in milliseconds, the lobby's rule (LobbyPingImage) on
	/// TheGameSpyConfig's cut-offs: Ping03 good, Ping02 middling, Ping01 bad; NULL for an unknown (< 0)
	const Image *pingImage( Int ms );
	/// the server the client is on, as every online page's subtitle has it: "<name> · <city> · 38 ms",
	/// each part left out while unknown, empty with no server
	UnicodeString onlineServerLine( void );

	/// the five tabs, in the masters' order; their buttons are TabHome, TabQuickMatch, TabCustom,
	/// TabLadder and TabSkins
	enum OnlineTab
	{
		ONLINE_TAB_NONE = -1,
		ONLINE_TAB_HOME,
		ONLINE_TAB_QUICK_MATCH,
		ONLINE_TAB_CUSTOM,
		ONLINE_TAB_LADDER,
		ONLINE_TAB_SKINS,
		ONLINE_TAB_COUNT
	};
	/** An online page's init: finds the tab buttons of layout ("WOLLadderScreen.wnd") under parent by
		* name, puts their labels in capitals, sizes each to its words (16 px bold plus 28 a side) and
		* stands them 8 apart from ONLINE_LEFT on the tab row, drawn as menuTab with open's tab open, and
		* makes the header's words.  A page with no tabs (the login) passes ONLINE_TAB_NONE and gets the
		* dialog's head with the wordmark.  Pair it with onlinePageShutdown, from the page's shutdown. */
	void onlinePageInit( GameWindow *parent, const char *layout, OnlineTab open );
	/// frees what onlinePageInit made, unless a later page's init (pushed in the same step) owns it now
	void onlinePageShutdown( GameWindow *parent );
	/// an online page with this head is up: it carries the server's notice itself (InGameUI's plate stands down)
	Bool onlinePageShowing( void );
	/** The head, from the draw callback of the page's panel window (before its children draw): the glass
		* and frame A round onlinePanel, the title in 40 px bold capitals at the content's left (title, or
		* with none the words of the master's hidden StaticTextTitle), a MUTED subtitle after it, the lit
		* header rule at 110.  Signed in, the title row's right end is the player's line, right to left:
		* the Bot ELO and the ELO as a label and a figure, the tier, the nick in gold, the tier's rank
		* portrait (a "?" well while unranked); the server's notice stands between it and the title as
		* a chip and its words; the tab row's right end is the server, its ping light (pingImage) and
		* round trip.  The friends package's FRIENDS key goes left of that server line, its invite toast
		* under the head's right end.  Signed out (the login) the wordmark takes the player's line's place. */
	void onlineHeader( const UnicodeString &title, const UnicodeString &subtitle = UnicodeString::TheEmptyString );
	/// the friends package's FRIENDS key: onlineHeader keeps width1080 (and 24 of air) clear at the
	/// left of the server's words on the tab row, the name cut shorter for it; 0 for no key
	void onlineReserveFriendsKey( Int width1080 );
	/// where the last onlineHeader stood that key, on the screen, 36 high on the tab row's middle;
	/// empty (lo.x == hi.x) when the header drew none (signed out, nothing reserved, no page)
	IRegion2D onlineFriendsKeyRect( void );
	/// a page's way out to a tab: its Back with the tab as the argument (see onlineTabClicked)
	typedef void (*OnlineLeave)( OnlineTab to );
	/** For the page's GBM_SELECTED: TRUE when control is one of its tabs, and the page then returns
		* MSG_HANDLED and does nothing else with it.  The open tab does nothing; any other calls
		* leave( tab ), where the page does what its Back does, refusing or asking where Back does (a
		* search, a join under way), with onlineTabScreen( tab ) as the next screen.  The shell is a
		* stack with the hub at its foot: the hub pushes that screen over itself, every other page sets
		* it as its next screen and pops, so its shutdown pushes it over the uncovered hub, and Home
		* (NULL) only pops. */
	Bool onlineTabClicked( GameWindow *control, OnlineLeave leave );
	/// the layout a tab opens, NULL for Home; Ladder and Skins share WOLLadderScreen.wnd
	const char *onlineTabScreen( OnlineTab tab );
	/// the tab the last click went to, so WOLLadderScreen.wnd opens as Skins; read it before
	/// onlinePageInit, which clears it
	OnlineTab onlineTabAsked( void );

	//===============================================================================================
	// The menu language (docs/plans/MENU-REDESIGN.md, "The foundation in code"): thin lit lines over
	// see-through dark, drawn by code and never from EA's art.  Everything is placed on the 1920x1080
	// board, scaled by the smaller axis and centred, so 21:9 and 4:3 letterbox with the backdrop
	// round it.  A converted screen uses these; the parts above stay for screens not yet converted.
	// UI-STYLE.md in the wrapper's docs/reference is the guide, with a worked example.
	//===============================================================================================

	/// frame A's grid on the board, every number at 1080
	const Int BOARD_WIDTH = 1920;
	const Int BOARD_HEIGHT = 1080;
	const Int MENU_PANEL_LEFT = 32;				///< the panel, 28 to 32 px off the screen's edges
	const Int MENU_PANEL_TOP = 28;
	const Int MENU_PANEL_RIGHT = 1888;
	const Int MENU_PANEL_BOTTOM = 1052;
	const Int MENU_INSET = 64;						///< content left; MENU_PANEL_RIGHT - 32 is its right
	const Int MENU_TITLE_TOP = 46;				///< the title row
	const Int MENU_TITLE_BOTTOM = 94;
	const Int MENU_HEADER_RULE = 110;			///< the lit header rule
	const Int MENU_TABS_TOP = 126;
	const Int MENU_TABS_BOTTOM = 162;
	const Int MENU_CONTENT_TOP = 182;
	const Int MENU_FOOT_RULE = 962;				///< the faint rule over the key row
	const Int MENU_KEYS_TOP = 976;
	const Int MENU_KEYS_BOTTOM = 1028;
	const Int MENU_KEY_WIDTH = 300;
	const Int MENU_KEY_GAP = 16;

	/// the board on a screen: scale is min( width / 1920, height / 1080 ), x and y its top left
	struct BoardFit
	{
		Real scale;
		Int x, y;
	};
	BoardFit boardFit( Int screenWidth, Int screenHeight );
	/// a 1080 rectangle (left, top, right, bottom) on that screen's board; pure, for tests
	IRegion2D boardRectOn( Int left, Int top, Int right, Int bottom, Int screenWidth, Int screenHeight );
	/// the same on this display
	IRegion2D boardRect( Int left, Int top, Int right, Int bottom );
	/// a length at 1080 on that screen, rounded, never under 1 when the length is not 0
	Int pxOn( Real at1080, Int screenWidth, Int screenHeight );

	/// The five sizes at 1080 (hud-artist.md, "Menu HUD"): a screen's title, a team's key figure,
	/// a row's key figure, body text and the 14 px capital labels.  The box family adds three of
	/// its own: its title 26, a key's word 20 and the foot's hint 15.  No text is under 14 at 1080.
	const Int MENU_TEXT_TITLE = 40;
	const Int MENU_TEXT_TEAM_FIGURE = 40;
	const Int MENU_TEXT_ROW_FIGURE = 24;
	const Int MENU_TEXT_BODY = 18;
	const Int MENU_TEXT_LABEL = 14;
	const Int MENU_TEXT_BOX_TITLE = 26;
	const Int MENU_TEXT_KEY = 20;
	const Int MENU_TEXT_HINT = 15;
	/// the smallest em menuFont draws, whatever the screen: 14 at 1080 is 9.3 at 720
	const Int MENU_TEXT_FLOOR = 9;
	/// the em in screen pixels a size at 1080 draws at on that screen, never under MENU_TEXT_FLOOR
	Int menuTextPixels( Int px1080, Int screenWidth, Int screenHeight );
	/// the point size whose em is that many pixels (the font library sets an em of points * 96 / 72)
	Int menuFontPoints( Int emPixels );
	/// Arial at a size given at 1080, its em scaled with the board: the one call for menu text
	GameFont *menuFont( Int px1080, Bool bold = FALSE );

	/// The backdrop.  Direct3D 11 blurs what is behind (radius 14) when a world is on the screen;
	/// on Direct3D 9 and SDL3 the backdrop is the dim alone.  veil darkens the whole screen;
	/// glass is a panel's see-through black over what is behind it (blurred when it can be).
	void veil( Int alpha );
	void glass( const IRegion2D &panel, Int alpha = 168 );
	/// the world outside a panel dimmed, as frame A stands on a dark shell
	void dimOutside( const IRegion2D &panel, Int alpha = 120 );
	/// whether glass blurs on this display right now (Display::drawBackdropBlur)
	Bool backdropBlurs( void );

	/// A line with its soft light: two wider passes at low alpha, then the line.  Only for the lines
	/// that matter: the panel edge, the header rule, a team's bar, the open tab, the primary key.
	void litLine( Int x, Int y, Int width, Int height, Color color );
	/// a 1 px rectangle's edge, its four sides each a litLine
	void litEdge( const IRegion2D &box, Color color );

	/// Frame A, the measured edge: the panel's 1 px lit EDGE line, ruler ticks standing out of it
	/// (4 px every 8, 8 every 40, 14 every 200, counted from the top left corner), a bright return
	/// 20 px off each corner, one cyan accent over the title.  panel is on the screen; the ticks are
	/// placed by the board's scale.  It dims the world outside the panel itself (dimOutside 120).
	void frameA( const IRegion2D &panel );
	/// the length at 1080 of the tick index steps of 8 px from the corner: 14, 8 or 4
	Int frameTickLength( Int index );
	const Int FRAME_TICK_STEP = 8;
	const Int FRAME_RETURN = 20;

	/// The title row of a converted screen: the title in MENU_TEXT_TITLE bold capitals at
	/// MENU_INSET, a MUTED subtitle after it, and the lit FRAME header rule at MENU_HEADER_RULE.
	/// words are the caller's strings; this sets their fonts.
	void menuHeader( DisplayString *title, DisplayString *subtitle );

	/// A tab: a 1 px TAB_EDGE box over black; the open one green with a lit line under it, the
	/// hovered one cyan.  The label (MENU_TEXT_LABEL + 2 bold) is centred; this sets its font.
	void menuTab( const IRegion2D &box, DisplayString *label, Bool open, Bool hovered );

	/// A key (a button): outline only.  Normal is a 1 px EDGE line; hover lifts it white with a
	/// cyan edge; pressed washes it EDGE; disabled is a FRAME line and a MUTED word.  The primary,
	/// lit key (exactly one on a screen: CONTINUE, START, the box key Enter presses) is the three
	/// line edge, EDGE, EDGE_LIT, EDGE, in its soft light over an EDGE wash.  The word is
	/// MENU_TEXT_KEY bold, centred; this sets its font.
	enum KeyState { KEY_NORMAL, KEY_HOVER, KEY_PRESSED, KEY_DISABLED };
	void menuKey( const IRegion2D &box, DisplayString *label, KeyState state, Bool primary );
	/** A push button drawn as menuKey from its own state (hilite, selected, enabled) and text.
		* primary puts the lit key on it.  A debug build asserts when two primaries under one top
		* level window are drawn in one frame. */
	void setMenuKey( GameWindow *button, Bool primary );

	/// A check box gadget drawn in the language: a 1 px box (EDGE, cyan on hover), a lit ACTIVE tick
	/// when checked, its label in body type at its right.
	void setMenuCheckBox( GameWindow *checkBox );
	/// A combo box gadget: its field a 1 px FRAME box over black (cyan on hover), its drop-down
	/// button the caret block, an EDGE square with a down caret.  The list it opens keeps its draw.
	void setMenuComboBox( GameWindow *comboBox );
	/// the caret block alone, for a code-drawn control
	void caretBlock( const IRegion2D &box, Bool hovered );

	/// A row's state.  LOCAL is the local player's: a gold edge light and a gold wash fading right
	/// (gold means him and nothing else).  HOVER is a white lift with a cyan edge light.  zebra lays
	/// the faint wash of an odd row at dense sizes.  No hairlines between rows.
	enum RowState { ROW_PLAIN, ROW_LOCAL, ROW_HOVER };
	void menuRow( const IRegion2D &row, RowState state, Bool zebra = FALSE );
	/// A column head in MENU_TEXT_LABEL bold capitals with its right edge at right; the sorted one
	/// ACTIVE with a caret before it.  This sets its font.
	void columnHead( DisplayString *words, Int right, Int middle, Bool sorted );

	/// The box: the message box pattern at three widths.  Every number is at 1080.
	enum BoxWidth { BOX_MENU = 560, BOX_QUESTION = 720, BOX_LIST = 1040 };
	const Int BOX_PAD = 32;					///< the content's inset
	const Int BOX_HEAD = 82;				///< the title band, down to the lit rule's foot
	const Int BOX_FOOT = 84;				///< the key band under the faint rule
	const Int BOX_LINE = 28;				///< a body line; the box grows a line at a time
	const Int BOX_KEY_WIDTH = 170;	///< three keys fit a question box
	const Int BOX_KEY_GAP = 16;			///< the 8 px grid, as MENU_KEY_GAP
	const Int BOX_KEY_HEIGHT = 48;
	const Int BOX_BAR_TOP = 832;		///< the command bar's highest plate: a box over a match ends 44 over it
	const Int BOX_TOP_MIN = 56;
	/// a question box holding lines of body: the head, a line of air, the lines, a line, the foot
	Int questionBoxHeight( Int lines );
	/// the most body lines a question box holds on the board
	Int questionBoxMaxLines( void );
	/// where a box of that size stands on the board: centred over a menu; over a match centred in
	/// the world above the bar and never reaching it
	IRegion2D boxRect1080( Int width, Int height, Bool overMatch );
	/// a key's rectangle on the board: index 0 is the rightmost (Enter's), counting left
	IRegion2D boxKeyRect1080( const IRegion2D &box, Int index );
	/** Words broken into lines no wider than widest, on spaces and on '\n'; a word wider than the
		* line stands alone on its own.  measure is the font's width on screen (or a test's rule). */
	void wrapLines( const UnicodeString &text, Int widest, TextMeasure measure, void *context,
		std::vector< UnicodeString > &lines );
	/** The box's shell on the screen: over a menu the screen steps back (veil 150), then the
		* glass (black 200), frame A, the title in MENU_TEXT_BOX_TITLE bold capitals from the top
		* left and the lit FRAME rule under the title band.  title may be NULL (the Victory band).
		* box is boardRect of boxRect1080.  This sets the title's font. */
	void boxShell( const IRegion2D &box, DisplayString *title, Bool overMatch );
	/// the faint rule over the box's foot
	void boxFootRule( const IRegion2D &box );

	//-----------------------------------------------------------------------------------------------
	// Added for the rooms (package 3a found the gaps): tracked capitals, heads from a left edge, a
	// tab's count, a pinned combo, a combo's text-only field and the open list.
	//-----------------------------------------------------------------------------------------------

	/** Words letter-spaced, as the designs set their capitals (14 px labels at 1.2, a box title at 12
		* percent of its size).  DisplayString cannot do it: Render2DSentenceClass builds a whole string
		* into one texture with its glyphs edge to edge, and spacing there would be a change to WW3D2 and
		* to every getWidth.  So each letter is drawn from a cached string of its own (one per font and
		* letter, kept for the run) at its advance plus the tracking.  tracking1080 is the space added
		* after each letter at 1080, scaled with the board.  Returns the width drawn. */
	Int drawTracked( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Real tracking1080 );
	Int trackedWidth( const UnicodeString &text, GameFont *font, Real tracking1080 );
	/// the pure part: the letters' advances summed, trackingPx between each two
	Int trackedWidthOf( const UnicodeString &text, Int trackingPx, TextMeasure measure, void *context );
	const Real MENU_LABEL_TRACKING = 1.2f;		///< the 14 px capitals' spacing at 1080

	/// columnHead from its left edge, in color (MUTED, the design's heads), tracked; the sorted one
	/// ACTIVE with its caret before it.  This sets the words' font.
	void columnHeadAt( DisplayString *words, Int left, Int middle, Color color = MUTED, Bool sorted = FALSE,
		Real tracking1080 = MENU_LABEL_TRACKING );

	/// menuTab with a count after its label in the label's colour (a closed chat's unread lines), two
	/// spaces after it; no count when count is 0 or less
	void menuTab( const IRegion2D &box, DisplayString *label, Bool open, Bool hovered, Int count );

	/// setMenuComboBox, pinned: while the combo is disabled (nobody here may change it) it draws as its
	/// value's words alone, no box and no caret
	void setMenuComboBoxPinned( GameWindow *comboBox );
	/// a combo's field as its words alone, in the field's font (GadgetComboBoxSetFont sets the field's,
	/// never the combo's own), INK, 10 px in, clipped to the field; setMenuComboBox leaves EA's draw
	void setMenuComboField( GameWindow *comboBox );
	/// a list box (a combo's open list: GadgetComboBoxGetListBox) on EA's colour draw in the language:
	/// near opaque black, a 1 px EDGE edge (cyan while hovered), the picked row an EDGE wash edged cyan.
	/// Its scroll bar keeps EA's art.
	void setMenuListBox( GameWindow *listBox );
}

#endif // _UISTYLE_H_
