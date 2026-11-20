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

// PERF1's timing aid.  ZH_GPU_TIMING="delay,count" measures the `count` frames that start `delay` seconds
// after the first Present - a stretch after loading whatever the resolution - and prints, as soon as the
// last of them is in (a run ended by a timeout still reports), the distribution of each over them:
//   - frame: the wall time from one Present to the next, everything the game did in between;
//   - draws: the draws recorded in it;
//   - device draw: the CPU time inside Gpu_Draw, recording those draws (state, programs, copies, staging);
//   - device flush: mid-frame flushes (a full batch, a read-back) recording and submitting the batch;
//   - device present: Present recording and submitting the frame's last batch, and the gamma pass,
//     including the swapchain wait;
//   - swapchain wait: SDL_WaitAndAcquireGPUSwapchainTexture's, which includes display pacing when VSync
//     is on and GPU backpressure in either mode; work: the frame without that wait;
//   - not paced: frames presented to a window SDL called hidden, occluded or minimised, and frames whose
//     swapchain gave no drawable.  macOS hands a hidden or occluded window drawables and does not wait for
//     vsync, so a run full of them measures the game unpaced - a -hiddenwindow run, and a MacBook Air's
//     locked session, read as 119 fps on a 60 Hz panel - and the counts say so;
//   - GPU (ZH_GPU_TIMING_SYNC=1 only): every submit waits for its fence, and the waits add up.  That
//     serialises CPU and GPU, so a SYNC run's frame times are not the game's: it measures the GPU.
// What is left of the frame after the device's parts is the engine's own CPU time (and, unsynced, any
// wait for the GPU inside SDL's swapchain acquire).

#include "PosixDevice9.h"
#include "SdlCreationLog.h"
#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <mutex>
#include <string>

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace {

struct TimingState
{
	bool Asked;
	bool Sync;
	double Delay;
	unsigned int Count;
	Uint64 FirstPresent;
	Uint64 LastPresent;
	bool Reported;
	std::vector<double> Frame, Work, DrawMs, FlushMs, PresentMs, AcquireMs, OffscreenMs, FenceMs, Draws, Flushes;
	unsigned int NotVisible;	///< measured frames presented to a window that was not visible
	unsigned int NotShown;		///< measured frames with no drawable
	bool Offscreen;
};

TimingState &timing_state()
{
	static TimingState state;
	static bool made = false;
	if (!made) {
		made = true;
		const char *asked = getenv("ZH_GPU_TIMING");
		state.Asked = asked != NULL && sscanf(asked, "%lf,%u", &state.Delay, &state.Count) == 2 && state.Count > 0;
		const char *sync = getenv("ZH_GPU_TIMING_SYNC");
		state.Sync = state.Asked && sync != NULL && sync[0] == '1';
		state.FirstPresent = 0;
		state.LastPresent = 0;
		state.Reported = false;
		state.NotVisible = 0;
		state.NotShown = 0;
		state.Offscreen = false;
	}
	return state;
}

void report(const char *what, std::vector<double> values, const char *unit)
{
	if (values.empty()) {
		return;
	}
	std::sort(values.begin(), values.end());
	double sum = 0.0;
	for (size_t i = 0; i < values.size(); ++i) sum += values[i];
	const size_t n = values.size();
	fprintf(stderr, "PosixDevice9 timing: %-15s mean %8.3f  p50 %8.3f  p95 %8.3f  p99 %8.3f  worst %8.3f %s\n", what, sum / n,
		values[n / 2], values[std::min(n - 1, (size_t)(n * 0.95))], values[std::min(n - 1, (size_t)(n * 0.99))], values[n - 1],
		unit);
}

}  // namespace

bool PosixDevice9::Timing_Is_Asked()
{
	return timing_state().Asked;
}

void PosixDevice9::Timing_Frame_Start()
{
	if (Gpu != NULL) {
		Gpu->Serialize_Submits(timing_state().Sync);
	}
}

