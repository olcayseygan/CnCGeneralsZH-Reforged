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

// FILE: MilesAudioManager.h //////////////////////////////////////////////////////////////////////////
// MilesAudioManager implementation
// Author: John K. McDonald, July 2002

#include "Common/AsciiString.h"
#include "Common/AudioSettings.h"
#include "Common/GameAudio.h"
#include "MSS/MSS.h"
#include "MilesAudioDevice/AudioFileLoader.h"
#include "mutex.h"
#include <list>
#include <map>
#if !defined(_WIN32)
#include <mutex>
#endif

class AudioEventRTS;

enum { MAXPROVIDERS = 64 };

enum PlayingAudioType
{
	PAT_Sample,
	PAT_3DSample,
	PAT_Stream,
	PAT_INVALID
};

//
// Stopping a sound has two halves and they cannot happen together.  The first half talks to Miles
// (stop the sample, unregister the callback) and must not hold a lock, because Miles calls back
// into us from its own timer thread.  The second half takes the object off our lists and frees it,
// and must hold one, because that same timer thread walks those lists.  PS_Stopping is the state
// between the two, and the transitions are interlocked so only one thread performs each half.
//
enum PlayingStatus
{
	PS_Playing,
	PS_Stopping,	///< about to be stopped
	PS_Stopped		///< about to be released
};

enum PlayingWhich
{
	PW_Attack,
	PW_Sound,
	PW_Decay,
	PW_INVALID
};

struct PlayingAudio
{
//	union
//	{
		HSAMPLE m_sample;
		H3DSAMPLE m_3DSample;
		HSTREAM m_stream;
//	};

	PlayingAudioType m_type;
	volatile PlayingStatus m_status;	// This member is adjusted by another running thread.
	AudioEventRTS *m_audioEventRTS;
	void *m_file;		// The file that was opened to play this
	Bool m_requestStop;	///< let the sound finish, but start no further loop of it
	Bool m_cleanupAudioEventRTS;
	/// Asked to fade out.  Moving from the playing list to the fading list writes two lists, so it
	/// happens in processPlayingList holding both locks rather than wherever the fade is asked for.
	Bool m_fade;
	/// logic frames of the fade gone by, counted on the audio clock: a pass a frame made a fade at 120
	/// fps a quarter of TimeToFadeAudio, and a music crossfade a drop followed by a slow rise
	Real m_framesFaded;
	/// Doppler's velocity for a 3D sound, units a second, and where the source was the last time the
	/// logic clock had moved (MilesAudioManager::trackVelocity).
	Bool m_tracked;
	Coord3D m_lastPosition;
	Coord3D m_velocity;
	/// EA's off-screen low pass (LowPassCutoff), which the sound was started with.
	Real m_offscreenOcclusion;
	/// The Aurora's pass (MilesAudioManager::updateSonicPass): the microphone's distance last pass,
	/// whether that was closing, and the logic frame the jet last flew at its attack speed.
	Real m_passDistance;
	Bool m_passClosing;
	UnsignedInt m_supersonicAt;

	PlayingAudio() :
		m_type(PAT_INVALID),
		m_status(PS_Playing),
		m_audioEventRTS(NULL),
		m_requestStop(false),
		m_cleanupAudioEventRTS(true),
		m_fade(false),
		m_sample(0),
		m_3DSample(0),
		m_stream(0),
		m_framesFaded(0.0f),
		m_tracked(false),
		m_offscreenOcclusion(0.0f),
		m_passDistance(0.0f),
		m_passClosing(false),
		m_supersonicAt(0)
	{
		m_lastPosition.zero();
		m_velocity.zero();
	}

	/// An entry past PS_Playing is waiting for the sweep that frees it: its Miles handle may already
	/// be back in the pool, so it must not be counted, touched or picked as a victim.
	Bool isPlaying() const { return m_status == PS_Playing; }
};

struct ProviderInfo
{
  AsciiString name;
  HPROVIDER id;
	Bool m_isValid;
};

struct OpenAudioFile
{
	AILSOUNDINFO m_soundInfo;
	void *m_file;
	UnsignedInt m_openCount;
	UnsignedInt m_fileSize;

	Bool m_compressed;	// if the file was compressed, then we need to free it with a miles function.
	
