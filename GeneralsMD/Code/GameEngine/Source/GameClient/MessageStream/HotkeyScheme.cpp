// The keyboard schemes' table (GameClient/HotkeyScheme.h, docs/plans/HOTKEY-SCHEME-RA.md).
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/HotkeyScheme.h"
#include "GameClient/HotKey.h"
#include "Common/AudioEventRTS.h"
#include "Common/GameAudio.h"
#include "Common/NameKeyGenerator.h"
#include "Common/OptionsCatalog.h"
#include "GameClient/ControlBar.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"

/// What Red Alert changes over Classic.  A and G stay the bar's, S stop and X scatter stay, Alt held
/// stays waypoints and Z held joins it; F is left empty.
static const HotkeySchemeKey RED_ALERT_KEYS[] =
{
	// the four production tabs on the bare letters, Ctrl taken off
	{ "TOGGLE_COMMAND_TAB_BUILDINGS",	"KEY_Q",	"NONE",		TRUE },
	{ "TOGGLE_COMMAND_TAB_GROUND",		"KEY_W",	"NONE",		TRUE },
	{ "TOGGLE_COMMAND_TAB_AIRCRAFT",	"KEY_E",	"NONE",		TRUE },
	{ "TOGGLE_COMMAND_TAB_DEFENSE",		"KEY_R",	"NONE",		TRUE },
	{ "REPAIR_MODE",									"KEY_K",	"NONE",		TRUE },
	{ "SELL_MODE",										"KEY_L",	"NONE",		TRUE },
	{ "DEPLOY",												"KEY_D",	"NONE",		TRUE },
	{ "SELECT_MATCHING_UNITS",				"KEY_T",	"NONE",		TRUE },
	{ "SELECT_ALL",										"KEY_P",	"NONE",		TRUE },
	{ "SELECT_NEXT_UNIT",							"KEY_N",	"NONE",		TRUE },
	{ "SELECT_PREV_UNIT",							"KEY_M",	"NONE",		TRUE },
	{ "BEGIN_WAYPOINTS_KEY",					"KEY_Z",	"NONE",		TRUE },
	// the taunt keys: cheer and the three signals to the allies
	{ "ALL_CHEER",										"KEY_F5",	"NONE",		TRUE },
	{ "SIGNAL_ATTACK",								"KEY_F6",	"NONE",		TRUE },
	{ "SIGNAL_DEFEND",								"KEY_F7",	"NONE",		TRUE },
	{ "SIGNAL_ATTENTION",							"KEY_F8",	"NONE",		TRUE },
	{ "PLACE_BEACON",									"KEY_B",	"NONE",		TRUE },
	// bookmarks 5-8 give F5-F8 to the taunts; select all aircraft gives W to a tab
	{ "VIEW_VIEW5",										"",				"NONE",		FALSE },
	{ "VIEW_VIEW6",										"",				"NONE",		FALSE },
	{ "VIEW_VIEW7",										"",				"NONE",		FALSE },
	{ "VIEW_VIEW8",										"",				"NONE",		FALSE },
	{ "SAVE_VIEW5",										"",				"NONE",		FALSE },
	{ "SAVE_VIEW6",										"",				"NONE",		FALSE },
	{ "SAVE_VIEW7",										"",				"NONE",		FALSE },
	{ "SAVE_VIEW8",										"",				"NONE",		FALSE },
	{ "SELECT_ALL_AIRCRAFT",					"",				"NONE",		FALSE },
	// the strip's cells by place, on the space Alt+Z/X/C left
	{ "COMMAND_SLOT01",								"KEY_Q",	"ALT",		FALSE },
	{ "COMMAND_SLOT02",								"KEY_W",	"ALT",		FALSE },
	{ "COMMAND_SLOT03",								"KEY_E",	"ALT",		FALSE },
	{ "COMMAND_SLOT04",								"KEY_R",	"ALT",		FALSE },
	{ "COMMAND_SLOT05",								"KEY_T",	"ALT",		FALSE },
	{ "COMMAND_SLOT06",								"KEY_Y",	"ALT",		FALSE },
	{ "COMMAND_SLOT07",								"KEY_U",	"ALT",		FALSE },
	{ "COMMAND_SLOT08",								"KEY_I",	"ALT",		FALSE },
	{ "COMMAND_SLOT09",								"KEY_O",	"ALT",		FALSE },
	{ "COMMAND_SLOT10",								"KEY_P",	"ALT",		FALSE },
	{ "COMMAND_SLOT11",								"KEY_A",	"ALT",		FALSE },
	{ "COMMAND_SLOT12",								"KEY_S",	"ALT",		FALSE },
	{ "COMMAND_SLOT13",								"KEY_D",	"ALT",		FALSE },
	{ "COMMAND_SLOT14",								"KEY_F",	"ALT",		FALSE },
	// the general's powers
	{ "SHORTCUT_SLOT01",							"KEY_F1",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT02",							"KEY_F2",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT03",							"KEY_F3",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT04",							"KEY_F4",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT05",							"KEY_F5",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT06",							"KEY_F6",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT07",							"KEY_F7",	"SHIFT",	FALSE },
	{ "SHORTCUT_SLOT08",							"KEY_F8",	"SHIFT",	FALSE },
	{ "TOGGLE_PURCHASE_SCIENCE",			"KEY_G",	"CTRL",		FALSE },
};

