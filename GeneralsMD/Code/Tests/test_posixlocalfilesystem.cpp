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
 * PosixLocalFileSystem and PosixLocalFile, linked from the real engine sources: the memory pools,
 * LocalFile, File, and the PosixDevice classes (C1).  test_posixpath checks WWLib's resolver and
 * listing on their own; this checks what the engine gets through its own interface - the
 * FilenameList INI::loadDirectory walks, a TEXT file through LocalFile's scanners, the directories
 * openFile creates, and what getFileInfo reports.
 *
 * Run on $TMPDIR and, on macOS, on a case-sensitive APFS image the test creates, as test_posixpath
 * is: without that run nothing here has shown that case is handled.
 */
#include "test_harness.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "Common/AsciiString.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/LocalFile.h"
#include "Common/Registry.h"
#include "Common/STLTypedefs.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"

#include "posixpath.h"

namespace {

void make_directory(const std::string & path)
{
	mkdir(path.c_str(), 0777);
}

void write_file(const std::string & path, const std::string & contents)
{
	FILE * file = fopen(path.c_str(), "wb");
	if (file != NULL) {
		fwrite(contents.data(), 1, contents.size(), file);
		fclose(file);
	}
}

std::string read_file(const std::string & path)
{
	std::string contents;
	FILE * file = fopen(path.c_str(), "rb");
	if (file == NULL) return contents;
	char buffer[256];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) contents.append(buffer, got);
	fclose(file);
	return contents;
}

