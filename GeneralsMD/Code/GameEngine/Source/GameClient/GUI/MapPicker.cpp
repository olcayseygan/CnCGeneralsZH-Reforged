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

// FILE: MapPicker.cpp ////////////////////////////////////////////////////////////////////////////
//
// The map pickers' grid (MapPicker.h).  Each listbox keeps everything EA's list kept: its rows in
// the game's order (fewest players first, then by shown name), each row's map path in its item data,
// skirmish's medal in its second column, the selection.  Only its drawing and its mouse are replaced:
// a tile a row, the map's own preview square at the top and its name under it, a player chip on the
// picture and the best win on its foot.  A click selects the row through the listbox, so the picker's
// own GLM_SELECTED handler moves the preview as it always did; a second click within the double-click
// time sends the listbox's GLM_DOUBLE_CLICKED, which accepts.  Everything is laid out on the 1080 board
// (UIStyle::boardRect) by the rects of README-pickers.md, which Tools/mapselect_layout.py also uses.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/Clock.h"

#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Image.h"
#include "GameClient/MapPicker.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"

#include <math.h>
#include <vector>

// the picker's rects on the 1080 board (README-pickers.md)
static const Int PICK_LEFT = 60;
static const Int PICK_GRID_RIGHT = 1276;		///< the tiles; the scroll groove stands at 1288 to 1294
static const Int PICK_GROOVE_LEFT = 1288;
static const Int PICK_RIGHT = 1294;					///< the seat column's right edge
static const Int PICK_TABS_BOTTOM = 166;
static const Int PICK_MAPS_TOP = 182;
static const Int PICK_TYPES_TOP = 306;			///< Tools/mapselect_layout.py's TYPE_TOP
static const Int PICK_BOTTOM = 952;
static const Int PICK_CROWD_LINE = 44;			///< a crowded room's line under the tabs moves the grid down
static const Int PICK_COLUMN_LEFT = 1318;		///< the room's map and rules column
static const Int PICK_COLUMN_RIGHT = 1860;
static const Int PICK_SQUARE_WORDS = 1666;	///< beside a square preview, the room's x + 348
static const Int PICK_FOOT_MIDDLE = 1002;
static const Int PICK_FOOT_RIGHT = 1306;		///< 12 before Cancel
static const Int PICK_TIP_WIDTH = 440;

enum { GRID_MAPS, GRID_TYPES, GRID_COUNT };
enum { CELL_TILE, CELL_TENANT, CELL_ADD };

/* The grid's own types live in an unnamed namespace.  At namespace scope FactionPicker.cpp's PickerCell
	 (a struct of UnicodeStrings) and this file's (plain ints) were one name with two layouts, and the
	 linker kept one std::vector<PickerCell> growth path for both: the first push_back past capacity in
	 layoutCells copied this file's ints as UnicodeStrings and the game fell over on Select Map. */
namespace
{

/// a row's tile as it was last measured: its picture and its name broken into lines for a width
struct PickerRow
{
	Bool asked;
	const Image *picture;
	Int width;					///< the screen width the name was broken for, 0 while not yet
	Int size;						///< the name's size at 1080
	Int lines;
	UnicodeString line[ 2 ];
};

struct PickerGrid
{
	GameWindow *list;
	Int first;					///< the first row of tiles on screen
	Int entries;				///< the list's count when the rows were last taken
	const void *head;		///< its first row's item data then: a new count or head is a new fill
	Int pressRow;
	UnsignedInt pressTime;
	Int grab;						///< board pixels between the pointer and the thumb's top while it is dragged, -1 none
	Bool pressed;				///< the left press this release ends came to the grid
	std::vector< PickerRow > rows;
};

/// a place in the grid's area, on the board
struct PickerCell
{
	Int left, top, right, bottom;
	Int kind;						///< CELL_TILE (index a row), CELL_TENANT (index a tab), CELL_ADD
	Int index;
	Int picture;				///< the picture's side at 1080
};

}	// namespace

static PickerGrid thePickerGrids[ GRID_COUNT ];
static GameWindow *thePickerPreview = NULL;
static GameWindow *thePickerSizeHead = NULL;
static GameWindow *thePickerTypeHead = NULL;
static const GameInfo *thePickerRoom = NULL;
static Int thePickerMinPlayers = 0;
static Bool thePickerSkirmish = FALSE;
static Int thePickerCounts[ 2 ] = { 0, 0 };		///< Official's and Unofficial's maps, as the init found them

// one display string a place on the screen, so a string's texture is rebuilt only when its words change
enum
{
	TEXT_TABS = 0,				///< three labels and three tails, then the five size boxes
	TEXT_HEADS = 12,
	TEXT_CROWD = 14,			///< two lines
	TEXT_TILES = 16,			///< five a tile on screen: two name lines, the player chip, the medal chip, the "?"
	TILE_TEXTS = 5,
	TILE_SLOTS = 72,
	TEXT_TENANTS = TEXT_TILES + TILE_TEXTS * TILE_SLOTS,	///< four a tenant: two label lines, two lines
	TEXT_EMPTY = TEXT_TENANTS + 8,												///< the head, three folder lines, ADD YOUR OWN
	TEXT_WORDS = TEXT_EMPTY + 8,													///< the picked map's: name (2), facts (3), medal, next, tag, line (3)
	TEXT_FOOT = TEXT_WORDS + 16,
	TEXT_TIP = TEXT_FOOT + 2,															///< the title and six lines
	TEXT_COUNT = TEXT_TIP + 8
};
static DisplayString *thePickerTexts[ TEXT_COUNT ];
static DisplayString *thePickerMeasure = NULL;

//-------------------------------------------------------------------------------------------------
static DisplayString *pickText( Int key, const UnicodeString &words, GameFont *font )
{
	if( key < 0 || key >= TEXT_COUNT )
		return NULL;
	DisplayString *&s = thePickerTexts[ key ];
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
	if( thePickerMeasure == NULL )
		thePickerMeasure = TheDisplayStringManager->newDisplayString();
	if( thePickerMeasure->getFont() != (GameFont *)font )
		thePickerMeasure->setFont( (GameFont *)font );
	thePickerMeasure->setText( words );
	return thePickerMeasure->getWidth( -1 );
}

static Int lineHeight( GameFont *font )
{
	if( thePickerMeasure == NULL )
		thePickerMeasure = TheDisplayStringManager->newDisplayString();
	thePickerMeasure->setFont( font );
	thePickerMeasure->setText( UnicodeString( u"Ag" ) );
	Int w = 0, h = 0;
	thePickerMeasure->getSize( &w, &h );
	return h;
}

static Int pickPx( Int length ) { return UIStyle::pxOn( (Real)length, TheDisplay->getWidth(), TheDisplay->getHeight() ); }
static IRegion2D board( Int l, Int t, Int r, Int b ) { return UIStyle::boardRect( l, t, r, b ); }
static Int boxW( const IRegion2D &b ) { return b.hi.x - b.lo.x; }
static Int boxH( const IRegion2D &b ) { return b.hi.y - b.lo.y; }
static Bool inside( const IRegion2D &b, Int x, Int y ) { return x >= b.lo.x && x < b.hi.x && y >= b.lo.y && y < b.hi.y; }
static const Color PICK_SHADOW = GameMakeColor( 0, 0, 0, 160 );

static void fillBox( const IRegion2D &b, Color color )
{
	if( boxW( b ) > 0 && boxH( b ) > 0 )
		TheDisplay->drawFillRect( b.lo.x, b.lo.y, boxW( b ), boxH( b ), color );
}

static void frameBox( const IRegion2D &b, Color color )
{
	TheDisplay->drawOpenRect( b.lo.x, b.lo.y, boxW( b ), boxH( b ), 1.0f, color );
}

/// words centred in a screen box, across or only down (across FALSE: from its left)
static void drawIn( Int key, const UnicodeString &words, GameFont *font, const IRegion2D &b, Color color, Bool across )
{
	DisplayString *s = pickText( key, words, font );
	if( s == NULL || words.isEmpty() )
		return;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( across ? ( b.lo.x + b.hi.x - w ) / 2 : b.lo.x, ( b.lo.y + b.hi.y - h ) / 2, color, PICK_SHADOW );
}

static UnicodeString partOf( const UnicodeString &text, Int from, Int count )
{
	UnicodeString out;
	for( Int each = from; each < from + count && each < text.getLength(); each++ )
		out.concat( text.getCharAt( each ) );
	return out;
}

