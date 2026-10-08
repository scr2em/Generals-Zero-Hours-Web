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
** WebAssembly port: the Microsoft C runtime extensions the game uses
** (_stricmp, _snprintf, _splitpath, itoa, ...) on top of musl.
** Included by windows.h and by the force-included web_prelude.h.
*/
#pragma once

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <alloca.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

/* Strings */
#define _stricmp   strcasecmp
#define _strnicmp  strncasecmp
#define _strcmpi   strcasecmp
#define _strdup    strdup
#define _snprintf  snprintf
#define _vsnprintf vsnprintf
#define _scprintf(...) snprintf(NULL, 0, __VA_ARGS__)
#ifndef stricmp
#define stricmp    strcasecmp
#endif
#ifndef strnicmp
#define strnicmp   strncasecmp
#endif
#ifndef strcmpi
#define strcmpi    strcasecmp
#endif
char *_strupr(char *s);
char *_strlwr(char *s);
char *_strrev(char *s);
#ifndef strupr
#define strupr _strupr
#endif
#ifndef strlwr
#define strlwr _strlwr
#endif
#ifndef strrev
#define strrev _strrev
#endif

/* Number <-> string */
char *_itoa(int value, char *buffer, int radix);
char *_ltoa(long value, char *buffer, int radix);
char *_ultoa(unsigned long value, char *buffer, int radix);
char *_i64toa(long long value, char *buffer, int radix);
char *_ui64toa(unsigned long long value, char *buffer, int radix);
#ifndef itoa
#define itoa _itoa
#endif
#ifndef ltoa
#define ltoa _ltoa
#endif
#ifndef ultoa
#define ultoa _ultoa
#endif
#define _atoi64 atoll
#define _strtoi64 strtoll
#define _strtoui64 strtoull

/* Wide strings (16-bit wchar_t, see wchar16.cpp) */
int _wcsicmp(const wchar_t *a, const wchar_t *b);
int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *_wcsupr(wchar_t *s);
wchar_t *_wcslwr(wchar_t *s);
wchar_t *_wcsdup(const wchar_t *s);
int _snwprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...);
int _vsnwprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list args);
wchar_t *_itow(int value, wchar_t *buffer, int radix);
#define wcsicmp _wcsicmp
#define wcsnicmp _wcsnicmp

/* Paths */
void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext);
void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext);
char *_fullpath(char *absPath, const char *relPath, size_t maxLength);
#define _getcwd getcwd
#define _chdir chdir
#define _rmdir rmdir
#define _unlink unlink
#define _access access
#define _isatty isatty
#define _fileno fileno
#define _getpid getpid
#define _putenv putenv
#define _tzset tzset
int _mkdir(const char *path);
#define _stat stat
#define _fstat fstat
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR

/* Math */
#define _isnan isnan
#define _finite isfinite
#define _hypot hypot
#define _copysign copysign
#define _logb logb
#define _chgsign(x) (-(x))
#define _fpclass(x) 0
unsigned int _control87(unsigned int newValue, unsigned int mask);
unsigned int _controlfp(unsigned int newValue, unsigned int mask);
unsigned int _clearfp(void);
unsigned int _statusfp(void);
void _fpreset(void);
#define _MCW_EM 0x0008001f
#define _MCW_PC 0x00030000
#define _MCW_RC 0x00000300
#define _PC_24  0x00020000
#define _PC_53  0x00010000
#define _PC_64  0x00000000
#define _RC_NEAR 0x00000000
#define _RC_CHOP 0x00000300
#define _EM_INVALID 0x00000010
#define _EM_DENORMAL 0x00080000
#define _EM_ZERODIVIDE 0x00000008
#define _EM_OVERFLOW 0x00000004
#define _EM_UNDERFLOW 0x00000002
#define _EM_INEXACT 0x00000001
#define _CW_DEFAULT (_RC_NEAR + _PC_53 + _EM_INVALID + _EM_ZERODIVIDE + _EM_OVERFLOW + _EM_UNDERFLOW + _EM_INEXACT + _EM_DENORMAL)

/* Memory */
#define _alloca alloca
#define _msize(p) malloc_usable_size(p)
size_t malloc_usable_size(void *p);
#define _aligned_malloc(size, align) aligned_alloc((align), (((size) + (align) - 1) / (align)) * (align))
#define _aligned_free free

/* Misc */
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define _CrtDbgBreak() ((void)0)
#define _ASSERTE(x) ((void)0)
#define _ASSERT(x) ((void)0)

#ifdef __cplusplus
} /* extern "C" */
#endif
