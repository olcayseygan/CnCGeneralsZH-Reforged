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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: LogicThread.h ////////////////////////////////////////////////////////////////////////////
// Desc:   LOGIC-THREAD.md phase 1: GameLogic::update on a thread of its own, the main thread
//         waiting for it, and a census of the client calls the tick makes from there.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __LOGICTHREAD_H__
#define __LOGICTHREAD_H__

#include "Lib/BaseType.h"
#include "Common/GameType.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

class Drawable;
class Player;

/**
	* `-logicthread 1`, the default on Windows, hands every tick to a dedicated thread; outside a network
	* game the render draws beside it (phases 3 and 4), reading only the copies taken at the sync point,
	* and in one the main thread waits for it (phase 5).  No option or saved setting reaches this.
	* `-logicthread 0`, the default off Windows and otherwise for a desync hunt, runs the tick on the main
	* thread.
	* `-logicthread 2` adds jitter to the tick to shake out races.
	*
	* The census counts calls into the client made from the logic thread, one count per channel
	* of LOGIC-THREAD.md section 1's table.  Each count is an entry point phase 2 has to turn into
	* a record replayed at the sync point; the phase 2 exit is every count at zero.  At
	* `-logicthread 0` nothing runs on the logic thread and every count stays zero.
	*/
namespace LogicThread
{

	enum Channel
	{
		CENSUS_DRAWABLE,				///< draw modules told of a transform change from the tick
		CENSUS_MODEL_CONDITION,	///< a model condition flush from the tick
		CENSUS_AUDIO,						///< AudioManager add/remove event
		CENSUS_INGAMEUI,				///< TheInGameUI messages, floating text, selection, superweapons, idle workers
		CENSUS_CONTROLBAR,			///< TheControlBar dirty mark and command lookups
		CENSUS_TACTICALVIEW,		///< TheTacticalView camera moves, shakes, filters, the time freeze reads
		CENSUS_RADAR,						///< TheRadar objects, events, shroud
		CENSUS_FXLIST,					///< FXList::doFXPos / doFXObj
		CENSUS_TERRAINVISUAL,		///< TheTerrainVisual heights, bibs, the water grid
		CENSUS_PARTICLES,				///< ParticleSystemManager create, find, destroy
		CENSUS_GAMECLIENT,			///< TheGameClient destroyDrawable, findDrawableByID, addScorch, setFrame
		CENSUS_EVA,							///< TheEva setShouldPlay
		CENSUS_NEW_DRAWABLE,		///< Drawable::buildRenderObjects, a drawable's render objects built (M4: after the tick for one logic made)
		CENSUS_CLIENT_RANDOM,		///< GameClientRandomValue draws
		CENSUS_GHOST,						///< GhostObjectManager add/remove
		CENSUS_MESSAGESTREAM,		///< MessageStream::appendMessage
		CENSUS_DISPLAY,					///< TheDisplay shroud levels
		CENSUS_JOBS,						///< JobSystem::parallel_for
		CENSUS_ON_DEMAND_FLUSH,	///< Drawable::getDrawModulesFlushed applying deferred work inside the tick
		CENSUS_OBSERVER_LOOKAT,	///< ControlBar::getObserverLookAtPlayer, a logic read of client state (M7)
		CENSUS_DRAWABLE_PUSH,		///< Drawable::doClientWork run inside the tick: hidden, colour, decal, shadows, sub-objects, ambient sound, caption
		CENSUS_DESTROY_DRAWABLE,	///< a drawable deleted: draw modules, render objects, shadows (M4: after the tick for one logic destroyed)
		CENSUS_AUDIO_READ,			///< a logic read of the audio device: sound lengths, a track done, a handle still playing (hazard 4, M7)
		CENSUS_CLIENT_READ,			///< a logic read of other client state: selection, input enabled, camera (hazard 4, M7)
		CENSUS_ASSET_LOAD,			///< the W3D asset manager loading a model or an animation for a cache logic reads (hazards 5 and 22, M7)
		CENSUS_ASSET_LOAD_IN_WINDOW,	///< the CENSUS_ASSET_LOAD calls made while the render's window is open; noteCensus counts it, never place it
		CENSUS_LIVE_MODEL_LOCK,	///< TickReachesLiveModels: the tick taking g_liveModelLock
		CENSUS_COUNT
	};

