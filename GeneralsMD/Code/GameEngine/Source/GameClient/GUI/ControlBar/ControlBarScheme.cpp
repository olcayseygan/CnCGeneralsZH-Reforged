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

// FILE: ControlBarScheme.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Apr 2002
//
//	Filename: 	ControlBarScheme.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Contains all the Command bar goodness in terms of how it looks
//						For instrucitons on how to use, please see it's .h file
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Lib/Clock.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"

#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameClient/ControlBarScheme.h"
#include "GameClient/Display.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameFont.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Image.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/UiAnimClock.h"
#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
enum{
	COMMAND_BAR_SIZE_OFFSET = 0
};

const FieldParse ControlBarSchemeManager::m_controlBarSchemeFieldParseTable[] = 
{

	{ "ImagePart",						ControlBarSchemeManager::parseImagePart,			NULL, NULL },
	{ "AnimatingPart",				ControlBarSchemeManager::parseAnimatingPart,	NULL, NULL },
	{ "ScreenCreationRes",		INI::parseICoord2D,						NULL, offsetof( ControlBarScheme, m_ScreenCreationRes ) },
	{ "Side",									INI::parseAsciiString,				NULL, offsetof( ControlBarScheme, m_side ) },
	{ "QueueButtonImage",			INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_buttonQueueImage ) },
	{ "RightHUDImage",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_rightHUDImage ) },
	{ "BuildUpClockColor",		INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_buildUpClockColor ) },
	{ "ButtonBorderBuildColor",			INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_borderBuildColor ) },
	{ "CommandBarBorderColor",			INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_commandBarBorderColor ) },
	{ "ButtonBorderActionColor",		INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_borderActionColor ) },
	{ "ButtonBorderUpgradeColor",		INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_borderUpgradeColor ) },
	{ "ButtonBorderSystemColor",		INI::parseColorInt,						NULL, offsetof( ControlBarScheme, m_borderSystemColor ) },
	{ "OptionsButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_optionsButtonEnable ) },
	{ "OptionsButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_optionsButtonHightlited ) },
	{ "OptionsButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_optionsButtonPushed ) },
	{ "OptionsButtonDisabled",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_optionsButtonDisabled ) },
	{ "IdleWorkerButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_idleWorkerButtonEnable ) },
	{ "IdleWorkerButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_idleWorkerButtonHightlited ) },
	{ "IdleWorkerButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_idleWorkerButtonPushed ) },
	{ "IdleWorkerButtonDisabled",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_idleWorkerButtonDisabled ) },
	{ "BuddyButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_buddyButtonEnable ) },
	{ "BuddyButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_buddyButtonHightlited ) },
	{ "BuddyButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_buddyButtonPushed ) },
	{ "BuddyButtonDisabled",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_buddyButtonDisabled) },
	{ "BeaconButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_beaconButtonEnable ) },
	{ "BeaconButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_beaconButtonHightlited ) },
	{ "BeaconButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_beaconButtonPushed ) },
	{ "BeaconButtonDisabled",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_beaconButtonDisabled ) },
	{ "GenBarButtonIn",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_genBarButtonIn ) },
	{ "GenBarButtonOn",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_genBarButtonOn ) },
	{ "ToggleButtonUpIn",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonUpIn ) },
	{ "ToggleButtonUpOn",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonUpOn ) },
	{ "ToggleButtonUpPushed",		INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonUpPushed ) },
	{ "ToggleButtonDownIn",			INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonDownIn ) },
	{ "ToggleButtonDownOn", 		INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonDownOn ) },
	{ "ToggleButtonDownPushed",	INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_toggleButtonDownPushed ) },

	{ "GeneralButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_generalButtonEnable ) },
	{ "GeneralButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_generalButtonHightlited ) },
	{ "GeneralButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_generalButtonPushed ) },
	{ "GeneralButtonDisabled",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_generalButtonDisabled ) },

	{ "UAttackButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_uAttackButtonEnable ) },
	{ "UAttackButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_uAttackButtonHightlited ) },
	{ "UAttackButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_uAttackButtonPushed ) },

	{ "GenArrow",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_genArrow) },

	{ "MinMaxButtonEnable",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_minMaxButtonEnable ) },
	{ "MinMaxButtonHightlited",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_minMaxButtonHightlited ) },
	{ "MinMaxButtonPushed",				INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_minMaxButtonPushed ) },

	{ "MinMaxUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_minMaxUL ) },
	{ "MinMaxLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_minMaxLR ) },

	{ "GeneralUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_generalUL ) },
	{ "GeneralLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_generalLR ) },

	{ "UAttackUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_uAttackUL ) },
	{ "UAttackLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_uAttackLR ) },

	{ "OptionsUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_optionsUL ) },
	{ "OptionsLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_optionsLR ) },

	{ "WorkerUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_workerUL ) },
	{ "WorkerLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_workerLR ) },

	{ "ChatUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_chatUL ) },
	{ "ChatLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_chatLR ) },

	{ "BeaconUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_beaconUL ) },
	{ "BeaconLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_beaconLR ) },

	{ "PowerBarUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_powerBarUL ) },
	{ "PowerBarLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_powerBarLR ) },

	{ "MoneyUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_moneyUL ) },
	{ "MoneyLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_moneyLR ) },
	{ "MoneyFontSize",	INI::parseInt,					NULL, offsetof( ControlBarScheme, m_moneyFontSize ) },
	{ "MoneyThousands",	INI::parseBool,					NULL, offsetof( ControlBarScheme, m_moneyThousands ) },
	{ "MoneySignPainted",	INI::parseBool,				NULL, offsetof( ControlBarScheme, m_moneySignPainted ) },
	{ "RadarUL", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_radarUL ) },
	{ "RadarLR", 		INI::parseICoord2D,				NULL, offsetof( ControlBarScheme, m_radarLR ) },

	{ "BlockRadarUL",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_RADAR ] ) },
	{ "BlockRadarLR",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_RADAR ] ) },
	{ "BlockProductionUL",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_PRODUCTION ] ) },
	{ "BlockProductionLR",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_PRODUCTION ] ) },
	{ "BlockMoneyUL",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_MONEY ] ) },
	{ "BlockMoneyLR",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_MONEY ] ) },
	{ "BlockUpgradesUL",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_UPGRADES ] ) },
	{ "BlockUpgradesLR",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_UPGRADES ] ) },
	{ "BlockGenPowerUL",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_GEN_POWER ] ) },
	{ "BlockGenPowerLR",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_GEN_POWER ] ) },
	{ "BlockObserverInfoUL",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockUL[ ControlBarScheme::BLOCK_OBSERVER_INFO ] ) },
	{ "BlockObserverInfoLR",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_blockLR[ ControlBarScheme::BLOCK_OBSERVER_INFO ] ) },

	{ "IdleInfantryButtonImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_idleButtonImage[ ControlBarScheme::IDLE_INFANTRY ] ) },
	{ "IdleInfantryUL",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleUL[ ControlBarScheme::IDLE_INFANTRY ] ) },
	{ "IdleInfantryLR",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleLR[ ControlBarScheme::IDLE_INFANTRY ] ) },
	{ "IdleVehicleButtonImage",		INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_idleButtonImage[ ControlBarScheme::IDLE_VEHICLE ] ) },
	{ "IdleVehicleUL",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleUL[ ControlBarScheme::IDLE_VEHICLE ] ) },
	{ "IdleVehicleLR",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleLR[ ControlBarScheme::IDLE_VEHICLE ] ) },
	{ "IdleAircraftButtonImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_idleButtonImage[ ControlBarScheme::IDLE_AIRCRAFT ] ) },
	{ "IdleAircraftUL",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleUL[ ControlBarScheme::IDLE_AIRCRAFT ] ) },
	{ "IdleAircraftLR",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleLR[ ControlBarScheme::IDLE_AIRCRAFT ] ) },
	{ "SelectArmyButtonImage",		INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_idleButtonImage[ ControlBarScheme::IDLE_ARMY ] ) },
	{ "SelectArmyUL",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleUL[ ControlBarScheme::IDLE_ARMY ] ) },
	{ "SelectArmyLR",				INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_idleLR[ ControlBarScheme::IDLE_ARMY ] ) },
	{ "RadarFrameImage",			INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_radarFrameName ) },
	{ "PowerTrayImage",				INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_powerTrayName ) },
	{ "CommandTabBuildingsButtonImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_commandTabImage[ 0 ] ) },
	{ "CommandTabGroundButtonImage",		INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_commandTabImage[ 1 ] ) },
	{ "CommandTabAircraftButtonImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_commandTabImage[ 2 ] ) },
	{ "CommandTabBuildingsUL",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabUL[ 0 ] ) },
	{ "CommandTabBuildingsLR",	INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabLR[ 0 ] ) },
	{ "CommandTabGroundUL",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabUL[ 1 ] ) },
	{ "CommandTabGroundLR",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabLR[ 1 ] ) },
	{ "CommandTabAircraftUL",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabUL[ 2 ] ) },
	{ "CommandTabAircraftLR",		INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabLR[ 2 ] ) },
	{ "CommandTabPlateUL",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabPlateUL ) },
	{ "CommandTabPlateLR",			INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_commandTabPlateLR ) },
	{ "CommandTabPageNextImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_commandTabPageImage[ 0 ] ) },
	{ "CommandTabPagePrevImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_commandTabPageImage[ 1 ] ) },
	{ "SignalAttackButtonImage",		INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_signalImage[ 0 ] ) },
	{ "SignalDefendButtonImage",		INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_signalImage[ 1 ] ) },
	{ "SignalAttentionButtonImage",	INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_signalImage[ 2 ] ) },
	{ "SignalAttackUL",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalUL[ 0 ] ) },
	{ "SignalAttackLR",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalLR[ 0 ] ) },
	{ "SignalDefendUL",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalUL[ 1 ] ) },
	{ "SignalDefendLR",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalLR[ 1 ] ) },
	{ "SignalAttentionUL",					INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalUL[ 2 ] ) },
	{ "SignalAttentionLR",					INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalLR[ 2 ] ) },
	{ "SignalPlateUL",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalPlateUL ) },
	{ "SignalPlateLR",							INI::parseICoord2D,	NULL, offsetof( ControlBarScheme, m_signalPlateLR ) },
	{ "SignalPlateImage",						INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_signalPlateImageName ) },
	{ "GrownRadarImage",						INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_grownRadarName ) },
	{ "GrownRadarRight",						INI::parseInt,					NULL, offsetof( ControlBarScheme, m_grownRadarRight ) },
	{ "GrownRadarPeriod",						INI::parseInt,					NULL, offsetof( ControlBarScheme, m_grownRadarPeriod ) },
	{ "GrownRadarLedge",						INI::parseInt,					NULL, offsetof( ControlBarScheme, m_grownRadarLedge ) },
	{ "TooltipImage",					INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_tooltipName ) },
	{ "TalentImage",					INI::parseAsciiString,	NULL, offsetof( ControlBarScheme, m_talentName ) },

	{ "CommandMarkerImage",		INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_commandMarkerImage) },
	{ "ExpBarForegroundImage",		INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_expBarForeground) },
	{ "PowerPurchaseImage",			INI::parseMappedImage,				NULL, offsetof( ControlBarScheme, m_powerPurchaseImage) },
	{ "PowerPurchaseCloseButtonEnable",			INI::parseMappedImage,	NULL, offsetof( ControlBarScheme, m_powerPurchaseCloseButtonEnable ) },
	{ "PowerPurchaseCloseButtonHightlited",	INI::parseMappedImage,	NULL, offsetof( ControlBarScheme, m_powerPurchaseCloseButtonHightlited ) },
	{ "PowerPurchaseCloseButtonPushed",			INI::parseMappedImage,	NULL, offsetof( ControlBarScheme, m_powerPurchaseCloseButtonPushed ) },
	{ "PowerPurchaseCloseButtonDisabled",		INI::parseMappedImage,	NULL, offsetof( ControlBarScheme, m_powerPurchaseCloseButtonDisabled ) },

	{ NULL,										NULL,													NULL, 0 }  // keep this last

};

