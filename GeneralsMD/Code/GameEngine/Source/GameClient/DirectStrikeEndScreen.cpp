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

// FILE: DirectStrikeEndScreen.cpp /////////////////////////////////////////////////////////////////////
//
// The end of a Direct Strike match, the owner's concept B, "the hand" (experiments/proofs/2026-11-21/
// direct-strike-end/README-end.md in the wrapper): the finale while the beaten fortress comes down, the
// result card dealt over two backs, then the results screen.  Since menu package 13 the card and the
// results screen are drawn in the menu language (experiments/proofs/2026-11-24/spirit/c-bridge/
// README-end3.md, "Direct Strike's end screen"; ds_end.py): frame A, the result as the title with the
// match's facts after it, a wing a team with its head (the fortress's end health large), a row a seat,
// the timeline of the middle and the fortresses under them, and the end screen's keys.  Drawn on the
// 1920x1080 board with no window of its own; the rects are EndScreenLayout.h's.
//
// Times run on logic frames, which keep coming at LOGICFRAMES_PER_SECOND a wall second after the
// decision, since the match keeps running under the end: no stock timer drops it to the shell.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/DirectStrikeEndScreen.h"

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
#include "GameClient/EndScreenLayout.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/DirectStrikeHud.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/UIStyle.h"
#include "GameClient/View.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/GUIUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/DirectStrikeDirector.h"
#include "GameLogic/DirectStrikeStats.h"
#include "GameLogic/Object.h"
#include "Lib/Clock.h"

#include <vector>

