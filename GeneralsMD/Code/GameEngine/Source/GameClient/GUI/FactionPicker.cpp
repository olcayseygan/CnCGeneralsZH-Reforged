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

// FILE: FactionPicker.cpp ////////////////////////////////////////////////////////////////////////
//
// The staging rooms' army screen (FactionPicker.h), in the menu language (MENU-REDESIGN.md package 7,
// experiments/proofs/2026-11-24/screens/pickers/README-pickers.md).  One window drawn in code over the
// whole room, modal while it is up: frame A on the room's panel, Random All and Custom Selection as
// tabs, one row a side (its tick box, its name, how many of its armies are in the draw, a RANDOM
// <SIDE> key, then its armies as named tiles), and on the right THE DRAW (the count, every army that
// can be drawn, the ones in it with the game's words for each) or, with one army picked, its large
// portrait, its words and features and the other armies of its side.  BACK and CONFIRM stand in the
// room's foot, CONFIRM on Play Game's rect.  It reads the armies from the seat's own box, so whatever
// the box lists (the sides, the generals, Observer where the room has one) is what the screen offers,
// and it hands the pick back through the box.
//
// Custom Selection ticks the Random pool.  A ticked pool goes into the seat's faction number
// (EncodeRandomPool, GameInfo.h), which every room already sends to the host and the host to every
// player, so the draw at the start of the match sees the same pool on every machine.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/Clock.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/FactionPicker.h"
#include "GameClient/GameClient.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameInfo.h"

#include <vector>

// the screen on the 1920x1080 board (README-pickers.md)
static const Int PANEL_LEFT = UIStyle::MENU_PANEL_LEFT;
static const Int PANEL_TOP = UIStyle::MENU_PANEL_TOP;
static const Int PANEL_RIGHT = UIStyle::MENU_PANEL_RIGHT;
static const Int PANEL_BOTTOM = UIStyle::MENU_PANEL_BOTTOM;
static const Int LEFT = 60;
static const Int RIGHT = 1860;
static const Int TABS_TOP = 126;
static const Int TABS_BOTTOM = 166;
static const Int LIST_RIGHT = 860;				///< the side rows' right edge
static const Int ROWS_TOP = 182;
static const Int BODY_BOTTOM = 952;
static const Int GAP = 8;
static const Int HEAD_WIDTH = 196;				///< a row's side head, 60 to 256
static const Int TICK = 24;
static const Int KEY_HEIGHT = 34;
static const Int DIVIDER = 876;
static const Int DETAIL_LEFT = 892;
static const Int FOOT_RULE = 962;
static const Int KEYS_TOP = 976;
static const Int KEYS_BOTTOM = 1028;
static const Int BACK_RIGHT = 270;
static const Int CONFIRM_LEFT = 1318;			///< the room's Play Game
static const Int FOOT_MIDDLE = 1002;
static const Int PORTRAIT = 380;
static const Int STRIP_PICTURE = 52;
static const Int TIP_WIDTH = 560;

// the screen's own types, unnamed: MapPicker.cpp has a PickerCell of its own (MapPicker.cpp says why)
namespace
{

struct PickerCell
{
	Int pick;							///< the box's item data: a template or PLAYERTEMPLATE_OBSERVER
	Int poolBit;					///< its bit in a Random pool, -1 when it cannot go in one
	UnicodeString name;		///< the box's own line, so an army reads as the box has it
	const Image *icon;
	const Image *portrait;
	UnicodeString strategy;
	UnicodeString features;
	IRegion2D box;				///< its tile on the screen
};

struct PickerGroup
{
	AsciiString leadSide;
	UnicodeString name;
	IRegion2D tickBox;		///< Custom Selection's box for the whole side
	IRegion2D keyBox;			///< RANDOM <SIDE>: that side alone in the draw
	UnsignedInt mask;			///< the side's pool bits, 0 for Observer
	Int top, bottom;			///< its row on the board
	std::vector<Int> cells;
};

}	// namespace

static Bool s_openOnStart = FALSE;
static Bool s_openOnStartCustom = FALSE;
static AsciiString s_openOnStartSide;

static GameWindow *s_window = NULL;
static GameWindow *s_combo = NULL;
static GameWindow *s_oldFocus = NULL;
static std::vector<PickerCell> s_cells;
static std::vector<PickerGroup> s_groups;
static Int s_focus = -1;					///< a cell, or -1 for Random
static Bool s_custom = FALSE;
static UnsignedInt s_mask = 0;
static Int s_lastPressCell = -1;
static UnsignedInt s_lastPressTime = 0;
static Bool s_pressedHere = FALSE;		///< the left press that this release ends came to the screen
static Bool s_sinking = FALSE;			///< closed: the window eats input, unseen, until the next frame
static UnsignedInt s_sinkFrame = 0;
static GameWindow *s_room = NULL;		///< the room's top window, disabled while the screen is up
static Bool s_roomWasEnabled = FALSE;

static IRegion2D s_tabBox[ 2 ], s_backBox, s_confirmBox;

// one display string a place on the screen, so a string's texture is rebuilt only when its words change
enum
{
	TEXT_TITLE = 0, TEXT_TABS = 1, TEXT_BACK = 3, TEXT_CONFIRM, TEXT_HINT,
	TEXT_GROUPS = 8,				///< eight a group: name, count, key, two line words, three side line rows
	GROUP_TEXTS = 8,
	TEXT_CELLS = TEXT_GROUPS + GROUP_TEXTS * 8,	///< two a cell: its name's lines
	TEXT_DETAIL = TEXT_CELLS + 2 * 48,					///< the right column's words
	DETAIL_TEXTS = 96,
	TEXT_TIP = TEXT_DETAIL + DETAIL_TEXTS,
	TEXT_COUNT = TEXT_TIP + 8
};
static DisplayString *s_texts[ TEXT_COUNT ];
static DisplayString *s_measure = NULL;

