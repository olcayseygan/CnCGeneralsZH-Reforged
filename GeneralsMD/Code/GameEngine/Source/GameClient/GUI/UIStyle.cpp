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

// FILE: UIStyle.cpp //////////////////////////////////////////////////////////////////////////////
//
// The colours, type and boxes of the front end's code-drawn screens.  See UIStyle.h.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/UIStyle.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/GameFont.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#include "Lib/WideCharFns.h"

namespace UIStyle
{

// read off MainMenu.wnd, OptionsMenu.wnd and the staging rooms at 1920x1080 on 2026-10-20
const Color GROUND = GameMakeColor( 0x05, 0x07, 0x0c, 255 );
const Color PANEL = GameMakeColor( 0, 0, 0, 200 );
const Color CARD = GameMakeColor( 0x0a, 0x0c, 0x16, 255 );
const Color FRAME = GameMakeColor( 0x2f, 0x37, 0xa8, 255 );
const Color GRID = GameMakeColor( 0x2f, 0x37, 0xa8, 90 );
const Color EDGE = GameMakeColor( 0x25, 0x34, 0xff, 255 );
const Color EDGE_LIT = GameMakeColor( 0x00, 0xe3, 0xff, 255 );
const Color TAB_EDGE = GameMakeColor( 0x85, 0x88, 0xb9, 255 );
const Color INK = GameMakeColor( 0xff, 0xff, 0xff, 255 );
const Color MUTED = GameMakeColor( 0xa4, 0xaa, 0xbe, 255 );
const Color ACTIVE = GameMakeColor( 0xba, 0xff, 0x0c, 255 );
// BRAND.md's gold and text blue
const Color GOLD = GameMakeColor( 0xf2, 0xc2, 0x30, 255 );
const Color WORDMARK_BLUE = GameMakeColor( 0x80, 0x95, 0xea, 255 );
const Color SHADOW = GameMakeColor( 0, 0, 0, 255 );
const Color SHELL_DIM = GameMakeColor( 2, 4, 12, 120 );

Int px( Real at1080 )
{
	const Real scale = min( TheDisplay->getWidth() / 1920.0f, TheDisplay->getHeight() / 1080.0f );
	return REAL_TO_INT( at1080 * scale + 0.5f );
}

Int boardX( Real at1080 )
{
	return ( TheDisplay->getWidth() - px( 1920.0f ) ) / 2 + px( at1080 );
}

Int boardY( Real at1080 )
{
	return ( TheDisplay->getHeight() - px( 1080.0f ) ) / 2 + px( at1080 );
}

void shell( void )
{
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	// both are retail art an incomplete install may lack
	const Image *still = TheMappedImageCollection->findImageByName( "MainMenuBackdrop" );
	if( still )
		TheDisplay->drawImage( still, 0, 0, width, height );
	else
		TheDisplay->drawFillRect( 0, 0, width, height, GROUND );
	TheDisplay->drawFillRect( 0, 0, width, height, SHELL_DIM );
	const Image *ruler = TheMappedImageCollection->findImageByName( "MainMenuRuler" );
	if( ruler )
		TheDisplay->drawImage( ruler, boardX( 0.0f ), boardY( 0.0f ), boardX( 1920.0f ), boardY( 1080.0f ) );
}

GameFont *font( Int points, Bool bold )
{
	return TheFontLibrary->getFont( AsciiString( "Arial" ), TheGlobalLanguageData->adjustFontSize( points ), bold );
}

GameFont *titleFont( Int points )
{
	return TheFontLibrary->getFont( AsciiString( "Generals" ), TheGlobalLanguageData->adjustFontSize( points ), FALSE );
}

void panel( Int x, Int y, Int width, Int height, Color fill )
{
	TheDisplay->drawFillRect( x, y, width, height, fill );
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, FRAME );
}

void rule( Int x, Int y, Int width )
{
	TheDisplay->drawFillRect( x, y, width, max( 1, px( 2 ) ), FRAME );
}

void button( Int x, Int y, Int width, Int height )
{
	TheDisplay->drawFillRect( x, y, width, height, PANEL );
	edge( x, y, width, height );
}

