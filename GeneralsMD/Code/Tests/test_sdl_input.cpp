/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * C3a: input off Windows, on SDL3's offscreen video driver with synthetic events.
 *
 * Every event goes in with SDL_PushEvent and comes out of SDL_PollEvent into SdlInput_dispatch, the
 * call SdlGameEngine::serviceWindowsOS makes for every event that is not quit or focus.  Then the
 * engine's own code reads it: Keyboard::update into key state and modifiers, the IME manager into
 * GWM_IME_CHAR, SdlMouse into MouseIO.
 *
 *   - Keys: DIKeyCodes.h, read as text, against the table - each of its 107 codes mapped or listed as
 *     unreachable with a reason; every table entry through dispatch into key state and back; the
 *     modifiers; SDL's repeats dropped (armed: forwarded, they would repeat every held key twice); the
 *     GUI keys (Command) reach nothing; caps lock from SDL's state.
 *   - Text: attach and detach turn SDL's text input on and off; committed UTF-8 as the UTF-16 units
 *     WM_CHAR would carry, surrogates included, control characters dropped; Return and keypad Enter as
 *     '\r' (armed: SDL's text never carries one); the composition and its cursor; nothing detached,
 *     nothing while composing.
 *   - Mouse: positions scaled to the game's pixels and held to its screen; the button states and the
 *     double click from SDL's click count; X1 and X2 as side-button presses; the wheel at 120 a notch with fractions
 *     carried and the natural-scrolling direction left alone; a full ring drops; the confinement
 *     condition.
 *   - The double-click time on macOS against `defaults read -g com.apple.mouse.doubleClickThreshold`.
 *   - .ANI: a made-up file decoded to the pixel; and with ZH_DATA_DIR every Data/Cursors file of the
 *     install (read only) against this test's own parse of the same bytes, and made into an SDL cursor.
 *
 * What it cannot see: real keyboards (JIS and ISO layouts, a Help key), a real input method session,
 * macOS's own shortcuts taking keys first, and how edge scrolling and the radar drag feel - those need
 * the game on screen (M4).  The offscreen driver's cursors may not be real cursors; the test says.
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "GameClient/IMEManagerPosix.h"
#include "GameClient/KeyDefs.h"
#include "Platform/DoubleClickTime.h"
#include "SdlDevice/GameClient/AniCursor.h"
#include "SdlDevice/GameClient/SdlInput.h"
#include "SdlDevice/GameClient/SdlKeyTable.h"
#include "SdlDevice/GameClient/SdlKeyboard.h"
#include "SdlDevice/GameClient/SdlMouse.h"
#include "Common/FileSystem.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

SDL_Window *theWindow = NULL;

void bootMemory( void )
{
	static bool booted = false;
	if (!booted)
	{
		booted = true;
		initMemoryManager();
	}
}

bool start( void )
{
	static bool started = false, ok = false;
	if (!started)
	{
		started = true;
		bootMemory();
		SDL_SetHint( SDL_HINT_VIDEO_DRIVER, "offscreen" );
		ok = SDL_Init( SDL_INIT_VIDEO );
		if (ok)
			theWindow = SDL_CreateWindow( "test_sdl_input", 800, 600, 0 );
		ok = ok && theWindow != NULL;
		if (!ok)
			printf( "  SDL: %s\n", SDL_GetError() );
	}
	return ok;
}

// what serviceWindowsOS does with everything that is not quit or focus
void pump( void )
{
	SDL_Event event;
	while (SDL_PollEvent( &event ))
		SdlInput_dispatch( event );
}

void pushKey( SDL_Scancode scancode, bool down, bool repeat = false )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = SDL_GetWindowID( theWindow );
	event.key.scancode = scancode;
	event.key.down = down;
	event.key.repeat = repeat;
	SDL_PushEvent( &event );
}

// The keys Keyboard::update produced this frame, as the engine's consumers walk them
std::vector<KeyboardIO> keysThisFrame( SdlKeyboard *keyboard )
{
	keyboard->update();
	std::vector<KeyboardIO> keys;
	for (KeyboardIO *key = keyboard->getFirstKey(); key->key != KEY_NONE; ++key)
		keys.push_back( *key );
	return keys;
}

SdlKeyboard *theKeyboard( void )
{
	static SdlKeyboard *keyboard = NULL;
	if (keyboard == NULL)
	{
		keyboard = new SdlKeyboard;
		keyboard->init();
	}
	return keyboard;
}

// DIKeyCodes.h as text: the engine's key codes, independent of the table under test
std::map<unsigned, std::string> engineKeyCodes( void )
{
	std::map<unsigned, std::string> codes;
	FILE *file = fopen( DIKEYCODES_H, "r" );
	if (file == NULL)
		return codes;
	char line[256];
	while (fgets( line, sizeof( line ), file ))
	{
		char name[64];
		unsigned value;
		if (sscanf( line, " #define DIK_%63s 0x%x", name, &value ) == 2)
			codes[value] = name;
	}
	fclose( file );
	return codes;
}

}  // namespace

// ---- Keys ----------------------------------------------------------------------------------------

// ---- Headless: no SDL video, so no cursors to load (first, before anything starts SDL's video) ----

namespace {

class CountingLocalFileSystem : public PosixLocalFileSystem
{
public:
	int opens;
	CountingLocalFileSystem( void ) : opens( 0 ) {}
	virtual File *openFile( const Char *filename, Int access = 0 ) { ++opens; return PosixLocalFileSystem::openFile( filename, access ); }
};

class OneCursorMouse : public SdlMouse
{
public:
	OneCursorMouse( void )
	{
		m_cursorInfo[ ARROW ].textureName = "SCCPointer";
		m_cursorInfo[ ARROW ].numDirections = 1;
	}
};

}  // namespace

