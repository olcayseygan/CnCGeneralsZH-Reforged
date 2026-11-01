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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Debug.cpp 
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: Debug.cpp
//
// Created:   Steven Johnson, August 2001
//
// Desc:      Debug logging and other debug utilities
//
// ----------------------------------------------------------------------------

// SYSTEM INCLUDES 
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"


// USER INCLUDES 
#define DEBUG_THREADSAFE
#ifdef DEBUG_THREADSAFE
#include "Common/CriticalSection.h"
#endif
#include "Common/CrashHandler.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "Common/MessageBoxFlags.h"
#include "Common/ExecutableDirectory.h"
#include "Platform/BreakIntoDebugger.h"
#include "stringex.h"
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>		// the Debug CRT's report hook (W3)
#endif
#include "Common/SystemInfo.h"
#include "Common/UnicodeString.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#if defined(DEBUG_STACKTRACE) || defined(IG_DEBUG_STACKTRACE)
	#include "Common/StackDump.h"
#endif

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
#if defined(_WIN32)
extern HWND ApplicationHWnd;
#endif
#include <thread>		// the main thread's id off Windows (Windows uses GetCurrentThreadId); GameDataGone's yield
#include <atomic>		// GameDataGone's state, set from whichever thread's read failed
#include <new>
#if defined(_WIN32)
#include <io.h>				// _chsize_s, DebugCutLogFile
#else
#include <unistd.h>		// ftruncate, DebugCutLogFile
#endif
#include "Common/FileWriteQueue.h"

extern char *gAppPrefix; /// So WB can have a different log file name.

#ifdef _INTERNAL
// this should ALWAYS be present
#pragma optimize("", off)
#endif

// ----------------------------------------------------------------------------
// DEFINES 
// ----------------------------------------------------------------------------

#ifdef DEBUG_LOGGING

#if defined(_INTERNAL)
	#define DEBUG_FILE_NAME				"DebugLogFileI.txt"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevI.txt"
#elif defined(_DEBUG)
	#define DEBUG_FILE_NAME				"DebugLogFileD.txt"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrevD.txt"
#else
	#define DEBUG_FILE_NAME				"DebugLogFile.txt"
	#define DEBUG_FILE_NAME_PREV	"DebugLogFilePrev.txt"
#endif

#endif

// ----------------------------------------------------------------------------
// PRIVATE TYPES 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// PRIVATE DATA 
// ----------------------------------------------------------------------------
long DebugCutLogFile(FILE *file, const char *path, long headEnd, long keep)
{
	fflush(file);
	const long size = ftell(file);
	// A second handle reads the tail: the log is opened "w", and in text mode on Windows.
	FILE *reader = zh_fopen(path, "rb");
	if (reader == NULL)
		return size;		// something else holds the file shut; it grows until the next try
	char *tail = (char *)malloc(keep);
	fseek(reader, size - keep, SEEK_SET);
	const size_t got = fread(tail, 1, keep, reader);
	fclose(reader);

	// From the first whole line, with each \r\n back to \n, which the text-mode write expands again.
	const char *newline = (const char *)memchr(tail, '\n', got);
	const size_t start = (newline != NULL) ? (size_t)(newline - tail) + 1 : got;
	size_t length = 0;
	for (size_t i = start; i < got; ++i)
	{
		if (tail[i] == '\r' && i + 1 < got && tail[i + 1] == '\n')
			continue;
		tail[length++] = tail[i];
	}

	fseek(file, headEnd, SEEK_SET);
	fprintf(file, "\n==== The log passed %ld MB and %ld bytes of it were cut here: the start of the run is above, its latest lines below ====\n\n",
		size / (1024 * 1024), size - keep + (long)start - headEnd);
	fwrite(tail, 1, length, file);
	free(tail);
	fflush(file);
	const long end = ftell(file);
#if defined(_WIN32)
	_chsize_s(_fileno(file), end);
#else
	ftruncate(fileno(file), end);
#endif
	return end;
}

#ifdef DEBUG_LOGGING
static FILE *theLogFile = NULL;

/* The log has a cap.  It used to grow for as long as the game ran, and players sent reports from
	 logs of 0.5 to 19 GB, which fill a disk and which the launcher could not read.  Past
	 LOG_CAP_BYTES the writer cuts it back to its first LOG_HEAD_BYTES (the launch: versions,
	 hardware, settings, the first load) and its latest LOG_KEEP_BYTES, in place, so the name, the
	 open file and the tail the launcher and a crash report read all stay as they were. */
static const long LOG_CAP_BYTES = 64 * 1024 * 1024;
static const long LOG_HEAD_BYTES = 8 * 1024 * 1024;
static const long LOG_KEEP_BYTES = 24 * 1024 * 1024;
static char theLogPath[ _MAX_PATH ];
static long theLogHeadEnd = 0;		// where the head ends: the end of the batch that took the file past LOG_HEAD_BYTES

/* The log file is written by a thread of its own (FileWriteQueue), never by the thread that logs.
	 Flushing once a second from the logging thread still put a disk write inside whichever logic frame
	 came due, and one 2v2 skirmish logged a 7.8 s logic frame with all of it in the AI, the phase that
	 logs the most.  The log opens during static initialisation, from inside the first operator new,
	 before this file's statics are constructed and before the memory manager exists.  So the queue is
	 built in place in static storage when the file opens. */
alignas(FileWriteQueue) static unsigned char theLogQueueStorage[ sizeof(FileWriteQueue) ];
static FileWriteQueue *theLogQueue = NULL;

static void queueLogOutput(const char *text)
{
	theLogQueue->write(text, strlen(text));
}

