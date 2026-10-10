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

// FILE: MilesAudioManager.cpp 
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  MilesAudioManager.cpp                                         */
/* Created:    John K. McDonald, Jr., 3/21/2002                              */
/* Desc:       This is the implementation for the MilesAudioManager, which   */
/*						 interfaces with the Miles Sound System.                       */
/* Revision History:                                                         */
/*		7/18/2002 : Initial creation                                           */
/*---------------------------------------------------------------------------*/

#if defined(_WIN32)
#include <dsound.h>
#endif
#include "Lib/Clock.h"
#include "Lib/BaseType.h"
#include "MilesAudioDevice/MilesAudioManager.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/ScopedMutex.h"

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/ObserverCamera.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"

#include "Common/file.h"
#if !defined(_WIN32)
#include "Common/LocalFileSystem.h"

/* Off Windows (C4): InterlockedCompareExchange's job on PlayingAudio::m_status.  Every call site
	 casts the 4-byte PlayingStatus to (volatile long *), which is the same width under Windows' LLP64
	 and twice it under LP64 (macOS, Linux): a like-for-like stand-in would compare and swap eight bytes
	 over a four-byte member.  So this takes the member's own type and works on its own four bytes, with
	 the full barrier every Interlocked* call is.  It returns the value that was there, as that does. */
static inline PlayingStatus playingStatusCompareExchange( volatile PlayingStatus *status,
	PlayingStatus exchange, PlayingStatus comparand )
{
	static_assert( sizeof( PlayingStatus ) == sizeof( Int ), "the status is compared and swapped as its own four bytes" );
	Int expected = (Int)comparand;
	__atomic_compare_exchange_n( (volatile Int *)status, &expected, (Int)exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST );
	return (PlayingStatus)expected;
}
#endif

#ifdef _INTERNAL
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

enum { INFINITE_LOOP_COUNT = 1000000 };

// Callback functions useful for Miles ////////////////////////////////////////////////////////////
static void AILCALLBACK setSampleCompleted( HSAMPLE sampleCompleted );
static void AILCALLBACK set3DSampleCompleted( H3DSAMPLE sample3DCompleted );
static void AILCALLBACK setStreamCompleted( HSTREAM streamCompleted );

static U32 AILCALLBACK streamingFileOpen(char const *fileName, AILFILEHANDLE *file_handle);
static void AILCALLBACK streamingFileClose(AILFILEHANDLE fileHandle);
static S32 AILCALLBACK streamingFileSeek(AILFILEHANDLE fileHandle, S32 offset, U32 type);
static U32 AILCALLBACK streamingFileRead(AILFILEHANDLE fileHandle, void *buffer, U32 bytes);

//-------------------------------------------------------------------------------------------------
/* "Is this sound one of these?", asked of a sound that is already playing or already queued.
	 EA read the type straight through the info pointer at fifteen places in this file and checked
	 it for null at two others, which is the same code admitting both that the pointer can be null
	 and that nobody remembered.  It can: setEventName drops the info because it is not valid for
	 the new name, the drawable custom-ambient path clears it by hand before deleting what it points
	 at, and a delayed loop hands the same event back to the request list without one.  When the
	 sound finishes the read is an access violation at address 0x90, in a stack that is entirely
	 audio and names nothing that caused it.  A sound with no info is not music and not speech,
	 which is what every one of those fifteen questions wants to hear. */
static Bool audioIsType( const AudioEventRTS *event, AudioType type )
{
	const AudioEventInfo *info = event ? event->getAudioEventInfo() : NULL;
	return info != NULL && info->m_soundType == type;
}

//-------------------------------------------------------------------------------------------------
MilesAudioManager::MilesAudioManager() :
	m_providerCount(0),
	m_selectedProvider(PROVIDER_ERROR),
	m_selectedSpeakerType(0),
	m_lastProvider(PROVIDER_ERROR),
	m_digitalHandle(NULL),
	m_num2DSamples(0),
	m_num3DSamples(0),
	m_numStreams(0),
	m_delayFilter(NULL),
	m_binkHandle(NULL),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString)
{
	m_audioCache = NEW AudioFileCache;
}

