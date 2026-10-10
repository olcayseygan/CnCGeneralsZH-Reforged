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
 * 4-5 are the 6.5 additions continuing the sequence. */
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
   device: the mix moves only when AIL_ex_mix_to_frame asks it to, logicFrame * rate / 30 frames from
   the start, so a capture holds exactly the sound of the logic frames it spans however fast or slow
   the run went.  Nothing is heard meanwhile.  AIL_ex_capture_length reports what the last finished
   capture wrote, in frames at its rate; the XAudio2 backend does not count and says zero. */
void      AILCALL AIL_ex_offline_mix(void);
void      AILCALL AIL_ex_mix_to_frame(S32 logicFrame);
void      AILCALL AIL_ex_capture_length(S32 *frames, S32 *rate);

/* ---- distance falloff -------------------------------------------------- */
/* Not Miles.  Nonzero makes every 3D sample fall off in a straight line from min_dist to max_dist
   instead of Miles' min_dist / distance; MilesAudioManager passes AudioSettings.ini's
   RangeVolumeFade, so what plays and what getEffectiveVolume culls follow one curve. */
void      AILCALL AIL_ex_set_3D_linear_falloff(S32 linear);

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

#endif /* MSS_H */
