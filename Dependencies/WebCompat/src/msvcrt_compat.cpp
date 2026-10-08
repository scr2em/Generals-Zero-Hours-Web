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
** WebAssembly port: implementations of the MSVC C runtime extensions
** declared in msvcrt_compat.h.
*/
#include "msvcrt_compat.h"

#include <fenv.h>
#include <limits.h>

extern "C" {

char *_strupr(char *s)
{
	for (char *p = s; *p; ++p) *p = (char)toupper((unsigned char)*p);
	return s;
}

char *_strlwr(char *s)
{
	for (char *p = s; *p; ++p) *p = (char)tolower((unsigned char)*p);
	return s;
}

char *_strrev(char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n / 2; ++i) {
		char c = s[i];
		s[i] = s[n - 1 - i];
		s[n - 1 - i] = c;
	}
	return s;
}

static char *u64_to_str(unsigned long long value, char *buffer, int radix, bool negative)
{
	char tmp[66];
	int i = 0;
	if (radix < 2 || radix > 36) radix = 10;
	do {
		int d = (int)(value % (unsigned)radix);
		tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		value /= (unsigned)radix;
	} while (value);
	char *out = buffer;
	if (negative) *out++ = '-';
	while (i) *out++ = tmp[--i];
	*out = 0;
	return buffer;
}

char *_i64toa(long long value, char *buffer, int radix)
{
	bool negative = radix == 10 && value < 0;
	unsigned long long u = negative ? 0ull - (unsigned long long)value : (unsigned long long)value;
	return u64_to_str(u, buffer, radix, negative);
}

char *_ui64toa(unsigned long long value, char *buffer, int radix)
{
	return u64_to_str(value, buffer, radix, false);
}

char *_itoa(int value, char *buffer, int radix)
{
	if (radix == 10) return _i64toa(value, buffer, radix);
	return _ui64toa((unsigned int)value, buffer, radix);
}

char *_ltoa(long value, char *buffer, int radix)
{
	return _itoa((int)value, buffer, radix);
}

char *_ultoa(unsigned long value, char *buffer, int radix)
{
	return _ui64toa(value, buffer, radix);
}

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
	if (drive) *drive = 0;
	if (dir) *dir = 0;
	if (fname) *fname = 0;
	if (ext) *ext = 0;
	if (!path) return;

	if (path[0] && path[1] == ':') {
		if (drive) {
			drive[0] = path[0];
			drive[1] = ':';
			drive[2] = 0;
		}
		path += 2;
	}

	const char *lastSlash = NULL;
	const char *lastDot = NULL;
	for (const char *p = path; *p; ++p) {
		if (*p == '/' || *p == '\\') lastSlash = p;
		else if (*p == '.') lastDot = p;
	}
	const char *nameStart = lastSlash ? lastSlash + 1 : path;
	if (lastDot && lastDot < nameStart) lastDot = NULL;

	if (dir && lastSlash) {
		size_t n = (size_t)(nameStart - path);
		memcpy(dir, path, n);
		dir[n] = 0;
	}
	const char *nameEnd = lastDot ? lastDot : nameStart + strlen(nameStart);
	if (fname) {
		size_t n = (size_t)(nameEnd - nameStart);
		memcpy(fname, nameStart, n);
		fname[n] = 0;
	}
	if (ext && lastDot) strcpy(ext, lastDot);
}

void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
	*path = 0;
	if (drive && *drive) {
		path[0] = drive[0];
		path[1] = ':';
		path[2] = 0;
	}
	if (dir && *dir) {
		strcat(path, dir);
		char last = dir[strlen(dir) - 1];
		if (last != '/' && last != '\\') strcat(path, "\\");
	}
	if (fname) strcat(path, fname);
	if (ext && *ext) {
		if (*ext != '.') strcat(path, ".");
		strcat(path, ext);
	}
}

char *_fullpath(char *absPath, const char *relPath, size_t maxLength)
{
	char buffer[PATH_MAX];
	if (relPath[0] == '/' || relPath[0] == '\\') {
		snprintf(buffer, sizeof(buffer), "%s", relPath);
	} else {
		char cwd[PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd))) return NULL;
		snprintf(buffer, sizeof(buffer), "%s/%s", cwd, relPath);
	}
	if (!absPath) return strdup(buffer);
	if (strlen(buffer) >= maxLength) return NULL;
	strcpy(absPath, buffer);
	return absPath;
}

// WebAssembly has a fixed floating point environment: round to nearest,
// no exceptions, and float operations always round to single precision
// (what the game asks for with _PC_24). Report that and ignore changes.
static unsigned int s_controlWord = _CW_DEFAULT;

unsigned int _control87(unsigned int newValue, unsigned int mask)
{
	s_controlWord = (s_controlWord & ~mask) | (newValue & mask);
	return s_controlWord;
}

unsigned int _controlfp(unsigned int newValue, unsigned int mask)
{
	return _control87(newValue, mask);
}

unsigned int _clearfp(void)
{
	return 0;
}

unsigned int _statusfp(void)
{
	return 0;
}

void _fpreset(void)
{
	s_controlWord = _CW_DEFAULT;
}

} // extern "C"
