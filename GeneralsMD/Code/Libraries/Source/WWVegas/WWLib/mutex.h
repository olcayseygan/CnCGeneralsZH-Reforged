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

#ifndef MUTEX_H
#define MUTEX_H

#if defined(_MSC_VER)
#pragma once
#endif

#include "always.h"
#include "thread.h"

#include <atomic>
#include <mutex>


// Always use mutex or critical section when accessing the same data from multiple threads!

// ----------------------------------------------------------------------------
//
// Mutex class is an expensive way of synchronization! Use critical sections
// (below) for all synchronization. Use mutexes for inter-process locking.
//
// ----------------------------------------------------------------------------

/*
	Was a Win32 mutex - CreateMutex, WaitForSingleObject, ReleaseMutex.  std::recursive_timed_mutex
	now (B14), which is the only standard type that answers all three of the things the Win32 one
	did and this interface exposes:

	  recursive   a Win32 mutex is owned by a thread and re-acquiring it from that thread succeeds.
	              Nothing in the tree is known to re-enter one (all nineteen live LockClass sites
	              are in the GameSpy threads and each is a leaf), but this is a public class in a
	              library whose other consumers - WWAudio, and Tools/ - are outside B14's reach,
	              so the mapping preserves the Win32 semantics rather than the current callers'.
	  timed       Lock(int time) takes milliseconds, and callers pass 0 for a poll.  try_lock_for.
	  counted     `locked` is kept, and so is the assert that you cannot destroy a locked mutex.

	WHAT IS LOST: the name.  CreateMutex(NULL,false,name) makes a mutex visible to other processes;
	std::recursive_timed_mutex is in-process only.  Nothing passes a name - every construction in
	the tree takes the NULL default - so nothing changes today, and a caller that starts passing
	one will get a mutex that silently does not synchronise across processes.  The parameter is
	kept so no consumer has to change, and asserts if it is ever used.
*/
class MutexClass
{
	std::recursive_timed_mutex Mutex;
	unsigned locked;

	// Lock and unlock are private so that you can't use them directly. Use LockClass as a sentry instead!
	// Lock returns true if lock was succesful, false otherwise
	bool Lock(int time);
	void Unlock();

public:
	// Name can (and usually should) be NULL. Use name only if you wish to create a globally unique mutex
	MutexClass(const char* name = NULL);
	~MutexClass();

	enum {
		WAIT_INFINITE=-1
	};

	class LockClass
	{
		MutexClass& mutex;
		bool failed;
	public:

		// In order to lock a mutex create a local instance of LockClass with mutex as a parameter.
		// Time is in milliseconds, INFINITE means infinite wait.
		LockClass(MutexClass& m, int time=MutexClass::WAIT_INFINITE);
		~LockClass();

		// Returns true if the lock failed
		bool Failed() { return failed; }
	private:
		LockClass &operator=(const LockClass&) { return(*this); }
	};
	friend class LockClass;
};

// ----------------------------------------------------------------------------
//
// Critical sections are faster than mutex classes and they should be used
// for all synchronization.
//
// ----------------------------------------------------------------------------

/*
	Was a Win32 CRITICAL_SECTION, heap-allocated behind a void*.  std::recursive_mutex now (B14).

	Recursive because CRITICAL_SECTION is, and because this class's consumers are not all in this
	task's reach.  The ones that are were read, all nine of them, and none re-enters:

	  SimpleFileFactoryClass::Mutex   ffactory.cpp 118, 132, 166, 199, 270.  Every locked region is
	                                  StringClass work on the SubDirectory member; the deepest,
	                                  Get_File at :270, calls FileClass::Open/Close on a file it
	                                  has just constructed, which does not come back through the
	                                  factory.  Note the nesting though: StringClass operations
	                                  take StringClass::m_Mutex, a FastCriticalSectionClass, so the
	                                  order is ffactory::Mutex -> StringClass::m_Mutex.  Nothing
	                                  takes them the other way; keep it that way.
	  lzo.cpp's static mutex          :75, :112.  Wraps lzo1x_1_compress / lzo1x_decompress, which
	                                  are leaf C functions over a static work buffer.
	  saveloadstatus.cpp's text_mutex :29, :37.  Two StringClass assignments.

	WWAudio's three sites and texfcach.cpp's one are not built today (neither file is in any
	target's source list; texfcach.cpp's body is inside #ifdef WW3D_DX8, which is defined nowhere).
	They were read anyway and are the same shape.

	A std::mutex would be faster by an owner check.  It would also turn any re-entry - including
	one a future consumer introduces - from a deadlock you can see in a debugger into undefined
	behaviour.  CRITICAL_SECTION was already paying for the owner check.
*/
class CriticalSectionClass
{
	std::recursive_mutex Mutex;
	unsigned locked;

	// Lock and unlock are private so that you can't use them directly. Use LockClass as a sentry instead!
	void Lock();
	void Unlock();

public:
	// Name can (and usually should) be NULL. Use name only if you wish to create a globally unique mutex
	CriticalSectionClass();
	~CriticalSectionClass();

	class LockClass
	{
		CriticalSectionClass& CriticalSection;
	public:
		// In order to lock a mutex create a local instance of LockClass with mutex as a parameter.
		LockClass(CriticalSectionClass& c);
		~LockClass();
	private:
		LockClass &operator=(const LockClass&) { return(*this); }
	};
	friend class LockClass;
};

// ----------------------------------------------------------------------------
//
// Fast critical section is really fast version of CriticalSection. The downside
// of it is that it can't be locked multiple times from the same thread.
//
// ----------------------------------------------------------------------------

/*
	The spin was EA's "lock bts" loop, then MSVC's _interlockedbittestandset intrinsic that
	compiles to the same instruction.  It is std::atomic_flag now (B14), which on x86 compiles to
	lock bts or lock xchg and on arm64 to LDAXR/STXR - the same lock, portably.

	This one needed no recursion decision, and that is worth saying because the other two locks in
	this header did.  The class comment above has always promised it "can't be locked multiple
	times from the same thread", and the old implementation enforced that the hard way: a second
	acquire on the same thread spun on a flag only that thread could clear, forever.  atomic_flag
	does exactly the same thing.  So re-entry deadlocked before and deadlocks now, identically,
	and there is no behaviour here that a recursive replacement could preserve or break.

	The orderings are the ones the instructions already implied.  test_and_set takes acquire, so
	nothing the critical section reads can be hoisted above the acquire; clear() takes release, so
	everything written inside it is visible to the next thread that takes the lock.  The old
	unlock was a plain `cs.Flag=0` store through a non-atomic unsigned, which on x86 happened to
	work because of that architecture's store ordering and would not have on arm64.

	Consumers, all hot: StringClass::m_Mutex (wwstring.cpp:48), WideStringClass::m_TempMutex
	(widestring.cpp:50), FastAllocator's per-size-class array (FastAllocator.h:379) and
	mempool.h's ObjectPoolCS (:100).
*/
class FastCriticalSectionClass
{
	std::atomic_flag Flag = ATOMIC_FLAG_INIT;

public:
	FastCriticalSectionClass() {}

	class LockClass
	{
		FastCriticalSectionClass& cs;
	public:
		WWINLINE LockClass(FastCriticalSectionClass& critical_section) : cs(critical_section)
		{
			while (cs.Flag.test_and_set(std::memory_order_acquire))
				ThreadClass::Switch_Thread();
		}

		~LockClass()
		{
			cs.Flag.clear(std::memory_order_release);
		}

	private:
		LockClass &operator=(const LockClass&);
		LockClass(const LockClass&);
	};

	friend class LockClass;
};

#endif
