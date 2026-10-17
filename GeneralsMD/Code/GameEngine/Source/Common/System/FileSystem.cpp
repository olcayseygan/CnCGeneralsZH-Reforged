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
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"
#include "Common/RAMFile.h"
#include "Common/RandomMapGenerator.h"

#include <set>
#include <string>


DECLARE_PERF_TIMER(FileSystem)

/// a drive or a root: the user data folder and the like, which a mod folder never stands in for
static Bool isAbsolutePath( const AsciiString &path )
{
	return strchr( path.str(), ':' ) != NULL || path.startsWith( "\\" ) || path.startsWith( "/" );
}

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

Int FileSystem::s_focus = FILE_FOCUS_ALL;

FileSystem::FileSystem()
{

}

//============================================================================
// FileSystem::setFocus
//============================================================================

void FileSystem::setFocus( Int focus )
{
	s_focus = focus;
	TheArchiveFileSystem->setFocus( focus );
}

//============================================================================
// FileSystem::focusedLooseFile
//============================================================================

AsciiString FileSystem::focusedLooseFile( const AsciiString& filename ) const
{
	// retail is Run's own loose files under the patches given before any -mod (modLooseFile); a mod is
	// its folder's and its patches', and a mod that is one .big has none
	if (s_focus == FILE_FOCUS_RETAIL)
	{
		Bool inPatchArchive = FALSE;
		AsciiString patched = modLooseFile( filename, &inPatchArchive );
		if (patched.isNotEmpty() || inPatchArchive)
			return patched;
		return TheLocalFileSystem->doesFileExist( filename.str() ) ? filename : AsciiString();
	}

	return modFolderFile( s_focus, filename );
}

/** -modfactions: the folders a faction set's loose files come from, its -modpatch ones after the
	* mod's own so they win.  FILE_FOCUS_RETAIL is the patches given before any -mod. */
static std::vector<AsciiString> modLayers( Int mod )
{
	if (mod < 0)
		return TheGlobalData->m_retailPatches;
	std::vector<AsciiString> layers( 1, TheGlobalData->m_mods[ mod ] );
	if (mod < (Int)TheGlobalData->m_modPatches.size())
		layers.insert( layers.end(), TheGlobalData->m_modPatches[ mod ].begin(), TheGlobalData->m_modPatches[ mod ].end() );
	return layers;
}

AsciiString FileSystem::modFolderFile( Int mod, const AsciiString& filename ) const
{
	const std::vector<AsciiString> layers = modLayers( mod );
	for (size_t i = layers.size(); i-- > 0; )
	{
		if (!layers[i].endsWith( "\\" ) && !layers[i].endsWith( "/" ))
			continue;
		AsciiString path = layers[i];
		path.concat( filename );
		if (TheLocalFileSystem->doesFileExist( path.str() ))
			return path;
	}
	return AsciiString();
}

//============================================================================
// FileSystem::splitModName
//============================================================================

