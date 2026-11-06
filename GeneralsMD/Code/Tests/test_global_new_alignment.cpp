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

namespace {
unsigned pool_constructions = 0, pool_destructions = 0, abstract_constructions = 0;
bool fail_pool_constructor = false;
struct ConstructorFailure {};

class PoolAllocationProbe : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(PoolAllocationProbe, "PoolAllocationProbe", 2, 2)
public:
	PoolAllocationProbe() : Value(0x12345678)
	{
		++pool_constructions;
		if (fail_pool_constructor) throw ConstructorFailure();
	}
	static void *Forbidden_Plain_New() { return new PoolAllocationProbe; }
	static MemoryPool *Pool() { return getClassMemoryPool(); }
	unsigned Value;
};
PoolAllocationProbe::~PoolAllocationProbe() { ++pool_destructions; }

class AbstractAllocationProbe : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_ABC(AbstractAllocationProbe)
public:
	AbstractAllocationProbe() { ++abstract_constructions; }
	static void *Forbidden_Plain_New() { return new AbstractAllocationProbe; }
	static void *Forbidden_Tagged_New() { return newInstance(AbstractAllocationProbe); }
};
AbstractAllocationProbe::~AbstractAllocationProbe() {}

class MissingGlueProbe : public MemoryPoolObject
{
public:
	static void *Forbidden_Inherited_New() { return MemoryPoolObject::operator new(sizeof(MissingGlueProbe)); }
private:
	MemoryPool *getObjectMemoryPool() { return NULL; }
};

void expect_allocation_bug(const char *path, void *(*allocate)())
{
	bool caught = false;
	try { allocate(); }
	catch (ErrorCode error) { caught = error == ERROR_BUG; }
	catch (...) {}
	if (!caught) printf("Pool allocation path '%s' did not throw ERROR_BUG\n", path);
	CHECK(caught);
}

void check_pool_allocation_contract()
{
	// The class allocation functions must throw even if the debug diagnostic is disabled or
	// dismissed. Calling the inherited raw form also avoids constructing an object at null.
	expect_allocation_bug("inherited without glue", &MissingGlueProbe::Forbidden_Inherited_New);
	expect_allocation_bug("abstract plain", &AbstractAllocationProbe::Forbidden_Plain_New);
	expect_allocation_bug("abstract tagged", &AbstractAllocationProbe::Forbidden_Tagged_New);
	expect_allocation_bug("concrete plain", &PoolAllocationProbe::Forbidden_Plain_New);
	CHECK(pool_constructions == 0 && abstract_constructions == 0);

	MemoryPool *pool = PoolAllocationProbe::Pool();
	const Int used = pool->getUsedBlockCount();
	void *raw = pool->allocateBlock("allocation contract selfcheck");
	CHECK(raw != NULL && zeroed(raw, sizeof(PoolAllocationProbe)));
	CHECK(pool->getUsedBlockCount() == used + 1);
	pool->freeBlock(raw);
	CHECK(pool->getUsedBlockCount() == used);

	PoolAllocationProbe *object = newInstance(PoolAllocationProbe);
	CHECK(object != NULL && object->Value == 0x12345678);
	CHECK(pool->getUsedBlockCount() == used + 1);
	object->deleteInstance();
	CHECK(pool->getUsedBlockCount() == used && pool_destructions == 1);

	// Matching placement delete must return the pool block when construction throws.
	fail_pool_constructor = true;
	bool caught = false;
	try { newInstance(PoolAllocationProbe); }
	catch (const ConstructorFailure &) { caught = true; }
	catch (...) {}
	fail_pool_constructor = false;
	CHECK(caught);
	CHECK(pool->getUsedBlockCount() == used);
	CHECK(pool_constructions == 2 && pool_destructions == 1);
}
} // namespace

int main( void )
{
	initMemoryManager();
	check_pool_allocation_contract();

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
#ifdef DEBUG_CRASHING
// Model dismissing the diagnostic: forbidden allocation still has to throw afterwards.
DEBUG_EXTERN_C char *TheCurrentIgnoreCrashPtr = NULL;
DEBUG_EXTERN_C void DebugCrash( const char *, ... ) {}
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
