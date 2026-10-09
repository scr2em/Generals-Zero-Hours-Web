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

// FILE: NativePlatform.cpp ///////////////////////////////////////////////////
//
// The platform layer (WebDevice/Platform/WebPlatform.h) of the native headless build
// (cmake/native-headless.cmake), in place of the browser's WebPlatform.cpp and WebStorage.cpp.
//
// There is no page: no input, no canvas, nothing to yield to. The message queue works (the game
// posts messages to itself), the window is a token, and the time is the clock that WebCompat's
// GetTickCount() reads. The game files are not mounted anywhere: they are read in place from the
// directories that NativeMain.cpp hands to WebCompat (webcompat_folders.h) before the game starts.
//
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>

#include "WebDevice/Platform/WebPlatform.h"

#include <condition_variable>
#include <deque>
#include <mutex>

namespace
{

// The one "window". Any value that is not 0 does.
const uintptr_t s_windowHandle = 0x00C0FFEE;

struct PlatformState
{
	std::mutex lock;
	std::condition_variable messageArrived;
	std::deque<WebPlatformMsg> messages;
	WebWindowProc windowProc = nullptr;
	int clientWidth = 800;
	int clientHeight = 600;
};

PlatformState &state()
{
	static PlatformState s;
	return s;
}

void pushMessage(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	PlatformState &s = state();
	WebPlatformMsg msg = {};
	msg.hwnd = s_windowHandle;
	msg.message = message;
	msg.wParam = wParam;
	msg.lParam = lParam;
	msg.time = WebPlatform_GetTimeMs();
	{
		std::lock_guard<std::mutex> guard(s.lock);
		s.messages.push_back(msg);
	}
	s.messageArrived.notify_all();
}

} // namespace

// ---- life cycle --------------------------------------------------------------------------------

extern "C" int WebPlatform_Init(const char *) { return 1; }
extern "C" void WebPlatform_Shutdown(void) {}
extern "C" const char *WebPlatform_GetCanvasSelector(void) { return "#canvas"; }
extern "C" void WebPlatform_NotifyExit(int) {}

// ---- the window --------------------------------------------------------------------------------

extern "C" uintptr_t WebPlatform_GetWindow(void) { return s_windowHandle; }

extern "C" void WebPlatform_SetWindowProc(WebWindowProc proc)
{
	PlatformState &s = state();
	std::lock_guard<std::mutex> guard(s.lock);
	s.windowProc = proc;
}

extern "C" void WebPlatform_SetTitle(const char *) {}

extern "C" void WebPlatform_SetClientSize(int width, int height)
{
	PlatformState &s = state();
	{
		std::lock_guard<std::mutex> guard(s.lock);
		s.clientWidth = width;
		s.clientHeight = height;
	}
	pushMessage(WEBWM_SIZE, 0, (intptr_t)(((uint32_t)height << 16) | ((uint32_t)width & 0xFFFF)));
}

extern "C" void WebPlatform_GetClientSize(int *width, int *height)
{
	PlatformState &s = state();
	std::lock_guard<std::mutex> guard(s.lock);
	if (width) *width = s.clientWidth;
	if (height) *height = s.clientHeight;
}

extern "C" void WebPlatform_SetCursorVisible(int) {}
extern "C" int WebPlatform_IsActive(void) { return 1; }
extern "C" void WebPlatform_PointerLeft(void) {}
extern "C" void WebPlatform_RequestScreenshot(void) { pushMessage(WEBWM_APP_SCREENSHOT, 0, 0); }

// ---- mouse cursors -----------------------------------------------------------------------------

extern "C" int WebPlatform_CursorLoad(const void *, int) { return 0; }
extern "C" void WebPlatform_CursorSet(int) {}

// ---- yielding: nothing to yield to -------------------------------------------------------------

extern "C" void WebPlatform_SetYieldEnabled(int) {}
extern "C" int WebPlatform_CanYield(void) { return 0; }
extern "C" void WebPlatform_BeginFrame(void) {}
extern "C" void WebPlatform_EndFrame(void) {}
extern "C" void WebPlatform_WaitFrame(void) {}
extern "C" void WebPlatform_YieldFrame(void) {}
extern "C" void WebPlatform_FramePresented(void) {}
extern "C" void WebPlatform_SetYieldLog(int) {}
extern "C" double WebPlatform_GetYieldedMs(void) { return 0.0; }
extern "C" unsigned WebPlatform_GetYieldCount(void) { return 0; }

// ---- the message pump --------------------------------------------------------------------------

extern "C" int WebPlatform_PeekMessage(WebPlatformMsg *msg, int remove)
{
	PlatformState &s = state();
	std::lock_guard<std::mutex> guard(s.lock);
	if (s.messages.empty())
		return 0;
	if (msg)
		*msg = s.messages.front();
	if (remove)
		s.messages.pop_front();
	return 1;
}

extern "C" int WebPlatform_GetMessage(WebPlatformMsg *msg)
{
	PlatformState &s = state();
	std::unique_lock<std::mutex> guard(s.lock);
	s.messageArrived.wait(guard, [&s] { return !s.messages.empty(); });
	const WebPlatformMsg front = s.messages.front();
	s.messages.pop_front();
	if (msg)
		*msg = front;
	return front.message == WEBWM_QUIT ? 0 : 1;
}

extern "C" int WebPlatform_PostMessage(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	pushMessage(message, wParam, lParam);
	return 1;
}

extern "C" void WebPlatform_PostQuitMessage(int exitCode)
{
	pushMessage(WEBWM_QUIT, (uintptr_t)exitCode, 0);
}

extern "C" intptr_t WebPlatform_SendMessage(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	WebWindowProc proc;
	{
		PlatformState &s = state();
		std::lock_guard<std::mutex> guard(s.lock);
		proc = s.windowProc;
	}
	return proc ? proc(s_windowHandle, message, wParam, lParam) : 0;
}

extern "C" intptr_t WebPlatform_DispatchMessage(const WebPlatformMsg *msg)
{
	if (!msg)
		return 0;
	return WebPlatform_SendMessage(msg->message, msg->wParam, msg->lParam);
}

// ---- keyboard: never a key -----------------------------------------------------------------------

extern "C" int WebPlatform_PopKeyEvent(WebKeyEvent *) { return 0; }
extern "C" void WebPlatform_RequestClose(void) { pushMessage(WEBWM_CLOSE, 0, 0); }
extern "C" void WebPlatform_SetInputLog(int) {}
extern "C" void WebPlatform_ResetKeys(void) {}
extern "C" int WebPlatform_GetKeyState(int) { return 0; }

// ---- time ----------------------------------------------------------------------------------------

extern "C" uint32_t WebPlatform_GetTimeMs(void)
{
	// The game compares message times with timeGetTime(), which is GetTickCount() in WebCompat.
	return (uint32_t)GetTickCount();
}

// ---- storage: the files are read in place ------------------------------------------------------

extern "C" int WebPlatform_MountStorage(void) { return 0; }
extern "C" int WebPlatform_GetStorageMode(void) { return WEBPLATFORM_STORAGE_DIRECT; }
extern "C" void WebPlatform_RequireDirectStorage(int) {}
extern "C" int WebPlatform_GetDirectStats(char *buffer, int capacity)
{
	static const char s_none[] = "{}";
	if (!buffer || capacity < (int)sizeof(s_none))
		return -1;
	memcpy(buffer, s_none, sizeof(s_none));
	return (int)sizeof(s_none) - 1;
}