//-------------------------------------------------------------------------------------------------
MilesAudioManager::~MilesAudioManager()
{
	DEBUG_ASSERTCRASH(m_binkHandle == NULL, ("Leaked a Bink handle. Chuybregts"));
	releaseHandleForBink();
	closeDevice();
	delete m_audioCache;
	
	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm...\n"));
	TheAudio = NULL;
}

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
AudioHandle MilesAudioManager::addAudioEvent( const AudioEventRTS *eventToAdd )
{
	if (TheGlobalData->m_preloadReport) {
		if (!eventToAdd->getEventName().isEmpty()) {
			m_allEventsLoaded.insert(eventToAdd->getEventName());
		}
	}

	return AudioManager::addAudioEvent(eventToAdd);
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp )
{
	std::list<PlayingAudio *>::iterator it;

	static char buffer[128] = { 0 };
	if (buffer[0] == 0) {
		AIL_MSS_version(buffer, 128);
	}
	
	Coord3D lookPos;
	TheTacticalView->getPosition( &lookPos );
	lookPos.z = TheTerrainLogic->getGroundHeight( lookPos.x, lookPos.y );
	const Coord3D *mikePos = TheAudio->getListenerPosition();
	Coord3D distanceVector = TheTacticalView->get3DCameraPosition();
	distanceVector.sub( mikePos );

	Int now = TheGameLogic->getFrame();
	static Int lastCheck = now;
	const Int frames = 60;
	static Int latency = 0;
	static Int worstLatency = 0;
	if( lastCheck + frames <= now )
	{
		latency = AIL_get_timer_highest_delay();
		if( latency > worstLatency )
		{
			worstLatency = latency;
		}
		lastCheck = now;
	}

	if( dd )
	{
		dd->printf("Miles Sound System version: %s    ", buffer);
		dd->printf("Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
		dd->printf("Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No"));
		dd->printf("3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No"));
		dd->printf("Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No"));
		dd->printf("Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No"));
		dd->printf("Channels Available: ");
		dd->printf("%d Sounds    ", m_sound->getAvailableSamples());

		dd->printf("%d(%d) 3D Sounds\n", m_sound->getAvailable3DSamples(), m_available3DSamples.size() );
		dd->printf("Volume: ");
		dd->printf("Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f));
		dd->printf("3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f));
		dd->printf("Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f));
		dd->printf("Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f));
		dd->printf("Current 3D Provider: %s    ", 
			
		TheAudio->getProviderName(m_selectedProvider).str());
		dd->printf("Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		dd->printf( "Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n", 
				(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z );
		dd->printf( "Camera distance from microphone: %d -- Zoom Volume: %d%%\n", 
				(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume()*100.0f) );
		dd->printf( "Worst latency: %d -- Current latency: %d\n", worstLatency, latency );

		dd->printf("-----------------------------------------------------------\n");
		dd->printf("Playing Audio\n");
	}
	if( fp )
	{
		fprintf( fp, "Miles Sound System version: %s    ", buffer );
		fprintf( fp, "Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize() );
		fprintf( fp, "Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No") );
		fprintf( fp, "3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No") );
		fprintf( fp, "Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No") );
		fprintf( fp, "Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No") );
		fprintf( fp, "Channels Available: " );
		fprintf( fp, "%d Sounds    ", m_sound->getAvailableSamples() );
		fprintf( fp, "%d 3D Sounds\n", m_sound->getAvailable3DSamples() );
		fprintf( fp, "Volume: ");
		fprintf( fp, "Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f) );
		fprintf( fp, "3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f) );
		fprintf( fp, "Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f) );
		fprintf( fp, "Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f) );
		fprintf( fp, "Current 3D Provider: %s    ", TheAudio->getProviderName(m_selectedProvider).str());
		fprintf( fp, "Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		fprintf( fp, "Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n", 
				(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z );
		fprintf( fp, "Camera distance from microphone: %d -- Zoom Volume: %d%%\n", 
				(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume()*100.0f) );

		fprintf( fp, "-----------------------------------------------------------\n" );
		fprintf( fp, "Playing Audio\n" );
	}

	PlayingAudio *playing = NULL;
	Int channel;
	Int channelCount;
	Real volume = 0.0f;
	AsciiString filenameNoSlashes;

	const Int maxChannels = 64;
	PlayingAudio *playingArray[maxChannels] = { NULL };

	// 2-D Sounds
	if( dd )
	{
		dd->printf("-----------------------------------------------------Sounds\n");
		channelCount = TheAudio->getNum2DSamples();
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[AIL_sample_user_data(playing->m_sample, 0)] = playing;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i) {
			playing = playingArray[i];
			if (!playing) {
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-20s - (%s) Volume: %d (2D)\n", i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
			playingArray[i] = NULL;
		}
	}
	if( fp )
	{
		fprintf( fp, "-----------------------------------------------------Sounds\n" );
		channelCount = TheAudio->getNum2DSamples();
		channel = 1;
		for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind( '\\' ) + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume( playing->m_audioEventRTS );

			fprintf( fp, "%2d: %-20s - (%s) Volume: %d (2D)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}
		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%d: Silence\n", i );
		}
	}

	const Coord3D *microphonePos = TheAudio->getListenerPosition();

	// Now 3D Sounds
	if( dd )
	{
		dd->printf("--------------------------------------------------3D Sounds\n");
		channelCount = TheAudio->getNum3DSamples();
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[AIL_3D_user_data(playing->m_3DSample, 0)] = playing;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i) 
		{
			playing = playingArray[i];
			if (!playing) 
			{
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);
			Real dist = -1.0f;
			const Coord3D *pos = playing->m_audioEventRTS->getPosition();
			char distStr[32];
			if( pos )
			{
				Coord3D vector = *microphonePos;
				vector.sub( pos );
				dist = vector.length();
				sprintf( distStr, "%d", REAL_TO_INT( dist ) );
			}
			else
			{
				sprintf( distStr, "???" );
			}
			char str[32];
			switch( playing->m_audioEventRTS->getOwnerType() )
			{
				case OT_Positional:
					sprintf( str, "(3D)" );
					break;
				case OT_Object:
					sprintf( str, "(3DObj)" );
					break;
				case OT_Drawable:
					sprintf( str, "(3DDraw)" );
					break;
				case OT_Dead:
					sprintf( str, "(3DDead)" );
					break;

			}

			dd->printf("%2d: %-20s - (%s) Volume: %d, Dist: %s, %s\n", 
				i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume), distStr, str );
			playingArray[i] = NULL;
		}
	}
	if( fp )
	{
		fprintf( fp, "--------------------------------------------------3D Sounds\n" );
		channelCount = TheAudio->getNum3DSamples();
		channel = 1;
		for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) 
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume( playing->m_audioEventRTS );
			fprintf( fp, "%2d: %-24s - (%s) Volume: %d \n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}

		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%2d: Silence\n", i );
		}
	}

	// Now Streams
	if( dd )
	{
		dd->printf("----------------------------------------------------Streams\n");
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}
		
		for ( int i = channel; i <= channelCount; ++i) {
			dd->printf("%2d: Silence\n", i);
		}
		dd->printf("===========================================================\n");
	}
	if( fp )
	{
		fprintf( fp, "----------------------------------------------------Streams\n" );
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) 
		{
			playing = *it;
			if( !playing ) 
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;
			
			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			fprintf( fp, "%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT( volume ) );
		}
		
		for( int i = channel; i <= channelCount; ++i ) 
		{
			fprintf( fp, "%2d: Silence\n", i );
		}
		fprintf( fp, "===========================================================\n" );
	}
}
#endif

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::init()
{
	AudioManager::init();
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("Sound has temporarily been disabled in debug builds only. jkmcd\n"));
	// for now, _DEBUG builds only should have no sound. ask jkmcd or srj about this.
	return;
#endif

	// We should now know how many samples we want to load
	openDevice();
	m_audioCache->setMaxSize(getAudioSettings()->m_maxCacheSize);

	// Now, set the file callbacks to load the streams from Biggie files
	AIL_set_file_callbacks(streamingFileOpen, streamingFileClose, streamingFileSeek, streamingFileRead);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::reset()
{
#if defined(_DEBUG) || defined(_INTERNAL)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif

	AudioManager::reset();
	stopAllAudioImmediately();
  removeAllAudioRequests();
  // This must come after stopAllAudioImmediately() and removeAllAudioRequests(), to ensure that
  // sounds pointing to the temporary AudioEventInfo handles are deleted before their info is deleted
  removeLevelSpecificAudioEventInfos();
}

//-------------------------------------------------------------------------------------------------
/** -wav <from> <to> [name]: record the finished mix over a range of logic frames.
	*
	* A -wav run opens no audio device (openDevice): the mix is moved by the logic clock, here, by
	* exactly 1/30 of a second of sound per logic frame, however long the frame took on the wall clock.
	* So the file is as long as the frames it spans and lines up with the picture of the same frames,
	* in a run that plays at the speed a person hears, in a -directorrecord run that crawls, and on a
	* machine with no speakers.  A sound asked for during frame N starts N/30 seconds in, because the
	* requests are only played after this has mixed up to N.  Nothing is heard while it runs. */
static void updateSoundCapture( void )
{
	static Bool recording = FALSE;
	static Bool finished = FALSE;

	if (TheGlobalData->m_wavEndFrame <= 0 || TheGameLogic == NULL)
		return;

	const UnsignedInt frame = TheGameLogic->getFrame();
	AIL_ex_mix_to_frame((S32)frame);

	if (finished || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return;
	if (frame < (UnsignedInt)TheGlobalData->m_wavStartFrame)
		return;

	if (!recording)
	{
		char directory[_MAX_PATH];
		snprintf(directory, ARRAY_SIZE(directory), "%sVideos", TheGlobalData->getPath_UserData().str());
#if defined(_WIN32)
		CreateDirectoryA(directory, NULL);
#else
		TheLocalFileSystem->createDirectory(AsciiString(directory));
#endif

		char pathname[_MAX_PATH];
		snprintf(pathname, ARRAY_SIZE(pathname), "%s\\%s.wav", directory, TheGlobalData->m_wavName.str());
		if (!AIL_ex_start_capture(pathname))
		{
			finished = TRUE;
			DEBUG_LOG(("AUDIO: nothing could be recorded into %s\n", pathname));
			return;
		}

		recording = TRUE;
		DEBUG_LOG(("AUDIO: recording logic frames %d to %d into %s\n",
			TheGlobalData->m_wavStartFrame, TheGlobalData->m_wavEndFrame, pathname));
		return;
	}

	if (frame <= (UnsignedInt)TheGlobalData->m_wavEndFrame)
		return;

	finished = TRUE;
	AIL_ex_stop_capture();

	// The file's own length, counted in samples written, against the frames the capture spanned.
	S32 samples = 0;
	S32 rate = 0;
	AIL_ex_capture_length(&samples, &rate);
	const double recordedSeconds = rate > 0 ? (double)samples / (double)rate : 0.0;
	const double pictureSeconds = (double)(frame - TheGlobalData->m_wavStartFrame) / LOGICFRAMES_PER_SECONDS_REAL;
	DEBUG_LOG(("AUDIO: recorded %.3f seconds of sound (%d samples at %d Hz) for %.3f seconds of picture, "
		"%.3f adrift\n", recordedSeconds, (Int)samples, (Int)rate, pictureSeconds, recordedSeconds - pictureSeconds));
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::update()
{
	AudioManager::update();
	processCompletedAudio();
	updateSoundCapture();
	setDeviceListenerPosition();
	processRequestList();
	processPlayingList();
	processFadingList();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAudio( AudioAffect which )
{
	// All we really need to do is:
	// 1) Remove the EOS callback.
	// 2) Stop the sample, (so that when we later unload it, bad stuff doesn't happen)
	// 3) Set the status to stopped, so that when we next process the playing list, we will 
	//		correctly clean up the sample.


	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				AIL_register_EOS_callback(playing->m_sample, NULL);
				AIL_stop_sample(playing->m_sample);
				playing->m_status = PS_Stopped;
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
				AIL_stop_3D_sample(playing->m_3DSample);
				playing->m_status = PS_Stopped;
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}
				AIL_register_stream_callback(playing->m_stream, NULL);
				AIL_pause_stream(playing->m_stream, 1);
				playing->m_status = PS_Stopped;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAudio( AudioAffect which )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_stop_sample(playing->m_sample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_stop_3D_sample(playing->m_3DSample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}

				AIL_pause_stream(playing->m_stream, 1);
			}
		}
	}
	
	//Get rid of PLAY audio requests when pausing audio.
	std::list<AudioRequest*>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); /* empty */) 
	{
		AudioRequest *req = (*ait);
		if( req && req->m_request == AR_Play ) 
		{
			req->deleteInstance();
			ait = m_audioRequests.erase(ait);
		}
		else
		{
			ait++;
		}
	}
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::resumeAudio( AudioAffect which )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	if (BitTest(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_resume_sample(playing->m_sample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				AIL_resume_3D_sample(playing->m_3DSample);
			}
		}
	}

	if (BitTest(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing && playing->isPlaying()) {
				if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
					if (!BitTest(which, AudioAffect_Music)) {
						continue;
					}
				} else {
					if (!BitTest(which, AudioAffect_Speech)) {
						continue;
					}
				}
				AIL_pause_stream(playing->m_stream, 0);
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAmbient( Bool shouldPause )
{

}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::playAudioEvent( AudioEventRTS *event, Bool requestStop )
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("MILES (%d) - Processing play request: %d (%s)", TheGameLogic->getFrame(), event->getPlayingHandle(), event->getEventName().str()));
#endif
	if (!event) {
		return;
	}

	const AudioEventInfo *info = event->getAudioEventInfo();
	if (!info) {
		delete event;			// nothing downstream will take it now
		return;
	}

	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing = NULL;

	AudioHandle handleToKill = event->getHandleToKill();

	AsciiString fileToPlay = event->getFilename();
	PlayingAudio *audio = allocatePlayingAudio();
	audio->m_requestStop = requestStop;
	switch(info->m_soundType)
	{
		case AT_Music:
		case AT_Streaming:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Stream\n"));
		#endif
			
			if ((info->m_soundType == AT_Streaming) && event->getUninterruptable()) {
				stopAllSpeech();
			}

			Real curVolume = 1.0;
			if (info->m_soundType == AT_Music) {
				curVolume = m_musicVolume;
			} else {
				curVolume = m_speechVolume;
			}
			curVolume *= event->getVolume();

			Bool foundSoundToReplace = false;
			if (handleToKill) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					playing = (*it);
					if (!playing) {
						continue;
					}

					if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill) 
					{
						//Release this streaming channel immediately because we are going to play another sound in it's place.
						stopPlayingAudio(playing);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			// One track at a time.  Whatever music is still on the list and not fading out is stopped,
			// so a track started without a stop in front of it cannot play over another.
			if (info->m_soundType == AT_Music && (!handleToKill || foundSoundToReplace)) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					playing = (*it);
					if (playing && !playing->m_fade && audioIsType(playing->m_audioEventRTS, AT_Music)) {
						stopPlayingAudio(playing);
					}
				}
			}

			HSTREAM stream;
			if (!handleToKill || foundSoundToReplace) {
				stream = AIL_open_stream(m_digitalHandle, fileToPlay.str(), 0);
			} else {
				stream = NULL;
			}

			// Put this on here, so that the audio event RTS will be cleaned up regardless.
			audio->m_audioEventRTS = event; 
			audio->m_stream = stream;
			audio->m_type = PAT_Stream;

			if (stream) {
				if ((info->m_soundType == AT_Streaming) && event->getUninterruptable()) {
					setDisallowSpeech(TRUE);
	 			}
				AIL_set_stream_volume_pan(stream, curVolume, 0.5f);
				playStream(event, stream);
				{
					CriticalSectionClass::LockClass lock(m_playingStreamsCS);
					m_playingStreams.push_back(audio);
				}
				audio = NULL;
			}
			break;
		}

		case AT_SoundEffect:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Sound"));
		#endif

			
			if (event->isPositionalAudio()) {
				// Sounds that are non-global are positional 3-D sounds. Deal with them accordingly
			#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Positional"));
			#endif
				Bool foundSoundToReplace = false;
				if (handleToKill) 
				{
					for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
						playing = (*it);
						if (!playing) {
							continue;
						}

						if( playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill ) 
						{
							//Release this 3D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}
				
				H3DSAMPLE sample3D;
				if( !handleToKill || foundSoundToReplace )
				{
					sample3D = getFirst3DSample( event );
					if( !sample3D )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample3D = getFirst3DSample( event );
						}
					}
				} 
				else 
				{
					sample3D = NULL;
				}
				// Push it onto the list of playing things
				audio->m_audioEventRTS = event; 
				audio->m_3DSample = sample3D;
				audio->m_file = NULL;
				audio->m_type = PAT_3DSample;
				{
					CriticalSectionClass::LockClass lock(m_playing3DSoundsCS);
					m_playing3DSounds.push_back(audio);
				}

				if (sample3D) {
					audio->m_file = playSample3D(event, sample3D);
					m_sound->notifyOf3DSampleStart();
				}

				if( !audio->m_file ) 
				{
					m_playing3DSounds.pop_back();
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)\n"));
					#endif
				} 
				else 
				{
					audio = NULL;
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Playing.\n"));
					#endif
				}
			} 
			else 
			{
				// UI sounds are always 2-D. All other sounds should be Positional
				// Unit acknowledgement, etc, falls into the UI category of sound.
				Bool foundSoundToReplace = false;
				if (handleToKill) {
					for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
						playing = (*it);
						if (!playing) {
							continue;
						}

						if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill) 
						{
							//Release this 2D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				HSAMPLE sample;
				if( !handleToKill || foundSoundToReplace ) 
				{
					sample = getFirst2DSample(event);
					if( !sample )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample = getFirst2DSample( event );
						}
					}
					} 
				else 
				{
					sample = NULL;
				}

				// Push it onto the list of playing things
				audio->m_audioEventRTS = event; 
				audio->m_sample = sample;
				audio->m_file = NULL;
				audio->m_type = PAT_Sample;
				{
					CriticalSectionClass::LockClass lock(m_playingSoundsCS);
					m_playingSounds.push_back(audio);
				}

				if (sample) {
					audio->m_file = playSample(event, sample);
					m_sound->notifyOf2DSampleStart();
				}

				if (!audio->m_file) {
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)\n"));
					#endif
					m_playingSounds.pop_back();
				} else {
					audio = NULL;
				}

				#ifdef INTENSIVE_AUDIO_DEBUG
					DEBUG_LOG((" Playing.\n"));
				#endif
			}
			break;
		}
	}

	// If we were able to successfully play audio, then we set it to NULL above. (And it will be freed
	// later. However, if audio is non-NULL at this point, then it must be freed.
	if (audio) {
		stopPlayingAudio(audio);
		releasePlayingAudio(audio);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAudioEvent( AudioHandle handle )
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("MILES (%d) - Processing stop request: %d\n", TheGameLogic->getFrame(), handle));
#endif

	std::list<PlayingAudio *>::iterator it;
	if ( handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade ) {
		// Every music stream, not the first one found.  A stream that was already told to stop or fade
		// stays on this list until the end of the update, so two track changes inside one update (two
		// logic frames, or two scripts on one frame) used to find the old track a second time and leave
		// the first new one playing under the second.
		for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
			PlayingAudio *audio = (*it);
			if (!audio) {
				continue;
			}

			if( audioIsType(audio->m_audioEventRTS, AT_Music) )
			{
				if( handle == AHSV_StopTheMusicFade )
				{
					audio->m_fade = true;	// processPlayingList moves it across, holding both locks
				}
				else
				{
					stopPlayingAudio( audio );
				}
			}
		}
	}

	//
	// A sound between loops that have a delay is not on any playing list: startNextLoop handed its
	// event back to the request list.  Stopping it there is the only way to stop it at all, or the
	// loop outlives the thing that asked for it.  A sound effect still plays the one pass it was
	// waiting for, the same as one caught mid-play below.
	//
	std::list<AudioRequest*>::iterator ait;
	for ( ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait ) {
		AudioRequest *req = (*ait);
		if (req && req->m_usePendingEvent && req->m_pendingEvent
				&& req->m_pendingEvent->getPlayingHandle() == handle) {
			if (audioIsType(req->m_pendingEvent, AT_SoundEffect)) {
				req->m_requestStop = true;
			} else {
				req->deleteInstance();
				m_audioRequests.erase(ait);
			}
			return;
		}
	}

	for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			// found it
			// Ask it to stop; the next processPlayingList sweep does the Miles side and the free.