//-------------------------------------------------------------------------------------------------
static DisplayString *text( Int key, const UnicodeString &words, GameFont *font )
{
	if( key < 0 || key >= TEXT_COUNT )
		return NULL;
	DisplayString *&s = s_texts[ key ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	if( s->getFont() != font )
		s->setFont( font );
	if( s->getText().compare( words ) != 0 )
		s->setText( words );
	return s;
}

static Int measureIn( const UnicodeString &words, void *font )
{
	if( s_measure == NULL )
		s_measure = TheDisplayStringManager->newDisplayString();
	if( s_measure->getFont() != (GameFont *)font )
		s_measure->setFont( (GameFont *)font );
	s_measure->setText( words );
	return s_measure->getWidth( -1 );
}

static Int heightOf( GameFont *font )
{
	if( s_measure == NULL )
		s_measure = TheDisplayStringManager->newDisplayString();
	s_measure->setFont( font );
	s_measure->setText( UnicodeString( u"Ag" ) );
	Int w = 0, h = 0;
	s_measure->getSize( &w, &h );
	return h;
}

static Int fpx( Int length ) { return UIStyle::pxOn( (Real)length, TheDisplay->getWidth(), TheDisplay->getHeight() ); }
static IRegion2D board( Int l, Int t, Int r, Int b ) { return UIStyle::boardRect( l, t, r, b ); }
static Int boxWidth( const IRegion2D &box ) { return box.hi.x - box.lo.x; }
static Int boxHeight( const IRegion2D &box ) { return box.hi.y - box.lo.y; }
static const Color SHADE = GameMakeColor( 0, 0, 0, 160 );

static Bool isInside( const IRegion2D &box, Int x, Int y )
{
	return x >= box.lo.x && x < box.hi.x && y >= box.lo.y && y < box.hi.y;
}

static void fillBox( const IRegion2D &b, Color color )
{
	if( boxWidth( b ) > 0 && boxHeight( b ) > 0 )
		TheDisplay->drawFillRect( b.lo.x, b.lo.y, boxWidth( b ), boxHeight( b ), color );
}

static void frameBox( const IRegion2D &b, Color color )
{
	TheDisplay->drawOpenRect( b.lo.x, b.lo.y, boxWidth( b ), boxHeight( b ), 1.0f, color );
}

/// words in a screen box: across centres them, else from its left; down they are centred
static void drawIn( Int key, const UnicodeString &words, GameFont *font, const IRegion2D &b, Color color, Bool across = FALSE )
{
	DisplayString *s = text( key, words, font );
	if( s == NULL || words.isEmpty() )
		return;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( across ? ( b.lo.x + b.hi.x - w ) / 2 : b.lo.x, ( b.lo.y + b.hi.y - h ) / 2, color, SHADE );
}

/// words wrapped to a board column from y, pitch apart, at most lines of them (the last cut with an
/// ellipsis at a word); the y under them
static Int drawWrapped( Int key, const UnicodeString &words, GameFont *font, Int left, Int y, Int right, Int pitch, Int lines, Color color )
{
	std::vector<UnicodeString> wrapped;
	UIStyle::wrapLines( words, fpx( right - left ), measureIn, font, wrapped );
	const Int count = min( lines, (Int)wrapped.size() );
	for( Int k = 0; k < count; k++ )
	{
		UnicodeString line = wrapped[ k ];
		if( k == count - 1 && count < (Int)wrapped.size() )
		{
			line.concat( u" " );
			line.concat( wrapped[ k + 1 ] );
			line = UIStyle::fitText( line, font, fpx( right - left ) );
		}
		drawIn( key + k, line, font, board( left, y, right, y + pitch ), color );
		y += pitch;
	}
	return y;
}

static void tracked( const UnicodeString &words, GameFont *font, Int x, Int y, Color color, Real tracking )
{
	UIStyle::drawTracked( words, font, x, y, color, tracking );
}

static void click( void )
{
	AudioEventRTS sound( "GUIClick" );
	TheAudio->addAudioEvent( &sound );
}

/** A Features label lists one thing a line. */
static void splitLines( const UnicodeString &words, std::vector<UnicodeString> &lines )
{
	lines.clear();
	UnicodeString line;
	for( Int each = 0; each <= words.getLength(); each++ )
	{
		const WideChar c = each < words.getLength() ? words.getCharAt( each ) : u'\n';
		// EA's labels carry the break as a real one; a hand-written .str may leave it as the two characters
		const Bool escaped = c == u'\\' && each + 1 < words.getLength() && words.getCharAt( each + 1 ) == u'n';
		if( c != u'\n' && !escaped )
		{
			line.concat( c );
			continue;
		}
		if( escaped )
			each++;
		line.trim();
		if( !line.isEmpty() )
			lines.push_back( line );
		line.clear();
	}
}

/// an army's line, its strategy with a full stop
static UnicodeString sentence( const UnicodeString &words )
{
	UnicodeString out = words;
	out.trim();
	if( !out.isEmpty() && out.getCharAt( out.getLength() - 1 ) != u'.' )
		out.concat( u'.' );
	return out;
}

static Bool isTicked( const PickerCell &cell )
{
	return cell.poolBit >= 0 && ( s_mask & ( 1u << cell.poolBit ) ) != 0;
}

static Int tickedCount( void )
{
	Int count = 0;
	for( size_t each = 0; each < s_cells.size(); each++ )
		count += isTicked( s_cells[ each ] ) ? 1 : 0;
	return count;
}

/// whether Random, as the screen stands, can land on the army
static Bool isDrawn( const PickerCell &cell )
{
	if( s_custom )
		return isTicked( cell );
	return cell.poolBit >= 0 && !ThePlayerTemplateStore->getNthPlayerTemplate( cell.pick )->isRandomExcluded();
}

/// the pool bits of the armies the box lists; a template it hides keeps a bit nobody can untick
static UnsignedInt listedMask( void )
{
	UnsignedInt mask = 0;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( s_cells[ each ].poolBit >= 0 )
			mask |= 1u << s_cells[ each ].poolBit;
	}
	return mask;
}

static Int cellAt( Int x, Int y )
{
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( isInside( s_cells[ each ].box, x, y ) )
			return (Int)each;
	}
	return -1;
}

/// the side a Custom Selection pool is, every army of it and nothing else, or -1
static Int poolSide( void )
{
	if( !s_custom )
		return -1;
	const UnsignedInt pool = s_mask & listedMask();
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		if( s_groups[ group ].mask != 0 && s_groups[ group ].mask == pool )
			return (Int)group;
	}
	return -1;
}

/// a side's tick box (tickBox) or its RANDOM key under the pointer, -1 for none
static Int sideAt( Int x, Int y, Bool tickBox )
{
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		const PickerGroup &g = s_groups[ group ];
		if( g.mask != 0 && isInside( tickBox ? g.tickBox : g.keyBox, x, y ) )
			return (Int)group;
	}
	return -1;
}

static Bool canConfirm( void )
{
	return s_focus >= 0 || !s_custom || tickedCount() > 0;
}

/// a side with a RANDOM key: two or more armies a Random can be; a lone army a plain Random never
/// draws (Turkiye) says EXPERIMENTAL instead
static Bool hasSideKey( const PickerGroup &g )
{
	Int drawable = 0;
	for( size_t each = 0; each < g.cells.size(); each++ )
		drawable += s_cells[ g.cells[ each ] ].poolBit >= 0 ? 1 : 0;
	return drawable > 1;
}

static Bool isExperimental( const PickerGroup &g )
{
	if( g.cells.size() != 1 || s_cells[ g.cells[ 0 ] ].pick == PLAYERTEMPLATE_OBSERVER )
		return FALSE;
	return ThePlayerTemplateStore->getNthPlayerTemplate( s_cells[ g.cells[ 0 ] ].pick )->isRandomExcluded();
}

//-------------------------------------------------------------------------------------------------
/** The armies in the box, each under the first army of its side: a general's Side begins with his
	* side's ("AmericaLaserGeneral" under "America"), so Turkiye, on America's BaseSide but a side of its
	* own, heads its own group. */
static void buildCells( void )
{
	std::vector<Int> candidates;
	GetRandomPoolCandidates( candidates );

	GameWindow *listBox = GadgetComboBoxGetListBox( s_combo );
	for( Int line = 0; line < GadgetComboBoxGetLength( s_combo ); line++ )
	{
		const Int pick = (Int)(intptr_t)GadgetComboBoxGetItemData( s_combo, line );
		if( IsRandomPlayerTemplate( pick ) )
			continue;

		PickerCell cell;
		cell.pick = pick;
		cell.poolBit = -1;
		cell.name = GadgetListBoxGetText( listBox, line, 0 );
		cell.icon = NULL;
		cell.portrait = NULL;
		cell.box.lo.x = cell.box.lo.y = cell.box.hi.x = cell.box.hi.y = 0;

		AsciiString side = "Observer";
		if( pick == PLAYERTEMPLATE_OBSERVER )
		{
			cell.icon = cell.portrait = TheMappedImageCollection->findImageByName( "GameinfoOBSRVR" );
			cell.strategy = TheGameText->fetch( "GUI:FactionPickObserverLine" );
		}
		else
		{
			const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate( pick );
			side = pt->getSide();
			for( size_t n = 0; n < candidates.size() && n < RANDOM_POOL_BITS; n++ )
			{
				if( candidates[ n ] == pick )
					cell.poolBit = (Int)n;
			}
			// a retail general has the challenge screen's pictures of him; a side its Command Center
			// (UIStyle::armyPortrait), Turkiye's own TRCameoCommandCenter_L included
			const GeneralPersona *persona = TheChallengeGenerals->getGeneralByTemplateName( pt->getName() );
			const Image *logo = UIStyle::armyPortrait( pt );
			cell.icon = persona && persona->getBioPortraitSmall() ? persona->getBioPortraitSmall() : logo;
			cell.portrait = persona && persona->getBioPortraitLarge() ? persona->getBioPortraitLarge() : logo;
			if( !pt->getTooltip().isEmpty() )
				cell.strategy = TheGameText->fetch( pt->getTooltip() );
			if( !pt->getGeneralFeatures().isEmpty() )
				cell.features = TheGameText->fetch( pt->getGeneralFeatures() );
		}

		size_t group = 0;
		for( ; group < s_groups.size(); group++ )
		{
			if( side.startsWith( s_groups[ group ].leadSide ) )
				break;
		}
		if( group == s_groups.size() )
		{
			PickerGroup lead;
			lead.leadSide = side;
			lead.name = cell.name;
			lead.mask = 0;
			lead.top = lead.bottom = 0;
			lead.tickBox = lead.keyBox = cell.box;
			s_groups.push_back( lead );
		}
		if( cell.poolBit >= 0 )
			s_groups[ group ].mask |= 1u << cell.poolBit;
		s_groups[ group ].cells.push_back( (Int)s_cells.size() );
		s_cells.push_back( cell );
	}
}

