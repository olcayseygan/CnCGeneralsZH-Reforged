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

// FILE: JobSystem.cpp ////////////////////////////////////////////////////////////////////////////
// Desc:   THREADING-ROADMAP.md 3.1 - a fork-join pool, and nothing else.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/JobSystem.h"
#include "Common/LogicThread.h"
#include "Common/EarlyCommandLine.h"
#include "GameLogic/FPUControl.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>	// std::system_error, caught below: libc++ brings it in with <thread>, libstdc++ does not
#include <thread>

/*
	This pool was CreateThread, a Win32 semaphore, an auto-reset event and Interlocked* over
	`volatile LONG`.  It is the standard library now (B8), because macOS has none of those and
	because the semantics below are the whole of what the pool needs - there was never anything
	Windows-specific about a fork-join pool.

	The two wakeups are the part worth reading before changing anything.  They are not the same
	primitive, and neither of them is a bare condition_variable notify - which is how a pool like
	this usually gets broken:

	  s_workReady was CreateSemaphore(0, MAX_WORKERS), released N-at-a-time per fork.  What makes
	  it a *counting* semaphore rather than an event is that its permits are **stored**.  A worker
	  that finished the previous fork is not necessarily back at the wait yet - it is somewhere
	  between decrementing s_workersBusy and looping round - and the next fork can start before it
	  gets there.  A permit released into that gap has to still be there when the worker arrives.
	  A plain notify_all has no memory: it wakes whoever is waiting at that instant and is lost on
	  everyone else, so that worker would sleep through a fork it had been counted into,
	  s_workersBusy would never reach zero, and parallel_for would hang forever.

	  So: an integer permit count under the mutex, with the wait keyed on a predicate over it.
	  notify_all then only ever wakes threads up to re-test that predicate; the permit is the
	  state, and the state is what survives the gap.  (C++20's std::counting_semaphore is exactly
	  this and would be the one-liner, but the tree builds as C++17 - see the gameengine target's
	  CXX_STANDARD.)

	  s_allDone was CreateEvent(auto-reset), set by whichever worker decremented s_workersBusy to
	  zero, and waited on by the forking thread alone.  An auto-reset event also stores its signal:
	  if the last worker finishes before the forking thread reaches the wait, the wait returns at
	  once instead of blocking forever.  A predicate wait has that property for free, because the
	  predicate *is* the condition - s_workersBusy == 0 is already true, so the forking thread does
	  not block.  One waiter, so notify_one.

	Ordering, stated once so nobody has to derive it again (the s_ names are the fields of Pool now,
	one set for each pool):

	  - s_fn, s_context, s_count, s_grain and the reset of s_nextIndex are written by the forking
	    thread under s_lock, before the permits are published.  A worker reads them after it has
	    taken s_lock and claimed a permit.  That unlock/lock pair is what publishes them, which is
	    the role ReleaseSemaphore/WaitForSingleObject played before.
	  - s_workersBusy is a plain Int under s_lock although it was an InterlockedDecrement.  It is a
	    condition_variable predicate now, and a predicate has to be read and written under the
	    mutex the waiter holds.  Making it an atomic as well would not remove that requirement; it
	    would only make the code read as though the lock were optional.
	  - s_nextIndex is the one genuinely lock-free thing here, and the only ordering that came out
	    *weaker* than the Win32 original - see the comment inside runChunks.

	The join waits for the workers that came, not for all of them (LOGIC-PARALLEL.md).  A worker
	counts itself into workersBusy when it takes a permit, under the lock, and the forking thread,
	once it finds no item left to claim, withdraws the permits nobody took, under the same lock,
	before it waits.  So a worker either took a permit while the fork was open and is waited for, or
	finds none and goes back to sleep.  It used to be counted in from the start, and on a busy machine
	one not yet scheduled held the join for a scheduler slice; the logic forks every few hundred
	microseconds.  The stored permits above still matter: they are what lets a worker on its way
	back round join a fork that started without it.
*/

namespace
{

	enum { MAX_WORKERS = 31 };		// nothing here scales past it, and the pool is not the bottleneck

