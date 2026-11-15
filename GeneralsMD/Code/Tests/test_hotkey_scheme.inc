// The keyboard schemes' tables, files and bar letters (GameClient/HotkeyScheme.h); lane B of
// docs/plans/HOTKEY-SCHEME-RA.md fills it.

#include "GameClient/HotkeyScheme.h"

/* HotkeyScheme.cpp: the Red Alert table gives each command one chord and each chord one command, or
   one key press would fire two things.  Classic has no table at all. */
TEST(hotkey_scheme_red_alert_table_is_one_command_a_chord)
{
	Int count = -1;
	CHECK( HotkeyScheme_keys( HOTKEY_SCHEME_CLASSIC, &count ) == NULL );
	CHECK_EQ( count, 0 );
	const HotkeySchemeKey *keys = HotkeyScheme_keys( HOTKEY_SCHEME_RED_ALERT, &count );
	CHECK( keys != NULL && count > 0 );
	for( Int one = 0; keys && one < count; one++ )
	{
		CHECK( keys[ one ].key[ 0 ] == 0 || Hotkeys_isKeyName( keys[ one ].key ) );
		for( Int other = one + 1; other < count; other++ )
		{
			CHECK( strcmp( keys[ one ].command, keys[ other ].command ) != 0 );
			CHECK( keys[ one ].key[ 0 ] == 0 || strcmp( keys[ one ].key, keys[ other ].key ) != 0
				|| strcmp( keys[ one ].modifiers, keys[ other ].modifiers ) != 0 );
		}
	}
}

/* The Hotkeys page shows Red Alert's defaults as that scheme's game keys: the tabs bare, the commands
   no language file binds (deploy, sell, repair, the Z hold) as rows, a Classic key on a Red Alert chord
   gone, and none of the launcher's defaults, since that file has no launcher part. */
TEST(hotkey_scheme_rows_show_red_alert_defaults)
{
	static const char MAP[] =
		"CommandMap SELECT_MATCHING_UNITS\r\n  Key = KEY_E\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap SELECT_NEXT_WORKER\r\n  Key = KEY_T\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap VIEW_COMMAND_CENTER\r\n  Key = KEY_H\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n"
		"CommandMap SELECT_ALL_AIRCRAFT\r\n  Key = KEY_W\r\n  Modifiers = NONE\r\n  UseableIn = GAME\r\n  Category = SELECTION\r\nEnd\r\n";
	const std::vector< HotkeyCommandRow > classic = Hotkeys_commandRows( MAP );
	std::vector< HotkeyCommandRow > rows = classic;
	HotkeyScheme_applyToRows( rows, HOTKEY_SCHEME_CLASSIC );
	CHECK_EQ( (Int)rows.size(), (Int)classic.size() );
	for( size_t each = 0; each < rows.size() && each < classic.size(); each++ )
		CHECK( rows[ each ].binding == classic[ each ].binding && rows[ each ].launcherDefault == classic[ each ].launcherDefault );

	HotkeyScheme_applyToRows( rows, HOTKEY_SCHEME_RED_ALERT );
	struct Want { const char *id; const char *key; const char *modifiers; };
	static const Want WANTS[] =
	{
		{ "TOGGLE_COMMAND_TAB_BUILDINGS", "KEY_Q", "NONE" }, { "TOGGLE_COMMAND_TAB_DEFENSE", "KEY_R", "NONE" },
		{ "SELECT_MATCHING_UNITS", "KEY_T", "NONE" }, { "SELECT_NEXT_WORKER", "", "NONE" },
		{ "VIEW_COMMAND_CENTER", "KEY_H", "NONE" }, { "SELECT_ALL_AIRCRAFT", "", "NONE" },
		{ "SIGNAL_ATTACK", "KEY_F6", "NONE" }, { "DEPLOY", "KEY_D", "NONE" }, { "SELL_MODE", "KEY_L", "NONE" },
		{ "REPAIR_MODE", "KEY_K", "NONE" }, { "BEGIN_WAYPOINTS_KEY", "KEY_Z", "NONE" },
		{ "COMMAND_SLOT01", "KEY_Q", "ALT" }, { "SHORTCUT_SLOT08", "KEY_F8", "SHIFT" },
	};
	for( size_t want = 0; want < sizeof( WANTS ) / sizeof( WANTS[ 0 ] ); want++ )
	{
		const HotkeyCommandRow *row = NULL;
		for( size_t each = 0; each < rows.size() && row == NULL; each++ )
			row = rows[ each ].id == WANTS[ want ].id ? &rows[ each ] : NULL;
		CHECK( row != NULL );
		if( row != NULL )
			CHECK( row->binding == HotkeyBinding( WANTS[ want ].key, WANTS[ want ].modifiers ) );
	}
	Bool anyLauncher = FALSE;
	const HotkeyCommandRow *zHold = NULL;
	for( size_t each = 0; each < rows.size(); each++ )
	{
		anyLauncher = anyLauncher || rows[ each ].launcherDefault;
		zHold = rows[ each ].id == "BEGIN_WAYPOINTS_KEY" ? &rows[ each ] : zHold;
	}
	CHECK( !anyLauncher );
	CHECK( zHold != NULL && zHold->pair == "END_WAYPOINTS_KEY" && zHold->usableIn == "GAME" && !zHold->hold );
}

