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
** WebAssembly port: Win32 error handling, debugging, system information,
** memory, module and COM functions.
*/
#include "webcompat_internal.h"

#include <strings.h>

#include <errno.h>
#ifndef __APPLE__
#include <malloc.h>
#endif
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <webcompat_folders.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#endif

using namespace WebCompat;

namespace
{

thread_local DWORD t_lastError = 0;

WebCompatMessageBoxHandler s_messageBoxHandler = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER s_unhandledExceptionFilter = nullptr;
UINT s_errorMode = 0;

// A heap handle only tells HeapAlloc what was asked for; every heap is the
// process heap.
struct HeapObject
{
	uint32_t magic;
};
HeapObject s_processHeap = { 0x50414548 };

size_t UsableSize(const void *pointer)
{
	return pointer ? malloc_usable_size(const_cast<void *>(pointer)) : 0;
}

const char *Win32ErrorText(DWORD id)
{
	switch (id)
	{
	case ERROR_SUCCESS: return "The operation completed successfully.";
	case ERROR_FILE_NOT_FOUND: return "The system cannot find the file specified.";
	case ERROR_PATH_NOT_FOUND: return "The system cannot find the path specified.";
	case ERROR_ACCESS_DENIED: return "Access is denied.";
	case ERROR_INVALID_HANDLE: return "The handle is invalid.";
	case ERROR_NOT_ENOUGH_MEMORY: return "Not enough storage is available to process this command.";
	case ERROR_NO_MORE_FILES: return "There are no more files.";
	case ERROR_FILE_EXISTS: return "The file exists.";
	case ERROR_INVALID_PARAMETER: return "The parameter is incorrect.";
	case ERROR_INSUFFICIENT_BUFFER: return "The data area passed to a system call is too small.";
	case ERROR_ALREADY_EXISTS: return "Cannot create a file when that file already exists.";
	case 126: return "The specified module could not be found.";
	default: return nullptr;
	}
}

} // namespace

namespace WebCompat
{

DWORD ErrnoToWin32Error(int err)
{
	switch (err)
	{
	case 0: return ERROR_SUCCESS;
	case ENOENT: return ERROR_FILE_NOT_FOUND;
	case ENOTDIR: return ERROR_PATH_NOT_FOUND;
	case EACCES:
	case EPERM:
	case EROFS: return ERROR_ACCESS_DENIED;
	case EBADF: return ERROR_INVALID_HANDLE;
	case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
	case EEXIST: return ERROR_ALREADY_EXISTS;
	case EINVAL: return ERROR_INVALID_PARAMETER;
	case ENOTEMPTY: return 145; // ERROR_DIR_NOT_EMPTY
	case ENOSPC: return 112; // ERROR_DISK_FULL
	case EISDIR: return ERROR_ACCESS_DENIED;
	case EMFILE: return 4; // ERROR_TOO_MANY_OPEN_FILES
	case EBUSY: return 32; // ERROR_SHARING_VIOLATION
	default: return ERROR_ACCESS_DENIED;
	}
}

void SetLastErrorFromErrno()
{
	t_lastError = ErrnoToWin32Error(errno);
}

} // namespace WebCompat

