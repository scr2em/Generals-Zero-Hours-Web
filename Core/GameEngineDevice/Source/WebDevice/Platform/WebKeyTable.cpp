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

// FILE: WebKeyTable.cpp //////////////////////////////////////////////////////
//
// Maps the physical keys the browser reports (KeyboardEvent.code) to the
// DirectInput scan codes (DIK_*) the game works with, and to Win32 virtual
// keys. Like DirectInput, the mapping is by key position and does not depend on
// the keyboard layout. The numeric values are those of dinput.h and winuser.h;
// this file deliberately includes neither so that it builds on its own.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Platform/WebPlatform.h"

#include <string.h>

namespace
{

struct KeyMapEntry
{
	const char *code;	// KeyboardEvent.code
	uint8_t dik;			// DIK_*
	uint8_t vk;				// VK_*
};

// Sorted by code (strcmp order) so lookup can use a binary search.
const KeyMapEntry s_keyMap[] =
{
	{ "AltLeft",					0x38, 0xA4 },	// DIK_LMENU, VK_LMENU
	{ "AltRight",					0xB8, 0xA5 },	// DIK_RMENU, VK_RMENU
	{ "ArrowDown",				0xD0, 0x28 },
	{ "ArrowLeft",				0xCB, 0x25 },
	{ "ArrowRight",				0xCD, 0x27 },
	{ "ArrowUp",					0xC8, 0x26 },
	{ "Backquote",				0x29, 0xC0 },
	{ "Backslash",				0x2B, 0xDC },
	{ "Backspace",				0x0E, 0x08 },
	{ "BracketLeft",			0x1A, 0xDB },
	{ "BracketRight",			0x1B, 0xDD },
	{ "CapsLock",					0x3A, 0x14 },
	{ "Comma",						0x33, 0xBC },
	{ "ContextMenu",			0xDD, 0x5D },
	{ "ControlLeft",			0x1D, 0xA2 },
	{ "ControlRight",			0x9D, 0xA3 },
	{ "Convert",					0x79, 0x1C },
	{ "Delete",						0xD3, 0x2E },
	{ "Digit0",						0x0B, 0x30 },
	{ "Digit1",						0x02, 0x31 },
	{ "Digit2",						0x03, 0x32 },
	{ "Digit3",						0x04, 0x33 },
	{ "Digit4",						0x05, 0x34 },
	{ "Digit5",						0x06, 0x35 },
	{ "Digit6",						0x07, 0x36 },
	{ "Digit7",						0x08, 0x37 },
	{ "Digit8",						0x09, 0x38 },
	{ "Digit9",						0x0A, 0x39 },
	{ "End",							0xCF, 0x23 },
	{ "Enter",						0x1C, 0x0D },
	{ "Equal",						0x0D, 0xBB },
	{ "Escape",						0x01, 0x1B },
	{ "F1",								0x3B, 0x70 },
	{ "F10",							0x44, 0x79 },
	{ "F11",							0x57, 0x7A },
	{ "F12",							0x58, 0x7B },
	{ "F13",							0x64, 0x7C },
	{ "F14",							0x65, 0x7D },
	{ "F15",							0x66, 0x7E },
	{ "F2",								0x3C, 0x71 },
	{ "F3",								0x3D, 0x72 },
	{ "F4",								0x3E, 0x73 },
	{ "F5",								0x3F, 0x74 },
	{ "F6",								0x40, 0x75 },
	{ "F7",								0x41, 0x76 },
	{ "F8",								0x42, 0x77 },
	{ "F9",								0x43, 0x78 },
	{ "Home",							0xC7, 0x24 },
	{ "Insert",						0xD2, 0x2D },
	{ "IntlBackslash",		0x56, 0xE2 },	// DIK_OEM_102
	{ "IntlRo",						0x73, 0xC1 },	// DIK_ABNT_C1
	{ "IntlYen",					0x7D, 0xDC },	// DIK_YEN
	{ "KanaMode",					0x70, 0x15 },	// DIK_KANA
	{ "KeyA",							0x1E, 0x41 },
	{ "KeyB",							0x30, 0x42 },
	{ "KeyC",							0x2E, 0x43 },
	{ "KeyD",							0x20, 0x44 },
	{ "KeyE",							0x12, 0x45 },
	{ "KeyF",							0x21, 0x46 },
	{ "KeyG",							0x22, 0x47 },
	{ "KeyH",							0x23, 0x48 },
	{ "KeyI",							0x17, 0x49 },
	{ "KeyJ",							0x24, 0x4A },
	{ "KeyK",							0x25, 0x4B },
	{ "KeyL",							0x26, 0x4C },
	{ "KeyM",							0x32, 0x4D },
	{ "KeyN",							0x31, 0x4E },
	{ "KeyO",							0x18, 0x4F },
	{ "KeyP",							0x19, 0x50 },
	{ "KeyQ",							0x10, 0x51 },
	{ "KeyR",							0x13, 0x52 },
	{ "KeyS",							0x1F, 0x53 },
	{ "KeyT",							0x14, 0x54 },
	{ "KeyU",							0x16, 0x55 },
	{ "KeyV",							0x2F, 0x56 },
	{ "KeyW",							0x11, 0x57 },
	{ "KeyX",							0x2D, 0x58 },
	{ "KeyY",							0x15, 0x59 },
	{ "KeyZ",							0x2C, 0x5A },
	{ "MetaLeft",					0xDB, 0x5B },
	{ "MetaRight",				0xDC, 0x5C },
	{ "Minus",						0x0C, 0xBD },
	{ "NonConvert",				0x7B, 0x1D },
	{ "NumLock",					0x45, 0x90 },
	{ "Numpad0",					0x52, 0x60 },
	{ "Numpad1",					0x4F, 0x61 },
	{ "Numpad2",					0x50, 0x62 },
	{ "Numpad3",					0x51, 0x63 },
	{ "Numpad4",					0x4B, 0x64 },
	{ "Numpad5",					0x4C, 0x65 },
	{ "Numpad6",					0x4D, 0x66 },
	{ "Numpad7",					0x47, 0x67 },
	{ "Numpad8",					0x48, 0x68 },
	{ "Numpad9",					0x49, 0x69 },
	{ "NumpadAdd",				0x4E, 0x6B },
	{ "NumpadComma",			0xB3, 0x6C },
	{ "NumpadDecimal",		0x53, 0x6E },
	{ "NumpadDivide",			0xB5, 0x6F },
	{ "NumpadEnter",			0x9C, 0x0D },
	{ "NumpadEqual",			0x8D, 0x0C },
	{ "NumpadMultiply",		0x37, 0x6A },
	{ "NumpadSubtract",		0x4A, 0x6D },
	{ "PageDown",					0xD1, 0x22 },
	{ "PageUp",						0xC9, 0x21 },
	{ "Pause",						0xC5, 0x13 },
	{ "Period",						0x34, 0xBE },
	{ "PrintScreen",			0xB7, 0x2C },
	{ "Quote",						0x28, 0xDE },
	{ "ScrollLock",				0x46, 0x91 },
	{ "Semicolon",				0x27, 0xBA },
	{ "ShiftLeft",				0x2A, 0xA0 },
	{ "ShiftRight",				0x36, 0xA1 },
	{ "Slash",						0x35, 0xBF },
	{ "Space",						0x39, 0x20 },
	{ "Tab",							0x0F, 0x09 },
};

const int s_keyMapCount = (int)(sizeof(s_keyMap) / sizeof(s_keyMap[0]));

const KeyMapEntry *findKey(const char *code)
{
	if (code == nullptr || code[0] == '\0')
		return nullptr;

	int lo = 0;
	int hi = s_keyMapCount - 1;
	while (lo <= hi)
	{
		const int mid = (lo + hi) / 2;
		const int cmp = strcmp(code, s_keyMap[mid].code);
		if (cmp == 0)
			return &s_keyMap[mid];
		if (cmp < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return nullptr;
}

} // namespace

extern "C" int WebPlatform_CodeToDIK(const char *code)
{
	const KeyMapEntry *entry = findKey(code);
	return entry ? entry->dik : 0;
}

extern "C" int WebPlatform_CodeToVK(const char *code)
{
	const KeyMapEntry *entry = findKey(code);
	return entry ? entry->vk : 0;
}
