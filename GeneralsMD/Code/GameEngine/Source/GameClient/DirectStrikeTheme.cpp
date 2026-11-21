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

// FILE: DirectStrikeTheme.cpp ////////////////////////////////////////////////////////////////////
//
// The Direct Strike look, DirectStrikeTheme.h.  Draws only, at the render's sync point like every
// screen that calls it; nothing here reads or writes the logic.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/DirectStrikeTheme.h"

#include "Common/PlayerTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Image.h"
#include "GameClient/Mouse.h"
#include "GameClient/UIStyle.h"

#include <vector>

namespace DirectStrikeTheme
{

namespace
{
	/// the mapped image names, in Piece's order, without the side's prefix; a rename of the art is an edit here
	const char *const PIECE_NAMES[ PIECE_COUNT ] =
	{
		"LaneCard",
		"LaneCardHero",
		"LaneCardBack",
		"LaneTier1",
		"LaneTier2",
		"LaneTier3",
		"LaneTabLock",
		"LaneDone",
		"LaneAirMark",
		"LaneIncomeIcon",
		"LaneCoin",
		"LaneMoneyChip",
		"LaneLaneChip",
		"LaneYourChip",
		"LaneYourRing",
		"LaneBarChip",
		"LaneCountChip",
		"LaneMapTower",
		"LaneMapFortress",
		"LaneEnemyPlotFrame",
		"LaneAbilitiesKey",
		"LaneAbilitiesPanel",
		"LaneAbilitiesPanelHead",
		"LaneAbilitiesPanelRow",
		"LaneAbilitiesPanelFoot",
		"LaneToggleOn",
		"LaneToggleOff",
		"SidebarKeyMenu",
		"SidebarKeyDiplomacy",

		"DSPanelTL", "DSPanelT", "DSPanelTR", "DSPanelL", "DSPanelC", "DSPanelR", "DSPanelBL", "DSPanelB", "DSPanelBR",
		"DSPanelLitTL", "DSPanelLitT", "DSPanelLitTR", "DSPanelLitL", "DSPanelLitC", "DSPanelLitR", "DSPanelLitBL", "DSPanelLitB", "DSPanelLitBR",
		"DSPlateTL", "DSPlateT", "DSPlateTR", "DSPlateL", "DSPlateC", "DSPlateR", "DSPlateBL", "DSPlateB", "DSPlateBR",
		"DSWellTL", "DSWellT", "DSWellTR", "DSWellL", "DSWellC", "DSWellR", "DSWellBL", "DSWellB", "DSWellBR",
		"DSDropL",
		"DSDropC",
		"DSDropR",
		"DSFieldR",
		"DSButtonL",
		"DSButtonC",
		"DSButtonR",
		"DSLampReady",
		"DSLampWaiting",
		"DSLampHost",
		"DSCoinAmerica",
		"DSCoinChina",
		"DSCoinGla",
		"DSCoinRandom",
		"DSRowL",
		"DSRowC",
		"DSRowR",

		"LobbyBack",
		"LobbySend",
		"LobbyPickTile",
		"LobbyIsland",
		"LobbyIslandSmall",
		"LobbyHeadquartersAmerica",
		"LobbyHeadquartersChina",
		"LobbyHeadquartersGla",
		"LobbyRandom",
		"LobbyOpen",
		"LobbyClosed",
		"LobbyPing1",
		"LobbyPing2",
		"LobbyPing3",
		"LobbyPing4",
		"LobbyAI",
		"LobbyCheck",
	};

	/// the sets' prefixes, the sidebar's: Turkiye and a watcher wear America's
	const char *const PREFIXES[ SIDE_COUNT ] = { "AmericaPro", "ChinaPro", "GLAPro" };

	/// type sizes: an em of Arial at 96 dots an inch is four thirds of its points
	const Real POINTS_PER_PIXEL = 0.75f;
	const char *const FACE = "Arial";

	enum { TEXT_POOL = 384 };
	DisplayString *s_pool[ TEXT_POOL ];
	Int s_poolUsed = 0;

	Side s_side = SIDE_AMERICA;
	Real s_scale = 1.0f;

	struct Found
	{
		AsciiString name;
		const Image *image;
	};
	std::vector<Found> s_found;

	/// a portrait cut square from its top centre, made once a source
	struct Crop
	{
		const Image *source;
		const Image *square;
	};
	std::vector<Crop> s_crops;

