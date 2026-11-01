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

// FILE: OptionsMenu.cpp //////////////////////////////////////////////////////////////////////////
// Author: Colin Day, October 2001
// Description: options menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "gamespy/ghttp/ghttp.h"

#include "Common/AudioAffect.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/UserPreferences.h"
#include "Common/GameLOD.h"
#include "Common/Monitors.h"
#include "Common/EarlyOptions.h"
#include "Common/OptionsCatalog.h"
#include "Common/Registry.h"
#include "Common/version.h"

#include "GameClient/ControlBar.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameText.h"
#include "GameClient/Display.h"
#include "GameClient/IMEManager.h"
#include "GameClient/ShellHooks.h"
#include "GameClient/GUICallbacks.h"
#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "WWDownload/Registry.h"
#include "Platform/HazardCloud.h"
#include "Platform/ModernWaterBuild.h"
//added by saad
//used to access a messagebox that does "ok" and "cancel"
#include "GameClient/MessageBox.h"

// This is for non-RC builds only!!!
#define VERBOSE_VERSION u"Release"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


static NameKeyType		comboBoxOnlineIPID	= NAMEKEY_INVALID;
static GameWindow *		comboBoxOnlineIP		= NULL;

static NameKeyType		comboBoxLANIPID	= NAMEKEY_INVALID;
static GameWindow *		comboBoxLANIP		= NULL;

static NameKeyType    comboBoxAntiAliasingID   = NAMEKEY_INVALID;
static GameWindow *   comboBoxAntiAliasing     = NULL;

static NameKeyType    comboBoxResolutionID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxResolution       = NULL;

static NameKeyType    comboBoxMonitorID         = NAMEKEY_INVALID;
static GameWindow *   comboBoxMonitor          = NULL;

// What the two display lists hold, entry for entry: the monitors on the desktop, and the sizes the
// monitor picked in the first list offers.  Filled when the menu opens and, for the sizes, again
// whenever another monitor is picked.
static MonitorEntry			menuMonitors[MAX_MONITOR_ENTRIES];
static Int							menuMonitorCount = 0;
static DisplayModeEntry	menuModes[MAX_DISPLAY_MODE_ENTRIES];
static Int							menuModeCount = 0;

static NameKeyType    comboBoxDetailID      = NAMEKEY_INVALID;
static GameWindow *   comboBoxDetail        = NULL; 

static NameKeyType		checkAlternateMouseID	= NAMEKEY_INVALID;
static GameWindow *		checkAlternateMouse		= NULL;

static NameKeyType		checkRetaliationID	= NAMEKEY_INVALID;
static GameWindow *		checkRetaliation		= NULL;

static NameKeyType		checkDoubleClickAttackMoveID	= NAMEKEY_INVALID;
static GameWindow *		checkDoubleClickAttackMove		= NULL;

static NameKeyType		sliderScrollSpeedID	= NAMEKEY_INVALID;
static GameWindow *		sliderScrollSpeed		= NULL;

static NameKeyType    checkLanguageFilterID = NAMEKEY_INVALID;
static GameWindow *   checkLanguageFilter   = NULL;

static NameKeyType		checkUseCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkUseCamera			= NULL;

static NameKeyType		checkSaveCameraID		= NAMEKEY_INVALID;
static GameWindow *		checkSaveCamera			= NULL;

static NameKeyType		checkSendDelayID		= NAMEKEY_INVALID;
static GameWindow *		checkSendDelay			= NULL;

static NameKeyType		checkDrawAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkDrawAnchor			= NULL;

static NameKeyType		checkMoveAnchorID		= NAMEKEY_INVALID;
static GameWindow *		checkMoveAnchor			= NULL;

static NameKeyType		buttonFirewallRefreshID	= NAMEKEY_INVALID;
static GameWindow *		buttonFirewallRefresh		= NULL;
//
//static NameKeyType    checkAudioHardwareID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioHardware   = NULL;
//
//static NameKeyType    checkAudioSurroundID = NAMEKEY_INVALID;
//static GameWindow *   checkAudioSurround   = NULL;
////volume controls
//
static NameKeyType    sliderMusicVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderMusicVolume   = NULL;

static NameKeyType    sliderSFXVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderSFXVolume   = NULL;

static NameKeyType    sliderVoiceVolumeID = NAMEKEY_INVALID;
static GameWindow *   sliderVoiceVolume   = NULL;

static NameKeyType    sliderGammaID = NAMEKEY_INVALID;
static GameWindow *   sliderGamma = NULL;

// The Graphics page's detail controls, which used to be EA's advanced display popup
static NameKeyType    sliderTextureResolutionID = NAMEKEY_INVALID;
static GameWindow *   sliderTextureResolution = NULL;

static NameKeyType    sliderParticleCapID = NAMEKEY_INVALID;
static GameWindow *   sliderParticleCap = NULL;

static NameKeyType    check3DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check3DShadows   = NULL;

static NameKeyType    check2DShadowsID = NAMEKEY_INVALID;
static GameWindow *   check2DShadows   = NULL;

static NameKeyType    checkCloudShadowsID = NAMEKEY_INVALID;
static GameWindow *   checkCloudShadows   = NULL;

static NameKeyType    checkGroundLightingID = NAMEKEY_INVALID;
static GameWindow *   checkGroundLighting   = NULL;

static NameKeyType    checkSmoothWaterID = NAMEKEY_INVALID;
static GameWindow *   checkSmoothWater   = NULL;

static NameKeyType    checkBuildingOcclusionID = NAMEKEY_INVALID;
static GameWindow *   checkBuildingOcclusion   = NULL;

static NameKeyType    checkPropsID = NAMEKEY_INVALID;
static GameWindow *   checkProps   = NULL;

static NameKeyType    checkExtraAnimationsID = NAMEKEY_INVALID;
static GameWindow *   checkExtraAnimations   = NULL;

static NameKeyType    checkNoDynamicLodID = NAMEKEY_INVALID;
static GameWindow *   checkNoDynamicLod   = NULL;

static NameKeyType    checkUnlockFpsID = NAMEKEY_INVALID;
static GameWindow *   checkUnlockFps   = NULL;

static NameKeyType    checkHeatEffectsID = NAMEKEY_INVALID;
static GameWindow *   checkHeatEffects   = NULL;

static NameKeyType    checkTreeSwayID = NAMEKEY_INVALID;
static GameWindow *   checkTreeSway   = NULL;

/*

static NameKeyType    radioHighID = NAMEKEY_INVALID;
static GameWindow *   radioHigh   = NULL;
static NameKeyType    radioMediumID = NAMEKEY_INVALID;
static GameWindow *   radioMedium   = NULL;
static NameKeyType    radioLowID = NAMEKEY_INVALID;
static GameWindow *   radioLow   = NULL;

*/

//Added By Saad for the resolution confirmation dialog box
DisplaySettings oldDispSettings, newDispSettings;
Bool dispChanged = FALSE;
extern Int timer;
extern void DoResolutionDialog();

//
// Set by saveOptions when the resolution really changed, acted on by applyPendingShellRebuild once
// the options layout has been taken down and nothing is reading a window out of it any more.
//
static Bool pendingShellRebuild = FALSE;
static Bool defaultsPressed = FALSE;
//

static Bool ignoreSelected = FALSE;
WindowLayout *OptionsLayout = NULL;

//-------------------------------------------------------------------------------------------------
// The pages, and the buttons that show them.
//
// EA's screen was one panel with every control on it, and it was already out of room when it
// shipped - the language filter, the keyboard button and the four camera check boxes are all still
// in the layout, parked off the right edge with HIDDEN set because there was nowhere to put them.
// The layout now sorts the same controls into seven pages on one grid (Tools/optionsmenu_layout.py);
// this is the two arrays that name them and the one function that decides which one you are
// looking at.  Shell::optionsLayoutFile names the file, OptionsMenuClassic.wnd.
//-------------------------------------------------------------------------------------------------
enum { OPTIONS_PAGE_COUNT = 8 };

static const char *TheOptionsPageNames[ OPTIONS_PAGE_COUNT ] =
{
	"OptionsMenu.wnd:PageDisplay",
	"OptionsMenu.wnd:PageGraphics",
	"OptionsMenu.wnd:PageEffects",
	"OptionsMenu.wnd:PageAudio",
	"OptionsMenu.wnd:PageControls",
	"OptionsMenu.wnd:PageInterface",
	"OptionsMenu.wnd:PageNetwork",
	"OptionsMenu.wnd:PageHotkeys",
};

static const char *TheOptionsTabNames[ OPTIONS_PAGE_COUNT ] =
{
	"OptionsMenu.wnd:TabDisplay",
	"OptionsMenu.wnd:TabGraphics",
	"OptionsMenu.wnd:TabEffects",
	"OptionsMenu.wnd:TabAudio",
	"OptionsMenu.wnd:TabControls",
	"OptionsMenu.wnd:TabInterface",
	"OptionsMenu.wnd:TabNetwork",
	"OptionsMenu.wnd:TabHotkeys",
};

static GameWindow *		optionsPage[ OPTIONS_PAGE_COUNT ]	= { NULL };
static GameWindow *		optionsTab[ OPTIONS_PAGE_COUNT ]	= { NULL };
static NameKeyType		optionsTabID[ OPTIONS_PAGE_COUNT ] =
{
	NAMEKEY_INVALID, NAMEKEY_INVALID, NAMEKEY_INVALID, NAMEKEY_INVALID, NAMEKEY_INVALID, NAMEKEY_INVALID,
	NAMEKEY_INVALID, NAMEKEY_INVALID
};

// The Hotkeys page, OptionsMenuHotkeys.cpp.  Its keys are written on Accept like the rest.
void OptionsHotkeysInit( void );
Bool OptionsHotkeysSystem( UnsignedInt msg, GameWindow *control, WindowMsgData row );
Bool OptionsHotkeysKey( UnsignedByte key, Int state );
void OptionsHotkeysSave( void );
void OptionsHotkeysClose( void );

//-------------------------------------------------------------------------------------------------
/** Show one page and hide the rest.
	*
	* The page you are on is the disabled button, which is the one piece of tab feedback available
	* without drawing new artwork: a pressed-looking tab would need a second image per button, and
	* the buttons are cloned from one that already exists. */
//-------------------------------------------------------------------------------------------------
static void showOptionsPage( Int which )
{
	for( Int i = 0; i < OPTIONS_PAGE_COUNT; ++i )
	{
		if( optionsPage[ i ] )
			optionsPage[ i ]->winHide( i != which );
		if( optionsTab[ i ] )
			optionsTab[ i ]->winEnable( i != which );
	}
}

//-------------------------------------------------------------------------------------------------
// The main menu's button columns under the options panel.
//
// The panel is the layouts' see-through black, so over the main menu its buttons showed through as
// ghosts on every page.  While options is up over a main menu on screen, the columns are hidden, and
// shown again on the way out.  The panel's draw hides them each frame rather than once: a main menu
// transition still running when options opens (-optionsmenu opens it over the menu's first frames)
// shows its column again when it ends.  The main menu's logo and frame stay; they are the screen's
// mark and its edge.
//-------------------------------------------------------------------------------------------------
enum { MAIN_MENU_COLUMN_COUNT = 7 };

static const char *TheMainMenuColumnNames[ MAIN_MENU_COLUMN_COUNT ] =
{
	"MainMenu.wnd:MapBorder",
	"MainMenu.wnd:MapBorder1",
	"MainMenu.wnd:MapBorder2",
	"MainMenu.wnd:MapBorder3",
	"MainMenu.wnd:MapBorder4",
	"MainMenu.wnd:MapBorder5",
	"MainMenu.wnd:MapBorder6",
};

static Bool mainMenuColumnHidden[ MAIN_MENU_COLUMN_COUNT ] = { FALSE };
static GameWinDrawFunc optionsPanelDraw = NULL;

/** The main menu's parent window when the main menu is what options opened over, else NULL. */
static GameWindow *mainMenuOnScreen( void )
{
	GameWindow *mainMenu = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "MainMenu.wnd:MainMenuParent" ) );
	if( mainMenu == NULL || mainMenu->winIsHidden() )
		return NULL;
	return mainMenu;
}

static void hideMainMenuColumns( void )
{
	GameWindow *mainMenu = mainMenuOnScreen();
	if( mainMenu == NULL )
		return;
	for( Int i = 0; i < MAIN_MENU_COLUMN_COUNT; ++i )
	{
		GameWindow *column = TheWindowManager->winGetWindowFromId( mainMenu, TheNameKeyGenerator->nameToKey( TheMainMenuColumnNames[ i ] ) );
		if( column->winIsHidden() )
			continue;
		column->winHide( TRUE );
		mainMenuColumnHidden[ i ] = TRUE;
	}
}

static void showMainMenuColumns( void )
{
	GameWindow *mainMenu = mainMenuOnScreen();
	for( Int i = 0; i < MAIN_MENU_COLUMN_COUNT; ++i )
	{
		if( mainMenu != NULL && mainMenuColumnHidden[ i ] )
			TheWindowManager->winGetWindowFromId( mainMenu, TheNameKeyGenerator->nameToKey( TheMainMenuColumnNames[ i ] ) )->winHide( FALSE );
		mainMenuColumnHidden[ i ] = FALSE;
	}
}

static void drawOptionsPanel( GameWindow *window, WinInstanceData *instData )
{
	hideMainMenuColumns();
	optionsPanelDraw( window, instData );
}

enum Detail
{
	ULTRADETAIL = 0,
	HIGHDETAIL,
	MEDIUMDETAIL,
	LOWDETAIL,
	CUSTOMDETAIL,

	DETAIL,
};


OptionPreferences::OptionPreferences( void )
{
	// note, the superclass will put this in the right dir automatically, this is just a leaf name
	load("Options.ini");
}

OptionPreferences::~OptionPreferences()
{
}


Int OptionPreferences::getCampaignDifficulty(void)
{
	OptionPreferences::const_iterator it = find("CampaignDifficulty");
	if (it == end())
		return TheScriptEngine->getGlobalDifficulty();

	Int factor = atoi(it->second.str());
	if (factor < DIFFICULTY_EASY)
		factor = DIFFICULTY_EASY;
	if (factor > DIFFICULTY_HARD)
		factor = DIFFICULTY_HARD;
	
	return factor;
}

void OptionPreferences::setCampaignDifficulty( Int diff )
{
	AsciiString prefString;
	prefString.format("%d", diff );
	(*this)["CampaignDifficulty"] = prefString;
}

UnsignedInt OptionPreferences::getLANIPAddress(void)
{
	AsciiString selectedIP = (*this)["IPAddress"];
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	while (IPlist)
	{
		if (selectedIP.compareNoCase(IPlist->getIPstring()) == 0)
		{
			return IPlist->getIP();
		}
		IPlist = IPlist->getNext();
	}
	return TheGlobalData->m_defaultIP;
}

void OptionPreferences::setLANIPAddress( AsciiString IP )
{
	(*this)["IPAddress"] = IP;
}

void OptionPreferences::setLANIPAddress( UnsignedInt IP )
{
	AsciiString tmp;
	tmp.format("%d.%d.%d.%d", ((IP & 0xff000000) >> 24), ((IP & 0xff0000) >> 16), ((IP & 0xff00) >> 8), (IP & 0xff));
	(*this)["IPAddress"] = tmp;
}

UnsignedInt OptionPreferences::getOnlineIPAddress(void)
{
	AsciiString selectedIP = (*this)["GameSpyIPAddress"];
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	while (IPlist)
	{
		if (selectedIP.compareNoCase(IPlist->getIPstring()) == 0)
		{
			return IPlist->getIP();
		}
		IPlist = IPlist->getNext();
	}
	return TheGlobalData->m_defaultIP;
}

void OptionPreferences::setOnlineIPAddress( AsciiString IP )
{
	(*this)["GameSpyIPAddress"] = IP;
}

