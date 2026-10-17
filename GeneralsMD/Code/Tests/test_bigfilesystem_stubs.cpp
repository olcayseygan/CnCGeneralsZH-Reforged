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
/*
 * Link-time stand-ins for test_bigfilesystem (and, before it, test_posixlocalfilesystem, whose list
 * this extends), which is built from the real GameMemory,
 * MemoryInit, LocalFile, File, RAMFile and PosixDevice sources rather than from gameengine, since
 * gameengine does not link on macOS yet.  Each name here is one those sources refer to and the test
 * never reaches; when gameengine links, the test links it instead and this file goes.
 *
 *  - DebugInit, DebugLog: Debug.cpp does not compile off Windows yet.  DebugLog prints, so a
 *    DEBUG_LOG from the memory manager is not lost.
 *  - INI: SubsystemInterfaceList::initSubsystem loads INI files; nothing here initialises a
 *    subsystem through it.
 *  - TheFileSystem, FileSystem::openFile: RAMFile::open(const char*) opens through the whole file
 *    system; the test opens files through PosixLocalFileSystem directly.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "Common/Debug.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"

extern "C" void DebugInit(int)
{
}

extern "C" void DebugLog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	fputs("  [DebugLog] ", stderr);
	vfprintf(stderr, format, args);
	va_end(args);
}

static void unreachable(const char *what)
{
	fprintf(stderr, "test_posixlocalfilesystem: %s was reached, and this test has no stand-in for it\n", what);
	abort();
}

INI::INI() { unreachable("INI::INI"); }
INI::~INI() {}
void INI::loadDirectory(AsciiString, Bool, INILoadType, Xfer *) { unreachable("INI::loadDirectory"); }
void INI::load(AsciiString, INILoadType, Xfer *) { unreachable("INI::load"); }

FileSystem *TheFileSystem = NULL;
File *FileSystem::openFile(const Char *, Int) { unreachable("FileSystem::openFile"); return NULL; }
// ArchiveFileSystem asks these only of a mod's archives; nothing loads mods here.
Bool FileSystem::isMapPath(const AsciiString &) { unreachable("FileSystem::isMapPath"); return FALSE; }
Bool FileSystem::isForkUIPath(const AsciiString &) { unreachable("FileSystem::isForkUIPath"); return FALSE; }

// GameDataGone (Common/Debug.h): an archive read failed because its drive went away.  Every archive this
// test opens is on the local install and stays there, so that never happens here.
extern "C" void GameDataGone(const char *what) { fprintf(stderr, "GameDataGone(%s)\n", what); unreachable("GameDataGone"); }

// The archive code's other reaches, for this test:
//  - TheAudio: Win32BIGFileSystem::closeArchiveFile stops the music when Music.big closes; nothing
//    closes an archive here.
//  - TheWritableGlobalData: ArchiveFileSystem::loadMods reads -mod's settings; nothing loads mods here.
//  - MessageBoxWrapper: the missing-base-game warning.  Counted, so the test can say the base game
//    was found without one.
AudioManager *TheAudio = NULL;
GlobalData *TheWritableGlobalData = NULL;
int g_messageBoxes = 0;
int MessageBoxWrapper(const char *text, const char *caption, unsigned int)
{
	fprintf(stderr, "  [MessageBoxWrapper] %s: %s\n", caption, text);
	++g_messageBoxes;
	return 1;
}
