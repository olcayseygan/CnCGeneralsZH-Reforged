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
 * The Miles surface on miniaudio (Miles6/miniaudio/miles_miniaudio.cpp), measured.
 *
 * Runs on miniaudio's null backend (ctest sets ZH_AUDIO_BACKEND=null): a device with no hardware
 * that still runs the real mix in real time.  The mix is listened to through AIL_ex_start_capture,
 * the same tap a player's -captureAudio run uses, so every level below is what would have come out
 * of the speakers.
 *
 * The expected numbers are worked out by hand from miles_xaudio2.cpp's formulas, not taken from the
 * code under test: a sine of amplitude 8000/32768 has an RMS of 0.17263, Miles' constant-power pan
 * puts sqrt(1 - pan) of it on the left and sqrt(pan) on the right, and XAudio2's output matrix sums
 * every source channel into each speaker.  If this file and miles_miniaudio agreed with each other
 * and not with miles_xaudio2, these numbers would catch it.
 *
 * The half that reads the game's own audio needs ZH_DATA_DIR (a folder holding generals/ and
 * zerohour/, read only) and says "skip" without it.  That half checks:
 *   - every WAV header against miniaudio's own WAV reader (dr_wav), an independent implementation;
 *   - every IMA ADPCM sound decoded by AIL_decompress_ADPCM against dr_wav's decode, sample for
 *     sample - the decoder in AIL_decompress_ADPCM is miles_xaudio2's, unchanged, so this is a
 *     check on the Windows build's decoder too;
 *   - every music MP3's length through AIL_stream_ms_position against a count of its MPEG frame
 *     headers.
 *
 * What it does not establish: that anything sounds right to a person (tools: miles_listen), the
 * behaviour of a real device's clock rather than the null backend's, and anything about XAudio2's
 * own resampler - the one stage known not to produce the same samples (C4).
 */
#include "test_harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "MSS/MSS.h"
#include "miniaudio.h"

// ---------------------------------------------------------------------------------------------
// The tone every level check plays: 440Hz at amplitude 8000, 22050Hz 16-bit, like the game's
// effects.
// ---------------------------------------------------------------------------------------------

static const unsigned TONE_RATE = 22050;
static const double TONE_HZ = 440.0;
static const double TONE_AMPLITUDE = 8000.0;
static const double TONE_RMS = TONE_AMPLITUDE / 32768.0 / sqrt(2.0);	// 0.17263
static const unsigned TONE_MS = 400;
static const double RELATIVE_TOLERANCE = 0.03;
static const double SILENT_RMS = 0.002;

