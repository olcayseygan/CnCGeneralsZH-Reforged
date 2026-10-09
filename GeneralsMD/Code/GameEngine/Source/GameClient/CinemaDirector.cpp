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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: CinemaDirector.cpp ///////////////////////////////////////////////////////////////////////
//
// -cinema <name>: take the interface off the screen and fly the camera from a shot list.
//
// The script engine can already move a camera, but only from inside a map, and every one of its
// moves runs on the wall clock: under -video the passes are paced by the capture, not by 33ms, and
// a pan timed in milliseconds comes out at whatever speed the disk allowed. Here the clock is the
// logic frame (plus how far the render pass is between two of them, when nothing is recording), so
// the same shot list over the same -seed frames the same picture on every run and every machine.
//
// A shot list is Run/Cinema/<name>.txt, one line a shot:  <seconds> <verb> <arguments>.
// Every shot starts at its own time and runs for its own length; shots on different channels
// (where the camera looks, how far away it is, which way it faces, how far it tilts) overlap freely,
// which is how a pan that climbs and turns at once is written: three lines with one time.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/Recorder.h"
#include "Common/ThingTemplate.h"
#include "GameClient/CinemaDirector.h"
#include "GameClient/CommandXlat.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Display.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"

#include <algorithm>
#include <vector>

static const char *CINEMA_DIRECTORY = "Cinema\\";
static const char *CINEMA_EXTENSION = ".txt";
static const char CINEMA_COMMENT_CHAR = '#';
enum { CINEMA_MAX_TOKENS = 3 + 2 * CINEMA_MAX_ROUTE_POINTS };
static const Real CINEMA_PITCH_LIMIT_DEGREES = 36.0f;	///< View::setPitch clamps to PI/5 either way
static const Real CINEMA_FOLLOW_RATE = 4.0f;						///< how fast a follow catches up, per second
static const Real CINEMA_CENTRE_GAIN = 0.5f;						///< share of the framing error taken out each frame
static const Real CINEMA_CENTRE_LIMIT = 900.0f;					///< a correction larger than this is a bad projection
static const Real CINEMA_FRAME_MS = 1000.0f / (Real)LOGICFRAMES_PER_SECOND;
static const Real CINEMA_CLOCK_PULL = 0.05f;						///< share of the gap to the logic clock closed each pass
static const Real CINEMA_CLOCK_SNAP_SECONDS = 0.5f;			///< further behind than this and the camera clock jumps to the logic one
static const Real CINEMA_LETTERBOX_ASPECT = 2.39f;	///< the game's own bars crop to 16:9, which on a 16:9 screen is no bars at all

// ------------------------------------------------------------------------------------------------
// the shot list
// ------------------------------------------------------------------------------------------------

static Bool isCinemaSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static Int tokenizeCinemaLine( const char *line, AsciiString *tokens, Int maxTokens )
{
	Int found = 0;
	const char *at = line;
	while (*at != 0)
	{
		while (*at != 0 && isCinemaSpace( *at ))
			++at;
		if (*at == 0 || *at == CINEMA_COMMENT_CHAR)
			break;
		const char *start = at;
		while (*at != 0 && !isCinemaSpace( *at ) && *at != CINEMA_COMMENT_CHAR)
			++at;
		if (found < maxTokens)
		{
			char word[ 128 ];
			const Int length = (Int)(at - start);
			const Int copied = (length < (Int)sizeof( word ) - 1) ? length : (Int)sizeof( word ) - 1;
			memcpy( word, start, copied );
			word[ copied ] = 0;
			tokens[ found ].set( word );
		}
		++found;
	}
	return found;
}

/** strtod takes "12abc" as 12; a typo in a shot list should say so rather than fire at the wrong time. */
static Bool parseCinemaNumber( const AsciiString &token, Real *value )
{
	const char *text = token.str();
	char *end = NULL;
	const double parsed = strtod( text, &end );
	if (end == text || *end != 0)
		return FALSE;
	*value = (Real)parsed;
	return TRUE;
}

static Bool parseCinemaSwitch( const AsciiString &token, Bool *on )
{
	if (token == "on")
	{
		*on = TRUE;
		return TRUE;
	}
	if (token == "off")
	{
		*on = FALSE;
		return TRUE;
	}
	return FALSE;
}

static Bool refuseCinemaLine( AsciiString *reason, const char *why )
{
	reason->set( why );
	return FALSE;
}