Bool FileSystem::splitModName( const AsciiString& filename, Int *mod, AsciiString *bare )
{
	if (TheGlobalData == NULL || !TheGlobalData->m_modFactions)
		return FALSE;
	const char *path = filename.str();
	const char *name = path;
	for (const char *p = path; *p != 0; ++p)
	{
		if (*p == '\\' || *p == '/')
			name = p + 1;
	}
	if ((name[ 0 ] != 'M' && name[ 0 ] != 'm') || name[ 1 ] < '0' || name[ 1 ] > '9')
		return FALSE;
	char *tilde = NULL;
	const long n = strtol( name + 1, &tilde, 10 );
	if (*tilde != '~' || n < 1 || n > (long)TheGlobalData->m_mods.size())
		return FALSE;
	*mod = (Int)n - 1;
	*bare = std::string( path, name - path ).c_str();
	bare->concat( tilde + 1 );
	return TRUE;
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
	m_fileExist.clear();		// see reset(): the cache outlives whatever it was an answer about
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
	// that ever gets told the world underneath it changed. A "no" cached before a mod archive was
	// mounted, or before Win32BIGFileSystem::init re-loaded the Patch*.big over the plain pass, made
	// that file invisible for the rest of the run - and the cache is keyed by name, so nothing else
	// would ever ask again.
	m_fileExist.clear();

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

	const Bool readOnly = (access & (File::WRITE | File::CREATE | File::APPEND | File::TRUNCATE)) == 0;
	Int mod;
	AsciiString bare;
	if ( readOnly && splitModName( AsciiString( filename ), &mod, &bare ) )
	{
		AsciiString loose = modFolderFile( mod, bare );
		if ( loose.isNotEmpty() )
			return TheLocalFileSystem->openFile( loose.str(), access );
		return TheArchiveFileSystem->openModFile( mod, bare, access );
	}
	if ( readOnly && s_focus != FILE_FOCUS_ALL )
	{
		AsciiString loose = focusedLooseFile( AsciiString( filename ) );
		if ( loose.isNotEmpty() )
			return TheLocalFileSystem->openFile( loose.str(), access );
		return TheArchiveFileSystem->openFile( filename, access );
	}

	// a mod's copy, loose or archived, goes in front of Run's own loose files and every other archive;
	// writes stay where they were
	if ( readOnly )
	{
		Bool inModArchive = FALSE;
		AsciiString modded = modLooseFile( AsciiString( filename ), &inModArchive );
		if ( modded.isNotEmpty() )
		{
			DEBUG_LOG(( "FileSystem::openFile - mod file %s\n", modded.str() ));
			return TheLocalFileSystem->openFile( modded.str(), access );
		}
		if ( inModArchive )
		{
			return TheArchiveFileSystem->openFile( filename, access );
		}
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
		m_fileExist[ TheNameKeyGenerator->nameToLowercaseKey( filename ) ] = true;
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

	Int mod;
	AsciiString bare;
	if (splitModName( AsciiString( filename ), &mod, &bare ))
		return modFolderFile( mod, bare ).isNotEmpty() || TheArchiveFileSystem->doesModFileExist( mod, bare );

	// the cache is the merged view's; a focus asks its own files every time (setFocus)
	if (s_focus != FILE_FOCUS_ALL)
		return focusedLooseFile( AsciiString( filename ) ).isNotEmpty() || TheArchiveFileSystem->doesFileExist( filename );

  unsigned key=TheNameKeyGenerator->nameToLowercaseKey(filename);
  std::map<unsigned,bool>::iterator i=m_fileExist.find(key);
  if (i!=m_fileExist.end())
    return i->second;

	if (TheLocalFileSystem->doesFileExist(filename) || modLooseFile(AsciiString(filename)).isNotEmpty())
  {
    m_fileExist[key]=true;
		return TRUE;
	}
	if (TheArchiveFileSystem != NULL && TheArchiveFileSystem->doesFileExist(filename))
  {
    m_fileExist[key]=true;
		return TRUE;
	}

  m_fileExist[key]=false;
	return FALSE;
}

//============================================================================
// FileSystem::getFileListInDirectory
//============================================================================
void FileSystem::getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	USE_PERF_TIMER(FileSystem)
	// a mod's focus does not see Run's own loose files, retail's sees no mod's (setFocus)
	if (s_focus < 0)
	{
		TheLocalFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);
	}
	TheArchiveFileSystem->getFileListInDirectory(AsciiString(""), directory, searchName, filenameList, searchSubdirectories);

	// a mod's loose files listed under their game path, so a file it replaces is loaded once, from the mod (openFile)
	// TheGlobalData is not there yet while GameData.ini itself loads, and there is no -mod before it is
	// -modfactions' merged view is retail's with the patches given before any -mod, and a focus sees
	// its own mod and that mod's patches
	if (TheGlobalData == NULL || isAbsolutePath(directory))
	{
		return;
	}
	const std::vector<AsciiString> folders = TheGlobalData->m_modFactions ? modLayers(s_focus) : TheGlobalData->m_mods;
	for (size_t i = 0; i < folders.size(); ++i)
	{
		const AsciiString &modDir = folders[i];
		if (!modDir.endsWith("\\") && !modDir.endsWith("/"))
		{
			continue;	// a .big, already in the archive tree
		}
		AsciiString modDirectory = modDir;
		modDirectory.concat(directory);
		FilenameList modded;
		TheLocalFileSystem->getFileListInDirectory(AsciiString(""), modDirectory, searchName, modded, searchSubdirectories);
		for (FilenameListIter it = modded.begin(); it != modded.end(); ++it)
		{
			AsciiString gamePath = it->str() + modDir.getLength();
			if (!isMapPath(gamePath))
				filenameList.insert(gamePath);
		}
	}
}