	// Note: OpenAudioFile does not own this m_eventInfo, and should not delete it.
	const AudioEventInfo *m_eventInfo;	// Not mutable, unlike the one on AudioEventRTS.
};

typedef std::hash_map< AsciiString, OpenAudioFile, rts::hash<AsciiString>, rts::equal_to<AsciiString> > OpenFilesHash;
typedef OpenFilesHash::iterator OpenFilesHashIt;

class AudioFileCache
{
	public:
		AudioFileCache();
		
		// Protected by mutex
		virtual ~AudioFileCache();
		void *openFile( AudioEventRTS *eventToOpenFrom );
		void closeFile( void *fileToClose );
		void setMaxSize( UnsignedInt size );
		/// Can openFile give this event its next file without the disk?  A no starts the read, off the frame.
		Bool fileIsReady( AudioEventRTS *eventToOpenFrom, Bool urgent );
		/// Has this one waited past the point of being heard as what it was?  A yes is counted as dropped.
		Bool fileComesTooLate( AudioEventRTS *eventToOpenFrom );
		// End Protected by mutex

		/// A stream opens its own file, on the main thread: this reads its start and end ahead of that.
		Bool streamIsWarm( const AsciiString &name ) { return m_loader.ready(name.str(), false, warmStreamFile); }
		void streamOpened( const AsciiString &name );

		void waitForLoads() { m_loader.waitUntilIdle(); }
		void logLoads();

		// Note: These functions should be used for informational purposes only. For speed reasons,
		// they are not protected by the mutex, so they are not guarenteed to be valid if called from
		// outside the audio cache. They should be used as a rough estimate only.
		UnsignedInt getCurrentlyUsedSize() const { return m_currentlyUsedSize; }
		UnsignedInt getMaxSize() const { return m_maxSize; }

	protected:
		void releaseOpenAudioFile( OpenAudioFile *fileToRelease );

		// This function will return TRUE if it was able to free enough space, and FALSE otherwise.
		Bool freeEnoughSpaceForSample(const OpenAudioFile& sampleThatNeedsSpace);
		
		OpenFilesHash m_openFiles;
		UnsignedInt m_currentlyUsedSize;
		UnsignedInt m_maxSize;
#if defined(_WIN32)
		HANDLE m_mutex;
#else
		std::timed_mutex *m_mutex;	///< Windows' mutex, as a process's own (C4): ScopedMutex.h
#endif
		AudioFileLoader m_loader;
		UnsignedInt m_readsOnTheFrame;	///< files read where they were asked for: -wav, the forced briefing voice
		UnsignedInt m_droppedLate;

	private:
		static char *warmStreamFile( const char *name, unsigned *size );
};

class MilesAudioManager : public AudioManager
{

	public:
#if defined(_DEBUG) || defined(_INTERNAL)
		virtual void audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp = NULL );
		virtual AudioHandle addAudioEvent( const AudioEventRTS *eventToAdd );	///< Add an audio event (event must be declared in an INI file)
