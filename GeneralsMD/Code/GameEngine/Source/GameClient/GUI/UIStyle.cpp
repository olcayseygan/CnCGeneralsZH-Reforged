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
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Image.h"
#include "GameLogic/AI.h"
#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/Online/OnlineQueues.h"
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

/// the layout points readableFont settles on: points, or the next size up whose capitals reach
/// READABLE_CAPITALS, at most twelve up
static Int readablePoints( Int points, Bool bold )
{
	// Arial's capitals stand about five eighths of its cell, so twelve pixels of capital want a cell
	// of about nineteen and a half
	Int grown = points;
	for( GameFont *found = font( grown, bold ); found && found->height * 5 < READABLE_CAPITALS * 8 && grown < points + 12; )
		found = font( ++grown, bold );
	return grown;
}

GameFont *readableFont( Int points, Bool bold )
{
	return font( readablePoints( points, bold ), bold );
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

Bool hasText( const char *key )
{
	if( TheGameText == NULL || key == NULL )
		return FALSE;
	// GameText has no quiet lookup: the labels that start with key as the string files spell it, then
	// the one that is key, as fetch compares them
	const AsciiStringVec &labels = TheGameText->getStringsWithLabelPrefix( AsciiString( key ) );
	for( size_t each = 0; each < labels.size(); each++ )
	{
		if( labels[ each ].compareNoCase( key ) == 0 )
			return TRUE;
	}
	return FALSE;
}

UnicodeString fitKey( const char *key, GameFont *font, Int widest )
{
	// most keys have no short form, and a fetch of one would log it as missing text
	AsciiString shortKey = key;
	shortKey.concat( "_Short" );
	const UnicodeString shortForm = hasText( shortKey.str() ) ? TheGameText->fetch( shortKey ) : UnicodeString::TheEmptyString;
	return fitText( TheGameText->fetch( key ), font, widest, shortForm );
}

UnicodeString fitName( const UnicodeString &name, GameFont *font, Int widest )
{
	return abbreviate( name, UnicodeString::TheEmptyString, widest, TheGlobalData->m_textLanguage, measureInFont, font, FALSE );
}

//-------------------------------------------------------------------------------------------------
// The boxes inside the online pages' panels
//-------------------------------------------------------------------------------------------------
GameFont *sectionFont( void )
{
	// the rooms' "Players" and "Map Preview" measure 25 pixels at 1080, TEXT_STRONG's size: two points
	// over the body.  The body is readableFont's, grown at 720 to keep its capitals readable, so the
	// heading keeps its two points over whatever the body grew to; on font( TEXT_STRONG ) alone it
	// came out at 14 pixels under a 17 pixel body
	return font( readablePoints( TEXT_BODY, FALSE ) + TEXT_STRONG - TEXT_BODY );
}

void heading( DisplayString *words, Int x, Int y )
{
	words->draw( x, y, ACTIVE, SHADOW );
}

void fact( DisplayString *label, DisplayString *value, Int x, Int y, Int width, Int height, Color valueColor )
{
	Int w, h;
	label->getSize( &w, &h );
	label->draw( x, y + ( height - h ) / 2, MUTED, SHADOW );
	value->getSize( &w, &h );
	value->draw( x + width - w, y + ( height - h ) / 2, valueColor, SHADOW );
	TheDisplay->drawFillRect( x, y + height - 1, width, 1, GRID );
}

void track( Int x, Int y, Int width, Int height, Real fraction, Color fill )
{
	TheDisplay->drawFillRect( x, y, width, height, GRID );
	const Int filled = REAL_TO_INT( width * max( 0.0f, min( 1.0f, fraction ) ) + 0.5f );
	if( filled > 0 )
		TheDisplay->drawFillRect( x, y, filled, height, fill );
}

GameFont *chipFont( void )
{
	// the spec's 16 pixels at 1080; readableFont grew it to the body's 21 and a Turkish
	// "Mağlubiyet" ran into the next column
	return font( TEXT_SMALL, TRUE );
}

Int chip( DisplayString *label, Int x, Int y, Color color, Color edge )
{
	// the load screen's TIP: eight pixels either side of the label, five over and four under at 1080
	Int w, h;
	label->getSize( &w, &h );
	const Int side = tallPx( 8.0f );
	const Int over = tallPx( 5.0f );
	const Int width = w + 2 * side;
	TheDisplay->drawOpenRect( x, y, width, h + over + tallPx( 4.0f ), 1.0f, edge );
	label->draw( x + side, y + over, color, SHADOW );
	return width;
}

//-------------------------------------------------------------------------------------------------
// Ratings
//-------------------------------------------------------------------------------------------------
// website/app/Models/Player.php TIERS, the rating each tier starts at; private's 0 is only the
// floor of its track, as a rating can fall under it
static const Real TIER_START[ TIER_COUNT ] = { 0.0f, 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f };
// the same on the ladder's Elo (TIERS); private has no floor there, its track runs from the site's
// starting Elo (app/Online/Elo.php START), so it shows empty as the site's meter does
static const Real ELO_TIER_START[ TIER_COUNT ] = { 1000.0f, 1000.0f, 1000.0f, 1075.0f, 1150.0f, 1250.0f, 1350.0f, 1500.0f };
static const char *const TIER_KEY[ TIER_COUNT ] = { "unranked", "private", "sergeant", "lieutenant", "captain", "major", "colonel", "general" };
// EA's rank table below, by name: RANK_NAME[ 7 ] is the portrait called General
static const Int TIER_RANK[ TIER_COUNT ] = { -1, 0, 2, 3, 4, 5, 6, 7 };

RatingTier ratingTier( Real rating, Int games, Bool elo )
{
	if( games < RANKED_GAMES )
		return TIER_UNRANKED;
	const Real *start = elo ? ELO_TIER_START : TIER_START;
	for( Int tier = TIER_GENERAL; tier > TIER_PRIVATE; tier-- )
	{
		if( rating >= start[ tier ] )
			return (RatingTier)tier;
	}
	return TIER_PRIVATE;
}

RatingTier aiRatingTier( Real elo, Int games )
{
	return ratingTier( aiRatingToLegacy( elo ), games );
}

Real aiTierStart( RatingTier tier )
{
	return aiRatingOfLegacy( tierStart( tier ) );
}

RatingTier playerTier( const OnlinePlayerInfo &info )
{
	for( Int tier = 0; tier < TIER_COUNT; tier++ )
	{
		if( info.tier.compareNoCase( TIER_KEY[ tier ] ) == 0 )
			return (RatingTier)tier;
	}
	return ratingTier( info.shown(), info.hasRating ? info.games : -1, info.hasElo );
}

Real tierStart( RatingTier tier, Bool elo )
{
	if( tier < 0 || tier >= TIER_COUNT )
		return elo ? ELO_TIER_START[ TIER_UNRANKED ] : 0.0f;
	return elo ? ELO_TIER_START[ tier ] : TIER_START[ tier ];
}

const char *tierKey( RatingTier tier )
{
	return tier >= 0 && tier < TIER_COUNT ? TIER_KEY[ tier ] : TIER_KEY[ TIER_UNRANKED ];
}

Int tierRank( RatingTier tier )
{
	return tier >= 0 && tier < TIER_COUNT ? TIER_RANK[ tier ] : -1;
}

// EA's names for the rank portraits, by rank (moved from PopupPlayerInfo.cpp); its seventh and
// eighth are named the other way round from RankPointValue.h's RANK_BRIGADIER_GENERAL and RANK_GENERAL
static const char *const RANK_NAME[] = { "Private", "Corporal", "Sergeant", "Lieutenant", "Captain", "Major",
	"Colonel", "General", "Brigadier", "Commander" };

AsciiString rankImageName( const AsciiString &side, Int rank )
{
	if( side.isEmpty() )
		return AsciiString( "NewPlayer" );
	if( rank < 0 || rank >= (Int)( sizeof( RANK_NAME ) / sizeof( RANK_NAME[ 0 ] ) ) )
		return AsciiString::TheEmptyString;

	// the artists' names: a side after an underscore, the random army's Elite after none
	const char *suffix = "Elite";
	if( side == "USA" || side == "America" )
		suffix = "_USA";
	else if( side == "China" )
		suffix = "_China";
	else if( side == "GLA" )
		suffix = "_GLA";
	AsciiString name;
	name.format( "Rank_%s%s", RANK_NAME[ rank ], suffix );
	if( name == "Rank_PrivateElite" )
		name = "Rank";
	return name;
}

const Image *rankImage( const AsciiString &side, Int rank )
{
	const AsciiString name = rankImageName( side, rank );
	if( name.isEmpty() )
		return NULL;
	const Image *image = TheMappedImageCollection->findImageByName( name );
	DEBUG_ASSERTCRASH( image, ( "Could not load rank image: %s", name.str() ) );
	return image;
}

const Image *tierImage( RatingTier tier, const AsciiString &side )
{
	const Int rank = tierRank( tier );
	return rank < 0 ? NULL : rankImage( side.isEmpty() ? AsciiString( "Random" ) : side, rank );
}

const Image *armyPortrait( const PlayerTemplate *army )
{
	if( army == NULL )
		return NULL;
	const GeneralPersona *persona = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName( army->getName() ) : NULL;
	if( persona && persona->getBioPortraitLarge() )
		return persona->getBioPortraitLarge();
	const AsciiString hqName = army->getStartingBuilding();
	const ThingTemplate *hq = TheThingFactory && hqName.isNotEmpty() ? TheThingFactory->findTemplate( hqName, FALSE ) : NULL;
	if( hq && hq->getSelectedPortraitImage() )
		return hq->getSelectedPortraitImage();
	if( hq && hq->getButtonImage() )
		return hq->getButtonImage();
	return army->getGeneralImage() ? army->getGeneralImage() : army->getSideIconImage();
}

void coverImage( const Image *image, Int x, Int y, Int width, Int height, const IRegion2D *within )
{
	if( image == NULL || width <= 0 || height <= 0 || image->getImageWidth() <= 0 || image->getImageHeight() <= 0 )
		return;
	const Real scale = max( (Real)width / image->getImageWidth(), (Real)height / image->getImageHeight() );
	const Int drawnWidth = max( width, REAL_TO_INT( image->getImageWidth() * scale + 0.5f ) );
	const Int drawnHeight = max( height, REAL_TO_INT( image->getImageHeight() * scale + 0.5f ) );
	const Int left = x - ( drawnWidth - width ) / 2;
	const Int top = y - ( drawnHeight - height ) / 4;
	IRegion2D box;
	box.lo.x = x;
	box.lo.y = y;
	box.hi.x = x + width;
	box.hi.y = y + height;
	if( within )
	{
		box.lo.x = max( box.lo.x, within->lo.x );
		box.lo.y = max( box.lo.y, within->lo.y );
		box.hi.x = min( box.hi.x, within->hi.x );
		box.hi.y = min( box.hi.y, within->hi.y );
	}
	if( box.hi.x > box.lo.x && box.hi.y > box.lo.y )
	{
		TheDisplay->setClipRegion( &box );
		TheDisplay->drawImage( image, left, top, left + drawnWidth, top + drawnHeight );
	}
	if( within )
		TheDisplay->setClipRegion( const_cast<IRegion2D *>( within ) );
	else
		TheDisplay->enableClipping( FALSE );
}

/// the languages that write a decimal comma; every one the game has but English
static Bool decimalComma( Int language )
{
	return language != TEXT_LANGUAGE_ENGLISH;
}

UnicodeString ratingTextIn( Real rating, Bool sign, Int language )
{
	// in whole hundredths, so a rounding never shows -0.00 and no C library's locale decides the point
	const Int hundredths = (Int)floor( rating * 100.0 + 0.5 );
	const Int whole = hundredths < 0 ? -hundredths : hundredths;
	AsciiString text;
	text.format( "%s%d%c%02d", hundredths < 0 ? "-" : ( sign && hundredths > 0 ? "+" : "" ), whole / 100,
		decimalComma( language ) ? ',' : '.', whole % 100 );
	UnicodeString wide;
	wide.translate( text );
	return wide;
}

UnicodeString ratingText( Real rating, Bool sign )
{
	return ratingTextIn( rating, sign, TheGlobalData->m_textLanguage );
}

UnicodeString eloText( Real elo, Bool sign )
{
	const Int whole = (Int)floor( elo + 0.5 );
	AsciiString text;
	text.format( sign && whole > 0 ? "+%d" : "%d", whole );
	UnicodeString wide;
	wide.translate( text );
	return wide;
}

UnicodeString ladderTextIn( Real value, Bool elo, Bool sign, Int language )
{
	return elo ? eloText( value, sign ) : ratingTextIn( value, sign, language );
}

UnicodeString ladderText( Real value, Bool elo, Bool sign )
{
	return elo ? eloText( value, sign ) : ratingText( value, sign );
}

Int percent( Int part, Int whole )
{
	return whole > 0 ? ( part * 200 + whole ) / ( whole * 2 ) : 0;
}

UnicodeString percentTextIn( Int value, Int language )
{
	UnicodeString text;
	if( language == TEXT_LANGUAGE_TURKISH )
		text.format( u"%%%d", value );
	else if( language == TEXT_LANGUAGE_ENGLISH )
		text.format( u"%d%%", value );
	else
		text.format( u"%d %%", value );
	return text;
}

UnicodeString percentText( Int value )
{
	return percentTextIn( value, TheGlobalData->m_textLanguage );
}

Color changeColor( Real change )
{
	if( change > 0.0f )
		return GOLD;
	return change < 0.0f ? MUTED : INK;
}

//-------------------------------------------------------------------------------------------------
// The online pages' frame
//-------------------------------------------------------------------------------------------------
Int tallPx( Real at1080 )
{
	return REAL_TO_INT( at1080 * TheDisplay->getHeight() / 1080.0f + 0.5f );
}

void onlineColumns( IRegion2D &main, IRegion2D &side )
{
	side.hi.x = boardX( ROOM_INSET_RIGHT );
	side.lo.x = side.hi.x - tallPx( ONLINE_SIDE_WIDTH );
	main.lo.x = boardX( ROOM_INSET );
	main.hi.x = side.lo.x - tallPx( ONLINE_COLUMN_GAP );
	main.lo.y = side.lo.y = boardY( ONLINE_CONTENT_TOP );
	main.hi.y = side.hi.y = boardY( ONLINE_CONTENT_BOTTOM );
}

const Image *pingImage( Int ms )
{
	if( ms < 0 )
		return NULL;
	// LobbyPingImage's rule; its images are found only by the lobby's own init.  300 and 600 are
	// GSConfig's defaults, for a client that has no config yet
	const Int good = TheGameSpyConfig ? TheGameSpyConfig->getPingCutoffGood() : 300;
	const Int bad = TheGameSpyConfig ? TheGameSpyConfig->getPingCutoffBad() : 600;
	return TheMappedImageCollection->findImageByName( ms < good ? "Ping03" : ( ms < bad ? "Ping02" : "Ping01" ) );
}

/// the server's own words, which the site may write in UTF-8 (a city with an umlaut)
static UnicodeString fromUtf8( const AsciiString &bytes )
{
	WideChar text[ 128 ];
	WideCharFromUtf8( bytes.str(), text, sizeof( text ) / sizeof( text[ 0 ] ) );
	return UnicodeString( text );
}

/// the server line in its two halves: the name and city joined ("<name> · <city>"), and the round
/// trip ("38 ms"), either empty while unknown.  A welcome with no server object still has a round trip.
static void serverLine( UnicodeString &place, UnicodeString &ping, Int &ms )
{
	AsciiString name, city;
	ms = -1;
	place.clear();
	ping.clear();
	if( !OnlineGetServerInfo( name, city, ms ) )
		name = city = AsciiString::TheEmptyString;
	place = fromUtf8( name );
	if( city.isNotEmpty() )
	{
		if( !place.isEmpty() )
			place.concat( HEADER_JOINER );
		place.concat( fromUtf8( city ) );
	}
	if( ms >= 0 )
		ping.format( u"%d ms", ms );
}

UnicodeString onlineServerLine( void )
{
	UnicodeString place, ping;
	Int ms;
	serverLine( place, ping, ms );
	if( !place.isEmpty() && !ping.isEmpty() )
		place.concat( HEADER_JOINER );
	place.concat( ping );
	return place;
}

static const char *const TAB_WINDOW[ ONLINE_TAB_COUNT ] = { "TabHome", "TabQuickMatch", "TabCustom", "TabLadder", "TabSkins" };
// Home is the hub at the stack's foot, reached by popping
static const char *const TAB_SCREEN[ ONLINE_TAB_COUNT ] = { NULL, "Menus/WOLQuickMatchMenu.wnd", "Menus/WOLCustomLobby.wnd",
	"Menus/WOLLadderScreen.wnd", "Menus/WOLLadderScreen.wnd" };

/// the header's words, made by onlinePageInit and freed by onlinePageShutdown
enum HeaderText
{
	HEADER_TITLE,
	HEADER_SUBTITLE,
	HEADER_MARK,				///< "Zero Hour"
	HEADER_MARK_BLUE,		///< " Reforged"
	HEADER_NICK,
	HEADER_TIER,
	HEADER_LADDER,
	HEADER_LADDER_VALUE,
	HEADER_AI,
	HEADER_AI_VALUE,
	HEADER_PING,				///< the server line's "38 ms", after its light
	HEADER_TEXT_COUNT
};

static GameWindow *s_pageParent = NULL;
static GameWindow *s_tabs[ ONLINE_TAB_COUNT ];
static GameWindow *s_masterTitle = NULL;	///< the master's StaticTextTitle, when it carries the title
static OnlineTab s_openTab = ONLINE_TAB_NONE;
static OnlineTab s_askedTab = ONLINE_TAB_NONE;
static DisplayString *s_header[ HEADER_TEXT_COUNT ];
static UnicodeString s_titleShown;			///< the title as last asked for
static UnicodeString s_subtitleShown;		///< the subtitle's words, its ping and its room as last fitted
static UnicodeString s_pingShown;
static Int s_subtitleRoom = -1;
static UnsignedInt s_lineSerial = 0;		///< the player's card serial the line was last laid out for
static Bool s_lineLaidOut = FALSE;

/// a word the strings may not have yet: the key's, else the English
static UnicodeString wordOr( const char *key, const WideChar *english )
{
	return hasText( key ) ? TheGameText->fetch( key ) : UnicodeString( english );
}

static void freeHeader( void )
{
	for( Int each = 0; each < HEADER_TEXT_COUNT; each++ )
	{
		if( s_header[ each ] )
			TheDisplayStringManager->freeDisplayString( s_header[ each ] );
		s_header[ each ] = NULL;
	}
}

/// a tab: the score screen's (UIStyle::tab), the open one's label ACTIVE and never lit by the mouse
static void drawOnlineTab( GameWindow *window, WinInstanceData *instData )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Bool open = s_openTab != ONLINE_TAB_NONE && window == s_tabs[ s_openTab ];
	const Color color = tab( x, y, width, height, BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ), open,
		!open && BitTest( instData->getState(), WIN_STATE_HILITED ) );
	DisplayString *text = instData->getTextDisplayString();
	if( text->getFont() != window->winGetFont() )
		text->setFont( window->winGetFont() );
	Int textWidth, textHeight;
	text->getSize( &textWidth, &textHeight );
	text->draw( x + ( width - textWidth ) / 2, y + ( height - textHeight ) / 2, color, SHADOW );
}