/** Read `count` numbers from tokens[first...] into out[]. */
static Bool parseCinemaNumbers( const AsciiString *tokens, Int first, Int count, Real *out )
{
	for (Int i = 0; i < count; ++i)
	{
		if (!parseCinemaNumber( tokens[ first + i ], &out[ i ] ))
			return FALSE;
	}
	return TRUE;
}

Bool CinemaDirector_parseLine( const char *line, CinemaShot *shot, Bool *isShot, AsciiString *reason )
{
	AsciiString tokens[ CINEMA_MAX_TOKENS ];
	const Int count = tokenizeCinemaLine( line, tokens, CINEMA_MAX_TOKENS );
	*isShot = FALSE;
	if (count == 0)
		return TRUE;
	if (count > CINEMA_MAX_TOKENS)
		return refuseCinemaLine( reason, "too many words on the line" );
	if (count < 2)
		return refuseCinemaLine( reason, "a shot is <seconds> <verb> ..." );
	if (!parseCinemaNumber( tokens[ 0 ], &shot->at ) || shot->at < 0.0f)
		return refuseCinemaLine( reason, "the first word is not a time in seconds" );

	shot->seconds = 0.0f;
	shot->values = 0;
	shot->routePoints = 0;
	shot->name.clear();
	shot->on = FALSE;

	const AsciiString &verb = tokens[ 1 ];
	const Int args = count - 2;
	if (verb == "cut")
	{
		shot->verb = CINEMA_VERB_CUT;
		// x y, then as many of zoom, angle, pitch as the line cares to give
		if (args < 2 || args > 5)
			return refuseCinemaLine( reason, "cut <x> <y> [zoom] [angle] [pitch]" );
		Real numbers[ 5 ];
		if (!parseCinemaNumbers( tokens, 2, args, numbers ))
			return refuseCinemaLine( reason, "cut takes numbers" );
		shot->routeX[ 0 ] = numbers[ 0 ];
		shot->routeY[ 0 ] = numbers[ 1 ];
		shot->values = args - 2;
		for (Int i = 0; i < shot->values; ++i)
			shot->value[ i ] = numbers[ 2 + i ];
	}
	else if (verb == "move")
	{
		shot->verb = CINEMA_VERB_MOVE;
		Real numbers[ 3 ];
		if (args != 3 || !parseCinemaNumbers( tokens, 2, 3, numbers ))
			return refuseCinemaLine( reason, "move <x> <y> <seconds>" );
		shot->routeX[ 0 ] = numbers[ 0 ];
		shot->routeY[ 0 ] = numbers[ 1 ];
		shot->routePoints = 1;
		shot->seconds = numbers[ 2 ];
	}
	else if (verb == "route")
	{
		shot->verb = CINEMA_VERB_ROUTE;
		if (args < 3 || (args - 1) % 2 != 0 || (args - 1) / 2 > CINEMA_MAX_ROUTE_POINTS - 1)
			return refuseCinemaLine( reason, "route <seconds> <x> <y> [<x> <y> ...], at most 15 points" );
		if (!parseCinemaNumber( tokens[ 2 ], &shot->seconds ))
			return refuseCinemaLine( reason, "route takes numbers" );
		shot->routePoints = (args - 1) / 2;
		for (Int i = 0; i < shot->routePoints; ++i)
		{
			if (!parseCinemaNumber( tokens[ 3 + 2 * i ], &shot->routeX[ i ] )
					|| !parseCinemaNumber( tokens[ 4 + 2 * i ], &shot->routeY[ i ] ))
				return refuseCinemaLine( reason, "route takes numbers" );
		}
	}
	else if (verb == "zoom" || verb == "angle" || verb == "orbit" || verb == "pitch")
	{
		if (verb == "zoom")
			shot->verb = CINEMA_VERB_ZOOM;
		else if (verb == "angle")
			shot->verb = CINEMA_VERB_ANGLE;
		else if (verb == "orbit")
			shot->verb = CINEMA_VERB_ORBIT;
		else
			shot->verb = CINEMA_VERB_PITCH;
		Real numbers[ 2 ];
		if (args != 2 || !parseCinemaNumbers( tokens, 2, 2, numbers ))
			return refuseCinemaLine( reason, "zoom, angle, orbit and pitch take <amount> <seconds>" );
		shot->value[ 0 ] = numbers[ 0 ];
		shot->values = 1;
		shot->seconds = numbers[ 1 ];
		if (shot->verb == CINEMA_VERB_ZOOM && shot->value[ 0 ] <= 0.0f)
			return refuseCinemaLine( reason, "zoom is a factor above 0" );
		if (shot->verb == CINEMA_VERB_PITCH && fabs( shot->value[ 0 ] ) > CINEMA_PITCH_LIMIT_DEGREES)
			return refuseCinemaLine( reason, "pitch is -36 to 36 degrees" );
	}
	else if (verb == "follow")
	{
		shot->verb = CINEMA_VERB_FOLLOW;
		if (args < 1 || args > 2)
			return refuseCinemaLine( reason, "follow <template> [seconds]" );
		shot->name = tokens[ 2 ];
		if (args == 2 && !parseCinemaNumber( tokens[ 3 ], &shot->seconds ))
			return refuseCinemaLine( reason, "follow's length is a number of seconds" );
	}
	else if (verb == "unfollow" || verb == "shot" || verb == "end")
	{
		if (args != 0)
			return refuseCinemaLine( reason, "unfollow, shot and end take nothing" );
		if (verb == "unfollow")
			shot->verb = CINEMA_VERB_UNFOLLOW;
		else if (verb == "shot")
			shot->verb = CINEMA_VERB_SHOT;
		else
			shot->verb = CINEMA_VERB_END;
	}
	else if (verb == "hud" || verb == "letterbox")
	{
		shot->verb = (verb == "hud") ? CINEMA_VERB_HUD : CINEMA_VERB_LETTERBOX;
		if (args != 1 || !parseCinemaSwitch( tokens[ 2 ], &shot->on ))
			return refuseCinemaLine( reason, "hud and letterbox take on or off" );
	}
	else if (verb == "filter")
	{
		shot->verb = CINEMA_VERB_FILTER;
		if (args != 1 || (tokens[ 2 ] != "bw" && tokens[ 2 ] != "blur" && tokens[ 2 ] != "off"))
			return refuseCinemaLine( reason, "filter takes bw, blur or off" );
		shot->name = tokens[ 2 ];
	}
	else
	{
		return refuseCinemaLine( reason, "unknown verb" );
	}

	if (shot->seconds < 0.0f)
		return refuseCinemaLine( reason, "a length cannot be negative" );
	*isShot = TRUE;
	return TRUE;
}

