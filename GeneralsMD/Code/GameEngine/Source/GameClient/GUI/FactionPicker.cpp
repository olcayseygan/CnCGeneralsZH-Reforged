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
// The staging rooms' army screen (FactionPicker.h).  One window drawn in code over the whole room,
// modal while it is up, in the front end's look (UIStyle.h): the armies on the left under their
// sides' headings with Random All and Custom Selection over them, the focused army's portrait, name
// and description on the right, Back and Confirm in the room's button row.  It reads the armies from
// the seat's own box, so whatever the box lists (the generals, a mod's sides, Observer where the room
// has one) is what the screen offers, and it hands the pick back through the box.  Every tile is one
// size whatever the box lists; a list taller than its place scrolls, with the wheel and a bar.
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

// the screen on the 1920x1080 board, inside the staging room's panel
static const Real LEFT = UIStyle::ROOM_INSET;
static const Real RIGHT = 1920.0f - UIStyle::ROOM_INSET;
static const Real GRID_RIGHT = 760.0f;
static const Real DETAIL_LEFT = 792.0f;
static const Real TOP = 168.0f;
static const Real TAB_HEIGHT = 44.0f;
static const Real TAB_GAP = 12.0f;
static const Real GRID_TOP = 232.0f;
static const Real GRID_BOTTOM = 916.0f;
static const Real HEADING_HEIGHT = 34.0f;
static const Real GROUP_GAP = 10.0f;
static const Real CELL = 112.0f;
static const Real CELL_GAP = 12.0f;
static const Real BAR_WIDTH = 20.0f;			// in the 28 left over beside five columns of tiles
static const Real BAR_MIN_THUMB = 48.0f;
static const Real PORTRAIT_BOTTOM = 440.0f;
static const Real BANNER_BOTTOM = 504.0f;
static const Real DESCRIPTION_TOP = 516.0f;
static const Real PAD = 16.0f;
static const Real POOL_ICON = 96.0f;
static const Real SIDE_TICK = 26.0f;
static const Real SIDE_TICK_GAP = 14.0f;
static const Real MAX_PORTRAIT_SCALE = 4.0f;	// a 64 px side logo blown up past this is only blur
static const Int POOL_DIM_ALPHA = 190;
static const char *const PICKER_IMAGE_PREFIX = "FactionPicker";	// a template's own picture here, FactionPickerFactionTurkiye

struct PickerCell
{
	Int pick;							///< the box's item data: a template or PLAYERTEMPLATE_OBSERVER
	Int poolBit;					///< its bit in a Random pool, -1 when it cannot go in one
	UnicodeString name;		///< the box's own line, so a mod's faction reads as the box has it
	const Image *icon;
	const Image *portrait;
	UnicodeString strategy;
	UnicodeString features;
	DisplayString *label;	///< the name in the cell, only for an army with no picture
	IRegion2D box;
};

struct PickerGroup
{
	AsciiString leadSide;
	AsciiString baseSide;	///< a mod's groups only
	Int mod;
	UnicodeString name;
	DisplayString *heading;
	ICoord2D at;
	IRegion2D nameBox;		///< a click on the name narrows the pool to the side
	IRegion2D tickBox;		///< Custom Selection's box for the whole side
	UnsignedInt mask;			///< the side's pool bits, 0 for Observer
	std::vector<Int> cells;
};

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
static UnicodeString s_leftOut;		///< the listed armies a plain Random never draws
static Int s_lastPressCell = -1;
static UnsignedInt s_lastPressTime = 0;

static DisplayString *s_title = NULL;
static DisplayString *s_tabText[ 2 ] = { NULL, NULL };
static DisplayString *s_backText = NULL;
static DisplayString *s_confirmText = NULL;
static DisplayString *s_nameText = NULL;
static DisplayString *s_strategyText = NULL;
static DisplayString *s_randomMark = NULL;
static std::vector<DisplayString *> s_featureText;

static IRegion2D s_panelBox, s_tabBox[ 2 ], s_backBox, s_confirmBox, s_portraitBox, s_bannerBox, s_descriptionBox;
static IRegion2D s_gridBox, s_barBox;
static Int s_scroll = 0;					///< pixels of the list above the grid's top
static Int s_scrollMax = 0;				///< 0 when the whole list fits, and then there is no bar
static Int s_thumbGrab = -1;			///< how far down the thumb the mouse holds it, -1 when it does not

//-------------------------------------------------------------------------------------------------
static DisplayString *newText( const UnicodeString &text, GameFont *font )
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont( font );
	string->setText( text );
	return string;
}

static void freeText( DisplayString *&string )
{
	if( string )
		TheDisplayStringManager->freeDisplayString( string );
	string = NULL;
}

static IRegion2D boardBox( Real x0, Real y0, Real x1, Real y1 )
{
	IRegion2D box;
	box.lo.x = UIStyle::boardX( x0 );
	box.lo.y = UIStyle::boardY( y0 );
	box.hi.x = UIStyle::boardX( x1 );
	box.hi.y = UIStyle::boardY( y1 );
	return box;
}

