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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   Generals
//
// Module:    Game Engine Common
//
// File name: ArchiveFileSystem.cpp
//
// Created:   11/26/01 TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/file.h"
#include "Common/PerfTimer.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

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

ArchiveFileSystem *TheArchiveFileSystem = NULL;


//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------

//------------------------------------------------------
// ArchivedFileInfo
//------------------------------------------------------
ArchiveFileSystem::ArchiveFileSystem()
{
}

ArchiveFileSystem::~ArchiveFileSystem() 
{
	ArchiveFileMap::iterator iter = m_archiveFileMap.begin();
	while (iter != m_archiveFileMap.end()) {
		ArchiveFile *file = iter->second;
		if (file != NULL) {
			delete file;
			file = NULL;
		}
		iter++;
	}
}

static const char *archiveBaseName(const AsciiString &path)
{
	const char *name = path.str();
	for (const char *c = name; *c; ++c) {
		if (*c == '\\' || *c == '/') {
			name = c + 1;
		}
	}
	return name;
}

static Int reforgedMountRank(const AsciiString &path)
{
	static const char *const baseArt[] = { "ReforgedTextures.big", "ReforgedNormals.big", "ReforgedTerrain.big" };
	const char *name = archiveBaseName(path);
	for (Int i = 0; i < (Int)(sizeof(baseArt) / sizeof(baseArt[0])); ++i) {
		if (strcasecmp(name, baseArt[i]) == 0) {
			return i;
		}
	}
	return (Int)(sizeof(baseArt) / sizeof(baseArt[0]));
}

Bool ArchiveFileSystem::reforgedMountsBefore(const AsciiString& a, const AsciiString& b)
{
	const Int ra = reforgedMountRank(a), rb = reforgedMountRank(b);
	if (ra != rb) {
		return ra < rb;
	}
	const Int byName = strcasecmp(archiveBaseName(a), archiveBaseName(b));
	if (byName != 0) {
		return byName < 0;
	}
	return strcmp(a.str(), b.str()) < 0;	// the same name in two folders: still one fixed order
}

Bool ArchiveFileSystem::isReforgedBaseArt(const AsciiString& path)
{
	return reforgedMountRank(path) < reforgedMountRank(AsciiString::TheEmptyString);
}

Bool ArchiveFileSystem::reforgedArchiveWanted(const AsciiString& path, Bool classicGraphics, Bool hdTextures, Bool detailedModels)
{
	// the online skins' sheets: no model and no retail name, a Classic and a Reforged copy of each, so
	// every look wants them and a sheet the models do not ask for is never read.  The same for the
	// fracture sets cut from retail models and from the rebuilt ones (W3DFractureDraw picks the set of
	// the model it draws), which cannot ship in the public source
	const char *name = archiveBaseName(path);
	if (strcasecmp(name, "ReforgedSkins.big") == 0 || strcasecmp(name, "ReforgedFracture.big") == 0) {
		return TRUE;
	}
	return !classicGraphics && (isReforgedBaseArt(path) ? hdTextures : detailedModels);
}

Bool ArchiveFileSystem::isForkArchive(const AsciiString& path)
{
	static const char *const named[] = { "ReforgedSkins.big", "ReforgedFracture.big", "ReforgedUSA.big", "ReforgedChina.big",
		"ReforgedGLA.big", "ReforgedTurkiye.big", "ReforgedTextures.big", "ReforgedTerrain.big" };
	const char *name = archiveBaseName(path);
	for (Int i = 0; i < (Int)(sizeof(named) / sizeof(named[0])); ++i) {
		if (strcasecmp(name, named[i]) == 0) {
			return TRUE;
		}
	}
	// the map batches, ReforgedNeutralA.big on
	return strlen(name) == 20 && strncasecmp(name, "ReforgedNeutral", 15) == 0 && isalpha((unsigned char)name[15])
		&& strcasecmp(name + 16, ".big") == 0;
}

