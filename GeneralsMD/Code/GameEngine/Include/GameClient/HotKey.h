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


// FILE: HotKey.h /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	HotKey.h
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __HOT_KEY_H_
#define __HOT_KEY_H_

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include <map>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "Common/SubsystemInterface.h"
#include "Common/MessageStream.h"
//-----------------------------------------------------------------------------
// FORWARD REFERENCES /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class AsciiString;
class GameWindow;
//-----------------------------------------------------------------------------
// TYPE DEFINES ///////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
class HotKeyTranslator : public GameMessageTranslator
{
public:
	virtual GameMessageDisposition translateGameMessage(const GameMessage *msg);
	virtual ~HotKeyTranslator() { }
};

//-----------------------------------------------------------------------------
class HotKey
{
public:
	HotKey( void );
	GameWindow *m_win;
	AsciiString m_key;
	Int m_claim;	///< how strongly m_win holds m_key, see keyClaim in HotKey.cpp
	// we may need a checkmark system.
};

//-----------------------------------------------------------------------------
class HotKeyManager : public SubsystemInterface
{
public:
	HotKeyManager( void );
	~HotKeyManager( void );
	// Inherited from subsystem interface -----------------------------------------------------------
	virtual	void init( void );															///< Initialize the Hotkey system
	virtual void update( void ) {}														///< A No-op for us
	virtual void reset( void );															///< Reset
	//-----------------------------------------------------------------------------------------------

	/** TRUE when win holds key afterwards, FALSE when a stronger or earlier claimant keeps it.
		* barSlot: win's command bar window slot (0-based), -1 off the bar; see playerButtonKey.
		* *displaced, when given, gets the window that held key until now, or NULL. */
	Bool addHotKey( GameWindow *win, const AsciiString& key, Int barSlot = -1, GameWindow **displaced = NULL );
	Bool executeHotKey( const AsciiString& key); // called fromt eh HotKeyTranslator
	
	AsciiString searchHotKey( const AsciiString& label);
	AsciiString searchHotKey( const UnicodeString& uStr );
	static AsciiString nameOf( WideChar c );	///< the map key for a letter: lower case, UTF-8

	/** The window this key is bound to at the moment, or NULL for a key nothing
		* claims.  When there is one, pressable comes back with the answer
		* executeHotKey would give: the window is showing and enabled, so the press
		* would land. */
	GameWindow *findHotKey( const AsciiString& key, Bool *pressable ) const;

	/** The key a command button gets: the one the player gave it in Hotkeys.ini (the launcher's
		* Hotkeys page writes it, INI::parseCommandButtonHotKeyDefinition reads it), 0 where the player
		* took the key away, and labelKey, the letter after the label's '&', where the player left the
		* button alone; upper case.  Attack move, stop, guard, rally point, evacuate and sell have fixed
		* letters in place of the label's, which the player's own letter still overrides; another button
		* gets one only from the player, and loses it to the fixed order on a bar holding both
		* (addHotKey).  Force Attack is 0: it is Ctrl, painted on the
		* cameo, never a letter.
		* On the command bar, barSlot is the button's window slot (0-based ButtonCommandNN less one) and
		* upgradeRank its place among the bar's upgrades in slot order (-1 for no upgrade).  There the
		* player's "<button>@<barSlot + 1>" comes before his "<button>", and with CommandBarGrid on the
		* grid's letter (commandBarGridKey) comes before the fixed and label letters. */
	static WideChar playerButtonKey( const AsciiString& button, WideChar labelKey, Int commandType,
		Int barSlot = -1, Int upgradeRank = -1 );
	/// playerButtonKey over `keys` (lower case button name to letter, 0 for none) in place of the file's
	static WideChar buttonKeyIn( const AsciiString& button, WideChar labelKey, Int commandType,
		const std::map< std::string, WideChar >& keys, Int barSlot = -1, Int upgradeRank = -1 );
	/** The grid's letter for command bar window slot barSlot: column barSlot / 2, row barSlot % 2,
		* Q W E R T Y U I O along the top and A S D F G H J K L along the bottom.  The k-th upgrade
		* (upgradeRank k) takes X C V B N instead, a sixth or later its slot's letter.  0 past the grid. */
	static WideChar commandBarGridKey( Int barSlot, Int upgradeRank );
	/** Each of `count` command bar slots' upgradeRank: GUI_COMMAND_PLAYER_UPGRADE and
		* GUI_COMMAND_OBJECT_UPGRADE count 0, 1, 2 in slot order, any other type is -1. */
	static void commandBarUpgradeRanks( const Int *types, Int count, Int *ranks );
	/** One keystroke does one thing (issue 128), by the key's scan code.  On a press, strokeDown
		* records whether a showing hotkey button holds its letter (barHoldsKey); while one does
		* (strokeToBar) the command map lets the key alone, press and release.  A press the command map
		* did take is marked with strokeToCommand, and strokePressesBar on the release then says FALSE,
		* so the button the press's command put on the bar is not pressed too.  strokePressesBar ends
		* the stroke. */
	static void strokeDown( Int key, Bool barHolds );
	static Bool strokeToBar( Int key );
	static void strokeToCommand( Int key );
	static Bool strokePressesBar( Int key );
	/// a showing button in the hotkey map holds the letter this key types, pressed bare or with shift
	static Bool barHoldsKey( Int key, Int modState );
	/// a command map record fires in a match on the key that types this letter (nameOf), bare
	static Bool commandMapHolds( const AsciiString& key );
	/// CommandBarGrid in Hotkeys.ini: every command bar button keyed by its window slot
	static Bool commandBarGrid( void );
	static void setCommandBarGrid( Bool grid );
	static WideChar fixedKey( const AsciiString& button, Int commandType );	///< the bar's fixed letter, or 0
	/// FixedOrderKeys in Hotkeys.ini: FALSE gives the orders their labels' letters, as shipped
	static Bool fixedOrderKeys( void );
	static void setFixedOrderKeys( Bool fixed );