TEST(without_sdl_video_the_mouse_loads_no_cursors_as_a_headless_run_has_none)
{
	bootMemory();
	CHECK( !SDL_WasInit( SDL_INIT_VIDEO ) );
	CountingLocalFileSystem *files = NEW CountingLocalFileSystem;
	TheLocalFileSystem = files;
	TheFileSystem = NEW FileSystem;
	{
		OneCursorMouse mouse;
		mouse.initCursorResources();
		CHECK_EQ( files->opens, 0 );		// not one cursor file asked for: each would fail, and assert in a debug build
		// armed: with SDL's video the same call does go for the file
		CHECK( start() );
		mouse.initCursorResources();
		CHECK( files->opens > 0 );
	}
	delete TheFileSystem;
	TheFileSystem = NULL;
	delete files;
	TheLocalFileSystem = NULL;
}

TEST(every_engine_key_code_is_mapped_or_listed_unreachable)
{
	const std::map<unsigned, std::string> codes = engineKeyCodes();
	CHECK_EQ( (int)codes.size(), 107 );
	std::set<unsigned> mapped, unreachable;
	for (Int i = 0; i < SdlKeyTableSize; ++i)
		mapped.insert( SdlKeyTable[i].dik );
	for (Int i = 0; i < SdlUnreachableKeysSize; ++i)
		unreachable.insert( SdlUnreachableKeys[i].dik );
	int missing = 0;
	for (std::map<unsigned, std::string>::const_iterator it = codes.begin(); it != codes.end(); ++it)
	{
		const bool isMapped = mapped.count( it->first ) != 0, isUnreachable = unreachable.count( it->first ) != 0;
		if (isMapped == isUnreachable)
		{
			printf( "  DIK_%s (0x%02X): %s\n", it->second.c_str(), it->first, isMapped ? "both mapped and unreachable" : "neither mapped nor unreachable" );
			++missing;
		}
	}
	CHECK_EQ( missing, 0 );
	// and the table names nothing the engine does not have
	for (std::set<unsigned>::const_iterator it = mapped.begin(); it != mapped.end(); ++it)
		CHECK( codes.count( *it ) != 0 );
	printf( "  %d engine key codes: %d reached from %d scancodes, %d unreachable\n", (int)codes.size(),
		(int)mapped.size(), (int)SdlKeyTableSize, (int)unreachable.size() );
}

TEST(no_scancode_is_in_the_table_twice_and_each_looks_itself_up)
{
	std::set<int> seen;
	for (Int i = 0; i < SdlKeyTableSize; ++i)
	{
		CHECK( seen.insert( (int)SdlKeyTable[i].scancode ).second );
		CHECK_EQ( SdlKeyTable_dikFor( SdlKeyTable[i].scancode ), SdlKeyTable[i].dik );
	}
	// a few by name, from the other side: what DirectInput calls these keys
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_A ), KEY_A );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_BACKSPACE ), KEY_BACKSPACE );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_LCTRL ), KEY_LCTRL );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_LALT ), KEY_LALT );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_KP_ENTER ), KEY_KPENTER );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_DELETE ), KEY_DEL );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_GRAVE ), KEY_TICK );
	// Command is the system's; so are Pause and F13
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_LGUI ), 0 );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_RGUI ), 0 );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_PAUSE ), 0 );
	CHECK_EQ( SdlKeyTable_dikFor( SDL_SCANCODE_F13 ), 0 );
}

TEST(every_table_entry_reaches_the_engines_key_state_through_dispatch)
{
	CHECK( start() );
	SdlKeyboard *keyboard = theKeyboard();
	int wrong = 0;
	for (Int i = 0; i < SdlKeyTableSize; ++i)
	{
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool down = pass == 0;
			pushKey( SdlKeyTable[i].scancode, down );
			pump();
			const std::vector<KeyboardIO> keys = keysThisFrame( keyboard );
			const bool right = keys.size() == 1 && keys[0].key == SdlKeyTable[i].dik
				&& BitTest( keys[0].state, down ? KEY_STATE_DOWN : KEY_STATE_UP );
			if (!right && wrong++ < 5)
				printf( "  %s %s: %d keys, first 0x%02X\n", SDL_GetScancodeName( SdlKeyTable[i].scancode ), down ? "down" : "up",
					(int)keys.size(), keys.empty() ? 0 : keys[0].key );
		}
	}
	CHECK_EQ( wrong, 0 );
	// a key the engine does not have reaches nothing
	pushKey( SDL_SCANCODE_LGUI, true );
	pushKey( SDL_SCANCODE_LGUI, false );
	pump();
	CHECK( keysThisFrame( keyboard ).empty() );
}

TEST(modifiers_come_from_the_keys_as_on_windows)
{
	CHECK( start() );
	SdlKeyboard *keyboard = theKeyboard();
	pushKey( SDL_SCANCODE_LCTRL, true );
	pushKey( SDL_SCANCODE_A, true );
	pump();
	std::vector<KeyboardIO> keys = keysThisFrame( keyboard );
	CHECK( keyboard->isCtrl() );
	CHECK( !keyboard->isAlt() && !keyboard->isShift() );
	CHECK( keys.size() == 2 && keys[1].key == KEY_A && BitTest( keys[1].state, KEY_STATE_CONTROL ) );
	pushKey( SDL_SCANCODE_A, false );
	pushKey( SDL_SCANCODE_LCTRL, false );
	pushKey( SDL_SCANCODE_RALT, true );		// Option on a Mac
	pushKey( SDL_SCANCODE_LSHIFT, true );
	pump();
	keysThisFrame( keyboard );
	CHECK( !keyboard->isCtrl() );
	CHECK( keyboard->isAlt() && keyboard->isShift() );
	pushKey( SDL_SCANCODE_RALT, false );
	pushKey( SDL_SCANCODE_LSHIFT, false );
	pump();
	keysThisFrame( keyboard );
	CHECK( !keyboard->isAlt() && !keyboard->isShift() );
}