static Bool isInside( const IRegion2D &box, Int x, Int y )
{
	return x >= box.lo.x && x < box.hi.x && y >= box.lo.y && y < box.hi.y;
}

static Int boxWidth( const IRegion2D &box ) { return box.hi.x - box.lo.x; }
static Int boxHeight( const IRegion2D &box ) { return box.hi.y - box.lo.y; }

static void click( void )
{
	AudioEventRTS sound( "GUIClick" );
	TheAudio->addAudioEvent( &sound );
}

static void drawCentred( DisplayString *string, const IRegion2D &box, Color color )
{
	Int width, height;
	string->getSize( &width, &height );
	string->draw( box.lo.x + ( boxWidth( box ) - width ) / 2, box.lo.y + ( boxHeight( box ) - height ) / 2, color, UIStyle::SHADOW );
}

static void drawFitted( const Image *image, const IRegion2D &box, Real maxScale )
{
	const Real scale = min( maxScale, min( (Real)boxWidth( box ) / image->getImageWidth(), (Real)boxHeight( box ) / image->getImageHeight() ) );
	const Int width = REAL_TO_INT( image->getImageWidth() * scale );
	const Int height = REAL_TO_INT( image->getImageHeight() * scale );
	const Int x = box.lo.x + ( boxWidth( box ) - width ) / 2;
	const Int y = box.lo.y + ( boxHeight( box ) - height ) / 2;
	TheDisplay->drawImage( image, x, y, x + width, y + height );
}

