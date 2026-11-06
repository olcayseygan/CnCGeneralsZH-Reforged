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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: MainMenuUtils.cpp
// Author: Matthew D. Campbell, Sept 2002
// Description: GameSpy version check, patch download, etc utils
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"

#include <fcntl.h>
#if !defined(_WIN32)
#include <netdb.h>			// gethostbyname, for the patch server lookup
#include <sys/stat.h>
#include <thread>				// the lookup's worker, where Windows uses CreateThread
#include <unistd.h>
#endif

//#include "Common/Registry.h"
#include "Common/UserPreferences.h"
#include "Common/version.h"
#include "GameClient/GameText.h"
#include "GameClient/MessageBox.h"
#include "GameClient/Shell.h"
#include "GameLogic/ScriptEngine.h"

#include "GameClient/ShellHooks.h"

#include "gamespy/ghttp/ghttp.h"
// Must follow ghttp.h: gsavailable.h uses gsi_char without including gsplatform.h.
#include "gamespy/gsavailable.h"

#include "GameNetwork/DownloadManager.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "Lib/Clock.h"
#include "GameNetwork/GeneralsOnline/OnlineBridge.h"

#include "WWDownload/Registry.h"
#include "WWDownload/urlBuilder.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

///////////////////////////////////////////////////////////////////////////////////////

static Bool checkingForPatchBeforeGameSpy = FALSE;
static Int checksLeftBeforeOnline = 0;

// The GameSpy SDK checks this before every backend call: peerInitialize() returns NULL until it is
// GSIACAvailable, and nothing here ever ran the check, so going online faulted on that NULL peer.
static GSIACResult availableCheckResult = GSIACWaiting;
// Kept apart from the result so a cancelled check cannot decrement checksLeftBeforeOnline after
// CancelPatchCheckCallback() has reset it.
static Bool availableCheckInProgress = FALSE;

static Int timeThroughOnline = 0; // used to avoid having old callbacks cause problems
static Bool mustDownloadPatch = FALSE;
static Bool cantConnectBeforeOnline = FALSE;
static std::list<QueuedDownload> queuedDownloads;

static char *MOTDBuffer = NULL;
static char *configBuffer = NULL;
GameWindow *onlineCancelWindow = NULL;

static Bool s_asyncDNSThreadDone = TRUE;
static Bool s_asyncDNSThreadSucceeded = FALSE;
static Bool s_asyncDNSLookupInProgress = FALSE;
#if defined(_WIN32)
static HANDLE s_asyncDNSThreadHandle = NULL;
#endif
enum {
	LOOKUP_INPROGRESS,
	LOOKUP_FAILED,
	LOOKUP_SUCCEEDED,
};

///////////////////////////////////////////////////////////////////////////////////////

static void startOnline( void );
static void reallyStartPatchCheck( void );

/* -online: no patch check and none of EA's HTTP.  StartPatchCheck dials the service and HTTPThinkWrapper
	 waits for its welcome, whose config (<QMMaps>) and MOTD stand in for the two files EA fetched. */
static Bool s_onlineWelcomeWait = FALSE;
static UnsignedInt s_onlineWelcomeSince = 0;
static const UnsignedInt ONLINE_WELCOME_TIMEOUT_MS = 15000;

///////////////////////////////////////////////////////////////////////////////////////

// someone has hit a button allowing downloads to start
void StartDownloadingPatches( void )
{
	if (queuedDownloads.empty())
	{
		HandleCanceledDownload();
		return;
	}

	WindowLayout *layout;
	layout = TheWindowManager->winCreateLayout( AsciiString( "Menus/DownloadMenu.wnd" ) );
	layout->runInit();
	layout->hide( FALSE );
	layout->bringForward();
	HandleCanceledDownload(FALSE);
	DEBUG_ASSERTCRASH(TheDownloadManager, ("No download manager!"));
	if (TheDownloadManager)
	{
		std::list<QueuedDownload>::iterator it = queuedDownloads.begin();
		while (it != queuedDownloads.end())
		{
			QueuedDownload q = *it;
			TheDownloadManager->queueFileForDownload(q.server, q.userName, q.password,
				q.file, q.localFile, q.regKey, q.tryResume);
			queuedDownloads.pop_front();
			it = queuedDownloads.begin();
		}
		TheDownloadManager->downloadNextQueuedFile();
	}
}

