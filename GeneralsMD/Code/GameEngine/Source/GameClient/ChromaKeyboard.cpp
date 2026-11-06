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

// FILE: ChromaKeyboard.cpp ///////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"

#if defined(_WIN32)
#include <wininet.h>
#endif
#include <math.h>

#include "GameClient/ChromaKeyboard.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/HotKey.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MetaEvent.h"
#include "Common/Energy.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/SpecialPowerType.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameNetwork/NetworkInterface.h"

//-----------------------------------------------------------------------------
// The devices, and where each one's cells sit in the single flat array the main
// thread fills and the worker sends.  The keyboard grid is fixed at six rows of
// twenty-two whatever board is underneath, the mouse at nine of seven, and the
// mousepad is one strip of fifteen.
//-----------------------------------------------------------------------------
struct ChromaDevice
{
	const char *endpoint;
	const char *effect;
	Int rows;
	Int columns;
	Int firstCell;
};

static const Int KEYBOARD_ROWS = 6;
static const Int KEYBOARD_COLUMNS = 22;
static const Int KEYBOARD_CELLS = KEYBOARD_ROWS * KEYBOARD_COLUMNS;
static const Int MOUSE_CELLS = 9 * 7;
static const Int MOUSEPAD_CELLS = 15;

static const Int KEYBOARD_FIRST_CELL = 0;
static const Int MOUSE_FIRST_CELL = KEYBOARD_CELLS;
static const Int MOUSEPAD_FIRST_CELL = MOUSE_FIRST_CELL + MOUSE_CELLS;
static const Int CHROMA_CELLS = MOUSEPAD_FIRST_CELL + MOUSEPAD_CELLS;

static const ChromaDevice CHROMA_DEVICES[] =
{
	{ "keyboard", "CHROMA_CUSTOM",  KEYBOARD_ROWS, KEYBOARD_COLUMNS, KEYBOARD_FIRST_CELL },
	// The mouse takes CUSTOM2, which is the nine by seven grid; plain CUSTOM on a
	// mouse is the old seven-lamp effect and ignores most of the device.
	{ "mouse",    "CHROMA_CUSTOM2", 9,             7,                MOUSE_FIRST_CELL    },
	{ "mousepad", "CHROMA_CUSTOM",  1,             MOUSEPAD_CELLS,   MOUSEPAD_FIRST_CELL },
};
static const Int CHROMA_DEVICE_COUNT = sizeof( CHROMA_DEVICES ) / sizeof( CHROMA_DEVICES[ 0 ] );

/* The WinINet transport - these constants, the worker hand-off below, and the HTTP side further
	 down - is Windows-only: off Windows nothing talks to the Chroma server, so there is no hardware to
	 feed and updateChromaKeyboard returns at once.  The mappings and the frame composition are
	 portable and built everywhere; test_gameengine checks the mappings. */
#if defined(_WIN32)
static const char *CHROMA_HOST = "localhost";
static const INTERNET_PORT CHROMA_PORT = 54235;
static const char *CHROMA_INIT_PATH = "/razer/chromasdk";
static const char *CHROMA_INIT_BODY =
	"{\"title\":\"Zero Hour Reforged\","
	"\"description\":\"The state of the match on the hardware\","
	"\"author\":{\"name\":\"Zero Hour Reforged\",\"contact\":\"https://github.com/olcayseygan/CnCGeneralsZH-Reforged\"},"
	"\"device_supported\":[\"keyboard\",\"mouse\",\"mousepad\"],"
	"\"category\":\"application\"}";

static const DWORD CHROMA_TIMEOUT_MS = 500;
/// Thirty a second, which is the rate the logic frame advances at and so the
/// rate the lighting can actually change at.  At ten a second a still board cost
/// nothing either, because of the dirty check below, but a superweapon ripple
/// crossing the keyboard stuttered: it had ten steps to cross it in.
static const DWORD CHROMA_SEND_INTERVAL_MS = 33;
/// The session dies after about ten idle seconds - measured, not assumed - so a
/// still frame is resent well inside that.
static const DWORD CHROMA_KEEPALIVE_MS = 4000;
#endif

//-----------------------------------------------------------------------------
// Colours, packed the way the device takes them: blue in the high byte, red in
// the low one.
//-----------------------------------------------------------------------------
static const Int COLOR_OFF = 0x000000;
static const Int COLOR_GREEN = 0x00FF00;
static const Int COLOR_YELLOW = 0x00FFFF;
static const Int COLOR_RED = 0x0000FF;
static const Int COLOR_AMBER = 0x0080FF;
static const Int COLOR_WHITE = 0xFFFFFF;
static const Int COLOR_WARM_WHITE = 0x78DCFF;
static const Int COLOR_BLUE = 0xFF0000;
static const Int COLOR_GOLD = 0x00D7FF;

//-----------------------------------------------------------------------------
// The keyboard grid, zone by zone.  Column zero is the strip down the left of
// the board and column one is escape, tab, caps and shift, so the typing rows
// start at two, except Z's, which starts at three behind the ISO key.  Row zero
// is the function keys.
//-----------------------------------------------------------------------------
static const char *CHROMA_KEY_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const Int CHROMA_KEY_ROW_COUNT = 4;
static const Int CHROMA_KEY_FIRST_COLUMNS[ CHROMA_KEY_ROW_COUNT ] = { 2, 2, 2, 3 };
static const Int CHROMA_POWER_ROW = 0;
static const Int CHROMA_KEY_FIRST_ROW = 1;

static const Int MATCH_STATE_ROW = 0;
static const Int MATCH_STATE_COLUMN = 1;			///< escape

static const Int FKEY_ROW = 0;
static const Int FKEY_FIRST_COLUMN = 3;
static const Int FKEY_COUNT = 12;

static const Int STATUS_ROW = 0;
static const Int STATUS_NETWORK_COLUMN = 15;	///< print screen
static const Int STATUS_RADAR_COLUMN = 16;		///< scroll lock
static const Int STATUS_PAUSED_COLUMN = 17;		///< pause

/// The six lamps of the navigation cluster, insert to page down.
static const Int ALERT_FIRST_COLUMN = 15;
static const Int ALERT_TOP_ROW = 1;
static const Int ALERT_BOTTOM_ROW = 2;
static const Int ALERTS_PER_ROW = 3;

static const Int NUMPAD_FIRST_ROW = 1;
static const Int NUMPAD_LAST_ROW = 5;
static const Int NUMPAD_FIRST_COLUMN = 18;
static const Int NUMPAD_LAST_COLUMN = 21;
static const Int NUMPAD_CELLS = (NUMPAD_LAST_ROW - NUMPAD_FIRST_ROW + 1)
															* (NUMPAD_LAST_COLUMN - NUMPAD_FIRST_COLUMN + 1);

static const Int SELECTION_HEALTH_ROW = 4;		///< the up arrow
static const Int SELECTION_HEALTH_COLUMN = 16;
static const Int SELECTION_RANK_ROW = 5;			///< the down arrow
static const Int SELECTION_RANK_COLUMN = 16;

/// The addressable keys along the bottom row, skipping the ones the grid does
/// not give a lamp of their own.
static const Int PRODUCTION_ROW = 5;
static const Int PRODUCTION_COLUMNS[] = { 1, 2, 3, 7, 11, 12, 13, 14 };
static const Int PRODUCTION_LAMPS = sizeof( PRODUCTION_COLUMNS ) / sizeof( PRODUCTION_COLUMNS[ 0 ] );

//-----------------------------------------------------------------------------
static const Real AMBIENT_SCALE = 0.22f;			///< the unlit bed of player colour
static const Real IDLE_PRODUCER_SCALE = 0.15f;	///< a factory that is building nothing
static const Int UNDER_ATTACK_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Real UNDER_ATTACK_PULSE_HZ = 2.5f;
/// The alarm keeps to the hardware that carries nothing to read: the mouse, which
/// is under the hand and the hardest thing on the desk to miss, and the strip
/// down the left edge of the board.  Washing the keys with it as well buried
/// every one of them under red for four seconds at the exact moment the player
/// most needs to read the bar, which is the moment the base is being shelled.
static const Real ALARM_DEPTH = 0.85f;
/// The column outside escape, tab, caps and shift, which no gauge and no key of
/// the command bar lives in.
static const Int ALARM_STRIP_COLUMN = 0;

/// Half of this many frames lit, half dark.
static const Int BLINK_PERIOD_FRAMES = ConvertRetailFrames( 16 );
/// A power announces itself by blinking and then holds steady.  A key that blinks
/// for the rest of the match stops being news and starts being an irritation.
static const Int READY_BLINK_FRAMES = LOGICFRAMES_PER_SECOND * 3;
/// The general's star on screen flashes on a one second cycle; the promotion
/// lamp keeps to the same one rather than inventing a second rhythm.
static const Int STAR_PERIOD_FRAMES = LOGICFRAMES_PER_SECOND;
static const Int ALERT_HOLD_FRAMES = LOGICFRAMES_PER_SECOND * 3;
/// Walking every object the player owns is not a per-frame job, and neither a
/// build clock nor a superweapon's charge moves fast enough to need one.
static const Int WALK_INTERVAL_FRAMES = ConvertRetailFrames( 15 );

static const UnsignedInt MONEY_PER_SEGMENT = 1000;

//-----------------------------------------------------------------------------
// The main thread writes s_pendingCells, the worker reads it.  One lock over the
// whole array: it is 840 bytes copied ten times a second.
//-----------------------------------------------------------------------------
#if defined(_WIN32)
static CRITICAL_SECTION s_cellLock;
static Int s_pendingCells[ CHROMA_CELLS ];
static Bool s_workerRunning = FALSE;
#endif
static Bool s_disabled = FALSE;
#if defined(_WIN32)
static volatile LONG s_workerShouldStop = 0;
/// Whether the hardware is ours right now: the option is on and a match is being
/// played.  Anywhere else the session is closed and Synapse lights the board.
static volatile LONG s_sessionWanted = 0;
static HANDLE s_workerThread = NULL;

/// How long the worker waits before asking an absent Chroma server again.
static const DWORD CHROMA_RETRY_MS = 5000;
#endif