/** One row a side down the left, the rows sharing the height; the tiles of every row one size, as
	* many columns as the longest side has armies (four at least). */
static void layout( void )
{
	const Int tabWidth = ( LIST_RIGHT - LEFT - GAP ) / 2;
	s_tabBox[ 0 ] = board( LEFT, TABS_TOP, LEFT + tabWidth, TABS_BOTTOM );
	s_tabBox[ 1 ] = board( LIST_RIGHT - tabWidth, TABS_TOP, LIST_RIGHT, TABS_BOTTOM );
	s_backBox = board( LEFT, KEYS_TOP, BACK_RIGHT, KEYS_BOTTOM );
	s_confirmBox = board( CONFIRM_LEFT, KEYS_TOP, RIGHT, KEYS_BOTTOM );

	const Int groups = max( 1, (Int)s_groups.size() );
	const Int rowHeight = ( BODY_BOTTOM - ROWS_TOP - ( groups - 1 ) * GAP ) / groups;
	Int columns = 4;
	for( size_t group = 0; group < s_groups.size(); group++ )
		columns = max( columns, (Int)s_groups[ group ].cells.size() );
	const Int tilesLeft = LEFT + HEAD_WIDTH;
	const Int tileWidth = ( LIST_RIGHT - tilesLeft - ( columns - 1 ) * GAP ) / columns;
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		PickerGroup &g = s_groups[ group ];
		g.top = ROWS_TOP + (Int)group * ( rowHeight + GAP );
		g.bottom = g.top + rowHeight;
		g.tickBox = board( LEFT, g.top + 4, LEFT + TICK, g.top + 4 + TICK );
		g.keyBox = board( LEFT, g.top + 60, LEFT + HEAD_WIDTH - 16, g.top + 60 + KEY_HEIGHT );
		for( size_t each = 0; each < g.cells.size(); each++ )
		{
			const Int left = tilesLeft + (Int)each * ( tileWidth + GAP );
			s_cells[ g.cells[ each ] ].box = board( left, g.top, left + tileWidth, g.bottom );
		}
	}
}

/// the name the screen shows for the pick as it stands: the army, Random of a side, or Random
static UnicodeString pickName( void )
{
	if( s_focus >= 0 )
		return s_cells[ s_focus ].name;
	const Int side = poolSide();
	if( side >= 0 )
	{
		UnicodeString name;
		name.format( TheGameText->fetch( "GUI:FactionPickRandomSide" ).str(), s_groups[ side ].name.str() );
		return name;
	}
	return TheGameText->fetch( "GUI:Random" );
}

//-------------------------------------------------------------------------------------------------
static void drawTickBox( const IRegion2D &b, Bool ticked, Bool hovered )
{
	if( ticked )
	{
		fillBox( b, UIStyle::EDGE_LIT );
		const Int s = boxWidth( b );
		const Real line = (Real)max( 2, s / 8 );
		const Color dark = GameMakeColor( 8, 12, 24, 255 );
		TheDisplay->drawLine( b.lo.x + s * 22 / 100, b.lo.y + s * 52 / 100, b.lo.x + s * 42 / 100, b.lo.y + s * 72 / 100, line, dark );
		TheDisplay->drawLine( b.lo.x + s * 42 / 100, b.lo.y + s * 72 / 100, b.lo.x + s * 78 / 100, b.lo.y + s * 28 / 100, line, dark );
	}
	else
	{
		fillBox( b, GameMakeColor( 0, 0, 0, 170 ) );
		frameBox( b, hovered ? UIStyle::EDGE_LIT : UIStyle::FRAME );
	}
}

/// a picture covering its square, a frame round it
static void drawPicture( const Image *image, const IRegion2D &square )
{
	fillBox( square, GameMakeColor( 6, 8, 12, 255 ) );
	if( image )
		UIStyle::coverImage( image, square.lo.x, square.lo.y, boxWidth( square ), boxHeight( square ) );
	frameBox( square, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );
}

/// a tile: its picture square at the top, its name on a line or two under it, its state
static void drawTile( Int index, Bool hovered )
{
	const PickerCell &cell = s_cells[ index ];
	const IRegion2D &r = cell.box;
	const Bool picked = index == s_focus;
	const Bool veiled = s_custom && !isTicked( cell ) && cell.poolBit >= 0;
	fillBox( r, GameMakeColor( 255, 255, 255, 8 ) );
	if( picked )
		fillBox( r, UIStyle::withAlpha( UIStyle::EDGE, 100 ) );
	else if( hovered )
		fillBox( r, GameMakeColor( 255, 255, 255, 24 ) );

	const Int side = min( boxWidth( r ) - fpx( 12 ), boxHeight( r ) - fpx( 50 ) );
	IRegion2D square;
	square.lo.x = ( r.lo.x + r.hi.x - side ) / 2;
	square.lo.y = r.lo.y + fpx( 6 );
	square.hi.x = square.lo.x + side;
	square.hi.y = square.lo.y + side;
	drawPicture( cell.icon, square );
	if( veiled )
		fillBox( square, GameMakeColor( 4, 6, 10, 170 ) );
	if( s_custom && cell.poolBit >= 0 )
	{
		IRegion2D tick;
		tick.lo.x = square.hi.x - fpx( 26 );
		tick.lo.y = square.lo.y + fpx( 6 );
		tick.hi.x = square.hi.x - fpx( 6 );
		tick.hi.y = square.lo.y + fpx( 26 );
		drawTickBox( tick, isTicked( cell ), FALSE );
	}

	// the name, broken at a space onto two lines when one is too narrow, the last cut at its end
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT + 1 );
	std::vector<UnicodeString> lines;
	UIStyle::wrapLines( cell.name, boxWidth( r ) - fpx( 14 ), measureIn, font, lines );
	if( lines.size() > 2 )
	{
		UnicodeString rest;
		for( size_t k = 1; k < lines.size(); k++ )
		{
			if( k > 1 )
				rest.concat( u" " );
			rest.concat( lines[ k ] );
		}
		lines.resize( 2 );
		lines[ 1 ] = UIStyle::fitText( rest, font, boxWidth( r ) - fpx( 14 ) );
	}
	const Int pitch = heightOf( font ) + fpx( 2 );
	const Int nameTop = square.hi.y + fpx( 4 );
	Int y = nameTop + ( r.hi.y - fpx( 3 ) - nameTop - pitch * (Int)lines.size() ) / 2;
	for( size_t k = 0; k < lines.size(); k++ )
	{
		IRegion2D at;
		at.lo.x = r.lo.x;
		at.lo.y = y;
		at.hi.x = r.hi.x;
		at.hi.y = y + pitch;
		drawIn( TEXT_CELLS + 2 * min( index, 47 ) + (Int)k, lines[ k ], font, at, veiled ? UIStyle::MUTED : UIStyle::INK, TRUE );
		y += pitch;
	}

	if( picked )
	{
		const Int line = max( 1, fpx( 2 ) );
		UIStyle::litLine( r.lo.x, r.lo.y, boxWidth( r ), line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.hi.y - line, boxWidth( r ), line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.lo.y, line, boxHeight( r ), UIStyle::EDGE_LIT );
		UIStyle::litLine( r.hi.x - line, r.lo.y, line, boxHeight( r ), UIStyle::EDGE_LIT );
		IRegion2D tick;
		tick.lo.x = square.hi.x - fpx( 22 );
		tick.lo.y = square.lo.y;
		tick.hi.x = square.hi.x;
		tick.hi.y = square.lo.y + fpx( 22 );
		drawTickBox( tick, TRUE, FALSE );
	}
	else if( hovered )
		UIStyle::litEdge( r, UIStyle::EDGE_LIT );
	else
		frameBox( r, UIStyle::withAlpha( UIStyle::FRAME, 150 ) );
}

