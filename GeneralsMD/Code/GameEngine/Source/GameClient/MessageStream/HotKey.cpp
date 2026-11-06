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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: HotKey.cpp /////////////////////////////////////////////////
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
//	Filename: 	HotKey.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include <algorithm>
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameClient/HotKey.h"
#include "Common/FileSystem.h"
#include "Common/Registry.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameText.h"
#include "Common/AudioEventRTS.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/ThingTemplate.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
/** The letter the player's key with this scan code types, the one a label's '&' and the bar's keys
	* mean.  The game's own key tables know four layouts and name every key by its US position, so on
	* a Turkish keyboard the key marked i read as an apostrophe; on Windows the player's layout says. */
static WideChar typedLetter( UnsignedByte scanCode )
{
	WideChar typed = TheKeyboard->getPrintableKey(scanCode, 0);
#if defined(_WIN32)
	HKL layout = GetKeyboardLayout(0);
	const UINT virtualKey = MapVirtualKeyExW(scanCode, MAPVK_VSC_TO_VK, layout);
	BYTE keys[256] = { 0 };
	WCHAR out[4] = { 0 };
	// a Cyrillic or other non-Latin letter never names a hotkey, so that player keeps the US position
	if( virtualKey != 0 && ToUnicodeEx(virtualKey, scanCode, keys, out, 4, 0x4, layout) == 1 && out[0] < 0x0250 )
		typed = (WideChar)out[0];
#endif
	return typed;
}

//-----------------------------------------------------------------------------
/** One keystroke does one thing (issue 128).  MetaEventTranslator fires a command map key on its
	* press and this translator a command bar button on its release, so a letter both held used to do
	* both: W selected every aircraft, the empty selection put a worker's bar up, and the release
	* started his War Factory.  The press decides now (HotKeyManager::strokeDown): a showing button on
	* the letter takes the stroke and the command stands aside; otherwise the command keeps it as
	* before and the release presses nothing.  By scan code, the key both translators are handed. */
enum { STROKE_KEYS = 256 };
static Bool s_strokeToBar[ STROKE_KEYS ];
static Bool s_strokeToCommand[ STROKE_KEYS ];

void HotKeyManager::strokeDown( Int key, Bool barHolds )
{
	if( key < 0 || key >= STROKE_KEYS )
		return;
	s_strokeToBar[ key ] = barHolds;
	s_strokeToCommand[ key ] = FALSE;
}

Bool HotKeyManager::strokeToBar( Int key )
{
	return key >= 0 && key < STROKE_KEYS && s_strokeToBar[ key ];
}

void HotKeyManager::strokeToCommand( Int key )
{
	if( key >= 0 && key < STROKE_KEYS )
		s_strokeToCommand[ key ] = TRUE;
}

Bool HotKeyManager::strokePressesBar( Int key )
{
	if( key < 0 || key >= STROKE_KEYS )
		return TRUE;
	const Bool command = s_strokeToCommand[ key ];
	s_strokeToBar[ key ] = s_strokeToCommand[ key ] = FALSE;
	return !command;
}

// Alt and the Evacuate button's key empty a transport where it stands instead of asking where
// (ControlBar::processCommandUI), so that chord is the bar's
static Bool isEvacuateButton( GameWindow *win )
{
	const CommandButton *command = win ? (const CommandButton *)GadgetButtonGetData( win ) : NULL;
	return command && command->getCommandType() == GUI_COMMAND_EVACUATE;
}

Bool HotKeyManager::barHoldsKey( Int key, Int modState )
{
	// the release presses a button bare or with shift (a batch); a chord with Ctrl or Alt is the
	// command's, but for Alt with the Evacuate button's key
	const Bool alt = modState == ALT;
	if( TheHotKeyManager == NULL || TheKeyboard == NULL || key < 0 || key >= STROKE_KEYS ||
			( ( modState & ~SHIFT ) != 0 && !alt ) )
		return FALSE;
	if( TheShell && TheShell->isShellActive() )
		return FALSE;	// the menus have no command bar, whatever the map still names
	const WideChar letter = typedLetter( (UnsignedByte)key );
	if( letter == 0 )
		return FALSE;
	Bool pressable = FALSE;
	GameWindow *win = TheHotKeyManager->findHotKey( nameOf( letter ), &pressable );
	return win != NULL && !BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) && ( !alt || isEvacuateButton( win ) );
}

Bool HotKeyManager::commandMapHolds( const AsciiString& key )
{
	if( TheMetaMap == NULL || TheKeyboard == NULL || key.isEmpty() )
		return FALSE;
	// the replay's fast forward lets its key through to the bar (MetaEventTranslator), so it holds none
	for( const MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next )
		if( rec->m_modState == NONE && BitTest( rec->m_usableIn, COMMANDUSABLE_GAME ) && rec->m_key != MK_NONE
				&& rec->m_meta != GameMessage::MSG_META_TOGGLE_FAST_FORWARD_REPLAY
				&& nameOf( typedLetter( (UnsignedByte)rec->m_key ) ) == key )
			return TRUE;
	return FALSE;
}