extern "C" {

DWORD WINAPI GetLastError(void)
{
	return t_lastError;
}

void WINAPI SetLastError(DWORD dwErrCode)
{
	t_lastError = dwErrCode;
}

void WINAPI OutputDebugStringA(LPCSTR lpOutputString)
{
	if (lpOutputString)
	{
		fputs(lpOutputString, stderr);
		fflush(stderr);
	}
}

void WINAPI OutputDebugStringW(LPCWSTR lpOutputString)
{
	if (!lpOutputString)
		return;
	char narrow[1024];
	const int length = WideCharToMultiByte(CP_ACP, 0, lpOutputString, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
	narrow[length > 0 ? length : 0] = 0;
	OutputDebugStringA(narrow);
}

void WINAPI DebugBreak(void)
{
	__builtin_trap();
}

BOOL WINAPI IsDebuggerPresent(void)
{
	return FALSE;
}

BOOL WINAPI IsBadReadPtr(const void *lp, UINT_PTR ucb)
{
	if (ucb == 0)
		return FALSE;
	const uintptr_t start = reinterpret_cast<uintptr_t>(lp);
#ifdef __EMSCRIPTEN__
	const uintptr_t memorySize = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
#else
	const uintptr_t memorySize = UINTPTR_MAX;
#endif
	return start == 0 || start + ucb < start || start + ucb > memorySize;
}

BOOL WINAPI IsBadWritePtr(LPVOID lp, UINT_PTR ucb)
{
	return IsBadReadPtr(lp, ucb);
}

BOOL WINAPI IsBadCodePtr(FARPROC lpfn)
{
	return lpfn == nullptr;
}

BOOL WINAPI IsBadStringPtrA(LPCSTR lpsz, UINT_PTR ucchMax)
{
	return ucchMax != 0 && IsBadReadPtr(lpsz, 1);
}

UINT WINAPI SetErrorMode(UINT uMode)
{
	const UINT previous = s_errorMode;
	s_errorMode = uMode;
	return previous;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
	LPTOP_LEVEL_EXCEPTION_FILTER previous = s_unhandledExceptionFilter;
	s_unhandledExceptionFilter = lpTopLevelExceptionFilter;
	return previous;
}

DWORD WINAPI FormatMessageA(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD, LPSTR lpBuffer, DWORD nSize, va_list *Arguments)
{
	char text[512];
	if (dwFlags & FORMAT_MESSAGE_FROM_STRING)
	{
		if (!lpSource)
			return 0;
		if ((dwFlags & FORMAT_MESSAGE_IGNORE_INSERTS) || !Arguments)
			snprintf(text, sizeof(text), "%s", static_cast<const char *>(lpSource));
		else
			vsnprintf(text, sizeof(text), static_cast<const char *>(lpSource), *Arguments);
	}
	else
	{
		const char *known = Win32ErrorText(dwMessageId);
		if (known)
			snprintf(text, sizeof(text), "%s\r\n", known);
		else
			snprintf(text, sizeof(text), "Unknown error %lu.\r\n", dwMessageId);
	}

	const size_t length = strlen(text);
	if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
	{
		char *allocated = static_cast<char *>(malloc(length + 1));
		if (!allocated)
			return 0;
		memcpy(allocated, text, length + 1);
		*reinterpret_cast<char **>(lpBuffer) = allocated;
		return (DWORD)length;
	}
	if (!lpBuffer || nSize == 0)
		return 0;
	if (length + 1 > nSize)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	memcpy(lpBuffer, text, length + 1);
	return (DWORD)length;
}

DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize, va_list *Arguments)
{
	if (dwFlags & FORMAT_MESSAGE_FROM_STRING)
	{
		// Wide format strings are not used by the game.
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	char narrow[512];
	const DWORD length = FormatMessageA(dwFlags & ~FORMAT_MESSAGE_ALLOCATE_BUFFER, lpSource, dwMessageId, dwLanguageId, narrow, sizeof(narrow), Arguments);
	if (length == 0)
		return 0;
	const int wideLength = MultiByteToWideChar(CP_ACP, 0, narrow, (int)length + 1, nullptr, 0);
	if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
	{
		WCHAR *allocated = static_cast<WCHAR *>(malloc(wideLength * sizeof(WCHAR)));
		if (!allocated)
			return 0;
		MultiByteToWideChar(CP_ACP, 0, narrow, (int)length + 1, allocated, wideLength);
		*reinterpret_cast<WCHAR **>(lpBuffer) = allocated;
		return length;
	}
	if (!lpBuffer || (DWORD)wideLength > nSize)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	MultiByteToWideChar(CP_ACP, 0, narrow, (int)length + 1, lpBuffer, wideLength);
	return length;
}

void webcompat_set_message_box_handler(WebCompatMessageBoxHandler handler)
{
	s_messageBoxHandler = handler;
}

int WINAPI MessageBoxA(HWND, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	if (s_messageBoxHandler)
		return s_messageBoxHandler(lpText, lpCaption, uType);

	fprintf(stderr, "[MessageBox] %s: %s\n", lpCaption ? lpCaption : "", lpText ? lpText : "");
	// Nobody can answer the box: pick the answer that lets the program go on.
	switch (uType & 0xF)
	{
	case MB_YESNO:
	case MB_YESNOCANCEL: return IDYES;
	case MB_ABORTRETRYIGNORE: return IDIGNORE;
	case MB_RETRYCANCEL: return IDCANCEL;
	default: return IDOK;
	}
}

int WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
	char text[1024];
	char caption[256];
	int length = lpText ? WideCharToMultiByte(CP_UTF8, 0, lpText, -1, text, sizeof(text) - 1, nullptr, nullptr) : 0;
	text[length > 0 ? length : 0] = 0;
	length = lpCaption ? WideCharToMultiByte(CP_UTF8, 0, lpCaption, -1, caption, sizeof(caption) - 1, nullptr, nullptr) : 0;
	caption[length > 0 ? length : 0] = 0;
	return MessageBoxA(hWnd, text, caption, uType);
}

BOOL WINAPI MessageBeep(UINT)
{
	return TRUE;
}

void WINAPI FatalAppExitA(UINT, LPCSTR lpMessageText)
{
	fprintf(stderr, "[FatalAppExit] %s\n", lpMessageText ? lpMessageText : "");
	abort();
}

/* ---------------------------------------------------------------------------
** Modules and resources
** ------------------------------------------------------------------------- */

// The renderer's Direct3D 8 implementation (Dependencies/WebD3D8) is the only "library" there is:
// the game loads it with LoadLibrary("D3D8.DLL") and GetProcAddress("Direct3DCreate8"). It is
// weak so that programs that do not contain it (the tests) still link.
extern "C" void *WebD3D8_LookupProc(const char *name) __attribute__((weak));

namespace
{
HMODULE const D3D8_MODULE = reinterpret_cast<HMODULE>(0x00D38000);
}

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName)
{
	if (lpLibFileName && WebD3D8_LookupProc)
	{
		// "D3D8.DLL", "d3d8", with or without a path, in any case.
		const char *name = lpLibFileName;
		for (const char *p = lpLibFileName; *p; ++p)
		{
			if (*p == '\\' || *p == '/')
				name = p + 1;
		}
		if (strcasecmp(name, "d3d8.dll") == 0 || strcasecmp(name, "d3d8") == 0)
			return D3D8_MODULE;
	}

	// There are no other dynamic libraries in the browser: everything else the game
	// looks up this way is optional and has a fallback.
	SetLastError(126); // ERROR_MOD_NOT_FOUND
	return nullptr;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName)
{
	if (lpModuleName)
	{
		SetLastError(126);
		return nullptr;
	}
	return reinterpret_cast<HMODULE>(0x00400000);
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName)
{
	if (lpModuleName)
	{
		SetLastError(126);
		return nullptr;
	}
	return reinterpret_cast<HMODULE>(0x00400000);
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
	char narrow[MAX_PATH];
	const DWORD length = GetModuleFileNameA(hModule, narrow, sizeof(narrow));
	if (!lpFilename || nSize == 0)
		return 0;
	// The path is ASCII.
	DWORD copied = length < nSize ? length : nSize - 1;
	for (DWORD i = 0; i < copied; ++i)
		lpFilename[i] = (unsigned char)narrow[i];
	lpFilename[copied] = 0;
	if (copied < length)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return nSize;
	}
	return copied;
}