// used to parse the anim types that each animation part of a scheme can have
static const LookupListRec AnimTypeNames[] = 
{
	{ "SLIDE_RIGHT", ControlBarSchemeAnimation::CB_ANIM_SLIDE_RIGHT },
	{ NULL, 0	}
};

static void animSlideRight( ControlBarSchemeAnimation *anim );

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
ControlBarSchemeImage::ControlBarSchemeImage( void )
{
	m_name.clear();
	m_position.x = m_position.y = 0;
	m_size.x = m_size.y = 0;
	m_image = NULL;
	m_layer = 0;
}

ControlBarSchemeImage::~ControlBarSchemeImage( void )
{
	m_image = NULL;
}

ControlBarSchemeAnimation::ControlBarSchemeAnimation( void )
{
	// Added by Sadullah Nader
	// Initializations missing and needed
	
	m_animDuration = 0;
	m_finalPos.x = m_finalPos.y = 0;

	// End Add
	m_name.clear();
	m_animType = 0;
	m_animImage = NULL;
	m_startPos.x = m_startPos.y = 0;
	m_currentFrame = 0;
}

ControlBarSchemeAnimation::~ControlBarSchemeAnimation( void )
{
	m_animImage = NULL;
}


void ControlBarScheme::reset(void)
{
	for (Int i = 0; i < MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS; i++)
	{
		ControlBarSchemeImageList::iterator it = m_layer[i].begin();

		while (it != m_layer[i].end())
		{
			ControlBarSchemeImage *im = *it;
			if( im )
				delete im;
			im = NULL;
			it ++;	
		}
		m_layer[i].clear();
	
	}


	ControlBarSchemeAnimationList::iterator it = m_animations.begin();

	while (it != m_animations.end())
	{
		ControlBarSchemeAnimation *anim = *it;
		if( anim )
		{
			anim->m_animImage = NULL;
			delete(anim);
		}
		anim = NULL;
		it ++;	
	}
	m_animations.clear();

	m_name.clear();
	m_ScreenCreationRes.x = m_ScreenCreationRes.y = 0;
	m_side.clear();
	m_buttonQueueImage = NULL;
	m_rightHUDImage = NULL;
		m_optionsButtonEnable = NULL;
	m_optionsButtonHightlited = NULL;
	m_optionsButtonPushed = NULL;
	m_optionsButtonDisabled = NULL;

	m_idleWorkerButtonEnable = NULL;
	m_idleWorkerButtonHightlited = NULL;
	m_idleWorkerButtonPushed = NULL;
	m_idleWorkerButtonDisabled = NULL;

	m_buddyButtonEnable = NULL;
	m_buddyButtonHightlited = NULL;
	m_buddyButtonPushed = NULL;
	m_buddyButtonDisabled = NULL;

	m_beaconButtonEnable = NULL;
	m_beaconButtonHightlited = NULL;
	m_beaconButtonPushed = NULL;
	m_beaconButtonDisabled = NULL;

	m_genBarButtonIn = NULL;
	m_genBarButtonOn = NULL;

	m_toggleButtonUpIn = NULL;
	m_toggleButtonUpOn = NULL;
	m_toggleButtonUpPushed = NULL;
	m_toggleButtonDownIn = NULL;
	m_toggleButtonDownOn = NULL;
	m_toggleButtonDownPushed = NULL;

	m_commandMarkerImage = NULL;
	m_expBarForeground = NULL;
	m_powerPurchaseImage = NULL;
	m_powerPurchaseCloseButtonEnable = NULL;
	m_powerPurchaseCloseButtonHightlited = NULL;
	m_powerPurchaseCloseButtonPushed = NULL;
	m_powerPurchaseCloseButtonDisabled = NULL;

}

ControlBarScheme::~ControlBarScheme(void)
{
	reset();
}

ControlBarScheme::ControlBarScheme(void)
{

	m_animations.clear();
	for (Int i = 0; i < MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS; i++)
		m_layer[i].clear();
	m_name.clear();
	m_ScreenCreationRes.x = m_ScreenCreationRes.y = 0;
	m_side.clear();
	m_controlBarPro = FALSE;
	m_buttonQueueImage = NULL;
	m_rightHUDImage = NULL;
	m_buildUpClockColor = GameMakeColor(0,0,0,100);
	m_borderBuildColor = GAME_COLOR_UNDEFINED;
	m_borderActionColor = GAME_COLOR_UNDEFINED;
	m_borderUpgradeColor = GAME_COLOR_UNDEFINED;
	m_borderSystemColor = GAME_COLOR_UNDEFINED;
	//m_buttonQueueImage = TheMappedImageCollection("")
	m_optionsButtonEnable = NULL;
	m_optionsButtonHightlited = NULL;
	m_optionsButtonPushed = NULL;
	m_optionsButtonDisabled = NULL;
	
	// Added By Sadullah Nader
	// Initializations needed 
	
	m_commandBarBorderColor = 0;
	
	//
	m_idleWorkerButtonEnable = NULL;
	m_idleWorkerButtonHightlited = NULL;
	m_idleWorkerButtonPushed = NULL;
	m_idleWorkerButtonDisabled = NULL;

	m_buddyButtonEnable = NULL;
	m_buddyButtonHightlited = NULL;
	m_buddyButtonPushed = NULL;
	m_buddyButtonDisabled= NULL;

	m_beaconButtonEnable = NULL;
	m_beaconButtonHightlited = NULL;
	m_beaconButtonPushed = NULL;
	m_beaconButtonDisabled= NULL;

	m_genBarButtonIn = NULL;
	m_genBarButtonOn = NULL;

	m_toggleButtonUpIn = NULL;
	m_toggleButtonUpOn = NULL;
	m_toggleButtonUpPushed = NULL;
	m_toggleButtonDownIn = NULL;
	m_toggleButtonDownOn = NULL;
	m_toggleButtonDownPushed = NULL;

	m_commandMarkerImage = NULL;
	m_expBarForeground = NULL;

	m_powerPurchaseImage = NULL;
	m_powerPurchaseCloseButtonEnable = NULL;
	m_powerPurchaseCloseButtonHightlited = NULL;
	m_powerPurchaseCloseButtonPushed = NULL;
	m_powerPurchaseCloseButtonDisabled = NULL;

	m_generalButtonEnable = NULL;
	m_generalButtonHightlited = NULL;
	m_generalButtonPushed = NULL;
	m_generalButtonDisabled = NULL;

	m_uAttackButtonEnable = NULL;
	m_uAttackButtonHightlited = NULL;
	m_uAttackButtonPushed = NULL;
	
	m_genArrow = NULL;

	m_minMaxButtonEnable = NULL;
	m_minMaxButtonHightlited = NULL;
	m_minMaxButtonPushed = NULL;
	
	m_minMaxUL.x = 0;
	m_minMaxLR.x = 0;

	m_generalUL.x = 0;
	m_generalLR.x = 0;

	m_uAttackUL.x = 0;
	m_uAttackLR.x = 0;

	m_optionsUL.x = 0;
	m_optionsLR.x = 0;

	m_workerUL.x = 0;
	m_workerLR.x = 0;

	m_chatUL.x = 0;
	m_chatLR.x = 0;

	m_beaconUL.x = 0;
	m_beaconLR.x = 0;

	m_powerBarUL.x = 0;
	m_powerBarLR.x = 0;

	m_minMaxUL.y = 0;
	m_minMaxLR.y = 0;

	m_generalUL.y = 0;
	m_generalLR.y = 0;

	m_uAttackUL.y = 0;
	m_uAttackLR.y = 0;

	m_optionsUL.y = 0;
	m_optionsLR.y = 0;

	m_workerUL.y = 0;
	m_workerLR.y = 0;

	m_chatUL.y = 0;
	m_chatLR.y = 0;

	m_beaconUL.y = 0;
	m_beaconLR.y = 0;

	m_powerBarUL.y = 0;
	m_powerBarLR.y = 0;
	
	m_moneyUL.x = 0;
	m_moneyUL.y = 0;
	
	m_moneyLR.x = 0;
	m_moneyLR.y = 0;
	m_moneyFontSize = 0;
	m_moneyThousands = FALSE;
	m_moneySignPainted = FALSE;
	m_radarUL.x = m_radarUL.y = 0;
	m_radarLR.x = m_radarLR.y = 0;

	for( Int i = 0; i < BLOCK_COUNT; i++ )
	{
		m_blockUL[ i ].x = m_blockUL[ i ].y = 0;
		m_blockLR[ i ].x = m_blockLR[ i ].y = 0;
	}
	for( Int i = 0; i < IDLE_COUNT; i++ )
	{
		m_idleUL[ i ].x = m_idleUL[ i ].y = 0;
		m_idleLR[ i ].x = m_idleLR[ i ].y = 0;
	}
	for( Int i = 0; i < FRAME_PIECE_COUNT; i++ )
		m_radarFrame[ i ] = NULL;
	for( Int i = 0; i < TRAY_PIECE_COUNT; i++ )
		m_powerTray[ i ] = NULL;
	for( Int tab = 0; tab < COMMAND_TAB_IMAGE_COUNT; tab++ )
	{
		for( Int state = 0; state < COMMAND_TAB_STATE_COUNT; state++ )
			m_commandTab[ tab ][ state ] = NULL;
		m_commandTabUL[ tab ].x = m_commandTabUL[ tab ].y = 0;
		m_commandTabLR[ tab ].x = m_commandTabLR[ tab ].y = 0;
	}
	m_commandTabPlateUL.x = m_commandTabPlateUL.y = 0;
	m_commandTabPlateLR.x = m_commandTabPlateLR.y = 0;
	for( Int arrow = 0; arrow < 2; arrow++ )
		for( Int state = 0; state < 4; state++ )
			m_commandTabPage[ arrow ][ state ] = NULL;
	for( Int key = 0; key < SIGNAL_KEY_COUNT; key++ )
	{
		for( Int state = 0; state < 4; state++ )
			m_signal[ key ][ state ] = NULL;
		m_signalUL[ key ].x = m_signalUL[ key ].y = 0;
		m_signalLR[ key ].x = m_signalLR[ key ].y = 0;
	}
	m_signalPlateUL.x = m_signalPlateUL.y = 0;
	m_signalPlateLR.x = m_signalPlateLR.y = 0;
	m_signalPlateImage = NULL;
	for( Int piece = 0; piece < GROWN_PIECE_COUNT; piece++ )
		m_grownRadar[ piece ] = NULL;
	m_grownRadarRight = 0;
	m_grownRadarPeriod = 0;
	m_grownRadarLedge = 0;
	m_tooltip = ControlBarTooltipImages();
	for( Int i = 0; i < TALENT_PIECE_COUNT; i++ )
		m_talent[ i ] = NULL;
}


