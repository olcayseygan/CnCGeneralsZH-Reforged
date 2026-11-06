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
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameMusic.cpp
//
// Created:   5/01/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameMusic.h"

#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"

#include <math.h>

#ifdef _INTERNAL
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------
#define MUSIC_PATH "Data\\Audio\\Tracks"  // directory path to the music files


//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
/** The INI data fields for music tracks */
//-------------------------------------------------------------------------------------------------
const FieldParse MusicTrack::m_musicTrackFieldParseTable[] = 
{

	{ "Filename",								INI::parseAsciiString,							NULL, offsetof( MusicTrack, filename ) },
	{ "Volume",									INI::parsePercentToReal,						NULL, offsetof( MusicTrack, volume ) },
	{ "Ambient",								INI::parseBool,											NULL, offsetof( MusicTrack, ambient ) },
	{ NULL,											NULL,																NULL, 0 },
};


//-------------------------------------------------------------------------------------------------
/** The skirmish playlists by mood.  Every track of Music.ini's Game_<side>_NN was measured for
	* loudness, onsets a second, spectral flux and the share of quiet passages, and ranked against the
	* rest of its own side's list: the three lowest calm, the four highest battle, the rest tense
	* (experiments/proofs/2026-10-25/audio-music/classify.py in the wrapper).  Game_Silent is the sixty
	* seconds the scripts leave between two tracks; it has no side and is never picked, but while our
	* music plays it is held like the tracks are.  Any other track is not here: Music.ini tags
	* them with MusicMood and MusicSide (buildTracks), and an untagged one stays the scripts'. */
struct DynamicTrack
{
	const char *name;
	const char *side;
	Int mood;
};

static const DynamicTrack TheDynamicTracks[] =
{
	{ "Game_USA_06", "USA", MusicManager::MOOD_CALM },
	{ "Game_USA_01", "USA", MusicManager::MOOD_CALM },
	{ "Game_USA_02", "USA", MusicManager::MOOD_CALM },
	{ "Game_USA_07", "USA", MusicManager::MOOD_TENSE },
	{ "Game_USA_11", "USA", MusicManager::MOOD_TENSE },
	{ "Game_USA_10", "USA", MusicManager::MOOD_TENSE },
	{ "Game_USA_03", "USA", MusicManager::MOOD_TENSE },
	{ "Game_USA_09", "USA", MusicManager::MOOD_BATTLE },
	{ "Game_USA_08", "USA", MusicManager::MOOD_BATTLE },
	{ "Game_USA_05", "USA", MusicManager::MOOD_BATTLE },
	{ "Game_USA_04", "USA", MusicManager::MOOD_BATTLE },

	{ "Game_CHI_07", "CHI", MusicManager::MOOD_CALM },
	{ "Game_CHI_02", "CHI", MusicManager::MOOD_CALM },
	{ "Game_CHI_03", "CHI", MusicManager::MOOD_CALM },
	{ "Game_CHI_08", "CHI", MusicManager::MOOD_TENSE },
	{ "Game_CHI_10", "CHI", MusicManager::MOOD_TENSE },
	{ "Game_CHI_06", "CHI", MusicManager::MOOD_TENSE },
	{ "Game_CHI_09", "CHI", MusicManager::MOOD_BATTLE },
	{ "Game_CHI_04", "CHI", MusicManager::MOOD_BATTLE },
	{ "Game_CHI_01", "CHI", MusicManager::MOOD_BATTLE },
	{ "Game_CHI_05", "CHI", MusicManager::MOOD_BATTLE },

	{ "Game_GLA_11", "GLA", MusicManager::MOOD_CALM },
	{ "Game_GLA_03", "GLA", MusicManager::MOOD_CALM },
	{ "Game_GLA_10", "GLA", MusicManager::MOOD_CALM },
	{ "Game_GLA_05", "GLA", MusicManager::MOOD_TENSE },
	{ "Game_GLA_04", "GLA", MusicManager::MOOD_TENSE },
	{ "Game_GLA_06", "GLA", MusicManager::MOOD_TENSE },
	{ "Game_GLA_08", "GLA", MusicManager::MOOD_TENSE },
	{ "Game_GLA_09", "GLA", MusicManager::MOOD_BATTLE },
	{ "Game_GLA_02", "GLA", MusicManager::MOOD_BATTLE },
	{ "Game_GLA_07", "GLA", MusicManager::MOOD_BATTLE },
	{ "Game_GLA_01", "GLA", MusicManager::MOOD_BATTLE },

	{ "Game_Silent", "", MusicManager::MOOD_CALM },
};

