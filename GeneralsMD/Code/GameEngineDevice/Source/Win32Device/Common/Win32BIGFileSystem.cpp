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

//////// Win32BIGFileSystem.h ///////////////////////////
// Bryan Cleveland, August 2002
/////////////////////////////////////////////////////////////

// Built off Windows too (C1 (f)): nothing below is Win32 but ntohl and one message box, so macOS and
// Linux mount archives with this same code rather than a copy whose load order could drift.
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>	// MessageBox, for the one thing a player has to be told before the menu
#else
#include <arpa/inet.h>	// ntohl
#include "Common/MessageBoxFlags.h"
#endif
#include "Common/AudioAffect.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/file.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFile.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/Registry.h"
#include "Common/EarlyOptions.h"
#include "Common/EarlyCommandLine.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static const char *BIGFileIdentifier = "BIGF";

// What makes a folder the base game's.  Textures.big is where the shell map's ground comes from,
// and an uninstalled copy takes it away while leaving its registry key behind.
static const char *const BASE_GAME_ARCHIVE = "Textures.big";

// Where to look when the registry does not say.  Steam writes no key and installs the two games as
// sibling folders under steamapps\common (apps 2229870 and 2732960), so the base game is one folder
// over; a copied-over install puts the same bigs in ZH_Generals next to the exe.  The First Decade
// does the same with its own folder name, and registers the collection's root folder as the base
// game's InstallPath, so a player on v2.0.1 got "no base game archives" with Generals one folder
// over.  The EA app and Origin spell it with "and", under one folder holding both games, and register
// no InstallPath for it: three v3.2.0 reports from such installs stopped at "no base game archives".
static const char *const BASE_GAME_DIRECTORIES[] = {
	"ZH_Generals\\",
	"..\\Command & Conquer Generals\\",
	"..\\Command & Conquer(tm) Generals\\",
	"..\\Command and Conquer Generals\\",
};

// The folder inside the First Decade's registered root that holds the base game.
static const char FIRST_DECADE_GENERALS_FOLDER[] = "Command & Conquer(tm) Generals\\";

static Bool holdsBaseGameArchives(const char *directory)
{
	AsciiString archive = directory;
	archive.concat(BASE_GAME_ARCHIVE);
	return TheLocalFileSystem->doesFileExist(archive.str());
}

// The game starts without the base archives and then looks and sounds broken: magenta ground and
// water, "Missing Audio File" for half the sound effects, no music and no tree models.  None of
// that names its own cause, and a player staring at a pink main menu has nothing to go on, so it is
// said here - before the menu, in the one place that knows.
static void reportMissingBaseGame(void)
{
	DEBUG_LOG(("Win32BIGFileSystem::init - no base game archives anywhere; most of the art and audio will be missing.\n"));
	// An unattended run (-headless or ZH_UNATTENDED) has nobody to press OK: a box here held a CI run for 38
	// minutes (W-ARM64), and a run without the base game's art could not have told anyone anything anyway.
	// The reason on stderr, and out.
	if (isUnattendedProcess())
	{
		fprintf(stderr, "generals: none of the base game's .big files could be found (Textures.big in the "
			"registered Generals folder, ZH_Generals or a sibling Command & Conquer Generals folder); an "
			"unattended run stops here rather than wait on a message box\n");
		fflush(stderr);
		_exit(2);
	}
#if defined(_WIN32)
	::MessageBox(NULL,
		"Zero Hour shares most of its artwork, sound effects and music with Command & Conquer Generals, "
		"and none of the base game's .big files could be found.\n\n"
		"Install Generals, or copy its .big files into a folder named ZH_Generals next to generals.exe.",
		"Zero Hour Reforged",
		MB_OK | MB_ICONWARNING | MB_TASKMODAL);
#else
	MessageBoxWrapper(
		"Zero Hour shares most of its artwork, sound effects and music with Command & Conquer Generals, "
		"and none of the base game's .big files could be found.\n\n"
		"Install Generals, or copy its .big files into a folder named ZH_Generals beside the game.",
		"Zero Hour Reforged",
		MSGBOX_OK | MSGBOX_ICONWARNING | MSGBOX_TASKMODAL);
#endif
}

Win32BIGFileSystem::Win32BIGFileSystem() : ArchiveFileSystem() {
}

Win32BIGFileSystem::~Win32BIGFileSystem() {
}

// The classic graphics setting, read out of Options.ini before anything mounts: the archives go in
// long before GlobalData exists, and the fork's upscaled art is the one thing classic has to keep
// out of the directory tree rather than switch off afterwards.
static Bool theClassicGraphics = FALSE;
static const char REFORGED_ARCHIVE_PREFIX[] = "Reforged";

static Bool isReforgedArchive(const AsciiString &path)
{
	const char *name = strrchr(path.str(), '\\');
	name = (name != NULL) ? name + 1 : path.str();
	return strncasecmp(name, REFORGED_ARCHIVE_PREFIX, sizeof(REFORGED_ARCHIVE_PREFIX) - 1) == 0;
}