///////////////////////////////////////////////////////////////////////////////////////

// user agrees to patch before going online
static void patchBeforeOnlineCallback( void )
{
	StartDownloadingPatches();
}

// user doesn't want to patch before going online
static void noPatchBeforeOnlineCallback( void )
{
	queuedDownloads.clear();
	if (mustDownloadPatch || cantConnectBeforeOnline)
	{
		// go back to normal
		HandleCanceledDownload();
	}
	else
	{
		// clear out unneeded downloads and go on
		startOnline();
	}
}

// the backend is unavailable: back to the menu
static void backendUnavailableCallback( void )
{
	HandleCanceledDownload();
}

///////////////////////////////////////////////////////////////////////////////////////

static Bool hasWriteAccess()
{
	const char* filename = "PatchAccessTest.txt";	

	zh_remove(filename);

#if defined(_WIN32)
	int handle = zh_open( filename, _O_CREAT | _O_RDWR, _S_IREAD | _S_IWRITE);
#else
	// Through zh_open like the Windows line, so a read-only install root (P1) answers "no write
	// access" here rather than being written to
	int handle = zh_open( filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR );
#endif
	if (handle == -1)
	{
		return false;
	}

#if defined(_WIN32)
	_close(handle);
#else
	close(handle);
#endif
	zh_remove(filename);
	
	unsigned int val;
	if (!GetUnsignedIntFromRegistry("", "Version", val))
	{
		return false;
	}

	if (!SetUnsignedIntInRegistry("", "Version", val))
	{
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////

static void startOnline( void )
{
	checkingForPatchBeforeGameSpy = FALSE;

	DEBUG_ASSERTCRASH(checksLeftBeforeOnline==0, ("starting online with pending callbacks"));
	if (onlineCancelWindow)
	{
		TheWindowManager->winDestroy(onlineCancelWindow);
		onlineCancelWindow = NULL;
	}

	if (cantConnectBeforeOnline)
	{
		MessageBoxOk(TheGameText->fetch("GUI:CannotConnectToServservTitle"),
			TheGameText->fetch("GUI:CannotConnectToServserv"),
			noPatchBeforeOnlineCallback);
		return;
	}
	if (queuedDownloads.size())
	{
		if (!hasWriteAccess())
		{
			MessageBoxOk(TheGameText->fetch("GUI:Error"),
				TheGameText->fetch("GUI:MustHaveAdminRights"),
				noPatchBeforeOnlineCallback);
		}
		else if (mustDownloadPatch)
		{
			MessageBoxOkCancel(TheGameText->fetch("GUI:PatchAvailable"),
				TheGameText->fetch("GUI:MustPatchForOnline"),
				patchBeforeOnlineCallback, noPatchBeforeOnlineCallback);
		}
		else
		{
			MessageBoxYesNo(TheGameText->fetch("GUI:PatchAvailable"),
				TheGameText->fetch("GUI:CanPatchForOnline"),
				patchBeforeOnlineCallback, noPatchBeforeOnlineCallback);
		}
		return;
	}

	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_SELECTED]);

	if (availableCheckResult != GSIACAvailable)
	{
		// The backend reported the title disabled; every GameSpy thread would fail to start.
		// noPatchBeforeOnlineCallback() would call startOnline() again and reopen this box.
		MessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"),
			TheGameText->fetch("GUI:GSDisconReason4"),
			backendUnavailableCallback);
		return;
	}

	DEBUG_ASSERTCRASH( !TheGameSpyBuddyMessageQueue, ("TheGameSpyBuddyMessageQueue exists!") );
	DEBUG_ASSERTCRASH( !TheGameSpyPeerMessageQueue, ("TheGameSpyPeerMessageQueue exists!") );
	DEBUG_ASSERTCRASH( !TheGameSpyInfo, ("TheGameSpyInfo exists!") );
	SetUpGameSpy(MOTDBuffer, configBuffer);
	if (MOTDBuffer)
	{
		delete[] MOTDBuffer;
		MOTDBuffer = NULL;
	}
	if (configBuffer)
	{
		delete[] configBuffer;
		configBuffer = NULL;
	}