//============================================================================
// FileSystem::modLooseFile
//============================================================================
AsciiString FileSystem::modLooseFile(const AsciiString& filename, Bool *inModArchive) const
{
	if (inModArchive != NULL)
	{
		*inModArchive = FALSE;
	}
	// a folder is not a file: "" or "Maps\" would otherwise find the mod folder itself, which exists
	// -modfactions reaches a mod's files only by its focus or its "M<n>~" names, so here only the
	// patches given before any -mod go over retail's
	if (TheGlobalData == NULL || isAbsolutePath(filename) || filename.isEmpty() || filename.endsWith("\\") || filename.endsWith("/") || isMapPath(filename))
	{
		return AsciiString();
	}
	const std::vector<AsciiString> &mods = TheGlobalData->m_modFactions ? TheGlobalData->m_retailPatches : TheGlobalData->m_mods;
	if (mods.empty())
	{
		return AsciiString();
	}

	// The last -mod that has the file wins, whether it has it loose or in an archive: a later mod's
	// .big beats an earlier mod's loose copy.  An archive is a mod's when loadMods loaded it for that
	// mod, not when its name merely starts with the mod's folder: a -mod naming the install folder, or
	// a folder above another -mod's, claimed every archive under it by the text alone.
	const AsciiString archive = TheArchiveFileSystem->getArchiveFilenameForFile(filename);
	const AsciiString archiveLayer = TheArchiveFileSystem->modLayerOf(archive);
	AsciiString loose;
	Bool archived = FALSE;
	for (size_t i = mods.size(); i-- > 0 && loose.isEmpty() && !archived; )
	{
		const AsciiString &mod = mods[i];
		const Bool isFolder = mod.endsWith("\\") || mod.endsWith("/");
		if (isFolder)
		{
			AsciiString path = mod;
			path.concat(filename);
			if (TheLocalFileSystem->doesFileExist(path.str()))
			{
				loose = path;
				break;
			}
		}
		archived = archiveLayer.isNotEmpty() && archiveLayer.compareNoCase(mod) == 0;
	}

	// ...except over a file the fork ships: retail < mods < the fork.  That copy is the one the install
	// folder holds loose, and the plain lookup after this finds it.
	// The interface is ours whoever has it: a mod's loose .wnd, Language.ini or shell image is never
	// read; its archived copies were kept out of the tree in ArchiveFileSystem::addToTree.
	const Bool oursAlways = isForkUIPath(filename) || TheArchiveFileSystem->isOurUITexture(filename);
	if ((loose.isNotEmpty() || archived) && (oursAlways || (isForkOwnedPath(filename) && TheLocalFileSystem->doesFileExist(filename.str()))))
	{
		static std::set<std::string> reported;
		if (reported.insert(std::string(filename.str())).second)
		{
			DEBUG_LOG(("FileSystem - the mod's %s is ignored, our own copy wins (%s)\n",
				filename.str(), loose.isNotEmpty() ? loose.str() : archive.str()));
		}
		return AsciiString();
	}

	if (inModArchive != NULL)
	{
		*inModArchive = archived;
	}
	return loose;
}

//============================================================================
// FileSystem::isMapPath
//============================================================================
Bool FileSystem::isMapPath(const AsciiString& filename)
{
	// A mod's Maps.big carries its own edits of the stock maps under the same names (Contra's Twilight
	// Flame is not ours), and a lobby has to agree on one file: retail's and the fork's maps win always.
	return strncasecmp(filename.str(), "maps", 4) == 0 && (filename.str()[4] == '\\' || filename.str()[4] == '/');
}

