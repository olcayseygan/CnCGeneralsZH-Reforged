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

// HtmlOverlay.cpp ////////////////////////////////////////////////////////////////////////////////
// litehtml's drawing callbacks answered with the game's 2D calls, fonts and mapped images.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/HtmlOverlay.h"
#include "GameClient/HtmlTemplate.h"
#include "GameClient/Image.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"

// BaseType.h's min and max macros would eat std::min and std::max inside litehtml's headers
#undef min
#undef max

#include <litehtml.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>

#include <algorithm>
#include <map>
#include <math.h>
#include <vector>

namespace
{

const litehtml::pixel_t DEFAULT_FONT_SIZE = 12;		///< CSS pixels at 800x600, where a page names none
const litehtml::pixel_t PIXELS_PER_POINT = 96.0f / 72.0f;
const litehtml::pixel_t BROWSER_DPI = 96;
const Int COLOR_BITS = 8;
const Int BOLD_WEIGHT = 600;										///< CSS font-weight from which the game's bold face is used
const Real ASCENT_SHARE = 0.8f;								///< of the font's height; GameFont only knows the height
const Real X_HEIGHT_SHARE = 0.5f;
const char *const PAGE_FOLDER = "Window\\Html\\";
const char *const EMPTY_PAGE = "<!DOCTYPE html><html></html>";	///< standards mode, as every page is
const char *const SVG_EXTENSION = ".svg";
const char *const SVG_UNITS = "px";
const Int RGBA_BYTES = 4;
const char *const GENERIC_FAMILIES[] = { "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui" };

const Int OPAQUE_ALPHA = 255;

/** The first family in a CSS font-family list, without its quotes. */
std::string firstFamily( const std::string &families )
{
	std::string family = families.substr( 0, families.find( ',' ) );
	const size_t first = family.find_first_not_of( " \t\"'" );
	const size_t last = family.find_last_not_of( " \t\"'" );
	return first == std::string::npos ? std::string() : family.substr( first, last - first + 1 );
}

Bool isGenericFamily( const std::string &family )
{
	for( size_t index = 0; index < ARRAY_SIZE( GENERIC_FAMILIES ); index++ )
		if( _stricmp( family.c_str(), GENERIC_FAMILIES[ index ] ) == 0 )
			return TRUE;
	return FALSE;
}

Bool isSvg( const std::string &source )
{
	const size_t extension = strlen( SVG_EXTENSION );
	return source.size() > extension && _stricmp( source.c_str() + source.size() - extension, SVG_EXTENSION ) == 0;
}

}	// namespace

//-------------------------------------------------------------------------------------------------
class HtmlOverlayContainer : public litehtml::document_container
{
public:
	explicit HtmlOverlayContainer( const AsciiString &defaultFont );
	~HtmlOverlayContainer( void );

	void setPage( const std::string &html );
	void draw( void );
	Bool hover( const ICoord2D &mouse );
	std::string click( const ICoord2D &mouse );
	std::string tip( IRegion2D &rect );
	Int bottomOf( const char *selector );
	void rectsOf( const char *selector, std::vector< IRegion2D > &rects );
	void setAlpha( Int alpha ) { m_alpha = alpha; }

