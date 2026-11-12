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
		m_archivedModel.clear();
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
		m_archivedModel.clear();
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

	// A loose model beats every archive, except a Reforged batch's copy of it that moves nothing the
	// logic reads (takesArchivedModel): the
	// loose master stays the fallback and the INI CRC's source, the batch adds only its LODs.
	if ( (access & (File::WRITE | File::CREATE)) == 0 && takesArchivedModel( filename ) )
	{
		file = TheArchiveFileSystem->openFile( filename );
		if ( file != NULL )
			return file;
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
// FileSystem::takesArchivedModel
//============================================================================

namespace {
	struct W3DChunk
	{
		UnsignedInt type;
		UnsignedInt length;		// of the data, without the 8 byte header
		const char *whole;		// header first
	};

	/// What the logic can read of one HLOD: muzzle, fire FX and launch names the htree lacks are found
	/// among its sub-objects (W3DModelDraw findSingleSubObj), and that search walks Lod[0], the coarsest.
	struct HLodView
	{
		std::string name, tree;																		///< the header's, up to the first NUL
		std::vector<std::string> lods;															///< each LOD array chunk whole, coarsest first
		std::vector< std::vector< std::pair<std::string, UnsignedInt> > > subObjects;	///< per level: lower-cased name, bone
		std::string aggregates, proxies;													///< those chunks whole
	};
}

static Bool readWhole( File *file, std::string &bytes )
{
	if ( file == NULL )
		return FALSE;
	const Int size = file->size();
	char *data = file->readEntireAndClose();
	bytes.assign( data, size );
	delete [] data;
	return TRUE;
}

/** The chunks of one level of a W3D file; FALSE when they do not add up to its size. */
static Bool walkChunks( const char *bytes, UnsignedInt size, std::vector<W3DChunk> &chunks )
{
	UnsignedInt at = 0;
	while ( at < size )
	{
		if ( size - at < 8 )
			return FALSE;
		W3DChunk chunk;
		memcpy( &chunk.type, bytes + at, 4 );
		memcpy( &chunk.length, bytes + at + 4, 4 );
		chunk.length &= 0x7FFFFFFF;		// the top bit says "has sub-chunks"
		if ( chunk.length > size - at - 8 )
			return FALSE;
		chunk.whole = bytes + at;
		chunks.push_back( chunk );
		at += 8 + chunk.length;
	}
	return TRUE;
}

static std::string chunkName( const char *name, size_t length, Bool lower )
{
	std::string s( name, strnlen( name, length ) );
	if ( lower )
		for ( size_t i = 0; i < s.size(); ++i )
			s[i] = (char)tolower( (unsigned char)s[i] );
	return s;
}

static Bool readHLod( const W3DChunk &hlod, HLodView &view )
{
	std::vector<W3DChunk> parts;
	if ( !walkChunks( hlod.whole + 8, hlod.length, parts ) )
		return FALSE;
	Int headers = 0;
	for ( size_t i = 0; i < parts.size(); ++i )
	{
		const W3DChunk &part = parts[i];
		const char *data = part.whole + 8;
		if ( part.type == 0x701 )				// HLOD_HEADER: version, level count, name, hierarchy name
		{
			if ( part.length < 40 )
				return FALSE;
			view.name = chunkName( data + 8, 16, FALSE );
			view.tree = chunkName( data + 24, 16, FALSE );
			++headers;
		}
		else if ( part.type == 0x702 )		// HLOD_LOD_ARRAY: an array header and a SUB_OBJECT (bone, name) each
		{
			view.lods.push_back( std::string( part.whole, 8 + part.length ) );
			view.subObjects.resize( view.subObjects.size() + 1 );
			std::vector<W3DChunk> records;
			if ( !walkChunks( data, part.length, records ) )
				return FALSE;
			for ( size_t r = 0; r < records.size(); ++r )
			{
				if ( records[r].type != 0x704 || records[r].length < 36 )
					continue;
				UnsignedInt bone;
				memcpy( &bone, records[r].whole + 8, 4 );
				view.subObjects.back().push_back( std::make_pair( chunkName( records[r].whole + 12, 32, TRUE ), bone ) );
			}
		}
		else if ( part.type == 0x705 )
			view.aggregates.append( part.whole, 8 + part.length );
		else if ( part.type == 0x706 )
			view.proxies.append( part.whole, 8 + part.length );
	}
	return headers == 1 && !view.lods.empty();
}

/** The batch copy has the master's top level, aggregates and proxies byte for byte, its names, and
	* no coarser level binds a name the master has to another bone. */
static Bool sameHLodBindings( const HLodView &master, const HLodView &copy )
{
	if ( master.name != copy.name || master.tree != copy.tree || master.lods.back() != copy.lods.back()
		|| master.aggregates != copy.aggregates || master.proxies != copy.proxies )
		return FALSE;

	std::map<std::string, UnsignedInt> bone;
	for ( size_t level = master.subObjects.size(); level-- > 0; )		// the top level's binding first
		for ( size_t i = 0; i < master.subObjects[level].size(); ++i )
			bone.insert( master.subObjects[level][i] );
	for ( size_t level = 0; level + 1 < copy.subObjects.size(); ++level )
		for ( size_t i = 0; i < copy.subObjects[level].size(); ++i )
		{
			std::map<std::string, UnsignedInt>::const_iterator it = bone.find( copy.subObjects[level][i].first );
			if ( it != bone.end() && it->second != copy.subObjects[level][i].second )
				return FALSE;
		}
	return TRUE;
}

/** The top-level chunks byte for byte - HIERARCHY 0x100, ANIMATION 0x200, COMPRESSED_ANIMATION 0x280,
	* MORPH_ANIMATION 0x2C0, HMODEL 0x300 - and every HLOD's bindings.  An _SKN on a retail _SKL has
	* none of the first, so its HLOD is what decides. */
static Bool sameLogicView( const std::string &master, const std::string &copy )
{
	const std::string *files[2] = { &master, &copy };
	std::string fixed[2];
	std::vector<HLodView> hlods[2];
	for ( Int f = 0; f < 2; ++f )
	{
		std::vector<W3DChunk> chunks;
		if ( !walkChunks( files[f]->data(), (UnsignedInt)files[f]->size(), chunks ) )
			return FALSE;
		for ( size_t i = 0; i < chunks.size(); ++i )
		{
			const UnsignedInt type = chunks[i].type;
			if ( type == 0x100 || type == 0x200 || type == 0x280 || type == 0x2C0 || type == 0x300 )
				fixed[f].append( chunks[i].whole, 8 + chunks[i].length );
			else if ( type == 0x700 )
			{
				hlods[f].resize( hlods[f].size() + 1 );
				if ( !readHLod( chunks[i], hlods[f].back() ) )
					return FALSE;
			}
		}
	}
	if ( fixed[0] != fixed[1] || hlods[0].size() != hlods[1].size() )
		return FALSE;
	for ( size_t i = 0; i < hlods[0].size(); ++i )
		if ( !sameHLodBindings( hlods[0][i], hlods[1][i] ) )
			return FALSE;
	return TRUE;
}

Bool FileSystem::takesArchivedModel( const Char *filename )
{
	if ( TheLocalFileSystem == NULL || TheArchiveFileSystem == NULL )
		return FALSE;

	// the W3D loader asks for "Art/W3D/x.w3d", the archive lists "art\w3d\x.w3d"
	std::string path( filename );
	for ( size_t i = 0; i < path.size(); ++i )
		path[i] = ( path[i] == '/' ) ? '\\' : (char)tolower( (unsigned char)path[i] );
	if ( path.size() < 12 || path.compare( 0, 8, "art\\w3d\\" ) != 0 || path.compare( path.size() - 4, 4, ".w3d" ) != 0 )
		return FALSE;

	const unsigned key = TheNameKeyGenerator->nameToLowercaseKey( path.c_str() );
	{
		std::lock_guard<std::mutex> lock( m_fileExistLock );
		std::map<unsigned,bool>::const_iterator it = m_archivedModel.find( key );
		if ( it != m_archivedModel.end() )
			return it->second;
	}

	// The tree's holder differs from the retail one only where a Reforged batch went over the model;
	// Classic and Detailed Models off mount none, so they never get past this.
	const AsciiString model( path.c_str() );
	Bool archived = FALSE;
	if ( TheArchiveFileSystem->getArchiveFilenameForFile( model ) != TheArchiveFileSystem->getRetailArchiveFilenameForFile( model )
		&& TheLocalFileSystem->doesFileExist( filename ) )
	{
		std::string loose, packed;
		archived = readWhole( TheLocalFileSystem->openFile( filename, File::READ | File::BINARY ), loose )
			&& readWhole( TheArchiveFileSystem->openFile( filename ), packed )
			&& sameLogicView( loose, packed );
		DEBUG_LOG(( "FileSystem::takesArchivedModel - %s from %s\n", filename,
			archived ? "the Reforged batch" : "the loose master, the batch moves what the logic reads" ));
	}

	std::lock_guard<std::mutex> lock( m_fileExistLock );
	m_archivedModel[ key ] = archived;
	return archived;
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