TEST(sdl_repeats_are_dropped_because_the_engine_repeats_keys_itself)
{
	CHECK( start() );
	SdlKeyboard *keyboard = theKeyboard();
	pushKey( SDL_SCANCODE_W, true );
	for (int i = 0; i < 5; ++i)
		pushKey( SDL_SCANCODE_W, true, true );
	pump();
	std::vector<KeyboardIO> keys = keysThisFrame( keyboard );
	CHECK_EQ( (int)keys.size(), 1 );
	pushKey( SDL_SCANCODE_W, false );
	pump();
	keysThisFrame( keyboard );
	// armed: had the repeats been forwarded, the engine would have seen six presses
	for (int i = 0; i < 6; ++i)
		keyboard->addKey( KEY_W, TRUE );
	keys = keysThisFrame( keyboard );
	CHECK_EQ( (int)keys.size(), 6 );
	keyboard->addKey( KEY_W, FALSE );
	keysThisFrame( keyboard );
}

TEST(caps_lock_is_sdls_state)
{
	CHECK( start() );
	SdlKeyboard *keyboard = theKeyboard();
	const SDL_Keymod before = SDL_GetModState();
	SDL_SetModState( SDL_KMOD_CAPS );
	CHECK( keyboard->getCapsState() );
	SDL_SetModState( SDL_KMOD_NONE );
	CHECK( !keyboard->getCapsState() );
	SDL_SetModState( before );
}

TEST(a_full_queue_drops_keys_as_directinputs_buffer_did)
{
	SdlKeyboard *keyboard = theKeyboard();
	int taken = 0;
	for (int i = 0; i < SdlKeyboard::QUEUE_SIZE + 10; ++i)
		taken += keyboard->addKey( KEY_Q, (i % 2) == 0 ) ? 1 : 0;
	CHECK_EQ( taken, (int)SdlKeyboard::QUEUE_SIZE );
	keysThisFrame( keyboard );
	CHECK( keysThisFrame( keyboard ).empty() );
}

// ---- Text ----------------------------------------------------------------------------------------

namespace {

// The manager with the window manager and the window's rectangle stood in for
class RecordingIME : public PosixIMEManager
{
public:
	std::vector<WideChar> sent;
protected:
	virtual void sendChar( GameWindow *, WideChar ch ) { sent.push_back( ch ); }
	virtual void windowArea( GameWindow *, Int &x, Int &y, Int &width, Int &height ) { x = 10; y = 20; width = 300; height = 24; }
};

GameWindow *const aTextField = reinterpret_cast<GameWindow *>( 0x1000 );	// never dereferenced: windowArea is stood in for

RecordingIME *theRecordingIME( void )
{
	static RecordingIME *ime = NULL;
	if (ime == NULL)
	{
		ime = new RecordingIME;
		TheIMEManager = ime;
	}
	ime->sent.clear();
	return ime;
}

void pushText( const char *utf8 )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_TEXT_INPUT;
	event.text.windowID = SDL_GetWindowID( theWindow );
	event.text.text = utf8;
	SDL_PushEvent( &event );
}

void pushEditing( const char *utf8, int start )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_TEXT_EDITING;
	event.edit.windowID = SDL_GetWindowID( theWindow );
	event.edit.text = utf8;
	event.edit.start = start;
	event.edit.length = 0;
	SDL_PushEvent( &event );
}

}  // namespace

TEST(attaching_a_text_field_turns_sdls_text_input_on_and_detaching_off)
{
	CHECK( start() );
	RecordingIME *ime = theRecordingIME();
	SdlInput_install();
	CHECK( !SDL_TextInputActive( theWindow ) );
	ime->attach( aTextField );
	CHECK( SDL_TextInputActive( theWindow ) );
	ime->disable();
	CHECK( !SDL_TextInputActive( theWindow ) );
	CHECK( !ime->isEnabled() );
	ime->enable();
	CHECK( SDL_TextInputActive( theWindow ) );
	ime->detatch();
	CHECK( !SDL_TextInputActive( theWindow ) );
	CHECK( ime->getWindow() == NULL );
}

TEST(committed_text_arrives_as_the_utf16_units_wm_char_carries)
{
	CHECK( start() );
	RecordingIME *ime = theRecordingIME();
	ime->attach( aTextField );
	pushText( "Ab\xC4\x9F\xC3\xBC\xC5\x9F\xE2\x82\xAC\xF0\x9F\x98\x80" );	// "Abğüş€😀"
	pushText( "x\ty\x01z" );		// control characters are not text: IMEManager passes 32 and up
	pump();
	const WideChar expected[] = { 'A', 'b', 0x011F, 0x00FC, 0x015F, 0x20AC, 0xD83D, 0xDE00, 'x', 'y', 'z' };
	CHECK_EQ( ime->sent.size(), sizeof( expected ) / sizeof( expected[0] ) );
	CHECK( ime->sent.size() == sizeof( expected ) / sizeof( expected[0] )
		&& memcmp( &ime->sent[0], expected, sizeof( expected ) ) == 0 );
	// detached, a text event goes nowhere
	ime->detatch();
	ime->sent.clear();
	pushText( "q" );
	pump();
	CHECK( ime->sent.empty() );
}

