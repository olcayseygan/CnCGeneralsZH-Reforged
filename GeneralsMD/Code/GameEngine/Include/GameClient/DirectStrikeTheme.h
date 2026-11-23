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

// FILE: DirectStrikeTheme.h //////////////////////////////////////////////////////////////////////
//
// The one look every Direct Strike screen draws in: the in-match HUD (LaneHud.cpp), the lobby
// (DirectStrikeLobby.cpp) and, once it moves over, the end screen.  It holds the palette, the type, the
// side's art and how a piece is named, and the shapes made of pieces (the 9-slice panel, the 3-slice
// bar, dropdown and key, a chip, a portrait well, a lamp).  A piece is asked for by its Piece below and
// never by a string: PIECE_NAMES in DirectStrikeTheme.cpp is the only place a mapped image's name is
// written, so when hud-artist renames the art it is that one table.
//
// A piece is <Prefix><Name><State>: the prefix is the side's (AmericaPro, ChinaPro, GLAPro) or the
// common frame's (CommonPro), the state "" up, H under the pointer, P lit or pressed, I unavailable.  A
// piece the data lacks is NULL and the caller draws its flat stand-in, so missing art never stops a screen.
//
// Every draw first calls begin() with the set it wears and the screen's scale: that picks the prefix and
// starts the strings over, so a screen owns the theme for its draw.  The match HUD wears the local side's
// set; the screens every player of a match shares (the lobby, the end screen) wear the common frame,
// SIDE_COMMON, the owner's Forged Alloy of 2026-11-23.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DIRECTSTRIKETHEME_H_
#define __DIRECTSTRIKETHEME_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameClient/LaneHudLayout.h"

class DisplayString;
class GameFont;
class Image;
class PlayerTemplate;

namespace DirectStrikeTheme
{
	// The contract is docs/reference/DIRECT-STRIKE-THEME.md in the wrapper, the art's own source being
	// Tools/hud_direct_strike_theme.py: its INK, its type roles and its letter spacing are these.

	// INK
	static const Color TEXT_WHITE			= GameMakeColor( 236, 238, 242, 255 );	///< figures, names
	static const Color TEXT_GREY			= GameMakeColor( 176, 182, 192, 255 );	///< secondary text, team heads
	static const Color TEXT_DIM				= GameMakeColor( 120, 126, 136, 255 );	///< hints, a closed seat, system lines
	static const Color TEXT_GOLD			= GameMakeColor( 242, 194, 48, 255 );		///< prices, hover, the you rim
	static const Color TEXT_GREEN			= GameMakeColor( 60, 186, 72, 255 );		///< ready, income
	static const Color TEXT_DARK			= GameMakeColor( 14, 16, 22, 255 );			///< on_lit: on a lit key or an open dropdown
	static const Color TEAM_US				= GameMakeColor( 66, 112, 236, 255 );
	static const Color TEAM_THEM			= GameMakeColor( 214, 44, 36, 255 );
	static const Color KEYLINE				= GameMakeColor( 12, 14, 18, 255 );			///< the dark rim round marks, lamps and swatches
	static const Color TEXT_SHADOW		= GameMakeColor( 0, 0, 0, 210 );				///< one pixel down and right, none on a lit face

	// type roles, Arial Bold at pixels in 1080
	static const Real TYPE_TICK				= 11.0f;
	static const Real TYPE_SMALL			= 13.0f;
	static const Real TYPE_PRICE			= 14.0f;
	static const Real TYPE_BODY				= 16.0f;	///< chat, picker names, hints
	static const Real TYPE_LABEL			= 18.0f;	///< dropdowns, team heads
	static const Real TYPE_NAME				= 22.0f;	///< seat names
	static const Real TYPE_FIGURE			= 28.0f;	///< money, START
	static const Real TYPE_TITLE			= 34.0f;
	static const Real TYPE_RESULT			= 40.0f;

	// spaced capitals: letter spacing as a share of the size
	static const Real TRACK_TITLE			= 0.30f;
	static const Real TRACK_RESULT		= 0.32f;
	static const Real TRACK_KEY				= 0.21f;
	static const Real TRACK_TEAM			= 0.20f;