static UnicodeString formatted( const char *key, Int value )
{
	UnicodeString out;
	out.format( TheGameText->fetch( key ).str(), value );
	return out;
}

static void click( void )
{
	AudioEventRTS sound( "GUIClick" );
	TheAudio->addAudioEvent( &sound );
}

/// the medal's star: ten strokes round its points and five from its middle to them, so it reads filled
static void drawStar( Int cx, Int cy, Int radius, Color color )
{
	Int xs[ 10 ], ys[ 10 ];
	for( Int k = 0; k < 10; k++ )
	{
		const Real angle = -1.5707963f + k * 0.6283185f;
		const Real reach = ( k % 2 ) ? radius * 0.42f : (Real)radius;
		xs[ k ] = cx + REAL_TO_INT( cosf( angle ) * reach );
		ys[ k ] = cy + REAL_TO_INT( sinf( angle ) * reach );
	}
	const Real line = (Real)max( 1, pickPx( 2 ) );
	for( Int k = 0; k < 10; k++ )
		TheDisplay->drawLine( xs[ k ], ys[ k ], xs[ ( k + 1 ) % 10 ], ys[ ( k + 1 ) % 10 ], line, color );
	for( Int k = 0; k < 10; k += 2 )
		TheDisplay->drawLine( cx, cy, xs[ k ], ys[ k ], line, color );
}

/// the pick's tick in a lit square
static void drawTickSquare( const IRegion2D &b )
{
	fillBox( b, UIStyle::EDGE_LIT );
	const Int s = boxW( b );
	const Real line = (Real)max( 2, pickPx( 3 ) );
	const Color dark = GameMakeColor( 8, 12, 24, 255 );
	TheDisplay->drawLine( b.lo.x + s / 4, b.lo.y + s / 2, b.lo.x + s * 2 / 5, b.lo.y + s * 3 / 4 - 1, line, dark );
	TheDisplay->drawLine( b.lo.x + s * 2 / 5, b.lo.y + s * 3 / 4 - 1, b.hi.x - s / 5, b.lo.y + s / 4, line, dark );
}

//-------------------------------------------------------------------------------------------------
MapPickerShape MapPickerGridShape( Int left, Int right, Int top, Int bottom, Int startColumns )
{
	MapPickerShape best = { max( 1, startColumns ), 0, 0, 1 };
	Int bestLeft = -1;
	const Int height = bottom - top;
	for( Int columns = max( 1, startColumns ); columns < max( 1, startColumns ) + 4; columns++ )
	{
		const Int width = ( right - left - ( columns - 1 ) * MAP_PICKER_GAP ) / columns;
		const Int natural = width - 2 * MAP_PICKER_INSET + MAP_PICKER_NAME_ROOM;
		const Int rows = max( 1, ( height + MAP_PICKER_GAP ) / ( natural + MAP_PICKER_GAP ) );
		const Int over = height - rows * natural - ( rows - 1 ) * MAP_PICKER_GAP;
		if( over < 0 )
			continue;		// one row taller than the area: its pictures would not fit
		if( bestLeft < 0 || over < bestLeft )
		{
			bestLeft = over;
			best.columns = columns;
			best.tileWidth = width;
			best.rows = rows;
		}
	}
	if( bestLeft < 0 )
	{
		// an area lower than any tile: one row, the pictures as tall as it lets them be
		best.tileWidth = ( right - left - ( best.columns - 1 ) * MAP_PICKER_GAP ) / best.columns;
		best.rows = 1;
	}
	best.tileHeight = ( height - ( best.rows - 1 ) * MAP_PICKER_GAP ) / best.rows;
	return best;
}

Int MapPickerFirstRow( Int picked, Int columns, Int rows, Int total )
{
	if( columns <= 0 || rows <= 0 )
		return 0;
	const Int allRows = ( total + columns - 1 ) / columns;
	const Int last = max( 0, allRows - rows );
	if( picked < 0 )
		return 0;
	return min( max( 0, picked / columns - rows + 1 ), last );
}

//-------------------------------------------------------------------------------------------------
static PickerGrid *gridOf( GameWindow *window )
{
	for( Int g = 0; g < GRID_COUNT; g++ )
	{
		if( window != NULL && thePickerGrids[ g ].list == window )
			return &thePickerGrids[ g ];
	}
	return NULL;
}

static Int kindOf( const PickerGrid &g ) { return &g == &thePickerGrids[ GRID_TYPES ] ? GRID_TYPES : GRID_MAPS; }

static Int textColumn( GameWindow *list ) { return max( 0, GadgetListBoxGetNumColumns( list ) - 1 ); }

/// the map a row stands for: the list's item data, the type list's shipped map (empty for Surprise me)
static AsciiString rowMap( const PickerGrid &g, Int row )
{
	if( kindOf( g ) == GRID_TYPES )
		return RandomMapTabTemplateAt( row );
	const char *path = (const char *)GadgetListBoxGetItemData( g.list, row, 0 );
	return path ? AsciiString( path ) : AsciiString::TheEmptyString;
}

/// skirmish's best win on the map: 0 none, 1 Easy, 2 Medium, 3 Hard, 4 Hard in every other seat; -1 no medals
static Int rowMedal( const PickerGrid &g, Int row )
{
	if( kindOf( g ) != GRID_MAPS || !thePickerSkirmish || GadgetListBoxGetNumColumns( g.list ) < 2 )
		return -1;
	return (Int)(intptr_t)GadgetListBoxGetItemData( g.list, row, 1 );
}

static Int selectedRow( const PickerGrid &g )
{
	Int selected = -1;
	if( g.list )
		GadgetListBoxGetSelected( g.list, &selected );
	return selected;
}

static Bool crowded( const PickerGrid &g )
{
	return kindOf( g ) == GRID_MAPS && thePickerMinPlayers > 0;
}

static Int gridTop( const PickerGrid &g )
{
	if( kindOf( g ) == GRID_TYPES )
		return PICK_TYPES_TOP;
	return PICK_MAPS_TOP + ( crowded( g ) ? PICK_CROWD_LINE : 0 );
}

static Bool tabOpen( GameWindow *tab )
{
	return tab && BitTest( tab->winGetInstanceData()->getState(), WIN_STATE_SELECTED );
}

static Bool tabEnabled( GameWindow *tab )
{
	return tab && !tab->winIsHidden() && BitTest( tab->winGetStatus(), WIN_STATUS_ENABLED );
}

/// a tab's map count: the open one's from its list, the other's as the init counted it
static Int tabCount( Int which )
{
	GameWindow *tab = RandomMapTabButton( which );
	if( tabOpen( tab ) && thePickerGrids[ GRID_MAPS ].list && !RandomMapTabIsOpen() )
		return GadgetListBoxGetNumEntries( thePickerGrids[ GRID_MAPS ].list );
	return which >= 0 && which < 2 ? thePickerCounts[ which ] : 0;
}

/// the tabs the end of a map list leads to: the other list when it holds maps, Random when it is open
/// to this room; never a tab with nothing on it
static Int gatherTenants( const PickerGrid &g, Int tabs[ 2 ] )
{
	Int count = 0;
	if( kindOf( g ) != GRID_MAPS )
		return 0;
	const Int other = tabOpen( RandomMapTabButton( 1 ) ) ? 0 : 1;
	if( tabEnabled( RandomMapTabButton( other ) ) && tabCount( other ) > 0 )
		tabs[ count++ ] = other;
	if( tabEnabled( RandomMapTabButton( 2 ) ) )
		tabs[ count++ ] = 2;
	return count;
}

/// a new fill (a tab opened, the list built again): the rows are measured afresh and the list scrolls
/// to the pick
static void takeRows( PickerGrid &g, const MapPickerShape &shape )
{
	const Int entries = GadgetListBoxGetNumEntries( g.list );
	const void *head = entries > 0 ? GadgetListBoxGetItemData( g.list, 0, 0 ) : NULL;
	if( entries == g.entries && head == g.head && (Int)g.rows.size() == entries )
		return;
	g.entries = entries;
	g.head = head;
	g.rows.clear();
	PickerRow blank;
	blank.asked = FALSE;
	blank.picture = NULL;
	blank.width = 0;
	blank.size = UIStyle::MENU_TEXT_HINT + 1;
	blank.lines = 0;
	g.rows.resize( entries, blank );
	g.first = MapPickerFirstRow( selectedRow( g ), shape.columns, shape.rows, entries );
	g.pressRow = -1;
	// the list's own position follows the grid's (scrollTo says why)
	if( entries > 0 )
		GadgetListBoxSetTopVisibleEntry( g.list, min( g.first * shape.columns, entries - 1 ) );
}

