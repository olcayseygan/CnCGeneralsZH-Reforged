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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/Miles6/xaudio2/miles_xaudio2.cpp by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.
/*
 * Miles Sound System 6.5 surface, implemented on miniaudio.
 *
 * The sibling of Miles6/xaudio2/miles_xaudio2.cpp for every platform that is not Windows (decision 3
 * in PORTING.md), and written against it function by function: MilesAudioManager.cpp
 * compiles unchanged against either, and should sound the same through either.  Where the two
 * libraries differ, this file matches what miles_xaudio2 *produces*, not what miniaudio would do by
 * default, and says so at the site.
 *
 * What maps onto what:
 *   HSAMPLE / H3DSAMPLE  -> a voice that plays the caller's PCM WAV image in place, through its own
 *                           resampler, into the mix.  The image is the game's; nothing here copies
 *                           or frees it, exactly as XAudio2 was handed the pointer.
 *   HSTREAM / HAUDIO     -> a file decoded by miniaudio through the game's own file callbacks, into
 *                           ~200ms chunks the service thread keeps four ahead of the mixer.
 *   3D positioning       -> the same distance attenuation and stereo pan miles_xaudio2 computes,
 *                           not miniaudio's spatializer.
 *   the mix              -> done here, in miniaudio's device callback, so that the output matrix is
 *                           exactly XAudio2's: every source channel summed into left and right at
 *                           the pan's two gains.
 *   EOS callbacks        -> the service thread notices a voice run dry and calls back from there,
 *                           outside every lock - the thread context miles_xaudio2 uses.
 *
 * Threads and locks.  The service thread and every API call take the engine lock (AIL_lock's,
 * recursive like the CRITICAL_SECTION it replaces).  The device callback takes only mixLock, held
 * for one mix pass; anything the mix reads is changed under it.  The order is always engine lock
 * then mixLock, and the callback never takes the engine lock, so a game holding AIL_lock cannot
 * stall the audio thread.
 *
 * miniaudio's null backend (ZH_AUDIO_BACKEND=null) is what the tests run on: a device with no
 * hardware behind it that still calls the mix in real time.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "zhio.h"	// the capture's path is the engine's spelling (C4)

#include "miniaudio.h"

#include "MSS/MSS.h"
#include "MSS/mss_ex_pcm.h"

#ifndef WAVE_FORMAT_IMA_ADPCM
#define WAVE_FORMAT_IMA_ADPCM 0x0011
#endif

namespace {

// The same numbers as miles_xaudio2.cpp, and for the same reasons.
const int SERVICE_PERIOD_MS = 10;
const int STREAM_CHUNK_MS = 200;
const int STREAM_BUFFERS_AHEAD = 4;
const int USER_DATA_SLOTS = 8;
const float DEFAULT_PAN = 0.5f;
// XAudio2's range for a source voice's frequency ratio: XAUDIO2_MIN_FREQ_RATIO below, and the 4.0
// miles_xaudio2 creates sample voices with above.
const float MIN_FREQ_RATIO = 1.0f / 1024.0f;
const float MAX_FREQ_RATIO = 4.0f;

// Frames the mix moves through a voice's resampler at a time.
const unsigned MIX_CHUNK_FRAMES = 512;
const unsigned OUTPUT_CHANNELS = 2;

// WAVEFORMATEX's fields, without windows.h.
struct WaveFormat
{
	unsigned short tag;
	unsigned short channels;
	unsigned int samplesPerSecond;
	unsigned int averageBytesPerSecond;
	unsigned short blockAlign;
	unsigned short bitsPerSample;
};

struct WaveImage
{
	WaveFormat format;
	const unsigned char *data;
	unsigned int dataBytes;
	unsigned int samplesPerBlock;
};

// ---------------------------------------------------------------------------------------------
// RIFF/WAVE - miles_xaudio2.cpp's reader, unchanged but for the struct
// ---------------------------------------------------------------------------------------------

unsigned int readU32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

unsigned short readU16(const unsigned char *p)
{
	return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

void writeU32(unsigned char *p, unsigned int value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
	p[2] = (unsigned char)((value >> 16) & 0xff);
	p[3] = (unsigned char)((value >> 24) & 0xff);
}

void writeU16(unsigned char *p, unsigned short value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
}

bool parseWave(const void *image, WaveImage *out)
{
	const unsigned char *bytes = (const unsigned char *)image;
	if (bytes == NULL) {
		return false;
	}
	if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) {
		return false;
	}

	const unsigned int riffBytes = readU32(bytes + 4);
	const unsigned char *cursor = bytes + 12;
	const unsigned char *end = bytes + 8 + riffBytes;
	bool haveFormat = false;

	memset(out, 0, sizeof(*out));
	while (cursor + 8 <= end) {
		const unsigned int chunkBytes = readU32(cursor + 4);
		const unsigned char *body = cursor + 8;

		if (memcmp(cursor, "fmt ", 4) == 0 && chunkBytes >= 16) {
			out->format.tag = readU16(body);
			out->format.channels = readU16(body + 2);
			out->format.samplesPerSecond = readU32(body + 4);
			out->format.averageBytesPerSecond = readU32(body + 8);
			out->format.blockAlign = readU16(body + 12);
			out->format.bitsPerSample = readU16(body + 14);
			if (chunkBytes >= 20) {
				out->samplesPerBlock = readU16(body + 18);
			}
			haveFormat = true;
		} else if (memcmp(cursor, "data", 4) == 0) {
			out->data = body;
			out->dataBytes = chunkBytes;
		}

		cursor = body + chunkBytes + (chunkBytes & 1);
	}

	return haveFormat && out->data != NULL;
}

unsigned int wavePcmSampleCount(const WaveImage &wave)
{
	if (wave.format.tag == WAVE_FORMAT_IMA_ADPCM) {
		if (wave.format.blockAlign == 0 || wave.format.channels == 0) {
			return 0;
		}
		const unsigned int blocks = wave.dataBytes / wave.format.blockAlign;
		const unsigned int perBlock = wave.samplesPerBlock != 0
			? wave.samplesPerBlock
			: 1 + (wave.format.blockAlign - 4 * wave.format.channels) * 2 / wave.format.channels;
		return blocks * perBlock;
	}

	const unsigned int frameBytes = wave.format.blockAlign != 0 ? wave.format.blockAlign : 1;
	return wave.dataBytes / frameBytes;
}

// ---------------------------------------------------------------------------------------------
// IMA ADPCM - miles_xaudio2.cpp's decoder, unchanged, so a sound effect decodes to the same samples
// ---------------------------------------------------------------------------------------------

const int ADPCM_INDEX_STEP[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

const int ADPCM_STEP_TABLE[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
	279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
	1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
	4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
	16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

short decodeAdpcmNibble(int nibble, int *predictor, int *index)
{
	const int step = ADPCM_STEP_TABLE[*index];
	int difference = step >> 3;

	if (nibble & 4) difference += step;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 1) difference += step >> 2;
	if (nibble & 8) difference = -difference;

	int sample = *predictor + difference;
	if (sample > 32767) sample = 32767;
	if (sample < -32768) sample = -32768;
	*predictor = sample;

	*index += ADPCM_INDEX_STEP[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;

	return (short)sample;
}

// Decodes every block into interleaved 16-bit PCM.  Returns the sample frame count.
unsigned int decodeAdpcm(const WaveImage &wave, std::vector<short> *out)
{
	const unsigned int channels = wave.format.channels;
	const unsigned int blockAlign = wave.format.blockAlign;
	if (channels == 0 || channels > 2 || blockAlign <= 4 * channels) {
		return 0;
	}

	const unsigned int blocks = wave.dataBytes / blockAlign;
	const unsigned int samplesPerBlock = 1 + (blockAlign - 4 * channels) * 2 / channels;
	out->clear();
	out->reserve((size_t)blocks * samplesPerBlock * channels);

	for (unsigned int block = 0; block < blocks; ++block) {
		const unsigned char *body = wave.data + (size_t)block * blockAlign;
		int predictor[2] = { 0, 0 };
		int index[2] = { 0, 0 };

		for (unsigned int channel = 0; channel < channels; ++channel) {
			predictor[channel] = (short)readU16(body + channel * 4);
			index[channel] = body[channel * 4 + 2];
			if (index[channel] > 88) index[channel] = 88;
			out->push_back((short)predictor[channel]);
		}

		const unsigned char *nibbles = body + 4 * channels;
		const unsigned int groupsPerChannel = (blockAlign - 4 * channels) / (4 * channels);
		std::vector<short> decoded[2];
		for (unsigned int channel = 0; channel < channels; ++channel) {
			decoded[channel].reserve(groupsPerChannel * 8);
		}

		for (unsigned int group = 0; group < groupsPerChannel; ++group) {
			for (unsigned int channel = 0; channel < channels; ++channel) {
				const unsigned char *word = nibbles + (group * channels + channel) * 4;
				for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
					decoded[channel].push_back(decodeAdpcmNibble(word[byteIndex] & 0x0f, &predictor[channel], &index[channel]));
					decoded[channel].push_back(decodeAdpcmNibble((word[byteIndex] >> 4) & 0x0f, &predictor[channel], &index[channel]));
				}
			}
		}

		const size_t frames = decoded[0].size();
		for (size_t frame = 0; frame < frames; ++frame) {
			for (unsigned int channel = 0; channel < channels; ++channel) {
				out->push_back(decoded[channel][frame]);
			}
		}
	}

	return channels != 0 ? (unsigned int)(out->size() / channels) : 0;
}

// ---------------------------------------------------------------------------------------------
// The mix path one voice takes: its source frames, converted to float, through its own resampler,
// summed across channels and added to the output at two gains.  This is the whole of what an
// XAudio2 source voice with an output matrix did.
// ---------------------------------------------------------------------------------------------

struct VoiceMix
{
	VoiceMix() : resamplerReady(false), channels(0), gainLeft(0.0f), gainRight(0.0f), passThrough(false), pending(0), pendingAt(0) {}

	ma_resampler resampler;
	bool resamplerReady;
	unsigned channels;
	float gainLeft;
	float gainRight;
	// An XAudio2 voice with the default matrix rather than a panned Miles one: left to left and right
	// to right (a mono source to both), each at its gain.  The movies' voice (mss_ex_pcm.h).
	bool passThrough;
	// Frames already converted to float and not yet taken by the resampler, kept across mix passes
	// so that nothing is dropped between them.
	float input[MIX_CHUNK_FRAMES * 2];
	unsigned pending;
	unsigned pendingAt;
};

bool prepareResampler(VoiceMix *mix, unsigned channels, unsigned sourceRate, unsigned deviceRate)
{
	if (mix->resamplerReady) {
		ma_resampler_uninit(&mix->resampler, NULL);
		mix->resamplerReady = false;
	}
	mix->channels = channels;
	mix->pending = 0;
	mix->pendingAt = 0;
	if (channels == 0 || sourceRate == 0 || deviceRate == 0) {
		return false;
	}
	// Linear with miniaudio's default low-pass, where XAudio2 has its own higher-order converter:
	// the one place the output is knowingly not the same samples (C4, choice 1).
	ma_resampler_config config = ma_resampler_config_init(ma_format_f32, channels, sourceRate, deviceRate,
		ma_resample_algorithm_linear);
	mix->resamplerReady = ma_resampler_init(&config, NULL, &mix->resampler) == MA_SUCCESS;
	return mix->resamplerReady;
}

void setResampleRate(VoiceMix *mix, float sourceFramesPerSecond, unsigned deviceRate)
{
	if (mix->resamplerReady && deviceRate != 0 && sourceFramesPerSecond > 0.0f) {
		ma_resampler_set_rate_ratio(&mix->resampler, sourceFramesPerSecond / (float)deviceRate);
	}
}

void resetResampler(VoiceMix *mix)
{
	if (mix->resamplerReady) {
		ma_resampler_reset(&mix->resampler);
	}
	mix->pending = 0;
	mix->pendingAt = 0;
}

// Fills up to maxFrames of float source frames.  Returns how many it gave; 0 means the source is dry.
typedef unsigned (*SourceRead)(void *source, float *out, unsigned maxFrames);

// Mixes up to `frames` output frames of one voice into `output`.  Returns true once the source is
// dry and nothing is left in flight, which is XAudio2's BuffersQueued == 0.
bool mixVoice(VoiceMix *mix, SourceRead read, void *source, float *output, unsigned frames, bool *sourceDry)
{
	float resampled[MIX_CHUNK_FRAMES * 2];
	unsigned done = 0;

	while (done < frames) {
		if (mix->pending == 0 && !*sourceDry) {
			mix->pendingAt = 0;
			mix->pending = read(source, mix->input, MIX_CHUNK_FRAMES);
			if (mix->pending == 0) {
				*sourceDry = true;
			}
		}
		if (mix->pending == 0) {
			break;
		}

		ma_uint64 inFrames = mix->pending;
		ma_uint64 outFrames = frames - done < MIX_CHUNK_FRAMES ? frames - done : MIX_CHUNK_FRAMES;
		ma_resampler_process_pcm_frames(&mix->resampler, mix->input + (size_t)mix->pendingAt * mix->channels,
			&inFrames, resampled, &outFrames);

		for (ma_uint64 frame = 0; mix->passThrough && frame < outFrames; ++frame) {
			const float left = resampled[frame * mix->channels];
			const float right = mix->channels >= 2 ? resampled[frame * mix->channels + 1] : left;
			output[(done + frame) * OUTPUT_CHANNELS + 0] += mix->gainLeft * left;
			output[(done + frame) * OUTPUT_CHANNELS + 1] += mix->gainRight * right;
		}
		for (ma_uint64 frame = 0; !mix->passThrough && frame < outFrames; ++frame) {
			// XAudio2's matrix row for each output speaker has every source channel at the same gain.
			float sum = 0.0f;
			for (unsigned channel = 0; channel < mix->channels; ++channel) {
				sum += resampled[frame * mix->channels + channel];
			}
			output[(done + frame) * OUTPUT_CHANNELS + 0] += mix->gainLeft * sum;
			output[(done + frame) * OUTPUT_CHANNELS + 1] += mix->gainRight * sum;
		}

		mix->pendingAt += (unsigned)inFrames;
		mix->pending -= (unsigned)inFrames;
		done += (unsigned)outFrames;
		if (inFrames == 0 && outFrames == 0) {
			break;
		}
	}

	return *sourceDry && mix->pending == 0;
}

// ---------------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------------

struct Sample
{
	bool is3D;
	WaveFormat format;
	const unsigned char *data;
	unsigned int dataBytes;
	float volume;
	float pan;
	unsigned int baseRate;
	unsigned int rate;
	S32 userData[USER_DATA_SLOTS];
	AILSAMPLECB endOfSample;
	AIL3DSAMPLECB endOf3DSample;
	bool playing;
	float positionX, positionY, positionZ;
	float minDistance, maxDistance;
	float occlusion;

	// What the mix sees, under mixLock.  `running` is the voice started, `queued` its one buffer
	// still in it, `cursor` the next source frame.
	bool running;
	bool queued;
	unsigned int cursor;
	bool sourceDry;
	VoiceMix mix;
};

struct Stream
{
	Stream()
		: decoderReady(false), channels(0), rate(0), queuedAt(0),
		  running(false), sourceDry(false), loopCount(1), paused(false), playing(false), exhausted(false),
		  volume(1.0f), pan(DEFAULT_PAN), totalMs(-1.0), framesPlayed(0), callback(NULL), hasVoice(false)
	{
	}

	ma_decoder decoder;
	bool decoderReady;
	unsigned channels;
	unsigned rate;

	// Decoded 16-bit chunks the mix has not finished.  The front is the one playing; queuedAt is how
	// far into it the mix is.  Under mixLock.
	std::deque<std::vector<short> > queued;
	size_t queuedAt;
	bool running;
	bool sourceDry;

	int loopCount;
	bool paused;
	bool playing;
	bool exhausted;
	float volume;
	float pan;
	double totalMs;
	long long framesPlayed;
	AILSTREAMCB callback;
	bool hasVoice;
	VoiceMix mix;
};

// A movie's voice (mss_ex_pcm.h): PCM its owner pushes, mixed until the queue runs dry, and again
// whenever more arrives.
struct PcmVoice
{
	PcmVoice() : channels(0), rate(0), queuedAt(0), queuedFrames(0), sourceDry(false) {}

	unsigned channels;
	unsigned rate;
	// Under mixLock: the chunks not yet mixed, how far into the front one the mix is, and how many
	// frames that leaves.
	std::deque<std::vector<short> > queued;
	size_t queuedAt;
	long long queuedFrames;
	bool sourceDry;
	VoiceMix mix;
};

struct Listener
{
	float positionX, positionY, positionZ;
	float faceX, faceY, faceZ;
	float upX, upY, upZ;
};

struct Capture
{
	FILE *file;
	unsigned int channels;
	unsigned int samplesPerSecond;
	unsigned int dataBytes;
	unsigned int sizesWrittenAt;
};

struct Engine
{
	Engine() : contextReady(false), deviceReady(false), serviceRunning(false), started(false), deviceRate(0),
		capture(NULL), offline(false), mixAnchored(false), mixedFrames(0), capturedFrames(0), capturedRate(0) {}

	// A process can end without AIL_shutdown - a test, or a game that exits in a hurry - and a
	// std::thread still joinable at destruction calls std::terminate.  The Windows build's service
	// thread is a HANDLE, which has no such rule, so this is a hazard only this backend has.
	~Engine()
	{
		if (serviceThread.joinable()) {
			serviceRunning.store(false);
			serviceThread.join();
		}
		if (deviceReady) {
			ma_device_uninit(&device);
			deviceReady = false;
		}
		if (contextReady) {
			ma_context_uninit(&context);
			contextReady = false;
		}
	}

	ma_context context;
	bool contextReady;
	ma_device device;
	bool deviceReady;
	std::recursive_mutex lock;
	std::mutex mixLock;
	std::thread serviceThread;
	std::atomic<bool> serviceRunning;
	std::vector<Sample *> samples;
	std::vector<Stream *> streams;
	std::vector<PcmVoice *> pcmVoices;
	Listener listener;
	bool started;
	unsigned deviceRate;
	Capture *capture;

	// AIL_ex_offline_mix: the device is opened on the null backend and never started, and the mix
	// moves only through AIL_ex_mix_to_frame.  mixedFrames is where the logic clock has it, in output
	// frames from logic frame zero.  Game thread only.
	bool offline;
	bool mixAnchored;
	long long mixedFrames;
	// What the last finished capture wrote, for AIL_ex_capture_length.
	unsigned capturedFrames;
	unsigned capturedRate;
};

Engine g_engine;
std::atomic<S32> g_linearFalloff(0);	// AIL_ex_set_3D_linear_falloff

AILFILEOPENCB g_fileOpen;
AILFILECLOSECB g_fileClose;
AILFILESEEKCB g_fileSeek;
AILFILEREADCB g_fileRead;

struct _AIL_DIGDRIVER *const DIGITAL_DRIVER = (struct _AIL_DIGDRIVER *)(uintptr_t)0x1;
struct _AIL_3DPOBJECT *const LISTENER_OBJECT = (struct _AIL_3DPOBJECT *)(uintptr_t)0x2;

const HPROVIDER PROVIDER_FAST_2D = 1;
const HPROVIDER PROVIDER_DOLBY = 2;

float clampUnit(float value)
{
	if (value < 0.0f) return 0.0f;
	if (value > 1.0f) return 1.0f;
	return value;
}

// applyPan and SetVolume together, as the two gains the mix uses.  Miles' pan is a constant-power
// law: dead centre is not two half-volume speakers.
void setGains(VoiceMix *mix, float volume, float pan)
{
	const float voiceVolume = clampUnit(volume);
	mix->gainLeft = voiceVolume * (float)sqrt(clampUnit(1.0f - pan));
	mix->gainRight = voiceVolume * (float)sqrt(clampUnit(pan));
}

void computeSpatialGain(const Sample &sample, float *volumeOut, float *panOut)
{
	const Listener &listener = g_engine.listener;
	const float dx = sample.positionX - listener.positionX;
	const float dy = sample.positionY - listener.positionY;
	const float dz = sample.positionZ - listener.positionZ;
	const float distance = (float)sqrt(dx * dx + dy * dy + dz * dz);

	float attenuation = AIL_ex_3D_distance_gain(distance, sample.minDistance, sample.maxDistance, g_linearFalloff);
	attenuation *= clampUnit(1.0f - sample.occlusion);

	float pan = DEFAULT_PAN;
	if (distance > 0.0001f) {
		// right = up x face, the axis a stereo pan moves along.  miles_xaudio2.cpp says why.
		const float rightX = listener.upY * listener.faceZ - listener.upZ * listener.faceY;
		const float rightY = listener.upZ * listener.faceX - listener.upX * listener.faceZ;
		const float rightZ = listener.upX * listener.faceY - listener.upY * listener.faceX;
		const float rightLength = (float)sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
		if (rightLength > 0.0001f) {
			const float projection = (dx * rightX + dy * rightY + dz * rightZ) / (rightLength * distance);
			pan = clampUnit(0.5f + 0.5f * projection);
		}
	}

	*volumeOut = clampUnit(sample.volume) * attenuation;
	*panOut = pan;
}

// Called with the engine lock held; takes mixLock for what the mix reads.
void refreshSampleOutput(Sample *sample)
{
	if (sample->mix.channels == 0) {
		return;
	}

	float volume = sample->volume;
	float pan = sample->pan;
	if (sample->is3D) {
		computeSpatialGain(*sample, &volume, &pan);
	}

	float ratio = 1.0f;
	if (sample->baseRate != 0) {
		ratio = (float)sample->rate / (float)sample->baseRate;
		if (ratio < MIN_FREQ_RATIO) ratio = MIN_FREQ_RATIO;
		if (ratio > MAX_FREQ_RATIO) ratio = MAX_FREQ_RATIO;
	}

	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	setGains(&sample->mix, volume, pan);
	setResampleRate(&sample->mix, (float)sample->baseRate * ratio, g_engine.deviceRate);
}

// XAudio2's Stop plus FlushSourceBuffers: the voice silent and empty.  Under mixLock.
void flushSample(Sample *sample)
{
	sample->running = false;
	sample->queued = false;
	sample->cursor = 0;
	sample->sourceDry = false;
	resetResampler(&sample->mix);
}

// ensureVoice: reuse the voice if the format matches, else build one for the new format.
bool ensureVoice(Sample *sample, const WaveFormat &format)
{
	if (!g_engine.deviceReady) {
		return false;
	}

	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	if (sample->mix.channels != 0 &&
		sample->format.channels == format.channels &&
		sample->format.samplesPerSecond == format.samplesPerSecond &&
		sample->format.bitsPerSample == format.bitsPerSample) {
		flushSample(sample);
		return true;
	}

	flushSample(sample);
	sample->format = format;
	return prepareResampler(&sample->mix, format.channels, format.samplesPerSecond, g_engine.deviceRate);
}

unsigned readSampleSource(void *source, float *out, unsigned maxFrames)
{
	Sample *sample = (Sample *)source;
	const unsigned frameBytes = sample->format.blockAlign != 0 ? sample->format.blockAlign : 1;
	const unsigned totalFrames = sample->dataBytes / frameBytes;
	if (sample->data == NULL || sample->cursor >= totalFrames) {
		return 0;
	}

	const unsigned frames = totalFrames - sample->cursor < maxFrames ? totalFrames - sample->cursor : maxFrames;
	const unsigned channels = sample->format.channels;
	const unsigned char *at = sample->data + (size_t)sample->cursor * frameBytes;
	for (unsigned frame = 0; frame < frames; ++frame) {
		for (unsigned channel = 0; channel < channels; ++channel) {
			float value;
			if (sample->format.bitsPerSample == 8) {
				value = ((float)at[frame * frameBytes + channel] - 128.0f) / 128.0f;
			} else {
				value = (float)(short)readU16(at + frame * frameBytes + channel * 2) / 32768.0f;
			}
			out[frame * channels + channel] = value;
		}
	}
	sample->cursor += frames;
	return frames;
}

// ---------------------------------------------------------------------------------------------
// Streams, decoded by miniaudio through the game's file callbacks
// ---------------------------------------------------------------------------------------------

// The game's file callbacks as a miniaudio VFS - open, read, seek, tell and size - the way
// miles_xaudio2 hands them to FFmpeg as an AVIO context with AVSEEK_SIZE.  It has to be a VFS and not
// ma_decoder_init's bare read/seek pair: without tell, dr_mp3 cannot find a file's end at open, so
// it cannot see the 128-byte ID3v1 tag that 54 of the 56 music tracks end with, and it drops the
// last MPEG frame (26ms) before one.  Measured on C_Chix01.mp3: 4,591,872 frames through read/seek,
// 4,593,024 through a VFS, 4,593,024 from FFmpeg.  The decoder owns the file handle and closes it
// through vfsClose.
ma_result vfsOpen(ma_vfs *, const char *path, ma_uint32, ma_vfs_file *file)
{
	AILFILEHANDLE handle = 0;
	if (g_fileOpen == NULL || g_fileOpen(path, &handle) == 0 || handle == 0) {
		return MA_DOES_NOT_EXIST;
	}
	*file = (ma_vfs_file)handle;
	return MA_SUCCESS;
}

ma_result vfsClose(ma_vfs *, ma_vfs_file file)
{
	if (g_fileClose != NULL) {
		g_fileClose((AILFILEHANDLE)file);
	}
	return MA_SUCCESS;
}

ma_result vfsRead(ma_vfs *, ma_vfs_file file, void *buffer, size_t bytes, size_t *bytesRead)
{
	*bytesRead = 0;
	if (g_fileRead == NULL) {
		return MA_AT_END;
	}
	const U32 read = g_fileRead((AILFILEHANDLE)file, buffer, (U32)bytes);
	*bytesRead = (size_t)read;
	return read == 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result vfsSeek(ma_vfs *, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
	if (g_fileSeek == NULL) {
		return MA_ERROR;
	}
	const U32 whence = origin == ma_seek_origin_current ? SEEK_CUR : (origin == ma_seek_origin_end ? SEEK_END : SEEK_SET);
	return g_fileSeek((AILFILEHANDLE)file, (S32)offset, whence) < 0 ? MA_ERROR : MA_SUCCESS;
}

// The game's seek answers the new position, as File::seek does, so a zero move is tell.
ma_result vfsTell(ma_vfs *, ma_vfs_file file, ma_int64 *cursor)
{
	if (g_fileSeek == NULL) {
		return MA_ERROR;
	}
	const S32 here = g_fileSeek((AILFILEHANDLE)file, 0, SEEK_CUR);
	*cursor = here;
	return here < 0 ? MA_ERROR : MA_SUCCESS;
}

ma_result vfsInfo(ma_vfs *, ma_vfs_file file, ma_file_info *info)
{
	if (g_fileSeek == NULL) {
		return MA_ERROR;
	}
	const S32 here = g_fileSeek((AILFILEHANDLE)file, 0, SEEK_CUR);
	const S32 size = g_fileSeek((AILFILEHANDLE)file, 0, SEEK_END);
	g_fileSeek((AILFILEHANDLE)file, here, SEEK_SET);
	info->sizeInBytes = size < 0 ? 0 : (ma_uint64)size;
	return size < 0 ? MA_ERROR : MA_SUCCESS;
}

ma_vfs_callbacks g_gameVfs = { vfsOpen, NULL, vfsClose, vfsRead, NULL, vfsSeek, vfsTell, vfsInfo };

void closeStreamDecoder(Stream *stream)
{
	if (stream->decoderReady) {
		ma_decoder_uninit(&stream->decoder);	// closes the file through vfsClose
		stream->decoderReady = false;
	}
}

bool initStreamDecoder(Stream *stream, const char *filename, unsigned channels)
{
	// 16-bit at the file's own rate, as FFmpeg's swresample was asked for; the mix resamples.
	ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, 0);
	stream->decoderReady = ma_decoder_init_vfs(&g_gameVfs, filename, &config, &stream->decoder) == MA_SUCCESS;
	return stream->decoderReady;
}

bool openStreamDecoder(Stream *stream, const char *filename)
{
	if (!initStreamDecoder(stream, filename, 0)) {
		return false;
	}
	// At most two channels, as miles_xaudio2 asks swresample for: a file with more is folded to
	// stereo, and one with one stays mono.
	if (stream->decoder.outputChannels > 2) {
		closeStreamDecoder(stream);
		if (!initStreamDecoder(stream, filename, 2)) {
			return false;
		}
	}
	stream->channels = stream->decoder.outputChannels;
	stream->rate = stream->decoder.outputSampleRate;

	if (!g_engine.deviceReady) {
		return false;
	}
	stream->hasVoice = prepareResampler(&stream->mix, stream->channels, stream->rate, g_engine.deviceRate);
	return stream->hasVoice;
}

// Decodes up to one chunk.  Returns false at the end of the file with nothing more to give.
bool decodeStreamChunk(Stream *stream, std::vector<short> *out)
{
	const unsigned wanted = stream->rate * STREAM_CHUNK_MS / 1000;
	out->resize((size_t)wanted * stream->channels);
	ma_uint64 read = 0;
	ma_decoder_read_pcm_frames(&stream->decoder, &(*out)[0], wanted, &read);
	out->resize((size_t)read * stream->channels);
	return read != 0;
}

bool rewindStream(Stream *stream)
{
	return ma_decoder_seek_to_pcm_frame(&stream->decoder, 0) == MA_SUCCESS;
}

unsigned readStreamSource(void *source, float *out, unsigned maxFrames)
{
	Stream *stream = (Stream *)source;
	unsigned given = 0;
	while (given < maxFrames && !stream->queued.empty()) {
		const std::vector<short> &chunk = stream->queued.front();
		const size_t chunkFrames = chunk.size() / stream->channels;
		const size_t take = chunkFrames - stream->queuedAt < maxFrames - given ? chunkFrames - stream->queuedAt : maxFrames - given;
		for (size_t frame = 0; frame < take; ++frame) {
			for (unsigned channel = 0; channel < stream->channels; ++channel) {
				out[(given + frame) * stream->channels + channel] =
					(float)chunk[(stream->queuedAt + frame) * stream->channels + channel] / 32768.0f;
			}
		}
		given += (unsigned)take;
		stream->queuedAt += take;
		if (stream->queuedAt >= chunkFrames) {
			stream->queued.pop_front();
			stream->queuedAt = 0;
		}
	}
	// SamplesPlayed counts source frames and is not reset by a loop.
	stream->framesPlayed += given;
	return given;
}

// Under mixLock.
unsigned readPcmSource(void *source, float *out, unsigned maxFrames)
{
	PcmVoice *voice = (PcmVoice *)source;
	unsigned given = 0;
	while (given < maxFrames && !voice->queued.empty()) {
		const std::vector<short> &chunk = voice->queued.front();
		const size_t chunkFrames = chunk.size() / voice->channels;
		const size_t take = chunkFrames - voice->queuedAt < maxFrames - given ? chunkFrames - voice->queuedAt : maxFrames - given;
		for (size_t index = 0; index < take * voice->channels; ++index) {
			out[(size_t)given * voice->channels + index] = (float)chunk[voice->queuedAt * voice->channels + index] / 32768.0f;
		}
		given += (unsigned)take;
		voice->queuedAt += take;
		if (voice->queuedAt >= chunkFrames) {
			voice->queued.pop_front();
			voice->queuedAt = 0;
		}
	}
	voice->queuedFrames -= given;
	return given;
}

// Called with the engine lock held.
void serviceStream(Stream *stream, std::vector<AILSTREAMCB> *callbacks, std::vector<HSTREAM> *callbackHandles)
{
	if (!stream->hasVoice || !stream->playing) {
		return;
	}

	size_t buffersQueued;
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		buffersQueued = stream->queued.size();
	}

	// Decoding reads the game's files, so it happens here under the engine lock, never in the mix.
	while (!stream->exhausted && buffersQueued < (size_t)STREAM_BUFFERS_AHEAD) {
		std::vector<short> chunk;
		if (!decodeStreamChunk(stream, &chunk) || chunk.empty()) {
			if (stream->loopCount > 1 && rewindStream(stream)) {
				stream->loopCount -= 1;
				continue;
			}
			stream->exhausted = true;
			break;
		}

		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		stream->queued.push_back(std::vector<short>());
		stream->queued.back().swap(chunk);
		stream->sourceDry = false;
		buffersQueued = stream->queued.size();
	}

	bool drained;
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		drained = stream->queued.empty() && stream->mix.pending == 0;
	}
	if (stream->exhausted && drained) {
		stream->playing = false;
		if (stream->callback != NULL) {
			callbacks->push_back(stream->callback);
			callbackHandles->push_back((HSTREAM)stream);
		}
	}
}

void serviceThreadMain()
{
	while (g_engine.serviceRunning.load()) {
		std::vector<AILSAMPLECB> sampleCallbacks;
		std::vector<HSAMPLE> sampleHandles;
		std::vector<AIL3DSAMPLECB> sample3DCallbacks;
		std::vector<H3DSAMPLE> sample3DHandles;
		std::vector<AILSTREAMCB> streamCallbacks;
		std::vector<HSTREAM> streamHandles;

		{
			std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
			for (size_t i = 0; i < g_engine.samples.size(); ++i) {
				Sample *sample = g_engine.samples[i];
				if (sample->mix.channels == 0) {
					continue;
				}
				if (sample->is3D) {
					refreshSampleOutput(sample);
				}
				if (!sample->playing) {
					continue;
				}

				bool queued;
				{
					std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
					queued = sample->queued;
				}
				if (!queued) {
					sample->playing = false;
					if (sample->is3D && sample->endOf3DSample != NULL) {
						sample3DCallbacks.push_back(sample->endOf3DSample);
						sample3DHandles.push_back((H3DSAMPLE)sample);
					} else if (!sample->is3D && sample->endOfSample != NULL) {
						sampleCallbacks.push_back(sample->endOfSample);
						sampleHandles.push_back((HSAMPLE)sample);
					}
				}
			}

			for (size_t i = 0; i < g_engine.streams.size(); ++i) {
				serviceStream(g_engine.streams[i], &streamCallbacks, &streamHandles);
			}
		}

		// Callbacks run outside the lock: the game takes its own locks in them.
		for (size_t i = 0; i < sampleCallbacks.size(); ++i) {
			sampleCallbacks[i](sampleHandles[i]);
		}
		for (size_t i = 0; i < sample3DCallbacks.size(); ++i) {
			sample3DCallbacks[i](sample3DHandles[i]);
		}
		for (size_t i = 0; i < streamCallbacks.size(); ++i) {
			streamCallbacks[i](streamHandles[i]);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(SERVICE_PERIOD_MS));
	}
}

// ---------------------------------------------------------------------------------------------
// Capture: the finished mix, copied to a 16-bit WAV as it goes out, the way miles_xaudio2's tap on
// the mastering voice does.  Written from the audio thread on purpose, as there: a write that
// blocks makes the live output stutter, and nobody is listening to a capture run.
// ---------------------------------------------------------------------------------------------

const int WAVE_HEADER_BYTES = 44;
const int WAVE_RIFF_SIZE_OFFSET = 4;
const int WAVE_DATA_SIZE_OFFSET = 40;
const int CAPTURE_BITS_PER_SAMPLE = 16;
const float CAPTURE_SAMPLE_SCALE = 32767.0f;

void writeWaveHeader(FILE *file, unsigned int channels, unsigned int samplesPerSecond, unsigned int dataBytes)
{
	unsigned char header[WAVE_HEADER_BYTES];
	const unsigned int blockAlign = channels * (CAPTURE_BITS_PER_SAMPLE / 8);
	memcpy(header, "RIFF", 4);
	writeU32(header + 4, WAVE_HEADER_BYTES - 8 + dataBytes);
	memcpy(header + 8, "WAVEfmt ", 8);
	writeU32(header + 16, 16);
	writeU16(header + 20, WAVE_FORMAT_PCM);
	writeU16(header + 22, (unsigned short)channels);
	writeU32(header + 24, samplesPerSecond);
	writeU32(header + 28, samplesPerSecond * blockAlign);
	writeU16(header + 32, (unsigned short)blockAlign);
	writeU16(header + 34, CAPTURE_BITS_PER_SAMPLE);
	memcpy(header + 36, "data", 4);
	writeU32(header + 40, dataBytes);
	fwrite(header, 1, sizeof(header), file);
}

void writeCaptureSizes(Capture *capture)
{
	unsigned char value[4];
	fseek(capture->file, WAVE_RIFF_SIZE_OFFSET, SEEK_SET);
	writeU32(value, WAVE_HEADER_BYTES - 8 + capture->dataBytes);
	fwrite(value, 1, 4, capture->file);
	fseek(capture->file, WAVE_DATA_SIZE_OFFSET, SEEK_SET);
	writeU32(value, capture->dataBytes);
	fwrite(value, 1, 4, capture->file);
}

// Under mixLock.
void writeCapture(Capture *capture, const float *mix, unsigned frames)
{
	const unsigned samples = frames * capture->channels;
	short converted[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	for (unsigned read = 0; read < samples; ) {
		const unsigned batch = samples - read < (unsigned)(sizeof(converted) / sizeof(converted[0]))
			? samples - read : (unsigned)(sizeof(converted) / sizeof(converted[0]));
		for (unsigned index = 0; index < batch; ++index) {
			const float value = mix[read + index];
			const float clamped = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
			converted[index] = (short)(clamped * CAPTURE_SAMPLE_SCALE);
		}
		capture->dataBytes += (unsigned int)fwrite(converted, sizeof(short), batch, capture->file) * sizeof(short);
		read += batch;
	}

	// Sizes back into the header once a second, so a killed recording run leaves a usable file.
	const unsigned int refreshEvery = capture->samplesPerSecond * capture->channels * sizeof(short);
	if (capture->dataBytes - capture->sizesWrittenAt >= refreshEvery) {
		capture->sizesWrittenAt = capture->dataBytes;
		writeCaptureSizes(capture);
		fseek(capture->file, 0, SEEK_END);
	}
}

// ---------------------------------------------------------------------------------------------
// The mix
// ---------------------------------------------------------------------------------------------

void dataCallback(ma_device *, void *output, const void *, ma_uint32 frameCount)
{
	float *out = (float *)output;
	memset(out, 0, (size_t)frameCount * OUTPUT_CHANNELS * sizeof(float));

	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	for (size_t i = 0; i < g_engine.samples.size(); ++i) {
		Sample *sample = g_engine.samples[i];
		if (!sample->running || !sample->queued || !sample->mix.resamplerReady) {
			continue;
		}
		if (mixVoice(&sample->mix, readSampleSource, sample, out, frameCount, &sample->sourceDry)) {
			sample->queued = false;
		}
	}
	for (size_t i = 0; i < g_engine.streams.size(); ++i) {
		Stream *stream = g_engine.streams[i];
		// `running` alone: pausing clears it under this lock, where `paused` is the API's own field.
		if (!stream->running || !stream->mix.resamplerReady) {
			continue;
		}
		stream->sourceDry = false;
		mixVoice(&stream->mix, readStreamSource, stream, out, frameCount, &stream->sourceDry);
	}
	for (size_t i = 0; i < g_engine.pcmVoices.size(); ++i) {
		PcmVoice *voice = g_engine.pcmVoices[i];
		// Dry is only until the owner queues more, so it is asked afresh each pass, as a stream's is.
		voice->sourceDry = false;
		mixVoice(&voice->mix, readPcmSource, voice, out, frameCount, &voice->sourceDry);
	}

	if (g_engine.capture != NULL) {
		writeCapture(g_engine.capture, out, frameCount);
	}
}

// ZH_AUDIO_BACKEND=null selects miniaudio's null backend: the tests' way of running the real mix
// on a machine, or in a container, with no audio hardware.
bool useNullBackend()
{
	const char *backend = getenv("ZH_AUDIO_BACKEND");
	return g_engine.offline || (backend != NULL && strcmp(backend, "null") == 0);
}

} // namespace

// =================================================================================================
// Startup and global state
// =================================================================================================

extern "C" {

S32 AILCALL AIL_startup(void)
{
	if (g_engine.started) {
		return 1;
	}

	memset(&g_engine.listener, 0, sizeof(g_engine.listener));
	g_engine.listener.faceY = 1.0f;
	g_engine.listener.upZ = 1.0f;
	g_engine.started = true;
	return 1;
}

S32 AILCALL AIL_quick_startup(S32 use_digital, S32, U32, S32, S32)
{
	if (!use_digital) {
		return 0;
	}
	if (g_engine.deviceReady) {
		return 1;
	}

	const ma_backend nullBackend[] = { ma_backend_null };
	if (ma_context_init(useNullBackend() ? nullBackend : NULL, useNullBackend() ? 1 : 0, NULL,
			&g_engine.context) != MA_SUCCESS) {
		return 0;
	}
	g_engine.contextReady = true;

	// Float stereo at the device's own rate: what XAudio2's mastering voice mixes in.
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format = ma_format_f32;
	config.playback.channels = OUTPUT_CHANNELS;
	config.sampleRate = 0;
	config.dataCallback = dataCallback;
	if (ma_device_init(&g_engine.context, &config, &g_engine.device) != MA_SUCCESS) {
		ma_context_uninit(&g_engine.context);
		g_engine.contextReady = false;
		return 0;
	}
	g_engine.deviceRate = g_engine.device.sampleRate;
	g_engine.deviceReady = true;
	if (!g_engine.offline && ma_device_start(&g_engine.device) != MA_SUCCESS) {
		ma_device_uninit(&g_engine.device);
		ma_context_uninit(&g_engine.context);
		g_engine.deviceReady = false;
		g_engine.contextReady = false;
		return 0;
	}

	g_engine.serviceRunning.store(true);
	g_engine.serviceThread = std::thread(serviceThreadMain);
	return 1;
}

void AILCALL AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls)
{
	if (pdig != NULL) *pdig = DIGITAL_DRIVER;
	if (pmdi != NULL) *pmdi = NULL;
	if (pdls != NULL) *pdls = NULL;
}

S32 AILCALL AIL_ex_start_capture(const char *pathname)
{
	if (!g_engine.deviceReady || pathname == NULL) {
		return 0;
	}

	/* zh_fopen, not fopen: the manager names the file as the engine names paths, "<user data>Videos\\
		 <name>.wav", and a plain fopen off Windows made one file with a backslash in its name (C4). */
	FILE *file = zh_fopen(pathname, "wb");
	if (file == NULL) {
		return 0;
	}

	Capture *capture = new Capture;
	capture->file = file;
	capture->channels = OUTPUT_CHANNELS;
	capture->samplesPerSecond = g_engine.deviceRate;
	capture->dataBytes = 0;
	capture->sizesWrittenAt = 0;
	writeWaveHeader(file, capture->channels, capture->samplesPerSecond, 0);

	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	Capture *previous = g_engine.capture;
	g_engine.capture = capture;
	if (previous != NULL) {
		writeCaptureSizes(previous);
		fclose(previous->file);
		delete previous;
	}
	return 1;
}

