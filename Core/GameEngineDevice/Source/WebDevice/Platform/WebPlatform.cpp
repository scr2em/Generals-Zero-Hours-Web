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

// FILE: WebPlatform.cpp //////////////////////////////////////////////////////
//
// Browser events -> Win32 messages, see WebPlatform.h.
//
// Threading: the DOM handlers are registered to run on the main browser thread
// (EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD), so they run whether or not
// the engine thread ever returns to an event loop, which it does not. They only
// touch the translator state (which is therefore single threaded) and append to
// two small queues guarded by a spin lock. The engine thread drains the queues.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Platform/WebPlatform.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#include <emscripten/threading_primitives.h>

#include <atomic>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

// Page access (main browser thread only) ------------------------------------------------------

// Writes left, top, width, height of the canvas in CSS pixels to out[0..3], and to out[4] whether the page point
// (x, y) is over the canvas or the stage around it (1: the black bars of a letterboxed canvas count), or over something
// else such as the toolbar (0).
EM_JS(int, web_platform_get_canvas_rect, (const char *selector, double *out, double x, double y), {
	var el = document.querySelector(UTF8ToString(selector));
	if (!el) return 0;
	var r = el.getBoundingClientRect();
	var o = out / 8;
	HEAPF64[o] = r.left;
	HEAPF64[o + 1] = r.top;
	HEAPF64[o + 2] = r.width;
	HEAPF64[o + 3] = r.height;
	var hit = el.parentElement ? el.parentElement : document.body;
	var top = document.elementFromPoint(x, y);
	HEAPF64[o + 4] = (!top || top === el || top === hit || top === document.body || top === document.documentElement) ? 1 : 0;
	return 1;
});

// Suspends the calling thread (JSPI: EM_ASYNC_JS functions are suspending imports) until the browser has
// run its next animation frame, so that it presents what the thread drew, or until timeoutMs have passed
// when that is above 0. Only call it from a suspendable stack, see WebPlatform_SetYieldEnabled().
EM_ASYNC_JS(void, web_platform_wait_display_frame, (int timeoutMs), {
	await new Promise((resolve) => {
		let timer = 0, raf = 0, done = false;
		const finish = () => {
			if (done) return;
			done = true;
			if (timer) clearTimeout(timer);
			if (raf && typeof cancelAnimationFrame === 'function') cancelAnimationFrame(raf);
			resolve();
		};
		if (typeof requestAnimationFrame === 'function')
			raf = requestAnimationFrame(finish);
		else
			timer = setTimeout(finish, 16);	// no animation frames in this context: about one at 60 Hz
		if (timeoutMs > 0 && !timer)
			timer = setTimeout(finish, timeoutMs);
	});
});

// Win32 GetTickCount from Dependencies/WebCompat, if it is part of the program.
// (Declared under another name because windows.h may declare GetTickCount with its own types.)
extern "C" uint32_t web_platform_GetTickCount(void) __asm__("GetTickCount") __attribute__((weak));

namespace
{

//-------------------------------------------------------------------------------------------------
// Small helpers
//-------------------------------------------------------------------------------------------------

enum
{
	MK_LBUTTON_ = 0x0001,
	MK_RBUTTON_ = 0x0002,
	MK_SHIFT_ = 0x0004,
	MK_CONTROL_ = 0x0008,
	MK_MBUTTON_ = 0x0010,

	DEFAULT_CLIENT_WIDTH = 800,
	DEFAULT_CLIENT_HEIGHT = 600,

	// Win32 defaults: GetDoubleClickTime() and SM_CXDOUBLECLK/SM_CYDOUBLECLK.
	DOUBLE_CLICK_MSEC = 500,
	DOUBLE_CLICK_SLOP = 4,

	MESSAGE_QUEUE_SIZE = 4096,	// power of two
	KEY_QUEUE_SIZE = 512				// power of two
};

class SpinLock
{
public:
	void lock() { while (m_flag.test_and_set(std::memory_order_acquire)) {} }
	void unlock() { m_flag.clear(std::memory_order_release); }
private:
	std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

class ScopedLock
{
public:
	explicit ScopedLock(SpinLock &lock) : m_lock(lock) { m_lock.lock(); }
	~ScopedLock() { m_lock.unlock(); }
private:
	SpinLock &m_lock;
};

inline intptr_t makeLParam(int lo, int hi)
{
	return (intptr_t)(int32_t)(((uint32_t)(hi & 0xFFFF) << 16) | (uint32_t)(lo & 0xFFFF));
}

//-------------------------------------------------------------------------------------------------
// State
//-------------------------------------------------------------------------------------------------

struct PlatformState
{
	// queues, guarded by queueLock
	SpinLock queueLock;
	WebPlatformMsg messages[MESSAGE_QUEUE_SIZE];
	uint32_t messageHead = 0;	// next to pop
	uint32_t messageTail = 0;	// next to push
	WebKeyEvent keys[KEY_QUEUE_SIZE];
	uint32_t keyHead = 0;
	uint32_t keyTail = 0;
	bool quitPosted = false;
	int quitCode = 0;

	// futex word the engine thread sleeps on in GetMessage
	std::atomic<uint32_t> wakeCounter{0};

