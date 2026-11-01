/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * C4's upper half: Windows' MilesAudioManager, compiled off Windows, over the Miles surface on miniaudio.
 *
 * The manager is the game's own class, the one SdlGameEngine::createAudioManager now returns, running
 * on miniaudio's null backend (ZH_AUDIO_BACKEND=null: a device with no hardware that still mixes in
 * real time, as the other audio tests run).  Its INIs and sounds come from the install's archives through the
 * engine's own file systems, read only: the archives are linked into a folder in the build tree, which
 * is made read-only and is the working directory, as test_bigfilesystem does (rule 9 - nothing here
 * starts the engine).  What it hears is the mix tap, AIL_ex_start_capture, into a WAV in the build
 * tree, measured here.
 *
 * AudioManager::update places the microphone from TheTacticalView and the terrain, which need the
 * renderer; the test's tick() does the rest of MilesAudioManager::update with a fixed listener at the
 * origin facing +y.  Events are marked uninterruptable, which skips shouldPlayLocally - a check against
 * the local player that needs a game's player list - and are chosen without a low-pass filter, whose
 * on-screen test needs the tactical view.
 *
 *   - -headless first, in a fresh process: parseHeadless's flags, the manager made and initialised
 *     exactly as on Windows, and no device ever opened (the capture refuses, as it does without one) -
 *     armed by the same probe succeeding in the real run.
 *   - A 2D sound plays and ends; the manager frees it on the MAIN thread's sweep, not the audio
 *     thread's (26bc0c45's contract): past its end it is still "playing" until tick() runs.
 *   - A streamed sound (speech) is heard for as long as its file lasts, by the test's own reading of
 *     the WAV's header, not the manager's.
 *   - A 3D sound leans to one channel, and to the other when it moves across the listener.
 *   - Music plays, and a stop brings silence.  Nothing plays with the sound switched off (armed).
 *
 * What it cannot see: how it sounds (a person with miles_listen and, at M5, the game); the camera's
 * microphone arithmetic; player-filtered and low-pass events in play; real hardware devices.
 */

#include "test_harness.h"

#include "PreRTS.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioSettings.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ScopedMutex.h"
#include "Common/file.h"
#include "GameClient/VideoPlayer.h"
#include "MilesAudioDevice/MilesAudioManager.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---- The world the manager needs ------------------------------------------------------------------

std::vector<std::string> bigNamesIn( const std::string &dir )
{
	std::vector<std::string> names;
	if (DIR *d = opendir( dir.c_str() ))
	{
		while (struct dirent *e = readdir( d ))
		{
			const std::string n = e->d_name;
			if (n.size() > 4 && n[0] != '.' && strcasecmp( n.c_str() + n.size() - 4, ".big" ) == 0)
				names.push_back( n );
		}
		closedir( d );
	}
	std::sort( names.begin(), names.end() );
	return names;
}

void linkArchives( const std::string &from, const std::string &to )
{
	mkdir( to.c_str(), 0777 );
	const std::vector<std::string> names = bigNamesIn( from );
	for (size_t i = 0; i < names.size(); ++i)
		symlink( (from + "/" + names[i]).c_str(), (to + "/" + names[i]).c_str() );
}

/* The install's archives, read only, as the working directory: the root's, Data\INI's stray INIZH.big,
	 and the base game's as ZH_Generals - test_bigfilesystem's farm. */
