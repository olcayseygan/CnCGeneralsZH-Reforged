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
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:    RTS 3
//
// File name:  Common/GameMusic.h
//
// Created:    5/01/01
//
//----------------------------------------------------------------------------

#pragma once

#ifndef __COMMON_GAMEMUSIC_H_
#define __COMMON_GAMEMUSIC_H_


//----------------------------------------------------------------------------
//           Includes                                                      
//----------------------------------------------------------------------------

#include "Common/GameAudio.h"
#include "Common/GameMemory.h"

#include <map>
#include <vector>


//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------

class AudioEventRTS;
struct FieldParse;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------


//===============================
// MusicTrack
//===============================

//-------------------------------------------------------------------------------------------------
/** The MusicTrack struct holds all information about a music track. 
	* Place data in TrackInfo that is useful to the game code in determining 
	* what tracks to play. */
//-------------------------------------------------------------------------------------------------

class MusicTrack : public MemoryPoolObject
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MusicTrack, "MusicTrack" )

public:

		MusicTrack();
		// virtual destructor prototype defined by memory pool object

		const FieldParse *getFieldParse( void ) const { return m_musicTrackFieldParseTable; }
			
		Int					index;									///< Track index
		AsciiString name;										///< Logical name of track
		AsciiString filename;								///< Filename with extension of music track
		Real				volume;									///< Mixing level for this track
		Bool				ambient;								///< Game info about this track(public)

		MusicTrack *next;
		MusicTrack *prev;

	static const FieldParse m_musicTrackFieldParseTable[];		///< the parse table for INI definition

};

/** Plays and stops the music the request list is handed, and, in a skirmish or multiplayer match
	* with DynamicMusic on, makes it follow the fight.
	*
	* The fight is measured from the sounds the game asks for (noteSound): a weapon firing, a unit
	* reaching a worse damage state or crying out under fire, weighted by how near the camera it is,
	* or by half when it is the local player's own unit wherever it stands.  Those add to an intensity
	* that halves about every three and a half seconds, so it reads roughly as weighted shots a second.
	* Above a threshold the music moves from calm to tense to battle, each a crossfade to a track of
	* the side's own playlist chosen by a hand table (TheDynamicTracks) or, for any other track, by the
	* MusicMood and MusicSide its Music.ini gives each track, and comes back down once the
	* fight has been over for a few seconds and the track has had its minimum time.  While tense or
	* battle music plays, the skirmish scripts' own track changes are held; back at calm, they choose
	* when the music changes again and a calm track of the side plays whichever they named.  Nothing
	* here is read by the logic, so nothing in it reaches a CRC. */
class MusicManager
{
	public:
		MusicManager();
		virtual ~MusicManager();

		void playTrack( AudioEventRTS *eventToUse );
		void stopTrack( AudioHandle eventToRemove );

		virtual void addAudioEvent(AudioEventRTS *eventToAdd);	// pre-copied
		virtual void removeAudioEvent( AudioHandle eventToRemove );

		void setVolume( Real m_volume );

		/// once each audio update: the intensity's decay, the mood it asks for and the crossfade
		void update( void );
		/// every sound the game asks for, before the options or the shroud can drop it
		void noteSound( const AudioEventRTS *event );

		enum Mood { MOOD_CALM, MOOD_TENSE, MOOD_BATTLE, MOOD_COUNT };

	private:
		/// a track of a side's playlist at a mood: the hand table's, or a MusicTrack's MusicMood tag
		struct MoodTrack
		{
			AsciiString name;
			AsciiString side;
			Int mood;
		};

		Bool engaged( void ) const;
		void startMatch( void );
		void releaseHeldStop( void );
		void buildTracks( void );
		const MoodTrack *findTrack( const AsciiString& name );
		AsciiString pickTrack( Mood mood );
		void changeTrack( Mood mood );
		void buildCombatSounds( void );

		std::vector<MoodTrack> m_tracks;							///< built at the first track of a match, so a map's music is in it

		std::map<const AudioEventInfo *, Real> m_combatWeight;	///< weapon fire, damage and fear sounds, built once
		Real m_intensity;
		UnsignedInt m_lastFrame;
		Mood m_mood;									///< what plays now; calm also while the scripts have the music
		UnsignedInt m_moodSince;			///< frame the music last changed
		UnsignedInt m_lowerSince;			///< frame the intensity first asked for less than m_mood, 0 while it does not
		Bool m_inControl;							///< tense or battle music of ours plays and the scripts' changes are held
		Bool m_issuing;								///< our own stop and play are going through
		Bool m_stopHeld;
		AudioHandle m_heldStop;				///< a script's stop, kept until the play after it says whose track it was
		AsciiString m_side;						///< "USA", "CHI", "GLA" or a track's MusicSide: whose playlist
		AsciiString m_track;					///< the track we last started
		AudioEventRTS *m_fadeEvent;		///< the device's own copy of the track fading in, NULL when none
		AudioHandle m_fadeHandle;			///< its handle: the copy is only touched while the device still has it
		UnsignedInt m_fadeStart;
		Real m_fadeTarget;
		Int m_pick[MOOD_COUNT];				///< steps through each mood's tracks in turn
		UnsignedInt m_lastLogFrame;
};

#endif // __COMMON_GAMEMUSIC_H_
