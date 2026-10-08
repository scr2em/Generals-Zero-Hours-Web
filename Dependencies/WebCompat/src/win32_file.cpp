/*
**	Command & Conquer Generals Zero Hour(tm)
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
** WebAssembly port: the Win32 file API on top of POSIX file descriptors.
** Paths are Windows paths (see WebCompat::ResolvePath).
*/
#include "webcompat_internal.h"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <vector>

using namespace WebCompat;

namespace
{

struct FileObject : HandleObject
{
	FileObject(int descriptor, bool deleteWhenClosed, const char *name, bool ownsDescriptor = true)
		: HandleObject(HANDLE_FILE), fd(descriptor), owned(ownsDescriptor), deleteOnClose(deleteWhenClosed), path(name)
	{
	}
	~FileObject() override
	{
		if (owned)
			close(fd);
		if (deleteOnClose)
			unlink(path.c_str());
	}

	int fd;
	bool owned;
	bool deleteOnClose;
	std::string path;
};

struct FindObject : HandleObject
{
	FindObject() : HandleObject(HANDLE_FIND), next(0) {}

	std::vector<WIN32_FIND_DATAA> entries;
	size_t next;
};

struct MappingObject : HandleObject
{
	MappingObject() : HandleObject(HANDLE_MAPPING), fd(-1), size(0), anonymous(nullptr) {}
	~MappingObject() override
	{
		if (fd >= 0)
			close(fd);
		free(anonymous);
	}