void mountTheInstall( void )
{
	static bool mounted = false;
	if (mounted)
		return;
	mounted = true;
	const char *data = getenv( "ZH_DATA_DIR" );
	if (data == NULL || data[0] == 0)
	{
		printf( "skip: no game data (ZH_DATA_DIR)\n" );
		exit( 77 );
	}
	const std::string root = std::string( getenv( "ZH_USER_DATA_DIR" ) ) + "/farm";
	const std::string farm = root + "/zerohour";
	std::string command = "chmod -R u+w '" + root + "' 2>/dev/null; rm -rf '" + root + "'";
	system( command.c_str() );
	command = "mkdir -p '" + root + "'";		// the user data folder itself is made on first use, later
	system( command.c_str() );
	linkArchives( std::string( data ) + "/zerohour", farm );
	mkdir( (farm + "/Data").c_str(), 0777 );
	linkArchives( std::string( data ) + "/zerohour/Data/INI", farm + "/Data/INI" );
	linkArchives( std::string( data ) + "/generals", farm + "/ZH_Generals" );
	chmod( (farm + "/ZH_Generals").c_str(), 0555 );
	chmod( (farm + "/Data/INI").c_str(), 0555 );
	chmod( (farm + "/Data").c_str(), 0555 );
	chmod( farm.c_str(), 0555 );
	if (chdir( farm.c_str() ) != 0)
		printf( "  could not enter %s\n", farm.c_str() );

	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;
	TheNameKeyGenerator->init();
	TheWritableGlobalData = NEW GlobalData;
	TheLocalFileSystem = NEW PosixLocalFileSystem;
	TheArchiveFileSystem = NEW Win32BIGFileSystem;
	TheFileSystem = NEW FileSystem;
	TheLocalFileSystem->init();
	TheArchiveFileSystem->init();
	TheVideoPlayer = NEW VideoPlayer;	// the manager tells it the speech volume whenever a volume changes
}

// MilesAudioManager::update without AudioManager::update's camera: a microphone at the origin facing +y
class TestAudio : public MilesAudioManager
{
public:
	void tick( void )
	{
		const Coord3D listener = { 0, 0, 0 }, facing = { 0, 1, 0 };
		setListenerPosition( &listener, &facing );
		set3DVolumeAdjustment( 1.0f );
		processCompletedAudio();
		setDeviceListenerPosition();
		processRequestList();
		m_audioCache->waitForLoads();		// the next tick plays what this one asked the disk for, however slow the disk
		processPlayingList();
		processFadingList();
	}
	// Music streams still on the playing list at full voice: neither stopping nor fading out
	int musicPlaying( void ) const
	{
		int n = 0;
		for (std::list<PlayingAudio *>::const_iterator it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it)
			if (*it && !(*it)->m_fade && (*it)->m_status == PS_Playing
					&& (*it)->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music)
				++n;
		return n;
	}
	void runFor( int ms, bool ticking = true )
	{
		for (int t = 0; t < ms; t += 10)
		{
			if (ticking)
				tick();
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}
	}
};

TestAudio *theAudio( void )
{
	static TestAudio *audio = NULL;
	if (audio == NULL)
	{
		mountTheInstall();
		audio = NEW TestAudio;
		TheAudio = audio;
		audio->init();
		audio->postProcessLoad();
	}
	return audio;
}

// ---- Listening ------------------------------------------------------------------------------------

struct Heard
{
	unsigned rate;
	std::vector<float> left, right;		// -1..1
	bool ok;
};