	// shared with both sides
	std::atomic<int> clientWidth{DEFAULT_CLIENT_WIDTH};
	std::atomic<int> clientHeight{DEFAULT_CLIENT_HEIGHT};
	std::atomic<int> active{1};
	std::atomic<int> pageHidden{0};	// document.visibilityState is "hidden": no animation frames run
	std::atomic<uint8_t> vkState[256];	// bit 7 down, bit 0 toggled

	std::atomic<WebWindowProc> windowProc{nullptr};

	// translator state, only touched by the main browser thread
	bool dikDown[256];
	bool metaHeld[256];	// keys pressed while a Meta (Command) key was down, whose key up the browser never sends (macOS)
	int buttonsFromCanvas = 0;	// MK_*BUTTON bits of presses that started on the canvas
	bool cursorWasInside = false;
	double wheelRemainder = 0.0;
	bool capsLock = false;
	bool pageVisible = true;
	bool pageFocused = true;
	struct ClickInfo
	{
		double time;
		int x, y;
	} lastClick[3];	// left, middle, right

	char canvasSelector[128] = "#canvas";
	bool initialized = false;

	PlatformState()
	{
		for (int i = 0; i < 256; ++i)
		{
			vkState[i].store(0);
			dikDown[i] = false;
			metaHeld[i] = false;
		}
		for (int i = 0; i < 3; ++i)
		{
			lastClick[i].time = -1.0e9;
			lastClick[i].x = lastClick[i].y = 0;
		}
	}
};

PlatformState &state()
{
	static PlatformState s;
	return s;
}

// A fake window handle. It only needs to be non-null and unique.
const uintptr_t s_windowHandle = 0x00C0FFEE;

//-------------------------------------------------------------------------------------------------
// Queues
//-------------------------------------------------------------------------------------------------

void wakeEngineThread()
{
	PlatformState &s = state();
	s.wakeCounter.fetch_add(1, std::memory_order_release);
	emscripten_futex_wake(&s.wakeCounter, 1);
}

// Caller holds the lock.
void pushMessageLocked(PlatformState &s, uint32_t message, uintptr_t wParam, intptr_t lParam, int ptX, int ptY)
{
	// Like Windows, collapse a run of mouse moves into the latest one. The game
	// polls once per frame while the browser can report a move every millisecond.
	if (message == WEBWM_MOUSEMOVE && s.messageHead != s.messageTail)
	{
		WebPlatformMsg &last = s.messages[(s.messageTail - 1) & (MESSAGE_QUEUE_SIZE - 1)];
		if (last.message == WEBWM_MOUSEMOVE)
		{
			last.wParam = wParam;
			last.lParam = lParam;
			last.time = WebPlatform_GetTimeMs();
			last.ptX = ptX;
			last.ptY = ptY;
			return;
		}
	}

	if (s.messageTail - s.messageHead >= MESSAGE_QUEUE_SIZE)
		return;	// full, drop

	WebPlatformMsg &msg = s.messages[s.messageTail & (MESSAGE_QUEUE_SIZE - 1)];
	msg.hwnd = s_windowHandle;
	msg.message = message;
	msg.wParam = wParam;
	msg.lParam = lParam;
	msg.time = WebPlatform_GetTimeMs();
	msg.ptX = ptX;
	msg.ptY = ptY;
	++s.messageTail;
}

void pushMessage(uint32_t message, uintptr_t wParam, intptr_t lParam, int ptX = 0, int ptY = 0)
{
	PlatformState &s = state();
	{
		ScopedLock lock(s.queueLock);
		pushMessageLocked(s, message, wParam, lParam, ptX, ptY);
	}
	wakeEngineThread();
}

void pushKeyEvent(int dik, bool down)
{
	PlatformState &s = state();
	{
		ScopedLock lock(s.queueLock);
		if (s.keyTail - s.keyHead >= KEY_QUEUE_SIZE)
			return;
		WebKeyEvent &key = s.keys[s.keyTail & (KEY_QUEUE_SIZE - 1)];
		key.dik = (uint8_t)dik;
		key.down = down ? 1 : 0;
		key.time = WebPlatform_GetTimeMs();
		++s.keyTail;
	}
	wakeEngineThread();
}

//-------------------------------------------------------------------------------------------------
// Keyboard translation
//-------------------------------------------------------------------------------------------------

// Number of bytes of the UTF-8 sequence starting with lead, 0 if invalid.
int utf8Length(unsigned char lead)
{
	if (lead < 0x80) return 1;
	if ((lead & 0xE0) == 0xC0) return 2;
	if ((lead & 0xF0) == 0xE0) return 3;
	if ((lead & 0xF8) == 0xF0) return 4;
	return 0;
}

// If key (KeyboardEvent.key) is one character, returns its code point.
bool singleCodePoint(const char *key, uint32_t &codePoint)
{
	const unsigned char *p = (const unsigned char *)key;
	const int len = utf8Length(p[0]);
	if (len == 0 || (int)strlen(key) != len)
		return false;

	if (len == 1)
		codePoint = p[0];
	else
	{
		codePoint = p[0] & (0xFF >> (len + 1));
		for (int i = 1; i < len; ++i)
			codePoint = (codePoint << 6) | (p[i] & 0x3F);
	}
	return true;
}

void setVkState(int vk, bool down)
{
	if (vk <= 0 || vk > 255)
		return;
	PlatformState &s = state();
	uint8_t v = s.vkState[vk].load(std::memory_order_relaxed);
	v = (uint8_t)(down ? (v | 0x80) : (v & 0x7F));
	s.vkState[vk].store(v, std::memory_order_relaxed);
}

void toggleVkState(int vk)
{
	PlatformState &s = state();
	s.vkState[vk].fetch_xor(0x01, std::memory_order_relaxed);
}

// Left and right modifiers also drive the generic VK_SHIFT/VK_CONTROL/VK_MENU.
void updateGenericModifiers()
{
	PlatformState &s = state();
	auto isDown = [&](int vk) { return (s.vkState[vk].load(std::memory_order_relaxed) & 0x80) != 0; };
	setVkState(0x10, isDown(0xA0) || isDown(0xA1));	// VK_SHIFT
	setVkState(0x11, isDown(0xA2) || isDown(0xA3));	// VK_CONTROL
	setVkState(0x12, isDown(0xA4) || isDown(0xA5));	// VK_MENU
}

void postChar(uint32_t codePoint)
{
	if (codePoint >= 0x10000)
	{
		// UTF-16 surrogate pair
		const uint32_t v = codePoint - 0x10000;
		pushMessage(WEBWM_CHAR, 0xD800 + (v >> 10), 1);
		pushMessage(WEBWM_CHAR, 0xDC00 + (v & 0x3FF), 1);
	}
	else
	{
		pushMessage(WEBWM_CHAR, codePoint, 1);
	}
}

// Keys the page keeps for itself, so that the player can always get out: reload and the address bar (the page asks
// before it reloads a running game) and the developer tools. Everything else is the game's: F5, F12 (the starter
// content's screenshot key), Tab, Ctrl+digits and the like are not the browser's while the game has the page. F11 is the
// page's fullscreen button: the page itself handles it (shell.html), with Keyboard Lock where the browser has it.
// Reserved browser shortcuts (Ctrl+W/T/N, Cmd+Q) cannot be taken by any page; fullscreen with Keyboard Lock gets most.
bool browserOwnsKey(const EmscriptenKeyboardEvent *e)
{
	if (strcmp(e->code, "F11") == 0)
		return true;	// handled by the page
	if ((e->ctrlKey || e->metaKey) && (strcmp(e->code, "KeyR") == 0 || strcmp(e->code, "KeyL") == 0))
		return true;
	if (((e->ctrlKey && e->shiftKey) || (e->metaKey && e->altKey)) && (strcmp(e->code, "KeyI") == 0 || strcmp(e->code, "KeyJ") == 0 || strcmp(e->code, "KeyC") == 0))
		return true;
	return false;
}

bool isModifierCode(const char *code)
{
	return strncmp(code, "Shift", 5) == 0 || strncmp(code, "Control", 7) == 0 || strncmp(code, "Alt", 3) == 0 || strncmp(code, "Meta", 4) == 0;
}

void releaseAllKeys()
{
	PlatformState &s = state();
	for (int dik = 1; dik < 256; ++dik)
	{
		if (s.dikDown[dik])
		{
			s.dikDown[dik] = false;
			pushKeyEvent(dik, false);
		}
	}
	for (int vk = 1; vk < 256; ++vk)
	{
		// keep the toggle bit
		setVkState(vk, false);
	}
	for (int dik = 0; dik < 256; ++dik)
		s.metaHeld[dik] = false;
	s.buttonsFromCanvas = 0;
}

bool onKey(int eventType, const EmscriptenKeyboardEvent *e, void *)
{
	PlatformState &s = state();
	const bool down = (eventType == EMSCRIPTEN_EVENT_KEYDOWN);

	const int dik = WebPlatform_CodeToDIK(e->code);
	int vk = WebPlatform_CodeToVK(e->code);
	if (vk == 0)
		vk = (int)e->keyCode;

	// Caps lock state: toggled by the key and re-derived from letters, since
	// the event does not carry getModifierState().
	if (down && !e->repeat && strcmp(e->code, "CapsLock") == 0)
		s.capsLock = !s.capsLock;
	{
		uint32_t cp;
		if (down && singleCodePoint(e->key, cp) && cp < 0x80 && ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')))
			s.capsLock = (cp >= 'A' && cp <= 'Z') != e->shiftKey;
	}
	if (down && !e->repeat)
	{
		if (strcmp(e->code, "CapsLock") == 0) toggleVkState(0x14);
		else if (strcmp(e->code, "NumLock") == 0) toggleVkState(0x90);
		else if (strcmp(e->code, "ScrollLock") == 0) toggleVkState(0x91);
	}
	{
		// keep the VK_CAPITAL toggle bit in sync with the derived state
		uint8_t v = s.vkState[0x14].load(std::memory_order_relaxed);
		v = (uint8_t)(s.capsLock ? (v | 0x01) : (v & 0xFE));
		s.vkState[0x14].store(v, std::memory_order_relaxed);
	}

	bool wasDown = false;
	if (dik != 0)
	{
		wasDown = s.dikDown[dik];
		if (down != wasDown)
		{
			s.dikDown[dik] = down;
			pushKeyEvent(dik, down);
		}
		// macOS sends no key up for a key pressed while Command is held. Release those keys when Command is.
		if (down && e->metaKey && !isModifierCode(e->code))
			s.metaHeld[dik] = true;
		else if (!down)
			s.metaHeld[dik] = false;
	}
	setVkState(vk, down);
	updateGenericModifiers();
	if (!down && strncmp(e->code, "Meta", 4) == 0)
	{
		for (int k = 1; k < 256; ++k)
		{
			if (!s.metaHeld[k])
				continue;
			s.metaHeld[k] = false;
			if (s.dikDown[k])
			{
				s.dikDown[k] = false;
				pushKeyEvent(k, false);
			}
		}
	}

	// Win32 message
	const bool sysKey = e->altKey && !e->ctrlKey;
	uint32_t message;
	if (down)
		message = sysKey ? WEBWM_SYSKEYDOWN : WEBWM_KEYDOWN;
	else
		message = sysKey ? WEBWM_SYSKEYUP : WEBWM_KEYUP;

	uint32_t lParam = 1;	// repeat count
	lParam |= (uint32_t)(dik & 0x7F) << 16;
	if (dik & 0x80) lParam |= 1u << 24;
	if (e->altKey) lParam |= 1u << 29;
	if (!down || wasDown || e->repeat) lParam |= 1u << 30;
	if (!down) lParam |= 1u << 31;
	pushMessage(message, (uintptr_t)vk, (intptr_t)(int32_t)lParam);

	if (down && !e->ctrlKey && !e->metaKey && !e->altKey)
	{
		uint32_t cp;
		if (singleCodePoint(e->key, cp))
			postChar(cp);
		else if (strcmp(e->key, "Enter") == 0)
			postChar(13);
		else if (strcmp(e->key, "Backspace") == 0)
			postChar(8);
		else if (strcmp(e->key, "Tab") == 0)
			postChar(9);
		else if (strcmp(e->key, "Escape") == 0)
			postChar(27);
	}

	// true = preventDefault
	return !browserOwnsKey(e);
}

//-------------------------------------------------------------------------------------------------
// Mouse translation
//-------------------------------------------------------------------------------------------------

// Browser pixel position -> client position, scaled by the CSS to canvas pixel ratio. inside: the pointer is over the
// game. A pointer over the black bars of a letterboxed canvas still belongs to the game and counts as at the nearest edge
// of the game (screen edge scrolling works with a canvas that does not touch the edges of the screen, and the cursor
// does not "leave" while it moves towards an edge); elsewhere on the page (the toolbar) it is outside.
void mapToClient(int clientX, int clientY, int &x, int &y, bool &inside)
{
	PlatformState &s = state();
	double rect[5];
	const int w = s.clientWidth.load(std::memory_order_relaxed);
	const int h = s.clientHeight.load(std::memory_order_relaxed);

	if (!web_platform_get_canvas_rect(s.canvasSelector, rect, (double)clientX, (double)clientY) || rect[2] <= 0.0 || rect[3] <= 0.0)
	{
		x = clientX;
		y = clientY;
		inside = (x >= 0 && y >= 0 && x < w && y < h);
		return;
	}

	const double px = (clientX - rect[0]) * (double)w / rect[2];
	const double py = (clientY - rect[1]) * (double)h / rect[3];
	x = (int)floor(px);
	y = (int)floor(py);
	inside = (px >= 0.0 && py >= 0.0 && px < (double)w && py < (double)h);
	if (!inside && rect[4] != 0.0)
	{
		x = x < 0 ? 0 : (x >= w ? w - 1 : x);
		y = y < 0 ? 0 : (y >= h ? h - 1 : y);
		inside = true;
	}
}

uintptr_t mouseKeyState(const EmscriptenMouseEvent *e)
{
	uintptr_t flags = 0;
	if (e->buttons & 1) flags |= MK_LBUTTON_;
	if (e->buttons & 2) flags |= MK_RBUTTON_;
	if (e->buttons & 4) flags |= MK_MBUTTON_;
	if (e->shiftKey) flags |= MK_SHIFT_;
	if (e->ctrlKey) flags |= MK_CONTROL_;
	return flags;
}

// DOM button numbers: 0 left, 1 middle, 2 right.
int buttonIndex(unsigned short domButton)
{
	return domButton <= 2 ? (int)domButton : -1;
}

bool onMouseDown(int, const EmscriptenMouseEvent *e, void *)
{
	PlatformState &s = state();
	const int b = buttonIndex(e->button);
	if (b < 0)
		return false;

	int x, y;
	bool inside;
	mapToClient(e->clientX, e->clientY, x, y, inside);

	static const uint32_t downMsg[3] = { WEBWM_LBUTTONDOWN, WEBWM_MBUTTONDOWN, WEBWM_RBUTTONDOWN };
	static const int mkBit[3] = { MK_LBUTTON_, MK_MBUTTON_, MK_RBUTTON_ };

	// Windows turns the second press of a quick pair into a double click message.
	const double now = emscripten_get_now();
	PlatformState::ClickInfo &last = s.lastClick[b];
	uint32_t message = downMsg[b];
	if (now - last.time <= (double)DOUBLE_CLICK_MSEC && abs(x - last.x) <= DOUBLE_CLICK_SLOP / 2 && abs(y - last.y) <= DOUBLE_CLICK_SLOP / 2)
	{
		message += 2;	// *BUTTONDBLCLK
		last.time = -1.0e9;	// a third press starts over
	}
	else
	{
		last.time = now;
		last.x = x;
		last.y = y;
	}

	s.buttonsFromCanvas |= mkBit[b];
	pushMessage(message, mouseKeyState(e) | (uintptr_t)mkBit[b], makeLParam(x, y), x, y);
	return true;	// no text selection, no focus change
}

bool onMouseUp(int, const EmscriptenMouseEvent *e, void *)
{
	PlatformState &s = state();
	const int b = buttonIndex(e->button);
	if (b < 0)
		return false;

	static const uint32_t upMsg[3] = { WEBWM_LBUTTONUP, WEBWM_MBUTTONUP, WEBWM_RBUTTONUP };
	static const int mkBit[3] = { MK_LBUTTON_, MK_MBUTTON_, MK_RBUTTON_ };

	// Only releases of presses that began on the canvas belong to the game.
	if ((s.buttonsFromCanvas & mkBit[b]) == 0)
		return false;
	s.buttonsFromCanvas &= ~mkBit[b];

	int x, y;
	bool inside;
	mapToClient(e->clientX, e->clientY, x, y, inside);
	pushMessage(upMsg[b], mouseKeyState(e), makeLParam(x, y), x, y);
	return true;
}

bool onMouseMove(int, const EmscriptenMouseEvent *e, void *)
{
	PlatformState &s = state();
	int x, y;
	bool inside;
	mapToClient(e->clientX, e->clientY, x, y, inside);

	// Report moves over the canvas, and moves outside it while a drag that began
	// there is going on. Report the first move outside too, once, so the game
	// learns the cursor left (it ignores positions outside the client area).
	if (inside || s.buttonsFromCanvas != 0 || s.cursorWasInside)
	{
		s.cursorWasInside = inside;
		pushMessage(WEBWM_MOUSEMOVE, mouseKeyState(e), makeLParam(x, y), x, y);
	}
	return false;
}

bool onWheel(int, const EmscriptenWheelEvent *e, void *)
{
	PlatformState &s = state();

	// Normalise to Win32 units: 120 per notch, positive = away from the user.
	double notches;
	switch (e->deltaMode)
	{
		case 1:		notches = -e->deltaY / 3.0; break;	// lines
		case 2:		notches = -e->deltaY; break;				// pages
		default:	notches = -e->deltaY / 100.0; break;	// pixels
	}
	s.wheelRemainder += notches * 120.0;
	const int delta = (int)s.wheelRemainder;
	if (delta == 0)
		return true;
	s.wheelRemainder -= delta;

	int x, y;
	bool inside;
	mapToClient(e->mouse.clientX, e->mouse.clientY, x, y, inside);

	// wParam: key state in the low word, wheel delta in the high word.
	// lParam: the position in screen coordinates, which are client coordinates here.
	const uint32_t wParam = ((uint32_t)(delta & 0xFFFF) << 16) | (uint32_t)(mouseKeyState(&e->mouse) & 0xFFFF);
	pushMessage(WEBWM_MOUSEWHEEL, (uintptr_t)wParam, makeLParam(x, y), x, y);
	return true;	// do not scroll the page
}

bool onContextMenu(int, const EmscriptenMouseEvent *, void *)
{
	return true;
}

//-------------------------------------------------------------------------------------------------
// Focus
//-------------------------------------------------------------------------------------------------

void setActive(bool active)
{
	PlatformState &s = state();
	if ((s.active.load(std::memory_order_relaxed) != 0) == active)
		return;

	s.active.store(active ? 1 : 0, std::memory_order_relaxed);
	if (!active)
	{
		releaseAllKeys();
		s.cursorWasInside = false;
		pushMessage(WEBWM_KILLFOCUS, 0, 0);
		pushMessage(WEBWM_ACTIVATE, 0, 0);			// WA_INACTIVE
		pushMessage(WEBWM_ACTIVATEAPP, 0, 0);
	}
	else
	{
		pushMessage(WEBWM_ACTIVATEAPP, 1, 0);
		pushMessage(WEBWM_ACTIVATE, 1, 0);			// WA_ACTIVE
		pushMessage(WEBWM_SETFOCUS, 0, 0);
	}
}

void updateActive()
{
	PlatformState &s = state();
	setActive(s.pageFocused && s.pageVisible);
}

bool onFocus(int eventType, const EmscriptenFocusEvent *, void *)
{
	state().pageFocused = (eventType == EMSCRIPTEN_EVENT_FOCUS);
	updateActive();
	return false;
}

bool onVisibility(int, const EmscriptenVisibilityChangeEvent *e, void *)
{
	state().pageVisible = !e->hidden;
	state().pageHidden.store(e->hidden ? 1 : 0, std::memory_order_relaxed);
	updateActive();
	return false;
}

bool onResize(int, const EmscriptenUiEvent *, void *)
{
	// The canvas keeps the client size the game chose; only its CSS box changes,
	// and mouse positions are mapped using the live box.
	return false;
}

} // namespace

//-------------------------------------------------------------------------------------------------
// Public interface
//-------------------------------------------------------------------------------------------------

extern "C" int WebPlatform_Init(const char *canvasSelector)
{
	PlatformState &s = state();
	if (s.initialized)
		return 1;

	if (canvasSelector != nullptr && canvasSelector[0] != '\0')
		snprintf(s.canvasSelector, sizeof(s.canvasSelector), "%s", canvasSelector);

	const pthread_t mainThread = EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD;

	int failures = 0;
	auto check = [&](EMSCRIPTEN_RESULT r, const char *what)
	{
		if (r != EMSCRIPTEN_RESULT_SUCCESS)
		{
			fprintf(stderr, "WebPlatform: could not register %s handler (%d)\n", what, (int)r);
			++failures;
		}
	};

	s.pageFocused = MAIN_THREAD_EM_ASM_INT({ return (document.hasFocus() && document.visibilityState === 'visible') ? 1 : 0; }) != 0;
	s.pageVisible = true;
	s.pageHidden.store(MAIN_THREAD_EM_ASM_INT({ return document.visibilityState === 'hidden' ? 1 : 0; }), std::memory_order_relaxed);
	s.active.store(s.pageFocused ? 1 : 0);

	check(emscripten_set_keydown_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onKey, mainThread), "keydown");
	check(emscripten_set_keyup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onKey, mainThread), "keyup");
	check(emscripten_set_mousedown_callback_on_thread(s.canvasSelector, nullptr, false, onMouseDown, mainThread), "mousedown");
	check(emscripten_set_mouseup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onMouseUp, mainThread), "mouseup");
	check(emscripten_set_mousemove_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onMouseMove, mainThread), "mousemove");
	check(emscripten_set_wheel_callback_on_thread(s.canvasSelector, nullptr, false, onWheel, mainThread), "wheel");
	check(emscripten_set_contextmenu_callback_on_thread(s.canvasSelector, nullptr, false, onContextMenu, mainThread), "contextmenu");
	check(emscripten_set_focus_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, onFocus, mainThread), "focus");
	check(emscripten_set_blur_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, onFocus, mainThread), "blur");
	check(emscripten_set_visibilitychange_callback_on_thread(nullptr, false, onVisibility, mainThread), "visibilitychange");
	check(emscripten_set_resize_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, onResize, mainThread), "resize");

	// The pointer leaving the page: the DOM reports it as mouseleave of the document element, which emscripten's
	// callbacks do not cover on the window.
	MAIN_THREAD_ASYNC_EM_ASM({
		if (typeof Module === 'undefined' || window.__zhPointerHooked) return;
		window.__zhPointerHooked = true;
		document.documentElement.addEventListener('mouseleave', function () {
			if (Module._WebPlatform_PointerLeft) Module._WebPlatform_PointerLeft();
		});
	});

	s.initialized = true;
	return failures == 0 ? 1 : 0;
}

