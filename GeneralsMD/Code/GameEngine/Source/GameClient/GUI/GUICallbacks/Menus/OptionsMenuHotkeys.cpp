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

// OptionsMenuHotkeys.cpp
//
// The options menu's Hotkeys page: what the launcher's Hotkeys page does, inside the game.  Every
// command on the left with its chord, and on the right the command bar of one building or unit,
// picked by general and object, with each button's letter.  A double click, Enter or Change waits for
// the key; Esc lets go.  Nothing is written until Accept, which writes Hotkeys.ini in the user data
// folder in the launcher's own format (Hotkeys_format in HotKey.cpp) and reads every key again, so a
// key changed from Esc > Options in a match counts from the next press.  The bars are found the way
// the launcher finds them (hotkeys.js, loadHotbars): from each playable general's starting building
// and unit through every build button, in the engine's own templates instead of the INI text.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "Common/GlobalData.h"
#include "Common/PlayerTemplate.h"
#include "Common/Registry.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"

namespace
{

enum { BAR_SLOTS = 14 };	///< ControlBar.wnd's ButtonCommand01 to 14; 15 to 18 are for scripts

struct BarObject
{
	UnicodeString name;
	Bool building;
	const CommandButton *slots[ BAR_SLOTS ];
};

struct BarFaction
{
	UnicodeString name;
	std::vector< BarObject > objects;
};

enum CaptureKind { CAPTURE_NONE, CAPTURE_COMMAND, CAPTURE_BUTTON };

typedef std::basic_string< WideChar > KeyText;	///< a key as the page shows it, for a map

const Color TEXT_COLOR = GameMakeColor( 254, 254, 254, 255 );
const Color HEADING_COLOR = GameMakeColor( 186, 255, 12, 255 );
const Color CHANGED_COLOR = GameMakeColor( 255, 214, 92, 255 );
const Color CLASH_COLOR = GameMakeColor( 255, 104, 72, 255 );
const Color QUIET_COLOR = GameMakeColor( 150, 150, 150, 255 );
const WideChar WAITING[] = u"...";	///< the key cell while a key is awaited; the note under the lists says what to press

GameWindow *s_parent = NULL;
GameWindow *s_commandList = NULL;
GameWindow *s_factionCombo = NULL;
GameWindow *s_objectCombo = NULL;
GameWindow *s_buttonList = NULL;
GameWindow *s_note = NULL;
GameWindow *s_change = NULL;
GameWindow *s_clear = NULL;
GameWindow *s_default = NULL;

std::vector< HotkeyCommandRow > s_rows;
HotkeysFile s_file;
std::string s_fileText;		///< Hotkeys.ini as the page opened, to write only what changed
std::map< std::string, HotkeyBinding > s_launcherCommands;
std::map< std::string, std::string > s_launcherButtons;
std::vector< BarFaction > s_factions;

Int s_active = 0;					///< the list touched last: 0 the commands, 1 the bar
Int s_commandRow = -1;		///< index into s_rows
const CommandButton *s_button = NULL;
CaptureKind s_capture = CAPTURE_NONE;
std::string s_holdCandidate;	///< Force Attack's hold: Ctrl or Alt pressed alone so far
Bool s_filling = FALSE;

UnicodeString wide( const std::string& text )
{
	UnicodeString out;
	out.translate( AsciiString( text.c_str() ) );
	return out;
}

std::string readText( const char *path )
{
	File *file = TheFileSystem->openFile( path, File::READ | File::BINARY );
	if( file == NULL )
		return std::string();
	const Int size = file->size();
	char *contents = file->readEntireAndClose();
	std::string text( contents, size );
	delete [] contents;
	return text;
}

AsciiString hotkeysPath( void )
{
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat( "Hotkeys.ini" );
	return path;
}

const HotkeyCommandRow *forceAttackRow( void )
{
	for( size_t each = 0; each < s_rows.size(); each++ )
		if( s_rows[ each ].id == "BEGIN_FORCEATTACK" )
			return &s_rows[ each ];
	return NULL;
}

/// What a command answers to in a match with the keys on the page: the player's, else the launcher's
/// part of the file, else the game's own.  The launcher's own defaults reach the game only from its part.
HotkeyBinding bindingOf( const HotkeyCommandRow& row )
{
	std::map< std::string, HotkeyBinding >::const_iterator mine = s_file.commands.find( row.id );
	if( mine != s_file.commands.end() )
		return mine->second;
	std::map< std::string, HotkeyBinding >::const_iterator launcher = s_launcherCommands.find( row.id );
	if( launcher != s_launcherCommands.end() )
		return launcher->second;
	return row.launcherDefault ? HotkeyBinding() : row.binding;
}

/// One letter as text, upper case; empty for 0.
UnicodeString letterText( WideChar letter )
{
	if( letter >= u'a' && letter <= u'z' )
		letter -= u'a' - u'A';
	const WideChar one[ 2 ] = { letter, 0 };
	return letter != 0 ? UnicodeString( one ) : UnicodeString();
}

UnicodeString chordOf( const HotkeyBinding& binding )
{
	const std::string text = Hotkeys_chordText( binding );
	return text.empty() ? TheGameText->fetch( "GUI:HotkeysNone" ) : wide( text );
}

UnicodeString commandName( const HotkeyCommandRow& row )
{
	static const struct { const char *prefix; const char *label; } FAMILIES[] =
	{
		{ "SAVE_VIEW", "GUI:HotkeysFamilySaveView" }, { "VIEW_VIEW", "GUI:HotkeysFamilyViewView" },
		{ "ADD_TEAM", "GUI:HotkeysFamilyAddTeam" }, { "SHORTCUT_SLOT", "GUI:HotkeysFamilyShortcut" },
		{ "COMMAND_SLOT", "GUI:HotkeysFamilySlot" },
	};
	for( size_t each = 0; each < sizeof( FAMILIES ) / sizeof( FAMILIES[ 0 ] ); each++ )
	{
		const size_t length = strlen( FAMILIES[ each ].prefix );
		if( row.id.compare( 0, length, FAMILIES[ each ].prefix ) == 0 && row.id.size() > length && isdigit( (unsigned char)row.id[ length ] ) )
		{
			UnicodeString name;
			name.format( TheGameText->fetch( FAMILIES[ each ].label ), atoi( row.id.c_str() + length ) );
			return name;
		}
	}
	// a command the game's text has no name for has one of the fork's
	AsciiString own;
	own.format( "GUI:HotkeysCommand%s", row.id.c_str() );
	Bool exists = FALSE;
	const UnicodeString named = TheGameText->fetch( own, &exists );
	if( exists )
		return named;
	if( !row.label.empty() )
		return TheGameText->fetch( row.label.c_str() );
	return wide( row.id );
}

const char *categoryOf( const HotkeyCommandRow& row )
{
	// the beacon keys sit with the smoke signals, both being what a player shows allies
	if( row.id.size() > 7 && row.id.compare( row.id.size() - 7, 7, "_BEACON" ) == 0 )
		return "INFORMATION";
	return row.category.c_str();
}

/// The bar's letters on the page: the launcher's moves, then the player's own over them, by lower case name.
std::map< std::string, WideChar > buttonKeys( void )
{
	std::map< std::string, WideChar > keys;
	for( int pass = 0; pass < 2; pass++ )
	{
		const std::map< std::string, std::string >& from = pass == 0 ? s_launcherButtons : s_file.buttons;
		for( std::map< std::string, std::string >::const_iterator it = from.begin(); it != from.end(); ++it )
		{
			AsciiString name( it->first.c_str() );
			name.toLower();
			keys[ name.str() ] = it->second.empty() ? 0 : (WideChar)it->second[ 0 ];
		}
	}
	return keys;
}

/// A button's key on a bar as text: Force Attack's chord, else its letter; empty for none.
UnicodeString buttonKeyText( const CommandButton *button, const std::map< std::string, WideChar >& keys )
{
	if( button->getCommandType() == GUI_COMMAND_FORCE_ATTACK )
		return wide( Hotkeys_chordText( bindingOf( *forceAttackRow() ) ) );
	const WideChar letter = HotKeyManager::buttonKeyIn( button->getName(),
		TheGameText->fetchEnglishHotKey( button->getTextLabel() ), button->getCommandType(), keys );
	return letterText( letter );
}

UnicodeString buttonName( const CommandButton *button )
{
	UnicodeString text = TheGameText->fetch( button->getTextLabel() );
	UnicodeString out;
	for( const WideChar *at = text.str(); *at; at++ )
		if( *at != u'&' )
		{
			const WideChar one[ 2 ] = { *at, 0 };
			out.concat( one );
		}
	return out.isEmpty() ? wide( button->getName().str() ) : out;
}

/// A text a player should read: not EA's "<<ANGRY M0B>>" placeholders, not a label the text lacks.
Bool shownText( const UnicodeString& text )
{
	static const WideChar MISSING[] = u"MISSING";
	if( text.isEmpty() || text.getCharAt( 0 ) == u'<' )
		return FALSE;
	for( Int at = 0; MISSING[ at ]; at++ )
		if( at >= text.getLength() || text.getCharAt( at ) != MISSING[ at ] )
			return TRUE;
	return FALSE;
}

/// Every playable general with each building and unit its first building and worker lead to.
void findBars( void )
{
	s_factions.clear();
	const CommandButton *force = TheControlBar->findCommandButton( AsciiString( "Command_ForceAttack" ) );
	for( Int index = 0; index < ThePlayerTemplateStore->getPlayerTemplateCount(); index++ )
	{
		const PlayerTemplate *side = ThePlayerTemplateStore->getNthPlayerTemplate( index );
		if( !side->isPlayableSide() || side->isObserver() )
			continue;
		BarFaction faction;
		faction.name = side->getDisplayName();
		std::vector< AsciiString > queue;
		queue.push_back( side->getStartingBuilding() );
		queue.push_back( side->getStartingUnit( 0 ) );
		std::map< std::string, Bool > seen;
		std::map< std::string, UnicodeString > builtBy;
		for( size_t at = 0; at < queue.size(); at++ )
		{
			if( queue[ at ].isEmpty() || seen.count( queue[ at ].str() ) )
				continue;
			seen[ queue[ at ].str() ] = TRUE;
			const ThingTemplate *thing = TheThingFactory->findTemplate( queue[ at ], FALSE );
			const CommandSet *set = thing ? TheControlBar->findCommandSet( thing->friend_getCommandSetString() ) : NULL;
			if( set == NULL )
				continue;
			const CommandButton *slots[ MAX_COMMANDS_PER_SET ];
			Int types[ MAX_COMMANDS_PER_SET ];
			for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
			{
				const CommandButton *button = set->getCommandButton( slot );
				slots[ slot ] = ( button && !BitTest( button->getOptions(), SCRIPT_ONLY ) ) ? button : NULL;
				types[ slot ] = slots[ slot ] ? slots[ slot ]->getCommandType() : GUI_COMMAND_NONE;
			}
			const Int forceSlot = ControlBar_forceAttackSlot( types, MAX_COMMANDS_PER_SET, ControlBar_forceFires( thing ) );
			if( forceSlot >= 0 && force )
				slots[ forceSlot ] = force;
			BarObject object;
			object.building = thing->isKindOf( KINDOF_STRUCTURE );
			Bool any = FALSE;
			for( Int slot = 0; slot < BAR_SLOTS; slot++ )
			{
				// a button with no picture is a leftover no player sees, and what only it builds is not reached
				object.slots[ slot ] = ( slots[ slot ] && slots[ slot ]->getButtonImage() ) ? slots[ slot ] : NULL;
				const CommandButton *button = object.slots[ slot ];
				if( button == NULL )
					continue;
				any = TRUE;
				const GUICommandType type = button->getCommandType();
				const ThingTemplate *built = button->getThingTemplate();
				if( ( type == GUI_COMMAND_UNIT_BUILD || type == GUI_COMMAND_DOZER_CONSTRUCT ) && built )
				{
					queue.push_back( built->getName() );
					if( !builtBy.count( built->getName().str() ) )
						builtBy[ built->getName().str() ] = buttonName( button );
				}
			}
			if( !any )
				continue;
			object.name = thing->getDisplayName();
			if( !shownText( object.name ) )
				object.name = builtBy.count( queue[ at ].str() ) ? builtBy[ queue[ at ].str() ] : wide( queue[ at ].str() );
			faction.objects.push_back( object );
		}
		if( !faction.objects.empty() )
			s_factions.push_back( faction );
	}
}

const BarObject *selectedObject( void )
{
	Int faction = -1, object = -1;
	GadgetComboBoxGetSelectedPos( s_factionCombo, &faction );
	GadgetComboBoxGetSelectedPos( s_objectCombo, &object );
	if( faction < 0 || faction >= (Int)s_factions.size() || object < 0 )
		return NULL;
	const Int index = (Int)(intptr_t)GadgetComboBoxGetItemData( s_objectCombo, object );
	return &s_factions[ faction ].objects[ index ];
}

/// Letters two or more buttons of the open bar share, each with those buttons' names.
std::map< KeyText, std::vector< UnicodeString > > barClashes( const BarObject *object )
{
	std::map< KeyText, std::vector< UnicodeString > > byKey, clashes;
	if( object == NULL )
		return clashes;
	const std::map< std::string, WideChar > keys = buttonKeys();
	for( Int slot = 0; slot < BAR_SLOTS; slot++ )
		if( object->slots[ slot ] )
		{
			const UnicodeString key = buttonKeyText( object->slots[ slot ], keys );
			if( !key.isEmpty() )
				byKey[ key.str() ].push_back( buttonName( object->slots[ slot ] ) );
		}
	for( std::map< KeyText, std::vector< UnicodeString > >::const_iterator it = byKey.begin(); it != byKey.end(); ++it )
		if( it->second.size() > 1 )
			clashes[ it->first ] = it->second;
	return clashes;
}

/// The commands holding the same chord as row, by name.
std::vector< UnicodeString > sameChord( const HotkeyCommandRow& row )
{
	std::vector< UnicodeString > others;
	const HotkeyBinding binding = bindingOf( row );
	if( binding.key.empty() )
		return others;
	for( size_t each = 0; each < s_rows.size(); each++ )
		if( !s_rows[ each ].barOnly && s_rows[ each ].id != row.id && bindingOf( s_rows[ each ] ) == binding )
			others.push_back( commandName( s_rows[ each ] ) );
	return others;
}

/// A bare letter one of the bar's fixed orders owns (stop on S excepted, being the same order).
Bool onFixedKey( const HotkeyCommandRow& row, const HotkeyBinding& binding )
{
	static const char FIXED[] = "ASGYVNCH";
	return binding.modifiers == "NONE" && binding.key.size() == 5 && binding.key.compare( 0, 4, "KEY_" ) == 0
		&& strchr( FIXED, binding.key[ 4 ] ) != NULL && !( row.id == "STOP" && binding.key == "KEY_S" );
}

UnicodeString joined( const std::vector< UnicodeString >& names )
{
	UnicodeString out;
	for( size_t each = 0; each < names.size(); each++ )
	{
		if( each > 0 )
			out.concat( u", " );
		out.concat( names[ each ] );
	}
	return out;
}

void fillCommands( void )
{
	s_filling = TRUE;
	const Int top = GadgetListBoxGetTopVisibleEntry( s_commandList );
	GadgetListBoxReset( s_commandList );
	static const char *const ORDER[] = { "SELECTION", "TEAM", "INFORMATION", "CONTROL", "INTERFACE", "SHORTCUTS", "GRID", "MISC" };
	Int selectedRow = -1;
	for( size_t category = 0; category < sizeof( ORDER ) / sizeof( ORDER[ 0 ] ); category++ )
	{
		Bool headed = FALSE;
		for( size_t each = 0; each < s_rows.size(); each++ )
		{
			const HotkeyCommandRow& row = s_rows[ each ];
			const char *own = categoryOf( row );
			Bool known = FALSE;
			for( size_t other = 0; other < sizeof( ORDER ) / sizeof( ORDER[ 0 ] ); other++ )
				known = known || strcmp( own, ORDER[ other ] ) == 0;
			if( row.barOnly || strcmp( known ? own : "MISC", ORDER[ category ] ) != 0 )
				continue;
			if( !headed )
			{
				AsciiString label;
				label.format( "GUI:HotkeysCategory%s", ORDER[ category ] );
				const Int heading = GadgetListBoxAddEntryText( s_commandList, TheGameText->fetch( label ), HEADING_COLOR, -1, 0 );
				GadgetListBoxSetItemData( s_commandList, (void *)0, heading, 0 );
				headed = TRUE;
			}
			const HotkeyBinding binding = bindingOf( row );
			const Bool waiting = s_capture == CAPTURE_COMMAND && (Int)each == s_commandRow;
			const Bool changed = s_file.commands.count( row.id ) != 0;
			const Bool clash = !sameChord( row ).empty() || ( changed && onFixedKey( row, binding ) );
			const Int at = GadgetListBoxAddEntryText( s_commandList, commandName( row ), changed ? CHANGED_COLOR : TEXT_COLOR, -1, 0 );
			GadgetListBoxAddEntryText( s_commandList, waiting ? UnicodeString( WAITING ) : chordOf( binding ),
				waiting ? HEADING_COLOR : clash ? CLASH_COLOR : binding.key.empty() && binding.modifiers == "NONE" ? QUIET_COLOR : TEXT_COLOR, at, 1 );
			GadgetListBoxSetItemData( s_commandList, (void *)(intptr_t)( each + 1 ), at, 0 );
			if( (Int)each == s_commandRow )
				selectedRow = at;
		}
	}
	if( selectedRow >= 0 )
		GadgetListBoxSetSelected( s_commandList, selectedRow );
	GadgetListBoxSetTopVisibleEntry( s_commandList, top );
	s_filling = FALSE;
}

void fillButtons( void )
{
	s_filling = TRUE;
	GadgetListBoxReset( s_buttonList );
	const BarObject *object = selectedObject();
	if( object == NULL )
	{
		s_filling = FALSE;
		return;
	}
	const std::map< std::string, WideChar > keys = buttonKeys();
	const std::map< KeyText, std::vector< UnicodeString > > clashes = barClashes( object );
	// a cameo is 61 by 48 on the bar; a third of that keeps fourteen rows to a short scroll
	const Int height = 22;
	Int selectedRow = -1;
	// the bar's own order: two rows of seven read down each column, 1 over 2
	for( Int slot = 0; slot < BAR_SLOTS; slot++ )
	{
		const CommandButton *button = object->slots[ slot ];
		if( button == NULL )
			continue;
		const UnicodeString key = buttonKeyText( button, keys );
		const Bool waiting = s_capture == CAPTURE_BUTTON && button == s_button;
		const Bool changed = button->getCommandType() == GUI_COMMAND_FORCE_ATTACK
			? s_file.commands.count( "BEGIN_FORCEATTACK" ) != 0
			: s_file.buttons.count( HotKeyManager::fileButtonName( button->getName() ) ) != 0;
		const Int at = GadgetListBoxAddEntryImage( s_buttonList, button->getButtonImage(), -1, 0, height, height * 61 / 48 );
		GadgetListBoxAddEntryText( s_buttonList, buttonName( button ), changed ? CHANGED_COLOR : TEXT_COLOR, at, 1 );
		GadgetListBoxAddEntryText( s_buttonList,
			waiting ? UnicodeString( WAITING ) : key.isEmpty() ? TheGameText->fetch( "GUI:HotkeysNone" ) : key,
			waiting ? HEADING_COLOR : clashes.count( key.str() ) ? CLASH_COLOR : key.isEmpty() ? QUIET_COLOR : TEXT_COLOR, at, 2 );
		GadgetListBoxSetItemData( s_buttonList, const_cast< CommandButton * >( button ), at, 0 );
		if( button == s_button )
			selectedRow = at;
	}
	if( selectedRow >= 0 )
		GadgetListBoxSetSelected( s_buttonList, selectedRow );
	s_filling = FALSE;
}

void fillObjects( void )
{
	s_filling = TRUE;
	GadgetComboBoxReset( s_objectCombo );
	Int faction = -1;
	GadgetComboBoxGetSelectedPos( s_factionCombo, &faction );
	if( faction >= 0 && faction < (Int)s_factions.size() )
	{
		// buildings first, then what they build, each in the order the general reaches them
		const std::vector< BarObject >& objects = s_factions[ faction ].objects;
		for( Int pass = 0; pass < 2; pass++ )
			for( size_t each = 0; each < objects.size(); each++ )
				if( objects[ each ].building == ( pass == 0 ) )
				{
					const Int at = GadgetComboBoxAddEntry( s_objectCombo, objects[ each ].name, TEXT_COLOR );
					GadgetComboBoxSetItemData( s_objectCombo, at, (void *)(intptr_t)each );
				}
		GadgetComboBoxSetSelectedPos( s_objectCombo, 0 );
	}
	s_filling = FALSE;
	s_button = NULL;
	fillButtons();
}

/// The line under the lists: what is being waited for, or what the selection's key is and what it clashes with.
void updateNote( void )
{
	UnicodeString note;
	const BarObject *object = selectedObject();
	Bool canChange = FALSE, changed = FALSE;
	if( s_active == 0 && s_commandRow >= 0 )
	{
		const HotkeyCommandRow& row = s_rows[ s_commandRow ];
		const HotkeyBinding binding = bindingOf( row );
		changed = s_file.commands.count( row.id ) != 0;
		canChange = TRUE;
		if( s_capture == CAPTURE_COMMAND )
			note = TheGameText->fetch( "GUI:HotkeysPressKey" );
		else
		{
			note.format( u"%s: %s.", commandName( row ).str(), chordOf( binding ).str() );
			const std::vector< UnicodeString > others = sameChord( row );
			UnicodeString line;
			if( !others.empty() )
			{
				line.format( TheGameText->fetch( "GUI:HotkeysAlsoOn" ), joined( others ).str() );
				note.concat( u" " );
				note.concat( line );
			}
			if( changed && onFixedKey( row, binding ) )
			{
				note.concat( u" " );
				note.concat( TheGameText->fetch( "GUI:HotkeysOnFixedKey" ) );
			}
			if( !changed && s_launcherCommands.count( row.id ) && !row.launcherDefault )
			{
				line.format( TheGameText->fetch( "GUI:HotkeysMoved" ), chordOf( row.binding ).str() );
				note.concat( u" " );
				note.concat( line );
			}
		}
	}
	else if( s_active == 1 && s_button )
	{
		const Bool force = s_button->getCommandType() == GUI_COMMAND_FORCE_ATTACK;
		const Bool fixed = !force && HotKeyManager::fixedKey( s_button->getName(), s_button->getCommandType() ) != 0;
		const std::string name = HotKeyManager::fileButtonName( s_button->getName() );
		changed = force ? s_file.commands.count( "BEGIN_FORCEATTACK" ) != 0 : s_file.buttons.count( name ) != 0;
		canChange = !fixed;
		if( s_capture == CAPTURE_BUTTON )
			note = TheGameText->fetch( force ? "GUI:HotkeysPressForceAttack" : "GUI:HotkeysPressLetter" );
		else if( fixed )
			note = TheGameText->fetch( "GUI:HotkeysFixed" );
		else if( force )
			note = TheGameText->fetch( "GUI:HotkeysForceAttack" );
		else if( changed )
		{
			const UnicodeString key = letterText( TheGameText->fetchEnglishHotKey( s_button->getTextLabel() ) );
			note.format( TheGameText->fetch( "GUI:HotkeysGameKey" ), key.isEmpty() ? TheGameText->fetch( "GUI:HotkeysNone" ).str() : key.str() );
		}
		else if( s_launcherButtons.count( name ) )
			note = TheGameText->fetch( "GUI:HotkeysButtonMoved" );
	}
	else
		note = TheGameText->fetch( "GUI:HotkeysPick" );

	// a clash on the open bar is said whatever is selected, as the launcher says it under the bar
	if( s_capture == CAPTURE_NONE )
	{
		const std::map< KeyText, std::vector< UnicodeString > > clashes = barClashes( object );
		for( std::map< KeyText, std::vector< UnicodeString > >::const_iterator it = clashes.begin(); it != clashes.end(); ++it )
		{
			UnicodeString line;
			line.format( TheGameText->fetch( "GUI:HotkeysClash" ), it->first.c_str(), joined( it->second ).str() );
			if( !note.isEmpty() )
				note.concat( u" " );
			note.concat( line );
		}
	}
	GadgetStaticTextSetText( s_note, note );
	s_change->winEnable( canChange );
	s_clear->winEnable( canChange );
	s_default->winEnable( changed );
}

void refresh( void )
{
	fillCommands();
	fillButtons();
	updateNote();
}

void startCapture( void )
{
	if( s_active == 0 && s_commandRow >= 0 )
		s_capture = CAPTURE_COMMAND;
	else if( s_active == 1 && s_button && ( s_button->getCommandType() == GUI_COMMAND_FORCE_ATTACK
			|| HotKeyManager::fixedKey( s_button->getName(), s_button->getCommandType() ) == 0 ) )
		s_capture = CAPTURE_BUTTON;
	else
		return;
	s_holdCandidate.clear();
	// every key comes to the menu while it waits, Enter and the arrows included
	TheWindowManager->winSetFocus( s_parent );
	refresh();
}

void setCommand( const std::string& id, const HotkeyBinding *binding )
{
	if( binding )
		s_file.commands[ id ] = *binding;
	else
		s_file.commands.erase( id );
}

/// Change to `binding`, or with NULL back to the game's own, whatever is selected.
void applySelected( const HotkeyBinding *binding, Bool backToGame )
{
	if( s_active == 0 && s_commandRow >= 0 )
		setCommand( s_rows[ s_commandRow ].id, backToGame ? NULL : binding );
	else if( s_active == 1 && s_button )
	{
		if( s_button->getCommandType() == GUI_COMMAND_FORCE_ATTACK )
			setCommand( "BEGIN_FORCEATTACK", backToGame ? NULL : binding );
		else if( backToGame )
			s_file.buttons.erase( HotKeyManager::fileButtonName( s_button->getName() ) );
		else
			s_file.buttons[ HotKeyManager::fileButtonName( s_button->getName() ) ] = binding->key.size() == 5 ? binding->key.substr( 4 ) : std::string();
	}
	s_capture = CAPTURE_NONE;
	refresh();
}

std::string keyName( UnsignedByte key )
{
	for( const LookupListRec *rec = KeyNames; rec->name != NULL; ++rec )
		if( rec->value == (Int)key && Hotkeys_isKeyName( rec->name ) )
			return rec->name;
	return std::string();
}

const char *modifierName( Bool ctrl, Bool alt, Bool shift )
{
	static const char *const NAMES[ 8 ] = { "NONE", "CTRL", "ALT", "CTRL_ALT", "SHIFT", "SHIFT_CTRL", "SHIFT_ALT", "SHIFT_ALT_CTRL" };
	return NAMES[ ( ctrl ? 1 : 0 ) | ( alt ? 2 : 0 ) | ( shift ? 4 : 0 ) ];
}

}	// namespace