// =================================================================================================
// The movies' voice (mss_ex_pcm.h)
// =================================================================================================

HEXPCM AILCALL AIL_ex_open_pcm(S32 rate, S32 channels)
{
	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	if (!g_engine.deviceReady || rate <= 0 || (channels != 1 && channels != 2)) {
		return NULL;
	}
	PcmVoice *voice = new PcmVoice;
	voice->channels = (unsigned)channels;
	voice->rate = (unsigned)rate;
	if (!prepareResampler(&voice->mix, voice->channels, voice->rate, g_engine.deviceRate)) {
		delete voice;
		return NULL;
	}
	voice->mix.passThrough = true;
	voice->mix.gainLeft = voice->mix.gainRight = 1.0f;
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	g_engine.pcmVoices.push_back(voice);
	return (HEXPCM)voice;
}

void AILCALL AIL_ex_close_pcm(HEXPCM handle)
{
	PcmVoice *voice = (PcmVoice *)handle;
	if (voice == NULL) {
		return;
	}
	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		for (size_t i = 0; i < g_engine.pcmVoices.size(); ++i) {
			if (g_engine.pcmVoices[i] == voice) {
				g_engine.pcmVoices.erase(g_engine.pcmVoices.begin() + (ptrdiff_t)i);
				break;
			}
		}
	}
	if (voice->mix.resamplerReady) {
		ma_resampler_uninit(&voice->mix.resampler, NULL);
	}
	delete voice;
}

