/*
**	Copyright 2026 İlyas Akın
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

/* The engine symbols that GCC makes a self-check name, and that the self-check never calls.

	 WHY THEY ARE NAMED.  GameMemory.h:914 is `#define EMPTY_DTOR(CLASS) inline CLASS::~CLASS() { }`.
	 The destructor is declared in the class and defined inline OUTSIDE it, so under the Itanium ABI it
	 is still the class's key function, and GCC emits the vtable - and with it the deleting destructor,
	 operator delete and getClassMemoryPool - in every translation unit that sees that definition:
	 every TU that includes PreRTS.h.  Clang demotes a key function that is later defined inline, and
	 when optimizing emits none of it - but not always at -O0: Ubuntu 24.04's clang 18 at -O0 emitted
	 six such vtables and 22 undefined references from one engine TU, at -O2 none (measured).  Under
	 GCC one engine TU linked on its own needs the memory pool, the string buffers and AudioEventRTS.
	 Measured on Ubuntu 24.04's GCC 13, Release: FPUControl.cpp.o alone carries the vtables of
	 Bucket, Overridable, ScienceInfo and DynamicAudioEventRTS.  -Wl,--gc-sections does not help, because GNU ld
	 reports undefined references before it collects sections.

	 WHO LINKS THIS.  The tests that build a few real engine TUs without gameengine, on purpose (a
	 pre-main string, a crash in a child, one file system under test):
	   - fpucontrol_selfcheck and widechar_crc_gate (both twins);
	   - test_crash_reporting and test_early_command_line, with ZH_EAGER_VTABLE_STUB_STRINGS;
	   - gametext_csf, test_premain_strings/unicode and the four built from ZH_POSIXLFS_SOURCES
	     (test_posixlocalfilesystem, test_registryfile, test_wwdownload_registry, test_bigfilesystem),
	     with ZH_EAGER_VTABLE_STUB_NO_POOL: they link the real memory manager, so only the audio event
	     is stubbed.
	 Each symbol here is one that some such TU names and none of them runs.  If one ever runs, it says
	 which and aborts: a failing test, never garbage.  A new header-inline dependency shows up as a link
	 error naming the symbol, and belongs here.  Those ten were measured on Arch's GCC 16 with ld.lld
	 (L1b, 2026-09-27); clang on macOS links them without it, and linking it there costs nothing.

	 AsciiString::freeBytes and UnicodeString::releaseBuffer are here for the targets that do NOT link
	 the real AsciiString.cpp and UnicodeString.cpp.  The widechar gate does, so it compiles this file
	 without ZH_EAGER_VTABLE_STUB_STRINGS; fpucontrol_selfcheck defines it.

	 WHEN IT GOES.  gameengine links on Linux now (W1), but these tests build their few TUs alone on
	 purpose, and linking gameengine for one destructor pulls in the device libraries and their
	 globals as well (test_lan_broadcast's link needs posixdevice, w3ddevice and test_gameengine_stubs).
	 So the file stays while they stay self-contained.  It goes when GCC stops emitting these
	 vtables (defining EMPTY_DTOR's destructors inside their classes might do it; not tried), or when
	 each test links the engine anyway. */

#include "PreRTS.h"

#include "Common/AsciiString.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameMemory.h"
#include "Common/LogicThread.h"
#include "Common/UnicodeString.h"

#include <stdio.h>
#include <stdlib.h>

static void neverCalled( const char *what )
{
	printf( "FAIL: reached %s, which Tests/gcc_eager_vtable_stubs.cpp stubs because it is never called\n", what );
	fflush( stdout );
	abort();
}

#if !defined(ZH_EAGER_VTABLE_STUB_NO_POOL)
MemoryPoolFactory *TheMemoryPoolFactory = NULL;
void MemoryPool::freeBlock( void * ) { neverCalled( "MemoryPool::freeBlock" ); }
MemoryPool *MemoryPoolFactory::createMemoryPool( const char *, Int, Int, Int ) { neverCalled( "MemoryPoolFactory::createMemoryPool" ); return NULL; }
#endif
AudioEventRTS::~AudioEventRTS() { neverCalled( "AudioEventRTS::~AudioEventRTS" ); }

// JobSystem::parallel_for's LOGIC_CENSUS names these.  The flag stays false here, so census()
// does not call noteCensus; the symbol still has to exist, and reaching it is a failing test.
thread_local Bool LogicThread::t_isLogicThread = FALSE;
void LogicThread::noteCensus( LogicThread::Channel ) { neverCalled( "LogicThread::noteCensus" ); }

#if defined(ZH_EAGER_VTABLE_STUB_STRINGS)
void AsciiString::freeBytes( void ) { neverCalled( "AsciiString::freeBytes" ); }
void UnicodeString::releaseBuffer() { neverCalled( "UnicodeString::releaseBuffer" ); }
#if defined(_DEBUG)
// Named by this file's own ~AudioEventRTS above, which destroys the event's AsciiString members.
void AsciiString::validate() const { neverCalled( "AsciiString::validate" ); }
// Named under GCC by the eager ~UnicodeString in ScienceInfo's destructor (linux-check's Debug row,
// arm64 gcc); clang on macOS does not emit it.
void UnicodeString::validate() const { neverCalled( "UnicodeString::validate" ); }
#endif
#endif