	int fd;        // a duplicate of the file's descriptor, or -1
	size_t size;
	void *anonymous; // the memory of a mapping that is not backed by a file
};

struct MappedView
{
	size_t length;
	bool anonymous;
	MappingObject *mapping;
};

pthread_mutex_t s_viewLock = PTHREAD_MUTEX_INITIALIZER;
std::map<void *, MappedView> s_views;

FileObject *FileFromHandle(HANDLE handle)
{
	return static_cast<FileObject *>(HandleToObject(handle, HANDLE_FILE));
}

DWORD AttributesFromStat(const struct stat &st)
{
	DWORD attributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
	if (!(st.st_mode & S_IWUSR))
		attributes |= FILE_ATTRIBUTE_READONLY;
	return attributes;
}

void FileTimeFromTimespec(const struct timespec &ts, FILETIME *out)
{
	UnixTimeToFileTime(ts.tv_sec, (int32_t)ts.tv_nsec, out);
}

// Wildcard match of a file name, ignoring case. '*' matches any run, '?' one
// character.
bool WildcardMatch(const char *pattern, const char *name)
{
	while (*pattern)
	{
		if (*pattern == '*')
		{
			while (*pattern == '*')
				++pattern;
			if (!*pattern)
				return true;
			for (; *name; ++name)
			{
				if (WildcardMatch(pattern, name))
					return true;
			}
			return WildcardMatch(pattern, name);
		}
		if (!*name)
			return false;
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
			return false;
		++pattern;
		++name;
	}
	return *name == 0;
}

void FillFindData(const struct stat &st, const char *name, WIN32_FIND_DATAA *data)
{
	memset(data, 0, sizeof(*data));
	data->dwFileAttributes = AttributesFromStat(st);
	FileTimeFromTimespec(st.st_mtim, &data->ftLastWriteTime);
	FileTimeFromTimespec(st.st_atim, &data->ftLastAccessTime);
	FileTimeFromTimespec(st.st_ctim, &data->ftCreationTime);
	data->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
	data->nFileSizeLow = (DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
	strncpy(data->cFileName, name, sizeof(data->cFileName) - 1);
}

bool EntryLess(const WIN32_FIND_DATAA &a, const WIN32_FIND_DATAA &b)
{
	return strcasecmp(a.cFileName, b.cFileName) < 0;
}

std::string TrimTrailingSeparators(std::string path)
{
	while (path.size() > 1 && (path[path.size() - 1] == '/' || path[path.size() - 1] == '\\'))
		path.erase(path.size() - 1);
	return path;
}

// Resolves a Windows path to a path that can be handed to POSIX.
std::string ToPosix(const char *path)
{
	char resolved[PATH_MAX];
	ResolvePath(path, resolved, sizeof(resolved));
	return resolved;
}

} // namespace

extern "C" {

HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD, LPSECURITY_ATTRIBUTES, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE)
{
	if (!lpFileName || !*lpFileName)
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	const std::string path = ToPosix(lpFileName);

	int flags;
	const bool read = (dwDesiredAccess & (GENERIC_READ | GENERIC_ALL)) != 0;
	const bool write = (dwDesiredAccess & (GENERIC_WRITE | GENERIC_ALL)) != 0;
	if (read && write)
		flags = O_RDWR;
	else if (write)
		flags = O_WRONLY;
	else
		flags = O_RDONLY;

	struct stat st;
	const bool existed = stat(path.c_str(), &st) == 0;
	switch (dwCreationDisposition)
	{
	case CREATE_NEW:
		flags |= O_CREAT | O_EXCL;
		break;
	case CREATE_ALWAYS:
		flags |= O_CREAT | O_TRUNC;
		break;
	case OPEN_EXISTING:
		break;
	case OPEN_ALWAYS:
		flags |= O_CREAT;
		break;
	case TRUNCATE_EXISTING:
		flags |= O_TRUNC;
		if (!write)
			flags = (flags & ~O_ACCMODE) | O_WRONLY;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}

	// Directories can be "opened" to query them, but not written.
	if (existed && S_ISDIR(st.st_mode))
		flags = O_RDONLY;

	const int fd = open(path.c_str(), flags, 0666);
	if (fd < 0)
	{
		if (errno == EEXIST)
			SetLastError(ERROR_FILE_EXISTS);
		else if (errno == ENOENT && !existed)
		{
			// Distinguish a missing file from a missing directory.
			const size_t slash = path.find_last_of('/');
			if (slash != std::string::npos && slash > 0 && access(path.substr(0, slash).c_str(), F_OK) != 0)
				SetLastError(ERROR_PATH_NOT_FOUND);
			else
				SetLastError(ERROR_FILE_NOT_FOUND);
		}
		else
			SetLastErrorFromErrno();
		return INVALID_HANDLE_VALUE;
	}
	const bool replaced = existed && (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == OPEN_ALWAYS);
	SetLastError(replaced ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return ToHandle(new FileObject(fd, (dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0, path.c_str()));
}

BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (lpNumberOfBytesRead)
		*lpNumberOfBytesRead = 0;
	ssize_t count;
	if (lpOverlapped)
	{
		const off_t offset = (off_t)(((uint64_t)lpOverlapped->OffsetHigh << 32) | lpOverlapped->Offset);
		count = pread(file->fd, lpBuffer, nNumberOfBytesToRead, offset);
	}
	else
	{
		count = read(file->fd, lpBuffer, nNumberOfBytesToRead);
	}
	if (count < 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	if (lpNumberOfBytesRead)
		*lpNumberOfBytesRead = (DWORD)count;
	return TRUE;
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (lpNumberOfBytesWritten)
		*lpNumberOfBytesWritten = 0;
	DWORD total = 0;
	while (total < nNumberOfBytesToWrite)
	{
		ssize_t count;
		if (lpOverlapped)
		{
			const off_t offset = (off_t)(((uint64_t)lpOverlapped->OffsetHigh << 32) | lpOverlapped->Offset) + total;
			count = pwrite(file->fd, static_cast<const char *>(lpBuffer) + total, nNumberOfBytesToWrite - total, offset);
		}
		else
		{
			count = write(file->fd, static_cast<const char *>(lpBuffer) + total, nNumberOfBytesToWrite - total);
		}
		if (count < 0)
		{
			SetLastErrorFromErrno();
			if (lpNumberOfBytesWritten)
				*lpNumberOfBytesWritten = total;
			return FALSE;
		}
		if (count == 0)
			break;
		total += (DWORD)count;
	}
	if (lpNumberOfBytesWritten)
		*lpNumberOfBytesWritten = total;
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return INVALID_SET_FILE_POINTER;
	}
	int whence;
	switch (dwMoveMethod)
	{
	case FILE_BEGIN: whence = SEEK_SET; break;
	case FILE_CURRENT: whence = SEEK_CUR; break;
	case FILE_END: whence = SEEK_END; break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_SET_FILE_POINTER;
	}
	int64_t distance;
	if (lpDistanceToMoveHigh)
		distance = (int64_t)(((uint64_t)(uint32_t)*lpDistanceToMoveHigh << 32) | (uint32_t)lDistanceToMove);
	else
		distance = lDistanceToMove;
	const off_t position = lseek(file->fd, (off_t)distance, whence);
	if (position < 0)
	{
		SetLastErrorFromErrno();
		return INVALID_SET_FILE_POINTER;
	}
	if (lpDistanceToMoveHigh)
		*lpDistanceToMoveHigh = (LONG)((uint64_t)position >> 32);
	SetLastError(ERROR_SUCCESS);
	return (DWORD)((uint64_t)position & 0xFFFFFFFFu);
}

BOOL WINAPI SetEndOfFile(HANDLE hFile)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	const off_t position = lseek(file->fd, 0, SEEK_CUR);
	if (position < 0 || ftruncate(file->fd, position) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
	FileObject *file = FileFromHandle(hFile);
	struct stat st;
	if (!file || fstat(file->fd, &st) != 0)
	{
		SetLastError(file ? ErrnoToWin32Error(errno) : ERROR_INVALID_HANDLE);
		return INVALID_FILE_SIZE;
	}
	if (lpFileSizeHigh)
		*lpFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
	SetLastError(ERROR_SUCCESS);
	return (DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
}

BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
	FileObject *file = FileFromHandle(hFile);
	struct stat st;
	if (!file || fstat(file->fd, &st) != 0)
	{
		SetLastError(file ? ErrnoToWin32Error(errno) : ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (lpCreationTime) FileTimeFromTimespec(st.st_ctim, lpCreationTime);
	if (lpLastAccessTime) FileTimeFromTimespec(st.st_atim, lpLastAccessTime);
	if (lpLastWriteTime) FileTimeFromTimespec(st.st_mtim, lpLastWriteTime);
	return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE hFile, const FILETIME *, const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	struct timespec times[2];
	times[0].tv_sec = 0;
	times[0].tv_nsec = UTIME_OMIT;
	times[1].tv_sec = 0;
	times[1].tv_nsec = UTIME_OMIT;
	int32_t nanoseconds;
	if (lpLastAccessTime)
	{
		times[0].tv_sec = (time_t)FileTimeToUnixSeconds(lpLastAccessTime, &nanoseconds);
		times[0].tv_nsec = nanoseconds;
	}
	if (lpLastWriteTime)
	{
		times[1].tv_sec = (time_t)FileTimeToUnixSeconds(lpLastWriteTime, &nanoseconds);
		times[1].tv_nsec = nanoseconds;
	}
	if (futimens(file->fd, times) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
	FileObject *file = FileFromHandle(hFile);
	struct stat st;
	if (!file || !lpFileInformation || fstat(file->fd, &st) != 0)
	{
		SetLastError(file ? ErrnoToWin32Error(errno) : ERROR_INVALID_HANDLE);
		return FALSE;
	}
	memset(lpFileInformation, 0, sizeof(*lpFileInformation));
	lpFileInformation->dwFileAttributes = AttributesFromStat(st);
	FileTimeFromTimespec(st.st_ctim, &lpFileInformation->ftCreationTime);
	FileTimeFromTimespec(st.st_atim, &lpFileInformation->ftLastAccessTime);
	FileTimeFromTimespec(st.st_mtim, &lpFileInformation->ftLastWriteTime);
	lpFileInformation->dwVolumeSerialNumber = 0x1234ABCD;
	lpFileInformation->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
	lpFileInformation->nFileSizeLow = (DWORD)((uint64_t)st.st_size & 0xFFFFFFFFu);
	lpFileInformation->nNumberOfLinks = (DWORD)st.st_nlink;
	lpFileInformation->nFileIndexHigh = (DWORD)((uint64_t)st.st_ino >> 32);
	lpFileInformation->nFileIndexLow = (DWORD)(st.st_ino & 0xFFFFFFFFu);
	return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE hFile)
{
	FileObject *file = FileFromHandle(hFile);
	if (!file)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	fsync(file->fd);
	return TRUE;
}

DWORD WINAPI GetFileType(HANDLE hFile)
{
	if (!FileFromHandle(hFile))
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return 0; // FILE_TYPE_UNKNOWN
	}
	return FILE_TYPE_DISK;
}

BOOL WINAPI DeleteFileA(LPCSTR lpFileName)
{
	const std::string path = ToPosix(lpFileName);
	if (unlink(path.c_str()) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists)
{
	const std::string source = ToPosix(lpExistingFileName);
	const std::string destination = ToPosix(lpNewFileName);
	const int in = open(source.c_str(), O_RDONLY);
	if (in < 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	const int out = open(destination.c_str(), O_WRONLY | O_CREAT | (bFailIfExists ? O_EXCL : O_TRUNC), 0666);
	if (out < 0)
	{
		if (errno == EEXIST)
			SetLastError(ERROR_FILE_EXISTS);
		else
			SetLastErrorFromErrno();
		close(in);
		return FALSE;
	}
	char buffer[65536];
	BOOL result = TRUE;
	for (;;)
	{
		const ssize_t count = read(in, buffer, sizeof(buffer));
		if (count < 0)
		{
			SetLastErrorFromErrno();
			result = FALSE;
			break;
		}
		if (count == 0)
			break;
		ssize_t written = 0;
		while (written < count)
		{
			const ssize_t n = write(out, buffer + written, count - written);
			if (n < 0)
			{
				SetLastErrorFromErrno();
				result = FALSE;
				break;
			}
			written += n;
		}
		if (!result)
			break;
	}
	close(in);
	close(out);
	return result;
}

BOOL WINAPI MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags)
{
	const std::string source = ToPosix(lpExistingFileName);
	const std::string destination = ToPosix(lpNewFileName);
	if (!(dwFlags & MOVEFILE_REPLACE_EXISTING) && access(destination.c_str(), F_OK) == 0)
	{
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	if (rename(source.c_str(), destination.c_str()) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
	return MoveFileExA(lpExistingFileName, lpNewFileName, 0);
}

BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES)
{
	const std::string path = TrimTrailingSeparators(ToPosix(lpPathName));
	if (mkdir(path.c_str(), 0777) != 0)
	{
		if (errno == EEXIST)
			SetLastError(ERROR_ALREADY_EXISTS);
		else if (errno == ENOENT)
			SetLastError(ERROR_PATH_NOT_FOUND);
		else
			SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR lpPathName)
{
	const std::string path = TrimTrailingSeparators(ToPosix(lpPathName));
	if (rmdir(path.c_str()) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName)
{
	if (!lpFileName)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_FILE_ATTRIBUTES;
	}
	const std::string path = TrimTrailingSeparators(ToPosix(lpFileName));
	struct stat st;
	if (stat(path.empty() ? "." : path.c_str(), &st) != 0)
	{
		if (errno == ENOENT)
		{
			const size_t slash = path.find_last_of('/');
			const bool parentMissing = slash != std::string::npos && slash > 0 && access(path.substr(0, slash).c_str(), F_OK) != 0;
			SetLastError(parentMissing ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND);
		}
		else
			SetLastErrorFromErrno();
		return INVALID_FILE_ATTRIBUTES;
	}
	return AttributesFromStat(st);
}

BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes)
{
	const std::string path = ToPosix(lpFileName);
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	mode_t mode = st.st_mode & 07777;
	if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		mode |= S_IWUSR;
	if (chmod(path.c_str(), mode) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd)))
	{
		SetLastErrorFromErrno();
		return 0;
	}
	const size_t length = strlen(cwd);
	if (length + 1 > nBufferLength)
		return (DWORD)(length + 1);
	memcpy(lpBuffer, cwd, length + 1);
	return (DWORD)length;
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName)
{
	const std::string path = ToPosix(lpPathName);
	if (chdir(path.c_str()) != 0)
	{
		SetLastErrorFromErrno();
		return FALSE;
	}
	return TRUE;
}

DWORD WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
	std::string path(lpFileName);
	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '\\')
			path[i] = '/';
	}
	if (path.size() >= 2 && path[1] == ':')
		path.erase(0, 2);
	if (path.empty() || path[0] != '/')
	{
		char cwd[PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd)))
			return 0;
		path = std::string(cwd) + "/" + path;
	}

	// Remove "." and ".." components.
	std::vector<std::string> parts;
	size_t position = 0;
	while (position <= path.size())
	{
		size_t end = path.find('/', position);
		if (end == std::string::npos)
			end = path.size();
		const std::string part = path.substr(position, end - position);
		position = end + 1;
		if (part.empty() || part == ".")
			continue;
		if (part == "..")
		{
			if (!parts.empty())
				parts.pop_back();
			continue;
		}
		parts.push_back(part);
	}
	std::string full;
	for (size_t i = 0; i < parts.size(); ++i)
		full += "/" + parts[i];
	if (full.empty())
		full = "/";

	const size_t length = full.size();
	if (length + 1 > nBufferLength)
		return (DWORD)(length + 1);
	memcpy(lpBuffer, full.c_str(), length + 1);
	if (lpFilePart)
	{
		const size_t slash = full.find_last_of('/');
		*lpFilePart = (slash == std::string::npos || slash + 1 >= length) ? nullptr : lpBuffer + slash + 1;
	}
	return (DWORD)length;
}

