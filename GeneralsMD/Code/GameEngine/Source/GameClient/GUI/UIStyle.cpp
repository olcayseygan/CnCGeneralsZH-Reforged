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
#include "GameClient/GameClient.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"

#include <map>
#include <string>
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "Common/QuickmatchPreferences.h"
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
static const Real ELO_TIER_START[ TIER_COUNT ] = { 1000.0f, 1000.0f, 1000.0f, 1200.0f, 1400.0f, 1600.0f, 1800.0f, 2000.0f };
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
	side.hi.x = boardX( (Real)ONLINE_RIGHT );
	side.lo.x = side.hi.x - tallPx( ONLINE_SIDE_WIDTH );
	main.lo.x = boardX( (Real)ONLINE_LEFT );
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
	HEADER_WELL,				///< the "?" in an unranked player's portrait well
	HEADER_PLACE,				///< the server line's "EU1 · Nuremberg"
	HEADER_NOTICE_CHIP,
	HEADER_NOTICE,
	HEADER_TEXT_COUNT
};

static GameWindow *s_pageParent = NULL;
static GameWindow *s_tabs[ ONLINE_TAB_COUNT ];
static GameWindow *s_masterTitle = NULL;	///< the master's StaticTextTitle, hidden: its words are the title
static OnlineTab s_openTab = ONLINE_TAB_NONE;
static OnlineTab s_askedTab = ONLINE_TAB_NONE;
static DisplayString *s_header[ HEADER_TEXT_COUNT ];
static UnicodeString s_titleShown;			///< the title as last asked for
static UnicodeString s_subtitleShown;		///< the subtitle as last asked for
static UnicodeString s_pingShown;
static UnsignedInt s_lineSerial = 0;		///< the player's card serial the line was last laid out for
static Bool s_lineLaidOut = FALSE;
static Int s_lineRoom = -1;							///< the room the nick was fitted to
static const Image *s_linePortrait = NULL;
static Int s_placeRoom = -1;						///< the room the server's name was fitted to
static UnicodeString s_placeShown;
static Int s_friendsKeyWidth = 0;				///< the friends key's width at 1080, 0 for none
static IRegion2D s_friendsKeyRect;			///< where the last header stood it, empty when it did not
static Bool s_noticeShown = FALSE;			///< the notice's words are laid out for s_noticeSerial and s_noticeRoom
static UnsignedInt s_noticeSerial = 0;
static Int s_noticeRoom = -1;

// the menu language's own, further down
static IRegion2D windowRegion( GameWindow *window );
static void fill( const IRegion2D &r, Color color );
static void outline( const IRegion2D &r, Color color );
static Color black( Int alpha );
static Int menuPx( Real at1080 );
static void drawCentred( DisplayString *words, GameFont *font, const IRegion2D &box, Color color );

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
	s_friendsKeyRect.lo.x = s_friendsKeyRect.hi.x = 0;		// no head, no key
}

/// a tab in the menu language: the open one green and lit, the hovered one cyan
static void drawOnlineTab( GameWindow *window, WinInstanceData *instData )
{
	const Bool open = s_openTab != ONLINE_TAB_NONE && window == s_tabs[ s_openTab ];
	menuTab( windowRegion( window ), instData->getTextDisplayString(), open,
		!open && BitTest( instData->getState(), WIN_STATE_HILITED ) );
}

static Int textWidth( DisplayString *words )
{
	Int w, h;
	words->getSize( &w, &h );
	return w;
}

Bool onlinePageShowing( void )
{
	return s_header[ HEADER_TITLE ] != NULL;
}

IRegion2D onlinePanel( void )
{
	if( s_openTab == ONLINE_TAB_NONE )
		return boardRect( ONLINE_LOGIN_LEFT, ONLINE_LOGIN_TOP, ONLINE_LOGIN_RIGHT, ONLINE_LOGIN_BOTTOM );
	return boardRect( MENU_PANEL_LEFT, MENU_PANEL_TOP, MENU_PANEL_RIGHT, MENU_PANEL_BOTTOM );
}

void onlineBox( const IRegion2D &box )
{
	fill( box, black( 70 ) );
	outline( box, FRAME );
}

Int onlineSection( DisplayString *words, Int left, Int right, Int top )
{
	GameFont *font = menuFont( MENU_TEXT_LABEL, TRUE );
	if( words && words->getFont() != font )
		words->setFont( font );
	if( words && words->getTextLength() )
		words->draw( left, top, ACTIVE, black( 160 ) );
	litLine( left, top + menuPx( 24.0f ), right - left, 1, FRAME );
	return top + menuPx( 26.0f );
}

Int onlineChip( DisplayString *label, Int x, Int middle, Bool lit )
{
	GameFont *font = menuFont( MENU_TEXT_LABEL, TRUE );
	if( label->getFont() != font )
		label->setFont( font );
	Int w, h;
	label->getSize( &w, &h );
	IRegion2D r;
	r.lo.x = x;
	r.hi.x = x + w + menuPx( 18.0f );
	r.lo.y = middle - menuPx( 11.0f );
	r.hi.y = middle + menuPx( 11.0f );
	fill( r, lit ? withAlpha( EDGE, 60 ) : black( 90 ) );
	outline( r, lit ? EDGE : FRAME );
	label->draw( x + menuPx( 9.0f ), middle - h / 2, lit ? INK : MUTED, black( 160 ) );
	return r.hi.x - r.lo.x;
}

/// the side whose rank portrait the header shows: the army last picked on the Quick Match page, Random (the
/// Elite portrait) for none
static AsciiString quickMatchSide( void )
{
	QuickMatchPreferences pref;
	const Int pick = pref.getSide();
	const PlayerTemplate *army = pick >= 0 && ThePlayerTemplateStore && pick < ThePlayerTemplateStore->getPlayerTemplateCount()
		? ThePlayerTemplateStore->getNthPlayerTemplate( pick ) : NULL;
	return army && army->getBaseSide().isNotEmpty() ? army->getBaseSide() : AsciiString( "Random" );
}

/** The player's line on the title row, right to left from right: the Bot ELO and the ELO, each a label
	* before its figure, 32 apart; the tier; the nick in gold, cut with an ellipsis where it reaches
	* leftLimit; the tier's rank portrait, 40 square, or a "?" well while unranked.  Returns its left edge. */
