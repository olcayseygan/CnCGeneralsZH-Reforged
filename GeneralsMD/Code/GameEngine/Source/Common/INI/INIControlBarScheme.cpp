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

// FILE: INIControlBarScheme.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Apr 2002
//
//	Filename: 	INIControlBarScheme.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Parse a control Bar Scheme
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarScheme.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** Parse a ControlBarScheme button */
//-------------------------------------------------------------------------------------------------
void INI::parseControlBarSchemeDefinition( INI *ini )
{
	AsciiString name;
	ControlBarSchemeManager *CBSchemeManager;
	ControlBarScheme *CBScheme;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );	

	// find existing item if present
	CBSchemeManager = TheControlBar->getControlBarSchemeManager();
	DEBUG_ASSERTCRASH( CBSchemeManager, ("parseControlBarSchemeDefinition: Unable to Get CBSchemeManager\n") );
	if( !CBSchemeManager )
		return;

	// A multifile load edits the scheme already there; anything else gets a cleared out scheme, a
	// previously allocated one included, to overwrite
	CBScheme = ini->getLoadType() == INI_LOAD_MULTIFILE ? CBSchemeManager->findControlBarScheme( name ) : NULL;
	if( CBScheme == NULL && ini->getLoadType() == INI_LOAD_MULTIFILE )
	{
		// An edit of a scheme no file defines has nothing to apply to, and a new scheme
		// holding only the edited fields has no ScreenCreationRes.  The block goes into a scheme that
		// is thrown away, so the parse reaches its End.
		DEBUG_LOG(( "[LINE: %d in '%s'] %s patches a control bar scheme that does not exist; the block is skipped.\n", ini->getLineNum(), ini->getFilename().str(), name.str() ));
		ControlBarScheme scratch;
		ini->initFromINI( &scratch, CBSchemeManager->getFieldParse() );
		return;
	}
	if( CBScheme == NULL )
		CBScheme = CBSchemeManager->newControlBarScheme( name );

	// sanity
	DEBUG_ASSERTCRASH( CBScheme, ("parseControlBarSchemeDefinition: Unable to allocate Scheme '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( CBScheme, CBSchemeManager->getFieldParse() );
	CBScheme->validate();

}  // end parseCommandButtonDefinition


