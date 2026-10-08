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
** WebAssembly port: windows, input state, GDI and the clipboard.
**
** The game has one window, the canvas of the page. A window is a small object
** that remembers its window procedure, style and size. The message queue,
** the window geometry and the keyboard and cursor state are not here: the
** platform layer (Core/GameEngineDevice/Source/WebDevice/Platform) defines
** PeekMessage, GetMessage, PostMessage, SendMessage, GetClientRect, GetKeyState
** and the like itself. GDI drawing does nothing: there is no device context
** to draw on.
*/
#include "webcompat_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <map>
#include <set>
#include <string>

using namespace WebCompat;

namespace
{

struct WindowObject
{
	WindowObject() : style(0), exStyle(0), procedure(nullptr), instance(nullptr), id(0), userData(0), visible(false), enabled(true), parent(nullptr)
	{
		memset(&rect, 0, sizeof(rect));
	}

	std::string className;
	std::string title;
	DWORD style;
	DWORD exStyle;
	WNDPROC procedure;
	HINSTANCE instance;
	LONG id;
	LONG userData;
	bool visible;
	bool enabled;
	RECT rect;
	HWND parent;
};

pthread_mutex_t s_windowLock = PTHREAD_MUTEX_INITIALIZER;

struct WindowState
{
	WindowState() : screenWidth(800), screenHeight(600), cursorVisible(0), focus(nullptr), activeWindow(nullptr), capture(nullptr), clipboardData(nullptr), quitPosted(false), nextClassAtom(0xC000), nextMessageId(0xC000), nextTimerId(1)
	{
		cursorPosition.x = 0;
		cursorPosition.y = 0;
		memset(keyDown, 0, sizeof(keyDown));
	}

	std::set<WindowObject *> windows;
	std::map<std::string, WNDPROC> classProcedures;
	std::map<std::string, UINT> registeredMessages;
	WindowObject desktop;
	int screenWidth;
	int screenHeight;
	POINT cursorPosition;
	int cursorVisible;
	bool keyDown[256];
	HWND focus;
	HWND activeWindow;
	HWND capture;
	HANDLE clipboardData;
	bool quitPosted;
	WORD nextClassAtom;
	UINT nextMessageId;
	UINT_PTR nextTimerId;
};

WindowState &State()
{
	static WindowState state;
	return state;
}

struct WindowLock
{
	WindowLock() { pthread_mutex_lock(&s_windowLock); }
	~WindowLock() { pthread_mutex_unlock(&s_windowLock); }
};

// Returns the window (the desktop included) for a handle, or null.
WindowObject *Lookup(HWND window)
{
	WindowState &state = State();
	WindowObject *object = reinterpret_cast<WindowObject *>(window);
	if (object == &state.desktop)
		return object;
	return state.windows.find(object) != state.windows.end() ? object : nullptr;
}

// GDI objects are distinct handles that carry nothing but their kind, size and
// pixels (DIB sections only).
enum GdiKind { GDI_STOCK = 1, GDI_FONT, GDI_BITMAP, GDI_BRUSH, GDI_DC };

struct GdiObject
{
	GdiKind kind;
	int width;
	int height;
	void *bits;
};

GdiObject *NewGdiObject(GdiKind kind, int width = 0, int height = 0, void *bits = nullptr)
{
	GdiObject *object = new GdiObject();
	object->kind = kind;
	object->width = width;
	object->height = height;
	object->bits = bits;
	return object;
}

// Text has a fixed 8x16 cell.
const int CELL_WIDTH = 8;
const int CELL_HEIGHT = 16;

HDC AsDC(GdiObject *object) { return reinterpret_cast<HDC>(object); }

} // namespace