	/** TRUE on the logic thread, FALSE everywhere else.  Constant-initialised, so a read is one
		* thread-local load. */
	extern thread_local Bool t_isLogicThread;

	void noteCensus( Channel channel );

	inline void census( Channel channel )
	{
		if( t_isLogicThread )
			noteCensus( channel );
	}

	/** LOGIC-THREAD.md phase 3, step 2.  The overlap window is the part of the main thread's pass that
		* will run beside the tick: W3DDisplay::draw from after the particle update to after End_Render
		* (phase 0's "render").  It is marked at every setting, and these accessors of state logic owns
		* count a call made from inside it, one count each, into HEADLESS LOGICTHREAD WINDOW.  Every count
		* is a read the render has to take from a copy made at the sync point before the tick may run
		* beside it.  A call from any other thread or place costs an atomic flag's load. */
	enum WindowRead
	{
		WINDOW_DRAWABLE_OBJECT,	///< Drawable::getObject: the render's way into an Object
		WINDOW_FIND_OBJECT,			///< GameLogic::findObjectByID
		WINDOW_OBJECT_LIST,			///< GameLogic::getFirstObject, a walk of logic's object list
		WINDOW_LOGIC_FRAME,			///< GameLogic::getFrame, which the tick advances; inside the window it answers from takeFrameForDraw's copy and counts nothing
		WINDOW_THING,						///< Thing::getPosition and getTransformMatrix: an Object's, or a Drawable's the tick writes at once
		WINDOW_SHROUD,					///< PartitionData::getShroudedStatus, a cache it fills
		WINDOW_PARTITION,				///< PartitionManager's cell shroud reads
		WINDOW_TERRAIN,					///< TerrainLogic's heights and water
		WINDOW_PLAYER,					///< PlayerList::getNthPlayer
		WINDOW_RADAR_OBJECT,		///< RadarObject::friend_getObject, the radar's list of Objects
		WINDOW_DRAWABLE_FIELD,	///< a drawable's model condition or status bits, which the tick sets beside the render (step 3b); the look is LookForDraw's
		WINDOW_PLAYER_STATE,		///< a player's energy, radar, rank or defeat, which the tick changes (step 3b)
		WINDOW_PARTITION_ITERATE,	///< PartitionManager's iterate core (projected shadow receivers), whose iteration flags the tick's scans share
		WINDOW_AUDIO_HANDLE,		///< AudioManager::allocateNewHandle, the truck draws' sounds: a pool the tick draws from
		WINDOW_COUNT
	};

	/** TRUE while the main thread is inside the window; read first, so every other caller pays this load. */
	extern std::atomic<Int> g_windowOpen;
	void noteWindow( WindowRead read );

	/** TRUE on the main thread inside the window, where a read has to come from a copy. */
	inline Bool isInsideWindow( void )
	{
		return g_windowOpen.load( std::memory_order_relaxed ) && !t_isLogicThread;
	}

	inline void windowRead( WindowRead read )
	{
		if( isInsideWindow() )
			noteWindow( read );
	}

	/** The window's span on the main thread, closed when it goes out of scope (a continue included).
		* Step 3b: at 1 and 2, a window opened with mayOverlap on a pass prepareOverlap found a tick due
		* for hands that tick to the logic thread before it marks itself open, and close waits for it. */
	class OverlapWindow
	{
	public:
		OverlapWindow( void ) : m_open( FALSE ) {}
		~OverlapWindow( void ) { close(); }
		void open( Bool mayOverlap );
		void close( void );
		/** Phase 4, at the end of a picture: TRUE while the tick handed over at the opening still runs and
			* another picture as long as pictureMS, the one just drawn, still ends before the pacer owes the next
			* one (a paced single-player pass).  The window stays open and the draw presents the same state
			* again with only the camera moved.  Under vsync a picture's length includes its wait for the
			* refresh, so a 60 Hz screen beside 60 ticks a second draws none and a faster one draws them on its
			* refreshes.  FALSE closes it as close() does, waiting for the tick. */
		Bool closeOrDrawAgain( Real pictureMS );
	private:
		Bool m_open;
	};