/// the tiles of rows firstRow to lastRow, the screen's first tile at the area's top left
static void placeTiles( std::vector< PickerCell > &cells, const MapPickerShape &shape, Int top, Int firstRow, Int lastRow )
{
	const Int step = shape.tileWidth + MAP_PICKER_GAP;
	const Int pitch = shape.tileHeight + MAP_PICKER_GAP;
	const Int picture = shape.tileWidth - 2 * MAP_PICKER_INSET;
	for( Int row = firstRow; row < lastRow; row++ )
	{
		const Int at = row - firstRow;
		const Int left = PICK_LEFT + ( at % shape.columns ) * step;
		const Int y = top + ( at / shape.columns ) * pitch;
		PickerCell c = { left, y, left + shape.tileWidth, y + shape.tileHeight, CELL_TILE, row, picture };
		cells.push_back( c );
	}
}

void MapPickerPlaceTiles( Int total, Int first, Int top, Int startColumns, std::vector< MapPickerTile > &tiles )
{
	// through the grid's own cells, growing its vector as layoutCells does
	const MapPickerShape shape = MapPickerGridShape( PICK_LEFT, PICK_GRID_RIGHT, top, PICK_BOTTOM, startColumns );
	const Int allRows = ( total + shape.columns - 1 ) / shape.columns;
	first = max( 0, min( first, allRows - shape.rows ) );
	std::vector< PickerCell > cells;
	placeTiles( cells, shape, top, first * shape.columns, min( total, ( first + shape.rows ) * shape.columns ) );
	tiles.clear();
	for( size_t k = 0; k < cells.size(); k++ )
	{
		MapPickerTile tile = { cells[ k ].left, cells[ k ].top, cells[ k ].right, cells[ k ].bottom, cells[ k ].index };
		tiles.push_back( tile );
	}
}

/** The cells of the grid's area on the board.  Past a row's worth of maps the tiles keep their shape and
	* scroll, the last row's empty places going to the tenants (one place each, the last stretched over
	* what is left).  A map list shorter than a row shares that one row with its tenants and an ADD YOUR
	* OWN cell, all grown to the area's height; an empty one leaves the area to its words and the tenants
	* as two large keys. */