/** A Features label lists one thing a line. */
static void splitLines( const UnicodeString &text, std::vector<UnicodeString> &lines )
{
	lines.clear();
	UnicodeString line;
	for( Int each = 0; each <= text.getLength(); each++ )
	{
		const WideChar c = each < text.getLength() ? text.getCharAt( each ) : u'\n';
		// EA's labels carry the break as a real one; a hand-written .str may leave it as the two characters
		const Bool escaped = c == u'\\' && each + 1 < text.getLength() && text.getCharAt( each + 1 ) == u'n';
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

static Int tickableCount( void )
{
	Int count = 0;
	for( size_t each = 0; each < s_cells.size(); each++ )
		count += s_cells[ each ].poolBit >= 0 ? 1 : 0;
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
	if( !isInside( s_gridBox, x, y ) )
		return -1;
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

static Int sideAt( Int x, Int y, Bool tickBox )
{
	if( !isInside( s_gridBox, x, y ) )
		return -1;
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		const PickerGroup &g = s_groups[ group ];
		if( g.mask != 0 && isInside( tickBox ? g.tickBox : g.nameBox, x, y ) )
			return (Int)group;
	}
	return -1;
}

static Bool canConfirm( void )
{
	return s_focus >= 0 || !s_custom || tickedCount() > 0;
}

/// "Contra - USA": EA's own word for a country where there is one, the mod's BaseSide as written otherwise
static UnicodeString modGroupName( Int mod, const AsciiString &baseSide )
{
	WideChar modName[ 128 ];
	WideCharFromUtf8( TheGlobalData->m_modNames[ mod ].str(), modName, ARRAY_SIZE( modName ) );
	UnicodeString name( modName );
	name.concat( u" - " );
	AsciiString label;
	label.format( "SIDE:%s", baseSide == "USA" ? "America" : baseSide.str() );
	Bool exists = FALSE;
	const UnicodeString country = TheGameText->fetch( label, &exists );
	if( exists )
	{
		name.concat( country );
	}
	else
	{
		UnicodeString written;
		written.translate( baseSide );
		name.concat( written );
	}
	return name;
}

//-------------------------------------------------------------------------------------------------
/** The armies in the box, each under the first army of its side: a general's Side begins with his
	* side's ("AmericaLaserGeneral" under "America"), so Turkiye, on America's BaseSide but a side of its
	* own, heads its own group, and a mod's sides stay with their mod. */
static void buildCells( void )
{
	std::vector<Int> candidates;
	GetRandomPoolCandidates( candidates );

	GameWindow *listBox = GadgetComboBoxGetListBox( s_combo );
	GameFont *smallFont = UIStyle::font( UIStyle::TEXT_SMALL );
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
		cell.label = NULL;

		AsciiString side = "Observer";
		AsciiString baseSide;
		Int mod = -1;
		if( pick == PLAYERTEMPLATE_OBSERVER )
		{
			cell.icon = cell.portrait = TheMappedImageCollection->findImageByName( "GameinfoOBSRVR" );
		}
		else
		{
			const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate( pick );
			side = pt->getSide();
			mod = pt->getModIndex();
			for( size_t n = 0; n < candidates.size() && n < RANDOM_POOL_BITS; n++ )
			{
				if( candidates[ n ] == pick )
					cell.poolBit = (Int)n;
			}
			// a retail general has the challenge screen's pictures of him; a side has its logo, unless a
			// picture is mapped for this screen alone (Turkiye borrows America's logo everywhere else)
			const GeneralPersona *persona = TheChallengeGenerals->getGeneralByTemplateName( pt->getName() );
			AsciiString ownImage( PICKER_IMAGE_PREFIX );
			ownImage.concat( pt->getName() );
			const Image *own = TheMappedImageCollection->findImageByName( ownImage );
			const Image *logo = own ? own : ( pt->getGeneralImage() ? pt->getGeneralImage() : pt->getSideIconImage() );
			cell.icon = persona && persona->getBioPortraitSmall() ? persona->getBioPortraitSmall() : logo;
			cell.portrait = persona && persona->getBioPortraitLarge() ? persona->getBioPortraitLarge() : logo;
			if( !pt->getTooltip().isEmpty() )
				cell.strategy = TheGameText->fetch( pt->getTooltip() );
			if( !pt->getGeneralFeatures().isEmpty() )
				cell.features = TheGameText->fetch( pt->getGeneralFeatures() );
			baseSide = pt->getBaseSide();
			// every mod faction stays out of a plain Random, too many to name; the list of who it draws says so
			if( pt->isRandomExcluded() && mod < 0 )
			{
				if( !s_leftOut.isEmpty() )
					s_leftOut.concat( u", " );
				s_leftOut.concat( cell.name );
			}
		}
		if( !cell.icon )
		{
			cell.label = newText( cell.name, smallFont );
			cell.label->setWordWrapCentered( TRUE );
		}

		// A mod lists its generals in any order and its plain side last (Contra), so its groups go by
		// BaseSide: one heading a country, however many generals it brings.
		size_t group = 0;
		for( ; group < s_groups.size(); group++ )
		{
			const PickerGroup &g = s_groups[ group ];
			if( g.mod == mod && ( mod >= 0 ? g.baseSide == baseSide : side.startsWith( g.leadSide ) ) )
				break;
		}
		if( group == s_groups.size() )
		{
			PickerGroup lead;
			lead.leadSide = side;
			lead.baseSide = baseSide;
			lead.mod = mod;
			lead.name = mod >= 0 ? modGroupName( mod, baseSide ) : cell.name;
			lead.heading = newText( lead.name, UIStyle::font( UIStyle::TEXT_HEADING ) );
			lead.mask = 0;
			s_groups.push_back( lead );
		}
		if( cell.poolBit >= 0 )
			s_groups[ group ].mask |= 1u << cell.poolBit;
		s_groups[ group ].cells.push_back( (Int)s_cells.size() );
		s_cells.push_back( cell );
	}
}

/** Places everything with the list at its top; what does not fit under the grid's bottom scrolls. */
static void layout( void )
{
	s_panelBox = boardBox( UIStyle::ROOM_PANEL_LEFT, UIStyle::ROOM_PANEL_TOP, UIStyle::ROOM_PANEL_RIGHT, UIStyle::ROOM_PANEL_BOTTOM );
	const Real tabWidth = ( GRID_RIGHT - LEFT - TAB_GAP ) / 2;
	s_tabBox[ 0 ] = boardBox( LEFT, TOP, LEFT + tabWidth, TOP + TAB_HEIGHT );
	s_tabBox[ 1 ] = boardBox( GRID_RIGHT - tabWidth, TOP, GRID_RIGHT, TOP + TAB_HEIGHT );
	s_gridBox = boardBox( LEFT, GRID_TOP, GRID_RIGHT - BAR_WIDTH, GRID_BOTTOM );
	s_barBox = boardBox( GRID_RIGHT - BAR_WIDTH, GRID_TOP, GRID_RIGHT, GRID_BOTTOM );

	const Int columns = (Int)( ( GRID_RIGHT - LEFT + CELL_GAP ) / ( CELL + CELL_GAP ) );
	Real y = GRID_TOP;
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		PickerGroup &g = s_groups[ group ];
		g.heading->setClipRegion( &s_gridBox );
		g.at.x = UIStyle::boardX( LEFT );
		g.at.y = UIStyle::boardY( y );
		Int nameWidth, nameHeight;
		// text shrinks slower than the board, and at 1024x768 the heading's letters ran under the tiles
		const UIStyle::TextSize headingSizes[] = { UIStyle::TEXT_HEADING, UIStyle::TEXT_STRONG, UIStyle::TEXT_BODY };
		for( size_t size = 0; size < ARRAY_SIZE( headingSizes ); size++ )
		{
			g.heading->setFont( UIStyle::font( headingSizes[ size ] ) );
			g.heading->getSize( &nameWidth, &nameHeight );
			if( nameHeight <= UIStyle::px( HEADING_HEIGHT ) )
				break;
		}
		g.nameBox.lo = g.at;
		g.nameBox.hi.x = g.at.x + nameWidth;
		g.nameBox.hi.y = g.at.y + nameHeight;
		const Int tick = UIStyle::px( SIDE_TICK );
		g.tickBox.lo.x = g.nameBox.hi.x + UIStyle::px( SIDE_TICK_GAP );
		g.tickBox.lo.y = g.at.y + ( nameHeight - tick ) / 2;
		g.tickBox.hi.x = g.tickBox.lo.x + tick;
		g.tickBox.hi.y = g.tickBox.lo.y + tick;
		y += HEADING_HEIGHT;
		for( size_t each = 0; each < g.cells.size(); each++ )
		{
			const Real x = LEFT + ( each % columns ) * ( CELL + CELL_GAP );
			const Real top = y + ( each / columns ) * ( CELL + CELL_GAP );
			PickerCell &c = s_cells[ g.cells[ each ] ];
			c.box = boardBox( x, top, x + CELL, top + CELL );
			c.box.hi.x = c.box.lo.x + UIStyle::px( CELL );	// one size however the board's corners round
			c.box.hi.y = c.box.lo.y + UIStyle::px( CELL );
			if( c.label )
			{
				c.label->setWordWrap( boxWidth( c.box ) - UIStyle::px( 8 ) );
				c.label->setClipRegion( &s_gridBox );
			}
		}
		y += ( ( (Int)g.cells.size() + columns - 1 ) / columns ) * ( CELL + CELL_GAP ) + GROUP_GAP;
	}
	s_scroll = 0;
	s_scrollMax = max( 0, UIStyle::boardY( y - CELL_GAP - GROUP_GAP ) - s_gridBox.hi.y );

	s_portraitBox = boardBox( DETAIL_LEFT, TOP, RIGHT, PORTRAIT_BOTTOM );
	s_bannerBox = boardBox( DETAIL_LEFT, PORTRAIT_BOTTOM + 8.0f, RIGHT, BANNER_BOTTOM );
	s_descriptionBox = boardBox( DETAIL_LEFT, DESCRIPTION_TOP, RIGHT, GRID_BOTTOM );
	s_backBox = boardBox( LEFT, UIStyle::ROOM_BUTTON_TOP, LEFT + UIStyle::ROOM_BUTTON_WIDTH, UIStyle::ROOM_BUTTON_BOTTOM );
	s_confirmBox = boardBox( RIGHT - UIStyle::ROOM_WIDE_BUTTON_WIDTH, UIStyle::ROOM_BUTTON_TOP, RIGHT, UIStyle::ROOM_BUTTON_BOTTOM );
}

