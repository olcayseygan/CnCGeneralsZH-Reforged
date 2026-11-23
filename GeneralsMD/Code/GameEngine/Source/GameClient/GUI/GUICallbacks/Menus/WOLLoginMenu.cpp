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
// FILE: WOLLoginMenu.cpp
// Author: Chris Huybregts, November 2001
// Description: Lan Lobby Menu
///////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "zhio.h"
#include "Lib/Clock.h"

#include "Lib/WideCharFns.h"

#include "Common/STLTypedefs.h"

#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/QuotedPrintable.h"
#include "Common/Registry.h"
#include "Common/UserPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/UIStyle.h"
#include "GameClient/MessageBox.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PingThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"

#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/Online/OnlineQueues.h"
#include "GameNetwork/Transport.h"

#include "GameNetwork/WOLBrowser/WebBrowser.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#ifdef ALLOW_NON_PROFILED_LOGIN
Bool GameSpyUseProfiles = false;
#endif // ALLOW_NON_PROFILED_LOGIN

static Bool webBrowserActive = FALSE;
static Bool useWebBrowserForTOS = FALSE;

static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;

static const UnsignedInt loginTimeoutInMS = 10000;
static UnsignedInt loginAttemptTime = 0;

class GameSpyLoginPreferences : public UserPreferences
{
public:
	GameSpyLoginPreferences() { m_emailPasswordMap.clear(); m_emailNickMap.clear(); }
	virtual ~GameSpyLoginPreferences() { m_emailPasswordMap.clear(); m_emailNickMap.clear(); }

	virtual Bool load(AsciiString fname);
	virtual Bool write(void);

	AsciiString getPasswordForEmail( AsciiString email );
	AsciiString getDateForEmail( AsciiString email, AsciiString &month, AsciiString &date, AsciiString &year  );
	AsciiStringList getNicksForEmail( AsciiString email );
	void addLogin( AsciiString email, AsciiString nick, AsciiString password, AsciiString date );
	void forgetLogin( AsciiString email );
	AsciiStringList getEmails( void );

private:
	typedef std::map<AsciiString, AsciiString> PassMap;
	typedef std::map<AsciiString, AsciiString> DateMap;
	typedef std::map<AsciiString, AsciiStringList> NickMap;
	PassMap m_emailPasswordMap;
	NickMap m_emailNickMap;
	DateMap m_emailDateMap;
};

static AsciiString obfuscate( AsciiString in )
{
	char *buf = NEW char[in.getLength() + 1];
	strcpy(buf, in.str());
	static const char *xorKey = "1337Munkee";		// not "xor": that is a C++ keyword, the ^ operator's other spelling
	char *c = buf;
	const char *c2 = xorKey;
	while (*c)
	{
		if (!*c2)
			c2 = xorKey;
		// EA wrote `*c = *c++ ^ *c2++`, unsequenced: VC6 stored to the old c, VS2022 x64 to the new one,
		// which overwrote the terminator and ran on through the heap until the login screen crashed.
		if (*c != *c2)
			*c ^= *c2;
		c++, c2++;
	}
	AsciiString out = buf;
	delete[] buf;		// NEW char[] above; `delete buf` was a mismatch both CRTs tolerated
	return out;
}

Bool GameSpyLoginPreferences::load( AsciiString fname )
{
	if (!UserPreferences::load(fname))
		return false;

	UserPreferences::iterator upIt = begin();
	while (upIt != end())
	{
		AsciiString key = upIt->first;
		if (key.startsWith("pass_"))
		{
			AsciiString email, pass;
			email = key.str() + 5;
			pass = upIt->second;

			AsciiString quoPass = QuotedPrintableToAsciiString(pass);
			pass = obfuscate(quoPass);

			m_emailPasswordMap[email] = pass;
		}
		if (key.startsWith("date_"))
		{
			AsciiString email, date;
			email = key.str() + 5;
			date = upIt->second;

			date = QuotedPrintableToAsciiString(date);

			m_emailDateMap[email] = date;
		}
		else if (key.startsWith("nick_"))
		{
			AsciiString email, nick, nicks;
			email = key.str() + 5;
			nicks = upIt->second;
			while (nicks.nextToken(&nick, ","))
			{
				m_emailNickMap[email].push_back(nick);
			}
		}
		++upIt;
	}

	return true;
}

Bool GameSpyLoginPreferences::write( void )
{
	if (m_filename.isEmpty())
		return false;

	FILE *fp = zh_fopen(m_filename.str(), "w");
	if (fp)
	{
		fprintf(fp, "lastEmail = %s\n",   ((*this)["lastEmail"].str()));
		fprintf(fp, "lastName = %s\n",    ((*this)["lastName"].str()));
		fprintf(fp, "useProfiles = %s\n", ((*this)["useProfiles"].str()));
		PassMap::iterator passIt = m_emailPasswordMap.begin();
		while (passIt != m_emailPasswordMap.end())
		{
			AsciiString pass = obfuscate(passIt->second);
			AsciiString quoPass = AsciiStringToQuotedPrintable(pass);

			fprintf(fp, "pass_%s = %s\n", passIt->first.str(), quoPass.str());
			++passIt;
		}
		
		PassMap::iterator dateIt = m_emailDateMap.begin();
		while (dateIt != m_emailDateMap.end())
		{
			AsciiString date = AsciiStringToQuotedPrintable(dateIt->second);

			fprintf(fp, "date_%s = %s\n", dateIt->first.str(), date.str());
			++dateIt;
		}

		NickMap::iterator nickIt = m_emailNickMap.begin();
		while (nickIt != m_emailNickMap.end())
		{
			AsciiString nicks;
			AsciiStringListIterator listIt = nickIt->second.begin();
			while (listIt != nickIt->second.end())
			{
				nicks.concat(*listIt);
				nicks.concat(',');
				++listIt;
			}
			fprintf(fp, "nick_%s = %s\n", nickIt->first.str(), nicks.str());
			++nickIt;
		}

		fclose(fp);
		return true;
	}
	return false;
}
AsciiString GameSpyLoginPreferences::getDateForEmail( AsciiString email, AsciiString &month, AsciiString &date, AsciiString &year )
{
	if ( m_emailDateMap.find(email) == m_emailDateMap.end() )
		return AsciiString::TheEmptyString;
	AsciiString fullDate = m_emailDateMap[email];
	if(fullDate.getLength() != 8)
		return AsciiString::TheEmptyString;
	month.format("%c%c", fullDate.getCharAt(0), fullDate.getCharAt(1));
	date.format("%c%c", fullDate.getCharAt(2), fullDate.getCharAt(3));
	year.format("%c%c%c%c", fullDate.getCharAt(4), fullDate.getCharAt(5), fullDate.getCharAt(6), fullDate.getCharAt(7));
	return m_emailDateMap[email];
}

AsciiString GameSpyLoginPreferences::getPasswordForEmail( AsciiString email )
{
	if ( m_emailPasswordMap.find(email) == m_emailPasswordMap.end() )
		return AsciiString::TheEmptyString;
	return m_emailPasswordMap[email];
}

AsciiStringList GameSpyLoginPreferences::getNicksForEmail( AsciiString email )
{
	if ( m_emailNickMap.find(email) == m_emailNickMap.end() )
	{
		AsciiStringList empty;
		return empty;
	}
	return m_emailNickMap[email];
}

void GameSpyLoginPreferences::addLogin( AsciiString email, AsciiString nick, AsciiString password, AsciiString date  )
{
	if ( std::find(m_emailNickMap[email].begin(), m_emailNickMap[email].end(), nick) == m_emailNickMap[email].end() )
		m_emailNickMap[email].push_back(nick);
	m_emailPasswordMap[email] = password;
	m_emailDateMap[email] = date;
}

void GameSpyLoginPreferences::forgetLogin( AsciiString email )
{
	m_emailNickMap.erase(email);
	m_emailPasswordMap.erase(email);
	m_emailDateMap.erase(email);

}

AsciiStringList GameSpyLoginPreferences::getEmails( void )
{
	AsciiStringList theList;
	NickMap::iterator it = m_emailNickMap.begin();
	while (it != m_emailNickMap.end())
	{
		theList.push_back(it->first);
		++it;
	}
	return theList;
}

static const char *PREF_FILENAME = "GameSpyLogin.ini";
static GameSpyLoginPreferences *loginPref = NULL;

/* -online.  The login that went out: its email keys the remembered session token, and the relay
	 handshake waits for this login's ticket, never one an earlier login left behind. */
static Bool s_rememberLogin = FALSE;
static Bool s_onlineLoginPending = FALSE;
static Bool s_onlineRelayTried = FALSE;
static AsciiString s_onlineLoginEmail;
static UnsignedByte s_onlineUsedTicket[16];

static void onlineLoginSent( const AsciiString &email, const char *how )
{
	s_onlineLoginPending = TRUE;
	s_onlineRelayTried = FALSE;
	s_onlineLoginEmail = email;
	DEBUG_LOG(("ONLINE LOGIN sent how=%s\n", how));
}

/// The UDP half of an -online login: HELLO_AUTH as soon as this login's ticket is in
static void onlineOpenRelayWhenTicketed( void )
{
	UnsignedByte ticket[16];
	if (!s_onlineLoginPending || s_onlineRelayTried || !OnlineGetRelayTicket( ticket )
		|| memcmp( ticket, s_onlineUsedTicket, sizeof(ticket) ) == 0)
		return;
	memcpy( s_onlineUsedTicket, ticket, sizeof(ticket) );
	s_onlineRelayTried = TRUE;
	if (Transport::openOnlineRelay())
	{
		const UnsignedInt vip = Transport::getRelayIP();
		DEBUG_LOG(("ONLINE RELAY ok vip=%d.%d.%d.%d\n", vip >> 24, (vip >> 16) & 0xff, (vip >> 8) & 0xff, vip & 0xff));
	}
	else
	{
		DEBUG_LOG(("ONLINE RELAY fail\n"));		// the login then times out and says it could not connect
	}
}