#ifdef ALLOW_NON_PROFILED_LOGIN
	UserPreferences pref;
	pref.load("GameSpyLogin.ini");
	UserPreferences::const_iterator it = pref.find("useProfiles");
	if (it != pref.end() && it->second.compareNoCase("yes") == 0)
#endif ALLOW_NON_PROFILED_LOGIN
		TheShell->push( AsciiString("Menus/GameSpyLoginProfile.wnd") );
#ifdef ALLOW_NON_PROFILED_LOGIN
	else
		TheShell->push( AsciiString("Menus/GameSpyLoginQuick.wnd") );
#endif ALLOW_NON_PROFILED_LOGIN
}

///////////////////////////////////////////////////////////////////////////////////////

static void queuePatch(Bool mandatory, AsciiString downloadURL)
{
	QueuedDownload q;
	Bool success = TRUE;

	AsciiString connectionType;
	success &= downloadURL.nextToken(&connectionType, ":");

	AsciiString server;
	success &= downloadURL.nextToken(&server, ":/");

	AsciiString user;
	success &= downloadURL.nextToken(&user, ":@");

	AsciiString pass;
	success &= downloadURL.nextToken(&pass, "@/");

	AsciiString filePath;
	success &= downloadURL.nextToken(&filePath, "");

	if (!success && user.isNotEmpty())
	{
		// no user/pass combo - move the file into it's proper place
		filePath = user;
		user = ""; // LFeenanEA - Credentials removed as per Security requirements
		pass = "";
		success = TRUE;
	}

	AsciiString fileStr = filePath;
	const char *s = filePath.reverseFind('/');
	if (s)
		fileStr = s+1;
	AsciiString fileName = "patches\\";
	fileName.concat(fileStr);

	DEBUG_LOG(("download URL split: %d [%s] [%s] [%s] [%s] [%s] [%s]\n",
		success, connectionType.str(), server.str(), user.str(), pass.str(),
		filePath.str(), fileName.str()));

	if (!success)
		return;

	q.file = filePath;
	q.localFile = fileName;
	q.password = pass;
	q.regKey = "";
	q.server = server;
	q.tryResume = TRUE;
	q.userName = user;

	std::list<QueuedDownload>::iterator it = queuedDownloads.begin();
	while (it != queuedDownloads.end())
	{
		if (it->localFile == q.localFile)
			return; // don't add it if it exists already (because we can check multiple times)
		++it;
	}

	queuedDownloads.push_back(q);
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool motdCallback( GHTTPRequest request, GHTTPResult result,
															char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	if (MOTDBuffer)
	{
		delete[] MOTDBuffer;
		MOTDBuffer = NULL;
	}

	MOTDBuffer = NEW char[bufferLen];
	memcpy(MOTDBuffer, buffer, bufferLen);
	MOTDBuffer[bufferLen-1] = 0;

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
	if (onlineCancelWindow && !checksLeftBeforeOnline)
	{
		TheWindowManager->winDestroy(onlineCancelWindow);
		onlineCancelWindow = NULL;
	}

	DEBUG_LOG(("------- Got MOTD before going online -------\n"));
	DEBUG_LOG(("%s\n", (MOTDBuffer)?MOTDBuffer:""));
	DEBUG_LOG(("--------------------------------------------\n"));

	if (!checksLeftBeforeOnline)
		startOnline();

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool configCallback( GHTTPRequest request, GHTTPResult result,
																char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	if (configBuffer)
	{
		delete[] configBuffer;
		configBuffer = NULL;
	}

	if (result != GHTTPSuccess || bufferLen < 100)
	{
		if (!checkingForPatchBeforeGameSpy)
			return GHTTPTrue;
		--checksLeftBeforeOnline;
		if (onlineCancelWindow && !checksLeftBeforeOnline)
		{
			TheWindowManager->winDestroy(onlineCancelWindow);
			onlineCancelWindow = NULL;
		}
		cantConnectBeforeOnline = TRUE;
		if (!checksLeftBeforeOnline)
		{
			startOnline();
		}
		return GHTTPTrue;
	}

	configBuffer = NEW char[bufferLen];
	memcpy(configBuffer, buffer, bufferLen);
	configBuffer[bufferLen-1] = 0;

	AsciiString fname;
	fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
	FILE *fp = zh_fopen(fname.str(), "wb");
	if (fp)
	{
		fwrite(configBuffer, bufferLen, 1, fp);
		fclose(fp);
	}

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
	if (onlineCancelWindow && !checksLeftBeforeOnline)
	{
		TheWindowManager->winDestroy(onlineCancelWindow);
		onlineCancelWindow = NULL;
	}

	DEBUG_LOG(("Got Config before going online\n"));

	if (!checksLeftBeforeOnline)
		startOnline();

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool configHeadCallback( GHTTPRequest request, GHTTPResult result,
																		char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	DEBUG_LOG(("HTTP head resp: res=%d, len=%d, buf=[%s]\n", result, bufferLen, buffer));

	if (result == GHTTPSuccess)
	{
		DEBUG_LOG(("Headers are [%s]\n", ghttpGetHeaders( request )));

		AsciiString headers(ghttpGetHeaders( request ));
		AsciiString line;
		while (headers.nextToken(&line, "\n\r"))
		{
			AsciiString key, val;
			line.nextToken(&key, ": ");
			line.nextToken(&val, ": \r\n");

			if (key.compare("Content-Length") == 0 && val.isNotEmpty())
			{
				Int serverLen = atoi(val.str());
				Int fileLen = 0;
				AsciiString fname;
				fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
				FILE *fp = zh_fopen(fname.str(), "rb");
				if (fp)
				{
					fseek(fp, 0, SEEK_END);
					fileLen = ftell(fp);
					fclose(fp);
				}

				if (serverLen == fileLen)
				{
					// we don't need to download the MOTD again
					--checksLeftBeforeOnline;
					DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
					if (onlineCancelWindow && !checksLeftBeforeOnline)
					{
						TheWindowManager->winDestroy(onlineCancelWindow);
						onlineCancelWindow = NULL;
					}

					if (configBuffer)
					{
						delete[] configBuffer;
						configBuffer = NULL;
					}

					AsciiString fname;
					fname.format("%sGeneralsOnline\\Config.txt", TheGlobalData->getPath_UserData().str());
					FILE *fp = zh_fopen(fname.str(), "rb");
					if (fp)
					{
						configBuffer = NEW char[fileLen];
						fread(configBuffer, fileLen, 1, fp);
						configBuffer[fileLen-1] = 0;
						fclose(fp);

						DEBUG_LOG(("Got Config before going online\n"));

						if (!checksLeftBeforeOnline)
							startOnline();

						return GHTTPTrue;
					}
				}
			}
		}
	}

	// we need to download the MOTD again
	std::string gameURL, mapURL;
	std::string configURL, motdURL;
	FormatURLFromRegistry(gameURL, mapURL, configURL, motdURL);
	ghttpGet( configURL.c_str(), GHTTPFalse, configCallback, param );

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool gamePatchCheckCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	Int run = (Int)(intptr_t)param;
	if (run != timeThroughOnline)
	{
		DEBUG_CRASH(("Old callback being called!"));
		return GHTTPTrue;
	}

	--checksLeftBeforeOnline;
	DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));

	DEBUG_LOG(("Result=%d, buffer=[%s], len=%d\n", result, buffer, bufferLen));
	if (result != GHTTPSuccess)
	{
		if (!checkingForPatchBeforeGameSpy)
			return GHTTPTrue;
		cantConnectBeforeOnline = TRUE;
		if (!checksLeftBeforeOnline)
		{
			startOnline();
		}
		return GHTTPTrue;
	}

	AsciiString message = buffer;
	AsciiString line;
	while (message.nextToken(&line, "\r\n"))
	{
		AsciiString type, req, url;
		Bool ok = TRUE;
		ok &= line.nextToken(&type, " ");
		ok &= line.nextToken(&req, " ");
		ok &= line.nextToken(&url, " ");
		if (ok && type == "patch")
		{
			DEBUG_LOG(("Saw a patch: %d/[%s]\n", atoi(req.str()), url.str()));
			queuePatch( atoi(req.str()), url );
			if (atoi(req.str()))
			{
				mustDownloadPatch = TRUE;
			}
		}
		else if (ok && type == "server")
		{
		}
	}

	if (!checksLeftBeforeOnline)
	{
		startOnline();
	}

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

void CancelPatchCheckCallbackAndReopenDropdown( void )
{
	HandleCanceledDownload();
	CancelPatchCheckCallback();
}

void CancelPatchCheckCallback( void )
{
	if (s_onlineWelcomeWait)
	{
		s_onlineWelcomeWait = FALSE;
		OnlineDisconnect();
	}
	s_asyncDNSLookupInProgress = FALSE;
	if (availableCheckInProgress)
	{
		GSICancelAvailableCheck();
		availableCheckInProgress = FALSE;
	}
	HandleCanceledDownload(FALSE); // don't dropdown
	checkingForPatchBeforeGameSpy = FALSE;
	checksLeftBeforeOnline = 0;
	if (onlineCancelWindow)
	{
		TheWindowManager->winDestroy(onlineCancelWindow);
		onlineCancelWindow = NULL;
	}
	queuedDownloads.clear();
	if (MOTDBuffer)
	{
		delete[] MOTDBuffer;
		MOTDBuffer = NULL;
	}
	if (configBuffer)
	{
		delete[] configBuffer;
		configBuffer = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool overallStatsCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	DEBUG_LOG(("overallStatsCallback() - Result=%d, len=%d\n", result, bufferLen));
	if (result != GHTTPSuccess)
	{
		return GHTTPTrue;
	}

	HandleOverallStats( buffer, bufferLen );
	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

static GHTTPBool numPlayersOnlineCallback( GHTTPRequest request, GHTTPResult result, char * buffer, GHTTPByteCount bufferLen64, void * param )
{
	// the SDK widened its byte count to 64 bits; the body wants the old Int.
	Int bufferLen = (Int)bufferLen64;
	(void)bufferLen;	// only read by DEBUG_LOG in some of these, which Release compiles out
	DEBUG_LOG(("numPlayersOnlineCallback() - Result=%d, buffer=[%s], len=%d\n", result, buffer, bufferLen));
	if (result != GHTTPSuccess)
	{
		return GHTTPTrue;
	}

	AsciiString message = buffer;
	message.trim();
	const char *s = message.reverseFind('\\');
	if (!s)
	{
		return GHTTPTrue;
	}

	if (*s == '\\')
		++s;

	DEBUG_LOG(("Message was '%s', trimmed to '%s'=%d\n", buffer, s, atoi(s)));
	HandleNumPlayersOnline(atoi(s));

	return GHTTPTrue;
}

///////////////////////////////////////////////////////////////////////////////////////

void CheckOverallStats( void )
{
	ghttpGet("http://gamestats.gamespy.com/ccgenzh/display.html",
		GHTTPFalse, overallStatsCallback, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////

void CheckNumPlayersOnline( void )
{
	ghttpGet("http://launch.gamespyarcade.com/software/launch/arcadecount2.dll?svcname=ccgenzh",
		GHTTPFalse, numPlayersOnlineCallback, NULL);
}

///////////////////////////////////////////////////////////////////////////////////////

#if defined(_WIN32)
DWORD WINAPI asyncGethostbynameThreadFunc( void * szName )
{
	HOSTENT *he = gethostbyname( (const char *)szName );

	if (he)
	{
		s_asyncDNSThreadSucceeded = TRUE;
	}
	else
	{
		s_asyncDNSThreadSucceeded = FALSE;
	}

	s_asyncDNSThreadDone = TRUE;
	return 0;
}
#else
/* The same lookup, on a std::thread where Windows uses CreateThread: gethostbyname is POSIX too.  The
	 two flags are shared with the polling below exactly as they are on Windows, unsynchronised; the
	 host is a dead service's and the answer only decides whether to offer a patch check. */
static void asyncGethostbynameThreadFunc( const char *szName )
{
	struct hostent *he = gethostbyname( szName );
	s_asyncDNSThreadSucceeded = (he != NULL) ? TRUE : FALSE;
	s_asyncDNSThreadDone = TRUE;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////

int asyncGethostbyname(char * szName)
{
	static int            stat = 0;
#if defined(_WIN32)
	static unsigned long  threadid;
#endif

	if( stat == 0 )
	{
		/* Kick off gethostname thread */
		s_asyncDNSThreadDone = FALSE;
#if defined(_WIN32)
		s_asyncDNSThreadHandle = CreateThread( NULL, 0, asyncGethostbynameThreadFunc, szName, 0, &threadid );

		if( s_asyncDNSThreadHandle == NULL )
		{
			return( LOOKUP_FAILED );
		}
#else
		try
		{
			std::thread( asyncGethostbynameThreadFunc, (const char *)szName ).detach();
		}
		catch( ... )		// a thread that cannot start, as a NULL handle from CreateThread above
		{
			return( LOOKUP_FAILED );
		}
#endif
		stat = 1;
	}
	if( stat == 1 )
	{
		if( s_asyncDNSThreadDone )
		{
			/* Thread finished */
			stat = 0;
			s_asyncDNSLookupInProgress = FALSE;
#if defined(_WIN32)
			s_asyncDNSThreadHandle = NULL;
#endif
			return( (s_asyncDNSThreadSucceeded)?LOOKUP_SUCCEEDED:LOOKUP_FAILED );
		}
	}

	return( LOOKUP_INPROGRESS );
}

///////////////////////////////////////////////////////////////////////////////////////

// GameSpy's HTTP SDK has had at least 1 crash bug, so we're going to just bail and
// never try again if they crash us.  We won't be able to get back online again (we'll
// time out) but at least we'll live.
static Bool isHttpOk = TRUE;

/// -online: the welcome arrived, so the queues can be made and the login screen pushed
static void startOnlineService( void )
{
	checkingForPatchBeforeGameSpy = FALSE;
	if (onlineCancelWindow)
	{
		TheWindowManager->winDestroy(onlineCancelWindow);
		onlineCancelWindow = NULL;
	}
	TheScriptEngine->signalUIInteract(TheShellHookNames[SHELL_SCRIPT_HOOK_MAIN_MENU_ONLINE_SELECTED]);
	SetUpGameSpy( OnlineGetMOTD().str(), OnlineGetConfig().str() );
	DEBUG_LOG(("ONLINE CONNECT ok\n"));
	TheShell->push( AsciiString("Menus/GameSpyLoginProfile.wnd") );
}

static void onlineCannotConnect( void )
{
	HandleCanceledDownload();
}

void HTTPThinkWrapper( void )
{
	if (s_onlineWelcomeWait)
	{
		OnlineUpdate();
		const OnlineStatus status = OnlineGetStatus();
		if (status == ONLINE_WELCOMED)
		{
			s_onlineWelcomeWait = FALSE;
			startOnlineService();
		}
		else if (status == ONLINE_FAILED || status == ONLINE_OFF
			|| Clock_Milliseconds() - s_onlineWelcomeSince > ONLINE_WELCOME_TIMEOUT_MS)
		{
			s_onlineWelcomeWait = FALSE;
			checkingForPatchBeforeGameSpy = FALSE;
			OnlineDisconnect();
			if (onlineCancelWindow)
			{
				TheWindowManager->winDestroy(onlineCancelWindow);
				onlineCancelWindow = NULL;
			}
			DEBUG_LOG(("ONLINE CONNECT fail status=%d\n", (Int)status));
			MessageBoxOk(TheGameText->fetch("GUI:CannotConnectToServservTitle"),
				TheGameText->fetch("GUI:CannotConnectToServserv"), onlineCannotConnect);
		}
		return;
	}

	if (s_asyncDNSLookupInProgress)
	{
		Int ret = asyncGethostbyname("servserv.generals.ea.com");
		switch(ret)
		{
		case LOOKUP_FAILED:
			cantConnectBeforeOnline = TRUE;
			startOnline();
			break;
		case LOOKUP_SUCCEEDED:
			reallyStartPatchCheck();
			break;
		}
	}

	// GSIAvailableCheckThink() is what advances the check; it has to be called until it stops
	// returning GSIACWaiting.  It counts as one of checksLeftBeforeOnline, so startOnline() waits
	// for it the same way it waits for the HTTP fetches.
	if (availableCheckInProgress)
	{
		availableCheckResult = GSIAvailableCheckThink();
		if (availableCheckResult != GSIACWaiting)
		{
			availableCheckInProgress = FALSE;
			--checksLeftBeforeOnline;
			DEBUG_ASSERTCRASH(checksLeftBeforeOnline>=0, ("Too many callbacks"));
			if (onlineCancelWindow && checksLeftBeforeOnline == 0)
			{
				TheWindowManager->winDestroy(onlineCancelWindow);
				onlineCancelWindow = NULL;
			}

			DEBUG_LOG(("Availability check returned %d\n", availableCheckResult));

			if (checksLeftBeforeOnline == 0)
				startOnline();
		}
	}

	if (isHttpOk)
	{
		try
		{
			ghttpThink();
		}
		catch (...)
		{
			isHttpOk = FALSE; // we can't abort the login, since we might be done with the
												// required checks and are fetching extras.  If it is a required
												// check, we'll time out normally.
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////

void StopAsyncDNSCheck( void )
{
#if defined(_WIN32)
	if (s_asyncDNSThreadHandle)
	{
#ifdef DEBUG_CRASHING
		Int res =
#endif
			TerminateThread(s_asyncDNSThreadHandle,0);
		DEBUG_ASSERTCRASH(res, ("Could not terminate the Async DNS Lookup thread!"));	// Thread still not killed!
	}
	s_asyncDNSThreadHandle = NULL;
#endif
	// Off Windows the lookup's thread is detached and cannot be killed: it finishes its lookup and
	// sets flags that nothing is polling for any more.  Its host name is a string literal, so nothing
	// it reads goes away under it.
	s_asyncDNSLookupInProgress = FALSE;
}

///////////////////////////////////////////////////////////////////////////////////////

void StartPatchCheck( void )
{
#ifdef ZH_ONLINE
	if (OnlineBridge_IsConfigured())
	{
		OnlineBridge_BeginLogin();
		return;
	}
#endif
	checkingForPatchBeforeGameSpy = TRUE;
	cantConnectBeforeOnline = FALSE;
	timeThroughOnline++;
	checksLeftBeforeOnline = 0;

	onlineCancelWindow = MessageBoxCancel(TheGameText->fetch("GUI:CheckingForPatches"),
		TheGameText->fetch("GUI:CheckingForPatches"), CancelPatchCheckCallbackAndReopenDropdown);

	// -online: dial our service instead of EA's patch, config and MOTD servers (HTTPThinkWrapper waits)
	if (OnlineIsEnabled())
	{
		s_onlineWelcomeWait = TRUE;
		s_onlineWelcomeSince = Clock_Milliseconds();
		OnlineUpdate();
		return;
	}

	s_asyncDNSLookupInProgress = TRUE;
	Int ret = asyncGethostbyname("servserv.generals.ea.com");
	switch(ret)
	{
	case LOOKUP_FAILED:
		cantConnectBeforeOnline = TRUE;
		startOnline();
		break;
	case LOOKUP_SUCCEEDED:
		reallyStartPatchCheck();
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////

static void reallyStartPatchCheck( void )
{
	checksLeftBeforeOnline = 5;  // the four ghttp calls below, plus the availability check

	GSICancelAvailableCheck();  // going online can be retried; do not leak the last attempt's socket
	availableCheckResult = GSIACWaiting;
	availableCheckInProgress = TRUE;
	GSIStartAvailableCheck("ccgenzh");

	std::string gameURL, mapURL;
	std::string configURL, motdURL;

	FormatURLFromRegistry(gameURL, mapURL, configURL, motdURL);

	std::string proxy;
	if (GetStringFromRegistry("", "Proxy", proxy))
	{
		if (!proxy.empty())
		{
			ghttpSetProxy(proxy.c_str());
		}
	}

	// check for a patch first
	DEBUG_LOG(("Game patch check: [%s]\n", gameURL.c_str()));
	DEBUG_LOG(("Map patch check: [%s]\n", mapURL.c_str()));
	DEBUG_LOG(("Config: [%s]\n", configURL.c_str()));
	DEBUG_LOG(("MOTD: [%s]\n", motdURL.c_str()));
	ghttpGet(gameURL.c_str(), GHTTPFalse, gamePatchCheckCallback, (void *)(intptr_t)timeThroughOnline);
	ghttpGet(mapURL.c_str(), GHTTPFalse, gamePatchCheckCallback, (void *)(intptr_t)timeThroughOnline);
	ghttpHead(configURL.c_str(), GHTTPFalse, configHeadCallback, (void *)(intptr_t)timeThroughOnline);
	ghttpGet(motdURL.c_str(), GHTTPFalse, motdCallback, (void *)(intptr_t)timeThroughOnline);
	
	// check total game stats
	CheckOverallStats();

	// check the users online
	CheckNumPlayersOnline();
}

///////////////////////////////////////////////////////////////////////////////////////
