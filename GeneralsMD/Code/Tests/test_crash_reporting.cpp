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

/* C5: what a crash leaves behind off Windows, from the real CrashHandlerPosix.cpp.  This program runs
	 itself as a child for each way of crashing - "test_crash_reporting --crash <kind>" - in a user data
	 folder of its own, and checks how the child died and what it wrote:

	 - it died of the signal, as it would have without the handler (the handler re-raises), or of
		 SIGABRT after an exception nothing caught;
	 - ReleaseCrashInfo.txt has ReleaseCrash's sections in order, the reason, the fault address, a
		 register line, the bytes at the PC, the raw frames, and in the named pass the function that
		 crashed;
	 - the debug log (a file the child hands the handler, as Debug.cpp does) got the same report.

	 Armed controls, which must NOT leave a report: a child that never installs the handler, and a stack
	 overflow on a thread with no alternate stack (the handler cannot run without one).

	 The children switch macOS's crash reporter off for themselves only, so a run leaves no reports in
	 ~/Library/Logs/DiagnosticReports; the game does not, and its crashes are reported as any app's are.

	 Not covered: the named pass's names for code outside this executable, a crash while the report is
	 being written, and a debugger taking breakIntoDebugger's SIGTRAP before the handler does (measured
	 by hand under lldb, C5).  "--crash break" is the child to run for it. */

#include "PreRTS.h"

#include "Common/CrashHandler.h"
#include "Platform/BreakIntoDebugger.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif

#include <string>
#include <vector>

extern char **environ;

// What CrashHandlerPosix.cpp names when an exception escapes a thread.  The engine's ReleaseCrash
// writes the report itself and ends the process; before TheGlobalData exists it returns, which is the
// case here, and the handler then aborts and reports through SIGABRT.
void ReleaseCrash( const char * )
{
}

// Debug.cpp's, which this binary does not link; no window draws here
const char * volatile TheReleaseCrashDrawingWindow = NULL;

//-------------------------------------------------------------------------------------------------
// The child: ways to crash.  Global and never inlined, so the named pass can name them.
//-------------------------------------------------------------------------------------------------

extern "C" __attribute__((noinline)) void crashByNullWrite( void )
{
	*(volatile int *)(uintptr_t)16 = 1;			// address 0x10, so the report's address is recognisable
}

extern "C" __attribute__((noinline)) int recurseForever( int depth )
{
	// Small frames, so the fault comes with too little stack left for a handler to run on it: only the
	// alternate stack can take the report (the overflow-noaltstack control shows that).
	volatile char ballast[ 64 ];
	ballast[ 0 ] = (char)depth;
	return recurseForever( depth + 1 ) + ballast[ 0 ];		// not a tail call
}

extern "C" __attribute__((noinline)) void throwFromAThread( void )
{
	throw 42;
}