/// The cap, after each batch the writer puts in the file.
static void capLogFile(FILE *file)
{
	const long size = ftell(file);
	if (theLogHeadEnd == 0 && size >= LOG_HEAD_BYTES)
		theLogHeadEnd = size;
	if (size > LOG_CAP_BYTES)
		DebugCutLogFile(file, theLogPath, theLogHeadEnd, LOG_KEEP_BYTES);
}

static void startLogWriter(void)
{
	if (theLogQueue == NULL)
		theLogQueue = new (theLogQueueStorage) FileWriteQueue;
	theLogQueue->start(theLogFile, capLogFile);
}

static void stopLogWriter(void)
{
	if (theLogQueue != NULL)
		theLogQueue->stop();
}
#endif
#define LARGE_BUFFER	8192
static char theBuffer[ LARGE_BUFFER ];	// make it big to avoid weird overflow bugs in debug mode
static int theDebugFlags = 0;
#if defined(_WIN32)
static DWORD theMainThreadID = 0;
#else
static std::thread::id theMainThreadID;
#endif
// ----------------------------------------------------------------------------
// PUBLIC DATA 
// ----------------------------------------------------------------------------

char* TheCurrentIgnoreCrashPtr = NULL;

// ----------------------------------------------------------------------------
// PRIVATE PROTOTYPES 
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString(void);
static const char *getCurrentTickString(void);
static const char *prepBuffer(const char* format, char *buffer, size_t bufferSize);
#ifdef DEBUG_LOGGING
static void doLogOutput(const char *buffer);
#endif
static int doCrashBox(const char *buffer, Bool logResult);
static void whackFunnyCharacters(char *buf);
#ifdef DEBUG_STACKTRACE
static void doStackDump();
#endif

// ----------------------------------------------------------------------------
// PRIVATE FUNCTIONS 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
/* W3: a run nobody watches never waits on an assertion box - a -headless game, or any process started with
	 ZH_UNATTENDED set (windows-ci.ps1 sets it for ctest).  It logs the assertion and goes on, as EA's full-screen
	 game did, and says so on stderr too. */
static Bool unattendedRun()
{
	static const Bool fromStart = getenv("ZH_UNATTENDED") != NULL || findEarlyCommandLineOption(L"-headless") != NULL;
	return fromStart || (TheGlobalData && TheGlobalData->m_headless);
}
#endif

inline Bool ignoringAsserts()
{
#if defined(_DEBUG) || defined(_INTERNAL)
	return !DX8Wrapper_IsWindowed || (TheGlobalData&&TheGlobalData->m_debugIgnoreAsserts) || unattendedRun();
#else
	return !DX8Wrapper_IsWindowed;
#endif
}

// ----------------------------------------------------------------------------
#if defined(_WIN32)
inline HWND getThreadHWND()
{
	return (theMainThreadID == GetCurrentThreadId())?ApplicationHWnd:NULL;
}
#endif

/* Before a message box in full screen the game's window is hidden, so that the box can be seen.  The
	 window is Windows' ApplicationHWnd; off Windows there is none until C2 creates one.
	 ShowWindow on another thread's window waits until that thread pumps.  A crash on the logic thread
	 can hold the memory pool locks here, and the main thread stops pumping at its next allocation, so
	 the wait never ended and no crash file got written.  Off the main thread, ask and carry on. */
static void hideFullScreenWindowForMessage( void )
{
#if defined(_WIN32)
	if (!DX8Wrapper_IsWindowed) {
		if (ApplicationHWnd) {
			if (getThreadHWND() != NULL)
				ShowWindow(ApplicationHWnd, SW_HIDE);
			else
				ShowWindowAsync(ApplicationHWnd, SW_HIDE);
		}
	}
#endif
}

// ----------------------------------------------------------------------------

/**
 * True for a run with nobody in front of it.
 *
 * Read straight off the command line and cached, rather than taken from TheGlobalData, because the
 * two places this matters - an assert and a release crash - happen at times when the engine may not
 * exist yet or may already be half torn down.
 *
 * A batch of forty headless matches that hits one crash used to stop dead on a message box waiting
 * for a click that was never coming, and the system-modal one put that box over everything else on
 * the machine while it waited.
 */
static Bool isUnattendedRun( void )
{
	static Int cached = -1;
	if (cached < 0)
		cached = isUnattendedProcess() ? 1 : 0;	// -headless or ZH_UNATTENDED (EarlyCommandLine.h)
	return cached != 0;
}

int MessageBoxWrapper( const char *lpText, const char *lpCaption, unsigned int uType )
{
#if defined(_WIN32)
	HWND threadHWND = getThreadHWND();
	if (!threadHWND || isUnattendedRun())
		return (uType & MSGBOX_ABORTRETRYIGNORE)?MSGBOX_ID_IGNORE:MSGBOX_ID_YES;

	return ::MessageBox(threadHWND, lpText, lpCaption, uType);
#else
	/* The platform layer's box when it has a window and is on its thread (TheMessageBoxHook, C2's SDL
		 one); never in an unattended run, as above. */
	if (TheMessageBoxHook != NULL && !isUnattendedRun())
	{
		const int answer = TheMessageBoxHook(lpText, lpCaption, uType);
		if (answer >= 0)
			return answer;
	}
	/* Otherwise Windows' own no-window path above: the same answer, and the text where someone can
		 read it - stderr, and the log if it is open (written directly: DebugLog can be the caller). */
	fprintf(stderr, "%s%s%s\n", lpCaption, (lpCaption && lpCaption[0]) ? ": " : "", lpText);
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		char line[ 4096 ];		// no operator new here: this can run before the memory manager exists
		snprintf(line, sizeof(line), "[message box] %s%s%s\n", lpCaption, (lpCaption && lpCaption[0]) ? ": " : "", lpText);
		queueLogOutput(line);
		FileWriteQueue::flushAll();		// a box is often the last thing before an _exit, and its reason must reach the file
	}