void ArchiveFileSystem::mountOverRetail(ArchiveFile *archiveFile, const AsciiString& archiveFilename)
{
	FilenameList models;
	archiveFile->getFileListInDirectory(AsciiString(""), AsciiString(""), AsciiString("*.w3d"), models, TRUE);
	for (FilenameListIter it = models.begin(); it != models.end(); ++it) {
		AsciiString model = *it;
		model.toLower();
		if (m_retailModels.find(model) == m_retailModels.end()) {
			m_retailModels[model] = getArchiveFilenameForFile(model);		// the first Reforged holder's view is retail's
		}
	}
	loadIntoDirectoryTree(archiveFile, archiveFilename, TRUE);
	m_archiveFileMap[archiveFilename] = archiveFile;
}

AsciiString ArchiveFileSystem::getRetailArchiveFilenameForFile(const AsciiString& filename) const
{
	AsciiString model = filename;
	model.toLower();
	ArchivedFileLocationMap::const_iterator it = m_retailModels.find(model);
	return it != m_retailModels.end() ? it->second : getArchiveFilenameForFile(filename);
}

File * ArchiveFileSystem::openRetailFile(const AsciiString& filename)
{
	const AsciiString archiveFilename = getRetailArchiveFilenameForFile(filename);
	ArchiveFileMap::iterator it = m_archiveFileMap.find(archiveFilename);
	return it != m_archiveFileMap.end() ? it->second->openFile(filename.str(), File::READ) : NULL;
}

/// The name of a .tga or .dds asked for in a folder named Textures, lower case and without its
/// extension; empty for anything else.  The W3D loader tries a .dds before the .tga a model names,
/// so the extension cannot tell one texture from another.
static std::string textureStem(const char *filename)
{
	const char *leaf = filename, *parent = NULL;
	for (const char *c = filename; *c; ++c) {
		if (*c == '\\' || *c == '/') {
			parent = leaf;
			leaf = c + 1;
		}
	}
	const char *dot = strrchr(leaf, '.');
	if (parent == NULL || dot == NULL || leaf - parent != 9 || strncasecmp(parent, "textures", 8) != 0
		|| (strcasecmp(dot, ".tga") != 0 && strcasecmp(dot, ".dds") != 0)) {
		return std::string();
	}
	std::string stem(leaf, dot);
	for (size_t i = 0; i < stem.size(); ++i) {
		stem[i] = (char)tolower((unsigned char)stem[i]);
	}
	return stem;
}

Bool ArchiveFileSystem::isRetailTexture(const Char *filename) const
{
	if (m_retailTextures.empty()) {
		return FALSE;
	}
	const std::string stem = textureStem(filename);
	return !stem.empty() && m_retailTextures.count(stem) != 0;
}

