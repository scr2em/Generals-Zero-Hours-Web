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
** Where the Win32 stand-ins say the game's folders are: the install paths in the
** registry (Zero Hour, and the original Generals whose data Zero Hour also reads)
** and the shell folders (My Documents and friends, the user data).
**
** The web build keeps the defaults, the folders of its WasmFS tree: /game,
** /generals and /userdata. The native headless build points them at real
** directories before the game starts.
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Sets the folders; a null argument keeps that folder. Call before anything reads
** the registry or asks for a shell folder. The paths are copied. */
void WebCompat_SetGameFolders(const char *zeroHour, const char *generals, const char *userData);

#ifdef __cplusplus
}
#endif