void onlinePageInit( GameWindow *parent, const char *layout, OnlineTab open )
{
	freeHeader();
	s_pageParent = parent;
	s_openTab = open;
	s_askedTab = ONLINE_TAB_NONE;
	s_titleShown.clear();
	s_subtitleShown.clear();
	s_pingShown.clear();
	s_subtitleRoom = -1;
	s_lineLaidOut = FALSE;
	s_masterTitle = NULL;
	if( parent )
	{
		AsciiString name;
		name.format( "%s:StaticTextTitle", layout );
		s_masterTitle = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
	}

	GameFont *body = readableFont( TEXT_BODY );
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
	{
		s_tabs[ each ] = NULL;
		if( open == ONLINE_TAB_NONE || parent == NULL )
			continue;
		AsciiString name;
		name.format( "%s:%s", layout, TAB_WINDOW[ each ] );
		s_tabs[ each ] = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
	}

	// One face for all five labels, in capitals: the body's, or the largest under it at which every
	// label reads whole with a pad either side.  The boxes shrink with the fitted board (0.75 at
	// 1440x1080, 0.667 at 720) and the body does not, so at the body's size they were cut ("QUICK M…").
	const Int pad = max( 4, px( 10.0f ) );
	GameFont *labelFont = NULL;
	for( Int points = readablePoints( TEXT_BODY, FALSE ); points >= 4 && labelFont == NULL; points-- )
	{
		GameFont *candidate = font( points );
		Bool whole = candidate != NULL;
		for( Int each = 0; each < ONLINE_TAB_COUNT && whole; each++ )
		{
			if( s_tabs[ each ] == NULL )
				continue;
			Int width, height;
			s_tabs[ each ]->winGetSize( &width, &height );
			whole = measureInFont( capitals( s_tabs[ each ]->winGetText() ), candidate ) <= width - 2 * pad
				&& candidate->height <= height;
		}
		if( whole )
			labelFont = candidate;
	}
	if( labelFont == NULL )
		labelFont = font( 4 );
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
	{
		GameWindow *window = s_tabs[ each ];
		if( window == NULL )
			continue;
		Int width, height;
		window->winGetSize( &width, &height );
		window->winSetFont( labelFont );
		GadgetButtonSetText( window, fitText( capitals( window->winGetText() ), labelFont, width - 2 * pad ) );
		window->winSetDrawFunc( drawOnlineTab );
	}

	const Int fonts[ HEADER_TEXT_COUNT ] = { 0, 1, 2, 2, 3, 4, 4, 4, 4, 4, 1 };
	GameFont *faces[] = { titleFont( TEXT_TITLE ), subtitleFont(), font( TEXT_STRONG, TRUE ), readableFont( TEXT_BODY, TRUE ), body };
	for( Int each = 0; each < HEADER_TEXT_COUNT; each++ )
	{
		s_header[ each ] = TheDisplayStringManager->newDisplayString();
		s_header[ each ]->setFont( faces[ fonts[ each ] ] );
	}
	s_header[ HEADER_MARK ]->setText( UnicodeString( u"Zero Hour" ) );
	s_header[ HEADER_MARK_BLUE ]->setText( UnicodeString( u" Reforged" ) );
	s_header[ HEADER_LADDER ]->setText( wordOr( "GUI:OnlineLadderShort", u"ELO" ) );
	s_header[ HEADER_AI ]->setText( wordOr( "GUI:OnlineAIShort", u"Bot ELO" ) );
}