// The scheme's layout is scaled by the display over this resolution, so a zero here divides by zero.
void ControlBarScheme::validate(void) const
{
	if (m_ScreenCreationRes.x <= 0 || m_ScreenCreationRes.y <= 0)
	{
		DEBUG_CRASH(("ControlBarScheme '%s' has an invalid screen creation resolution (%d, %d)",
			m_name.str(), m_ScreenCreationRes.x, m_ScreenCreationRes.y));
		throw INI_INVALID_DATA;
	}
}

//
// Put one of the windows the scheme owns at its UL/LR, in the .wnd loader's own stretched space.
//
static void placeSchemeWindow( GameWindow *win, const ICoord2D& ul, const ICoord2D& lr,
															 const Coord2D& resMultiplier )
{
	Int parX = 0, parY = 0;
	GameWindow *parent = win->winGetParent();
	if( parent )
		parent->winGetScreenPosition( &parX, &parY );
	const Int x = ul.x * resMultiplier.x - parX;
	const Int y = ul.y * resMultiplier.y - parY;
	win->winSetPosition( x, y );
	win->winSetSize( ( lr.x - ul.x ) * resMultiplier.x + COMMAND_BAR_SIZE_OFFSET,
									 ( lr.y - ul.y ) * resMultiplier.y + COMMAND_BAR_SIZE_OFFSET );
}

/// <prefix><middle><suffix> for each suffix from the mapped images; every one NULL when the scheme names no prefix
static void findPieces( const AsciiString& prefix, const char *middle, const char *const *suffixes, Int count, const Image **out )
{
	for( Int i = 0; i < count; i++ )
	{
		out[ i ] = NULL;
		if( prefix.isEmpty() )
			continue;
		AsciiString name = prefix;
		name.concat( middle );
		name.concat( suffixes[ i ] );
		out[ i ] = TheMappedImageCollection->findImageByName( name );
	}
}

static const char *const theFrameSuffixes[ ControlBarScheme::FRAME_PIECE_COUNT ] = { "TL", "T", "TR", "L", "C", "R", "BL", "B", "BR" };

void ControlBarTooltipImages::find( const AsciiString& prefix )
{
	// the generator's TIP_ICONS, in ICON_* order
	static const char *const iconSuffixes[ ICON_COUNT ] =
		{ "Money", "Time", "Health", "Armor", "Damage", "Rate", "Range", "Speed", "Power" };
	findPieces( prefix, "Tip", theFrameSuffixes, ControlBarScheme::FRAME_PIECE_COUNT, card );
	findPieces( prefix, "TipHead", theFrameSuffixes, 3, head );
	findPieces( prefix, "CursorTip", theFrameSuffixes, ControlBarScheme::FRAME_PIECE_COUNT, cursor );
	findPieces( prefix, "TipIcon", iconSuffixes, ICON_COUNT, icon );
	static const char *const tabSuffix[ 1 ] = { "TabPanel" };
	findPieces( prefix, "", tabSuffix, 1, &tabPanel );
}

void ControlBarScheme::init(void)
{
	// before anything below lays the bar out: layoutPanels fits the radar's frame from these
	static const char *const traySuffixes[ TRAY_PIECE_COUNT ] =
		{ "Cell", "EdgeTop", "EdgeLeft", "EdgeBottom", "CornerTopLeft", "CornerBottomLeft", "Step" };
	findPieces( m_radarFrameName, "", theFrameSuffixes, FRAME_PIECE_COUNT, m_radarFrame );
	findPieces( m_powerTrayName, "", traySuffixes, TRAY_PIECE_COUNT, m_powerTray );
	static const char *const tabStates[ COMMAND_TAB_STATE_COUNT ] = { "", "H", "P", "I", "O" };
	for( Int tab = 0; tab < COMMAND_TAB_IMAGE_COUNT; tab++ )
		findPieces( m_commandTabImage[ tab ], "", tabStates, COMMAND_TAB_STATE_COUNT, m_commandTab[ tab ] );
	for( Int arrow = 0; arrow < 2; arrow++ )
		findPieces( m_commandTabPageImage[ arrow ], "", tabStates, 4, m_commandTabPage[ arrow ] );
	for( Int key = 0; key < SIGNAL_KEY_COUNT; key++ )
		findPieces( m_signalImage[ key ], "", tabStates, 4, m_signal[ key ] );
	findPieces( m_signalPlateImageName, "", tabStates, 1, &m_signalPlateImage );
	static const char *const grownSuffixes[ GROWN_PIECE_COUNT ] = { "TL", "T", "TR", "L", "R", "BL", "B", "BR", "C" };
	findPieces( m_grownRadarName, "", grownSuffixes, GROWN_PIECE_COUNT, m_grownRadar );
	m_tooltip.find( m_tooltipName );
	static const char *const talentSuffixes[ TALENT_PIECE_COUNT ] =
		{ "GeneralsPowerWindowWide", "TalentPlateLocked", "TalentPlateWaiting", "TalentPlatePicked", "TalentPlateRejected",
			"TalentNodeLocked", "TalentNodeWaiting", "TalentNodePicked", "TalentNodeRejected", "TalentSpineLit",
			"TalentArmLeft", "TalentArmRight" };
	findPieces( m_talentName, "", talentSuffixes, TALENT_PIECE_COUNT, m_talent );

	if(TheControlBar)
	{
		TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_DEFAULT);
		TheControlBar->updateBuildQueueDisabledImages( m_buttonQueueImage );
		TheControlBar->updateRightHUDImage(m_rightHUDImage);
		TheControlBar->updateBuildUpClockColor( m_buildUpClockColor );
		TheControlBar->updateCommanBarBorderColors(m_borderBuildColor, m_borderActionColor, m_borderUpgradeColor,m_borderSystemColor);
		TheControlBar->updateBorderColor(m_commandBarBorderColor);
		//TheControlBar->updateCommandMarkerImage(m_commandMarkerImage);
		TheControlBar->updateSlotExitImage(m_commandMarkerImage);
		TheControlBar->updateUpDownImages(m_toggleButtonUpIn, m_toggleButtonUpOn, m_toggleButtonUpPushed, m_toggleButtonDownIn, m_toggleButtonDownOn, m_toggleButtonDownPushed, m_generalButtonEnable, m_generalButtonHightlited);
		TheControlBar->setArrowImage( m_genArrow);
	}
	GameWindow *win = NULL;

	Coord2D resMultiplier;
	resMultiplier.x = TheDisplay->getWidth()/INT_TO_REAL(m_ScreenCreationRes.x) ;
	resMultiplier.y = TheDisplay->getHeight()/INT_TO_REAL(m_ScreenCreationRes.y);

	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PopupCommunicator" ) );
	if(win)	
	{
//		DEBUG_ASSERTCRASH(m_buddyButtonEnable,     ("No enable button image for communicator in scheme %s!\n", m_name.str()));
//		DEBUG_ASSERTCRASH(m_buddyButtonHightlited, ("No hilite button image for communicator in scheme %s!\n", m_name.str()));
//		DEBUG_ASSERTCRASH(m_buddyButtonPushed,     ("No pushed button image for communicator in scheme %s!\n", m_name.str()));
		GadgetButtonSetEnabledImage(win, m_buddyButtonEnable);
		GadgetButtonSetHiliteImage(win, m_buddyButtonHightlited);
		GadgetButtonSetHiliteSelectedImage(win, m_buddyButtonPushed);
		GadgetButtonSetDisabledImage(win, m_buddyButtonDisabled);
		placeSchemeWindow( win, m_chatUL, m_chatLR, resMultiplier );
	}
	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonIdleWorker" ) );
	if(win)	
	{
		GadgetButtonSetEnabledImage(win, m_idleWorkerButtonEnable);
		GadgetButtonSetHiliteImage(win, m_idleWorkerButtonHightlited);
		GadgetButtonSetHiliteSelectedImage(win, m_idleWorkerButtonPushed);
		GadgetButtonSetDisabledImage(win, m_idleWorkerButtonDisabled);
		placeSchemeWindow( win, m_workerUL, m_workerLR, resMultiplier );
	}

	//
	// The idle infantry, vehicle and aircraft buttons stand in a column at the grid's right end, the
	// select army button in the left one.  A scheme that does not place one shrinks it to nothing
	// rather than leave it at the .wnd's 0,0.
	//
	static const char *const idleNames[ IDLE_COUNT ] =
	{
		"ControlBar.wnd:ButtonIdleInfantry",
		"ControlBar.wnd:ButtonIdleVehicle",
		"ControlBar.wnd:ButtonIdleAircraft",
		"ControlBar.wnd:ButtonSelectArmy",
	};
	for( Int i = 0; i < IDLE_COUNT; i++ )
	{
		win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( idleNames[ i ] ) );
		if( win == NULL )
			continue;
		if( m_idleLR[ i ].x <= m_idleUL[ i ].x )
		{
			win->winSetSize( 0, 0 );
			continue;
		}
		static const char *const states[ 4 ] = { "", "H", "P", "I" };
		const Image *image[ 4 ];
		findPieces( m_idleButtonImage[ i ], "", states, 4, image );
		GadgetButtonSetEnabledImage( win, image[ 0 ] );
		GadgetButtonSetHiliteImage( win, image[ 1 ] );
		GadgetButtonSetHiliteSelectedImage( win, image[ 2 ] );
		GadgetButtonSetDisabledImage( win, image[ 3 ] );
		placeSchemeWindow( win, m_idleUL[ i ], m_idleLR[ i ], resMultiplier );
	}

	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ExpBarForeground" ) );
	if(win)	
	{
		win->winSetEnabledImage(0, m_expBarForeground);
	}
	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonOptions" ) );
	if(win)	
	{
		GadgetButtonSetEnabledImage(win, m_optionsButtonEnable);
		GadgetButtonSetHiliteImage(win, m_optionsButtonHightlited);
		GadgetButtonSetHiliteSelectedImage(win, m_optionsButtonPushed);
		GadgetButtonSetDisabledImage(win, m_optionsButtonDisabled);
		placeSchemeWindow( win, m_optionsUL, m_optionsLR, resMultiplier );
	}
	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonPlaceBeacon" ) );
	if(win)	
	{
		GadgetButtonSetEnabledImage(win, m_beaconButtonEnable);
		GadgetButtonSetHiliteImage(win, m_beaconButtonHightlited);
		GadgetButtonSetHiliteSelectedImage(win, m_beaconButtonPushed);
		GadgetButtonSetDisabledImage(win, m_beaconButtonDisabled);
		placeSchemeWindow( win, m_beaconUL, m_beaconLR, resMultiplier );
	}

	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:MoneyDisplay" ) );
	if(win)
	{
		placeSchemeWindow( win, m_moneyUL, m_moneyLR, resMultiplier );
		// set every time, so a scheme without the keys (the observer's, when a player is out) gets
		// ControlBar.wnd's Arial 8 and centring back
		enum { WND_MONEY_FONT_SIZE = 8 };
		const Bool own = m_moneyFontSize > 0;
		win->winSetFont( TheFontLibrary->getFont( win->winGetFont()->nameString,
			TheGlobalLanguageData->adjustFontSize( own ? m_moneyFontSize : WND_MONEY_FONT_SIZE ), own ) );
		TextData *text = (TextData *)win->winGetUserData();
		text->centered = !m_moneySignPainted;
		if( m_moneySignPainted )
			text->leftMargin = 0;
	}

	// a scheme whose painting has its own radar box stands the radar in it; Radar::findDrawPositions
	// letterboxes the map inside
	win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:LeftHUD" ) );
	if( win && m_radarLR.x > m_radarUL.x )
		placeSchemeWindow( win, m_radarUL, m_radarLR, resMultiplier );

	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:PowerWindow" ) );
	if(win)
		placeSchemeWindow( win, m_powerBarUL, m_powerBarLR, resMultiplier );

	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonGeneral" ) );
	if(win)	
	{

		GadgetButtonSetEnabledImage(win, m_generalButtonEnable);
		GadgetButtonSetHiliteImage(win, m_generalButtonHightlited);
		GadgetButtonSetHiliteSelectedImage(win, m_generalButtonPushed);
		GadgetButtonSetDisabledImage(win, m_generalButtonDisabled);
		placeSchemeWindow( win, m_generalUL, m_generalLR, resMultiplier );
	}
	
	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ButtonLarge" ) );
	if(win)	
	{
		// The images are set above
//		GadgetButtonSetEnabledImage(win, m_minMaxButtonEnable);
//		GadgetButtonSetHiliteImage(win, m_minMaxButtonHightlited);
//		GadgetButtonSetHiliteSelectedImage(win, m_minMaxButtonPushed);
		placeSchemeWindow( win, m_minMaxUL, m_minMaxLR, resMultiplier );
	}
	
	win= TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "ControlBar.wnd:WinUAttack" ) );
	if(win)	
	{
		win->winSetEnabledImage(0,m_uAttackButtonEnable);
		win->winSetDisabledImage(0,m_uAttackButtonHightlited);
		placeSchemeWindow( win, m_uAttackUL, m_uAttackLR, resMultiplier );
	}

	win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:GenExpParent" ) );
	if(win)
	{
		win->winSetEnabledImage(0,m_powerPurchaseImage);
		// where it stands is ControlBar Pro's GeneralsExpPoints.wnd at its pieces' scale, centred at the
		// top (ControlBarLayoutUniform in ControlBar::initWindows), so nothing here moves it
	}
	// its close button stands in the window's close boss, the same rect for every side
	win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( "GeneralsExpPoints.wnd:ButtonExit" ) );
	if( win && m_powerPurchaseCloseButtonEnable )
	{
		GadgetButtonSetEnabledImage( win, m_powerPurchaseCloseButtonEnable );
		GadgetButtonSetHiliteImage( win, m_powerPurchaseCloseButtonHightlited );
		GadgetButtonSetHiliteSelectedImage( win, m_powerPurchaseCloseButtonPushed );
		GadgetButtonSetDisabledImage( win, m_powerPurchaseCloseButtonDisabled );
	}

	//
	// A painting whose plates outgrow ControlBar.wnd's input blockers names its own, so a click on
	// the new steel does not fall through to the world.
	//
	static const char *const blockNames[ BLOCK_COUNT ] =
	{
		"ControlBar.wnd:ControlBarProBlockInputRadar",
		"ControlBar.wnd:ControlBarProBlockInputProduction",
		"ControlBar.wnd:ControlBarProBlockInputMoney",
		"ControlBar.wnd:ControlBarProBlockInputUpgrades",
		"ControlBar.wnd:ControlBarProBlockInputGenPowerButton",
		"ControlBar.wnd:ControlBarProBlockInputObserverPlayerInfoH",
	};
	for( Int i = 0; i < BLOCK_COUNT; i++ )
	{
		if( m_blockLR[ i ].x <= m_blockUL[ i ].x )
			continue;
		win = TheWindowManager->winGetWindowFromId( NULL, TheNameKeyGenerator->nameToKey( blockNames[ i ] ) );
		if( win == NULL )
			continue;
		placeSchemeWindow( win, m_blockUL[ i ], m_blockLR[ i ], resMultiplier );
	}

	//
	// Everything above put a window back where the loader would have stretched it to, which is
	// the whole width of the screen rather than one of the three panels the bar is really in.
	// layoutPanels reads those positions and re-anchors them; it is why it has to run last, and
	// why it runs again every time a scheme is set rather than once at startup.
	//
	if( TheControlBar )
		TheControlBar->layoutPanels();
}