/// a side's head: its tick box, its name, how many of it are in the draw, its RANDOM key or what it is,
/// the side's own line under the key
static void drawGroupHead( Int group, const ICoord2D &mouse )
{
	const PickerGroup &g = s_groups[ group ];
	const Int key = TEXT_GROUPS + GROUP_TEXTS * min( group, 7 );
	const Bool observer = g.mask == 0;
	Int x = LEFT;
	if( s_custom && !observer )
	{
		drawTickBox( g.tickBox, FactionPickerSideTicked( s_mask, g.mask ), isInside( g.tickBox, mouse.x, mouse.y ) );
		x = LEFT + 34;
	}
	GameFont *nameFont = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY, TRUE );
	const UnicodeString name = UIStyle::capitals( observer ? TheGameText->fetch( "GUI:Observer" ) : g.name );
	drawIn( key, UIStyle::fitText( name, nameFont, fpx( LEFT + HEAD_WIDTH - 8 - x ) ), nameFont, board( x, g.top + 2, LEFT + HEAD_WIDTH - 8, g.top + 30 ),
		UIStyle::ACTIVE );

	Int inDraw = 0, drawable = 0;
	for( size_t each = 0; each < g.cells.size(); each++ )
	{
		const PickerCell &cell = s_cells[ g.cells[ each ] ];
		drawable += cell.poolBit >= 0 ? 1 : 0;
		inDraw += isDrawn( cell ) ? 1 : 0;
	}
	UnicodeString count;
	if( observer )
		count = TheGameText->fetch( "GUI:FactionPickObserverHead" );
	else
		count.format( TheGameText->fetch( "GUI:FactionPickInDraw" ).str(), inDraw, drawable );
	GameFont *line = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	drawIn( key + 1, UIStyle::fitText( count, line, fpx( LEFT + HEAD_WIDTH - 8 - x ) ), line, board( x, g.top + 32, LEFT + HEAD_WIDTH - 8, g.top + 52 ),
		inDraw > 0 || observer ? UIStyle::INK : UIStyle::MUTED );

	GameFont *caps = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	if( isExperimental( g ) )
	{
		const IRegion2D at = board( LEFT, g.top + 60, LEFT + HEAD_WIDTH - 8, g.top + 80 );
		tracked( TheGameText->fetch( "GUI:FactionPickExperimental" ), caps, at.lo.x, at.lo.y, UIStyle::MUTED, 1.0f );
	}
	else if( hasSideKey( g ) )
	{
		// what a click on the side's name did before, said on a key: that side alone, at random
		const Bool lit = s_custom && poolSide() == group;
		const Bool hovered = isInside( g.keyBox, mouse.x, mouse.y );
		fillBox( g.keyBox, GameMakeColor( 0, 0, 0, 160 ) );
		if( lit )
		{
			fillBox( g.keyBox, UIStyle::withAlpha( UIStyle::EDGE, 90 ) );
			UIStyle::litEdge( g.keyBox, UIStyle::EDGE_LIT );
		}
		else
		{
			if( hovered )
				fillBox( g.keyBox, GameMakeColor( 255, 255, 255, 14 ) );
			frameBox( g.keyBox, hovered ? UIStyle::EDGE_LIT : UIStyle::withAlpha( UIStyle::EDGE, 200 ) );
		}
		UnicodeString words;
		words.format( TheGameText->fetch( "GUI:FactionPickRandomSide" ).str(), g.name.str() );
		words = UIStyle::fitText( UIStyle::capitals( words ), caps, boxWidth( g.keyBox ) - fpx( 12 ) );
		const Int width = UIStyle::trackedWidth( words, caps, UIStyle::MENU_LABEL_TRACKING );
		tracked( words, caps, ( g.keyBox.lo.x + g.keyBox.hi.x - width ) / 2, ( g.keyBox.lo.y + g.keyBox.hi.y - heightOf( caps ) ) / 2,
			UIStyle::INK, UIStyle::MENU_LABEL_TRACKING );
		// the side's own line from the game in the head's free height
		const PickerCell &lead = s_cells[ g.cells[ 0 ] ];
		const Int top = g.top + 60 + KEY_HEIGHT + 10;
		GameFont *note = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL );
		const Int lines = max( 0, ( g.bottom - top ) / 19 );
		if( lines > 0 && !lead.strategy.isEmpty() )
			drawWrapped( key + 4, sentence( lead.strategy ), note, LEFT, top, LEFT + HEAD_WIDTH - 12, 19, min( 3, lines ), UIStyle::MUTED );
	}

	// a side of one army uses the row's free width: Turkiye's never drawn by a plain Random, the
	// Observer what an observer gets
	if( g.cells.size() == 1 )
	{
		const PickerCell &only = s_cells[ g.cells[ 0 ] ];
		Int fx = 0, fy = 0;
		{
			const UIStyle::BoardFit fit = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() );
			const Real scale = fit.scale > 0.0f ? fit.scale : 1.0f;
			fx = REAL_TO_INT( ( only.box.hi.x - fit.x ) / scale + 0.5f ) + 24;
			fy = g.top + 8;
		}
		GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
		const Int lines = max( 1, ( g.bottom - fy ) / 22 );
		if( observer )
		{
			const Int y = drawWrapped( key + 2, TheGameText->fetch( "GUI:FactionPickObserverLine" ), body, fx, fy, LIST_RIGHT, 22, 1, UIStyle::INK );
			drawWrapped( key + 5, TheGameText->fetch( "GUI:FactionPickObserverMore" ), body, fx, y + 6, LIST_RIGHT, 22, max( 1, min( 3, lines - 2 ) ), UIStyle::MUTED );
		}
		else if( isExperimental( g ) )
		{
			const Int y = drawWrapped( key + 2, TheGameText->fetch( "GUI:FactionPickTurkiyeLeft" ), body, fx, fy, LIST_RIGHT, 22, 2, UIStyle::MUTED );
			if( !only.strategy.isEmpty() )
				drawWrapped( key + 5, sentence( only.strategy ), body, fx, y + 6, LIST_RIGHT, 22, max( 1, min( 3, ( g.bottom - y - 10 ) / 22 ) ), UIStyle::INK );
		}
	}
}

/// a section head: 14 px bold capitals in green, a lit rule under it when rule
static Int drawSectionHead( const UnicodeString &words, Int left, Int top, Int right, Bool rule )
{
	GameFont *caps = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	const IRegion2D at = board( left, top, right, top + 22 );
	tracked( UIStyle::capitals( words ), caps, at.lo.x, at.lo.y + ( boxHeight( at ) - heightOf( caps ) ) / 2, UIStyle::ACTIVE,
		UIStyle::MENU_LABEL_TRACKING );
	if( rule )
	{
		const IRegion2D line = board( left, top + 26, right, top + 27 );
		UIStyle::litLine( line.lo.x, line.lo.y, boxWidth( line ), 1, UIStyle::FRAME );
	}
	return top + ( rule ? 34 : 26 );
}

/** THE DRAW: the count as the figure, every army a Random can be in a strip (in the draw lit, out of it
	* veiled), the pick's name and one line, then the armies in the draw with the game's words for each. */
