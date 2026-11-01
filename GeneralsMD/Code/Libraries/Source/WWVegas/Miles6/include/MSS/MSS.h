/*
 * Miles Sound System 6.5 declarations, reconstructed - not the SDK header.
 *
 * The real SDK is binary-only and absent, but the game ships mss32.dll and it
 * exports decorated stdcall names (_AIL_startup@0, ...), so the exe can bind to
 * it directly through an import library generated from stub/miles.def.  This
 * header declares just the surface MilesAudioManager.cpp and the WWAudio
 * headers touch.
 *
 * Sources of truth, in order:
 *  - stub/miles.def (from Thyme, github.com/TheAssemblyArmada/Thyme) - the
 *    stdcall @N byte counts validate every signature's argument bytes against
 *    the real DLL's export table (dumpbin /exports on the shipped mss32.dll
 *    matches this def name for name);
 *  - Thyme's deps/miles/miles.h - signatures reverse-engineered against this
 *    same game;
 *  - a public copy of the Miles 6.0 mss.h - constants (speaker types, pipeline
 *    stages, HPROENUM_FIRST) and the AIL_MSS_version macro.  The 5.1/7.1
 *    speaker values are 6.5 additions that continue the 6.0 sequence; they only
 *    select a speaker layout, so a mismatch cannot corrupt anything.
 */

#ifndef MSS_H
#define MSS_H

/* Windows links both backends into one exe, XAudio2 for play and miniaudio for a -wav run, each
   compiled under its own prefix; miles_dispatch.cpp owns the plain names and picks one. */
#ifdef MSS_BACKEND_PREFIX
#include "mss_backend_names.h"
#endif

#include <math.h>	/* the inline helpers at the end */

/* The real Mss.H pulls in the Windows multimedia headers; WWAudio relies on
 * that for LPWAVEFORMAT in its Miles-facing signatures. */
#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>

#define AILCALL     __stdcall
#ifndef AILCALLBACK
#define AILCALLBACK __stdcall
#endif
#else
/* Off Windows the same surface is implemented on miniaudio (Miles6/miniaudio, decision 3).  There
 * is no DLL to bind to, so there is no calling convention to match, and the two names mmsystem.h
 * supplied come from here instead.  C4. */
#include <stdint.h>
#include <stdio.h>
#define AILCALL
#ifndef AILCALLBACK
#define AILCALLBACK
#endif
#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif
#endif

/* Miles' scalar names are the long family (S32 = signed long), and the game
 * passes long* where the API wants S32* - keep the real SDK's choice.
 * On LP64 that makes S32 and U32 64 bits wide, and it stays that way on purpose, unlike the
 * width fixes elsewhere in this port: MilesAudioManager.cpp:2842 hands AIL_stream_ms_position a
 * long*, and that file is not to change.  Nothing here reaches a file or a network - the API is
 * in-process between the game and its own backend - and every value through it is an index, a
 * millisecond count or a rate.  C4. */
typedef signed char        S8;
typedef unsigned char      U8;
typedef signed short       S16;
typedef unsigned short     U16;
typedef signed long        S32;
typedef unsigned long      U32;
typedef float              F32;
typedef double             F64;
typedef char               C8;

/* Opaque Miles handles. The real SDK declares these as pointers to undefined
 * structs; keep that shape so `= NULL` and pointer comparisons still compile.
 * A 3D sample IS a positionable object in Miles: H3DSAMPLE aliases H3DPOBJECT
 * (the game passes samples to AIL_set_3D_position et al). */
struct _AIL_DIGDRIVER;   typedef struct _AIL_DIGDRIVER  *HDIGDRIVER;
struct _AIL_SAMPLE;      typedef struct _AIL_SAMPLE     *HSAMPLE;
struct _AIL_STREAM;      typedef struct _AIL_STREAM     *HSTREAM;
struct _AIL_3DPOBJECT;   typedef struct _AIL_3DPOBJECT  *H3DPOBJECT;
typedef H3DPOBJECT H3DSAMPLE;
struct _AIL_TIMER;       typedef struct _AIL_TIMER      *HTIMER;
struct _AIL_AUDIO;       typedef struct _AIL_AUDIO      *HAUDIO;
struct _AIL_MDIDRIVER;   typedef struct _AIL_MDIDRIVER  *HMDIDRIVER;
struct _AIL_DLSDEVICE;   typedef struct _AIL_DLSDEVICE  *HDLSDEVICE;