static Int drawPlayerRow( Int right, Int middle, Int leftLimit )
{
	if( !OnlineIsEnabled() || TheGameSpyInfo == NULL )
		return right;
	const AsciiString nick = TheGameSpyInfo->getLocalName();
	if( nick.isEmpty() )
		return right;
	OnlinePlayerInfo info;
	OnlineGetPlayerInfo( nick, info );
	DisplayString **t = s_header;
	const Int figureGap = menuPx( 10.0f ), group = menuPx( 32.0f ), tierGap = menuPx( 14.0f ), nickGap = menuPx( 12.0f );
	const Int portrait = menuPx( 40.0f );
	const Int room = right - leftLimit;
	if( !s_lineLaidOut || info.serial != s_lineSerial || room != s_lineRoom )
	{
		s_lineLaidOut = TRUE;
		s_lineSerial = info.serial;
		s_lineRoom = room;
		const UnicodeString none( u"\x2013" );		// an en dash for a number nobody sent
		const RatingTier tier = playerTier( info );
		s_linePortrait = tierImage( tier, quickMatchSide() );
		t[ HEADER_TIER ]->setText( OnlineTierName( tierKey( tier ) ) );
		t[ HEADER_LADDER_VALUE ]->setText( info.hasRating ? ladderText( info.shown(), info.hasElo ) : none );
		t[ HEADER_AI_VALUE ]->setText( info.hasAIRating ? eloText( info.aiRating ) : none );
		Int rest = 2 * figureGap + 2 * group + tierGap + nickGap + portrait;
		for( Int each = HEADER_TIER; each <= HEADER_AI_VALUE; each++ )
			rest += textWidth( t[ each ] );
		UnicodeString shown;
		shown.translate( nick );
		t[ HEADER_NICK ]->setText( fitName( shown, t[ HEADER_NICK ]->getFont(), max( 0, room - rest ) ) );
	}
	const Int order[] = { HEADER_AI_VALUE, HEADER_AI, HEADER_LADDER_VALUE, HEADER_LADDER, HEADER_TIER, HEADER_NICK };
	const Color colors[] = { INK, MUTED, INK, MUTED, MUTED, GOLD };
	const Int gaps[] = { figureGap, group, figureGap, group, tierGap, nickGap };
	Int x = right;
	for( Int each = 0; each < 6; each++ )
	{
		DisplayString *s = t[ order[ each ] ];
		Int w, h;
		s->getSize( &w, &h );
		x -= w;
		s->draw( x, middle - h / 2, colors[ each ], black( 160 ) );
		x -= gaps[ each ];
	}
	IRegion2D well;
	well.lo.x = x - portrait;
	well.lo.y = middle - portrait / 2;
	well.hi.x = x;
	well.hi.y = well.lo.y + portrait;
	if( s_linePortrait )
		TheDisplay->drawImage( s_linePortrait, well.lo.x, well.lo.y, well.hi.x, well.hi.y );
	else
	{
		fill( well, GameMakeColor( 10, 12, 20, 230 ) );
		outline( well, withAlpha( FRAME, 200 ) );
		drawCentred( t[ HEADER_WELL ], menuFont( MENU_TEXT_ROW_FIGURE ), well, MUTED );
	}
	return well.lo.x;
}

/** The server's notice on the title row from left: a lit chip (MAINTENANCE or NOTICE) and its words with
	* when it ends, cut to end before right; nothing without a notice or the room for one.  It replaces the
	* plate InGameUI hangs from the screen's top in a match. */
static void drawNoticeRow( Int left, Int right, Int middle )
{
	OnlineNotice notice;
	if( right - left < menuPx( 240.0f ) || !OnlineGetNotice( notice ) )
		return;
	DisplayString **t = s_header;
	const Int gap = menuPx( 12.0f );
	if( !s_noticeShown || notice.serial != s_noticeSerial || right - left != s_noticeRoom )
	{
		s_noticeShown = TRUE;
		s_noticeSerial = notice.serial;
		s_noticeRoom = right - left;
		t[ HEADER_NOTICE_CHIP ]->setText( TheGameText->fetch( notice.maintenance ? "GUI:OnlineNoticeMaintenance" : "GUI:OnlineNoticeInfo" ) );
		const AsciiString utf8 = OnlineNoticeWords( notice, TheGlobalData->m_textLanguage );
		UnicodeString words = utf8.isNotEmpty() ? fromUtf8( utf8 ) : TheGameText->fetch( "GUI:OnlineNoticeMaintenanceWords" );
		const UnicodeString when = OnlineUntilText( notice.until );
		if( !when.isEmpty() )
		{
			UnicodeString until;
			until.format( TheGameText->fetch( notice.maintenance ? "GUI:OnlineNoticeBack" : "GUI:OnlineNoticeUntil" ), when.str() );
			words.concat( HEADER_JOINER );
			words.concat( until );
		}
		GameFont *chipFace = menuFont( MENU_TEXT_LABEL, TRUE );
		const Int chipWidth = measureInFont( t[ HEADER_NOTICE_CHIP ]->getText(), chipFace ) + menuPx( 18.0f );
		t[ HEADER_NOTICE ]->setText( fitText( words, t[ HEADER_NOTICE ]->getFont(), max( 0, right - left - chipWidth - gap ) ) );
	}
	const Int chipWidth = onlineChip( t[ HEADER_NOTICE_CHIP ], left, middle, TRUE );
	Int w, h;
	t[ HEADER_NOTICE ]->getSize( &w, &h );
	t[ HEADER_NOTICE ]->draw( left + chipWidth + gap, middle - h / 2, INK, black( 160 ) );
}

/** The server on the tab row, ending at right: its name and city in body type, the ping light 18 square,
	* the round trip MUTED; the name cut where it reaches left. */
static void drawServerRow( Int left, Int right, Int middle )
{
	UnicodeString place, ping;
	Int ms;
	serverLine( place, ping, ms );
	DisplayString **t = s_header;
	if( ping.compare( s_pingShown ) != 0 )
	{
		s_pingShown = ping;
		t[ HEADER_PING ]->setText( ping );
	}
	Int x = right;
	if( !ping.isEmpty() )
	{
		Int w, h;
		t[ HEADER_PING ]->getSize( &w, &h );
		x -= w;
		t[ HEADER_PING ]->draw( x, middle - h / 2, MUTED, black( 160 ) );
		x -= menuPx( 8.0f );
	}
	const Image *light = ms >= 0 ? pingImage( ms ) : NULL;
	if( light )
	{
		const Int size = menuPx( 18.0f );
		x -= size;
		TheDisplay->drawImage( light, x, middle - size / 2, x + size, middle - size / 2 + size );
		x -= menuPx( 10.0f );
	}
	// the friends key keeps its width and 24 of air at the left of the server's words
	const Int keyWidth = s_friendsKeyWidth > 0 ? menuPx( (Real)s_friendsKeyWidth ) : 0;
	const Int keyGap = s_friendsKeyWidth > 0 ? menuPx( 24.0f ) : 0;
	const Int room = x - left - keyWidth - keyGap;
	if( room != s_placeRoom || place.compare( s_placeShown ) != 0 )
	{
		s_placeRoom = room;
		s_placeShown = place;
		t[ HEADER_PLACE ]->setText( s_placeRoom > 0 ? fitName( place, t[ HEADER_PLACE ]->getFont(), s_placeRoom ) : UnicodeString::TheEmptyString );
	}
	Int wordsLeft = x;
	if( t[ HEADER_PLACE ]->getTextLength() > 0 )
	{
		Int w, h;
		t[ HEADER_PLACE ]->getSize( &w, &h );
		t[ HEADER_PLACE ]->draw( x - w, middle - h / 2, INK, black( 160 ) );
		wordsLeft = x - w;
	}
	if( keyWidth > 0 )
	{
		const Int half = menuPx( 18.0f );
		s_friendsKeyRect.lo.x = max( left, wordsLeft - keyGap - keyWidth );
		s_friendsKeyRect.hi.x = s_friendsKeyRect.lo.x + keyWidth;
		s_friendsKeyRect.lo.y = middle - half;
		s_friendsKeyRect.hi.y = middle + half;
	}
}

