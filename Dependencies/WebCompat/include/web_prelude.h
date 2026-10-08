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
** WebAssembly port: force-included ahead of every translation unit
** (see Dependencies/WebCompat/CMakeLists.txt).
**
** Makes the MSVC C runtime extensions available everywhere, as they were on
** Windows. Win32 itself is only visible to files that include windows.h.
*/
#pragma once

#ifndef ZH_WEB
#define ZH_WEB 1
#endif

#include "msvcrt_compat.h"
