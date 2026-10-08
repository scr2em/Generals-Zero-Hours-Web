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
** WebAssembly port: the generic-text mappings of <tchar.h>. TCHAR is a
** narrow char (UNICODE is not defined), so the _tcs functions map to the
** narrow C library.
*/
#pragma once

#include "windows.h"

#ifdef UNICODE
#error "The web build compiles the game with narrow TCHAR"
#endif

#define _tcslen    strlen
#define _tcsclen   strlen
#define _tcscpy    strcpy
#define _tcsncpy   strncpy
#define _tcscat    strcat
#define _tcsncat   strncat
#define _tcscmp    strcmp
#define _tcsncmp   strncmp
#define _tcsicmp   _stricmp
#define _tcsnicmp  _strnicmp
#define _tcschr    strchr
#define _tcsrchr   strrchr
#define _tcsstr    strstr
#define _tcsspn    strspn
#define _tcscspn   strcspn
#define _tcspbrk   strpbrk
#define _tcstok    strtok
#define _tcsdup    _strdup
#define _tcsupr    _strupr
#define _tcslwr    _strlwr
#define _tcstol    strtol
#define _tcstoul   strtoul
#define _tcstod    strtod
#define _ttoi      atoi
#define _ttol      atol
#define _tcsinc(p) ((p) + 1)
#define _tcsdec(start, p) ((p) - 1)
#define _tcsnextc(p) ((unsigned int)(unsigned char)*(p))
#define _tprintf   printf
#define _ftprintf  fprintf
#define _stprintf  sprintf
#define _sntprintf _snprintf
#define _vsntprintf _vsnprintf
#define _tfopen    fopen
#define _fgetts    fgets
#define _fputts    fputs
#define _tmain     main
#define _istalpha  isalpha
#define _istdigit  isdigit
#define _istspace  isspace
#define _istalnum  isalnum
#define _istupper  isupper
#define _istlower  islower
#define _totupper  toupper
#define _totlower  tolower