void PosixDevice9::Timing_Present(double present_ms, unsigned int draws)
{
	TimingState &state = timing_state();
	const Uint64 now = SDL_GetTicksNS();
	double flush_ms = 0.0, fence_ms = 0.0, acquire_ms = 0.0, offscreen_ms = 0.0;
	unsigned int flushes = 0, not_visible = 0, not_shown = 0;
	Gpu->Take_Timing(flush_ms, fence_ms, flushes, acquire_ms, offscreen_ms, not_visible, not_shown);
	state.Offscreen = Gpu->Offscreen_Presents();
	if (state.FirstPresent == 0) {
		state.FirstPresent = now;
	}
	if (state.LastPresent != 0 && (double)(now - state.LastPresent) / 1.0e6 > 50.0) {
		// A long frame, split into the device's parts: what is left is the engine's.
		const double frame_ms = (double)(now - state.LastPresent) / 1.0e6;
		fprintf(stderr, "PosixDevice9 timing: LONG FRAME %.1f ms at %.1f s: device draw %.2f, flushes %u (%.2f ms), present %.2f"
			" (swapchain wait %.2f, offscreen wait %.2f), %u draws; the engine's own %.1f ms\n", frame_ms,
			(double)(state.LastPresent - state.FirstPresent) / 1.0e9, TimingDrawMs, flushes, flush_ms, present_ms, acquire_ms,
			offscreen_ms, draws, frame_ms - TimingDrawMs - flush_ms - present_ms);
	}
	const bool measuring = (double)(now - state.FirstPresent) / 1.0e9 >= state.Delay && state.Frame.size() < state.Count;
	if (measuring && state.LastPresent != 0) {
		const double frame_ms = (double)(now - state.LastPresent) / 1.0e6;
		state.Frame.push_back(frame_ms);
		state.Work.push_back(frame_ms - acquire_ms - offscreen_ms);
		state.AcquireMs.push_back(acquire_ms);
		state.OffscreenMs.push_back(offscreen_ms);
		state.Draws.push_back((double)draws);
		state.DrawMs.push_back(TimingDrawMs);
		state.FlushMs.push_back(flush_ms);
		state.Flushes.push_back((double)flushes);
		state.PresentMs.push_back(present_ms);
		state.FenceMs.push_back(fence_ms);
		if (not_visible > 0) {
			++state.NotVisible;
		}
		if (not_shown > 0) {
			++state.NotShown;
		}
	}
	state.LastPresent = now;
	TimingDrawMs = 0.0;
	if (!state.Reported && state.Frame.size() == state.Count) {
		Timing_Report();
	}
}

void PosixDevice9::Timing_Report()
{
	TimingState &state = timing_state();
	if (!state.Asked || state.Reported) {
		return;
	}
	if (state.FirstPresent == 0) {
		// A device made and replaced before any frame (the engine does that at some sizes, e.g. 3024x1964
		// offscreen on an M3 Pro) has nothing to report, and must not use up the one report.
		return;
	}
	state.Reported = true;
	fprintf(stderr, "PosixDevice9 timing: %zu frames from %.0f s after the first Present%s%s\n", state.Frame.size(),
		state.Delay, state.Frame.size() < state.Count ? " (the run ended before the window filled)" : "",
		state.Sync ? ", SERIALISED (every submit waits for its fence: GPU time, not frame rate)" : "");
	report("frame", state.Frame, "ms");
	report("work (no vsync)", state.Work, "ms");
	if (state.Offscreen) {
		// -offscreen: there is no swapchain; its wait is the GPU's two frames in flight and the pacer.
		fprintf(stderr, "PosixDevice9 timing: OFFSCREEN (no swapchain: no vsync, no drawable; see ZH_OFFSCREEN_HZ)\n");
		report("offscreen wait", state.OffscreenMs, "ms");
	} else {
		report("swapchain wait", state.AcquireMs, "ms");
		fprintf(stderr, "PosixDevice9 timing: %u of %zu frames presented to a window that was not visible, %u with no"
			" drawable%s\n", state.NotVisible, state.Frame.size(), state.NotShown,
			state.NotVisible == 0 && state.NotShown == 0 ? ""
				: " (hidden, occluded or minimised: the display did not pace them, so the times above are not its pacing)");
	}
	report("draws", state.Draws, "");
	report("device draw", state.DrawMs, "ms");
	report("device flush", state.FlushMs, "ms");
	report("flushes", state.Flushes, "");
	report("device present", state.PresentMs, "ms");
	if (state.Sync) {
		report("GPU (fences)", state.FenceMs, "ms");
	}
}