/* mmsystem.h leaves the compressed-format tags to mmreg.h. */
#ifndef WAVE_FORMAT_IMA_ADPCM
#define WAVE_FORMAT_IMA_ADPCM 0x0011
#endif

/* Providers are enumerated by value, not by pointer. */
typedef U32 HPROVIDER;
typedef U32 HPROENUM;
#define HPROENUM_FIRST 0
typedef S32 M3DRESULT;

typedef void *AILLPDIRECTSOUND;
typedef void *AILLPDIRECTSOUNDBUFFER;

/* 3D speaker types (AIL_set_3D_speaker_type). 0-3 are verbatim Miles 6.0;
 * 4-5 are the 6.5 additions continuing the sequence.  What each one does here: 2 speakers and
 * Surround (Dolby's matrix, which nothing decodes any more) are the stereo pan; Headphones is the
 * stereo pan's place taken by AIL_ex_hrtf on every 3D sample, in both backends; 4, 5.1 and 7.1 are a
 * mastering voice of that many channels with every 3D sample panned round them (XAudio2 only:
 * miniaudio plays them as stereo).  A change rebuilds XAudio2's output, as a lost device does. */
#define AIL_3D_2_SPEAKER  0
#define AIL_3D_HEADPHONE  1
#define AIL_3D_SURROUND   2
#define AIL_3D_4_SPEAKER  3
#define AIL_3D_51_SPEAKER 4
#define AIL_3D_71_SPEAKER 5

/* Digital pipeline stages (AIL_set_sample_processor). */
typedef enum
{
	DP_ASI_DECODER = 0,  /* Must be "ASI codec stream" provider */
	DP_FILTER,           /* Must be "MSS pipeline filter" provider */
	DP_MERGE,            /* Must be "MSS mixer" provider */
	N_SAMPLE_STAGES,
	SAMPLE_ALL_STAGES
} SAMPLESTAGE;

/* AIL_WAV_info / AIL_decompress_ADPCM exchange this. */
typedef struct _AILSOUNDINFO
{
	S32         format;
	const void *data_ptr;
	U32         data_len;
	U32         rate;
	S32         bits;
	S32         channels;
	U32         samples;
	U32         block_size;
	const void *initial_ptr;
} AILSOUNDINFO;

/* Callback shapes as the game defines them (typed handles, U32 file handles). */
typedef void (AILCALLBACK *AILSAMPLECB)   (HSAMPLE sample);
typedef void (AILCALLBACK *AIL3DSAMPLECB) (H3DSAMPLE sample);
typedef void (AILCALLBACK *AILSTREAMCB)   (HSTREAM stream);
/* The file handle is whatever the host wants it to be and this game puts a File* in it, so it is
   pointer sized.  On Win32 that is the U32 the retail DLL's ABI expects; x64 needs all 64 bits. */
#if defined(_WIN32)
typedef UINT_PTR AILFILEHANDLE;
#else
typedef uintptr_t AILFILEHANDLE;
#endif
typedef U32  (AILCALLBACK *AILFILEOPENCB) (const char *filename, AILFILEHANDLE *file_handle);
typedef void (AILCALLBACK *AILFILECLOSECB)(AILFILEHANDLE file_handle);
typedef S32  (AILCALLBACK *AILFILESEEKCB) (AILFILEHANDLE file_handle, S32 offset, U32 type);
typedef U32  (AILCALLBACK *AILFILEREADCB) (AILFILEHANDLE file_handle, void *buffer, U32 bytes);

/* Thyme's stub miles.c spells the same callback types this way. */
typedef AILSAMPLECB    AIL_sample_callback;
typedef AIL3DSAMPLECB  AIL_3dsample_callback;
typedef AILSTREAMCB    AIL_stream_callback;
typedef AILFILEOPENCB  AIL_file_open_callback;
typedef AILFILECLOSECB AIL_file_close_callback;
typedef AILFILESEEKCB  AIL_file_seek_callback;
typedef AILFILEREADCB  AIL_file_read_callback;

/* The version string lives in the DLL's string table, resource id 1; the real
 * header reads it the same way instead of importing a function. */
