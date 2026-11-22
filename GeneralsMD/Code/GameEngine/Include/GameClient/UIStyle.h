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
	/// TIERS: general from 1500, colonel 1350, major 1250, captain 1150, lieutenant 1075, sergeant 1000.
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
	// The online pages: the hub, Quick Match, Custom games, Ladder and Skins stand in one frame, the
	// room's panel, header and button row, with five tabs on the score screen's tab row and the
	// player's line at its right end.  Their masters (Tools/online_layout.py) are authored at
	// 1920x1080 and fitted, so the header is drawn on the same fitted board (boardX, boardY, px).
	//-----------------------------------------------------------------------------------------------
	const Real ROOM_INSET_RIGHT = 1796.0f;				///< ROOM_INSET mirrored: the content's right edge
	const Real ONLINE_TABS_TOP = 158.0f;					///< the score screen's tab row
	const Real ONLINE_TABS_BOTTOM = 190.0f;
	const Real ONLINE_CONTENT_TOP = 206.0f;
	const Real ONLINE_CONTENT_BOTTOM = 916.0f;
	const Real ONLINE_SIDE_WIDTH = 540.0f;				///< the side column; the main column takes the rest
	const Real ONLINE_COLUMN_GAP = 24.0f;

	/// a length that holds text on a code-drawn online page, by the height's scale.  px takes the
	/// smaller axis, 0.75 at 1440x1080, while adjustFontSize keeps the type at 1080's size there, so
	/// a box sized by px would be a third too small for its words; by the height it is not, and the
	/// main column narrows instead.  The same as px on any screen 16:9 or wider.
	Int tallPx( Real at1080 );
	/// the online pages' content band, ONLINE_CONTENT_TOP to ONLINE_CONTENT_BOTTOM on the board: a
	/// side column ONLINE_SIDE_WIDTH wide (tallPx) ending at ROOM_INSET_RIGHT, and the main column
	/// from ROOM_INSET to ONLINE_COLUMN_GAP before it.  1108 and 540 at 16:9; at 4:3 the side keeps
	/// its width and the main column narrows.
	void onlineColumns( IRegion2D &main, IRegion2D &side );
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
		* name, gives them the score screen's tab art with open's label ACTIVE, puts their labels in
		* capitals fitted to their boxes, and makes the header's words.  A page with no tabs (the login)
		* passes ONLINE_TAB_NONE and gets the header without the player's line.  Pair it with
		* onlinePageShutdown, from the page's shutdown. */
	void onlinePageInit( GameWindow *parent, const char *layout, OnlineTab open );
	/// frees what onlinePageInit made, unless a later page's init (pushed in the same step) owns it now
	void onlinePageShutdown( GameWindow *parent );
	/** The header, from the page's draw callback: the wordmark at the right of the title line and the
		* player's line at the right end of the tab row (nick, tier, ladder rating, AI rating, from what
		* the server said about the local player); with a title, also the title in capitals from x 120
		* in the Generals face and the two-pixel FRAME rule at 147.  An empty title is for a master that
		* carries its own title and rule (StaticTextTitle).  The subtitle follows the title's text, ours
		* or the master's, MUTED and cut with an ellipsis before the wordmark; an empty one is the
		* server line, its ping light before the milliseconds as the mockups have it. */
	void onlineHeader( const UnicodeString &title, const UnicodeString &subtitle = UnicodeString::TheEmptyString );
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
}

#endif // _UISTYLE_H_
