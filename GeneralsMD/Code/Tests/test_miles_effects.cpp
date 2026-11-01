/*
 * Doppler and the limiter of the Miles surface on miniaudio (MSS.h's AIL_ex_set_3D_air and
 * AIL_ex_limit), measured in the mix a -wav run records, and what the mix does not do: no echo after
 * a sound has stopped, and no filter that grows with distance, the two that made every map sound
 * like the inside of a tunnel.
 *
 * miles_miniaudio.cpp is compiled into this test on its own, without the XAudio2 half, and run on its
 * offline mix: AIL_ex_offline_mix, then AIL_ex_mix_to_frame moves the mix by logic frames, so every
 * figure below is the same on every run and on a machine with no sound card.  What is heard comes
 * back through AIL_ex_start_capture, the tap -wav writes its file from.  The expected numbers are the
 * physics the helpers in MSS.h claim, worked out here, not read back from them.
 *
 * Also the read of a sound's file off the frame (AudioFileLoader.h), against a disk the test holds,
 * and through this backend's decompress and mix.
 *
 * Not covered: XAudio2's own filter, which needs an audio device.
 */
#include "test_harness.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MSS/MSS.h"
#include "MilesAudioDevice/AudioFileLoader.h"

static const unsigned TONE_RATE = 22050;
static const double TONE_PI = 3.14159265358979323846;	// MSVC's math.h gives M_PI only on request
static const double TONE_AMPLITUDE = 8000.0;
static const int LOGIC_FRAMES_PER_SECOND = 60;	// the logic clock AIL_ex_mix_to_frame counts
static const char CAPTURE_PATH[] = "test_miles_effects.wav";