DWORD WINAPI GetShortPathNameA(LPCSTR lpszLongPath, LPSTR lpszShortPath, DWORD cchBuffer)
{
	const size_t length = strlen(lpszLongPath);
	if (length + 1 > cchBuffer)
		return (DWORD)(length + 1);
	memcpy(lpszShortPath, lpszLongPath, length + 1);
	return (DWORD)length;
}

HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
	if (!lpFileName || !*lpFileName)
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	std::string pattern(lpFileName);
	for (size_t i = 0; i < pattern.size(); ++i)
	{
		if (pattern[i] == '\\')
			pattern[i] = '/';
	}
	const size_t slash = pattern.find_last_of('/');
	std::string directory = slash == std::string::npos ? std::string() : pattern.substr(0, slash + 1);
	std::string name = slash == std::string::npos ? pattern : pattern.substr(slash + 1);
	if (name.size() >= 2 && name[1] == ':' && slash == std::string::npos)
	{
		// "C:*.*"
		directory = name.substr(0, 2);
		name = name.substr(2);
	}
	if (name == "*.*")
		name = "*";

	FindObject *find = new FindObject();
	WIN32_FIND_DATAA data;
	struct stat st;

	if (name.find_first_of("*?") == std::string::npos)
	{
		// A single file or directory.
		const std::string path = TrimTrailingSeparators(ToPosix(lpFileName));
		if (stat(path.c_str(), &st) == 0)
		{
			const size_t last = path.find_last_of('/');
			FillFindData(st, last == std::string::npos ? path.c_str() : path.c_str() + last + 1, &data);
			find->entries.push_back(data);
		}
	}
	else
	{
		const std::string posixDirectory = directory.empty() ? std::string(".") : ToPosix(directory.c_str());
		DIR *dir = opendir(posixDirectory.c_str());
		if (!dir)
		{
			delete find;
			SetLastError(ERROR_PATH_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		}
		while (const struct dirent *entry = readdir(dir))
		{
			if (!WildcardMatch(name.c_str(), entry->d_name))
				continue;
			const std::string full = posixDirectory + "/" + entry->d_name;
			if (stat(full.c_str(), &st) != 0)
				continue;
			FillFindData(st, entry->d_name, &data);
			find->entries.push_back(data);
		}
		closedir(dir);
		std::sort(find->entries.begin(), find->entries.end(), EntryLess);
	}

	if (find->entries.empty())
	{
		delete find;
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	*lpFindFileData = find->entries[0];
	find->next = 1;
	return ToHandle(find);
}

BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
	FindObject *find = static_cast<FindObject *>(HandleToObject(hFindFile, HANDLE_FIND));
	if (!find)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	if (find->next >= find->entries.size())
	{
		SetLastError(ERROR_NO_MORE_FILES);
		return FALSE;
	}
	*lpFindFileData = find->entries[find->next++];
	return TRUE;
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
	FindObject *find = static_cast<FindObject *>(HandleToObject(hFindFile, HANDLE_FIND));
	if (!find)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	ReleaseObject(find);
	return TRUE;
}

