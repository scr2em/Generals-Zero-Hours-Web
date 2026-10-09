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
// Makes the game data and the persistent user data directory visible to the game
// through WasmFS. There are two ways to provide the game data, chosen by the page:
//
//  * OPFS mode (the default): the page copied the player's game files into the
//    Origin Private File System. The OPFS backend is wrapped in WasmFS' ignore-case
//    backend because Windows file names are case insensitive and the game relies on
//    that. That backend stores names in lower case, so everything in OPFS is lower
//    case (the page lower-cases names when it imports the game data).
//        /opfs/game, /opfs/generals, /opfs/userdata  ->  /game, /generals, /userdata
//
//  * Direct mode ("read in place"): nothing was copied. The page picked the player's
//    install folder with showDirectoryPicker() and handed the engine thread the File
//    objects of the files it needs (Module.zhDirect, see webdirect/zhdirect_pre.js).
//    A JavaScript backend (webdirect/library_zhdirect.js) serves them read-only and
//    synchronously, reading blocks of the files on demand. The tree below is created
//    in memory at mount time (one empty file per picked file, whose reads and size
//    come from the File), also behind the ignore-case backend:
//        /direct/game, /direct/generals             ->  /game, /generals
//    The user data (writes) stays in OPFS: /opfs/userdata -> /userdata.
//
//  * Army packages (.zharmy): the launcher can hand over the player's package files the same
//    way, as entries below "armies/". They appear at /armies (/direct/armies), read only, in
//    both modes above: with a game read in place they are part of the same tree; with the
//    OPFS copy (or the starter content) the tree holds only the packages, and the game data
//    still comes from OPFS.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Platform/WebPlatform.h"

#include <emscripten/emscripten.h>
#include <emscripten/wasmfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <unordered_set>

// Implemented by webdirect/library_zhdirect.js (linked with --js-library).
extern "C"
{
	int zh_direct_available(void);
	int zh_direct_count(void);
	int zh_direct_roots(void);
	int zh_direct_entry(int index, char *buffer, int capacity);
	void zh_direct_register_backend(backend_t backend);
	int zh_direct_stats(char *buffer, int capacity);
}