static int child( const char *kind, const char *logPath )
{
#if defined(__APPLE__)
	// This child's crashes are the test's, not the machine's: no report to ReportCrash.
	task_set_exception_ports( mach_task_self(), EXC_MASK_CRASH | EXC_MASK_CORPSE_NOTIFY, MACH_PORT_NULL,
		EXCEPTION_DEFAULT, THREAD_STATE_NONE );
#else
	// Nor a core file: systemd-coredump honours a zero RLIMIT_CORE too.  Before this, every run left a
	// dozen cores in /var/lib/systemd/coredump (L1b, 2026-09-27).
	struct rlimit noCore = { 0, 0 };
	setrlimit( RLIMIT_CORE, &noCore );
#endif
	const std::string what = kind;
	if (what != "segv-noinstall")
	{
		installCrashHandlers();
		const int log = open( logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
		const char *before = "a line the log already had\n";
		if (write( log, before, strlen( before ) ) < 0) {}
		setCrashLogDescriptor( log );
	}

	if (what == "segv" || what == "segv-noinstall")
		crashByNullWrite();
	else if (what == "abort")
		abort();
	else if (what == "fpe")
		raise( SIGFPE );			// arm64 does not trap an integer division by zero
	else if (what == "bus")
		raise( SIGBUS );
	else if (what == "trap")
		__builtin_trap();			// SIGTRAP on arm64, SIGILL on x86-64
	else if (what == "break")
		breakIntoDebugger();		// the assertion box's Retry: raise(SIGTRAP), with no debugger here
	else if (what == "overflow")
		recurseForever( 0 );
	else if (what == "overflow-noaltstack")
	{
		// The control has to know it disabled the stack, or it proves nothing: macOS refuses SS_DISABLE
		// with a size below MINSIGSTKSZ, and the handler then runs on the stack it was meant not to have.
		stack_t none;
		memset( &none, 0, sizeof( none ) );
		none.ss_flags = SS_DISABLE;
		none.ss_size = SIGSTKSZ;
		stack_t now;
		if (sigaltstack( &none, NULL ) != 0 || sigaltstack( NULL, &now ) != 0 || (now.ss_flags & SS_DISABLE) == 0)
			return 2;			// the parent sees a clean exit, not a signal, and fails the control
		recurseForever( 0 );
	}
	else if (what == "thread-segv")
	{
		std::thread worker( crashByNullWrite );
		worker.join();
	}
	else if (what == "thread-exception")
	{
		std::thread worker( throwFromAThread );
		worker.join();
	}
	return 0;		// not reached for any kind above
}

//-------------------------------------------------------------------------------------------------
// The parent
//-------------------------------------------------------------------------------------------------

static int s_failed = 0;
static int s_checks = 0;

static void check( bool ok, const char *kind, const char *what )
{
	++s_checks;
	if (!ok)
	{
		++s_failed;
		printf( "  FAIL %s: %s\n", kind, what );
	}
}

static std::string readFile( const std::string &path )
{
	std::string contents;
	FILE *file = fopen( path.c_str(), "rb" );
	if (file == NULL)
		return contents;
	char buffer[ 4096 ];
	size_t got;
	while ((got = fread( buffer, 1, sizeof( buffer ), file )) > 0)
		contents.append( buffer, got );
	fclose( file );
	return contents;
}

/** Removes a child's user data folder once it has been read: TMPDIR is the test's own folder in the build
	* (CMakeLists.txt), and every run left one there. */
static void removeFolder( const std::string &folder )
{
	if (folder.empty())
		return;
	const std::string command = "rm -rf '" + folder + "'";
	if (system( command.c_str() ) != 0)
		printf( "  could not remove %s\n", folder.c_str() );
}

/** Runs one child; returns its wait status, and its user data folder in `folder`. */
static int runChild( const char *self, const char *kind, std::string &folder )
{
	char pattern[] = "/tmp/zh_crash_XXXXXX";
	const char *temp = getenv( "TMPDIR" );
	std::string base = (temp && *temp) ? std::string( temp ) + "/zh_crash_XXXXXX" : std::string( pattern );
	std::vector<char> name( base.begin(), base.end() );
	name.push_back( 0 );
	folder = mkdtemp( &name[0] ) ? std::string( &name[0] ) : std::string();

	std::string userData = "ZH_USER_DATA_DIR=" + folder;
	std::string log = folder + "/DebugLogFile.txt";
	std::vector<char *> env;
	for (char **e = environ; *e; ++e)
		if (strncmp( *e, "ZH_USER_DATA_DIR=", 17 ) != 0)
			env.push_back( *e );
	env.push_back( (char *)userData.c_str() );
	env.push_back( NULL );
	char *args[] = { (char *)self, (char *)"--crash", (char *)kind, (char *)log.c_str(), NULL };
	pid_t pid;
	if (posix_spawn( &pid, self, NULL, NULL, args, &env[0] ) != 0)
		return -1;
	int status = 0;
	while (waitpid( pid, &status, 0 ) < 0 && errno == EINTR) {}
	return status;
}

#if defined(__linux__)
/* True when a raw frame of this executable in report[from, to) - "<exe>+0x<offset>" - is in <function>,
	 by binutils' addr2line over the executable's own symbol table, as a Linux crash report is read.
	 GCC assembles with binutils' as, so addr2line is there wherever this builds.  Each frame is asked at
	 its offset and one byte before: a caller's frame is a return address, which after a call that never
	 returns (child's abort, trap and raise, inlined into main) is the first byte of the NEXT function -
	 symbolizers take return address - 1 for that - while the crashing PC may be its function's first
	 byte (crashByNullWrite's is), where - 1 is the function before.  And each is asked for its inline
	 chain (-i): GCC inlines child into main and splits main's unlikely paths into main.cold, where the
	 innermost name at abort's return address is std::thread's operator==; main is further out. */
static bool rawFramesResolveTo( const std::string &report, size_t from, size_t to, const char *function )
{
	char exe[ 1024 ];
	const ssize_t n = readlink( "/proc/self/exe", exe, sizeof( exe ) - 1 );
	if (n <= 0)
		return false;
	exe[ n ] = 0;
	const char *slash = strrchr( exe, '/' );
	const std::string module = std::string( slash ? slash + 1 : exe ) + "+0x";
	std::string command = std::string( "addr2line -f -i -C -e '" ) + exe + "'";
	int frames = 0;
	for (size_t at = report.find( module, from ); at != std::string::npos && at < to; at = report.find( module, at ))
	{
		at += module.size();
		const size_t end = report.find_first_not_of( "0123456789ABCDEFabcdef", at );
		const unsigned long offset = strtoul( report.substr( at, end - at ).c_str(), NULL, 16 );
		char both[ 64 ];
		snprintf( both, sizeof( both ), " 0x%lx 0x%lx", offset, offset ? offset - 1 : 0 );
		command += both;
		++frames;
	}
	if (frames == 0)
		return false;
	FILE *pipe = popen( (command + " 2>&1").c_str(), "r" );
	if (pipe == NULL)
		return false;
	bool found = false;
	char line[ 512 ];
	while (fgets( line, sizeof( line ), pipe ) != NULL)
	{
		line[ strcspn( line, "\n" ) ] = 0;
		found = found || strcmp( line, function ) == 0 || std::string( line ) == std::string( function ) + ".cold";
	}
	pclose( pipe );
	return found;
}
#endif

// The named pass names what dladdr can: exported functions.  abort, fpe, bus and trap crash inside the C
// library or in the static child(), so the name checked for them is main, their exported caller.
static void expectReport( const char *self, const char *kind, int signal, const char *reason,
	const char *address, const char *function )
{
	std::string folder;
	const int status = runChild( self, kind, folder );
	check( WIFSIGNALED( status ) && WTERMSIG( status ) == signal, kind, "died of the expected signal" );

	const std::string report = readFile( folder + "/ReleaseCrashInfo.txt" );
	check( report.compare( 0, 17, "Release Crash at " ) == 0, kind, "starts \"Release Crash at \"" );
	const size_t reasonAt = report.find( std::string( "\n; Reason " ) + reason + "\n" );
	const size_t last = report.find( "\nLast error:\n" );
	const size_t details = report.find( "\nDetails:\nRegister dump...\n" );
	const size_t bytes = report.find( "\nBytes at PC (0x" );
	const size_t dump = report.find( "\nStack Dump:\n" );
	const size_t current = report.find( "\nCurrent stack:\n" );
	check( reasonAt != std::string::npos, kind, reason );
	check( last != std::string::npos && reasonAt < last && last < details && details < bytes && bytes < dump
		&& dump < current, kind, "the sections, in ReleaseCrash's order" );
#if defined(__arm64__) || defined(__aarch64__)
	check( report.find( "PC:" ) != std::string::npos && report.find( "X00:" ) != std::string::npos, kind, "the registers" );
#else
	check( report.find( "Rip:" ) != std::string::npos, kind, "the registers" );
#endif
	if (address != NULL)
		check( report.find( std::string( "Access address:" ) + address ) != std::string::npos, kind, "the fault address" );
	// raw frames: "  <module>(0) : <module>+0x..." in the dump; named ones after "Current stack:"
	check( dump != std::string::npos && report.find( "(0) : ", dump ) < current, kind, "raw frames in Windows' line shape" );
	if (function != NULL)
#if defined(__linux__)
		/* glibc's dladdr names only .dynsym's symbols, so the named pass names libc and not this program
		   (and a find() of "main" used to pass on "__libc_start_main").  What a Linux report gives is its
		   raw frames, resolved offline: so the check is that one of them resolves to the function. */
		check( dump != std::string::npos && rawFramesResolveTo( report, dump, current, function ), kind,
			"the crashing function, resolved offline from the raw frames" );
#else
		check( current != std::string::npos && report.find( function, current ) != std::string::npos, kind,
			"the crashing function, named" );
#endif

	const std::string log = readFile( folder + "/DebugLogFile.txt" );
	check( log.compare( 0, 26, "a line the log already had" ) == 0 && log.find( "\nLast error:\n" ) != std::string::npos,
		kind, "the same report in the debug log, after what it had" );
	printf( "  %s: %s\n", kind, report.empty() ? "(no report)" : "report written" );
	removeFolder( folder );
}

static void expectNoReport( const char *self, const char *kind, int signal )
{
	std::string folder;
	const int status = runChild( self, kind, folder );
	check( WIFSIGNALED( status ) && WTERMSIG( status ) == signal, kind, "died of the expected signal" );
	check( readFile( folder + "/ReleaseCrashInfo.txt" ).empty(), kind, "left no report (the control)" );
	printf( "  %s: control, no report\n", kind );
	removeFolder( folder );
}

int main( int argc, char *argv[] )
{
	if (argc >= 4 && strcmp( argv[1], "--crash" ) == 0)
		return child( argv[2], argv[3] );

	const char *self = argv[0];
	expectReport( self, "segv", SIGSEGV, "Uncaught signal SIGSEGV on the main thread", "0000000000000010", "crashByNullWrite" );
	expectReport( self, "abort", SIGABRT, "Uncaught signal SIGABRT on the main thread", NULL, "main" );
	expectReport( self, "fpe", SIGFPE, "Uncaught signal SIGFPE on the main thread", NULL, "main" );
	expectReport( self, "bus", SIGBUS, "Uncaught signal SIGBUS on the main thread", NULL, "main" );
#if defined(__arm64__) || defined(__aarch64__)
	expectReport( self, "trap", SIGTRAP, "Uncaught signal SIGTRAP on the main thread", NULL, "main" );
#else
	expectReport( self, "trap", SIGILL, "Uncaught signal SIGILL on the main thread", NULL, "main" );
#endif
	// Retry with no debugger attached: the break is a crash, reported, as DebugBreak is on Windows.
	expectReport( self, "break", SIGTRAP, "Uncaught signal SIGTRAP on the main thread", NULL, "main" );
	expectReport( self, "overflow", SIGSEGV, "Uncaught signal SIGSEGV on the main thread", NULL, "recurseForever" );
	expectReport( self, "thread-segv", SIGSEGV, "Uncaught signal SIGSEGV on a worker thread", "0000000000000010", "crashByNullWrite" );
	expectReport( self, "thread-exception", SIGABRT, "Uncaught exception on a worker thread", NULL, NULL );
	expectNoReport( self, "segv-noinstall", SIGSEGV );
#if defined(__APPLE__)
	expectNoReport( self, "overflow-noaltstack", SIGILL );	// macOS kills with SIGILL when it cannot push a signal frame
#else
	expectNoReport( self, "overflow-noaltstack", SIGSEGV );
#endif

	printf( "%d checks, %d failed\n", s_checks, s_failed );
	return s_failed == 0 ? 0 : 1;
}
