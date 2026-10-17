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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/Main/WinMain.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: PosixMain.cpp ////////////////////////////////////////////////////////////////////////////
// Desc:   The game's entry point off Windows (C2): WinMain's start-up sequence without the Windows.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* One main for macOS and Linux, on SDL3 (decision 3).  It follows WinMain.cpp step for step, in its
	 order, and leaves out what only Windows has; each left-out step says where it goes:

	 - _set_FMA3_enable(0): the x64 C runtime's choice between two libm paths.  Whether this platform's
		 libm answers the logic's log() the same way is E1's question, not the entry point's.
	 - SetProcessDPIAware: the window's pixel density is the renderer's (D4).
	 - _set_se_translator / SetUnhandledExceptionFilter: their counterpart is C5's crash handler,
		 installCrashHandlers, first thing in main.
	 - "-DX" stack dumps: Windows symbol lookup (GetFunctionDetails).
	 - _CrtSetDbgFlag, the splash bitmap, OLE, copy protection and the Optimus exports: Windows only.
	 - The Windows window class and WndProc: the window is SdlGameEngine's, which the engine creates
		 before GameEngine::init, as WinMain creates it before GameMain.

	 The install root is the process's working directory, as on Windows, where WinMain sets it to the
	 executable's directory.  Here that is the default too, and "-root <dir>" overrides it.  It is set
	 once, before anything asks for a path, and nothing changes it after (C1's Roots paragraph).  Port
	 rule 9 (PORTING.md) applies to how this is RUN: GameEngine::init deletes Data\INI\INIZH.big from the root, as it
	 does in a player's install, so a development run must be rooted at a copy of the game data.
	 The fork's own data is an overlay searched before that root (P1, decision 9): "-overlay <dir>", or
	 the one a package puts beside the executable.  See chooseOverlays.  Since P1 step 2 every root is
	 read-only: a relative write, that deletion included, is refused and logged, so rule 9 holds by
	 construction as well as by how the game is run. */

#include <SDL3/SDL_main.h>	// SDL3's main: on macOS and Linux an ordinary main

#include "PreRTS.h"

#include "Lib/BaseType.h"
#include "Platform/RenderTypes.h"
#include "Common/CrashHandler.h"
#include "Common/CriticalSection.h"
#include "Common/Debug.h"
#include "Common/EarlyCommandLine.h"
#include "Common/EarlyOptions.h"
#include "Common/Errors.h"
#include "Common/ExecutableDirectory.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/version.h"
#include "Common/WindowMode.h"
#include "SdlDevice/Common/SdlGameEngine.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "posixpath.h"

#include <SDL3/SDL.h>		// the folder dialog and the message box, before the engine exists (P1 step 4)
#include <atomic>
#include <mutex>

#include "Common/RegistryFile.h"
#include "PosixDevice/Common/PosixInstallRoot.h"

// GLOBALS ////////////////////////////////////////////////////////////////////
// gameengine names these three; WinMain.cpp defines them on Windows, with these values.
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char s_noAppPrefix[] = "";
char *gAppPrefix = s_noAppPrefix; /// So WB can have a different debug log file name.

// WinMain.cpp's other two, which W3DDevice names: the window the device draws into, which SdlGameEngine
// sets when it makes one (the SDL_Window; null under -headless, where there is none), and whether it is
// borderless, which W3DDisplay keeps up to date across a change of window mode.
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

static CriticalSection critSec2, critSec3, critSec4, critSec5;

// WinMain's pre-parse: how the window starts, settled before the engine exists.
static SdlGameEngine::WindowRequest s_windowRequest = { FALSE, FALSE, FALSE, FALSE, FALSE };

// WinMain's GENERALS_GUID, the name of its one-copy mutex; here the name of a lock file.
#define GENERALS_GUID "685EAFF2-3216-4265-B047-251C5F4B82F3"

// The player's choice of folder, through SDL's native dialog (NSOpenPanel on macOS).  The dialog answers
// asynchronously, maybe on another thread, so the answer is handed over under a lock.
struct FolderAnswer
{
	std::mutex lock;
	std::atomic<int> state;		// 0 waiting, 1 chosen, 2 cancelled or failed
	std::string path;
	std::string failure;		// SDL's error when it failed; empty when the player cancelled
};

static void SDLCALL onFolderChosen( void *userdata, const char * const *filelist, int )
{
	FolderAnswer *answer = (FolderAnswer *)userdata;
	std::lock_guard<std::mutex> guard( answer->lock );
	if (filelist != NULL && filelist[0] != NULL)
	{
		answer->path = filelist[0];
		answer->state = 1;
	}
	else
	{
		if (filelist == NULL)		// an error, not the player's cancel (an empty list)
			answer->failure = SDL_GetError();
		answer->state = 2;
	}
}

#if defined(__APPLE__)
/** Brings this app to the front.  The chooser runs before any window exists, and without one nothing
	* activates the app (SDL does it when it shows a window, and not at launch on macOS 14 and later), so
	* its message box and its folder panel - modal, owned by no window - would open behind whatever app
	* was in front.  Through the Objective-C runtime, so this file stays C++: [NSApp activate] where it
	* exists (macOS 14), and activateIgnoringOtherApps: before it.  The runtime's three functions are
	* declared here rather than through <objc/runtime.h>, whose BOOL would clash with bittype.h's. */
extern "C" void *objc_getClass( const char *name );
extern "C" void *sel_registerName( const char *name );
extern "C" void objc_msgSend( void );

static void activateThisApp()
{
	void *applicationClass = objc_getClass( "NSApplication" );
	if (applicationClass == NULL)
		return;
	void *application = ((void *(*)( void *, void * ))objc_msgSend)( applicationClass, sel_registerName( "sharedApplication" ) );
	if (application == NULL)
		return;
	void *activate = sel_registerName( "activate" );
	if (((bool (*)( void *, void *, void * ))objc_msgSend)( application, sel_registerName( "respondsToSelector:" ), activate ))
		((void (*)( void *, void * ))objc_msgSend)( application, activate );
	((void (*)( void *, void *, bool ))objc_msgSend)( application, sel_registerName( "activateIgnoringOtherApps:" ), true );
}
#else
static void activateThisApp() {}
#endif

/** Whether this runs in the Steam Deck's Game Mode (P3): gamescope's session names itself in
	* XDG_CURRENT_DESKTOP, and Steam's gamepad interface sets SteamGamepadUI for what it starts.  A file
	* dialog may not show there, so PosixMain asks for -root in Steam's launch options instead.  (Both names
	* are what gamescope and Steam document; a Steam Deck itself has not been measured yet.) */
static bool inSteamGameMode()
{
	const char *desktop = getenv( "XDG_CURRENT_DESKTOP" ), *gamepadUi = getenv( "SteamGamepadUI" );
	return (desktop != NULL && strcasecmp( desktop, "gamescope" ) == 0) || (gamepadUi != NULL && gamepadUi[0] == '1');
}

/// What Game Mode says when the Zero Hour folder is found nowhere; the package's README.txt says the same
static const char GAME_MODE_NO_ROOT[] =
	"Zero Hour Reforged could not find your Command & Conquer Generals Zero Hour folder.\n\n"
	"In Steam, open this game's Properties and enter under Launch Options:\n\n"
	"-root \"/home/you/Games/Command & Conquer Generals Zero Hour\"\n\n"
	"with the path of your own Zero Hour folder (the one with INIZH.big in it) between the quotes. Or start "
	"the game once from Desktop Mode to choose the folder there; it is remembered after that.";

/** PosixInstallChooser over SDL: the reason the last choice was refused, if any, in a message box, then
	* the folder dialog.  FALSE when the player cancels, or when the dialog cannot be shown; then the reason
	* goes into the std::string the context points at, for the message the caller shows. */
static bool chooseFolderWithSdl( PosixInstallQuestion question, const std::string &why, std::string &chosen, void *context )
{
	std::string &failure = *(std::string *)context;
	if (!SDL_InitSubSystem( SDL_INIT_VIDEO ))
	{
		failure = std::string( "The folder dialog could not be opened: " ) + SDL_GetError();
		return false;
	}
	activateThisApp();
	if (!why.empty())
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_WARNING, "Zero Hour Reforged", why.c_str(), NULL );

	FolderAnswer answer;
	answer.state = 0;
	char home[ 4096 ];
	const SDL_PropertiesID properties = SDL_CreateProperties();
	SDL_SetStringProperty( properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, question == CHOOSE_GENERALS
		? "Choose your Command & Conquer Generals folder (the original game)"
		: "Choose your Command & Conquer Generals Zero Hour folder" );
	SDL_SetStringProperty( properties, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, "Use This Folder" );
	if (findHomeDirectory( home, sizeof( home ) ))
		SDL_SetStringProperty( properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, home );
	SDL_ShowFileDialogWithProperties( SDL_FILEDIALOG_OPENFOLDER, onFolderChosen, &answer, properties );
	while (answer.state == 0)
	{
		SDL_PumpEvents();
		SDL_Delay( 10 );
	}
	SDL_DestroyProperties( properties );
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	std::lock_guard<std::mutex> guard( answer.lock );
	chosen = answer.path;
	if (!answer.failure.empty())
		failure = "The folder dialog could not be shown: " + answer.failure;
	return answer.state == 1;
}