static const char *const TheMoodNames[] = { "calm", "tense", "battle" };

// The fight, in weighted sounds a second.  Up at the first figure of each pair, down only below the
// second, so a fight that hovers on a line does not swap the music back and forth.  Measured on
// musicfight.txt: six Crusaders against six Scorpions and four Technicals on the camera peak at 12,
// two Rangers against three Rebels at 5.
static const Real TENSE_ON = 1.0f;
static const Real TENSE_OFF = 0.4f;
static const Real BATTLE_ON = 6.0f;
static const Real BATTLE_OFF = 3.0f;

static const Real INTENSITY_SECONDS = 5.0f;		///< the decay's time constant: half gone in 3.5 seconds
static const Real NEAR_RANGE = 500.0f;				///< a sound this far from the microphone on the ground counts for nothing
static const Real OWN_SHARE = 0.5f;						///< the least a sound of the local player's own unit counts, wherever it is
static const Real FEAR_WEIGHT = 3.0f;					///< a unit hit into a worse state or crying out counts as three shots

static const UnsignedInt UP_AFTER = 4 * LOGICFRAMES_PER_SECOND;			///< a crossfade's length: no change cuts one short
static const UnsignedInt DOWN_HOLD = 12 * LOGICFRAMES_PER_SECOND;	///< quieter this long before the music comes down: from battle, past tense to calm
static const UnsignedInt MIN_TRACK = 20 * LOGICFRAMES_PER_SECOND;		///< and never before a track has had this long
static const Real FADE_FLOOR = 0.05f;													///< where the new track rises from, above MinSampleVolume's cull

//-------------------------------------------------------------------------------------------------
/** The retail table, then every MusicTrack the loaded data tags with a MusicMood, in Music.ini's
	* order: a tag on a retail track moves it, a tag on a new one adds it.  Built at a match's first
	* track, after any map.ini has added its own. */
void MusicManager::buildTracks( void )
{
	m_tracks.clear();
	for (Int i = 0; i < ARRAY_SIZE( TheDynamicTracks ); ++i)
	{
		MoodTrack track;
		track.name = TheDynamicTracks[i].name;
		track.side = TheDynamicTracks[i].side;
		track.mood = TheDynamicTracks[i].mood;
		m_tracks.push_back( track );
	}

	Int tagged = 0;
	const AsciiString first = TheAudio->nextTrackName( AsciiString::TheEmptyString );
	AsciiString name = first;
	while (name.isNotEmpty())
	{
		const AudioEventInfo *info = TheAudio->findAudioEventInfo( name );
		if (info != NULL && info->m_musicMood > 0)
		{
			MoodTrack *track = NULL;
			for (size_t i = 0; i < m_tracks.size() && track == NULL; ++i)
				if (m_tracks[i].name.compareNoCase( name ) == 0)
					track = &m_tracks[i];
			if (track == NULL)
			{
				m_tracks.push_back( MoodTrack() );
				track = &m_tracks.back();
				track->name = name;
			}
			track->side = info->m_musicSide;
			track->mood = info->m_musicMood - 1;
			++tagged;
		}
		name = TheAudio->nextTrackName( name );
		if (name == first)
			break;
	}
	DEBUG_LOG(("MUSIC: %d tracks by mood, %d of them tagged in Music.ini\n", (Int)m_tracks.size(), tagged));
}

//-------------------------------------------------------------------------------------------------
const MusicManager::MoodTrack *MusicManager::findTrack( const AsciiString& name )
{
	if (m_tracks.empty())
		buildTracks();
	for (size_t i = 0; i < m_tracks.size(); ++i)
		if (name.compareNoCase( m_tracks[i].name ) == 0)
			return &m_tracks[i];
	return NULL;
}

//-------------------------------------------------------------------------------------------------
MusicManager::MusicManager()
{
	startMatch();
	m_lastFrame = 0;
}

//-------------------------------------------------------------------------------------------------
MusicManager::~MusicManager()
{

}

//-------------------------------------------------------------------------------------------------
void MusicManager::playTrack( AudioEventRTS *eventToUse )
{
	AudioRequest *audioRequest = TheAudio->allocateAudioRequest( true );
	audioRequest->m_pendingEvent = eventToUse;
	audioRequest->m_request = AR_Play;
	TheAudio->appendAudioRequest( audioRequest );
}