//
// Add an animation to the animation list
//-----------------------------------------------------------------------------
void ControlBarScheme::addAnimation( ControlBarSchemeAnimation *schemeAnim )
{
	if( !schemeAnim )
	{
		DEBUG_ASSERTCRASH(FALSE,("Trying to add a null animation to the controlbarscheme\n"));
		return;
	}
	m_animations.push_back( schemeAnim );
}// addAnimation

//
// Add an image to the proper layer list
//-----------------------------------------------------------------------------
void ControlBarScheme::addImage( ControlBarSchemeImage *schemeImage )
{
	if( !schemeImage )
	{
		DEBUG_ASSERTCRASH(FALSE,("Trying to add a null image to the controlbarscheme\n"));
		return;
	}

	if(schemeImage->m_layer < 0 || schemeImage->m_layer >= MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS)
	{
		DEBUG_ASSERTCRASH(FALSE,("SchemeImage %s attempted to be added to layer %d which is not Between to %d, %d\n",
								 schemeImage->m_name.str(), schemeImage->m_layer, 0, MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS));
		// bring the foobar to the front so we make it obvious that something's wrong
		schemeImage->m_layer = 0;
	}

	m_layer[schemeImage->m_layer].push_back(schemeImage);
}// addImage

//
// Update the position of the image that's animating
//-----------------------------------------------------------------------------
void ControlBarScheme::updateAnim (ControlBarSchemeAnimation * anim)
{
	switch(anim->m_animType)
	{
		case ControlBarSchemeAnimation::CB_ANIM_SLIDE_RIGHT:
		{
			animSlideRight( anim );
			break;
		}
		default:
		{
			DEBUG_ASSERTCRASH(FALSE,("We tried to animate but not animate function was found %d", anim->m_animType));
		}
	}
}

//
// Add an image to the proper layer list
//-----------------------------------------------------------------------------
void ControlBarScheme::update( void )
{
	ControlBarSchemeAnimationList::iterator it = m_animations.begin();
	
	while (it != m_animations.end())
	{
		ControlBarSchemeAnimation *anim = *it;
		if( !anim )
		{
			DEBUG_ASSERTCRASH(FALSE,("THere's no Animation in the ControlBarSchemeAnimationList:m_animations"));
			return;
		}
		updateAnim( anim );
		it ++;	
	}
	
}

//
// Loop through the propper lists and draw everything for the forground
//-----------------------------------------------------------------------------
void ControlBarScheme::drawForeground( Coord2D multi, ICoord2D offset )
{
	for(Int i = CONTROL_BAR_SCHEME_FOREGROUND_IMAGE_LAYERS - 1; i >= 0; i--)
	{
		ControlBarSchemeImageList::iterator it = m_layer[i].begin();
		while (it != m_layer[i].end())
		{	
			ControlBarSchemeImage *schemeImage = *it;
			if( !schemeImage )
			{
				DEBUG_ASSERTCRASH(FALSE,("There is no ControlBarSchemeImage found in the m_layer list"));
				it++;
				continue;
			}
			
			// if we dont' have an image, don't try to draw it
			if(!schemeImage->m_image)
			{
				it++;
				continue;
			}
			
			// draw the image, its far edges rounded out: 1066 * (1920 / 1066.0f) truncated to 1919 and left
			// the screen's last column of battlefield showing past the bar
			TheDisplay->drawImage(schemeImage->m_image,
														REAL_TO_INT_FLOOR( schemeImage->m_position.x * multi.x ) + offset.x,
														REAL_TO_INT_FLOOR( schemeImage->m_position.y * multi.y ) + offset.y,
														REAL_TO_INT_CEIL( ( schemeImage->m_position.x + schemeImage->m_size.x ) * multi.x - 0.01f ) + offset.x,
														REAL_TO_INT_CEIL( ( schemeImage->m_position.y + schemeImage->m_size.y ) * multi.y - 0.01f ) + offset.y);
			
			it ++;	
		}
	}
}

//
// Add an image to the proper layer list
//-----------------------------------------------------------------------------
void ControlBarScheme::drawBackground( Coord2D multi, ICoord2D offset )
{
	
	for(Int i = MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS - 1; i >= CONTROL_BAR_SCHEME_FOREGROUND_IMAGE_LAYERS; i--)
	{
		ControlBarSchemeImageList::iterator it = m_layer[i].begin();
		while (it != m_layer[i].end())
		{	
			ControlBarSchemeImage *schemeImage = *it;
			if( !schemeImage )
			{
				DEBUG_ASSERTCRASH(FALSE,("There is no ControlBarSchemeImage found in the m_layer list"));
				it++;
				continue;
			}
			
			// if we don't have an image, don't try to draw it
			if(!schemeImage->m_image)
			{
				it++;
				continue;
			}
			
			// draw it, rounded out as drawForeground does
			TheDisplay->drawImage(schemeImage->m_image,
														REAL_TO_INT_FLOOR( schemeImage->m_position.x * multi.x ) + offset.x,
														REAL_TO_INT_FLOOR( schemeImage->m_position.y * multi.y ) + offset.y,
														REAL_TO_INT_CEIL( ( schemeImage->m_position.x + schemeImage->m_size.x ) * multi.x - 0.01f ) + offset.x,
														REAL_TO_INT_CEIL( ( schemeImage->m_position.y + schemeImage->m_size.y ) * multi.y - 0.01f ) + offset.y);
			
			it ++;	
		}
	}
}