extern "C" {

/* ---------------------------------------------------------------------------
** Hooks for the platform layer
** ------------------------------------------------------------------------- */

void webcompat_set_screen_size(int width, int height)
{
	WindowLock lock;
	WindowState &state = State();
	state.screenWidth = width;
	state.screenHeight = height;
	state.desktop.rect.right = width;
	state.desktop.rect.bottom = height;
	// The top level windows cover the canvas.
	for (std::set<WindowObject *>::iterator it = state.windows.begin(); it != state.windows.end(); ++it)
	{
		if (!(*it)->parent)
		{
			(*it)->rect.right = (*it)->rect.left + width;
			(*it)->rect.bottom = (*it)->rect.top + height;
		}
	}
}

void webcompat_set_cursor_position(int x, int y)
{
	WindowLock lock;
	State().cursorPosition.x = x;
	State().cursorPosition.y = y;
}

void webcompat_set_key_state(int virtualKey, int down)
{
	WindowLock lock;
	if (virtualKey >= 0 && virtualKey < 256)
		State().keyDown[virtualKey] = down != 0;
}

/* ---------------------------------------------------------------------------
** Windows
** ------------------------------------------------------------------------- */

ATOM WINAPI RegisterClassA(const WNDCLASSA *lpWndClass)
{
	if (!lpWndClass || !lpWndClass->lpszClassName)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	WindowLock lock;
	WindowState &state = State();
	state.classProcedures[lpWndClass->lpszClassName] = lpWndClass->lpfnWndProc;
	return state.nextClassAtom++;
}

BOOL WINAPI UnregisterClassA(LPCSTR lpClassName, HINSTANCE)
{
	WindowLock lock;
	return lpClassName && State().classProcedures.erase(lpClassName) > 0;
}

HWND WINAPI CreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID)
{
	WindowObject *window = new WindowObject();
	window->className = (lpClassName && !IS_INTRESOURCE(lpClassName)) ? lpClassName : "";
	window->title = lpWindowName ? lpWindowName : "";
	window->style = dwStyle;
	window->exStyle = dwExStyle;
	window->parent = hWndParent;
	window->instance = hInstance;
	window->id = (dwStyle & WS_CHILD) ? (LONG)(LONG_PTR)hMenu : 0;
	window->visible = (dwStyle & WS_VISIBLE) != 0;

	WindowLock lock;
	WindowState &state = State();
	std::map<std::string, WNDPROC>::iterator found = state.classProcedures.find(window->className);
	window->procedure = found != state.classProcedures.end() ? found->second : nullptr;
	window->rect.left = X == CW_USEDEFAULT ? 0 : X;
	window->rect.top = Y == CW_USEDEFAULT ? 0 : Y;
	window->rect.right = window->rect.left + ((nWidth == CW_USEDEFAULT || nWidth == 0) ? state.screenWidth : nWidth);
	window->rect.bottom = window->rect.top + ((nHeight == CW_USEDEFAULT || nHeight == 0) ? state.screenHeight : nHeight);
	state.windows.insert(window);
	HWND handle = reinterpret_cast<HWND>(window);
	if (!state.activeWindow)
		state.activeWindow = handle;
	if (!state.focus)
		state.focus = handle;
	return handle;
}

BOOL WINAPI DestroyWindow(HWND hWnd)
{
	WNDPROC procedure = nullptr;
	{
		WindowLock lock;
		WindowObject *window = Lookup(hWnd);
		if (!window || window == &State().desktop)
		{
			SetLastError(1400); // ERROR_INVALID_WINDOW_HANDLE
			return FALSE;
		}
		procedure = window->procedure;
	}
	if (procedure)
	{
		procedure(hWnd, WM_DESTROY, 0, 0);
		procedure(hWnd, WM_NCDESTROY, 0, 0);
	}
	WindowLock lock;
	WindowState &state = State();
	WindowObject *window = Lookup(hWnd);
	if (window)
	{
		state.windows.erase(window);
		if (state.focus == hWnd) state.focus = nullptr;
		if (state.activeWindow == hWnd) state.activeWindow = nullptr;
		if (state.capture == hWnd) state.capture = nullptr;
		delete window;
	}
	return TRUE;
}

LRESULT WINAPI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM, LPARAM)
{
	if (Msg == WM_CLOSE)
		DestroyWindow(hWnd);
	else if (Msg == WM_NCHITTEST)
		return HTCLIENT;
	return 0;
}

LRESULT WINAPI CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return lpPrevWndFunc ? lpPrevWndFunc(hWnd, Msg, wParam, lParam) : 0;
}

BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window)
		return FALSE;
	const bool wasVisible = window->visible;
	window->visible = nCmdShow != SW_HIDE;
	return wasVisible;
}

BOOL WINAPI UpdateWindow(HWND hWnd)
{
	WindowLock lock;
	return Lookup(hWnd) != nullptr;
}

BOOL WINAPI IsWindow(HWND hWnd)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	return window && window != &State().desktop;
}

BOOL WINAPI IsWindowVisible(HWND hWnd)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	return window && window->visible;
}

BOOL WINAPI IsZoomed(HWND)
{
	return FALSE;
}

HWND WINAPI SetFocus(HWND hWnd)
{
	WindowLock lock;
	HWND previous = State().focus;
	State().focus = hWnd;
	return previous;
}

HWND WINAPI GetFocus(void)
{
	WindowLock lock;
	return State().focus;
}

HWND WINAPI GetActiveWindow(void)
{
	WindowLock lock;
	return State().activeWindow;
}

HWND WINAPI SetActiveWindow(HWND hWnd)
{
	WindowLock lock;
	HWND previous = State().activeWindow;
	State().activeWindow = hWnd;
	return previous;
}

HWND WINAPI GetForegroundWindow(void)
{
	return GetActiveWindow();
}

BOOL WINAPI SetForegroundWindow(HWND hWnd)
{
	SetActiveWindow(hWnd);
	return TRUE;
}

HWND WINAPI GetDesktopWindow(void)
{
	WindowLock lock;
	State().desktop.visible = true;
	State().desktop.rect.right = State().screenWidth;
	State().desktop.rect.bottom = State().screenHeight;
	return reinterpret_cast<HWND>(&State().desktop);
}

HWND WINAPI FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName)
{
	WindowLock lock;
	WindowState &state = State();
	for (std::set<WindowObject *>::iterator it = state.windows.begin(); it != state.windows.end(); ++it)
	{
		if ((!lpClassName || (*it)->className == lpClassName) && (!lpWindowName || (*it)->title == lpWindowName))
			return reinterpret_cast<HWND>(*it);
	}
	return nullptr;
}

HWND WINAPI GetParent(HWND hWnd)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	return window ? window->parent : nullptr;
}

HWND WINAPI SetCapture(HWND hWnd)
{
	WindowLock lock;
	HWND previous = State().capture;
	State().capture = hWnd;
	return previous;
}

BOOL WINAPI ReleaseCapture(void)
{
	WindowLock lock;
	State().capture = nullptr;
	return TRUE;
}

HWND WINAPI GetCapture(void)
{
	WindowLock lock;
	return State().capture;
}

BOOL WINAPI SetWindowPos(HWND hWnd, HWND, int X, int Y, int cx, int cy, UINT uFlags)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window)
		return FALSE;
	const int width = window->rect.right - window->rect.left;
	const int height = window->rect.bottom - window->rect.top;
	const int newX = (uFlags & SWP_NOMOVE) ? window->rect.left : X;
	const int newY = (uFlags & SWP_NOMOVE) ? window->rect.top : Y;
	const int newWidth = (uFlags & SWP_NOSIZE) ? width : cx;
	const int newHeight = (uFlags & SWP_NOSIZE) ? height : cy;
	window->rect.left = newX;
	window->rect.top = newY;
	window->rect.right = newX + newWidth;
	window->rect.bottom = newY + newHeight;
	if (uFlags & SWP_SHOWWINDOW)
		window->visible = true;
	if (uFlags & SWP_HIDEWINDOW)
		window->visible = false;
	return TRUE;
}

BOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL)
{
	return SetWindowPos(hWnd, nullptr, X, Y, nWidth, nHeight, 0);
}

BOOL WINAPI AdjustWindowRect(LPRECT, DWORD, BOOL)
{
	// The canvas has no frame: the client area is the window.
	return TRUE;
}

BOOL WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD)
{
	return TRUE;
}

BOOL WINAPI GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT *lpwndpl)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window || !lpwndpl)
		return FALSE;
	memset(lpwndpl, 0, sizeof(*lpwndpl));
	lpwndpl->length = sizeof(*lpwndpl);
	lpwndpl->showCmd = window->visible ? SW_SHOWNORMAL : SW_HIDE;
	lpwndpl->rcNormalPosition = window->rect;
	return TRUE;
}

