// MetaMap::applyScheme and the scheme's new commands (MetaEvent.cpp, CommandXlat.cpp); lane C of
// docs/plans/HOTKEY-SCHEME-RA.md.

static MetaMapRec *schemeBind( MetaRecProbe &map, GameMessage::Type meta, MappableKeyType key, MappableKeyModState mods,
	MappableKeyTransition transition = DOWN )
{
	MetaMapRec *rec = map.getMetaMapRec( meta );
	rec->m_key = key;
	rec->m_modState = mods;
	rec->m_transition = transition;
	rec->m_usableIn = COMMANDUSABLE_GAME;
	return rec;
}

static void schemeClassicKeys( MetaRecProbe &map )
{
	schemeBind( map, GameMessage::MSG_META_SELECT_ALL, MK_Q, NONE );
	schemeBind( map, GameMessage::MSG_META_SELECT_MATCHING_UNITS, MK_E, NONE );
	schemeBind( map, GameMessage::MSG_META_SELECT_ALL_AIRCRAFT, MK_W, NONE );
	schemeBind( map, GameMessage::MSG_META_TOGGLE_COMMAND_TAB_BUILDINGS, MK_Q, CTRL );
	schemeBind( map, GameMessage::MSG_META_SIGNAL_ATTACK, MK_Z, ALT );
	schemeBind( map, GameMessage::MSG_META_VIEW_VIEW5, MK_F5, NONE );
	schemeBind( map, GameMessage::MSG_META_SCATTER, MK_X, NONE );
	schemeBind( map, GameMessage::MSG_META_BEGIN_FORCEATTACK, MK_NONE, CTRL );
	schemeBind( map, GameMessage::MSG_META_BEGIN_WAYPOINTS, MK_NONE, ALT );
	schemeBind( map, GameMessage::MSG_META_END_WAYPOINTS, MK_NONE, ALT, UP );
}

TEST(hotkey_scheme_red_alert_moves_and_frees_classic_chords)
{
	MetaRecProbe map;
	schemeClassicKeys( map );
	// a language file's command on L, Red Alert's sell; and one on K's release, which the repair press leaves
	MetaMapRec *onL = schemeBind( map, GameMessage::MSG_META_TOGGLE_LOWER_DETAILS, MK_L, NONE );
	MetaMapRec *onKUp = schemeBind( map, GameMessage::MSG_META_TOGGLE_CONTROL_BAR, MK_K, NONE, UP );
	map.applyScheme( HOTKEY_SCHEME_RED_ALERT );

	// moved: the tab to bare Q, select all to P, select same type to T, the signal to F6
	const MetaMapRec *tab = map.getMetaMapRec( GameMessage::MSG_META_TOGGLE_COMMAND_TAB_BUILDINGS );
	CHECK_EQ( (Int)tab->m_key, (Int)MK_Q );
	CHECK_EQ( (Int)tab->m_modState, (Int)NONE );
	CHECK_EQ( (Int)tab->m_usableIn, (Int)COMMANDUSABLE_GAME );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SELECT_ALL )->m_key, (Int)MK_P );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SELECT_MATCHING_UNITS )->m_key, (Int)MK_T );
	const MetaMapRec *signal = map.getMetaMapRec( GameMessage::MSG_META_SIGNAL_ATTACK );
	CHECK_EQ( (Int)signal->m_key, (Int)MK_F6 );
	CHECK_EQ( (Int)signal->m_modState, (Int)NONE );

	// the new commands land on their keys, and the strip slots on Alt
	const MetaMapRec *sell = map.getMetaMapRec( GameMessage::MSG_META_SELL_MODE );
	CHECK_EQ( (Int)sell->m_key, (Int)MK_L );
	CHECK_EQ( (Int)sell->m_usableIn, (Int)COMMANDUSABLE_GAME );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_REPAIR_MODE )->m_key, (Int)MK_K );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_DEPLOY )->m_key, (Int)MK_D );
	const MetaMapRec *slot1 = map.getMetaMapRec( GameMessage::MSG_META_COMMAND_SLOT01 );
	CHECK_EQ( (Int)slot1->m_key, (Int)MK_Q );
	CHECK_EQ( (Int)slot1->m_modState, (Int)ALT );

	// unbound by the table: bookmark 5 (F5 is the cheer's) and select all aircraft (W is a tab's)
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_VIEW_VIEW5 )->m_usableIn, (Int)COMMANDUSABLE_NONE );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SELECT_ALL_AIRCRAFT )->m_usableIn, (Int)COMMANDUSABLE_NONE );

	// freed: whatever else sat on a table chord
	CHECK_EQ( (Int)onL->m_key, (Int)MK_NONE );
	CHECK_EQ( (Int)onL->m_usableIn, (Int)COMMANDUSABLE_NONE );

	// kept: another transition, another key, and the bare modifier holds
	CHECK_EQ( (Int)onKUp->m_key, (Int)MK_K );
	CHECK_EQ( (Int)onKUp->m_usableIn, (Int)COMMANDUSABLE_GAME );
	const MetaMapRec *scatter = map.getMetaMapRec( GameMessage::MSG_META_SCATTER );
	CHECK_EQ( (Int)scatter->m_key, (Int)MK_X );
	CHECK_EQ( (Int)scatter->m_usableIn, (Int)COMMANDUSABLE_GAME );
	const MetaMapRec *force = map.getMetaMapRec( GameMessage::MSG_META_BEGIN_FORCEATTACK );
	CHECK_EQ( (Int)force->m_modState, (Int)CTRL );
	CHECK_EQ( (Int)force->m_usableIn, (Int)COMMANDUSABLE_GAME );

	// the table's last rows free their chords too: Ctrl+G (promotion screen), Shift+F8 (power 8)
	MetaRecProbe late;
	MetaMapRec *onCtrlG = schemeBind( late, GameMessage::MSG_META_SCATTER, MK_G, CTRL );
	MetaMapRec *onShiftF8 = schemeBind( late, GameMessage::MSG_META_CREATE_FORMATION, MK_F8, SHIFT );
	late.applyScheme( HOTKEY_SCHEME_RED_ALERT );
	CHECK_EQ( (Int)onCtrlG->m_usableIn, (Int)COMMANDUSABLE_NONE );
	CHECK_EQ( (Int)onCtrlG->m_key, (Int)MK_NONE );
	CHECK_EQ( (Int)onShiftF8->m_usableIn, (Int)COMMANDUSABLE_NONE );
	const MetaMapRec *science = late.getMetaMapRec( GameMessage::MSG_META_TOGGLE_PURCHASE_SCIENCE );
	CHECK_EQ( (Int)science->m_key, (Int)MK_G );
	CHECK_EQ( (Int)science->m_modState, (Int)CTRL );

	// a second pass changes nothing (reloadKeys over records already moved)
	map.applyScheme( HOTKEY_SCHEME_RED_ALERT );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SELECT_ALL )->m_key, (Int)MK_P );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_TOGGLE_COMMAND_TAB_BUILDINGS )->m_usableIn, (Int)COMMANDUSABLE_GAME );
}

