// File system test for the browser build: what the game does to its files, in a few lines.
//
//   cmake --build <dir> --target web_fs_test
//   node smoke.mjs --site <dir>/GeneralsMD --page web_fs_test.html --data <dir with ZeroHour/ and Generals/>
//
// Prints "FS: ok ..." or "FS: FAIL ..." lines and "FS_DONE" at the end. Uses the WebCompat file
// functions (Windows paths, case insensitivity) on top of the OPFS mounts of WebStorage.cpp.
// The data set must contain Data/INI/GameData.ini (any content) in ZeroHour, like the one
// gen_synthetic_data.py --loose writes.
#include <windows.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <emscripten/html5.h>

#include <filesystem>
#include <string>

#include "WebDevice/Platform/WebPlatform.h"

static int s_failures = 0;

static void check(bool ok, const char *what, const std::string &detail = std::string())
{
	printf("FS: %s %s %s\n", ok ? "ok  " : "FAIL", what, detail.c_str());
	if (!ok)
		++s_failures;
}

static std::string errnoText()
{
	return std::string("errno=") + std::to_string(errno) + " " + strerror(errno);
}

int main()
{
	// Pulls the GL library in, which the canvas hand-over to this thread needs.
	printf("FS: webgl context %d\n", (int)emscripten_webgl_get_current_context());
	check(WebPlatform_MountStorage() == 0, "mount storage");

	struct stat st;
	check(stat("/game", &st) == 0 && S_ISDIR(st.st_mode), "stat /game", errnoText());
	check(access("/game/data/ini/gamedata.ini", F_OK) == 0, "access absolute lower case", errnoText());
	check(access("/game/Data/INI/GameData.ini", F_OK) == 0, "access absolute mixed case", errnoText());

	// What WorkingDirectory does, then relative accesses like the engine's.
	check(SetCurrentDirectoryA("/game") != 0, "SetCurrentDirectory /game");
	char cwd[512] = {};
	getcwd(cwd, sizeof(cwd));
	printf("FS: cwd = %s\n", cwd);
	check(access("data/ini/gamedata.ini", F_OK) == 0, "access relative lower case", errnoText());
	check(access("Data/INI/GameData.ini", F_OK) == 0, "access relative mixed case", errnoText());
	check(std::filesystem::exists("Data/INI/GameData.ini"), "std::filesystem::exists relative mixed case");
	check(std::filesystem::exists("Data\\INI\\GameData.ini") == false, "backslash path is not a posix path (the game converts)");

	FILE *f = fopen("Data\\INI\\GameData.ini", "rb");
	check(f != nullptr, "fopen with Windows path and mixed case", errnoText());
	if (f)
	{
		char buffer[64] = {};
		size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
		check(n > 0, "fread", std::to_string(n) + " bytes");
		fclose(f);
	}

	HANDLE h = CreateFileA("Data\\INI\\GameData.ini", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	check(h != INVALID_HANDLE_VALUE, "CreateFile Windows path", errnoText());
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle(h);

	// directory listing of the archives, like the BIG loader does
	for (const char *dirName : { ".", "/game", "/game/", "/game/Data", "Data/INI", "/generals" })
	{
		int count = 0;
		bool foundBig = false;
		DIR *d = opendir(dirName);
		if (d)
		{
			while (const dirent *e = readdir(d))
			{
				++count;
				if (strstr(e->d_name, ".big") || strstr(e->d_name, ".ini"))
					foundBig = true;
			}
			closedir(d);
		}
		check(d != nullptr && count >= 2 && (foundBig || strstr(dirName, "Data") != nullptr), "opendir/readdir", std::string(dirName) + " entries=" + std::to_string(count) + " " + errnoText());
	}
	{
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA("*.big", &fd);
		check(h != INVALID_HANDLE_VALUE, "FindFirstFile *.big", h != INVALID_HANDLE_VALUE ? fd.cFileName : "");
		if (h != INVALID_HANDLE_VALUE)
			FindClose(h);
	}
	{
		std::error_code ec;
		int n = 0;
		for (const auto &entry : std::filesystem::directory_iterator(".", ec))
		{
			(void)entry;
			++n;
		}
		check(!ec && n >= 2, "std::filesystem::directory_iterator .", std::to_string(n) + " " + ec.message());
	}

	// the Generals install through its own link
	check(access("/generals", F_OK) == 0, "access /generals", errnoText());

	// writing user data, then reading it back with another case
	mkdir("/userdata/Save", 0777);
	FILE *w = fopen("/userdata/Save/Test.sav", "wb");
	check(w != nullptr, "create file in /userdata", errnoText());
	if (w)
	{
		fputs("hello", w);
		fclose(w);
		struct stat s2;
		check(stat("/userdata/save/TEST.SAV", &s2) == 0 && s2.st_size == 5, "stat written file with other case", errnoText());
		check(remove("/userdata/Save/Test.sav") == 0, "remove", errnoText());
	}

	// What ReleaseCrash does with the user data directory.
	{
		CreateDirectoryA("/userdata/Command and Conquer Generals Zero Hour Data", nullptr);
		const char *cur = "/userdata/Command and Conquer Generals Zero Hour Data/ReleaseCrashInfo.txt";
		const char *prev = "/userdata/Command and Conquer Generals Zero Hour Data/ReleaseCrashInfoPrev.txt";
		remove(prev);
		int r = rename(cur, prev);
		printf("FS: rename -> %d %s\n", r, errnoText().c_str());
		FILE *c = fopen(cur, "w");
		check(c != nullptr, "fopen crash log for writing", errnoText());
		if (c)
		{
			fprintf(c, "crash\n");
			fclose(c);
		}
		check(remove(cur) == 0, "remove crash log", errnoText());
	}

	// Cost of lookups: the engine looks files up on the OPFS before it asks the archives.
	{
		char name[64];
		double t0 = emscripten_get_now();
		int found = 0;
		for (int i = 0; i < 500; ++i)
		{
			snprintf(name, sizeof(name), "Art/W3D/missing%d.w3d", i);
			found += access(name, F_OK) == 0;
		}
		double t1 = emscripten_get_now();
		for (int i = 0; i < 500; ++i)
			found += access("Data/INI/GameData.ini", F_OK) == 0;
		double t2 = emscripten_get_now();
		for (int i = 0; i < 500; ++i)
		{
			snprintf(name, sizeof(name), "Art/W3D/missing%d.w3d", i % 10);
			found += std::filesystem::exists(name);
		}
		double t3 = emscripten_get_now();
		printf("FS: timing per call: missing file %.3f ms, existing file %.3f ms, missing file via std::filesystem %.3f ms (found %d)\n", (t1 - t0) / 500, (t2 - t1) / 500, (t3 - t2) / 500, found);
	}

	printf("FS: %d failures\nFS_DONE\n", s_failures);
	return 0;
}
