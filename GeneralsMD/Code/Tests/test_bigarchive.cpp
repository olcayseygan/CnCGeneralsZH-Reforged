// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms under GNU GPL section 7 apply: see LICENSE.md.
// Synthetic BIG indices exercise the native parser without retail data.
#include "test_harness.h"
#include "Common/ArchiveFile.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {
void bigEndian(std::string &bytes, unsigned value)
{
	for (int shift = 24; shift >= 0; shift -= 8) bytes += (char)(value >> shift);
}

std::string makeArchive(const std::string &name)
{
	std::string bytes("BIGF\0\0\0\0", 8);
	bigEndian(bytes, 1);
	bigEndian(bytes, 25 + (unsigned)name.size());
	bigEndian(bytes, 25 + (unsigned)name.size());
	bigEndian(bytes, 1);
	bytes += name;
	bytes += '\0';
	bytes += 'x';
	return bytes;
}

bool accepts(const std::string &bytes)
{
	static bool booted = false;
	if (!booted) { initMemoryManager(); booted = true; }
	const char *temp = getenv("TMPDIR");
	std::string pattern = std::string(temp ? temp : "/tmp") + "/bigarchive.XXXXXX";
	std::vector<char> path(pattern.begin(), pattern.end());
	path.push_back(0);
	int fd = mkstemp(path.data());
	CHECK(fd >= 0);
	if (fd < 0) return false;
	FILE *out = fdopen(fd, "wb");
	CHECK(out != NULL);
	if (!out) { close(fd); unlink(path.data()); return false; }
	CHECK_EQ(fwrite(bytes.data(), 1, bytes.size(), out), bytes.size());
	fclose(out);
	PosixLocalFileSystem local;
	LocalFileSystem *saved = TheLocalFileSystem;
	TheLocalFileSystem = &local;
	Win32BIGFileSystem parser;
	ArchiveFile *archive = parser.openArchiveFile(path.data());
	const bool accepted = archive != NULL;
	delete archive;
	TheLocalFileSystem = saved;
	unlink(path.data());
	return accepted;
}
}

TEST(bigarchive_accepts_normal_and_maximum_length_member_names)
{
	CHECK(accepts(makeArchive("Data\\INI\\synthetic.ini")));
	CHECK(accepts(makeArchive("loose.txt")));
	CHECK(accepts(makeArchive(std::string(_MAX_PATH - 1, 'a'))));
}

TEST(bigarchive_rejects_long_names_truncation_and_invalid_ranges)
{
	CHECK(!accepts(makeArchive(std::string(_MAX_PATH, 'a'))));
	CHECK(!accepts(makeArchive("")));
	std::string valid = makeArchive("safe.txt");
	for (size_t size = 0; size < valid.size(); ++size) CHECK(!accepts(valid.substr(0, size)));
	std::string bad = valid;
	bad[16] = (char)0x7f; // entry offset beyond the real file
	CHECK(!accepts(bad));
	bad = valid; bad[20] = (char)0x7f; // member size beyond the real file
	CHECK(!accepts(bad));
	bad = valid; bad[8] = (char)0x7f; // impossible entry count
	CHECK(!accepts(bad));
}