/** The boxes stay in screen pixels, so a scroll moves them and drawing and clicks need nothing else. */
static void setScroll( Int scroll )
{
	scroll = max( 0, min( scroll, s_scrollMax ) );
	const Int shift = s_scroll - scroll;
	s_scroll = scroll;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		s_cells[ each ].box.lo.y += shift;
		s_cells[ each ].box.hi.y += shift;
	}
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		PickerGroup &g = s_groups[ group ];
		g.at.y += shift;
		g.nameBox.lo.y += shift;
		g.nameBox.hi.y += shift;
		g.tickBox.lo.y += shift;
		g.tickBox.hi.y += shift;
	}
}

static Int thumbHeight( void )
{
	const Int track = boxHeight( s_barBox );
	return max( UIStyle::px( BAR_MIN_THUMB ), track * track / ( track + s_scrollMax ) );
}

static void dragThumb( Int y )
{
	setScroll( FactionPickerScrollAtThumb( y - s_barBox.lo.y - s_thumbGrab, s_scrollMax, boxHeight( s_barBox ), thumbHeight() ) );
}

/// on the thumb the press takes hold of it where it is; on the track the thumb comes to the mouse
static void pressBar( Int y )
{
	const Int top = s_barBox.lo.y + FactionPickerThumbTop( s_scroll, s_scrollMax, boxHeight( s_barBox ), thumbHeight() );
	const Bool onThumb = y >= top && y < top + thumbHeight();
	s_thumbGrab = onThumb ? y - top : thumbHeight() / 2;
	if( !onThumb )
		dragThumb( y );
}

