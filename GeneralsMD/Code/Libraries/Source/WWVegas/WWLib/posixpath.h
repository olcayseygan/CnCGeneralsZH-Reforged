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
** The one place a path the engine spelled becomes a path the operating system can open (C1).
**
** The engine spells paths the way Windows took them: "Data\INI\GameData.ini", in whatever case the
** code or the data happened to use, and it keeps them that way - INI load order, the INI CRC,
** MapCache.ini's keys and the portable save paths all depend on the spelling, so nothing upstream is
** normalised.  This translates only the string handed to the operating system:
**
**   - '\' and '/' both separate.  A leading '/' or '\' is absolute; anything else is relative to the
**     current directory, which is the install root.  A drive letter or a UNC path is refused.
**   - Each component is tried exactly first, and on a miss matched without regard to ASCII case
**     against the directory's listing.  On a case-insensitive volume the exact try always succeeds.
**   - Two entries differing only in case (possible on a case-sensitive volume, never on Windows): the
**     exact spelling wins; failing that, the first in byte order, logged once (decision D2).
**   - A component that does not exist keeps the engine's spelling when the intent allows creating
**     it, as Windows would have created it.
**
** Directory listings are cached, each against its directory's modification time, which POSIX changes
** whenever an entry is added or removed.  So a lookup that misses costs one stat when nothing has
** changed, which matters: the engine probes the local file system before the archives for every
** asset, and most of those probes miss.
**
** On a volume with coarse timestamps - exFAT, where the Steam installs on this project's machines
** sit, keeps them to two seconds - two changes inside one tick leave the time where it was.  That is
** harmless there and not a reason to change the cache: exFAT is case-insensitive, so the exact try
** always finds an existing file and the cache is never consulted.  The case-sensitive volumes the
** cache exists for (APFS case-sensitive, ext4, overlayfs) keep nanoseconds.
**
** POSIX only.  Windows opens the engine's spelling as it is.
*/

#ifndef POSIXPATH_H
#define POSIXPATH_H

#if defined(_WIN32)
#error "posixpath.h is the POSIX side of path resolution; Windows opens the engine's spelling as it is"
#endif

#include <string>
#include <vector>

enum PosixPathIntent
{
	// Every component must exist.
	POSIX_PATH_EXISTING,
	// Every component but the last must exist; a missing last one keeps the engine's spelling.
	POSIX_PATH_CREATE_LEAF,
	// Any component may be missing; every missing one keeps the engine's spelling.  For walks that
	// create the directories along a path.
	POSIX_PATH_CREATE_PATH,
	// Every component must exist, in the current directory only, never in an overlay: for whatever
	// changes a file that is there (unlink, remove, rename's source, an open for writing).  The two
	// CREATE intents resolve there too.  Reads alone see the overlays (P1).
	POSIX_PATH_EXISTING_IN_ROOT
};

// P1 (decision 9): the fork's own data is an overlay, read roots searched BEFORE the current
// directory (the install) for a relative path that is read, in the order given.  A relative path that
// is written, created, removed or renamed resolves in the current directory only, so nothing ever
// lands in an overlay.  A directory listing is the union of every root's (see
// PosixPath_List_Like_Win32).  Absolute paths are untouched.  Set once, before the engine starts
// (PosixMain); each entry is a real, absolute directory.
void PosixPath_Set_Overlays(const std::vector<std::string> & real_directories);
std::vector<std::string> PosixPath_Overlays();

// P1 step 2: the roots are read-only.  With this set, every zh_* call that would write a RELATIVE
// path - create, truncate, append, update, remove, unlink, rename (either end), mkdir - is refused
// with EROFS and logged once per path, so nothing the engine addresses relative to the install can
// change it.  Absolute paths (the user data directory, the logs) are the engine's
// deliberate destinations and pass.  PosixMain sets it for every run; -writableRoot turns it off, for
// a harness's armed control only.  Reads, and the overlays, are unaffected.
void PosixPath_Set_Root_Read_Only(bool read_only);
bool PosixPath_Root_Read_Only();

// Resolves engine_path for intent into real_path.  False when a component the intent needs does not
// exist, or when the path is one no POSIX system can have (empty, a drive letter, a UNC path).
bool PosixPath_Resolve(const char * engine_path, PosixPathIntent intent, std::string & real_path);

// Drops the cached listing of one directory, by its real path.  The zh_* forwarders call it after
// they create, remove or rename, so the next lookup does not wait for the directory's time to move -
// a file system whose timestamps are coarser than two changes in a row would otherwise hide one.
void PosixPath_Forget_Directory(const char * real_directory);

// Drops every cached listing.  For tests, and for anything that changes many directories at once.
void PosixPath_Forget_All();

// Windows' FindFirstFile matching, for the patterns the engine passes: "*.ini", "*.big",
// "Patch*.big", "*", "*.w3d", "*.tga", an empty pattern, and "*." for directories.
// - '*' matches any run and '?' one character; anything else matches itself, without regard to
//   ASCII case.
// - "*.*" means "*", and "*." means a name with no '.', as Windows reads them.
// - An empty pattern matches nothing: FindFirstFile on a path ending in a separator finds nothing.
// Windows also tries a pattern against a file's 8.3 short name, so "*.ini" can find "x.inix" through
// "X~1.INI" there.  That is not emulated; nothing the game ships depends on it, and short names are
// commonly switched off on NTFS volumes anyway.
bool PosixPath_Matches_Pattern(const char * pattern, const char * name);

// Win32LocalFileSystem::getFileListInDirectory, reproduced for a POSIX file system, so that the list
// the engine builds - and the INI load order and CRC that follow from it - are what Windows builds for
// the same files (C1's design, "why this needed a design").
// - The files: those matching the last component of original_directory + current_directory +
//   search_name, in the directory before it (resolved, so case need not match the disk).  Each is
//   appended to found as original_directory + current_directory + its on-disk name, exactly as the
//   Win32 code joins it.
// - With search_subdirectories: every subdirectory matching "*." (no dot in the name) is listed the
//   same way, with current_directory + its name + '\'.
// Entries are visited in byte order.  The caller's set compares without case and keeps the first of
// two names that differ only in case - which a case-sensitive volume can hold and Windows cannot - so
// byte order makes that first one the same every time, as decision D2 does for lookups.
// - With overlays (P1), a relative directory is listed in every root that has it, and the listings
//   are merged by name: one entry for a name however many roots hold it (the first root's spelling,
//   which is also the copy a read opens), then byte order over the merged set.  So a directory that
//   spans the overlay and the install lists as one Windows folder holding both would.
void PosixPath_List_Like_Win32(const std::string & current_directory, const std::string & original_directory,
	const std::string & search_name, bool search_subdirectories, std::vector<std::string> & found);

#endif
