// Test of the "read in place" mode without the game (see ../INTEGRATION.md, test/README.txt).
//
// The page (direct_test.html) hands this program the File objects of a folder that holds
//   ZeroHour/INIZH.big            pattern file, 3 MB
//   ZeroHour/TexturesZH.big       pattern file, any size (the 1.5 GB case of the memory test)
//   ZeroHour/Data/INI/GameData.ini   the text "hello direct\n"
//   ZeroHour/generalszh.exe (any bytes), setup.exe and Movies/intro.bik (not selected)
//   Generals/INI.big              pattern file, 1 MB
// where byte i of a pattern file is pattern(i). It checks the mount, case insensitive lookup,
// directory listing, stat, sequential and random reads, seeks, FILE* and threads, and prints
// "DIRECT: ok/FAIL ..." lines and "DIRECT_DONE" at the end.
//
// Arguments: -waitchange (wait for Generals/INI.big to change on disk), -nothreads (skip the multi thread part), -full (also read the whole big file sequentially and report the speed),
//            -random N (number of random reads of the big file, default 3000)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>

#include <atomic>
#include <string>
#include <vector>

#include "WebDevice/Platform/WebPlatform.h"

static int s_failures = 0;

static void check(bool ok, const char *what, const std::string &detail = std::string())
{
	printf("DIRECT: %s %s %s\n", ok ? "ok  " : "FAIL", what, detail.c_str());
	if (!ok)
		++s_failures;
}

static std::string errnoText()
{
	return std::string("errno=") + std::to_string(errno) + " " + strerror(errno);
}

// ---- the pattern (the page writes the same bytes, see direct_test.html) ----

static const unsigned PERIOD = 1000003;
static std::vector<unsigned char> s_pattern;

static void makePattern()
{
	s_pattern.resize(PERIOD);
	for (unsigned k = 0; k < PERIOD; ++k)
		s_pattern[k] = (unsigned char)((k * 2654435761u) >> 16);
}

// Compares n bytes read from file offset `offset` with the pattern. Returns the first bad offset or -1.
static long long verify(const unsigned char *data, size_t n, long long offset)
{
	unsigned phase = (unsigned)(offset % PERIOD);
	for (size_t i = 0; i < n; ++i)
	{
		if (data[i] != s_pattern[phase])
			return offset + (long long)i;
		if (++phase == PERIOD)
			phase = 0;
	}
	return -1;
}

static uint32_t s_rng = 12345;
static uint32_t rnd()
{
	s_rng = s_rng * 1664525u + 1013904223u;
	return s_rng >> 8;
}

static double nowMs()
{
	return emscripten_get_now();
}

// Random reads of a pattern file with pread; returns the number of bad reads.
static int randomReads(const char *path, long long size, int count, uint32_t seed, size_t maxLen)
{
	uint32_t saved = s_rng;
	s_rng = seed;
	int fd = open(path, O_RDONLY);
	int bad = 0;
	std::vector<unsigned char> buf(maxLen);
	for (int i = 0; i < count && fd >= 0; ++i)
	{
		// Mostly small reads (directories, small assets), some big ones.
		size_t len = 1 + rnd() % ((rnd() % 8 == 0) ? maxLen : 16384);
		long long off = ((long long)rnd() * 4099 + rnd()) % (size > (long long)len ? size - (long long)len : 1);
		ssize_t n = pread(fd, buf.data(), len, off);
		if (n != (ssize_t)len)
		{
			++bad;
			continue;
		}
		if (verify(buf.data(), len, off) >= 0)
			++bad;
	}
	if (fd >= 0)
		close(fd);
	s_rng = saved;
	return bad;
}

struct ThreadArgs
{
	const char *path;
	long long size;
	int count;
	uint32_t seed;
	int bad;
};

static void *threadMain(void *p)
{
	ThreadArgs *a = static_cast<ThreadArgs *>(p);
	a->bad = randomReads(a->path, a->size, a->count, a->seed, 300000);
	return nullptr;
}

