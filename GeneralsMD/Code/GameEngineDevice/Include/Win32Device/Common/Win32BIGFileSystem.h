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

//////// Win32BIGFileSystem.h ///////////////////////////
// Bryan Cleveland, August 2002
/////////////////////////////////////////////////////////////

#pragma once

#ifndef __WIN32BIGFILESYSTEM_H
#define __WIN32BIGFILESYSTEM_H

#include "Common/ArchiveFileSystem.h"

#include <vector>

class Win32BIGFileSystem : public ArchiveFileSystem
{
public:
	Win32BIGFileSystem();
	virtual ~Win32BIGFileSystem();

	virtual void init( void );
	virtual void update( void );
	virtual void reset( void );
	virtual void postProcessLoad( void );

	// ArchiveFile operations
	virtual void closeAllArchiveFiles( void );											///< Close all Archivefiles currently open

	// File operations
	virtual ArchiveFile * openArchiveFile(const Char *filename);
	virtual void closeArchiveFile(const Char *filename);
	virtual void closeAllFiles( void );															///< Close all files associated with ArchiveFiles

	virtual Bool loadBigFilesFromDirectory(AsciiString dir, AsciiString fileMask, Bool overwrite = FALSE, Bool searchSubdirectories = TRUE);
protected:
	std::vector<AsciiString> m_reforgedArt;		///< the Reforged*.big beside the exe this run mounts, held for the end of init

};

#endif // __WIN32BIGFILESYSTEM_H