void onlineReserveFriendsKey( Int width1080 )
{
	s_friendsKeyWidth = max( 0, width1080 );
}

IRegion2D onlineFriendsKeyRect( void )
{
	return s_friendsKeyRect;
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
	s_lineLaidOut = FALSE;
	s_masterTitle = NULL;
	if( parent )
	{
		AsciiString name;
		name.format( "%s:StaticTextTitle", layout );
		s_masterTitle = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
	}

	s_lineRoom = -1;
	s_placeRoom = -1;
	s_placeShown.clear();
	s_noticeShown = FALSE;

	// the tabs from the content's left edge on the tab row, each its words in capitals plus 28 a side,
	// 8 apart (header.py); the master only stands them roughly there
	GameFont *labelFont = menuFont( MENU_TEXT_LABEL + 2, TRUE );
	const IRegion2D row = boardRect( ONLINE_LEFT, MENU_TABS_TOP, ONLINE_RIGHT, MENU_TABS_BOTTOM );
	Int x = row.lo.x;
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
	{
		s_tabs[ each ] = NULL;
		if( open == ONLINE_TAB_NONE || parent == NULL )
			continue;
		AsciiString name;
		name.format( "%s:%s", layout, TAB_WINDOW[ each ] );
		GameWindow *window = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( name ) );
		s_tabs[ each ] = window;
		if( window == NULL )
			continue;
		const UnicodeString label = capitals( window->winGetText() );
		GadgetButtonSetText( window, label );
		window->winSetFont( labelFont );
		const Int width = measureInFont( label, labelFont ) + 2 * menuPx( 28.0f );
		Int parentX = 0, parentY = 0;
		if( window->winGetParent() )
			window->winGetParent()->winGetScreenPosition( &parentX, &parentY );
		window->winSetPosition( x - parentX, row.lo.y - parentY );
		window->winSetSize( width, row.hi.y - row.lo.y );
		window->winSetDrawFunc( drawOnlineTab );
		x += width + menuPx( 8.0f );
	}

	// in HeaderText's order: the title, the subtitle, the wordmark's two halves, the nick, the tier, the
	// ELO's label and figure, the Bot ELO's, the ping, the well's "?", the server, the notice's chip and words
	const Int sizes[ HEADER_TEXT_COUNT ] = { MENU_TEXT_TITLE, MENU_TEXT_BODY, MENU_TEXT_ROW_FIGURE, MENU_TEXT_ROW_FIGURE,
		MENU_TEXT_ROW_FIGURE, MENU_TEXT_BODY, MENU_TEXT_LABEL, MENU_TEXT_ROW_FIGURE, MENU_TEXT_LABEL, MENU_TEXT_ROW_FIGURE,
		MENU_TEXT_BODY, MENU_TEXT_ROW_FIGURE, MENU_TEXT_BODY, MENU_TEXT_LABEL, MENU_TEXT_BODY };
	const Bool bold[ HEADER_TEXT_COUNT ] = { TRUE, FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
		TRUE, FALSE };
	for( Int each = 0; each < HEADER_TEXT_COUNT; each++ )
	{
		s_header[ each ] = TheDisplayStringManager->newDisplayString();
		s_header[ each ]->setFont( menuFont( sizes[ each ], bold[ each ] ) );
	}
	s_header[ HEADER_MARK ]->setText( UnicodeString( u"Zero Hour" ) );
	s_header[ HEADER_MARK_BLUE ]->setText( UnicodeString( u" Reforged" ) );
	s_header[ HEADER_LADDER ]->setText( capitals( wordOr( "GUI:OnlineLadderShort", u"ELO" ) ) );
	s_header[ HEADER_AI ]->setText( capitals( wordOr( "GUI:OnlineAIShort", u"Bot ELO" ) ) );
	s_header[ HEADER_WELL ]->setText( UnicodeString( u"?" ) );
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