HANDLE WINAPI GetStdHandle(DWORD nStdHandle)
{
	// The standard streams are shared objects that outlive any CloseHandle.
	static FileObject *s_streams[3];
	static pthread_once_t s_once = PTHREAD_ONCE_INIT;
	pthread_once(&s_once, []()
	{
		static const char *const names[3] = { "<stdin>", "<stdout>", "<stderr>" };
		for (int i = 0; i < 3; ++i)
		{
			s_streams[i] = new FileObject(i, false, names[i], false);
			AddRefObject(s_streams[i]);
		}
	});
	switch (nStdHandle)
	{
	case STD_INPUT_HANDLE: return ToHandle(s_streams[0]);
	case STD_OUTPUT_HANDLE: return ToHandle(s_streams[1]);
	case STD_ERROR_HANDLE: return ToHandle(s_streams[2]);
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
}

/* ---------------------------------------------------------------------------
** File mappings. Views are copies of the file made with mmap, so writes
** reach the file only for shared writable views as far as the file system
** allows.
** ------------------------------------------------------------------------- */

HANDLE WINAPI CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES, DWORD, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR)
{
	MappingObject *mapping = new MappingObject();
	size_t size = (size_t)dwMaximumSizeLow;
	(void)dwMaximumSizeHigh;
	if (hFile != INVALID_HANDLE_VALUE)
	{
		FileObject *file = FileFromHandle(hFile);
		struct stat st;
		if (!file || fstat(file->fd, &st) != 0)
		{
			delete mapping;
			SetLastError(ERROR_INVALID_HANDLE);
			return nullptr;
		}
		mapping->fd = dup(file->fd);
		if (size == 0)
			size = (size_t)st.st_size;
	}
	else
	{
		if (size == 0)
		{
			delete mapping;
			SetLastError(ERROR_INVALID_PARAMETER);
			return nullptr;
		}
		mapping->anonymous = calloc(1, size);
		if (!mapping->anonymous)
		{
			delete mapping;
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return nullptr;
		}
	}
	mapping->size = size;
	return ToHandle(mapping);
}

