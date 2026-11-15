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
#include "Common/UnicodeString.h"

class GameFont;
class DisplayString;

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
	extern const Color EDGE_LIT;				///< the bright middle line of a button's edge, a hover	extern const Color TAB_EDGE;				///< a tab's box
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
	/// the string of key fitted in font, its "<key>_Short" form tried before the automatic one
	UnicodeString fitKey( const char *key, GameFont *font, Int widest );
	/// a player's name fitted in font: whole, or cut with an ellipsis, never abbreviated
	UnicodeString fitName( const UnicodeString &name, GameFont *font, Int widest );

	/// a house colour lifted toward white where it is too dark to read as words on a panel
	Color readable( Color player );
	Color withAlpha( Color color, Int alpha );
}

#endif // _UISTYLE_H_
