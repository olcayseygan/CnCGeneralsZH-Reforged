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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngineDevice/Source/Win32Device/Common/Win32LocalFileSystem.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "PosixDevice/Common/PosixLocalFile.h"

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "posixpath.h"
#include "zhio.h"

PosixLocalFileSystem::PosixLocalFileSystem() : LocalFileSystem()
{
}

PosixLocalFileSystem::~PosixLocalFileSystem()
{
}

File * PosixLocalFileSystem::openFile(const Char *filename, Int access /* = 0 */)
{
	// sanity check
	if (filename == NULL || filename[0] == '\0') {
		return NULL;
	}

	PosixLocalFile *file = newInstance( PosixLocalFile );

	if (access & File::WRITE) {
		// Only separators distinguish parents from the leaf. Dotted directories and
		// extensionless saves are ordinary paths; keep the leading root intact.
		const std::string path(filename);
		for (size_t end = path.find_first_of("\\/"); end != std::string::npos;
				end = path.find_first_of("\\/", end + 1)) {
			if (end > 0) createDirectory(AsciiString(path.substr(0, end).c_str()));
		}
	}

	if (file->open(filename, access) == FALSE) {
		file->close();
		file->deleteInstance();
		file = NULL;
	} else {
		file->deleteOnClose();
	}

	return file;
}

void PosixLocalFileSystem::update()
{
}

void PosixLocalFileSystem::init()
{
}

void PosixLocalFileSystem::reset()
{
}

Bool PosixLocalFileSystem::doesFileExist(const Char *filename) const
{
	return zh_access(filename, F_OK) == 0;
}

void PosixLocalFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList & filenameList, Bool searchSubdirectories) const
{
	std::vector<std::string> found;
	PosixPath_List_Like_Win32(currentDirectory.str(), originalDirectory.str(), searchName.str(), searchSubdirectories != FALSE, found);
	for (size_t i = 0; i < found.size(); ++i) {
		AsciiString newFilename = found[i].c_str();
		if (filenameList.find(newFilename) == filenameList.end()) {
			filenameList.insert(newFilename);
		}
	}
}

// What FindFirstFile reports for one name: the last write time as a FILETIME (100 ns units since
// 1601), and the size, 0 for a directory.
Bool PosixLocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	std::string real;
	struct stat status;
	if (!PosixPath_Resolve(filename.str(), POSIX_PATH_EXISTING, real) || stat(real.c_str(), &status) != 0) {
		return FALSE;
	}

#if defined(__APPLE__)
	const long long nanoseconds = status.st_mtimespec.tv_nsec;
#else
	const long long nanoseconds = status.st_mtim.tv_nsec;
#endif
	const unsigned long long seconds_1601_to_1970 = 11644473600ULL;
	const unsigned long long filetime = ((unsigned long long)status.st_mtime + seconds_1601_to_1970) * 10000000ULL + nanoseconds / 100;
	const unsigned long long size = S_ISDIR(status.st_mode) ? 0 : (unsigned long long)status.st_size;

	fileInfo->timestampHigh = (Int)(filetime >> 32);
	fileInfo->timestampLow = (Int)(filetime & 0xFFFFFFFFULL);
	fileInfo->sizeHigh = (Int)(size >> 32);
	fileInfo->sizeLow = (Int)(size & 0xFFFFFFFFULL);

	return TRUE;
}

// One level, as CreateDirectory makes it: false when it exists already or its parent does not.
// Windows' _MAX_DIR limit is not imposed; the operating system's own applies.
Bool PosixLocalFileSystem::createDirectory(AsciiString directory)
{
	if (directory.getLength() > 0) {
		return zh_mkdir(directory.str()) == 0;
	}
	return FALSE;
}

// The calls engine code made directly until C1 (decision D3), each doing what the Windows call does.

// CopyFile: the bytes, and the source's last write time.  A copy onto the file itself fails, as
// CopyFile's sharing check makes it, rather than truncating the source.
Bool PosixLocalFileSystem::copyFile(const Char *from, const Char *to, Bool failIfExists)
{
	const int source = zh_open(from, O_RDONLY, 0);
	if (source < 0) {
		return FALSE;
	}
	struct stat sourceStatus;
	if (fstat(source, &sourceStatus) != 0 || S_ISDIR(sourceStatus.st_mode)) {
		close(source);
		errno = EISDIR;
		return FALSE;
	}
	std::string realTarget;
	struct stat targetStatus;
	if (PosixPath_Resolve(to, POSIX_PATH_CREATE_LEAF, realTarget) && stat(realTarget.c_str(), &targetStatus) == 0
			&& targetStatus.st_dev == sourceStatus.st_dev && targetStatus.st_ino == sourceStatus.st_ino) {
		close(source);
		errno = EBUSY;
		return FALSE;
	}

	const int target = zh_open(to, O_WRONLY | O_CREAT | O_TRUNC | (failIfExists ? O_EXCL : 0), sourceStatus.st_mode & 0777);
	if (target < 0) {
		close(source);
		return FALSE;
	}
	Bool copied = TRUE;
	char buffer[65536];
	for (;;) {
		const ssize_t got = read(source, buffer, sizeof(buffer));
		if (got == 0) {
			break;
		}
		if (got < 0) {
			if (errno == EINTR) continue;
			copied = FALSE;
			break;
		}
		for (ssize_t done = 0; done < got; ) {
			const ssize_t put = write(target, buffer + done, got - done);
			if (put < 0) {
				if (errno == EINTR) continue;
				copied = FALSE;
				break;
			}
			done += put;
		}
		if (!copied) {
			break;
		}
	}

	if (copied) {
		struct timespec times[2];
		times[0].tv_sec = 0;
		times[0].tv_nsec = UTIME_NOW;
#if defined(__APPLE__)
		times[1] = sourceStatus.st_mtimespec;
#else
		times[1] = sourceStatus.st_mtim;
#endif
		futimens(target, times);
	}
	const int error = errno;
	close(source);
	if (close(target) != 0) {
		copied = FALSE;
	}
	if (!copied) {
		zh_unlink(to);		// CopyFile leaves no partial copy behind
		errno = error;
	}
	return copied;
}

Bool PosixLocalFileSystem::deleteFile(const Char *path)
{
	return zh_unlink(path) == 0;
}

// rename replaces an existing file atomically, which is what MOVEFILE_REPLACE_EXISTING asks for, and
// fails across volumes as MoveFileEx does without MOVEFILE_COPY_ALLOWED.
Bool PosixLocalFileSystem::moveFileReplacing(const Char *from, const Char *to)
{
	return zh_rename(from, to) == 0;
}

// The same matching as getFileListInDirectory (FindFirstFile's patterns, case-insensitive), without
// recursion, in byte order.
void PosixLocalFileSystem::getFilesInDirectory(const AsciiString& directory, const AsciiString& searchName, std::vector<AsciiString> &names) const
{
	std::string original = directory.str();
	if (!original.empty() && original[original.size() - 1] != '\\' && original[original.size() - 1] != '/') {
		original += '\\';
	}
	std::vector<std::string> found;
	PosixPath_List_Like_Win32("", original, searchName.str(), false, found);
	for (size_t i = 0; i < found.size(); ++i) {
		names.push_back(AsciiString(found[i].c_str() + original.size()));
	}
}

AsciiString PosixLocalFileSystem::getCurrentDirectory() const
{
	char directory[PATH_MAX];
	if (getcwd(directory, sizeof(directory)) == NULL) {
		return AsciiString::TheEmptyString;
	}
	return AsciiString(directory);
}