#define MSSDLLNAME "MSS32.DLL"
#if defined(_WIN32)
#define AIL_MSS_version(str, len)                \
{                                                \
	HINSTANCE mssvl = LoadLibraryA(MSSDLLNAME);  \
	if (mssvl == NULL)                           \
		*(str) = 0;                              \
	else {                                       \
		LoadStringA(mssvl, 1, str, len);         \
		FreeLibrary(mssvl);                      \
	}                                            \
}
#else
/* No DLL and no string table: the one caller logs it, so say what is actually underneath. */
#define AIL_MSS_version(str, len)                \
{                                                \
	snprintf((str), (size_t)(len), "%s", "Miles 6.5 surface on miniaudio"); \
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- startup / global ------------------------------------------------- */
S32       AILCALL AIL_startup(void);
void      AILCALL AIL_shutdown(void);
S32       AILCALL AIL_quick_startup(S32 use_digital, S32 use_MIDI, U32 output_rate, S32 output_bits, S32 output_channels);
void      AILCALL AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls);
char *    AILCALL AIL_set_redist_directory(const char *dir);
void      AILCALL AIL_lock(void);
void      AILCALL AIL_unlock(void);
S32       AILCALL AIL_get_timer_highest_delay(void);
void      AILCALL AIL_set_file_callbacks(AILFILEOPENCB opencb, AILFILECLOSECB closecb, AILFILESEEKCB seekcb, AILFILEREADCB readcb);

/* ---- providers / filters ---------------------------------------------- */
S32       AILCALL AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name);
S32       AILCALL AIL_open_3D_provider(HPROVIDER lib);
void      AILCALL AIL_close_3D_provider(HPROVIDER lib);
S32       AILCALL AIL_enumerate_filters(HPROENUM *next, HPROVIDER *dest, char **name);
void      AILCALL AIL_set_3D_speaker_type(HPROVIDER lib, S32 speaker_type);
HPROVIDER AILCALL AIL_set_sample_processor(HSAMPLE sample, S32 pipeline_stage, HPROVIDER provider);
void      AILCALL AIL_set_filter_sample_preference(HSAMPLE sample, const char *name, const void *val);

/* ---- 2D samples -------------------------------------------------------- */
HSAMPLE   AILCALL AIL_allocate_sample_handle(HDIGDRIVER dig);
void      AILCALL AIL_release_sample_handle(HSAMPLE sample);
void      AILCALL AIL_init_sample(HSAMPLE sample);
S32       AILCALL AIL_set_sample_file(HSAMPLE sample, const void *file_image, S32 block);
void      AILCALL AIL_start_sample(HSAMPLE sample);
void      AILCALL AIL_stop_sample(HSAMPLE sample);
void      AILCALL AIL_resume_sample(HSAMPLE sample);
void      AILCALL AIL_end_sample(HSAMPLE sample);
void      AILCALL AIL_set_sample_volume_pan(HSAMPLE sample, F32 volume, F32 pan);
void      AILCALL AIL_sample_volume_pan(HSAMPLE sample, F32 *volume, F32 *pan);
void      AILCALL AIL_set_sample_playback_rate(HSAMPLE sample, S32 playback_rate);
S32       AILCALL AIL_sample_playback_rate(HSAMPLE sample);
void      AILCALL AIL_set_sample_user_data(HSAMPLE sample, U32 index, S32 value);
S32       AILCALL AIL_sample_user_data(HSAMPLE sample, U32 index);
AILSAMPLECB AILCALL AIL_register_EOS_callback(HSAMPLE sample, AILSAMPLECB EOS);
void      AILCALL AIL_get_DirectSound_info(HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB);