	/** One pool: its workers and the fork in flight.  There are two (LOGIC-PARALLEL.md): the main
		* thread's, which the render's particle and shadow forks use, and the logic thread's, so a tick
		* running beside the render forks without waiting for the render's fork or tearing its state
		* (LOGIC-THREAD.md hazard 14).  Each one keeps the whole fork state described above. */
	struct Pool
	{
		/** One lock, three conditions, each with a named predicate.  Nothing runs a job while holding
			* it: it is taken to hand out a permit, to report a worker done, and to report one exited. */
		std::mutex								lock;
		std::condition_variable		workReadyCv;			// predicate: permits > 0 || quitting
		std::condition_variable		allDoneCv;				// predicate: workersBusy == 0
		std::condition_variable		workerExitedCv;		// predicate: workersExited >= numWorkers

		Int		permits = 0;					// stored wakeups; see above for why they have to be counted
		Int		workersBusy = 0;			// workers that took a permit of this fork and are not done with it
		Int		workersExited = 0;		// workers that have left workerMain for good
		Bool	quitting = FALSE;

		std::thread		threads[ MAX_WORKERS ];
		Int						numWorkers = 0;
		Bool					forLogic = FALSE;		// its workers read what the tick reads, not the render's copies

		// The job in flight.  Written by the forking thread before the permits are released and read
		// by the workers after they take one, which is the pairing that publishes them.
		JobSystem::JobFunc	fn = NULL;
		void								*context = NULL;
		std::atomic<Int>		nextIndex{ 0 };
		Int									count = 0;
		Int									grain = 1;
	};

	Pool	s_mainPool;
	Pool	s_logicPool;
	Bool	s_running = FALSE;

	/** The logic thread forks on its own pool, everything else on the main one.  A map's load or
		* teardown tick runs on the main thread and forks there, which is safe because nothing else forks then. */
	Pool &poolForThisThread( void )
	{
		return ( LogicThread::t_isLogicThread || LogicThread::t_isLogicWorker ) ? s_logicPool : s_mainPool;
	}

	// Per-thread, so the allocator can tell whose hand is in the pool without a lock of its own.
	thread_local Bool		s_isWorker = FALSE;
	thread_local Bool		s_inJob = FALSE;
	thread_local Int		s_slot = 0;					// 0 off the pools, 1 to numWorkers on a pool's workers
	std::atomic<Int>		s_workerAllocations( 0 );

	/** Claim chunks of the current job until there are none left.  Every thread in the fork runs
		* this, the forking one included - a pool of N-1 workers plus the caller is N threads of
		* work, and the caller blocking while the workers run would waste the best one of them. */
	void runChunks( Pool &pool )
	{
		for( ;; )
		{
			/* relaxed, and this is the one place the file is weaker than the Win32 original, where
				 InterlockedExchangeAdd was a full barrier.  It is sound because the claim carries no
				 data: read-modify-write operations on a single atomic are totally ordered whatever
				 memory order is asked for, so no index is handed out twice and none is skipped, and
				 every other ordering this pool needs comes from somewhere else - the job's inputs from
				 the permit handover, the job's outputs from the join.  It does rely on JobSystem.h's
				 rule 3, that an item writes only its own slot.  A job that breaks that rule was already
				 broken under the barrier; it is only less likely to have got away with it. */
			const Int start = pool.nextIndex.fetch_add( pool.grain, std::memory_order_relaxed );
			if( start >= pool.count )
				break;

			Int end = start + pool.grain;
			if( end > pool.count )
				end = pool.count;

			for( Int i = start; i < end; ++i )
				pool.fn( i, pool.context );
		}
	}