void OptionPreferences::setOnlineIPAddress( UnsignedInt IP )
{
	AsciiString tmp;
	tmp.format("%d.%d.%d.%d", ((IP & 0xff000000) >> 24), ((IP & 0xff0000) >> 16), ((IP & 0xff00) >> 8), (IP & 0xff));
	(*this)["GameSpyIPAddress"] = tmp;
}

Bool OptionPreferences::getAlternateMouseModeEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseAlternateMouse");
	if (it == end())
		return TheGlobalData->m_useAlternateMouse;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getRetaliationModeEnabled(void)
{
	OptionPreferences::const_iterator it = find("Retaliation");
	if (it == end())
		return TheGlobalData->m_clientRetaliationModeEnabled;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getDoubleClickAttackMoveEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseDoubleClickAttackMove");
	if( it == end() )
		return TheGlobalData->m_doubleClickAttackMove;

	if( strcasecmp( it->second.str(), "yes" ) == 0 )
		return TRUE;

	return FALSE;
}

Real OptionPreferences::getScrollFactor(void)
{
	OptionPreferences::const_iterator it = find("ScrollFactor");
	if (it == end())
		return TheGlobalData->m_keyboardDefaultScrollFactor;

	Int factor = atoi(it->second.str());
	if (factor < 0)
		factor = 0;
	if (factor > 100)
		factor = 100;
	
	return factor/100.0f;
}

Bool OptionPreferences::usesSystemMapDir(void)
{
	OptionPreferences::const_iterator it = find("UseSystemMapDir");
	if (it == end())
		return TRUE;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::saveCameraInReplays(void)
{
	OptionPreferences::const_iterator it = find("SaveCameraInReplays");
	if (it == end())
		return TRUE;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::useCameraInReplays(void)
{
	OptionPreferences::const_iterator it = find("UseCameraInReplays");
	if (it == end())
		return TRUE;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Int OptionPreferences::getIdealStaticGameDetail(void)
{
	OptionPreferences::const_iterator it = find("IdealStaticGameLOD");
	if (it == end())
		return STATIC_GAME_LOD_UNKNOWN;

	return TheGameLODManager->getStaticGameLODIndex(it->second);
}

Int OptionPreferences::getStaticGameDetail(void)
{
	OptionPreferences::const_iterator it = find("StaticGameLOD");
	if (it == end())
		return TheGameLODManager->getStaticLODLevel();

	return TheGameLODManager->getStaticGameLODIndex(it->second);
}

Bool OptionPreferences::getSendDelay(void)
{
	OptionPreferences::const_iterator it = find("SendDelay");
	if (it == end())
		return TheGlobalData->m_firewallSendDelay;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Int OptionPreferences::getFirewallBehavior()
{
	OptionPreferences::const_iterator it = find("FirewallBehavior");
	if (it == end())
		return TheGlobalData->m_firewallBehavior;

	Int behavior = atoi(it->second.str());
	if (behavior < 0)
	{
		behavior = 0;
	}
	return behavior;
}

Short OptionPreferences::getFirewallPortAllocationDelta()
{
	OptionPreferences::const_iterator it = find("FirewallPortAllocationDelta");
	if (it == end()) {
		return TheGlobalData->m_firewallPortAllocationDelta;
	}

	Short delta = atoi(it->second.str());
	return delta;
}

UnsignedShort OptionPreferences::getFirewallPortOverride()
{
	OptionPreferences::const_iterator it = find("FirewallPortOverride");
	if (it == end()) {
		return TheGlobalData->m_firewallPortOverride;
	}

	Int override = atoi(it->second.str());
	if (override < 0 || override > 65535)
		override = 0;
	return override;
}

Bool OptionPreferences::getFirewallNeedToRefresh()
{
	OptionPreferences::const_iterator it = find("FirewallNeedToRefresh");
	if (it == end()) {
		return FALSE;
	}

	Bool retval = FALSE;
	AsciiString str = it->second;
	if (str.compareNoCase("TRUE") == 0) {
		retval = TRUE;
	}
	return retval;
}

AsciiString OptionPreferences::getPreferred3DProvider(void)
{
	OptionPreferences::const_iterator it = find("3DAudioProvider");
	if (it == end())
		return TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS];
	return it->second;
}

AsciiString OptionPreferences::getSpeakerType(void)
{
	OptionPreferences::const_iterator it = find("SpeakerType");
	if (it == end())
		return TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getAudioSettings()->m_defaultSpeakerType2D);
	return it->second;
}

Real OptionPreferences::getSoundVolume(void)
{
	OptionPreferences::const_iterator it = find("SFXVolume");
	if (it == end())
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative < 0 )
		{
			Real scale = 1.0f + relative;
			return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_defaultSoundVolume * 100.0f;
	}

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Real OptionPreferences::get3DSoundVolume(void)
{
	OptionPreferences::const_iterator it = find("SFX3DVolume");
	if (it == end())
	{
		Real relative = TheAudio->getAudioSettings()->m_relative2DVolume;
		if( relative > 0 )
		{
			Real scale = 1.0f - relative;
			return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f * scale;
		}
		return TheAudio->getAudioSettings()->m_default3DSoundVolume * 100.0f;
	}

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Real OptionPreferences::getSpeechVolume(void)
{
	OptionPreferences::const_iterator it = find("VoiceVolume");
	if (it == end())
		return TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f;

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

Bool OptionPreferences::getCloudShadowsEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseCloudMap");
	if (it == end())
		return TheGlobalData->m_useCloudMap;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getLightmapEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseLightMap");
	if (it == end())
		return TheGlobalData->m_useLightMap;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getSmoothWaterEnabled(void)
{
	OptionPreferences::const_iterator it = find("ShowSoftWaterEdge");
	if (it == end())
		return TheGlobalData->m_showSoftWaterEdge;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getTreesEnabled(void)
{
	OptionPreferences::const_iterator it = find("ShowTrees");
	if (it == end())
		return TheGlobalData->m_useTrees;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getExtraAnimationsDisabled(void)
{
	OptionPreferences::const_iterator it = find("ExtraAnimations");
	if (it == end())
		return TheGlobalData->m_useDrawModuleLOD;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return FALSE;	//we are enabling extra animations, so disabled LOD
	}
	return TRUE;
}

/** Swaying trees used to ride on ExtraAnimations, so an Options.ini written before they had a box of
	* their own keeps doing what that setting did. */
Bool OptionPreferences::getTreeSwayEnabled(void)
{
	OptionPreferences::const_iterator it = find("TreeSway");
	if (it == end())
		return !getExtraAnimationsDisabled();

	return strcasecmp(it->second.str(), "yes") == 0;
}

Bool OptionPreferences::getUseHeatEffects(void)
{
	OptionPreferences::const_iterator it = find("HeatEffects");
	if (it == end())
		return TheGlobalData->m_useHeatEffects;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getDynamicLODEnabled(void)
{
	OptionPreferences::const_iterator it = find("DynamicLOD");
	if (it == end())
		return TheGlobalData->m_enableDynamicLOD;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getFPSLimitEnabled(void)
{
	OptionPreferences::const_iterator it = find("FPSLimit");
	if (it == end())
		return TheGlobalData->m_useFpsLimit;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::get3DShadowsEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseShadowVolumes");
	if (it == end())
		return TheGlobalData->m_useShadowVolumes;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::get2DShadowsEnabled(void)
{
	OptionPreferences::const_iterator it = find("UseShadowDecals");
	if (it == end())
		return TheGlobalData->m_useShadowDecals;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Bool OptionPreferences::getBuildingOcclusionEnabled(void)
{
	OptionPreferences::const_iterator it = find("BuildingOcclusion");
	if (it == end())
		return TheGlobalData->m_enableBehindBuildingMarkers;

	if (strcasecmp(it->second.str(), "yes") == 0) {
		return TRUE;
	}
	return FALSE;
}

Int OptionPreferences::getParticleCap(void)
{
	OptionPreferences::const_iterator it = find("MaxParticleCount");
	if (it == end())
		return TheGlobalData->m_maxParticleCount;

	Int factor = (Int) atoi(it->second.str());
	if (factor < 100)	//clamp to at least 100 particles.
		factor = 100;

	return factor;
}

/* There is deliberately no MaxCameraHeight here.  The zoom ceiling is how much of the battlefield
	 you can see at once, so a local Options.ini key would hand one player a permanent advantage over
	 everyone else in the game - and unlike GameData.ini, Options.ini is not covered by the file
	 mismatch check that refuses to start a LAN game between machines whose data differs.  The
	 ceiling is fixed for everyone at ZOOM_OUT_LIMIT_FACTOR times the GameData.ini value. */

/* The fourteen getters that used to sit here - EdgeScrollInWindowedMode through BloomThreshold -
	 were fourteen copies of the same six lines, and the copy is what made adding a setting cost ten
	 edits.  They are rows in TheOptionCatalog now (Common/OptionsCatalog.cpp), which also gives them
	 the half they never had: the menu can write them back. */

Int OptionPreferences::getTextureReduction(void)
{
	OptionPreferences::const_iterator it = find("TextureReduction");
	if (it == end())
		return -1;	//unknown texture reduction

	Int factor = (Int) atoi(it->second.str());
	if (factor > 2)	//clamp it.
		factor=2;
	return factor;
}

Real OptionPreferences::getGammaValue(void)
{
	OptionPreferences::const_iterator it = find("Gamma");
 	if (it == end())
 		return 50.0f;
 
 	Real gamma = (Real) atoi(it->second.str());
 	return gamma;
}

void OptionPreferences::getResolution(Int *xres, Int *yres)
{
	*xres = TheGlobalData->m_xResolution;
	*yres = TheGlobalData->m_yResolution;

	OptionPreferences::const_iterator it = find("Resolution");
	if (it == end())
		return;

	Int selectedXRes,selectedYRes;
	if (sscanf(it->second.str(),"%d%d", &selectedXRes, &selectedYRes) != 2)
		return;

	*xres=selectedXRes;
	*yres=selectedYRes;
}

Real OptionPreferences::getMusicVolume(void)
{
	OptionPreferences::const_iterator it = find("MusicVolume");
	if (it == end())
		return TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f;

	Real volume = (Real) atof(it->second.str());
	if (volume < 0.0f)
	{
		volume = 0.0f;
	}
	return volume;
}

static OptionPreferences *pref = NULL;

static void setDefaults( void )
{
	//-------------------------------------------------------------------------------------------------
	// provider type
//	GadgetCheckBoxSetChecked(checkAudioHardware, FALSE);

	//-------------------------------------------------------------------------------------------------
	// speaker type
//	GadgetCheckBoxSetChecked(checkAudioSurround, FALSE);

	//-------------------------------------------------------------------------------------------------
	// language filter
	GadgetCheckBoxSetChecked( checkLanguageFilter, TRUE );

	//-------------------------------------------------------------------------------------------------
	// send Delay
	GadgetCheckBoxSetChecked(checkSendDelay, FALSE);
	
	// Default puts the *game* options back to their defaults.  It used to reach into the display
	// settings too - re-running the machine benchmark and dropping the resolution to 800x600 - so
	// one click on a menu full of audio and control settings threw away a monitor's worth of
	// picture and there was no undo.
	const Bool modifyDisplaySettings = FALSE;

	//-------------------------------------------------------------------------------------------------
	// LOD
	if (modifyDisplaySettings && ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE))) {
		StaticGameLODLevel level=TheGameLODManager->findStaticLODLevel();
		switch (level)
		{
		case STATIC_GAME_LOD_LOW:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, LOWDETAIL);
			break;
		case STATIC_GAME_LOD_MEDIUM:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, MEDIUMDETAIL);
			break;
		case STATIC_GAME_LOD_HIGH:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, HIGHDETAIL);
			break;
		case STATIC_GAME_LOD_ULTRA:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, ULTRADETAIL);
			break;
		case STATIC_GAME_LOD_CUSTOM:
			GadgetComboBoxSetSelectedPos(comboBoxDetail, CUSTOMDETAIL);
			break;
		default:
			DEBUG_ASSERTCRASH(FALSE,("Tried to set comboBoxDetail to a value of %d ", TheGameLODManager->getStaticLODLevel()) );
		};
	}
	
	//-------------------------------------------------------------------------------------------------
	// Mouse Mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, FALSE);
	GadgetCheckBoxSetChecked(checkRetaliation, TRUE );
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, FALSE );

	//-------------------------------------------------------------------------------------------------
//	// scroll speed val
	Int valMin, valMax;
//	GadgetSliderGetMinMax(sliderScrollSpeed,&valMin, &valMax);
//	GadgetSliderSetPosition(sliderScrollSpeed, ((valMax - valMin) / 2 + valMin));
	Int scrollPos = (Int)(TheGlobalData->m_keyboardDefaultScrollFactor*100.0f);
	GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );


	//-------------------------------------------------------------------------------------------------
	// slider music volume
	GadgetSliderGetMinMax(sliderMusicVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderMusicVolume,REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultMusicVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	GadgetSliderGetMinMax(sliderSFXVolume,&valMin, &valMax);
	Real maxVolume = MAX( TheAudio->getAudioSettings()->m_defaultSoundVolume, TheAudio->getAudioSettings()->m_default3DSoundVolume );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume * 100.0f ) );

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	GadgetSliderGetMinMax(sliderVoiceVolume,&valMin, &valMax);
	GadgetSliderSetPosition(sliderVoiceVolume, REAL_TO_INT(TheAudio->getAudioSettings()->m_defaultSpeechVolume * 100.0f));

	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	GadgetSliderGetMinMax(sliderGamma,&valMin, &valMax);
 	GadgetSliderSetPosition(sliderGamma, ((valMax - valMin) / 2 + valMin));

	//-------------------------------------------------------------------------------------------------
 	// Texture resolution slider
	//

	if ((TheGameLogic->isInGame() == FALSE) || (TheGameLogic->isInShellGame() == TRUE))
	{	
		Int	txtFact=TheGameLODManager->getRecommendedTextureReduction();

		GadgetSliderSetPosition( sliderTextureResolution, 2-txtFact);

		//-------------------------------------------------------------------------------------------------
 		// 3D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

		//-------------------------------------------------------------------------------------------------
 		// 2D Shadows checkbox
		//
		GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

		//-------------------------------------------------------------------------------------------------
 		// Cloud Shadows checkbox
		//
		GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

		//-------------------------------------------------------------------------------------------------
 		// Ground Lighting (lightmap) checkbox
		//
		GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

		//-------------------------------------------------------------------------------------------------
 		// Smooth Water Border checkbox
		//
		GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

		//-------------------------------------------------------------------------------------------------
 		// Extra Animations (buildups) and Swaying Trees checkboxes
		//
		GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);
		// the fork's box: a stock or modded OptionsMenu.wnd that wins over Run/Window has none
		if( checkTreeSway )
			GadgetCheckBoxSetChecked( checkTreeSway, TheGlobalData->m_useTreeSway);

		//-------------------------------------------------------------------------------------------------
 		// DisableDynamicLOD
		//
		GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

		//-------------------------------------------------------------------------------------------------
 		// Disable FPS Limit
		//
		GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

		//-------------------------------------------------------------------------------------------------
 		// Heat Effects
		//
		GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

		//-------------------------------------------------------------------------------------------------
 		// Building Occlusion checkbox
		//
		GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

		//-------------------------------------------------------------------------------------------------
 		// Particle Cap slider
		//
		GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount);

		//-------------------------------------------------------------------------------------------------
 		// Trees and Shrubs
		//
		GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	}
}

//-------------------------------------------------------------------------------------------------
/** The control this catalog row is shown by, or NULL while the row has no control yet. */
//-------------------------------------------------------------------------------------------------
static GameWindow *findOptionWidget( const OptionDef& def )
{
	if( def.widgetName == NULL || def.widgetName[ 0 ] == 0 )
		return NULL;

	return TheWindowManager->winGetWindowFromId(
		NULL, TheNameKeyGenerator->nameToKey( AsciiString( def.widgetName ) ) );
}