/* ---- 3D samples -------------------------------------------------------- */
H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER lib);
void      AILCALL AIL_release_3D_sample_handle(H3DSAMPLE sample);
S32       AILCALL AIL_set_3D_sample_file(H3DSAMPLE sample, const void *file_image);
void      AILCALL AIL_start_3D_sample(H3DSAMPLE sample);
void      AILCALL AIL_stop_3D_sample(H3DSAMPLE sample);
void      AILCALL AIL_resume_3D_sample(H3DSAMPLE sample);
void      AILCALL AIL_end_3D_sample(H3DSAMPLE sample);
void      AILCALL AIL_set_3D_sample_volume(H3DSAMPLE sample, F32 volume);
void      AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, S32 playback_rate);
S32       AILCALL AIL_3D_sample_playback_rate(H3DSAMPLE sample);
void      AILCALL AIL_set_3D_sample_distances(H3DSAMPLE sample, F32 max_dist, F32 min_dist);
void      AILCALL AIL_set_3D_sample_occlusion(H3DSAMPLE sample, F32 occlusion);
void      AILCALL AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value);
S32       AILCALL AIL_3D_user_data(H3DPOBJECT obj, U32 index);
AIL3DSAMPLECB AILCALL AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL3DSAMPLECB EOS);

/* ---- 3D positioning ---------------------------------------------------- */
H3DPOBJECT AILCALL AIL_open_3D_listener(HPROVIDER lib);
void      AILCALL AIL_close_3D_listener(H3DPOBJECT listener);
void      AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z);
void      AILCALL AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up);
void      AILCALL AIL_set_3D_velocity_vector(H3DPOBJECT obj, F32 dX, F32 dY, F32 dZ);

/* ---- streams ----------------------------------------------------------- */
HSTREAM   AILCALL AIL_open_stream(HDIGDRIVER dig, const char *filename, S32 stream_mem);
void      AILCALL AIL_close_stream(HSTREAM stream);
void      AILCALL AIL_start_stream(HSTREAM stream);
void      AILCALL AIL_pause_stream(HSTREAM stream, S32 onoff);
void      AILCALL AIL_set_stream_loop_count(HSTREAM stream, S32 count);
S32       AILCALL AIL_stream_loop_count(HSTREAM stream);
void      AILCALL AIL_set_stream_volume_pan(HSTREAM stream, F32 volume, F32 pan);
void      AILCALL AIL_stream_volume_pan(HSTREAM stream, F32 *volume, F32 *pan);
void      AILCALL AIL_stream_ms_position(HSTREAM stream, S32 *total_milliseconds, S32 *current_milliseconds);
AILSTREAMCB AILCALL AIL_register_stream_callback(HSTREAM stream, AILSTREAMCB callback);

/* ---- quick API (music) ------------------------------------------------- */
HAUDIO    AILCALL AIL_quick_load_and_play(const char *filename, U32 loop_count, S32 wait_request);
void      AILCALL AIL_quick_unload(HAUDIO audio);
void      AILCALL AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 extravol);

/* ---- capture ----------------------------------------------------------- */
/* Not Miles.  The fork's own pair: everything the game plays is mixed into one mastering voice, so
   an effect on that voice is the whole soundtrack and nothing else on the machine.  The caller
   decides when the recording starts, because only the caller knows what logic frame it is. */
S32       AILCALL AIL_ex_start_capture(const char *pathname);
void      AILCALL AIL_ex_stop_capture(void);

/* The logic clock's mix, for -wav.  AIL_ex_offline_mix, called before AIL_startup, opens no audio
   device: the mix moves only when AIL_ex_mix_to_frame asks it to, logicFrame * rate / 60 frames from
   the start, so a capture holds exactly the sound of the logic frames it spans however fast or slow
   the run went.  Nothing is heard meanwhile.  AIL_ex_capture_length reports what the last finished
   capture wrote, in frames at its rate; the XAudio2 backend does not count and says zero.
   ZH_WAV_XAUDIO2 in the environment leaves the run on XAudio2 instead, recording its master in real
   time (miles_dispatch.cpp). */
void      AILCALL AIL_ex_offline_mix(void);
void      AILCALL AIL_ex_mix_to_frame(S32 logicFrame);
void      AILCALL AIL_ex_capture_length(S32 *frames, S32 *rate);

/* ---- distance falloff -------------------------------------------------- */
/* Not Miles.  Nonzero makes every 3D sample fall off in a straight line from min_dist to max_dist
   instead of Miles' min_dist / distance; MilesAudioManager passes AudioSettings.ini's
   RangeVolumeFade, so what plays and what getEffectiveVolume culls follow one curve. */
void      AILCALL AIL_ex_set_3D_linear_falloff(S32 linear);