void ArchiveFileSystem::keepRetailTextures()
{
	static const char ART_TEXTURES[] = "art\\textures\\";
	static const char *const holders[] = { "Textures.big", "TexturesZH.big", "Patch.big", "PatchZH.big" };

	// What EA ships, path by path: Art\Textures from its texture and patch archives, Data\<language>\
	// Art\Textures from <language>.big and <language>ZH.big for a language EA shipped (so W3DEnglishZH.big
	// is no language archive).  A Zero Hour archive counts beside the exe, a base game one in the base
	// game's folder, or beside the exe when the base game was found there (m_baseGameDirectory empty): a
	// stray English.big beside a Steam install's exe is nobody's.  Where two hold a path, a Zero Hour
	// archive over a base game one and then the first by name, the order the plain mount gives them.
	// ponytail: archive names and folders, not contents, say what is EA's; an edited TexturesZH.big, or a
	// stray in a folder that holds the base game beside the exe, passes as retail.
	std::map<std::string, AsciiString> shipped;
	for (Int zeroHour = 1; zeroHour >= 0; --zeroHour) {
		for (ArchiveFileMap::const_iterator it = m_archiveFileMap.begin(); it != m_archiveFileMap.end(); ++it) {
			const char *name = archiveBaseName(it->first);
			const size_t length = strlen(name);
			const Bool zh = length >= 6 && strcasecmp(name + length - 6, "ZH.big") == 0;
			if (zh != (zeroHour != 0) || length < 4 || strcasecmp(name + length - 4, ".big") != 0) {
				continue;
			}
			const Bool besideExe = name == it->first.str();
			const Bool inBaseGame = !m_baseGameDirectory.isEmpty()
				&& strncasecmp(it->first.str(), m_baseGameDirectory.str(), m_baseGameDirectory.getLength()) == 0;
			if ((zh || m_baseGameDirectory.isEmpty()) ? !besideExe : !inBaseGame) {
				continue;
			}
			AsciiString directory = ART_TEXTURES;
			Bool holder = FALSE;
			for (Int i = 0; i < (Int)(sizeof(holders) / sizeof(holders[0])); ++i) {
				holder = holder || strcasecmp(name, holders[i]) == 0;
			}
			if (!holder) {
				const std::string language(name, length - (zh ? 6 : 4));
				if (!FileSystem::isRetailLanguage(language.c_str())) {
					continue;
				}
				directory.format("data\\%s\\%s", language.c_str(), ART_TEXTURES);
			}
			FilenameList files;
			it->second->getFileListInDirectory(AsciiString(""), directory, AsciiString("*"), files, FALSE);
			for (FilenameListIter f = files.begin(); f != files.end(); ++f) {
				AsciiString path = *f;
				path.toLower();
				shipped.insert(std::make_pair(std::string(path.str()), it->first));
			}
		}
	}
	m_retailTextures.clear();
	for (std::map<std::string, AsciiString>::const_iterator it = shipped.begin(); it != shipped.end(); ++it) {
		m_retailTextures.insert(textureStem(it->first.c_str()));
	}
	m_retailTextures.erase(std::string());

	// Every texture folder in the tree: Art\Textures and each Data\<x>\Art\Textures.
	std::vector< std::pair<std::string, ArchivedDirectoryInfo *> > folders;
	ArchivedDirectoryInfoMap::iterator art = m_rootDirectory.m_directories.find("art");
	if (art != m_rootDirectory.m_directories.end()) {
		ArchivedDirectoryInfoMap::iterator textures = art->second.m_directories.find("textures");
		if (textures != art->second.m_directories.end()) {
			folders.push_back(std::make_pair(std::string(ART_TEXTURES), &textures->second));
		}
	}
	ArchivedDirectoryInfoMap::iterator data = m_rootDirectory.m_directories.find("data");
	if (data != m_rootDirectory.m_directories.end()) {
		for (ArchivedDirectoryInfoMap::iterator x = data->second.m_directories.begin(); x != data->second.m_directories.end(); ++x) {
			ArchivedDirectoryInfoMap::iterator xart = x->second.m_directories.find("art");
			if (xart == x->second.m_directories.end()) {
				continue;
			}
			ArchivedDirectoryInfoMap::iterator textures = xart->second.m_directories.find("textures");
			if (textures != xart->second.m_directories.end()) {
				folders.push_back(std::make_pair("data\\" + std::string(x->first.str()) + "\\" + ART_TEXTURES, &textures->second));
			}
		}
	}

	for (size_t i = 0; i < folders.size(); ++i) {
		ArchivedFileLocationMap &files = folders[i].second->m_files;
		for (ArchivedFileLocationMap::iterator it = files.begin(); it != files.end(); ) {
			const std::string path = folders[i].first + it->first.str();
			std::map<std::string, AsciiString>::const_iterator own = shipped.find(path);
			if (own != shipped.end()) {
				if (own->second != it->second) {
					DEBUG_LOG(("ArchiveFileSystem::keepRetailTextures - %s from %s, not %s\n", path.c_str(), own->second.str(), it->second.str()));
					it->second = own->second;
				}
				++it;
			} else if (m_retailTextures.count(textureStem(path.c_str())) != 0) {
				DEBUG_LOG(("ArchiveFileSystem::keepRetailTextures - %s in %s dropped, a retail texture's name\n", path.c_str(), it->second.str()));
				files.erase(it++);
			} else {
				++it;
			}
		}
	}
	DEBUG_LOG(("ArchiveFileSystem::keepRetailTextures - %d retail texture names\n", (Int)m_retailTextures.size()));
}

