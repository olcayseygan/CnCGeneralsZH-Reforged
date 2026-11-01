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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameAudio.cpp
//
// Created:   5/01/01
//
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/LogicThread.h"
#include "Common/GameAudio.h"

#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameMusic.h"
#include "GameLogic/LogicRandomValue.h"

#include <algorithm>
#include <array>
#include "Common/GameSounds.h"
#include "Common/GlobalData.h"
#include "Common/MiscAudio.h"
#include "Common/OSDisplay.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/UserPreferences.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include "WWMath/matrix3d.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _INTERNAL
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static const char* TheSpeakerTypes[] =
{
	"2 Speakers",
	"Headphones",
	"Surround Sound",
	"4 Speaker",
	"5.1 Surround",
	"7.1 Surround",
	NULL
};

static const Int TheSpeakerTypesCount = sizeof(TheSpeakerTypes) / sizeof(TheSpeakerTypes[0]);

static void parseSpeakerType( INI *ini, void *instance, void *store, const void *userData );

// Field Parse table for Audio Settings ///////////////////////////////////////////////////////////
static const FieldParse audioSettingsFieldParseTable[] =
{
	{ "AudioRoot",						INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_audioRoot) },
	{ "SoundsFolder",					INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_soundsFolder) },
	{ "MusicFolder",					INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_musicFolder) },
	{ "StreamingFolder",			INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_streamingFolder) },
	{ "SoundsExtension",			INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_soundsExtension) },

	{ "UseDigital",						INI::parseBool,											NULL,							offsetof( AudioSettings, m_useDigital) },
	{ "UseMidi",							INI::parseBool,											NULL,							offsetof( AudioSettings, m_useMidi) },
	{ "OutputRate",						INI::parseInt,											NULL,							offsetof( AudioSettings, m_outputRate) },
	{ "OutputBits",						INI::parseInt,											NULL,							offsetof( AudioSettings, m_outputBits) },
	{ "OutputChannels",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_outputChannels) },
	{ "SampleCount2D",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_sampleCount2D) },
	{ "SampleCount3D",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_sampleCount3D) },
	{ "StreamCount",					INI::parseInt,											NULL,							offsetof( AudioSettings, m_streamCount) },

	{ "Preferred3DHW1",				INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_preferred3DProvider[0]) },
	{ "Preferred3DHW2",				INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_preferred3DProvider[1]) },
	{ "Preferred3DHW3",				INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_preferred3DProvider[2]) },
	{ "Preferred3DHW4",				INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_preferred3DProvider[3]) },

	{ "Preferred3DSW",				INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_preferred3DProvider[4]) },

	{ "Default2DSpeakerType",		 parseSpeakerType,							NULL,							offsetof( AudioSettings, m_defaultSpeakerType2D) },
	{ "Default3DSpeakerType",		 parseSpeakerType,							NULL,							offsetof( AudioSettings, m_defaultSpeakerType3D) },

	{ "MinSampleVolume",			INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_minVolume) },
	{ "RangeVolumeFade",			INI::parseBool,											NULL,							offsetof( AudioSettings, m_rangeVolumeFade) },
	{ "SpeedOfSound",					INI::parseReal,											NULL,							offsetof( AudioSettings, m_speedOfSound) },
	{ "DopplerScale",					INI::parseReal,											NULL,							offsetof( AudioSettings, m_dopplerScale) },
	{ "DopplerFastSpeed",			INI::parseReal,											NULL,							offsetof( AudioSettings, m_dopplerFastSpeed) },
	{ "ReplyUnitCooldown",		INI::parseReal,											NULL,							offsetof( AudioSettings, m_replyUnitCooldown) },
	{ "ReplyGap",							INI::parseReal,											NULL,							offsetof( AudioSettings, m_replyGap) },
	{ "ReplyMaxVoices",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_replyMaxVoices) },
	{ "BattleVoiceBudget",		INI::parseInt,											NULL,							offsetof( AudioSettings, m_battleVoiceBudget) },
	{ "CoalesceRadius",				INI::parseReal,											NULL,							offsetof( AudioSettings, m_coalesceRadius) },
	{ "CoalesceFrames",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_coalesceFrames) },
	{ "CoalesceGainStep",			INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_coalesceGainStep) },
	{ "CoalesceMaxGain",			INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_coalesceMaxGain) },
	{ "ZoomMixNearHeight",		INI::parseReal,											NULL,							offsetof( AudioSettings, m_zoomMixNearHeight) },
	{ "ZoomMixFarHeight",			INI::parseReal,											NULL,							offsetof( AudioSettings, m_zoomMixFarHeight) },
	{ "ZoomMixDetailVolume",	INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_zoomMixDetailVolume) },
	{ "AmbienceBedNearVolume",	INI::parsePercentToReal,					NULL,							offsetof( AudioSettings, m_ambienceBedNearVolume) },
	{ "AmbienceBedFarVolume",	INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_ambienceBedFarVolume) },
	{ "AmbienceBedSnow",			INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_ambienceBedSnow) },
	{ "AmbienceBedTown",			INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_ambienceBedTown) },
	{ "AmbienceBedDesert",		INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_ambienceBedDesert) },
	{ "AmbienceBedField",			INI::parseAsciiString,							NULL,							offsetof( AudioSettings, m_ambienceBedField) },
	{ "GlobalMinRange",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_globalMinRange) },
	{ "GlobalMaxRange",				INI::parseInt,											NULL,							offsetof( AudioSettings, m_globalMaxRange) },
	{ "TimeBetweenDrawableSounds", INI::parseDurationUnsignedInt, NULL,							offsetof( AudioSettings, m_drawableAmbientFrames) },
	{ "TimeToFadeAudio",			INI::parseDurationUnsignedInt,			NULL,							offsetof( AudioSettings, m_fadeAudioFrames) },
	{ "AudioFootprintInBytes",INI::parseUnsignedInt,							NULL,							offsetof( AudioSettings, m_maxCacheSize) },
	{ "Relative2DVolume",			INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_relative2DVolume ) },
	{ "DefaultSoundVolume",		INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_defaultSoundVolume) },
	{ "Default3DSoundVolume",	INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_default3DSoundVolume) },
	{ "DefaultSpeechVolume",	INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_defaultSpeechVolume) },
	{ "DefaultMusicVolume",		INI::parsePercentToReal,						NULL,							offsetof( AudioSettings, m_defaultMusicVolume) },
	{ "MicrophoneDesiredHeightAboveTerrain",	INI::parseReal,			NULL,							offsetof( AudioSettings, m_microphoneDesiredHeightAboveTerrain ) },
	{ "MicrophoneMaxPercentageBetweenGroundAndCamera", INI::parsePercentToReal,	NULL,	offsetof( AudioSettings, m_microphoneMaxPercentageBetweenGroundAndCamera ) },
  { "ZoomMinDistance",		INI::parseReal,									NULL,							offsetof( AudioSettings, m_zoomMinDistance ) },
  { "ZoomMaxDistance",		INI::parseReal,									NULL,							offsetof( AudioSettings, m_zoomMaxDistance ) },
  { "ZoomSoundVolumePercentageAmount",		INI::parsePercentToReal,	NULL,		offsetof( AudioSettings, m_zoomSoundVolumePercentageAmount ) },

	{ NULL, NULL, NULL, NULL }
};