BOOL WINAPI SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT *lpwndpl)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window || !lpwndpl)
		return FALSE;
	window->rect = lpwndpl->rcNormalPosition;
	window->visible = lpwndpl->showCmd != SW_HIDE;
	return TRUE;
}

LONG WINAPI GetWindowLongA(HWND hWnd, int nIndex)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window)
		return 0;
	switch (nIndex)
	{
	case GWL_STYLE: return (LONG)window->style;
	case GWL_EXSTYLE: return (LONG)window->exStyle;
	case GWL_WNDPROC: return (LONG)(LONG_PTR)window->procedure;
	case GWL_HINSTANCE: return (LONG)(LONG_PTR)window->instance;
	case GWL_ID: return window->id;
	case GWL_USERDATA: return window->userData;
	default: return 0;
	}
}

LONG WINAPI SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window)
		return 0;
	LONG previous;
	switch (nIndex)
	{
	case GWL_STYLE: previous = (LONG)window->style; window->style = (DWORD)dwNewLong; break;
	case GWL_EXSTYLE: previous = (LONG)window->exStyle; window->exStyle = (DWORD)dwNewLong; break;
	case GWL_WNDPROC: previous = (LONG)(LONG_PTR)window->procedure; window->procedure = reinterpret_cast<WNDPROC>((LONG_PTR)dwNewLong); break;
	case GWL_ID: previous = window->id; window->id = dwNewLong; break;
	case GWL_USERDATA: previous = window->userData; window->userData = dwNewLong; break;
	default: previous = 0; break;
	}
	return previous;
}

int WINAPI GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window || !lpString || nMaxCount <= 0)
		return 0;
	strncpy(lpString, window->title.c_str(), (size_t)nMaxCount - 1);
	lpString[nMaxCount - 1] = 0;
	return (int)strlen(lpString);
}

BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL)
{
	return TRUE;
}

BOOL WINAPI ValidateRect(HWND, const RECT *)
{
	return TRUE;
}

HDC WINAPI BeginPaint(HWND hWnd, LPPAINTSTRUCT lpPaint)
{
	if (!lpPaint)
		return nullptr;
	memset(lpPaint, 0, sizeof(*lpPaint));
	GetClientRect(hWnd, &lpPaint->rcPaint);
	lpPaint->hdc = GetDC(hWnd);
	return lpPaint->hdc;
}

BOOL WINAPI EndPaint(HWND hWnd, const PAINTSTRUCT *lpPaint)
{
	if (lpPaint)
		ReleaseDC(hWnd, lpPaint->hdc);
	return TRUE;
}

BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable)
{
	WindowLock lock;
	WindowObject *window = Lookup(hWnd);
	if (!window)
		return FALSE;
	const bool wasDisabled = !window->enabled;
	window->enabled = bEnable != FALSE;
	return wasDisabled;
}

HWND WINAPI GetDlgItem(HWND, int)
{
	return nullptr;
}

INT_PTR WINAPI DialogBoxIndirectParamA(HINSTANCE, LPCDLGTEMPLATE, HWND, DLGPROC, LPARAM)
{
	// Nothing can show a modal dialog in the page.
	SetLastError(ERROR_ACCESS_DENIED);
	return -1;
}

INT_PTR WINAPI DialogBoxParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM)
{
	SetLastError(ERROR_ACCESS_DENIED);
	return -1;
}

BOOL WINAPI EndDialog(HWND, INT_PTR)
{
	return TRUE;
}

LRESULT WINAPI SendDlgItemMessageA(HWND, int, UINT, WPARAM, LPARAM)
{
	return 0;
}

BOOL WINAPI SetDlgItemTextA(HWND, int, LPCSTR)
{
	return FALSE;
}

BOOL WINAPI EnumThreadWindows(DWORD, WNDENUMPROC lpfn, LPARAM lParam)
{
	// Top level windows only; the callback may create or destroy windows.
	std::set<WindowObject *> windows;
	{
		WindowLock lock;
		windows = State().windows;
	}
	for (std::set<WindowObject *>::iterator it = windows.begin(); it != windows.end(); ++it)
	{
		if (!(*it)->parent && !lpfn(reinterpret_cast<HWND>(*it), lParam))
			return FALSE;
	}
	return TRUE;
}