DWORD WINAPI GetModuleFileNameA(HMODULE, LPSTR lpFilename, DWORD nSize)
{
	// The game cuts the executable's directory off at the last backslash (log files, MemoryPools.ini,
// working directory), so the file name is separated like on Windows. The path functions accept both.
	static const char s_path[] = "/game\\generalszh.exe";
	if (!lpFilename || nSize == 0)
		return 0;
	const DWORD length = (DWORD)(sizeof(s_path) - 1);
	if (length + 1 > nSize)
	{
		memcpy(lpFilename, s_path, nSize - 1);
		lpFilename[nSize - 1] = 0;
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return nSize;
	}
	memcpy(lpFilename, s_path, length + 1);
	return length;
}

FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	if (hModule == D3D8_MODULE && WebD3D8_LookupProc && !IS_INTRESOURCE(lpProcName))
	{
		if (void *proc = WebD3D8_LookupProc(lpProcName))
			return reinterpret_cast<FARPROC>(proc);
	}
	SetLastError(127); // ERROR_PROC_NOT_FOUND
	return nullptr;
}

BOOL WINAPI FreeLibrary(HMODULE)
{
	return TRUE;
}

HRSRC WINAPI FindResourceA(HMODULE, LPCSTR, LPCSTR)
{
	SetLastError(1814); // ERROR_RESOURCE_NAME_NOT_FOUND
	return nullptr;
}

