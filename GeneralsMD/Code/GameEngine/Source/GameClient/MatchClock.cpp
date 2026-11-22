// The match clock (MatchClock.h).  A steel tray at the top middle of the screen: the match's running
// time on its left, the time left in the phase on its right, the nights gone by between them under a
// small moon, and along its foot a bar of the cycle, day amber, dusk orange, night dark blue (dark red
// in Apocalypse) and dawn pale, with the sun or the moon riding it at the hour.  A match with no day
// and night shows the clock alone on a smaller plate.  Its pieces are ReforgedMatchClock.tga
// (Tools/hud_match_clock.py); a missing piece is drawn as flat steel.
//
// Thread: LOGIC-THREAD.md.  MatchClock_gather and MatchClock_draw run from InGameUI::drawHudOverlay at
// the render's sync point.  Both clocks are pure functions of the logic frame, so nothing of the
// logic's state is read but the frame and whether the Apocalypse director is on, set at map load.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/MatchClock.h"

#include "Common/GameCommon.h"
#include "Common/UnicodeString.h"
#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/Image.h"
#include "GameLogic/ApocalypseDirector.h"
#include "GameLogic/DayNightCycle.h"

namespace
{
	// lengths at 1080, put through the screen's height; the tray's art is drawn to these (hud_match_clock.py)
	const Real TRAY_W = 240.0f, TRAY_H = 52.0f;
	const Real BAR_X = 13.0f, BAR_Y = 35.0f, BAR_W = 214.0f, BAR_H = 8.0f;		///< the trough's inside
	const Real TEXT_Y = 8.0f, TEXT_SIDE = 14.0f;
	const Real MARK = 28.0f;										///< the sun or moon on the bar
	const Real NIGHT_MARK = 18.0f;							///< the small moon by the night count
	const Real PLATE_W = 96.0f, PLATE_H = 32.0f;	///< the clock alone
	const Real TOP_GAP = 4.0f;
	const Real FONT_POINTS = 13.0f;
	enum { LEAST_POINTS = 6 };

	const Color TEXT = GameMakeColor( 230, 236, 242, 255 );
	const Color TEXT_DIM = GameMakeColor( 170, 184, 196, 255 );
	const Color DROP = GameMakeColor( 0, 0, 0, 255 );
	const Color STEEL_FACE = GameMakeColor( 20, 28, 36, 225 );
	const Color STEEL_LIT = GameMakeColor( 200, 214, 226, 190 );
	const Color STEEL_RIM = GameMakeColor( 112, 132, 150, 235 );
	const Color PAST = GameMakeColor( 0, 0, 0, 110 );		///< laid over the part of the bar the cycle has gone by

	DisplayString *s_clock = NULL;
	DisplayString *s_phaseLeft = NULL;
	DisplayString *s_nights = NULL;
}

static Int px( Real at1080 )
{
	return max( REAL_TO_INT( at1080 * TheDisplay->getHeight() / 1080.0f + 0.5f ), 1 );
}

static const Image *piece( const char *name )
{
	return TheMappedImageCollection != NULL ? TheMappedImageCollection->findImageByName( AsciiString( name ) ) : NULL;
}

static DisplayString *keptString( DisplayString *&string, const UnicodeString &text )
{
	GameFont *font = TheFontLibrary->getFont( "Arial", max( px( FONT_POINTS ), (Int)LEAST_POINTS ), TRUE );
	if( string == NULL )
		string = TheDisplayStringManager->newDisplayString();
	if( string->getFont() != font )
		string->setFont( font );
	if( string->getText().compare( text ) != 0 )
		string->setText( text );
	return string;
}