static void layoutCells( PickerGrid &g, std::vector< PickerCell > &cells, MapPickerShape &shape, Int &allRows )
{
	cells.clear();
	const Int top = gridTop( g );
	const Int startColumns = kindOf( g ) == GRID_TYPES ? 9 : 8;
	shape = MapPickerGridShape( PICK_LEFT, PICK_GRID_RIGHT, top, PICK_BOTTOM, startColumns );
	takeRows( g, shape );
	const Int total = (Int)g.rows.size();
	allRows = ( total + shape.columns - 1 ) / shape.columns;
	Int tenants[ 2 ];
	const Int tenantCount = gatherTenants( g, tenants );

	if( kindOf( g ) == GRID_MAPS && total == 0 )
	{
		// the head and the folder line over the top 100, the tenants as keys under them
		const Int keys = max( 1, tenantCount );
		const Int width = ( PICK_RIGHT - PICK_LEFT - ( keys - 1 ) * MAP_PICKER_GAP ) / keys;
		for( Int k = 0; k < tenantCount; k++ )
		{
			PickerCell c = { PICK_LEFT + k * ( width + MAP_PICKER_GAP ), top + 100, PICK_LEFT + k * ( width + MAP_PICKER_GAP ) + width,
				PICK_BOTTOM, CELL_TENANT, tenants[ k ], 0 };
			cells.push_back( c );
		}
		allRows = 0;
		return;
	}
	if( kindOf( g ) == GRID_MAPS && total < shape.columns )
	{
		// one row, no scroll bar: the maps, the tenants, ADD YOUR OWN
		const Int places = max( 3, total + tenantCount + 1 );
		const Int width = ( PICK_RIGHT - PICK_LEFT - ( places - 1 ) * MAP_PICKER_GAP ) / places;
		const Int picture = max( 16, min( width - 2 * MAP_PICKER_INSET, PICK_BOTTOM - top - MAP_PICKER_NAME_ROOM ) );
		for( Int k = 0; k < total + tenantCount + 1; k++ )
		{
			const Int left = PICK_LEFT + k * ( width + MAP_PICKER_GAP );
			const Int right = k == total + tenantCount ? PICK_RIGHT : left + width;
			PickerCell c = { left, top, right, PICK_BOTTOM, CELL_TILE, k, picture };
			if( k >= total )
			{
				c.kind = k < total + tenantCount ? CELL_TENANT : CELL_ADD;
				c.index = k < total + tenantCount ? tenants[ k - total ] : 0;
			}
			cells.push_back( c );
		}
		g.first = 0;
		allRows = 1;
		return;
	}

	g.first = max( 0, min( g.first, allRows - shape.rows ) );
	const Int firstRow = g.first * shape.columns;
	const Int lastRow = min( total, ( g.first + shape.rows ) * shape.columns );
	const Int step = shape.tileWidth + MAP_PICKER_GAP;
	const Int pitch = shape.tileHeight + MAP_PICKER_GAP;
	placeTiles( cells, shape, top, firstRow, lastRow );
	// the last row on screen and short: its empty places are the tenants'
	const Int spare = lastRow == total ? ( g.first + shape.rows ) * shape.columns - total : 0;
	const Int onLastRow = total % shape.columns == 0 ? 0 : shape.columns - total % shape.columns;
	const Int places = min( spare, onLastRow );
	if( places > 0 && tenantCount > 0 )
	{
		const Int used = min( places, tenantCount );
		Int at = total - firstRow;
		for( Int k = tenantCount - used; k < tenantCount; k++ )
		{
			const Int span = k == tenantCount - 1 ? places - ( used - 1 ) : 1;
			const Int left = PICK_LEFT + ( at % shape.columns ) * step;
			const Int y = top + ( at / shape.columns ) * pitch;
			PickerCell c = { left, y, left + span * step - MAP_PICKER_GAP, y + shape.tileHeight, CELL_TENANT, tenants[ k ], 0 };
			cells.push_back( c );
			at += span;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** A name on one line or two, broken at a space, in 16, then 15, then 14 px; a fuller name on two
	* lines beats a cut one.  A word too wide for the tile at 14 is cut with an ellipsis. */
static void breakName( const UnicodeString &name, Int widest, PickerRow &row )
{
	static const Int sizes[] = { UIStyle::MENU_TEXT_HINT + 1, UIStyle::MENU_TEXT_HINT, UIStyle::MENU_TEXT_LABEL };
	row.width = widest;
	for( Int s = 0; s < (Int)ARRAY_SIZE( sizes ); s++ )
	{
		GameFont *font = UIStyle::menuFont( sizes[ s ] );
		row.size = sizes[ s ];
		if( measureIn( name, font ) <= widest )
		{
			row.lines = 1;
			row.line[ 0 ] = name;
			return;
		}
		for( Int at = name.getLength() - 1; at > 0; at-- )
		{
			if( name.getCharAt( at ) != u' ' )
				continue;
			const UnicodeString a = partOf( name, 0, at );
			const UnicodeString b = partOf( name, at + 1, name.getLength() - at - 1 );
			if( measureIn( a, font ) <= widest && measureIn( b, font ) <= widest )
			{
				row.lines = 2;
				row.line[ 0 ] = a;
				row.line[ 1 ] = b;
				return;
			}
		}
	}
	row.lines = 1;
	row.line[ 0 ] = UIStyle::fitText( name, UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL ), widest );
}

/// a map's picture letterboxed at its own shape in a square, the shape its extent says
static void drawMapPicture( const Image *image, const MapMetaData *md, const IRegion2D &square )
{
	ICoord2D ul, lr;
	if( md )
		findDrawPositions( square.lo.x, square.lo.y, boxW( square ), boxH( square ), md->m_extent, &ul, &lr );
	else
	{
		ul = square.lo;
		lr = square.hi;
	}
	TheDisplay->drawImage( image, ul.x, ul.y, lr.x, lr.y );
}

/// a chip on a picture: words in 14 px bold capitals on black; a star before them when star
static void drawChip( Int key, const UnicodeString &words, Int x, Int y, Bool star )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	DisplayString *s = pickText( key, words, font );
	if( s == NULL )
		return;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	const Int lead = star ? pickPx( 22 ) : pickPx( 7 );
	const IRegion2D chip = { { x, y }, { x + lead + w + pickPx( 7 ), y + pickPx( 22 ) } };
	fillBox( chip, GameMakeColor( 0, 0, 0, 200 ) );
	if( star )
		drawStar( x + pickPx( 11 ), y + pickPx( 11 ), pickPx( 7 ), UIStyle::INK );
	s->draw( x + lead, y + ( pickPx( 22 ) - h ) / 2, UIStyle::INK, PICK_SHADOW );
}

static UnicodeString medalWord( Int medal, Int players )
{
	switch( medal )
	{
		case 1: return TheGameText->fetch( "GUI:MapMedalEasy" );
		case 2: return TheGameText->fetch( "GUI:MapMedalMedium" );
		case 3: return TheGameText->fetch( "GUI:MapMedalHard" );
		case 4: return formatted( "GUI:MapMedalHardMax", max( 1, players - 1 ) );
	}
	return TheGameText->fetch( "GUI:MapNotWon" );
}

static UnicodeString nextMedal( Int medal, Int players )
{
	switch( medal )
	{
		case 0: return TheGameText->fetch( "GUI:MapNextEasy" );
		case 1: return TheGameText->fetch( "GUI:MapNextMedium" );
		case 2: return TheGameText->fetch( "GUI:MapNextHard" );
		case 3: return formatted( "GUI:MapNextHardMax", max( 1, players - 1 ) );
	}
	return TheGameText->fetch( "GUI:MapNextAll" );
}

/// "8 players, 20 supplies, 12 tech", joined by UIStyle::HEADER_JOINER
static UnicodeString mapFacts( const MapMetaData *md )
{
	UnicodeString facts = formatted( "GUI:MapPlayersCount", md->m_numPlayers );
	facts.concat( UIStyle::HEADER_JOINER );
	facts.concat( formatted( "GUI:MapSupplies", (Int)md->m_supplyPositions.size() ) );
	facts.concat( UIStyle::HEADER_JOINER );
	facts.concat( formatted( "GUI:MapTech", (Int)md->m_techPositions.size() ) );
	return facts;
}

/// one tile: the picture square at its top (or a "?" well), its chips, its name under it, its state
static void drawTile( PickerGrid &g, const PickerCell &c, Int slot, Bool picked, Bool hovered )
{
	const IRegion2D r = board( c.left, c.top, c.right, c.bottom );
	fillBox( r, GameMakeColor( 255, 255, 255, 8 ) );
	if( picked )
		fillBox( r, UIStyle::withAlpha( UIStyle::EDGE, 100 ) );
	else if( hovered )
		fillBox( r, GameMakeColor( 255, 255, 255, 24 ) );

	PickerRow &row = g.rows[ c.index ];
	const AsciiString path = rowMap( g, c.index );
	const MapMetaData *md = path.isEmpty() ? NULL : TheMapCache->findMap( path );
	if( !row.asked )
	{
		row.asked = TRUE;
		row.picture = md ? getMapPreviewImage( path ) : NULL;
	}
	const Int pictureLeft = ( c.left + c.right - c.picture ) / 2;
	const IRegion2D pr = board( pictureLeft, c.top + MAP_PICKER_INSET, pictureLeft + c.picture, c.top + MAP_PICKER_INSET + c.picture );
	if( row.picture )
	{
		fillBox( pr, GameMakeColor( 6, 8, 12, 255 ) );
		drawMapPicture( row.picture, md, pr );
	}
	else
	{
		fillBox( pr, GameMakeColor( 10, 12, 20, 230 ) );
		drawIn( TEXT_TILES + slot * TILE_TEXTS + 4, UnicodeString( u"?" ), UIStyle::menuFont( UIStyle::MENU_TEXT_TITLE + 8 ), pr, UIStyle::MUTED, TRUE );
	}
	frameBox( pr, UIStyle::withAlpha( UIStyle::FRAME, 200 ) );

	// the player count on the picture's top left (a map type's count is not the new map's), the best win
	// on its foot
	if( kindOf( g ) == GRID_MAPS && md )
	{
		drawChip( TEXT_TILES + slot * TILE_TEXTS + 2, formatted( "GUI:MapPlayersChip", md->m_numPlayers ), pr.lo.x, pr.lo.y, FALSE );
		const Int medal = rowMedal( g, c.index );
		if( medal > 0 )
			drawChip( TEXT_TILES + slot * TILE_TEXTS + 3, medalWord( medal, md->m_numPlayers ), pr.lo.x, pr.hi.y - pickPx( 22 ), TRUE );
	}
	if( picked )
		drawTickSquare( board( pictureLeft + c.picture - 22, c.top + MAP_PICKER_INSET, pictureLeft + c.picture, c.top + MAP_PICKER_INSET + 22 ) );

	// the name, one line or two, centred in the room under the picture
	const Int widest = pickPx( c.right - c.left - 14 );
	if( row.width != widest )
		breakName( GadgetListBoxGetText( g.list, c.index, textColumn( g.list ) ), widest, row );
	GameFont *font = UIStyle::menuFont( row.size );
	const Int nameTop = c.top + MAP_PICKER_INSET + c.picture + 4;
	const Int pitch = row.size + 3;
	const Int linesTop = nameTop + ( c.bottom - 3 - nameTop - pitch * row.lines ) / 2;
	for( Int k = 0; k < row.lines; k++ )
	{
		const IRegion2D line = board( c.left + 7, linesTop + k * pitch, c.right - 7, linesTop + ( k + 1 ) * pitch );
		drawIn( TEXT_TILES + slot * TILE_TEXTS + k, row.line[ k ], font, line, UIStyle::INK, TRUE );
	}

	if( picked )
	{
		const Int line = max( 1, pickPx( 2 ) );
		UIStyle::litLine( r.lo.x, r.lo.y, boxW( r ), line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.hi.y - line, boxW( r ), line, UIStyle::EDGE_LIT );
		UIStyle::litLine( r.lo.x, r.lo.y, line, boxH( r ), UIStyle::EDGE_LIT );
		UIStyle::litLine( r.hi.x - line, r.lo.y, line, boxH( r ), UIStyle::EDGE_LIT );
	}
	else if( hovered )
		UIStyle::litEdge( r, UIStyle::EDGE_LIT );
	else
		frameBox( r, UIStyle::withAlpha( UIStyle::FRAME, 150 ) );
}

/// the words a tenant shows: its tab's name in capitals and what is behind it
static void tenantWords( Int which, UnicodeString &label, UnicodeString &line )
{
	if( which == 2 )
	{
		label = TheGameText->fetch( "GUI:MapTenantRandom" );
		line = TheGameText->fetch( "GUI:MapTenantRandomLine" );
		return;
	}
	GameWindow *tab = RandomMapTabButton( which );
	label = UIStyle::capitals( tab ? tab->winGetText() : UnicodeString::TheEmptyString );
	const Int count = tabCount( which );
	line = formatted( count == 1 ? "GUI:MapTenantCountOne" : "GUI:MapTenantCount", count );
}

/// a cell that leads to another tab: a dark well with an arrow, the tab's name and a line
static void drawTenant( const PickerCell &c, Int slot, Bool hovered )
{
	const IRegion2D r = board( c.left, c.top, c.right, c.bottom );
	fillBox( r, GameMakeColor( 0, 0, 0, 120 ) );
	if( hovered )
		fillBox( r, GameMakeColor( 255, 255, 255, 14 ) );
	if( hovered )
		UIStyle::litEdge( r, UIStyle::EDGE_LIT );
	else
		frameBox( r, UIStyle::withAlpha( UIStyle::EDGE, 170 ) );
	const Int cx = ( c.left + c.right ) / 2;
	const Int cy = c.top + ( c.bottom - c.top ) * 36 / 100;
	const Real line = (Real)max( 2, pickPx( 3 ) );
	const IRegion2D shaft = board( cx - 22, cy, cx + 18, cy );
	const IRegion2D head = board( cx + 6, cy - 12, cx + 20, cy + 12 );
	TheDisplay->drawLine( shaft.lo.x, shaft.lo.y, shaft.hi.x, shaft.lo.y, line, UIStyle::EDGE_LIT );
	TheDisplay->drawLine( head.lo.x, head.lo.y, head.hi.x, shaft.lo.y, line, UIStyle::EDGE_LIT );
	TheDisplay->drawLine( head.hi.x, shaft.lo.y, head.lo.x, head.hi.y, line, UIStyle::EDGE_LIT );

	UnicodeString label, words;
	tenantWords( c.index, label, words );
	const Int key = TEXT_TENANTS + 4 * ( slot % 2 );
	const Int labelTop = c.top + ( c.bottom - c.top ) * 58 / 100;
	std::vector< UnicodeString > lines;
	GameFont *labelFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 1, TRUE );
	UIStyle::wrapLines( label, pickPx( c.right - c.left - 16 ), measureIn, labelFont, lines );
	Int y = labelTop;
	for( Int k = 0; k < (Int)lines.size() && k < 2; k++ )
	{
		drawIn( key + k, lines[ k ], labelFont, board( c.left + 8, y, c.right - 8, y + 18 ), UIStyle::INK, TRUE );
		y += 18;
	}
	GameFont *lineFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL );
	lines.clear();	// wrapLines adds to what the vector holds
	UIStyle::wrapLines( words, pickPx( c.right - c.left - 16 ), measureIn, lineFont, lines );
	y += 4;
	for( Int k = 0; k < (Int)lines.size() && k < 2; k++ )
	{
		drawIn( key + 2 + k, lines[ k ], lineFont, board( c.left + 8, y, c.right - 8, y + 17 ), UIStyle::MUTED, TRUE );
		y += 17;
	}
}

