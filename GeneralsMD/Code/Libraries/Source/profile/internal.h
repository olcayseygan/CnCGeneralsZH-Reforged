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

/////////////////////////////////////////////////////////////////////////EA-V1
// $File: //depot/GeneralsMD/Staging/code/Libraries/Source/profile/internal.h $
// $Author: mhoffe $
// $Revision: #3 $
// $DateTime: 2003/07/09 10:57:23 $
//
// �2003 Electronic Arts
//
// Internal header
//////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER
#  pragma once
#endif
#ifndef INTERNAL_H // Include guard
#define INTERNAL_H

#include "../debug/debug.h"

#include <atomic>
#include <thread>

#include "internal_funclevel.h"
#include "internal_highlevel.h"
#include "internal_cmd.h"
#include "internal_result.h"

class ProfileFastCS
{
  ProfileFastCS(const ProfileFastCS&);
  ProfileFastCS& operator=(const ProfileFastCS&);

	std::atomic_flag m_Flag = ATOMIC_FLAG_INIT;

	/* The third copy of WWLib's "lock bts" spin (B14 did all three in one commit; the other two
	   are WWLib/mutex.h and WWDebug/wwmemlog.cpp).  std::atomic_flag now, for the same reasons
	   set out at length in mutex.h: same instruction on x86, a real one on arm64, and an unlock
	   that is a release store rather than a plain write to a volatile unsigned.

	   Two things about this copy that the other two do not have.  It is unreachable: nothing in
	   the tree includes internal.h, and nothing names ProfileFastCS.  And its wait was already a
	   no-op - `static HANDLE testEvent` is declared here and defined nowhere, so the `if
	   (testEvent)` was always false and taking the lock under contention was a bare busy spin
	   that would have failed to link the moment anybody used it.  yield() is what the branch was
	   reaching for. */
	void ThreadSafeSetFlag()
	{
		while (m_Flag.test_and_set(std::memory_order_acquire))
			std::this_thread::yield();
	}

	void ThreadSafeClearFlag()
	{
		m_Flag.clear(std::memory_order_release);
	}

public:
	ProfileFastCS(void)
  {
  }

	class Lock
	{
    Lock(const Lock&);
    Lock& operator=(const Lock&);

		ProfileFastCS& CriticalSection;

	public:
		Lock(ProfileFastCS& cs): 
      CriticalSection(cs)
		{
			CriticalSection.ThreadSafeSetFlag();
		}

		~Lock()
		{
			CriticalSection.ThreadSafeClearFlag();
		}
	};

	friend class Lock;
};

void *ProfileAllocMemory(unsigned numBytes);
void *ProfileReAllocMemory(void *oldPtr, unsigned newSize);
void ProfileFreeMemory(void *ptr);

__forceinline void ProfileGetTime(__int64 &t)
{
#if defined(_M_ARM64)
  // Windows on Arm has no __rdtsc.  Profile's start-up calibration (profile.cpp) measures these
  // ticks against QueryPerformanceCounter, so the counter itself serves; _pch.h has windows.h.
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  t = counter.QuadPart;
#else
  t = (__int64)__rdtsc();
#endif
}

#endif // INTERNAL_H