//-------------------------------------------------------------------------------------------------
/** GlobalData -> the controls, for every catalog row that has one.
	*
	* This is the pass EA wrote by hand once per setting, in three places each.  A combo box builds
	* its entries here too: an OPTION_ENUM's labelKey with the value appended is the CSF key, so
	* HealthBars with hi = 3 reads GUI:HealthBars0 through GUI:HealthBars3 and a fifth mode is a
	* bigger hi and one more string. */
//-------------------------------------------------------------------------------------------------
static void fillCatalogWidgets( void )
{
	const Color entryColor = GameMakeColor( 255, 255, 255, 255 );

	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];
		GameWindow *widget = findOptionWidget( def );
		if( widget == NULL )
			continue;

		if (strcmp(def.iniKey, "WaterStyle") == 0)
		{
			// The Linux renderer and Direct3D 11 draw the Modern water; -d3d9, -headless and macOS do not.
			const Bool supported = ModernWater::Supported();
			widget->winHide(!supported);
			GameWindow *label = TheWindowManager->winGetWindowFromId(
				NULL, NAMEKEY("OptionsMenu.wnd:LabelWaterStyle"));
			if (label) label->winHide(!supported);
			if (!supported) continue;
		}
		if (strcmp(def.iniKey, "CloudStyle") == 0)
		{
			// The display is initialized before the shell. Query when the menu
			// opens so a reused layout also reflects the current device.
			const Bool supported = HazardCloud::Supported();
			widget->winHide(!supported);
			GameWindow *label = TheWindowManager->winGetWindowFromId(
				NULL, NAMEKEY("OptionsMenu.wnd:LabelCloudStyle"));
			if (label) label->winHide(!supported);
			if (!supported) continue;
		}

		const Int value = clampOptionValue( def, def.get() );

		switch( def.kind )
		{
			case OPTION_BOOL:
				GadgetCheckBoxSetChecked( widget, value != 0 );
				break;

			case OPTION_ENUM:
			{
				GadgetComboBoxReset( widget );
				for( Int entry = def.lo; entry <= def.hi; ++entry )
				{
					AsciiString key;
					key.format( "%s%d", def.labelKey, entry );
					GadgetComboBoxAddEntry( widget, TheGameText->fetch( key ), entryColor );
				}
				GadgetComboBoxSetSelectedPos( widget, value - def.lo );
				break;
			}

			case OPTION_INT:
				GadgetSliderSetPosition( widget, value );
				break;
		}
	}

	// the choice for the next start, which is the file's: GlobalData holds what this run started with
	const Bool classicNextStart = getEarlyOptionBool( "ClassicGraphics", true );
	GadgetCheckBoxSetChecked( findOptionWidget( *findOptionDef( "ClassicGraphics" ) ), classicNextStart );

	// A Classic run holds these two forced in memory, and a Reforged next start reads them from the
	// file.  Shown as forced they read Custom beside the High the player just saved, and Accept saves that.
	if( TheGlobalData->m_classicGraphics && !classicNextStart )
	{
		GadgetComboBoxSetSelectedPos( findOptionWidget( *findOptionDef( "LightingStyle" ) ),
			getEarlyOptionInt( "LightingStyle", LIGHTING_STYLE_CLASSIC, LIGHTING_STYLE_CLASSIC, LIGHTING_STYLE_REFORGED ) );
		GadgetCheckBoxSetChecked( findOptionWidget( *findOptionDef( "VolumetricSmokeShadows" ) ), getEarlyOptionBool( "VolumetricSmokeShadows", false ) );
	}
}

//-------------------------------------------------------------------------------------------------
/** The controls -> GlobalData and Options.ini, for every catalog row whose control the player moved.
	*
	* A control shows the value GlobalData held as the menu opened, and that can be something
	* Options.ini does not say: the launcher's -language, a -lighting switch, or ClassicGraphics
	* forcing the light to Classic.  So only a row whose control now says something else is written;
	* every other key stays as the launcher or the last Accept left it on disk. */
//-------------------------------------------------------------------------------------------------
static void readCatalogWidgets( OptionPreferences &out )
{
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];
		GameWindow *widget = findOptionWidget( def );
		if( widget == NULL )
		{
			// no control for Defaults to have moved, and the file is all that remembers the row
			if( defaultsPressed && def.get() != optionDefaultValue( def ) )
			{
				def.set( optionDefaultValue( def ) );
				out[ AsciiString( def.iniKey ) ] = formatOptionValue( def, optionDefaultValue( def ) );
			}
			continue;
		}

		// A hidden, unfilled combo must not overwrite the player's opt-in.
		if (strcmp(def.iniKey, "CloudStyle") == 0 && !HazardCloud::Supported())
			continue;
		if (strcmp(def.iniKey, "WaterStyle") == 0 && !ModernWater::Supported())
			continue;

		Int value;
		switch( def.kind )
		{
			case OPTION_BOOL:
				value = GadgetCheckBoxIsChecked( widget ) ? 1 : 0;
				break;

			case OPTION_ENUM:
			{
				Int selected = -1;
				GadgetComboBoxGetSelectedPos( widget, &selected );
				// -1 is a combo box with nothing picked, which is not a value to write anywhere
				if( selected < 0 )
					continue;
				value = clampOptionValue( def, def.lo + selected );
				break;
			}

			default:
				value = clampOptionValue( def, GadgetSliderGetPosition( widget ) );
				break;
		}

		// Only the file moves.  This run keeps the archives, the tile and the shadow pass it started with,
		// and everything that asks GlobalData has to go on agreeing with them until the next start.
		if( strcmp( def.iniKey, "ClassicGraphics" ) == 0 )
		{
			if( ( value != 0 ) != getEarlyOptionBool( def.iniKey, true ) )
				out[ AsciiString( def.iniKey ) ] = formatOptionValue( def, value );
			continue;
		}

		if( value == def.get() )
			continue;

		// a layout read as the menus were built shows only once they are built again
		if( def.apply == APPLY_SHELL_REBUILD )
			pendingShellRebuild = TRUE;
		def.set( value );
		out[ AsciiString( def.iniKey ) ] = formatOptionValue( def, value );
	}

	// saved for the next start, and still forced in this Classic run (GlobalData::parseGameDataDefinition)
	if( TheGlobalData->m_classicGraphics )
	{
		TheWritableGlobalData->m_lightingStyle = LIGHTING_STYLE_CLASSIC;
		TheWritableGlobalData->m_volumetricSmokeShadows = FALSE;
	}
}

static const Color MENU_ENTRY_COLOR = GameMakeColor( 255, 255, 255, 255 );

//-------------------------------------------------------------------------------------------------
/** The monitor picked in the monitor list. */
//-------------------------------------------------------------------------------------------------
static MonitorEntry selectedMonitor( void )
{
	Int index = -1;
	GadgetComboBoxGetSelectedPos( comboBoxMonitor, &index );
	if( index >= 0 && index < menuMonitorCount )
		return menuMonitors[ index ];
	return findMonitor( TheGlobalData->m_monitor.str() );
}

//-------------------------------------------------------------------------------------------------
/** Fill the resolution list with the sizes this monitor offers, and pick the one asked for.  A
	* monitor that does not have it gets its own desktop size instead, which is the one size every
	* monitor is sure to show well. */
//-------------------------------------------------------------------------------------------------
static void fillResolutionList( const MonitorEntry &monitor, Int wantedWidth, Int wantedHeight )
{
	const Int desktopWidth = monitor.rect.right - monitor.rect.left;
	const Int desktopHeight = monitor.rect.bottom - monitor.rect.top;

	menuModeCount = listDisplayModes( monitor.device, menuModes, MAX_DISPLAY_MODE_ENTRIES );
	GadgetComboBoxReset( comboBoxResolution );

	Int wanted = -1;
	Int desktop = 0;
	for( Int index = 0; index < menuModeCount; ++index )
	{
		const DisplayModeEntry &mode = menuModes[ index ];
		UnicodeString text;
		text.format( u"%d x %d", mode.width, mode.height );
		GadgetComboBoxAddEntry( comboBoxResolution, text, MENU_ENTRY_COLOR );

		if( mode.width == wantedWidth && mode.height == wantedHeight )
			wanted = index;
		if( mode.width == desktopWidth && mode.height == desktopHeight )
			desktop = index;
	}
	GadgetComboBoxSetSelectedPos( comboBoxResolution, wanted >= 0 ? wanted : desktop );
}

//-------------------------------------------------------------------------------------------------
/** Every monitor on the desktop, by its number and the name it gives itself, with the one the game
	* is on picked. */
//-------------------------------------------------------------------------------------------------
static void fillMonitorList( void )
{
	const MonitorEntry current = findMonitor( TheGlobalData->m_monitor.str() );

	menuMonitorCount = listMonitors( menuMonitors, MAX_MONITOR_ENTRIES );
	GadgetComboBoxReset( comboBoxMonitor );

	Int selected = 0;
	for( Int index = 0; index < menuMonitorCount; ++index )
	{
		const MonitorEntry &monitor = menuMonitors[ index ];
		UnicodeString text;
		if( monitor.name[ 0 ] )
			text.format( u"%d: %hs", monitor.number, monitor.name );
		else
			text.format( u"%d", monitor.number );
		GadgetComboBoxAddEntry( comboBoxMonitor, text, MENU_ENTRY_COLOR );

		if( ::strcasecmp( monitor.device, current.device ) == 0 )
			selected = index;
	}
	GadgetComboBoxSetSelectedPos( comboBoxMonitor, selected );
}

//-------------------------------------------------------------------------------------------------
/** Borderless fullscreen takes the desktop's resolution and nothing else, so while it is the
	* selected mode the resolution list has no say and says so by going grey.  Fullscreen and
	* windowed both hand it back.  Read off the control rather than off GlobalData, so it follows the
	* dropdown while the screen is open instead of waiting for Accept. */
//-------------------------------------------------------------------------------------------------
static void updateResolutionEnabled( void )
{
	if( comboBoxResolution == NULL )
		return;

	const OptionDef *mode = findOptionDef( "WindowMode" );
	GameWindow *modeWidget = mode ? findOptionWidget( *mode ) : NULL;

	Int selected = -1;
	if( modeWidget )
		GadgetComboBoxGetSelectedPos( modeWidget, &selected );

	const Int value = ( selected >= 0 && mode ) ? mode->lo + selected : TheGlobalData->m_windowMode;
	comboBoxResolution->winEnable( value != WINDOW_MODE_BORDERLESS );
}

//-------------------------------------------------------------------------------------------------
/** The line under the resolution list: "the monitor supports this mode" when the picked size is one
	* of the modes Windows lists for the picked monitor, nothing when it is not.  Asked of the monitor
	* again rather than of the list, so the line stays honest whatever the list comes to hold. */
//-------------------------------------------------------------------------------------------------
static void updateResolutionNote( void )
{
	GameWindow *note = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:ResolutionNote" ) );
	if( note == NULL || comboBoxResolution == NULL )
		return;

	Int index = -1;
	GadgetComboBoxGetSelectedPos( comboBoxResolution, &index );
	Bool supported = FALSE;
	if( index >= 0 && index < menuModeCount )
	{
		DisplayModeEntry modes[ MAX_DISPLAY_MODE_ENTRIES ];
		const Int count = listDisplayModes( selectedMonitor().device, modes, MAX_DISPLAY_MODE_ENTRIES );
		for( Int i = 0; i < count && !supported; ++i )
			supported = ( modes[ i ].width == menuModes[ index ].width && modes[ i ].height == menuModes[ index ].height );
	}
	GadgetStaticTextSetText( note, supported ? TheGameText->fetch( "GUI:ResolutionNote0" ) : UnicodeString::TheEmptyString );
}

//-------------------------------------------------------------------------------------------------
/** Music, effects and voice: from their sliders into GlobalData, the audio manager and out. */
//-------------------------------------------------------------------------------------------------
static void saveVolumeOptions( OptionPreferences &out )
{
	Int val;

	//-------------------------------------------------------------------------------------------------
	// slider music volume
	val = GadgetSliderGetPosition(sliderMusicVolume);
	if(val != -1)
	{
	  TheWritableGlobalData->m_musicVolumeFactor = val;
    AsciiString prefString;
    prefString.format("%d", val);
    out["MusicVolume"] = prefString;
    TheAudio->setVolume(val / 100.0f, (AudioAffect) (AudioAffect_Music | AudioAffect_SystemSetting));
	}

	//-------------------------------------------------------------------------------------------------
	// slider SFX volume
	val = GadgetSliderGetPosition(sliderSFXVolume);
	if(val != -1)
	{
		//Both 2D and 3D sound effects are sharing the same slider. However, there is a
		//relative slider that gets applied to one of these values to lower that sound volume.
		Real sound2DVolume = val / 100.0f;
		Real sound3DVolume = val / 100.0f;
		Real relative2DVolume = TheAudio->getAudioSettings()->m_relative2DVolume;
		relative2DVolume = MIN( 1.0f, MAX( -1.0, relative2DVolume ) );
		if( relative2DVolume < 0.0f )
		{
			//Lower the 2D volume
			sound2DVolume *= 1.0f + relative2DVolume;
		}
		else
		{
			//Lower the 3D volume
			sound3DVolume *= 1.0f - relative2DVolume;
		}

		//Apply the sound volumes in the audio system now.
    TheAudio->setVolume( sound2DVolume, (AudioAffect) (AudioAffect_Sound | AudioAffect_SystemSetting) );
		TheAudio->setVolume( sound3DVolume, (AudioAffect) (AudioAffect_Sound3D | AudioAffect_SystemSetting) );

		//Save the settings in the options.ini.
    TheWritableGlobalData->m_SFXVolumeFactor = val;
    AsciiString prefString;
    prefString.format("%d", REAL_TO_INT( sound2DVolume * 100.0f ) );
    out["SFXVolume"] = prefString;
    prefString.format("%d", REAL_TO_INT( sound3DVolume * 100.0f ) );
		out["SFX3DVolume"] = prefString;
	}

	//-------------------------------------------------------------------------------------------------
	// slider Voice volume
	val = GadgetSliderGetPosition(sliderVoiceVolume);
	if(val != -1)
	{
    TheWritableGlobalData->m_voiceVolumeFactor = val;
    AsciiString prefString;
    prefString.format("%d", val);
    out["VoiceVolume"] = prefString;
    TheAudio->setVolume(val / 100.0f, (AudioAffect) (AudioAffect_Speech | AudioAffect_SystemSetting));
	}
}

//-------------------------------------------------------------------------------------------------
/** The audio output: the endpoint and the speaker setup.  Neither is a catalog row.  The speaker
	* setup is EA's SpeakerType string, which MilesAudioManager reads out of Options.ini itself and
	* hands the backend; the menu offers four of EA's six (GameAudio.cpp's TheSpeakerTypes, numbered as
	* MSS.h's AIL_3D_*), and a file holding Surround Sound or 4 Speaker shows the stereo entry until the
	* player picks one.  The endpoint is the backend's own id, "" for the default, and one that has gone
	* shows Default while the saved id stays for when it comes back. */
//-------------------------------------------------------------------------------------------------
static const UnsignedInt TheMenuSpeakerTypes[] = { 0 /* 2 Speakers */, 1 /* Headphones */, 4 /* 5.1 Surround */, 5 /* 7.1 Surround */ };
enum { MENU_SPEAKER_TYPE_COUNT = sizeof( TheMenuSpeakerTypes ) / sizeof( TheMenuSpeakerTypes[ 0 ] ) };
enum { MAX_AUDIO_DEVICE_ENTRIES = 32 };
static AsciiString menuAudioDevices[ MAX_AUDIO_DEVICE_ENTRIES ];	///< entry 0 is "", the default
static Int menuAudioDeviceCount = 0;
static Int speakerTypeShown = 0;	///< the entries the menu opened on, so Accept writes only a change
static Int audioDeviceShown = 0;

static GameWindow *audioOutputWidget( const char *name )
{
	return TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( name ) );
}