BOOL WINAPI EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
	return EnumThreadWindows(0, lpEnumFunc, lParam);
}

void WINAPI InitCommonControls(void)
{
}

UINT_PTR WINAPI SetTimer(HWND, UINT_PTR nIDEvent, UINT, TIMERPROC)
{
	// WM_TIMER is not generated: the game uses its own timing.
	WindowLock lock;
	return nIDEvent ? nIDEvent : State().nextTimerId++;
}

BOOL WINAPI KillTimer(HWND, UINT_PTR)
{
	return TRUE;
}

HMONITOR WINAPI MonitorFromWindow(HWND, DWORD)
{
	return reinterpret_cast<HMONITOR>(1);
}

/* ---------------------------------------------------------------------------
** Messages
** ------------------------------------------------------------------------- */

UINT WINAPI RegisterWindowMessageA(LPCSTR lpString)
{
	WindowLock lock;
	WindowState &state = State();
	std::map<std::string, UINT>::iterator it = state.registeredMessages.find(lpString);
	if (it != state.registeredMessages.end())
		return it->second;
	const UINT id = state.nextMessageId++;
	state.registeredMessages[lpString] = id;
	return id;
}

/* ---------------------------------------------------------------------------
** Input state
** ------------------------------------------------------------------------- */

BOOL WINAPI GetKeyboardState(PBYTE lpKeyState)
{
	if (!lpKeyState)
		return FALSE;
	WindowLock lock;
	for (int i = 0; i < 256; ++i)
		lpKeyState[i] = State().keyDown[i] ? 0x80 : 0;
	return TRUE;
}

UINT WINAPI MapVirtualKeyA(UINT uCode, UINT uMapType)
{
	// MAPVK_VK_TO_CHAR: only the keys with an unambiguous character.
	if (uMapType == 2)
	{
		if ((uCode >= '0' && uCode <= '9') || (uCode >= 'A' && uCode <= 'Z'))
			return uCode;
		if (uCode == VK_SPACE)
			return ' ';
	}
	return 0;
}

int WINAPI ToAscii(UINT uVirtKey, UINT, const BYTE *lpKeyState, LPWORD lpChar, UINT)
{
	const bool shift = lpKeyState && (lpKeyState[VK_SHIFT] & 0x80);
	WORD result = 0;
	if (uVirtKey >= 'A' && uVirtKey <= 'Z')
	{
		const bool upper = shift != (lpKeyState && (lpKeyState[VK_CAPITAL] & 0x01));
		result = (WORD)(upper ? uVirtKey : uVirtKey + 32);
	}
	else if (uVirtKey >= '0' && uVirtKey <= '9')
	{
		static const char s_shifted[] = ")!@#$%^&*(";
		result = (WORD)(shift ? s_shifted[uVirtKey - '0'] : uVirtKey);
	}
	else if (uVirtKey == VK_SPACE)
	{
		result = ' ';
	}
	if (!result)
		return 0;
	*lpChar = result;
	return 1;
}

BOOL WINAPI GetCursorPos(LPPOINT lpPoint)
{
	if (!lpPoint)
		return FALSE;
	WindowLock lock;
	*lpPoint = State().cursorPosition;
	return TRUE;
}

BOOL WINAPI SetCursorPos(int X, int Y)
{
	webcompat_set_cursor_position(X, Y);
	return TRUE;
}

int WINAPI ShowCursor(BOOL bShow)
{
	WindowLock lock;
	State().cursorVisible += bShow ? 1 : -1;
	return State().cursorVisible;
}

HCURSOR WINAPI GetCursor(void)
{
	return nullptr; // the page draws the cursor
}

HCURSOR WINAPI LoadCursorA(HINSTANCE, LPCSTR)
{
	return reinterpret_cast<HCURSOR>(NewGdiObject(GDI_STOCK));
}

HCURSOR WINAPI LoadCursorFromFileA(LPCSTR)
{
	return reinterpret_cast<HCURSOR>(NewGdiObject(GDI_STOCK));
}

