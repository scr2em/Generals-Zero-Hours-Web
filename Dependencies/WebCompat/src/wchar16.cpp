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
** WebAssembly port: the wide character C library for 16-bit wchar_t.
**
** The game stores text as UTF-16 (WideChar is a Windows wchar_t), so the
** web build compiles with -fshort-wchar. musl's wide character functions are
** written for 32-bit wchar_t: they would read and write twice the memory, so
** every one the game could call is defined here. The linker takes these in
** place of musl's. Code points above U+FFFF are UTF-16 surrogate pairs.
*/
#include "webcompat_internal.h"
#include "charset.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

using namespace WebCompat;

static_assert(sizeof(wchar_t) == 2, "wchar16.cpp must be compiled with -fshort-wchar");

extern "C" {

/* ---------------------------------------------------------------------------
** Strings
** ------------------------------------------------------------------------- */

size_t wcslen(const wchar_t *s)
{
	const wchar_t *p = s;
	while (*p)
		++p;
	return (size_t)(p - s);
}

size_t wcsnlen(const wchar_t *s, size_t n)
{
	size_t length = 0;
	while (length < n && s[length])
		++length;
	return length;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src)
{
	wchar_t *d = dest;
	while ((*d++ = *src++) != 0)
	{
	}
	return dest;
}

wchar_t *wcpcpy(wchar_t *dest, const wchar_t *src)
{
	while ((*dest = *src++) != 0)
		++dest;
	return dest;
}

wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n)
{
	size_t i = 0;
	for (; i < n && src[i]; ++i)
		dest[i] = src[i];
	for (; i < n; ++i)
		dest[i] = 0;
	return dest;
}

wchar_t *wcscat(wchar_t *dest, const wchar_t *src)
{
	wcscpy(dest + wcslen(dest), src);
	return dest;
}

wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n)
{
	wchar_t *d = dest + wcslen(dest);
	size_t i = 0;
	for (; i < n && src[i]; ++i)
		d[i] = src[i];
	d[i] = 0;
	return dest;
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
	while (*a && *a == *b)
	{
		++a;
		++b;
	}
	return (int)(unsigned short)*a - (int)(unsigned short)*b;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
	for (; n; --n, ++a, ++b)
	{
		if (*a != *b || !*a)
			return (int)(unsigned short)*a - (int)(unsigned short)*b;
	}
	return 0;
}

int wcscoll(const wchar_t *a, const wchar_t *b)
{
	return wcscmp(a, b);
}

size_t wcsxfrm(wchar_t *dest, const wchar_t *src, size_t n)
{
	const size_t length = wcslen(src);
	if (n)
	{
		const size_t copy = length < n ? length : n - 1;
		memcpy(dest, src, copy * sizeof(wchar_t));
		dest[copy] = 0;
	}
	return length;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
	for (;; ++s)
	{
		if (*s == c)
			return const_cast<wchar_t *>(s);
		if (!*s)
			return nullptr;
	}
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
	const wchar_t *found = nullptr;
	for (;; ++s)
	{
		if (*s == c)
			found = s;
		if (!*s)
			return const_cast<wchar_t *>(found);
	}
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)
{
	if (!*needle)
		return const_cast<wchar_t *>(haystack);
	for (; *haystack; ++haystack)
	{
		const wchar_t *h = haystack;
		const wchar_t *n = needle;
		while (*h && *n && *h == *n)
		{
			++h;
			++n;
		}
		if (!*n)
			return const_cast<wchar_t *>(haystack);
	}
	return nullptr;
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept)
{
	size_t length = 0;
	while (s[length] && wcschr(accept, s[length]))
		++length;
	return length;
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject)
{
	size_t length = 0;
	while (s[length] && !wcschr(reject, s[length]))
		++length;
	return length;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept)
{
	s += wcscspn(s, accept);
	return *s ? const_cast<wchar_t *>(s) : nullptr;
}

wchar_t *wcstok(wchar_t *s, const wchar_t *delimiters, wchar_t **save)
{
	if (!s)
		s = *save;
	if (!s)
		return nullptr;
	s += wcsspn(s, delimiters);
	if (!*s)
	{
		*save = nullptr;
		return nullptr;
	}
	wchar_t *token = s;
	s += wcscspn(s, delimiters);
	if (*s)
	{
		*s = 0;
		*save = s + 1;
	}
	else
	{
		*save = nullptr;
	}
	return token;
}