	// the flat stand-ins, drawn where a piece is missing, and what no piece draws
	static const Color PLATE					= GameMakeColor( 22, 27, 33, 228 );
	static const Color PLATE_EDGE			= GameMakeColor( 92, 106, 120, 255 );
	static const Color PLATE_LIT			= GameMakeColor( 140, 152, 166, 255 );
	static const Color WELL						= GameMakeColor( 8, 11, 14, 235 );
	static const Color KEY_FACE				= GameMakeColor( 36, 44, 54, 240 );
	static const Color KEY_FACE_HOT		= GameMakeColor( 52, 64, 78, 245 );
	static const Color KEY_FACE_LIT		= GameMakeColor( 40, 96, 74, 250 );
	static const Color GREYED					= GameMakeColor( 0, 0, 0, 150 );
	static const Color CARD_FACE			= GameMakeColor( 30, 36, 44, 245 );
	static const Color WHITE					= GameMakeColor( 240, 242, 246, 255 );
	static const Color LEGAL					= GameMakeColor( 80, 230, 110, 255 );
	static const Color PRICE					= GameMakeColor( 255, 214, 90, 255 );
	static const Color VEIL						= GameMakeColor( 4, 6, 10, 170 );		///< the shell map dimmed under a screen

	/// every piece any Direct Strike screen draws; PIECE_NAMES holds their names in this order
	enum Piece
	{
		// the HUD, hud-artist's README-v4 and v6
		LANE_CARD,
		LANE_CARD_HERO,
		LANE_CARD_BACK,
		LANE_TIER1,
		LANE_TIER2,
		LANE_TIER3,
		LANE_TAB_LOCK,
		LANE_DONE,
		LANE_AIR_MARK,
		LANE_INCOME_ICON,
		LANE_COIN,
		LANE_MONEY_CHIP,
		LANE_LANE_CHIP,
		LANE_YOUR_CHIP,
		LANE_YOUR_RING,
		LANE_BAR_CHIP,
		LANE_COUNT_CHIP,
		LANE_MAP_TOWER,
		LANE_MAP_FORTRESS,
		LANE_ENEMY_PLOT_FRAME,
		LANE_ABILITIES_KEY,
		LANE_ABILITIES_PANEL,
		LANE_ABILITIES_PANEL_HEAD,
		LANE_ABILITIES_PANEL_ROW,
		LANE_ABILITIES_PANEL_FOOT,
		LANE_TOGGLE_ON,
		LANE_TOGGLE_OFF,
		SIDEBAR_KEY_MENU,
		SIDEBAR_KEY_DIPLOMACY,

		// the shapes every screen builds from: four 9-slices, each TL, T, TR, L, C, R, BL, B, BR in this order
		PANEL_TL, PANEL_T, PANEL_TR, PANEL_L, PANEL_C, PANEL_R, PANEL_BL, PANEL_B, PANEL_BR,
		PANEL_LIT_TL, PANEL_LIT_T, PANEL_LIT_TR, PANEL_LIT_L, PANEL_LIT_C, PANEL_LIT_R, PANEL_LIT_BL, PANEL_LIT_B, PANEL_LIT_BR,
		PLATE_TL, PLATE_T, PLATE_TR, PLATE_L, PLATE_C, PLATE_R, PLATE_BL, PLATE_B, PLATE_BR,
		WELL_TL, WELL_T, WELL_TR, WELL_L, WELL_C, WELL_R, WELL_BL, WELL_B, WELL_BR,
		DROP_L,
		DROP_C,
		DROP_R,
		FIELD_R,					///< a dropdown's right end without its caret
		BUTTON_L,					///< the wide text key's 3-slice
		BUTTON_C,
		BUTTON_R,
		LAMP_READY,
		LAMP_WAITING,
		LAMP_HOST,
		COIN_AMERICA,
		COIN_CHINA,
		COIN_GLA,
		COIN_RANDOM,
		ROW_L,						///< an open list's row as a 3-slice, only in its H (under the pointer) and P (current) states
		ROW_C,
		ROW_R,

		// the lobby's own, README-lobby.md
		LOBBY_PICK_TILE,
		LOBBY_ISLAND,
		LOBBY_ISLAND_BAND,			///< the island on the map band
		LOBBY_HEADQUARTERS_AMERICA,	///< a plain side's portrait: its command centre's build button picture
		LOBBY_HEADQUARTERS_CHINA,
		LOBBY_HEADQUARTERS_GLA,
		LOBBY_RANDOM,
		LOBBY_OPEN,
		LOBBY_CLOSED,
		LOBBY_PING1,
		LOBBY_PING2,
		LOBBY_PING3,
		LOBBY_PING4,
		LOBBY_AI,
		LOBBY_CHECK,