/** The chooser's stand-in for tests (no test run may need a human at any machine).
	* ZH_TEST_CHOOSER_ANSWERS names a file of answers, one folder a line; each time the chooser is asked it
	* takes the next line, and "cancel" or the end of the file cancels.  Nothing is shown: the reason the last
	* answer was refused, which the dialog's message box would have shown, goes to stderr, as each answer does.
	* PosixMain treats a start with it set as a packaged first launch; the SDL panel itself was proven by the
	* user's own eyes (2026-09-27), and test_first_launch_chooser runs this path headless. */
struct ScriptedAnswers
{
	std::vector<std::string> answers;
	size_t next;
};

static bool readScriptedAnswers( const char *file, ScriptedAnswers &script )
{
	FILE *f = fopen( file, "r" );
	if (f == NULL)
		return false;
	char line[ 4096 ];
	while (fgets( line, sizeof( line ), f ) != NULL)
	{
		size_t n = strlen( line );
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		script.answers.push_back( line );
	}
	fclose( f );
	script.next = 0;
	return true;
}

static bool chooseFolderFromScript( PosixInstallQuestion question, const std::string &why, std::string &chosen, void *context )
{
	ScriptedAnswers &script = *(ScriptedAnswers *)context;
	if (question == CHOOSE_GENERALS)
		fprintf( stderr, "generals: chooser (test answers): asked for the Generals folder: %s\n", why.c_str() );
	else if (!why.empty())
		fprintf( stderr, "generals: chooser (test answers): refused, asking again: %s\n", why.c_str() );
	if (script.next >= script.answers.size() || strcasecmp( script.answers[script.next].c_str(), "cancel" ) == 0)
	{
		fprintf( stderr, "generals: chooser (test answers): cancel\n" );
		return false;
	}
	chosen = script.answers[script.next++];
	fprintf( stderr, "generals: chooser (test answers): answer %s\n", chosen.c_str() );
	return true;
}