static void startPings( void )
{
	std::list<AsciiString> pingServers = TheGameSpyConfig->getPingServers();
	Int timeout = TheGameSpyConfig->getPingTimeoutInMs();
	Int reps = TheGameSpyConfig->getNumPingRepetitions();

	for (std::list<AsciiString>::const_iterator it = pingServers.begin(); it != pingServers.end(); ++it)
	{
		AsciiString pingServer = *it;
		PingRequest req;
		req.hostname = pingServer.str();
		req.repetitions = reps;
		req.timeout = timeout;
		ThePinger->addRequest(req);
	}
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void onlineLoginScreenDone( void );

static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );
	onlineLoginScreenDone();

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		if (loginPref)
		{
			loginPref->write();
			delete loginPref;
			loginPref = NULL;
		}
		TheShell->push(nextScreen);
	}
	else
	{
		DEBUG_ASSERTCRASH(loginPref != NULL, ("loginPref == NULL"));
		if (loginPref)
		{
			loginPref->write();
			delete loginPref;
			loginPref = NULL;
		}
	}

	nextScreen = NULL;

}  // end if


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLLoginID =						NAMEKEY_INVALID;
static NameKeyType buttonBackID =								NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonLoginID =							NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonCreateAccountID =			NAMEKEY_INVALID;	// profile, quick
static NameKeyType buttonUseAccountID =					NAMEKEY_INVALID;	// quick
static NameKeyType buttonDontUseAccountID =			NAMEKEY_INVALID;	// profile
static NameKeyType buttonTOSID =								NAMEKEY_INVALID;	// TOS
static NameKeyType parentTOSID =								NAMEKEY_INVALID;	// TOS Parent
static NameKeyType buttonTOSOKID =							NAMEKEY_INVALID;	// TOS
static NameKeyType listboxTOSID =								NAMEKEY_INVALID;	// TOS
static NameKeyType comboBoxEmailID =						NAMEKEY_INVALID;	// profile
static NameKeyType comboBoxLoginNameID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryLoginNameID =				NAMEKEY_INVALID;	// quick
static NameKeyType textEntryPasswordID =				NAMEKEY_INVALID;	// profile
static NameKeyType checkBoxRememberPasswordID =	NAMEKEY_INVALID;	// checkbox to remember information or not
static NameKeyType textEntryMonthID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryDayID =				NAMEKEY_INVALID;	// profile
static NameKeyType textEntryYearID =				NAMEKEY_INVALID;	// profile

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLLogin =						NULL;
static GameWindow *buttonBack =								NULL;
static GameWindow *buttonLogin =							NULL;
static GameWindow *buttonCreateAccount =			NULL;
static GameWindow *buttonUseAccount =					NULL;
static GameWindow *buttonDontUseAccount =			NULL;
static GameWindow *buttonTOS						=			NULL;
static GameWindow *parentTOS						=			NULL;
static GameWindow *buttonTOSOK					=			NULL;
static GameWindow *listboxTOS						=			NULL;
static GameWindow *comboBoxEmail =						NULL;
static GameWindow *comboBoxLoginName =				NULL;
static GameWindow *textEntryLoginName =				NULL;
static GameWindow *textEntryPassword =				NULL;
static GameWindow *checkBoxRememberPassword =	NULL;
static GameWindow *textEntryMonth =				NULL;
static GameWindow *textEntryDay =				NULL;
static GameWindow *textEntryYear =				NULL;

void EnableLoginControls( Bool state )
{
	if (buttonLogin)
		buttonLogin->winEnable(state);
	if (buttonCreateAccount)
		buttonCreateAccount->winEnable(state);
	if (buttonUseAccount)
		buttonUseAccount->winEnable(state);
	if (buttonDontUseAccount)
		buttonDontUseAccount->winEnable(state);
	if (comboBoxEmail)
		comboBoxEmail->winEnable(state);
	if (comboBoxLoginName)
		comboBoxLoginName->winEnable(state);
	if (textEntryLoginName)
		textEntryLoginName->winEnable(state);
	if (textEntryPassword)
		textEntryPassword->winEnable(state);
	if (checkBoxRememberPassword)
		checkBoxRememberPassword->winEnable(state);
	if( buttonTOS )
		buttonTOS->winEnable(state);

	if (textEntryMonth)
		textEntryMonth->winEnable(state);
	if (textEntryDay)
		textEntryDay->winEnable(state);
	if( textEntryYear )
		textEntryYear->winEnable(state);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the WOL Login Menu */
//-------------------------------------------------------------------------------------------------
static void onlineLoginScreen( void );

void WOLLoginMenuInit( WindowLayout *layout, void *userData )
{
	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	loginAttemptTime = 0;

	if (!loginPref)
	{
		loginPref = NEW GameSpyLoginPreferences;
		loginPref->load(PREF_FILENAME);
	}
	
	// if the ESRB warning is blank (other country) hide the box
	GameWindow *esrbTitle = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("GameSpyLoginProfile.wnd:StaticTextESRBTop") );
	GameWindow *esrbParent = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("GameSpyLoginProfile.wnd:ParentESRB") );
	if (esrbTitle && esrbParent)
	{
		if ( GadgetStaticTextGetText( esrbTitle ).getLength() < 2 )
		{
			esrbParent->winHide(TRUE);
		}
	}

	parentWOLLoginID =						TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:WOLLoginMenuParent" );
	buttonBackID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonBack" );
	buttonLoginID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonLogin" );
	buttonCreateAccountID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonCreateAccount" );
	buttonUseAccountID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonUseAccount" );
	buttonDontUseAccountID =			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonDontUseAccount" );
	buttonTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonTOS" );
	parentTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ParentTOS" );
	buttonTOSOKID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ButtonTOSOK" );
	listboxTOSID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ListboxTOS" );
	comboBoxEmailID =							TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ComboBoxEmail" );
	comboBoxLoginNameID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:ComboBoxLoginName" );
	textEntryLoginNameID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryLoginName" );
	textEntryPasswordID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryPassword" );
	checkBoxRememberPasswordID =	TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:CheckBoxRememberInfo" );
	textEntryMonthID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryMonth" );
	textEntryDayID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryDay" );
	textEntryYearID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginProfile.wnd:TextEntryYear" );

	parentWOLLogin =							TheWindowManager->winGetWindowFromId( NULL,  parentWOLLoginID );
	buttonBack =									TheWindowManager->winGetWindowFromId( NULL,  buttonBackID);
	buttonLogin =									TheWindowManager->winGetWindowFromId( NULL,  buttonLoginID);
	buttonCreateAccount =					TheWindowManager->winGetWindowFromId( NULL,  buttonCreateAccountID);
	buttonUseAccount =						TheWindowManager->winGetWindowFromId( NULL,  buttonUseAccountID);
	buttonDontUseAccount =				TheWindowManager->winGetWindowFromId( NULL,  buttonDontUseAccountID);
	buttonTOS =										TheWindowManager->winGetWindowFromId( NULL,  buttonTOSID);
	parentTOS =										TheWindowManager->winGetWindowFromId( NULL,  parentTOSID);
	buttonTOSOK =									TheWindowManager->winGetWindowFromId( NULL,  buttonTOSOKID);
	listboxTOS =									TheWindowManager->winGetWindowFromId( NULL,  listboxTOSID);
	comboBoxEmail =								TheWindowManager->winGetWindowFromId( NULL,  comboBoxEmailID);
	comboBoxLoginName =						TheWindowManager->winGetWindowFromId( NULL,  comboBoxLoginNameID);
	textEntryLoginName =					TheWindowManager->winGetWindowFromId( NULL,  textEntryLoginNameID);
	textEntryPassword =						TheWindowManager->winGetWindowFromId( NULL,  textEntryPasswordID);
	checkBoxRememberPassword =		TheWindowManager->winGetWindowFromId( NULL,  checkBoxRememberPasswordID);
	textEntryMonth =					TheWindowManager->winGetWindowFromId( NULL,  textEntryMonthID);
	textEntryDay =					TheWindowManager->winGetWindowFromId( NULL,  textEntryDayID);
	textEntryYear =					TheWindowManager->winGetWindowFromId( NULL,  textEntryYearID);

	GadgetTextEntrySetText(textEntryMonth, UnicodeString::TheEmptyString);
	
	GadgetTextEntrySetText(textEntryDay, UnicodeString::TheEmptyString);
	
	GadgetTextEntrySetText(textEntryYear, UnicodeString::TheEmptyString);



	GameWindowList tabList;
	tabList.push_front(comboBoxEmail);
	tabList.push_back(comboBoxLoginName);
	tabList.push_back(textEntryPassword);
	tabList.push_back(textEntryMonth);
	tabList.push_back(textEntryDay);
	tabList.push_back(textEntryYear);
	tabList.push_back(checkBoxRememberPassword);
	tabList.push_back(buttonLogin);
	tabList.push_back(buttonCreateAccount);
	tabList.push_back(buttonTOS);
	tabList.push_back(buttonBack);
	TheWindowManager->clearTabList();
	TheWindowManager->registerTabList(tabList);
	TheWindowManager->winSetFocus( comboBoxEmail );
	// short form or long form?
#ifdef ALLOW_NON_PROFILED_LOGIN
	if (parentWOLLogin)
	{
		GameSpyUseProfiles = true;
#endif // ALLOW_NON_PROFILED_LOGIN

		DEBUG_ASSERTCRASH(buttonBack,						("buttonBack missing!"));
		DEBUG_ASSERTCRASH(buttonLogin,					("buttonLogin missing!"));
		DEBUG_ASSERTCRASH(buttonCreateAccount,	("buttonCreateAccount missing!"));
		//DEBUG_ASSERTCRASH(buttonDontUseAccount,	("buttonDontUseAccount missing!"));
		DEBUG_ASSERTCRASH(comboBoxEmail,				("comboBoxEmail missing!"));
		DEBUG_ASSERTCRASH(comboBoxLoginName,		("comboBoxLoginName missing!"));
		DEBUG_ASSERTCRASH(textEntryPassword,		("textEntryPassword missing!"));

		//TheShell->registerWithAnimateManager(parentWOLLogin, WIN_ANIMATION_SLIDE_TOP, TRUE);
		/**/
//		TheShell->registerWithAnimateManager(buttonTOS, WIN_ANIMATION_SLIDE_BOTTOM, TRUE);
		//TheShell->registerWithAnimateManager(buttonCreateAccount, WIN_ANIMATION_SLIDE_LEFT, TRUE);
		//TheShell->registerWithAnimateManager(buttonDontUseAccount, WIN_ANIMATION_SLIDE_LEFT, TRUE);
//		TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_BOTTOM, TRUE);
		/**/
#ifdef ALLOW_NON_PROFILED_LOGIN
	}
	else
	{
		GameSpyUseProfiles = false;

		parentWOLLoginID =						TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:WOLLoginMenuParent" );
		buttonBackID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonBack" );
		buttonLoginID =								TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonLogin" );
		buttonCreateAccountID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonCreateAccount" );
		buttonUseAccountID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonUseAccount" );
		buttonDontUseAccountID =			TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonDontUseAccount" );
		buttonTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonTOS" );
		parentTOSID							=			TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ParentTOS" );
		buttonTOSOKID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ButtonTOSOK" );
		listboxTOSID						=			TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ListboxTOS" );
		comboBoxEmailID =							TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:ComboBoxEmail" );
		textEntryLoginNameID =				TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:TextEntryLoginName" );
		textEntryPasswordID =					TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:TextEntryPassword" );
		checkBoxRememberPasswordID =	TheNameKeyGenerator->nameToKey( "GameSpyLoginQuick.wnd:CheckBoxRememberPassword" );

		parentWOLLogin =							TheWindowManager->winGetWindowFromId( NULL,  parentWOLLoginID );
		buttonBack =									TheWindowManager->winGetWindowFromId( NULL,  buttonBackID);
		buttonLogin =									TheWindowManager->winGetWindowFromId( NULL,  buttonLoginID);
		buttonCreateAccount =					TheWindowManager->winGetWindowFromId( NULL,  buttonCreateAccountID);
		buttonUseAccount =						TheWindowManager->winGetWindowFromId( NULL,  buttonUseAccountID);
		buttonDontUseAccount =				TheWindowManager->winGetWindowFromId( NULL,  buttonDontUseAccountID);
		comboBoxEmail =								TheWindowManager->winGetWindowFromId( NULL,  comboBoxEmailID);
		buttonTOS =										TheWindowManager->winGetWindowFromId( NULL,  buttonTOSID);
		parentTOS =										TheWindowManager->winGetWindowFromId( NULL,  parentTOSID);
		buttonTOSOK =									TheWindowManager->winGetWindowFromId( NULL,  buttonTOSOKID);
		listboxTOS =									TheWindowManager->winGetWindowFromId( NULL,  listboxTOSID);
		textEntryLoginName =					TheWindowManager->winGetWindowFromId( NULL,  textEntryLoginNameID);
		textEntryPassword =						TheWindowManager->winGetWindowFromId( NULL,  textEntryPasswordID);
		checkBoxRememberPassword =		TheWindowManager->winGetWindowFromId( NULL,  checkBoxRememberPasswordID);

		DEBUG_ASSERTCRASH(buttonBack,						("buttonBack missing!"));
		DEBUG_ASSERTCRASH(buttonLogin,					("buttonLogin missing!"));
		DEBUG_ASSERTCRASH(buttonCreateAccount,	("buttonCreateAccount missing!"));
		DEBUG_ASSERTCRASH(buttonUseAccount,			("buttonUseAccount missing!"));
		DEBUG_ASSERTCRASH(textEntryLoginName,		("textEntryLoginName missing!"));
		TheWindowManager->winSetFocus( textEntryLoginName );
		//TheShell->registerWithAnimateManager(parentWOLLogin, WIN_ANIMATION_SLIDE_TOP, TRUE);
		
//		TheShell->registerWithAnimateManager(buttonTOS, WIN_ANIMATION_SLIDE_LEFT, TRUE);
//		TheShell->registerWithAnimateManager(buttonCreateAccount, WIN_ANIMATION_SLIDE_LEFT, TRUE);
//		TheShell->registerWithAnimateManager(buttonUseAccount, WIN_ANIMATION_SLIDE_LEFT, TRUE);
//		TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE);
		
	}