Real CinemaDirector_ease( Real t )
{
	if (t <= 0.0f)
		return 0.0f;
	if (t >= 1.0f)
		return 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

/** Catmull-Rom through every point, with the ends doubled so the curve starts and stops on them. The
		parameter is split evenly between the spans rather than by their length, which is fine for a route
		of roughly even legs and is written down here so a lopsided one is not a surprise. */
void CinemaDirector_routePoint( const Real *xs, const Real *ys, Int count, Real t, Real *x, Real *y )
{
	if (count == 1 || t <= 0.0f)
	{
		*x = xs[ 0 ];
		*y = ys[ 0 ];
		return;
	}
	if (t >= 1.0f)
	{
		*x = xs[ count - 1 ];
		*y = ys[ count - 1 ];
		return;
	}
	const Real spans = (Real)(count - 1);
	Int span = (Int)(t * spans);
	if (span > count - 2)
		span = count - 2;
	const Real u = t * spans - (Real)span;
	const Int i0 = (span > 0) ? span - 1 : 0;
	const Int i1 = span;
	const Int i2 = span + 1;
	const Int i3 = (span + 2 < count) ? span + 2 : count - 1;
	const Real u2 = u * u;
	const Real u3 = u2 * u;
	*x = 0.5f * ((2.0f * xs[ i1 ]) + (-xs[ i0 ] + xs[ i2 ]) * u
		+ (2.0f * xs[ i0 ] - 5.0f * xs[ i1 ] + 4.0f * xs[ i2 ] - xs[ i3 ]) * u2
		+ (-xs[ i0 ] + 3.0f * xs[ i1 ] - 3.0f * xs[ i2 ] + xs[ i3 ]) * u3);
	*y = 0.5f * ((2.0f * ys[ i1 ]) + (-ys[ i0 ] + ys[ i2 ]) * u
		+ (2.0f * ys[ i0 ] - 5.0f * ys[ i1 ] + 4.0f * ys[ i2 ] - ys[ i3 ]) * u2
		+ (-ys[ i0 ] + 3.0f * ys[ i1 ] - 3.0f * ys[ i2 ] + ys[ i3 ]) * u3);
}

// ------------------------------------------------------------------------------------------------
// the camera
// ------------------------------------------------------------------------------------------------

/** One number moving from one value to another over a stretch of the shot list's clock. */
struct CinemaTween
{
	Real from;
	Real to;
	Real start;
	Real length;

	Real at( Real now ) const
	{
		if (length <= 0.0f)
			return to;
		return from + (to - from) * CinemaDirector_ease( (now - start) / length );
	}

	void go( Real now, Real target, Real seconds )
	{
		from = at( now );
		to = target;
		start = now;
		length = seconds;
	}

	void cut( Real value )
	{
		from = value;
		to = value;
		length = 0.0f;
	}
};

enum CinemaPlaceMode
{
	CINEMA_PLACE_STILL,
	CINEMA_PLACE_ROUTE,
	CINEMA_PLACE_FOLLOW
};

static Bool theCinemaLoaded = FALSE;
static std::vector<CinemaShot> theCinemaShots;
static size_t theCinemaNext = 0;
static Bool theCinemaHudHidden = FALSE;
static Bool theCinemaShowMap = FALSE;	///< the console's hidehud showmap: the radar stays in the bottom left corner
static Bool theCinemaLetterbox = FALSE;
static Bool theCinemaFlying = FALSE;			///< a camera verb has run; until then the player has the camera
static Real theCinemaBaseZoom = 1.0f;
static Real theCinemaStillX = 0.0f;
static Real theCinemaStillY = 0.0f;
static CinemaPlaceMode theCinemaPlaceMode = CINEMA_PLACE_STILL;
static Real theCinemaRouteX[ CINEMA_MAX_ROUTE_POINTS ];
static Real theCinemaRouteY[ CINEMA_MAX_ROUTE_POINTS ];
static Int theCinemaRoutePoints = 0;
static Real theCinemaRouteStart = 0.0f;
static Real theCinemaRouteLength = 0.0f;
static Real theCinemaCentreX = 0.0f;				///< what the framing loop is adding to the look point
static Real theCinemaCentreY = 0.0f;
static ObjectID theCinemaFollowID = INVALID_ID;
static Real theCinemaFollowUntil = 0.0f;		///< 0 is for as long as the next shot allows
static Real theCinemaLastNow = 0.0f;
static CinemaTween theCinemaZoom;
static CinemaTween theCinemaAngle;
static CinemaTween theCinemaPitch;
static UnsignedInt theCinemaSeenFrame = 0;
static UnsignedInt theCinemaSeenFrameAt = 0;
static Bool theCinemaClockStarted = FALSE;
static Real theCinemaClock = 0.0f;
static UnsignedInt theCinemaClockWall = 0;

static Bool cinemaShotIsEarlier( const CinemaShot &left, const CinemaShot &right )
{
	return left.at < right.at;
}

static void loadCinema( void )
{
	theCinemaLoaded = TRUE;

	AsciiString path;
	path.set( CINEMA_DIRECTORY );
	path.concat( TheGlobalData->m_cinemaScript );
	path.concat( CINEMA_EXTENSION );

	// bytes, not text, for the reason loadScenario gives
	File *file = TheFileSystem->openFile( path.str(), File::READ );
	if (file == NULL)
	{
		DEBUG_LOG(("CINEMA: cannot open '%s'; the interface is off and the camera is yours\n", path.str()));
		return;
	}
	const Int fileSize = file->size();
	char *contents = file->readEntireAndClose();
	if (contents == NULL)
	{
		DEBUG_LOG(("CINEMA: '%s' read as nothing\n", path.str()));
		return;
	}

	Int lineNumber = 0;
	Int refused = 0;
	Int at = 0;
	while (at < fileSize)
	{
		Int end = at;
		while (end < fileSize && contents[ end ] != '\n')
			++end;
		char line[ 512 ];
		const Int length = end - at;
		const Int copied = (length < (Int)sizeof( line ) - 1) ? length : (Int)sizeof( line ) - 1;
		memcpy( line, contents + at, copied );
		line[ copied ] = 0;
		++lineNumber;

		CinemaShot shot;
		Bool isShot = FALSE;
		AsciiString reason;
		if (!CinemaDirector_parseLine( line, &shot, &isShot, &reason ))
		{
			DEBUG_LOG(("CINEMA: %s line %d: %s\n", path.str(), lineNumber, reason.str()));
			++refused;
		}
		else if (isShot)
		{
			theCinemaShots.push_back( shot );
		}
		at = end + 1;
	}
	delete [] contents;

	std::stable_sort( theCinemaShots.begin(), theCinemaShots.end(), cinemaShotIsEarlier );
	DEBUG_LOG(("CINEMA: loaded '%s', %d shots, %d lines refused\n", path.str(), (Int)theCinemaShots.size(), refused));
}

/** The shot list's clock in seconds. While a -video range is being recorded it is the logic frame
		alone, because that is what each picture is. Otherwise it runs on the wall clock and is pulled a
		little towards the logic frame every pass. Read straight off the logic frame, plus the wall time
		since that frame arrived, it restarted from nothing whenever a frame came in, stood still at the
		cap when one came in late and leapt when a catch-up ran two at once, and the camera went in
		lurches that turned the stomach of whoever watched the window. */
static Real cinemaNow( void )
{
	const UnsignedInt frame = TheGameLogic->getFrame();
	const UnsignedInt wall = Clock_Milliseconds();
	if (frame != theCinemaSeenFrame)
	{
		theCinemaSeenFrame = frame;
		theCinemaSeenFrameAt = wall;
	}
	const Bool recording = TheGlobalData->m_videoEndFrame > 0;
	const Bool paused = TheGameLogic->isGamePaused();
	Real fraction = 0.0f;
	if (!recording && !paused)
	{
		fraction = (Real)(wall - theCinemaSeenFrameAt) / CINEMA_FRAME_MS;
		if (fraction > 1.0f)
			fraction = 1.0f;
	}
	// recording, the picture's own time: the logic frame, or a share of one before it for a -recordfps
	// picture between two (CinemaDirector_update runs after the logic loop has named the coming picture)
	const Real logicNow = (recording ? GameEngine_pictureFrame() : (Real)frame + fraction) / (Real)LOGICFRAMES_PER_SECOND;

	const Real wallSeconds = (Real)(wall - theCinemaClockWall) / 1000.0f;
	theCinemaClockWall = wall;
	if (recording || !theCinemaClockStarted || logicNow - theCinemaClock > CINEMA_CLOCK_SNAP_SECONDS)
	{
		theCinemaClockStarted = TRUE;
		theCinemaClock = logicNow;
		return theCinemaClock;
	}
	const Real advanced = paused ? theCinemaClock : theCinemaClock + wallSeconds;
	const Real pulled = advanced + (logicNow - advanced) * CINEMA_CLOCK_PULL;
	// a stalled logic frame slows the camera down to a stop; it never winds it back
	if (pulled > theCinemaClock)
		theCinemaClock = pulled;
	return theCinemaClock;
}

static void cinemaPlace( Real now, Real *x, Real *y )
{
	if (theCinemaPlaceMode == CINEMA_PLACE_ROUTE)
	{
		const Real t = (theCinemaRouteLength > 0.0f) ? CinemaDirector_ease( (now - theCinemaRouteStart) / theCinemaRouteLength ) : 1.0f;
		CinemaDirector_routePoint( theCinemaRouteX, theCinemaRouteY, theCinemaRoutePoints, t, x, y );
		return;
	}
	*x = theCinemaStillX;
	*y = theCinemaStillY;
}

/** Freeze wherever the camera is looking right now; the next move starts from there. */
static void cinemaSettle( Real now )
{
	cinemaPlace( now, &theCinemaStillX, &theCinemaStillY );
	theCinemaPlaceMode = CINEMA_PLACE_STILL;
}

static void cinemaTakeTheCamera( void )
{
	if (theCinemaFlying)
		return;
	theCinemaFlying = TRUE;

	Coord3D pos;
	TheTacticalView->getPosition( &pos );
	theCinemaStillX = pos.x;
	theCinemaStillY = pos.y;
	theCinemaBaseZoom = TheTacticalView->getZoom();
	theCinemaZoom.cut( 1.0f );
	theCinemaAngle.cut( TheTacticalView->getAngle() * 180.0f / PI );
	theCinemaPitch.cut( TheTacticalView->getPitch() * 180.0f / PI );

	// the view eases its own zoom towards the ground under it and chases a locked unit; both would
	// fight a camera that is being told exactly where to be every frame
	TheTacticalView->setOkToAdjustHeight( FALSE );
	TheTacticalView->setCameraLock( INVALID_ID );
}

static Object *findNearestOfTemplate( const AsciiString &name, Real x, Real y )
{
	Object *best = NULL;
	Real bestDistance = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->getTemplate()->getName() != name)
			continue;
		const Real dx = obj->getPosition()->x - x;
		const Real dy = obj->getPosition()->y - y;
		const Real distance = dx * dx + dy * dy;
		if (best == NULL || distance < bestDistance)
		{
			best = obj;
			bestDistance = distance;
		}
	}
	return best;
}

