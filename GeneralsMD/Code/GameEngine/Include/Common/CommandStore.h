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

// FILE: CommandStore.h ///////////////////////////////////////////////////////////////////////////
// Desc:   Every CommandButton and CommandSet definition, owned by logic (LOGIC-THREAD.md phase 2,
//         M2).  The AI, the scripts and the build checks look their buttons and sets up here; the
//         control bar reads the same lists to draw them.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __COMMANDSTORE_H__
#define __COMMANDSTORE_H__

#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

class CommandButton;
class CommandSet;
class INI;

/**
	* What logic reads from a button or a set (its command, object, upgrade, special power, options,
	* slots) does not change during a match.  The fields that do, the cached button image and the
	* cameo flash count, belong to the client and only the control bar touches them.  A map.ini or
	* solo.ini adds overrides at map load; reset() removes them between games.
	*/
class CommandStore : public SubsystemInterface
{

public:

	CommandStore( void );
	virtual ~CommandStore( void );

	void init( void );					///< load CommandButton.ini, CommandSet.ini and CommandSetReforged.ini
	void reset( void );					///< remove the map's overrides
	void update( void ) { }

	const CommandButton *findCommandButton( const AsciiString& name );	///< the button's final override
	const CommandSet *findCommandSet( const AsciiString& name );				///< the set's final override

	CommandButton *findNonConstCommandButton( const AsciiString& name );
	CommandSet *findNonConstCommandSet( const AsciiString& name );

	const CommandButton *getCommandButtons( void ) { return m_commandButtons; }
	CommandButton *friend_getCommandButtons( void ) { return m_commandButtons; }	///< for ControlBar::postProcessCommands' image cache
	CommandSet *friend_getCommandSets( void ) { return m_commandSets; }				///< for ControlBar::initWindows' count of slots

	static void parseCommandButtonDefinition( INI *ini );
	static void parseCommandSetDefinition( INI *ini );

private:

	CommandButton *newCommandButton( const AsciiString& name );
	CommandButton *newCommandButtonOverride( CommandButton *buttonToOverride );
	CommandSet *newCommandSet( const AsciiString& name );
	CommandSet *newCommandSetOverride( CommandSet *setToOverride );

	CommandButton *m_commandButtons;		///< every button defined
	CommandSet *m_commandSets;					///< every set defined

};

extern CommandStore *TheCommandStore;

#endif // __COMMANDSTORE_H__
