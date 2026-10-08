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
** WebAssembly port: includes the DirectX 8 headers (min-dx8-sdk) on top of
** Dependencies/WebCompat.
**
** The Win32 stand-ins are still incomplete for a few of the types that only
** the unused parts of the D3D headers mention (RGNDATA in Present(), IStream
** and LPGLYPHMETRICSFLOAT in the D3DX mesh/shape headers). They are renamed to
** opaque private types while the headers are parsed, so this file works no
** matter what WebCompat provides. Only the library's own translation units see
** these private names; they never cross the COM boundary (pointers only).
*/
#pragma once

#include <windows.h>
#include <objbase.h>

#ifndef HMONITOR_DECLARED
#define HMONITOR_DECLARED
#endif

struct WebD3D8_RGNDATA;
struct WebD3D8_IStream;
#define RGNDATA WebD3D8_RGNDATA
#define IStream WebD3D8_IStream
#define LPGLYPHMETRICSFLOAT void *

#include <d3d8.h>
#include <d3dx8.h>

#undef RGNDATA
#undef IStream
#undef LPGLYPHMETRICSFLOAT

typedef const WebD3D8_RGNDATA *WebD3D8_PCRGNDATA;