wchar_t *_wcsdup(const wchar_t *s)
{
	const size_t size = (wcslen(s) + 1) * sizeof(wchar_t);
	wchar_t *copy = static_cast<wchar_t *>(malloc(size));
	if (copy)
		memcpy(copy, s, size);
	return copy;
}

wchar_t *wcsdup(const wchar_t *s)
{
	return _wcsdup(s);
}

int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
	for (;; ++a, ++b)
	{
		const wint_t x = towlower((wint_t)(unsigned short)*a);
		const wint_t y = towlower((wint_t)(unsigned short)*b);
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
	}
}

int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
	for (; n; --n, ++a, ++b)
	{
		const wint_t x = towlower((wint_t)(unsigned short)*a);
		const wint_t y = towlower((wint_t)(unsigned short)*b);
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
	}
	return 0;
}

int wcscasecmp(const wchar_t *a, const wchar_t *b)
{
	return _wcsicmp(a, b);
}

int wcsncasecmp(const wchar_t *a, const wchar_t *b, size_t n)
{
	return _wcsnicmp(a, b, n);
}

wchar_t *_wcsupr(wchar_t *s)
{
	for (wchar_t *p = s; *p; ++p)
		*p = (wchar_t)towupper((wint_t)(unsigned short)*p);
	return s;
}

wchar_t *_wcslwr(wchar_t *s)
{
	for (wchar_t *p = s; *p; ++p)
		*p = (wchar_t)towlower((wint_t)(unsigned short)*p);
	return s;
}

wchar_t *_wcsrev(wchar_t *s)
{
	const size_t length = wcslen(s);
	for (size_t i = 0; i < length / 2; ++i)
	{
		const wchar_t c = s[i];
		s[i] = s[length - 1 - i];
		s[length - 1 - i] = c;
	}
	return s;
}

/* ---------------------------------------------------------------------------
** Memory
** ------------------------------------------------------------------------- */

wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, size_t n)
{
	return static_cast<wchar_t *>(memcpy(dest, src, n * sizeof(wchar_t)));
}

wchar_t *wmempcpy(wchar_t *dest, const wchar_t *src, size_t n)
{
	return static_cast<wchar_t *>(memcpy(dest, src, n * sizeof(wchar_t))) + n;
}

wchar_t *wmemmove(wchar_t *dest, const wchar_t *src, size_t n)
{
	return static_cast<wchar_t *>(memmove(dest, src, n * sizeof(wchar_t)));
}

wchar_t *wmemset(wchar_t *dest, wchar_t c, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		dest[i] = c;
	return dest;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
	for (; n; --n, ++a, ++b)
	{
		if (*a != *b)
			return (int)(unsigned short)*a - (int)(unsigned short)*b;
	}
	return 0;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
	for (; n; --n, ++s)
	{
		if (*s == c)
			return const_cast<wchar_t *>(s);
	}
	return nullptr;
}

/* ---------------------------------------------------------------------------
** Numbers
** ------------------------------------------------------------------------- */

// The numeric functions convert the text that can be part of a number to a
// narrow string, let the narrow function parse it and map the end back.
static const wchar_t *SkipSpace(const wchar_t *s)
{
	while (*s && iswspace((wint_t)(unsigned short)*s))
		++s;
	return s;
}

static size_t NumericPrefix(const wchar_t *s, char *buffer, size_t size)
{
	size_t n = 0;
	while (s[n] && s[n] < 0x80 && n + 1 < size)
	{
		const char c = (char)s[n];
		if (!(isalnum((unsigned char)c) || c == '.' || c == '+' || c == '-'))
			break;
		buffer[n] = c;
		++n;
	}
	buffer[n] = 0;
	return n;
}

long wcstol(const wchar_t *s, wchar_t **end, int base)
{
	const wchar_t *start = SkipSpace(s);
	char buffer[128];
	NumericPrefix(start, buffer, sizeof(buffer));
	char *narrowEnd;
	const long value = strtol(buffer, &narrowEnd, base);
	if (end)
		*end = const_cast<wchar_t *>(narrowEnd == buffer ? s : start + (narrowEnd - buffer));
	return value;
}

unsigned long wcstoul(const wchar_t *s, wchar_t **end, int base)
{
	const wchar_t *start = SkipSpace(s);
	char buffer[128];
	NumericPrefix(start, buffer, sizeof(buffer));
	char *narrowEnd;
	const unsigned long value = strtoul(buffer, &narrowEnd, base);
	if (end)
		*end = const_cast<wchar_t *>(narrowEnd == buffer ? s : start + (narrowEnd - buffer));
	return value;
}

