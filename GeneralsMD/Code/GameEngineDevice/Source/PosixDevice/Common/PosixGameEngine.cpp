/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

#include "PreRTS.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "GameNetwork/NetworkInterface.h"
#include "PosixDevice/Common/PosixGameEngine.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"

PosixGameEngine::PosixGameEngine()
{
}

PosixGameEngine::~PosixGameEngine()
{
}

void PosixGameEngine::reset( void )
{
	GameEngine::reset();
}

// Win32GameEngine::update also idles while the window is minimized and wakes the audio after; both
// are about the window, and belong with the subclass that has one.  The event pump is
// GameEngine::execute's, after the picture cap's wait.
void PosixGameEngine::update( void )
{
	GameEngine::update();
}

void PosixGameEngine::serviceWindowsOS( void )
{
}

LocalFileSystem *PosixGameEngine::createLocalFileSystem( void )
{
	return NEW PosixLocalFileSystem;
}

// Win32BIGFileSystem is the BIG reader on every platform: nothing in it is Win32 now but ntohl and one
// message box, both behind #if (C1, PR (f)).
ArchiveFileSystem *PosixGameEngine::createArchiveFileSystem( void )
{
	return NEW Win32BIGFileSystem;
}

NetworkInterface *PosixGameEngine::createNetwork( void )
{
	return NetworkInterface::createNetwork();
}

// Nothing creates the browser - GameEngine::init's call is commented out on Windows too - and every
// user of TheWebBrowser tests it for NULL.
WebBrowser *PosixGameEngine::createWebBrowser( void )
{
	return NULL;
}
