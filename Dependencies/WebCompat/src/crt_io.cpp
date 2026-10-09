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
** WebAssembly port: the file related functions of the Microsoft C runtime
** (io.h, direct.h, process.h, conio.h, stat) that take Windows paths.
*/
#include "webcompat_internal.h"

#include <io.h>
#include <direct.h>
#include <process.h>
#include <conio.h>
#include <excpt.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <string>

// The wrappers below call the C library functions that the header remaps.
#undef fopen
#undef freopen

using namespace WebCompat;

namespace
{

std::string ToPosix(const char *path)
{
	char resolved[PATH_MAX];
	ResolvePath(path, resolved, sizeof(resolved));
	return resolved;
}

// The mode of a file is read-only for the "user" bit when Windows would call
// the file read-only.
void FillFindInfo(const WIN32_FIND_DATAA &data, struct _finddata_t *info)
{
	info->attrib = 0;
	if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		info->attrib |= _A_SUBDIR;
	if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		info->attrib |= _A_RDONLY;
	if (data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)
		info->attrib |= _A_ARCH;
	int32_t nanoseconds;
	info->time_write = (time_t)FileTimeToUnixSeconds(&data.ftLastWriteTime, &nanoseconds);
	info->time_access = (time_t)FileTimeToUnixSeconds(&data.ftLastAccessTime, &nanoseconds);
	info->time_create = (time_t)FileTimeToUnixSeconds(&data.ftCreationTime, &nanoseconds);
	info->size = data.nFileSizeLow;
	strncpy(info->name, data.cFileName, sizeof(info->name) - 1);
	info->name[sizeof(info->name) - 1] = 0;
}

} // namespace

extern "C" {

FILE *webcompat_fopen(const char *path, const char *mode)
{
	if (!path || !mode)
	{
		errno = EINVAL;
		return nullptr;
	}
	return fopen(ToPosix(path).c_str(), mode);
}

FILE *webcompat_freopen(const char *path, const char *mode, FILE *stream)
{
	if (!path || !mode)
	{
		errno = EINVAL;
		return nullptr;
	}
	return freopen(ToPosix(path).c_str(), mode, stream);
}

#if defined(ZH_NATIVE_HEADLESS) && defined(__GLIBC__)
// The native headless build on Linux: libstdc++'s <cstdio> undefines the fopen and freopen macros of
// msvcrt_compat.h, so code that includes it calls the C library directly. The program is linked with
// --wrap=fopen --wrap=freopen (cmake/native-headless.cmake), which sends every call here.
FILE *__real_fopen(const char *path, const char *mode);
FILE *__real_freopen(const char *path, const char *mode, FILE *stream);

FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (!path || !mode)
	{
		errno = EINVAL;
		return nullptr;
	}
	return __real_fopen(ToPosix(path).c_str(), mode);
}

FILE *__wrap_freopen(const char *path, const char *mode, FILE *stream)
{
	if (!path || !mode)
		return __real_freopen(path, mode, stream);
	return __real_freopen(ToPosix(path).c_str(), mode, stream);
}
#endif

int _open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & O_CREAT)
	{
		va_list args;
		va_start(args, flags);
		mode = (mode_t)va_arg(args, int);
		va_end(args);
	}
	return open(ToPosix(path).c_str(), flags, mode ? mode : 0666);
}

int _creat(const char *path, int mode)
{
	return open(ToPosix(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0666);
}

int _close(int fd)
{
	return close(fd);
}

int _read(int fd, void *buffer, unsigned int count)
{
	return (int)read(fd, buffer, count);
}

int _write(int fd, const void *buffer, unsigned int count)
{
	return (int)write(fd, buffer, count);
}

long _lseek(int fd, long offset, int origin)
{
	return (long)lseek(fd, offset, origin);
}

long _tell(int fd)
{
	return (long)lseek(fd, 0, SEEK_CUR);
}

long _filelength(int fd)
{
	struct stat st;
	if (fstat(fd, &st) != 0)
		return -1;
	return (long)st.st_size;
}

int _chsize(int fd, long size)
{
	return ftruncate(fd, size);
}

int _commit(int fd)
{
	return fsync(fd);
}

int _dup(int fd)
{
	return dup(fd);
}

int _eof(int fd)
{
	struct stat st;
	const off_t position = lseek(fd, 0, SEEK_CUR);
	if (position < 0 || fstat(fd, &st) != 0)
		return -1;
	return position >= st.st_size ? 1 : 0;
}

int _setmode(int, int)
{
	return O_BINARY; // the previous mode: there is only binary
}

int _access(const char *path, int mode)
{
	return access(ToPosix(path).c_str(), mode);
}

int _chmod(const char *path, int mode)
{
	return chmod(ToPosix(path).c_str(), (mode_t)mode);
}

int _unlink(const char *path)
{
	return unlink(ToPosix(path).c_str());
}

int _rmdir(const char *path)
{
	return rmdir(ToPosix(path).c_str());
}

int _mkdir(const char *path)
{
	return mkdir(ToPosix(path).c_str(), 0777);
}

int _chdir(const char *path)
{
	return chdir(ToPosix(path).c_str());
}

char *_getdcwd(int, char *buffer, int maxlen)
{
	if (!buffer)
	{
		char *copy = static_cast<char *>(malloc(PATH_MAX));
		if (copy && !getcwd(copy, PATH_MAX))
		{
			free(copy);
			return nullptr;
		}
		return copy;
	}
	return getcwd(buffer, (size_t)maxlen);
}

int _getdrive(void)
{
	return 3; // C:
}

int _chdrive(int)
{
	return 0;
}

intptr_t _findfirst(const char *filespec, struct _finddata_t *fileinfo)
{
	WIN32_FIND_DATAA data;
	HANDLE handle = FindFirstFileA(filespec, &data);
	if (handle == INVALID_HANDLE_VALUE)
	{
		errno = ENOENT;
		return -1;
	}
	FillFindInfo(data, fileinfo);
	return reinterpret_cast<intptr_t>(handle);
}

int _findnext(intptr_t handle, struct _finddata_t *fileinfo)
{
	WIN32_FIND_DATAA data;
	if (!FindNextFileA(reinterpret_cast<HANDLE>(handle), &data))
	{
		errno = ENOENT;
		return -1;
	}
	FillFindInfo(data, fileinfo);
	return 0;
}

int _findclose(intptr_t handle)
{
	return FindClose(reinterpret_cast<HANDLE>(handle)) ? 0 : -1;
}

intptr_t _spawnl(int, const char *, const char *, ...)
{
	errno = ENOSYS;
	return -1;
}

intptr_t _spawnlp(int, const char *, const char *, ...)
{
	errno = ENOSYS;
	return -1;
}

intptr_t _spawnv(int, const char *, const char *const *)
{
	errno = ENOSYS;
	return -1;
}

intptr_t _spawnvp(int, const char *, const char *const *)
{
	errno = ENOSYS;
	return -1;
}

int _kbhit(void)
{
	return 0;
}

int _getch(void)
{
	return -1;
}

int _getche(void)
{
	return -1;
}

int _putch(int c)
{
	return putchar(c);
}

unsigned long GetExceptionCode(void)
{
	return 0;
}

void *GetExceptionInformation(void)
{
	return nullptr;
}

} // extern "C"

int _stat(const char *path, struct _stat *buffer)
{
	return stat(ToPosix(path).c_str(), buffer);
}

int _stati64(const char *path, struct _stati64 *buffer)
{
	return stat(ToPosix(path).c_str(), buffer);
}

int _fstat(int fd, struct _stat *buffer)
{
	return fstat(fd, buffer);
}