/** The install root (P1 step 4, PosixInstallRoot.h): "-root <dir>"; else Registry.ini's InstallPath while
	* it still holds the game; else, inside an app bundle, the known places and then the player's own choice
	* (never in an unattended run: -headless or ZH_UNATTENDED), which is written to Registry.ini's
	* InstallPath so it is asked once; else the executable's directory.  Read from argv itself rather than
	* EarlyCommandLine.h, whose values end at a space, because a path may have one. */
static Bool chooseInstallRoot( int argc, char *argv[], const std::vector<std::string> &overlays, char *out, size_t outSize )
{
	PosixInstallRequest request;
	Bool unattended = FALSE;
	for (int i = 1; i < argc; ++i)
	{
		request.arguments.push_back( argv[i] );
		if (strcasecmp( argv[i], "-headless" ) == 0)
			unattended = TRUE;
	}
	// ZH_UNATTENDED too: a scripted run that draws has nobody to answer a chooser or a box either.
	if (unattendedByEnvironment())
		unattended = TRUE;
	char buffer[ 4096 ];
	getExecutableDirectory( buffer, sizeof( buffer ), FALSE );
	request.executableDirectory = buffer;
	request.insideAppBundle = isExecutablePackaged() != FALSE;		// a macOS app, or a Linux package (P3)
	request.home = findHomeDirectory( buffer, sizeof( buffer ) ) ? buffer : "";
	if (isExecutableInAppBundle())
		request.forbidden.push_back( request.executableDirectory + "/../.." );	// the bundle
	else if (request.insideAppBundle)
		request.forbidden.push_back( request.executableDirectory + "/.." );		// the Linux package
	request.forbidden.insert( request.forbidden.end(), overlays.begin(), overlays.end() );
	request.registryFile = findRegistryFile( buffer, sizeof( buffer ) ) ? buffer : "";
	std::string dialogFailure;
	const bool gameMode = inSteamGameMode();
	request.chooser = unattended || gameMode ? NULL : chooseFolderWithSdl;
	request.chooserContext = &dialogFailure;
	ScriptedAnswers script;
	const char *answers = getenv( "ZH_TEST_CHOOSER_ANSWERS" );
	const bool scripted = answers != NULL && answers[0] != 0;
	if (scripted)
	{
		if (!readScriptedAnswers( answers, script ))
		{
			fprintf( stderr, "generals: cannot read ZH_TEST_CHOOSER_ANSWERS %s\n", answers );
			return FALSE;
		}
		request.insideAppBundle = true;		// a packaged first launch, whatever the executable's place
		request.chooser = chooseFolderFromScript;
		request.chooserContext = &script;
	}

	PosixInstallChoice choice;
	if (!PosixChooseInstallRoot( request, choice ))
	{
		// the dialog's own failure, when it failed, says more than "none was chosen"; Game Mode has no dialog
		std::string problem = dialogFailure.empty() ? choice.problem
			: dialogFailure + "\n\nStart the game with -root <folder> to name the Zero Hour folder instead.";
		if (gameMode && !unattended && !scripted)
			problem = GAME_MODE_NO_ROOT;
		fprintf( stderr, "generals: %s\n", problem.c_str() );
		if (request.insideAppBundle && !unattended && !scripted)
		{
			activateThisApp();
			SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Zero Hour Reforged", problem.c_str(), NULL );
		}
		return FALSE;
	}
	if (choice.writeInstallPath && !writeRegistryFile( registryFileKey( "", AsciiString::TheEmptyString,
			AsciiString( "InstallPath" ) ), AsciiString( choice.root.c_str() ) ))
		fprintf( stderr, "generals: could not remember %s in Registry.ini; it will be asked for again\n", choice.root.c_str() );
	if (choice.writeGeneralsInstallPath && !writeRegistryFile( registryFileKey( "Generals\\", AsciiString::TheEmptyString,
			AsciiString( "InstallPath" ) ), AsciiString( choice.generals.c_str() ) ))
		fprintf( stderr, "generals: could not remember %s in Registry.ini; it will be asked for again\n", choice.generals.c_str() );
	if (choice.root.size() + 1 > outSize)
		return FALSE;
	strcpy( out, choice.root.c_str() );
	return TRUE;
}

