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

// FILE: SinglePlayerParts.h //////////////////////////////////////////////////////////////////////
// The drawing pieces the single player's screens share beside UIStyle's and MenuBox's (package 12):
// spaced capitals, a list box whose rows code draws over EA's gadget (its row height, which row is
// where, the 3 px bar), a picked row, a ring, a text box in the language.  The Challenge, the replay
// list, Load Game and both boxes call them; SinglePlayerLayout.h holds the numbers.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SINGLEPLAYERPARTS_H_
#define __SINGLEPLAYERPARTS_H_

#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MenuBoxParts.h"
#include "GameClient/Mouse.h"
#include "GameClient/SinglePlayerLayout.h"
#include "GameClient/UIStyle.h"

namespace SinglePlayerParts
{
	/// a 1080 rect on the screen's board
	inline IRegion2D on( const IRegion2D &r1080 ) { return MenuBox::onBoard( r1080 ); }
	inline Int px( Real at1080 ) { return UIStyle::pxOn( at1080, TheDisplay->getWidth(), TheDisplay->getHeight() ); }

	/// the 14 px capitals, spaced as the design spaces them, on a middle line; returns the width
	inline Int capitals( const UnicodeString &text, Int x, Int middle, Color color, MenuBox::Align align = MenuBox::LEFT,
		Int widest = 0 )
	{
		if( text.isEmpty() )
			return 0;
		GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
		UnicodeString words = UIStyle::capitals( text );
		if( widest > 0 && UIStyle::trackedWidth( words, font, UIStyle::MENU_LABEL_TRACKING ) > widest )
			words = UIStyle::capitals( UIStyle::fitText( text, font, widest ) );
		const Int w = UIStyle::trackedWidth( words, font, UIStyle::MENU_LABEL_TRACKING );
		const Int left = align == MenuBox::LEFT ? x : align == MenuBox::CENTRE ? x - w / 2 : x - w;
		UIStyle::drawTracked( words, font, left, middle - font->height / 2, color, UIStyle::MENU_LABEL_TRACKING );
		return w;
	}

	/// a key's capitals, its _Short form when the long one does not fit widest
	inline Int capitalsKey( const char *key, Int x, Int middle, Color color, MenuBox::Align align = MenuBox::LEFT,
		Int widest = 0 )
	{
		GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
		UnicodeString text = TheGameText->fetch( key );
		if( widest > 0 && UIStyle::trackedWidth( UIStyle::capitals( text ), font, UIStyle::MENU_LABEL_TRACKING ) > widest )
		{
			AsciiString shortKey = key;
			shortKey.concat( "_Short" );
			if( UIStyle::hasText( shortKey.str() ) )
				text = TheGameText->fetch( shortKey );
		}
		return capitals( text, x, middle, color, align, widest );
	}

	/// words fitted to widest (a name only ever cut with the ellipsis) on a middle line
	inline Int words( MenuBox::TextPool &pool, const UnicodeString &text, Int size, Bool bold, Int x, Int middle,
		Color color, Int widest, MenuBox::Align align = MenuBox::LEFT, Bool name = FALSE )
	{
		GameFont *font = UIStyle::menuFont( size, bold );
		const UnicodeString fitted = widest <= 0 ? text : name ? UIStyle::fitName( text, font, widest ) : UIStyle::fitText( text, font, widest );
		return MenuBox::words( pool.get( fitted ), font, x, middle, color, align );
	}

	/// a key's words fitted to widest, its _Short form first when the long one does not fit
	inline Int wordsKey( MenuBox::TextPool &pool, const char *key, Int size, Bool bold, Int x, Int middle, Color color,
		Int widest, MenuBox::Align align = MenuBox::LEFT )
	{
		GameFont *font = UIStyle::menuFont( size, bold );
		return MenuBox::words( pool.get( UIStyle::fitKey( key, font, widest ) ), font, x, middle, color, align );
	}

