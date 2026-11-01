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

// FILE: LogicThread.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   LOGIC-THREAD.md phase 1: the tick on its own thread, serialised with the main thread.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/LogicThread.h"
#include "Common/AudioEventRTS.h"
#include "Common/CriticalSection.h"
#include "Common/EarlyCommandLine.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/Object.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <eh.h>
#endif

thread_local Bool LogicThread::t_isLogicThread = FALSE;
std::atomic<Int> LogicThread::g_windowOpen( 0 );
std::recursive_mutex LogicThread::g_liveModelLock;

namespace
{
	typedef std::chrono::steady_clock LogicClock;

	Bool						s_enabled = FALSE;
	/* A pointer, so a process that ends without ~GameEngine (a RELEASE_CRASH, exit() from a menu)
		 does not run a joinable std::thread's destructor at static teardown, which is std::terminate. */
	std::thread			*s_thread = NULL;

	/* One request and one answer, both under s_lock.  The main thread sets s_tickPending and waits
		 for s_tickDone; the logic thread waits for s_tickPending, runs the tick, sets s_tickDone.  The
		 lock's release and acquire on each side is what publishes everything the other thread wrote
		 before it, which is the whole of the memory model phase 1 needs: one thread runs at a time. */
	std::mutex								s_lock;
	std::condition_variable		s_wakeCv;
	std::condition_variable		s_doneCv;
	Bool											s_tickPending = FALSE;
	Bool											s_tickDone = FALSE;
	Bool											s_quitting = FALSE;
	std::exception_ptr				s_error;
	Real											s_tickMS = 0.0f;		///< the tick as the logic thread timed it
	/* R3's park, under s_lock too: the render inside the window beside a tick (set as startOverlap hands
		 the tick over, cleared as waitOverlap starts to wait), and the tick asking it to close (parkRender). */
	std::condition_variable		s_parkCv;
	Bool											s_renderBeside = FALSE;
	Bool											s_parkWanted = FALSE;

	// Written by the logic thread only, read by the main thread after a handoff has published them.
	Int64				s_census[ LogicThread::CENSUS_COUNT ] = { 0 };

	// Main thread only.
	Int					s_threadedTicks = 0;
	Int					s_inlineTicks = 0;
	Real				s_handoffTotalMS = 0.0f;
	Real				s_handoffMaxMS = 0.0f;
	// The handoff in microseconds, one bucket each up to a millisecond; the last bucket is everything past it.
	const Int		HANDOFF_BUCKETS = 1001;
	Int					s_handoffHistogram[ HANDOFF_BUCKETS ] = { 0 };

	// phase 3 step 2: reads of logic state from inside the overlap window, and how many windows there were
	std::atomic<Int64>	s_windowReads[ LogicThread::WINDOW_COUNT ];
	Int64				s_windows = 0;
	const char *const WINDOW_NAMES[ LogicThread::WINDOW_COUNT ] =
	{
		"drawableobject", "findobject", "objectlist", "logicframe", "thing", "shroud", "partition",
		"terrain", "player", "radarobject", "drawablefield", "playerstate", "partitioniterate", "audiohandle"
	};

	// step 3b, main thread only: the pass's first tick due and allowed beside the render (prepareOverlap),
	// handed over (the window open), waited for (the window closed), flushed, and the times of the last one
	Bool				s_overlapWanted = FALSE;
	Bool				s_overlapRunning = FALSE;
	Bool				s_overlapDone = FALSE;
	Bool				s_overlapTaken = FALSE;
	std::exception_ptr	s_overlapError;
	LogicClock::time_point	s_overlapStart;
	Real				s_overlapTickMS = 0.0f;
	Real				s_overlapSyncMS = 0.0f;		///< how far into the pass this tick started
	Real				s_overlapWaitMS = 0.0f;
	Int					s_overlappedTicks = 0;
	Real				s_overlapWaitTotalMS = 0.0f;
	Int					s_extraPictures = 0;		///< phase 4: pictures drawn again while a tick ran on (closeOrDrawAgain)
	// the wait in tenths of a millisecond up to 50, the last bucket everything past it
	const Int		WAIT_BUCKETS = 501;
	Int					s_waitHistogram[ WAIT_BUCKETS ] = { 0 };