TEST(return_and_keypad_enter_end_an_edit_with_the_cr_wm_char_carries)
{
	CHECK( start() );
	RecordingIME *ime = theRecordingIME();
	ime->attach( aTextField );
	// armed: SDL's text never carries it, so without the key a field could never finish
	pushText( "\r" );
	pump();
	CHECK( ime->sent.empty() );
	pushKey( SDL_SCANCODE_RETURN, true );
	pushKey( SDL_SCANCODE_RETURN, false );
	pushKey( SDL_SCANCODE_KP_ENTER, true );
	pushKey( SDL_SCANCODE_KP_ENTER, false );
	pump();
	CHECK( ime->sent.size() == 2 && ime->sent[0] == u'\r' && ime->sent[1] == u'\r' );
	// not while composing: the key belongs to the input method then
	ime->sent.clear();
	pushEditing( "ka", 2 );
	pushKey( SDL_SCANCODE_RETURN, true );
	pump();
	CHECK( ime->sent.empty() );
	ime->detatch();
	pushKey( SDL_SCANCODE_RETURN, false );
	pushKey( SDL_SCANCODE_RETURN, true );
	pump();
	CHECK( ime->sent.empty() );		// nor with no field
	pushKey( SDL_SCANCODE_RETURN, false );
	pump();
	keysThisFrame( theKeyboard() );
}

TEST(the_composition_and_its_cursor_come_from_text_editing)
{
	CHECK( start() );
	RecordingIME *ime = theRecordingIME();
	ime->attach( aTextField );
	pushEditing( "\xE3\x81\xAB\xE3\x81\xBB", 1 );	// "にほ", cursor after the first
	pump();
	CHECK( ime->isComposing() );
	UnicodeString composition;
	ime->getCompositionString( composition );
	CHECK( composition.getLength() == 2 && composition.getCharAt( 0 ) == 0x306B && composition.getCharAt( 1 ) == 0x307B );
	CHECK_EQ( ime->getCompositionCursorPosition(), 1 );
	// the cursor counts code points; the engine's string counts UTF-16 units
	pushEditing( "\xF0\x9F\x98\x80" "a", 1 );		// "😀a", cursor before the a
	pump();
	CHECK_EQ( ime->getCompositionCursorPosition(), 2 );
	pushEditing( "abc", -1 );		// no cursor given: at the end
	pump();
	CHECK_EQ( ime->getCompositionCursorPosition(), 3 );
	CHECK( ime->sent.empty() );		// composing sends nothing
	pushText( "\xE6\x97\xA5" );		// the commit ends it
	pump();
	CHECK( !ime->isComposing() );
	CHECK( ime->sent.size() == 1 && ime->sent[0] == 0x65E5 );
	pushEditing( "x", 1 );
	pushEditing( "", 0 );			// an empty composition is an ended one
	pump();
	CHECK( !ime->isComposing() );
	CHECK_EQ( ime->getCandidateCount(), 0 );	// the platform draws the candidates
	ime->detatch();
}

// ---- Mouse ---------------------------------------------------------------------------------------

namespace {

class OpenMouse : public SdlMouse
{
public:
	using SdlMouse::getMouseEvent;
	enum { RING = Mouse::NUM_MOUSE_EVENTS };
};

OpenMouse *theMouse( void )
{
	static OpenMouse *mouse = NULL;
	if (mouse == NULL)
		mouse = new OpenMouse;
	MouseIO drain;
	while (mouse->getMouseEvent( &drain, FALSE ) == MOUSE_OK)
		;
	return mouse;
}

void pushButton( Uint8 button, bool down, Uint8 clicks, float x, float y )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = SDL_GetWindowID( theWindow );
	event.button.button = button;
	event.button.down = down;
	event.button.clicks = clicks;
	event.button.x = x;
	event.button.y = y;
	SDL_PushEvent( &event );
}

void pushMotion( float x, float y )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.windowID = SDL_GetWindowID( theWindow );
	event.motion.x = x;
	event.motion.y = y;
	SDL_PushEvent( &event );
}

void pushWheel( float y, bool flipped = false )
{
	SDL_Event event;
	SDL_zero( event );
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.windowID = SDL_GetWindowID( theWindow );
	event.wheel.y = y;
	event.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
	event.wheel.mouse_x = 100;
	event.wheel.mouse_y = 50;
	SDL_PushEvent( &event );
}

std::vector<MouseIO> mouseEvents( OpenMouse *mouse )
{
	std::vector<MouseIO> events;
	MouseIO io;
	while (mouse->getMouseEvent( &io, FALSE ) == MOUSE_OK)
		events.push_back( io );
	return events;
}

}  // namespace