	inline Int measureLetters( const UnicodeString &text, void *font )
	{
		return UIStyle::trackedWidth( text, (GameFont *)font, 0.0f );
	}

	/// words broken to widest, at most lines of them, pitch apart from top; returns the y under the last
	inline Int wrapped( MenuBox::TextPool &pool, const UnicodeString &text, Int size, Int x, Int top, Int widest,
		Int lines, Color color, Int pitch )
	{
		std::vector< UnicodeString > broken;
		UIStyle::wrapLines( text, widest, measureLetters, UIStyle::menuFont( size, FALSE ), broken );
		Int y = top;
		for( size_t each = 0; each < broken.size() && (Int)each < lines; each++ )
		{
			words( pool, broken[ each ], size, FALSE, x, y + pitch / 2, color, widest, MenuBox::LEFT, TRUE );
			y += pitch;
		}
		return y;
	}

	/// a game speed as the lists write it: 1.0x, 1.25x, 1.5x
	inline UnicodeString speedText( Int percent )
	{
		UnicodeString text;
		if( percent % 10 == 0 )
			text.format( u"%d.%dx", percent / 100, ( percent % 100 ) / 10 );
		else
			text.format( u"%d.%02dx", percent / 100, percent % 100 );
		return text;
	}

	/// seconds as a clock: 14:05, 1:12:40
	inline UnicodeString lengthText( Int seconds )
	{
		char buffer[ 32 ];
		SinglePlayerLayout::clockText( seconds, buffer, sizeof( buffer ) );
		UnicodeString text;
		text.translate( AsciiString( buffer ) );
		return text;
	}

	/// a point at 1080 on the screen's board
	inline ICoord2D point( Real x1080, Real y1080 )
	{
		const IRegion2D r = UIStyle::boardRect( (Int)floor( x1080 + 0.5f ), (Int)floor( y1080 + 0.5f ), 1921, 1081 );
		return r.lo;
	}

