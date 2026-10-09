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
** WebAssembly port: conversions between the Windows ANSI code page (1252),
** UTF-8 and UTF-16, shared by the string functions.
*/
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace WebCompat
{

// Windows-1252 bytes 0x80..0x9F in UTF-16.
inline uint16_t Cp1252ToUtf16(unsigned char c)
{
	static const uint16_t s_high[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
	};
	return (c >= 0x80 && c < 0xA0) ? s_high[c - 0x80] : c;
}

// Returns -1 if the character has no Windows-1252 equivalent.
inline int Utf16ToCp1252(uint32_t c)
{
	if (c < 0x80 || (c >= 0xA0 && c <= 0xFF))
		return (int)c;
	for (int i = 0x80; i < 0xA0; ++i)
	{
		if (Cp1252ToUtf16((unsigned char)i) == c)
			return i;
	}
	return -1;
}

// Decodes one UTF-8 sequence. Returns the number of bytes used, or 0 if the
// sequence is invalid (the caller skips one byte). `available` is the number
// of readable bytes.
inline size_t DecodeUtf8(const unsigned char *s, size_t available, uint32_t *codePoint)
{
	if (available == 0)
		return 0;
	const unsigned char c = s[0];
	if (c < 0x80)
	{
		*codePoint = c;
		return 1;
	}
	size_t length;
	uint32_t value;
	if ((c & 0xE0) == 0xC0) { length = 2; value = c & 0x1F; }
	else if ((c & 0xF0) == 0xE0) { length = 3; value = c & 0x0F; }
	else if ((c & 0xF8) == 0xF0) { length = 4; value = c & 0x07; }
	else return 0;
	if (available < length)
		return 0;
	for (size_t i = 1; i < length; ++i)
	{
		if ((s[i] & 0xC0) != 0x80)
			return 0;
		value = (value << 6) | (s[i] & 0x3F);
	}
	static const uint32_t s_minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
	if (value < s_minimum[length] || value > 0x10FFFF || (value >= 0xD800 && value < 0xE000))
		return 0;
	*codePoint = value;
	return length;
}

// Encodes a code point as UTF-8. Returns the number of bytes (1 to 4).
inline size_t EncodeUtf8(uint32_t c, unsigned char *out)
{
	if (c < 0x80)
	{
		out[0] = (unsigned char)c;
		return 1;
	}
	if (c < 0x800)
	{
		out[0] = (unsigned char)(0xC0 | (c >> 6));
		out[1] = (unsigned char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000)
	{
		out[0] = (unsigned char)(0xE0 | (c >> 12));
		out[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
		out[2] = (unsigned char)(0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = (unsigned char)(0xF0 | (c >> 18));
	out[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
	out[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
	out[3] = (unsigned char)(0x80 | (c & 0x3F));
	return 4;
}

inline bool IsHighSurrogate(uint32_t c) { return c >= 0xD800 && c < 0xDC00; }
inline bool IsLowSurrogate(uint32_t c) { return c >= 0xDC00 && c < 0xE000; }

} // namespace WebCompat
