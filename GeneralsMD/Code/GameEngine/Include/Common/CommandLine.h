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

// CommandLine.h
// The command-line interface
// Author: Matthew D. Campbell, September 2001

#pragma once

#ifndef _COMMAND_LINE_H_
#define _COMMAND_LINE_H_

class AsciiString;

void parseCommandLine(int argc, char *argv[]); ///< Parse command-line parameters
void mountModFromCommandLine(int argc, char *argv[]); ///< Read -mod alone and mount its archives

/// "maps\foo.map" -> "maps\foo\foo.map".  Exposed so a test can hand it a path with no map file
/// in it; that used to run until AsciiString's length ceiling threw out of parseCommandLine.
void ConvertShortMapPathToLongMapPath(AsciiString &mapName);

#endif // _COMMAND_LINE_H_
