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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------=
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					                  
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:    GameEngine
//
// Module:     IO
//
// File name:  FileSystem.h
//
// Created:    
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __FILESYSTEM_H
#define __FILESYSTEM_H

//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

//#include "Common/File.h"
#include "Common/STLTypedefs.h"
#include "Common/SubsystemInterface.h"

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------
class File;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

typedef std::set<AsciiString, rts::less_than_nocase<AsciiString> > FilenameList;
typedef FilenameList::iterator FilenameListIter;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------
//#define W3D_DIR_PATH "../FinalArt/W3D/"					///< .w3d files live here
//#define TGA_DIR_PATH "../FinalArt/Textures/"		///< .tga texture files live here
//#define TERRAIN_TGA_DIR_PATH "../FinalArt/Terrain/"		///< terrain .tga texture files live here
#define W3D_DIR_PATH "Art/W3D/"					///< .w3d files live here
#define TGA_DIR_PATH "Art/Textures/"		///< .tga texture files live here
#define TERRAIN_TGA_DIR_PATH "Art/Terrain/"		///< terrain .tga texture files live here
#define TERRAIN_HD_TGA_DIR_PATH "Art/TerrainHD/"	///< the same files at twice the size, loose, if there are any
#define MAP_PREVIEW_DIR_PATH "%sMapPreviews/"	///< We need a common place we can copy the map previews to at runtime.
#define USER_W3D_DIR_PATH "%sW3D/"					///< .w3d files live here
#define USER_TGA_DIR_PATH "%sTextures/"		///< User .tga texture files live here

// the following defines are only to be used while maintaining legacy compatability
// with old files until they are completely gone and in the regular art set
#ifdef MAINTAIN_LEGACY_FILES
#define LEGACY_W3D_DIR_PATH "../LegacyArt/W3D/"				///< .w3d files live here
#define LEGACY_TGA_DIR_PATH "../LegacyArt/Textures/"	///< .tga texture files live here
#endif  // MAINTAIN_LEGACY_FILES

// LOAD_TEST_ASSETS automatically loads w3d assets from the TEST_W3D_DIR_PATH
// without having to add an INI entry.
///@todo this allows us to use the test art directory, it should be removed for FINAL release
#define LOAD_TEST_ASSETS 1
#ifdef LOAD_TEST_ASSETS
	#define ROAD_DIRECTORY		"../TestArt/TestRoad/"
	#define TEST_STRING				"***TESTING"
// the following directories will be used to look for test art
#define LOOK_FOR_TEST_ART
#define TEST_W3D_DIR_PATH "../TestArt/"					///< .w3d files live here
#define TEST_TGA_DIR_PATH "../TestArt/"		///< .tga texture files live here
#endif

enum { FILE_FOCUS_ALL = -2, FILE_FOCUS_RETAIL = -1 };	///< FileSystem::setFocus; 0 and up is a -mod index

struct FileInfo {
	Int sizeHigh;
	Int sizeLow;
	Int timestampHigh;
	Int timestampLow;
};

//===============================
// FileSystem
//===============================
/**
  * FileSystem is an interface class for creating specific FileSystem objects.
  * 
	* A FileSystem object's implemenation decides what derivative of File object needs to be 
	* created when FileSystem::Open() gets called.
	*/
//===============================
#include <map>

class FileSystem : public SubsystemInterface
{
  FileSystem(const FileSystem&);
  FileSystem& operator=(const FileSystem&);
  
public:
	FileSystem();
	virtual	~FileSystem();

	void init();
	void reset();
	void update();

	File* openFile( const Char *filename, Int access = 0 );		///< opens a File interface to the specified file
	Bool doesFileExist(const Char *filename) const;								///< returns TRUE if the file exists.  filename should have no directory.
	void getFileListInDirectory(const AsciiString& directory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const; ///< search the given directory for files matching the searchName (egs. *.ini, *.rep).  Possibly search subdirectories.
	Bool getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const; ///< fills in the FileInfo struct for the file given. returns TRUE if successful.

	/// "<mod dir><filename>" when the last -mod that has this game path has it loose, else empty.  When
	/// that mod has it in an archive instead, *inModArchive says so.  Either copy beats Run's loose files
	/// and every archive that is not a mod's - except a fork-owned file the install folder holds loose,
	/// which no mod replaces.
	AsciiString modLooseFile(const AsciiString& filename, Bool *inModArchive = NULL) const;

	/// TRUE for a game path the fork ships loose and no mod replaces (BalanceReforged.ini, Patch.str, its
	/// .wnd layouts ...); see the definition for how the set is decided.
	static Bool isForkOwnedPath(const AsciiString& filename);

	/// TRUE for a game path that draws the game's interface (a .wnd, Language.ini's fonts, the shell's
	/// image sheets): a merged mod never replaces it, the copy retail or the fork has wins
	static Bool isForkUIPath(const AsciiString& filename);

	/// TRUE for a game path under "Maps\": the maps are always ours, a mod's own maps are never listed or
	/// loaded, so every machine in a match reads the same map whatever mods it runs
	static Bool isMapPath(const AsciiString& filename);

	/// -modfactions: FILE_FOCUS_RETAIL or a -mod index makes every read see that set of files alone,
	/// so retail and each mod load their factions one after another; FILE_FOCUS_ALL is the merged view.
	void setFocus(Int focus);
	Int getFocus() const { return s_focus; }

	/// -modfactions: a file name starting "M<n>~" ("Art\W3D\M2~AVRanger.w3d") is mod n's copy and nothing
	/// else; the art code falls back to the plain name itself.  TRUE for such a name, with *mod the -mod
	/// index and *bare the path without the prefix.
	static Bool splitModName(const AsciiString& filename, Int *mod, AsciiString *bare);

	Bool createDirectory(AsciiString directory); ///< create a directory of the given name.

	/// language when Data\<language>\Language.ini is loose or in an archive, otherwise the first
	/// retail language that has one (english first), otherwise empty.
	/// Only meaningful once the archives are mounted.
	AsciiString installedLanguage(const AsciiString &language) const;
	/// the retail language whose <language>ZH.big sits loose in the install folder, otherwise empty.
	AsciiString zeroHourLanguage() const;

	Bool areMusicFilesOnCD();
	void loadMusicFilesFromCD();
	void unloadMusicFilesFromCD();
protected:
	AsciiString focusedLooseFile(const AsciiString& filename) const;	///< the loose copy the focus sees, else empty
	AsciiString modFolderFile(Int mod, const AsciiString& filename) const;	///< "<mod folder><filename>" when it is there, else empty
  mutable std::map<unsigned,bool> m_fileExist;
	// static: GlobalData's constructor reads files through a TheFileSystem that a few engine tests leave
	// NULL, and openFile has always answered NULL there without touching the object
	static Int s_focus;
};

extern FileSystem*	TheFileSystem;



//----------------------------------------------------------------------------
//           Inlining                                                       
//----------------------------------------------------------------------------



#endif // __WSYS_FILESYSTEM_H