extern "C" void WebPlatform_Shutdown(void)
{
	PlatformState &s = state();
	if (!s.initialized)
		return;

	const pthread_t mainThread = EM_CALLBACK_THREAD_CONTEXT_MAIN_RUNTIME_THREAD;
	emscripten_set_keydown_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, nullptr, mainThread);
	emscripten_set_keyup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, nullptr, mainThread);
	emscripten_set_mousedown_callback_on_thread(s.canvasSelector, nullptr, false, nullptr, mainThread);
	emscripten_set_mouseup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, nullptr, mainThread);
	emscripten_set_mousemove_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, nullptr, mainThread);
	emscripten_set_wheel_callback_on_thread(s.canvasSelector, nullptr, false, nullptr, mainThread);
	emscripten_set_contextmenu_callback_on_thread(s.canvasSelector, nullptr, false, nullptr, mainThread);
	emscripten_set_focus_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr, mainThread);
	emscripten_set_blur_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr, mainThread);
	emscripten_set_visibilitychange_callback_on_thread(nullptr, false, nullptr, mainThread);
	emscripten_set_resize_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr, mainThread);
	s.initialized = false;
}

extern "C" const char *WebPlatform_GetCanvasSelector(void)
{
	return state().canvasSelector;
}

extern "C" void WebPlatform_NotifyExit(int exitCode)
{
	MAIN_THREAD_ASYNC_EM_ASM({
		if (typeof Module !== 'undefined' && Module.onExit) Module.onExit($0);
	}, exitCode);
}