static void fillAudioOutputLists( void )
{
	GameWindow *speakers = audioOutputWidget( "OptionsMenu.wnd:ComboBoxSpeakerType" );
	const UnsignedInt speakerType = TheAudio->translateSpeakerTypeToUnsignedInt( pref->getSpeakerType() );
	GadgetComboBoxReset( speakers );
	speakerTypeShown = 0;
	for( Int entry = 0; entry < MENU_SPEAKER_TYPE_COUNT; ++entry )
	{
		AsciiString label;
		label.format( "GUI:SpeakerType%d", entry );
		GadgetComboBoxAddEntry( speakers, TheGameText->fetch( label ), MENU_ENTRY_COLOR );
		if( TheMenuSpeakerTypes[ entry ] == speakerType )
			speakerTypeShown = entry;
	}
	GadgetComboBoxSetSelectedPos( speakers, speakerTypeShown );

	GameWindow *devices = audioOutputWidget( "OptionsMenu.wnd:ComboBoxAudioDevice" );
	OptionPreferences::const_iterator saved = pref->find( "AudioDevice" );
	const AsciiString savedDevice = saved != pref->end() ? saved->second : AsciiString();
	GadgetComboBoxReset( devices );
	GadgetComboBoxAddEntry( devices, TheGameText->fetch( "GUI:AudioDeviceDefault" ), MENU_ENTRY_COLOR );
	menuAudioDevices[ 0 ].clear();
	menuAudioDeviceCount = 1;
	audioDeviceShown = 0;
	UnicodeString name;
	while( menuAudioDeviceCount < MAX_AUDIO_DEVICE_ENTRIES
		&& TheGameEngine->getAudioOutputDevice( menuAudioDeviceCount - 1, menuAudioDevices[ menuAudioDeviceCount ], name ) )
	{
		GadgetComboBoxAddEntry( devices, name, MENU_ENTRY_COLOR );
		if( menuAudioDevices[ menuAudioDeviceCount ] == savedDevice )
			audioDeviceShown = menuAudioDeviceCount;
		++menuAudioDeviceCount;
	}
	GadgetComboBoxSetSelectedPos( devices, audioDeviceShown );
}

static void saveAudioOutputOptions( OptionPreferences &out )
{
	Int index = -1;
	GadgetComboBoxGetSelectedPos( audioOutputWidget( "OptionsMenu.wnd:ComboBoxSpeakerType" ), &index );
	if( index >= 0 && index < MENU_SPEAKER_TYPE_COUNT && index != speakerTypeShown )
	{
		const AsciiString speakerType = TheAudio->translateUnsignedIntToSpeakerType( TheMenuSpeakerTypes[ index ] );
		out[ "SpeakerType" ] = speakerType;
		TheAudio->setPreferredSpeaker( speakerType );
		TheAudio->setSpeakerType( TheMenuSpeakerTypes[ index ] );
		speakerTypeShown = index;
	}

	index = -1;
	GadgetComboBoxGetSelectedPos( audioOutputWidget( "OptionsMenu.wnd:ComboBoxAudioDevice" ), &index );
	if( index >= 0 && index < menuAudioDeviceCount && index != audioDeviceShown )
	{
		out[ "AudioDevice" ] = menuAudioDevices[ index ];
		TheGameEngine->setAudioOutputDevice( menuAudioDevices[ index ] );
		audioDeviceShown = index;
	}
}

static void saveOptions( void )
{
	Int index;
	Int val;

	// which of the three the window is wearing right now, before the controls overwrite it
	const Int oldWindowMode = TheGlobalData->m_windowMode;
	const Bool oldVSync = TheGlobalData->m_vsync;
	const Int oldScaling = TheGlobalData->m_fullscreenScaling;
	const MonitorEntry oldMonitor = findMonitor( TheGlobalData->m_monitor.str() );

	//-------------------------------------------------------------------------------------------------
	// The catalog's controls, read back into GlobalData and onto Options.ini where the player changed
	// them.  The monitor goes in with them, because a borderless window takes its size from it.
	readCatalogWidgets( *pref );
	const MonitorEntry monitor = selectedMonitor();
	TheWritableGlobalData->m_monitor = monitor.device;

	//
	// A catalog row writes its own GlobalData field and nothing else, so the window mode is a raw
	// number until this runs: applyWindowMode is what turns it into the windowed flag the device
	// layer reads, and into the desktop resolution and edge scrolling that borderless means.  The
	// resolution block further down then hands all of it to the display in one change.
	//
	applyWindowMode();

	//-------------------------------------------------------------------------------------------------
//	// provider type
//	Bool isChecked = GadgetCheckBoxIsChecked(checkAudioHardware);
//	TheAudio->setHardwareAccelerated(isChecked);
//	(*pref)["3DAudioProvider"] = TheAudio->getProviderName(TheAudio->getSelectedProvider());
//
	//-------------------------------------------------------------------------------------------------
	// speaker setup and endpoint, in place of EA's surround check box
	saveAudioOutputOptions( *pref );

	//-------------------------------------------------------------------------------------------------
	// language filter
	if( GadgetCheckBoxIsChecked( checkLanguageFilter ) )
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, true);
			TheWritableGlobalData->m_languageFilterPref = true;
			(*pref)["LanguageFilter"] = "true";
	}
	else
	{
			//GadgetCheckBoxSetChecked( checkLanguageFilter, false);
			TheWritableGlobalData->m_languageFilterPref = false;
			(*pref)["LanguageFilter"] = "false";
	}
	
	//-------------------------------------------------------------------------------------------------
	// send Delay
	TheWritableGlobalData->m_firewallSendDelay = GadgetCheckBoxIsChecked(checkSendDelay);
	if (TheGlobalData->m_firewallSendDelay) {
		(*pref)["SendDelay"] = AsciiString("yes");
	} else {
		(*pref)["SendDelay"] = AsciiString("no");
	}

	//-------------------------------------------------------------------------------------------------
	// Cloud shadows are no preset's (GameLODManager::applyStaticLODLevel leaves them), and a preset
	// picked here still shows its step's value in the box, so the box is saved whatever the preset.
	TheWritableGlobalData->m_useCloudMap = GadgetCheckBoxIsChecked( checkCloudShadows );
	(*pref)["UseCloudMap"] = TheGlobalData->m_useCloudMap ? AsciiString("yes") : AsciiString("no");

	// no preset has an opinion on the behind-buildings marker, so neither does the detail box
	TheWritableGlobalData->m_enableBehindBuildingMarkers = GadgetCheckBoxIsChecked( checkBuildingOcclusion );
	(*pref)["BuildingOcclusion"] = TheWritableGlobalData->m_enableBehindBuildingMarkers ? AsciiString("yes") : AsciiString("no");

	//-------------------------------------------------------------------------------------------------
	// Custom game detail settings.
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if (index == CUSTOMDETAIL)
	{
 		//-------------------------------------------------------------------------------------------------
 		// Texture resolution slider
		{
				AsciiString prefString;

		 		val = GadgetSliderGetPosition(sliderTextureResolution);
				val = 2-val;

				prefString.format("%d",val);
				(*pref)["TextureReduction"] = prefString;

				if (TheGlobalData->m_textureReductionFactor != val)
				{
					TheGameClient->adjustLOD(val-TheGlobalData->m_textureReductionFactor);	//apply the new setting
				}
		}

		TheWritableGlobalData->m_useShadowVolumes = GadgetCheckBoxIsChecked( check3DShadows );
		(*pref)["UseShadowVolumes"] = TheWritableGlobalData->m_useShadowVolumes ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_useShadowDecals = GadgetCheckBoxIsChecked( check2DShadows );
		(*pref)["UseShadowDecals"] = TheWritableGlobalData->m_useShadowDecals ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_useLightMap = GadgetCheckBoxIsChecked( checkGroundLighting );
		(*pref)["UseLightMap"] = TheGlobalData->m_useLightMap ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_showSoftWaterEdge = GadgetCheckBoxIsChecked( checkSmoothWater );
		(*pref)["ShowSoftWaterEdge"] = TheGlobalData->m_showSoftWaterEdge ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_useDrawModuleLOD = !GadgetCheckBoxIsChecked( checkExtraAnimations );
		(*pref)["ExtraAnimations"] = TheGlobalData->m_useDrawModuleLOD ? AsciiString("no") : AsciiString("yes");

		// without the fork's box the trees sway with the extra animations, as retail decided it
		TheWritableGlobalData->m_useTreeSway = checkTreeSway ? GadgetCheckBoxIsChecked( checkTreeSway ) : !TheGlobalData->m_useDrawModuleLOD;
		(*pref)["TreeSway"] = TheGlobalData->m_useTreeSway ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_enableDynamicLOD = !GadgetCheckBoxIsChecked( checkNoDynamicLod );
		(*pref)["DynamicLOD"] = TheGlobalData->m_enableDynamicLOD ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_useHeatEffects = GadgetCheckBoxIsChecked( checkHeatEffects );
		(*pref)["HeatEffects"] = TheGlobalData->m_useHeatEffects ? AsciiString("yes") : AsciiString("no");

		// Never write this out
		//TheWritableGlobalData->m_useFpsLimit = !GadgetCheckBoxIsChecked( checkUnlockFps );
		//(*pref)["FPSLimit"] = TheGlobalData->m_useFpsLimit ? AsciiString("yes") : AsciiString("no");

		TheWritableGlobalData->m_useTrees = GadgetCheckBoxIsChecked( checkProps);
		(*pref)["ShowTrees"] = TheWritableGlobalData->m_useTrees ? AsciiString("yes") : AsciiString("no");

 		//-------------------------------------------------------------------------------------------------
		// Particle Cap slider
		{
				AsciiString prefString;

		 		val = GadgetSliderGetPosition(sliderParticleCap);

				prefString.format("%d",val);
				(*pref)["MaxParticleCount"] = prefString;

				TheWritableGlobalData->m_maxParticleCount = val;
		}
	}

	//-------------------------------------------------------------------------------------------------
	// LOD
	Bool levelChanged=FALSE;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	//The levels stored by the LOD Manager are inverted compared to GUI so find correct one:
	switch (index) {
	case ULTRADETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_ULTRA);
		break;
	case HIGHDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_HIGH);
		break;
	case MEDIUMDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_MEDIUM);
		break;
	case LOWDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_LOW);
		break;
	case CUSTOMDETAIL:
		levelChanged=TheGameLODManager->setStaticLODLevel(STATIC_GAME_LOD_CUSTOM);
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("LOD passed in was %d, %d is not a supported LOD",index,index));
		break;
	}

	if (levelChanged)
	        (*pref)["StaticGameLOD"] = TheGameLODManager->getStaticGameLODLevelName(TheGameLODManager->getStaticLODLevel());

	//-------------------------------------------------------------------------------------------------
	// Resolution and window mode.
	//
	// One device change rather than two: the mode decides whether the device is windowed, and
	// borderless decides the resolution as well - applyWindowMode above has already put the desktop
	// size in GlobalData, and the dropdown has no say over a window that covers the display.
	//
	GadgetComboBoxGetSelectedPos( comboBoxResolution, &index );
	Int xres, yres, bitDepth;

	oldDispSettings.xRes = TheDisplay->getWidth();
	oldDispSettings.yRes = TheDisplay->getHeight();
	oldDispSettings.bitDepth = TheDisplay->getBitDepth();
	oldDispSettings.windowed = TheDisplay->getWindowed();

	xres = TheGlobalData->m_xResolution;
	yres = TheGlobalData->m_yResolution;
	bitDepth = TheDisplay->getBitDepth();
	if( TheGlobalData->m_windowMode != WINDOW_MODE_BORDERLESS && index >= 0 && index < menuModeCount )
	{
		xres = menuModes[ index ].width;
		yres = menuModes[ index ].height;
	}

	//
	// Not the windowed flag: borderless and windowed are both windowed devices, and a switch between
	// those two is a change of frame with the same flag on both sides of it.
	//
	const Bool sizeChanged = ( oldDispSettings.xRes != xres || oldDispSettings.yRes != yres );
	const Bool modeChanged = ( oldWindowMode != TheGlobalData->m_windowMode );
	const Bool vsyncChanged = ( oldVSync != TheGlobalData->m_vsync );
	const Bool monitorChanged = ( ::strcasecmp( oldMonitor.device, monitor.device ) != 0 );
	const Bool scalingChanged = ( oldScaling != TheGlobalData->m_fullscreenScaling );

	if( sizeChanged || modeChanged || vsyncChanged || monitorChanged || scalingChanged )
	{
		if( !TheDisplay->setDisplayMode( xres, yres, bitDepth, TheGlobalData->m_windowed ) )
		{
			// the game is still where it was, so the window's next move must not go anywhere else
			TheWritableGlobalData->m_monitor = oldMonitor.device;
			applyWindowMode();
		}
		else
		{
			TheWritableGlobalData->m_xResolution = xres;
			TheWritableGlobalData->m_yResolution = yres;
			(*pref)["Monitor"] = monitor.device;

			TheHeaderTemplateManager->headerNotifyResolutionChange();
			TheMouse->mouseNotifyResolutionChange();

			AsciiString prefString;
			prefString.format("%d %d", xres, yres );
			(*pref)["Resolution"] = prefString;

			//
			// Every window on screen was laid out against the old size, so the shell is rebuilt from
			// the layouts.  A mode change that keeps the resolution - fullscreen to borderless on a
			// screen the game was already filling - changes no rectangle, so it does not pay for a
			// trip back to the main menu.
			//
			if( sizeChanged )
			{
				// only a resolution change gets the "keep these settings?" dialog; the frame round a
				// window is not something a player needs talking out of
				dispChanged = TRUE;

				//Save new settings for a dialog box confirmation after options are accepted
				newDispSettings.xRes = xres;
				newDispSettings.yRes = yres;
				newDispSettings.bitDepth = bitDepth;
				newDispSettings.windowed = TheDisplay->getWindowed();

				//
				// The shell and the command bar are rebuilt after this function returns, not here.
				// Every static GameWindow * in this file points into the options layout, the options
				// layout belongs to the shell, and the rest of saveOptions goes on reading those
				// pointers - the volume sliders and the gamma slider are all below this block.
				// Deleting the shell in the middle of it read four windows back out of freed pool
				// blocks.
				//
				pendingShellRebuild = TRUE;
			}
		}
	}

	//-------------------------------------------------------------------------------------------------
	// IP address
	UnsignedInt ip;
	GadgetComboBoxGetSelectedPos(comboBoxLANIP, &index);
	if (index>=0 && TheGlobalData)
	{
		ip = (UnsignedInt)(uintptr_t)GadgetComboBoxGetItemData(comboBoxLANIP, index);
		TheWritableGlobalData->m_defaultIP = ip;
		pref->setLANIPAddress(ip);
	}
	GadgetComboBoxGetSelectedPos(comboBoxOnlineIP, &index);
	if (index>=0)
	{
		ip = (UnsignedInt)(uintptr_t)GadgetComboBoxGetItemData(comboBoxOnlineIP, index);
		pref->setOnlineIPAddress(ip);
	}

	//-------------------------------------------------------------------------------------------------
	// HTTP Proxy
	GameWindow *textEntryHTTPProxy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("OptionsMenu.wnd:TextEntryHTTPProxy"));
	if (textEntryHTTPProxy)
	{
		UnicodeString uStr = GadgetTextEntryGetText(textEntryHTTPProxy);
		AsciiString aStr;
		aStr.translate(uStr);
		SetStringInRegistry("", "Proxy", aStr.str());
		ghttpSetProxy(aStr.str());
	}

	//-------------------------------------------------------------------------------------------------
	// Firewall Port Override
	GameWindow *textEntryFirewallPortOverride = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("OptionsMenu.wnd:TextEntryFirewallPortOverride"));
	if (textEntryFirewallPortOverride)
	{
		UnicodeString uStr = GadgetTextEntryGetText(textEntryFirewallPortOverride);
		AsciiString aStr;
		aStr.translate(uStr);
		Int override = atoi(aStr.str());
		if (override < 0 || override > 65535)
			override = 0;
		if (TheGlobalData->m_firewallPortOverride != override)
		{	TheWritableGlobalData->m_firewallPortOverride = override;
		    aStr.format("%d", override);
			(*pref)["FirewallPortOverride"] = aStr;
		}
	}

	//-------------------------------------------------------------------------------------------------
	// antialiasing
  GadgetComboBoxGetSelectedPos(comboBoxAntiAliasing, &index);
  if( index >= 0 && TheGlobalData->m_antiAliasBoxValue != index )
  {
    TheWritableGlobalData->m_antiAliasBoxValue = index;
    AsciiString prefString;
		prefString.format("%d", index);
		(*pref)["AntiAliasing"] = prefString;
  }


	//-------------------------------------------------------------------------------------------------
	// mouse mode
	TheWritableGlobalData->m_useAlternateMouse = GadgetCheckBoxIsChecked(checkAlternateMouse);
	(*pref)["UseAlternateMouse"] = TheWritableGlobalData->m_useAlternateMouse ? AsciiString("yes") : AsciiString("no");

	TheWritableGlobalData->m_clientRetaliationModeEnabled = GadgetCheckBoxIsChecked(checkRetaliation);
	(*pref)["Retaliation"] = TheWritableGlobalData->m_clientRetaliationModeEnabled? AsciiString("yes") : AsciiString("no");

	TheWritableGlobalData->m_doubleClickAttackMove = GadgetCheckBoxIsChecked( checkDoubleClickAttackMove );
	(*pref)["UseDoubleClickAttackMove"] = TheWritableGlobalData->m_doubleClickAttackMove ? AsciiString("yes") : AsciiString("no");

	//-------------------------------------------------------------------------------------------------
	// scroll speed val
	val = GadgetSliderGetPosition(sliderScrollSpeed);
	if(val != -1)
	{
		TheWritableGlobalData->m_keyboardScrollFactor = val/100.0f;
		DEBUG_LOG(("Scroll Spped val %d, keyboard scroll factor %f\n", val, TheGlobalData->m_keyboardScrollFactor));
		AsciiString prefString;
		prefString.format("%d", val);
		(*pref)["ScrollFactor"] = prefString;
	}
	
	saveVolumeOptions( *pref );

 	//-------------------------------------------------------------------------------------------------
 	// slider Gamma
 	val = GadgetSliderGetPosition(sliderGamma);
 	if(val != -1)
 	{
		Real gammaval=1.0f;
		//generate a value between 0.6 and 2.0.
		if (val < 50)
		{	//darker gamma
			if (val <= 0)
				gammaval = 0.6f;
			else
				gammaval=1.0f-(0.4f) * (Real)(50-val)/50.0f;
		}
		else
		if (val > 50)
			gammaval=1.0f+(1.0f) * (Real)(val-50)/50.0f;

 		AsciiString prefString;
 		prefString.format("%d", val);
 		(*pref)["Gamma"] = prefString;

		if (TheGlobalData->m_displayGamma != gammaval)
		{	TheWritableGlobalData->m_displayGamma = gammaval;
			TheDisplay->setGamma(TheGlobalData->m_displayGamma,0.0f, 1.0f, FALSE);
		}
 	}

}