void onlineHeader( const UnicodeString &title, const UnicodeString &subtitle )
{
	DisplayString **t = s_header;
	if( t[ HEADER_TITLE ] == NULL )
		return;		// no page, or between a page's shutdown and the next one's init

	s_friendsKeyRect.lo.x = s_friendsKeyRect.hi.x = 0;		// stood again by drawServerRow
	s_friendsKeyRect.lo.y = s_friendsKeyRect.hi.y = 0;
	const Bool signedOut = s_openTab == ONLINE_TAB_NONE;
	const IRegion2D panel = onlinePanel();
	glass( panel );
	frameA( panel );

	// every row keeps its offset from the panel's top left, so the login's dialog carries the same head
	const Int dy = signedOut ? ONLINE_LOGIN_TOP - MENU_PANEL_TOP : 0;
	const Int panelLeft = signedOut ? ONLINE_LOGIN_LEFT : MENU_PANEL_LEFT;
	const Int panelRight = signedOut ? ONLINE_LOGIN_RIGHT : MENU_PANEL_RIGHT;
	const IRegion2D row = boardRect( panelLeft + 28, MENU_TITLE_TOP + dy, panelRight - 28, MENU_TITLE_BOTTOM + dy );
	const Int middle = ( row.lo.y + row.hi.y ) / 2;

	// the title, the page's or the words of the master's hidden StaticTextTitle, then the subtitle
	UnicodeString words = title;
	if( words.isEmpty() && s_masterTitle && BitTest( s_masterTitle->winGetStyle(), GWS_STATIC_TEXT ) )
		words = GadgetStaticTextGetText( s_masterTitle );
	if( words.compare( s_titleShown ) != 0 )
	{
		s_titleShown = words;
		t[ HEADER_TITLE ]->setText( capitals( words ) );
	}
	Int w, h;
	t[ HEADER_TITLE ]->getSize( &w, &h );
	t[ HEADER_TITLE ]->draw( row.lo.x, middle - h / 2, INK, black( 160 ) );
	Int after = row.lo.x + w + menuPx( 40.0f );
	if( subtitle.compare( s_subtitleShown ) != 0 )
	{
		s_subtitleShown = subtitle;
		t[ HEADER_SUBTITLE ]->setText( subtitle );
	}
	if( !subtitle.isEmpty() )
	{
		t[ HEADER_SUBTITLE ]->getSize( &w, &h );
		t[ HEADER_SUBTITLE ]->draw( after - menuPx( 20.0f ), middle - h / 2, MUTED, black( 160 ) );
		after += w + menuPx( 20.0f );
	}

	const IRegion2D rule = boardRect( panelLeft + 2, MENU_HEADER_RULE + dy, panelRight - 2, MENU_HEADER_RULE + dy + 2 );
	litLine( rule.lo.x, rule.lo.y, rule.hi.x - rule.lo.x, max( 1, rule.hi.y - rule.lo.y ), FRAME );
	// the foot's faint rule, 90 over the panel's foot, the keys 14 under it
	const Int footY = ( signedOut ? ONLINE_LOGIN_BOTTOM : MENU_PANEL_BOTTOM ) - ( MENU_PANEL_BOTTOM - MENU_FOOT_RULE );
	const IRegion2D foot = boardRect( panelLeft + 28, footY, panelRight - 28, footY + 1 );
	TheDisplay->drawFillRect( foot.lo.x, foot.lo.y, foot.hi.x - foot.lo.x, 1, withAlpha( FRAME, 110 ) );

	if( signedOut )
	{
		// the wordmark in the player's line's place
		Int markWidth, markHeight;
		t[ HEADER_MARK ]->getSize( &markWidth, &markHeight );
		wordmark( &t[ HEADER_MARK ], row.hi.x, middle - markHeight / 2 );
		return;
	}
	const Int lineLeft = drawPlayerRow( row.hi.x, middle, after );
	drawNoticeRow( after, lineLeft - menuPx( 32.0f ), middle );

	// the tab row's right end: the server, from 40 past the last tab
	const IRegion2D tabs = boardRect( ONLINE_LEFT, MENU_TABS_TOP, ONLINE_RIGHT, MENU_TABS_BOTTOM );
	Int tabsRight = tabs.lo.x;
	for( Int each = 0; each < ONLINE_TAB_COUNT; each++ )
	{
		if( s_tabs[ each ] && !s_tabs[ each ]->winIsHidden() )
			tabsRight = max( tabsRight, windowRegion( s_tabs[ each ] ).hi.x );
	}
	drawServerRow( tabsRight + menuPx( 40.0f ), tabs.hi.x, ( tabs.lo.y + tabs.hi.y ) / 2 );
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

//=================================================================================================
// The menu language.  The look is hud-artist.md's "Menu HUD"; the reference drawing is
// experiments/proofs/2026-11-24/spirit/c-bridge/c3.py on c2.py, and every number here is theirs.
//=================================================================================================

BoardFit boardFit( Int screenWidth, Int screenHeight )
{
	BoardFit fit;
	fit.scale = min( screenWidth / (Real)BOARD_WIDTH, screenHeight / (Real)BOARD_HEIGHT );
	fit.x = ( screenWidth - REAL_TO_INT( BOARD_WIDTH * fit.scale + 0.5f ) ) / 2;
	fit.y = ( screenHeight - REAL_TO_INT( BOARD_HEIGHT * fit.scale + 0.5f ) ) / 2;
	return fit;
}

Int pxOn( Real at1080, Int screenWidth, Int screenHeight )
{
	const Int length = REAL_TO_INT( at1080 * boardFit( screenWidth, screenHeight ).scale + 0.5f );
	if( length == 0 && at1080 > 0.0f )
		return 1;
	return length;
}

IRegion2D boardRectOn( Int left, Int top, Int right, Int bottom, Int screenWidth, Int screenHeight )
{
	const BoardFit fit = boardFit( screenWidth, screenHeight );
	IRegion2D r;
	r.lo.x = fit.x + REAL_TO_INT( left * fit.scale + 0.5f );
	r.lo.y = fit.y + REAL_TO_INT( top * fit.scale + 0.5f );
	r.hi.x = fit.x + REAL_TO_INT( right * fit.scale + 0.5f );
	r.hi.y = fit.y + REAL_TO_INT( bottom * fit.scale + 0.5f );
	return r;
}

IRegion2D boardRect( Int left, Int top, Int right, Int bottom )
{
	return boardRectOn( left, top, right, bottom, TheDisplay->getWidth(), TheDisplay->getHeight() );
}

/// pxOn on this display: a line never thins to nothing
static Int menuPx( Real at1080 )
{
	return pxOn( at1080, TheDisplay->getWidth(), TheDisplay->getHeight() );
}

Int menuTextPixels( Int px1080, Int screenWidth, Int screenHeight )
{
	const Int pixels = REAL_TO_INT( px1080 * boardFit( screenWidth, screenHeight ).scale + 0.5f );
	return max( pixels, MENU_TEXT_FLOOR );
}

Int menuFontPoints( Int emPixels )
{
	// the nearest point size: an em of points * 4 / 3 pixels (render2dsentence.cpp's 96 dpi)
	return max( 1, ( emPixels * 3 + 2 ) / 4 );
}

GameFont *menuFont( Int px1080, Bool bold )
{
	const Int em = menuTextPixels( px1080, TheDisplay->getWidth(), TheDisplay->getHeight() );
	return TheFontLibrary->getFont( AsciiString( "Arial" ), menuFontPoints( em ), bold );
}

static Color black( Int alpha )
{
	return GameMakeColor( 0, 0, 0, alpha );
}

static void fill( const IRegion2D &r, Color color )
{
	TheDisplay->drawFillRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, color );
}

static void outline( const IRegion2D &r, Color color )
{
	TheDisplay->drawOpenRect( r.lo.x, r.lo.y, r.hi.x - r.lo.x, r.hi.y - r.lo.y, 1.0f, color );
}

static IRegion2D windowRegion( GameWindow *window )
{
	IRegion2D r;
	Int w, h;
	window->winGetScreenPosition( &r.lo.x, &r.lo.y );
	window->winGetSize( &w, &h );
	r.hi.x = r.lo.x + w;
	r.hi.y = r.lo.y + h;
	return r;
}

/// words centred in a box, in font
static void drawCentred( DisplayString *words, GameFont *font, const IRegion2D &box, Color color )
{
	if( words == NULL || words->getTextLength() == 0 )
		return;
	if( words->getFont() != font )
		words->setFont( font );
	Int w, h;
	words->getSize( &w, &h );
	words->draw( ( box.lo.x + box.hi.x - w ) / 2, ( box.lo.y + box.hi.y - h ) / 2, color, black( 160 ) );
}