/// m:ss, or h:mm:ss past an hour
static UnicodeString clockText( UnsignedInt frames, UnsignedInt framesPerSecond )
{
	const UnsignedInt seconds = frames / ( framesPerSecond > 0 ? framesPerSecond : 1 );
	UnicodeString text;
	if( seconds >= 3600 )
		text.format( u"%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60 );
	else
		text.format( u"%u:%02u", seconds / 60, seconds % 60 );
	return text;
}

static void drawSteel( Int x, Int y, Int w, Int h )
{
	TheDisplay->drawFillRect( x, y, w, h, STEEL_FACE );
	TheDisplay->drawFillRect( x + 1, y + 1, w - 2, 1, STEEL_LIT );
	TheDisplay->drawOpenRect( x, y, w, h, 1.0f, STEEL_RIM );
}

/// the bar's colour for a phase
static Color phaseColor( Int phase, Bool apocalypse )
{
	switch( phase )
	{
		case 1:		return GameMakeColor( 214, 120, 40, 255 );
		case 2:		return apocalypse ? GameMakeColor( 110, 18, 16, 255 ) : GameMakeColor( 28, 44, 96, 255 );
		case 3:		return GameMakeColor( 190, 150, 150, 255 );
		default:	return GameMakeColor( 196, 160, 64, 255 );
	}
}

//-------------------------------------------------------------------------------------------------
Bool MatchClock_wanted( void )
{
	// ponytail: Apocalypse only in this release (the owner, 2026-11-22); return TRUE here to show it in every match
	return TheApocalypseDirector != NULL && TheApocalypseDirector->isActive();
}

//-------------------------------------------------------------------------------------------------
void MatchClock_gather( UnsignedInt frame, MatchClockState &state )
{
	state.matchFrames = frame;
	state.framesPerSecond = (UnsignedInt)LOGICFRAMES_PER_SECOND;
	state.hasCycle = FALSE;
	state.phase = 0;
	state.night = 0.0f;
	state.intoPhase = 0;
	state.nightCount = -1;
	state.bloodMoon = FALSE;
	for( Int p = 0; p < MATCHCLOCK_PHASES; ++p )
		state.phaseFrames[ p ] = 0;

	if( DayNight_isOn() )
	{
		/* The box's cycle: its four lengths out of the header's seconds over the cycle's frames, and
			how far into the phase from the frame the phase last changed, found by halving, which holds
			wherever the cycle starts.  ponytail: once DayNight_sky is committed, the mark's height and
			the night count can come from it instead of this search. */
		static const UnsignedInt SECONDS[ MATCHCLOCK_PHASES ] = { DAYNIGHT_DAY_SECONDS, DAYNIGHT_DUSK_SECONDS, DAYNIGHT_NIGHT_SECONDS, DAYNIGHT_DAWN_SECONDS };
		const UnsignedInt cycle = DayNight_cycleFrames();
		for( Int p = 0; p < MATCHCLOCK_PHASES; ++p )
			state.phaseFrames[ p ] = (UnsignedInt)( (UnsignedInt64)SECONDS[ p ] * cycle / DAYNIGHT_CYCLE_SECONDS );
		state.hasCycle = TRUE;
		state.phase = DayNight_phase( frame );
		state.night = DayNight_amount( frame );
		UnsignedInt same = 0, differs = state.phaseFrames[ state.phase ] + 1;
		while( differs - same > 1 )
		{
			const UnsignedInt mid = ( same + differs ) / 2;
			if( mid <= frame && DayNight_phase( frame - mid ) == state.phase )
				same = mid;
			else
				differs = mid;
		}
		state.intoPhase = same;
		return;
	}

	if( TheApocalypseDirector != NULL && TheApocalypseDirector->isActive() )
	{
		UnsignedInt cycle = 0, phaseFrames = 0;
		Apocalypse_phaseAt( frame, &state.phase, &state.intoPhase, &phaseFrames, &cycle );
		for( Int p = 0; p < MATCHCLOCK_PHASES; ++p )
			state.phaseFrames[ p ] = (UnsignedInt)Apocalypse_phaseRules( p ).seconds * (UnsignedInt)LOGICFRAMES_PER_GAME_SECOND;
		state.hasCycle = TRUE;
		state.night = Apocalypse_nightAmount( state.phase, state.intoPhase, state.phaseFrames[ state.phase ] );
		state.nightCount = (Int)cycle + ( state.phase >= 2 ? 1 : 0 );
		// ponytail: the director has no Blood Moon yet (APOCALYPSE-ZOMBIES.md); bloodMoon stays FALSE until it does
	}
}

//-------------------------------------------------------------------------------------------------
void MatchClock_draw( const MatchClockState &state, Int top )
{
	const Int screenW = (Int)TheDisplay->getWidth();
	const Int y = top + px( TOP_GAP );
	const UnicodeString clock = clockText( state.matchFrames, state.framesPerSecond );

	if( !state.hasCycle )
	{
		const Int w = px( PLATE_W ), h = px( PLATE_H ), x = ( screenW - w ) / 2;
		drawSteel( x, y, w, h );
		DisplayString *s = keptString( s_clock, clock );
		Int tw = 0, th = 0;
		s->getSize( &tw, &th );
		s->draw( x + ( w - tw ) / 2, y + ( h - th ) / 2, TEXT, DROP );
		return;
	}

	const Int w = px( TRAY_W ), h = px( TRAY_H ), x = ( screenW - w ) / 2;
	const Image *tray = piece( "ReforgedMatchClockTray" );
	if( tray != NULL )
		TheDisplay->drawImage( tray, x, y, x + w, y + h );
	else
		drawSteel( x, y, w, h );

	// the bar: each phase its colour over its share of the cycle, the part gone by darkened
	const Bool apocalypse = !DayNight_isOn();
	const Int barX = x + px( BAR_X ), barY = y + px( BAR_Y ), barW = px( BAR_W ), barH = px( BAR_H );
	UnsignedInt cycle = 0, before = 0;
	for( Int p = 0; p < MATCHCLOCK_PHASES; ++p )
	{
		cycle += state.phaseFrames[ p ];
		if( p < state.phase )
			before += state.phaseFrames[ p ];
	}
	if( cycle == 0 )
		return;
	Int segmentX = barX;
	UnsignedInt sum = 0;
	for( Int p = 0; p < MATCHCLOCK_PHASES; ++p )
	{
		sum += state.phaseFrames[ p ];
		const Int segmentEnd = barX + (Int)( (UnsignedInt64)barW * sum / cycle );
		if( segmentEnd > segmentX )
			TheDisplay->drawFillRect( segmentX, barY, segmentEnd - segmentX, barH, phaseColor( p, apocalypse ) );
		segmentX = segmentEnd;
	}
	const UnsignedInt at = before + ( state.intoPhase < state.phaseFrames[ state.phase ] ? state.intoPhase : state.phaseFrames[ state.phase ] );
	const Int markX = barX + (Int)( (UnsignedInt64)barW * at / cycle );
	if( markX > barX )
		TheDisplay->drawFillRect( barX, barY, markX - barX, barH, PAST );

	// the sun by day and through the dusk's first half, the moon after it
	const Image *mark = piece( state.bloodMoon ? "ReforgedMatchClockBloodMoon" : state.night >= 0.5f ? "ReforgedMatchClockMoon" : "ReforgedMatchClockSun" );
	if( mark != NULL )
	{
		const Int m = px( MARK );
		TheDisplay->drawImage( mark, markX - m / 2, barY + barH / 2 - m / 2, markX - m / 2 + m, barY + barH / 2 - m / 2 + m );
	}

	// the match's time on the left, the phase's time left on the right, the nights between
	Int tw = 0, th = 0;
	DisplayString *s = keptString( s_clock, clock );
	s->draw( x + px( TEXT_SIDE ), y + px( TEXT_Y ), TEXT, DROP );
	const UnsignedInt left = state.phaseFrames[ state.phase ] > state.intoPhase ? state.phaseFrames[ state.phase ] - state.intoPhase : 0;
	s = keptString( s_phaseLeft, clockText( left, state.framesPerSecond ) );
	s->getSize( &tw, &th );
	s->draw( x + w - px( TEXT_SIDE ) - tw, y + px( TEXT_Y ), TEXT_DIM, DROP );
	if( state.nightCount > 0 )
	{
		UnicodeString count;
		count.format( u"%d", state.nightCount );
		s = keptString( s_nights, count );
		s->getSize( &tw, &th );
		const Int m = px( NIGHT_MARK );
		const Int groupX = x + ( w - m - tw ) / 2;
		const Image *moon = piece( state.bloodMoon ? "ReforgedMatchClockBloodMoon" : "ReforgedMatchClockMoon" );
		if( moon != NULL )
			TheDisplay->drawImage( moon, groupX, y + px( TEXT_Y ) + th / 2 - m / 2, groupX + m, y + px( TEXT_Y ) + th / 2 - m / 2 + m );
		s->draw( groupX + m, y + px( TEXT_Y ), TEXT, DROP );
	}
}