unsigned le32( const unsigned char *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
unsigned le16( const unsigned char *p ) { return p[0] | (p[1] << 8); }

Heard readCapture( const std::string &path )
{
	Heard h = { 0, {}, {}, false };
	FILE *f = fopen( path.c_str(), "rb" );
	if (f == NULL)
		return h;
	std::vector<unsigned char> b;
	unsigned char buf[65536];
	size_t n;
	while ((n = fread( buf, 1, sizeof( buf ), f )) > 0)
		b.insert( b.end(), buf, buf + n );
	fclose( f );
	if (b.size() < 44 || memcmp( &b[0], "RIFF", 4 ) != 0)
		return h;
	size_t at = 12, dataAt = 0, dataLen = 0;
	unsigned channels = 0, bits = 0;
	while (at + 8 <= b.size())
	{
		const unsigned len = le32( &b[at + 4] );
		if (memcmp( &b[at], "fmt ", 4 ) == 0) { channels = le16( &b[at + 10] ); h.rate = le32( &b[at + 12] ); bits = le16( &b[at + 22] ); }
		if (memcmp( &b[at], "data", 4 ) == 0) { dataAt = at + 8; dataLen = std::min( (size_t)len, b.size() - dataAt ); }
		at += 8 + len + (len & 1);
	}
	if (channels != 2 || bits != 16 || dataAt == 0)
		return h;
	for (size_t i = 0; i + 4 <= dataLen; i += 4)
	{
		h.left.push_back( (short)le16( &b[dataAt + i] ) / 32768.0f );
		h.right.push_back( (short)le16( &b[dataAt + i + 2] ) / 32768.0f );
	}
	h.ok = true;
	return h;
}

double rms( const std::vector<float> &s, size_t from = 0, size_t to = (size_t)-1 )
{
	to = std::min( to, s.size() );
	double sum = 0;
	for (size_t i = from; i < to; ++i)
		sum += s[i] * s[i];
	return to > from ? sqrt( sum / (to - from) ) : 0.0;
}

// How long the mix was audibly non-silent, from the first to the last 10 ms block above the floor
double audibleSeconds( const Heard &h, double floor = 0.002 )
{
	const size_t block = h.rate / 100;
	long first = -1, last = -1;
	for (size_t b = 0; block > 0 && (b + 1) * block <= h.left.size(); ++b)
		if (rms( h.left, b * block, (b + 1) * block ) + rms( h.right, b * block, (b + 1) * block ) > floor)
		{
			if (first < 0) first = (long)b;
			last = (long)b;
		}
	return first < 0 ? 0.0 : (last - first + 1) / 100.0;
}

std::string capturePath( const char *name )
{
	return std::string( getenv( "ZH_USER_DATA_DIR" ) ) + "/" + name + ".wav";
}

// The test's own reading of a sound file's length: its fmt chunk, and fact's frame count when compressed
double fileSeconds( const AsciiString &path )
{
	File *file = TheFileSystem->openFile( path.str(), File::READ | File::BINARY );
	if (file == NULL)
		return -1;
	std::vector<unsigned char> b( file->size() );
	const bool read = !b.empty() && file->read( &b[0], (Int)b.size() ) == (Int)b.size();
	file->close();
	if (!read || memcmp( &b[0], "RIFF", 4 ) != 0)
		return -1;
	unsigned rate = 0, blockAlign = 0, factFrames = 0, format = 0;
	size_t dataLen = 0;
	for (size_t at = 12; at + 8 <= b.size(); )
	{
		const unsigned len = le32( &b[at + 4] );
		if (memcmp( &b[at], "fmt ", 4 ) == 0) { format = le16( &b[at + 8] ); rate = le32( &b[at + 12] ); blockAlign = le16( &b[at + 20] ); }
		if (memcmp( &b[at], "fact", 4 ) == 0) factFrames = le32( &b[at + 8] );
		if (memcmp( &b[at], "data", 4 ) == 0) dataLen = len;
		at += 8 + len + (len & 1);
	}
	if (rate == 0)
		return -1;
	if (format != 1 && factFrames != 0)
		return (double)factFrames / rate;
	return blockAlign ? (double)(dataLen / blockAlign) / rate : -1;
}

// ---- The events: the install's own, picked by what they are, first by name ------------------------

struct Pick
{
	AsciiString name;
	AsciiString file;
	double seconds;
};

bool usable( const AudioEventInfo *info )
{
	return info->m_lowPassFreq <= 0.0f && !BitTest( info->m_control, AC_LOOP );
}

Pick pickEvent( AudioType soundType, UnsignedInt mustHave, UnsignedInt mustNotHave, double maxSeconds )
{
	std::vector<AsciiString> names;
	const AudioEventInfoHash &all = TheAudio->getAllAudioEvents();
	for (AudioEventInfoHash::const_iterator it = all.begin(); it != all.end(); ++it)
	{
		const AudioEventInfo *info = it->second;
		if (info->m_soundType == soundType && (info->m_type & mustHave) == mustHave && (info->m_type & mustNotHave) == 0
				&& (soundType == AT_Music || usable( info )))
			names.push_back( it->first );
	}
	std::sort( names.begin(), names.end(), []( const AsciiString &a, const AsciiString &b ) { return strcmp( a.str(), b.str() ) < 0; } );
	for (size_t i = 0; i < names.size(); ++i)
	{
		AudioEventRTS event( names[i] );
		TheAudio->getInfoForAudioEvent( &event );
		if (event.getAudioEventInfo() == NULL)
			continue;
		event.generatePlayInfo();
		event.generateFilename();
		const AsciiString file = event.getFilename();
		if (file.isEmpty())
			continue;
		const double seconds = soundType == AT_Music ? 1.0 : fileSeconds( file );
		if (seconds > 0.2 && seconds <= maxSeconds)
		{
			Pick p = { names[i], file, seconds };
			return p;
		}
	}
	Pick none = { AsciiString::TheEmptyString, AsciiString::TheEmptyString, 0 };
	return none;
}

AudioHandle play( const Pick &pick, const Coord3D *at = NULL )
{
	AudioEventRTS event( pick.name );
	if (at != NULL)
		event.setPosition( at );
	event.setUninterruptable( TRUE );		// skips shouldPlayLocally: no player list here
	return TheAudio->addAudioEvent( &event );
}

}  // namespace