//-------------------------------------------------------------------------------------------------
void MusicManager::stopTrack( AudioHandle eventToRemove )
{
	AudioRequest *audioRequest = TheAudio->allocateAudioRequest( false );
	audioRequest->m_handleToInteractOn = eventToRemove;
	audioRequest->m_request = AR_Stop;
	TheAudio->appendAudioRequest( audioRequest );
}

//-------------------------------------------------------------------------------------------------
void MusicManager::addAudioEvent( AudioEventRTS *eventToAdd )
{
	const MoodTrack *track = findTrack( eventToAdd->getEventName() );
	if (track != NULL && track->side.isNotEmpty())
		m_side = track->side;

	if (m_inControl && !m_issuing && engaged())
	{
		// The scripts' cycle goes on underneath; the fight keeps its music.
		if (track != NULL)
		{
			m_stopHeld = FALSE;
			TheAudio->releaseAudioEventRTS( eventToAdd );
			return;
		}
		// Anything else, a map's own cue, takes the music back until the cycle plays a table track again.
		releaseHeldStop();
	}
	if (track == NULL)
		m_side.clear();

	// At calm the cycle's own next track is swapped for a calm one of its side, or a fight's music
	// would play over a quiet base.  The scripts' sixty seconds of silence stay as they are, and so
	// does the track of a side that tagged no calm one.
	const AsciiString calm = !m_issuing && !m_inControl && track != NULL && track->side.isNotEmpty()
		&& track->mood != MOOD_CALM && engaged() ? pickTrack( MOOD_CALM ) : AsciiString::TheEmptyString;
	if (calm.isNotEmpty())
	{
		m_track = calm;
		DEBUG_LOG(("MUSIC: frame %u the scripts' %s plays as %s\n", TheGameLogic->getFrame(),
			eventToAdd->getEventName().str(), m_track.str()));
		eventToAdd->setEventName( m_track );
		TheAudio->getInfoForAudioEvent( eventToAdd );
		eventToAdd->generateFilename();
		eventToAdd->generatePlayInfo();
	}

	if (m_issuing)
	{
		m_fadeEvent = eventToAdd;
		m_fadeHandle = eventToAdd->getPlayingHandle();
	}
	playTrack( eventToAdd );
}

//-------------------------------------------------------------------------------------------------
void MusicManager::removeAudioEvent( AudioHandle eventToRemove )
{
	if (m_inControl && !m_issuing && engaged())
	{
		m_stopHeld = TRUE;
		m_heldStop = eventToRemove;
		return;
	}
	stopTrack( eventToRemove );
}

//-------------------------------------------------------------------------------------------------
/** A stop with no table track after it was not the cycle's: let it through and give the music up. */
void MusicManager::releaseHeldStop( void )
{
	if (m_stopHeld)
		stopTrack( m_heldStop );
	m_stopHeld = FALSE;
	m_inControl = FALSE;
	m_mood = MOOD_CALM;
}

//-------------------------------------------------------------------------------------------------
void MusicManager::startMatch( void )
{
	m_intensity = 0.0f;
	m_mood = MOOD_CALM;
	m_moodSince = 0;
	m_lowerSince = 0;
	m_inControl = FALSE;
	m_issuing = FALSE;
	m_stopHeld = FALSE;
	m_heldStop = AHSV_Error;
	m_side.clear();
	m_track.clear();
	m_fadeEvent = NULL;
	m_fadeHandle = AHSV_Error;
	m_fadeStart = 0;
	m_fadeTarget = 1.0f;
	m_lastLogFrame = 0;
	for (Int i = 0; i < MOOD_COUNT; ++i)
		m_pick[i] = 0;
}

//-------------------------------------------------------------------------------------------------
/** A skirmish or multiplayer match the local player still plays in, with the option on.  Read on the
	* main thread outside the tick, where the audio already reads the player and the objects. */
Bool MusicManager::engaged( void ) const
{
	if (!TheGlobalData->m_dynamicMusic || TheGlobalData->m_legacySound || TheGameLogic == NULL || !TheGameLogic->isInGame()
			|| TheGameLogic->isLoadingMap() || TheGameLogic->isInShellGame()
			|| !(TheGameLogic->isInSkirmishGame() || TheGameLogic->isInMultiplayerGame()))
		return FALSE;
	const Player *me = ThePlayerList->getLocalPlayer();
	return me != NULL && me->isPlayerActive();
}