//-------------------------------------------------------------------------------------------------
const HotkeySchemeKey *HotkeyScheme_keys( Int scheme, Int *count )
{
	if( scheme == HOTKEY_SCHEME_RED_ALERT )
	{
		*count = (Int)( sizeof( RED_ALERT_KEYS ) / sizeof( RED_ALERT_KEYS[ 0 ] ) );
		return RED_ALERT_KEYS;
	}
	*count = 0;
	return NULL;
}

//-------------------------------------------------------------------------------------------------
const char *HotkeyScheme_fileName( Int scheme )
{
	return scheme == HOTKEY_SCHEME_RED_ALERT ? "HotkeysRedAlert.ini" : "Hotkeys.ini";
}

//-------------------------------------------------------------------------------------------------
const char *HotkeyScheme_fixedLetters( Int scheme )
{
	return scheme == HOTKEY_SCHEME_RED_ALERT ? "ASGYVLCH" : "ASGYVNCH";
}

//-------------------------------------------------------------------------------------------------
void HotkeyScheme_applyToRows( std::vector< HotkeyCommandRow > &rows, Int scheme )
{
	Int count = 0;
	const HotkeySchemeKey *keys = HotkeyScheme_keys( scheme, &count );
	if( keys == NULL )
		return;
	// the scheme's file has no launcher part, so no row's key is the launcher's
	for( size_t each = 0; each < rows.size(); each++ )
		rows[ each ].launcherDefault = FALSE;

	std::vector< Bool > inTable( rows.size(), FALSE );
	for( Int key = 0; key < count; key++ )
	{
		size_t at = 0;
		while( at < rows.size() && rows[ at ].id != keys[ key ].command )
			at++;
		if( at == rows.size() )
		{
			// a command no language file binds: Deploy, the sidebar's sell and repair, the Z hold
			HotkeyCommandRow row;
			row.id = keys[ key ].command;
			row.usableIn = "GAME";
			const Bool begin = row.id.compare( 0, 6, "BEGIN_" ) == 0;
			row.category = ( begin || row.id == "DEPLOY" ) ? "CONTROL" : "INTERFACE";
			if( begin )
				row.pair = "END_" + row.id.substr( 6 );
			row.barOnly = FALSE;
			row.hold = FALSE;
			row.launcherDefault = FALSE;
			rows.push_back( row );
			inTable.push_back( FALSE );
		}
		rows[ at ].binding = keys[ key ].key[ 0 ] ? HotkeyBinding( keys[ key ].key, keys[ key ].modifiers ) : HotkeyBinding();
		inTable[ at ] = TRUE;
	}

	// a command left on a chord the table hands out loses it, as MetaMap::applyScheme frees its record
	for( size_t each = 0; each < rows.size(); each++ )
	{
		if( inTable[ each ] || rows[ each ].binding.key.empty() )
			continue;
		for( Int key = 0; key < count; key++ )
			if( keys[ key ].key[ 0 ] && rows[ each ].binding == HotkeyBinding( keys[ key ].key, keys[ key ].modifiers ) )
			{
				rows[ each ].binding = HotkeyBinding();
				break;
			}
	}
}