bool Sdl_Creation_Log_Asked()
{
	static const bool asked = getenv("ZH_GPU_CREATION_LOG") != NULL;
	return asked;
}

double Sdl_Now_Ms()
{
	return (double)SDL_GetTicksNS() / 1.0e6;
}

namespace {
struct CreationLogBuffer
{
	std::mutex Lock;		// the lines: appended from any thread
	std::mutex Writing;		// one writer at a time: Present, and the exit handler
	std::string Lines;
	std::string Spare;		// what a flush writes, outside Lock; its capacity is kept for the next
	bool AtExit = false;
};
/// Never destroyed: the exit handler that flushes it can run after static destructors.
CreationLogBuffer &creation_log_buffer()
{
	static CreationLogBuffer *buffer = new CreationLogBuffer;
	return *buffer;
}
/// A megabyte kept is written at once, for a run that stops presenting.
const size_t CREATION_LOG_KEPT_MAX = 1u << 20;

void flush_at_exit()
{
	Sdl_Creation_Log_Flush();
}
}

void Sdl_Creation_Log_Line(const char *line)
{
	CreationLogBuffer &buffer = creation_log_buffer();
	bool full;
	{
		std::lock_guard<std::mutex> guard(buffer.Lock);
		if (!buffer.AtExit) {
			buffer.AtExit = true;
			atexit(flush_at_exit);
		}
		buffer.Lines += line;
		buffer.Lines += '\n';
		full = buffer.Lines.size() > CREATION_LOG_KEPT_MAX;
	}
	if (full) {
		Sdl_Creation_Log_Flush();
	}
}

void Sdl_Creation_Log(const char *what, double started_ms, double took_ms, const char *detail)
{
	char line[256];
	snprintf(line, sizeof(line), "PosixDevice9 create: t %10.1f ms  %-9s %8.2f ms  %s", started_ms, what, took_ms, detail);
	Sdl_Creation_Log_Line(line);
}

void Sdl_Creation_Log_Trace(const char *what)
{
	void *frames[12];
	const int count = backtrace(frames, 12);
	std::string line = std::string("PosixDevice9 create: trace ") + what + ":";
	for (int i = 1; i < count; ++i) {
		Dl_info info;
		const char *name = "?";
		char *demangled = NULL;
		if (dladdr(frames[i], &info) != 0 && info.dli_sname != NULL) {
			int status = 0;
			demangled = abi::__cxa_demangle(info.dli_sname, NULL, NULL, &status);
			name = (demangled != NULL && status == 0) ? demangled : info.dli_sname;
		}
		// The function's name without its parameters: the line is for reading.
		std::string shown(name);
		const size_t parameters = shown.find('(');
		if (parameters != std::string::npos) {
			shown.erase(parameters);
		}
		line += (i == 1) ? " " : " <- ";
		line += shown;
		free(demangled);
	}
	Sdl_Creation_Log_Line(line.c_str());
}

void Sdl_Creation_Log_Flush()
{
	CreationLogBuffer &buffer = creation_log_buffer();
	std::lock_guard<std::mutex> writing_guard(buffer.Writing);
	{
		std::lock_guard<std::mutex> guard(buffer.Lock);
		buffer.Spare.swap(buffer.Lines);
	}
	if (buffer.Spare.empty()) {
		return;
	}
	const double writing = Sdl_Now_Ms();
	fwrite(buffer.Spare.data(), 1, buffer.Spare.size(), stderr);
	fflush(stderr);
	const double wrote = Sdl_Now_Ms() - writing;
	const size_t bytes = buffer.Spare.size();
	buffer.Spare.clear();
	if (wrote > 5.0) {
		// The write can still block (PERF1): now once a present, and said, so a long frame can be read.
		char line[128];
		snprintf(line, sizeof(line), "PosixDevice9 create: t %10.1f ms  logflush  %8.2f ms  (%zu bytes)", writing, wrote, bytes);
		Sdl_Creation_Log_Line(line);
	}
}
