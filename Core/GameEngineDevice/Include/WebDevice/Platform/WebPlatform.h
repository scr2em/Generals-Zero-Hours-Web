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

// FILE: WebPlatform.h ////////////////////////////////////////////////////////
//
// The browser platform layer of the WebAssembly build.
//
// The game engine runs on a worker thread (-sPROXY_TO_PTHREAD) and the
// browser delivers input on the main browser thread. This layer
//  - registers the DOM event handlers (emscripten/html5.h) on the main browser
//    thread and translates the events into the Win32 messages the game's
//    window procedure expects (WM_MOUSEMOVE, WM_KEYDOWN, ...),
//  - queues them in a small lock protected ring that the engine thread drains
//    through the message pump (PeekMessage/GetMessage/DispatchMessage),
//  - keeps a second queue of keyboard transitions in DirectInput scan codes
//    for WebKeyboard,
//  - mounts the game data (a copy in the Origin Private File System, or the player's own
//    folder read in place) and the user data (OPFS).
//
// This header is plain C with fixed-size types so Dependencies/WebCompat can
// declare and call these functions without including any game header. The
// functions are thread safe unless noted otherwise.
//
// The "window" is the canvas: client coordinates are canvas pixels, the window
// origin is (0,0) on the "screen", so ScreenToClient/ClientToScreen are the
// identity.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Same memory layout as the Win32 MSG structure on a 32 bit target.
typedef struct WebPlatformMsg
{
	uintptr_t hwnd;
	uint32_t  message;
	uintptr_t wParam;
	intptr_t  lParam;
	uint32_t  time;
	int32_t   ptX;
	int32_t   ptY;
} WebPlatformMsg;

// A keyboard transition in DirectInput scan code terms, auto repeat removed
// like DirectInput's buffered data.
typedef struct WebKeyEvent
{
	uint8_t  dik;		// DIK_* scan code
	uint8_t  down;	// 1 = pressed, 0 = released
	uint32_t time;	// WebPlatform_GetTimeMs()
} WebKeyEvent;

// Window procedure, same shape as WNDPROC.
typedef intptr_t (*WebWindowProc)(uintptr_t hwnd, uint32_t message, uintptr_t wParam, intptr_t lParam);

// ---- life cycle (call from the engine thread) ------------------------------

// Registers the DOM event handlers. canvasSelector is a CSS selector for the
// canvas element, NULL means "#canvas". Safe to call more than once.
// Returns 1 on success.
int WebPlatform_Init(const char *canvasSelector);
void WebPlatform_Shutdown(void);

// The CSS selector of the canvas. The WebGL layer must use the same canvas;
// it is the one transferred to the engine thread by -sOFFSCREENCANVASES_TO_PTHREAD.
const char *WebPlatform_GetCanvasSelector(void);

// Tells the page (Module.onExit) that the game has ended.
void WebPlatform_NotifyExit(int exitCode);

// ---- the window ------------------------------------------------------------

uintptr_t WebPlatform_GetWindow(void);					// the one and only HWND, never 0
void WebPlatform_SetWindowProc(WebWindowProc proc);
void WebPlatform_SetTitle(const char *title);

// Client area in canvas pixels, i.e. the size of the WebGL back buffer. The
// default is 800x600. SetClientSize also posts a WM_SIZE, and tells the page so
// it can lay out the canvas with the right aspect ratio.
void WebPlatform_SetClientSize(int width, int height);
void WebPlatform_GetClientSize(int *width, int *height);

// Chooses whether the page shows the system cursor over the canvas (visible
// != 0) or hides it because the game draws its own.
void WebPlatform_SetCursorVisible(int visible);

// 1 while the page has focus and is visible.
int WebPlatform_IsActive(void);

// The pointer left the page (Module._WebPlatform_PointerLeft, from the page's mouseleave): the game stops screen edge
// scrolling like it does when the cursor leaves its window. Callable from any thread.
void WebPlatform_PointerLeft(void);

// Asks the game to take a screenshot, like its screenshot key does (the page's Screenshot button, for keyboards without
// the key: Module._WebPlatform_RequestScreenshot). The game's window procedure gets WEBWM_APP_SCREENSHOT.
void WebPlatform_RequestScreenshot(void);

// ---- mouse cursors ---------------------------------------------------------
//
// The game's cursors are Windows animated cursor files (.ANI). The page (web/zhcursor.js) decodes them to CSS cursors and
// animates them on the canvas; without it the system arrow stays.