HGLOBAL WINAPI LoadResource(HMODULE, HRSRC)
{
	return nullptr;
}

LPVOID WINAPI LockResource(HGLOBAL)
{
	return nullptr;
}

DWORD WINAPI SizeofResource(HMODULE, HRSRC)
{
	return 0;
}

int WINAPI LoadStringA(HINSTANCE, UINT, LPSTR lpBuffer, int cchBufferMax)
{
	if (lpBuffer && cchBufferMax > 0)
		lpBuffer[0] = 0;
	return 0;
}

/* ---------------------------------------------------------------------------
** System information
** ------------------------------------------------------------------------- */

BOOL WINAPI GetVersionExA(LPOSVERSIONINFO lpVersionInformation)
{
	if (!lpVersionInformation || lpVersionInformation->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA))
		return FALSE;
	// Windows XP
	lpVersionInformation->dwMajorVersion = 5;
	lpVersionInformation->dwMinorVersion = 1;
	lpVersionInformation->dwBuildNumber = 2600;
	lpVersionInformation->dwPlatformId = VER_PLATFORM_WIN32_NT;
	lpVersionInformation->szCSDVersion[0] = 0;
	return TRUE;
}

DWORD WINAPI GetVersion(void)
{
	return 0x0A280105; // 5.1, build 2600, NT
}

void WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo)
{
	memset(lpSystemInfo, 0, sizeof(*lpSystemInfo));
	lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
	lpSystemInfo->dwPageSize = 65536;
	lpSystemInfo->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(0x10000);
	lpSystemInfo->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(0x7FFEFFFF);
	long processors = sysconf(_SC_NPROCESSORS_ONLN);
	if (processors < 1)
		processors = 1;
	lpSystemInfo->dwNumberOfProcessors = (DWORD)processors;
	lpSystemInfo->dwActiveProcessorMask = processors >= 32 ? 0xFFFFFFFFu : ((1u << processors) - 1u);
	lpSystemInfo->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
	lpSystemInfo->dwAllocationGranularity = 65536;
	lpSystemInfo->wProcessorLevel = 6;
}

void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer)
{
#ifdef __EMSCRIPTEN__
	// The memory the program can use is what the heap may grow to.
	const size_t limit = emscripten_get_heap_max();
	const size_t used = (size_t)(uintptr_t)sbrk(0);
#else
	// Native headless build: the numbers of a web page that uses a quarter of its 4 GB, so that nothing the
	// game derives from them differs from the web build.
	const size_t limit = 0xFFFF0000u;
	const size_t used = limit / 4;
#endif
	const size_t available = limit > used ? limit - used : 0;
	lpBuffer->dwLength = sizeof(MEMORYSTATUS);
	lpBuffer->dwMemoryLoad = limit ? (DWORD)((uint64_t)used * 100 / limit) : 0;
	lpBuffer->dwTotalPhys = limit;
	lpBuffer->dwAvailPhys = available;
	lpBuffer->dwTotalPageFile = limit;
	lpBuffer->dwAvailPageFile = available;
	lpBuffer->dwTotalVirtual = limit;
	lpBuffer->dwAvailVirtual = available;
}

BOOL WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize)
{
	static const char s_name[] = "WEBBROWSER";
	if (!lpBuffer || !nSize || *nSize < sizeof(s_name))
	{
		if (nSize)
			*nSize = sizeof(s_name);
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}
	memcpy(lpBuffer, s_name, sizeof(s_name));
	*nSize = sizeof(s_name) - 1;
	return TRUE;
}

BOOL WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer)
{
	static const char s_name[] = "Player";
	if (!lpBuffer || !pcbBuffer || *pcbBuffer < sizeof(s_name))
	{
		if (pcbBuffer)
			*pcbBuffer = sizeof(s_name);
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return FALSE;
	}
	memcpy(lpBuffer, s_name, sizeof(s_name));
	*pcbBuffer = sizeof(s_name);
	return TRUE;
}