	/** Live render objects between the tick and a fork that poses them.  The sun's shadow pass holds it
		* on the main thread across its pose fork; the tick takes it (TickReachesLiveModels) wherever it
		* reaches a live model through Drawable::getDrawModulesFlushed, which can set a model's state or
		* pose its bones.  Neither side waits for the other while holding it, so neither can deadlock, and
		* a bone read lands at the same pose either side of the fork: posing is a function of the model
		* and the frame's sync time alone.  Recursive, because those reaches nest.  TickReachesLiveModels
		* parks the render (parkRender) before it takes this, so the tick never waits while holding it. */
	extern std::recursive_mutex g_liveModelLock;

	/** The tick's half of g_liveModelLock, after parkRender: the flush behind a reach builds render
		* objects, adds them to the scene and loads assets, none of which may run beside the render.
		* Nothing on any other thread, where the tick is not running beside a fork. */
	class TickReachesLiveModels
	{
	public:
		TickReachesLiveModels( void );
		~TickReachesLiveModels( void );
	private:
		TickReachesLiveModels( const TickReachesLiveModels & );
		TickReachesLiveModels &operator=( const TickReachesLiveModels & );
		Bool m_locked;
	};

	/** LOGIC-THREAD.md phase 3, step 3b, the overlap.  GameClient::update calls this in front of the
		* draw: at 1 and 2 it asks GameEngine::planOverlappedTick, which runs the message stream and the
		* pacer there in a single-player match, whether the pass's first tick is due. */
	void prepareOverlap( void );

	/** R3, on the logic thread before the tick fills a template's bone caches from the W3D asset manager,
		* whose hash tables and reference counts the render uses unlocked, or reaches a live model
		* (TickReachesLiveModels): a tick running beside the render
		* asks it to close its window and waits until it has, so the rest of the tick runs with the main
		* thread waiting for it (one picture's hitch, once per state).  Anywhere else it returns at once. */
	void parkRender( void );

	/** W3DDisplay::draw, after the render's own restore (smooth motion): the tick that ran beside the
		* render is flushed here, as runTick flushes one, and an exception it threw is thrown again. */
	void flushOverlap( void );

	/** GameEngine::update: TRUE once if this pass's first tick ran beside the render. */
	Bool takeOverlappedTick( void );

	/** The last overlapped tick's own length on the logic thread, and how long the main thread waited for it. */
	void takeOverlapTimes( Real &tickMS, Real &waitMS );

	/** Read -logicthread and start the thread if it asks for one.  Called once, from GameEngine::init. */
	void init( void );

	/** Join the thread.  Safe without an init(). */
	void shutdown( void );

	/** Run one TheGameLogic->UPDATE(): on the logic thread with this one waiting, or inline at
		* -logicthread 0 and on the ticks that load or tear down a map, which build windows, the load
		* screen and every render object on the device and stay on the main thread.  An exception
		* the tick throws is thrown again here, on the main thread, where GameEngine::execute catches
		* it as it always did. */
	void runTick( void );

	/** The census and the handoff cost, as HEADLESS LOGICTHREAD lines. */
	void report( void );

	/** LOGIC-THREAD.md phase 2, M5.  The effects a tick asks of the client (sounds, FX lists, particle
		* systems) wait in one queue, in the order the tick asked for them, and runTick runs them on the
		* main thread after the drawables' deferred work, so every drawable they attach to has its render
		* objects.  Keyed on isInGameLogicUpdate past the map load, like the drawables, so -logicthread 0
		* and 1 run one path.  A tick clearing the game runs its calls at once, as a map load does. */
	Bool defersEffects( void );