	const Image *lookUp( const AsciiString &name )
	{
		for( size_t i = 0; i < s_found.size(); ++i )
			if( s_found[ i ].name == name )
				return s_found[ i ].image;
		Found found;
		found.name = name;
		found.image = TheMappedImageCollection->findImageByName( name );
		s_found.push_back( found );
		return found.image;
	}

	Int scaled( const Image *image, Bool wide )
	{
		return REAL_TO_INT( ( wide ? image->getImageWidth() : image->getImageHeight() ) * s_scale + 0.5f );
	}

	/// the square from the top centre of a portrait, as an image of its own on the same texture
	const Image *squareOf( const Image *source )
	{
		if( source == NULL || BitTest( source->getStatus(), IMAGE_STATUS_ROTATED_90_CLOCKWISE ) )
			return source;
		const Int w = source->getImageWidth(), h = source->getImageHeight();
		if( w <= 0 || h <= 0 || w == h )
			return source;
		for( size_t i = 0; i < s_crops.size(); ++i )
			if( s_crops[ i ].source == source )
				return s_crops[ i ].square;
		Image *square = newInstance( Image );
		AsciiString name;
		name.format( "DirectStrikeSquare%s", source->getName().str() );
		square->setName( name );
		square->setFilename( source->getFilename() );
		square->setTextureWidth( source->getTextureSize()->x );
		square->setTextureHeight( source->getTextureSize()->y );
		square->setStatus( source->getStatus() );
		const Region2D *uv = source->getUV();
		Region2D cut = *uv;
		const Real du = uv->hi.x - uv->lo.x, dv = uv->hi.y - uv->lo.y;
		if( w > h )
		{
			cut.lo.x = uv->lo.x + du * ( w - h ) * 0.5f / w;
			cut.hi.x = cut.lo.x + du * h / w;
		}
		else
			cut.hi.y = uv->lo.y + dv * w / h;
		square->setUV( &cut );
		ICoord2D size;
		size.x = size.y = min( w, h );
		square->setImageSize( &size );
		TheMappedImageCollection->addImage( square );
		Crop crop;
		crop.source = source;
		crop.square = square;
		s_crops.push_back( crop );
		return square;
	}
}

//-------------------------------------------------------------------------------------------------
Side sideOf( const AsciiString &side )
{
	if( side.startsWith( "China" ) )
		return SIDE_CHINA;
	if( side.startsWith( "GLA" ) )
		return SIDE_GLA;
	return SIDE_AMERICA;
}

const char *prefixOf( Side side )
{
	return PREFIXES[ side ];
}

void begin( Side side, Real scale )
{
	s_side = side;
	s_scale = scale;
	s_poolUsed = 0;
}

Real scale( void )
{
	return s_scale;
}

Int px( Real at1080 )
{
	return LaneHud_px( at1080, s_scale );
}

const Image *artOf( Side side, Piece piece, const char *state )
{
	AsciiString name;
	if( *state != 0 )
	{
		name.format( "%s%s%s", PREFIXES[ side ], PIECE_NAMES[ piece ], state );
		const Image *image = lookUp( name );
		if( image != NULL )
			return image;
	}
	name.format( "%s%s", PREFIXES[ side ], PIECE_NAMES[ piece ] );
	return lookUp( name );
}

const Image *art( Piece piece, const char *state )
{
	return artOf( s_side, piece, state );
}

void forgetPieces( void )
{
	s_found.clear();
}

AsciiString pieceName( Piece piece )
{
	AsciiString name;
	name.format( "%s%s", PREFIXES[ s_side ], PIECE_NAMES[ piece ] );
	return name;
}

const char *stateOf( Bool hot, Bool lit, Bool disabled )
{
	return lit ? "P" : disabled ? "I" : hot ? "H" : "";
}

//-------------------------------------------------------------------------------------------------
GameFont *hudFont( Int points )
{
	return UIStyle::readableFont( points, TRUE );
}

GameFont *font( Real em1080 )
{
	const Int points = max( 1, REAL_TO_INT( em1080 * s_scale * POINTS_PER_PIXEL + 0.5f ) );
	return TheFontLibrary->getFont( AsciiString( FACE ), points, TRUE );
}

DisplayString *pooled( const UnicodeString &text, GameFont *font )
{
	if( s_poolUsed >= TEXT_POOL || font == NULL )
		return NULL;
	DisplayString *&s = s_pool[ s_poolUsed++ ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	if( s->getFont() != font )
		s->setFont( font );
	if( s->getText().compare( text ) != 0 )
		s->setText( text );
	return s;
}

Int drawText( const UnicodeString &text, GameFont *font, Int x, Int y, Int h, Color color, Int align, Bool shadow )
{
	if( text.isEmpty() )
		return 0;
	DisplayString *s = pooled( text, font );
	if( s == NULL )
		return 0;
	Int w = 0, th = 0;
	s->getSize( &w, &th );
	const Int lx = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
	// the string always draws its drop; without a shadow the drop is the text itself, on the text
	if( shadow )
		s->draw( lx, y + ( h - th ) / 2, color, TEXT_SHADOW );
	else
		s->draw( lx, y + ( h - th ) / 2, color, color, 0, 0 );
	return w;
}

Int textWidth( const UnicodeString &text, GameFont *font )
{
	DisplayString *s = text.isEmpty() ? NULL : pooled( text, font );
	if( s == NULL )
		return 0;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	return w;
}

Int drawSpaced( const UnicodeString &text, GameFont *font, Int x, Int y, Int h, Real spacing, Color color, Int align, Bool shadow )
{
	const Int length = text.getLength();
	const Int gap = REAL_TO_INT( spacing * s_scale + 0.5f );
	Int widths[ 64 ];
	Int total = 0;
	for( Int i = 0; i < length && i < (Int)ARRAY_SIZE( widths ); ++i )
	{
		UnicodeString letter;
		letter.concat( text.getCharAt( i ) );
		widths[ i ] = textWidth( letter, font );
		total += widths[ i ] + ( i > 0 ? gap : 0 );
	}
	Int lx = align < 0 ? x : align == 0 ? x - total / 2 : x - total;
	for( Int i = 0; i < length && i < (Int)ARRAY_SIZE( widths ); ++i )
	{
		UnicodeString letter;
		letter.concat( text.getCharAt( i ) );
		drawText( letter, font, lx, y, h, color, -1, shadow );
		lx += widths[ i ] + gap;
	}
	return total;
}

//-------------------------------------------------------------------------------------------------
void drawImage( const Image *image, const LaneHudBox &b, Color tint )
{
	if( image != NULL )
		TheDisplay->drawImage( image, b.x, b.y, b.x + b.w, b.y + b.h, tint );
}

void drawCentred( const Image *image, const LaneHudBox &b, Color tint )
{
	if( image == NULL )
		return;
	const Int w = scaled( image, TRUE ), h = scaled( image, FALSE );
	drawImage( image, LaneHud_box( b.x + ( b.w - w ) / 2, b.y + ( b.h - h ) / 2, w, h ), tint );
}

void plate( const LaneHudBox &b )
{
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, PLATE );
	TheDisplay->drawFillRect( b.x + 1, b.y + 1, b.w - 2, 1, PLATE_LIT );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, PLATE_EDGE );
}