/// where the maps come from, in body words wrapped from x; returns the y under them
static Int drawFolderLine( Int key, Int lines, Int left, Int top, Int right, Int size, Bool centred )
{
	AsciiString folder = TheGlobalData->getPath_UserData();
	folder.concat( TheMapCache->getMapDir() );
	UnicodeString where;
	where.translate( folder );
	UnicodeString words;
	words.format( TheGameText->fetch( "GUI:MapFolderLine" ).str(), where.str() );
	GameFont *font = UIStyle::menuFont( size );
	std::vector< UnicodeString > wrapped;
	UIStyle::wrapLines( words, pickPx( right - left ), measureIn, font, wrapped );
	const Int pitch = size + 6;
	for( Int k = 0; k < (Int)wrapped.size() && k < lines; k++ )
	{
		drawIn( key + k, wrapped[ k ], font, board( left, top, right, top + pitch ), UIStyle::MUTED, centred );
		top += pitch;
	}
	return top;
}

static void drawAddOwn( const PickerCell &c )
{
	const IRegion2D r = board( c.left, c.top, c.right, c.bottom );
	fillBox( r, GameMakeColor( 0, 0, 0, 90 ) );
	frameBox( r, UIStyle::withAlpha( UIStyle::FRAME, 140 ) );
	const Int y = ( c.top + c.bottom ) / 2 - 50;
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
	const IRegion2D at = board( c.left + 20, y, c.right - 20, y + 26 );
	UIStyle::drawTracked( TheGameText->fetch( "GUI:MapAddOwn" ), font, at.lo.x, at.lo.y, UIStyle::INK, UIStyle::MENU_LABEL_TRACKING );
	drawFolderLine( TEXT_EMPTY + 1, 3, c.left + 20, y + 36, c.right - 20, UIStyle::MENU_TEXT_HINT, FALSE );
}

/// the list's track at the grid's right: the window's share of the rows lit
static void drawGroove( const PickerGrid &g, Int top, Int rows, Int allRows )
{
	const IRegion2D groove = board( PICK_GROOVE_LEFT, top, PICK_RIGHT, PICK_BOTTOM );
	fillBox( groove, GameMakeColor( 0, 0, 0, 140 ) );
	frameBox( groove, UIStyle::withAlpha( UIStyle::FRAME, 180 ) );
	if( allRows <= rows )
		return;
	const Int span = PICK_BOTTOM - top;
	const Int t0 = top + span * g.first / allRows;
	const Int t1 = top + span * min( allRows, g.first + rows ) / allRows;
	const IRegion2D thumb = board( PICK_GROOVE_LEFT + 1, t0, PICK_RIGHT - 1, t1 );
	UIStyle::litLine( thumb.lo.x, thumb.lo.y, boxW( thumb ), boxH( thumb ), UIStyle::EDGE );
}

/// the hovered map's tooltip over its tile, its left edge on the tile's: the name, the facts and, in
/// skirmish, what to beat next in EA's words
static void drawTooltip( const PickerGrid &g, const PickerCell &c )
{
	const AsciiString path = rowMap( g, c.index );
	const MapMetaData *md = path.isEmpty() ? NULL : TheMapCache->findMap( path );
	if( md == NULL )
		return;
	std::vector< UnicodeString > lines;
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	const Int widest = pickPx( PICK_TIP_WIDTH - 28 );
	UIStyle::wrapLines( mapFacts( md ), widest, measureIn, font, lines );
	const Int medal = rowMedal( g, c.index );
	if( medal >= 0 )
	{
		static const char *const tips[] = { "TOOLTIP:MapNoSuccess", "TOOLTIP:MapEasySuccess", "TOOLTIP:MapMediumSuccess",
			"TOOLTIP:MapHardSuccess", "TOOLTIP:MapMaxBrutalSuccess" };
		std::vector< UnicodeString > more;
		UIStyle::wrapLines( TheGameText->fetch( tips[ max( 0, min( 4, medal ) ) ] ), widest, measureIn, font, more );
		lines.insert( lines.end(), more.begin(), more.end() );
	}
	const Int count = min( 6, (Int)lines.size() );
	const Int height = 14 + 26 + 21 * count + 10;
	const Int x = min( c.left, UIStyle::MENU_PANEL_RIGHT - 20 - PICK_TIP_WIDTH );
	const Int y = c.top - height - 8 > PICK_TABS_BOTTOM ? c.top - height - 8 : c.bottom + 8;
	const IRegion2D shadow = board( x + 6, y + 6, x + PICK_TIP_WIDTH + 6, y + height + 6 );
	fillBox( shadow, GameMakeColor( 0, 0, 0, 110 ) );
	const IRegion2D r = board( x, y, x + PICK_TIP_WIDTH, y + height );
	fillBox( r, GameMakeColor( 4, 6, 12, 240 ) );
	frameBox( r, UIStyle::EDGE );
	drawIn( TEXT_TIP, GadgetListBoxGetText( g.list, c.index, textColumn( g.list ) ), UIStyle::menuFont( UIStyle::MENU_TEXT_BODY ),
		board( x + 14, y + 10, x + PICK_TIP_WIDTH - 14, y + 36 ), UIStyle::INK, FALSE );
	for( Int k = 0; k < count; k++ )
		drawIn( TEXT_TIP + 1 + k, lines[ k ], font, board( x + 14, y + 40 + k * 21, x + PICK_TIP_WIDTH - 14, y + 40 + ( k + 1 ) * 21 ),
			UIStyle::MUTED, FALSE );
}

/// the preview window on the board, from where FitLobbyRoomMap put it
static void previewOnBoard( Int &left, Int &top, Int &right, Int &bottom )
{
	Int x = 0, y = 0, w = 0, h = 0;
	thePickerPreview->winGetScreenPosition( &x, &y );
	thePickerPreview->winGetSize( &w, &h );
	const UIStyle::BoardFit fit = UIStyle::boardFit( TheDisplay->getWidth(), TheDisplay->getHeight() );
	const Real scale = fit.scale > 0.0f ? fit.scale : 1.0f;
	left = REAL_TO_INT( ( x - fit.x ) / scale + 0.5f );
	top = REAL_TO_INT( ( y - fit.y ) / scale + 0.5f );
	right = REAL_TO_INT( ( x + w - fit.x ) / scale + 0.5f );
	bottom = REAL_TO_INT( ( y + h - fit.y ) / scale + 0.5f );
}

/** The picked map's words, as the room shows its own map's: under a wide preview in the band the
	* room's name and SELECT MAP stand on (the name and facts at the left, the best win and the next one
	* at the right); beside a square one stacked from x 1666.  facts are one line each; badge and note
	* are the right hand pair (the medal word with its star, and the next medal; or the Random tab's line). */