//
// Constructor for the manager
//-----------------------------------------------------------------------------
ControlBarSchemeManager::ControlBarSchemeManager( void )
{
	m_currentScheme = NULL;
	m_loadingControlBarPro = FALSE;
	m_consoleTooltip = ControlBarTooltipImages();
	m_schemeList.clear();
	m_multiplyer.x = m_multiplyer.y = 1;
}

//
// Destructor for the Manager
//-----------------------------------------------------------------------------
ControlBarSchemeManager::~ControlBarSchemeManager( void )
{
	ControlBarSchemeList::iterator it = m_schemeList.begin();

	// iterate through the list and return the scheme that we're looking for
	while (it != m_schemeList.end())
	{
		ControlBarScheme *CBScheme = *it;
		if( CBScheme )
			delete CBScheme;
		CBScheme = NULL;
		it ++;	
	}
	m_schemeList.clear();
	m_currentScheme = NULL;

}

//
// Parse the Image Part of the command bar
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::parseImagePart(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "Position",				INI::parseICoord2D,				NULL, offsetof( ControlBarSchemeImage, m_position ) },
			{ "Size",						INI::parseICoord2D,				NULL, offsetof( ControlBarSchemeImage, m_size ) },
      { "ImageName",			INI::parseMappedImage,		NULL, offsetof( ControlBarSchemeImage, m_image ) },
			{ "Layer",					INI::parseInt,						NULL, offsetof( ControlBarSchemeImage, m_layer ) },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	ControlBarSchemeImage *schemeImage = NEW ControlBarSchemeImage;
	ini->initFromINI(schemeImage, myFieldParse);
	((ControlBarScheme*)instance)->addImage(schemeImage);

}

//
// each animation part contains and image, parse it
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::parseAnimatingPartImage(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "Position",				INI::parseICoord2D,				NULL, offsetof( ControlBarSchemeImage, m_position ) },
			{ "Size",						INI::parseICoord2D,				NULL, offsetof( ControlBarSchemeImage, m_size ) },
      { "ImageName",			INI::parseMappedImage,		NULL, offsetof( ControlBarSchemeImage, m_image ) },
			{ "Layer",					INI::parseInt,						NULL, offsetof( ControlBarSchemeImage, m_layer ) },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};

	ControlBarSchemeImage *schemeImage = NEW ControlBarSchemeImage;
	ini->initFromINI(schemeImage, myFieldParse);
	((ControlBarSchemeAnimation*)instance)->m_animImage = schemeImage;

}

//
// parse the animating part of the control bar scheme
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::parseAnimatingPart(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
{
	static const FieldParse myFieldParse[] = 
		{
			{ "Name",						INI::parseAsciiString,		NULL, offsetof( ControlBarSchemeAnimation, m_name ) },
      { "Animation",			INI::parseLookupList,			AnimTypeNames, offsetof( ControlBarSchemeAnimation, m_animType ) },
			{ "Duration",				INI::parseDurationUnsignedInt,			NULL, offsetof( ControlBarSchemeAnimation, m_animDuration ) },
			{ "FinalPos",				INI::parseICoord2D,			NULL, offsetof( ControlBarSchemeAnimation, m_finalPos ) },
			{ "ImagePart",			ControlBarSchemeManager::parseAnimatingPartImage,	NULL, NULL },
			{ NULL,							NULL,											NULL, 0 }  // keep this last
		};
	
	ControlBarSchemeAnimation *schemeAnim = NEW ControlBarSchemeAnimation;
	ini->initFromINI(schemeAnim, myFieldParse);
	((ControlBarScheme*)instance)->addAnimation(schemeAnim);
	((ControlBarScheme*)instance)->addImage(schemeAnim->m_animImage);
}


//
// Create a new control bar and return it.  Link it into our control bar list
//-----------------------------------------------------------------------------
ControlBarScheme *ControlBarSchemeManager::newControlBarScheme( AsciiString name )
{
	ControlBarScheme *cbScheme = 	findControlBarScheme(name);
	if(cbScheme)
	{
		DEBUG_ASSERTCRASH(false,("We're overwriting a previous control bar scheme %s",name.str()));
		cbScheme->reset();
		cbScheme->m_name.set( name );
		cbScheme->m_name.toLower();
		cbScheme->m_controlBarPro = m_loadingControlBarPro;
		return cbScheme;
	}

	cbScheme = NEW ControlBarScheme;

	if( !cbScheme  || name.isEmpty() )
	{
		DEBUG_ASSERTCRASH(FALSE,("Could not create controlbar %s", name.str()));
		return NULL;
	}

	cbScheme->m_name.set( name );
	cbScheme->m_name.toLower();
	cbScheme->m_controlBarPro = m_loadingControlBarPro;

	m_schemeList.push_back(cbScheme);
		
	return cbScheme;
}

//
// Find a scheme based on name
//-----------------------------------------------------------------------------
ControlBarScheme *ControlBarSchemeManager::findControlBarScheme( AsciiString name )
{
	name.toLower();
	
	ControlBarSchemeList::iterator it = m_schemeList.begin();

	// iterate through the list and return the scheme that we're looking for
	while (it != m_schemeList.end())
	{
		ControlBarScheme *CBScheme = *it;
		if( !CBScheme )
		{
			DEBUG_ASSERTCRASH(FALSE,("There's no ControlBarScheme in the ControlBarSchemeList:m_schemeList"));
			return NULL;
		}
		if(CBScheme->m_name.compareNoCase( name ) == 0)
			return CBScheme;
		it ++;	
	}
	return NULL;
}

//
// Preload assets
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::preloadAssets( TimeOfDay timeOfDay )
{
	for (ControlBarSchemeList::iterator it = m_schemeList.begin(); it != m_schemeList.end(); ++it)
	{
		ControlBarScheme *CBScheme = *it;
		if( !CBScheme )
		{
			DEBUG_ASSERTCRASH(FALSE,("There's no ControlBarScheme in the ControlBarSchemeList:m_schemeList"));
			continue;
		}
		if (CBScheme->m_buttonQueueImage)
		{
			TheDisplay->preloadTextureAssets(CBScheme->m_buttonQueueImage->getFilename());
		}

		if (CBScheme->m_rightHUDImage)
		{
			// was preloading m_buttonQueueImage here (copy-paste), which also meant a NULL deref
			// for a scheme that sets RightHUD but no button queue image.
			TheDisplay->preloadTextureAssets(CBScheme->m_rightHUDImage->getFilename());
		}

		for (Int layer = 0; layer < MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS; ++layer)
		{
			for (ControlBarScheme::ControlBarSchemeImageList::iterator listIt = CBScheme->m_layer[layer].begin(); listIt != CBScheme->m_layer[layer].end(); ++listIt)
			{
				ControlBarSchemeImage *cbImage = *listIt;
				if (cbImage)
				{
					const Image *image = TheMappedImageCollection->findImageByName( cbImage->m_name );
					if (image)
					{
						TheDisplay->preloadImageTexture(image->getFilename());
					}
				}
			}
		}
	}
}

//
// Find a scheme based on name
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::init( void )
{

	INI ini;
	// Read from INI all the ControlBarSchemes
	ini.load( AsciiString( "Data\\INI\\Default\\ControlBarScheme.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\ControlBarScheme.ini" ), INI_LOAD_OVERWRITE, NULL );
	// the fork's edits to EA's schemes, each block naming only what it changes
	ini.load( AsciiString( "Data\\INI\\ControlBarSchemeReforged.ini" ), INI_LOAD_MULTIFILE, NULL );
	// ControlBar Pro's schemes, the bar every side wears: each replaces EA's of the same name whole,
	// so nothing above reaches it (a Debug build asserts once a scheme, newControlBarScheme).  A
	// scheme of a name it does not have stays EA's, so the ones read here are marked and
	// findSchemeForFaction picks among them only.
	m_loadingControlBarPro = TRUE;
	ini.load( AsciiString( "Data\\INI\\ControlBarProReforged.ini" ), INI_LOAD_OVERWRITE, NULL );
	m_loadingControlBarPro = FALSE;
	m_consoleTooltip.find( AsciiString( "Helpbox" ) );

//	//Load the user modified control bar schemes
//	WIN32_FIND_DATA findData;
//	AsciiString userDataPath;
//	if(TheGlobalData)
//	{
//		userDataPath.format("%sINI\\ControlBarScheme.ini",TheGlobalData->getPath_UserData().str());
//		if	(FindFirstFile(userDataPath.str(), &findData) !=INVALID_HANDLE_VALUE)
//			ini.load(userDataPath,  INI_LOAD_OVERWRITE, NULL );
//	}
	if( m_schemeList.size() <= 0 )
	{
		DEBUG_ASSERTCRASH(FALSE,("There's no ControlBarScheme in the ControlBarSchemeList:m_schemeList that was just read from the INI file"));
		return;
	}

}

//
// only recommended to use if you know what you're doing, cause you could actually load in
// a control bar built for 16x12 into an 8x6 screen which would use a lot more memory then it should
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::setControlBarScheme(AsciiString schemeName)
{
	ControlBarScheme *tempScheme = findControlBarScheme( schemeName );
	if(tempScheme)
	{
		// setup the multiplyer value
		// (cast like the sibling below: this was an Int/Int divide, so any resolution that is not
		// an exact multiple of the scheme's authoring resolution scaled by 1.0 or 0.)
		m_multiplyer.x = TheDisplay->getWidth() / (Real)tempScheme->m_ScreenCreationRes.x;
		m_multiplyer.y = TheDisplay->getHeight() / (Real)tempScheme->m_ScreenCreationRes.y;
		m_currentScheme = tempScheme;
	}
	else
	{
		DEBUG_ASSERTCRASH(FALSE,("There's no ControlBarScheme in the ControlBarSchemeList:m_schemeList"));
		m_currentScheme = NULL;
	}
	if(m_currentScheme)
		m_currentScheme->init();
}

//
// Update calls all the animation update calls for the
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::update( void )
{
	//
	// The scheme animations count .ini-authored frames, one per call, and this is called once per
	// rendered frame. With the frame rate uncapped that runs them several times too fast, so pace
	// them off the wall clock at the rate they were authored against. State is a static rather than
	// a member because there is exactly one scheme manager and nothing restarts this animation.
	//
	static UnsignedInt lastStepMs = 0;
	static Real stepAccumMs = 0.0f;

	// Duration is parsed from ms into logic frames, so the steps are logic frames too
	if (!GameClient_isUiAnimStepDue(lastStepMs, stepAccumMs, Clock_Milliseconds(), LOGICFRAMES_PER_SECONDS_REAL))
		return;

	if(m_currentScheme)
		m_currentScheme->update();
}

//-----------------------------------------------------------------------------
void ControlBarSchemeManager::drawForeground( ICoord2D offset )
{
	if(m_currentScheme)
		m_currentScheme->drawForeground( m_multiplyer, offset );
}
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::drawBackground( ICoord2D offset )
{
	if(m_currentScheme)
		m_currentScheme->drawBackground( m_multiplyer, offset );
}

//-----------------------------------------------------------------------------
/** Which side's artwork the bar on screen is wearing, when the scheme's own name is not a side
	* anything else knows - "Observer".  ControlBar.ini builds that bar entirely out of one faction's
	* plates (the American ones, down to the logo in the right hand corner), so the scheme that shares
	* its right hand plate is the side a watcher is really looking at, and anything dressing itself to
	* match the bar - the HUD strips do - has a side to ask for. */
//-----------------------------------------------------------------------------
AsciiString ControlBarSchemeManager::getCurrentArtTwinSide( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_rightHUDImage == NULL )
		return AsciiString::TheEmptyString;

	for( ControlBarSchemeList::const_iterator it = m_schemeList.begin(); it != m_schemeList.end(); ++it )
	{
		const ControlBarScheme *scheme = *it;
		if( scheme == NULL || scheme->m_rightHUDImage != m_currentScheme->m_rightHUDImage )
			continue;
		if( scheme->m_side.isEmpty() || scheme->m_side.compareNoCase( m_currentScheme->m_side ) == 0 )
			continue;

		return scheme->m_side;
	}

	return AsciiString::TheEmptyString;

}