static int listCount(const char *path, const char *mustContain)
{
	DIR *d = opendir(path);
	if (!d)
		return -1;
	int n = 0;
	bool found = mustContain == nullptr;
	while (dirent *e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		++n;
		if (mustContain && strcmp(e->d_name, mustContain) == 0)
			found = true;
	}
	closedir(d);
	return found ? n : -2;
}

int main(int argc, char **argv)
{
	bool full = false, noThreads = false, waitChange = false;
	int randomCount = 3000;
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "-full") == 0)
			full = true;
		if (strcmp(argv[i], "-waitchange") == 0)
			waitChange = true;
		if (strcmp(argv[i], "-nothreads") == 0)
			noThreads = true;
		if (strcmp(argv[i], "-random") == 0 && i + 1 < argc)
			randomCount = atoi(argv[++i]);
	}

	// Pulls the GL library in, which the canvas hand-over to this thread needs.
	printf("DIRECT: webgl context %d\n", (int)emscripten_webgl_get_current_context());
	makePattern();

	const double t0 = nowMs();
	check(WebPlatform_MountStorage() == 0, "mount storage");
	printf("DIRECT: mount took %.1f ms\n", nowMs() - t0);
	check(WebPlatform_GetStorageMode() == WEBPLATFORM_STORAGE_DIRECT, "storage mode is direct", std::to_string(WebPlatform_GetStorageMode()));

	struct stat st;
	check(stat("/game", &st) == 0 && S_ISDIR(st.st_mode), "stat /game", errnoText());
	check(stat("/generals", &st) == 0 && S_ISDIR(st.st_mode), "stat /generals", errnoText());
	check(stat("/userdata", &st) == 0 && S_ISDIR(st.st_mode), "stat /userdata", errnoText());

	// Case insensitive lookup, Windows style names.
	check(access("/game/data/ini/gamedata.ini", F_OK) == 0, "access lower case", errnoText());
	check(access("/game/Data/INI/GameData.ini", F_OK) == 0, "access mixed case", errnoText());
	check(access("/game/DATA/ini/GAMEDATA.INI", F_OK) == 0, "access upper case", errnoText());
	check(access("/game/data/ini/missing.ini", F_OK) != 0 && errno == ENOENT, "missing file is ENOENT", errnoText());
	check(chdir("/game") == 0, "chdir /game", errnoText());
	check(access("Data/INI/GameData.ini", F_OK) == 0, "access relative mixed case", errnoText());
	check(access("INIZH.big", F_OK) == 0, "access INIZH.big", errnoText());
	check(access("setup.exe", F_OK) != 0 && access("Movies/intro.bik", F_OK) != 0, "files the game does not need are not there", "");
	check(access("generalszh.exe", F_OK) == 0, "the engine executable is there", errnoText());

	// Directory listing: the archives of the install are found by listing it.
	check(listCount("/game", "inizh.big") >= 2, "list /game finds inizh.big", std::to_string(listCount("/game", "inizh.big")));
	check(listCount("/game/Data", "ini") >= 1, "list /game/Data", std::to_string(listCount("/game/Data", "ini")));
	check(listCount("/generals", "ini.big") == 1, "list /generals", std::to_string(listCount("/generals", "ini.big")));

	// A small text file with stdio, as the INI reader does.
	{
		FILE *f = fopen("Data/INI/GameData.ini", "rb");
		char text[64] = {};
		size_t n = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
		check(f && n == 13 && strcmp(text, "hello direct\n") == 0, "fopen/fread small text file", errnoText() + " n=" + std::to_string(n));
		if (f)
			fclose(f);
	}

	// Sizes.
	long long iniSize = 0, texSize = 0, genSize = 0;
	if (stat("/game/inizh.big", &st) == 0) iniSize = st.st_size;
	if (stat("/game/texturesZH.big", &st) == 0) texSize = st.st_size;
	if (stat("/generals/INI.BIG", &st) == 0) genSize = st.st_size;
	printf("DIRECT: size inizh.big=%lld texturesZH.big=%lld ini.big=%lld\n", iniSize, texSize, genSize);
	check(iniSize == 3 * 1024 * 1024, "stat size of inizh.big");
	check(genSize == 1024 * 1024, "stat size of generals ini.big");
	check(texSize > 0, "stat size of texturesZH.big");

	// Sequential read of the small archive, in odd sized pieces.
	{
		int fd = open("/game/INIZH.BIG", O_RDONLY);
		check(fd >= 0, "open for reading", errnoText());
		std::vector<unsigned char> buf(70001);
		long long pos = 0;
		long long bad = -1;
		while (fd >= 0)
		{
			ssize_t n = read(fd, buf.data(), buf.size());
			if (n <= 0)
				break;
			if (bad < 0)
				bad = verify(buf.data(), (size_t)n, pos);
			pos += n;
		}
		check(pos == iniSize && bad < 0, "sequential read of inizh.big", "bytes=" + std::to_string(pos) + " firstBad=" + std::to_string(bad));
		// Seeking, including past the end.
		if (fd >= 0)
		{
			check(lseek(fd, 1234567, SEEK_SET) == 1234567, "lseek set");
			unsigned char b[100];
			check(read(fd, b, sizeof(b)) == 100 && verify(b, 100, 1234567) < 0, "read after lseek");
			check(lseek(fd, -50, SEEK_END) == iniSize - 50 && read(fd, b, sizeof(b)) == 50 && verify(b, 50, iniSize - 50) < 0, "read the last 50 bytes");
			check(read(fd, b, sizeof(b)) == 0, "read at EOF is 0");
			close(fd);
		}
	}

	// stdio with seeks, as the BIG loader does (header, then entries).
	{
		FILE *f = fopen("Data\\..\\INIZH.big", "rb");
		if (!f) f = fopen("INIZH.big", "rb");
		bool ok = f != nullptr;
		unsigned char b[4000];
		for (int i = 0; ok && i < 200; ++i)
		{
			long long off = (long long)rnd() % (iniSize - 4000);
			ok = fseek(f, (long)off, SEEK_SET) == 0 && fread(b, 1, sizeof(b), f) == sizeof(b) && verify(b, sizeof(b), off) < 0;
		}
		check(ok, "fseek/fread random");
		if (f)
			fclose(f);
	}

	// Read only.
	{
		errno = 0;
		int fd = open("/game/inizh.big", O_WRONLY);
		check(fd < 0, "open for writing is refused", errnoText());
		if (fd >= 0)
			close(fd);
		errno = 0;
		FILE *f = fopen("/game/Data/newfile.txt", "wb");
		bool wrote = f && fwrite("x", 1, 1, f) == 1 && fflush(f) == 0;
		check(!wrote, "writing a new file in /game fails", errnoText());
		if (f)
			fclose(f);
	}

	// User data is writable and persistent (OPFS).
	{
		FILE *f = fopen("/userdata/direct_test.txt", "wb");
		bool ok = f && fwrite("abc", 1, 3, f) == 3 && fflush(f) == 0;
		check(ok, "write /userdata", errnoText());
		if (f)
			fclose(f);
		unlink("/userdata/direct_test.txt");
	}

	// Random reads of the big file.
	if (texSize > 0)
	{
		const double r0 = nowMs();
		int bad = randomReads("/game/texturesZH.big", texSize, randomCount, 777, 300000);
		const double dt = nowMs() - r0;
		check(bad == 0, "random reads of texturesZH.big", std::to_string(randomCount) + " reads, " + std::to_string(bad) + " bad, " + std::to_string(dt) + " ms");
		printf("DIRECT: random reads: %.2f ms per read\n", dt / randomCount);

		// Threads (the audio stream thread reads files, too).
		if (!noThreads)
		{
		pthread_t th[3];
		ThreadArgs args[3];
		const double t1 = nowMs();
		for (int i = 0; i < 3; ++i)
		{
			args[i] = { "/game/texturesZH.big", texSize, randomCount / 3, 1000u + i, 0 };
			pthread_create(&th[i], nullptr, threadMain, &args[i]);
		}
		int badThreads = 0;
		for (int i = 0; i < 3; ++i)
		{
			pthread_join(th[i], nullptr);
			badThreads += args[i].bad;
		}
		check(badThreads == 0, "random reads from 3 other threads", std::to_string(badThreads) + " bad, " + std::to_string(nowMs() - t1) + " ms");
		}
	}

	// One big read (straight into the heap, no cache) and one that starts and ends inside blocks.
	if (texSize > 8 * 1024 * 1024)
	{
		std::vector<unsigned char> big(3 * 1024 * 1024 + 11);
		int fd = open("/game/texturesZH.big", O_RDONLY);
		ssize_t n = fd >= 0 ? pread(fd, big.data(), big.size(), 7) : -1;
		check(n == (ssize_t)big.size() && verify(big.data(), big.size(), 7) < 0, "3 MB read at an odd offset", errnoText());
		n = fd >= 0 ? pread(fd, big.data(), 700001, 262144 - 5) : -1;
		check(n == 700001 && verify(big.data(), 700001, 262144 - 5) < 0, "700 KB read across blocks", errnoText());
		// A read that crosses the end of the file returns what is left.
		n = fd >= 0 ? pread(fd, big.data(), 5000, texSize - 1234) : -1;
		check(n == 1234 && verify(big.data(), 1234, texSize - 1234) < 0, "read across the end of the file", std::to_string(n));
		if (fd >= 0)
			close(fd);
	}

	// Sequential read of the whole big file.
	if (full && texSize > 0)
	{
		const size_t chunk = 64 * 1024;
		std::vector<unsigned char> buf(chunk);
		int fd = open("/game/texturesZH.big", O_RDONLY);
		long long pos = 0, bad = -1;
		const double s0 = nowMs();
		size_t heap0 = emscripten_get_heap_size();
		size_t heapMax = heap0;
		while (fd >= 0)
		{
			ssize_t n = read(fd, buf.data(), chunk);
			if (n <= 0)
				break;
			if (bad < 0)
				bad = verify(buf.data(), (size_t)n, pos);
			pos += n;
			if ((pos & 0x3FFFFFF) == 0)
				heapMax = emscripten_get_heap_size() > heapMax ? emscripten_get_heap_size() : heapMax;
		}
		const double dt = nowMs() - s0;
		if (fd >= 0)
			close(fd);
		check(pos == texSize && bad < 0, "sequential read of the whole texturesZH.big", "bytes=" + std::to_string(pos) + " firstBad=" + std::to_string(bad));
		printf("DIRECT: whole file: %.1f MB/s, wasm heap %zu MB -> %zu MB\n", pos / 1e6 / (dt / 1000.0), heap0 >> 20, heapMax >> 20);
	}

	// A file that changes on disk after the folder was opened: the page's File object is stale and the
	// read fails with EIO (the harness appends to Generals/INI.big when it sees the first line).
	if (waitChange)
	{
		printf("DIRECT: waiting for change\n");
		fflush(stdout);
		int fd = open("/generals/ini.big", O_RDONLY);
		bool failed = false;
		unsigned char b[16];
		for (int i = 0; i < 100 && !failed && fd >= 0; ++i)
		{
			usleep(100 * 1000);
			// Past the cached block: the cache would still serve what it has.
			failed = pread(fd, b, sizeof(b), (off_t)(i % 4) * 300000 + 100000) < 0;
			if (failed)
				check(errno == EIO, "a changed file fails with EIO", errnoText());
		}
		check(failed, "the change of a file on disk is detected");
		if (fd >= 0)
			close(fd);
	}

	char stats[512];
	if (WebPlatform_GetDirectStats(stats, sizeof(stats)) > 0)
		printf("DIRECT: stats %s\n", stats);

	printf("DIRECT: %d failures\n", s_failures);
	printf("DIRECT_DONE\n");
	return s_failures ? 1 : 0;
}