	litehtml::uint_ptr create_font( const litehtml::font_description &description, const litehtml::document *document,
																	litehtml::font_metrics *metrics ) override;
	void delete_font( litehtml::uint_ptr font ) override {}
	litehtml::pixel_t text_width( const char *text, litehtml::uint_ptr font ) override;
	void draw_text( litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr font, litehtml::web_color color,
									const litehtml::position &place ) override;
	litehtml::pixel_t pt_to_px( float points ) const override { return points * PIXELS_PER_POINT; }
	litehtml::pixel_t get_default_font_size( void ) const override { return DEFAULT_FONT_SIZE; }
	const char *get_default_font_name( void ) const override { return m_defaultFont.c_str(); }
	void draw_list_marker( litehtml::uint_ptr hdc, const litehtml::list_marker &marker ) override {}
	void load_image( const char *source, const char *baseUrl, bool redrawOnReady ) override {}
	void get_image_size( const char *source, const char *baseUrl, litehtml::size &size ) override;
	void draw_image( litehtml::uint_ptr hdc, const litehtml::background_layer &layer, const std::string &url,
									 const std::string &baseUrl ) override;
	void draw_solid_fill( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
												const litehtml::web_color &color ) override;
	void draw_linear_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														 const litehtml::background_layer::linear_gradient &gradient ) override;
	void draw_radial_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														 const litehtml::background_layer::radial_gradient &gradient ) override;
	void draw_conic_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														const litehtml::background_layer::conic_gradient &gradient ) override;
	void draw_borders( litehtml::uint_ptr hdc, const litehtml::borders &borders, const litehtml::position &place,
										 bool root ) override;
	void set_caption( const char *caption ) override {}
	void set_base_url( const char *baseUrl ) override {}
	void link( const std::shared_ptr< litehtml::document > &document, const litehtml::element::ptr &element ) override {}
	void on_anchor_click( const char *url, const litehtml::element::ptr &element ) override {}
	bool on_element_click( const litehtml::element::ptr &element ) override;
	void on_mouse_event( const litehtml::element::ptr &element, litehtml::mouse_event event ) override {}
	void set_cursor( const char *cursor ) override {}
	void transform_text( litehtml::string &text, litehtml::text_transform transform ) override;
	void import_css( litehtml::string &text, const litehtml::string &url, litehtml::string &baseUrl ) override;
	void set_clip( const litehtml::position &place, const litehtml::border_radiuses &radiuses ) override {}
	void del_clip( void ) override {}
	void get_viewport( litehtml::position &viewport ) const override;
	litehtml::element::ptr create_element( const char *tagName, const litehtml::string_map &attributes,
																				 const std::shared_ptr< litehtml::document > &document ) override { return nullptr; }
	void get_media_features( litehtml::media_features &media ) const override;
	void get_language( litehtml::string &language, litehtml::string &culture ) const override;

private:
	struct CachedString
	{
		DisplayString *string;
		UnsignedInt stamp;	///< the draw that last used it
	};
	typedef std::map< std::pair< GameFont *, std::string >, CachedString > StringCache;

	/** A row of pixels of one colour in a rasterised SVG, from the box's corner, in screen pixels. */
	struct SvgRun
	{
		Int x;
		Int y;
		Int length;
		Color color;
	};
	typedef std::vector< SvgRun > SvgRuns;

	/** A colour with its alpha scaled by the whole page's, setAlpha's. */
	Color tint( Int red, Int green, Int blue, Int alpha ) const { return GameMakeColor( red, green, blue, alpha * m_alpha / OPAQUE_ALPHA ); }
	Color tint( const litehtml::web_color &color ) const { return tint( color.red, color.green, color.blue, color.alpha ); }

	Int screen( litehtml::pixel_t pagePixels ) const { return (Int)floorf( pagePixels * m_scale + 0.5f ); }
	litehtml::pixel_t page( Int screenPixels ) const { return screenPixels / m_scale; }
	litehtml::pixel_t viewportWidth( void ) const { return page( m_screenWidth ); }
	litehtml::pixel_t viewportHeight( void ) const { return page( m_screenHeight ); }
	DisplayString *displayString( GameFont *font, const char *text );
	void freeStrings( Bool all );
	void fillBox( const litehtml::position &box, const litehtml::web_color &color );
	NSVGimage *svgImage( const std::string &source );
	const SvgRuns &svgRuns( const std::string &source, Int width, Int height );

	std::string							m_defaultFont;
	NSVGrasterizer *				m_rasterizer;
	std::map< std::string, NSVGimage * >	m_svgImages;	///< by page path; NULL for a file missing or unreadable
	std::map< std::string, SvgRuns >			m_svgRuns;		///< by page path and screen size
	std::string							m_page;
	litehtml::document::ptr	m_document;
	litehtml::document::ptr	m_cssDocument;	///< an empty page the stylesheets are parsed against, for its quirks mode
	litehtml::css						m_masterCss;		///< litehtml's own stylesheet, parsed once
	litehtml::css						m_pageCss;			///< the page's <style>, parsed while its text stays the same
	std::string							m_styles;				///< that text
	Real										m_scale;
	Int											m_screenWidth;
	Int											m_screenHeight;
	StringCache							m_strings;
	UnsignedInt							m_stamp;
	std::string							m_clicked;
	Int											m_alpha;			///< the whole page's, OPAQUE_ALPHA unless it is fading
