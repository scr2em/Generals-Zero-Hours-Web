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
** WebAssembly port: low level file I/O of the Microsoft C runtime. Paths are
** Windows paths: they are resolved case-insensitively (see
** webcompat_resolve_path).
*/
#pragma once

#include "windows.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR   O_RDWR
#define _O_APPEND O_APPEND
#define _O_CREAT  O_CREAT
#define _O_TRUNC  O_TRUNC
#define _O_EXCL   O_EXCL
/* There is no text mode: line endings are never translated. */
#define _O_BINARY 0
#define _O_TEXT   0
#ifndef O_BINARY
#define O_BINARY  0
#endif
#ifndef O_TEXT
#define O_TEXT    0
#endif

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

typedef unsigned long _fsize_t;

struct _finddata_t
{
	unsigned attrib;
	time_t time_create;
	time_t time_access;
	time_t time_write;
	_fsize_t size;
	char name[260];
};

int _open(const char *path, int flags, ...);
int _creat(const char *path, int mode);
int _close(int fd);
int _read(int fd, void *buffer, unsigned int count);
int _write(int fd, const void *buffer, unsigned int count);
long _lseek(int fd, long offset, int origin);
long _tell(int fd);
long _filelength(int fd);
int _chsize(int fd, long size);
int _commit(int fd);
int _dup(int fd);
int _eof(int fd);
int _setmode(int fd, int mode);

intptr_t _findfirst(const char *filespec, struct _finddata_t *fileinfo);
int _findnext(intptr_t handle, struct _finddata_t *fileinfo);
int _findclose(intptr_t handle);

#ifdef __cplusplus
}
#endif