void ArchiveFileSystem::loadIntoDirectoryTree(const ArchiveFile *archiveFile, const AsciiString& archiveFilename, Bool overwrite)
{

	FilenameList filenameList;

	archiveFile->getFileListInDirectory(AsciiString(""), AsciiString(""), AsciiString("*"), filenameList, TRUE);

	FilenameListIter it = filenameList.begin();

	while (it != filenameList.end()) {
		// add this filename to the directory tree.
		AsciiString path = *it;
		path.toLower();
		AsciiString token;
		AsciiString debugpath;

		ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

		Bool infoInPath;
		infoInPath = path.nextToken(&token, "\\/");

		while (infoInPath && (!token.find('.') || path.find('.'))) {
			ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
			if (tempiter == dirInfo->m_directories.end()) 
			{
				dirInfo->m_directories[token].clear();
				dirInfo->m_directories[token].m_directoryName = token;
			}

			dirInfo = &(dirInfo->m_directories[token]);
			debugpath.concat(token);
			debugpath.concat('\\');
			infoInPath = path.nextToken(&token, "\\/");
		}

		// token is the filename, and dirInfo is the directory that this file is in.
		if (dirInfo->m_files.find(token) == dirInfo->m_files.end() || overwrite) {
			AsciiString path2;
			path2 = debugpath;
			path2.concat(token);
//			DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s\n", path2.str(), archiveFilename.str()));
			dirInfo->m_files[token] = archiveFilename;
		}

		it++;
	}
}

Bool ArchiveFileSystem::doesFileExist(const Char *filename) const
{
	AsciiString path = filename;
	path.toLower();
	AsciiString token;

	const ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	path.nextToken(&token, "\\/");

	while (!token.find('.') || path.find('.'))
	{
		ArchivedDirectoryInfoMap::const_iterator tempiter = dirInfo->m_directories.find(token);
		if (tempiter != dirInfo->m_directories.end()) 
		{
			dirInfo = &tempiter->second;
			path.nextToken(&token, "\\/");
		}
		else
		{
			// the directory doesn't exist, so return false
			return FALSE;
		}
	}

	// token is the filename, and dirInfo is the directory that this file is in.
	if (dirInfo->m_files.find(token) == dirInfo->m_files.end()) {
		return FALSE;
	}
	return TRUE;
}

File * ArchiveFileSystem::openFile(const Char *filename, Int access /* = 0 */)
{
	AsciiString archiveFilename;
	archiveFilename = getArchiveFilenameForFile(AsciiString(filename));

	if (archiveFilename.getLength() == 0) {
		return NULL;
	}

	return m_archiveFileMap[archiveFilename]->openFile(filename, access);
}

Bool ArchiveFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	if (fileInfo == NULL) {
		return FALSE;
	}

	if (filename.getLength() <= 0) {
		return FALSE;
	}

	AsciiString archiveFilename = getArchiveFilenameForFile(filename);
	ArchiveFileMap::const_iterator it = m_archiveFileMap.find(archiveFilename);
	if (it != m_archiveFileMap.end())
	{
		return it->second->getFileInfo(filename, fileInfo);
	}
	else
	{
		return FALSE;
	}
}