// ---- -headless first: a fresh process has never opened the device --------------------------------

TEST(headless_makes_the_same_manager_and_never_opens_a_device)
{
	mountTheInstall();
	// parseHeadless's audio flags, exactly
	TheWritableGlobalData->m_audioOn = FALSE;
	TheWritableGlobalData->m_musicOn = FALSE;
	TheWritableGlobalData->m_soundsOn = FALSE;
	TheWritableGlobalData->m_speechOn = FALSE;
	TestAudio *headless = NEW TestAudio;
	TheAudio = headless;
	headless->init();
	headless->postProcessLoad();
	CHECK( !TheAudio->getAllAudioEvents().empty() );		// its INIs were read all the same
	// the capture refuses without a ready device: none was opened
	CHECK_EQ( AIL_ex_start_capture( capturePath( "headless" ).c_str() ), 0 );
	// and it takes events and updates as the Windows headless run does, playing nothing
	Pick any = pickEvent( AT_SoundEffect, ST_UI, 0, 5.0 );
	CHECK( !any.name.isEmpty() );
	const AudioHandle handle = play( any );
	headless->runFor( 100 );
	CHECK( !headless->isCurrentlyPlaying( handle ) );
	CHECK_EQ( (int)headless->getNum2DSamples(), 0 );		// no device, no voices
	delete headless;
	TheAudio = NULL;
	TheWritableGlobalData->m_audioOn = TRUE;
	TheWritableGlobalData->m_musicOn = TRUE;
	TheWritableGlobalData->m_soundsOn = TRUE;
	TheWritableGlobalData->m_speechOn = TRUE;
}

// ---- The real run ---------------------------------------------------------------------------------

TEST(the_manager_opens_the_device_and_is_silent_with_nothing_playing)
{
	TestAudio *audio = theAudio();
	CHECK( audio->isOn( AudioAffect_Sound ) );
	CHECK( AIL_ex_start_capture( capturePath( "silence" ).c_str() ) != 0 );		// arms the headless probe
	audio->runFor( 300 );
	AIL_ex_stop_capture();
	const Heard h = readCapture( capturePath( "silence" ) );
	CHECK( h.ok && h.rate > 0 );
	CHECK( h.left.size() > h.rate / 10 );
	CHECK( rms( h.left ) + rms( h.right ) < 0.0005 );
}

