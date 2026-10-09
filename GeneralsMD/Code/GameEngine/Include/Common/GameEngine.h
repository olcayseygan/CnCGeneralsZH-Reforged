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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameEngine.h /////////////////////////////////////////////////////////
// The Game engine interface
// Author: Michael S. Booth, April 2001

#pragma once

#ifndef _GAME_ENGINE_H_
#define _GAME_ENGINE_H_

#include "Common/SubsystemInterface.h"
#include "Common/GameType.h"

#define DEFAULT_MAX_FPS		45

/** How much wall-clock debt the pacer will carry and pay back. A render pass slower than one
	logic frame owes the simulation more than one tick; without a catch-up the game speed just
	becomes the render speed. The cap is the anti-spiral: when the logic frame itself is what
	overran, the debt past this much is dropped and the match runs slow.

	It is a duration, not a frame count, because the frame count that duration buys depends on the
	game speed. Three frames - the number this used to be - is a tenth of a second at the retail
	30fps and half that at 60, so picking a faster game speed made the pacer give up on a slow
	render twice as early: at 60fps a match visibly ran in slow motion the moment the frame rate
	dropped under 20. See GameEngine_isLogicFrameDue in GameEngine.cpp. */
#define LOGIC_CATCHUP_MAX_MS	100.0f

/** The above in logic frames, at a given game speed: 3 at the retail 30fps, 6 at 60. */
Int GameEngine_logicCatchupMaxFrames( Int logicFps );

/** R1, smooth motion: the renderer's blend reads how far into the next logic tick the wall clock is
	(0 just after a tick, 1 a whole tick later or more; always 1 in fast mode or before the first tick).
	The logic loop notes each pass's last tick with GameEngine_noteLogicTickDone. */
void GameEngine_noteLogicTickDone( Int logicFps, Bool fastMode );
Real GameEngine_logicTickFraction( void );

/** -video and -directorrecord: whether the logic frame is one a range records.  -recordfps draws
	GameEngine_videoPictures() pictures of each of those, and the logic runs after the last; the coming
	draw is picture GameEngine_videoPicture() of them, 0 first.  1 and 0 outside a range, and at 30.
	The last picture shows the logic frame, and each one before it is that share of a frame earlier: its
	blend fraction is (picture + 1) / pictures, and GameEngine_pictureFrame is its time in logic frames,
	what an animation drawn for footage runs on instead of the wall clock.  GameEngine_pictureMilliseconds
	is the same time in milliseconds, the logic frame times 1000 / LOGICFRAMES_PER_SECOND for its last
	picture.  All client side: the logic runs the frames it always did. */
Bool GameEngine_isVideoFrame( void );
Int GameEngine_videoPictures( void );
/** The pictures of this logic frame or of the next one are blends, so smooth motion has to see this one. */
Bool GameEngine_videoBlends( void );
/** What the movie is told: -recordfps, or 30 where it does not apply (a network game, -headless). */
Int GameEngine_videoFramesPerSecond( void );
Int GameEngine_videoPicture( void );
Real GameEngine_pictureFrame( void );
UnsignedInt GameEngine_pictureMilliseconds( void );

/**	How long the catch-up loop may keep starting new logic ticks before it gives up for this pass.

	A frame count cannot tell a cheap tick from an expensive one, and that is the whole difference
	between the two situations the loop is in.  Behind because the *renderer* hitched, the ticks are
	2ms each and paying three of them costs nothing.  Behind because eight Brutal AIs are all
	fighting at once, the ticks are 25ms each and paying three of them freezes the picture for 75ms
	on top of the render - which is exactly the 91ms and 113ms frames measured on Twilight Flame.

	So the loop is bounded by a clock as well as by a count: after each tick, another one only
	starts if the loop has spent less than this. Cheap debt still gets paid in full; expensive debt
	is left on the accumulator, where the existing cap stops it spiralling and the match honestly
	runs a touch slow instead of stopping dead. The worst case becomes this plus one tick.

	That is the intent the frame count was already written for - "when the logic frame itself is what
	overran, paying the debt would just queue more of the same work" - measured in the unit that can
	actually see it. */
#define LOGIC_CATCHUP_BUDGET_MS	8.0f

/** Whether the catch-up loop may start another logic tick this pass. */
Bool GameEngine_mayStartAnotherCatchupTick( Int ticksSoFar, Int maxTicks, Real elapsedMsInLoop );