extern "C" uintptr_t WebPlatform_GetWindow(void)
{
	return s_windowHandle;
}

extern "C" void WebPlatform_SetWindowProc(WebWindowProc proc)
{
	state().windowProc.store(proc);
}

extern "C" void WebPlatform_SetTitle(const char *title)
{
	MAIN_THREAD_ASYNC_EM_ASM({ document.title = UTF8ToString($0); }, title ? title : "");
}

extern "C" void WebPlatform_SetClientSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	PlatformState &s = state();
	const bool changed = (s.clientWidth.load() != width || s.clientHeight.load() != height);
	s.clientWidth.store(width);
	s.clientHeight.store(height);

	MAIN_THREAD_ASYNC_EM_ASM({
		var el = document.querySelector(UTF8ToString($0));
		if (el) {
			el.dataset.clientWidth = $1;
			el.dataset.clientHeight = $2;
		}
		window.dispatchEvent(new CustomEvent('zh-clientsize', { detail: { width: $1, height: $2 } }));
	}, s.canvasSelector, width, height);

	if (changed)
		pushMessage(WEBWM_SIZE, 0 /* SIZE_RESTORED */, makeLParam(width, height));
}

extern "C" void WebPlatform_GetClientSize(int *width, int *height)
{
	PlatformState &s = state();
	if (width) *width = s.clientWidth.load();
	if (height) *height = s.clientHeight.load();
}