bool is_directory(const std::string & path)
{
	struct stat status;
	return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

bool is_case_sensitive(const std::string & directory)
{
	const std::string probe = directory + "/case_probe";
	write_file(probe, "x");
	struct stat status;
	const bool sensitive = stat((directory + "/CASE_PROBE").c_str(), &status) != 0;
	remove(probe.c_str());
	return sensitive;
}

void boot_memory()
{
	static bool booted = false;
	if (!booted) {
		initMemoryManager();
		booted = true;
	}
}

// The list as the engine holds it, in the set's order, joined with '|'.
std::string listed(PosixLocalFileSystem & files, const char * original, const char * search, Bool subdirectories)
{
	FilenameList list;
	files.getFileListInDirectory(AsciiString(""), AsciiString(original), AsciiString(search), list, subdirectories);
	std::string joined;
	for (FilenameList::const_iterator it = list.begin(); it != list.end(); ++it) {
		joined += (joined.empty() ? "" : "|") + std::string(it->str());
	}
	return joined;
}

// Returns the number of checks that ran only because the volume is case-sensitive.
unsigned run_suite(const std::string & root, const char * where)
{
	make_directory(root);
	const bool sensitive = is_case_sensitive(root);
	printf("  %s: %s, a %s volume\n", where, root.c_str(), sensitive ? "case-SENSITIVE" : "case-insensitive");
	char previous[4096];
	if (getcwd(previous, sizeof(previous)) == NULL || chdir(root.c_str()) != 0) {
		printf("  could not enter %s\n", root.c_str());
		CHECK(false);
		return 0;
	}
	PosixPath_Forget_All();
	unsigned sensitive_only = 0;
	PosixLocalFileSystem files;

	// ---- the INI load order: the set INI::loadDirectory walks ----
	// '\\' (0x5C) sorts after the digits and '/' (0x2F) before them, so Default2.ini comes before
	// Default\\x.ini only while the engine's spelling is kept - which is why nothing is normalised.
	make_directory(root + "/Data");
	make_directory(root + "/Data/INI");
	make_directory(root + "/Data/INI/Default");
	make_directory(root + "/Data/INI/Object");
	write_file(root + "/Data/INI/Weapon.ini", "");
	write_file(root + "/Data/INI/armor.INI", "");
	write_file(root + "/Data/INI/_under.ini", "");
	write_file(root + "/Data/INI/Default2.ini", "");
	write_file(root + "/Data/INI/Default/x.ini", "");
	write_file(root + "/Data/INI/Object/tank.ini", "");
	write_file(root + "/Data/INI/notes.txt", "");
	const char * order = "Data\\INI\\_under.ini|Data\\INI\\armor.INI|Data\\INI\\Default2.ini|Data\\INI\\Default\\x.ini"
		"|Data\\INI\\Object\\tank.ini|Data\\INI\\Weapon.ini";
	CHECK_STR(listed(files, "Data\\INI\\", "*.ini", TRUE).c_str(), order);
	CHECK_STR(listed(files, "data\\ini\\", "*.INI", TRUE).c_str(),
		"data\\ini\\_under.ini|data\\ini\\armor.INI|data\\ini\\Default2.ini|data\\ini\\Default\\x.ini"
		"|data\\ini\\Object\\tank.ini|data\\ini\\Weapon.ini");
	CHECK_STR(listed(files, "Data\\INI\\", "*.ini", FALSE).c_str(),
		"Data\\INI\\_under.ini|Data\\INI\\armor.INI|Data\\INI\\Default2.ini|Data\\INI\\Weapon.ini");
	if (sensitive) {
		// Two names differing only in case: the set keeps one, and always the same one.
		write_file(root + "/Data/INI/weapon.ini", "");
		CHECK_STR(listed(files, "Data\\INI\\", "*.ini", TRUE).c_str(), order);
		remove((root + "/Data/INI/weapon.ini").c_str());
		++sensitive_only;
	}

	// ---- openFile for writing creates the directories along the path ----
	File * file = files.openFile("SAVE\\Slot 1\\Deep\\game.sav", File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
	CHECK(file != NULL);
	if (file != NULL) {
		CHECK_EQ(file->write("saved", 5), 5);
		file->close();
	}
	CHECK_STR(read_file(root + "/SAVE/Slot 1/Deep/game.sav").c_str(), "saved");
	// An absolute path keeps its root: the directories go under it, not under the current one.
	const std::string absolute = root + "/Abs\\Dir\\file.txt";
	file = files.openFile(absolute.c_str(), File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
	CHECK(file != NULL);
	if (file != NULL) file->close();
	CHECK(is_directory(root + "/Abs/Dir"));
	CHECK(!is_directory(root + root));
	// A dot belongs to a directory name too, and an extensionless leaf is still a file.
	file = files.openFile("Saves.v2\\Slot\\checkpoint", File::WRITE | File::CREATE | File::BINARY);
	CHECK(file != NULL);
	if (file != NULL) { CHECK_EQ(file->write("ok", 2), 2); file->close(); }
	CHECK_STR(read_file(root + "/Saves.v2/Slot/checkpoint").c_str(), "ok");
	CHECK(!is_directory(root + "/Saves.v2/Slot/checkpoint"));
	if (sensitive) {
		// An existing directory in another case is used, not created again beside it.
		file = files.openFile("save\\SLOT 1\\deep\\other.sav", File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
		CHECK(file != NULL);
		if (file != NULL) file->close();
		CHECK(!is_directory(root + "/save"));
		CHECK_STR(read_file(root + "/SAVE/Slot 1/Deep/other.sav").c_str(), "");
		sensitive_only += 2;
	}

	// ---- reading, and doesFileExist, in the engine's spelling ----
	CHECK(files.doesFileExist("save\\slot 1\\deep\\GAME.SAV"));
	CHECK(!files.doesFileExist("save\\slot 1\\deep\\absent.sav"));
	file = files.openFile("save\\slot 1\\deep\\GAME.SAV", File::READ | File::BINARY);
	CHECK(file != NULL);
	if (file != NULL) {
		CHECK_EQ(file->size(), 5);
		char buffer[8] = { 0 };
		CHECK_EQ(file->read(buffer, sizeof(buffer)), 5);
		CHECK_STR(buffer, "saved");
		file->close();
	}
	CHECK(files.openFile("Data\\INI\\absent.ini", File::READ) == NULL);

	// ---- a TEXT file through LocalFile's scanners reads as on Windows ----
	write_file(root + "/Data/INI/text.ini", "12 -3\r\nword\r\n4.5\r\nline two\r\nlast");
	file = files.openFile("DATA\\ini\\Text.INI", File::READ | File::TEXT);
	CHECK(file != NULL);
	if (file != NULL) {
		Int number = 0;
		Real real = 0;
		AsciiString word;
		CHECK(file->scanInt(number));
		CHECK_EQ(number, 12);
		CHECK(file->scanInt(number));
		CHECK_EQ(number, -3);
		CHECK(file->scanString(word));
		CHECK_STR(word.str(), "word");
		CHECK(file->scanReal(real));
		CHECK(real == 4.5f);
		char line[64];
		file->nextLine(line, sizeof(line));		// the rest of "4.5"'s line
		CHECK_STR(line, "\n");
		file->nextLine(line, sizeof(line));
		CHECK_STR(line, "line two\n");				// "\r\n" read as "\n", as _O_TEXT reads it
		// At the end of the file nextLine counts the read that found nothing, so the byte after "last"
		// is whatever the buffer held (Win32 alike): start it clean.
		memset(line, 0, sizeof(line));
		file->nextLine(line, sizeof(line));
		CHECK_STR(line, "last");
		CHECK_EQ(file->size(), 32);					// the size is the file's, in bytes
		file->close();
	}
	file = files.openFile("Data\\INI\\text.ini", File::READ | File::BINARY);
	if (file != NULL) {
		char buffer[64];
		CHECK_EQ(file->read(buffer, sizeof(buffer)), 32);		// BINARY keeps every '\r'
		file->close();
	}
	file = files.openFile("Data\\INI\\text.ini", File::READ | File::TEXT);
	if (file != NULL) {
		char buffer[64];
		CHECK_EQ(file->read(buffer, sizeof(buffer)), 28);		// TEXT drops the four
		file->close();
	}

	// ---- getFileInfo: FILETIME units, as FindFirstFile reports them ----
	// 2003-02-10 00:00:00 UTC is 1044835200 s after 1970 and 12689308800 s after 1601; plus 0.25 s,
	// that is 126893088000000000 + 2500000 in 100 ns units.
	struct timeval times[2];
	times[0].tv_sec = times[1].tv_sec = 1044835200;
	times[0].tv_usec = times[1].tv_usec = 250000;
	utimes((root + "/Data/INI/text.ini").c_str(), times);
	FileInfo info;
	memset(&info, 0, sizeof(info));
	CHECK(files.getFileInfo(AsciiString("data\\INI\\TEXT.ini"), &info));
	const unsigned long long filetime = 126893088000000000ULL + 2500000ULL;
	CHECK_EQ((unsigned)info.timestampHigh, (unsigned)(filetime >> 32));
	CHECK_EQ((unsigned)info.timestampLow, (unsigned)(filetime & 0xFFFFFFFFULL));
	CHECK_EQ(info.sizeHigh, 0);
	CHECK_EQ(info.sizeLow, 32);
	CHECK(files.getFileInfo(AsciiString("Data\\INI\\Object"), &info));
	CHECK_EQ(info.sizeLow, 0);										// a directory has no size
	CHECK(!files.getFileInfo(AsciiString("Data\\INI\\absent.ini"), &info));

	// ---- createDirectory: one level, false when it exists or its parent does not ----
	CHECK(files.createDirectory(AsciiString("SAVE\\Replays")));
	CHECK(is_directory(root + "/SAVE/Replays"));
	CHECK(!files.createDirectory(AsciiString("save\\replays")));
	CHECK(!files.createDirectory(AsciiString("NoParent\\Child")));
	CHECK(!files.createDirectory(AsciiString("")));

	// ---- the operations engine code used to make directly (C1, decision D3) ----
	// copyFile: the bytes and the last write time; failIfExists; never onto itself.
	make_directory(root + "/Replays");
	write_file(root + "/Replays/Last Replay.rep", "GENREP-bytes");
	times[0].tv_sec = times[1].tv_sec = 1044835200;
	times[0].tv_usec = times[1].tv_usec = 0;
	utimes((root + "/Replays/Last Replay.rep").c_str(), times);
	CHECK(files.copyFile("replays\\LAST REPLAY.rep", "Replays\\Kept.rep", TRUE));
	CHECK_STR(read_file(root + "/Replays/Kept.rep").c_str(), "GENREP-bytes");
	struct stat copied;
	CHECK(stat((root + "/Replays/Kept.rep").c_str(), &copied) == 0 && copied.st_mtime == 1044835200);
	write_file(root + "/Replays/Kept.rep", "older");
	CHECK(!files.copyFile("Replays\\Last Replay.rep", "Replays\\Kept.rep", TRUE));		// failIfExists
	CHECK_STR(read_file(root + "/Replays/Kept.rep").c_str(), "older");
	CHECK(files.copyFile("Replays\\Last Replay.rep", "Replays\\Kept.rep", FALSE));		// replaced
	CHECK_STR(read_file(root + "/Replays/Kept.rep").c_str(), "GENREP-bytes");
	CHECK(!files.copyFile("Replays\\Kept.rep", "replays\\kept.REP", FALSE));			// onto itself
	CHECK_STR(read_file(root + "/Replays/Kept.rep").c_str(), "GENREP-bytes");
	CHECK(!files.copyFile("Replays\\Absent.rep", "Replays\\X.rep", FALSE));
	CHECK(!files.copyFile("Replays", "Replays\\Dir.rep", FALSE));						// a directory is not a file
	CHECK(!files.doesFileExist("Replays\\Dir.rep"));

	// deleteFile: one file, in any case spelling; never a directory, empty or not.
	CHECK(files.deleteFile("REPLAYS\\kept.rep"));
	CHECK(!files.doesFileExist("Replays\\Kept.rep"));
	CHECK(!files.deleteFile("Replays\\Kept.rep"));
	make_directory(root + "/Replays/Empty");
	CHECK(!files.deleteFile("Replays\\Empty"));
	CHECK(is_directory(root + "/Replays/Empty"));

	// moveFileReplacing: over an existing file, which is gone afterwards.
	write_file(root + "/Replays/cache.txt.123", "new");
	write_file(root + "/Replays/cache.txt", "old");
	CHECK(files.moveFileReplacing("Replays\\cache.txt.123", "replays\\CACHE.TXT"));
	CHECK_STR(read_file(root + "/Replays/cache.txt").c_str(), "new");
	CHECK(!files.doesFileExist("Replays\\cache.txt.123"));
	CHECK(!files.moveFileReplacing("Replays\\cache.txt.123", "Replays\\cache.txt"));

	// getFilesInDirectory: files only, bare names, matching the pattern, current directory untouched.
	write_file(root + "/Replays/00000001.sav", "");
	write_file(root + "/Replays/00000002.SAV", "");
	write_file(root + "/Replays/map.map", "");
	make_directory(root + "/Replays/dir.sav");
	std::vector<AsciiString> names;
	files.getFilesInDirectory(AsciiString("replays\\"), AsciiString("*.sav"), names);
	CHECK_EQ((int)names.size(), 2);
	if (names.size() == 2) {
		CHECK_STR(names[0].str(), "00000001.sav");
		CHECK_STR(names[1].str(), "00000002.SAV");
	}
	names.clear();
	files.getFilesInDirectory(AsciiString("Replays"), AsciiString("*"), names);		// no trailing separator
	CHECK_EQ((int)names.size(), 5);		// Last Replay.rep, cache.txt, the two saves, map.map
	names.clear();
	files.getFilesInDirectory(AsciiString("NoSuchDir"), AsciiString("*"), names);
	CHECK_EQ((int)names.size(), 0);
	char here[4096];
	CHECK(getcwd(here, sizeof(here)) != NULL);
	CHECK_STR(files.getCurrentDirectory().str(), here);

	if (chdir(previous) != 0) CHECK(false);
	if (!sensitive) {
		printf("  %s: the %u checks that need a case-sensitive volume did not run here\n", where, 3u);
	}
	return sensitive_only;
}

std::string temp_root(const char * name)
{
	const char * temp = getenv("TMPDIR");
	std::string base = (temp != NULL && *temp) ? temp : "/tmp";
	if (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
	char unique[64];
	snprintf(unique, sizeof(unique), "/%s_%d", name, (int)getpid());
	return base + unique;
}

void remove_tree(const std::string & root)
{
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
}

} // namespace

// registry.cpp's POSIX half: Registry.ini in the user data directory, whose path is spelled the
// Windows way and opened through zh_fopen (C1 (d)).  Before (d) a raw fopen read nothing here.  The
// directory is the test's own: ZH_USER_DATA_DIR is set before anything asks for it.
TEST(registry_values_come_from_registry_ini_in_the_user_data_directory)
{
	boot_memory();
	const std::string root = temp_root("test_posixlocalfilesystem_userdata");
	setenv("ZH_USER_DATA_DIR", root.c_str(), 1);
	make_directory(root);
	write_file(root + "/Registry.ini",
		"Language = german\n"
		"Version = 65540\n"
		"MapPackVersion = not a number\n"
		"ergc\\Proxy = 1\n"
		"Generals\\InstallPath = /games/generals\n"
		"language = polish\n");			// the last of two keys differing only in case wins

	AsciiString text;
	CHECK(GetStringFromRegistry("", "Language", text));
	CHECK_STR(text.str(), "polish");
	CHECK(GetStringFromRegistry("\\ergc", "Proxy", text));
	CHECK_STR(text.str(), "1");
	CHECK(GetStringFromGeneralsRegistry("", "InstallPath", text));
	CHECK_STR(text.str(), "/games/generals");
	CHECK(!GetStringFromRegistry("", "SKU", text));
	CHECK_EQ(GetRegistryVersion(), 65540u);
	CHECK_EQ(GetRegistryMapPackVersion(), 65536u);			// not a whole number: the default
	CHECK_STR(GetRegistryGameName().str(), "GeneralsMPTest");

	unsetenv("ZH_USER_DATA_DIR");
	remove_tree(root);
}

TEST(posixlocalfile_pool_is_sized_like_win32localfile)
{
	boot_memory();
	Int initial = 0, overflow = 0;
	userMemoryAdjustPoolSize("PosixLocalFile", initial, overflow);
	CHECK_EQ(initial, 1024);
	CHECK_EQ(overflow, 256);
}

TEST(posixlocalfilesystem_on_this_volume)
{
	boot_memory();
	const std::string root = temp_root("test_posixlocalfilesystem");
	run_suite(root, "default volume");
	remove_tree(root);
}

TEST(posixlocalfilesystem_on_a_case_sensitive_volume)
{
	boot_memory();
#if defined(__APPLE__)
	const std::string image_root = temp_root("test_posixlocalfilesystem_image");
	const std::string image = image_root + ".sparseimage";
	const std::string mount = image_root + "_mount";
	mkdir(mount.c_str(), 0777);
	const std::string create = "hdiutil create -quiet -size 16m -type SPARSE -fs 'Case-sensitive APFS' -volname ZHCS '" + image_root + "' 2>&1";
	const std::string attach = "hdiutil attach -quiet -nobrowse -mountpoint '" + mount + "' '" + image + "' 2>&1";
	if (system(create.c_str()) != 0 || system(attach.c_str()) != 0) {
		printf("  FAILED to create or attach a case-sensitive APFS image at %s; hdiutil is required on macOS\n", mount.c_str());
		CHECK(false);
		remove(image.c_str());
		rmdir(mount.c_str());
		return;
	}
	const unsigned sensitive_checks = run_suite(mount + "/root", "case-sensitive APFS image");
	CHECK(sensitive_checks > 0);
	const std::string detach = "hdiutil detach -quiet '" + mount + "' 2>&1";
	if (system(detach.c_str()) != 0) printf("  could not detach %s\n", mount.c_str());
	remove(image.c_str());
	rmdir(mount.c_str());
#else
	printf("  not needed off macOS: the default-volume run is already case-sensitive here\n");
#endif
}