#endif // ALLOW_NON_PROFILED_LOGIN


#ifdef ALLOW_NON_PROFILED_LOGIN
	if (GameSpyUseProfiles)
	{
#endif // ALLOW_NON_PROFILED_LOGIN
		// Read login names from registry...
		GadgetComboBoxReset(comboBoxEmail);
		GadgetTextEntrySetText(textEntryPassword, UnicodeString::TheEmptyString);

		// look for cached nicks to add
		AsciiString lastName;
		AsciiString lastEmail;
		Bool markCheckBox = FALSE;
		UserPreferences::const_iterator it = loginPref->find("lastName");
		if (it != loginPref->end())
		{
			lastName = it->second;
		}
		it = loginPref->find("lastEmail");
		if (it != loginPref->end())
		{
			lastEmail = it->second;
		}

		// fill in list of Emails, and select the most recent
		AsciiStringList cachedEmails = loginPref->getEmails();
		AsciiStringListIterator eIt = cachedEmails.begin();
		Int selectedPos = -1;
		while (eIt != cachedEmails.end())
		{
			UnicodeString uniEmail;
			uniEmail.translate(*eIt);
			Int pos = GadgetComboBoxAddEntry(comboBoxEmail, uniEmail, GameSpyColor[GSCOLOR_DEFAULT]);
			if (*eIt == lastEmail)
				selectedPos = pos;

			++eIt;
		}
		if (selectedPos >= 0)
		{
			GadgetComboBoxSetSelectedPos(comboBoxEmail, selectedPos);

			// fill in the password for the selected email
			UnicodeString pass;
			pass.translate(loginPref->getPasswordForEmail(lastEmail));
			GadgetTextEntrySetText(textEntryPassword, pass);
			
			AsciiString month,day,year;
			loginPref->getDateForEmail(lastEmail, month, day, year);
			pass.translate(month);
			GadgetTextEntrySetText(textEntryMonth, pass);
			pass.translate(day);
			GadgetTextEntrySetText(textEntryDay, pass);
			pass.translate(year);
			GadgetTextEntrySetText(textEntryYear, pass);

			markCheckBox = TRUE;
		}

		// fill in list of nicks for selected email, selecting the most recent
		GadgetComboBoxReset(comboBoxLoginName);
		AsciiStringList cachedNicks = loginPref->getNicksForEmail(lastEmail);
		AsciiStringListIterator nIt = cachedNicks.begin();
		selectedPos = -1;
		while (nIt != cachedNicks.end())
		{
			UnicodeString uniNick;
			uniNick.translate(*nIt);
			Int pos = GadgetComboBoxAddEntry(comboBoxLoginName, uniNick, GameSpyColor[GSCOLOR_DEFAULT]);
			if (*nIt == lastName)
				selectedPos = pos;

			++nIt;
		}
		if (selectedPos >= 0)
		{
			GadgetComboBoxSetSelectedPos(comboBoxLoginName, selectedPos);
			markCheckBox = TRUE;
		}
		// always start with not storing information
		if( markCheckBox)
			GadgetCheckBoxSetChecked(checkBoxRememberPassword, TRUE);
		else
			GadgetCheckBoxSetChecked(checkBoxRememberPassword, FALSE);
#ifdef ALLOW_NON_PROFILED_LOGIN
	}
	else
	{
		// Read login names from registry...
		GadgetComboBoxReset(comboBoxLoginName);
		UnicodeString nick;

		UserPreferences::const_iterator it = loginPref->find("lastName");
		if (it != loginPref->end())
		{
			nick.translate(it->second);
		}
		else
		{
			char userBuf[32] = "";
			unsigned long bufSize = 32;
			GetUserName(userBuf, &bufSize);
			nick.translate(userBuf);
		}

		GadgetTextEntrySetText(textEntryLoginName, nick);
	}
#endif // ALLOW_NON_PROFILED_LOGIN

	EnableLoginControls(TRUE);
	onlineLoginScreen();

	// Show Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	
	RaiseGSMessageBox();

	// -online has no terms screen: that button is Forgot Password there
	OptionPreferences optionPref;
	if (!OnlineIsEnabled() && !optionPref.getBool("SawTOS", TRUE))
	{
		TheWindowManager->winSendSystemMsg( parentWOLLogin, GBM_SELECTED, 
																			(WindowMsgData)buttonTOS, buttonTOSID );
	}
	TheTransitionHandler->setGroup("GameSpyLoginProfileFade");

} // WOLLoginMenuInit

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu shutdown method */
//-------------------------------------------------------------------------------------------------
static Bool loggedInOK = false;
void WOLLoginMenuShutdown( WindowLayout *layout, void *userData )
{
	isShuttingDown = true;
	loggedInOK = false;
	TheWindowManager->clearTabList();
	if (webBrowserActive)
	{
		if (TheWebBrowser != NULL)
		{
			TheWebBrowser->closeBrowserWindow(listboxTOS);
		}
		webBrowserActive = FALSE;
	}

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}  //end if

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("GameSpyLoginProfileFade");

}  // WOLLoginMenuShutdown


// this is used to check if we've got all the pings
static void checkLogin( void )
{
	if (loggedInOK && ThePinger && !ThePinger->arePingsInProgress())
	{
		// save off our ping string, and end those threads
		AsciiString pingStr = ThePinger->getPingString( 1000 );
		DEBUG_LOG(("Ping string is %s\n", pingStr.str()));
		TheGameSpyInfo->setPingString(pingStr);
		//delete ThePinger;
		//ThePinger = NULL;

		buttonPushed = true;
		loggedInOK = false; // don't try this again

		loginAttemptTime = 0;

		// start looking for group rooms
		TheGameSpyInfo->clearGroupRoomList();

		SignalUIInteraction(SHELL_SCRIPT_HOOK_GENERALS_ONLINE_LOGIN);
		nextScreen = "Menus/WOLWelcomeMenu.wnd";
		TheShell->pop();
		
		// read in some cached data
		GameSpyMiscPreferences mPref;
		PSPlayerStats localPSStats = GameSpyPSMessageQueueInterface::parsePlayerKVPairs(mPref.getCachedStats().str());
		localPSStats.id = TheGameSpyInfo->getLocalProfileID();
		TheGameSpyInfo->setCachedLocalPlayerStats(localPSStats);
//		TheGameSpyPSMessageQueue->trackPlayerStats(localPSStats);

		// and push the info around to other players
//		PSResponse newResp;
//		newResp.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
//		newResp.player = localPSStats;
//		TheGameSpyPSMessageQueue->addResponse(newResp);
	}
}

/// Sign In as a click on it, while it takes one: Enter in either field, and the automatic sign-ins
static void pressLogin( void )
{
	if (buttonPushed || !parentWOLLogin || !buttonLogin || !BitTest( buttonLogin->winGetStatus(), WIN_STATUS_ENABLED )
		|| buttonLogin->winIsHidden())
		return;
	TheWindowManager->winSendSystemMsg( parentWOLLogin, GBM_SELECTED, (WindowMsgData)buttonLogin, buttonLoginID );
}

/* -onlineuser/-onlinepass: fill the fields and press Login as a player would, once per process; a refusal
	 leaves the screen to a person.  No password logs in with the token a remembered login saved.  Without
	 them, Keep me signed in does the same on the first visit after a start: the login came back ticked
	 with its token saved, so the player is signed in without pressing anything.  Back on this screen
	 later in the same run (signed out, or another account wanted) it waits for a press. */
static void onlineAutoLogin( void )
{
	static Bool s_done = FALSE;
	if (s_done || !OnlineIsEnabled() || OnlineGetStatus() != ONLINE_WELCOMED || loginAttemptTime
		|| !comboBoxEmail || !textEntryPassword || !parentWOLLogin || !buttonLogin
		|| !BitTest( buttonLogin->winGetStatus(), WIN_STATUS_ENABLED ))		// the welcome wakes it a pass later
		return;
	s_done = TRUE;

	if (TheGlobalData->m_onlineUser.isEmpty())
	{
		AsciiString email;
		email.translate( GadgetComboBoxGetText( comboBoxEmail ) );
		if (!checkBoxRememberPassword || !GadgetCheckBoxIsChecked( checkBoxRememberPassword ) || email.isEmpty()
			|| !GadgetTextEntryGetText( textEntryPassword ).isEmpty() || OnlineGetSavedToken( email ).isEmpty())
			return;
		DEBUG_LOG(("ONLINE AUTOLOGIN kept\n"));
		pressLogin();
		return;
	}

	UnicodeString text;
	text.translate( TheGlobalData->m_onlineUser );
	GadgetComboBoxSetText( comboBoxEmail, text );
	text.translate( TheGlobalData->m_onlinePass );
	GadgetTextEntrySetText( textEntryPassword, text );
	if (checkBoxRememberPassword)
		GadgetCheckBoxSetChecked( checkBoxRememberPassword, TRUE );

	DEBUG_LOG(("ONLINE AUTOLOGIN login\n"));
	pressLogin();
}