	// -logicthread 2: 1 with a jitter on both sides of the handoff, so the races step 3 must not have
	// meet more orders.  A generator per thread and of its own: nothing here may touch a game stream.
	Int					s_mode = 0;
	void jitter( void )
	{
		if( s_mode < 2 )
			return;
		static thread_local std::minstd_rand random( (unsigned)LogicClock::now().time_since_epoch().count() );
		std::this_thread::sleep_for( std::chrono::microseconds( random() % 2000 ) );
	}

	const char *const CHANNEL_NAMES[ LogicThread::CENSUS_COUNT ] =
	{
		"drawable", "modelcondition", "audio", "ingameui", "controlbar", "tacticalview", "radar",
		"fxlist", "terrainvisual", "particles", "gameclient", "eva", "newdrawable", "clientrandom",
		"ghost", "messagestream", "display", "jobs", "ondemandflush", "observerlookat",
		"drawablepush", "destroydrawable", "audioread", "clientread", "assetload", "assetloadinwindow",
		"livemodellock"
	};

	// M5: what the tick asked of the audio, the FX lists and the particle systems, in call order.
	// Filled inside the tick, run and emptied by flushAfterTick; one thread touches it at a time.
	std::vector< std::function<void()> > s_effects;

	// M6: the camera calls of the tick, see callViewAfterTick
	std::vector< std::function<void()> > s_viewCalls;

	// M7: what the tick reads of the client, see LogicThread::ClientBlock
	LogicThread::ClientBlock s_block;

	/* The sync point: the main thread, before the tick, with every call the last tick queued already
		 run.  So the block holds what the tick's first read of the client would have returned. */
	void captureClientBlock( void )
	{
		s_block.selection.clear();
		const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
		for( DrawableListCIt it = selected->begin(); it != selected->end(); ++it )
		{
			LogicThread::SelectedEntry entry = { (*it)->getID(), (*it)->getObject()->getID() };
			s_block.selection.push_back( entry );
		}
		s_block.frameSelectionChanged = TheInGameUI->getFrameSelectionChanged();
		s_block.observerLookAt = TheControlBar->getObserverLookAtPlayer();
		s_block.cameraFrozen = TheTacticalView->isTimeFrozen();
		s_block.cameraMoving = TheTacticalView->isCameraMoving();
		s_block.cameraBlurring = TheTacticalView->isCameraMotionBlurring();
		TheAudio->captureForLogic();
	}

	void runViewCalls( void )
	{
		// swapped out first: a call that queues another (none does today) waits for the next run
		std::vector< std::function<void()> > calls;
		calls.swap( s_viewCalls );
		for( size_t i = 0; i < calls.size(); ++i )
			calls[ i ]();
	}

	/* Everything a tick left for the main thread, in the order it has to run: the drawables' work first
		 (M1, M3, M4 builds the render objects of the drawables the tick made), the camera (M6), then the
		 effects (M5, and M6's interface, radar, EVA, display and terrain pushes), which attach to those
		 drawables and read their bones, then the deletion of the drawables the tick destroyed, whose
		 bones a death effect may still have read, then the control bar (M2). */
	void flushAfterTick( void )
	{
		// M7: the frame the tick ran, which it used to hand the client itself; a tick that started a map
		// ran frame 0 after the load, one that cleared the game left 0
		TheGameClient->setFrame( TheGameLogic->getFrameForClient() );
		// phase 3: the client's list and lookup take what the tick made and destroyed, before anything
		// below looks a drawable up
		TheGameClient->linkDrawablesOfTick();
		Drawable::flushPendingClientWork();
		runViewCalls();
		// an effect queues nothing more: none of them runs inside a tick.  Swapped out first, as the view
		// calls are: an effect that pumps a frame (a modal message box) or throws ran the effects before it
		// a second time, and an appended message twice was a double delete in the message stream
		std::vector< std::function<void()> > effects;
		effects.swap( s_effects );
		for( size_t i = 0; i < effects.size(); ++i )
			effects[ i ]();
		AudioEventRTS::forgetHeldCallers();
		TheGameClient->flushDestroyedDrawables();
		ControlBar::flushLogicCalls();
	}