static void drawPickedWords( const UnicodeString &name, const std::vector< UnicodeString > &facts, const UnicodeString &badge,
	const UnicodeString &note, Bool star )
{
	if( thePickerPreview == NULL )
		return;
	Int left, top, right, bottom;
	previewOnBoard( left, top, right, bottom );
	GameFont *nameFont = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE );
	GameFont *factFont = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	GameFont *badgeFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	if( right < PICK_COLUMN_RIGHT - 160 )
	{
		// beside a square preview: everything stacked down to the rules
		const Int x = PICK_SQUARE_WORDS;
		Int y = top - 2;
		std::vector< UnicodeString > lines;
		UIStyle::wrapLines( name, pickPx( PICK_COLUMN_RIGHT - x ), measureIn, nameFont, lines );
		for( Int k = 0; k < (Int)lines.size() && k < 2; k++ )
		{
			drawIn( TEXT_WORDS + k, lines[ k ], nameFont, board( x, y, PICK_COLUMN_RIGHT, y + 30 ), UIStyle::INK, FALSE );
			y += 30;
		}
		for( Int k = 0; k < (Int)facts.size() && k < 3; k++ )
		{
			drawIn( TEXT_WORDS + 2 + k, UIStyle::fitText( facts[ k ], factFont, pickPx( PICK_COLUMN_RIGHT - x ) ), factFont,
				board( x, y + 2, PICK_COLUMN_RIGHT, y + 24 ), UIStyle::MUTED, FALSE );
			y += 22;
		}
		y += 12;
		if( !badge.isEmpty() )
		{
			const IRegion2D at = board( x + 24, y, PICK_COLUMN_RIGHT, y + 22 );
			if( star )
			{
				const IRegion2D s = board( x + 8, y + 11, x + 8, y + 11 );
				drawStar( s.lo.x, s.lo.y, pickPx( 8 ), UIStyle::INK );
			}
			drawIn( TEXT_WORDS + 5, badge, badgeFont, star ? at : board( x, y, PICK_COLUMN_RIGHT, y + 22 ), UIStyle::INK, FALSE );
			y += 24;
		}
		lines.clear();	// wrapLines adds to what the vector holds
		UIStyle::wrapLines( note, pickPx( PICK_COLUMN_RIGHT - x ), measureIn, factFont, lines );
		for( Int k = 0; k < (Int)lines.size() && k < 4; k++ )
		{
			drawIn( TEXT_WORDS + 6 + k, lines[ k ], factFont, board( x, y, PICK_COLUMN_RIGHT, y + 22 ), UIStyle::MUTED, FALSE );
			y += 22;
		}
		return;
	}
	// under a wide one: the name and the facts left of x 1644, the pair from x 1660
	const Int y = bottom + 10;
	const Int wordsRight = PICK_COLUMN_RIGHT - 216;
	drawIn( TEXT_WORDS, UIStyle::fitName( name, nameFont, pickPx( wordsRight - PICK_COLUMN_LEFT ) ), nameFont,
		board( PICK_COLUMN_LEFT, y, wordsRight, y + 30 ), UIStyle::INK, FALSE );
	UnicodeString joined;
	for( Int k = 0; k < (Int)facts.size(); k++ )
	{
		if( k > 0 )
			joined.concat( UIStyle::HEADER_JOINER );
		joined.concat( facts[ k ] );
	}
	drawIn( TEXT_WORDS + 2, UIStyle::fitText( joined, factFont, pickPx( wordsRight - PICK_COLUMN_LEFT ) ), factFont,
		board( PICK_COLUMN_LEFT, y + 30, wordsRight, y + 50 ), UIStyle::MUTED, FALSE );
	const Int mx = PICK_COLUMN_RIGHT - 200;
	if( !badge.isEmpty() )
	{
		if( star )
		{
			const IRegion2D s = board( mx + 8, y + 13, mx + 8, y + 13 );
			drawStar( s.lo.x, s.lo.y, pickPx( 8 ), UIStyle::INK );
		}
		drawIn( TEXT_WORDS + 5, badge, badgeFont, board( star ? mx + 24 : mx, y + 2, PICK_COLUMN_RIGHT, y + 24 ), UIStyle::INK, FALSE );
	}
	// the note in what is left of the band, two lines at most; a longer one is left out
	std::vector< UnicodeString > lines;
	UIStyle::wrapLines( note, pickPx( PICK_COLUMN_RIGHT - mx ), measureIn, factFont, lines );
	const Int noteTop = badge.isEmpty() ? y + 2 : y + 26;
	const Int room = badge.isEmpty() ? 2 : 1;
	if( (Int)lines.size() <= room )
		for( Int k = 0; k < (Int)lines.size(); k++ )
			drawIn( TEXT_WORDS + 6 + k, lines[ k ], factFont, board( mx, noteTop + k * 22, PICK_COLUMN_RIGHT, noteTop + ( k + 1 ) * 22 ),
				UIStyle::MUTED, FALSE );
}

/// the map list's picked map: its name, its facts, the best win and the next one in skirmish
static void drawPickedMap( const PickerGrid &g )
{
	const Int row = selectedRow( g );
	if( row < 0 || row >= (Int)g.rows.size() )
		return;
	const AsciiString path = rowMap( g, row );
	const MapMetaData *md = path.isEmpty() ? NULL : TheMapCache->findMap( path );
	if( md == NULL )
		return;
	std::vector< UnicodeString > facts;
	facts.push_back( formatted( "GUI:MapPlayersCount", md->m_numPlayers ) );
	facts.push_back( formatted( "GUI:MapSupplies", (Int)md->m_supplyPositions.size() ) );
	facts.push_back( formatted( "GUI:MapTech", (Int)md->m_techPositions.size() ) );
	const Int medal = rowMedal( g, row );
	if( medal >= 0 )
		drawPickedWords( md->m_displayName, facts, medalWord( medal, md->m_numPlayers ), nextMedal( medal, md->m_numPlayers ), TRUE );
	else
		drawPickedWords( md->m_displayName, facts, UnicodeString::TheEmptyString, UnicodeString::TheEmptyString, FALSE );
}

/// the Random tab's words: the new map is the type's style, its spots placed when it is built
static void drawPickedType( void )
{
	static const char *const sizes[] = { "GUI:RandomMapSizeTiny", "GUI:RandomMapSizeSmall", "GUI:RandomMapSizeMedium",
		"GUI:RandomMapSizeLarge", "GUI:RandomMapSizeHuge" };
	const AsciiString path = RandomMapTabTemplate();
	const MapMetaData *md = path.isEmpty() ? NULL : TheMapCache->findMap( path );
	std::vector< UnicodeString > facts;
	UnicodeString style;
	style.format( TheGameText->fetch( "GUI:RandomMapStyleOf" ).str(), md ? md->m_displayName.str()
		: TheGameText->fetch( path == "style:aod" ? "GUI:RandomMapAodShort" : "GUI:RandomMapAnyTemplate" ).str() );
	facts.push_back( style );
	UnicodeString size = TheGameText->fetch( sizes[ max( 0, min( (Int)ARRAY_SIZE( sizes ) - 1, RandomMapTabSize() ) ) ] );
	size.concat( UIStyle::HEADER_JOINER );
	size.concat( formatted( "GUI:RandomMapSpots", RandomMapTabPlayers() ) );
	facts.push_back( size );
	drawPickedWords( TheGameText->fetch( "GUI:RandomMapName" ), facts, UnicodeString::TheEmptyString,
		TheGameText->fetch( "GUI:RandomMapSpotsLine" ), FALSE );

	// the preview is the type's own map, so it says STYLE: its spots are not the new map's
	if( thePickerPreview && md )
	{
		Int left, top, right, bottom;
		previewOnBoard( left, top, right, bottom );
		GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
		const UnicodeString tag = TheGameText->fetch( "GUI:MapStyleTag" );
		const Int width = UIStyle::trackedWidth( tag, font, UIStyle::MENU_LABEL_TRACKING );
		const IRegion2D chip = board( left, top, left, top + 24 );
		const IRegion2D wash = { chip.lo, { chip.lo.x + width + pickPx( 16 ), chip.hi.y } };
		fillBox( wash, GameMakeColor( 0, 0, 0, 200 ) );
		UIStyle::drawTracked( tag, font, chip.lo.x + pickPx( 8 ), chip.lo.y + ( boxH( chip ) - lineHeight( font ) ) / 2, UIStyle::INK,
			UIStyle::MENU_LABEL_TRACKING );
	}
}