#endif
	return (uType & MSGBOX_ABORTRETRYIGNORE)?MSGBOX_ID_IGNORE:MSGBOX_ID_YES;
#endif
}

// ----------------------------------------------------------------------------
// getCurrentTimeString 
/** 
	Return the current time in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTimeString(void)
{
	time_t aclock;
	time(&aclock);
	struct tm *newtime = localtime(&aclock);
	return asctime(newtime);
}

// ----------------------------------------------------------------------------
// getCurrentTickString 
/** 
	Return the current TickCount in string form
*/
// ----------------------------------------------------------------------------
static const char *getCurrentTickString(void)
{
	static char TheTickString[32];
	snprintf(TheTickString, ARRAY_SIZE(TheTickString), "(T=%08x)",Clock_Milliseconds_Coarse());
	return TheTickString;
}

// ----------------------------------------------------------------------------
// prepBuffer
// zap the buffer and optionally prepend the tick time.
// ----------------------------------------------------------------------------
/**
	Empty the buffer passed in, then optionally prepend the current TickCount
	value in string form, depending on the setting of theDebugFlags.
*/
static const char *prepBuffer(const char* format, char *buffer, size_t bufferSize)
{
	buffer[0] = 0;
#ifdef ALLOW_DEBUG_UTILS
	if (theDebugFlags & DEBUG_FLAG_PREPEND_TIME)
	{
		strlcpy(buffer, getCurrentTickString(), bufferSize);
		strlcat(buffer, " ", bufferSize);
	}
#endif
	return format;
}

// ----------------------------------------------------------------------------
// doLogOutput
/** 
	send a string directly to the log file and/or console without further processing.
*/
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
/* The log used to be flushed after every line, which made each DEBUG_LOG inside a logic frame a
	 synchronous write to disk. Single units in big fights logged update times of 60 to 145ms with
	 nothing in the pathfinder, always beside a log line, and did not repeat on a rerun of the same
	 seed. The lines go to the writer thread now (theLogQueue), which writes them once a second, and
	 every crash path writes the queue out itself before the process exits. */
static void doLogOutput(const char *buffer)
{
	/* DebugLog already holds this lock around its static buffer; DebugCrash and the stack dump reach
		 the file without it, and with the logic on a thread of its own (LOGIC-THREAD.md hazard 20) two
		 threads writing at once tear a line.  The lock is recursive, so DebugLog taking it twice is free. */
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	// log message to file
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_FILE)
	{
		if (theLogFile)
			queueLogOutput(buffer);	// note, no \n (should be there already)
	}

	// log message to dev studio output window
	if (theDebugFlags & DEBUG_FLAG_LOG_TO_CONSOLE)
	{
#if defined(_WIN32)
		::OutputDebugString(buffer);
#else
		// The debugger's output channel on Windows; stderr is the one a debugger shows elsewhere.
		fputs(buffer, stderr);
#endif
	}
}
#endif

// ----------------------------------------------------------------------------
// doCrashBox
/*
	present a messagebox with the given message. Depending on user selection,
	we exit the app, break into debugger, or continue execution. 
*/
// ----------------------------------------------------------------------------
static int doCrashBox(const char *buffer, Bool logResult)
{
	int result;

	if (!ignoringAsserts()) {
		result = MessageBoxWrapper(buffer, "Assertion Failure", MSGBOX_ABORTRETRYIGNORE|MSGBOX_TASKMODAL|MSGBOX_ICONWARNING|MSGBOX_DEFBUTTON3);
		//result = MessageBoxWrapper(buffer, "Assertion Failure", MB_ABORTRETRYIGNORE|MB_TASKMODAL|MB_ICONWARNING);
	}	else {
		result = MSGBOX_ID_IGNORE;
	}

	switch(result)
	{
		case MSGBOX_ID_ABORT:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Abort]\n");
#endif
			FileWriteQueue::flushAll();		// the log and the replay: _exit skips the C library's flush
			_exit(1);
			break;
		case MSGBOX_ID_RETRY:
#ifdef DEBUG_LOGGING
			if (logResult)
				DebugLog("[Retry]\n");
#endif
			breakIntoDebugger();
			break;
		case MSGBOX_ID_IGNORE:
#ifdef DEBUG_LOGGING
			// do nothing, just keep going
			if (logResult)
				DebugLog("[Ignore]\n");
#endif
			break;
	}
	return result;
}

#ifdef DEBUG_STACKTRACE
// ----------------------------------------------------------------------------
/**
	Dumps a stack trace (from the current PC) to logfile and/or console.
*/
static void doStackDump()
{
	const int STACKTRACE_SIZE	= 24;
	const int STACKTRACE_SKIP = 2;
	void* stacktrace[STACKTRACE_SIZE];

	doLogOutput("\nStack Dump:\n");
	::FillStackAddresses(stacktrace, STACKTRACE_SIZE, STACKTRACE_SKIP);
	::StackDumpFromAddresses(stacktrace, STACKTRACE_SIZE, doLogOutput);
}
#endif

// ----------------------------------------------------------------------------
// whackFunnyCharacters 
/** 
	Eliminates any undesirable nonprinting characters, aside from newline,
	replacing them with spaces.
*/
// ----------------------------------------------------------------------------
static void whackFunnyCharacters(char *buf)
{
	for (char *p = buf + strlen(buf) - 1; p >= buf; --p)
	{
		// ok, these are naughty magic numbers, but I'm guessing you know ASCII.... 
		if (*p >= 0 && *p < 32 && *p != 10 && *p != 13)
			*p = 32;
	}
}