static void DestroyOptionsLayout() {

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_CLOSED);

	showMainMenuColumns();
	TheShell->destroyOptionsLayout();
	OptionsLayout = NULL;
}

//
// Every window on screen was laid out against the old size, so the shell is rebuilt from the
// layouts.  This runs after the options layout is gone: it deletes the shell those windows belong
// to, so nothing that still holds one of them may run afterwards.  A mode change that keeps the
// resolution - fullscreen to borderless on a screen the game was already filling - changes no
// rectangle and never gets here.
//
static void applyPendingShellRebuild( void )
{
	if( !pendingShellRebuild )
		return;
	pendingShellRebuild = FALSE;

	delete TheShell;
	TheShell = NULL;

	TheShell = MSGNEW("GameClientSubsystem") Shell;
	TheShell->init();

	TheInGameUI->notifyResolutionChange();

	//
	// Out of a match the shell is what you are looking at, so it opens on the main menu.  In one,
	// the menus were rebuilt for the next time you want them and the thing on screen is the
	// battlefield: pushing the main menu over a running game is why the resolution list used to be
	// greyed out in-game at all.  The command bar has just been built from its layouts and comes up
	// hidden, so it is put back.
	//
	if( TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() )
	{
		//
		// A Shell is born active - the constructor says so, because a shell that is being built is
		// normally a shell somebody is about to look at.  In a match nobody is, and an active shell
		// is not a cosmetic detail: WindowXlat hands every mouse and key message to the window
		// system and then marks it used whenever the shell is active, so nothing reaches the
		// command translator, the selection translator or the camera.  The menus went on answering
		// clicks while production, unit orders, the camera keys and the whole keyboard did nothing.
		//
		TheShell->hideShell();

		ShowControlBar( TRUE );
	}
	else
		TheShell->push( AsciiString("Menus/MainMenu.wnd") );
}

//
// What the money box on the command bar actually says.  The bar is thrown away and built again by
// every one of these mode changes, and the new gadget carries the "GUI:$$$" its .wnd ships with
// until something writes a number over it - so this line reading dollar signs a full second after a
// rebuild is the readout that never got written.
//
static void resDrillLogMoney( const char *tag )
{
	GameWindow *moneyWindow = TheWindowManager->winGetWindowFromId(
		NULL, TheNameKeyGenerator->nameToKey( AsciiString( "ControlBar.wnd:MoneyDisplay" ) ) );

	if( moneyWindow == NULL )
	{
		DEBUG_LOG(("RESDRILL: %s, no money readout on the bar\n", tag));
		return;
	}

	AsciiString text;
	text.translate( GadgetStaticTextGetText( moneyWindow ) );
	DEBUG_LOG(("RESDRILL: %s, money readout reads \"%s\"\n", tag, text.str()));
}

//
// Every top-level window the manager is holding.  A rebuild that leaves the old copy on the list
// shows up here as the same name twice, which on screen is one panel painted over another.
//
static void resDrillLogWindowRoots( const char *tag )
{
	Int count = 0;

	for( GameWindow *window = TheWindowManager->winGetWindowList(); window;
			 window = window->winGetNext() )
	{
		ICoord2D position, size;
		window->winGetScreenPosition( &position.x, &position.y );
		window->winGetSize( &size.x, &size.y );

		DEBUG_LOG(("RESDRILL: %s, root \"%s\" (%d,%d %dx%d)%s\n",
			tag, window->winGetInstanceData()->m_decoratedNameString.str(),
			position.x, position.y, size.x, size.y,
			window->winIsHidden() ? " hidden" : ""));
		count++;
	}

	DEBUG_LOG(("RESDRILL: %s, %d top level windows\n", tag, count));
}