void onlinePageShutdown( GameWindow *parent )
{
	// the hub's shutdown can finish after the page it pushed has made its own
	if( parent != s_pageParent )
		return;
	freeHeader();
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
		s_tabs[ each ] = NULL;
	s_masterTitle = NULL;
	s_pageParent = NULL;
	s_openTab = ONLINE_TAB_NONE;
}

/** The player's line, right to left from right: the AI rating, the ladder rating, the tier, the nick
	* in bold, the nick cut with an ellipsis where the tabs leave too little room. */
static void drawPlayerLine( Int right )
{
	Int left = -1;
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
	{
		if( s_tabs[ each ] == NULL )
			continue;
		Int x, y, width, height;
		s_tabs[ each ]->winGetScreenPosition( &x, &y );
		s_tabs[ each ]->winGetSize( &width, &height );
		left = max( left, x + width + px( ONLINE_COLUMN_GAP ) );
	}
	if( left < 0 || !OnlineIsEnabled() || TheGameSpyInfo == NULL )
		return;
	const AsciiString nick = TheGameSpyInfo->getLocalName();
	if( nick.isEmpty() )
		return;

	OnlinePlayerInfo info;
	OnlineGetPlayerInfo( nick, info );
	const Int gap = px( 12.0f );
	const Int space = px( 6.0f );
	DisplayString **t = s_header;
	if( !s_lineLaidOut || info.serial != s_lineSerial )
	{
		s_lineLaidOut = TRUE;
		s_lineSerial = info.serial;
		const UnicodeString none( u"\x2013" );		// an en dash for a number nobody sent
		t[ HEADER_TIER ]->setText( OnlineTierName( tierKey( playerTier( info ) ) ) );
		t[ HEADER_LADDER_VALUE ]->setText( info.hasRating ? ladderText( info.shown(), info.hasElo ) : none );
		t[ HEADER_AI_VALUE ]->setText( info.hasAIRating ? eloText( info.aiRating ) : none );
		Int rest = 3 * gap + 2 * space;
		for( Int each = HEADER_TIER; each <= HEADER_AI_VALUE; each++ )
		{
			Int w, h;
			t[ each ]->getSize( &w, &h );
			rest += w;
		}
		UnicodeString shown;
		shown.translate( nick );
		t[ HEADER_NICK ]->setText( fitName( shown, t[ HEADER_NICK ]->getFont(), right - left - rest ) );
	}

	const Int top = boardY( ONLINE_TABS_TOP );
	const Int rowHeight = boardY( ONLINE_TABS_BOTTOM ) - top;
	const Int order[] = { HEADER_AI_VALUE, HEADER_AI, HEADER_LADDER_VALUE, HEADER_LADDER, HEADER_TIER, HEADER_NICK };
	const Color colors[] = { INK, MUTED, INK, MUTED, MUTED, INK };
	Int x = right;
	for( Int each = 0; each < 6; each++ )
	{
		Int w, h;
		t[ order[ each ] ]->getSize( &w, &h );
		x -= w;
		t[ order[ each ] ]->draw( x, top + ( rowHeight - h ) / 2, colors[ each ], SHADOW );
		x -= ( order[ each ] == HEADER_AI || order[ each ] == HEADER_LADDER || order[ each ] == HEADER_TIER ) ? gap : space;
	}
}