// Hands a cursor file's bytes to the page. Returns a cursor id (1 and up), or 0 when it cannot be shown.
int WebPlatform_CursorLoad(const void *data, int size);

// Shows a cursor returned by WebPlatform_CursorLoad over the canvas; 0 hides the system cursor (the game draws its own).
void WebPlatform_CursorSet(int id);

// ---- yielding to the browser (JSPI) ----------------------------------------
//
// The browser shows what the engine thread drew only when that thread returns to its event loop.
// The game's blocking loops that draw from inside themselves (load screens, fades, movie loops)
// would never show a frame. The build uses JSPI (-sJSPI): the whole engine thread runs on a
// suspendable stack, so a call here suspends it, the browser runs its event loop (and presents the
// canvas), and the thread resumes after the next display frame (requestAnimationFrame). The game
// code is not restructured: the Direct3D 8 layer calls WebPlatform_FramePresented() at the end of
// every Present(), which covers every place that renders. Only the thread that called
// WebPlatform_SetYieldEnabled() ever suspends, and only while it is not in a non-suspendable call
// (an event loop callback); everywhere else these functions return at once.

// Marks the calling thread as the one that may suspend (call from the engine thread, which runs
// under JSPI) or, with 0, stops yielding. Off by default: programs whose frames run from
// emscripten_set_main_loop (the platform tests) never suspend.
void WebPlatform_SetYieldEnabled(int enabled);

// 1 when WebPlatform_SetYieldEnabled(1) was called and the browser can suspend (WebAssembly.Suspending).
int WebPlatform_CanYield(void);

// The frame loop brackets every frame of the game with these two (see WebPlatform_FramePresented).
// EndFrame waits for the next display frame if the frame did not already wait in its Present().
void WebPlatform_BeginFrame(void);
void WebPlatform_EndFrame(void);

// Suspends until the browser has shown its next display frame (requestAnimationFrame fired). This is
// how the frame loop paces itself, like a vsynced Present(): no-op unless yielding is enabled.
void WebPlatform_WaitFrame(void);

// Like WebPlatform_WaitFrame(), but at most once per display frame (every 12 ms at the fastest) and never
// while the page is hidden (its animation frames do not run then), so a loop may call it for every
// progress step without slowing down. For
// blocking loops that do not render but should let the browser show the last frame and run its events.
void WebPlatform_YieldFrame(void);

// The Direct3D 8 layer calls it at the end of every Present(). The first present after
// WebPlatform_BeginFrame() waits for the next display frame, which is the frame loop's pacing; any
// further present in the same frame comes from a blocking loop inside the frame (load screen, movie)
// and yields like WebPlatform_YieldFrame().
void WebPlatform_FramePresented(void);

// With enable != 0, logs (printf) every wait that a blocking loop inside a frame causes (not the frame
// loop's own pacing), for tests and for finding loops that do not show their frames. Off by default.
void WebPlatform_SetYieldLog(int enable);

// Time the engine thread spent suspended in the calls above since the program started, in
// milliseconds, and how many times it suspended. For frame timing and for the tests.
double WebPlatform_GetYieldedMs(void);
unsigned WebPlatform_GetYieldCount(void);

// ---- the message pump ------------------------------------------------------

// Returns 1 and fills msg when a message is waiting; removes it when remove != 0.
int WebPlatform_PeekMessage(WebPlatformMsg *msg, int remove);

// Blocks until a message is available. Returns 0 for WM_QUIT, else 1.
int WebPlatform_GetMessage(WebPlatformMsg *msg);

// Appends a message to the queue (from any thread).
int WebPlatform_PostMessage(uint32_t message, uintptr_t wParam, intptr_t lParam);
void WebPlatform_PostQuitMessage(int exitCode);

// Calls the window procedure; returns its result.
intptr_t WebPlatform_DispatchMessage(const WebPlatformMsg *msg);
intptr_t WebPlatform_SendMessage(uint32_t message, uintptr_t wParam, intptr_t lParam);

// ---- keyboard --------------------------------------------------------------

// Pops the oldest keyboard transition. Returns 0 when there is none.
int WebPlatform_PopKeyEvent(WebKeyEvent *event);

/** Asks the game to quit, as the close button of a window would. Callable from any thread; the page
	calls it as Module._WebPlatform_RequestClose(). The game ends through Module.onExit(0). */