static void drawTheDraw( void )
{
	Int key = TEXT_DETAIL;
	drawSectionHead( TheGameText->fetch( "GUI:FactionPickTheDraw" ), DETAIL_LEFT, TABS_TOP, RIGHT, FALSE );
	const Int figureTop = TABS_TOP + 26;
	std::vector<Int> drawable, drawn;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( s_cells[ each ].poolBit < 0 )
			continue;
		drawable.push_back( (Int)each );
		if( isDrawn( s_cells[ each ] ) )
			drawn.push_back( (Int)each );
	}
	UnicodeString figure;
	figure.format( u"%d", (Int)drawn.size() );
	GameFont *figureFont = UIStyle::menuFont( UIStyle::MENU_TEXT_TEAM_FIGURE );
	DisplayString *s = text( key++, figure, figureFont );
	const IRegion2D figureAt = board( DETAIL_LEFT, figureTop, DETAIL_LEFT + 120, figureTop + 48 );
	Int figureW = 0, figureH = 0;
	s->getSize( &figureW, &figureH );
	s->draw( figureAt.lo.x, ( figureAt.lo.y + figureAt.hi.y - figureH ) / 2, UIStyle::INK, SHADE );
	UnicodeString of;
	of.format( TheGameText->fetch( "GUI:FactionPickOfArmies" ).str(), (Int)drawable.size() );
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	const IRegion2D ofAt = { { figureAt.lo.x + figureW + fpx( 12 ), board( 0, figureTop + 14, 0, 0 ).lo.y }, { board( DETAIL_LEFT + 300, 0, 0, 0 ).lo.x, board( 0, figureTop + 40, 0, 0 ).lo.y } };
	drawIn( key++, of, body, ofAt, UIStyle::MUTED );

	// the strip, spread over what is left of the row
	const Int stripLeft = DETAIL_LEFT + 300;
	const Int count = max( 1, (Int)drawable.size() );
	const Int step = count > 1 ? min( STRIP_PICTURE + GAP, ( RIGHT - stripLeft - STRIP_PICTURE ) / ( count - 1 ) ) : 0;
	const Int picture = min( STRIP_PICTURE, max( 16, step > 0 ? step - 4 : STRIP_PICTURE ) );
	for( Int k = 0; k < (Int)drawable.size(); k++ )
	{
		const PickerCell &cell = s_cells[ drawable[ k ] ];
		const IRegion2D pr = board( stripLeft + k * step, figureTop - 2, stripLeft + k * step + picture, figureTop - 2 + picture );
		if( cell.icon )
			UIStyle::coverImage( cell.icon, pr.lo.x, pr.lo.y, boxWidth( pr ), boxHeight( pr ) );
		if( isDrawn( cell ) )
			UIStyle::litEdge( pr, UIStyle::EDGE_LIT );
		else
		{
			fillBox( pr, GameMakeColor( 4, 6, 10, 180 ) );
			frameBox( pr, UIStyle::withAlpha( UIStyle::FRAME, 140 ) );
		}
	}
	const IRegion2D rule = board( DETAIL_LEFT, figureTop + 64, RIGHT, figureTop + 65 );
	UIStyle::litLine( rule.lo.x, rule.lo.y, boxWidth( rule ), 1, UIStyle::FRAME );

	// the pick's name in spaced capitals, and the line that says how the draw goes
	const Int nameTop = figureTop + 78;
	GameFont *nameFont = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE + 8, TRUE );
	const IRegion2D nameAt = board( DETAIL_LEFT, nameTop, RIGHT, nameTop + 44 );
	const UnicodeString name = UIStyle::fitText( UIStyle::capitals( pickName() ), nameFont, boxWidth( nameAt ) );
	tracked( name, nameFont, nameAt.lo.x, nameAt.lo.y + ( boxHeight( nameAt ) - heightOf( nameFont ) ) / 2, UIStyle::INK, 2.0f );
	drawIn( key++, UIStyle::fitText( TheGameText->fetch( s_custom ? "GUI:FactionPickCustomLine" : "GUI:FactionPickAllLine" ), body,
		boxWidth( nameAt ) ), body, board( DETAIL_LEFT, nameTop + 48, RIGHT, nameTop + 74 ), UIStyle::INK );

	// the armies in the draw, one column of rows while five fit, else two
	const Int listTop = drawSectionHead( TheGameText->fetch( "GUI:FactionPickInTheDraw" ), DETAIL_LEFT, nameTop + 90, RIGHT, TRUE );
	if( drawn.empty() )
		return;
	const Int columns = drawn.size() <= 5 ? 1 : 2;
	const Int perColumn = ( (Int)drawn.size() + columns - 1 ) / columns;
	const Int rowHeight = min( 120, ( BODY_BOTTOM - listTop ) / perColumn );
	const Int columnWidth = ( RIGHT - DETAIL_LEFT - ( columns - 1 ) * 20 ) / columns;
	GameFont *rowName = UIStyle::menuFont( columns == 1 ? UIStyle::MENU_TEXT_ROW_FIGURE : UIStyle::MENU_TEXT_BODY );
	GameFont *rowLine = UIStyle::menuFont( columns == 1 ? UIStyle::MENU_TEXT_HINT : UIStyle::MENU_TEXT_LABEL );
	for( Int k = 0; k < (Int)drawn.size() && key < TEXT_DETAIL + DETAIL_TEXTS - 4; k++ )
	{
		const PickerCell &cell = s_cells[ drawn[ k ] ];
		const Int column = k / perColumn, row = k % perColumn;
		const Int x0 = DETAIL_LEFT + column * ( columnWidth + 20 );
		const Int y = listTop + row * rowHeight;
		if( row % 2 )
			fillBox( board( x0, y, x0 + columnWidth, y + rowHeight - 4 ), GameMakeColor( 255, 255, 255, 6 ) );
		const Int p = rowHeight - 14;
		drawPicture( cell.icon, board( x0 + 6, y + 5, x0 + 6 + p, y + 5 + p ) );
		const Int tx = x0 + 6 + p + 16;
		const Int right = x0 + columnWidth - 8;
		drawIn( key++, UIStyle::fitText( cell.name, rowName, fpx( right - tx ) ), rowName, board( tx, y + 4, right, y + ( columns == 1 ? 34 : 28 ) ), UIStyle::INK );
		const Int lineTop = y + ( columns == 1 ? 36 : 30 );
		if( columns == 1 )
		{
			drawIn( key++, UIStyle::fitText( sentence( cell.strategy ), rowLine, fpx( right - tx ) ), rowLine, board( tx, lineTop, right, lineTop + 22 ), UIStyle::INK );
			std::vector<UnicodeString> features;
			splitLines( cell.features, features );
			UnicodeString joined;
			for( size_t f = 0; f < features.size(); f++ )
			{
				if( f > 0 )
					joined.concat( UIStyle::HEADER_JOINER );
				joined.concat( features[ f ] );
			}
			const Int room = max( 0, ( y + rowHeight - lineTop - 24 ) / 20 );
			if( room > 0 && !joined.isEmpty() )
				drawWrapped( key, joined, rowLine, tx, lineTop + 24, right, 20, min( 2, room ), UIStyle::MUTED );
			key += 2;
		}
		else
		{
			drawWrapped( key, sentence( cell.strategy ), rowLine, tx, lineTop, right, 18, max( 1, min( 2, ( rowHeight - 40 ) / 18 ) ), UIStyle::MUTED );
			key += 2;
		}
	}
}

/** One army picked: its large portrait, its name, the game's line for it and its features; under the
	* portrait the other armies of its side with their lines, to weigh it against them. */