//-------------------------------------------------------------------------------------------------
void OptionsHotkeysInit( void )
{
	s_parent = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:OptionsMenuParent" ) );
	s_commandList = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ListboxHotkeyCommands" ) );
	s_factionCombo = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ComboBoxHotkeyFaction" ) );
	s_objectCombo = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ComboBoxHotkeyObject" ) );
	s_buttonList = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ListboxHotkeyButtons" ) );
	s_note = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:HotkeyNote" ) );
	s_change = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ButtonHotkeyChange" ) );
	s_clear = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ButtonHotkeyClear" ) );
	s_default = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ButtonHotkeyDefault" ) );

	AsciiString commandMap;
	commandMap.format( "Data\\%s\\CommandMap.ini", GetRegistryLanguage().str() );
	s_rows = Hotkeys_commandRows( readText( commandMap.str() ) );
	s_fileText = TheFileSystem->doesFileExist( hotkeysPath().str() ) ? readText( hotkeysPath().str() ) : std::string();
	s_file = Hotkeys_parse( s_fileText, s_rows );
	s_launcherCommands.clear();
	s_launcherButtons.clear();
	Hotkeys_launcherKeys( s_file.launcherPart, &s_launcherCommands, &s_launcherButtons );
	s_active = 0;
	s_commandRow = -1;
	s_button = NULL;
	s_capture = CAPTURE_NONE;

	findBars();
	s_filling = TRUE;
	GadgetComboBoxReset( s_factionCombo );
	for( size_t each = 0; each < s_factions.size(); each++ )
		GadgetComboBoxAddEntry( s_factionCombo, s_factions[ each ].name, TEXT_COLOR );
	GadgetComboBoxSetSelectedPos( s_factionCombo, 0 );
	s_filling = FALSE;
	fillObjects();
	refresh();
}