		// the Spearhead lobby on the common sheet: a pointed piece is drawn in its own light, so a team's piece and
		// its mirror are two (Right points right, team 1's; Left points left, team 2's)
		CONSOLE_TL, CONSOLE_T, CONSOLE_TR, CONSOLE_L, CONSOLE_C, CONSOLE_R, CONSOLE_BL, CONSOLE_B, CONSOLE_BR,
		SPEAR_RIGHT_L, SPEAR_RIGHT_C, SPEAR_RIGHT_R,				///< a seat with a player, 148 high
		SPEAR_LEFT_L, SPEAR_LEFT_C, SPEAR_LEFT_R,
		SPEAR_YOU_RIGHT_L, SPEAR_YOU_RIGHT_C, SPEAR_YOU_RIGHT_R,	///< the local player's seat, its lip bright
		SPEAR_YOU_LEFT_L, SPEAR_YOU_LEFT_C, SPEAR_YOU_LEFT_R,
		BAY_RIGHT_L, BAY_RIGHT_C, BAY_RIGHT_R,							///< an open or closed seat
		BAY_LEFT_L, BAY_LEFT_C, BAY_LEFT_R,
		SPEAR_HEAD_RIGHT_L, SPEAR_HEAD_RIGHT_C, SPEAR_HEAD_RIGHT_R,	///< a team head, 44 high
		SPEAR_HEAD_LEFT_L, SPEAR_HEAD_LEFT_C, SPEAR_HEAD_LEFT_R,
		HEX_TITLE_L, HEX_TITLE_C, HEX_TITLE_R,
		TUG_L, TUG_C, TUG_R,
		SEG,						///< a seat lip's segment, unlit
		SEG_LIT,				///< lit: white, drawn tinted at its core grown by GLOW
		TUG_SEG,
		TUG_SEG_LIT,
		LED,						///< a team head's readiness light
		LED_LIT,
		CHEVRON_R,			///< white, tinted, pointing right
		CHEVRON_L,

		// the Riveted Clamp controls of the shared screens
		CLAMP_START_L, CLAMP_START_C, CLAMP_START_R,
		CLAMP_BACK,
		CLAMP_PLUS,			///< an open seat's key: sit here
		CLAMP_SEND,
		CLAMP_CHIP_L, CLAMP_CHIP_C, CLAMP_CHIP_R,
		CLAMP_CHIP_FIELD_R,	///< a chip's right end without its caret block
		CLAMP_BOX_TL, CLAMP_BOX_T, CLAMP_BOX_TR, CLAMP_BOX_L, CLAMP_BOX_C, CLAMP_BOX_R, CLAMP_BOX_BL, CLAMP_BOX_B, CLAMP_BOX_BR,
		CLAMP_BAND_L, CLAMP_BAND_C, CLAMP_BAND_R,

		PIECE_COUNT
	};

	/// a lit piece's halo past its core on every side, at 1080: it is drawn at its core grown by this
	static const Real GLOW = 4.0f;

	/// the set a screen wears: a base side's (0 America, Turkiye and a watcher too, 1 China, 2 GLA), or the common
	/// frame every player of a match shares, which is no side and so stands after the count
	enum Side { SIDE_AMERICA, SIDE_CHINA, SIDE_GLA, SIDE_COUNT, SIDE_COMMON = SIDE_COUNT };
	Side sideOf( const AsciiString &side );
	const char *prefixOf( Side side );

	/// starts a screen's draw: the side whose art it wears, its scale (1 at 1080), the strings anew
	void begin( Side side, Real scale );
	Real scale( void );
	/// a length at 1080 on this screen, one pixel at least
	Int px( Real at1080 );

	/// the piece in the side begin() chose, in that state if the data has it, else plain; NULL while missing
	const Image *art( Piece piece, const char *state = "" );
	/// a piece of another side's set, for a coin or badge that names its own side
	const Image *artOf( Side side, Piece piece, const char *state = "" );
	/// forget what was looked up, after a piece was added to the image collection
	void forgetPieces( void );
	/// the image collection's name for a piece in the side begin() chose, for a piece made at run time
	AsciiString pieceName( Piece piece );
	const char *stateOf( Bool hot, Bool lit, Bool disabled );

