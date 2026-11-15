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
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
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

GameFont *readableFont( Int points, Bool bold )
{
	// Arial's capitals stand about five eighths of its cell, so twelve pixels of capital want a cell
	// of about nineteen and a half
	GameFont *found = font( points, bold );
	for( Int grown = points + 1; found && found->height * 5 < READABLE_CAPITALS * 8 && grown <= points + 12; grown++ )
		found = font( grown, bold );
	return found;
}

UnicodeString grouped( Int value )
{
	UnicodeString digits;
	digits.format( u"%d", value < 0 ? -value : value );
	UnicodeString text;
	if( value < 0 )
		text.concat( u'-' );
	const Int length = digits.getLength();
	for( Int each = 0; each < length; each++ )
	{
		if( each > 0 && ( length - each ) % 3 == 0 )
			text.concat( u',' );
		text.concat( digits.getCharAt( each ) );
	}
	return text;
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

void pushButton( Int x, Int y, Int width, Int height, ButtonLook look )
{
	// the names every layout's MainButton draws with in its four states
	static const char *const PREFIX[] = { "Buttons-", "Buttons-HiLite-", "Buttons-Pushed-", "Buttons-Disabled-" };
	AsciiString name;
	name.format( "%sLeft", PREFIX[ look ] );
	const Image *left = TheMappedImageCollection->findImageByName( name );
	name.format( "%sMiddle", PREFIX[ look ] );
	const Image *middle = TheMappedImageCollection->findImageByName( name );
	name.format( "%sRight", PREFIX[ look ] );
	const Image *right = TheMappedImageCollection->findImageByName( name );
	if( !left || !middle || !right )
	{
		button( x, y, width, height );	// retail art an incomplete install may lack
		return;
	}

	// laid out as W3DGadgetPushButtonImageDrawThree lays a layout's button out
	const Int leftEnd = x + left->getImageWidth();
	const Int rightStart = x + width - right->getImageWidth();
	if( rightStart <= leftEnd )
	{
		TheDisplay->drawImage( left, x, y, x + width / 2, y + height );
		TheDisplay->drawImage( right, x + width / 2, y, x + width, y + height );
		return;
	}
	const Int piece = middle->getImageWidth();
	Int at = leftEnd;
	for( ; at + piece <= rightStart; at += piece )
		TheDisplay->drawImage( middle, at, y, at + piece, y + height );
	if( at < rightStart )
	{
		IRegion2D rest;
		rest.lo.x = at;
		rest.lo.y = y;
		rest.hi.x = rightStart;
		rest.hi.y = y + height;
		TheDisplay->setClipRegion( &rest );
		TheDisplay->drawImage( middle, at, y, at + piece, y + height );
		TheDisplay->enableClipping( FALSE );
	}
	TheDisplay->drawImage( left, x, y, leftEnd, y + height );
	TheDisplay->drawImage( right, rightStart, y, x + width, y + height );
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

Color tab( Int x, Int y, Int width, Int height, Bool enabled, Bool open, Bool hovered )
{
	if( !enabled )
	{
		tab( x, y, width, height );
		return MUTED;
	}
	if( hovered )
	{
		pushButton( x, y, width, height, open ? BUTTON_PUSHED : BUTTON_LIT );
		return ACTIVE;
	}
	tab( x, y, width, height );
	return open ? ACTIVE : INK;
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

const WideChar HEADER_JOINER[] = { u' ', 0x00B7, u' ', 0 };

GameFont *subtitleFont( void )
{
	return readableFont( TEXT_BODY );
}

Int headerGap( void )
{
	return px( 16.0f );
}

void header( DisplayString *title, DisplayString *subtitle, Int x, Int middle )
{
	Int titleWidth, titleHeight;
	title->getSize( &titleWidth, &titleHeight );
	title->draw( x, middle - titleHeight / 2, INK, SHADOW );
	if( subtitle && subtitle->getTextLength() > 0 )
		subtitle->draw( x + titleWidth + headerGap(), middle - subtitle->getFont()->height / 2, MUTED, SHADOW );
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

//-------------------------------------------------------------------------------------------------
// Abbreviation.  Letters are named by number: a u"" literal is read in the machine's code page by MSVC.
//-------------------------------------------------------------------------------------------------
static const WideChar ABBREVIATION_ELLIPSIS = 0x2026;

static Bool isCyrillic( WideChar c )
{
	return c >= 0x0400 && c <= 0x04FF;
}

static Bool isLetter( WideChar c )
{
	return ( c >= u'a' && c <= u'z' ) || ( c >= u'A' && c <= u'Z' ) || ( c >= 0x00C0 && c <= 0x024F && c != 0x00D7 && c != 0x00F7 )
		|| isCyrillic( c );
}

/// a Cyrillic capital as its small letter, so one table serves both
static WideChar cyrillicSmall( WideChar c )
{
	if( c >= 0x0410 && c <= 0x042F )
		return (WideChar)( c + 0x20 );
	if( c >= 0x0400 && c <= 0x040F )
		return (WideChar)( c + 0x50 );
	return c;
}

/** A vowel in any of the game's languages.  yVowel: y is a vowel in French and German; in English
	* (Dstryd) and Turkish (OYUNCU, OY.) it is a consonant. */
static Bool isVowel( WideChar c, Bool yVowel )
{
	switch( c )
	{
		case u'a': case u'e': case u'i': case u'o': case u'u':
		case u'A': case u'E': case u'I': case u'O': case u'U':
			return TRUE;
		case u'y': case u'Y':
			return yVowel;
		case 0x00FF: case 0x0130: case 0x0131: case 0x0152: case 0x0153:	// y diaeresis, Turkish dotted I and dotless i, oe
			return TRUE;
	}
	// Latin-1's a grave to ae, e grave to i diaeresis, o grave to o diaeresis, o stroke to y acute, both cases
	// (not named "small": the Windows headers define that as char)
	const WideChar lower = ( c >= 0x00C0 && c <= 0x00DE ) ? (WideChar)( c + 0x20 ) : c;
	if( ( lower >= 0x00E0 && lower <= 0x00E6 ) || ( lower >= 0x00E8 && lower <= 0x00EF ) || ( lower >= 0x00F2 && lower <= 0x00F6 )
			|| ( lower >= 0x00F8 && lower <= 0x00FD ) )
		return TRUE;
	switch( cyrillicSmall( c ) )
	{
		case 0x0430: case 0x0435: case 0x0438: case 0x043E: case 0x0443: case 0x044B: case 0x044D: case 0x044E:
		case 0x044F: case 0x0451: case 0x0454: case 0x0456: case 0x0457:	// a e i o u y e yu ya yo, Ukrainian ie i yi
			return TRUE;
	}
	return FALSE;
}

/// a letter a word is never cut after: the short i, the soft and hard signs, Turkish soft g
static Bool holdsOn( WideChar c )
{
	const WideChar lower = cyrillicSmall( c );
	return lower == 0x0439 || lower == 0x044C || lower == 0x044A || c == 0x011E || c == 0x011F;
}

static UnicodeString partOf( const UnicodeString &text, Int from, Int count )
{
	UnicodeString part;
	for( Int at = from; at < from + count && at < text.getLength(); at++ )
		part.concat( text.getCharAt( at ) );
	return part;
}

/** A word's forms, longest first: itself, then (in English, a word of six letters or more) its inner
	* vowels dropped, then every cut after a consonant and before a vowel that keeps the first syllable,
	* with a period.  A word under four letters keeps itself. */
static std::vector< UnicodeString > wordForms( const UnicodeString &word, Bool english, Bool yVowel )
{
	std::vector< UnicodeString > forms;
	forms.push_back( word );
	const Int length = word.getLength();
	if( length < 4 )
		return forms;
	if( english && length >= 6 )
	{
		UnicodeString dropped;
		dropped.concat( word.getCharAt( 0 ) );
		for( Int at = 1; at + 1 < length; at++ )
		{
			if( !isVowel( word.getCharAt( at ), FALSE ) )
				dropped.concat( word.getCharAt( at ) );
		}
		dropped.concat( word.getCharAt( length - 1 ) );
		if( dropped.getLength() < length )
			forms.push_back( dropped );
	}
	for( Int at = length - 2; at >= 1; at-- )
	{
		const WideChar before = word.getCharAt( at - 1 );
		if( !isLetter( before ) || isVowel( before, yVowel ) || holdsOn( before ) || !isVowel( word.getCharAt( at ), yVowel ) )
			continue;
		Bool syllable = FALSE;
		for( Int each = 0; each < at && !syllable; each++ )
			syllable = isVowel( word.getCharAt( each ), yVowel );
		if( !syllable )
			continue;
		UnicodeString cut = partOf( word, 0, at );
		cut.concat( u'.' );
		forms.push_back( cut );
	}
	// longest first; a vowel drop goes before a cut of the same length
	std::stable_sort( forms.begin() + 1, forms.end(),
		[]( const UnicodeString &a, const UnicodeString &b ) { return a.getLength() > b.getLength(); } );
	return forms;
}

static Bool fits( const UnicodeString &text, Int widest, TextMeasure measure, void *context )
{
	return measure( text, context ) <= widest;
}

UnicodeString abbreviate( const UnicodeString &text, const UnicodeString &shortForm, Int widest, Int language,
	TextMeasure measure, void *context, Bool words, const UnicodeString &keep )
{
	if( text.isEmpty() || fits( text, widest, measure, context ) )
		return text;
	if( words && !shortForm.isEmpty() && fits( shortForm, widest, measure, context ) )
		return shortForm;

	if( words )
	{
		// the words, each with its forms, and which of keep's words they are
		const Bool english = language == TEXT_LANGUAGE_ENGLISH;
		const Bool yVowel = language == TEXT_LANGUAGE_GERMAN || language == TEXT_LANGUAGE_FRENCH;
		std::vector< UnicodeString > kept;
		UnicodeString piece;
		for( Int at = 0; at <= keep.getLength(); at++ )
		{
			if( at == keep.getLength() || keep.getCharAt( at ) == u' ' )
			{
				if( !piece.isEmpty() )
					kept.push_back( piece );
				piece.clear();
			}
			else
				piece.concat( keep.getCharAt( at ) );
		}
		std::vector< std::vector< UnicodeString > > forms;
		for( Int at = 0; at <= text.getLength(); at++ )
		{
			if( at == text.getLength() || text.getCharAt( at ) == u' ' )
			{
				if( !piece.isEmpty() )
				{
					Bool isKept = FALSE;
					for( size_t each = 0; each < kept.size() && !isKept; each++ )
						isKept = kept[ each ].compare( piece ) == 0;
					forms.push_back( isKept ? std::vector< UnicodeString >( 1, piece ) : wordForms( piece, english, yVowel ) );
				}
				piece.clear();
			}
			else
				piece.concat( text.getCharAt( at ) );
		}
		std::vector< size_t > step( forms.size(), 0 );
		for( ;; )
		{
			UnicodeString joined;
			for( size_t each = 0; each < forms.size(); each++ )
			{
				if( each > 0 )
					joined.concat( u' ' );
				joined.concat( forms[ each ][ step[ each ] ] );
			}
			if( fits( joined, widest, measure, context ) )
				return joined;
			// the longest word that can still give, the last word only once the others cannot
			Int pick = -1;
			for( size_t each = 0; each + 1 < forms.size(); each++ )
			{
				if( step[ each ] + 1 < forms[ each ].size()
						&& ( pick < 0 || forms[ each ][ step[ each ] ].getLength() > forms[ pick ][ step[ pick ] ].getLength() ) )
					pick = (Int)each;
			}
			if( pick < 0 && !forms.empty() && step.back() + 1 < forms.back().size() )
				pick = (Int)forms.size() - 1;
			if( pick < 0 )
				break;
			step[ pick ]++;
		}
		// several words all at their shortest and still too wide: a word of four letters or more
		// gives way to its first letter and a period, the longest first and the last word last, so a
		// label stays a label ("ABD S. S. Gen.") before the ellipsis takes it
		if( forms.size() > 1 )
		{
			std::vector< UnicodeString > current( forms.size() );
			for( size_t each = 0; each < forms.size(); each++ )
				current[ each ] = forms[ each ][ step[ each ] ];
			for( ;; )
			{
				// a kept word, or one under four letters, has one form and never gives
				Int pick = -1;
				for( size_t each = 0; each + 1 < forms.size(); each++ )
				{
					if( forms[ each ].size() > 1 && current[ each ].getLength() > 2
							&& ( pick < 0 || current[ each ].getLength() > current[ pick ].getLength() ) )
						pick = (Int)each;
				}
				if( pick < 0 && forms.back().size() > 1 && current.back().getLength() > 2 )
					pick = (Int)forms.size() - 1;
				if( pick < 0 )
					break;
				UnicodeString initial;
				initial.concat( forms[ pick ][ 0 ].getCharAt( 0 ) );
				initial.concat( u'.' );
				current[ pick ] = initial;
				UnicodeString joined;
				for( size_t each = 0; each < current.size(); each++ )
				{
					if( each > 0 )
						joined.concat( u' ' );
					joined.concat( current[ each ] );
				}
				if( fits( joined, widest, measure, context ) )
					return joined;
			}
		}
	}

	// the ellipsis: as many first letters as fit, at least one
	for( Int count = text.getLength() - 1; count >= 1; count-- )
	{
		UnicodeString cut = partOf( text, 0, count );
		while( cut.getLength() > 1 && cut.getCharAt( cut.getLength() - 1 ) == u' ' )
			cut.removeLastChar();
		cut.concat( ABBREVIATION_ELLIPSIS );
		if( count == 1 || fits( cut, widest, measure, context ) )
			return cut;
	}
	return text;
}

static Int measureInFont( const UnicodeString &text, void *context )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( (GameFont *)context );
	string->setText( text );
	Int width = 0, height = 0;
	string->getSize( &width, &height );
	TheDisplayStringManager->freeDisplayString( string );
	return width;
}

UnicodeString fitText( const UnicodeString &text, GameFont *font, Int widest, const UnicodeString &shortForm, const UnicodeString &keep )
{
	return abbreviate( text, shortForm, widest, TheGlobalData->m_textLanguage, measureInFont, font, TRUE, keep );
}

UnicodeString fitKey( const char *key, GameFont *font, Int widest )
{
	AsciiString shortKey = key;
	shortKey.concat( "_Short" );
	Bool known = FALSE;
	const UnicodeString shortForm = TheGameText->fetch( shortKey, &known );
	return fitText( TheGameText->fetch( key ), font, widest, known ? shortForm : UnicodeString::TheEmptyString );
}

UnicodeString fitName( const UnicodeString &name, GameFont *font, Int widest )
{
	return abbreviate( name, UnicodeString::TheEmptyString, widest, TheGlobalData->m_textLanguage, measureInFont, font, FALSE );
}

}