/* ---- air, room and duck ------------------------------------------------ */
/* Not Miles.  What MilesAudioManager hands down from AudioSettings.ini and the map.
 *
 * AIL_ex_set_3D_air: the speed of sound in game units a second, for doppler on the velocity
 * AIL_set_3D_velocity_vector gives a 3D sample (units a second as well).  Zero, where it starts,
 * switches it off.
 *
 * Both backends hold the mix under AIL_EX_LIMITER_CEILING with the same limiter (AIL_ex_limit), so
 * nothing they play or record clips.  AIL_ex_set_limiter(0) passes the mix through untouched, as
 * Miles did, for the Legacy sound option; it starts on.
 *
 * AIL_ex_set_3D_sample_dry: a 3D sample that takes no doppler, only its distance gain, pan and
 * occlusion; a unit's reply stays clean.  Set on each play; a fresh handle is wet. */
void      AILCALL AIL_ex_set_3D_air(F32 speed_of_sound);
void      AILCALL AIL_ex_set_3D_sample_dry(H3DSAMPLE handle, S32 dry);
void      AILCALL AIL_ex_set_limiter(S32 on);

/* ---- output device ------------------------------------------------------ */
/* Not Miles.  AIL_ex_set_output_device: the endpoint to play on, an MMDevice id as IMMDevice::GetId
 * gives it, NULL or "" for the system's default.  May come before AIL_startup, which is how WinMain
 * hands down the one Options.ini saved; later, XAudio2 rebuilds its output on it at once.  An id no
 * endpoint answers to any more plays on the default.  miniaudio always plays on the default.
 *
 * AIL_ex_enumerate_output_devices: the active render endpoints, `index` from 0 until it answers 0,
 * each one's id (ASCII, what AIL_ex_set_output_device takes) and its name as UTF-16, both cut to
 * fit and terminated.  miniaudio lists none.
 *
 * XAudio2 keeps playing through a lost device: a critical error, the default endpoint changing
 * under a game on the default, or an endpoint arriving while there is none, and the engine, its
 * mastering voice, the room and every voice are made again.  What was playing ends (its EOS
 * callback comes as if it had played out), the streams carry on from their decoders.  With no
 * endpoint at all AIL_quick_startup still succeeds: a sample started then plays in silence for
 * its own length and ends, a stream likewise, until an endpoint comes. */
void      AILCALL AIL_ex_set_output_device(const char *id);
S32       AILCALL AIL_ex_enumerate_output_devices(S32 index, char *id, S32 id_bytes, U16 *name, S32 name_chars);