void veil( Int alpha )
{
	TheDisplay->drawFillRect( 0, 0, TheDisplay->getWidth(), TheDisplay->getHeight(), black( alpha ) );
	// a box drawn after this frosts the veiled screen, not the world under it
	TheDisplay->staleBackdrop();
}

Bool backdropBlurs( void )
{
	// Direct3D 11 blurs what stands behind the glass (radius 14 at 1080); -d3d9, SDL3 and a frame
	// with no world under its interface draw the dim alone.  An empty box only asks.
	return TheDisplay->drawBackdropBlur( 0, 0, 0, 0 );
}

void glass( const IRegion2D &panel, Int alpha )
{
	TheDisplay->drawBackdropBlur( panel.lo.x, panel.lo.y, panel.hi.x, panel.hi.y );
	fill( panel, black( alpha ) );
	// a panel stacked on this one frosts this one, not the world under both
	TheDisplay->staleBackdrop();
}

void dimOutside( const IRegion2D &panel, Int alpha )
{
	const Int w = TheDisplay->getWidth();
	const Int h = TheDisplay->getHeight();
	const Color dim = black( alpha );
	TheDisplay->drawFillRect( 0, 0, w, panel.lo.y, dim );
	TheDisplay->drawFillRect( 0, panel.hi.y, w, h - panel.hi.y, dim );
	TheDisplay->drawFillRect( 0, panel.lo.y, panel.lo.x, panel.hi.y - panel.lo.y, dim );
	TheDisplay->drawFillRect( panel.hi.x, panel.lo.y, w - panel.hi.x, panel.hi.y - panel.lo.y, dim );
}

void litLine( Int x, Int y, Int width, Int height, Color color )
{
	// c2.glow_line: the line over a blurred copy of itself, here two wider passes at low alpha
	const Int g = menuPx( 2.0f );
	TheDisplay->drawFillRect( x - 2 * g, y - 2 * g, width + 4 * g, height + 4 * g, withAlpha( color, 24 ) );
	TheDisplay->drawFillRect( x - g, y - g, width + 2 * g, height + 2 * g, withAlpha( color, 48 ) );
	TheDisplay->drawFillRect( x, y, width, height, withAlpha( color, 255 ) );
}

void litEdge( const IRegion2D &box, Color color )
{
	const Int w = box.hi.x - box.lo.x;
	const Int h = box.hi.y - box.lo.y;
	litLine( box.lo.x, box.lo.y, w, 1, color );
	litLine( box.lo.x, box.hi.y - 1, w, 1, color );
	litLine( box.lo.x, box.lo.y, 1, h, color );
	litLine( box.hi.x - 1, box.lo.y, 1, h, color );
}

Int frameTickLength( Int index )
{
	if( index % 25 == 0 )
		return 14;
	if( index % 5 == 0 )
		return 8;
	return 4;
}

/// a tick's colour: the long and middle ones EDGE, the short ones FRAME, as c3.frame_a
static Color tickColor( Int index )
{
	const Int length = frameTickLength( index );
	if( length == 14 )
		return withAlpha( EDGE, 230 );
	if( length == 8 )
		return withAlpha( EDGE, 190 );
	return withAlpha( FRAME, 200 );
}

void frameA( const IRegion2D &panel )
{
	dimOutside( panel, 120 );
	const Int x0 = panel.lo.x, y0 = panel.lo.y, x1 = panel.hi.x, y1 = panel.hi.y;
	litEdge( panel, EDGE );

	// the scale, read from the top left corner
	for( Int i = 0; ; i++ )
	{
		const Int x = x0 + menuPx( (Real)( FRAME_TICK_STEP * i ) );
		if( i > 0 && x > x1 )
			break;
		const Int n = menuPx( (Real)frameTickLength( i ) );
		TheDisplay->drawFillRect( x, y0 - n, 1, n, tickColor( i ) );
		TheDisplay->drawFillRect( x, y1, 1, n, tickColor( i ) );
	}
	for( Int i = 0; ; i++ )
	{
		const Int y = y0 + menuPx( (Real)( FRAME_TICK_STEP * i ) );
		if( i > 0 && y > y1 )
			break;
		const Int n = menuPx( (Real)frameTickLength( i ) );
		TheDisplay->drawFillRect( x0 - n, y, n, 1, tickColor( i ) );
		TheDisplay->drawFillRect( x1, y, n, 1, tickColor( i ) );
	}

	// the corners' returns: an L standing FRAME_RETURN off each corner, its arms three returns long
	const Int r = menuPx( (Real)FRAME_RETURN );
	const Int arm = 3 * r;
	litLine( x0 - r, y0 - r, arm, 1, EDGE );
	litLine( x0 - r, y0 - r, 1, arm, EDGE );
	litLine( x1 + r - arm, y0 - r, arm, 1, EDGE );
	litLine( x1 + r - 1, y0 - r, 1, arm, EDGE );
	litLine( x0 - r, y1 + r - 1, arm, 1, EDGE );
	litLine( x0 - r, y1 + r - arm, 1, arm, EDGE );
	litLine( x1 + r - arm, y1 + r - 1, arm, 1, EDGE );
	litLine( x1 + r - 1, y1 + r - arm, 1, arm, EDGE );

	// the one accent, over the title
	litLine( x0 + menuPx( 32.0f ), y0 - menuPx( 2.0f ), menuPx( 200.0f ), menuPx( 3.0f ), EDGE_LIT );
}

void menuHeader( DisplayString *title, DisplayString *subtitle )
{
	const IRegion2D row = boardRect( MENU_INSET, MENU_TITLE_TOP, MENU_PANEL_RIGHT - 32, MENU_TITLE_BOTTOM );
	const Int middle = ( row.lo.y + row.hi.y ) / 2;
	Int x = row.lo.x;
	if( title && title->getTextLength() )
	{
		title->setFont( menuFont( MENU_TEXT_TITLE, TRUE ) );
		Int w, h;
		title->getSize( &w, &h );
		title->draw( x, middle - h / 2, INK, black( 160 ) );
		x += w + menuPx( 20.0f );
	}
	if( subtitle && subtitle->getTextLength() )
	{
		subtitle->setFont( menuFont( MENU_TEXT_BODY ) );
		Int w, h;
		subtitle->getSize( &w, &h );
		subtitle->draw( x, middle - h / 2, MUTED, black( 160 ) );
	}
	const IRegion2D rule = boardRect( MENU_PANEL_LEFT + 2, MENU_HEADER_RULE, MENU_PANEL_RIGHT - 2, MENU_HEADER_RULE + 2 );
	litLine( rule.lo.x, rule.lo.y, rule.hi.x - rule.lo.x, max( 1, rule.hi.y - rule.lo.y ), FRAME );
}