static void drawGeneral( void )
{
	Int key = TEXT_DETAIL;
	const PickerCell &cell = s_cells[ s_focus ];
	drawPicture( cell.portrait ? cell.portrait : cell.icon, board( DETAIL_LEFT, TABS_TOP, DETAIL_LEFT + PORTRAIT, TABS_TOP + PORTRAIT ) );
	const Int x = DETAIL_LEFT + PORTRAIT + 24;
	GameFont *nameFont = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE + 4, TRUE );
	std::vector<UnicodeString> lines;
	UIStyle::wrapLines( UIStyle::capitals( cell.name ), fpx( RIGHT - x ), measureIn, nameFont, lines );
	Int y = TABS_TOP - 4;
	for( Int k = 0; k < (Int)lines.size() && k < 2; k++ )
	{
		const IRegion2D at = board( x, y, RIGHT, y + 36 );
		tracked( lines[ k ], nameFont, at.lo.x, at.lo.y + ( boxHeight( at ) - heightOf( nameFont ) ) / 2, UIStyle::INK, 1.6f );
		y += 36;
	}
	GameFont *body = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY );
	if( !cell.strategy.isEmpty() )
	{
		y = drawWrapped( key, sentence( cell.strategy ), body, x, y + 6, RIGHT, 25, 4, UIStyle::INK ) + 14;
		key += 4;
	}
	std::vector<UnicodeString> features;
	splitLines( cell.features, features );
	if( !features.empty() )
	{
		y = drawSectionHead( TheGameText->fetch( "GUI:FactionPickFeatures" ), x, y, RIGHT, TRUE ) + 2;
		const Int rowHeight = max( 20, min( 30, ( TABS_TOP + PORTRAIT - y ) / (Int)features.size() ) );
		GameFont *featureFont = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT + 1 );
		for( size_t f = 0; f < features.size() && key < TEXT_DETAIL + 40; f++ )
		{
			const IRegion2D dot = board( x + 2, y + rowHeight / 2 - 2, x + 8, y + rowHeight / 2 + 2 );
			fillBox( dot, UIStyle::EDGE_LIT );
			drawIn( key++, UIStyle::fitText( features[ f ], featureFont, fpx( RIGHT - x - 20 ) ), featureFont, board( x + 20, y, RIGHT, y + rowHeight ), UIStyle::INK );
			y += rowHeight;
		}
	}

	// the other armies of the side under the portrait
	const PickerGroup *side = NULL;
	for( size_t group = 0; group < s_groups.size(); group++ )
		for( size_t each = 0; each < s_groups[ group ].cells.size(); each++ )
			if( s_groups[ group ].cells[ each ] == s_focus )
				side = &s_groups[ group ];
	if( side == NULL || side->cells.size() < 2 )
		return;
	UnicodeString head;
	head.format( TheGameText->fetch( "GUI:FactionPickOthers" ).str(), side->name.str() );
	const Int listTop = drawSectionHead( head, DETAIL_LEFT, TABS_TOP + PORTRAIT + 22, RIGHT, TRUE );
	const Int others = (Int)side->cells.size() - 1;
	const Int rowHeight = min( 90, ( BODY_BOTTOM - listTop ) / others );
	GameFont *rowName = UIStyle::menuFont( UIStyle::MENU_TEXT_BODY + 2 );
	GameFont *rowLine = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	Int row = 0;
	key = TEXT_DETAIL + 48;
	for( size_t each = 0; each < side->cells.size(); each++ )
	{
		if( side->cells[ each ] == s_focus )
			continue;
		const PickerCell &other = s_cells[ side->cells[ each ] ];
		const Int top = listTop + row * rowHeight;
		if( row % 2 )
			fillBox( board( DETAIL_LEFT, top, RIGHT, top + rowHeight - 4 ), GameMakeColor( 255, 255, 255, 6 ) );
		const Int p = rowHeight - 14;
		drawPicture( other.icon, board( DETAIL_LEFT + 6, top + 5, DETAIL_LEFT + 6 + p, top + 5 + p ) );
		const Int tx = DETAIL_LEFT + p + 24;
		drawIn( key++, UIStyle::fitText( other.name, rowName, fpx( RIGHT - tx ) ), rowName, board( tx, top + 4, RIGHT, top + 30 ), UIStyle::INK );
		drawIn( key++, UIStyle::fitText( sentence( other.strategy ), rowLine, fpx( RIGHT - tx ) ), rowLine, board( tx, top + 32, RIGHT, top + 54 ), UIStyle::MUTED );
		++row;
	}
}

/// the hovered tile's tooltip over it: its name and the game's line for it
static void drawTooltip( Int index )
{
	const PickerCell &cell = s_cells[ index ];
	const UnicodeString words = cell.strategy.isEmpty() ? TheGameText->fetch( "GUI:FactionPickObserverLine" ) : sentence( cell.strategy );
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	std::vector<UnicodeString> lines;
	UIStyle::wrapLines( words, fpx( TIP_WIDTH - 28 ), measureIn, font, lines );
	const Int count = min( 6, (Int)lines.size() );
	const UIStyle::BoardFit fit = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() );
	const Real scale = fit.scale > 0.0f ? fit.scale : 1.0f;
	const Int tileLeft = REAL_TO_INT( ( cell.box.lo.x - fit.x ) / scale + 0.5f );
	const Int tileTop = REAL_TO_INT( ( cell.box.lo.y - fit.y ) / scale + 0.5f );
	const Int tileBottom = REAL_TO_INT( ( cell.box.hi.y - fit.y ) / scale + 0.5f );
	const Int height = 14 + 26 + 21 * count + 10;
	const Int x = min( tileLeft, PANEL_RIGHT - 20 - TIP_WIDTH );
	const Int y = tileTop - height - 8 > TABS_BOTTOM ? tileTop - height - 8 : tileBottom + 8;
	fillBox( board( x + 6, y + 6, x + TIP_WIDTH + 6, y + height + 6 ), GameMakeColor( 0, 0, 0, 110 ) );
	const IRegion2D r = board( x, y, x + TIP_WIDTH, y + height );
	fillBox( r, GameMakeColor( 4, 6, 12, 240 ) );
	frameBox( r, UIStyle::EDGE );
	drawIn( TEXT_TIP, cell.name, UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ), board( x + 14, y + 10, x + TIP_WIDTH - 14, y + 36 ), UIStyle::INK );
	for( Int k = 0; k < count; k++ )
		drawIn( TEXT_TIP + 1 + k, lines[ k ], font, board( x + 14, y + 40 + k * 21, x + TIP_WIDTH - 14, y + 40 + ( k + 1 ) * 21 ), UIStyle::MUTED );
}

static void drawKey( Int key, const IRegion2D &box, const UnicodeString &words, Bool enabled, Bool primary, const ICoord2D &mouse )
{
	DisplayString *label = text( key, words, UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE ) );
	const UIStyle::KeyState state = !enabled ? UIStyle::KEY_DISABLED : ( isInside( box, mouse.x, mouse.y ) ? UIStyle::KEY_HOVER : UIStyle::KEY_NORMAL );
	UIStyle::menuKey( box, label, state, primary );
}

static void removeWindow( void );