/* Classic keeps Hotkeys.ini, which the launcher reads byte for byte; Red Alert has its own file. */
TEST(hotkey_scheme_file_names_differ)
{
	CHECK_STR( HotkeyScheme_fileName( HOTKEY_SCHEME_CLASSIC ), "Hotkeys.ini" );
	CHECK_STR( HotkeyScheme_fileName( HOTKEY_SCHEME_RED_ALERT ), "HotkeysRedAlert.ini" );
	CHECK_STR( HotkeyScheme_fixedLetters( HOTKEY_SCHEME_CLASSIC ), "ASGYVNCH" );
	CHECK_STR( HotkeyScheme_fixedLetters( HOTKEY_SCHEME_RED_ALERT ), "ASGYVLCH" );
}

/* A Red Alert file the page writes reads back as written, the Z hold's release half included, and a
   key equal to the scheme's default is no change. */
TEST(hotkey_scheme_red_alert_file_round_trips)
{
	std::vector< HotkeyCommandRow > rows = Hotkeys_commandRows( HOTKEYS_COMMAND_MAP );
	HotkeyScheme_applyToRows( rows, HOTKEY_SCHEME_RED_ALERT );
	HotkeysFile file;
	file.commands[ "DEPLOY" ] = HotkeyBinding( "KEY_J", "NONE" );
	file.commands[ "BEGIN_WAYPOINTS_KEY" ] = HotkeyBinding( "KEY_X", "CTRL" );
	file.commands[ "SELL_MODE" ] = HotkeyBinding( "", "NONE" );
	file.buttons[ "Command_Sell" ] = "U";
	const std::string text = Hotkeys_format( file, rows );
	CHECK( text.find( "CommandMap END_WAYPOINTS_KEY\r\n  Key = KEY_X\r\n  Transition = UP\r\n" ) != std::string::npos );
	const HotkeysFile again = Hotkeys_parse( text, rows );
	CHECK( again.commands.size() == file.commands.size() );
	CHECK( again.commands.count( "DEPLOY" ) && again.commands.find( "DEPLOY" )->second == HotkeyBinding( "KEY_J", "NONE" ) );
	CHECK( again.commands.count( "BEGIN_WAYPOINTS_KEY" ) && again.commands.find( "BEGIN_WAYPOINTS_KEY" )->second == HotkeyBinding( "KEY_X", "CTRL" ) );
	CHECK( again.commands.count( "SELL_MODE" ) && again.commands.find( "SELL_MODE" )->second == HotkeyBinding() );
	CHECK( again.buttons.count( "Command_Sell" ) && again.buttons.find( "Command_Sell" )->second == "U" );
	CHECK( Hotkeys_format( again, rows ) == text );
}