long long wcstoll(const wchar_t *s, wchar_t **end, int base)
{
	const wchar_t *start = SkipSpace(s);
	char buffer[128];
	NumericPrefix(start, buffer, sizeof(buffer));
	char *narrowEnd;
	const long long value = strtoll(buffer, &narrowEnd, base);
	if (end)
		*end = const_cast<wchar_t *>(narrowEnd == buffer ? s : start + (narrowEnd - buffer));
	return value;
}

unsigned long long wcstoull(const wchar_t *s, wchar_t **end, int base)
{
	const wchar_t *start = SkipSpace(s);
	char buffer[128];
	NumericPrefix(start, buffer, sizeof(buffer));
	char *narrowEnd;
	const unsigned long long value = strtoull(buffer, &narrowEnd, base);
	if (end)
		*end = const_cast<wchar_t *>(narrowEnd == buffer ? s : start + (narrowEnd - buffer));
	return value;
}

double wcstod(const wchar_t *s, wchar_t **end)
{
	const wchar_t *start = SkipSpace(s);
	char buffer[128];
	NumericPrefix(start, buffer, sizeof(buffer));
	char *narrowEnd;
	const double value = strtod(buffer, &narrowEnd);
	if (end)
		*end = const_cast<wchar_t *>(narrowEnd == buffer ? s : start + (narrowEnd - buffer));
	return value;
}

float wcstof(const wchar_t *s, wchar_t **end)
{
	return (float)wcstod(s, end);
}

long double wcstold(const wchar_t *s, wchar_t **end)
{
	return wcstod(s, end);
}

int _wtoi(const wchar_t *s)
{
	return (int)wcstol(s, nullptr, 10);
}

long _wtol(const wchar_t *s)
{
	return wcstol(s, nullptr, 10);
}

long long _wtoi64(const wchar_t *s)
{
	return wcstoll(s, nullptr, 10);
}

double _wtof(const wchar_t *s)
{
	return wcstod(s, nullptr);
}

static wchar_t *UnsignedToWide(unsigned long long value, wchar_t *buffer, int radix, bool negative)
{
	wchar_t digits[66];
	int count = 0;
	if (radix < 2 || radix > 36)
		radix = 10;
	do
	{
		const int digit = (int)(value % (unsigned)radix);
		digits[count++] = (wchar_t)(digit < 10 ? L'0' + digit : L'a' + digit - 10);
		value /= (unsigned)radix;
	} while (value);
	wchar_t *out = buffer;
	if (negative)
		*out++ = L'-';
	while (count)
		*out++ = digits[--count];
	*out = 0;
	return buffer;
}

wchar_t *_itow(int value, wchar_t *buffer, int radix)
{
	if (radix == 10 && value < 0)
		return UnsignedToWide(0u - (unsigned)value, buffer, radix, true);
	return UnsignedToWide((unsigned)value, buffer, radix, false);
}

wchar_t *_ltow(long value, wchar_t *buffer, int radix)
{
	return _itow((int)value, buffer, radix);
}

wchar_t *_ultow(unsigned long value, wchar_t *buffer, int radix)
{
	return UnsignedToWide(value, buffer, radix, false);
}

wchar_t *_i64tow(long long value, wchar_t *buffer, int radix)
{
	if (radix == 10 && value < 0)
		return UnsignedToWide(0ull - (unsigned long long)value, buffer, radix, true);
	return UnsignedToWide((unsigned long long)value, buffer, radix, false);
}

wchar_t *_ui64tow(unsigned long long value, wchar_t *buffer, int radix)
{
	return UnsignedToWide(value, buffer, radix, false);
}

/* ---------------------------------------------------------------------------
** Multibyte conversion (UTF-8)
** ------------------------------------------------------------------------- */

size_t mbstowcs(wchar_t *dest, const char *src, size_t n)
{
	const unsigned char *s = reinterpret_cast<const unsigned char *>(src);
	size_t count = 0;
	for (;;)
	{
		uint32_t codePoint;
		size_t used = DecodeUtf8(s, 4, &codePoint);
		if (*s == 0)
		{
			if (dest && count < n)
				dest[count] = 0;
			return count;
		}
		if (used == 0)
		{
			// Not UTF-8: take the byte as a Windows-1252 character.
			codePoint = Cp1252ToUtf16(*s);
			used = 1;
		}
		s += used;
		const size_t units = codePoint >= 0x10000 ? 2 : 1;
		if (dest)
		{
			if (count + units > n)
				return count;
			if (units == 2)
			{
				codePoint -= 0x10000;
				dest[count] = (wchar_t)(0xD800 + (codePoint >> 10));
				dest[count + 1] = (wchar_t)(0xDC00 + (codePoint & 0x3FF));
			}
			else
			{
				dest[count] = (wchar_t)codePoint;
			}
		}
		count += units;
	}
}