static void drawPicker( GameWindow *window, WinInstanceData *instData )
{
	// a closed screen draws nothing, and the frame after it closed it goes (closePicker says why)
	if( s_sinking )
	{
		if( TheGameClient == NULL || TheGameClient->getFrame() != s_sinkFrame )
			removeWindow();
		return;
	}
	// the room stays up underneath for its updates, so the screen frosts all of its panel over it
	const IRegion2D panel = board( PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM );
	UIStyle::glass( panel, 230 );
	UIStyle::frameA( panel );
	UIStyle::menuHeader( text( TEXT_TITLE, UIStyle::capitals( TheGameText->fetch( "GUI:FactionPickTitle" ) ),
		UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE, TRUE ) ), NULL );

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	static const char *const tabs[] = { "GUI:FactionPickRandomAll", "GUI:FactionPickCustom" };
	const Bool tabOn[ 2 ] = { s_focus < 0 && !s_custom, s_custom };
	for( Int tab = 0; tab < 2; tab++ )
	{
		DisplayString *label = text( TEXT_TABS + tab, UIStyle::capitals( TheGameText->fetch( tabs[ tab ] ) ),
			UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE ) );
		UIStyle::menuTab( s_tabBox[ tab ], label, tabOn[ tab ], !tabOn[ tab ] && isInside( s_tabBox[ tab ], mouse.x, mouse.y ) );
	}

	for( size_t group = 0; group < s_groups.size(); group++ )
		drawGroupHead( (Int)group, mouse );
	Int hovered = -1;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		const Bool over = isInside( s_cells[ each ].box, mouse.x, mouse.y );
		drawTile( (Int)each, over );
		if( over )
			hovered = (Int)each;
	}

	const IRegion2D divider = board( DIVIDER, TABS_TOP, DIVIDER + 1, BODY_BOTTOM );
	fillBox( divider, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	if( s_focus >= 0 )
		drawGeneral();
	else
		drawTheDraw();

	// the foot: its rule, BACK at the left, CONFIRM lit on Play Game's rect, the keys' hint between
	const IRegion2D rule = board( LEFT, FOOT_RULE, RIGHT, FOOT_RULE + 1 );
	fillBox( rule, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	drawKey( TEXT_BACK, s_backBox, UIStyle::capitals( TheGameText->fetch( "GUI:Back" ) ), TRUE, FALSE, mouse );
	GameFont *keyFont = UIStyle::menuFont( UIStyle::MENU_TEXT_KEY, TRUE );
	UnicodeString confirm;
	confirm.format( TheGameText->fetch( "GUI:FactionPickConfirm" ).str(), pickName().str() );
	confirm = UIStyle::capitals( confirm );
	if( measureIn( confirm, keyFont ) > boxWidth( s_confirmBox ) - fpx( 24 ) )
	{
		// a long general's name: the key says CONFIRM alone, the pick is named over it on the right
		confirm.format( TheGameText->fetch( "GUI:FactionPickConfirm" ).str(), UnicodeString::TheEmptyString.str() );
		confirm.trim();
		confirm = UIStyle::capitals( confirm );
	}
	drawKey( TEXT_CONFIRM, s_confirmBox, confirm, canConfirm(), TRUE, mouse );
	GameFont *hintFont = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	const Int hintLeft = BACK_RIGHT + 16;
	drawIn( TEXT_HINT, UIStyle::fitKey( s_custom ? "GUI:FactionPickHintTick" : "GUI:FactionPickHintPlay", hintFont, fpx( CONFIRM_LEFT - 12 - hintLeft ) ),
		hintFont, board( hintLeft, FOOT_MIDDLE - 12, CONFIRM_LEFT - 12, FOOT_MIDDLE + 12 ), UIStyle::MUTED );

	if( hovered >= 0 )
		drawTooltip( hovered );
}

//-------------------------------------------------------------------------------------------------
/// the screen's window goes, and the room under it has the mouse and the keys again
static void removeWindow( void )
{
	if( s_window == NULL )
		return;
	// hiding the modal window pops it off the modal stack itself; a second unset only logs a complaint
	s_window->winHide( TRUE );
	TheWindowManager->winDestroy( s_window );
	s_window = NULL;
	s_sinking = FALSE;
	// the room takes clicks again, unless it is the one going away
	if( s_room && s_roomWasEnabled && !BitTest( s_room->winGetStatus(), WIN_STATUS_DESTROYED ) )
		s_room->winSetStatus( WIN_STATUS_ENABLED );
	s_room = NULL;
}

/** The screen closes.  Its window stays modal, drawing nothing, until the next frame (drawPicker takes
	* it away): BACK and CONFIRM stand on Play Game's rectangle, and the room's key under them is not to
	* see a single message of the click or the key that closed the screen, whatever else of it the same
	* frame still carries.  now takes it away at once, for the room going away under it. */
static void closePicker( Bool restoreFocus, Bool now = FALSE )
{
	if( s_window == NULL || s_sinking )
	{
		if( now )
			removeWindow();
		return;
	}
	s_sinking = TRUE;
	s_sinkFrame = TheGameClient ? TheGameClient->getFrame() : 0;
	if( now )
		removeWindow();
	if( restoreFocus )
		TheWindowManager->winSetFocus( s_oldFocus );
	s_oldFocus = NULL;

	s_focus = -1;
	s_pressedHere = FALSE;
	s_cells.clear();
	s_groups.clear();
	for( Int k = 0; k < TEXT_COUNT; k++ )
	{
		if( s_texts[ k ] )
			TheDisplayStringManager->freeDisplayString( s_texts[ k ] );
		s_texts[ k ] = NULL;
	}
	if( s_measure )
		TheDisplayStringManager->freeDisplayString( s_measure );
	s_measure = NULL;
}

/** The box's Random line carries the seat's pool in its item data and says which in its text, so
	* every seat in the room, not only whoever opens the screen, reads a Custom Selection pool as one.
	* The box's own text is the line's, copied on every selection of it. */
static void setRandomLine( GameWindow *combo, Int line, Int pick )
{
	GadgetComboBoxSetItemData( combo, line, (void *)(intptr_t)pick );
	GameWindow *listBox = GadgetComboBoxGetListBox( combo );
	Color color;
	GadgetListBoxGetTextAndColor( listBox, &color, line, 0 );
	GadgetListBoxAddEntryText( listBox, TheGameText->fetch( pick < PLAYERTEMPLATE_RANDOM_POOL ? "GUI:FactionPickCustom" : "GUI:Random" ), color, line, 0 );
}

/** The pick goes in through the box: its line selected and the room told, as a pick from its list
	* was.  A Random rides on the box's Random line, whose item data carries the pool. */
static void confirm( void )
{
	if( !canConfirm() )
		return;
	const Int pick = s_focus >= 0 ? s_cells[ s_focus ].pick : ( s_custom ? EncodeRandomPool( s_mask & listedMask() ) : (Int)PLAYERTEMPLATE_RANDOM );
	AsciiString pickedName;
	if( s_focus >= 0 )
		pickedName.translate( s_cells[ s_focus ].name );
	DEBUG_LOG(( "FACTIONPICKER confirm pick %d \"%s\"\n", pick, pickedName.str() ));
	GameWindow *combo = s_combo;
	closePicker( TRUE );

	// the host may have taken the seat away while the screen was up
	if( !BitTest( combo->winGetStatus(), WIN_STATUS_ENABLED ) )
		return;
	for( Int line = 0; line < GadgetComboBoxGetLength( combo ); line++ )
	{
		const Int data = (Int)(intptr_t)GadgetComboBoxGetItemData( combo, line );
		if( data != pick && !( IsRandomPlayerTemplate( pick ) && IsRandomPlayerTemplate( data ) ) )
			continue;
		if( IsRandomPlayerTemplate( pick ) )
			setRandomLine( combo, line, pick );
		GadgetComboBoxSetSelectedPos( combo, line, TRUE );
		// the list only reports a change of line, and Random to a Random pool is none
		TheWindowManager->winSendSystemMsg( combo->winGetOwner(), GCM_SELECTED, (WindowMsgData)combo, 0 );
		return;
	}
}

/// a side's RANDOM key: Random from that side alone ("Random USA")
static void narrowToSide( Int side )
{
	s_custom = TRUE;
	s_focus = -1;
	s_mask = s_groups[ side ].mask;
}

static void pressAt( Int x, Int y )
{
	if( isInside( s_backBox, x, y ) )
	{
		click();
		closePicker( TRUE );
		return;
	}
	if( isInside( s_confirmBox, x, y ) )
	{
		if( !canConfirm() )
			return;
		click();
		confirm();
		return;
	}
	if( isInside( s_tabBox[ 0 ], x, y ) )
	{
		click();
		s_custom = FALSE;
		s_focus = -1;
		return;
	}
	if( isInside( s_tabBox[ 1 ], x, y ) )
	{
		click();
		s_custom = !s_custom;
		s_focus = -1;
		return;
	}
	const Int tickedSide = s_custom ? sideAt( x, y, TRUE ) : -1;
	if( tickedSide >= 0 )
	{
		click();
		s_mask = FactionPickerToggleSide( s_mask & listedMask(), s_groups[ tickedSide ].mask );
		return;
	}
	const Int keyedSide = sideAt( x, y, FALSE );
	if( keyedSide >= 0 && hasSideKey( s_groups[ keyedSide ] ) )
	{
		click();
		narrowToSide( keyedSide );
		return;
	}
	const Int each = cellAt( x, y );
	if( each < 0 )
		return;
	click();
	if( s_custom && s_cells[ each ].poolBit >= 0 )
		s_mask ^= 1u << s_cells[ each ].poolBit;
	else
	{
		s_custom = FALSE;
		s_focus = each;
	}
}

/** The window manager hands the second press of a double click on as a plain press, so the pair is
	* timed here, on the release: a confirm on the press would leave its release to the room. */
static void releaseAt( Int x, Int y )
{
	const Int cell = cellAt( x, y );
	const UnsignedInt now = Clock_Milliseconds();
	const Bool second = cell >= 0 && cell == s_lastPressCell && now - s_lastPressTime <= TheGlobalData->m_doubleClickTimeMS;
	s_lastPressCell = second ? -1 : cell;
	s_lastPressTime = now;
	if( second && !s_custom )
	{
		s_focus = cell;
		confirm();
		return;
	}
	pressAt( x, y );
}

static WindowMsgHandledType pickerInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	const Int x = (Int)( mData1 & 0xFFFF );
	const Int y = (Int)( ( mData1 >> 16 ) & 0xFFFF );
	// closed and waiting for the next frame: whatever still comes is the room's no more than before
	if( s_sinking )
		return MSG_HANDLED;
	switch( msg )
	{
		case GWM_CHAR:
			if( BitTest( mData2, KEY_STATE_UP ) )
			{
				if( mData1 == KEY_ESC )
					closePicker( TRUE );
				else if( mData1 == KEY_ENTER || mData1 == KEY_KPENTER )
					confirm();
			}
			break;
		case GWM_LEFT_DOWN:
			s_pressedHere = TRUE;
			break;
		case GWM_LEFT_UP:
			DEBUG_LOG(( "FACTIONPICKER release at %d,%d, its press %s\n", x, y, s_pressedHere ? "here" : "elsewhere" ));
			// only the release of a press the screen took acts: the click that opened it ended on the box
			if( s_pressedHere )
				releaseAt( x, y );
			s_pressedHere = FALSE;
			break;
	}
	// nothing under the screen sees the mouse or the keys while it is up
	return MSG_HANDLED;
}