/** The subtitle from left, MUTED, centred on middle and kept short of limit: the page's words, or
	* with none the server line, its ping light between the place and the milliseconds. */
static void drawSubtitle( const UnicodeString &subtitle, Int left, Int limit, Int middle )
{
	DisplayString **t = s_header;
	UnicodeString words = subtitle, ping;
	Int ms = -1;
	if( subtitle.isEmpty() )
		serverLine( words, ping, ms );
	const Image *light = ping.isEmpty() ? NULL : pingImage( ms );
	const Int lightSize = px( 16.0f );
	const Int gap = px( 10.0f );
	const Int room = limit - left;
	if( room != s_subtitleRoom || words.compare( s_subtitleShown ) != 0 || ping.compare( s_pingShown ) != 0 )
	{
		s_subtitleRoom = room;
		s_subtitleShown = words;
		s_pingShown = ping;
		t[ HEADER_PING ]->setText( ping );
		Int pingWidth = 0, pingHeight = 0;
		if( !ping.isEmpty() )
			t[ HEADER_PING ]->getSize( &pingWidth, &pingHeight );
		const Int wordsRoom = room - ( ping.isEmpty() ? 0 : pingWidth + gap + ( light ? lightSize + gap : 0 ) );
		t[ HEADER_SUBTITLE ]->setText( wordsRoom > 0 ? fitName( words, t[ HEADER_SUBTITLE ]->getFont(), wordsRoom ) : UnicodeString::TheEmptyString );
	}

	Int x = left;
	const Int top = middle - t[ HEADER_SUBTITLE ]->getFont()->height / 2;
	if( t[ HEADER_SUBTITLE ]->getTextLength() > 0 )
	{
		Int w, h;
		t[ HEADER_SUBTITLE ]->getSize( &w, &h );
		t[ HEADER_SUBTITLE ]->draw( x, top, MUTED, SHADOW );
		x += w + gap;
	}
	if( ping.isEmpty() )
		return;
	if( light )
	{
		TheDisplay->drawImage( light, x, middle - lightSize / 2, x + lightSize, middle - lightSize / 2 + lightSize );
		x += lightSize + gap;
	}
	t[ HEADER_PING ]->draw( x, top, MUTED, SHADOW );
}

