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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   WSYS Library
//
// Module:    IO
//
// File name: IO_FileSystem.cpp
//
// Created:   4/23/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/file.h"
#include "Common/FileSystem.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/CDManager.h"
#include "Common/GameAudio.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/RAMFile.h"
#include "Common/RandomMapGenerator.h"


DECLARE_PERF_TIMER(FileSystem)

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------

//===============================
// TheFileSystem 
//===============================
/**
  *	This is the FileSystem's singleton class. All file access 
	* should be through TheFileSystem, unless code needs to use an explicit
	* File or FileSystem derivative.
	*
	* Using TheFileSystem->open and File exclusively for file access, particularly
	* in library or modular code, allows applications to transparently implement 
	* file access as they see fit. This is particularly important for code that
	* needs to be shared between applications, such as games and tools.
	*/
//===============================

FileSystem	*TheFileSystem = NULL;

//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//============================================================================
// FileSystem::FileSystem
//============================================================================

FileSystem::FileSystem()
{

}

//============================================================================
// FileSystem::~FileSystem
//============================================================================

FileSystem::~FileSystem()
{

}

//============================================================================
// FileSystem::init
//============================================================================

void		FileSystem::init( void )
{
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist.clear();		// see reset(): the cache outlives whatever it was an answer about
	}
	TheLocalFileSystem->init();
	TheArchiveFileSystem->init();
}

//============================================================================
// FileSystem::update
//============================================================================

void		FileSystem::update( void )
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->update();
	TheArchiveFileSystem->update();
}

//============================================================================
// FileSystem::reset
//============================================================================

void		FileSystem::reset( void )
{
	USE_PERF_TIMER(FileSystem)

	// doesFileExist remembers its answer for the life of the process and this is the only place
	// that ever gets told the world underneath it changed. A "no" cached before
	// Win32BIGFileSystem::init re-loaded the Patch*.big over the plain pass made
	// that file invisible for the rest of the run - and the cache is keyed by name, so nothing else
	// would ever ask again.
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist.clear();
	}

	TheLocalFileSystem->reset();
	TheArchiveFileSystem->reset();
}

//============================================================================
// FileSystem::open
//============================================================================

File*		FileSystem::openFile( const Char *filename, Int access )
{
	USE_PERF_TIMER(FileSystem)
	File *file = NULL;

	//
	// A generated map is not on any disk and never will be: its seed, player count and size are in
	// its name, so the bytes are built from the name on the first read and served out of memory
	// after that.  It goes in front of both file systems because the map cache, the loader, the
	// CRC and the preview all reach a map through here and none of them has to know the difference.
	//
	const Char *generated = NULL;
	Int generatedSize = 0;
	if ( generatedMapBytes( AsciiString( filename ), &generated, &generatedSize ) )
	{
		RAMFile *ramFile = newInstance( RAMFile );
		if ( ramFile->openFromMemory( generated, generatedSize, AsciiString( filename ) ) )
			return ramFile;

		ramFile->close();
		ramFile->deleteInstance();
		return NULL;
	}

	if ( TheLocalFileSystem != NULL )
	{
		file = TheLocalFileSystem->openFile( filename, access );
	}

	/* A file made here exists from now on, and doesFileExist may already have said it did not: a
		 map transfer asks before it writes, and the map.ini it then writes was skipped by the loader
		 on the receiving machine, which played with the stock data while the host played with the
		 map's (upstream #991). */
	if ( file != NULL && (access & File::CREATE) )
	{
		const unsigned key = TheNameKeyGenerator->nameToLowercaseKey( filename );
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		m_fileExist[ key ] = true;
	}

	if ( (TheArchiveFileSystem != NULL) && (file == NULL) )
	{
		file = TheArchiveFileSystem->openFile( filename );
	}

	return file;
}

//============================================================================
// FileSystem::doesFileExist
//============================================================================

Bool FileSystem::doesFileExist(const Char *filename) const
{
	USE_PERF_TIMER(FileSystem)

	// a generated map exists wherever its name can be read, which is everywhere - see openFile
	if (isGeneratedMapPath( AsciiString( filename ) ))
		return TRUE;

  unsigned key=TheNameKeyGenerator->nameToLowercaseKey(filename);
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		std::map<unsigned,bool>::iterator i=m_fileExist.find(key);
		if (i!=m_fileExist.end())
			return i->second;
	}

	// asked unlocked, so a slow disk does not hold the other thread up; two threads asking the same
	// name both find the same answer and store it twice
	const Bool exists = TheLocalFileSystem->doesFileExist(filename)
		|| (TheArchiveFileSystem != NULL && TheArchiveFileSystem->doesFileExist(filename));

	std::lock_guard<std::mutex> lock( m_fileExistLock );
	m_fileExist[key]=exists != FALSE;
	return exists;
}

//============================================================================
// FileSystem::getFileListInDirectory
//============================================================================
void FileSystem::getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	USE_PERF_TIMER(FileSystem)
	TheLocalFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
	TheArchiveFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
}