//-----------------------------------------------------------------------------
GameMessageDisposition HotKeyTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	GameMessage::Type t = msg->getType();

	if ( t == GameMessage::MSG_RAW_KEY_UP)
	{
		// a press the command map took presses no button on its release, whatever the bar shows now
		const Bool barHeld = HotKeyManager::strokeToBar( msg->getArgument(0)->integer );
		if( !HotKeyManager::strokePressesBar( msg->getArgument(0)->integer ) )
			return disp;

		//char key = msg->getArgument(0)->integer;
		Int keyState = msg->getArgument(1)->integer;

		// for our purposes here, we don't care to distinguish between right and left keys,
		// so just fudge a little to simplify things.
		Int newModState = 0;

		if( keyState & KEY_STATE_CONTROL )
		{
			newModState |= CTRL;
		}

		if( keyState & KEY_STATE_SHIFT )
		{
			newModState |= SHIFT;
		}

		if( keyState & KEY_STATE_ALT )
		{
			newModState |= ALT;
		}
		// Alt and the Evacuate button's key (barHoldsKey), whichever was let go first: the transport
		// empties where it stands, as an Alt-click on the button does (ControlBar::processCommandUI)
		if( barHeld && ( keyState & ( KEY_STATE_ALT | KEY_STATE_PRESSED_WITH_CTRL_ALT ) ) && !( keyState & KEY_STATE_CONTROL ) )
		{
			Bool pressable = FALSE;
			GameWindow *win = TheHotKeyManager ? TheHotKeyManager->findHotKey(
				HotKeyManager::nameOf( typedLetter( (UnsignedByte)msg->getArgument(0)->integer ) ), &pressable ) : NULL;
			if( pressable && isEvacuateButton( win ) )
			{
				TheInGameUI->setGUICommand( NULL );
				pickAndPlayUnitVoiceResponse( TheInGameUI->getAllSelectedDrawables(), GameMessage::MSG_EVACUATE );
				TheMessageStream->appendMessage( GameMessage::MSG_EVACUATE );
				AudioEventRTS buttonClick( "GUIClick" );
				if( TheAudio )
					TheAudio->addAudioEvent( &buttonClick );
				return DESTROY_MESSAGE;
			}
		}

		// Ctrl+Alt and a build button's key puts its unit on repeat or takes it off
		const Bool repeatChord = newModState == ( CTRL | ALT );
		// shift is allowed through: a shift-click on a build button queues a batch of units, and
		// the label hotkey is meant to be that click's equal. Ctrl and alt still block the hotkey.
		if( !repeatChord && ( newModState & ~SHIFT ) != 0 )
			return disp;
		// Ctrl+F let go of Ctrl first ends on a bare F release; it is still Ctrl+F
		if( !repeatChord && ( keyState & KEY_STATE_PRESSED_WITH_CTRL_ALT ) )
			return disp;
		const UnsignedByte scanCode = (UnsignedByte)msg->getArgument(0)->integer;
		const WideChar typed = typedLetter( scanCode );
		if( repeatChord )
		{
			// straight to the bar rather than as a click: a repeated unit the bank cannot pay for
			// right now has a greyed button, and that is when the repeat most wants stopping
			Bool pressable = FALSE;
			GameWindow *win = TheHotKeyManager ? TheHotKeyManager->findHotKey( HotKeyManager::nameOf( typed ), &pressable ) : NULL;
			const CommandButton *command = win ? (const CommandButton *)GadgetButtonGetData( win ) : NULL;
			if( command && command->getCommandType() == GUI_COMMAND_UNIT_BUILD &&
					!BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
			{
				TheControlBar->toggleRepeatUnit( command->getThingTemplate() );
				AudioEventRTS buttonClick( "GUIClick" );
				if( TheAudio )
					TheAudio->addAudioEvent( &buttonClick );
				disp = DESTROY_MESSAGE;
			}
			return disp;
		}
		if(TheHotKeyManager && TheHotKeyManager->executeHotKey(HotKeyManager::nameOf(typed)))
			disp = DESTROY_MESSAGE;
	}
	return disp;
}

//-----------------------------------------------------------------------------
HotKey::HotKey()
{
	m_win = NULL;
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_key.clear();
	//
	m_claim = 0;
}

//-----------------------------------------------------------------------------
HotKeyManager::HotKeyManager( void )
{

}

//-----------------------------------------------------------------------------
HotKeyManager::~HotKeyManager( void )
{
	m_hotKeyMap.clear();
}
	
//-----------------------------------------------------------------------------
void HotKeyManager::init( void )
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::reset( void )
{
	m_hotKeyMap.clear();
}

static Int keyClaim( GameWindow *win, const AsciiString& key, Int barSlot );

//-----------------------------------------------------------------------------
Bool HotKeyManager::addHotKey( GameWindow *win, const AsciiString& keyIn, Int barSlot, GameWindow **displaced )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	// two buttons on one letter, which the Hotkeys page marks: a fixed order on its own letter keeps
	// it, a letter the player gave beats a label's or the grid's, and between equals the first keeps it
	const Int claim = keyClaim( win, key, barSlot );
	if( it != m_hotKeyMap.end() && it->second.m_win != win && claim <= it->second.m_claim )
	{
		DEBUG_LOG(( "HOTKEYS: %s already on %s, not on %s\n", key.str(), it->second.m_win->winGetInstanceData()->m_decoratedNameString.str(), win->winGetInstanceData()->m_decoratedNameString.str() ));
		return FALSE;
	}
	if( displaced )
		*displaced = ( it != m_hotKeyMap.end() && it->second.m_win != win ) ? it->second.m_win : NULL;
	HotKey newHK;
	newHK.m_key.set(key);
	newHK.m_win = win;
	newHK.m_claim = claim;
	m_hotKeyMap[key] = newHK;
	return TRUE;
}