	Real elapsedMS( LogicClock::time_point a, LogicClock::time_point b )
	{
		return std::chrono::duration<Real, std::milli>( b - a ).count();
	}

#if defined(_WIN32)
	/* WinMain's _set_se_translator covers only the thread that set it.  Without it here a fault in the
		 tick reached the catch below as a bare structured exception and the dump showed the main thread's
		 stack after the rethrow, not the fault's. */
	_se_translator_function s_seTranslator = NULL;
#endif

	void threadMain( void )
	{
#if defined(_WIN32)
		_set_se_translator( s_seTranslator );
#endif
		LogicThread::t_isLogicThread = TRUE;
		// GameLogic::update sets it again on entry; a new thread starts on the CRT default.
		setFPMode();

		for( ;; )
		{
			{
				std::unique_lock<std::mutex> lock( s_lock );
				s_wakeCv.wait( lock, []{ return s_tickPending || s_quitting; } );
				if( s_quitting )
					break;
				s_tickPending = FALSE;
			}
			jitter();

			const LogicClock::time_point t0 = LogicClock::now();
			std::exception_ptr error;
			try
			{
				TheGameLogic->UPDATE();
			}
			catch( ... )
			{
				error = std::current_exception();
			}
			const LogicClock::time_point t1 = LogicClock::now();

			{
				std::lock_guard<std::mutex> lock( s_lock );
				s_error = error;
				s_tickMS = elapsedMS( t0, t1 );
				s_tickDone = TRUE;
			}
			s_doneCv.notify_one();
		}
	}

	/* A tick that starts or ends a map builds the load screen, pushes shell windows, pumps the window
		 messages and creates every render object of the map on the device.  The plan's invariant puts
		 loading and teardown at the sync point with the logic thread parked, and in phase 1 that means
		 these ticks run where they always ran. */
	Bool tickNeedsMainThread( void )
	{
		return TheGameLogic->isNewGamePending()
			|| TheCommandList->containsMessageOfType( GameMessage::MSG_NEW_GAME )
			|| TheCommandList->containsMessageOfType( GameMessage::MSG_CLEAR_GAME_DATA );
	}

	Int handoffPercentileMicros( Real fraction )
	{
		const Int want = (Int)( s_threadedTicks * fraction );
		Int seen = 0;
		for( Int i = 0; i < HANDOFF_BUCKETS; ++i )
		{
			seen += s_handoffHistogram[ i ];
			if( seen > want )
				return i;
		}
		return HANDOFF_BUCKETS - 1;
	}

}  // anonymous namespace