// Singleton TheAudio /////////////////////////////////////////////////////////////////////////////
AudioManager *TheAudio = NULL;


// AudioManager Device Independent functions //////////////////////////////////////////////////////
AudioManager::AudioManager() : 
	m_soundOn(TRUE), 
	m_sound3DOn(TRUE), 
	m_musicOn(TRUE), 
	m_speechOn(TRUE), 
	m_music(NULL), 
	m_sound(NULL),
	m_surroundSpeakers(FALSE),
	m_hardwareAccel(FALSE),
	m_musicPlayingFromCD(FALSE)
{
	// Added by Sadullah Nader
	m_adjustedVolumes.clear();
	m_audioRequests.clear();
	m_listenerPosition.zero();
	m_musicTracks.clear();
	m_musicVolume = 0.0f;
	m_sound3DVolume = 0.0f;	
	m_soundVolume   = 0.0f;
	m_speechVolume  = 0.0f;
	m_systemMusicVolume   = 0.0f;
	m_systemSound3DVolume = 0.0f;
	m_systemSoundVolume   = 0.0f; 
	m_systemSpeechVolume  = 0.0f;
	m_systemAmbientVolume = 0.0f;
	m_systemMasterVolume = 1.0f;
	m_zoomVolume = 1.0f;
	m_zoomOut = 0.0f;
	m_zoomDetailVolume = 1.0f;
	m_rangeScale = 1.0f;
	m_volumeHasChanged			= FALSE;
	//

	m_listenerOrientation.set(0.0, 1.0, 0.0);
	theAudioHandlePool = AHSV_FirstHandle;
	m_audioSettings = NEW AudioSettings;
	m_miscAudio = NEW MiscAudio;
	m_silentAudioEvent = NEW AudioEventRTS;
	m_savedValues = NULL;
	m_disallowSpeech = FALSE;
}