void key( const LaneHudBox &b, Piece piece, Bool lit, Bool disabled )
{
	const Bool hot = !disabled && b.contains( TheMouse->getMouseStatus()->pos.x, TheMouse->getMouseStatus()->pos.y );
	const Image *image = art( piece, stateOf( hot, lit, disabled ) );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, lit ? KEY_FACE_LIT : hot ? KEY_FACE_HOT : KEY_FACE );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, hot || lit ? UIStyle::EDGE_LIT : PLATE_EDGE );
	if( disabled )
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, GREYED );
}

void chip( const LaneHudBox &b, Piece piece )
{
	const Image *image = art( piece );
	if( image != NULL )
	{
		drawImage( image, b );
		return;
	}
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, CARD_FACE );
	TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, PLATE_EDGE );
	const Int in = px( 5 );
	TheDisplay->drawFillRect( b.x + in, b.y + in, b.w - 2 * in, b.h - 2 * in, WELL );
}

void nineSlice( const LaneHudBox &b, Piece topLeft, Color tint )
{
	const Image *tl = art( topLeft );
	if( tl == NULL )
	{
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, WELL );
		TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, PLATE_EDGE );
		return;
	}
	enum { T = 1, TR, L, C, R, BL, B, BR };
	const Int c = min( scaled( tl, TRUE ), min( b.w, b.h ) / 2 );
	const Int x1 = b.x + c, x2 = b.x + b.w - c, y1 = b.y + c, y2 = b.y + b.h - c;
	drawImage( tl, LaneHud_box( b.x, b.y, c, c ), tint );
	drawImage( art( (Piece)( topLeft + T ) ), LaneHud_box( x1, b.y, x2 - x1, c ), tint );
	drawImage( art( (Piece)( topLeft + TR ) ), LaneHud_box( x2, b.y, c, c ), tint );
	drawImage( art( (Piece)( topLeft + L ) ), LaneHud_box( b.x, y1, c, y2 - y1 ), tint );
	drawImage( art( (Piece)( topLeft + C ) ), LaneHud_box( x1, y1, x2 - x1, y2 - y1 ), tint );
	drawImage( art( (Piece)( topLeft + R ) ), LaneHud_box( x2, y1, c, y2 - y1 ), tint );
	drawImage( art( (Piece)( topLeft + BL ) ), LaneHud_box( b.x, y2, c, c ), tint );
	drawImage( art( (Piece)( topLeft + B ) ), LaneHud_box( x1, y2, x2 - x1, c ), tint );
	drawImage( art( (Piece)( topLeft + BR ) ), LaneHud_box( x2, y2, c, c ), tint );
}