//============================================================================
// FileSystem::isForkOwnedPath
//============================================================================
Bool FileSystem::isForkOwnedPath(const AsciiString& filename)
{
	// What the fork installs loose next to the exe: the launcher's payload, which is the post-build copy
	// of Code/Data (less Scenarios, Cinema and the shader cache).
	// Tests/test_gameengine.cpp walks Code/Data and fails when a master falls outside this.  Retail's own
	// loose files (Data\Scripts\MultiplayerScripts.scb, the movies, the cursors) are not in it, so a mod
	// still replaces those.  Nor is the fork's SkirmishScripts.scb, by the owner's call: a mod's skirmish
	// AI is written for its own generals, which the fork's has no players for, so a mod's scb wins whole
	// and the fork's C++ AI runs it.  The fork's Reforged*.big are kept on top in ArchiveFileSystem::loadMods.
	AsciiString path = filename;
	path.toLower();
	std::string p(path.str());
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/')
			p[i] = '\\';
	}
	const auto starts = [&p](const char *prefix) { return p.compare(0, strlen(prefix), prefix) == 0; };
	const auto ends = [&p](const char *suffix) { size_t n = strlen(suffix); return p.size() >= n && p.compare(p.size() - n, n, suffix) == 0; };

	if (starts("data\\ini\\") && p.find('\\', strlen("data\\ini\\")) == std::string::npos && ends("reforged.ini"))
		return TRUE;
	if (starts("data\\ini\\mappedimages\\handcreated\\reforged") && ends(".ini"))
		return TRUE;
	return p == "data\\patch.str"
		|| starts("data\\turkish\\") || starts("data\\german\\")
		|| starts("art\\textures\\reforged")
		|| starts("window\\")
		|| p == "install_final.bmp";
}

//============================================================================
// FileSystem::isForkUIPath
//============================================================================
Bool FileSystem::isForkUIPath(const AsciiString& filename)
{
	// A mod integrates into our game and never brings its own interface: every window layout, the
	// fonts (Language.ini, HeaderTemplate.ini), the shell's colours and transitions and the image sheets
	// the shell and the HUD frames are cut from.  The image sheets a mod's cameos live in
	// (SAUserInterface512 and the like) and the Generals Challenge portraits stay the mod's: those are
	// its units and its generals.  The textures our sheets name are added in ArchiveFileSystem::loadMods.
	AsciiString path = filename;
	path.toLower();
	std::string p(path.str());
	for (size_t i = 0; i < p.size(); ++i)
	{
		if (p[i] == '/')
			p[i] = '\\';
	}
	if (p.compare(0, strlen("window\\"), "window\\") == 0)
		return TRUE;
	const size_t slash = p.find_last_of('\\');
	const std::string dir = slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
	const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
	// Data\<language>\Language.ini and HeaderTemplate.ini
	if (dir.compare(0, strlen("data\\"), "data\\") == 0 && dir.find('\\', strlen("data\\")) == dir.size() - 1
		&& (name == "language.ini" || name == "headertemplate.ini"))
		return TRUE;
	if ((dir == "data\\ini\\" || dir == "data\\ini\\default\\")
		&& (name == "shellmenuscheme.ini" || name == "windowtransitions.ini" || name == "ingameui.ini" || name == "controlbarresizer.ini"))
		return TRUE;
	static const char *const sheets[] = { "handcreatedmappedimages.ini", "scshelluserinterface512.ini",
		"scsmshelluserinterface512.ini", "scgameuserinterface512.ini", "sclogosuserinterface512.ini",
		"scpurchasepowers512.ini", "sacontrolbar512.ini", "sncontrolbar512.ini", "sucontrolbar512.ini" };
	if (dir.compare(0, strlen("data\\ini\\mappedimages\\"), "data\\ini\\mappedimages\\") == 0)
	{
		for (size_t i = 0; i < sizeof(sheets) / sizeof(sheets[0]); ++i)
		{
			if (name == sheets[i])
				return TRUE;
		}
	}
	return FALSE;
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

	Int mod;
	AsciiString bare;
	if (splitModName(filename, &mod, &bare)) {
		AsciiString loose = modFolderFile(mod, bare);
		if (loose.isNotEmpty()) {
			return TheLocalFileSystem->getFileInfo(loose, fileInfo);
		}
		return TheArchiveFileSystem->getModFileInfo(mod, bare, fileInfo);
	}

	if (s_focus != FILE_FOCUS_ALL) {
		AsciiString loose = focusedLooseFile(filename);
		if (loose.isNotEmpty()) {
			return TheLocalFileSystem->getFileInfo(loose, fileInfo);
		}
		return TheArchiveFileSystem->getFileInfo(filename, fileInfo);
	}

	Bool inModArchive = FALSE;
	AsciiString modded = modLooseFile(filename, &inModArchive);
	if (modded.isNotEmpty()) {
		return TheLocalFileSystem->getFileInfo(modded, fileInfo);
	}
	if (inModArchive) {
		return TheArchiveFileSystem->getFileInfo(filename, fileInfo);
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
		if (TheLocalFileSystem != NULL && TheLocalFileSystem->doesFileExist(probe.str()))
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