//-------------------------------------------------------------------------------------------------
Bool OptionsHotkeysSystem( UnsignedInt msg, GameWindow *control, WindowMsgData row )
{
	if( s_commandList == NULL || s_filling )
		return FALSE;
	const Bool ours = control == s_commandList || control == s_buttonList || control == s_factionCombo
		|| control == s_objectCombo || control == s_change || control == s_clear || control == s_default;
	if( !ours )
		return FALSE;
	// a press anywhere on the page lets go of a key being waited for
	const Bool wasWaiting = s_capture != CAPTURE_NONE;
	s_capture = CAPTURE_NONE;

	if( msg == GCM_SELECTED )
	{
		if( control == s_factionCombo )
			fillObjects();
		else
		{
			s_button = NULL;
			fillButtons();
		}
	}
	else if( msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED )
	{
		if( (Int)row < 0 )
			return TRUE;
		if( control == s_commandList )
		{
			const Int index = (Int)(intptr_t)GadgetListBoxGetItemData( s_commandList, (Int)row, 0 ) - 1;
			if( index < 0 )
				return TRUE;	// a category's heading
			s_active = 0;
			s_commandRow = index;
		}
		else
		{
			s_active = 1;
			s_button = (const CommandButton *)GadgetListBoxGetItemData( s_buttonList, (Int)row, 0 );
		}
		if( msg == GLM_DOUBLE_CLICKED )
		{
			startCapture();
			return TRUE;
		}
	}
	else if( msg == GBM_SELECTED )
	{
		if( control == s_change && !wasWaiting )
		{
			startCapture();
			return TRUE;
		}
		if( control == s_clear )
		{
			const HotkeyBinding none;
			applySelected( &none, FALSE );
			return TRUE;
		}
		if( control == s_default )
		{
			applySelected( NULL, TRUE );
			return TRUE;
		}
	}
	refresh();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool OptionsHotkeysKey( UnsignedByte key, Int state )
{
	if( s_capture == CAPTURE_NONE )
		return FALSE;
	const Bool down = BitTest( state, KEY_STATE_DOWN );
	if( key == KEY_ESC )
	{
		if( !down )
		{
			s_capture = CAPTURE_NONE;
			refresh();
		}
		return TRUE;
	}
	if( BitTest( state, KEY_STATE_AUTOREPEAT ) )
		return TRUE;
	const Bool ctrl = BitTest( state, KEY_STATE_CONTROL ), alt = BitTest( state, KEY_STATE_ALT ), shift = BitTest( state, KEY_STATE_SHIFT );
	const Bool ctrlKey = key == KEY_LCTRL || key == KEY_RCTRL, altKey = key == KEY_LALT || key == KEY_RALT;
	const Bool shiftKey = key == KEY_LSHIFT || key == KEY_RSHIFT;
	const Bool force = s_capture == CAPTURE_BUTTON && s_button->getCommandType() == GUI_COMMAND_FORCE_ATTACK;

	if( force )
	{
		// Ctrl or Alt let go with no key between is the hold alone; Shift cannot be the hold
		if( ctrlKey || altKey )
		{
			if( down )
				s_holdCandidate = s_holdCandidate.empty() ? ( ctrlKey ? "CTRL" : "ALT" ) : std::string( "-" );
			else if( s_holdCandidate == "CTRL" || s_holdCandidate == "ALT" )
			{
				const HotkeyBinding hold( "", s_holdCandidate );
				applySelected( &hold, FALSE );
			}
			return TRUE;
		}
		const std::string name = keyName( key );
		if( down && !shift && !name.empty() )
		{
			const HotkeyBinding hold( name, modifierName( ctrl, alt, FALSE ) );
			applySelected( &hold, FALSE );
		}
		else if( down )
			s_holdCandidate = "-";
		return TRUE;
	}
	if( !down || ctrlKey || altKey || shiftKey )
		return TRUE;
	const std::string name = keyName( key );
	if( name.empty() )
		return TRUE;
	if( s_capture == CAPTURE_BUTTON )
	{
		// a button takes a letter and nothing else
		if( name.size() == 5 && isalpha( (unsigned char)name[ 4 ] ) )
		{
			const HotkeyBinding letter( name, "NONE" );
			applySelected( &letter, FALSE );
		}
		return TRUE;
	}
	const HotkeyBinding chord( name, modifierName( ctrl, alt, shift ) );
	applySelected( &chord, FALSE );
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void OptionsHotkeysSave( void )
{
	if( s_commandList == NULL )
		return;
	const std::string text = Hotkeys_format( s_file, s_rows );
	// a page looked at and left alone writes nothing, so a file never written stays unwritten
	if( text == Hotkeys_format( Hotkeys_parse( s_fileText, s_rows ), s_rows ) )
	{
		s_commandList = NULL;
		return;
	}
	FILE *file = fopen( hotkeysPath().str(), "wb" );
	if( file == NULL )
	{
		DEBUG_LOG(( "HOTKEYS: %s could not be written\n", hotkeysPath().str() ));
		s_commandList = NULL;
		return;
	}
	fwrite( text.data(), 1, text.size(), file );
	fclose( file );
	DEBUG_LOG(( "HOTKEYS: wrote %s from the options menu\n", hotkeysPath().str() ));
	s_commandList = NULL;
	HotKeyManager::reloadKeys();
}

//-------------------------------------------------------------------------------------------------
void OptionsHotkeysClose( void )
{
	s_commandList = NULL;
	s_capture = CAPTURE_NONE;
}