size_t wcstombs(char *dest, const wchar_t *src, size_t n)
{
	size_t count = 0;
	for (;;)
	{
		uint32_t c = (unsigned short)*src;
		if (c == 0)
		{
			if (dest && count < n)
				dest[count] = 0;
			return count;
		}
		++src;
		if (IsHighSurrogate(c) && IsLowSurrogate((unsigned short)*src))
		{
			c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned short)*src - 0xDC00);
			++src;
		}
		else if (IsHighSurrogate(c) || IsLowSurrogate(c))
		{
			c = 0xFFFD;
		}
		unsigned char bytes[4];
		const size_t length = EncodeUtf8(c, bytes);
		if (dest)
		{
			if (count + length > n)
				return count;
			memcpy(dest + count, bytes, length);
		}
		count += length;
	}
}

size_t mbsrtowcs(wchar_t *dest, const char **src, size_t n, mbstate_t *)
{
	const size_t count = mbstowcs(dest, *src, dest ? n : (size_t)-1);
	if (dest)
	{
		// Advance past the converted text.
		const unsigned char *s = reinterpret_cast<const unsigned char *>(*src);
		size_t converted = 0;
		while (converted < count && *s)
		{
			uint32_t codePoint;
			size_t used = DecodeUtf8(s, 4, &codePoint);
			if (used == 0)
			{
				codePoint = *s;
				used = 1;
			}
			s += used;
			converted += codePoint >= 0x10000 ? 2 : 1;
		}
		*src = *s ? reinterpret_cast<const char *>(s) : nullptr;
	}
	return count;
}

size_t wcsrtombs(char *dest, const wchar_t **src, size_t n, mbstate_t *)
{
	const size_t count = wcstombs(dest, *src, dest ? n : (size_t)-1);
	if (dest)
	{
		const wchar_t *s = *src;
		size_t converted = 0;
		while (*s && converted < count)
		{
			uint32_t c = (unsigned short)*s++;
			if (IsHighSurrogate(c) && IsLowSurrogate((unsigned short)*s))
			{
				c = 0x10000 + ((c - 0xD800) << 10) + ((unsigned short)*s - 0xDC00);
				++s;
			}
			unsigned char bytes[4];
			converted += EncodeUtf8(c, bytes);
		}
		*src = *s ? s : nullptr;
	}
	return count;
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *)
{
	if (!s)
		return 0;
	if (n == 0)
		return (size_t)-2;
	const unsigned char *u = reinterpret_cast<const unsigned char *>(s);
	if (*u == 0)
	{
		if (pwc)
			*pwc = 0;
		return 0;
	}
	uint32_t codePoint;
	size_t used = DecodeUtf8(u, n, &codePoint);
	if (used == 0)
	{
		// A sequence cut short by the end of the input is incomplete.
		if (*u >= 0xC2 && *u < 0xF5 && n < (size_t)(*u >= 0xF0 ? 4 : *u >= 0xE0 ? 3 : 2))
			return (size_t)-2;
		errno = EILSEQ;
		return (size_t)-1;
	}
	// Characters outside the BMP do not fit in one 16-bit unit.
	if (pwc)
		*pwc = codePoint >= 0x10000 ? (wchar_t)0xFFFD : (wchar_t)codePoint;
	return used;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *)
{
	if (!s)
		return 1;
	uint32_t c = (unsigned short)wc;
	if (IsHighSurrogate(c) || IsLowSurrogate(c))
		c = 0xFFFD;
	return EncodeUtf8(c, reinterpret_cast<unsigned char *>(s));
}

int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
	if (!s)
		return 0;
	const size_t result = mbrtowc(pwc, s, n, nullptr);
	if (result == (size_t)-2)
	{
		errno = EILSEQ;
		return -1;
	}
	return (int)result;
}

int wctomb(char *s, wchar_t wc)
{
	if (!s)
		return 0;
	return (int)wcrtomb(s, wc, nullptr);
}

} // extern "C"