// What the page shows over the canvas: a cursor id (1 and up), 0 for none (the game draws its own), -1 for the system
// arrow. Only changes are sent: the game sets its cursor every frame.
static void sendCursor(int code)
{
	static std::atomic<int> s_last{-2};
	if (s_last.exchange(code) == code)
		return;
	MAIN_THREAD_ASYNC_EM_ASM({
		var el = document.querySelector(UTF8ToString($1));
		if (window.zhCursor) window.zhCursor.set($0);
		else if (el) el.style.cursor = $0 > 0 ? 'default' : ($0 == 0 ? 'none' : 'default');
	}, code, state().canvasSelector);
}

extern "C" void WebPlatform_SetCursorVisible(int visible)
{
	sendCursor(visible ? -1 : 0);
}

extern "C" int WebPlatform_IsActive(void)
{
	return state().active.load(std::memory_order_relaxed);
}

extern "C" EMSCRIPTEN_KEEPALIVE void WebPlatform_PointerLeft(void)
{
	PlatformState &s = state();
	if (!s.cursorWasInside)
		return;
	s.cursorWasInside = false;
	// A position outside of the client area is how the game learns that the cursor left (see WebWndProc).
	pushMessage(WEBWM_MOUSEMOVE, 0, makeLParam(-1, -1), -1, -1);
}