BOOL WINAPI DestroyCursor(HCURSOR)
{
	return TRUE;
}

HICON WINAPI LoadIconA(HINSTANCE, LPCSTR)
{
	return reinterpret_cast<HICON>(NewGdiObject(GDI_STOCK));
}

HANDLE WINAPI LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT)
{
	return NewGdiObject(GDI_STOCK);
}

int WINAPI GetSystemMetrics(int nIndex)
{
	WindowLock lock;
	switch (nIndex)
	{
	case SM_CXSCREEN: return State().screenWidth;
	case SM_CYSCREEN: return State().screenHeight;
	case SM_CXCURSOR:
	case SM_CYCURSOR: return 32;
	case SM_CXFRAME:
	case SM_CYFRAME: return 0;
	case SM_CYCAPTION: return 0;
	case SM_CXDOUBLECLK:
	case SM_CYDOUBLECLK: return 4;
	case SM_MOUSEWHEELPRESENT: return 1;
	default: return 0;
	}
}

/* ---------------------------------------------------------------------------
** GDI
** ------------------------------------------------------------------------- */

HDC WINAPI GetDC(HWND)
{
	return AsDC(NewGdiObject(GDI_DC));
}

int WINAPI ReleaseDC(HWND, HDC hDC)
{
	delete reinterpret_cast<GdiObject *>(hDC);
	return 1;
}

HDC WINAPI CreateCompatibleDC(HDC)
{
	return AsDC(NewGdiObject(GDI_DC));
}

BOOL WINAPI DeleteDC(HDC hdc)
{
	delete reinterpret_cast<GdiObject *>(hdc);
	return TRUE;
}

HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ h)
{
	return h;
}

BOOL WINAPI DeleteObject(HGDIOBJ ho)
{
	GdiObject *object = reinterpret_cast<GdiObject *>(ho);
	if (!object)
		return FALSE;
	if (object->kind == GDI_STOCK && object->width == -1)
		return TRUE; // shared stock objects stay
	delete object;
	return TRUE;
}

HGDIOBJ WINAPI GetStockObject(int i)
{
	static GdiObject s_stock[32];
	if (i < 0 || i >= 32)
		return nullptr;
	s_stock[i].kind = GDI_STOCK;
	s_stock[i].width = -1;
	return &s_stock[i];
}

int WINAPI GetObjectA(HANDLE h, int c, LPVOID pv)
{
	GdiObject *object = reinterpret_cast<GdiObject *>(h);
	if (object && object->kind == GDI_BITMAP && pv && c >= (int)sizeof(BITMAP))
	{
		BITMAP *bitmap = static_cast<BITMAP *>(pv);
		memset(bitmap, 0, sizeof(*bitmap));
		bitmap->bmWidth = object->width;
		bitmap->bmHeight = object->height;
		bitmap->bmPlanes = 1;
		bitmap->bmBitsPixel = 32;
		bitmap->bmWidthBytes = object->width * 4;
		bitmap->bmBits = object->bits;
		return (int)sizeof(BITMAP);
	}
	return 0;
}

HFONT WINAPI CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR)
{
	return reinterpret_cast<HFONT>(NewGdiObject(GDI_FONT));
}

HFONT WINAPI CreateFontIndirectA(const LOGFONTA *)
{
	return reinterpret_cast<HFONT>(NewGdiObject(GDI_FONT));
}

HBITMAP WINAPI CreateDIBSection(HDC, const BITMAPINFO *pbmi, UINT, void **ppvBits, HANDLE, DWORD)
{
	if (!pbmi)
		return nullptr;
	const int width = pbmi->bmiHeader.biWidth;
	const int height = pbmi->bmiHeader.biHeight < 0 ? -pbmi->bmiHeader.biHeight : pbmi->bmiHeader.biHeight;
	const int bitsPerPixel = pbmi->bmiHeader.biBitCount ? pbmi->bmiHeader.biBitCount : 32;
	// Rows are padded to 32 bits.
	const size_t stride = (((size_t)width * bitsPerPixel + 31) / 32) * 4;
	void *bits = calloc(1, stride * (size_t)height + 1);
	if (ppvBits)
		*ppvBits = bits;
	return reinterpret_cast<HBITMAP>(NewGdiObject(GDI_BITMAP, width, height, bits));
}

