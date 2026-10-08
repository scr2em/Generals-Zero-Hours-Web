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

// FILE: WebWin32Shim.cpp /////////////////////////////////////////////////////
//
// The Win32 window, message and input calls the game makes, answered by the
// browser platform layer (WebPlatform.h). Dependencies/WebCompat declares these
// functions in windows.h; the definitions here are weak so that WebCompat can
// supply its own, which should then forward to the same WebPlatform_* calls.
//
///////////////////////////////////////////////////////////////////////////////

#include <windows.h>

#include "WebDevice/Platform/WebPlatform.h"

#define WEB_WEAK __attribute__((weak))

static_assert(sizeof(MSG) == sizeof(WebPlatformMsg), "MSG and WebPlatformMsg must have the same layout");
static_assert(offsetof(MSG, message) == offsetof(WebPlatformMsg, message), "MSG layout");
static_assert(offsetof(MSG, time) == offsetof(WebPlatformMsg, time), "MSG layout");
static_assert(offsetof(MSG, pt) == offsetof(WebPlatformMsg, ptX), "MSG layout");

namespace
{

// PM_REMOVE
const UINT REMOVE_FLAG = 0x0001;

HCURSOR s_currentCursor = nullptr;

}

extern "C"
{

WEB_WEAK BOOL WINAPI PeekMessageA(LPMSG lpMsg, HWND, UINT, UINT, UINT wRemoveMsg)
{
	return WebPlatform_PeekMessage(reinterpret_cast<WebPlatformMsg *>(lpMsg), (wRemoveMsg & REMOVE_FLAG) != 0) ? TRUE : FALSE;
}

WEB_WEAK BOOL WINAPI GetMessageA(LPMSG lpMsg, HWND, UINT, UINT)
{
	return WebPlatform_GetMessage(reinterpret_cast<WebPlatformMsg *>(lpMsg)) ? TRUE : FALSE;
}

WEB_WEAK BOOL WINAPI WaitMessage(void)
{
	WebPlatformMsg msg;
	while (!WebPlatform_PeekMessage(&msg, 0))
	{
		// GetMessage would remove the message, so poll with a short sleep
		usleep(1000);
	}
	return TRUE;
}

// WM_CHAR is produced by the platform layer from the browser's key events.
WEB_WEAK BOOL WINAPI TranslateMessage(const MSG *)
{
	return FALSE;
}

WEB_WEAK LRESULT WINAPI DispatchMessageA(const MSG *lpMsg)
{
	return WebPlatform_DispatchMessage(reinterpret_cast<const WebPlatformMsg *>(lpMsg));
}

WEB_WEAK BOOL WINAPI PostMessageA(HWND, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return WebPlatform_PostMessage(Msg, wParam, lParam) ? TRUE : FALSE;
}

WEB_WEAK LRESULT WINAPI SendMessageA(HWND, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return WebPlatform_SendMessage(Msg, wParam, lParam);
}

WEB_WEAK void WINAPI PostQuitMessage(int nExitCode)
{
	WebPlatform_PostQuitMessage(nExitCode);
}

// The window is the canvas, at (0,0) of the "screen", so window, client and
// screen coordinates are the same.

WEB_WEAK BOOL WINAPI GetClientRect(HWND, LPRECT lpRect)
{
	int width, height;
	WebPlatform_GetClientSize(&width, &height);
	lpRect->left = 0;
	lpRect->top = 0;
	lpRect->right = width;
	lpRect->bottom = height;
	return TRUE;
}

WEB_WEAK BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect)
{
	return GetClientRect(hWnd, lpRect);
}

WEB_WEAK BOOL WINAPI ClientToScreen(HWND, LPPOINT)
{
	return TRUE;
}

WEB_WEAK BOOL WINAPI ScreenToClient(HWND, LPPOINT)
{
	return TRUE;
}

WEB_WEAK BOOL WINAPI IsIconic(HWND)
{
	return FALSE;
}

WEB_WEAK BOOL WINAPI SetWindowTextA(HWND, LPCSTR lpString)
{
	WebPlatform_SetTitle(lpString);
	return TRUE;
}

WEB_WEAK SHORT WINAPI GetKeyState(int nVirtKey)
{
	return static_cast<SHORT>(WebPlatform_GetKeyState(nVirtKey));
}

WEB_WEAK SHORT WINAPI GetAsyncKeyState(int vKey)
{
	return static_cast<SHORT>(WebPlatform_GetKeyState(vKey) & 0x8000);
}

// A null cursor means the game draws the cursor itself.
WEB_WEAK HCURSOR WINAPI SetCursor(HCURSOR hCursor)
{
	HCURSOR previous = s_currentCursor;
	s_currentCursor = hCursor;
	WebPlatform_SetCursorVisible(hCursor != nullptr);
	return previous;
}

WEB_WEAK BOOL WINAPI ClipCursor(const RECT *)
{
	return TRUE;
}

} // extern "C"