	/** Inside a tick, queue `call` for after it; anywhere else, run it now. */
	void callAfterTick( const std::function<void()>& call );

	/** LOGIC-THREAD.md phase 2, M6.  The camera calls a tick's scripts make (moves, filters, letterbox,
		* fades, the time freeze) wait in a queue of their own, run first after the tick.  A script that
		* asks the camera back in the same tick whether its move is finished or time frozen reads M7's
		* block, which each queued call updates (noteViewEffect). */
	void callViewAfterTick( const std::function<void()>& call );
	Bool defersView( void );

	/** The tick cleared the game: what it queued has no game left to play in.  GameEngine::reset. */
	void discardEffects( void );

	/** LOGIC-THREAD.md phase 2, M7.  What logic reads of the client, copied by runTick on the main
		* thread before every tick (the sync point), so the tick reads this block and never the client.
		* Between ticks only the player changes these, so the copy holds what a live read in the tick
		* gave.  Inside the tick logic changes some of them itself through calls that now wait for the
		* tick's end: a script selects a unit, starts a camera move.  Those calls write the block too,
		* at the call, so a read later in the same tick sees them as it saw the immediate call. */
	struct SelectedEntry
	{
		DrawableID drawable;
		ObjectID object;
	};
	struct ClientBlock
	{
		std::vector<SelectedEntry> selection;		///< the local selection (InGameUI::m_selectedDrawables)
		UnsignedInt frameSelectionChanged;	///< InGameUI::getFrameSelectionChanged
		Player *observerLookAt;							///< ControlBar::getObserverLookAtPlayer
		Bool cameraFrozen;									///< View::isTimeFrozen
		Bool cameraMoving;									///< View::isCameraMoving
		Bool cameraBlurring;								///< View::isCameraMotionBlurring
	};
	const ClientBlock &clientBlock( void );

	/** A select or deselect the tick queued (InGameUI), written to the block at the call. */
	void noteSelected( DrawableID drawable, ObjectID object, Bool selected );
	void noteSelectionCleared( void );

	/** Drawable::isSelected for logic: inside a tick, whether the block's selection holds the drawable;
		* anywhere else the drawable's own flag, since there a select or deselect runs at the call. */
	Bool isSelectedForTick( const Drawable *draw );

	/** What a queued camera call does to the two answers logic reads (time frozen, movement finished),
		* written to the block at the call.  This is the camera's state machine in four words, not the
		* camera: a move whose path collapses to one point, or a motion blur the device refuses, leaves the
		* real camera finished where the block says moving.  Every setting answers from the block since
		* phase 3: asking the camera meant running its queued calls inside the tick, and phase 2 counted 0
		* of 18680 campaign reads where the two answers differed. */
	enum ViewEffect
	{
		VIEW_MOVES,							///< a move, zoom, pitch or rotation starts
		VIEW_FREEZES_TIME,			///< cameraModFreezeTime
		VIEW_BLUR_STARTS,				///< an in or out motion blur, which counts as a finished movement
		VIEW_BLUR_ENDS					///< a filter mode that replaces it
	};
	void noteViewEffect( ViewEffect effect );
	Bool isCameraMovementFinishedForTick( void );
	Bool isTimeFrozenForTick( void );

}  // namespace LogicThread

#define LOGIC_CENSUS( channel ) LogicThread::census( LogicThread::channel )
#define LOGIC_WINDOW_READ( read ) LogicThread::windowRead( LogicThread::read )

/** LOGIC-THREAD.md phase 2, M6.  First line of a client entry point logic calls: inside a tick the
	* call queues itself (a lambda calling the function again with copies of its arguments) and returns;
	* after the tick the copy runs the body on the main thread.  A void function only. */
#define LOGIC_AFTER_TICK( ... ) \
	if( LogicThread::defersEffects() ) { LogicThread::callAfterTick( __VA_ARGS__ ); return; }

#endif // __LOGICTHREAD_H__