void Win32BIGFileSystem::init() {
	DEBUG_ASSERTCRASH(TheLocalFileSystem != NULL, ("TheLocalFileSystem must be initialized before TheArchiveFileSystem."));
	if (TheLocalFileSystem == NULL) {
		return;
	}

	theClassicGraphics = getEarlyOptionBool("ClassicGraphics", true);

	//
	// The exe's own directory and no further down.  This used to walk every subdirectory under it,
	// which had two consequences.  A stray archive - one extracted into Data\INI, say - joined the
	// set without anyone asking, and since the INI CRC covers the INI files as resolved, that is a
	// silent "join denied, different INI set" for everyone who has not got the same stray file.  And
	// ZH_Generals\*.big, which the explicit pass below loads on purpose and in a particular order,
	// was found and parsed by this pass as well: fifteen archives opened twice at every start.
	//
	loadBigFilesFromDirectory("", "*.big", FALSE, FALSE);

	// loadIntoDirectoryTree keeps the FIRST archive that claims a path and the plain
	// *.big pass above enumerates case-insensitively alphabetically, so INIZH.big claims
	// Data\INI\... before PatchINI.big is even looked at and the 1.04 patch content is
	// unreachable.  Re-load the patch archives with overwrite so they win, like retail.
	loadBigFilesFromDirectory("", "Patch*.big", TRUE, FALSE);

    // Zero Hour is not standalone: the base game's bigs carry most of the art, the sound effects
    // and the music.  A retail install registers the key below and Steam registers nothing, so a
    // folder that holds Textures.big is what this looks for rather than a key that exists.  The
    // pass above already loaded whatever sits next to the exe, so there is only something to fetch
    // when the base archives are not already there.
    if (!holdsBaseGameArchives(""))
    {
      AsciiString installPath;
      GetStringFromGeneralsRegistry("", "InstallPath", installPath );
      if (!installPath.isEmpty() && !installPath.endsWith("\\"))
      {
        installPath.concat("\\");
      }
      if (!installPath.isEmpty() && !holdsBaseGameArchives(installPath.str()))
      {
        AsciiString firstDecade = installPath;
        firstDecade.concat(FIRST_DECADE_GENERALS_FOLDER);
        if (holdsBaseGameArchives(firstDecade.str()))
        {
          installPath = firstDecade;
        }
      }
      // An uninstalled retail or First Decade copy leaves its key behind, and trusting it loaded
      // no base archive at all: the water and the ground came up magenta and black on the shell map.
      if (installPath.isEmpty() || !holdsBaseGameArchives(installPath.str()))
      {
        DEBUG_LOG(("Win32BIGFileSystem::init - no base game archives in the registered folder '%s'\n", installPath.str()));
        installPath.clear();
        for (Int i = 0; i < (Int)ARRAY_SIZE(BASE_GAME_DIRECTORIES) && installPath.isEmpty(); i++)
        {
          if (holdsBaseGameArchives(BASE_GAME_DIRECTORIES[i]))
          {
            installPath = BASE_GAME_DIRECTORIES[i];
          }
        }
      }
      if (installPath.isEmpty())
      {
        reportMissingBaseGame();
      }
      else
      {
        // Loaded second on purpose: loadIntoDirectoryTree does not overwrite, so the
        // Zero Hour bigs already in the tree win over the base game's copies.
        DEBUG_LOG(("Win32BIGFileSystem::init - loading the base game's archives from '%s'\n", installPath.str()));
        loadBigFilesFromDirectory(installPath, "*.big");
        m_baseGameDirectory = installPath;
      }
    }

    // ... except where that costs resolution: a number of the base game's textures were shipped
    // downscaled in TexturesZH.big, and load order alone made those the ones the game uses.
    prioritizeLargerFiles(TGA_DIR_PATH, "TexturesZH.big", "Textures.big");
}

void Win32BIGFileSystem::reset() {
}

void Win32BIGFileSystem::update() {
}

void Win32BIGFileSystem::postProcessLoad() {
}

