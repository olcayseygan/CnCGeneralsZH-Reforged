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

// FILE: INICommandButton.cpp /////////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Command buttons are the atomic units we can configure into command sets to then
//				 display in the context sensitive user interface
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "Common/SpecialPower.h"
#include "Common/CommandStore.h"
#include "GameClient/ControlBar.h"

//-------------------------------------------------------------------------------------------------
/** Parse a command button */
//-------------------------------------------------------------------------------------------------
void INI::parseCommandButtonDefinition( INI *ini )
{
	CommandStore::parseCommandButtonDefinition(ini);
}

//-------------------------------------------------------------------------------------------------
/** Parse a command button */
//-------------------------------------------------------------------------------------------------
void CommandStore::parseCommandButtonDefinition( INI *ini )
{
	// read the name
	AsciiString name = ini->getNextToken();

	// find existing item if present
	CommandButton *button = TheCommandStore->findNonConstCommandButton( name );
	if( button == NULL )
	{
		// allocate a new item
		button = TheCommandStore->newCommandButton( name );
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) 
		{
			button->markAsOverride();
		}
	}  // end if
	else if( ini->getLoadType() == INI_LOAD_MULTIFILE )
	{
		// a patch file's button edits EA's in place and names only what changes
	}
	else if( ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES )
	{
		DEBUG_CRASH(( "[LINE: %d in '%s'] Duplicate commandbutton %s found!", ini->getLineNum(), ini->getFilename().str(), name.str() ));
	}
	else
	{
		button = TheCommandStore->newCommandButtonOverride( button );
	}

	// parse the ini definition
	ini->initFromINI( button, button->getFieldParse() );

	// ControlBar::postProcessCommands looks the images up once, at startup, and a map.ini is read at
	// map load, long after: a button it adds or re-images drew empty. Look that one up now.
	if( ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES )
		button->cacheButtonImage();
	

	//Make sure buttons with special power templates also have the appropriate option set.
	const SpecialPowerTemplate *spTemplate = button->getSpecialPowerTemplate();
	Bool needsTemplate = BitTest( button->getOptions(), NEED_SPECIAL_POWER_SCIENCE );
	if( spTemplate && !needsTemplate )
	{
		DEBUG_CRASH( ("[LINE: %d in '%s'] CommandButton %s has SpecialPower = %s but the button also requires Options = NEED_SPECIAL_POWER_SCIENCE. Failure to do so will cause bugs such as invisible side shortcut buttons",
			ini->getLineNum(), ini->getFilename().str(), name.str(), spTemplate->getName().str() ) );
	}
	else if( !spTemplate && needsTemplate )
	{
		DEBUG_CRASH( ("[LINE: %d in '%s'] CommandButton %s has Options = NEED_SPECIAL_POWER_SCIENCE but doesn't specify a SpecialPower = xxxx. Please evaluate INI.",
			ini->getLineNum(), ini->getFilename().str(), name.str() ) );
	}

}  // end parseCommandButtonDefinition