//
// -resdrill: the resolution change a player makes in the middle of a match, made from a script.
//
// It lives here rather than in the drill's own file because the sequence is the thing under test -
// reset the device, throw the shell away, build the command bar again while the match that owns it
// keeps running - and a copy of that sequence somewhere else would be a copy that can go right
// while the real one goes wrong.  Everything below the mode change is applyPendingShellRebuild,
// which is what the options menu itself reaches on its way out.
//
void ResolutionDrillApply( Int xres, Int yres )
{
	if( TheDisplay == NULL )
		return;

	const Int wasX = TheDisplay->getWidth();
	const Int wasY = TheDisplay->getHeight();
	const Int bitDepth = TheDisplay->getBitDepth();

	// The list the options menu is about to show, which is the monitor the game is on.
	DisplayModeEntry modes[ MAX_DISPLAY_MODE_ENTRIES ];
	const Int modeCount = listDisplayModes( findMonitor( TheGlobalData->m_monitor.str() ).device,
																					modes, MAX_DISPLAY_MODE_ENTRIES );

	// With no size asked for, take the first mode the monitor offers that is not the one on screen.
	// A drill that had to be told the monitor's modes would be a drill nobody runs on a new machine.
	if( xres <= 0 || yres <= 0 )
	{
		xres = yres = 0;
		for( Int i = 0; i < modeCount; i++ )
		{
			if( modes[ i ].width != wasX || modes[ i ].height != wasY )
			{
				xres = modes[ i ].width;
				yres = modes[ i ].height;
				break;
			}
		}
	}

	if( xres <= 0 || yres <= 0 || ( xres == wasX && yres == wasY ) )
	{
		DEBUG_LOG(("RESDRILL: no second mode to switch to, still %dx%d\n", wasX, wasY));
		return;
	}

	// which entry of the dropdown that is: the combo is filled straight off the same list, so the
	// entry index and the mode index are the same number
	Int modeIndex = -1;
	for( Int m = 0; m < modeCount; m++ )
	{
		if( modes[ m ].width == xres && modes[ m ].height == yres )
		{
			modeIndex = m;
			break;
		}
	}
	if( modeIndex < 0 )
	{
		DEBUG_LOG(("RESDRILL: %dx%d is not a mode this monitor offers\n", xres, yres));
		return;
	}

	DEBUG_LOG(("RESDRILL: %dx%d -> %dx%d (mode %d, bit depth %d)\n",
		wasX, wasY, xres, yres, modeIndex, bitDepth));

	//
	// From here it is the player's own route and nothing else: open the quit menu, open the options
	// menu the way that menu's Options button does, put the dropdown on the mode we want, and press
	// Accept.  Doing the device change directly instead - which this used to - skips the part that
	// broke, which is the shell being torn down and rebuilt from inside a shell window's own message
	// handler while the options layout is still on the stack.  QuitMenu.cpp's buttonOptions branch
	// is the three lines below.
	//
	// The quit menu is not decoration here.  It pauses the game and takes input off the player until
	// Return is pressed, and it is stretched to the screen it was built on, so a resolution change
	// under it puts Return somewhere other than where the button now is - off the edge entirely on a
	// smaller screen, which is a paused match with no way out.
	//
	if( !TheInGameUI->isQuitMenuVisible() )
		ToggleQuitMenu();

	WindowLayout *optLayout = TheShell->getOptionsLayout( TRUE );
	if( optLayout == NULL )
	{
		DEBUG_LOG(("RESDRILL: no options layout to open\n"));
		return;
	}
	optLayout->runInit();
	optLayout->hide( FALSE );
	optLayout->bringForward();

	if( comboBoxResolution == NULL )
	{
		DEBUG_LOG(("RESDRILL: the options menu came up without its resolution dropdown\n"));
		return;
	}
	//
	// Drop the list open first, by pressing the dropdown's own button.  Setting the selection
	// straight skips the one thing a player cannot skip: opening a combo box makes it the window
	// manager's lone window, and the lone window is the one pointer processDestroyList does not
	// clear when the window it names is destroyed.
	//
	GameWindow *dropDownButton = GadgetComboBoxGetDropDownButton( comboBoxResolution );
	if( dropDownButton != NULL )
		TheWindowManager->winSendSystemMsg( comboBoxResolution, GBM_SELECTED,
																				(WindowMsgData)dropDownButton,
																				dropDownButton->winGetWindowId() );

	GadgetComboBoxSetSelectedPos( comboBoxResolution, modeIndex );

	GameWindow *parent = TheWindowManager->winGetWindowFromId(
		NULL, TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:OptionsMenuParent" ) ) );
	NameKeyType acceptID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonAccept" ) );
	GameWindow *accept = TheWindowManager->winGetWindowFromId( parent, acceptID );
	if( parent == NULL || accept == NULL )
	{
		DEBUG_LOG(("RESDRILL: no Accept button on the options menu (parent %p, button %p)\n",
			parent, accept));
		return;
	}

	QuitMenuLogPlacement( "before the mode change" );
	resDrillLogMoney( "before the mode change" );
	resDrillLogWindowRoots( "before the mode change" );
	TheWindowManager->logInputOwners( "before the mode change" );

	DEBUG_LOG(("RESDRILL: pressing Accept\n"));
	TheWindowManager->winSendSystemMsg( parent, GBM_SELECTED, (WindowMsgData)accept, acceptID );

	DEBUG_LOG(("RESDRILL: shell and command bar rebuilt at %dx%d\n",
		TheDisplay->getWidth(), TheDisplay->getHeight()));

	QuitMenuLogPlacement( "after the mode change" );
}

//
// Accept leaves the "keep this resolution?" box on screen, and a player answers it - so the drill
// has to as well.  Cancel is the longer of the two answers: DeclineResolution changes the mode back
// and rebuilds the shell and the command bar a *second* time, from inside the message box's own
// GBM_SELECTED handler, over windows the first rebuild already replaced.  Answering it on a later
// frame rather than in the same breath as Accept is the point; the box a player sees has been on
// screen for a second or two by the time it is clicked.
//
void ResolutionDrillDismiss( Bool accept )
{
	extern GameWindow *resAcceptMenu;		// MainMenu.cpp owns it, DoResolutionDialog fills it in

	if( resAcceptMenu == NULL )
	{
		DEBUG_LOG(("RESDRILL: no resolution dialog to answer\n"));
		return;
	}

	resDrillLogMoney( "after the mode change" );
	resDrillLogWindowRoots( "after the mode change" );
	TheWindowManager->logInputOwners( "after the mode change" );

	NameKeyType buttonID = TheNameKeyGenerator->nameToKey(
		AsciiString( accept ? "MessageBox.wnd:ButtonOk" : "MessageBox.wnd:ButtonCancel" ) );
	GameWindow *button = TheWindowManager->winGetWindowFromId( resAcceptMenu, buttonID );
	if( button == NULL )
	{
		DEBUG_LOG(("RESDRILL: the resolution dialog has no %s button\n", accept ? "Ok" : "Cancel"));
		return;
	}

	DEBUG_LOG(("RESDRILL: answering the resolution dialog with %s\n", accept ? "Ok" : "Cancel"));
	TheWindowManager->winSendSystemMsg( resAcceptMenu, GBM_SELECTED, (WindowMsgData)button, buttonID );

	DEBUG_LOG(("RESDRILL: after the dialog the display is %dx%d\n",
		TheDisplay->getWidth(), TheDisplay->getHeight()));

	QuitMenuLogPlacement( "after the dialog" );
	TheWindowManager->logInputOwners( "after the dialog" );

	//
	// Press Return the way a player does, then say whether the game came back.  Input enabled 0 or
	// paused 1 on this line is the resolution change having stranded the menu that holds the pause.
	//
	if( TheInGameUI->isQuitMenuVisible() )
		ToggleQuitMenu();

	DEBUG_LOG(("RESDRILL: quit menu closed, input enabled %d, game paused %d\n",
		TheInGameUI->getInputEnabled() ? 1 : 0, TheGameLogic->isGamePaused() ? 1 : 0));

	resDrillLogWindowRoots( "after the quit menu closed" );
	TheWindowManager->logInputOwners( "after the quit menu closed" );
}

//-------------------------------------------------------------------------------------------------
// The Graphics page.
//
// These controls were EA's advanced display popup, which opened only when Custom was picked in the
// detail box, so choosing High never showed what High turns on.  They stand on the page now.
// Picking a preset shows its values in them without applying anything - Accept does that through
// setStaticLODLevel, as it always has - and touching any of them makes the detail box read Custom,
// which is the level saveOptions reads them back under.
//-------------------------------------------------------------------------------------------------
static GameWindow **const TheGraphicsDetailControls[] =
{
	&sliderTextureResolution, &sliderParticleCap,
	&check3DShadows, &check2DShadows, &checkCloudShadows, &checkGroundLighting, &checkSmoothWater,
	&checkProps, &checkExtraAnimations, &checkTreeSway, &checkHeatEffects, &checkNoDynamicLod,
};
enum { GRAPHICS_DETAIL_CONTROL_COUNT = sizeof( TheGraphicsDetailControls ) / sizeof( TheGraphicsDetailControls[ 0 ] ) };

static Bool isGraphicsDetailControl( const GameWindow *control )
{
	for( Int i = 0; i < GRAPHICS_DETAIL_CONTROL_COUNT; ++i )
		if( control != NULL && *TheGraphicsDetailControls[ i ] == control )
			return TRUE;

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
// The Performance / Quality slider.  TheGraphicsQualityTable (OptionsCatalog.cpp) says what every
// graphics control on the Graphics and Effects pages shows at each of its steps, and the four
// presets in the detail box are four of those steps.  Moving the slider fills in the controls and
// puts the detail box on the preset the step is, or on Custom between them; Accept then applies
// whatever the controls show, as it does for a preset or for boxes ticked by hand.
//-------------------------------------------------------------------------------------------------
static const char *const TheDetailCaptions[ DETAIL ] = { "GUI:Ultra", "GUI:High", "GUI:Medium", "GUI:Low", "GUI:Custom" };

/** The GameLOD level behind a preset entry of the detail box, which lists them Ultra first. */
static StaticGameLODLevel detailLevel( Int index )
{
	return (StaticGameLODLevel)( STATIC_GAME_LOD_ULTRA - index );
}

/** The detail box's entry for a step of the slider: the preset on that step, otherwise Custom. */
static Int detailIndexAtStep( Int step )
{
	for( Int index = ULTRADETAIL; index < CUSTOMDETAIL; ++index )
		if( TheGraphicsQualityAnchors[ detailLevel( index ) ] == step )
			return index;

	return CUSTOMDETAIL;
}

static GameWindow *graphicsQualitySlider( void )
{
	return findOptionWidget( *findOptionDef( "GraphicsQuality" ) );
}

static GameWindow *graphicsQualityWidget( const GraphicsQualityRow &row )
{
	AsciiString name;
	name.format( "OptionsMenu.wnd:%s", row.control );
	return TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( name ) );
}

static Bool isGraphicsQualityControl( const GameWindow *control )
{
	for( Int row = 0; row < TheGraphicsQualityTableCount; ++row )
		if( graphicsQualityWidget( TheGraphicsQualityTable[ row ] ) == control )
			return TRUE;

	return FALSE;
}

static GameWindow *classicGraphicsCheckBox( void )
{
	return findOptionWidget( *findOptionDef( "ClassicGraphics" ) );
}

/** The row's control when the quality slider sets it, otherwise NULL.  It leaves a control the player
	* cannot change - greyed in a match or under Direct3D 9, hidden where the device lacks it - and,
	* while Classic Graphics is ticked, every row the fork added: under it a preset is EA's detail
	* settings alone, so the box can read High with the Reforged boxes off, which is what a first run is. */
static GameWindow *graphicsQualityDrivenWidget( const GraphicsQualityRow &row )
{
	GameWindow *control = graphicsQualityWidget( row );
	if( control->winIsHidden() || !control->winGetEnabled() )
		return NULL;

	if( GadgetCheckBoxIsChecked( classicGraphicsCheckBox() ) && graphicsQualityRowIsReforged( row ) )
		return NULL;

	return control;
}

/** What a row's control shows, in the table's terms: a check box 0 or 1, a combo box its entry, a slider its position. */
static Int graphicsQualityShownValue( GameWindow *control )
{
	if( BitTest( control->winGetStyle(), GWS_CHECK_BOX ) )
		return GadgetCheckBoxIsChecked( control ) ? 1 : 0;

	if( BitTest( control->winGetStyle(), GWS_COMBO_BOX ) )
	{
		Int entry = -1;
		GadgetComboBoxGetSelectedPos( control, &entry );
		return entry;
	}

	return GadgetSliderGetPosition( control );
}

/** Whether every control the slider sets shows this step's value. */
static Bool graphicsQualityStepShown( Int step )
{
	for( Int row = 0; row < TheGraphicsQualityTableCount; ++row )
	{
		GameWindow *control = graphicsQualityDrivenWidget( TheGraphicsQualityTable[ row ] );
		if( control && graphicsQualityShownValue( control ) != TheGraphicsQualityTable[ row ].values[ step ] )
			return FALSE;
	}

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Every slider has a readout beside it.  The shipped screen had none, so a gamma slider, a scroll
// speed slider and three volume sliders gave no way to tell 40 from 60.
//-------------------------------------------------------------------------------------------------
enum SliderReadoutKind
{
	READOUT_NUMBER,			///< the slider's own position
	READOUT_PERCENT,		///< a 0..100 position with a percent sign
	READOUT_TEXTURE,		///< 0..2, low to high texture resolution
	READOUT_ANISOTROPY,	///< samples, where 0 is whatever the card offers
	READOUT_QUALITY,		///< the quality slider: the preset on its step, or how far along it is in percent
};

struct SliderReadout
{
	const char				*sliderName;
	const char				*readoutName;
	SliderReadoutKind	kind;
};

static const SliderReadout TheSliderReadouts[] =
{
	{ "OptionsMenu.wnd:SliderGamma",				"OptionsMenu.wnd:ValueGamma",							READOUT_NUMBER },
	{ "OptionsMenu.wnd:SliderGraphicsQuality",	"OptionsMenu.wnd:ValueGraphicsQuality",	READOUT_QUALITY },
	{ "OptionsMenu.wnd:LowResSlider",				"OptionsMenu.wnd:ValueTextureResolution",	READOUT_TEXTURE },
	{ "OptionsMenu.wnd:ParticleCapSlider",	"OptionsMenu.wnd:ValueParticleCap",				READOUT_NUMBER },
	{ "OptionsMenu.wnd:SliderAnisotropy",		"OptionsMenu.wnd:ValueAnisotropy",				READOUT_ANISOTROPY },
	{ "OptionsMenu.wnd:SliderMusicVolume",	"OptionsMenu.wnd:ValueMusicVolume",				READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderSFXVolume",		"OptionsMenu.wnd:ValueSFXVolume",					READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderVoiceVolume",	"OptionsMenu.wnd:ValueVoiceVolume",				READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderAmbientVolume",	"OptionsMenu.wnd:ValueAmbientVolume",		READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderMasterVolume",	"OptionsMenu.wnd:ValueMasterVolume",			READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderScrollSpeed",	"OptionsMenu.wnd:ValueScrollSpeed",				READOUT_NUMBER },
	{ "OptionsMenu.wnd:SliderZoomSpeed",		"OptionsMenu.wnd:ValueZoomSpeed",					READOUT_PERCENT },
	{ "OptionsMenu.wnd:SliderDragTolerance",	"OptionsMenu.wnd:ValueDragTolerance",			READOUT_NUMBER },
	{ "OptionsMenu.wnd:SliderMenuTransitionSpeed",	"OptionsMenu.wnd:ValueMenuTransitionSpeed",	READOUT_PERCENT },
};

static const char *const TheTextureResolutionCaptions[] = { "GUI:Low", "GUI:Medium", "GUI:High" };
enum { TEXTURE_RESOLUTION_STEPS = sizeof( TheTextureResolutionCaptions ) / sizeof( TheTextureResolutionCaptions[ 0 ] ) };

static void updateSliderReadouts( void )
{
	for( Int i = 0; i < (Int)( sizeof( TheSliderReadouts ) / sizeof( TheSliderReadouts[ 0 ] ) ); ++i )
	{
		const SliderReadout &readout = TheSliderReadouts[ i ];
		GameWindow *slider = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( readout.sliderName ) );
		GameWindow *value = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( readout.readoutName ) );
		// a stale Run/Window layout without the readouts still opens; it just says nothing
		if( slider == NULL || value == NULL )
			continue;

		const Int position = GadgetSliderGetPosition( slider );
		UnicodeString text;
		switch( readout.kind )
		{
			case READOUT_PERCENT:
				text.format( u"%d%%", position );
				break;

			case READOUT_TEXTURE:
				text = TheGameText->fetch( TheTextureResolutionCaptions[ max( 0, min( TEXTURE_RESOLUTION_STEPS - 1, position ) ) ] );
				break;

			case READOUT_ANISOTROPY:
				if( position == 0 )
					text = TheGameText->fetch( "GUI:AnisotropyCardMaximum" );
				else
					text.format( u"%dx", position );
				break;

			case READOUT_QUALITY:
			{
				// the box's word, not the step's: a box changed by hand leaves the slider on a preset's step
				Int detailIndex = CUSTOMDETAIL;
				GadgetComboBoxGetSelectedPos( comboBoxDetail, &detailIndex );
				if( detailIndex == CUSTOMDETAIL )
					text.format( u"%d%%", position * 100 / ( GRAPHICS_QUALITY_STEPS - 1 ) );
				else
					text = TheGameText->fetch( TheDetailCaptions[ detailIndex ] );
				break;
			}

			default:
				text.format( u"%d", position );
				break;
		}
		GadgetStaticTextSetText( value, text );
	}
}

static void updateDetailNote( void );

/** One quality table value in its control. */
static void setGraphicsQualityControl( GameWindow *control, Int value )
{
	if( BitTest( control->winGetStyle(), GWS_CHECK_BOX ) )
		GadgetCheckBoxSetChecked( control, value != 0 );
	else if( BitTest( control->winGetStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetSelectedPos( control, value );
	else
		GadgetSliderSetPosition( control, value );
}

/** Put one step of the quality slider in every control TheGraphicsQualityTable names, and the detail
	* box on the preset that step is, or on Custom between two of them.  A control the slider leaves
	* (graphicsQualityDrivenWidget) keeps what it shows, and so Accept leaves its setting alone. */
static void showGraphicsQualityStep( Int step )
{
	ignoreSelected = TRUE;
	for( Int row = 0; row < TheGraphicsQualityTableCount; ++row )
	{
		GameWindow *control = graphicsQualityDrivenWidget( TheGraphicsQualityTable[ row ] );
		if( control != NULL )
			setGraphicsQualityControl( control, TheGraphicsQualityTable[ row ].values[ step ] );
	}
	// trees are no step's: a preset gives them whenever the memory test passed, as applyStaticLODLevel does
	if( detailIndexAtStep( step ) != CUSTOMDETAIL && checkProps->winGetEnabled() )
		GadgetCheckBoxSetChecked( checkProps, TheGameLODManager->didMemPass() );
	GadgetComboBoxSetSelectedPos( comboBoxDetail, detailIndexAtStep( step ) );
	ignoreSelected = FALSE;

	updateSliderReadouts();
	updateDetailNote();
}

/** A preset picked in the detail box: the quality slider moves to its step and the controls show
	* that step.  Custom leaves both where they are. */
static void showDetailPreset( Int index )
{
	if( index == CUSTOMDETAIL )
		return;

	const Int step = TheGraphicsQualityAnchors[ detailLevel( index ) ];
	ignoreSelected = TRUE;
	GadgetSliderSetPosition( graphicsQualitySlider(), step );
	ignoreSelected = FALSE;
	showGraphicsQualityStep( step );
}

/** The Classic Graphics box clicked, outside a match.  Unticked, the page shows the High preset, which
	* is the Reforged picture in one click, and Accept saves it.  Ticked, the fork's rows go back to the
	* slider's first step, where every one of them is off, so no glow or filter stays behind under Classic. */
static void showClassicGraphicsChoice( void )
{
	if( !comboBoxDetail->winGetEnabled() )
		return;

	if( !GadgetCheckBoxIsChecked( classicGraphicsCheckBox() ) )
	{
		showDetailPreset( HIGHDETAIL );
		return;
	}

	ignoreSelected = TRUE;
	for( Int row = 0; row < TheGraphicsQualityTableCount; ++row )
	{
		GameWindow *control = graphicsQualityWidget( TheGraphicsQualityTable[ row ] );
		if( graphicsQualityRowIsReforged( TheGraphicsQualityTable[ row ] ) && !control->winIsHidden() && control->winGetEnabled() )
			setGraphicsQualityControl( control, TheGraphicsQualityTable[ row ].values[ 0 ] );
	}
	ignoreSelected = FALSE;
	updateSliderReadouts();
}

/** The row Defaults leaves: the words and how the window sits on this monitor are the player's, for
	* the reason setDefaults leaves the resolution.  The slider's row follows the preset. */
static Bool defaultsKeepsRow( const OptionDef& def )
{
	static const char *const kept[] = { "TextLanguage", "WindowMode", "FullscreenScaling", "VSync", "GraphicsQuality" };
	for( Int i = 0; i < ARRAY_SIZE( kept ); ++i )
		if( strcmp( def.iniKey, kept[ i ] ) == 0 )
			return TRUE;

	return FALSE;
}

/** Defaults is the Classic game, as a first run has it: the box ticked and every catalog row on its
	* default, the preset the machine was rated for.  Accept saves what moved, so a Reforged player is one
	* untick away from High again.  A control hidden or greyed here (-d3d9, a match) keeps what it shows. */
static void showClassicDefaults( void )
{
	if( comboBoxDetail->winGetEnabled() )
	{
		GadgetCheckBoxSetChecked( classicGraphicsCheckBox(), TRUE );
		showDetailPreset( STATIC_GAME_LOD_ULTRA - TheGameLODManager->findStaticLODLevel() );
	}

	ignoreSelected = TRUE;
	for( Int i = 0; i < TheOptionCatalogCount; ++i )
	{
		const OptionDef& def = TheOptionCatalog[ i ];
		GameWindow *control = findOptionWidget( def );
		if( control != NULL && !defaultsKeepsRow( def ) && !control->winIsHidden() && control->winGetEnabled() )
			setGraphicsQualityControl( control, optionDefaultValue( def ) );
	}
	GadgetCheckBoxSetChecked( checkCloudShadows, optionDefaultValue( *findOptionDef( "UseCloudMap" ) ) != 0 );
	ignoreSelected = FALSE;

	defaultsPressed = TRUE;
	updateSliderReadouts();
}

/** The grey line under the detail box: what the picked preset turns on, and that picking one only
	* fills in the boxes until Accept.  GUI:DetailNote0..4 follow the box's own order, Ultra to Custom. */
static void updateDetailNote( void )
{
	GameWindow *note = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY( "OptionsMenu.wnd:DetailNote" ) );
	// a stale Run/Window layout without the line still opens; it just says nothing
	if( note == NULL )
		return;

	Int index = CUSTOMDETAIL;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if( index < ULTRADETAIL || index > CUSTOMDETAIL )
		index = CUSTOMDETAIL;

	AsciiString key;
	key.format( "GUI:DetailNote%d", index );
	GadgetStaticTextSetText( note, TheGameText->fetch( key ) );
}

/** A detail control was touched, so what the page shows is nobody's preset any more.  In a match the
	* box is greyed and keeps its preset: what can still change there (glow, the live effects) is the
	* catalog's to save, and Custom would be saved over the preset for it. */
static void markDetailCustom( void )
{
	Int index = CUSTOMDETAIL;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
	if( index == CUSTOMDETAIL || !comboBoxDetail->winGetEnabled() )
		return;

	ignoreSelected = TRUE;
	GadgetComboBoxSetSelectedPos( comboBoxDetail, CUSTOMDETAIL );
	ignoreSelected = FALSE;
	updateSliderReadouts();
	updateDetailNote();
}
//-------------------------------------------------------------------------------------------------
/** Initialize the options menu */
//-------------------------------------------------------------------------------------------------
void OptionsMenuInit( WindowLayout *layout, void *userData )
{
	ignoreSelected = TRUE;
	defaultsPressed = FALSE;
	if (TheGameEngine->getQuitting())
		return;
	OptionsLayout = layout;
	if (!pref)
	{
		pref = NEW OptionPreferences;
	}

	SignalUIInteraction(SHELL_SCRIPT_HOOK_OPTIONS_OPENED);

	comboBoxLANIPID				 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxIP" ) );
	comboBoxLANIP					 = TheWindowManager->winGetWindowFromId( NULL,  comboBoxLANIPID);
	comboBoxOnlineIPID		 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxOnlineIP" ) );
	comboBoxOnlineIP			 = TheWindowManager->winGetWindowFromId( NULL,  comboBoxOnlineIPID);
	checkAlternateMouseID  = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAlternateMouse" ) );
	checkAlternateMouse	   = TheWindowManager->winGetWindowFromId( NULL, checkAlternateMouseID);
	checkRetaliationID		 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Retaliation" ) );
	checkRetaliation	     = TheWindowManager->winGetWindowFromId( NULL, checkRetaliationID);
	checkDoubleClickAttackMoveID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckDoubleClickAttackMove" ) );
	checkDoubleClickAttackMove   = TheWindowManager->winGetWindowFromId( NULL, checkDoubleClickAttackMoveID );
	sliderScrollSpeedID	   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderScrollSpeed" ) );
	sliderScrollSpeed		   = TheWindowManager->winGetWindowFromId( NULL,  sliderScrollSpeedID);
	comboBoxAntiAliasingID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxAntiAliasing" ) );
	comboBoxAntiAliasing   = TheWindowManager->winGetWindowFromId( NULL, comboBoxAntiAliasingID );
	comboBoxResolutionID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxResolution" ) );
	comboBoxResolution     = TheWindowManager->winGetWindowFromId( NULL, comboBoxResolutionID );
	comboBoxMonitorID      = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxMonitor" ) );
	comboBoxMonitor        = TheWindowManager->winGetWindowFromId( NULL, comboBoxMonitorID );
	comboBoxDetailID			 = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ComboBoxDetail" ) );
	comboBoxDetail		   = TheWindowManager->winGetWindowFromId( NULL, comboBoxDetailID );

	checkLanguageFilterID  = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckLanguageFilter" ) );
	checkLanguageFilter    = TheWindowManager->winGetWindowFromId( NULL, checkLanguageFilterID );
	checkSendDelayID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckSendDelay" ) );
	checkSendDelay				 = TheWindowManager->winGetWindowFromId( NULL, checkSendDelayID);
	buttonFirewallRefreshID	= TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ButtonFirewallRefresh" ) );
	buttonFirewallRefresh		= TheWindowManager->winGetWindowFromId( NULL, buttonFirewallRefreshID);
	checkDrawAnchorID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxDrawAnchor" ) );
	checkDrawAnchor				 = TheWindowManager->winGetWindowFromId( NULL, checkDrawAnchorID);
	checkMoveAnchorID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxMoveAnchor" ) );
	checkMoveAnchor				 = TheWindowManager->winGetWindowFromId( NULL, checkMoveAnchorID);

	// Replay camera
	checkSaveCameraID      = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxSaveCamera" ) );
	checkSaveCamera        = TheWindowManager->winGetWindowFromId( NULL, checkSaveCameraID );
	checkUseCameraID       = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBoxUseCamera" ) );
	checkUseCamera         = TheWindowManager->winGetWindowFromId( NULL, checkUseCameraID );

//	// Speakers and 3-D Audio
//	checkAudioSurroundID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAudioSurround" ) );
//	checkAudioSurround     = TheWindowManager->winGetWindowFromId( NULL, checkAudioSurroundID );
//	checkAudioHardwareID   = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckAudioHardware" ) );
//	checkAudioHardware     = TheWindowManager->winGetWindowFromId( NULL, checkAudioHardwareID );
//
	// Volume Controls
	sliderMusicVolumeID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderMusicVolume" ) );
	sliderMusicVolume      = TheWindowManager->winGetWindowFromId( NULL, sliderMusicVolumeID );
	sliderSFXVolumeID      = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderSFXVolume" ) );
	sliderSFXVolume        = TheWindowManager->winGetWindowFromId( NULL, sliderSFXVolumeID );
	sliderVoiceVolumeID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderVoiceVolume" ) );
	sliderVoiceVolume      = TheWindowManager->winGetWindowFromId( NULL, sliderVoiceVolumeID );
 	sliderGammaID    = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:SliderGamma" ) );
 	sliderGamma      = TheWindowManager->winGetWindowFromId( NULL, sliderGammaID );

//	checkBoxLowTextureDetailID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckLowTextureDetail" ) );
//	checkBoxLowTextureDetail      = TheWindowManager->winGetWindowFromId( NULL, checkBoxLowTextureDetailID );
	
	sliderTextureResolutionID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:LowResSlider" ) );
	sliderTextureResolution = TheWindowManager->winGetWindowFromId( NULL, sliderTextureResolutionID );

	check3DShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Check3DShadows" ) );
	check3DShadows   = TheWindowManager->winGetWindowFromId( NULL, check3DShadowsID);

	check2DShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:Check2DShadows" ) );
	check2DShadows   = TheWindowManager->winGetWindowFromId( NULL, check2DShadowsID);

	checkCloudShadowsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckCloudShadows" ) );
	checkCloudShadows   = TheWindowManager->winGetWindowFromId( NULL, checkCloudShadowsID);

	checkGroundLightingID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckGroundLighting" ) );
	checkGroundLighting   = TheWindowManager->winGetWindowFromId( NULL, checkGroundLightingID);

	checkSmoothWaterID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckSmoothWater" ) );
	checkSmoothWater   = TheWindowManager->winGetWindowFromId( NULL, checkSmoothWaterID);

	checkExtraAnimationsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckExtraAnimations" ) );
	checkExtraAnimations   = TheWindowManager->winGetWindowFromId( NULL, checkExtraAnimationsID);

	checkNoDynamicLodID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckNoDynamicLOD" ) );
	checkNoDynamicLod   = TheWindowManager->winGetWindowFromId( NULL, checkNoDynamicLodID);

	checkHeatEffectsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckHeatEffects" ) );
	checkHeatEffects   = TheWindowManager->winGetWindowFromId( NULL, checkHeatEffectsID);

	checkTreeSwayID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckTreeSway" ) );
	checkTreeSway   = TheWindowManager->winGetWindowFromId( NULL, checkTreeSwayID);

	checkUnlockFpsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckUnlockFPS" ) );
	checkUnlockFps   = TheWindowManager->winGetWindowFromId( NULL, checkUnlockFpsID);

	checkBuildingOcclusionID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckBehindBuilding" ) );
	checkBuildingOcclusion   = TheWindowManager->winGetWindowFromId( NULL, checkBuildingOcclusionID);

	checkPropsID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:CheckShowProps" ) );
	checkProps   = TheWindowManager->winGetWindowFromId( NULL, checkPropsID);

	sliderParticleCapID = TheNameKeyGenerator->nameToKey( AsciiString( "OptionsMenu.wnd:ParticleCapSlider" ) );
  sliderParticleCap = TheWindowManager->winGetWindowFromId( NULL, sliderParticleCapID );

	Color color =  GameMakeColor(255,255,255,255);

  enum AliasingMode
  {
    OFF = 0,
    LOW,
    HIGH,
    NUM_ALIASING_MODES
  };

	NameKeyType versionID = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:LabelVersion") );
	GameWindow *labelVersion = TheWindowManager->winGetWindowFromId( NULL, versionID );
	// The fork's release number rather than EA's registry "Version 1.04".
	UnicodeString versionString( u"Reforged " );
	UnicodeString releaseVersion;
	releaseVersion.translate( TheVersion->getReleaseVersion() );
	versionString.concat( releaseVersion );
	if (TheGlobalData->m_modName.isNotEmpty())
	{
		// the label is one line under the menu; a long -mod list keeps its start, the replay keeps all of it
		const Int MAX_MOD_CHARS = 48;
		UnicodeString mods = TheVersion->getUnicodeModName();
		if (mods.getLength() > MAX_MOD_CHARS)
		{
			UnicodeString cut;
			for (Int i = 0; i < MAX_MOD_CHARS - 3; ++i)
				cut.concat( mods.getCharAt( i ) );
			cut.concat( u"..." );
			mods = cut;
		}
		versionString.concat( u" [" );
		versionString.concat( mods );
		versionString.concat( u"]" );
	}

	if (TheVersion->showFullVersion())
	{
		if (TheVersion)
		{
			UnicodeString version;
			version.format(u"(%s) %s -- %s", versionString.str(), TheVersion->getFullUnicodeVersion().str(), TheVersion->getUnicodeBuildTime().str());
			GadgetStaticTextSetText( labelVersion, version );
		}
		else
		{
			labelVersion->winHide( TRUE );
		}
	}
	else
	{
		GadgetStaticTextSetText( labelVersion, versionString );
	}


	// Choose an IP address, then initialize the IP combo box
	UnsignedInt selectedIP = pref->getLANIPAddress();

	UnicodeString str;
	IPEnumeration IPs;
	EnumeratedIP *IPlist = IPs.getAddresses();
	Int index;
	Int selectedIndex = -1;
	Int count = 0;
	GadgetComboBoxReset(comboBoxLANIP);
	while (IPlist)
	{
		count++;
		str.translate(IPlist->getIPstring());
		index = GadgetComboBoxAddEntry(comboBoxLANIP, str, color);
		GadgetComboBoxSetItemData(comboBoxLANIP, index, (void *)(uintptr_t)(IPlist->getIP()));
		if (selectedIP == IPlist->getIP())
		{
			selectedIndex = index;
		}
		IPlist = IPlist->getNext();
	}
	if (selectedIndex >= 0)
	{
		GadgetComboBoxSetSelectedPos(comboBoxLANIP, selectedIndex);
	}
	else
	{
		GadgetComboBoxSetSelectedPos(comboBoxLANIP, 0);
		if (IPs.getAddresses())
		{
			pref->setLANIPAddress(IPs.getAddresses()->getIPstring());
		}
	}

	// And now the GameSpy one
	if (comboBoxOnlineIP)
	{
		UnsignedInt selectedIP = pref->getOnlineIPAddress();
		UnicodeString str;
		IPEnumeration IPs;
		EnumeratedIP *IPlist = IPs.getAddresses();
		Int index;
		Int selectedIndex = -1;
		Int count = 0;
		GadgetComboBoxReset(comboBoxOnlineIP);
		while (IPlist)
		{
			count++;
			str.translate(IPlist->getIPstring());
			index = GadgetComboBoxAddEntry(comboBoxOnlineIP, str, color);
			GadgetComboBoxSetItemData(comboBoxOnlineIP, index, (void *)(uintptr_t)(IPlist->getIP()));
			if (selectedIP == IPlist->getIP())
			{
				selectedIndex = index;
			}
			IPlist = IPlist->getNext();
		}
		if (selectedIndex >= 0)
		{
			GadgetComboBoxSetSelectedPos(comboBoxOnlineIP, selectedIndex);
		}
		else
		{
			GadgetComboBoxSetSelectedPos(comboBoxOnlineIP, 0);
			if (IPs.getAddresses())
			{
				pref->setOnlineIPAddress(IPs.getAddresses()->getIPstring());
			}
		}
	}

	// HTTP Proxy
	GameWindow *textEntryHTTPProxy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("OptionsMenu.wnd:TextEntryHTTPProxy"));
	if (textEntryHTTPProxy)
	{
		UnicodeString uStr;
		std::string proxy;
		GetStringFromRegistry("", "Proxy", proxy);
		uStr.translate(proxy.c_str());
		GadgetTextEntrySetText(textEntryHTTPProxy, uStr);
	}

	// Firewall Port Override
	GameWindow *textEntryFirewallPortOverride = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("OptionsMenu.wnd:TextEntryFirewallPortOverride"));
	if (textEntryFirewallPortOverride)
	{
			UnicodeString uStr;
			if (TheGlobalData->m_firewallPortOverride != 0)
			{	AsciiString aStr;
				aStr.format("%d",TheGlobalData->m_firewallPortOverride);
				uStr.translate(aStr);
			}
			GadgetTextEntrySetText(textEntryFirewallPortOverride,uStr);
	}

	// populate anti aliasing modes
	AsciiString selectedAliasingMode = (*pref)["AntiAliasing"];
	GadgetComboBoxReset(comboBoxAntiAliasing);
	AsciiString temp;
	for (Int i=0; i < NUM_ALIASING_MODES; ++i)
	{
		temp.format("GUI:AntiAliasing%d", i);
		str = TheGameText->fetch( temp );
		index = GadgetComboBoxAddEntry(comboBoxAntiAliasing, str, color);
	}
	Int val = atoi(selectedAliasingMode.str());
	if( val < 0 || val > NUM_ALIASING_MODES )
	{
		TheWritableGlobalData->m_antiAliasBoxValue = val = 0;
	}
	GadgetComboBoxSetSelectedPos(comboBoxAntiAliasing, val);

	// the monitor the game is on, and the size it is showing there
	fillMonitorList();
	fillResolutionList( selectedMonitor(), TheDisplay->getWidth(), TheDisplay->getHeight() );
	fillAudioOutputLists();

	// set the display detail
	GadgetComboBoxReset(comboBoxDetail);
	for (Int entry = ULTRADETAIL; entry < DETAIL; ++entry)
		GadgetComboBoxAddEntry(comboBoxDetail, TheGameText->fetch(TheDetailCaptions[entry]), color);

	//Check if level was never set and default to setting most suitable for system.
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_UNKNOWN)
		TheGameLODManager->setStaticLODLevel(TheGameLODManager->findStaticLODLevel());

	switch (TheGameLODManager->getStaticLODLevel())
	{
	case STATIC_GAME_LOD_LOW:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, LOWDETAIL);
		break;
	case STATIC_GAME_LOD_MEDIUM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, MEDIUMDETAIL);
		break;
	case STATIC_GAME_LOD_HIGH:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, HIGHDETAIL);
		break;
	case STATIC_GAME_LOD_ULTRA:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, ULTRADETAIL);
		break;
	case STATIC_GAME_LOD_CUSTOM:
		GadgetComboBoxSetSelectedPos(comboBoxDetail, CUSTOMDETAIL);
		break;
	default:
		DEBUG_ASSERTCRASH(FALSE,("Tried to set comboBoxDetail to a value of %d ", TheGameLODManager->getStaticLODLevel()) );
	};

	Int txtFact=TheGameLODManager->getCurrentTextureReduction();

	GadgetSliderSetPosition( sliderTextureResolution, 2-txtFact);

	GadgetCheckBoxSetChecked( check3DShadows, TheGlobalData->m_useShadowVolumes);

	GadgetCheckBoxSetChecked( check2DShadows, TheGlobalData->m_useShadowDecals);

	GadgetCheckBoxSetChecked( checkCloudShadows, TheGlobalData->m_useCloudMap);

	GadgetCheckBoxSetChecked( checkGroundLighting, TheGlobalData->m_useLightMap);

	GadgetCheckBoxSetChecked( checkSmoothWater, TheGlobalData->m_showSoftWaterEdge);

	GadgetCheckBoxSetChecked( checkExtraAnimations, !TheGlobalData->m_useDrawModuleLOD);

	if( checkTreeSway )
		GadgetCheckBoxSetChecked( checkTreeSway, TheGlobalData->m_useTreeSway);

	GadgetCheckBoxSetChecked( checkNoDynamicLod, !TheGlobalData->m_enableDynamicLOD);

	GadgetCheckBoxSetChecked( checkHeatEffects, TheGlobalData->m_useHeatEffects);

	GadgetCheckBoxSetChecked( checkUnlockFps, !TheGlobalData->m_useFpsLimit);

	GadgetCheckBoxSetChecked( checkBuildingOcclusion, TheGlobalData->m_enableBehindBuildingMarkers);

	GadgetCheckBoxSetChecked( checkProps, TheGlobalData->m_useTrees);
	//checkProps->winEnable(false);	//gray out the option for now.

	GadgetSliderSetPosition( sliderParticleCap, TheGlobalData->m_maxParticleCount );

	//set language filter
	AsciiString languageFilter = (*pref)["LanguageFilter"];
	if (languageFilter == "true" )
	{
		GadgetCheckBoxSetChecked( checkLanguageFilter, true);
		TheWritableGlobalData->m_languageFilterPref = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkLanguageFilter, false);
		TheWritableGlobalData->m_languageFilterPref = false;
	}
	
	//set replay camera
	if (pref->saveCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, true);
		TheWritableGlobalData->m_saveCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkSaveCamera, false);
		TheWritableGlobalData->m_saveCameraInReplay = false;
	}
	if (pref->useCameraInReplays())
	{
		GadgetCheckBoxSetChecked( checkUseCamera, true);
		TheWritableGlobalData->m_useCameraInReplay = true;
	}
	else
	{
		GadgetCheckBoxSetChecked( checkUseCamera, false);
		TheWritableGlobalData->m_useCameraInReplay = false;
	}

	//set scroll options
	AsciiString test = (*pref)["DrawScrollAnchor"];
	DEBUG_LOG(("DrawScrollAnchor == [%s]\n", test.str()));
	if (test == "Yes" || (test.isEmpty() && TheInGameUI->getDrawRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, true);
		TheInGameUI->setDrawRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkDrawAnchor, false);
		TheInGameUI->setDrawRMBScrollAnchor(false);
	}
	test = (*pref)["MoveScrollAnchor"];
	DEBUG_LOG(("MoveScrollAnchor == [%s]\n", test.str()));
	if (test == "Yes" || (test.isEmpty() && TheInGameUI->getMoveRMBScrollAnchor()))
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, true);
		TheInGameUI->setMoveRMBScrollAnchor(true);
	}
	else
	{
		GadgetCheckBoxSetChecked( checkMoveAnchor, false);
		TheInGameUI->setMoveRMBScrollAnchor(false);
	}