TEST(window_points_scale_to_the_games_pixels_and_hold_to_its_screen)
{
	Int x, y;
	SdlInput_scaleToGame( 400.0f, 300.0f, 800, 600, 1024, 768, x, y );
	CHECK( x == 512 && y == 384 );
	SdlInput_scaleToGame( 1439.9f, 899.9f, 1440, 900, 2880, 1800, x, y );	// a Retina window, the game at pixels
	CHECK( x == 2879 && y == 1799 );
	// Wayland at 150 %: a 1280x720 game in a window of 853x480 points (sizeWindow), its centre and its far corner
	SdlInput_scaleToGame( 426.5f, 240.0f, 853, 480, 1280, 720, x, y );
	CHECK( x == 640 && y == 360 );
	SdlInput_scaleToGame( 852.9f, 479.9f, 853, 480, 1280, 720, x, y );
	CHECK( x == 1279 && y == 719 );
	SdlInput_scaleToGame( -12.0f, 950.0f, 800, 600, 800, 600, x, y );		// a captured drag outside the window
	CHECK( x == 0 && y == 599 );
	SdlInput_scaleToGame( 7.5f, 3.2f, 0, 0, 0, 0, x, y );								// nothing to scale by
	CHECK( x == 7 && y == 3 );
}

TEST(motion_and_buttons_translate_as_win32mouses_messages_did)
{
	CHECK( start() );
	OpenMouse *mouse = theMouse();
	// no display in this test: the 800 x 600 window is the screen
	pushMotion( 120.6f, 45.2f );
	pushButton( SDL_BUTTON_LEFT, true, 1, 121, 46 );
	pushButton( SDL_BUTTON_LEFT, false, 1, 121, 46 );
	pushButton( SDL_BUTTON_RIGHT, true, 1, 10, 10 );		// a trackpad's secondary click is this too
	pushButton( SDL_BUTTON_MIDDLE, true, 1, 10, 10 );
	pushButton( SDL_BUTTON_X1, true, 1, 10, 10 );				// back: WM_XBUTTONDOWN with XBUTTON1
	pushButton( SDL_BUTTON_X2, true, 1, 10, 10 );				// forward: XBUTTON2
	pushMotion( -40, 9000 );
	pump();
	const std::vector<MouseIO> e = mouseEvents( mouse );
	CHECK_EQ( (int)e.size(), 8 );
	if (e.size() == 8)
	{
		CHECK( e[0].pos.x == 120 && e[0].pos.y == 45 && e[0].leftState == MBS_Up && e[0].wheelPos == 0 );
		CHECK( e[1].leftState == MBS_Down && e[1].pos.x == 121 && e[1].pos.y == 46 );
		CHECK( e[2].leftState == MBS_Up );
		CHECK( e[3].rightState == MBS_Down && e[3].leftState == MBS_Up );
		CHECK( e[4].middleState == MBS_Down && e[4].sideButton == 0 );
		CHECK( e[5].sideButton == -1 && e[5].leftState == MBS_Up && e[5].middleState == MBS_Up );
		CHECK( e[6].sideButton == 1 );
		CHECK( e[7].pos.x == 0 && e[7].pos.y == 599 );
	}
}

TEST(the_second_of_each_pair_of_clicks_is_windows_double_click)
{
	CHECK( start() );
	OpenMouse *mouse = theMouse();
	// Windows: down, up, double click, up, down, up, double click, up.  SDL counts 1, 2, 3, 4.
	for (Uint8 clicks = 1; clicks <= 4; ++clicks)
	{
		pushButton( SDL_BUTTON_LEFT, true, clicks, 5, 5 );
		pushButton( SDL_BUTTON_LEFT, false, clicks, 5, 5 );
	}
	pushButton( SDL_BUTTON_RIGHT, true, 2, 5, 5 );
	pump();
	const std::vector<MouseIO> e = mouseEvents( mouse );
	CHECK_EQ( (int)e.size(), 9 );
	if (e.size() == 9)
	{
		const MouseButtonState downs[] = { MBS_Down, MBS_DoubleClick, MBS_Down, MBS_DoubleClick };
		for (int i = 0; i < 4; ++i)
		{
			CHECK_EQ( e[2 * i].leftState, downs[i] );
			CHECK_EQ( e[2 * i + 1].leftState, MBS_Up );
		}
		CHECK_EQ( e[8].rightState, MBS_DoubleClick );
	}
}

TEST(the_wheel_is_120_a_notch_with_fractions_carried_and_natural_scrolling_left_alone)
{
	CHECK( start() );
	OpenMouse *mouse = theMouse();
	SdlInput_resetWheel();
	pushWheel( 1.0f );
	pushWheel( -2.0f );
	pushWheel( 0.004f );		// 0.48 of a unit: nothing yet...
	pushWheel( 0.004f );
	pushWheel( 0.004f );		// ...1.44 by now: one
	pushWheel( 1.0f, true );	// natural scrolling: SDL's y is what the user's setting made it; not undone
	pump();
	const std::vector<MouseIO> e = mouseEvents( mouse );
	CHECK_EQ( (int)e.size(), 4 );
	if (e.size() == 4)
	{
		CHECK_EQ( e[0].wheelPos, 120 );
		CHECK_EQ( e[1].wheelPos, -240 );
		CHECK_EQ( e[2].wheelPos, 1 );
		CHECK_EQ( e[3].wheelPos, 120 );
		CHECK( e[0].pos.x == 100 && e[0].pos.y == 50 );
	}
	SdlInput_resetWheel();
}

TEST(a_full_mouse_ring_drops_events_as_win32mouses_does)
{
	OpenMouse *mouse = theMouse();
	for (int i = 0; i < OpenMouse::RING + 20; ++i)
		mouse->addEvent( SdlMouse::EVENT_MOVE, i, 0, SdlMouse::BUTTON_LEFT, 0, 0, 0 );
	const std::vector<MouseIO> e = mouseEvents( mouse );
	CHECK_EQ( (int)e.size(), (int)OpenMouse::RING );
	CHECK( !e.empty() && e.back().pos.x == OpenMouse::RING - 1 );
}