namespace
{

enum { STATS = EndScreen::DS_COLUMNS, TEXT_POOL = 260, KEYS = 3 };
enum { KEY_REMATCH, KEY_REPLAY, KEY_MENU };

//=================================================================================================
// The look.  Colours past the language's, the result card's rects (from its top left corner), the
// timeline's and the sheet's glyph names.  Rects are on the 1920x1080 board.
//=================================================================================================
namespace Look
{

struct Box { Real x0, y0, x1, y1; };

// the teams' colours, ours blue and theirs red, as the lobby and the HUD show them
const Color OUR_BLUE = GameMakeColor( 66, 112, 236, 255 );
const Color THEIR_RED = GameMakeColor( 214, 44, 36, 255 );
const Real THEIR_RED_COLUMN_ALPHA = 170.0f / 255.0f;		///< their middle columns, under our blue
const Color GREYED = GameMakeColor( 199, 199, 199, 255 );		///< a beaten side's portrait, 0.78 bright
const Color CARD_GLASS = GameMakeColor( 0, 0, 0, 200 );
const Color BACK_GLASS = GameMakeColor( 0, 0, 0, 150 );
const Color WELL = GameMakeColor( 10, 12, 22, 230 );
const Color MARK_FILL = GameMakeColor( 6, 8, 12, 230 );
const Color LIGHT_KEY = GameMakeColor( 220, 224, 236, 255 );
const Real VIGNETTE_FINALE = 0.7f, VIGNETTE_RESULT = 0.7f;
const Int VIGNETTE_BANDS = 32;				///< layers of the gradient
const Int VIGNETTE_DEPTH_SHARE = 4;		///< a quarter of the screen's height in from each edge
const Color SHADE_RESULT = GameMakeColor( 0, 0, 0, 115 );			///< the world under the result, at 0.55
const Real BACKS_ALPHA = 0.75f;
const Real HEALTH_LINE = 2.0f;

// the result card, dealt over two backs
const Real RESULT_CENTRE_X = 960.0f, RESULT_CENTRE_Y = 540.0f, RESULT_W = 340.0f, RESULT_H = 452.0f;
const Real BACK_OFFSET_X = 70.0f, BACK_OFFSET_Y = 12.0f, DEAL_DROP = 80.0f;
const Box RESULT_PORTRAIT = { 10.0f, 10.0f, 330.0f, 300.0f };
const Box RESULT_RULE = { 10.0f, 303.0f, 330.0f, 305.0f };		///< green under a won portrait
const Real RESULT_WORD_X = 170.0f, RESULT_WORD_Y = 362.0f, RESULT_WORD_MAX = 300.0f;	///< alone on its line, centred
const Real RESULT_CLOCK_Y = 413.0f;		///< the match's length on its own line under the word, centred
const Real RESULT_REASON_Y = 437.0f, RESULT_REASON_MAX = 300.0f;	///< a team that left, under the length

// the timeline under the rows: its labels' column, the middle's band over the fortresses'
const Real PLOT_LEFT = 180.0f, PLOT_RIGHT = 1852.0f;
const Real MIDDLE_SHARE = 0.42f;			///< of the timeline's height, the middle's band
const Int MIDDLE_COLUMN_SECONDS = 2;
const Real TOWER_MARK_HALF = 10.0f;

// sheet glyphs, looked up as <common prefix><name> first (DirectStrikeTheme's SIDE_COMMON)
const char *const PREFIX = "CommonPro";
const char *const PIECE_TOWER_DOWN = "LaneStatDamage";
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
	* fall runs on.  The match is decided at DIRECTSTRIKE_COLLAPSE_SECONDS and keeps ticking under the finale. */
const Real FALL_GAME_SECONDS = 7.5f;
const Real BACKS_AT = 0.0f, CARD_AT = 0.3f, DEAL_SECONDS = 0.3f;
const Real RESULTS_AT = CARD_AT + DEAL_SECONDS + 2.0f;

struct Seat
{
	UnicodeString name;
	UnicodeString side;
	Color color;
	Int team;
	Bool local;
	const Image *portrait;
	const Image *mvp;
	Int stat[ STATS ];
};

// copied at the sync point
Int s_phase = DIRECTSTRIKE_END_PLAYING;
Real s_seconds = 0.0f;							///< into the finale while it plays, then since the result card
Int s_winner = -1;
Int s_leftTeam = -1;								///< the team that left the game and so lost it, -1 for a match played out
Int s_localTeam = -1;								///< -1 for a watcher and a replay
Int s_ourTeam = 0;									///< the team drawn on the left and in blue
Int s_matchSeconds = 0;
UnsignedInt s_endFrame = 0;
std::vector<Seat> s_seats;
std::vector<DirectStrikeSample> s_samples;
UnsignedInt s_towerDown[ DIRECTSTRIKE_TEAMS ][ 2 ];
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
/// a width on the screen as a length on the board
Real bw( Int screen ) { return (Real)screen * 1000.0f / (Real)max( UIStyle::px( 1000.0f ), 1 ); }

Real clamp01( Real v ) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

Color withAlpha( Color c, Real a )
{
	const UnsignedInt alpha = (UnsignedInt)( clamp01( a ) * ( ( (UnsignedInt)c >> 24 ) & 0xFF ) );
	return (Color)( ( (UnsignedInt)c & 0x00FFFFFF ) | ( alpha << 24 ) );
}

GameFont *font( Int px1080, Bool bold = FALSE )
{
	return UIStyle::menuFont( px1080, bold );
}

/// a piece of the common frame by its name, else a loose image of that bare name; NULL while missing
const Image *piece( const char *name )
{
	AsciiString full;
	full.format( "%s%s", Look::PREFIX, name );
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
/// a pooled string of text in font, made once and kept while it says the same
DisplayString *pooled( const UnicodeString &text, GameFont *face )
{
	if( s_poolUsed >= TEXT_POOL )
		return NULL;
	DisplayString *&s = s_pool[ s_poolUsed++ ];
	if( s == NULL )
		s = TheDisplayStringManager->newDisplayString();
	if( s->getFont() != face )
		s->setFont( face );
	if( s->getText().compare( text ) != 0 )
		s->setText( text );
	return s;
}

/// text at screen x (align -1 from x, 0 centred on x, +1 ending at x), its middle at screen y; its width
Int drawText( const UnicodeString &text, GameFont *face, Int x, Int y, Color color, Int align = -1 )
{
	if( text.isEmpty() )
		return 0;
	DisplayString *s = pooled( text, face );
	if( s == NULL )
		return 0;
	Int w = 0, h = 0;
	s->getSize( &w, &h );
	s->draw( align < 0 ? x : align == 0 ? x - w / 2 : x - w, y - h / 2, color, UIStyle::SHADOW );
	return w;
}

/// a label fitted to maxWidth pixels by the language's rule, a name cut with an ellipsis
Int drawFitted( const UnicodeString &text, GameFont *face, Int x, Int y, Color color, Int align, Int maxWidth, Bool name = FALSE )
{
	return drawText( name ? UIStyle::fitName( text, face, maxWidth ) : UIStyle::fitText( text, face, max( maxWidth, 1 ) ), face, x, y, color, align );
}

/// tracked capitals from screen x, their middle at y; their width
Int drawCaps( const UnicodeString &text, GameFont *face, Int x, Int y, Color color, Real tracking, Int align = -1 )
{
	const UnicodeString caps = UIStyle::capitals( text );
	const Int width = UIStyle::trackedWidth( caps, face, tracking );
	const Int left = align < 0 ? x : align == 0 ? x - width / 2 : x - width;
	UIStyle::drawTracked( caps, face, left, y - face->height / 2, color, tracking );
	return width;
}

UnicodeString clockText( Int seconds )
{
	UnicodeString text;
	text.format( u"%d:%02d", seconds / 60, seconds % 60 );
	return text;
}

/// a board rect, x0 y0 to x1 y1 at 1080
void fill( Real x0, Real y0, Real x1, Real y1, Color c )
{
	TheDisplay->drawFillRect( bx( x0 ), by( y0 ), max( bx( x1 ) - bx( x0 ), 1 ), max( by( y1 ) - by( y0 ), 1 ), c );
}

/// a box of the look, moved to a card's corner
void fillAt( const Look::Box &b, Real x0, Real y0, Color c )
{
	fill( x0 + b.x0, y0 + b.y0, x0 + b.x1, y0 + b.y1, c );
}

void lit( Real x0, Real y0, Real x1, Real y1, Color c )
{
	UIStyle::litLine( bx( x0 ), by( y0 ), max( bx( x1 ) - bx( x0 ), 1 ), max( by( y1 ) - by( y0 ), 1 ), c );
}

void frame( Real x0, Real y0, Real x1, Real y1, Real width, Color c )
{
	TheDisplay->drawOpenRect( bx( x0 ), by( y0 ), bx( x1 ) - bx( x0 ), by( y1 ) - by( y0 ), width, c );
}

/// an image inside a box, its aspect kept, against the box's top
void fitTop( const Image *image, Real x0, Real y0, Real x1, Real y1, Color tint )
{
	if( image == NULL || image->getImageWidth() <= 0 || image->getImageHeight() <= 0 )
		return;
	const Real w = x1 - x0, h = y1 - y0;
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

UnicodeString resultWord( void )
{
	if( s_winner < 0 )
		return TheGameText->fetch( "GUI:LaneEndDraw" );
	if( s_localTeam < 0 )
	{
		UnicodeString word;
		word.format( TheGameText->fetch( "GUI:LaneEndTeamWins" ), s_winner + 1 );
		return word;
	}
	return TheGameText->fetch( s_localTeam == s_winner ? "GUI:Victory" : "GUI:Defeat" );
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

Color teamColor( Int team ) { return team == s_ourTeam ? Look::OUR_BLUE : Look::THEIR_RED; }

//-------------------------------------------------------------------------------------------------
// The result card: two card backs, then the card dealt on them, dark glass in a lit blue edge
//-------------------------------------------------------------------------------------------------
void drawResult( Real t )
{
	using namespace Look;
	const Real backs = clamp01( ( t - BACKS_AT ) / DEAL_SECONDS );
	for( Int k = -1; k <= 1; k += 2 )
	{
		const Real ox = RESULT_CENTRE_X + BACK_OFFSET_X * k, oy = RESULT_CENTRE_Y + BACK_OFFSET_Y;
		const Real a = BACKS_ALPHA * backs;
		fill( ox - RESULT_W * 0.5f, oy - RESULT_H * 0.5f, ox + RESULT_W * 0.5f, oy + RESULT_H * 0.5f, withAlpha( BACK_GLASS, a ) );
		frame( ox - RESULT_W * 0.5f, oy - RESULT_H * 0.5f, ox + RESULT_W * 0.5f, oy + RESULT_H * 0.5f, 1.0f, withAlpha( UIStyle::FRAME, a ) );
	}

	const Real deal = clamp01( ( t - CARD_AT ) / DEAL_SECONDS );
	if( deal <= 0.0f )
		return;
	const Real x0 = RESULT_CENTRE_X - RESULT_W * 0.5f, y0 = RESULT_CENTRE_Y - RESULT_H * 0.5f + ( 1.0f - deal ) * DEAL_DROP;
	const Bool victory = won();
	fill( x0, y0, x0 + RESULT_W, y0 + RESULT_H, withAlpha( CARD_GLASS, deal ) );
	if( deal >= 1.0f )
		UIStyle::litEdge( UIStyle::boardRect( (Int)x0, (Int)y0, (Int)( x0 + RESULT_W ), (Int)( y0 + RESULT_H ) ), UIStyle::EDGE );
	else
		frame( x0, y0, x0 + RESULT_W, y0 + RESULT_H, 1.0f, withAlpha( UIStyle::EDGE, deal ) );
	fillAt( RESULT_PORTRAIT, x0, y0, withAlpha( WELL, deal ) );
	fitTop( s_resultPortrait, x0 + RESULT_PORTRAIT.x0, y0 + RESULT_PORTRAIT.y0, x0 + RESULT_PORTRAIT.x1, y0 + RESULT_PORTRAIT.y1,
		withAlpha( victory ? 0xFFFFFFFF : GREYED, deal ) );
	if( victory )
		fillAt( RESULT_RULE, x0, y0, withAlpha( UIStyle::ACTIVE, deal ) );
	GameFont *word = font( UIStyle::MENU_TEXT_TITLE, TRUE );
	const UnicodeString caps = UIStyle::fitText( UIStyle::capitals( resultWord() ), word, px( RESULT_WORD_MAX ) );
	drawText( caps, word, bx( x0 + RESULT_WORD_X ), by( y0 + RESULT_WORD_Y ), withAlpha( UIStyle::INK, deal ), 0 );
	drawText( clockText( s_matchSeconds ), font( 20 ), bx( x0 + RESULT_W * 0.5f ), by( y0 + RESULT_CLOCK_Y ), withAlpha( UIStyle::MUTED, deal ), 0 );
	drawFitted( reasonText(), font( UIStyle::MENU_TEXT_LABEL ), bx( x0 + RESULT_WORD_X ), by( y0 + RESULT_REASON_Y ), withAlpha( UIStyle::MUTED, deal ), 0,
		px( RESULT_REASON_MAX ) );
}

//-------------------------------------------------------------------------------------------------
// The results screen
//-------------------------------------------------------------------------------------------------
/// the seats on a team, and the most any team has
Int seatsOf( Int team )
{
	Int seats = 0;
	for( size_t i = 0; i < s_seats.size(); ++i )
		if( s_seats[ i ].team == team )
			++seats;
	return seats;
}

/// a team's fortress at the end, as a share of the most it ever held
Int fortressPercent( Int team )
{
	Int most = 1;
	for( size_t k = 0; k < s_samples.size(); ++k )
		most = max( most, s_samples[ k ].fortress[ team ] );
	if( s_samples.empty() )
		return 0;
	return UIStyle::percent( s_samples.back().fortress[ team ], most );
}

/// our share of the time a team held the middle, 0 to 1 (a half with nobody holding it)
Real ourMiddleShare( void )
{
	const Int sign = s_ourTeam == 0 ? 1 : -1;
	Real ours = 0.0f, theirs = 0.0f;
	for( size_t k = 0; k < s_samples.size(); ++k )
	{
		const Int held = s_samples[ k ].middle * sign;
		if( held > 0 )
			ours += (Real)held;
		else
			theirs -= (Real)held;
	}
	return ours + theirs > 0.0f ? ours / ( ours + theirs ) : 0.5f;
}

/** A team's head from y: its lit bar, TEAM n and VICTORY or DEFEAT, its fortress's end health large,
	* the column words right over the figures with DAMAGE green and sorted, the lit rule under them. */
void drawTeamHead( Int team, Real x0, Real x1, Real y, const Int left[ STATS ], const Int right[ STATS ] )
{
	lit( x0, y + 8.0f, x0 + 4.0f, y + 44.0f, teamColor( team ) );
	UnicodeString name;
	name.format( TheGameText->fetch( "GUI:LoadTeam" ), team + 1 );
	const Int nameWidth = drawCaps( name, font( 22, TRUE ), bx( x0 + 16.0f ), by( y + 26.0f ), UIStyle::INK, 2.4f );
	if( s_winner >= 0 )
	{
		const Bool winners = team == s_winner;
		drawCaps( TheGameText->fetch( winners ? "GUI:Victory" : "GUI:Defeat" ), font( UIStyle::MENU_TEXT_LABEL, TRUE ), bx( x0 + 32.0f ) + nameWidth,
			by( y + 27.0f ), winners ? UIStyle::ACTIVE : UIStyle::MUTED, 1.4f );
	}
	const Int figure = drawText( UIStyle::percentText( fortressPercent( team ) ), font( UIStyle::MENU_TEXT_TEAM_FIGURE ), bx( x1 - 8.0f ), by( y + 26.0f ),
		UIStyle::INK, 1 );
	drawCaps( TheGameText->fetch( "GUI:DirectStrikeFortress" ), font( UIStyle::MENU_TEXT_LABEL, TRUE ), bx( x1 - 20.0f ) - figure, by( y + 30.0f ), UIStyle::MUTED,
		1.2f, 1 );

	GameFont *caps = font( UIStyle::MENU_TEXT_LABEL, TRUE );
	for( Int col = 0; col < STATS; ++col )
	{
		const Bool sorted = col == EndScreen::DS_LEAD_COLUMN;
		const UnicodeString words = UIStyle::capitals( UIStyle::fitKey( STAT_LABELS[ col ], caps, px( (Real)( right[ col ] - left[ col ] + 10 ) ) ) );
		const Int width = drawText( words, caps, bx( (Real)right[ col ] + 1.0f ), by( y + 70.0f ), sorted ? UIStyle::ACTIVE : UIStyle::MUTED, 1 );
		if( sorted )
		{
			const Int size = max( px( 8.0f ), 4 );
			const Int cx = bx( (Real)right[ col ] ) - width - px( 6.0f ) - size;
			const Int cy = by( y + 70.0f ) - size / 4;
			for( Int row = 0; row < size / 2; ++row )
				TheDisplay->drawFillRect( cx + row, cy + row, size - 2 * row, 1, UIStyle::ACTIVE );
		}
	}
	lit( x0, y + 92.0f, x1, y + 93.0f, UIStyle::FRAME );
}

/** A seat's row: his army's picture with his colour along its foot, his name (gold for the local player)
	* and side, TOP UNIT and its picture at the line's right, his seven figures on the foot, the damage at
	* 24 with its bar; a column's best in cyan when three seats or more stand. */
void drawSeatRow( const Seat &s, Real x0, Real y0, Real x1, Real y1, const Int left[ STATS ], const Int right[ STATS ], const Int best[ STATS ],
	Int topDamage )
{
	const Bool winners = s.team == s_winner;
	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
	const IRegion2D row = UIStyle::boardRect( (Int)x0, (Int)y0, (Int)x1, (Int)y1 );
	const Bool hovered = mouse->x >= row.lo.x && mouse->x < row.hi.x && mouse->y >= row.lo.y && mouse->y < row.hi.y;
	UIStyle::menuRow( row, s.local ? UIStyle::ROW_LOCAL : hovered ? UIStyle::ROW_HOVER : UIStyle::ROW_PLAIN );

	const Real pic = y1 - y0 - 16.0f;
	const Real px0 = x0 + 8.0f, py0 = y0 + 8.0f;
	fill( px0, py0, px0 + pic, py0 + pic, Look::WELL );
	fitTop( s.portrait, px0, py0, px0 + pic, py0 + pic, winners || s_winner < 0 ? 0xFFFFFFFF : Look::GREYED );
	fill( px0, py0 + pic - 4.0f, px0 + pic, py0 + pic, s.color );
	if( UIStyle::readable( s.color ) != s.color )
		fill( px0, py0 + pic - 5.0f, px0 + pic, py0 + pic - 4.0f, Look::LIGHT_KEY );

	// the top unit at the line's right, framed in his colour (gold for the local player)
	const Real nameY = y0 + 24.0f;
	const Real mvpH = 40.0f, mvpW = mvpH * 1.36f;
	const Real mvpX1 = x1 - 8.0f, mvpX0 = mvpX1 - mvpW;
	fill( mvpX0, nameY - mvpH * 0.5f, mvpX1, nameY + mvpH * 0.5f, Look::MARK_FILL );
	if( s.mvp != NULL )
		TheDisplay->drawImage( s.mvp, bx( mvpX0 ) + 2, by( nameY - mvpH * 0.5f ) + 2, bx( mvpX1 ) - 2, by( nameY + mvpH * 0.5f ) - 2 );
	else
		drawText( UnicodeString( u"-" ), font( UIStyle::MENU_TEXT_BODY, TRUE ), bx( ( mvpX0 + mvpX1 ) * 0.5f ), by( nameY ), UIStyle::MUTED, 0 );
	frame( mvpX0, nameY - mvpH * 0.5f, mvpX1, nameY + mvpH * 0.5f, 2.0f, s.local ? UIStyle::GOLD : UIStyle::readable( s.color ) );
	const Int topWidth = drawCaps( TheGameText->fetch( "GUI:LaneEndTopUnit" ), font( UIStyle::MENU_TEXT_LABEL, TRUE ), bx( mvpX0 - 12.0f ), by( nameY ),
		UIStyle::MUTED, 1.2f, 1 );

	const Real nameX = px0 + pic + 16.0f;
	GameFont *nameFont = font( UIStyle::MENU_TEXT_ROW_FIGURE );
	const Int room = bx( mvpX0 - 24.0f ) - topWidth - bx( nameX );
	const Int nameWidth = drawFitted( s.name, nameFont, bx( nameX ), by( nameY ), s.local ? UIStyle::GOLD : UIStyle::INK, -1, room / 2 + room / 4, TRUE );
	drawFitted( s.side, font( 16 ), bx( nameX ) + nameWidth + px( 12.0f ), by( nameY + 2.0f ), UIStyle::MUTED, -1, room - nameWidth - px( 12.0f ) );

	// the figures on the foot line; the damage, the lead, at 24 with its bar
	const Real figureY = y1 - 26.0f;
	for( Int col = 0; col < STATS; ++col )
	{
		const Bool lead = col == EndScreen::DS_LEAD_COLUMN;
		GameFont *face = font( lead ? UIStyle::MENU_TEXT_ROW_FIGURE : UIStyle::MENU_TEXT_BODY );
		const UnicodeString value = col == STATS - 1 ? clockText( s.stat[ col ] ) : UIStyle::grouped( s.stat[ col ] );
		const Bool isBest = best[ col ] >= 0 && s.stat[ col ] == best[ col ];
		const Int width = drawText( value, face, bx( (Real)right[ col ] ), by( figureY ), isBest ? UIStyle::EDGE_LIT : UIStyle::INK, 1 );
		const Int under = by( figureY ) + face->height / 2 + 1;
		if( isBest )
			TheDisplay->drawFillRect( bx( (Real)right[ col ] ) - width, under, width, max( px( 2.0f ), 1 ), UIStyle::EDGE_LIT );
		if( lead )
		{
			const Int barLeft = bx( (Real)left[ col ] );
			const Int full = bx( (Real)right[ col ] ) - barLeft;
			const Real share = topDamage > 0 ? (Real)s.stat[ col ] / topDamage : 0.0f;
			TheDisplay->drawFillRect( barLeft, under + 1, max( (Int)( full * clamp01( share ) ), 1 ), max( px( 3.0f ), 2 ),
				isBest ? UIStyle::EDGE_LIT : UIStyle::readable( teamColor( s.team ) ) );
		}
	}
}

/// the x of a frame on the timeline's plot
Real timelineX( UnsignedInt frameAt )
{
	const Real span = s_endFrame > 0 ? (Real)s_endFrame : 1.0f;
	return Look::PLOT_LEFT + ( Look::PLOT_RIGHT - Look::PLOT_LEFT ) * clamp01( (Real)frameAt / span );
}

/** The timeline from top to bottom: MIDDLE, each team's hold in its colour from a white centre line, a
	* column every two seconds, the teams named at the left; FORTRESS, the two health step lines on 0, 50
	* and 100 % gridlines with a mark where a tower fell; a tick every wave and the minutes under them, the
	* match's end in bold at the right. */
void drawTimeline( Real top, Real bottom )
{
	using namespace Look;
	GameFont *caps = font( UIStyle::MENU_TEXT_LABEL, TRUE );
	GameFont *labelFont =font( UIStyle::MENU_TEXT_LABEL );
	const Real axisY = bottom - 18.0f;
	const Real middleTop = top + 6.0f, middleBottom = top + ( axisY - top ) * MIDDLE_SHARE;
	const Real middleY = ( middleTop + middleBottom ) * 0.5f, reach = ( middleBottom - middleTop ) * 0.5f;
	const Real healthTop = middleBottom + 22.0f, healthBottom = axisY - 6.0f;

	// the bands' names at the left
	drawCaps( TheGameText->fetch( "GUI:LaneEndTimelineMiddle" ), caps, bx( EndScreen::INSET_LEFT ), by( middleTop + 8.0f ), UIStyle::INK, 1.2f );
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		UnicodeString name;
		name.format( TheGameText->fetch( "GUI:LoadTeam" ), t + 1 );
		const Bool above = t == s_ourTeam;
		const Real y = above ? middleY - reach * 0.5f : middleY + reach * 0.5f;
		fill( EndScreen::INSET_LEFT, y - 6.0f, EndScreen::INSET_LEFT + 4.0f, y + 6.0f, teamColor( t ) );
		drawFitted( name, labelFont,bx( EndScreen::INSET_LEFT + 12.0f ), by( y ), UIStyle::MUTED, -1, px( PLOT_LEFT - EndScreen::INSET_LEFT - 20.0f ) );
	}
	drawCaps( TheGameText->fetch( "GUI:DirectStrikeFortress" ), caps, bx( EndScreen::INSET_LEFT ), by( healthTop + 8.0f ), UIStyle::INK, 1.2f );

	// the middle: our share past the line above it in blue, theirs below in red, in columns
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
			fill( x0, middleY - reach * share, x1, middleY, OUR_BLUE );
		else if( share < 0.0f )
			fill( x0, middleY, x1, middleY - reach * share, withAlpha( THEIR_RED, THEIR_RED_COLUMN_ALPHA ) );
	}
	fill( PLOT_LEFT, middleY, PLOT_RIGHT, middleY + 1.0f, UIStyle::INK );

	// the fortresses' health on its gridlines, a run drawn once for as long as it holds
	for( Int step = 0; step <= 2; ++step )
	{
		const Real y = healthBottom - ( healthBottom - healthTop ) * step * 0.5f;
		fill( PLOT_LEFT, y, PLOT_RIGHT, y + 1.0f, UIStyle::GRID );
		drawText( UIStyle::percentText( step * 50 ), labelFont,bx( PLOT_LEFT - 8.0f ), by( y ), UIStyle::MUTED, 1 );
	}
	Int most = 1;
	for( size_t k = 0; k < s_samples.size(); ++k )
		for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
			most = max( most, s_samples[ k ].fortress[ t ] );
	const Image *mark = piece( PIECE_TOWER_DOWN );
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
	{
		const Color c = teamColor( t );
		Real runX = timelineX( 0 ), runY = healthTop;
		for( size_t k = 0; k < s_samples.size(); ++k )
		{
			const Real y = healthBottom - ( healthBottom - healthTop ) * s_samples[ k ].fortress[ t ] / most;
			const Real x = timelineX( s_samples[ k ].frame );
			if( y != runY || k + 1 == s_samples.size() )
			{
				TheDisplay->drawLine( bx( runX ), by( runY ), bx( x ), by( runY ), HEALTH_LINE, c );
				TheDisplay->drawLine( bx( x ), by( runY ), bx( x ), by( y ), HEALTH_LINE, c );
				runX = x;
				runY = y;
			}
		}

		// where a tower fell: a dark mark ringed in the team's colour on the full line
		for( Int k = 0; k < 2; ++k )
		{
			if( s_towerDown[ t ][ k ] == 0 )
				continue;
			const Real x = timelineX( s_towerDown[ t ][ k ] ), y = healthTop - 4.0f;
			fill( x - TOWER_MARK_HALF, y - TOWER_MARK_HALF, x + TOWER_MARK_HALF, y + TOWER_MARK_HALF, MARK_FILL );
			frame( x - TOWER_MARK_HALF, y - TOWER_MARK_HALF, x + TOWER_MARK_HALF, y + TOWER_MARK_HALF, 1.0f, c );
			if( mark != NULL )
				TheDisplay->drawImage( mark, bx( x - TOWER_MARK_HALF + 3.0f ), by( y - TOWER_MARK_HALF + 3.0f ), bx( x + TOWER_MARK_HALF - 3.0f ),
					by( y + TOWER_MARK_HALF - 3.0f ), UIStyle::INK );
		}
	}

	// the foot: a tick every wave, the minutes (every five past ten minutes), the end in bold
	const UnsignedInt tickFrames = DIRECTSTRIKE_TICK_FRAMES, minute = 60 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt markFrames = s_endFrame < 10 * minute ? minute : s_endFrame < 30 * minute ? 5 * minute : 10 * minute;
	for( UnsignedInt f = DIRECTSTRIKE_FIRST_TICK_FRAMES; f < s_endFrame && tickFrames > 0; f += tickFrames )
		fill( timelineX( f ), axisY - 4.0f, timelineX( f ) + 1.0f, axisY, withAlpha( UIStyle::MUTED, 0.6f ) );
	const Real endX = timelineX( s_endFrame );
	for( UnsignedInt f = 0; f < s_endFrame; f += markFrames )
	{
		const Real x = timelineX( f );
		if( endX - x < 60.0f )
			break;
		fill( x, middleTop, x + 1.0f, axisY, withAlpha( UIStyle::GRID, 0.8f ) );
		drawText( clockText( (Int)( f / LOGICFRAMES_PER_SECOND ) ), labelFont,bx( x ), by( axisY + 9.0f ), UIStyle::MUTED, f == 0 ? -1 : 0 );
	}
	drawText( clockText( s_matchSeconds ), caps, bx( endX ), by( axisY + 9.0f ), UIStyle::INK, 1 );
}

/// the key's rect on the key row: Rematch (or Lobby) lit at the right, then Replay, then Back to menu
IRegion2D keyRect( Int k )
{
	Int x0, x1;
	EndScreen::keyRect( k, x0, x1 );
	return UIStyle::boardRect( x0, EndScreen::KEYS_TOP, x1, EndScreen::KEYS_BOTTOM );
}

Bool overKey( Int k, const ICoord2D *mouse )
{
	const IRegion2D r = keyRect( k );
	return mouse->x >= r.lo.x && mouse->x < r.hi.x && mouse->y >= r.lo.y && mouse->y < r.hi.y;
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
	* menu's reallyDoStart).  A network game has no room to go back to: an online room is gone from the
	* server at its game.start, and the LAN and online rooms pop to their lobby when the shell comes back
	* (LanGameOptionsMenuInit, WOLGameSetupMenuInit).  So there Rematch clears the game exactly as Back does. */
void rematch( void )
{
	if( TheGameLogic->isInMultiplayerGame() )
	{
		TheMessageStream->appendMessage( GameMessage::MSG_CLEAR_GAME_DATA );
		return;
	}
	const Int gameSpeedPercent = TheSkirmishGameInfo->getGameSpeedPercent();	// the match's, the lobby chip's pick, kept
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

/** The end screen's keys: Rematch (Lobby in a network match, which only goes back to its room) lit, Replay
	* and Back to menu; a key that cannot work here in the dimmed state. */
void drawKeys( void )
{
	const ICoord2D *mouse = &TheMouse->getMouseStatus()->pos;
	const Bool down = TheMouse->getMouseStatus()->leftState != MBS_Up;
	GameFont *face = font( UIStyle::MENU_TEXT_KEY, TRUE );
	for( Int k = 0; k < KEYS; ++k )
	{
		const Bool live = keyLive( k ), hot = live && overKey( k, mouse );
		const UIStyle::KeyState state = !live ? UIStyle::KEY_DISABLED : hot && down ? UIStyle::KEY_PRESSED : hot ? UIStyle::KEY_HOVER : UIStyle::KEY_NORMAL;
		const char *label = k == KEY_REMATCH && TheGameLogic->isInMultiplayerGame() ? "GUI:LaneEndLobby" : KEY_LABELS[ k ];
		const IRegion2D box = keyRect( k );
		DisplayString *words = pooled( UIStyle::fitText( UIStyle::capitals( TheGameText->fetch( label ) ), face, box.hi.x - box.lo.x - px( 16.0f ) ), face );
		UIStyle::menuKey( box, words, state, k == KEY_REMATCH && live );
	}
}

void drawResults( void )
{
	using namespace EndScreen;
	const IRegion2D panel = UIStyle::boardRect( PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM );
	UIStyle::glass( panel, 168 );
	UIStyle::frameA( panel );

	// the title and the facts after it: Direct Strike, the format, the length, a team that left
	const Real titleMiddle = ( TITLE_TOP + TITLE_BOTTOM ) * 0.5f;
	const Int titleWidth = drawCaps( resultWord(), font( UIStyle::MENU_TEXT_TITLE, TRUE ), bx( INSET_LEFT ), by( titleMiddle ), UIStyle::INK,
		UIStyle::MENU_TEXT_TITLE * 0.12f );
	static const WideChar DOT[] = { u' ', 0x00B7, u' ', 0 };
	UnicodeString facts = TheGameText->fetch( "GUI:DirectStrikeLower" );
	UnicodeString format;
	format.format( u"%dv%d", seatsOf( s_ourTeam ), seatsOf( 1 - s_ourTeam ) );
	facts.concat( DOT );
	facts.concat( format );
	facts.concat( DOT );
	facts.concat( clockText( s_matchSeconds ) );
	const UnicodeString reason = reasonText();
	if( !reason.isEmpty() )
	{
		facts.concat( DOT );
		facts.concat( reason );
	}
	const Int factsLeft = bx( INSET_LEFT ) + titleWidth + px( 24.0f );
	drawFitted( facts, font( 20 ), factsLeft, by( titleMiddle + 2.0f ), UIStyle::MUTED, -1, bx( INSET_RIGHT ) - factsLeft );

	// the lit rule, and the share of the middle each team held as a line under it
	lit( PANEL_LEFT + 2, RULE_Y, PANEL_RIGHT - 2, RULE_Y + 2, UIStyle::FRAME );
	const Real x0 = PANEL_LEFT + 2, x1 = PANEL_RIGHT - 2;
	const Real split = x0 + ( x1 - x0 ) * ourMiddleShare();
	lit( x0, SHARE_TOP, split, SHARE_BOTTOM, teamColor( s_ourTeam ) );
	lit( split, SHARE_TOP, x1, SHARE_BOTTOM, teamColor( 1 - s_ourTeam ) );
	fill( ( x0 + x1 ) * 0.5f, SHARE_TOP - 4.0f, ( x0 + x1 ) * 0.5f + 1.0f, SHARE_BOTTOM + 4.0f, UIStyle::INK );

	// a wing a team, ours on the left; a column's best in cyan when three seats or more stand
	const Int most = max( seatsOf( 0 ), seatsOf( 1 ) );
	const Real rowHeight = (Real)directStrikeRowHeight( most );
	Int best[ STATS ];
	Int topDamage = 0;
	for( Int col = 0; col < STATS; ++col )
	{
		best[ col ] = -1;
		if( s_seats.size() < 3 || col == 3 )		// money spent is no achievement
			continue;
		Int value = -1, holders = 0;
		for( size_t i = 0; i < s_seats.size(); ++i )
		{
			if( s_seats[ i ].stat[ col ] > value )
			{
				value = s_seats[ i ].stat[ col ];
				holders = 1;
			}
			else if( s_seats[ i ].stat[ col ] == value )
				++holders;
		}
		if( holders == 1 && value > 0 )
			best[ col ] = value;
	}
	for( size_t i = 0; i < s_seats.size(); ++i )
		topDamage = max( topDamage, s_seats[ i ].stat[ DS_LEAD_COLUMN ] );
	for( Int side = 0; side < DIRECTSTRIKE_TEAMS; ++side )
	{
		const Int team = side == 0 ? s_ourTeam : 1 - s_ourTeam;
		const Real wx0 = side == 0 ? (Real)WING_LEFT_X0 : (Real)WING_RIGHT_X0;
		const Real wx1 = side == 0 ? (Real)WING_LEFT_X1 : (Real)WING_RIGHT_X1;
		Int left[ STATS ], right[ STATS ];
		directStrikeColumns( (Int)wx1, left, right );
		drawTeamHead( team, wx0, wx1, CONTENT_TOP, left, right );
		Real y = CONTENT_TOP + TEAM_HEAD;
		for( size_t i = 0; i < s_seats.size(); ++i )
		{
			if( s_seats[ i ].team != team )
				continue;
			drawSeatRow( s_seats[ i ], wx0, y, wx1, y + rowHeight, left, right, best, topDamage );
			y += rowHeight;
		}
	}

	// the lane's centre between the wings, a dashed line down the gap
	const Real timelineTop = (Real)directStrikeTimelineTop( most );
	const Real centre = ( WING_LEFT_X1 + WING_RIGHT_X0 ) * 0.5f;
	for( Real y = CONTENT_TOP; y < timelineTop - DS_TIMELINE_GAP; y += 12.0f )
		fill( centre, y, centre + 1.0f, min( y + 6.0f, timelineTop - DS_TIMELINE_GAP ), UIStyle::FRAME );

	drawTimeline( timelineTop, (Real)DS_TIMELINE_BOTTOM );

	// the foot names the marks, the keys stand on the key row
	fill( INSET_LEFT, PAGE_FOOT_RULE, INSET_RIGHT, PAGE_FOOT_RULE + 1, UIStyle::withAlpha( UIStyle::FRAME, 110 ) );
	GameFont *labelFont =font( UIStyle::MENU_TEXT_LABEL );
	const Real footY = ( KEYS_TOP + KEYS_BOTTOM ) * 0.5f;
	Real at = INSET_LEFT;
	fill( at, footY - 8.0f, at + 16.0f, footY + 8.0f, Look::MARK_FILL );
	frame( at, footY - 8.0f, at + 16.0f, footY + 8.0f, 1.0f, UIStyle::MUTED );
	at += 24.0f;
	at += bw( drawText( TheGameText->fetch( "GUI:LaneEndTowerDown" ), labelFont,bx( at ), by( footY ), UIStyle::MUTED ) ) + 28.0f;
	fill( at, footY - 4.0f, at + 1.0f, footY + 4.0f, UIStyle::MUTED );
	at += 10.0f;
	drawText( TheGameText->fetch( "GUI:LaneEndWaveSent" ), labelFont,bx( at ), by( footY ), UIStyle::MUTED );
	drawKeys();
}

}	// namespace

//-------------------------------------------------------------------------------------------------
Bool DirectStrikeEndScreen_isShowing( void )
{
	return s_phase == DIRECTSTRIKE_END_OVER;
}

Real DirectStrikeEndScreen_finale( void )
{
	return s_phase == DIRECTSTRIKE_END_COLLAPSE ? s_seconds : -1.0f;
}

void DirectStrikeEndScreen_preDraw( void )
{
	if( TheDirectStrikeDirector == NULL || TheDirectStrikeStats == NULL || !TheDirectStrikeDirector->isActive() || !TheGameLogic->isInGame()
			|| TheGameLogic->isInShellGame() )
	{
		s_phase = DIRECTSTRIKE_END_PLAYING;
		s_cameraMoved = s_resultsOpen = s_left = s_barHidden = FALSE;
		s_samplesOf = 0;
		slowMotion( FALSE );
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt down = 0;
	Int fallen = 0;
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		if( TheDirectStrikeStats->getFortressDownFrame( t ) != 0 && ( down == 0 || TheDirectStrikeStats->getFortressDownFrame( t ) < down ) )
		{
			down = TheDirectStrikeStats->getFortressDownFrame( t );
			fallen = t;
		}
	// a match a team left has no fall to watch: the card at once
	const UnsignedInt cardFrame = down != 0 ? down + (UnsignedInt)( FALL_GAME_SECONDS * LOGICFRAMES_PER_GAME_SECOND ) : 0;

	s_phase = TheDirectStrikeStats->getEndPhase();
	if( s_phase == DIRECTSTRIKE_END_OVER && now < cardFrame )
		s_phase = DIRECTSTRIKE_END_COLLAPSE;	// decided, and the fall still has to finish before the card covers it
	if( s_phase != DIRECTSTRIKE_END_COLLAPSE )
		slowMotion( FALSE );
	if( s_phase == DIRECTSTRIKE_END_PLAYING )
	{
		s_cameraMoved = s_resultsOpen = s_left = s_barHidden = FALSE;
		s_samplesOf = 0;
		return;
	}

	Int team, seat;
	const Player *local = ThePlayerList->getLocalPlayer();
	s_localTeam = local != NULL && !TheGameLogic->isInReplayGame() && TheDirectStrikeDirector->seatOf( local, &team, &seat ) ? team : -1;
	s_ourTeam = s_localTeam >= 0 ? s_localTeam : 0;

	if( s_phase == DIRECTSTRIKE_END_COLLAPSE )
	{
		// the finale: the camera to the fortress coming down, once, then the fall in slow motion
		const UnsignedInt ms = Clock_Milliseconds();
		if( !s_cameraMoved )
			s_finaleStartMs = ms;
		s_seconds = (Real)( ms - s_finaleStartMs ) / 1000.0f;
		slowMotion( s_seconds >= SLOW_FROM && s_seconds < SLOW_UNTIL );
		const Object *fortress = TheGameLogic->findObjectByID( TheDirectStrikeDirector->getFortress( fallen ) );
		if( !s_cameraMoved && TheTacticalView != NULL )
		{
			s_cameraMoved = TRUE;
			const Coord3D at = fortress != NULL ? *fortress->getPosition() : *TheDirectStrikeDirector->getSpawn( fallen );
			// side-on to the lane with our end on the left (a watcher's is team A's), held through the
			// move by orient FALSE, and in a little
			DirectStrikeHud_turnToLane( s_ourTeam );
			TheTacticalView->moveCameraTo( &at, (Int)FINALE_CAMERA_MS, 0, FALSE, FINALE_CAMERA_EASE_MS, FINALE_CAMERA_EASE_MS );
			TheTacticalView->zoomCamera( TheTacticalView->getZoom() * FINALE_ZOOM, (Int)FINALE_CAMERA_MS, FINALE_CAMERA_EASE_MS,
																	 FINALE_CAMERA_EASE_MS );
		}
		return;
	}

	// over: what the result and the results screen show, frozen from the decision on
	s_endFrame = TheDirectStrikeStats->getEndFrame();
	s_seconds = (Real)( now - max( s_endFrame, cardFrame ) ) / LOGICFRAMES_PER_SECOND;
	s_winner = TheDirectStrikeStats->getWinner();
	s_leftTeam = -1;
	for( Int t = DIRECTSTRIKE_TEAMS - 1; t >= 0; --t )
		if( TheDirectStrikeStats->getTeamLeftFrame( t ) != 0 && TheDirectStrikeStats->isOut( t, s_endFrame ) )
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
	for( Int i = 0; i < TheDirectStrikeStats->getSampleCount(); ++i )
		s_samples.push_back( TheDirectStrikeStats->getSample( i ) );
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		for( Int k = 0; k < 2; ++k )
			s_towerDown[ t ][ k ] = TheDirectStrikeStats->getTowerDownFrame( t, k );

	s_seats.clear();
	s_resultPortrait = NULL;
	for( Int t = 0; t < DIRECTSTRIKE_TEAMS; ++t )
		for( Int s = 0; s < DIRECTSTRIKE_SEATS; ++s )
		{
			Player *player = TheDirectStrikeDirector->getSeatPlayer( t, s ) >= 0 ? ThePlayerList->getNthPlayer( TheDirectStrikeDirector->getSeatPlayer( t, s ) ) : NULL;
			if( player == NULL )
				continue;
			const DirectStrikeSeatStats &st = TheDirectStrikeStats->getSeat( t, s );
			Seat c;
			c.name = player->getPlayerDisplayName();
			c.side = player->getPlayerTemplate() ? player->getPlayerTemplate()->getDisplayName() : UnicodeString::TheEmptyString;
			c.color = player->getPlayerColor();
			c.team = t;
			c.local = player == local && s_localTeam >= 0;
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
void DirectStrikeEndScreen_draw( void )
{
	s_poolUsed = 0;
	if( s_phase == DIRECTSTRIKE_END_COLLAPSE )
	{
		// the finale: the edges darken over a second while the fortress comes down
		vignette( Look::VIGNETTE_FINALE * clamp01( s_seconds ) );
		return;
	}
	if( s_phase != DIRECTSTRIKE_END_OVER )
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
Bool DirectStrikeEndScreen_press( const ICoord2D *mouse, Bool right )
{
	if( s_phase != DIRECTSTRIKE_END_OVER )
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