//	// Audio Init shiznat
//	GadgetCheckBoxSetChecked(checkAudioHardware, TheAudio->getHardwareAccelerated());
//	GadgetCheckBoxSetChecked(checkAudioSurround, TheAudio->getSpeakerSurround());

	// set the mouse mode
	GadgetCheckBoxSetChecked(checkAlternateMouse, TheGlobalData->m_useAlternateMouse);
	GadgetCheckBoxSetChecked(checkRetaliation, TheGlobalData->m_clientRetaliationModeEnabled);
	GadgetCheckBoxSetChecked( checkDoubleClickAttackMove, TheGlobalData->m_doubleClickAttackMove );

	// set scroll speed slider
	Int scrollPos = (Int)(TheGlobalData->m_keyboardScrollFactor*100.0f);
	GadgetSliderSetPosition( sliderScrollSpeed, scrollPos );
	DEBUG_LOG(("Scroll SPeed %d\n", scrollPos));

	// set the send delay check box
	GadgetCheckBoxSetChecked(checkSendDelay, TheGlobalData->m_firewallSendDelay);

 	// set volume sliders

	// set music volume slider
	GadgetSliderSetPosition( sliderMusicVolume, REAL_TO_INT(pref->getMusicVolume()) );

	//set SFX volume slider
	Real maxVolume = MAX( pref->getSoundVolume(), pref->get3DSoundVolume() );
	GadgetSliderSetPosition( sliderSFXVolume, REAL_TO_INT( maxVolume ) );

	//set voice volume slider
	GadgetSliderSetPosition( sliderVoiceVolume, REAL_TO_INT(pref->getSpeechVolume()) );
	
	// set the gamma slider
 	GadgetSliderSetPosition( sliderGamma, REAL_TO_INT(pref->getGammaValue()) );

	//-------------------------------------------------------------------------------------------------
	// the pages, their buttons, and everything the catalog puts on them
	for( Int page = 0; page < OPTIONS_PAGE_COUNT; ++page )
	{
		optionsPage[ page ] = TheWindowManager->winGetWindowFromId(
			NULL, TheNameKeyGenerator->nameToKey( AsciiString( TheOptionsPageNames[ page ] ) ) );
		optionsTabID[ page ] = TheNameKeyGenerator->nameToKey( AsciiString( TheOptionsTabNames[ page ] ) );
		optionsTab[ page ] = TheWindowManager->winGetWindowFromId( NULL, optionsTabID[ page ] );
	}

	fillCatalogWidgets();
	OptionsHotkeysInit();
	showOptionsPage( 0 );

	// show menu
	layout->hide( FALSE );

	// the main menu's buttons go while this is up; the backdrop's draw keeps them gone
	// (a layout kept from an earlier open already draws through it)
	GameWindow *backdrop = layout->getFirstWindow();
	if( backdrop->winGetDrawFunc() != drawOptionsPanel )
	{
		optionsPanelDraw = backdrop->winGetDrawFunc();
		backdrop->winSetDrawFunc( drawOptionsPanel );
	}
	hideMainMenuColumns();

	// set keyboard focus to main parent
	AsciiString parentName( "OptionsMenu.wnd:OptionsMenuParent" );
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( parentName );
	GameWindow *parent = TheWindowManager->winGetWindowFromId( NULL, parentID );
	TheWindowManager->winSetFocus( parent );
	
	if( (TheGameLogic->isInGame() && TheGameLogic->getGameMode() != GAME_SHELL) || TheGameSpyInfo )
	{
		// disable controls that you can't change the options for in game
		comboBoxLANIP->winEnable(FALSE);
		if (comboBoxOnlineIP)
			comboBoxOnlineIP->winEnable(FALSE);
		checkSendDelay->winEnable(FALSE);
		buttonFirewallRefresh->winEnable(FALSE);

		if (comboBoxDetail)
			comboBoxDetail->winEnable(FALSE);
		graphicsQualitySlider()->winEnable(FALSE);

		// and what the detail box decides with it: a preset is picked outside a match, where the
		// shadows and textures it changes can be rebuilt
		for( Int control = 0; control < GRAPHICS_DETAIL_CONTROL_COUNT; ++control )
			if( *TheGraphicsDetailControls[ control ] )
				(*TheGraphicsDetailControls[ control ])->winEnable( FALSE );

		//
		// The resolution list used to be greyed out here too.  It is not any more: changing it in a
		// match rebuilds the menus and the command bar and leaves you looking at the battlefield -
		// see the resolution block in saveOptions.
		//

//		if (checkAudioSurround)
//			checkAudioSurround->winEnable(FALSE);
//
//		if (checkAudioHardware)
//			checkAudioHardware->winEnable(FALSE);
	}

	// three looks only the Direct3D 11 picture draws; under -d3d9 their boxes keep the saved value, greyed
	if( !TheGlobalData->m_direct3D11 )
	{
		static const char *const direct3D11Only[] = { "MapLight", "TeamGlow", "WaterFoam", "GroundMarks", "Firelight", "HemiAmbient", "AmbientOcclusion", "InternalHdr", "Weather", "WaterSplashes", "Atmosphere", "StructureCollapse" };
		for( Int i = 0; i < (Int)( sizeof( direct3D11Only ) / sizeof( direct3D11Only[ 0 ] ) ); ++i )
			findOptionWidget( *findOptionDef( direct3D11Only[ i ] ) )->winEnable( FALSE );
	}

	// A preset is its own step on the quality slider, whatever step Options.ini kept from a Custom mix,
	// as long as the boxes show that step.  An Options.ini from before the slider can hold a preset
	// with boxes ticked by hand beside it, and then the page is the Custom mix it really is.  Run after
	// the greying above, which decides what the slider sets; in a match the box keeps its preset.
	Int detailIndex = CUSTOMDETAIL;
	GadgetComboBoxGetSelectedPos( comboBoxDetail, &detailIndex );
	if( detailIndex != CUSTOMDETAIL )
	{
		const Int step = TheGraphicsQualityAnchors[ detailLevel( detailIndex ) ];
		if( !comboBoxDetail->winGetEnabled() || graphicsQualityStepShown( step ) )
			GadgetSliderSetPosition( graphicsQualitySlider(), step );
		else
			GadgetComboBoxSetSelectedPos( comboBoxDetail, CUSTOMDETAIL );
	}
	updateSliderReadouts();
	updateDetailNote();


	// borderless owns the resolution; the list is grey while it is picked
	updateResolutionEnabled();
	updateResolutionNote();

	TheWindowManager->winSetModal(parent);
	ignoreSelected = FALSE;
}  // end OptionsMenuInit