extern "C" EMSCRIPTEN_KEEPALIVE void WebPlatform_RequestScreenshot(void)
{
	pushMessage(WEBWM_APP_SCREENSHOT, 0, 0);
}

extern "C" int WebPlatform_CursorLoad(const void *data, int size)
{
	static std::atomic<int> s_nextId{1};
	if (data == nullptr || size <= 0)
		return 0;
	const int id = s_nextId.fetch_add(1);
	// The page copies the bytes before this returns (the call waits for it), so the caller's buffer is free afterwards.
	const int ok = MAIN_THREAD_EM_ASM_INT({
		if (typeof window === 'undefined' || !window.zhCursor) return 0;
		window.zhCursor.load($0, HEAPU8.slice($1, $1 + $2));
		return 1;
	}, id, data, size);
	return ok ? id : 0;
}

extern "C" void WebPlatform_CursorSet(int id)
{
	sendCursor(id > 0 ? id : 0);
}

//-------------------------------------------------------------------------------------------------
// Yielding to the browser
//-------------------------------------------------------------------------------------------------

namespace
{

// A blocking loop that renders (load screen, movie) is shown at most this often, so a loop that
// reports progress thousands of times does not spend its time waiting for animation frames. A bit
// under one display frame at 60 Hz (16.7 ms), so a movie that draws every frame of its own is
// shown frame by frame.
const double YIELD_MIN_INTERVAL_MS = 12.0;

// How long a wait for an animation frame may take while the page is visible before the thread goes on
// anyway (a canvas that is not displayed gets none), so it can never hang for good.
const int YIELD_VISIBLE_TIMEOUT_MS = 500;

// Only touched by the yielding thread.
struct YieldState
{
	bool enabled = false;
	bool waiting = false;			// suspended right now: nothing may suspend again
	bool framePaced = false;	// the frame in progress has already waited for a display frame
	bool log = false;
	pthread_t thread = 0;
	double lastWaitEnd = -1.0e9;
	double yieldedMs = 0.0;
	unsigned count = 0;
};

YieldState &yieldState()
{
	static YieldState y;
	return y;
}

bool mayYield()
{
	YieldState &y = yieldState();
	return y.enabled && !y.waiting && pthread_equal(pthread_self(), y.thread);
}

void waitDisplayFrame()
{
	YieldState &y = yieldState();
	const bool hidden = state().pageHidden.load(std::memory_order_relaxed) != 0;
	y.waiting = true;
	const double start = emscripten_get_now();
	web_platform_wait_display_frame(hidden ? 0 : YIELD_VISIBLE_TIMEOUT_MS);
	const double end = emscripten_get_now();
	y.waiting = false;
	y.yieldedMs += end - start;
	++y.count;
	y.lastWaitEnd = end;
}

} // namespace