/** The name, the description and the Confirm label follow the focus. */
static void refresh( void )
{
	UnicodeString name, strategy, features;
	if( s_focus >= 0 )
	{
		const PickerCell &cell = s_cells[ s_focus ];
		name = cell.name;
		strategy = cell.strategy;
		features = cell.features;
	}
	else
	{
		const Int side = poolSide();
		if( side >= 0 )
			name.format( TheGameText->fetch( "GUI:FactionPickRandomSide" ), s_groups[ side ].name.str() );
		else
			name = TheGameText->fetch( "GUI:Random" );
		if( s_custom )
		{
			strategy.format( TheGameText->fetch( "GUI:FactionPickPool" ), tickedCount(), tickableCount() );
			strategy.concat( u' ' );
			strategy.concat( TheGameText->fetch( "GUI:FactionPickSideHint" ) );
		}
		else
		{
			strategy = TheGameText->fetch( "GUI:FactionPickRandom" );
			if( !s_leftOut.isEmpty() )
			{
				UnicodeString leftOut;
				leftOut.format( TheGameText->fetch( "GUI:FactionPickLeftOut" ), s_leftOut.str() );
				strategy.concat( u' ' );
				strategy.concat( leftOut );
			}
			strategy.concat( u' ' );
			strategy.concat( TheGameText->fetch( "GUI:FactionPickSideNameHint" ) );
		}
		// the armies the draw can land on
		for( size_t each = 0; each < s_cells.size(); each++ )
		{
			const PickerCell &cell = s_cells[ each ];
			if( !isDrawn( cell ) )
				continue;
			features.concat( cell.name );
			features.concat( u'\n' );
		}
	}

	s_nameText->setText( UIStyle::capitals( name ) );
	s_strategyText->setText( strategy );

	for( size_t each = 0; each < s_featureText.size(); each++ )
		freeText( s_featureText[ each ] );
	s_featureText.clear();
	std::vector<UnicodeString> lines;
	splitLines( features, lines );
	const Int columnWidth = ( boxWidth( s_descriptionBox ) - UIStyle::px( 3 * PAD ) ) / 2;
	for( size_t each = 0; each < lines.size(); each++ )
	{
		UnicodeString line( u"- " );
		line.concat( lines[ each ] );
		DisplayString *text = newText( line, UIStyle::font( UIStyle::TEXT_BODY ) );
		text->setWordWrap( columnWidth );
		s_featureText.push_back( text );
	}

	UnicodeString confirm;
	confirm.format( TheGameText->fetch( "GUI:FactionPickConfirm" ), name.str() );
	s_confirmText->setText( UIStyle::capitals( confirm ) );
	// "Contra - USA Air Force General" is wider than the button in the heading's letters
	const UIStyle::TextSize sizes[] = { UIStyle::TEXT_HEADING, UIStyle::TEXT_STRONG, UIStyle::TEXT_BODY };
	for( size_t each = 0; each < ARRAY_SIZE( sizes ); each++ )
	{
		s_confirmText->setFont( UIStyle::titleFont( sizes[ each ] ) );
		Int width, height;
		s_confirmText->getSize( &width, &height );
		if( width <= boxWidth( s_confirmBox ) - UIStyle::px( PAD ) )
			break;
	}
}

//-------------------------------------------------------------------------------------------------
static void drawCellPicture( const PickerCell &cell, const IRegion2D &box )
{
	const Int inset = UIStyle::px( 6 );
	IRegion2D inner = box;
	inner.lo.x += inset;
	inner.lo.y += inset;
	inner.hi.x -= inset;
	inner.hi.y -= inset;
	if( cell.icon )
		drawFitted( cell.icon, inner, 100.0f );
	else
		drawCentred( cell.label, box, UIStyle::INK );
}

/// the clip draws an open box's top and left line a pixel out, so only what the grid's edge cuts takes it
static void clipIfCut( const IRegion2D &box )
{
	if( box.lo.y < s_gridBox.lo.y || box.hi.y > s_gridBox.hi.y )
		TheDisplay->setClipRegion( &s_gridBox );
	else
		TheDisplay->enableClipping( FALSE );
}

/// the display's clip stops images and open boxes, and a filled box goes round it
static void fillInGrid( Int x, Int y, Int width, Int height, Color color )
{
	const Int top = max( y, s_gridBox.lo.y );
	const Int bottom = min( y + height, s_gridBox.hi.y );
	if( bottom > top )
		TheDisplay->drawFillRect( x, top, width, bottom - top, color );
}

static void panelInGrid( Int x, Int y, Int width, Int height, Color fill )
{
	fillInGrid( x, y, width, height, fill );
	TheDisplay->drawOpenRect( x, y, width, height, 1.0f, UIStyle::FRAME );
}

static void drawTickBox( Int x, Int y, Int size, Bool ticked )
{
	panelInGrid( x, y, size, size, ticked ? UIStyle::ACTIVE : UIStyle::CARD );
	// the check mark's lines go round the clip too, and half a tick says nothing
	if( !ticked || y < s_gridBox.lo.y || y + size > s_gridBox.hi.y )
		return;
	const Real line = (Real)max( 2, UIStyle::px( 3 ) );
	TheDisplay->drawLine( x + size / 5, y + size / 2, x + size * 2 / 5, y + size * 3 / 4, line, UIStyle::GROUND );
	TheDisplay->drawLine( x + size * 2 / 5, y + size * 3 / 4, x + size * 4 / 5, y + size / 4, line, UIStyle::GROUND );
}