// ----------------------------------------------------------------------------
// PUBLIC FUNCTIONS 
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// DebugInit 
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Initialize the debug utilities. This should be called once, as near to the 
	start of the app as possible, before anything else (since other code will
	probably want to make use of it).
*/
#if defined(_WIN32) && defined(_DEBUG)
/* W3: the Debug CRT's own checks - a checked iterator's "vector subscript out of range", a heap check - put
	 up their box too, which in a run nobody watches waits for ever.  There they are logged with the stack and
	 the run goes on; anywhere else the CRT asks as it always did. */
static int __cdecl unattendedCrtReport( int, char *message, int *returnValue )
{
	if (!unattendedRun())
		return FALSE;
	fprintf(stderr, "DEBUG CRT REPORT (auto-ignored): %s\n", message ? message : "");
#ifdef DEBUG_LOGGING
	DebugLog("DEBUG CRT REPORT (auto-ignored): %s\n", message ? message : "");
#ifdef DEBUG_STACKTRACE
	doStackDump();
#endif
	FileWriteQueue::flushAll();
#endif
	if (returnValue != NULL)
		*returnValue = 0;		// no debugger
	return TRUE;					// handled: no box
}
#endif

void DebugInit(int flags)
{
//	if (theDebugFlags != 0)
//		::MessageBox(NULL, "Debug already inited", "", MB_OK|MB_APPLMODAL);

	// just quietly allow multiple calls to this, so that static ctors can call it.
	if (theDebugFlags == 0) 
	{
		theDebugFlags = flags;

#if defined(_WIN32)
		theMainThreadID = GetCurrentThreadId();
#else
		theMainThreadID = std::this_thread::get_id();
#endif

	#ifdef DEBUG_LOGGING

		char dirbuf[ _MAX_PATH ];
#if defined(_WIN32)
		getExecutableDirectory( dirbuf, sizeof( dirbuf ), TRUE );
#else
		getLogDirectory( dirbuf, sizeof( dirbuf ), TRUE );	// outside an app bundle (P1)
#endif

		char prevbuf[ _MAX_PATH ];
		char curbuf[ _MAX_PATH ];

		/* -logPrefix <tag> names this run's log.  Two copies of the game on one machine - which is
			 what testing a network game here comes down to, MULTIPLAYER.md section 6 - otherwise open
			 the same file with "w" and only one of them keeps it.  It is read here rather than in
			 WinMain because a static constructor gets here first: preMainInitMemoryManager() calls
			 DEBUG_INIT before main, and by the time WinMain could set gAppPrefix the file is open. */
		char logPrefix[ 32 ];
		if (!findEarlyCommandLineValue( L"-logPrefix", logPrefix, sizeof( logPrefix ) ))
			logPrefix[0] = 0;

		strlcpy(prevbuf, dirbuf, ARRAY_SIZE(prevbuf));
		strlcat(prevbuf, gAppPrefix, ARRAY_SIZE(prevbuf));
		strlcat(prevbuf, logPrefix, ARRAY_SIZE(prevbuf));
		strlcat(prevbuf, DEBUG_FILE_NAME_PREV, ARRAY_SIZE(prevbuf));
		strlcpy(curbuf, dirbuf, ARRAY_SIZE(curbuf));
		strlcat(curbuf, gAppPrefix, ARRAY_SIZE(curbuf));
		strlcat(curbuf, logPrefix, ARRAY_SIZE(curbuf));
		strlcat(curbuf, DEBUG_FILE_NAME, ARRAY_SIZE(curbuf));

 		zh_remove(prevbuf);
		// A failed rotate is not fatal - the log still opens - but it means the ".prev" file holds
		// some older run than the one before this, and a bug report read on that assumption is read
		// wrong. It fails when the previous log is still open somewhere, which is what a second copy
		// of the game without -logPrefix does.
		// A first run under a new -logPrefix has nothing to rotate, and rename fails for that too -
		// so ask whether the file was there before deciding anything went wrong.
		FILE *existing = zh_fopen(curbuf, "r");
		const bool hadPreviousLog = (existing != NULL);
		if (existing != NULL)
			fclose(existing);

		const int rotated = zh_rename(curbuf, prevbuf);
		theLogFile = zh_fopen(curbuf, "w");
		strlcpy(theLogPath, curbuf, ARRAY_SIZE(theLogPath));
#if !defined(_WIN32)
		// The crash handler writes its report into the log too, and may only use the descriptor.
		setCrashLogDescriptor(theLogFile != NULL ? fileno(theLogFile) : -1);
#else
		// The exception dump's first record goes straight into the file, past the queue (StackDump.cpp).
		// _get_osfhandle fails with -1 (INVALID_HANDLE_VALUE), not NULL; StackDump takes NULL for none.
		const intptr_t logHandle = theLogFile != NULL ? _get_osfhandle(fileno(theLogFile)) : -1;
		SetCrashRecordHandle(logHandle != -1 ? (void *)logHandle : NULL);
#endif
		if (theLogFile != NULL)
		{
			startLogWriter();
			atexit(stopLogWriter);		// exit() from anywhere still gets the last second of lines
			DebugLog("Log %s opened: %s\n", curbuf, getCurrentTimeString());
			if (hadPreviousLog && rotated != 0)
				DebugLog("Could not rotate the previous log to %s: it holds an older run than this one.\n", prevbuf);
		}
	#endif
#if defined(_WIN32) && defined(_DEBUG)
		_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, unattendedCrtReport);
#endif
	}

}  
#endif