//-----------------------------------------------------------------------------
/** The side's widest scheme, as EA picked.  Neither interface draws a scheme's painting; the plates
	* stand in for it (ControlBarPlateForSide), and the scheme only places the bar's own windows. */
//-----------------------------------------------------------------------------
ControlBarScheme *ControlBarSchemeManager::findSchemeForSide( const AsciiString& side, Bool controlBarProOnly ) const
{
	ControlBarScheme *best = NULL;
	for( ControlBarSchemeList::const_iterator it = m_schemeList.begin(); it != m_schemeList.end(); ++it )
	{
		ControlBarScheme *scheme = *it;
		if( scheme->m_side.compareNoCase( side ) != 0 || ( controlBarProOnly && !scheme->m_controlBarPro ) )
			continue;
		if( best == NULL || scheme->m_ScreenCreationRes.x > best->m_ScreenCreationRes.x )
			best = scheme;
	}
	return best;
}

//-----------------------------------------------------------------------------
/** The bar follows the country, and is always ControlBar Pro's.  A side with a ControlBar Pro scheme of
	* its own (Boss and Observer among them) wears it; any other wears its country's by name, the way
	* ControlBarPlateForSide reads a side, so a ChinaTankGeneral without one gets China's.  A side of no
	* country we know (the civilians) wears America's.  The squished bar does not come here. */
//-----------------------------------------------------------------------------
ControlBarScheme *ControlBarSchemeManager::findSchemeForFaction( const AsciiString& side ) const
{
	ControlBarScheme *scheme = findSchemeForSide( side, TRUE );
	if( scheme != NULL )
		return scheme;

	if( side.startsWithNoCase( "China" ) )
		return findSchemeForSide( AsciiString( "China" ), TRUE );
	if( side.startsWithNoCase( "GLA" ) )
		return findSchemeForSide( AsciiString( "GLA" ), TRUE );
	return findSchemeForSide( AsciiString( "America" ), TRUE );
}

Bool ControlBarSchemeManager::isCurrentSchemePainted( void ) const
{
	return m_currentScheme != NULL;
}

const Image *const *ControlBarSchemeManager::getRadarFrame( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_radarFrame[ ControlBarScheme::FRAME_TL ] == NULL
			|| m_currentScheme->m_radarFrame[ ControlBarScheme::FRAME_BR ] == NULL )
		return NULL;
	return m_currentScheme->m_radarFrame;
}

void ControlBarSchemeManager::radarFrameRect( const Image *const *frame, const IRegion2D& radar, IRegion2D *outer )
{
	const Real k = TheDisplay->getHeight() / 1080.0f;
	const Image *tl = frame[ ControlBarScheme::FRAME_TL ];
	const Image *br = frame[ ControlBarScheme::FRAME_BR ];
	outer->lo.x = radar.lo.x - REAL_TO_INT_CEIL( tl->getImageWidth() * k );
	outer->lo.y = radar.lo.y - REAL_TO_INT_CEIL( tl->getImageHeight() * k );
	outer->hi.x = radar.hi.x + REAL_TO_INT_CEIL( br->getImageWidth() * k );
	outer->hi.y = radar.hi.y + REAL_TO_INT_CEIL( br->getImageHeight() * k );
}

Bool ControlBarSchemeManager::isMoneyGrouped( void ) const
{
	return m_currentScheme != NULL && m_currentScheme->m_moneyThousands;
}

Bool ControlBarSchemeManager::isMoneySignPainted( void ) const
{
	return m_currentScheme != NULL && m_currentScheme->m_moneySignPainted;
}

Int ControlBarSchemeManager::radarArtTop( Int fallback ) const
{
	const Int radar = ControlBarScheme::BLOCK_RADAR;
	if( m_currentScheme == NULL || m_currentScheme->m_blockLR[ radar ].x <= m_currentScheme->m_blockUL[ radar ].x )
		return fallback;
	static NameKeyType blockID = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarProBlockInputRadar" );
	GameWindow *block = TheWindowManager->winGetWindowFromId( NULL, blockID );
	if( block == NULL )
		return fallback;
	Int x, y;
	block->winGetScreenPosition( &x, &y );
	// the signal keys' plate stands on the radar's top and rides a grown radar up: what stands on the
	// radar's top stands on the plate's, wherever it is placed now
	IRegion2D plate;
	if( TheControlBar && TheControlBar->getSignalPlateRect( &plate ) )
		y = min( y, plate.lo.y );
	return y;
}

const Image *const *ControlBarSchemeManager::getCommandTabImages( Int tab ) const
{
	if( m_currentScheme == NULL || tab < 0 || tab >= ControlBarScheme::COMMAND_TAB_IMAGE_COUNT
			|| m_currentScheme->m_commandTab[ tab ][ 0 ] == NULL )
		return NULL;
	return m_currentScheme->m_commandTab[ tab ];
}

const Image *const *ControlBarSchemeManager::getCommandTabPageImages( Int arrow ) const
{
	if( m_currentScheme == NULL || arrow < 0 || arrow > 1 || m_currentScheme->m_commandTabPage[ arrow ][ 0 ] == NULL )
		return NULL;
	return m_currentScheme->m_commandTabPage[ arrow ];
}

/** Seats named in a scheme's creation resolution, to 800x600 design units by drawPainting's own
	* conversion; the plate is the seats' extent where the scheme names none.  FALSE when any seat is
	* empty or the scheme has no creation resolution. */
static Bool seatsToDesign( const ControlBarScheme *scheme, const ICoord2D *ul, const ICoord2D *lr, Int count,
													 const ICoord2D &plateUL, const ICoord2D &plateLR, IRegion2D *seats, IRegion2D *plate )
{
	if( scheme == NULL || scheme->m_ScreenCreationRes.x <= 0 || scheme->m_ScreenCreationRes.y <= 0 )
		return FALSE;
	const Real toDesignX = 800.0f / scheme->m_ScreenCreationRes.x;
	const Real toDesignY = 600.0f / scheme->m_ScreenCreationRes.y;
	struct Convert
	{
		static Bool rect( const ICoord2D &ul, const ICoord2D &lr, Real dx, Real dy, IRegion2D *out )
		{
			if( lr.x <= ul.x || lr.y <= ul.y )
				return FALSE;
			out->lo.x = REAL_TO_INT_FLOOR( ul.x * dx );
			out->lo.y = REAL_TO_INT_FLOOR( ul.y * dy );
			out->hi.x = REAL_TO_INT_CEIL( lr.x * dx );
			out->hi.y = REAL_TO_INT_CEIL( lr.y * dy );
			return TRUE;
		}
	};
	for( Int i = 0; i < count; i++ )
		if( !Convert::rect( ul[ i ], lr[ i ], toDesignX, toDesignY, &seats[ i ] ) )
			return FALSE;
	if( !Convert::rect( plateUL, plateLR, toDesignX, toDesignY, plate ) )
	{
		*plate = seats[ 0 ];
		for( Int i = 1; i < count; i++ )
		{
			plate->lo.x = min( plate->lo.x, seats[ i ].lo.x );
			plate->lo.y = min( plate->lo.y, seats[ i ].lo.y );
			plate->hi.x = max( plate->hi.x, seats[ i ].hi.x );
			plate->hi.y = max( plate->hi.y, seats[ i ].hi.y );
		}
	}
	return TRUE;
}

Bool ControlBarSchemeManager::getCommandTabSeats( IRegion2D *tabs, IRegion2D *plate ) const
{
	return m_currentScheme != NULL && seatsToDesign( m_currentScheme, m_currentScheme->m_commandTabUL, m_currentScheme->m_commandTabLR,
		ControlBarScheme::COMMAND_TAB_IMAGE_COUNT, m_currentScheme->m_commandTabPlateUL, m_currentScheme->m_commandTabPlateLR, tabs, plate );
}

Bool ControlBarSchemeManager::getSignalSeats( IRegion2D *keys, IRegion2D *plate ) const
{
	return m_currentScheme != NULL && seatsToDesign( m_currentScheme, m_currentScheme->m_signalUL, m_currentScheme->m_signalLR,
		ControlBarScheme::SIGNAL_KEY_COUNT, m_currentScheme->m_signalPlateUL, m_currentScheme->m_signalPlateLR, keys, plate );
}

const Image *const *ControlBarSchemeManager::getGrownRadarPieces( void ) const
{
	if( m_currentScheme == NULL )
		return NULL;
	for( Int piece = 0; piece < ControlBarScheme::GROWN_PIECE_COUNT; piece++ )
		if( m_currentScheme->m_grownRadar[ piece ] == NULL )
			return NULL;
	return m_currentScheme->m_grownRadar;
}

Real ControlBarSchemeManager::getArtScale( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_ScreenCreationRes.y <= 0 || TheDisplay == NULL )
		return 1.0f;
	return (Real)TheDisplay->getHeight() / (Real)m_currentScheme->m_ScreenCreationRes.y;
}