static void cinemaEndTheMatch( void )
{
	// the same teardown an unattended run finishes with, and for the same reasons (updateHeadlessRun)
	if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		TheRecorder->stopRecording();
	TheGameLogic->clearGameData( FALSE );
	TheGameEngine->setQuitting( TRUE );
}

static GameWindow *cinemaControlBarWindow( void )
{
	static const NameKeyType id = TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:ControlBarParent" ) );
	return TheWindowManager->winGetWindowFromId( NULL, id );
}

static void cinemaSetHud( Bool hidden )
{
	if (hidden == theCinemaHudHidden)
		return;
	theCinemaHudHidden = hidden;
	if (hidden)
	{
		HideControlBar( TRUE );
		TheInGameUI->deselectAllDrawables();
	}
	else
	{
		ShowControlBar( TRUE );
		TheMouse->setVisibility( TRUE );
	}
}

/** The match puts the control bar up and the letterbox down on its own when loading finishes, which is
		after the first frame this runs on, so what the shot list asked for is re-asserted every pass. */
static void cinemaHoldTheBar( void )
{
	GameWindow *bar = cinemaControlBarWindow();
	if (bar != NULL && !bar->winIsHidden())
		HideControlBar( TRUE );
}

static void cinemaHoldTheFrame( void )
{
	if (theCinemaHudHidden)
	{
		cinemaHoldTheBar();
		// the cursor comes back whenever the game decides it should, so it is put away every pass
		TheMouse->setVisibility( FALSE );
		if (TheInGameUI->getSelectCount() > 0)
			TheInGameUI->deselectAllDrawables();
	}
	if (theCinemaLetterbox != TheDisplay->isLetterBoxed() && !TheDisplay->isLetterBoxFading())
	{
		TheDisplay->setLetterBoxAspect( CINEMA_LETTERBOX_ASPECT );
		TheDisplay->enableLetterBox( theCinemaLetterbox );
	}
}