void panel( const LaneHudBox &b )
{
	nineSlice( b, PANEL_TL );
}

void threeSlice( const LaneHudBox &b, Piece left, Piece centre, Piece right, const char *state )
{
	const Image *l = art( left, state ), *m = art( centre, state ), *r = art( right, state );
	if( l == NULL || m == NULL || r == NULL )
	{
		TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, *state == 'P' ? KEY_FACE_LIT : *state == 'H' ? KEY_FACE_HOT : KEY_FACE );
		TheDisplay->drawOpenRect( b.x, b.y, b.w, b.h, 1.0f, *state == 'H' || *state == 'P' ? UIStyle::EDGE_LIT : PLATE_EDGE );
		return;
	}
	const Int lw = min( scaled( l, TRUE ), b.w / 2 ), rw = min( scaled( r, TRUE ), b.w - lw );
	drawImage( l, LaneHud_box( b.x, b.y, lw, b.h ) );
	drawImage( m, LaneHud_box( b.x + lw, b.y, b.w - lw - rw, b.h ) );
	drawImage( r, LaneHud_box( b.x + b.w - rw, b.y, rw, b.h ) );
}

void dropdown( const LaneHudBox &b, const char *state, Bool field )
{
	threeSlice( b, DROP_L, DROP_C, field ? FIELD_R : DROP_R, field ? "" : state );
}

void listRow( const LaneHudBox &b, Bool current, Bool hot )
{
	if( current || hot )
		threeSlice( b, ROW_L, ROW_C, ROW_R, current ? "P" : "H" );
}

Color listText( Bool current, Bool hot )
{
	return current ? TEXT_WHITE : hot ? TEXT_GOLD : TEXT_GREY;
}

void currentFrame( const LaneHudBox &b )
{
	const Int t = px( 2 );
	TheDisplay->drawFillRect( b.x - t, b.y - t, b.w + 2 * t, t, TEXT_GOLD );
	TheDisplay->drawFillRect( b.x - t, b.y + b.h, b.w + 2 * t, t, TEXT_GOLD );
	TheDisplay->drawFillRect( b.x - t, b.y, t, b.h, TEXT_GOLD );
	TheDisplay->drawFillRect( b.x + b.w, b.y, t, b.h, TEXT_GOLD );
}

void portraitWell( const LaneHudBox &well, const Image *picture, Color band, Bool fill )
{
	const Int in = px( 4 );
	const LaneHudBox inner = LaneHud_box( well.x + in, well.y + in, well.w - 2 * in, well.h - 2 * in );
	if( fill )
		drawImage( squareOf( picture ), inner );
	else
		drawCentred( picture, inner );
	if( band != 0 )
		TheDisplay->drawFillRect( well.x + in, well.y + well.h - in - px( 6 ), well.w - 2 * in, px( 6 ), band );
}

void swatch( const LaneHudBox &b, Color color )
{
	const Int frame = px( 2 );
	TheDisplay->drawFillRect( b.x, b.y, b.w, b.h, KEYLINE );
	TheDisplay->drawFillRect( b.x + frame, b.y + frame, b.w - 2 * frame, b.h - 2 * frame, color );
}

const Image *generalPortrait( const PlayerTemplate *army )
{
	if( army == NULL || TheChallengeGenerals == NULL )
		return NULL;
	const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName( army->getName() );
	if( general == NULL )
		return NULL;
	return general->getBioPortraitLarge() != NULL ? general->getBioPortraitLarge() : general->getBioPortraitSmall();
}

const Image *headquartersPicture( Side side )
{
	static const Piece HEADQUARTERS[ SIDE_COUNT ] = { LOBBY_HEADQUARTERS_AMERICA, LOBBY_HEADQUARTERS_CHINA, LOBBY_HEADQUARTERS_GLA };
	return art( HEADQUARTERS[ side ] );
}

}	// namespace DirectStrikeTheme