TEST(the_pointer_is_confined_only_where_win32mouse_clips_it)
{
	CHECK( SdlMouse::wantsConfinement( TRUE, TRUE, TRUE ) );
	CHECK( !SdlMouse::wantsConfinement( FALSE, TRUE, TRUE ) );		// a plain window leaves it free
	CHECK( !SdlMouse::wantsConfinement( TRUE, FALSE, TRUE ) );		// without the focus
	CHECK( !SdlMouse::wantsConfinement( TRUE, TRUE, FALSE ) );		// not snatched from across the desktop
}

TEST(the_double_click_time_is_the_users_own)
{
#if defined(__APPLE__)
	CHECK( start() );		// SDL has brought in AppKit, as in the game
	unsigned expected = 500;
	FILE *pipe = popen( "defaults read -g com.apple.mouse.doubleClickThreshold 2>/dev/null", "r" );
	if (pipe != NULL)
	{
		double seconds = 0;
		if (fscanf( pipe, "%lf", &seconds ) == 1 && seconds >= 0.1 && seconds <= 5.0)
			expected = (unsigned)( seconds * 1000.0 + 0.5 );
		pclose( pipe );
	}
	printf( "  NSEvent says %u ms; the user's defaults say %u ms\n", systemDoubleClickTimeMS(), expected );
	CHECK_EQ( systemDoubleClickTimeMS(), expected );
	/* 500 is also the fallback, so show the answer came from NSEvent: its class is there to ask in this
		 process, as in the game.  The value itself cannot be varied here without changing the user's own
		 setting (a per-process argument default does not reach NSEvent), which a test does not do. */
	typedef void *(*GetClass)( const char * );
	GetClass getClass = (GetClass)dlsym( RTLD_DEFAULT, "objc_getClass" );
	CHECK( getClass != NULL && getClass( "NSEvent" ) != NULL );
#else
	CHECK_EQ( systemDoubleClickTimeMS(), 500u );
#endif
}

// ---- .ANI cursors --------------------------------------------------------------------------------

namespace {

void put16( std::vector<UnsignedByte> &b, unsigned v ) { b.push_back( v & 0xFF ); b.push_back( (v >> 8) & 0xFF ); }
void put32( std::vector<UnsignedByte> &b, unsigned v ) { put16( b, v & 0xFFFF ); put16( b, v >> 16 ); }
void chunk( std::vector<UnsignedByte> &b, const char *id, const std::vector<UnsignedByte> &body )
{
	b.insert( b.end(), id, id + 4 );
	put32( b, (unsigned)body.size() );
	b.insert( b.end(), body.begin(), body.end() );
	if (body.size() & 1)
		b.push_back( 0 );
}

// An 8 x 2 .CUR at 4 bits: palette 0 black, 1 red, 2 white; row 0 (top) 0,1,2,1,..; row 1 masked on the right half
std::vector<UnsignedByte> madeUpCursor( unsigned hotX, unsigned hotY, unsigned colourShift )
{
	std::vector<UnsignedByte> image;
	put32( image, 40 ); put32( image, 8 ); put32( image, 4 ); put16( image, 1 ); put16( image, 4 );
	put32( image, 0 ); put32( image, 0 ); put32( image, 0 ); put32( image, 0 ); put32( image, 3 ); put32( image, 0 );
	const UnsignedByte palette[] = { 0, 0, 0, 0, 0, 0, 255, 0, 255, 255, 255, 0 };		// BGRA quads
	image.insert( image.end(), palette, palette + 12 );
	// colour rows, bottom-up: the bottom row (row 1) first, 4 bytes each
	const UnsignedByte bottom[] = { 0x22, 0x22, 0x11, 0x11 }, top[] = { (UnsignedByte)(0x01 + colourShift), 0x21, 0x21, 0x21 };
	image.insert( image.end(), bottom, bottom + 4 );
	image.insert( image.end(), top, top + 4 );
	// mask rows, bottom-up: bottom row's right half masked; top row clear
	const UnsignedByte maskBottom[] = { 0x0F, 0, 0, 0 }, maskTop[] = { 0, 0, 0, 0 };
	image.insert( image.end(), maskBottom, maskBottom + 4 );
	image.insert( image.end(), maskTop, maskTop + 4 );
	std::vector<UnsignedByte> file;
	put16( file, 0 ); put16( file, 2 ); put16( file, 1 );
	file.push_back( 8 ); file.push_back( 2 ); file.push_back( 3 ); file.push_back( 0 );
	put16( file, hotX ); put16( file, hotY ); put32( file, (unsigned)image.size() ); put32( file, 22 );
	file.insert( file.end(), image.begin(), image.end() );
	return file;
}

std::vector<UnsignedByte> madeUpAni( unsigned framesClaimed )
{
	std::vector<UnsignedByte> anih, rate, seq, frames, riff;
	put32( anih, 36 ); put32( anih, framesClaimed ); put32( anih, 3 ); put32( anih, 0 ); put32( anih, 0 );
	put32( anih, 0 ); put32( anih, 0 ); put32( anih, 6 ); put32( anih, 3 );		// 6 jiffies, icons, sequenced
	put32( rate, 6 ); put32( rate, 12 ); put32( rate, 60 );
	put32( seq, 1 ); put32( seq, 0 ); put32( seq, 1 );
	frames.insert( frames.end(), "fram", "fram" + 4 );
	chunk( frames, "icon", madeUpCursor( 3, 1, 0 ) );
	chunk( frames, "icon", madeUpCursor( 3, 1, 1 ) );
	std::vector<UnsignedByte> body;
	body.insert( body.end(), "ACON", "ACON" + 4 );
	chunk( body, "anih", anih );
	chunk( body, "rate", rate );
	chunk( body, "seq ", seq );
	chunk( body, "LIST", frames );
	riff.insert( riff.end(), "RIFF", "RIFF" + 4 );
	put32( riff, (unsigned)body.size() );
	riff.insert( riff.end(), body.begin(), body.end() );
	return riff;
}

}  // namespace