	/// a key in the language, its word the key's capitals (the _Short form when the long one does not fit)
	inline void key( GameWindow *button, const char *textKey, Bool primary )
	{
		if( button == NULL )
			return;
		UIStyle::setMenuKey( button, primary );
		Int w, h;
		button->winGetSize( &w, &h );
		button->winSetTooltip( UnicodeString::TheEmptyString );
		GadgetButtonSetText( button, UIStyle::capitals( UIStyle::fitKey( textKey, UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE ),
			w - px( 24.0f ) ) ) );
	}

	/// a picked row or card: an EDGE wash and a lit cyan edge, as the primary key is lit
	inline void picked( const IRegion2D &r )
	{
		MenuBox::fill( r, UIStyle::withAlpha( UIStyle::EDGE, 52 ) );
		UIStyle::litEdge( r, UIStyle::EDGE_LIT );
	}

	/// a section's head: green spaced capitals, what it holds after them MUTED, a faint rule under
	inline void section( MenuBox::TextPool &pool, Int x0, Int x1, Int top, const UnicodeString &label,
		const UnicodeString &after = UnicodeString::TheEmptyString )
	{
		const Int middle = top + px( 9.0f );
		const Int w = capitals( label, x0, middle, UIStyle::ACTIVE );
		if( !after.isEmpty() )
			words( pool, after, UIStyle::MENU_TEXT_BODY, FALSE, x0 + w + px( 16.0f ), middle, UIStyle::MUTED,
				x1 - x0 - w - px( 16.0f ), MenuBox::LEFT, TRUE );
		MenuBox::rule( x0, x1, top + px( 26.0f ), 150 );
	}

	/// a fact: its label in capitals, its value in body type under it
	inline void fact( MenuBox::TextPool &pool, const IRegion2D &cell, const UnicodeString &label,
		const UnicodeString &value, Color ink = UIStyle::INK )
	{
		const Int w = cell.hi.x - cell.lo.x;
		capitals( label, cell.lo.x, cell.lo.y + px( 8.0f ), UIStyle::MUTED, MenuBox::LEFT, w );
		words( pool, value, UIStyle::MENU_TEXT_BODY, FALSE, cell.lo.x, cell.lo.y + px( 33.0f ), ink, w, MenuBox::LEFT, TRUE );
	}

	/// a disc of colour, rows of fills (the display has no circle)
	inline void disc( Int cx, Int cy, Int radius, Color color )
	{
		for( Int dy = -radius; dy <= radius; dy++ )
		{
			const Int half = (Int)sqrt( (double)( radius * radius - dy * dy ) );
			TheDisplay->drawFillRect( cx - half, cy + dy, 2 * half + 1, 1, color );
		}
	}

	/// a ring: a dark disc edged in colour, its battle's number in it (none for the player's own)
	inline void ring( MenuBox::TextPool &pool, Int cx, Int cy, Int radius, Color edge, Int number, Int width = 2 )
	{
		disc( cx, cy, radius + px( 2.0f ), GameMakeColor( 6, 8, 14, 235 ) );
		const Int segments = 24;
		for( Int each = 0; each < segments; each++ )
		{
			const Real a0 = 6.2831853f * each / segments, a1 = 6.2831853f * ( each + 1 ) / segments;
			TheDisplay->drawLine( cx + (Int)( radius * cos( a0 ) ), cy + (Int)( radius * sin( a0 ) ),
				cx + (Int)( radius * cos( a1 ) ), cy + (Int)( radius * sin( a1 ) ), (Real)max( 1, px( (Real)width ) ), edge );
		}
		if( number > 0 )
		{
			UnicodeString n;
			n.format( u"%d", number );
			MenuBox::words( pool.get( n ), UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE ), cx, cy, UIStyle::INK, MenuBox::CENTRE );
		}
	}

	/// a picture in a 1 px FRAME edge
	inline void framed( const Image *image, const IRegion2D &r, Bool cover = TRUE )
	{
		if( image )
		{
			if( cover )
				UIStyle::coverImage( image, r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y );
			else
				TheDisplay->drawImage( image, r.lo.x, r.lo.y, r.hi.x, r.hi.y );
		}
		MenuBox::outline( MenuBox::region( r.lo.x - 1, r.lo.y - 1, r.hi.x + 1, r.hi.y + 1 ), UIStyle::withAlpha( UIStyle::FRAME, 220 ) );
	}

	//-----------------------------------------------------------------------------------------------
	// a list box whose rows code draws: EA's gadget keeps the model, the scrolling, the clicks and the
	// keys; its rows are given a fixed height by an empty picture cell, and its own draw and scroll
	// buttons give way to the caller's
	//-----------------------------------------------------------------------------------------------
	inline ListboxData *listData( GameWindow *list ) { return list ? (ListboxData *)list->winGetUserData() : NULL; }

	/// the height every row takes: an empty picture in column column sets it (a row's pitch is its
	/// height and one pixel, the gadget's own rule)
	inline void rowHeight( GameWindow *list, Int row, Int column, Int px1080 )
	{
		GadgetListBoxAddEntryImage( list, NULL, row, column, max( 4, px( (Real)px1080 ) - 2 ), 1 );
	}

	/// the list's own draw replaced, its scroll bar's windows hidden (the caller draws a 3 px one)
	inline void takeOver( GameWindow *list, GameWinDrawFunc draw )
	{
		ListboxData *data = listData( list );
		if( data == NULL )
			return;
		list->winSetDrawFunc( draw );
		if( data->upButton )
			data->upButton->winHide( TRUE );
		if( data->downButton )
			data->downButton->winHide( TRUE );
		if( data->slider )
			data->slider->winHide( TRUE );
	}

	/// row's rectangle on the screen, inside the list's own rect; top < bottom only when it shows
	inline IRegion2D rowRect( GameWindow *list, Int row, Int right )
	{
		const ListboxData *data = listData( list );
		const IRegion2D r = MenuBox::windowRect( list );
		const Int above = row > 0 ? data->listData[ row - 1 ].listHeight : 0;
		const Int top = r.lo.y + above - data->displayPos;
		return MenuBox::region( r.lo.x, top, right, top + data->listData[ row ].height + 1 );
	}

	inline Int rowCount( GameWindow *list ) { return GadgetListBoxGetNumEntries( list ); }

	inline Int selectedRow( GameWindow *list )
	{
		Int selected = -1;
		GadgetListBoxGetSelected( list, &selected );
		return selected;
	}

	/// the row under the pointer, -1 for none
	inline Int hoveredRow( GameWindow *list )
	{
		if( TheMouse == NULL || list == NULL || list->winIsHidden() )
			return -1;
		const ICoord2D at = TheMouse->getMouseStatus()->pos;
		const IRegion2D r = MenuBox::windowRect( list );
		if( at.x < r.lo.x || at.x >= r.hi.x || at.y < r.lo.y || at.y >= r.hi.y )
			return -1;
		const Int count = rowCount( list );
		for( Int row = 0; row < count; row++ )
		{
			const IRegion2D rr = rowRect( list, row, r.hi.x );
			if( at.y >= rr.lo.y && at.y < rr.hi.y )
				return row;
		}
		return -1;
	}

	/// the list's 3 px bar at x: a faint track, the thumb in EDGE; nothing when every row shows
	inline void scrollBar( GameWindow *list, Int x )
	{
		const ListboxData *data = listData( list );
		const IRegion2D r = MenuBox::windowRect( list );
		if( data == NULL || data->totalHeight <= data->displayHeight || data->totalHeight <= 0 )
			return;
		const Int w = max( 2, px( 3.0f ) );
		const Int h = r.hi.y - r.lo.y;
		TheDisplay->drawFillRect( x, r.lo.y, w, h, UIStyle::withAlpha( UIStyle::FRAME, 90 ) );
		const Int t0 = r.lo.y + h * data->displayPos / data->totalHeight;
		const Int t1 = r.lo.y + h * min( data->totalHeight, data->displayPos + data->displayHeight ) / data->totalHeight;
		TheDisplay->drawFillRect( x, t0, w, max( 2, t1 - t0 ), UIStyle::EDGE );
	}

	/// column heads over a list: each its capitals over its words, the sorted one green with a caret;
	/// one lit rule under them, 16 px past the first and the last
	inline void heads( const SinglePlayerLayout::Column *columns, const UnicodeString *labels, Int count, Int sorted,
		Int top1080, Int listLeft1080, Int listRight1080 )
	{
		const Int middle = on( SinglePlayerLayout::rect( 0, top1080 + 9, 1, top1080 + 10 ) ).lo.y;
		for( Int each = 0; each < count; each++ )
		{
			const IRegion2D c = on( SinglePlayerLayout::rect( columns[ each ].left, top1080, columns[ each ].right, top1080 + 18 ) );
			const Color ink = each == sorted ? UIStyle::ACTIVE : UIStyle::INK;
			const Int w = capitals( labels[ each ], columns[ each ].alignRight ? c.hi.x : c.lo.x, middle, ink,
				columns[ each ].alignRight ? MenuBox::RIGHT : MenuBox::LEFT, c.hi.x - c.lo.x - px( 16.0f ) );
			if( each == sorted )
			{
				// the caret, a small down triangle after the words
				const Int cx = columns[ each ].alignRight ? c.hi.x - w - px( 12.0f ) : c.lo.x + w + px( 12.0f );
				const Int s = px( 5.0f );
				for( Int row = 0; row <= s; row++ )
					TheDisplay->drawFillRect( cx - ( s - row ), middle - s / 2 + row, 2 * ( s - row ) + 1, 1, UIStyle::ACTIVE );
			}
		}
		const IRegion2D rule = on( SinglePlayerLayout::rect( listLeft1080, top1080 + 26, listRight1080, top1080 + 27 ) );
		UIStyle::litLine( rule.lo.x, rule.lo.y, rule.hi.x - rule.lo.x, 1, UIStyle::FRAME );
	}

	//-----------------------------------------------------------------------------------------------
	// a text box in the language: see-through black, a cyan lit edge while it has the keys, EA's
	// colour draw for the words and the caret over it
	//-----------------------------------------------------------------------------------------------
	inline void entryDraw( GameWindow *window, WinInstanceData *instData )
	{
		const IRegion2D r = MenuBox::windowRect( window );
		MenuBox::fill( r, GameMakeColor( 0, 0, 0, 150 ) );
		if( TheWindowManager->winGetFocus() == window )
			UIStyle::litEdge( r, UIStyle::EDGE_LIT );
		else
			MenuBox::outline( r, UIStyle::EDGE );
		TheWindowManager->getTextEntryDrawFunc()( window, instData );
	}

	inline void setEntry( GameWindow *entry )
	{
		if( entry == NULL )
			return;
		entry->winClearStatus( WIN_STATUS_IMAGE );
		const Color none = (Color)WIN_COLOR_UNDEFINED;
		GadgetTextEntrySetEnabledColor( entry, none );
		GadgetTextEntrySetEnabledBorderColor( entry, none );
		GadgetTextEntrySetHiliteColor( entry, none );
		GadgetTextEntrySetHiliteBorderColor( entry, none );
		GadgetTextEntrySetDisabledColor( entry, none );
		GadgetTextEntrySetDisabledBorderColor( entry, none );
		entry->winSetEnabledTextColors( UIStyle::INK, GameMakeColor( 0, 0, 0, 160 ) );
		entry->winSetHiliteTextColors( UIStyle::INK, GameMakeColor( 0, 0, 0, 160 ) );
		GadgetTextEntrySetFont( entry, UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ) );
		entry->winSetDrawFunc( entryDraw );
	}
}