// Where two archives hold the same file, loadIntoDirectoryTree keeps whichever was loaded first.
// That is the right rule for content, but Zero Hour ships downscaled copies of a number of the base
// game's textures in TexturesZH.big, and those win over the full-size originals in the base game's
// Textures.big purely because of load order.  For the named pair of archives, and only inside the
// named directory, keep whichever copy is actually the larger file.
void ArchiveFileSystem::prioritizeLargerFiles(const AsciiString& directory, const char *inferiorArchive, const char *superiorArchive)
{
	// walk down to the directory
	AsciiString path = directory;
	path.toLower();
	AsciiString token;

	ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;
	while (path.nextToken(&token, "\\/"))
	{
		ArchivedDirectoryInfoMap::iterator dirIt = dirInfo->m_directories.find(token);
		if (dirIt == dirInfo->m_directories.end())
			return;

		dirInfo = &dirIt->second;
	}

	// find the archive that holds the bigger copies
	ArchiveFile *superior = NULL;
	AsciiString superiorName;
	for (ArchiveFileMap::iterator archiveIt = m_archiveFileMap.begin(); archiveIt != m_archiveFileMap.end(); ++archiveIt)
	{
		if (archiveIt->first.endsWithNoCase(superiorArchive))
		{
			superior = archiveIt->second;
			superiorName = archiveIt->first;
			break;
		}
	}

	if (superior == NULL)
		return;

	for (ArchivedFileLocationMap::iterator fileIt = dirInfo->m_files.begin(); fileIt != dirInfo->m_files.end(); ++fileIt)
	{
		if (!fileIt->second.endsWithNoCase(inferiorArchive))
			continue;

		ArchiveFileMap::iterator inferiorIt = m_archiveFileMap.find(fileIt->second);
		if (inferiorIt == m_archiveFileMap.end())
			continue;

		AsciiString filePath = directory;
		filePath.concat(fileIt->first);

		FileInfo inferiorInfo, superiorInfo;
		if (!inferiorIt->second->getFileInfo(filePath, &inferiorInfo))
			continue;
		if (!superior->getFileInfo(filePath, &superiorInfo))
			continue;

		if (superiorInfo.sizeLow > inferiorInfo.sizeLow)
		{
			DEBUG_LOG(("ArchiveFileSystem::prioritizeLargerFiles - %s (%d bytes) from %s wins over the %d byte copy in %s\n",
								 filePath.str(), superiorInfo.sizeLow, superiorName.str(), inferiorInfo.sizeLow, fileIt->second.str()));
			fileIt->second = superiorName;
		}
	}
}

AsciiString ArchiveFileSystem::getArchiveFilenameForFile(const AsciiString& filename) const
{
	AsciiString path;
	path = filename;
	path.toLower();
	AsciiString token;
	AsciiString debugpath;

	const ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	path.nextToken(&token, "\\/");

	while (!token.find('.') || path.find('.')) {

		ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it != dirInfo->m_directories.end())
		{
			dirInfo = &it->second;
		}
		else
		{
			// the directory doesn't exist, so return NULL

			// dump the directories;
			//DEBUG_LOG(("directory %s not found in %s in archive file system\n", token.str(), debugpath.str()));
			//DEBUG_LOG(("directories in %s in archive file system are:\n", debugpath.str()));
			//ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.begin();
			//while (it != dirInfo->m_directories.end()) {
			//	DEBUG_LOG(("\t%s\n", it->second.m_directoryName.str()));
			//	it++;
			//}
			//DEBUG_LOG(("end of directory list.\n"));
			return AsciiString::TheEmptyString;
		}

		debugpath.concat(token);
		debugpath.concat('\\');

		path.nextToken(&token, "\\/");
	}

	ArchivedFileLocationMap::const_iterator it = dirInfo->m_files.find(token);
	if (it != dirInfo->m_files.end())
	{
		return it->second;
	}
	else
	{
		return AsciiString::TheEmptyString;
	}

}

void ArchiveFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	ArchiveFileMap::const_iterator it = m_archiveFileMap.begin();
	while (it != m_archiveFileMap.end()) {
		it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
		it++;
	}
}
