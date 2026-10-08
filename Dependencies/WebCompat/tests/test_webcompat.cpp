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
** WebAssembly port: a runtime test of the WebCompat layer for node. Built and
** run by run_tests.sh, not part of the game build.
*/
#include <windows.h>
#include <mbstring.h>
#include <oaidl.h>
#include <vfw.h>
#include <io.h>
#include <process.h>
#include <tchar.h>
#include <winsock.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <string>

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(condition) \
	do { \
		++s_checks; \
		if (!(condition)) { \
			++s_failures; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static void TestWideStrings()
{
	CHECK(sizeof(wchar_t) == 2);
	CHECK(wcslen(L"abc") == 3);
	CHECK(wcslen(L"") == 0);

	wchar_t buffer[32];
	wcscpy(buffer, L"Hello");
	wcscat(buffer, L", World");
	CHECK(wcscmp(buffer, L"Hello, World") == 0);
	CHECK(wcslen(buffer) == 12);
	CHECK(wcsncmp(buffer, L"Help", 3) == 0);
	CHECK(wcsncmp(buffer, L"Help", 4) < 0);
	CHECK(wcschr(buffer, L',') == buffer + 5);
	CHECK(wcsrchr(buffer, L'o') == buffer + 8);
	CHECK(wcsstr(buffer, L"World") == buffer + 7);
	CHECK(wcsstr(buffer, L"xyz") == nullptr);
	CHECK(wcsspn(L"aabbc", L"ab") == 4);
	CHECK(wcscspn(L"aabbc", L"c") == 4);
	CHECK(wcspbrk(L"hello world", L" #") != nullptr);
	CHECK(_wcsicmp(L"AbC", L"aBc") == 0);
	CHECK(_wcsicmp(L"abc", L"abd") < 0);
	CHECK(_wcsnicmp(L"ABCdef", L"abcxyz", 3) == 0);

	wchar_t copy[8];
	wcsncpy(copy, L"abcdefghijkl", 8);
	CHECK(copy[7] == L'h');
	wcsncpy(copy, L"ab", 8);
	CHECK(copy[2] == 0 && copy[7] == 0);

	wcscpy(buffer, L"mixed Case");
	_wcsupr(buffer);
	CHECK(wcscmp(buffer, L"MIXED CASE") == 0);
	_wcslwr(buffer);
	CHECK(wcscmp(buffer, L"mixed case") == 0);

	wmemset(buffer, L'x', 4);
	CHECK(buffer[3] == L'x');
	CHECK(wmemcmp(L"abc", L"abd", 3) < 0);
	CHECK(wmemchr(L"abcdef", L'd', 6) != nullptr);
	wmemcpy(buffer, L"12345", 6);
	wmemmove(buffer + 1, buffer, 6);
	CHECK(wcscmp(buffer, L"112345") == 0);

	wchar_t tokens[] = L"a,b;;c";
	wchar_t *save = nullptr;
	wchar_t *token = wcstok(tokens, L",;", &save);
	CHECK(token && wcscmp(token, L"a") == 0);
	token = wcstok(nullptr, L",;", &save);
	CHECK(token && wcscmp(token, L"b") == 0);
	token = wcstok(nullptr, L",;", &save);
	CHECK(token && wcscmp(token, L"c") == 0);
	CHECK(wcstok(nullptr, L",;", &save) == nullptr);

	CHECK(towupper(L'a') == L'A');
	CHECK(iswspace(L' ') != 0);
	CHECK(iswdigit(L'7') != 0);
	CHECK(iswalpha(L'q') != 0);
	CHECK(!iswalpha(L'1'));

	wchar_t *end;
	CHECK(wcstol(L"  -123abc", &end, 10) == -123);
	CHECK(end && *end == L'a');
	CHECK(wcstoul(L"ff", nullptr, 16) == 255);
	CHECK(wcstod(L"3.5e2x", &end) == 350.0);
	CHECK(*end == L'x');
	CHECK(_wtoi(L"42") == 42);
	_itow(-77, buffer, 10);
	CHECK(wcscmp(buffer, L"-77") == 0);
	_itow(255, buffer, 16);
	CHECK(wcscmp(buffer, L"ff") == 0);
}

static void TestWidePrintf()
{
	wchar_t buffer[64];
	int n = swprintf(buffer, 64, L"%d-%u-%x-%X", -5, 7u, 255, 255);
	CHECK(n == 10 && wcscmp(buffer, L"-5-7-ff-FF") == 0);

	// %s is wide, %S narrow in the wide functions.
	n = swprintf(buffer, 64, L"[%s|%S|%ls|%hs]", L"wide", "narrow", L"lswide", "hsnarrow");
	CHECK(wcscmp(buffer, L"[wide|narrow|lswide|hsnarrow]") == 0);

	n = swprintf(buffer, 64, L"%c%C%lc%hc", L'a', 'b', L'c', 'd');
	CHECK(wcscmp(buffer, L"abcd") == 0);

	n = swprintf(buffer, 64, L"%5d|%-5d|%05d|%.2f|%8.3f|%+d", 42, 42, 42, 3.14159, 2.5, 3);
	CHECK(wcscmp(buffer, L"   42|42   |00042|3.14|   2.500|+3") == 0);

	n = swprintf(buffer, 64, L"%10s|%-10s|%.3s", L"abc", L"abc", L"abcdef");
	CHECK(wcscmp(buffer, L"       abc|abc       |abc") == 0);

	n = swprintf(buffer, 64, L"%*d|%.*f", 6, 12, 1, 9.96);
	CHECK(wcscmp(buffer, L"    12|10.0") == 0);

	n = swprintf(buffer, 64, L"%I64d %lld %llu", 5000000000LL, -5000000000LL, 5000000000ULL);
	CHECK(wcscmp(buffer, L"5000000000 -5000000000 5000000000") == 0);

	n = swprintf(buffer, 64, L"100%%");
	CHECK(wcscmp(buffer, L"100%") == 0);

	n = swprintf(buffer, 64, L"%s", (const wchar_t *)nullptr);
	CHECK(wcscmp(buffer, L"(null)") == 0);

	// Truncation: C99 swprintf terminates and fails.
	n = swprintf(buffer, 5, L"%s", L"abcdefgh");
	CHECK(n == -1 && wcscmp(buffer, L"abcd") == 0);

	// _snwprintf: a buffer that is exactly full has no terminator.
	wmemset(buffer, L'#', 10);
	n = _snwprintf(buffer, 4, L"%s", L"abcd");
	CHECK(n == 4 && buffer[3] == L'd' && buffer[4] == L'#');
	n = _snwprintf(buffer, 4, L"%s", L"abcdef");
	CHECK(n == -1);
	n = _snwprintf(buffer, 8, L"%s", L"abc");
	CHECK(n == 3 && buffer[3] == 0);

	char narrow[32];
	n = _snprintf(narrow, sizeof(narrow), "%s|%ls|%S|%d", "n", L"wide", L"wide2", 9);
	CHECK(n == 15 && strcmp(narrow, "n|wide|wide2|9") == 0 || strcmp(narrow, "n|wide|wide2|9") == 0);
	n = _snprintf(narrow, 4, "%s", "abcd");
	CHECK(n == 4 && memcmp(narrow, "abcd", 4) == 0);
	n = _snprintf(narrow, 4, "%s", "abcde");
	CHECK(n == -1);

	// Scanning.
	int a = 0, b = 0, c = 0, d = 0;
	n = swscanf(L"192.168.1.20", L"%d.%d.%d.%d", &a, &b, &c, &d);
	CHECK(n == 4 && a == 192 && b == 168 && c == 1 && d == 20);
	wchar_t word[16];
	float f = 0;
	n = swscanf(L"  name 2.5", L"%s %f", word, &f);
	CHECK(n == 2 && wcscmp(word, L"name") == 0 && f == 2.5f);
}

static void TestMultiByte()
{
	wchar_t wide[32];
	char narrow[32];

	// Windows-1252
	const char ansi[] = "caf\xE9 \x80";
	int n = MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, 32);
	CHECK(n == 7 && wide[3] == 0xE9 && wide[5] == 0x20AC);
	n = WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, 32, nullptr, nullptr);
	CHECK(n == 7 && strcmp(narrow, ansi) == 0);

	// UTF-8 with a surrogate pair
	const char utf8[] = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
	n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 32);
	CHECK(n == 6 && wide[1] == 0xE9 && wide[2] == 0x20AC && wide[3] == 0xD83D && wide[4] == 0xDE00);
	CHECK(MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0) == 6);
	n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, 32, nullptr, nullptr);
	CHECK(n == 11 && strcmp(narrow, utf8) == 0);

	BOOL usedDefault = FALSE;
	const wchar_t unmappable[] = { 'a', 0x4E2D, 0 };
	n = WideCharToMultiByte(CP_ACP, 0, unmappable, -1, narrow, 32, nullptr, &usedDefault);
	CHECK(n == 3 && narrow[1] == '?' && usedDefault);

	CHECK(WideCharToMultiByte(CP_ACP, 0, L"abc", 2, narrow, 1, nullptr, nullptr) == 0);
	CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

	// The C library conversions.
	CHECK(mbstowcs(wide, "h\xC3\xA9llo", 32) == 5 && wide[1] == 0xE9);
	CHECK(wcstombs(narrow, L"h\u00E9llo", 32) == 6);
	CHECK(mbstowcs(nullptr, "abc", 0) == 3);
}