/** Random's preview: every army a Random can be, the ones it can land on lit and the rest dimmed. */
static void drawPoolPreview( const IRegion2D &area )
{
	std::vector<Int> shown;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( s_cells[ each ].poolBit >= 0 )
			shown.push_back( (Int)each );
	}
	if( shown.empty() )
		return;

	const Int gap = UIStyle::px( CELL_GAP );
	const Int count = (Int)shown.size();
	Int size = UIStyle::px( POOL_ICON );
	Int columns = 1;
	Int rows = 1;
	for( ;; )
	{
		columns = max( 1, ( boxWidth( area ) + gap ) / ( size + gap ) );
		rows = ( count + columns - 1 ) / columns;
		if( rows * ( size + gap ) - gap <= boxHeight( area ) || size <= gap )
			break;
		size -= gap;
	}
	columns = min( columns, count );
	const Int left = area.lo.x + ( boxWidth( area ) - ( columns * ( size + gap ) - gap ) ) / 2;
	const Int top = area.lo.y + ( boxHeight( area ) - ( rows * ( size + gap ) - gap ) ) / 2;
	for( Int n = 0; n < count; n++ )
	{
		const PickerCell &cell = s_cells[ shown[ n ] ];
		// a short last row sits centred under the full ones
		const Int inRow = min( columns, count - ( n / columns ) * columns );
		const Int rowShift = ( columns - inRow ) * ( size + gap ) / 2;
		IRegion2D box;
		box.lo.x = left + rowShift + ( n % columns ) * ( size + gap );
		box.lo.y = top + ( n / columns ) * ( size + gap );
		box.hi.x = box.lo.x + size;
		box.hi.y = box.lo.y + size;
		UIStyle::panel( box.lo.x, box.lo.y, size, size, UIStyle::CARD );
		drawCellPicture( cell, box );
		if( !isDrawn( cell ) )
			TheDisplay->drawFillRect( box.lo.x, box.lo.y, size, size, UIStyle::withAlpha( UIStyle::GROUND, POOL_DIM_ALPHA ) );
	}
}