//-----------------------------------------------------------------------------
GameWindow *HotKeyManager::findHotKey( const AsciiString& keyIn, Bool *pressable ) const
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::const_iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() || it->second.m_win == NULL )
		return NULL;

	GameWindow *win = it->second.m_win;
	const UnsignedInt status = win->winGetStatus();
	*pressable = !BitTest( status, WIN_STATUS_HIDDEN ) && BitTest( status, WIN_STATUS_ENABLED );
	return win;
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::executeHotKey( const AsciiString& keyIn )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;
	GameWindow *win = it->second.m_win;
	if( !win )
		return FALSE;
	if( !BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
	{
		if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
 		{
			// what a player's own key (Hotkeys.ini) pressed can be read back from the log
			const CommandButton *command = (const CommandButton *)GadgetButtonGetData( win );
			const DrawableList *selected = TheInGameUI ? TheInGameUI->getAllSelectedDrawables() : NULL;
			DEBUG_LOG(( "HOTKEYS: %s pressed %s, selection led by %s\n", key.str(), command ? command->getName().str() : "a window",
				( selected && !selected->empty() ) ? selected->front()->getTemplate()->getName().str() : "nothing" ));
 			TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );
 
 			// here we make the same click sound that the GUI uses when you click a button
 			AudioEventRTS buttonClick("GUIClick");
 
 			if( TheAudio )
 			{
 				TheAudio->addAudioEvent( &buttonClick );
 			}  // end if
			return TRUE;
 		}

		const CommandButton *disabled = (const CommandButton *)GadgetButtonGetData( win );
		DEBUG_LOG(( "HOTKEYS: %s found %s disabled\n", key.str(), disabled ? disabled->getName().str() : "a window" ));
		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
		{
			TheAudio->addAudioEvent( &disabledClick );
		}
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
/** The map key for a letter: lower case, as UTF-8, so a Turkish letter is itself rather than the
	* low byte of itself.  I, dotted İ and dotless ı are all one key: a Turkish label's İptal and an
	* English player's I key have to meet, and so do the I key of a Turkish Q layout and a label's I. */
AsciiString HotKeyManager::nameOf( WideChar c )
{
	if( c == 0x0130 || c == 0x0131 || c == L'I' )
		c = L'i';
	else if( c >= L'A' && c <= L'Z' )
		c = c + ( L'a' - L'A' );
	else if( c >= 0x00C0 && c <= 0x00DE && c != 0x00D7 )
		c = c + 0x20;
	else if( c >= 0x0100 && c <= 0x017F && ( c & 1 ) == 0 )
		c = c + 1;	// Latin Extended-A pairs: Ğ ğ, Ş ş

	char utf8[ 4 ] = { 0 };
	if( c < 0x80 )
		utf8[ 0 ] = (char)c;
	else if( c < 0x800 )
	{
		utf8[ 0 ] = (char)( 0xC0 | ( c >> 6 ) );
		utf8[ 1 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	else
	{
		utf8[ 0 ] = (char)( 0xE0 | ( c >> 12 ) );
		utf8[ 1 ] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
		utf8[ 2 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	return AsciiString( utf8 );
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const AsciiString& label)
{
	return searchHotKey(TheGameText->fetch(label));
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const UnicodeString& uStr )
{
	if(uStr.isEmpty())
		return AsciiString::TheEmptyString;

	const WideChar *marker = (const WideChar *)uStr.str();
	while (marker && *marker)
	{
		if (*marker == u'&')
		{
			// found a '&' - now look for the next char
			return nameOf( *(marker+1) );
		}
		marker++;
	}
	return AsciiString::TheEmptyString;	
}

//-----------------------------------------------------------------------------
/** The player's command button keys from Hotkeys.ini, by button name in lower case: an upper case
	* letter, or 0 for a button the player wants without a key.  Read once while the engine starts and
	* kept for the whole run, so it lives outside the manager, which every game client rebuilds. */
static std::map< std::string, WideChar > s_playerButtonKeys;

/** The owner's fixed keys, 2026-10-12, on every bar: A attack move, S stop, G guard, Y rally
	* point, V evacuate, N sell, C combat drop, and H for land mines, camo netting and the GLA's fake
	* and real switches, which go by button name.  They are defaults: the player may move any of these
	* orders to another letter, or none, like every other button.  No button keeps one of these
	* letters from its label; the player may give one to any button, and on a bar where the fixed order
	* stands too, the order gets the key (addHotKey).  The launcher holds the same table (hotkeys.js,
	* FIXED_KEYS). */
static const WideChar FIXED_KEYS[] = { 'A', 'S', 'G', 'Y', 'V', 'N', 'C', 'H' };
/** FixedOrderKeys in Hotkeys.ini: with Enabled = No none of the above holds, and every button, the
	* orders included, wears its label's letter or the player's, as in the game as shipped.  Read with
	* the button keys, back to on before each read. */
static Bool s_fixedOrderKeys = TRUE;
/** CommandBarGrid in Hotkeys.ini: a command bar button takes its window slot's letter
	* (HotKeyManager::commandBarGridKey) in place of its fixed and label letters.  Read with the button
	* keys, back to off before each read.  The launcher holds the same tables (hotkeys.js, GRID_ROWS
	* and GRID_UPGRADE_KEYS). */
static Bool s_commandBarGrid = FALSE;
static const char GRID_ROWS[ 2 ][ 10 ] = { "QWERTYUIO", "ASDFGHJKL" };
static const char GRID_UPGRADE_KEYS[] = "XCVBN";
static const char *const FIXED_H_BUTTONS[] =
{
	"command_upgradechinamines", "command_upgradeempmines", "command_upgradecamonetting", "command_upgradeglacamonetting",
	"command_upgradeglaworkerfakecommandset", "command_upgradeglaworkerrealcommandset"
};
static const char FIXED_H_PREFIX[] = "command_becomerealgla";	///< a fake building's switch to the real one

static WideChar fixedButtonKey( const AsciiString& lowerName, Int commandType )
{
	if( !s_fixedOrderKeys )
		return 0;
	if( strncmp( lowerName.str(), FIXED_H_PREFIX, sizeof( FIXED_H_PREFIX ) - 1 ) == 0 )
		return 'H';
	for( Int each = 0; each < (Int)( sizeof( FIXED_H_BUTTONS ) / sizeof( FIXED_H_BUTTONS[ 0 ] ) ); each++ )
		if( lowerName.compare( FIXED_H_BUTTONS[ each ] ) == 0 )
			return 'H';
	switch( commandType )
	{
		case GUI_COMMAND_ATTACK_MOVE:			return 'A';
		case GUI_COMMAND_STOP:						return 'S';
		case GUI_COMMAND_GUARD:						return 'G';
		case GUI_COMMAND_SET_RALLY_POINT:	return 'Y';
		case GUI_COMMAND_EVACUATE:				return 'V';
		case GUI_COMMAND_SELL:						return 'N';
		case GUI_COMMAND_COMBATDROP:			return 'C';
	}
	return 0;
}

/** The player's key for a button, into *letter; FALSE leaves it alone. */
static Bool findPlayerKey( const std::map< std::string, WideChar >& keys, const std::string& bare, WideChar *letter )
{
	std::map< std::string, WideChar >::const_iterator it = keys.find( bare );
	if( it == keys.end() )
		return FALSE;
	*letter = it->second;
	return TRUE;
}

/** The player's key for a button standing in command bar slot barSlot (-1 off the bar), into *letter:
	* his "<button>@<barSlot + 1>" there, else his "<button>".  FALSE leaves *letter alone. */
static Bool findPlayerSlotKey( const std::map< std::string, WideChar >& keys, const std::string& bare, Int barSlot, WideChar *letter )
{
	if( barSlot >= 0 )
	{
		char at[ 16 ];
		snprintf( at, sizeof( at ), "@%d", barSlot + 1 );
		if( findPlayerKey( keys, bare + at, letter ) )
			return TRUE;
	}
	return findPlayerKey( keys, bare, letter );
}

/** How strongly the command button win, in command bar slot barSlot or -1, holds key, the map's
	* lower case name: 2 when it is the button's fixed letter (fixedButtonKey) and the player left the
	* button alone, 1 when the player gave it that letter in Hotkeys.ini, 0 for the label's or the
	* grid's.  With the grid on a bar button has no fixed letter. */
static Int keyClaim( GameWindow *win, const AsciiString& key, Int barSlot )
{
	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( win );
	AsciiString lower = command->getName();
	lower.toLower();
	WideChar letter = 0;
	if( findPlayerSlotKey( s_playerButtonKeys, std::string( lower.str() ), barSlot, &letter ) )
		return letter != 0 && HotKeyManager::nameOf( letter ) == key ? 1 : 0;
	if( s_commandBarGrid && barSlot >= 0 )
		return 0;
	const WideChar fixed = fixedButtonKey( lower, command->getCommandType() );
	return fixed != 0 && HotKeyManager::nameOf( fixed ) == key ? 2 : 0;
}

static Bool isOneOf( WideChar letter, const WideChar *letters, Int count )
{
	for( Int each = 0; each < count; each++ )
		if( letters[ each ] == letter )
			return TRUE;
	return FALSE;
}

//-----------------------------------------------------------------------------
WideChar HotKeyManager::playerButtonKey( const AsciiString& button, WideChar labelKey, Int commandType,
	Int barSlot, Int upgradeRank )
{
	return buttonKeyIn( button, labelKey, commandType, s_playerButtonKeys, barSlot, upgradeRank );
}

//-----------------------------------------------------------------------------
WideChar HotKeyManager::commandBarGridKey( Int barSlot, Int upgradeRank )
{
	if( barSlot < 0 || barSlot >= 2 * (Int)strlen( GRID_ROWS[ 0 ] ) )
		return 0;
	if( upgradeRank >= 0 && upgradeRank < (Int)strlen( GRID_UPGRADE_KEYS ) )
		return GRID_UPGRADE_KEYS[ upgradeRank ];
	return GRID_ROWS[ barSlot % 2 ][ barSlot / 2 ];
}

//-----------------------------------------------------------------------------
void HotKeyManager::commandBarUpgradeRanks( const Int *types, Int count, Int *ranks )
{
	Int upgrades = 0;
	for( Int slot = 0; slot < count; slot++ )
	{
		const Bool upgrade = types[ slot ] == GUI_COMMAND_PLAYER_UPGRADE || types[ slot ] == GUI_COMMAND_OBJECT_UPGRADE;
		ranks[ slot ] = upgrade ? upgrades++ : -1;
	}
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::commandBarGrid( void )
{
	return s_commandBarGrid;
}

void HotKeyManager::setCommandBarGrid( Bool grid )
{
	s_commandBarGrid = grid;
}

//-----------------------------------------------------------------------------
WideChar HotKeyManager::fixedKey( const AsciiString& button, Int commandType )
{
	AsciiString lower = button;
	lower.toLower();
	return fixedButtonKey( lower, commandType );
}

//-----------------------------------------------------------------------------
WideChar HotKeyManager::buttonKeyIn( const AsciiString& button, WideChar labelKey, Int commandType,
	const std::map< std::string, WideChar >& keys, Int barSlot, Int upgradeRank )
{
	// Force Attack is Ctrl.  The English label's F is not the key and may not bind the button.
	if( commandType == GUI_COMMAND_FORCE_ATTACK )
		return 0;
	AsciiString lower = button;
	lower.toLower();
	// the player's letter first, his for this slot before his for the button, a fixed order's
	// included: the fixed letter is only its default.  With the grid on, the bar's place is next
	WideChar letter = labelKey;
	const Bool chosen = findPlayerSlotKey( keys, std::string( lower.str() ), barSlot, &letter );
	if( !chosen && s_commandBarGrid && barSlot >= 0 )
	{
		const Bool upgrade = commandType == GUI_COMMAND_PLAYER_UPGRADE || commandType == GUI_COMMAND_OBJECT_UPGRADE;
		return commandBarGridKey( barSlot, upgrade ? upgradeRank : -1 );
	}
	if( !chosen )
	{
		const WideChar fixed = fixedButtonKey( lower, commandType );
		if( fixed != 0 )
			return fixed;
	}
	if( letter >= 'a' && letter <= 'z' )
		letter -= 'a' - 'A';
	// a label's own fixed letter gives way; one the player chose stays, and the bar's fixed order
	// still takes it first (addHotKey)
	if( !chosen && s_fixedOrderKeys && isOneOf( letter, FIXED_KEYS, (Int)( sizeof( FIXED_KEYS ) / sizeof( FIXED_KEYS[ 0 ] ) ) ) )
		return 0;
	return letter;
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::fixedOrderKeys( void )
{
	return s_fixedOrderKeys;
}

void HotKeyManager::setFixedOrderKeys( Bool fixed )
{
	s_fixedOrderKeys = fixed;
}

//-----------------------------------------------------------------------------
/** FixedOrderKeys, Enabled = Yes or No, End: whether the orders hold their fixed letters (FIXED_KEYS). */
/*static*/ void INI::parseFixedOrderKeysDefinition( INI *ini )
{
	static const FieldParse fields[] =
	{
		{ "Enabled", INI::parseBool, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};
	Bool enabled = TRUE;
	ini->initFromINI( &enabled, fields );
	s_fixedOrderKeys = enabled;
	DEBUG_LOG(( "HOTKEYS: fixed order keys %s\n", enabled ? "on" : "off" ));
}

//-----------------------------------------------------------------------------
/** CommandBarGrid, Enabled = Yes or No, End: whether command bar buttons take their window slot's
	* letter (s_commandBarGrid).  A launcher before 2026-10-15 wrote the same block for the place-based
	* grid it had then; that file now turns this one on. */
/*static*/ void INI::parseCommandBarGridDefinition( INI *ini )
{
	static const FieldParse fields[] =
	{
		{ "Enabled", INI::parseBool, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};
	Bool enabled = FALSE;
	ini->initFromINI( &enabled, fields );
	s_commandBarGrid = enabled;
	DEBUG_LOG(( "HOTKEYS: command bar grid %s\n", enabled ? "on" : "off" ));
}

//-----------------------------------------------------------------------------
/** CommandButtonHotKey <button name>, Key = one letter or digit, or NONE, End.  Only the player's own
	* Hotkeys.ini carries these, and nothing in it reaches the INI CRC. */
/*static*/ void INI::parseCommandButtonHotKeyDefinition( INI *ini )
{
	AsciiString name = ini->getNextToken();
	name.toLower();

	static const FieldParse fields[] =
	{
		{ "Key", INI::parseAsciiString, NULL, 0 },
		{ NULL, NULL, NULL, 0 }
	};
	AsciiString key;
	ini->initFromINI( &key, fields );

	WideChar letter = 0;
	if( key.getLength() == 1 && isalnum( (unsigned char)key.getCharAt( 0 ) ) )
		letter = (WideChar)toupper( (unsigned char)key.getCharAt( 0 ) );
	else if( key.compareNoCase( "NONE" ) != 0 )
		throw INI_INVALID_DATA;

	s_playerButtonKeys[ name.str() ] = letter;
	DEBUG_LOG(( "HOTKEYS: button %s key %s\n", name.str(), key.str() ));
}

//-----------------------------------------------------------------------------
void HotKeyManager::loadPlayerKeys( void )
{
	s_playerButtonKeys.clear();
	s_fixedOrderKeys = TRUE;
	s_commandBarGrid = FALSE;
	AsciiString hotkeys = TheGlobalData->getPath_UserData();
	hotkeys.concat( "Hotkeys.ini" );
	if( !TheFileSystem->doesFileExist( hotkeys.str() ) )
		return;
	INI ini;
	try
	{
		ini.load( hotkeys, INI_LOAD_MULTIFILE, NULL );
		DEBUG_LOG(( "HOTKEYS: read %s\n", hotkeys.str() ));
	}
	catch( ... )
	{
		DEBUG_LOG(( "HOTKEYS: %s stopped at a block it could not read; the keys before it stand\n", hotkeys.str() ));
	}
}

//-----------------------------------------------------------------------------
void HotKeyManager::reloadKeys( void )
{
	TheMetaMap->clear();
	AsciiString commandMap;
	commandMap.format( "Data\\%s\\CommandMap.ini", GetRegistryLanguage().str() );
	INI ini;
	ini.load( commandMap, INI_LOAD_OVERWRITE, NULL );
	TheMetaMap->keepArrowsForCamera();
#if defined(_DEBUG) || defined(_INTERNAL)
	ini.load( "Data\\INI\\CommandMapDebug.ini", INI_LOAD_MULTIFILE, NULL );
#endif
#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	ini.load( "Data\\INI\\CommandMapDemo.ini", INI_LOAD_MULTIFILE, NULL );
#endif
	loadPlayerKeys();
	// the bar's letters are given out again on its next update
	if( TheControlBar )
		TheControlBar->markUIDirty();
}

//-----------------------------------------------------------------------------
// Hotkeys.ini, the launcher's format
//-----------------------------------------------------------------------------
const char HOTKEYS_LAUNCHER_PART[] = "; the launcher's own keys from here on";

/// KEY_NAMES in hotkeys.js: the keys a command may take there, so what this writes the launcher reads.
static const char *const HOTKEY_KEY_NAMES[] =
{
	"KEY_ESC", "KEY_BACKSPACE", "KEY_ENTER", "KEY_SPACE", "KEY_TAB",
	"KEY_F1", "KEY_F2", "KEY_F3", "KEY_F4", "KEY_F5", "KEY_F6", "KEY_F7", "KEY_F8", "KEY_F9", "KEY_F10", "KEY_F11", "KEY_F12",
	"KEY_A", "KEY_B", "KEY_C", "KEY_D", "KEY_E", "KEY_F", "KEY_G", "KEY_H", "KEY_I", "KEY_J", "KEY_K", "KEY_L", "KEY_M",
	"KEY_N", "KEY_O", "KEY_P", "KEY_Q", "KEY_R", "KEY_S", "KEY_T", "KEY_U", "KEY_V", "KEY_W", "KEY_X", "KEY_Y", "KEY_Z",
	"KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5", "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_0",
	"KEY_KP1", "KEY_KP2", "KEY_KP3", "KEY_KP4", "KEY_KP5", "KEY_KP6", "KEY_KP7", "KEY_KP8", "KEY_KP9", "KEY_KP0",
	"KEY_KPPLUS", "KEY_KPMINUS", "KEY_KPSLASH", "KEY_MINUS", "KEY_EQUAL", "KEY_LBRACKET", "KEY_RBRACKET",
	"KEY_SEMICOLON", "KEY_APOSTROPHE", "KEY_TICK", "KEY_BACKSLASH", "KEY_COMMA", "KEY_PERIOD", "KEY_SLASH",
	"KEY_UP", "KEY_DOWN", "KEY_LEFT", "KEY_RIGHT", "KEY_HOME", "KEY_END", "KEY_PGUP", "KEY_PGDN", "KEY_INS", "KEY_DEL",
};

Bool Hotkeys_isKeyName( const std::string& key )
{
	for( size_t each = 0; each < sizeof( HOTKEY_KEY_NAMES ) / sizeof( HOTKEY_KEY_NAMES[ 0 ] ); each++ )
		if( key == HOTKEY_KEY_NAMES[ each ] )
			return TRUE;
	return FALSE;
}

static std::string lowered( std::string text )
{
	for( size_t at = 0; at < text.size(); at++ )
		text[ at ] = (char)tolower( (unsigned char)text[ at ] );
	return text;
}

static Bool startsWith( const std::string& text, const char *prefix )
{
	return text.compare( 0, strlen( prefix ), prefix ) == 0;
}

static std::string trimmed( const std::string& text )
{
	size_t first = text.find_first_not_of( " \t\r\n" );
	if( first == std::string::npos )
		return std::string();
	size_t last = text.find_last_not_of( " \t\r\n" );
	return text.substr( first, last - first + 1 );
}

/// The file's lines without their CR.
static std::vector< std::string > linesOf( const std::string& text )
{
	std::vector< std::string > lines;
	size_t start = 0;
	for( ;; )
	{
		size_t end = text.find( '\n', start );
		std::string line = text.substr( start, end == std::string::npos ? std::string::npos : end - start );
		if( !line.empty() && line[ line.size() - 1 ] == '\r' )
			line.erase( line.size() - 1 );
		lines.push_back( line );
		if( end == std::string::npos )
			return lines;
		start = end + 1;
	}
}

/** One top-level block of an INI text the way hotkeys.js parseIni reads it: "Type Name" up to its
	* End, comments after ; and // gone, fields as key and value. */
struct HotkeyIniBlock
{
	std::string type, name;
	std::vector< std::pair< std::string, std::string > > fields;
	std::string field( const char *key, const char *otherwise ) const
	{
		for( size_t each = 0; each < fields.size(); each++ )
			if( lowered( fields[ each ].first ) == lowered( key ) )
				return fields[ each ].second;
		return otherwise;
	}
	Bool hasField( const char *key ) const
	{
		for( size_t each = 0; each < fields.size(); each++ )
			if( lowered( fields[ each ].first ) == lowered( key ) )
				return TRUE;
		return FALSE;
	}
};

static std::vector< HotkeyIniBlock > iniBlocks( const std::string& text )
{
	std::vector< HotkeyIniBlock > blocks;
	HotkeyIniBlock block;
	Bool open = FALSE;
	Int depth = 0;
	std::vector< std::string > lines = linesOf( text );
	for( size_t each = 0; each < lines.size(); each++ )
	{
		std::string line = lines[ each ];
		size_t comment = line.find( ';' );
		if( comment != std::string::npos )
			line.erase( comment );
		comment = line.find( "//" );
		if( comment != std::string::npos )
			line.erase( comment );
		line = trimmed( line );
		if( line.empty() )
			continue;
		const std::string word = line.substr( 0, line.find_first_of( " \t=" ) );
		if( !open )
		{
			block = HotkeyIniBlock();
			block.type = word;
			std::string rest = trimmed( line.substr( word.size() ) );
			block.name = rest.substr( 0, rest.find_first_of( " \t" ) );
			open = TRUE;
			depth = 0;
			continue;
		}
		if( lowered( word ) == "end" )
		{
			if( depth == 0 )
			{
				blocks.push_back( block );
				open = FALSE;
			}
			else
				depth--;
		}
		else if( line.find_first_of( " \t=" ) == std::string::npos )
			depth++;	// a word alone opens a block inside, whose fields are not the block's
		else if( depth == 0 )
		{
			const size_t equals = line.find( '=' );
			if( equals == std::string::npos )
				block.fields.push_back( std::make_pair( word, trimmed( line.substr( word.size() ) ) ) );
			else
				block.fields.push_back( std::make_pair( trimmed( line.substr( 0, equals ) ), trimmed( line.substr( equals + 1 ) ) ) );
		}
	}
	return blocks;
}

static HotkeyCommandRow commandRow( const std::string& id, const std::string& category, const char *key = "", const char *modifiers = "NONE" )
{
	HotkeyCommandRow row;
	row.id = id;
	row.binding = HotkeyBinding( key, modifiers );
	row.usableIn = "GAME";
	row.category = category;
	row.barOnly = FALSE;
	row.hold = FALSE;
	row.launcherDefault = FALSE;
	return row;
}

std::vector< HotkeyCommandRow > Hotkeys_commandRows( const std::string& commandMapText )
{
	std::vector< HotkeyCommandRow > rows;
	std::vector< HotkeyIniBlock > blocks = iniBlocks( commandMapText );
	for( size_t each = 0; each < blocks.size(); each++ )
	{
		const HotkeyIniBlock& block = blocks[ each ];
		const std::string key = block.field( "Key", "" );
		if( block.type != "CommandMap" || startsWith( block.name, "DEMO_" ) || startsWith( block.name, "DEBUG_" )
				|| startsWith( block.name, "CHEAT_" ) || startsWith( block.name, "END_" ) || !Hotkeys_isKeyName( key ) )
			continue;
		// a bare arrow goes to Ctrl, as the game loads it (MetaMap::keepArrowsForCamera)
		std::string modifiers = block.field( "Modifiers", "NONE" );
		if( modifiers == "NONE" && ( key == "KEY_UP" || key == "KEY_DOWN" || key == "KEY_LEFT" || key == "KEY_RIGHT" ) )
			modifiers = "CTRL";
		HotkeyCommandRow row = commandRow( block.name, block.field( "Category", "MISC" ), key.c_str(), modifiers.c_str() );
		row.usableIn = block.field( "UseableIn", "GAME" );
		row.label = block.field( "DisplayName", "" );
		if( startsWith( block.name, "BEGIN_" ) )
			row.pair = "END_" + block.name.substr( 6 );
		rows.push_back( row );
	}

	// UNBOUND_COMMANDS in hotkeys.js: commands the engine knows that the shipped map binds to nothing
	std::vector< HotkeyCommandRow > unbound;
	static const char *const SELECTION[] = { "SELECT_ALL", "SELECT_ALL_MILITARY", "SELECT_ALL_VEHICLES", "SELECT_ALL_AIRCRAFT",
		"SELECT_ALL_BARRACKS", "SELECT_ALL_WARFACTORIES", "SELECT_ALL_AIRFIELDS", "SELECT_NEXT_IDLE_WORKER", "SELECT_NEXT_IDLE_UNIT",
		"SELECT_NEXT_IDLE_AIRCRAFT", "SELECT_NEXT_IDLE_VEHICLE", "SELECT_NEXT_IDLE_INFANTRY", "SELECT_ALL_IDLE_UNITS",
		"SELECT_ALL_IDLE_AIRCRAFT", "SELECT_ALL_IDLE_VEHICLES" };
	for( size_t each = 0; each < sizeof( SELECTION ) / sizeof( SELECTION[ 0 ] ); each++ )
		unbound.push_back( commandRow( SELECTION[ each ], "SELECTION" ) );
	unbound.push_back( commandRow( "TOGGLE_PAUSE", "INTERFACE" ) );
	unbound.push_back( commandRow( "TOGGLE_PURCHASE_SCIENCE", "INTERFACE" ) );
	unbound.push_back( commandRow( "SIGNAL_ATTACK", "INFORMATION" ) );
	unbound.push_back( commandRow( "SIGNAL_DEFEND", "INFORMATION" ) );
	unbound.push_back( commandRow( "SIGNAL_ATTENTION", "INFORMATION" ) );
	unbound.push_back( commandRow( "REQUEST_MONEY", "INFORMATION" ) );
	unbound.push_back( commandRow( "AUTO_GARRISON", "CONTROL" ) );
	unbound.push_back( commandRow( "AUTO_REPAIR", "CONTROL" ) );
	// the orders the bar's fixed places press (orderAtPlace in ControlBar.cpp). Their buttons take
	// a letter from the button's label, which the CommandSet names, so without these rows a set
	// whose guard button sat elsewhere or had no letter left guard with no key a player could set
	static const char *const ORDERS[] = { "TOGGLE_ATTACKMOVE", "TOGGLE_FORCEATTACK", "TOGGLE_GUARD", "TOGGLE_MOVE", "HOLD_POSITION" };
	for( size_t each = 0; each < sizeof( ORDERS ) / sizeof( ORDERS[ 0 ] ); each++ )
		unbound.push_back( commandRow( ORDERS[ each ], "CONTROL" ) );
	char name[ 32 ];
	for( Int slot = 1; slot <= 8; slot++ )
	{
		snprintf( name, sizeof( name ), "SHORTCUT_SLOT%02d", slot );
		unbound.push_back( commandRow( name, "SHORTCUTS" ) );
	}
	for( Int slot = 1; slot <= 18; slot++ )
	{
		snprintf( name, sizeof( name ), "COMMAND_SLOT%02d", slot );
		unbound.push_back( commandRow( name, "GRID" ) );
	}
	// LAUNCHER_DEFAULTS: I, free in the shipped map, for the next idle worker, and Alt with Z, X and C,
	// which nothing in the shipped map holds, for the signals to the allies. Request money has no key:
	// Alt+V is evacuate in place.
	static const char *const DEFAULTS[][ 3 ] = { { "SELECT_NEXT_IDLE_WORKER", "KEY_I", "NONE" },
		{ "SIGNAL_ATTACK", "KEY_Z", "ALT" }, { "SIGNAL_DEFEND", "KEY_X", "ALT" }, { "SIGNAL_ATTENTION", "KEY_C", "ALT" } };
	for( size_t each = 0; each < unbound.size(); each++ )
		for( size_t row = 0; row < sizeof( DEFAULTS ) / sizeof( DEFAULTS[ 0 ] ); row++ )
			if( unbound[ each ].id == DEFAULTS[ row ][ 0 ] )
			{
				unbound[ each ].binding = HotkeyBinding( DEFAULTS[ row ][ 1 ], DEFAULTS[ row ][ 2 ] );
				unbound[ each ].launcherDefault = TRUE;
			}

	// HOLD_COMMANDS: KEY_NONE and a modifier in the shipped file, which the rows above leave out.
	// Force Attack's moves on the launcher's page only, the other two on the options menu's as well.
	static const char *const HOLDS[][ 2 ] = { { "FORCEATTACK", "CTRL" }, { "WAYPOINTS", "ALT" }, { "PREFER_SELECTION", "SHIFT" } };
	for( size_t each = 0; each < sizeof( HOLDS ) / sizeof( HOLDS[ 0 ] ); each++ )
	{
		HotkeyCommandRow hold = commandRow( std::string( "BEGIN_" ) + HOLDS[ each ][ 0 ], "CONTROL", "", HOLDS[ each ][ 1 ] );
		hold.pair = std::string( "END_" ) + HOLDS[ each ][ 0 ];
		hold.barOnly = each == 0;
		hold.hold = TRUE;
		unbound.push_back( hold );
	}

	std::vector< HotkeyCommandRow > all = rows;
	for( size_t each = 0; each < unbound.size(); each++ )
	{
		Bool known = FALSE;
		for( size_t row = 0; row < rows.size(); row++ )
			known = known || rows[ row ].id == unbound[ each ].id;
		if( !known )
			all.push_back( unbound[ each ] );
	}
	return all;
}

HotkeysFile Hotkeys_parse( const std::string& text, const std::vector< HotkeyCommandRow >& rows )
{
	HotkeysFile file;
	const size_t part = text.find( HOTKEYS_LAUNCHER_PART );
	if( part != std::string::npos )
		file.launcherPart = text.substr( part + strlen( HOTKEYS_LAUNCHER_PART ) );
	std::vector< HotkeyIniBlock > blocks = iniBlocks( text.substr( 0, part ) );
	for( size_t each = 0; each < blocks.size(); each++ )
	{
		const HotkeyIniBlock& block = blocks[ each ];
		const std::string key = block.field( "Key", "" );
		if( block.type == "FixedOrderKeys" )
		{
			const std::string enabled = lowered( block.field( "Enabled", "yes" ) );
			file.fixedOrderKeys = !( enabled == "no" || enabled == "false" );
		}
		else if( block.type == "CommandBarGrid" )
			file.commandBarGrid = lowered( block.field( "Enabled", "no" ) ) == "yes";
		else if( block.type == "CommandButtonHotKey" )
		{
			const Bool letter = key.size() == 1 && isalnum( (unsigned char)key[ 0 ] );
			file.buttons[ block.name ] = letter ? std::string( 1, (char)toupper( (unsigned char)key[ 0 ] ) ) : std::string();
		}
		else if( block.type == "CommandMap" && !startsWith( block.name, "END_" ) )
		{
			const std::string usableIn = block.field( "UseableIn", "" );
			const std::string modifiers = block.field( "Modifiers", "NONE" );
			const Bool none = lowered( usableIn ) == "none";
			const Bool named = Hotkeys_isKeyName( key );
			const Bool modsOnly = lowered( key ) == "key_none" && !none && modifiers != "NONE";
			file.commands[ block.name ] = ( none || ( !named && !modsOnly ) )
				? HotkeyBinding( "", "NONE" ) : HotkeyBinding( named ? key : "", modifiers );
		}
	}
	// the launcher's own default, written for the game's sake, is not a change the player made
	for( size_t each = 0; each < rows.size(); each++ )
	{
		if( !rows[ each ].barOnly && !rows[ each ].launcherDefault )
			continue;
		std::map< std::string, HotkeyBinding >::iterator it = file.commands.find( rows[ each ].id );
		if( it != file.commands.end() && it->second == rows[ each ].binding )
			file.commands.erase( it );
	}
	return file;
}

/// The launcher's part without the blocks naming something in `names`, and the blank line after each.
static std::string launcherPartWithout( const std::string& part, const std::map< std::string, Bool >& names )
{
	if( part.empty() )
		return "\r\n";
	std::vector< std::string > lines = linesOf( part );
	std::vector< std::string > kept;
	for( size_t each = 0; each < lines.size(); each++ )
	{
		const std::string line = trimmed( lines[ each ] );
		const Bool header = startsWith( line, "CommandMap " ) || startsWith( line, "CommandButtonHotKey " );
		if( header && names.count( trimmed( line.substr( line.find( ' ' ) ) ) ) )
		{
			while( each < lines.size() && lowered( trimmed( lines[ each ] ) ) != "end" )
				each++;
			if( each + 1 < lines.size() && trimmed( lines[ each + 1 ] ).empty() )
				each++;
			continue;
		}
		kept.push_back( lines[ each ] );
	}
	std::string out;
	for( size_t each = 0; each < kept.size(); each++ )
		out += ( each > 0 ? "\r\n" : "" ) + kept[ each ];
	return out;
}

void Hotkeys_launcherKeys( const std::string& launcherPart, std::map< std::string, HotkeyBinding > *commands,
	std::map< std::string, std::string > *buttons )
{
	std::vector< HotkeyIniBlock > blocks = iniBlocks( launcherPart );
	for( size_t each = 0; each < blocks.size(); each++ )
	{
		const HotkeyIniBlock& block = blocks[ each ];
		const std::string key = block.field( "Key", "" );
		if( block.type == "CommandButtonHotKey" )
			( *buttons )[ block.name ] = ( key.size() == 1 && isalnum( (unsigned char)key[ 0 ] ) ) ? key : std::string();
		else if( block.type == "CommandMap" && !startsWith( block.name, "END_" ) )
			( *commands )[ block.name ] = lowered( block.field( "UseableIn", "" ) ) == "none"
				? HotkeyBinding( "", "NONE" ) : HotkeyBinding( Hotkeys_isKeyName( key ) ? key : "", block.field( "Modifiers", "NONE" ) );
	}
}

/** Where a character falls in String.localeCompare's root collation: blanks, then punctuation in its
	* own order, then digits, then letters with case set aside. */
static Int collationWeight( unsigned char c )
{
	static const char PUNCTUATION[] = "_-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$";
	if( isspace( c ) )
		return 1;
	const char *at = strchr( PUNCTUATION, c );
	if( c != 0 && at != NULL )
		return 10 + (Int)( at - PUNCTUATION );
	if( isdigit( c ) )
		return 100 + ( c - '0' );
	if( isalpha( c ) )
		return 200 + ( tolower( c ) - 'a' );
	return 300 + c;
}

bool Hotkeys_nameLess( const std::string& left, const std::string& right )
{
	const size_t common = left.size() < right.size() ? left.size() : right.size();
	for( size_t at = 0; at < common; at++ )
	{
		const Int l = collationWeight( (unsigned char)left[ at ] ), r = collationWeight( (unsigned char)right[ at ] );
		if( l != r )
			return l < r;
	}
	if( left.size() != right.size() )
		return left.size() < right.size();
	// the same letters: lower case first
	for( size_t at = 0; at < common; at++ )
		if( left[ at ] != right[ at ] )
			return islower( (unsigned char)left[ at ] ) != 0;
	return false;
}

template< class Map > static std::vector< std::string > sortedNames( const Map& map )
{
	std::vector< std::string > names;
	for( typename Map::const_iterator it = map.begin(); it != map.end(); ++it )
		names.push_back( it->first );
	std::sort( names.begin(), names.end(), Hotkeys_nameLess );
	return names;
}

std::string Hotkeys_format( const HotkeysFile& file, const std::vector< HotkeyCommandRow >& rows )
{
	std::string out =
		"; Zero Hour Reforged hotkeys, written by the launcher's Hotkeys page. Only what differs from the\r\n"
		"; game is here. The game reads it at start; it is not part of the multiplayer checksum.\r\n"
		"\r\n";
	if( !file.fixedOrderKeys )
		out += "FixedOrderKeys\r\n  Enabled = No\r\nEnd\r\n\r\n";
	if( file.commandBarGrid )
		out += "CommandBarGrid\r\n  Enabled = Yes\r\nEnd\r\n\r\n";
	std::map< std::string, Bool > named;
	std::vector< std::string > ids = sortedNames( file.commands );
	for( size_t each = 0; each < ids.size(); each++ )
	{
		const HotkeyBinding& binding = file.commands.find( ids[ each ] )->second;
		const HotkeyCommandRow *row = NULL;
		for( size_t candidate = 0; candidate < rows.size() && row == NULL; candidate++ )
			if( rows[ candidate ].id == ids[ each ] )
				row = &rows[ candidate ];
		const Bool chord = !binding.key.empty() || binding.modifiers != "NONE";
		for( Int half = 0; half < 2; half++ )
		{
			if( half == 1 && ( row == NULL || row->pair.empty() ) )
				break;
			const std::string& name = half == 0 ? ids[ each ] : row->pair;
			named[ name ] = TRUE;
			out += "CommandMap " + name + "\r\n";
			out += "  Key = " + ( binding.key.empty() ? std::string( "KEY_NONE" ) : binding.key ) + "\r\n";
			out += std::string( "  Transition = " ) + ( half == 0 ? "DOWN" : "UP" ) + "\r\n";
			out += "  Modifiers = " + ( chord ? binding.modifiers : std::string( "NONE" ) ) + "\r\n";
			out += "  UseableIn = " + ( chord ? ( row ? row->usableIn : std::string( "GAME" ) ) : std::string( "NONE" ) ) + "\r\n";
			out += "End\r\n\r\n";
		}
	}
	std::vector< std::string > buttons = sortedNames( file.buttons );
	for( size_t each = 0; each < buttons.size(); each++ )
	{
		const std::string& key = file.buttons.find( buttons[ each ] )->second;
		named[ buttons[ each ] ] = TRUE;
		out += "CommandButtonHotKey " + buttons[ each ] + "\r\n";
		out += "  Key = " + ( key.empty() ? std::string( "NONE" ) : key ) + "\r\n";
		out += "End\r\n\r\n";
	}
	out += HOTKEYS_LAUNCHER_PART;
	out += launcherPartWithout( file.launcherPart, named );
	return out;
}

std::string Hotkeys_chordText( const HotkeyBinding& binding )
{
	std::string text;
	const std::string& mods = binding.modifiers;
	if( mods == "CTRL" || mods == "CTRL_ALT" || mods == "SHIFT_CTRL" || mods == "SHIFT_ALT_CTRL" )
		text = "Ctrl";
	if( mods == "ALT" || mods == "CTRL_ALT" || mods == "SHIFT_ALT" || mods == "SHIFT_ALT_CTRL" )
		text += text.empty() ? "Alt" : " + Alt";
	if( mods == "SHIFT" || mods == "SHIFT_CTRL" || mods == "SHIFT_ALT" || mods == "SHIFT_ALT_CTRL" )
		text += text.empty() ? "Shift" : " + Shift";
	if( binding.key.empty() )
		return text;
	static const char *const WORDS[][ 2 ] =
	{
		{ "ESC", "Esc" }, { "BACKSPACE", "Backspace" }, { "ENTER", "Enter" }, { "SPACE", "Space" }, { "TAB", "Tab" },
		{ "LBRACKET", "[" }, { "RBRACKET", "]" }, { "SEMICOLON", ";" }, { "APOSTROPHE", "'" }, { "TICK", "`" },
		{ "BACKSLASH", "\\" }, { "COMMA", "," }, { "PERIOD", "." }, { "SLASH", "/" }, { "MINUS", "-" }, { "EQUAL", "=" },
		{ "UP", "Up" }, { "DOWN", "Down" }, { "LEFT", "Left" }, { "RIGHT", "Right" }, { "HOME", "Home" }, { "END", "End" },
		{ "PGUP", "Page Up" }, { "PGDN", "Page Down" }, { "INS", "Insert" }, { "DEL", "Delete" },
		{ "KPPLUS", "Num +" }, { "KPMINUS", "Num -" }, { "KPSLASH", "Num /" },
	};
	std::string name = startsWith( binding.key, "KEY_" ) ? binding.key.substr( 4 ) : binding.key;
	std::string word = name;
	for( size_t each = 0; each < sizeof( WORDS ) / sizeof( WORDS[ 0 ] ); each++ )
		if( name == WORDS[ each ][ 0 ] )
			word = WORDS[ each ][ 1 ];
	if( name.size() == 3 && startsWith( name, "KP" ) && isdigit( (unsigned char)name[ 2 ] ) )
		word = std::string( "Num " ) + name[ 2 ];
	return text.empty() ? word : text + " + " + word;
}

//-----------------------------------------------------------------------------
HotKeyManager *TheHotKeyManager = NULL;

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