void menuTab( const IRegion2D &box, DisplayString *label, Bool open, Bool hovered )
{
	fill( box, black( 120 ) );
	Color words = INK;
	if( open )
	{
		outline( box, GameMakeColor( 90, 120, 40, 255 ) );
		const Int line = max( 1, menuPx( 2.0f ) );
		litLine( box.lo.x, box.hi.y - line, box.hi.x - box.lo.x, line, ACTIVE );
		words = ACTIVE;
	}
	else
		outline( box, hovered ? EDGE_LIT : withAlpha( TAB_EDGE, 150 ) );
	drawCentred( label, menuFont( MENU_TEXT_LABEL + 2, TRUE ), box, words );
}

void menuKey( const IRegion2D &box, DisplayString *label, KeyState state, Bool primary )
{
	fill( box, black( 170 ) );
	Color words = INK;
	if( state == KEY_DISABLED )
	{
		outline( box, FRAME );
		words = MUTED;
	}
	else if( primary )
	{
		// the lit key: an EDGE wash, the cyan line two pixels deep in its soft light, EDGE inside it
		fill( box, withAlpha( EDGE, state == KEY_PRESSED ? 110 : 60 ) );
		const Int w = box.hi.x - box.lo.x;
		const Int h = box.hi.y - box.lo.y;
		const Int line = max( 1, menuPx( 2.0f ) );
		litLine( box.lo.x, box.lo.y, w, line, EDGE_LIT );
		litLine( box.lo.x, box.hi.y - line, w, line, EDGE_LIT );
		litLine( box.lo.x, box.lo.y, line, h, EDGE_LIT );
		litLine( box.hi.x - line, box.lo.y, line, h, EDGE_LIT );
		IRegion2D inner = box;
		inner.lo.x += line;
		inner.lo.y += line;
		inner.hi.x -= line;
		inner.hi.y -= line;
		outline( inner, EDGE );
		if( state == KEY_HOVER )
			fill( inner, GameMakeColor( 255, 255, 255, 14 ) );
	}
	else if( state == KEY_PRESSED )
	{
		fill( box, withAlpha( EDGE, 90 ) );
		outline( box, EDGE );
	}
	else if( state == KEY_HOVER )
	{
		fill( box, GameMakeColor( 255, 255, 255, 14 ) );
		outline( box, EDGE_LIT );
	}
	else
		outline( box, EDGE );
	drawCentred( label, menuFont( MENU_TEXT_KEY, TRUE ), box, words );
}

static KeyState keyStateOf( GameWindow *window, WinInstanceData *instData )
{
	if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) == FALSE )
		return KEY_DISABLED;
	if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
		return KEY_PRESSED;
	if( BitTest( instData->getState(), WIN_STATE_HILITED ) )
		return KEY_HOVER;
	return KEY_NORMAL;
}

static void menuKeyDraw( GameWindow *window, WinInstanceData *instData )
{
	menuKey( windowRegion( window ), instData->getTextDisplayString(), keyStateOf( window, instData ), FALSE );
}

static void menuPrimaryKeyDraw( GameWindow *window, WinInstanceData *instData )
{
#if defined(_DEBUG) || defined(_INTERNAL)
	// one lit key a screen: a second primary under the same top window in the same frame is a bug
	static UnsignedInt s_frame = 0xFFFFFFFF;
	static GameWindow *s_root = NULL;
	static GameWindow *s_key = NULL;
	GameWindow *root = window;
	while( root->winGetParent() )
		root = root->winGetParent();
	const UnsignedInt frame = TheGameClient ? TheGameClient->getFrame() : 0;
	DEBUG_ASSERTCRASH( !( frame == s_frame && root == s_root && window != s_key ),
		( "UIStyle: two primary keys lit on one screen" ) );
	s_frame = frame;
	s_root = root;
	s_key = window;
#endif
	menuKey( windowRegion( window ), instData->getTextDisplayString(), keyStateOf( window, instData ), TRUE );
}

void setMenuKey( GameWindow *button, Bool primary )
{
	if( button == NULL )
		return;
	button->winSetDrawFunc( primary ? menuPrimaryKeyDraw : menuKeyDraw );
	button->winSetFont( menuFont( MENU_TEXT_KEY, TRUE ) );
}

static void menuCheckBoxDraw( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D r = windowRegion( window );
	const KeyState state = keyStateOf( window, instData );
	const Int side = min( r.hi.y - r.lo.y, menuPx( 22.0f ) );
	IRegion2D box;
	box.lo.x = r.lo.x;
	box.lo.y = ( r.lo.y + r.hi.y - side ) / 2;
	box.hi.x = box.lo.x + side;
	box.hi.y = box.lo.y + side;
	fill( box, black( 170 ) );
	outline( box, state == KEY_DISABLED ? FRAME : state == KEY_HOVER ? EDGE_LIT : EDGE );
	if( BitTest( instData->getState(), WIN_STATE_SELECTED ) )
	{
		// the tick, two strokes, lit green (MUTED when the box cannot be changed)
		const Color tick = state == KEY_DISABLED ? MUTED : ACTIVE;
		const Real width = (Real)max( 2, menuPx( 3.0f ) );
		const Int ax = box.lo.x + side / 5, ay = box.lo.y + side / 2;
		const Int bx = box.lo.x + side * 2 / 5, by = box.hi.y - side / 4;
		const Int cx = box.hi.x - side / 5, cy = box.lo.y + side / 4;
		TheDisplay->drawLine( ax, ay, bx, by, width, tick );
		TheDisplay->drawLine( bx, by, cx, cy, width, tick );
	}
	DisplayString *label = instData->getTextDisplayString();
	if( label && label->getTextLength() )
	{
		GameFont *font = menuFont( MENU_TEXT_BODY );
		if( label->getFont() != font )
			label->setFont( font );
		Int w, h;
		label->getSize( &w, &h );
		label->draw( box.hi.x + menuPx( 12.0f ), ( r.lo.y + r.hi.y - h ) / 2, state == KEY_DISABLED ? MUTED : INK, black( 160 ) );
	}
}

void setMenuCheckBox( GameWindow *checkBox )
{
	if( checkBox )
		checkBox->winSetDrawFunc( menuCheckBoxDraw );
}

void caretBlock( const IRegion2D &box, Bool hovered )
{
	fill( box, black( 170 ) );
	outline( box, hovered ? EDGE_LIT : EDGE );
	const Int w = ( box.hi.x - box.lo.x ) / 3;
	const Int cx = ( box.lo.x + box.hi.x ) / 2;
	const Int cy = ( box.lo.y + box.hi.y ) / 2;
	const Real width = (Real)max( 1, menuPx( 2.0f ) );
	TheDisplay->drawLine( cx - w / 2, cy - w / 4, cx, cy + w / 4, width, INK );
	TheDisplay->drawLine( cx, cy + w / 4, cx + w / 2, cy - w / 4, width, INK );
}

