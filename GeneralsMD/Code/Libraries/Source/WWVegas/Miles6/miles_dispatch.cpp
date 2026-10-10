/*
 * The Miles surface's plain names on Windows, each forwarding to one of two backends linked beside
 * each other: miles_xaudio2.cpp (XA_) for everything a player hears, miles_miniaudio.cpp (MA_) for a
 * -wav run.  XAudio2 mixes on the device's clock and has no way to be stepped, so a capture made
 * through it is as long as the device played, glitches and dropped passes and all, and needs a
 * device to exist.  miniaudio's mix is this port's own code, so the logic clock can pull it.
 *
 * The choice is made once, before AIL_startup: AIL_ex_offline_mix selects miniaudio and nothing
 * else does.  Off Windows miniaudio is the only backend and this file is not built.  Bink's movie
 * sound opens its own XAudio2 on Windows and goes through neither.
 */

#include "MSS/MSS.h"

#define MSS_API_LIST \
	MSS_API(S32, AIL_startup, (void), ()) \
	MSS_API(void, AIL_shutdown, (void), ()) \
	MSS_API(S32, AIL_quick_startup, (S32 use_digital, S32 use_MIDI, U32 output_rate, S32 output_bits, S32 output_channels), (use_digital, use_MIDI, output_rate, output_bits, output_channels)) \
	MSS_API(void, AIL_quick_handles, (HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls), (pdig, pmdi, pdls)) \
	MSS_API(char *, AIL_set_redist_directory, (const char *dir), (dir)) \
	MSS_API(void, AIL_lock, (void), ()) \
	MSS_API(void, AIL_unlock, (void), ()) \
	MSS_API(S32, AIL_get_timer_highest_delay, (void), ()) \
	MSS_API(void, AIL_set_file_callbacks, (AILFILEOPENCB opencb, AILFILECLOSECB closecb, AILFILESEEKCB seekcb, AILFILEREADCB readcb), (opencb, closecb, seekcb, readcb)) \
	MSS_API(S32, AIL_enumerate_3D_providers, (HPROENUM *next, HPROVIDER *dest, char **name), (next, dest, name)) \
	MSS_API(S32, AIL_open_3D_provider, (HPROVIDER lib), (lib)) \
	MSS_API(void, AIL_close_3D_provider, (HPROVIDER lib), (lib)) \
	MSS_API(S32, AIL_enumerate_filters, (HPROENUM *next, HPROVIDER *dest, char **name), (next, dest, name)) \
	MSS_API(void, AIL_set_3D_speaker_type, (HPROVIDER lib, S32 speaker_type), (lib, speaker_type)) \
	MSS_API(HPROVIDER, AIL_set_sample_processor, (HSAMPLE sample, S32 pipeline_stage, HPROVIDER provider), (sample, pipeline_stage, provider)) \
	MSS_API(void, AIL_set_filter_sample_preference, (HSAMPLE sample, const char *name, const void *val), (sample, name, val)) \
	MSS_API(HSAMPLE, AIL_allocate_sample_handle, (HDIGDRIVER dig), (dig)) \
	MSS_API(void, AIL_release_sample_handle, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_init_sample, (HSAMPLE sample), (sample)) \
	MSS_API(S32, AIL_set_sample_file, (HSAMPLE sample, const void *file_image, S32 block), (sample, file_image, block)) \
	MSS_API(void, AIL_start_sample, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_stop_sample, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_resume_sample, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_end_sample, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_set_sample_volume_pan, (HSAMPLE sample, F32 volume, F32 pan), (sample, volume, pan)) \
	MSS_API(void, AIL_sample_volume_pan, (HSAMPLE sample, F32 *volume, F32 *pan), (sample, volume, pan)) \
	MSS_API(void, AIL_set_sample_playback_rate, (HSAMPLE sample, S32 playback_rate), (sample, playback_rate)) \
	MSS_API(S32, AIL_sample_playback_rate, (HSAMPLE sample), (sample)) \
	MSS_API(void, AIL_set_sample_user_data, (HSAMPLE sample, U32 index, S32 value), (sample, index, value)) \
	MSS_API(S32, AIL_sample_user_data, (HSAMPLE sample, U32 index), (sample, index)) \
	MSS_API(AILSAMPLECB, AIL_register_EOS_callback, (HSAMPLE sample, AILSAMPLECB EOS), (sample, EOS)) \
	MSS_API(void, AIL_get_DirectSound_info, (HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB), (sample, lplpDS, lplpDSB)) \
	MSS_API(H3DSAMPLE, AIL_allocate_3D_sample_handle, (HPROVIDER lib), (lib)) \
	MSS_API(void, AIL_release_3D_sample_handle, (H3DSAMPLE sample), (sample)) \
	MSS_API(S32, AIL_set_3D_sample_file, (H3DSAMPLE sample, const void *file_image), (sample, file_image)) \
	MSS_API(void, AIL_start_3D_sample, (H3DSAMPLE sample), (sample)) \
	MSS_API(void, AIL_stop_3D_sample, (H3DSAMPLE sample), (sample)) \
	MSS_API(void, AIL_resume_3D_sample, (H3DSAMPLE sample), (sample)) \
	MSS_API(void, AIL_end_3D_sample, (H3DSAMPLE sample), (sample)) \
	MSS_API(void, AIL_set_3D_sample_volume, (H3DSAMPLE sample, F32 volume), (sample, volume)) \
	MSS_API(void, AIL_set_3D_sample_playback_rate, (H3DSAMPLE sample, S32 playback_rate), (sample, playback_rate)) \
	MSS_API(S32, AIL_3D_sample_playback_rate, (H3DSAMPLE sample), (sample)) \
	MSS_API(void, AIL_set_3D_sample_distances, (H3DSAMPLE sample, F32 max_dist, F32 min_dist), (sample, max_dist, min_dist)) \
	MSS_API(void, AIL_set_3D_sample_occlusion, (H3DSAMPLE sample, F32 occlusion), (sample, occlusion)) \
	MSS_API(void, AIL_set_3D_user_data, (H3DPOBJECT obj, U32 index, S32 value), (obj, index, value)) \
	MSS_API(S32, AIL_3D_user_data, (H3DPOBJECT obj, U32 index), (obj, index)) \
	MSS_API(AIL3DSAMPLECB, AIL_register_3D_EOS_callback, (H3DSAMPLE sample, AIL3DSAMPLECB EOS), (sample, EOS)) \
	MSS_API(H3DPOBJECT, AIL_open_3D_listener, (HPROVIDER lib), (lib)) \
	MSS_API(void, AIL_close_3D_listener, (H3DPOBJECT listener), (listener)) \
	MSS_API(void, AIL_set_3D_position, (H3DPOBJECT obj, F32 X, F32 Y, F32 Z), (obj, X, Y, Z)) \
	MSS_API(void, AIL_set_3D_orientation, (H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up), (obj, X_face, Y_face, Z_face, X_up, Y_up, Z_up)) \
	MSS_API(void, AIL_set_3D_velocity_vector, (H3DPOBJECT obj, F32 dX, F32 dY, F32 dZ), (obj, dX, dY, dZ)) \
	MSS_API(HSTREAM, AIL_open_stream, (HDIGDRIVER dig, const char *filename, S32 stream_mem), (dig, filename, stream_mem)) \
	MSS_API(void, AIL_close_stream, (HSTREAM stream), (stream)) \
	MSS_API(void, AIL_start_stream, (HSTREAM stream), (stream)) \
	MSS_API(void, AIL_pause_stream, (HSTREAM stream, S32 onoff), (stream, onoff)) \
	MSS_API(void, AIL_set_stream_loop_count, (HSTREAM stream, S32 count), (stream, count)) \
	MSS_API(S32, AIL_stream_loop_count, (HSTREAM stream), (stream)) \
	MSS_API(void, AIL_set_stream_volume_pan, (HSTREAM stream, F32 volume, F32 pan), (stream, volume, pan)) \
	MSS_API(void, AIL_stream_volume_pan, (HSTREAM stream, F32 *volume, F32 *pan), (stream, volume, pan)) \
	MSS_API(void, AIL_stream_ms_position, (HSTREAM stream, S32 *total_milliseconds, S32 *current_milliseconds), (stream, total_milliseconds, current_milliseconds)) \
	MSS_API(AILSTREAMCB, AIL_register_stream_callback, (HSTREAM stream, AILSTREAMCB callback), (stream, callback)) \
	MSS_API(HAUDIO, AIL_quick_load_and_play, (const char *filename, U32 loop_count, S32 wait_request), (filename, loop_count, wait_request)) \
	MSS_API(void, AIL_quick_unload, (HAUDIO audio), (audio)) \
	MSS_API(void, AIL_quick_set_volume, (HAUDIO audio, F32 volume, F32 extravol), (audio, volume, extravol)) \
	MSS_API(S32, AIL_ex_start_capture, (const char *pathname), (pathname)) \
	MSS_API(void, AIL_ex_stop_capture, (void), ()) \
	MSS_API(void, AIL_ex_set_3D_linear_falloff, (S32 linear), (linear)) \
	MSS_API(S32, AIL_WAV_info, (const void *data, AILSOUNDINFO *info), (data, info)) \
	MSS_API(S32, AIL_decompress_ADPCM, (const AILSOUNDINFO *info, void **outdata, U32 *outsize), (info, outdata, outsize)) \
	MSS_API(void, AIL_mem_free_lock, (void *ptr), (ptr))

extern "C" {

#define MSS_API(ret, name, params, args) ret AILCALL XA_##name params; ret AILCALL MA_##name params;
MSS_API_LIST
#undef MSS_API

void AILCALL MA_AIL_ex_offline_mix(void);
void AILCALL MA_AIL_ex_mix_to_frame(S32 logicFrame);
void AILCALL MA_AIL_ex_capture_length(S32 *frames, S32 *rate);

static bool g_offline = false;

#define MSS_API(ret, name, params, args) ret AILCALL name params { return g_offline ? MA_##name args : XA_##name args; }
MSS_API_LIST
#undef MSS_API

void AILCALL AIL_ex_offline_mix(void)
{
	g_offline = true;
	MA_AIL_ex_offline_mix();
}

void AILCALL AIL_ex_mix_to_frame(S32 logicFrame)
{
	if (g_offline) {
		MA_AIL_ex_mix_to_frame(logicFrame);
	}
}

void AILCALL AIL_ex_capture_length(S32 *frames, S32 *rate)
{
	if (g_offline) {
		MA_AIL_ex_capture_length(frames, rate);
		return;
	}
	*frames = 0;
	*rate = 0;
}

} // extern "C"