HBITMAP WINAPI CreateCompatibleBitmap(HDC, int cx, int cy)
{
	return reinterpret_cast<HBITMAP>(NewGdiObject(GDI_BITMAP, cx, cy, nullptr));
}

HBRUSH WINAPI CreateSolidBrush(COLORREF)
{
	return reinterpret_cast<HBRUSH>(NewGdiObject(GDI_BRUSH));
}

int WINAPI FillRect(HDC, const RECT *, HBRUSH)
{
	return 1;
}

COLORREF WINAPI SetTextColor(HDC, COLORREF)
{
	return 0;
}

COLORREF WINAPI SetBkColor(HDC, COLORREF)
{
	return 0;
}

int WINAPI SetBkMode(HDC, int)
{
	return OPAQUE;
}

BOOL WINAPI TextOutA(HDC, int, int, LPCSTR, int)
{
	return TRUE;
}

BOOL WINAPI TextOutW(HDC, int, int, LPCWSTR, int)
{
	return TRUE;
}

BOOL WINAPI ExtTextOutW(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *)
{
	return TRUE;
}

BOOL WINAPI ExtTextOutA(HDC, int, int, UINT, const RECT *, LPCSTR, UINT, const INT *)
{
	return TRUE;
}

int WINAPI DrawTextA(HDC, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
	if (lprc && (format & DT_CALCRECT))
	{
		const int length = cchText < 0 ? (int)strlen(lpchText) : cchText;
		lprc->right = lprc->left + length * CELL_WIDTH;
		lprc->bottom = lprc->top + CELL_HEIGHT;
	}
	return CELL_HEIGHT;
}

BOOL WINAPI GetTextExtentPoint32A(HDC, LPCSTR lpString, int c, LPSIZE psizl)
{
	if (!psizl)
		return FALSE;
	psizl->cx = (c < 0 ? (int)strlen(lpString) : c) * CELL_WIDTH;
	psizl->cy = CELL_HEIGHT;
	return TRUE;
}

BOOL WINAPI GetTextExtentPoint32W(HDC, LPCWSTR lpString, int c, LPSIZE psizl)
{
	if (!psizl)
		return FALSE;
	psizl->cx = (c < 0 ? (int)wcslen(lpString) : c) * CELL_WIDTH;
	psizl->cy = CELL_HEIGHT;
	return TRUE;
}

BOOL WINAPI GetTextMetricsA(HDC, LPTEXTMETRIC lptm)
{
	if (!lptm)
		return FALSE;
	memset(lptm, 0, sizeof(*lptm));
	lptm->tmHeight = CELL_HEIGHT;
	lptm->tmAscent = 13;
	lptm->tmDescent = 3;
	lptm->tmAveCharWidth = CELL_WIDTH;
	lptm->tmMaxCharWidth = CELL_WIDTH;
	lptm->tmWeight = FW_NORMAL;
	lptm->tmFirstChar = 32;
	lptm->tmLastChar = (CHAR)255;
	lptm->tmDefaultChar = '?';
	lptm->tmBreakChar = ' ';
	return TRUE;
}

BOOL WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD)
{
	return TRUE;
}

int WINAPI GetDeviceCaps(HDC, int index)
{
	WindowLock lock;
	switch (index)
	{
	case HORZRES: return State().screenWidth;
	case VERTRES: return State().screenHeight;
	case BITSPIXEL: return 32;
	case LOGPIXELSX:
	case LOGPIXELSY: return 96;
	case VREFRESH: return 60;
	default: return 0;
	}
}

COLORREF WINAPI GetPixel(HDC, int, int)
{
	return 0;
}

COLORREF WINAPI SetPixel(HDC, int, int, COLORREF color)
{
	return color;
}

int WINAPI AddFontResourceA(LPCSTR)
{
	return 1;
}

BOOL WINAPI RemoveFontResourceA(LPCSTR)
{
	return TRUE;
}