TEST(a_2d_sound_plays_ends_and_is_freed_on_the_main_threads_sweep)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_SoundEffect, ST_UI, ST_WORLD, 1.5 );
	CHECK( !pick.name.isEmpty() );
	if (pick.name.isEmpty())
		return;
	printf( "  2D: %s, %s, %.2f s\n", pick.name.str(), pick.file.str(), pick.seconds );
	CHECK( AIL_ex_start_capture( capturePath( "2d" ).c_str() ) != 0 );
	const AudioHandle handle = play( pick );
	audio->runFor( 50 );		// the request is taken on the next tick
	CHECK( audio->isCurrentlyPlaying( handle ) );
	// past its end with no tick: the completion is queued by the audio thread, not acted on
	audio->runFor( (int)(pick.seconds * 1000) + 400, false );
	CHECK( audio->isCurrentlyPlaying( handle ) );
	// the main thread's sweep does the free
	audio->runFor( 100 );
	CHECK( !audio->isCurrentlyPlaying( handle ) );
	AIL_ex_stop_capture();
	const Heard h = readCapture( capturePath( "2d" ) );
	CHECK( h.ok );
	CHECK( rms( h.left ) + rms( h.right ) > 0.001 );
}

TEST(a_streamed_sound_is_heard_for_as_long_as_its_file_lasts)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_Streaming, 0, 0, 4.0 );
	CHECK( !pick.name.isEmpty() );
	if (pick.name.isEmpty())
		return;
	CHECK( AIL_ex_start_capture( capturePath( "stream" ).c_str() ) != 0 );
	const AudioHandle handle = play( pick );
	audio->runFor( (int)(pick.seconds * 1000) + 800 );
	AIL_ex_stop_capture();
	CHECK( !audio->isCurrentlyPlaying( handle ) );
	const Heard h = readCapture( capturePath( "stream" ) );
	const double heard = audibleSeconds( h );
	printf( "  stream: %s, %s: the file says %.2f s, heard %.2f s\n", pick.name.str(), pick.file.str(), pick.seconds, heard );
	CHECK( h.ok );
	CHECK( heard > pick.seconds * 0.6 && heard < pick.seconds + 0.3 );	// quiet head and tail are not heard
}

TEST(a_3d_sound_leans_to_one_side_and_to_the_other_across_the_listener)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_SoundEffect, ST_WORLD, 0, 2.0 );
	CHECK( !pick.name.isEmpty() );
	if (pick.name.isEmpty())
		return;
	printf( "  3D: %s, %.2f s\n", pick.name.str(), pick.seconds );
	double balance[2] = { 0, 0 };
	for (int side = 0; side < 2; ++side)
	{
		const Coord3D at = { side == 0 ? 60.0f : -60.0f, 0, 0 };
		CHECK( AIL_ex_start_capture( capturePath( side == 0 ? "3d_plus_x" : "3d_minus_x" ).c_str() ) != 0 );
		play( pick, &at );
		audio->runFor( (int)(pick.seconds * 1000) + 300 );
		AIL_ex_stop_capture();
		const Heard h = readCapture( capturePath( side == 0 ? "3d_plus_x" : "3d_minus_x" ) );
		const double l = rms( h.left ), r = rms( h.right );
		CHECK( h.ok && l + r > 0.0005 );
		balance[side] = (r - l) / (r + l + 1e-12);
	}
	printf( "  3D balance, right minus left over both: at +x %.2f, at -x %.2f\n", balance[0], balance[1] );
	CHECK( fabs( balance[0] ) > 0.2 && fabs( balance[1] ) > 0.2 );
	CHECK( balance[0] * balance[1] < 0 );		// armed by itself: a pan that did not follow would lean one way twice
}