	void workerMain( Pool *pool, Int slot )
	{
		s_isWorker = TRUE;
		s_slot = slot;
		LogicThread::t_isLogicWorker = pool->forLogic;

		/* The FPU control word is per-thread and a new thread starts on the CRT default, not on
			 what setFPMode() left on the main thread.  A job that computes a float with a different
			 precision or rounding mode than the rest of the game is the kind of bug that shows up as
			 a rare visual difference and never as a crash, so this is not optional even for work
			 that never touches the simulation. */
		setFPMode();

		for( ;; )
		{
			{
				std::unique_lock<std::mutex> lock( pool->lock );
				pool->workReadyCv.wait( lock, [pool]{ return pool->permits > 0 || pool->quitting; } );
				if( pool->quitting )
					break;
				--pool->permits;
				++pool->workersBusy;			// the forking thread waits for this one now
			}

			s_inJob = TRUE;
			runChunks( *pool );
			s_inJob = FALSE;

			Bool lastOut = FALSE;
			{
				std::lock_guard<std::mutex> lock( pool->lock );
				lastOut = (--pool->workersBusy == 0);
			}
			if( lastOut )
				pool->allDoneCv.notify_one();			// exactly one waiter, and it is the forking thread
		}

		{
			std::lock_guard<std::mutex> lock( pool->lock );
			++pool->workersExited;
		}
		pool->workerExitedCv.notify_one();
	}

	void startPool( Pool &pool, Int workers, Bool forLogic )
	{
		/* There is no "could not create the sync objects" path any more.  The Win32 version had to
			 cope with CreateSemaphore or CreateEvent failing and fall back to running inline; a mutex
			 and a condition variable with static storage duration cannot fail to exist, so that branch
			 is gone rather than ported. */
		{
			std::lock_guard<std::mutex> lock( pool.lock );
			pool.quitting = FALSE;
			pool.permits = 0;
			pool.workersBusy = 0;
			pool.workersExited = 0;
		}
		pool.numWorkers = 0;
		pool.forLogic = forLogic;

		for( Int i = 0; i < workers; ++i )
		{
			try
			{
				pool.threads[ i ] = std::thread( workerMain, &pool, i + 1 );
			}
			catch( const std::system_error & )
			{
				break;						// take what we got; the pool is allowed to be smaller than asked for
			}
			++pool.numWorkers;
		}
	}

	void stopPool( Pool &pool )
	{
		/* There is never work in flight here: parallel_for does not return until its fork has joined,
			 so by the time anything can call this every worker is parked on workReadyCv, or on its
			 way back to it with no permit waiting to be taken. */
		{
			std::lock_guard<std::mutex> lock( pool.lock );
			pool.quitting = TRUE;
		}
		pool.workReadyCv.notify_all();

		/* Bounded, not an unconditional join().  This runs on the way out of a process that may be
			 quitting from anywhere, and a worker that somehow never wakes must not be able to hang the
			 exit - the Win32 version waited 5000ms per handle and leaked the handle on a timeout.
			 std::thread has no timed join, so the wait is on the workers' own exit count instead and
			 any that miss it are detached, which is the same trade: a leaked thread rather than a hang.

			 It should be unreachable.  A worker's only blocking point is workReadyCv, it is woken
			 above, and there is no work in flight by construction - so reaching this timeout means a
			 bug in this file rather than a slow machine. */
		Bool allOut = TRUE;
		{
			std::unique_lock<std::mutex> lock( pool.lock );
			allOut = pool.workerExitedCv.wait_for( lock, std::chrono::seconds( 5 ),
																						 [&pool]{ return pool.workersExited >= pool.numWorkers; } );
		}

		for( Int i = 0; i < pool.numWorkers; ++i )
		{
			if( !pool.threads[ i ].joinable() )
				continue;

			if( allOut )
			{
				pool.threads[ i ].join();				// already out of workerMain, so this returns at once
			}
			else
			{
				DEBUG_LOG(("JobSystem: worker %d did not exit, detaching it\n", i));
				pool.threads[ i ].detach();
			}
			pool.threads[ i ] = std::thread();
		}
		pool.numWorkers = 0;

		/* The lock and the condition variables are not destroyed here, unlike the Win32 handles, which
			 shutdown() used to CloseHandle out from under anything still waiting on them.  A detached
			 worker stays parked on a live object until static destruction now, which narrows that
			 window rather than opening a new one. */
	}

}  // anonymous namespace

thread_local Bool LogicThread::t_isLogicWorker = FALSE;