Int ControlBarSchemeManager::getGrownRadarRight( void ) const
{
	return m_currentScheme ? REAL_TO_INT_FLOOR( m_currentScheme->m_grownRadarRight * getArtScale() + 0.5f ) : 0;
}

Int ControlBarSchemeManager::getGrownRadarLedge( void ) const
{
	return m_currentScheme ? REAL_TO_INT_FLOOR( m_currentScheme->m_grownRadarLedge * getArtScale() + 0.5f ) : 0;
}

const Image *const *ControlBarSchemeManager::getSignalImages( Int kind ) const
{
	if( m_currentScheme == NULL || kind < 0 || kind >= ControlBarScheme::SIGNAL_KEY_COUNT || m_currentScheme->m_signal[ kind ][ 0 ] == NULL )
		return NULL;
	return m_currentScheme->m_signal[ kind ];
}

const Image *const *ControlBarSchemeManager::getPowerTray( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_powerTray[ ControlBarScheme::TRAY_CELL ] == NULL )
		return NULL;
	return m_currentScheme->m_powerTray;
}

const Image *const *ControlBarSchemeManager::getTalentImages( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_talent[ ControlBarScheme::TALENT_WINDOW ] == NULL )
		return NULL;
	return m_currentScheme->m_talent;
}

const ControlBarTooltipImages& ControlBarSchemeManager::getTooltipImages( void ) const
{
	if( m_currentScheme == NULL || m_currentScheme->m_tooltip.card[ ControlBarScheme::FRAME_TL ] == NULL )
		return m_consoleTooltip;
	return m_currentScheme->m_tooltip;
}

const Image *ControlBarSchemeManager::getTabImage( const char *piece ) const
{
	if( getTabPanelImage() == NULL )
		return NULL;
	AsciiString name = m_currentScheme->m_tooltipName;
	name.concat( "Tab" );
	name.concat( piece );
	return TheMappedImageCollection->findImageByName( name );
}

//-----------------------------------------------------------------------------
/** Nine pieces between outer and inner: the corners, the edges stretched along inner and the centre
	* under it.  A piece the art leaves out (drawImage takes NULL) leaves what is under it showing. */
//-----------------------------------------------------------------------------
static void drawNinePiece( const Image *const *pieces, const IRegion2D& outer, const IRegion2D& inner )
{
	const Int xs[ 4 ] = { outer.lo.x, inner.lo.x, inner.hi.x, outer.hi.x };
	const Int ys[ 4 ] = { outer.lo.y, inner.lo.y, inner.hi.y, outer.hi.y };
	for( Int piece = 0; piece < ControlBarScheme::FRAME_PIECE_COUNT; piece++ )
	{
		const Int col = piece % 3, row = piece / 3;
		TheDisplay->drawImage( pieces[ piece ], xs[ col ], ys[ row ], xs[ col + 1 ], ys[ row + 1 ] );
	}
}

Bool ControlBarDrawNinePieceBox( const Image *const *pieces, const IRegion2D& box, Real k )
{
	const Image *tl = pieces[ ControlBarScheme::FRAME_TL ];
	const Image *br = pieces[ ControlBarScheme::FRAME_BR ];
	if( tl == NULL || br == NULL )
		return FALSE;
	IRegion2D inner;
	inner.lo.x = box.lo.x + REAL_TO_INT_CEIL( tl->getImageWidth() * k );
	inner.lo.y = box.lo.y + REAL_TO_INT_CEIL( tl->getImageHeight() * k );
	// a box narrower than its two corners (the pointer's tooltip opening) lets the right ones overhang
	inner.hi.x = max( inner.lo.x, box.hi.x - REAL_TO_INT_CEIL( br->getImageWidth() * k ) );
	inner.hi.y = max( inner.lo.y, box.hi.y - REAL_TO_INT_CEIL( br->getImageHeight() * k ) );
	IRegion2D outer = box;
	outer.hi.x = max( outer.hi.x, inner.hi.x + REAL_TO_INT_CEIL( br->getImageWidth() * k ) );
	outer.hi.y = max( outer.hi.y, inner.hi.y + REAL_TO_INT_CEIL( br->getImageHeight() * k ) );
	drawNinePiece( pieces, outer, inner );
	return TRUE;
}

//-----------------------------------------------------------------------------
/** The radar's frame, nine pieces round wherever ControlBar::fitRadarToMap stood the radar: the
	* corners at their size, the edges stretched along the radar and the centre under it, which is
	* what shows while the player has no radar.  Drawn after the painting and before the windows. */
//-----------------------------------------------------------------------------
static void drawRadarFrame( const Image *const *frame )
{
	static NameKeyType radarID = NAMEKEY( "ControlBar.wnd:LeftHUD" );
	GameWindow *radar = TheWindowManager->winGetWindowFromId( NULL, radarID );
	if( radar == NULL || radar->winIsHidden() )
		return;
	IRegion2D inner, outer;
	radar->winGetScreenPosition( &inner.lo.x, &inner.lo.y );
	radar->winGetSize( &inner.hi.x, &inner.hi.y );
	inner.hi.x += inner.lo.x;
	inner.hi.y += inner.lo.y;
	ControlBarSchemeManager::radarFrameRect( frame, inner, &outer );
	drawNinePiece( frame, outer, inner );
}

//-----------------------------------------------------------------------------
/** A filled band, skipped when empty */
//-----------------------------------------------------------------------------
static void fillBand( Int x0, Int y0, Int x1, Int y1, Color color )
{
	if( x1 > x0 && y1 > y0 )
		TheDisplay->drawFillRect( x0, y0, x1 - x0, y1 - y0, color );
}

/// a piece's size on screen: its texels times the art's scale, rounded to whole pixels
static Int grownSize( Int texels, Real scale )
{
	return MAX( 1, REAL_TO_INT_FLOOR( texels * scale + 0.5f ) );
}

/// the run x0..x1, y0..y1 tiled from its top left by the piece at its scaled size, never stretched; the
/// last tile in a row or a column is cut, keeping its top left (grown_compose's tile)
static void grownTile( const Image *piece, Real scale, Int x0, Int y0, Int x1, Int y1, Color tint )
{
	if( x1 <= x0 || y1 <= y0 )
		return;
	const Int w = grownSize( piece->getImageWidth(), scale ), h = grownSize( piece->getImageHeight(), scale );
	IRegion2D run;
	run.lo.x = x0;
	run.lo.y = y0;
	run.hi.x = x1;
	run.hi.y = y1;
	TheDisplay->setClipRegion( &run );
	TheDisplay->enableClipping( TRUE );
	for( Int y = y0; y < y1; y += h )
		for( Int x = x0; x < x1; x += w )
			TheDisplay->drawImage( piece, x, y, x + w, y + h, tint );
	TheDisplay->enableClipping( FALSE );
}

static void grownCorner( const Image *piece, Real scale, Int x, Int y, Color tint )
{
	TheDisplay->drawImage( piece, x, y, x + grownSize( piece->getImageWidth(), scale ),
		y + grownSize( piece->getImageHeight(), scale ), tint );
}

//-----------------------------------------------------------------------------
/** The side's own frame round the glass well 'well' (the grown picture joined with the painted
	* glass), as hud_command_bar.py's grown_compose lays it: every number below is read off a piece's
	* size, scaled with the art and rounded to whole pixels.
	*
	*   head = T's height, R = R's width, cut = TR's width less R, l0 = TL's height less head,
	*   r0 = TR's height less head, foot = BR's height less B's height
	*
	* The four edges tiled, then the four corners over them, then the glass C over the well; the
	* picture is the radar window's, drawn after.  While the under-attack lamp is lit the frame's pieces
	* take its red, the glass and the picture none. */
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::drawGrownRadarPieces( const Image *const *P, Real s, const IRegion2D &well, Bool lit )
{
	const Int x5 = well.lo.x, yT = well.lo.y, xR = well.hi.x, yB = well.hi.y;
	const Int head = grownSize( P[ ControlBarScheme::GROWN_T ]->getImageHeight(), s );
	const Int R = grownSize( P[ ControlBarScheme::GROWN_R ]->getImageWidth(), s );
	const Int cut = grownSize( P[ ControlBarScheme::GROWN_TR ]->getImageWidth(), s ) - R;
	const Int l0 = grownSize( P[ ControlBarScheme::GROWN_TL ]->getImageHeight(), s ) - head;
	const Int r0 = grownSize( P[ ControlBarScheme::GROWN_TR ]->getImageHeight(), s ) - head;
	const Int bH = grownSize( P[ ControlBarScheme::GROWN_B ]->getImageHeight(), s );
	const Int foot = grownSize( P[ ControlBarScheme::GROWN_BR ]->getImageHeight(), s ) - bH;
	const Int top = yT - head;
	const Color tint = lit ? GameMakeColor( 255, 110, 100, 255 ) : 0xFFFFFFFF;

	grownTile( P[ ControlBarScheme::GROWN_T ], s, grownSize( P[ ControlBarScheme::GROWN_TL ]->getImageWidth(), s ), top, xR - cut, yT, tint );
	grownTile( P[ ControlBarScheme::GROWN_L ], s, 0, yT + l0, x5, yB - foot, tint );
	grownTile( P[ ControlBarScheme::GROWN_R ], s, xR, yT + r0, xR + R, yB - foot, tint );
	grownTile( P[ ControlBarScheme::GROWN_B ], s, x5, yB, xR - cut, yB + bH, tint );
	grownCorner( P[ ControlBarScheme::GROWN_TL ], s, 0, top, tint );
	grownCorner( P[ ControlBarScheme::GROWN_TR ], s, xR - cut, top, tint );
	grownCorner( P[ ControlBarScheme::GROWN_BL ], s, 0, yB - foot, tint );
	grownCorner( P[ ControlBarScheme::GROWN_BR ], s, xR - cut, yB - foot, tint );
	grownTile( P[ ControlBarScheme::GROWN_C ], s, x5, yT, xR, yB, 0xFFFFFFFF );
}