static void menuComboDraw( GameWindow *window, WinInstanceData *instData )
{
	const IRegion2D r = windowRegion( window );
	const KeyState state = keyStateOf( window, instData );
	fill( r, black( 120 ) );
	outline( r, state == KEY_DISABLED ? withAlpha( FRAME, 140 ) : state == KEY_HOVER ? EDGE_LIT : FRAME );
}

static void menuCaretDraw( GameWindow *window, WinInstanceData *instData )
{
	caretBlock( windowRegion( window ), keyStateOf( window, instData ) == KEY_HOVER );
}

void setMenuComboBox( GameWindow *comboBox )
{
	if( comboBox == NULL )
		return;
	comboBox->winSetDrawFunc( menuComboDraw );
	GameWindow *button = GadgetComboBoxGetDropDownButton( comboBox );
	if( button )
		button->winSetDrawFunc( menuCaretDraw );
}

void menuRow( const IRegion2D &row, RowState state, Bool zebra )
{
	if( zebra )
		fill( row, GameMakeColor( 255, 255, 255, 7 ) );
	const Int edge = max( 2, menuPx( 3.0f ) );
	if( state == ROW_LOCAL )
	{
		// the gold wash, 34 at the left fading to nothing at the right, in sixteen steps
		const Int steps = 16;
		const Int width = row.hi.x - row.lo.x;
		for( Int each = 0; each < steps; each++ )
		{
			const Int a = row.lo.x + width * each / steps;
			const Int b = row.lo.x + width * ( each + 1 ) / steps;
			TheDisplay->drawFillRect( a, row.lo.y, b - a, row.hi.y - row.lo.y, withAlpha( GOLD, 34 * ( steps - each ) / steps ) );
		}
		litLine( row.lo.x, row.lo.y, edge, row.hi.y - row.lo.y, GOLD );
	}
	else if( state == ROW_HOVER )
	{
		fill( row, GameMakeColor( 255, 255, 255, 14 ) );
		litLine( row.lo.x, row.lo.y, edge, row.hi.y - row.lo.y, EDGE_LIT );
	}
}

void columnHead( DisplayString *words, Int right, Int middle, Bool sorted )
{
	if( words == NULL || words->getTextLength() == 0 )
		return;
	words->setFont( menuFont( MENU_TEXT_LABEL, TRUE ) );
	Int w, h;
	words->getSize( &w, &h );
	words->draw( right - w, middle - h / 2, sorted ? ACTIVE : INK, black( 160 ) );
	if( sorted )
	{
		// the caret, a small down triangle before the words
		const Int size = menuPx( 8.0f );
		const Int x = right - w - menuPx( 6.0f ) - size;
		const Int y = middle - size / 4;
		for( Int row = 0; row < size / 2; row++ )
			TheDisplay->drawFillRect( x + row, y + row, size - 2 * row, 1, ACTIVE );
	}
}

Int questionBoxHeight( Int lines )
{
	return BOX_HEAD + BOX_LINE + lines * BOX_LINE + BOX_LINE + BOX_FOOT;
}

Int questionBoxMaxLines( void )
{
	return ( BOARD_HEIGHT - 2 * BOX_TOP_MIN - questionBoxHeight( 0 ) ) / BOX_LINE;
}

IRegion2D boxRect1080( Int width, Int height, Bool overMatch )
{
	IRegion2D r;
	r.lo.x = ( BOARD_WIDTH - width ) / 2;
	if( overMatch )
	{
		// boxes.py's place(): the world above the bar, 44 clear of it (frame A's return and air)
		const Int lowest = BOX_BAR_TOP - 44;
		r.lo.y = max( BOX_TOP_MIN, ( lowest - height ) / 2 + 8 );
		if( r.lo.y + height > lowest )
			r.lo.y = max( BOX_TOP_MIN, lowest - height );
	}
	else
		r.lo.y = ( BOARD_HEIGHT - height ) / 2;
	r.hi.x = r.lo.x + width;
	r.hi.y = r.lo.y + height;
	return r;
}

IRegion2D boxKeyRect1080( const IRegion2D &box, Int index )
{
	IRegion2D r;
	r.hi.x = box.hi.x - BOX_PAD - index * ( BOX_KEY_WIDTH + BOX_KEY_GAP );
	r.lo.x = r.hi.x - BOX_KEY_WIDTH;
	r.hi.y = box.hi.y - 20;
	r.lo.y = r.hi.y - BOX_KEY_HEIGHT;
	return r;
}

void wrapLines( const UnicodeString &text, Int widest, TextMeasure measure, void *context,
	std::vector< UnicodeString > &lines )
{
	UnicodeString line, word;
	const Int length = text.getLength();
	for( Int at = 0; at <= length; at++ )
	{
		const WideChar c = at < length ? text.getCharAt( at ) : u'\n';
		if( c != u' ' && c != u'\n' )
		{
			word.concat( c );
			continue;
		}
		if( !word.isEmpty() )
		{
			UnicodeString longer = line;
			if( !longer.isEmpty() )
				longer.concat( u' ' );
			longer.concat( word );
			if( line.isEmpty() || measure( longer, context ) <= widest )
				line = longer;
			else
			{
				lines.push_back( line );
				line = word;
			}
			word.clear();
		}
		if( c == u'\n' )
		{
			lines.push_back( line );
			line.clear();
		}
	}
}

void boxShell( const IRegion2D &box, DisplayString *title, Bool overMatch )
{
	// over a menu the screen steps right back; over a match only frame A's own dim, so the battle reads
	if( !overMatch )
		veil( 150 );
	glass( box, 200 );
	frameA( box );
	if( title == NULL )
		return;
	if( title->getTextLength() )
	{
		title->setFont( menuFont( MENU_TEXT_BOX_TITLE, TRUE ) );
		Int w, h;
		title->getSize( &w, &h );
		title->draw( box.lo.x + menuPx( (Real)BOX_PAD ), box.lo.y + menuPx( 44.0f ) - h / 2, INK, black( 160 ) );
	}
	litLine( box.lo.x + menuPx( 2.0f ), box.lo.y + menuPx( 80.0f ), box.hi.x - box.lo.x - 2 * menuPx( 2.0f ),
		max( 1, menuPx( 2.0f ) ), FRAME );
}

void boxFootRule( const IRegion2D &box )
{
	const Int pad = menuPx( (Real)BOX_PAD );
	TheDisplay->drawFillRect( box.lo.x + pad, box.hi.y - menuPx( (Real)BOX_FOOT ), box.hi.x - box.lo.x - 2 * pad, 1,
		withAlpha( FRAME, 110 ) );
}