//-------------------------------------------------------------------------------------------------
/** options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuShutdown( WindowLayout *layout, void *userData )
{
/* moved into the back button stuff
	if (pref)
	{
		pref->write();
		delete pref;
		pref = NULL;
	}

	comboBoxIP = NULL;

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
*/

}  // end OptionsMenuShutdown

//-------------------------------------------------------------------------------------------------
/** options menu update method */
//-------------------------------------------------------------------------------------------------
void OptionsMenuUpdate( WindowLayout *layout, void *userData )
{

}  // end OptionsMenuUpdate

//-------------------------------------------------------------------------------------------------
/** Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			// the Hotkeys page waiting for a key takes every key, Esc included
			if( OptionsHotkeysKey( key, (Int)mData2 ) )
				return MSG_HANDLED;

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
						AsciiString buttonName( "OptionsMenu.wnd:ButtonBack" );
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( buttonName );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)button, buttonID );

					}  // end if

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}  // end escape

			}  // end switch( key )

		}  // end char

	}  // end switch( msg )

	return MSG_IGNORED;

}  // end OptionsMenuInput

//-------------------------------------------------------------------------------------------------
/** options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType OptionsMenuSystem( GameWindow *window, UnsignedInt msg, 
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	static NameKeyType buttonBack = NAMEKEY_INVALID;
	static NameKeyType buttonDefaults = NAMEKEY_INVALID;
	static NameKeyType buttonAccept = NAMEKEY_INVALID;
	static NameKeyType buttonReplayMenu = NAMEKEY_INVALID;

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{

			// get ids for our children controls
			buttonBack = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonBack") );
			buttonDefaults = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonDefaults") );
			buttonAccept = TheNameKeyGenerator->nameToKey( AsciiString("OptionsMenu.wnd:ButtonAccept") );

			break;

		}  // end create

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{

			break;

		}  // end case

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}  // end input

		//---------------------------------------------------------------------------------------------
		case GCM_SELECTED:
			{
				if(ignoreSelected)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( OptionsHotkeysSystem( msg, control, mData2 ) )
					break;

				if (controlID == comboBoxDetailID)
				{
					Int index = CUSTOMDETAIL;
					GadgetComboBoxGetSelectedPos( comboBoxDetail, &index );
					showDetailPreset( index );
					updateDetailNote();
				}
				else if( controlID == comboBoxMonitorID )
				{
					// another monitor offers other sizes; keep the picked one where it has it
					Int index = -1;
					GadgetComboBoxGetSelectedPos( comboBoxResolution, &index );
					const Bool picked = ( index >= 0 && index < menuModeCount );
					fillResolutionList( selectedMonitor(),
															picked ? menuModes[ index ].width : TheDisplay->getWidth(),
															picked ? menuModes[ index ].height : TheDisplay->getHeight() );
				}
				else if( isGraphicsQualityControl( control ) )
				{
					markDetailCustom();
				}

				// picking borderless greys the resolution list out, the other two hand it back
				updateResolutionEnabled();
				updateResolutionNote();
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		case GLM_DOUBLE_CLICKED:
		{
			if( ignoreSelected || !OptionsHotkeysSystem( msg, (GameWindow *)mData1, mData2 ) )
				return MSG_IGNORED;
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GSM_SLIDER_TRACK:
		case GSM_SLIDER_DONE:
		{
			if( ignoreSelected )
				break;

			updateSliderReadouts();
			// the quality slider sets the controls rather than being one of them, so it never makes Custom itself
			if( (GameWindow *)mData1 == graphicsQualitySlider() )
				showGraphicsQualityStep( GadgetSliderGetPosition( (GameWindow *)mData1 ) );
			else if( isGraphicsDetailControl( (GameWindow *)mData1 ) || isGraphicsQualityControl( (GameWindow *)mData1 ) )
				markDetailCustom();
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			if(ignoreSelected)
				break;
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			for( Int page = 0; page < OPTIONS_PAGE_COUNT; ++page )
				if( controlID == optionsTabID[ page ] )
				{
					showOptionsPage( page );
					return MSG_HANDLED;
				}
			if( OptionsHotkeysSystem( msg, control, mData2 ) )
				return MSG_HANDLED;

			if( controlID == buttonBack )
			{
				OptionsHotkeysClose();
				// go back one screen
				//TheShell->pop();
				if (pref)
				{
					delete pref;
					pref = NULL;
				}

				comboBoxLANIP = NULL;
				comboBoxOnlineIP = NULL;

				if(GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS))
					GameSpyCloseOverlay(GSOVERLAY_OPTIONS);
				else
				{
					DestroyOptionsLayout();
				}

			}  // end if
			else if (controlID == buttonAccept )
			{
				saveOptions();
				// Hotkeys.ini, and every key read again from it
				OptionsHotkeysSave();

				// the command bar rebuilds its grid letters on its next update
				if( TheControlBar )
					TheControlBar->markUIDirty();

				if (pref)
				{
					pref->write();
					delete pref;
					pref = NULL;
				}

				comboBoxLANIP = NULL;
				comboBoxOnlineIP = NULL;
				
				if(!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
					destroyQuitMenu(); // if we're in a game, the change res then enter the same kind of game, we nee the quit menu to be gone.


				if(GameSpyIsOverlayOpen(GSOVERLAY_OPTIONS))
				{
					GameSpyCloseOverlay(GSOVERLAY_OPTIONS);
					applyPendingShellRebuild();
				}
				else
				{
					DestroyOptionsLayout();
					applyPendingShellRebuild();
					if (dispChanged)
					{
						DoResolutionDialog();
					}
				}

			}
			else if (controlID == buttonDefaults )
			{
				setDefaults();
				showClassicDefaults();
			}
			else if( control == classicGraphicsCheckBox() )
			{
				showClassicGraphicsChoice();
			}
			else if( isGraphicsDetailControl( control ) || isGraphicsQualityControl( control ) )
			{
				markDetailCustom();
			}
			else if(controlID == checkDrawAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setDrawRMBScrollAnchor(true);
          	(*pref)["DrawScrollAnchor"] = "Yes";
        }
				else
        {
          	TheInGameUI->setDrawRMBScrollAnchor(false);
          	(*pref)["DrawScrollAnchor"] = "No";
        }
      }
			else if(controlID == checkMoveAnchorID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheInGameUI->setMoveRMBScrollAnchor(true);
          	(*pref)["MoveScrollAnchor"] = "Yes";
        }
				else
        {
          	TheInGameUI->setMoveRMBScrollAnchor(false);
          	(*pref)["MoveScrollAnchor"] = "No";
        }
      }
			else if(controlID == checkSaveCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_saveCameraInReplay = true;
          	(*pref)["SaveCameraInReplays"] = "yes";
        }
				else
        {
          	TheWritableGlobalData->m_saveCameraInReplay = false;
          	(*pref)["SaveCameraInReplays"] = "no";
        }
      }
			else if(controlID == checkUseCameraID )
      {
        if( GadgetCheckBoxIsChecked( control ) )
        {
          	TheWritableGlobalData->m_useCameraInReplay = true;
          	(*pref)["UseCameraInReplays"] = "yes";
        }
				else
        {
          	TheWritableGlobalData->m_useCameraInReplay = false;
          	(*pref)["UseCameraInReplays"] = "no";
        }
      }
			else if (controlID == buttonFirewallRefreshID)
			{
				// setting the behavior to unknown will force the firewall helper to detect the firewall behavior
				// the next time we log into gamespy/WOL/whatever.
				char num[16];
				num[0] = 0;
				TheWritableGlobalData->m_firewallBehavior = FirewallHelperClass::FIREWALL_TYPE_UNKNOWN;
				snprintf(num, sizeof(num), "%d", (Int)TheGlobalData->m_firewallBehavior);
				AsciiString numstr;
				numstr = num;
				(*pref)["FirewallBehavior"] = numstr;
			}
			break;

		}  // end selected

		default:
			return MSG_IGNORED;

	}  // end switch

	return MSG_HANDLED;

}  // end OptionsMenuSystem