//============================================================================
// FileSystem::getFileInfo
//============================================================================
Bool FileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	USE_PERF_TIMER(FileSystem)
	if (fileInfo == NULL) {
		return FALSE;
	}
	memset(fileInfo, 0, sizeof(fileInfo));

	// a generated map has a size and no timestamp: the bytes follow from the name, so there is no
	// moment at which they were made and nothing that could make them go stale
	const Char *generated = NULL;
	Int generatedSize = 0;
	if (generatedMapBytes(filename, &generated, &generatedSize)) {
		fileInfo->sizeLow = generatedSize;
		return TRUE;
	}

	if (TheLocalFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	if (TheArchiveFileSystem->getFileInfo(filename, fileInfo)) {
		return TRUE;
	}

	return FALSE;
}

//============================================================================
// FileSystem::createDirectory
//============================================================================
Bool FileSystem::createDirectory(AsciiString directory) 
{
	USE_PERF_TIMER(FileSystem)
	if (TheLocalFileSystem != NULL) {
		return TheLocalFileSystem->createDirectory(directory);
	}
	return FALSE;
}

//============================================================================
// FileSystem::installedLanguage
//============================================================================
/* The registry's Language outlives the files it names: a Steam install switched from German to
	 English keeps Language=german with only EnglishZH.big beside the exe, and GlobalLanguage::init
	 then threw on Data\german\Language.ini before the first frame (player report #39). A German-only
	 install has no english to fall back to either (issue #63), so every retail language is tried.
	 Empty when no Language.ini is found at all. */
static const char *const RetailLanguages[] = { "english", "german", "french", "spanish", "italian",
	"korean", "chinese", "brazilian", "polish", "russian" };

AsciiString FileSystem::installedLanguage(const AsciiString &language) const
{
	AsciiString probe;
	if (!language.isEmpty()) {
		probe.format("Data\\%s\\Language.ini", language.str());
		if (doesFileExist(probe.str()))
			return language;
	}
	for (Int i = 0; i < (Int)ARRAY_SIZE(RetailLanguages); ++i) {
		probe.format("Data\\%s\\Language.ini", RetailLanguages[i]);
		if (doesFileExist(probe.str()))
			return AsciiString(RetailLanguages[i]);
	}
	return AsciiString::TheEmptyString;
}

//============================================================================
// FileSystem::zeroHourLanguage
//============================================================================
/* With no Language in the registry (a Linux or Mac install never ran EA's installer, which writes it)
	 the default english finds the base game's English.big in ZH_Generals, so a German Zero Hour showed
	 Generals' text with Zero Hour's strings missing.  Zero Hour's own language archive says which it is. */
AsciiString FileSystem::zeroHourLanguage() const
{
	AsciiString probe;
	for (Int i = 0; i < (Int)ARRAY_SIZE(RetailLanguages); ++i) {
		probe.format("%sZH.big", RetailLanguages[i]);
		if (TheLocalFileSystem->doesFileExist(probe.str()))
			return AsciiString(RetailLanguages[i]);
	}
	return AsciiString::TheEmptyString;
}

//============================================================================
// FileSystem::areMusicFilesOnCD
//============================================================================
Bool FileSystem::areMusicFilesOnCD()
{
	// No-CD layouts (Steam) ship the security big next to the exe instead of on
	// a disc; finding it there is the same proof of media this check wants, so
	// look locally first and never prompt for a CD the install cannot have.
	File *localSec = TheLocalFileSystem->openFile("genseczh.big");
	if (localSec) {
		localSec->close();
		return TRUE;
	}

	if (!TheCDManager) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - No CD Manager; returning false\n"));
		return FALSE;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking drive %d\n", i));
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (!cdRoot.endsWith("\\"))
			cdRoot.concat("\\");
		cdRoot.concat("genseczh.big");
		DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - checking for %s\n", cdRoot.str()));
		File *musicBig = TheLocalFileSystem->openFile(cdRoot.str());
		if (musicBig)
		{
			DEBUG_LOG(("FileSystem::areMusicFilesOnCD() - found it!\n"));
			musicBig->close();
			return TRUE;
		}
	}
	return FALSE;
}
//============================================================================
// FileSystem::loadMusicFilesFromCD
//============================================================================
void FileSystem::loadMusicFilesFromCD()
{
	if (!TheCDManager) {
		return;
	}

	AsciiString cdRoot;
	Int dc = TheCDManager->driveCount();
	for (Int i = 0; i < dc; ++i) {
		CDDriveInterface *cdi = TheCDManager->getDrive(i);
		if (!cdi) {
			continue;
		}

		cdRoot = cdi->getPath();
		if (TheArchiveFileSystem->loadBigFilesFromDirectory(cdRoot, MUSIC_BIG)) {
			break;
		}
	}
}

//============================================================================
// FileSystem::unloadMusicFilesFromCD
//============================================================================
void FileSystem::unloadMusicFilesFromCD()
{
	if (!(TheAudio && TheAudio->isMusicPlayingFromCD())) {
		return;
	}

	TheArchiveFileSystem->closeArchiveFile( MUSIC_BIG );
}