//-------------------------------------------------------------------------------------------------
AudioManager::~AudioManager()
{
	// cleanup all of the loaded AudioEventInfos
	AudioEventInfoHashIt it;
	for (it = m_allAudioEventInfo.begin(); it != m_allAudioEventInfo.end(); ++it) {
		AudioEventInfo *eventInfo = (*it).second;
		if (eventInfo) {
			eventInfo->deleteInstance();
			eventInfo = NULL;
		}
	}

	delete m_silentAudioEvent;
	m_silentAudioEvent = NULL;
	
	delete m_music;
	m_music = NULL;
	
	delete m_sound;
	m_sound = NULL;
	
	delete m_miscAudio;
	m_miscAudio = NULL;

	delete m_audioSettings;
	m_audioSettings = NULL;

	if (m_savedValues) 
		delete [] m_savedValues;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::init()
{
	INI ini;
	ini.load( AsciiString( "Data\\INI\\AudioSettings.ini" ), INI_LOAD_OVERWRITE, NULL);

	ini.load( AsciiString( "Data\\INI\\Default\\Music.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\Music.ini" ), INI_LOAD_OVERWRITE, NULL );

	ini.load( AsciiString( "Data\\INI\\Default\\SoundEffects.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\SoundEffects.ini" ), INI_LOAD_OVERWRITE, NULL );

	ini.load( AsciiString( "Data\\INI\\Default\\Speech.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\Speech.ini" ), INI_LOAD_OVERWRITE, NULL );

	ini.load( AsciiString( "Data\\INI\\Default\\Voice.ini" ), INI_LOAD_OVERWRITE, NULL );
	ini.load( AsciiString( "Data\\INI\\Voice.ini" ), INI_LOAD_OVERWRITE, NULL );

	// the original Generals campaigns' briefings and mission dialogue, which Zero Hour's files left out
	ini.load( AsciiString( "Data\\INI\\SpeechReforged.ini" ), INI_LOAD_OVERWRITE, NULL );

	// -modfactions: each mod's own events under "M<n>~" names; AudioSettings and MiscAudio stay ours
	INI::loadModPasses( "Data\\INI\\Music.ini", NULL );
	INI::loadModPasses( "Data\\INI\\SoundEffects.ini", NULL );
	INI::loadModPasses( "Data\\INI\\Speech.ini", NULL );
	INI::loadModPasses( "Data\\INI\\Voice.ini", NULL );

	// do the miscellaneous sound files last so that we find the audioeventrts associated with the events.
	ini.load( AsciiString( "Data\\INI\\MiscAudio.ini" ), INI_LOAD_OVERWRITE, NULL);
	
	//
	// One music track existing implies all, since they are BIGd, and when none does the retail
	// disc may still have them: the music shipped on CD 1.  So the disc is still read.
	//
	// What is gone is the loop that used to sit around this read.  A system-modal "Please insert
	// the first game CD" came up over the splash screen, OK asked again forever and only Cancel
	// let the game start - and no digital install can ever satisfy it, because there is no disc to
	// insert and the missing music is the base game's Music.big, not a CD.  Music is not something
	// the game needs to run: the audio manager plays nothing when it has nothing, which is the call
	// GameEngine::init already makes two lines after this returns.
	//
	if (!isMusicAlreadyLoaded())
	{
		// @todo Unload any files from CD first. - jkmcd
		TheFileSystem->loadMusicFilesFromCD();
		m_musicPlayingFromCD = isMusicAlreadyLoaded();
	}
	
	m_music = NEW MusicManager;
	m_sound = NEW SoundManager;

	// Set our system volumes from the user's preferred settings, not the defaults.
	m_systemMusicVolume = getAudioSettings() ? getAudioSettings()->m_preferredMusicVolume : 0.55f;
	m_systemSoundVolume = getAudioSettings() ? getAudioSettings()->m_preferredSoundVolume : 0.75f;
	m_systemSound3DVolume = getAudioSettings() ? getAudioSettings()->m_preferred3DSoundVolume: 0.75f;
	m_systemSpeechVolume = getAudioSettings() ? getAudioSettings()->m_preferredSpeechVolume : 0.55f;
	m_systemAmbientVolume = TheGlobalData->m_ambientVolume / 100.0f;	// the catalog loaded it from Options.ini
	m_systemMasterVolume = TheGlobalData->m_masterVolume / 100.0f;

	m_scriptMusicVolume = 1.0f;
	m_scriptSoundVolume = 1.0f;
	m_scriptSound3DVolume = 1.0f;
	m_scriptSpeechVolume = 1.0f;

	recomputeVolumes();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::postProcessLoad()
{

}

//-------------------------------------------------------------------------------------------------
void AudioManager::reset()
{
	// Sounds, dialogue and music a map.ini redefined go back to the game's own, in place, because
	// every AudioEventRTS that played one holds the pointer; backwards, so map.ini then solo.ini on
	// the same event ends at the first.  A voice line's length is what a script waiting on it counts,
	// so this is not only what the next match sounds like.  An event the map added stays: the next
	// definition starts again from the default event, as a new one would.
	for (Int i = (Int)m_beforeMapOverrides.size() - 1; i >= 0; --i)
	{
		*m_beforeMapOverrides[i].first = *m_beforeMapOverrides[i].second;
		m_beforeMapOverrides[i].second->deleteInstance();
	}
	m_beforeMapOverrides.clear();

	// clear out any adjusted volumes we might have set.
	m_adjustedVolumes.clear();

	// adjust the scripted volumes, and reset the 
	m_scriptMusicVolume = 1.0f;
	m_scriptSoundVolume = 1.0f;
	m_scriptSound3DVolume = 1.0f;
	m_scriptSpeechVolume = 1.0f;

	recomputeVolumes();

	m_disallowSpeech = FALSE;
	m_handlesQueuedFromTick.clear();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::update()
{
	Coord3D groundPos, microphonePos;
	TheTacticalView->getPosition( &groundPos );
	Real angle = TheTacticalView->getAngle();
	Matrix3D rot = Matrix3D::Identity;
	rot.Rotate_Z( angle );
	Vector3 forward( 0, 1, 0 );
	rot.mulVector3( forward );

	Real desiredHeight = TheAudio->getAudioSettings()->m_microphoneDesiredHeightAboveTerrain;
	Real maxPercentage = TheAudio->getAudioSettings()->m_microphoneMaxPercentageBetweenGroundAndCamera;

	Coord3D lookTo;
	lookTo.set(forward.X, forward.Y, forward.Z);

	//Kris: At this point, the microphone is calculated to be at the ground position where the camera is looking at.
	//Instead we want to move the microphone towards the camera. Hopefully, it'll be a desired altitude, but if it
	//gets too close to the camera (or even past it), that would be undesirable. Therefore, we have a backup method
	//of making sure we only go a certain percentage towards the camera or the desired height, whichever occurs first.
	Coord3D cameraPos = TheTacticalView->get3DCameraPosition();
	Coord3D groundToCameraVector;
	groundToCameraVector.set( &cameraPos );
	groundToCameraVector.sub( &groundPos );
	Real bestScaleFactor;

	if( cameraPos.z <= desiredHeight || groundToCameraVector.z <= 0.0f )
	{
		//Use the percentage calculation!
		bestScaleFactor = maxPercentage;
	}
	else
	{
		//Calculate the stopping position of the groundToCameraVector when we force z to be m_microphoneDesiredHeightAboveTerrain
		Real zScale = desiredHeight / groundToCameraVector.z;

		//Use the smallest of the two scale calculations
		bestScaleFactor = MIN( maxPercentage, zScale );
	}

	//Now apply the best scalar to the ground-to-camera vector.
	groundToCameraVector.scale( bestScaleFactor );

	//Set the microphone to be the ground position adjusted for terrain plus the vector we just calculated.
	groundPos.z = TheTerrainLogic->getGroundHeight( groundPos.x, groundPos.y );
	microphonePos.set( &groundPos );
	microphonePos.add( &groundToCameraVector );

	//Viola! A properly placed microphone.
	setListenerPosition( &microphonePos, &lookTo );


	//Now determine if we would like to boost the volume based on the camera being close to the microphone!
	Real maxBoostScalar = TheAudio->getAudioSettings()->m_zoomSoundVolumePercentageAmount;
	Real minDist = TheAudio->getAudioSettings()->m_zoomMinDistance;
	Real maxDist = TheAudio->getAudioSettings()->m_zoomMaxDistance;

	//We can't boost a sound above 100%, instead reduce the normal sound level.
	m_zoomVolume = 1.0f - maxBoostScalar;

	//Are we even using a boost?
	if( maxBoostScalar > 0.0f )
	{
		//How far away is the camera from the microphone?
		Coord3D vector = cameraPos;
		vector.sub( &microphonePos );
		Real dist = vector.length();

		if( dist < minDist )
		{
			//Max volume!
			m_zoomVolume = 1.0f;
		}
		else if( dist < maxDist )
		{
			//Determine what the boost amount will be. 
			Real scalar = (dist - minDist) / (maxDist - minDist);
			m_zoomVolume = 1.0f - scalar * maxBoostScalar;
		}
	}

	// Zoomed out over a battle, every gun at full detail is a wall of noise; the detail steps back and
	// the ambience, which keeps its level, comes forward (ZoomMix* in AudioSettings.h).
	const AudioSettings *settings = TheAudio->getAudioSettings();
	const Real span = settings->m_zoomMixFarHeight - settings->m_zoomMixNearHeight;
	const Real height = cameraPos.z - groundPos.z;
	m_zoomOut = span > 0.0f ? (height - settings->m_zoomMixNearHeight) / span : (height > settings->m_zoomMixNearHeight ? 1.0f : 0.0f);
	m_zoomOut = MIN( 1.0f, MAX( 0.0f, m_zoomOut ) );
	m_zoomDetailVolume = 1.0f - (1.0f - settings->m_zoomMixDetailVolume) * m_zoomOut;

	// The sounds' ranges were made for EA's zoom, MaxCameraHeight; the wheel goes nearly three times
	// further out, where the side of the screen stood 700 units from the microphone and its top 1000,
	// past nearly every sound's MaxRange of 800, so only the middle of the screen was heard.  Past
	// MaxCameraHeight the ranges grow with the zoom, and the screen falls off from its middle to its
	// edges as it does at EA's zoom.  Legacy sound keeps retail's ranges.
	m_rangeScale = TheGlobalData->m_legacySound ? 1.0f
		: MAX( 1.0f, TheTacticalView->getHeightAboveGround() / TheGlobalData->m_maxCameraHeight );

	set3DVolumeAdjustment( m_zoomVolume );

	m_music->update();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::getInfoForAudioEvent( const AudioEventRTS *eventToFindAndFill ) const
{
	if (!eventToFindAndFill) {
		return;
	}

	if (eventToFindAndFill->getAudioEventInfo()) {
		// already done
		return;
	}

	eventToFindAndFill->setAudioEventInfo(findAudioEventInfo(eventToFindAndFill->getEventName()));
}

//-------------------------------------------------------------------------------------------------
AudioHandle AudioManager::addAudioEvent(const AudioEventRTS *eventToAdd)
{
	if (eventToAdd->getEventName().isEmpty() || eventToAdd->getEventName() == AsciiString("NoSound")) {
		return AHSV_NoSound;
	}

	if (!LogicThread::defersEffects()) {
		return addAudioEventNow(eventToAdd, 0);
	}

	/* LOGIC-THREAD.md phase 2, M5.  Inside a tick the sound gets its handle here, so logic can keep it
		 and stop the sound later, and everything else waits for the end of the tick: the options, the
		 local player, the request list and the audio stream's draws, which then come in the order the
		 tick played its sounds.  The handle is minted whether or not this machine will play the sound;
		 which handles exist was never the same on two machines, so nothing logic keeps depends on it. */
	if (!eventToAdd->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToAdd);
		if (!eventToAdd->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for requested audio event '%s'\n", eventToAdd->getEventName().str()));
			return AHSV_Error;
		}
	}

	const AudioHandle handle = allocateNewHandle();
	m_handlesQueuedFromTick.push_back(handle);
	AudioEventRTS copy(*eventToAdd);

	// A list played in order steps on from the index the caller's event holds; the build writes the
	// step into its own copy, so the caller's event takes it here.  A random list remembers its last
	// pick to avoid playing it twice running, and that pick is made after the tick: the caller is held
	// until then, so the pick reads its memory as it stands and leaves the new pick in it.
	const AudioEventInfo *info = eventToAdd->getAudioEventInfo();
	AudioEventRTS *caller = const_cast<AudioEventRTS*>(eventToAdd);
	Int callerSlot = -1;
	if (info->m_soundType != AT_Music && info->m_soundType != AT_Streaming && !info->m_sounds.empty()) {
		if (BitTest(info->m_control, AC_RANDOM)) {
			callerSlot = AudioEventRTS::holdCallerForPick(caller);
		} else {
			caller->setPlayingAudioIndex(caller->getPlayingAudioIndex() + 1);
		}
	}

	// A scripted sound picks its file from the logic seed without moving it; pin the seed it would
	// have read at this call.
	const Bool logical = eventToAdd->getIsLogicalAudio();
	std::array<UnsignedInt, 6> seed = {};
	if (logical) {
		CopyGameLogicSeed(seed.data());
	}

	LogicThread::callAfterTick([this, copy, handle, logical, seed, callerSlot]() mutable {
		std::vector<AudioHandle>::iterator queued = std::find(m_handlesQueuedFromTick.begin(), m_handlesQueuedFromTick.end(), handle);
		if (queued != m_handlesQueuedFromTick.end()) {
			m_handlesQueuedFromTick.erase(queued);
		}
		if (logical) {
			PinGameLogicSeedForUnchanged(seed.data());
		}
		AudioEventRTS *liveCaller = AudioEventRTS::heldCaller(callerSlot);
		if (liveCaller) {
			copy.setPlayingAudioIndex(liveCaller->getPlayingAudioIndex());
		}
		addAudioEventNow(&copy, handle);
		if (liveCaller) {
			liveCaller->setPlayingAudioIndex(copy.getPlayingAudioIndex());
		}
		if (logical) {
			PinGameLogicSeedForUnchanged(NULL);
		}
	});
	return handle;
}

//-------------------------------------------------------------------------------------------------
AudioHandle AudioManager::addAudioEventNow(const AudioEventRTS *eventToAdd, AudioHandle handle)
{
	LOGIC_CENSUS( CENSUS_AUDIO );

#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("AUDIO (%d): Received addAudioEvent('%s')", TheGameLogic->getFrame(), eventToAdd->getEventName().str()));
#endif
	if (!eventToAdd->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToAdd);
		if (!eventToAdd->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for requested audio event '%s'\n", eventToAdd->getEventName().str()));
			return AHSV_Error;
		}
	}

	// the fight the music listens to is heard even with the effects switched off
	m_music->noteSound(eventToAdd);

	switch (eventToAdd->getAudioEventInfo()->m_soundType)
	{
		case AT_Music:
			if (!isOn(AudioAffect_Music)) 
				return AHSV_NoSound;
			break;
		case AT_SoundEffect:
			if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) 
				return AHSV_NoSound;
			break;
		case AT_Streaming:
			if (!isOn(AudioAffect_Speech)) 
				return AHSV_NoSound;
			break;
	}

	// if we're currently playing uninterruptable speech, then disallow the addition of this sample
	if (getDisallowSpeech() && eventToAdd->getAudioEventInfo()->m_soundType == AT_Streaming) {
		return AHSV_NoSound;
	}


	// Whether this event is for the local player at all depends on nothing the copy below
	// adds - only on the event info and the owning player, both of which are already here.
	// It used to be asked after a pool allocation, a filename pick and a play-info pass, so
	// in a four-player match most of that work was done for sounds nobody could hear.
	if (!eventToAdd->getUninterruptable() && !shouldPlayLocally(eventToAdd)) {
		return AHSV_NotForLocal;
	}

	AudioEventRTS *audioEvent = MSGNEW("AudioEventRTS") AudioEventRTS(*eventToAdd);		// poolify
	audioEvent->setPlayingHandle( handle ? handle : allocateNewHandle() );
	audioEvent->generateFilename();	// which file are we actually going to play?
	((AudioEventRTS*)eventToAdd)->setPlayingAudioIndex( audioEvent->getPlayingAudioIndex() );
	audioEvent->generatePlayInfo();	// generate pitch shift and volume shift now as well

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == audioEvent->getEventName()) {
			audioEvent->setVolume(it->second);
			break;
		}
	}
	
	// cull muted audio
	if (audioEvent->getVolume() < TheAudio->getAudioSettings()->m_minVolume) {
#ifdef INTENSIVE_AUDIO_DEBUG
		DEBUG_LOG((" - culled due to muting (%d).\n", audioEvent->getVolume()));
#endif
		releaseAudioEventRTS(audioEvent);
		return AHSV_Muted;
	}

	AudioType type = eventToAdd->getAudioEventInfo()->m_soundType;
	if (type == AT_Music)
	{
		m_music->addAudioEvent(audioEvent);
	} 
	else 
	{
		//Possible to nuke audioEvent inside.
		m_sound->addAudioEvent(audioEvent);
	}

	if( audioEvent )
	{
		return audioEvent->getPlayingHandle();
	}
	return AHSV_NoSound;
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isValidAudioEvent(const AudioEventRTS *eventToCheck) const
{
	if (eventToCheck->getEventName().isEmpty()) {
		return false;
	}

	getInfoForAudioEvent(eventToCheck);

	return (eventToCheck->getAudioEventInfo() != NULL);
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isValidAudioEvent( AudioEventRTS *eventToCheck ) const
{
	if( eventToCheck->getEventName().isEmpty() ) 
	{
		return false;
	}

	getInfoForAudioEvent( eventToCheck );

	return( eventToCheck->getAudioEventInfo() );
}

//-------------------------------------------------------------------------------------------------
void AudioManager::addTrackName( const AsciiString& trackName )
{
	m_musicTracks.push_back(trackName);
}

//-------------------------------------------------------------------------------------------------
AsciiString AudioManager::nextTrackName(const AsciiString& currentTrack )
{
	std::vector<AsciiString>::iterator it;
	for (it = m_musicTracks.begin(); it != m_musicTracks.end(); ++it) {
		if (*it == currentTrack) {
			break;
		}
	}

	if (it != m_musicTracks.end()) {
		++it;
	}

	if (it == m_musicTracks.end()) {
		it = m_musicTracks.begin();
		if (it == m_musicTracks.end()) {
			return AsciiString::TheEmptyString;
		}
	}

	return *it;
}

//-------------------------------------------------------------------------------------------------
AsciiString AudioManager::prevTrackName(const AsciiString& currentTrack )
{
	std::vector<AsciiString>::reverse_iterator rit;
	for (rit = m_musicTracks.rbegin(); rit != m_musicTracks.rend(); ++rit) {
		if (*rit == currentTrack) {
			break;
		}
	}
	
	if (rit != m_musicTracks.rend()) {
		++rit;
	}

	if (rit == m_musicTracks.rend()) {
		rit = m_musicTracks.rbegin();
		if (rit == m_musicTracks.rend()) {
			return AsciiString::TheEmptyString;
		}
	}

	return *rit;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::removeAudioEvent(AudioHandle audioEvent)
{
	// in order with the tick's adds, so a sound it starts and stops is started first
	LogicThread::callAfterTick([this, audioEvent]() { removeAudioEventNow(audioEvent); });
}

//-------------------------------------------------------------------------------------------------
void AudioManager::removeAudioEventNow(AudioHandle audioEvent)
{
	LOGIC_CENSUS( CENSUS_AUDIO );
	if (audioEvent == AHSV_StopTheMusic || audioEvent == AHSV_StopTheMusicFade) {
		m_music->removeAudioEvent(audioEvent);
		return;
	}

	if (audioEvent < AHSV_FirstHandle) {
		return;
	}

	AudioRequest *req = allocateAudioRequest( false );
	req->m_handleToInteractOn = audioEvent;
	req->m_request = AR_Stop;
	appendAudioRequest( req );
}

//-------------------------------------------------------------------------------------------------
void AudioManager::setAudioEventEnabled( AsciiString eventToAffect, Bool enable )
{
	setAudioEventVolumeOverride(eventToAffect, (enable ? -1.0f : 0.0f) );
}

//-------------------------------------------------------------------------------------------------
void AudioManager::setAudioEventVolumeOverride( AsciiString eventToAffect, Real newVolume )
{
	if (eventToAffect == AsciiString::TheEmptyString) {
		m_adjustedVolumes.clear();
		return;
	}

	// Find any playing audio events and adjust their volume accordingly.
	if (newVolume != -1.0f) {
		adjustVolumeOfPlayingAudio(eventToAffect, newVolume);
	}

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == eventToAffect) {
			if (newVolume == -1.0f) {
				m_adjustedVolumes.erase(it);
				return;
			} else {
				it->second = newVolume;
				return;
			}
		}
	}

	if (newVolume != -1.0f) {
		std::pair<AsciiString, Real> newPair;
		newPair.first = eventToAffect;
		newPair.second = newVolume;
		m_adjustedVolumes.push_front(newPair);
	}
}