// ----------------------------------------------------------------------------
// DebugLog 
// ----------------------------------------------------------------------------
#ifdef DEBUG_LOGGING
/**
	Print a character string to the logfile and/or console.
*/
void DebugLog(const char *format, ...)
{
#ifdef DEBUG_THREADSAFE
	ScopedCriticalSection scopedCriticalSection(TheDebugLogCriticalSection);
#endif

	if (theDebugFlags == 0)
		MessageBoxWrapper("DebugLog - Debug not inited properly", "", MSGBOX_OK|MSGBOX_TASKMODAL);

	format = prepBuffer(format, theBuffer, ARRAY_SIZE(theBuffer));

	va_list arg;
  va_start(arg, format);
  vsprintf(theBuffer + strlen(theBuffer), format, arg);
  va_end(arg);

	if (strlen(theBuffer) >= sizeof(theBuffer))
		MessageBoxWrapper("String too long for debug buffer", "", MSGBOX_OK|MSGBOX_TASKMODAL);

	whackFunnyCharacters(theBuffer);
	doLogOutput(theBuffer);
} 
#endif

// ----------------------------------------------------------------------------
// DebugCrash
// ----------------------------------------------------------------------------
#ifdef DEBUG_CRASHING
/**
	Print a character string to the logfile and/or console, then halt execution
	while presenting the user with an exit/debug/ignore dialog containing the same
	text message.
*/
void DebugCrash(const char *format, ...)
{
	// Note: You might want to make this thread safe, but we cannot. The reason is that 
	// there is an implicit requirement on other threads that the message loop be running.

	// make it not static so that it'll be thread-safe.
	// make it big to avoid weird overflow bugs in debug mode
	char theCrashBuffer[ LARGE_BUFFER ];	
	if (theDebugFlags == 0)
	{
		hideFullScreenWindowForMessage();
		MessageBoxWrapper("DebugCrash - Debug not inited properly", "", MSGBOX_OK|MSGBOX_TASKMODAL);
	}

	format = prepBuffer(format, theCrashBuffer, ARRAY_SIZE(theCrashBuffer));
	strlcat(theCrashBuffer, "ASSERTION FAILURE: ", ARRAY_SIZE(theCrashBuffer));

	// The overflow test below used to sit after a vsprintf with no bound, so by the time it could
	// measure the length the stack was already written past.  vsnprintf stops at the wall instead.
	const size_t used = strlen(theCrashBuffer);
	va_list arg;
  va_start(arg, format);
  const int wanted = vsnprintf(theCrashBuffer + used, ARRAY_SIZE(theCrashBuffer) - used, format, arg);
  va_end(arg);
	theCrashBuffer[ ARRAY_SIZE(theCrashBuffer) - 1 ] = 0;

	if (wanted < 0 || (size_t)wanted >= ARRAY_SIZE(theCrashBuffer) - used)
	{
		hideFullScreenWindowForMessage();
		MessageBoxWrapper("String too long for debug buffers", "", MSGBOX_OK|MSGBOX_TASKMODAL);
	}

#ifdef DEBUG_LOGGING
	if (ignoringAsserts()) 
	{
		doLogOutput("**** CRASH IN FULL SCREEN - Auto-ignored, CHECK THIS LOG!\n");
	}
	whackFunnyCharacters(theCrashBuffer);
	doLogOutput(theCrashBuffer);
#endif
#if defined(_DEBUG) || defined(_INTERNAL)
	if (unattendedRun())
		fprintf(stderr, "DEBUG CRASH (auto-ignored): %s\n", theCrashBuffer);
#endif
#ifdef DEBUG_STACKTRACE
	if (!(TheGlobalData && TheGlobalData->m_debugIgnoreStackTrace))
	{
		doStackDump();
	}
#endif

	strlcat(theCrashBuffer, "\n\nAbort->exception; Retry->debugger; Ignore->continue\n", ARRAY_SIZE(theCrashBuffer));

	int result = doCrashBox(theCrashBuffer, true);

	if (result == MSGBOX_ID_IGNORE && TheCurrentIgnoreCrashPtr != NULL) 
	{
		int yn;
		if (!ignoringAsserts()) 
		{
			yn = MessageBoxWrapper("Ignore this crash from now on?", "", MSGBOX_YESNO|MSGBOX_TASKMODAL);
		}	
		else 
		{
			yn = MSGBOX_ID_YES;
		}
		if (yn == MSGBOX_ID_YES)
			*TheCurrentIgnoreCrashPtr = 1;
		if( TheKeyboard )
			TheKeyboard->resetKeys();
		if( TheMouse )
			TheMouse->reset();
	}

}  
#endif

// ----------------------------------------------------------------------------
// DebugShutdown 
// ----------------------------------------------------------------------------
#ifdef ALLOW_DEBUG_UTILS
/**
	Shut down the debug utilities. This should be called once, as near to the 
	end of the app as possible, after everything else (since other code will
	probably want to make use of it).
*/
void DebugShutdown()
{
#ifdef DEBUG_LOGGING
	if (theLogFile)
	{
		DebugLog("Log closed: %s\n", getCurrentTimeString());
		stopLogWriter();
#if !defined(_WIN32)
		setCrashLogDescriptor(-1);
#else
		SetCrashRecordHandle(NULL);
#endif
		fclose(theLogFile);
	}
	theLogFile = NULL;
#endif
	theDebugFlags = 0;
}  

// ----------------------------------------------------------------------------
// DebugGetFlags 
// ----------------------------------------------------------------------------
/**
	Get the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
int DebugGetFlags()
{
	return theDebugFlags;
}

// ----------------------------------------------------------------------------
// DebugSetFlags 
// ----------------------------------------------------------------------------
/**
	Set the current values for the flags passed to DebugInit. Most code will never
	need to use this; the most common usage would be to temporarily enable or disable
	the DEBUG_FLAG_PREPEND_TIME bit for complex logfile messages.
*/
void DebugSetFlags(int flags)
{
	theDebugFlags = flags;
}

