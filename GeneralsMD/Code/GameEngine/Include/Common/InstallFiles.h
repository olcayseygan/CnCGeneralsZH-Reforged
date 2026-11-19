/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Olcay Seygan
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

// FILE: InstallFiles.h ///////////////////////////////////////////////////////////////////////////
// Desc:   The sha256 of every file of the install the game reads, for online login to send: a
//         reskin or any edited file is refused at the door instead of played unseen.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>

struct InstallFile
{
	std::string path;			///< relative to the game folder, lower case, '/' (zh_generals/textures.big, art/textures/foo.tga)
	std::string sha256;		///< lower case hex
};

/// GameEngine::init, archives mounted: lists every mounted .big and every loose file under Art, Data and
/// Window beside the exe, then hashes them on a thread of their own.  Once; later calls do nothing.
void InstallFiles_start();
/// TRUE once that thread has finished, and when none was started (a headless run, the tests).
bool InstallFiles_ready();
/// What it hashed, sorted by path; empty before ready.
const std::vector<InstallFile> &InstallFiles_list();
/// Tests: this list in place of a hashed one, ready at once.
void InstallFiles_setList( const std::vector<InstallFile> &list );