static void runShot( const CinemaShot &shot )
{
	const Real now = shot.at;
	DEBUG_LOG(("CINEMA: %.2fs verb %d over %.2fs\n", now, (Int)shot.verb, shot.seconds));

	switch (shot.verb)
	{
		case CINEMA_VERB_CUT:
			cinemaTakeTheCamera();
			theCinemaPlaceMode = CINEMA_PLACE_STILL;
			theCinemaFollowID = INVALID_ID;
			theCinemaStillX = shot.routeX[ 0 ];
			theCinemaStillY = shot.routeY[ 0 ];
			if (shot.values > 0)
				theCinemaZoom.cut( shot.value[ 0 ] );
			if (shot.values > 1)
				theCinemaAngle.cut( shot.value[ 1 ] );
			if (shot.values > 2)
				theCinemaPitch.cut( shot.value[ 2 ] );
			break;

		case CINEMA_VERB_MOVE:
		case CINEMA_VERB_ROUTE:
		{
			cinemaTakeTheCamera();
			cinemaSettle( now );
			theCinemaFollowID = INVALID_ID;
			theCinemaRouteX[ 0 ] = theCinemaStillX;
			theCinemaRouteY[ 0 ] = theCinemaStillY;
			theCinemaRoutePoints = 1;
			const Int points = (shot.verb == CINEMA_VERB_MOVE) ? 1 : shot.routePoints;
			for (Int i = 0; i < points; ++i)
			{
				theCinemaRouteX[ theCinemaRoutePoints ] = shot.routeX[ i ];
				theCinemaRouteY[ theCinemaRoutePoints ] = shot.routeY[ i ];
				++theCinemaRoutePoints;
			}
			theCinemaRouteStart = now;
			theCinemaRouteLength = shot.seconds;
			theCinemaPlaceMode = CINEMA_PLACE_ROUTE;
			break;
		}

		case CINEMA_VERB_ZOOM:
			cinemaTakeTheCamera();
			theCinemaZoom.go( now, shot.value[ 0 ], shot.seconds );
			break;

		case CINEMA_VERB_ANGLE:
			cinemaTakeTheCamera();
			theCinemaAngle.go( now, shot.value[ 0 ], shot.seconds );
			break;

		case CINEMA_VERB_ORBIT:
			cinemaTakeTheCamera();
			theCinemaAngle.go( now, theCinemaAngle.to + shot.value[ 0 ], shot.seconds );
			break;

		case CINEMA_VERB_PITCH:
			cinemaTakeTheCamera();
			theCinemaPitch.go( now, shot.value[ 0 ], shot.seconds );
			break;

		case CINEMA_VERB_FOLLOW:
		{
			cinemaTakeTheCamera();
			cinemaSettle( now );
			Object *obj = findNearestOfTemplate( shot.name, theCinemaStillX, theCinemaStillY );
			if (obj == NULL)
			{
				DEBUG_LOG(("CINEMA: follow: no '%s' on the map\n", shot.name.str()));
				break;
			}
			theCinemaFollowID = obj->getID();
			theCinemaFollowUntil = (shot.seconds > 0.0f) ? now + shot.seconds : 0.0f;
			theCinemaPlaceMode = CINEMA_PLACE_FOLLOW;
			theCinemaCentreX = 0.0f;
			theCinemaCentreY = 0.0f;
			break;
		}

		case CINEMA_VERB_UNFOLLOW:
			theCinemaFollowID = INVALID_ID;
			theCinemaPlaceMode = CINEMA_PLACE_STILL;
			break;

		case CINEMA_VERB_HUD:
			cinemaSetHud( !shot.on );
			break;

		case CINEMA_VERB_LETTERBOX:
			theCinemaLetterbox = shot.on;
			break;

		// what ScriptActions::doBlackWhiteMode and doCameraMotionBlur set, with no fade
		case CINEMA_VERB_FILTER:
			if (shot.name == "bw")
			{
				TheTacticalView->setViewFilterMode( FM_VIEW_BW_BLACK_AND_WHITE );
				TheTacticalView->setViewFilter( FT_VIEW_BW_FILTER );
				TheTacticalView->setFadeParameters( 0, 1 );
			}
			else if (shot.name == "blur")
			{
				if (TheTacticalView->setViewFilter( FT_VIEW_MOTION_BLUR_FILTER )
						&& !TheTacticalView->setViewFilterMode( FM_VIEW_MB_IN_ALPHA ))
					TheTacticalView->setViewFilter( FT_NULL_FILTER );
			}
			else
			{
				TheTacticalView->setViewFilterMode( FM_NULL_MODE );
				TheTacticalView->setViewFilter( FT_NULL_FILTER );
			}
			break;

		case CINEMA_VERB_SHOT:
		{
			Coord3D pos;
			TheTacticalView->getPosition( &pos );
			DEBUG_LOG(("CINEMA: shot at %.2fs, looking at (%.0f,%.0f), zoom %.2f, angle %.1f, pitch %.1f\n", now,
				pos.x, pos.y, TheTacticalView->getZoom(), TheTacticalView->getAngle() * 180.0f / PI,
				TheTacticalView->getPitch() * 180.0f / PI));
			TheDisplay->takeScreenShot();
			break;
		}

		case CINEMA_VERB_END:
			cinemaEndTheMatch();
			break;

		default:
			DEBUG_CRASH(("CINEMA: verb %d has no handler", (Int)shot.verb));
			break;
	}
}