TEST(hotkey_scheme_classic_leaves_the_records_alone)
{
	MetaRecProbe map;
	schemeClassicKeys( map );
	map.applyScheme( HOTKEY_SCHEME_CLASSIC );
	const MetaMapRec *all = map.getMetaMapRec( GameMessage::MSG_META_SELECT_ALL );
	CHECK_EQ( (Int)all->m_key, (Int)MK_Q );
	CHECK_EQ( (Int)all->m_usableIn, (Int)COMMANDUSABLE_GAME );
	const MetaMapRec *tab = map.getMetaMapRec( GameMessage::MSG_META_TOGGLE_COMMAND_TAB_BUILDINGS );
	CHECK_EQ( (Int)tab->m_key, (Int)MK_Q );
	CHECK_EQ( (Int)tab->m_modState, (Int)CTRL );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SIGNAL_ATTACK )->m_modState, (Int)ALT );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_VIEW_VIEW5 )->m_usableIn, (Int)COMMANDUSABLE_GAME );
	// nothing new appears: the sell command stays off the keyboard
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_SELL_MODE )->m_usableIn, (Int)COMMANDUSABLE_NONE );
}

TEST(hotkey_scheme_waypoint_key_holds_on_z)
{
	MetaRecProbe map;
	schemeClassicKeys( map );
	MetaMapRec *onZ = schemeBind( map, GameMessage::MSG_META_CREATE_FORMATION, MK_Z, NONE );
	map.applyScheme( HOTKEY_SCHEME_RED_ALERT );

	const MetaMapRec *begin = map.getMetaMapRec( GameMessage::MSG_META_BEGIN_WAYPOINTS_KEY );
	CHECK_EQ( (Int)begin->m_key, (Int)MK_Z );
	CHECK_EQ( (Int)begin->m_modState, (Int)NONE );
	CHECK_EQ( (Int)begin->m_transition, (Int)DOWN );
	CHECK_EQ( (Int)begin->m_usableIn, (Int)COMMANDUSABLE_GAME );
	const MetaMapRec *end = map.getMetaMapRec( GameMessage::MSG_META_END_WAYPOINTS_KEY );
	CHECK_EQ( (Int)end->m_key, (Int)MK_Z );
	CHECK_EQ( (Int)end->m_modState, (Int)NONE );
	CHECK_EQ( (Int)end->m_transition, (Int)UP );
	CHECK_EQ( (Int)end->m_usableIn, (Int)COMMANDUSABLE_GAME );

	// Z bare was someone else's; Alt held stays the waypoint hold beside it
	CHECK_EQ( (Int)onZ->m_usableIn, (Int)COMMANDUSABLE_NONE );
	const MetaMapRec *alt = map.getMetaMapRec( GameMessage::MSG_META_BEGIN_WAYPOINTS );
	CHECK_EQ( (Int)alt->m_key, (Int)MK_NONE );
	CHECK_EQ( (Int)alt->m_modState, (Int)ALT );
	CHECK_EQ( (Int)alt->m_usableIn, (Int)COMMANDUSABLE_GAME );
	CHECK_EQ( (Int)map.getMetaMapRec( GameMessage::MSG_META_END_WAYPOINTS )->m_transition, (Int)UP );

}

struct SchemeNameProbe : public MetaMap
{
	using MetaMap::findGameMessageMetaType;
};

// Hotkeys.ini and the scheme's table name the new commands by these words
TEST(hotkey_scheme_new_commands_have_names)
{
	SchemeNameProbe names;
	CHECK_EQ( (Int)names.findGameMessageMetaType( "SELL_MODE" ), (Int)GameMessage::MSG_META_SELL_MODE );
	CHECK_EQ( (Int)names.findGameMessageMetaType( "REPAIR_MODE" ), (Int)GameMessage::MSG_META_REPAIR_MODE );
	CHECK_EQ( (Int)names.findGameMessageMetaType( "BEGIN_WAYPOINTS_KEY" ), (Int)GameMessage::MSG_META_BEGIN_WAYPOINTS_KEY );
	CHECK_EQ( (Int)names.findGameMessageMetaType( "END_WAYPOINTS_KEY" ), (Int)GameMessage::MSG_META_END_WAYPOINTS_KEY );
}
