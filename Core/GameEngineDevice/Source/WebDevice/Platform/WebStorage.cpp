/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: WebStorage.cpp ///////////////////////////////////////////////////////
//
// Makes the game data, which the page copied into the Origin Private File
// System (OPFS), and the persistent user data directory visible to the game
// through WasmFS.
//
// The OPFS backend is wrapped in WasmFS' ignore-case backend because Windows
// file names are case insensitive and the game relies on that. That backend
// stores names in lower case, so everything in OPFS is lower case (the page
// lower-cases names when it imports the game data).
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Platform/WebPlatform.h"

#include <emscripten/wasmfs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

// Creates dir (a directory of the mounted OPFS tree) and a link to it at linkPath.
bool exposeDirectory(const char *dir, const char *linkPath)
{
	if (mkdir(dir, 0777) != 0 && errno != EEXIST)
	{
		fprintf(stderr, "WebPlatform: cannot create %s (%s)\n", dir, strerror(errno));
		return false;
	}

	if (symlink(dir, linkPath) != 0 && errno != EEXIST)
	{
		fprintf(stderr, "WebPlatform: cannot link %s to %s (%s)\n", linkPath, dir, strerror(errno));
		return false;
	}
	return true;
}

} // namespace

extern "C" int WebPlatform_MountStorage(void)
{
	static int s_result = 1;	// 1 = not tried yet
	if (s_result != 1)
		return s_result;

	s_result = -1;

	// This blocks until the OPFS worker is up, so the engine must not be on the
	// browser main thread (it is not: -sPROXY_TO_PTHREAD).
	backend_t opfs = wasmfs_create_opfs_backend();
	if (opfs == nullptr)
	{
		fprintf(stderr, "WebPlatform: the Origin Private File System is not available\n");
		return s_result;
	}

	backend_t root = wasmfs_create_icase_backend(opfs);
	if (root == nullptr)
	{
		fprintf(stderr, "WebPlatform: cannot create the case insensitive backend\n");
		return s_result;
	}

	if (wasmfs_create_directory("/opfs", 0777, root) != 0)
	{
		fprintf(stderr, "WebPlatform: cannot mount the Origin Private File System\n");
		return s_result;
	}

	if (exposeDirectory("/opfs/game", "/game")
		&& exposeDirectory("/opfs/generals", "/generals")
		&& exposeDirectory("/opfs/userdata", "/userdata"))
	{
		s_result = 0;
	}

	return s_result;
}
