/*
**	Command & Conquer Generals(tm)
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//////// Win32BIGFileSystem.h ///////////////////////////
// Bryan Cleveland, August 2002
/////////////////////////////////////////////////////////////

#include <winsock2.h>
#include "Common/AudioAffect.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/File.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFile.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static const char *BIGFileIdentifier = "BIGF";

Win32BIGFileSystem::Win32BIGFileSystem() : ArchiveFileSystem() {
}

Win32BIGFileSystem::~Win32BIGFileSystem() {
}

void Win32BIGFileSystem::init() {
	DEBUG_ASSERTCRASH(TheLocalFileSystem != NULL, ("TheLocalFileSystem must be initialized before TheArchiveFileSystem."));
	if (TheLocalFileSystem == NULL) {
		return;
	}

	loadBigFilesFromDirectory("", "*.big");
}

void Win32BIGFileSystem::reset() {
}

void Win32BIGFileSystem::update() {
}

void Win32BIGFileSystem::postProcessLoad() {
}

// Discard the whole index on malformed input; no partially mounted archive escapes.
static ArchiveFile *rejectBIGArchive(File *file, ArchiveFile *archive)
{
	file->close();
	delete archive;
	return NULL;
}

ArchiveFile * Win32BIGFileSystem::openArchiveFile(const Char *filename) {
	File *fp = TheLocalFileSystem->openFile(filename, File::READ | File::BINARY);
	if (fp == NULL) return NULL;
	const Int actualSize = fp->size();
	char buffer[_MAX_PATH];
	if (actualSize < 16 || fp->read(buffer, 4) != 4 || memcmp(buffer, BIGFileIdentifier, 4) != 0)
		return rejectBIGArchive(fp, NULL);
	Int archiveSize = 0, count = 0, dataStart = 0;
	if (fp->read(&archiveSize, 4) != 4 || fp->read(&count, 4) != 4 || fp->read(&dataStart, 4) != 4)
		return rejectBIGArchive(fp, NULL);
	count = ntohl(count);
	if (count < 0 || count > (actualSize - 16) / 9) return rejectBIGArchive(fp, NULL);
	// The stored archive size and data-start fields are advisory in retail archives.
	ArchiveFile *archive = NEW Win32BIGFile;
	archive->attachFile(NULL);
	AsciiString archiveName(filename);
	archiveName.toLower();
	Int firstData = actualSize;
	for (Int i = 0; i < count; ++i) {
		Int offset = 0, size = 0;
		if (fp->read(&offset, 4) != 4 || fp->read(&size, 4) != 4)
			return rejectBIGArchive(fp, archive);
		offset = ntohl(offset);
		size = ntohl(size);
		if (offset < 16 || offset > actualSize || size < 0 || size > actualSize - offset)
			return rejectBIGArchive(fp, archive);
		if (offset < firstData) firstData = offset;
		Int pathLength = 0;
		do {
			if (pathLength >= (Int)sizeof(buffer) || fp->read(buffer + pathLength, 1) != 1)
				return rejectBIGArchive(fp, archive);
		} while (buffer[pathLength++] != 0);
		if (pathLength == 1) return rejectBIGArchive(fp, archive);
		Int filenameIndex = pathLength - 1;
		while (filenameIndex >= 0 && buffer[filenameIndex] != '\\' && buffer[filenameIndex] != '/')
			--filenameIndex;
		ArchivedFileInfo info;
		info.m_archiveFilename = archiveName;
		info.m_offset = offset;
		info.m_size = size;
		info.m_filename = buffer + filenameIndex + 1;
		if (info.m_filename.isEmpty()) return rejectBIGArchive(fp, archive);
		info.m_filename.toLower();
		buffer[filenameIndex + 1] = 0;
		archive->addFile(AsciiString(buffer), &info);
	}
	if (fp->position() > firstData) return rejectBIGArchive(fp, archive);
	archive->attachFile(fp);
	return archive;
}

void Win32BIGFileSystem::closeArchiveFile(const Char *filename) {
	// Need to close the specified big file
	ArchiveFileMap::iterator it =  m_archiveFileMap.find(filename);
	if (it == m_archiveFileMap.end()) {
		return;
	}

	if (stricmp(filename, MUSIC_BIG) == 0) {
		// Stop the current audio
		TheAudio->stopAudio(AudioAffect_Music);

		// No need to turn off other audio, as the lookups will just fail.
	}
	DEBUG_ASSERTCRASH(stricmp(filename, MUSIC_BIG) == 0, ("Attempting to close Archive file '%s', need to add code to handle its shutdown correctly.", filename));

	// may need to do some other processing here first.
	
	delete (it->second);
	m_archiveFileMap.erase(it);
}

void Win32BIGFileSystem::closeAllArchiveFiles() {
}

void Win32BIGFileSystem::closeAllFiles() {
}

Bool Win32BIGFileSystem::loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite) {

	FilenameList filenameList;
	TheLocalFileSystem->getFileListInDirectory(dir, AsciiString(""), fileMask, filenameList, TRUE);

	Bool actuallyAdded = FALSE;
	FilenameListIter it = filenameList.begin();
	while (it != filenameList.end()) {
		ArchiveFile *archiveFile = openArchiveFile((*it).str());

		if (archiveFile != NULL) {
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - loading %s into the directory tree.\n", (*it).str()));
			loadIntoDirectoryTree(archiveFile, *it, overwrite);
			m_archiveFileMap[(*it)] = archiveFile;
			DEBUG_LOG(("Win32BIGFileSystem::loadBigFilesFromDirectory - %s inserted into the archive file map.\n", (*it).str()));
			actuallyAdded = TRUE;
		}

		it++;
	}

	return actuallyAdded;
}
