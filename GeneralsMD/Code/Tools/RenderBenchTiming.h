// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <time.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

namespace RenderBench {

// Set the same policy in both short-lived benchmark processes. This does not
// change the game's scheduling or any system-wide setting.
inline const char *Configure_Thread()
{
#if defined(__APPLE__)
	const int error = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
	if (error) {
		std::fprintf(stderr, "benchmark: cannot set thread QoS (%d)\n", error);
		std::exit(1);
	}
	return "user_initiated";
#else
	return "default";
#endif
}

// Unlike wall time, this excludes time when this thread was descheduled or
// waiting. Keep GPU-completion wall time as a separate measurement.
inline uint64_t Thread_Nanoseconds()
{
	struct timespec value;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) {
		std::fprintf(stderr, "benchmark: thread CPU clock unavailable\n");
		std::exit(1);
	}
	return uint64_t(value.tv_sec) * 1000000000ull + uint64_t(value.tv_nsec);
}

} // namespace RenderBench
