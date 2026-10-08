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
/* WebAssembly port: C++ exception handling helpers of the Microsoft C runtime. */
#pragma once

#include "windows.h"

#ifdef __cplusplus
typedef void (__cdecl *_se_translator_function)(unsigned int, struct _EXCEPTION_POINTERS *);
/* Windows exceptions never occur, so there is nothing to translate. */
inline _se_translator_function _set_se_translator(_se_translator_function)
{
	return 0;
}
#endif