/// the foot's left: in skirmish's map lists the medal's legend with its star, then how to accept
static void drawFoot( const PickerGrid &g )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	const char *hintKey = kindOf( g ) == GRID_TYPES ? "GUI:MapHintAcceptType" : "GUI:MapHintAccept";
	const UnicodeString hint = UIStyle::fitKey( hintKey, font, pickPx( 420 ) );
	const IRegion2D foot = board( PICK_LEFT, PICK_FOOT_MIDDLE - 12, PICK_FOOT_RIGHT, PICK_FOOT_MIDDLE + 12 );
	Int x = foot.lo.x;
	if( kindOf( g ) == GRID_MAPS && thePickerSkirmish )
	{
		const IRegion2D s = board( PICK_LEFT + 8, PICK_FOOT_MIDDLE, PICK_LEFT + 8, PICK_FOOT_MIDDLE );
		drawStar( s.lo.x, s.lo.y, pickPx( 8 ), UIStyle::INK );
		x = board( PICK_LEFT + 24, 0, PICK_LEFT + 24, 0 ).lo.x;
		const Int room = foot.hi.x - x - measureIn( hint, font ) - pickPx( 32 );
		const UnicodeString legend = UIStyle::fitKey( "GUI:MapMedalLegend", font, room );
		const IRegion2D at = { { x, foot.lo.y }, { foot.hi.x, foot.hi.y } };
		drawIn( TEXT_FOOT, legend, font, at, UIStyle::MUTED, FALSE );
		x += measureIn( legend, font ) + pickPx( 32 );
	}
	const IRegion2D at = { { x, foot.lo.y }, { foot.hi.x, foot.hi.y } };
	drawIn( TEXT_FOOT + 1, hint, font, at, UIStyle::MUTED, FALSE );
}

/// TOOLTIP:MapListCrowded, on the screen: the line under the tabs past eight seats
static void drawCrowdLine( void )
{
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	UnicodeString words = TheGameText->fetch( "TOOLTIP:MapListCrowded" );
	words.concat( u'.' );
	std::vector< UnicodeString > lines;
	UIStyle::wrapLines( words, pickPx( PICK_RIGHT - PICK_LEFT ), measureIn, font, lines );
	for( Int k = 0; k < (Int)lines.size() && k < 2; k++ )
		drawIn( TEXT_CROWD + k, lines[ k ], font, board( PICK_LEFT, PICK_MAPS_TOP - 4 + k * 20, PICK_RIGHT, PICK_MAPS_TOP + 16 + k * 20 ),
			UIStyle::MUTED, FALSE );
}

//-------------------------------------------------------------------------------------------------
static void drawGrid( GameWindow *window, WinInstanceData *instData )
{
	PickerGrid *g = gridOf( window );
	if( g == NULL || TheMapCache == NULL )
		return;
	std::vector< PickerCell > cells;
	MapPickerShape shape;
	Int allRows = 0;
	layoutCells( *g, cells, shape, allRows );
	const Int top = gridTop( *g );

	if( crowded( *g ) )
		drawCrowdLine();
	if( kindOf( *g ) == GRID_MAPS && g->rows.empty() )
	{
		const IRegion2D head = board( PICK_LEFT, top + 10, PICK_RIGHT, top + 44 );
		GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_ROW_FIGURE, TRUE );
		const UnicodeString words = TheGameText->fetch( "GUI:MapNoneOwn" );
		const Int width = UIStyle::trackedWidth( words, font, 2.0f );
		UIStyle::drawTracked( words, font, ( head.lo.x + head.hi.x - width ) / 2, head.lo.y + ( boxH( head ) - lineHeight( font ) ) / 2,
			UIStyle::INK, 2.0f );
		drawFolderLine( TEXT_EMPTY + 1, 2, PICK_LEFT, top + 54, PICK_RIGHT, UIStyle::MENU_TEXT_BODY, TRUE );
	}

	// under the pointer, unless something stands over the grid there (an open list, a box)
	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	const Bool pointerHere = TheWindowManager->getWindowUnderCursor( mouse.x, mouse.y ) == window;
	const Int picked = selectedRow( *g );
	const PickerCell *tip = NULL;
	for( Int k = 0; k < (Int)cells.size(); k++ )
	{
		const PickerCell &c = cells[ k ];
		const Bool hovered = pointerHere && inside( board( c.left, c.top, c.right, c.bottom ), mouse.x, mouse.y );
		if( c.kind == CELL_TILE )
		{
			drawTile( *g, c, min( k, (Int)TILE_SLOTS - 1 ), c.index == picked, hovered );
			if( hovered && kindOf( *g ) == GRID_MAPS )
				tip = &c;
		}
		else if( c.kind == CELL_TENANT )
			drawTenant( c, k, hovered );
		else
			drawAddOwn( c );
	}
	// a list that can scroll has its track; one row or none has nothing to scroll
	if( allRows > 1 || ( allRows == 1 && (Int)g->rows.size() >= shape.columns ) )
		drawGroove( *g, top, shape.rows, allRows );

	if( kindOf( *g ) == GRID_MAPS )
		drawPickedMap( *g );
	else
		drawPickedType();
	drawFoot( *g );
	if( tip )
		drawTooltip( *g, *tip );
}

/// the first row moved to row, kept in the list
static void scrollTo( PickerGrid &g, Int first )
{
	std::vector< PickerCell > cells;
	MapPickerShape shape;
	Int allRows = 0;
	layoutCells( g, cells, shape, allRows );
	g.first = max( 0, min( first, allRows - shape.rows ) );
	/* The list behind the grid follows to the grid's first row on screen, so the list's own position
		 (what Tools/ui_walk.py reads as a list's scroll, through ControlServer's uitree) says the grid
		 moved: the wheel was turning the tiles while the walker saw a list that never scrolled. */
	if( g.list && (Int)g.rows.size() > 0 )
		GadgetListBoxSetTopVisibleEntry( g.list, min( g.first * shape.columns, (Int)g.rows.size() - 1 ) );
}

static WindowMsgHandledType gridInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	PickerGrid *g = gridOf( window );
	if( g == NULL )
		return MSG_IGNORED;
	const Int x = (Short)LOLONGTOSHORT( mData1 );
	const Int y = (Short)HILONGTOSHORT( mData1 );
	std::vector< PickerCell > cells;
	MapPickerShape shape;
	Int allRows = 0;
	layoutCells( *g, cells, shape, allRows );
	const Int top = gridTop( *g );
	switch( msg )
	{
		case GWM_WHEEL_UP:
		case GWM_WHEEL_DOWN:
			scrollTo( *g, g->first + ( msg == GWM_WHEEL_UP ? -1 : 1 ) );
			return MSG_HANDLED;

		case GWM_LEFT_DOWN:
		{
			g->pressed = TRUE;
			// the groove: a press on the thumb takes it, one beside it moves a window's worth
			const IRegion2D groove = board( PICK_GROOVE_LEFT - 6, top, PICK_RIGHT, PICK_BOTTOM );
			if( allRows > shape.rows && inside( groove, x, y ) )
			{
				const Int span = PICK_BOTTOM - top;
				const Int t0 = top + span * g->first / allRows;
				const Int t1 = top + span * min( allRows, g->first + shape.rows ) / allRows;
				const IRegion2D thumb = board( PICK_GROOVE_LEFT - 6, t0, PICK_RIGHT, t1 );
				if( inside( thumb, x, y ) )
					g->grab = y - thumb.lo.y;
				else
					scrollTo( *g, g->first + ( y < thumb.lo.y ? -shape.rows : shape.rows ) );
			}
			return MSG_HANDLED;
		}

		case GWM_LEFT_DRAG:
			if( g->grab >= 0 && allRows > shape.rows )
			{
				const IRegion2D track = board( PICK_GROOVE_LEFT, top, PICK_RIGHT, PICK_BOTTOM );
				const Int span = max( 1, boxH( track ) );
				scrollTo( *g, ( ( y - g->grab - track.lo.y ) * allRows + span / 2 ) / span );
			}
			return MSG_HANDLED;

		case GWM_LEFT_UP:
		{
			// only the release of a press the grid took picks: a click that began on another window
			// (the room's Select Map, a key) ends here without picking or accepting
			const Bool pressed = g->pressed;
			g->pressed = FALSE;
			if( g->grab >= 0 )
			{
				g->grab = -1;
				return MSG_HANDLED;
			}
			if( !pressed )
				return MSG_HANDLED;
			for( Int k = 0; k < (Int)cells.size(); k++ )
			{
				const PickerCell &c = cells[ k ];
				if( !inside( board( c.left, c.top, c.right, c.bottom ), x, y ) )
					continue;
				if( c.kind == CELL_TENANT )
				{
					// the tab itself is clicked: the picker fills its list or shows the Random panel
					click();
					GadgetRadioSetSelection( RandomMapTabButton( c.index ), TRUE );
					return MSG_HANDLED;
				}
				if( c.kind != CELL_TILE )
					return MSG_HANDLED;
				click();
				const UnsignedInt now = Clock_Milliseconds();
				const Bool second = c.index == g->pressRow && now - g->pressTime <= TheGlobalData->m_doubleClickTimeMS;
				g->pressRow = second ? -1 : c.index;
				g->pressTime = now;
				// the row is selected again even when it is the pick already, so the picker always answers
				GadgetListBoxSetSelected( g->list, c.index );
				if( second )
					TheWindowManager->winSendSystemMsg( g->list->winGetOwner(), GLM_DOUBLE_CLICKED, (WindowMsgData)g->list, c.index );
				return MSG_HANDLED;
			}
			return MSG_HANDLED;
		}

		case GWM_RIGHT_DOWN:
		case GWM_RIGHT_UP:
			return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

static void attachGrid( Int which, GameWindow *listbox )
{
	PickerGrid &g = thePickerGrids[ which ];
	g.list = listbox;
	g.first = 0;
	g.entries = -1;
	g.head = NULL;
	g.pressRow = -1;
	g.pressTime = 0;
	g.grab = -1;
	g.pressed = FALSE;
	g.rows.clear();
	if( listbox == NULL )
		return;
	listbox->winSetDrawFunc( drawGrid );
	listbox->winSetInputFunc( gridInput );
	listbox->winSetTooltipFunc( NULL );
	// the list's own slider and arrows: the grid has its own track
	for( GameWindow *child = listbox->winGetChild(); child; child = child->winGetNext() )
		child->winHide( TRUE );
}

/// MAP SIZE and MAP TYPE: the window's words in the menu's 14 px green capitals
static void drawHead( GameWindow *window, WinInstanceData *instData )
{
	Int x = 0, y = 0;
	window->winGetScreenPosition( &x, &y );
	GameFont *font = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL, TRUE );
	UIStyle::drawTracked( UIStyle::capitals( GadgetStaticTextGetText( window ) ), font, x, y, UIStyle::ACTIVE, UIStyle::MENU_LABEL_TRACKING );
}