void WebPlatform_RequestClose(void);

/** Logs (printf) every input message and key event the engine takes from the queues, for tests
	and for finding out whether input reaches the game. Off by default. */
void WebPlatform_SetInputLog(int enable);

// Forget queued transitions and release all keys (focus loss).
void WebPlatform_ResetKeys(void);

// GetKeyState/GetAsyncKeyState replacement: bit 15 = down, bit 0 = toggled
// (caps/num/scroll lock). vk is a Win32 virtual key code.
int WebPlatform_GetKeyState(int vk);

// ---- time ------------------------------------------------------------------

// Milliseconds on a clock shared by all threads. Message and key event times use
// it, and timeGetTime()/GetTickCount() should return the same value.
uint32_t WebPlatform_GetTimeMs(void);

// ---- storage ---------------------------------------------------------------

// Makes the game data and the user data visible in the WasmFS tree (call once from the
// engine thread, before anything touches the file system):
//   /game      Zero Hour install
//   /generals  Generals install
//   /userdata  saves, replays, options.ini  (always the Origin Private File System)
// Names are case insensitive. Returns 0 on success. Where /game and /generals come from depends
// on what the page did before the game started:
//   * it copied the install into OPFS (directories "game" and "generals"): OPFS mode, or
//   * it handed the engine thread the File objects of the folder the player picked
//     (Module.zhDirect, see GeneralsMD/Code/Main/webdirect): direct mode, "read in place". The
//     files are served read-only, straight from the player's disk; nothing is copied.
int WebPlatform_MountStorage(void);

enum
{
	WEBPLATFORM_STORAGE_NONE = 0,		// WebPlatform_MountStorage() has not succeeded
	WEBPLATFORM_STORAGE_OPFS = 1,		// the game data was copied into OPFS
	WEBPLATFORM_STORAGE_DIRECT = 2		// the game data is read in place from the player's folder
};

// Which of the two WebPlatform_MountStorage() set up.
int WebPlatform_GetStorageMode(void);

// With required != 0 WebPlatform_MountStorage() fails instead of falling back to OPFS when the page
// did not hand over any files (the page asked for direct mode with -webdirect). Call before mounting.
void WebPlatform_RequireDirectStorage(int required);

// Counters of the calling thread's reads in direct mode as JSON text in buffer (reads, cache hits,
// bytes fetched from the files ...). Returns the length, or -1 when it does not fit. For tests and logs.
int WebPlatform_GetDirectStats(char *buffer, int capacity);

// ---- DirectInput scan code / virtual key lookup (exposed for tests) --------

// KeyboardEvent.code -> DIK_* (0 if unknown) and VK_* (0 if unknown).
int WebPlatform_CodeToDIK(const char *code);
int WebPlatform_CodeToVK(const char *code);

// Win32 message numbers used by the platform layer.
enum
{
	WEBWM_SIZE = 0x0005,
	WEBWM_ACTIVATE = 0x0006,
	WEBWM_SETFOCUS = 0x0007,
	WEBWM_KILLFOCUS = 0x0008,
	WEBWM_ACTIVATEAPP = 0x001C,
	WEBWM_QUIT = 0x0012,
	WEBWM_CLOSE = 0x0010,
	WEBWM_KEYDOWN = 0x0100,
	WEBWM_KEYUP = 0x0101,
	WEBWM_CHAR = 0x0102,
	WEBWM_SYSKEYDOWN = 0x0104,
	WEBWM_SYSKEYUP = 0x0105,
	WEBWM_MOUSEMOVE = 0x0200,
	WEBWM_LBUTTONDOWN = 0x0201,
	WEBWM_LBUTTONUP = 0x0202,
	WEBWM_LBUTTONDBLCLK = 0x0203,
	WEBWM_RBUTTONDOWN = 0x0204,
	WEBWM_RBUTTONUP = 0x0205,
	WEBWM_RBUTTONDBLCLK = 0x0206,
	WEBWM_MBUTTONDOWN = 0x0207,
	WEBWM_MBUTTONUP = 0x0208,
	WEBWM_MBUTTONDBLCLK = 0x0209,
	WEBWM_MOUSEWHEEL = 0x020A,

	// Messages of this layer itself (WM_APP and up)
	WEBWM_APP_SCREENSHOT = 0x8001
};

#ifdef __cplusplus
} // extern "C"
#endif