TEST(a_made_up_ani_decodes_to_the_pixel)
{
	const std::vector<UnsignedByte> file = madeUpAni( 2 );
	AniCursor cursor;
	std::string why;
	CHECK( AniCursor_decode( &file[0], file.size(), cursor, &why ) );
	CHECK_EQ( (int)cursor.frames.size(), 2 );
	CHECK_EQ( (int)cursor.steps.size(), 3 );
	if (cursor.frames.size() == 2 && cursor.steps.size() == 3)
	{
		CHECK( cursor.steps[0].frame == 1 && cursor.steps[1].frame == 0 && cursor.steps[2].frame == 1 );
		CHECK( cursor.steps[0].durationMs == 100 && cursor.steps[1].durationMs == 200 && cursor.steps[2].durationMs == 1000 );
		const AniCursorFrame &f = cursor.frames[0];
		CHECK( f.width == 8 && f.height == 2 && f.hotX == 3 && f.hotY == 1 );
		// top row: indices 0,1,2,1,2,1,2,1 -> black, red, white, red...
		const UnsignedByte *p = &f.rgba[0];
		CHECK( p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255 );
		CHECK( p[4] == 255 && p[5] == 0 && p[6] == 0 && p[7] == 255 );
		CHECK( p[8] == 255 && p[9] == 255 && p[10] == 255 && p[11] == 255 );
		// bottom row: white, white, white, white, then masked (transparent) over red: inverted on Windows
		const UnsignedByte *q = &f.rgba[ 8 * 4 ];
		CHECK( q[0] == 255 && q[3] == 255 );
		CHECK( q[4 * 4 + 3] == 0 && q[7 * 4 + 3] == 0 );
		CHECK_EQ( f.invertedPixels, 4 );
		// frame 1 differs only in its first byte's low nibble: its second pixel is white, not red
		const UnsignedByte *r = &cursor.frames[1].rgba[0];
		CHECK( r[0] == 0 && r[3] == 255 && r[4] == 255 && r[5] == 255 && r[6] == 255 && r[7] == 255 );
	}
	// armed: a header that claims three frames where there are two is refused
	const std::vector<UnsignedByte> wrong = madeUpAni( 3 );
	CHECK( !AniCursor_decode( &wrong[0], wrong.size(), cursor, &why ) );
	CHECK( !why.empty() );
}

namespace {

// The test's own reading of an .ANI: chunks, frames, and each frame's pixels, written apart from AniCursor
struct Oracle
{
	std::vector<AniCursorFrame> frames;
	std::vector<std::pair<int, unsigned> > steps;
};

unsigned r16( const std::vector<UnsignedByte> &b, size_t at ) { return b[at] | (b[at + 1] << 8); }
unsigned r32( const std::vector<UnsignedByte> &b, size_t at ) { return r16( b, at ) | (r16( b, at + 2 ) << 16); }

bool oracleFrame( const std::vector<UnsignedByte> &b, size_t at, AniCursorFrame &f )
{
	f.hotX = (int)r16( b, at + 10 );
	f.hotY = (int)r16( b, at + 12 );
	const size_t img = at + r32( b, at + 18 );
	f.width = (int)r32( b, img + 4 );
	f.height = (int)r32( b, img + 8 ) / 2;
	const unsigned bpp = r16( b, img + 14 );
	if (bpp != 4 && bpp != 8 && bpp != 1)
		return false;
	unsigned paletteCount = r32( b, img + 32 );
	if (paletteCount == 0)
		paletteCount = 1u << bpp;
	const size_t palette = img + r32( b, img );
	const size_t xorAt = palette + 4 * paletteCount;
	const size_t xorRow = ((f.width * bpp + 31) / 32) * 4, andRow = ((f.width + 31) / 32) * 4;
	const size_t andAt = xorAt + xorRow * f.height;
	f.invertedPixels = 0;
	f.rgba.assign( (size_t)f.width * f.height * 4, 0 );
	for (int fileRow = 0; fileRow < f.height; ++fileRow)		// in file order, bottom row first
	{
		const int screenRow = f.height - 1 - fileRow;
		for (int x = 0; x < f.width; ++x)
		{
			unsigned index;
			const UnsignedByte byte = b[ xorAt + fileRow * xorRow + (x * bpp) / 8 ];
			if (bpp == 8) index = byte;
			else if (bpp == 4) index = (x % 2 == 0) ? (byte >> 4) : (byte & 15);
			else index = (byte >> (7 - x % 8)) & 1;
			const bool transparent = ((b[ andAt + fileRow * andRow + x / 8 ] << (x % 8)) & 0x80) != 0;
			const size_t q = palette + 4 * index;
			UnsignedByte *o = &f.rgba[ ((size_t)screenRow * f.width + x) * 4 ];
			if (transparent)
			{
				if (b[q] | b[q + 1] | b[q + 2])
					f.invertedPixels++;
				continue;		// all four stay 0
			}
			o[0] = b[q + 2]; o[1] = b[q + 1]; o[2] = b[q]; o[3] = 255;
		}
	}
	return true;
}

bool oracle( const std::vector<UnsignedByte> &b, Oracle &o )
{
	unsigned rate = 0, frames = 0, steps = 0, flags = 0;
	std::vector<unsigned> rates, seq;
	for (size_t at = 12; at + 8 <= b.size(); )
	{
		const std::string id( (const char *)&b[at], 4 );
		const unsigned n = r32( b, at + 4 );
		if (id == "anih") { frames = r32( b, at + 12 ); steps = r32( b, at + 16 ); rate = r32( b, at + 36 ); flags = r32( b, at + 40 ); }
		if (id == "rate") for (unsigned i = 0; i < n / 4; ++i) rates.push_back( r32( b, at + 8 + 4 * i ) );
		if (id == "seq ") for (unsigned i = 0; i < n / 4; ++i) seq.push_back( r32( b, at + 8 + 4 * i ) );
		if (id == "LIST")
			for (size_t f = at + 12; f + 8 <= at + 8 + n; )
			{
				const unsigned m = r32( b, f + 4 );
				if (std::string( (const char *)&b[f], 4 ) == "icon")
				{
					AniCursorFrame frame;
					if (!oracleFrame( b, f + 8, frame ))
						return false;
					o.frames.push_back( frame );
				}
				f += 8 + m + (m & 1);
			}
		at += 8 + n + (n & 1);
	}
	if (steps == 0) steps = frames;
	for (unsigned i = 0; i < steps; ++i)
	{
		const int frame = (flags & 2) ? (int)seq[i] : (int)i;
		const unsigned jiffies = i < rates.size() ? rates[i] : rate;
		o.steps.push_back( std::make_pair( frame, (jiffies * 1000 + 30) / 60 ) );
	}
	return o.frames.size() == frames;
}

}  // namespace