/** A rate sampled every half second, averaged, and the whole number written on screen for it. */
struct RateReading
{
	Real average;		///< the samples so far, or the last several once there are that many
	Int samples;		///< how many the average holds, up to the window
	Int shown;			///< the average as written, moved only a full step at a time

	RateReading() : average( 0.0f ), samples( 0 ), shown( 0 ) {}
	void add( Real sample );
	void restart() { samples = 0; }		///< the next sample starts the average again; shown stays
};

// forward declarations
class AudioManager;
class GameLogic;
class GameClient;
class MessageStream;															///< @todo Create a MessageStreamInterface abstract class
class FileSystem;
class Keyboard;
class LocalFileSystem;
class ArchiveFileSystem;
class FileSystem;
class Mouse;
class NetworkInterface;
class ModuleFactory;
class ThingFactory;
class FunctionLexicon;
class Radar;
class WebBrowser;
class ParticleSystemManager;

/**
 * The implementation of the game engine
 */
class GameEngine : public SubsystemInterface
{

public:

	GameEngine( void );
	virtual ~GameEngine();

	virtual void init( void );								///< Init engine by creating client and logic
	virtual void init( int argc, char *argv[] );			///< Init engine by creating client and logic
	virtual void reset( void );								///< reset system to starting state
	virtual void update( void );							///< per frame update

	virtual void execute( void );											/**< The "main loop" of the game engine.
																								 It will not return until the game exits. */
	virtual void setFramesPerSecondLimit( Int fps );	///< Set the maximum rate engine updates are allowed to occur
	virtual Int  getFramesPerSecondLimit( void );			///< Get maxFPS.  Not inline since it is called from another lib.
	virtual Int  getLogicFramesPerSecond( void );			///< logic frames the last second really ran; the rate a countdown on screen divides by
	virtual void setQuitting( Bool quitting );				///< set quitting status
	virtual Bool getQuitting(void);						///< is app getting ready to quit.

	virtual Bool isMultiplayerSession( void );
	virtual void serviceWindowsOS(void) {};		///< service the native OS
	virtual Bool isActive(void) {return m_isActive;}	///< returns whether app has OS focus.
	virtual void setIsActive(Bool isActive) { m_isActive = isActive; };

protected:

	virtual FileSystem *createFileSystem( void );								///< Factory for FileSystem classes
	virtual LocalFileSystem *createLocalFileSystem( void ) = 0;	///< Factory for LocalFileSystem classes
	virtual ArchiveFileSystem *createArchiveFileSystem( void ) = 0;	///< Factory for ArchiveFileSystem classes
	virtual GameLogic *createGameLogic( void ) = 0;							///< Factory for GameLogic classes.
	virtual GameClient *createGameClient( void ) = 0;						///< Factory for GameClient classes.
	virtual MessageStream *createMessageStream( void );					///< Factory for the message stream
	virtual ModuleFactory *createModuleFactory( void ) = 0;			///< Factory for modules
	virtual ThingFactory *createThingFactory( void ) = 0;				///< Factory for the thing factory
	virtual FunctionLexicon *createFunctionLexicon( void ) = 0;	///< Factory for Function Lexicon
	virtual Radar *createRadar( void ) = 0;											///< Factory for radar
	virtual WebBrowser *createWebBrowser( void ) = 0;						///< Factory for embedded browser
	virtual ParticleSystemManager* createParticleSystemManager( void ) = 0;
	virtual AudioManager *createAudioManager( void ) = 0;				///< Factory for Audio Manager

	void sampleLogicRate( void );

	Int m_maxFPS;																									///< Maximum frames per second allowed
	UnsignedInt m_logicRateSampleMs;		///< wall clock the current logic rate sample started at, 0 for none
	UnsignedInt m_logicRateSampleFrame;	///< logic frame it started on
	RateReading m_measuredLogicFps;			///< the rate countdowns divide by, 0 shown before there is one
  Bool m_quitting;  ///< true when we need to quit the game
	Bool m_isActive;	///< app has OS focus.

};
inline void GameEngine::setQuitting( Bool quitting ) { m_quitting = quitting; }
inline Bool GameEngine::getQuitting(void) { return m_quitting; }

// the game engine singleton
extern GameEngine *TheGameEngine;

/// This function creates a new game engine instance, and is device specific
extern GameEngine *CreateGameEngine( void );

/// The entry point for the game system
extern void GameMain( int argc, char *argv[] );

#endif // _GAME_ENGINE_H_
