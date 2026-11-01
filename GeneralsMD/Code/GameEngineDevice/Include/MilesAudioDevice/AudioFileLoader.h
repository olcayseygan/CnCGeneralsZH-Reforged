/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: AudioFileLoader.h
// The disk half of a sound's first play, on a thread of its own, so the frame that asked for the
// sound does not wait on a hard disk: 137 ms once in a two minute window on a quiet machine, 9806 ms
// beside a build on the same drive.  The main thread asks by name and looks again next pass; the
// thread reads the file through a function it was given and decompresses it.  Which files stay in
// memory and for how long is AudioFileCache's business and stays on the main thread, so all this
// shares with anyone is the job list under its own lock.  The engine is reached only through that
// function, so a test can hold it.

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <string.h>

#include "MSS/MSS.h"

class AudioFileLoader
{
public:
	/// A sound file as the mixer wants it: a WAV image of PCM.
	struct Loaded
	{
		void *file;			///< NULL when the file is missing or in a format the mixer does not play
		unsigned size;		///< bytes of file
		bool compressed;	///< made by AIL_decompress_ADPCM, and so freed by AIL_mem_free_lock
		AILSOUNDINFO info;
		float readMs;
		float decodeMs;
		float askedToDoneMs;
	};

	/// All of a file in a new[] buffer, or NULL.  Called on the thread.
	typedef char *(*ReadFile)(const char *name, unsigned *size);

	enum { LATE_BUCKETS = 6 };
	static float lateBucketMs(int bucket)
	{
		static const float limits[LATE_BUCKETS - 1] = { 5.0f, 16.7f, 33.3f, 100.0f, 500.0f };
		return limits[bucket];
	}

	/// What the thread has done since it started, for the log.
	struct Stats
	{
		unsigned files;
		double readMs, readWorstMs;
		double decodeMs, decodeWorstMs;
		double askedToDoneMs, askedToDoneWorstMs;
		unsigned doneWithin[LATE_BUCKETS];	///< files by asked to done, up to each of lateBucketMs and past the last
	};

	explicit AudioFileLoader(ReadFile read) :
		m_read(read),
		m_stopping(false),
		m_working(false),
		m_thread(&AudioFileLoader::run, this)
	{
		memset(&m_stats, 0, sizeof(m_stats));
	}

	/** Waits for the file in hand, if there is one, so nothing outlives the file system it reads. */
	~AudioFileLoader()
	{
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_stopping = true;
		}
		m_wake.notify_all();
		m_thread.join();
		for (Jobs::iterator it = m_jobs.begin(); it != m_jobs.end(); ++it) {
			if (it->second.done) {
				release(&it->second.loaded);
			}
		}
	}

	/** Is this file read and decoded?  The first ask queues it, and a false means ask again later.
		* An urgent one goes to the front.  `read` is for a file that wants another way to the disk than
		* the loader's own. */
	bool ready(const char *name, bool urgent = false, ReadFile read = NULL)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		Jobs::iterator it = m_jobs.find(name);
		if (it != m_jobs.end()) {
			return it->second.done;
		}
		Job &job = m_jobs[name];
		job.done = false;
		job.read = read != NULL ? read : m_read;
		job.askedAt = Clock::now();
		if (urgent) {
			m_queue.push_front(name);
		} else {
			m_queue.push_back(name);
		}
		m_wake.notify_one();
		return false;
	}

	/** How long a file asked for and not yet finished has been waited for, in milliseconds. */
	float waitedMs(const char *name)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		Jobs::iterator it = m_jobs.find(name);
		return it != m_jobs.end() && !it->second.done ? milliseconds(Clock::now() - it->second.askedAt) : 0.0f;
	}

	/** Hands over what ready said was there, which is then the caller's to release.  False if the
		* thread has not finished it. */
	bool take(const char *name, Loaded *loaded)
	{
		std::lock_guard<std::mutex> lock(m_lock);
		Jobs::iterator it = m_jobs.find(name);
		if (it == m_jobs.end() || !it->second.done) {
			return false;
		}
		*loaded = it->second.loaded;
		m_jobs.erase(it);
		return true;
	}

	/** Until every file asked for so far has been read.  A test's way of not racing the disk. */
	void waitUntilIdle()
	{
		std::unique_lock<std::mutex> lock(m_lock);
		m_idle.wait(lock, [this] { return m_queue.empty() && !m_working; });
	}

	Stats stats()
	{
		std::lock_guard<std::mutex> lock(m_lock);
		return m_stats;
	}

	/** Reading and decompressing one file, on whichever thread calls it. */
	static void load(ReadFile read, const char *name, Loaded *loaded)
	{
		memset(loaded, 0, sizeof(*loaded));
		const Clock::time_point start = Clock::now();
		unsigned size = 0;
		char *buffer = read(name, &size);
		const Clock::time_point afterRead = Clock::now();
		loaded->readMs = milliseconds(afterRead - start);
		if (buffer == NULL) {
			return;
		}

		AILSOUNDINFO info;
		memset(&info, 0, sizeof(info));
		AIL_WAV_info(buffer, &info);
		if (info.format == WAVE_FORMAT_IMA_ADPCM) {
			void *decompressed = NULL;
			U32 decompressedSize = 0;
			if (AIL_decompress_ADPCM(&info, &decompressed, &decompressedSize)) {
				loaded->file = decompressed;
				loaded->size = decompressedSize;
				loaded->compressed = true;
				loaded->info = info;
			}
			delete [] buffer;
		} else if (info.format == WAVE_FORMAT_PCM) {
			loaded->file = buffer;
			loaded->size = size;
			loaded->info = info;
		} else {
			delete [] buffer;
		}
		loaded->decodeMs = milliseconds(Clock::now() - afterRead);
	}

	static void release(Loaded *loaded)
	{
		if (loaded->compressed) {
			AIL_mem_free_lock(loaded->file);
		} else {
			delete [] (char *)loaded->file;
		}
		loaded->file = NULL;
	}

