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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: CommandStore.cpp /////////////////////////////////////////////////////////////////////////
// Desc:   The CommandButton and CommandSet lists, moved out of ControlBar (LOGIC-THREAD.md phase 2,
//         M2) so the tick finds its buttons and sets without asking the client.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CommandStore.h"
#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "GameClient/ControlBar.h"

CommandStore *TheCommandStore = NULL;

//-------------------------------------------------------------------------------------------------
CommandStore::CommandStore( void ) :
	m_commandButtons( NULL ),
	m_commandSets( NULL )
{
}

//-------------------------------------------------------------------------------------------------
CommandStore::~CommandStore( void )
{
	while( m_commandSets )
	{
		CommandSet *set = m_commandSets->friend_getNext();
		m_commandSets->deleteInstance();
		m_commandSets = set;
	}

	while( m_commandButtons )
	{
		CommandButton *button = m_commandButtons->friend_getNext();
		m_commandButtons->deleteInstance();
		m_commandButtons = button;
	}
}

//-------------------------------------------------------------------------------------------------
/** The same files in the same order ControlBar::init read them.  The INI CRC takes them at the end
	of GameEngine::init, not here. */
//-------------------------------------------------------------------------------------------------
void CommandStore::init( void )
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\Default\\CommandButton.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\CommandButton.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\CommandSet.ini" ), INI_LOAD_OVERWRITE, NULL );

	// -modfactions: each mod's buttons and sets after retail's, under its names
	INI::loadModPasses( "Data\\INI\\CommandButton.ini", NULL );
	INI::loadModPasses( "Data\\INI\\CommandSet.ini", NULL );

	// the fork's corrections to EA's buttons and sets, edited in place
	ini.load( AsciiString( "Data\\INI\\CommandSetReforged.ini" ), INI_LOAD_MULTIFILE, NULL );

	Int buttons = 0;
	for( const CommandButton *b = m_commandButtons; b; b = b->getNext() )
		++buttons;
	Int sets = 0;
	for( CommandSet *s = m_commandSets; s; s = s->friend_getNext() )
		++sets;
	DEBUG_LOG(( "CommandStore: %d command buttons, %d command sets\n", buttons, sets ));
}

//-------------------------------------------------------------------------------------------------
/** Remove the overrides a map.ini or solo.ini added. */
//-------------------------------------------------------------------------------------------------
void CommandStore::reset( void )
{
	CommandSet *set = m_commandSets;
	while( set )
	{
		CommandSet *nextSet = set->friend_getNext();
		Bool possibleAdjustment = ( set == m_commandSets );
		Overridable *stillValid = set->deleteOverrides();
		if( stillValid == NULL && possibleAdjustment )
			m_commandSets = nextSet;
		set = nextSet;
	}

	CommandButton *button = m_commandButtons;
	while( button )
	{
		CommandButton *nextButton = button->friend_getNext();
		Bool possibleAdjustment = ( button == m_commandButtons );
		Overridable *stillValid = button->deleteOverrides();
		if( stillValid == NULL && possibleAdjustment )
			m_commandButtons = nextButton;
		button = nextButton;
	}
}

//-------------------------------------------------------------------------------------------------
/** Find a command button of the given name if present */
//-------------------------------------------------------------------------------------------------
CommandButton *CommandStore::findNonConstCommandButton( const AsciiString& name )
{
	for( const CommandButton *command = m_commandButtons; command; command = command->getNext() )
		if( command->getName() == name )
			return const_cast<CommandButton*>((const CommandButton*)command->getFinalOverride());

	AsciiString retailName;
	if( TheModNamespace.findRetailName( name, MODNAME_COMMANDBUTTON, &retailName ) )
		return findNonConstCommandButton( retailName );
	return NULL;  // not found
}

//-------------------------------------------------------------------------------------------------
/** Find existing command set by name */
//-------------------------------------------------------------------------------------------------
CommandSet* CommandStore::findNonConstCommandSet( const AsciiString& name )
{
	for( CommandSet *set = m_commandSets; set != NULL; set = set->friend_getNext() )
		if( set->getName() == name )
			return const_cast<CommandSet*>((const CommandSet *) set);

	AsciiString retailName;
	if( TheModNamespace.findRetailName( name, MODNAME_COMMANDSET, &retailName ) )
		return findNonConstCommandSet( retailName );
	return NULL;  // set not found
}

//-------------------------------------------------------------------------------------------------
const CommandButton *CommandStore::findCommandButton( const AsciiString& name )
{
	CommandButton *btn = findNonConstCommandButton( name );
	if( btn )
		btn = (CommandButton *)btn->friend_getFinalOverride();
	return btn;
}

//-------------------------------------------------------------------------------------------------
const CommandSet *CommandStore::findCommandSet( const AsciiString& name )
{
	CommandSet *set = findNonConstCommandSet( name );
	if( set )
		set = (CommandSet*)set->friend_getFinalOverride();
	return set;
}

