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

// FILE: Debug.h 
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  Debug.h
//
// Created:    Steven Johnson, August 2001
//
// Desc:       Debug Utilities
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DEBUG_H_
#define __DEBUG_H_

class AsciiString;

#if defined(_DEBUG) && defined(_INTERNAL)
	#error "Only one at a time of these should ever be defined"
#endif

#define NO_RELEASE_DEBUG_LOGGING

#ifdef RELEASE_DEBUG_LOGGING  ///< Creates a DebugLogFile.txt (No I or D) with all the debug log goodness.  Good for startup problems.
	#define ALLOW_DEBUG_UTILS 1
	#define DEBUG_LOGGING 1
	#define DISABLE_DEBUG_CRASHING 1
	#define DISABLE_DEBUG_STACKTRACE 1
	#define DISABLE_DEBUG_PROFILE 1
#endif

// These are stolen from the WW3D Debug file. REALLY useful. :-)
#define STRING_IT(a) #a																				  
#define TOKEN_IT(a) STRING_IT(,##a)
#define MESSAGE(a) message (__FILE__ "(" TOKEN_IT(__LINE__) ") : " a)

// by default, turn on ALLOW_DEBUG_UTILS if _DEBUG is turned on.
#if (defined(_DEBUG) || defined(_INTERNAL)) && !defined(ALLOW_DEBUG_UTILS) && !defined(DISABLE_ALLOW_DEBUG_UTILS)
	#define ALLOW_DEBUG_UTILS 1
#endif

// these are predicated on ALLOW_DEBUG_UTILS, not _DEBUG, and allow you to selectively disable
// bits of the debug stuff for special builds.
#if defined(ALLOW_DEBUG_UTILS) && !defined(DEBUG_LOGGING) && !defined(DISABLE_DEBUG_LOGGING)
	#define DEBUG_LOGGING 1
#endif
#if defined(ALLOW_DEBUG_UTILS) && !defined(DEBUG_CRASHING) && !defined(DISABLE_DEBUG_CRASHING)
	#define DEBUG_CRASHING 1
#endif
#if defined(ALLOW_DEBUG_UTILS) && !defined(DEBUG_STACKTRACE) && !defined(DISABLE_DEBUG_STACKTRACE)
	#define DEBUG_STACKTRACE 1
#endif
#if defined(ALLOW_DEBUG_UTILS) && !defined(DEBUG_PROFILE) && !defined(DISABLE_DEBUG_PROFILE)
	#define DEBUG_PROFILE 1
#endif

#ifdef __cplusplus
	#define DEBUG_EXTERN_C extern "C"
#else
	#define DEBUG_EXTERN_C extern
#endif


// SYSTEM INCLUDES ////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////

// FORWARD REFERENCES /////////////////////////////////////////////////////////

// TYPE DEFINES ///////////////////////////////////////////////////////////////

// INLINING ///////////////////////////////////////////////////////////////////

// EXTERNALS //////////////////////////////////////////////////////////////////

/// @todo: the standard line-to-string trick isn't working correctly in vc6; figure out why
#define DEBUG_STRING_IT(b)	#b
#define DEBUG_TOKEN_IT(a)		DEBUG_STRING_IT(a)
#define DEBUG_FILENLINE			__FILE__ ":" DEBUG_TOKEN_IT(__LINE__)

#ifdef ALLOW_DEBUG_UTILS

	enum 
	{
		DEBUG_FLAG_LOG_TO_FILE = 0x01,	
		DEBUG_FLAG_LOG_TO_CONSOLE = 0x02,
		DEBUG_FLAG_PREPEND_TIME = 0x04,
#if defined(_INTERNAL) || defined(RELEASE_DEBUG_LOGGING)
		//
		// by default, _INTERNAL builds log to file, but not to console, in the interest
		// of speed. want console output? just change this line:
		//
		// A release build that logs is here for the same reason and pays the same price twice over:
		// the console side is OutputDebugString, which takes a system-wide lock and crosses into the
		// kernel on every line, whether or not anything is listening.  A frame that logs a few
		// hundred lines - one per object, which the battle plan code does - then spends more time in
		// that call than in the work the frame was for.  The file is what anybody reads afterwards.
		//
		DEBUG_FLAGS_DEFAULT = (DEBUG_FLAG_LOG_TO_FILE)
#else
		DEBUG_FLAGS_DEFAULT = (DEBUG_FLAG_LOG_TO_FILE | DEBUG_FLAG_LOG_TO_CONSOLE)
#endif
	};

	DEBUG_EXTERN_C void DebugInit(int flags);
	DEBUG_EXTERN_C void DebugShutdown();

	DEBUG_EXTERN_C int DebugGetFlags();
	DEBUG_EXTERN_C void DebugSetFlags(int flags);

	#define DEBUG_INIT(f)						do { DebugInit(f); } while (0)
	#define DEBUG_SHUTDOWN()				do { DebugShutdown(); } while (0)

#else

	#define DEBUG_INIT(f)						((void)0)
	#define DEBUG_SHUTDOWN()				((void)0)

#endif

#ifdef DEBUG_LOGGING

	DEBUG_EXTERN_C void DebugLog(const char *format, ...);

	#define DEBUG_LOG(m)						do { { DebugLog m ; } } while (0)
	#define DEBUG_ASSERTLOG(c, m)		do { { if (!(c)) DebugLog m ; } } while (0)

#else

	#define DEBUG_LOG(m)						((void)0)
	#define DEBUG_ASSERTLOG(c, m)		((void)0)

#endif