private:
	typedef std::chrono::steady_clock Clock;

	struct Job
	{
		bool done;
		ReadFile read;
		Clock::time_point askedAt;
		Loaded loaded;
	};
	typedef std::map<std::string, Job> Jobs;

	static float milliseconds(Clock::duration span)
	{
		return std::chrono::duration<float, std::milli>(span).count();
	}

	void run()
	{
		std::unique_lock<std::mutex> lock(m_lock);
		for (;;) {
			m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
			if (m_stopping) {
				return;
			}
			const std::string name = m_queue.front();
			m_queue.pop_front();
			m_working = true;
			const ReadFile read = m_jobs[name].read;

			lock.unlock();
			Loaded loaded;
			load(read, name.c_str(), &loaded);
			lock.lock();

			m_working = false;
			Job &job = m_jobs[name];
			job.loaded = loaded;
			job.done = true;
			job.loaded.askedToDoneMs = milliseconds(Clock::now() - job.askedAt);
			note(job.loaded);
			m_idle.notify_all();
		}
	}

	void note(const Loaded &loaded)
	{
		++m_stats.files;
		m_stats.readMs += loaded.readMs;
		m_stats.decodeMs += loaded.decodeMs;
		m_stats.askedToDoneMs += loaded.askedToDoneMs;
		if (loaded.readMs > m_stats.readWorstMs) {
			m_stats.readWorstMs = loaded.readMs;
		}
		if (loaded.decodeMs > m_stats.decodeWorstMs) {
			m_stats.decodeWorstMs = loaded.decodeMs;
		}
		if (loaded.askedToDoneMs > m_stats.askedToDoneWorstMs) {
			m_stats.askedToDoneWorstMs = loaded.askedToDoneMs;
		}
		int bucket = 0;
		while (bucket < LATE_BUCKETS - 1 && loaded.askedToDoneMs > lateBucketMs(bucket)) {
			++bucket;
		}
		++m_stats.doneWithin[bucket];
	}

	ReadFile m_read;
	std::mutex m_lock;
	std::condition_variable m_wake;
	std::condition_variable m_idle;
	std::deque<std::string> m_queue;
	Jobs m_jobs;
	Stats m_stats;
	bool m_stopping;
	bool m_working;
	std::thread m_thread;	///< last, so it starts with everything above it made
};