//-----------------------------------------------------------------------------
/** The steel round a side's radar that Minimap Size has grown out of the painting's box, drawn in
	* code rather than art so every side gets it: a dark outer edge, a steel face lit from the top
	* left and shaded to the bottom right, and a dark lip against the picture.  It flashes red with
	* EA's under-attack lamp, whose own window is small beside a frame this size.  Drawn after the
	* painting and before the windows, as the observer's nine-piece frame is. */
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::drawGrownRadarFrame( void )
{
	IRegion2D o, i;
	Bool lit = FALSE;
	if( TheControlBar == NULL || !TheControlBar->getGrownRadarFrame( &o, &i, &lit ) )
		return;

	// the side's own frame where its scheme names one
	const ControlBarSchemeManager *schemes = TheControlBar->getControlBarSchemeManager();
	const Image *const *pieces = schemes ? schemes->getGrownRadarPieces() : NULL;
	if( pieces != NULL )
	{
		drawGrownRadarPieces( pieces, schemes->getArtScale(), i, lit );
		return;
	}

	const Color dark = GameMakeColor( 10, 12, 14, 255 );
	const Color face = lit ? GameMakeColor( 150, 34, 26, 255 ) : GameMakeColor( 62, 68, 74, 255 );
	const Color light = lit ? GameMakeColor( 230, 80, 60, 255 ) : GameMakeColor( 132, 140, 148, 255 );
	const Color shade = lit ? GameMakeColor( 90, 18, 14, 255 ) : GameMakeColor( 34, 38, 42, 255 );

	// dark glass under the picture, which is all that shows while the player has no radar
	fillBand( i.lo.x, i.lo.y, i.hi.x, i.hi.y, GameMakeColor( 6, 8, 10, 255 ) );

	// the face, four bands round the picture
	fillBand( o.lo.x, o.lo.y, o.hi.x, i.lo.y, face );
	fillBand( o.lo.x, i.hi.y, o.hi.x, o.hi.y, face );
	fillBand( o.lo.x, i.lo.y, i.lo.x, i.hi.y, face );
	fillBand( i.hi.x, i.lo.y, o.hi.x, i.hi.y, face );

	// lit along the top and left of the outside, shaded along the bottom and right
	fillBand( o.lo.x + 1, o.lo.y + 1, o.hi.x - 1, o.lo.y + 2, light );
	fillBand( o.lo.x + 1, o.lo.y + 1, o.lo.x + 2, o.hi.y - 1, light );
	fillBand( o.lo.x + 1, o.hi.y - 2, o.hi.x - 1, o.hi.y - 1, shade );
	fillBand( o.hi.x - 2, o.lo.y + 1, o.hi.x - 1, o.hi.y - 1, shade );

	// the inside of the bevel is turned the other way, shaded top and left where it meets the glass
	fillBand( i.lo.x - 1, i.lo.y - 1, i.hi.x + 1, i.lo.y, shade );
	fillBand( i.lo.x - 1, i.lo.y - 1, i.lo.x, i.hi.y + 1, shade );
	fillBand( i.lo.x - 1, i.hi.y, i.hi.x + 1, i.hi.y + 1, light );
	fillBand( i.hi.x, i.lo.y - 1, i.hi.x + 1, i.hi.y + 1, light );

	// a dark line outside, and a dark lip in the window's own edge, the pixel W3DLeftHUDDraw leaves
	fillBand( o.lo.x, o.lo.y, o.hi.x, o.lo.y + 1, dark );
	fillBand( o.lo.x, o.hi.y - 1, o.hi.x, o.hi.y, dark );
	fillBand( o.lo.x, o.lo.y, o.lo.x + 1, o.hi.y, dark );
	fillBand( o.hi.x - 1, o.lo.y, o.hi.x, o.hi.y, dark );
	fillBand( i.lo.x, i.lo.y, i.hi.x, i.lo.y + 1, dark );
	fillBand( i.lo.x, i.hi.y - 1, i.hi.x, i.hi.y, dark );
	fillBand( i.lo.x, i.lo.y, i.lo.x + 1, i.hi.y, dark );
	fillBand( i.hi.x - 1, i.lo.y, i.hi.x, i.hi.y, dark );
}

//-----------------------------------------------------------------------------
/** ControlBar Pro's painting, cut at the seams below and each piece drawn where its panel stands
	* (ControlBarPanelDesignToScreen).  The seams fall in the gaps of every side's art. */
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::drawPainting( ICoord2D offset, Bool foreground )
{
	// where the left and centre, and the centre and right panels meet, in 800x600 design units
	static const Int seams[ ControlBar::CB_PANEL_COUNT + 1 ] = { 0, 182, 616, 800 };

	const Real toDesignX = 800.0f / m_currentScheme->m_ScreenCreationRes.x;
	const Real toDesignY = 600.0f / m_currentScheme->m_ScreenCreationRes.y;
	const Int first = foreground ? 0 : CONTROL_BAR_SCHEME_FOREGROUND_IMAGE_LAYERS;
	const Int last = foreground ? CONTROL_BAR_SCHEME_FOREGROUND_IMAGE_LAYERS : MAX_CONTROL_BAR_SCHEME_IMAGE_LAYERS;
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	const ICoord2D barOffset = offset;

	for( Int p = 0; p < ControlBar::CB_PANEL_COUNT; p++ )
	{
		// the left piece holds only the observer's column, which stands against the radar's frame
		// (ControlBar::fitRadarToMap); a side's left piece is empty and its shift is 0
		offset.x = barOffset.x + ( p == ControlBar::CB_PANEL_LEFT ? TheControlBar->getRadarColumnShift() : 0 );
		IRegion2D band, clip;
		band.lo.x = seams[ p ];
		band.hi.x = seams[ p + 1 ];
		band.lo.y = 0;
		band.hi.y = 600;
		ControlBarPanelDesignToScreen( p, &band, width, height, &clip );
		clip.lo.x += offset.x;
		clip.hi.x += offset.x;
		clip.lo.y += offset.y;
		clip.hi.y += offset.y;
		TheDisplay->setClipRegion( &clip );
		TheDisplay->enableClipping( TRUE );

		for( Int layer = last - 1; layer >= first; layer-- )
		{
			for( ControlBarScheme::ControlBarSchemeImageList::const_iterator it = m_currentScheme->m_layer[ layer ].begin();
					 it != m_currentScheme->m_layer[ layer ].end(); ++it )
			{
				const ControlBarSchemeImage *schemeImage = *it;
				if( schemeImage->m_image == NULL )
					continue;
				IRegion2D design, rect;
				design.lo.x = REAL_TO_INT_FLOOR( schemeImage->m_position.x * toDesignX );
				design.lo.y = REAL_TO_INT_FLOOR( schemeImage->m_position.y * toDesignY );
				design.hi.x = REAL_TO_INT_CEIL( ( schemeImage->m_position.x + schemeImage->m_size.x ) * toDesignX );
				design.hi.y = REAL_TO_INT_CEIL( ( schemeImage->m_position.y + schemeImage->m_size.y ) * toDesignY );
				ControlBarPanelDesignToScreen( p, &design, width, height, &rect );
				TheDisplay->drawImage( schemeImage->m_image, rect.lo.x + offset.x, rect.lo.y + offset.y,
															 rect.hi.x + offset.x, rect.hi.y + offset.y );
			}
		}
		TheDisplay->enableClipping( FALSE );
	}

	// the radar window travels with the bar, so its frame needs no offset
	const Image *const *frame = getRadarFrame();
	if( !foreground && frame )
		drawRadarFrame( frame );
	else if( !foreground )
		drawGrownRadarFrame();
}

//-----------------------------------------------------------------------------
void ControlBarSchemeManager::setControlBarSchemeByPlayerTemplate( const PlayerTemplate *pt, Bool useSmall)
{
	if(!pt)
		return;
	AsciiString side = pt->getSide();

	// if we don't have a side, set it to Observer shell
	if(side.isEmpty())
		side.set("Observer");
	// picked again every time, even for the side on screen: a new resolution can want a different
	// scheme for it (findSchemeForSide), and the multiplier below is the screen's
	// the squished bar keeps EA's pick: the side's Small scheme, and none ships, so "Default"
	AsciiString smallSide = side;
	smallSide.concat("Small");
	// a side with no bar of its own wears its country's or America's (findSchemeForFaction)
	ControlBarScheme *tempScheme = useSmall ? findSchemeForSide( smallSide ) : findSchemeForFaction( side );
	if(tempScheme)
	{
		DEBUG_LOG(("setControlBarSchemeByPlayer gave side %s the scheme of %s\n", side.str(), tempScheme->m_side.str()));
		m_multiplyer.x = TheDisplay->getWidth() / (Real)tempScheme->m_ScreenCreationRes.x;
		m_multiplyer.y = TheDisplay->getHeight() / (Real)tempScheme->m_ScreenCreationRes.y;
		m_currentScheme = tempScheme;
	}
	else
		m_currentScheme = findControlBarScheme("Default");
	m_currentScheme->init();
}
//-----------------------------------------------------------------------------
void ControlBarSchemeManager::setControlBarSchemeByPlayer(Player *p)
{
	GameWindow *communicatorButton = TheWindowManager->winGetWindowFromId( NULL, NAMEKEY("ControlBar.wnd:PopupCommunicator") );
	if (communicatorButton && TheControlBar)
	{
		if (TheRecorder->isMultiplayer())
			TheControlBar->setControlCommand(communicatorButton, TheControlBar->findCommandButton("NonCommand_Communicator") );
		else
			TheControlBar->setControlCommand(communicatorButton, TheControlBar->findCommandButton("NonCommand_BriefingHistory") );
	}

	if(!p)
		return;
	AsciiString side = p->getSide();

	// if we don't have a side, set it to Observer shell
	if(side.isEmpty())
		side.set("Observer");
	// picked again every time, even for the side on screen: a new resolution can want a different
	// scheme for it (findSchemeForSide), and the multiplier below is the screen's
	// the neutral and civilian players are of no country, and wear America's
	m_currentScheme = findSchemeForFaction( side );
	DEBUG_LOG(("setControlBarSchemeByPlayer gave side %s the scheme of %s\n", side.str(), m_currentScheme->m_side.str()));
	m_multiplyer.x = TheDisplay->getWidth() / (Real)m_currentScheme->m_ScreenCreationRes.x;
	m_multiplyer.y = TheDisplay->getHeight() / (Real)m_currentScheme->m_ScreenCreationRes.y;
	m_currentScheme->init();
}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
static void animSlideRight( ControlBarSchemeAnimation *anim )
{
	if(!anim->m_animImage || (anim->m_animDuration == 0))
		return;

	UnsignedInt currentFrame = anim->getCurrentFrame();
	ICoord2D startPos = anim->getStartPos();
	// if we're at the end, bring us to the beginning
	if(currentFrame == anim->m_animDuration)
	{
		anim->m_animImage->m_position.x = startPos.x;
		anim->m_animImage->m_position.y = startPos.y;
		anim->setCurrentFrame( 0 );
		return;
	}
	else if(currentFrame == 0)
	{
		// if we're at the beginning, save off the start position
		startPos.x = anim->m_animImage->m_position.x;
		startPos.y = anim->m_animImage->m_position.y;
		anim->setStartPos(startPos);
	}
	
	// now lets animate this bad boy!
	
	// now increment the frame
	currentFrame++;
	anim->setCurrentFrame(currentFrame);

	// now lets find what position we should be at.
	anim->m_animImage->m_position.x = startPos.x + (((anim->m_finalPos.x - startPos.x) * currentFrame) / anim->m_animDuration);


}

