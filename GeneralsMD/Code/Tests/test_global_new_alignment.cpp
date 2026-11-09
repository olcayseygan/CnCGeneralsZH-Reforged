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
	 strings they name.

	 macOS frameworks can also bind a system-allocated block to the executable's operator delete,
	 or use the system deallocator on a block our global new returned. These forms must interoperate
	 without consulting a DMA header belonging only to explicit engine pools. */

#include "PreRTS.h"

#include "Common/CriticalSection.h"
#include "Common/ExecutableDirectory.h"
#include "Common/GameMemory.h"
#include "Common/JobSystem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

// Apple libc++ does not declare sized delete for every deployment target; the
// executable supplies these C++14 forms in GameMemory.cpp on POSIX.
void operator delete(void *p, size_t size) WW_NOEXCEPT_DELETE;
void operator delete[](void *p, size_t size) WW_NOEXCEPT_DELETE;

static int s_failed = 0;
#define CHECK( x ) do { if (!(x)) { printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x ); ++s_failed; } } while (0)

static bool aligned( const void *p )
{
	return ((uintptr_t)p % __STDCPP_DEFAULT_NEW_ALIGNMENT__) == 0;
}

static bool zeroed( const void *p, size_t size )
{
	if (p == NULL)
		return false;
	const unsigned char *bytes = (const unsigned char *)p;
	for (size_t i = 0; i < size; ++i)
		if (bytes[i] != 0)
			return false;
	return true;
}

int main( void )
{
	initMemoryManager();

	// Every size a small object comes in, and some past the memory manager's sub-pools.
	static const size_t sizes[] = { 0, 1, 2, 3, 4, 5, 7, 8, 12, 16, 20, 24, 28, 32, 40, 48, 64, 100, 128,
		256, 1000, 4096, 100000 };
	enum { COUNT = sizeof(sizes) / sizeof(sizes[0]) };
	void *single[COUNT];
	void *array[COUNT];
	for (int i = 0; i < COUNT; ++i)
	{
		single[i] = ::operator new( sizes[i] );
		array[i] = ::operator new[]( sizes[i] );
		void *nothrow = ::operator new( sizes[i], std::nothrow );
		void *nothrowArray = ::operator new[]( sizes[i], std::nothrow );
		CHECK( aligned( single[i] ) );
		CHECK( aligned( array[i] ) );
		CHECK( aligned( nothrow ) );
		CHECK( aligned( nothrowArray ) );
		CHECK( zeroed( single[i], sizes[i] ) );
		CHECK( zeroed( array[i], sizes[i] ) );
		CHECK( zeroed( nothrow, sizes[i] ) );
		CHECK( zeroed( nothrowArray, sizes[i] ) );
		::operator delete( nothrow );
		::operator delete[]( nothrowArray, std::nothrow );
	}
	for (int i = 0; i < COUNT; ++i)
	{
		::operator delete( single[i] );
		::operator delete[]( array[i] );
	}

	// A system C++ runtime may obtain storage through malloc while binding its
	// deletes to the executable. Exercise every deallocation signature with that
	// storage, including the debug forms, rather than passing a pool-owned block.
	void *foreign[8];
	for (int i = 0; i < 8; ++i) {
		foreign[i] = malloc(37);
		CHECK( foreign[i] != NULL );
		if (foreign[i] != NULL)
			memset( foreign[i], 0xa5, 37 );
	}
	::operator delete( foreign[0] );
	::operator delete[]( foreign[1] );
	::operator delete( foreign[2], size_t(37) );
	::operator delete[]( foreign[3], size_t(37) );
	::operator delete( foreign[4], std::nothrow );
	::operator delete[]( foreign[5], std::nothrow );
	::operator delete( foreign[6], __FILE__, __LINE__ );
	::operator delete[]( foreign[7], __FILE__, __LINE__ );

	// The reverse crossing is valid too: framework-owned deallocators can free
	// memory returned by our zero-initialising global allocation forms.
	free( ::operator new(37) );
	free( ::operator new[](37) );
	free( ::operator new(37, std::nothrow) );
	free( ::operator new[](37, std::nothrow) );
	free( ::operator new(37, __FILE__, __LINE__) );
	free( ::operator new[](37, __FILE__, __LINE__) );
	::operator delete( NULL );
	::operator delete[]( NULL );
	::operator delete( NULL, size_t(0) );
	::operator delete[]( NULL, size_t(0) );

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
DEBUG_EXTERN_C void ReleaseCrash( const char *reason ) { fprintf( stderr, "ReleaseCrash: %s\n", reason ); abort(); }	// a corrupt block header
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