//-------------------------------------------------------------------------------------------------
/// What Deploy presses: the selection's own command, never an order every unit shares or a build button.
static Bool isOwnCommand( Int type )
{
	switch( type )
	{
		case GUI_COMMAND_SWITCH_WEAPON:
		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_SPECIAL_POWER_CONSTRUCT:
		case GUI_COMMAND_FIRE_WEAPON:
		case GUI_COMMAND_HACK_INTERNET:
		case GUI_COMMAND_TOGGLE_OVERCHARGE:
#ifdef ALLOW_SURRENDER
		case GUI_COMMAND_POW_RETURN_TO_PRISON:
#endif
		case GUI_COMMAND_COMBATDROP:
		case GUI_COMMAND_EXECUTE_RAILED_TRANSPORT:
		case GUI_COMMAND_EVACUATE:
			return TRUE;
	}
	return FALSE;
}

Int HotkeyScheme_firstOwnSlot( const Int *types, const Bool *pressable, Int count )
{
	for( Int slot = 0; slot < count; slot++ )
		if( pressable[ slot ] && isOwnCommand( types[ slot ] ) )
			return slot;
	return -1;
}

//-------------------------------------------------------------------------------------------------
Bool HotkeyScheme_pressDeploy( void )
{
	if( TheWindowManager == NULL || TheNameKeyGenerator == NULL )
		return FALSE;
	GameWindow *windows[ MAX_COMMANDS_PER_SET ];
	Int types[ MAX_COMMANDS_PER_SET ];
	Bool pressable[ MAX_COMMANDS_PER_SET ];
	AsciiString name;
	for( Int slot = 0; slot < MAX_COMMANDS_PER_SET; slot++ )
	{
		name.format( "ControlBar.wnd:ButtonCommand%02d", slot + 1 );
		GameWindow *win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( name.str() ) );
		const CommandButton *command = win ? (const CommandButton *)GadgetButtonGetData( win ) : NULL;
		windows[ slot ] = win;
		types[ slot ] = command ? command->getCommandType() : GUI_COMMAND_NONE;
		// showing: neither the window nor anything it stands in is hidden (the bar's command pane)
		Bool showing = command != NULL && BitTest( win->winGetStatus(), WIN_STATUS_ENABLED );
		for( GameWindow *up = win; showing && up; up = up->winGetParent() )
			showing = !up->winIsHidden();
		pressable[ slot ] = showing;
	}
	const Int slot = HotkeyScheme_firstOwnSlot( types, pressable, MAX_COMMANDS_PER_SET );
	if( slot < 0 )
	{
		DEBUG_LOG(( "HOTKEYS: deploy found no command of the selection's own to press\n" ));
		return FALSE;
	}
	GameWindow *win = windows[ slot ];
	const CommandButton *command = (const CommandButton *)GadgetButtonGetData( win );
	DEBUG_LOG(( "HOTKEYS: deploy pressed %s in slot %d\n", command->getName().str(), slot + 1 ));
	// the press executeHotKey sends, and its click
	TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );
	AudioEventRTS buttonClick( "GUIClick" );
	if( TheAudio )
		TheAudio->addAudioEvent( &buttonClick );
	return TRUE;
}