//-------------------------------------------------------------------------------------------------
void MusicManager::buildCombatSounds( void )
{
	for (const ThingTemplate *tmpl = TheThingFactory->firstTemplate(); tmpl; tmpl = tmpl->friend_getNextTemplate())
	{
		const WeaponTemplateSetVector& sets = tmpl->getWeaponTemplateSets();
		for (size_t s = 0; s < sets.size(); ++s)
		{
			for (Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
			{
				const WeaponTemplate *weapon = sets[s].getNth( (WeaponSlotType)slot );
				const AudioEventInfo *info = weapon ? TheAudio->findAudioEventInfo( weapon->getFireSound().getEventName() ) : NULL;
				if (info != NULL && m_combatWeight.find( info ) == m_combatWeight.end())
					m_combatWeight[info] = 1.0f;
			}
		}
		const AudioEventRTS *hurt[] = { tmpl->getSoundOnDamaged(), tmpl->getSoundOnReallyDamaged(), tmpl->getVoiceFear() };
		for (Int i = 0; i < ARRAY_SIZE( hurt ); ++i)
		{
			const AudioEventInfo *info = TheAudio->findAudioEventInfo( hurt[i]->getEventName() );
			if (info != NULL)
				m_combatWeight[info] = FEAR_WEIGHT;
		}
	}
	DEBUG_LOG(("MUSIC: %d sounds count as fighting\n", (Int)m_combatWeight.size()));
}

//-------------------------------------------------------------------------------------------------
void MusicManager::noteSound( const AudioEventRTS *event )
{
	if (!engaged())
		return;
	if (m_combatWeight.empty())
		buildCombatSounds();

	std::map<const AudioEventInfo *, Real>::const_iterator it = m_combatWeight.find( event->getAudioEventInfo() );
	if (it == m_combatWeight.end())
		return;

	// The getters are not const; they only read.
	AudioEventRTS *sound = const_cast<AudioEventRTS *>( event );
	Coord3D pos;
	Bool mine = FALSE;
	const Object *obj = TheGameLogic->findObjectByID( sound->getObjectID() );
	if (obj != NULL)
	{
		pos = *obj->getPosition();
		mine = obj->getControllingPlayer() == ThePlayerList->getLocalPlayer();
	}
	else if (sound->getPosition() != NULL)
		pos = *sound->getPosition();
	else
		return;

	const Coord3D *mic = TheAudio->getListenerPosition();
	const Real dx = pos.x - mic->x;
	const Real dy = pos.y - mic->y;
	const Real nearness = MAX( 0.0f, 1.0f - sqrtf( dx * dx + dy * dy ) / NEAR_RANGE );
	m_intensity += it->second * MAX( nearness, mine ? OWN_SHARE : 0.0f ) / INTENSITY_SECONDS;
}

//-------------------------------------------------------------------------------------------------
/** The side's tracks of this mood in turn, never the one we started last; empty for an unknown side. */
AsciiString MusicManager::pickTrack( Mood mood )
{
	if (m_tracks.empty())
		buildTracks();
	AsciiString pick;
	Int count = 0;
	for (size_t i = 0; i < m_tracks.size(); ++i)
		if (m_tracks[i].mood == mood && m_side == m_tracks[i].side)
			++count;
	for (Int tries = 0; tries < count && (pick.isEmpty() || pick == m_track); ++tries)
	{
		const Int want = m_pick[mood]++ % count;
		Int n = 0;
		for (size_t i = 0; i < m_tracks.size(); ++i)
			if (m_tracks[i].mood == mood && m_side == m_tracks[i].side && n++ == want)
				pick = m_tracks[i].name;
	}
	return pick;
}

//-------------------------------------------------------------------------------------------------
/** A crossfade: whatever music plays falls over TimeToFadeAudio, in logic time (the device's fading
	* list), and a track of this mood rises over the same frames from the same frame, so the two overlap
	* end to end.  The fading track is marked as such, so the one-track rule in the device leaves it
	* alone until it has gone. */
void MusicManager::changeTrack( Mood mood )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AsciiString pick = pickTrack( mood );

	DEBUG_LOG(("MUSIC: frame %u intensity %.2f %s -> %s, %s\n", now, m_intensity, TheMoodNames[m_mood],
		TheMoodNames[mood], pick.str()));
	m_mood = mood;
	m_moodSince = now;
	m_lowerSince = 0;
	if (pick.isEmpty())
		return;

	m_issuing = TRUE;
	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
	AudioEventRTS track( pick );
	track.setVolume( FADE_FLOOR );
	TheAudio->addAudioEvent( &track );
	m_issuing = FALSE;

	m_track = pick;
	m_fadeStart = now;
	m_fadeTarget = TheAudio->findAudioEventInfo( pick )->m_volume;
}