static void TestFiles()
{
	CHECK(CreateDirectoryA("Data", nullptr));
	CHECK(CreateDirectoryA("Data\\INI", nullptr));
	CHECK(!CreateDirectoryA("Data", nullptr));
	CHECK(GetLastError() == ERROR_ALREADY_EXISTS);

	HANDLE file = CreateFileA("Data\\INI\\Test.ini", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	CHECK(file != INVALID_HANDLE_VALUE);
	DWORD written = 0;
	CHECK(WriteFile(file, "hello world", 11, &written, nullptr) && written == 11);
	CHECK(CloseHandle(file));

	// Different capitalisation and slashes.
	file = CreateFileA("data/ini/TEST.INI", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(file != INVALID_HANDLE_VALUE);
	CHECK(GetFileSize(file, nullptr) == 11);
	char buffer[32] = {};
	DWORD read = 0;
	CHECK(ReadFile(file, buffer, 5, &read, nullptr) && read == 5 && memcmp(buffer, "hello", 5) == 0);
	CHECK(SetFilePointer(file, 6, nullptr, FILE_BEGIN) == 6);
	CHECK(ReadFile(file, buffer, 31, &read, nullptr) && read == 5 && memcmp(buffer, "world", 5) == 0);
	CHECK(ReadFile(file, buffer, 31, &read, nullptr) && read == 0);
	CHECK(SetFilePointer(file, -5, nullptr, FILE_END) == 6);
	CHECK(CloseHandle(file));

	CHECK(CreateFileA("Data\\INI\\Missing.ini", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE);
	CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
	CHECK(CreateFileA("Nothing\\Missing.ini", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE);
	CHECK(GetLastError() == ERROR_PATH_NOT_FOUND);

	CHECK(GetFileAttributesA("DATA\\ini") & FILE_ATTRIBUTE_DIRECTORY);
	CHECK(GetFileAttributesA("Data\\INI\\test.ini") != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesA("Data\\INI\\nope.ini") == INVALID_FILE_ATTRIBUTES);

	// The C runtime
	int fd = _open("DATA\\Ini\\test.ini", _O_RDONLY | _O_BINARY);
	CHECK(fd >= 0);
	CHECK(_filelength(fd) == 11);
	_close(fd);
	CHECK(_access("data\\ini\\test.ini", 0) == 0);
	struct _stat st;
	CHECK(_stat("data\\ini", &st) == 0 && (st.st_mode & _S_IFDIR));
	FILE *stream = fopen("DATA\\INI\\TEST.INI", "rb");
	CHECK(stream != nullptr);
	if (stream)
		fclose(stream);

	// Enumeration
	HANDLE other = CreateFileA("Data\\INI\\Other.txt", GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
	CloseHandle(other);
	WIN32_FIND_DATAA data;
	HANDLE find = FindFirstFileA("Data\\INI\\*.ini", &data);
	CHECK(find != INVALID_HANDLE_VALUE && strcmp(data.cFileName, "Test.ini") == 0 && data.nFileSizeLow == 11);
	CHECK(!FindNextFileA(find, &data) && GetLastError() == ERROR_NO_MORE_FILES);
	CHECK(FindClose(find));
	find = FindFirstFileA("Data\\INI\\*.*", &data);
	int count = 0;
	bool more = find != INVALID_HANDLE_VALUE;
	while (more)
	{
		if (strcmp(data.cFileName, ".") && strcmp(data.cFileName, ".."))
			++count;
		more = FindNextFileA(find, &data) != FALSE;
	}
	CHECK(count == 2);
	FindClose(find);
	CHECK(FindFirstFileA("Data\\INI\\*.xyz", &data) == INVALID_HANDLE_VALUE);

	CHECK(CopyFileA("Data\\INI\\Test.ini", "Data\\Copy.ini", TRUE));
	CHECK(!CopyFileA("Data\\INI\\Test.ini", "Data\\Copy.ini", TRUE));
	CHECK(MoveFileA("Data\\Copy.ini", "Data\\Moved.ini"));
	CHECK(GetFileAttributesA("data\\moved.ini") != INVALID_FILE_ATTRIBUTES);
	CHECK(DeleteFileA("Data\\Moved.ini"));
	CHECK(!DeleteFileA("Data\\Moved.ini"));

	// Profile files
	CHECK(WritePrivateProfileStringA("Video", "Width", "800", "Data\\game.ini"));
	CHECK(WritePrivateProfileStringA("Video", "Height", "600", "Data\\game.ini"));
	CHECK(WritePrivateProfileStringA("Audio", "Volume", "7", "Data\\game.ini"));
	CHECK(GetPrivateProfileIntA("video", "WIDTH", 0, "data\\GAME.INI") == 800);
	CHECK(GetPrivateProfileIntA("Video", "Depth", 32, "Data\\game.ini") == 32);
	GetPrivateProfileStringA("Audio", "Volume", "none", buffer, sizeof(buffer), "Data\\game.ini");
	CHECK(strcmp(buffer, "7") == 0);
	CHECK(GetPrivateProfileStringA("Video", nullptr, "", buffer, sizeof(buffer), "Data\\game.ini") == 12);

	char path[MAX_PATH];
	char *filePart;
	DWORD length = GetFullPathNameA("a\\b\\..\\c.txt", sizeof(path), path, &filePart);
	CHECK(length > 0 && filePart && strcmp(filePart, "c.txt") == 0);
}

static void TestRegistry()
{
	HKEY key;
	CHECK(RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Electronic Arts\\EA Games\\Command and Conquer Generals Zero Hour", 0, KEY_READ, &key) == ERROR_SUCCESS);
	char value[64];
	DWORD size = sizeof(value);
	DWORD type = 0;
	CHECK(RegQueryValueExA(key, "InstallPath", nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS);
	CHECK(type == REG_SZ && strcmp(value, "/game/") == 0 && size == 7);
	size = sizeof(value);
	CHECK(RegQueryValueExA(key, "Language", nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && strcmp(value, "english") == 0);
	DWORD version = 0;
	size = sizeof(version);
	CHECK(RegQueryValueExA(key, "Version", nullptr, &type, (BYTE *)&version, &size) == ERROR_SUCCESS && type == REG_DWORD && version != 0);
	size = 2;
	CHECK(RegQueryValueExA(key, "InstallPath", nullptr, &type, (BYTE *)value, &size) == ERROR_MORE_DATA && size == 7);
	CHECK(RegQueryValueExA(key, "Nope", nullptr, &type, (BYTE *)value, &size) == ERROR_FILE_NOT_FOUND);
	RegCloseKey(key);

	CHECK(RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Nothing", 0, KEY_READ, &key) == ERROR_FILE_NOT_FOUND);
	CHECK(RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Test\\Sub", 0, nullptr, 0, KEY_WRITE, nullptr, &key, &type) == ERROR_SUCCESS && type == REG_CREATED_NEW_KEY);
	CHECK(RegSetValueExA(key, "Name", 0, REG_SZ, (const BYTE *)"value", 6) == ERROR_SUCCESS);
	RegCloseKey(key);
	CHECK(RegOpenKeyExA(HKEY_CURRENT_USER, "software\\test\\SUB", 0, KEY_READ, &key) == ERROR_SUCCESS);
	size = sizeof(value);
	CHECK(RegQueryValueExA(key, "name", nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && strcmp(value, "value") == 0);
}

static volatile LONG s_counter = 0;
static CRITICAL_SECTION s_section;

static DWORD WINAPI CounterThread(LPVOID parameter)
{
	for (int i = 0; i < 1000; ++i)
	{
		EnterCriticalSection(&s_section);
		EnterCriticalSection(&s_section);
		++s_counter;
		LeaveCriticalSection(&s_section);
		LeaveCriticalSection(&s_section);
		InterlockedIncrement((LONG *)parameter);
	}
	return 7;
}

static DWORD WINAPI WaitingThread(LPVOID parameter)
{
	HANDLE event = (HANDLE)parameter;
	return WaitForSingleObject(event, 5000) == WAIT_OBJECT_0 ? 1 : 0;
}

static void TestThreads()
{
	InitializeCriticalSection(&s_section);
	volatile LONG interlocked = 0;
	HANDLE threads[3];
	for (int i = 0; i < 3; ++i)
		threads[i] = CreateThread(nullptr, 0, CounterThread, (LPVOID)&interlocked, 0, nullptr);
	CHECK(WaitForMultipleObjects(3, threads, TRUE, 10000) == WAIT_OBJECT_0);
	DWORD exitCode = 0;
	CHECK(GetExitCodeThread(threads[0], &exitCode) && exitCode == 7);
	for (int i = 0; i < 3; ++i)
		CloseHandle(threads[i]);
	CHECK(s_counter == 3000 && interlocked == 3000);
	DeleteCriticalSection(&s_section);

	// A manual reset event releases a waiting thread; an auto reset event only once.
	HANDLE event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	HANDLE waiter = CreateThread(nullptr, 0, WaitingThread, event, 0, nullptr);
	Sleep(50);
	CHECK(WaitForSingleObject(waiter, 0) == WAIT_TIMEOUT);
	SetEvent(event);
	CHECK(WaitForSingleObject(waiter, 5000) == WAIT_OBJECT_0);
	GetExitCodeThread(waiter, &exitCode);
	CHECK(exitCode == 1);
	CloseHandle(waiter);
	CHECK(WaitForSingleObject(event, 0) == WAIT_OBJECT_0);
	ResetEvent(event);
	CHECK(WaitForSingleObject(event, 10) == WAIT_TIMEOUT);
	CloseHandle(event);

	HANDLE autoEvent = CreateEventA(nullptr, FALSE, TRUE, nullptr);
	CHECK(WaitForSingleObject(autoEvent, 0) == WAIT_OBJECT_0);
	CHECK(WaitForSingleObject(autoEvent, 0) == WAIT_TIMEOUT);
	CloseHandle(autoEvent);

	HANDLE mutex = CreateMutexA(nullptr, FALSE, "TestMutex");
	CHECK(mutex && GetLastError() == ERROR_SUCCESS);
	HANDLE again = CreateMutexA(nullptr, FALSE, "TestMutex");
	CHECK(again && GetLastError() == ERROR_ALREADY_EXISTS);
	CHECK(WaitForSingleObject(mutex, 0) == WAIT_OBJECT_0);
	CHECK(WaitForSingleObject(again, 0) == WAIT_OBJECT_0); // recursive
	CHECK(ReleaseMutex(mutex) && ReleaseMutex(mutex));
	CloseHandle(mutex);
	CloseHandle(again);

	HANDLE semaphore = CreateSemaphoreA(nullptr, 1, 2, nullptr);
	CHECK(WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0);
	CHECK(WaitForSingleObject(semaphore, 0) == WAIT_TIMEOUT);
	LONG previous = -1;
	CHECK(ReleaseSemaphore(semaphore, 2, &previous) && previous == 0);
	CHECK(!ReleaseSemaphore(semaphore, 1, nullptr));
	CloseHandle(semaphore);

	DWORD slot = TlsAlloc();
	CHECK(slot != TLS_OUT_OF_INDEXES);
	CHECK(TlsSetValue(slot, (LPVOID)123) && TlsGetValue(slot) == (LPVOID)123);
	TlsFree(slot);

	DWORD start = GetTickCount();
	Sleep(60);
	DWORD elapsed = GetTickCount() - start;
	CHECK(elapsed >= 50 && elapsed < 1000);
	LARGE_INTEGER frequency, counter1, counter2;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&counter1);
	Sleep(10);
	QueryPerformanceCounter(&counter2);
	CHECK(frequency.QuadPart > 0 && counter2.QuadPart > counter1.QuadPart);
}

static unsigned __stdcall CrtThread(void *argument)
{
	*(int *)argument = 99;
	return 0;
}

static void TestMisc()
{
	SYSTEMTIME st = { 2024, 2, 0, 29, 13, 45, 30, 123 };
	FILETIME ft;
	CHECK(SystemTimeToFileTime(&st, &ft));
	SYSTEMTIME back;
	CHECK(FileTimeToSystemTime(&ft, &back));
	CHECK(back.wYear == 2024 && back.wMonth == 2 && back.wDay == 29 && back.wHour == 13 && back.wMinute == 45 && back.wSecond == 30 && back.wMilliseconds == 123);
	CHECK(back.wDayOfWeek == 4); // a Thursday
	char text[64];
	CHECK(GetDateFormatA(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, text, sizeof(text)) > 0);
	CHECK(strcmp(text, "Thursday, February 29, 2024") == 0);
	CHECK(GetDateFormatA(LOCALE_USER_DEFAULT, 0, &st, "yyyy-MM-dd", text, sizeof(text)) > 0 && strcmp(text, "2024-02-29") == 0);
	CHECK(GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &st, nullptr, text, sizeof(text)) > 0 && strcmp(text, "1:45:30 PM") == 0);

	char drive[8], dir[64], name[64], ext[16];
	_splitpath("C:\\Games\\Zero Hour\\game.exe", drive, dir, name, ext);
	CHECK(strcmp(drive, "C:") == 0 && strcmp(dir, "\\Games\\Zero Hour\\") == 0 && strcmp(name, "game") == 0 && strcmp(ext, ".exe") == 0);

	CHECK(_stricmp("HeLLo", "hello") == 0);
	CHECK(strcmp(_strupr(strcpy(text, "abc")), "ABC") == 0);
	CHECK(strcmp(_itoa(-45, text, 10), "-45") == 0);
	CHECK(_tcslen(_T("abcd")) == 4);

	CHECK(MulDiv(10, 3, 4) == 8);
	CHECK(MulDiv(-10, 3, 4) == -8);
	CHECK(MulDiv(1, 1, 0) == -1);

	// Memory
	void *memory = GlobalAlloc(GPTR, 100);
	CHECK(memory && GlobalSize(memory) >= 100 && ((char *)memory)[99] == 0);
	GlobalFree(memory);
	memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64);
	CHECK(memory && HeapSize(GetProcessHeap(), 0, memory) >= 64);
	HeapFree(GetProcessHeap(), 0, memory);

	MEMORYSTATUS status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	CHECK(status.dwTotalPhys > 0 && status.dwAvailPhys > 0);

	// Windows, messages
	WNDCLASSA wc = {};
	wc.lpszClassName = "TestClass";
	wc.lpfnWndProc = [](HWND, UINT message, WPARAM wParam, LPARAM) -> LRESULT { return message == WM_USER ? (LRESULT)wParam + 1 : 0; };
	CHECK(RegisterClassA(&wc) != 0);
	HWND window = CreateWindowExA(0, "TestClass", "Title", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, nullptr, nullptr, nullptr, nullptr);
	CHECK(window && IsWindow(window) && IsWindowVisible(window));
	CHECK(GetWindowLongA(window, GWL_STYLE) == (LONG)(WS_POPUP | WS_VISIBLE));
	CHECK(((WNDPROC)GetWindowLongA(window, GWL_WNDPROC))(window, WM_USER, 41, 0) == 42);
	char title[16];
	CHECK(GetWindowTextA(window, title, sizeof(title)) == 5 && strcmp(title, "Title") == 0);
	DestroyWindow(window);
	CHECK(!IsWindow(window));

	// The C runtime thread functions
	int value = 0;
	uintptr_t thread = _beginthreadex(nullptr, 0, CrtThread, &value, 0, nullptr);
	CHECK(thread != 0);
	WaitForSingleObject((HANDLE)thread, 5000);
	CHECK(value == 99);
	CloseHandle((HANDLE)thread);

	CHECK(__argc == 0);
	char folder[MAX_PATH];
	CHECK(SHGetSpecialFolderPathA(nullptr, folder, CSIDL_PERSONAL, TRUE) && strcmp(folder, "/userdata") == 0);
	CHECK(GetFileAttributesA("/userdata") & FILE_ATTRIBUTE_DIRECTORY);
	CHECK(GetModuleHandleA("shell32.dll") == nullptr);
	char module[MAX_PATH];
	CHECK(GetModuleFileNameA(nullptr, module, sizeof(module)) > 0 && strcmp(module, "/game/generalszh.exe") == 0);

	// Sockets
	WSADATA wsa;
	CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
	SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(s != INVALID_SOCKET);
	u_long nonBlocking = 1;
	CHECK(ioctlsocket(s, FIONBIO, &nonBlocking) == 0);
	char datagram[16];
	SOCKADDR_IN from;
	int fromLength = sizeof(from);
	CHECK(recvfrom(s, datagram, sizeof(datagram), 0, (SOCKADDR *)&from, &fromLength) == SOCKET_ERROR);
	// A literal null length matches neither overload of the int/socklen_t pair.
	CHECK(recvfrom(s, datagram, (int)sizeof(datagram), 0, nullptr, nullptr) == SOCKET_ERROR);
	closesocket(s);
	WSACleanup();
}

static void TestOleAutomationAndExtras()
{
	// BSTR: length prefix, zero terminator, embedded zeros kept
	BSTR bstr = SysAllocString(L"Hello");
	CHECK(bstr != nullptr && SysStringLen(bstr) == 5 && wcscmp(bstr, L"Hello") == 0);
	SysFreeString(bstr);
	OLECHAR raw[3] = { L'a', 0, L'b' };
	bstr = SysAllocStringLen(raw, 3);
	CHECK(bstr != nullptr && SysStringLen(bstr) == 3 && bstr[2] == L'b' && bstr[3] == 0);
	SysFreeString(bstr);
	CHECK(SysAllocString(nullptr) == nullptr && SysStringLen(nullptr) == 0);

	// There are no type libraries
	ITypeLib *typeLib = (ITypeLib *)1;
	CHECK(LoadTypeLib(L"x.tlb", &typeLib) == TYPE_E_CANTLOADLIBRARY && typeLib == nullptr);

	// Wide module name
	WCHAR wide[MAX_PATH];
	CHECK(GetModuleFileNameW(nullptr, wide, MAX_PATH) > 0 && wcscmp(wide, L"/game/generalszh.exe") == 0);
	WCHAR small[4];
	CHECK(GetModuleFileNameW(nullptr, small, 4) == 4 && small[3] == 0);

	// One monitor, the screen
	webcompat_set_screen_size(1024, 768);
	MONITORINFO info = { sizeof(MONITORINFO) };
	CHECK(GetMonitorInfo(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &info));
	CHECK(info.rcMonitor.right == 1024 && info.rcWork.bottom == 768 && (info.dwFlags & MONITORINFOF_PRIMARY));

	// Processes and pipes cannot be created
	HANDLE read = (HANDLE)1, write = (HANDLE)1;
	CHECK(!CreatePipe(&read, &write, nullptr, 0) && read == nullptr);
	STARTUPINFOW startup = { sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION process = {};
	WCHAR command[] = L"worker.exe";
	CHECK(!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));

	// Pointer-sized interlocked operations
	void *volatile slot = nullptr;
	int a, b;
	CHECK(InterlockedCompareExchangePointer((PVOID volatile *)&slot, &a, nullptr) == nullptr && slot == &a);
	CHECK(InterlockedCompareExchangePointer((PVOID volatile *)&slot, &b, nullptr) == &a && slot == &a);
	CHECK(InterlockedExchangePointer((PVOID volatile *)&slot, &b) == &a && slot == &b);

	// No input context for the IME, and AVI files cannot be written
	CHECK(ImmGetContext(nullptr) == nullptr);
	PAVIFILE avi = (PAVIFILE)1;
	AVIFileInit();
	CHECK(AVIFileOpen(&avi, "x.avi", OF_WRITE | OF_CREATE, nullptr) == AVIERR_FILEOPEN && avi == nullptr);
	AVIFileExit();

	// Multibyte characters are bytes
	CHECK(_mbsnccnt((const unsigned char *)"abcdef", 4) == 4);
	CHECK(_mbsnccnt((const unsigned char *)"ab", 4) == 2);
}

int main()
{
	TestWideStrings();
	TestWidePrintf();
	TestMultiByte();
	TestFiles();
	TestRegistry();
	TestThreads();
	TestMisc();
	TestOleAutomationAndExtras();
	printf("%d checks, %d failures\n", s_checks, s_failures);
	return s_failures ? 1 : 0;
}
