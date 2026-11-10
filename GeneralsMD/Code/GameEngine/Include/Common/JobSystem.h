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

// FILE: JobSystem.h //////////////////////////////////////////////////////////////////////////////
// Desc:   THREADING-ROADMAP.md 3.1 - a fork-join pool, and nothing else.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __JOBSYSTEM_H__
#define __JOBSYSTEM_H__

#include "Lib/BaseType.h"

/**
	* A deliberately minimal fork-join pool.  One entry point, no task dependencies, no work
	* stealing, no futures - because the only workloads on the roadmap are pure functions over an
	* array (THREADING-ROADMAP.md section 3), and everything past fork-join is code nobody has a
	* use for yet.
	*
	* Three rules the callers have to keep, all of them from section 1 of that document:
	*
	*  1. **A job must not allocate.**  Every MemoryPool in the game shares one critical section
	*     (GameMemory.cpp), so one newInstance per work item turns parallel work back into a queue.
	*     Size and allocate on the calling thread before the fork; workers only write into memory
	*     that already exists.  Violations are counted - workerAllocationCount() - rather than left
	*     to be found later as a mystery perf regression.
	*
	*  2. **A job must not touch simulation state.**  The lockstep sequence is the simulation; a
	*     draw from GameLogicRandomValue or a write an Object reads back desyncs a multiplayer game
	*     hours later with no reproducible case.  A job the logic thread forks may read it, lazy
	*     caches excepted, since the tick waits inside the fork and nothing writes it meanwhile
	*     (LOGIC-PARALLEL.md); what it decides is applied after the join, in item order.
	*
	*  3. **Output order must not depend on scheduling.**  Write into a pre-sized array indexed by
	*     item, or concatenate per-job lists afterwards in item order.  Never append to one shared
	*     list from inside a job.
	*
	* The FPU control word is per-thread and resets to the CRT default on every new thread, so each
	* worker calls setFPMode() once on entry (section 1.3).
	*/
namespace JobSystem
{

	/** The body of one work item.  `index` is the item, `context` is whatever was handed to
		* parallel_for.  A plain function pointer, not a std::function: a std::function that does not
		* fit its small-object buffer allocates, and rule 1 above says jobs do not allocate. */
	typedef void (*JobFunc)( Int index, void *context );

	/** Start the pools: one the logic thread forks on and one for every other thread, so a tick
		* running beside the render forks while the render does (LOGIC-THREAD.md hazard 14).  `workers`
		* is the number of *extra* threads in each, since the calling thread works too.  0 means no
		* pool at all - parallel_for runs inline.  A negative value, the default,
		* asks the command line: `-jobthreads <n>` if it is there, otherwise one fewer than the
		* machine has processors.
		*
		* `-jobthreads 0` is the point of that switch: it is how a threading change is measured
		* against its own single-threaded baseline in the same binary, on the same machine, without
		* a rebuild in between.
		*
		* Calling this twice without a shutdown() in between does nothing the second time. */
	void init( Int workers = -1 );

	/** Stop the pool and join every worker.  Safe to call without an init(), and safe to call
		* while another shutdown is not in flight; there is no work in flight by construction,
		* because parallel_for does not return until its fork has joined. */
	void shutdown( void );

	/** Extra threads in the pool this thread forks on.  0 means parallel_for runs everything inline,
		* which is a supported configuration and the one a single-core machine gets. */
	Int workerCount( void );

	/** Run fn(0..count-1, context) across the pool and return when every item is done.
		*
		* `granularity` is the smallest number of consecutive items a thread claims at once.  It is
		* not a hint that can be ignored: it is how a caller says "an item is cheap, do not pay an
		* interlocked claim for each one".  Anything below 1 is read as 1.
		*
		* With count <= granularity, or with no workers, this runs the whole range inline on the
		* calling thread - the same code path, so a single-threaded machine is a supported build
		* and not an untested one. */
	void parallel_for( Int count, Int granularity, JobFunc fn, void *context );

	/** TRUE on a pool worker, FALSE on the thread that called parallel_for (and everywhere else). */
	Bool isWorkerThread( void );

	/** TRUE while this thread runs items of a parallel_for: a worker, the forking thread working its
		* own fork, and the inline loop with no workers alike, so code that has to be safe beside other
		* items takes the same path at -jobthreads 0 as at 15. */
	Bool isInsideJob( void );

	/** This thread's place in the fork: 0 on the thread that called parallel_for (and every thread off
		* the pools), 1 to workerCount() on a pool's workers.  Per-thread scratch a job writes is indexed by
		* it, sized before the fork for workerCount() + 1 slots.  The two pools number their workers alike,
		* so scratch belongs to whichever pool forks the work that uses it. */
	Int threadSlot( void );

	/** How many times a pool worker has allocated from a MemoryPool since the process started.
		* Rule 1 says this is zero.  It is a count rather than an assert because the Debug config
		* cannot link this tree standalone, so a _DEBUG-only check would never actually run. */
	Int workerAllocationCount( void );

	/** Called by the allocator, not by game code. */
	void noteWorkerAllocation( void );

}  // namespace JobSystem

#endif // __JOBSYSTEM_H__