//-----------------------------------------------------------------------------
// Pure helpers.
//-----------------------------------------------------------------------------
Int chromaCellForKey( char key )
{
	for( Int row = 0; row < CHROMA_KEY_ROW_COUNT; ++row )
	{
		const char *keys = CHROMA_KEY_ROWS[ row ];
		for( Int column = 0; keys[ column ] != 0; ++column )
		{
			if( keys[ column ] == key )
				return (CHROMA_KEY_FIRST_ROW + row) * KEYBOARD_COLUMNS
						 + CHROMA_KEY_FIRST_COLUMNS[ row ] + column;
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
Int chromaPowerSegments( Int production, Int consumption )
{
	if( production <= 0 )
		return 0;
	if( production < consumption )
		return CHROMA_POWER_BROWNOUT;

	const Real headroom = 1.0f - (Real)consumption / (Real)production;
	const Int segments = (Int)(headroom * CHROMA_POWER_SEGMENTS + 0.999f);
	return segments > CHROMA_POWER_SEGMENTS ? CHROMA_POWER_SEGMENTS : segments;
}

//-----------------------------------------------------------------------------
Int chromaBarSegments( Real fraction, Int segments )
{
	if( fraction <= 0.0f )
		return 0;
	const Int lit = (Int)(fraction * segments + 0.999f);
	return lit > segments ? segments : lit;
}

//-----------------------------------------------------------------------------
Int chromaMoneySegments( UnsignedInt money, Int segments )
{
	const UnsignedInt lit = money / MONEY_PER_SEGMENT;
	return lit > (UnsignedInt)segments ? segments : (Int)lit;
}

//-----------------------------------------------------------------------------
static Int chromaChannel( Real level )
{
	if( level <= 0.0f )
		return 0;
	if( level >= 1.0f )
		return 255;
	return (Int)(level * 255.0f + 0.5f);
}

//-----------------------------------------------------------------------------
static Int chromaColor( Real red, Real green, Real blue )
{
	// Clamped, because an effect's flicker swings a channel past one on purpose
	// and an unclamped byte would carry over into the colour next to it.
	const Int r = chromaChannel( red );
	const Int g = chromaChannel( green );
	const Int b = chromaChannel( blue );
	// Chroma wants BGR, not the RGB the rest of the engine speaks.
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
static Int chromaScale( Int color, Real scale )
{
	const Int b = (Int)(((color >> 16) & 0xFF) * scale);
	const Int g = (Int)(((color >> 8) & 0xFF) * scale);
	const Int r = (Int)((color & 0xFF) * scale);
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
/** Drag a colour towards red by the alarm depth, keeping whatever of it survives. */
static Int chromaAlarmed( Int color, Real alarm )
{
	if( alarm <= 0.0f )
		return color;

	const Real keep = 1.0f - alarm;
	const Int b = (Int)(((color >> 16) & 0xFF) * keep);
	const Int g = (Int)(((color >> 8) & 0xFF) * keep);
	Int r = (Int)((color & 0xFF) * keep + alarm * 255.0f);
	if( r > 255 )
		r = 255;
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
static Bool chromaBlinkIsOn( UnsignedInt frame, Int periodFrames )
{
	return ((frame / (periodFrames / 2)) & 1) == 0;
}

//-----------------------------------------------------------------------------
static Int chromaKeyboardCell( Int row, Int column )
{
	return row * KEYBOARD_COLUMNS + column;
}

//-----------------------------------------------------------------------------
/** Ready holds the eye for a few seconds and then stops asking for it. */
static Int chromaReadyColor( UnsignedInt frame, UnsignedInt readySince, Int color )
{
	if( readySince == 0 || frame < readySince )
		return color;
	if( frame - readySince >= (UnsignedInt)READY_BLINK_FRAMES )
		return color;
	return chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? color : COLOR_OFF;
}

/// When the first superweapon came ready, so the numpad can announce it and then
/// stop shouting.  Zero means none is.
static UnsignedInt s_superweaponReadySince = 0;

//-----------------------------------------------------------------------------
// The superweapon effects.  A launch takes the whole board for a few seconds:
// nothing else on it matters while a nuke is in the air.
//-----------------------------------------------------------------------------
enum ChromaEffect
{
	EFFECT_NONE = 0,
	EFFECT_NUKE,
	EFFECT_LASER,
	EFFECT_SCUD
};

static const Int NUKE_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Int LASER_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 9 / 2;
static const Int SCUD_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 5;

// Every distance below is in half-widths of the device, measured from its top
// left lamp: a device is two across, and a keyboard's far corner is a little
// over two away.  The owner tuned all three of these by eye on the hardware,
// through a preview that runs the same maths; the numbers are his.

/// Left lamp to right lamp, in those units.
static const Real ACROSS_EXTENT = 2.0f;

// The nuke.  A thin white shock goes out first and fast.  Behind it comes the
// body, a long wave in three bands from its front to its back: red, a longer
// stretch of white, then orange.  The colours breathe.
static const Real NUKE_SHOCK_SPEED = 3.5f;
static const Real NUKE_SHOCK_LENGTH = 0.40f;
static const Real NUKE_BODY_SPEED = 1.3f;
static const Real NUKE_RED_LENGTH = 0.9f;
static const Real NUKE_WHITE_LENGTH = 1.1f;
static const Real NUKE_ORANGE_LENGTH = 0.8f;
static const Real NUKE_BAND_BLEND = 0.25f;			///< how much of the body a band change is spread over
static const Real NUKE_LEAD_EDGE = 0.15f;			///< how quickly the front of the body comes up
static const Real NUKE_TAIL_EDGE = 0.5f;				///< and how slowly its back goes down
static const Real NUKE_RED_GREEN = 0.05f;			///< the green in the red band
static const Real NUKE_ORANGE_GREEN = 0.45f;		///< and in the orange one
static const Real NUKE_NOISE = 0.5f;						///< total swing in brightness
static const Real NUKE_HUE_NOISE = 0.35f;			///< total swing in hue
static const Real NUKE_END_FADE_FROM = 0.88f;		///< of the effect's length; out cleanly after this

// The particle cannon.  The beam comes down on the ground, so what the board
// shows is the spot it lands on: a white point that charges in the top left
// corner, wanders the keyboard, throws blue rings off itself as it goes, and
// leaves a blue fire burning down along where it has been.
static const Real LASER_CHARGE_SECONDS = 0.4f;
static const Real LASER_ROAM_SECONDS = 3.0f;
/// Half swings across and down over the roam.  Different, so the two never line
/// up and the point covers the board without retracing itself.
static const Real LASER_PASSES_ACROSS = 3.0f;
static const Real LASER_PASSES_DOWN = 5.0f;
static const Real LASER_CORE_RADIUS = 0.16f;
static const Real LASER_GLOW_RADIUS = 0.50f;
static const Real LASER_GLOW_LEVEL = 0.7f;
static const Int LASER_TRAIL_SAMPLES = 12;
static const Real LASER_TRAIL_SECONDS = 1.0f;
static const Real LASER_TRAIL_RADIUS = 0.30f;
static const Real LASER_SPRAY_EVERY = 0.12f;
static const Real LASER_SPRAY_LIFE = 0.45f;
static const Int LASER_SPRAY_ALIVE = 5;					///< more than life over interval, so none is dropped early
static const Real LASER_SPRAY_SPEED = 1.5f;
static const Real LASER_SPRAY_LENGTH = 0.22f;
static const Real LASER_SPRAY_REACH = 0.70f;

// The scud storm.  Nine missiles, which is what the building fires, landing one
// after another in no particular place.  Each landing is a flash, a green ring
// running out from it, and a pool of something left on the ground.
static const Int SCUD_COUNT = 9;
static const Real SCUD_FIRST_LANDING = 0.15f;
static const Real SCUD_LIFE = 1.0f;
static const Real SCUD_LANDING_JITTER = 0.10f;
/// Landings keep this far off the left and right edges, as a fraction of the width.
static const Real SCUD_EDGE_MARGIN = 0.08f;
static const Real SCUD_FLASH_SECONDS = 0.18f;
static const Real SCUD_FLASH_RADIUS = 0.35f;
static const Real SCUD_RING_SPEED = 1.1f;
static const Real SCUD_RING_LENGTH = 0.30f;
static const Real SCUD_REACH = 1.0f;
static const Real SCUD_POOL_RADIUS = 0.45f;
static const Real SCUD_POOL_LEVEL = 0.35f;

static ChromaEffect s_effect = EFFECT_NONE;
static UnsignedInt s_effectStartFrame = 0;
static UnsignedInt s_effectSeed = 0;

//-----------------------------------------------------------------------------
/** A cheap integer hash, for the flame's flicker and the scud's landing spots.
	* Not the game's random number generator: that one is logic state, and drawing
	* from it here would desync every machine that has a keyboard. */
static UnsignedInt chromaHash( UnsignedInt value )
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

//-----------------------------------------------------------------------------
static Real chromaHashUnit( UnsignedInt value )
{
	return (Real)(chromaHash( value ) & 0xFFFF) / 65535.0f;
}

//-----------------------------------------------------------------------------
static Int chromaEffectFrames( ChromaEffect effect )
{
	switch( effect )
	{
		case EFFECT_NUKE:		return NUKE_EFFECT_FRAMES;
		case EFFECT_LASER:	return LASER_EFFECT_FRAMES;
		case EFFECT_SCUD:		return SCUD_EFFECT_FRAMES;
		default:						return 0;
	}
}

//-----------------------------------------------------------------------------
/** Several lights falling on one lamp.  The brightest wins channel by channel,
	* which is what lets a white point sit on a blue fire without going grey. */
struct ChromaLight
{
	Real red;
	Real green;
	Real blue;
};

//-----------------------------------------------------------------------------
static void chromaLighten( ChromaLight *light, Real red, Real green, Real blue )
{
	if( red > light->red )
		light->red = red;
	if( green > light->green )
		light->green = green;
	if( blue > light->blue )
		light->blue = blue;
}

//-----------------------------------------------------------------------------
/** One wave rather than a train of them: a single crest trailing the front, dark
	* ahead of it and dark again once it has gone by. */
static Real chromaSingleWave( Real distance, Real radius, Real length, Real fadeAt )
{
	const Real offset = radius - distance;
	if( offset < 0.0f || offset > length || distance >= fadeAt )
		return 0.0f;

	const Real crest = 0.5f - 0.5f * cosf( offset / length * TWO_PI );
	return crest * (1.0f - distance / fadeAt);
}

//-----------------------------------------------------------------------------
static Bool chromaEffectIsLive( UnsignedInt frame )
{
	if( s_effect == EFFECT_NONE )
		return FALSE;

	const Int duration = chromaEffectFrames( s_effect );
	if( frame < s_effectStartFrame || frame - s_effectStartFrame >= (UnsignedInt)duration )
	{
		s_effect = EFFECT_NONE;
		return FALSE;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------
/** A lamp and a moment, as every effect needs them.  The position is in
	* half-widths from the top left lamp, so one set of maths covers a keyboard, a
	* mouse and a fifteen lamp strip without knowing what shape any of them is. */
struct ChromaEffectSample
{
	Real across;
	Real down;
	Real downExtent;		///< top lamp to bottom lamp, in the same units
	Int cellIndex;
	UnsignedInt frame;	///< frames since the launch
	Real elapsed;				///< the same, in seconds
	Real life;					///< the same, as a fraction of the effect's length
};

//-----------------------------------------------------------------------------
static Real chromaDistance( Real dx, Real dy )
{
	return (Real)sqrt( dx * dx + dy * dy );
}

//-----------------------------------------------------------------------------
/** Flicker for one lamp, stepped every so many frames rather than every frame,
	* so it shimmers instead of buzzing.  The salt keeps two flickers on one lamp
	* from moving together. */
static Real chromaFlicker( const ChromaEffectSample &sample, UnsignedInt framesPerStep, UnsignedInt salt )
{
	return chromaHashUnit( (UnsignedInt)sample.cellIndex * 2654435761u
												 + (sample.frame / LOGICFRAMES_PER_RETAIL_FRAME / framesPerStep) * 7919u + salt );	// steps are retail 30 Hz frames
}

//-----------------------------------------------------------------------------
/** The blue fire the cannon leaves behind it: dark blue through the body, cyan
	* where it is hottest, and no red anywhere. */
static void chromaLightenWithBlueFire( ChromaLight *light, Real body )
{
	chromaLighten( light, body * body * body * 0.25f, body * body * 0.75f, body );
}

//-----------------------------------------------------------------------------
static void chromaNukeLight( const ChromaEffectSample &sample, ChromaLight *light )
{
	const Real distance = chromaDistance( sample.across, sample.down );

	// The body.  How far behind its front this lamp sits decides which band it is
	// in: red at the front, then white, then orange at the back.
	const Real bodyLength = NUKE_RED_LENGTH + NUKE_WHITE_LENGTH + NUKE_ORANGE_LENGTH;
	const Real behind = sample.elapsed * NUKE_BODY_SPEED - distance;
	if( behind > 0.0f && behind < bodyLength )
	{
		const Real redEnd = NUKE_RED_LENGTH;
		const Real whiteEnd = NUKE_RED_LENGTH + NUKE_WHITE_LENGTH;
		const Real halfBlend = NUKE_BAND_BLEND * 0.5f;

		Real bandGreen;
		Real bandBlue;
		if( behind < redEnd - halfBlend )
		{
			bandGreen = NUKE_RED_GREEN;
			bandBlue = 0.0f;
		}
		else if( behind < redEnd + halfBlend )
		{
			const Real toWhite = (behind - (redEnd - halfBlend)) / NUKE_BAND_BLEND;
			bandGreen = NUKE_RED_GREEN + toWhite * (1.0f - NUKE_RED_GREEN);
			bandBlue = toWhite;
		}
		else if( behind < whiteEnd - halfBlend )
		{
			bandGreen = 1.0f;
			bandBlue = 1.0f;
		}
		else if( behind < whiteEnd + halfBlend )
		{
			const Real toOrange = (behind - (whiteEnd - halfBlend)) / NUKE_BAND_BLEND;
			bandGreen = 1.0f - toOrange * (1.0f - NUKE_ORANGE_GREEN);
			bandBlue = 1.0f - toOrange;
		}
		else
		{
			bandGreen = NUKE_ORANGE_GREEN;
			bandBlue = 0.0f;
		}

		Real lead = behind / NUKE_LEAD_EDGE;
		if( lead > 1.0f )
			lead = 1.0f;
		Real tail = (bodyLength - behind) / NUKE_TAIL_EDGE;
		if( tail > 1.0f )
			tail = 1.0f;

		// Brightness breathes on one flicker and the hue on another.  The hue one
		// pushes green and blue opposite ways, so white wanders warm and cool
		// instead of just getting dimmer.
		const Real noise = 1.0f - NUKE_NOISE * 0.5f + NUKE_NOISE * chromaFlicker( sample, 2, 0 );
		const Real hue = 1.0f - NUKE_HUE_NOISE * 0.5f + NUKE_HUE_NOISE * chromaFlicker( sample, 3, 40503u );
		const Real level = lead * tail * noise;
		chromaLighten( light, level, bandGreen * level * hue, bandBlue * level * (2.0f - hue) );
	}

	// The shock: thin, white, fast, out in front of all of it.  The fade distance
	// is past any corner, so it crosses at full strength.
	static const Real SHOCK_NEVER_FADES = 99.0f;
	const Real shock = chromaSingleWave( distance, sample.elapsed * NUKE_SHOCK_SPEED,
																			 NUKE_SHOCK_LENGTH, SHOCK_NEVER_FADES );
	chromaLighten( light, shock, shock, shock );

	if( sample.life > NUKE_END_FADE_FROM )
	{
		const Real ending = 1.0f - (sample.life - NUKE_END_FADE_FROM) / (1.0f - NUKE_END_FADE_FROM);
		light->red *= ending;
		light->green *= ending;
		light->blue *= ending;
	}
}

//-----------------------------------------------------------------------------
/** Where the cannon's spot is this long into its roam.  It starts in the top left
	* corner and swings across and down at different rates. */
static void chromaLaserPoint( Real roamTime, Real downExtent, Real *across, Real *down )
{
	Real along = roamTime / LASER_ROAM_SECONDS;
	if( along < 0.0f )
		along = 0.0f;
	if( along > 1.0f )
		along = 1.0f;

	const Real halfTurn = TWO_PI * 0.5f;
	*across = ACROSS_EXTENT * (0.5f - 0.5f * cosf( along * halfTurn * LASER_PASSES_ACROSS ));
	*down = downExtent * (0.5f - 0.5f * cosf( along * halfTurn * LASER_PASSES_DOWN ));
}

//-----------------------------------------------------------------------------
static void chromaLaserLight( const ChromaEffectSample &sample, ChromaLight *light )
{
	const Real roamTime = sample.elapsed - LASER_CHARGE_SECONDS;
	Real pointAcross;
	Real pointDown;

	if( roamTime > 0.0f )
	{
		// Where the point has been is on fire.  The path has no inverse, so it is
		// walked backwards a twelfth of a second at a time and the hottest sample
		// near this lamp wins.
		Real hottest = 0.0f;
		for( Int step = 1; step <= LASER_TRAIL_SAMPLES; ++step )
		{
			const Real back = (Real)step * (LASER_TRAIL_SECONDS / (Real)LASER_TRAIL_SAMPLES);
			const Real at = roamTime - back;
			if( at < 0.0f )
				break;
			if( at > LASER_ROAM_SECONDS )
				continue;

			chromaLaserPoint( at, sample.downExtent, &pointAcross, &pointDown );
			const Real heat = (1.0f - chromaDistance( sample.across - pointAcross, sample.down - pointDown )
																/ LASER_TRAIL_RADIUS)
											* (1.0f - back / LASER_TRAIL_SECONDS);
			if( heat > hottest )
				hottest = heat;
		}
		if( hottest > 0.0f )
			chromaLightenWithBlueFire( light, hottest * (0.45f + 0.55f * chromaFlicker( sample, 2, 0 )) );

		// The blue it throws off: a small ring born at the point every so often,
		// left where it was born while the point moves on.
		const Real emitting = roamTime < LASER_ROAM_SECONDS ? roamTime : LASER_ROAM_SECONDS;
		const Int newest = (Int)(emitting / LASER_SPRAY_EVERY);
		for( Int back = 0; back < LASER_SPRAY_ALIVE && back <= newest; ++back )
		{
			const Int ring = newest - back;
			const Real born = (Real)ring * LASER_SPRAY_EVERY;
			const Real age = roamTime - born;
			if( age < 0.0f || age >= LASER_SPRAY_LIFE )
				continue;

			chromaLaserPoint( born, sample.downExtent, &pointAcross, &pointDown );
			// One spark per lamp per ring, held for the ring's life, so a ring is
			// rough round its edge without its roughness crawling.
			const Real spark = chromaHashUnit( (UnsignedInt)sample.cellIndex * 40503u
																				 + (UnsignedInt)ring * 7919u );
			const Real spray = chromaSingleWave( chromaDistance( sample.across - pointAcross,
																													 sample.down - pointDown ),
																					 age * LASER_SPRAY_SPEED, LASER_SPRAY_LENGTH,
																					 LASER_SPRAY_REACH )
											 * (1.0f - age / LASER_SPRAY_LIFE) * (0.6f + 0.4f * spark);
			chromaLighten( light, spray * 0.15f, spray * 0.6f, spray );
		}
	}

	if( roamTime < LASER_ROAM_SECONDS )
	{
		// The point itself: coming up to strength in the corner, then off round
		// the board with a blue glow about it.
		const Real strength = roamTime <= 0.0f ? sample.elapsed / LASER_CHARGE_SECONDS : 1.0f;
		chromaLaserPoint( roamTime, sample.downExtent, &pointAcross, &pointDown );
		const Real off = chromaDistance( sample.across - pointAcross, sample.down - pointDown );

		const Real glow = (1.0f - off / LASER_GLOW_RADIUS) * LASER_GLOW_LEVEL * strength;
		chromaLighten( light, glow * 0.1f, glow * 0.4f, glow );

		const Real core = (1.0f - off / LASER_CORE_RADIUS) * strength
										* (0.8f + 0.2f * chromaFlicker( sample, 1, 40503u ));
		chromaLighten( light, core, core, core );
	}
}

//-----------------------------------------------------------------------------
static void chromaScudLight( const ChromaEffectSample &sample, ChromaLight *light )
{
	const Real effectSeconds = (Real)SCUD_EFFECT_FRAMES / (Real)LOGICFRAMES_PER_SECOND;
	const Real spacing = (effectSeconds - SCUD_LIFE - SCUD_FIRST_LANDING) / (Real)(SCUD_COUNT - 1);

	for( Int scud = 0; scud < SCUD_COUNT; ++scud )
	{
		// One after another, each nudged off its slot so the salvo is a bombardment
		// and not a metronome.
		const UnsignedInt scudSeed = s_effectSeed + (UnsignedInt)scud * 0x9e3779b9u;
		const Real landing = SCUD_FIRST_LANDING + (Real)scud * spacing
											 + (chromaHashUnit( scudSeed + 2 ) - 0.5f) * 2.0f * SCUD_LANDING_JITTER;
		const Real age = sample.elapsed - landing;
		if( age < 0.0f || age >= SCUD_LIFE )
			continue;

		const Real targetAcross = (SCUD_EDGE_MARGIN + (1.0f - 2.0f * SCUD_EDGE_MARGIN) * chromaHashUnit( scudSeed ))
														* ACROSS_EXTENT;
		const Real targetDown = chromaHashUnit( scudSeed + 1 ) * sample.downExtent;
		const Real distance = chromaDistance( sample.across - targetAcross, sample.down - targetDown );
		const Real dying = 1.0f - age / SCUD_LIFE;

		if( age < SCUD_FLASH_SECONDS )
		{
			const Real flash = (1.0f - age / SCUD_FLASH_SECONDS) * (1.0f - distance / SCUD_FLASH_RADIUS);
			chromaLighten( light, flash * 0.8f, flash, flash * 0.8f );
		}

		const Real ring = chromaSingleWave( distance, age * SCUD_RING_SPEED, SCUD_RING_LENGTH, SCUD_REACH )
										* dying;
		chromaLighten( light, ring * 0.1f, ring, ring * 0.15f );

		// What is left on the ground, stirring a little as it thins out.
		const Real pool = (1.0f - distance / SCUD_POOL_RADIUS) * SCUD_POOL_LEVEL * dying
										* (0.7f + 0.6f * chromaFlicker( sample, 3, 0 ));
		chromaLighten( light, pool * 0.05f, pool * 0.8f, pool * 0.1f );
	}
}

//-----------------------------------------------------------------------------
static Int chromaEffectColor( Real across, Real down, Real downExtent, Int cellIndex,
															UnsignedInt frame )
{
	ChromaEffectSample sample;
	sample.across = across;
	sample.down = down;
	sample.downExtent = downExtent;
	sample.cellIndex = cellIndex;
	sample.frame = frame - s_effectStartFrame;
	sample.elapsed = (Real)sample.frame / (Real)LOGICFRAMES_PER_SECOND;
	sample.life = (Real)sample.frame / (Real)chromaEffectFrames( s_effect );

	ChromaLight light = { 0.0f, 0.0f, 0.0f };
	switch( s_effect )
	{
		case EFFECT_NUKE:		chromaNukeLight( sample, &light ); break;
		case EFFECT_LASER:	chromaLaserLight( sample, &light ); break;
		case EFFECT_SCUD:		chromaScudLight( sample, &light ); break;
		default:						break;
	}
	return chromaColor( light.red, light.green, light.blue );
}

//-----------------------------------------------------------------------------
void chromaSuperweaponLaunched( Int specialPowerType )
{
	ChromaEffect effect = EFFECT_NONE;
	switch( specialPowerType )
	{
		case SPECIAL_PARTICLE_UPLINK_CANNON:
		case SUPW_SPECIAL_PARTICLE_UPLINK_CANNON:
		case LAZR_SPECIAL_PARTICLE_UPLINK_CANNON:
			effect = EFFECT_LASER;
			break;

		case SPECIAL_NEUTRON_MISSILE:
		case NUKE_SPECIAL_NEUTRON_MISSILE:
		case SUPW_SPECIAL_NEUTRON_MISSILE:
			effect = EFFECT_NUKE;
			break;

		case SPECIAL_SCUD_STORM:
			effect = EFFECT_SCUD;
			break;

		default:
			return;
	}

	s_effect = effect;
	s_effectStartFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	s_effectSeed = chromaHash( s_effectStartFrame + (UnsignedInt)specialPowerType );
}

//-----------------------------------------------------------------------------
/** Paint the running effect over every device, on top of whatever the layout put
	* there.  Nothing else on the board matters while a nuke is in the air.
	*
	* The origin is the top left lamp and both axes are divided by the same number,
	* which is the half width.  Dividing each axis by its own extent instead
	* stretches a keyboard's six rows to the same span as its twenty-two columns,
	* and a ripple drawn in those coordinates jumps four rings per row: it comes
	* out as a grid of dots rather than as a wave.  Keys are about square, so one
	* step across and one step down have to be worth the same. */
static void chromaPaintEffect( Int *cells, UnsignedInt frame )
{
	for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
	{
		const ChromaDevice &info = CHROMA_DEVICES[ device ];
		const Real halfWidth = (Real)(info.columns - 1) * 0.5f;
		const Real downExtent = halfWidth > 0.0f ? (Real)(info.rows - 1) / halfWidth : 0.0f;
		for( Int row = 0; row < info.rows; ++row )
		{
			for( Int column = 0; column < info.columns; ++column )
			{
				const Real across = halfWidth > 0.0f ? (Real)column / halfWidth : 0.0f;
				const Real down = halfWidth > 0.0f ? (Real)row / halfWidth : 0.0f;
				const Int cell = info.firstCell + row * info.columns + column;
				cells[ cell ] = chromaEffectColor( across, down, downExtent, cell, frame );
			}
		}
	}
}

#if defined(_WIN32)
//-----------------------------------------------------------------------------
// The HTTP side.  Everything below here runs on the worker thread.
//-----------------------------------------------------------------------------
static Bool chromaRequest( HINTERNET connection, const char *verb, const char *path,
													 const char *body, char *reply, Int replyBytes )
{
	static const char *ACCEPT_TYPES[] = { "application/json", NULL };
	static const char *CONTENT_TYPE_HEADER = "Content-Type: application/json\r\n";

	HINTERNET request = HttpOpenRequestA( connection, verb, path, NULL, NULL, ACCEPT_TYPES,
																				INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0 );
	if( request == NULL )
		return FALSE;

	Bool ok = HttpSendRequestA( request, CONTENT_TYPE_HEADER, (DWORD)strlen( CONTENT_TYPE_HEADER ),
															(LPVOID)body, (DWORD)strlen( body ) ) ? TRUE : FALSE;
	if( ok && reply != NULL )
	{
		DWORD read = 0;
		ok = InternetReadFile( request, reply, (DWORD)(replyBytes - 1), &read ) ? TRUE : FALSE;
		reply[ ok ? read : 0 ] = 0;
	}

	InternetCloseHandle( request );
	return ok;
}

//-----------------------------------------------------------------------------
/** Ask the Chroma server for a session, and read back the address it wants the
	* frames on.  That address is not the one the request went to and not a path
	* under it either: Synapse hands out its own port and its own root, so it has
	* to be parsed rather than assembled. */
static Bool chromaOpenSession( HINTERNET connection, char *host, Int hostBytes,
															 INTERNET_PORT *port, char *path, Int pathBytes )
{
	char reply[ 512 ];
	if( !chromaRequest( connection, "POST", CHROMA_INIT_PATH, CHROMA_INIT_BODY, reply, sizeof( reply ) ) )
		return FALSE;

	const char *uri = strstr( reply, "http://" );
	if( uri == NULL )
		return FALSE;
	uri += strlen( "http://" );

	char parsedHost[ 64 ];
	char parsedPath[ 128 ];
	UnsignedShort parsedPort = 0;
	if( sscanf( uri, "%63[^:/]:%hu%127[^\"]", parsedHost, &parsedPort, parsedPath ) == 3 )
		*port = (INTERNET_PORT)parsedPort;
	else if( sscanf( uri, "%63[^:/]%127[^\"]", parsedHost, parsedPath ) == 2 )
		*port = INTERNET_DEFAULT_HTTP_PORT;
	else
		return FALSE;

	_snprintf( host, hostBytes, "%s", parsedHost );
	host[ hostBytes - 1 ] = 0;
	_snprintf( path, pathBytes, "%s", parsedPath );
	path[ pathBytes - 1 ] = 0;
	return TRUE;
}

//-----------------------------------------------------------------------------
/** A one-row device takes a flat array of colours, a taller one an array of rows. */
static void chromaBuildDeviceBody( const ChromaDevice &device, const Int *cells,
																	 char *body, Int bodyBytes )
{
	const Bool nested = device.rows > 1;
	Int used = _snprintf( body, bodyBytes, "{\"effect\":\"%s\",\"param\":[", device.effect );
	for( Int row = 0; row < device.rows && used > 0 && used < bodyBytes; ++row )
	{
		if( nested )
			used += _snprintf( body + used, bodyBytes - used, row == 0 ? "[" : ",[" );
		for( Int column = 0; column < device.columns && used > 0 && used < bodyBytes; ++column )
		{
			const Bool first = row == 0 && column == 0;
			used += _snprintf( body + used, bodyBytes - used,
												 (nested ? column == 0 : first) ? "%d" : ",%d",
												 cells[ row * device.columns + column ] );
		}
		if( nested )
			used += _snprintf( body + used, bodyBytes - used, "]" );
	}
	_snprintf( body + used, bodyBytes - used, "]}" );
	body[ bodyBytes - 1 ] = 0;
}

//-----------------------------------------------------------------------------
/** Open a session and connect to the address it names, or NULL when there is no
	* Chroma server to open one with. */
static HINTERNET chromaConnect( HINTERNET internet, char *sessionRoot, Int sessionRootBytes )
{
	HINTERNET handshake = InternetConnectA( internet, CHROMA_HOST, CHROMA_PORT, NULL, NULL,
																					INTERNET_SERVICE_HTTP, 0, 0 );
	if( handshake == NULL )
		return NULL;

	char sessionHost[ 64 ];
	INTERNET_PORT sessionPort = 0;
	const Bool opened = chromaOpenSession( handshake, sessionHost, sizeof( sessionHost ),
																				 &sessionPort, sessionRoot, sessionRootBytes );
	InternetCloseHandle( handshake );
	if( !opened )
		return NULL;

	HINTERNET connection = InternetConnectA( internet, sessionHost, sessionPort, NULL, NULL,
																					 INTERNET_SERVICE_HTTP, 0, 0 );
	if( connection != NULL )
		DEBUG_LOG(( "Chroma: session open on %s:%d%s\n", sessionHost, (Int)sessionPort, sessionRoot ));
	return connection;
}

//-----------------------------------------------------------------------------
/** Hand the hardware back.  Without the DELETE it holds the last frame sent
	* until the session times out, ten seconds of a battle on a board in a menu. */
static void chromaDisconnect( HINTERNET connection, const char *sessionRoot )
{
	chromaRequest( connection, "DELETE", sessionRoot, "", NULL, 0 );
	InternetCloseHandle( connection );
	DEBUG_LOG(( "Chroma: session closed, the board is Synapse's again\n" ));
}

//-----------------------------------------------------------------------------
/** The session is held only while s_sessionWanted says so, which is while a
	* match is being played with the option on.  Out of a match the board goes
	* back to whatever Synapse draws for the desktop. */
static DWORD WINAPI chromaWorkerMain( LPVOID )
{
	// Big enough for the largest device's cells as ten-digit numbers, plus the
	// punctuation and the effect name.
	char body[ KEYBOARD_CELLS * 12 + 128 ];
	char sessionRoot[ 128 ];
	char devicePath[ CHROMA_DEVICE_COUNT ][ 160 ];
	Int sent[ CHROMA_CELLS ];
	Int cells[ CHROMA_CELLS ];

	HINTERNET internet = InternetOpenA( "ZeroHourReforged", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0 );
	if( internet == NULL )
		return 0;

	DWORD timeout = CHROMA_TIMEOUT_MS;
	InternetSetOptionA( internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof( timeout ) );
	InternetSetOptionA( internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof( timeout ) );
	InternetSetOptionA( internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof( timeout ) );

	HINTERNET connection = NULL;
	Bool everAttempted = FALSE;
	Bool absenceLogged = FALSE;
	DWORD lastAttemptMs = 0;
	DWORD lastSendMs = 0;
	Bool answersLogged = FALSE;
	while( InterlockedCompareExchange( &s_workerShouldStop, 0, 0 ) == 0 )
	{
		const DWORD nowMs = Clock_Milliseconds();
		const Bool wanted = InterlockedCompareExchange( &s_sessionWanted, 0, 0 ) != 0;
		if( !wanted )
		{
			if( connection != NULL )
			{
				chromaDisconnect( connection, sessionRoot );
				connection = NULL;
			}
			Sleep( CHROMA_SEND_INTERVAL_MS );
			continue;
		}

		if( connection == NULL )
		{
			// Synapse may be started, or restarted, after the game, so an absent
			// server is asked again every few seconds rather than given up on.
			if( everAttempted && nowMs - lastAttemptMs < CHROMA_RETRY_MS )
			{
				Sleep( CHROMA_SEND_INTERVAL_MS );
				continue;
			}
			everAttempted = TRUE;
			lastAttemptMs = nowMs;
			connection = chromaConnect( internet, sessionRoot, sizeof( sessionRoot ) );
			if( connection == NULL )
			{
				if( !absenceLogged )
					DEBUG_LOG(( "Chroma: no Razer server on %s:%d, asking again every %d ms\n",
											CHROMA_HOST, (Int)CHROMA_PORT, (Int)CHROMA_RETRY_MS ));
				absenceLogged = TRUE;
				Sleep( CHROMA_SEND_INTERVAL_MS );
				continue;
			}
			for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
			{
				_snprintf( devicePath[ device ], sizeof( devicePath[ device ] ), "%s/%s",
									 sessionRoot, CHROMA_DEVICES[ device ].endpoint );
				devicePath[ device ][ sizeof( devicePath[ device ] ) - 1 ] = 0;
			}
			memset( sent, 0, sizeof( sent ) );
			lastSendMs = 0;
		}

		EnterCriticalSection( &s_cellLock );
		memcpy( cells, s_pendingCells, sizeof( cells ) );
		LeaveCriticalSection( &s_cellLock );

		const Bool keepalive = lastSendMs == 0 || nowMs - lastSendMs >= CHROMA_KEEPALIVE_MS;
		Bool sentAnything = FALSE;
		for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
		{
			const ChromaDevice &info = CHROMA_DEVICES[ device ];
			const Int deviceBytes = (Int)sizeof( Int ) * info.rows * info.columns;
			if( !keepalive && memcmp( cells + info.firstCell, sent + info.firstCell, deviceBytes ) == 0 )
				continue;

			chromaBuildDeviceBody( info, cells + info.firstCell, body, sizeof( body ) );
			// The server answers every frame with a result code, and a device it does
			// not have is refused rather than ignored, so the answer is worth a line
			// the first time round.
			char reply[ 64 ];
			const Bool sentOk = chromaRequest( connection, "PUT", devicePath[ device ],
																				 body, reply, sizeof( reply ) );
			if( !answersLogged )
			{
				DEBUG_LOG(( "Chroma: %s %s, answer %s\n", info.endpoint,
										sentOk ? "took the frame" : "refused it", sentOk ? reply : "none" ));
			}
			memcpy( sent + info.firstCell, cells + info.firstCell, deviceBytes );
			sentAnything = TRUE;
		}
		answersLogged = TRUE;
		if( sentAnything )
			lastSendMs = nowMs;

		Sleep( CHROMA_SEND_INTERVAL_MS );
	}

	if( connection != NULL )
		chromaDisconnect( connection, sessionRoot );
	InternetCloseHandle( internet );
	return 0;
}
#endif

//-----------------------------------------------------------------------------
// The walk.  Everything below here runs on the main thread.
//
// One pass over the player's objects, twice a second, collects every special
// power's charge and every factory's build clock.  The stock control bar pays a
// full object walk per shortcut button per frame; this pays one for all of it.
//-----------------------------------------------------------------------------
struct ChromaProducer
{
	ObjectID id;
	Real progress;			///< 0 to 1, or -1 for a factory that is building nothing
};

struct ChromaSnapshot
{
	Real bestSuperweaponCharge;
	Bool anySuperweaponReady;
	ChromaProducer producers[ PRODUCTION_LAMPS ];
	Int producerCount;
};

//-----------------------------------------------------------------------------
/** Keep the lowest object ids, so a lamp stays with the same factory for the
	* whole match instead of shuffling every time a building goes up. */
static void chromaRememberProducer( ChromaSnapshot *snapshot, ObjectID id, Real progress )
{
	Int slot = snapshot->producerCount;
	if( slot >= PRODUCTION_LAMPS )
	{
		if( id >= snapshot->producers[ PRODUCTION_LAMPS - 1 ].id )
			return;
		slot = PRODUCTION_LAMPS - 1;
	}
	else
	{
		++snapshot->producerCount;
	}

	while( slot > 0 && snapshot->producers[ slot - 1 ].id > id )
	{
		snapshot->producers[ slot ] = snapshot->producers[ slot - 1 ];
		--slot;
	}
	snapshot->producers[ slot ].id = id;
	snapshot->producers[ slot ].progress = progress;
}

//-----------------------------------------------------------------------------
static void chromaVisitObject( Object *obj, void *userData )
{
	ChromaSnapshot *snapshot = (ChromaSnapshot *)userData;

	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| obj->testStatus( OBJECT_STATUS_SOLD )
			|| obj->isEffectivelyDead() )
		return;

	// Only the superweapons are read here.  The generals powers come off the tray
	// itself, which already knows which of them are showing and which key reaches
	// each, neither of which this walk could work out.
	if( obj->isKindOf( KINDOF_FS_SUPERWEAPON ) )
	{
		for( BehaviorModule **module = obj->getBehaviorModules(); module && *module; ++module )
		{
			SpecialPowerModuleInterface *power = (*module)->getSpecialPower();
			if( power == NULL || power->isScriptOnly() )
				continue;

			const Real charge = power->getPercentReady();
			if( charge > snapshot->bestSuperweaponCharge )
				snapshot->bestSuperweaponCharge = charge;
			if( power->isReady() )
				snapshot->anySuperweaponReady = TRUE;
		}
	}

	ProductionUpdateInterface *production = obj->getProductionUpdateInterface();
	if( production != NULL )
	{
		const ProductionEntry *head = production->firstProduction();
		// getPercentComplete is already a percentage: the control bar hands it
		// straight to the button clock, which wants 0 to 100.
		const Real progress = head != NULL ? head->getPercentComplete() / 100.0f : -1.0f;
		chromaRememberProducer( snapshot, obj->getID(), progress );
	}
}

//-----------------------------------------------------------------------------
static const ChromaSnapshot &chromaWalkPlayer( Player *localPlayer, UnsignedInt frame )
{
	static ChromaSnapshot snapshot;
	static UnsignedInt lastWalkFrame = 0;
	static Bool everWalked = FALSE;

	if( everWalked && frame >= lastWalkFrame
			&& frame - lastWalkFrame < (UnsignedInt)WALK_INTERVAL_FRAMES )
		return snapshot;

	everWalked = TRUE;
	lastWalkFrame = frame;
	snapshot.bestSuperweaponCharge = 0.0f;
	snapshot.anySuperweaponReady = FALSE;
	snapshot.producerCount = 0;

	if( localPlayer != NULL )
		localPlayer->iterateObjects( chromaVisitObject, &snapshot );

	if( !snapshot.anySuperweaponReady )
		s_superweaponReadySince = 0;
	else if( s_superweaponReadySince == 0 )
		s_superweaponReadySince = frame;

	return snapshot;
}

//-----------------------------------------------------------------------------
// The alert lamps.  EVA cannot be polled - its flag array is private and is
// consumed before anyone outside could see it - and the radar's event ring lets
// only a position escape, so the score counters are what an edge is taken from.
//-----------------------------------------------------------------------------
enum ChromaAlert
{
	ALERT_UNIT_LOST = 0,
	ALERT_BUILDING_LOST,
	ALERT_UNDER_ATTACK,
	ALERT_PROMOTION,
	ALERT_UPGRADE_DONE,
	ALERT_BUILT,

	ALERT_COUNT
};

struct ChromaAlertState
{
	UnsignedInt firedFrame[ ALERT_COUNT ];
	Int unitsLost;
	Int buildingsLost;
	Int unitsBuilt;
	Int buildingsBuilt;
	UpgradeMaskType upgrades;
	Bool haveBaseline;
};

static ChromaAlertState s_alerts;

//-----------------------------------------------------------------------------
static Int chromaAlertCell( Int alert )
{
	const Int row = alert < ALERTS_PER_ROW ? ALERT_TOP_ROW : ALERT_BOTTOM_ROW;
	return chromaKeyboardCell( row, ALERT_FIRST_COLUMN + alert % ALERTS_PER_ROW );
}

//-----------------------------------------------------------------------------
static void chromaUpdateAlerts( Player *localPlayer, UnsignedInt frame )
{
	ScoreKeeper *score = localPlayer->getScoreKeeper();
	const Int unitsLost = score->getTotalUnitsLost();
	const Int buildingsLost = score->getTotalBuildingsLost();
	const Int unitsBuilt = score->getTotalUnitsBuilt();
	const Int buildingsBuilt = score->getTotalBuildingsBuilt();
	const UpgradeMaskType upgrades = localPlayer->getCompletedUpgradeMask();

	if( s_alerts.haveBaseline )
	{
		if( unitsLost > s_alerts.unitsLost )
			s_alerts.firedFrame[ ALERT_UNIT_LOST ] = frame;
		if( buildingsLost > s_alerts.buildingsLost )
			s_alerts.firedFrame[ ALERT_BUILDING_LOST ] = frame;
		if( unitsBuilt > s_alerts.unitsBuilt || buildingsBuilt > s_alerts.buildingsBuilt )
			s_alerts.firedFrame[ ALERT_BUILT ] = frame;
		if( upgrades != s_alerts.upgrades )
			s_alerts.firedFrame[ ALERT_UPGRADE_DONE ] = frame;
	}

	s_alerts.unitsLost = unitsLost;
	s_alerts.buildingsLost = buildingsLost;
	s_alerts.unitsBuilt = unitsBuilt;
	s_alerts.buildingsBuilt = buildingsBuilt;
	s_alerts.upgrades = upgrades;
	s_alerts.haveBaseline = TRUE;
}

//-----------------------------------------------------------------------------
static void chromaResetAlerts( void )
{
	for( Int alert = 0; alert < ALERT_COUNT; ++alert )
		s_alerts.firedFrame[ alert ] = 0;
	s_alerts.haveBaseline = FALSE;
}

//-----------------------------------------------------------------------------
static Bool chromaAlertIsLit( Int alert, UnsignedInt frame )
{
	const UnsignedInt fired = s_alerts.firedFrame[ alert ];
	return fired > 0 && frame >= fired && frame - fired < (UnsignedInt)ALERT_HOLD_FRAMES;
}

//-----------------------------------------------------------------------------
// The zones.
//-----------------------------------------------------------------------------

/** The digit row reads as a tank of power that empties, and it keeps to the same
	* three colours the meter on the command bar uses so the two never disagree. */
static void chromaFillPowerRow( Int *cells, const Energy *energy, UnsignedInt frame )
{
	const Int production = energy->getProduction();
	const Int consumption = energy->getConsumption();
	const Int litSegments = chromaPowerSegments( production, consumption );
	const Int yellowRange = TheGlobalData ? TheGlobalData->m_powerBarYellowRange : 0;
	const Bool warning = consumption > production - yellowRange && consumption <= production;
	const Int litColor = warning ? COLOR_YELLOW : COLOR_GREEN;
	const Int shortColor = chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? COLOR_RED : COLOR_OFF;

	const char *digits = CHROMA_KEY_ROWS[ CHROMA_POWER_ROW ];
	for( Int segment = 0; segment < CHROMA_POWER_SEGMENTS; ++segment )
	{
		Int color = COLOR_OFF;
		if( litSegments == CHROMA_POWER_BROWNOUT )
			color = shortColor;
		else if( segment < litSegments )
			color = litColor;
		cells[ chromaCellForKey( digits[ segment ] ) ] = color;
	}
}

//-----------------------------------------------------------------------------
/** How far round its clock a button is, zero to one, or one for a button that is
	* drawing no clock.  Read straight off the gadget the control bar already wrote
	* it to, so a key cannot drift from the button it stands for. */
static Real chromaButtonClock( GameWindow *button )
{
	if( button == NULL || !BitTest( button->winGetStyle(), GWS_PUSH_BUTTON ) )
		return 1.0f;

	const PushButtonData *buttonData = (const PushButtonData *)button->winGetUserData();
	if( buttonData == NULL || buttonData->drawClock == NO_CLOCK )
		return 1.0f;
	return buttonData->percentClock / 100.0f;
}

//-----------------------------------------------------------------------------
/** A key that can be pressed, dimmed by its build clock but never to nothing: a
	* factory that has just started a tank still takes a second order, and a dark
	* key says it would not. */
static Int chromaPressableWithClock( Int pressable, GameWindow *button )
{
	static const Real CLOCK_FLOOR = 0.35f;
	return chromaScale( pressable, CLOCK_FLOOR + (1.0f - CLOCK_FLOOR) * chromaButtonClock( button ) );
}

//-----------------------------------------------------------------------------
/** Where every bindable key sits on the grid, as runs of keys that are next to
	* each other on the board.
	*
	* A run works because MappableKeyType is DirectInput's scancodes and those are
	* numbered along the rows of the keyboard: 1 to 0 then minus and equals is one
	* unbroken count, and so is Q to P then the two brackets.  The alternative,
	* turning the key into its character and looking the character up, is what was
	* here and it was wrong twice over: numpad 1 and the 1 above the letters give
	* the same character and so lit the same lamp, and on a keyboard laid out for
	* any language but English the character is not the key. */
struct ChromaKeyRun
{
	Int firstKey;			///< MappableKeyType of the leftmost key
	Int count;
	Int row;
	Int firstColumn;
};

static const ChromaKeyRun CHROMA_KEY_RUNS[] =
{
	{ MK_ESC,					1,	0,	1 },
	{ MK_F1,					10,	0,	3 },		// F1 to F10
	{ MK_F11,					2,	0,	13 },		// F11, F12

	{ MK_TICK,				1,	1,	1 },
	{ MK_1,						12,	1,	2 },		// 1 to 0, minus, equals
	{ MK_BACKSPACE,		1,	1,	14 },
	{ MK_INS,					1,	1,	15 },
	{ MK_HOME,				1,	1,	16 },
	{ MK_PGUP,				1,	1,	17 },
	{ MK_KPSLASH,			1,	1,	19 },
	{ MK_KPMINUS,			1,	1,	21 },

	{ MK_TAB,					1,	2,	1 },
	{ MK_Q,						12,	2,	2 },		// Q to P, both brackets
	{ MK_BACKSLASH,		1,	2,	14 },
	{ MK_DEL,					1,	2,	15 },
	{ MK_END,					1,	2,	16 },
	{ MK_PGDN,				1,	2,	17 },
	{ MK_KP7,					3,	2,	18 },		// 7, 8, 9
	{ MK_KPPLUS,			1,	2,	21 },

	{ MK_A,						11,	3,	2 },		// A to L, semicolon, apostrophe
	{ MK_ENTER,				1,	3,	14 },
	{ MK_KP4,					3,	3,	18 },		// 4, 5, 6

	// Z sits one column further right than A: column two of this row is the extra
	// key an ISO board has between left shift and Z (RZKEY_Z is 0x0403).
	{ MK_Z,						10,	4,	3 },		// Z to M, comma, period, slash
	{ MK_UP,					1,	4,	16 },
	{ MK_KP1,					3,	4,	18 },		// 1, 2, 3

	{ MK_SPACE,				1,	5,	7 },
	{ MK_LEFT,				1,	5,	15 },
	{ MK_DOWN,				1,	5,	16 },
	{ MK_RIGHT,				1,	5,	17 },
	{ MK_KP0,					1,	5,	19 },		// RZKEY_NUMPAD0 is 0x0513
};
static const Int CHROMA_KEY_RUN_COUNT = sizeof( CHROMA_KEY_RUNS ) / sizeof( CHROMA_KEY_RUNS[ 0 ] );

//-----------------------------------------------------------------------------
Int chromaCellForMappableKey( Int key )
{
	if( key == MK_NONE )
		return -1;

	for( Int run = 0; run < CHROMA_KEY_RUN_COUNT; ++run )
	{
		const ChromaKeyRun &keys = CHROMA_KEY_RUNS[ run ];
		const Int along = key - keys.firstKey;
		if( along >= 0 && along < keys.count )
			return chromaKeyboardCell( keys.row, keys.firstColumn + along );
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Which lamp each command bar slot and each tray slot answers to under the
// bindings in force, looked up twice a second rather than per lamp per frame.
// The player can rebind a slot in Options mid-match, so it cannot be looked up
// once.
//-----------------------------------------------------------------------------
/// How many slots of each bar a key can actually reach, counted off the messages
/// themselves rather than off the arrays behind the bars.  Bounding a slot read
/// out of a message by the bar's array once ran past the end of the command
/// messages and into the shortcut ones, and filed function keys as command bar
/// slots.
static const Int COMMAND_SLOTS_WITH_KEYS =
	(Int)GameMessage::MSG_META_SHORTCUT_SLOT01 - (Int)GameMessage::MSG_META_COMMAND_SLOT01;
static const Int SHORTCUT_SLOTS_WITH_KEYS =
	(Int)GameMessage::MSG_META_VIEW_COMMAND_CENTER - (Int)GameMessage::MSG_META_SHORTCUT_SLOT01;

static Int s_commandSlotCell[ COMMAND_SLOTS_WITH_KEYS ];
static Int s_shortcutSlotCell[ SHORTCUT_SLOTS_WITH_KEYS ];

//-----------------------------------------------------------------------------
Int chromaCommandSlotForMessage( Int metaMessage )
{
	const Int slot = metaMessage - (Int)GameMessage::MSG_META_COMMAND_SLOT01;
	return slot >= 0 && slot < COMMAND_SLOTS_WITH_KEYS ? slot : -1;
}

//-----------------------------------------------------------------------------
Int chromaShortcutSlotForMessage( Int metaMessage )
{
	const Int slot = metaMessage - (Int)GameMessage::MSG_META_SHORTCUT_SLOT01;
	return slot >= 0 && slot < SHORTCUT_SLOTS_WITH_KEYS ? slot : -1;
}

//-----------------------------------------------------------------------------
static void chromaRefreshBoundCells( UnsignedInt frame )
{
	static UnsignedInt lastRefreshFrame = 0;
	static Bool everRefreshed = FALSE;
	if( everRefreshed && frame >= lastRefreshFrame
			&& frame - lastRefreshFrame < (UnsignedInt)WALK_INTERVAL_FRAMES )
		return;

	everRefreshed = TRUE;
	lastRefreshFrame = frame;
	for( Int slot = 0; slot < COMMAND_SLOTS_WITH_KEYS; ++slot )
		s_commandSlotCell[ slot ] = -1;
	for( Int slot = 0; slot < SHORTCUT_SLOTS_WITH_KEYS; ++slot )
		s_shortcutSlotCell[ slot ] = -1;
	if( TheMetaMap == NULL )
		return;

	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
	{
		// only the bare key: the shifted copy of a slot is the same lamp
		if( rec->m_modState != 0 )
			continue;

		const Int commandSlot = chromaCommandSlotForMessage( (Int)rec->m_meta );
		if( commandSlot >= 0 && s_commandSlotCell[ commandSlot ] < 0 )
			s_commandSlotCell[ commandSlot ] = chromaCellForMappableKey( rec->m_key );

		const Int shortcutSlot = chromaShortcutSlotForMessage( (Int)rec->m_meta );
		if( shortcutSlot >= 0 && s_shortcutSlotCell[ shortcutSlot ] < 0 )
			s_shortcutSlotCell[ shortcutSlot ] = chromaCellForMappableKey( rec->m_key );
	}

	// CHROMADRILL is temporary, and the three blocks that write it come out once
	// the owner has confirmed the keys light where their labels say.  There is no
	// way to see this lighting from a script - the hardware is the output - so the
	// log is the only instrument, and it answers the one question worth asking:
	// which lamp did each slot get, and what was behind it.
#if defined(DEBUG_LOGGING)
	static Bool reported = FALSE;
	if( !reported )
	{
		reported = TRUE;
		for( Int slot = 0; slot < COMMAND_SLOTS_WITH_KEYS; ++slot )
			DEBUG_LOG(( "CHROMADRILL: command slot %d -> cell %d\n", slot, s_commandSlotCell[ slot ] ));
		for( Int slot = 0; slot < SHORTCUT_SLOTS_WITH_KEYS; ++slot )
			DEBUG_LOG(( "CHROMADRILL: shortcut slot %d -> cell %d\n", slot, s_shortcutSlotCell[ slot ] ));
	}
#endif
}

//-----------------------------------------------------------------------------
/** The command bar: the grid keys.  What a key would do comes from the control
	* bar's own resolution of the press, so the lamps follow the labels on screen. */
static void chromaFillCommandGrid( Int *cells, Int pressable, Int unavailable )
{
	for( Int slot = 0; slot < COMMAND_SLOTS_WITH_KEYS; ++slot )
	{
		const Int cell = s_commandSlotCell[ slot ];
		if( cell < 0 )
			continue;

		GameWindow *button = NULL;
		switch( TheControlBar->peekCommandButtonPress( slot, &button ) )
		{
			case ControlBar::PRESS_FIRES:
				cells[ cell ] = chromaPressableWithClock( pressable, button );
				break;
			case ControlBar::PRESS_ARMS_CHORD:
				// worth starting only if the group holds something the second key can build
				cells[ cell ] = button != NULL ? pressable : unavailable;
				break;
			case ControlBar::PRESS_IS_REFUSED:
				cells[ cell ] = unavailable;
				break;
			default:
				break;	// no button behind this key, so it keeps the bed's colour
		}
	}

#if defined(DEBUG_LOGGING)
	static UnsignedInt lastReportFrame = 0;
	const UnsignedInt now = TheGameLogic ? TheGameLogic->getFrame() : 0;
	if( now > lastReportFrame + LOGICFRAMES_PER_SECOND * 5 )
	{
		lastReportFrame = now;
		DEBUG_LOG(( "CHROMADRILL: frame %d selected %d\n",
								now, TheInGameUI ? TheInGameUI->getSelectCount() : -1 ));
		for( Int slot = 0; slot < COMMAND_SLOTS_WITH_KEYS; ++slot )
		{
			GameWindow *button = NULL;
			const Int outcome = (Int)TheControlBar->peekCommandButtonPress( slot, &button );
			const Int cell = s_commandSlotCell[ slot ];
			const CommandButton *command = button ? (const CommandButton *)GadgetButtonGetData( button ) : NULL;
			DEBUG_LOG(( "CHROMADRILL:   slot %d outcome %d lamp r%dc%d command %s\n",
									slot, outcome, cell < 0 ? -1 : cell / KEYBOARD_COLUMNS,
									cell < 0 ? -1 : cell % KEYBOARD_COLUMNS,
									command ? command->getName().str() : "-" ));
		}
	}
#endif
}

//-----------------------------------------------------------------------------
/** The generals powers, on the keys that reach them.  It takes two presses: the
	* first names a row of the tray and the second a power in that row.  So with
	* nothing armed, the key of every row that holds a usable power blinks; once a
	* row is armed the blinking moves onto the keys of the usable powers inside it.
	* A power still charging shows as far as its clock has got, and a key that
	* reaches nothing stays the colour of the bed. */
static void chromaFillPowerTray( Int *cells, Int bed, Int factionColor, UnsignedInt frame )
{
	const Int usable = chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? COLOR_WARM_WHITE : COLOR_OFF;

	for( Int slot = 0; slot < SHORTCUT_SLOTS_WITH_KEYS; ++slot )
	{
		const Int cell = s_shortcutSlotCell[ slot ];
		if( cell < 0 )
			continue;

		GameWindow *button = NULL;
		switch( TheControlBar->peekSpecialPowerShortcutPress( slot, &button ) )
		{
			case ControlBar::PRESS_FIRES:
				cells[ cell ] = usable;
				break;
			case ControlBar::PRESS_ARMS_CHORD:
				cells[ cell ] = button != NULL ? usable : bed;
				break;
			case ControlBar::PRESS_IS_REFUSED:
				cells[ cell ] = chromaScale( factionColor, chromaButtonClock( button ) );
				break;
			default:
				break;
		}
	}
}

//-----------------------------------------------------------------------------
/** The numpad fills from the bottom as the nearest superweapon charges, and the
	* whole block blinks once any of them can fire. */
static void chromaFillNumpad( Int *cells, const ChromaSnapshot &snapshot, Int factionColor,
															UnsignedInt frame )
{
	const Int readyColor = chromaReadyColor( frame, s_superweaponReadySince, COLOR_WARM_WHITE );
	const Int lit = chromaBarSegments( snapshot.bestSuperweaponCharge, NUMPAD_CELLS );

	Int index = 0;
	for( Int row = NUMPAD_LAST_ROW; row >= NUMPAD_FIRST_ROW; --row )
	{
		for( Int column = NUMPAD_FIRST_COLUMN; column <= NUMPAD_LAST_COLUMN; ++column )
		{
			Int color;
			if( snapshot.anySuperweaponReady )
				color = readyColor;
			else
				color = index < lit ? factionColor : COLOR_OFF;
			cells[ chromaKeyboardCell( row, column ) ] = color;
			++index;
		}
	}
}

//-----------------------------------------------------------------------------
/** One lamp per factory along the bottom row, ordered by object id so a lamp
	* keeps its building.  Dark is a factory building nothing, which is the thing
	* worth noticing. */
static void chromaFillProduction( Int *cells, const ChromaSnapshot &snapshot, Int factionColor )
{
	for( Int lamp = 0; lamp < snapshot.producerCount && lamp < PRODUCTION_LAMPS; ++lamp )
	{
		const Real progress = snapshot.producers[ lamp ].progress;
		const Int color = progress < 0.0f ? chromaScale( COLOR_RED, IDLE_PRODUCER_SCALE )
																			: chromaScale( factionColor, progress );
		cells[ chromaKeyboardCell( PRODUCTION_ROW, PRODUCTION_COLUMNS[ lamp ] ) ] = color;
	}
}

//-----------------------------------------------------------------------------
/** Two arrow keys carry the selection: how hurt it is, and what rank the one in
	* front of it holds. */
static void chromaFillSelection( Int *cells )
{
	if( TheInGameUI == NULL || TheInGameUI->getSelectCount() <= 0 )
		return;

	const DrawableList *selected = TheInGameUI->getAllSelectedLocalDrawables();
	if( selected == NULL )
		return;

	Real health = 0.0f;
	Real maxHealth = 0.0f;
	VeterancyLevel rank = LEVEL_INVALID;
	for( DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || obj->getBodyModule() == NULL )
			continue;

		health += obj->getBodyModule()->getHealth();
		maxHealth += obj->getBodyModule()->getMaxHealth();
		if( rank == LEVEL_INVALID )
			rank = obj->getVeterancyLevel();
	}

	if( maxHealth > 0.0f )
	{
		const Real fraction = health / maxHealth;
		cells[ chromaKeyboardCell( SELECTION_HEALTH_ROW, SELECTION_HEALTH_COLUMN ) ] =
			chromaColor( 1.0f - fraction, fraction, 0.0f );
	}

	Int rankColor = COLOR_OFF;
	switch( rank )
	{
		case LEVEL_VETERAN:	rankColor = COLOR_GREEN; break;
		case LEVEL_ELITE:		rankColor = COLOR_BLUE; break;
		case LEVEL_HEROIC:	rankColor = COLOR_GOLD; break;
		default:						break;
	}
	cells[ chromaKeyboardCell( SELECTION_RANK_ROW, SELECTION_RANK_COLUMN ) ] = rankColor;
}

//-----------------------------------------------------------------------------
/** Escape says what the match itself is doing, and the three keys beside the
	* function row say whether the network, the radar and the clock are healthy. */
static void chromaFillMatchState( Int *cells, UnsignedInt frame )
{
	Int matchColor = COLOR_OFF;
	if( TheVictoryConditions != NULL && TheVictoryConditions->isLocalDefeat() )
		matchColor = COLOR_RED;
	else if( TheVictoryConditions != NULL && TheVictoryConditions->isLocalAlliedVictory() )
		matchColor = COLOR_WHITE;
	else if( TheGameLogic->isPeaceTime() )
		matchColor = chromaBlinkIsOn( frame, STAR_PERIOD_FRAMES ) ? COLOR_BLUE : COLOR_OFF;
	else if( TheGameLogic->isGamePaused()
					 || (TheInGameUI != NULL && (TheInGameUI->isQuitMenuVisible() || !TheInGameUI->getInputEnabled())) )
		matchColor = COLOR_AMBER;
	cells[ chromaKeyboardCell( MATCH_STATE_ROW, MATCH_STATE_COLUMN ) ] = matchColor;

	Int networkColor = COLOR_OFF;
	if( TheNetwork != NULL )
	{
		networkColor = TheNetwork->isFrameDataReady()
									 ? chromaScale( COLOR_GREEN, AMBIENT_SCALE )
									 : (chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? COLOR_RED : COLOR_OFF);
	}
	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_NETWORK_COLUMN ) ] = networkColor;

	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_PAUSED_COLUMN ) ] =
		TheGameLogic->isGamePaused() ? COLOR_AMBER : COLOR_OFF;
}

//-----------------------------------------------------------------------------
static void chromaFillAlerts( Int *cells, Player *localPlayer, UnsignedInt frame )
{
	static const Int ALERT_COLORS[ ALERT_COUNT ] =
	{
		COLOR_RED,				// a unit of yours died
		COLOR_RED,				// a building of yours died
		COLOR_RED,				// something is shooting at the base
		COLOR_GOLD,				// a promotion is waiting to be spent
		COLOR_GREEN,			// an upgrade finished
		COLOR_WHITE				// something of yours finished building
	};

	for( Int alert = 0; alert < ALERT_COUNT; ++alert )
	{
		if( alert == ALERT_PROMOTION || alert == ALERT_UNDER_ATTACK )
			continue;
		cells[ chromaAlertCell( alert ) ] =
			chromaAlertIsLit( alert, frame ) ? ALERT_COLORS[ alert ] : COLOR_OFF;
	}

	const UnsignedInt attackedFrame = localPlayer->getAttackedFrame();
	const Bool underAttack = attackedFrame > 0 && frame >= attackedFrame
												 && frame - attackedFrame < (UnsignedInt)UNDER_ATTACK_FRAMES;
	cells[ chromaAlertCell( ALERT_UNDER_ATTACK ) ] =
		underAttack && chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES )
		? ALERT_COLORS[ ALERT_UNDER_ATTACK ] : COLOR_OFF;

	// The general's star on screen flashes on the frame count, not a timer of its
	// own; the lamp keeps to the same cycle so the two blink together.
	const Bool promotionWaiting = localPlayer->getSciencePurchasePoints() > 0;
	cells[ chromaAlertCell( ALERT_PROMOTION ) ] =
		promotionWaiting && chromaBlinkIsOn( frame, STAR_PERIOD_FRAMES )
		? ALERT_COLORS[ ALERT_PROMOTION ] : COLOR_OFF;
}

//-----------------------------------------------------------------------------
static void chromaFillMoney( Int *cells, Player *localPlayer, Real alarm )
{
	const UnsignedInt money = localPlayer->getMoney()->countMoney();
	const Int lit = chromaMoneySegments( money, MOUSEPAD_CELLS );
	// The pad joins the alarm on the lamps the money has not reached, which leaves
	// the count readable while it pulses: a green lamp and a red one are still two
	// different lamps, where a pad tinted end to end is one long smear.
	const Int dark = chromaAlarmed( COLOR_OFF, alarm * ALARM_DEPTH );

	Int *mousepad = cells + MOUSEPAD_FIRST_CELL;
	for( Int led = 0; led < MOUSEPAD_CELLS; ++led )
		mousepad[ led ] = led < lit ? COLOR_GREEN : dark;
}

//-----------------------------------------------------------------------------
/** The base is being shot at.  It goes on the mouse, which carries nothing to
	* read and sits under the hand, and on the strip down the left edge of the
	* board.  The mousepad takes it too, in chromaFillMoney, on the lamps the money
	* bar has not reached.  The keys are left alone: this fires exactly when the
	* player most needs to read the bar, and it used to bury it. */
static void chromaFillAlarm( Int *cells, Real alarm )
{
	if( alarm <= 0.0f )
		return;

	for( Int row = 0; row < KEYBOARD_ROWS; ++row )
	{
		const Int cell = chromaKeyboardCell( row, ALARM_STRIP_COLUMN );
		cells[ cell ] = chromaAlarmed( cells[ cell ], alarm * ALARM_DEPTH );
	}
	for( Int led = 0; led < MOUSE_CELLS; ++led )
	{
		Int *cell = cells + MOUSE_FIRST_CELL + led;
		*cell = chromaAlarmed( *cell, alarm * ALARM_DEPTH );
	}
}

//-----------------------------------------------------------------------------
#if !defined(_WIN32)
/* Off Windows nothing sends a frame (see the transport above), so nothing composes one: only
	 updateChromaKeyboard's Windows half calls this.  It is kept, and built, for the transport that
	 will - a macOS or Linux lighting backend would call it from updateChromaKeyboard. */
[[maybe_unused]] static void chromaFillCells( Int *cells, Bool inMatch );
#endif
static void chromaFillCells( Int *cells, Bool inMatch )
{
	Real red = 0.5f, green = 0.5f, blue = 0.5f;
	Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( localPlayer != NULL )
	{
		UnsignedByte r, g, b, a;
		GameGetColorComponents( localPlayer->getPlayerColor(), &r, &g, &b, &a );
		red = r / 255.0f;
		green = g / 255.0f;
		blue = b / 255.0f;
	}
	const Int factionColor = chromaColor( red, green, blue );

	const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;

	Real alarm = 0.0f;
	if( inMatch && localPlayer != NULL )
	{
		const UnsignedInt attackedFrame = localPlayer->getAttackedFrame();
		if( attackedFrame > 0 && frame >= attackedFrame
				&& frame - attackedFrame < (UnsignedInt)UNDER_ATTACK_FRAMES )
		{
			const Real seconds = (Real)frame / (Real)LOGICFRAMES_PER_SECOND;
			const Real phase = sinf( seconds * UNDER_ATTACK_PULSE_HZ * TWO_PI );
			alarm = 0.5f + 0.5f * phase;
		}
	}
	// With something selected the bar is full of keys worth pressing, and a board
	// glowing all over is the worst background to read them against, so the bed
	// goes out and leaves the pressable keys and the gauges lit on black.  With
	// nothing selected there is nothing to read and the colour comes back.
	const Bool hasSelection = TheInGameUI != NULL && TheInGameUI->getSelectCount() > 0;
	const Int bed = hasSelection ? COLOR_OFF : chromaScale( factionColor, AMBIENT_SCALE );
	for( Int cell = 0; cell < CHROMA_CELLS; ++cell )
		cells[ cell ] = bed;

	if( !inMatch || localPlayer == NULL || TheHotKeyManager == NULL )
	{
		chromaResetAlerts();
		s_effect = EFFECT_NONE;
		s_superweaponReadySince = 0;
		return;
	}

	chromaUpdateAlerts( localPlayer, frame );
	chromaRefreshBoundCells( frame );
	const ChromaSnapshot &snapshot = chromaWalkPlayer( localPlayer, frame );

	const Int pressable = factionColor;
	const Int unavailable = COLOR_OFF;

	if( TheControlBar != NULL )
	{
		chromaFillCommandGrid( cells, pressable, unavailable );
		chromaFillPowerTray( cells, bed, factionColor, frame );
	}
	chromaFillPowerRow( cells, localPlayer->getEnergy(), frame );
	chromaFillNumpad( cells, snapshot, factionColor, frame );
	chromaFillProduction( cells, snapshot, factionColor );
	chromaFillSelection( cells );
	chromaFillMatchState( cells, frame );
	chromaFillAlerts( cells, localPlayer, frame );
	chromaFillMoney( cells, localPlayer, alarm );
	chromaFillAlarm( cells, alarm );

	// The radar lamp is the one piece of match state that comes off the player
	// rather than the logic, so it is set here where the player is in hand.
	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_RADAR_COLUMN ) ] =
		localPlayer->hasRadar() ? chromaScale( COLOR_GREEN, AMBIENT_SCALE ) : COLOR_OFF;

#if defined(DEBUG_LOGGING)
	{
		static UnsignedInt lastGridFrame = 0;
		if( frame > lastGridFrame + LOGICFRAMES_PER_SECOND * 5 )
		{
			lastGridFrame = frame;
			for( Int row = 0; row < KEYBOARD_ROWS; ++row )
			{
				// At most 18 + 22 * 7 + 1 characters, so it never truncates, where snprintf and MSVC's
				// _snprintf, which it replaced, would differ.
				char line[ 256 ];
				Int used = snprintf( line, sizeof( line ), "CHROMADRILL: row %d", row );
				for( Int column = 0; column < KEYBOARD_COLUMNS; ++column )
					used += snprintf( line + used, sizeof( line ) - used, " %06X",
														 cells[ chromaKeyboardCell( row, column ) ] );
				snprintf( line + used, sizeof( line ) - used, "\n" );
				line[ sizeof( line ) - 1 ] = 0;
				DEBUG_LOG(( "%s", line ));
			}
		}
	}
#endif

	// A superweapon going off takes the lot for a few seconds.  It goes on last
	// because it is meant to bury everything under it.
	if( chromaEffectIsLive( frame ) )
		chromaPaintEffect( cells, frame );
}

//-----------------------------------------------------------------------------
void updateChromaKeyboard( void )
{
	if( s_disabled )
		return;
#if defined(_WIN32)

	if( !s_workerRunning )
	{
		InitializeCriticalSection( &s_cellLock );
		DWORD threadId = 0;
		s_workerThread = ::CreateThread( NULL, 0, chromaWorkerMain, NULL, 0, &threadId );
		if( s_workerThread == NULL )
		{
			DeleteCriticalSection( &s_cellLock );
			s_disabled = TRUE;
			return;
		}
		s_workerRunning = TRUE;
	}

	// The shell map is a running game as far as GameLogic is concerned, so the
	// match test has to exclude it or the main menu lights up like a battle.
	const Bool inMatch = TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
	// The option applies live: off, or out of a match, and the worker closes the
	// session and the board goes back to Synapse.
	const Bool wanted = inMatch && TheGlobalData != NULL && TheGlobalData->m_chromaLighting;
	InterlockedExchange( &s_sessionWanted, wanted ? 1 : 0 );

	Int cells[ CHROMA_CELLS ];
	chromaFillCells( cells, inMatch );

	EnterCriticalSection( &s_cellLock );
	memcpy( s_pendingCells, cells, sizeof( s_pendingCells ) );
	LeaveCriticalSection( &s_cellLock );
#endif
}

//-----------------------------------------------------------------------------
void disableChromaKeyboard( void )
{
	s_disabled = TRUE;
}

//-----------------------------------------------------------------------------
void shutdownChromaKeyboard( void )
{
#if defined(_WIN32)
	if( !s_workerRunning )
		return;

	InterlockedExchange( &s_workerShouldStop, 1 );
	WaitForSingleObject( s_workerThread, 2000 );
	CloseHandle( s_workerThread );
	s_workerThread = NULL;
	DeleteCriticalSection( &s_cellLock );
	s_workerRunning = FALSE;
#endif
}