//
// A line written once per object, or once per drawable per frame, is a development trace: useful
// with a debugger attached, and in a release build nothing but cost - every line is a flush to
// disk, and a frame that writes a few hundred of them is a frame the player feels.  DEBUG_LOG_DEV
// says "this one is a trace": it stays in a debug or internal build and disappears from the release
// build that logs.  Anything a player's log has to be able to answer stays on plain DEBUG_LOG.
//
#if defined(DEBUG_LOGGING) && (defined(_DEBUG) || defined(_INTERNAL))
	#define DEBUG_LOG_DEV(m)				do { { DebugLog m ; } } while (0)
#else
	#define DEBUG_LOG_DEV(m)				((void)0)
#endif

/// The debug log's cap (Debug.cpp): cut the open, flushed log at path down to its first headEnd
/// bytes, a line saying what went, and the whole lines in its last keep bytes; it stays open at
/// its new end, which is returned.  Here rather than static so test_gameengine can call it.
#include <stdio.h>
DEBUG_EXTERN_C long DebugCutLogFile(FILE *file, const char *path, long headEnd, long keep);

#ifdef DEBUG_CRASHING

	DEBUG_EXTERN_C void DebugCrash(const char *format, ...);

	/*
		Yeah, it's a sleazy global, since we can't reasonably add
		any args to DebugCrash due to the varargs nature of it. 
		We'll just let it slide in this case...
	*/
	DEBUG_EXTERN_C char* TheCurrentIgnoreCrashPtr;

	#define DEBUG_CRASH(m)	\
		do { \
			{ \
				static char ignoreCrash = 0; \
				if (!ignoreCrash) { \
					TheCurrentIgnoreCrashPtr = &ignoreCrash; \
					DebugCrash m ; \
					TheCurrentIgnoreCrashPtr = NULL; \
				} \
			} \
		} while (0)

	#define DEBUG_ASSERTCRASH(c, m)		do { { if (!(c)) DEBUG_CRASH(m); } } while (0)

	//Note: RELEASE_CRASH(m) is now always defined.
	//#define RELEASE_CRASH(m)					DEBUG_CRASH((m))

#else

	#define DEBUG_CRASH(m)					((void)0)
	#define DEBUG_ASSERTCRASH(c, m)	((void)0)

//	DEBUG_EXTERN_C void ReleaseCrash(const char* reason);

//	#define RELEASE_CRASH(m)				do { ReleaseCrash(m); } while (0)

#endif

DEBUG_EXTERN_C void ReleaseCrash(const char* reason);
DEBUG_EXTERN_C void ReleaseCrashLocalized(const AsciiString& p, const AsciiString& m);

/** The decorated name of the window whose draw function is running, NULL between draws; set by
	* GameWindowManager::drawWindow and printed by ReleaseCrash.  A fault inside a gadget's draw names
	* only the gadget's code, never which of a screen's lists it was (v4.3.x reports 4591 4627 4630). */
extern const char * volatile TheReleaseCrashDrawingWindow;

/** The game's data has gone away under it: the drive the install is on was disconnected, ejected or put
	* to sleep while the game had its archives open.  The archive layer calls this when a read of an open
	* archive fails that way (File::deviceGone).  Not a crash, and nothing is saved: on the main thread it logs,
	* says so in a box (none under -headless) and ends the process at once with GAME_DATA_GONE_EXIT_STATUS.
	* On any other thread (the audio's streaming reads) it only records the loss, and GameDataGoneCheck, which
	* the main loop calls every frame, ends it there.  The first loss is the one reported. */
DEBUG_EXTERN_C void GameDataGone(const char* what);
DEBUG_EXTERN_C void GameDataGoneCheck(void);
enum { GAME_DATA_GONE_EXIT_STATUS = 3 };		///< the process's exit status when the game's data went away

#define RELEASE_CRASH(m)				do { ReleaseCrash(m); } while (0)
#define RELEASE_CRASHLOCALIZED(p, m)		do { ReleaseCrashLocalized(p, m); } while (0)


#ifdef DEBUG_PROFILE

// int64_t, not Int64: wwdebug's files include this header without Lib/BaseType.h, and only they compile
// DEBUG_PROFILE (the engine's targets define RELEASE_DEBUG_LOGGING, which turns it off).  EA wrote __int64;
// on MSVC int64_t, __int64 and Int64 are one type.
#include <stdint.h>

class SimpleProfiler
{
private:
	int64_t m_freq;
	int64_t m_startThisSession;
	int64_t m_totalThisSession;
	int64_t m_totalAllSessions;
	int			m_numSessions;

public:

	SimpleProfiler();
	void start();
	void stop();
	void stopAndLog(const char *msg, int howOftenToLog, int howOftenToResetAvg);
	double getTime();				// of most recent session, in milliseconds
	int getNumSessions();
	double getTotalTime();	// total over all sessions, in milliseconds
	double getAverageTime();	// averaged over all sessions, in milliseconds

};

#define BEGIN_PROFILE(uniqueid) \
	static SimpleProfiler prof_##uniqueid; \
	prof_##uniqueid.start();

#define END_PROFILE(uniqueid, msg, howoftentolog, howoftentoreset) \
	prof_##uniqueid.stopAndLog(msg, howoftentolog, howoftentoreset);

#else
	
#define BEGIN_PROFILE(uniqueid)
#define END_PROFILE(uniqueid, msg, howoftentolog, howoftentoreset)

#endif

// MACROS //////////////////////////////////////////////////////////////////

#endif // __DEBUG_H_