static WindowMsgHandledType pickerSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_INPUT_FOCUS )
	{
		if( mData1 == TRUE )
			*(Bool *)mData2 = TRUE;
		return MSG_HANDLED;
	}
	return GameWinDefaultSystem( window, msg, mData1, mData2 );
}

/// the screen's name, which the window tree (ControlServer's uitree) shows and finds it by
static const char *const PICKER_WINDOW_NAME = "FactionPicker.wnd:FactionPicker";

static void openPicker( GameWindow *combo )
{
	if( s_window )
		return;
	s_combo = combo;
	// the box's own list never shows under the screen, nor holds the window manager's open window
	GadgetComboBoxHideList( combo );
	if( TheWindowManager->winGetLoneWindow() == combo )
		TheWindowManager->winSetLoneWindow( NULL );
	/* The room under the screen takes no click while it is up, whatever way a click looks for a window:
		 in the UI walk a click on CONFIRM reached the room's Play Game, standing on the same rectangle,
		 and started the match without the screen ever confirming.  Its top window is disabled, which no
		 child under it survives (the window list skips a top window that is not enabled), and enabled
		 again once the screen has gone.  Only its own flag: winEnable would set every child's too, and
		 enabling them again would wake keys the room keeps off (Reset, a joiner's boxes). */
	s_room = combo;
	while( s_room->winGetParent() )
		s_room = s_room->winGetParent();
	s_roomWasEnabled = BitTest( s_room->winGetStatus(), WIN_STATUS_ENABLED );
	if( s_roomWasEnabled )
		s_room->winClearStatus( WIN_STATUS_ENABLED );
	buildCells();

	// the box's Random line remembers the seat's last pool, so a seat on an army keeps it too
	Int current = PLAYERTEMPLATE_RANDOM;
	Int line = 0;
	GadgetComboBoxGetSelectedPos( combo, &line );
	if( line >= 0 )
		current = (Int)(intptr_t)GadgetComboBoxGetItemData( combo, line );
	Int random = PLAYERTEMPLATE_RANDOM;
	for( Int each = 0; each < GadgetComboBoxGetLength( combo ); each++ )
	{
		const Int data = (Int)(intptr_t)GadgetComboBoxGetItemData( combo, each );
		if( IsRandomPlayerTemplate( data ) )
			random = data;
	}
	s_mask = DecodeRandomPool( random );
	s_custom = current < PLAYERTEMPLATE_RANDOM_POOL;
	s_focus = -1;
	s_lastPressCell = -1;
	s_pressedHere = FALSE;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( s_cells[ each ].pick == current )
			s_focus = (Int)each;
	}
	layout();

	s_window = TheWindowManager->winCreate( NULL, WIN_STATUS_ENABLED | WIN_STATUS_ABOVE, 0, 0,
		TheDisplay->getWidth(), TheDisplay->getHeight(), pickerSystem );
	s_window->winGetInstanceData()->m_decoratedNameString = PICKER_WINDOW_NAME;
	s_window->winSetWindowId( NAMEKEY( AsciiString( PICKER_WINDOW_NAME ) ) );
	s_window->winSetDrawFunc( drawPicker );
	s_window->winSetInputFunc( pickerInput );
	s_oldFocus = TheWindowManager->winGetFocus();
	TheWindowManager->winSetModal( s_window );
	TheWindowManager->winSetFocus( s_window );
}

//-------------------------------------------------------------------------------------------------
// the seat's box: a click on it or on its arrow opens the screen instead of the list
static WindowMsgHandledType comboInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	// the press is the box's own, so its release comes back here and nowhere in the room
	if( msg == GWM_LEFT_DOWN )
		return MSG_HANDLED;
	if( msg == GWM_LEFT_UP )
	{
		click();
		openPicker( window );
		return MSG_HANDLED;
	}
	return GadgetComboBoxInput( window, msg, mData1, mData2 );
}

static WindowMsgHandledType comboSystem( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GBM_SELECTED && (GameWindow *)mData1 == GadgetComboBoxGetDropDownButton( window ) )
	{
		openPicker( window );
		return MSG_HANDLED;
	}
	return GadgetComboBoxSystem( window, msg, mData1, mData2 );
}

void FactionPickerAttach( GameWindow *parent, const char *layoutFilename )
{
	GameWindow *first = NULL;
	for( Int i = 0; i < MAX_SLOTS; ++i )
	{
		AsciiString name;
		name.format( "%s:ComboBoxPlayerTemplate%d", layoutFilename, i );
		GameWindow *combo = TheWindowManager->winGetWindowFromId( parent, NAMEKEY( name ) );
		if( !combo )
			continue;	// a layout with fewer seats
		combo->winSetInputFunc( comboInput );
		combo->winSetSystemFunc( comboSystem );
		if( !first )
			first = combo;
	}
	if( s_openOnStart && first )
	{
		s_openOnStart = FALSE;
		openPicker( first );
		if( s_openOnStartCustom )
		{
			s_custom = TRUE;
			s_focus = -1;
		}
		for( size_t group = 0; group < s_groups.size(); group++ )
		{
			if( !s_openOnStartSide.isEmpty() && s_groups[ group ].leadSide.compareNoCase( s_openOnStartSide ) == 0 )
				narrowToSide( (Int)group );
		}
	}
}

void FactionPickerDetach( void )
{
	if( s_window )
		closePicker( FALSE, TRUE );
	s_combo = NULL;
}

void FactionPickerSyncRandom( GameWindow *combo, Int pick )
{
	if( !IsRandomPlayerTemplate( pick ) )
		return;
	for( Int line = 0; line < GadgetComboBoxGetLength( combo ); line++ )
	{
		if( IsRandomPlayerTemplate( (Int)(intptr_t)GadgetComboBoxGetItemData( combo, line ) ) )
		{
			setRandomLine( combo, line, pick );
			return;
		}
	}
}

void FactionPickerOpenOnStart( Bool custom, const char *side )
{
	s_openOnStart = TRUE;
	s_openOnStartCustom = custom;
	s_openOnStartSide = side;
}

Bool FactionPickerSideTicked( UnsignedInt pool, UnsignedInt side )
{
	return side != 0 && ( pool & side ) == side;
}

UnsignedInt FactionPickerToggleSide( UnsignedInt pool, UnsignedInt side )
{
	return FactionPickerSideTicked( pool, side ) ? pool & ~side : pool | side;
}

Int FactionPickerThumbTop( Int scroll, Int scrollMax, Int track, Int thumb )
{
	return scrollMax > 0 ? scroll * ( track - thumb ) / scrollMax : 0;
}

Int FactionPickerScrollAtThumb( Int thumbTop, Int scrollMax, Int track, Int thumb )
{
	const Int travel = track - thumb;
	return max( 0, min( thumbTop, travel ) ) * scrollMax / travel;
}