ArchiveFile * Win32BIGFileSystem::openArchiveFile(const Char *filename) {
	File *fp = TheLocalFileSystem->openFile(filename, File::READ | File::BINARY);
	AsciiString archiveFileName;
	archiveFileName = filename;
	archiveFileName.toLower();
	Int archiveFileSize = 0;
	Int numLittleFiles = 0;

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - opening BIG file %s\n", filename));

	if (fp == NULL) {
		DEBUG_CRASH(("Could not open archive file %s for parsing", filename));
		return NULL;
	}

	AsciiString asciibuf;
	char buffer[_MAX_PATH];
	fp->read(buffer, 4); // read the "BIG" at the beginning of the file.
	buffer[4] = 0;
	if (strcmp(buffer, BIGFileIdentifier) != 0) {
#if defined(_WIN32)
		DEBUG_CRASH(("Error reading BIG file identifier in file %s", filename));
#else
		// Quietly, and once for each such file: macOS leaves a "._" AppleDouble companion beside every
		// file it copies onto exFAT or FAT, and "*.big" finds them, so an install that came off such a
		// volume has twenty of these.  They are not archives and nothing is lost by leaving them out
		// (C1, PR (f)).
		DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - %s is not a BIG archive (no BIGF), left out\n", filename));
#endif
		fp->close();
		fp = NULL;
		return NULL;
	}

	// Allocated after the checks above, not before them: both of those returns used to walk away
	// from a Win32BIGFile that had just been made. A directory of files that are not archives -
	// which is what a mod folder handed to -mod can be - leaked one per file.
	ArchiveFile *archiveFile = NEW Win32BIGFile;

	// read in the file size.
	fp->read(&archiveFileSize, 4);

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - size of archive file is %d bytes\n", archiveFileSize));

//	char t;

	// read in the number of files contained in this BIG file.
	// change the order of the bytes cause the file size is in reverse byte order for some reason.
	fp->read(&numLittleFiles, 4);
	numLittleFiles = ntohl(numLittleFiles);

	DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - %d are contained in archive\n", numLittleFiles));
//	for (Int i = 0; i < 2; ++i) {
//		t = buffer[i];
//		buffer[i] = buffer[(4-i)-1];
//		buffer[(4-i)-1] = t;
//	}

	// seek to the beginning of the directory listing.
	fp->seek(0x10, File::START);
	// read in each directory listing.
	ArchivedFileInfo *fileInfo = NEW ArchivedFileInfo;

	for (Int i = 0; i < numLittleFiles; ++i) {
		Int filesize = 0;
		Int fileOffset = 0;
		fp->read(&fileOffset, 4);
		fp->read(&filesize, 4);

		filesize = ntohl(filesize);
		fileOffset = ntohl(fileOffset);

		fileInfo->m_archiveFilename = archiveFileName;
		fileInfo->m_offset = fileOffset;
		fileInfo->m_size = filesize;
		
		// read in the path name of the file.
		Int pathIndex = -1;
		do {
			++pathIndex;
			fp->read(buffer + pathIndex, 1);
		} while (buffer[pathIndex] != 0);

		Int filenameIndex = pathIndex;
		// the index test first: a name with no separator used to read buffer[-1] before the test stopped it
		while ((filenameIndex >= 0) && (buffer[filenameIndex] != '\\') && (buffer[filenameIndex] != '/')) {
			--filenameIndex;
		}

		fileInfo->m_filename = (char *)(buffer + filenameIndex + 1);
		fileInfo->m_filename.toLower();
		buffer[filenameIndex + 1] = 0;

		AsciiString path;
		path = buffer;

		AsciiString debugpath;
		debugpath = path;
		debugpath.concat(fileInfo->m_filename);
//		DEBUG_LOG(("Win32BIGFileSystem::openArchiveFile - adding file %s to archive file %s, file number %d\n", debugpath.str(), fileInfo->m_archiveFilename.str(), i));

		archiveFile->addFile(path, fileInfo);
	}

	archiveFile->attachFile(fp);

	delete fileInfo;
	fileInfo = NULL;

	// leave fp open as the archive file will be using it.

	return archiveFile;
}

void Win32BIGFileSystem::closeArchiveFile(const Char *filename) {
	// Need to close the specified big file
	ArchiveFileMap::iterator it =  m_archiveFileMap.find(filename);
	if (it == m_archiveFileMap.end()) {
		return;
	}

	if (strcasecmp(filename, MUSIC_BIG) == 0) {
		// Stop the current audio
		TheAudio->stopAudio(AudioAffect_Music);

		// No need to turn off other audio, as the lookups will just fail.
	}
	DEBUG_ASSERTCRASH(strcasecmp(filename, MUSIC_BIG) == 0, ("Attempting to close Archive file '%s', need to add code to handle its shutdown correctly.", filename));

	// may need to do some other processing here first.
	
	delete (it->second);
	m_archiveFileMap.erase(it);
}

void Win32BIGFileSystem::closeAllArchiveFiles() {
}

void Win32BIGFileSystem::closeAllFiles() {
}

Bool Win32BIGFileSystem::loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite, Bool searchSubdirectories) {

	FilenameList filenameList;
	TheLocalFileSystem->getFileListInDirectory(dir, AsciiString(""), fileMask, filenameList, searchSubdirectories);

	Bool actuallyAdded = FALSE;
	FilenameListIter it = filenameList.begin();
	while (it != filenameList.end()) {
		if (theClassicGraphics && isReforgedArchive(*it)) {
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - classic graphics, leaving %s out.\n", (*it).str()));
			it++;
			continue;
		}

		ArchiveFile *archiveFile = openArchiveFile((*it).str());

		if (archiveFile != NULL) {
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - loading %s into the directory tree.\n", (*it).str()));
			// The fork's art claims over what was mounted before it: ReforgedUI.big sorts after
			// EnglishZH.big and has to beat its Data\English\Art\Textures interface pages.
			loadIntoDirectoryTree(archiveFile, *it, overwrite || isReforgedArchive(*it));
			m_archiveFileMap[(*it)] = archiveFile;
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - %s inserted into the archive file map.\n", (*it).str()));
			actuallyAdded = TRUE;
		}

		it++;
	}

	return actuallyAdded;
}
