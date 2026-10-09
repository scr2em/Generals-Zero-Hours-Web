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
** WebAssembly port: the Win32 console API and named pipes. The page has no
** console window: AllocConsole fails, so the programs that probe for a console
** fall back to the standard streams.
*/
#include "webcompat_internal.h"

#include <string.h>

extern "C" {

BOOL WINAPI AllocConsole(void)
{
	SetLastError(ERROR_ACCESS_DENIED);
	return FALSE;
}

BOOL WINAPI FreeConsole(void)
{
	return TRUE;
}

BOOL WINAPI GetConsoleMode(HANDLE, LPDWORD lpMode)
{
	if (lpMode)
		*lpMode = 0;
	return FALSE;
}

BOOL WINAPI SetConsoleMode(HANDLE, DWORD)
{
	return FALSE;
}

BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE, PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo)
{
	if (!lpConsoleScreenBufferInfo)
		return FALSE;
	memset(lpConsoleScreenBufferInfo, 0, sizeof(*lpConsoleScreenBufferInfo));
	lpConsoleScreenBufferInfo->dwSize.X = 80;
	lpConsoleScreenBufferInfo->dwSize.Y = 25;
	lpConsoleScreenBufferInfo->srWindow.Right = 79;
	lpConsoleScreenBufferInfo->srWindow.Bottom = 24;
	lpConsoleScreenBufferInfo->dwMaximumWindowSize = lpConsoleScreenBufferInfo->dwSize;
	return TRUE;
}

BOOL WINAPI SetConsoleScreenBufferSize(HANDLE, COORD)
{
	return FALSE;
}

BOOL WINAPI SetConsoleWindowInfo(HANDLE, BOOL, const SMALL_RECT *)
{
	return FALSE;
}

BOOL WINAPI SetConsoleCursorInfo(HANDLE, const CONSOLE_CURSOR_INFO *)
{
	return FALSE;
}

BOOL WINAPI SetConsoleCursorPosition(HANDLE, COORD)
{
	return FALSE;
}

BOOL WINAPI SetConsoleTextAttribute(HANDLE, WORD)
{
	return FALSE;
}

BOOL WINAPI SetConsoleTitleA(LPCSTR)
{
	return TRUE;
}

BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE, LPDWORD lpNumberOfEvents)
{
	if (lpNumberOfEvents)
		*lpNumberOfEvents = 0;
	return TRUE;
}

BOOL WINAPI ReadConsoleInputA(HANDLE, PINPUT_RECORD, DWORD, LPDWORD lpNumberOfEventsRead)
{
	if (lpNumberOfEventsRead)
		*lpNumberOfEventsRead = 0;
	return FALSE;
}

BOOL WINAPI WriteConsoleOutputA(HANDLE, const CHAR_INFO *, COORD, COORD, PSMALL_RECT)
{
	return FALSE;
}

BOOL WINAPI WriteConsoleA(HANDLE hConsoleOutput, const void *lpBuffer, DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten, LPVOID)
{
	return WriteFile(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, nullptr);
}

BOOL WINAPI SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD)
{
	SetLastError(ERROR_INVALID_HANDLE);
	return FALSE;
}

} // extern "C"