S32 AILCALL AIL_ex_queue_pcm(HEXPCM handle, const S16 *data, S32 frames)
{
	PcmVoice *voice = (PcmVoice *)handle;
	if (voice == NULL || data == NULL || frames <= 0) {
		return 0;
	}
	std::vector<short> chunk(data, data + (size_t)frames * voice->channels);
	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	voice->queued.push_back(std::vector<short>());
	voice->queued.back().swap(chunk);
	voice->queuedFrames += frames;
	return frames;
}

S32 AILCALL AIL_ex_pcm_queued_frames(HEXPCM handle)
{
	PcmVoice *voice = (PcmVoice *)handle;
	if (voice == NULL) {
		return 0;
	}
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	return (S32)voice->queuedFrames;
}

void AILCALL AIL_ex_flush_pcm(HEXPCM handle)
{
	PcmVoice *voice = (PcmVoice *)handle;
	if (voice == NULL) {
		return;
	}
	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	voice->queued.clear();
	voice->queuedAt = 0;
	voice->queuedFrames = 0;
	resetResampler(&voice->mix);
}

void AILCALL AIL_ex_set_pcm_volume(HEXPCM handle, F32 volume)
{
	PcmVoice *voice = (PcmVoice *)handle;
	if (voice == NULL) {
		return;
	}
	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	voice->mix.gainLeft = voice->mix.gainRight = clampUnit(volume);
}