//-------------------------------------------------------------------------------------------------
/** Allocate a new command button, assign name, and tie to list */
//-------------------------------------------------------------------------------------------------
CommandButton *CommandStore::newCommandButton( const AsciiString& name )
{
	CommandButton *newButton = newInstance(CommandButton);
	newButton->setName(name);
	newButton->friend_addToList(&m_commandButtons);
	return newButton;
}

//-------------------------------------------------------------------------------------------------
CommandButton *CommandStore::newCommandButtonOverride( CommandButton *buttonToOverride )
{
	if (!buttonToOverride) {
		return NULL;
	}

	CommandButton *newOverride = newInstance(CommandButton);
	*newOverride = *buttonToOverride;
	newOverride->markAsOverride();
	buttonToOverride->setNextOverride(newOverride);
	return newOverride;
}

//-------------------------------------------------------------------------------------------------
/** Allocate a new command set, link to list, initialize to default, and return it */
//-------------------------------------------------------------------------------------------------
CommandSet *CommandStore::newCommandSet( const AsciiString& name )
{
	CommandSet* set = newInstance(CommandSet)(name);
	set->friend_addToList(&m_commandSets);
	return set;
}

//-------------------------------------------------------------------------------------------------
/** Create an overridden command set. */
//-------------------------------------------------------------------------------------------------
CommandSet *CommandStore::newCommandSetOverride( CommandSet *setToOverride )
{
	if (!setToOverride) {
		return NULL;
	}

	/* Hang the override off the end of the chain.  findNonConstCommandSet hands back the base set (the
		 button lookup hands back the final override), so a set map.ini and solo.ini both edit, or one
		 file edits twice, copied the base, next-override pointer and all, and hung the copy off the
		 base: the chain ran base, second, first, the final override was the first edit and the second
		 never showed. */
	setToOverride = (CommandSet *)setToOverride->friend_getFinalOverride();

	CommandSet* set = newInstance(CommandSet)(setToOverride->getName());

	// it's an override; DON'T add it to the main list.
	// !!! DO NOT DO THIS !!! -- > set->friend_addToList(&m_commandSets); <-- !!! DO NOT DO THIS !!!

	*set = *setToOverride;
	set->markAsOverride();

	setToOverride->setNextOverride(set);

	return set;
}

//-------------------------------------------------------------------------------------------------
/** Parse a command set */
//-------------------------------------------------------------------------------------------------
/*static*/ void CommandStore::parseCommandSetDefinition( INI *ini )
{
	AsciiString name;
	CommandSet *commandSet;

	// read the name
	const char* c = ini->getNextToken();
	name.set( c );

	// find existing item if present
	commandSet = TheCommandStore->findNonConstCommandSet( name );
	if( commandSet == NULL )
	{

		// allocate a new item
		commandSet = TheCommandStore->newCommandSet( name );
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES) {
			commandSet->markAsOverride();
		}
	}  // end if
	else if( ini->getLoadType() == INI_LOAD_MULTIFILE )
	{
		// The fork's sets are EA's with corrections.  A mod that ships its own CommandSet.ini fills the
		// same names with its own buttons, and a set replaced whole took Contra's Demolitions General
		// his power tray and left its Air Force General two columns of promotions.  The mod's set stays.
		Bool inModArchive = FALSE;
		if( TheFileSystem->modLooseFile( AsciiString( "Data\\INI\\CommandSet.ini" ), &inModArchive ).isNotEmpty() || inModArchive )
		{
			CommandSet *unused = newInstance(CommandSet)( name );
			ini->initFromINI( unused, unused->friend_getFieldParse() );
			unused->deleteInstance();
			return;
		}

		// a patch file's set replaces the set whole, in place, so every pointer to it stays good
		commandSet->friend_clearCommands();
	}
	else if( ini->getLoadType() == INI_LOAD_OVERWRITE )
	{
		// two mods under -modfactions can both name a set "CommandSet GLA" once their Side names are
		// stripped; the later one wins, as a patch file's does, instead of refusing the whole load
		DEBUG_LOG(( "[LINE: %d in '%s'] commandset %s defined again, the later one replaces it\n",
			ini->getLineNum(), ini->getFilename().str(), name.str() ));
		commandSet->friend_clearCommands();
	}
	else if( ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES )
	{
		//Holy crap, this sucks to debug!!!
		//If you have two different command sets, the previous
		//code would simply allow you to define multiple command set
		//with the same name, and just nuke the old button with the new one.
		//So, I (KM) have added this assert to notify in case of two same-name
		//command set.
		DEBUG_CRASH(( "[LINE: %d in '%s'] Duplicate commandset %s found!", ini->getLineNum(), ini->getFilename().str(), name.str() ));
		throw INI_INVALID_DATA;

		//@todo SUPPORT OVERRIDES -- JM
	} else {
		commandSet = TheCommandStore->newCommandSetOverride(commandSet);
	}

	// sanity
	DEBUG_ASSERTCRASH( commandSet, ("parseCommandSetDefinition: Unable to allocate set '%s'\n", name.str()) );

	// parse the ini definition
	ini->initFromINI( commandSet, commandSet->friend_getFieldParse() );

}  // end parseCommandSetDefinition