/* -online, on GameSpyLoginProfile.wnd as Tools/online_layout.py writes it (ONLINE-UI-REDESIGN.md "Login"):
	 the server panel on the left, the sign-in panel on the right, every text in one of the master's static
	 texts.  The counts and the wordmark are drawn in code over the server panel. */
static GameWindow *s_onlineStatus = NULL;		///< under Sign In: a page opened, or fields left empty
static GameWindow *s_onlineMOTD = NULL;
static Bool s_onlineAwaitWelcome = FALSE;		///< the screen dialled and waits for the service's welcome
static Bool s_savedSignIn = FALSE;					///< the password is empty and a token is saved for the login typed
static GameWinDrawFunc s_serverPanelDraw = NULL;
static GameWinDrawFunc s_passwordDraw = NULL;
static DisplayString *s_savedHint = NULL;		///< GUI:OnlineLoginSavedToken, inside the empty password field

/// players online, open games and players in Quick Match: a label and a figure each
enum { ONLINE_COUNTS = 3 };
static DisplayString *s_countLabel[ONLINE_COUNTS];
static DisplayString *s_countFigure[ONLINE_COUNTS];
static Int s_countShown[ONLINE_COUNTS];
static Bool s_countsShown = FALSE;
/// the message of the day stands under the counts, or alone in their place under the heading
static Int s_motdX = 0, s_motdWidth = 0, s_motdBottom = 0, s_motdUnderCounts = 0, s_motdAlone = 0;
/// the panel's heading: the server's name and city and the host (the header's server line has the ping)
static GameWindow *s_serverText = NULL;
static Bool s_serverLaidOut = FALSE;
static AsciiString s_serverName, s_serverCity;

static const Color STATUS_ERROR = GameMakeColor( 255, 96, 96, 255 );

/// the status line: what happened, or what went wrong in red
static void onlineStatus( const UnicodeString &text, Bool error = FALSE )
{
	if (!s_onlineStatus)
		return;
	s_onlineStatus->winSetEnabledTextColors( error ? STATUS_ERROR : UIStyle::MUTED, UIStyle::SHADOW );
	GadgetStaticTextSetText( s_onlineStatus, text );
	DEBUG_LOG(("ONLINE LOGINSTATUS error=%d %s\n", (Int)error, WideCharAsUtf8( text.str() ).str()));
}

/* A maintenance notice, from the welcome on or -testNotice, says so on the status line under Sign In before
	 anyone tries, with when the service should be back; the line goes when the notice does. */
static Bool s_maintenanceShown = FALSE;
static UnsignedInt s_maintenanceSerial = 0;
static void onlineMaintenanceLine( void )
{
	OnlineNotice notice;
	const Bool on = OnlineGetNotice( notice ) && notice.maintenance;
	if (on == s_maintenanceShown && (!on || notice.serial == s_maintenanceSerial))
		return;
	if (on)
		onlineStatus( OnlineMaintenanceText( notice.until ), TRUE );
	else
		onlineStatus( UnicodeString::TheEmptyString );
	s_maintenanceShown = on;
	s_maintenanceSerial = notice.serial;
}

/// Sign In's label: what the screen is doing while it cannot be pressed (connecting, signing in), else Sign In
static void onlineLoginLabel( const char *key )
{
	if (buttonLogin)
		GadgetButtonSetText( buttonLogin, UIStyle::capitals( TheGameText->fetch( key ) ) );
}

/// an empty password with a token saved for this login signs in with the token: the field says so
static void onlineSavedLoginHint( void )
{
	s_savedSignIn = FALSE;
	if (!OnlineIsEnabled() || !comboBoxEmail || !textEntryPassword)
		return;
	AsciiString email;
	email.translate( GadgetComboBoxGetText( comboBoxEmail ) );
	s_savedSignIn = email.isNotEmpty() && GadgetTextEntryGetText( textEntryPassword ).isEmpty()
		&& OnlineGetSavedToken( email ).isNotEmpty();
}

/// the password field as EA draws it, with the saved sign-in's line in it while it is empty
static void drawPasswordEntry( GameWindow *window, WinInstanceData *instData )
{
	if (s_passwordDraw)
		s_passwordDraw( window, instData );
	if (!s_savedSignIn || !s_savedHint)
		return;
	Int x, y, width, height, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	s_savedHint->getSize( &w, &h );
	s_savedHint->draw( x + UIStyle::px( 14.0f ), y + ( height - h ) / 2, UIStyle::MUTED, UIStyle::SHADOW );
}

/// the server panel as the master fills it, the three counts across its top, and the page's header
/// (wordmark, and the server line with its ping after the master's title)
static void drawServerPanel( GameWindow *window, WinInstanceData *instData )
{
	if (s_serverPanelDraw)
		s_serverPanelDraw( window, instData );
	UIStyle::onlineHeader( UnicodeString::TheEmptyString, UnicodeString::TheEmptyString );
	if (!s_countsShown)
		return;
	// on the board, as login.html sets them: a column every 306 from 148, the label at 270, the figure
	// 10 under it, a GRID rule at 386 over the message of the day
	for (Int i = 0; i < ONLINE_COUNTS; ++i)
	{
		const Int x = UIStyle::boardX( 148.0f + 306.0f * i );
		const Int y = UIStyle::boardY( 270.0f );
		Int w, h;
		s_countLabel[i]->getSize( &w, &h );
		s_countLabel[i]->draw( x, y, UIStyle::MUTED, UIStyle::SHADOW );
		s_countFigure[i]->draw( x, y + h + UIStyle::px( 10.0f ), UIStyle::INK, UIStyle::SHADOW );
	}
	TheDisplay->drawFillRect( UIStyle::boardX( 148.0f ), UIStyle::boardY( 386.0f ), UIStyle::px( 904.0f ), 1, UIStyle::GRID );
}

static UnicodeString fromUtf8( const AsciiString &bytes )
{
	return UnicodeString( MultiByteToWideCharSingleLine( bytes.str() ).c_str() );
}

/// the heading as the server names itself, "EU1 · Somewhere · eu1.example.org" joined as the header joins
/// its parts; the host alone from a server that sends no name
static void onlineServerHeading( void )
{
	AsciiString name, city;
	Int ms;
	if (!OnlineIsEnabled() || !s_serverText)
		return;
	if (!OnlineGetServerInfo( name, city, ms ))
		name = city = AsciiString::TheEmptyString;
	if (s_serverLaidOut && name == s_serverName && city == s_serverCity)
		return;
	s_serverLaidOut = TRUE;
	s_serverName = name;
	s_serverCity = city;
	UnicodeString heading = fromUtf8( name ), host;
	if (city.isNotEmpty())
	{
		if (!heading.isEmpty())
			heading.concat( UIStyle::HEADER_JOINER );
		heading.concat( fromUtf8( city ) );
	}
	host.translate( TheGlobalData->m_onlineHost );
	if (!heading.isEmpty())
		heading.concat( UIStyle::HEADER_JOINER );
	heading.concat( host );
	GadgetStaticTextSetText( s_serverText, heading );
}

/// every update: the server's name, the counts the server sent, and the message of the day under them or
/// in their place
static void onlineServerPanel( void )
{
	onlineServerHeading();
	Int counts[ONLINE_COUNTS];
	const Bool have = OnlineIsEnabled() && s_countFigure[0] && OnlineGetCounts( counts[0], counts[1], counts[2] );
	for (Int i = 0; have && i < ONLINE_COUNTS; ++i)
	{
		if (counts[i] != s_countShown[i])
		{
			s_countShown[i] = counts[i];
			s_countFigure[i]->setText( UIStyle::grouped( counts[i] ) );
		}
	}
	if (s_onlineMOTD && have != s_countsShown)
	{
		const Int top = have ? s_motdUnderCounts : s_motdAlone;
		s_onlineMOTD->winSetPosition( s_motdX, top );
		s_onlineMOTD->winSetSize( s_motdWidth, s_motdBottom - top );
	}
	s_countsShown = have;
}

/// the welcome's message of the day as one text, EA's colour prefixes ("\aarrggbb", "\\") dropped as the
/// hub drops them; empty when the server sent none
static UnicodeString onlineMOTDText( void )
{
	AsciiString motd = OnlineGetMOTD();
	motd.trim();
	UnicodeString text;
	AsciiString line;
	while (motd.nextToken( &line, "\n" ))
	{
		line.trim();
		if (line.startsWith( "\\\\" ))
			line = line.str() + 1;
		else if (line.startsWith( "\\" ) && line.getLength() > 9)
			line = line.str() + 9;
		if (!text.isEmpty())
			text.concat( u"\n" );
		text.concat( UnicodeString( MultiByteToWideCharSingleLine( line.str() ).c_str() ) );
	}
	return text;
}

/// Create Account and Forgot Password show when the welcome named their pages
static void onlineSiteButtons( void )
{
	if (buttonCreateAccount)
		buttonCreateAccount->winHide( OnlineGetRegisterUrl().isEmpty() );
	if (buttonTOS)
		buttonTOS->winHide( OnlineGetResetUrl().isEmpty() );
}

/* The box for a login the screen keeps nothing of.  EA unticked it, so under -online, where the email is
	 typed rather than picked, every letter of it took back a tick given first and the login was never
	 kept.  -online leaves it as the player set it, and ticks it for a login whose token is saved under
	 another spelling (tokens are kept lower case, the screen's own list as typed). */
static void onlineRememberUnsaved( const AsciiString &email )
{
	if (!checkBoxRememberPassword)
		return;
	if (!OnlineIsEnabled())
		GadgetCheckBoxSetChecked( checkBoxRememberPassword, FALSE );
	else if (email.isNotEmpty() && OnlineGetSavedToken( email ).isNotEmpty())
		GadgetCheckBoxSetChecked( checkBoxRememberPassword, TRUE );
}

/// Create Account and Forgot Password: the site's page in the player's browser.  A test server's copies
/// only log the address, so a scripted run puts no browser on the desktop, and the line says so.
static void onlineOpenPage( const AsciiString &url, const char *what )
{
	const Bool test = OnlineIsTestServer();
	const Bool opened = url.isNotEmpty() && !test && OnlineOpenURL( url.str() );
	DEBUG_LOG(("ONLINE %s url=%s opened=%d\n", what, url.str(), (Int)opened));
	if (opened)
		onlineStatus( TheGameText->fetch( "GUI:OnlineOpenedInBrowser" ) );
	else if (test)
		onlineStatus( TheGameText->fetch( "GUI:OnlineBrowserTestServer" ) );
	else
		onlineStatus( TheGameText->fetch( "GUI:OnlineBrowserFailed" ), TRUE );
}

static void focusPassword( void )
{
	if (textEntryPassword && !buttonPushed)
		TheWindowManager->winSetFocus( textEntryPassword );
}