	// type: the HUD's in UIStyle's layout points, the lobby's Arial Bold at an em in 1080 pixels
	GameFont *hudFont( Int points );
	GameFont *font( Real em1080 );

	/// text in a band: x is its left (align -1), middle (0) or right end (+1), centred down y..y+h; its width
	Int drawText( const UnicodeString &text, GameFont *font, Int x, Int y, Int h, Color color, Int align = -1, Bool shadow = TRUE );
	/// the same with `spacing` 1080 pixels between letters, README-lobby.md's titles
	Int drawSpaced( const UnicodeString &text, GameFont *font, Int x, Int y, Int h, Real spacing, Color color, Int align = -1, Bool shadow = TRUE );
	Int textWidth( const UnicodeString &text, GameFont *font );
	/// a string of this draw's pool set to text in font, to measure or draw by hand; NULL once the pool is spent
	DisplayString *pooled( const UnicodeString &text, GameFont *font );

	// shapes
	void drawImage( const Image *image, const LaneHudBox &b, Color tint = 0xFFFFFFFF );
	/// an image at its own size times the scale, centred in b
	void drawCentred( const Image *image, const LaneHudBox &b, Color tint = 0xFFFFFFFF );
	/// a flat plate with a lit top line
	void plate( const LaneHudBox &b );
	/// a key in its piece or flat: lit when its mode is on, brighter under the pointer
	void key( const LaneHudBox &b, Piece piece, Bool lit, Bool disabled = FALSE );
	/// a chip of the HUD's floating top: its piece, or a card's flat face with its glass 5 in
	void chip( const LaneHudBox &b, Piece piece );
	/// a 9-slice from its top left piece (PANEL_TL, PANEL_LIT_TL, PLATE_TL, WELL_TL, CONSOLE_TL, CLAMP_BOX_TL),
	/// corners at their own size times the scale; a flat well where the pieces are missing.  The middles stretch,
	/// unless the centre is a textured tile (wider than TILE_MIN): then the edges and the centre repeat from the
	/// top left at their own size, the last copy cut, so the scratches are never smeared
	void nineSlice( const LaneHudBox &b, Piece topLeft, Color tint = 0xFFFFFFFF );
	/// the plain panel, nineSlice from PANEL_TL
	void panel( const LaneHudBox &b );
	/// a 3-slice bar: left and right at their own widths times the scale, the centre stretched between, or repeated
	/// from the left when it is a textured tile as above
	void threeSlice( const LaneHudBox &b, Piece left, Piece centre, Piece right, const char *state = "", Color tint = 0xFFFFFFFF );
	/// a dropdown ("" up, H hot, P open), or a field without its caret
	void dropdown( const LaneHudBox &b, const char *state, Bool field );
	/// the keys row's chip (team size, game speed), the Riveted Clamp's dropdown at 72: "" up, H hot, P open, or a
	/// field without its caret block
	void clampChip( const LaneHudBox &b, const char *state, Bool field );
	/// a lit piece (white, its halo in the alpha) at its core grown by GLOW on every side, in the colour
	void glow( Piece piece, const LaneHudBox &core, Color tint );
	/// a picture filling a square well from its centre top, the colour band 6 high at its foot
	void portraitWell( const LaneHudBox &well, const Image *picture, Color band, Bool fill );
	/// a swatch: the colour in a 2 pixel black frame
	void swatch( const LaneHudBox &b, Color color );

	/// an open list's row, one rule for every list: the current choice a lit band with a gold bar on its leading
	/// edge, the row under the pointer a plain light band, any other row nothing
	void listRow( const LaneHudBox &b, Bool current, Bool hot );
	/// the text on such a row: white on the current one, gold under the pointer, grey otherwise
	Color listText( Bool current, Bool hot );
	/// the current choice where a list is a grid of cells (the army picker, the colours): a gold frame round it
	void currentFrame( const LaneHudBox &b );

	/// the general's face from the challenge menu, NULL for a plain side
	const Image *generalPortrait( const PlayerTemplate *army );
	/// a plain side's command centre, its build button picture's centre square at a portrait well's 172 on the local
	/// side's sheet
	const Image *headquartersPicture( Side side );
}

#endif // __DIRECTSTRIKETHEME_H_
