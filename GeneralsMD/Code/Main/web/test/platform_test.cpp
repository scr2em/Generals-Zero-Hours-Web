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

// FILE: platform_test.cpp ////////////////////////////////////////////////////
//
// A tiny stand-in for the game that exercises the browser plumbing: the engine
// thread (-sPROXY_TO_PTHREAD), OPFS mounted through WasmFS, and the input
// message queue. It prints "TEST:" lines that web/test/e2e.mjs checks.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Platform/WebPlatform.h"

#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

static const char *messageName(uint32_t m)
{
	switch (m)
	{
		case WEBWM_SIZE: return "WM_SIZE";
		case WEBWM_ACTIVATE: return "WM_ACTIVATE";
		case WEBWM_SETFOCUS: return "WM_SETFOCUS";
		case WEBWM_KILLFOCUS: return "WM_KILLFOCUS";
		case WEBWM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
		case WEBWM_CLOSE: return "WM_CLOSE";
		case WEBWM_KEYDOWN: return "WM_KEYDOWN";
		case WEBWM_KEYUP: return "WM_KEYUP";
		case WEBWM_CHAR: return "WM_CHAR";
		case WEBWM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
		case WEBWM_SYSKEYUP: return "WM_SYSKEYUP";
		case WEBWM_MOUSEMOVE: return "WM_MOUSEMOVE";
		case WEBWM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
		case WEBWM_LBUTTONUP: return "WM_LBUTTONUP";
		case WEBWM_LBUTTONDBLCLK: return "WM_LBUTTONDBLCLK";
		case WEBWM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
		case WEBWM_RBUTTONUP: return "WM_RBUTTONUP";
		case WEBWM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
		case WEBWM_MBUTTONUP: return "WM_MBUTTONUP";
		case WEBWM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
		case 0x0400 + 1: return "WM_USER+1";
		default: return "WM_?";
	}
}

static int s_quit = 0;
static int s_mouseX = 0, s_mouseY = 0;
static int s_clientW = 640, s_clientH = 480;
static int s_buttons = 0;

static intptr_t windowProc(uintptr_t hwnd, uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	if (message == WEBWM_MOUSEMOVE || (message >= WEBWM_LBUTTONDOWN && message <= WEBWM_MBUTTONDBLCLK))
	{
		s_mouseX = (int)(int16_t)(lParam & 0xFFFF);
		s_mouseY = (int)(int16_t)((lParam >> 16) & 0xFFFF);
		s_buttons = (int)(wParam & 0x13);
		printf("TEST: MSG %s x=%d y=%d wp=0x%x\n", messageName(message), (int)(lParam & 0xFFFF), (int)((lParam >> 16) & 0xFFFF), (unsigned)wParam);
	}
	else if (message == WEBWM_MOUSEWHEEL)
	{
		printf("TEST: MSG %s delta=%d x=%d y=%d\n", messageName(message), (int)(int16_t)((wParam >> 16) & 0xFFFF), (int)(lParam & 0xFFFF), (int)((lParam >> 16) & 0xFFFF));
	}
	else
	{
		printf("TEST: MSG %s wp=0x%x lp=0x%x\n", messageName(message), (unsigned)wParam, (unsigned)lParam);
	}

	if (message == WEBWM_KEYDOWN && wParam == 0x1B)	// VK_ESCAPE
		s_quit = 1;
	return 0;
}

static void *poster(void *)
{
	usleep(300 * 1000);
	WebPlatform_PostMessage(0x0400 + 1, 42, 4242);
	return nullptr;
}

static void listDirectory(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == nullptr)
	{
		printf("TEST: cannot open directory %s\n", path);
		return;
	}
	while (dirent *entry = readdir(dir))
	{
		if (entry->d_name[0] != '.')
			printf("TEST: %s/%s\n", path, entry->d_name);
	}
	closedir(dir);
}

// The launcher passes "-army <path>" for every army package the player checked. Stand-in for the engine's
// loader: opens each package, prints its size and first bytes (proves the file is readable at that path, also in
// the middle) and writes /userdata/ArmyReport.json the way the engine does (every package "loaded", id = file
// name without its extension; a package whose name contains "skip" is reported as skipped).
static void loadArmies(int argc, char **argv)
{
	std::string report = "{\"ruleset\":\"test\",\"packages\":[";
	int count = 0;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (strcmp(argv[i], "-army") != 0)
			continue;
		const char *path = argv[++i];
		struct stat st = {};
		const int fd = open(path, O_RDONLY);
		unsigned char head[4] = {}, mid[4] = {};
		if (fd < 0 || fstat(fd, &st) != 0)
		{
			printf("TEST: army %s cannot open (%s)\n", path, strerror(errno));
			if (fd >= 0)
				close(fd);
			continue;
		}
		const ssize_t n = read(fd, head, sizeof(head));
		const off_t middle = st.st_size > 8 ? st.st_size / 2 : 0;
		const ssize_t m = pread(fd, mid, sizeof(mid), middle);
		close(fd);
		printf("TEST: army %s size=%lld first=%02x%02x%02x%02x(%d) middle@%lld=%02x%02x%02x%02x(%d)\n", path, (long long)st.st_size,
			head[0], head[1], head[2], head[3], (int)n, (long long)middle, mid[0], mid[1], mid[2], mid[3], (int)m);

		std::string id = path;
		id = id.substr(id.rfind('/') + 1);
		if (id.rfind('.') != std::string::npos)
			id = id.substr(0, id.rfind('.'));
		const bool skipped = id.find("skip") != std::string::npos;
		if (count++)
			report += ",";
		report += "{\"id\":\"" + id + "\",\"path\":\"" + path + "\",\"status\":\"" + (skipped ? "skipped" : "loaded") + "\",\"reason\":\"" +
			(skipped ? "test: needs another ruleset" : "") + "\",\"factions\":[{\"displayName\":\"Faction of " + id + "\",\"ai\":true}]}";
		if (skipped)
			printf("TEST: ZHARMY: %s skipped: test: needs another ruleset\n", id.c_str());
		else
			printf("TEST: ZHARMY: %s loaded\n", id.c_str());
	}
	report += "]}\n";
	FILE *f = fopen("/userdata/ArmyReport.json", "wb");
	if (f)
	{
		fwrite(report.data(), 1, report.size(), f);
		fclose(f);
	}
	printf("TEST: armies %d\n", count);
}