/// what went wrong, in EA's message box over the panel as retail shows it; OK puts the cursor in the password
static void onlineLoginError( const UnicodeString &text )
{
	DEBUG_LOG(("ONLINE LOGINERROR %s\n", WideCharAsUtf8( text.str() ).str()));
	GSMessageBoxOk( TheGameText->fetch( "GUI:OnlineLoginFailed" ), text, focusPassword );
}

/* A ban's line: its end and the site's reason, each left out when the refusal had none (no end is an
	 indefinite ban).  A format string missing from the language falls back to the plain line. */
static UnicodeString onlineBannedText( const AsciiString &until, const AsciiString &reason )
{
	const UnicodeString when = OnlineUntilText( until );
	const UnicodeString why( MultiByteToWideCharSingleLine( reason.str() ).c_str() );		// the site's UTF-8, on one line
	const char *key = when.isEmpty()
		? (why.isEmpty() ? NULL : "GUI:OnlineLoginBannedReason")
		: (why.isEmpty() ? "GUI:OnlineLoginBannedUntil" : "GUI:OnlineLoginBannedUntilReason");
	Bool exists = FALSE;
	const UnicodeString format = key ? TheGameText->fetch( key, &exists ) : UnicodeString::TheEmptyString;
	if (!exists)
		return TheGameText->fetch( "GUI:OnlineLoginBanned" );
	UnicodeString text;
	if (when.isEmpty())
		text.format( format, why.str() );
	else if (why.isEmpty())
		text.format( format, when.str() );
	else
		text.format( format, when.str(), why.str() );
	return text;
}

/* A refused or failed sign-in: the server's code worded honestly (a wrong password and an unknown login
	 read the same), EA's disconnect text when no refusal arrived.  The password is cleared and focused for
	 the next try; a token the server no longer takes is forgotten. */
static void onlineLoginFailed( Int reason )
{
	Int retry = 0;
	AsciiString file, until, banReason;
	const AsciiString code = OnlineGetLoginFailure( &retry, &file, &until, &banReason );
	UnicodeString text;
	if (code == "maintenance")
		text = OnlineMaintenanceText( until );
	else if (code == "files" && file.isEmpty())
		text = TheGameText->fetch( "GUI:OnlineLoginFilesNone" );		// no list reached it, or none was hashed
	else if (code == "files")
	{
		UnicodeString name;
		name.translate( file );
		text.format( TheGameText->fetch( "GUI:OnlineLoginFiles" ), name.str() );
	}
	else if (code == "badlogin" || code == "badtoken")
		text = TheGameText->fetch( "GUI:OnlineLoginBadBody" );
	else if (code == "throttled" && retry > 0)
		text.format( TheGameText->fetch( "GUI:OnlineLoginThrottled" ), retry );
	else if (code == "throttled")
		text = TheGameText->fetch( "GUI:OnlineLoginThrottledNoRetry" );
	else if (code == "unavailable")
		text = TheGameText->fetch( "GUI:OnlineLoginUnavailable" );
	else if (code == "expired")
		text = TheGameText->fetch( "GUI:OnlineLoginExpired" );
	else if (code == "banned")
		text = onlineBannedText( until, banReason );
	else if (code == "elsewhere")
		text = TheGameText->fetch( "GUI:OnlineLoginElsewhere" );
	else
	{
		AsciiString key;
		key.format( "GUI:GSDisconReason%d", reason );
		text = TheGameText->fetch( key );
	}
	if (code == "expired" || code == "badtoken")
		OnlineSaveToken( s_onlineLoginEmail, AsciiString::TheEmptyString );
	GadgetTextEntrySetText( textEntryPassword, UnicodeString::TheEmptyString );
	TheWindowManager->winSetFocus( textEntryPassword );
	DEBUG_LOG(("ONLINE LOGINFOCUS password=%d\n", (Int)(textEntryPassword && TheWindowManager->winGetFocus() == textEntryPassword)));
	onlineSavedLoginHint();
	onlineLoginLabel( "GUI:OnlineSignIn" );
	onlineLoginError( text );
}

/* -online: the screen opens at once and dials the service itself; Sign In waits for the welcome.  EA put a
	 "Checking for Updates" box in front of this, which here only hid the same wait. */
static UnsignedInt s_onlineAwaitSince = 0;
static const UnsignedInt ONLINE_WELCOME_TIMEOUT_MS = 15000;
static void onlineWelcomed( Bool reload );

static void onlineStartConnect( void )
{
	s_onlineAwaitWelcome = TRUE;
	s_onlineAwaitSince = Clock_Milliseconds();
	if (OnlineGetStatus() == ONLINE_FAILED)
		OnlineDisconnect();		// the next update dials again
	OnlineUpdate();
	if (buttonLogin)
		buttonLogin->winEnable( FALSE );
	onlineLoginLabel( "GUI:OnlineConnecting" );
	onlineStatus( UnicodeString::TheEmptyString );
}

/// every update while the screen waits: the welcome, or a server that cannot be reached (Sign In retries)
static void onlineAwaitWelcome( void )
{
	OnlineUpdate();
	const OnlineStatus status = OnlineGetStatus();
	if (status == ONLINE_WELCOMED)
	{
		onlineWelcomed( TRUE );
		return;
	}
	if (status != ONLINE_FAILED && status != ONLINE_OFF && Clock_Milliseconds() - s_onlineAwaitSince <= ONLINE_WELCOME_TIMEOUT_MS)
		return;
	s_onlineAwaitWelcome = FALSE;
	OnlineDisconnect();
	DEBUG_LOG(("ONLINE CONNECT fail status=%d\n", (Int)status));
	if (buttonLogin)
		buttonLogin->winEnable( TRUE );
	onlineLoginLabel( "GUI:OnlineSignIn" );
	// -testNotice has no server behind it: the proof shot of the maintenance line wants no box over it
	if (TheGlobalData->m_testNotice.isEmpty())
		onlineLoginError( TheGameText->fetch( "GUI:OnlineCannotReachServer" ) );
}

static GameWindow *loginWindow( const char *name )
{
	AsciiString full;
	full.format( "GameSpyLoginProfile.wnd:%s", name );
	return TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( full ) );
}

/// a master's static text in one of UIStyle's colours, with text when given
static GameWindow *loginText( const char *name, Color color, const UnicodeString &text = UnicodeString::TheEmptyString )
{
	GameWindow *win = loginWindow( name );
	if (win)
	{
		win->winSetEnabledTextColors( color, UIStyle::SHADOW );
		if (!text.isEmpty())
			GadgetStaticTextSetText( win, text );
	}
	return win;
}

static DisplayString *newLoginString( GameFont *font, const UnicodeString &text )
{
	DisplayString *words = TheDisplayStringManager->newDisplayString();
	words->setFont( font );
	words->setText( text );
	return words;
}

static void freeLoginString( DisplayString *&words )
{
	if (words)
		TheDisplayStringManager->freeDisplayString( words );
	words = NULL;
}

/* -online: the game only logs in, the site makes accounts (the owner's call of 2026-11-06).  The master
	 holds one field for the email or the nick and hides the nick box and the birth date; Create Account and
	 Forgot Password open the site's pages the server named in its welcome, and go when it named none. */
static void onlineLoginScreen( void )
{
	s_onlineStatus = NULL;
	s_onlineMOTD = NULL;
	s_onlineAwaitWelcome = FALSE;
	s_savedSignIn = FALSE;
	s_countsShown = FALSE;
	s_maintenanceShown = FALSE;
	if (!OnlineIsEnabled())
		return;
	UIStyle::onlinePageInit( parentWOLLogin, "GameSpyLoginProfile.wnd", UIStyle::ONLINE_TAB_NONE );

	// the server panel: the server's heading, the counts drawn over it, the message of the day
	s_serverText = loginText( "StaticTextServer", UIStyle::ACTIVE );
	s_serverLaidOut = FALSE;
	s_onlineMOTD = loginText( "StaticTextMOTD", UIStyle::INK );
	const char *countKeys[ONLINE_COUNTS] = { "GUI:OnlineLoginOnlineNow", "GUI:OnlineOpenGamesLabel", "GUI:OnlineLoginInQuickMatch" };
	for (Int i = 0; i < ONLINE_COUNTS; ++i)
	{
		freeLoginString( s_countLabel[i] );
		freeLoginString( s_countFigure[i] );
		s_countLabel[i] = newLoginString( UIStyle::readableFont( UIStyle::TEXT_BODY ), TheGameText->fetch( countKeys[i] ) );
		s_countFigure[i] = newLoginString( UIStyle::titleFont( UIStyle::TEXT_DISPLAY ), UnicodeString::TheEmptyString );
		s_countShown[i] = -1;
	}
	if (s_onlineMOTD && s_serverText)
	{
		// from its top, as a page reads, where a static text centres its words in the box
		TextData *motd = (TextData *)s_onlineMOTD->winGetUserData();
		if (motd)
			motd->centeredVertically = FALSE;
		// under the counts as the master has it, or alone under the server's heading where the counts
		// would stand, as tall as it can be
		Int x, y, width, height;
		s_onlineMOTD->winGetPosition( &s_motdX, &s_motdUnderCounts );
		s_onlineMOTD->winGetSize( &s_motdWidth, &height );
		s_motdBottom = s_motdUnderCounts + height;
		s_serverText->winGetPosition( &x, &y );
		s_serverText->winGetSize( &width, &height );
		s_motdAlone = y + height + UIStyle::px( 8.0f );
		s_onlineMOTD->winSetPosition( s_motdX, s_motdAlone );
		s_onlineMOTD->winSetSize( s_motdWidth, s_motdBottom - s_motdAlone );
	}
	GameWindow *serverPanel = loginWindow( "PanelServer" );
	if (serverPanel)
	{
		// a second init on the same layout must not take the hook for the master's draw
		if (serverPanel->winGetDrawFunc() != drawServerPanel)
			s_serverPanelDraw = serverPanel->winGetDrawFunc();
		serverPanel->winSetDrawFunc( drawServerPanel );
	}

	// the sign-in panel
	loginText( "StaticTextSignIn", UIStyle::ACTIVE );
	s_onlineStatus = loginText( "StaticTextStatus", UIStyle::MUTED );
	// the field labels in the body face, as the status line under them (EA's labels are a size up)
	const char *labels[] = { "StaticTextEmail", "StaticTextPassword" };
	for (Int i = 0; s_onlineStatus && i < (Int)ARRAY_SIZE(labels); ++i)
	{
		GameWindow *label = loginWindow( labels[i] );
		if (label)
			label->winSetFont( s_onlineStatus->winGetFont() );
	}
	// Create Account and Forgot Password are small buttons: 21 of a layout button's 28 at 1080, scaled
	// from their own fitted face so a squarer screen keeps the ratio
	GameWindow *siteButtons[] = { buttonCreateAccount, buttonTOS };		// not "small": a Windows macro
	for (Int i = 0; i < (Int)ARRAY_SIZE(siteButtons); ++i)
	{
		GameFont *face = siteButtons[i] ? siteButtons[i]->winGetFont() : NULL;
		if (face)
			siteButtons[i]->winSetFont( TheFontLibrary->getFont( face->nameString, max( 1, ( face->pointSize * 21 + 14 ) / 28 ), face->bold ) );
	}
	if (textEntryPassword)
	{
		if (textEntryPassword->winGetDrawFunc() != drawPasswordEntry)
			s_passwordDraw = textEntryPassword->winGetDrawFunc();
		textEntryPassword->winSetDrawFunc( drawPasswordEntry );
		Int width, height;
		textEntryPassword->winGetSize( &width, &height );
		freeLoginString( s_savedHint );
		GameFont *font = textEntryPassword->winGetFont();
		s_savedHint = newLoginString( font,
			UIStyle::fitText( TheGameText->fetch( "GUI:OnlineLoginSavedToken" ), font, width - 2 * UIStyle::px( 14.0f ) ) );
	}
	// a fresh profile has no remembered logins: no arrow over an empty list, the field takes its room
	GameWindow *dropDown = comboBoxEmail ? GadgetComboBoxGetDropDownButton( comboBoxEmail ) : NULL;
	GameWindow *emailBox = comboBoxEmail ? GadgetComboBoxGetEditBox( comboBoxEmail ) : NULL;
	if (dropDown && emailBox && GadgetComboBoxGetLength( comboBoxEmail ) == 0)
	{
		Int width, height, boxWidth, boxHeight;
		comboBoxEmail->winGetSize( &width, &height );
		emailBox->winGetSize( &boxWidth, &boxHeight );
		dropDown->winHide( TRUE );
		emailBox->winSetSize( width, boxHeight );
	}
	if (checkBoxRememberPassword)
	{
		GadgetCheckBoxSetText( checkBoxRememberPassword, TheGameText->fetch( "GUI:OnlineKeepSignedIn" ) );
		checkBoxRememberPassword->winSetTooltip( TheGameText->fetch( "TOOLTIP:OnlineKeepSignedIn" ) );
	}
	onlineSiteButtons();		// none before a welcome names the pages
	onlineSavedLoginHint();
	onlineServerHeading();		// the host before any welcome

	if (OnlineGetStatus() == ONLINE_WELCOMED)
		onlineWelcomed( FALSE );
	else
		onlineStartConnect();
}