#endif	// ALLOW_DEBUG_UTILS

#ifdef DEBUG_PROFILE
// ----------------------------------------------------------------------------
SimpleProfiler::SimpleProfiler()
{
	m_freq = Clock_Ticks_Per_Second();
	m_startThisSession = 0;
	m_totalThisSession = 0;
	m_totalAllSessions = 0;
	m_numSessions = 0;
}

// ----------------------------------------------------------------------------
void SimpleProfiler::start()
{
	DEBUG_ASSERTCRASH(m_startThisSession == 0, ("already started"));
	m_startThisSession = Clock_Ticks();
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stop()
{
	if (m_startThisSession != 0) 
	{
		__int64 stop;
		stop = Clock_Ticks();
		m_totalThisSession = stop - m_startThisSession;
		m_totalAllSessions += stop - m_startThisSession;
		m_startThisSession = 0;
		++m_numSessions;
	}
}

// ----------------------------------------------------------------------------
void SimpleProfiler::stopAndLog(const char *msg, int howOftenToLog, int howOftenToResetAvg)
{
	stop();
	// howOftenToResetAvg==0 means "never reset"
	if (howOftenToResetAvg > 0 && m_numSessions >= howOftenToResetAvg)
	{
		m_numSessions = 0;
		m_totalAllSessions = 0;
		DEBUG_LOG(("%s: reset averages\n",msg));
	}
	DEBUG_ASSERTLOG(m_numSessions % howOftenToLog != 0, ("%s: %f msec, total %f msec, avg %f msec\n",msg,getTime(),getTotalTime(),getAverageTime()));
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTime()
{
	stop();
	return (double)m_totalThisSession / (double)m_freq * 1000.0;
}

// ----------------------------------------------------------------------------
int SimpleProfiler::getNumSessions()
{
	stop();
	return m_numSessions;
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getTotalTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq);
}

// ----------------------------------------------------------------------------
double SimpleProfiler::getAverageTime()
{
	stop();
	if (!m_numSessions)
		return 0.0;

	return (double)m_totalAllSessions * 1000.0 / ((double)m_freq * (double)m_numSessions);
}

#endif	// ALLOW_DEBUG_UTILS

// ----------------------------------------------------------------------------
// ReleaseCrash
// ----------------------------------------------------------------------------
/**
	Halt the application, EVEN IN FINAL RELEASE BUILDS. This should be called
	only when a crash is guaranteed by continuing, and no meaningful continuation
	of processing is possible, even by throwing an exception.
*/

	#define RELEASECRASH_FILE_NAME				"ReleaseCrashInfo.txt"
	#define RELEASECRASH_FILE_NAME_PREV		"ReleaseCrashInfoPrev.txt"

	static FILE *theReleaseCrashLogFile = NULL;

	/* Everything the crash file gets goes into DebugLogFile.txt as well.  The crash file lives under
		 Documents and is opened through an ANSI path in a MAX_PATH buffer, so a redirected or non-ASCII
		 Documents folder loses it, and two players on v2.0.0 sent reports with the "Technical
		 Difficulties" box on screen and no crash file at all.  The log sits beside the exe and the
		 launcher always sends its tail. */
	static void releaseCrashLogOutput(const char *buffer)
	{
		if (theReleaseCrashLogFile)
		{
			fprintf(theReleaseCrashLogFile, "%s", buffer);	// note, no \n (should be there already)
			fflush(theReleaseCrashLogFile);
		}
#ifdef DEBUG_LOGGING
		if (theLogFile)
		{
			fprintf(theLogFile, "%s", buffer);
			fflush(theLogFile);
		}
#endif
	}

	static const int RELEASECRASH_STACK_SIZE = 12;
	// 1, not 6: FillStackAddresses drops its own frame itself now, so 6 skipped
	// past everything the game did and logged two ntdll frames instead.
	static const int RELEASECRASH_STACK_SKIP = 1;

	static void releaseCrashStackOutput(void)
	{
		void* stacktrace[RELEASECRASH_STACK_SIZE];
		::FillStackAddresses(stacktrace, RELEASECRASH_STACK_SIZE, RELEASECRASH_STACK_SKIP);
		::StackDumpFromAddresses(stacktrace, RELEASECRASH_STACK_SIZE, releaseCrashLogOutput);
	}

	/// The reason and the registers go into the log before the crash file is even opened.
	static void releaseCrashToLog(const char *reason)
	{
#ifdef DEBUG_LOGGING
		if (theLogFile)
		{
			fprintf(theLogFile, "\nRelease Crash at %s; Reason %s\n\nLast error:\n%s\n\nCurrent stack:\n",
				getCurrentTimeString(), reason, g_LastErrorDump.str());
			fflush(theLogFile);
		}
#endif
	}

void ReleaseCrash(const char *reason)
{
	// the debug log and the replay are written by threads of their own once a second, and this
	// function ends in _exit
	FileWriteQueue::flushAll();
#ifdef DEBUG_LOGGING
	// From here to _exit the file is this thread's alone, so the writer cannot cut into the report
	// with other threads' lines.  Released only on the shutdown return below; what is queued meanwhile
	// goes out before each _exit.
	if (theLogQueue != NULL)
		theLogQueue->fileLock.lock();
#endif
	releaseCrashToLog(reason);

	/// do additional reporting on the crash, if possible

	hideFullScreenWindowForMessage();
//#if defined(_DEBUG) || defined(_INTERNAL)
//	/* static */ char buff[8192]; // not so static so we can be threadsafe
//	snprintf(buff, 8192, "Sorry, a serious error occurred. (%s)", reason);/
//	buff[8191] = 0;
//	::MessageBox(NULL, buff, "Technical Difficulties...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
//#else
//	::MessageBox(NULL, "Sorry, a serious error occurred.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);
//#endif

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	if (TheGlobalData==NULL) {
#ifdef DEBUG_LOGGING
		// the process goes on, and stopping the log writer waits for this lock
		if (theLogQueue != NULL)
			theLogQueue->fileLock.unlock();
#endif
		return; // We are shutting down, and TheGlobalData has been freed.  jba. [4/15/2003]
	}

	strlcpy(prevbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(prevbuf));
	strlcat(prevbuf, RELEASECRASH_FILE_NAME_PREV, ARRAY_SIZE(prevbuf));
	strlcpy(curbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(curbuf));
	strlcat(curbuf, RELEASECRASH_FILE_NAME, ARRAY_SIZE(curbuf));

 	zh_remove(prevbuf);
	FILE *existingCrashLog = zh_fopen(curbuf, "r");
	const bool hadPreviousCrashLog = (existingCrashLog != NULL);
	if (existingCrashLog != NULL)
		fclose(existingCrashLog);
	const int rotated = zh_rename(curbuf, prevbuf);

	theReleaseCrashLogFile = zh_fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %s\n", getCurrentTimeString(), reason);
		// Two crashes in a row and this is the report anybody reads. If the older one could not be
		// moved aside it is gone, and the file next to this one is not the crash before it.
		if (hadPreviousCrashLog && rotated != 0)
			fprintf(theReleaseCrashLogFile, "(the previous crash report could not be rotated aside; %s is older than it looks)\n", prevbuf);
		fprintf(theReleaseCrashLogFile, "\nLast error:\n%s\n\nCurrent stack:\n", g_LastErrorDump.str());
		// The stack walk below can fault, and the reason and the registers above it must survive that.
		fflush(theReleaseCrashLogFile);
	}
	else
	{
		DEBUG_LOG(("ReleaseCrash - could not write %s\n", curbuf));
	}
	releaseCrashStackOutput();
	if (theReleaseCrashLogFile)
	{
		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = NULL;
	}
	FileWriteQueue::flushAll();		// the "could not write" line above, if there was one

	hideFullScreenWindowForMessage();
	// the log is written by now; a run nobody is watching should die rather than wait for a click
	if (isUnattendedRun())
	{
		_exit(1);
	}

#if defined(_WIN32)
#if defined(_DEBUG) || defined(_INTERNAL)
	/* static */ char buff[8192]; // not so static so we can be threadsafe
	snprintf(buff, 8192, "Sorry, a serious error occurred. (%s)", reason);
	buff[8191] = 0;
	::MessageBox(NULL, buff, "Technical Difficulties...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
#else
// crash error messaged changed 3/6/03 BGC
//	::MessageBox(NULL, "Sorry, a serious error occurred.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);
//	::MessageBox(NULL, "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game. Please visit the forums at www.generals.ea.com for suggested courses of action or consult your manual for Technical Support contact information.", "Technical Difficulties...", MB_OK|MB_TASKMODAL|MB_ICONERROR);

// crash error message changed again 8/22/03 M Lorenzen... made this message box modal to the system so it will appear on top of any task-modal windows, splash-screen, etc.
// The 2003 text sent the player to forums at generals.ea.com that no longer exist, and never said
// that a report had just been written or where. Name the file instead.
	char buff[ 2 * _MAX_PATH ];
	_snprintf(buff, ARRAY_SIZE(buff), "You have encountered a serious error and the game has to close.\n\nA crash report was written to:\n%s\n\nPlease include that file when you report the problem.", curbuf);
	buff[ARRAY_SIZE(buff) - 1] = 0;
	::MessageBox(NULL, buff, "Technical Difficulties...", MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);


#endif
#else
	// The same box, through MessageBoxWrapper: SDL's when there is a window and this is its thread (C2),
	// otherwise a line on stderr.  (A crash signal never gets here; C5's handler writes the file itself.)
	{
		char text[ 1024 ];
#if defined(_DEBUG) || defined(_INTERNAL)
		snprintf(text, sizeof(text), "Sorry, a serious error occurred. (%s)", reason);
#else
		snprintf(text, sizeof(text), "You have encountered a serious error.  Serious errors can be caused by many things including viruses, overheated hardware and hardware that does not meet the minimum specifications for the game. Please visit the forums at www.generals.ea.com for suggested courses of action or consult your manual for Technical Support contact information. (%s)", reason);
#endif
		MessageBoxWrapper(text, "Technical Difficulties...", MSGBOX_OK | MSGBOX_ICONERROR | MSGBOX_TASKMODAL);
	}
#endif

	_exit(1);
}  

// ----------------------------------------------------------------------------
// GameDataGone
// ----------------------------------------------------------------------------

/* 0: the data is there.  1: a thread is writing theGameDataGoneWhat.  2: it is gone, and
	 theGameDataGoneWhat says what failed. */
static std::atomic<int> theGameDataGone( 0 );
static char theGameDataGoneWhat[ 256 ];

static Bool isMainThread( void )
{
#if defined(_WIN32)
	return theMainThreadID == GetCurrentThreadId();
#else
	return theMainThreadID == std::this_thread::get_id();
#endif
}

/* On the main thread only.  Written for a run that is past saving: the log is flushed by hand and the
	 process ends with _exit, so no destructor, no quit path and nothing that writes Options.ini or a save
	 runs on data it can no longer read. */
static void gameDataGoneExit( void )
{
	while (theGameDataGone.load() == 1)
		std::this_thread::yield();		// another thread is still naming what failed

	char text[ 1024 ];
	snprintf( text, sizeof( text ), "The game's data is no longer available (%s could not be read). "
		"If Zero Hour is installed on an external or network drive, it may have been disconnected, ejected or "
		"put to sleep. Reconnect it and start the game again.", theGameDataGoneWhat );

	DEBUG_LOG(( "GAME DATA GONE: %s\n", text ));
	FileWriteQueue::flushAll();		// written once a second otherwise, and _exit below skips the CRT's flush
	fprintf( stderr, "GAME DATA GONE: %s\n", text );
	fflush( stderr );

	if (!isUnattendedRun())
	{
		hideFullScreenWindowForMessage();
		MessageBoxWrapper( text, "Command & Conquer Generals Zero Hour", MSGBOX_OK | MSGBOX_ICONERROR | MSGBOX_TASKMODAL );
	}
	_exit( GAME_DATA_GONE_EXIT_STATUS );
}

void GameDataGone( const char *what )
{
	int none = 0;
	if (theGameDataGone.compare_exchange_strong( none, 1 ))
	{
		strlcpy( theGameDataGoneWhat, (what != NULL && what[0] != '\0') ? what : "an archive", sizeof( theGameDataGoneWhat ) );
		theGameDataGone.store( 2 );
	}
	if (isMainThread())
		gameDataGoneExit();
}

void GameDataGoneCheck( void )
{
	if (theGameDataGone.load() != 0)
		gameDataGoneExit();
}

void ReleaseCrashLocalized(const AsciiString& p, const AsciiString& m)
{
	// see ReleaseCrash: the log and the replay are written once a second and _exit below skips the
	// CRT's flush
	FileWriteQueue::flushAll();
#ifdef DEBUG_LOGGING
	if (theLogQueue != NULL)
		theLogQueue->fileLock.lock();		// the report uninterrupted, as in ReleaseCrash
#endif
	if (!TheGameText) {
		ReleaseCrash(m.str());
		// Back here only when TheGlobalData is gone; the lock taken above goes as ReleaseCrash's did.
#ifdef DEBUG_LOGGING
		if (theLogQueue != NULL)
			theLogQueue->fileLock.unlock();
#endif
		return;
	}

	releaseCrashToLog(m.str());
	UnicodeString prompt = TheGameText->fetch(p);
	UnicodeString mesg = TheGameText->fetch(m);


	/// do additional reporting on the crash, if possible

	hideFullScreenWindowForMessage();

	if (isUnattendedRun())
	{
		// nobody to read it; the log below is the report - see ReleaseCrash above
	}
#if defined(_WIN32)
	else if (TheSystemIsUnicode)
	{
		// Win32's W API: WideChar and WCHAR are the same two bytes on Windows, which makes the cast honest.
		::MessageBoxW(NULL, reinterpret_cast<LPCWSTR>(mesg.str()), reinterpret_cast<LPCWSTR>(prompt.str()), MB_OK|MB_SYSTEMMODAL|MB_ICONERROR);
	}
	else
	{
		// However, if we're using the default version of the message box, we need to 
		// translate the string into an AsciiString
		AsciiString promptA, mesgA;
		promptA.translate(prompt);
		mesgA.translate(mesg);
		//Make sure main window is not TOP_MOST
		::SetWindowPos(ApplicationHWnd, HWND_NOTOPMOST, 0, 0, 0, 0,SWP_NOSIZE |SWP_NOMOVE);
		::MessageBoxA(NULL, mesgA.str(), promptA.str(), MB_OK|MB_TASKMODAL|MB_ICONERROR);
	}
#else
	else
	{
		// No window to own a box until C2: the localised text, as UTF-8, where someone can read it.
		fprintf(stderr, "%s: %s\n", WideCharAsUtf8(prompt.str()).str(), WideCharAsUtf8(mesg.str()).str());
	}
#endif

	char prevbuf[ _MAX_PATH ];
	char curbuf[ _MAX_PATH ];

	strlcpy(prevbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(prevbuf));
	strlcat(prevbuf, RELEASECRASH_FILE_NAME_PREV, ARRAY_SIZE(prevbuf));
	strlcpy(curbuf, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(curbuf));
	strlcat(curbuf, RELEASECRASH_FILE_NAME, ARRAY_SIZE(curbuf));

 	zh_remove(prevbuf);
	FILE *existingCrashLog = zh_fopen(curbuf, "r");
	const bool hadPreviousCrashLog = (existingCrashLog != NULL);
	if (existingCrashLog != NULL)
		fclose(existingCrashLog);
	const int rotated = zh_rename(curbuf, prevbuf);

	theReleaseCrashLogFile = zh_fopen(curbuf, "w");
	if (theReleaseCrashLogFile)
	{
		fprintf(theReleaseCrashLogFile, "Release Crash at %s; Reason %s\n", getCurrentTimeString(), WideCharAsUtf8( mesg.str() ).str());
		if (hadPreviousCrashLog && rotated != 0)
			fprintf(theReleaseCrashLogFile, "(the previous crash report could not be rotated aside; %s is older than it looks)\n", prevbuf);
	}
	else
	{
		DEBUG_LOG(("ReleaseCrashLocalized - could not write %s\n", curbuf));
	}
	releaseCrashStackOutput();
	if (theReleaseCrashLogFile)
	{
		fflush(theReleaseCrashLogFile);
		fclose(theReleaseCrashLogFile);
		theReleaseCrashLogFile = NULL;
	}
	FileWriteQueue::flushAll();

	_exit(1);
}