#endif

		// from AudioDevice
		virtual void init();
		virtual void postProcessLoad();
		virtual void reset();
		virtual void update();

		MilesAudioManager();
		virtual ~MilesAudioManager();


		virtual void nextMusicTrack( void );
		virtual void prevMusicTrack( void );
		virtual Bool isMusicPlaying( void ) const;
		virtual Bool hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const;
		virtual AsciiString getMusicTrackName( void ) const;

		virtual void openDevice( void );
		virtual void closeDevice( void );
		virtual void *getDevice( void ) { return m_digitalHandle; }

		virtual void stopAudio( AudioAffect which );
		virtual void pauseAudio( AudioAffect which );
		virtual void resumeAudio( AudioAffect which );
		virtual void pauseAmbient( Bool shouldPause );

		virtual void killAudioEventImmediately( AudioHandle audioEvent );

		///< Return whether the current audio is playing or not.
		///< NOTE NOTE NOTE !!DO NOT USE THIS IN FOR GAMELOGIC PURPOSES!! NOTE NOTE NOTE
		virtual Bool isCurrentlyPlaying( AudioHandle handle );

		virtual void captureForLogic( void );

		virtual void notifyOfAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags );
		virtual PlayingAudio *findPlayingAudioFrom( UnsignedIntPtr audioCompleted, UnsignedInt flags );

		virtual UnsignedInt getProviderCount( void ) const;
		virtual AsciiString getProviderName( UnsignedInt providerNum ) const;
		virtual UnsignedInt getProviderIndex( AsciiString providerName ) const;
		virtual void selectProvider( UnsignedInt providerNdx );
		virtual void unselectProvider( void );
		virtual UnsignedInt getSelectedProvider( void ) const;
		virtual void setSpeakerType( UnsignedInt speakerType );
		virtual UnsignedInt getSpeakerType( void );

 		virtual void *getHandleForBink( void );
 		virtual void releaseHandleForBink( void );

		virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay);

		virtual UnsignedInt getNum2DSamples( void ) const;
		virtual UnsignedInt getNum3DSamples( void ) const;
		virtual UnsignedInt getNumStreams( void ) const;

		virtual Bool doesViolateLimit( AudioEventRTS *event ) const;
		virtual Bool isPlayingLowerPriority( AudioEventRTS *event ) const;
		virtual Bool isPlayingAlready( AudioEventRTS *event ) const;
		virtual Bool isObjectPlayingVoice( UnsignedInt objID ) const;
		Bool killLowestPrioritySoundImmediately( AudioEventRTS *event );
		AudioEventRTS* findLowestPrioritySound( AudioEventRTS *event );

		// The battle mix: replies held back as they start (playAudioEvent), identical shots folded
		// together and a budget for battle voices through doesViolateLimit, the question every sound is
		// asked before it plays.  None of them touches a sound logic asked for (getIsLogicalAudio).
		Bool doesViolateEventLimit( AudioEventRTS *event ) const;
		Bool refuseReply( AudioEventRTS *event ) const;
		Bool coalesceShot( AudioEventRTS *event ) const;
		void noteShot( AudioEventRTS *event ) const;
		Bool refuseOverBudget( AudioEventRTS *event ) const;
		Real getLoudness( AudioEventRTS *event ) const;
		void logMix( void );
		void noteReach( const PlayingAudio *playing, const Coord3D *pos ) const;
		void setSampleDistances( H3DSAMPLE sample3D, const AudioEventRTS *event ) const;
		void applyOcclusion( PlayingAudio *playing );

		virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume);

		virtual void removePlayingAudio( AsciiString eventName );
		virtual void removeAllDisabledAudio();

		virtual void processRequestList( void );
		virtual void processPlayingList( void );
		virtual void processFadingList( void );

		Bool shouldProcessRequestThisFrame( AudioRequest *req ) const;
		void adjustRequest( AudioRequest *req );
		void advanceAudioClock( void );
		void trackVelocity( PlayingAudio *playing, const Coord3D *pos );
		void updateSonicPass( PlayingAudio *playing, const Coord3D *pos );
		void chooseAmbienceBed( void );
		void applyLegacySound( void );
		void updateAmbienceBed( void );
		Bool checkForSample( AudioRequest *req );

		virtual void setHardwareAccelerated(Bool accel);
		virtual void setSpeakerSurround(Bool surround);
		
		virtual void setPreferredProvider(AsciiString provider) { m_pref3DProvider = provider; }
		virtual void setPreferredSpeaker(AsciiString speakerType) { m_prefSpeaker = speakerType; }

		virtual Real getFileLengthMS( AsciiString strToLoad ) const;

		virtual void closeAnySamplesUsingFile( const void *fileToClose );


	protected:
		// 3-D functions
		virtual void setDeviceListenerPosition( void );
		const Coord3D *getCurrentPositionFromEvent( AudioEventRTS *event );
		Bool isOnScreen( const Coord3D *pos ) const;
		Real getEffectiveVolume(AudioEventRTS *event) const;

		// Looping functions
		Bool startNextLoop( PlayingAudio *looping );
		Bool fileIsReady( AudioEventRTS *event );
		Bool fileComesTooLate( AudioRequest *req );
		void continueFromRequestList( PlayingAudio *playing );
		void endLoopPassNow( PlayingAudio *audio );

		void playStream( AudioEventRTS *event, HSTREAM stream );
		// Returns the file handle for attachment to the PlayingAudio structure
		void *playSample( AudioEventRTS *event, HSAMPLE sample );
		void *playSample3D( PlayingAudio *audio );

	protected:
		void buildProviderList( void );
		void createListener( void );
		void initDelayFilter( void );
		Bool isValidProvider( void );
		void initSamplePools( void );
		void processRequest( AudioRequest *req );

		void playAudioEvent( AudioEventRTS *event, Bool requestStop, Bool nextLoop );
		void stopAudioEvent( AudioHandle handle );
		void pauseAudioEvent( AudioHandle handle );

		void *loadFileForRead( AudioEventRTS *eventToLoadFrom );
		void closeFile( void *fileRead );

		PlayingAudio *allocatePlayingAudio( void );
		void releaseMilesHandles( PlayingAudio *release );
		void releasePlayingAudio( PlayingAudio *release );
		/// First half of a stop: everything that talks to Miles.  Safe to call twice, and safe to
		/// call with no lock held, which is the point of it.
		void stopPlayingAudio( PlayingAudio *release );
		/// Second half: take the stopped ones off the list and free them, holding its lock.
		void releasePlayingAudioInListIfStopped( std::list<PlayingAudio *> &list, CriticalSectionClass &cs );
		
		void stopAllAudioImmediately( void );
		void freeAllMilesHandles( void );

		HSAMPLE getFirst2DSample( AudioEventRTS *event );
		H3DSAMPLE getFirst3DSample( AudioEventRTS *event );

		void adjustPlayingVolume( PlayingAudio *audio );
		Real getVoiceMixedVolume( const AudioEventRTS *event, Real sliderVolume ) const;	///< a unit's reply lifted over the sliders, everything else as the sliders say

		void stopAllSpeech( void );
		
	protected:
		void initFilters( HSAMPLE sample, const AudioEventRTS *eventInfo );
		void initFilters3D( PlayingAudio *audio, const Coord3D *pos );

	protected:
		ProviderInfo m_provider3D[MAXPROVIDERS];
		UnsignedInt m_providerCount;
		UnsignedInt m_selectedProvider;
		UnsignedInt m_lastProvider;
		UnsignedInt m_selectedSpeakerType;

		AsciiString m_pref3DProvider;
		AsciiString m_prefSpeaker;

		HDIGDRIVER m_digitalHandle;
		H3DPOBJECT m_listener;
		HPROVIDER m_delayFilter;

		/// The logic clock as update() last read it, and the milliseconds of logic time since the read
		/// before: the request list's delays and doppler's velocity run on it, so both keep to the
		/// picture at any render rate and in a -wav run.  Out of a game a pass counts as one frame.
		UnsignedInt m_clockFrame;
		Real m_clockElapsedMs;
		UnsignedInt m_clockMovedAt;	///< Clock_Milliseconds when the logic clock last moved on
		AsciiString m_bedMap;	///< the map chooseAmbienceBed last chose a bed for
		Bool m_legacySound;	///< what applyLegacySound last put into m_audioSettings
		UnsignedInt m_boomedAt;	///< the logic frame of the last sonic boom (updateSonicPass)
		AudioSettings m_engineSettings;	///< AudioSettings.ini as init read it, for Legacy sound turned off again
		Bool m_askingOnArrival;	///< checkForSample is asking canPlayNow about a sound that waited out its Delay

		/// The map's ambience bed (AmbienceBed* in AudioSettings.h): the event chooseAmbienceBed chose, the
		/// handle of the one playing, and the logic frame it was last started at.
		AsciiString m_bedEvent;
		AudioHandle m_bedHandle;
		UnsignedInt m_bedStartedAt;

		// LOGIC-THREAD.md M7: what the tick asks, as captureForLogic found it at the sync point
		std::vector<AudioHandle> m_playingAtSync;											///< every handle isCurrentlyPlaying said yes to, sorted
		std::vector< std::pair<AsciiString, Int> > m_musicLoopsAtSync;	///< each music stream and the loops it has finished

		// This is a list of all handles that are forcibly played. They always play as UI sounds.
		std::list<HAUDIO> m_audioForcePlayed;

		// Available handles for play. Note that there aren't handles open in advance for 
		// streaming things, only 2-D and 3-D sounds.
		std::list<HSAMPLE> m_availableSamples;
		std::list<H3DSAMPLE> m_available3DSamples;

		// Currently Playing stuff. Useful if we have to preempt it. 
		// This should rarely if ever happen, as we mirror this in Sounds, and attempt to 
		// keep preemption from taking place here.
		std::list<PlayingAudio *> m_playingSounds;
		std::list<PlayingAudio *> m_playing3DSounds;
		std::list<PlayingAudio *> m_playingStreams;

		// Currently fading stuff. At this point, we just want to let it finish fading, when it is
		// done it should be added to the completed list, then "freed" and the counts should be updated
		// on the next update
		std::list<PlayingAudio *> m_fadingAudio;

		// Miles calls notifyOfAudioCompletion from its own timer thread, and that walks these lists
		// looking for the sample that finished.  Erasing from them on the main thread at the same
		// moment invalidates the iterator it is holding, so every write that can move an element
		// is guarded.  Reads of a single member are left alone; m_status is volatile for that.
		CriticalSectionClass m_playingSoundsCS;
		CriticalSectionClass m_playing3DSoundsCS;
		CriticalSectionClass m_playingStreamsCS;
		CriticalSectionClass m_fadingAudioCS;

	public:
		/// Called by the audio thread when a sample or stream runs out; handled on the next update.
		void queueAudioCompletion( UnsignedIntPtr audioCompleted, UnsignedInt flags );

	protected:
		/* The finished-sound callback used to run the whole completion - the next loop, the decay,
			 the stop - on the XAudio2 service thread, holding a PlayingAudio and its AudioEventRTS that
			 the main thread was free to delete at the same moment.  v2.0.0 and v2.0.1 players crashed
			 in it reading freed memory.  The thread now only writes the handle down, and the main
			 thread, the one that frees those objects, does the rest at the top of update().  A handle
			 is remembered with how many times it had been started when it finished, so a completion
			 that arrives after the sample was handed to another sound is dropped, not applied to it. */
		struct CompletedAudio
		{
			UnsignedIntPtr handle;
			UnsignedInt flags;
			UnsignedInt startsAtCompletion;
		};
		void noteAudioStarted( UnsignedIntPtr handle );
		void processCompletedAudio( void );
		std::list<CompletedAudio> m_completedAudio;
		std::map<UnsignedIntPtr, UnsignedInt> m_audioStarts;
		CriticalSectionClass m_completedAudioCS;

		/// A one-shot battle sound let through, for the ones asked for right after it to fold into.
		struct RecentShot
		{
			AsciiString name;
			Coord3D position;
			UnsignedInt frame;
			AudioHandle handle;
			Int merged;
			Real volume;	///< the volume it was asked at, before anything was folded into it
		};
		mutable std::vector<RecentShot> m_recentShots;
		mutable std::map<UnsignedInt, UnsignedInt> m_replyAtMs;	///< object -> Clock_Milliseconds of its last reply
		mutable UnsignedInt m_lastReplyAtMs;
		/// What the mix did since the last AUDIO MIX line, and the most 3D voices heard at once.
		struct MixCounts
		{
			Int started3D, merged, stolen, overBudget, repliesLet, repliesHeld;
			UnsignedInt peak3D, peakBattle;
			// the AUDIO REACH line (noteReach): battle sounds started in the middle of the screen, on the
			// rest of it and off it, their summed distance, gain in dB and cutoff in Hz
			Int reachCount[3];
			Real reachDistance[3], reachGainDb[3], reachCutoff[3];
		};
		mutable MixCounts m_mixCounts;
		UnsignedInt m_mixLoggedAt;

		AudioFileCache *m_audioCache;
		PlayingAudio *m_binkHandle;
		UnsignedInt m_num2DSamples;
		UnsignedInt m_num3DSamples;
		UnsignedInt m_numStreams;
		
#if defined(_DEBUG) || defined(_INTERNAL)
		typedef std::set<AsciiString> SetAsciiString;
		typedef SetAsciiString::iterator SetAsciiStringIt;
		SetAsciiString m_allEventsLoaded;
		void dumpAllAssetsUsed();
#endif

};

