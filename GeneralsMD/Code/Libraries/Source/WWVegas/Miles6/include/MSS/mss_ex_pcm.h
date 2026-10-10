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
 * A voice in the Miles mix that plays PCM its owner pushes: the movies' sound off Windows (V1).
 *
 * Not Miles 6.5 API - an extension, "AIL_ex_" like the capture, implemented only by
 * miles_miniaudio.cpp.  On Windows Bink/ffmpeg/bink_ffmpeg.cpp gives each movie an XAudio2 engine of
 * its own; off Windows the movie plays through C4's device instead of a second one, so it shares the
 * game's output, its lifetime and the capture the tests listen to.
 *
 * The voice mixes the way that XAudio2 source voice did, not the way a Miles sample does: its left
 * channel to the left speaker and its right to the right at the volume's gain, where a Miles voice
 * sums every channel into both at a pan's constant-power gains.  A mono stream goes to both speakers
 * at the full gain (no movie in the install is mono; V1).
 *
 * The engine lock serialises these calls with the rest of the Miles surface; the mix takes what was
 * queued under its own lock.
 */

#ifndef MSS_EX_PCM_H
#define MSS_EX_PCM_H

/* Windows builds miles_miniaudio.cpp too, for -wav (miles_dispatch.cpp), and it defines these under
   their plain names; the movies there keep their own XAudio2 engine and call none of them. */
#if defined(_WIN32) && !defined(MSS_BACKEND_PREFIX)
#error "mss_ex_pcm.h is miles_miniaudio's; on Windows the movies keep their own XAudio2 engine"
#endif

#include "MSS/MSS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _AIL_EX_PCM *HEXPCM;

/* A voice for 16-bit interleaved PCM at `rate` with 1 or 2 channels, playing as soon as anything is
   queued.  NULL when the digital driver is not open (no device, or -headless): the caller plays
   without sound, as a failed XAudio2Create did. */
HEXPCM AILCALL AIL_ex_open_pcm(S32 rate, S32 channels);
void   AILCALL AIL_ex_close_pcm(HEXPCM pcm);

/* Copies `frames` frames onto the end of the voice's queue.  Returns the frames taken. */
S32    AILCALL AIL_ex_queue_pcm(HEXPCM pcm, const S16 *data, S32 frames);
/* Frames queued and not yet mixed. */
S32    AILCALL AIL_ex_pcm_queued_frames(HEXPCM pcm);
/* Drops everything queued: XAudio2's FlushSourceBuffers. */
void   AILCALL AIL_ex_flush_pcm(HEXPCM pcm);
/* 0..1, applied to both channels: XAudio2's SetVolume. */
void   AILCALL AIL_ex_set_pcm_volume(HEXPCM pcm, F32 volume);

#ifdef __cplusplus
}
#endif

#endif /* MSS_EX_PCM_H */