	/// Hotkeys.ini out of the user data folder: the button letters afresh, its CommandMap blocks over
	/// the records the MetaMap holds.  A block that will not parse ends the file there.
	static void loadPlayerKeys( void );
	/// Every key as the engine started with them, from the language's CommandMap.ini and Hotkeys.ini
	/// again, so a file the options menu has just written counts from the next key press.
	static void reloadKeys( void );

private:
	typedef std::map<AsciiString, HotKey> HotKeyMap;
	HotKeyMap m_hotKeyMap;
};
extern HotKeyManager *TheHotKeyManager;

//-----------------------------------------------------------------------------
/** Hotkeys.ini as the launcher's Hotkeys page writes it (launcher/hotkeys.js, formatOverrides and
	* parseOverrides), so the options menu's Hotkeys page and the launcher each read what the other
	* wrote.  The player's part comes first; from HOTKEYS_LAUNCHER_PART on are the launcher's own keys,
	* which the game keeps as they stand except for a block naming a command or a button the player's
	* part now names, whose later block would otherwise beat the player's choice. */
//-----------------------------------------------------------------------------
extern const char HOTKEYS_LAUNCHER_PART[];

struct HotkeyBinding
{
	std::string key;				///< a KEY_ name, empty for no key
	std::string modifiers;	///< a ModifierNames name, NONE for none
	HotkeyBinding() : modifiers( "NONE" ) {}
	HotkeyBinding( const std::string& k, const std::string& m ) : key( k ), modifiers( m ) {}
	bool operator==( const HotkeyBinding& other ) const { return key == other.key && modifiers == other.modifiers; }
	bool operator!=( const HotkeyBinding& other ) const { return !( *this == other ); }
};

/** One command a player can bind: the shipped CommandMap.ini's own entry, or one it binds to nothing. */
struct HotkeyCommandRow
{
	std::string id;
	HotkeyBinding binding;	///< the game's own key
	std::string usableIn;		///< UseableIn as the file says it
	std::string category;
	std::string label;			///< DisplayName, empty for none
	std::string pair;				///< END_ of a BEGIN_, empty otherwise
	Bool barOnly;						///< Force Attack's hold, which the options menu's page leaves to the launcher's
	Bool hold;							///< a BEGIN_ the shipped file puts on a modifier alone, which a modifier alone may take again
	Bool launcherDefault;		///< a key the launcher gives a command the game leaves unbound
};

struct HotkeysFile
{
	std::map< std::string, std::string > buttons;				///< button name to its letter, "" for none
	std::map< std::string, HotkeyBinding > commands;		///< command to its key, key "" for none
	std::string launcherPart;	///< what followed HOTKEYS_LAUNCHER_PART, verbatim; empty when the file had none
	Bool fixedOrderKeys;			///< FixedOrderKeys' Enabled; written only when off
	Bool commandBarGrid;			///< CommandBarGrid's Enabled; written only when on
	HotkeysFile() : fixedOrderKeys( TRUE ), commandBarGrid( FALSE ) {}
};

/** The rows of the shipped Data\<language>\CommandMap.ini text, then the commands it leaves unbound
	* and the holds it puts on a modifier alone: Force Attack, waypoints and prefer selection
	* (loadCommands in hotkeys.js). */
std::vector< HotkeyCommandRow > Hotkeys_commandRows( const std::string& commandMapText );
/** The player's part of a Hotkeys.ini text, and the launcher's part as it stands (parseOverrides). */
HotkeysFile Hotkeys_parse( const std::string& text, const std::vector< HotkeyCommandRow >& rows );
/** The file for these keys, byte for byte what the launcher writes for the same player part. */
std::string Hotkeys_format( const HotkeysFile& file, const std::vector< HotkeyCommandRow >& rows );
/** The launcher's part's own blocks, by name to its Key line (CommandMap) or letter (CommandButtonHotKey). */
void Hotkeys_launcherKeys( const std::string& launcherPart, std::map< std::string, HotkeyBinding > *commands,
	std::map< std::string, std::string > *buttons );
/** Whether a KEY_ name is one the launcher reads (hotkeys.js, KEY_NAMES). */
Bool Hotkeys_isKeyName( const std::string& key );
/** English words for a binding, the launcher's chordText: "Ctrl + F", "Alt", "Num 4"; empty for none. */
std::string Hotkeys_chordText( const HotkeyBinding& binding );
/** The launcher's name order (String.localeCompare), for the blocks of one kind. */
bool Hotkeys_nameLess( const std::string& left, const std::string& right );
//-----------------------------------------------------------------------------
// INLINING ///////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// EXTERNALS //////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

#endif // __HOT_KEY_H_