//-------------------------------------------------------------------------------------------------
void JobSystem::init( Int workers )
{
	if( s_running )
		return;

	if( workers < 0 )
	{
		/* -jobthreads <n>, read here rather than from CommandLine.cpp's table because the pool is
			 started before that table runs and because 0 has to be reachable - which is the whole
			 point: it is the single-threaded baseline to measure a threading change against. */
		char value[ 32 ];
		if( findEarlyCommandLineValue( L"-jobthreads", value, sizeof(value) ) )
		{
			workers = atoi( value );
		}
		else
		{
			/* The forking thread works too, so the pool wants one fewer than the machine has.
				 hardware_concurrency() is allowed to answer 0 when it cannot tell, which lands on -1
				 and is clamped to no pool below - the supported single-threaded configuration. */
			workers = (Int)std::thread::hardware_concurrency() - 1;
		}
	}
	if( workers > MAX_WORKERS )
		workers = MAX_WORKERS;
	if( workers < 0 )
		workers = 0;

	// ponytail: two pools of n each oversubscribe the cores while a tick and the render both fork;
	// share one set of workers between them if a measurement ever says the render's short forks hurt
	startPool( s_mainPool, workers, FALSE );
	startPool( s_logicPool, workers, TRUE );

	s_running = TRUE;
	DEBUG_LOG(("JobSystem: %d worker threads a pool, one pool for the main thread and one for the logic thread\n", s_mainPool.numWorkers));
}

//-------------------------------------------------------------------------------------------------
void JobSystem::shutdown( void )
{
	if( !s_running )
		return;

	stopPool( s_mainPool );
	stopPool( s_logicPool );
	s_running = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerCount( void )
{
	return poolForThisThread().numWorkers;
}

//-------------------------------------------------------------------------------------------------
Bool JobSystem::isWorkerThread( void )
{
	return s_isWorker;
}

//-------------------------------------------------------------------------------------------------
Bool JobSystem::isInsideJob( void )
{
	return s_inJob;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::threadSlot( void )
{
	return s_slot;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerAllocationCount( void )
{
	return s_workerAllocations.load( std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
void JobSystem::noteWorkerAllocation( void )
{
	// A diagnostic counter on the allocator's path: it has to be atomic, and it orders nothing.
	s_workerAllocations.fetch_add( 1, std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
void JobSystem::parallel_for( Int count, Int granularity, JobFunc fn, void *context )
{
	if( count <= 0 || fn == NULL )
		return;

	if( granularity < 1 )
		granularity = 1;

	// Each thread forks on its own pool, so the tick and the render may both be in here at once.
	Pool &pool = poolForThisThread();

	/* Inline when there is nobody to hand work to, or when the whole range is one chunk anyway.
		 Same code path as the parallel case would take for a single chunk, so the single-threaded
		 machine is running tested code rather than a second implementation - isInsideJob() included.
		 A fork from inside a job runs inline too: the pool's one fork state is in use. */
	if( pool.numWorkers <= 0 || count <= granularity || s_inJob )
	{
		const Bool wasInJob = s_inJob;
		s_inJob = TRUE;
		for( Int i = 0; i < count; ++i )
			fn( i, context );
		s_inJob = wasInJob;
		return;
	}

	{
		std::lock_guard<std::mutex> lock( pool.lock );
		pool.fn = fn;
		pool.context = context;
		pool.count = count;
		pool.grain = granularity;
		pool.nextIndex.store( 0, std::memory_order_relaxed );
		pool.workersBusy = 0;
		pool.permits = pool.numWorkers;			// one per worker, and they keep until they are taken or withdrawn
	}
	pool.workReadyCv.notify_all();

	s_inJob = TRUE;
	runChunks( pool );		// the forking thread is a worker too
	s_inJob = FALSE;

	/* Wait for the workers that took a permit, not for the work: the chunks can all be claimed by
		 this thread while such a worker is still inside its last item, and returning then would let the
		 caller reuse the output buffers under it.  The permits nobody took are withdrawn first, so a
		 worker that wakes from here on finds none and goes back to sleep. */
	{
		std::unique_lock<std::mutex> lock( pool.lock );
		pool.permits = 0;
		pool.allDoneCv.wait( lock, [&pool]{ return pool.workersBusy == 0; } );
		pool.fn = NULL;
		pool.context = NULL;
	}
}