BOOL WINAPI GetDeviceGammaRamp(HDC, LPVOID lpRamp)
{
	// A linear ramp.
	if (!lpRamp)
		return FALSE;
	WORD *ramp = static_cast<WORD *>(lpRamp);
	for (int channel = 0; channel < 3; ++channel)
	{
		for (int i = 0; i < 256; ++i)
			ramp[channel * 256 + i] = (WORD)(i * 257);
	}
	return TRUE;
}

BOOL WINAPI SetDeviceGammaRamp(HDC, LPVOID)
{
	return TRUE;
}

/* ---------------------------------------------------------------------------
** Rectangles
** ------------------------------------------------------------------------- */

BOOL WINAPI SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom)
{
	if (!lprc)
		return FALSE;
	lprc->left = xLeft;
	lprc->top = yTop;
	lprc->right = xRight;
	lprc->bottom = yBottom;
	return TRUE;
}

BOOL WINAPI SetRectEmpty(LPRECT lprc)
{
	return SetRect(lprc, 0, 0, 0, 0);
}

BOOL WINAPI IntersectRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)
{
	RECT result;
	result.left = lprcSrc1->left > lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
	result.top = lprcSrc1->top > lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
	result.right = lprcSrc1->right < lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
	result.bottom = lprcSrc1->bottom < lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;
	if (result.left >= result.right || result.top >= result.bottom)
	{
		SetRectEmpty(lprcDst);
		return FALSE;
	}
	*lprcDst = result;
	return TRUE;
}

BOOL WINAPI UnionRect(LPRECT lprcDst, const RECT *lprcSrc1, const RECT *lprcSrc2)
{
	const bool empty1 = IsRectEmpty(lprcSrc1);
	const bool empty2 = IsRectEmpty(lprcSrc2);
	if (empty1 && empty2)
	{
		SetRectEmpty(lprcDst);
		return FALSE;
	}
	if (empty1)
	{
		*lprcDst = *lprcSrc2;
		return TRUE;
	}
	if (empty2)
	{
		*lprcDst = *lprcSrc1;
		return TRUE;
	}
	lprcDst->left = lprcSrc1->left < lprcSrc2->left ? lprcSrc1->left : lprcSrc2->left;
	lprcDst->top = lprcSrc1->top < lprcSrc2->top ? lprcSrc1->top : lprcSrc2->top;
	lprcDst->right = lprcSrc1->right > lprcSrc2->right ? lprcSrc1->right : lprcSrc2->right;
	lprcDst->bottom = lprcSrc1->bottom > lprcSrc2->bottom ? lprcSrc1->bottom : lprcSrc2->bottom;
	return TRUE;
}

BOOL WINAPI OffsetRect(LPRECT lprc, int dx, int dy)
{
	if (!lprc)
		return FALSE;
	lprc->left += dx;
	lprc->right += dx;
	lprc->top += dy;
	lprc->bottom += dy;
	return TRUE;
}

BOOL WINAPI PtInRect(const RECT *lprc, POINT pt)
{
	return pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom;
}

BOOL WINAPI IsRectEmpty(const RECT *lprc)
{
	return !lprc || lprc->left >= lprc->right || lprc->top >= lprc->bottom;
}

/* ---------------------------------------------------------------------------
** Shell and clipboard
** ------------------------------------------------------------------------- */

HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR, LPCSTR lpFile, LPCSTR, LPCSTR, INT)
{
	fprintf(stderr, "[ShellExecute] %s\n", lpFile ? lpFile : "");
	return reinterpret_cast<HINSTANCE>(33); // success
}

BOOL WINAPI OpenClipboard(HWND)
{
	return TRUE;
}

BOOL WINAPI CloseClipboard(void)
{
	return TRUE;
}

BOOL WINAPI EmptyClipboard(void)
{
	WindowLock lock;
	State().clipboardData = nullptr;
	return TRUE;
}

HANDLE WINAPI GetClipboardData(UINT)
{
	WindowLock lock;
	return State().clipboardData;
}

HANDLE WINAPI SetClipboardData(UINT, HANDLE hMem)
{
	WindowLock lock;
	State().clipboardData = hMem;
	return hMem;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT)
{
	WindowLock lock;
	return State().clipboardData != nullptr;
}

} // extern "C"