void AILCALL AIL_ex_stop_capture(void)
{
	Capture *capture;
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		capture = g_engine.capture;
		g_engine.capture = NULL;
	}
	if (capture != NULL) {
		writeCaptureSizes(capture);
		fclose(capture->file);
		g_engine.capturedFrames = capture->dataBytes / (capture->channels * sizeof(short));
		g_engine.capturedRate = capture->samplesPerSecond;
		delete capture;
	}
}

void AILCALL AIL_ex_offline_mix(void)
{
	g_engine.offline = true;
}

// The logic runs 30 frames a second of game time (LOGICFRAMES_PER_SECOND), whatever the wall clock
// does.  Each target is computed from the absolute frame, so the remainders of a rate 30 does not
// divide never add up: frames a to b always mix (b - a) * rate / 30, give or take one sample.
void AILCALL AIL_ex_mix_to_frame(S32 logicFrame)
{
	const long long LOGIC_FRAMES_PER_SECOND = 30;
	if (!g_engine.offline || !g_engine.deviceReady) {
		return;
	}
	const long long target = (long long)logicFrame * g_engine.deviceRate / LOGIC_FRAMES_PER_SECOND;
	// The first call, and a logic clock that went back (a new match starts at frame 0), place the mix
	// rather than make it play the difference.
	if (!g_engine.mixAnchored || target < g_engine.mixedFrames) {
		g_engine.mixAnchored = true;
		g_engine.mixedFrames = target;
		return;
	}
	float out[MIX_CHUNK_FRAMES * OUTPUT_CHANNELS];
	while (g_engine.mixedFrames < target) {
		const unsigned frames = target - g_engine.mixedFrames < MIX_CHUNK_FRAMES
			? (unsigned)(target - g_engine.mixedFrames) : MIX_CHUNK_FRAMES;
		dataCallback(&g_engine.device, out, NULL, frames);
		g_engine.mixedFrames += frames;
	}
}