/// what onlineLoginScreen made, from shutdownComplete before the next screen goes up
static void onlineLoginScreenDone( void )
{
	UIStyle::onlinePageShutdown( parentWOLLogin );
	for (Int i = 0; i < ONLINE_COUNTS; ++i)
	{
		freeLoginString( s_countLabel[i] );
		freeLoginString( s_countFigure[i] );
	}
	freeLoginString( s_savedHint );
	s_countsShown = FALSE;
	s_onlineStatus = NULL;
	s_onlineMOTD = NULL;
	s_serverText = NULL;
}

/* The service's welcome is in: Create Account and Forgot Password show when it named their pages, the
	 message of the day goes in, Sign In wakes up.  reload remakes the queues with the welcome's MOTD and
	 config, which the screen opened without. */
static void onlineWelcomed( Bool reload )
{
	s_onlineAwaitWelcome = FALSE;
	if (reload)
	{
		TearDownGameSpy();
		SetUpGameSpy( OnlineGetMOTD().str(), OnlineGetConfig().str() );
	}
	DEBUG_LOG(("ONLINE CONNECT ok\n"));

	const AsciiString url = OnlineGetRegisterUrl();
	onlineSiteButtons();
	if (buttonLogin)
		buttonLogin->winEnable( TRUE );
	onlineLoginLabel( "GUI:OnlineSignIn" );
	if (s_onlineMOTD)
		GadgetStaticTextSetText( s_onlineMOTD, onlineMOTDText() );
	onlineSavedLoginHint();

	// what a player can type into, for online_check.py: a window counts when neither it nor a parent is hidden
	AsciiString shown;
	const struct { GameWindow *win; const char *name; } fields[] = { { comboBoxEmail, "login" }, { comboBoxLoginName, "nick" },
		{ textEntryPassword, "password" }, { textEntryMonth, "birthdate" }, { checkBoxRememberPassword, "remember" } };
	for (Int i = 0; i < (Int)ARRAY_SIZE(fields); ++i)
	{
		Bool visible = fields[i].win != NULL;
		for (GameWindow *w = fields[i].win; w && visible; w = w->winGetParent())
			visible = !w->winIsHidden();
		if (!visible)
			continue;
		if (shown.isNotEmpty())
			shown.concat( "," );
		shown.concat( fields[i].name );
	}
	DEBUG_LOG(("ONLINE LOGINSCREEN fields=%s register=%s\n", shown.str(), url.isEmpty() ? "hidden" : url.str()));
}

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu update method */
//-------------------------------------------------------------------------------------------------
void WOLLoginMenuUpdate( WindowLayout * layout, void *userData)
{

	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);

	if (s_onlineAwaitWelcome && !buttonPushed && !isShuttingDown)
		onlineAwaitWelcome();
	if (!isShuttingDown)
	{
		onlineServerPanel();
		if (OnlineIsEnabled())
			onlineMaintenanceLine();
	}

	if (TheShell->isAnimFinished() && !buttonPushed && TheGameSpyPeerMessageQueue)
	{
		onlineAutoLogin();
		onlineOpenRelayWhenTicketed();

		PingResponse pingResp;
		if (ThePinger && ThePinger->getResponse(pingResp))
		{
			checkLogin();
		}

		PeerResponse resp;
		if (!loggedInOK && TheGameSpyPeerMessageQueue->getResponse( resp ))
		{
			switch (resp.peerResponseType)
			{
			case PeerResponse::PEERRESPONSE_GROUPROOM:
				{
					GameSpyGroupRoom room;
					room.m_groupID = resp.groupRoom.id;
					room.m_maxWaiting = resp.groupRoom.maxWaiting;
					room.m_name = resp.groupRoomName.c_str();
					room.m_translatedName = UnicodeString(u"TEST");
					room.m_numGames = resp.groupRoom.numGames;
					room.m_numPlaying = resp.groupRoom.numPlaying;
					room.m_numWaiting = resp.groupRoom.numWaiting;
					TheGameSpyInfo->addGroupRoom( room );
				}
				break;
			case PeerResponse::PEERRESPONSE_LOGIN:
				{
					loggedInOK = true;

					// fetch our player info
					TheGameSpyInfo->setLocalName( resp.nick.c_str() );
					TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
					TheGameSpyInfo->loadSavedIgnoreList();
					TheGameSpyInfo->setLocalIPs(resp.player.internalIP, resp.player.externalIP);
					TheGameSpyInfo->readAdditionalDisconnects();
					//TheGameSpyInfo->setLocalEmail( resp.player.email );
					//TheGameSpyInfo->setLocalPassword( resp)

					GameSpyMiscPreferences miscPref;
					TheGameSpyInfo->setMaxMessagesPerUpdate(miscPref.getMaxMessagesPerUpdate());

					// the kept login goes to disk at sign-in too, not only when the screen closes, so a run that
					// ends on the way out still finds it next time
					if (loginPref)
						loginPref->write();

					if (OnlineIsEnabled())
					{
						// the session token is what is remembered, never the password
						s_onlineLoginPending = FALSE;
						OnlineSaveToken( s_onlineLoginEmail, s_rememberLogin ? OnlineGetSessionToken() : AsciiString::TheEmptyString );
						const UnsignedInt vip = Transport::getRelayIP();
						DEBUG_LOG(("ONLINE LOGIN ok vip=%d.%d.%d.%d nick=%s\n", vip >> 24, (vip >> 16) & 0xff, (vip >> 8) & 0xff, vip & 0xff,
							resp.nick.c_str()));
					}
				}
				break;
			case PeerResponse::PEERRESPONSE_DISCONNECT:
				{
					if (OnlineIsEnabled())
					{
						s_onlineLoginPending = FALSE;
						DEBUG_LOG(("ONLINE LOGIN fail reason=%d\n", resp.discon.reason));
					}
					loginAttemptTime = 0;
					if (OnlineIsEnabled())
					{
						// the server's refusal worded, read before the teardown below
						EnableLoginControls( TRUE );
						onlineLoginFailed( resp.discon.reason );
					}
					else
					{
						UnicodeString title, body;
						AsciiString disconMunkee;
						disconMunkee.format("GUI:GSDisconReason%d", resp.discon.reason);
						title = TheGameText->fetch( "GUI:GSErrorTitle" );
						body = TheGameText->fetch( disconMunkee );
						GSMessageBoxOk( title, body );
						EnableLoginControls( TRUE );
					}

					// kill & restart the threads
					AsciiString motd = TheGameSpyInfo->getMOTD();
					AsciiString config = TheGameSpyInfo->getConfig();
					DEBUG_LOG(("Tearing down GameSpy from WOLLoginMenuUpdate(PEERRESPONSE_DISCONNECT)\n"));
					TearDownGameSpy();
					SetUpGameSpy( motd.str(), config.str() );
				}
				break;
			}
		}

		checkLogin();
	}

	// a login held back until the install files are hashed: the status line says so, and the timeout
	// starts once the hashes and the login are out
	static Bool s_checkingFiles = FALSE;
	const Bool checkingFiles = OnlineIsEnabled() && loginAttemptTime && OnlineIsCheckingFiles();
	if (checkingFiles)
		loginAttemptTime = Clock_Milliseconds();
	if (checkingFiles != s_checkingFiles && !isShuttingDown)
	{
		s_checkingFiles = checkingFiles;
		onlineStatus( checkingFiles ? TheGameText->fetch( "GUI:OnlineCheckingFiles" ) : UnicodeString::TheEmptyString );
	}

	if (TheGameSpyInfo && !buttonPushed && loginAttemptTime && (loginAttemptTime + loginTimeoutInMS < Clock_Milliseconds()))
	{
		// timed out a login attempt, so say so
		loginAttemptTime = 0;
		if (OnlineIsEnabled())
		{
			s_onlineLoginPending = FALSE;
			DEBUG_LOG(("ONLINE LOGIN fail reason=timeout\n"));
			EnableLoginControls( TRUE );
			onlineLoginFailed( DISCONNECT_COULDNOTCONNECT );
		}
		else
		{
			UnicodeString title, body;
			AsciiString disconMunkee;
			disconMunkee.format("GUI:GSDisconReason4");	// ("could not connect to server")
			title = TheGameText->fetch( "GUI:GSErrorTitle" );
			body = TheGameText->fetch( disconMunkee );
			GSMessageBoxOk( title, body );
			EnableLoginControls( TRUE );
		}

		// kill & restart the threads
		AsciiString motd = TheGameSpyInfo->getMOTD();
		AsciiString config = TheGameSpyInfo->getConfig();
		DEBUG_LOG(("Tearing down GameSpy from WOLLoginMenuUpdate(login timeout)\n"));
		TearDownGameSpy();
		SetUpGameSpy( motd.str(), config.str() );
	}

}// WOLLoginMenuUpdate

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLoginMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitTest( state, KEY_STATE_UP ) )
					{
						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																							(WindowMsgData)buttonBack, buttonBackID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;
}// WOLLoginMenuInput