static void put16(unsigned char *p, unsigned value) { p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); }
static void put32(unsigned char *p, unsigned value) { put16(p, value & 0xffff); put16(p + 2, value >> 16); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned get32(const unsigned char *p) { return get16(p) | (get16(p + 2) << 16); }

// A mono 16-bit sine as a WAV image, the shape of the game's effects.
static std::vector<unsigned char> makeTone(double hz, unsigned milliseconds)
{
	const unsigned frames = TONE_RATE * milliseconds / 1000;
	std::vector<unsigned char> image(44 + frames * 2);
	memcpy(&image[0], "RIFF", 4);
	put32(&image[4], 36 + frames * 2);
	memcpy(&image[8], "WAVEfmt ", 8);
	put32(&image[16], 16);
	put16(&image[20], 1);
	put16(&image[22], 1);
	put32(&image[24], TONE_RATE);
	put32(&image[28], TONE_RATE * 2);
	put16(&image[32], 2);
	put16(&image[34], 16);
	memcpy(&image[36], "data", 4);
	put32(&image[40], frames * 2);
	for (unsigned frame = 0; frame < frames; ++frame) {
		put16(&image[44 + frame * 2], (unsigned short)(short)(sin(2.0 * TONE_PI * hz * frame / TONE_RATE) * TONE_AMPLITUDE));
	}
	return image;
}

// The device once, offline; a listener at the origin facing +Y with +Z up, so -X is the right speaker.
static HPROVIDER g_provider = 0;
static int g_logicFrame = 0;

static void startAudio()
{
	static bool started = false;
	if (started) {
		return;
	}
	started = true;
	AIL_ex_offline_mix();
	AIL_startup();
	AIL_quick_startup(1, 0, 44100, 16, 2);
	HPROENUM next = HPROENUM_FIRST;
	char *name = NULL;
	AIL_enumerate_3D_providers(&next, &g_provider, &name);
	H3DPOBJECT listener = AIL_open_3D_listener(g_provider);
	AIL_set_3D_orientation(listener, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	AIL_set_3D_position(listener, 0.0f, 0.0f, 0.0f);
	AIL_ex_mix_to_frame(g_logicFrame);	// places the mix
}

static void runSeconds(double seconds)
{
	const int frames = (int)(seconds * LOGIC_FRAMES_PER_SECOND);
	for (int i = 0; i < frames; ++i) {
		AIL_ex_mix_to_frame(++g_logicFrame);
	}
}

// A 3D sample of `tone` on the X axis, minimum distance 10 and a maximum out of the way.
static H3DSAMPLE place(const std::vector<unsigned char> &tone, float x, float velocityX, float volume)
{
	H3DSAMPLE sample = AIL_allocate_3D_sample_handle(g_provider);
	AIL_set_3D_sample_file(sample, &tone[0]);
	AIL_set_3D_sample_volume(sample, volume);
	AIL_set_3D_sample_distances(sample, 100000.0f, 10.0f);
	AIL_set_3D_velocity_vector(sample, velocityX, 0.0f, 0.0f);
	AIL_set_3D_position(sample, x, 0.0f, 0.0f);
	return sample;
}

struct Capture
{
	std::vector<double> mono;	// left plus right over two
	unsigned rate;
	double peak;	// the loudest sample of either side, full scale 1
};

static Capture readCapture()
{
	Capture capture;
	capture.rate = 0;
	capture.peak = 0.0;
	FILE *file = fopen(CAPTURE_PATH, "rb");
	if (file == NULL) {
		return capture;
	}
	std::vector<unsigned char> bytes;
	unsigned char buffer[65536];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		bytes.insert(bytes.end(), buffer, buffer + got);
	}
	fclose(file);
	remove(CAPTURE_PATH);
	if (bytes.size() < 44) {
		return capture;
	}
	capture.rate = get32(&bytes[24]);
	const size_t frames = (bytes.size() - 44) / 4;
	capture.mono.resize(frames);
	for (size_t i = 0; i < frames; ++i) {
		const double left = (double)(short)get16(&bytes[44 + i * 4]) / 32767.0;
		const double right = (double)(short)get16(&bytes[44 + i * 4 + 2]) / 32767.0;
		capture.mono[i] = (left + right) / 2.0;
		capture.peak = fabs(left) > capture.peak ? fabs(left) : capture.peak;
		capture.peak = fabs(right) > capture.peak ? fabs(right) : capture.peak;
	}
	return capture;
}

// Plays the samples given for `seconds` and returns what came out.
static Capture listen(const std::vector<H3DSAMPLE> &samples, double seconds)
{
	AIL_ex_start_capture(CAPTURE_PATH);
	for (size_t i = 0; i < samples.size(); ++i) {
		AIL_start_3D_sample(samples[i]);
	}
	runSeconds(seconds);
	AIL_ex_stop_capture();
	for (size_t i = 0; i < samples.size(); ++i) {
		AIL_release_3D_sample_handle(samples[i]);
	}
	return readCapture();
}

static double rms(const Capture &capture, double fromSeconds, double toSeconds)
{
	const size_t from = (size_t)(fromSeconds * capture.rate);
	const size_t to = (size_t)(toSeconds * capture.rate) < capture.mono.size() ? (size_t)(toSeconds * capture.rate) : capture.mono.size();
	double sum = 0.0;
	for (size_t i = from; i < to; ++i) {
		sum += capture.mono[i] * capture.mono[i];
	}
	return to > from ? sqrt(sum / (double)(to - from)) : 0.0;
}

static double frequency(const Capture &capture, double fromSeconds, double toSeconds)
{
	const size_t from = (size_t)(fromSeconds * capture.rate);
	const size_t to = (size_t)(toSeconds * capture.rate);
	unsigned crossings = 0;
	for (size_t i = from + 1; i < to && i < capture.mono.size(); ++i) {
		if ((capture.mono[i - 1] < 0.0) != (capture.mono[i] < 0.0)) {
			++crossings;
		}
	}
	return (double)crossings / 2.0 / (toSeconds - fromSeconds);
}

// The level of an 8 kHz tone at a distance, with the 1/distance falloff taken back out.
static double brightnessAt(float x)
{
	std::vector<unsigned char> tone = makeTone(8000.0, 500);
	std::vector<H3DSAMPLE> samples(1, place(tone, x, 0.0f, 1.0f));
	const Capture capture = listen(samples, 0.6);
	return rms(capture, 0.1, 0.4) / (10.0 / fabs(x));
}

TEST(distance_changes_the_level_and_leaves_the_top_end)
{
	startAudio();
	const double nearLevel = brightnessAt(-20.0f);
	const double farLevel = brightnessAt(-1600.0f);
	// all of it on the right, half of the mono average: 0.17263 / 2 at full volume
	CHECK(nearLevel > 0.05);
	CHECK(fabs(farLevel / nearLevel - 1.0) < 0.05);
}

TEST(doppler_raises_what_comes_and_lowers_what_goes)
{
	startAudio();
	// A quarter of the speed of sound towards the listener is 1700 / 1275, a third up; the same away
	// is 1700 / 2125, a fifth down.
	AIL_ex_set_3D_air(1700.0f);
	std::vector<unsigned char> tone = makeTone(440.0, 500);
	std::vector<H3DSAMPLE> coming(1, place(tone, -500.0f, 425.0f, 1.0f));
	const Capture up = listen(coming, 0.6);
	std::vector<H3DSAMPLE> going(1, place(tone, -500.0f, -425.0f, 1.0f));
	const Capture down = listen(going, 0.6);
	std::vector<H3DSAMPLE> standing(1, place(tone, -500.0f, 0.0f, 1.0f));
	const Capture still = listen(standing, 0.6);
	AIL_ex_set_3D_air(0.0f);

	CHECK_NEAR(frequency(still, 0.05, 0.25), 440.0, 440.0 * 0.02);
	CHECK_NEAR(frequency(up, 0.05, 0.25), 440.0 * 4.0 / 3.0, 440.0 * 0.02);
	CHECK_NEAR(frequency(down, 0.05, 0.25), 440.0 * 0.8, 440.0 * 0.02);
}

TEST(a_sound_that_has_stopped_leaves_no_echo)
{
	startAudio();
	// A tenth of a second of tone, then listen past it, near and far: silence, where the room this
	// mix used to have rang on for two seconds.
	std::vector<unsigned char> tone = makeTone(440.0, 100);
	std::vector<H3DSAMPLE> nearSound(1, place(tone, -20.0f, 0.0f, 1.0f));
	const Capture nearCapture = listen(nearSound, 1.0);
	std::vector<H3DSAMPLE> farSound(1, place(tone, -200.0f, 0.0f, 1.0f));
	const Capture farCapture = listen(farSound, 1.0);

	CHECK(rms(nearCapture, 0.0, 0.1) > 0.02);
	CHECK(rms(nearCapture, 0.15, 0.9) < 0.0002);
	CHECK(rms(farCapture, 0.0, 0.1) > 0.002);
	CHECK(rms(farCapture, 0.15, 0.9) < 0.0002);
}

TEST(a_dry_sample_takes_no_doppler)
{
	startAudio();
	// A unit's reply, coming at a quarter of the speed of sound, keeps its pitch.
	AIL_ex_set_3D_air(1700.0f);
	std::vector<unsigned char> tone = makeTone(440.0, 500);
	std::vector<H3DSAMPLE> reply(1, place(tone, -500.0f, 425.0f, 1.0f));
	AIL_ex_set_3D_sample_dry(reply[0], 1);
	const Capture capture = listen(reply, 0.6);
	AIL_ex_set_3D_air(0.0f);

	CHECK_NEAR(frequency(capture, 0.05, 0.25), 440.0, 440.0 * 0.02);
}

TEST(a_loud_mix_never_reaches_full_scale)
{
	startAudio();
	// Eight tones at full volume inside their minimum distance, all on the right: four times full
	// scale before the limiter, and nothing past its ceiling after it.
	const unsigned TONES = 8;
	std::vector<unsigned char> tones[TONES];
	std::vector<H3DSAMPLE> samples;
	for (unsigned i = 0; i < TONES; ++i) {
		tones[i] = makeTone(110.0 * (i + 1), 1000);
		samples.push_back(place(tones[i], -5.0f, 0.0f, 1.0f));
	}
	const Capture capture = listen(samples, 1.2);

	CHECK(capture.peak <= AIL_EX_LIMITER_CEILING + 0.001);
	CHECK(capture.peak > AIL_EX_LIMITER_CEILING - 0.05);
	CHECK(rms(capture, 1.1, 1.2) < 0.0002);
}

// ---- A sound's file off the frame (AudioFileLoader) ----------------------------------------------

// A disk that holds every read until the test lets it go, so "the asker did not wait" is no race.
static std::mutex g_diskLock;
static std::condition_variable g_diskMoved;
static bool g_diskOpen = false;
static int g_diskReads = 0;
static std::vector<unsigned char> g_diskImage;

static char *readFromHeldDisk(const char *name, unsigned *size)
{
	std::unique_lock<std::mutex> lock(g_diskLock);
	++g_diskReads;
	g_diskMoved.wait(lock, [] { return g_diskOpen; });
	if (strcmp(name, "missing.wav") == 0) {
		return NULL;
	}
	char *buffer = new char[g_diskImage.size()];
	memcpy(buffer, &g_diskImage[0], g_diskImage.size());
	*size = (unsigned)g_diskImage.size();
	return buffer;
}

static void holdDisk(const std::vector<unsigned char> &image)
{
	std::lock_guard<std::mutex> lock(g_diskLock);
	g_diskImage = image;
	g_diskOpen = false;
	g_diskReads = 0;
}

static void openDisk()
{
	std::lock_guard<std::mutex> lock(g_diskLock);
	g_diskOpen = true;
	g_diskMoved.notify_all();
}

static int diskReads()
{
	std::lock_guard<std::mutex> lock(g_diskLock);
	return g_diskReads;
}

// A WAV image of IMA ADPCM, the shape of the game's voices: runs of one nibble up and one down, a
// triangle wave and so a sound, with no encoder here to get wrong.
static std::vector<unsigned char> makeAdpcm(unsigned blocks)
{
	const unsigned BLOCK = 256;
	const unsigned dataBytes = blocks * BLOCK;
	std::vector<unsigned char> image(60 + dataBytes);
	memcpy(&image[0], "RIFF", 4);
	put32(&image[4], (unsigned)image.size() - 8);
	memcpy(&image[8], "WAVEfmt ", 8);
	put32(&image[16], 20);
	put16(&image[20], 0x11);
	put16(&image[22], 1);
	put32(&image[24], TONE_RATE);
	put32(&image[28], TONE_RATE / 2);
	put16(&image[32], BLOCK);
	put16(&image[34], 4);
	put16(&image[36], 2);
	put16(&image[38], 1 + (BLOCK - 4) * 2);
	memcpy(&image[40], "fact", 4);
	put32(&image[44], 4);
	put32(&image[48], blocks * (1 + (BLOCK - 4) * 2));
	memcpy(&image[52], "data", 4);
	put32(&image[56], dataBytes);
	for (unsigned i = 0; i < dataBytes; ++i) {
		image[60 + i] = (i % BLOCK) < 4 ? 0 : ((i % 80) < 40 ? 0x44 : 0xCC);
	}
	return image;
}

TEST(a_file_on_a_stuck_disk_leaves_the_asker_free_and_is_read_once)
{
	startAudio();
	const std::vector<unsigned char> image = makeAdpcm(40);
	holdDisk(image);
	{
		AudioFileLoader loader(readFromHeldDisk);
		AudioFileLoader::Loaded loaded;

		// every call comes straight back while the read sits on the disk: one held under the lock
		// would hang here
		CHECK(!loader.ready("voice.wav"));
		CHECK(!loader.ready("voice.wav"));
		CHECK(!loader.take("voice.wav", &loaded));
		CHECK(!loader.take("never asked.wav", &loaded));
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		CHECK(loader.waitedMs("voice.wav") >= 10.0f);
		CHECK_EQ(loader.waitedMs("never asked.wav"), 0.0f);

		openDisk();
		loader.waitUntilIdle();
		CHECK(loader.ready("voice.wav"));
		CHECK_EQ(loader.waitedMs("voice.wav"), 0.0f);
		CHECK_EQ(diskReads(), 1);
		CHECK(loader.take("voice.wav", &loaded));
		CHECK(!loader.take("voice.wav", &loaded));	// handed over once

		// what comes back is what the mixer's own decompress makes of the file
		AILSOUNDINFO info;
		AIL_WAV_info(&image[0], &info);
		void *direct = NULL;
		U32 directSize = 0;
		CHECK(AIL_decompress_ADPCM(&info, &direct, &directSize) != 0);
		CHECK(loaded.compressed);
		CHECK_EQ(loaded.size, (unsigned)directSize);
		CHECK_EQ(memcmp(loaded.file, direct, directSize), 0);
		CHECK_EQ((int)loaded.info.format, 0x11);
		AIL_mem_free_lock(direct);

		// and it plays
		const std::vector<unsigned char> decoded((unsigned char *)loaded.file, (unsigned char *)loaded.file + loaded.size);
		std::vector<H3DSAMPLE> sound(1, place(decoded, -20.0f, 0.0f, 1.0f));
		const Capture capture = listen(sound, 0.4);
		CHECK(rms(capture, 0.0, 0.3) > 0.01);
		AudioFileLoader::release(&loaded);
		CHECK(loaded.file == NULL);

		const AudioFileLoader::Stats stats = loader.stats();
		CHECK_EQ(stats.files, 1u);
	}
}

TEST(an_urgent_file_goes_past_the_queue_but_not_the_one_on_the_disk)
{
	static std::vector<std::string> order;
	order.clear();
	struct Recording
	{
		static char *read(const char *name, unsigned *size)
		{
			char *buffer = readFromHeldDisk(name, size);
			std::lock_guard<std::mutex> lock(g_diskLock);
			order.push_back(name);
			return buffer;
		}
	};
	holdDisk(makeTone(440.0, 50));
	AudioFileLoader loader(Recording::read);
	loader.ready("in hand.wav");
	while (diskReads() == 0) {
		std::this_thread::yield();
	}
	loader.ready("shot.wav");
	loader.ready("reply.wav", true);
	openDisk();
	loader.waitUntilIdle();
	CHECK_EQ((int)order.size(), 3);
	CHECK_STR(order[0].c_str(), "in hand.wav");
	CHECK_STR(order[1].c_str(), "reply.wav");
	CHECK_STR(order[2].c_str(), "shot.wav");
	const char *names[3] = { "in hand.wav", "reply.wav", "shot.wav" };
	for (int i = 0; i < 3; ++i) {
		AudioFileLoader::Loaded loaded;
		CHECK(loader.take(names[i], &loaded));
		AudioFileLoader::release(&loaded);
	}
}

TEST(a_plain_pcm_file_comes_through_untouched)
{
	const std::vector<unsigned char> image = makeTone(440.0, 100);
	holdDisk(image);
	openDisk();
	AudioFileLoader loader(readFromHeldDisk);
	loader.ready("shot.wav");
	loader.waitUntilIdle();
	AudioFileLoader::Loaded loaded;
	CHECK(loader.take("shot.wav", &loaded));
	CHECK(!loaded.compressed);
	CHECK_EQ(loaded.size, (unsigned)image.size());
	CHECK_EQ(memcmp(loaded.file, &image[0], image.size()), 0);
	AudioFileLoader::release(&loaded);
}

TEST(a_missing_file_comes_back_empty_and_the_next_ask_looks_again)
{
	holdDisk(std::vector<unsigned char>(1, 0));
	openDisk();
	AudioFileLoader loader(readFromHeldDisk);
	AudioFileLoader::Loaded loaded;
	loader.ready("missing.wav");
	loader.waitUntilIdle();
	CHECK(loader.take("missing.wav", &loaded));
	CHECK(loaded.file == NULL);
	CHECK(!loader.ready("missing.wav"));
	loader.waitUntilIdle();
	CHECK_EQ(diskReads(), 2);
}

TEST(closing_waits_for_the_file_in_hand_and_reads_no_more)
{
	const std::vector<unsigned char> image = makeTone(440.0, 50);
	holdDisk(image);
	{
		AudioFileLoader loader(readFromHeldDisk);
		loader.ready("first.wav");
		loader.ready("second.wav");
		loader.ready("third.wav");
		// the thread is on the disk with the first before the disk opens, and the close starts behind it
		while (diskReads() == 0) {
			std::this_thread::yield();
		}
		std::thread opener([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			openDisk();
		});
		opener.detach();
	}
	CHECK_EQ(diskReads(), 1);

	// a finished file nobody took goes with the loader
	holdDisk(image);
	openDisk();
	{
		AudioFileLoader loader(readFromHeldDisk);
		loader.ready("not taken.wav");
		loader.waitUntilIdle();
	}
	CHECK_EQ(diskReads(), 1);
}

TEST(headphones_hear_a_sound_on_the_right_later_quieter_and_duller_in_the_left_ear)
{
	// MSS.h's ears alone, both backends' code.  A click straight to the right of a listener facing +y
	// (right is up x face, -x here, as the test above places its tones): Woodworth's 0.66 ms is 31.6
	// frames at 48 kHz, and the far ear's shadow both lowers and spreads what arrives.
	const float RATE = 48000.0f;
	const int FRAMES = 96;
	float side = 0.0f;
	float front = 0.0f;
	AIL_ex_3D_bearing(-10.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, &side, &front);
	CHECK_NEAR(side, 1.0f, 0.0001f);
	CHECK_NEAR(front, 0.0f, 0.0001f);

	AIL_EX_HRTF_TARGET ears;
	AIL_ex_hrtf_target(side, front, RATE, &ears);
	CHECK_NEAR(ears.delay[0], AIL_EX_HRTF_HEAD_SECONDS * (TONE_PI / 2.0 + 1.0) * RATE, 0.01);
	CHECK_EQ(ears.delay[1], 0.0f);
	CHECK(ears.gain[0] < ears.gain[1]);
	CHECK(ears.coefficient[0] < ears.coefficient[1]);

	float click[FRAMES] = { 1.0f };
	float heard[FRAMES * 2];
	AIL_EX_HRTF head;
	AIL_ex_hrtf_reset(&head);
	AIL_ex_hrtf_process(&head, &ears, click, heard, FRAMES);

	int loudest[2] = { 0, 0 };
	float leftSum = 0.0f;
	float rightSum = 0.0f;
	for (int frame = 0; frame < FRAMES; ++frame) {
		for (int ear = 0; ear < 2; ++ear) {
			if (fabsf(heard[frame * 2 + ear]) > fabsf(heard[loudest[ear] * 2 + ear])) {
				loudest[ear] = frame;
			}
		}
		leftSum += heard[frame * 2];
		rightSum += heard[frame * 2 + 1];
	}
	CHECK(loudest[1] <= 1);
	CHECK(loudest[0] >= 31);
	CHECK(fabsf(heard[30 * 2]) < 1e-6f);	// nothing reaches the left ear before the delay
	CHECK(leftSum < rightSum);

	// straight ahead the two ears are one: no delay, the stereo pan's centre gain
	AIL_ex_hrtf_target(0.0f, 1.0f, RATE, &ears);
	CHECK_EQ(ears.delay[0], ears.delay[1]);
	CHECK_NEAR(ears.gain[0], AIL_EX_HRTF_CENTRE, 0.0001f);
	CHECK_NEAR(ears.gain[1], AIL_EX_HRTF_CENTRE, 0.0001f);
}