extern "C" void WebPlatform_SetYieldEnabled(int enabled)
{
	YieldState &y = yieldState();
	y.enabled = enabled != 0;
	y.thread = pthread_self();
	y.framePaced = false;
}

extern "C" void WebPlatform_SetYieldLog(int enable)
{
	yieldState().log = enable != 0;
}

extern "C" int WebPlatform_CanYield(void)
{
	return yieldState().enabled ? 1 : 0;
}

extern "C" void WebPlatform_BeginFrame(void)
{
	yieldState().framePaced = false;
}

extern "C" void WebPlatform_EndFrame(void)
{
	if (!mayYield() || yieldState().framePaced)
		return;
	WebPlatform_WaitFrame();
}

extern "C" void WebPlatform_WaitFrame(void)
{
	if (!mayYield())
		return;
	yieldState().framePaced = true;
	waitDisplayFrame();
}

extern "C" void WebPlatform_YieldFrame(void)
{
	if (!mayYield())
		return;
	// A hidden page gets no animation frames; waiting would stall the loop for nothing.
	if (state().pageHidden.load(std::memory_order_relaxed) != 0)
		return;
	if (emscripten_get_now() - yieldState().lastWaitEnd < YIELD_MIN_INTERVAL_MS)
		return;
	waitDisplayFrame();
	if (yieldState().log)
		printf("WebPlatform: yielded to the browser inside a frame, wait #%u, %.0f ms\n", yieldState().count, emscripten_get_now());
}

