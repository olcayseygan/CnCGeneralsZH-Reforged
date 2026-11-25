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

// FILE: MenuBoxParts.h ///////////////////////////////////////////////////////////////////////////
// The small drawing pieces the box family (docs/plans/MENU-REDESIGN.md, package 5) shares beside
// UIStyle's: a window's rect on the screen, a child placed by its 1080 rect, words drawn at a
// middle line, a player's stripe and lamp, and a pool of display strings for rows drawn in code.
// UIStyle holds the language's parts; these are only what four callbacks would otherwise each
// write again.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __MENUBOXPARTS_H_
#define __MENUBOXPARTS_H_

#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameWindow.h"
#include "GameClient/UIStyle.h"

#include <vector>

namespace MenuBox
{
	inline IRegion2D windowRect( GameWindow *window )
	{
		IRegion2D r;
		Int w, h;
		window->winGetScreenPosition( &r.lo.x, &r.lo.y );
		window->winGetSize( &w, &h );
		r.hi.x = r.lo.x + w;
		r.hi.y = r.lo.y + h;
		return r;
	}

	inline IRegion2D region( Int x0, Int y0, Int x1, Int y1 )
	{
		IRegion2D r;
		r.lo.x = x0;
		r.lo.y = y0;
		r.hi.x = x1;
		r.hi.y = y1;
		return r;
	}

	/// a 1080 rect on the screen's board
	inline IRegion2D onBoard( const IRegion2D &r1080 )
	{
		return UIStyle::boardRect( r1080.lo.x, r1080.lo.y, r1080.hi.x, r1080.hi.y );
	}

	/// window to a 1080 rect, its parent's screen rect given (a root's parent is the screen)
	inline void place( GameWindow *window, const IRegion2D &r1080 )
	{
		if( window == NULL )
			return;
		const IRegion2D s = onBoard( r1080 );
		Int px = 0, py = 0;
		if( window->winGetParent() )
			window->winGetParent()->winGetScreenPosition( &px, &py );
		window->winSetPosition( s.lo.x - px, s.lo.y - py );
		window->winSetSize( s.hi.x - s.lo.x, s.hi.y - s.lo.y );
	}

	inline void fill( const IRegion2D &r, Color color )
	{
		TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, color );
	}

	inline void outline( const IRegion2D &r, Color color )
	{
		TheDisplay->drawOpenRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, 1.0f, color );
	}

	/// a faint rule across, 1 px
	inline void rule( Int x0, Int x1, Int y, Int alpha )
	{
		TheDisplay->drawFillRect( x0, y, x1 - x0, 1, UIStyle::withAlpha( UIStyle::FRAME, alpha ) );
	}

	enum Align { LEFT, CENTRE, RIGHT };

	/// words on a middle line; returns the width drawn
	inline Int words( DisplayString *text, GameFont *font, Int x, Int middle, Color color, Align align = LEFT )
	{
		if( text == NULL || text->getTextLength() == 0 )
			return 0;
		if( text->getFont() != font )
			text->setFont( font );
		Int w, h;
		text->getSize( &w, &h );
		const Int left = align == LEFT ? x : align == CENTRE ? x - w / 2 : x - w;
		text->draw( left, middle - h / 2, color, GameMakeColor( 0, 0, 0, 160 ) );
		return w;
	}

	/// a player's colour as a stripe before his name, the light keyline round a dark colour
	inline void stripe( Int x, Int y0, Int y1, Int width, Color colour )
	{
		TheDisplay->drawFillRect( x, y0, width, y1 - y0, colour );
		Real r, g, b, a;
		GameGetColorComponentsReal( colour, &r, &g, &b, &a );
		if( r * 0.3f + g * 0.59f + b * 0.11f < 0.28f )
			TheDisplay->drawOpenRect( x, y0, width, y1 - y0, 1.0f, GameMakeColor( 200, 205, 220, 150 ) );
	}

	/// a round lamp, drawn as a small square cross (the display has no circle)
	inline void lamp( Int cx, Int cy, Int radius, Color colour )
	{
		TheDisplay->drawFillRect( cx - radius, cy - radius / 2, 2 * radius, radius, colour );
		TheDisplay->drawFillRect( cx - radius / 2, cy - radius, radius, 2 * radius, colour );
	}

	/** Display strings for rows drawn in code, handed out in order each frame and kept: a row's
		* words are set only when they change, so a steady list costs no text rebuild. */
	class TextPool
	{
	public:
		TextPool() : m_next( 0 ) {}
		~TextPool() { clear(); }
		void begin( void ) { m_next = 0; }
		DisplayString *get( const UnicodeString &text )
		{
			if( m_next == m_strings.size() )
				m_strings.push_back( TheDisplayStringManager->newDisplayString() );
			DisplayString *s = m_strings[ m_next++ ];
			if( s->getText().compare( text ) != 0 )
				s->setText( text );
			return s;
		}
		void clear( void )
		{
			if( TheDisplayStringManager )
				for( size_t each = 0; each < m_strings.size(); each++ )
					TheDisplayStringManager->freeDisplayString( m_strings[ each ] );
			m_strings.clear();
			m_next = 0;
		}
	private:
		std::vector< DisplayString * > m_strings;
		size_t m_next;
	};
}

#endif // __MENUBOXPARTS_H_