void AILCALL AIL_ex_capture_length(S32 *frames, S32 *rate)
{
	*frames = (S32)g_engine.capturedFrames;
	*rate = (S32)g_engine.capturedRate;
}

void AILCALL AIL_shutdown(void)
{
	AIL_ex_stop_capture();

	if (g_engine.serviceThread.joinable()) {
		g_engine.serviceRunning.store(false);
		g_engine.serviceThread.join();
	}

	if (g_engine.deviceReady) {
		ma_device_uninit(&g_engine.device);
		g_engine.deviceReady = false;
	}
	if (g_engine.contextReady) {
		ma_context_uninit(&g_engine.context);
		g_engine.contextReady = false;
	}
	g_engine.started = false;
}

char *AILCALL AIL_set_redist_directory(const char *)
{
	return NULL;
}

void AILCALL AIL_lock(void)
{
	if (g_engine.started) {
		g_engine.lock.lock();
	}
}

void AILCALL AIL_unlock(void)
{
	if (g_engine.started) {
		g_engine.lock.unlock();
	}
}

S32 AILCALL AIL_get_timer_highest_delay(void)
{
	return SERVICE_PERIOD_MS;
}

void AILCALL AIL_set_file_callbacks(AILFILEOPENCB opencb, AILFILECLOSECB closecb, AILFILESEEKCB seekcb, AILFILEREADCB readcb)
{
	g_fileOpen = opencb;
	g_fileClose = closecb;
	g_fileSeek = seekcb;
	g_fileRead = readcb;
}