/* Deploy presses the selection's own command: the first showing, enabled one in slot order, past the
   build buttons and the orders every unit shares. */
TEST(hotkey_scheme_deploy_takes_first_own_command)
{
	const Int types[] = { GUI_COMMAND_UNIT_BUILD, GUI_COMMAND_ATTACK_MOVE, GUI_COMMAND_SPECIAL_POWER,
		GUI_COMMAND_FIRE_WEAPON, GUI_COMMAND_EVACUATE, GUI_COMMAND_NONE };
	Bool pressable[] = { TRUE, TRUE, FALSE, TRUE, TRUE, TRUE };
	CHECK_EQ( HotkeyScheme_firstOwnSlot( types, pressable, 6 ), 3 );
	pressable[ 2 ] = TRUE;
	CHECK_EQ( HotkeyScheme_firstOwnSlot( types, pressable, 6 ), 2 );
	CHECK_EQ( HotkeyScheme_firstOwnSlot( types, pressable, 2 ), -1 );
	const Int others[] = { GUI_COMMAND_STOP, GUI_COMMAND_GUARD, GUI_COMMAND_SELL, GUI_COMMAND_PLAYER_UPGRADE };
	const Bool all[] = { TRUE, TRUE, TRUE, TRUE };
	CHECK_EQ( HotkeyScheme_firstOwnSlot( others, all, 4 ), -1 );
	const Int own[] = { GUI_COMMAND_SWITCH_WEAPON, GUI_COMMAND_SPECIAL_POWER_CONSTRUCT, GUI_COMMAND_HACK_INTERNET,
		GUI_COMMAND_TOGGLE_OVERCHARGE, GUI_COMMAND_COMBATDROP, GUI_COMMAND_EXECUTE_RAILED_TRANSPORT };
	for( Int each = 0; each < (Int)( sizeof( own ) / sizeof( own[ 0 ] ) ); each++ )
		CHECK_EQ( HotkeyScheme_firstOwnSlot( own + each, all, 1 ), 0 );
}

/* Under Red Alert a label's letter the player did not choose gives way when the table binds it bare
   (D deploy, Q the tab, N next unit) or it is a fixed letter (L sell); his own D stays, and Classic is
   as it was.  Sell's fixed letter is L there and N in Classic. */
TEST(hotkey_scheme_label_gives_way)
{
	const AsciiString build( "Command_ConstructAmericaVehicleHumvee" );
	const std::map< std::string, WideChar > none;
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'D', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'q', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'N', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'L', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), 0 );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'J', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'J' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'D', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_CLASSIC ), (WideChar)'D' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'L', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_CLASSIC ), (WideChar)'L' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'N', GUI_COMMAND_UNIT_BUILD, none, -1, -1, HOTKEY_SCHEME_CLASSIC ), 0 );
	std::map< std::string, WideChar > mine;
	mine[ "command_constructamericavehiclehumvee" ] = 'D';
	CHECK_EQ( HotKeyManager::buttonKeyIn( build, 'V', GUI_COMMAND_UNIT_BUILD, mine, -1, -1, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'D' );

	const AsciiString sell( "Command_Sell" );
	CHECK_EQ( HotKeyManager::buttonKeyIn( sell, 'S', GUI_COMMAND_SELL, none, -1, -1, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'L' );
	CHECK_EQ( HotKeyManager::buttonKeyIn( sell, 'S', GUI_COMMAND_SELL, none, -1, -1, HOTKEY_SCHEME_CLASSIC ), (WideChar)'N' );
	CHECK_EQ( HotKeyManager::fixedKey( sell, GUI_COMMAND_SELL, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'L' );
	CHECK_EQ( HotKeyManager::fixedKey( sell, GUI_COMMAND_SELL, HOTKEY_SCHEME_CLASSIC ), (WideChar)'N' );
	CHECK_EQ( HotKeyManager::fixedKey( AsciiString( "Command_Stop" ), GUI_COMMAND_STOP, HOTKEY_SCHEME_RED_ALERT ), (WideChar)'S' );
}