#if defined(_WIN32)
			InterlockedCompareExchange( (volatile long *)&audio->m_status, PS_Stopping, PS_Playing );
#else
			playingStatusCompareExchange( &audio->m_status, PS_Stopping, PS_Playing );
#endif
			notifyOfAudioCompletion((UnsignedIntPtr)(audio->m_stream), PAT_Stream);
			break;
		}
	}

	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			// EA's stop for a sound effect: let this pass finish and start no further loop.  Moving
			// it straight to PS_Stopping cut death cries, build-complete barks and weapon tails off
			// the frame they were asked to stop.
			audio->m_requestStop = true;
			break;
		}
	}

	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		PlayingAudio *audio = (*it);
		if (!audio) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" (%s)\n", audio->m_audioEventRTS->getEventName()));
		#endif
			audio->m_requestStop = true;
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::killAudioEventImmediately( AudioHandle audioEvent )
{
	//First look for it in the request list.
	std::list<AudioRequest*>::iterator ait;
	for( ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ait++ ) 
	{
		AudioRequest *req = (*ait);
		// A play request carries the event, not the handle, so the match is on the event's handle.
		// The two used to share a union and this compared a pointer against a handle instead.
		if( req && req->m_usePendingEvent && req->m_pendingEvent
				&& req->m_pendingEvent->getPlayingHandle() == audioEvent )
		{
			req->deleteInstance();
			ait = m_audioRequests.erase(ait);
			return;
		}
	}

	//Look for matching 3D sound to kill
	std::list<PlayingAudio *>::iterator it;
	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio ) 
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );
			return;
		}
	}

	//Look for matching 2D sound to kill
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio )
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );
			return;
		}
	}

	//Look for matching steaming sound to kill
	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); it++ ) 
	{
		PlayingAudio *audio = (*it);
		if( !audio )
		{
			continue;
		}

		if( audio->m_audioEventRTS->getPlayingHandle() == audioEvent ) 
		{
			stopPlayingAudio( audio );
			releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
			return;
		}
	}

}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::pauseAudioEvent( AudioHandle handle )
{
	// pause audio
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::loadFileForRead( AudioEventRTS *eventToLoadFrom )
{
	return m_audioCache->openFile(eventToLoadFrom);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::closeFile( void *fileRead )
{
	m_audioCache->closeFile(fileRead);
}


//-------------------------------------------------------------------------------------------------
PlayingAudio *MilesAudioManager::allocatePlayingAudio( void )
{
	PlayingAudio *aud = NEW PlayingAudio;	// poolify
	aud->m_status = PS_Playing;
	return aud;
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releaseMilesHandles( PlayingAudio *release )
{
	switch (release->m_type)
	{
		case PAT_Sample:
		{
			if (release->m_sample) {
				AIL_register_EOS_callback(release->m_sample, NULL);
				AIL_stop_sample(release->m_sample);
				m_availableSamples.push_back(release->m_sample);
				release->m_sample = 0;
			}
			break;
		}
		case PAT_3DSample:
		{
			if (release->m_3DSample) {
				AIL_register_3D_EOS_callback(release->m_3DSample, NULL);
				AIL_stop_3D_sample(release->m_3DSample);
				m_available3DSamples.push_back(release->m_3DSample);
				release->m_3DSample = 0;
			}
			break;
		}
		case PAT_Stream:
		{
			if (release->m_stream) {
				AIL_register_stream_callback(release->m_stream, NULL);
				AIL_close_stream(release->m_stream);
				release->m_stream = 0;
			}
			break;
		}
	}
	release->m_type = PAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releasePlayingAudio( PlayingAudio *release )
{
	//
	// A playing event's info can be gone by the time the sound finishes: setEventName drops it
	// (it is not valid for a new name), the drawable custom-ambient path clears it by hand before
	// the info it points at is deleted, and a delayed loop hands the same event back to the
	// request list without one.  EA knew - playAudioEvent and checkForSample both test for it -
	// and then read m_soundType straight through the pointer here, which is an access violation
	// at address 0x90 in a stack that is all audio and names nothing that caused it.
	//
	// Missing info is not a reason to skip the notification: that is the count of how many sample
	// channels are in use, and losing one leaks a channel for the rest of the match until the
	// game runs out and goes quiet.  m_type already says which channel this was, and the two
	// handle tests below already do the real work - the sound type is only excluding streams,
	// and a stream holds neither handle.
	//
	// Which of the three it was has never been pinned down, and a guard that hides the cause is
	// worth less than one that names it: say the event, so the next occurrence arrives with the
	// sound's name attached instead of a register dump.
	if (release->m_file) {
		closeFile( release->m_file );
		release->m_file = NULL;
	}
	if (release->m_cleanupAudioEventRTS) {
		releaseAudioEventRTS(release->m_audioEventRTS);
	}
	delete release;
}

//-------------------------------------------------------------------------------------------------
/** Everything that has to talk to Miles to stop a sound.  Never call this holding one of the list
	* critical sections: Miles calls back into notifyOfAudioCompletion from its own thread and that
	* wants the same lock.  The two interlocked steps make the body run exactly once even if the
	* main thread and the timer thread both arrive here for the same sound. */
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopPlayingAudio( PlayingAudio *release )
{
	// Playing -> Stopping, then Stopping -> Stopped.  Whoever wins the second one does the work.
#if defined(_WIN32)
	InterlockedCompareExchange( (volatile long *)&release->m_status, PS_Stopping, PS_Playing );
#else
	playingStatusCompareExchange( &release->m_status, PS_Stopping, PS_Playing );
#endif
#if defined(_WIN32)
	const long prevStatus = InterlockedCompareExchange( (volatile long *)&release->m_status, PS_Stopped, PS_Stopping );
#else
	const long prevStatus = playingStatusCompareExchange( &release->m_status, PS_Stopped, PS_Stopping );
#endif
	if (prevStatus != PS_Stopping) {
		return;
	}

	const AudioEventInfo *info = release->m_audioEventRTS ? release->m_audioEventRTS->getAudioEventInfo() : NULL;
	if (!info)
	{
		DEBUG_LOG(("AUDIO: releasing '%s' with no event info - see stopPlayingAudio\n",
			release->m_audioEventRTS ? release->m_audioEventRTS->getEventName().str() : "(no event)"));
	}
	if (!info || info->m_soundType == AT_SoundEffect) {
		if (release->m_type == PAT_Sample) {
			if (release->m_sample) {
				m_sound->notifyOf2DSampleCompletion();
			}
		} else {
			if (release->m_3DSample) {
				m_sound->notifyOf3DSampleCompletion();
			}
		}
	}
	releaseMilesHandles(release);	// forces stop of this audio
}

//-------------------------------------------------------------------------------------------------
/** Take everything that has finished stopping off one list and free it, holding that list's lock
	* so the Miles thread is not walking it at the same moment. */
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releasePlayingAudioInListIfStopped( std::list<PlayingAudio *> &list, CriticalSectionClass &cs )
{
	CriticalSectionClass::LockClass lock(cs);

	std::list<PlayingAudio *>::iterator it;
	for (it = list.begin(); it != list.end(); )
	{
		PlayingAudio *playing = (*it);
		if (playing == NULL || playing->m_status == PS_Stopped)
		{
			if (playing)
				releasePlayingAudio(playing);
			it = list.erase(it);
			continue;
		}
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAllAudioImmediately( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	// Stop them all first, with no lock held, because that is the half that calls Miles.
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		if (playing)
			stopPlayingAudio(playing);
	}

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = (*it);
		if (playing)
			stopPlayingAudio(playing);
	}

	// Then take them off, holding each list's lock.
	releasePlayingAudioInListIfStopped(m_playingSounds, m_playingSoundsCS);
	releasePlayingAudioInListIfStopped(m_playing3DSounds, m_playing3DSoundsCS);
	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);
	releasePlayingAudioInListIfStopped(m_fadingAudio, m_fadingAudioCS);


	std::list<HAUDIO>::iterator hit;
	for (hit = m_audioForcePlayed.begin(); hit != m_audioForcePlayed.end(); ++hit) {
		if (*hit) {
			AIL_quick_unload(*hit);
		}
	}

	m_audioForcePlayed.clear();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::freeAllMilesHandles( void )
{
	// First, we need to ensure that we don't have any sample handles open. To that end, we must stop
	// all of our currently playing audio.
	stopAllAudioImmediately();

	// Walks through the available 2-D and 3-D handles and releases them
	std::list<HSAMPLE>::iterator it;
	for ( it = m_availableSamples.begin(); it != m_availableSamples.end(); /* empty */ ) {
		HSAMPLE sample = *it;
		AIL_release_sample_handle(sample);
		it = m_availableSamples.erase(it);
	}
	m_num2DSamples = 0;
	
	std::list<H3DSAMPLE>::iterator it3D;
	for ( it3D = m_available3DSamples.begin(); it3D != m_available3DSamples.end(); /* empty */ ) {
		H3DSAMPLE sample3D = *it3D;
		AIL_release_3D_sample_handle(sample3D);
		it3D = m_available3DSamples.erase(it3D);
	}
	m_num3DSamples = 0;
	m_numStreams = 0;
}

//-------------------------------------------------------------------------------------------------
HSAMPLE MilesAudioManager::getFirst2DSample( AudioEventRTS *event )
{
	if (m_availableSamples.begin() != m_availableSamples.end()) {
		HSAMPLE retSample = *m_availableSamples.begin();
		m_availableSamples.erase(m_availableSamples.begin());
		return (retSample);
	}

	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle

	return NULL;
}

//-------------------------------------------------------------------------------------------------
H3DSAMPLE MilesAudioManager::getFirst3DSample( AudioEventRTS *event )
{
	if (m_available3DSamples.begin() != m_available3DSamples.end()) {
		H3DSAMPLE retSample = *m_available3DSamples.begin();
		m_available3DSamples.erase(m_available3DSamples.begin());
		return (retSample);
	}

	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle
	return NULL;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustPlayingVolume( PlayingAudio *audio )
{
	Real desiredVolume = audio->m_audioEventRTS->getVolume() * audio->m_audioEventRTS->getVolumeShift();
	Real pan;
	if (audio->m_type == PAT_Sample) {
		AIL_sample_volume_pan(audio->m_sample, NULL, &pan);
		AIL_set_sample_volume_pan(audio->m_sample, getVoiceMixedVolume(audio->m_audioEventRTS, m_soundVolume), pan);

	} else if (audio->m_type == PAT_3DSample) {
		AIL_set_3D_sample_volume(audio->m_3DSample, getVoiceMixedVolume(audio->m_audioEventRTS, m_sound3DVolume));

	} else if (audio->m_type == PAT_Stream) {
		AIL_stream_volume_pan(audio->m_stream, NULL, &pan);
		if (audioIsType(audio->m_audioEventRTS, AT_Music)) {
			AIL_set_stream_volume_pan(audio->m_stream, getVoiceMixedVolume(audio->m_audioEventRTS, m_musicVolume), pan);
			
		} else {
			AIL_set_stream_volume_pan(audio->m_stream, m_speechVolume * desiredVolume, pan);
			
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::stopAllSpeech( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);
		if (!playing) {
			continue;
		}

		if (audioIsType(playing->m_audioEventRTS, AT_Streaming)) {
			stopPlayingAudio(playing);
		}
		++it;
	}

	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initFilters( HSAMPLE sample, const AudioEventRTS *event )
{
	// set the sample volume
	Real volume = getVoiceMixedVolume(event, m_soundVolume);
	AIL_set_sample_volume_pan(sample, volume, 0.5f);

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		AIL_set_sample_playback_rate(sample, REAL_TO_INT(AIL_sample_playback_rate(sample) * pitchShift));
	}

	// set up delay filter, if applicable
	if (event->getDelay() > 0.0f) {
		Real value;
		value = event->getDelay();
		AIL_set_sample_processor(sample, DP_FILTER, m_delayFilter);
		AIL_set_filter_sample_preference(sample, "Mono Delay Time", &value);

		value = 0.0;
		AIL_set_filter_sample_preference(sample, "Mono Delay", &value);
		AIL_set_filter_sample_preference(sample, "Mono Delay Mix", &value);		
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initFilters3D( H3DSAMPLE sample, const AudioEventRTS *event, const Coord3D *pos )
{
	// set the sample volume
	Real volume = getVoiceMixedVolume(event, m_sound3DVolume);
	AIL_set_3D_sample_volume(sample, volume);

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		AIL_set_3D_sample_playback_rate(sample, REAL_TO_INT(AIL_3D_sample_playback_rate(sample) * pitchShift));
	}
	
	// Low pass filter
	if (event->getAudioEventInfo()->m_lowPassFreq > 0 && !isOnScreen(pos) ) {
		AIL_set_3D_sample_occlusion(sample, 1.0f - event->getAudioEventInfo()->m_lowPassFreq);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::nextMusicTrack( void )
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::prevMusicTrack( void )
{
	AsciiString trackName;
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music 
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isMusicPlaying( void ) const
{
	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const
{
	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			if (playing->m_audioEventRTS->getEventName() == trackName) {
				if (INFINITE_LOOP_COUNT - AIL_stream_loop_count(playing->m_stream) >= numberOfTimes) {
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
AsciiString MilesAudioManager::getMusicTrackName( void ) const
{
	// First check the requests. If there's one there, then report that as the currently playing track.
	std::list<AudioRequest *>::const_iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		if ((*ait)->m_request != AR_Play) {
			continue;
		}

		if (!(*ait)->m_usePendingEvent) {
			continue;
		}

		if (audioIsType((*ait)->m_pendingEvent, AT_Music)) {
			return (*ait)->m_pendingEvent->getEventName();
		}
	}

	std::list<PlayingAudio *>::const_iterator it;
	PlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && audioIsType(playing->m_audioEventRTS, AT_Music)) {
			return playing->m_audioEventRTS->getEventName();
		}
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::openDevice( void )
{
	if (!TheGlobalData->m_audioOn) {
		return;
	}
	
	AIL_set_redist_directory("MSS\\");
	// -wav records from a mix the logic clock moves (updateSoundCapture), so the run plays no sound
	// and needs no audio device.
	if (TheGlobalData->m_wavEndFrame > 0)
		AIL_ex_offline_mix();
	AIL_startup();
	Int retval = 0;

	// AIL_quick_startup should be replaced later with a call to actually pick which device to use, etc
	const AudioSettings *audioSettings = getAudioSettings();
	AIL_ex_set_3D_linear_falloff(audioSettings->m_rangeVolumeFade);
	m_selectedSpeakerType = TheAudio->translateSpeakerTypeToUnsignedInt(m_prefSpeaker);

	retval = AIL_quick_startup(audioSettings->m_useDigital, audioSettings->m_useMidi, audioSettings->m_outputRate, audioSettings->m_outputBits, audioSettings->m_outputChannels);
	
	// Quick handles tells us where to store the various devices. For now, we're only interested in the digital handle.
	AIL_quick_handles(&m_digitalHandle, NULL, NULL);
	
	if (retval) {
		buildProviderList();
	} else {
		// if we couldn't initialize any devices, turn sound off (fail silently)
		setOn( false, AudioAffect_All );
	}

	selectProvider(TheAudio->getProviderIndex(m_pref3DProvider));

	// Now that we're all done, update the cached variables so that everything is in sync.
	TheAudio->refreshCachedVariables();

	if (!isValidProvider()) {
		return;
	}

	initDelayFilter();
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::closeDevice( void )
{
	freeAllMilesHandles();
	unselectProvider();
	AIL_shutdown();
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isCurrentlyPlaying( AudioHandle handle )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	// if something is requested, it is also considered playing
	std::list<AudioRequest *>::iterator ait;
	AudioRequest *req = NULL;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		req = *ait;
		if (req && req->m_usePendingEvent && req->m_pendingEvent->getPlayingHandle() == handle) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::queueAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	CriticalSectionClass::LockClass lock(m_completedAudioCS);
	CompletedAudio completed;
	completed.handle = audioCompleted;
	completed.flags = flags;
	completed.startsAtCompletion = m_audioStarts[audioCompleted];
	m_completedAudio.push_back(completed);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::noteAudioStarted( UnsignedIntPtr handle )
{
	CriticalSectionClass::LockClass lock(m_completedAudioCS);
	++m_audioStarts[handle];
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processCompletedAudio( void )
{
	std::list<CompletedAudio> completedAudio;
	{
		CriticalSectionClass::LockClass lock(m_completedAudioCS);
		completedAudio.swap(m_completedAudio);
	}

	for (std::list<CompletedAudio>::const_iterator it = completedAudio.begin(); it != completedAudio.end(); ++it) {
		Bool restartedSince;
		{
			CriticalSectionClass::LockClass lock(m_completedAudioCS);
			restartedSince = m_audioStarts[it->handle] != it->startsAtCompletion;
		}
		if (!restartedSince) {
			notifyOfAudioCompletion(it->handle, it->flags);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::notifyOfAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	PlayingAudio *playing = findPlayingAudioFrom(audioCompleted, flags);
	if (!playing) {
		DEBUG_CRASH(("Audio has completed playing, but we can't seem to find it. - jkmcd"));
		return;
	}
	
	if (getDisallowSpeech() && audioIsType(playing->m_audioEventRTS, AT_Streaming)) {
		setDisallowSpeech(FALSE);
	}

	if (playing->m_audioEventRTS->getAudioEventInfo()->m_control & AC_LOOP) {
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Attack) {
			playing->m_audioEventRTS->setNextPlayPortion(PP_Sound);
		}
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Sound) {
			// First, decrease the loop count.
			playing->m_audioEventRTS->decreaseLoopCount();

			// Now, try to start the next loop
			if (startNextLoop(playing)) {
				return;
			}
		}
	}

	playing->m_audioEventRTS->advanceNextPlayPortion();
	if (playing->m_audioEventRTS->getNextPlayPortion() != PP_Done) {
		if (playing->m_type == PAT_Sample) {
			closeFile(playing->m_file);	// close it so as not to leak it.
			playing->m_file = playSample(playing->m_audioEventRTS, playing->m_sample);
			
			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_file) {
				return;
			}
		} else if (playing->m_type == PAT_3DSample) {
			closeFile(playing->m_file);	// close it so as not to leak it.
			playing->m_file = playSample3D(playing->m_audioEventRTS, playing->m_3DSample);

			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_file) {
				return;
			}
		} 
	}

	if (playing->m_type == PAT_Stream) {
		if (audioIsType(playing->m_audioEventRTS, AT_Music)) {
			playStream(playing->m_audioEventRTS, playing->m_stream);
			
			return;
		}
	}

	// Ask for the stop rather than declaring it: this runs on the Miles thread, and the half that
	// talks back to Miles and frees the object belongs to the main thread's next sweep.
#if defined(_WIN32)
	InterlockedCompareExchange( (volatile long *)&playing->m_status, PS_Stopping, PS_Playing );
#else
	playingStatusCompareExchange( &playing->m_status, PS_Stopping, PS_Playing );
#endif
}

//-------------------------------------------------------------------------------------------------
PlayingAudio *MilesAudioManager::findPlayingAudioFrom( UnsignedIntPtr audioCompleted, UnsignedInt flags )
{
	// Miles calls in here from its own timer thread when a sample finishes, so every walk of these
	// lists is guarded against the main thread taking an element out from under it.
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	if (flags == PAT_Sample) {
		HSAMPLE sample = (HSAMPLE) audioCompleted;
		CriticalSectionClass::LockClass lock(m_playingSoundsCS);
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_sample == sample) {
				return playing;
			}
		}
	}

	if (flags == PAT_3DSample) {
		H3DSAMPLE sample3D = (H3DSAMPLE) audioCompleted;
		CriticalSectionClass::LockClass lock(m_playing3DSoundsCS);
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_3DSample == sample3D) {
				return playing;
			}
		}
	}

	if (flags == PAT_Stream) {
		HSTREAM stream = (HSTREAM) audioCompleted;
		{
			CriticalSectionClass::LockClass lock(m_playingStreamsCS);
			for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
				playing = *it;
				if (playing && playing->m_stream == stream) {
					return playing;
				}
			}
		}
		// A stream that is fading has moved lists, and its callback still arrives.
		{
			CriticalSectionClass::LockClass lock(m_fadingAudioCS);
			for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
				playing = *it;
				if (playing && playing->m_stream == stream) {
					return playing;
				}
			}
		}
	}

	return NULL;
}


//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getProviderCount( void ) const
{
	return m_providerCount;
}

//-------------------------------------------------------------------------------------------------
AsciiString MilesAudioManager::getProviderName( UnsignedInt providerNum ) const
{
	if (isOn(AudioAffect_Sound3D) && providerNum < m_providerCount) {
		return m_provider3D[providerNum].name;
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getProviderIndex( AsciiString providerName ) const
{
	for (UnsignedInt i = 0; i < m_providerCount; ++i) {
		if (providerName == m_provider3D[i].name) {
			return i;
		}
	}

	return PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::selectProvider( UnsignedInt providerNdx )
{
	if (!isOn(AudioAffect_Sound3D)) 
	{
		return;
	}

	if (providerNdx == m_selectedProvider) 
	{
		return;
	}

	if (isValidProvider()) 
	{
		freeAllMilesHandles();
		unselectProvider();
	}

#if defined(_WIN32)
	/* Never runs, on Windows either: AIL_get_DirectSound_info answers NULL in both backends under this
		 manager (XAudio2's and miniaudio's), so useDolby stays FALSE.  Kept on Windows as it was; off
		 Windows there is no DirectSound to name (C4). */
	LPDIRECTSOUND lpDirectSoundInfo;
	AIL_get_DirectSound_info( NULL, (void**)&lpDirectSoundInfo, NULL );
#endif
	Bool useDolby = FALSE;
#if defined(_WIN32)
	if( lpDirectSoundInfo )
	{
		DWORD speakerConfig;
		lpDirectSoundInfo->GetSpeakerConfig( &speakerConfig );
		switch( DSSPEAKER_CONFIG( speakerConfig ) )
		{
			case DSSPEAKER_DIRECTOUT:
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_MONO:     
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_STEREO:   
				m_selectedSpeakerType = AIL_3D_2_SPEAKER;
				break;
			case DSSPEAKER_HEADPHONE:
				m_selectedSpeakerType = AIL_3D_HEADPHONE;
				useDolby = TRUE;
				break;
			case DSSPEAKER_QUAD:     
				m_selectedSpeakerType = AIL_3D_4_SPEAKER;
				useDolby = TRUE;
				break;
			case DSSPEAKER_SURROUND:
				m_selectedSpeakerType = AIL_3D_SURROUND;
				useDolby = TRUE;
				break;
			case DSSPEAKER_5POINT1:  
				m_selectedSpeakerType = AIL_3D_51_SPEAKER;
				useDolby = TRUE;
				break;
			case DSSPEAKER_7POINT1:  
				m_selectedSpeakerType = AIL_3D_71_SPEAKER;
				useDolby = TRUE;
				break;
		}
	}
#endif

	Bool success = FALSE;
	if( useDolby )
	{
		providerNdx = getProviderIndex( "Dolby Surround" );
	}
	else
	{
		providerNdx = getProviderIndex( "Miles Fast 2D Positional Audio" );
	}
	success = providerNdx < m_providerCount && AIL_open_3D_provider( m_provider3D[providerNdx].id ) == 0;

	//if (providerNdx < m_providerCount) 
	//{
	//	failed = AIL_open_3D_provider(m_provider3D[providerNdx].id);
	//}



	if( !success ) 
	{
		m_selectedProvider = PROVIDER_ERROR;
		// try to select a failsafe
		providerNdx = getProviderIndex( "Miles Fast 2D Positional Audio" );
		success = providerNdx < m_providerCount && AIL_open_3D_provider( m_provider3D[providerNdx].id ) == 0;
	}

	if ( success )
	{
		m_selectedProvider = providerNdx;
	
		initSamplePools();
		
		createListener();
		setSpeakerType(m_selectedSpeakerType);
		if (TheVideoPlayer) 
		{
			TheVideoPlayer->notifyVideoPlayerOfNewProvider(TRUE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::unselectProvider( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	if (TheVideoPlayer) {
		TheVideoPlayer->notifyVideoPlayerOfNewProvider(FALSE);
	}
	AIL_close_3D_listener(m_listener);
	m_listener = NULL;

	AIL_close_3D_provider(m_provider3D[m_selectedProvider].id);
	m_lastProvider = m_selectedProvider;

	m_selectedProvider = PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getSelectedProvider( void ) const
{
	return m_selectedProvider;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setSpeakerType( UnsignedInt speakerType )
{
	if (!isValidProvider()) {
		return;
	}
	
	AIL_set_3D_speaker_type( m_provider3D[m_selectedProvider].id, speakerType );
	m_selectedSpeakerType = speakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getSpeakerType( void )
{
	if (!isValidProvider()) {
		return 0;
	}

	return m_selectedSpeakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNum2DSamples( void ) const
{
	return m_num2DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNum3DSamples( void ) const
{
	return m_num3DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt MilesAudioManager::getNumStreams( void ) const
{
	return m_numStreams;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::doesViolateLimit( AudioEventRTS *event ) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) {
		return false;
	}

	Int totalCount = 0;
	Int totalRequestCount = 0;

	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	}
	
	// Also check the request list in case we've requested to play this sound.
	std::list<AudioRequest*>::const_iterator arIt;
	for (arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest *req = (*arIt);
		if (req == NULL) {
			continue;
		}
		if( req->m_usePendingEvent ) 
		{
			if( req->m_pendingEvent->getEventName() == event->getEventName() ) 
			{
				totalRequestCount++;
				totalCount++;
			}
		}
	}

	//If our event is an interrupting type, then normally we would always add it. The exception is when we have requested
	//multiple sounds in the same frame and those requests violate the limit. Because we don't have any "old" sounds to
	//remove in the case of an interrupt, we need to catch it early and prevent the sound from being added if we already
	//reached the limit
	if( event->getAudioEventInfo()->m_control & AC_INTERRUPT )
	{
		if( totalRequestCount < limit )
		{
			Int totalPlayingCount = totalCount - totalRequestCount;
			if( totalRequestCount + totalPlayingCount < limit )
			{
				//We aren't exceeding the actual limit, then clear the kill handle.
				event->setHandleToKill(0);
				return false;
			}

			//We are exceeding the limit - the kill handle will kill the
			//oldest playing sound to enforce the actual limit.
			return false;
		}
	}

	if( totalCount < limit ) 
	{
		event->setHandleToKill(0);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isPlayingAlready( AudioEventRTS *event ) const
{
	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isObjectPlayingVoice( UnsignedInt objID ) const
{
	if (objID == 0) {
		return false;
	}

	std::list<PlayingAudio *>::const_iterator it;
		// 2-D
	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	// 3-D
	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** A unit's reply is one short line, and in a fight it was lost under the guns.  It plays at the
	* louder of the effects and voice sliders, lifted again on top of that up to full scale.  Nothing
	* else is turned down for it.  EVA and briefing speech is left where the voice slider puts it. */
Real MilesAudioManager::getVoiceMixedVolume( const AudioEventRTS *event, Real sliderVolume ) const
{
	static const Real VOICE_BOOST = 1.6f;

	// A sound can reach here with no info (see audioIsType): v2.0.0 read m_type through it and a
	// looping sound's next pass died at address 0x40 on the XAudio2 service thread.
	const AudioEventInfo *info = event->getAudioEventInfo();
	const Real eventVolume = event->getVolume() * event->getVolumeShift();
	// A reply is heard in the ear, not out on the field.  A world sound flagged as voice - a soldier's
	// death cry - kept its place in the battle before this boost and keeps it now.
	if (info != NULL && (info->m_type & ST_VOICE) && !event->isPositionalAudio()) {
		return min( 1.0f, eventVolume * max( sliderVolume, m_speechVolume ) * VOICE_BOOST );
	}

	// Ambience answers to its own slider, whichever effects slider the caller passed.
	if (isAmbientSound( info )) {
		return eventVolume * getAmbientVolume( event->isPositionalAudio() );
	}

	return eventVolume * sliderVolume;
}

//-------------------------------------------------------------------------------------------------
AudioEventRTS* MilesAudioManager::findLowestPrioritySound( AudioEventRTS *event )
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return NULL;
	}
	AudioEventRTS *lowestPriorityEvent = NULL;
	AudioPriority lowestPriority = priority;

	const std::list<PlayingAudio *> &playingList = event->isPositionalAudio() ? m_playing3DSounds : m_playingSounds;
	std::list<PlayingAudio *>::const_iterator it;
	for( it = playingList.begin(); it != playingList.end(); ++it )
	{
		if( !(*it)->isPlaying() )
		{
			continue;
		}
		AudioEventRTS *itEvent = (*it)->m_audioEventRTS;
		AudioPriority itPriority = itEvent->getAudioEventInfo()->m_priority;
		if( itPriority < lowestPriority )
		{
			lowestPriorityEvent = itEvent;
			lowestPriority = itPriority;
			if( lowestPriority == AP_LOWEST )
			{
				return lowestPriorityEvent;
			}
		}
	}
	return lowestPriorityEvent;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isPlayingLowerPriority( AudioEventRTS *event ) const
{
	//We don't actually want to do anything to this CONST function. Remember, we're
	//just checking to see if there is a lower priority sound.
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return false;
	}
	std::list<PlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if ((*it)->isPlaying() && (*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::killLowestPrioritySoundImmediately( AudioEventRTS *event )
{
	//Actually, we want to kill the LOWEST PRIORITY SOUND, not the first "lower" priority
	//sound we find, because it could easily be 
	AudioEventRTS *lowestPriorityEvent = findLowestPrioritySound( event );
	if( lowestPriorityEvent )
	{
		std::list<PlayingAudio *>::iterator it;
		if( event->isPositionalAudio() )
		{
			for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) 
			{
				PlayingAudio *playing = (*it);
				if( !playing ) 
				{
					continue;
				}

				if( playing->isPlaying() && playing->m_audioEventRTS == lowestPriorityEvent )
				{
					//Release this 3D sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio( playing );
					releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );
					return TRUE;
				}
			}
		}
		else
		{
			for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
			{
				PlayingAudio *playing = (*it);
				if( !playing )
				{
					continue;
				}

				if( playing->isPlaying() && playing->m_audioEventRTS == lowestPriorityEvent )
				{
					//Release this 2D sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio( playing );
					releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	Real pan;
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_sample_volume_pan(playing->m_sample, NULL, &pan);
			AIL_set_sample_volume_pan(playing->m_sample, desiredVolume, pan);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_set_3D_sample_volume(playing->m_3DSample, desiredVolume);
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->isPlaying() && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			AIL_stream_volume_pan(playing->m_stream, NULL, &pan);
			AIL_set_stream_volume_pan(playing->m_stream, desiredVolume, pan);
		}
	}
}


//-------------------------------------------------------------------------------------------------
void MilesAudioManager::removePlayingAudio( AsciiString eventName )
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::removeAllDisabledAudio()
{
	std::list<PlayingAudio *>::iterator it;

	PlayingAudio *playing = NULL;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingSounds, m_playingSoundsCS );

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playing3DSounds, m_playing3DSoundsCS );

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ) 
	{
		playing = *it;
		if( playing && playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio( playing );
		}
		it++;
	}
	releasePlayingAudioInListIfStopped( m_playingStreams, m_playingStreamsCS );
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processRequestList( void )
{
	std::list<AudioRequest*>::iterator it;
	for (it = m_audioRequests.begin(); it != m_audioRequests.end(); /* empty */) {
		AudioRequest *req = (*it);
		if (req == NULL) {
			continue;
		}

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req)) {
			processRequest(req);
		}
		req->deleteInstance();
		it = m_audioRequests.erase(it);
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processPlayingList( void )
{
	// Two passes.  The first one talks to Miles and holds no lock; it only ever writes a status.
	// The second takes the finished ones off the lists, holding each list's own lock, and calls
	// nothing in Miles.  The whole point of the split is that the Miles timer thread walks these
	// same lists from notifyOfAudioCompletion.
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = (*it);
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
			continue;

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it)
	{
		playing = (*it);
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
		{
		}
		else
		{
			if (m_volumeHasChanged) 
			{
				adjustPlayingVolume(playing);
			}

			const Coord3D *pos = getCurrentPositionFromEvent(playing->m_audioEventRTS);
			if (pos) 
			{
				if( playing->m_audioEventRTS->isDead() )
				{
					stopAudioEvent( playing->m_audioEventRTS->getPlayingHandle() );
					continue;
				}
				else
				{
					Real volForConsideration = getEffectiveVolume(playing->m_audioEventRTS);
					// the slider is taken back out: a sound is culled by distance, not by how far a slider is down
					const Real slider = isAmbientSound( playing->m_audioEventRTS->getAudioEventInfo() ) ? getAmbientVolume( TRUE ) : m_sound3DVolume;
					volForConsideration /= (slider > 0.0f ? slider : 1.0f);
					Bool playAnyways = BitTest( playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_GLOBAL) || playing->m_audioEventRTS->getAudioEventInfo()->m_priority == AP_CRITICAL;
					if( volForConsideration < m_audioSettings->m_minVolume && !playAnyways )
					{
						// don't want to get an additional callback for this sample
						AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
						stopPlayingAudio( playing );
						continue;
					}
					// canPlayNow keeps a shrouded sound from starting in the fog, but a loop that started
					// in the open played on after its source flew into the fog: an enemy Helix or Comanche
					// could be heard, and followed, where it could not be seen.  Stopped like one out of
					// range, and the drawable starts it again once the spot is clear.
					else if( !playAnyways
						&& BitTest( playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_SHROUDED )
						&& playing->m_audioEventRTS->getAudioEventInfo()->isPermanentSound()
						&& ThePartitionManager->getShroudStatusForPlayer( TheObserverCamera.getShroudPlayerIndex(), pos ) != CELLSHROUD_CLEAR )
					{
						AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
						stopPlayingAudio( playing );
						continue;
					}
					else 
					{
						Real x = pos->x;
						Real y = pos->y;
						Real z = pos->z;
						AIL_set_3D_position( playing->m_3DSample, x, y, z );
					}
				}
			} 
			else 
			{
				AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
				stopPlayingAudio( playing );
				continue;
			}
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		if (!playing)
			continue;

		if (playing->m_status == PS_Stopping)
		{
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status == PS_Stopped)
			continue;

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	if (m_volumeHasChanged) {
		m_volumeHasChanged = false;
		TheVideoPlayer->setVolume(getVolume(AudioAffect_Speech));
	}

	//
	// Second pass: take the finished ones off, holding each list's own lock.  Nothing here calls
	// into Miles, which is what makes it safe to hold the lock.
	//
	releasePlayingAudioInListIfStopped(m_playingSounds, m_playingSoundsCS);
	releasePlayingAudioInListIfStopped(m_playing3DSounds, m_playing3DSoundsCS);
	releasePlayingAudioInListIfStopped(m_playingStreams, m_playingStreamsCS);

	//
	// And move anything that asked to fade across, holding both lists.
	//
	{
		CriticalSectionClass::LockClass streamLock(m_playingStreamsCS);
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
			playing = (*it);
			if (playing && playing->m_fade)
			{
				{
					CriticalSectionClass::LockClass fadeLock(m_fadingAudioCS);
					m_fadingAudio.push_back(playing);
				}
				it = m_playingStreams.erase(it);
				continue;
			}
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processFadingList( void )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;

	// Same two passes as processPlayingList, for the same reason.
	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			stopPlayingAudio( playing );
			continue;
		}

		if (playing->m_status != PS_Playing) {
			stopPlayingAudio( playing );
			continue;
		}

		++playing->m_framesFaded;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS);
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);

		switch(playing->m_type)
		{
			case PAT_Sample:
			{
				AIL_set_sample_volume_pan(playing->m_sample, volume, 0.5f);
				break;
			}

			case PAT_3DSample:
			{
				// Not volume: that carries the distance falloff, which the backend applies again.
				AIL_set_3D_sample_volume(playing->m_3DSample, getVoiceMixedVolume(playing->m_audioEventRTS, m_sound3DVolume)
					* (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames));
				break;
			}
			
			case PAT_Stream:
			{
				AIL_set_stream_volume_pan(playing->m_stream, volume, 0.5f);
				break;
			}
			
		}
	}

	releasePlayingAudioInListIfStopped(m_fadingAudio, m_fadingAudioCS);
}


//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::shouldProcessRequestThisFrame( AudioRequest *req ) const
{
	if (!req->m_usePendingEvent) {
		return true;
	}

	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) {
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::adjustRequest( AudioRequest *req )
{
	if (!req->m_usePendingEvent) {
		return;
	}

	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::checkForSample( AudioRequest *req )
{
	if (!req->m_usePendingEvent) {
		return true;
	}

  if ( req->m_pendingEvent->getAudioEventInfo() == NULL )
  {
    // Fill in event info
    getInfoForAudioEvent( req->m_pendingEvent );
  }
  
	if (req->m_pendingEvent->getAudioEventInfo()->m_type != AT_SoundEffect) 
  {
		return true;
	}

	return m_sound->canPlayNow(req->m_pendingEvent);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setHardwareAccelerated(Bool accel)
{
	// Extends
	Bool retEarly = (accel == m_hardwareAccel);
	AudioManager::setHardwareAccelerated(accel);

	if (retEarly) {
		return;
	}
	
	if (m_hardwareAccel) {
		for (Int i = 0; i < MAX_HW_PROVIDERS; ++i) {
			UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[i]);
			TheAudio->selectProvider(providerNdx);
			if (getSelectedProvider() == providerNdx) {
				return;
			}
		}
	}

	// set it false
	AudioManager::setHardwareAccelerated(FALSE);
	UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS]);
	TheAudio->selectProvider(providerNdx);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setSpeakerSurround(Bool surround)
{
	// Extends
	Bool retEarly = (surround == m_surroundSpeakers);
	AudioManager::setSpeakerSurround(surround);

	if (retEarly) {
		return;
	}

	UnsignedInt speakerType;
	if (m_surroundSpeakers) {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType3D;
	} else {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType2D;
	}

	TheAudio->setSpeakerType(speakerType);
}

//-------------------------------------------------------------------------------------------------
Real MilesAudioManager::getFileLengthMS( AsciiString strToLoad ) const
{
	if (strToLoad.isEmpty()) {
		return 0.0f;
	}

	//
	// The file's own header first, because two script conditions are built on this answer and a
	// machine with no sound device - a -headless run is one - cannot ask Miles anything.  See
	// AudioManager::getWaveFileLengthMS.  Miles still answers for whatever is not a .wav.
	//
	const Real fromHeader = getWaveFileLengthMS(strToLoad);
	if (fromHeader > 0.0f) {
		return fromHeader;
	}

	// Load it as a stream to get the file info without actually opening the file.
	HSTREAM stream = AIL_open_stream(m_digitalHandle, strToLoad.str(), 0);
	if (!stream) {
		return 0.0f;
	}

	long retVal;
	AIL_stream_ms_position(stream, &retVal, NULL);
	// Now close the stream
	AIL_close_stream(stream);

	return INT_TO_REAL(retVal);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::closeAnySamplesUsingFile( const void *fileToClose )
{
	std::list<PlayingAudio *>::iterator it;
	PlayingAudio *playing;
	
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_file == fileToClose) {
			stopPlayingAudio(playing);
		}
		++it;
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		playing = *it;
		if (!playing) {
			++it;
			continue;
		}

		if (playing->m_file == fileToClose) {
			stopPlayingAudio(playing);
		}
		++it;
	}

	releasePlayingAudioInListIfStopped(m_playingSounds, m_playingSoundsCS);
	releasePlayingAudioInListIfStopped(m_playing3DSounds, m_playing3DSoundsCS);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::setDeviceListenerPosition( void )
{
	if (m_listener) {
		AIL_set_3D_orientation(m_listener, m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z, 0, 0, -1);

		Real x = m_listenerPosition.x;
		Real y = m_listenerPosition.y;
		Real z = m_listenerPosition.z;
		AIL_set_3D_position( m_listener, x, y, z );
	}
}

//-------------------------------------------------------------------------------------------------
const Coord3D *MilesAudioManager::getCurrentPositionFromEvent( AudioEventRTS *event )
{
	if (!event->isPositionalAudio()) {
		return NULL;
	}

	return event->getCurrentPosition();
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isOnScreen( const Coord3D *pos ) const
{
	static ICoord2D dummy;
	// WorldToScreen will return True if the point is onscreen and false if it is offscreen.
	return TheTacticalView->worldToScreen(pos, &dummy);
}

//-------------------------------------------------------------------------------------------------
Real MilesAudioManager::getEffectiveVolume(AudioEventRTS *event) const
{
	Real volume = 1.0f;
	volume *= (event->getVolume() * event->getVolumeShift());
	if (event->getAudioEventInfo()->m_soundType == AT_Music) 
	{
		volume *= m_musicVolume;
	} 
	else if (event->getAudioEventInfo()->m_soundType == AT_Streaming) 
	{
		volume *= m_speechVolume;
	} 
	else 
	{
		const Bool ambient = isAmbientSound( event->getAudioEventInfo() );
		if (event->isPositionalAudio())
		{
			volume *= ambient ? getAmbientVolume( TRUE ) : m_sound3DVolume;
			Coord3D distance = m_listenerPosition;
			const Coord3D *pos = event->getCurrentPosition();
			if (pos) 
			{
				distance.sub(pos);
				Real objMinDistance;
				Real objMaxDistance;

				if (event->getAudioEventInfo()->m_type & ST_GLOBAL) 
				{
					objMinDistance = TheAudio->getAudioSettings()->m_globalMinRange;
					objMaxDistance = TheAudio->getAudioSettings()->m_globalMaxRange;
				} 
				else 
				{
					objMinDistance = event->getAudioEventInfo()->m_minDistance;
					objMaxDistance = event->getAudioEventInfo()->m_maxDistance;
				}

				Real objDistance = distance.length();

				// `RangeVolumeFade = Yes` in AudioSettings.ini uses the linear fade EA left
				// commented out right here. The default curve is 1/distance, which never reaches
				// zero, so the hard cut at the maximum range makes a sound you are walking away
				// from stop dead instead of fading out. The linear one reaches zero exactly where
				// the cut is. Off by default: it changes how every 3D sound in the game attenuates,
				// and that is a listening decision, not a bug fix.  The curve is the audio backend's
				// own, so a sound is culled on the same curve it is heard at.
				volume *= AIL_ex_3D_distance_gain( objDistance, objMinDistance, objMaxDistance,
					TheAudio->getAudioSettings()->m_rangeVolumeFade );
			}
		} 
		else
		{
			volume *= ambient ? getAmbientVolume( FALSE ) : m_soundVolume;
		}
	}

	return volume;
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::startNextLoop( PlayingAudio *looping )
{
	closeFile(looping->m_file);
	looping->m_file = NULL;

	if (!looping->isPlaying() || looping->m_requestStop) {
		return false;
	}

	if (looping->m_audioEventRTS->hasMoreLoops()) {
		// generate a new filename, and test to see whether we can play with it now
		looping->m_audioEventRTS->generateFilename();
		
		if (looping->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
			// fake it out so that this sound appears done, but also so that it will not
			// delete the sound on completion (which would suck)
			looping->m_cleanupAudioEventRTS = false;
#if defined(_WIN32)
			InterlockedCompareExchange( (volatile long *)&looping->m_status, PS_Stopping, PS_Playing );
#else
			playingStatusCompareExchange( &looping->m_status, PS_Stopping, PS_Playing );
#endif
			
			
			AudioRequest *req = allocateAudioRequest(true);
			req->m_pendingEvent = looping->m_audioEventRTS;
			req->m_requiresCheckForSample = true;
			appendAudioRequest(req);
			return true;
		}

		if (looping->m_type == PAT_3DSample) {
			looping->m_file = playSample3D(looping->m_audioEventRTS, looping->m_3DSample);
		} else {
			looping->m_file = playSample(looping->m_audioEventRTS, looping->m_sample);
		}
		
		return looping->m_file != NULL;
	} 
	return false;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::playStream( AudioEventRTS *event, HSTREAM stream )
{
	// Force it to the beginning
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		AIL_set_stream_loop_count(stream, INFINITE_LOOP_COUNT);
	}

	AIL_register_stream_callback(stream, setStreamCompleted);
	noteAudioStarted((UnsignedIntPtr) stream);	// before the start: a stream that ends at once still counts as this one
	AIL_start_stream(stream);
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		// Need to stop/fade out the old music here.
	}
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::playSample( AudioEventRTS *event, HSAMPLE sample )
{
	AIL_init_sample(sample);

	// Prep any sort of filtering, etc, here
	AIL_register_EOS_callback(sample, setSampleCompleted);
	initFilters(sample, event);

	// Load the file in
	void *fileBuffer = NULL;
	fileBuffer = loadFileForRead(event);
	if (fileBuffer) {
		AIL_set_sample_file(sample, fileBuffer, 0);

		// Start playback
		noteAudioStarted((UnsignedIntPtr) sample);
		AIL_start_sample(sample);
	}

	return fileBuffer;
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::playSample3D( AudioEventRTS *event, H3DSAMPLE sample3D )
{
	const Coord3D *pos = getCurrentPositionFromEvent(event);
	if (pos) {
		// Load the file in
		void *fileBuffer = loadFileForRead(event);
		if (fileBuffer) {
			AIL_set_3D_sample_file(sample3D, fileBuffer);

			// Prep any sort of filtering, etc, here
			AIL_register_3D_EOS_callback(sample3D, set3DSampleCompleted);

			// Set the position values of the sample here.  Miles takes the maximum first; EA passed the
			// minimum first, and with the pair backwards every world sound played at full volume out to
			// its cut-off, so far-reaching ambient loops drowned the fighting next to the camera.
			if (event->getAudioEventInfo()->m_type & ST_GLOBAL) {
				AIL_set_3D_sample_distances(sample3D, TheAudio->getAudioSettings()->m_globalMaxRange, TheAudio->getAudioSettings()->m_globalMinRange );
			} else {
				AIL_set_3D_sample_distances(sample3D, event->getAudioEventInfo()->m_maxDistance, event->getAudioEventInfo()->m_minDistance );
			}
			
			// Set the position of the sample here
			Real x = pos->x;
			Real y = pos->y;
			Real z = pos->z;
			AIL_set_3D_position( sample3D, x, y, z );
			initFilters3D(sample3D, event, pos);
			
			// Start playback
			noteAudioStarted((UnsignedIntPtr) sample3D);
			AIL_start_3D_sample(sample3D);
		}
		return fileBuffer;
	}

	return NULL;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::buildProviderList( void )
{
   HPROENUM next = HPROENUM_FIRST;

	 char *name;
	 UnsignedInt index = 0;
	 while (index < MAXPROVIDERS && AIL_enumerate_3D_providers(&next, &m_provider3D[index].id, &name)) {
		 m_provider3D[index].name.set(name);	// set it to the AsciiString
		 ++index;
	 }
	 
	 m_providerCount = index;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::createListener( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	m_listener = AIL_open_3D_listener(m_provider3D[m_selectedProvider].id);
	// initial listener position will be (0, 0, 0)
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initDelayFilter( void )
{
	if (m_delayFilter != NULL) {
		return;
	}

	char* filterName;
	HPROENUM enumFLTs = HPROENUM_FIRST;
	HPROVIDER currentProvider;

	while (AIL_enumerate_filters(&enumFLTs, &currentProvider, &filterName )) {  
		if (strcmp(filterName,"Mono Delay Filter") == 0) {
			m_delayFilter = currentProvider;
			break;  
		}
	}
}

//-------------------------------------------------------------------------------------------------
Bool MilesAudioManager::isValidProvider( void )
{
	return (m_selectedProvider < m_providerCount);
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::initSamplePools( void )
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	int i = 0;
	for (i = 0; i < getAudioSettings()->m_sampleCount2D; ++i) {
		HSAMPLE sample = AIL_allocate_sample_handle(m_digitalHandle);
		DEBUG_ASSERTCRASH(sample, ("Couldn't get %d 2D samples\n", i + 1));
		if (sample) {
			AIL_init_sample(sample);
			AIL_set_sample_user_data(sample, 0, i + 1);
			m_availableSamples.push_back(sample);
			++m_num2DSamples;
		}
	}

	for (i = 0; i < getAudioSettings()->m_sampleCount3D; ++i) {
		H3DSAMPLE sample = AIL_allocate_3D_sample_handle(m_provider3D[m_selectedProvider].id);
		DEBUG_ASSERTCRASH(sample, ("Couldn't get %d 3D samples\n", i + 1));
		if (sample) {
			AIL_set_3D_user_data(sample, 0, i + 1);
			m_available3DSamples.push_back(sample);
			++m_num3DSamples;
		}
	}

	// Streams are basically free, so we can just allocate the appropriate number
	m_numStreams = getAudioSettings()->m_streamCount;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::processRequest( AudioRequest *req )
{
	switch (req->m_request)
	{
		case AR_Play:
		{
			// hand the event over: from here it belongs to the playing list, or to playAudioEvent
			// to free if it cannot be played
			const Bool requestStop = req->m_requestStop;
			playAudioEvent(req->releasePendingEvent(), requestStop);
			break;
		}
		case AR_Pause:
		{
			pauseAudioEvent(req->m_handleToInteractOn);
			break;
		}
		case AR_Stop:
		{
			stopAudioEvent(req->m_handleToInteractOn);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void *MilesAudioManager::getHandleForBink( void )
{
	if (m_binkHandle == NULL) {
		PlayingAudio *aud = allocatePlayingAudio();
		aud->m_audioEventRTS = NEW AudioEventRTS("BinkHandle");	// poolify
		getInfoForAudioEvent(aud->m_audioEventRTS);
		aud->m_sample = getFirst2DSample(aud->m_audioEventRTS);
		aud->m_type = PAT_Sample;

		if (!aud->m_sample) {
			stopPlayingAudio(aud);
			releasePlayingAudio(aud);
			return NULL;
		}

		m_binkHandle = aud;
	}
	
	AILLPDIRECTSOUND lpDS;
	AIL_get_DirectSound_info(m_binkHandle->m_sample, &lpDS, NULL);
	return lpDS;
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::releaseHandleForBink( void )
{
	if (m_binkHandle) {
		stopPlayingAudio(m_binkHandle);
		releasePlayingAudio(m_binkHandle);
		m_binkHandle = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
void MilesAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'\n", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
		case AT_Music:
			if (!isOn(AudioAffect_Music)) 
				return;
			break;
		case AT_SoundEffect:
			if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D)) 
				return;
			break;
		case AT_Streaming:
			if (!isOn(AudioAffect_Speech)) 
				return;
			break;
	}
	
	AudioEventRTS event = *eventToPlay;

	event.generateFilename();
	event.generatePlayInfo();

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == event.getEventName()) {
			event.setVolume(it->second);
			break;
		}
	}

	AsciiString fileToPlay = event.getFilename();
	HAUDIO haud = AIL_quick_load_and_play(fileToPlay.str(), 1, 0);

	// Even though the event type is not Speech, this is used only for mission briefings, so use the 
	// speech slider to adjust the volume.
	// Get the volume from the event, and pass 0.5 to play the audio in the middle. (0.0 is full left, 1.0 is full right)
	AIL_quick_set_volume(haud, event.getVolume() * getVolume(AudioAffect_Speech), 0.5);
	m_audioForcePlayed.push_back(haud);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AILCALLBACK setSampleCompleted( HSAMPLE sampleCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) sampleCompleted, PAT_Sample);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK set3DSampleCompleted( H3DSAMPLE sample3DCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) sample3DCompleted, PAT_3DSample);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK setStreamCompleted( HSTREAM streamCompleted )
{
	static_cast<MilesAudioManager *>(TheAudio)->queueAudioCompletion((UnsignedIntPtr) streamCompleted, PAT_Stream);
}

//-------------------------------------------------------------------------------------------------
U32 AILCALLBACK streamingFileOpen(char const *fileName, AILFILEHANDLE *file_handle)
{
	// The handle is a File*, so it is pointer sized - the cast to a 32-bit U32 that used to be here
	// threw half of every pointer away and was guarded by a debug-only crash saying as much.
	(*file_handle) = (AILFILEHANDLE) TheFileSystem->openFile(fileName, File::READ | File::STREAMING);
	return ((*file_handle) != 0);
}

//-------------------------------------------------------------------------------------------------
void AILCALLBACK streamingFileClose(AILFILEHANDLE fileHandle)
{
	((File*) fileHandle)->close();
}

//-------------------------------------------------------------------------------------------------
S32 AILCALLBACK streamingFileSeek(AILFILEHANDLE fileHandle, S32 offset, U32 type)
{
	return ((File*) fileHandle)->seek(offset, (File::seekMode) type);
}

//-------------------------------------------------------------------------------------------------
U32 AILCALLBACK streamingFileRead(AILFILEHANDLE file_handle, void *buffer, U32 bytes)
{
	return ((File*) file_handle)->read(buffer, bytes);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AudioFileCache::AudioFileCache() : m_maxSize(0), m_currentlyUsedSize(0), m_mutexName("AudioFileCacheMutex")
{
#if defined(_WIN32)
	m_mutex = CreateMutex(NULL, FALSE, m_mutexName);
#else
	m_mutex = new std::timed_mutex;	// the process's own: Windows' name made it every process's, by accident
#endif
}

//-------------------------------------------------------------------------------------------------
AudioFileCache::~AudioFileCache()
{
	{
		ScopedMutex mut(m_mutex);

		// Free all the samples that are open.
		OpenFilesHashIt it;
		for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
			if (it->second.m_openCount > 0) {
				DEBUG_CRASH(("Sample '%s' is still playing, and we're trying to quit.\n", it->second.m_eventInfo->m_audioName.str()));
			}

			releaseOpenAudioFile(&it->second);
			// Don't erase it from the map, cause it makes this whole process way more complicated, and 
			// we're about to go away anyways.
		}
	}

#if defined(_WIN32)
	CloseHandle(m_mutex);
#else
	delete m_mutex;
#endif
}

//-------------------------------------------------------------------------------------------------
void *AudioFileCache::openFile( AudioEventRTS *eventToOpenFrom )
{
	// Protect the entire openFile function
	ScopedMutex mut(m_mutex);

	AsciiString strToFind;
	switch (eventToOpenFrom->getNextPlayPortion())
	{
		case PP_Attack:
			strToFind = eventToOpenFrom->getAttackFilename();
			break;
		case PP_Sound:
			strToFind = eventToOpenFrom->getFilename();
			break;
		case PP_Decay:
			strToFind = eventToOpenFrom->getDecayFilename();
			break;
		case PP_Done:
			return NULL;
	}

	OpenFilesHash::iterator it;
	it = m_openFiles.find(strToFind);

	if (it != m_openFiles.end()) {
		++it->second.m_openCount;
		// A cached file keeps the info of whoever opened it first, and a map's customised sound
		// shares its file with the stock one.  That info is deleted with the level
		// (removeLevelSpecificAudioEventInfos), and freeEnoughSpaceForSample went on reading the
		// priority out of it for the file's next user.  The event opening it now is alive.
		it->second.m_eventInfo = eventToOpenFrom->getAudioEventInfo();
		return it->second.m_file;
	}

	// Couldn't find the file, so actually open it.
	File *file = TheFileSystem->openFile(strToFind.str());
	if (!file) {
		DEBUG_ASSERTLOG(strToFind.isEmpty(), ("Missing Audio File: '%s'\n", strToFind.str()));
		return NULL;
	}

	UnsignedInt fileSize = file->size();
	char* buffer = file->readEntireAndClose();

	OpenAudioFile openedAudioFile;
	openedAudioFile.m_eventInfo = eventToOpenFrom->getAudioEventInfo();

	AILSOUNDINFO soundInfo;
	AIL_WAV_info(buffer, &soundInfo);

	if (eventToOpenFrom->isPositionalAudio()) {
		if (soundInfo.channels > 1) {
			DEBUG_CRASH(("Requested Positional Play of audio '%s', but it is in stereo.", strToFind.str()));
			delete [] buffer;
			return NULL;
		}
	}
	
	if (soundInfo.format == WAVE_FORMAT_IMA_ADPCM) {
		void *decompressFileBuffer;
		U32 newFileSize;
		AIL_decompress_ADPCM(&soundInfo, &decompressFileBuffer, &newFileSize);
		fileSize = newFileSize;
		openedAudioFile.m_compressed = TRUE;
		delete [] buffer;
		openedAudioFile.m_file = decompressFileBuffer;
		openedAudioFile.m_soundInfo = soundInfo;
		openedAudioFile.m_openCount = 1;
	} else if (soundInfo.format == WAVE_FORMAT_PCM) {
		openedAudioFile.m_compressed = FALSE;
		openedAudioFile.m_file = buffer;
		openedAudioFile.m_soundInfo = soundInfo;
		openedAudioFile.m_openCount = 1;
	} else {
		DEBUG_CRASH(("Unexpected compression type in '%s'\n", strToFind.str()));
		// prevent leaks
		delete [] buffer;
		return NULL;
	}

	openedAudioFile.m_fileSize = fileSize;
	m_currentlyUsedSize += openedAudioFile.m_fileSize;
	if (m_currentlyUsedSize > m_maxSize) {
		// We need to free some samples, or we're not going to be able to play this sound.
		if (!freeEnoughSpaceForSample(openedAudioFile)) {
			m_currentlyUsedSize -= openedAudioFile.m_fileSize;
			releaseOpenAudioFile(&openedAudioFile);
			return NULL;
		}
	}

	m_openFiles[strToFind] = openedAudioFile;
	return openedAudioFile.m_file;
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::closeFile( void *fileToClose )
{
	if (!fileToClose) {
		return;
	}

	// Protect the entire closeFile function
	ScopedMutex mut(m_mutex);

	OpenFilesHash::iterator it;
	for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
		if ( it->second.m_file == fileToClose ) {
			--it->second.m_openCount;
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::setMaxSize( UnsignedInt size )
{
	// Protect the function, in case we're trying to use this value elsewhere.
	ScopedMutex mut(m_mutex);

	m_maxSize = size;
}

//-------------------------------------------------------------------------------------------------
void AudioFileCache::releaseOpenAudioFile( OpenAudioFile *fileToRelease )
{
	if (fileToRelease->m_openCount > 0) {
		// This thing needs to be terminated IMMEDIATELY.
		TheAudio->closeAnySamplesUsingFile(fileToRelease->m_file);
	}

	if (fileToRelease->m_file) {
		if (fileToRelease->m_compressed) {
			// Files read in via AIL_decompress_ADPCM must be freed with AIL_mem_free_lock. 
			AIL_mem_free_lock(fileToRelease->m_file);
		} else { 
			// Otherwise, we read it, we own it, blow it away.
			delete [] fileToRelease->m_file;
		}
		fileToRelease->m_file = NULL;
		fileToRelease->m_eventInfo = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
Bool AudioFileCache::freeEnoughSpaceForSample(const OpenAudioFile& sampleThatNeedsSpace)
{
	
	Int spaceRequired = m_currentlyUsedSize - m_maxSize;
	Int runningTotal = 0;

	std::list<AsciiString> filesToClose;
	// First, search for any samples that have ref counts of 0. They are low-hanging fruit, and 
	// should be considered immediately.
	OpenFilesHashIt it;
	for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
		if (it->second.m_openCount == 0) {
			// This is said low-hanging fruit.
			filesToClose.push_back(it->first);

			runningTotal += it->second.m_fileSize;

			if (runningTotal >= spaceRequired) {
				break;
			}
		}
	}

	// If we don't have enough space yet, then search through the events who have a count of 1 or more
	// and who are lower priority than this sound.
	// Mical said that at this point, sounds shouldn't care if other sounds are interruptable or not.
	// Kill any files of lower priority necessary to clear our the buffer.
	if (runningTotal < spaceRequired) {
		for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
			if (it->second.m_openCount > 0) {
				if (it->second.m_eventInfo->m_priority < sampleThatNeedsSpace.m_eventInfo->m_priority) {
					filesToClose.push_back(it->first);
					runningTotal += it->second.m_fileSize;
				
					if (runningTotal >= spaceRequired) {
						break;
					}
				}
			}
		}
	}

	// We weren't able to find enough sounds to truncate. Therefore, this sound is not going to play.
	if (runningTotal < spaceRequired) {
		return FALSE;
	}

	std::list<AsciiString>::iterator ait;
	for (ait = filesToClose.begin(); ait != filesToClose.end(); ++ait) {
		OpenFilesHashIt itToErase = m_openFiles.find(*ait);
		if (itToErase != m_openFiles.end()) {
			releaseOpenAudioFile(&itToErase->second);
			m_currentlyUsedSize -= itToErase->second.m_fileSize;
			m_openFiles.erase(itToErase);
		}
	}

	return TRUE;
}


#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void MilesAudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) {
		return;
	}

	// Dump all the audio assets we've used.
	FILE *logfile=fopen("PreloadedAssets.txt","a+");	//append to log
	if (!logfile)
		return;

	std::list<AsciiString> missingEvents;
	std::list<AsciiString> usedFiles;

	std::list<AsciiString>::iterator lit;

	fprintf(logfile, "\nAudio Asset Report - BEGIN\n");
	{
		SetAsciiStringIt it;
		std::vector<AsciiString>::iterator asIt;
		for (it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
			AsciiString astr = *it;
			AudioEventInfo *aei = findAudioEventInfo(astr);
			if (!aei) {
				missingEvents.push_back(astr);
				continue;
			}

			for (asIt = aei->m_attackSounds.begin(); asIt != aei->m_attackSounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_sounds.begin(); asIt != aei->m_sounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_decaySounds.begin(); asIt != aei->m_decaySounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			if (!aei->m_filename.isEmpty()) {
				usedFiles.push_back(aei->m_filename);
			}
		}

		fprintf(logfile, "\nEvents Requested that are missing information - BEGIN\n");
		for (lit = missingEvents.begin(); lit != missingEvents.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nEvents Requested that are missing information - END\n");

		fprintf(logfile, "\nFiles Used - BEGIN\n");
		for (lit = usedFiles.begin(); lit != usedFiles.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nFiles Used - END\n");
	}
	fprintf(logfile, "\nAudio Asset Report - END\n");
	fclose(logfile);
	logfile = NULL;
}
#endif