static void put16(unsigned char *p, unsigned value) { p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); }
static void put32(unsigned char *p, unsigned value) { put16(p, value & 0xffff); put16(p + 2, value >> 16); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned get32(const unsigned char *p) { return get16(p) | (get16(p + 2) << 16); }

static std::vector<unsigned char> makeTone(unsigned channels, unsigned milliseconds)
{
	const unsigned frames = TONE_RATE * milliseconds / 1000;
	const unsigned payload = frames * channels * 2;
	std::vector<unsigned char> image(44 + payload);
	memcpy(&image[0], "RIFF", 4);
	put32(&image[4], 36 + payload);
	memcpy(&image[8], "WAVEfmt ", 8);
	put32(&image[16], 16);
	put16(&image[20], 1);
	put16(&image[22], channels);
	put32(&image[24], TONE_RATE);
	put32(&image[28], TONE_RATE * channels * 2);
	put16(&image[32], channels * 2);
	put16(&image[34], 16);
	memcpy(&image[36], "data", 4);
	put32(&image[40], payload);
	for (unsigned frame = 0; frame < frames; ++frame) {
		const short value = (short)(sin(2.0 * M_PI * TONE_HZ * frame / TONE_RATE) * TONE_AMPLITUDE);
		for (unsigned channel = 0; channel < channels; ++channel) {
			put16(&image[44 + (frame * channels + channel) * 2], (unsigned short)value);
		}
	}
	return image;
}

static bool near(double actual, double expected)
{
	return fabs(actual - expected) <= RELATIVE_TOLERANCE * expected;
}

// ---------------------------------------------------------------------------------------------
// The device, once, and the capture that listens to it
// ---------------------------------------------------------------------------------------------

static HDIGDRIVER g_driver = NULL;

static bool audio()
{
	static int state = 0;	// 0 untried, 1 up, -1 unavailable
	if (state == 0) {
		AIL_startup();
		state = AIL_quick_startup(1, 0, 44100, 16, 2) ? 1 : -1;
		if (state == 1) {
			AIL_quick_handles(&g_driver, NULL, NULL);
		} else {
			printf("  skip: no audio device, not even the null backend\n");
		}
	}
	return state == 1;
}

struct Heard
{
	double left;
	double right;
	double hz;		// of the louder channel, from zero crossings
	unsigned rate;
	bool anything;
};

static std::atomic<int> g_finished(0);
static void AILCALLBACK sampleDone(HSAMPLE) { g_finished.store(1); }
static void AILCALLBACK sample3DDone(H3DSAMPLE) { g_finished.store(1); }
static void AILCALLBACK streamDone(HSTREAM) { g_finished.store(1); }

static bool waitFinished(unsigned timeoutMs)
{
	for (unsigned waited = 0; waited < timeoutMs; waited += 10) {
		if (g_finished.load()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

static std::string tempPath(const char *name)
{
	const char *temp = getenv("TMPDIR");
	std::string path = temp != NULL ? temp : "/tmp";
	if (!path.empty() && path[path.size() - 1] != '/') path += "/";
	char unique[64];
	snprintf(unique, sizeof(unique), "zh_miles_%d_", (int)getpid());
	return path + unique + name;
}

// What the capture file holds: the middle half of the span where anything was audible.
static Heard analyse(const std::string &path)
{
	Heard heard;
	memset(&heard, 0, sizeof(heard));
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL) {
		return heard;
	}
	std::vector<unsigned char> bytes;
	unsigned char buffer[65536];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		bytes.insert(bytes.end(), buffer, buffer + got);
	}
	fclose(file);
	if (bytes.size() < 44 || get16(&bytes[22]) != 2) {
		return heard;
	}
	heard.rate = get32(&bytes[24]);
	const size_t frames = (bytes.size() - 44) / 4;
	std::vector<double> left(frames), right(frames);
	size_t first = frames, last = 0;
	for (size_t i = 0; i < frames; ++i) {
		left[i] = (double)(short)get16(&bytes[44 + i * 4]) / 32767.0;
		right[i] = (double)(short)get16(&bytes[44 + i * 4 + 2]) / 32767.0;
		if (fabs(left[i]) > 0.003 || fabs(right[i]) > 0.003) {
			if (first == frames) first = i;
			last = i;
		}
	}
	if (first == frames || last <= first + 64) {
		return heard;
	}
	heard.anything = true;
	const size_t from = first + (last - first) / 4;
	const size_t to = last - (last - first) / 4;
	double sumLeft = 0.0, sumRight = 0.0;
	for (size_t i = from; i < to; ++i) {
		sumLeft += left[i] * left[i];
		sumRight += right[i] * right[i];
	}
	heard.left = sqrt(sumLeft / (double)(to - from));
	heard.right = sqrt(sumRight / (double)(to - from));
	const std::vector<double> &loud = heard.left >= heard.right ? left : right;
	unsigned crossings = 0;
	for (size_t i = from + 1; i < to; ++i) {
		if ((loud[i - 1] < 0.0) != (loud[i] < 0.0)) ++crossings;
	}
	heard.hz = (double)crossings / 2.0 / ((double)(to - from) / (double)heard.rate);
	return heard;
}

// Plays one 2D sample of the tone and returns what came out.
static Heard play2D(unsigned channels, float volume, float pan, S32 rate)
{
	Heard none;
	memset(&none, 0, sizeof(none));
	if (!audio()) return none;
	std::vector<unsigned char> tone = makeTone(channels, TONE_MS);
	const std::string path = tempPath("capture.wav");

	HSAMPLE sample = AIL_allocate_sample_handle(g_driver);
	if (sample == NULL) return none;
	AIL_init_sample(sample);
	AIL_register_EOS_callback(sample, sampleDone);
	AIL_set_sample_file(sample, &tone[0], 0);
	AIL_set_sample_volume_pan(sample, volume, pan);
	if (rate > 0) AIL_set_sample_playback_rate(sample, rate);

	g_finished.store(0);
	AIL_ex_start_capture(path.c_str());
	AIL_start_sample(sample);
	const bool finished = waitFinished(TONE_MS * 4 + 1000);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	AIL_ex_stop_capture();
	AIL_release_sample_handle(sample);

	Heard heard = analyse(path);
	remove(path.c_str());
	CHECK(finished);
	return heard;
}

// ---------------------------------------------------------------------------------------------
// Levels, against miles_xaudio2's curves
// ---------------------------------------------------------------------------------------------

TEST(centre_pan_is_constant_power)
{
	Heard heard = play2D(1, 1.0f, 0.5f, 0);
	if (!audio()) return;
	// sqrt(0.5) of the tone on each side, not 0.5: 0.12207.
	CHECK(near(heard.left, TONE_RMS * sqrt(0.5)));
	CHECK(near(heard.right, TONE_RMS * sqrt(0.5)));
	CHECK(near(heard.hz, TONE_HZ));
}

TEST(hard_pans_leave_the_other_speaker_silent)
{
	Heard left = play2D(1, 1.0f, 0.0f, 0);
	if (!audio()) return;
	CHECK(near(left.left, TONE_RMS));
	CHECK(left.right < SILENT_RMS);

	Heard right = play2D(1, 1.0f, 1.0f, 0);
	CHECK(near(right.right, TONE_RMS));
	CHECK(right.left < SILENT_RMS);
}

TEST(a_quarter_pan_follows_the_square_root_law)
{
	// Linear panning would give 0.75/0.25; Miles gives sqrt(0.75)/sqrt(0.25).
	Heard heard = play2D(1, 1.0f, 0.25f, 0);
	if (!audio()) return;
	CHECK(near(heard.left, TONE_RMS * sqrt(0.75)));
	CHECK(near(heard.right, TONE_RMS * sqrt(0.25)));
}

TEST(volume_scales_linearly)
{
	Heard heard = play2D(1, 0.5f, 0.5f, 0);
	if (!audio()) return;
	CHECK(near(heard.left, 0.5 * TONE_RMS * sqrt(0.5)));
	CHECK(near(heard.right, 0.5 * TONE_RMS * sqrt(0.5)));
}

TEST(a_stereo_source_is_summed_into_both_speakers)
{
	// miles_xaudio2's output matrix puts every source channel into each speaker at the pan gain, so
	// a stereo sound with the same signal on both sides comes out twice as loud per speaker, not
	// kept left and right.  That is what Windows plays; pinned so nobody "fixes" one platform.
	Heard heard = play2D(2, 1.0f, 0.5f, 0);
	if (!audio()) return;
	CHECK(near(heard.left, 2.0 * TONE_RMS * sqrt(0.5)));
	CHECK(near(heard.right, 2.0 * TONE_RMS * sqrt(0.5)));
}

TEST(playback_rate_is_pitch)
{
	Heard heard = play2D(1, 1.0f, 0.5f, (S32)(TONE_RATE * 2));
	if (!audio()) return;
	CHECK(near(heard.hz, TONE_HZ * 2.0));
}

// ---------------------------------------------------------------------------------------------
// 3D: miles_xaudio2's min / distance falloff between the two distances (a straight line with
// AIL_ex_set_3D_linear_falloff), occlusion as a low-pass, and a pan along up x face.
// ---------------------------------------------------------------------------------------------

static Heard play3D(float x, float occlusion)
{
	Heard none;
	memset(&none, 0, sizeof(none));
	if (!audio()) return none;
	std::vector<unsigned char> tone = makeTone(1, TONE_MS);
	const std::string path = tempPath("capture3d.wav");

	HPROENUM next = HPROENUM_FIRST;
	HPROVIDER provider = 0;
	char *name = NULL;
	AIL_enumerate_3D_providers(&next, &provider, &name);
	H3DPOBJECT listener = AIL_open_3D_listener(provider);
	// face +Y, up +Z: up x face is -X, so -X is the right speaker.
	AIL_set_3D_orientation(listener, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	AIL_set_3D_position(listener, 0.0f, 0.0f, 0.0f);

	H3DSAMPLE sample = AIL_allocate_3D_sample_handle(provider);
	if (sample == NULL) return none;
	AIL_register_3D_EOS_callback(sample, sample3DDone);
	AIL_set_3D_sample_file(sample, &tone[0]);
	AIL_set_3D_sample_volume(sample, 1.0f);
	AIL_set_3D_sample_distances(sample, 110.0f, 10.0f);
	AIL_set_3D_sample_occlusion(sample, occlusion);
	AIL_set_3D_position(sample, x, 0.0f, 0.0f);

	g_finished.store(0);
	AIL_ex_start_capture(path.c_str());
	AIL_start_3D_sample(sample);
	const bool finished = waitFinished(TONE_MS * 4 + 1000);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	AIL_ex_stop_capture();
	AIL_release_3D_sample_handle(sample);
	AIL_close_3D_listener(listener);

	Heard heard = analyse(path);
	remove(path.c_str());
	CHECK(finished);
	return heard;
}

TEST(falloff_is_the_minimum_over_the_distance)
{
	// 60 units out with the minimum at 10: a sixth of the volume, all of it on the right.  Full
	// volume here is the arguments swapped (minimum 110, maximum 10), which is how every world
	// sound in the game played until the call site was fixed.
	Heard heard = play3D(-60.0f, 0.0f);
	if (!audio()) return;
	CHECK(near(heard.right, TONE_RMS / 6.0));
	CHECK(heard.left < SILENT_RMS);
}

TEST(linear_falloff_is_a_straight_line_between_the_distances)
{
	// RangeVolumeFade: 60 units out, halfway from 10 to 110, is half the volume.
	AIL_ex_set_3D_linear_falloff(1);
	Heard heard = play3D(-60.0f, 0.0f);
	AIL_ex_set_3D_linear_falloff(0);
	if (!audio()) return;
	CHECK(near(heard.right, 0.5 * TONE_RMS));
	CHECK(heard.left < SILENT_RMS);
}

TEST(occlusion_is_a_low_pass_and_inside_the_minimum_is_full)
{
	// Occlusion is a filter (MSS.h's AIL_ex_3D_cutoff): half occluded is a one-pole at 5 kHz, which leaves the 440 Hz tone at 0.993 of itself.
	Heard heard = play3D(-5.0f, 0.5f);
	if (!audio()) return;
	CHECK(near(heard.right, TONE_RMS));
	CHECK(heard.left < SILENT_RMS);
}

TEST(beyond_the_maximum_is_silent_and_still_ends)
{
	Heard heard = play3D(-200.0f, 0.0f);
	if (!audio()) return;
	CHECK(!heard.anything);
}

// ---------------------------------------------------------------------------------------------
// Streams, through the file callbacks
// ---------------------------------------------------------------------------------------------

struct OpenFile
{
	FILE *file;
	long base;
	long size;
	long at;
};

// Stream names are either a path or "<archive>|<entry>", an entry inside a .big.
static std::map<std::string, std::pair<long, long> > g_bigEntries;	// "<archive>|<entry>" -> offset, size

static U32 AILCALLBACK fileOpen(const char *name, AILFILEHANDLE *handle)
{
	std::string path = name;
	long base = 0, size = -1;
	const size_t bar = path.find('|');
	if (bar != std::string::npos) {
		std::map<std::string, std::pair<long, long> >::iterator it = g_bigEntries.find(path);
		if (it == g_bigEntries.end()) return 0;
		base = it->second.first;
		size = it->second.second;
		path = path.substr(0, bar);
	}
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL) return 0;
	if (size < 0) {
		fseek(file, 0, SEEK_END);
		size = ftell(file);
	}
	OpenFile *open = new OpenFile;
	open->file = file;
	open->base = base;
	open->size = size;
	open->at = 0;
	*handle = (AILFILEHANDLE)open;
	return 1;
}

static void AILCALLBACK fileClose(AILFILEHANDLE handle)
{
	OpenFile *open = (OpenFile *)handle;
	fclose(open->file);
	delete open;
}

static S32 AILCALLBACK fileSeek(AILFILEHANDLE handle, S32 offset, U32 whence)
{
	OpenFile *open = (OpenFile *)handle;
	long target = whence == SEEK_CUR ? open->at + offset : (whence == SEEK_END ? open->size + offset : offset);
	if (target < 0) target = 0;
	if (target > open->size) target = open->size;
	open->at = target;
	return (S32)target;
}

static U32 AILCALLBACK fileRead(AILFILEHANDLE handle, void *buffer, U32 bytes)
{
	OpenFile *open = (OpenFile *)handle;
	const long left = open->size - open->at;
	const long wanted = (long)bytes < left ? (long)bytes : left;
	if (wanted <= 0) return 0;
	fseek(open->file, open->base + open->at, SEEK_SET);
	const size_t got = fread(buffer, 1, (size_t)wanted, open->file);
	open->at += (long)got;
	return (U32)got;
}

TEST(a_stream_loops_and_its_position_keeps_counting)
{
	if (!audio()) return;
	std::vector<unsigned char> tone = makeTone(1, TONE_MS);
	const std::string path = tempPath("stream.wav");
	FILE *out = fopen(path.c_str(), "wb");
	CHECK(out != NULL);
	if (out == NULL) return;
	fwrite(&tone[0], 1, tone.size(), out);
	fclose(out);

	AIL_set_file_callbacks(fileOpen, fileClose, fileSeek, fileRead);
	HSTREAM stream = AIL_open_stream(g_driver, path.c_str(), 0);
	CHECK(stream != NULL);
	if (stream != NULL) {
		S32 total = 0;
		AIL_stream_ms_position(stream, &total, NULL);
		CHECK(total == (S32)TONE_MS);

		AIL_register_stream_callback(stream, streamDone);
		AIL_set_stream_loop_count(stream, 3);
		const std::string capture = tempPath("capturestream.wav");
		g_finished.store(0);
		AIL_ex_start_capture(capture.c_str());
		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		AIL_set_stream_volume_pan(stream, 0.5f, 0.0f);
		AIL_start_stream(stream);
		CHECK(waitFinished(TONE_MS * 3 * 3 + 1000));
		const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		AIL_ex_stop_capture();

		// Three passes: at least three tones' worth of wall time, and the position counts all three.
		CHECK(elapsed >= TONE_MS * 3 * 0.9);
		S32 position = 0;
		AIL_stream_ms_position(stream, NULL, &position);
		CHECK(position >= (S32)(TONE_MS * 3 * 0.95) && position <= (S32)(TONE_MS * 3 * 1.05));
		CHECK(AIL_stream_loop_count(stream) == 1);

		// A stream's volume and pan are the same curves as a sample's.
		Heard heard = analyse(capture);
		CHECK(near(heard.left, 0.5 * TONE_RMS));
		CHECK(heard.right < SILENT_RMS);
		remove(capture.c_str());
		AIL_close_stream(stream);
	}
	remove(path.c_str());
}

// ---------------------------------------------------------------------------------------------
// The game's own audio (ZH_DATA_DIR)
// ---------------------------------------------------------------------------------------------

struct BigEntry
{
	std::string name;
	long offset;
	long size;
};

static std::string dataDir()
{
	const char *dir = getenv("ZH_DATA_DIR");
	return dir != NULL ? dir : "";
}

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

// A .big is "BIGF", sizes, then offset/size/name for each entry, all big-endian.
static std::vector<BigEntry> readBig(const std::string &path)
{
	std::vector<BigEntry> entries;
	FILE *file = fopen(path.c_str(), "rb");
	if (file == NULL) return entries;
	unsigned char header[16];
	if (fread(header, 1, 16, file) == 16 && memcmp(header, "BIGF", 4) == 0) {
		const unsigned count = be32(header + 8);
		for (unsigned i = 0; i < count; ++i) {
			unsigned char pair[8];
			if (fread(pair, 1, 8, file) != 8) break;
			BigEntry entry;
			entry.offset = (long)be32(pair);
			entry.size = (long)be32(pair + 4);
			int c;
			while ((c = fgetc(file)) != EOF && c != 0) entry.name += (char)c;
			entries.push_back(entry);
		}
	}
	fclose(file);
	return entries;
}

static std::vector<unsigned char> readEntry(const std::string &path, const BigEntry &entry)
{
	std::vector<unsigned char> bytes((size_t)entry.size);
	FILE *file = fopen(path.c_str(), "rb");
	if (file != NULL) {
		fseek(file, entry.offset, SEEK_SET);
		const size_t got = fread(bytes.empty() ? NULL : &bytes[0], 1, bytes.size(), file);
		bytes.resize(got);
		fclose(file);
	}
	return bytes;
}

static bool endsWith(const std::string &text, const char *suffix)
{
	const size_t n = strlen(suffix);
	if (text.size() < n) return false;
	for (size_t i = 0; i < n; ++i) {
		if (tolower((unsigned char)text[text.size() - n + i]) != suffix[i]) return false;
	}
	return true;
}

static const char *const WAV_ARCHIVES[] = {
	"zerohour/AudioZH.big", "zerohour/AudioEnglishZH.big", "zerohour/SpeechZH.big",
	"zerohour/SpeechEnglishZH.big", "generals/Audio.big", "generals/AudioEnglish.big",
	"generals/Speech.big", "generals/SpeechEnglish.big",
};

TEST(every_wav_header_and_adpcm_decode_matches_dr_wav)
{
	const std::string dir = dataDir();
	struct stat info;
	if (dir.empty() || stat((dir + "/zerohour").c_str(), &info) != 0) {
		printf("  skip: no game data (ZH_DATA_DIR)\n");
		return;
	}

	unsigned pcmFiles = 0, adpcmFiles = 0, headerMismatches = 0, lengthMismatches = 0, sampleMismatches = 0;
	unsigned long long adpcmFrames = 0;
	for (size_t a = 0; a < sizeof(WAV_ARCHIVES) / sizeof(WAV_ARCHIVES[0]); ++a) {
		const std::string archive = dir + "/" + WAV_ARCHIVES[a];
		std::vector<BigEntry> entries = readBig(archive);
		for (size_t e = 0; e < entries.size(); ++e) {
			if (!endsWith(entries[e].name, ".wav")) continue;
			std::vector<unsigned char> bytes = readEntry(archive, entries[e]);
			AILSOUNDINFO sound;
			memset(&sound, 0, sizeof(sound));
			if (bytes.size() < 44 || !AIL_WAV_info(&bytes[0], &sound)) { ++headerMismatches; continue; }

			// dr_wav's frame count and 16-bit decode, an implementation this file did not write.
			ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
			ma_decoder decoder;
			if (ma_decoder_init_memory(&bytes[0], bytes.size(), &config, &decoder) != MA_SUCCESS) { ++headerMismatches; continue; }
			ma_uint64 drFrames = 0;
			ma_decoder_get_length_in_pcm_frames(&decoder, &drFrames);
			if (decoder.outputSampleRate != sound.rate || decoder.outputChannels != (ma_uint32)sound.channels) {
				++headerMismatches;
			}

			if (sound.format == WAVE_FORMAT_PCM) {
				++pcmFiles;
				if ((ma_uint64)sound.samples != drFrames) ++lengthMismatches;
			} else if (sound.format == 0x11) {
				++adpcmFiles;
				void *pcm = NULL;
				U32 pcmBytes = 0;
				if (!AIL_decompress_ADPCM(&sound, &pcm, &pcmBytes)) { ++lengthMismatches; ma_decoder_uninit(&decoder); continue; }
				const unsigned char *ours = (const unsigned char *)pcm + 44;
				const ma_uint64 ourFrames = (pcmBytes - 44) / 2 / sound.channels;
				adpcmFrames += ourFrames;
				if (ourFrames != drFrames || (ma_uint64)sound.samples != drFrames) ++lengthMismatches;

				std::vector<short> theirs((size_t)drFrames * sound.channels);
				ma_uint64 read = 0;
				ma_decoder_read_pcm_frames(&decoder, theirs.empty() ? NULL : &theirs[0], drFrames, &read);
				const ma_uint64 compare = read < ourFrames ? read : ourFrames;
				for (ma_uint64 i = 0; i < compare * sound.channels; ++i) {
					if ((short)get16(ours + i * 2) != theirs[(size_t)i]) { ++sampleMismatches; break; }
				}
				AIL_mem_free_lock(pcm);
			}
			ma_decoder_uninit(&decoder);
		}
	}
	printf("  %u PCM and %u IMA ADPCM sounds (%llu ADPCM frames); header %u, length %u, sample %u mismatches\n",
		pcmFiles, adpcmFiles, adpcmFrames, headerMismatches, lengthMismatches, sampleMismatches);
	CHECK(pcmFiles > 0 && adpcmFiles > 0);
	CHECK_EQ(headerMismatches, 0u);
	CHECK_EQ(lengthMismatches, 0u);
	CHECK_EQ(sampleMismatches, 0u);
}

// Counts an MP3's audio frames from its own headers: layer III, 1152 samples a frame for MPEG-1 and
// 576 for MPEG-2 and 2.5 (Silence60.mp3 is MPEG-2 at 22050 Hz).  The Xing/Info frame a VBR or LAME
// file opens with carries no audio; LAME's tag says how many samples of encoder delay and padding
// the decoder is to drop.  Only whole frames count: USA_09.mp3's last frame is cut off 436 bytes
// short, and dr_mp3 drops a frame it cannot finish where FFmpeg decodes it anyway (a recorded
// deviation, C4).
struct Mp3Count
{
	unsigned frames;
	unsigned samplesPerFrame;
	unsigned rate;
	bool infoFrame;
	unsigned delay;
	unsigned padding;
};

static Mp3Count scanMp3(const std::vector<unsigned char> &bytes)
{
	static const unsigned BITRATES_V1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
	static const unsigned BITRATES_V2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
	static const unsigned RATES_V1[4] = { 44100, 48000, 32000, 0 };
	Mp3Count count;
	memset(&count, 0, sizeof(count));
	size_t at = 0;
	if (bytes.size() > 10 && memcmp(&bytes[0], "ID3", 3) == 0) {
		at = 10 + (((size_t)bytes[6] & 0x7f) << 21 | ((size_t)bytes[7] & 0x7f) << 14 | ((size_t)bytes[8] & 0x7f) << 7 | ((size_t)bytes[9] & 0x7f));
	}
	bool first = true;
	while (at + 4 <= bytes.size()) {
		const unsigned char *h = &bytes[at];
		// Frame sync, then layer III (01) in any version but the reserved one (01).
		const unsigned version = (h[1] >> 3) & 3;	// 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
		if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0 || ((h[1] >> 1) & 3) != 1 || version == 1) { ++at; continue; }
		const bool mpeg1 = version == 3;
		const unsigned bitrate = (mpeg1 ? BITRATES_V1 : BITRATES_V2)[h[2] >> 4] * 1000;
		const unsigned baseRate = RATES_V1[(h[2] >> 2) & 3];
		const unsigned rate = mpeg1 ? baseRate : (version == 2 ? baseRate / 2 : baseRate / 4);
		if (bitrate == 0 || rate == 0) { ++at; continue; }
		const size_t length = (mpeg1 ? 144 : 72) * bitrate / rate + ((h[2] >> 1) & 1);
		if (at + length > bytes.size()) break;
		if (first) {
			first = false;
			count.rate = rate;
			count.samplesPerFrame = mpeg1 ? 1152 : 576;
			const bool mono = (h[3] >> 6) == 3;
			const size_t sideInfo = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
			const size_t tag = at + 4 + sideInfo;
			if (tag + 4 <= bytes.size() && (memcmp(&bytes[tag], "Xing", 4) == 0 || memcmp(&bytes[tag], "Info", 4) == 0)) {
				count.infoFrame = true;
				// LAME's tag sits 120 bytes into Xing's, with the 12-bit delay and padding at +21.
				const size_t lame = tag + 120;
				if (lame + 24 <= bytes.size() && memcmp(&bytes[lame], "LAME", 4) == 0) {
					count.delay = ((unsigned)bytes[lame + 21] << 4) | (bytes[lame + 22] >> 4);
					count.padding = (((unsigned)bytes[lame + 22] & 0x0f) << 8) | bytes[lame + 23];
				}
				at += length;
				continue;
			}
		}
		++count.frames;
		at += length;
	}
	return count;
}

TEST(every_music_mp3_length_matches_its_frame_headers)
{
	const std::string dir = dataDir();
	struct stat info;
	if (dir.empty() || stat((dir + "/zerohour").c_str(), &info) != 0) {
		printf("  skip: no game data (ZH_DATA_DIR)\n");
		return;
	}
	if (!audio()) return;
	AIL_set_file_callbacks(fileOpen, fileClose, fileSeek, fileRead);

	static const char *const MUSIC[] = { "zerohour/MusicZH.big", "generals/Music.big" };
	unsigned files = 0, outside = 0;
	double worst = 0.0;
	for (size_t a = 0; a < 2; ++a) {
		const std::string archive = dir + "/" + MUSIC[a];
		std::vector<BigEntry> entries = readBig(archive);
		for (size_t e = 0; e < entries.size(); ++e) {
			if (!endsWith(entries[e].name, ".mp3")) continue;
			const std::string name = archive + "|" + entries[e].name;
			g_bigEntries[name] = std::make_pair(entries[e].offset, entries[e].size);
			Mp3Count scanned = scanMp3(readEntry(archive, entries[e]));

			HSTREAM stream = AIL_open_stream(g_driver, name.c_str(), 0);
			if (stream == NULL) { ++outside; continue; }
			S32 total = 0;
			AIL_stream_ms_position(stream, &total, NULL);
			AIL_close_stream(stream);
			++files;

			// The decoder drops LAME's delay and padding, and the position truncates to whole
			// milliseconds, so the match is exact: a frame gained or lost is 13 or 26ms.
			const double samples = (double)scanned.frames * scanned.samplesPerFrame - scanned.delay - scanned.padding;
			const double expectedMs = scanned.rate != 0 ? samples * 1000.0 / scanned.rate : 0.0;
			const double off = fabs((double)total - expectedMs);
			if (off > worst) worst = off;
			if (scanned.frames == 0 || total != (S32)expectedMs) {
				++outside;
				printf("  %s: %d ms, headers say %.1f ms (%u frames, info %d, delay %u, padding %u)\n",
					entries[e].name.c_str(), (int)total, expectedMs, scanned.frames, scanned.infoFrame, scanned.delay, scanned.padding);
			}
		}
	}
	printf("  %u of %u MP3 streams match to the millisecond; largest fraction truncated %.2f ms\n", files - outside, files, worst);
	CHECK(files > 0);
	CHECK_EQ(outside, 0u);
}

TEST(adpcm_speech_streams_through_the_file_callbacks)
{
	const std::string dir = dataDir();
	struct stat info;
	if (dir.empty() || stat((dir + "/zerohour").c_str(), &info) != 0) {
		printf("  skip: no game data (ZH_DATA_DIR)\n");
		return;
	}
	if (!audio()) return;
	AIL_set_file_callbacks(fileOpen, fileClose, fileSeek, fileRead);

	// Speech is IMA ADPCM and is streamed, so this is dr_wav through the game's callbacks.
	const std::string archive = dir + "/zerohour/SpeechEnglishZH.big";
	std::vector<BigEntry> entries = readBig(archive);
	for (size_t e = 0; e < entries.size(); ++e) {
		std::vector<unsigned char> bytes = readEntry(archive, entries[e]);
		AILSOUNDINFO sound;
		memset(&sound, 0, sizeof(sound));
		if (!endsWith(entries[e].name, ".wav") || bytes.size() < 44 || !AIL_WAV_info(&bytes[0], &sound) ||
			sound.format != 0x11 || sound.samples < sound.rate) {
			continue;	// the first ADPCM line longer than a second
		}

		const std::string name = archive + "|" + entries[e].name;
		g_bigEntries[name] = std::make_pair(entries[e].offset, entries[e].size);
		HSTREAM stream = AIL_open_stream(g_driver, name.c_str(), 0);
		CHECK(stream != NULL);
		if (stream == NULL) return;
		S32 total = 0;
		AIL_stream_ms_position(stream, &total, NULL);
		CHECK_EQ(total, (S32)((double)sound.samples * 1000.0 / sound.rate));

		AIL_start_stream(stream);
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		S32 position = 0;
		AIL_stream_ms_position(stream, NULL, &position);
		CHECK(position >= 200);
		AIL_close_stream(stream);
		printf("  %s: %d ms, %d ms in after 400ms of wall time\n", entries[e].name.c_str(), (int)total, (int)position);
		return;
	}
	CHECK(false);	// no ADPCM speech found
}