/** The camera looks at a point on the ground and the tilt swings the picture around it, so a unit
		standing on that point rides above the middle of the frame, further the steeper the tilt. Rather
		than work the offset out from the projection, the loop measures it: where the frame's middle
		lands in the world at the unit's own height is the point the camera should be looking at, and
		the difference between that and the chased point is taken out of the look point a little at a
		time. It settles in a few frames and stays settled while the tilt or the zoom is still moving.
		It aims at the chased point and not at the unit: aimed at the unit it also took out the chase's
		lag, pinned the camera to the unit and put every 30Hz step and every jink back into the picture. */
static void cinemaCentreOn( Real chasedX, Real chasedY, Real height )
{
	ICoord2D middle;
	middle.x = TheTacticalView->getWidth() / 2;
	middle.y = TheTacticalView->getHeight() / 2;

	Coord3D atMiddle;
	TheTacticalView->screenToWorldAtZ( &middle, &atMiddle, height );

	const Real missX = chasedX - atMiddle.x;
	const Real missY = chasedY - atMiddle.y;
	if (fabs( missX ) > CINEMA_CENTRE_LIMIT || fabs( missY ) > CINEMA_CENTRE_LIMIT)
		return;

	theCinemaCentreX += missX * CINEMA_CENTRE_GAIN;
	theCinemaCentreY += missY * CINEMA_CENTRE_GAIN;
}

