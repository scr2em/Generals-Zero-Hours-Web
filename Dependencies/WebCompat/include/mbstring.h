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
** WebAssembly port: multibyte string functions. The game's ANSI code page is
** Windows-1252, which has no multibyte characters, so a character is a byte.
*/
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The number of characters in the first count bytes of the string, which
** ends at the first zero byte. */
static inline size_t _mbsnccnt(const unsigned char *string, size_t count)
{
	size_t length = 0;
	while (length < count && string[length] != 0)
		++length;
	return length;
}

#ifdef __cplusplus
}
#endif