LANGID WINAPI GetSystemDefaultLangID(void)
{
	return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

LANGID WINAPI GetUserDefaultLangID(void)
{
	return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

LCID WINAPI GetUserDefaultLCID(void)
{
	return 0x0409;
}

LCID WINAPI GetSystemDefaultLCID(void)
{
	return 0x0409;
}

UINT WINAPI GetACP(void)
{
	return 1252;
}

HKL WINAPI GetKeyboardLayout(DWORD)
{
	return reinterpret_cast<HKL>(0x04090409);
}

UINT WINAPI GetDoubleClickTime(void)
{
	return 500;
}

DWORD WINAPI GetLogicalDrives(void)
{
	return 1u << 2; // C:
}

DWORD WINAPI GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer)
{
	static const char s_drives[] = "C:\\\0";
	if (nBufferLength < sizeof(s_drives))
		return sizeof(s_drives);
	memcpy(lpBuffer, s_drives, sizeof(s_drives));
	return sizeof(s_drives) - 1;
}

UINT WINAPI GetDriveTypeA(LPCSTR)
{
	return DRIVE_FIXED;
}

BOOL WINAPI GetVolumeInformationA(LPCSTR, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
	if (lpVolumeNameBuffer && nVolumeNameSize)
		snprintf(lpVolumeNameBuffer, nVolumeNameSize, "GAME");
	if (lpVolumeSerialNumber)
		*lpVolumeSerialNumber = 0x1234ABCD;
	if (lpMaximumComponentLength)
		*lpMaximumComponentLength = 255;
	if (lpFileSystemFlags)
		*lpFileSystemFlags = 0;
	if (lpFileSystemNameBuffer && nFileSystemNameSize)
		snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "NTFS");
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceA(LPCSTR, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
	if (lpSectorsPerCluster) *lpSectorsPerCluster = 8;
	if (lpBytesPerSector) *lpBytesPerSector = 512;
	if (lpNumberOfFreeClusters) *lpNumberOfFreeClusters = 1u << 20;
	if (lpTotalNumberOfClusters) *lpTotalNumberOfClusters = 1u << 21;
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR, PULARGE_INTEGER lpFreeBytesAvailableToCaller, PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes)
{
	const ULONGLONG free = 4ull << 30;
	if (lpFreeBytesAvailableToCaller) lpFreeBytesAvailableToCaller->QuadPart = free;
	if (lpTotalNumberOfBytes) lpTotalNumberOfBytes->QuadPart = 8ull << 30;
	if (lpTotalNumberOfFreeBytes) lpTotalNumberOfFreeBytes->QuadPart = free;
	return TRUE;
}

static UINT CopyDirectory(const char *directory, LPSTR lpBuffer, UINT uSize)
{
	const UINT length = (UINT)strlen(directory);
	if (length + 1 > uSize)
		return length + 1;
	memcpy(lpBuffer, directory, length + 1);
	return length;
}

UINT WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize)
{
	return CopyDirectory("/windows", lpBuffer, uSize);
}

UINT WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize)
{
	return CopyDirectory("/windows/system32", lpBuffer, uSize);
}

DWORD WINAPI GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer)
{
	return CopyDirectory("/tmp/", lpBuffer, nBufferLength);
}

UINT WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName)
{
	static std::atomic<UINT> s_counter(1);
	const UINT unique = uUnique ? uUnique : (s_counter.fetch_add(1) & 0xFFFF);
	char name[MAX_PATH];
	const size_t pathLength = strlen(lpPathName);
	const bool hasSeparator = pathLength && (lpPathName[pathLength - 1] == '\\' || lpPathName[pathLength - 1] == '/');
	snprintf(name, sizeof(name), "%s%s%.3s%X.tmp", lpPathName, hasSeparator ? "" : "/", lpPrefixString ? lpPrefixString : "", unique);
	strcpy(lpTempFileName, name);
	if (uUnique == 0)
	{
		// The function creates the file when it picks the number itself.
		FILE *file = fopen(name, "ab");
		if (file)
			fclose(file);
	}
	return unique;
}

int WINAPI MulDiv(int nNumber, int nNumerator, int nDenominator)
{
	if (nDenominator == 0)
		return -1;
	// Round to nearest, away from zero on ties, as Windows does.
	const long long product = (long long)nNumber * nNumerator;
	const bool negative = (product < 0) != (nDenominator < 0);
	const unsigned long long magnitude = (unsigned long long)(product < 0 ? -product : product);
	const unsigned long long divisor = (unsigned long long)(nDenominator < 0 ? -(long long)nDenominator : (long long)nDenominator);
	const unsigned long long result = (magnitude + divisor / 2) / divisor;
	if (negative)
		return result > 2147483648ull ? -1 : (int)(0 - (long long)result);
	return result > 2147483647ull ? -1 : (int)result;
}