class PlayerTemplate;

/// a seat of a replay, as its details list it
struct ReplaySeatInfo
{
	UnicodeString name;					///< the player's, or the computer level's
	UnicodeString shown;				///< name, numbered when two seats share it
	Color colour;
	const PlayerTemplate *army;	///< NULL for a random pick
	Int team;										///< -1 for none
	Int startPos;								///< -1 for none
	Bool left;									///< dropped out of the match
	Bool recorder;							///< the player whose replay it is
};

/// a replay as the lists show it, read off its header by PopulateReplayFileListbox (ReplayMenu.cpp)
struct ReplayRowInfo
{
	UnicodeString label;				///< the file's name, "Last Replay" for the newest
	UnicodeString recorded;			///< date and time in the player's own format
	UnicodeString map;
	AsciiString mapName;
	UnicodeString version;
	UnicodeString teams;				///< "4 v 4"
	Bool older;									///< another version or CRC recorded it
	Int seconds;
	Int speedPercent;
	Bool multiplayer, quitEarly, desync;
	Int64 sortKey;
	std::vector< ReplaySeatInfo > seats;
};

/// the details of a row of a list PopulateReplayFileListbox filled, NULL for none
const ReplayRowInfo *GetReplayRowInfo( GameWindow *listbox, Int row );
/// a replay list's rows over EA's gadget: kinds names each column's field (SinglePlayerLayout's
/// ReplayColumn), columns where each stands at 1080; a replay another build recorded is MUTED
void DrawReplayRows( GameWindow *listbox, MenuBox::TextPool &pool, const SinglePlayerLayout::Column *columns,
	const Int *kinds, Int count, Int rowRight1080 );

#endif // __SINGLEPLAYERPARTS_H_