//-------------------------------------------------------------------------------------------------
void LogicThread::noteCensus( Channel channel )
{
	++s_census[ channel ];
	// R3: a load beside the render touches the asset manager and the reference counts the render uses;
	// one with the window closed has the main thread waiting.  A stale relaxed read only counts too many.
	if( channel == CENSUS_ASSET_LOAD && g_windowOpen.load( std::memory_order_relaxed ) )
		++s_census[ CENSUS_ASSET_LOAD_IN_WINDOW ];
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteWindow( WindowRead read )
{
	s_windowReads[ read ].fetch_add( 1, std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
/* The handshake runs under s_lock, never on g_windowOpen's relaxed stores: the main thread clears
	 s_renderBeside in waitOverlap after its last render call, so the lock's release and acquire publish
	 everything the render did before the tick goes on, and the main thread then waits for the tick.

	 No deadlock.  The tick waits only for the main thread to reach waitOverlap, which it does when the
	 picture it is drawing ends (closeOrDrawAgain sees s_parkWanted and closes) or at once between
	 pictures.  Inside the window the main thread waits on nothing of the tick's but g_liveModelLock (the
	 shadow pass's pose fork, W3DVolumetricShadow.cpp), released before End_Render, and the tick parks
	 before it takes that lock (TickReachesLiveModels), never while it holds it. */
void LogicThread::parkRender( void )
{
	if( !t_isLogicThread )
		return;
	std::unique_lock<std::mutex> lock( s_lock );
	if( !s_renderBeside )
		return;
	s_parkWanted = TRUE;
	s_doneCv.notify_one();		// the main thread may be in tickOutlastsNextPicture
	s_parkCv.wait( lock, []{ return !s_renderBeside; } );
}

//-------------------------------------------------------------------------------------------------
LogicThread::TickReachesLiveModels::TickReachesLiveModels( void ) : m_locked( t_isLogicThread )
{
	if( !m_locked )
		return;
	noteCensus( CENSUS_LIVE_MODEL_LOCK );
	// the flush behind the reach can build render objects, add them to the scene and load from the asset
	// manager, and the bone read poses the live model: none of it beside the render
	parkRender();
	g_liveModelLock.lock();
}

//-------------------------------------------------------------------------------------------------
LogicThread::TickReachesLiveModels::~TickReachesLiveModels( void )
{
	if( !m_locked )
		return;
	g_liveModelLock.unlock();
}

//-------------------------------------------------------------------------------------------------
/* Step 3b: the pass's first tick to the logic thread, at the sync point's end.  Everything the render
	 reads of logic was taken before this (takeReadsForRender), and the client block the tick reads is
	 taken here, as runTick takes it. */
static void startOverlap( void )
{
	if( tickNeedsMainThread() )
		return;
	captureClientBlock();
	TheGameClient->setFrame( TheGameLogic->getFrame() );
	TheGameLogic->growObjectLookupForTick();
	jitter();
	s_overlapSyncMS = GameEngine_msSincePassStart();
	s_overlapStart = LogicClock::now();
	// open before the tick wakes, so nothing the main thread does beside it reads as outside the window
	LogicThread::g_windowOpen.store( 1, std::memory_order_relaxed );
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_tickPending = TRUE;
		s_renderBeside = TRUE;
	}
	s_wakeCv.notify_one();
	s_overlapRunning = TRUE;
}

//-------------------------------------------------------------------------------------------------
static void waitOverlap( void )
{
	const LogicClock::time_point t0 = LogicClock::now();
	std::exception_ptr error;
	Real tickMS;
	{
		std::unique_lock<std::mutex> lock( s_lock );
		// the render is done with the window: a tick parked in parkRender goes on with this thread waiting
		s_renderBeside = FALSE;
		s_parkWanted = FALSE;
		s_parkCv.notify_one();
		s_doneCv.wait( lock, []{ return s_tickDone; } );
		s_tickDone = FALSE;
		error = s_error;
		s_error = NULL;
		tickMS = s_tickMS;
	}
	const LogicClock::time_point t1 = LogicClock::now();
	jitter();
	// as runTick: the main thread's FPU as an inline tick leaves it
	setFPMode();

	s_overlapRunning = FALSE;
	s_overlapDone = TRUE;
	s_overlapError = error;
	s_overlapTickMS = tickMS;
	s_overlapWaitMS = elapsedMS( t0, t1 );
	++s_overlappedTicks;
	s_overlapWaitTotalMS += s_overlapWaitMS;
	Int bucket = (Int)( s_overlapWaitMS * 10.0f );
	if( bucket >= WAIT_BUCKETS )
		bucket = WAIT_BUCKETS - 1;
	++s_waitHistogram[ bucket ];
}

//-------------------------------------------------------------------------------------------------
void LogicThread::OverlapWindow::open( Bool mayOverlap )
{
	if( mayOverlap && s_overlapWanted )
		startOverlap();
	s_overlapWanted = FALSE;
	m_open = TRUE;
	++s_windows;
	g_windowOpen.store( 1, std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
/* Behind the clock the next tick is due the whole time, so the rule in closeOrDrawAgain never draws
	 again and the eye waits on the tick: sync + a 13 ms tick is an 18 ms frame on the 2000 object crowd
	 with 4 ms of picture in it.  Wait for the tick only as long as the sync point and a picture still
	 fit in a 60 Hz frame after it, then draw once more, timed to end as the last tick's length says this
	 one does, since a picture still going when the tick ends holds the next tick up.  A picture that
	 fills the frame by itself (a slow card) gains nothing from a second one.  A present that waits for
	 the refresh never gets one either: the sync point ate the start of that refresh, so the picture's
	 own length looks like room, and the second picture's whole refresh halved the game's speed. */
static Bool tickOutlastsNextPicture( Real pictureMS )
{
#if defined(_WIN32)
	const Bool presentWaitsForRefresh = TheGlobalData->m_vsync;
#else
	const Bool presentWaitsForRefresh = TRUE;	// the SDL GPU swapchain's VSYNC, whatever was asked for
#endif
	if( presentWaitsForRefresh || !TheGameEngine->isPassPacedByClock() )
		return FALSE;
	const Real PICTURE_BUDGET_MS = 15.0f;		// 16.7 less the flush and the sync point's slower passes
	if( pictureMS >= PICTURE_BUDGET_MS )
		return FALSE;
	const Real latestStartMS = PICTURE_BUDGET_MS - s_overlapSyncMS - pictureMS;
	const Real tickLeftMS = s_overlapTickMS - elapsedMS( s_overlapStart, LogicClock::now() );
	const Real startMS = tickLeftMS <= latestStartMS ? latestStartMS : std::min( latestStartMS, tickLeftMS - pictureMS );
	// whole microseconds: a float duration added to the steady clock rounds away the wait
	const std::chrono::microseconds wait( (long long)( std::max( startMS, 0.0f ) * 1000.0f ) );
	std::unique_lock<std::mutex> lock( s_lock );
	return !s_doneCv.wait_for( lock, wait, []{ return s_tickDone || s_parkWanted; } );
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::OverlapWindow::closeOrDrawAgain( Real pictureMS )
{
	if( m_open && s_overlapRunning )
	{
		Bool running;
		{
			std::lock_guard<std::mutex> lock( s_lock );
			// a tick parked in parkRender waits for this window to close
			running = !s_tickDone && !s_parkWanted;
		}
		// the plan's rule, wait only once the next tick is due, with the next picture's length taken off
		// so a picture never holds the next tick up; the last picture stands in for the next one
		if( running && ( TheGameEngine->msUntilNextTickDue() > pictureMS || tickOutlastsNextPicture( pictureMS ) ) )
		{
			++s_extraPictures;
			return TRUE;
		}
	}
	close();
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::OverlapWindow::close( void )
{
	if( !m_open )
		return;
	m_open = FALSE;
	g_windowOpen.store( 0, std::memory_order_relaxed );
	if( s_overlapRunning )
		waitOverlap();
	// the tick no longer reads the manager's list and lookup
	TheParticleSystemManager->friend_fileWindowSystems();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::prepareOverlap( void )
{
	s_overlapWanted = s_enabled && TheGameEngine->planOverlappedTick();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::flushOverlap( void )
{
	if( !s_overlapDone )
		return;
	s_overlapDone = FALSE;
	s_overlapTaken = TRUE;
	if( s_overlapError )
	{
		std::exception_ptr error = s_overlapError;
		s_overlapError = NULL;
		std::rethrow_exception( error );
	}
	flushAfterTick();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::takeOverlappedTick( void )
{
	// a draw that left before its flush (none does today) still has the tick flushed before the engine goes on
	flushOverlap();
	const Bool taken = s_overlapTaken;
	s_overlapTaken = FALSE;
	s_overlapWanted = FALSE;
	return taken;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::takeOverlapTimes( Real &tickMS, Real &waitMS )
{
	tickMS = s_overlapTickMS;
	waitMS = s_overlapWaitMS;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::init( void )
{
	// the command line has the last word.  Off Windows the tick stays on the main thread: a macOS std::thread
	// has a 512 KB stack, and a tick needs more
#if defined(_WIN32)
	char value[ 32 ] = "1";
#else
	char value[ 32 ] = "0";
#endif
	findEarlyCommandLineValue( L"-logicthread", value, sizeof(value) );
	if( atoi( value ) == 0 )
		return;

	s_enabled = TRUE;
	s_mode = atoi( value );
#if defined(_WIN32)
	// there is no getter: setting returns the one before, which goes straight back
	s_seTranslator = _set_se_translator( NULL );
	_set_se_translator( s_seTranslator );
#endif
	s_thread = new std::thread( threadMain );
	DEBUG_LOG(( "LOGICTHREAD: the tick runs on its own thread, the main thread waiting for it\n" ));
}

//-------------------------------------------------------------------------------------------------
void LogicThread::shutdown( void )
{
	if( s_thread == NULL )
		return;
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_quitting = TRUE;
	}
	s_wakeCv.notify_one();
	s_thread->join();
	delete s_thread;
	s_thread = NULL;
	s_enabled = FALSE;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::runTick( void )
{
	captureClientBlock();
	// the client's clock reads the frame the tick runs, as GameLogic::update set it at its top
	TheGameClient->setFrame( TheGameLogic->getFrame() );

	if( !s_enabled || tickNeedsMainThread() )
	{
		++s_inlineTicks;
		TheGameLogic->UPDATE();
		flushAfterTick();
		return;
	}

	const LogicClock::time_point t0 = LogicClock::now();
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_tickPending = TRUE;
	}
	s_wakeCv.notify_one();

	std::exception_ptr error;
	Real tickMS;
	{
		std::unique_lock<std::mutex> lock( s_lock );
		s_doneCv.wait( lock, []{ return s_tickDone; } );
		s_tickDone = FALSE;
		error = s_error;
		s_error = NULL;
		tickMS = s_tickMS;
	}
	const LogicClock::time_point t1 = LogicClock::now();
	jitter();

	// The inline tick left this thread's FPU in setFPMode's state; keep the main thread as it was at 0.
	setFPMode();

	// What the thread costs: the wait less the tick the logic thread timed for itself.
	Real handoffMS = elapsedMS( t0, t1 ) - tickMS;
	if( handoffMS < 0.0f )
		handoffMS = 0.0f;
	++s_threadedTicks;
	s_handoffTotalMS += handoffMS;
	if( handoffMS > s_handoffMaxMS )
		s_handoffMaxMS = handoffMS;
	Int bucket = (Int)( handoffMS * 1000.0f );
	if( bucket >= HANDOFF_BUCKETS )
		bucket = HANDOFF_BUCKETS - 1;
	++s_handoffHistogram[ bucket ];

	if( error )
		std::rethrow_exception( error );

	// What the tick deferred on the drawables (phase 2, M1), the effects (M5) and the control bar (M2),
	// here on the main thread at both settings.
	flushAfterTick();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::defersEffects( void )
{
	/* Nor while the game is cleared.  That tick runs on the main thread (tickNeedsMainThread), and
		 GameEngine::reset discards the queue: the closers clearGameData and the resets call were lost
		 with it, which left the victory banner and a script's popup message over the shell. */
	return TheGameLogic != NULL && TheGameLogic->isInGameLogicUpdate() && !TheGameLogic->isLoadingMap()
		&& !TheGameLogic->isClearingGameData();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::callAfterTick( const std::function<void()>& call )
{
	if( defersEffects() )
		s_effects.push_back( call );
	else
		call();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::defersView( void )
{
	return defersEffects();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::callViewAfterTick( const std::function<void()>& call )
{
	if( defersView() )
		s_viewCalls.push_back( call );
	else
		call();
}

//-------------------------------------------------------------------------------------------------
const LogicThread::ClientBlock &LogicThread::clientBlock( void )
{
	return s_block;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteSelected( DrawableID drawable, ObjectID object, Bool selected )
{
	std::vector<SelectedEntry>::iterator it = s_block.selection.begin();
	while( it != s_block.selection.end() && it->drawable != drawable )
		++it;
	if( selected == ( it != s_block.selection.end() ) )
		return;
	if( selected )
	{
		SelectedEntry entry = { drawable, object };
		s_block.selection.push_back( entry );
	}
	else
		s_block.selection.erase( it );
	s_block.frameSelectionChanged = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteSelectionCleared( void )
{
	if( s_block.selection.empty() )
		return;
	s_block.selection.clear();
	s_block.frameSelectionChanged = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::isSelectedForTick( const Drawable *draw )
{
	if( !defersEffects() )
		return draw->isSelected();
	const DrawableID id = draw->getID();
	for( size_t i = 0; i < s_block.selection.size(); ++i )
		if( s_block.selection[ i ].drawable == id )
			return TRUE;
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::noteViewEffect( ViewEffect effect )
{
	switch( effect )
	{
		case VIEW_MOVES:					s_block.cameraMoving = TRUE;		break;
		case VIEW_FREEZES_TIME:		s_block.cameraFrozen = TRUE;		break;
		case VIEW_BLUR_STARTS:		s_block.cameraBlurring = TRUE;	break;
		case VIEW_BLUR_ENDS:			s_block.cameraBlurring = FALSE;	break;
	}
}

//-------------------------------------------------------------------------------------------------
/* Phase 3: the block at both settings.  Asking the camera meant running the tick's queued camera calls
	 inside the tick, on the camera the render reads; phase 2's three campaign missions found 0 reads,
	 of 18680, where the camera and the block disagreed. */
Bool LogicThread::isCameraMovementFinishedForTick( void )
{
	return s_block.cameraBlurring || !s_block.cameraMoving;
}

//-------------------------------------------------------------------------------------------------
Bool LogicThread::isTimeFrozenForTick( void )
{
	return s_block.cameraFrozen;
}

//-------------------------------------------------------------------------------------------------
void LogicThread::discardEffects( void )
{
	s_effects.clear();
	s_viewCalls.clear();
	AudioEventRTS::forgetHeldCallers();
}

//-------------------------------------------------------------------------------------------------
void LogicThread::report( void )
{
	DEBUG_LOG(( "HEADLESS LOGICTHREAD: %s | %d ticks on the logic thread, %d on the main thread | handoff %.3f/%.3f/%.3f max %.3f ms mean/p50/p99\n",
		s_enabled ? "on" : "off", s_threadedTicks, s_inlineTicks,
		s_threadedTicks ? s_handoffTotalMS / s_threadedTicks : 0.0f,
		handoffPercentileMicros( 0.5f ) / 1000.0f, handoffPercentileMicros( 0.99f ) / 1000.0f, s_handoffMaxMS ));

	// step 3b: the ticks that ran beside the render, and how long the render's end waited for them
	{
		Int waitP50 = 0, waitP99 = 0, seen = 0;
		for( Int i = 0; i < WAIT_BUCKETS; ++i )
		{
			if( seen <= s_overlappedTicks / 2 && seen + s_waitHistogram[ i ] > s_overlappedTicks / 2 )
				waitP50 = i;
			if( seen <= (Int)( s_overlappedTicks * 0.99f ) && seen + s_waitHistogram[ i ] > (Int)( s_overlappedTicks * 0.99f ) )
				waitP99 = i;
			seen += s_waitHistogram[ i ];
		}
		DEBUG_LOG(( "HEADLESS LOGICTHREAD OVERLAP: %d ticks beside the render | wait %.2f/%.1f/%.1f ms mean/p50/p99 | %d pictures drawn again while one ran\n",
			s_overlappedTicks, s_overlappedTicks ? s_overlapWaitTotalMS / s_overlappedTicks : 0.0f,
			waitP50 / 10.0f, waitP99 / 10.0f, s_extraPictures ));
	}

	char line[ 1024 ];
	Int used = 0;
	for( Int i = 0; i < CENSUS_COUNT; ++i )
		used += snprintf( line + used, sizeof(line) - used, "%s%s %lld", i ? " | " : "", CHANNEL_NAMES[ i ], (long long)s_census[ i ] );
	DEBUG_LOG(( "HEADLESS LOGICTHREAD CENSUS: %s\n", line ));
	used = 0;
	for( Int i = 0; i < WINDOW_COUNT; ++i )
		used += snprintf( line + used, sizeof(line) - used, " | %s %lld", WINDOW_NAMES[ i ], (long long)s_windowReads[ i ].load() );
	DEBUG_LOG(( "HEADLESS LOGICTHREAD WINDOW: %lld windows%s\n", (long long)s_windows, line ));

	// hazard 13: the pools' one lock, and the dynamic allocator's in front of global new
	DEBUG_LOG(( "HEADLESS LOGICTHREAD LOCKS: memory pool %llu contended enters, dma %llu\n",
		TheMemoryPoolCriticalSection ? TheMemoryPoolCriticalSection->getContendedCount() : 0ull,
		TheDmaCriticalSection ? TheDmaCriticalSection->getContendedCount() : 0ull ));
}