namespace
{

// The file <-> entry table of the direct mode, shared with every thread's JavaScript (see
// library_zhdirect.js): header[0] is the address of count uint32_t slots, header[1] the count.
uint32_t g_directHeader[4];
int g_storageMode = WEBPLATFORM_STORAGE_NONE;
int g_requireDirect = 0;

// Creates dir (a directory of a mounted tree) and a link to it at linkPath.
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

// Mounts the case insensitive wrapper of backend at path.
bool mountIgnoreCase(backend_t backend, const char *path)
{
	backend_t root = wasmfs_create_icase_backend(backend);
	if (root == nullptr)
	{
		fprintf(stderr, "WebPlatform: cannot create the case insensitive backend\n");
		return false;
	}
	if (wasmfs_create_directory(path, 0777, root) != 0)
	{
		fprintf(stderr, "WebPlatform: cannot mount %s\n", path);
		return false;
	}
	return true;
}

// The persistent user data: /userdata, in OPFS. Without OPFS (a private window may not have it)
// it is in memory and gone with the page; only the direct mode can get here, OPFS mode needs OPFS
// for the game data anyway.
bool mountUserData(bool required)
{
	// This blocks until the OPFS worker is up, so the engine must not be on the
	// browser main thread (it is not: -sPROXY_TO_PTHREAD).
	backend_t opfs = wasmfs_create_opfs_backend();
	if (opfs == nullptr)
	{
		fprintf(stderr, "WebPlatform: the Origin Private File System is not available\n");
		if (required)
			return false;
		fprintf(stderr, "WebPlatform: saves and options will not be kept\n");
		opfs = wasmfs_create_memory_backend();
		if (opfs == nullptr)
			return false;
	}

	return mountIgnoreCase(opfs, "/opfs") && exposeDirectory("/opfs/userdata", "/userdata");
}

// Creates "/direct/<dirs of path>" below /direct as needed.
bool makeDirectories(const std::string &path, std::unordered_set<std::string> &made)
{
	size_t slash = 0;
	while ((slash = path.find('/', slash + 1)) != std::string::npos)
	{
		const std::string dir = "/direct/" + path.substr(0, slash);
		if (made.insert(dir).second && mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST)
		{
			fprintf(stderr, "WebPlatform: cannot create %s (%s)\n", dir.c_str(), strerror(errno));
			return false;
		}
	}
	return true;
}

// Which roots the page's files are below (zh_direct_roots).
enum { DIRECT_ROOT_GAME = 1, DIRECT_ROOT_ARMIES = 2 };

// Mounts the files the page handed over as /direct. With the game root, /game and /generals
// point into it; with the armies root, /armies does.
bool mountDirect(int roots)
{
	const int count = zh_direct_count();

	backend_t js = wasmfs_create_jsimpl_backend();
	if (js == nullptr)
	{
		fprintf(stderr, "WebPlatform: cannot create the file backend\n");
		return false;
	}
	zh_direct_register_backend(js);

	if (!mountIgnoreCase(js, "/direct"))
		return false;

	// Tell every thread's JavaScript where the table is, before the first file is created.
	uint32_t *table = static_cast<uint32_t *>(calloc((size_t)count + 1, sizeof(uint32_t)));
	if (table == nullptr)
		return false;
	g_directHeader[0] = (uint32_t)(uintptr_t)table;
	g_directHeader[1] = (uint32_t)count;

	if ((roots & DIRECT_ROOT_GAME) && (mkdir("/direct/game", 0777) != 0 || mkdir("/direct/generals", 0777) != 0))
	{
		fprintf(stderr, "WebPlatform: cannot create the install directories (%s)\n", strerror(errno));
		return false;
	}
	if ((roots & DIRECT_ROOT_ARMIES) && mkdir("/direct/armies", 0777) != 0)
	{
		fprintf(stderr, "WebPlatform: cannot create the armies directory (%s)\n", strerror(errno));
		return false;
	}

	std::unordered_set<std::string> made;
	char path[1024];
	int created = 0, skipped = 0, armies = 0;
	for (int i = 0; i < count; ++i)
	{
		const int length = zh_direct_entry(i, path, (int)sizeof(path));
		if (length < 0)
		{
			fprintf(stderr, "WebPlatform: skipping entry %d (%s)\n", i, length == -2 ? "file of 2 GB or more" : "name too long");
			++skipped;
			continue;
		}

		const std::string name = std::string("/direct/") + path;
		if (!makeDirectories(path, made))
			return false;

		// Read only for everyone: the game must not open these for writing. (The mode applies to
		// later opens; creating with O_WRONLY works.) The JavaScript side learns which File this
		// WasmFS file belongs to during this call.
		const int fd = open(name.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0444);
		if (fd < 0)
		{
			// Two names that differ only in case.
			fprintf(stderr, "WebPlatform: cannot create %s (%s)\n", name.c_str(), strerror(errno));
			++skipped;
			continue;
		}
		close(fd);
		++created;
		if (strncmp(path, "armies/", 7) == 0)
			++armies;
	}

	printf("WebPlatform: direct mode, %d files (%d skipped)\n", created, skipped);
	if (roots & DIRECT_ROOT_ARMIES)
		printf("WebPlatform: %d army package files at /armies\n", armies);

	if ((roots & DIRECT_ROOT_ARMIES) && symlink("/direct/armies", "/armies") != 0)
		return false;
	if (roots & DIRECT_ROOT_GAME)
		return symlink("/direct/game", "/game") == 0 && symlink("/direct/generals", "/generals") == 0;
	return true;
}

// Mounts the copy of the game data in OPFS.
bool mountOpfsCopy()
{
	backend_t opfs = wasmfs_create_opfs_backend();
	if (opfs == nullptr)
	{
		fprintf(stderr, "WebPlatform: the Origin Private File System is not available\n");
		return false;
	}

	return mountIgnoreCase(opfs, "/opfs")
		&& exposeDirectory("/opfs/game", "/game")
		&& exposeDirectory("/opfs/generals", "/generals")
		&& exposeDirectory("/opfs/userdata", "/userdata");
}

} // namespace

// The address of the table the JavaScript side keeps its file associations in (see mountDirect).
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t *zh_direct_header(void)
{
	return g_directHeader;
}

extern "C" void WebPlatform_RequireDirectStorage(int required)
{
	g_requireDirect = required;
}

extern "C" int WebPlatform_GetStorageMode(void)
{
	return g_storageMode;
}

extern "C" int WebPlatform_GetDirectStats(char *buffer, int capacity)
{
	return zh_direct_stats(buffer, capacity);
}

extern "C" int WebPlatform_MountStorage(void)
{
	static int s_result = 1;	// 1 = not tried yet
	if (s_result != 1)
		return s_result;

	s_result = -1;

	const int roots = zh_direct_available() != 0 ? zh_direct_roots() : 0;
	const bool direct = (roots & DIRECT_ROOT_GAME) != 0;
	if (g_requireDirect && !direct)
	{
		fprintf(stderr, "WebPlatform: the page asked for the game files to be read from the player's folder but did not hand them over\n");
		return s_result;
	}

	// Army packages (and the game files, in direct mode) handed over by the page.
	if (roots != 0 && !mountDirect(roots))
		return s_result;

	if (direct)
	{
		// Reads of the game data come from the player's folder; saves and options still go to OPFS.
		if (mountUserData(false))
		{
			g_storageMode = WEBPLATFORM_STORAGE_DIRECT;
			s_result = 0;
		}
		return s_result;
	}

	if (mountOpfsCopy())
	{
		g_storageMode = WEBPLATFORM_STORAGE_OPFS;
		s_result = 0;
	}

	return s_result;
}