int main(int argc, char **argv)
{
	printf("TEST: start argc=%d\n", argc);
	for (int i = 1; i < argc; ++i)
		printf("TEST: arg %d=%s\n", i, argv[i]);

	// The game puts large buffers on the stack.
	{
		volatile char *big = (volatile char *)alloca(6 * 1024 * 1024);
		big[0] = 1;
		big[6 * 1024 * 1024 - 1] = 2;
		printf("TEST: stack ok %d\n", big[0] + big[6 * 1024 * 1024 - 1]);
	}

	printf("TEST: init=%d selector=%s\n", WebPlatform_Init("#canvas"), WebPlatform_GetCanvasSelector());
	WebPlatform_SetWindowProc(windowProc);

	const int mount = WebPlatform_MountStorage();
	printf("TEST: mount=%d\n", mount);

	if (mount == 0)
	{
		// A file imported by the page, opened with different case than stored.
		FILE *f = fopen("/game/Data/INI/GameData.ini", "rb");
		if (f)
		{
			char buffer[256] = {};
			const size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
			fclose(f);
			printf("TEST: read %u bytes: %s\n", (unsigned)n, buffer);
		}
		else
		{
			printf("TEST: cannot open GameData.ini\n");
		}

		listDirectory("/game");
		listDirectory("/game/data");
		listDirectory("/generals");
		listDirectory("/armies");

		// User data persists in OPFS.
		FILE *counter = fopen("/userdata/Options.ini", "rb");
		int runs = 0;
		if (counter)
		{
			if (fscanf(counter, "%d", &runs) != 1)
				runs = 0;
			fclose(counter);
		}
		runs++;
		counter = fopen("/userdata/Options.ini", "wb");
		if (counter)
		{
			fprintf(counter, "%d\n", runs);
			fclose(counter);
		}
		printf("TEST: userdata runs=%d\n", runs);
		loadArmies(argc, argv);
	}

	int w, h;
	WebPlatform_GetClientSize(&w, &h);
	printf("TEST: client %dx%d\n", w, h);
	// WebGL2 from this thread on the canvas the page transferred to us.
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl = 0;
	{
		EmscriptenWebGLContextAttributes attributes;
		emscripten_webgl_init_context_attributes(&attributes);
		attributes.majorVersion = 2;
		attributes.explicitSwapControl = true;
		attributes.alpha = false;
		emscripten_set_canvas_element_size(WebPlatform_GetCanvasSelector(), s_clientW, s_clientH);
		gl = emscripten_webgl_create_context(WebPlatform_GetCanvasSelector(), &attributes);
		const EMSCRIPTEN_RESULT made = gl ? emscripten_webgl_make_context_current(gl) : -1;
		printf("TEST: webgl context=%d current=%d version=%s\n", (int)gl, (int)made, gl ? (const char *)glGetString(GL_VERSION) : "none");
	}
	printf("TEST: active=%d window=%u\n", WebPlatform_IsActive(), (unsigned)WebPlatform_GetWindow());

	// Blocking GetMessage woken by another thread.
	pthread_t thread;
	pthread_create(&thread, nullptr, poster, nullptr);
	WebPlatformMsg msg;
	const int got = WebPlatform_GetMessage(&msg);
	// A WM_SIZE or activation message may be first; drain until ours.
	int guard = 0;
	while (got && msg.message != 0x0401 && guard++ < 16)
		WebPlatform_GetMessage(&msg);
	printf("TEST: GetMessage woke: message=0x%x wp=%u lp=%d\n", msg.message, (unsigned)msg.wParam, (int)msg.lParam);
	pthread_join(thread, nullptr);

	// the game picks its resolution after start-up; this posts a WM_SIZE
	WebPlatform_SetClientSize(s_clientW, s_clientH);

	printf("TEST: ready\n");

	// A busy main loop that never returns to the event loop, like the game's.
	uint32_t lastKeyReport = 0;
	for (int frame = 0; !s_quit && frame < 60 * 120; ++frame)
	{
		WebPlatformMsg m;
		while (WebPlatform_PeekMessage(&m, 0))
		{
			WebPlatform_GetMessage(&m);
			WebPlatform_DispatchMessage(&m);
		}

		WebKeyEvent key;
		while (WebPlatform_PopKeyEvent(&key))
		{
			printf("TEST: DIK 0x%02x %s\n", key.dik, key.down ? "down" : "up");
			lastKeyReport = key.time;
		}
		(void)lastKeyReport;

		if (gl)
		{
			// the colour follows the mouse; a button turns it brighter
			glViewport(0, 0, s_clientW, s_clientH);
			glClearColor(s_mouseX / (float)s_clientW, s_mouseY / (float)s_clientH, s_buttons ? 1.0f : 0.35f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			emscripten_webgl_commit_frame();
		}

		usleep(16 * 1000);
	}

	printf("TEST: shift=%d ctrl=%d\n", WebPlatform_GetKeyState(0x10), WebPlatform_GetKeyState(0x11));
	printf("TEST: done\n");
	WebPlatform_NotifyExit(0);
	return 0;
}