extern "C" void WebPlatform_FramePresented(void)
{
	if (!mayYield())
		return;
	if (!yieldState().framePaced)
		WebPlatform_WaitFrame();	// the frame's own present: pace the frame loop like a vsynced Present()
	else
		WebPlatform_YieldFrame();	// a further present within the frame: a blocking loop is rendering
}

extern "C" double WebPlatform_GetYieldedMs(void)
{
	return yieldState().yieldedMs;
}

extern "C" unsigned WebPlatform_GetYieldCount(void)
{
	return yieldState().count;
}

extern "C" int WebPlatform_PeekMessage(WebPlatformMsg *msg, int remove)
{
	PlatformState &s = state();
	ScopedLock lock(s.queueLock);

	if (s.messageHead != s.messageTail)
	{
		if (msg)
			*msg = s.messages[s.messageHead & (MESSAGE_QUEUE_SIZE - 1)];
		if (remove)
			++s.messageHead;
		return 1;
	}

	// WM_QUIT is only delivered once everything posted before it has been.
	if (s.quitPosted)
	{
		if (msg)
		{
			memset(msg, 0, sizeof(*msg));
			msg->hwnd = 0;
			msg->message = WEBWM_QUIT;
			msg->wParam = (uintptr_t)s.quitCode;
			msg->time = WebPlatform_GetTimeMs();
		}
		if (remove)
			s.quitPosted = false;
		return 1;
	}

	return 0;
}

extern "C" int WebPlatform_GetMessage(WebPlatformMsg *msg)
{
	PlatformState &s = state();
	for (;;)
	{
		const uint32_t seen = s.wakeCounter.load(std::memory_order_acquire);
		if (WebPlatform_PeekMessage(msg, 1))
			return msg->message != WEBWM_QUIT ? 1 : 0;

		// Sleep until the browser thread posts something; the timeout keeps the
		// thread responsive to proxied calls even if a wake-up is missed.
		emscripten_futex_wait(&s.wakeCounter, seen, 50.0);
	}
}

// Asks the game to quit like the window's close button does (WEBWM_CLOSE). For the page: any thread
// may call it, so the launcher can use Module._WebPlatform_RequestClose() from the main thread.
extern "C" EMSCRIPTEN_KEEPALIVE void WebPlatform_RequestClose(void)
{
	pushMessage(WEBWM_CLOSE, 0, 0);
}

extern "C" int WebPlatform_PostMessage(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	pushMessage(message, wParam, lParam);
	return 1;
}

extern "C" void WebPlatform_PostQuitMessage(int exitCode)
{
	PlatformState &s = state();
	{
		ScopedLock lock(s.queueLock);
		s.quitPosted = true;
		s.quitCode = exitCode;
	}
	wakeEngineThread();
}

static std::atomic<int> s_inputLog{0};

extern "C" void WebPlatform_SetInputLog(int enable)
{
	s_inputLog.store(enable);
}

extern "C" intptr_t WebPlatform_DispatchMessage(const WebPlatformMsg *msg)
{
	WebWindowProc proc = state().windowProc.load();
	if (proc == nullptr || msg == nullptr)
		return 0;
	if (s_inputLog.load(std::memory_order_relaxed))
		printf("input: message 0x%x wParam 0x%x lParam 0x%x at %d,%d\n", (unsigned)msg->message, (unsigned)msg->wParam, (unsigned)msg->lParam, msg->ptX, msg->ptY);
	return proc(msg->hwnd ? msg->hwnd : s_windowHandle, msg->message, msg->wParam, msg->lParam);
}

extern "C" intptr_t WebPlatform_SendMessage(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
	WebWindowProc proc = state().windowProc.load();
	if (proc == nullptr)
		return 0;
	return proc(s_windowHandle, message, wParam, lParam);
}

extern "C" int WebPlatform_PopKeyEvent(WebKeyEvent *event)
{
	PlatformState &s = state();
	ScopedLock lock(s.queueLock);
	if (s.keyHead == s.keyTail)
		return 0;
	if (event)
		*event = s.keys[s.keyHead & (KEY_QUEUE_SIZE - 1)];
	if (s_inputLog.load(std::memory_order_relaxed))
		printf("input: key dik 0x%x %s\n", (unsigned)s.keys[s.keyHead & (KEY_QUEUE_SIZE - 1)].dik, s.keys[s.keyHead & (KEY_QUEUE_SIZE - 1)].down ? "down" : "up");
	++s.keyHead;
	return 1;
}

extern "C" void WebPlatform_ResetKeys(void)
{
	PlatformState &s = state();
	ScopedLock lock(s.queueLock);
	s.keyHead = s.keyTail;
}

extern "C" int WebPlatform_GetKeyState(int vk)
{
	if (vk <= 0 || vk > 255)
		return 0;
	const uint8_t v = state().vkState[vk].load(std::memory_order_relaxed);
	// bit 15 = down, bit 0 = toggled, as a SHORT
	return ((v & 0x80) ? 0x8000 : 0) | (v & 0x01);
}

extern "C" uint32_t WebPlatform_GetTimeMs(void)
{
	// The game compares input times with timeGetTime(), so use the same clock when
	// Dependencies/WebCompat is linked in.
	if (&web_platform_GetTickCount != nullptr)
		return web_platform_GetTickCount();
	return (uint32_t)(uint64_t)emscripten_get_now();
}