void edge( Int x, Int y, Int width, Int height )
{
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, EDGE );
	TheDisplay->drawOpenRect( x + 1, y + 1, width - 2, height - 2, 1.0f, EDGE_LIT );
	TheDisplay->drawOpenRect( x + 2, y + 2, width - 4, height - 4, 1.0f, EDGE );
}

void tab( Int x, Int y, Int width, Int height )
{
	TheDisplay->drawFillRect( x, y, width, height, PANEL );
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, TAB_EDGE );
}

void stripe( Int x, Int y, Int height, Color player )
{
	TheDisplay->drawFillRect( x, y, stripeWidth(), height, player );
}

Int stripeWidth( void )
{
	return max( 3, px( 6 ) );
}

void wordmark( DisplayString *words[ 2 ], Int right, Int y )
{
	Int first, second, height;
	words[ 0 ]->getSize( &first, &height );
	words[ 1 ]->getSize( &second, &height );
	words[ 0 ]->draw( right - first - second, y, INK, SHADOW );
	words[ 1 ]->draw( right - second, y, WORDMARK_BLUE, SHADOW );
}

/** towupper in the C locale leaves every letter past ASCII as it is ("ORDU SEç"), so Latin-1's,
	* Latin Extended-A's and Cyrillic's case pairs are mapped here, and Turkish's two i's by the language. */
static WideChar capital( WideChar letter, Bool turkish )
{
	const WideChar DOTTED_CAPITAL_I = 0x0130;
	const WideChar DOTLESS_SMALL_I = 0x0131;
	if( letter == u'i' && turkish )
		return DOTTED_CAPITAL_I;
	if( letter == DOTLESS_SMALL_I )
		return u'I';
	if( letter >= 0x00E0 && letter <= 0x00FE && letter != 0x00F7 )	// a with grave to thorn, not the division sign
		return (WideChar)( letter - 0x20 );
	if( letter >= 0x0430 && letter <= 0x044F )	// Russian's a to ya
		return (WideChar)( letter - 0x20 );
	if( letter >= 0x0450 && letter <= 0x045F )	// ie with grave to dzhe, Ukrainian's ie, i and yi among them
		return (WideChar)( letter - 0x50 );
	// pairs that start on an even code: A with macron to K with cedilla, eng to y with circumflex (g and s
	// with breve and cedilla among them), and Cyrillic's omega to ha with descender (Ukrainian's ghe with upturn)
	const Bool evenPair = ( letter >= 0x0100 && letter <= 0x0137 ) || ( letter >= 0x014A && letter <= 0x0177 )
		|| ( letter >= 0x0460 && letter <= 0x0481 ) || ( letter >= 0x048A && letter <= 0x04BF );
	if( evenPair && ( letter & 1 ) )
		return (WideChar)( letter - 1 );
	return (WideChar)WideCharToUpper( letter );
}

static UnicodeString capitalsOf( const UnicodeString &words, Bool turkish )
{
	UnicodeString result;
	for( Int each = 0; each < words.getLength(); each++ )
		result.concat( capital( words.getCharAt( each ), turkish ) );
	return result;
}

UnicodeString capitals( const UnicodeString &words )
{
	return capitalsOf( words, TheGlobalData->m_textLanguage == TEXT_LANGUAGE_TURKISH );
}

UnicodeString nameCapitals( const UnicodeString &name )
{
	return capitalsOf( name, FALSE );
}

/** Blue and purple house colours are too dark for words on a panel; they are lifted part of the way
	* to white, while a stripe, a line or a map spot keeps the colour itself. */
Color readable( Color player )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( player, &r, &g, &b, &a );
	const Int luma = ( 54 * r + 183 * g + 19 * b ) / 256;
	if( luma >= 120 )
		return player;
	const Int lift = 110;	// of 256
	return GameMakeColor( r + ( 255 - r ) * lift / 256, g + ( 255 - g ) * lift / 256, b + ( 255 - b ) * lift / 256, 255 );
}

Color withAlpha( Color color, Int alpha )
{
	UnsignedByte r, g, b, a;
	GameGetColorComponents( color, &r, &g, &b, &a );
	return GameMakeColor( r, g, b, alpha );
}

}
