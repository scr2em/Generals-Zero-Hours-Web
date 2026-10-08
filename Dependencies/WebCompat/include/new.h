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
/* WebAssembly port: <new.h> of the Microsoft C runtime. */
#pragma once

#include <new>

#ifdef __cplusplus
typedef int (__cdecl *_PNH)(size_t);
inline _PNH _set_new_handler(_PNH)
{
	return 0;
}
inline int _set_new_mode(int)
{
	return 0;
}
#endif