// =================================================================================================
// Providers and filters
// =================================================================================================

S32 AILCALL AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name)
{
	// MilesAudioManager looks its provider up by name ("Miles Fast 2D Positional Audio"), and
	// indexes its array with the result without checking it, so those names have to be here.
	static char fast2D[] = "Miles Fast 2D Positional Audio";
	static char dolby[] = "Dolby Surround";

	switch (*next) {
		case 0:
			*dest = PROVIDER_FAST_2D;
			*name = fast2D;
			*next = 1;
			return 1;
		case 1:
			*dest = PROVIDER_DOLBY;
			*name = dolby;
			*next = 2;
			return 1;
		default:
			return 0;
	}
}

S32 AILCALL AIL_open_3D_provider(HPROVIDER lib)
{
	return (lib == PROVIDER_FAST_2D || lib == PROVIDER_DOLBY) ? 0 : 1;
}

void AILCALL AIL_close_3D_provider(HPROVIDER)
{
}

S32 AILCALL AIL_enumerate_filters(HPROENUM *, HPROVIDER *, char **)
{
	// No pipeline filters: the only one the game looks for is the Mono Delay, and a missing one
	// leaves m_delayFilter NULL, which it already handles.
	return 0;
}

void AILCALL AIL_set_3D_speaker_type(HPROVIDER, S32)
{
}

HPROVIDER AILCALL AIL_set_sample_processor(HSAMPLE, S32, HPROVIDER provider)
{
	return provider;
}

void AILCALL AIL_set_filter_sample_preference(HSAMPLE, const char *, const void *)
{
}

// =================================================================================================
// 2D samples
// =================================================================================================

HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER dig)
{
	if (dig == NULL || !g_engine.deviceReady) {
		return NULL;
	}

	Sample *sample = new Sample;
	sample->is3D = false;
	memset(&sample->format, 0, sizeof(sample->format));
	sample->data = NULL;
	sample->dataBytes = 0;
	sample->volume = 1.0f;
	sample->pan = DEFAULT_PAN;
	sample->baseRate = 0;
	sample->rate = 0;
	memset(sample->userData, 0, sizeof(sample->userData));
	sample->endOfSample = NULL;
	sample->endOf3DSample = NULL;
	sample->playing = false;
	sample->positionX = sample->positionY = sample->positionZ = 0.0f;
	sample->minDistance = 0.0f;
	sample->maxDistance = 1.0f;
	sample->occlusion = 0.0f;
	sample->running = false;
	sample->queued = false;
	sample->cursor = 0;
	sample->sourceDry = false;

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	g_engine.samples.push_back(sample);
	return (HSAMPLE)sample;
}

void AILCALL AIL_release_sample_handle(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	{
		std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		for (size_t i = 0; i < g_engine.samples.size(); ++i) {
			if (g_engine.samples[i] == sample) {
				g_engine.samples.erase(g_engine.samples.begin() + i);
				break;
			}
		}
	}
	if (sample->mix.resamplerReady) {
		ma_resampler_uninit(&sample->mix.resampler, NULL);
	}
	delete sample;
}

void AILCALL AIL_init_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		flushSample(sample);
		sample->data = NULL;
		sample->dataBytes = 0;
	}
	sample->playing = false;
	sample->volume = 1.0f;
	sample->pan = DEFAULT_PAN;
	sample->endOfSample = NULL;
}

S32 AILCALL AIL_set_sample_file(HSAMPLE handle, const void *file_image, S32)
{
	Sample *sample = (Sample *)handle;
	WaveImage wave;
	if (sample == NULL || !parseWave(file_image, &wave) || wave.format.tag != WAVE_FORMAT_PCM) {
		return 0;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->playing = false;
	const bool ready = ensureVoice(sample, wave.format);
	if (ready) {
		{
			std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
			sample->data = wave.data;
			sample->dataBytes = wave.dataBytes;
		}
		sample->baseRate = wave.format.samplesPerSecond;
		sample->rate = wave.format.samplesPerSecond;
		refreshSampleOutput(sample);
	}
	return ready ? 1 : 0;
}

void AILCALL AIL_start_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->mix.channels == 0 || sample->data == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		flushSample(sample);
		sample->queued = true;
	}
	refreshSampleOutput(sample);
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		sample->running = true;
	}
	sample->playing = true;
}

void AILCALL AIL_stop_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->mix.channels == 0) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	{
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		sample->running = false;
	}
	sample->playing = false;
}

void AILCALL AIL_resume_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->mix.channels == 0) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	if (sample->queued) {
		sample->running = true;
		sample->playing = true;
	}
}

void AILCALL AIL_end_sample(HSAMPLE handle)
{
	AIL_stop_sample(handle);
}

void AILCALL AIL_set_sample_volume_pan(HSAMPLE handle, F32 volume, F32 pan)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->volume = volume;
	sample->pan = pan;
	refreshSampleOutput(sample);
}

void AILCALL AIL_sample_volume_pan(HSAMPLE handle, F32 *volume, F32 *pan)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}
	if (volume != NULL) *volume = sample->volume;
	if (pan != NULL) *pan = sample->pan;
}

void AILCALL AIL_set_sample_playback_rate(HSAMPLE handle, S32 playback_rate)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || playback_rate <= 0) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->rate = (unsigned int)playback_rate;
	refreshSampleOutput(sample);
}

S32 AILCALL AIL_sample_playback_rate(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	return sample != NULL ? (S32)sample->rate : 0;
}

void AILCALL AIL_set_sample_user_data(HSAMPLE handle, U32 index, S32 value)
{
	Sample *sample = (Sample *)handle;
	if (sample != NULL && index < USER_DATA_SLOTS) {
		sample->userData[index] = value;
	}
}

S32 AILCALL AIL_sample_user_data(HSAMPLE handle, U32 index)
{
	Sample *sample = (Sample *)handle;
	return (sample != NULL && index < USER_DATA_SLOTS) ? sample->userData[index] : 0;
}

AILSAMPLECB AILCALL AIL_register_EOS_callback(HSAMPLE handle, AILSAMPLECB EOS)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return NULL;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	AILSAMPLECB previous = sample->endOfSample;
	sample->endOfSample = EOS;
	return previous;
}

void AILCALL AIL_get_DirectSound_info(HSAMPLE, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB)
{
	// There is no DirectSound object behind miniaudio either.  The two callers both handle NULL:
	// the speaker config falls back to stereo, and Bink plays its own audio.
	if (lplpDS != NULL) *lplpDS = NULL;
	if (lplpDSB != NULL) *lplpDSB = NULL;
}

// =================================================================================================
// 3D samples
// =================================================================================================

H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER lib)
{
	if (lib != PROVIDER_FAST_2D && lib != PROVIDER_DOLBY) {
		return NULL;
	}

	HSAMPLE handle = AIL_allocate_sample_handle(DIGITAL_DRIVER);
	Sample *sample = (Sample *)handle;
	if (sample != NULL) {
		sample->is3D = true;
		sample->minDistance = 1.0f;
		sample->maxDistance = 1000.0f;
	}
	return (H3DSAMPLE)sample;
}

void AILCALL AIL_release_3D_sample_handle(H3DSAMPLE handle)
{
	AIL_release_sample_handle((HSAMPLE)handle);
}

S32 AILCALL AIL_set_3D_sample_file(H3DSAMPLE handle, const void *file_image)
{
	Sample *sample = (Sample *)handle;
	if (sample != NULL) {
		sample->occlusion = 0.0f;
	}
	return AIL_set_sample_file((HSAMPLE)handle, file_image, 0);
}

void AILCALL AIL_start_3D_sample(H3DSAMPLE handle)
{
	AIL_start_sample((HSAMPLE)handle);
}

void AILCALL AIL_stop_3D_sample(H3DSAMPLE handle)
{
	AIL_stop_sample((HSAMPLE)handle);
}

void AILCALL AIL_resume_3D_sample(H3DSAMPLE handle)
{
	AIL_resume_sample((HSAMPLE)handle);
}

void AILCALL AIL_end_3D_sample(H3DSAMPLE handle)
{
	AIL_stop_sample((HSAMPLE)handle);
}

void AILCALL AIL_set_3D_sample_volume(H3DSAMPLE handle, F32 volume)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->volume = volume;
	refreshSampleOutput(sample);
}

void AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE handle, S32 playback_rate)
{
	AIL_set_sample_playback_rate((HSAMPLE)handle, playback_rate);
}

S32 AILCALL AIL_3D_sample_playback_rate(H3DSAMPLE handle)
{
	return AIL_sample_playback_rate((HSAMPLE)handle);
}

void AILCALL AIL_set_3D_sample_distances(H3DSAMPLE handle, F32 max_dist, F32 min_dist)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->maxDistance = max_dist;
	sample->minDistance = min_dist;
	refreshSampleOutput(sample);
}

void AILCALL AIL_ex_set_3D_linear_falloff(S32 linear)
{
	// A playing sample takes it at its next position update.
	g_linearFalloff.store(linear);
}

void AILCALL AIL_set_3D_sample_occlusion(H3DSAMPLE handle, F32 occlusion)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	sample->occlusion = clampUnit(occlusion);
	refreshSampleOutput(sample);
}

void AILCALL AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value)
{
	AIL_set_sample_user_data((HSAMPLE)obj, index, value);
}

S32 AILCALL AIL_3D_user_data(H3DPOBJECT obj, U32 index)
{
	return AIL_sample_user_data((HSAMPLE)obj, index);
}

AIL3DSAMPLECB AILCALL AIL_register_3D_EOS_callback(H3DSAMPLE handle, AIL3DSAMPLECB EOS)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return NULL;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	AIL3DSAMPLECB previous = sample->endOf3DSample;
	sample->endOf3DSample = EOS;
	return previous;
}

// =================================================================================================
// 3D positioning
// =================================================================================================

H3DPOBJECT AILCALL AIL_open_3D_listener(HPROVIDER lib)
{
	return (lib == PROVIDER_FAST_2D || lib == PROVIDER_DOLBY) ? LISTENER_OBJECT : NULL;
}

void AILCALL AIL_close_3D_listener(H3DPOBJECT)
{
}

void AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z)
{
	if (obj == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	if (obj == LISTENER_OBJECT) {
		g_engine.listener.positionX = X;
		g_engine.listener.positionY = Y;
		g_engine.listener.positionZ = Z;
	} else {
		Sample *sample = (Sample *)obj;
		sample->positionX = X;
		sample->positionY = Y;
		sample->positionZ = Z;
		refreshSampleOutput(sample);
	}
}

void AILCALL AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up)
{
	if (obj != LISTENER_OBJECT) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	g_engine.listener.faceX = X_face;
	g_engine.listener.faceY = Y_face;
	g_engine.listener.faceZ = Z_face;
	g_engine.listener.upX = X_up;
	g_engine.listener.upY = Y_up;
	g_engine.listener.upZ = Z_up;
}

void AILCALL AIL_set_3D_velocity_vector(H3DPOBJECT, F32, F32, F32)
{
	// No doppler: Miles' was off in this game's settings too.
}

// =================================================================================================
// Streams
// =================================================================================================

HSTREAM AILCALL AIL_open_stream(HDIGDRIVER dig, const char *filename, S32)
{
	if (dig == NULL || filename == NULL || !g_engine.deviceReady) {
		return NULL;
	}

	// Opened outside the engine lock, as miles_xaudio2 does: this reads the game's files through its
	// callbacks, and holding the lock here would add a lock order the Windows build does not have.
	Stream *stream = new Stream;
	if (!openStreamDecoder(stream, filename)) {
		closeStreamDecoder(stream);
		if (stream->mix.resamplerReady) {
			ma_resampler_uninit(&stream->mix.resampler, NULL);
		}
		delete stream;
		return NULL;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	g_engine.streams.push_back(stream);
	return (HSTREAM)stream;
}

void AILCALL AIL_close_stream(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	{
		std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
		{
			std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
			for (size_t i = 0; i < g_engine.streams.size(); ++i) {
				if (g_engine.streams[i] == stream) {
					g_engine.streams.erase(g_engine.streams.begin() + i);
					break;
				}
			}
			stream->queued.clear();
		}
		closeStreamDecoder(stream);
	}
	if (stream->mix.resamplerReady) {
		ma_resampler_uninit(&stream->mix.resampler, NULL);
	}
	delete stream;
}

void AILCALL AIL_start_stream(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL || !stream->hasVoice) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	stream->playing = true;
	stream->paused = false;
	stream->exhausted = false;
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	setGains(&stream->mix, stream->volume, stream->pan);
	stream->running = true;
}

void AILCALL AIL_pause_stream(HSTREAM handle, S32 onoff)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL || !stream->hasVoice) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
	stream->paused = onoff != 0;
	if (stream->paused) {
		stream->running = false;
	} else {
		stream->running = true;
		stream->playing = true;
	}
}

void AILCALL AIL_set_stream_loop_count(HSTREAM handle, S32 count)
{
	Stream *stream = (Stream *)handle;
	if (stream != NULL) {
		stream->loopCount = count;
	}
}

S32 AILCALL AIL_stream_loop_count(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	return stream != NULL ? stream->loopCount : 0;
}

void AILCALL AIL_set_stream_volume_pan(HSTREAM handle, F32 volume, F32 pan)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	stream->volume = volume;
	stream->pan = pan;
	if (stream->hasVoice) {
		std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
		setGains(&stream->mix, volume, pan);
	}
}

void AILCALL AIL_stream_volume_pan(HSTREAM handle, F32 *volume, F32 *pan)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}
	if (volume != NULL) *volume = stream->volume;
	if (pan != NULL) *pan = stream->pan;
}

void AILCALL AIL_stream_ms_position(HSTREAM handle, S32 *total_milliseconds, S32 *current_milliseconds)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	if (total_milliseconds != NULL) {
		// Measured on first request rather than at open.  miniaudio's MP3 length is exact and costs a
		// pass over the file; FFmpeg's was an estimate taken at open.  Nothing waits on a music
		// stream's open for it, and the one caller that asks opens a stream to do nothing else.
		std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
		if (stream->totalMs < 0.0) {
			ma_uint64 frames = 0;
			stream->totalMs = 0.0;
			if (stream->decoderReady && stream->rate != 0 &&
				ma_decoder_get_length_in_pcm_frames(&stream->decoder, &frames) == MA_SUCCESS) {
				stream->totalMs = (double)frames * 1000.0 / (double)stream->rate;
			}
		}
		*total_milliseconds = (S32)stream->totalMs;
	}
	if (current_milliseconds != NULL) {
		S32 position = 0;
		if (stream->hasVoice && stream->rate != 0) {
			std::lock_guard<std::mutex> mixGuard(g_engine.mixLock);
			position = (S32)(stream->framesPlayed * 1000 / stream->rate);
		}
		*current_milliseconds = position;
	}
}

AILSTREAMCB AILCALL AIL_register_stream_callback(HSTREAM handle, AILSTREAMCB callback)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return NULL;
	}

	std::lock_guard<std::recursive_mutex> guard(g_engine.lock);
	AILSTREAMCB previous = stream->callback;
	stream->callback = callback;
	return previous;
}

// =================================================================================================
// Quick API - mission briefing speech
// =================================================================================================

HAUDIO AILCALL AIL_quick_load_and_play(const char *filename, U32 loop_count, S32)
{
	HSTREAM stream = AIL_open_stream(DIGITAL_DRIVER, filename, 0);
	if (stream == NULL) {
		return NULL;
	}

	AIL_set_stream_loop_count(stream, loop_count == 0 ? 1 : (S32)loop_count);
	AIL_start_stream(stream);
	return (HAUDIO)stream;
}

void AILCALL AIL_quick_unload(HAUDIO audio)
{
	AIL_close_stream((HSTREAM)audio);
}

void AILCALL AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 extravol)
{
	AIL_set_stream_volume_pan((HSTREAM)audio, volume, extravol);
}

// =================================================================================================
// File format helpers
// =================================================================================================

S32 AILCALL AIL_WAV_info(const void *data, AILSOUNDINFO *info)
{
	WaveImage wave;
	if (info == NULL || !parseWave(data, &wave)) {
		return 0;
	}

	memset(info, 0, sizeof(*info));
	info->format = wave.format.tag;
	info->data_ptr = wave.data;
	info->data_len = wave.dataBytes;
	info->rate = wave.format.samplesPerSecond;
	info->bits = wave.format.bitsPerSample;
	info->channels = wave.format.channels;
	info->samples = wavePcmSampleCount(wave);
	info->block_size = wave.format.blockAlign;
	info->initial_ptr = data;
	return 1;
}

S32 AILCALL AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, U32 *outsize)
{
	if (info == NULL || outdata == NULL || outsize == NULL) {
		return 0;
	}

	WaveImage wave;
	if (!parseWave(info->initial_ptr, &wave) || wave.format.tag != WAVE_FORMAT_IMA_ADPCM) {
		return 0;
	}

	std::vector<short> pcm;
	const unsigned int frames = decodeAdpcm(wave, &pcm);
	if (frames == 0) {
		return 0;
	}

	// The caller hands the result straight to AIL_set_sample_file, so it has to be a WAV image.
	const unsigned int channels = wave.format.channels;
	const unsigned int payloadBytes = (unsigned int)(pcm.size() * sizeof(short));
	const unsigned int imageBytes = 44 + payloadBytes;
	unsigned char *image = (unsigned char *)malloc(imageBytes);
	if (image == NULL) {
		return 0;
	}

	const unsigned int blockAlign = channels * 2;
	const unsigned int byteRate = wave.format.samplesPerSecond * blockAlign;
	memcpy(image, "RIFF", 4);
	writeU32(image + 4, imageBytes - 8);
	memcpy(image + 8, "WAVEfmt ", 8);
	writeU32(image + 16, 16);
	writeU16(image + 20, WAVE_FORMAT_PCM);
	writeU16(image + 22, (unsigned short)channels);
	writeU32(image + 24, wave.format.samplesPerSecond);
	writeU32(image + 28, byteRate);
	writeU16(image + 32, (unsigned short)blockAlign);
	writeU16(image + 34, 16);
	memcpy(image + 36, "data", 4);
	writeU32(image + 40, payloadBytes);
	// Little-endian samples, as the WAV format and XAudio2's memcpy of them both assume.
	for (size_t i = 0; i < pcm.size(); ++i) {
		writeU16(image + 44 + i * 2, (unsigned short)pcm[i]);
	}

	*outdata = image;
	*outsize = imageBytes;
	return 1;
}

void AILCALL AIL_mem_free_lock(void *ptr)
{
	free(ptr);
}

} // extern "C"