/** A followed unit is chased, not pinned: the camera closes a fixed share of the gap every second,
		so a unit that jinks does not shake the picture and one that dies leaves the camera where it was. */
static void cinemaChase( Real now )
{
	const Real elapsed = now - theCinemaLastNow;
	if (theCinemaFollowUntil > 0.0f && now >= theCinemaFollowUntil)
	{
		theCinemaFollowID = INVALID_ID;
		theCinemaPlaceMode = CINEMA_PLACE_STILL;
		return;
	}
	Object *obj = TheGameLogic->findObjectByID( theCinemaFollowID );
	if (obj == NULL || obj->isEffectivelyDead())
	{
		theCinemaFollowID = INVALID_ID;
		theCinemaPlaceMode = CINEMA_PLACE_STILL;
		return;
	}
	const Real share = (elapsed > 0.0f) ? 1.0f - (Real)exp( -CINEMA_FOLLOW_RATE * elapsed ) : 0.0f;
	theCinemaStillX += (obj->getPosition()->x - theCinemaStillX) * share;
	theCinemaStillY += (obj->getPosition()->y - theCinemaStillY) * share;
	cinemaCentreOn( theCinemaStillX, theCinemaStillY, obj->getPosition()->z );
}

void CinemaDirector_setHudHidden( Bool hidden, Bool showMap )
{
	cinemaSetHud( hidden );
	theCinemaShowMap = hidden && showMap;
}