static Bool isNickOkay(UnicodeString nick)
{
	static const WideChar * legalIRCChars = u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]`_^{|}-";

	Int len = nick.getLength();
	if (len == 0)
		return TRUE;

	if (len == 1 && nick.getCharAt(0) == u'-')
		return FALSE;

	WideChar newChar = nick.getCharAt(len-1);
	if (WideCharChr(legalIRCChars, newChar) == NULL)
		return FALSE;

	return TRUE;
}

/* Today's year, month or day, as a number: "yyyy", "MM" or "dd".  Windows asks GetDateFormat with that
	 numeric picture, as isAgeOkay always did; elsewhere it is localtime's, which is the same local date
	 and depends on no locale category. */
#define DATE_BUFFER_SIZE 256
static Int todaysDatePart( const char *picture )
{
#if defined(_WIN32)
	char dateBuffer[ DATE_BUFFER_SIZE ];
	GetDateFormat( LOCALE_SYSTEM_DEFAULT,
								 0, NULL,
								 picture,
								 dateBuffer, DATE_BUFFER_SIZE );
	return atoi(dateBuffer);
#else
	const time_t now = time( NULL );
	struct tm local;
	localtime_r( &now, &local );
	if (strcmp( picture, "yyyy" ) == 0)
		return local.tm_year + 1900;
	if (strcmp( picture, "MM" ) == 0)
		return local.tm_mon + 1;
	return local.tm_mday;
#endif
}

static Bool isAgeOkay(AsciiString &month, AsciiString &day, AsciiString year)
{
	if(month.isEmpty() || day.isEmpty() || year.isEmpty() || year.getLength() != 4)
		return FALSE;

	Int monthInt = atoi(month.str());
	Int dayInt = atoi(day.str());

	if(monthInt > 12 || dayInt > 31)
		return FALSE;
		// setup date buffer for local region date format
	month.format("%02.2d",monthInt);
	day.format("%02.2d",dayInt);

	// test the year first
	Int sysVal = todaysDatePart("yyyy");
	Int userVal = atoi(year.str());
	if(sysVal - userVal >= 14)
		return TRUE;
	else if( sysVal - userVal <= 12)
		return FALSE;

	sysVal = todaysDatePart("MM");
	userVal = atoi(month.str());
	if(sysVal - userVal >0 )
		return TRUE;
	else if( sysVal -userVal < 0 )
		return FALSE;
//	month.format("%02.2d",userVal);
	sysVal = todaysDatePart("dd");
	userVal = atoi(day.str());
	if(sysVal - userVal< 0)
		return FALSE;
//	day.format("%02.2d",userVal);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** WOL Login Menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLLoginMenuSystem( GameWindow *window, UnsignedInt msg, 
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg )
	{
		
		
		case GWM_CREATE:
			{
				break;
			} // case GWM_DESTROY:

		case GWM_DESTROY:
			{
				break;
			} // case GWM_DESTROY:

		case GWM_INPUT_FOCUS:
			{	
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}//case GWM_INPUT_FOCUS:

		// someone typed in a combo box.  Clear password (or fill it in if the typed name matches a known login name)
		case GCM_UPDATE_TEXT:
			{
				UnicodeString uNick = GadgetComboBoxGetText(comboBoxLoginName);
				UnicodeString uEmail = GadgetComboBoxGetText(comboBoxEmail);
				AsciiString nick, email;
				nick.translate(uNick);
				email.translate(uEmail);
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				UnicodeString trimmedNick = uNick, trimmedEmail = uEmail;
				trimmedNick.trim();
				trimmedEmail.trim();
				if (!trimmedNick.isEmpty())
				{
					if (trimmedNick.getCharAt(trimmedNick.getLength()-1) == u'\\')
						trimmedNick.removeLastChar();
					if (trimmedNick.getCharAt(trimmedNick.getLength()-1) == u'/')
						trimmedNick.removeLastChar();
				}
				if (!trimmedEmail.isEmpty())
				{
					if (trimmedEmail.getCharAt(trimmedEmail.getLength()-1) == u'\\')
						trimmedEmail.removeLastChar();
					if (trimmedEmail.getCharAt(trimmedEmail.getLength()-1) == u'/')
						trimmedEmail.removeLastChar();
				}
				if (trimmedEmail.getLength() != uEmail.getLength())
				{
					// we just trimmed a space.  set the text back and bail
					GadgetComboBoxSetText(comboBoxEmail, trimmedEmail);
					break;
				}
				if (trimmedNick.getLength() != nick.getLength())
				{
					// we just trimmed a space.  set the text back and bail
					GadgetComboBoxSetText(comboBoxLoginName, trimmedNick);
					break;
				}

				if (controlID == comboBoxEmailID)
				{
					// email changed.  look up password, and choose new login names

					// fill in the password for the selected email
					UnicodeString pass;
					pass.translate(loginPref->getPasswordForEmail(email));
					GadgetTextEntrySetText(textEntryPassword, pass);

					// fill in list of nicks for selected email, selecting the first
					AsciiStringList cachedNicks = loginPref->getNicksForEmail(email);
					AsciiStringListIterator nIt = cachedNicks.begin();
					Int selectedPos = -1;
					GadgetComboBoxReset(comboBoxLoginName);
					while (nIt != cachedNicks.end())
					{
						UnicodeString uniNick;
						uniNick.translate(*nIt);
						GadgetComboBoxAddEntry(comboBoxLoginName, uniNick, GameSpyColor[GSCOLOR_DEFAULT]);
						selectedPos = 0;
						++nIt;
					}
					if (selectedPos >= 0)
					{
						GadgetComboBoxSetSelectedPos(comboBoxLoginName, selectedPos);
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, true);
						AsciiString month,day,year;
						loginPref->getDateForEmail(email, month, day, year);
						pass.translate(month);
						GadgetTextEntrySetText(textEntryMonth, pass);
						pass.translate(day);
						GadgetTextEntrySetText(textEntryDay, pass);
						pass.translate(year);
						GadgetTextEntrySetText(textEntryYear, pass);

					}
					else
					{
						onlineRememberUnsaved( email );
						GadgetTextEntrySetText(textEntryMonth, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryDay, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryYear, UnicodeString::TheEmptyString);

					}
					onlineSavedLoginHint();
				}
				else if (controlID == comboBoxLoginNameID)
				{
					// they typed a new login name.  Email & pass shouldn't change
				}

				break;
			}

		// -online: typing a password drops the saved sign-in's line, emptying it brings it back
		case GEM_UPDATE_TEXT:
			{
				if (!buttonPushed && (GameWindow *)mData1 == textEntryPassword)
					onlineSavedLoginHint();
				break;
			}

		case GCM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// Enter in the email field signs in; the letters typed already filled the rest
				if (controlID == comboBoxEmailID && mData2 == 1)
				{
					pressLogin();
					break;
				}

				if (controlID == comboBoxEmailID)
				{
					// email changed.  look up password, and choose new login names
					UnicodeString uEmail = GadgetComboBoxGetText(comboBoxEmail);
					AsciiString email;
					email.translate(uEmail);

					// fill in the password for the selected email
					UnicodeString pass;
					pass.translate(loginPref->getPasswordForEmail(email));
					GadgetTextEntrySetText(textEntryPassword, pass);

					// fill in list of nicks for selected email, selecting the first
					AsciiStringList cachedNicks = loginPref->getNicksForEmail(email);
					AsciiStringListIterator nIt = cachedNicks.begin();
					Int selectedPos = -1;
					GadgetComboBoxReset(comboBoxLoginName);
					while (nIt != cachedNicks.end())
					{
						UnicodeString uniNick;
						uniNick.translate(*nIt);
						GadgetComboBoxAddEntry(comboBoxLoginName, uniNick, GameSpyColor[GSCOLOR_DEFAULT]);
						selectedPos = 0;
						++nIt;
					}
					if (selectedPos >= 0)
					{
						GadgetComboBoxSetSelectedPos(comboBoxLoginName, selectedPos);
						GadgetCheckBoxSetChecked(checkBoxRememberPassword, true);
						AsciiString month,day,year;
						loginPref->getDateForEmail(email, month, day, year);
						pass.translate(month);
						GadgetTextEntrySetText(textEntryMonth, pass);
						pass.translate(day);
						GadgetTextEntrySetText(textEntryDay, pass);
						pass.translate(year);
						GadgetTextEntrySetText(textEntryYear, pass);

					}
					else
					{
						onlineRememberUnsaved( email );
						GadgetTextEntrySetText(textEntryMonth, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryDay, UnicodeString::TheEmptyString);
						GadgetTextEntrySetText(textEntryYear, UnicodeString::TheEmptyString);
					}
					onlineSavedLoginHint();

				}
				else if (controlID == comboBoxLoginNameID)
				{
					// they typed a new login name.  Email & pass shouldn't change
				}
				break;
			}

		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				// If we back out, just bail - we haven't gotten far enough to need to log out
				if ( controlID == buttonBackID )
				{
					buttonPushed = true;
					TearDownGameSpy();
					TheShell->pop();
				} //if ( controlID == buttonBack )
#ifdef ALLOW_NON_PROFILED_LOGIN
				else if ( controlID == buttonUseAccountID )
				{
					buttonPushed = true;
					nextScreen = "Menus/GameSpyLoginProfile.wnd";
					TheShell->pop();
					//TheShell->push( "Menus/GameSpyLoginProfile.wnd" );
				} //if ( controlID == buttonUseAccount )
				else if ( controlID == buttonDontUseAccountID )
				{
					buttonPushed = true;
					nextScreen = "Menus/GameSpyLoginQuick.wnd";
					TheShell->pop();
					//TheShell->push( "Menus/GameSpyLoginQuick.wnd" );
				} //if ( controlID == buttonDontUseAccount )
#endif // ALLOW_NON_PROFILED_LOGIN
				else if ( controlID == buttonCreateAccountID )
				{
					// -online: accounts are made on the site; the button opens its register page
					if (OnlineIsEnabled())
					{
						onlineOpenPage( OnlineGetRegisterUrl(), "REGISTER" );
						break;
					}
#ifdef ALLOW_NON_PROFILED_LOGIN
					if (GameSpyUseProfiles)
					{
#endif // ALLOW_NON_PROFILED_LOGIN
						// actually attempt to create an account based on info entered
						AsciiString month, day, year;
						month.translate( GadgetTextEntryGetText(textEntryMonth) );
						day.translate( GadgetTextEntryGetText(textEntryDay) );
						year.translate( GadgetTextEntryGetText(textEntryYear) );
						
						if(!isAgeOkay(month, day, year))
						{
							GSMessageBoxOk(TheGameText->fetch("GUI:AgeFailedTitle"), TheGameText->fetch("GUI:AgeFailed"));
							break;
						}
						
						AsciiString login, password, email;
						email.translate( GadgetComboBoxGetText(comboBoxEmail) );
						login.translate( GadgetComboBoxGetText(comboBoxLoginName) );
						password.translate( GadgetTextEntryGetText(textEntryPassword) );

						if ( !email.isEmpty() && !login.isEmpty() && !password.isEmpty() )
						{
							loginAttemptTime = Clock_Milliseconds();
							BuddyRequest req;
							req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGINNEW;
							strcpy(req.arg.login.nick, login.str());
							strcpy(req.arg.login.email, email.str());
							strcpy(req.arg.login.password, password.str());
							req.arg.login.hasFirewall = TRUE;
							
							TheGameSpyInfo->setLocalBaseName( login );
							//TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
							TheGameSpyInfo->setLocalEmail( email );
							TheGameSpyInfo->setLocalPassword( password );

							TheGameSpyBuddyMessageQueue->addRequest( req );
							s_rememberLogin = checkBoxRememberPassword && GadgetCheckBoxIsChecked(checkBoxRememberPassword);
							if(s_rememberLogin)
							{
								(*loginPref)["lastName"] = login;
								(*loginPref)["lastEmail"] = email;
								(*loginPref)["useProfiles"] = "yes";
								AsciiString date;
								date = month;
								date.concat(day);
								date.concat(year);

								// -online keeps the session token instead (OnlineSaveToken at login)
								loginPref->addLogin(email, login, OnlineIsEnabled() ? AsciiString::TheEmptyString : password, date);
							}
							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
						else
						{
							// user didn't fill in all info.  prompt him.
							if(email.isEmpty() && login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
							else if( email.isEmpty() && login.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmailNickname"));
							else if( email.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmailPassword"));
							else if( login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoNicknamePassword"));
							else if( email.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoEmail"));
							else if( password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoPassword"));
							else if( login.isEmpty() )
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoNickname"));
							else
								GSMessageBoxOk(TheGameText->fetch("GUI:Error"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
						}
#ifdef ALLOW_NON_PROFILED_LOGIN
					}
					else
					{
						// not the profile screen - switch to it
						buttonPushed = TRUE;
						nextScreen = "Menus/GameSpyLoginProfile.wnd";
						TheShell->pop();
					}
#endif // ALLOW_NON_PROFILED_LOGIN
				} //if ( controlID == buttonCreateAccount )
				else if ( controlID == buttonLoginID )
				{
					// -online: Sign In wakes up again after a failed connect, and then dials before anything else
					if (OnlineIsEnabled() && OnlineGetStatus() != ONLINE_WELCOMED)
					{
						onlineStartConnect();
						break;
					}
					AsciiString login, password, email;

#ifdef ALLOW_NON_PROFILED_LOGIN
					if (GameSpyUseProfiles)
					{
#endif // ALLOW_NON_PROFILED_LOGIN
						AsciiString month, day, year;
						month.translate( GadgetTextEntryGetText(textEntryMonth) );
						day.translate( GadgetTextEntryGetText(textEntryDay) );
						year.translate( GadgetTextEntryGetText(textEntryYear) );

						// -online asks no birth date: the site's register page is where an account is made
						if(!OnlineIsEnabled() && !isAgeOkay(month, day, year))
						{
							GSMessageBoxOk(TheGameText->fetch("GUI:AgeFailedTitle"), TheGameText->fetch("GUI:AgeFailed"));
							break;
						}

						email.translate( GadgetComboBoxGetText(comboBoxEmail) );
						login.translate( GadgetComboBoxGetText(comboBoxLoginName) );
						password.translate( GadgetTextEntryGetText(textEntryPassword) );
						if (OnlineIsEnabled())
							login = email;		// one field, the email or the nick: the server takes either

						// -online: an empty password with a remembered session token logs in with the token
						AsciiString token;
						if (OnlineIsEnabled() && password.isEmpty() && !email.isEmpty())
							token = OnlineGetSavedToken( email );

						if ( !email.isEmpty() && !login.isEmpty() && (!password.isEmpty() || token.isNotEmpty()) )
						{
							loginAttemptTime = Clock_Milliseconds();
							TheGameSpyInfo->setLocalBaseName( login );
							//TheGameSpyInfo->setLocalProfileID( resp.player.profileID );
							TheGameSpyInfo->setLocalEmail( email );
							TheGameSpyInfo->setLocalPassword( password );

							if (token.isNotEmpty())
							{
								OnlineResume( token );
							}
							else
							{
								BuddyRequest req;
								req.buddyRequestType = BuddyRequest::BUDDYREQUEST_LOGIN;
								strcpy(req.arg.login.nick, login.str());
								strcpy(req.arg.login.email, email.str());
								strcpy(req.arg.login.password, password.str());
								req.arg.login.hasFirewall = true;
								TheGameSpyBuddyMessageQueue->addRequest( req );
							}
							s_rememberLogin = checkBoxRememberPassword && GadgetCheckBoxIsChecked(checkBoxRememberPassword);
							if(s_rememberLogin)
							{
								(*loginPref)["lastName"] = login;
								(*loginPref)["lastEmail"] = email;
								(*loginPref)["useProfiles"] = "yes";
								AsciiString date;
								date = month;
								date.concat(day);
								date.concat(year);

								// -online keeps the session token instead (OnlineSaveToken at login)
								loginPref->addLogin(email, login, OnlineIsEnabled() ? AsciiString::TheEmptyString : password, date);
							}
							else
							{
								// unticked forgets this login, and the next start no longer opens on it
								loginPref->forgetLogin(email);
								if ((*loginPref)["lastEmail"] == email)
									loginPref->erase("lastEmail");
							}
							if (OnlineIsEnabled())
							{
								onlineLoginSent( email, token.isNotEmpty() ? "resume" : "login" );
								onlineLoginLabel( "GUI:OnlineSigningIn" );
								onlineStatus( UnicodeString::TheEmptyString );
							}
							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
						else
						{
							// user didn't fill in all info.  prompt him.  -online has two fields, and says so in the status line
							if (OnlineIsEnabled())
							{
								onlineStatus( TheGameText->fetch( "GUI:OnlineLoginEmpty" ), TRUE );
								TheWindowManager->winSetFocus( email.isEmpty() ? comboBoxEmail : textEntryPassword );
							}
							else if(email.isEmpty() && login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
							else if( email.isEmpty() && login.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmailNickname"));
							else if( email.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmailPassword"));
							else if( login.isEmpty() && password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoNicknamePassword"));
							else if( email.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoEmail"));
							else if( password.isEmpty())
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoPassword"));
							else if( login.isEmpty() )
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoNickname"));
							else
								GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSNoLoginInfoAll"));
						}
#ifdef ALLOW_NON_PROFILED_LOGIN
					}
					else
					{
						login.translate( GadgetTextEntryGetText(textEntryLoginName) );

						if ( !login.isEmpty() )
						{
							loginAttemptTime = Clock_Milliseconds();
							PeerRequest req;
							req.peerRequestType = PeerRequest::PEERREQUEST_LOGIN;
							req.nick = login.str();
							req.login.profileID = 0;
							TheGameSpyPeerMessageQueue->addRequest( req );

							(*loginPref)["lastName"] = login;
							loginPref->erase("lastEmail");
							(*loginPref)["useProfiles"] = "no";
							EnableLoginControls( FALSE );

							// fire off some pings
							startPings();
						}
					}
#endif // ALLOW_NON_PROFILED_LOGIN

				} //if ( controlID == buttonLogin )
				else if ( controlID == buttonTOSID && OnlineIsEnabled() )
				{
					// -online: that button is Forgot Password, the site's reset page
					onlineOpenPage( OnlineGetResetUrl(), "RESET" );
				}
				else if ( controlID == buttonTOSID )
				{
					parentTOS->winHide(FALSE);
					useWebBrowserForTOS = FALSE;//loginPref->getBool("UseTOSBrowser", TRUE);
					if (useWebBrowserForTOS && (TheWebBrowser != NULL))
					{
						TheWebBrowser->createBrowserWindow("TermsOfService", listboxTOS);
						webBrowserActive = TRUE;
					}
					else
					{
						// Okay, no web browser.  This means we're looking at a UTF-8 text file.
						GadgetListBoxReset(listboxTOS);
						AsciiString fileName;
						fileName.format("Data\\%s\\TOS.txt", GetRegistryLanguage().str());
						File *theFile = TheFileSystem->openFile(fileName.str(), File::READ);
						if (theFile)
						{
							Int size = theFile->size();

							char *fileBuf = new char[size];
							Color tosColor = GameMakeColor(255, 255, 255, 255);

							Int bytesRead = theFile->read(fileBuf, size);
							if (bytesRead == size && size > 2)
							{
								fileBuf[size-1] = 0; // just to be safe
								AsciiString asciiBuf = fileBuf+2;
								AsciiString asciiLine;
								while (asciiBuf.nextToken(&asciiLine, "\r\n"))
								{
									UnicodeString uniLine;
									uniLine = UnicodeString(MultiByteToWideCharSingleLine(asciiLine.str()).c_str());
									int len = uniLine.getLength();
									for (int index = len-1; index >= 0; index--)
									{
										if (WideCharIsSpace(uniLine.getCharAt(index)))
										{
											uniLine.removeLastChar();
										}
										else
										{
											break;
										}
									}
									//uniLine.trim();
									DEBUG_LOG(("adding TOS line: [%s]\n", WideCharAsUtf8( uniLine.str() ).str()));
									GadgetListBoxAddEntryText(listboxTOS, uniLine, tosColor, -1);
								}

							}

							delete fileBuf;
							fileBuf = NULL;

							theFile->close();
							theFile = NULL;
						}
					}
					EnableLoginControls( FALSE );
					buttonBack->winEnable(FALSE);
					
				}
				else if ( controlID == buttonTOSOKID )
				{
					EnableLoginControls( TRUE );

					parentTOS->winHide(TRUE);
					if (useWebBrowserForTOS && (TheWebBrowser != NULL))
					{
						if (listboxTOS != NULL)
						{
							TheWebBrowser->closeBrowserWindow(listboxTOS);
						}
					}

					OptionPreferences optionPref;
					optionPref["SawTOS"] = "yes";
					optionPref.write();
					webBrowserActive = FALSE;
					buttonBack->winEnable(TRUE);
				}
				break;
			}// case GBM_SELECTED:
	
		case GEM_EDIT_DONE:
			{
				// Enter in the password field signs in
				if ((GameWindow *)mData1 == textEntryPassword)
					pressLogin();
				break;
			}
			/*
		case GEM_UPDATE_TEXT:
			{
				if (buttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == textEntryLoginNameID )
				{
					UnicodeString munkee = GadgetTextEntryGetText( textEntryLoginName );
					if ( !isNickOkay( munkee ) )
					{
						munkee.removeLastChar();
						GadgetTextEntrySetText( textEntryLoginName, munkee );
					}
				}// if ( controlID == textEntryLoginNameID )
				break;
			}//case GEM_UPDATE_TEXT:
			*/
		default:
			return MSG_IGNORED;

	}//Switch

	return MSG_HANDLED;
}// WOLLoginMenuSystem


