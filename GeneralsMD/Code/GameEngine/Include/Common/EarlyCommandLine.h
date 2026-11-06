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

#pragma once

// EarlyCommandLine.h
//
// A couple of options have to be read before CommandLine.cpp's parser exists: the debug log's file
// name is chosen by a static constructor (preMainInitMemoryManager -> DEBUG_INIT), and the
// one-copy-at-a-time mutex is taken in WinMain long before TheGlobalData is around to hold a flag.
//
// Both read the *wide* command line on purpose.  WinMain tokenizes lpCmdLine in place and that
// buffer is the process's ANSI command line, so GetCommandLineA is truncated to the exe name and
// the first switch by the time anything asks.  GetCommandLineW is untouched.

#if defined(_WIN32)
#include <windows.h>
#else
#include "Platform/MSVCCompat.h"	// _wcsnicmp, which its other includers happened to bring in first
#endif
#include <stdlib.h>	// getenv
#include <wchar.h>
#include <wctype.h>

/** The text just past an option in a command line, or NULL if it does not carry it.  An option is
    only an option on a word boundary, so "-win" does not match inside "-window". */
inline const wchar_t *findCommandLineOptionIn( const wchar_t *cmdLine, const wchar_t *option )
{
	if (cmdLine == NULL)
		return NULL;

	const size_t len = wcslen( option );
	for (const wchar_t *at = cmdLine; *at != 0; ++at)
	{
		if ((at == cmdLine || iswspace( at[-1] )) &&
				_wcsnicmp( at, option, len ) == 0 &&
				(at[len] == 0 || iswspace( at[len] )))
			return at + len;
	}
	return NULL;
}

/** The word after an option, if there is one and it is not itself an option. */
inline bool findCommandLineValueIn( const wchar_t *cmdLine, const wchar_t *option, char *out, size_t outSize )
{
	const wchar_t *at = findCommandLineOptionIn( cmdLine, option );
	if (at == NULL || outSize == 0)
		return false;

	while (iswspace( *at ))
		++at;
	if (*at == 0 || *at == L'-')
		return false;

	size_t i = 0;
	while (*at != 0 && !iswspace( *at ) && i + 1 < outSize)
		out[i++] = (char)*at++;
	out[i] = 0;
	return i > 0;
}

/* The two above read the process's own command line.  Windows keeps it for the process's whole life
	 and GetCommandLineW hands it out at any time, even before main.  So do the POSIX systems, as the
	 argv the kernel gave the process: Darwin through _NSGetArgv, Linux through /proc/self/cmdline.
	 That matters because the first reader runs before main: the debug log takes -logPrefix from
	 inside the memory manager's pre-main start (GameMemory.h), so an argv handed over by main would
	 come too late.

	 processCommandLineW joins argv with single spaces into one wide string, as Windows' line looks,
	 one byte to one wchar_t, so findCommandLineValueIn's narrowing gives the same bytes back (a UTF-8
	 path survives).  It is built once, into static storage: it must not allocate, because the pre-main
	 reader runs while the memory manager is being started.  32,767 is Windows' own limit on a command
	 line; a longer one is cut there. */
#if defined(_WIN32)
inline const wchar_t *findEarlyCommandLineOption( const wchar_t *option )
{
	return findCommandLineOptionIn( GetCommandLineW(), option );
}

inline bool findEarlyCommandLineValue( const wchar_t *option, char *out, size_t outSize )
{
	return findCommandLineValueIn( GetCommandLineW(), option, out, outSize );
}
#else

#include <string.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#else
#error "EarlyCommandLine.h: no way to read the process's command line on this platform"
#endif

/** Appends one argument to `line` at `length`, a space first unless it is the first; false once full. */
inline bool appendCommandLineArgument( wchar_t *line, size_t capacity, size_t &length, const char *argument, size_t argumentLength )
{
	if (length > 0)
	{
		if (length + 1 >= capacity)
			return false;
		line[length++] = L' ';
	}
	for (size_t i = 0; i < argumentLength; ++i)
	{
		if (length + 1 >= capacity)
			return false;
		line[length++] = (wchar_t)(unsigned char)argument[i];
	}
	return true;
}

/** This process's command line, as one wide string: argv joined with single spaces. */
inline const wchar_t *processCommandLineW( void )
{
	static wchar_t s_line[ 32768 ];
	struct Builder
	{
		static const wchar_t *build( wchar_t *line, size_t capacity )
		{
			size_t length = 0;
#if defined(__APPLE__)
			char **argv = *_NSGetArgv();
			const int argc = *_NSGetArgc();
			for (int i = 0; i < argc && argv[i] != NULL; ++i)
			{
				if (!appendCommandLineArgument( line, capacity, length, argv[i], strlen( argv[i] ) ))
					break;
			}
#else
			// NUL-separated arguments.  Read in pieces into a fixed buffer: no allocation (see above).
			static char s_raw[ 32768 ];
			size_t rawLength = 0;
			const int fd = ::open( "/proc/self/cmdline", O_RDONLY );
			if (fd >= 0)
			{
				ssize_t got;
				while (rawLength < sizeof( s_raw ) && (got = ::read( fd, s_raw + rawLength, sizeof( s_raw ) - rawLength )) > 0)
					rawLength += (size_t)got;
				::close( fd );
			}
			for (size_t start = 0; start < rawLength; )
			{
				size_t end = start;
				while (end < rawLength && s_raw[end] != 0)
					++end;
				if (!appendCommandLineArgument( line, capacity, length, s_raw + start, end - start ))
					break;
				start = end + 1;
			}
#endif
			line[length] = 0;
			return line;
		}
	};
	static const wchar_t *s_built = Builder::build( s_line, sizeof( s_line ) / sizeof( s_line[0] ) );
	return s_built;
}

inline const wchar_t *findEarlyCommandLineOption( const wchar_t *option )
{
	return findCommandLineOptionIn( processCommandLineW(), option );
}

inline bool findEarlyCommandLineValue( const wchar_t *option, char *out, size_t outSize )
{
	return findCommandLineValueIn( processCommandLineW(), option, out, outSize );
}
#endif

/** ASCII option names also work at portable call sites; Windows still reads its native wide command line. */
template<size_t N>
inline bool findEarlyCommandLineValue( const char (&option)[N], char *out, size_t outSize )
{
	wchar_t wide[N];
	for (size_t i = 0; i < N; ++i)
		wide[i] = (unsigned char)option[i];
	return findEarlyCommandLineValue( wide, out, outSize );
}

/** ZH_UNATTENDED in the environment, set to anything but "" or "0": the CI scripts and the test harnesses set
	 it for every run they start, drawing or not.  Players never do. */
inline bool unattendedByEnvironment( void )
{
	const char *value = getenv( "ZH_UNATTENDED" );
	return value != NULL && value[0] != 0 && !(value[0] == '0' && value[1] == 0);
}

/** A run nobody is watching: -headless, or ZH_UNATTENDED (above).  Such a run never waits on a message box,
	 a crash report or a folder chooser: it says why on stderr or in the log and exits, or takes the box's
	 default.  Every box the game can raise asks this (Debug.cpp's isUnattendedRun, the missing base game,
	 WWLib's INI and WWDebug's assert, the debug library, PosixMain). */
inline bool isUnattendedProcess( void )
{
	return findEarlyCommandLineOption( L"-headless" ) != NULL || unattendedByEnvironment();
}
