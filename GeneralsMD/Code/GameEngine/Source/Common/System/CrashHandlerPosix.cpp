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

// FILE: CrashHandlerPosix.cpp ////////////////////////////////////////////////////////////////////
// Desc:   ReleaseCrashInfo.txt from a crash signal, off Windows (C5).  See CrashHandler.h.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* THE RULE: between the signal and the end of the raw report, only async-signal-safe calls - open,
	 write, rename, unlink, fsync, close, time, sigaction, raise, nanosleep, pthread_self - and fixed
	 buffers formatted by hand.  No malloc, no stdio, no AsciiString, no DEBUG_LOG, no memory manager, no
	 message box: any of them can hold a lock the crashed thread already held, and a crash reporter that
	 deadlocks one time in fifty is worse than none.  Everything that needs any of them is done in
	 installCrashHandlers, before a crash.

	 THE FILE is ReleaseCrash's (Debug.cpp): the same name in the same folder, the previous one rotated to
	 ReleaseCrashInfoPrev.txt, and the same sections in the same order -

		 Release Crash at <asctime>			(asctime ends in a newline, so "; Reason" starts line two)
		 ; Reason Uncaught signal SIGSEGV on the main thread

		 Last error:
		 <what DumpExceptionInfo writes: the kind, the address, "Details:", the registers, the bytes at the
		 PC, "Stack Dump:" and the raw frames>

		 Current stack:
		 <the frames named, in StackDumpFromAddresses' line shape>

	 Frame lines are Windows' "  <file>(<line>) : <function> 0x<address>".  In-process there is no line
	 table, so the module is the file and the line is 0: a raw frame reads "  generals(0) : generals+0x1F3A0
	 0x0000000100E1F3A0", which atos turns into a name and a line offline; a named one gives the symbol.

	 ORDER, as DumpExceptionInfo's: the registers and the bytes at the PC first, then the raw frames, then
	 fsync - and only after that the naming pass, with dladdr, which is not formally async-signal-safe.  If
	 it hangs or faults, the names are lost and nothing else.

	 THE END: the default action is restored and the signal raised again, so the process dies of it - the
	 exit status says which - and macOS's crash reporter and a core dump still see the crash. */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CrashHandler.h"
#include "Common/EarlyOptions.h"

#include <atomic>
#include <dlfcn.h>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/ucontext.h>
#elif defined(__linux__)
#include <execinfo.h>
#include <link.h>
#include <ucontext.h>
#else
#error "CrashHandlerPosix.cpp: read the loaded images and a signal's registers on this platform"
#endif

void ReleaseCrash( const char *reason );

