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

// FILE: LaneWarEndScreen.cpp /////////////////////////////////////////////////////////////////////
//
// The end of a Lane War match, the owner's concept B, "the hand" (experiments/proofs/2026-11-21/
// direct-strike-end/README-end.md in the wrapper).  Drawn on the 1920x1080 board with no window of
// its own.  Its pieces come from the side's ReforgedHud<Side>LaneEnd sheet by the README's names
// (<Prefix>LaneEndCard and the rest); a piece the sheet does not have yet is a flat fill.
//
// Times run on logic frames, which keep coming at LOGICFRAMES_PER_SECOND a wall second after the
// decision, since the match keeps running under the end: no stock timer drops it to the shell.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/LaneWarEndScreen.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/LaneHud.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/UIStyle.h"
#include "GameClient/View.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LaneWarDirector.h"
#include "GameLogic/LaneWarStats.h"
#include "GameLogic/Object.h"
#include "Lib/Clock.h"

#include <vector>

namespace
{

enum { STATS = 7, TEXT_POOL = 200, KEYS = 3 };
enum { KEY_REMATCH, KEY_REPLAY, KEY_MENU };

//=================================================================================================
// The look.  Every colour, font, rect and sheet piece name the end draws with is here and nowhere
// else, so the shared Direct Strike theme takes it over in one step.  Rects are on the 1920x1080
// board; a card's are from its top left corner.
//=================================================================================================
namespace Look
{

struct FontSpec { Bool title; Int points; Bool bold; };
struct Box { Real x0, y0, x1, y1; };

// colours
const Color OUR_BLUE = GameMakeColor( 66, 112, 236, 255 );
const Color THEIR_RED = GameMakeColor( 214, 44, 36, 255 );
const Real THEIR_RED_COLUMN_ALPHA = 170.0f / 255.0f;		///< their middle columns, under our blue
const Color GOLD = GameMakeColor( 242, 194, 48, 255 );
const Color WORD_WHITE = GameMakeColor( 236, 238, 242, 255 );
const Color WORD_GREY = GameMakeColor( 196, 200, 208, 255 );
const Color TIME_GREY = GameMakeColor( 176, 182, 192, 255 );
const Color GREYED = GameMakeColor( 199, 199, 199, 255 );		///< a beaten side's portrait, 0.78 bright
const Color STEEL = GameMakeColor( 30, 36, 44, 255 );				///< the flat card backs
const Color STEEL_LIP = GameMakeColor( 96, 108, 124, 255 );	///< their lip, and a beaten card's
const Color RESULT_FLAT = GameMakeColor( 22, 28, 36, 255 );
const Color RESULT_WELL = GameMakeColor( 8, 10, 14, 255 );
const Color CARD_FLAT = GameMakeColor( 34, 40, 48, 240 );
const Color CARD_WELL = GameMakeColor( 10, 12, 16, 230 );
const Color HEADER_FLAT = GameMakeColor( 16, 22, 30, 240 );
const Color HEADER_LIP = GameMakeColor( 150, 170, 196, 255 );
const Color TIMELINE_FLAT = GameMakeColor( 26, 32, 40, 240 );
const Color TIMELINE_PLOT = GameMakeColor( 8, 14, 22, 235 );
const Color PANEL_LIP = GameMakeColor( 110, 124, 140, 255 );	///< the flat timeline's and keys' lip
const Color KEY_FLAT = GameMakeColor( 24, 30, 38, 240 );
const Color SHADE_RESULT = GameMakeColor( 0, 0, 0, 115 );			///< the world under the result, at 0.55
const Color SHADE_RESULTS = GameMakeColor( 0, 0, 0, 150 );
const Real VIGNETTE_FINALE = 0.7f, VIGNETTE_RESULT = 0.7f, VIGNETTE_RESULTS = 0.6f;
const Int VIGNETTE_BANDS = 32;				///< layers of the gradient
const Int VIGNETTE_DEPTH_SHARE = 4;		///< a quarter of the screen's height in from each edge
const Real BACKS_ALPHA = 0.75f;
const Real KEY_DIM_ALPHA = 0.4f;				///< a key not wired yet
const Real TICK_ALPHA = 0.6f, MARK_LINE_ALPHA = 0.35f;
const Real PLATE_LIP = 2.0f, LOCAL_FRAME_LINE = 1.0f, HEALTH_LINE = 2.0f;

// fonts
const FontSpec WORD_FONTS[ 2 ] = { { TRUE, UIStyle::TEXT_DISPLAY, FALSE }, { TRUE, UIStyle::TEXT_TITLE, FALSE } };
const FontSpec NAME_FONTS[ 2 ] = { { FALSE, UIStyle::TEXT_HEADING, TRUE }, { FALSE, UIStyle::TEXT_BODY, TRUE } };
const FontSpec CLOCK_FONT = { FALSE, UIStyle::TEXT_STRONG, TRUE };
const FontSpec NUMBER_FONT = { FALSE, UIStyle::TEXT_STRONG, TRUE };
const FontSpec LABEL_FONT = { FALSE, UIStyle::TEXT_SMALL, FALSE };
const FontSpec NO_MVP_FONT = { FALSE, UIStyle::TEXT_HEADING, TRUE };
const FontSpec TICK_FONT = { FALSE, UIStyle::TEXT_SMALL, FALSE };
const FontSpec KEY_FONT = { FALSE, UIStyle::TEXT_SMALL, TRUE };
const FontSpec TOOLTIP_FONT = { FALSE, UIStyle::TEXT_BODY, FALSE };
const FontSpec REASON_FONTS[ 2 ] = { { FALSE, UIStyle::TEXT_SMALL, TRUE }, { FALSE, UIStyle::TEXT_SMALL, FALSE } };

// the result card, dealt over two backs
const Real RESULT_CENTRE_X = 960.0f, RESULT_CENTRE_Y = 540.0f, RESULT_W = 340.0f, RESULT_H = 452.0f;
const Real BACK_OFFSET_X = 70.0f, BACK_OFFSET_Y = 12.0f, DEAL_DROP = 80.0f;
const Box RESULT_PORTRAIT = { 10.0f, 10.0f, 330.0f, 300.0f };
const Box RESULT_RULE = { 10.0f, 303.0f, 330.0f, 305.0f };		///< gold under a won portrait, flat card only
const Real RESULT_WORD_X = 170.0f, RESULT_WORD_Y = 362.0f, RESULT_WORD_MAX = 300.0f;	///< alone on its line, centred
const Real RESULT_CLOCK_Y = 413.0f;		///< the match's length on its own line under the word, centred
const Real RESULT_REASON_Y = 437.0f, RESULT_REASON_MAX = 300.0f;	///< a team that left, under the length
const Real CLOCK_GLYPH_SIZE = 16.0f, CLOCK_GLYPH_GAP = 6.0f;

// a player's card on the results screen
const Real CARD_W = 250.0f, CARD_H = 590.0f, LOCAL_FRAME_GAP = 3.0f;
const Real CARDS_OURS[ LANEWAR_SEATS ] = { 110.0f, 375.0f, 640.0f };
const Real CARDS_THEIRS[ LANEWAR_SEATS ] = { 1030.0f, 1295.0f, 1560.0f };
const Real CARD_Y = 128.0f, CARD_Y_LOCAL = 116.0f;
const Box CARD_PORTRAIT = { 10.0f, 10.0f, 240.0f, 232.0f };
const Box CARD_BAND = { 10.0f, 240.0f, 240.0f, 244.0f };		///< the player's colour
const Real CARD_NAME_X = 125.0f, CARD_NAME_Y = 260.0f, CARD_NAME_MAX = 224.0f;
const Box CARD_MVP_WELL = { 64.0f, 278.0f, 186.0f, 376.0f };
const Box CARD_MVP = { 65.0f, 279.0f, 185.0f, 375.0f };
const Box CARD_STATS_WELL = { 10.0f, 388.0f, 240.0f, 580.0f };
const Real STAT_ROW_Y = 402.0f, STAT_ROW_STEP = 26.0f, STAT_ROW_H = 18.0f;
const Real STAT_GLYPH_X = 22.0f, STAT_GLYPH_SIZE = 18.0f, STAT_NUMBER_X = 228.0f;

// the header chip, the middle line and the timeline
const Box HEADER = { 750.0f, 26.0f, 1170.0f, 102.0f };
const Real HEADER_WORD_X = 960.0f, HEADER_WORD_Y = 64.0f, HEADER_WORD_MAX = 380.0f;	///< alone in the chip, centred
const Real HEADER_CLOCK_Y = 120.0f;		///< under the chip, centred in the gap between the teams' cards
const Real HEADER_REASON_Y = 142.0f, HEADER_REASON_MAX = 136.0f;	///< a team that left, under the clock in the same gap
const Real MIDDLE_LINE_X = 960.0f, MIDDLE_LINE_TOP = 140.0f, MIDDLE_LINE_BOTTOM = 712.0f, MIDDLE_DASH = 6.0f;
const Real MIDDLE_LINE_TOP_UNDER_REASON = 158.0f;
const Box TIMELINE = { 110.0f, 744.0f, 1810.0f, 912.0f };
const Real TIMELINE_SLICE_END = 24.0f, TIMELINE_PLOT_INSET = 8.0f;
const Real PLOT_LEFT = 152.0f, PLOT_RIGHT = 1792.0f;
const Real TIMELINE_GLYPH_X = 126.0f, TIMELINE_GLYPH_SIZE = 18.0f, MIDDLE_GLYPH_Y = 781.0f, FORTRESS_GLYPH_Y = 852.0f;
const Real TICK_TOP = 900.0f, TICK_BOTTOM = 904.0f;
const Real MARK_LINE_TOP = 752.0f, MARK_LINE_BOTTOM = 890.0f, MARK_LABEL_Y = 896.0f;	///< the time marks
const Real MIDDLE_Y = 790.0f, MIDDLE_REACH = 34.0f;
const Int MIDDLE_COLUMN_SECONDS = 2;
const Real HEALTH_FULL_Y = 836.0f, HEALTH_ZERO_Y = 886.0f;
const Real TOWER_MARK_RISE = 10.0f, TOWER_MARK_HALF = 8.0f, TOWER_MARK_FLAT_HALF = 4.0f;

// the keys under the timeline
const Real KEY_X[ KEYS ] = { 852.0f, 940.0f, 1028.0f };
const Real KEY_W = 72.0f, KEY_TOP = 940.0f, KEY_BOTTOM = 996.0f, KEY_LABEL_Y = 968.0f, TOOLTIP_Y = 1014.0f;

// sheet pieces, each looked up as <side prefix><name> first
const char *const PREFIX_AMERICA = "AmericaPro", *const PREFIX_CHINA = "ChinaPro", *const PREFIX_GLA = "GLAPro";
const char *const PIECE_RESULT = "LaneEndResult";			///< also the backs, its wells left empty
const char *const PIECE_RESULT_WON = "LaneEndResultWon";
const char *const PIECE_CARD = "LaneEndCard", *const PIECE_CARD_WON = "LaneEndCardWon";
const char *const PIECE_HEADER = "LaneEndHeader";
const char *const PIECE_TIMELINE_L = "LaneEndTimelineL", *const PIECE_TIMELINE_C = "LaneEndTimelineC", *const PIECE_TIMELINE_R = "LaneEndTimelineR";
const char *const PIECE_TIME = "LaneStatTime", *const PIECE_FORTRESS = "LaneStatFortress";
const char *const PIECE_MIDDLE = "LaneStatMiddle", *const PIECE_TOWER_DOWN = "LaneStatDamage";
const char *const STAT_GLYPHS[ STATS ] = { "LaneStatWaves", "LaneStatPlaced", "LaneStatEarned", "LaneStatSpent",
	"LaneStatKills", "LaneStatDamage", "LaneStatMiddle" };
const char *const KEY_PIECES[ KEYS ] = { "LaneEndRematch", "LaneEndReplay", "LaneEndMenu" };
const char *const KEY_HOT_SUFFIX = "H";
const char *const CREST_AMERICA = "SAFactionLogo144_US", *const CREST_CHINA = "SNFactionLogo144_China", *const CREST_GLA = "SUFactionLogo144_GLA";

}	// namespace Look

const char *const STAT_LABELS[ STATS ] = { "GUI:LaneEndWaves", "GUI:LaneEndUnits", "GUI:LaneEndEarned", "GUI:LaneEndSpent",
	"GUI:LaneEndKills", "GUI:LaneEndDamage", "GUI:LaneEndMiddle" };
const char *const KEY_LABELS[ KEYS ] = { "GUI:LaneEndRematch", "GUI:LaneEndReplay", "GUI:LaneMenu" };

// the pacing
const Real FINALE_CAMERA_MS = 1000.0f, FINALE_CAMERA_EASE_MS = 300.0f;
const Real FINALE_ZOOM = 0.85f;	///< of the camera's distance it ends the move at
const Real SLOW_FROM = 1.0f, SLOW_UNTIL = 3.5f, SLOW_SHARE = 0.35f;	///< wall seconds into the finale, and the pace
/** Game seconds from the fortress's death to the result card: the fracture's half second standing, its
	* four second track and the settle of what still moves (W3DFractureDraw.cpp), on the game clock the
	* fall runs on.  The match is decided at LANEWAR_COLLAPSE_SECONDS and keeps ticking under the finale. */
const Real FALL_GAME_SECONDS = 7.5f;
const Real BACKS_AT = 0.0f, CARD_AT = 0.3f, DEAL_SECONDS = 0.3f;
const Real RESULTS_AT = CARD_AT + DEAL_SECONDS + 2.0f;

struct Seat
{
	UnicodeString name;
	Color color;
	Int team;
	Bool local;
	AsciiString prefix;								///< the side's sheet prefix: AmericaPro, ChinaPro, GLAPro
	const Image *portrait;
	const Image *mvp;
	Int stat[ STATS ];
};

// copied at the sync point
Int s_phase = LANEWAR_END_PLAYING;
Real s_seconds = 0.0f;							///< into the finale while it plays, then since the result card
Int s_winner = -1;
Int s_leftTeam = -1;								///< the team that left the game and so lost it, -1 for a match played out
Int s_localTeam = -1;								///< -1 for a watcher and a replay
Int s_ourTeam = 0;									///< the team drawn on the left and in blue
Int s_matchSeconds = 0;
UnsignedInt s_endFrame = 0;
std::vector<Seat> s_seats;
std::vector<LaneWarSample> s_samples;
UnsignedInt s_towerDown[ LANEWAR_TEAMS ][ 2 ];
AsciiString s_localPrefix;
const Image *s_resultPortrait = NULL;

// the client's own
Bool s_cameraMoved = FALSE;
UnsignedInt s_finaleStartMs = 0;
Bool s_slowed = FALSE;
Int s_savedFps = 0;
Bool s_resultsOpen = FALSE;					///< the results screen, by time or a click on the result card
Bool s_barHidden = FALSE;
Bool s_left = FALSE;								///< Back to menu was pressed: the match is on its way out
UnsignedInt s_samplesOf = 0;				///< the end frame s_samples were copied for

DisplayString *s_pool[ TEXT_POOL ] = { NULL };
Int s_poolUsed = 0;

/** The fall in slow motion: the engine's tick rate, wall pacing only, so the logic and its CRC are the
	* same as at any speed.  A skirmish alone: a network game is paced by the network and a replay by its
	* own speed keys. */
void slowMotion( Bool on )
{
	if( on && !s_slowed && TheGameLogic->isInSkirmishGame() && !TheGameLogic->isInReplayGame() )
	{
		s_slowed = TRUE;
		s_savedFps = TheGameEngine->getFramesPerSecondLimit();
		TheGameEngine->setFramesPerSecondLimit( max( 1, REAL_TO_INT( s_savedFps * SLOW_SHARE ) ) );
	}
	else if( !on && s_slowed )
	{
		s_slowed = FALSE;
		TheGameEngine->setFramesPerSecondLimit( s_savedFps );
	}
}

//-------------------------------------------------------------------------------------------------
Int bx( Real x ) { return UIStyle::boardX( x ); }
Int by( Real y ) { return UIStyle::boardY( y ); }
Int px( Real l ) { return UIStyle::px( l ); }

Real clamp01( Real v ) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

Color withAlpha( Color c, Real a )
{
	const UnsignedInt alpha = (UnsignedInt)( clamp01( a ) * ( ( (UnsignedInt)c >> 24 ) & 0xFF ) );
	return (Color)( ( (UnsignedInt)c & 0x00FFFFFF ) | ( alpha << 24 ) );
}

GameFont *fontOf( const Look::FontSpec &spec )
{
	return spec.title ? UIStyle::titleFont( spec.points ) : UIStyle::readableFont( spec.points, spec.bold );
}

AsciiString prefixOf( const Player *player )
{
	const AsciiString side = player != NULL ? player->getSide() : AsciiString( "America" );
	if( side.startsWith( "China" ) )
		return AsciiString( Look::PREFIX_CHINA );
	if( side.startsWith( "GLA" ) )
		return AsciiString( Look::PREFIX_GLA );
	return AsciiString( Look::PREFIX_AMERICA );
}

const Image *piece( const AsciiString &prefix, const char *name )
{
	AsciiString full;
	full.format( "%s%s", prefix.str(), name );
	const Image *image = TheMappedImageCollection->findImageByName( full );
	return image != NULL ? image : TheMappedImageCollection->findImageByName( AsciiString( name ) );
}

/// the general's face from the challenge menu, else the side's crest
const Image *portraitOf( const Player *player )
{
	const PlayerTemplate *side = player->getPlayerTemplate();
	if( side == NULL )
		return NULL;
	const GeneralPersona *general = TheChallengeGenerals ? TheChallengeGenerals->getGeneralByTemplateName( side->getName() ) : NULL;
	if( general != NULL && general->getBioPortraitLarge() != NULL )
		return general->getBioPortraitLarge();
	if( general != NULL && general->getBioPortraitSmall() != NULL )
		return general->getBioPortraitSmall();
	const AsciiString base = side->getBaseSide();
	const char *crest = base.compareNoCase( "China" ) == 0 ? Look::CREST_CHINA
										: base.compareNoCase( "GLA" ) == 0 ? Look::CREST_GLA : Look::CREST_AMERICA;
	return TheMappedImageCollection->findImageByName( AsciiString( crest ) );
}

//-------------------------------------------------------------------------------------------------
/// text at x (align -1 from x, 0 centred on x, +1 ending at x), its middle at y
void drawText( const UnicodeString &text, const Look::FontSpec &spec, Int x, Int y, Color color, Int align = -1 )
{
	if( s_poolUsed >= TEXT_POOL || text.isEmpty() )
		return;
	GameFont *font = fontOf( spec );
	DisplayString *&s = s_pool[ s_poolUsed++ ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	if( s->getFont() != font )
		s->setFont( font );
	if( s->getText().compare( text ) != 0 )
		s->setText( text );
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( align < 0 ? x : align == 0 ? x - w / 2 : x - w, y - h / 2, color, UIStyle::SHADOW );
}

/** The same within maxWidth pixels: the first of the two fonts it fits in, else the second with the
	* text cut short.  A player's name is the machine's for a LAN seat and can be any length. */
void drawFitted( const UnicodeString &text, const Look::FontSpec *specs, Int x, Int y, Color color, Int align, Int maxWidth )
{
	if( s_poolUsed >= TEXT_POOL || text.isEmpty() )
		return;
	DisplayString *&s = s_pool[ s_poolUsed ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	Int w = 0, h = 0;
	for( Int f = 0; f < 2; ++f )
	{
		s->setFont( fontOf( specs[ f ] ) );
		s->setText( text );
		s->getSize( &w, &h );
		if( w <= maxWidth )
			break;
	}
	UnicodeString cut = text;
	while( w > maxWidth && cut.getLength() > 1 )
	{
		cut.removeLastChar();
		UnicodeString shown = cut;
		shown.concat( u".." );
		s->setText( shown );
		s->getSize( &w, &h );
	}
	++s_poolUsed;
	s->draw( align < 0 ? x : align == 0 ? x - w / 2 : x - w, y - h / 2, color, UIStyle::SHADOW );
}

UnicodeString clockText( Int seconds )
{
	UnicodeString text;
	text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

/// the match's length with the clock glyph before it, the pair centred on the board's cx
void drawMatchTime( Real cx, Real cy, Color tint )
{
	static DisplayString *measure = NULL;
	if( measure == NULL )
		measure = TheDisplayStringManager->newDisplayString();
	const UnicodeString text = clockText( s_matchSeconds );
	GameFont *font = fontOf( Look::CLOCK_FONT );
	if( measure->getFont() != font )
		measure->setFont( font );
	measure->setText( text );
	Int textWidth = 0, textHeight = 0;
	measure->getSize( &textWidth, &textHeight );
	const Image *clock = piece( s_localPrefix, Look::PIECE_TIME );
	const Int glyph = px( Look::CLOCK_GLYPH_SIZE ), gap = clock != NULL ? glyph + px( Look::CLOCK_GLYPH_GAP ) : 0;
	const Int left = bx( cx ) - ( gap + textWidth ) / 2, middle = by( cy );
	if( clock != NULL )
		TheDisplay->drawImage( clock, left, middle - glyph / 2, left + glyph, middle + glyph / 2, tint );
	drawText( text, Look::CLOCK_FONT, left + gap, middle, tint );
}

/// a board rect, x0 y0 to x1 y1 at 1080
void fill( Real x0, Real y0, Real x1, Real y1, Color c )
{
	TheDisplay->drawFillRect( bx( x0 ), by( y0 ), bx( x1 ) - bx( x0 ), by( y1 ) - by( y0 ), c );
}

/// a box of the look, moved to a card's corner
void fillAt( const Look::Box &b, Real x0, Real y0, Color c )
{
	fill( x0 + b.x0, y0 + b.y0, x0 + b.x1, y0 + b.y1, c );
}

void imageAt( const Image *image, const Look::Box &b, Real x0, Real y0, Color tint )
{
	TheDisplay->drawImage( image, bx( x0 + b.x0 ), by( y0 + b.y0 ), bx( x0 + b.x1 ), by( y0 + b.y1 ), tint );
}

void frame( Real x0, Real y0, Real x1, Real y1, Real width, Color c )
{
	TheDisplay->drawOpenRect( bx( x0 ), by( y0 ), bx( x1 ) - bx( x0 ), by( y1 ) - by( y0 ), width, c );
}

/// a piece of the sheet stretched over the rect, or the flat stand-in for it
void plate( const Image *image, Real x0, Real y0, Real x1, Real y1, Color flat, Color lip, Real alpha = 1.0f )
{
	if( image != NULL )
	{
		TheDisplay->drawImage( image, bx( x0 ), by( y0 ), bx( x1 ), by( y1 ), withAlpha( 0xFFFFFFFF, alpha ) );
		return;
	}
	fill( x0, y0, x1, y1, withAlpha( flat, alpha ) );
	frame( x0, y0, x1, y1, Look::PLATE_LIP, withAlpha( lip, alpha ) );
}

/// an image inside a well, its aspect kept, against the well's top
void fitTop( const Image *image, const Look::Box &b, Real cardX, Real cardY, Color tint )
{
	if( image == NULL || image->getImageWidth() <= 0 || image->getImageHeight() <= 0 )
		return;
	const Real x0 = cardX + b.x0, y0 = cardY + b.y0;
	const Real w = b.x1 - b.x0, h = b.y1 - b.y0;
	const Real aspect = (Real)image->getImageWidth() / (Real)image->getImageHeight();
	Real dw = w, dh = w / aspect;
	if( dh > h )
	{
		dh = h;
		dw = h * aspect;
	}
	const Real lx = x0 + ( w - dw ) * 0.5f;
	TheDisplay->drawImage( image, bx( lx ), by( y0 ), bx( lx + dw ), by( y0 + dh ), tint );
}

/** The edges darkened to strength at the rim, fading inwards.  Every layer is a frame from the screen's
	* edge in, each one reaching a step deeper, so the layers stack into the gradient with no seam between
	* them: bands laid edge to edge left a pixel of world between each pair, a bright line round the screen. */
void vignette( Real strength )
{
	if( strength <= 0.0f )
		return;
	const Int w = (Int)TheDisplay->getWidth(), h = (Int)TheDisplay->getHeight();
	const Int depth = h / Look::VIGNETTE_DEPTH_SHARE;
	// what one layer lays down so that all of them stacked at the rim come to strength
	const Real layerAlpha = 1.0f - (Real)pow( 1.0 - clamp01( strength ), 1.0 / Look::VIGNETTE_BANDS );
	const Color c = withAlpha( 0xFF000000, layerAlpha );
	for( Int i = 1; i <= Look::VIGNETTE_BANDS; ++i )
	{
		const Int t = depth * i / Look::VIGNETTE_BANDS;
		TheDisplay->drawFillRect( 0, 0, w, t, c );
		TheDisplay->drawFillRect( 0, h - t, w, t, c );
		TheDisplay->drawFillRect( 0, t, t, h - 2 * t, c );
		TheDisplay->drawFillRect( w - t, t, t, h - 2 * t, c );
	}
}

void shade( Color c )
{
	TheDisplay->drawFillRect( 0, 0, (Int)TheDisplay->getWidth(), (Int)TheDisplay->getHeight(), c );
}

UnicodeString resultWord( Color *color )
{
	*color = Look::WORD_WHITE;
	if( s_winner < 0 )
		return TheGameText->fetch( "GUI:LaneEndDraw" );
	if( s_localTeam < 0 )
	{
		UnicodeString word;
		word.format( TheGameText->fetch( "GUI:LaneEndTeamWins" ), s_winner + 1 );
		return word;
	}
	if( s_localTeam == s_winner )
		return TheGameText->fetch( "GUI:Victory" );
	*color = Look::WORD_GREY;
	return TheGameText->fetch( "GUI:Defeat" );
}

Bool won( void ) { return s_winner >= 0 && ( s_localTeam < 0 || s_localTeam == s_winner ); }

/// why the match ended when it was not a fortress: empty for one played out
UnicodeString reasonText( void )
{
	UnicodeString text;
	if( s_leftTeam >= 0 )
		text.format( TheGameText->fetch( "GUI:LaneEndLeft" ), s_leftTeam + 1 );
	return text;
}

//-------------------------------------------------------------------------------------------------
// The result card: two card backs, then the card dealt on them
//-------------------------------------------------------------------------------------------------
void drawResult( Real t )
{
	using namespace Look;
	const Real backs = clamp01( ( t - BACKS_AT ) / DEAL_SECONDS );
	const Image *back = piece( s_localPrefix, PIECE_RESULT );
	for( Int k = -1; k <= 1; k += 2 )
	{
		const Real ox = RESULT_CENTRE_X + BACK_OFFSET_X * k, oy = RESULT_CENTRE_Y + BACK_OFFSET_Y;
		plate( back, ox - RESULT_W * 0.5f, oy - RESULT_H * 0.5f, ox + RESULT_W * 0.5f, oy + RESULT_H * 0.5f, STEEL, STEEL_LIP,
					 BACKS_ALPHA * backs );
	}

	const Real deal = clamp01( ( t - CARD_AT ) / DEAL_SECONDS );
	if( deal <= 0.0f )
		return;
	const Real x0 = RESULT_CENTRE_X - RESULT_W * 0.5f, y0 = RESULT_CENTRE_Y - RESULT_H * 0.5f + ( 1.0f - deal ) * DEAL_DROP;
	const Bool victory = won();
	const Image *art = piece( s_localPrefix, victory ? PIECE_RESULT_WON : PIECE_RESULT );
	plate( art, x0, y0, x0 + RESULT_W, y0 + RESULT_H, RESULT_FLAT, victory ? GOLD : STEEL_LIP, deal );
	if( art == NULL )
		fillAt( RESULT_PORTRAIT, x0, y0, withAlpha( RESULT_WELL, deal ) );
	fitTop( s_resultPortrait, RESULT_PORTRAIT, x0, y0, withAlpha( victory ? 0xFFFFFFFF : GREYED, deal ) );
	if( victory && art == NULL )
		fillAt( RESULT_RULE, x0, y0, withAlpha( GOLD, deal ) );
	Color wordColor;
	const UnicodeString word = resultWord( &wordColor );
	drawFitted( word, WORD_FONTS, bx( x0 + RESULT_WORD_X ), by( y0 + RESULT_WORD_Y ), withAlpha( wordColor, deal ), 0, px( RESULT_WORD_MAX ) );
	drawMatchTime( x0 + RESULT_W * 0.5f, y0 + RESULT_CLOCK_Y, withAlpha( TIME_GREY, deal ) );
	drawFitted( reasonText(), REASON_FONTS, bx( x0 + RESULT_WORD_X ), by( y0 + RESULT_REASON_Y ), withAlpha( TIME_GREY, deal ), 0,
							px( RESULT_REASON_MAX ) );
}

//-------------------------------------------------------------------------------------------------
// The results screen
//-------------------------------------------------------------------------------------------------
void drawCard( const Seat &s, Real x0, Real y0 )
{
	using namespace Look;
	const Bool winners = s.team == s_winner;
	const Real x1 = x0 + CARD_W, y1 = y0 + CARD_H;
	if( s.local )
		frame( x0 - LOCAL_FRAME_GAP, y0 - LOCAL_FRAME_GAP, x1 + LOCAL_FRAME_GAP, y1 + LOCAL_FRAME_GAP, LOCAL_FRAME_LINE, GOLD );
	const Image *art = piece( s.prefix, winners ? PIECE_CARD_WON : PIECE_CARD );
	plate( art, x0, y0, x1, y1, CARD_FLAT, winners ? GOLD : STEEL_LIP );

	// the sheet's card has its wells; the flat stand-in gets dark ones
	const Color well = art != NULL ? 0 : CARD_WELL;
	const Color tint = winners ? 0xFFFFFFFF : GREYED;
	fillAt( CARD_PORTRAIT, x0, y0, well );
	fitTop( s.portrait, CARD_PORTRAIT, x0, y0, tint );
	fillAt( CARD_BAND, x0, y0, s.color );
	drawFitted( s.name, NAME_FONTS, bx( x0 + CARD_NAME_X ), by( y0 + CARD_NAME_Y ), WORD_WHITE, 0, px( CARD_NAME_MAX ) );

	fillAt( CARD_MVP_WELL, x0, y0, well );
	if( s.mvp != NULL )
		imageAt( s.mvp, CARD_MVP, x0, y0, tint );
	else
		drawText( UnicodeString( u"-" ), NO_MVP_FONT, bx( x0 + ( CARD_MVP.x0 + CARD_MVP.x1 ) * 0.5f ),
							by( y0 + ( CARD_MVP.y0 + CARD_MVP.y1 ) * 0.5f ), TIME_GREY, 0 );

	fillAt( CARD_STATS_WELL, x0, y0, well );
	const Color ink = winners ? WORD_WHITE : WORD_GREY;
	for( Int i = 0; i < STATS; ++i )
	{
		const Real ry = y0 + STAT_ROW_Y + STAT_ROW_STEP * i, mid = ry + STAT_ROW_H * 0.5f;
		const Image *glyph = piece( s.prefix, STAT_GLYPHS[ i ] );
		if( glyph != NULL )
			TheDisplay->drawImage( glyph, bx( x0 + STAT_GLYPH_X ), by( ry ), bx( x0 + STAT_GLYPH_X + STAT_GLYPH_SIZE ), by( ry + STAT_GLYPH_SIZE ),
														 TIME_GREY );
		else
			drawText( TheGameText->fetch( STAT_LABELS[ i ] ), LABEL_FONT, bx( x0 + STAT_GLYPH_X ), by( mid ), TIME_GREY );
		const UnicodeString value = i == STATS - 1 ? clockText( s.stat[ i ] ) : UIStyle::grouped( s.stat[ i ] );
		drawText( value, NUMBER_FONT, bx( x0 + STAT_NUMBER_X ), by( mid ), ink, 1 );
	}
}

/// the x of a frame on the timeline's plot
Real timelineX( UnsignedInt frame )
{
	const Real span = s_endFrame > 0 ? (Real)s_endFrame : 1.0f;
	return Look::PLOT_LEFT + ( Look::PLOT_RIGHT - Look::PLOT_LEFT ) * clamp01( (Real)frame / span );
}

void drawGlyph( const Image *glyph, Real x, Real y, Real size, Color tint )
{
	if( glyph != NULL )
		TheDisplay->drawImage( glyph, bx( x ), by( y ), bx( x + size ), by( y + size ), tint );
}

void drawTimeline( void )
{
	using namespace Look;
	// a 3-slice: the ends at their size, the middle stretched between them
	const Image *sliceL = piece( s_localPrefix, PIECE_TIMELINE_L ), *sliceC = piece( s_localPrefix, PIECE_TIMELINE_C );
	const Image *sliceR = piece( s_localPrefix, PIECE_TIMELINE_R );
	if( sliceL != NULL && sliceC != NULL && sliceR != NULL )
	{
		const Real innerL = TIMELINE.x0 + TIMELINE_SLICE_END, innerR = TIMELINE.x1 - TIMELINE_SLICE_END;
		TheDisplay->drawImage( sliceL, bx( TIMELINE.x0 ), by( TIMELINE.y0 ), bx( innerL ), by( TIMELINE.y1 ) );
		TheDisplay->drawImage( sliceC, bx( innerL ), by( TIMELINE.y0 ), bx( innerR ), by( TIMELINE.y1 ) );
		TheDisplay->drawImage( sliceR, bx( innerR ), by( TIMELINE.y0 ), bx( TIMELINE.x1 ), by( TIMELINE.y1 ) );
	}
	else
	{
		plate( NULL, TIMELINE.x0, TIMELINE.y0, TIMELINE.x1, TIMELINE.y1, TIMELINE_FLAT, PANEL_LIP );
		fill( TIMELINE.x0 + TIMELINE_PLOT_INSET, TIMELINE.y0 + TIMELINE_PLOT_INSET, TIMELINE.x1 - TIMELINE_PLOT_INSET,
					TIMELINE.y1 - TIMELINE_PLOT_INSET, TIMELINE_PLOT );
	}

	// the glyphs at the left: the middle by the top band, the fortress by the bottom one
	drawGlyph( piece( s_localPrefix, PIECE_MIDDLE ), TIMELINE_GLYPH_X, MIDDLE_GLYPH_Y, TIMELINE_GLYPH_SIZE, TIME_GREY );
	drawGlyph( piece( s_localPrefix, PIECE_FORTRESS ), TIMELINE_GLYPH_X, FORTRESS_GLYPH_Y, TIMELINE_GLYPH_SIZE, TIME_GREY );

	// the foot: a tick every spawn tick, then a faint line and the time every mark, a minute apart under
	// ten minutes, five under half an hour and ten after that
	const UnsignedInt tickFrames = LANEWAR_TICK_FRAMES, minute = 60 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt markFrames = s_endFrame < 10 * minute ? minute : s_endFrame < 30 * minute ? 5 * minute : 10 * minute;
	for( UnsignedInt f = LANEWAR_FIRST_TICK_FRAMES; f < s_endFrame; f += tickFrames )
		fill( timelineX( f ), TICK_TOP, timelineX( f ) + 1.0f, TICK_BOTTOM, withAlpha( TIME_GREY, TICK_ALPHA ) );
	for( UnsignedInt f = markFrames; f < s_endFrame; f += markFrames )
	{
		fill( timelineX( f ), MARK_LINE_TOP, timelineX( f ) + 1.0f, MARK_LINE_BOTTOM, withAlpha( 0xFFFFFFFF, MARK_LINE_ALPHA ) );
		drawText( clockText( (Int)( f / LOGICFRAMES_PER_SECOND ) ), TICK_FONT, bx( timelineX( f ) ), by( MARK_LABEL_Y ), TIME_GREY, 0 );
	}

	// the middle: our share past the line above the gold line in blue, theirs below in red, in columns
	const UnsignedInt column = MIDDLE_COLUMN_SECONDS * LOGICFRAMES_PER_SECOND;
	const Int sign = s_ourTeam == 0 ? 1 : -1;
	size_t i = 0;
	for( UnsignedInt f0 = 0; f0 < s_endFrame && !s_samples.empty(); f0 += column )
	{
		Int sum = 0, count = 0;
		while( i < s_samples.size() && s_samples[ i ].frame < f0 + column )
		{
			sum += s_samples[ i ].middle * sign;
			++count;
			++i;
		}
		if( count == 0 )
			continue;
		const Real share = (Real)sum / ( 100.0f * count );
		const Real x0 = timelineX( f0 ), x1 = timelineX( f0 + column );
		if( share > 0.0f )
			fill( x0, MIDDLE_Y - MIDDLE_REACH * share, x1, MIDDLE_Y, OUR_BLUE );
		else if( share < 0.0f )
			fill( x0, MIDDLE_Y, x1, MIDDLE_Y - MIDDLE_REACH * share, withAlpha( THEIR_RED, THEIR_RED_COLUMN_ALPHA ) );
	}
	fill( PLOT_LEFT, MIDDLE_Y, PLOT_RIGHT, MIDDLE_Y + 1.0f, GOLD );

	// the fortresses' health, a run drawn once for as long as it holds
	Int most = 1;
	for( size_t k = 0; k < s_samples.size(); ++k )
		for( Int t = 0; t < LANEWAR_TEAMS; ++t )
			most = max( most, s_samples[ k ].fortress[ t ] );
	const Image *mark = piece( s_localPrefix, PIECE_TOWER_DOWN );
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
	{
		const Color c = t == s_ourTeam ? OUR_BLUE : THEIR_RED;
		Real runX = timelineX( 0 ), runY = HEALTH_FULL_Y;
		for( size_t k = 0; k < s_samples.size(); ++k )
		{
			const Real y = HEALTH_ZERO_Y - ( HEALTH_ZERO_Y - HEALTH_FULL_Y ) * s_samples[ k ].fortress[ t ] / most;
			const Real x = timelineX( s_samples[ k ].frame );
			if( y != runY || k + 1 == s_samples.size() )
			{
				TheDisplay->drawLine( bx( runX ), by( runY ), bx( x ), by( runY ), HEALTH_LINE, c );
				TheDisplay->drawLine( bx( x ), by( runY ), bx( x ), by( y ), HEALTH_LINE, c );
				runX = x;
				runY = y;
			}
		}

		// where a tower fell, the damage mark in the team's colour over its line
		for( Int k = 0; k < 2; ++k )
		{
			if( s_towerDown[ t ][ k ] == 0 )
				continue;
			const Real x = timelineX( s_towerDown[ t ][ k ] ), y = HEALTH_FULL_Y - TOWER_MARK_RISE;
			if( mark != NULL )
				drawGlyph( mark, x - TOWER_MARK_HALF, y - TOWER_MARK_HALF, 2.0f * TOWER_MARK_HALF, c );
			else
				fill( x - TOWER_MARK_FLAT_HALF, y - TOWER_MARK_FLAT_HALF, x + TOWER_MARK_FLAT_HALF, y + TOWER_MARK_FLAT_HALF, c );
		}
	}
}

Bool overKey( Int k, const ICoord2D *mouse )
{
	using namespace Look;
	return mouse->x >= bx( KEY_X[ k ] ) && mouse->x < bx( KEY_X[ k ] + KEY_W ) && mouse->y >= by( KEY_TOP ) && mouse->y < by( KEY_BOTTOM );
}

Bool watchingReplay( void ) { return TheRecorder->getMode() == RECORDERMODETYPE_PLAYBACK; }

/// the replay the Replay key plays: the one being watched again, else the match just recorded
AsciiString replayToWatch( void )
{
	if( watchingReplay() )
		return TheRecorder->getCurrentReplayFilename();
	if( TheRecorder->getMode() != RECORDERMODETYPE_RECORD )
		return AsciiString::TheEmptyString;
	AsciiString file = TheRecorder->getLastReplayFileName();
	file.concat( TheRecorder->getReplayExtention() );
	return file;
}

/// a replay has no rematch; every other match does, a skirmish here and a network game in its room
Bool keyLive( Int k )
{
	if( k == KEY_REPLAY )
		return replayToWatch().isNotEmpty();
	if( k == KEY_REMATCH )
		return !watchingReplay() && ( TheGameLogic->isInMultiplayerGame() || ( TheGameLogic->isInSkirmishGame() && TheSkirmishGameInfo != NULL ) );
	return TRUE;
}

/// the match ended where it stands, for what the key does next; the recording closed first so it can be played
void endMatchHere( void )
{
	if( TheRecorder->getMode() == RECORDERMODETYPE_RECORD )
		TheRecorder->stopRecording();
	TheGameLogic->clearGameData( FALSE );
	TheGameEngine->setQuitting( FALSE );
}

void watchReplay( void )
{
	const AsciiString file = replayToWatch();
	endMatchHere();
	if( !TheRecorder->playbackFile( file ) )
		TheShell->showShell();
}

/** The same seats, armies and colours on a fresh island, as the room's Start builds it (the skirmish
	* menu's reallyDoStart); a network game goes back to its room, where the host starts the next one. */
void rematch( void )
{
	if( TheGameLogic->isInMultiplayerGame() )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
		return;
	}
	const Int gameSpeedPercent = TheSkirmishGameInfo->getGameSpeedPercent();
	endMatchHere();
	if( !SetDirectStrikeMap( TheSkirmishGameInfo ) )
	{
		TheShell->showShell();
		return;
	}
	FitDirectStrikeRoom( TheSkirmishGameInfo );
	TheSkirmishGameInfo->setSeed( TheGlobalData->m_fixedSeed >= 0 ? TheGlobalData->m_fixedSeed : (Int)Clock_Milliseconds() );
	TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
	InitRandom( TheSkirmishGameInfo->getSeed() );
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SKIRMISH );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	GameEngine_appendNewGameRates( msg, gameSpeedPercent );
}

void drawKeys( void )
{
	using namespace Look;
	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
	for( Int k = 0; k < KEYS; ++k )
	{
		const Bool hot = keyLive( k ) && overKey( k, mouse );
		AsciiString name( KEY_PIECES[ k ] );
		if( hot )
			name.concat( KEY_HOT_SUFFIX );
		const Image *art = piece( s_localPrefix, name.str() );
		const Real alpha = keyLive( k ) ? 1.0f : KEY_DIM_ALPHA;
		const Real centre = KEY_X[ k ] + KEY_W * 0.5f;
		plate( art, KEY_X[ k ], KEY_TOP, KEY_X[ k ] + KEY_W, KEY_BOTTOM, KEY_FLAT, hot ? GOLD : PANEL_LIP, alpha );
		if( art == NULL )
			drawText( TheGameText->fetch( KEY_LABELS[ k ] ), KEY_FONT, bx( centre ), by( KEY_LABEL_Y ), withAlpha( hot ? GOLD : WORD_WHITE, alpha ), 0 );
		if( overKey( k, mouse ) )
			drawText( TheGameText->fetch( KEY_LABELS[ k ] ), TOOLTIP_FONT, bx( centre ), by( TOOLTIP_Y ), WORD_WHITE, 0 );
	}
}

void drawResults( void )
{
	using namespace Look;
	shade( SHADE_RESULTS );
	vignette( VIGNETTE_RESULTS );

	// the header chip: the word between the chip's left lip and the clock, then the match's length
	plate( piece( s_localPrefix, PIECE_HEADER ), HEADER.x0, HEADER.y0, HEADER.x1, HEADER.y1, HEADER_FLAT, HEADER_LIP );
	Color wordColor;
	const UnicodeString word = resultWord( &wordColor );
	drawFitted( word, WORD_FONTS, bx( HEADER_WORD_X ), by( HEADER_WORD_Y ), wordColor, 0, px( HEADER_WORD_MAX ) );
	drawMatchTime( HEADER_WORD_X, HEADER_CLOCK_Y, TIME_GREY );
	const UnicodeString reason = reasonText();
	drawFitted( reason, REASON_FONTS, bx( HEADER_WORD_X ), by( HEADER_REASON_Y ), TIME_GREY, 0, px( HEADER_REASON_MAX ) );

	// the cards, ours on the left, the local player's raised; a team short of three seats keeps its
	// cards next to the middle line
	Int seated[ LANEWAR_TEAMS ] = { 0, 0 }, placed[ LANEWAR_TEAMS ] = { 0, 0 };
	for( size_t i = 0; i < s_seats.size(); ++i )
		++seated[ s_seats[ i ].team ];
	for( size_t i = 0; i < s_seats.size(); ++i )
	{
		const Seat &s = s_seats[ i ];
		const Int slot = placed[ s.team ]++;
		const Real x = s.team == s_ourTeam ? CARDS_OURS[ LANEWAR_SEATS - seated[ s.team ] + slot ] : CARDS_THEIRS[ slot ];
		drawCard( s, x, s.local ? CARD_Y_LOCAL : CARD_Y );
	}

	// the lane's middle line between the teams, gold dashes
	for( Real y = reason.isEmpty() ? MIDDLE_LINE_TOP : MIDDLE_LINE_TOP_UNDER_REASON; y < MIDDLE_LINE_BOTTOM; y += 2.0f * MIDDLE_DASH )
		fill( MIDDLE_LINE_X - 1.0f, y, MIDDLE_LINE_X + 1.0f, min( y + MIDDLE_DASH, MIDDLE_LINE_BOTTOM ), GOLD );

	drawTimeline();
	drawKeys();
}

}	// namespace

//-------------------------------------------------------------------------------------------------
Bool LaneWarEndScreen_isShowing( void )
{
	return s_phase == LANEWAR_END_OVER;
}

Real LaneWarEndScreen_finale( void )
{
	return s_phase == LANEWAR_END_COLLAPSE ? s_seconds : -1.0f;
}

void LaneWarEndScreen_preDraw( void )
{
	if( TheLaneWarDirector == NULL || TheLaneWarStats == NULL || !TheLaneWarDirector->isActive() || !TheGameLogic->isInGame()
			|| TheGameLogic->isInShellGame() )
	{
		s_phase = LANEWAR_END_PLAYING;
		s_cameraMoved = s_resultsOpen = s_left = s_barHidden = FALSE;
		s_samplesOf = 0;
		slowMotion( FALSE );
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt down = 0;
	Int fallen = 0;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		if( TheLaneWarStats->getFortressDownFrame( t ) != 0 && ( down == 0 || TheLaneWarStats->getFortressDownFrame( t ) < down ) )
		{
			down = TheLaneWarStats->getFortressDownFrame( t );
			fallen = t;
		}
	// a match a team left has no fall to watch: the card at once
	const UnsignedInt cardFrame = down != 0 ? down + (UnsignedInt)( FALL_GAME_SECONDS * LOGICFRAMES_PER_GAME_SECOND ) : 0;

	s_phase = TheLaneWarStats->getEndPhase();
	if( s_phase == LANEWAR_END_OVER && now < cardFrame )
		s_phase = LANEWAR_END_COLLAPSE;	// decided, and the fall still has to finish before the card covers it
	if( s_phase != LANEWAR_END_COLLAPSE )
		slowMotion( FALSE );
	if( s_phase == LANEWAR_END_PLAYING )
	{
		s_cameraMoved = s_resultsOpen = s_left = s_barHidden = FALSE;
		s_samplesOf = 0;
		return;
	}

	Int team, seat;
	const Player *local = ThePlayerList->getLocalPlayer();
	s_localTeam = local != NULL && !TheGameLogic->isInReplayGame() && TheLaneWarDirector->seatOf( local, &team, &seat ) ? team : -1;
	s_ourTeam = s_localTeam >= 0 ? s_localTeam : 0;
	s_localPrefix = prefixOf( local );

	if( s_phase == LANEWAR_END_COLLAPSE )
	{
		// the finale: the camera to the fortress coming down, once, then the fall in slow motion
		const UnsignedInt ms = Clock_Milliseconds();
		if( !s_cameraMoved )
			s_finaleStartMs = ms;
		s_seconds = (Real)( ms - s_finaleStartMs ) / 1000.0f;
		slowMotion( s_seconds >= SLOW_FROM && s_seconds < SLOW_UNTIL );
		const Object *fortress = TheGameLogic->findObjectByID( TheLaneWarDirector->getFortress( fallen ) );
		if( !s_cameraMoved && TheTacticalView != NULL )
		{
			s_cameraMoved = TRUE;
			const Coord3D at = fortress != NULL ? *fortress->getPosition() : *TheLaneWarDirector->getSpawn( fallen );
			// side-on to the lane with our end on the left (a watcher's is team A's), held through the
			// move by orient FALSE, and in a little
			LaneHud_turnToLane( s_ourTeam );
			TheTacticalView->moveCameraTo( &at, (Int)FINALE_CAMERA_MS, 0, FALSE, FINALE_CAMERA_EASE_MS, FINALE_CAMERA_EASE_MS );
			TheTacticalView->zoomCamera( TheTacticalView->getZoom() * FINALE_ZOOM, (Int)FINALE_CAMERA_MS, FINALE_CAMERA_EASE_MS,
																	 FINALE_CAMERA_EASE_MS );
		}
		return;
	}

	// over: what the result and the results screen show, frozen from the decision on
	s_endFrame = TheLaneWarStats->getEndFrame();
	s_seconds = (Real)( now - max( s_endFrame, cardFrame ) ) / LOGICFRAMES_PER_SECOND;
	s_winner = TheLaneWarStats->getWinner();
	s_leftTeam = -1;
	for( Int t = LANEWAR_TEAMS - 1; t >= 0; --t )
		if( TheLaneWarStats->getTeamLeftFrame( t ) != 0 && TheLaneWarStats->isOut( t, s_endFrame ) )
			s_leftTeam = t;
	s_matchSeconds = (Int)( s_endFrame / LOGICFRAMES_PER_SECOND );
	if( s_seconds >= RESULTS_AT )
		s_resultsOpen = TRUE;
	if( !s_barHidden )
	{
		// a watcher's bar (minimap, standings, portrait) would stand over the result and the cards
		s_barHidden = TRUE;
		HideControlBar();
	}
	if( s_samplesOf == s_endFrame )
		return;
	s_samplesOf = s_endFrame;

	s_samples.clear();
	for( Int i = 0; i < TheLaneWarStats->getSampleCount(); ++i )
		s_samples.push_back( TheLaneWarStats->getSample( i ) );
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int k = 0; k < 2; ++k )
			s_towerDown[ t ][ k ] = TheLaneWarStats->getTowerDownFrame( t, k );

	s_seats.clear();
	s_resultPortrait = NULL;
	for( Int t = 0; t < LANEWAR_TEAMS; ++t )
		for( Int s = 0; s < LANEWAR_SEATS; ++s )
		{
			Player *player = TheLaneWarDirector->getSeatPlayer( t, s ) >= 0 ? ThePlayerList->getNthPlayer( TheLaneWarDirector->getSeatPlayer( t, s ) ) : NULL;
			if( player == NULL )
				continue;
			const LaneWarSeatStats &st = TheLaneWarStats->getSeat( t, s );
			Seat c;
			c.name = player->getPlayerDisplayName();
			c.color = player->getPlayerColor();
			c.team = t;
			c.local = player == local && s_localTeam >= 0;
			c.prefix = prefixOf( player );
			c.portrait = portraitOf( player );
			const ThingTemplate *mvp = st.mvp( NULL );
			c.mvp = mvp != NULL ? mvp->getButtonImage() : NULL;
			c.stat[ 0 ] = st.waves;
			c.stat[ 1 ] = st.placed;
			c.stat[ 2 ] = st.totalEarned();
			c.stat[ 3 ] = st.totalSpent();
			c.stat[ 4 ] = st.kills;
			c.stat[ 5 ] = REAL_TO_INT( st.towerDamage + st.fortressDamage );
			c.stat[ 6 ] = (Int)( st.middleFrames / LOGICFRAMES_PER_SECOND );
			s_seats.push_back( c );
			// the result card shows the local player's general; a watcher sees the winners' first
			if( c.local || ( s_localTeam < 0 && s_resultPortrait == NULL && t == max( s_winner, 0 ) ) )
				s_resultPortrait = c.portrait;
		}
}

//-------------------------------------------------------------------------------------------------
void LaneWarEndScreen_draw( void )
{
	s_poolUsed = 0;
	if( s_phase == LANEWAR_END_COLLAPSE )
	{
		// the finale: the edges darken over a second while the fortress comes down
		vignette( Look::VIGNETTE_FINALE * clamp01( s_seconds ) );
		return;
	}
	if( s_phase != LANEWAR_END_OVER )
		return;
	if( s_resultsOpen )
	{
		drawResults();
		return;
	}
	// the result, over the heap
	shade( Look::SHADE_RESULT );
	vignette( Look::VIGNETTE_RESULT );
	drawResult( s_seconds );
}

//-------------------------------------------------------------------------------------------------
Bool LaneWarEndScreen_press( const ICoord2D *mouse, Bool right )
{
	if( s_phase != LANEWAR_END_OVER )
		return FALSE;
	if( right )
		return TRUE;
	if( !s_resultsOpen )
	{
		// a click on the result opens the results at once
		s_resultsOpen = TRUE;
		return TRUE;
	}
	for( Int k = 0; k < KEYS && !s_left; ++k )
	{
		if( !keyLive( k ) || !overKey( k, mouse ) )
			continue;
		s_left = TRUE;
		if( k == KEY_REPLAY )
			watchReplay();
		else if( k == KEY_REMATCH )
			rematch();
		else
			TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );	// the quit menu's Exit without its surrender
	}
	return TRUE;
}