static void drawPicker( GameWindow *window, WinInstanceData *instData )
{
	// the room stays up underneath for its updates, so the screen paints over every pixel of it
	UIStyle::shell();
	UIStyle::panel( s_panelBox.lo.x, s_panelBox.lo.y, boxWidth( s_panelBox ), boxHeight( s_panelBox ), UIStyle::PANEL );

	Int width, height;
	s_title->getSize( &width, &height );
	s_title->draw( UIStyle::boardX( UIStyle::ROOM_TITLE_LEFT ), UIStyle::boardY( UIStyle::ROOM_TITLE_MIDDLE ) - height / 2, UIStyle::INK, UIStyle::SHADOW );
	UIStyle::rule( s_panelBox.lo.x, UIStyle::boardY( UIStyle::ROOM_HEADER_LINE ), boxWidth( s_panelBox ) );

	const Bool tabOn[ 2 ] = { s_focus < 0 && !s_custom, s_custom };
	for( Int tab = 0; tab < 2; tab++ )
	{
		const IRegion2D &box = s_tabBox[ tab ];
		UIStyle::tab( box.lo.x, box.lo.y, boxWidth( box ), boxHeight( box ) );
		drawCentred( s_tabText[ tab ], box, tabOn[ tab ] ? UIStyle::ACTIVE : UIStyle::INK );
	}

	const ICoord2D mouse = TheMouse->getMouseStatus()->pos;
	const Bool mouseInGrid = isInside( s_gridBox, mouse.x, mouse.y );
	for( size_t group = 0; group < s_groups.size(); group++ )
	{
		// the name is a button (that side alone), lit under the mouse; the box is Custom Selection's
		const PickerGroup &g = s_groups[ group ];
		const Bool narrows = g.mask != 0;
		const Bool hovered = narrows && mouseInGrid && isInside( g.nameBox, mouse.x, mouse.y );
		g.heading->draw( g.at.x, g.at.y, hovered ? UIStyle::EDGE_LIT : UIStyle::ACTIVE, UIStyle::SHADOW );
		clipIfCut( g.tickBox );
		if( s_custom && narrows )
			drawTickBox( g.tickBox.lo.x, g.tickBox.lo.y, boxWidth( g.tickBox ), FactionPickerSideTicked( s_mask, g.mask ) );
	}
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		const PickerCell &cell = s_cells[ each ];
		const IRegion2D &box = cell.box;
		// the display's clip takes a picture wholly under the grid for a turned-over one
		if( box.hi.y <= s_gridBox.lo.y || box.lo.y >= s_gridBox.hi.y )
			continue;
		clipIfCut( box );
		panelInGrid( box.lo.x, box.lo.y, boxWidth( box ), boxHeight( box ), UIStyle::CARD );
		drawCellPicture( cell, box );
		const Int inset = UIStyle::px( 6 );

		if( s_custom )
		{
			// out of the pool reads as greyed; an army that cannot go in a pool has no box to tick
			if( !isTicked( cell ) )
				fillInGrid( box.lo.x, box.lo.y, boxWidth( box ), boxHeight( box ), UIStyle::withAlpha( UIStyle::GROUND, 150 ) );
			if( cell.poolBit >= 0 )
			{
				const Int size = UIStyle::px( SIDE_TICK );
				drawTickBox( box.hi.x - size - inset, box.lo.y + inset, size, isTicked( cell ) );
			}
		}
		if( (Int)each == s_focus )
			UIStyle::edge( box.lo.x, box.lo.y, boxWidth( box ), boxHeight( box ) );
		else if( mouseInGrid && isInside( box, mouse.x, mouse.y ) )
			TheDisplay->drawOpenRect( box.lo.x, box.lo.y, boxWidth( box ), boxHeight( box ), 1.0f, UIStyle::EDGE_LIT );
	}
	TheDisplay->enableClipping( FALSE );
	if( s_scrollMax > 0 )
	{
		UIStyle::panel( s_barBox.lo.x, s_barBox.lo.y, boxWidth( s_barBox ), boxHeight( s_barBox ), UIStyle::CARD );
		const Int top = s_barBox.lo.y + FactionPickerThumbTop( s_scroll, s_scrollMax, boxHeight( s_barBox ), thumbHeight() );
		UIStyle::button( s_barBox.lo.x, top, boxWidth( s_barBox ), thumbHeight() );
	}

	UIStyle::panel( s_portraitBox.lo.x, s_portraitBox.lo.y, boxWidth( s_portraitBox ), boxHeight( s_portraitBox ), UIStyle::CARD );
	IRegion2D portraitInner = s_portraitBox;
	const Int portraitInset = UIStyle::px( PAD );
	portraitInner.lo.x += portraitInset;
	portraitInner.lo.y += portraitInset;
	portraitInner.hi.x -= portraitInset;
	portraitInner.hi.y -= portraitInset;
	if( s_focus < 0 )
		drawPoolPreview( portraitInner );
	else if( s_cells[ s_focus ].portrait )
		drawFitted( s_cells[ s_focus ].portrait, portraitInner, MAX_PORTRAIT_SCALE );
	else
		drawCentred( s_randomMark, s_portraitBox, UIStyle::MUTED );

	UIStyle::panel( s_bannerBox.lo.x, s_bannerBox.lo.y, boxWidth( s_bannerBox ), boxHeight( s_bannerBox ), UIStyle::CARD );
	UIStyle::stripe( s_bannerBox.lo.x, s_bannerBox.lo.y, boxHeight( s_bannerBox ), UIStyle::GOLD );
	drawCentred( s_nameText, s_bannerBox, UIStyle::INK );

	const IRegion2D &d = s_descriptionBox;
	UIStyle::panel( d.lo.x, d.lo.y, boxWidth( d ), boxHeight( d ), UIStyle::CARD );
	const Int pad = UIStyle::px( PAD );
	s_strategyText->draw( d.lo.x + pad, d.lo.y + pad, UIStyle::INK, UIStyle::SHADOW );
	s_strategyText->getSize( &width, &height );
	const Int featuresTop = d.lo.y + pad + ( height > 0 ? height + pad : 0 );
	const Int perColumn = ( (Int)s_featureText.size() + 1 ) / 2;
	Int y = featuresTop;
	for( size_t each = 0; each < s_featureText.size(); each++ )
	{
		if( (Int)each == perColumn )
			y = featuresTop;
		const Int x = d.lo.x + pad + ( (Int)each < perColumn ? 0 : ( boxWidth( d ) - pad ) / 2 );
		s_featureText[ each ]->draw( x, y, UIStyle::MUTED, UIStyle::SHADOW );
		s_featureText[ each ]->getSize( &width, &height );
		y += height;
	}

	UIStyle::button( s_backBox.lo.x, s_backBox.lo.y, boxWidth( s_backBox ), boxHeight( s_backBox ) );
	drawCentred( s_backText, s_backBox, UIStyle::INK );
	UIStyle::button( s_confirmBox.lo.x, s_confirmBox.lo.y, boxWidth( s_confirmBox ), boxHeight( s_confirmBox ) );
	drawCentred( s_confirmText, s_confirmBox, canConfirm() ? UIStyle::INK : UIStyle::MUTED );
}

//-------------------------------------------------------------------------------------------------
static void closePicker( Bool restoreFocus )
{
	s_window->winHide( TRUE );
	TheWindowManager->winUnsetModal( s_window );
	TheWindowManager->winDestroy( s_window );
	s_window = NULL;
	if( restoreFocus )
		TheWindowManager->winSetFocus( s_oldFocus );
	s_oldFocus = NULL;

	for( size_t each = 0; each < s_cells.size(); each++ )
		freeText( s_cells[ each ].label );
	for( size_t group = 0; group < s_groups.size(); group++ )
		freeText( s_groups[ group ].heading );
	for( size_t each = 0; each < s_featureText.size(); each++ )
		freeText( s_featureText[ each ] );
	s_cells.clear();
	s_groups.clear();
	s_featureText.clear();
	freeText( s_title );
	freeText( s_tabText[ 0 ] );
	freeText( s_tabText[ 1 ] );
	freeText( s_backText );
	freeText( s_confirmText );
	freeText( s_nameText );
	freeText( s_strategyText );
	freeText( s_randomMark );
	s_leftOut.clear();
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
	DEBUG_LOG(( "FACTIONPICKER confirm pick %d \"%s\" list scrolled %d of %d\n", pick, pickedName.str(), s_scroll, s_scrollMax ));
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
		GadgetComboBoxSetItemData( combo, line, (void *)(intptr_t)pick );
		GadgetComboBoxSetSelectedPos( combo, line, TRUE );
		// the list only reports a change of line, and Random to a Random pool is none
		TheWindowManager->winSendSystemMsg( combo->winGetOwner(), GCM_SELECTED, (WindowMsgData)combo, 0 );
		return;
	}
}