//-------------------------------------------------------------------------------------------------
// The rooms' additions.
//-------------------------------------------------------------------------------------------------

/// one string per font and text, kept for the run: a tracked letter, a tab's count.  ponytail: never
/// freed until exit; a few hundred small strings at most (the letters of the labels in use, per font)
static DisplayString *cachedText( GameFont *font, const UnicodeString &text )
{
	typedef std::pair< GameFont *, std::basic_string< WideChar > > Key;
	static std::map< Key, DisplayString * > s_cache;
	const Key key( font, std::basic_string< WideChar >( text.str() ) );
	std::map< Key, DisplayString * >::iterator found = s_cache.find( key );
	if( found != s_cache.end() )
		return found->second;
	DisplayString *s = TheDisplayStringManager->newDisplayString();
	s->setFont( font );
	s->setText( text );
	s_cache[ key ] = s;
	return s;
}

static Int letterAdvance( const UnicodeString &letter, void *context )
{
	return cachedText( (GameFont *)context, letter )->getWidth( -1 );
}

Int trackedWidthOf( const UnicodeString &text, Int trackingPx, TextMeasure measure, void *context )
{
	Int width = 0;
	const Int length = text.getLength();
	for( Int each = 0; each < length; each++ )
	{
		UnicodeString letter;
		letter.concat( text.getCharAt( each ) );
		width += measure( letter, context );
		if( each + 1 < length )
			width += trackingPx;
	}
	return width;
}

Int trackedWidth( const UnicodeString &text, GameFont *font, Real tracking1080 )
{
	return trackedWidthOf( text, REAL_TO_INT( tracking1080 * boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() ).scale + 0.5f ),
		letterAdvance, font );
}

Int drawTracked( const UnicodeString &text, GameFont *font, Int x, Int y, Color color, Real tracking1080 )
{
	const Int tracking = REAL_TO_INT( tracking1080 * boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() ).scale + 0.5f );
	Int at = x;
	const Int length = text.getLength();
	for( Int each = 0; each < length; each++ )
	{
		UnicodeString letter;
		letter.concat( text.getCharAt( each ) );
		DisplayString *s = cachedText( font, letter );
		if( letter.getCharAt( 0 ) != u' ' )
			s->draw( at, y, color, black( 160 ) );
		at += s->getWidth( -1 ) + ( each + 1 < length ? tracking : 0 );
	}
	return at - x;
}

void columnHeadAt( DisplayString *words, Int left, Int middle, Color color, Bool sorted, Real tracking1080 )
{
	if( words == NULL || words->getTextLength() == 0 )
		return;
	GameFont *font = menuFont( MENU_TEXT_LABEL, TRUE );
	if( words->getFont() != font )
		words->setFont( font );
	Int w, h;
	words->getSize( &w, &h );
	drawTracked( words->getText(), font, left, middle - h / 2, sorted ? ACTIVE : color, tracking1080 );
	if( sorted )
	{
		const Int size = menuPx( 8.0f );
		const Int x = left - menuPx( 6.0f ) - size;
		const Int y = middle - size / 4;
		for( Int row = 0; row < size / 2; row++ )
			TheDisplay->drawFillRect( x + row, y + row, size - 2 * row, 1, ACTIVE );
	}
}

void menuTab( const IRegion2D &box, DisplayString *label, Bool open, Bool hovered, Int count )
{
	if( count <= 0 || label == NULL || label->getTextLength() == 0 )
	{
		menuTab( box, label, open, hovered );
		return;
	}
	// the tab without its words, then the words and the count as one line, centred as menuTab centres
	menuTab( box, NULL, open, hovered );
	GameFont *font = menuFont( MENU_TEXT_LABEL + 2, TRUE );
	if( label->getFont() != font )
		label->setFont( font );
	UnicodeString number;
	number.format( u"  %d", count );
	DisplayString *tail = cachedText( font, number );
	Int tailW, h;
	tail->getSize( &tailW, &h );
	const Int lead = label->getWidth( -1 );
	const Color words = open ? ACTIVE : INK;
	const Int x = ( box.lo.x + box.hi.x - lead - tailW ) / 2;
	const Int y = ( box.lo.y + box.hi.y - h ) / 2;
	label->draw( x, y, words, black( 160 ) );
	tail->draw( x + lead, y, words, black( 160 ) );
}

static void menuComboPinnedDraw( GameWindow *window, WinInstanceData *instData )
{
	if( BitTest( window->winGetStatus(), WIN_STATUS_ENABLED ) )
		menuComboDraw( window, instData );
}

static void menuCaretPinnedDraw( GameWindow *window, WinInstanceData *instData )
{
	GameWindow *combo = window->winGetParent();
	if( combo && BitTest( combo->winGetStatus(), WIN_STATUS_ENABLED ) )
		menuCaretDraw( window, instData );
}

void setMenuComboBoxPinned( GameWindow *comboBox )
{
	if( comboBox == NULL )
		return;
	setMenuComboBox( comboBox );
	comboBox->winSetDrawFunc( menuComboPinnedDraw );
	if( GameWindow *button = GadgetComboBoxGetDropDownButton( comboBox ) )
		button->winSetDrawFunc( menuCaretPinnedDraw );
}

static void menuFieldDraw( GameWindow *window, WinInstanceData * )
{
	EntryData *e = (EntryData *)window->winGetUserData();
	if( e == NULL || e->text == NULL )
		return;
	const IRegion2D r = windowRegion( window );
	GameFont *font = window->winGetFont();
	if( e->text->getFont() != font )
		e->text->setFont( font );
	IRegion2D clip = r;
	e->text->setClipRegion( &clip );
	Int w, h;
	e->text->getSize( &w, &h );
	e->text->draw( r.lo.x + menuPx( 10.0f ), ( r.lo.y + r.hi.y - h ) / 2, INK, black( 160 ) );
}

void setMenuComboField( GameWindow *comboBox )
{
	if( comboBox == NULL )
		return;
	if( GameWindow *edit = GadgetComboBoxGetEditBox( comboBox ) )
		edit->winSetDrawFunc( menuFieldDraw );
}

void setMenuListBox( GameWindow *listBox )
{
	if( listBox == NULL )
		return;
	const Color ground = black( 235 );
	const Color picked = withAlpha( EDGE, 90 );
	GadgetListBoxSetColors( listBox,
		ground, EDGE, picked, EDGE_LIT,
		ground, FRAME, withAlpha( FRAME, 90 ), FRAME,
		ground, EDGE_LIT, picked, EDGE_LIT );
	listBox->winClearStatus( WIN_STATUS_IMAGE );
	listBox->winSetDrawFunc( TheWindowManager->getListBoxDrawFunc() );
}

}