/* ---- file format helpers ---------------------------------------------- */
S32       AILCALL AIL_WAV_info(const void *data, AILSOUNDINFO *info);
S32       AILCALL AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, U32 *outsize);
void      AILCALL AIL_mem_free_lock(void *ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Not Miles: the distance curve both backends play a 3D sample at and the one
   MilesAudioManager::getEffectiveVolume culls with, kept in one place so the two cannot drift.  Full
   inside min_dist, then min_dist / distance (Miles' rolloff), or a straight line to zero when linear
   is set, and silent from max_dist on. */
static inline F32 AIL_ex_3D_distance_gain(F32 distance, F32 min_dist, F32 max_dist, S32 linear)
{
	if (distance >= max_dist)
		return 0.0f;
	if (distance <= min_dist)
		return 1.0f;
	if (linear && max_dist > min_dist)
		return 1.0f - (distance - min_dist) / (max_dist - min_dist);
	return min_dist / distance;
}

/* Not Miles, and like the distance curve kept here so the two backends cannot drift: the rest of a
   3D sample's sound.  Both play the low-pass below as a one-pole filter after the voice's resampler,
   at the output rate, which is where XAudio2 runs a source voice's filter. */
#define AIL_EX_OPEN_CUTOFF 20000.0f

/* The cutoff in Hz.  Occlusion is EA's LowPassCutoff, which MilesAudioManager hands down as 1 - cutoff
   for a sound off screen: 20 kHz times (1 - occlusion) squared, 5 kHz for the shipped 50%, and open
   for anything on screen.  Both backends run the cutoff through two one-pole stages, 12 dB an octave. */
static inline F32 AIL_ex_3D_cutoff(F32 occlusion)
{
	const F32 open = 1.0f - occlusion;
	return AIL_EX_OPEN_CUTOFF * open * open;
}

/* y += a * (x - y) for a cutoff at a rate: XAudio2CutoffFrequencyToOnePoleCoefficient, held at 1 (no
   filter at all) for an open cutoff and from half the rate up, where that helper turns round and
   closes again. */
static inline F32 AIL_ex_one_pole_coefficient(F32 cutoff, F32 rate)
{
	if (cutoff >= AIL_EX_OPEN_CUTOFF || cutoff * 2.0f >= rate)
		return 1.0f;
	const F32 k = 1.0f - 2.0f * cutoff / rate;
	return 1.0f - k * k;
}

/* The pitch ratio of a source moving at (vx, vy, vz), (dx, dy, dz) from the listener.  The listener
   counts as standing still: it is the camera, and a scroll would bend every sound on the map at once.
   The speed towards the listener is held to half the speed of sound either way, a ratio of 2/3 to 2. */
static inline F32 AIL_ex_doppler_ratio(F32 dx, F32 dy, F32 dz, F32 vx, F32 vy, F32 vz, F32 speed_of_sound)
{
	const F32 distance = sqrtf(dx * dx + dy * dy + dz * dz);
	if (speed_of_sound <= 0.0f || distance < 0.0001f)
		return 1.0f;
	F32 approach = -(vx * dx + vy * dy + vz * dz) / distance;
	const F32 limit = 0.5f * speed_of_sound;
	if (approach > limit) approach = limit;
	if (approach < -limit) approach = -limit;
	return speed_of_sound / (speed_of_sound - approach);
}

/* Where (dx, dy, dz) lies round the listener, in the plane of its face and right (up x face) axes,
   which is the plane the camera turns in: side -1 full left to 1 full right, front 1 ahead to -1
   behind, side^2 + front^2 = 1.  A sound straight above or below the listener is ahead.  Headphones
   and the surround pan read it; the stereo pan keeps its own projection, which counts height too. */
static inline void AIL_ex_3D_bearing(F32 dx, F32 dy, F32 dz, F32 faceX, F32 faceY, F32 faceZ,
	F32 upX, F32 upY, F32 upZ, F32 *side, F32 *front)
{
	const F32 rightX = upY * faceZ - upZ * faceY;
	const F32 rightY = upZ * faceX - upX * faceZ;
	const F32 rightZ = upX * faceY - upY * faceX;
	const F32 rightLength = sqrtf(rightX * rightX + rightY * rightY + rightZ * rightZ);
	const F32 faceLength = sqrtf(faceX * faceX + faceY * faceY + faceZ * faceZ);
	F32 across, along, length;
	*side = 0.0f;
	*front = 1.0f;
	if (rightLength < 0.0001f || faceLength < 0.0001f)
		return;
	across = (dx * rightX + dy * rightY + dz * rightZ) / rightLength;
	along = (dx * faceX + dy * faceY + dz * faceZ) / faceLength;
	length = sqrtf(across * across + along * along);
	if (length < 0.0001f)
		return;
	*side = across / length;
	*front = along / length;
}

/* Headphones (AIL_3D_HEADPHONE), kept here so both backends put a 3D sample in the same place: the
   sound set round the head by the three cues a head gives, with no measured responses.  The far ear
   hears it later, by Woodworth's spherical head of 8.75 cm, 0.66 ms at the side; quieter, 5 dB at the
   side, with the near ear lifted so the power holds; and duller, a one-pole low-pass falling to
   2.2 kHz at the side for the head's shadow.  Behind, both ears are dulled a little, to 8 kHz
   straight behind, the one cue left to tell a sound there from the same sound ahead.  Ahead both
   ears take 0.707, where the stereo pan puts a centred sound.  A sample's channels are summed into
   the one signal both ears hear, as the stereo pan sums them. */
#define AIL_EX_HRTF_LINE 128				/* frames of delay line: 0.66 ms at 192 kHz is 126 */
#define AIL_EX_HRTF_HEAD_SECONDS 0.000255f	/* the head's radius over the speed of sound, 0.0875 / 343 */
#define AIL_EX_HRTF_CENTRE 0.7071f
#define AIL_EX_HRTF_NEAR_LIFT 0.3f
#define AIL_EX_HRTF_FAR_DROP 0.45f
#define AIL_EX_HRTF_SHADOW 8.0f				/* 20 kHz / (1 + 8 side^2): 2.2 kHz at the side */
#define AIL_EX_HRTF_BEHIND 1.5f				/* 20 kHz / (1 + 1.5 behind^2): 8 kHz straight behind */
#define AIL_EX_HRTF_DENORMAL_GUARD 1e-18f

/* What each ear does to a sound from one bearing, left ear first. */
typedef struct
{
	F32 delay[2];		/* frames */
	F32 gain[2];
	F32 coefficient[2];	/* AIL_ex_one_pole_coefficient's */
} AIL_EX_HRTF_TARGET;

/* One sample's ears: the delay line both read, each ear's low-pass, and the cues the last frame used. */
typedef struct
{
	F32 line[AIL_EX_HRTF_LINE];
	S32 at;
	F32 store[2];
	AIL_EX_HRTF_TARGET now;
	S32 primed;
} AIL_EX_HRTF;

static inline void AIL_ex_hrtf_target(F32 side, F32 front, F32 rate, AIL_EX_HRTF_TARGET *target)
{
	const F32 lateral = fabsf(side) < 1.0f ? fabsf(side) : 1.0f;
	const F32 behind = front < 0.0f ? -front : 0.0f;
	const F32 limit = (F32)(AIL_EX_HRTF_LINE - 2);
	const F32 itd = AIL_EX_HRTF_HEAD_SECONDS * (asinf(lateral) + lateral) * rate;
	const F32 rear = AIL_EX_OPEN_CUTOFF / (1.0f + AIL_EX_HRTF_BEHIND * behind * behind);
	const F32 shadow = AIL_EX_OPEN_CUTOFF / (1.0f + AIL_EX_HRTF_SHADOW * lateral * lateral);
	const S32 distant = side < 0.0f ? 1 : 0;	/* a sound on the left has the right ear away from it */
	target->delay[distant] = itd < limit ? itd : limit;
	target->delay[1 - distant] = 0.0f;
	target->gain[distant] = AIL_EX_HRTF_CENTRE * (1.0f - AIL_EX_HRTF_FAR_DROP * lateral);
	target->gain[1 - distant] = AIL_EX_HRTF_CENTRE * (1.0f + AIL_EX_HRTF_NEAR_LIFT * lateral);
	target->coefficient[distant] = AIL_ex_one_pole_coefficient(shadow < rear ? shadow : rear, rate);
	target->coefficient[1 - distant] = AIL_ex_one_pole_coefficient(rear, rate);
}

static inline void AIL_ex_hrtf_reset(AIL_EX_HRTF *head)
{
	S32 i;
	for (i = 0; i < AIL_EX_HRTF_LINE; ++i)
		head->line[i] = 0.0f;
	head->at = 0;
	head->store[0] = head->store[1] = 0.0f;
	head->primed = 0;
}

/* `frames` of the summed signal into interleaved stereo, written rather than added, each cue moving in
   a straight line from the last pass's to `target` so a sound going round the head neither clicks nor
   steps.  The delay is read between two frames of the line. */
static inline void AIL_ex_hrtf_process(AIL_EX_HRTF *head, const AIL_EX_HRTF_TARGET *target,
	const F32 *mono, F32 *stereo, S32 frames)
{
	const F32 step = frames > 0 ? 1.0f / (F32)frames : 0.0f;
	S32 frame, ear;
	if (!head->primed) {
		head->now = *target;
		head->primed = 1;
	}
	for (frame = 0; frame < frames; ++frame) {
		const F32 t = (F32)(frame + 1) * step;
		head->line[head->at] = mono[frame];
		for (ear = 0; ear < 2; ++ear) {
			const F32 delay = head->now.delay[ear] + (target->delay[ear] - head->now.delay[ear]) * t;
			const F32 gain = head->now.gain[ear] + (target->gain[ear] - head->now.gain[ear]) * t;
			const F32 coefficient = head->now.coefficient[ear] + (target->coefficient[ear] - head->now.coefficient[ear]) * t;
			const S32 whole = (S32)delay;
			const F32 part = delay - (F32)whole;
			const F32 later = head->line[(head->at - whole + AIL_EX_HRTF_LINE) % AIL_EX_HRTF_LINE];
			const F32 earlier = head->line[(head->at - whole - 1 + AIL_EX_HRTF_LINE) % AIL_EX_HRTF_LINE];
			const F32 heard = later + (earlier - later) * part;
			head->store[ear] += coefficient * (heard - head->store[ear]) + AIL_EX_HRTF_DENORMAL_GUARD;
			stereo[frame * 2 + ear] = gain * head->store[ear];
		}
		head->at = (head->at + 1) % AIL_EX_HRTF_LINE;
	}
	head->now = *target;
}

/* The master limiter both backends run last.  A frame comes out AIL_EX_LIMITER_LOOKAHEAD - 1 frames
   late, at a gain already down to what the loudest frame of the window needs, so no sample passes the
   ceiling and none is cut flat.  The gain falls along the average of the window's floors, a fade of
   two milliseconds rather than a step, and comes back over AIL_EX_LIMITER_RELEASE_SECONDS. */
#define AIL_EX_LIMITER_LOOKAHEAD 96
#define AIL_EX_LIMITER_MAX_CHANNELS 16
#define AIL_EX_LIMITER_CEILING 0.89f	/* -1 dBFS */
#define AIL_EX_LIMITER_RELEASE_SECONDS 0.3f

typedef struct
{
	F32 delayed[AIL_EX_LIMITER_LOOKAHEAD * AIL_EX_LIMITER_MAX_CHANNELS];
	F32 needs[AIL_EX_LIMITER_LOOKAHEAD];	/* the gain each frame of the window asks for */
	F32 floors[AIL_EX_LIMITER_LOOKAHEAD];	/* the least of the window's needs, as it stood at each frame */
	double floorSum;
	F32 gain;
	F32 release;
	S32 channels;
	S32 at;
} AIL_EX_LIMITER;

static inline void AIL_ex_limiter_init(AIL_EX_LIMITER *limiter, S32 channels, F32 rate)
{
	S32 i;
	for (i = 0; i < AIL_EX_LIMITER_LOOKAHEAD * AIL_EX_LIMITER_MAX_CHANNELS; ++i)
		limiter->delayed[i] = 0.0f;
	for (i = 0; i < AIL_EX_LIMITER_LOOKAHEAD; ++i) {
		limiter->needs[i] = 1.0f;
		limiter->floors[i] = 1.0f;
	}
	limiter->floorSum = AIL_EX_LIMITER_LOOKAHEAD;
	limiter->gain = 1.0f;
	limiter->release = 1.0f - expf(-1.0f / (AIL_EX_LIMITER_RELEASE_SECONDS * rate));
	limiter->channels = channels;
	limiter->at = 0;
}

/* In place, `count` interleaved frames of the channel count the limiter was made for. */
static inline void AIL_ex_limit(AIL_EX_LIMITER *limiter, F32 *frames, S32 count)
{
	const S32 channels = limiter->channels;
	S32 frame, channel, i;
	for (frame = 0; frame < count; ++frame) {
		F32 *sample = frames + frame * channels;
		const S32 at = limiter->at;
		const S32 oldest = (at + 1) % AIL_EX_LIMITER_LOOKAHEAD;
		F32 peak = 0.0f;
		F32 floor = 1.0f;
		F32 target;
		for (channel = 0; channel < channels; ++channel) {
			const F32 level = fabsf(sample[channel]);
			if (level > peak) peak = level;
		}
		limiter->needs[at] = peak > AIL_EX_LIMITER_CEILING ? AIL_EX_LIMITER_CEILING / peak : 1.0f;
		for (i = 0; i < AIL_EX_LIMITER_LOOKAHEAD; ++i) {
			if (limiter->needs[i] < floor) floor = limiter->needs[i];
		}
		limiter->floorSum += floor - limiter->floors[at];
		limiter->floors[at] = floor;
		target = (F32)(limiter->floorSum / AIL_EX_LIMITER_LOOKAHEAD);
		limiter->gain = target < limiter->gain ? target : limiter->gain + (target - limiter->gain) * limiter->release;
		for (channel = 0; channel < channels; ++channel) {
			limiter->delayed[at * channels + channel] = sample[channel];
			sample[channel] = limiter->delayed[oldest * channels + channel] * limiter->gain;
		}
		limiter->at = oldest;
	}
}

#endif /* MSS_H */