TEST(music_plays_and_a_stop_brings_silence)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_Music, 0, 0, 10.0 );
	CHECK( !pick.name.isEmpty() );
	if (pick.name.isEmpty())
		return;
	CHECK( AIL_ex_start_capture( capturePath( "music" ).c_str() ) != 0 );
	const AudioHandle handle = play( pick );
	audio->runFor( 1500 );
	TheAudio->removeAudioEvent( handle );
	audio->runFor( 700 );
	AIL_ex_stop_capture();
	const Heard h = readCapture( capturePath( "music" ) );
	CHECK( h.ok );
	/* Windows onto the capture by position, not by the nominal times above: a tick is 10 ms of sleep
		 plus its own work, so the capture runs longer than the sum (2.2 s asked, 3 s seen under load).
		 0.5-1.0 s in is before the stop however slow the ticks; the last 0.3 s is after it. */
	const size_t playing = h.rate * 1, stopped = h.left.size() > h.rate * 3 / 10 ? h.left.size() - h.rate * 3 / 10 : 0;
	const double during = rms( h.left, h.rate / 2, playing ) + rms( h.right, h.rate / 2, playing );
	const double after = rms( h.left, stopped, h.left.size() ) + rms( h.right, stopped, h.right.size() );
	printf( "  music: %s, %s: level %.4f playing, %.4f after the stop\n", pick.name.str(), pick.file.str(), during, after );
	CHECK( during > 0.001 );
	CHECK( after < during * 0.05 );
}

TEST(two_track_changes_in_one_update_leave_one_track_playing)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_Music, 0, 0, 10.0 );
	CHECK( !pick.name.isEmpty() );
	if (pick.name.isEmpty())
		return;
	play( pick );
	audio->runFor( 200 );
	CHECK_EQ( audio->musicPlaying(), 1 );
	// doMusicTrackChange twice before the next update: the second stop used to find the faded track again
	for (int i = 0; i < 2; ++i)
	{
		TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );
		play( pick );
	}
	audio->runFor( 20 );		// one tick for the stops and to warm the file, one to start the last track
	CHECK_EQ( audio->musicPlaying(), 1 );
	// a track started with no stop in front of it replaces the one playing
	play( pick );
	audio->runFor( 20 );
	CHECK_EQ( audio->musicPlaying(), 1 );
	TheAudio->removeAudioEvent( AHSV_StopTheMusic );
	audio->runFor( 100 );
	CHECK_EQ( audio->musicPlaying(), 0 );
}

TEST(with_sound_switched_off_nothing_is_heard)
{
	TestAudio *audio = theAudio();
	const Pick pick = pickEvent( AT_SoundEffect, ST_UI, ST_WORLD, 1.5 );
	audio->setOn( FALSE, AudioAffect_All );
	CHECK( AIL_ex_start_capture( capturePath( "off" ).c_str() ) != 0 );
	play( pick );
	audio->runFor( (int)(pick.seconds * 1000) + 200 );
	AIL_ex_stop_capture();
	audio->setOn( TRUE, AudioAffect_All );
	const Heard h = readCapture( capturePath( "off" ) );
	CHECK( h.ok );
	CHECK( rms( h.left ) + rms( h.right ) < 0.0005 );
}

// ---- ScopedMutex off Windows keeps Windows' semantics ---------------------------------------------

TEST(scoped_mutex_waits_500_ms_then_goes_ahead_and_never_frees_a_lock_it_did_not_take)
{
	std::timed_mutex mutex;
	std::atomic<bool> held( false ), release( false );
	std::thread holder( [&]() {
		mutex.lock();
		held = true;
		while (!release)
			std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
		mutex.unlock();
	} );
	while (!held)
		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	const auto start = std::chrono::steady_clock::now();
	{
		ScopedMutex lock( &mutex );		// WaitForSingleObject( m, 500 ) timing out
	}
	const long waited = (long)std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start ).count();
	printf( "  waited %ld ms for a mutex another thread held\n", waited );
	CHECK( waited >= 450 && waited < 900 );
	CHECK( !mutex.try_lock() );		// the other thread still has it: nothing was released that was not taken
	release = true;
	holder.join();
	{
		ScopedMutex lock( &mutex );		// free: taken at once...
		CHECK( !mutex.try_lock() );
	}
	CHECK( mutex.try_lock() );			// ...and given back
	mutex.unlock();
}
