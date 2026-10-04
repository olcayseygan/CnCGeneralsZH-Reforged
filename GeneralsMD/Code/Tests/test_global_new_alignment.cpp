/*
**	Copyright 2026 levent
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

/* The global operator new that GameMemory.cpp defines, against the alignment the C++ ABI promises for
	 it: __STDCPP_DEFAULT_NEW_ALIGNMENT__, 16 on x86-64 and arm64.

	 On Linux the executable's operator new is the one every shared library in the process gets, Mesa's
	 Vulkan drivers and the libLLVM they load among them.  The memory manager's blocks are 4-byte
	 aligned (MEM_BOUND_ALIGNMENT), and libLLVM's static constructors store to what they get with movaps:
	 the game died with SIGSEGV inside SDL_CreateGPUDevice on a Radeon Vega with Pardus 23 (Debian 12,
	 Mesa 22.3), or ran, depending on where the blocks happened to land.  The real GameMemory.cpp and MemoryInit.cpp, and the
	 strings they name. */

#include "PreRTS.h"

#include "Common/CriticalSection.h"
#include "Common/ExecutableDirectory.h"
#include "Common/GameMemory.h"
#include "Common/JobSystem.h"

#include <stdint.h>
#include <stdio.h>
#include <new>

static int s_failed = 0;
#define CHECK( x ) do { if (!(x)) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x ); ++s_failed; } } while (0)

static bool aligned( const void *p )
{
	return ((uintptr_t)p % __STDCPP_DEFAULT_NEW_ALIGNMENT__) == 0;
}

int main( void )
{
	initMemoryManager();

	// Every size a small object comes in, and some past the memory manager's sub-pools.
	static const size_t sizes[] = { 1, 2, 3, 4, 5, 7, 8, 12, 16, 20, 24, 28, 32, 40, 48, 64, 100, 128,
		256, 1000, 4096, 100000 };
	enum { COUNT = sizeof(sizes) / sizeof(sizes[0]) };
	void *single[COUNT];
	void *array[COUNT];
	for (int i = 0; i < COUNT; ++i)
	{
		single[i] = ::operator new( sizes[i] );
		array[i] = ::operator new[]( sizes[i] );
		void *nothrow = ::operator new( sizes[i], std::nothrow );
		CHECK( aligned( single[i] ) );
		CHECK( aligned( array[i] ) );
		CHECK( aligned( nothrow ) );
		::operator delete( nothrow );
	}
	for (int i = 0; i < COUNT; ++i)
	{
		::operator delete( single[i] );
		::operator delete[]( array[i] );
	}

	printf( "%s (%d failed)\n", s_failed ? "FAILED" : "OK", s_failed );
	return s_failed ? 1 : 0;
}

// What GameMemory.cpp, MemoryInit.cpp and the strings name that this test does not build.
CriticalSection *TheUnicodeStringCriticalSection = NULL;	// ScopedCriticalSection skips a null one
CriticalSection *TheDmaCriticalSection = NULL;
CriticalSection *TheMemoryPoolCriticalSection = NULL;
Bool JobSystem::isWorkerThread() { return FALSE; }
void JobSystem::noteWorkerAllocation() {}
void getExecutableDirectory( char *buf, size_t size, Bool ) { if (size > 0) buf[0] = 0; }	// no pool-size override
#ifdef ALLOW_DEBUG_UTILS
DEBUG_EXTERN_C void DebugInit( int ) {}
#endif
#ifdef DEBUG_LOGGING
#include <stdarg.h>
DEBUG_EXTERN_C void DebugLog( const char *format, ... )
{
	va_list args;
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
}
#endif