/** A package's art overlay, the macOS app's or a Linux package's: "<user data>/ReforgedArt", when it is a
	* folder.  Neither ships the Reforged*.big (they are over a gigabyte), and the game does not download them:
	* whatever puts them in that folder (a launcher) is how a package gets the upscaled art. */
static void appendUserArtOverlay( std::vector<std::string> &overlays )
{
	char folder[ 4096 ];
	if (!findUserDataDirectory( folder, sizeof( folder ) ))
		return;
	std::string candidate( folder );
	while (!candidate.empty() && (candidate[candidate.size() - 1] == '\\' || candidate[candidate.size() - 1] == '/'))
		candidate.erase( candidate.size() - 1 );
	candidate += "/ReforgedArt";
	char real[ PATH_MAX ];
	struct stat status;
	if (realpath( candidate.c_str(), real ) != NULL && stat( real, &status ) == 0 && S_ISDIR( status.st_mode ))
		overlays.push_back( real );
}

/** The fork's overlay (P1, decision 9): read roots searched before the install for every relative path
	* that is read.  "-overlay <dir>", repeatable, in the order given; with none, the one a package puts
	* beside the executable: "<exe>/../Resources/Overlay" in a macOS app bundle, or
	* "<exe>/../share/zero-hour-reforged/overlay" in a Linux package, and after it the package's upscaled
	* art, which lives in the user data folder, never in the package, and is read from
	* "<user data>/ReforgedArt" when it is there (appendUserArtOverlay).  An unpacked build has neither and
	* runs on the install alone, as before.  Resolved to real paths here, before the chdir to the root,
	* so a relative -overlay means what it meant where the command was typed.  FALSE for an -overlay
	* that is not a directory. */