/// a click on a side's name: Random from that side alone ("USA random")
static void narrowToSide( Int side )
{
	s_custom = TRUE;
	s_focus = -1;
	s_mask = s_groups[ side ].mask;
	refresh();
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
		click();
		confirm();
		return;
	}
	if( isInside( s_tabBox[ 0 ], x, y ) )
	{
		click();
		s_custom = FALSE;
		s_focus = -1;
		refresh();
		return;
	}
	if( isInside( s_tabBox[ 1 ], x, y ) )
	{
		click();
		s_custom = !s_custom;
		s_focus = -1;
		refresh();
		return;
	}
	const Int tickedSide = s_custom ? sideAt( x, y, TRUE ) : -1;
	if( tickedSide >= 0 )
	{
		click();
		s_mask = FactionPickerToggleSide( s_mask & listedMask(), s_groups[ tickedSide ].mask );
		refresh();
		return;
	}
	const Int namedSide = sideAt( x, y, FALSE );
	if( namedSide >= 0 )
	{
		click();
		narrowToSide( namedSide );
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
	refresh();
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
		case GWM_WHEEL_UP:
			setScroll( s_scroll - UIStyle::px( CELL + CELL_GAP ) );
			break;
		case GWM_WHEEL_DOWN:
			setScroll( s_scroll + UIStyle::px( CELL + CELL_GAP ) );
			break;
		case GWM_LEFT_DOWN:
			if( s_scrollMax > 0 && isInside( s_barBox, x, y ) )
				pressBar( y );
			break;
		case GWM_LEFT_DRAG:
			if( s_thumbGrab >= 0 )
				dragThumb( y );
			break;
		case GWM_LEFT_UP:
			// a press that took the thumb ends here wherever the mouse went, on a tile or not
			if( s_thumbGrab >= 0 )
				s_thumbGrab = -1;
			else
				releaseAt( x, y );
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

static void openPicker( GameWindow *combo )
{
	if( s_window )
		return;
	s_combo = combo;
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
	s_thumbGrab = -1;
	for( size_t each = 0; each < s_cells.size(); each++ )
	{
		if( s_cells[ each ].pick == current )
			s_focus = (Int)each;
	}

	s_title = newText( UIStyle::capitals( TheGameText->fetch( "GUI:FactionPickTitle" ) ), UIStyle::titleFont( UIStyle::TEXT_TITLE ) );
	s_tabText[ 0 ] = newText( TheGameText->fetch( "GUI:FactionPickRandomAll" ), UIStyle::font( UIStyle::TEXT_BODY ) );
	s_tabText[ 1 ] = newText( TheGameText->fetch( "GUI:FactionPickCustom" ), UIStyle::font( UIStyle::TEXT_BODY ) );
	s_backText = newText( UIStyle::capitals( TheGameText->fetch( "GUI:Back" ) ), UIStyle::titleFont( UIStyle::TEXT_HEADING ) );
	s_confirmText = newText( UnicodeString::TheEmptyString, UIStyle::titleFont( UIStyle::TEXT_HEADING ) );
	s_nameText = newText( UnicodeString::TheEmptyString, UIStyle::titleFont( UIStyle::TEXT_TITLE ) );
	s_strategyText = newText( UnicodeString::TheEmptyString, UIStyle::font( UIStyle::TEXT_STRONG ) );
	s_randomMark = newText( UnicodeString( u"?" ), UIStyle::titleFont( UIStyle::TEXT_DISPLAY ) );
	layout();
	if( s_focus >= 0 )
		setScroll( s_cells[ s_focus ].box.hi.y - s_gridBox.hi.y );	// the seat's army in view, at the foot when it has to scroll
	s_strategyText->setWordWrap( boxWidth( s_descriptionBox ) - UIStyle::px( 2 * PAD ) );
	refresh();

	s_window = TheWindowManager->winCreate( NULL, WIN_STATUS_ENABLED | WIN_STATUS_ABOVE, 0, 0,
		TheDisplay->getWidth(), TheDisplay->getHeight(), pickerSystem );
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
			continue;	// a mod's layout with fewer seats
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
			refresh();
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
		closePicker( FALSE );
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
			GadgetComboBoxSetItemData( combo, line, (void *)(intptr_t)pick );
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