namespace {

//-------------------------------------------------------------------------------------------------
// Prepared at install
//-------------------------------------------------------------------------------------------------

const int CRASH_SIGNALS[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
const size_t CRASH_SIGNAL_COUNT = sizeof( CRASH_SIGNALS ) / sizeof( CRASH_SIGNALS[0] );
const size_t ALTERNATE_STACK_SIZE = 64 * 1024;
const unsigned int MAX_FRAMES = 64;
const unsigned int MAX_IMAGES = 512;

bool s_installed = false;
char s_crashPath[ 4096 ];
char s_previousPath[ 4096 ];
long s_localOffset = 0;						///< seconds east of UTC when installed, for the asctime line
pthread_t s_mainThread;
int s_probe[ 2 ] = { -1, -1 };			///< a pipe: writing from an address says whether it can be read
std::atomic<int> s_logFd( -1 );
std::atomic_flag s_handling = ATOMIC_FLAG_INIT;
pthread_t s_handlingThread;				///< which thread is writing the report, once one is
const char *volatile s_terminateReason = NULL;

struct Image
{
	uintptr_t base;
	char name[ 64 ];
};
Image s_images[ MAX_IMAGES ];
unsigned int s_imageCount = 0;

void copyName( char *out, size_t size, const char *path )
{
	const char *leaf = (path != NULL) ? strrchr( path, '/' ) : NULL;
	const char *name = leaf ? leaf + 1 : (path ? path : "?");
	if (*name == 0)
		name = "?";
	strncpy( out, name, size - 1 );
	out[ size - 1 ] = 0;
}

#if defined(__linux__)
int collectImage( struct dl_phdr_info *info, size_t, void * )
{
	if (s_imageCount >= MAX_IMAGES)
		return 1;
	uintptr_t lowest = UINTPTR_MAX;
	for (int i = 0; i < info->dlpi_phnum; ++i)
		if (info->dlpi_phdr[i].p_type == PT_LOAD && info->dlpi_phdr[i].p_vaddr < lowest)
			lowest = info->dlpi_phdr[i].p_vaddr;
	Image &image = s_images[ s_imageCount++ ];
	image.base = info->dlpi_addr + (lowest == UINTPTR_MAX ? 0 : lowest);
	char executable[ 4096 ];
	const char *path = info->dlpi_name;
	if (path == NULL || *path == 0)
	{
		const ssize_t length = readlink( "/proc/self/exe", executable, sizeof( executable ) - 1 );
		executable[ length > 0 ? length : 0 ] = 0;
		path = executable;
	}
	copyName( image.name, sizeof( image.name ), path );
	return 0;
}
#endif

/** Every image loaded now, for naming a frame "module+offset" without asking the loader in the handler. */
void collectImages( void )
{
	s_imageCount = 0;
#if defined(__APPLE__)
	const uint32_t count = _dyld_image_count();
	for (uint32_t i = 0; i < count && s_imageCount < MAX_IMAGES; ++i)
	{
		Image &image = s_images[ s_imageCount++ ];
		image.base = (uintptr_t)_dyld_get_image_header( i );
		copyName( image.name, sizeof( image.name ), _dyld_get_image_name( i ) );
	}
#else
	dl_iterate_phdr( collectImage, NULL );
#endif
}

/** The image an address is in: the one with the greatest base at or below it. */
const Image *imageOf( uintptr_t address )
{
	const Image *best = NULL;
	for (unsigned int i = 0; i < s_imageCount; ++i)
		if (s_images[i].base <= address && (best == NULL || s_images[i].base > best->base))
			best = &s_images[i];
	return best;
}

//-------------------------------------------------------------------------------------------------
// Writing, by hand
//-------------------------------------------------------------------------------------------------

/** A line under construction in a fixed buffer. */
struct Line
{
	char text[ 512 ];
	size_t length;
	Line() : length( 0 ) { text[0] = 0; }
	void add( const char *s )
	{
		while (*s && length + 1 < sizeof( text ))
			text[ length++ ] = *s++;
		text[ length ] = 0;
	}
	void addChar( char c )
	{
		if (length + 1 < sizeof( text ))
		{
			text[ length++ ] = c;
			text[ length ] = 0;
		}
	}
	void addHex( uint64_t value, int digits )		///< upper case, as Windows' %X
	{
		static const char HEX[] = "0123456789ABCDEF";
		char out[ 16 ];
		int n = 0;
		do
		{
			out[ n++ ] = HEX[ value & 15 ];
			value >>= 4;
		} while (value != 0 && n < 16);
		while (n < digits && n < 16)
			out[ n++ ] = '0';
		while (n > 0)
			addChar( out[ --n ] );
	}
	void addDecimal( uint64_t value, int digits = 1 )
	{
		char out[ 20 ];
		int n = 0;
		do
		{
			out[ n++ ] = (char)('0' + value % 10);
			value /= 10;
		} while (value != 0 && n < 20);
		while (n < digits && n < 20)
			out[ n++ ] = '0';
		while (n > 0)
			addChar( out[ --n ] );
	}
};

int s_crashFd = -1;

void writeAll( int fd, const char *text, size_t length )
{
	while (fd >= 0 && length > 0)
	{
		const ssize_t written = write( fd, text, length );
		if (written < 0)
		{
			if (errno == EINTR)
				continue;
			return;
		}
		text += written;
		length -= (size_t)written;
	}
}

/** Into the crash file and the debug log, as ReleaseCrash's releaseCrashLogOutput writes both. */
void emit( const char *text )
{
	const size_t length = strlen( text );
	writeAll( s_crashFd, text, length );
	writeAll( s_logFd.load(), text, length );
}

void emit( const Line &line )
{
	emit( line.text );
}

/** Whether one byte at `address` can be read, and what it is: written into the probe pipe and read back. */
bool readByte( uintptr_t address, uint8_t *out )
{
	if (s_probe[1] < 0 || write( s_probe[1], (const void *)address, 1 ) != 1)
		return false;
	return read( s_probe[0], out, 1 ) == 1;
}

bool readWord( uintptr_t address, uintptr_t *out )
{
	uint8_t bytes[ sizeof( uintptr_t ) ];
	for (size_t i = 0; i < sizeof( bytes ); ++i)
		if (!readByte( address + i, &bytes[i] ))
			return false;
	memcpy( out, bytes, sizeof( bytes ) );
	return true;
}

/** asctime's "Www Mmm dd hh:mm:ss yyyy\n", from time() and the offset taken at install. */
void addAsctime( Line &line )
{
	static const char *DAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	const int64_t seconds = (int64_t)time( NULL ) + s_localOffset;
	int64_t days = seconds / 86400;
	int64_t rest = seconds % 86400;
	if (rest < 0) { rest += 86400; --days; }
	const int weekday = (int)((days + 4) % 7 + 7) % 7;		// 1970-01-01 was a Thursday
	// days since 1970-01-01 to a civil date (Howard Hinnant's algorithm)
	const int64_t z = days + 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	const int day = (int)(doy - (153 * mp + 2) / 5 + 1);
	const int month = (int)(mp < 10 ? mp + 3 : mp - 9);
	const int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
	line.add( DAYS[ weekday ] );
	line.addChar( ' ' );
	line.add( MONTHS[ month - 1 ] );
	line.addChar( ' ' );
	if (day < 10) line.addChar( ' ' );
	line.addDecimal( (uint64_t)day );
	line.addChar( ' ' );
	line.addDecimal( (uint64_t)(rest / 3600), 2 );
	line.addChar( ':' );
	line.addDecimal( (uint64_t)(rest % 3600 / 60), 2 );
	line.addChar( ':' );
	line.addDecimal( (uint64_t)(rest % 60), 2 );
	line.addChar( ' ' );
	line.addDecimal( (uint64_t)year );
	line.addChar( '\n' );
}

const char *signalName( int signal )
{
	switch (signal)
	{
		case SIGSEGV: return "SIGSEGV";
		case SIGBUS: return "SIGBUS";
		case SIGILL: return "SIGILL";
		case SIGFPE: return "SIGFPE";
		case SIGABRT: return "SIGABRT";
		case SIGTRAP: return "SIGTRAP";
		default: return "a signal";
	}
}

/** What the signal and its si_code mean, in DumpExceptionInfo's "Error code / Description" shape. */
const char *signalDescription( int signal, int code )
{
	switch (signal)
	{
		case SIGSEGV:
			if (code == SEGV_MAPERR) return "Description: The thread tried to read from or write to an address that is not mapped.";
			if (code == SEGV_ACCERR) return "Description: The thread tried to read from or write to an address it does not have the appropriate access to.";
			return "Description: A segmentation fault.";
		case SIGBUS:
			if (code == BUS_ADRALN) return "Description: The thread tried to access misaligned data.";
			if (code == BUS_ADRERR) return "Description: The thread tried to access a physical address that does not exist.";
			return "Description: A bus error.";
		case SIGILL: return "Description: The thread tried to execute an invalid instruction.";
		case SIGFPE:
			if (code == FPE_INTDIV) return "Description: The thread tried to divide an integer value by an integer divisor of zero.";
			if (code == FPE_FLTDIV) return "Description: The thread tried to divide a floating-point value by a floating-point divisor of zero.";
			return "Description: An arithmetic exception.";
		case SIGABRT: return "Description: The process aborted.";
		case SIGTRAP: return "Description: A breakpoint or trap instruction was executed, with no debugger to take it.";
		default: return "Description: Unknown.";
	}
}

void emitRegister( Line &line, const char *name, uint64_t value, bool last )
{
	line.add( name );
	line.addChar( ':' );
	line.addHex( value, 16 );
	line.add( last ? "\n" : "\t" );
}

/** The registers, and the PC, frame pointer and stack pointer, from the signal's context. */
void emitRegisters( void *context, uintptr_t *pc, uintptr_t *fp, uintptr_t *lr )
{
	const ucontext_t *uc = (const ucontext_t *)context;
	*pc = *fp = *lr = 0;
	emit( "Register dump...\n" );
#if defined(__APPLE__) && defined(__arm64__)
	const auto &s = uc->uc_mcontext->__ss;
	for (int i = 0; i < 29; i += 3)
	{
		Line line;
		for (int j = i; j < i + 3 && j < 29; ++j)
		{
			char name[ 4 ] = { 'X', (char)('0' + j / 10), (char)('0' + j % 10), 0 };
			emitRegister( line, name, s.__x[j], j == i + 2 || j == 28 );
		}
		emit( line );
	}
	Line tail;
	emitRegister( tail, "FP", s.__fp, false );
	emitRegister( tail, "LR", s.__lr, false );
	emitRegister( tail, "SP", s.__sp, false );
	emitRegister( tail, "PC", s.__pc, true );
	emit( tail );
	Line flags;
	flags.add( "CPSR:" );
	flags.addHex( s.__cpsr, 8 );
	flags.add( " \n" );
	emit( flags );
	*pc = s.__pc; *fp = s.__fp; *lr = s.__lr;
#elif defined(__APPLE__) && defined(__x86_64__)
	const auto &s = uc->uc_mcontext->__ss;
	Line a, b, c, d, e, f, g;
	emitRegister( a, "Rip", s.__rip, false ); emitRegister( a, "Rsp", s.__rsp, false ); emitRegister( a, "Rbp", s.__rbp, true );
	emitRegister( b, "Rax", s.__rax, false ); emitRegister( b, "Rbx", s.__rbx, false ); emitRegister( b, "Rcx", s.__rcx, true );
	emitRegister( c, "Rdx", s.__rdx, false ); emitRegister( c, "Rsi", s.__rsi, false ); emitRegister( c, "Rdi", s.__rdi, true );
	emitRegister( d, "R8 ", s.__r8, false ); emitRegister( d, "R9 ", s.__r9, false ); emitRegister( d, "R10", s.__r10, true );
	emitRegister( e, "R11", s.__r11, false ); emitRegister( e, "R12", s.__r12, false ); emitRegister( e, "R13", s.__r13, true );
	emitRegister( f, "R14", s.__r14, false ); emitRegister( f, "R15", s.__r15, true );
	g.add( "EFlags:" ); g.addHex( s.__rflags, 8 ); g.add( " \n" );
	emit( a ); emit( b ); emit( c ); emit( d ); emit( e ); emit( f ); emit( g );
	*pc = s.__rip; *fp = s.__rbp;
#elif defined(__linux__) && defined(__aarch64__)
	const auto &m = uc->uc_mcontext;
	for (int i = 0; i < 29; i += 3)
	{
		Line line;
		for (int j = i; j < i + 3 && j < 29; ++j)
		{
			char name[ 4 ] = { 'X', (char)('0' + j / 10), (char)('0' + j % 10), 0 };
			emitRegister( line, name, m.regs[j], j == i + 2 || j == 28 );
		}
		emit( line );
	}
	Line tail;
	emitRegister( tail, "FP", m.regs[29], false );
	emitRegister( tail, "LR", m.regs[30], false );
	emitRegister( tail, "SP", m.sp, false );
	emitRegister( tail, "PC", m.pc, true );
	emit( tail );
	*pc = m.pc; *fp = m.regs[29]; *lr = m.regs[30];
#elif defined(__linux__) && defined(__x86_64__)
	const greg_t *r = uc->uc_mcontext.gregs;
	Line a, b, c, d, e, f, g;
	emitRegister( a, "Rip", r[REG_RIP], false ); emitRegister( a, "Rsp", r[REG_RSP], false ); emitRegister( a, "Rbp", r[REG_RBP], true );
	emitRegister( b, "Rax", r[REG_RAX], false ); emitRegister( b, "Rbx", r[REG_RBX], false ); emitRegister( b, "Rcx", r[REG_RCX], true );
	emitRegister( c, "Rdx", r[REG_RDX], false ); emitRegister( c, "Rsi", r[REG_RSI], false ); emitRegister( c, "Rdi", r[REG_RDI], true );
	emitRegister( d, "R8 ", r[REG_R8], false ); emitRegister( d, "R9 ", r[REG_R9], false ); emitRegister( d, "R10", r[REG_R10], true );
	emitRegister( e, "R11", r[REG_R11], false ); emitRegister( e, "R12", r[REG_R12], false ); emitRegister( e, "R13", r[REG_R13], true );
	emitRegister( f, "R14", r[REG_R14], false ); emitRegister( f, "R15", r[REG_R15], true );
	g.add( "EFlags:" ); g.addHex( (uint64_t)r[REG_EFL], 8 ); g.add( " \n" );
	emit( a ); emit( b ); emit( c ); emit( d ); emit( e ); emit( f ); emit( g );
	*pc = r[REG_RIP]; *fp = r[REG_RBP];
#else
	emit( "(the registers are not read on this CPU)\n" );
#endif
}

/** The frames of the crashed thread: the PC, then (on Darwin, whose ABI keeps frame pointers) the link
	* register and the frame-pointer chain from the signal's context, each read checked; elsewhere
	* backtrace(), primed at install. */
unsigned int collectFrames( uintptr_t pc, uintptr_t fp, uintptr_t lr, uintptr_t *frames )
{
	unsigned int count = 0;
	if (pc != 0)
		frames[ count++ ] = pc;
#if defined(__APPLE__)
	if (lr != 0 && count < MAX_FRAMES)
		frames[ count++ ] = lr;		// arm64: a leaf that has not saved its frame is found only here
	uintptr_t frame = fp;
	while (frame != 0 && (frame & (sizeof( uintptr_t ) - 1)) == 0 && count < MAX_FRAMES)
	{
		uintptr_t next = 0, returnAddress = 0;
		if (!readWord( frame, &next ) || !readWord( frame + sizeof( uintptr_t ), &returnAddress ))
			break;
		if (returnAddress == 0)
			break;
		if (count == 0 || frames[ count - 1 ] != returnAddress)		// the LR above, pushed again
			frames[ count++ ] = returnAddress;
		if (next <= frame)
			break;					// the chain only climbs; anything else is not a frame record
		frame = next;
	}
#else
	(void)fp;
	(void)lr;
	void *walked[ MAX_FRAMES ];
	const int got = backtrace( walked, (int)MAX_FRAMES );
	for (int i = 0; i < got && count < MAX_FRAMES; ++i)
		frames[ count++ ] = (uintptr_t)walked[i];
#endif
	return count;
}

/** "  <module>(0) : <module>+0x<offset> 0x<address>" - offline-symbolisable, no loader call. */
void emitRawFrame( uintptr_t address )
{
	Line line;
	const Image *image = imageOf( address );
	line.add( "  " );
	line.add( image ? image->name : "?" );
	line.add( "(0) : " );
	line.add( image ? image->name : "?" );
	line.add( "+0x" );
	line.addHex( image ? address - image->base : address, 1 );
	line.add( " 0x" );
	line.addHex( address, 16 );
	line.addChar( '\n' );
	emit( line );
}

/** "  <module>(0) : <symbol>+0x<offset> 0x<address>", through dladdr: the best-effort pass. */
void emitNamedFrame( uintptr_t address )
{
	Dl_info info;
	memset( &info, 0, sizeof( info ) );
	if (dladdr( (const void *)address, &info ) == 0 || info.dli_sname == NULL)
	{
		emitRawFrame( address );
		return;
	}
	char module[ 64 ];
	copyName( module, sizeof( module ), info.dli_fname );
	Line line;
	line.add( "  " );
	line.add( module );
	line.add( "(0) : " );
	line.add( info.dli_sname );
	line.add( "+0x" );
	line.addHex( address - (uintptr_t)info.dli_saddr, 1 );
	line.add( " 0x" );
	line.addHex( address, 16 );
	line.addChar( '\n' );
	emit( line );
}

//-------------------------------------------------------------------------------------------------
// The handler
//-------------------------------------------------------------------------------------------------

/** Restores the default action for `signal` and raises it: the process dies of it on return. */
void dieOf( int signal )
{
	struct sigaction fallback;
	memset( &fallback, 0, sizeof( fallback ) );
	fallback.sa_handler = SIG_DFL;
	sigemptyset( &fallback.sa_mask );
	sigaction( signal, &fallback, NULL );
	raise( signal );		// delivered on return; a hardware fault also faults again on the same instruction
}

void handleCrash( int signal, siginfo_t *info, void *context )
{
	if (s_handling.test_and_set())
	{
		// A crash inside the report itself (another signal than the one being handled, which
		// SA_RESETHAND already took care of): nothing more can be written, so die of it.
		if (pthread_equal( pthread_self(), s_handlingThread ))
		{
			dieOf( signal );
			return;
		}
		// Another thread is writing the report; it ends the process when it is done.
		for (;;)
		{
			struct timespec second = { 1, 0 };
			nanosleep( &second, NULL );
		}
	}
	s_handlingThread = pthread_self();

	// The file, rotated as ReleaseCrash rotates it.
	unlink( s_previousPath );
	rename( s_crashPath, s_previousPath );
	s_crashFd = open( s_crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 );

	const bool mainThread = pthread_equal( pthread_self(), s_mainThread ) != 0;
	Line heading;
	heading.add( "Release Crash at " );
	addAsctime( heading );
	heading.add( "; Reason " );
	if (signal == SIGABRT && s_terminateReason != NULL)
		heading.add( s_terminateReason );
	else
	{
		heading.add( "Uncaught signal " );
		heading.add( signalName( signal ) );
		heading.add( mainThread ? " on the main thread" : " on a worker thread" );
	}
	// a static copy GameWindowManager::drawWindow keeps, so safe to read from a signal
	const char *drawing = TheReleaseCrashDrawingWindow;
	if( drawing != NULL )
	{
		heading.add( "\nDrawing window: " );
		heading.add( drawing[0] ? drawing : "(unnamed)" );
	}
	heading.add( "\n\nLast error:\n" );
	writeAll( s_logFd.load(), "\n", 1 );		// the log has lines before this one; the file starts with the heading
	emit( heading );

	// What DumpExceptionInfo writes, in its order.
	Line kind;
	kind.add( "Signal " );
	kind.add( signalName( signal ) );
	kind.add( ", code " );
	kind.addDecimal( info ? (uint64_t)(unsigned int)info->si_code : 0 );
	kind.addChar( '\n' );
	emit( kind );
	emit( signalDescription( signal, info ? info->si_code : 0 ) );
	emit( "\n" );
	if (info && (signal == SIGSEGV || signal == SIGBUS))
	{
		Line address;
		address.add( "Access address:" );
		address.addHex( (uint64_t)(uintptr_t)info->si_addr, 16 );
		address.add( " could not be accessed.\n" );
		emit( address );
	}
	emit( "\nDetails:\n" );
	uintptr_t pc, fp, lr;
	emitRegisters( context, &pc, &fp, &lr );

	Line bytes;
	bytes.add( "\nBytes at PC (0x" );
	bytes.addHex( pc, 16 );
	bytes.add( ")  : " );
	for (int i = 0; i < 32; ++i)
	{
		uint8_t byte;
		if (pc != 0 && readByte( pc + (uintptr_t)i, &byte ))
		{
			bytes.addHex( byte, 2 );
			bytes.addChar( ' ' );
		}
		else
			bytes.add( "?? " );
	}
	bytes.addChar( '\n' );
	emit( bytes );

	uintptr_t frames[ MAX_FRAMES ];
	const unsigned int frameCount = collectFrames( pc, fp, lr, frames );
	emit( "\nStack Dump:\n" );
	for (unsigned int i = 0; i < frameCount; ++i)
		emitRawFrame( frames[i] );
	emit( "\nCurrent stack:\n" );
	if (s_crashFd >= 0)
		fsync( s_crashFd );
	const int logFd = s_logFd.load();
	if (logFd >= 0)
		fsync( logFd );

	// The best-effort pass: names, where dladdr can give them.
	for (unsigned int i = 0; i < frameCount; ++i)
		emitNamedFrame( frames[i] );
	if (s_crashFd >= 0)
	{
		fsync( s_crashFd );
		close( s_crashFd );
	}
	if (logFd >= 0)
		fsync( logFd );

	// Die of the signal, as the process would have without this handler.
	dieOf( signal );
}

/** An exception nothing caught: ReleaseCrash's report when the engine is up, the signal path otherwise. */
void handleTerminate( void )
{
	s_terminateReason = "Uncaught exception on a worker thread";
	if (!pthread_equal( pthread_self(), s_mainThread ))
		ReleaseCrash( s_terminateReason );		// returns only if TheGlobalData is gone
	else
		s_terminateReason = "Uncaught exception on the main thread";
	abort();
}

} // namespace

//-------------------------------------------------------------------------------------------------
// Install
//-------------------------------------------------------------------------------------------------

void installThreadCrashStack( void )
{
	stack_t current;
	if (sigaltstack( NULL, &current ) == 0 && (current.ss_flags & SS_DISABLE) == 0)
		return;		// this thread has one
	stack_t alternate;
	alternate.ss_sp = malloc( ALTERNATE_STACK_SIZE );		// kept for the thread's life: a thread's end is the process's here
	alternate.ss_size = ALTERNATE_STACK_SIZE;
	alternate.ss_flags = 0;
	if (alternate.ss_sp != NULL)
		sigaltstack( &alternate, NULL );
}

void setCrashLogDescriptor( int fd )
{
	s_logFd.store( fd );
}

void installCrashHandlers( void )
{
	if (s_installed)
		return;
	s_installed = true;
	s_mainThread = pthread_self();

	// The crash file's path: the user data folder, which findUserDataDirectory spells the engine's way,
	// with a separator at the end - a real POSIX directory otherwise.
	char folder[ 4096 ];
	s_crashPath[0] = s_previousPath[0] = 0;
	if (findUserDataDirectory( folder, sizeof( folder ) ))
	{
		size_t length = strlen( folder );
		if (length > 0 && (folder[ length - 1 ] == '\\' || folder[ length - 1 ] == '/'))
			folder[ --length ] = 0;
		snprintf( s_crashPath, sizeof( s_crashPath ), "%s/ReleaseCrashInfo.txt", folder );
		snprintf( s_previousPath, sizeof( s_previousPath ), "%s/ReleaseCrashInfoPrev.txt", folder );
	}

	time_t now = time( NULL );
	struct tm local;
	if (localtime_r( &now, &local ) != NULL)
		s_localOffset = local.tm_gmtoff;

	if (pipe( s_probe ) == 0)
	{
		fcntl( s_probe[0], F_SETFL, O_NONBLOCK );
		fcntl( s_probe[1], F_SETFL, O_NONBLOCK );
		fcntl( s_probe[0], F_SETFD, FD_CLOEXEC );
		fcntl( s_probe[1], F_SETFD, FD_CLOEXEC );
	}
	collectImages();
	{
		// Anything the handler's calls load on first use is loaded now: dladdr's tables, and on Linux
		// backtrace()'s unwinder, whose first call allocates.
		Dl_info info;
		dladdr( (const void *)&installCrashHandlers, &info );
#if defined(__linux__)
		void *frames[ 4 ];
		backtrace( frames, 4 );
#endif
	}

	installThreadCrashStack();
	for (size_t i = 0; i < CRASH_SIGNAL_COUNT; ++i)
	{
		struct sigaction action;
		memset( &action, 0, sizeof( action ) );
		action.sa_sigaction = handleCrash;
		action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
		sigemptyset( &action.sa_mask );
		sigaction( CRASH_SIGNALS[i], &action, NULL );
	}
	std::set_terminate( handleTerminate );
}