void onlineHeader( const UnicodeString &title, const UnicodeString &subtitle )
{
	DisplayString **t = s_header;
	if( t[ HEADER_TITLE ] == NULL )
		return;		// no page, or between a page's shutdown and the next one's init

	// the wordmark as the score screen sets it, its right edge on the content's
	const Int right = boardX( ROOM_INSET_RIGHT );
	const Int middle = boardY( ROOM_TITLE_MIDDLE );
	Int first, second, markHeight;
	t[ HEADER_MARK ]->getSize( &first, &markHeight );
	t[ HEADER_MARK_BLUE ]->getSize( &second, &markHeight );
	wordmark( &t[ HEADER_MARK ], right, middle - markHeight / 2 );

	// where the subtitle starts: after our title, or after the master's title's text
	Int after = -1;
	if( !title.isEmpty() )
	{
		const Int x = boardX( ROOM_TITLE_LEFT );
		if( title.compare( s_titleShown ) != 0 )
		{
			s_titleShown = title;
			t[ HEADER_TITLE ]->setText( capitals( title ) );
		}
		header( t[ HEADER_TITLE ], NULL, x, middle );
		rule( boardX( ROOM_PANEL_LEFT ), boardY( ROOM_HEADER_LINE ), boardX( ROOM_PANEL_RIGHT ) - boardX( ROOM_PANEL_LEFT ) );
		Int titleWidth, titleHeight;
		t[ HEADER_TITLE ]->getSize( &titleWidth, &titleHeight );
		after = x + titleWidth + headerGap();
	}
	else if( s_masterTitle && !s_masterTitle->winIsHidden() && BitTest( s_masterTitle->winGetStyle(), GWS_STATIC_TEXT ) )
	{
		// placed as W3DStaticText draws it: centred, or from its left margin
		const TextData *data = (const TextData *)s_masterTitle->winGetUserData();
		Int x, y, width, height, textWidth = 0, textHeight = 0;
		s_masterTitle->winGetScreenPosition( &x, &y );
		s_masterTitle->winGetSize( &width, &height );
		if( data && data->text )
			data->text->getSize( &textWidth, &textHeight );
		after = ( data && data->centered ? x + width / 2 + textWidth / 2 : x + ( data ? data->leftMargin : 0 ) + textWidth ) + headerGap();
	}
	if( after >= 0 )
		drawSubtitle( subtitle, after, right - first - second - headerGap(), middle );

	drawPlayerLine( right );
}

Bool onlineTabClicked( GameWindow *control, OnlineLeave leave )
{
	OnlineTab clicked = ONLINE_TAB_NONE;
	for( Int each = 0; each < ONLINE_TAB_COUNT && clicked == ONLINE_TAB_NONE; each++ )
	{
		if( control != NULL && control == s_tabs[ each ] )
			clicked = (OnlineTab)each;
	}
	if( clicked == ONLINE_TAB_NONE )
		return FALSE;
	if( clicked != s_openTab && leave )
	{
		s_askedTab = clicked;
		leave( clicked );
	}
	return TRUE;
}

const char *onlineTabScreen( OnlineTab tab )
{
	return tab >= 0 && tab < ONLINE_TAB_COUNT ? TAB_SCREEN[ tab ] : NULL;
}

OnlineTab onlineTabAsked( void )
{
	return s_askedTab;
}

}