HANDLE WINAPI OpenFileMappingA(DWORD, BOOL, LPCSTR)
{
	SetLastError(ERROR_FILE_NOT_FOUND);
	return nullptr;
}

LPVOID WINAPI MapViewOfFileEx(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap, LPVOID)
{
	MappingObject *mapping = static_cast<MappingObject *>(HandleToObject(hFileMappingObject, HANDLE_MAPPING));
	if (!mapping)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return nullptr;
	}
	const off_t offset = (off_t)(((uint64_t)dwFileOffsetHigh << 32) | dwFileOffsetLow);
	size_t length = dwNumberOfBytesToMap ? dwNumberOfBytesToMap : mapping->size - (size_t)offset;
	void *address;
	MappedView view;
	view.length = length;
	view.mapping = mapping;
	if (mapping->anonymous)
	{
		// Every view of an anonymous mapping shows the same memory.
		address = static_cast<char *>(mapping->anonymous) + offset;
		view.anonymous = true;
	}
	else
	{
		const int protection = (dwDesiredAccess & FILE_MAP_WRITE) ? (PROT_READ | PROT_WRITE) : PROT_READ;
		address = mmap(nullptr, length, protection, (dwDesiredAccess & FILE_MAP_WRITE) ? MAP_SHARED : MAP_PRIVATE, mapping->fd, offset);
		if (address == MAP_FAILED)
		{
			SetLastErrorFromErrno();
			return nullptr;
		}
		view.anonymous = false;
	}
	AddRefObject(mapping);
	pthread_mutex_lock(&s_viewLock);
	s_views[address] = view;
	pthread_mutex_unlock(&s_viewLock);
	return address;
}

LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)
{
	return MapViewOfFileEx(hFileMappingObject, dwDesiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap, nullptr);
}

BOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress)
{
	MappedView view;
	pthread_mutex_lock(&s_viewLock);
	std::map<void *, MappedView>::iterator it = s_views.find(const_cast<void *>(lpBaseAddress));
	const bool found = it != s_views.end();
	if (found)
	{
		view = it->second;
		s_views.erase(it);
	}
	pthread_mutex_unlock(&s_viewLock);
	if (!found)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	if (!view.anonymous)
		munmap(const_cast<void *>(lpBaseAddress), view.length);
	ReleaseObject(view.mapping);
	return TRUE;
}

HFILE WINAPI _lopen(LPCSTR lpPathName, int iReadWrite)
{
	const std::string path = ToPosix(lpPathName);
	int flags = O_RDONLY;
	if ((iReadWrite & 3) == 1)
		flags = O_WRONLY;
	else if ((iReadWrite & 3) == 2)
		flags = O_RDWR;
	const int fd = open(path.c_str(), flags);
	if (fd < 0)
		SetLastErrorFromErrno();
	return fd < 0 ? -1 : fd;
}

HFILE WINAPI _lclose(HFILE hFile)
{
	return close(hFile) == 0 ? 0 : -1;
}

} // extern "C"