/* ---------------------------------------------------------------------------
** The game's folders (webcompat_folders.h). Shell folders: all of them are
** below the user data folder, /userdata by default.
** ------------------------------------------------------------------------- */

} // extern "C"

namespace
{

struct GameFolders
{
	std::string zeroHour = "/game/";
	std::string generals = "/generals/";
	std::string userData = "/userdata";
	std::string desktop = "/userdata/Desktop";
	std::string appData = "/userdata/AppData";
};

GameFolders &Folders()
{
	static GameFolders folders;
	return folders;
}

std::string WithoutTrailingSlash(const char *path)
{
	std::string text(path);
	while (text.size() > 1 && (text.back() == '/' || text.back() == '\\'))
		text.pop_back();
	return text;
}

} // namespace

extern "C" void WebCompat_SetGameFolders(const char *zeroHour, const char *generals, const char *userData)
{
	GameFolders &folders = Folders();
	if (zeroHour)
		folders.zeroHour = WithoutTrailingSlash(zeroHour) + "/";
	if (generals)
		folders.generals = WithoutTrailingSlash(generals) + "/";
	if (userData)
	{
		folders.userData = WithoutTrailingSlash(userData);
		folders.desktop = folders.userData + "/Desktop";
		folders.appData = folders.userData + "/AppData";
	}
}

const char *WebCompat::InstallFolder(bool generals)
{
	return generals ? Folders().generals.c_str() : Folders().zeroHour.c_str();
}

extern "C" {

static const char *FolderPath(int csidl)
{
	switch (csidl & 0xFF)
	{
	case CSIDL_DESKTOP:
	case CSIDL_DESKTOPDIRECTORY: return Folders().desktop.c_str();
	case CSIDL_PERSONAL: return Folders().userData.c_str();
	case CSIDL_APPDATA:
	case CSIDL_LOCAL_APPDATA:
	case CSIDL_COMMON_APPDATA: return Folders().appData.c_str();
	default: return nullptr;
	}
}

static BOOL CopyFolderPath(int csidl, bool create, LPSTR pszPath)
{
	const char *path = FolderPath(csidl);
	if (!path || !pszPath)
		return FALSE;
	strcpy(pszPath, path);
	if (create || (csidl & CSIDL_FLAG_CREATE))
	{
		// Make sure the folder exists, parents first.
		CreateDirectoryA(Folders().userData.c_str(), nullptr);
		CreateDirectoryA(path, nullptr);
	}
	return TRUE;
}

BOOL WINAPI SHGetSpecialFolderPathA(HWND, LPSTR pszPath, int csidl, BOOL fCreate)
{
	return CopyFolderPath(csidl, fCreate != FALSE, pszPath);
}

HRESULT WINAPI SHGetFolderPathA(HWND, int csidl, HANDLE, DWORD, LPSTR pszPath)
{
	return CopyFolderPath(csidl, false, pszPath) ? S_OK : E_INVALIDARG;
}

// An "item id list" only carries the folder number here.
HRESULT WINAPI SHGetSpecialFolderLocation(HWND, int csidl, LPITEMIDLIST *ppidl)
{
	if (!ppidl || !FolderPath(csidl))
		return E_INVALIDARG;
	LPITEMIDLIST list = static_cast<LPITEMIDLIST>(CoTaskMemAlloc(sizeof(ITEMIDLIST)));
	if (!list)
		return E_OUTOFMEMORY;
	memset(list, 0, sizeof(*list));
	list->id[0] = (BYTE)csidl;
	*ppidl = list;
	return S_OK;
}

BOOL WINAPI SHGetPathFromIDListA(LPCITEMIDLIST pidl, LPSTR pszPath)
{
	return pidl && CopyFolderPath(pidl->id[0], false, pszPath);
}

const GUID FOLDERID_Documents = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE)
{
	return ES_CONTINUOUS;
}

int __argc = 0;
char **__argv = nullptr;

/* ---------------------------------------------------------------------------
** Memory
** ------------------------------------------------------------------------- */

HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes)
{
	// Moveable memory is not moved in a flat address space: the handle is
	// the pointer.
	void *memory = (uFlags & GMEM_ZEROINIT) ? calloc(1, dwBytes ? dwBytes : 1) : malloc(dwBytes ? dwBytes : 1);
	if (!memory)
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return memory;
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags)
{
	const size_t oldSize = UsableSize(hMem);
	void *memory = realloc(hMem, dwBytes ? dwBytes : 1);
	if (memory && (uFlags & GMEM_ZEROINIT) && dwBytes > oldSize)
		memset(static_cast<char *>(memory) + oldSize, 0, dwBytes - oldSize);
	return memory;
}

HGLOBAL WINAPI GlobalFree(HGLOBAL hMem)
{
	free(hMem);
	return nullptr;
}

LPVOID WINAPI GlobalLock(HGLOBAL hMem)
{
	return hMem;
}

BOOL WINAPI GlobalUnlock(HGLOBAL)
{
	return FALSE;
}

SIZE_T WINAPI GlobalSize(HGLOBAL hMem)
{
	return UsableSize(hMem);
}

HLOCAL WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes)
{
	return GlobalAlloc(uFlags, uBytes);
}

HLOCAL WINAPI LocalFree(HLOCAL hMem)
{
	free(hMem);
	return nullptr;
}

LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD)
{
	if (lpAddress)
	{
		// Committing inside memory that was reserved earlier: it is already usable.
		return (flAllocationType & MEM_COMMIT) ? lpAddress : nullptr;
	}
	const SIZE_T rounded = (dwSize + 4095) & ~(SIZE_T)4095;
	void *memory = aligned_alloc(4096, rounded ? rounded : 4096);
	if (memory)
		memset(memory, 0, rounded);
	else
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return memory;
}

BOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T, DWORD dwFreeType)
{
	if (dwFreeType & MEM_RELEASE)
		free(lpAddress);
	return TRUE;
}

BOOL WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD lpflOldProtect)
{
	if (lpflOldProtect)
		*lpflOldProtect = PAGE_READWRITE;
	return TRUE;
}

HANDLE WINAPI GetProcessHeap(void)
{
	return &s_processHeap;
}

HANDLE WINAPI HeapCreate(DWORD, SIZE_T, SIZE_T)
{
	return &s_processHeap;
}

BOOL WINAPI HeapDestroy(HANDLE)
{
	return TRUE;
}

LPVOID WINAPI HeapAlloc(HANDLE, DWORD dwFlags, SIZE_T dwBytes)
{
	void *memory = (dwFlags & HEAP_ZERO_MEMORY) ? calloc(1, dwBytes ? dwBytes : 1) : malloc(dwBytes ? dwBytes : 1);
	if (!memory)
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
	return memory;
}

LPVOID WINAPI HeapReAlloc(HANDLE, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes)
{
	const size_t oldSize = UsableSize(lpMem);
	void *memory = realloc(lpMem, dwBytes ? dwBytes : 1);
	if (memory && (dwFlags & HEAP_ZERO_MEMORY) && dwBytes > oldSize)
		memset(static_cast<char *>(memory) + oldSize, 0, dwBytes - oldSize);
	return memory;
}

BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID lpMem)
{
	free(lpMem);
	return TRUE;
}

SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID lpMem)
{
	return UsableSize(lpMem);
}

/* ---------------------------------------------------------------------------
** COM
** ------------------------------------------------------------------------- */

HRESULT WINAPI CoInitialize(LPVOID)
{
	return S_OK;
}

HRESULT WINAPI CoInitializeEx(LPVOID, DWORD)
{
	return S_OK;
}

void WINAPI CoUninitialize(void)
{
}

HRESULT WINAPI CoCreateInstance(REFCLSID, void *, DWORD, REFIID, LPVOID *ppv)
{
	if (ppv)
		*ppv = nullptr;
	return (HRESULT)0x80040154L; // REGDB_E_CLASSNOTREG
}

LPVOID WINAPI CoTaskMemAlloc(SIZE_T cb)
{
	return malloc(cb ? cb : 1);
}

void WINAPI CoTaskMemFree(LPVOID pv)
{
	free(pv);
}

HRESULT WINAPI OleInitialize(LPVOID)
{
	return S_OK;
}

void WINAPI OleUninitialize(void)
{
}

const IID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const IID IID_IClassFactory = { 0x00000001, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const IID IID_IDispatch = { 0x00020400, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

} // extern "C"