//-------------------------------------------------------------------------------------------------
void MusicManager::update( void )
{
	if (m_stopHeld)
		releaseHeldStop();

	const Bool match = TheGameLogic != NULL && TheGameLogic->isInGame() && !TheGameLogic->isLoadingMap();
	const UnsignedInt now = match ? TheGameLogic->getFrame() : 0;
	if (!match || now < m_lastFrame)
	{
		// the next match builds the mood table again, with whatever music its map.ini brings
		m_tracks.clear();
		if (m_lastFrame != 0)
			startMatch();
		m_lastFrame = now;
		return;
	}

	// one step of the decay a logic frame, e^(-1/300) to five places without a runtime exp
	// (simulation_uses_no_runtime_trig scans this folder); a jump of more than ten seconds counts as ten
	for (UnsignedInt f = m_lastFrame; f < now && f < m_lastFrame + 10 * LOGICFRAMES_PER_SECOND; ++f)
		m_intensity *= 1.0f - 1.0f / (INTENSITY_SECONDS * LOGICFRAMES_PER_SECOND);
	m_lastFrame = now;

	// The rise is the device's own copy of the event, which the device frees when the stream ends, so
	// it is written only while the device still answers for its handle.  The device reads the volume
	// back on its next pass, with the music slider on it, in the same update.
	// The old track falls in a straight line, so the new one rises as sqrt(1 - (1 - t)^2): the two
	// together keep the power of one track all through, where two straight lines dip 3 dB halfway.
	if (m_fadeEvent != NULL)
	{
		const Real fadeFrames = (Real)MAX( 1, TheAudio->getAudioSettings()->m_fadeAudioFrames );
		const Real t = MIN( 1.0f, (Real)(now - m_fadeStart) / fadeFrames );
		if (TheAudio->isCurrentlyPlaying( m_fadeHandle ))
			m_fadeEvent->setVolume( MAX( FADE_FLOOR, m_fadeTarget * sqrtf( t * (2.0f - t) ) ) );
		if (t >= 1.0f || !TheAudio->isCurrentlyPlaying( m_fadeHandle ))
			m_fadeEvent = NULL;
	}

	if (!engaged())
	{
		m_inControl = FALSE;
		m_mood = MOOD_CALM;
		return;
	}

	// Every two seconds only while -wav records, to lay beside the recorded mix; the changes always.
	if (TheGlobalData->m_wavEndFrame > 0 && now - m_lastLogFrame >= 2 * LOGICFRAMES_PER_SECOND)
	{
		m_lastLogFrame = now;
		DEBUG_LOG(("MUSIC: frame %u intensity %.2f %s %s\n", now, m_intensity, TheMoodNames[m_mood], m_side.str()));
	}
	// Until the scripts play a track of the table there is no playlist to choose from.
	if (m_side.isEmpty())
		return;

	Mood wanted = MOOD_CALM;
	if (m_intensity >= BATTLE_ON || (m_mood == MOOD_BATTLE && m_intensity >= BATTLE_OFF))
		wanted = MOOD_BATTLE;
	else if (m_intensity >= TENSE_ON || (m_mood != MOOD_CALM && m_intensity >= TENSE_OFF))
		wanted = MOOD_TENSE;

	if (wanted > m_mood && now - m_moodSince >= UP_AFTER)
	{
		m_inControl = TRUE;
		changeTrack( wanted );
	}
	else if (wanted < m_mood)
	{
		if (m_lowerSince == 0)
			m_lowerSince = now;
		if (now - m_lowerSince >= DOWN_HOLD && now - m_moodSince >= MIN_TRACK)
		{
			changeTrack( wanted );
			// back at calm the scripts have the music again, from their next change on
			m_inControl = wanted != MOOD_CALM;
		}
	}
	else
		m_lowerSince = 0;
}