public:
	Bool										m_hud;				///< laid out at ControlBarHudScale(), the bottom HUD's own
	Bool										m_screenPixels;	///< laid out in the screen's own pixels, over either scale
};

//-------------------------------------------------------------------------------------------------
HtmlOverlayContainer::HtmlOverlayContainer( const AsciiString &defaultFont ) :
	m_defaultFont( defaultFont.str() ),
	m_rasterizer( nsvgCreateRasterizer() ),
	m_scale( 1.0f ),
	m_screenWidth( 0 ),
	m_screenHeight( 0 ),
	m_stamp( 0 ),
	m_alpha( OPAQUE_ALPHA ),
	m_hud( FALSE ),
	m_screenPixels( FALSE )
{
}

//-------------------------------------------------------------------------------------------------
HtmlOverlayContainer::~HtmlOverlayContainer( void )
{
	m_document = nullptr;
	m_cssDocument = nullptr;
	freeStrings( TRUE );
	for( std::map< std::string, NSVGimage * >::iterator image = m_svgImages.begin(); image != m_svgImages.end(); ++image )
		if( image->second )
			nsvgDelete( image->second );
	nsvgDeleteRasterizer( m_rasterizer );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::setPage( const std::string &html )
{
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	const Real scale = m_screenPixels ? 1.0f : m_hud ? ControlBarHudScale() : ControlBarUniformScale();
	if( m_document && html == m_page && width == m_screenWidth && height == m_screenHeight && scale == m_scale )
		return;

	m_page = html;
	m_screenWidth = width;
	m_screenHeight = height;
	m_scale = scale;

	// litehtml makes an element for every word and one for every white space character, each with a
	// whole set of CSS properties, so the stylesheet and the indenting came in as thousands of them:
	// eleven milliseconds to build the command bar and three to throw it away
	std::string body;
	std::string styles;
	HtmlTemplate_compact( html, body, styles );

	// the stylesheets were two thirds of building a page, and they are the same text every time:
	// litehtml's own is parsed once, and the page's again only when its text changes
	if( m_cssDocument == nullptr )
	{
		m_cssDocument = litehtml::document::createFromString( litehtml::estring( EMPTY_PAGE, litehtml::encoding::utf_8 ), this, "", "" );
		m_masterCss.parse_css_stylesheet( std::string( litehtml::master_css ), "", m_cssDocument );
		m_masterCss.sort_selectors();
	}
	if( styles != m_styles )
	{
		m_styles = styles;
		m_pageCss = litehtml::css();
		m_pageCss.parse_css_stylesheet( m_styles, "", m_cssDocument );
		m_pageCss.sort_selectors();
	}

	m_document = litehtml::document::createFromString( litehtml::estring( body, litehtml::encoding::utf_8 ), this, "", "",
																										 &m_masterCss, &m_pageCss );
	m_document->render( viewportWidth() );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw( void )
{
	if( !m_document )
		return;

	m_stamp++;
	const litehtml::position clip( 0, 0, viewportWidth(), viewportHeight() );
	TheDisplay->beginBatch2D();
	m_document->draw( 0, 0, 0, &clip );
	TheDisplay->endBatch2D();
	freeStrings( FALSE );
}

//-------------------------------------------------------------------------------------------------
Bool HtmlOverlayContainer::hover( const ICoord2D &mouse )
{
	if( !m_document )
		return FALSE;

	litehtml::position::vector redraw;
	const litehtml::pixel_t x = page( mouse.x );
	const litehtml::pixel_t y = page( mouse.y );
	if( m_document->on_mouse_over( x, y, x, y, redraw ) )
		m_document->render( viewportWidth() );

	// the root and the body span the whole screen; only what is drawn on them is the page's
	std::shared_ptr< const litehtml::element > over = m_document->get_over_element();
	return over && over->parent() && !over->is_body();
}

//-------------------------------------------------------------------------------------------------
std::string HtmlOverlayContainer::click( const ICoord2D &mouse )
{
	m_clicked.clear();
	if( !m_document )
		return m_clicked;

	litehtml::position::vector redraw;
	const litehtml::pixel_t x = page( mouse.x );
	const litehtml::pixel_t y = page( mouse.y );
	m_document->on_lbutton_down( x, y, x, y, redraw );
	m_document->on_lbutton_up( x, y, x, y, redraw );
	return m_clicked;
}

//-------------------------------------------------------------------------------------------------
std::string HtmlOverlayContainer::tip( IRegion2D &rect )
{
	rect.lo.x = rect.lo.y = rect.hi.x = rect.hi.y = 0;
	if( !m_document )
		return std::string();

	for( std::shared_ptr< const litehtml::element > element = m_document->get_over_element(); element; element = element->parent() )
	{
		const char *text = element->get_attr( "data-tip" );
		if( text == NULL )
			continue;

		const litehtml::position placement = element->get_placement();
		rect.lo.x = screen( placement.x );
		rect.lo.y = screen( placement.y );
		rect.hi.x = screen( placement.x + placement.width );
		rect.hi.y = screen( placement.y + placement.height );
		return text;
	}
	return std::string();
}

//-------------------------------------------------------------------------------------------------
Int HtmlOverlayContainer::bottomOf( const char *selector )
{
	if( !m_document )
		return 0;

	litehtml::element::ptr element = m_document->root()->select_one( selector );
	if( !element )
		return 0;
	const litehtml::position placement = element->get_placement();
	return screen( placement.y + placement.height );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::rectsOf( const char *selector, std::vector< IRegion2D > &rects )
{
	rects.clear();
	if( !m_document )
		return;

	const litehtml::elements_list elements = m_document->root()->select_all( selector );
	for( litehtml::elements_list::const_iterator element = elements.begin(); element != elements.end(); ++element )
	{
		const litehtml::position placement = ( *element )->get_placement();
		if( placement.width <= 0 || placement.height <= 0 )
			continue;

		IRegion2D rect;
		rect.lo.x = screen( placement.x );
		rect.lo.y = screen( placement.y );
		rect.hi.x = screen( placement.x + placement.width );
		rect.hi.y = screen( placement.y + placement.height );
		rects.push_back( rect );
	}
}

//-------------------------------------------------------------------------------------------------
bool HtmlOverlayContainer::on_element_click( const litehtml::element::ptr &element )
{
	const char *action = element->get_attr( "data-click" );
	if( action == NULL )
		return false;
	m_clicked = action;
	return true;
}

//-------------------------------------------------------------------------------------------------
DisplayString *HtmlOverlayContainer::displayString( GameFont *font, const char *text )
{
	const StringCache::key_type key( font, text );
	StringCache::iterator found = m_strings.find( key );
	if( found == m_strings.end() )
	{
		CachedString cached;
		cached.string = TheDisplayStringManager->newDisplayString();
		cached.string->setFont( font );
		cached.string->setText( UnicodeString( MultiByteToWideCharSingleLine( text ).c_str() ) );
		found = m_strings.insert( std::make_pair( key, cached ) ).first;
	}
	found->second.stamp = m_stamp;
	return found->second.string;
}

//-------------------------------------------------------------------------------------------------
/** Strings the last draw did not use go, so a number that changes every half second does not leave
	* a string behind for every value it has had. */
void HtmlOverlayContainer::freeStrings( Bool all )
{
	for( StringCache::iterator cached = m_strings.begin(); cached != m_strings.end(); )
	{
		if( !all && cached->second.stamp == m_stamp )
		{
			++cached;
			continue;
		}
		TheDisplayStringManager->freeDisplayString( cached->second.string );
		cached = m_strings.erase( cached );
	}
}

//-------------------------------------------------------------------------------------------------
litehtml::uint_ptr HtmlOverlayContainer::create_font( const litehtml::font_description &description,
																											const litehtml::document *document,
																											litehtml::font_metrics *metrics )
{
	std::string family = firstFamily( description.family );
	if( family.empty() || isGenericFamily( family ) )
		family = m_defaultFont;

	const Int points = std::max( 1, screen( description.size ) );
	const Bool bold = description.weight >= BOLD_WEIGHT;
	GameFont *font = TheFontLibrary->getFont( AsciiString( family.c_str() ), points, bold );
	if( font == NULL )
		font = TheFontLibrary->getFont( AsciiString( m_defaultFont.c_str() ), points, bold );
	if( font == NULL )
		return 0;

	if( metrics )
	{
		metrics->font_size = description.size;
		metrics->height = page( font->height );
		metrics->ascent = metrics->height * ASCENT_SHARE;
		metrics->descent = metrics->height - metrics->ascent;
		metrics->x_height = metrics->height * X_HEIGHT_SHARE;
		metrics->ch_width = text_width( "0", (litehtml::uint_ptr)font );
		metrics->draw_spaces = false;
	}
	return (litehtml::uint_ptr)font;
}

//-------------------------------------------------------------------------------------------------
/** getSize adds the font's pixel overlap (an eighth of its height, up to four pixels) once a string,
	* and litehtml measures every word and every space as a string of its own, so a gap between two
	* words came out a space plus two overlaps: "Macro  agent" at 1080 lines.  A word is measured by
	* its letters' advance alone and a space keeps one overlap, which is also what stops a small
	* font's space, two pixels of advance or less, from closing up altogether. */
//-------------------------------------------------------------------------------------------------
litehtml::pixel_t HtmlOverlayContainer::text_width( const char *text, litehtml::uint_ptr font )
{
	if( font == 0 )
		return 0;

	DisplayString *string = displayString( (GameFont *)font, text );
	if( text[ 0 ] != ' ' )
		return page( string->getWidth() );
	Int width = 0, height = 0;
	string->getSize( &width, &height );
	return page( width );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_text( litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr font,
																		 litehtml::web_color color, const litehtml::position &place )
{
	if( font == 0 || color.alpha == 0 )
		return;

	displayString( (GameFont *)font, text )->draw( screen( place.x ), screen( place.y ), tint( color ),
																								 tint( 0, 0, 0, color.alpha ) );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_image_size( const char *source, const char *baseUrl, litehtml::size &size )
{
	if( isSvg( source ) )
	{
		const NSVGimage *picture = svgImage( source );
		size.width = picture ? picture->width : 0;
		size.height = picture ? picture->height : 0;
		return;
	}

	const Image *image = TheMappedImageCollection->findImageByName( AsciiString( source ) );
	size.width = image ? (litehtml::pixel_t)image->getImageWidth() : 0;
	size.height = image ? (litehtml::pixel_t)image->getImageHeight() : 0;
}

//-------------------------------------------------------------------------------------------------
/** An image source is the name of one of the game's mapped images, stretched over the box, or a .svg
	* file beside the pages, fitted into the box whole and centred. */
void HtmlOverlayContainer::draw_image( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																			const std::string &url, const std::string &baseUrl )
{
	if( layer.is_root )
		return;

	const litehtml::position &box = layer.origin_box;
	const Int left = screen( box.x );
	const Int top = screen( box.y );
	const Int right = screen( box.x + box.width );
	const Int bottom = screen( box.y + box.height );
	if( isSvg( url ) )
	{
		const SvgRuns &runs = svgRuns( url, right - left, bottom - top );
		for( SvgRuns::const_iterator run = runs.begin(); run != runs.end(); ++run )
		{
			UnsignedByte red, green, blue, alpha;
			GameGetColorComponents( run->color, &red, &green, &blue, &alpha );
			TheDisplay->drawFillRect( left + run->x, top + run->y, run->length, 1, tint( red, green, blue, alpha ) );
		}
		return;
	}

	const Image *image = TheMappedImageCollection->findImageByName( AsciiString( url.c_str() ) );
	if( image )
		TheDisplay->drawImage( image, left, top, right, bottom, tint( OPAQUE_ALPHA, OPAQUE_ALPHA, OPAQUE_ALPHA, OPAQUE_ALPHA ) );
}

//-------------------------------------------------------------------------------------------------
/** A .svg beside the pages, parsed once. */
NSVGimage *HtmlOverlayContainer::svgImage( const std::string &source )
{
	std::map< std::string, NSVGimage * >::iterator found = m_svgImages.find( source );
	if( found != m_svgImages.end() )
		return found->second;

	NSVGimage *picture = NULL;
	const std::string path = std::string( PAGE_FOLDER ) + source;
	File *file = TheFileSystem->openFile( path.c_str(), File::READ | File::BINARY );
	if( file == NULL )
		DEBUG_LOG(( "HtmlOverlay: the picture %s is missing\n", path.c_str() ));
	else
	{
		const Int size = file->size();
		char *contents = file->readEntireAndClose();
		std::vector< char > text( contents, contents + size );
		delete [] contents;
		text.push_back( '\0' );
		picture = nsvgParse( &text[ 0 ], SVG_UNITS, (float)BROWSER_DPI );
	}
	m_svgImages[ source ] = picture;
	return picture;
}

//-------------------------------------------------------------------------------------------------
/** A .svg rasterised at `width` by `height` screen pixels, kept as rows of one colour so the game's
	* rectangle fill can draw it; once per size, since the picture itself never changes. */
const HtmlOverlayContainer::SvgRuns &HtmlOverlayContainer::svgRuns( const std::string &source, Int width, Int height )
{
	const std::string key = source + "@" + std::to_string( width ) + "x" + std::to_string( height );
	std::map< std::string, SvgRuns >::iterator found = m_svgRuns.find( key );
	if( found != m_svgRuns.end() )
		return found->second;

	SvgRuns &runs = m_svgRuns[ key ];
	NSVGimage *picture = svgImage( source );
	if( picture == NULL || width <= 0 || height <= 0 || picture->width <= 0 || picture->height <= 0 )
		return runs;

	const float scale = std::min( width / picture->width, height / picture->height );
	const float offsetX = ( width - picture->width * scale ) / 2;
	const float offsetY = ( height - picture->height * scale ) / 2;
	std::vector< unsigned char > pixels( width * height * RGBA_BYTES );
	nsvgRasterize( m_rasterizer, picture, offsetX, offsetY, scale, &pixels[ 0 ], width, height, width * RGBA_BYTES );

	for( Int y = 0; y < height; y++ )
	{
		const unsigned char *row = &pixels[ y * width * RGBA_BYTES ];
		Int x = 0;
		while( x < width )
		{
			const unsigned char *pixel = row + x * RGBA_BYTES;
			Int length = 1;
			while( x + length < width && memcmp( pixel, row + ( x + length ) * RGBA_BYTES, RGBA_BYTES ) == 0 )
				length++;
			if( pixel[ 3 ] > 0 )
			{
				SvgRun run;
				run.x = x;
				run.y = y;
				run.length = length;
				run.color = GameMakeColor( pixel[ 0 ], pixel[ 1 ], pixel[ 2 ], pixel[ 3 ] );
				runs.push_back( run );
			}
			x += length;
		}
	}
	return runs;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::fillBox( const litehtml::position &box, const litehtml::web_color &color )
{
	const Int left = screen( box.x );
	const Int top = screen( box.y );
	const Int right = screen( box.x + box.width );
	const Int bottom = screen( box.y + box.height );
	if( color.alpha > 0 && right > left && bottom > top )
		TheDisplay->drawFillRect( left, top, right - left, bottom - top, tint( color ) );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_solid_fill( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																					 const litehtml::web_color &color )
{
	if( layer.is_root )
		return;
	fillBox( layer.border_box, color );
}

//-------------------------------------------------------------------------------------------------
/** One row or column of pixels at a time across the box, along whichever axis the gradient runs
	* further on, each filled with the colour its stops blend to there.  A line as thick as the box,
	* blended between its ends, was the first way: the line takes whole-pixel centres, so a box an odd
	* number of pixels wide came out a pixel short on one side and let what was under it show. */
void HtmlOverlayContainer::draw_linear_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																								const litehtml::background_layer::linear_gradient &gradient )
{
	if( layer.is_root || gradient.color_points.empty() )
		return;

	std::vector< litehtml::background_layer::color_point > stops = gradient.color_points;
	if( stops.front().offset > 0 )
		stops.insert( stops.begin(), litehtml::background_layer::color_point( 0, stops.front().color ) );
	if( stops.back().offset < 1 )
		stops.push_back( litehtml::background_layer::color_point( 1, stops.back().color ) );

	const litehtml::position &box = layer.border_box;
	const Real runX = gradient.end.x - gradient.start.x;
	const Real runY = gradient.end.y - gradient.start.y;
	const Bool across = fabsf( runX ) >= fabsf( runY );
	const Int left = screen( box.x );
	const Int top = screen( box.y );
	const Int right = screen( box.x + box.width );
	const Int bottom = screen( box.y + box.height );
	const Int first = across ? left : top;
	const Int last = across ? right : bottom;
	const Real start = across ? gradient.start.x : gradient.start.y;
	const Real run = across ? runX : runY;
	if( right <= left || bottom <= top || run == 0 )
		return;

	size_t stop = 0;
	for( Int pixel = first; pixel < last; pixel++ )
	{
		const Real offset = ( page( pixel ) + page( 1 ) / 2 - start ) / run;
		while( stop + 2 < stops.size() && offset > stops[ stop + 1 ].offset )
			stop++;
		const litehtml::web_color &from = stops[ stop ].color;
		const litehtml::web_color &to = stops[ stop + 1 ].color;
		const Real span = stops[ stop + 1 ].offset - stops[ stop ].offset;
		const Real share = span > 0 ? std::min( 1.0f, std::max( 0.0f, ( offset - stops[ stop ].offset ) / span ) ) : 0.0f;
		const Color color = tint( REAL_TO_INT( from.red + ( to.red - from.red ) * share ),
															REAL_TO_INT( from.green + ( to.green - from.green ) * share ),
															REAL_TO_INT( from.blue + ( to.blue - from.blue ) * share ),
															REAL_TO_INT( from.alpha + ( to.alpha - from.alpha ) * share ) );
		if( across )
			TheDisplay->drawFillRect( pixel, top, 1, bottom - top, color );
		else
			TheDisplay->drawFillRect( left, pixel, right - left, 1, color );
	}
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_radial_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																								const litehtml::background_layer::radial_gradient &gradient )
{
	if( !layer.is_root && !gradient.color_points.empty() )
		fillBox( layer.border_box, gradient.color_points.front().color );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_conic_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																							 const litehtml::background_layer::conic_gradient &gradient )
{
	if( !layer.is_root && !gradient.color_points.empty() )
		fillBox( layer.border_box, gradient.color_points.front().color );
}

//-------------------------------------------------------------------------------------------------
/** A border's width as drawn: none for a side styled away. */
static litehtml::pixel_t drawnWidth( const litehtml::border &border )
{
	const Bool styledAway = border.style == litehtml::border_style_none || border.style == litehtml::border_style_hidden;
	return styledAway || border.width <= 0 ? 0 : border.width;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_borders( litehtml::uint_ptr hdc, const litehtml::borders &borders,
																				const litehtml::position &place, bool root )
{
	if( root )
		return;

	//
	// A side is as many whole screen pixels thick as its width comes to, one at least, measured in
	// from the box's own edge, and never short of where its inner edge rounds to by itself.  With
	// both edges rounded apart and nothing else, at a scale that is not a whole number a one pixel
	// border was one screen pixel on one side of a cell and two on the other.  Cut to its width and
	// nothing else, it stopped a pixel short of what stands inside it: a cell's ring left the command
	// button's own coloured outline showing down one side of a whole column at HUD Size 115%.
	//
	const Int left = screen( place.x );
	const Int top = screen( place.y );
	const Int right = screen( place.x + place.width );
	const Int bottom = screen( place.y + place.height );
	const litehtml::border *sides[] = { &borders.top, &borders.bottom, &borders.left, &borders.right };
	const Int inner[] =
	{
		screen( place.y + borders.top.width ) - top,
		bottom - screen( place.y + place.height - borders.bottom.width ),
		screen( place.x + borders.left.width ) - left,
		right - screen( place.x + place.width - borders.right.width )
	};
	Int thick[ ARRAY_SIZE( sides ) ];
	for( size_t side = 0; side < ARRAY_SIZE( sides ); side++ )
		thick[ side ] = drawnWidth( *sides[ side ] ) > 0 ? std::max( inner[ side ], std::max( 1, screen( sides[ side ]->width ) ) ) : 0;

	// the top and bottom own the corners and the sides stand between them: drawn full height, a
	// groove's dark left edge ran on down past its lit bottom and stuck out of the bevel in black
	const Int sideTop = top + thick[ 0 ];
	const Int sideHeight = bottom - top - thick[ 0 ] - thick[ 1 ];
	const IRegion2D edges[] =
	{
		{ { left, top }, { right, top + thick[ 0 ] } },
		{ { left, bottom - thick[ 1 ] }, { right, bottom } },
		{ { left, sideTop }, { left + thick[ 2 ], sideTop + sideHeight } },
		{ { right - thick[ 3 ], sideTop }, { right, sideTop + sideHeight } }
	};
	for( size_t side = 0; side < ARRAY_SIZE( sides ); side++ )
	{
		const IRegion2D &edge = edges[ side ];
		if( sides[ side ]->color.alpha > 0 && edge.hi.x > edge.lo.x && edge.hi.y > edge.lo.y )
			TheDisplay->drawFillRect( edge.lo.x, edge.lo.y, edge.hi.x - edge.lo.x, edge.hi.y - edge.lo.y,
																tint( sides[ side ]->color ) );
	}
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::transform_text( litehtml::string &text, litehtml::text_transform transform )
{
	Bool wordStart = TRUE;
	for( size_t index = 0; index < text.size(); index++ )
	{
		const unsigned char letter = (unsigned char)text[ index ];
		if( transform == litehtml::text_transform_uppercase || ( transform == litehtml::text_transform_capitalize && wordStart ) )
			text[ index ] = (char)toupper( letter );
		else if( transform == litehtml::text_transform_lowercase )
			text[ index ] = (char)tolower( letter );
		wordStart = isspace( letter ) != 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** <link rel="stylesheet" href="x.css"> reads x.css from beside the pages. */
void HtmlOverlayContainer::import_css( litehtml::string &text, const litehtml::string &url, litehtml::string &baseUrl )
{
	const std::string path = std::string( PAGE_FOLDER ) + url;
	File *file = TheFileSystem->openFile( path.c_str(), File::READ | File::BINARY );
	if( file == NULL )
	{
		DEBUG_LOG(( "HtmlOverlay: the stylesheet %s is missing\n", path.c_str() ));
		return;
	}
	const Int size = file->size();
	char *contents = file->readEntireAndClose();
	text.assign( contents, size );
	delete [] contents;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_viewport( litehtml::position &viewport ) const
{
	viewport = litehtml::position( 0, 0, viewportWidth(), viewportHeight() );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_media_features( litehtml::media_features &media ) const
{
	media.type = litehtml::media_type_screen;
	media.width = media.device_width = viewportWidth();
	media.height = media.device_height = viewportHeight();
	media.color = COLOR_BITS;
	media.color_index = 0;
	media.monochrome = 0;
	media.resolution = BROWSER_DPI;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_language( litehtml::string &language, litehtml::string &culture ) const
{
	language = "en";
	culture.clear();
}

//-------------------------------------------------------------------------------------------------
HtmlOverlay::HtmlOverlay( const AsciiString &defaultFont ) :
	m_container( new HtmlOverlayContainer( defaultFont ) )
{
}

//-------------------------------------------------------------------------------------------------
HtmlOverlay::~HtmlOverlay( void )
{
	delete m_container;
}

void HtmlOverlay::setPage( const std::string &html )	{ m_container->setPage( html ); }
void HtmlOverlay::draw( void )													{ m_container->draw(); }
void HtmlOverlay::setHud( Bool hud )										{ m_container->m_hud = hud; }
void HtmlOverlay::setScreenPixels( Bool screenPixels )	{ m_container->m_screenPixels = screenPixels; }
void HtmlOverlay::setAlpha( Int alpha )									{ m_container->setAlpha( alpha ); }
Bool HtmlOverlay::hover( const ICoord2D &mouse )				{ return m_container->hover( mouse ); }
std::string HtmlOverlay::click( const ICoord2D &mouse )	{ return m_container->click( mouse ); }
std::string HtmlOverlay::tip( IRegion2D &rect )					{ return m_container->tip( rect ); }
Int HtmlOverlay::bottomOf( const char *selector )				{ return m_container->bottomOf( selector ); }
void HtmlOverlay::rectsOf( const char *selector, std::vector< IRegion2D > &rects )	{ m_container->rectsOf( selector, rects ); }