static Bool chooseOverlays( int argc, char *argv[], std::vector<std::string> &overlays )
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (strcasecmp( argv[i], "-overlay" ) != 0)
			continue;
		char real[ PATH_MAX ];
		struct stat status;
		if (realpath( argv[i + 1], real ) == NULL || stat( real, &status ) != 0 || !S_ISDIR( status.st_mode ))
		{
			fprintf( stderr, "generals: cannot use '%s' as an overlay: %s\n", argv[i + 1], strerror( errno ? errno : ENOTDIR ) );
			return FALSE;
		}
		overlays.push_back( real );
		++i;
	}
	if (!overlays.empty())
		return TRUE;

	char exe[ 4096 ];
	getExecutableDirectory( exe, sizeof( exe ), FALSE );
	static const char *const packaged[] = { "/../Resources/Overlay", "/../share/zero-hour-reforged/overlay" };
	for (size_t i = 0; exe[0] != 0 && i < sizeof( packaged ) / sizeof( packaged[0] ); ++i)
	{
		const std::string candidate = std::string( exe ) + packaged[i];
		char real[ PATH_MAX ];
		struct stat status;
		if (realpath( candidate.c_str(), real ) != NULL && stat( real, &status ) == 0 && S_ISDIR( status.st_mode ))
		{
			overlays.push_back( real );
			appendUserArtOverlay( overlays );
			break;
		}
	}
	return TRUE;
}

/** WinMain's one-copy guard: a named mutex there, an exclusive lock on a file in the user data directory
	* here, held for the process's life.  FALSE if another copy holds it. */
static Bool takeOneCopyLock( void )
{
	char path[ 4096 ];
	if (!findUserDataDirectory( path, sizeof( path ) ))
		return TRUE;	// nowhere to keep the lock: do not stop the game over it
	strncat( path, "Generals-" GENERALS_GUID ".lock", sizeof( path ) - strlen( path ) - 1 );
	const int fd = zh_open( path, O_CREAT | O_RDWR, 0644 );
	if (fd < 0)
		return TRUE;
	if (flock( fd, LOCK_EX | LOCK_NB ) != 0)
	{
		const Bool heldElsewhere = (errno == EWOULDBLOCK);
		close( fd );
		return heldElsewhere ? FALSE : TRUE;
	}
	return TRUE;	// the descriptor stays open, and the lock with it, until the process ends
}