/// the match is over and the next one builds its own bar, so the flag goes without touching the bar
void CinemaDirector_forgetHudHidden( void )
{
	if (TheGlobalData->m_cinemaScript.isEmpty())
	{
		theCinemaHudHidden = FALSE;
		theCinemaShowMap = FALSE;
	}
}

Bool CinemaDirector_isHudHidden( void )
{
	return theCinemaHudHidden;
}

Bool CinemaDirector_showsMap( void )
{
	return theCinemaHudHidden && theCinemaShowMap;
}

void CinemaDirector_update( void )
{
	// the console's hidehud: no shot list, but the match still puts the bar back on its own
	if (TheGlobalData->m_cinemaScript.isEmpty() && theCinemaHudHidden && TheGameLogic->isInGame())
		cinemaHoldTheBar();
	if (TheGlobalData->m_cinemaScript.isEmpty() || TheTacticalView == NULL || TheTerrainLogic == NULL)
		return;
	if (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;

	if (!theCinemaLoaded)
	{
		loadCinema();
		cinemaSetHud( TRUE );
	}

	const Real now = cinemaNow();
	while (theCinemaNext < theCinemaShots.size() && theCinemaShots[ theCinemaNext ].at <= now)
	{
		runShot( theCinemaShots[ theCinemaNext ] );
		++theCinemaNext;
		if (TheGameEngine->getQuitting())
			return;
	}

	cinemaHoldTheFrame();

	if (theCinemaFlying)
	{
		if (theCinemaPlaceMode == CINEMA_PLACE_FOLLOW)
			cinemaChase( now );
		Coord3D look;
		cinemaPlace( now, &look.x, &look.y );
		if (theCinemaPlaceMode == CINEMA_PLACE_FOLLOW)
		{
			look.x += theCinemaCentreX;
			look.y += theCinemaCentreY;
		}
		look.z = TheTerrainLogic->getGroundHeight( look.x, look.y );
		TheTacticalView->lookAt( &look );
		TheTacticalView->setZoom( theCinemaBaseZoom * theCinemaZoom.at( now ) );
		TheTacticalView->setAngle( theCinemaAngle.at( now ) * PI / 180.0f );
		TheTacticalView->setPitch( theCinemaPitch.at( now ) * PI / 180.0f );
	}
	theCinemaLastNow = now;
}

Bool CinemaDirector_hidesHud( void )
{
	// the console's freecam is a photo mode, and the interface is the first thing out of a photo
	return theCinemaHudHidden || (TheTacticalView != NULL && TheTacticalView->isFreeCamera());
}
