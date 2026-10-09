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
** Makes the MSVC C runtime extensions and Win32 available everywhere, as
** they were on Windows, where nearly every translation unit sees windows.h
** through its precompiled header. The game guards its own windows.h
** includes with _WIN32, which the web build deliberately leaves undefined.
*/
#pragma once

#ifndef ZH_WEB
#define ZH_WEB 1
#endif

// The Win32 stand-ins of this library are in use. That is the web build and the native headless build
// (cmake/native-headless.cmake), which runs the same code with the system compiler. Code that needs the
// browser itself tests __EMSCRIPTEN__ instead.
#ifndef ZH_WEBCOMPAT
#define ZH_WEBCOMPAT 1
#endif

#include "windows.h"

// The rest of the C runtime extensions, which on Windows come with the
// compiler's own headers.
#include "direct.h"
#include "io.h"
#include "process.h"