static GameWindow *pickerWindow( const char *layoutFilename, GameWindow *parent, const char *name )
{
	AsciiString full;
	full.format( "%s:%s", layoutFilename, name );
	return TheWindowManager->winGetWindowFromId( parent, NAMEKEY( full ) );
}

/// a key in the menu language, its word in capitals
static void styleKey( GameWindow *key, Bool primary )
{
	if( key == NULL )
		return;
	UIStyle::setMenuKey( key, primary );
	GadgetButtonSetText( key, UIStyle::capitals( key->winGetText() ) );
}

//-------------------------------------------------------------------------------------------------
void MapPickerInit( const char *layoutFilename, GameWindow *parent, GameWindow *listbox, GameWindow *preview,
	const GameInfo *room, Int minPlayers, Bool skirmish )
{
	MapPickerShutdown();
	thePickerPreview = preview;
	thePickerRoom = room;
	thePickerMinPlayers = minPlayers;
	thePickerSkirmish = skirmish;
	if( TheMapCache )
		TheMapCache->updateCache();
	thePickerCounts[ 0 ] = countMapListMaps( TRUE, TRUE, minPlayers );
	thePickerCounts[ 1 ] = countMapListMaps( FALSE, TRUE, minPlayers ) + ( skirmish ? countMapListMaps( FALSE, FALSE, minPlayers ) : 0 );
	attachGrid( GRID_MAPS, listbox );
	styleKey( pickerWindow( layoutFilename, parent, "ButtonBack" ), FALSE );
	styleKey( pickerWindow( layoutFilename, parent, "ButtonOK" ), TRUE );
}

void MapPickerAttachTypes( GameWindow *listbox, GameWindow *sizeHead, GameWindow *typeHead )
{
	attachGrid( GRID_TYPES, listbox );
	thePickerSizeHead = sizeHead;
	thePickerTypeHead = typeHead;
	if( sizeHead )
		sizeHead->winSetDrawFunc( drawHead );
	if( typeHead )
		typeHead->winSetDrawFunc( drawHead );
}

void MapPickerShutdown( void )
{
	for( Int g = 0; g < GRID_COUNT; g++ )
	{
		thePickerGrids[ g ].list = NULL;
		thePickerGrids[ g ].rows.clear();
	}
	thePickerPreview = thePickerSizeHead = thePickerTypeHead = NULL;
	thePickerRoom = NULL;
	for( Int k = 0; k < TEXT_COUNT; k++ )
	{
		if( thePickerTexts[ k ] )
			TheDisplayStringManager->freeDisplayString( thePickerTexts[ k ] );
		thePickerTexts[ k ] = NULL;
	}
	if( thePickerMeasure )
		TheDisplayStringManager->freeDisplayString( thePickerMeasure );
	thePickerMeasure = NULL;
}

Bool MapPickerFitPreview( GameWindow *window, const MapMetaData *map )
{
	if( window == NULL || window != thePickerPreview )
		return FALSE;
	// the room's rule (FitLobbyRoomMap): a wide map at the column's width, at most 278 tall; a squarer
	// one at most 324 square at the left, its words beside it
	Real ratio = 542.0f / 278.0f;
	if( map )
	{
		const Real w = map->m_extent.hi.x - map->m_extent.lo.x;
		const Real h = map->m_extent.hi.y - map->m_extent.lo.y;
		if( w > 0.0f && h > 0.0f )
			ratio = w / h;
	}
	Int right = PICK_COLUMN_RIGHT, bottom = 0;
	if( ratio >= 1.3f )
		bottom = 160 + min( 278, REAL_TO_INT( 542.0f / ratio + 0.5f ) );
	else
	{
		right = PICK_COLUMN_LEFT + min( 324, REAL_TO_INT( 324.0f * ratio + 0.5f ) );
		bottom = 160 + 324;
	}
	const IRegion2D box = board( PICK_COLUMN_LEFT, 160, right, bottom );
	Int parentX = 0, parentY = 0;
	if( GameWindow *parent = window->winGetParent() )
		parent->winGetScreenPosition( &parentX, &parentY );
	window->winSetPosition( box.lo.x - parentX, box.lo.y - parentY );
	window->winSetSize( boxW( box ), boxH( box ) );
	return TRUE;
}

void MapPickerDrawTab( GameWindow *window, WinInstanceData *instData )
{
	Int x = 0, y = 0, w = 0, h = 0;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );
	const IRegion2D box = { { x, y }, { x + w, y + h } };
	const Bool enabled = BitTest( window->winGetStatus(), WIN_STATUS_ENABLED );
	const Bool isOpen = BitTest( instData->getState(), WIN_STATE_SELECTED );
	const Bool hovered = enabled && !isOpen && BitTest( instData->getState(), WIN_STATE_HILITED );
	DisplayString *lead = instData->getTextDisplayString();
	if( lead == NULL )
		return;

	Int which = -1;
	for( Int k = 0; k < 3; k++ )
		if( window == RandomMapTabButton( k ) )
			which = k;
	UnicodeString tail;
	if( which == 2 && !enabled )
		tail = TheGameText->fetch( "GUI:RandomTabStatsOff" );
	else if( which >= 0 && which < 2 && enabled )
	{
		tail.format( u"%d", tabCount( which ) );
	}

	// the button's own words, put in capitals once; a list tab's count after them in its own string
	UIStyle::menuTab( box, NULL, isOpen, hovered );
	GameFont *labelFont = UIStyle::menuFont( UIStyle::MENU_TEXT_LABEL + 2, TRUE );
	GameFont *tailFont = UIStyle::menuFont( UIStyle::MENU_TEXT_HINT );
	const UnicodeString words = lead->getText();
	const UnicodeString capital = UIStyle::capitals( words );
	if( capital.compare( words ) != 0 )
		lead->setText( capital );
	if( lead->getFont() != labelFont )
		lead->setFont( labelFont );
	Int leadW = 0, leadH = 0;
	lead->getSize( &leadW, &leadH );
	DisplayString *rest = NULL;
	Int restW = 0, restH = 0;
	if( !tail.isEmpty() && which >= 0 )
	{
		rest = pickText( TEXT_TABS + which, tail, tailFont );
		rest->getSize( &restW, &restH );
		restW += pickPx( 12 );
	}
	const Color ink = isOpen ? UIStyle::ACTIVE : ( enabled ? UIStyle::INK : UIStyle::MUTED );
	const Int left = x + ( w - leadW - restW ) / 2;
	lead->draw( left, y + ( h - leadH ) / 2, ink, PICK_SHADOW );
	if( rest )
		rest->draw( left + leadW + pickPx( 12 ), y + ( h - restH ) / 2, UIStyle::MUTED, PICK_SHADOW );
}