//-------------------------------------------------------------------------------------------------
void AudioManager::removeAudioEvent( AsciiString eventToRemove )
{
	LogicThread::callAfterTick( [this, eventToRemove]() {
		LOGIC_CENSUS( CENSUS_AUDIO );
		removePlayingAudio( eventToRemove );
	} );
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isQueuedFromTick( AudioHandle handle ) const
{
	return std::find( m_handlesQueuedFromTick.begin(), m_handlesQueuedFromTick.end(), handle ) != m_handlesQueuedFromTick.end();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::removeDisabledEvents()
{
	removeAllDisabledAudio();
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isCurrentlyPlaying( AudioHandle audioEvent )
{
	return true;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AudioManager::translateSpeakerTypeToUnsignedInt( const AsciiString& speakerType )
{
	for (UnsignedInt i = 0; TheSpeakerTypes[i]; ++i) {
		if (TheSpeakerTypes[i] == speakerType) {
			return i;
		}
	}

	return 0;
}

//-------------------------------------------------------------------------------------------------
AsciiString AudioManager::translateUnsignedIntToSpeakerType( UnsignedInt speakerType )
{
	if (speakerType >= TheSpeakerTypesCount) {
		return TheSpeakerTypes[0];
	}
	
	return TheSpeakerTypes[speakerType];
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isOn( AudioAffect whichToGet ) const
{
	if (whichToGet & AudioAffect_Music) {
		return m_musicOn;
	} else if (whichToGet & AudioAffect_Sound) {
		return m_soundOn;
	} else if (whichToGet & AudioAffect_Sound3D) {
		return m_sound3DOn;
	}

	// Speech
	return m_speechOn;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::setOn( Bool turnOn, AudioAffect whichToAffect )
{
	if (whichToAffect & AudioAffect_Music) {
		m_musicOn = turnOn;
	}

	if (whichToAffect & AudioAffect_Sound) {
		m_soundOn = turnOn;
	}

	if (whichToAffect & AudioAffect_Sound3D) {
		m_sound3DOn = turnOn;
	}

	if (whichToAffect & AudioAffect_Speech) {
		m_speechOn = turnOn;
	}
}

//-------------------------------------------------------------------------------------------------
void AudioManager::setVolume( Real volume, AudioAffect whichToAffect )
{
	if (whichToAffect & AudioAffect_Music) {
		if (whichToAffect & AudioAffect_SystemSetting) {
			m_systemMusicVolume = volume;
		} else {
			m_scriptMusicVolume = volume;
		}
	}

	if (whichToAffect & AudioAffect_Sound) {
		if (whichToAffect & AudioAffect_SystemSetting) {
			m_systemSoundVolume = volume;
		} else {
			m_scriptSoundVolume = volume;
		}
	}

	if (whichToAffect & AudioAffect_Sound3D) {
		if (whichToAffect & AudioAffect_SystemSetting) {
			m_systemSound3DVolume = volume;
		} else {
			m_scriptSound3DVolume = volume;
		}
	}

	if (whichToAffect & AudioAffect_Speech) {
		if (whichToAffect & AudioAffect_SystemSetting) {
			m_systemSpeechVolume = volume;
		} else {
			m_scriptSpeechVolume = volume;
		}
	}

	// No script volume of its own: a script that turns the effects down turns ambience down with them,
	// through the effects' script factor in getAmbientVolume.
	if ((whichToAffect & AudioAffect_Ambient) && (whichToAffect & AudioAffect_SystemSetting)) {
		m_systemAmbientVolume = volume;
	}

	if ((whichToAffect & AudioAffect_Master) && (whichToAffect & AudioAffect_SystemSetting)) {
		m_systemMasterVolume = volume;
	}

	recomputeVolumes();
}

//-------------------------------------------------------------------------------------------------
/** What the device reads: script times slider for each kind, the camera's zoom on 3D sound, and the
	* Master slider on all of it.  Nothing reaches a sample without passing one of these four or
	* getAmbientVolume, so a unit's reply, whose boost is taken on top of the louder of the effects and
	* voice volumes and clamped to full scale, is turned down by Master like the rest. */
void AudioManager::recomputeVolumes( void )
{
	m_musicVolume = m_scriptMusicVolume * m_systemMusicVolume * m_systemMasterVolume;
	m_soundVolume = m_scriptSoundVolume * m_systemSoundVolume * m_systemMasterVolume;
	m_sound3DVolume = MIN( 1.0f, MAX( 0.0f, m_zoomVolume * m_zoomDetailVolume * m_scriptSound3DVolume * m_systemSound3DVolume * m_systemMasterVolume ) );
	m_speechVolume = m_scriptSpeechVolume * m_systemSpeechVolume * m_systemMasterVolume;

	// Flagged every frame, changed or not: MilesAudioManager::getEffectiveVolume folds the listener's
	// distance into a 3D sound's volume, so a playing sound needs it recomputed as the camera moves.
	// EA skipped this while any speech stream played, against a hang inside the Miles DLL that the
	// XAudio2 implementation does not have, and 3D volumes froze for as long as an EVA line lasted.
	m_volumeHasChanged = TRUE;
}

//-------------------------------------------------------------------------------------------------
Real AudioManager::getVolume( AudioAffect whichToGet )
{
	if (whichToGet & AudioAffect_Music) {
		return m_musicVolume;
	} else if (whichToGet & AudioAffect_Sound) {
		return m_soundVolume;
	} else if (whichToGet & AudioAffect_Sound3D) {
		return m_sound3DVolume;
	} else if (whichToGet & AudioAffect_Ambient) {
		return m_systemAmbientVolume;
	}

	// Speech
	return m_speechVolume;
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isAmbientSound( const AudioEventInfo *info )
{
	return info != NULL
		&& info->m_soundType == AT_SoundEffect
		&& BitTest( info->m_type, ST_WORLD )
		&& !BitTest( info->m_type, ST_UI | ST_VOICE | ST_SHROUDED )
		&& BitTest( info->m_control, AC_LOOP )
		&& info->m_priority == AP_LOWEST;
}

//-------------------------------------------------------------------------------------------------
Real AudioManager::getAmbientVolume( Bool positional ) const
{
	const Real volume = positional ? m_zoomVolume * m_scriptSound3DVolume * m_systemAmbientVolume
		: m_scriptSoundVolume * m_systemAmbientVolume;
	return MIN( 1.0f, MAX( 0.0f, volume * m_systemMasterVolume ) );
}

//-------------------------------------------------------------------------------------------------
void AudioManager::set3DVolumeAdjustment( Real volumeAdjustment )
{
	m_zoomVolume = volumeAdjustment;
	recomputeVolumes();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::setListenerPosition( const Coord3D *newListenerPos, const Coord3D *newListenerOrientation )
{
	m_listenerPosition = *newListenerPos;
	m_listenerOrientation = *newListenerOrientation;
}

//-------------------------------------------------------------------------------------------------
const Coord3D *AudioManager::getListenerPosition( void ) const
{
	return &m_listenerPosition;
}

//-------------------------------------------------------------------------------------------------
AudioRequest *AudioManager::allocateAudioRequest( Bool useAudioEvent )
{
	AudioRequest *audioReq = newInstance(AudioRequest);
	audioReq->m_usePendingEvent = useAudioEvent;
	audioReq->m_requiresCheckForSample = false;
	return audioReq;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::releaseAudioRequest( AudioRequest *requestToRelease )
{
	if (requestToRelease) {
		requestToRelease->deleteInstance();
	}
}

//-------------------------------------------------------------------------------------------------
void AudioManager::appendAudioRequest( AudioRequest *m_request )
{
	m_audioRequests.push_back(m_request);
}

//-------------------------------------------------------------------------------------------------
// Remove all pending audio requests
void AudioManager::removeAllAudioRequests( void )
{
  std::list<AudioRequest*>::iterator it;
  for ( it = m_audioRequests.begin(); it != m_audioRequests.end(); it++ ) {
    releaseAudioRequest( *it );
  }

  m_audioRequests.clear();
}

//-------------------------------------------------------------------------------------------------
void AudioManager::processRequestList( void )
{
	
}

//-------------------------------------------------------------------------------------------------
AudioEventInfo *AudioManager::newAudioEventInfo( AsciiString audioName )
{
	AudioEventInfo *eventInfo = findAudioEventInfo(audioName);
	if (eventInfo) {
		DEBUG_CRASH(("Requested add of '%s' multiple times. Is this intentional? - jkmcd\n", audioName.str()));
		return eventInfo;
	}

	m_allAudioEventInfo[audioName] = newInstance(AudioEventInfo);
	return m_allAudioEventInfo[audioName];
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::rememberEventBeforeMapINI( AsciiString eventName )
{
	// the same lookup newAudioEventInfo makes, so this keeps the event the parse will write over
	AudioEventInfo *live = findAudioEventInfo(eventName);
	if (live == NULL)
		return FALSE;

	AudioEventInfo *copy = newInstance(AudioEventInfo);
	*copy = *live;
	m_beforeMapOverrides.push_back(std::make_pair(live, copy));
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Add an AudioEventInfo structure allocated elsewhere to the audio event list
void AudioManager::addAudioEventInfo( AudioEventInfo * newEvent )
{
  // Warning: Don't try to copy the structure. It may be a derived class
  AudioEventInfo *eventInfo = findAudioEventInfo( newEvent->m_audioName );
  if (eventInfo) 
  {
    DEBUG_CRASH(("Requested add of '%s' multiple times. Is this intentional? - jkmcd\n", newEvent->m_audioName.str()));
    *eventInfo = *newEvent;
  }
  else
  {
    m_allAudioEventInfo[newEvent->m_audioName] = newEvent;
  }
}

//-------------------------------------------------------------------------------------------------
AudioEventInfo *AudioManager::findAudioEventInfo( AsciiString eventName ) const
{
	AudioEventInfoHash::const_iterator it;
	it = m_allAudioEventInfo.find(eventName);
	if (it == m_allAudioEventInfo.end()) {
		AsciiString retailName;
		return TheModNamespace.findRetailName(eventName, MODNAME_AUDIOEVENT, &retailName) ? findAudioEventInfo(retailName) : NULL;
	}

	return (*it).second;
}

//-------------------------------------------------------------------------------------------------
// Remove all AudioEventInfo's with the m_isLevelSpecific flag
void AudioManager::removeLevelSpecificAudioEventInfos(void)
{
  AudioEventInfoHash::iterator it = m_allAudioEventInfo.begin();

  while ( it != m_allAudioEventInfo.end() )
  {
    AudioEventInfoHash::iterator next = it; // Make sure erase doesn't cause problems
    next++;

    if ( it->second->isLevelSpecific() )
    {
      it->second->deleteInstance();
      m_allAudioEventInfo.erase( it );
    }

    it = next;
  }

}

//-------------------------------------------------------------------------------------------------
const AudioSettings *AudioManager::getAudioSettings( void ) const
{
	return m_audioSettings;
}

//-------------------------------------------------------------------------------------------------
AudioSettings *AudioManager::friend_getAudioSettings( void )
{
	return m_audioSettings;
}

//-------------------------------------------------------------------------------------------------
const MiscAudio *AudioManager::getMiscAudio( void ) const
{
	return m_miscAudio;
}

//-------------------------------------------------------------------------------------------------
MiscAudio *AudioManager::friend_getMiscAudio( void )
{
	return m_miscAudio;
}

//-------------------------------------------------------------------------------------------------
const FieldParse *AudioManager::getFieldParseTable( void ) const
{
	return audioSettingsFieldParseTable;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::refreshCachedVariables()
{
	m_hardwareAccel = isCurrentProviderHardwareAccelerated();
	m_surroundSpeakers = isCurrentSpeakerTypeSurroundSound();
}

//-------------------------------------------------------------------------------------------------
/**
 * How long a RIFF/WAVE file runs, in milliseconds, read out of its own header.  Zero for anything
 * that is not one, or that cannot be read.
 *
 * This exists because getAudioLengthMS is not a client question.  Two script conditions are built on
 * it - "has this speech finished" and "has this sound finished" - and both run inside GameLogic, so
 * the answer is part of the simulation.  The way it used to be answered was not: Miles was asked to
 * open the file, and a machine with no working sound device has no handle to open it with, which
 * made the length zero.  So did every -headless run, since headless turns the audio off.  Those
 * machines walked straight past a wait that everyone else sat through - a mismatch in a network
 * game, and a replay that diverges from the machine that recorded it.
 *
 * Every sound and every line of speech the game ships is a .wav, and a .wav says how long it is in
 * its own header: the byte rate in the format chunk and the size of the data chunk.  No device, same
 * answer everywhere.
 */
Real AudioManager::getWaveFileLengthMS( AsciiString strToLoad ) const
{
	if (strToLoad.isEmpty())
		return 0.0f;

	File *file = TheFileSystem->openFile( strToLoad.str(), File::READ | File::BINARY );
	if (file == NULL)
		return 0.0f;

	Real lengthMS = 0.0f;

	// RIFF <size> WAVE, then a run of <fourcc><size><payload> chunks.
	char header[12];
	if (file->read( header, sizeof(header) ) == sizeof(header)
			&& ::memcmp( header, "RIFF", 4 ) == 0
			&& ::memcmp( header + 8, "WAVE", 4 ) == 0)
	{
		UnsignedInt bytesPerSecond = 0;
		UnsignedInt dataBytes = 0;

		// 'fmt ' can follow 'data' in a legal file, so read to the end rather than stopping at
		// whichever of the two turns up first.
		for (;;)
		{
			char chunkID[4];
			UnsignedInt chunkSize = 0;
			if (file->read( chunkID, sizeof(chunkID) ) != sizeof(chunkID))
				break;
			if (file->read( &chunkSize, sizeof(chunkSize) ) != sizeof(chunkSize))
				break;

			if (::memcmp( chunkID, "fmt ", 4 ) == 0 && chunkSize >= 16)
			{
				// wFormatTag, nChannels, nSamplesPerSec, nAvgBytesPerSec, ...
				char fmt[16];
				if (file->read( fmt, sizeof(fmt) ) != sizeof(fmt))
					break;
				::memcpy( &bytesPerSecond, fmt + 8, sizeof(bytesPerSecond) );
				if (chunkSize > sizeof(fmt))
					file->seek( chunkSize - sizeof(fmt), File::CURRENT );
			}
			else if (::memcmp( chunkID, "data", 4 ) == 0)
			{
				dataBytes = chunkSize;
				if (file->seek( chunkSize, File::CURRENT ) < 0)
					break;
			}
			else if (file->seek( chunkSize, File::CURRENT ) < 0)
			{
				break;
			}

			if (chunkSize & 1)
				file->seek( 1, File::CURRENT );		// chunks are word aligned
		}

		if (bytesPerSecond > 0 && dataBytes > 0)
			lengthMS = (Real)dataBytes * 1000.0f / (Real)bytesPerSecond;
	}

	file->close();
	return lengthMS;
}

//-------------------------------------------------------------------------------------------------
/* Logic's, not the device's (LOGIC-THREAD.md M7): the event table, the file headers and the logic
	 stream's Unchanged reads, with no draw from the audio stream.  Only a file that is not a .wav goes
	 to the device, in getFileLengthMS, which counts it. */
Real AudioManager::getAudioLengthMS( const AudioEventRTS *event )
{
	if (!event->getAudioEventInfo()) {
		getInfoForAudioEvent(event);
		if (!event->getAudioEventInfo()) {
			return 0.0f;
		}
	}

	AudioEventRTS tmpEvent = *event;

	tmpEvent.generateFilename( FALSE );
	tmpEvent.generatePlayInfo( FALSE );
	AsciiString attack = tmpEvent.getAttackFilename();
	AsciiString sound = tmpEvent.getFilename();
	AsciiString decay = tmpEvent.getDecayFilename();

	/* A file in this install's language folder wins over the generic one, and a line of speech runs
		 a different length in German than in English, so two players with different languages waited
		 on the same script for a different number of frames.  In a multiplayer or skirmish session the
		 length comes from the generic folder on every machine, and a line that has no generic file
		 counts as finished at once. */
	if (TheGameEngine->isMultiplayerSession())
	{
		const AudioType type = tmpEvent.getAudioEventInfo()->m_soundType;
		const AsciiString localizedPrefix = tmpEvent.generateFilenamePrefix(type, TRUE);
		const AsciiString genericPrefix = tmpEvent.generateFilenamePrefix(type, FALSE);
		AsciiString *names[] = { &attack, &sound, &decay };
		for (Int i = 0; i < (Int)(sizeof(names) / sizeof(names[0])); ++i)
		{
			if (names[i]->startsWith(localizedPrefix))
			{
				AsciiString generic = genericPrefix;
				generic.concat(names[i]->str() + localizedPrefix.getLength());
				*names[i] = generic;
			}
		}
	}

	return getFileLengthMS(attack) + getFileLengthMS(sound) + getFileLengthMS(decay);
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isMusicAlreadyLoaded(void) const
{
	const AudioEventInfo *musicToLoad = NULL;
	AudioEventInfoHash::const_iterator it;
	for (it = m_allAudioEventInfo.begin(); it != m_allAudioEventInfo.end(); ++it) {
		if (it->second) {
			const AudioEventInfo *aet = it->second;
			if (aet->m_soundType == AT_Music) {
				musicToLoad = aet;
			}
		}
	}

	if (!musicToLoad) {
		return FALSE;
	}

	AudioEventRTS aud;
	aud.setAudioEventInfo(musicToLoad);
	aud.generateFilename();

	AsciiString astr = aud.getFilename();

	return (TheFileSystem->doesFileExist(astr.str()));
}

//-------------------------------------------------------------------------------------------------
void AudioManager::findAllAudioEventsOfType( AudioType audioType, std::vector<AudioEventInfo*>& allEvents )
{
	AudioEventInfoHashIt it;
	for (it = m_allAudioEventInfo.begin(); it != m_allAudioEventInfo.end(); ++it) {
		AudioEventInfo *aud = (*it).second;
		if (aud->m_soundType == audioType) {
			allEvents.push_back(aud);
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isCurrentProviderHardwareAccelerated()
{
	for (Int i = 0; i < MAX_HW_PROVIDERS; ++i) {
		if (getProviderName(getSelectedProvider()) == TheAudio->getAudioSettings()->m_preferred3DProvider[i]) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::isCurrentSpeakerTypeSurroundSound()
{
	return (getSpeakerType() == TheAudio->getAudioSettings()->m_defaultSpeakerType3D);
}

//-------------------------------------------------------------------------------------------------
Bool AudioManager::shouldPlayLocally(const AudioEventRTS *audioEvent)
{
	Player *localPlayer = ThePlayerList->getLocalPlayer();
	if( !localPlayer->isPlayerActive() ) 
	{
		//We are dead, thus are observing. Get the player we are observing. It's 
		//possible that we're not looking at any player, therefore it can be NULL.
		localPlayer = TheControlBar->getObserverLookAtPlayer();
	}

	const AudioEventInfo *ei = audioEvent->getAudioEventInfo();

	// Music should always play locally.
	if (ei->m_soundType == AT_Music) {
		return TRUE;
	}

	if (!BitTest(ei->m_type, (ST_PLAYER | ST_ALLIES | ST_ENEMIES | ST_EVERYONE))) {
		DEBUG_CRASH(("No player restrictions specified for '%s'. Using Everyone.\n", ei->m_audioName.str()));
		return TRUE;
	}

	if (BitTest(ei->m_type, ST_EVERYONE)) {
		return TRUE;
	}

	Player *owningPlayer = ThePlayerList->getNthPlayer(audioEvent->getPlayerIndex());

	if (BitTest(ei->m_type, ST_PLAYER) && BitTest(ei->m_type, ST_UI) && owningPlayer == NULL) {
		DEBUG_ASSERTCRASH(!TheGameLogic->isInGameLogicUpdate(), ("Playing %s sound -- player-based UI sound without specifying a player.\n"));
		return TRUE;
	}

	if (owningPlayer == NULL) {
		DEBUG_CRASH(("Sound '%s' expects an owning player, but the audio event that created it didn't specify one.\n", ei->m_audioName.str()));
		return FALSE;
	}

	if( !localPlayer )
	{
		return FALSE;
	}

	const Team *localTeam = localPlayer->getDefaultTeam();
	if (localTeam == NULL) { 
		return FALSE;
	}

	if (BitTest(ei->m_type, ST_PLAYER))  {
		return owningPlayer == localPlayer;
	}

	if (BitTest(ei->m_type, ST_ALLIES)) { 
		// We have to also check that the owning player isn't the local player, because PLAYER 
		// wasn't specified, or we wouldn't have gotten here.
		return (owningPlayer != localPlayer) && owningPlayer->getRelationship(localTeam) == ALLIES;
	}

	if (BitTest(ei->m_type, ST_ENEMIES)) {
		return owningPlayer->getRelationship(localTeam) == ENEMIES;
	}
	
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
AudioHandle AudioManager::allocateNewHandle( void )
{
	// the tick mints handles too (addAudioEvent), so a sound the render starts beside it shares the pool
	LOGIC_WINDOW_READ( WINDOW_AUDIO_HANDLE );
	// note, intenionally a post increment rather than a pre increment.
	return theAudioHandlePool++;
}

//-------------------------------------------------------------------------------------------------
void AudioManager::releaseAudioEventRTS( AudioEventRTS *eventToRelease )
{
	if( eventToRelease )
	{
		delete eventToRelease;
		eventToRelease = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
void AudioManager::loseFocus( void )
{
	DEBUG_ASSERTLOG(m_savedValues == NULL, ("AudioManager::loseFocus() - leak - jkmcd\n"));
	// In this case, make all the audio go silent.
	m_savedValues = NEW Real[NUM_VOLUME_TYPES];
	m_savedValues[0] = m_systemMusicVolume;
	m_savedValues[1] = m_systemSoundVolume;
	m_savedValues[2] = m_systemSound3DVolume;
	m_savedValues[3] = m_systemSpeechVolume;
	m_savedValues[4] = m_systemAmbientVolume;

	// Now, set them all to 0.
	setVolume(0.0f, (AudioAffect) (AudioAffect_All | AudioAffect_SystemSetting));
}

//-------------------------------------------------------------------------------------------------
void AudioManager::regainFocus( void )
{
	if (!m_savedValues) {
		return;
	}

	// We got focus back. Restore the previous audio values.
	setVolume(m_savedValues[0], (AudioAffect) (AudioAffect_Music | AudioAffect_SystemSetting));
	setVolume(m_savedValues[1], (AudioAffect) (AudioAffect_Sound | AudioAffect_SystemSetting));
	setVolume(m_savedValues[2], (AudioAffect) (AudioAffect_Sound3D | AudioAffect_SystemSetting));
	setVolume(m_savedValues[3], (AudioAffect) (AudioAffect_Speech | AudioAffect_SystemSetting));
	setVolume(m_savedValues[4], (AudioAffect) (AudioAffect_Ambient | AudioAffect_SystemSetting));

	// Now, blow away the old volumes.
	delete [] m_savedValues;
	m_savedValues = NULL;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void INI::parseAudioSettingsDefinition( INI *ini )
{
	ini->initFromINI(TheAudio->friend_getAudioSettings(), TheAudio->getFieldParseTable());

	// time to override the volume settings, default 3-D provider, and speaker setup with the
	// ones stored from the prefs
	OptionPreferences prefs;

	TheAudio->setPreferredProvider(prefs.getPreferred3DProvider());
	TheAudio->setPreferredSpeaker(prefs.getSpeakerType());

	Real relative2DVolume = TheAudio->getAudioSettings()->m_relative2DVolume;
	relative2DVolume = MIN( 1.0f, MAX( -1.0f, relative2DVolume ) );

	TheAudio->friend_getAudioSettings()->m_preferredSoundVolume		= prefs.getSoundVolume() / 100.0f;
	TheAudio->friend_getAudioSettings()->m_preferred3DSoundVolume	= prefs.get3DSoundVolume() / 100.0f;
	TheAudio->friend_getAudioSettings()->m_preferredSpeechVolume	= prefs.getSpeechVolume() / 100.0f;
	TheAudio->friend_getAudioSettings()->m_preferredMusicVolume		= prefs.getMusicVolume() / 100.0f;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
void parseSpeakerType( INI *ini, void *instance, void *store, const void* userData )
{
	AsciiString str;
	ini->parseAsciiString( ini, instance, &str, userData );

	(*(UnsignedInt*)store) = TheAudio->translateSpeakerTypeToUnsignedInt(str);
}