TEST(every_install_cursor_decodes_as_this_tests_own_parse_says)
{
	const char *dataDir = getenv( "ZH_DATA_DIR" );
	if (dataDir == NULL || *dataDir == 0)
	{
		printf( "  skip: no game data (ZH_DATA_DIR) - the install's cursors are not checked\n" );
		return;
	}
	CHECK( start() );
	const std::string dir = std::string( dataDir ) + "/zerohour/Data/Cursors";
	DIR *listing = opendir( dir.c_str() );
	CHECK( listing != NULL );
	if (listing == NULL)
		return;
	int files = 0, frames = 0, steps = 0, inverted = 0, cursors = 0;
	std::string cursorError;
	while (struct dirent *entry = readdir( listing ))
	{
		const std::string name = entry->d_name;
		if (name.size() < 5 || strcasecmp( name.c_str() + name.size() - 4, ".ani" ) != 0)
			continue;
		if (name[0] == '.')
			continue;		// "._X.ani": macOS's AppleDouble metadata beside each file on an exFAT volume, not a cursor
		FILE *file = fopen( (dir + "/" + name).c_str(), "rb" );
		CHECK( file != NULL );
		if (file == NULL)
			continue;
		std::vector<UnsignedByte> bytes;
		UnsignedByte buffer[4096];
		size_t got;
		while ((got = fread( buffer, 1, sizeof( buffer ), file )) > 0)
			bytes.insert( bytes.end(), buffer, buffer + got );
		fclose( file );
		++files;

		AniCursor decoded;
		Oracle expected;
		std::string why;
		const bool ok = AniCursor_decode( &bytes[0], bytes.size(), decoded, &why );
		if (!ok) printf( "  %s: %s\n", name.c_str(), why.c_str() );
		CHECK( ok );
		CHECK( oracle( bytes, expected ) );
		CHECK_EQ( decoded.frames.size(), expected.frames.size() );
		CHECK_EQ( decoded.steps.size(), expected.steps.size() );
		for (size_t i = 0; i < decoded.frames.size() && i < expected.frames.size(); ++i)
		{
			const AniCursorFrame &a = decoded.frames[i], &b = expected.frames[i];
			CHECK( a.width == b.width && a.height == b.height && a.hotX == b.hotX && a.hotY == b.hotY );
			CHECK( a.rgba == b.rgba );
			CHECK_EQ( a.invertedPixels, b.invertedPixels );
			inverted += a.invertedPixels;
		}
		// what SdlMouse asks for at a content scale of 1: every file carries a 32, EA's alone or the
		// Reforged 48's second, and that one comes back rather than the first
		AniCursor small;
		CHECK( AniCursor_decode( &bytes[0], bytes.size(), small, &why, 32 ) );
		for (size_t i = 0; i < small.frames.size(); ++i)
			CHECK( small.frames[i].width == 32 && small.frames[i].height == 32 );
		for (size_t i = 0; i < decoded.steps.size() && i < expected.steps.size(); ++i)
			CHECK( decoded.steps[i].frame == expected.steps[i].first && decoded.steps[i].durationMs == expected.steps[i].second );
		frames += (int)decoded.frames.size();
		steps += (int)decoded.steps.size();
		if (SDL_Cursor *cursor = SdlMouse::createCursor( decoded ))
		{
			++cursors;
			SDL_DestroyCursor( cursor );
		}
		else if (cursorError.empty())
			cursorError = SDL_GetError();
	}
	closedir( listing );
	printf( "  %d .ANI files, %d frames, %d animation steps, %d screen-inverting pixels made transparent; "
		"%d SDL cursors made%s%s\n", files, frames, steps, inverted, cursors,
		cursorError.empty() ? "" : " (the offscreen driver: ", cursorError.empty() ? "" : (cursorError + ")").c_str() );
	CHECK_EQ( files, 52 );
	CHECK_EQ( frames, 296 );
}