// main =======================================================================
/** Application entry point */
//=============================================================================
int main( int argc, char *argv[] )
{
	// Before anything else, and before another thread exists: a crash from here on leaves
	// ReleaseCrashInfo.txt, as WinMain's _set_se_translator and SetUnhandledExceptionFilter make it on Windows.
	installCrashHandlers();

	// The one locale category the game may set (a port rule; C2): dates in the replay and save
	// lists in the user's format.  LC_NUMERIC would change how the INI parser reads decimals.
	setlocale( LC_TIME, "" );

#ifdef __linux__
	// Before SDL first starts (the folder chooser may): Wayland and X11 name the window by this app id,
	// and desktops match it to the .desktop file of that name for the icon and the dock entry.  SDL's own
	// default is the executable's name, generals, which no .desktop file has.  The Flatpak's is its id.
	const char *flatpakId = getenv( "FLATPAK_ID" );
	SDL_SetAppMetadata( "Zero Hour Reforged", VERSION_STRING,
		flatpakId != NULL && flatpakId[0] != 0 ? flatpakId : "zero-hour-reforged" );
#endif

	try {

		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		std::vector<std::string> overlays;
		if (!chooseOverlays( argc, argv, overlays ))
			return 1;
		char root[ 4096 ];
		root[0] = 0;
		if (!chooseInstallRoot( argc, argv, overlays, root, sizeof( root ) ))
			return 1;		// it has said why
		if (chdir( root ) != 0)
		{
			fprintf( stderr, "generals: cannot use '%s' as the install root: %s\n", root, strerror( errno ) );
			return 1;
		}
		PosixPath_Set_Overlays( overlays );
		for (size_t i = 0; i < overlays.size(); ++i)
			fprintf( stderr, "generals: overlay %s, searched before the install\n", overlays[i].c_str() );
		// The roots are read-only (P1 step 2): nothing the engine addresses relative to the install, the
		// Data\INI\INIZH.big deletion in GameEngine::init included, can change it.  -writableRoot is a
		// harness's armed control, never a player's switch.
		Bool writableRoot = FALSE;
		for (int i = 1; i < argc; ++i)
			if (strcasecmp( argv[i], "-writableRoot" ) == 0)
				writableRoot = TRUE;
		PosixPath_Set_Root_Read_Only( !writableRoot );
		if (writableRoot)
			fprintf( stderr, "generals: -writableRoot: the install root is WRITABLE for this run\n" );

		// The window mode, as WinMain settles it: Options.ini's saved mode, then the command line over it.
		{
			const int savedMode = getEarlyOptionInt( "WindowMode", WINDOW_MODE_FULLSCREEN, 0, WINDOW_MODE_COUNT - 1 );
			s_windowRequest.borderless = (savedMode == WINDOW_MODE_BORDERLESS);
			s_windowRequest.windowed = (savedMode != WINDOW_MODE_FULLSCREEN);
		}
		for (int i = 1; i < argc; ++i)
		{
			if (strcasecmp( argv[i], "-win" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.borderless = FALSE;	// an explicit -win beats a borderless Options.ini
			}
			if (strcasecmp( argv[i], "-fullscreen" ) == 0)
			{
				s_windowRequest.windowed = FALSE;
				s_windowRequest.borderless = FALSE;
			}
			if (strcasecmp( argv[i], "-borderless" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.borderless = TRUE;
			}
			if (strcasecmp( argv[i], "-headless" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.headless = TRUE;
			}
			if (strcasecmp( argv[i], "-hiddenwindow" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.hidden = TRUE;
			}
			if (strcasecmp( argv[i], "-offscreen" ) == 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.offscreen = TRUE;
			}
		}
		/* -hiddenwindow, or ZH_HIDDEN_WINDOW=1: the game draws exactly as it does in a window - the device
			 renders into its own back buffer, which -screenshot, -video and the frame dumps read - but the
			 window is never shown.  It is for harnesses and automated runs, so they do not put windows in front
			 of the person using the machine.  A window means windowed: a hidden one never takes the display
			 fullscreen.  -headless, which draws nothing, is unaffected. */
		{
			const char *hidden = getenv( "ZH_HIDDEN_WINDOW" );
			if (hidden != NULL && hidden[0] != '\0' && strcmp( hidden, "0" ) != 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.hidden = TRUE;
			}
		}
		/* -offscreen, or ZH_OFFSCREEN=1: no window at all, for a host with no window server (a worker reached
			 over ssh, CI).  The game runs as it does in a hidden window - it is not -headless: every frame is
			 drawn - but into the device's own target, and nothing is presented to a display.  SdlGameEngine
			 starts SDL's video without a window and asks the device for an offscreen frame; like a hidden
			 window, it is silent.  ZH_OFFSCREEN_HZ=<n> paces the frames at n a second in place of vsync. */
		{
			const char *offscreen = getenv( "ZH_OFFSCREEN" );
			if (offscreen != NULL && offscreen[0] != '\0' && strcmp( offscreen, "0" ) != 0)
			{
				s_windowRequest.windowed = TRUE;
				s_windowRequest.offscreen = TRUE;
			}
		}

		// start the log
		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
		initMemoryManager();

		// Set up version info
		TheVersion = NEW Version;
		TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
			AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
			AsciiString(__TIME__), AsciiString(__DATE__));

		/* -multiInstance lets a second copy start, as on Windows: one copy at a time is right for a
			 player and wrong for a test, where a network game needs two processes on one machine. */
		const Bool oneCopyIsEnough = (findEarlyCommandLineOption( L"-multiInstance" ) == NULL);
		if (oneCopyIsEnough && !takeOneCopyLock())
		{
			DEBUG_LOG(("Generals is already running...Bail!\n"));
			delete TheVersion;
			TheVersion = NULL;
			shutdownMemoryManager();
			DEBUG_SHUTDOWN();
			return 0;
		}
		DEBUG_LOG(("Create GeneralsMutex okay.\n"));

		DEBUG_LOG(("CRC message is %d\n", GameMessage::MSG_LOGIC_CRC));

		// run the game main loop
		GameMain(argc, argv);
		SdlGameEngine_releaseWindow();		// after the engine, as WinMain's DestroyWindow

		delete TheVersion;
		TheVersion = NULL;

	#ifdef MEMORYPOOL_DEBUG
		TheMemoryPoolFactory->debugMemoryReport(REPORT_POOLINFO | REPORT_POOL_OVERFLOW | REPORT_SIMPLE_LEAKS, 0, 0);
	#endif
	#if defined(_DEBUG) || defined(_INTERNAL)
		TheMemoryPoolFactory->memoryPoolUsageReport("AAAMemStats");
	#endif

		// close the log
		shutdownMemoryManager();
		DEBUG_SHUTDOWN();
	}
	// As WinMain: name what escaped the engine, and leave through ReleaseCrash so no destructor runs.
	catch (INIException e)
	{
		RELEASE_CRASH((e.mFailureMessage ? e.mFailureMessage : "Uncaught INI exception in main"));
	}
	catch (ErrorCode ec)
	{
		char why[ 64 ];
		snprintf( why, sizeof(why), "Uncaught ErrorCode 0x%08x in main", (UnsignedInt)ec );
		RELEASE_CRASH((why));
	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in main"));
	}

	TheUnicodeStringCriticalSection = NULL;
	TheDmaCriticalSection = NULL;
	TheMemoryPoolCriticalSection = NULL;

	return 0;

}  // end main

// CreateGameEngine ===========================================================
/** Create the game engine we're going to use: SDL3's, over C1's POSIX one */
//=============================================================================
GameEngine *CreateGameEngine( void )
{
	return NEW SdlGameEngine( s_windowRequest );
}
